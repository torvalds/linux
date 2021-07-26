/*
 * Copyright (c) 2016-2018, Fuzhou Rockchip Electronics Co., Ltd
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 * date: 2021-07-26
 * function: rk ftl v5 for rockchip soc base on arm v7 to support MLC NAND.
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
	.file	"rk_ftlv5_arm_v8.c"
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
	.type	ftl_set_blk_mode.part.17, %function
ftl_set_blk_mode.part.17:
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
	.size	ftl_set_blk_mode.part.17, .-ftl_set_blk_mode.part.17
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
	push	{r4, lr}
	.save {r4, lr}
	mov	r4, r1
	bne	.L106
	ldr	r3, .L162
.L107:
	lsl	r2, r0, #1
	strh	r0, [r2, r3]	@ movhi
	add	r0, r0, #1
	cmp	r0, #512
	bne	.L107
.L113:
	mov	r2, #2048
	mov	r1, #255
	ldr	r0, .L162+4
	uxth	r4, r4
	bl	ftl_memset
	ldr	r2, .L162
	mov	r3, #0
	add	r0, r2, #1024
.L108:
	uxth	r1, r3
	cmp	r4, r1
	bhi	.L141
	pop	{r4, pc}
.L106:
	cmp	r0, #1
	bne	.L109
	ldr	r1, .L162
	mov	r3, #0
.L112:
	cmp	r3, #3
	uxth	r2, r3
	bls	.L110
	tst	r2, #1
	movne	r0, #3
	moveq	r0, #2
	rsb	r2, r0, r2, lsl #1
	uxth	r2, r2
.L110:
	lsl	r0, r3, #1
	add	r3, r3, #1
	cmp	r3, #512
	strh	r2, [r0, r1]	@ movhi
	bne	.L112
	b	.L113
.L109:
	cmp	r0, #2
	bne	.L114
	ldr	r1, .L162
	mov	r2, #0
.L116:
	uxth	r3, r2
	cmp	r2, #1
	lsl	r0, r2, #1
	add	r2, r2, #1
	lslhi	r3, r3, #1
	subhi	r3, r3, #1
	uxthhi	r3, r3
	cmp	r2, #512
	strh	r3, [r0, r1]	@ movhi
	bne	.L116
	b	.L113
.L114:
	cmp	r0, #3
	bne	.L117
	ldr	r1, .L162
	mov	r3, #0
.L120:
	cmp	r3, #5
	uxth	r2, r3
	bls	.L118
	tst	r2, #1
	movne	r0, #5
	moveq	r0, #4
	rsb	r2, r0, r2, lsl #1
	uxth	r2, r2
.L118:
	lsl	r0, r3, #1
	add	r3, r3, #1
	cmp	r3, #512
	strh	r2, [r0, r1]	@ movhi
	bne	.L120
	b	.L113
.L117:
	cmp	r0, #4
	mov	r3, #0
	bne	.L121
	ldr	r2, .L162+8
	strh	r3, [r2, #84]	@ movhi
	mov	r3, #1
	strh	r3, [r2, #86]	@ movhi
	mov	r3, #2
	strh	r3, [r2, #88]	@ movhi
	mov	r3, #3
	strh	r3, [r2, #90]	@ movhi
	mov	r3, #5
	strh	r3, [r2, #94]	@ movhi
	mov	r3, #7
	strh	r3, [r2, #96]	@ movhi
	mov	r3, #8
	strh	r0, [r2, #92]	@ movhi
	strh	r3, [r2, #98]!	@ movhi
.L123:
	tst	r3, #1
	movne	r1, #7
	moveq	r1, #6
	rsb	r1, r1, r3, lsl #1
	add	r3, r3, #1
	uxth	r3, r3
	strh	r1, [r2, #2]!	@ movhi
	cmp	r3, #512
	bne	.L123
	b	.L113
.L121:
	cmp	r0, #5
	bne	.L124
	ldr	r2, .L162+8
	add	r1, r2, #84
.L125:
	lsl	r0, r3, #1
	strh	r3, [r0, r1]	@ movhi
	add	r3, r3, #1
	cmp	r3, #16
	bne	.L125
	add	r2, r2, #114
.L126:
	strh	r3, [r2, #2]!	@ movhi
	add	r3, r3, #2
	uxth	r3, r3
	cmp	r3, #1008
	bne	.L126
	b	.L113
.L124:
	cmp	r0, #6
	bne	.L127
	ldr	r0, .L162
	mov	r1, r3
.L130:
	cmp	r1, #5
	uxth	r2, r1
	bls	.L128
	tst	r2, #1
	movne	r2, #12
	moveq	r2, #10
	sub	r2, r3, r2
	uxth	r2, r2
.L128:
	lsl	ip, r1, #1
	add	r1, r1, #1
	cmp	r1, #512
	add	r3, r3, #3
	strh	r2, [ip, r0]	@ movhi
	uxth	r3, r3
	bne	.L130
	b	.L113
.L127:
	cmp	r0, #9
	bne	.L131
	ldr	r2, .L162+8
	movw	r1, #1021
	strh	r3, [r2, #84]	@ movhi
	mov	r3, #1
	strh	r3, [r2, #86]	@ movhi
	mov	r3, r2
	mov	r2, #2
	strh	r2, [r3, #88]!	@ movhi
	mov	r2, #3
.L132:
	strh	r2, [r3, #2]!	@ movhi
	add	r2, r2, #2
	uxth	r2, r2
	cmp	r2, r1
	bne	.L132
	b	.L113
.L131:
	cmp	r0, #10
	bne	.L133
	ldr	r2, .L162+8
	add	r1, r2, #84
.L134:
	lsl	r0, r3, #1
	strh	r3, [r0, r1]	@ movhi
	add	r3, r3, #1
	cmp	r3, #63
	bne	.L134
	add	r2, r2, #208
	movw	r1, #961
.L135:
	strh	r3, [r2, #2]!	@ movhi
	add	r3, r3, #2
	uxth	r3, r3
	cmp	r3, r1
	bne	.L135
	b	.L113
.L133:
	cmp	r0, #11
	bne	.L136
	ldr	r2, .L162+8
	mov	r3, #0
	add	r1, r2, #84
.L137:
	lsl	r0, r3, #1
	strh	r3, [r0, r1]	@ movhi
	add	r3, r3, #1
	cmp	r3, #8
	bne	.L137
	add	r2, r2, #98
.L139:
	tst	r3, #1
	movne	r1, #7
	moveq	r1, #6
	rsb	r1, r1, r3, lsl #1
	add	r3, r3, #1
	uxth	r3, r3
	strh	r1, [r2, #2]!	@ movhi
	cmp	r3, #512
	bne	.L139
	b	.L113
.L136:
	cmp	r0, #12
	bne	.L113
	ldr	r3, .L162+8
	mov	r2, #0
	strh	r2, [r3, #84]	@ movhi
	mov	r2, #1
	strh	r2, [r3, #86]	@ movhi
	mov	r2, #2
	strh	r2, [r3, #88]	@ movhi
	mov	r2, #3
	strh	r2, [r3, #90]!	@ movhi
	mov	r2, #4
.L140:
	sub	r1, r2, #1
	add	r1, r1, r2, lsr #1
	add	r2, r2, #1
	uxth	r2, r2
	strh	r1, [r3, #2]!	@ movhi
	cmp	r2, #512
	bne	.L140
	b	.L113
.L141:
	lsl	r1, r3, #1
	add	r3, r3, #1
	ldrh	r1, [r1, r2]
	lsl	ip, r1, #1
	strh	r1, [r0, ip]	@ movhi
	b	.L108
.L163:
	.align	2
.L162:
	.word	.LANCHOR0+84
	.word	.LANCHOR0+1108
	.word	.LANCHOR0
	.fnend
	.size	BuildFlashLsbPageTable, .-BuildFlashLsbPageTable
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
	push	{r4, r5, r6, r7, r8, lr}
	.save {r4, r5, r6, r7, r8, lr}
	add	r8, r1, r1, lsl #2
	ldr	r7, .L173
	mov	r6, r0
	mov	r5, #0
	add	r7, r1, r7
.L166:
	ldr	r3, .L173+4
	ldrb	r3, [r3]	@ zero_extendqisi2
	cmp	r5, r3
	bcc	.L170
	pop	{r4, r5, r6, r7, r8, pc}
.L170:
	ldr	r4, .L173+8
	mov	r3, #85
	str	r3, [r6, #8]
	mov	r0, #200
	ldrsb	r3, [r5, r4]
	str	r3, [r6, #4]
	bl	ndelay
	ldr	r3, .L173+12
	ldrb	r3, [r3]	@ zero_extendqisi2
	cmp	r3, #34
	addeq	r3, r5, r8
	addeq	r4, r4, r3
	ldrsbeq	r3, [r4, #5]
	beq	.L172
	cmp	r3, #35
	addeq	r3, r5, r8
	ldrsbne	r3, [r7]
	addeq	r4, r4, r3
	ldrsbeq	r3, [r4, #50]
.L172:
	str	r3, [r6]
	add	r5, r5, #1
	b	.L166
.L174:
	.align	2
.L173:
	.word	.LANCHOR1+396
	.word	g_maxRegNum
	.word	.LANCHOR1+256
	.word	g_retryMode
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
	ldr	r3, .L179
	push	{r4, r5, r6, r7, r8, r9, r10, lr}
	.save {r4, r5, r6, r7, r8, r9, r10, lr}
	mov	r4, #0
	ldr	r8, .L179+4
	mov	r6, r0
	mov	r7, r3
	mov	r9, #161
	add	r1, r3, r1, lsl #2
	mov	r10, r4
	add	r5, r1, #3
.L176:
	ldrb	r3, [r8]	@ zero_extendqisi2
	cmp	r4, r3
	bcc	.L177
	pop	{r4, r5, r6, r7, r8, r9, r10, pc}
.L177:
	str	r9, [r6, #8]
	mov	r0, #300
	str	r10, [r6]
	ldrsb	r3, [r7, r4]
	add	r4, r4, #1
	str	r3, [r6]
	ldrsb	r3, [r5, #1]!
	str	r3, [r6]
	bl	ndelay
	b	.L176
.L180:
	.align	2
.L179:
	.word	.LANCHOR1+404
	.word	g_maxRegNum
	.fnend
	.size	SamsungSetRRPara, .-SamsungSetRRPara
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
	ldr	r3, .L196
	push	{r4, r5, r6, r7, r8, r9, r10, lr}
	.save {r4, r5, r6, r7, r8, r9, r10, lr}
	mov	r6, #0
	ldr	r4, .L196+4
	ldrh	r0, [r3, #10]
	strb	r6, [r4, #3156]
	strb	r6, [r4, #3157]
	bl	FlashBlockAlignInit
	mov	r2, #8
	mov	r1, r6
	ldr	r0, .L196+8
	bl	ftl_memset
	mov	r2, #32
	mov	r1, r6
	add	r0, r4, #3168
	ldr	r9, .L196+12
	bl	ftl_memset
	mov	r2, #128
	mov	r1, r6
	add	r0, r4, #3200
	mov	r8, r9
	bl	ftl_memset
	ldr	r5, [r4, #48]
	add	r7, r5, #1
.L183:
	ldrb	r2, [r5]	@ zero_extendqisi2
	add	r1, r9, r6, lsl #3
	mov	r0, r7
	bl	FlashMemCmp8
	cmp	r0, #0
	bne	.L182
	ldrb	r3, [r4, #3156]	@ zero_extendqisi2
	add	r2, r4, r3, lsl #2
	str	r0, [r2, #3168]
	add	r2, r3, #1
	add	r3, r4, r3
	strb	r2, [r4, #3156]
	strb	r6, [r3, #3160]
.L182:
	add	r6, r6, #1
	cmp	r6, #4
	bne	.L183
	ldrb	r3, [r4, #3156]	@ zero_extendqisi2
	strb	r3, [r4, #3157]
	ldrb	r3, [r5, #8]	@ zero_extendqisi2
	cmp	r3, #2
	beq	.L184
.L188:
	ldrh	r2, [r5, #14]
	ldrb	r3, [r4, #3156]	@ zero_extendqisi2
	smulbb	r3, r3, r2
	ldrb	r2, [r5, #13]	@ zero_extendqisi2
	smulbb	r3, r3, r2
	ldr	r2, .L196+16
	strh	r3, [r2]	@ movhi
	pop	{r4, r5, r6, r7, r8, r9, r10, pc}
.L184:
	ldr	r9, [r4, #40]
	mov	r6, #0
.L187:
	ldrb	r2, [r5]	@ zero_extendqisi2
	add	r1, r8, r6, lsl #3
	mov	r0, r7
	bl	FlashMemCmp8
	cmp	r0, #0
	bne	.L185
	ldrh	r3, [r5, #14]
	ldrb	r2, [r4, #3156]	@ zero_extendqisi2
	and	r1, r3, #65280
	ldrb	r3, [r5, #13]	@ zero_extendqisi2
	mul	r3, r9, r3
	mul	r3, r3, r1
	add	r1, r4, r2, lsl #2
	str	r3, [r1, #3168]
	ldrb	r0, [r5, #23]	@ zero_extendqisi2
	cmp	r0, #0
	lslne	r3, r3, #1
	strne	r3, [r1, #3168]
	add	r3, r2, #1
	add	r2, r4, r2
	strb	r3, [r4, #3156]
	strb	r6, [r2, #3160]
.L185:
	add	r6, r6, #1
	cmp	r6, #4
	bne	.L187
	b	.L188
.L197:
	.align	2
.L196:
	.word	.LANCHOR1+468
	.word	.LANCHOR0
	.word	.LANCHOR0+3160
	.word	IDByte
	.word	.LANCHOR0+3328
	.fnend
	.size	FlashDieInfoInit, .-FlashDieInfoInit
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
	ldr	r1, .L200
	bl	ftl_memcpy
	mov	r0, #0
	pop	{r4, pc}
.L201:
	.align	2
.L200:
	.word	.LANCHOR0+3332
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
	ldr	r5, .L211
	ldr	r9, .L211+4
	add	r6, r5, #500
.L205:
	lsl	r8, r4, #5
	ldrb	r2, [r6, r4, lsl #5]	@ zero_extendqisi2
	mov	r1, r9
	add	r0, r8, #1
	add	r0, r6, r0
	bl	FlashMemCmp8
	subs	r7, r0, #0
	bne	.L203
	add	r5, r5, r8
	ldr	r2, .L211+8
	ldrb	r0, [r5, #522]	@ zero_extendqisi2
	add	r6, r6, r8
	mov	r3, r7
	mov	r1, r2
.L204:
	ldrb	ip, [r2, r3, lsl #5]	@ zero_extendqisi2
	cmp	ip, r0
	beq	.L207
	add	r3, r3, #1
	cmp	r3, #4
	bne	.L204
.L207:
	ldr	r4, .L211+12
	add	r1, r1, r3, lsl #5
	mov	r2, #32
	ldr	r0, .L211+16
	bl	ftl_memcpy
	mov	r2, #32
	mov	r1, r6
	mov	r0, r4
	bl	ftl_memcpy
	ldrh	r0, [r4, #10]
	bl	FlashBlockAlignInit
	b	.L202
.L203:
	add	r4, r4, #1
	cmp	r4, #86
	bne	.L205
	mvn	r7, #0
.L202:
	mov	r0, r7
	pop	{r4, r5, r6, r7, r8, r9, r10, pc}
.L212:
	.align	2
.L211:
	.word	.LANCHOR1
	.word	IDByte
	.word	.LANCHOR1+3252
	.word	.LANCHOR1+468
	.word	.LANCHOR0+52
	.fnend
	.size	FlashLoadPhyInfoInRam, .-FlashLoadPhyInfoInRam
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
	ldr	r3, .L214
	ldr	r2, [r3, #-2804]
	ldr	r1, [r2]
	str	r1, [r3, #-2800]
	ldr	r1, [r2, #4]
	str	r1, [r3, #-2796]
	ldr	r1, [r2, #8]
	str	r1, [r3, #-2792]
	ldr	r1, [r2, #12]
	str	r1, [r3, #-2788]
	ldr	r1, [r2, #304]
	str	r1, [r3, #-2784]
	ldr	r1, [r2, #308]
	str	r1, [r3, #-2780]
	ldr	r1, [r2, #336]
	ldr	r2, [r2, #344]
	str	r1, [r3, #-2776]
	str	r2, [r3, #-2772]
	bx	lr
.L215:
	.align	2
.L214:
	.word	.LANCHOR2
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
	ldr	r2, .L222
	mov	fp, r3
	mov	r10, r1
	mov	r7, r0
	ldr	r5, .L222+4
	ldrh	r3, [r2, #14]
	ldrh	r2, [r2, #12]
	ldrh	r6, [r5, #40]
	ldr	r4, [r0, #4]
	smulbb	r3, r3, r2
	ldrb	r2, [r5, #36]	@ zero_extendqisi2
	uxth	r3, r3
	cmp	r2, #1
	lsleq	r6, r6, #1
	ubfx	r2, r4, #10, #16
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
	ubfx	r0, r4, #0, #10
	bne	.L218
	ldr	r3, .L222+8
	ldrb	r3, [r3, #-2740]	@ zero_extendqisi2
	cmp	r3, #0
	addeq	r0, r5, r0, lsl #1
	ldrheq	r0, [r0, #84]
.L218:
	add	r5, r5, r8, lsl #2
	ldr	r3, [r5, #3168]
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
.L223:
	.align	2
.L222:
	.word	.LANCHOR2-2768
	.word	.LANCHOR0
	.word	.LANCHOR2
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
	ldr	r3, .L237
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
	bne	.L230
	cmp	r2, #0
	add	r3, r4, r3
	ldrbne	r2, [r0, #66]	@ zero_extendqisi2
	ldrbeq	r2, [r0, #65]	@ zero_extendqisi2
	str	r2, [r3, #2056]
	ldrb	r0, [r0, #67]	@ zero_extendqisi2
	cmp	r0, #0
	movne	r2, #0
	addne	ip, r4, r5, lsl #8
	bne	.L229
.L230:
	mov	r0, #80
	bl	ndelay
	ldr	r0, [r4, r5, lsl #8]
	uxtb	r0, r0
	pop	{r4, r5, r6, pc}
.L231:
	lsl	r3, r2, #3
	add	r2, r2, #1
	lsr	r3, r1, r3
	uxtb	r3, r3
	str	r3, [ip, #4]
.L229:
	cmp	r2, r0
	bcc	.L231
	b	.L230
.L238:
	.align	2
.L237:
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
.L240:
	mov	r2, r6
	mov	r1, r5
	mov	r0, r4
	bl	FlashReadStatusEN
	cmp	r0, #255
	beq	.L240
	tst	r0, #64
	popne	{r4, r5, r6, pc}
	mov	r1, #3
	mov	r0, #1
	bl	usleep_range
	b	.L240
	.fnend
	.size	FlashWaitReadyEN, .-FlashWaitReadyEN
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
	ldr	r2, .L250
	mov	ip, #1
	ldr	r0, .L250+4
	ldr	r3, [r2, #48]
	ldr	r1, [r2, #40]
	ldrb	r3, [r3, #9]	@ zero_extendqisi2
	smulbb	r3, r3, r1
	strh	r3, [r4, #4]	@ unaligned
	ldr	r3, .L250+8
	ldrb	r1, [r3, #-2739]	@ zero_extendqisi2
	ldr	r3, [r3, #-2736]
	strb	r1, [r4, #7]
	str	r3, [r4]	@ unaligned
	ldr	r3, [r2, #48]
	ldrb	r1, [r3, #9]	@ zero_extendqisi2
	strb	r1, [r4, #6]
	mov	r1, #32
	strb	r1, [r4, #8]
	ldrb	r1, [r2, #3156]	@ zero_extendqisi2
	ldrb	r3, [r3, #7]	@ zero_extendqisi2
	strb	r3, [r4, #9]
	mov	r3, #0
	strb	r3, [r4, #10]
.L247:
	uxtb	r2, r3
	cmp	r1, r2
	bhi	.L248
	pop	{r4, pc}
.L248:
	ldrb	lr, [r3, r0]	@ zero_extendqisi2
	add	r3, r3, #1
	ldrb	r2, [r4, #10]	@ zero_extendqisi2
	orr	r2, r2, ip, lsl lr
	strb	r2, [r4, #10]
	b	.L247
.L251:
	.align	2
.L250:
	.word	.LANCHOR0
	.word	.LANCHOR0+3160
	.word	.LANCHOR2
	.fnend
	.size	ftl_read_flash_info, .-ftl_read_flash_info
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
	ldr	r3, .L253
	ldr	r2, [r3, #-2732]
	str	r0, [r3, #-2732]
	mov	r0, r2
	bx	lr
.L254:
	.align	2
.L253:
	.word	.LANCHOR2
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
	ldr	r3, .L256
	ldr	r3, [r3, #48]
	ldrb	r0, [r3, #9]	@ zero_extendqisi2
	bx	lr
.L257:
	.align	2
.L256:
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
	ldr	r2, .L260
	add	r3, r2, r0, lsl #3
	ldr	r0, [r2, r0, lsl #3]
	ldrb	r3, [r3, #4]	@ zero_extendqisi2
	add	r3, r3, #8
	add	r0, r0, r3, lsl #8
	bx	lr
.L261:
	.align	2
.L260:
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
	ldr	r3, .L263
	ldr	r2, [r3, #-2804]
	lsl	r3, r0, #8
	orr	r0, r3, r0, lsl #16
	orr	r0, r0, #1
	str	r0, [r2, #304]
	bx	lr
.L264:
	.align	2
.L263:
	.word	.LANCHOR2
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
	ldr	r3, .L266
	orr	r0, r0, #16640
	ldr	r3, [r3, #-2804]
	str	r0, [r3, #344]
	bx	lr
.L267:
	.align	2
.L266:
	.word	.LANCHOR2
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
	ldr	r3, .L271
	cmp	r0, #0
	ldr	r2, [r3, #-2804]
	ldr	r3, [r2]
	bfieq	r3, r0, #13, #1
	orrne	r3, r3, #253952
	str	r3, [r2]
	bx	lr
.L272:
	.align	2
.L271:
	.word	.LANCHOR2
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
	ldr	r3, .L280
	ands	r1, r0, #6
	ldr	r2, [r3, #-2804]
	ldr	r3, [r2]
	bfieq	r3, r1, #13, #1
	beq	.L276
	movw	r1, #8322
	orr	r3, r3, #24576
	str	r1, [r2, #344]
	bfc	r3, #15, #1
	ldr	r1, .L280+4
	orr	r3, r3, #196608
	tst	r0, #4
	orrne	r3, r3, #32768
	str	r1, [r2, #304]
	mov	r1, #38
	str	r1, [r2, #308]
	mov	r1, #39
	str	r1, [r2, #308]
.L276:
	str	r3, [r2]
	mov	r0, #0
	bx	lr
.L281:
	.align	2
.L280:
	.word	.LANCHOR2
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
	ldr	r3, .L283
	mov	r2, #1
	ldr	r1, [r3, r0, lsl #3]
	add	r0, r3, r0, lsl #3
	ldrb	r0, [r0, #4]	@ zero_extendqisi2
	ldr	r3, [r1]
	lsl	r2, r2, r0
	bfi	r3, r2, #0, #8
	str	r3, [r1]
	bx	lr
.L284:
	.align	2
.L283:
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
	ldr	r3, .L286
	ldr	r2, [r3, r0, lsl #3]
	ldr	r3, [r2]
	bfc	r3, #0, #8
	bfc	r3, #17, #1
	str	r3, [r2]
	bx	lr
.L287:
	.align	2
.L286:
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
	mov	r8, r3
	ldr	r3, .L297
	mov	r7, r2
	mov	r5, r0
	mov	r6, r1
	ldr	r4, .L297+4
	ldr	r2, [r3, #48]
	ldrb	r2, [r2, #19]	@ zero_extendqisi2
	cmp	r2, #6
	bne	.L289
	mov	r2, #20
	sub	r4, r4, #8
	add	r2, r2, r0, lsl #6
	add	r2, r2, r8, lsl #2
.L296:
	add	r4, r4, r2
.L290:
	ldr	r9, [r3, r5, lsl #3]
	add	r3, r3, r5, lsl #3
	mov	r0, r5
	ldrb	fp, [r3, #4]	@ zero_extendqisi2
	sub	r6, r6, #1
	bl	NandcFlashCs
	mov	r3, #54
	sub	r4, r4, #1
	lsl	fp, fp, #8
	add	r10, r9, fp
	str	r3, [r10, #2056]
	sub	r3, r7, #1
	add	r7, r7, r6
.L293:
	cmp	r3, r7
	bne	.L294
	mov	r3, #22
	add	r9, r9, fp
	str	r3, [r9, #2056]
	mov	r0, r5
	bl	NandcFlashDeCs
	ldr	r3, .L297+8
	add	r5, r3, r5
	strb	r8, [r5, #-1876]
	add	sp, sp, #12
	@ sp needed
	pop	{r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L289:
	cmp	r2, #7
	bne	.L291
	mov	r2, #160
	mov	r1, #28
	smlabb	r1, r2, r0, r1
	mov	r2, #10
	sub	r4, r4, #8
	smlabb	r2, r2, r8, r1
	b	.L296
.L291:
	cmp	r2, #8
	addeq	r4, r4, #20
	addeq	r2, r8, r8, lsl #2
	beq	.L296
	add	r2, r8, #2
	add	r2, r2, r0, lsl #3
	add	r4, r4, r2, lsl #3
	sub	r4, r4, #4
	b	.L290
.L294:
	ldrb	r2, [r3, #1]!	@ zero_extendqisi2
	mov	r0, #200
	str	r2, [r10, #2052]
	str	r3, [sp, #4]
	bl	ndelay
	ldrsb	r2, [r4, #1]!
	ldr	r3, [sp, #4]
	str	r2, [r10, #2048]
	b	.L293
.L298:
	.align	2
.L297:
	.word	.LANCHOR0
	.word	.LANCHOR2-2720
	.word	.LANCHOR2
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
	ldr	r3, .L309
	ldr	r3, [r3, #48]
	ldrb	r3, [r3, #19]	@ zero_extendqisi2
	sub	r3, r3, #1
	cmp	r3, #7
	bxhi	lr
	push	{r4, r5, r6, lr}
	.save {r4, r5, r6, lr}
	mov	r4, #0
	ldr	r6, .L309+4
	sub	r5, r6, #2720
	sub	r5, r5, #4
.L302:
	ldr	r3, .L309+8
	uxtb	r0, r4
	ldrb	r3, [r3, r4, lsl #3]	@ zero_extendqisi2
	cmp	r3, #173
	bne	.L301
	mov	r3, #0
	mov	r2, r5
	ldrb	r1, [r6, #-2727]	@ zero_extendqisi2
	bl	HynixSetRRPara
.L301:
	add	r4, r4, #1
	cmp	r4, #4
	bne	.L302
	pop	{r4, r5, r6, pc}
.L310:
	.align	2
.L309:
	.word	.LANCHOR0
	.word	.LANCHOR2
	.word	IDByte
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
	ldr	r5, .L319
	add	r4, r5, r0, lsl #4
	ldr	r3, [r4, #3208]
	cmp	r3, #0
	beq	.L313
	ldrb	r7, [r4, #3200]	@ zero_extendqisi2
	mov	r6, r0
	add	r5, r5, r6, lsl #2
	mov	r0, r7
	bl	NandcFlashCs
	ldr	r2, [r5, #3168]
	mov	r0, r7
	ldr	r1, [r4, #3204]
	adds	r2, r2, #0
	movne	r2, #1
	bl	FlashWaitReadyEN
	mov	r1, r0
	mov	r0, r7
	bl	NandcFlashDeCs
	ldr	r3, [r4, #3208]
	sbfx	r0, r1, #0, #1
	str	r0, [r3]
	mov	r3, #0
	ldr	r2, [r4, #3212]
	str	r3, [r4, #3208]
	cmp	r2, r3
	strne	r0, [r2]
	strne	r3, [r4, #3212]
.L313:
	mov	r0, #0
	pop	{r4, r5, r6, r7, r8, pc}
.L320:
	.align	2
.L319:
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
	ldr	r3, .L329
	push	{r0, r1, r2, r4, r5, lr}
	.save {r4, r5, lr}
	.pad #12
	ldr	r4, .L329+4
	ldr	r5, [r3, r0, lsl #3]
.L325:
	ldr	r3, [r5]
	str	r3, [sp, #4]
	ldr	r3, [sp, #4]
	tst	r3, #512
	bne	.L326
	mov	r0, #10
	bl	ndelay
	subs	r4, r4, #1
	bne	.L325
	mvn	r0, #0
.L323:
	add	sp, sp, #12
	@ sp needed
	pop	{r4, r5, pc}
.L326:
	mov	r0, #0
	b	.L323
.L330:
	.align	2
.L329:
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
	ldr	r3, .L337
	ldr	r4, .L337+4
	ldr	r5, [r3, r0, lsl #3]
	mov	r0, #130
	bl	ndelay
.L333:
	ldr	r3, [r5]
	str	r3, [sp, #4]
	ldr	r3, [sp, #4]
	tst	r3, #512
	bne	.L334
	mov	r1, #2
	mov	r0, #1
	bl	usleep_range
	subs	r4, r4, #1
	bne	.L333
	mvn	r0, #0
.L331:
	add	sp, sp, #12
	@ sp needed
	pop	{r4, r5, pc}
.L334:
	mov	r0, #0
	b	.L331
.L338:
	.align	2
.L337:
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
	ldr	r3, .L341
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
.L342:
	.align	2
.L341:
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
	ldr	r6, .L350
	ldrb	r3, [r6, #-2740]	@ zero_extendqisi2
	cmp	r3, #0
	popeq	{r4, r5, r6, r7, r8, pc}
	mov	r5, r0
	bl	NandcFlashCs
	ldr	r3, .L350+4
	ldr	r7, [r3, r5, lsl #3]
	add	r3, r3, r5, lsl #3
	ldrb	r8, [r3, #4]	@ zero_extendqisi2
	ldr	r3, .L350+8
	ldrb	r3, [r3, r5, lsl #3]	@ zero_extendqisi2
	lsl	r8, r8, #8
	cmp	r3, #44
	bne	.L345
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
.L345:
	mov	r0, r5
	add	r7, r7, r8
	bl	NandcWaitFlashReadyNoDelay
	mov	r3, #218
	mov	r0, r5
	str	r3, [r7, #2056]
	bl	NandcWaitFlashReady
	mov	r3, #2
	strb	r3, [r6, #-1872]
	pop	{r4, r5, r6, r7, r8, pc}
.L351:
	.align	2
.L350:
	.word	.LANCHOR2
	.word	.LANCHOR0
	.word	IDByte
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
	ldr	r6, .L359
	ldrb	r3, [r6, #-2740]	@ zero_extendqisi2
	cmp	r3, #0
	popeq	{r4, r5, r6, r7, r8, pc}
	mov	r5, r0
	bl	NandcFlashCs
	ldr	r3, .L359+4
	ldr	r7, [r3, r5, lsl #3]
	add	r3, r3, r5, lsl #3
	ldrb	r8, [r3, #4]	@ zero_extendqisi2
	ldr	r3, .L359+8
	ldrb	r3, [r3, r5, lsl #3]	@ zero_extendqisi2
	lsl	r8, r8, #8
	cmp	r3, #44
	bne	.L354
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
.L354:
	mov	r0, r5
	add	r7, r7, r8
	bl	NandcWaitFlashReadyNoDelay
	mov	r3, #223
	mov	r0, r5
	str	r3, [r7, #2056]
	bl	NandcWaitFlashReady
	mov	r3, #0
	strb	r3, [r6, #-1872]
	pop	{r4, r5, r6, r7, r8, pc}
.L360:
	.align	2
.L359:
	.word	.LANCHOR2
	.word	.LANCHOR0
	.word	IDByte
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
	ldr	r3, .L386
	push	{r0, r1, r2, r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.save {r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.pad #12
	mov	lr, #0
	ldr	r4, .L386+4
	mov	r5, #239
	mov	r6, #128
	mov	r7, #1
	ldrb	r3, [r3, #-1871]	@ zero_extendqisi2
	mov	r9, #35
	mov	r8, r4
	mov	r10, #32
	and	r2, r3, #4
	and	r3, r3, #1
	str	r2, [sp, #4]
	mov	r2, lr
	str	r3, [sp]
.L373:
	ldr	r1, .L386+8
	add	r3, r4, lr
	ldrb	r3, [r3, #4]	@ zero_extendqisi2
	ldrb	ip, [lr, r1]	@ zero_extendqisi2
	cmp	ip, #69
	cmpne	ip, #152
	beq	.L364
	cmp	ip, #44
	cmpne	ip, #173
	bne	.L365
.L364:
	cmp	r0, #1
	ldr	r1, [r8, lr]
	bne	.L366
	ldr	fp, [sp]
	cmp	fp, #0
	beq	.L365
	lsl	r3, r3, #8
	cmp	ip, #173
	add	fp, r1, r3
	str	r5, [fp, #2056]
	streq	r0, [fp, #2052]
	beq	.L385
	cmp	ip, #44
	moveq	ip, #5
	streq	r0, [fp, #2052]
	strne	r6, [fp, #2052]
	streq	ip, [fp, #2048]
	strne	r0, [fp, #2048]
.L371:
	add	r3, r1, r3
	str	r2, [r3, #2048]
	str	r2, [r3, #2048]
	str	r2, [r3, #2048]
.L365:
	add	lr, lr, #8
	cmp	lr, #32
	bne	.L373
	mov	r0, #0
	bl	NandcWaitFlashReady
	mov	r0, #0
	add	sp, sp, #12
	@ sp needed
	pop	{r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L366:
	ldr	fp, [sp, #4]
	cmp	fp, #0
	beq	.L365
	lsl	r3, r3, #8
	cmp	ip, #173
	add	fp, r1, r3
	str	r5, [fp, #2056]
	streq	r7, [fp, #2052]
	streq	r10, [fp, #2048]
	beq	.L371
	cmp	ip, #44
	streq	r7, [fp, #2052]
	streq	r9, [fp, #2048]
	beq	.L371
	str	r6, [fp, #2052]
.L385:
	str	r2, [fp, #2048]
	b	.L371
.L387:
	.align	2
.L386:
	.word	.LANCHOR2
	.word	.LANCHOR0
	.word	IDByte
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
	ldr	ip, .L390
	ldr	r3, .L390+4
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
.L391:
	.align	2
.L390:
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
	ldr	r3, .L394
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
.L395:
	.align	2
.L394:
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
	ldr	r1, .L403
	add	r4, r4, r4, lsl #2
	ldr	r0, .L403+4
	mov	r2, #0
	ldr	ip, .L403+8
	sub	lr, r1, #45
.L397:
	ldrb	r3, [r0]	@ zero_extendqisi2
	cmp	r2, r3
	bcc	.L400
	mov	r0, #0
	pop	{r4, r5, r6, lr}
	b	NandcWaitFlashReady
.L400:
	ldrb	r3, [ip]	@ zero_extendqisi2
	cmp	r3, #67
	add	r3, r2, r4
	addeq	r3, lr, r3
	addne	r3, r1, r3
	ldrsb	r3, [r3, #5]
	add	r2, r2, #1
	str	r3, [r5]
	b	.L397
.L404:
	.align	2
.L403:
	.word	.LANCHOR1+301
	.word	g_maxRegNum
	.word	g_retryMode
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
	ldr	r0, .L407
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
.L408:
	.align	2
.L407:
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
	ldr	r8, .L420
	mov	r6, r0
	mov	r9, r1
	mov	r7, r5
	ldr	r10, .L420+4
.L410:
	cmp	r7, r9
	bne	.L415
	mov	r0, #0
	add	sp, sp, #16
	@ sp needed
	pop	{r4, r5, r6, r7, r8, r9, r10, pc}
.L415:
	sub	r3, r9, r7
	add	r2, sp, #8
	uxtb	r3, r3
	mov	r1, #0
	add	r0, r6, r5
	str	r3, [sp]
	add	r3, sp, #12
	bl	LogAddr2PhyAddr
	ldrb	r2, [r8, #3156]	@ zero_extendqisi2
	ldr	r3, [sp, #12]
	cmp	r2, r3
	mvnls	r3, #0
	strls	r3, [r6, r5]
	bls	.L412
	add	r2, r8, r3
	add	r3, r8, r3, lsl #4
	ldrb	r4, [r2, #3160]	@ zero_extendqisi2
	strb	r4, [r3, #3200]
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
	bne	.L414
	ldr	r1, [sp, #8]
	mov	r0, r10
	bl	printk
.L414:
	mov	r0, r4
	bl	NandcFlashDeCs
.L412:
	add	r7, r7, #1
	add	r5, r5, #36
	b	.L410
.L421:
	.align	2
.L420:
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
	ldr	r4, .L455
	.pad #20
	sub	sp, sp, #20
	ldrb	r5, [r4, #36]	@ zero_extendqisi2
	cmp	r5, #0
	moveq	r9, r0
	moveq	r10, r1
	beq	.L424
	mov	r1, r2
	bl	FlashEraseSLc2KBlocks
.L422:
	add	sp, sp, #20
	@ sp needed
	pop	{r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L433:
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
	ldrb	r3, [r4, #3156]	@ zero_extendqisi2
	mov	r7, r0
	ldr	r0, [sp, #12]
	cmp	r3, r0
	mvnls	r3, #0
	strls	r3, [r9, r6]
	bls	.L427
	ldr	r3, .L455+4
	ldrb	r3, [r3, #-1870]	@ zero_extendqisi2
	cmp	r3, #0
	add	r3, r4, r0, lsl #4
	moveq	r7, #0
	ldr	r3, [r3, #3208]
	cmp	r3, #0
	beq	.L429
	uxtb	r0, r0
	bl	FlashWaitCmdDone
.L429:
	ldr	r2, [sp, #12]
	cmp	r7, #0
	addne	r6, r6, #36
	mov	r0, #0
	addne	r6, r9, r6
	lsl	r3, r2, #4
	add	r2, r4, r2
	add	r1, r4, r3
	add	r3, r4, r3
	str	r0, [r1, #3212]
	ldr	r0, [sp, #8]
	strne	r6, [r1, #3212]
	ldrb	r6, [r2, #3160]	@ zero_extendqisi2
	str	r0, [r1, #3204]
	str	fp, [r1, #3208]
	mov	r0, r6
	strb	r6, [r3, #3200]
	bl	NandcFlashCs
	cmp	r10, #1
	mov	r0, r6
	bne	.L431
	ldr	r3, .L455+4
	ldrb	r3, [r3, #-2740]	@ zero_extendqisi2
	cmp	r3, #0
	beq	.L431
	bl	flash_enter_slc_mode
.L432:
	ldr	r3, [sp, #12]
	mov	r0, r6
	ldr	r1, [sp, #8]
	add	r5, r5, r7
	add	r3, r4, r3, lsl #2
	ldr	r2, [r3, #3168]
	adds	r2, r2, #0
	movne	r2, #1
	bl	FlashWaitReadyEN
	mov	r2, r7
	ldr	r1, [sp, #8]
	mov	r0, r6
	bl	FlashEraseCmd
	mov	r0, r6
	bl	NandcFlashDeCs
.L427:
	add	r5, r5, #1
.L424:
	cmp	r5, r8
	bcc	.L433
	ldr	r6, .L455+4
	mov	r5, #0
	ldr	r7, .L455+8
.L434:
	ldrb	r3, [r4, #3156]	@ zero_extendqisi2
	cmp	r5, r3
	bcc	.L436
	ldr	r3, .L455+4
	ldr	r3, [r3, #-1868]
	cmp	r3, #0
	bne	.L437
.L438:
	mov	r0, #0
	b	.L422
.L431:
	bl	flash_exit_slc_mode
	b	.L432
.L436:
	uxtb	r0, r5
	bl	FlashWaitCmdDone
	cmp	r10, #1
	bne	.L435
	ldrb	r3, [r6, #-2740]	@ zero_extendqisi2
	cmp	r3, #0
	beq	.L435
	ldrb	r0, [r7, r5, lsl #4]	@ zero_extendqisi2
	bl	flash_exit_slc_mode
.L435:
	add	r5, r5, #1
	b	.L434
.L437:
	ldr	r3, .L455+12
	ldrb	r3, [r3]	@ zero_extendqisi2
	cmp	r3, #69
	moveq	r3, #0
	moveq	r2, #36
	moveq	r1, r3
	bne	.L438
.L439:
	cmp	r3, r8
	beq	.L438
	mul	r0, r2, r3
	add	r3, r3, #1
	str	r1, [r9, r0]
	b	.L439
.L456:
	.align	2
.L455:
	.word	.LANCHOR0
	.word	.LANCHOR2
	.word	.LANCHOR0+3200
	.word	IDByte
	.fnend
	.size	FlashEraseBlocks, .-FlashEraseBlocks
	.align	2
	.global	HynixGetReadRetryDefault
	.syntax unified
	.arm
	.fpu softvfp
	.type	HynixGetReadRetryDefault, %function
HynixGetReadRetryDefault:
	.fnstart
	@ args = 0, pretend = 0, frame = 48
	@ frame_needed = 0, uses_anonymous_args = 0
	ldr	r3, .L574
	mvn	r2, #83
	push	{r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.save {r4, r5, r6, r7, r8, r9, r10, fp, lr}
	mov	r4, r0
	cmp	r4, #2
	mvn	r1, #81
	.pad #52
	sub	sp, sp, #52
	strb	r0, [r3, #-2728]
	mvn	r0, #82
	strb	r2, [r3, #-2724]
	mvn	r2, #80
	strb	r0, [r3, #-2723]
	strb	r1, [r3, #-2722]
	strb	r2, [r3, #-2721]
	bne	.L458
	mvn	r2, #88
	mov	r5, #7
	strb	r2, [r3, #-2724]
	mvn	r2, #8
	ldr	r3, .L574+4
	strb	r2, [r3, #3397]
.L523:
	mov	r6, #4
	b	.L459
.L458:
	cmp	r4, #3
	bne	.L460
	mvn	r2, #79
	strb	r2, [r3, #-2724]
	mvn	r2, #78
	strb	r2, [r3, #-2723]
	mvn	r2, #77
	strb	r2, [r3, #-2722]
	mvn	r2, #76
	strb	r2, [r3, #-2721]
	mvn	r2, #75
	strb	r2, [r3, #-2720]
	mvn	r2, #74
	strb	r2, [r3, #-2719]
	mvn	r2, #73
	strb	r2, [r3, #-2718]
	mvn	r2, #72
.L568:
	mov	r5, #8
	strb	r2, [r3, #-2717]
	mov	r6, r5
.L459:
	sub	r3, r4, #1
	cmp	r3, #1
	movls	r9, #0
	ldrls	r10, .L574+8
	bls	.L466
	sub	r3, r4, #3
	cmp	r3, #5
	bhi	.L473
	smulbb	r3, r6, r5
	asr	r2, r3, #1
	lsl	r3, r3, #4
	str	r3, [sp, #44]
	lsl	r3, r2, #2
	str	r2, [sp, #4]
	str	r3, [sp, #36]
	lsl	r3, r2, #1
	str	r3, [sp, #24]
	mov	r3, #0
.L573:
	str	r3, [sp, #20]
	ldrb	r3, [sp, #20]	@ zero_extendqisi2
	str	r3, [sp, #8]
	ldr	r3, .L574+12
	ldr	r2, [sp, #8]
	ldrb	r3, [r3, #3156]	@ zero_extendqisi2
	cmp	r3, r2
	bls	.L473
	ldr	r2, [sp, #8]
	ldr	r3, .L574+12
	add	r3, r3, r2
	ldrb	r9, [r3, #3160]	@ zero_extendqisi2
	ldr	r3, .L574+12
	mov	r0, r9
	ldr	fp, [r3, r9, lsl #3]
	add	r3, r3, r9, lsl #3
	ldrb	r10, [r3, #4]	@ zero_extendqisi2
	mov	r3, #255
	add	r7, fp, r10, lsl #8
	str	r3, [r7, #2056]
	bl	NandcWaitFlashReady
	cmp	r4, #7
	bne	.L475
	ldr	r3, .L574+16
	mov	r0, #160
	mla	r0, r0, r9, r3
	add	r3, r0, #20
.L569:
	str	r3, [sp, #16]
	cmp	r4, #4
	add	r3, fp, r10, lsl #8
	mov	r2, #54
	str	r2, [r3, #2056]
	bne	.L478
	mov	r2, #255
	str	r2, [r3, #2052]
	mov	r2, #64
	str	r2, [r3, #2048]
	mov	r2, #204
.L570:
	str	r2, [r3, #2052]
	mov	r2, #77
	b	.L571
.L460:
	cmp	r4, #4
	bne	.L461
	mvn	ip, #51
	strb	r0, [r3, #-2719]
	strb	ip, [r3, #-2724]
	mvn	ip, #64
	strb	ip, [r3, #-2723]
	mvn	ip, #85
	strb	ip, [r3, #-2722]
	mvn	ip, #84
	strb	ip, [r3, #-2721]
	mvn	ip, #50
	strb	ip, [r3, #-2720]
	strb	r1, [r3, #-2718]
	b	.L568
.L461:
	cmp	r4, #5
	bne	.L462
	mov	r2, #56
	mov	r5, #8
	strb	r2, [r3, #-2724]
	mov	r2, #57
	strb	r2, [r3, #-2723]
	mov	r2, #58
	strb	r2, [r3, #-2722]
	mov	r2, #59
	strb	r2, [r3, #-2721]
	b	.L523
.L462:
	cmp	r4, #6
	bne	.L463
	mov	r2, #14
	mov	r5, #12
	strb	r2, [r3, #-2724]
	mov	r2, #15
	strb	r2, [r3, #-2723]
	mov	r2, #16
	strb	r2, [r3, #-2722]
	mov	r2, #17
	strb	r2, [r3, #-2721]
	b	.L523
.L463:
	cmp	r4, #7
	bne	.L464
	mvn	r2, #79
	mov	r5, #12
	strb	r2, [r3, #-2724]
	mvn	r2, #78
	strb	r2, [r3, #-2723]
	mvn	r2, #77
	strb	r2, [r3, #-2722]
	mvn	r2, #76
	strb	r2, [r3, #-2721]
	mvn	r2, #75
	strb	r2, [r3, #-2720]
	mvn	r2, #74
	strb	r2, [r3, #-2719]
	mvn	r2, #73
	strb	r2, [r3, #-2718]
	mvn	r2, #72
	strb	r2, [r3, #-2717]
	mvn	r2, #43
	strb	r2, [r3, #-2716]
	mvn	r2, #42
	strb	r2, [r3, #-2715]
	mov	r6, #10
	b	.L459
.L464:
	cmp	r4, #8
	mov	r5, #7
	bne	.L523
	mov	r2, #6
	strb	r5, [r3, #-2723]
	strb	r2, [r3, #-2724]
	mov	r2, #9
	strb	r2, [r3, #-2721]
	mov	r2, #10
	strb	r4, [r3, #-2722]
	mov	r5, #50
	strb	r2, [r3, #-2720]
	mov	r6, #5
	b	.L459
.L472:
	add	r2, r3, r2
	ldr	r4, .L574+8
	ldrb	r2, [r2, #3160]	@ zero_extendqisi2
	mov	r7, #0
	mov	fp, #55
	ldr	r8, [r3, r2, lsl #3]
	add	r3, r3, r2, lsl #3
	add	r4, r4, r2, lsl #6
	ldrb	r3, [r3, #4]	@ zero_extendqisi2
	add	r4, r4, #20
	add	r8, r8, r3, lsl #8
.L467:
	add	r3, r10, r7
	str	fp, [r8, #2056]
	ldrb	r3, [r3, #4]	@ zero_extendqisi2
	mov	r0, #80
	str	r3, [r8, #2052]
	bl	ndelay
	ldr	r3, [r8, #2048]
	strb	r3, [r4, r7]
	add	r7, r7, #1
	uxtb	r3, r7
	cmp	r6, r3
	bhi	.L467
	ldr	lr, .L574+20
	mov	r1, r4
	mov	r2, #0
.L470:
	mov	r3, #1
	add	ip, lr, r2
.L469:
	ldrb	r0, [ip, r3, lsl #2]	@ zero_extendqisi2
	ldrb	r7, [r1]	@ zero_extendqisi2
	add	r0, r0, r7
	strb	r0, [r1, r3, lsl #3]
	add	r3, r3, #1
	cmp	r3, #7
	bne	.L469
	add	r2, r2, #1
	add	r1, r1, #1
	cmp	r2, #4
	bne	.L470
	add	r9, r9, #1
	mov	r3, #0
	strb	r3, [r4, #16]
	strb	r3, [r4, #24]
	strb	r3, [r4, #32]
	strb	r3, [r4, #40]
	strb	r3, [r4, #48]
	strb	r3, [r4, #41]
	strb	r3, [r4, #49]
.L466:
	ldr	r3, .L574+12
	uxtb	r2, r9
	ldrb	r1, [r3, #3156]	@ zero_extendqisi2
	cmp	r1, r2
	bhi	.L472
.L473:
	ldr	r3, .L574
	strb	r6, [r3, #-2727]
	strb	r5, [r3, #-2726]
	add	sp, sp, #52
	@ sp needed
	pop	{r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L475:
	cmp	r4, #8
	beq	.L477
	ldr	r3, .L574+16
	add	r0, r3, r9, lsl #6
	add	r3, r0, #12
	b	.L569
.L478:
	sub	r2, r4, #5
	cmp	r2, #1
	bhi	.L480
	ldr	r2, .L574
	ldrb	r2, [r2, #-2724]	@ zero_extendqisi2
	str	r2, [r3, #2052]
	mov	r2, #82
.L571:
	str	r2, [r3, #2048]
.L479:
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
.L522:
	add	r3, fp, r10, lsl #8
	mov	r2, #48
	mov	r0, r9
	str	r2, [r3, #2056]
	bl	NandcWaitFlashReady
	sub	r3, r4, #5
	cmp	r4, #8
	cmpne	r3, #1
	str	r3, [sp, #40]
	movls	r2, #16
	bls	.L483
	cmp	r4, #7
	moveq	r2, #32
	movne	r2, #2
.L483:
	ldr	r3, .L574
	sub	r2, r2, #1
	add	ip, fp, r10, lsl #8
	ldr	r3, [r3, #-1864]
	str	ip, [sp]
	sub	r1, r3, #1
	uxtab	r2, r3, r2
	mov	r0, r1
.L484:
	ldr	ip, [sp]
	ldr	ip, [ip, #2048]
	strb	ip, [r0, #1]!
	cmp	r2, r0
	bne	.L484
	cmp	r4, #8
	bne	.L485
	mov	r2, #0
.L487:
	ldrb	r0, [r3, r2, lsl #2]	@ zero_extendqisi2
	uxtb	r1, r2
	cmp	r0, #50
	beq	.L486
	add	r0, r3, r2, lsl #2
	ldrb	r0, [r0, #1]	@ zero_extendqisi2
	cmp	r0, #5
	beq	.L486
	add	r2, r2, #1
	cmp	r2, #8
	bne	.L487
.L488:
	mov	r1, #0
	ldr	r0, .L574+24
	bl	printk
.L490:
	b	.L490
.L480:
	cmp	r4, #7
	bne	.L479
	mov	r2, #174
	str	r2, [r3, #2052]
	mov	r2, #0
	str	r2, [r3, #2048]
	mov	r2, #176
	b	.L570
.L486:
	cmp	r1, #6
	bhi	.L488
.L489:
	ldr	r1, .L574
	ldr	r2, [r1, #-1864]
	mov	r3, r2
.L499:
	ldr	ip, [sp, #44]
	sub	r0, r3, r2
	cmp	r0, ip
	blt	.L500
	ldr	r3, [sp, #24]
	ldr	r1, [r1, #-1864]
	add	r0, r1, r3
	mov	r3, #8
.L502:
	mov	lr, r0
	mov	ip, #0
.L501:
	ldrh	r7, [lr]
	add	ip, ip, #1
	mvn	r7, r7
	strh	r7, [lr], #2	@ movhi
	ldr	r7, [sp, #4]
	cmp	r7, ip
	bgt	.L501
	ldr	ip, [sp, #36]
	subs	r3, r3, #1
	add	r0, r0, ip
	bne	.L502
	str	r3, [sp, #12]
.L508:
	mov	ip, #0
	mov	r0, ip
.L507:
	mov	lr, #1
	mov	r7, #0
	lsl	lr, lr, r0
	mov	r3, #16
	str	r3, [sp, #32]
	str	lr, [sp, #28]
	mov	lr, r1
.L505:
	ldrh	r8, [lr]
	mov	r3, r8
	ldr	r8, [sp, #28]
	bics	r3, r8, r3
	ldr	r3, [sp, #24]
	addeq	r7, r7, #1
	add	lr, lr, r3
	ldr	r3, [sp, #32]
	subs	r3, r3, #1
	str	r3, [sp, #32]
	bne	.L505
	cmp	r7, #8
	add	r0, r0, #1
	ldrhi	r3, [sp, #28]
	orrhi	ip, ip, r3
	uxthhi	ip, ip
	cmp	r0, #16
	bne	.L507
	ldr	r3, [sp, #12]
	strh	ip, [r1], #2	@ movhi
	add	r3, r3, #1
	str	r3, [sp, #12]
	ldr	r0, [sp, #12]
	ldr	r3, [sp, #4]
	cmp	r3, r0
	bgt	.L508
	ldr	r3, .L574
	ldr	r1, [r3, #-1864]
	mov	r3, #0
	sub	r0, r1, #4
	add	ip, r1, #28
.L511:
	ldr	lr, [r0, #4]!
	cmp	lr, #0
	addeq	r3, r3, #1
	cmp	ip, r0
	bne	.L511
	cmp	r3, #7
	ble	.L512
	ldr	r0, .L574+28
	mov	r3, #1024
	mov	r2, #1
	bl	rknand_print_hex
	mov	r1, #0
	ldr	r0, .L574+24
	bl	printk
.L513:
	b	.L513
.L485:
	cmp	r4, #7
	bne	.L491
	mov	r2, #0
.L493:
	ldrb	r0, [r3, r2, lsl #2]	@ zero_extendqisi2
	uxtb	r1, r2
	cmp	r0, #12
	beq	.L492
	add	r0, r3, r2, lsl #2
	ldrb	r0, [r0, #1]	@ zero_extendqisi2
	cmp	r0, #10
	beq	.L492
	add	r2, r2, #1
	cmp	r2, #8
	bne	.L493
.L494:
	mov	r1, #0
	ldr	r0, .L574+24
	bl	printk
.L495:
	b	.L495
.L492:
	cmp	r1, #6
	bls	.L489
	b	.L494
.L491:
	cmp	r4, #6
	bne	.L489
	add	r3, r3, #7
.L496:
	ldrb	r2, [r1, #1]!	@ zero_extendqisi2
	cmp	r2, #12
	beq	.L489
	ldrb	r2, [r1, #8]	@ zero_extendqisi2
	cmp	r2, #4
	beq	.L489
	cmp	r3, r1
	bne	.L496
	mov	r1, #0
	ldr	r0, .L574+24
	bl	printk
.L498:
	b	.L498
.L500:
	ldr	r0, [sp]
	ldr	r0, [r0, #2048]
	strb	r0, [r3], #1
	b	.L499
.L512:
	cmp	r4, #6
	moveq	ip, #4
	beq	.L514
	cmp	r4, #7
	moveq	ip, #10
	beq	.L514
	cmp	r4, #8
	moveq	ip, #5
	movne	ip, #8
.L514:
	sub	r3, r6, #1
	ldr	r0, [sp, #16]
	uxtb	r3, r3
	mov	lr, #0
	add	r3, r3, #1
.L515:
	mov	r8, r0
	mov	r1, r2
.L516:
	ldrb	r7, [r1], #1	@ zero_extendqisi2
	strb	r7, [r8], #1
	sub	r7, r1, r2
	uxtb	r7, r7
	cmp	r6, r7
	bhi	.L516
	add	lr, lr, #1
	add	r2, r2, r3
	cmp	r5, lr
	add	r0, r0, ip
	bgt	.L515
	add	r10, fp, r10, lsl #8
	mov	r3, #255
	mov	r0, r9
	str	r3, [r10, #2056]
	bl	NandcWaitFlashReady
	ldr	r3, [sp, #40]
	cmp	r3, #1
	bhi	.L518
	mov	r3, #54
	ldr	r2, [sp]
	str	r3, [r10, #2056]
	mvn	r1, #0
	ldr	r3, .L574
	ldr	r0, [sp, #8]
	ldrb	r3, [r3, #-2724]	@ zero_extendqisi2
	str	r3, [r2, #2052]
	mov	r3, #0
	str	r3, [r2, #2048]
	mov	r3, #22
	str	r3, [r10, #2056]
	bl	FlashReadCmd
.L519:
	mov	r0, r9
	bl	NandcWaitFlashReady
	ldr	r3, [sp, #20]
	add	r3, r3, #1
	b	.L573
.L518:
	cmp	r4, #8
	moveq	r3, #190
	movne	r3, #56
	str	r3, [r10, #2056]
	b	.L519
.L477:
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
	ldr	r3, .L574+32
	str	r3, [sp, #16]
	b	.L522
.L575:
	.align	2
.L574:
	.word	.LANCHOR2
	.word	.LANCHOR1
	.word	.LANCHOR2-2728
	.word	.LANCHOR0
	.word	.LANCHOR2-2720
	.word	.LANCHOR1+3380
	.word	.LC2
	.word	.LC3
	.word	.LANCHOR2-2700
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
	bhi	.L578
	b	HynixGetReadRetryDefault
.L578:
	cmp	r3, #49
	bne	.L579
	ldr	r0, .L590
	mov	r2, #64
	ldr	r1, .L590+4
	strb	r3, [r0, #-2728]
	mov	r3, #4
	strb	r3, [r0, #-2727]
	mov	r3, #15
	strb	r3, [r0, #-2726]
.L588:
	sub	r0, r0, #2720
	sub	r0, r0, #4
	b	ftl_memcpy
.L579:
	sub	r2, r3, #65
	cmp	r3, #33
	cmpne	r2, #1
	bhi	.L580
	ldr	r0, .L590
	strb	r3, [r0, #-2728]
	mov	r3, #4
.L589:
	strb	r3, [r0, #-2727]
	mov	r3, #7
	strb	r3, [r0, #-2726]
	mov	r2, #45
	ldr	r1, .L590+8
	b	.L588
.L580:
	cmp	r3, #67
	cmpne	r3, #34
	ldreq	r0, .L590
	strbeq	r3, [r0, #-2728]
	moveq	r3, #5
	beq	.L589
.L581:
	cmp	r3, #68
	cmpne	r3, #35
	bxne	lr
	ldr	r0, .L590
	mov	r2, #95
	ldr	r1, .L590+12
	strb	r3, [r0, #-2728]
	mov	r3, #5
	strb	r3, [r0, #-2727]
	mov	r3, #17
	strb	r3, [r0, #-2726]
	b	.L588
.L591:
	.align	2
.L590:
	.word	.LANCHOR2
	.word	.LANCHOR1+404
	.word	.LANCHOR1+256
	.word	.LANCHOR1+301
	.fnend
	.size	FlashGetReadRetryDefault, .-FlashGetReadRetryDefault
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
	ldr	r0, .L598
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
	bne	.L593
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
.L595:
	mov	r1, r8
	mov	r0, r7
	pop	{r4, r5, r6, r7, r8, r9, r10, lr}
	b	FlashSetRandomizer
.L593:
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
	b	.L595
.L599:
	.align	2
.L598:
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
	ldr	r4, .L611
	bl	NandcWaitFlashReady
	bl	FlashSetReadRetryDefault
	ldr	r0, [r4, #-1860]
	cmp	r0, #0
	beq	.L601
	mov	r0, #0
	bl	flash_enter_slc_mode
.L602:
	ldrb	r3, [r4, #-1856]	@ zero_extendqisi2
	cmp	r3, #0
	beq	.L603
	ldrb	r3, [r4, #-1871]	@ zero_extendqisi2
	tst	r3, #1
	beq	.L603
	mov	r0, #1
	bl	FlashSetInterfaceMode
	mov	r0, #1
	bl	NandcSetMode
	mov	r3, #0
	strb	r3, [r4, #-1856]
.L603:
	ldr	r3, .L611+4
	mov	r0, #0
	ldr	r3, [r3]
	str	r0, [r3, #336]
	pop	{r4, pc}
.L601:
	bl	flash_exit_slc_mode
	b	.L602
.L612:
	.align	2
.L611:
	.word	.LANCHOR2
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
	ldr	r3, .L614
	ldr	r3, [r3, r0, lsl #3]
	str	r1, [r3, #336]
	bx	lr
.L615:
	.align	2
.L614:
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
	ldr	r1, .L627
	bl	__aeabi_idiv
	ldr	r3, .L627+4
	cmp	r0, #250
	movwgt	r2, #8354
	ldr	r3, [r3, #-2804]
	bgt	.L625
	cmp	r0, #220
	ble	.L619
.L626:
	movw	r2, #8322
	b	.L625
.L619:
	cmp	r0, #185
	movwgt	r2, #4226
	bgt	.L625
	cmp	r0, #160
	movwgt	r2, #4194
	bgt	.L625
	cmp	r4, #35
	movwls	r2, #4193
	bls	.L625
	cmp	r4, #99
	movwls	r2, #4225
	bhi	.L626
.L625:
	str	r2, [r3, #4]
	pop	{r4, pc}
.L628:
	.align	2
.L627:
	.word	1000000
	.word	.LANCHOR2
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
	bls	.L630
	movw	r3, #8322
	cmp	r0, r3
	bne	.L631
.L630:
	ldr	r3, .L632
	ldr	r3, [r3, #-2804]
	str	r0, [r3, #4]
.L631:
	ldr	r3, .L632+4
	ldrb	r0, [r3, #489]	@ zero_extendqisi2
	b	NandcTimeCfg
.L633:
	.align	2
.L632:
	.word	.LANCHOR2
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
	ldr	r3, .L637
	mov	r1, #1
	push	{r4, r5, r6, lr}
	.save {r4, r5, r6, lr}
	mov	r2, #0
	ldr	r4, .L637+4
	mov	r5, #0
	str	r1, [r3, #12]
	mov	r1, #2
	str	r1, [r3, #20]
	mov	r1, #3
	stm	r3, {r0, r2}
	str	r0, [r4, #-2804]
	str	r0, [r3, #8]
	str	r0, [r3, #16]
	str	r1, [r3, #28]
	str	r0, [r3, #24]
	ldr	r3, [r0]
	and	r3, r3, #253952
	ubfx	r1, r3, #13, #1
	bfi	r3, r2, #13, #1
	ldr	r2, [r0, #352]
	orr	r3, r3, #256
	str	r1, [r4, #-1852]
	movw	r1, #2049
	ubfx	r2, r2, #16, #4
	str	r2, [r4, #-1848]
	ldr	r2, [r0, #352]
	cmp	r2, r1
	str	r2, [r4, #-1844]
	moveq	r2, #8
	streq	r2, [r4, #-1848]
	str	r3, [r0]
	mov	r0, #40
	ldr	r3, [r4, #-2804]
	str	r5, [r3, #336]
	bl	NandcTimeCfg
	ldr	r3, [r4, #-2804]
	movw	r2, #8322
	mov	r0, #36864
	str	r2, [r3, #344]
	ldr	r2, .L637+8
	str	r2, [r3, #304]
	bl	ftl_dma32_malloc
	str	r0, [r4, #-1840]
	str	r0, [r4, #-1836]
	add	r0, r0, #32768
	str	r0, [r4, #-1832]
	str	r5, [r4, #-1812]
	str	r5, [r4, #-1804]
	pop	{r4, r5, r6, pc}
.L638:
	.align	2
.L637:
	.word	.LANCHOR0
	.word	.LANCHOR2
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
	ldr	ip, .L641
	str	lr, [sp, #-4]!
	.save {lr}
	ldr	lr, [ip, #-2804]
	ldr	lr, [lr, #4]
	str	lr, [r0]
	ldr	r0, [ip, #-2804]
	ldr	r0, [r0]
	str	r0, [r1]
	ldr	r1, [ip, #-2804]
	ldr	r1, [r1, #304]
	str	r1, [r2]
	ldr	r1, [ip, #-2804]
	ldr	r2, [r1, #308]
	ldr	r1, [r1, #344]
	uxtb	r2, r2
	orr	r2, r2, r1, lsl #16
	str	r2, [r3]
	ldr	pc, [sp], #4
.L642:
	.align	2
.L641:
	.word	.LANCHOR2
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
	ldr	r3, .L651
	mov	ip, #1
	mov	r1, #0
	ldr	r2, [r3, #-2804]
	str	r0, [r3, #-1800]
	mov	r3, r1
	str	ip, [r2, #8]
	mov	ip, #16
	cmp	r0, ip
	bfi	r3, ip, #8, #8
	bfi	r3, r1, #18, #1
	bne	.L644
.L647:
	bfc	r3, #4, #1
.L645:
	orr	r3, r3, #1
	str	r3, [r2, #12]
	bx	lr
.L644:
	cmp	r0, #24
	orreq	r3, r3, #16
	beq	.L645
	cmp	r0, #40
	orr	r3, r3, #262144
	orr	r3, r3, #16
	bne	.L645
	b	.L647
.L652:
	.align	2
.L651:
	.word	.LANCHOR2
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
	ldr	r3, .L654
	strb	r0, [r3, #-2739]
	b	NandcBchSel
.L655:
	.align	2
.L654:
	.word	.LANCHOR2
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
	ldr	r3, .L665
	push	{r4, r5, r6, lr}
	.save {r4, r5, r6, lr}
	mov	r5, #0
	ldr	r6, .L665+4
	mov	r4, r3
	ldr	r2, [r3, #-2804]
	ldr	r1, [r3, #-2800]
	str	r1, [r2]
	ldr	r1, [r3, #-2796]
	ldr	r2, [r3, #-2804]
	str	r1, [r2, #4]
	ldr	r1, [r3, #-2792]
	str	r1, [r2, #8]
	ldr	r1, [r3, #-2788]
	str	r1, [r2, #12]
	ldr	r1, [r3, #-2784]
	str	r1, [r2, #304]
	ldr	r1, [r3, #-2780]
	str	r1, [r2, #308]
	ldr	r1, [r3, #-2776]
	str	r1, [r2, #336]
	ldr	r1, [r3, #-2772]
	str	r1, [r2, #344]
.L658:
	ldrb	r3, [r6, r5, lsl #3]	@ zero_extendqisi2
	sub	r3, r3, #1
	uxtb	r3, r3
	cmp	r3, #253
	bhi	.L657
	uxtb	r0, r5
	bl	FlashReset
.L657:
	add	r5, r5, #1
	cmp	r5, #4
	bne	.L658
	ldrb	r3, [r4, #-1856]	@ zero_extendqisi2
	cmp	r3, #0
	beq	.L659
	mov	r0, #1
	bl	NandcSetMode
	ldrb	r0, [r4, #-1871]	@ zero_extendqisi2
	bl	FlashSetInterfaceMode
	ldrb	r0, [r4, #-1871]	@ zero_extendqisi2
	bl	NandcSetMode
	ldrb	r0, [r4, #-2783]	@ zero_extendqisi2
	bl	NandcSetDdrPara
.L659:
	ldr	r3, .L665+8
	pop	{r4, r5, r6, lr}
	ldr	r3, [r3, #48]
	ldrb	r0, [r3, #20]	@ zero_extendqisi2
	b	FlashBchSel
.L666:
	.align	2
.L665:
	.word	.LANCHOR2
	.word	IDByte
	.word	.LANCHOR0
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
	ldr	r3, .L671
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
.L672:
	.align	2
.L671:
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
.L674:
	ldr	r3, [r0, #8]
	str	r3, [sp, #4]
	ldr	r3, [sp, #4]
	tst	r3, #1048576
	beq	.L674
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
	ldr	ip, .L696
	push	{r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.save {r4, r5, r6, r7, r8, r9, r10, fp, lr}
	mov	r4, #0
	ldr	r5, .L696+4
	.pad #20
	sub	sp, sp, #20
	ldr	r6, [ip, r0, lsl #3]
	add	ip, ip, r0, lsl #3
	ldr	r8, [sp, #56]
	ldrb	r0, [ip, #4]	@ zero_extendqisi2
	mov	ip, #16
	ldr	r7, [r6, #12]
	bfi	r7, ip, #8, #8
	bfi	r7, r4, #3, #1
	bfi	r4, r1, #1, #1
	bfi	r7, r0, #5, #3
	orr	r4, r4, #8
	mov	r0, #1
	bfi	r4, r0, #5, #2
	lsr	r3, r3, r0
	orr	r4, r4, #536870912
	orr	r4, r4, #1024
	bfi	r4, r3, #4, #1
	ldr	r3, [r5, #-1848]
	cmp	r3, #3
	bls	.L679
	ldr	r3, [r6, #16]
	str	r3, [sp, #12]
	ldr	r3, [sp, #12]
	bfc	r3, #2, #1
	str	r3, [sp, #12]
	ldr	r3, [sp, #60]
	cmp	r8, #0
	cmpeq	r3, #0
	beq	.L680
	cmp	r1, #0
	bne	.L681
.L689:
	add	r2, r2, #1
	cmp	r8, #0
	asr	r2, r2, #1
	movne	r0, r8
	bfi	r4, r2, #22, #6
	ldreq	r0, [r5, #-1836]
.L683:
	ldr	r3, [r5, #-1832]
	ubfx	r10, r4, #22, #5
	mov	r9, r1
	mov	r2, r1
	lsl	r1, r10, #10
	str	r0, [r5, #-1828]
	str	r3, [r5, #-1824]
	bl	rknand_dma_map_single
	mov	r2, r9
	str	r0, [r5, #-1820]
	lsl	r1, r10, #7
	ldr	r0, [r5, #-1824]
	bl	rknand_dma_map_single
	mov	r3, #1
	str	r0, [r5, #-1816]
	str	r3, [r5, #-1812]
	mov	r2, #16
	ldr	r3, [r5, #-1820]
	tst	r8, #3
	clz	r1, r9
	lsr	r1, r1, #5
	str	r3, [r6, #20]
	ldr	r3, [r5, #-1816]
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
.L680:
	ldr	r3, [sp, #12]
	str	r3, [r6, #16]
.L679:
	str	r7, [r6, #12]
	str	r4, [r6, #8]
	orr	r4, r4, #4
	str	r4, [r6, #8]
	add	sp, sp, #20
	@ sp needed
	pop	{r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L681:
	ldr	r3, [r5, #-1800]
	lsr	r10, r2, #1
	ldr	ip, [sp, #60]
	cmp	r3, #25
	movcc	r3, #64
	movcs	r3, #128
	str	r3, [sp, #4]
	mov	r3, #0
	mov	r0, r3
.L685:
	cmp	r0, r10
	bcs	.L689
	ldr	lr, [sp, #60]
	add	r0, r0, #1
	cmp	lr, #0
	bic	lr, r3, #3
	ldrne	fp, [ip], #4	@ unaligned
	mvneq	r9, #0
	ldrne	r9, [r5, #-1832]
	ldreq	fp, [r5, #-1832]
	strne	fp, [r9, lr]
	streq	r9, [fp, lr]
	ldr	lr, [sp, #4]
	add	r3, r3, lr
	b	.L685
.L697:
	.align	2
.L696:
	.word	.LANCHOR0
	.word	.LANCHOR2
	.fnend
	.size	NandcXferStart, .-NandcXferStart
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
	ldr	r3, .L738
	ldr	r5, .L738+4
	ldr	r4, [r3, r0, lsl #3]
	ldr	r3, [r5, #-1848]
	cmp	r3, #3
	bls	.L729
	ldr	r3, [r4, #16]
	tst	r3, #4
	beq	.L729
	ldr	r6, [r4, #16]
	ldr	r3, [r4, #8]
	ubfx	r6, r6, #1, #1
	cmp	r6, #0
	str	r3, [sp]
	beq	.L700
	ldr	r7, .L738+8
	mov	r6, #0
	ldr	r8, .L738+12
.L701:
	ldr	r2, [r4, #28]
	ldr	r3, [sp]
	ubfx	r2, r2, #16, #5
	ubfx	r3, r3, #22, #6
	cmp	r2, r3
	bge	.L709
	ldr	r3, [r5, #-1848]
	cmp	r3, #5
	bhi	.L702
.L705:
	add	r6, r6, #1
	bics	r3, r6, #-16777216
	bne	.L704
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
.L704:
	mov	r1, #5
	mov	r0, #1
	bl	usleep_range
	b	.L701
.L702:
	ldr	r3, [r4]
	str	r3, [sp, #4]
	ldr	r3, [sp, #4]
	tst	r3, #8192
	beq	.L705
	ldr	r3, [sp, #4]
	tst	r3, #131072
	beq	.L705
.L709:
	ldr	r3, [r5, #-1812]
	cmp	r3, #0
	beq	.L710
	ldr	r1, [sp]
	mov	r2, #0
	ldr	r0, [r5, #-1820]
	ubfx	r1, r1, #22, #5
	lsl	r1, r1, #10
	bl	rknand_dma_unmap_single
	ldr	r1, [sp]
	mov	r2, #0
	ldr	r0, [r5, #-1816]
	ubfx	r1, r1, #22, #5
	lsl	r1, r1, #7
	bl	rknand_dma_unmap_single
.L710:
	mov	r3, #0
	str	r3, [r5, #-1812]
.L698:
	add	sp, sp, #8
	@ sp needed
	pop	{r4, r5, r6, r7, r8, pc}
.L700:
	ldr	r7, .L738+16
	ldr	r8, .L738+12
.L711:
	ldr	r3, [sp]
	tst	r3, #1048576
	beq	.L713
	ldr	r3, [r5, #-1804]
	cmp	r3, #0
	beq	.L714
	mov	r0, r4
	bl	NandcSendDumpDataStart
.L714:
	ldr	r3, [r5, #-1812]
	cmp	r3, #0
	beq	.L715
	ldr	r1, [sp]
	mov	r2, #1
	ldr	r0, [r5, #-1820]
	ubfx	r1, r1, #22, #5
	lsl	r1, r1, #10
	bl	rknand_dma_unmap_single
	ldr	r1, [sp]
	mov	r2, #1
	ldr	r0, [r5, #-1816]
	ubfx	r1, r1, #22, #5
	lsl	r1, r1, #7
	bl	rknand_dma_unmap_single
.L715:
	ldr	r3, [r5, #-1804]
	cmp	r3, #0
	beq	.L710
	mov	r0, r4
	bl	NandcSendDumpDataDone
	b	.L710
.L713:
	ldr	r3, [r4, #8]
	add	r6, r6, #1
	str	r3, [sp]
	bics	r3, r6, #-16777216
	bne	.L712
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
.L712:
	mov	r1, #5
	mov	r0, #1
	bl	usleep_range
	b	.L711
.L729:
	ldr	r3, [r4, #8]
	str	r3, [sp]
	ldr	r3, [sp]
	tst	r3, #1048576
	beq	.L729
	b	.L698
.L739:
	.align	2
.L738:
	.word	.LANCHOR0
	.word	.LANCHOR2
	.word	.LC4
	.word	.LC5
	.word	.LC6
	.fnend
	.size	NandcXferComp, .-NandcXferComp
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
	bne	.L741
	cmp	r3, #0
	beq	.L742
	mov	r2, #1024
	mov	r1, r3
	bl	ftl_memcpy
.L742:
	cmp	r5, #0
	lsrne	r4, r4, #1
	ldrne	r3, [r5]	@ unaligned
	addne	r4, r4, r4, lsl #1
	strne	r3, [r6, r4, lsl #4]
	pop	{r4, r5, r6, pc}
.L741:
	cmp	r3, #0
	beq	.L745
	mov	r1, r0
	mov	r2, #1024
	mov	r0, r3
	bl	ftl_memcpy
.L745:
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
	mov	r8, r3
	ldr	r3, .L797
	tst	r8, #63
	.pad #92
	sub	sp, sp, #92
	mov	r7, r0
	mov	r5, r1
	str	r2, [sp, #8]
	ldr	r4, [sp, #128]
	ldr	r6, [r3, r0, lsl #3]
	bne	.L758
	cmp	r4, #0
	bne	.L759
	mov	r2, #64
	mov	r1, #255
	add	r0, sp, #24
	bl	ftl_memset
	add	r4, sp, #24
.L759:
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
	bne	.L760
	ldr	r3, .L797+4
	mov	r1, r5
	ldr	r2, [r3, #-1800]
	cmp	r2, #25
	ldr	r2, [sp, #8]
	movcc	lr, #64
	movcs	lr, #128
	lsr	r0, r2, #1
	mov	r2, r5
.L762:
	cmp	r1, r0
	add	r4, r4, #4
	add	ip, lr, r2
	bcc	.L763
	ldr	r2, [sp, #8]
	ldr	r0, [r3, #-1800]
	ldr	r1, [r3, #-1848]
	lsr	ip, r2, #2
	mov	r2, #0
	mov	r9, r2
.L764:
	cmp	r2, ip
	bcs	.L760
	cmp	r0, #0
	bne	.L770
.L760:
	mov	r3, #0
	str	r3, [r6, #16]
.L771:
	ldr	r3, .L797+4
	ldr	r3, [r3, #-1848]
	cmp	r3, #5
	movls	r3, #0
	movhi	r3, #1
	cmp	r5, #0
	movne	r3, #0
	cmp	r3, #0
	beq	.L757
	ldr	r3, [r6]
	and	r2, r3, #139264
	cmp	r2, #139264
	mvneq	r9, #0
	orreq	r3, r3, #131072
	streq	r3, [r6]
.L757:
	mov	r0, r9
	add	sp, sp, #92
	@ sp needed
	pop	{r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L763:
	ldr	r7, [r3, #-1832]
	bic	r2, r2, #3
	add	r1, r1, #1
	ldr	r2, [r7, r2]
	strb	r2, [r4, #-4]
	lsr	r7, r2, #8
	strb	r7, [r4, #-3]
	lsr	r7, r2, #16
	lsr	r2, r2, #24
	strb	r7, [r4, #-2]
	strb	r2, [r4, #-1]
	mov	r2, ip
	b	.L762
.L770:
	add	r3, r2, #8
	ldr	r3, [r6, r3, lsl #2]
	str	r3, [sp, #20]
	ldr	r3, [sp, #20]
	tst	r3, #4
	bne	.L786
	ldr	r3, [sp, #20]
	ubfx	r3, r3, #15, #1
	cmp	r3, #0
	bne	.L786
	cmp	r1, #5
	bls	.L766
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
.L796:
	orr	r3, r3, lr, lsl #5
.L768:
	cmp	r9, r3
	movcc	r9, r3
.L765:
	add	r2, r2, #1
	b	.L764
.L766:
	cmp	r1, #3
	bls	.L768
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
	b	.L796
.L786:
	mvn	r9, #0
	b	.L765
.L758:
	cmp	r1, #1
	bne	.L772
	mov	r9, #0
	cmp	r4, #0
	mov	r10, r9
	movne	r3, #4
	moveq	r3, #0
	str	r3, [sp, #12]
.L773:
	ldr	r3, [sp, #8]
	cmp	r9, r3
	movcs	r9, #0
	bcs	.L771
.L775:
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
	b	.L773
.L772:
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
.L776:
	ldr	r3, [sp, #8]
	cmp	r10, r3
	bcs	.L771
	mov	r1, r5
	mov	r0, r7
	bl	NandcXferComp
	ldr	r3, [r6, #32]
	add	r10, r10, #2
	str	r3, [sp, #20]
	ldr	r3, [sp, #8]
	cmp	r3, r10
	bls	.L777
	mov	r3, #0
	mov	r2, #2
	str	r3, [sp, #4]
	mov	r1, #0
	str	r3, [sp]
	mov	r0, r7
	and	r3, r10, #3
	bl	NandcXferStart
.L777:
	ldr	r3, [sp, #20]
	tst	r3, #4
	mvnne	r9, #0
	bne	.L778
	ldr	r3, [sp, #20]
	ldr	r2, [sp, #20]
	ubfx	r3, r3, #3, #5
	ubfx	r2, r2, #27, #1
	orr	r3, r3, r2, lsl #5
	cmp	r9, r3
	movcc	r9, r3
.L778:
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
	b	.L776
.L798:
	.align	2
.L797:
	.word	.LANCHOR0
	.word	.LANCHOR2
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
	ldr	r3, .L802
	subs	r4, r0, #0
	mov	r6, r1
	mov	r7, r2
	ldrb	r5, [r3, #477]	@ zero_extendqisi2
	bne	.L800
	ldr	r1, .L802+4
	ldrb	r3, [r1, #37]	@ zero_extendqisi2
	ldr	r0, [r1, #40]
	mul	r0, r0, r3
	cmp	r0, r6
	movhi	r5, #4
.L800:
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
.L803:
	.align	2
.L802:
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
	ldr	r4, .L830
	.pad #20
	sub	sp, sp, #20
	mov	fp, r2
	stm	sp, {r0, r1}
	ldr	r3, [r4, #-2804]
	ldr	r3, [r3, #304]
	str	r3, [sp, #12]
	ldr	r3, [r4, #-1848]
	cmp	r3, #8
	ldr	r3, [sp, #56]
	movcc	r10, #6
	movcs	r10, #12
	cmp	r3, #0
	moveq	r5, #1024
	beq	.L806
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
	ldrb	r0, [r4, #-1871]	@ zero_extendqisi2
	bl	FlashSetInterfaceMode
	ldrb	r0, [r4, #-1871]	@ zero_extendqisi2
	bl	NandcSetMode
	cmn	r5, #1
	bne	.L807
.L816:
	mvn	r5, #0
.L804:
	mov	r0, r5
	add	sp, sp, #20
	@ sp needed
	pop	{r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L807:
	mov	r2, r5
	ldr	r1, [sp, #4]
	ldr	r0, .L830+4
	bl	printk
	cmp	r5, #9
	bhi	.L809
	ldr	r2, [sp]
	ldr	r3, .L830+8
	ldr	r3, [r3, r2, lsl #3]
	ldr	r2, [r3, #3840]
	ldr	r2, [r3]
	orr	r2, r2, #131072
	str	r2, [r3]
.L809:
	ldr	r3, [r4, #-1796]
	add	r3, r3, #1
	cmp	r3, #2048
	str	r3, [r4, #-1796]
	movcs	r7, #0
	strcs	r7, [r4, #-1796]
	movcs	fp, r7
	bcc	.L804
.L806:
	mov	r9, #0
	mvn	r8, #0
	mov	r6, r9
	mov	r4, r9
	str	r9, [sp, #8]
.L814:
	uxtb	r0, r10
	bl	NandcSetDdrPara
	mov	r3, r7
	mov	r2, fp
	ldm	sp, {r0, r1}
	bl	FlashReadRawPage
	add	r3, r5, #1
	cmp	r0, r3
	bhi	.L810
	cmp	r0, #2
	bhi	.L820
	add	r4, r4, #1
	cmp	r4, #9
	bls	.L820
	mov	r3, r6
	mov	r5, r0
	sub	r6, r10, r4
	mov	r8, #0
.L812:
	ldr	r2, [sp, #8]
	cmp	r4, r2
	movls	r6, r3
.L813:
	cmp	r6, #0
	beq	.L815
	mov	r1, r6
	ldr	r0, .L830+12
	bl	printk
	uxtb	r0, r6
	bl	NandcSetDdrPara
.L815:
	cmn	r8, #1
	bne	.L804
	ldm	sp, {r1, r2}
	ldr	r0, .L830+16
	bl	printk
	ldr	r3, [sp, #56]
	cmp	r3, #0
	beq	.L816
	ldr	r3, [sp, #12]
	ubfx	r0, r3, #8, #8
	bl	NandcSetDdrPara
	b	.L804
.L810:
	ldr	r3, [sp, #8]
	cmp	r4, r3
	bls	.L821
	cmp	r4, #7
	sub	r6, r9, r4
	bhi	.L813
	str	r4, [sp, #8]
.L821:
	mov	r4, #0
	b	.L811
.L820:
	mov	r8, #0
	mov	r9, r10
	mov	r5, r0
	mov	r7, r8
	mov	fp, r8
.L811:
	add	r10, r10, #2
	cmp	r10, #69
	bls	.L814
	mov	r3, r6
	mov	r6, r9
	b	.L812
.L831:
	.align	2
.L830:
	.word	.LANCHOR2
	.word	.LC7
	.word	.LANCHOR0
	.word	.LC8
	.word	.LC9
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
	mov	r7, r1
	mov	r8, r2
	mov	r9, r3
	bl	FlashReadRawPage
	cmn	r0, #1
	mov	r4, r0
	ldr	r6, .L852
	bne	.L833
	ldr	r10, .L852+4
	ldrb	fp, [r10, #44]	@ zero_extendqisi2
	cmp	fp, #0
	bne	.L834
.L836:
	ldrb	r3, [r6, #-1856]	@ zero_extendqisi2
	cmp	r3, #0
	beq	.L833
	ldr	r3, [r6, #-2804]
	mov	r2, r8
	mov	r1, r7
	mov	r0, r5
	ldr	r10, [r3, #304]
	mov	r3, #1
	str	r3, [sp]
	mov	r3, r9
	bl	FlashDdrTunningRead
	cmn	r0, #1
	mov	r4, r0
	beq	.L837
	ldrb	r3, [r6, #-2739]	@ zero_extendqisi2
	cmp	r0, r3, lsr #1
	bls	.L833
.L837:
	ubfx	r0, r10, #8, #8
	bl	NandcSetDdrPara
	b	.L833
.L834:
	mov	r3, #0
	mov	r2, r8
	strb	r3, [r10, #44]
	mov	r1, r7
	mov	r3, r9
	mov	r0, r5
	bl	FlashReadRawPage
	cmn	r0, #1
	strb	fp, [r10, #44]
	movne	r4, r0
	beq	.L836
.L833:
	ldr	r10, [r6, #-1792]
	adds	r3, r10, #0
	movne	r3, #1
	cmn	r4, #1
	movne	r3, #0
	cmp	r3, #0
	beq	.L832
	mov	r3, r9
	mov	r2, r8
	mov	r1, r7
	mov	r0, r5
	blx	r10
	mov	r3, r7
	mov	r4, r0
	mov	r1, r0
	mov	r2, r5
	ldr	r0, .L852+8
	bl	printk
	cmn	r4, #1
	bne	.L832
	ldrb	r3, [r6, #-2740]	@ zero_extendqisi2
	cmp	r3, #0
	beq	.L832
	mov	r0, r5
	bl	flash_enter_slc_mode
	ldr	r4, [r6, #-1792]
	mov	r3, r9
	mov	r2, r8
	mov	r1, r7
	mov	r0, r5
	blx	r4
	mov	r4, r0
	mov	r0, r5
	bl	flash_exit_slc_mode
.L832:
	mov	r0, r4
	add	sp, sp, #12
	@ sp needed
	pop	{r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L853:
	.align	2
.L852:
	.word	.LANCHOR2
	.word	.LANCHOR0
	.word	.LC10
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
	ldr	r5, .L864
	mov	r4, #0
	mov	r7, r1
	ldrb	r0, [r5, #-1871]	@ zero_extendqisi2
	bl	FlashSetInterfaceMode
	ldrb	r0, [r5, #-1871]	@ zero_extendqisi2
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
	bne	.L855
	ldrb	r2, [r5, #-1871]	@ zero_extendqisi2
	tst	r2, #1
	beq	.L855
	mov	r0, #1
	bl	FlashSetInterfaceMode
	mov	r0, #1
	bl	NandcSetMode
	strb	r4, [r5, #-1856]
.L856:
	mov	r0, #0
	add	sp, sp, #8
	@ sp needed
	pop	{r4, r5, r6, r7, r8, pc}
.L855:
	mov	r2, #1
	strb	r2, [r3, #-1856]
	b	.L856
.L865:
	.align	2
.L864:
	.word	.LANCHOR2
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
	ldr	r10, .L881
	mov	r5, #0
	mov	r7, #4
	strb	r3, [sp, #12]
	mov	r3, #40
	strb	r3, [sp, #13]
	mov	r3, #24
	strb	r3, [sp, #14]
	mov	r3, #16
	ldr	r4, .L881+4
	mvn	r6, #0
	strb	r3, [sp, #15]
	mov	r0, r5
	ldr	r3, [r10, #40]
	sub	r9, r4, #2720
	str	r5, [r4, #-1784]
	sub	r9, r9, #8
	str	r3, [sp, #4]
	ldr	r3, [r4, #-1864]
	str	r3, [r4, #-1788]
	bl	flash_enter_slc_mode
.L867:
	add	fp, r5, #1
	mov	r8, #0
.L869:
	add	r3, sp, #12
	ldrb	r0, [r3, r8]	@ zero_extendqisi2
	bl	FlashBchSel
	mov	r3, #0
	ldr	r2, [r4, #-1864]
	mov	r1, r5
	mov	r0, r3
	bl	FlashReadRawPage
	cmn	r0, #1
	bne	.L868
	mov	r3, #0
	ldr	r2, [r4, #-1864]
	mov	r1, fp
	mov	r0, r3
	bl	FlashReadRawPage
	cmn	r0, #1
	bne	.L868
	add	r8, r8, #1
	cmp	r8, #4
	bne	.L869
.L870:
	ldr	r3, [sp, #4]
	subs	r7, r7, #1
	add	r5, r5, r3
	bne	.L867
	mov	r0, r7
	b	.L880
.L871:
	movw	r1, #2036
	add	r0, r8, #12
	bl	js_hash
	ldr	r3, [r8, #8]
	cmp	r3, r0
	mvnne	r6, #0
	bne	.L870
	ldr	r6, .L881+8
	mov	r2, #32
	add	r1, r8, #160
	mov	r0, r6
	bl	ftl_memcpy
	ldr	r1, [r4, #-1788]
	mov	r2, #32
	ldr	r0, .L881+12
	add	r1, r1, #192
	bl	ftl_memcpy
	ldr	r1, [r4, #-1788]
	mov	r2, #852
	mov	r0, r9
	add	r1, r1, #224
	bl	ftl_memcpy
	ldrh	r0, [r6, #10]
	bl	FlashBlockAlignInit
	ldr	r6, [r4, #-1788]
	mov	r0, r5
	str	r5, [r4, #-1784]
	ldr	r1, [r10, #40]
	ldr	r3, [r6, #1076]
	strb	r3, [r4, #-1856]
	bl	__aeabi_uidiv
	add	r0, r0, #1
	cmp	r0, #1
	movls	r3, #2
	strhi	r0, [r4, #-1780]
	strls	r3, [r4, #-1780]
	ldrh	r3, [r6, #14]
	mov	r6, #0
	strb	r3, [r4, #-1776]
	b	.L870
.L868:
	ldr	r8, [r4, #-1788]
	ldr	r2, .L881+16
	ldr	r3, [r8]
	cmp	r3, r2
	bne	.L870
	cmp	r6, #0
	bne	.L871
	ldr	r1, [r10, #40]
	mov	r0, r5
	bl	__aeabi_uidiv
	add	r0, r0, #1
	str	r0, [r4, #-1780]
	mov	r0, r6
.L880:
	bl	flash_exit_slc_mode
	mov	r0, r6
	add	sp, sp, #20
	@ sp needed
	pop	{r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L882:
	.align	2
.L881:
	.word	.LANCHOR0
	.word	.LANCHOR2
	.word	.LANCHOR1+468
	.word	.LANCHOR0+52
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
	.pad #28
	sub	sp, sp, #28
	mov	r7, r0
	str	r2, [sp, #8]
	mov	r10, r3
	str	r1, [sp, #20]
	bl	NandcWaitFlashReady
	ldr	r3, .L911
	ldr	r2, .L911+4
	ldr	r5, [r3, r7, lsl #3]
	add	r3, r3, r7, lsl #3
	str	r2, [sp, #12]
	ldrb	r3, [r3, #4]	@ zero_extendqisi2
	str	r3, [sp, #4]
	add	r6, r3, #8
	ldrb	r3, [r2]	@ zero_extendqisi2
	add	r6, r5, r6, lsl #8
	sub	r3, r3, #67
	cmp	r3, #1
	ldr	r3, [sp, #4]
	movls	r4, #0
	lsl	r3, r3, #8
	str	r3, [sp, #16]
	bls	.L884
	ldr	r3, .L911+8
	ldrb	r4, [r3, #-1856]	@ zero_extendqisi2
	cmp	r4, #0
	beq	.L885
	mov	r4, #1
	mov	r0, #0
	bl	NandcSetDdrMode
.L885:
	ldr	r3, [sp, #16]
	mov	r2, #92
	add	r3, r5, r3
	str	r2, [r3, #2056]
	mov	r2, #197
	str	r2, [r3, #2056]
.L884:
	mov	r8, #1
	mvn	r9, #0
.L886:
	ldr	r3, .L911+12
	ldrb	r3, [r3]	@ zero_extendqisi2
	add	r3, r3, #1
	cmp	r8, r3
	bcc	.L895
	mov	fp, r9
.L894:
	ldr	r3, [sp, #12]
	mov	r1, #0
	mov	r0, r6
	ldrb	r2, [r3]	@ zero_extendqisi2
	sub	r2, r2, #67
	cmp	r2, #1
	bhi	.L896
	bl	SandiskSetRRPara
.L897:
	ldr	r3, [sp, #16]
	mov	r2, #255
	add	r5, r5, r3
	str	r2, [r5, #2056]
	ldr	r2, .L911+8
	ldrb	r2, [r2, #-2739]	@ zero_extendqisi2
	add	r2, r2, r2, lsl #1
	cmp	fp, r2, asr #2
	bcc	.L898
	cmn	fp, #1
	movne	fp, #256
.L898:
	mov	r0, r7
	bl	NandcWaitFlashReady
	cmp	r4, #0
	beq	.L883
	mov	r0, #4
	bl	NandcSetDdrMode
.L883:
	mov	r0, fp
	add	sp, sp, #28
	@ sp needed
	pop	{r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L895:
	ldr	r3, [sp, #12]
	mov	r0, r6
	uxtb	r1, r8
	ldrb	r3, [r3]	@ zero_extendqisi2
	sub	r3, r3, #67
	cmp	r3, #1
	bhi	.L887
	bl	SandiskSetRRPara
.L888:
	ldr	r3, [sp, #12]
	ldrb	r3, [r3]	@ zero_extendqisi2
	cmp	r3, #34
	bne	.L889
	ldr	r3, .L911+12
	ldrb	r3, [r3]	@ zero_extendqisi2
	sub	r3, r3, #3
	cmp	r8, r3
	ldreq	r3, [sp, #4]
	moveq	r2, #179
	addeq	r3, r5, r3, lsl #8
	streq	r2, [r3, #2056]
.L889:
	ldr	r3, [sp, #16]
	mov	r2, #38
	cmp	r4, #0
	add	r3, r5, r3
	str	r2, [r3, #2056]
	mov	r2, #93
	str	r2, [r3, #2056]
	beq	.L890
	mov	r0, #4
	bl	NandcSetDdrMode
	mov	r3, r10
	ldr	r2, [sp, #8]
	ldr	r1, [sp, #20]
	mov	r0, r7
	bl	FlashReadRawPage
	mov	fp, r0
	mov	r0, #0
	bl	NandcSetDdrMode
.L891:
	cmn	fp, #1
	beq	.L892
	ldr	r3, .L911+8
	cmn	r9, #1
	moveq	r9, fp
	ldrb	r2, [r3, #-2739]	@ zero_extendqisi2
	add	r2, r2, r2, lsl #1
	cmp	fp, r2, asr #2
	bcc	.L894
	mov	r10, #0
	str	r10, [sp, #8]
.L892:
	add	r8, r8, #1
	b	.L886
.L887:
	bl	ToshibaSetRRPara
	b	.L888
.L890:
	mov	r3, r10
	ldr	r2, [sp, #8]
	ldr	r1, [sp, #20]
	mov	r0, r7
	bl	FlashReadRawPage
	mov	fp, r0
	b	.L891
.L896:
	bl	ToshibaSetRRPara
	b	.L897
.L912:
	.align	2
.L911:
	.word	.LANCHOR0
	.word	g_retryMode
	.word	.LANCHOR2
	.word	g_maxRetryCount
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
	mov	r7, r0
	mov	r9, r2
	mov	r8, r3
	mov	r10, r1
	mov	r6, #1
	bl	NandcWaitFlashReady
	ldr	r2, .L927
	mvn	r4, #0
	ldr	fp, .L927+4
	add	r3, r2, r7, lsl #3
	ldrb	r5, [r3, #4]	@ zero_extendqisi2
	add	r3, r5, #8
	ldr	r5, [r2, r7, lsl #3]
	add	r5, r5, r3, lsl #8
.L914:
	ldr	r3, .L927+8
	ldrb	r3, [r3]	@ zero_extendqisi2
	add	r3, r3, #1
	cmp	r6, r3
	bcc	.L918
.L917:
	mov	r1, #0
	mov	r0, r5
	bl	SamsungSetRRPara
	ldr	r3, .L927+4
	ldrb	r3, [r3, #-2739]	@ zero_extendqisi2
	add	r3, r3, r3, lsl #1
	cmp	r4, r3, asr #2
	bcc	.L913
	cmn	r4, #1
	movne	r4, #256
.L913:
	mov	r0, r4
	pop	{r3, r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L918:
	uxtb	r1, r6
	mov	r0, r5
	bl	SamsungSetRRPara
	mov	r3, r8
	mov	r2, r9
	mov	r1, r10
	mov	r0, r7
	bl	FlashReadRawPage
	cmn	r0, #1
	beq	.L915
	ldrb	r3, [fp, #-2739]	@ zero_extendqisi2
	cmn	r4, #1
	moveq	r4, r0
	add	r3, r3, r3, lsl #1
	cmp	r0, r3, asr #2
	bcc	.L921
	mov	r8, #0
	mov	r9, r8
.L915:
	add	r6, r6, #1
	b	.L914
.L921:
	mov	r4, r0
	b	.L917
.L928:
	.align	2
.L927:
	.word	.LANCHOR0
	.word	.LANCHOR2
	.word	g_maxRetryCount
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
	@ args = 0, pretend = 0, frame = 24
	@ frame_needed = 0, uses_anonymous_args = 0
.L931:
	push	{r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.save {r4, r5, r6, r7, r8, r9, r10, fp, lr}
	mov	r8, r3
	ldr	r3, .L954
	mov	fp, r2
	.pad #36
	sub	sp, sp, #36
	mov	r6, r0
	str	r1, [sp, #20]
	ldrb	r2, [r3, #-2739]	@ zero_extendqisi2
	ldrb	r3, [r3, #-2740]	@ zero_extendqisi2
	cmp	r3, #0
	ldrne	r5, .L954+4
	addeq	r2, r2, r2, lsl #1
	asreq	r5, r2, #2
	smullne	r2, r3, r2, r5
	movne	r5, r3
	mov	r3, #0
	str	r3, [sp, #8]
	ldr	r3, .L954+8
	add	r3, r3, r0, lsl #3
	str	r3, [sp, #28]
.L941:
	mov	r0, r6
	mov	r10, #0
	bl	NandcWaitFlashReady
	ldr	r3, .L954+8
	mvn	r4, #0
	ldr	r3, [r3, r6, lsl #3]
	str	r3, [sp, #12]
	ldr	r3, [sp, #28]
	ldrb	r3, [r3, #4]	@ zero_extendqisi2
	str	r3, [sp, #16]
	ldr	r2, [sp, #16]
	ldr	r3, [sp, #12]
	add	r7, r3, r2, lsl #8
.L932:
	ldr	r3, .L954+12
	ldrb	r3, [r3]	@ zero_extendqisi2
	cmp	r10, r3
	bcc	.L936
.L935:
	ldr	r3, [sp, #12]
	mov	r0, #200
	ldr	r2, [sp, #16]
	add	r7, r3, r2, lsl #8
	mov	r3, #239
	str	r3, [r7, #2056]
	mov	r3, #137
	str	r3, [r7, #2052]
	bl	ndelay
	cmp	r4, r5
	mov	r3, #0
	str	r3, [r7, #2048]
	str	r3, [r7, #2048]
	str	r3, [r7, #2048]
	str	r3, [r7, #2048]
	bcc	.L937
	cmn	r4, #1
	movne	r4, #256
.L937:
	cmn	r4, #1
	movne	r7, #0
	moveq	r7, #1
	cmp	r4, #256
	movne	r1, r7
	orreq	r1, r7, #1
	cmp	r1, #0
	beq	.L938
	mov	r3, r10
	str	r4, [sp]
	ldr	r2, [sp, #20]
	mov	r1, r10
	ldr	r0, .L954+16
	bl	printk
	ldr	r3, [sp, #8]
	cmp	r3, #0
	bne	.L939
	ldr	r3, .L954
	ldrb	r3, [r3, #-2740]	@ zero_extendqisi2
	cmp	r3, #0
	moveq	r7, #0
	andne	r7, r7, #1
	cmp	r7, #0
	beq	.L929
	mov	r1, #3
	mov	r0, r6
	bl	micron_auto_read_calibration_config
	mov	r3, #1
	str	r3, [sp, #8]
	b	.L941
.L936:
	mov	r3, #239
	mov	r0, #200
	str	r3, [r7, #2056]
	mov	r3, #137
	str	r3, [r7, #2052]
	mov	r9, #0
	bl	ndelay
	add	r3, r10, #1
	mov	r2, fp
	str	r3, [r7, #2048]
	mov	r0, r6
	str	r9, [r7, #2048]
	str	r3, [sp, #24]
	mov	r3, r8
	str	r9, [r7, #2048]
	ldr	r1, [sp, #20]
	str	r9, [r7, #2048]
	bl	FlashReadRawPage
	cmn	r0, #1
	beq	.L933
	cmn	r4, #1
	moveq	r4, r0
	cmp	r0, r5
	bcc	.L943
	mov	r8, r9
	mov	fp, r9
.L933:
	ldr	r10, [sp, #24]
	b	.L932
.L943:
	mov	r4, r0
	mov	r8, r9
	mov	fp, r9
	b	.L935
.L939:
	mov	r1, #0
	mov	r0, r6
	bl	micron_auto_read_calibration_config
	cmn	r4, #1
	movne	r4, #256
.L929:
	mov	r0, r4
	add	sp, sp, #36
	@ sp needed
	pop	{r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L938:
	ldr	r3, [sp, #8]
	cmp	r3, #0
	beq	.L929
	mov	r0, r6
	mov	r4, #256
	bl	micron_auto_read_calibration_config
	b	.L929
.L955:
	.align	2
.L954:
	.word	.LANCHOR2
	.word	1431655766
	.word	.LANCHOR0
	.word	g_maxRetryCount
	.word	.LC11
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
	str	r1, [sp]
	mov	r8, #0
	mvn	r6, #0
	mov	fp, r2
	ldr	r1, .L974
	mov	r5, r0
	ldr	r7, .L974+4
	ldr	r3, [r1, #48]
	add	r2, r7, r0
	ldrb	r10, [r7, #-2726]	@ zero_extendqisi2
	ldrb	r4, [r2, #-2716]	@ zero_extendqisi2
	ldrb	r3, [r3, #19]	@ zero_extendqisi2
	str	r1, [sp, #4]
	sub	r3, r3, #7
	cmp	r3, #1
	ldrbls	r4, [r2, #-2708]	@ zero_extendqisi2
	bl	NandcWaitFlashReady
.L958:
	cmp	r8, r10
	bcc	.L963
.L962:
	ldr	r3, [sp, #4]
	add	r5, r7, r5
	ldr	r3, [r3, #48]
	ldrb	r3, [r3, #19]	@ zero_extendqisi2
	sub	r3, r3, #7
	cmp	r3, #1
	ldrb	r3, [r7, #-2739]	@ zero_extendqisi2
	strbls	r4, [r5, #-2708]
	strbhi	r4, [r5, #-2716]
	add	r3, r3, r3, lsl #1
	cmp	r6, r3, asr #2
	bcc	.L956
	cmn	r6, #1
	movne	r6, #256
.L956:
	mov	r0, r6
	add	sp, sp, #12
	@ sp needed
	pop	{r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L963:
	add	r4, r4, #1
	ldr	r2, .L974+8
	uxtb	r4, r4
	ldrb	r1, [r7, #-2727]	@ zero_extendqisi2
	mov	r0, r5
	cmp	r10, r4
	movls	r4, #0
	mov	r3, r4
	bl	HynixSetRRPara
	mov	r3, r9
	mov	r2, fp
	ldr	r1, [sp]
	mov	r0, r5
	bl	FlashReadRawPage
	cmn	r0, #1
	beq	.L960
	ldrb	r3, [r7, #-2739]	@ zero_extendqisi2
	cmn	r6, #1
	moveq	r6, r0
	add	r3, r3, r3, lsl #1
	cmp	r0, r3, asr #2
	bcc	.L967
	mov	r9, #0
	mov	fp, r9
.L960:
	add	r8, r8, #1
	b	.L958
.L967:
	mov	r6, r0
	b	.L962
.L975:
	.align	2
.L974:
	.word	.LANCHOR0
	.word	.LANCHOR2
	.word	.LANCHOR2-2724
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
	ldr	r3, .L1006
	ldr	r2, [r3, r10, lsl #3]
	add	r3, r3, r10, lsl #3
	ldrb	r6, [r3, #4]	@ zero_extendqisi2
	ldr	r3, .L1006+4
	str	r2, [sp, #12]
	ldrb	r2, [r3, #-1872]	@ zero_extendqisi2
	str	r3, [sp, #20]
	cmp	r2, #0
	bne	.L977
	ldr	r3, [sp, #12]
	lsl	r8, r6, #8
	mvn	r4, #0
	mov	r7, #1
	add	r5, r3, r8
.L981:
	mov	r3, #239
	mov	r6, #0
	str	r3, [r5, #2056]
	mov	r3, #141
	str	r3, [r5, #2052]
	mov	r2, fp
	ldr	r3, .L1006+8
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
	beq	.L978
	ldr	r3, [sp, #20]
	cmn	r4, #1
	moveq	r4, r0
	ldrb	r3, [r3, #-2739]	@ zero_extendqisi2
	add	r3, r3, r3, lsl #1
	cmp	r0, r3, asr #2
	bcc	.L989
	mov	r9, r6
	mov	fp, r6
.L978:
	add	r7, r7, #1
	cmp	r7, #26
	bne	.L981
.L980:
	ldr	r3, [sp, #12]
	add	r8, r3, r8
	mov	r3, #239
	str	r3, [r8, #2056]
	mov	r3, #141
.L1005:
	str	r3, [r5, #2052]
	mov	r3, #0
	str	r3, [r5, #2048]
	str	r3, [r5, #2048]
	str	r3, [r5, #2048]
	str	r3, [r5, #2048]
	ldr	r3, .L1006+4
	ldrb	r3, [r3, #-2739]	@ zero_extendqisi2
	add	r3, r3, r3, lsl #1
	cmp	r4, r3, asr #2
	bcc	.L987
	cmn	r4, #1
	movne	r4, #256
.L987:
	cmn	r4, #1
	cmpne	r4, #256
	bne	.L988
	str	r4, [sp]
	mov	r3, r7
	ldr	r2, [sp, #16]
	mov	r1, r7
	ldr	r0, .L1006+12
	bl	printk
.L988:
	mov	r0, r10
	bl	NandcWaitFlashReady
	mov	r0, r4
	add	sp, sp, #28
	@ sp needed
	pop	{r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L989:
	mov	r4, r0
	b	.L980
.L977:
	ldr	r3, [sp, #12]
	lsl	r6, r6, #8
	ldr	r8, .L1006+16
	mvn	r4, #0
	mov	r7, #1
	add	r5, r3, r6
.L986:
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
	beq	.L983
	ldr	r3, .L1006+4
	cmn	r4, #1
	moveq	r4, r0
	ldrb	r3, [r3, #-2739]	@ zero_extendqisi2
	add	r3, r3, r3, lsl #1
	cmp	r0, r3, asr #2
	bcc	.L990
	mov	r9, #0
	mov	fp, r9
.L983:
	add	r7, r7, #1
	add	r8, r8, #4
	cmp	r7, #26
	bne	.L986
.L985:
	ldr	r3, [sp, #12]
	add	r6, r3, r6
	mov	r3, #239
	str	r3, [r6, #2056]
	mov	r3, #137
	b	.L1005
.L990:
	mov	r4, r0
	b	.L985
.L1007:
	.align	2
.L1006:
	.word	.LANCHOR0
	.word	.LANCHOR2
	.word	.LANCHOR3
	.word	.LC12
	.word	.LANCHOR3+26
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
	ldr	r3, .L1012
	subs	r4, r0, #0
	mov	r5, r1
	mov	r7, r2
	ldrb	r6, [r3, #477]	@ zero_extendqisi2
	bne	.L1009
	ldr	r1, .L1012+4
	ldrb	r3, [r1, #37]	@ zero_extendqisi2
	ldr	r0, [r1, #40]
	mul	r0, r0, r3
	cmp	r0, r5
	bls	.L1009
	ldrb	r3, [r1, #36]	@ zero_extendqisi2
	cmp	r3, #0
	movne	r6, #4
.L1009:
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
.L1013:
	.align	2
.L1012:
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
	ldr	r4, .L1028
	ldr	r5, .L1028+4
	ldr	r3, [r4, #-1864]
	ldrb	r0, [r4, #-1775]	@ zero_extendqisi2
	ldr	r8, .L1028+8
	str	r3, [r4, #-1788]
	bl	FlashBchSel
	mov	r2, #2048
	mov	r1, #0
	ldr	r0, [r4, #-1864]
	bl	ftl_memset
	ldr	r3, [r4, #-1788]
	mov	r2, #32
	ldr	r1, .L1028+12
	str	r8, [r3]
	ldr	r0, [r4, #-1788]
	ldrb	r3, [r5, #3156]	@ zero_extendqisi2
	add	r0, r0, #16
	strh	r3, [r0, #-4]	@ movhi
	ldrb	r3, [r5, #37]	@ zero_extendqisi2
	strh	r3, [r0, #-2]	@ movhi
	ldrb	r3, [r4, #-1856]	@ zero_extendqisi2
	str	r3, [r0, #1060]
	bl	ftl_memcpy
	ldr	r0, [r4, #-1788]
	mov	r2, #8
	ldr	r1, .L1028+16
	add	r0, r0, #80
	bl	ftl_memcpy
	ldr	r0, [r4, #-1788]
	mov	r2, #32
	add	r1, r5, #3168
	add	r0, r0, #96
	bl	ftl_memcpy
	ldr	r0, [r4, #-1788]
	mov	r2, #32
	ldr	r1, .L1028+20
	add	r0, r0, #160
	bl	ftl_memcpy
	ldr	r0, [r4, #-1788]
	mov	r2, #32
	add	r1, r5, #52
	add	r0, r0, #192
	bl	ftl_memcpy
	ldr	r0, [r4, #-1788]
	sub	r1, r4, #2720
	mov	r2, #852
	sub	r1, r1, #8
	add	r0, r0, #224
	bl	ftl_memcpy
	ldr	r6, [r4, #-1788]
	movw	r1, #2036
	add	r0, r6, #12
	bl	js_hash
	movw	r3, #1592
	str	r0, [r6, #8]
	str	r3, [r6, #4]
	mov	r6, #0
	ldr	r3, [r4, #-1772]
	mov	r7, r6
	mov	r0, #0
	str	r3, [r4, #-1788]
	bl	flash_enter_slc_mode
.L1020:
	ldr	r1, [r5, #40]
	mov	r2, #0
	mov	r0, r2
	mul	r1, r1, r7
	bl	FlashEraseBlock
	ldrb	r9, [r4, #-2740]	@ zero_extendqisi2
	cmp	r9, #0
	beq	.L1015
	mov	r9, #0
.L1016:
	ldr	r1, [r5, #40]
	mov	r3, #0
	ldr	r2, [r4, #-1864]
	mov	r0, r3
	mla	r1, r1, r7, r9
	add	r9, r9, #1
	bl	FlashProgPage
	cmp	r9, #10
	bne	.L1016
.L1017:
	ldr	r1, [r5, #40]
	mov	r3, #0
	ldr	r2, [r4, #-1772]
	mov	r0, r3
	add	r10, r7, #1
	mul	r1, r1, r7
	bl	FlashReadRawPage
	cmn	r0, #1
	beq	.L1018
	ldr	r9, [r4, #-1788]
	ldr	r3, [r9]
	cmp	r3, r8
	bne	.L1018
	movw	r1, #2036
	add	r0, r9, #12
	bl	js_hash
	ldr	r3, [r9, #8]
	cmp	r3, r0
	bne	.L1018
	ldr	r3, [r5, #40]
	cmp	r6, #1
	str	r10, [r4, #-1780]
	mul	r7, r7, r3
	str	r7, [r4, #-1784]
	beq	.L1021
	mov	r6, #1
.L1018:
	cmp	r10, #4
	mov	r7, r10
	bne	.L1020
.L1019:
	mov	r0, #0
	bl	flash_exit_slc_mode
	clz	r0, r6
	lsr	r0, r0, #5
	rsb	r0, r0, #0
	pop	{r4, r5, r6, r7, r8, r9, r10, pc}
.L1015:
	ldr	r1, [r5, #40]
	mov	r3, r9
	ldr	r2, [r4, #-1864]
	mov	r0, r9
	mul	r1, r1, r7
	bl	FlashProgPage
	ldr	r1, [r5, #40]
	mov	r3, r9
	ldr	r2, [r4, #-1864]
	mov	r0, r9
	mul	r1, r1, r7
	add	r1, r1, #1
	bl	FlashProgPage
	b	.L1017
.L1021:
	mov	r6, #2
	b	.L1019
.L1029:
	.align	2
.L1028:
	.word	.LANCHOR2
	.word	.LANCHOR0
	.word	1312902724
	.word	IDByte
	.word	.LANCHOR0+3160
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
	ldr	r4, .L1049
	mov	r9, r0
	strb	r3, [sp, #12]
	mov	r3, #40
	strb	r3, [sp, #13]
	mov	r3, #24
	strb	r3, [sp, #14]
	mov	r3, #16
	strb	r3, [sp, #15]
	ldrb	r3, [r4, #-2739]	@ zero_extendqisi2
	str	r3, [sp, #4]
	ldr	r3, [r4, #-1860]
	cmp	r3, #0
	beq	.L1031
	mov	r0, #0
	bl	flash_enter_slc_mode
.L1031:
	ldr	fp, .L1049+4
	mvn	r7, #0
	mov	r5, #2
	mov	r2, #2048
	mov	r1, #0
	mov	r0, r9
	mov	r10, fp
	bl	ftl_memset
.L1032:
	ldrb	r3, [fp, #37]	@ zero_extendqisi2
	cmp	r5, r3
	bcc	.L1037
.L1036:
	ldr	r0, [sp, #4]
	bl	FlashBchSel
	ldr	r3, [r4, #-1860]
	cmp	r3, #0
	beq	.L1030
	mov	r0, #0
	bl	flash_exit_slc_mode
.L1030:
	mov	r0, r7
	add	sp, sp, #20
	@ sp needed
	pop	{r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L1037:
	mov	r6, #0
.L1034:
	add	r3, sp, #12
	ldrb	r8, [r3, r6]	@ zero_extendqisi2
	mov	r0, r8
	bl	FlashBchSel
	ldr	r1, [r10, #40]
	mov	r3, #0
	ldr	r2, [r4, #-1864]
	mov	r0, r3
	mul	r1, r1, r5
	bl	FlashReadRawPage
	cmn	r0, #1
	bne	.L1033
	add	r6, r6, #1
	cmp	r6, #4
	bne	.L1034
.L1035:
	add	r5, r5, #1
	b	.L1032
.L1040:
	mov	r7, #0
	b	.L1036
.L1033:
	ldr	r3, [r4, #-1864]
	ldr	r2, .L1049+8
	ldr	r3, [r3]
	cmp	r3, r2
	bne	.L1035
	mov	r1, r8
	ldr	r0, .L1049+12
	bl	printk
	mov	r2, #2048
	ldr	r1, [r4, #-1864]
	mov	r0, r9
	bl	ftl_memcpy
	ldr	r3, [r4, #-1864]
	ldr	r3, [r3, #512]
	strb	r3, [r10, #37]
	ldr	r3, [r4, #-1780]
	cmp	r5, r3
	bcs	.L1040
	str	r5, [r4, #-1780]
	mov	r7, #0
	bl	FlashSavePhyInfo
	b	.L1035
.L1050:
	.align	2
.L1049:
	.word	.LANCHOR2
	.word	.LANCHOR0
	.word	-52655045
	.word	.LC13
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
	mov	r6, r0
	ldr	r4, .L1149
	.pad #28
	sub	sp, sp, #28
	mov	r0, #32768
	mov	r7, #0
	bl	ftl_dma32_malloc
	str	r0, [r4, #-1864]
	mov	r0, #32768
	bl	ftl_dma32_malloc
	str	r0, [r4, #-1772]
	mov	r0, #4096
	bl	ftl_dma32_malloc
	str	r0, [r4, #-1768]
	mov	r0, #32768
	bl	ftl_dma32_malloc
	str	r0, [r4, #-1764]
	mov	r0, #4096
	bl	ftl_dma32_malloc
	ldr	r5, .L1149+4
	mov	r3, #50
	str	r0, [r4, #-1760]
	mov	r0, r6
	ldr	r6, .L1149+8
	mov	r9, r7
	strb	r3, [r5, #37]
	strb	r3, [r4, #-1776]
	mov	r3, #128
	mov	r8, r6
	str	r3, [r5, #40]
	mov	r3, #60
	str	r7, [r4, #-1780]
	strb	r7, [r4, #-1856]
	str	r7, [r4, #-1796]
	strb	r7, [r5, #36]
	strb	r7, [r4, #-1756]
	strb	r3, [r4, #-1775]
	bl	NandcInit
.L1057:
	add	r2, r5, r7, lsl #3
	uxtb	r10, r7
	ldr	fp, [r5, r7, lsl #3]
	ldrb	r2, [r2, #4]	@ zero_extendqisi2
	mov	r0, r10
	str	r2, [sp, #20]
	bl	FlashReset
	mov	r0, r10
	bl	NandcFlashCs
	ldr	r2, [sp, #20]
	mov	r3, #144
	mov	r0, #200
	add	fp, fp, r2, lsl #8
	str	r3, [fp, #2056]
	str	r9, [fp, #2052]
	bl	ndelay
	ldr	r2, [fp, #2048]
	uxtb	r2, r2
	strb	r2, [r6]
	cmp	r2, #44
	ldr	r1, [fp, #2048]
	strb	r1, [r6, #1]
	ldr	r1, [fp, #2048]
	strb	r1, [r6, #2]
	ldr	r1, [fp, #2048]
	strb	r1, [r6, #3]
	ldr	r1, [fp, #2048]
	strb	r1, [r6, #4]
	ldr	r1, [fp, #2048]
	strb	r1, [r6, #5]
	bne	.L1052
	mov	r2, #239
	mov	r0, #200
	str	r2, [fp, #2056]
	mov	r2, #1
	str	r2, [fp, #2052]
	bl	ndelay
	mov	r2, #4
	str	r2, [fp, #2048]
	str	r9, [fp, #2048]
	str	r9, [fp, #2048]
	str	r9, [fp, #2048]
.L1052:
	mov	r0, r10
	bl	NandcFlashDeCs
	ldrb	r2, [r6]	@ zero_extendqisi2
	sub	r3, r2, #1
	uxtb	r3, r3
	cmp	r3, #253
	bhi	.L1053
	ldrb	r1, [r6, #5]	@ zero_extendqisi2
	ldrb	r3, [r6, #1]	@ zero_extendqisi2
	ldr	r0, .L1149+12
	str	r1, [sp, #12]
	ldrb	r1, [r6, #4]	@ zero_extendqisi2
	str	r1, [sp, #8]
	ldrb	r1, [r6, #3]	@ zero_extendqisi2
	str	r1, [sp, #4]
	ldrb	r1, [r6, #2]	@ zero_extendqisi2
	str	r1, [sp]
	add	r1, r7, #1
	bl	printk
.L1053:
	cmp	r7, #0
	bne	.L1054
	ldrb	r3, [r8]	@ zero_extendqisi2
	sub	r3, r3, #1
	uxtb	r3, r3
	cmp	r3, #253
	bhi	.L1104
	ldrb	r3, [r8, #1]	@ zero_extendqisi2
	cmp	r3, #255
	beq	.L1104
	bl	FlashCs123Init
.L1054:
	ldrb	r3, [r6]	@ zero_extendqisi2
	add	r7, r7, #1
	add	r6, r6, #8
	cmp	r3, #181
	moveq	r3, #44
	strbeq	r3, [r6, #-8]
	cmp	r7, #4
	bne	.L1057
	ldrb	r3, [r8]	@ zero_extendqisi2
	cmp	r3, #173
	beq	.L1058
	ldr	r0, [r4, #-1852]
	bl	NandcSetDdrMode
.L1058:
	mov	r2, #852
	mov	r1, #0
	ldr	r0, .L1149+16
	bl	ftl_memset
	ldr	r7, .L1149+20
	ldr	r2, .L1149+24
	ldr	r0, [r4, #-1844]
	ldr	r6, .L1149+28
	add	r3, r2, #468
	cmp	r0, r7
	str	r3, [r5, #48]
	mov	r3, #0
	strb	r3, [r5, #44]
	bne	.L1059
	ldrb	r3, [r2, #487]	@ zero_extendqisi2
	cmp	r3, #50
	movne	r3, #1
	strne	r3, [r4, #-1860]
.L1059:
	ldrb	r3, [r8, #1]	@ zero_extendqisi2
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
	bne	.L1060
	cmp	r3, #220
	bne	.L1061
	ldrb	r1, [r8, #3]	@ zero_extendqisi2
	cmp	r1, #149
	bne	.L1061
.L1060:
	mov	ip, #16
	mov	r1, #1
	strb	ip, [r5, #37]
	strb	ip, [r4, #-1775]
	ldrb	ip, [r8]	@ zero_extendqisi2
	strb	r1, [r5, #36]
	strb	r3, [r2, #3410]
	cmp	ip, #152
	strb	ip, [r2, #3409]
	bne	.L1063
	ldrsb	ip, [r8, #4]
	cmp	ip, #0
	movge	r1, #24
	strblt	r1, [r4, #-1756]
	strbge	r1, [r4, #-1775]
.L1063:
	movw	r1, #2049
	cmp	r0, r1
	cmpne	r0, r7
	moveq	r1, #16
	strbeq	r1, [r4, #-1775]
	cmp	r3, #218
	bne	.L1067
	ldr	r3, .L1149+32
	mov	r1, #2048
	strh	r1, [r3, #14]	@ movhi
	mvn	r3, #37
.L1143:
	strb	r3, [r2, #3410]
.L1068:
	mov	r2, #32
	ldr	r1, .L1149+36
	ldr	r0, .L1149+40
	bl	ftl_memcpy
	mov	r2, #32
	ldr	r1, .L1149+32
	ldr	r0, .L1149+44
	bl	ftl_memcpy
.L1061:
	ldrb	r3, [r5, #36]	@ zero_extendqisi2
	cmp	r3, #0
	bne	.L1071
	bl	FlashLoadPhyInfoInRam
	cmp	r0, #0
	bne	.L1073
	ldr	r3, [r5, #48]
	ldrh	r3, [r3, #16]
	lsr	r3, r3, #8
	tst	r3, #1
	and	r0, r3, #7
	strb	r0, [r4, #-1871]
	bne	.L1073
	mov	r3, #1
	strb	r3, [r4, #-1856]
	bl	FlashSetInterfaceMode
	ldrb	r0, [r4, #-1871]	@ zero_extendqisi2
	bl	NandcSetMode
.L1073:
	ldr	r3, [r5, #48]
	ldrb	r3, [r3, #26]	@ zero_extendqisi2
	strb	r3, [r4, #-2740]
	bl	FlashLoadPhyInfo
	cmp	r0, #0
	beq	.L1071
	ldr	r3, [r4, #-1852]
	cmp	r3, #0
	beq	.L1076
	mov	r0, #1
	bl	FlashSetInterfaceMode
	mov	r0, #1
.L1144:
	bl	NandcSetMode
	bl	FlashLoadPhyInfo
	cmp	r0, #0
	beq	.L1071
	mov	r0, #1
	bl	FlashSetInterfaceMode
	mov	r0, #1
	bl	NandcSetMode
	ldr	r3, [r5, #48]
	ldr	r0, .L1149+48
	ldrh	r1, [r3, #14]
	bl	printk
	bl	FlashLoadPhyInfoInRam
	cmn	r0, #1
	beq	.L1051
	bl	FlashDieInfoInit
	ldr	r3, [r5, #48]
	ldrb	r0, [r3, #19]	@ zero_extendqisi2
	bl	FlashGetReadRetryDefault
	ldr	r3, .L1149+52
	ldr	r2, [r5, #48]
	ldrh	r3, [r3]
	ldrb	r1, [r2, #9]	@ zero_extendqisi2
	add	r3, r3, #4080
	add	r3, r3, #15
	cmp	r1, r3, asr #12
	ldrh	r3, [r2, #14]
	blt	.L1078
	add	r0, r3, #255
	cmp	r1, r0, asr #8
	bge	.L1079
.L1078:
	bic	r3, r3, #255
	strh	r3, [r2, #14]	@ movhi
.L1079:
	ldrb	r3, [r4, #-1871]	@ zero_extendqisi2
	tst	r3, #6
	beq	.L1080
	bl	FlashSavePhyInfo
	mov	r0, #0
	bl	flash_enter_slc_mode
	ldr	r1, [r4, #-1784]
	mov	r0, #0
	bl	FlashDdrParaScan
	mov	r0, #0
	bl	flash_exit_slc_mode
.L1080:
	bl	FlashSavePhyInfo
.L1071:
	ldr	r9, [r5, #48]
	ldrb	r3, [r9, #26]	@ zero_extendqisi2
	ldrb	r1, [r9, #12]	@ zero_extendqisi2
	ldrh	r0, [r9, #10]
	strb	r3, [r4, #-2740]
	ldrh	r3, [r9, #16]
	ubfx	r2, r3, #7, #1
	strb	r2, [r5, #44]
	ubfx	r2, r3, #3, #1
	strb	r2, [r4, #-1755]
	ubfx	r2, r3, #4, #1
	ubfx	r3, r3, #8, #3
	strb	r2, [r4, #-1870]
	strb	r3, [r4, #-1871]
	mov	r3, #0
	str	r3, [r4, #-1792]
	bl	__aeabi_idiv
	mov	r1, r0
	ldrb	r0, [r9, #18]	@ zero_extendqisi2
	bl	BuildFlashLsbPageTable
	bl	FlashDieInfoInit
	ldr	r3, [r5, #48]
	ldrh	r2, [r3, #16]
	tst	r2, #64
	beq	.L1082
	ldrb	r0, [r3, #19]	@ zero_extendqisi2
	ldr	r3, .L1149+56
	ldr	r2, .L1149+60
	ldrb	r1, [r4, #-2726]	@ zero_extendqisi2
	strb	r0, [r3]
	ldrb	r3, [r4, #-2727]	@ zero_extendqisi2
	mov	ip, r2
	strb	r3, [r2]
	ldr	r3, .L1149+64
	strb	r1, [r3]
	sub	r1, r0, #1
	cmp	r1, #7
	bhi	.L1083
	ldr	r3, .L1149+68
	str	r3, [r4, #-1792]
	sub	r3, r0, #5
	cmp	r0, #8
	cmpne	r3, #1
	movls	r3, #1
	strls	r3, [r4, #-1804]
	cmp	r0, #7
	beq	.L1105
	cmp	r0, #8
	addne	r6, r6, #12
	bne	.L1085
.L1105:
	add	r6, r6, #20
.L1085:
	sub	r2, r6, #1
	mov	r3, #0
	add	r6, r6, #31
.L1087:
	ldrsb	r1, [r2, #1]!
	cmp	r1, #0
	addeq	r3, r3, #1
	cmp	r6, r2
	bne	.L1087
	cmp	r3, #27
	bls	.L1082
	bl	FlashGetReadRetryDefault
	bl	FlashSavePhyInfo
.L1082:
	ldr	r3, [r4, #-1844]
	cmp	r3, r7
	bne	.L1097
	ldrb	r2, [r4, #-2740]	@ zero_extendqisi2
	cmp	r2, #0
	ldrne	r2, [r5, #48]
	movne	r1, #0
	strbne	r1, [r2, #18]
.L1097:
	ldrb	r2, [r8]	@ zero_extendqisi2
	cmp	r2, #44
	bne	.L1098
	ldrb	r2, [r4, #-1856]	@ zero_extendqisi2
	cmp	r2, #0
	beq	.L1098
	cmp	r3, r7
	bne	.L1099
	ldrb	r3, [r4, #-2740]	@ zero_extendqisi2
	cmp	r3, #0
	bne	.L1098
.L1099:
	mov	r3, #0
	mov	r0, #1
	strb	r3, [r4, #-1856]
	bl	FlashSetInterfaceMode
	mov	r0, #1
	bl	NandcSetMode
.L1098:
	ldrb	r3, [r4, #-1871]	@ zero_extendqisi2
	tst	r3, #6
	beq	.L1100
	ldrb	r2, [r4, #-1856]	@ zero_extendqisi2
	cmp	r2, #0
	bne	.L1101
	tst	r3, #1
	bne	.L1100
.L1101:
	mov	r0, #0
	bl	flash_enter_slc_mode
	ldr	r1, [r4, #-1784]
	mov	r0, #0
	bl	FlashDdrParaScan
	mov	r0, #0
	bl	flash_exit_slc_mode
.L1100:
	ldr	r3, [r5, #48]
	mov	r9, #16
	ldr	r6, .L1149+72
	ldrb	r0, [r3, #20]	@ zero_extendqisi2
	bl	FlashBchSel
	ldr	r0, .L1149+76
	bl	FlashReadIdbDataRaw
	ldr	r0, .L1149+80
	strb	r9, [r5, #37]
	bl	FlashTimingCfg
	ldr	r7, [r5, #48]
	ldrb	r2, [r8, #1]	@ zero_extendqisi2
	ldrb	r3, [r7, #12]	@ zero_extendqisi2
	strh	r3, [r6, #8]	@ movhi
	ldrb	r3, [r7, #7]	@ zero_extendqisi2
	str	r3, [r4, #-2764]
	lsl	r3, r2, r9
	orr	r3, r3, r2, lsl #8
	ldrb	r2, [r8]	@ zero_extendqisi2
	orr	r3, r3, r2
	ldrb	r2, [r8, #3]	@ zero_extendqisi2
	orr	r3, r3, r2, lsl #24
	str	r3, [r4, #-2768]
	ldrb	r3, [r5, #3156]	@ zero_extendqisi2
	ldrh	r4, [r7, #14]
	strh	r3, [r6, #10]	@ movhi
	ldrb	r3, [r7, #13]	@ zero_extendqisi2
	strh	r4, [r6, #14]	@ movhi
	strh	r3, [r6, #12]	@ movhi
	ldrh	r3, [r7, #10]
	strh	r3, [r6, #16]	@ movhi
	ldrb	r1, [r7, #12]	@ zero_extendqisi2
	ldrh	r0, [r7, #10]
	bl	__aeabi_idiv
	strh	r0, [r6, #18]	@ movhi
	ldrb	r2, [r7, #9]	@ zero_extendqisi2
	strh	r2, [r6, #20]	@ movhi
	ldrh	r1, [r7, #10]
	ldrb	r3, [r7, #9]	@ zero_extendqisi2
	smulbb	r3, r3, r1
	mov	r1, #512
	strh	r1, [r6, #24]	@ movhi
	ldrb	r1, [r5, #37]	@ zero_extendqisi2
	uxth	r3, r3
	strh	r1, [r6, #26]	@ movhi
	ldrb	r1, [r5, #36]	@ zero_extendqisi2
	strh	r3, [r6, #22]	@ movhi
	cmp	r1, #1
	bne	.L1102
	lsl	r3, r3, #1
	lsr	r4, r4, #1
	lsl	r2, r2, #1
	strb	r9, [r5, #37]
	strh	r3, [r6, #22]	@ movhi
	mov	r3, #8
	strh	r4, [r6, #14]	@ movhi
	strh	r2, [r6, #20]	@ movhi
	strh	r3, [r6, #26]	@ movhi
.L1102:
	ldrb	r0, [r7, #20]	@ zero_extendqisi2
	bl	FlashBchSel
	bl	ftl_flash_suspend
	mov	r0, #0
.L1051:
	add	sp, sp, #28
	@ sp needed
	pop	{r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L1067:
	cmp	r3, #220
	ldreq	r3, .L1149+32
	moveq	r1, #4096
	strheq	r1, [r3, #14]	@ movhi
	mvneq	r3, #35
	beq	.L1143
.L1069:
	cmp	r3, #211
	ldreq	r3, .L1149+32
	moveq	r1, #4096
	strheq	r1, [r3, #14]	@ movhi
	moveq	r3, #2
	strbeq	r3, [r2, #3421]
	b	.L1068
.L1076:
	ldrb	r0, [r4, #-1871]	@ zero_extendqisi2
	bl	FlashSetInterfaceMode
	ldrb	r0, [r4, #-1871]	@ zero_extendqisi2
	b	.L1144
.L1083:
	sub	r1, r0, #17
	cmp	r1, #2
	bhi	.L1089
	ldr	r2, .L1149+84
	cmp	r0, #19
	str	r2, [r4, #-1792]
	moveq	r2, #15
	beq	.L1146
.L1148:
	mov	r2, #7
.L1146:
	strb	r2, [r3]
	b	.L1082
.L1089:
	sub	r1, r0, #65
	cmp	r0, #33
	cmpne	r1, #1
	ldrls	r1, .L1149+88
	strls	r1, [r4, #-1792]
	movls	r1, #4
	strbls	r1, [r2]
	bls	.L1148
.L1091:
	sub	r2, r0, #67
	sub	r1, r0, #34
	uxtb	r2, r2
	cmp	r2, #1
	cmphi	r1, #1
	movls	r1, #1
	movhi	r1, #0
	bhi	.L1092
	ldr	r1, .L1149+88
	cmp	r0, #68
	cmpne	r0, #35
	str	r1, [r4, #-1792]
	movne	r1, #7
	moveq	r1, #17
	cmp	r2, #1
	strb	r1, [r3]
	movls	r3, #4
	movhi	r3, #5
	strb	r3, [ip]
	b	.L1082
.L1092:
	cmp	r0, #49
	ldreq	r3, .L1149+92
	streq	r3, [r4, #-1792]
	beq	.L1082
	cmp	r0, #50
	ldreq	r3, .L1149+96
	streq	r1, [r4, #-1860]
	streq	r3, [r4, #-1792]
	b	.L1082
.L1104:
	mvn	r0, #1
	b	.L1051
.L1150:
	.align	2
.L1149:
	.word	.LANCHOR2
	.word	.LANCHOR0
	.word	IDByte
	.word	.LC14
	.word	.LANCHOR2-2728
	.word	1446522928
	.word	.LANCHOR1
	.word	.LANCHOR2-2720
	.word	.LANCHOR1+3408
	.word	.LANCHOR1+3284
	.word	.LANCHOR0+52
	.word	.LANCHOR1+468
	.word	.LC15
	.word	.LANCHOR0+3328
	.word	g_retryMode
	.word	g_maxRegNum
	.word	g_maxRetryCount
	.word	HynixReadRetrial
	.word	.LANCHOR2-2768
	.word	.LANCHOR0+3332
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
	mov	r6, r0
	ldr	r5, .L1167
	mov	r7, r1
	mov	r4, r2
	ldrb	r3, [r5, #-2740]	@ zero_extendqisi2
	cmp	r3, #0
	beq	.L1152
	ldr	r3, [r5, #-1860]
	cmp	r3, #0
	popne	{r4, r5, r6, r7, r8, r9, r10, pc}
.L1152:
	ldr	r2, .L1167+4
	ldr	r3, [r2, #48]
	mov	r8, r2
	ldrb	r3, [r3, #19]	@ zero_extendqisi2
	sub	r1, r3, #5
	cmp	r3, #50
	cmpne	r1, #2
	bls	.L1153
	sub	r2, r3, #19
	and	r2, r2, #239
	cmp	r2, #0
	cmpne	r3, #68
	popne	{r4, r5, r6, r7, r8, r9, r10, pc}
.L1153:
	ldr	r9, .L1167+8
	movw	r10, #65535
.L1155:
	ldr	r3, [r8, #48]
	ldrh	r3, [r3, #10]
	cmp	r3, r4
	bhi	.L1156
	pop	{r4, r5, r6, r7, r8, r9, r10, pc}
.L1156:
	lsl	r3, r4, #1
	ldrh	r3, [r9, r3]
	cmp	r3, r10
	popne	{r4, r5, r6, r7, r8, r9, r10, pc}
	mov	r2, #32768
	mov	r1, #255
	ldr	r0, [r5, #-1772]
	bl	ftl_memset
	ldr	r3, [r5, #-1772]
	add	r1, r4, r7
	mov	r0, r6
	add	r4, r4, #1
	uxth	r4, r4
	mov	r2, r3
	bl	FlashProgPage
	b	.L1155
.L1168:
	.align	2
.L1167:
	.word	.LANCHOR2
	.word	.LANCHOR0
	.word	.LANCHOR0+1108
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
	ldr	r3, .L1218
	push	{r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.save {r4, r5, r6, r7, r8, r9, r10, fp, lr}
	mov	r4, r0
	ldr	r10, .L1218+4
	mov	r8, #0
	.pad #36
	sub	sp, sp, #36
	ldr	r9, .L1218+8
	ldrb	r3, [r3, #477]	@ zero_extendqisi2
	str	r1, [sp, #16]
	str	r2, [sp, #20]
	str	r3, [sp, #12]
.L1170:
	ldr	r3, [sp, #16]
	cmp	r8, r3
	bne	.L1190
	mov	r0, #0
	add	sp, sp, #36
	@ sp needed
	pop	{r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L1190:
	ldr	r3, [sp, #16]
	add	r2, sp, #28
	ldr	r1, [sp, #20]
	mov	r0, r4
	sub	r3, r3, r8
	uxtb	r3, r3
	str	r3, [sp]
	add	r3, sp, #24
	bl	LogAddr2PhyAddr
	ldrb	r2, [r10, #3156]	@ zero_extendqisi2
	ldr	r3, [sp, #24]
	cmp	r2, r3
	mvnls	r3, #0
	strls	r3, [r4]
	bls	.L1172
	add	r3, r10, r3
	mov	r7, #0
	ldrb	r6, [r3, #3160]	@ zero_extendqisi2
	mov	r0, r6
	bl	NandcWaitFlashReady
	mov	r0, r6
	bl	NandcFlashCs
.L1173:
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
	ldrb	r3, [r9, #-1756]	@ zero_extendqisi2
	mov	r5, r0
	cmp	r3, #0
	beq	.L1174
	mov	r0, r6
	bl	flash_read_ecc
	cmp	r0, #5
	movhi	r5, #256
.L1174:
	cmp	r7, #9
	cmnls	r5, #1
	moveq	r3, #1
	movne	r3, #0
	addeq	r7, r7, #1
	beq	.L1173
.L1175:
	cmp	r7, #0
	mov	r7, r3
	movne	r5, #256
.L1177:
	ldr	r3, [r10, #40]
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
	ldrb	r2, [r9, #-1756]	@ zero_extendqisi2
	mov	fp, r0
	cmp	r2, #0
	beq	.L1180
	mov	r0, r6
	bl	flash_read_ecc
	cmp	r0, #5
	movhi	fp, #256
.L1180:
	cmp	r7, #9
	cmnls	fp, #1
	addeq	r7, r7, #1
	beq	.L1177
.L1181:
	cmp	r7, #0
	mov	r0, r6
	movne	fp, #256
	bl	NandcFlashDeCs
	ldrb	r3, [r9, #-2739]	@ zero_extendqisi2
	cmp	r5, fp
	movcc	r5, fp
	add	r3, r3, r3, lsl #1
	cmp	r5, r3, asr #2
	bls	.L1183
	cmn	r5, #1
	movne	r5, #256
.L1183:
	cmp	r5, #256
	cmnne	r5, #1
	movne	r3, #0
	streq	r5, [r4]
	strne	r3, [r4]
	ldr	r3, [r4, #12]
	cmp	r3, #0
	beq	.L1186
	ldr	r2, [r3, #12]
	cmn	r2, #1
	bne	.L1186
	ldr	r2, [r3, #8]
	cmn	r2, #1
	bne	.L1186
	ldr	r3, [r3]
	cmn	r3, #1
	strne	r2, [r4]
.L1186:
	ldr	r3, [r4]
	cmn	r3, #1
	bne	.L1172
	ldr	r1, [r4, #4]
	ldrb	r2, [r9, #-2739]	@ zero_extendqisi2
	ldr	r0, .L1218+12
	bl	printk
	ldr	r1, [r4, #8]
	cmp	r1, #0
	beq	.L1188
	mov	r3, #8
	mov	r2, #4
	ldr	r0, .L1218+16
	bl	rknand_print_hex
.L1188:
	ldr	r1, [r4, #12]
	cmp	r1, #0
	beq	.L1172
	mov	r3, #4
	ldr	r0, .L1218+20
	mov	r2, r3
	bl	rknand_print_hex
.L1172:
	add	r8, r8, #1
	add	r4, r4, #36
	b	.L1170
.L1219:
	.align	2
.L1218:
	.word	.LANCHOR1
	.word	.LANCHOR0
	.word	.LANCHOR2
	.word	.LC16
	.word	.LC17
	.word	.LC18
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
	ldr	r9, .L1291
	str	r1, [sp, #24]
	ldrb	r10, [r9, #36]	@ zero_extendqisi2
	str	r2, [sp, #28]
	cmp	r10, #0
	bne	.L1221
	ldr	r3, .L1291+4
	mov	fp, r0
	ldr	r6, .L1291+8
	str	r10, [sp, #8]
	ldrb	r3, [r3, #477]	@ zero_extendqisi2
	str	r3, [sp, #20]
	ldrb	r3, [r9, #44]	@ zero_extendqisi2
	str	r3, [sp, #36]
.L1222:
	ldr	r3, [sp, #8]
	ldr	r2, [sp, #24]
	cmp	r3, r2
	bcc	.L1255
	mov	r0, #0
	b	.L1220
.L1221:
	bl	FlashReadSlc2KPages
.L1220:
	add	sp, sp, #52
	@ sp needed
	pop	{r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L1255:
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
	ldrb	r2, [r9, #3156]	@ zero_extendqisi2
	mov	r5, r0
	ldr	r3, [sp, #40]
	cmp	r2, r3
	ldrls	r2, [sp, #12]
	mvnls	r3, #0
	strls	r3, [fp, r2]
	bls	.L1225
	add	r3, r9, r3
	ldrb	r4, [r3, #3160]	@ zero_extendqisi2
	ldrb	r3, [r6, #-1755]	@ zero_extendqisi2
	mov	r0, r4
	cmp	r3, #0
	moveq	r5, #0
	bl	NandcWaitFlashReady
	ldr	r3, [r9, #48]
	ldrb	r2, [r3, #19]	@ zero_extendqisi2
	sub	r3, r2, #1
	cmp	r3, #7
	bhi	.L1227
	sub	r2, r2, #7
	add	r1, r6, r4
	cmp	r2, #1
	add	r2, r6, r4
	ldrb	r3, [r1, #-2716]	@ zero_extendqisi2
	ldrb	r2, [r2, #-1876]	@ zero_extendqisi2
	ldrbls	r3, [r1, #-2708]	@ zero_extendqisi2
	cmp	r2, r3
	beq	.L1227
	ldr	r2, .L1291+12
	mov	r0, r4
	ldrb	r1, [r6, #-2727]	@ zero_extendqisi2
	bl	HynixSetRRPara
.L1227:
	mov	r0, r4
	lsr	r7, r7, #31
	bl	NandcFlashCs
	ldr	r3, [sp, #28]
	mov	r0, r4
	cmp	r3, #1
	orreq	r7, r7, #1
	cmp	r7, #0
	str	r7, [sp, #16]
	beq	.L1229
	ldrb	r3, [r6, #-2740]	@ zero_extendqisi2
	cmp	r3, #0
	beq	.L1229
	bl	flash_enter_slc_mode
.L1235:
	ldr	r1, [sp, #44]
	cmn	r1, #1
	cmpeq	r4, #255
	moveq	r3, #0
	movne	r3, #1
	moveq	r5, r3
	beq	.L1231
	cmp	r5, #0
	beq	.L1232
	ldr	r2, [r9, #40]
	mov	r0, r4
	add	r2, r1, r2
	bl	FlashReadDpCmd
.L1233:
	mov	r0, r4
	bl	NandcWaitFlashReady
	cmp	r5, #0
	beq	.L1231
	ldr	r1, [sp, #44]
	mov	r0, r4
	bl	FlashReadDpDataOutCmd
.L1231:
	ldr	r3, [r8, #12]
	mov	r1, #0
	ldr	r2, [sp, #20]
	mov	r0, r4
	str	r3, [sp]
	ldr	r3, [r8, #8]
	bl	NandcXferData
	ldrb	r3, [r9, #44]	@ zero_extendqisi2
	mov	r7, r0
	adds	r2, r3, #0
	movne	r2, #1
	cmn	r0, #1
	movne	r2, #0
	cmp	r2, #0
	str	r2, [sp, #32]
	beq	.L1234
	mov	r3, #0
	mov	r5, #0
	strb	r3, [r9, #44]
	b	.L1235
.L1229:
	bl	flash_exit_slc_mode
	b	.L1235
.L1232:
	mov	r0, r4
	bl	FlashReadCmd
	b	.L1233
.L1234:
	cmp	r5, #0
	beq	.L1236
	ldr	r3, [r9, #40]
	mov	r0, r4
	ldr	r1, [sp, #44]
	add	r1, r1, r3
	bl	FlashReadDpDataOutCmd
	ldr	r3, [sp, #12]
	mov	r0, r4
	ldr	r1, [sp, #32]
	add	r3, r3, #36
	add	r3, fp, r3
	ldr	r2, [r3, #12]
	str	r2, [sp]
	ldr	r2, [sp, #20]
	ldr	r3, [r3, #8]
	bl	NandcXferData
	cmn	r0, #1
	mov	r10, r0
	moveq	r5, #0
.L1236:
	mov	r0, r4
	bl	NandcFlashDeCs
	ldrb	r3, [sp, #36]	@ zero_extendqisi2
	cmn	r7, #1
	strb	r3, [r9, #44]
	bne	.L1237
	ldrb	r3, [r6, #-1856]	@ zero_extendqisi2
	cmp	r3, #0
	bne	.L1238
.L1242:
	ldr	r5, [r6, #-1792]
	cmp	r5, #0
	bne	.L1239
	ldr	r3, [r8, #12]
	mov	r0, r4
	ldr	r2, [r8, #8]
	ldr	r1, [sp, #44]
	bl	FlashReadRawPage
	mov	r7, r0
.L1243:
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
	bne	.L1250
	ldr	r1, [r8, #4]
	ldrb	r2, [r6, #-2739]	@ zero_extendqisi2
	ldr	r0, .L1291+16
	bl	printk
	ldr	r1, [r8, #12]
	cmp	r1, #0
	beq	.L1250
	mov	r3, #4
	ldr	r0, .L1291+20
	mov	r2, r3
	bl	rknand_print_hex
.L1250:
	cmp	r5, #0
	beq	.L1252
	ldrb	r3, [r6, #-2739]	@ zero_extendqisi2
	add	r3, r3, r3, lsl #1
	cmp	r10, r3, asr #2
	bls	.L1253
	ldr	r3, [r6, #-1792]
	cmp	r3, #0
	moveq	r10, #256
.L1253:
	ldr	r3, [sp, #12]
	cmp	r10, #256
	cmnne	r10, #1
	movne	r2, #0
	add	r3, r3, #36
	streq	r10, [fp, r3]
	strne	r2, [fp, r3]
.L1252:
	ldr	r3, [sp, #8]
	add	r3, r3, r5
	str	r3, [sp, #8]
	ldr	r3, [sp, #16]
	cmp	r3, #0
	beq	.L1225
	ldrb	r3, [r6, #-2740]	@ zero_extendqisi2
	cmp	r3, #0
	beq	.L1225
	mov	r0, r4
	bl	flash_exit_slc_mode
.L1225:
	ldr	r3, [sp, #8]
	add	r3, r3, #1
	str	r3, [sp, #8]
	b	.L1222
.L1238:
	ldr	r3, [r6, #-2804]
	mov	r0, r4
	ldr	r1, [sp, #44]
	ldr	r5, [r3, #304]
	mov	r3, #1
	str	r3, [sp]
	ldr	r3, [r8, #12]
	ldr	r2, [r8, #8]
	bl	FlashDdrTunningRead
	cmn	r0, #1
	mov	r7, r0
	beq	.L1241
	ldrb	r3, [r6, #-2739]	@ zero_extendqisi2
	cmp	r0, r3, lsr #1
	bls	.L1258
.L1241:
	ubfx	r0, r5, #8, #8
	bl	NandcSetDdrPara
	cmn	r7, #1
	beq	.L1242
.L1258:
	mov	r5, #0
.L1237:
	ldrb	r3, [r6, #-2739]	@ zero_extendqisi2
	add	r3, r3, r3, lsl #1
	cmp	r7, r3, asr #2
	bls	.L1243
	ldr	r3, [r6, #-1792]
	cmp	r3, #0
	moveq	r7, #256
	b	.L1243
.L1239:
	ldr	r3, [r8, #12]
	mov	r0, r4
	ldr	r2, [r8, #8]
	ldr	r1, [sp, #44]
	blx	r5
	cmn	r0, #1
	mov	r7, r0
	bne	.L1260
	ldr	r3, [r9, #48]
	ldrb	r3, [r3, #19]	@ zero_extendqisi2
	sub	r3, r3, #1
	cmp	r3, #7
	bhi	.L1244
	mov	r3, #0
	ldr	r2, .L1291+12
	ldrb	r1, [r6, #-2727]	@ zero_extendqisi2
	mov	r0, r4
	bl	HynixSetRRPara
.L1244:
	ldr	r3, [r8, #12]
	mov	r0, r4
	ldr	r2, [r8, #8]
	ldr	r1, [sp, #44]
	bl	FlashReadRawPage
	ldrb	r2, [r6, #-2739]	@ zero_extendqisi2
	mov	r7, r0
	mov	r3, r0
	ldr	r1, [r8, #4]
	ldr	r0, .L1291+24
	bl	printk
	cmn	r7, #1
	bne	.L1260
	ldrb	r5, [r6, #-2740]	@ zero_extendqisi2
	cmp	r5, #0
	beq	.L1243
	ldr	r3, [sp, #16]
	mov	r0, r4
	cmp	r3, #0
	beq	.L1245
	bl	flash_enter_slc_mode
.L1246:
	ldr	r5, [r6, #-1792]
	mov	r0, r4
	ldr	r3, [r8, #12]
	ldr	r2, [r8, #8]
	ldr	r1, [sp, #44]
	blx	r5
	mov	r7, r0
.L1260:
	mov	r5, #0
	b	.L1243
.L1245:
	bl	flash_exit_slc_mode
	b	.L1246
.L1292:
	.align	2
.L1291:
	.word	.LANCHOR0
	.word	.LANCHOR1
	.word	.LANCHOR2
	.word	.LANCHOR2-2724
	.word	.LC16
	.word	.LC18
	.word	.LC19
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
	ldr	r8, .L1305
	.pad #60
	sub	sp, sp, #60
	mov	r1, #0
	mov	r5, #0
	ldr	fp, .L1305+4
	mov	r9, r5
	sub	r3, r8, #2768
	sub	r4, r8, #1744
	ldrh	r6, [r3, #14]
	sub	r0, r4, #10
	ldrh	r3, [r3, #12]
	mvn	r10, #0
	smulbb	r6, r6, r3
	bl	ftl_memset
	uxth	r6, r6
	ldr	r3, [r8, #-1768]
	str	r5, [sp, #28]
	str	r4, [sp, #8]
	str	r3, [sp, #32]
.L1294:
	ldrb	r3, [fp, #3156]	@ zero_extendqisi2
	uxtb	r7, r5
	cmp	r3, r7
	bhi	.L1300
	mov	r0, r10
	add	sp, sp, #60
	@ sp needed
	pop	{r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L1300:
	sub	r4, r6, #1
	mul	r3, r7, r6
	uxth	r4, r4
	sub	r2, r6, #12
	str	r2, [sp, #4]
.L1295:
	ldr	r2, [sp, #4]
	cmp	r4, r2
	ble	.L1297
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
	beq	.L1296
	ldr	r2, [r8, #-1768]
	ldrh	r1, [r2]
	movw	r2, #61664
	cmp	r1, r2
	bne	.L1296
	mov	r1, r7
	mov	r2, r4
	ldr	r0, .L1305+8
	add	r9, r9, #1
	bl	printk
	ldr	r3, [sp, #8]
	uxth	r9, r9
	add	r7, r3, r7, lsl #1
	strh	r4, [r7, #-10]	@ movhi
.L1297:
	ldr	r3, .L1305+4
	add	r5, r5, #1
	ldrb	r3, [r3, #3156]	@ zero_extendqisi2
	cmp	r3, r9
	moveq	r10, #0
	b	.L1294
.L1296:
	sub	r4, r4, #1
	uxth	r4, r4
	b	.L1295
.L1306:
	.align	2
.L1305:
	.word	.LANCHOR2
	.word	.LANCHOR0
	.word	.LC20
	.fnend
	.size	FlashLoadFactorBbt, .-FlashLoadFactorBbt
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
	ldr	r5, .L1320
	.pad #40
	sub	sp, sp, #40
	mov	r6, r0
	mov	r9, r1
	sub	r2, r5, #2768
	ldrh	r3, [r2, #14]
	ldrh	r2, [r2, #12]
	smulbb	r3, r3, r2
	ldr	r2, [r5, #-1864]
	uxth	r3, r3
	str	r2, [sp, #12]
	ldr	r2, [r5, #-1768]
	sub	r7, r3, #1
	mul	r10, r1, r3
	uxth	r7, r7
	sub	r4, r3, #16
	str	r2, [sp, #16]
.L1308:
	cmp	r7, r4
	mvnle	r0, #0
	ble	.L1307
.L1314:
	add	r3, r7, r10
	mov	r2, #1
	lsl	r3, r3, #10
	mov	r1, r2
	add	r0, sp, #4
	str	r3, [sp, #8]
	bl	FlashReadPages
	ldr	r3, [sp, #4]
	cmn	r3, #1
	beq	.L1309
	ldr	r3, [r5, #-1768]
	ldrh	r2, [r3]
	movw	r3, #61664
	cmp	r2, r3
	bne	.L1309
	cmp	r6, #0
	moveq	r0, r6
	beq	.L1307
	cmp	r9, #0
	moveq	r1, r9
	moveq	lr, #1
	beq	.L1312
.L1311:
	mov	r2, r8
	ldr	r1, [r5, #-1864]
	mov	r0, r6
	bl	ftl_memcpy
	mov	r3, #4
	ldr	r0, .L1320+4
	mov	r2, r3
	mov	r1, r6
	bl	rknand_print_hex
	mov	r0, #0
.L1307:
	add	sp, sp, #40
	@ sp needed
	pop	{r4, r5, r6, r7, r8, r9, r10, pc}
.L1313:
	ldr	r0, [r5, #-1864]
	lsr	ip, r3, #5
	and	r3, r3, #31
	ldr	r2, [r0, ip, lsl #2]
	orr	r3, r2, lr, lsl r3
	str	r3, [r0, ip, lsl #2]
.L1312:
	ldr	r0, [r5, #-1780]
	uxth	r3, r1
	add	r1, r1, #1
	cmp	r3, r0
	bcc	.L1313
	b	.L1311
.L1309:
	sub	r7, r7, #1
	uxth	r7, r7
	b	.L1308
.L1321:
	.align	2
.L1320:
	.word	.LANCHOR2
	.word	.LC21
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
	ldr	r3, .L1333
	push	{r4, r5, r6, r7, r8, lr}
	.save {r4, r5, r6, r7, r8, lr}
	mov	r5, r0
	ldr	r6, .L1333+4
	ldr	r3, [r3, #48]
	ldr	r0, [r6, #-1772]
	ldrb	r4, [r3, #13]	@ zero_extendqisi2
	ldrh	r3, [r3, #14]
	smulbb	r4, r4, r3
	uxth	r4, r4
	add	r2, r4, #7
	asr	r2, r2, #3
	bl	FlashReadFacBbtData
	cmn	r0, #1
	bne	.L1323
.L1327:
	mov	r3, #0
.L1324:
	lsl	r3, r3, #1
	mvn	r2, #0
	mov	r0, #0
	strh	r2, [r5, r3]	@ movhi
	pop	{r4, r5, r6, r7, r8, pc}
.L1323:
	mov	r2, #0
	lsr	lr, r4, #4
	mov	r3, r2
	sub	r4, r4, #1
	mov	r7, #1
.L1325:
	uxth	r1, r2
	cmp	r1, r4
	bge	.L1324
	ldr	r8, [r6, #-1772]
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
	bcc	.L1325
	b	.L1327
.L1334:
	.align	2
.L1333:
	.word	.LANCHOR0
	.word	.LANCHOR2
	.fnend
	.size	FlashGetBadBlockList, .-FlashGetBadBlockList
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
	ldr	r3, .L1363
	push	{r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.save {r4, r5, r6, r7, r8, r9, r10, fp, lr}
	mov	r10, r1
	ldr	r9, .L1363+4
	.pad #60
	sub	sp, sp, #60
	mov	r8, r2
	mov	r4, r0
	ldrb	fp, [r3, #477]	@ zero_extendqisi2
	mov	r6, r0
	mov	r7, #0
.L1336:
	cmp	r7, r10
	bne	.L1342
	ldr	r5, .L1363+8
	mov	r6, #0
	ldr	r9, .L1363+12
.L1343:
	cmp	r7, r6
	bne	.L1350
	mov	r0, #0
	add	sp, sp, #60
	@ sp needed
	pop	{r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L1342:
	sub	r3, r10, r7
	add	r2, sp, #12
	uxtb	r3, r3
	mov	r1, r8
	mov	r0, r6
	str	r3, [sp]
	add	r3, sp, #16
	bl	LogAddr2PhyAddr
	ldrb	r2, [r9, #3156]	@ zero_extendqisi2
	ldr	r3, [sp, #16]
	cmp	r2, r3
	mvnls	r3, #0
	strls	r3, [r6]
	bls	.L1338
	add	r3, r9, r3
	ldrb	r5, [r3, #3160]	@ zero_extendqisi2
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
.L1338:
	add	r7, r7, #1
	add	r6, r6, #36
	b	.L1336
.L1350:
	ldr	r3, [r4]
	cmn	r3, #1
	bne	.L1344
	ldr	r1, [r4, #4]
	ldr	r0, .L1363+16
	bl	printk
.L1345:
	add	r6, r6, #1
	add	r4, r4, #36
	b	.L1343
.L1344:
	sub	r3, r7, r6
	add	r2, sp, #12
	uxtb	r3, r3
	mov	r1, r8
	mov	r0, r4
	str	r3, [sp]
	add	r3, sp, #16
	bl	LogAddr2PhyAddr
	ldr	r2, [r5, #-1764]
	mov	r3, #0
	mov	lr, r4
	add	ip, sp, #20
	str	r3, [r2]
	ldr	r2, [r5, #-1760]
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
	ldr	r3, [r5, #-1764]
	str	r3, [sp, #28]
	ldr	r3, [r5, #-1760]
	str	r3, [sp, #32]
	bl	FlashReadPages
	ldr	r10, [sp, #20]
	cmn	r10, #1
	bne	.L1346
	ldr	r1, [r4, #4]
	ldr	r0, .L1363+20
	bl	printk
	str	r10, [r4]
.L1346:
	ldr	r10, [sp, #20]
	cmp	r10, #256
	bne	.L1347
	ldr	r1, [r4, #4]
	ldr	r0, .L1363+24
	bl	printk
	str	r10, [r4]
.L1347:
	ldr	r3, [r4, #12]
	cmp	r3, #0
	beq	.L1348
	ldr	r2, [r3]
	ldr	r3, [r5, #-1760]
	ldr	r3, [r3]
	cmp	r2, r3
	beq	.L1348
	ldr	r1, [r4, #4]
	ldr	r0, .L1363+28
	bl	printk
	mvn	r3, #0
	str	r3, [r4]
.L1348:
	ldr	r3, [r4, #8]
	cmp	r3, #0
	beq	.L1345
	ldr	r2, [r3]
	ldr	r3, [r5, #-1764]
	ldr	r3, [r3]
	cmp	r2, r3
	beq	.L1345
	ldr	r1, [r4, #4]
	mov	r0, r9
	bl	printk
	mvn	r3, #0
	str	r3, [r4]
	b	.L1345
.L1364:
	.align	2
.L1363:
	.word	.LANCHOR1
	.word	.LANCHOR0
	.word	.LANCHOR2
	.word	.LC26
	.word	.LC22
	.word	.LC23
	.word	.LC24
	.word	.LC25
	.fnend
	.size	FlashProgSlc2KPages, .-FlashProgSlc2KPages
	.align	2
	.global	FlashProgPages
	.syntax unified
	.arm
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
	ldr	r6, .L1418
	str	r1, [sp, #8]
	ldr	ip, [r6, #48]
	ldrb	r8, [r6, #36]	@ zero_extendqisi2
	str	r3, [sp, #20]
	ldrb	ip, [ip, #19]	@ zero_extendqisi2
	cmp	r8, #0
	str	ip, [sp, #16]
	bne	.L1366
	ldr	r3, .L1418+4
	mov	r4, r0
	mov	r9, r2
	ldrb	r3, [r3, #477]	@ zero_extendqisi2
	str	r3, [sp, #12]
.L1367:
	ldr	r3, [sp, #8]
	cmp	r8, r3
	bcc	.L1380
	ldr	r7, .L1418+8
	mov	r5, #0
	ldr	r8, .L1418+12
.L1381:
	ldrb	r3, [r6, #3156]	@ zero_extendqisi2
	cmp	r5, r3
	bcc	.L1383
	ldr	r3, [sp, #20]
	cmp	r3, #0
	bne	.L1384
.L1392:
	mov	r0, #0
	b	.L1365
.L1366:
	bl	FlashProgSlc2KPages
.L1365:
	add	sp, sp, #76
	@ sp needed
	pop	{r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L1380:
	ldr	r3, [sp, #8]
	mov	r7, #36
	mul	r7, r7, r8
	add	r2, sp, #28
	mov	r1, r9
	sub	r3, r3, r8
	uxtb	r3, r3
	add	fp, r4, r7
	str	r3, [sp]
	mov	r0, fp
	add	r3, sp, #32
	bl	LogAddr2PhyAddr
	ldrb	r3, [r6, #3156]	@ zero_extendqisi2
	mov	r10, r0
	ldr	r0, [sp, #32]
	cmp	r3, r0
	mvnls	r3, #0
	strls	r3, [r4, r7]
	bls	.L1370
	ldr	r3, .L1418+8
	ldrb	r3, [r3, #-1870]	@ zero_extendqisi2
	cmp	r3, #0
	add	r3, r6, r0, lsl #4
	moveq	r10, #0
	ldr	r3, [r3, #3208]
	cmp	r3, #0
	beq	.L1372
	uxtb	r0, r0
	bl	FlashWaitCmdDone
.L1372:
	ldr	r3, [sp, #32]
	mov	r1, #0
	cmp	r10, #0
	add	r2, r6, r3, lsl #4
	str	r1, [r2, #3212]
	ldr	r1, [sp, #28]
	str	fp, [r2, #3208]
	str	r1, [r2, #3204]
	addne	r1, r7, #36
	addne	r1, r4, r1
	strne	r1, [r2, #3212]
	add	r2, r6, r3
	ldrb	r5, [r2, #3160]	@ zero_extendqisi2
	add	r3, r6, r3, lsl #4
	strb	r5, [r3, #3200]
	mov	r0, r5
	ldrb	r3, [r6, #3156]	@ zero_extendqisi2
	cmp	r3, #1
	bne	.L1374
	bl	NandcWaitFlashReady
.L1375:
	ldr	r3, [sp, #16]
	sub	r3, r3, #1
	cmp	r3, #7
	bhi	.L1376
	ldr	r1, .L1418+8
	add	r3, r1, r5
	ldrb	r3, [r3, #-1876]	@ zero_extendqisi2
	cmp	r3, #0
	beq	.L1376
	mov	r3, #0
	ldr	r2, .L1418+16
	ldrb	r1, [r1, #-2727]	@ zero_extendqisi2
	mov	r0, r5
	bl	HynixSetRRPara
.L1376:
	mov	r0, r5
	bl	NandcFlashCs
	cmp	r9, #1
	mov	r0, r5
	bne	.L1377
	ldr	r3, .L1418+8
	ldrb	r3, [r3, #-2740]	@ zero_extendqisi2
	cmp	r3, #0
	beq	.L1377
	bl	flash_enter_slc_mode
.L1378:
	ldr	r1, [sp, #28]
	mov	r0, r5
	bl	FlashProgFirstCmd
	ldr	r3, [fp, #12]
	mov	r1, #1
	ldr	r2, [sp, #12]
	mov	r0, r5
	str	r3, [sp]
	ldr	r3, [fp, #8]
	bl	NandcXferData
	cmp	r10, #0
	beq	.L1379
	ldr	r1, [sp, #28]
	mov	r0, r5
	bl	FlashProgDpFirstCmd
	ldr	r3, [sp, #32]
	mov	r0, r5
	ldr	r1, [sp, #28]
	add	r7, r7, #36
	add	r7, r4, r7
	add	r3, r6, r3, lsl #2
	ldr	r2, [r3, #3168]
	adds	r2, r2, #0
	movne	r2, #1
	bl	FlashWaitReadyEN
	ldr	r3, [r6, #40]
	mov	r0, r5
	ldr	r1, [sp, #28]
	add	r1, r1, r3
	bl	FlashProgDpSecondCmd
	ldr	r3, [r7, #12]
	mov	r1, #1
	ldr	r2, [sp, #12]
	mov	r0, r5
	str	r3, [sp]
	ldr	r3, [r7, #8]
	bl	NandcXferData
.L1379:
	ldr	r1, [sp, #28]
	mov	r0, r5
	add	r8, r8, r10
	bl	FlashProgSecondCmd
	mov	r0, r5
	bl	NandcFlashDeCs
.L1370:
	add	r8, r8, #1
	b	.L1367
.L1374:
	bl	NandcFlashCs
	ldr	r3, [sp, #32]
	mov	r0, r5
	ldr	r1, [sp, #28]
	add	r3, r6, r3, lsl #2
	ldr	r2, [r3, #3168]
	adds	r2, r2, #0
	movne	r2, #1
	bl	FlashWaitReadyEN
	mov	r0, r5
	bl	NandcFlashDeCs
	b	.L1375
.L1377:
	bl	flash_exit_slc_mode
	b	.L1378
.L1383:
	uxtb	r0, r5
	bl	FlashWaitCmdDone
	cmp	r9, #1
	bne	.L1382
	ldrb	r3, [r7, #-2740]	@ zero_extendqisi2
	cmp	r3, #0
	beq	.L1382
	ldrb	r0, [r8, r5, lsl #4]	@ zero_extendqisi2
	bl	flash_exit_slc_mode
.L1382:
	add	r5, r5, #1
	b	.L1381
.L1384:
	ldr	r5, .L1418+8
	mov	r6, #0
	ldr	r7, .L1418+20
.L1385:
	ldr	r3, [sp, #8]
	cmp	r6, r3
	beq	.L1392
	ldr	r3, [r4]
	cmn	r3, #1
	bne	.L1386
	ldr	r1, [r4, #4]
	ldr	r0, .L1418+24
	bl	printk
.L1387:
	add	r6, r6, #1
	add	r4, r4, #36
	b	.L1385
.L1386:
	ldr	r3, [sp, #8]
	add	r2, sp, #28
	mov	r1, r9
	mov	r0, r4
	sub	r3, r3, r6
	uxtb	r3, r3
	str	r3, [sp]
	add	r3, sp, #32
	bl	LogAddr2PhyAddr
	ldr	r2, [r5, #-1764]
	mov	r3, #0
	mov	lr, r4
	add	ip, sp, #36
	str	r3, [r2]
	ldr	r2, [r5, #-1760]
	str	r3, [r2]
	ldmia	lr!, {r0, r1, r2, r3}
	stmia	ip!, {r0, r1, r2, r3}
	ldmia	lr!, {r0, r1, r2, r3}
	stmia	ip!, {r0, r1, r2, r3}
	mov	r2, r9
	ldr	r3, [lr]
	mov	r1, #1
	add	r0, sp, #36
	str	r3, [ip]
	ldr	r3, [r5, #-1764]
	str	r3, [sp, #44]
	ldr	r3, [r5, #-1760]
	str	r3, [sp, #48]
	bl	FlashReadPages
	ldr	r8, [sp, #36]
	cmn	r8, #1
	bne	.L1388
	ldr	r1, [r4, #4]
	ldr	r0, .L1418+28
	bl	printk
	str	r8, [r4]
.L1388:
	ldr	r3, [r4, #12]
	cmp	r3, #0
	beq	.L1389
	ldr	r2, [r3]
	ldr	r3, [r5, #-1760]
	ldr	r3, [r3]
	cmp	r2, r3
	beq	.L1389
	ldr	r1, [r4, #4]
	ldr	r0, .L1418+32
	bl	printk
	mvn	r3, #0
	str	r3, [r4]
.L1389:
	ldr	r3, [r4, #8]
	cmp	r3, #0
	beq	.L1387
	ldr	r2, [r3]
	ldr	r3, [r5, #-1764]
	ldr	r3, [r3]
	cmp	r2, r3
	beq	.L1387
	ldr	r1, [r4, #4]
	mov	r0, r7
	bl	printk
	mvn	r3, #0
	str	r3, [r4]
	b	.L1387
.L1419:
	.align	2
.L1418:
	.word	.LANCHOR0
	.word	.LANCHOR1
	.word	.LANCHOR2
	.word	.LANCHOR0+3200
	.word	.LANCHOR2-2724
	.word	.LC26
	.word	.LC22
	.word	.LC23
	.word	.LC25
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
	ldr	r5, .L1424
	ldr	r3, [r5, #-1780]
	cmp	r0, r3
	movcc	r4, #0
	bcc	.L1420
	ldr	r3, [r5, #-1772]
	mov	r4, r0
	mov	r2, #32
	add	r0, sp, #40
	mov	r1, #165
	str	r0, [sp, #16]
	str	r3, [sp, #12]
	bl	ftl_memset
	mov	r2, #8
	mov	r1, #90
	ldr	r0, [r5, #-1772]
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
.L1420:
	mov	r0, r4
	add	sp, sp, #108
	@ sp needed
	pop	{r4, r5, pc}
.L1425:
	.align	2
.L1424:
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
	ldr	r3, .L1477
	push	{r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.save {r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.pad #84
	sub	sp, sp, #84
	ldr	r0, .L1477+4
	sub	r1, r3, #2768
	mov	r4, r3
	ldr	r2, [r3, #-1768]
	str	r2, [sp, #20]
	ldrh	r2, [r1, #14]
	ldrh	r1, [r1, #12]
	smulbb	r2, r2, r1
	uxth	r2, r2
	str	r2, [sp]
	ldr	r2, .L1477+8
	ldr	r1, [r2, #48]
	ldrb	r1, [r1, #24]	@ zero_extendqisi2
	str	r1, [sp, #24]
	ldrh	r1, [r2, #40]
	ldrb	r2, [r2, #36]	@ zero_extendqisi2
	str	r1, [sp, #16]
	cmp	r2, #1
	moveq	r3, r1
	mov	r1, #1
	lsleq	r3, r3, #1
	uxtheq	r3, r3
	streq	r3, [sp, #16]
	bl	printk
	ldr	r0, [r4, #-1768]
	mov	r2, #4096
	mov	r1, #0
	ldr	r4, .L1477
	bl	ftl_memset
	ldr	r3, [sp]
	lsr	r3, r3, #4
	str	r3, [sp, #28]
	mov	r3, #0
	str	r3, [sp, #8]
	sub	r3, r4, #1744
	sub	r3, r3, #10
	str	r3, [sp, #32]
.L1428:
	ldr	r5, .L1477+8
	ldrb	r7, [sp, #8]	@ zero_extendqisi2
	ldrb	r3, [r5, #3156]	@ zero_extendqisi2
	cmp	r3, r7
	bhi	.L1455
	add	sp, sp, #84
	@ sp needed
	pop	{r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L1455:
	ldr	r2, [sp, #32]
	lsl	r3, r7, #1
	ldrh	r6, [r2, r3]
	cmp	r6, #0
	bne	.L1429
	ldr	r3, .L1477+12
	mov	r1, r6
	ldr	r0, [r4, #-1864]
	add	fp, r5, r7, lsl #2
	mov	r8, r6
	ldrh	r2, [r3, #20]
	mov	r9, r6
	lsl	r2, r2, #9
	bl	ftl_memset
	add	r3, r5, r7
	str	r6, [sp, #4]
	ldrb	r10, [r3, #3160]	@ zero_extendqisi2
.L1430:
	ldrh	r3, [sp, #4]
	ldr	r2, [sp]
	str	r3, [sp, #12]
	cmp	r3, r2
	bcc	.L1441
.L1440:
	ldr	r5, .L1477+8
	mov	r2, r8
	mov	r1, r7
	ldr	r0, .L1477+16
	bl	printk
	ldrb	r3, [r5, #3156]	@ zero_extendqisi2
	ldr	r2, [sp, #28]
	mul	r3, r2, r3
	cmp	r8, r3
	mov	r8, r5
	blt	.L1442
	ldr	r3, .L1477+12
	mov	r1, #0
	ldr	r0, [r4, #-1864]
	ldrh	r2, [r3, #20]
	lsl	r2, r2, #9
	bl	ftl_memset
.L1442:
	cmp	r7, #0
	bne	.L1444
	sub	r3, r4, #1776
	ldr	r9, .L1477+20
	sub	r3, r3, #4
	ldrh	fp, [r3]
	mov	r10, #1
.L1445:
	ldrb	r3, [r8, #37]	@ zero_extendqisi2
	cmp	r3, fp
	bhi	.L1447
	ldr	r3, [sp]
	mov	r10, #1
	ldr	r9, .L1477+20
	sub	fp, r3, #1
	sub	r8, r3, #50
	uxth	fp, fp
.L1448:
	cmp	fp, r8
	bgt	.L1450
	ldrb	r3, [r5, #37]	@ zero_extendqisi2
	ldr	r2, [r4, #-1780]
	sub	r3, r3, r2
	cmp	r6, r3
	bcc	.L1444
	ldr	r3, .L1477+12
	mov	r1, #0
	ldr	r0, [r4, #-1864]
	ldrh	r2, [r3, #20]
	lsl	r2, r2, #9
	bl	ftl_memset
.L1444:
	ldr	r3, [sp]
	ldrb	r6, [sp, #8]	@ zero_extendqisi2
	ldr	r8, .L1477+24
	sub	r5, r3, #1
	ldr	r10, .L1477+28
	ldr	r9, .L1477+32
	uxth	r5, r5
	mul	r6, r3, r6
	add	r8, r8, r7, lsl #1
.L1452:
	mov	r1, r7
	mov	r2, r5
	mov	r0, r10
	bl	printk
	ldr	r1, [r4, #-1864]
.L1453:
	lsr	r2, r5, #5
	and	r3, r5, #31
	ldr	r2, [r1, r2, lsl #2]
	lsr	r3, r2, r3
	ands	r3, r3, #1
	bne	.L1454
	ldr	r2, [sp, #20]
	add	r0, sp, #44
	strh	r5, [r8]	@ movhi
	strh	r9, [r2]	@ movhi
	strh	r5, [r2, #2]	@ movhi
	strh	r3, [r2, #8]	@ movhi
	mov	r2, #1
	ldr	r3, [r4, #-1864]
	mov	r1, r2
	str	r3, [sp, #52]
	ldr	r3, [r4, #-1768]
	str	r3, [sp, #56]
	add	r3, r5, r6
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
	beq	.L1429
	sub	r5, r5, #1
	uxth	r5, r5
	b	.L1452
.L1441:
	mvn	r3, #0
	strb	r3, [sp, #42]
	strb	r3, [sp, #43]
	ldr	r3, [sp, #24]
	tst	r3, #1
	beq	.L1432
	ldr	r3, [fp, #3168]
	add	r2, sp, #42
	mov	r0, r10
	add	r3, r9, r3
	mov	r1, r3
	str	r3, [sp, #36]
	bl	FlashReadSpare
	ldrb	r2, [r5, #36]	@ zero_extendqisi2
	ldr	r3, [sp, #36]
	cmp	r2, #1
	bne	.L1432
	ldr	r1, [r5, #40]
	add	r2, sp, #43
	mov	r0, r10
	add	r1, r3, r1
	bl	FlashReadSpare
	ldrb	r3, [sp, #42]	@ zero_extendqisi2
	ldrb	r2, [sp, #43]	@ zero_extendqisi2
	and	r3, r3, r2
	strb	r3, [sp, #42]
.L1432:
	ldr	r3, [sp, #24]
	tst	r3, #2
	beq	.L1434
	ldr	r3, [r5, #48]
	add	r2, sp, #43
	mov	r0, r10
	ldrh	r1, [r3, #10]
	ldr	r3, [fp, #3168]
	sub	r1, r1, #1
	add	r1, r1, r3
	add	r1, r1, r9
	bl	FlashReadSpare
.L1434:
	ldr	r2, [r5, #48]
	ldrb	r3, [r2, #7]	@ zero_extendqisi2
	cmp	r3, #8
	cmpne	r3, #1
	ldrb	r3, [sp, #42]	@ zero_extendqisi2
	beq	.L1435
	ldrb	r2, [r2, #18]	@ zero_extendqisi2
	cmp	r2, #12
	bne	.L1436
.L1435:
	cmp	r3, #0
	ldrbne	r0, [sp, #43]	@ zero_extendqisi2
	clzne	r0, r0
	lsrne	r0, r0, #5
	bne	.L1437
.L1457:
	mov	r0, #1
	b	.L1437
.L1436:
	cmp	r3, #255
	bne	.L1457
	ldrb	r0, [sp, #43]	@ zero_extendqisi2
	subs	r0, r0, #255
	movne	r0, #1
.L1437:
	ldr	r3, [sp, #24]
	tst	r3, #4
	beq	.L1438
	ldr	r1, [fp, #3168]
	mov	r0, r10
	add	r1, r9, r1
	bl	SandiskProgTestBadBlock
.L1438:
	cmp	r0, #0
	beq	.L1439
	ldr	r2, [sp, #4]
	mov	r1, r7
	ldr	r0, .L1477+36
	add	r8, r8, #1
	bl	printk
	ldr	r3, [sp, #12]
	mov	ip, #1
	ldr	r2, [r4, #-1864]
	uxth	r8, r8
	and	r0, r3, #31
	lsr	r1, r3, #5
	ldr	r3, [r2, r1, lsl #2]
	orr	r3, r3, ip, lsl r0
	str	r3, [r2, r1, lsl #2]
	ldr	r2, [sp, #28]
	ldrb	r3, [r5, #3156]	@ zero_extendqisi2
	mul	r3, r2, r3
	cmp	r8, r3
	bgt	.L1440
.L1439:
	ldr	r3, [sp, #4]
	add	r3, r3, #1
	str	r3, [sp, #4]
	ldr	r3, [sp, #16]
	add	r9, r9, r3
	b	.L1430
.L1447:
	mov	r0, fp
	bl	FlashTestBlk
	cmp	r0, #0
	beq	.L1446
	mov	r1, fp
	mov	r0, r9
	bl	printk
	ldr	r1, [r4, #-1864]
	lsr	r0, fp, #5
	add	r6, r6, #1
	and	r3, fp, #31
	uxth	r6, r6
	ldr	r2, [r1, r0, lsl #2]
	orr	r3, r2, r10, lsl r3
	str	r3, [r1, r0, lsl #2]
.L1446:
	add	fp, fp, #1
	uxth	fp, fp
	b	.L1445
.L1450:
	mov	r0, fp
	bl	FlashTestBlk
	cmp	r0, #0
	beq	.L1449
	mov	r1, fp
	mov	r0, r9
	bl	printk
	ldr	r1, [r4, #-1864]
	lsr	r0, fp, #5
	and	r3, fp, #31
	ldr	r2, [r1, r0, lsl #2]
	orr	r3, r2, r10, lsl r3
	str	r3, [r1, r0, lsl #2]
.L1449:
	sub	fp, fp, #1
	uxth	fp, fp
	b	.L1448
.L1454:
	sub	r5, r5, #1
	uxth	r5, r5
	b	.L1453
.L1429:
	ldr	r3, [sp, #8]
	add	r3, r3, #1
	str	r3, [sp, #8]
	b	.L1428
.L1478:
	.align	2
.L1477:
	.word	.LANCHOR2
	.word	.LC27
	.word	.LANCHOR0
	.word	.LANCHOR2-2768
	.word	.LC29
	.word	.LC30
	.word	.LANCHOR2-1754
	.word	.LC31
	.word	-3872
	.word	.LC28
	.fnend
	.size	FlashMakeFactorBbt, .-FlashMakeFactorBbt
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
.L1480:
	cmp	r2, r0
	uxth	r3, r1
	add	r1, r1, #1
	bls	.L1481
	sub	r0, r3, #1
	uxth	r0, r0
	bx	lr
.L1481:
	lsl	r2, r2, #1
	b	.L1480
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
	ldr	r3, .L1484
	cmp	r0, #24
	movcc	r0, #24
	sub	r2, r3, #1728
	sub	ip, r3, #1712
	ldrh	r2, [r2, #-4]
	ldrh	r1, [ip, #-10]
	str	r0, [r3, #-1736]
	mul	r2, r0, r2
	sub	r0, r1, r0
	ldr	r1, [r3, #-1716]
	strh	r0, [ip, #-12]	@ movhi
	mov	r0, #0
	str	r2, [r3, #-1728]
	sub	r2, r1, r2
	str	r2, [r3, #-1720]
	bx	lr
.L1485:
	.align	2
.L1484:
	.word	.LANCHOR2
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
	@ args = 0, pretend = 0, frame = 24
	@ frame_needed = 0, uses_anonymous_args = 0
	push	{r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.save {r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.pad #28
	sub	sp, sp, #28
	ldr	ip, .L1514
	mov	r9, r0
	ldrh	r4, [r0, #14]
	ldrh	r5, [r0, #8]
	mov	r2, ip
	ldrh	r1, [r0, #10]
	ldrh	r3, [r0, #12]
	mov	r0, #0
	strh	r4, [ip, #-10]	@ movhi
	str	ip, [sp, #4]
	add	ip, ip, #6
	strh	r5, [r2], #16	@ movhi
	strh	r1, [r2, #-14]	@ movhi
	strh	r3, [r2, #-12]	@ movhi
	str	r2, [sp, #8]
.L1487:
	strb	r0, [r0, ip]
	add	r0, r0, #1
	cmp	r0, #32
	bne	.L1487
	ldrh	ip, [r9, #14]
	ldrh	r0, [r9, #20]
	cmp	r0, ip, lsr #8
	bcs	.L1488
	uxtb	r8, r3
	ldr	r7, .L1514+4
	lsl	r0, r8, #1
	uxtb	r0, r0
	str	r0, [sp, #12]
	sub	r0, r1, #1
	mul	r0, r3, r0
	str	r0, [sp, #20]
	mov	r0, #0
.L1489:
	cmp	r0, r3
	bcs	.L1491
	ldr	r2, [sp, #20]
	sub	fp, r0, r3
	uxtb	ip, r0
	add	fp, r7, fp
	add	lr, r0, r2
	add	r2, r7, lr
	mov	lr, #0
	str	r2, [sp, #16]
	mov	r6, lr
	b	.L1492
.L1490:
	ldr	r2, [sp, #16]
	add	r10, r8, ip
	strb	ip, [fp, lr]
	add	r6, r6, #1
	strb	r10, [r2, lr]
	ldr	r2, [sp, #12]
	add	ip, r2, ip
	uxtb	ip, ip
.L1492:
	cmp	r6, r1
	add	lr, lr, r3
	bcc	.L1490
	add	r0, r0, #1
	b	.L1489
.L1491:
	ldr	r2, [sp, #8]
	lsl	r1, r1, #1
	lsr	r4, r4, #1
	strh	r1, [r2, #-14]	@ movhi
	ldr	r2, [sp, #4]
	strh	r4, [r2, #-10]	@ movhi
.L1488:
	ldr	r2, [sp, #8]
	cmp	r5, #1
	ldr	r1, .L1514+8
	mov	r0, #5
	ldrh	r10, [r9, #16]
	ldrh	r7, [r2, #-14]
	strh	r0, [r1, #-10]	@ movhi
	mov	r0, #0
	strheq	r5, [r1, #-10]	@ movhi
	ldr	r5, .L1514+12
	smulbb	r7, r7, r3
	strh	r0, [r1, #-8]	@ movhi
	mov	r0, #4352
	sub	r2, r5, #1728
	strh	r0, [r1, #-6]	@ movhi
	uxth	r7, r7
	ldr	r0, .L1514+16
	sub	r6, r5, #1664
	strh	r7, [r2, #-4]	@ movhi
	sub	r8, r5, #1648
	ldr	r2, [sp, #4]
	ldrb	fp, [r0, #36]	@ zero_extendqisi2
	strh	r10, [r6, #-2]	@ movhi
	ldrh	r4, [r2, #-10]
	cmp	fp, #0
	ldrh	r2, [r9, #20]
	movne	r0, #384
	strhne	r0, [r1, #-6]	@ movhi
	smulbb	r3, r3, r4
	ldrh	r1, [r9, #18]
	mov	r0, r2
	strh	r2, [r8, #-12]	@ movhi
	str	r2, [sp, #8]
	strh	r3, [r6, #-4]	@ movhi
	smulbb	r3, r7, r10
	strh	r1, [r6]	@ movhi
	str	r1, [sp, #12]
	strh	r3, [r8, #-14]	@ movhi
	bl	Ftl_log2
	ldr	r2, [sp, #8]
	mov	r3, r0
	strh	r0, [r8, #-10]	@ movhi
	cmp	r4, #1024
	ldr	r1, [sp, #12]
	str	r3, [sp, #8]
	lsl	r0, r2, #9
	uxth	r0, r0
	mul	r1, r2, r1
	strh	r0, [r8, #-8]	@ movhi
	lsr	r0, r0, #8
	strh	r0, [r8, #-6]	@ movhi
	ldrh	r0, [r9, #26]
	ldr	r9, .L1514+20
	strh	r0, [r8, #-4]	@ movhi
	mul	r0, r4, r7
	str	r0, [r5, #-1716]
	uxtbhi	r0, r4
	strhhi	r0, [r6, #-8]	@ movhi
	ldrh	r0, [r6, #-8]
	sub	r0, r4, r0
	lsl	r4, r4, #6
	mul	r0, r7, r0
	mul	r0, r2, r0
	mul	r10, r10, r0
	ldrh	r0, [r6, #-6]
	asr	r10, r10, #11
	lsl	r0, r0, #3
	str	r10, [r5, #-1648]
	bl	__aeabi_idiv
	uxth	r0, r0
	ldr	r3, [sp, #8]
	mov	r1, r7
	cmp	r0, #4
	movls	r2, #4
	strhhi	r0, [r9, #-12]	@ movhi
	strhls	r2, [r9, #-12]	@ movhi
	cmp	fp, #0
	movne	r2, #640
	ldrh	r0, [r9, #-12]
	strhne	r2, [r6, #-6]	@ movhi
	ldrh	r2, [r6, #-6]
	asr	r2, r2, r3
	add	r3, r3, #9
	asr	r4, r4, r3
	add	r2, r2, #2
	strh	r4, [r9, #-8]	@ movhi
	uxth	r4, r4
	strh	r2, [r9, #-10]	@ movhi
	mul	r3, r4, r7
	add	r4, r4, #8
	str	r3, [r5, #-1636]
	bl	__aeabi_uidiv
	uxtah	r0, r4, r0
	cmp	r7, #1
	sub	r3, r5, #1728
	ldr	r7, .L1514+24
	addeq	r0, r0, #4
	sub	r3, r3, #8
	str	r0, [r5, #-1736]
	ldrh	r0, [r3]
	bl	FtlSysBlkNumInit
	ldr	r4, [r5, #-1720]
	mov	r0, #2048
	ldr	r3, [r5, #-1736]
	str	r3, [r5, #-1632]
	lsl	r3, r4, #2
	ldrh	r4, [r6, #-2]
	ldrh	r6, [r8, #-12]
	mul	r4, r4, r3
	ldrh	r3, [r8, #-10]
	mov	r1, r6
	add	r3, r3, #9
	lsr	r4, r4, r3
	add	r4, r4, #2
	uxth	r4, r4
	strh	r4, [r7, #-12]	@ movhi
	bl	__aeabi_idiv
	ldrh	r2, [r9, #-12]
	mov	r3, #0
	strh	r0, [r7, #-10]	@ movhi
	str	r3, [r5, #-2736]
	ldrb	r0, [r5, #-2740]	@ zero_extendqisi2
	add	r3, r2, #3
	strh	r3, [r9, #-12]	@ movhi
	ldr	r3, [r5, #-1636]
	cmp	r0, #0
	addne	r2, r2, #4
	add	r1, r3, #3
	strhne	r2, [r9, #-12]	@ movhi
	str	r1, [r5, #-1636]
	addne	r3, r3, #5
	bne	.L1513
	cmp	r1, #7
	bhi	.L1502
	mov	r3, #8
.L1513:
	str	r3, [r5, #-1636]
.L1502:
	mov	r3, #0
	mov	r0, #0
	strh	r3, [r7, #-8]	@ movhi
	ldr	r3, [sp, #4]
	ldrh	r2, [r3, #-12]
	lsr	r3, r2, #3
	add	r3, r3, r2, lsl #1
	add	r3, r3, #52
	add	r4, r3, r4, lsl #2
	cmp	r4, r6, lsl #9
	movcc	r3, #1
	strhcc	r3, [r7, #-8]	@ movhi
	add	sp, sp, #28
	@ sp needed
	pop	{r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L1515:
	.align	2
.L1514:
	.word	.LANCHOR2-1712
	.word	.LANCHOR2-1706
	.word	.LANCHOR2-1664
	.word	.LANCHOR2
	.word	.LANCHOR0
	.word	.LANCHOR2-1632
	.word	.LANCHOR2-1616
	.fnend
	.size	FtlConstantsInit, .-FtlConstantsInit
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
	push	{r3, r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.save {r3, r4, r5, r6, r7, r8, r9, r10, fp, lr}
	movw	r3, #65535
	ldr	r4, .L1619
	mvn	r2, #0
	mov	r5, #0
	mov	r0, #1024
	mov	r8, #12
	mov	r10, #36
	str	r3, [r4, #-1552]
	sub	r3, r4, #1536
	strh	r2, [r3]	@ movhi
	sub	r3, r4, #1520
	strh	r2, [r3, #-14]	@ movhi
	sub	r6, r4, #1648
	strh	r2, [r3, #-12]	@ movhi
	sub	r7, r4, #1616
	strh	r2, [r3, #-10]	@ movhi
	mov	r2, #32
	strh	r2, [r3, #-8]	@ movhi
	mov	r2, #128
	strh	r2, [r3, #-6]	@ movhi
	sub	r9, r4, #1728
	strh	r5, [r3, #-4]	@ movhi
	strh	r5, [r3, #-2]	@ movhi
	strh	r5, [r3]	@ movhi
	sub	r3, r4, #1504
	strh	r5, [r7, #-6]	@ movhi
	str	r5, [r4, #-1620]
	str	r5, [r4, #-1616]
	str	r5, [r4, #-1612]
	str	r5, [r4, #-1608]
	str	r5, [r4, #-1604]
	str	r5, [r4, #-1600]
	str	r5, [r4, #-1596]
	str	r5, [r4, #-1592]
	str	r5, [r4, #-1588]
	str	r5, [r4, #-1584]
	str	r5, [r4, #-1580]
	str	r5, [r4, #-1576]
	str	r5, [r4, #-1572]
	str	r5, [r4, #-1568]
	str	r5, [r4, #-1564]
	str	r5, [r4, #-1560]
	str	r5, [r4, #-1556]
	str	r5, [r4, #-1548]
	str	r5, [r4, #-1544]
	str	r5, [r4, #-1540]
	strh	r5, [r3, #-14]	@ movhi
	ldrh	r1, [r6, #-12]
	bl	__aeabi_idiv
	ldrh	r3, [r9, #-4]
	str	r0, [r4, #-1516]
	str	r5, [r4, #-1512]
	lsl	r3, r3, #2
	cmp	r0, r3
	ldrh	r0, [r6, #-14]
	strhi	r3, [r4, #-1516]
	lsl	r0, r0, #1
	bl	ftl_malloc
	str	r0, [r4, #-1508]
	ldrh	r0, [r6, #-14]
	mul	r0, r8, r0
	bl	ftl_malloc
	ldrh	r5, [r9, #-4]
	str	r0, [r4, #-1504]
	mul	r5, r10, r5
	lsl	fp, r5, #3
	mov	r0, fp
	bl	ftl_malloc
	str	r0, [r4, #-1500]
	mov	r0, r5
	bl	ftl_malloc
	str	r0, [r4, #-1496]
	mov	r0, fp
	bl	ftl_malloc
	str	r0, [r4, #-1492]
	mov	r0, r5
	bl	ftl_malloc
	str	r0, [r4, #-1488]
	mov	r0, r5
	bl	ftl_malloc
	str	r0, [r4, #-1484]
	ldr	r0, [r4, #-1516]
	mul	r0, r10, r0
	bl	ftl_malloc
	ldrh	r3, [r9, #-4]
	ldrh	r5, [r6, #-8]
	str	r0, [r4, #-1480]
	lsl	r3, r3, #1
	mov	r0, r5
	add	r3, r3, #1
	str	r3, [r4, #-1476]
	bl	ftl_malloc
	str	r0, [r4, #-1472]
	mov	r0, r5
	bl	ftl_malloc
	str	r0, [r4, #-1468]
	mov	r0, r5
	bl	ftl_malloc
	str	r0, [r4, #-1464]
	ldr	r0, [r4, #-1476]
	mul	r0, r0, r5
	bl	ftl_malloc
	str	r0, [r4, #-1460]
	ldr	r0, [r4, #-1516]
	mul	r0, r0, r5
	bl	ftl_malloc
	str	r0, [r4, #-1456]
	mov	r0, r5
	bl	ftl_malloc
	str	r0, [r4, #-1452]
	mov	r0, r5
	bl	ftl_malloc
	str	r0, [r4, #-1448]
	ldr	r0, [r4, #-1476]
	mul	r0, r8, r0
	bl	ftl_malloc
	ldrh	r3, [r6, #-6]
	ldrh	r5, [r9, #-4]
	str	r0, [r4, #-1444]
	mul	r5, r5, r3
	mov	r0, r5
	bl	ftl_malloc
	str	r0, [r4, #-1440]
	lsl	r0, r5, #3
	ldr	r5, .L1619+4
	bl	ftl_malloc
	ldrh	r3, [r6, #-6]
	str	r0, [r4, #-1436]
	add	r9, r5, #288
	ldr	r0, [r4, #-1476]
	mul	r0, r0, r3
	bl	ftl_malloc
	ldrh	r3, [r6, #-6]
	str	r0, [r4, #-1432]
	ldr	r0, [r4, #-1516]
	mul	r0, r0, r3
	bl	ftl_malloc
	str	r0, [r4, #-1428]
	ldrh	r0, [r5, #-10]
	lsl	r0, r0, #1
	uxth	r0, r0
	strh	r0, [r9]	@ movhi
	bl	ftl_malloc
	str	r0, [r4, #-1420]
	ldrh	r0, [r9]
	ldr	r3, .L1619+8
	add	r0, r0, #544
	add	r0, r0, #3
	lsr	r0, r0, #9
	strh	r0, [r9]	@ movhi
	and	r0, r3, r0, lsl #9
	bl	ftl_malloc
	ldrh	r9, [r5, #-10]
	str	r0, [r4, #-1416]
	add	r0, r0, #32
	str	r0, [r4, #-1412]
	lsl	r9, r9, #1
	mov	r0, r9
	bl	ftl_malloc
	str	r0, [r4, #-1408]
	mov	r0, r9
	bl	ftl_malloc
	ldr	r9, [r4, #-1636]
	str	r0, [r4, #-1404]
	lsl	r9, r9, #1
	mov	r0, r9
	bl	ftl_malloc
	str	r0, [r4, #-1400]
	mov	r0, r9
	bl	ftl_malloc
	str	r0, [r4, #-1396]
	ldrh	r0, [r5, #-10]
	lsr	r0, r0, #3
	add	r0, r0, #4
	bl	ftl_malloc
	ldr	r3, .L1619+12
	str	r0, [r3, #32]
	ldrh	r0, [r5, #68]
	lsl	r0, r0, #1
	bl	ftl_malloc
	str	r0, [r4, #-1392]
	ldrh	r0, [r5, #68]
	lsl	r0, r0, #1
	bl	ftl_malloc
	str	r0, [r4, #-1388]
	ldrh	r0, [r5, #68]
	lsl	r0, r0, #2
	bl	ftl_malloc
	str	r0, [r4, #-1384]
	ldrh	r0, [r5, #70]
	lsl	r0, r0, #2
	bl	ftl_malloc
	ldrh	r2, [r5, #70]
	mov	r1, #0
	str	r0, [r4, #-1380]
	lsl	r2, r2, #2
	bl	ftl_memset
	ldrh	r9, [r7, #-12]
	lsl	r9, r9, #2
	mov	r0, r9
	bl	ftl_malloc
	str	r0, [r4, #-1376]
	mov	r0, r9
	bl	ftl_malloc
	str	r0, [r4, #-1372]
	ldr	r0, [r4, #-1636]
	lsl	r0, r0, #2
	bl	ftl_malloc
	str	r0, [r4, #-1368]
	ldrh	r0, [r7, #-10]
	mul	r0, r8, r0
	bl	ftl_malloc
	ldrh	r3, [r7, #-10]
	add	r7, r5, #16
	str	r0, [r4, #-1364]
	ldrh	r0, [r6, #-8]
	add	r6, r5, #368
	mul	r0, r0, r3
	bl	ftl_malloc
	ldrh	r3, [r5, #-10]
	str	r0, [r4, #-1360]
	mov	r0, #6
	mul	r0, r0, r3
	bl	ftl_malloc
	str	r0, [r4, #-1356]
	ldrh	r0, [r5, #44]
	ldrh	r3, [r5, #2]
	add	r0, r0, #31
	asr	r0, r0, #5
	strh	r0, [r6, #-8]	@ movhi
	mul	r0, r0, r3
	lsl	r0, r0, #2
	bl	ftl_malloc
	ldrh	r2, [r6, #-8]
	mov	r3, #1
	ldrh	ip, [r5, #2]
	add	r5, r5, #392
	str	r0, [r4, #-1320]
	lsl	r2, r2, #2
	mov	r1, r2
.L1518:
	cmp	r3, ip
	bcc	.L1519
	add	r3, r6, r3, lsl #2
	mov	r2, #0
	add	r6, r6, #52
	add	r3, r3, #20
.L1520:
	cmp	r6, r3
	bne	.L1521
	ldr	r3, [r4, #-1400]
	cmp	r3, #0
	bne	.L1522
.L1524:
	ldr	r1, .L1619+16
	ldr	r0, .L1619+20
	bl	printk
	mvn	r0, #0
	pop	{r3, r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L1519:
	ldr	r0, [r4, #-1320]
	add	r3, r3, #1
	add	r0, r0, r1
	add	r1, r1, r2
	str	r0, [r5, #4]!
	b	.L1518
.L1521:
	str	r2, [r3, #4]!
	b	.L1520
.L1522:
	ldr	r3, [r4, #-1396]
	cmp	r3, #0
	beq	.L1524
	ldr	r3, [r4, #-1376]
	cmp	r3, #0
	beq	.L1524
	ldr	r3, [r4, #-1368]
	cmp	r3, #0
	beq	.L1524
	ldr	r3, [r4, #-1364]
	cmp	r3, #0
	beq	.L1524
	ldr	r3, [r4, #-1360]
	cmp	r3, #0
	beq	.L1524
	ldr	r3, [r4, #-1356]
	cmp	r3, #0
	beq	.L1524
	ldr	r3, [r4, #-1320]
	cmp	r3, #0
	beq	.L1524
	ldr	r3, [r4, #-1404]
	cmp	r3, #0
	beq	.L1524
	ldr	r3, [r4, #-1508]
	cmp	r3, #0
	beq	.L1524
	ldr	r3, [r4, #-1504]
	cmp	r3, #0
	beq	.L1524
	ldr	r3, [r4, #-1500]
	cmp	r3, #0
	beq	.L1524
	ldr	r3, [r4, #-1492]
	cmp	r3, #0
	beq	.L1524
	ldr	r3, [r4, #-1488]
	cmp	r3, #0
	beq	.L1524
	ldr	r3, [r4, #-1484]
	cmp	r3, #0
	beq	.L1524
	ldr	r3, [r4, #-1496]
	cmp	r3, #0
	beq	.L1524
	ldr	r3, [r4, #-1472]
	cmp	r3, #0
	beq	.L1524
	ldr	r3, [r4, #-1468]
	cmp	r3, #0
	beq	.L1524
	ldr	r3, [r4, #-1464]
	cmp	r3, #0
	beq	.L1524
	ldr	r3, [r4, #-1460]
	cmp	r3, #0
	beq	.L1524
	ldr	r3, [r4, #-1452]
	cmp	r3, #0
	beq	.L1524
	ldr	r3, [r4, #-1448]
	cmp	r3, #0
	beq	.L1524
	ldr	r3, [r4, #-1444]
	cmp	r3, #0
	beq	.L1524
	ldr	r3, [r4, #-1440]
	cmp	r3, #0
	beq	.L1524
	ldr	r3, [r4, #-1436]
	cmp	r3, #0
	beq	.L1524
	ldr	r3, [r4, #-1432]
	cmp	r3, #0
	beq	.L1524
	ldr	r3, [r4, #-1412]
	cmp	r3, #0
	beq	.L1524
	ldr	r3, [r4, #-1420]
	cmp	r3, #0
	beq	.L1524
	ldr	r3, [r4, #-1392]
	cmp	r3, #0
	beq	.L1524
	ldr	r3, .L1619
	ldr	r2, [r3, #-1388]
	cmp	r2, #0
	beq	.L1524
	ldr	r2, [r3, #-1384]
	cmp	r2, #0
	beq	.L1524
	ldr	r3, [r3, #-1380]
	cmp	r3, #0
	beq	.L1524
	mov	r0, #0
	pop	{r3, r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L1620:
	.align	2
.L1619:
	.word	.LANCHOR2
	.word	.LANCHOR2-1712
	.word	33553920
	.word	.LANCHOR0
	.word	.LANCHOR3+130
	.word	.LC32
	.fnend
	.size	FtlMemInit, .-FtlMemInit
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
	ldr	r2, .L1628
	sub	r3, r2, #1280
	ldrh	r3, [r3, #-8]
	cmp	r3, #0
	beq	.L1627
	ldr	r3, [r2, #-1392]
	sub	r2, r2, #1632
	ldrh	r2, [r2, #-12]
	add	r2, r3, r2, lsl #1
.L1623:
	cmp	r3, r2
	bne	.L1624
.L1627:
	mov	r0, #0
	bx	lr
.L1624:
	ldrh	r1, [r3], #2
	cmp	r0, r1
	bne	.L1623
	mov	r0, #1
	bx	lr
.L1629:
	.align	2
.L1628:
	.word	.LANCHOR2
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
	ldr	r2, .L1640
	ldr	r3, [r2, #-1512]
	cmp	r3, #0
	beq	.L1633
	push	{r4, r5, lr}
	.save {r4, r5, lr}
	mov	r5, #36
	ldr	r4, [r2, #-1480]
	mov	r2, #0
.L1632:
	mla	ip, r5, r2, r4
	ldr	lr, [ip, #16]
	cmp	lr, r1
	movhi	ip, #0
	movls	ip, #1
	cmp	lr, r0
	movcc	ip, #0
	cmp	ip, #0
	bne	.L1634
	add	r2, r2, #1
	cmp	r3, r2
	bne	.L1632
	mov	r0, ip
	pop	{r4, r5, pc}
.L1633:
	mov	r0, r3
	bx	lr
.L1634:
	mov	r0, #1
	pop	{r4, r5, pc}
.L1641:
	.align	2
.L1640:
	.word	.LANCHOR2
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
	ldr	r3, .L1643
	ldr	r0, [r3, #-2736]
	bx	lr
.L1644:
	.align	2
.L1643:
	.word	.LANCHOR2
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
	ldr	r3, .L1646
	ldr	r0, [r3, #-2736]
	bx	lr
.L1647:
	.align	2
.L1646:
	.word	.LANCHOR2
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
	ldr	r3, .L1649
	ldr	r0, [r3, #-2736]
	bx	lr
.L1650:
	.align	2
.L1649:
	.word	.LANCHOR2
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
	ldr	r3, .L1652
	ldr	r0, [r3, #-1284]
	bx	lr
.L1653:
	.align	2
.L1652:
	.word	.LANCHOR2
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
	mov	r5, r0
	ldr	r4, .L1656
	sub	r3, r4, #1664
	ldrh	r7, [r3, #-4]
	mov	r1, r7
	bl	__aeabi_uidiv
	uxth	r6, r0
	mov	r1, r7
	mov	r0, r5
	bl	__aeabi_uidivmod
	add	r2, r4, r6, lsl #2
	uxth	r3, r1
	ldr	r2, [r2, #-1320]
	lsr	r1, r3, #5
	and	ip, r3, #31
	mov	lr, #1
	ldr	r0, [r2, r1, lsl #2]
	orr	r0, r0, lr, lsl ip
	str	r0, [r2, r1, lsl #2]
	mov	r2, r6
	str	r0, [sp]
	mov	r1, r5
	ldr	r0, .L1656+4
	bl	printk
	sub	r3, r4, #1344
	mov	r0, #0
	ldrh	r2, [r3, #2]
	add	r2, r2, #1
	strh	r2, [r3, #2]	@ movhi
	add	sp, sp, #12
	@ sp needed
	pop	{r4, r5, r6, r7, pc}
.L1657:
	.align	2
.L1656:
	.word	.LANCHOR2
	.word	.LC33
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
	mov	r7, r0
	ldr	r5, .L1660
	sub	r3, r5, #1664
	ldrh	r6, [r3, #-4]
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
	ldr	r3, [r5, #-1320]
	ldr	r0, [r3, r2, lsl #2]
	lsr	r0, r0, r4
	and	r0, r0, #1
	pop	{r4, r5, r6, r7, r8, pc}
.L1661:
	.align	2
.L1660:
	.word	.LANCHOR2
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
	.global	FtlBbt2Bitmap
	.syntax unified
	.arm
	.fpu softvfp
	.type	FtlBbt2Bitmap, %function
FtlBbt2Bitmap:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	push	{r4, r5, r6, lr}
	.save {r4, r5, r6, lr}
	mov	r6, r1
	ldr	r5, .L1669
	mov	r4, r0
	mov	r1, #0
	mov	r0, r6
	ldrh	r2, [r5, #-8]
	sub	r5, r5, #4
	lsl	r2, r2, #2
	bl	ftl_memset
	add	r0, r4, #1020
	sub	r2, r4, #2
	add	r0, r0, #2
	movw	r4, #65535
	mov	lr, #1
.L1665:
	ldrh	r3, [r2, #2]!
	cmp	r3, r4
	popeq	{r4, r5, r6, pc}
	lsr	ip, r3, #5
	and	r3, r3, #31
	cmp	r2, r0
	ldr	r1, [r6, ip, lsl #2]
	orr	r3, r1, lr, lsl r3
	str	r3, [r6, ip, lsl #2]
	ldrh	r3, [r5, #6]
	add	r3, r3, #1
	strh	r3, [r5, #6]	@ movhi
	bne	.L1665
	pop	{r4, r5, r6, pc}
.L1670:
	.align	2
.L1669:
	.word	.LANCHOR2-1344
	.fnend
	.size	FtlBbt2Bitmap, .-FtlBbt2Bitmap
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
	push	{r0, r1, r4, r5, r6, r7, r8, r9, r10, lr}
	.save {r4, r5, r6, r7, r8, r9, r10, lr}
	.pad #8
	ldr	r4, .L1686
	ldr	r6, [r4, #-1280]
	cmp	r6, #0
	bne	.L1673
	ldr	r0, [r4, #-1472]
	sub	r7, r4, #1648
	ldr	r3, [r4, #-1440]
	sub	r5, r4, #1344
	ldrh	r2, [r7, #-8]
	add	r7, r7, #324
	mov	r1, r6
	str	r0, [r4, #-1268]
	str	r3, [r4, #-1264]
	bl	ftl_memset
.L1674:
	ldr	r3, .L1686+4
	ldrh	r3, [r3]
	cmp	r6, r3
	blt	.L1675
	ldr	r6, [r4, #-1264]
	mov	r2, #16
	mov	r1, #255
	ldr	r9, .L1686+8
	mov	r7, #0
	mov	r0, r6
	mov	r8, r7
	bl	ftl_memset
	ldr	r3, .L1686+12
	strh	r3, [r6]	@ movhi
	ldr	r3, [r4, #-1340]
	str	r3, [r6, #4]
	ldrh	r3, [r5, #-4]
	strh	r3, [r6, #2]	@ movhi
	ldrh	r3, [r5], #-4
	strh	r3, [r6, #8]	@ movhi
	ldrh	r3, [r5, #6]
	strh	r3, [r6, #10]	@ movhi
	ldr	r3, [r4, #-1736]
	strh	r3, [r6, #12]	@ movhi
.L1676:
	ldr	r3, [r4, #-1472]
	mov	r10, #0
	ldrh	r2, [r5, #2]
	ldrh	r1, [r5]
	str	r3, [r4, #-1268]
	ldr	r3, [r4, #-1440]
	str	r10, [r4, #-1276]
	str	r3, [r4, #-1264]
	orr	r3, r2, r1, lsl #10
	ldrh	r0, [r6, #10]
	str	r3, [r4, #-1272]
	ldrh	r3, [r5, #4]
	str	r0, [sp]
	mov	r0, r9
	bl	printk
	ldr	r3, .L1686+16
	ldrh	r2, [r5, #2]
	ldrh	r3, [r3]
	sub	r3, r3, #1
	cmp	r2, r3
	blt	.L1677
	ldr	r3, [r4, #-1340]
	ldrh	r2, [r5]
	ldr	r0, [r4, #-1488]
	add	r3, r3, #1
	strh	r10, [r5, #2]	@ movhi
	str	r3, [r4, #-1340]
	str	r3, [r6, #4]
	ldrh	r3, [r5, #4]
	strh	r2, [r6, #8]	@ movhi
	strh	r2, [r5, #4]	@ movhi
	mov	r2, #1
	strh	r3, [r5]	@ movhi
	mov	r1, r2
	lsl	r3, r3, #10
	str	r3, [r4, #-1272]
	str	r3, [r0, #4]
	bl	FlashEraseBlocks
.L1677:
	mov	r3, #1
	ldr	r0, .L1686+20
	mov	r2, r3
	mov	r1, r3
	bl	FlashProgPages
	ldrh	r3, [r5, #2]
	add	r3, r3, #1
	strh	r3, [r5, #2]	@ movhi
	ldr	r3, [r4, #-1276]
	cmn	r3, #1
	bne	.L1678
	add	r7, r7, #1
	ldr	r1, [r4, #-1272]
	uxth	r7, r7
	ldr	r0, .L1686+24
	bl	printk
	cmp	r7, #3
	bls	.L1676
	mov	r2, r7
	ldr	r1, [r4, #-1272]
	ldr	r0, .L1686+28
	bl	printk
	mov	r3, #1
	str	r3, [r4, #-1280]
.L1673:
	mov	r0, #0
	add	sp, sp, #8
	@ sp needed
	pop	{r4, r5, r6, r7, r8, r9, r10, pc}
.L1675:
	ldrh	r2, [r5, #-8]
	ldr	r3, [r4, #-1268]
	ldr	r1, [r7, #4]!
	mul	r0, r6, r2
	lsl	r2, r2, #2
	add	r6, r6, #1
	add	r0, r3, r0, lsl #2
	bl	ftl_memcpy
	b	.L1674
.L1681:
	mov	r8, #1
	b	.L1676
.L1678:
	add	r8, r8, #1
	cmp	r8, #1
	ble	.L1681
	cmp	r3, #256
	bne	.L1673
	b	.L1676
.L1687:
	.align	2
.L1686:
	.word	.LANCHOR2
	.word	.LANCHOR2-1710
	.word	.LC34
	.word	-3887
	.word	.LANCHOR2-1664
	.word	.LANCHOR2-1276
	.word	.LC35
	.word	.LC36
	.fnend
	.size	FtlBbmTblFlush, .-FtlBbmTblFlush
	.align	2
	.global	FtlLoadFactoryBbt
	.syntax unified
	.arm
	.fpu softvfp
	.type	FtlLoadFactoryBbt, %function
FtlLoadFactoryBbt:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	push	{r4, r5, r6, r7, r8, r9, r10, lr}
	.save {r4, r5, r6, r7, r8, r9, r10, lr}
	mov	r8, #0
	ldr	r5, .L1699
	ldr	r3, [r5, #-1472]
	sub	r7, r5, #1328
	ldr	r10, [r5, #-1440]
	sub	r9, r5, #1264
	sub	r7, r7, #10
	sub	r9, r9, #12
	str	r3, [r5, #-1268]
	str	r10, [r5, #-1264]
.L1689:
	ldr	r6, .L1699+4
	ldrh	r3, [r6]
	cmp	r8, r3
	bcc	.L1694
	mov	r0, #0
	pop	{r4, r5, r6, r7, r8, r9, r10, pc}
.L1694:
	ldrh	r4, [r6, #42]
	mvn	r3, #0
	add	r6, r6, #46
	strh	r3, [r7, #2]!	@ movhi
	add	r4, r4, r3
	uxth	r4, r4
.L1690:
	ldrh	r3, [r6, #-4]
	sub	r2, r3, #16
	cmp	r4, r2
	ble	.L1692
	mla	r3, r8, r3, r4
	mov	r2, #1
	mov	r1, r2
	mov	r0, r9
	lsl	r3, r3, #10
	str	r3, [r5, #-1272]
	bl	FlashReadPages
	ldr	r3, [r5, #-1276]
	cmn	r3, #1
	beq	.L1691
	ldrh	r2, [r10]
	movw	r3, #61664
	cmp	r2, r3
	bne	.L1691
	strh	r4, [r7]	@ movhi
.L1692:
	add	r8, r8, #1
	b	.L1689
.L1691:
	sub	r4, r4, #1
	uxth	r4, r4
	b	.L1690
.L1700:
	.align	2
.L1699:
	.word	.LANCHOR2
	.word	.LANCHOR2-1710
	.fnend
	.size	FtlLoadFactoryBbt, .-FtlLoadFactoryBbt
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
	ldr	r0, .L1702
	mvn	r2, #0
	mov	r1, #255
	strh	r2, [r0, #-4]	@ movhi
	mov	r2, #0
	strh	r2, [r0, #2]	@ movhi
	mov	r2, #16
	add	r0, r0, #8
	b	ftl_memset
.L1703:
	.align	2
.L1702:
	.word	.LANCHOR2-1344
	.fnend
	.size	FtlBbtMemInit, .-FtlBbtMemInit
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
	ldr	r3, .L1712
	push	{r4, r5, r6, lr}
	.save {r4, r5, r6, lr}
	mov	r5, #0
	mov	r4, r5
	ldrh	r2, [r3, #-4]
	ldrh	r6, [r3, #-46]
	mul	r6, r6, r2
.L1705:
	uxth	r0, r5
	cmp	r0, r6
	blt	.L1707
	mov	r0, r4
	pop	{r4, r5, r6, pc}
.L1707:
	bl	FtlBbmIsBadBlock
	cmp	r0, #0
	add	r5, r5, #1
	addne	r4, r4, #1
	uxthne	r4, r4
	b	.L1705
.L1713:
	.align	2
.L1712:
	.word	.LANCHOR2-1664
	.fnend
	.size	FtlBbtCalcTotleCnt, .-FtlBbtCalcTotleCnt
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
	ldr	r4, .L1735
	ldr	r7, [r4, #-1280]
	cmp	r7, #0
	bne	.L1715
	ldr	r10, .L1735+4
	sub	fp, r4, #1264
	sub	r8, r4, #1344
	sub	fp, fp, #12
	bl	FtlBbtMemInit
	sub	r9, r10, #18
	bl	FtlLoadFactoryBbt
.L1716:
	ldr	r5, .L1735+8
	ldrh	r3, [r5]
	cmp	r7, r3
	bcc	.L1722
	mov	r5, #0
.L1723:
	ldr	r3, .L1735+12
	uxth	r0, r5
	add	r5, r5, #1
	ldrh	r3, [r3]
	cmp	r3, r0
	bhi	.L1724
	sub	r6, r8, #4
	ldrh	r5, [r6, #12]
	movw	r7, #65535
	sub	r5, r5, #1
	uxth	r5, r5
.L1725:
	ldrh	r3, [r6, #12]
	sub	r3, r3, #48
	cmp	r5, r3
	ble	.L1729
	mov	r0, r5
	bl	FtlBbmIsBadBlock
	cmp	r0, #1
	beq	.L1726
	mov	r0, r5
	bl	FlashTestBlk
	cmp	r0, #0
	beq	.L1727
	mov	r0, r5
	bl	FtlBbmMapBadBlock
.L1726:
	sub	r5, r5, #1
	uxth	r5, r5
	b	.L1725
.L1722:
	ldr	r3, [r4, #-1440]
	movw	r2, #65535
	ldr	r0, [r4, #-1472]
	str	r3, [sp, #4]
	str	r3, [r4, #-1264]
	ldrh	r3, [r9, #2]!
	str	r0, [r4, #-1268]
	cmp	r3, r2
	beq	.L1717
	ldrh	r6, [r5, #42]
	mov	r2, #1
	mov	r1, r2
	mov	r0, fp
	mla	r6, r7, r6, r3
	lsl	r3, r6, #10
	str	r3, [r4, #-1272]
	bl	FlashReadPages
	ldrh	r2, [r5, #42]
	ldr	r1, [r4, #-1268]
	ldr	r0, [r10]
	add	r2, r2, #7
	asr	r2, r2, #3
	bl	ftl_memcpy
.L1718:
	uxth	r0, r6
	add	r7, r7, #1
	add	r10, r10, #4
	bl	FtlBbmMapBadBlock
	b	.L1716
.L1717:
	mov	r1, r7
	bl	FlashGetBadBlockList
	ldr	r1, [r10]
	ldr	r0, [r4, #-1268]
	bl	FtlBbt2Bitmap
	ldrh	r5, [r5, #42]
.L1720:
	sub	r5, r5, #1
	uxth	r5, r5
.L1719:
	ldr	r3, .L1735+16
	ldrh	r0, [r3, #-4]
	smlabb	r0, r0, r7, r5
	uxth	r0, r0
	bl	FtlBbmIsBadBlock
	cmp	r0, #1
	beq	.L1720
	mov	r2, #16
	mov	r1, #0
	strh	r5, [r9]	@ movhi
	ldr	r0, [r4, #-1440]
	bl	ftl_memset
	ldr	r3, [sp, #4]
	movw	r2, 61664	@ movhi
	strh	r2, [r3]	@ movhi
	mov	r3, #0
	ldr	r2, [sp, #4]
	str	r3, [r2, #4]
	ldrh	r3, [r9]
	strh	r3, [r2, #2]	@ movhi
	ldr	r3, .L1735+16
	ldrh	r2, [r8, #-8]
	ldr	r1, [r10]
	ldrh	r6, [r3, #-4]
	ldrh	r3, [r9]
	lsl	r2, r2, #2
	ldr	r0, [r4, #-1268]
	mla	r6, r7, r6, r3
	lsl	r3, r6, #10
	str	r3, [r4, #-1272]
	bl	ftl_memcpy
	mov	r2, #1
	mov	r0, fp
	mov	r1, r2
	bl	FlashEraseBlocks
	mov	r3, #1
	mov	r0, fp
	mov	r2, r3
	mov	r1, r3
	bl	FlashProgPages
	ldr	r3, [r4, #-1276]
	cmn	r3, #1
	bne	.L1718
	uxth	r0, r6
	bl	FtlBbmMapBadBlock
	b	.L1719
.L1724:
	bl	FtlBbmMapBadBlock
	b	.L1723
.L1727:
	ldrh	r3, [r6]
	cmp	r3, r7
	strheq	r5, [r6]	@ movhi
	beq	.L1726
.L1728:
	strh	r5, [r6, #4]	@ movhi
.L1729:
	ldrh	r3, [r8, #-4]
	sub	r5, r8, #4
	ldr	r0, [r4, #-1488]
	mov	r6, #0
	str	r6, [r4, #-1340]
	mov	r2, #2
	mov	r1, #1
	strh	r6, [r8, #-2]	@ movhi
	lsl	r3, r3, #10
	str	r3, [r0, #4]
	ldrh	r3, [r5, #4]
	lsl	r3, r3, #10
	str	r3, [r0, #40]
	bl	FlashEraseBlocks
	ldrh	r0, [r8, #-4]
	bl	FtlBbmMapBadBlock
	ldrh	r0, [r5, #4]
	bl	FtlBbmMapBadBlock
	bl	FtlBbmTblFlush
	ldr	r3, [r4, #-1340]
	ldrh	r2, [r5, #4]
	strh	r6, [r8, #-2]	@ movhi
	add	r3, r3, #1
	str	r3, [r4, #-1340]
	ldrh	r3, [r8, #-4]
	strh	r2, [r8, #-4]	@ movhi
	strh	r3, [r5, #4]	@ movhi
	bl	FtlBbmTblFlush
.L1715:
	mov	r0, #0
	add	sp, sp, #12
	@ sp needed
	pop	{r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L1736:
	.align	2
.L1735:
	.word	.LANCHOR2
	.word	.LANCHOR2-1320
	.word	.LANCHOR2-1710
	.word	.LANCHOR2-1652
	.word	.LANCHOR2-1664
	.fnend
	.size	FtlMakeBbt, .-FtlMakeBbt
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
	mov	r5, r1
	ldr	r4, .L1739
	mov	r7, r0
	sub	r3, r4, #1696
	sub	r4, r4, #1664
	ldrh	r6, [r3, #-12]
	mov	r1, r6
	bl	__aeabi_uidiv
	ldrh	r4, [r4, #-4]
	smulbb	r5, r6, r5
	mov	r1, r6
	smulbb	r4, r4, r0
	mov	r0, r7
	bl	__aeabi_uidivmod
	add	r0, r5, r1
	add	r0, r4, r0
	uxth	r0, r0
	pop	{r4, r5, r6, r7, r8, pc}
.L1740:
	.align	2
.L1739:
	.word	.LANCHOR2
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
	ldr	r3, .L1743
	push	{r4, r5, r6, lr}
	.save {r4, r5, r6, lr}
	mov	r6, r0
	sub	r2, r3, #1696
	sub	r3, r3, #1664
	ldrh	r5, [r2, #-12]
	ldrh	r1, [r3, #-4]
	bl	__aeabi_uidiv
	mov	r1, r5
	smulbb	r4, r0, r5
	mov	r0, r6
	bl	__aeabi_uidivmod
	add	r1, r4, r1
	uxth	r0, r1
	pop	{r4, r5, r6, pc}
.L1744:
	.align	2
.L1743:
	.word	.LANCHOR2
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
	ldr	r4, .L1747
	sub	r3, r4, #1664
	sub	r4, r4, #1696
	ldrh	r1, [r3, #-4]
	bl	__aeabi_uidivmod
	uxth	r0, r1
	ldrh	r1, [r4, #-12]
	bl	__aeabi_uidiv
	uxth	r0, r0
	pop	{r4, pc}
.L1748:
	.align	2
.L1747:
	.word	.LANCHOR2
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
	bls	.L1750
	sub	r0, r0, r1
	cmp	r0, #-2147483648
	movhi	r0, #0
	movls	r0, #1
	bx	lr
.L1750:
	sub	r0, r1, r0
	cmp	r0, #-2147483648
	movls	r0, #0
	movhi	r0, #1
	bx	lr
	.fnend
	.size	ftl_cmp_data_ver, .-ftl_cmp_data_ver
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
	ldr	r3, .L1764
	cmp	r1, #1
	push	{r4, r5, r6, r7, r8, lr}
	.save {r4, r5, r6, r7, r8, lr}
	lsl	r8, r0, #10
	.pad #104
	sub	sp, sp, #104
	mov	r2, r1
	mov	r7, r1
	ldrheq	r5, [r3]
	mov	r6, #0
	ldrhne	r5, [r3, #-2]
	add	r3, sp, #40
	str	r3, [sp, #16]
	mov	r1, #1
	add	r0, sp, #4
	str	r6, [sp, #12]
	sub	r5, r5, #1
	sxth	r5, r5
	orr	r3, r5, r8
	str	r3, [sp, #8]
	bl	FlashReadPages
	ldr	r3, [sp, #40]
	cmn	r3, #1
	bne	.L1755
.L1756:
	cmp	r6, r5
	ble	.L1759
.L1755:
	mov	r0, r5
	add	sp, sp, #104
	@ sp needed
	pop	{r4, r5, r6, r7, r8, pc}
.L1759:
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
	bne	.L1757
	ldr	r3, [sp, #44]
	cmn	r3, #1
	bne	.L1757
	ldr	r3, [sp, #4]
	cmn	r3, #1
	subne	r4, r4, #1
	sxthne	r5, r4
	bne	.L1756
.L1757:
	add	r4, r4, #1
	sxth	r6, r4
	b	.L1756
.L1765:
	.align	2
.L1764:
	.word	.LANCHOR2-1664
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
	push	{r4, r5, r6, r7, r8, lr}
	.save {r4, r5, r6, r7, r8, lr}
	ldr	r4, .L1798
	ldr	r3, [r4, #-1472]
	sub	r7, r4, #1664
	ldr	r6, [r4, #-1440]
	sub	r8, r4, #1264
	sub	r8, r8, #12
	str	r3, [r4, #-1268]
	str	r6, [r4, #-1264]
	bl	FtlBbtMemInit
	ldrh	r5, [r7, #-4]
	sub	r5, r5, #1
	uxth	r5, r5
.L1767:
	ldrh	r3, [r7, #-4]
	sub	r3, r3, #48
	cmp	r5, r3
	ble	.L1770
	lsl	r3, r5, #10
	mov	r2, #1
	mov	r1, r2
	mov	r0, r8
	str	r3, [r4, #-1272]
	bl	FlashReadPages
	ldr	r3, [r4, #-1276]
	cmn	r3, #1
	bne	.L1768
	ldr	r3, [r4, #-1272]
	mov	r2, #1
	mov	r1, r2
	mov	r0, r8
	add	r3, r3, #1
	str	r3, [r4, #-1272]
	bl	FlashReadPages
.L1768:
	ldr	r3, [r4, #-1276]
	cmn	r3, #1
	beq	.L1769
	ldrh	r2, [r6]
	movw	r3, #61649
	cmp	r2, r3
	bne	.L1769
	ldr	r2, [r6, #4]
	ldr	r3, .L1798+4
	str	r2, [r4, #-1340]
	ldrh	r2, [r6, #8]
	strh	r5, [r3, #-4]	@ movhi
	strh	r2, [r3]	@ movhi
.L1770:
	ldr	r5, .L1798+4
	movw	r2, #65535
	ldrh	r3, [r5, #-4]
	sub	r7, r5, #4
	cmp	r3, r2
	beq	.L1784
	ldrh	r3, [r7, #4]
	cmp	r3, r2
	beq	.L1774
	lsl	r3, r3, #10
	mov	r2, #1
	mov	r1, r2
	add	r0, r5, #68
	str	r3, [r4, #-1272]
	bl	FlashReadPages
	ldr	r3, [r4, #-1276]
	cmn	r3, #1
	beq	.L1774
	ldrh	r2, [r6]
	movw	r3, #61649
	cmp	r2, r3
	bne	.L1774
	ldr	r3, [r6, #4]
	ldr	r2, [r4, #-1340]
	cmp	r3, r2
	ldrhhi	r2, [r7, #4]
	strhi	r3, [r4, #-1340]
	ldrhhi	r3, [r6, #8]
	strhhi	r2, [r5, #-4]	@ movhi
	strhhi	r3, [r7, #4]	@ movhi
.L1774:
	ldr	r8, .L1798+8
	mov	r1, #1
	ldrh	r0, [r5, #-4]
	bl	FtlGetLastWrittenPage
	sxth	r7, r0
	add	r0, r0, #1
	strh	r0, [r5, #-2]	@ movhi
.L1776:
	cmp	r7, #0
	blt	.L1781
	ldrh	r3, [r5, #-4]
	mov	r2, #1
	mov	r1, r2
	mov	r0, r8
	orr	r3, r7, r3, lsl #10
	str	r3, [r4, #-1272]
	ldr	r3, [r4, #-1472]
	str	r3, [r4, #-1268]
	bl	FlashReadPages
	ldr	r3, [r4, #-1276]
	cmn	r3, #1
	beq	.L1777
	ldrh	r2, [r6]
	movw	r3, #61649
	cmp	r2, r3
	bne	.L1777
.L1781:
	ldrh	r3, [r6, #10]
	ldrh	r0, [r6, #12]
	strh	r3, [r5, #2]	@ movhi
	movw	r3, #65535
	cmp	r0, r3
	bne	.L1778
.L1779:
	add	r7, r5, #20
	mov	r6, #0
.L1782:
	ldr	r3, .L1798+12
	ldrh	r3, [r3]
	cmp	r6, r3
	bcc	.L1783
	mov	r0, #0
	pop	{r4, r5, r6, r7, r8, pc}
.L1769:
	sub	r5, r5, #1
	uxth	r5, r5
	b	.L1767
.L1777:
	sub	r7, r7, #1
	sxth	r7, r7
	b	.L1776
.L1778:
	ldr	r2, [r4, #-1736]
	cmp	r0, r2
	beq	.L1779
	ldr	r3, .L1798+16
	ldrh	r3, [r3, #-10]
	lsr	r3, r3, #2
	cmp	r2, r3
	cmpcc	r0, r3
	bcs	.L1779
	bl	FtlSysBlkNumInit
	b	.L1779
.L1783:
	ldrh	r2, [r5, #-8]
	ldr	r1, [r4, #-1268]
	ldr	r0, [r7, #4]!
	lsl	r2, r2, #2
	mla	r1, r6, r2, r1
	add	r6, r6, #1
	bl	ftl_memcpy
	b	.L1782
.L1784:
	mvn	r0, #0
	pop	{r4, r5, r6, r7, r8, pc}
.L1799:
	.align	2
.L1798:
	.word	.LANCHOR2
	.word	.LANCHOR2-1344
	.word	.LANCHOR2-1276
	.word	.LANCHOR2-1710
	.word	.LANCHOR2-1712
	.fnend
	.size	FtlLoadBbt, .-FtlLoadBbt
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
	ldr	r3, .L1802
	mov	r2, #2048
	push	{r4, lr}
	.save {r4, lr}
	mov	r4, #0
	mov	r1, r4
	strh	r0, [r3, #-8]	@ movhi
	mov	r0, r3
	strh	r4, [r3, #-6]	@ movhi
	strh	r4, [r3, #-4]	@ movhi
	strh	r4, [r3, #-2]	@ movhi
	bl	ftl_memset
	mov	r0, r4
	pop	{r4, pc}
.L1803:
	.align	2
.L1802:
	.word	.LANCHOR2-1232
	.fnend
	.size	FtlFreeSysBlkQueueInit, .-FtlFreeSysBlkQueueInit
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
	ldr	r3, .L1805
	ldrh	r0, [r3, #6]
	clz	r0, r0
	lsr	r0, r0, #5
	bx	lr
.L1806:
	.align	2
.L1805:
	.word	.LANCHOR2-1240
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
	ldr	r3, .L1808
	ldrh	r0, [r3, #6]
	sub	r0, r0, #1024
	clz	r0, r0
	lsr	r0, r0, #5
	bx	lr
.L1809:
	.align	2
.L1808:
	.word	.LANCHOR2-1240
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
	ldr	r5, .L1823
	sub	r3, r5, #1232
	ldrh	r2, [r3, #-2]
	mov	r4, r3
	cmp	r2, #1024
	popeq	{r4, r5, r6, r7, r8, pc}
	cmp	r1, #0
	mov	r6, r0
	beq	.L1812
	ldr	r3, [r5, #-1280]
	cmp	r3, #0
	bne	.L1812
	bl	P2V_block_in_plane
	mov	r7, r0
	ldr	r0, [r5, #-1488]
	lsl	r3, r6, #10
	mov	r2, #1
	mov	r1, r2
	str	r3, [r0, #4]
	bl	FlashEraseBlocks
	ldr	r2, [r5, #-1412]
	lsl	r0, r7, #1
	ldrh	r3, [r2, r0]
	add	r3, r3, #1
	strh	r3, [r2, r0]	@ movhi
	ldr	r3, [r5, #-1572]
	add	r3, r3, #1
	str	r3, [r5, #-1572]
.L1812:
	ldrh	r2, [r4, #-2]
	sub	r3, r4, #8
	add	r2, r2, #1
	strh	r2, [r4, #-2]	@ movhi
	ldrh	r2, [r4, #-4]
	add	r1, r3, r2, lsl #1
	add	r2, r2, #1
	ubfx	r2, r2, #0, #10
	strh	r6, [r1, #8]	@ movhi
	strh	r2, [r4, #-4]	@ movhi
	pop	{r4, r5, r6, r7, r8, pc}
.L1824:
	.align	2
.L1823:
	.word	.LANCHOR2
	.fnend
	.size	FtlFreeSysBlkQueueIn, .-FtlFreeSysBlkQueueIn
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
	ldr	r3, .L1870
	push	{r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.save {r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.pad #28
	sub	sp, sp, #28
	ldr	r2, [r3, #-1280]
	cmp	r2, #0
	movne	r4, #0
	bne	.L1825
	mov	r10, r3
	ldrb	r3, [r3, #-1870]	@ zero_extendqisi2
	ldrb	r8, [r10, #-2740]	@ zero_extendqisi2
	mov	r7, r1
	mov	fp, r2
	mov	r5, r2
	mov	r4, r2
	mov	r9, #36
	str	r0, [sp, #4]
	str	r3, [sp, #16]
	str	r0, [r10, #-1540]
.L1827:
	ldr	r1, .L1870+4
	ldrh	r0, [r1]
	uxth	r1, fp
	cmp	r0, r1
	bhi	.L1831
	cmp	r5, #0
	beq	.L1825
	adds	r8, r8, #0
	mov	r6, #0
	movne	r8, #1
	mov	r2, r5
	mov	r1, r8
	ldr	r0, [r10, #-1488]
	strb	r6, [r10, #-1870]
	mov	r9, #36
	bl	FlashEraseBlocks
	ldrb	r3, [sp, #16]	@ zero_extendqisi2
	strb	r3, [r10, #-1870]
.L1833:
	uxth	r2, r6
	cmp	r5, r2
	bhi	.L1835
	cmp	r7, #0
	bne	.L1836
	uxth	r8, r8
	mov	r3, #6
	str	r3, [sp, #12]
	mov	r3, #1
	str	r3, [sp, #8]
.L1837:
	ldr	r5, .L1870
	mov	fp, #0
.L1846:
	mov	r10, #0
	mov	r6, r10
.L1838:
	ldr	r3, .L1870+8
	ldrh	r1, [r3, #-4]
	uxth	r3, r10
	cmp	r1, r3
	bhi	.L1841
	cmp	r6, #0
	beq	.L1825
	mov	r3, #1
	mov	r2, r8
	mov	r9, #0
	mov	r1, r6
	ldr	r0, [r5, #-1488]
	strb	r9, [r5, #-1870]
	bl	FlashProgPages
	ldrb	r3, [sp, #16]	@ zero_extendqisi2
	mov	r2, #36
	strb	r3, [r5, #-1870]
.L1843:
	uxth	r3, r9
	cmp	r6, r3
	bhi	.L1845
	ldr	r3, [sp, #12]
	add	fp, fp, r3
	ldr	r3, [sp, #8]
	uxth	fp, fp
	cmp	r3, fp
	bhi	.L1846
	mov	r9, #0
	mov	fp, #36
.L1847:
	uxth	r3, r9
	cmp	r6, r3
	bhi	.L1849
	ldr	r3, [sp, #4]
	adds	r7, r7, #0
	movne	r7, #1
	cmp	r3, #63
	movhi	r10, r7
	orrls	r10, r7, #1
	cmp	r10, #0
	beq	.L1825
	mov	r2, r6
	mov	r1, r8
	ldr	r0, [r5, #-1488]
	bl	FlashEraseBlocks
.L1825:
	mov	r0, r4
	add	sp, sp, #28
	@ sp needed
	pop	{r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L1831:
	uxth	r1, fp
	ldr	ip, [r10, #-1488]
	mov	r3, #0
	mul	r0, r9, r1
	str	r3, [ip, r0]
	add	r0, r10, r1
	ldrb	r0, [r0, #-1706]	@ zero_extendqisi2
	ldr	r1, [sp, #4]
	bl	V2P_block
	cmp	r7, #0
	mov	r6, r0
	beq	.L1828
	bl	IsBlkInVendorPart
	cmp	r0, #0
	bne	.L1829
.L1828:
	mov	r0, r6
	bl	FtlBbmIsBadBlock
	cmp	r0, #0
	addne	r4, r4, #1
	uxthne	r4, r4
	bne	.L1829
	ldr	r1, .L1870+12
	lsl	r6, r6, #10
	ldr	ip, [r10, #-1488]
	ldrh	r1, [r1]
	mla	ip, r9, r5, ip
	mul	r1, r5, r1
	add	r5, r5, #1
	uxth	r5, r5
	str	r0, [ip, #8]
	str	r6, [ip, #4]
	add	r0, r1, #3
	cmp	r1, #0
	movlt	r1, r0
	ldr	r0, [r10, #-1436]
	bic	r1, r1, #3
	add	r1, r0, r1
	str	r1, [ip, #12]
.L1829:
	add	fp, fp, #1
	b	.L1827
.L1835:
	mul	r2, r9, r6
	ldr	r1, [r10, #-1488]
	add	ip, r1, r2
	ldr	r2, [r1, r2]
	cmn	r2, #1
	bne	.L1834
	ldr	r0, [ip, #4]
	add	r4, r4, #1
	uxth	r4, r4
	ubfx	r0, r0, #10, #16
	bl	FtlBbmMapBadBlock
.L1834:
	add	r6, r6, #1
	b	.L1833
.L1836:
	ldr	r2, .L1870+16
	ldrh	r3, [r2]
	str	r3, [sp, #8]
	ldrb	r3, [r10, #-2740]	@ zero_extendqisi2
	cmp	r3, #0
	ldreq	r3, [sp, #8]
	movne	r8, #1
	moveq	r8, #1
	strne	r8, [sp, #12]
	lsreq	r3, r3, #2
	streq	r3, [sp, #12]
	b	.L1837
.L1841:
	uxth	r3, r10
	mov	r2, #36
	ldr	r0, [r5, #-1488]
	mul	r1, r2, r3
	mov	r2, #0
	add	r3, r5, r3
	str	r2, [r0, r1]
	ldr	r1, [sp, #4]
	ldrb	r0, [r3, #-1706]	@ zero_extendqisi2
	bl	V2P_block
	cmp	r7, #0
	mov	r9, r0
	beq	.L1839
	bl	IsBlkInVendorPart
	cmp	r0, #0
	bne	.L1840
.L1839:
	mov	r0, r9
	bl	FtlBbmIsBadBlock
	cmp	r0, #0
	bne	.L1840
	ldr	r1, [r5, #-1488]
	mov	r3, #36
	add	r9, fp, r9, lsl #10
	mla	r1, r3, r6, r1
	ldr	r3, [r5, #-1452]
	str	r3, [r1, #8]
	ldr	r3, .L1870+20
	str	r9, [r1, #4]
	ldrh	r3, [r3, #-6]
	mul	r3, r6, r3
	add	r6, r6, #1
	uxth	r6, r6
	add	r0, r3, #3
	cmp	r3, #0
	movlt	r3, r0
	ldr	r0, [r5, #-1448]
	bic	r3, r3, #3
	add	r3, r0, r3
	str	r3, [r1, #12]
.L1840:
	add	r10, r10, #1
	b	.L1838
.L1845:
	mul	r3, r2, r9
	ldr	r1, [r5, #-1488]
	add	ip, r1, r3
	ldr	r3, [r1, r3]
	cmp	r3, #0
	beq	.L1844
	ldr	r0, [ip, #4]
	add	r4, r4, #1
	str	r2, [sp, #20]
	uxth	r4, r4
	ubfx	r0, r0, #10, #16
	bl	FtlBbmMapBadBlock
	ldr	r2, [sp, #20]
.L1844:
	add	r9, r9, #1
	b	.L1843
.L1849:
	cmp	r7, #0
	beq	.L1848
	mul	r3, fp, r9
	ldr	r2, [r5, #-1488]
	add	r1, r2, r3
	ldr	r3, [r2, r3]
	cmp	r3, #0
	bne	.L1848
	ldr	r0, [r1, #4]
	mov	r1, #1
	ubfx	r0, r0, #10, #16
	bl	FtlFreeSysBlkQueueIn
.L1848:
	add	r9, r9, #1
	b	.L1847
.L1871:
	.align	2
.L1870:
	.word	.LANCHOR2
	.word	.LANCHOR2-1732
	.word	.LANCHOR2-1728
	.word	.LANCHOR2-1654
	.word	.LANCHOR2-1664
	.word	.LANCHOR2-1648
	.fnend
	.size	FtlLowFormatEraseBlock, .-FtlLowFormatEraseBlock
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
	ldr	r3, .L1885
	ldrh	r2, [r3, #6]
	cmp	r2, #0
	bxeq	lr
	push	{r4, r5, lr}
	.save {r4, r5, lr}
	add	lr, r3, #8
	add	r2, lr, #2048
	ldrh	ip, [r3, #2]
	mov	r4, #0
	ldrh	r5, [r2, #28]
	mov	r0, r4
	ldrh	r2, [r3, #4]
	and	r5, r5, #31
.L1874:
	uxth	r1, r4
	add	r4, r4, #1
	cmp	r5, r1
	bgt	.L1875
	cmp	r0, #0
	strhne	ip, [lr, #-6]	@ movhi
	strhne	r2, [lr, #-4]	@ movhi
	pop	{r4, r5, pc}
.L1875:
	add	r1, r3, ip, lsl #1
	add	ip, ip, #1
	ubfx	ip, ip, #0, #10
	ldrh	r0, [r1, #8]
	add	r1, r3, r2, lsl #1
	strh	r0, [r1, #8]	@ movhi
	mov	r0, #1
	add	r2, r2, r0
	ubfx	r2, r2, #0, #10
	b	.L1874
.L1886:
	.align	2
.L1885:
	.word	.LANCHOR2-1240
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
	ldr	r4, .L1898
	sub	r6, r4, #1232
	sub	r7, r6, #8
.L1888:
	ldrh	r1, [r6, #-2]
	sub	r2, r6, #8
	cmp	r1, #0
	beq	.L1889
	ldrh	r3, [r6, #-6]
	sub	r1, r1, #1
	ldr	r9, [r4, #-1280]
	strh	r1, [r6, #-2]	@ movhi
	add	r0, r2, r3, lsl #1
	cmp	r9, #0
	add	r3, r3, #1
	ubfx	r3, r3, #0, #10
	ldrh	r5, [r0, #8]
	strh	r3, [r6, #-6]	@ movhi
	bne	.L1890
	mov	r0, r5
	bl	P2V_block_in_plane
	mov	r8, r0
	ldr	r0, [r4, #-1488]
	lsl	r3, r5, #10
	str	r3, [r0, #4]
	ldrb	r3, [r4, #-2740]	@ zero_extendqisi2
	cmp	r3, #0
	beq	.L1891
	mov	r2, #1
	mov	r1, r9
	bl	FlashEraseBlocks
.L1891:
	mov	r2, #1
	ldr	r0, [r4, #-1488]
	mov	r1, r2
	bl	FlashEraseBlocks
	ldr	r2, [r4, #-1412]
	lsl	r0, r8, #1
	ldrh	r3, [r2, r0]
	add	r3, r3, #1
	strh	r3, [r2, r0]	@ movhi
	ldr	r3, [r4, #-1572]
	add	r3, r3, #1
	str	r3, [r4, #-1572]
.L1890:
	sub	r3, r5, #1
	movw	r2, #65533
	uxth	r3, r3
	cmp	r3, r2
	bls	.L1893
	ldrh	r2, [r7, #6]
	mov	r1, r5
	ldr	r0, .L1898+4
	bl	printk
	b	.L1888
.L1889:
	ldr	r0, .L1898+8
	bl	printk
.L1892:
	b	.L1892
.L1893:
	mov	r0, r5
	pop	{r4, r5, r6, r7, r8, r9, r10, pc}
.L1899:
	.align	2
.L1898:
	.word	.LANCHOR2
	.word	.LC38
	.word	.LC37
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
	ldr	r3, .L1906
	str	lr, [sp, #-4]!
	.save {lr}
	movw	lr, #65535
	ldr	r2, [r0]
	ldr	ip, [r3, #-1356]
	sub	r3, r2, ip
	asr	r0, r3, #1
	ldr	r3, .L1906+4
	mul	r3, r3, r0
	mov	r0, #6
	uxth	r3, r3
.L1902:
	cmp	r3, r1
	beq	.L1903
	ldrh	r3, [r2]
	cmp	r3, lr
	beq	.L1904
	mla	r2, r0, r3, ip
	b	.L1902
.L1903:
	mov	r0, #1
	ldr	pc, [sp], #4
.L1904:
	mov	r0, #0
	ldr	pc, [sp], #4
.L1907:
	.align	2
.L1906:
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
	ldr	r2, .L1924
	sub	r4, r2, #1712
	ldrh	r3, [r4, #-12]
	cmp	r3, r0
	bls	.L1910
	mov	lr, #6
	ldr	r6, [r2, #-1356]
	mul	lr, lr, r0
	mvn	ip, #0
	add	r1, r6, lr
	strh	ip, [r1, #2]	@ movhi
	strh	ip, [r6, lr]	@ movhi
	ldr	r3, [r2, #864]
	cmp	r3, #0
	streq	r1, [r2, #864]
	beq	.L1910
	ldr	r8, [r2, #-1404]
	lsl	r10, r0, #1
	mov	r5, r2
	ldrh	r2, [r1, #4]
	ldrh	r4, [r4, #-12]
	ldrh	r7, [r8, r10]
	cmp	r2, #0
	str	r4, [sp]
	mulne	ip, r2, r7
	ldr	r7, [r5, #-1356]
	sub	r2, r3, r7
	asr	r9, r2, #1
	ldr	r2, .L1924+4
	mul	r2, r2, r9
	ldr	r9, [r5, #-1412]
	add	r4, r9, r10
	uxth	r2, r2
	str	r4, [sp, #4]
	mov	r4, #0
.L1919:
	ldr	r5, [sp]
	add	r4, r4, #1
	uxth	r4, r4
	cmp	r4, r5
	movls	r5, #0
	movhi	r5, #1
	cmp	r0, r2
	orreq	r5, r5, #1
	cmp	r5, #0
	bne	.L1910
	lsl	r10, r2, #1
	ldrh	r5, [r3, #4]
	ldrh	fp, [r8, r10]
	cmp	r5, #0
	mvneq	r5, #0
	mulne	r5, r5, fp
	cmp	ip, r5
	bne	.L1915
	ldr	r5, [sp, #4]
	ldrh	r10, [r9, r10]
	ldrh	r5, [r5]
	cmp	r10, r5
	bcc	.L1917
.L1916:
	strh	r2, [r6, lr]	@ movhi
	ldr	ip, .L1924
	ldrh	r2, [r3, #2]
	strh	r2, [r1, #2]	@ movhi
	ldr	r2, [ip, #864]
	cmp	r3, r2
	ldrhne	lr, [r3, #2]
	movne	r2, #6
	ldrne	r1, [ip, #-1356]
	strheq	r0, [r3, #2]	@ movhi
	streq	r1, [ip, #864]
	mulne	r2, r2, lr
	strhne	r0, [r1, r2]	@ movhi
	strhne	r0, [r3, #2]	@ movhi
	b	.L1910
.L1915:
	bcc	.L1916
.L1917:
	ldrh	r5, [r3]
	movw	r10, #65535
	cmp	r5, r10
	bne	.L1918
	strh	r2, [r1, #2]	@ movhi
	strh	r0, [r3]	@ movhi
	ldr	r3, .L1924
	str	r1, [r3, #868]
.L1910:
	mov	r0, #0
	add	sp, sp, #12
	@ sp needed
	pop	{r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L1918:
	mov	r3, #6
	mov	r2, r5
	mla	r3, r3, r5, r7
	b	.L1919
.L1925:
	.align	2
.L1924:
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
	ldr	r2, .L1928
	ldrh	r3, [r2]
	add	r3, r3, #1
	strh	r3, [r2]	@ movhi
	pop	{r4, pc}
.L1929:
	.align	2
.L1928:
	.word	.LANCHOR2+872
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
	beq	.L1931
	ldr	r2, .L1938
	mov	r1, #6
	mul	r5, r1, r0
	mvn	r3, #0
	ldr	r6, [r2, #-1356]
	mov	ip, r2
	add	lr, r6, r5
	strh	r3, [lr, #2]	@ movhi
	strh	r3, [r6, r5]	@ movhi
	ldr	r3, [r2, #876]
	cmp	r3, #0
	streq	lr, [r2, #876]
	beq	.L1931
	ldr	r8, [r2, #-1412]
	lsl	r2, r0, #1
	ldr	r7, [ip, #-1356]
	ldrh	r9, [r8, r2]
	sub	r2, r3, r7
	asr	r10, r2, #1
	ldr	r2, .L1938+4
	mul	r2, r2, r10
	mov	r10, r1
	uxth	r2, r2
.L1935:
	lsl	r1, r2, #1
	ldrh	r1, [r8, r1]
	cmp	r1, r9
	bcs	.L1933
	ldrh	r1, [r3]
	cmp	r1, r4
	bne	.L1934
	strh	r2, [lr, #2]	@ movhi
	strh	r0, [r3]	@ movhi
.L1931:
	mov	r0, #0
	pop	{r4, r5, r6, r7, r8, r9, r10, pc}
.L1934:
	mla	r3, r10, r1, r7
	mov	r2, r1
	b	.L1935
.L1933:
	ldrh	r1, [r3, #2]
	strh	r1, [lr, #2]	@ movhi
	strh	r2, [r6, r5]	@ movhi
	ldr	r2, [ip, #876]
	cmp	r3, r2
	ldrhne	lr, [r3, #2]
	movne	r2, #6
	ldrne	r1, [ip, #-1356]
	strheq	r0, [r3, #2]	@ movhi
	streq	lr, [ip, #876]
	mulne	r2, r2, lr
	strhne	r0, [r1, r2]	@ movhi
	strhne	r0, [r3, #2]	@ movhi
	b	.L1931
.L1939:
	.align	2
.L1938:
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
	ldr	r2, .L1942
	ldrh	r3, [r2]
	add	r3, r3, #1
	strh	r3, [r2]	@ movhi
	pop	{r4, pc}
.L1943:
	.align	2
.L1942:
	.word	.LANCHOR2+880
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
	ldr	r4, .L1950
	mul	r1, ip, r1
	movw	r5, #65535
	ldr	r3, [r0]
	ldr	r2, [r4, #-1356]
	add	lr, r2, r1
	cmp	lr, r3
	ldrh	r3, [r2, r1]
	bne	.L1945
	cmp	r3, r5
	mlane	r3, ip, r3, r2
	moveq	r3, #0
	streq	r3, [r0]
	strne	r3, [r0]
	mvnne	r0, #0
	strhne	r0, [r3, #2]	@ movhi
.L1947:
	mvn	r3, #0
	mov	r0, #0
	strh	r3, [r2, r1]	@ movhi
	strh	r3, [lr, #2]	@ movhi
	pop	{r4, r5, pc}
.L1945:
	cmp	r3, r5
	ldrh	r0, [lr, #2]
	bne	.L1948
	cmp	r0, r3
	mulne	r3, ip, r0
	mvnne	r0, #0
	strhne	r0, [r2, r3]	@ movhi
	b	.L1947
.L1948:
	mla	r3, ip, r3, r2
	strh	r0, [r3, #2]	@ movhi
	ldrh	r3, [lr, #2]
	ldrh	r5, [r2, r1]
	ldr	r0, [r4, #-1356]
	mul	r3, ip, r3
	strh	r5, [r0, r3]	@ movhi
	b	.L1947
.L1951:
	.align	2
.L1950:
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
	beq	.L1958
	ldr	r2, .L1963
	push	{r4, lr}
	.save {r4, lr}
	movw	lr, #65535
	mov	r4, #6
	ldr	r2, [r2, #-1356]
.L1954:
	cmp	r1, #0
	bne	.L1955
.L1957:
	ldr	r4, .L1963+4
	sub	r3, r3, r2
	asr	r3, r3, #1
	mul	r4, r4, r3
	uxth	r1, r4
	bl	List_remove_node
	uxth	r0, r4
	pop	{r4, pc}
.L1955:
	ldrh	ip, [r3]
	cmp	ip, lr
	beq	.L1957
	sub	r1, r1, #1
	mla	r3, r4, ip, r2
	uxth	r1, r1
	b	.L1954
.L1958:
	movw	r0, #65535
	bx	lr
.L1964:
	.align	2
.L1963:
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
	ldr	r2, .L1971
	ldr	r3, [r2, #864]
	cmp	r3, #0
	ldrne	r1, [r2, #-1356]
	movne	ip, #6
	movwne	r2, #65535
	bne	.L1967
.L1970:
	movw	r0, #65535
	bx	lr
.L1969:
	sub	r0, r0, #1
	mla	r3, ip, r3, r1
	uxth	r0, r0
.L1967:
	cmp	r0, #0
	beq	.L1968
	ldrh	r3, [r3]
	cmp	r3, r2
	bne	.L1969
	b	.L1970
.L1968:
	ldr	r0, .L1971+4
	sub	r3, r3, r1
	asr	r3, r3, #1
	mul	r3, r0, r3
	uxth	r0, r3
	bx	lr
.L1972:
	.align	2
.L1971:
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
	ldr	r3, .L1984
	add	r2, r3, #884
	ldrh	r2, [r2]
	cmp	r2, r0
	beq	.L1981
	add	r2, r3, #932
	ldrh	r2, [r2]
	cmp	r2, r0
	beq	.L1981
	add	r2, r3, #980
	ldrh	r2, [r2]
	cmp	r2, r0
	beq	.L1981
	push	{r4, lr}
	.save {r4, lr}
	mov	lr, #6
	mul	lr, lr, r0
	ldr	r1, [r3, #-1356]
	ldr	r2, [r3, #864]
	add	ip, r1, lr
	cmp	ip, r2
	beq	.L1974
	ldr	r4, [r3, #-1404]
	lsl	r3, r0, #1
	ldrh	r2, [ip, #4]
	ldrh	r3, [r4, r3]
	cmp	r2, #0
	mvneq	r2, #0
	mulne	r2, r2, r3
	ldrh	r3, [ip, #2]
	movw	ip, #65535
	cmp	r3, ip
	bne	.L1976
	ldrh	ip, [r1, lr]
	cmp	ip, r3
	beq	.L1974
.L1976:
	mov	ip, #6
	mul	ip, ip, r3
	ldr	r3, .L1984+4
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
	bcs	.L1974
	mov	r4, r0
	mov	r1, r0
	ldr	r0, .L1984+8
	bl	List_remove_node
	ldr	r2, .L1984+12
	mov	r0, r4
	ldrh	r3, [r2]
	sub	r3, r3, #1
	strh	r3, [r2]	@ movhi
	bl	INSERT_DATA_LIST
.L1974:
	mov	r0, #0
	pop	{r4, pc}
.L1981:
	mov	r0, #0
	bx	lr
.L1985:
	.align	2
.L1984:
	.word	.LANCHOR2
	.word	-1431655765
	.word	.LANCHOR2+864
	.word	.LANCHOR2+872
	.fnend
	.size	List_update_data_list, .-List_update_data_list
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
.L1987:
	ldrh	r1, [r4, #6]
	uxth	r3, r2
	cmp	r1, r3
	bhi	.L1991
	ldr	r3, .L2007
	mov	r6, #0
	mov	r8, r6
	mov	r10, r6
	ldrh	r2, [r3]
	ldrh	r3, [r4]
	lsl	r3, r3, #1
	strh	r2, [r5, r3]	@ movhi
	ldrh	r9, [r5]
.L1992:
	ldrh	r3, [r4, #10]
	uxth	r1, r6
	cmp	r3, r1
	bhi	.L1996
	mov	r0, r8
	pop	{r3, r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L1991:
	uxth	r3, r2
	mov	r1, #0
	ldr	r0, [r6, r3, lsl #2]
	ubfx	r0, r0, #10, #16
.L1988:
	ldrh	ip, [r4, #10]
	uxth	r3, r1
	cmp	ip, r3
	addls	r2, r2, #1
	bls	.L1987
.L1990:
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
	b	.L1988
.L1996:
	uxth	r3, r6
	lsl	r3, r3, #1
	ldrh	r2, [r5, r3]
	cmp	r9, r2
	bls	.L1993
	ldrh	r0, [r7, r3]
	add	fp, r7, r3
	cmp	r0, #0
	bne	.L1994
.L1995:
	add	r6, r6, #1
	b	.L1992
.L1993:
	cmp	r2, #0
	bne	.L1995
	ldrh	r0, [r7, r3]
	add	fp, r7, r3
	cmp	r0, #0
	beq	.L1995
.L1997:
	mov	r1, #1
	bl	FtlFreeSysBlkQueueIn
	strh	r10, [fp]	@ movhi
	ldrh	r3, [r4, #8]
	sub	r3, r3, #1
	strh	r3, [r4, #8]	@ movhi
	b	.L1995
.L1994:
	subs	r9, r2, #0
	mov	r8, r1
	beq	.L1997
	b	.L1995
.L2008:
	.align	2
.L2007:
	.word	.LANCHOR2-1664
	.fnend
	.size	ftl_free_no_use_map_blk, .-ftl_free_no_use_map_blk
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
.L2010:
	uxth	r5, r3
	cmp	r5, r1
	bcs	.L2013
	mov	r7, r2
	add	r3, r3, #1
	ldrh	r6, [r7]
	add	r2, r2, #2
	cmp	r6, #0
	bne	.L2010
	mov	r4, r0
	bl	FtlFreeSysBlkQueueOut
	sub	r3, r0, #1
	movw	r2, #65533
	uxth	r3, r3
	mov	r1, r0
	strh	r0, [r7]	@ movhi
	cmp	r3, r2
	bls	.L2011
	ldr	r3, .L2017
	ldr	r0, .L2017+4
	ldrh	r2, [r3, #6]
	bl	printk
.L2012:
	b	.L2012
.L2011:
	ldr	r3, [r4, #28]
	strh	r6, [r4, #2]	@ movhi
	strh	r5, [r4]	@ movhi
	add	r3, r3, #1
	str	r3, [r4, #28]
	ldrh	r3, [r4, #8]
	add	r3, r3, #1
	strh	r3, [r4, #8]	@ movhi
.L2013:
	mov	r0, #0
	pop	{r4, r5, r6, r7, r8, pc}
.L2018:
	.align	2
.L2017:
	.word	.LANCHOR2-1240
	.word	.LC39
	.fnend
	.size	ftl_map_blk_alloc_new_blk, .-ftl_map_blk_alloc_new_blk
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
	ldr	r5, .L2038
	mov	r8, r1
	mov	r10, r2
	mov	r6, #0
	sub	r9, r5, #1664
	mov	fp, r9
.L2020:
	ldr	r3, [r5, #-1588]
	add	r3, r3, #1
	str	r3, [r5, #-1588]
	ldrh	r3, [r9]
	ldrh	r2, [r4, #2]
	sub	r3, r3, #1
	cmp	r2, r3
	bge	.L2021
	ldrh	r2, [r4]
	movw	r3, #65535
	cmp	r2, r3
	bne	.L2022
.L2021:
	mov	r0, r4
	bl	Ftl_write_map_blk_to_last_page
.L2022:
	ldr	r1, [r5, #-1280]
	cmp	r1, #0
	bne	.L2023
	ldrh	r3, [r4]
	ldr	r2, [r4, #12]
	ldr	r0, [r5, #-1440]
	lsl	r3, r3, #1
	ldrh	r7, [r2, r3]
	mov	r2, #16
	ldrh	r3, [r4, #2]
	str	r10, [r5, #-1268]
	str	r0, [r5, #-1264]
	orr	r3, r3, r7, lsl #10
	str	r3, [r5, #-1272]
	bl	ftl_memset
	ldr	r3, [r5, #-1264]
	ldr	r2, [r4, #28]
	strh	r8, [r3, #8]	@ movhi
	str	r2, [r3, #4]
	ldrh	r2, [r4, #4]
	str	r3, [sp, #4]
	strh	r7, [r3, #2]	@ movhi
	strh	r2, [r3]	@ movhi
	ldr	r2, .L2038+4
	ldrb	r2, [r2, #36]	@ zero_extendqisi2
	cmp	r2, #0
	beq	.L2024
	ldr	r2, .L2038+8
	ldr	r0, [r5, #-1268]
	ldrh	r1, [r2]
	bl	js_hash
	ldr	r3, [sp, #4]
	str	r0, [r3, #12]
.L2024:
	mov	r3, #1
	ldr	r0, .L2038+12
	mov	r2, r3
	mov	r1, r3
	bl	FlashProgPages
	ldrh	r3, [r4, #2]
	add	r3, r3, #1
	uxth	r3, r3
	strh	r3, [r4, #2]	@ movhi
	ldr	r2, [r5, #-1276]
	cmn	r2, #1
	bne	.L2025
	ldr	r1, [r5, #-1272]
	add	r6, r6, #1
	ldr	r0, .L2038+16
	uxth	r6, r6
	bl	printk
	ldrh	r3, [r4, #2]
	cmp	r3, #2
	ldrhls	r3, [fp]
	subls	r3, r3, #1
	strhls	r3, [r4, #2]	@ movhi
	cmp	r6, #3
	bls	.L2020
	mov	r2, r6
	ldr	r1, [r5, #-1272]
	ldr	r0, .L2038+20
	bl	printk
	mov	r3, #1
	str	r3, [r5, #-1280]
.L2023:
	mov	r0, #0
	add	sp, sp, #12
	@ sp needed
	pop	{r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L2025:
	cmp	r2, #0
	strhne	r7, [r4, #40]	@ movhi
	cmp	r3, #1
	cmpne	r2, #256
	beq	.L2029
	ldr	r3, [r4, #36]
	cmp	r3, #0
	beq	.L2030
.L2029:
	mov	r3, #0
	str	r3, [r4, #36]
	b	.L2020
.L2030:
	ldr	r2, [r5, #-1272]
	ldr	r3, [r4, #24]
	str	r2, [r3, r8, lsl #2]
	b	.L2023
.L2039:
	.align	2
.L2038:
	.word	.LANCHOR2
	.word	.LANCHOR0
	.word	.LANCHOR2-1656
	.word	.LANCHOR2-1276
	.word	.LC40
	.word	.LC41
	.fnend
	.size	FtlMapWritePage, .-FtlMapWritePage
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
	sub	r3, r3, #4
	cmp	r2, r3
	blt	.L2041
	uxth	r0, r0
	lsl	r0, r0, #1
	ldrh	r9, [r5, r0]
	cmp	r9, #0
	beq	.L2041
	ldr	r3, [r4, #32]
	cmp	r3, #0
	bne	.L2041
	mov	r2, #1
	str	r2, [r4, #32]
	strh	r3, [r5, r0]	@ movhi
	ldrh	r3, [r4, #8]
	ldrh	r2, [r4, #2]
	sub	r3, r3, #1
	strh	r3, [r4, #8]	@ movhi
	ldr	r3, .L2053
	ldrh	r3, [r3]
	cmp	r2, r3
	bcc	.L2042
	mov	r0, r4
	bl	ftl_map_blk_alloc_new_blk
.L2042:
	ldr	r5, .L2053+4
	mov	r6, #0
	sub	fp, r5, #1264
	sub	fp, fp, #12
.L2043:
	ldrh	r2, [r4, #6]
	uxth	r3, r6
	cmp	r2, r3
	bhi	.L2048
	mov	r1, #1
	mov	r0, r9
	bl	FtlFreeSysBlkQueueIn
	mov	r3, #0
	str	r3, [r4, #32]
.L2041:
	ldr	r3, .L2053
	ldrh	r2, [r4, #2]
	ldrh	r3, [r3]
	cmp	r2, r3
	bcc	.L2046
	mov	r0, r4
	bl	ftl_map_blk_alloc_new_blk
	b	.L2046
.L2048:
	uxth	r7, r6
	add	r2, r10, r7, lsl #2
	str	r2, [sp]
	ldr	r2, [r10, r7, lsl #2]
	cmp	r9, r2, lsr #10
	bne	.L2044
	ldr	r2, [r5, #-1468]
	mov	r0, fp
	ldr	r8, [r5, #-1440]
	str	r3, [sp, #4]
	str	r2, [r5, #-1268]
	str	r8, [r5, #-1264]
	ldr	r2, [r10, r7, lsl #2]
	str	r2, [r5, #-1272]
	mov	r2, #1
	mov	r1, r2
	bl	FlashReadPages
	ldr	r2, [r5, #-1276]
	ldr	r3, [sp, #4]
	cmn	r2, #1
	bne	.L2045
.L2047:
	ldr	r2, [sp]
	mov	r3, #0
	ldr	r0, .L2053+8
	str	r3, [r2]
	ldrh	r2, [r8, #8]
	ldr	r1, [r5, #-1272]
	bl	printk
	mov	r3, #1
	str	r3, [r5, #-1280]
.L2046:
	mov	r0, #0
	add	sp, sp, #12
	@ sp needed
	pop	{r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L2045:
	ldrh	r2, [r8, #8]
	cmp	r2, r3
	bne	.L2047
	ldrh	r2, [r8]
	ldrh	r3, [r4, #4]
	cmp	r2, r3
	bne	.L2047
	ldr	r2, [r5, #-1268]
	mov	r1, r7
	mov	r0, r4
	bl	FtlMapWritePage
.L2044:
	add	r6, r6, #1
	b	.L2043
.L2054:
	.align	2
.L2053:
	.word	.LANCHOR2-1664
	.word	.LANCHOR2
	.word	.LC42
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
	ldr	r5, .L2066
	ldr	r6, [r5, #-1280]
	cmp	r6, #0
	bne	.L2056
	ldrh	r3, [r0]
	movw	r2, #65535
	mov	r4, r0
	ldr	r7, [r0, #12]
	cmp	r3, r2
	bne	.L2057
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
.L2056:
	mov	r0, #0
	pop	{r4, r5, r6, r7, r8, r9, r10, pc}
.L2057:
	lsl	r3, r3, #1
	ldr	r8, [r0, #24]
	mov	r1, #255
	ldrh	r9, [r7, r3]
	ldrh	r3, [r0, #2]
	ldr	r7, [r5, #-1440]
	orr	r3, r3, r9, lsl #10
	str	r7, [r5, #-1264]
	str	r3, [r5, #-1272]
	ldr	r3, [r5, #-1472]
	str	r3, [r5, #-1268]
	ldr	r3, [r0, #28]
	str	r3, [r7, #4]
	ldr	r3, .L2066+4
	strh	r3, [r7, #8]	@ movhi
	ldrh	r3, [r0, #4]
	strh	r9, [r7, #2]	@ movhi
	strh	r3, [r7]	@ movhi
	sub	r3, r5, #1664
	ldrh	r2, [r3]
	ldr	r0, [r5, #-1472]
	lsl	r2, r2, #3
	bl	ftl_memset
	mov	r2, r6
	mov	r3, r6
.L2058:
	ldrh	r0, [r4, #6]
	uxth	r1, r2
	cmp	r0, r1
	bhi	.L2060
	ldr	r3, .L2066+8
	ldrb	r3, [r3, #36]	@ zero_extendqisi2
	cmp	r3, #0
	beq	.L2061
	ldr	r3, .L2066+12
	ldr	r0, [r5, #-1268]
	ldrh	r1, [r3, #-8]
	bl	js_hash
	str	r0, [r7, #12]
.L2061:
	mov	r2, #1
	mov	r3, #0
	mov	r1, r2
	ldr	r0, .L2066+16
	bl	FlashProgPages
	ldrh	r3, [r4, #2]
	mov	r0, r4
	add	r3, r3, #1
	strh	r3, [r4, #2]	@ movhi
	bl	ftl_map_blk_gc
	b	.L2056
.L2060:
	uxth	r1, r2
	ldr	r0, [r8, r1, lsl #2]
	cmp	r9, r0, lsr #10
	bne	.L2059
	ldr	r0, [r5, #-1472]
	add	r3, r3, #1
	uxth	r3, r3
	str	r1, [r0, r3, lsl #3]
	ldr	r0, [r8, r1, lsl #2]
	ldr	r1, [r5, #-1472]
	add	r1, r1, r3, lsl #3
	str	r0, [r1, #4]
.L2059:
	add	r2, r2, #1
	b	.L2058
.L2067:
	.align	2
.L2066:
	.word	.LANCHOR2
	.word	-1291
	.word	.LANCHOR0
	.word	.LANCHOR2-1648
	.word	.LANCHOR2-1276
	.fnend
	.size	Ftl_write_map_blk_to_last_page, .-Ftl_write_map_blk_to_last_page
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
	ldr	r5, .L2070
	mul	r4, r4, r0
	ldr	r3, [r5, #-1364]
	add	r0, r5, #1024
	add	r0, r0, #4
	add	r2, r3, r4
	ldrh	r1, [r3, r4]
	ldr	r2, [r2, #8]
	bl	FtlMapWritePage
	ldr	r3, [r5, #-1364]
	mov	r0, #0
	add	r4, r3, r4
	ldr	r3, [r4, #4]
	bic	r3, r3, #-2147483648
	str	r3, [r4, #4]
	pop	{r4, r5, r6, pc}
.L2071:
	.align	2
.L2070:
	.word	.LANCHOR2
	.fnend
	.size	flush_l2p_region, .-flush_l2p_region
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
	ldr	r3, .L2083
	mov	r1, #0
	push	{r4, r5, r6, lr}
	.save {r4, r5, r6, lr}
	mov	ip, #12
	movw	lr, #65535
	sub	r2, r3, #1616
	ldrh	r2, [r2, #-10]
	ldr	r3, [r3, #-1364]
.L2073:
	uxth	r0, r1
	cmp	r0, r2
	bcc	.L2075
	mov	r0, r2
	mov	r1, #0
	mov	ip, #-2147483648
	mov	r5, #12
.L2076:
	uxth	r4, r1
	cmp	r4, r2
	bcc	.L2078
	cmp	r0, r2
	popcc	{r4, r5, r6, pc}
	ldr	r1, .L2083+4
	mov	r0, r2
	mvn	ip, #0
	ldrh	r5, [r1]
	mov	r1, #0
.L2079:
	uxth	lr, r1
	cmp	lr, r2
	bcc	.L2081
	pop	{r4, r5, r6, pc}
.L2075:
	add	r1, r1, #1
	mla	r4, ip, r1, r3
	ldrh	r4, [r4, #-12]
	cmp	r4, lr
	bne	.L2073
	pop	{r4, r5, r6, pc}
.L2078:
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
	b	.L2076
.L2081:
	ldr	r4, [r3, #4]
	cmp	ip, r4
	bls	.L2080
	ldrh	r6, [r3]
	cmp	r6, r5
	movne	ip, r4
	movne	r0, lr
.L2080:
	add	r1, r1, #1
	add	r3, r3, #12
	b	.L2079
.L2084:
	.align	2
.L2083:
	.word	.LANCHOR2
	.word	.LANCHOR2+1072
	.fnend
	.size	select_l2p_ram_region, .-select_l2p_ram_region
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
	ldr	r4, .L2101
	ldr	r3, [r4, #-1284]
	cmp	r0, r3
	bcs	.L2086
	sub	fp, r4, #1648
	mov	r9, r0
	ldrh	r0, [fp, #-10]
	mov	r10, r1
	str	r2, [sp, #8]
	mov	r5, #12
	ldr	r2, [r4, #-1364]
	add	r3, r0, #7
	str	fp, [sp, #4]
	lsr	r6, r9, r3
	str	r3, [sp]
	sub	r3, r4, #1616
	uxth	r8, r6
	ldrh	r1, [r3, #-10]
	mov	r3, #0
.L2087:
	uxth	r7, r3
	cmp	r7, r1
	bcc	.L2092
	str	r2, [sp, #12]
	bl	select_l2p_ram_region
	mul	r5, r5, r0
	ldr	r2, [sp, #12]
	mov	r7, r0
	ldrh	r1, [r2, r5]
	add	r3, r2, r5
	movw	r2, #65535
	cmp	r1, r2
	beq	.L2093
	ldr	r3, [r3, #4]
	cmp	r3, #0
	bge	.L2093
	bl	flush_l2p_region
.L2093:
	ldr	r3, [r4, #-1376]
	uxth	r6, r6
	ldr	fp, [r3, r6, lsl #2]
	cmp	fp, #0
	bne	.L2094
	ldr	r0, [r4, #-1364]
	mov	r1, #255
	ldr	r3, [sp, #4]
	add	r0, r0, r5
	ldrh	r2, [r3, #-8]
	ldr	r0, [r0, #8]
	bl	ftl_memset
	ldr	r2, [r4, #-1364]
	strh	r8, [r2, r5]	@ movhi
	ldr	r2, [r4, #-1364]
	add	r5, r2, r5
	str	fp, [r5, #4]
	b	.L2089
.L2086:
	cmp	r2, #0
	mvn	r0, #0
	streq	r0, [r1]
.L2085:
	add	sp, sp, #20
	@ sp needed
	pop	{r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L2092:
	add	r3, r3, #1
	mla	r0, r5, r3, r2
	ldrh	r0, [r0, #-12]
	cmp	r0, r8
	bne	.L2087
.L2089:
	ldr	r3, [sp]
	mvn	r0, #0
	bic	r9, r9, r0, lsl r3
	ldr	r3, [sp, #8]
	uxth	r9, r9
	cmp	r3, #0
	mov	r3, #12
	bne	.L2090
	ldr	r2, [r4, #-1364]
	mla	r3, r3, r7, r2
	ldr	r3, [r3, #8]
	ldr	r3, [r3, r9, lsl #2]
	str	r3, [r10]
.L2091:
	ldr	r2, [r4, #-1364]
	mov	r3, #12
	mov	r0, #0
	mla	r7, r3, r7, r2
	ldr	r3, [r7, #4]
	cmn	r3, #1
	addne	r3, r3, #1
	strne	r3, [r7, #4]
	b	.L2085
.L2090:
	mul	r3, r3, r7
	ldr	r2, [r4, #-1364]
	ldr	r1, [r10]
	add	r2, r2, r3
	ldr	r2, [r2, #8]
	str	r1, [r2, r9, lsl #2]
	ldr	r2, [r4, #-1364]
	add	r3, r2, r3
	ldr	r2, [r3, #4]
	orr	r2, r2, #-2147483648
	str	r2, [r3, #4]
	ldr	r3, .L2101+4
	strh	r8, [r3]	@ movhi
	b	.L2091
.L2094:
	ldr	r2, [r4, #-1364]
	ldr	r0, .L2101+8
	str	fp, [r4, #-1272]
	add	r2, r2, r5
	ldr	r2, [r2, #8]
	str	r2, [r4, #-1268]
	ldr	r2, [r4, #-1440]
	str	r2, [r4, #-1264]
	mov	r2, #1
	mov	r1, r2
	bl	FlashReadPages
	ldr	r2, [r4, #-1264]
	ldrh	r2, [r2, #8]
	cmp	r2, r8
	beq	.L2095
	mov	r2, fp
	mov	r1, r6
	ldr	r0, .L2101+12
	bl	printk
	mov	r3, #4
	ldr	r1, [r4, #-1264]
	mov	r2, r3
	ldr	r0, .L2101+16
	bl	rknand_print_hex
	ldr	r3, .L2101+20
	mov	r2, #4
	ldr	r1, [r4, #-1376]
	ldr	r0, .L2101+24
	ldrh	r3, [r3, #-12]
	bl	rknand_print_hex
	mov	r3, #1
	str	r3, [r4, #-1280]
.L2096:
	ldr	r3, [r4, #-1364]
	mov	r1, #0
	add	r2, r3, r5
	str	r1, [r2, #4]
	strh	r8, [r3, r5]	@ movhi
	b	.L2089
.L2095:
	ldr	r2, [r4, #-1276]
	cmp	r2, #256
	bne	.L2096
	mov	r2, fp
	mov	r1, r6
	ldr	r0, .L2101+28
	bl	printk
	ldr	r3, [r4, #-1364]
	mov	r1, r6
	ldr	r0, .L2101+32
	add	r3, r3, r5
	ldr	r2, [r3, #8]
	bl	FtlMapWritePage
	b	.L2096
.L2102:
	.align	2
.L2101:
	.word	.LANCHOR2
	.word	.LANCHOR2+1072
	.word	.LANCHOR2-1276
	.word	.LC43
	.word	.LC18
	.word	.LANCHOR2-1616
	.word	.LC44
	.word	.LC45
	.word	.LANCHOR2+1028
	.fnend
	.size	log2phys, .-log2phys
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
	push	{r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.save {r4, r5, r6, r7, r8, r9, r10, fp, lr}
	mov	r10, r2
	ldr	r4, .L2113
	add	r2, r0, r1
	.pad #60
	sub	sp, sp, #60
	ldrh	r3, [r4, #-6]
	cmp	r2, r3
	mvnhi	r8, #0
	bhi	.L2103
	add	r4, r4, #1664
	mov	r9, r0
	sub	r3, r4, #1648
	mov	r6, r1
	ldrh	r7, [r3, #-10]
	mov	r8, #0
	str	r3, [sp, #4]
	lsr	r7, r0, r7
	lsl	fp, r7, #2
.L2105:
	cmp	r6, #0
	bne	.L2110
.L2103:
	mov	r0, r8
	add	sp, sp, #60
	@ sp needed
	pop	{r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L2110:
	ldr	r3, [r4, #-1380]
	mov	r0, r9
	ldr	r2, [r3, fp]
	ldr	r3, [sp, #4]
	str	r2, [sp, #12]
	ldrh	r3, [r3, #-12]
	mov	r1, r3
	str	r3, [sp, #8]
	bl	__aeabi_uidivmod
	ldr	r3, [sp, #8]
	ldr	r2, [sp, #12]
	str	r1, [sp]
	sub	r5, r3, r1
	uxth	r5, r5
	cmp	r6, r5
	uxthcc	r5, r6
	cmp	r2, #0
	cmpne	r5, r3
	movne	r1, #1
	moveq	r1, #0
	beq	.L2107
	ldr	r3, [r4, #-1464]
	add	r0, sp, #20
	str	r2, [sp, #24]
	mov	r2, #1
	mov	r1, r2
	str	r3, [sp, #28]
	mov	r3, #0
	str	r3, [sp, #32]
	bl	FlashReadPages
.L2108:
	lsl	r3, r5, #9
	ldr	r0, [r4, #-1464]
	mov	r1, r10
	mov	r2, r3
	str	r3, [sp, #8]
	ldr	r3, [sp]
	sub	r6, r6, r5
	add	r9, r9, r5
	add	fp, fp, #4
	add	r0, r0, r3, lsl #9
	bl	ftl_memcpy
	mov	r1, r7
	ldr	r2, [r4, #-1464]
	ldr	r0, .L2113+4
	add	r7, r7, #1
	bl	FtlMapWritePage
	ldr	r3, [sp, #8]
	cmn	r0, #1
	mvneq	r8, #0
	add	r10, r10, r3
	b	.L2105
.L2107:
	ldr	r3, [sp, #4]
	ldr	r0, [r4, #-1464]
	ldrh	r2, [r3, #-8]
	bl	ftl_memset
	b	.L2108
.L2114:
	.align	2
.L2113:
	.word	.LANCHOR2-1664
	.word	.LANCHOR2+1076
	.fnend
	.size	FtlVendorPartWrite, .-FtlVendorPartWrite
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
	push	{r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.save {r4, r5, r6, r7, r8, r9, r10, fp, lr}
	mov	r10, r2
	ldr	r5, .L2126
	add	r2, r0, r1
	.pad #60
	sub	sp, sp, #60
	ldrh	r3, [r5, #-6]
	cmp	r2, r3
	mvnhi	r8, #0
	bhi	.L2115
	add	r5, r5, #1664
	mov	r9, r0
	sub	r3, r5, #1648
	mov	r7, r1
	ldrh	r6, [r3, #-10]
	mov	r8, #0
	str	r3, [sp, #8]
	lsr	r6, r0, r6
	lsl	fp, r6, #2
.L2117:
	cmp	r7, #0
	bne	.L2123
.L2115:
	mov	r0, r8
	add	sp, sp, #60
	@ sp needed
	pop	{r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L2123:
	ldr	r3, [r5, #-1380]
	mov	r0, r9
	ldr	r3, [r3, fp]
	str	r3, [sp, #12]
	ldr	r3, [sp, #8]
	ldrh	r4, [r3, #-12]
	mov	r1, r4
	bl	__aeabi_uidivmod
	sub	r4, r4, r1
	ldr	r3, [sp, #12]
	uxth	r4, r4
	str	r1, [sp, #4]
	cmp	r7, r4
	uxthcc	r4, r7
	cmp	r3, #0
	lsl	r2, r4, #9
	str	r2, [sp, #12]
	beq	.L2119
	ldr	r2, [r5, #-1464]
	add	r0, sp, #20
	str	r3, [sp, #24]
	str	r3, [sp, #12]
	mov	r3, #0
	str	r2, [sp, #28]
	mov	r2, #1
	mov	r1, r2
	str	r3, [sp, #32]
	bl	FlashReadPages
	ldr	r2, [sp, #20]
	ldr	r3, [sp, #12]
	cmn	r2, #1
	ldr	r2, [r5, #-1276]
	mvneq	r8, #0
	cmp	r2, #256
	bne	.L2121
	mov	r2, r3
	mov	r1, r6
	ldr	r0, .L2126+4
	bl	printk
	ldr	r2, [r5, #-1464]
	mov	r1, r6
	ldr	r0, .L2126+8
	bl	FtlMapWritePage
.L2121:
	ldr	r1, [r5, #-1464]
	lsl	r2, r4, #9
	ldr	r3, [sp, #4]
	mov	r0, r10
	add	r1, r1, r3, lsl #9
	bl	ftl_memcpy
.L2122:
	add	r6, r6, #1
	sub	r7, r7, r4
	add	r9, r9, r4
	add	r10, r10, r4, lsl #9
	add	fp, fp, #4
	b	.L2117
.L2119:
	lsl	r2, r4, #9
	mov	r1, r3
	mov	r0, r10
	bl	ftl_memset
	b	.L2122
.L2127:
	.align	2
.L2126:
	.word	.LANCHOR2-1664
	.word	.LC46
	.word	.LANCHOR2+1076
	.fnend
	.size	FtlVendorPartRead, .-FtlVendorPartRead
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
	ldr	r3, .L2137
	add	r1, r3, #1120
	ldrh	r2, [r1]
	cmp	r2, #4
	cmpls	r0, #0
	bne	.L2129
	add	r2, r2, #1
	strh	r2, [r1]	@ movhi
	bx	lr
.L2129:
	mov	r2, #0
	str	lr, [sp, #-4]!
	.save {lr}
	strh	r2, [r1]	@ movhi
	sub	r1, r3, #1712
	movw	lr, #65535
	str	r2, [r3, #1124]
	ldrh	r1, [r1, #-12]
	ldr	r2, [r3, #-1404]
	add	r1, r2, r1, lsl #1
.L2130:
	cmp	r2, r1
	bne	.L2132
	ldr	pc, [sp], #4
.L2132:
	ldrh	ip, [r2], #2
	cmp	ip, lr
	ldrne	r0, [r3, #1124]
	addne	r0, r0, ip
	strne	r0, [r3, #1124]
	b	.L2130
.L2138:
	.align	2
.L2137:
	.word	.LANCHOR2
	.fnend
	.size	FtlUpdateVaildLpn, .-FtlUpdateVaildLpn
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
	ldr	r4, .L2149
	str	r2, [r0, #36]
	ldr	r2, [r4, #-1280]
	ldrh	r5, [r0, #6]
	ldr	r3, [r0, #24]
	cmp	r2, #0
	popne	{r4, r5, r6, pc}
	mov	r6, r0
	ldr	r2, [r4, #-1440]
	ldr	r0, [r4, #-1468]
	sub	r5, r5, #1
	uxth	r5, r5
	str	r2, [r4, #-1264]
	str	r0, [r4, #-1268]
	ldr	r3, [r3, r5, lsl #2]
	cmp	r3, #0
	str	r3, [r4, #-1272]
	beq	.L2143
	mov	r2, #1
	ldr	r0, .L2149+4
	mov	r1, r2
	bl	FlashReadPages
.L2144:
	ldr	r2, [r4, #-1268]
	mov	r1, r5
	mov	r0, r6
	pop	{r4, r5, r6, lr}
	b	FtlMapWritePage
.L2143:
	sub	r3, r4, #1648
	mov	r1, #255
	ldrh	r2, [r3, #-8]
	bl	ftl_memset
	b	.L2144
.L2150:
	.align	2
.L2149:
	.word	.LANCHOR2
	.word	.LANCHOR2-1276
	.fnend
	.size	FtlMapBlkWriteDumpData, .-FtlMapBlkWriteDumpData
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
	ldr	r4, .L2169
	ldr	r3, [r4, #-1280]
	cmp	r3, #0
	bne	.L2153
	ldr	r2, [r4, #-1472]
	add	r7, r4, #816
	ldr	r6, [r4, #-1440]
	sub	r9, r4, #1648
	ldr	r5, .L2169+4
	mov	r1, #255
	str	r2, [r4, #-1268]
	movw	r2, #1128
	ldrh	r2, [r4, r2]
	str	r6, [r4, #-1264]
	str	r3, [r6, #12]
	strh	r2, [r6, #2]	@ movhi
	ldr	r2, .L2169+8
	ldr	r8, .L2169+12
	strh	r2, [r6]	@ movhi
	ldr	r2, [r4, #1136]
	stmib	r6, {r2, r3}
	ldr	r3, .L2169+16
	str	r3, [r4, #816]
	ldr	r3, .L2169+20
	str	r3, [r4, #820]
	ldrh	r3, [r5, #6]
	strh	r3, [r7, #8]	@ movhi
	sub	r3, r4, #1696
	ldrh	r3, [r3, #-14]
	strb	r3, [r4, #826]
	add	r3, r4, #884
	ldrh	r2, [r3]
	strh	r2, [r7, #14]	@ movhi
	ldrh	r2, [r3, #2]
	ldrb	r3, [r4, #890]	@ zero_extendqisi2
	orr	r3, r3, r2, lsl #6
	strh	r3, [r7, #16]	@ movhi
	ldrb	r3, [r4, #892]	@ zero_extendqisi2
	strb	r3, [r4, #827]
	add	r3, r4, #932
	ldrh	r2, [r3]
	strh	r2, [r7, #18]	@ movhi
	ldrh	r2, [r3, #2]
	ldrb	r3, [r4, #938]	@ zero_extendqisi2
	orr	r3, r3, r2, lsl #6
	strh	r3, [r7, #20]	@ movhi
	ldrb	r3, [r4, #940]	@ zero_extendqisi2
	strb	r3, [r4, #828]
	add	r3, r4, #980
	ldrh	r2, [r3]
	strh	r2, [r7, #22]	@ movhi
	ldrh	r2, [r3, #2]
	ldrb	r3, [r4, #986]	@ zero_extendqisi2
	ldr	r0, [r4, #-1268]
	orr	r3, r3, r2, lsl #6
	ldrh	r2, [r9, #-8]
	strh	r3, [r7, #24]	@ movhi
	ldrb	r3, [r4, #988]	@ zero_extendqisi2
	strb	r3, [r4, #829]
	ldr	r3, [r4, #-1580]
	str	r3, [r4, #848]
	ldr	r3, [r4, #-1612]
	str	r3, [r4, #856]
	ldr	r3, [r4, #-1608]
	str	r3, [r4, #852]
	sub	r3, r4, #1536
	ldrh	r3, [r3]
	strh	r3, [r7, #44]	@ movhi
	sub	r3, r4, #1520
	ldrh	r3, [r3, #-14]
	strh	r3, [r7, #46]	@ movhi
	bl	ftl_memset
	mov	r1, r7
	mov	r2, #48
	sub	r7, r4, #1712
	ldr	r0, [r4, #-1268]
	bl	ftl_memcpy
	ldrh	r2, [r7, #-12]
	ldr	r0, [r4, #-1268]
	ldr	r1, [r4, #-1404]
	lsl	r2, r2, #1
	add	r0, r0, #48
	bl	ftl_memcpy
	ldrh	r0, [r7, #-12]
	ldr	r3, [r4, #-1268]
	ldr	r1, [r8, #32]
	lsr	r2, r0, #3
	lsl	r0, r0, #1
	add	r0, r0, #51
	add	r2, r2, #4
	bic	r0, r0, #3
	add	r0, r3, r0
	bl	ftl_memcpy
	sub	r3, r4, #1616
	str	r9, [sp, #4]
	ldrh	r2, [r3, #-8]
	cmp	r2, #0
	beq	.L2154
	ldrh	r0, [r7, #-12]
	ldrh	r2, [r3, #-12]
	ldr	r1, [r4, #-1376]
	lsr	r3, r0, #3
	lsl	r2, r2, #2
	add	r3, r3, r0, lsl #1
	ldr	r0, [r4, #-1268]
	add	r3, r3, #52
	ubfx	r3, r3, #2, #14
	add	r0, r0, r3, lsl #2
	bl	ftl_memcpy
.L2154:
	ldr	r10, .L2169+24
	mov	r7, #0
	movw	r9, #65535
	mov	r0, #0
	bl	FtlUpdateVaildLpn
	mov	fp, r10
.L2155:
	ldr	r3, [r4, #-1472]
	ldrh	r1, [r5, #2]
	ldrh	r2, [r5]
	str	r3, [r4, #-1268]
	ldr	r3, [r4, #-1440]
	str	r3, [r4, #-1264]
	orr	r3, r1, r2, lsl #10
	str	r3, [r4, #-1272]
	ldrh	r3, [r10]
	sub	r3, r3, #1
	cmp	r1, r3
	blt	.L2156
	mov	r3, #0
	ldrh	r9, [r5, #4]
	strh	r3, [r5, #2]	@ movhi
	strh	r2, [r5, #4]	@ movhi
	bl	FtlFreeSysBlkQueueOut
	ldr	r3, [r4, #-1612]
	strh	r0, [r5]	@ movhi
	add	r2, r3, #1
	str	r3, [r4, #1136]
	str	r2, [r4, #-1612]
	lsl	r2, r0, #10
	str	r2, [r4, #-1272]
	str	r3, [r6, #4]
	strh	r0, [r6, #2]	@ movhi
.L2156:
	ldrb	r3, [r8, #36]	@ zero_extendqisi2
	cmp	r3, #0
	beq	.L2157
	ldr	r3, [sp, #4]
	ldr	r0, [r4, #-1472]
	ldrh	r1, [r3, #-8]
	bl	js_hash
	str	r0, [r6, #12]
.L2157:
	mov	r3, #1
	ldr	r0, .L2169+28
	mov	r2, r3
	mov	r1, r3
	bl	FlashProgPages
	ldrh	r3, [r5, #2]
	ldr	r2, [r4, #-1276]
	add	r3, r3, #1
	uxth	r3, r3
	cmn	r2, #1
	strh	r3, [r5, #2]	@ movhi
	bne	.L2158
	cmp	r3, #1
	add	r7, r7, #1
	ldrheq	r3, [fp]
	uxth	r7, r7
	subeq	r3, r3, #1
	strheq	r3, [r5, #2]	@ movhi
	cmp	r7, #3
	bls	.L2155
	mov	r2, r7
	ldr	r1, [r4, #-1272]
	ldr	r0, .L2169+32
	bl	printk
	mov	r3, #1
	str	r3, [r4, #-1280]
.L2153:
	mov	r0, #0
	add	sp, sp, #12
	@ sp needed
	pop	{r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L2158:
	cmp	r3, #1
	cmpne	r2, #256
	beq	.L2155
	movw	r3, #65535
	cmp	r9, r3
	beq	.L2153
	mov	r1, #1
	mov	r0, r9
	bl	FtlFreeSysBlkQueueIn
	b	.L2153
.L2170:
	.align	2
.L2169:
	.word	.LANCHOR2
	.word	.LANCHOR2+1128
	.word	-3932
	.word	.LANCHOR0
	.word	1179929683
	.word	1342177379
	.word	.LANCHOR2-1664
	.word	.LANCHOR2-1276
	.word	.LC47
	.fnend
	.size	FtlVpcTblFlush, .-FtlVpcTblFlush
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
	mov	r5, #0
	ldr	r4, .L2250
	movw	r3, #1144
	mov	r1, r5
	.pad #28
	sub	sp, sp, #28
	ldr	r2, [r4, #-1636]
	sub	r7, r4, #1280
	ldr	r0, [r4, #-1368]
	sub	r6, r4, #1632
	strh	r5, [r4, r3]	@ movhi
	strh	r5, [r7, #-8]	@ movhi
	lsl	r2, r2, #2
	bl	ftl_memset
	ldr	r2, [r4, #-1636]
	mov	r1, r5
	ldr	r0, [r4, #-1400]
	lsl	r2, r2, #1
	bl	ftl_memset
	ldrh	r2, [r6, #-12]
	mov	r1, r5
	ldr	r0, [r4, #-1384]
	lsl	r2, r2, #2
	bl	ftl_memset
	ldrh	r2, [r6, #-12]
	mov	r1, r5
	ldr	r0, [r4, #-1392]
	lsl	r2, r2, #1
	bl	ftl_memset
	mov	r2, #16
	mov	r1, #255
	ldr	r0, .L2250+4
	bl	ftl_memset
	sub	r3, r4, #1712
	str	r7, [sp, #8]
	ldrh	r3, [r3, #-12]
	str	r6, [sp, #12]
	str	r3, [sp]
	sub	r3, r4, #1696
	sub	r3, r3, #10
	str	r3, [sp, #16]
.L2172:
	ldr	r2, .L2250+8
	ldr	r1, [sp]
	ldrh	r3, [r2]
	cmp	r3, r1
	bls	.L2212
	mov	r5, #0
	ldrh	r3, [r2, #-10]
	ldr	r6, [r4, #-1500]
	mov	r7, r5
	ldr	fp, [r4, #-1460]
	mov	r8, #36
	ldr	r10, [r4, #-1432]
	ldrh	r9, [r2, #68]
	b	.L2213
.L2174:
	str	r3, [sp, #20]
	ldr	r3, [sp, #16]
	ldr	r1, [sp]
	ldrb	r0, [r3, r5]	@ zero_extendqisi2
	bl	V2P_block
	str	r0, [sp, #4]
	bl	FtlBbmIsBadBlock
	cmp	r0, #0
	ldr	r3, [sp, #20]
	bne	.L2173
	ldr	r2, [sp, #4]
	mla	r1, r8, r7, r6
	lsl	r2, r2, #10
	stmib	r1, {r2, fp}
	mul	r2, r9, r7
	add	r7, r7, #1
	uxth	r7, r7
	add	r0, r2, #3
	cmp	r2, #0
	movlt	r2, r0
	bic	r2, r2, #3
	add	r2, r10, r2
	str	r2, [r1, #12]
.L2173:
	add	r5, r5, #1
.L2213:
	uxth	r2, r5
	cmp	r3, r2
	bhi	.L2174
	cmp	r7, #0
	bne	.L2175
.L2211:
	ldr	r3, [sp]
	add	r3, r3, #1
	uxth	r3, r3
	str	r3, [sp]
	b	.L2172
.L2175:
	ldr	r8, .L2250+4
	mov	r2, #1
	mov	r1, r7
	mov	r0, r6
	bl	FlashReadPages
	add	r9, r8, #16
	mov	r3, #0
	str	r3, [sp, #4]
.L2176:
	ldrh	r3, [sp, #4]
	cmp	r7, r3
	bls	.L2211
	ldr	r3, [sp, #4]
	mov	r10, #36
	mul	r10, r10, r3
	ldr	r3, [r4, #-1500]
	add	r2, r3, r10
	ldr	r3, [r3, r10]
	ldr	r5, [r2, #4]
	ldr	r6, [r2, #12]
	cmn	r3, #1
	ubfx	r5, r5, #10, #16
	bne	.L2179
	mov	fp, #16
	movw	r3, #65535
.L2181:
	ldr	r0, [r4, #-1500]
	str	r3, [sp, #20]
	add	r0, r0, r10
	ldr	r2, [r0, #4]
	add	r2, r2, #1
	str	r2, [r0, #4]
	mov	r2, #1
	mov	r1, r2
	bl	FlashReadPages
	ldrh	r2, [r6]
	ldr	r3, [sp, #20]
	cmp	r2, r3
	bne	.L2178
	ldr	r3, [r4, #-1500]
	mvn	r2, #0
	str	r2, [r3, r10]
	ldr	r3, [r4, #-1500]
	ldr	r3, [r3, r10]
	cmp	r3, r2
	beq	.L2180
.L2179:
	ldr	r2, [r4, #-1612]
	ldr	r3, [r6, #4]
	cmn	r2, #1
	beq	.L2182
	cmp	r2, r3
	bhi	.L2183
.L2182:
	cmn	r3, #1
	addne	r2, r3, #1
	strne	r2, [r4, #-1612]
.L2183:
	ldrh	r2, [r6]
	movw	r1, #61604
	cmp	r2, r1
	beq	.L2185
	bhi	.L2186
	movw	r3, #61574
	cmp	r2, r3
	beq	.L2187
.L2184:
	ldr	r3, [sp, #4]
	add	r3, r3, #1
	str	r3, [sp, #4]
	b	.L2176
.L2178:
	ldr	r2, [r4, #-1500]
	ldr	r2, [r2, r10]
	cmn	r2, #1
	bne	.L2179
	sub	fp, fp, #1
	uxth	fp, fp
	cmp	fp, #0
	bne	.L2181
.L2180:
	ldrb	r1, [r4, #-2740]	@ zero_extendqisi2
	cmp	r1, #0
	bne	.L2249
.L2209:
	mov	r0, r5
	bl	FtlFreeSysBlkQueueIn
	b	.L2184
.L2186:
	movw	r3, #61634
	cmp	r2, r3
	beq	.L2188
	movw	r3, #65535
	cmp	r2, r3
	bne	.L2184
.L2249:
	mov	r1, #0
	b	.L2209
.L2188:
	ldr	r0, [r4, #-1636]
	ldrh	r2, [r9]
	ldr	ip, [r4, #-1368]
	uxth	r1, r0
	sub	r3, r1, #1
	sub	r1, r1, r2
	sub	r1, r1, #1
	sxth	r3, r3
	sxth	r1, r1
.L2190:
	cmp	r3, r1
	bgt	.L2196
	cmp	r3, #0
	bge	.L2226
	b	.L2184
.L2196:
	ldr	fp, [r6, #4]
	lsl	lr, r3, #2
	ldr	r10, [ip, r3, lsl #2]
	cmp	fp, r10
	bls	.L2191
	ldr	r1, [ip]
	cmp	r1, #0
	bne	.L2192
	cmp	r0, r2
	addne	r2, r2, #1
	strhne	r2, [r9]	@ movhi
.L2192:
	uxth	ip, r3
	mov	r1, #0
.L2193:
	uxth	r0, r1
	sxth	r2, r1
	cmp	r0, ip
	bcc	.L2194
	ldr	r1, [r6, #4]
	cmp	r3, #0
	ldr	r2, [r4, #-1368]
	str	r1, [r2, lr]
	lsl	r2, r3, #1
	ldr	r1, [r4, #-1400]
	strh	r5, [r1, r2]	@ movhi
	blt	.L2184
	ldrh	r2, [r9]
	ldr	r1, [r4, #-1636]
	sub	r1, r1, r2
	sub	r1, r1, #1
	sxth	r1, r1
	cmp	r3, r1
	bgt	.L2184
.L2226:
	add	r2, r2, #1
	ldr	r1, [r6, #4]
	strh	r2, [r9]	@ movhi
	ldr	r2, [r4, #-1368]
	str	r1, [r2, r3, lsl #2]
	lsl	r3, r3, #1
	ldr	r2, [r4, #-1400]
.L2247:
	strh	r5, [r2, r3]	@ movhi
	b	.L2184
.L2194:
	ldr	r0, [r4, #-1368]
	add	r1, r1, #1
	add	r10, r0, r2, lsl #2
	ldr	r10, [r10, #4]
	str	r10, [r0, r2, lsl #2]
	lsl	r2, r2, #1
	ldr	r0, [r4, #-1400]
	add	r10, r0, r2
	ldrh	r10, [r10, #2]
	strh	r10, [r0, r2]	@ movhi
	b	.L2193
.L2191:
	sub	r3, r3, #1
	sxth	r3, r3
	b	.L2190
.L2187:
	ldr	r3, [sp, #12]
	ldr	r1, [sp, #8]
	ldr	ip, [r4, #-1384]
	ldrh	r2, [r3, #-12]
	ldrh	r1, [r1, #-8]
	sub	r0, r2, #1
	sxth	r3, r0
	sub	r0, r0, r1
.L2199:
	cmp	r3, r0
	ble	.L2204
	ldr	fp, [r6, #4]
	lsl	lr, r3, #2
	ldr	r10, [ip, r3, lsl #2]
	cmp	fp, r10
	bls	.L2200
	sub	r2, r2, r1
	ldr	r0, [ip]
	clz	r2, r2
	uxth	ip, r3
	lsr	r2, r2, #5
	cmp	r0, #0
	orrne	r2, r2, #1
	cmp	r2, #0
	ldreq	r2, .L2250+12
	addeq	r1, r1, #1
	strheq	r1, [r2]	@ movhi
	mov	r1, #0
.L2202:
	uxth	r0, r1
	sxth	r2, r1
	cmp	r0, ip
	bcc	.L2203
	ldr	r1, [r6, #4]
	ldr	r2, [r4, #-1384]
	str	r1, [r2, lr]
	lsl	r2, r3, #1
	ldr	r1, [r4, #-1392]
	strh	r5, [r1, r2]	@ movhi
.L2204:
	cmp	r3, #0
	blt	.L2184
	ldr	r2, [sp, #8]
	ldrh	r1, [r2, #-8]
	ldr	r2, .L2250+16
	ldrh	r2, [r2]
	sub	r2, r2, #1
	sub	r2, r2, r1
	sxth	r2, r2
	cmp	r3, r2
	bgt	.L2184
	ldr	r2, [sp, #8]
	add	r1, r1, #1
	strh	r1, [r2, #-8]	@ movhi
	ldr	r2, [r4, #-1384]
	ldr	r1, [r6, #4]
	str	r1, [r2, r3, lsl #2]
	lsl	r3, r3, #1
	ldr	r2, [r4, #-1392]
	b	.L2247
.L2203:
	ldr	r0, [r4, #-1384]
	add	r1, r1, #1
	add	r10, r0, r2, lsl #2
	ldr	r10, [r10, #4]
	str	r10, [r0, r2, lsl #2]
	lsl	r2, r2, #1
	ldr	r0, [r4, #-1392]
	add	r10, r0, r2
	ldrh	r10, [r10, #2]
	strh	r10, [r0, r2]	@ movhi
	b	.L2202
.L2200:
	sub	r3, r3, #1
	sxth	r3, r3
	b	.L2199
.L2185:
	ldrh	r1, [r8]
	movw	r2, #65535
	cmp	r1, r2
	strheq	r5, [r8]	@ movhi
	beq	.L2248
	ldrh	r0, [r8, #4]
	cmp	r0, r2
	beq	.L2207
	mov	r1, #1
	bl	FtlFreeSysBlkQueueIn
.L2207:
	ldr	r3, [r6, #4]
	ldr	r2, [r4, #1136]
	cmp	r2, r3
	strhcs	r5, [r8, #4]	@ movhi
	bcs	.L2184
	ldrh	r3, [r8]
	strh	r5, [r8]	@ movhi
	strh	r3, [r8, #4]	@ movhi
	ldr	r3, [r6, #4]
.L2248:
	str	r3, [r4, #1136]
	b	.L2184
.L2212:
	ldr	r1, [r4, #-1400]
	ldrh	r3, [r1]
	cmp	r3, #0
	beq	.L2214
.L2217:
	ldr	r1, [r4, #-1392]
	ldrh	r2, [r1]
	cmp	r2, #0
	beq	.L2215
.L2237:
	mov	r0, #0
	add	sp, sp, #28
	@ sp needed
	pop	{r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L2214:
	movw	r2, #1144
	ldrh	r2, [r4, r2]
	cmp	r2, #0
	ldrne	r0, [r4, #-1636]
	beq	.L2217
.L2218:
	sxth	r2, r3
	cmp	r2, r0
	bcs	.L2217
	lsl	ip, r2, #1
	add	r3, r3, #1
	ldrh	ip, [r1, ip]
	cmp	ip, #0
	beq	.L2218
	mov	r3, r2
	mov	lr, #0
.L2219:
	ldr	r1, [r4, #-1636]
	cmp	r3, r1
	bcs	.L2217
	ldr	r0, [r4, #-1400]
	lsl	r1, r3, #1
	sub	ip, r3, r2
	lsl	r5, ip, #1
	ldrh	r6, [r0, r1]
	strh	r6, [r0, r5]	@ movhi
	ldr	r0, [r4, #-1368]
	ldr	r5, [r0, r3, lsl #2]
	add	r3, r3, #1
	sxth	r3, r3
	str	r5, [r0, ip, lsl #2]
	ldr	r0, [r4, #-1400]
	strh	lr, [r0, r1]	@ movhi
	b	.L2219
.L2215:
	ldr	r3, .L2250+20
	ldrh	r0, [r3, #-8]
	cmp	r0, #0
	subne	r3, r3, #352
	ldrhne	r0, [r3, #-12]
	beq	.L2237
.L2222:
	sxth	r3, r2
	cmp	r3, r0
	mov	lr, r3
	bge	.L2237
	lsl	ip, r3, #1
	add	r2, r2, #1
	ldrh	ip, [r1, ip]
	cmp	ip, #0
	beq	.L2222
	mov	ip, #0
.L2223:
	ldr	r2, [sp, #12]
	ldrh	r2, [r2, #-12]
	cmp	r3, r2
	bge	.L2237
	ldr	r1, [r4, #-1392]
	lsl	r2, r3, #1
	sub	r0, r3, lr
	lsl	r5, r0, #1
	ldrh	r6, [r1, r2]
	strh	r6, [r1, r5]	@ movhi
	ldr	r1, [r4, #-1384]
	ldr	r5, [r1, r3, lsl #2]
	add	r3, r3, #1
	sxth	r3, r3
	str	r5, [r1, r0, lsl #2]
	ldr	r1, [r4, #-1392]
	strh	ip, [r1, r2]	@ movhi
	b	.L2223
.L2251:
	.align	2
.L2250:
	.word	.LANCHOR2
	.word	.LANCHOR2+1128
	.word	.LANCHOR2-1722
	.word	.LANCHOR2-1288
	.word	.LANCHOR2-1644
	.word	.LANCHOR2-1280
	.fnend
	.size	FtlScanSysBlk, .-FtlScanSysBlk
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
	ldr	r4, .L2255
	sub	r5, r4, #1424
	ldr	r2, [r4, #-1416]
	ldrh	r1, [r5]
	bl	FtlVendorPartRead
	ldr	r3, [r4, #-1416]
	ldr	r2, [r3]
	ldr	r3, .L2255+4
	cmp	r2, r3
	beq	.L2253
	ldr	r1, .L2255+8
	ldr	r0, .L2255+12
	bl	printk
	ldrh	r2, [r5]
	mov	r1, #0
	ldr	r0, [r4, #-1416]
	lsl	r2, r2, #9
	bl	ftl_memset
.L2253:
	mov	r0, #0
	pop	{r4, r5, r6, pc}
.L2256:
	.align	2
.L2255:
	.word	.LANCHOR2
	.word	1112818501
	.word	.LC48
	.word	.LC49
	.fnend
	.size	FtlLoadEctTbl, .-FtlLoadEctTbl
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
	beq	.L2258
	b	ftl_set_blk_mode.part.17
.L2258:
	ldr	r2, .L2259
	lsr	r0, r0, #5
	and	r3, r3, #31
	mov	ip, #1
	ldr	r1, [r2, #32]
	ldr	r2, [r1, r0, lsl #2]
	bic	r3, r2, ip, lsl r3
	str	r3, [r1, r0, lsl #2]
	bx	lr
.L2260:
	.align	2
.L2259:
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
	ldr	r3, .L2262
	lsr	r2, r0, #5
	and	r0, r0, #31
	ldr	r3, [r3, #32]
	ldr	r3, [r3, r2, lsl #2]
	lsr	r0, r3, r0
	and	r0, r0, #1
	bx	lr
.L2263:
	.align	2
.L2262:
	.word	.LANCHOR0
	.fnend
	.size	ftl_get_blk_mode, .-ftl_get_blk_mode
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
	ldr	r6, .L2285
	ldr	r7, .L2285+4
	ldr	r1, .L2285+8
	mov	r5, r6
	ldr	r0, .L2285+12
	bl	printk
	mov	r2, #8192
	mov	r1, #0
	ldr	r0, .L2285+4
	bl	memset
.L2265:
	ldr	r3, [r6, #-1284]
	cmp	r4, r3
	bcc	.L2267
	ldr	r8, .L2285+4
	mov	r4, #0
	ldr	r9, .L2285+16
	mov	r6, r4
.L2268:
	ldr	r3, .L2285+20
	ldrh	r2, [r3]
	uxth	r3, r4
	cmp	r2, r3
	bhi	.L2270
	ldr	r4, [r5, #876]
	cmp	r4, #0
	beq	.L2271
	ldr	r3, .L2285+24
	mov	r7, #0
	ldr	r9, .L2285+4
	mov	fp, #6
	ldr	r10, .L2285+28
	ldrh	r8, [r3]
	ldr	r3, [r5, #-1356]
	sub	r4, r4, r3
	ldr	r3, .L2285+32
	asr	r4, r4, #1
	mul	r4, r3, r4
	uxth	r4, r4
.L2272:
	uxth	r3, r7
	cmp	r8, r3
	bls	.L2271
	ldr	r2, [r5, #-1404]
	lsl	r3, r4, #1
	ldrh	r2, [r2, r3]
	cmp	r2, #0
	beq	.L2273
	mov	r6, #1
	ldrh	r3, [r9, r3]
	mov	r1, r4
	mov	r0, r10
	bl	printk
.L2273:
	mul	r4, fp, r4
	ldr	r3, [r5, #-1356]
	add	r7, r7, #1
	ldrh	r4, [r3, r4]
	movw	r3, #65535
	cmp	r4, r3
	bne	.L2272
.L2271:
	mov	r1, r6
	ldr	r0, .L2285+36
	bl	printk
	add	sp, sp, #12
	@ sp needed
	pop	{r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L2267:
	mov	r2, #0
	add	r1, sp, #4
	mov	r0, r4
	bl	log2phys
	ldr	r0, [sp, #4]
	cmn	r0, #1
	beq	.L2266
	ubfx	r0, r0, #10, #16
	bl	P2V_block_in_plane
	lsl	r0, r0, #1
	ldrh	r3, [r7, r0]
	add	r3, r3, #1
	strh	r3, [r7, r0]	@ movhi
.L2266:
	add	r4, r4, #1
	b	.L2265
.L2270:
	uxth	r1, r4
	ldr	r3, [r5, #-1404]
	lsl	r7, r1, #1
	ldrh	r2, [r3, r7]
	ldrh	r3, [r8, r7]
	cmp	r2, r3
	beq	.L2269
	mov	r0, r9
	bl	printk
	ldr	r3, [r5, #-1404]
	movw	r2, #65535
	ldrh	r3, [r3, r7]
	cmp	r3, r2
	beq	.L2269
	ldrh	r2, [r8, r7]
	cmp	r2, r3
	movhi	r6, #1
.L2269:
	add	r4, r4, #1
	b	.L2268
.L2286:
	.align	2
.L2285:
	.word	.LANCHOR2
	.word	check_valid_page_count_table
	.word	.LANCHOR3+141
	.word	.LC50
	.word	.LC51
	.word	.LANCHOR2-1724
	.word	.LANCHOR2+880
	.word	.LC52
	.word	-1431655765
	.word	.LC53
	.fnend
	.size	FtlCheckVpc, .-FtlCheckVpc
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
	push	{r4, r5, r6, r7, r8, r9, lr}
	.save {r4, r5, r6, r7, r8, r9, lr}
	lsl	r8, r0, #10
	ldr	r4, .L2295
	.pad #28
	sub	sp, sp, #28
	mov	r7, r0
	mov	r5, #0
	ldr	r3, [r4, #-1472]
	sub	r6, r4, #1264
	sub	r9, r4, #1664
	sub	r6, r6, #12
	str	r3, [r4, #-1268]
	ldr	r3, [r4, #-1440]
	str	r3, [r4, #-1264]
.L2288:
	ldrh	r2, [r9]
	sxth	r3, r5
	cmp	r3, r2
	blt	.L2290
	add	sp, sp, #28
	@ sp needed
	pop	{r4, r5, r6, r7, r8, r9, pc}
.L2290:
	mov	r2, #1
	orr	r3, r3, r8
	mov	r1, r2
	mov	r0, r6
	str	r3, [r4, #-1272]
	bl	FlashReadPages
	ldr	r2, [r4, #-1268]
	mov	r1, r7
	ldr	r3, [r4, #-1264]
	ldr	r0, .L2295+4
	ldr	r2, [r2]
	str	r2, [sp, #16]
	ldr	r2, [r3, #12]
	str	r2, [sp, #12]
	ldr	r2, [r3, #8]
	str	r2, [sp, #8]
	ldr	r2, [r3, #4]
	str	r2, [sp, #4]
	ldr	r3, [r3]
	ldr	r2, [r4, #-1276]
	str	r3, [sp]
	ldr	r3, [r4, #-1272]
	bl	printk
	ldr	r3, [r4, #-1264]
	ldr	r3, [r3]
	cmn	r3, #1
	beq	.L2289
	mov	r3, #768
	mov	r2, #4
	ldr	r1, [r4, #-1472]
	ldr	r0, .L2295+8
	bl	rknand_print_hex
.L2289:
	add	r5, r5, #1
	b	.L2288
.L2296:
	.align	2
.L2295:
	.word	.LANCHOR2
	.word	.LC54
	.word	.LC55
	.fnend
	.size	FtlDumpSysBlock, .-FtlDumpSysBlock
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
	ldr	r6, .L2306
	.pad #32
	sub	sp, sp, #32
	mov	r1, #0
	ldr	r8, .L2306+4
	mov	r4, r6
	ldr	r0, .L2306+8
	bl	printk
.L2298:
	ldr	r3, [r6, #-1284]
	cmp	r5, r3
	bcc	.L2304
	add	sp, sp, #32
	@ sp needed
	pop	{r4, r5, r6, r7, r8, pc}
.L2304:
	mov	r2, #0
	add	r1, sp, #28
	mov	r0, r5
	bl	log2phys
	ubfx	r3, r5, #0, #11
	cmp	r3, #0
	bne	.L2299
	ldr	r2, [sp, #28]
	mov	r1, r5
	mov	r0, r8
	bl	printk
.L2299:
	ldr	r3, [sp, #28]
	cmn	r3, #1
	beq	.L2301
	str	r3, [r4, #-1272]
	mov	r2, #0
	ldr	r3, [r4, #-1472]
	mov	r1, #1
	ldr	r7, [r4, #-1440]
	ldr	r0, .L2306+12
	str	r3, [r4, #-1268]
	str	r5, [r4, #-1260]
	str	r7, [r4, #-1264]
	str	r2, [r4, #-1276]
	bl	FlashReadPages
	ldr	r3, [r4, #-1276]
	cmn	r3, #1
	cmpne	r3, #256
	beq	.L2302
	ldr	r3, [r7, #8]
	cmp	r5, r3
	beq	.L2301
.L2302:
	ldr	r2, [r4, #-1268]
	ldr	r3, [r4, #-1264]
	ldr	r0, .L2306+16
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
	ldr	r3, [r3]
	ldr	r2, [r4, #-1272]
	bl	printk
.L2301:
	add	r5, r5, #1
	b	.L2298
.L2307:
	.align	2
.L2306:
	.word	.LANCHOR2
	.word	.LC57
	.word	.LC56
	.word	.LANCHOR2-1276
	.word	.LC58
	.fnend
	.size	Ftlscanalldata, .-Ftlscanalldata
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
	push	{r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.save {r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.pad #44
	sub	sp, sp, #44
	ldr	r8, .L2323
	ldrh	r6, [r8, #-12]
	add	fp, r8, #6
.L2309:
	ldrh	r3, [r8, #-10]
	ldr	r1, .L2323+4
	cmp	r3, r6
	mov	r4, r1
	bhi	.L2316
	sub	r8, r1, #1264
	mov	r7, #0
	sub	r8, r8, #12
.L2317:
	ldr	r3, .L2323+8
	sxth	r5, r7
	ldrh	r3, [r3]
	cmp	r5, r3
	bge	.L2320
	lsl	r5, r5, #1
	mov	r6, #0
	ldr	r9, .L2323+12
	b	.L2321
.L2311:
	mov	r1, r6
	ldrb	r0, [fp, r7]	@ zero_extendqisi2
	str	r3, [sp, #36]
	str	r2, [sp, #32]
	bl	V2P_block
	str	r0, [sp, #28]
	bl	FtlBbmIsBadBlock
	cmp	r0, #0
	ldr	r2, [sp, #32]
	ldr	r3, [sp, #36]
	bne	.L2310
	mov	r1, #36
	mla	r0, r1, r5, r9
	ldr	r1, [sp, #28]
	lsl	r1, r1, #10
	stmib	r0, {r1, r3}
	ldr	r1, [sp, #24]
	mul	r1, r1, r5
	add	r5, r5, #1
	uxth	r5, r5
	add	ip, r1, #3
	cmp	r1, #0
	movlt	r1, ip
	bic	r1, r1, #3
	add	r1, r10, r1
	str	r1, [r0, #12]
.L2310:
	add	r7, r7, #1
.L2318:
	uxth	r1, r7
	cmp	r2, r1
	bhi	.L2311
	cmp	r5, #0
	bne	.L2312
.L2315:
	add	r6, r6, #1
	uxth	r6, r6
	b	.L2309
.L2312:
	ldr	r10, .L2323+16
	mov	r0, r9
	mov	r7, #0
	mov	r9, #36
	mov	r2, #1
	mov	r1, r5
	bl	FlashReadPages
.L2313:
	uxth	r3, r7
	cmp	r5, r3
	bls	.L2315
	ldr	r3, [r4, #-1500]
	mla	r3, r9, r7, r3
	add	r7, r7, #1
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
	b	.L2313
.L2316:
	sub	r0, r1, #1728
	ldr	r9, [r1, #-1500]
	ldr	r3, [r1, #-1460]
	mov	r7, #0
	ldr	r10, [r1, #-1432]
	mov	r5, r7
	ldrh	r1, [r0, #74]
	ldrh	r2, [r0, #-4]
	str	r1, [sp, #24]
	b	.L2318
.L2319:
	ldr	r2, [r4, #-1400]
	mov	r0, r8
	ldrh	r2, [r2, r5]
	orr	r3, r3, r2, lsl #10
	mov	r2, #1
	mov	r1, r2
	str	r3, [r4, #-1272]
	bl	FlashReadPages
	ldr	r2, [r4, #-1268]
	ldr	r1, [r4, #-1400]
	ldr	r3, [r4, #-1264]
	ldr	r0, [r2, #4]
	ldrh	r1, [r1, r5]
	str	r0, [sp, #20]
	ldr	r2, [r2]
	ldr	r0, .L2323+20
	str	r2, [sp, #16]
	ldr	r2, [r3, #12]
	str	r2, [sp, #12]
	ldr	r2, [r3, #8]
	str	r2, [sp, #8]
	ldr	r2, [r3, #4]
	str	r2, [sp, #4]
	ldr	r3, [r3]
	str	r3, [sp]
	ldr	r3, [r4, #-1272]
	ldr	r2, [r4, #-1276]
	bl	printk
.L2321:
	ldrh	r2, [r9]
	sxth	r3, r6
	add	r6, r6, #1
	cmp	r3, r2
	blt	.L2319
	add	r7, r7, #1
	b	.L2317
.L2320:
	ldr	r5, .L2323+24
	mov	r2, #2
	ldr	r3, [r4, #-1636]
	ldr	r1, [r4, #-1400]
	ldr	r0, .L2323+28
	bl	rknand_print_hex
	ldrh	r3, [r5, #-12]
	mov	r2, #4
	ldr	r1, [r4, #-1376]
	ldr	r0, .L2323+32
	bl	rknand_print_hex
	ldrh	r3, [r5, #-12]
	mov	r2, #4
	ldr	r1, [r4, #-1372]
	ldr	r0, .L2323+36
	add	sp, sp, #44
	@ sp needed
	pop	{r4, r5, r6, r7, r8, r9, r10, fp, lr}
	b	rknand_print_hex
.L2324:
	.align	2
.L2323:
	.word	.LANCHOR2-1712
	.word	.LANCHOR2
	.word	.LANCHOR2+1144
	.word	.LANCHOR2-1664
	.word	.LC59
	.word	.LC60
	.word	.LANCHOR2-1616
	.word	.LC61
	.word	.LC62
	.word	.LC63
	.fnend
	.size	dump_map_info, .-dump_map_info
	.align	2
	.global	FtlMapTblRecovery
	.syntax unified
	.arm
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
	mov	r1, #0
	mov	r6, #0
	ldrh	fp, [r0, #6]
	str	r3, [sp, #4]
	ldr	r3, [r0, #16]
	ldr	r5, .L2367
	ldr	r10, [r0, #12]
	lsl	r2, fp, #2
	str	r3, [sp, #12]
	ldrh	r3, [r0, #8]
	mov	r8, r5
	ldr	r0, [sp, #4]
	str	r3, [sp, #8]
	bl	ftl_memset
	ldr	r3, [r5, #-1472]
	ldr	r7, [r5, #-1440]
	str	r6, [r4, #32]
	str	r3, [r5, #-1268]
	mvn	r3, #0
	str	r7, [r5, #-1264]
	strh	r3, [r4]	@ movhi
	strh	r3, [r4, #2]	@ movhi
	mov	r3, #1
	str	r3, [r4, #36]
	sub	r3, r5, #1664
	add	r3, r3, #388
	str	r6, [r4, #28]
	str	r3, [sp, #20]
.L2326:
	ldr	r2, [sp, #8]
	sxth	r3, r6
	cmp	r3, r2
	bge	.L2345
	ldr	r2, [sp, #8]
	sub	r2, r2, #1
	cmp	r3, r2
	lsl	r2, r3, #1
	bne	.L2327
	ldrh	r0, [r10, r2]
	mov	r1, #1
	str	r3, [sp, #16]
	add	r3, r10, r2
	str	r3, [sp, #8]
	mov	r8, #0
	bl	FtlGetLastWrittenPage
	ldr	r2, [sp, #12]
	sxth	r9, r0
	ldr	r3, [sp, #16]
	add	r0, r0, #1
	strh	r6, [r4]	@ movhi
	ldr	r10, .L2367+4
	ldr	r6, .L2367+8
	strh	r0, [r4, #2]	@ movhi
	ldr	r3, [r2, r3, lsl #2]
	str	r3, [r4, #28]
.L2328:
	sxth	r2, r8
	add	r1, r9, #1
	cmp	r2, r1
	blt	.L2331
.L2345:
	mov	r0, r4
	bl	ftl_free_no_use_map_blk
	ldr	r3, .L2367+12
	ldrh	r2, [r4, #2]
	ldrh	r3, [r3]
	cmp	r2, r3
	bne	.L2333
	mov	r0, r4
	bl	ftl_map_blk_alloc_new_blk
.L2333:
	mov	r0, r4
	bl	ftl_map_blk_gc
	mov	r0, r4
	bl	ftl_map_blk_gc
	mov	r0, #0
	add	sp, sp, #36
	@ sp needed
	pop	{r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L2331:
	ldr	r3, [sp, #8]
	mov	r0, r6
	ldrh	r1, [r3]
	orr	r2, r2, r1, lsl #10
	str	r2, [r5, #-1272]
	mov	r2, #1
	mov	r1, r2
	bl	FlashReadPages
	ldrb	r2, [r10, #36]	@ zero_extendqisi2
	cmp	r2, #0
	beq	.L2329
	ldr	r2, [r5, #-1264]
	ldr	r2, [r2, #12]
	cmp	r2, #0
	str	r2, [sp, #12]
	beq	.L2329
	sub	r1, r6, #380
	ldr	r0, [r5, #-1268]
	ldrh	r1, [r1]
	bl	js_hash
	ldr	r2, [sp, #12]
	cmp	r2, r0
	mvnne	r2, #0
	strne	r2, [r5, #-1276]
.L2329:
	ldr	r1, .L2367
	ldr	r2, [r1, #-1276]
	cmn	r2, #1
	beq	.L2330
	ldrh	r2, [r7, #8]
	cmp	fp, r2
	bls	.L2330
	ldrh	ip, [r7]
	ldrh	r0, [r4, #4]
	cmp	ip, r0
	ldreq	r1, [r1, #-1272]
	ldreq	r3, [sp, #4]
	streq	r1, [r3, r2, lsl #2]
.L2330:
	add	r8, r8, #1
	b	.L2328
.L2327:
	ldr	r3, [r5, #-1472]
	ldr	r0, [sp, #20]
	str	r3, [r5, #-1268]
	add	r3, r10, r2
	str	r3, [sp, #16]
	ldr	r3, .L2367+12
	ldrh	r2, [r10, r2]
	ldrh	r3, [r3]
	sub	r3, r3, #1
	orr	r3, r3, r2, lsl #10
	mov	r2, #1
	mov	r1, r2
	str	r3, [r5, #-1272]
	bl	FlashReadPages
	ldr	r3, [r5, #-1276]
	cmn	r3, #1
	beq	.L2347
	ldrh	r2, [r7]
	ldrh	r3, [r4, #4]
	cmp	r2, r3
	bne	.L2347
	ldrh	r2, [r7, #8]
	movw	r3, #64245
	cmp	r2, r3
	beq	.L2335
.L2347:
	mov	r9, #0
.L2336:
	ldr	r2, .L2367+12
	sxth	r3, r9
	ldrh	r2, [r2]
	cmp	r3, r2
	bge	.L2343
	ldr	r2, [sp, #16]
	ldrh	r2, [r2]
	orr	r3, r3, r2, lsl #10
	mov	r2, #1
	mov	r1, r2
	str	r3, [r8, #-1272]
	ldr	r3, .L2367+8
	mov	r0, r3
	str	r3, [sp, #28]
	bl	FlashReadPages
	ldr	r2, .L2367+4
	ldrb	r2, [r2, #36]	@ zero_extendqisi2
	cmp	r2, #0
	beq	.L2340
	ldr	r2, [r8, #-1264]
	ldr	r2, [r2, #12]
	cmp	r2, #0
	str	r2, [sp, #24]
	beq	.L2340
	ldr	r3, [sp, #28]
	ldr	r0, [r8, #-1268]
	sub	r3, r3, #380
	ldrh	r1, [r3]
	bl	js_hash
	ldr	r2, [sp, #24]
	cmp	r2, r0
	mvnne	r3, #0
	strne	r3, [r8, #-1276]
.L2340:
	ldr	r3, [r8, #-1276]
	cmn	r3, #1
	beq	.L2341
	ldrh	r3, [r7, #8]
	cmp	fp, r3
	bls	.L2341
	ldrh	r1, [r7]
	ldrh	r2, [r4, #4]
	cmp	r1, r2
	ldreq	r2, [r8, #-1272]
	ldreq	r1, [sp, #4]
	streq	r2, [r1, r3, lsl #2]
.L2341:
	add	r9, r9, #1
	b	.L2336
.L2335:
	mov	r1, #0
	mov	ip, #4
.L2337:
	ldr	r2, .L2367+12
	sxth	r3, r1
	ldrh	r2, [r2]
	sub	r2, r2, #1
	cmp	r3, r2
	blt	.L2339
.L2343:
	add	r6, r6, #1
	b	.L2326
.L2339:
	ldr	r0, [r8, #-1472]
	add	r1, r1, #1
	ldr	r2, [r0, r3, lsl #3]
	uxth	lr, r2
	cmp	fp, lr
	addhi	r3, ip, r3, lsl #3
	movhi	r2, lr
	ldrhi	r3, [r0, r3]
	ldrhi	r0, [sp, #4]
	strhi	r3, [r0, r2, lsl #2]
	b	.L2337
.L2368:
	.align	2
.L2367:
	.word	.LANCHOR2
	.word	.LANCHOR0
	.word	.LANCHOR2-1276
	.word	.LANCHOR2-1664
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
	ldr	r3, .L2371
	push	{r4, lr}
	.save {r4, lr}
	sub	r2, r3, #1632
	add	r0, r3, #1072
	ldrh	r1, [r2, #-12]
	add	r0, r0, #4
	ldrh	r2, [r2, #-10]
	strh	r1, [r0, #10]	@ movhi
	strh	r2, [r0, #6]	@ movhi
	ldr	r2, [r3, #-1392]
	ldr	r1, .L2371+4
	str	r2, [r3, #1088]
	ldr	r2, [r3, #-1384]
	strh	r1, [r0, #4]	@ movhi
	sub	r1, r3, #1280
	ldrh	r1, [r1, #-8]
	str	r2, [r3, #1092]
	ldr	r2, [r3, #-1388]
	strh	r1, [r0, #8]	@ movhi
	str	r2, [r3, #1096]
	ldr	r2, [r3, #-1380]
	str	r2, [r3, #1100]
	bl	FtlMapTblRecovery
	mov	r0, #0
	pop	{r4, pc}
.L2372:
	.align	2
.L2371:
	.word	.LANCHOR2
	.word	-3962
	.fnend
	.size	FtlLoadVonderInfo, .-FtlLoadVonderInfo
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
	push	{r4, r5, r6, r7, r8, lr}
	.save {r4, r5, r6, r7, r8, lr}
	mov	r1, #0
	ldr	r4, .L2377
	ldr	r2, [r4, #-1636]
	sub	r6, r4, #1648
	sub	r5, r4, #1616
	ldr	r0, [r4, #-1396]
	lsl	r2, r2, #1
	bl	ftl_memset
	ldrh	r3, [r6, #-8]
	mov	r1, #255
	ldrh	r2, [r5, #-10]
	ldr	r0, [r4, #-1360]
	mul	r2, r2, r3
	bl	ftl_memset
	mov	r2, #0
	mov	r3, r4
	mov	r0, r6
	mov	r1, r5
	mov	r4, #12
	mov	r5, r2
	mvn	r6, #0
.L2374:
	ldrh	r7, [r1, #-10]
	uxth	ip, r2
	add	lr, r2, #1
	cmp	r7, ip
	bhi	.L2375
	ldr	r2, .L2377+4
	mvn	r0, #0
	movw	ip, #1028
	ldrh	r1, [r1, #-12]
	strh	r0, [r3, ip]	@ movhi
	strh	r0, [r2, #2]	@ movhi
	ldr	r0, [r3, #-1636]
	strh	r1, [r2, #6]	@ movhi
	strh	r0, [r2, #10]	@ movhi
	ldr	r0, .L2377+8
	strh	r0, [r2, #4]	@ movhi
	movw	r0, #1144
	ldrh	r0, [r3, r0]
	strh	r0, [r2, #8]	@ movhi
	ldr	r2, [r3, #-1400]
	str	r2, [r3, #1040]
	ldr	r2, [r3, #-1368]
	str	r2, [r3, #1044]
	ldr	r2, [r3, #-1396]
	str	r2, [r3, #1048]
	ldr	r2, [r3, #-1376]
	str	r2, [r3, #1052]
	pop	{r4, r5, r6, r7, r8, pc}
.L2375:
	uxth	r2, r2
	ldr	ip, [r3, #-1364]
	mul	r7, r4, r2
	add	r8, ip, r7
	str	r5, [r8, #4]
	strh	r6, [ip, r7]	@ movhi
	ldr	ip, [r3, #-1364]
	add	ip, ip, r7
	ldrh	r7, [r0, #-8]
	mul	r2, r2, r7
	ldr	r7, [r3, #-1360]
	bic	r2, r2, #3
	add	r2, r7, r2
	str	r2, [ip, #8]
	mov	r2, lr
	b	.L2374
.L2378:
	.align	2
.L2377:
	.word	.LANCHOR2
	.word	.LANCHOR2+1028
	.word	-3902
	.fnend
	.size	FtlL2PDataInit, .-FtlL2PDataInit
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
	ldr	r0, .L2381
	bl	FtlMapTblRecovery
	mov	r0, #0
	pop	{r4, pc}
.L2382:
	.align	2
.L2381:
	.word	.LANCHOR2+1028
	.fnend
	.size	FtlLoadMapInfo, .-FtlLoadMapInfo
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
	push	{r4, lr}
	.save {r4, lr}
	strh	r3, [r0, #4]	@ movhi
	movw	r4, #65535
	ldr	r3, .L2391
	ldrh	lr, [r3, #-4]
	add	r3, r0, r2, lsl #1
	add	r3, r3, #14
.L2384:
	cmp	r2, lr
	bcc	.L2386
	ldr	r3, .L2391+4
	add	ip, r0, #16
	movw	r4, #65535
	ldrh	r3, [r3, #-2]
	sub	r3, r3, #1
	sub	r1, r3, r1
	mov	r3, #0
	uxth	r1, r1
.L2387:
	uxth	r2, r3
	cmp	lr, r2
	bhi	.L2389
	pop	{r4, pc}
.L2386:
	ldrh	ip, [r3, #2]!
	add	r2, r2, #1
	uxth	r2, r2
	cmp	ip, r4
	ldrhne	ip, [r0, #4]
	addne	ip, ip, #1
	strhne	ip, [r0, #4]	@ movhi
	b	.L2384
.L2389:
	ldrh	r2, [ip], #2
	add	r3, r3, #1
	cmp	r2, r4
	ldrhne	r2, [r0, #4]
	addne	r2, r1, r2
	strhne	r2, [r0, #4]	@ movhi
	b	.L2387
.L2392:
	.align	2
.L2391:
	.word	.LANCHOR2-1728
	.word	.LANCHOR2-1664
	.fnend
	.size	ftl_sb_update_avl_pages, .-ftl_sb_update_avl_pages
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
	ldr	r6, .L2403
	ubfx	r0, r1, #10, #16
	str	r1, [sp, #4]
	bl	P2V_block_in_plane
	ldr	r2, [r6, #-1404]
	lsl	r7, r0, #1
	ldrh	r3, [r2, r7]
	cmp	r3, #0
	bne	.L2394
	ldr	r4, [r6, #876]
	cmp	r4, #0
	beq	.L2395
	ldr	r2, [r6, #-1356]
	add	r8, r6, #880
	ldr	ip, .L2403+4
	mov	lr, #6
	ldrh	r1, [r8]
	movw	r9, #65535
	sub	r4, r4, r2
	asr	r4, r4, #1
	mul	r4, ip, r4
	uxth	r4, r4
.L2396:
	uxth	ip, r3
	cmp	r1, ip
	bls	.L2395
	cmp	r4, r0
	bne	.L2397
	mov	r1, r4
	ldr	r0, .L2403+8
	bl	List_remove_node
	ldrh	r3, [r8]
	mov	r0, r4
	sub	r3, r3, #1
	strh	r3, [r8]	@ movhi
	bl	INSERT_DATA_LIST
	ldr	r2, [r6, #-1404]
	ldrh	r3, [r2, r7]
.L2394:
	add	r3, r3, #1
	strh	r3, [r2, r7]	@ movhi
	b	.L2395
.L2397:
	mul	r4, lr, r4
	add	r3, r3, #1
	ldrh	r4, [r2, r4]
	cmp	r4, r9
	bne	.L2396
.L2395:
	mov	r2, #1
	add	r1, sp, #4
	mov	r0, r5
	bl	log2phys
	add	sp, sp, #12
	@ sp needed
	pop	{r4, r5, r6, r7, r8, r9, pc}
.L2404:
	.align	2
.L2403:
	.word	.LANCHOR2
	.word	-1431655765
	.word	.LANCHOR2+876
	.fnend
	.size	FtlReUsePrevPpa, .-FtlReUsePrevPpa
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
	ldr	r3, .L2418
	push	{r4, r5, r6, r7, r8, r9, r10, lr}
	.save {r4, r5, r6, r7, r8, r9, r10, lr}
	mov	r4, r0
	add	r7, r0, #16
	mvn	r9, #0
	add	r6, r3, #22
	mov	r5, #0
	ldrh	r8, [r3, #-4]
	strh	r5, [r0, #4]	@ movhi
	strb	r5, [r0, #7]
.L2406:
	uxth	r3, r5
	cmp	r8, r3
	bhi	.L2408
	ldr	r2, .L2418+4
	ldrb	r3, [r4, #7]	@ zero_extendqisi2
	sub	r1, r2, #1664
	ldrh	r1, [r1, #-2]
	smulbb	r3, r3, r1
	strh	r3, [r4, #4]	@ movhi
	mov	r3, #0
	strb	r3, [r4, #9]
	ldr	r3, [r2, #-1868]
	cmp	r3, #0
	beq	.L2409
	ldrh	r3, [r4]
	ldr	r2, [r2, #-1412]
	lsl	r3, r3, #1
	ldrh	r3, [r2, r3]
	cmp	r3, #79
	movls	r3, #1
	strbls	r3, [r4, #9]
.L2409:
	ldr	r3, .L2418+8
	mov	r0, #0
	ldrb	r3, [r3, #36]	@ zero_extendqisi2
	cmp	r3, #0
	movne	r3, #1
	strbne	r3, [r4, #9]
	pop	{r4, r5, r6, r7, r8, r9, r10, pc}
.L2408:
	ldrb	r0, [r6, r5]	@ zero_extendqisi2
	add	r7, r7, #2
	ldrh	r1, [r4]
	add	r5, r5, #1
	bl	V2P_block
	strh	r9, [r7, #-2]	@ movhi
	mov	r10, r0
	bl	FtlBbmIsBadBlock
	cmp	r0, #0
	strheq	r10, [r7, #-2]	@ movhi
	ldrbeq	r3, [r4, #7]	@ zero_extendqisi2
	addeq	r3, r3, #1
	strbeq	r3, [r4, #7]
	b	.L2406
.L2419:
	.align	2
.L2418:
	.word	.LANCHOR2-1728
	.word	.LANCHOR2
	.word	.LANCHOR0
	.fnend
	.size	make_superblock, .-make_superblock
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
	ldr	r4, .L2449
	.pad #36
	sub	sp, sp, #36
	movw	r8, #1128
	ldr	r3, [r4, #-1472]
	sub	r6, r4, #1712
	ldrh	r2, [r6, #-12]
	ldr	r0, [r4, #-1404]
	str	r3, [r4, #-1268]
	ldr	r3, [r4, #-1440]
	lsl	r2, r2, #1
	str	r3, [r4, #-1264]
	bl	ftl_memset
	ldrh	r0, [r4, r8]
	movw	r3, #65535
	cmp	r0, r3
	bne	.L2421
.L2432:
	mvn	r0, #0
.L2420:
	add	sp, sp, #36
	@ sp needed
	pop	{r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L2421:
	mov	r1, #1
	ldr	r7, .L2449+4
	bl	FtlGetLastWrittenPage
	ldrsh	r10, [r4, r8]
	sub	r8, r4, #1264
	sub	r8, r8, #12
	sxth	r5, r0
	add	r0, r0, #1
	strh	r0, [r7, #2]	@ movhi
.L2423:
	cmp	r5, #0
	ldr	fp, .L2449+8
	blt	.L2431
	orr	r3, r5, r10, lsl #10
	mov	r2, #1
	mov	r1, r2
	str	r3, [r4, #-1272]
	mov	r0, r8
	ldr	r3, [r4, #-1472]
	str	r3, [r4, #-1268]
	bl	FlashReadPages
	ldrb	r3, [fp, #36]	@ zero_extendqisi2
	cmp	r3, #0
	beq	.L2424
	ldr	r9, [r4, #-1264]
	ldr	r3, [r9, #12]
	cmp	r3, #0
	str	r3, [sp, #28]
	beq	.L2424
	ldr	r2, [r4, #-1268]
	sub	r1, r8, #380
	ldrh	r1, [r1]
	mov	r0, r2
	str	r2, [sp, #24]
	bl	js_hash
	ldr	r3, [sp, #28]
	cmp	r3, r0
	beq	.L2424
	cmp	r5, #0
	bne	.L2425
	ldrh	r1, [r7, #4]
	ldr	r2, [sp, #24]
	cmp	r10, r1
	beq	.L2425
	ldr	r2, [r2]
	str	r3, [sp, #12]
	ldrh	r1, [r7]
	str	r2, [sp, #16]
	ldr	r3, [r9, #8]
	ldr	r2, [r4, #-1276]
	ldr	r0, .L2449+12
	str	r3, [sp, #8]
	ldr	r3, [r9, #4]
	str	r3, [sp, #4]
	ldr	r3, [r9]
	str	r3, [sp]
	ldr	r3, [r4, #-1272]
	bl	printk
	sub	r3, r8, #388
	ldrsh	r10, [r7, #4]
	ldrh	r5, [r3]
.L2427:
	sub	r5, r5, #1
	sxth	r5, r5
	b	.L2423
.L2425:
	mvn	r3, #0
	str	r3, [r4, #-1276]
.L2424:
	ldr	r3, [r4, #-1276]
	cmn	r3, #1
	beq	.L2427
	ldr	r3, [r4, #-1472]
	ldr	r2, .L2449+16
	ldr	r3, [r3]
	cmp	r3, r2
	bne	.L2427
	ldr	r3, [r4, #-1440]
	ldrh	r2, [r3]
	movw	r3, #61604
	cmp	r2, r3
	bne	.L2427
.L2431:
	mov	r2, #48
	ldr	r1, [r4, #-1268]
	ldr	r0, .L2449+20
	bl	ftl_memcpy
	ldrh	r2, [r6, #-12]
	ldr	r1, [r4, #-1268]
	ldr	r0, [r4, #-1404]
	lsl	r2, r2, #1
	add	r1, r1, #48
	bl	ftl_memcpy
	ldrh	r1, [r6, #-12]
	ldr	r3, [r4, #-1268]
	ldr	r0, [fp, #32]
	lsr	r2, r1, #3
	lsl	r1, r1, #1
	add	r1, r1, #51
	add	r2, r2, #4
	bic	r1, r1, #3
	add	r1, r3, r1
	bl	ftl_memcpy
	ldr	r3, .L2449+24
	ldrh	r2, [r3, #-8]
	cmp	r2, #0
	beq	.L2429
	ldrh	r1, [r6, #-12]
	ldrh	r2, [r3, #-12]
	ldr	r0, [r4, #-1372]
	lsr	r3, r1, #3
	lsl	r2, r2, #2
	add	r3, r3, r1, lsl #1
	ldr	r1, [r4, #-1268]
	add	r3, r3, #52
	ubfx	r3, r3, #2, #14
	add	r1, r1, r3, lsl #2
	bl	ftl_memcpy
.L2429:
	ldr	r2, [r4, #816]
	ldr	r3, .L2449+16
	cmp	r2, r3
	bne	.L2432
	ldr	r5, .L2449+20
	ldrb	r1, [r4, #826]	@ zero_extendqisi2
	sub	r3, r5, #2512
	ldrh	r2, [r5, #8]
	ldrh	r3, [r3, #-14]
	strh	r2, [r7, #6]	@ movhi
	cmp	r1, r3
	bne	.L2432
	sub	r1, r5, #2480
	sub	r0, r5, #2464
	ldrh	r3, [r1, #-2]
	add	r1, r1, #316
	ldrh	r0, [r0, #-12]
	add	r6, r5, #336
	str	r2, [r4, #1148]
	mul	r3, r2, r3
	str	r3, [r4, #-1284]
	mul	r3, r3, r0
	ldr	r0, [r4, #-1720]
	str	r3, [r4, #-2736]
	ldrh	r3, [r1, #6]
	sub	r0, r0, r3
	sub	r3, r5, #2544
	ldrh	r1, [r3, #-4]
	sub	r0, r0, r2
	bl	__aeabi_uidiv
	ldrh	r3, [r5, #16]
	mov	r2, r5
	ldrh	ip, [r5, #14]
	strh	r0, [r6]	@ movhi
	lsr	r1, r3, #6
	and	r3, r3, #63
	strb	r3, [r4, #890]
	ldrb	r3, [r4, #827]	@ zero_extendqisi2
	strh	ip, [r2, #68]!	@ movhi
	strh	r1, [r2, #2]	@ movhi
	mvn	r1, #0
	strb	r3, [r4, #892]
	movw	r3, #1156
	strh	r1, [r4, r3]	@ movhi
	add	r2, r5, #340
	ldrh	r1, [r5, #18]
	mov	r3, #0
	strh	r3, [r2, #2]	@ movhi
	mov	r2, r5
	strb	r3, [r4, #1162]
	strh	r1, [r2, #116]!	@ movhi
	ldrh	r1, [r5, #20]
	strb	r3, [r4, #1164]
	lsr	r0, r1, #6
	and	r1, r1, #63
	strb	r1, [r4, #938]
	ldrb	r1, [r4, #828]	@ zero_extendqisi2
	strh	r0, [r2, #2]	@ movhi
	ldrh	r0, [r5, #22]
	strb	r1, [r4, #940]
	mov	r1, r5
	strh	r0, [r1, #164]!	@ movhi
	mov	r6, r1
	ldrh	r0, [r5, #24]
	mov	r5, r2
	lsr	lr, r0, #6
	and	r0, r0, #63
	strb	r0, [r4, #986]
	ldrb	r0, [r4, #829]	@ zero_extendqisi2
	strh	lr, [r1, #2]	@ movhi
	strb	r0, [r4, #988]
	str	r3, [r4, #-1604]
	ldr	r0, [r4, #848]
	str	r3, [r4, #-1600]
	str	r3, [r4, #-1584]
	str	r3, [r4, #-1588]
	str	r0, [r4, #-1580]
	str	r3, [r4, #-1576]
	ldr	r0, [r4, #-1612]
	str	r3, [r4, #-1568]
	str	r3, [r4, #-1592]
	ldr	r3, [r4, #856]
	ldr	r2, [r4, #-1608]
	cmp	r3, r0
	strhi	r3, [r4, #-1612]
	ldr	r3, [r4, #852]
	cmp	r3, r2
	strhi	r3, [r4, #-1608]
	movw	r3, #65535
	cmp	ip, r3
	beq	.L2435
	ldr	r0, .L2449+28
	bl	make_superblock
.L2435:
	ldrh	r2, [r5]
	movw	r3, #65535
	cmp	r2, r3
	beq	.L2436
	ldr	r0, .L2449+32
	bl	make_superblock
.L2436:
	ldrh	r2, [r6]
	movw	r3, #65535
	cmp	r2, r3
	beq	.L2437
	ldr	r0, .L2449+36
	bl	make_superblock
.L2437:
	movw	r3, #1156
	ldrh	r2, [r4, r3]
	movw	r3, #65535
	cmp	r2, r3
	beq	.L2438
	ldr	r0, .L2449+40
	bl	make_superblock
.L2438:
	mov	r0, #0
	b	.L2420
.L2450:
	.align	2
.L2449:
	.word	.LANCHOR2
	.word	.LANCHOR2+1128
	.word	.LANCHOR0
	.word	.LC64
	.word	1179929683
	.word	.LANCHOR2+816
	.word	.LANCHOR2-1616
	.word	.LANCHOR2+884
	.word	.LANCHOR2+932
	.word	.LANCHOR2+980
	.word	.LANCHOR2+1156
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
	@ args = 0, pretend = 0, frame = 72
	@ frame_needed = 0, uses_anonymous_args = 0
	push	{r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.save {r4, r5, r6, r7, r8, r9, r10, fp, lr}
	ubfx	r0, r0, #10, #16
	ldr	r7, .L2463
	.pad #100
	sub	sp, sp, #100
	mov	r8, r1
	bl	P2V_block_in_plane
	sub	r5, r7, #1664
	mov	r6, r0
	ldr	r1, .L2463+4
	ldr	r0, .L2463+8
	ldrh	r9, [r5, #-2]
	bl	printk
	ldr	r2, [r7, #-1404]
	lsl	r3, r6, #1
	mov	r1, r6
	ldr	r0, .L2463+12
	ldrh	r2, [r2, r3]
	bl	printk
	add	r0, sp, #96
	strh	r6, [r0, #-48]!	@ movhi
	bl	make_superblock
	ldrb	r4, [r7, #-2740]	@ zero_extendqisi2
	str	r7, [sp, #44]
	adds	r3, r4, #0
	movne	r3, #1
	cmp	r8, #0
	movne	r3, #0
	cmp	r3, #0
	moveq	r4, r3
	beq	.L2452
	mov	r0, r6
	bl	ftl_get_blk_mode
	cmp	r0, #1
	mov	r4, r0
	ldrheq	r9, [r5]
.L2452:
	ldr	r7, .L2463
	mov	r6, #0
	mov	r10, #36
	ldrh	r3, [r5, #-2]
	mov	r2, r9
	mov	r1, r4
	ldr	r0, .L2463+16
	bl	printk
.L2453:
	ldr	r3, .L2463+20
	add	ip, sp, #62
	ldr	r0, [r7, #-1500]
	movw	lr, #65535
	ldrh	r3, [r3, #-4]
	str	r3, [sp, #28]
	ldr	r3, [r7, #-1460]
	str	r3, [sp, #32]
	ldr	r3, .L2463+24
	ldrh	r2, [r3, #-8]
	ldrh	fp, [r3, #-6]
	str	r2, [sp, #36]
	ldr	r2, [r7, #-1432]
	str	r2, [sp, #40]
	mov	r2, #0
	mov	r5, r2
.L2454:
	ldr	r1, [sp, #28]
	uxth	r3, r2
	cmp	r1, r3
	bhi	.L2456
	mov	r8, #0
	mov	r2, r4
	mov	r1, r5
	bl	FlashReadPages
.L2457:
	uxth	r3, r8
	cmp	r5, r3
	bhi	.L2458
	add	r6, r6, #1
	uxth	r6, r6
	cmp	r9, r6
	bne	.L2453
.L2459:
	mov	r0, #0
	add	sp, sp, #100
	@ sp needed
	pop	{r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L2456:
	ldrh	r3, [ip, #2]!
	cmp	r3, lr
	beq	.L2455
	mla	r1, r10, r5, r0
	orr	r3, r6, r3, lsl #10
	str	r3, [r1, #4]
	ldr	r3, [sp, #36]
	mul	r3, r3, r5
	add	r8, r3, #3
	cmp	r3, #0
	movlt	r3, r8
	ldr	r8, [sp, #32]
	bic	r3, r3, #3
	add	r3, r8, r3
	str	r3, [r1, #8]
	mul	r3, fp, r5
	add	r5, r5, #1
	uxth	r5, r5
	add	r8, r3, #3
	cmp	r3, #0
	movlt	r3, r8
	ldr	r8, [sp, #40]
	bic	r3, r3, #3
	add	r3, r8, r3
	str	r3, [r1, #12]
.L2455:
	add	r2, r2, #1
	b	.L2454
.L2458:
	ldr	r3, [sp, #44]
	mul	r0, r10, r8
	ldrh	r1, [sp, #48]
	add	r8, r8, #1
	ldr	ip, [r3, #-1500]
	add	r2, ip, r0
	ldr	lr, [r2, #8]
	ldr	r3, [r2, #12]
	ldr	fp, [lr, #4]
	str	fp, [sp, #20]
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
	ldr	r0, .L2463+28
	bl	printk
	b	.L2457
.L2464:
	.align	2
.L2463:
	.word	.LANCHOR2
	.word	.LANCHOR3+153
	.word	.LC50
	.word	.LC65
	.word	.LC66
	.word	.LANCHOR2-1728
	.word	.LANCHOR2-1648
	.word	.LC60
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
	ldr	r5, .L2476
	.pad #84
	sub	sp, sp, #84
	ldr	r1, .L2476+4
	ldr	r0, .L2476+8
	bl	printk
.L2466:
	ldr	r3, .L2476+12
	uxth	r0, r6
	ldrh	r3, [r3, #-10]
	cmp	r3, r0
	bhi	.L2474
	mov	r0, #0
	add	sp, sp, #84
	@ sp needed
	pop	{r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L2474:
	add	r4, sp, #80
	movw	r9, #65535
	strh	r0, [r4, #-48]!	@ movhi
	mov	r10, #36
	bl	ftl_get_blk_mode
	uxth	r1, r6
	ldr	ip, [r5, #-1404]
	mov	r3, r0
	ldr	r0, .L2476+16
	lsl	r2, r1, #1
	ldrh	r2, [ip, r2]
	bl	printk
	mov	r0, r4
	bl	make_superblock
	ldr	r3, .L2476+20
	add	ip, sp, #46
	ldr	r0, [r5, #-1500]
	ldr	r7, [r5, #-1432]
	ldrh	r2, [r3, #-4]
	ldrh	lr, [r3, #72]
	ldrh	r8, [r3, #74]
	str	r2, [sp, #24]
	ldr	r2, [r5, #-1460]
	str	r2, [sp, #28]
	mov	r2, #0
	mov	r4, r2
.L2467:
	ldr	r1, [sp, #24]
	uxth	r3, r2
	cmp	r1, r3
	bhi	.L2469
	ldr	r9, .L2476+24
	mov	r7, #0
	mov	r8, #36
	mov	r2, #0
	mov	r1, r4
	bl	FlashReadPages
.L2470:
	uxth	r3, r7
	cmp	r4, r3
	bhi	.L2471
	ldr	r9, .L2476+28
	mov	r7, #0
	mov	r8, #36
	mov	r2, #1
	mov	r1, r4
	ldr	r0, [r5, #-1500]
	bl	FlashReadPages
.L2472:
	uxth	r3, r7
	cmp	r4, r3
	bhi	.L2473
	add	r6, r6, #1
	b	.L2466
.L2469:
	ldrh	r3, [ip, #2]!
	cmp	r3, r9
	beq	.L2468
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
.L2468:
	add	r2, r2, #1
	b	.L2467
.L2471:
	mul	r0, r8, r7
	ldr	ip, [r5, #-1500]
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
	b	.L2470
.L2473:
	mul	r0, r8, r7
	ldr	ip, [r5, #-1500]
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
	b	.L2472
.L2477:
	.align	2
.L2476:
	.word	.LANCHOR2
	.word	.LANCHOR3+170
	.word	.LC50
	.word	.LANCHOR2-1712
	.word	.LC67
	.word	.LANCHOR2-1728
	.word	.LC68
	.word	.LC69
	.fnend
	.size	FtlScanAllBlock, .-FtlScanAllBlock
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
	mov	r2, #6
	ldr	r4, .L2489
	mov	r5, #0
	.pad #20
	sub	sp, sp, #20
	mov	r1, #0
	mov	r8, r5
	mov	r9, r5
	sub	r6, r4, #1712
	ldr	r0, [r4, #-1356]
	ldrh	r3, [r6, #-10]
	mul	r2, r2, r3
	bl	ftl_memset
	add	r2, r4, #880
	add	r3, r4, #872
	strh	r5, [r2]	@ movhi
	sub	r2, r4, #1616
	str	r5, [r4, #876]
	str	r5, [r4, #864]
	str	r5, [r4, #868]
	strh	r5, [r3]	@ movhi
	strh	r5, [r2, #-6]	@ movhi
	str	r6, [sp]
	str	r3, [sp, #4]
.L2479:
	ldr	r3, [sp]
	sxth	r7, r5
	ldrh	r3, [r3, #-12]
	cmp	r7, r3
	bge	.L2486
	ldr	r3, .L2489+4
	mov	r10, #0
	mov	r6, r10
	uxth	fp, r5
	ldrh	r2, [r3]
	add	r3, r3, #4
	ldrh	r3, [r3, #62]
	b	.L2487
.L2481:
	add	r0, r4, r1
	mov	r1, fp
	ldrb	r0, [r0, #-1706]	@ zero_extendqisi2
	add	r10, r10, #1
	str	r3, [sp, #12]
	str	r2, [sp, #8]
	bl	V2P_block
	bl	FtlBbmIsBadBlock
	ldr	r3, [sp, #12]
	cmp	r0, #0
	ldr	r2, [sp, #8]
	addeq	r6, r3, r6
	sxtheq	r6, r6
.L2487:
	sxth	r1, r10
	cmp	r1, r2
	blt	.L2481
	cmp	r6, #0
	lsl	r10, r7, #1
	ldreq	r3, [r4, #-1404]
	mvneq	r2, #0
	strheq	r2, [r3, r10]	@ movhi
	beq	.L2483
	mov	r1, r6
	mov	r0, #32768
	bl	__aeabi_idiv
	sxth	r6, r0
.L2483:
	ldr	r2, [r4, #-1356]
	add	r3, r10, r7
	add	r3, r2, r3, lsl #1
	strh	r6, [r3, #4]	@ movhi
	ldr	r3, .L2489+8
	ldrh	r3, [r3]
	cmp	r7, r3
	beq	.L2484
	ldr	r3, .L2489+12
	ldrh	r3, [r3]
	cmp	r7, r3
	beq	.L2484
	ldr	r3, .L2489+16
	ldrh	r3, [r3]
	cmp	r7, r3
	beq	.L2484
	ldr	r3, [r4, #-1404]
	uxth	r0, r5
	ldrh	r3, [r3, r10]
	cmp	r3, #0
	bne	.L2485
	add	r8, r8, #1
	uxth	r8, r8
	bl	INSERT_FREE_LIST
.L2484:
	add	r5, r5, #1
	b	.L2479
.L2485:
	add	r9, r9, #1
	uxth	r9, r9
	bl	INSERT_DATA_LIST
	b	.L2484
.L2486:
	ldr	r3, [sp, #4]
	mov	r0, #0
	strh	r9, [r3]	@ movhi
	ldr	r3, .L2489+20
	strh	r8, [r3]	@ movhi
	add	sp, sp, #20
	@ sp needed
	pop	{r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L2490:
	.align	2
.L2489:
	.word	.LANCHOR2
	.word	.LANCHOR2-1732
	.word	.LANCHOR2+884
	.word	.LANCHOR2+932
	.word	.LANCHOR2+980
	.word	.LANCHOR2+880
	.fnend
	.size	SupperBlkListInit, .-SupperBlkListInit
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
	ldr	r3, .L2493
	ldr	r2, .L2493+4
	ldr	r1, [r3, #1204]
	cmp	r1, r2
	bxne	lr
	ldr	r2, .L2493+8
	mov	r1, #1
	mov	r0, #0
	str	r2, [r3, #1208]
	ldr	r2, [r3, #1716]
	str	r2, [r3, #1292]
	ldr	r2, [r3, #1720]
	str	r2, [r3, #1296]
	ldr	r2, [r3, #-1604]
	str	r2, [r3, #1212]
	ldr	r2, [r3, #-1600]
	str	r2, [r3, #1216]
	ldr	r2, [r3, #-1584]
	str	r2, [r3, #1220]
	ldr	r2, [r3, #-1588]
	str	r2, [r3, #1224]
	ldr	r2, [r3, #-1576]
	str	r2, [r3, #1232]
	ldr	r2, [r3, #-1572]
	str	r2, [r3, #1236]
	ldr	r2, [r3, #-1596]
	str	r2, [r3, #1240]
	ldr	r2, [r3, #-1592]
	str	r2, [r3, #1244]
	ldr	r2, [r3, #-1568]
	str	r2, [r3, #1248]
	ldr	r2, [r3, #-1564]
	str	r2, [r3, #1252]
	ldr	r2, [r3, #-1616]
	str	r2, [r3, #1264]
	ldr	r2, [r3, #-1620]
	str	r2, [r3, #1268]
	ldr	r2, .L2493+12
	b	FtlVendorPartWrite
.L2494:
	.align	2
.L2493:
	.word	.LANCHOR2
	.word	1179929683
	.word	1342177379
	.word	.LANCHOR2+1204
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
	ldr	r3, .L2505
	ldr	r2, [r3, #-1868]
	cmp	r2, #0
	moveq	r2, #32
	beq	.L2496
	ldr	r2, [r3, #-1564]
	cmp	r2, #39
	movhi	r2, #32
	movls	r2, #4
.L2496:
	movw	ip, #1724
	ldrh	r1, [r3, ip]
	cmp	r1, #31
	addls	r1, r1, #1
	movls	r2, #1
	strhls	r1, [r3, ip]	@ movhi
	cmp	r0, #0
	bne	.L2498
	ldr	r1, [r3, #-1416]
	ldr	r0, [r1, #20]
	ldr	r1, [r1, #16]
	add	r2, r2, r0
	cmp	r1, r2
	bcc	.L2503
.L2498:
	push	{r4, lr}
	.save {r4, lr}
	mov	r0, #64
	ldr	r2, [r3, #-1416]
	ldr	r1, [r2, #16]
	str	r1, [r2, #20]
	ldr	r1, .L2505+4
	str	r1, [r2]
	ldr	r2, [r3, #-1416]
	ldr	r3, .L2505+8
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
.L2503:
	mov	r0, #0
	bx	lr
.L2506:
	.align	2
.L2505:
	.word	.LANCHOR2
	.word	1112818501
	.word	.LANCHOR2-1424
	.fnend
	.size	FtlEctTblFlush, .-FtlEctTblFlush
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
	ldr	r4, .L2513
	mov	r0, #0
	ldr	r2, .L2513+4
	bl	FtlVendorPartRead
	ldr	r5, .L2513+8
	ldr	r3, [r4, #1204]
	cmp	r3, r5
	beq	.L2508
	mov	r2, #512
	mov	r1, #0
	ldr	r0, .L2513+4
	bl	ftl_memset
	str	r5, [r4, #1204]
.L2508:
	ldr	r3, [r4, #1204]
	cmp	r3, r5
	bne	.L2509
	ldr	r3, [r4, #1292]
	str	r3, [r4, #1716]
	ldr	r3, [r4, #1296]
	str	r3, [r4, #1720]
	ldr	r3, [r4, #1212]
	str	r3, [r4, #-1604]
	ldr	r3, [r4, #1216]
	str	r3, [r4, #-1600]
	ldr	r3, [r4, #1220]
	str	r3, [r4, #-1584]
	ldr	r3, [r4, #1224]
	str	r3, [r4, #-1588]
	ldr	r3, [r4, #1232]
	str	r3, [r4, #-1576]
	ldr	r3, [r4, #1236]
	str	r3, [r4, #-1572]
	ldr	r3, [r4, #1240]
	str	r3, [r4, #-1596]
	ldr	r3, [r4, #1244]
	str	r3, [r4, #-1592]
	ldr	r3, [r4, #1248]
	str	r3, [r4, #-1568]
	ldr	r3, [r4, #1252]
	str	r3, [r4, #-1564]
	ldr	r3, [r4, #1264]
	str	r3, [r4, #-1616]
.L2509:
	ldr	r1, [r4, #1272]
	mov	r3, #0
	ldr	r2, .L2513+12
	str	r3, [r4, #-1620]
	cmp	r1, r2
	bne	.L2510
	ldrb	r2, [r4, #-2740]	@ zero_extendqisi2
	cmp	r2, r3
	beq	.L2511
	str	r3, [r4, #1272]
	bl	Ftl_save_ext_data
.L2510:
	ldr	r3, .L2513+16
	ldr	ip, [r4, #-1580]
	ldr	r2, [r4, #-1576]
	ldrh	r0, [r3, #-10]
	ldrh	r1, [r3, #-60]
	mla	r0, ip, r0, r2
	bl	__aeabi_uidiv
	str	r0, [r4, #1728]
	pop	{r4, r5, r6, pc}
.L2511:
	mov	r3, #1
	ldr	r1, .L2513+20
	ldr	r0, .L2513+24
	str	r3, [r4, #-1868]
	bl	printk
	b	.L2510
.L2514:
	.align	2
.L2513:
	.word	.LANCHOR2
	.word	.LANCHOR2+1204
	.word	1179929683
	.word	305432421
	.word	.LANCHOR2-1664
	.word	.LC70
	.word	.LC49
	.fnend
	.size	Ftl_load_ext_data, .-Ftl_load_ext_data
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
	push	{r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.save {r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.pad #36
	sub	sp, sp, #36
	ldr	r6, .L2532
	ldrh	r4, [r6, #-4]
	add	r3, r6, #388
	str	r3, [sp, #28]
	sub	r4, r4, #16
	lsl	r10, r4, #10
.L2516:
	ldrh	r3, [r6, #-4]
	ldr	r1, .L2532+4
	cmp	r4, r3
	mov	r5, r1
	blt	.L2524
	ldr	r1, .L2532+8
	ldr	r0, .L2532+12
	add	sp, sp, #36
	@ sp needed
	pop	{r4, r5, r6, r7, r8, r9, r10, fp, lr}
	b	printk
.L2524:
	uxth	r8, r4
	mov	r0, r8
	bl	ftl_get_blk_mode
	ldrb	r3, [r1, #-2740]	@ zero_extendqisi2
	cmp	r3, #0
	beq	.L2517
	ldr	r3, .L2532+16
	ldrh	r3, [r3]
	cmp	r4, r3
	bge	.L2518
	ldr	r3, .L2532+20
	ldrh	r3, [r3]
	cmp	r4, r3
	blt	.L2518
.L2517:
	cmp	r0, #1
	ldrhne	r7, [r6, #-2]
	movne	r9, #0
	bne	.L2520
.L2518:
	ldrh	r7, [r6]
	mov	r9, #-2147483648
.L2520:
	mov	r3, r9
	mov	r2, r7
	mov	r1, r4
	ldr	r0, .L2532+24
	bl	printk
	mov	r0, r8
	bl	FtlBbmIsBadBlock
	cmp	r0, #0
	beq	.L2521
	mov	r3, r9
	mov	r2, r7
	mov	r1, r4
	ldr	r0, .L2532+28
	bl	printk
.L2521:
	ldr	fp, .L2532+32
	mov	r8, #0
.L2522:
	cmp	r8, r7
	addeq	r4, r4, #1
	addeq	r10, r10, #1024
	beq	.L2516
.L2523:
	add	r3, r9, r10
	mov	r2, #0
	add	r3, r3, r8
	mov	r1, #1
	str	r3, [r5, #-1272]
	add	r8, r8, #1
	ldr	r3, [r5, #-1472]
	ldr	r0, [sp, #28]
	str	r2, [r5, #-1276]
	str	r3, [r5, #-1268]
	ldr	r3, [r5, #-1440]
	str	r3, [r5, #-1264]
	bl	FlashReadPages
	ldr	r2, [r5, #-1268]
	mov	r0, fp
	ldr	r3, [r5, #-1264]
	ldr	r1, [r2, #4]
	str	r1, [sp, #16]
	ldr	r2, [r2]
	str	r2, [sp, #12]
	ldr	r2, [r3, #12]
	str	r2, [sp, #8]
	ldr	r2, [r3, #8]
	str	r2, [sp, #4]
	ldr	r2, [r3, #4]
	str	r2, [sp]
	ldr	r3, [r3]
	ldr	r2, [r5, #-1276]
	ldr	r1, [r5, #-1272]
	bl	printk
	b	.L2522
.L2533:
	.align	2
.L2532:
	.word	.LANCHOR2-1664
	.word	.LANCHOR2
	.word	.LANCHOR3+186
	.word	.LC74
	.word	.LANCHOR2-1724
	.word	.LANCHOR2-1652
	.word	.LC71
	.word	.LC72
	.word	.LC73
	.fnend
	.size	ftl_scan_all_ppa, .-ftl_scan_all_ppa
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
	ldr	r3, .L2541
	push	{r4, r5, r6, r7, r8, r9, r10, lr}
	.save {r4, r5, r6, r7, r8, r9, r10, lr}
	mov	r5, #0
	mov	r7, r0
	mov	r4, r5
	add	r6, r3, #22
	ldrh	r8, [r3, #-4]
	ldrh	r9, [r3, #62]
.L2535:
	uxth	r3, r5
	cmp	r8, r3
	bhi	.L2537
	cmp	r4, #0
	moveq	r0, r4
	beq	.L2538
	mov	r1, r4
	mov	r0, #32768
	bl	__aeabi_idiv
.L2538:
	ldr	r3, .L2541+4
	mov	r2, #6
	ldr	r3, [r3, #-1356]
	mla	r7, r2, r7, r3
	strh	r0, [r7, #4]	@ movhi
	mov	r0, #0
	pop	{r4, r5, r6, r7, r8, r9, r10, pc}
.L2537:
	mov	r1, r7
	ldrb	r0, [r6, r5]	@ zero_extendqisi2
	bl	V2P_block
	bl	FtlBbmIsBadBlock
	cmp	r0, #0
	add	r5, r5, #1
	addeq	r4, r4, r9
	uxtheq	r4, r4
	b	.L2535
.L2542:
	.align	2
.L2541:
	.word	.LANCHOR2-1728
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
	ldr	r2, .L2546
	ldr	r0, [r2, #876]
	cmp	r0, #0
	bxeq	lr
	ldr	r3, [r2, #-1356]
	sub	r0, r0, r3
	ldr	r3, .L2546+4
	asr	r0, r0, #1
	mul	r0, r3, r0
	ldr	r3, [r2, #-1412]
	uxth	r0, r0
	lsl	r0, r0, #1
	ldrh	r0, [r3, r0]
	bx	lr
.L2547:
	.align	2
.L2546:
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
	ldr	r1, .L2560
	ldr	r3, [r1, #876]
	cmp	r3, #0
	beq	.L2554
	add	r2, r1, #880
	push	{r4, r5, lr}
	.save {r4, r5, lr}
	ldrh	r2, [r2]
	mov	r4, #6
	movw	r5, #65535
	ldr	ip, [r1, #-1356]
	rsb	r2, r2, r2, lsl #3
	sub	r3, r3, ip
	asr	r2, r2, #3
	asr	r3, r3, #1
	cmp	r0, r2
	uxthgt	r0, r2
	ldr	r2, .L2560+4
	mul	r3, r2, r3
	mov	r2, #0
	uxth	r3, r3
.L2551:
	uxth	lr, r2
	cmp	r0, lr
	bls	.L2553
	mul	lr, r4, r3
	add	r2, r2, #1
	ldrh	lr, [ip, lr]
	cmp	lr, r5
	bne	.L2555
.L2553:
	ldr	r2, [r1, #-1412]
	lsl	r3, r3, #1
	ldrh	r0, [r2, r3]
	pop	{r4, r5, pc}
.L2555:
	mov	r3, lr
	b	.L2551
.L2554:
	mov	r0, r3
	bx	lr
.L2561:
	.align	2
.L2560:
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
	ldr	r7, .L2575
	add	r5, r8, #12
	.pad #36
	sub	sp, sp, #36
	ldr	r1, .L2575+4
	bl	strcpy
	ldr	r2, [r7, #-2768]
	mov	r0, r5
	ldr	r1, .L2575+8
	bl	sprintf
	add	r5, r5, r0
	ldr	r2, [r7, #-1648]
	mov	r0, r5
	ldr	r1, .L2575+12
	bl	sprintf
	ldr	r3, .L2575+16
	add	r5, r5, r0
	ldr	r3, [r3, #3440]
	cmp	r3, #1
	subne	r0, r5, r8
	bne	.L2562
	add	r3, sp, #28
	add	r2, sp, #24
	add	r1, sp, #20
	add	r0, sp, #16
	bl	NandcGetTimeCfg
	ldr	r3, [sp, #28]
	mov	r0, r5
	ldr	r2, [sp, #16]
	sub	r4, r7, #1344
	ldr	r1, .L2575+20
	add	r9, r7, #880
	str	r3, [sp, #4]
	ldr	r3, [sp, #24]
	str	r3, [sp]
	ldr	r3, [sp, #20]
	bl	sprintf
	add	r6, r5, r0
	ldr	r1, .L2575+24
	mov	r0, r6
	add	r6, r6, #10
	bl	strcpy
	ldr	r2, [r7, #-1284]
	mov	r0, r6
	ldr	r1, .L2575+28
	add	r5, r7, #816
	bl	sprintf
	add	r6, r6, r0
	ldr	r2, [r7, #1124]
	ldr	r1, .L2575+32
	mov	r0, r6
	bl	sprintf
	add	r6, r6, r0
	ldr	r2, [r7, #-1584]
	ldr	r1, .L2575+36
	mov	r0, r6
	bl	sprintf
	add	r6, r6, r0
	ldr	r2, [r7, #-1596]
	ldr	r1, .L2575+40
	mov	r0, r6
	bl	sprintf
	add	r6, r6, r0
	ldr	r2, [r7, #-1600]
	ldr	r1, .L2575+44
	mov	r0, r6
	bl	sprintf
	add	r6, r6, r0
	ldr	r2, [r7, #-1592]
	ldr	r1, .L2575+48
	mov	r0, r6
	bl	sprintf
	add	r6, r6, r0
	ldr	r2, [r7, #-1588]
	ldr	r1, .L2575+52
	mov	r0, r6
	bl	sprintf
	add	r6, r6, r0
	ldr	r2, [r7, #-1604]
	ldr	r1, .L2575+56
	mov	r0, r6
	bl	sprintf
	ldr	r2, [r7, #1716]
	add	r6, r6, r0
	ldr	r1, .L2575+60
	mov	r0, r6
	lsr	r2, r2, #11
	bl	sprintf
	ldr	r2, [r7, #1720]
	add	r6, r6, r0
	ldr	r1, .L2575+64
	mov	r0, r6
	lsr	r2, r2, #11
	bl	sprintf
	add	r6, r6, r0
	ldr	r2, [r7, #-1612]
	ldr	r1, .L2575+68
	mov	r0, r6
	bl	sprintf
	add	r6, r6, r0
	ldr	r2, [r7, #-1608]
	ldr	r1, .L2575+72
	mov	r0, r6
	bl	sprintf
	add	r6, r6, r0
	bl	FtlBbtCalcTotleCnt
	ldrh	r2, [r4, #2]
	mov	r3, r0
	ldr	r1, .L2575+76
	mov	r0, r6
	bl	sprintf
	add	r6, r6, r0
	ldrh	r2, [r9]
	ldr	r1, .L2575+80
	mov	r0, r6
	bl	sprintf
	add	r6, r6, r0
	ldr	r2, [r7, #-1580]
	ldr	r1, .L2575+84
	mov	r0, r6
	bl	sprintf
	add	r6, r6, r0
	ldr	r2, [r7, #-1576]
	ldr	r1, .L2575+88
	mov	r0, r6
	bl	sprintf
	add	r6, r6, r0
	ldr	r2, [r7, #1728]
	ldr	r1, .L2575+92
	mov	r0, r6
	bl	sprintf
	add	r6, r6, r0
	ldr	r2, [r7, #-1572]
	ldr	r1, .L2575+96
	mov	r0, r6
	bl	sprintf
	add	r6, r6, r0
	ldr	r2, [r7, #-1568]
	ldr	r1, .L2575+100
	mov	r0, r6
	bl	sprintf
	add	r6, r6, r0
	ldr	r2, [r7, #-1564]
	ldr	r1, .L2575+104
	mov	r0, r6
	bl	sprintf
	add	r6, r6, r0
	ldrh	r2, [r5, #30]
	ldr	r1, .L2575+108
	mov	r0, r6
	bl	sprintf
	add	r6, r6, r0
	ldrh	r2, [r5, #28]
	ldr	r1, .L2575+112
	mov	r0, r6
	bl	sprintf
	add	r6, r6, r0
	ldr	r2, [r7, #-2736]
	ldr	r1, .L2575+116
	mov	r0, r6
	bl	sprintf
	add	r6, r6, r0
	ldr	r2, [r7, #-1632]
	ldr	r1, .L2575+120
	mov	r0, r6
	bl	sprintf
	add	r6, r6, r0
	ldr	r2, [r7, #-1736]
	ldr	r1, .L2575+124
	mov	r0, r6
	bl	sprintf
	add	r6, r6, r0
	ldrh	r2, [r4, #110]
	ldr	r1, .L2575+128
	mov	r0, r6
	bl	sprintf
	sub	r3, r7, #1712
	add	r6, r6, r0
	ldrh	r2, [r3, #-12]
	mov	r0, r6
	ldr	r1, .L2575+132
	movw	r5, #1156
	bl	sprintf
	add	r3, r7, #1152
	add	r6, r6, r0
	ldrh	r2, [r3]
	mov	r0, r6
	ldr	r1, .L2575+136
	bl	sprintf
	add	r6, r6, r0
	ldr	r2, [r7, #-1720]
	ldr	r1, .L2575+140
	mov	r0, r6
	bl	sprintf
	movw	r3, #1128
	add	r6, r6, r0
	ldrh	r2, [r7, r3]
	mov	r0, r6
	ldr	r1, .L2575+144
	bl	sprintf
	add	r6, r6, r0
	ldrh	r2, [r4, #-4]
	ldr	r1, .L2575+148
	mov	r0, r6
	bl	sprintf
	add	r4, r7, #884
	add	r6, r6, r0
	ldrh	r2, [r4, #2]
	mov	r0, r6
	ldr	r1, .L2575+152
	bl	sprintf
	add	r6, r6, r0
	ldrb	r2, [r7, #890]	@ zero_extendqisi2
	ldr	r1, .L2575+156
	mov	r0, r6
	bl	sprintf
	add	r6, r6, r0
	ldrh	r2, [r4]
	ldr	r1, .L2575+160
	mov	r0, r6
	bl	sprintf
	add	r6, r6, r0
	ldrb	r2, [r7, #892]	@ zero_extendqisi2
	ldr	r1, .L2575+164
	mov	r0, r6
	bl	sprintf
	add	r6, r6, r0
	ldrh	r2, [r4, #4]
	ldr	r1, .L2575+168
	mov	r0, r6
	bl	sprintf
	ldrh	r3, [r4]
	add	r6, r6, r0
	ldr	r2, [r7, #-1404]
	mov	r0, r6
	ldr	r1, .L2575+172
	add	r4, r7, #932
	lsl	r3, r3, #1
	ldrh	r2, [r2, r3]
	bl	sprintf
	add	r6, r6, r0
	ldrh	r2, [r4, #2]
	ldr	r1, .L2575+176
	mov	r0, r6
	bl	sprintf
	add	r6, r6, r0
	ldrb	r2, [r7, #938]	@ zero_extendqisi2
	ldr	r1, .L2575+180
	mov	r0, r6
	bl	sprintf
	add	r6, r6, r0
	ldrh	r2, [r4]
	ldr	r1, .L2575+184
	mov	r0, r6
	bl	sprintf
	add	r6, r6, r0
	ldrb	r2, [r7, #940]	@ zero_extendqisi2
	ldr	r1, .L2575+188
	mov	r0, r6
	bl	sprintf
	add	r6, r6, r0
	ldrh	r2, [r4, #4]
	ldr	r1, .L2575+192
	mov	r0, r6
	bl	sprintf
	ldrh	r3, [r4]
	add	r6, r6, r0
	ldr	r2, [r7, #-1404]
	mov	r0, r6
	ldr	r1, .L2575+196
	add	r4, r7, #980
	lsl	r3, r3, #1
	ldrh	r2, [r2, r3]
	bl	sprintf
	add	r6, r6, r0
	ldrh	r2, [r4, #2]
	ldr	r1, .L2575+200
	mov	r0, r6
	bl	sprintf
	add	r6, r6, r0
	ldrb	r2, [r7, #986]	@ zero_extendqisi2
	ldr	r1, .L2575+204
	mov	r0, r6
	bl	sprintf
	add	r6, r6, r0
	ldrh	r2, [r4]
	ldr	r1, .L2575+208
	mov	r0, r6
	bl	sprintf
	add	r6, r6, r0
	ldrb	r2, [r7, #988]	@ zero_extendqisi2
	ldr	r1, .L2575+212
	mov	r0, r6
	bl	sprintf
	add	r6, r6, r0
	ldrh	r2, [r4, #4]
	ldr	r1, .L2575+216
	mov	r0, r6
	bl	sprintf
	add	r6, r6, r0
	ldrh	r2, [r4, #178]
	ldr	r1, .L2575+220
	mov	r0, r6
	bl	sprintf
	add	r6, r6, r0
	ldrb	r2, [r7, #1162]	@ zero_extendqisi2
	ldr	r1, .L2575+224
	mov	r0, r6
	bl	sprintf
	add	r6, r6, r0
	ldrh	r2, [r7, r5]
	ldr	r1, .L2575+228
	mov	r0, r6
	bl	sprintf
	add	r6, r6, r0
	ldrb	r2, [r7, #1164]	@ zero_extendqisi2
	ldr	r1, .L2575+232
	mov	r0, r6
	bl	sprintf
	add	r6, r6, r0
	ldrh	r2, [r4, #180]
	ldr	r1, .L2575+236
	mov	r0, r6
	bl	sprintf
	ldr	r3, [r7, #1280]
	add	r6, r6, r0
	ldr	r1, [r7, #-1616]
	mov	r0, r6
	ldr	r2, [r7, #-1868]
	str	r3, [sp, #4]
	ldr	r3, [r7, #1288]
	orr	r2, r2, r1, lsl #8
	ldr	r1, .L2575+240
	str	r3, [sp]
	ldr	r3, [r7, #1284]
	bl	sprintf
	add	r4, r6, r0
	ldr	r2, [r7, #1276]
	ldr	r1, .L2575+244
	mov	r0, r4
	bl	sprintf
	add	r4, r4, r0
	ldr	r2, [r7, #1300]
	ldr	r1, .L2575+248
	mov	r0, r4
	bl	sprintf
	sub	r6, r7, #1520
	add	r4, r4, r0
	ldrh	r2, [r6, #-8]
	mov	r0, r4
	ldr	r1, .L2575+252
	bl	sprintf
	add	r4, r4, r0
	ldrh	r2, [r6, #-6]
	ldr	r1, .L2575+256
	mov	r0, r4
	bl	sprintf
	add	r4, r4, r0
	ldr	r2, [r7, #-1544]
	ldr	r1, .L2575+260
	mov	r0, r4
	bl	sprintf
	add	r4, r4, r0
	ldrh	r2, [r6, #-4]
	ldr	r1, .L2575+264
	mov	r0, r4
	bl	sprintf
	add	r4, r4, r0
	bl	GetFreeBlockMinEraseCount
	ldr	r1, .L2575+268
	mov	r2, r0
	mov	r0, r4
	bl	sprintf
	add	r4, r4, r0
	ldrh	r0, [r9]
	bl	GetFreeBlockMaxEraseCount
	ldr	r1, .L2575+272
	mov	r2, r0
	mov	r0, r4
	bl	sprintf
	ldrh	r3, [r7, r5]
	movw	r2, #65535
	add	r4, r4, r0
	cmp	r3, r2
	beq	.L2565
	ldr	r2, [r7, #-1404]
	lsl	r3, r3, #1
	mov	r0, r4
	ldr	r1, .L2575+276
	ldrh	r2, [r2, r3]
	bl	sprintf
	add	r4, r4, r0
.L2565:
	mov	r0, #0
	ldr	r9, .L2575+280
	bl	List_get_gc_head_node
	uxth	r3, r0
	mov	r5, #0
	movw	fp, #65535
	mov	r10, #6
.L2567:
	cmp	r3, fp
	beq	.L2566
	ldr	r2, [r7, #-1412]
	lsl	r1, r3, #1
	mul	r6, r10, r3
	mov	r0, r4
	ldrh	r2, [r2, r1]
	str	r2, [sp, #8]
	ldr	r2, [r7, #-1356]
	add	r2, r2, r6
	ldrh	r2, [r2, #4]
	str	r2, [sp, #4]
	ldr	r2, [r7, #-1404]
	ldrh	r2, [r2, r1]
	mov	r1, r9
	str	r2, [sp]
	mov	r2, r5
	bl	sprintf
	add	r5, r5, #1
	ldr	r3, [r7, #-1356]
	cmp	r5, #16
	add	r4, r4, r0
	ldrh	r3, [r3, r6]
	bne	.L2567
.L2566:
	ldr	r2, [r7, #-1356]
	mov	r5, #0
	ldr	r3, [r7, #876]
	movw	r9, #65535
	ldr	fp, .L2575+284
	mov	r10, #6
	sub	r3, r3, r2
	ldr	r2, .L2575+288
	asr	r3, r3, #1
	mul	r3, r2, r3
	uxth	r3, r3
.L2569:
	cmp	r3, r9
	beq	.L2568
	ldr	r1, [r7, #-1412]
	lsl	r2, r3, #1
	mul	r6, r10, r3
	mov	r0, r4
	ldrh	r2, [r1, r2]
	mov	r1, fp
	str	r2, [sp, #4]
	ldr	r2, [r7, #-1356]
	add	r2, r2, r6
	ldrh	r2, [r2, #4]
	str	r2, [sp]
	mov	r2, r5
	add	r5, r5, #1
	bl	sprintf
	cmp	r5, #4
	add	r4, r4, r0
	ldrne	r3, [r7, #-1356]
	ldrhne	r3, [r3, r6]
	bne	.L2569
.L2568:
	sub	r0, r4, r8
.L2562:
	add	sp, sp, #36
	@ sp needed
	pop	{r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L2576:
	.align	2
.L2575:
	.word	.LANCHOR2
	.word	.LC75
	.word	.LC76
	.word	.LC77
	.word	.LANCHOR1
	.word	.LC78
	.word	.LC79
	.word	.LC80
	.word	.LC81
	.word	.LC82
	.word	.LC83
	.word	.LC84
	.word	.LC85
	.word	.LC86
	.word	.LC87
	.word	.LC88
	.word	.LC89
	.word	.LC90
	.word	.LC91
	.word	.LC92
	.word	.LC93
	.word	.LC94
	.word	.LC95
	.word	.LC96
	.word	.LC97
	.word	.LC98
	.word	.LC99
	.word	.LC100
	.word	.LC101
	.word	.LC102
	.word	.LC103
	.word	.LC104
	.word	.LC105
	.word	.LC106
	.word	.LC107
	.word	.LC108
	.word	.LC109
	.word	.LC110
	.word	.LC111
	.word	.LC112
	.word	.LC113
	.word	.LC114
	.word	.LC115
	.word	.LC116
	.word	.LC117
	.word	.LC118
	.word	.LC119
	.word	.LC120
	.word	.LC121
	.word	.LC122
	.word	.LC123
	.word	.LC124
	.word	.LC125
	.word	.LC126
	.word	.LC127
	.word	.LC128
	.word	.LC129
	.word	.LC130
	.word	.LC131
	.word	.LC132
	.word	.LC133
	.word	.LC134
	.word	.LC135
	.word	.LC136
	.word	.LC137
	.word	.LC138
	.word	.LC139
	.word	.LC140
	.word	.LC141
	.word	.LC142
	.word	.LC143
	.word	.LC144
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
	ldr	r2, .L2579
	ldr	r1, .L2579+4
	bl	sprintf
	add	r4, r5, r0
	mov	r0, r4
	bl	FtlPrintInfo2buf
	add	r0, r4, r0
	sub	r0, r0, r5
	pop	{r4, r5, r6, pc}
.L2580:
	.align	2
.L2579:
	.word	.LC145
	.word	.LC49
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
	ldr	r4, .L2609
	ldr	r2, [r4, #1728]
	ldr	r3, [r4, #-1564]
	cmp	r2, r3
	bcs	.L2582
	sub	r2, r4, #1712
	mov	r3, #0
	ldrh	r1, [r2, #-12]
	ldr	r2, [r4, #-1412]
	str	r3, [r4, #-1580]
	sub	r2, r2, #2
.L2583:
	cmp	r3, r1
	bcc	.L2584
	ldr	r5, [r4, #-1580]
	mov	r0, r5
	bl	__aeabi_uidiv
	ldr	r3, .L2609+4
	str	r0, [r4, #1728]
	ldr	r0, [r4, #-1576]
	ldrh	r1, [r3, #-10]
	sub	r0, r5, r0
	bl	__aeabi_uidiv
	str	r0, [r4, #-1580]
.L2585:
	ldr	r5, [r4, #-1564]
	ldr	r8, [r4, #1728]
	add	r3, r5, #256
	cmp	r3, r8
	bls	.L2590
	ldr	r2, [r4, #-1568]
	add	r3, r5, #768
	cmp	r3, r2
	bls	.L2590
	ldr	r2, [r4, #-1868]
	cmp	r5, #40
	movls	r3, #0
	movhi	r3, #1
	cmp	r2, #0
	orreq	r3, r3, #1
	cmp	r3, #0
	beq	.L2590
.L2592:
	movw	r6, #65535
.L2591:
	mov	r0, r6
	add	sp, sp, #28
	@ sp needed
	pop	{r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L2584:
	ldrh	r0, [r2, #2]!
	add	r3, r3, #1
	ldr	ip, [r4, #-1580]
	add	r0, r0, ip
	str	r0, [r4, #-1580]
	b	.L2583
.L2582:
	ldr	r3, [r4, #-1568]
	cmp	r2, r3
	addhi	r3, r3, #1
	strhi	r3, [r4, #-1568]
	movhi	r3, #0
	bls	.L2585
.L2587:
	ldr	r2, .L2609+8
	ldrh	r2, [r2]
	cmp	r3, r2
	bcs	.L2585
	ldr	r0, [r4, #-1412]
	lsl	r1, r3, #1
	add	r3, r3, #1
	ldrh	r2, [r0, r1]
	add	r2, r2, #1
	strh	r2, [r0, r1]	@ movhi
	b	.L2587
.L2590:
	ldr	r6, .L2609+12
	ldrh	r0, [r6]
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
	bne	.L2592
	ldr	r3, [r4, #864]
	cmp	r3, #0
	beq	.L2592
	sub	r6, r6, #2592
	ldr	ip, [r4, #-1356]
	ldrh	r2, [r6, #-12]
	movw	r7, #65535
	ldr	r9, [r4, #-1412]
	mov	fp, #6
	ldr	lr, .L2609+16
	str	r2, [sp, #20]
	mov	r2, r7
.L2593:
	ldrh	r0, [r3]
	movw	r6, #65535
	cmp	r0, r6
	bne	.L2596
	mov	r6, r2
.L2595:
	movw	r3, #65535
	cmp	r6, r3
	beq	.L2592
	lsl	fp, r6, #1
	ldrh	r1, [r9, fp]
	cmp	r5, r1
	bcs	.L2597
	bl	GetFreeBlockMinEraseCount
	cmp	r5, r0
	strcc	r7, [r4, #-1564]
.L2597:
	cmp	r8, r1
	bls	.L2592
	add	r3, r1, #128
	cmp	r10, r3
	ble	.L2592
	add	r3, r1, #256
	cmp	r8, r3
	bhi	.L2598
	ldr	r3, [r4, #-1568]
	add	r1, r1, #768
	cmp	r1, r3
	bcs	.L2592
.L2598:
	str	r10, [sp, #8]
	mov	r2, r8
	ldrh	r3, [r9, fp]
	mov	r1, r6
	ldr	r0, .L2609+20
	str	r3, [sp, #4]
	ldr	r3, [r4, #-1404]
	ldrh	r3, [r3, fp]
	str	r3, [sp]
	ldr	r3, [r4, #-1568]
	bl	printk
	mov	r3, #1
	str	r3, [r4, #-1556]
	b	.L2591
.L2596:
	add	r1, r1, #1
	ldr	r6, [sp, #20]
	uxth	r1, r1
	cmp	r1, r6
	bhi	.L2592
	ldrh	r6, [r3, #4]
	cmp	r6, #0
	beq	.L2594
	sub	r3, r3, ip
	asr	r3, r3, #1
	mul	r3, lr, r3
	uxth	r6, r3
	lsl	r3, r6, #1
	ldrh	r3, [r9, r3]
	cmp	r5, r3
	bcs	.L2595
	cmp	r7, r3
	movhi	r7, r3
	movhi	r2, r6
.L2594:
	mla	r3, fp, r0, ip
	b	.L2593
.L2610:
	.align	2
.L2609:
	.word	.LANCHOR2
	.word	.LANCHOR2-1664
	.word	.LANCHOR2-1724
	.word	.LANCHOR2+880
	.word	-1431655765
	.word	.LC146
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
	beq	.L2614
	ldr	r2, .L2617
	lsl	r3, r0, #1
	push	{r4, lr}
	.save {r4, lr}
	mov	r1, #0
	ldr	r2, [r2, #-1404]
	strh	r1, [r2, r3]	@ movhi
	bl	INSERT_FREE_LIST
	mov	r0, #0
	pop	{r4, pc}
.L2614:
	mov	r0, #0
	bx	lr
.L2618:
	.align	2
.L2617:
	.word	.LANCHOR2
	.fnend
	.size	free_data_superblock, .-free_data_superblock
	.align	2
	.global	allocate_data_superblock
	.syntax unified
	.arm
	.fpu softvfp
	.type	allocate_data_superblock, %function
allocate_data_superblock:
	.fnstart
	@ args = 0, pretend = 0, frame = 16
	@ frame_needed = 0, uses_anonymous_args = 0
	push	{r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.save {r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.pad #20
	sub	sp, sp, #20
	ldr	r4, .L2669
	ldr	r3, [r4, #-1280]
	cmp	r3, #0
	bne	.L2620
	mov	r5, r0
.L2621:
	ldr	r3, .L2669+4
	ldrb	r2, [r5, #8]	@ zero_extendqisi2
	cmp	r5, r3
	sub	r7, r3, #100
	sub	r10, r7, #2592
	bne	.L2622
	ldrh	r3, [r7]
	ldr	ip, [r4, #-1556]
	lsr	r0, r3, #1
	mul	lr, ip, r3
	add	r1, r0, #1
	add	r1, r1, lr, lsr #2
	ldr	lr, [r4, #-1868]
	uxth	r1, r1
	cmp	lr, #0
	beq	.L2623
	ldr	lr, [r4, #-1564]
	cmp	lr, #39
	bhi	.L2623
	cmp	lr, #2
	bls	.L2649
	cmp	ip, #0
	movne	r3, #0
	andeq	r3, r3, #1
	cmp	r3, #0
	moveq	r1, r0
	beq	.L2623
.L2649:
	mov	r1, #0
	b	.L2624
.L2622:
	cmp	r2, #1
	bne	.L2649
	ldrh	r3, [r10]
	cmp	r3, #1
	beq	.L2649
	ldrb	r3, [r4, #-2740]	@ zero_extendqisi2
	cmp	r3, #0
	bne	.L2649
	ldr	r0, [r4, #-1868]
	ldrh	r3, [r7]
	cmp	r0, #0
	lsr	r1, r3, #3
	beq	.L2623
	ldr	r0, [r4, #-1564]
	cmp	r0, #1
	rsbls	r3, r3, r3, lsl #3
	ubfxls	r1, r3, #3, #16
.L2623:
	cmp	r1, #0
	subne	r1, r1, #1
	uxthne	r1, r1
.L2624:
	ldr	r0, .L2669+8
	bl	List_pop_index_node
	ldrh	r3, [r7]
	uxth	r8, r0
	sub	r3, r3, #1
	strh	r3, [r7]	@ movhi
	ldrh	r3, [r10, #-12]
	cmp	r3, r8
	bls	.L2621
	ldr	r3, [r4, #-1404]
	lsl	r6, r8, #1
	ldrh	r7, [r3, r6]
	cmp	r7, #0
	bne	.L2621
	strh	r8, [r5]	@ movhi
	mov	r0, r5
	bl	make_superblock
	ldrb	r3, [r5, #7]	@ zero_extendqisi2
	cmp	r3, #0
	beq	.L2666
	ldr	r2, .L2669+12
	add	r9, r5, #16
	ldr	r0, [r4, #-1488]
	mov	ip, r9
	mov	lr, r7
	ldrh	r1, [r2]
	mov	r2, #36
	mov	r3, r0
	mla	r1, r2, r1, r0
	str	r1, [sp, #4]
.L2627:
	ldr	r1, [sp, #4]
	cmp	r1, r3
	bne	.L2629
	ldr	r3, [r4, #-1868]
	ldr	r2, .L2669+16
	adds	r3, r3, #0
	movne	r3, #1
	cmp	r5, r2
	movne	r3, #0
	cmp	r3, #0
	beq	.L2630
	ldr	r3, [r4, #-1412]
	ldrh	r3, [r3, r6]
	cmp	r3, #40
	movhi	r3, #0
	strbhi	r3, [r4, #892]
.L2630:
	ldrb	r3, [r5, #8]	@ zero_extendqisi2
	ldr	r2, [r4, #-1412]
	ldr	fp, .L2669+20
	cmp	r3, #0
	ldrh	r3, [r2, r6]
	bne	.L2631
	cmp	r3, #0
	mov	r0, r8
	ldrhne	r1, [fp, #-10]
	moveq	r3, #2
	addne	r3, r3, r1
	mov	r1, #0
	strh	r3, [r2, r6]	@ movhi
	ldr	r3, [r4, #-1580]
	add	r3, r3, #1
	str	r3, [r4, #-1580]
	bl	ftl_set_blk_mode
.L2634:
	ldr	r3, [r4, #-1412]
	ldr	r2, [r4, #-1568]
	ldr	r0, [r4, #-1580]
	ldrh	r3, [r3, r6]
	ldrh	r1, [r10, #-12]
	cmp	r3, r2
	ldrh	r2, [fp, #-10]
	strhi	r3, [r4, #-1568]
	ldr	r3, [r4, #-1576]
	mla	r0, r0, r2, r3
	bl	__aeabi_uidiv
	ldr	r2, [r4, #-1416]
	ldr	r1, [r4, #-1488]
	str	r0, [r4, #1728]
	ldr	r3, [r2, #16]
	add	r3, r3, #1
	str	r3, [r2, #16]
	mov	r2, #36
	mla	r2, r2, r7, r1
	add	r3, r1, #4
	add	r2, r2, #40
.L2636:
	add	r3, r3, #36
	cmp	r2, r3
	bne	.L2637
	ldrb	r3, [r4, #-2740]	@ zero_extendqisi2
	cmp	r3, #0
	beq	.L2638
	ldrb	r3, [r5, #8]	@ zero_extendqisi2
	mov	r2, r7
	ldr	r0, [r4, #-1488]
	cmp	r3, #1
	moveq	r1, #0
	movne	r1, #1
	bl	FlashEraseBlocks
.L2638:
	ldrb	r1, [r5, #8]	@ zero_extendqisi2
	mov	r2, r7
	ldr	r0, [r4, #-1488]
	mov	r10, #0
	bl	FlashEraseBlocks
	mov	r3, r10
	mov	r1, #36
.L2640:
	uxth	r2, r10
	cmp	r7, r2
	bhi	.L2642
	cmp	r3, #0
	ble	.L2643
	mov	r0, r8
	bl	update_multiplier_value
	bl	FtlBbmTblFlush
.L2643:
	ldrb	r2, [r5, #7]	@ zero_extendqisi2
	cmp	r2, #0
	bne	.L2644
.L2666:
	ldr	r3, [r4, #-1404]
	mvn	r2, #0
	strh	r2, [r3, r6]	@ movhi
	b	.L2621
.L2629:
	str	lr, [r3, #8]
	movw	fp, #65535
	str	lr, [r3, #12]
	add	r3, r3, #36
	ldrh	r1, [ip], #2
	cmp	r1, fp
	mlane	fp, r2, r7, r0
	lslne	r1, r1, #10
	addne	r7, r7, #1
	uxthne	r7, r7
	strne	r1, [fp, #4]
	b	.L2627
.L2631:
	add	r3, r3, #1
	mov	r0, r8
	strh	r3, [r2, r6]	@ movhi
	ldr	r3, [r4, #-1576]
	add	r3, r3, #1
	str	r3, [r4, #-1576]
	bl	ftl_set_blk_mode.part.17
	b	.L2634
.L2637:
	ldr	r1, [r3, #-36]
	bic	r1, r1, #1020
	bic	r1, r1, #3
	str	r1, [r3, #-36]
	b	.L2636
.L2642:
	mul	r2, r1, r10
	ldr	r0, [r4, #-1488]
	add	ip, r0, r2
	ldr	r2, [r0, r2]
	cmn	r2, #1
	bne	.L2641
	ldr	r0, [ip, #4]
	add	r3, r3, #1
	str	r1, [sp, #12]
	str	r2, [sp, #8]
	ubfx	r0, r0, #10, #16
	str	r3, [sp, #4]
	bl	FtlBbmMapBadBlock
	ldr	r2, [sp, #8]
	ldr	r1, [sp, #12]
	ldr	r3, [sp, #4]
	strh	r2, [r9]	@ movhi
	ldrb	r2, [r5, #7]	@ zero_extendqisi2
	sub	r2, r2, #1
	strb	r2, [r5, #7]
.L2641:
	add	r10, r10, #1
	add	r9, r9, #2
	b	.L2640
.L2644:
	ldrh	r3, [fp, #-2]
	strh	r8, [r5]	@ movhi
	smulbb	r3, r3, r2
	mov	r2, #0
	strh	r2, [r5, #2]	@ movhi
	strb	r2, [r5, #6]
	ldr	r2, [r4, #-1612]
	uxth	r3, r3
	ldr	r1, [r4, #-1404]
	strh	r3, [r5, #4]	@ movhi
	str	r2, [r5, #12]
	add	r2, r2, #1
	str	r2, [r4, #-1612]
	ldrh	r2, [r5]
	lsl	r2, r2, #1
	strh	r3, [r1, r2]	@ movhi
.L2620:
	mov	r0, #0
	add	sp, sp, #20
	@ sp needed
	pop	{r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L2670:
	.align	2
.L2669:
	.word	.LANCHOR2
	.word	.LANCHOR2+980
	.word	.LANCHOR2+876
	.word	.LANCHOR2-1732
	.word	.LANCHOR2+884
	.word	.LANCHOR2-1664
	.fnend
	.size	allocate_data_superblock, .-allocate_data_superblock
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
	ldr	r2, .L2677
	mov	ip, #12
	push	{r4, r5, r6, r7, r8, lr}
	.save {r4, r5, r6, r7, r8, lr}
	mov	lr, #1
	mov	r4, #36
	mov	r3, #0
	str	r3, [r2, #1732]
.L2672:
	ldr	r8, .L2677+4
	uxth	r5, r3
	add	r0, r3, #1
	ldrh	r1, [r8]
	cmp	r5, r1
	bcc	.L2673
	mov	ip, #12
	mov	lr, #0
.L2674:
	ldr	r3, [r2, #-1476]
	cmp	r1, r3
	bcc	.L2675
	pop	{r4, r5, r6, r7, r8, pc}
.L2673:
	uxth	r3, r3
	ldr	r6, [r2, #-1444]
	mul	r5, ip, r3
	add	r1, r6, r5
	str	lr, [r1, #8]
	ldrh	r1, [r8, #76]
	mul	r1, r3, r1
	add	r7, r1, #3
	cmp	r1, #0
	movlt	r1, r7
	ldr	r7, [r2, #-1460]
	bic	r1, r1, #3
	add	r1, r7, r1
	str	r1, [r6, r5]
	ldrh	r1, [r8, #78]
	ldr	r7, [r2, #-1444]
	mul	r1, r3, r1
	add	r6, r7, r5
	add	r8, r1, #3
	cmp	r1, #0
	movlt	r1, r8
	ldr	r8, [r2, #-1432]
	bic	r1, r1, #3
	add	r1, r8, r1
	str	r1, [r6, #4]
	ldr	r1, [r2, #-1484]
	mla	r3, r4, r3, r1
	ldr	r1, [r7, r5]
	str	r1, [r3, #8]
	ldr	r1, [r6, #4]
	str	r1, [r3, #12]
	mov	r3, r0
	b	.L2672
.L2675:
	mul	r4, ip, r1
	ldr	r6, [r2, #-1444]
	ldr	r5, .L2677+8
	add	r3, r6, r4
	str	lr, [r3, #8]
	ldrh	r3, [r5]
	mul	r3, r1, r3
	add	r0, r3, #3
	cmp	r3, #0
	movlt	r3, r0
	ldr	r0, [r2, #-1460]
	bic	r3, r3, #3
	add	r3, r0, r3
	str	r3, [r6, r4]
	ldrh	r3, [r5, #2]
	ldr	r0, [r2, #-1444]
	mul	r3, r1, r3
	add	r0, r0, r4
	add	r1, r1, #1
	uxth	r1, r1
	add	r4, r3, #3
	cmp	r3, #0
	movlt	r3, r4
	ldr	r4, [r2, #-1432]
	bic	r3, r3, #3
	add	r3, r4, r3
	str	r3, [r0, #4]
	b	.L2674
.L2678:
	.align	2
.L2677:
	.word	.LANCHOR2
	.word	.LANCHOR2-1732
	.word	.LANCHOR2-1656
	.fnend
	.size	FtlGcBufInit, .-FtlGcBufInit
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
	push	{r4, r5, r6, lr}
	.save {r4, r5, r6, lr}
	mvn	r3, #0
	ldr	r4, .L2681
	movw	r2, #1740
	mov	r5, #0
	mov	r1, r5
	strh	r3, [r4, r2]	@ movhi
	sub	r6, r4, #1712
	str	r3, [r4, #1752]
	sub	r3, r4, #1280
	strh	r5, [r3, #-8]	@ movhi
	sub	r3, r4, #1632
	ldrh	r2, [r3, #-12]
	ldr	r0, [r4, #-1392]
	str	r5, [r4, #1736]
	str	r5, [r4, #1744]
	lsl	r2, r2, #1
	str	r5, [r4, #1748]
	str	r5, [r4, #-1868]
	bl	ftl_memset
	ldrh	r2, [r6, #-10]
	mov	r1, r5
	ldr	r0, [r4, #-1412]
	lsl	r2, r2, #1
	bl	ftl_memset
	ldrh	r2, [r6, #-10]
	mov	r1, r5
	ldr	r0, [r4, #-1420]
	lsl	r2, r2, #1
	bl	ftl_memset
	mov	r1, r5
	mov	r2, #48
	add	r0, r4, #816
	bl	ftl_memset
	add	r0, r4, #1200
	mov	r2, #512
	mov	r1, r5
	add	r0, r0, #4
	bl	ftl_memset
	bl	FtlGcBufInit
	bl	FtlL2PDataInit
	mov	r0, r5
	pop	{r4, r5, r6, pc}
.L2682:
	.align	2
.L2681:
	.word	.LANCHOR2
	.fnend
	.size	FtlVariablesInit, .-FtlVariablesInit
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
	ldr	r3, .L2691
	push	{r4, r5, r6, r7, r8, r9, r10, lr}
	.save {r4, r5, r6, r7, r8, r9, r10, lr}
	mov	lr, #0
	mov	r5, #36
	mov	r7, #12
	mov	r8, lr
	ldr	r6, [r3, #-1476]
	ldr	r4, [r3, #-1444]
.L2684:
	uxth	r3, lr
	cmp	r1, r3
	popls	{r4, r5, r6, r7, r8, r9, r10, pc}
	mla	ip, r5, r3, r0
	mov	r2, #0
.L2685:
	uxth	r3, r2
	cmp	r6, r3
	bls	.L2686
	mul	r3, r7, r3
	add	r2, r2, #1
	ldr	r10, [r4, r3]
	add	r9, r4, r3
	ldr	r3, [ip, #8]
	cmp	r10, r3
	bne	.L2685
	str	r8, [r9, #8]
.L2686:
	add	lr, lr, #1
	b	.L2684
.L2692:
	.align	2
.L2691:
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
	ldr	r3, .L2701
	mov	ip, #0
	push	{r4, r5, r6, r7, r8, r9, lr}
	.save {r4, r5, r6, r7, r8, r9, lr}
	mov	r6, #12
	mov	r7, #1
	mov	r8, #36
	ldr	r4, [r3, #-1476]
	ldr	r5, [r3, #-1444]
.L2694:
	uxth	r2, ip
	cmp	r1, r2
	bhi	.L2698
	pop	{r4, r5, r6, r7, r8, r9, pc}
.L2698:
	mov	lr, #0
.L2695:
	uxth	r3, lr
	cmp	r4, r3
	bls	.L2696
	mla	r3, r6, r3, r5
	add	lr, lr, #1
	ldr	r9, [r3, #8]
	cmp	r9, #0
	bne	.L2695
	mla	r2, r8, r2, r0
	ldr	lr, [r3]
	str	r7, [r3, #8]
	str	lr, [r2, #8]
	ldr	r3, [r3, #4]
	str	r3, [r2, #12]
.L2696:
	add	ip, ip, #1
	b	.L2694
.L2702:
	.align	2
.L2701:
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
	ldr	r2, .L2708
	movw	r1, #1756
	ldr	r3, [r2, #-1508]
	ldrh	r2, [r2, r1]
	add	r2, r3, r2, lsl #1
.L2704:
	cmp	r3, r2
	bne	.L2706
	mov	r0, #0
	bx	lr
.L2706:
	ldrh	r1, [r3], #2
	cmp	r1, r0
	bne	.L2704
	mov	r0, #1
	bx	lr
.L2709:
	.align	2
.L2708:
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
	ldr	r3, .L2715
	movw	r2, #1756
	mov	ip, #0
	ldrh	lr, [r3, r2]
	ldr	r2, [r3, #-1508]
	sub	r1, r2, #2
.L2711:
	uxth	r7, ip
	cmp	r7, lr
	bcc	.L2713
	bne	.L2712
	lsl	ip, r7, #1
	strh	r0, [r2, ip]	@ movhi
	movw	r2, #1756
	ldrh	r0, [r3, r2]
	add	r0, r0, #1
	strh	r0, [r3, r2]	@ movhi
	b	.L2712
.L2713:
	ldrh	r7, [r1, #2]!
	add	ip, ip, #1
	cmp	r7, r0
	bne	.L2711
.L2712:
	movw	ip, #1758
	mov	r0, #12
	ldrh	r2, [r3, ip]
	mul	r0, r0, r2
	ldr	r2, [r3, #-1504]
	add	r1, r2, r0
	stmib	r1, {r5, r6}
	str	r4, [r2, r0]
	ldrh	r2, [r3, ip]
	add	r2, r2, #1
	strh	r2, [r3, ip]	@ movhi
	pop	{r4, r5, r6, r7, r8, pc}
.L2716:
	.align	2
.L2715:
	.word	.LANCHOR2
	.fnend
	.size	FtlGcUpdatePage, .-FtlGcUpdatePage
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
	ldr	r4, .L2719
	movw	r2, #1756
	mov	r1, #255
	strh	r3, [r4, r2]	@ movhi
	sub	r5, r4, #1648
	movw	r2, #1758
	ldr	r0, [r4, #-1508]
	strh	r3, [r4, r2]	@ movhi
	ldrh	r2, [r5, #-14]
	lsl	r2, r2, #1
	bl	ftl_memset
	ldrh	r3, [r5, #-14]
	mov	r2, #12
	ldr	r0, [r4, #-1504]
	mov	r1, #255
	mul	r2, r2, r3
	bl	ftl_memset
	pop	{r4, r5, r6, lr}
	b	FtlGcBufInit
.L2720:
	.align	2
.L2719:
	.word	.LANCHOR2
	.fnend
	.size	FtlGcPageVarInit, .-FtlGcPageVarInit
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
	ldr	r2, .L2770
	movw	r3, #3444
	push	{r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.save {r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.pad #68
	sub	sp, sp, #68
	mov	r5, r0
	str	r1, [sp, #8]
	ldrh	r6, [r2, r3]
	movw	r3, #65535
	cmp	r6, r3
	beq	.L2753
	cmp	r6, #0
	bne	.L2722
.L2723:
	bl	FtlGcPageVarInit
	b	.L2724
.L2753:
	mov	r6, #0
.L2722:
	ldr	r3, .L2770+4
	ldr	r2, [sp, #8]
	ldrh	r3, [r3, #-2]
	cmp	r3, r2
	beq	.L2723
.L2724:
	ldr	r4, .L2770+8
	mov	r2, #0
	mvn	r3, #0
	stm	sp, {r2, r3}
.L2725:
	ldrh	r1, [r5]
	movw	r3, #65535
	mov	r2, #0
	strb	r2, [r5, #8]
	cmp	r1, r3
	beq	.L2726
.L2750:
	ldr	r3, .L2770+12
	add	ip, r5, #16
	ldr	r0, [r4, #-1500]
	movw	r8, #65535
	ldr	fp, [r4, #-1432]
	mov	r9, #36
	ldrh	r2, [r3, #-4]
	ldrh	lr, [r3, #74]
	str	r2, [sp, #12]
	ldr	r2, [r4, #-1460]
	str	r2, [sp, #16]
	ldrh	r2, [r3, #72]
	str	r2, [sp, #20]
	mov	r2, #0
	mov	r7, r2
.L2727:
	ldr	r1, [sp, #12]
	uxth	r3, r2
	cmp	r1, r3
	bhi	.L2729
	mov	r10, #0
	mov	r2, #0
	mov	r1, r7
	bl	FlashReadPages
.L2730:
	uxth	r3, r10
	cmp	r7, r3
	bhi	.L2748
	ldr	r3, [sp]
	add	r6, r6, #1
	uxth	r6, r6
	add	r3, r3, #1
	str	r3, [sp]
	ldr	r2, [sp]
	ldr	r3, [sp, #8]
	cmp	r3, r2
	ldr	r2, .L2770+4
	bls	.L2749
.L2751:
	ldrh	r3, [r2, #-2]
	cmp	r3, r6
	bhi	.L2750
	mov	r2, #0
	b	.L2726
.L2729:
	ldrh	r3, [ip], #2
	cmp	r3, r8
	beq	.L2728
	mla	r1, r9, r7, r0
	orr	r3, r6, r3, lsl #10
	str	r3, [r1, #4]
	ldr	r3, [sp, #20]
	mul	r3, r3, r7
	add	r10, r3, #3
	cmp	r3, #0
	movlt	r3, r10
	ldr	r10, [sp, #16]
	bic	r3, r3, #3
	add	r3, r10, r3
	str	r3, [r1, #8]
	mul	r3, lr, r7
	add	r7, r7, #1
	uxth	r7, r7
	add	r10, r3, #3
	cmp	r3, #0
	movlt	r3, r10
	bic	r3, r3, #3
	add	r3, fp, r3
	str	r3, [r1, #12]
.L2728:
	add	r2, r2, #1
	b	.L2727
.L2748:
	mov	r9, #36
	ldr	r8, [r4, #-1500]
	mul	r9, r9, r10
	add	r3, r8, r9
	ldr	fp, [r3, #4]
	str	r3, [sp, #12]
	ubfx	r0, fp, #10, #16
	bl	P2V_plane
	ldr	r8, [r8, r9]
	mov	r2, r0
	ldr	r3, [sp, #12]
	cmp	r8, #0
	ldr	r3, [r3, #12]
	bne	.L2731
	ldrh	r0, [r3]
	movw	r1, #65535
	cmp	r0, r1
	bne	.L2732
.L2735:
	ldrb	r1, [r4, #-2740]	@ zero_extendqisi2
	cmp	r1, #0
	beq	.L2765
	mov	r3, #1
	str	r3, [r4, #1748]
.L2726:
	ldr	r1, .L2770
	mvn	r0, #0
	movw	r3, #3444
	strh	r6, [r5, #2]	@ movhi
	strb	r2, [r5, #6]
	strh	r0, [r1, r3]	@ movhi
	mov	r1, r6
	mov	r0, r5
	bl	ftl_sb_update_avl_pages
	b	.L2721
.L2732:
	ldr	r0, [r3, #8]
	ldr	r1, [r4, #-1284]
	cmp	r0, r1
	bhi	.L2735
	ldr	r2, .L2770+16
	ldrb	r2, [r2, #36]	@ zero_extendqisi2
	cmp	r2, #0
	bne	.L2738
.L2739:
	ldr	r2, [r3, #8]
	mov	r1, fp
	ldr	r0, [r3, #12]
	add	r10, r10, #1
	bl	FtlGcUpdatePage
	b	.L2730
.L2765:
	ldrh	r3, [r5]
	ldr	r2, [r4, #-1404]
	lsl	r3, r3, #1
.L2769:
	strh	r1, [r2, r3]	@ movhi
	ldrh	r0, [r5]
	bl	INSERT_FREE_LIST
	ldr	r2, .L2770+20
	mvn	r3, #0
	strh	r3, [r5]	@ movhi
	strh	r3, [r2]	@ movhi
.L2768:
	bl	FtlGcPageVarInit
	mov	r6, #0
	b	.L2725
.L2738:
	mov	r2, r8
	add	r1, sp, #24
	str	r3, [sp, #12]
	bl	log2phys
	ldr	r3, [sp, #12]
	ldr	r1, [sp, #24]
	ldr	r2, [r3, #12]
	cmn	r1, #1
	sub	r0, r2, r1
	clz	r0, r0
	lsr	r0, r0, #5
	moveq	r0, #0
	cmp	r0, #0
	beq	.L2739
	str	r2, [sp, #32]
	mov	r1, #1
	ldr	r2, [r4, #-1448]
	add	r0, sp, #28
	str	r2, [sp, #36]
	ldr	r2, [r4, #-1436]
	str	r2, [sp, #40]
	mov	r2, r8
	bl	FlashReadPages
	ldr	r2, .L2770+24
	ldr	r1, [r4, #-1500]
	ldr	r3, [sp, #12]
	ldrh	r2, [r2]
	add	r9, r1, r9
	ldr	r1, [sp, #36]
	lsl	r2, r2, #7
.L2740:
	cmp	r8, r2
	beq	.L2739
	ldr	r0, [r9, #8]
	ldr	ip, [r0, r8, lsl #2]
	ldr	r0, [r1, r8, lsl #2]
	cmp	ip, r0
	beq	.L2741
	ldr	r2, [sp, #32]
	ldrh	r1, [r5]
	ldr	r0, .L2770+28
	bl	printk
	ldrh	r3, [r5]
	mov	r1, #0
	ldr	r2, [r4, #-1404]
	lsl	r3, r3, #1
	b	.L2769
.L2741:
	add	r8, r8, #1
	b	.L2740
.L2731:
	mov	r2, fp
	ldrh	r1, [r5]
	ldr	r0, .L2770+32
	bl	printk
	ldr	r3, [r4, #-1868]
	cmp	r3, #0
	ldrh	r3, [r5]
	bne	.L2744
	ldrb	r2, [r4, #-2740]	@ zero_extendqisi2
	cmp	r2, #0
	beq	.L2745
.L2744:
	ldr	r1, [r4, #-1412]
	lsl	r2, r3, #1
	ldrh	r2, [r1, r2]
	cmp	r2, #159
	bls	.L2746
.L2745:
	ldr	r2, [r4, #-1500]
	ldr	r2, [r2, r9]
	cmn	r2, #1
	bne	.L2747
.L2746:
	ldr	r2, [r4, #-1500]
	add	r9, r2, r9
	ldr	r2, [r9, #4]
	str	r2, [sp, #4]
.L2747:
	ldr	r2, [r4, #-1404]
	lsl	r3, r3, #1
	mov	r1, #0
	strh	r1, [r2, r3]	@ movhi
	ldrh	r0, [r5]
	bl	INSERT_FREE_LIST
	mvn	r3, #0
	strh	r3, [r5]	@ movhi
	b	.L2768
.L2749:
	ldr	r1, .L2770+36
	movw	r0, #65535
	ldrh	r3, [r1]
	cmp	r3, r0
	beq	.L2751
	ldr	r0, [sp]
	add	r3, r3, r0
	strh	r3, [r1]	@ movhi
	ldrh	r3, [r2, #-2]
	cmp	r3, r6
	bls	.L2751
.L2721:
	ldr	r0, [sp, #4]
	add	sp, sp, #68
	@ sp needed
	pop	{r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L2771:
	.align	2
.L2770:
	.word	.LANCHOR1
	.word	.LANCHOR2-1664
	.word	.LANCHOR2
	.word	.LANCHOR2-1728
	.word	.LANCHOR0
	.word	.LANCHOR2+1156
	.word	.LANCHOR2-1660
	.word	.LC147
	.word	.LC148
	.word	.LANCHOR1+3444
	.fnend
	.size	FtlGcScanTempBlk, .-FtlGcScanTempBlk
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
	ldr	r6, .L2780
	ldrh	r3, [r6]
	cmp	r3, r0
	beq	.L2774
	add	r5, r6, #16
	ldrh	r3, [r5, #-14]
	cmp	r3, r0
	beq	.L2774
	ldrh	r3, [r5, #-12]
	cmp	r3, r0
	beq	.L2774
	ldrh	r3, [r5, #-10]
	cmp	r3, r0
	beq	.L2774
	mov	r4, r0
	mov	r1, r0
	ldr	r0, .L2780+4
	bl	printk
	ldrh	r2, [r6]
	movw	r3, #65535
	cmp	r2, r3
	strheq	r4, [r6]	@ movhi
	beq	.L2774
	ldrh	r2, [r5, #-14]
	cmp	r2, r3
	strheq	r4, [r5, #-14]	@ movhi
	beq	.L2774
	ldrh	r2, [r5, #-12]
	cmp	r2, r3
	strheq	r4, [r5, #-12]	@ movhi
	beq	.L2774
	ldrh	r2, [r5, #-10]
	cmp	r2, r3
	strheq	r4, [r5, #-10]	@ movhi
.L2774:
	mov	r0, #0
	pop	{r4, r5, r6, pc}
.L2781:
	.align	2
.L2780:
	.word	.LANCHOR2-1536
	.word	.LC149
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
	ldr	r6, .L2793
	ldrh	r3, [r6]
	cmp	r3, r0
	beq	.L2790
	add	r5, r6, #16
	ldrh	r3, [r5, #-14]
	cmp	r3, r0
	beq	.L2790
	ldrh	r3, [r5, #-12]
	cmp	r3, r0
	beq	.L2790
	ldrh	r3, [r5, #-10]
	cmp	r3, r0
	beq	.L2790
	mov	r4, r0
	mov	r1, r0
	ldr	r0, .L2793+4
	bl	printk
	ldrh	r2, [r6]
	movw	r3, #65535
	cmp	r2, r3
	strheq	r4, [r6]	@ movhi
	beq	.L2790
	ldrh	r2, [r5, #-14]
	cmp	r2, r3
	strheq	r4, [r5, #-14]	@ movhi
	beq	.L2790
	ldrh	r2, [r5, #-12]
	cmp	r2, r3
	strheq	r4, [r5, #-12]	@ movhi
	beq	.L2790
	ldrh	r2, [r5, #-10]
	cmp	r2, r3
	bne	.L2791
	strh	r4, [r5, #-10]	@ movhi
.L2790:
	mov	r0, #0
	pop	{r4, r5, r6, pc}
.L2791:
	mvn	r0, #0
	pop	{r4, r5, r6, pc}
.L2794:
	.align	2
.L2793:
	.word	.LANCHOR2-1536
	.word	.LC149
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
	ldr	r6, .L2804
	bl	P2V_block_in_plane
	sub	r7, r6, #1520
	mov	r4, r0
	mov	r2, r5
	ldrh	r1, [r7, #-2]
	ldr	r0, .L2804+4
	bl	printk
	mov	r0, r4
	bl	FtlGcRefreshBlock
	ldr	r3, [r6, #-1868]
	cmp	r3, #0
	beq	.L2796
	ldr	r2, [r6, #-1412]
	lsl	r4, r4, #1
	ldrh	r3, [r2, r4]
	cmp	r3, #39
	subhi	r3, r3, #40
	strhhi	r3, [r2, r4]	@ movhi
.L2796:
	ldrh	r3, [r7, #-2]
	mov	r2, #0
	ldr	r0, .L2804+8
.L2797:
	uxth	r1, r2
	cmp	r3, r1
	bhi	.L2799
	cmp	r3, #15
	addls	r2, r3, #1
	lslls	r3, r3, #1
	strhls	r2, [r7, #-2]	@ movhi
	ldrls	r2, .L2804+8
	strhls	r5, [r2, r3]	@ movhi
	b	.L2798
.L2799:
	add	r2, r2, #1
	add	r1, r0, r2, lsl #1
	ldrh	r1, [r1, #-2]
	cmp	r1, r5
	bne	.L2797
.L2798:
	mov	r0, #0
	pop	{r4, r5, r6, r7, r8, pc}
.L2805:
	.align	2
.L2804:
	.word	.LANCHOR2
	.word	.LC150
	.word	.LANCHOR2+1760
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
	push	{r4, lr}
	.save {r4, lr}
	ldr	r4, .L2813
	ldrh	r3, [r4, #-2]
	cmp	r3, #0
	beq	.L2807
	ldrh	r1, [r4, #-16]
	movw	r2, #65535
	cmp	r1, r2
	bne	.L2807
	add	r4, r4, #16
	ldrh	r2, [r4, #-14]
	cmp	r2, r3
	ldr	r2, .L2813+4
	movcs	r3, #0
	strhcs	r3, [r4, #-14]	@ movhi
	ldrh	r3, [r4, #-14]
	lsl	r3, r3, #1
	ldrh	r0, [r2, r3]
	bl	P2V_block_in_plane
	bl	FtlGcRefreshBlock
	ldrh	r3, [r4, #-14]
	add	r3, r3, #1
	strh	r3, [r4, #-14]	@ movhi
.L2807:
	mov	r0, #0
	pop	{r4, pc}
.L2814:
	.align	2
.L2813:
	.word	.LANCHOR2-1520
	.word	.LANCHOR2+1760
	.fnend
	.size	FtlGcReFreshBadBlk, .-FtlGcReFreshBadBlk
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
	ldr	r4, .L2828
	ldrh	r3, [r4, #-2]
	cmp	r3, #0
	beq	.L2816
	add	r9, r4, #1520
	mov	r7, #0
	add	fp, r9, #1760
	str	r0, [sp]
.L2817:
	ldr	r3, .L2828+4
	ldrh	r2, [r3, #-4]
	uxth	r3, r7
	cmp	r2, r3
	bhi	.L2823
	bl	FtlGcReFreshBadBlk
.L2816:
	mov	r0, #0
	add	sp, sp, #12
	@ sp needed
	pop	{r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L2823:
	uxtah	r3, r9, r7
	ldr	r1, [sp]
	mov	r8, #0
	ldrb	r0, [r3, #-1706]	@ zero_extendqisi2
	bl	V2P_block
	ldr	r2, .L2828+8
	mov	r10, r0
.L2818:
	ldrh	r1, [r4, #-2]
	uxth	r5, r8
	cmp	r1, r5
	addls	r7, r7, #1
	bls	.L2817
.L2822:
	uxth	r6, r8
	lsl	r1, r6, #1
	ldrh	r1, [fp, r1]
	cmp	r1, r10
	bne	.L2819
	mov	r1, r10
	mov	r0, r2
	str	r2, [sp, #4]
	add	r6, fp, r6, lsl #1
	bl	printk
	mov	r0, r10
	bl	FtlBbmMapBadBlock
	bl	FtlBbmTblFlush
	ldrh	r1, [r4, #-2]
	ldr	r2, [sp, #4]
.L2820:
	cmp	r5, r1
	bcc	.L2821
	sub	r1, r1, #1
	strh	r1, [r4, #-2]	@ movhi
.L2819:
	add	r8, r8, #1
	b	.L2818
.L2821:
	ldrh	r0, [r6, #2]!
	add	r5, r5, #1
	uxth	r5, r5
	strh	r0, [r6, #-2]	@ movhi
	b	.L2820
.L2829:
	.align	2
.L2828:
	.word	.LANCHOR2-1520
	.word	.LANCHOR2-1728
	.word	.LC151
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
	ldr	r2, .L2839
	lsl	r3, r0, #1
	push	{r4, lr}
	.save {r4, lr}
	ldr	r1, [r2, #-1404]
	ldrh	r3, [r1, r3]
	cmp	r3, #0
	bne	.L2831
	movw	r1, #1156
	mov	r4, r0
	ldrh	r0, [r2, r1]
	cmp	r0, r4
	mvneq	r3, #0
	strheq	r3, [r2, r1]	@ movhi
	beq	.L2833
	add	r1, r2, #884
	ldrh	r1, [r1]
	cmp	r1, r4
	beq	.L2830
	add	r1, r2, #932
	ldrh	r1, [r1]
	cmp	r1, r4
	beq	.L2830
	add	r2, r2, #980
	ldrh	r2, [r2]
	cmp	r2, r4
	beq	.L2830
.L2833:
	mov	r1, r4
	ldr	r0, .L2839+4
	bl	List_remove_node
	ldr	r2, .L2839+8
	mov	r0, r4
	ldrh	r3, [r2]
	sub	r3, r3, #1
	strh	r3, [r2]	@ movhi
	bl	free_data_superblock
	mov	r0, r4
	bl	FtlGcFreeBadSuperBlk
	mov	r3, #1
.L2830:
	mov	r0, r3
	pop	{r4, pc}
.L2831:
	bl	List_update_data_list
	mov	r3, #0
	b	.L2830
.L2840:
	.align	2
.L2839:
	.word	.LANCHOR2
	.word	.LANCHOR2+864
	.word	.LANCHOR2+872
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
	push	{r4, r5, r6, lr}
	.save {r4, r5, r6, lr}
	cmp	r0, r3
	mov	r4, r0
	ldr	r5, .L2852
	beq	.L2842
	ldr	r3, [r5, #-1404]
	lsl	r6, r0, #1
	ldrh	r2, [r3, r6]
	cmp	r2, #0
	subne	r2, r2, #1
	strhne	r2, [r3, r6]	@ movhi
	bne	.L2842
	mov	r1, r0
	ldr	r0, .L2852+4
	bl	printk
	ldr	r3, [r5, #-1404]
	mov	r2, #32
	mov	r1, r4
	add	r0, r5, #876
	strh	r2, [r3, r6]	@ movhi
	bl	test_node_in_list
	cmp	r0, #0
	beq	.L2844
	mov	r1, r4
	add	r0, r5, #876
	bl	List_remove_node
	add	r2, r5, #880
	mov	r0, r4
	ldrh	r3, [r2]
	sub	r3, r3, #1
	strh	r3, [r2]	@ movhi
	bl	INSERT_DATA_LIST
	ldr	r3, [r5, #-1404]
	mov	r1, r4
	ldr	r0, .L2852+8
	ldrh	r2, [r3, r6]
	bl	printk
.L2844:
	mov	r0, r4
	bl	FtlGcRefreshBlock
.L2847:
	mov	r0, #0
	pop	{r4, r5, r6, pc}
.L2842:
	movw	r6, #1740
	movw	r3, #65535
	ldrh	r0, [r5, r6]
	cmp	r0, r3
	strheq	r4, [r5, r6]	@ movhi
	beq	.L2847
	cmp	r4, r0
	beq	.L2847
	bl	update_vpc_list
	adds	r0, r0, #0
	strh	r4, [r5, r6]	@ movhi
	movne	r0, #1
	pop	{r4, r5, r6, pc}
.L2853:
	.align	2
.L2852:
	.word	.LANCHOR2
	.word	.LC152
	.word	.LC153
	.fnend
	.size	decrement_vpc_count, .-decrement_vpc_count
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
	beq	.L2999
	ldr	r2, .L3011
	push	{r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.save {r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.pad #52
	sub	sp, sp, #52
	ldrh	r3, [r0, #2]
	mov	fp, r0
	ldrh	r2, [r2, #-2]
	str	r3, [sp, #8]
	cmp	r2, r3
	mov	r2, #0
	strheq	r2, [r0, #4]	@ movhi
	strbeq	r2, [r0, #6]
	ldrhne	r0, [r0, #16]
	bne	.L2858
.L2997:
	mov	r0, #0
	add	sp, sp, #52
	@ sp needed
	pop	{r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L2859:
	uxth	r0, r2
	add	r0, fp, r0, lsl #1
	ldrh	r0, [r0, #16]
.L2858:
	cmp	r0, r1
	add	r2, r2, #1
	beq	.L2859
	ldrb	r1, [fp, #8]	@ zero_extendqisi2
	ldrb	r3, [fp, #6]	@ zero_extendqisi2
	cmp	r1, #1
	str	r3, [sp, #12]
	bne	.L2860
	bl	FtlGetLastWrittenPage
	cmn	r0, #1
	mov	r4, r0
	beq	.L2861
	ldr	r3, .L3011+4
	ldrb	r3, [r3, #-2740]	@ zero_extendqisi2
	cmp	r3, #0
	bne	.L2932
	ldr	r3, .L3011+8
	add	r3, r3, r0, lsl #1
	ldrh	r5, [r3, #84]
.L2862:
	ldr	r3, .L3011+4
	mov	r9, #36
	sub	r2, r3, #1728
	ldr	r0, [r3, #-1500]
	ldrh	r2, [r2, #-4]
	sub	r3, r3, #1648
	ldr	lr, [r3, #216]
	ldrh	r7, [r3, #-6]
	add	r3, fp, #16
	str	r2, [sp]
	mov	r2, #0
	mov	ip, r3
	mov	r6, r2
	mov	r10, r2
	str	r3, [sp, #20]
.L2863:
	ldr	r1, [sp]
	uxth	r3, r2
	cmp	r1, r3
	bhi	.L2865
	ldrb	r3, [fp, #8]	@ zero_extendqisi2
	ldr	r8, .L3011+4
	cmp	r3, #1
	movne	r3, #0
	bne	.L3002
	ldrb	r3, [r8, #-2740]	@ zero_extendqisi2
	adds	r3, r3, #0
	movne	r3, #1
.L3002:
	str	r3, [sp, #24]
	mov	r1, r6
	ldr	r2, [sp, #24]
	mov	r7, #0
	ldr	r10, .L3011+4
	movw	r9, #65535
	bl	FlashReadPages
	ldr	r3, [r8, #-1608]
	str	r3, [sp, #16]
.L2867:
	uxth	r3, r7
	cmp	r6, r3
	bhi	.L2872
	bne	.L2870
	add	r4, r4, #1
	uxth	r3, r4
	str	r3, [sp]
	ldr	r3, [r8, #-1500]
	ldr	r0, [r3, #4]
.L3003:
	ubfx	r0, r0, #10, #16
	bl	P2V_plane
	ldrb	r3, [fp, #8]	@ zero_extendqisi2
	str	r0, [sp, #4]
	cmp	r3, #1
	bne	.L2874
	ldrb	r2, [r8, #-2740]	@ zero_extendqisi2
	cmp	r2, #0
	ldreq	r2, [sp]
	ldreq	r4, .L3011+8
	addeq	r4, r4, r2, lsl #1
	ldrheq	r2, [r4, #84]
	streq	r2, [sp]
.L2874:
	ldr	r2, .L3011
	ldr	r1, [sp]
	ldrh	r2, [r2, #-2]
	cmp	r2, r1
	ldmib	sp, {r0, r1}
	ldrheq	r2, [sp]
	strheq	r2, [fp, #2]	@ movhi
	moveq	r2, #0
	strbeq	r2, [fp, #6]
	strheq	r2, [fp, #4]	@ movhi
	ldrh	r2, [sp, #12]
	str	r2, [sp, #28]
	ldr	ip, [sp, #28]
	ldr	r2, [sp]
	cmp	r2, r1
	cmpeq	r0, ip
	moveq	r2, r0
	beq	.L3010
	ldr	r2, [sp, #16]
	sub	r10, r2, #1
	movw	r2, #65535
	subs	r9, r9, r2
	movne	r9, #1
	cmp	r3, #0
	orreq	r9, r9, #1
	cmp	r9, #0
	beq	.L2878
	ldr	r3, [r8, #1752]
	uxth	r9, r5
	uxth	r5, r5
	ldr	r6, .L3011+4
	cmn	r3, #1
	streq	r10, [r8, #1752]
	ldr	r3, [r8, #1752]
	mvn	r8, #0
	mov	r7, r8
	str	r3, [sp, #12]
	ldr	r3, [sp, #8]
	add	r3, r3, #7
	cmp	r5, r3
	subgt	r4, r9, #7
	ldrle	r4, [sp, #8]
	uxthgt	r4, r4
.L2881:
	cmp	r4, r9
	bhi	.L2894
	ldr	r3, .L3011+12
	mov	ip, #36
	ldr	r0, [r6, #-1500]
	ldr	r1, [sp, #20]
	ldrh	r3, [r3]
	str	r3, [sp, #16]
	mov	r3, #0
	mov	r5, r3
	b	.L2895
.L2860:
	mov	r1, #0
	bl	FtlGetLastWrittenPage
	cmn	r0, #1
	mov	r4, r0
	beq	.L2861
.L2932:
	mov	r5, r4
	b	.L2862
.L2861:
	mov	r3, #0
	strh	r3, [fp, #2]	@ movhi
.L3009:
	strb	r3, [fp, #6]
	b	.L2997
.L2865:
	ldrh	r3, [ip], #2
	movw	r1, #65535
	cmp	r3, r1
	beq	.L2864
	mla	r1, r9, r6, r0
	orr	r3, r5, r3, lsl #10
	stmib	r1, {r3, r10}
	mul	r3, r7, r6
	add	r6, r6, #1
	uxth	r6, r6
	add	r8, r3, #3
	cmp	r3, #0
	movlt	r3, r8
	bic	r3, r3, #3
	add	r3, lr, r3
	str	r3, [r1, #12]
.L2864:
	add	r2, r2, #1
	b	.L2863
.L2872:
	mov	r3, #36
	ldr	r1, [r10, #-1500]
	mul	r3, r3, r7
	add	r2, r1, r3
	ldr	r3, [r1, r3]
	cmp	r3, #0
	bne	.L2868
	ldr	r2, [r2, #12]
	ldr	r3, [r2, #4]
	cmn	r3, #1
	beq	.L2869
	ldr	r1, [r10, #-1608]
	mov	r0, r3
	bl	ftl_cmp_data_ver
	cmp	r0, #0
	addne	r3, r3, #1
	strne	r3, [r10, #-1608]
.L2869:
	ldr	r3, [r2]
	cmn	r3, #1
	bne	.L2871
.L2870:
	uxth	r3, r4
	uxth	r7, r7
	str	r3, [sp]
	mov	r2, #36
	ldr	r3, [r8, #-1500]
	mla	r7, r2, r7, r3
	ldr	r0, [r7, #4]
	b	.L3003
.L2868:
	ldr	r1, [r2, #4]
	uxth	r9, r5
	ldr	r0, .L3011+16
	bl	printk
	ldrh	r3, [fp]
	ldr	r2, .L3011+20
	strh	r3, [r2]	@ movhi
.L2871:
	add	r7, r7, #1
	b	.L2867
.L2883:
	ldrh	r2, [r1], #2
	movw	lr, #65535
	add	r3, r3, #1
	cmp	r2, lr
	mlane	lr, ip, r5, r0
	addne	r5, r5, #1
	orrne	r2, r4, r2, lsl #10
	uxthne	r5, r5
	strne	r2, [lr, #4]
.L2895:
	ldr	lr, [sp, #16]
	uxth	r2, r3
	cmp	r2, lr
	bcc	.L2883
	mov	r1, r5
	ldr	r2, [sp, #24]
	bl	FlashReadPages
	ldr	r3, [r6, #-1500]
	mov	r2, #36
	ldrb	ip, [r6, #-2740]	@ zero_extendqisi2
	movw	r1, #65535
	mla	r5, r2, r5, r3
	ldr	r2, .L3011+24
	add	r2, r2, r4, lsl #1
.L2884:
	cmp	r5, r3
	addeq	r4, r4, #1
	uxtheq	r4, r4
	beq	.L2881
.L2893:
	ldr	r0, [r3]
	cmp	r0, #0
	bne	.L2885
	ldr	r0, [r3, #12]
	ldrh	lr, [r0]
	cmp	lr, r1
	beq	.L2886
	ldr	r0, [r0, #4]
	cmn	r0, #1
	beq	.L2886
	cmn	r8, #1
	ldr	r7, [r6, #1752]
	str	r0, [r6, #1752]
	bne	.L2886
	ldrh	r0, [r2]
	cmp	r0, r1
	bne	.L2887
	cmp	ip, #0
	beq	.L2886
.L2887:
	cmp	r10, r7
	movne	r8, r7
.L2886:
	add	r3, r3, #36
	b	.L2884
.L2885:
	ldrh	r1, [fp]
	movw	r2, #1794
	ldr	r3, .L3011+4
	strh	r1, [r3, r2]	@ movhi
	ldrb	r2, [fp, #8]	@ zero_extendqisi2
	cmp	r2, #0
	bne	.L2878
	ldr	r2, .L3011+24
	lsl	r4, r4, #1
	ldrh	r1, [r2, r4]
	movw	r2, #65535
	cmp	r1, r2
	bne	.L2889
	cmn	r8, #1
	strne	r8, [r3, #1752]
	bne	.L2878
	ldr	r2, [sp, #12]
	cmp	r10, r2
	beq	.L2891
.L3005:
	str	r2, [r3, #1752]
	b	.L2878
.L2891:
	ldr	r2, [r3, #1752]
.L3004:
	sub	r2, r2, #1
	b	.L3005
.L2889:
	cmp	r7, r10
	beq	.L2892
	cmn	r7, #1
	strne	r7, [r3, #1752]
.L2878:
	ldr	r9, [sp, #8]
	mov	r2, #1
	ldr	r4, .L3011+4
	movw	r3, #1796
	strh	r2, [r4, r3]	@ movhi
.L2896:
	ldr	r3, .L3011+28
	movw	r6, #65535
	ldr	r0, [r4, #-1500]
	mov	r7, #36
	ldrb	r5, [r4, #-2740]	@ zero_extendqisi2
	mov	r2, #0
	ldrh	lr, [r3, #-4]
	ldr	r1, [sp, #20]
	str	r2, [sp, #12]
.L2897:
	uxth	r3, r2
	cmp	lr, r3
	bhi	.L2900
	ldr	r2, [sp, #24]
	ldr	r1, [sp, #12]
	bl	FlashReadPages
	mov	r3, #0
.L3008:
	str	r3, [sp, #16]
	ldr	r2, [sp, #12]
	ldrh	r3, [sp, #16]
	cmp	r2, r3
	bhi	.L2926
	ldrb	r3, [fp, #8]	@ zero_extendqisi2
	add	r9, r9, #1
	uxth	r9, r9
	cmp	r3, #1
	ldr	r3, .L3011
	bne	.L2927
	ldrb	r2, [r4, #-2740]	@ zero_extendqisi2
	cmp	r2, #0
	beq	.L2927
	ldrh	r2, [r3]
	ldr	r1, [sp]
	cmp	r2, r9
	cmpeq	r1, r9
	beq	.L2903
.L2927:
	ldrh	r3, [r3, #-2]
	cmp	r3, r9
	bne	.L2896
	ldr	r2, .L3011+28
	movw	r0, #65535
	mov	r3, #0
	strh	r9, [fp, #2]	@ movhi
	strh	r3, [fp, #4]	@ movhi
	ldrh	r2, [r2, #-4]
.L2928:
	uxth	r1, r3
	cmp	r1, r2
	bcs	.L2997
	ldr	r1, [sp, #20]
	ldrh	ip, [r1], #2
	cmp	ip, r0
	str	r1, [sp, #20]
	add	r1, r3, #1
	bne	.L3009
	mov	r3, r1
	b	.L2928
.L2892:
	ldr	r2, [r3, #1752]
	cmp	r10, r2
	bne	.L3004
	b	.L2878
.L2894:
	mvn	r3, #0
	str	r3, [r6, #1752]
	b	.L2878
.L2900:
	ldrh	r3, [r1], #2
	cmp	r3, r6
	beq	.L2898
	ldr	ip, [sp, #12]
	orr	r3, r9, r3, lsl #10
	mla	ip, r7, ip, r0
	str	r3, [ip, #4]
	ldrb	r8, [fp, #8]	@ zero_extendqisi2
	cmp	r8, #1
	bne	.L2899
	cmp	r5, #0
	orrne	r3, r3, #-2147483648
	strne	r3, [ip, #4]
.L2899:
	ldr	r3, [sp, #12]
	add	r3, r3, #1
	uxth	r3, r3
	str	r3, [sp, #12]
.L2898:
	add	r2, r2, #1
	b	.L2897
.L2926:
	ldr	r3, [sp, #16]
	mov	r6, #36
	ldr	r8, [r4, #-1500]
	mul	r6, r6, r3
	add	r7, r8, r6
	ldr	r5, [r7, #4]
	ubfx	r0, r5, #10, #16
	str	r5, [sp, #44]
	bl	P2V_plane
	ldr	r3, [sp, #8]
	cmp	r9, r3
	bcc	.L2902
	ldr	r2, [sp, #28]
	moveq	r3, #1
	movne	r3, #0
	cmp	r2, r0
	movls	r3, #0
	andhi	r3, r3, #1
	cmp	r3, #0
	bne	.L2902
	ldr	r3, [sp]
	ldr	r2, [sp, #4]
	cmp	r9, r3
	cmpeq	r2, r0
	beq	.L2903
	ldr	r3, [r8, r6]
	cmn	r3, #1
	beq	.L2904
	ldr	r3, [r7, #12]
	movw	r2, #61589
	ldrh	r1, [r3]
	cmp	r1, r2
	ldrhne	r0, [fp]
	bne	.L3006
	ldr	r10, [r3, #4]
	cmn	r10, #1
	beq	.L2906
	ldr	r1, [r4, #-1608]
	mov	r0, r10
	bl	ftl_cmp_data_ver
	cmp	r0, #0
	addne	r2, r10, #1
	strne	r2, [r4, #-1608]
.L2906:
	ldr	r5, [r3, #8]
	add	r1, sp, #40
	ldr	r3, [r3, #12]
	mov	r2, #0
	mov	r0, r5
	str	r3, [sp, #36]
	bl	log2phys
	ldr	r1, [r4, #1752]
	cmn	r1, #1
	beq	.L2907
	mov	r0, r10
	bl	ftl_cmp_data_ver
	cmp	r0, #0
	beq	.L2907
	ldr	r3, [sp, #36]
	cmn	r3, #1
	beq	.L2908
	ldr	r0, [r4, #-1500]
	mov	r2, #0
	mov	r1, #1
	add	r0, r0, r6
	str	r3, [r0, #4]
	ldr	r7, [r0, #12]
	bl	FlashReadPages
	ldr	r2, [r4, #-1500]
	ldr	r1, [r2, r6]
	add	r3, r2, r6
	cmn	r1, #1
	bne	.L2909
.L2910:
	mvn	r3, #0
	str	r3, [sp, #36]
.L2917:
	ldr	r8, [sp, #36]
	cmn	r8, #1
	beq	.L2902
.L2931:
	ubfx	r0, r8, #10, #16
	bl	P2V_block_in_plane
	ldr	r2, [r4, #-1404]
	lsl	r3, r0, #1
	mov	r1, r0
	ldrh	r3, [r2, r3]
	cmp	r3, #0
	beq	.L2923
.L3006:
	bl	decrement_vpc_count
	b	.L2902
.L2908:
	ldr	r3, [sp, #44]
	ldr	r2, [sp, #40]
	cmp	r2, r3
	bne	.L2902
	mov	r2, #1
	add	r1, sp, #36
	mov	r0, r5
	bl	log2phys
.L2902:
	ldr	r3, [sp, #16]
	add	r3, r3, #1
	b	.L3008
.L2909:
	ldr	r1, [r7, #8]
	cmp	r5, r1
	bne	.L2910
	ldr	r8, [r7, #4]
	ldr	r0, [r4, #1752]
	mov	r1, r8
	bl	ftl_cmp_data_ver
	cmp	r0, #0
	beq	.L2910
	ldr	r1, [sp, #40]
	ldr	r0, [sp, #44]
	cmp	r1, r0
	bne	.L2912
.L2915:
	ldr	r1, [sp, #36]
	mov	r0, r5
	bl	FtlReUsePrevPpa
	b	.L2910
.L2912:
	ldr	r0, [sp, #36]
	cmp	r1, r0
	beq	.L2910
	cmn	r1, #1
	streq	r1, [r2, r6]
	beq	.L2914
	str	r1, [r3, #4]
	mov	r2, #0
	mov	r1, #1
	mov	r0, r3
	ldr	r7, [r3, #12]
	bl	FlashReadPages
.L2914:
	ldr	r3, [r4, #-1500]
	ldr	r3, [r3, r6]
	cmn	r3, #1
	beq	.L2915
	ldr	r3, [r7, #4]
	ldr	r0, [r4, #1752]
	mov	r1, r3
	bl	ftl_cmp_data_ver
	cmp	r0, #0
	beq	.L2915
	mov	r1, r3
	mov	r0, r8
	bl	ftl_cmp_data_ver
	cmp	r0, #0
	beq	.L2910
	b	.L2915
.L2907:
	ldr	r3, [sp, #44]
	ldr	r2, [sp, #40]
	cmp	r2, r3
	beq	.L2917
	ldr	r3, [sp, #36]
	cmn	r3, #1
	beq	.L2919
	ldr	r2, [r4, #-1716]
	ubfx	r3, r3, #10, #21
	cmp	r3, r2
	bcs	.L2902
.L2919:
	mov	r2, #1
	add	r1, sp, #44
	mov	r0, r5
	bl	log2phys
	ldr	r8, [sp, #40]
	cmn	r8, #1
	beq	.L2917
	ldr	r3, [sp, #36]
	cmp	r8, r3
	beq	.L2931
	ldr	r6, .L3011+32
	ubfx	r0, r8, #10, #16
	bl	P2V_block_in_plane
	ldrh	r3, [r6]
	sub	r6, r6, #884
	cmp	r3, r0
	beq	.L2922
	add	r3, r6, #932
	ldrh	r3, [r3]
	cmp	r3, r0
	beq	.L2922
	add	r3, r6, #980
	ldrh	r3, [r3]
	cmp	r3, r0
	bne	.L2917
.L2922:
	ldr	r0, [r6, #-1500]
	mov	r2, #0
	mov	r1, #1
	str	r8, [r0, #4]
	ldr	r7, [r0, #12]
	bl	FlashReadPages
	ldr	r3, [r6, #-1500]
	ldr	r3, [r3]
	cmn	r3, #1
	beq	.L2917
	ldr	r1, [r7, #4]
	mov	r0, r10
	bl	ftl_cmp_data_ver
	cmp	r0, #0
	bne	.L2917
	mov	r2, #1
	add	r1, sp, #40
	mov	r0, r5
	bl	log2phys
	b	.L2917
.L2923:
	ldr	r0, .L3011+36
	bl	printk
	b	.L2902
.L2904:
	ldrh	r3, [fp]
	mov	r1, r5
	ldr	r2, .L3011+20
	ldr	r0, .L3011+40
	strh	r3, [r2]	@ movhi
	mov	r2, r10
	bl	printk
	ldr	r3, [r4, #1800]
	cmp	r3, #31
	ldrls	r1, [sp, #44]
	addls	r2, r4, r3, lsl #2
	addls	r3, r3, #1
	strls	r3, [r4, #1800]
	strls	r1, [r2, #1804]
	ldrh	r0, [fp]
	bl	decrement_vpc_count
	ldr	r3, [r4, #1752]
	cmn	r3, #1
	bne	.L2925
.L3007:
	str	r10, [r4, #1752]
	b	.L2902
.L2925:
	cmp	r10, r3
	bcs	.L2902
	b	.L3007
.L2903:
	ldrb	r3, [sp, #4]	@ zero_extendqisi2
	ldr	r2, [sp, #4]
	strb	r3, [fp, #6]
	ldrh	r3, [sp]
	strh	r3, [fp, #2]	@ movhi
.L3010:
	ldr	r1, [sp]
	mov	r0, fp
	bl	ftl_sb_update_avl_pages
	b	.L2997
.L2999:
	mov	r0, #0
	bx	lr
.L3012:
	.align	2
.L3011:
	.word	.LANCHOR2-1664
	.word	.LANCHOR2
	.word	.LANCHOR0
	.word	.LANCHOR2-1732
	.word	.LC154
	.word	.LANCHOR2+1794
	.word	.LANCHOR0+1108
	.word	.LANCHOR2-1728
	.word	.LANCHOR2+884
	.word	.LC155
	.word	.LC156
	.fnend
	.size	FtlRecoverySuperblock, .-FtlRecoverySuperblock
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
	ldr	r5, .L3028
	ldr	r6, .L3028+4
	add	r3, r0, r3, lsl #1
	ldrh	r3, [r3, #16]
.L3017:
	movw	r1, #65535
	cmp	r3, r1
	beq	.L3019
	ldrb	r2, [r4, #8]	@ zero_extendqisi2
	cmp	r2, #1
	bne	.L3020
	ldrb	r3, [r5, #-2740]	@ zero_extendqisi2
	cmp	r3, #0
	bne	.L3020
	ldrh	r3, [r4, #2]
	lsl	r3, r3, #1
	ldrh	r3, [r6, r3]
	cmp	r3, r1
	bne	.L3020
	ldrh	r3, [r4, #4]
	ldrh	r0, [r4]
	sub	r3, r3, #1
	strh	r3, [r4, #4]	@ movhi
	bl	decrement_vpc_count
	ldrh	r2, [r4, #4]
	cmp	r2, #0
	bne	.L3019
	ldrh	r3, [r4, #2]
	strb	r2, [r4, #6]
	add	r3, r3, #1
	strh	r3, [r4, #2]	@ movhi
	pop	{r4, r5, r6, pc}
.L3019:
	ldrb	r3, [r4, #6]	@ zero_extendqisi2
	ldr	r2, .L3028+8
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
	b	.L3017
.L3020:
	ldrb	r3, [r5, #-2740]	@ zero_extendqisi2
	adds	r3, r3, #0
	movne	r3, #1
	cmp	r2, #1
	movne	r3, #0
	cmp	r3, #0
	popeq	{r4, r5, r6, pc}
	ldr	r1, .L3028+12
	ldrh	r2, [r4, #2]
	ldrh	r3, [r1]
	cmp	r2, r3
	popcc	{r4, r5, r6, pc}
	ldrh	r3, [r4]
	ldr	r0, [r5, #-1404]
	ldrh	ip, [r4, #4]
	lsl	r3, r3, #1
	ldrh	r2, [r0, r3]
	sub	r2, r2, ip
	strh	r2, [r0, r3]	@ movhi
	mov	r3, #0
	ldrh	r2, [r1, #-2]
	strh	r3, [r4, #4]	@ movhi
	strb	r3, [r4, #6]
	strh	r2, [r4, #2]	@ movhi
	pop	{r4, r5, r6, pc}
.L3029:
	.align	2
.L3028:
	.word	.LANCHOR2
	.word	.LANCHOR0+1108
	.word	.LANCHOR2-1732
	.word	.LANCHOR2-1664
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
	ldr	r6, .L3046
	ldr	r8, .L3046+4
	add	r3, r0, r3, lsl #1
	add	r7, r6, #1728
	ldrh	r2, [r3, #16]
.L3031:
	movw	r1, #65535
	cmp	r2, r1
	beq	.L3032
	ldrb	r3, [r4, #8]	@ zero_extendqisi2
	ldrh	r5, [r4, #2]
	cmp	r3, #1
	ldrh	r3, [r4, #4]
	bne	.L3034
	ldrb	r0, [r7, #-2740]	@ zero_extendqisi2
	cmp	r0, #0
	bne	.L3034
	lsl	r0, r5, #1
	ldrh	r0, [r8, r0]
	cmp	r0, r1
	bne	.L3034
	sub	r3, r3, #1
	ldrh	r0, [r4]
	strh	r3, [r4, #4]	@ movhi
	bl	decrement_vpc_count
.L3032:
	ldrb	r3, [r4, #6]	@ zero_extendqisi2
	ldrh	r2, [r6, #-4]
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
	b	.L3031
.L3034:
	ldr	r7, .L3046+8
	orr	r5, r5, r2, lsl #10
	ldr	r8, .L3046+4
	sub	r3, r3, #1
	strh	r3, [r4, #4]	@ movhi
.L3035:
	ldrb	r3, [r4, #6]	@ zero_extendqisi2
	movw	r2, #65535
	ldrh	r0, [r6, #-4]
.L3037:
	add	r3, r3, #1
	uxtb	r3, r3
	cmp	r3, r0
	ldrheq	r3, [r4, #2]
	addeq	r3, r3, #1
	strheq	r3, [r4, #2]	@ movhi
	moveq	r3, #0
	add	r1, r4, r3, lsl #1
	ldrh	r1, [r1, #16]
	cmp	r1, r2
	beq	.L3037
	strb	r3, [r4, #6]
	ldrb	r3, [r4, #8]	@ zero_extendqisi2
	cmp	r3, #1
	bne	.L3030
	ldrb	r3, [r7, #-2740]	@ zero_extendqisi2
	cmp	r3, #0
	ldrh	r3, [r4, #2]
	bne	.L3039
	lsl	r3, r3, #1
	ldrh	r3, [r8, r3]
	cmp	r3, r2
	bne	.L3030
	ldrh	r3, [r4, #4]
	cmp	r3, #0
	beq	.L3030
	sub	r3, r3, #1
	ldrh	r0, [r4]
	strh	r3, [r4, #4]	@ movhi
	bl	decrement_vpc_count
	b	.L3035
.L3039:
	ldr	r1, .L3046+12
	ldrh	r2, [r1]
	cmp	r3, r2
	bcc	.L3030
	ldrh	r3, [r4]
	ldr	r0, [r7, #-1404]
	ldrh	ip, [r4, #4]
	lsl	r3, r3, #1
	ldrh	r2, [r0, r3]
	sub	r2, r2, ip
	strh	r2, [r0, r3]	@ movhi
	mov	r3, #0
	ldrh	r2, [r1, #-2]
	strh	r3, [r4, #4]	@ movhi
	strb	r3, [r4, #6]
	strh	r2, [r4, #2]	@ movhi
.L3030:
	mov	r0, r5
	pop	{r4, r5, r6, r7, r8, pc}
.L3047:
	.align	2
.L3046:
	.word	.LANCHOR2-1728
	.word	.LANCHOR0+1108
	.word	.LANCHOR2
	.word	.LANCHOR2-1664
	.fnend
	.size	get_new_active_ppa, .-get_new_active_ppa
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
	ldr	r4, .L3067
	ldr	r3, [r4, #-1280]
	cmp	r3, #0
	bne	.L3048
	add	r6, r4, #884
	ldrh	r2, [r6, #4]
	cmp	r2, #0
	beq	.L3050
	ldrb	r3, [r4, #892]	@ zero_extendqisi2
	cmp	r3, #0
	bne	.L3050
	sub	r3, r4, #1664
	ldrb	r1, [r4, #891]	@ zero_extendqisi2
	ldrh	r3, [r3, #-2]
	mul	r3, r3, r1
	cmp	r2, r3
	beq	.L3050
	ldrb	r8, [r4, #894]	@ zero_extendqisi2
	cmp	r8, #0
	bne	.L3048
	ldr	r7, [r4, #-1284]
	sub	r3, r4, #1728
	mov	r2, r8
	mov	r1, sp
	ldrh	r9, [r3, #-4]
	sub	r7, r7, #1
	mov	r0, r7
	bl	log2phys
	ldr	r3, [sp]
	ldr	r5, [r4, #-1440]
	ldr	r0, [r4, #-1472]
	cmn	r3, #1
	str	r3, [sp, #8]
	str	r7, [sp, #20]
	str	r0, [sp, #12]
	str	r5, [sp, #16]
	str	r8, [r5, #4]
	beq	.L3052
	mov	r2, r8
	mov	r1, #1
	add	r0, sp, #4
	bl	FlashReadPages
.L3053:
	ldr	r10, .L3067+4
	mov	r8, #0
	ldr	r3, .L3067+8
	lsl	r9, r9, #2
	mov	fp, r8
	strh	r3, [r5]	@ movhi
.L3054:
	cmp	r9, r8
	bne	.L3058
.L3055:
	mov	r3, #1
.L3066:
	strb	r3, [r4, #894]
.L3048:
	add	sp, sp, #44
	@ sp needed
	pop	{r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L3052:
	sub	r3, r4, #1648
	mov	r1, #255
	ldrh	r2, [r3, #-8]
	bl	ftl_memset
	b	.L3053
.L3058:
	ldrh	r3, [r6, #4]
	cmp	r3, #0
	beq	.L3055
	ldr	r3, [sp, #8]
	mov	r0, r10
	str	r7, [r5, #8]
	add	r8, r8, #1
	str	r3, [r5, #12]
	ldrh	r3, [r6]
	strh	r3, [r5, #2]	@ movhi
	bl	get_new_active_ppa
	ldr	r3, [r4, #-1608]
	mov	r1, #1
	str	r0, [sp, #8]
	add	r0, sp, #4
	str	r3, [r5, #4]
	add	r3, r3, #1
	cmn	r3, #1
	moveq	r3, fp
	str	r3, [r4, #-1608]
	mov	r3, #0
	mov	r2, r3
	bl	FlashProgPages
	ldrh	r0, [r6]
	bl	decrement_vpc_count
	b	.L3054
.L3050:
	mov	r3, #0
	b	.L3066
.L3068:
	.align	2
.L3067:
	.word	.LANCHOR2
	.word	.LANCHOR2+884
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
	ldr	r5, .L3074
	mov	r7, #12
	bl	FtlWriteDumpData
	sub	r6, r5, #1616
.L3070:
	ldrh	r3, [r6, #-10]
	uxth	r0, r4
	cmp	r3, r0
	bhi	.L3072
	mov	r0, #0
	pop	{r4, r5, r6, r7, r8, pc}
.L3072:
	ldr	r2, [r5, #-1364]
	uxth	r3, r4
	mla	r3, r7, r3, r2
	ldr	r3, [r3, #4]
	cmp	r3, #0
	bge	.L3071
	bl	flush_l2p_region
.L3071:
	add	r4, r4, #1
	b	.L3070
.L3075:
	.align	2
.L3074:
	.word	.LANCHOR2
	.fnend
	.size	l2p_flush, .-l2p_flush
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
	push	{r4, r5, r6, r7, r8, r9, lr}
	.save {r4, r5, r6, r7, r8, r9, lr}
	.pad #44
	sub	sp, sp, #44
	ldr	r5, .L3092
	ldr	r9, [r5, #-1280]
	cmp	r9, #0
	bne	.L3076
	ldrb	r3, [r5, #-2740]	@ zero_extendqisi2
	cmp	r3, #0
	beq	.L3087
	ldrb	r3, [r0, #8]	@ zero_extendqisi2
	cmp	r3, #1
	ldrheq	r7, [r0, #4]
	moveq	r9, r3
	beq	.L3078
.L3087:
	mov	r7, #12
.L3078:
	mvn	r3, #0
	ldr	r6, [r5, #-1440]
	str	r3, [sp, #20]
	mov	r8, #0
	ldr	r3, [r5, #-1472]
	movw	r2, #61589
	str	r6, [sp, #16]
	mov	r4, r0
	str	r3, [sp, #12]
	mvn	r3, #2
	str	r3, [r6, #8]
	mvn	r3, #1
	str	r3, [r6, #12]
	ldrh	r3, [r0]
	strh	r8, [r6]	@ movhi
	strh	r3, [r6, #2]	@ movhi
	ldr	r3, [r5, #-1472]
	str	r2, [r3]
	ldr	r2, .L3092+4
	ldr	r3, [r5, #-1472]
	str	r2, [r3, #4]
.L3079:
	subs	r7, r7, #1
	bcc	.L3082
	ldrh	r3, [r4, #4]
	cmp	r3, #0
	bne	.L3080
.L3082:
	ldrh	r3, [r4]
	ldr	r1, [r5, #-1404]
	ldrh	r0, [r4, #4]
	lsl	r3, r3, #1
	ldrh	r2, [r1, r3]
	sub	r2, r2, r0
	strh	r2, [r1, r3]	@ movhi
	ldr	r3, .L3092+8
	ldrh	r3, [r3, #-2]
	strh	r3, [r4, #2]	@ movhi
	mov	r3, #0
	strb	r3, [r4, #6]
	strh	r3, [r4, #4]	@ movhi
.L3076:
	add	sp, sp, #44
	@ sp needed
	pop	{r4, r5, r6, r7, r8, r9, pc}
.L3080:
	mov	r0, r4
	bl	get_new_active_ppa
	cmn	r0, #1
	str	r0, [sp, #8]
	beq	.L3082
	ldr	r3, [r5, #-1608]
	mov	r2, r9
	mov	r1, #1
	add	r0, sp, #4
	str	r3, [r6, #4]
	add	r3, r3, #1
	cmn	r3, #1
	moveq	r3, r8
	str	r3, [r5, #-1608]
	mov	r3, #0
	bl	FlashProgPages
	ldrh	r0, [r4]
	bl	decrement_vpc_count
	b	.L3079
.L3093:
	.align	2
.L3092:
	.word	.LANCHOR2
	.word	305419896
	.word	.LANCHOR2-1664
	.fnend
	.size	FtlSuperblockPowerLostFix, .-FtlSuperblockPowerLostFix
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
	push	{r0, r1, r2, r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.save {r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.pad #12
	mov	r5, #0
	ldr	r4, .L3109
	ldr	r1, .L3109+4
	ldr	r0, .L3109+8
	sub	r7, r4, #1712
	bl	printk
	ldrh	r2, [r7, #-10]
	mov	r1, #0
	ldr	r0, [r4, #-1408]
	lsl	r2, r2, #1
	bl	ftl_memset
.L3095:
	ldr	r3, [r4, #-1284]
	cmp	r5, r3
	bcc	.L3097
	ldr	r9, .L3109+12
	mov	r8, #0
	add	r10, r9, #96
	add	fp, r9, #48
.L3098:
	ldrh	r3, [r7, #-12]
	uxth	r6, r8
	cmp	r3, r6
	bhi	.L3101
	bl	l2p_flush
	bl	FtlVpcTblFlush
	add	sp, sp, #12
	@ sp needed
	pop	{r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L3097:
	mov	r2, #0
	add	r1, sp, #4
	mov	r0, r5
	bl	log2phys
	ldr	r0, [sp, #4]
	cmn	r0, #1
	beq	.L3096
	ubfx	r0, r0, #10, #16
	bl	P2V_block_in_plane
	ldr	r2, [r4, #-1408]
	lsl	r0, r0, #1
	ldrh	r3, [r2, r0]
	add	r3, r3, #1
	strh	r3, [r2, r0]	@ movhi
.L3096:
	add	r5, r5, #1
	b	.L3095
.L3101:
	uxth	r1, r8
	ldr	r3, [r4, #-1404]
	movw	r0, #65535
	lsl	r5, r1, #1
	ldrh	r2, [r3, r5]
	ldr	r3, [r4, #-1408]
	ldrh	r3, [r3, r5]
	cmp	r2, r0
	cmpne	r2, r3
	beq	.L3099
	ldrh	r0, [r9]
	cmp	r0, r6
	beq	.L3099
	ldrh	r0, [r10]
	cmp	r0, r6
	beq	.L3099
	ldrh	r0, [fp]
	cmp	r0, r6
	beq	.L3099
	ldr	r0, .L3109+16
	bl	printk
	ldr	r3, [r4, #-1404]
	ldrh	r2, [r3, r5]
	cmp	r2, #0
	ldr	r2, [r4, #-1408]
	ldrh	r2, [r2, r5]
	strh	r2, [r3, r5]	@ movhi
	bne	.L3100
.L3099:
	add	r8, r8, #1
	b	.L3098
.L3100:
	mov	r0, r6
	bl	update_vpc_list
	b	.L3099
.L3110:
	.align	2
.L3109:
	.word	.LANCHOR2
	.word	.LANCHOR3+203
	.word	.LC50
	.word	.LANCHOR2+884
	.word	.LC157
	.fnend
	.size	FtlVpcCheckAndModify, .-FtlVpcCheckAndModify
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
	push	{r4, r5, r6, lr}
	.save {r4, r5, r6, lr}
	ldr	r4, .L3138
	ldr	r3, [r4, #-1280]
	cmp	r3, #0
	bne	.L3112
	ldrh	r6, [r0]
	movw	r3, #65535
	mov	r5, r0
	cmp	r6, r3
	beq	.L3113
	ldr	r2, [r4, #-1404]
	lsl	r3, r6, #1
	mov	r0, r6
	ldrh	r3, [r2, r3]
	cmp	r3, #0
	beq	.L3114
	bl	INSERT_DATA_LIST
.L3113:
	mov	r3, #0
	strb	r3, [r5, #8]
	ldr	r3, .L3138+4
	cmp	r5, r3
	beq	.L3115
	ldr	r2, .L3138+8
	ldrh	r2, [r2]
	cmp	r2, #1
	beq	.L3115
	ldrb	r1, [r4, #-2740]	@ zero_extendqisi2
	cmp	r1, #0
	beq	.L3116
.L3115:
	mov	r3, #1
	strb	r3, [r5, #8]
.L3117:
	movw	r3, #1740
	ldrh	r0, [r4, r3]
	movw	r3, #65535
	cmp	r0, r3
	beq	.L3122
	cmp	r6, r0
	bne	.L3123
	ldr	r2, [r4, #-1404]
	lsl	r3, r0, #1
	ldrh	r3, [r2, r3]
	cmp	r3, #0
	beq	.L3124
.L3123:
	bl	update_vpc_list
.L3124:
	mvn	r2, #0
	movw	r3, #1740
	strh	r2, [r4, r3]	@ movhi
.L3122:
	mov	r0, r5
	bl	allocate_data_superblock
	bl	l2p_flush
	mov	r0, #0
	bl	FtlEctTblFlush
	bl	FtlVpcTblFlush
.L3112:
	mov	r0, #0
	pop	{r4, r5, r6, pc}
.L3114:
	bl	INSERT_FREE_LIST
	b	.L3113
.L3116:
	sub	r3, r3, #48
	cmp	r5, r3
	bne	.L3117
	cmp	r2, #3
	beq	.L3119
	ldr	r3, [r4, #-1616]
	cmp	r3, #1
	bne	.L3120
.L3119:
	mov	r3, #1
	strb	r3, [r4, #892]
.L3120:
	ldr	r3, [r4, #-1868]
	cmp	r3, #0
	beq	.L3117
	ldr	r3, [r4, #-1564]
	cmp	r3, #39
	movls	r3, #1
	strbls	r3, [r4, #892]
	b	.L3117
.L3139:
	.align	2
.L3138:
	.word	.LANCHOR2
	.word	.LANCHOR2+932
	.word	.LANCHOR2-1712
	.fnend
	.size	allocate_new_data_superblock, .-allocate_new_data_superblock
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
	ldr	r5, .L3157
	ldr	r9, [r5, #1284]
	mov	r6, r5
	cmp	r9, #0
	beq	.L3141
	ldr	r2, [r5, #1288]
	ldr	r3, [r5, #-1284]
	cmp	r2, r3
	bcs	.L3142
	mov	r4, #2048
.L3147:
	ldr	r0, [r6, #1288]
	ldr	r3, [r6, #-1284]
	cmp	r0, r3
	bcc	.L3143
.L3146:
	mvn	r0, #0
.L3140:
	add	sp, sp, #40
	@ sp needed
	pop	{r4, r5, r6, r7, r8, r9, r10, pc}
.L3143:
	mov	r2, #0
	mov	r1, sp
	bl	log2phys
	ldr	r2, [sp]
	ldr	r3, [r6, #1288]
	cmn	r2, #1
	add	r3, r3, #1
	str	r3, [r6, #1288]
	beq	.L3145
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
	bne	.L3146
	ldr	r0, [sp]
	ubfx	r0, r0, #10, #16
	bl	P2V_block_in_plane
	bl	FtlGcRefreshBlock
	b	.L3146
.L3145:
	subs	r4, r4, #1
	bne	.L3147
	b	.L3146
.L3142:
	ldr	r3, [r5, #-1584]
	mov	r0, #0
	str	r0, [r5, #1284]
	str	r0, [r5, #1288]
	str	r3, [r5, #1280]
	b	.L3140
.L3141:
	ldr	r1, [r5, #-1568]
	movw	r4, #10000
	ldr	r8, [r5, #-1584]
	add	r10, r5, #816
	ldr	r7, [r5, #1280]
	cmp	r1, r4
	add	r3, r8, #1048576
	movhi	r4, #31
	movls	r4, #63
	cmp	r7, r3
	bhi	.L3151
	ldr	r3, [r5, #-1284]
	lsr	r1, r1, #10
	mov	r0, #1000
	add	r1, r1, #1
	mul	r0, r0, r3
	bl	__aeabi_uidiv
	add	r0, r0, r7
	cmp	r8, r0
	bhi	.L3151
	ldrh	r3, [r10, #28]
	ands	r0, r4, r3
	movne	r0, r9
	bne	.L3140
	ldr	r2, [r5, #1304]
	cmp	r3, r2
	beq	.L3140
.L3151:
	ldrh	r3, [r10, #28]
	mov	r0, #0
	str	r0, [r6, #1288]
	str	r8, [r6, #1280]
	str	r3, [r6, #1304]
	mov	r3, #1
	str	r3, [r6, #1284]
	b	.L3140
.L3158:
	.align	2
.L3157:
	.word	.LANCHOR2
	.fnend
	.size	FtlReadRefresh, .-FtlReadRefresh
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
	ldr	r3, .L3323
	push	{r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.save {r4, r5, r6, r7, r8, r9, r10, fp, lr}
	mov	lr, r0
	.pad #44
	sub	sp, sp, #44
	ldr	r0, [r3, #-1280]
	cmp	r0, #0
	bne	.L3255
	ldr	ip, .L3323+4
	ldr	r4, [ip, #3440]
	cmp	r4, #1
	bne	.L3159
	ldr	r2, [r3, #-1560]
	cmp	r2, #0
	bne	.L3159
	add	r0, r3, #872
	ldrh	r0, [r0]
	cmp	r0, #47
	bls	.L3255
	movw	r2, #3444
	mov	r8, r1
	ldrh	r1, [ip, r2]
	movw	r2, #65535
	mov	r5, r3
	str	lr, [sp, #24]
	cmp	r1, r2
	bne	.L3161
.L3164:
	ldr	r6, .L3323+8
	movw	r1, #65535
	ldrh	ip, [r6, #-14]
	cmp	ip, r1
	bne	.L3162
.L3163:
	ldr	r3, [r5, #-1544]
	ldr	r2, [sp, #24]
	add	r3, r3, #1
	cmp	r2, #1
	add	r3, r3, r2, lsl #7
	str	r3, [r5, #-1544]
	bne	.L3165
	ldr	r2, [r5, #-1868]
	cmp	r2, #0
	bne	.L3166
	ldrb	r2, [r5, #-2740]	@ zero_extendqisi2
	cmp	r2, #0
	beq	.L3165
.L3166:
	ldr	r2, [r5, #-1564]
	cmp	r2, #39
	bhi	.L3165
	movw	r2, #1932
	movw	r4, #65535
	ldrh	r2, [r5, r2]
	add	r3, r2, r3
	str	r3, [r5, #-1544]
	bl	FtlGcReFreshBadBlk
	movw	r3, #1156
	ldrh	r2, [r5, r3]
	cmp	r2, r4
	bne	.L3167
	ldr	r3, .L3323+12
	ldrh	r1, [r3]
	cmp	r1, r2
	bne	.L3254
	ldr	r2, [r5, #-1544]
	add	r3, r3, #2416
	cmp	r2, #1024
	bhi	.L3169
	ldrh	r2, [r3]
	cmp	r2, #63
	bhi	.L3254
.L3169:
	ldrh	r0, [r3]
	movw	r2, #1932
	ldrh	r3, [r6, #-6]
	mov	r1, #0
	strh	r1, [r5, r2]	@ movhi
	add	r3, r3, #64
	cmp	r0, r3
	bgt	.L3254
	ldr	r3, [r5, #-1564]
	str	r1, [r5, #-1544]
	cmp	r3, r1
	moveq	r3, #6
	beq	.L3315
	cmp	r3, #5
	bhi	.L3171
	mov	r3, #18
.L3315:
	strh	r3, [r5, r2]	@ movhi
.L3171:
	mov	r0, #32
	movw	r10, #65535
	bl	List_get_gc_head_node
	uxth	r3, r0
	cmp	r3, r10
	beq	.L3175
	ldrh	r0, [r6, #-4]
	cmp	r0, #0
	beq	.L3173
	ldr	r2, .L3323+16
	lsl	r7, r3, #1
	ldr	r1, [r5, #-1404]
	ldrh	lr, [r2], #-64
	ldrh	ip, [r1, r7]
	str	r1, [sp, #12]
	ldrh	r3, [r2, #-4]
	mul	r3, r3, lr
	add	r3, r3, #1
	cmp	ip, r3
	bgt	.L3175
	add	fp, r0, #1
	mov	r9, #0
	uxth	fp, fp
	str	r9, [r5, #-1556]
	strh	fp, [r6, #-4]	@ movhi
	bl	List_get_gc_head_node
	uxth	r4, r0
	ldr	r1, [sp, #12]
	cmp	r4, r10
	beq	.L3175
	lsl	r10, r4, #1
	mov	r2, r4
	ldr	r0, .L3323+20
	ldrh	r3, [r1, r10]
	ldrh	r1, [r1, r7]
	str	r1, [sp]
	mov	r1, fp
	bl	printk
	ldrh	r3, [r6, #-4]
	cmp	r3, #40
	bls	.L3174
	ldr	r3, [r5, #-1404]
	ldrh	r3, [r3, r10]
	cmp	r3, #32
	strhhi	r9, [r6, #-4]	@ movhi
.L3174:
	mov	r2, #6
	movw	r3, #1932
	strh	r2, [r5, r3]	@ movhi
.L3167:
	movw	r0, #65535
	ldr	r3, [sp, #24]
	sub	r2, r4, r0
	clz	r2, r2
	lsr	r2, r2, #5
	cmp	r3, #0
	movne	r1, #0
	andeq	r1, r2, #1
	cmp	r1, #0
	beq	.L3189
	ldr	r3, .L3323+24
	ldrh	r2, [r3]
	cmp	r2, #24
	movhi	r9, #1
	bhi	.L3190
	cmp	r2, #16
	sub	r3, r3, #2544
	ldrhhi	r3, [r3, #-2]
	lsrhi	r9, r3, #5
	bhi	.L3190
	cmp	r2, #12
	ldrhhi	r3, [r3, #-2]
	lsrhi	r9, r3, #4
	bhi	.L3190
	cmp	r2, #8
	ldrhhi	r3, [r3, #-2]
	ldrhls	r9, [r3, #-2]
	lsrhi	r9, r3, #2
.L3190:
	ldrh	r3, [r6, #-8]
	cmp	r3, r2
	bcs	.L3194
	ldr	r3, .L3323+28
	movw	r2, #65535
	ldrh	r3, [r3]
	cmp	r3, r2
	bne	.L3195
	ldr	r2, .L3323+12
	ldrh	r2, [r2]
	cmp	r2, r3
	bne	.L3195
	movw	r3, #1932
	ldrh	r0, [r5, r3]
	cmp	r0, #0
	bne	.L3196
	ldr	r3, [r5, #-1284]
	ldr	r2, [r5, #1124]
	add	r3, r3, r3, lsl #1
	cmp	r2, r3, lsr #2
	movcs	r3, #18
	bcs	.L3319
.L3196:
	ldr	r3, .L3323+32
	ldrh	r3, [r3]
	add	r3, r3, r3, lsl #1
	asr	r3, r3, #2
.L3319:
	strh	r3, [r6, #-8]	@ movhi
	mov	r3, #0
	str	r3, [r5, #-1556]
.L3159:
	add	sp, sp, #44
	@ sp needed
	pop	{r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L3161:
	add	r3, r3, #980
	ldrh	r3, [r3]
	cmp	r3, r2
	beq	.L3164
	mov	r0, r4
	bl	FtlGcFreeTempBlock
	cmp	r0, #0
	beq	.L3164
	mov	r0, r4
	b	.L3159
.L3162:
	mov	r3, r6
	ldrh	r2, [r3, #-16]!
	cmp	r2, r1
	bne	.L3163
	ldrh	r0, [r6, #-12]
	cmp	r0, r2
	beq	.L3163
	ldrh	r1, [r6, #-10]
	cmp	r1, r2
	strhne	ip, [r3]	@ movhi
	mvnne	r3, #0
	strhne	r0, [r6, #-14]	@ movhi
	strhne	r1, [r6, #-12]	@ movhi
	strhne	r3, [r6, #-10]	@ movhi
	b	.L3163
.L3173:
	mov	r3, #1
	strh	r3, [r6, #-4]	@ movhi
.L3175:
	bl	GetSwlReplaceBlock
	movw	r3, #65535
	mov	r4, r0
	cmp	r0, r3
	bne	.L3167
	mov	r2, #0
	movw	r3, #1932
	strh	r2, [r5, r3]	@ movhi
.L3165:
	movw	r3, #1156
	movw	r4, #65535
	ldrh	r3, [r5, r3]
	cmp	r3, r4
	bne	.L3167
.L3254:
	ldr	r7, .L3323+28
	movw	r3, #65535
	ldrh	r4, [r7]
	cmp	r4, r3
	movne	r4, r3
	bne	.L3167
	ldr	r3, .L3323+12
	ldrh	r9, [r3]
	cmp	r9, r4
	bne	.L3167
	ldrh	r3, [r7, #-100]!
	ldr	r2, [r5, #-1544]
	cmp	r3, #24
	movcc	r3, #5120
	movcs	r3, #1024
	cmp	r2, r3
	bls	.L3167
	mov	r3, #0
	movw	r2, #1932
	str	r3, [r5, #-1544]
	strh	r3, [r5, r2]	@ movhi
	bl	GetSwlReplaceBlock
	cmp	r0, r9
	mov	r4, r0
	movne	r9, r0
	bne	.L3177
	ldrh	r2, [r7]
	ldrh	r3, [r6, #-6]
	cmp	r2, r3
	bcs	.L3178
	mov	r0, #64
	bl	List_get_gc_head_node
	uxth	r3, r0
	cmp	r3, r4
	beq	.L3180
	ldr	r3, [r5, #-1620]
	ldr	r1, .L3323+36
	cmp	r3, #0
	uxth	r3, r0
	bne	.L3181
	ldrh	r2, [r1]
	cmp	r2, #3
	beq	.L3181
	ldr	r2, [r5, #-1616]
	cmp	r2, #0
	bne	.L3181
	ldr	r2, [r5, #-1868]
	cmp	r2, #0
	bne	.L3181
	ldrb	r0, [r5, #-2740]	@ zero_extendqisi2
	cmp	r0, #0
	beq	.L3182
.L3181:
	ldr	r2, [r5, #-1404]
	lsl	r3, r3, #1
	ldrh	r1, [r1]
	ldrh	r0, [r2, r3]
	ldr	r2, .L3323+16
	cmp	r1, #3
	ldrh	r3, [r2], #-64
	ldrh	r2, [r2, #-4]
	mul	r2, r3, r2
	lsreq	r3, r3, #1
	movne	r3, #0
	add	r3, r3, r2
	cmp	r0, r3
	bgt	.L3184
	mov	r0, #0
	bl	List_get_gc_head_node
	ldr	r3, [r5, #-1284]
	uxth	r9, r0
	ldr	r2, [r5, #1124]
	add	r3, r3, r3, lsl #1
	cmp	r2, r3, lsr #2
	movls	r3, #160
	bls	.L3316
.L3317:
	mov	r3, #128
.L3316:
	strh	r3, [r6, #-6]	@ movhi
	movw	r3, #65535
	cmp	r9, r3
	beq	.L3180
.L3177:
	ldr	r3, [r5, #-1404]
	lsl	r1, r9, #1
	ldrh	r0, [r6, #-8]
	mov	r4, r9
	ldrh	r2, [r7]
	ldrh	r3, [r3, r1]
	str	r0, [sp, #4]
	ldr	r0, [r5, #-1412]
	ldrh	r1, [r0, r1]
	ldr	r0, .L3323+40
	str	r1, [sp]
	mov	r1, r9
	bl	printk
	b	.L3180
.L3184:
	mov	r3, #128
.L3318:
	strh	r3, [r6, #-6]	@ movhi
.L3180:
	bl	FtlGcReFreshBadBlk
	b	.L3167
.L3182:
	ldr	r2, [r5, #-1404]
	lsl	r3, r3, #1
	ldrh	r3, [r2, r3]
	cmp	r3, #7
	bhi	.L3187
	bl	List_get_gc_head_node
	uxth	r9, r0
	b	.L3317
.L3187:
	mov	r3, #64
	b	.L3318
.L3178:
	mov	r3, #80
	b	.L3318
.L3195:
	ldr	r3, .L3323+32
	ldrh	r3, [r3]
	add	r3, r3, r3, lsl #1
	asr	r3, r3, #2
	strh	r3, [r6, #-8]	@ movhi
.L3194:
	ldr	r3, [r5, #-1868]
	movw	r4, #65535
	adds	r3, r3, #0
	movne	r3, #1
	cmp	r8, #2
	movhi	r3, #0
	cmp	r3, #0
	addne	r3, r9, #1
	uxthne	r9, r3
.L3200:
	movw	r3, #1156
	ldrh	r2, [r5, r3]
	movw	r1, #65535
	cmp	r2, r1
	bne	.L3210
	cmp	r4, r2
	strhne	r4, [r5, r3]	@ movhi
	bne	.L3212
	ldr	r3, .L3323+12
	ldrh	r2, [r3]
	cmp	r2, r4
	beq	.L3212
	ldr	r1, [r5, #-1404]
	lsl	r2, r2, #1
	ldrh	r2, [r1, r2]
	cmp	r2, #0
	mvneq	r2, #0
	strheq	r2, [r3]	@ movhi
	movw	r2, #1156
	ldrh	r1, [r3]
	strh	r1, [r5, r2]	@ movhi
	mvn	r2, #0
	strh	r2, [r3]	@ movhi
.L3212:
	movw	r6, #1156
	mov	r3, #0
	ldrh	r0, [r5, r6]
	strb	r3, [r5, #1164]
	movw	r3, #65535
	cmp	r0, r3
	beq	.L3210
	bl	IsBlkInGcList
	cmp	r0, #0
	mvnne	r3, #0
	strhne	r3, [r5, r6]	@ movhi
	ldrb	r3, [r5, #-2740]	@ zero_extendqisi2
	cmp	r3, #0
	beq	.L3216
	movw	r3, #1156
	ldrh	r0, [r5, r3]
	bl	ftl_get_blk_mode
	strb	r0, [r5, #1164]
.L3216:
	movw	r7, #1156
	movw	r3, #65535
	ldrh	r2, [r5, r7]
	ldr	r6, .L3323+44
	cmp	r2, r3
	beq	.L3210
	mov	r0, r6
	bl	make_superblock
	mov	r3, #0
	movw	r2, #1934
	strh	r3, [r6, #2]	@ movhi
	add	r6, r6, #780
	strh	r3, [r5, r2]	@ movhi
	strb	r3, [r5, #1162]
	ldrh	r3, [r5, r7]
	ldr	r2, [r5, #-1404]
	lsl	r3, r3, #1
	ldrh	r3, [r2, r3]
	strh	r3, [r6]	@ movhi
.L3210:
	ldr	r2, .L3323+48
	movw	r3, #1156
	ldrh	r3, [r5, r3]
	ldrh	r1, [r2]
	cmp	r1, r3
	beq	.L3217
	ldrh	r1, [r2, #48]
	cmp	r1, r3
	beq	.L3217
	ldrh	r2, [r2, #96]
	cmp	r2, r3
	bne	.L3218
.L3217:
	mvn	r2, #0
	movw	r3, #1156
	strh	r2, [r5, r3]	@ movhi
.L3218:
	ldr	r5, .L3323
	mov	r10, r5
.L3251:
	ldr	r8, .L3323+44
	movw	r3, #65535
	ldrh	r2, [r8]
	cmp	r2, r3
	bne	.L3219
	ldr	fp, .L3323+8
	mov	r3, #0
	str	r3, [r5, #-1556]
.L3220:
	ldr	r6, .L3323+52
	ldrh	r7, [r6]
	mov	r0, r7
	bl	List_get_gc_head_node
	ldr	r1, .L3323+44
	uxth	r2, r0
	strh	r2, [r1]	@ movhi
	movw	r1, #65535
	cmp	r2, r1
	bne	.L3221
	mov	r3, #0
	mov	r0, #8
	strh	r3, [r6]	@ movhi
	b	.L3159
.L3189:
	ldr	r3, .L3323+28
	ldrh	r8, [r3]
	cmp	r8, r0
	bne	.L3201
	ldr	r0, .L3323+12
	ldrh	r0, [r0]
	cmp	r0, r8
	movne	r2, #0
	andeq	r2, r2, #1
	cmp	r2, #0
	beq	.L3201
	movw	r2, #1156
	ldrh	r2, [r5, r2]
	cmp	r2, r8
	beq	.L3202
.L3207:
	mov	r4, r8
.L3201:
	ldr	r3, [r5, #-1868]
	cmp	r3, #0
	moveq	r9, #1
	movne	r9, #2
	b	.L3200
.L3202:
	mov	r4, r3
	ldrh	r3, [r6, #-8]
	ldrh	r2, [r4, #-100]!
	str	r1, [r5, #-1556]
	cmp	r2, r3
	bls	.L3203
	movw	r3, #1932
	ldrh	r3, [r5, r3]
	cmp	r3, #0
	bne	.L3204
	ldr	r3, [r5, #-1284]
	ldr	r2, [r5, #1124]
	add	r3, r3, r3, lsl #1
	cmp	r2, r3, lsr #2
	movcs	r3, #18
	bcs	.L3320
.L3204:
	ldr	r3, .L3323+32
	ldrh	r3, [r3]
	add	r3, r3, r3, lsl #1
	asr	r3, r3, #2
.L3320:
	strh	r3, [r6, #-8]	@ movhi
	bl	FtlReadRefresh
	mov	r0, #0
	bl	List_get_gc_head_node
	uxth	r0, r0
	ldr	r3, [r5, #-1404]
	lsl	r0, r0, #1
	ldrh	r3, [r3, r0]
	cmp	r3, #4
	bls	.L3203
.L3322:
	movw	r3, #1932
	ldrh	r0, [r5, r3]
	b	.L3159
.L3203:
	movw	r7, #1932
	ldrh	r0, [r5, r7]
	cmp	r0, #0
	bne	.L3207
	ldr	r10, .L3323+32
	ldrh	r9, [r10]
	add	r3, r9, r9, lsl #1
	asr	r3, r3, #2
	strh	r3, [r6, #-8]	@ movhi
	bl	List_get_gc_head_node
	uxth	r0, r0
	ldr	r3, [r5, #-1404]
	lsl	r0, r0, #1
	ldrh	r2, [r3, r0]
	sub	r3, r10, #2816
	sub	r10, r10, #2880
	ldrh	r1, [r3]
	ldrh	r3, [r10, #-4]
	mul	r3, r3, r1
	add	r3, r3, r3, lsr #31
	cmp	r2, r3, asr #1
	ble	.L3208
	ldrh	r3, [r4]
	sub	r9, r9, #1
	cmp	r3, r9
	blt	.L3208
	bl	FtlReadRefresh
	ldrh	r0, [r5, r7]
	b	.L3159
.L3208:
	cmp	r2, #0
	bne	.L3207
	movw	r0, #65535
	bl	decrement_vpc_count
	ldrh	r0, [r4]
	add	r0, r0, #1
	b	.L3159
.L3221:
	str	r0, [sp, #16]
	mov	r0, r2
	str	r2, [sp, #12]
	add	r7, r7, #1
	bl	IsBlkInGcList
	cmp	r0, #0
	ldr	r2, [sp, #12]
	ldr	r3, [sp, #16]
	strhne	r7, [r6]	@ movhi
	bne	.L3220
	ldr	lr, .L3323+16
	uxth	r3, r3
	ldr	r0, [r10, #-1404]
	uxth	r7, r7
	lsl	r1, r3, #1
	ldrh	r3, [r6, #-142]
	ldrh	lr, [lr, #-68]
	strh	r7, [r6]	@ movhi
	ldrh	ip, [r0, r1]
	mul	r3, lr, r3
	add	lr, r3, r3, lsr #31
	cmp	ip, lr, asr #1
	bgt	.L3224
	cmp	r7, #48
	cmphi	ip, #8
	bls	.L3225
	add	r6, r6, #3280
	ldrh	ip, [r6]
	cmp	ip, #35
	bhi	.L3225
.L3224:
	mov	ip, #0
	strh	ip, [fp, #-4]	@ movhi
.L3225:
	ldrh	r1, [r0, r1]
	movw	r0, #65535
	cmp	r3, r1
	cmple	r4, r0
	bne	.L3226
	ldrh	r0, [fp, #-4]
	cmp	r0, #3
	bhi	.L3226
	movw	r2, #1156
	mvn	r1, #0
	strh	r1, [r10, r2]	@ movhi
	movw	r3, #1932
	mov	r2, #0
	ldrh	r0, [r10, r3]
	strh	r2, [fp, #-4]	@ movhi
	b	.L3159
.L3226:
	cmp	r1, #0
	bne	.L3227
	movw	r0, #65535
	bl	decrement_vpc_count
	ldrh	r3, [fp, #-4]
	add	r3, r3, #1
	strh	r3, [fp, #-4]	@ movhi
	b	.L3220
.L3227:
	mov	r3, #0
	strb	r3, [r10, #1164]
	ldrb	r3, [r10, #-2740]	@ zero_extendqisi2
	cmp	r3, #0
	beq	.L3228
	mov	r0, r2
	bl	ftl_get_blk_mode
	strb	r0, [r10, #1164]
.L3228:
	ldr	r0, .L3323+44
	bl	make_superblock
	ldrh	r2, [r8]
	mov	r3, #0
	ldr	r1, .L3323+56
	ldr	r0, [r10, #-1404]
	lsl	r2, r2, #1
	strh	r3, [r1]	@ movhi
	ldrh	r2, [r0, r2]
	strh	r3, [r8, #2]	@ movhi
	strb	r3, [r10, #1162]
	strh	r2, [r1, #2]	@ movhi
.L3219:
	ldr	r3, [sp, #24]
	cmp	r3, #1
	bne	.L3229
	bl	FtlReadRefresh
.L3229:
	mov	r3, #1
	str	r3, [r10, #-1560]
	ldr	r3, .L3323+16
	ldrh	r2, [r3, #-2]
	str	r2, [sp, #12]
	ldrb	r2, [r10, #-2740]	@ zero_extendqisi2
	cmp	r2, #0
	beq	.L3230
	ldrb	r2, [r10, #1164]	@ zero_extendqisi2
	cmp	r2, #1
	ldrheq	r3, [r3]
	streq	r3, [sp, #12]
.L3230:
	ldrh	r3, [r8, #2]
	ldr	r1, [sp, #12]
	ldr	r6, .L3323+44
	add	r2, r3, r9
	cmp	r2, r1
	movgt	r2, r1
	subgt	r3, r2, r3
	uxthgt	r9, r3
	mov	r3, #0
	str	r3, [sp, #20]
.L3232:
	ldrh	r3, [sp, #20]
	cmp	r9, r3
	bls	.L3239
	ldr	r3, .L3323+60
	add	ip, r6, #14
	ldrh	r1, [r6, #2]
	mov	lr, #36
	ldr	r0, [r5, #-1484]
	ldrh	r8, [r3]
	ldr	r3, [sp, #20]
	add	r1, r1, r3
	mov	r3, #0
	mov	fp, r3
	b	.L3240
.L3234:
	ldrh	r2, [ip, #2]!
	movw	r7, #65535
	add	r3, r3, #1
	cmp	r2, r7
	mlane	r7, lr, fp, r0
	addne	fp, fp, #1
	orrne	r2, r1, r2, lsl #10
	uxthne	fp, fp
	strne	r2, [r7, #4]
.L3240:
	uxth	r2, r3
	cmp	r8, r2
	bhi	.L3234
	ldrb	r2, [r5, #1164]	@ zero_extendqisi2
	mov	r1, fp
	bl	FlashReadPages
	mov	r3, #0
.L3321:
	str	r3, [sp, #16]
	ldrh	r3, [sp, #16]
	cmp	fp, r3
	ldrls	r3, [sp, #20]
	addls	r3, r3, #1
	strls	r3, [sp, #20]
	bls	.L3232
.L3238:
	ldr	r2, [sp, #16]
	mov	r3, #36
	mul	r7, r3, r2
	ldr	r3, [r5, #-1484]
	add	r2, r3, r7
	ldr	r3, [r3, r7]
	cmn	r3, #1
	beq	.L3236
	ldr	r8, [r2, #12]
	movw	r3, #61589
	ldrh	r2, [r8]
	cmp	r2, r3
	bne	.L3236
	mov	r2, #0
	add	r1, sp, #32
	ldr	r0, [r8, #8]
	bl	log2phys
	ldr	r2, [r5, #-1484]
	ldr	r3, [sp, #32]
	add	r2, r2, r7
	ldr	r1, [r2, #4]
	bic	r3, r3, #-2147483648
	cmp	r3, r1
	bne	.L3236
	ldr	r3, .L3323+56
	mov	r0, #36
	ldr	r1, .L3323+56
	ldr	r2, [r2, #16]
	ldrh	r3, [r3]
	add	r3, r3, #1
	strh	r3, [r1]	@ movhi
	ldr	r1, [r5, #1732]
	ldr	r3, [r5, #-1496]
	mla	r3, r0, r1, r3
	str	r2, [r3, #16]
	str	r3, [sp, #28]
	bl	Ftl_get_new_temp_ppa
	ldr	r3, [sp, #28]
	mov	r1, #36
	ldr	r2, [r5, #-1496]
	str	r0, [r3, #4]
	ldr	r3, [r5, #1732]
	mla	r2, r1, r3, r2
	ldr	r3, [r5, #-1484]
	add	r3, r3, r7
	ldr	r1, [r3, #8]
	str	r1, [r2, #8]
	mov	r1, #1
	ldr	r3, [r3, #12]
	str	r3, [r2, #12]
	ldr	r3, [sp, #32]
	str	r3, [r8, #12]
	ldr	r3, .L3323+28
	ldrh	r3, [r3]
	strh	r3, [r8, #2]	@ movhi
	ldr	r3, [r5, #-1608]
	ldr	r0, [r5, #-1484]
	str	r3, [r8, #4]
	ldr	r3, [r5, #1732]
	add	r0, r0, r7
	add	r3, r3, #1
	str	r3, [r5, #1732]
	bl	FtlGcBufAlloc
	ldrb	r3, [r5, #-2740]	@ zero_extendqisi2
	cmp	r3, #0
	bne	.L3237
	ldrb	r2, [r5, #987]	@ zero_extendqisi2
	ldr	r3, [r5, #1732]
	cmp	r2, r3
	beq	.L3237
	ldr	r3, .L3323+28
	ldrh	r3, [r3, #4]
	cmp	r3, #0
	bne	.L3236
.L3237:
	bl	Ftl_gc_temp_data_write_back
	cmp	r0, #0
	beq	.L3236
	ldr	r3, .L3323
	mvn	r0, #0
	movw	r1, #1156
	mov	r2, #0
	strh	r0, [r3, r1]	@ movhi
	ldr	r1, .L3323+44
	str	r2, [r3, #-1560]
	strh	r2, [r1, #2]	@ movhi
	movw	r2, #1932
	ldrh	r0, [r3, r2]
	b	.L3159
.L3236:
	ldr	r3, [sp, #16]
	add	r3, r3, #1
	b	.L3321
.L3239:
	ldrh	r3, [r6, #2]
	ldr	r2, [sp, #12]
	add	r3, r9, r3
	uxth	r3, r3
	cmp	r2, r3
	strh	r3, [r6, #2]	@ movhi
	bhi	.L3241
	ldr	r3, [r5, #1732]
	cmp	r3, #0
	beq	.L3242
	bl	Ftl_gc_temp_data_write_back
	cmp	r0, #0
	movne	r3, #0
	strne	r3, [r5, #-1560]
	bne	.L3322
.L3242:
	ldr	r3, .L3323+56
	ldrh	r7, [r3]
	cmp	r7, #0
	bne	.L3243
	ldrh	r3, [r6]
	ldr	r2, [r5, #-1404]
	lsl	r3, r3, #1
	ldrh	r3, [r2, r3]
	cmp	r3, #0
	beq	.L3243
.L3244:
	ldr	r3, [r5, #-1284]
	cmp	r7, r3
	bcs	.L3249
	mov	r2, #0
	add	r1, sp, #36
	mov	r0, r7
	bl	log2phys
	ldr	r0, [sp, #36]
	cmn	r0, #1
	beq	.L3245
	ubfx	r0, r0, #10, #16
	bl	P2V_block_in_plane
	ldrh	r3, [r6]
	cmp	r3, r0
	bne	.L3245
.L3249:
	ldr	r3, [r5, #-1284]
	cmp	r7, r3
	bcc	.L3243
	ldrh	r3, [r6]
	mov	r1, #0
	ldr	r2, [r5, #-1404]
	lsl	r3, r3, #1
	strh	r1, [r2, r3]	@ movhi
	ldrh	r0, [r6]
	bl	update_vpc_list
	bl	FtlCacheWriteBack
	bl	l2p_flush
	bl	FtlVpcTblFlush
.L3243:
	mvn	r3, #0
	strh	r3, [r6]	@ movhi
.L3241:
	ldr	r3, .L3323+24
	ldrh	r3, [r3]
	cmp	r3, #2
	bhi	.L3250
	ldr	r3, .L3323+64
	ldrh	r9, [r3]
	b	.L3251
.L3245:
	add	r7, r7, #1
	b	.L3244
.L3250:
	mov	r2, #0
	str	r2, [r5, #-1560]
	movw	r2, #1932
	ldrh	r0, [r5, r2]
	cmp	r0, #0
	addeq	r0, r3, #1
	b	.L3159
.L3255:
	mov	r0, #0
	b	.L3159
.L3324:
	.align	2
.L3323:
	.word	.LANCHOR2
	.word	.LANCHOR1
	.word	.LANCHOR2-1520
	.word	.LANCHOR2-1536
	.word	.LANCHOR2-1664
	.word	.LC158
	.word	.LANCHOR2+880
	.word	.LANCHOR2+980
	.word	.LANCHOR2+1152
	.word	.LANCHOR2-1712
	.word	.LC159
	.word	.LANCHOR2+1156
	.word	.LANCHOR2+884
	.word	.LANCHOR2-1524
	.word	.LANCHOR2+1934
	.word	.LANCHOR2-1732
	.word	.LANCHOR2-1666
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
	@ args = 0, pretend = 0, frame = 8
	@ frame_needed = 0, uses_anonymous_args = 0
	push	{r0, r1, r2, r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.save {r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.pad #12
	ldr	r4, .L3368
	ldr	r8, [r4, #-1280]
	cmp	r8, #0
	bne	.L3327
	ldr	r1, [r4, #-1512]
	cmp	r1, #0
	beq	.L3327
	ldrb	r3, [r4, #-2740]	@ zero_extendqisi2
	mov	r6, #0
	ldr	r5, [r4, #1940]
	mov	r9, #36
	ldr	r10, .L3368+4
	cmp	r3, #0
	ldr	r0, [r4, #-1480]
	ldrbne	r7, [r5, #8]	@ zero_extendqisi2
	moveq	r7, r8
	ldrb	r3, [r5, #9]	@ zero_extendqisi2
	subne	r7, r7, #1
	clzne	r7, r7
	lsrne	r7, r7, #5
	mov	r2, r7
	bl	FlashProgPages
.L3330:
	ldr	r3, [r4, #-1512]
	cmp	r6, r3
	bcc	.L3337
.L3349:
	mov	r3, #0
	str	r3, [r4, #-1512]
.L3327:
	mov	r0, #0
	add	sp, sp, #12
	@ sp needed
	pop	{r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L3337:
	mul	fp, r9, r6
	ldr	r3, [r4, #-1480]
	add	r0, r3, fp
	ldr	r3, [r3, fp]
	cmn	r3, #1
	bne	.L3331
	ldr	r10, .L3368+4
.L3332:
	ldr	r3, [r4, #-1512]
	cmp	r8, r3
	bcc	.L3347
	movw	r5, #16386
.L3350:
	ldr	r3, .L3368+8
	ldrh	r3, [r3]
	cmp	r3, #0
	beq	.L3349
	mov	r1, #1
	mov	r0, r1
	bl	ftl_do_gc
	subs	r5, r5, #1
	bne	.L3350
	b	.L3349
.L3331:
	ldr	r3, [r0, #4]
	cmp	r7, #0
	mov	r2, #1
	add	r1, sp, #4
	ldr	r0, [r0, #16]
	orrne	r3, r3, #-2147483648
	str	r3, [sp, #4]
	bl	log2phys
	ldr	r3, [r4, #-1480]
	add	fp, r3, fp
	ldr	r3, [fp, #12]
	ldr	r0, [r3, #12]
	cmn	r0, #1
	beq	.L3335
	ubfx	r0, r0, #10, #16
	bl	P2V_block_in_plane
	ldr	r2, [r4, #-1404]
	lsl	r3, r0, #1
	mov	fp, r0
	ldrh	r2, [r2, r3]
	cmp	r2, #0
	bne	.L3336
	mov	r1, r0
	mov	r0, r10
	bl	printk
.L3336:
	mov	r0, fp
	bl	decrement_vpc_count
.L3335:
	add	r6, r6, #1
	b	.L3330
.L3347:
	mov	r6, #36
	ldr	r3, [r4, #-1480]
	mul	r6, r6, r8
	mov	r9, #0
	mov	fp, #1
	mvn	r2, #0
	str	r2, [r3, r6]
.L3338:
	ldr	r2, [r4, #-1480]
	add	r3, r2, r6
	ldr	r2, [r2, r6]
	ldr	r0, [r3, #4]
	cmn	r2, #1
	beq	.L3342
	cmp	r7, #0
	mov	r2, #1
	orrne	r0, r0, #-2147483648
	add	r1, sp, #4
	str	r0, [sp, #4]
	ldr	r0, [r3, #16]
	bl	log2phys
	ldr	r3, [r4, #-1480]
	add	r6, r3, r6
	ldr	r3, [r6, #12]
	ldr	r0, [r3, #12]
	cmn	r0, #1
	beq	.L3345
	ubfx	r0, r0, #10, #16
	bl	P2V_block_in_plane
	ldr	r2, [r4, #-1404]
	lsl	r3, r0, #1
	mov	r6, r0
	ldrh	r2, [r2, r3]
	cmp	r2, #0
	bne	.L3346
	mov	r1, r0
	mov	r0, r10
	bl	printk
.L3346:
	mov	r0, r6
	bl	decrement_vpc_count
.L3345:
	add	r8, r8, #1
	b	.L3332
.L3342:
	ubfx	r0, r0, #10, #16
	bl	P2V_block_in_plane
	ldrh	r3, [r5]
	cmp	r3, r0
	bne	.L3339
	ldr	r1, [r4, #-1404]
	lsl	r3, r3, #1
	ldrh	r0, [r5, #4]
	ldrh	r2, [r1, r3]
	sub	r2, r2, r0
	strh	r2, [r1, r3]	@ movhi
	ldr	r3, .L3368+12
	strb	r9, [r5, #6]
	strh	r9, [r5, #4]	@ movhi
	ldrh	r3, [r3]
	strh	r3, [r5, #2]	@ movhi
.L3339:
	ldrh	r3, [r5, #4]
	cmp	r3, #0
	bne	.L3340
	mov	r0, r5
	bl	allocate_new_data_superblock
.L3340:
	ldr	r3, [r4, #1300]
	add	r3, r3, #1
	str	r3, [r4, #1300]
	ldr	r3, [r4, #-1480]
	add	r3, r3, r6
	ldr	r0, [r3, #4]
	ubfx	r0, r0, #10, #16
	bl	FtlGcMarkBadPhyBlk
	mov	r0, r5
	bl	get_new_active_ppa
	ldr	r3, [r4, #-1480]
	mov	r2, r0
	str	r0, [sp, #4]
	mov	r1, #1
	add	r0, r3, r6
	str	r2, [r0, #4]
	mov	r2, r7
	ldrb	r3, [r5, #9]	@ zero_extendqisi2
	bl	FlashProgPages
	ldr	r3, [r4, #-1480]
	ldr	r3, [r3, r6]
	cmn	r3, #1
	streq	fp, [r4, #-1280]
	ldr	r3, [r4, #-1280]
	cmp	r3, #0
	beq	.L3338
	b	.L3327
.L3369:
	.align	2
.L3368:
	.word	.LANCHOR2
	.word	.LC160
	.word	.LANCHOR2-1522
	.word	.LANCHOR2-1666
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
	ldr	r3, .L3376
	ldr	r3, [r3, #-1280]
	cmp	r3, #0
	bne	.L3373
	ldr	r3, .L3376+4
	push	{r4, lr}
	.save {r4, lr}
	ldr	r4, [r3, #3440]
	cmp	r4, #1
	bne	.L3371
	bl	FtlCacheWriteBack
	bl	l2p_flush
	mov	r0, r4
	bl	FtlEctTblFlush
	bl	FtlVpcTblFlush
.L3371:
	mov	r0, #0
	pop	{r4, pc}
.L3373:
	mov	r0, #0
	bx	lr
.L3377:
	.align	2
.L3376:
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
	ldr	r3, .L3384
	ldr	r3, [r3, #3440]
	cmp	r3, #1
	bne	.L3381
	push	{r4, lr}
	.save {r4, lr}
	bl	FtlSysFlush
	mov	r0, #0
	pop	{r4, pc}
.L3381:
	mov	r0, #0
	bx	lr
.L3385:
	.align	2
.L3384:
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
	.global	ftl_discard
	.syntax unified
	.arm
	.fpu softvfp
	.type	ftl_discard, %function
ftl_discard:
	.fnstart
	@ args = 0, pretend = 0, frame = 8
	@ frame_needed = 0, uses_anonymous_args = 0
	push	{r0, r1, r2, r4, r5, r6, r7, r8, r9, lr}
	.save {r4, r5, r6, r7, r8, r9, lr}
	.pad #12
	ldr	r4, .L3406
	ldr	r3, [r4, #-2736]
	cmp	r3, r1
	cmpcs	r3, r0
	bls	.L3398
	add	r2, r0, r1
	mov	r7, r0
	cmp	r3, r2
	mov	r5, r1
	bcc	.L3398
	cmp	r1, #31
	bhi	.L3391
.L3392:
	mov	r0, #0
.L3389:
	add	sp, sp, #12
	@ sp needed
	pop	{r4, r5, r6, r7, r8, r9, pc}
.L3391:
	ldr	r3, [r4, #-1280]
	cmp	r3, #0
	bne	.L3392
	sub	r9, r4, #1648
	bl	FtlCacheWriteBack
	ldrh	r6, [r9, #-12]
	mov	r0, r7
	mov	r1, r6
	bl	__aeabi_uidiv
	smulbb	r3, r0, r6
	mov	r8, r0
	sub	r7, r7, r3
	uxth	r7, r7
	cmp	r7, #0
	beq	.L3393
	sub	r6, r6, r7
	add	r8, r0, #1
	cmp	r6, r5
	movcs	r6, r5
	uxth	r6, r6
	sub	r5, r5, r6
.L3393:
	mvn	r3, #0
	str	r3, [sp, #4]
.L3394:
	ldrh	r3, [r9, #-12]
	cmp	r5, r3
	bcs	.L3396
	ldr	r3, [r4, #1944]
	cmp	r3, #32
	bls	.L3392
	mov	r5, #0
	str	r5, [r4, #1944]
	bl	l2p_flush
	bl	FtlVpcTblFlush
	b	.L3392
.L3396:
	mov	r2, #0
	mov	r1, sp
	mov	r0, r8
	bl	log2phys
	ldr	r3, [sp]
	cmn	r3, #1
	beq	.L3395
	ldr	r3, [r4, #1944]
	mov	r2, #1
	add	r1, sp, #4
	mov	r0, r8
	add	r3, r3, #1
	str	r3, [r4, #1944]
	ldr	r3, [r4, #-1596]
	add	r3, r3, #1
	str	r3, [r4, #-1596]
	bl	log2phys
	ldr	r0, [sp]
	ubfx	r0, r0, #10, #16
	bl	P2V_block_in_plane
	bl	decrement_vpc_count
.L3395:
	ldrh	r3, [r9, #-12]
	add	r8, r8, #1
	sub	r5, r5, r3
	b	.L3394
.L3398:
	mvn	r0, #0
	b	.L3389
.L3407:
	.align	2
.L3406:
	.word	.LANCHOR2
	.fnend
	.size	ftl_discard, .-ftl_discard
	.align	2
	.global	FtlGcFreeTempBlock
	.syntax unified
	.arm
	.fpu softvfp
	.type	FtlGcFreeTempBlock, %function
FtlGcFreeTempBlock:
	.fnstart
	@ args = 0, pretend = 0, frame = 8
	@ frame_needed = 0, uses_anonymous_args = 0
	push	{r0, r1, r2, r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.save {r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.pad #12
	ldr	r4, .L3447
	ldr	ip, [r4, #-1280]
	sub	r7, r4, #1664
	ldrh	r1, [r7, #-2]
	cmp	ip, #0
	beq	.L3409
.L3445:
	mov	r0, #0
.L3408:
	add	sp, sp, #12
	@ sp needed
	pop	{r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L3409:
	add	r5, r4, #980
	movw	lr, #65535
	ldrh	r6, [r5]
	cmp	r6, lr
	bne	.L3411
.L3420:
	ldrh	r2, [r5]
	movw	r3, #65535
	mov	r6, #0
	str	r6, [r4, #1748]
	cmp	r2, r3
	beq	.L3445
	bl	FtlCacheWriteBack
	ldrb	r2, [r4, #987]	@ zero_extendqisi2
	mov	r10, #12
	ldrh	r0, [r7, #-2]
	ldrh	r3, [r5]
	ldr	r1, [r4, #-1404]
	ldr	r9, .L3447+4
	smulbb	r2, r2, r0
	lsl	r3, r3, #1
	strh	r2, [r1, r3]	@ movhi
	movw	r3, #1758
	ldr	r2, [r4, #-1604]
	ldrh	r3, [r4, r3]
	add	r3, r3, r2
	str	r3, [r4, #-1604]
.L3421:
	ldrh	r2, [r9]
	uxth	r3, r6
	cmp	r2, r3
	bhi	.L3425
	movw	r0, #65535
	bl	decrement_vpc_count
	ldrb	r3, [r4, #-2740]	@ zero_extendqisi2
	cmp	r3, #0
	beq	.L3426
	ldrh	r1, [r5]
	ldr	r0, .L3447+8
	bl	printk
.L3426:
	ldrh	r0, [r5]
	ldr	r2, [r4, #-1404]
	lsl	r3, r0, #1
	ldrh	r3, [r2, r3]
	cmp	r3, #0
	beq	.L3427
	bl	INSERT_DATA_LIST
.L3428:
	mvn	r6, #0
	movw	r3, #1758
	strh	r6, [r5]	@ movhi
	mov	r5, #0
	strh	r5, [r4, r3]	@ movhi
	movw	r3, #1756
	strh	r5, [r4, r3]	@ movhi
	bl	l2p_flush
	bl	FtlVpcTblFlush
	movw	r3, #1156
	strh	r6, [r4, r3]	@ movhi
	ldr	r3, [r4, #-1868]
	cmp	r3, r5
	ldr	r3, .L3447+12
	add	r2, r3, #272
	ldrh	r2, [r2]
	beq	.L3429
	ldr	r1, [r4, #-1564]
	cmp	r1, #39
	bhi	.L3429
	ldrh	r1, [r3]
	cmp	r1, r2
	subcc	r3, r3, #2400
	lslcc	r2, r2, #1
	bcs	.L3445
.L3446:
	strh	r2, [r3, #-8]	@ movhi
	b	.L3445
.L3411:
	cmp	r0, #0
	beq	.L3414
	ldr	r2, .L3447+16
	movw	r3, #3444
	ldrh	r0, [r2, r3]
	cmp	r0, lr
	beq	.L3415
.L3416:
	mov	r1, #2
.L3414:
	ldr	r0, .L3447+20
	bl	FtlGcScanTempBlk
	cmn	r0, #1
	str	r0, [sp, #4]
	beq	.L3417
	ldr	r2, [r4, #-1412]
	lsl	r6, r6, #1
	ldrh	r3, [r2, r6]
	cmp	r3, #4
	bls	.L3418
	sub	r3, r3, #5
	mov	r0, #1
	strh	r3, [r2, r6]	@ movhi
	bl	FtlEctTblFlush
.L3418:
	ldr	r3, [r4, #1748]
	cmp	r3, #0
	bne	.L3419
	ldr	r3, [r4, #1300]
	ldr	r0, [sp, #4]
	add	r3, r3, #1
	ubfx	r0, r0, #10, #16
	str	r3, [r4, #1300]
	bl	FtlBbmMapBadBlock
	bl	FtlBbmTblFlush
.L3419:
	mov	r3, #0
	str	r3, [r4, #1748]
.L3431:
	mov	r0, #1
	b	.L3408
.L3415:
	strh	ip, [r2, r3]	@ movhi
	add	r3, r4, #880
	ldrh	r3, [r3]
	cmp	r3, #17
	bhi	.L3416
	b	.L3414
.L3417:
	ldr	r2, .L3447+16
	movw	r3, #3444
	ldrh	r2, [r2, r3]
	movw	r3, #65535
	cmp	r2, r3
	bne	.L3431
	b	.L3420
.L3425:
	uxth	r8, r6
	ldr	fp, [r4, #-1504]
	ldr	r3, [r4, #-1284]
	mul	r8, r10, r8
	add	r7, fp, r8
	ldr	r0, [r7, #8]
	cmp	r0, r3
	bcc	.L3422
.L3443:
	ldrh	r0, [r5]
	b	.L3444
.L3422:
	mov	r2, #0
	add	r1, sp, #4
	bl	log2phys
	ldr	r0, [fp, r8]
	ldr	r3, [sp, #4]
	cmp	r0, r3
	bne	.L3424
	ubfx	r0, r0, #10, #16
	bl	P2V_block_in_plane
	mov	r2, #1
	mov	r8, r0
	add	r1, r7, #4
	ldr	r0, [r7, #8]
	bl	log2phys
	mov	r0, r8
.L3444:
	bl	decrement_vpc_count
	b	.L3423
.L3424:
	ldr	r2, [r7, #4]
	cmp	r3, r2
	bne	.L3443
.L3423:
	add	r6, r6, #1
	b	.L3421
.L3427:
	bl	INSERT_FREE_LIST
	b	.L3428
.L3429:
	ldrh	r3, [r3]
	add	r1, r2, r2, lsl #1
	cmp	r3, r1, asr #2
	ble	.L3445
	ldrb	r0, [r4, #-2740]	@ zero_extendqisi2
	ldr	r3, .L3447+24
	cmp	r0, #0
	moveq	r2, #20
	strheq	r2, [r3, #-8]	@ movhi
	beq	.L3408
	sub	r2, r2, #2
	b	.L3446
.L3448:
	.align	2
.L3447:
	.word	.LANCHOR2
	.word	.LANCHOR2+1758
	.word	.LC161
	.word	.LANCHOR2+880
	.word	.LANCHOR1
	.word	.LANCHOR2+980
	.word	.LANCHOR2-1520
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
	push	{r4, r5, r6, lr}
	.save {r4, r5, r6, lr}
	ldr	r4, .L3452
	sub	r5, r4, #1664
	add	r6, r4, #980
	ldrh	r1, [r5, #-2]
	mov	r0, r6
	bl	FtlGcScanTempBlk
	ldrh	r2, [r6, #2]
	ldrh	r3, [r5, #-2]
	cmp	r2, r3
	popcc	{r4, r5, r6, pc}
	add	r0, r6, #48
	bl	FtlMapBlkWriteDumpData
	mov	r0, #0
	bl	FtlGcFreeTempBlock
	mov	r3, #0
	str	r3, [r4, #1748]
	pop	{r4, r5, r6, pc}
.L3453:
	.align	2
.L3452:
	.word	.LANCHOR2
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
	ldr	r4, .L3456
	add	r6, r4, #884
	str	r5, [r4, #1800]
	mov	r0, r6
	add	r4, r4, #932
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
.L3457:
	.align	2
.L3456:
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
	push	{r4, r5, r6, r7, r8, lr}
	.save {r4, r5, r6, r7, r8, lr}
	mov	r2, #0
	ldr	r4, .L3476
	movw	r3, #1796
	strh	r2, [r4, r3]	@ movhi
	mvn	r2, #0
	movw	r3, #1794
	strh	r2, [r4, r3]	@ movhi
	sub	r3, r4, #1728
	ldrh	r0, [r3]
	bl	FtlFreeSysBlkQueueInit
	bl	FtlScanSysBlk
	movw	r3, #1128
	ldrh	r2, [r4, r3]
	movw	r3, #65535
	cmp	r2, r3
	bne	.L3459
.L3461:
	mvn	r7, #0
.L3458:
	mov	r0, r7
	pop	{r4, r5, r6, r7, r8, pc}
.L3459:
	bl	FtlLoadSysInfo
	subs	r7, r0, #0
	bne	.L3461
	bl	FtlLoadMapInfo
	bl	FtlLoadVonderInfo
	bl	Ftl_load_ext_data
	bl	FtlLoadEctTbl
	bl	FtlFreeSysBLkSort
	bl	SupperBlkListInit
	bl	FtlPowerLostRecovery
	mov	r0, #1
	bl	FtlUpdateVaildLpn
	ldr	r2, [r4, #-1364]
	sub	r3, r4, #1616
	ldrh	r1, [r3, #-10]
	mov	r0, #12
	mov	r3, r7
.L3462:
	cmp	r3, r1
	bge	.L3467
	mla	ip, r0, r3, r2
	ldr	ip, [ip, #4]
	cmp	ip, #0
	bge	.L3463
.L3467:
	ldr	r5, .L3476+4
	cmp	r3, r1
	ldrh	r2, [r5, #28]
	add	r6, r5, #68
	add	r2, r2, #1
	strh	r2, [r5, #28]	@ movhi
	bge	.L3474
.L3464:
	ldrh	r3, [r6]
	ldr	r1, [r4, #-1404]
	ldrh	r0, [r6, #4]
	ldr	ip, .L3476+8
	lsl	r3, r3, #1
	ldrh	r2, [r1, r3]
	sub	r2, r2, r0
	strh	r2, [r1, r3]	@ movhi
	mov	r2, #0
	ldrh	r3, [ip, #-2]
	ldr	lr, [r4, #-1404]
	strb	r2, [r4, #890]
	strh	r3, [r6, #2]	@ movhi
	ldr	r3, .L3476+12
	strh	r2, [r6, #4]	@ movhi
	ldrh	r1, [r3]
	ldrh	r8, [r3, #4]
	lsl	r1, r1, #1
	ldrh	r0, [lr, r1]
	sub	r0, r0, r8
	strh	r0, [lr, r1]	@ movhi
	ldrh	r1, [ip, #-2]
	strh	r2, [r3, #4]	@ movhi
	strb	r2, [r4, #938]
	strh	r1, [r3, #2]	@ movhi
	ldrh	r3, [r5, #30]
	add	r3, r3, #1
	strh	r3, [r5, #30]	@ movhi
	bl	l2p_flush
	bl	FtlVpcTblFlush
	bl	FtlVpcTblFlush
	b	.L3468
.L3463:
	add	r3, r3, #1
	b	.L3462
.L3474:
	movw	r3, #1796
	ldrh	r3, [r4, r3]
	cmp	r3, #0
	bne	.L3464
.L3468:
	ldrh	r0, [r6]
	movw	r3, #65535
	cmp	r0, r3
	beq	.L3469
	ldrh	r3, [r6, #4]
	cmp	r3, #0
	bne	.L3469
	ldr	r4, .L3476+12
	ldrh	r3, [r4, #4]
	cmp	r3, #0
	bne	.L3469
	bl	FtlGcRefreshOpenBlock
	ldrh	r0, [r4]
	bl	FtlGcRefreshOpenBlock
	bl	FtlVpcTblFlush
	sub	r0, r4, #48
	bl	allocate_new_data_superblock
	mov	r0, r4
	bl	allocate_new_data_superblock
.L3469:
	ldr	r3, .L3476+16
	ldrb	r3, [r3, #36]	@ zero_extendqisi2
	cmp	r3, #0
	bne	.L3470
	ldrh	r3, [r5, #28]
	tst	r3, #31
	bne	.L3458
.L3470:
	bl	FtlVpcCheckAndModify
	b	.L3458
.L3477:
	.align	2
.L3476:
	.word	.LANCHOR2
	.word	.LANCHOR2+816
	.word	.LANCHOR2-1664
	.word	.LANCHOR2+932
	.word	.LANCHOR0
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
	ldr	r4, .L3510
	ldr	r5, [r4, #-1280]
	cmp	r5, #0
	bne	.L3480
	sub	r6, r4, #1616
	mov	r1, r5
	ldrh	r2, [r6, #-12]
	ldr	r0, [r4, #-1372]
	lsl	r2, r2, #2
	bl	ftl_memset
	ldrh	r2, [r6, #-12]
	mov	r1, r5
	ldr	r0, [r4, #-1376]
	lsl	r2, r2, #2
	bl	ftl_memset
	sub	r3, r4, #1728
	str	r5, [r4, #-1612]
	ldrh	r0, [r3]
	str	r5, [r4, #-1608]
	bl	FtlFreeSysBlkQueueInit
	bl	FtlLoadBbt
	cmp	r0, #0
	beq	.L3481
	bl	FtlMakeBbt
.L3481:
	ldr	r0, .L3510+4
	mov	r2, #0
.L3482:
	ldr	r7, .L3510+8
	uxth	r3, r2
	add	r2, r2, #1
	ldrh	r1, [r7]
	cmp	r3, r1, lsl #7
	blt	.L3483
	sub	r7, r7, #52
	ldrh	r6, [r7, #-12]
	mov	r5, #0
.L3484:
	ldrh	r3, [r7, #-10]
	cmp	r3, r6
	bhi	.L3485
	ldr	r10, .L3510+12
	sub	r3, r5, #3
	ldrh	r1, [r10, #-4]
	cmp	r3, r1, lsl #1
	blt	.L3486
	mov	r0, r5
	mov	r5, #0
	bl	__aeabi_uidiv
	ldr	r3, [r4, #-1632]
	add	r0, r0, r3
	uxth	r0, r0
	bl	FtlSysBlkNumInit
	sub	r3, r4, #1728
	ldrh	r0, [r3]
	bl	FtlFreeSysBlkQueueInit
	ldrh	r6, [r7, #-12]
.L3487:
	ldrh	r3, [r7, #-10]
	cmp	r3, r6
	bhi	.L3488
.L3486:
	mov	r6, #0
	mov	r8, r6
.L3489:
	ldrh	r3, [r7, #-12]
	uxth	r0, r6
	add	r6, r6, #1
	cmp	r3, r0
	bhi	.L3490
	ldrh	r3, [r7, #-10]
	ldr	r2, [r4, #-1720]
	ldrh	r6, [r10, #-4]
	str	r3, [r4, #-1540]
	mov	r0, r2
	str	r2, [sp, #4]
	mov	r1, r6
	bl	__aeabi_uidiv
	ldr	r3, .L3510+16
	ubfx	r10, r0, #5, #16
	add	r1, r10, #36
	mov	fp, r0
	str	r0, [r4, #-1284]
	strh	r1, [r3]	@ movhi
	mov	r1, #24
	mul	r1, r1, r6
	mov	r9, r3
	cmp	r8, r1
	ble	.L3491
	ldr	r2, [sp, #4]
	mov	r1, r6
	sub	r0, r2, r8
	bl	__aeabi_uidiv
	str	r0, [r4, #-1284]
	lsr	r0, r0, #5
	add	r0, r0, #24
	strh	r0, [r9]	@ movhi
.L3491:
	ldr	r2, [r4, #-1868]
	cmp	r2, #1
	bne	.L3492
	ldrh	r2, [r9]
	mov	r1, r6
	mov	r0, r8
	str	r2, [sp, #4]
	bl	__aeabi_uidiv
	ldr	r2, [sp, #4]
	uxtah	r0, r2, r0
	add	r2, r2, r0, asr #2
	strh	r2, [r9]	@ movhi
.L3492:
	ldrb	r2, [r4, #-2740]	@ zero_extendqisi2
	cmp	r2, #0
	beq	.L3493
	ldrh	r2, [r9]
	mov	r1, r6
	mov	r0, r8
	str	r2, [sp, #4]
	bl	__aeabi_uidiv
	ldr	r2, [sp, #4]
	uxtah	r0, r2, r0
	add	r2, r2, r0, asr #2
	strh	r2, [r9]	@ movhi
.L3493:
	ldr	r1, .L3510+20
	ldrh	r2, [r1, #-8]
	cmp	r2, #0
	beq	.L3495
	ldrh	r0, [r9]
	add	r0, r0, r2, lsr #1
	strh	r0, [r9]	@ movhi
	mul	r0, r6, r2
	cmp	r8, r0
	addlt	r2, r2, #32
	strlt	fp, [r4, #-1284]
	addlt	r2, r10, r2
	strhlt	r2, [r9]	@ movhi
.L3495:
	ldrh	r2, [r9]
	ldr	r3, [r4, #-1284]
	sub	r3, r3, r2
	mul	r6, r6, r3
	ldrh	r3, [r1, #-2]
	str	r6, [r4, #1148]
	mul	r6, r6, r3
	ldr	r3, .L3510+24
	ldrh	r3, [r3, #-12]
	str	r6, [r4, #-1284]
	mul	r6, r6, r3
	str	r6, [r4, #-2736]
	bl	FtlBbmTblFlush
	ldr	r3, .L3510+24
	add	r1, r5, r8
	ldr	r2, [r4, #-1716]
	ldrh	r3, [r3, #-4]
	add	r3, r3, r2, lsr #3
	cmp	r1, r3
	bls	.L3497
	lsr	r2, r2, #5
	ldr	r0, .L3510+28
	bl	printk
.L3497:
	ldrh	r2, [r7, #-10]
	mov	r1, #0
	ldr	r5, .L3510+32
	mvn	r6, #0
	ldr	r0, [r4, #-1404]
	lsl	r2, r2, #1
	bl	ftl_memset
	mov	r3, #0
	movw	r2, #1156
	strh	r3, [r5, #2]	@ movhi
	sub	r5, r5, #272
	str	r3, [r4, #1124]
	mov	r1, #255
	strh	r6, [r4, r2]	@ movhi
	strb	r3, [r4, #1162]
	ldrh	r2, [r7, #-12]
	mov	r7, r5
	strb	r3, [r4, #1164]
	strh	r3, [r5, #2]	@ movhi
	strb	r3, [r4, #890]
	strh	r3, [r5]	@ movhi
	mov	r3, #1
	strb	r3, [r4, #892]
	lsr	r2, r2, #3
	ldr	r3, .L3510+36
	ldr	r0, [r3, #32]
	bl	ftl_memset
.L3498:
	mov	r0, r7
	bl	make_superblock
	ldrb	r3, [r4, #891]	@ zero_extendqisi2
	cmp	r3, #0
	ldrh	r3, [r5]
	bne	.L3499
	ldr	r2, [r4, #-1404]
	lsl	r3, r3, #1
	strh	r6, [r2, r3]	@ movhi
	ldrh	r3, [r5]
	add	r3, r3, #1
	strh	r3, [r5]	@ movhi
	b	.L3498
.L3483:
	ldr	ip, [r4, #-1452]
	mvn	r1, r3
	orr	r1, r3, r1, lsl #16
	str	r1, [ip, r3, lsl #2]
	ldr	r1, [r4, #-1448]
	str	r0, [r1, r3, lsl #2]
	b	.L3482
.L3485:
	mov	r0, r6
	mov	r1, #1
	bl	FtlLowFormatEraseBlock
	add	r6, r6, #1
	add	r5, r5, r0
	uxth	r5, r5
	uxth	r6, r6
	b	.L3484
.L3488:
	mov	r0, r6
	mov	r1, #1
	bl	FtlLowFormatEraseBlock
	add	r6, r6, #1
	add	r5, r5, r0
	uxth	r5, r5
	uxth	r6, r6
	b	.L3487
.L3490:
	mov	r1, #0
	bl	FtlLowFormatEraseBlock
	add	r8, r8, r0
	uxth	r8, r8
	b	.L3489
.L3499:
	ldr	r2, [r4, #-1612]
	lsl	r3, r3, #1
	ldrh	r1, [r5, #4]
	mvn	r6, #0
	str	r2, [r4, #896]
	add	r2, r2, #1
	str	r2, [r4, #-1612]
	ldr	r2, [r4, #-1404]
	strh	r1, [r2, r3]	@ movhi
	mov	r2, #0
	ldr	r3, .L3510+40
	strb	r2, [r4, #938]
	strh	r2, [r3, #2]	@ movhi
	mov	r7, r3
	ldrh	r2, [r5]
	mov	r5, r3
	add	r2, r2, #1
	strh	r2, [r3]	@ movhi
	mov	r2, #1
	strb	r2, [r4, #940]
.L3500:
	mov	r0, r7
	bl	make_superblock
	ldrb	r3, [r4, #939]	@ zero_extendqisi2
	cmp	r3, #0
	ldrh	r3, [r5]
	bne	.L3501
	ldr	r2, [r4, #-1404]
	lsl	r3, r3, #1
	strh	r6, [r2, r3]	@ movhi
	ldrh	r3, [r5]
	add	r3, r3, #1
	strh	r3, [r5]	@ movhi
	b	.L3500
.L3501:
	ldr	r2, [r4, #-1612]
	lsl	r3, r3, #1
	ldrh	r1, [r5, #4]
	mvn	r6, #0
	ldr	r5, .L3510+44
	str	r2, [r4, #944]
	add	r2, r2, #1
	str	r2, [r4, #-1612]
	ldr	r2, [r4, #-1404]
	strh	r1, [r2, r3]	@ movhi
	strh	r6, [r5], #148	@ movhi
	bl	FtlFreeSysBlkQueueOut
	movw	r3, #1128
	strh	r6, [r5, #4]	@ movhi
	strh	r0, [r4, r3]	@ movhi
	mov	r3, #0
	strh	r3, [r5, #2]	@ movhi
	ldr	r3, [r4, #1148]
	strh	r3, [r5, #6]	@ movhi
	ldr	r3, [r4, #-1612]
	str	r3, [r4, #1136]
	add	r3, r3, #1
	str	r3, [r4, #-1612]
	bl	FtlVpcTblFlush
	bl	FtlSysBlkInit
	cmp	r0, #0
	ldreq	r3, .L3510+48
	moveq	r2, #1
	streq	r2, [r3, #3440]
.L3480:
	mov	r0, #0
	add	sp, sp, #12
	@ sp needed
	pop	{r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L3511:
	.align	2
.L3510:
	.word	.LANCHOR2
	.word	168778952
	.word	.LANCHOR2-1660
	.word	.LANCHOR2-1728
	.word	.LANCHOR2+1152
	.word	.LANCHOR2-1664
	.word	.LANCHOR2-1648
	.word	.LC162
	.word	.LANCHOR2+1156
	.word	.LANCHOR0
	.word	.LANCHOR2+932
	.word	.LANCHOR2+980
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
	push	{r4, r5, lr}
	.save {r4, r5, lr}
	.pad #20
	sub	sp, sp, #20
	ldr	r4, .L3548
	ldrb	r3, [r4, #-2740]	@ zero_extendqisi2
	cmp	r3, #0
	beq	.L3513
.L3515:
	mov	r5, #0
.L3512:
	mov	r0, r5
	add	sp, sp, #20
	@ sp needed
	pop	{r4, r5, pc}
.L3513:
	ldr	r3, .L3548+4
	ldr	r0, [r3]
	bl	FlashInit
	subs	r5, r0, #0
	bne	.L3515
	bl	FlashLoadFactorBbt
	cmp	r0, #0
	beq	.L3516
	bl	FlashMakeFactorBbt
.L3516:
	ldr	r0, [r4, #-1772]
	bl	FlashReadIdbDataRaw
	cmp	r0, #0
	beq	.L3517
	mov	r2, #16
	mov	r1, #0
	mov	r0, sp
	bl	FlashReadFacBbtData
	ldr	r1, [sp]
	mov	r3, #0
	mov	r2, r3
	mov	r0, #1
.L3519:
	ands	ip, r1, r0, lsl r2
	add	r2, r2, #1
	addne	r3, r3, #1
	cmp	r2, #16
	bne	.L3519
	cmp	r3, #6
	ldrls	r3, .L3548+8
	bls	.L3545
	mov	r2, #0
	mov	r0, #1
.L3523:
	ands	ip, r1, r0, lsl r2
	add	r2, r2, #1
	addne	r3, r3, #1
	cmp	r2, #24
	bne	.L3523
	cmp	r3, #17
	ldr	r3, .L3548+8
	movhi	r2, #36
.L3545:
	strb	r2, [r3, #37]
	ldr	r3, .L3548+8
	ldrb	r2, [r3, #37]	@ zero_extendqisi2
	ldr	r3, .L3548+12
	strh	r2, [r3, #26]	@ movhi
.L3517:
	ldr	r1, .L3548+16
	ldr	r0, .L3548+20
	bl	printk
	ldr	r0, .L3548+12
	bl	FtlConstantsInit
	bl	FtlVariablesInit
	ldr	r0, [r4, #-1728]
	mov	r4, #1
	uxth	r0, r0
	bl	FtlFreeSysBlkQueueInit
.L3525:
	bl	FtlLoadBbt
	cmp	r0, #0
	beq	.L3526
.L3547:
	bl	FtlLowFormat
	cmp	r4, #3
	mvnhi	r5, #0
	bhi	.L3512
.L3527:
	add	r4, r4, #1
	b	.L3525
.L3526:
	bl	FtlSysBlkInit
	cmp	r0, #0
	bne	.L3547
	ldr	r3, .L3548+24
	mov	r2, #1
	str	r2, [r3, #3440]
	b	.L3512
.L3549:
	.align	2
.L3548:
	.word	.LANCHOR2
	.word	RK29_NANDC_REG_BASE
	.word	.LANCHOR0
	.word	.LANCHOR2-2768
	.word	.LC145
	.word	.LC49
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
	ldr	r4, .L3566
	ldr	r3, [r4, #-1280]
	cmp	r3, #0
	beq	.L3551
.L3554:
	mov	r0, #0
	pop	{r4, r5, r6, pc}
.L3551:
	ldrb	r3, [r4, #-2740]	@ zero_extendqisi2
	cmp	r3, #0
	beq	.L3553
	ldr	r3, [r4, #1732]
	tst	r3, #1
	beq	.L3553
	add	r3, r4, #980
	ldrh	r3, [r3, #4]
	cmp	r3, #0
	bne	.L3554
.L3553:
	mov	r3, #0
	mov	r5, #0
	mov	r6, #36
	mov	r2, r3
	ldr	r1, [r4, #1732]
	ldr	r0, [r4, #-1496]
	bl	FlashProgPages
.L3555:
	ldr	r1, [r4, #1732]
	uxth	r3, r5
	cmp	r3, r1
	bcc	.L3557
	ldr	r0, [r4, #-1496]
	bl	FtlGcBufFree
	mov	r3, #0
	str	r3, [r4, #1732]
	ldr	r3, .L3566+4
	ldrh	r3, [r3, #4]
	cmp	r3, #0
	bne	.L3554
	mov	r0, #1
	bl	FtlGcFreeTempBlock
	b	.L3565
.L3557:
	mul	r3, r6, r3
	ldr	r2, [r4, #-1496]
	add	r5, r5, #1
	ldr	ip, [r2, r3]
	add	r1, r2, r3
	ldr	r0, [r1, #12]
	cmn	ip, #1
	bne	.L3556
	ldr	r1, .L3566+4
	mov	lr, #0
	ldr	r0, [r4, #-1404]
	ldrh	r2, [r1]
	lsl	r2, r2, #1
	strh	lr, [r0, r2]	@ movhi
	ldr	r2, [r4, #1300]
	strh	ip, [r1]	@ movhi
	add	r2, r2, #1
	str	r2, [r4, #1300]
	ldr	r2, [r4, #-1496]
	add	r3, r2, r3
	ldr	r0, [r3, #4]
	ubfx	r0, r0, #10, #16
	bl	FtlBbmMapBadBlock
	bl	FtlBbmTblFlush
	bl	FtlGcPageVarInit
.L3565:
	mov	r0, #1
	pop	{r4, r5, r6, pc}
.L3556:
	ldr	r2, [r0, #8]
	ldr	r1, [r1, #4]
	ldr	r0, [r0, #12]
	bl	FtlGcUpdatePage
	b	.L3555
.L3567:
	.align	2
.L3566:
	.word	.LANCHOR2
	.word	.LANCHOR2+980
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
	ldr	r3, .L3575
	movw	r2, #65535
	ldrh	r1, [r3]
	cmp	r1, r2
	beq	.L3569
	ldrh	r3, [r3, #4]
	cmp	r3, #0
	ldrne	r0, .L3575
	bne	.L3574
.L3569:
	push	{r4, r5, r6, lr}
	.save {r4, r5, r6, lr}
	mov	r4, #0
	ldr	r5, .L3575+4
	bl	FtlCacheWriteBack
	mov	r0, #0
	bl	FtlGcFreeTempBlock
	add	r0, r5, #980
	strb	r4, [r5, #988]
	bl	allocate_data_superblock
	movw	r3, #1756
	strh	r4, [r5, r3]	@ movhi
	movw	r3, #1758
	strh	r4, [r5, r3]	@ movhi
	bl	l2p_flush
	mov	r0, r4
	bl	FtlEctTblFlush
	bl	FtlVpcTblFlush
	pop	{r4, r5, r6, lr}
	ldr	r0, .L3575
.L3574:
	b	get_new_active_ppa
.L3576:
	.align	2
.L3575:
	.word	.LANCHOR2+980
	.word	.LANCHOR2
	.fnend
	.size	Ftl_get_new_temp_ppa, .-Ftl_get_new_temp_ppa
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
	ldr	ip, .L3621
	push	{r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.save {r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.pad #84
	sub	sp, sp, #84
	ldr	ip, [ip, #3440]
	cmp	ip, #1
	bne	.L3601
	cmp	r0, #16
	mov	r8, r3
	str	r2, [sp, #28]
	mov	r5, r1
	bne	.L3579
	mov	r2, r3
	ldr	r1, [sp, #28]
	add	r0, r5, #256
	bl	FtlVendorPartRead
	mov	r10, r0
.L3577:
	mov	r0, r10
	add	sp, sp, #84
	@ sp needed
	pop	{r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L3579:
	ldr	r4, .L3621+4
	ldr	r2, [sp, #28]
	ldr	r3, [r4, #-2736]
	cmp	r2, r3
	cmpls	r1, r3
	bcs	.L3601
	add	r2, r1, r2
	cmp	r3, r2
	str	r2, [sp, #44]
	bcc	.L3601
	sub	r3, r4, #1648
	mov	r0, r5
	ldrh	r6, [r3, #-12]
	mov	r1, r6
	bl	__aeabi_uidiv
	ldr	r3, [sp, #44]
	mov	r1, r6
	str	r0, [sp, #36]
	sub	r0, r3, #1
	bl	__aeabi_uidiv
	ldr	r3, [sp, #36]
	mov	r1, r0
	ldr	r2, [sp, #28]
	str	r0, [sp, #40]
	rsb	r3, r3, #1
	add	r3, r3, r0
	ldr	r0, [sp, #36]
	str	r3, [sp, #32]
	ldr	r3, [r4, #1720]
	add	r3, r3, r2
	ldr	r2, [sp, #32]
	str	r3, [r4, #1720]
	ldr	r3, [r4, #-1584]
	add	r3, r3, r2
	str	r3, [r4, #-1584]
	bl	FtlCacheMetchLpa
	cmp	r0, #0
	beq	.L3580
	bl	FtlCacheWriteBack
.L3580:
	ldr	r6, [sp, #36]
	mov	r3, #0
	ldr	r4, .L3621+4
	mov	r7, r3
	mov	r10, r3
	str	r3, [sp, #52]
	str	r3, [sp, #48]
.L3581:
	ldr	r3, [sp, #32]
	cmp	r3, #0
	bne	.L3598
	ldr	r3, .L3621+8
	ldrh	r3, [r3, #-2]
	cmp	r3, #0
	beq	.L3577
	mov	r1, #1
	ldr	r0, [sp, #32]
	bl	ftl_do_gc
	b	.L3577
.L3598:
	mov	r2, #0
	add	r1, sp, #76
	mov	r0, r6
	bl	log2phys
	ldr	r3, [sp, #76]
	cmn	r3, #1
	moveq	r9, #0
	beq	.L3583
	ldr	r2, [r4, #-1500]
	mov	r9, #36
	mla	r9, r9, r7, r2
	str	r3, [r9, #4]
	ldr	r3, [sp, #36]
	cmp	r6, r3
	bne	.L3587
	ldr	r3, [r4, #-1452]
	mov	r0, r5
	str	r3, [r9, #8]
	ldr	r3, .L3621+12
	ldrh	fp, [r3, #-12]
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
.L3588:
	ldr	r3, .L3621+12
	ldr	r2, [r4, #-1436]
	str	r6, [r9, #16]
	ldrh	r3, [r3, #-6]
	mul	r3, r7, r3
	add	r7, r7, #1
	bic	r3, r3, #3
	add	r3, r2, r3
	str	r3, [r9, #12]
	b	.L3586
.L3585:
	mla	r0, r0, r6, r9
	ldr	r2, [sp, #44]
	cmp	r5, r0
	movls	r3, #1
	movhi	r3, #0
	cmp	r2, r0
	movls	r3, #0
	cmp	r3, #0
	beq	.L3584
	sub	r0, r0, r5
	mov	r2, #512
	mov	r1, #0
	add	r0, r8, r0, lsl #9
	bl	ftl_memset
.L3584:
	add	r9, r9, #1
.L3583:
	ldr	r3, .L3621+16
	ldrh	r0, [r3]
	cmp	r9, r0
	bcc	.L3585
.L3586:
	ldr	r3, [sp, #32]
	add	r6, r6, #1
	subs	r3, r3, #1
	str	r3, [sp, #32]
	beq	.L3590
	ldr	r3, .L3621+20
	ldrh	r3, [r3]
	cmp	r7, r3, lsl #3
	bne	.L3581
.L3590:
	cmp	r7, #0
	beq	.L3581
	mov	r2, #0
	mov	r1, r7
	ldr	r0, [r4, #-1500]
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
.L3597:
	mov	r9, #36
	ldr	r3, [r4, #-1500]
	mul	r9, r9, fp
	ldr	r1, [sp, #36]
	add	r3, r3, r9
	ldr	r2, [r3, #16]
	cmp	r1, r2
	bne	.L3592
	ldr	r1, [r3, #8]
	ldr	r3, [r4, #-1452]
	cmp	r1, r3
	bne	.L3593
	ldr	r3, [sp, #60]
	mov	r0, r8
	ldr	r2, [sp, #64]
	add	r1, r1, r3
.L3620:
	bl	ftl_memcpy
.L3593:
	ldr	r3, [r4, #-1500]
	ldr	r2, [r3, r9]
	add	r1, r3, r9
	cmn	r2, #1
	ldreq	r3, [r4, #1276]
	moveq	r10, r2
	addeq	r3, r3, #1
	streq	r3, [r4, #1276]
	ldr	r3, [r1, #12]
	ldr	r2, [r1, #16]
	ldr	r3, [r3, #8]
	cmp	r2, r3
	beq	.L3595
	ldr	r3, [r4, #1276]
	add	r3, r3, #1
	str	r3, [r4, #1276]
	ldr	r2, [r1, #8]
	ldr	r3, [r1, #12]
	ldr	r0, [r2, #4]
	str	r0, [sp, #16]
	ldr	r2, [r2]
	ldr	r0, .L3621+24
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
.L3595:
	ldr	r3, [r4, #-1500]
	add	r2, r3, r9
	ldr	r3, [r3, r9]
	cmp	r3, #256
	bne	.L3596
	ldr	r0, [r2, #4]
	ubfx	r0, r0, #10, #16
	bl	P2V_block_in_plane
	bl	FtlGcRefreshBlock
.L3596:
	add	fp, fp, #1
	cmp	r7, fp
	bne	.L3597
	mov	r7, #0
	b	.L3581
.L3587:
	ldr	r3, [sp, #40]
	cmp	r6, r3
	bne	.L3589
	ldr	r3, [r4, #-1448]
	ldr	r1, [sp, #44]
	str	r3, [r9, #8]
	ldr	r3, .L3621+12
	ldrh	r2, [r3, #-12]
	mul	r3, r2, r6
	sub	r1, r1, r3
	cmp	r2, r1
	str	r1, [sp, #52]
	bne	.L3588
.L3619:
	sub	r3, r3, r5
	add	r3, r8, r3, lsl #9
	str	r3, [r9, #8]
	b	.L3588
.L3589:
	ldr	r3, .L3621+12
	ldrh	r3, [r3, #-12]
	mul	r3, r6, r3
	b	.L3619
.L3592:
	ldr	r1, [sp, #40]
	cmp	r1, r2
	bne	.L3593
	ldr	r1, [r3, #8]
	ldr	r3, [r4, #-1448]
	cmp	r1, r3
	bne	.L3593
	ldr	r3, .L3621+16
	ldr	r2, [sp, #68]
	ldrh	r0, [r3]
	ldr	r3, [sp, #40]
	mul	r0, r3, r0
	sub	r0, r0, r5
	add	r0, r8, r0, lsl #9
	b	.L3620
.L3601:
	mvn	r10, #0
	b	.L3577
.L3622:
	.align	2
.L3621:
	.word	.LANCHOR1
	.word	.LANCHOR2
	.word	.LANCHOR2-1520
	.word	.LANCHOR2-1648
	.word	.LANCHOR2-1660
	.word	.LANCHOR2-1732
	.word	.LC58
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
	ldr	r6, .L3642
	ldr	r4, .L3642+4
	ldr	r1, .L3642+8
	str	r3, [r6, #3440]
	mov	r3, #0
	ldr	r0, .L3642+12
	str	r3, [r4, #1948]
	str	r3, [r4, #-1280]
	bl	printk
	sub	r0, r4, #2768
	bl	FtlConstantsInit
	bl	FtlMemInit
	bl	FtlVariablesInit
	sub	r3, r4, #1728
	ldrh	r0, [r3]
	bl	FtlFreeSysBlkQueueInit
	bl	FtlLoadBbt
	cmp	r0, #0
	beq	.L3626
	ldr	r1, .L3642+16
	ldr	r0, .L3642+20
.L3641:
	bl	printk
.L3627:
	mov	r0, #0
	pop	{r4, r5, r6, r7, r8, pc}
.L3626:
	bl	FtlSysBlkInit
	subs	r5, r0, #0
	ldrne	r1, .L3642+16
	ldrne	r0, .L3642+24
	bne	.L3641
.L3628:
	mov	r1, #1
	str	r1, [r6, #3440]
	bl	ftl_do_gc
	add	r3, r4, #880
	ldrh	r7, [r3]
	mov	r6, r3
	cmp	r7, #15
	bhi	.L3629
	add	r8, r3, #276
	sub	r4, r4, #1536
.L3632:
	ldrh	r3, [r8]
	movw	r2, #65535
	cmp	r3, r2
	bne	.L3630
	ldrh	r2, [r4]
	cmp	r2, r3
	bne	.L3630
	and	r0, r5, #63
	bl	List_get_gc_head_node
	uxth	r0, r0
	bl	FtlGcRefreshBlock
.L3630:
	mov	r1, #1
	mov	r0, r1
	bl	ftl_do_gc
	mov	r1, #1
	mov	r0, #0
	bl	ftl_do_gc
	ldrh	r2, [r6]
	add	r3, r7, #2
	cmp	r2, r3
	bhi	.L3627
	add	r5, r5, #1
	cmp	r5, #4096
	bne	.L3632
	b	.L3627
.L3629:
	ldrb	r3, [r4, #-2740]	@ zero_extendqisi2
	cmp	r3, #0
	beq	.L3627
	mov	r4, #128
.L3634:
	mov	r1, #1
	mov	r0, r1
	bl	ftl_do_gc
	subs	r4, r4, #1
	bne	.L3634
	b	.L3627
.L3643:
	.align	2
.L3642:
	.word	.LANCHOR1
	.word	.LANCHOR2
	.word	.LC145
	.word	.LC49
	.word	.LANCHOR3+224
	.word	.LC163
	.word	.LC164
	.fnend
	.size	FtlInit, .-FtlInit
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
	ldr	r4, .L3712
	.pad #84
	sub	sp, sp, #84
	ldr	r3, [r4, #-1280]
	cmp	r3, #0
	bne	.L3685
	mov	r9, r2
	ldr	r2, .L3712+4
	ldr	r2, [r2, #3440]
	cmp	r2, #1
	movne	r0, r3
	bne	.L3644
	cmp	r0, #16
	mov	r7, r1
	bne	.L3646
	mov	r2, fp
	mov	r1, r9
	add	r0, r7, #256
	bl	FtlVendorPartWrite
.L3644:
	add	sp, sp, #84
	@ sp needed
	pop	{r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L3646:
	ldr	r3, [r4, #-2736]
	cmp	r9, r3
	cmpls	r1, r3
	bcs	.L3688
	add	r6, r1, r9
	cmp	r3, r6
	bcc	.L3688
	mov	r3, #2048
	mov	r0, r7
	str	r3, [r4, #1952]
	sub	r3, r4, #1648
	ldrh	r5, [r3, #-12]
	mov	r1, r5
	bl	__aeabi_uidiv
	mov	r1, r5
	str	r0, [sp]
	sub	r0, r6, #1
	bl	__aeabi_uidiv
	ldr	r2, [sp]
	cmp	r9, r5, lsl #1
	ldr	r3, [r4, #-1600]
	str	r0, [sp, #24]
	sub	r6, r0, r2
	ldr	r2, [r4, #-1512]
	add	r8, r6, #1
	add	r3, r3, r8
	str	r3, [r4, #-1600]
	ldr	r3, [r4, #1716]
	add	r3, r3, r9
	str	r3, [r4, #1716]
	movcs	r3, #1
	movcc	r3, #0
	cmp	r2, #0
	str	r3, [sp, #16]
	beq	.L3689
	mov	r3, #36
	mul	r3, r3, r2
	ldr	r2, [r4, #-1480]
	sub	r3, r3, #36
	add	r10, r2, r3
	ldr	r3, [sp]
	ldr	r2, [r10, #16]
	cmp	r3, r2
	strne	fp, [sp, #12]
	bne	.L3649
	ldr	r2, [r4, #-1592]
	mov	r1, r5
	mov	r0, r7
	add	r2, r2, #1
	str	r2, [r4, #-1592]
	ldr	r2, [r4, #1956]
	add	r2, r2, #1
	str	r2, [r4, #1956]
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
	bne	.L3650
	ldr	r3, [r4, #1956]
	cmp	r3, #2
	bgt	.L3650
.L3685:
	mov	r0, #0
	b	.L3644
.L3650:
	add	r3, fp, r8
	sub	r9, r9, r5
	str	r3, [sp, #12]
	add	r7, r7, r5
	ldr	r3, [sp]
	mov	r8, r6
	add	r3, r3, #1
	str	r3, [sp]
.L3649:
	mov	r3, #0
	str	r3, [r4, #1956]
.L3648:
	ldr	r1, [sp, #24]
	ldr	r0, [sp]
	bl	FtlCacheMetchLpa
	cmp	r0, #0
	beq	.L3651
	bl	FtlCacheWriteBack
.L3651:
	ldr	r5, .L3712+8
	ldr	r6, [sp]
	sub	r10, r5, #884
	str	r5, [r4, #1940]
	sub	r3, r10, #1648
	str	r3, [sp, #4]
.L3652:
	cmp	r8, #0
	bne	.L3680
	ldr	r3, [sp, #24]
	mov	r0, r8
	ldr	r2, [sp]
	sub	r1, r3, r2
	bl	ftl_do_gc
	ldr	r2, .L3712+12
	ldrh	r3, [r2]
	mov	r6, r2
	cmp	r3, #5
	bls	.L3681
	cmp	r3, #31
	bhi	.L3685
	ldr	r3, .L3712+16
	ldrb	r3, [r3, #36]	@ zero_extendqisi2
	cmp	r3, #0
	bne	.L3685
.L3681:
	ldr	r5, .L3712
	ldr	r7, .L3712+20
	sub	r4, r5, #1520
.L3684:
	ldrh	r2, [r7]
	movw	r3, #65535
	cmp	r2, r3
	bne	.L3683
	ldr	r3, .L3712+24
	ldrh	r3, [r3]
	cmp	r3, r2
	bne	.L3683
	ldrh	r2, [r4, #-14]
	cmp	r2, r3
	bne	.L3683
	and	r0, r8, #7
	bl	List_get_gc_head_node
	uxth	r0, r0
	bl	FtlGcRefreshBlock
.L3683:
	mov	r1, #1
	mov	r3, #128
	mov	r0, r1
	strh	r3, [r4, #-6]	@ movhi
	strh	r3, [r4, #-8]	@ movhi
	bl	ftl_do_gc
	mov	r1, #1
	mov	r0, #0
	bl	ftl_do_gc
	ldr	r3, [r5, #-1280]
	cmp	r3, #0
	bne	.L3685
	ldrh	r3, [r6]
	cmp	r3, #2
	bhi	.L3685
	add	r8, r8, #1
	cmp	r8, #256
	bne	.L3684
	b	.L3685
.L3689:
	str	fp, [sp, #12]
	b	.L3648
.L3680:
	ldrh	r1, [r5, #4]
	cmp	r1, #0
	bne	.L3653
	ldr	r2, .L3712+8
	ldr	r4, .L3712+4
	cmp	r5, r2
	bne	.L3654
	add	r0, r5, #48
	ldrh	fp, [r0, #4]
	cmp	fp, #0
	bne	.L3655
	bl	allocate_new_data_superblock
	str	fp, [r4, #3448]
.L3655:
	ldr	r0, .L3712+8
	bl	allocate_new_data_superblock
	ldr	r5, .L3712+8
	ldr	r2, [r4, #3448]
	add	r0, r5, #48
	cmp	r2, #0
	movne	r5, r0
.L3656:
	ldrh	r2, [r5, #4]
	cmp	r2, #0
	bne	.L3657
	mov	r0, r5
	bl	allocate_new_data_superblock
.L3657:
	str	r5, [r10, #1940]
.L3653:
	ldr	r1, [r10, #-1512]
	ldr	r2, [r10, #-1516]
	sub	r2, r2, r1
	ldrh	r1, [r5, #4]
	cmp	r2, r8
	movcs	r2, r8
	cmp	r1, r2
	movcc	r3, r1
	movcs	r3, r2
	str	r3, [sp, #36]
	mov	r3, #0
.L3710:
	str	r3, [sp, #20]
	ldr	r3, [sp, #20]
	ldr	r2, [sp, #36]
	cmp	r3, r2
	bne	.L3676
.L3659:
	ldr	r3, [sp, #20]
	ldr	r2, [r10, #-1512]
	ldr	r1, [r10, #-1516]
	sub	r8, r8, r3
	ldr	r3, [sp, #16]
	cmp	r2, r1
	orrcs	r3, r3, #1
	cmp	r3, #0
	bne	.L3677
	ldrh	r2, [r5, #4]
	cmp	r2, #0
	beq	.L3677
.L3679:
	mov	r3, #0
	str	r3, [sp, #16]
	b	.L3652
.L3654:
	str	r1, [r4, #3448]
	ldrh	r1, [r2, #4]
	cmp	r1, #0
	movne	r5, r2
	bne	.L3657
	mov	r0, r5
	bl	allocate_new_data_superblock
	b	.L3656
.L3676:
	ldrh	r2, [r5, #4]
	cmp	r2, #0
	beq	.L3659
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
	beq	.L3660
	ldr	r2, .L3712+28
	ldrh	r1, [r2]
	add	r2, r7, r9
	mls	r2, r1, r6, r2
	cmp	r1, r2
	bne	.L3659
.L3660:
	mov	r2, #0
	add	r1, sp, #40
	mov	r0, r6
	mov	fp, #36
	bl	log2phys
	mov	r0, r5
	bl	get_new_active_ppa
	ldr	r1, [r10, #-1512]
	ldr	ip, [r10, #-1480]
	ldr	r3, [sp, #4]
	mla	ip, fp, r1, ip
	ldrh	r2, [r3, #-6]
	str	r0, [ip, #4]
	mul	r0, r2, r1
	str	r6, [ip, #16]
	bic	r3, r0, #3
	str	r3, [sp, #28]
	ldr	r0, [sp, #28]
	ldr	r3, [r10, #-1428]
	str	r3, [sp, #32]
	add	r3, r3, r0
	str	r3, [sp, #8]
	str	r3, [ip, #12]
	ldr	r3, [sp, #4]
	ldrh	r0, [r3, #-8]
	mul	r1, r1, r0
	ldr	r0, [r10, #-1456]
	bic	r1, r1, #3
	add	r1, r0, r1
	ldr	r0, [sp, #8]
	str	r1, [ip, #8]
	mov	r1, #0
	bl	ftl_memset
	ldr	r3, [sp]
	cmp	r3, r6
	orreq	r4, r4, #1
	cmp	r4, #0
	beq	.L3661
	cmp	r3, r6
	bne	.L3662
	ldr	r3, [sp, #4]
	mov	r0, r7
	ldrh	r4, [r3, #-12]
	mov	r1, r4
	bl	__aeabi_uidivmod
	sub	r4, r4, r1
	mov	fp, r1
	cmp	r4, r9
	movcs	r4, r9
.L3663:
	ldr	r3, [sp, #4]
	ldrh	r2, [r3, #-12]
	cmp	r2, r4
	bne	.L3664
	ldr	r3, [sp]
	cmp	r3, r6
	mulne	r1, r4, r6
	ldrne	r3, [sp, #12]
	ldreq	r1, [sp, #12]
	subne	r1, r1, r7
	addne	r1, r3, r1, lsl #9
	ldr	r3, [sp, #16]
	cmp	r3, #0
	beq	.L3666
	ldr	r2, [r10, #-1512]
	mov	ip, #36
	ldr	r0, [r10, #-1480]
	mla	r2, ip, r2, r0
	str	r1, [r2, #8]
.L3667:
	ldr	r2, .L3712+32
	ldr	r3, [sp, #32]
	ldr	r1, [sp, #28]
	strh	r2, [r3, r1]	@ movhi
	ldr	r3, [sp, #8]
	ldr	r2, [r10, #-1608]
	str	r2, [r3, #4]
	add	r2, r2, #1
	cmn	r2, #1
	ldr	r3, [sp, #8]
	moveq	r2, #0
	str	r2, [r10, #-1608]
	ldr	r2, [sp, #40]
	str	r6, [r3, #8]
	add	r6, r6, #1
	str	r2, [r3, #12]
	ldrh	r2, [r5]
	strh	r2, [r3, #2]	@ movhi
	ldr	r2, [r10, #-1512]
	ldr	r3, [sp, #20]
	add	r2, r2, #1
	str	r2, [r10, #-1512]
	add	r3, r3, #1
	b	.L3710
.L3662:
	ldr	r3, [sp, #4]
	add	r4, r7, r9
	mov	fp, #0
	ldrh	r2, [r3, #-12]
	smulbb	r2, r2, r6
	sub	r4, r4, r2
	uxth	r4, r4
	b	.L3663
.L3666:
	ldr	r2, [r10, #-1480]
	mov	ip, #36
	ldr	r0, [r10, #-1512]
	ldr	r3, [sp, #4]
	mla	r0, ip, r0, r2
	ldrh	r2, [r3, #-8]
.L3711:
	ldr	r0, [r0, #8]
	b	.L3708
.L3664:
	ldr	r2, [sp, #40]
	cmn	r2, #1
	beq	.L3668
	ldr	r1, [r10, #-1480]
	mov	r0, #36
	str	r2, [sp, #48]
	ldr	r2, [r10, #-1512]
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
	ldreq	r2, [r10, #1276]
	addeq	r2, r2, #1
	streq	r2, [r10, #1276]
	beq	.L3671
	ldr	r3, [sp, #8]
	ldr	r2, [r3, #8]
	cmp	r6, r2
	beq	.L3671
	ldr	r2, [r10, #1276]
	ldr	r0, .L3712+36
	add	r2, r2, #1
	str	r2, [r10, #1276]
	mov	r2, r6
	ldr	r1, [r3, #8]
	bl	printk
.L3671:
	ldr	r3, [sp]
	lsl	r2, r4, #9
	cmp	r3, r6
	bne	.L3672
	ldr	r0, [r10, #-1480]
	mov	ip, #36
	ldr	r1, [r10, #-1512]
	mla	r1, ip, r1, r0
	ldr	r0, [r1, #8]
	ldr	r1, [sp, #12]
	add	r0, r0, fp, lsl #9
.L3708:
	bl	ftl_memcpy
	b	.L3667
.L3668:
	ldr	r2, [r10, #-1480]
	mov	r1, #36
	ldr	r0, [r10, #-1512]
	ldr	r3, [sp, #4]
	mla	r0, r1, r0, r2
	ldrh	r2, [r3, #-8]
	mov	r1, #0
	ldr	r0, [r0, #8]
	bl	ftl_memset
	b	.L3671
.L3672:
	ldr	r1, .L3712+28
	mov	lr, #36
	ldr	r0, [r10, #-1512]
	ldr	ip, [r10, #-1480]
	ldrh	r1, [r1]
	ldr	r3, [sp, #12]
	mla	r0, lr, r0, ip
	mul	r1, r6, r1
	sub	r1, r1, r7
	add	r1, r3, r1, lsl #9
	b	.L3711
.L3661:
	ldr	r3, [sp, #16]
	cmp	r3, #0
	beq	.L3673
	ldr	r2, [r10, #-1512]
	ldr	r1, [r10, #-1480]
	ldr	r3, [sp, #4]
	mla	fp, fp, r2, r1
	ldrh	r2, [r3, #-12]
	ldr	r3, [sp, #12]
	mul	r2, r6, r2
	sub	r2, r2, r7
	add	r2, r3, r2, lsl #9
	str	r2, [fp, #8]
	b	.L3667
.L3673:
	ldr	r3, [sp, #4]
	ldr	r2, [r10, #-1512]
	ldr	r0, [r10, #-1480]
	ldrh	r1, [r3, #-12]
	mla	fp, fp, r2, r0
	ldrh	r2, [r3, #-8]
	ldr	r3, [sp, #12]
	mul	r1, r6, r1
	ldr	r0, [fp, #8]
	sub	r1, r1, r7
	add	r1, r3, r1, lsl #9
	b	.L3708
.L3677:
	bl	FtlCacheWriteBack
	cmp	r8, #1
	mov	r2, #0
	str	r2, [r10, #-1512]
	bhi	.L3652
	b	.L3679
.L3688:
	mvn	r0, #0
	b	.L3644
.L3713:
	.align	2
.L3712:
	.word	.LANCHOR2
	.word	.LANCHOR1
	.word	.LANCHOR2+884
	.word	.LANCHOR2+880
	.word	.LANCHOR0
	.word	.LANCHOR2+1156
	.word	.LANCHOR2-1536
	.word	.LANCHOR2-1660
	.word	-3947
	.word	.LC165
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
	ldr	r4, .L3731
	ldrb	r3, [r4, #-2740]	@ zero_extendqisi2
	cmp	r3, #0
	beq	.L3716
	movw	r3, #1794
	add	r8, r4, #884
	ldrh	r6, [r4, r3]
	add	r5, r4, #932
	ldr	r3, [r4, #-1404]
	ldr	r0, .L3731+4
	mov	r1, r6
	lsl	r7, r6, #1
	ldrh	r2, [r3, r7]
	bl	printk
	ldrh	r0, [r8]
	bl	FtlGcRefreshOpenBlock
	ldrh	r0, [r5]
	bl	FtlGcRefreshOpenBlock
	mov	r0, r8
	bl	allocate_new_data_superblock
	mov	r0, r5
	movw	r5, #4097
	bl	allocate_new_data_superblock
.L3718:
	subs	r5, r5, #1
	beq	.L3722
	mov	r1, #1
	mov	r0, r1
	bl	ftl_do_gc
	ldr	r3, [r4, #-1404]
	ldrh	r3, [r3, r7]
	cmp	r3, #0
	bne	.L3718
.L3722:
	ldr	r3, [r4, #-1404]
	mov	r1, r6
	ldr	r0, .L3731+4
	ldrh	r2, [r3, r7]
	bl	printk
	ldr	r3, [r4, #-1404]
	ldrh	r5, [r3, r7]
	cmp	r5, #0
	bne	.L3720
	add	r0, sp, #48
	movw	r9, #65535
	strh	r6, [r0, #-48]!	@ movhi
	mov	r10, #36
	bl	make_superblock
	ldr	r3, .L3731+8
	add	r0, sp, #14
	ldr	r8, [r4, #-1488]
	mov	r2, r5
	mov	ip, r5
	ldrh	lr, [r3, #-4]
.L3723:
	uxth	r3, r2
	cmp	lr, r3
	bhi	.L3725
	ldr	r3, [r4, #-1404]
	mov	r1, r6
	ldr	r0, .L3731+12
	ldrh	r2, [r3, r7]
	bl	printk
	mov	r2, r5
	mov	r1, #0
	ldr	r0, [r4, #-1488]
	bl	FlashEraseBlocks
	mov	r2, r5
	mov	r1, #1
	ldr	r0, [r4, #-1488]
	bl	FlashEraseBlocks
.L3720:
	mvn	r2, #0
	movw	r3, #1794
	strh	r2, [r4, r3]	@ movhi
.L3716:
	add	sp, sp, #48
	@ sp needed
	pop	{r4, r5, r6, r7, r8, r9, r10, pc}
.L3725:
	ldrh	r3, [r0, #2]!
	add	r2, r2, #1
	cmp	r3, r9
	mlane	r1, r10, r5, r8
	lslne	r3, r3, #10
	addne	r5, r5, #1
	uxthne	r5, r5
	stmibne	r1, {r3, ip}
	strne	ip, [r1, #12]
	b	.L3723
.L3732:
	.align	2
.L3731:
	.word	.LANCHOR2
	.word	.LC166
	.word	.LANCHOR2-1728
	.word	.LC167
	.fnend
	.size	ftl_fix_nand_power_lost_error, .-ftl_fix_nand_power_lost_error
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
	.global	read_retry_cur_offset
	.section	.rodata
	.set	.LANCHOR3,. + 0
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
	.type	__func__.23800, %object
	.size	__func__.23800, 11
__func__.23800:
	.ascii	"FtlMemInit\000"
	.type	__func__.24547, %object
	.size	__func__.24547, 12
__func__.24547:
	.ascii	"FtlCheckVpc\000"
	.type	__func__.24579, %object
	.size	__func__.24579, 17
__func__.24579:
	.ascii	"FtlDumpBlockInfo\000"
	.type	__func__.24598, %object
	.size	__func__.24598, 16
__func__.24598:
	.ascii	"FtlScanAllBlock\000"
	.type	__func__.24866, %object
	.size	__func__.24866, 17
__func__.24866:
	.ascii	"ftl_scan_all_ppa\000"
	.type	__func__.24846, %object
	.size	__func__.24846, 21
__func__.24846:
	.ascii	"FtlVpcCheckAndModify\000"
	.type	__func__.23873, %object
	.size	__func__.23873, 8
__func__.23873:
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
	.type	gFtlInitStatus, %object
	.size	gFtlInitStatus, 4
gFtlInitStatus:
	.word	-1
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
	.type	slcPageToMlcPageTbl, %object
	.size	slcPageToMlcPageTbl, 1024
slcPageToMlcPageTbl:
	.space	1024
	.type	mlcPageToSlcPageTbl, %object
	.size	mlcPageToSlcPageTbl, 2048
mlcPageToSlcPageTbl:
	.space	2048
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
	.type	gTotleBlock, %object
	.size	gTotleBlock, 2
gTotleBlock:
	.space	2
	.space	2
	.type	gNandIDataBuf, %object
	.size	gNandIDataBuf, 2048
gNandIDataBuf:
	.space	2048
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
	.type	gNandFlashEccBits, %object
	.size	gNandFlashEccBits, 1
gNandFlashEccBits:
	.space	1
	.space	2
	.type	g_MaxLbaSector, %object
	.size	g_MaxLbaSector, 4
g_MaxLbaSector:
	.space	4
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
	.type	gFlashCurMode, %object
	.size	gFlashCurMode, 1
gFlashCurMode:
	.space	1
	.type	gFlashInterfaceMode, %object
	.size	gFlashInterfaceMode, 1
gFlashInterfaceMode:
	.space	1
	.type	gMultiPageProgEn, %object
	.size	gMultiPageProgEn, 1
gMultiPageProgEn:
	.space	1
	.space	1
	.type	g_inkDie_check_enable, %object
	.size	g_inkDie_check_enable, 4
g_inkDie_check_enable:
	.space	4
	.type	gFlashPageBuffer0, %object
	.size	gFlashPageBuffer0, 4
gFlashPageBuffer0:
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
	.type	gNandFlashIDBEccBits, %object
	.size	gNandFlashIDBEccBits, 1
gNandFlashIDBEccBits:
	.space	1
	.space	2
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
	.type	g_page_map_check_enable, %object
	.size	g_page_map_check_enable, 2
g_page_map_check_enable:
	.space	2
	.type	g_free_slc_blk_num, %object
	.size	g_free_slc_blk_num, 2
g_free_slc_blk_num:
	.space	2
	.type	g_SlcPartLbaEndSector, %object
	.size	g_SlcPartLbaEndSector, 4
g_SlcPartLbaEndSector:
	.space	4
	.type	g_all_blk_used_slc_mode, %object
	.size	g_all_blk_used_slc_mode, 4
g_all_blk_used_slc_mode:
	.space	4
	.type	g_GlobalSysVersion, %object
	.size	g_GlobalSysVersion, 4
g_GlobalSysVersion:
	.space	4
	.type	g_GlobalDataVersion, %object
	.size	g_GlobalDataVersion, 4
g_GlobalDataVersion:
	.space	4
	.type	g_totle_gc_page_count, %object
	.size	g_totle_gc_page_count, 4
g_totle_gc_page_count:
	.space	4
	.type	g_totle_write_page_count, %object
	.size	g_totle_write_page_count, 4
g_totle_write_page_count:
	.space	4
	.type	g_totle_discard_page_count, %object
	.size	g_totle_discard_page_count, 4
g_totle_discard_page_count:
	.space	4
	.type	g_totle_cache_write_count, %object
	.size	g_totle_cache_write_count, 4
g_totle_cache_write_count:
	.space	4
	.type	g_totle_l2p_write_count, %object
	.size	g_totle_l2p_write_count, 4
g_totle_l2p_write_count:
	.space	4
	.type	g_totle_read_page_count, %object
	.size	g_totle_read_page_count, 4
g_totle_read_page_count:
	.space	4
	.type	g_totle_mlc_erase_count, %object
	.size	g_totle_mlc_erase_count, 4
g_totle_mlc_erase_count:
	.space	4
	.type	g_totle_slc_erase_count, %object
	.size	g_totle_slc_erase_count, 4
g_totle_slc_erase_count:
	.space	4
	.type	g_totle_sys_slc_erase_count, %object
	.size	g_totle_sys_slc_erase_count, 4
g_totle_sys_slc_erase_count:
	.space	4
	.type	g_max_erase_count, %object
	.size	g_max_erase_count, 4
g_max_erase_count:
	.space	4
	.type	g_min_erase_count, %object
	.size	g_min_erase_count, 4
g_min_erase_count:
	.space	4
	.type	g_in_gc_progress, %object
	.size	g_in_gc_progress, 4
g_in_gc_progress:
	.space	4
	.type	g_in_swl_replace, %object
	.size	g_in_swl_replace, 4
g_in_swl_replace:
	.space	4
	.type	g_gc_head_data_block, %object
	.size	g_gc_head_data_block, 4
g_gc_head_data_block:
	.space	4
	.type	g_gc_head_data_block_count, %object
	.size	g_gc_head_data_block_count, 4
g_gc_head_data_block_count:
	.space	4
	.type	g_gc_skip_write_count, %object
	.size	g_gc_skip_write_count, 4
g_gc_skip_write_count:
	.space	4
	.type	g_cur_erase_blk, %object
	.size	g_cur_erase_blk, 4
g_cur_erase_blk:
	.space	4
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
	.type	g_gc_free_blk_threshold, %object
	.size	g_gc_free_blk_threshold, 2
g_gc_free_blk_threshold:
	.space	2
	.type	g_gc_merge_free_blk_threshold, %object
	.size	g_gc_merge_free_blk_threshold, 2
g_gc_merge_free_blk_threshold:
	.space	2
	.type	g_gc_blk_index, %object
	.size	g_gc_blk_index, 2
g_gc_blk_index:
	.space	2
	.type	g_gc_bad_block_temp_num, %object
	.size	g_gc_bad_block_temp_num, 2
g_gc_bad_block_temp_num:
	.space	2
	.type	g_gc_refresh_block_temp_num, %object
	.size	g_gc_refresh_block_temp_num, 2
g_gc_refresh_block_temp_num:
	.space	2
	.type	g_gc_bad_block_gc_index, %object
	.size	g_gc_bad_block_gc_index, 2
g_gc_bad_block_gc_index:
	.space	2
	.type	c_wr_page_buf_num, %object
	.size	c_wr_page_buf_num, 4
c_wr_page_buf_num:
	.space	4
	.type	g_wr_page_num, %object
	.size	g_wr_page_num, 4
g_wr_page_num:
	.space	4
	.type	p_gc_blk_tbl, %object
	.size	p_gc_blk_tbl, 4
p_gc_blk_tbl:
	.space	4
	.type	p_gc_page_info, %object
	.size	p_gc_page_info, 4
p_gc_page_info:
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
	.type	req_erase, %object
	.size	req_erase, 4
req_erase:
	.space	4
	.type	req_gc, %object
	.size	req_gc, 4
req_gc:
	.space	4
	.type	req_wr_io, %object
	.size	req_wr_io, 4
req_wr_io:
	.space	4
	.type	c_gc_page_buf_num, %object
	.size	c_gc_page_buf_num, 4
c_gc_page_buf_num:
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
	.type	p_gc_data_buf, %object
	.size	p_gc_data_buf, 4
p_gc_data_buf:
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
	.type	gp_gc_page_buf_info, %object
	.size	gp_gc_page_buf_info, 4
gp_gc_page_buf_info:
	.space	4
	.type	p_sys_spare_buf, %object
	.size	p_sys_spare_buf, 4
p_sys_spare_buf:
	.space	4
	.type	p_io_spare_buf, %object
	.size	p_io_spare_buf, 4
p_io_spare_buf:
	.space	4
	.type	p_gc_spare_buf, %object
	.size	p_gc_spare_buf, 4
p_gc_spare_buf:
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
	.type	p_erase_count_table, %object
	.size	p_erase_count_table, 4
p_erase_count_table:
	.space	4
	.type	p_valid_page_count_check_table, %object
	.size	p_valid_page_count_check_table, 4
p_valid_page_count_check_table:
	.space	4
	.type	p_valid_page_count_table, %object
	.size	p_valid_page_count_table, 4
p_valid_page_count_table:
	.space	4
	.type	p_map_block_table, %object
	.size	p_map_block_table, 4
p_map_block_table:
	.space	4
	.type	p_map_block_valid_page_count, %object
	.size	p_map_block_valid_page_count, 4
p_map_block_valid_page_count:
	.space	4
	.type	p_vendor_block_table, %object
	.size	p_vendor_block_table, 4
p_vendor_block_table:
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
	.type	p_l2p_ram_map, %object
	.size	p_l2p_ram_map, 4
p_l2p_ram_map:
	.space	4
	.type	p_l2p_map_buf, %object
	.size	p_l2p_map_buf, 4
p_l2p_map_buf:
	.space	4
	.type	p_data_block_list_table, %object
	.size	p_data_block_list_table, 4
p_data_block_list_table:
	.space	4
	.type	c_ftl_nand_bbm_buf_size, %object
	.size	c_ftl_nand_bbm_buf_size, 2
c_ftl_nand_bbm_buf_size:
	.space	2
	.space	2
	.type	gBbtInfo, %object
	.size	gBbtInfo, 60
gBbtInfo:
	.space	60
	.type	g_totle_vendor_block, %object
	.size	g_totle_vendor_block, 2
g_totle_vendor_block:
	.space	2
	.space	2
	.type	g_MaxLpn, %object
	.size	g_MaxLpn, 4
g_MaxLpn:
	.space	4
	.type	g_flash_read_only_en, %object
	.size	g_flash_read_only_en, 4
g_flash_read_only_en:
	.space	4
	.type	req_sys, %object
	.size	req_sys, 36
req_sys:
	.space	36
	.type	gSysFreeQueue, %object
	.size	gSysFreeQueue, 2056
gSysFreeQueue:
	.space	2056
	.type	g_sys_save_data, %object
	.size	g_sys_save_data, 48
g_sys_save_data:
	.space	48
	.type	p_data_block_list_head, %object
	.size	p_data_block_list_head, 4
p_data_block_list_head:
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
	.type	gL2pMapInfo, %object
	.size	gL2pMapInfo, 44
gL2pMapInfo:
	.space	44
	.type	g_l2p_last_update_region_id, %object
	.size	g_l2p_last_update_region_id, 2
g_l2p_last_update_region_id:
	.space	2
	.space	2
	.type	gVendorBlkInfo, %object
	.size	gVendorBlkInfo, 44
gVendorBlkInfo:
	.space	44
	.type	FtlUpdateVaildLpnCount, %object
	.size	FtlUpdateVaildLpnCount, 2
FtlUpdateVaildLpnCount:
	.space	2
	.space	2
	.type	g_VaildLpn, %object
	.size	g_VaildLpn, 4
g_VaildLpn:
	.space	4
	.type	gSysInfo, %object
	.size	gSysInfo, 16
gSysInfo:
	.space	16
	.type	g_totle_map_block, %object
	.size	g_totle_map_block, 2
g_totle_map_block:
	.space	2
	.space	2
	.type	g_MaxLbn, %object
	.size	g_MaxLbn, 4
g_MaxLbn:
	.space	4
	.type	c_ftl_nand_data_op_blks_per_plane, %object
	.size	c_ftl_nand_data_op_blks_per_plane, 2
c_ftl_nand_data_op_blks_per_plane:
	.space	2
	.space	2
	.type	g_gc_superblock, %object
	.size	g_gc_superblock, 48
g_gc_superblock:
	.space	48
	.type	g_sys_ext_data, %object
	.size	g_sys_ext_data, 512
g_sys_ext_data:
	.space	512
	.type	g_totle_write_sector, %object
	.size	g_totle_write_sector, 4
g_totle_write_sector:
	.space	4
	.type	g_totle_read_sector, %object
	.size	g_totle_read_sector, 4
g_totle_read_sector:
	.space	4
	.type	g_ect_tbl_power_up_flush, %object
	.size	g_ect_tbl_power_up_flush, 2
g_ect_tbl_power_up_flush:
	.space	2
	.space	2
	.type	g_totle_avg_erase_count, %object
	.size	g_totle_avg_erase_count, 4
g_totle_avg_erase_count:
	.space	4
	.type	g_gc_num_req, %object
	.size	g_gc_num_req, 4
g_gc_num_req:
	.space	4
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
	.type	g_gc_blk_num, %object
	.size	g_gc_blk_num, 2
g_gc_blk_num:
	.space	2
	.type	g_gc_page_offset, %object
	.size	g_gc_page_offset, 2
g_gc_page_offset:
	.space	2
	.type	g_gc_bad_block_temp_tbl, %object
	.size	g_gc_bad_block_temp_tbl, 34
g_gc_bad_block_temp_tbl:
	.space	34
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
	.type	g_ftl_nand_free_count, %object
	.size	g_ftl_nand_free_count, 4
g_ftl_nand_free_count:
	.space	4
	.type	last_cache_match_count, %object
	.size	last_cache_match_count, 4
last_cache_match_count:
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
	.ascii	"otp error! %d\000"
.LC3:
	.ascii	"rr\000"
.LC4:
	.ascii	"%d statReg->V6.mtrans_cnt=%d flReg.V6.page_num=%d\012"
	.ascii	"\000"
.LC5:
	.ascii	"nandc:\000"
.LC6:
	.ascii	"%d flReg.d32=%x %x\012\000"
.LC7:
	.ascii	"sdr read ok %x ecc=%d\012\000"
.LC8:
	.ascii	"sync para %d\012\000"
.LC9:
	.ascii	"TOG mode Read error %x %x\012\000"
.LC10:
	.ascii	"read retry status %x %x %x\012\000"
.LC11:
	.ascii	"micron RR %d row=%x,count %d,status=%d\012\000"
.LC12:
	.ascii	"samsung RR %d row=%x,count %d,status=%d\012\000"
.LC13:
	.ascii	"ECC:%d\012\000"
.LC14:
	.ascii	"No.%d FLASH ID:%x %x %x %x %x %x\012\000"
.LC15:
	.ascii	"FlashLoadPhyInfo fail %x!!\012\000"
.LC16:
	.ascii	"Read pageadd=%x  ecc=%x err=%x\012\000"
.LC17:
	.ascii	"data:\000"
.LC18:
	.ascii	"spare:\000"
.LC19:
	.ascii	"ReadRetry pageadd=%x ecc=%x err=%x\012\000"
.LC20:
	.ascii	"FLFB:%d %d\012\000"
.LC21:
	.ascii	"BBT:\000"
.LC22:
	.ascii	"prog error: = %x\012\000"
.LC23:
	.ascii	"prog read error: = %x\012\000"
.LC24:
	.ascii	"prog read REFRESH: = %x\012\000"
.LC25:
	.ascii	"prog read s error: = %x %x %x\012\000"
.LC26:
	.ascii	"prog read d error: = %x %x %x\012\000"
.LC27:
	.ascii	"FlashMakeFactorBbt %d\012\000"
.LC28:
	.ascii	"bad block:%d %d\012\000"
.LC29:
	.ascii	"FMFB:%d %d\012\000"
.LC30:
	.ascii	"E:bad block:%d\012\000"
.LC31:
	.ascii	"FMFB:Save %d %d\012\000"
.LC32:
	.ascii	"%s error allocating memory. return -1\012\000"
.LC33:
	.ascii	"phyBlk = 0x%x die = %d block_in_die = 0x%x 0x%8x\012"
	.ascii	"\000"
.LC34:
	.ascii	"FtlBbmTblFlush id=%x,page=%x,previd=%x cnt=%d\012\000"
.LC35:
	.ascii	"FtlBbmTblFlush error:%x\012\000"
.LC36:
	.ascii	"FtlBbmTblFlush error = %x error count = %d\012\000"
.LC37:
	.ascii	"FtlFreeSysBlkQueueOut free count = %d\012\000"
.LC38:
	.ascii	"FtlFreeSysBlkQueueOut = %x, free count = %d, error\012"
	.ascii	"\000"
.LC39:
	.ascii	"FtlFreeSysBlkQueueOut = %x, free count = %d\012\000"
.LC40:
	.ascii	"FtlMapWritePage error = %x\012\000"
.LC41:
	.ascii	"FtlMapWritePage error = %x error count = %d\012\000"
.LC42:
	.ascii	"page map lost: %x %x\012\000"
.LC43:
	.ascii	"region_id = %x phyAddr = %x\012\000"
.LC44:
	.ascii	"map_ppn:\000"
.LC45:
	.ascii	"load_l2p_region refresh = %x phyAddr = %x\012\000"
.LC46:
	.ascii	"FtlVendorPartRead refresh = %x phyAddr = %x\012\000"
.LC47:
	.ascii	"FtlVpcTblFlush error = %x error count = %d\012\000"
.LC48:
	.ascii	"no ect\000"
.LC49:
	.ascii	"%s\012\000"
.LC50:
	.ascii	"...%s enter...\012\000"
.LC51:
	.ascii	"FtlCheckVpc2 %x = %x  %x\012\000"
.LC52:
	.ascii	"free blk vpc error %x = %x  %x\012\000"
.LC53:
	.ascii	"error_flag %x\012\000"
.LC54:
	.ascii	"id = %x,%x addr= %x,spare= %x %x %x %x data = %x\012"
	.ascii	"\000"
.LC55:
	.ascii	":\000"
.LC56:
	.ascii	"Ftlscanalldata = %x\012\000"
.LC57:
	.ascii	"scan lpa = %x ppa= %x\012\000"
.LC58:
	.ascii	"lba = %x,addr= %x,spare= %x %x %x %x data=%x %x\012"
	.ascii	"\000"
.LC59:
	.ascii	"phyBlk = %x,addr= %x,spare= %x %x %x %x data=%x %x\012"
	.ascii	"\000"
.LC60:
	.ascii	"id = %x,%x addr= %x,spare= %x %x %x %x data=%x %x\012"
	.ascii	"\000"
.LC61:
	.ascii	"Mblk:\000"
.LC62:
	.ascii	"L2P:\000"
.LC63:
	.ascii	"L2PC:\000"
.LC64:
	.ascii	"id = %x,%x addr= %x,spare= %x %x %x %x data= %x\012"
	.ascii	"\000"
.LC65:
	.ascii	"superBlkID = %x vpc=%x\012\000"
.LC66:
	.ascii	"flashmode = %x pagenum = %x %x\012\000"
.LC67:
	.ascii	"blk = %x vpc=%x mode = %x\012\000"
.LC68:
	.ascii	"mlc id = %x,%x addr= %x,spare= %x %x %x %x data=%x "
	.ascii	"%x\012\000"
.LC69:
	.ascii	"slc id = %x,%x addr= %x,spare= %x %x %x %x data=%x "
	.ascii	"%x\012\000"
.LC70:
	.ascii	"slc mode\000"
.LC71:
	.ascii	"ftl_scan_all_ppa blk %x page %x flag: %x\012\000"
.LC72:
	.ascii	"ftl_scan_all_ppa blk %x page %x flag: %x .........."
	.ascii	"..... is bad block\012\000"
.LC73:
	.ascii	"addr= %x, status= %d,spare= %x %x %x %x data=%x %x\012"
	.ascii	"\000"
.LC74:
	.ascii	"%s finished\012\000"
.LC75:
	.ascii	"FLASH INFO:\012\000"
.LC76:
	.ascii	"FLASH ID: %x\012\000"
.LC77:
	.ascii	"Device Capacity: %d MB\012\000"
.LC78:
	.ascii	"FMWAIT: %x %x %x %x\012\000"
.LC79:
	.ascii	"FTL INFO:\012\000"
.LC80:
	.ascii	"g_MaxLpn = 0x%x\012\000"
.LC81:
	.ascii	"g_VaildLpn = 0x%x\012\000"
.LC82:
	.ascii	"read_page_count = 0x%x\012\000"
.LC83:
	.ascii	"discard_page_count = 0x%x\012\000"
.LC84:
	.ascii	"write_page_count = 0x%x\012\000"
.LC85:
	.ascii	"cache_write_count = 0x%x\012\000"
.LC86:
	.ascii	"l2p_write_count = 0x%x\012\000"
.LC87:
	.ascii	"gc_page_count = 0x%x\012\000"
.LC88:
	.ascii	"totle_write = %d MB\012\000"
.LC89:
	.ascii	"totle_read = %d MB\012\000"
.LC90:
	.ascii	"GSV = 0x%x\012\000"
.LC91:
	.ascii	"GDV = 0x%x\012\000"
.LC92:
	.ascii	"bad blk num = %d %d\012\000"
.LC93:
	.ascii	"free_superblocks = 0x%x\012\000"
.LC94:
	.ascii	"mlc_EC = 0x%x\012\000"
.LC95:
	.ascii	"slc_EC = 0x%x\012\000"
.LC96:
	.ascii	"avg_EC = 0x%x\012\000"
.LC97:
	.ascii	"sys_EC = 0x%x\012\000"
.LC98:
	.ascii	"max_EC = 0x%x\012\000"
.LC99:
	.ascii	"min_EC = 0x%x\012\000"
.LC100:
	.ascii	"PLT = 0x%x\012\000"
.LC101:
	.ascii	"POT = 0x%x\012\000"
.LC102:
	.ascii	"MaxSector = 0x%x\012\000"
.LC103:
	.ascii	"init_sys_blks_pp = 0x%x\012\000"
.LC104:
	.ascii	"sys_blks_pp = 0x%x\012\000"
.LC105:
	.ascii	"free sysblock = 0x%x\012\000"
.LC106:
	.ascii	"data_blks_pp = 0x%x\012\000"
.LC107:
	.ascii	"data_op_blks_pp = 0x%x\012\000"
.LC108:
	.ascii	"max_data_blks = 0x%x\012\000"
.LC109:
	.ascii	"Sys.id = 0x%x\012\000"
.LC110:
	.ascii	"Bbt.id = 0x%x\012\000"
.LC111:
	.ascii	"ACT.page = 0x%x\012\000"
.LC112:
	.ascii	"ACT.plane = 0x%x\012\000"
.LC113:
	.ascii	"ACT.id = 0x%x\012\000"
.LC114:
	.ascii	"ACT.mode = 0x%x\012\000"
.LC115:
	.ascii	"ACT.a_pages = 0x%x\012\000"
.LC116:
	.ascii	"ACT VPC = 0x%x\012\000"
.LC117:
	.ascii	"BUF.page = 0x%x\012\000"
.LC118:
	.ascii	"BUF.plane = 0x%x\012\000"
.LC119:
	.ascii	"BUF.id = 0x%x\012\000"
.LC120:
	.ascii	"BUF.mode = 0x%x\012\000"
.LC121:
	.ascii	"BUF.a_pages = 0x%x\012\000"
.LC122:
	.ascii	"BUF VPC = 0x%x\012\000"
.LC123:
	.ascii	"TMP.page = 0x%x\012\000"
.LC124:
	.ascii	"TMP.plane = 0x%x\012\000"
.LC125:
	.ascii	"TMP.id = 0x%x\012\000"
.LC126:
	.ascii	"TMP.mode = 0x%x\012\000"
.LC127:
	.ascii	"TMP.a_pages = 0x%x\012\000"
.LC128:
	.ascii	"GC.page = 0x%x\012\000"
.LC129:
	.ascii	"GC.plane = 0x%x\012\000"
.LC130:
	.ascii	"GC.id = 0x%x\012\000"
.LC131:
	.ascii	"GC.mode = 0x%x\012\000"
.LC132:
	.ascii	"GC.a_pages = 0x%x\012\000"
.LC133:
	.ascii	"WR_CHK = 0x%x %x %x %x\012\000"
.LC134:
	.ascii	"Read Err = 0x%x\012\000"
.LC135:
	.ascii	"Prog Err = 0x%x\012\000"
.LC136:
	.ascii	"gc_free_blk_th= 0x%x\012\000"
.LC137:
	.ascii	"gc_merge_free_blk_th= 0x%x\012\000"
.LC138:
	.ascii	"gc_skip_write_count= 0x%x\012\000"
.LC139:
	.ascii	"gc_blk_index= 0x%x\012\000"
.LC140:
	.ascii	"free min EC= 0x%x\012\000"
.LC141:
	.ascii	"free max EC= 0x%x\012\000"
.LC142:
	.ascii	"GC__SB VPC = 0x%x\012\000"
.LC143:
	.ascii	"%d. [0x%x]=0x%x 0x%x  0x%x\012\000"
.LC144:
	.ascii	"free %d. [0x%x] 0x%x  0x%x\012\000"
.LC145:
	.ascii	"FTL version: 5.0.63 20210616\000"
.LC146:
	.ascii	"swblk %x ,avg = %x max= %x vpc= %x,ec=%x ,max ec=%x"
	.ascii	"\012\000"
.LC147:
	.ascii	"FtlGcScanTempBlk Error ID %x %x!!!!!!! \012\000"
.LC148:
	.ascii	"FtlGcScanTempBlkError ID %x %x!!!!!!!\012\000"
.LC149:
	.ascii	"FtlGcRefreshBlock  0x%x\012\000"
.LC150:
	.ascii	"FtlGcMarkBadPhyBlk %d 0x%x\012\000"
.LC151:
	.ascii	"FtlGcFreeBadSuperBlk 0x%x\012\000"
.LC152:
	.ascii	"decrement_vpc_count %x = %d\012\000"
.LC153:
	.ascii	"decrement_vpc_count %x = %d in free list\012\000"
.LC154:
	.ascii	"RSB refresh addr %x\012\000"
.LC155:
	.ascii	"spuer block %x vpn is 0\012 \000"
.LC156:
	.ascii	"g_recovery_ppa %x ver %x\012 \000"
.LC157:
	.ascii	"FtlCheckVpc %x = %x  %x\012\000"
.LC158:
	.ascii	"%d GC datablk  = %x vpc %x %x\012\000"
.LC159:
	.ascii	"SWL %x, FSB = %x vpc= %x,ec=%x th=%x\012\000"
.LC160:
	.ascii	"Ftlwrite decrement_vpc_count %x = %d\012\000"
.LC161:
	.ascii	"GC des block %x done\012\000"
.LC162:
	.ascii	"too many bad block  = %d %d\012\000"
.LC163:
	.ascii	"...%s: no bad block mapping table, format device\012"
	.ascii	"\000"
.LC164:
	.ascii	"...%s FtlSysBlkInit error ,format device!\012\000"
.LC165:
	.ascii	"FtlWrite: lpa error:%x %x\012\000"
.LC166:
	.ascii	"fix power lost blk = %x vpc=%x\012\000"
.LC167:
	.ascii	"erase power lost blk = %x vpc=%x\012\000"
