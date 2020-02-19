/*
 * Copyright (c) 2016-2018, Fuzhou Rockchip Electronics Co., Ltd
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 * date: 2020-02-18
 * function: rk ftl v5 for rockchip soc base on arm v7 to support MLC NAND.
 */
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
	.file	"rk_ftlv5_arm_v7.S"
#APP
	.syntax unified
	.text
	.align	2
	.global	FlashMemCmp8
	.type	FlashMemCmp8, %function
FlashMemCmp8:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	ldr	r3, .L11
	str	lr, [sp, #-4]!
	.save {lr}
	ldrb	r3, [r3]	@ zero_extendqisi2
	cmp	r3, #0
	beq	.L4
	ldrb	r3, [r1, #1]	@ zero_extendqisi2
	ldrb	ip, [r0, #1]	@ zero_extendqisi2
	cmp	ip, r3
	movne	r3, #0
	beq	.L8
.L4:
	cmp	r3, r2
	beq	.L8
	ldrb	lr, [r0, r3]	@ zero_extendqisi2
	ldrb	ip, [r1, r3]	@ zero_extendqisi2
	add	r3, r3, #1
	cmp	lr, ip
	beq	.L4
	mov	r0, r3
	ldr	pc, [sp], #4
.L8:
	mov	r0, #0
	ldr	pc, [sp], #4
.L12:
	.align	2
.L11:
	.word	.LANCHOR0
	.fnend
	.size	FlashMemCmp8, .-FlashMemCmp8
	.align	2
	.global	FlashRsvdBlkChk
	.type	FlashRsvdBlkChk, %function
FlashRsvdBlkChk:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	@ link register save eliminated.
	ldr	r3, .L14
	ldrb	r2, [r3, #1]	@ zero_extendqisi2
	ldr	r3, [r3, #4]
	mul	r3, r3, r2
	cmp	r1, r3
	movcs	r2, #0
	movcc	r2, #1
	cmp	r0, #0
	movne	r2, #0
	eor	r0, r2, #1
	bx	lr
.L15:
	.align	2
.L14:
	.word	.LANCHOR0
	.fnend
	.size	FlashRsvdBlkChk, .-FlashRsvdBlkChk
	.align	2
	.global	FlashGetRandomizer
	.type	FlashGetRandomizer, %function
FlashGetRandomizer:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	and	r3, r1, #127
	ldr	r2, .L25
	stmfd	sp!, {r4, lr}
	.save {r4, lr}
	mov	r3, r3, asl #1
	ldrh	r4, [r2, r3]
	ldr	r3, .L25+4
	ldrb	r3, [r3, #8]	@ zero_extendqisi2
	cmp	r3, #0
	beq	.L17
	bl	FlashRsvdBlkChk
	cmp	r0, #0
	orrne	r4, r4, #-1073741824
.L17:
	mov	r0, r4
	ldmfd	sp!, {r4, pc}
.L26:
	.align	2
.L25:
	.word	.LANCHOR1
	.word	.LANCHOR0
	.fnend
	.size	FlashGetRandomizer, .-FlashGetRandomizer
	.align	2
	.global	FlashSetRandomizer
	.type	FlashSetRandomizer, %function
FlashSetRandomizer:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	and	r3, r1, #127
	ldr	r2, .L36
	stmfd	sp!, {r4, r5, r6, lr}
	.save {r4, r5, r6, lr}
	mov	r3, r3, asl #1
	mov	r6, r0
	ldrh	r5, [r2, r3]
	ldr	r3, .L36+4
	ldrb	r2, [r3, #8]	@ zero_extendqisi2
	mov	r4, r3
	cmp	r2, #0
	beq	.L28
	bl	FlashRsvdBlkChk
	cmp	r0, #0
	orrne	r5, r5, #-1073741824
.L28:
	add	r4, r4, r6, asl #3
	ldr	r3, [r4, #12]
	str	r5, [r3, #336]
	ldmfd	sp!, {r4, r5, r6, pc}
.L37:
	.align	2
.L36:
	.word	.LANCHOR1
	.word	.LANCHOR0
	.fnend
	.size	FlashSetRandomizer, .-FlashSetRandomizer
	.align	2
	.global	FlashBlockAlignInit
	.type	FlashBlockAlignInit, %function
FlashBlockAlignInit:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	@ link register save eliminated.
	cmp	r0, #512
	ldr	r3, .L44
	movhi	r2, #1024
	bhi	.L43
	cmp	r0, #256
	movhi	r2, #512
	bhi	.L43
	cmp	r0, #128
	strls	r0, [r3, #4]
	bxls	lr
	mov	r2, #256
.L43:
	str	r2, [r3, #4]
	bx	lr
.L45:
	.align	2
.L44:
	.word	.LANCHOR0
	.fnend
	.size	FlashBlockAlignInit, .-FlashBlockAlignInit
	.align	2
	.global	FlashReadCmd
	.type	FlashReadCmd, %function
FlashReadCmd:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	@ link register save eliminated.
	ldr	ip, .L47
	mov	r2, r1, lsr #16
	add	ip, ip, r0, asl #3
	ldr	r3, [ip, #12]
	ldrb	ip, [ip, #16]	@ zero_extendqisi2
	add	r3, r3, ip, asl #8
	mov	ip, #0
	str	ip, [r3, #2056]
	str	ip, [r3, #2052]
	str	ip, [r3, #2052]
	uxtb	ip, r1
	str	ip, [r3, #2052]
	mov	ip, r1, lsr #8
	str	ip, [r3, #2052]
	str	r2, [r3, #2052]
	mov	r2, #48
	str	r2, [r3, #2056]
	b	FlashSetRandomizer
.L48:
	.align	2
.L47:
	.word	.LANCHOR0
	.fnend
	.size	FlashReadCmd, .-FlashReadCmd
	.align	2
	.global	FlashReadDpDataOutCmd
	.type	FlashReadDpDataOutCmd, %function
FlashReadDpDataOutCmd:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	ldr	ip, .L54
	stmfd	sp!, {r4, lr}
	.save {r4, lr}
	add	r3, ip, r0, asl #3
	ldrb	ip, [ip, #60]	@ zero_extendqisi2
	uxtb	r4, r1
	ldr	r2, [r3, #12]
	mov	lr, r1, lsr #8
	ldrb	r3, [r3, #16]	@ zero_extendqisi2
	cmp	ip, #1
	mov	ip, r1, lsr #16
	mov	r3, r3, asl #8
	add	r3, r2, r3
	bne	.L50
	mov	r2, #6
	str	r2, [r3, #2056]
	mov	r2, #0
	str	r2, [r3, #2052]
	str	r2, [r3, #2052]
	str	r4, [r3, #2052]
	str	lr, [r3, #2052]
	str	ip, [r3, #2052]
	b	.L53
.L50:
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
.L53:
	mov	r2, #224
	str	r2, [r3, #2056]
	ldmfd	sp!, {r4, lr}
	b	FlashSetRandomizer
.L55:
	.align	2
.L54:
	.word	.LANCHOR0
	.fnend
	.size	FlashReadDpDataOutCmd, .-FlashReadDpDataOutCmd
	.align	2
	.global	FlashProgFirstCmd
	.type	FlashProgFirstCmd, %function
FlashProgFirstCmd:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	@ link register save eliminated.
	ldr	ip, .L57
	mov	r2, r1, lsr #16
	add	ip, ip, r0, asl #3
	ldr	r3, [ip, #12]
	ldrb	ip, [ip, #16]	@ zero_extendqisi2
	add	r3, r3, ip, asl #8
	mov	ip, #128
	str	ip, [r3, #2056]
	mov	ip, #0
	str	ip, [r3, #2052]
	str	ip, [r3, #2052]
	uxtb	ip, r1
	str	ip, [r3, #2052]
	mov	ip, r1, lsr #8
	str	ip, [r3, #2052]
	str	r2, [r3, #2052]
	b	FlashSetRandomizer
.L58:
	.align	2
.L57:
	.word	.LANCHOR0
	.fnend
	.size	FlashProgFirstCmd, .-FlashProgFirstCmd
	.align	2
	.global	FlashEraseCmd
	.type	FlashEraseCmd, %function
FlashEraseCmd:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	str	lr, [sp, #-4]!
	.save {lr}
	cmp	r2, #0
	ldr	lr, .L65
	add	r0, lr, r0, asl #3
	ldrb	r3, [r0, #16]	@ zero_extendqisi2
	ldr	ip, [r0, #12]
	mov	r3, r3, asl #8
	beq	.L60
	add	r2, ip, r3
	mov	r0, #96
	str	r0, [r2, #2056]
	uxtb	r0, r1
	str	r0, [r2, #2052]
	mov	r0, r1, lsr #8
	str	r0, [r2, #2052]
	mov	r0, r1, lsr #16
	str	r0, [r2, #2052]
	ldr	r2, [lr, #4]
	add	r1, r1, r2
.L60:
	add	r3, ip, r3
	mov	r2, #96
	str	r2, [r3, #2056]
	uxtb	r2, r1
	str	r2, [r3, #2052]
	mov	r2, r1, lsr #8
	mov	r1, r1, lsr #16
	str	r2, [r3, #2052]
	str	r1, [r3, #2052]
	mov	r2, #208
	str	r2, [r3, #2056]
	ldr	pc, [sp], #4
.L66:
	.align	2
.L65:
	.word	.LANCHOR0
	.fnend
	.size	FlashEraseCmd, .-FlashEraseCmd
	.align	2
	.global	FlashProgDpSecondCmd
	.type	FlashProgDpSecondCmd, %function
FlashProgDpSecondCmd:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	ldr	ip, .L69
	mov	r2, r1, lsr #16
	str	lr, [sp, #-4]!
	.save {lr}
	add	lr, ip, r0, asl #3
	ldrb	ip, [ip, #55]	@ zero_extendqisi2
	ldr	r3, [lr, #12]
	ldrb	lr, [lr, #16]	@ zero_extendqisi2
	add	r3, r3, lr, asl #8
	str	ip, [r3, #2056]
	mov	ip, #0
	str	ip, [r3, #2052]
	str	ip, [r3, #2052]
	uxtb	ip, r1
	str	ip, [r3, #2052]
	mov	ip, r1, lsr #8
	str	ip, [r3, #2052]
	str	r2, [r3, #2052]
	ldr	lr, [sp], #4
	b	FlashSetRandomizer
.L70:
	.align	2
.L69:
	.word	.LANCHOR0
	.fnend
	.size	FlashProgDpSecondCmd, .-FlashProgDpSecondCmd
	.align	2
	.global	FlashProgSecondCmd
	.type	FlashProgSecondCmd, %function
FlashProgSecondCmd:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	stmfd	sp!, {r3, r4, r5, lr}
	.save {r3, r4, r5, lr}
	ldr	r3, .L73
	add	r0, r3, r0, asl #3
	ldr	r3, .L73+4
	ldrb	r5, [r0, #16]	@ zero_extendqisi2
	ldr	r4, [r0, #12]
	ldr	r3, [r3, #4]
	add	r4, r4, r5, asl #8
	ldr	r0, .L73+8
	blx	r3
	mov	r3, #16
	str	r3, [r4, #2056]
	ldmfd	sp!, {r3, r4, r5, pc}
.L74:
	.align	2
.L73:
	.word	.LANCHOR0
	.word	arm_delay_ops
	.word	214748300
	.fnend
	.size	FlashProgSecondCmd, .-FlashProgSecondCmd
	.align	2
	.global	FlashProgDpFirstCmd
	.type	FlashProgDpFirstCmd, %function
FlashProgDpFirstCmd:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	@ link register save eliminated.
	ldr	r2, .L76
	add	r0, r2, r0, asl #3
	ldrb	r2, [r2, #54]	@ zero_extendqisi2
	ldrb	r1, [r0, #16]	@ zero_extendqisi2
	ldr	r3, [r0, #12]
	add	r3, r3, r1, asl #8
	str	r2, [r3, #2056]
	bx	lr
.L77:
	.align	2
.L76:
	.word	.LANCHOR0
	.fnend
	.size	FlashProgDpFirstCmd, .-FlashProgDpFirstCmd
	.align	2
	.global	js_hash
	.type	js_hash, %function
js_hash:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	@ link register save eliminated.
	ldr	r3, .L82
	add	r1, r0, r1
.L79:
	cmp	r0, r1
	beq	.L81
	mov	r2, r3, asl #5
	ldrb	ip, [r0], #1	@ zero_extendqisi2
	add	r2, r2, r3, lsr #2
	add	r2, r2, ip
	eor	r3, r3, r2
	b	.L79
.L81:
	mov	r0, r3
	bx	lr
.L83:
	.align	2
.L82:
	.word	1204201446
	.fnend
	.size	js_hash, .-js_hash
	.align	2
	.global	FlashLoadIdbInfo
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
	.type	BuildFlashLsbPageTable, %function
BuildFlashLsbPageTable:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	cmp	r0, #0
	stmfd	sp!, {r4, lr}
	.save {r4, lr}
	mov	r4, r1
	bne	.L86
	ldr	r3, .L121
.L87:
	mov	r2, r0, asl #1
	strh	r0, [r2, r3]	@ movhi
	add	r0, r0, #1
	cmp	r0, #256
	bne	.L87
.L91:
	mov	r2, #2048
	ldr	r0, .L121+4
	mov	r1, #255
	uxth	r4, r4
	bl	ftl_memset
	ldr	r2, .L121
	mov	r3, #0
	add	r0, r2, #1024
	b	.L88
.L86:
	cmp	r0, #1
	bne	.L89
	ldr	ip, .L121
	mov	r3, #0
.L90:
	uxth	r2, r3
	mov	lr, r3, asl #1
	cmp	r2, #3
	movls	r0, #0
	movhi	r0, #1
	bics	r1, r0, r3
	add	r3, r3, #1
	movne	r1, #2
	moveq	r1, #3
	cmp	r0, #0
	rsb	r1, r1, r2, asl #1
	movne	r2, r1
	cmp	r3, #256
	strh	r2, [lr, ip]	@ movhi
	bne	.L90
	b	.L91
.L89:
	cmp	r0, #2
	bne	.L92
	ldr	r1, .L121
	mov	r3, #0
.L93:
	uxth	r2, r3
	mov	r0, r3, asl #1
	cmp	r2, #1
	add	r3, r3, #1
	mov	ip, r2, asl #1
	subhi	r2, ip, #1
	cmp	r3, #256
	strh	r2, [r0, r1]	@ movhi
	bne	.L93
	b	.L91
.L92:
	cmp	r0, #3
	bne	.L94
	ldr	ip, .L121
	mov	r3, #0
.L95:
	uxth	r2, r3
	mov	lr, r3, asl #1
	cmp	r2, #5
	movls	r0, #0
	movhi	r0, #1
	bics	r1, r0, r3
	add	r3, r3, #1
	movne	r1, #4
	moveq	r1, #5
	cmp	r0, #0
	rsb	r1, r1, r2, asl #1
	movne	r2, r1
	cmp	r3, #256
	strh	r2, [lr, ip]	@ movhi
	bne	.L95
	b	.L91
.L94:
	cmp	r0, #4
	mov	r3, #0
	bne	.L96
	ldr	r2, .L121+8
	strh	r3, [r2, #76]	@ movhi
	mov	r3, #1
	strh	r0, [r2, #84]	@ movhi
	strh	r3, [r2, #78]	@ movhi
	mov	r3, #2
	strh	r3, [r2, #80]	@ movhi
	mov	r3, #3
	strh	r3, [r2, #82]	@ movhi
	mov	r3, #5
	strh	r3, [r2, #86]	@ movhi
	mov	r3, #7
	strh	r3, [r2, #88]	@ movhi
	mov	r3, #8
	strh	r3, [r2, #90]!	@ movhi
.L97:
	tst	r3, #1
	movne	r1, #7
	moveq	r1, #6
	rsb	r1, r1, r3, asl #1
	add	r3, r3, #1
	strh	r1, [r2, #2]!	@ movhi
	uxth	r3, r3
	cmp	r3, #256
	bne	.L97
	b	.L91
.L96:
	cmp	r0, #5
	bne	.L98
	ldr	r2, .L121
.L99:
	mov	r1, r3, asl #1
	strh	r3, [r1, r2]	@ movhi
	add	r3, r3, #1
	cmp	r3, #16
	bne	.L99
	ldr	r2, .L121+12
.L100:
	strh	r3, [r2, #2]!	@ movhi
	add	r3, r3, #2
	uxth	r3, r3
	cmp	r3, #496
	bne	.L100
	b	.L91
.L98:
	cmp	r0, #6
	bne	.L101
	ldr	r1, .L121+16
.L102:
	cmp	r3, #5
	add	r2, r3, r3, asl #1
	movls	r0, #0
	movhi	r0, #1
	bics	ip, r0, r3
	movne	ip, #10
	moveq	ip, #12
	cmp	r0, #0
	subne	r2, r2, ip
	moveq	r2, r3
	add	r3, r3, #1
	strh	r2, [r1, #2]!	@ movhi
	uxth	r3, r3
	cmp	r3, #256
	bne	.L102
	b	.L91
.L101:
	cmp	r0, #9
	bne	.L103
	ldr	r2, .L121+8
	movw	r1, #509
	strh	r3, [r2, #76]	@ movhi
	mov	r3, #1
	strh	r3, [r2, #78]	@ movhi
	mov	r3, #2
	strh	r3, [r2, #80]!	@ movhi
	mov	r3, #3
.L104:
	strh	r3, [r2, #2]!	@ movhi
	add	r3, r3, #2
	uxth	r3, r3
	cmp	r3, r1
	bne	.L104
	b	.L91
.L103:
	cmp	r0, #10
	bne	.L91
	ldr	r2, .L121
.L105:
	mov	r1, r3, asl #1
	strh	r3, [r1, r2]	@ movhi
	add	r3, r3, #1
	cmp	r3, #63
	bne	.L105
	ldr	r2, .L121+20
	movw	r1, #961
.L106:
	strh	r3, [r2, #2]!	@ movhi
	add	r3, r3, #2
	uxth	r3, r3
	cmp	r3, r1
	bne	.L106
	b	.L91
.L88:
	uxth	r1, r3
	cmp	r1, r4
	bcs	.L120
	mov	r1, r3, asl #1
	add	r3, r3, #1
	ldrh	r1, [r1, r2]
	mov	ip, r1, asl #1
	strh	r1, [r0, ip]	@ movhi
	b	.L88
.L120:
	ldmfd	sp!, {r4, pc}
.L122:
	.align	2
.L121:
	.word	.LANCHOR0+76
	.word	.LANCHOR0+1100
	.word	.LANCHOR0
	.word	.LANCHOR0+106
	.word	.LANCHOR0+74
	.word	.LANCHOR0+200
	.fnend
	.size	BuildFlashLsbPageTable, .-BuildFlashLsbPageTable
	.align	2
	.global	FlashPrintInfo
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
	.global	FlashDieInfoInit
	.type	FlashDieInfoInit, %function
FlashDieInfoInit:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	stmfd	sp!, {r3, r4, r5, r6, r7, r8, r9, lr}
	.save {r3, r4, r5, r6, r7, r8, r9, lr}
	mov	r5, #0
	ldr	r3, .L139
	ldr	r4, .L139+4
	ldr	r9, .L139+8
	ldrh	r0, [r3, #10]
	strb	r5, [r4, #3148]
	strb	r5, [r4, #3149]
	bl	FlashBlockAlignInit
	mov	r1, r5
	mov	r2, #8
	add	r0, r4, #3152
	bl	ftl_memset
	mov	r1, r5
	mov	r2, #32
	ldr	r0, .L139+12
	bl	ftl_memset
	ldr	r0, .L139+16
	mov	r1, r5
	mov	r2, #128
	bl	ftl_memset
	ldr	r7, [r4, #3320]
	add	r6, r7, #1
.L126:
	mov	r0, r6
	add	r1, r9, r5, asl #3
	ldrb	r2, [r7]	@ zero_extendqisi2
	bl	FlashMemCmp8
	ldr	r8, .L139+8
	cmp	r0, #0
	bne	.L125
	ldrb	r3, [r4, #3148]	@ zero_extendqisi2
	add	r2, r4, r3, asl #2
	str	r0, [r2, #3160]
	add	r2, r3, #1
	add	r3, r4, r3
	strb	r2, [r4, #3148]
	strb	r5, [r3, #3152]
.L125:
	add	r5, r5, #1
	cmp	r5, #4
	bne	.L126
	ldrb	r3, [r4, #3148]	@ zero_extendqisi2
	strb	r3, [r4, #3149]
	ldrb	r3, [r7, #8]	@ zero_extendqisi2
	cmp	r3, #2
	beq	.L127
.L131:
	ldrb	r3, [r7, #13]	@ zero_extendqisi2
	ldrb	r2, [r4, #3148]	@ zero_extendqisi2
	smulbb	r2, r2, r3
	ldrh	r3, [r7, #14]
	smulbb	r3, r2, r3
	movw	r2, #3324
	strh	r3, [r4, r2]	@ movhi
	ldmfd	sp!, {r3, r4, r5, r6, r7, r8, r9, pc}
.L127:
	ldr	r3, .L139+4
	mov	r5, #0
	ldr	r9, [r3, #4]
.L130:
	mov	r0, r6
	add	r1, r8, r5, asl #3
	ldrb	r2, [r7]	@ zero_extendqisi2
	bl	FlashMemCmp8
	cmp	r0, #0
	bne	.L128
	ldrb	r1, [r7, #13]	@ zero_extendqisi2
	ldrh	r3, [r7, #14]
	ldrb	r2, [r4, #3148]	@ zero_extendqisi2
	mul	r1, r9, r1
	and	r3, r3, #65280
	add	r0, r4, r2, asl #2
	mul	r3, r3, r1
	str	r3, [r0, #3160]
	ldrb	r1, [r7, #23]	@ zero_extendqisi2
	cmp	r1, #0
	movne	r3, r3, asl #1
	strne	r3, [r0, #3160]
	add	r3, r2, #1
	add	r2, r4, r2
	strb	r3, [r4, #3148]
	strb	r5, [r2, #3152]
.L128:
	add	r5, r5, #1
	cmp	r5, #4
	bne	.L130
	b	.L131
.L140:
	.align	2
.L139:
	.word	.LANCHOR1+256
	.word	.LANCHOR0
	.word	IDByte
	.word	.LANCHOR0+3160
	.word	.LANCHOR0+3192
	.fnend
	.size	FlashDieInfoInit, .-FlashDieInfoInit
	.align	2
	.global	FlashReadIdbData
	.type	FlashReadIdbData, %function
FlashReadIdbData:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	stmfd	sp!, {r3, lr}
	.save {r3, lr}
	mov	r2, #2048
	ldr	r1, .L143
	bl	ftl_memcpy
	mov	r0, #0
	ldmfd	sp!, {r3, pc}
.L144:
	.align	2
.L143:
	.word	.LANCHOR0+3328
	.fnend
	.size	FlashReadIdbData, .-FlashReadIdbData
	.align	2
	.global	FlashLoadPhyInfoInRam
	.type	FlashLoadPhyInfoInRam, %function
FlashLoadPhyInfoInRam:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	stmfd	sp!, {r3, r4, r5, r6, r7, r8, r9, lr}
	.save {r3, r4, r5, r6, r7, r8, r9, lr}
	mov	r6, #0
	ldr	r8, .L158
.L149:
	mov	r4, r6, asl #5
	ldr	r1, .L158+4
	add	r0, r4, #1
	ldrb	r2, [r8, r6, asl #5]	@ zero_extendqisi2
	add	r0, r8, r0
	ldr	r5, .L158+8
	bl	FlashMemCmp8
	add	r9, r5, #288
	subs	r7, r0, #0
	bne	.L146
	adds	r9, r9, r4
	beq	.L152
	add	r4, r5, r4
	add	r5, r5, #2944
	ldrb	r3, [r4, #310]	@ zero_extendqisi2
	mov	r4, r7
	b	.L151
.L146:
	add	r6, r6, #1
	cmp	r6, #83
	bne	.L149
	b	.L152
.L157:
	add	r4, r4, #1
	cmp	r4, #4
	beq	.L150
.L151:
	ldrb	r2, [r5, r4, asl #5]	@ zero_extendqisi2
	cmp	r2, r3
	bne	.L157
.L150:
	ldr	r6, .L158+12
	mov	r2, #32
	ldr	r1, .L158+16
	ldr	r0, .L158+20
	add	r1, r1, r4, asl #5
	bl	ftl_memcpy
	mov	r0, r6
	mov	r1, r9
	mov	r2, #32
	bl	ftl_memcpy
	ldrh	r0, [r6, #10]
	bl	FlashBlockAlignInit
	b	.L147
.L152:
	mvn	r7, #0
.L147:
	mov	r0, r7
	ldmfd	sp!, {r3, r4, r5, r6, r7, r8, r9, pc}
.L159:
	.align	2
.L158:
	.word	.LANCHOR1+288
	.word	IDByte
	.word	.LANCHOR1
	.word	.LANCHOR1+256
	.word	.LANCHOR1+2944
	.word	.LANCHOR0+44
	.fnend
	.size	FlashLoadPhyInfoInRam, .-FlashLoadPhyInfoInRam
	.align	2
	.global	ftl_flash_suspend
	.type	ftl_flash_suspend, %function
ftl_flash_suspend:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	@ link register save eliminated.
	ldr	r3, .L161
	ldr	r2, [r3, #-2808]
	ldr	r1, [r2]
	str	r1, [r3, #-2804]
	ldr	r1, [r2, #4]
	str	r1, [r3, #-2800]
	ldr	r1, [r2, #8]
	str	r1, [r3, #-2796]
	ldr	r1, [r2, #12]
	str	r1, [r3, #-2792]
	ldr	r1, [r2, #304]
	str	r1, [r3, #-2788]
	ldr	r1, [r2, #308]
	str	r1, [r3, #-2784]
	ldr	r1, [r2, #336]
	ldr	r2, [r2, #344]
	str	r1, [r3, #-2780]
	str	r2, [r3, #-2776]
	bx	lr
.L162:
	.align	2
.L161:
	.word	.LANCHOR2
	.fnend
	.size	ftl_flash_suspend, .-ftl_flash_suspend
	.global	__aeabi_uidiv
	.align	2
	.global	LogAddr2PhyAddr
	.type	LogAddr2PhyAddr, %function
LogAddr2PhyAddr:
	.fnstart
	@ args = 4, pretend = 0, frame = 8
	@ frame_needed = 0, uses_anonymous_args = 0
	stmfd	sp!, {r0, r1, r2, r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.save {r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.pad #12
	mov	r9, r3
	ldr	r3, .L169
	mov	fp, r1
	ldr	r7, .L169+4
	mov	r5, r0
	mov	r8, r2
	ldrh	r4, [r3, #12]
	ldrh	r3, [r3, #14]
	ldrh	r10, [r7, #4]
	smulbb	r4, r4, r3
	ldrb	r3, [r7]	@ zero_extendqisi2
	cmp	r3, #1
	ldr	r3, [r0, #4]
	moveq	r10, r10, asl #1
	bic	r3, r3, #-2147483648
	str	r3, [sp, #4]
	ubfx	r6, r3, #10, #16
	uxth	r4, r4
	uxtheq	r10, r10
	mov	r1, r4
	mov	r0, r6
	bl	__aeabi_uidiv
	cmp	fp, #1
	uxth	r0, r0
	ldr	r3, [sp, #4]
	smulbb	r4, r0, r4
	ubfx	r1, r3, #0, #10
	rsb	r6, r4, r6
	uxth	r6, r6
	bne	.L165
	ldr	r3, .L169+8
	ldrb	r3, [r3, #-2744]	@ zero_extendqisi2
	cmp	r3, #0
	addeq	r1, r7, r1, asl #1
	ldreqh	r1, [r1, #76]
.L165:
	add	lr, r7, r0, asl #2
	ldrb	r3, [sp, #48]	@ zero_extendqisi2
	ldr	ip, [lr, #3160]
	cmp	r3, #1
	mla	ip, r10, r6, ip
	add	r1, ip, r1
	str	r1, [r8]
	str	r0, [r9]
	movls	r0, #0
	ldrhi	r0, [r5, #4]
	ldrhi	r3, [r5, #40]
	addhi	r0, r0, #1024
	rsbhi	r0, r3, r0
	clzhi	r0, r0
	movhi	r0, r0, lsr #5
	add	sp, sp, #12
	@ sp needed
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L170:
	.align	2
.L169:
	.word	.LANCHOR2-2772
	.word	.LANCHOR0
	.word	.LANCHOR2
	.fnend
	.size	LogAddr2PhyAddr, .-LogAddr2PhyAddr
	.align	2
	.global	ftl_read_flash_info
	.type	ftl_read_flash_info, %function
ftl_read_flash_info:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	stmfd	sp!, {r4, lr}
	.save {r4, lr}
	mov	r1, #0
	mov	r2, #11
	mov	r4, r0
	bl	ftl_memset
	ldr	r2, .L176
	mov	ip, #1
	add	r2, r2, #3152
	ldr	r3, [r2, #168]
	ldrb	r1, [r3, #9]	@ zero_extendqisi2
	ldr	r3, [r2, #-3148]
	smulbb	r3, r1, r3
	strh	r3, [r4, #4]	@ unaligned
	ldr	r3, .L176+4
	ldrb	r1, [r3, #-2743]	@ zero_extendqisi2
	ldr	r3, [r3, #-2740]
	strb	r1, [r4, #7]
	str	r3, [r4]	@ unaligned
	ldr	r3, [r2, #168]
	ldrb	r0, [r2, #-4]	@ zero_extendqisi2
	ldrb	r1, [r3, #9]	@ zero_extendqisi2
	strb	r1, [r4, #6]
	mov	r1, #32
	strb	r1, [r4, #8]
	ldrb	r3, [r3, #7]	@ zero_extendqisi2
	strb	r3, [r4, #9]
	mov	r3, #0
	strb	r3, [r4, #10]
.L172:
	uxtb	r1, r3
	cmp	r1, r0
	bcs	.L175
	ldrb	lr, [r3, r2]	@ zero_extendqisi2
	add	r3, r3, #1
	ldrb	r1, [r4, #10]	@ zero_extendqisi2
	orr	r1, r1, ip, asl lr
	strb	r1, [r4, #10]
	b	.L172
.L175:
	ldmfd	sp!, {r4, pc}
.L177:
	.align	2
.L176:
	.word	.LANCHOR0
	.word	.LANCHOR2
	.fnend
	.size	ftl_read_flash_info, .-ftl_read_flash_info
	.align	2
	.global	FlashScheduleEnSet
	.type	FlashScheduleEnSet, %function
FlashScheduleEnSet:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	@ link register save eliminated.
	ldr	r3, .L179
	ldr	r2, [r3, #-2736]
	str	r0, [r3, #-2736]
	mov	r0, r2
	bx	lr
.L180:
	.align	2
.L179:
	.word	.LANCHOR2
	.fnend
	.size	FlashScheduleEnSet, .-FlashScheduleEnSet
	.align	2
	.global	FlashGetPageSize
	.type	FlashGetPageSize, %function
FlashGetPageSize:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	@ link register save eliminated.
	ldr	r3, .L182
	ldr	r3, [r3, #3320]
	ldrb	r0, [r3, #9]	@ zero_extendqisi2
	bx	lr
.L183:
	.align	2
.L182:
	.word	.LANCHOR0
	.fnend
	.size	FlashGetPageSize, .-FlashGetPageSize
	.align	2
	.global	NandcReadDontCaseBusyEn
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
	.type	NandcGetChipIf, %function
NandcGetChipIf:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	@ link register save eliminated.
	ldr	r3, .L186
	add	r0, r3, r0, asl #3
	ldrb	r2, [r0, #16]	@ zero_extendqisi2
	ldr	r0, [r0, #12]
	add	r2, r2, #8
	add	r0, r0, r2, asl #8
	bx	lr
.L187:
	.align	2
.L186:
	.word	.LANCHOR0
	.fnend
	.size	NandcGetChipIf, .-NandcGetChipIf
	.align	2
	.global	NandcSetDdrPara
	.type	NandcSetDdrPara, %function
NandcSetDdrPara:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	@ link register save eliminated.
	ldr	r3, .L189
	ldr	r2, [r3, #-2808]
	mov	r3, r0, asl #8
	orr	r0, r3, r0, asl #16
	orr	r3, r0, #1
	str	r3, [r2, #304]
	bx	lr
.L190:
	.align	2
.L189:
	.word	.LANCHOR2
	.fnend
	.size	NandcSetDdrPara, .-NandcSetDdrPara
	.align	2
	.global	NandcSetDdrDiv
	.type	NandcSetDdrDiv, %function
NandcSetDdrDiv:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	@ link register save eliminated.
	ldr	r3, .L192
	orr	r0, r0, #16640
	ldr	r3, [r3, #-2808]
	str	r0, [r3, #344]
	bx	lr
.L193:
	.align	2
.L192:
	.word	.LANCHOR2
	.fnend
	.size	NandcSetDdrDiv, .-NandcSetDdrDiv
	.align	2
	.global	NandcSetDdrMode
	.type	NandcSetDdrMode, %function
NandcSetDdrMode:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	@ link register save eliminated.
	ldr	r3, .L197
	cmp	r0, #0
	ldr	r2, [r3, #-2808]
	ldr	r3, [r2]
	bfieq	r3, r0, #13, #1
	orrne	r3, r3, #253952
	str	r3, [r2]
	bx	lr
.L198:
	.align	2
.L197:
	.word	.LANCHOR2
	.fnend
	.size	NandcSetDdrMode, .-NandcSetDdrMode
	.align	2
	.global	NandcSetMode
	.type	NandcSetMode, %function
NandcSetMode:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	@ link register save eliminated.
	ldr	r3, .L206
	ands	r1, r0, #6
	ldr	r2, [r3, #-2808]
	ldr	r3, [r2]
	bfieq	r3, r1, #13, #1
	beq	.L202
	orr	r3, r3, #24576
	movw	r1, #8322
	bfc	r3, #15, #1
	str	r1, [r2, #344]
	orr	r3, r3, #196608
	ldr	r1, .L206+4
	tst	r0, #4
	orrne	r3, r3, #32768
	str	r1, [r2, #304]
	mov	r1, #38
	str	r1, [r2, #308]
	mov	r1, #39
	str	r1, [r2, #308]
.L202:
	str	r3, [r2]
	mov	r0, #0
	bx	lr
.L207:
	.align	2
.L206:
	.word	.LANCHOR2
	.word	1052675
	.fnend
	.size	NandcSetMode, .-NandcSetMode
	.align	2
	.global	NandcFlashCs
	.type	NandcFlashCs, %function
NandcFlashCs:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	@ link register save eliminated.
	ldr	r2, .L209
	add	r0, r2, r0, asl #3
	mov	r2, #1
	ldr	r1, [r0, #12]
	ldrb	r0, [r0, #16]	@ zero_extendqisi2
	ldr	r3, [r1]
	mov	r2, r2, asl r0
	bfi	r3, r2, #0, #8
	str	r3, [r1]
	bx	lr
.L210:
	.align	2
.L209:
	.word	.LANCHOR0
	.fnend
	.size	NandcFlashCs, .-NandcFlashCs
	.align	2
	.global	NandcFlashDeCs
	.type	NandcFlashDeCs, %function
NandcFlashDeCs:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	@ link register save eliminated.
	ldr	r3, .L212
	add	r0, r3, r0, asl #3
	ldr	r2, [r0, #12]
	ldr	r3, [r2]
	bfc	r3, #0, #8
	bfc	r3, #17, #1
	str	r3, [r2]
	bx	lr
.L213:
	.align	2
.L212:
	.word	.LANCHOR0
	.fnend
	.size	NandcFlashDeCs, .-NandcFlashDeCs
	.align	2
	.global	NandcDelayns
	.type	NandcDelayns, %function
NandcDelayns:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	stmfd	sp!, {r3, lr}
	.save {r3, lr}
	add	r0, r0, #996
	ldr	r3, .L216
	add	r0, r0, #3
	umull	r0, r1, r0, r3
	ldr	r3, .L216+4
	ldr	r3, [r3, #8]
	mov	r0, r1, lsr #6
	blx	r3
	mov	r0, #0
	ldmfd	sp!, {r3, pc}
.L217:
	.align	2
.L216:
	.word	274877907
	.word	arm_delay_ops
	.fnend
	.size	NandcDelayns, .-NandcDelayns
	.align	2
	.global	FlashReadStatus
	.type	FlashReadStatus, %function
FlashReadStatus:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	stmfd	sp!, {r3, r4, r5, lr}
	.save {r3, r4, r5, lr}
	mov	r2, #112
	ldr	r3, .L220
	add	r0, r3, r0, asl #3
	ldrb	r4, [r0, #16]	@ zero_extendqisi2
	ldr	r5, [r0, #12]
	mov	r0, #80
	add	r3, r5, r4, asl #8
	add	r4, r4, #8
	str	r2, [r3, #2056]
	bl	NandcDelayns
	ldr	r0, [r5, r4, asl #8]
	ldmfd	sp!, {r3, r4, r5, pc}
.L221:
	.align	2
.L220:
	.word	.LANCHOR0
	.fnend
	.size	FlashReadStatus, .-FlashReadStatus
	.align	2
	.global	ToshibaSetRRPara
	.type	ToshibaSetRRPara, %function
ToshibaSetRRPara:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	stmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, lr}
	.save {r4, r5, r6, r7, r8, r9, r10, lr}
	add	r7, r1, r1, asl #2
	ldr	r8, .L232
	mov	r5, r0
	ldr	r10, .L232+4
	mov	r6, r1
	add	r9, r8, #3072
	mov	r4, #0
.L223:
	ldrb	r3, [r10]	@ zero_extendqisi2
	cmp	r4, r3
	bcs	.L231
	mov	r3, #85
	str	r3, [r5, #8]
	ldrsb	r3, [r4, r9]
	mov	r0, #200
	str	r3, [r5, #4]
	bl	NandcDelayns
	ldr	r3, .L232+8
	ldrb	r3, [r3]	@ zero_extendqisi2
	cmp	r3, #34
	addeq	r3, r4, r7
	addeq	r3, r9, r3
	beq	.L230
	cmp	r3, #35
	addne	r3, r8, r6
	addne	r3, r3, #3216
	ldrnesb	r3, [r3]
	bne	.L229
	ldr	r3, .L232+12
	add	r2, r4, r7
	add	r3, r3, r2
.L230:
	ldrsb	r3, [r3, #5]
.L229:
	str	r3, [r5]
	add	r4, r4, #1
	b	.L223
.L231:
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, pc}
.L233:
	.align	2
.L232:
	.word	.LANCHOR1
	.word	g_maxRegNum
	.word	g_retryMode
	.word	.LANCHOR1+3120
	.fnend
	.size	ToshibaSetRRPara, .-ToshibaSetRRPara
	.align	2
	.global	SamsungSetRRPara
	.type	SamsungSetRRPara, %function
SamsungSetRRPara:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	ldr	r3, .L239
	stmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, lr}
	.save {r4, r5, r6, r7, r8, r9, r10, lr}
	add	r1, r3, r1, asl #2
	ldr	r8, .L239+4
	mov	r4, #0
	add	r5, r1, #3
	mov	r6, r0
	mov	r7, r3
	mov	r9, #161
	mov	r10, r4
.L235:
	ldrb	r3, [r8]	@ zero_extendqisi2
	cmp	r4, r3
	bcs	.L238
	str	r9, [r6, #8]
	mov	r0, #300
	str	r10, [r6]
	ldrsb	r3, [r4, r7]
	add	r4, r4, #1
	str	r3, [r6]
	ldrsb	r3, [r5, #1]!
	str	r3, [r6]
	bl	NandcDelayns
	b	.L235
.L238:
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, pc}
.L240:
	.align	2
.L239:
	.word	.LANCHOR1+3224
	.word	g_maxRegNum
	.fnend
	.size	SamsungSetRRPara, .-SamsungSetRRPara
	.align	2
	.global	HynixSetRRPara
	.type	HynixSetRRPara, %function
HynixSetRRPara:
	.fnstart
	@ args = 0, pretend = 0, frame = 8
	@ frame_needed = 0, uses_anonymous_args = 0
	stmfd	sp!, {r0, r1, r2, r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.save {r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.pad #12
	mov	r6, r3
	ldr	r3, .L250
	mov	r10, r2
	mov	r7, r0
	mov	r9, r1
	ldr	r2, [r3, #3320]
	ldrb	r4, [r2, #19]	@ zero_extendqisi2
	mov	r2, r0, asl #3
	cmp	r4, #6
	ldreq	r4, .L250+4
	addeq	r4, r4, r0, asl #6
	addeq	r4, r4, #20
	addeq	r4, r4, r6, asl #2
	beq	.L243
.L242:
	cmp	r4, #7
	bne	.L244
	ldr	r1, .L250+4
	mov	r4, #160
	mla	r4, r4, r0, r1
	add	r1, r6, r6, asl #2
	add	r4, r4, #28
	add	r4, r4, r1, asl #1
	b	.L243
.L244:
	cmp	r4, #8
	addne	r4, r6, r2
	addeq	r1, r6, r6, asl #2
	ldrne	r1, .L250+4
	ldreq	r4, .L250+8
	addne	r4, r1, r4, asl #3
	addeq	r4, r4, r1
	addne	r4, r4, #20
.L243:
	add	r3, r3, r2
	mov	r0, r7
	sub	r9, r9, #1
	sub	r4, r4, #1
	ldrb	fp, [r3, #16]	@ zero_extendqisi2
	ldr	r8, [r3, #12]
	bl	NandcFlashCs
	sub	r2, r10, #1
	add	r10, r10, r9
	mov	fp, fp, asl #8
	mov	r3, #54
	add	r5, r8, fp
	str	r3, [r5, #2056]
.L246:
	cmp	r2, r10
	beq	.L249
	ldrb	r3, [r2, #1]!	@ zero_extendqisi2
	mov	r0, #200
	str	r3, [r5, #2052]
	str	r2, [sp, #4]
	bl	NandcDelayns
	ldrsb	r3, [r4, #1]!
	str	r3, [r5, #2048]
	ldr	r2, [sp, #4]
	b	.L246
.L249:
	add	r8, r8, fp
	mov	r3, #22
	mov	r0, r7
	str	r3, [r8, #2056]
	bl	NandcFlashDeCs
	ldr	r3, .L250+12
	add	r7, r3, r7
	strb	r6, [r7, #-1880]
	add	sp, sp, #12
	@ sp needed
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L251:
	.align	2
.L250:
	.word	.LANCHOR0
	.word	.LANCHOR2-2732
	.word	.LANCHOR2-2704
	.word	.LANCHOR2
	.fnend
	.size	HynixSetRRPara, .-HynixSetRRPara
	.align	2
	.global	FlashSetReadRetryDefault
	.type	FlashSetReadRetryDefault, %function
FlashSetReadRetryDefault:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	ldr	r3, .L260
	ldr	r3, [r3, #3320]
	ldrb	r3, [r3, #19]	@ zero_extendqisi2
	sub	r3, r3, #1
	cmp	r3, #7
	bxhi	lr
	stmfd	sp!, {r4, r5, r6, lr}
	.save {r4, r5, r6, lr}
	mov	r4, #0
	ldr	r5, .L260+4
	ldr	r6, .L260+8
.L253:
	ldrb	r3, [r5, r4, asl #3]	@ zero_extendqisi2
	uxtb	r0, r4
	cmp	r3, #173
	bne	.L254
	ldrb	r1, [r6, #-2731]	@ zero_extendqisi2
	mov	r3, #0
	ldr	r2, .L260+12
	bl	HynixSetRRPara
.L254:
	add	r4, r4, #1
	cmp	r4, #4
	bne	.L253
	ldmfd	sp!, {r4, r5, r6, pc}
.L261:
	.align	2
.L260:
	.word	.LANCHOR0
	.word	IDByte
	.word	.LANCHOR2
	.word	.LANCHOR2-2728
	.fnend
	.size	FlashSetReadRetryDefault, .-FlashSetReadRetryDefault
	.align	2
	.global	FlashReadStatusEN
	.type	FlashReadStatusEN, %function
FlashReadStatusEN:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	ldr	ip, .L273
	stmfd	sp!, {r3, r4, r5, lr}
	.save {r3, r4, r5, lr}
	add	r0, ip, r0, asl #3
	ldr	r3, [ip, #3320]
	ldrb	r5, [r0, #16]	@ zero_extendqisi2
	ldr	r4, [r0, #12]
	ldrb	r3, [r3, #8]	@ zero_extendqisi2
	cmp	r3, #2
	mov	r3, r5, asl #8
	addne	r3, r4, r3
	add	r5, r5, #8
	movne	r2, #112
	strne	r2, [r3, #2056]
	bne	.L267
	cmp	r2, #0
	add	r3, r4, r3
	ldrneb	r2, [ip, #58]	@ zero_extendqisi2
	ldreqb	r2, [ip, #57]	@ zero_extendqisi2
	str	r2, [r3, #2056]
	ldrb	r0, [ip, #59]	@ zero_extendqisi2
	cmp	r0, #0
	addne	ip, r4, r5, asl #8
	movne	r2, #0
	beq	.L267
.L266:
	cmp	r2, r0
	bcs	.L267
	mov	r3, r2, asl #3
	add	r2, r2, #1
	mov	r3, r1, lsr r3
	uxtb	r3, r3
	str	r3, [ip, #4]
	b	.L266
.L267:
	mov	r0, #80
	bl	NandcDelayns
	ldr	r0, [r4, r5, asl #8]
	uxtb	r0, r0
	ldmfd	sp!, {r3, r4, r5, pc}
.L274:
	.align	2
.L273:
	.word	.LANCHOR0
	.fnend
	.size	FlashReadStatusEN, .-FlashReadStatusEN
	.align	2
	.global	FlashWaitReadyEN
	.type	FlashWaitReadyEN, %function
FlashWaitReadyEN:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	stmfd	sp!, {r4, r5, r6, lr}
	.save {r4, r5, r6, lr}
	mov	r4, r0
	mov	r5, r1
	mov	r6, r2
.L279:
	mov	r0, r4
	mov	r1, r5
	mov	r2, r6
	bl	FlashReadStatusEN
	cmp	r0, #255
	beq	.L279
	tst	r0, #64
	beq	.L279
	ldmfd	sp!, {r4, r5, r6, pc}
	.fnend
	.size	FlashWaitReadyEN, .-FlashWaitReadyEN
	.align	2
	.global	FlashWaitCmdDone
	.type	FlashWaitCmdDone, %function
FlashWaitCmdDone:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	stmfd	sp!, {r3, r4, r5, r6, r7, lr}
	.save {r3, r4, r5, r6, r7, lr}
	ldr	r5, .L291
	add	r4, r5, r0, asl #4
	ldr	r3, [r4, #3200]
	ldrb	r7, [r4, #3192]	@ zero_extendqisi2
	cmp	r3, #0
	beq	.L285
	mov	r6, r0
	mov	r0, r7
	add	r5, r5, r6, asl #2
	bl	NandcFlashCs
	ldr	r1, [r4, #3196]
	mov	r0, r7
	ldr	r2, [r5, #3160]
	adds	r2, r2, #0
	movne	r2, #1
	bl	FlashWaitReadyEN
	mov	r5, r0
	mov	r0, r7
	bl	NandcFlashDeCs
	ldr	r2, [r4, #3200]
	sbfx	r3, r5, #0, #1
	str	r3, [r2]
	mov	r2, #0
	ldr	r1, [r4, #3204]
	str	r2, [r4, #3200]
	cmp	r1, r2
	strne	r3, [r1]
	strne	r2, [r4, #3204]
.L285:
	mov	r0, #0
	ldmfd	sp!, {r3, r4, r5, r6, r7, pc}
.L292:
	.align	2
.L291:
	.word	.LANCHOR0
	.fnend
	.size	FlashWaitCmdDone, .-FlashWaitCmdDone
	.align	2
	.type	flash_read_ecc, %function
flash_read_ecc:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	ldr	r2, .L295
	stmfd	sp!, {r4, lr}
	.save {r4, lr}
	add	r0, r2, r0, asl #3
	ldrb	r4, [r0, #16]	@ zero_extendqisi2
	ldr	r3, [r0, #12]
	mov	r0, #80
	add	r4, r3, r4, asl #8
	mov	r3, #122
	str	r3, [r4, #2056]
	bl	NandcDelayns
	ldr	r3, [r4, #2048]
	ldr	r0, [r4, #2048]
	and	r3, r3, #15
	and	r0, r0, #15
	cmp	r0, r3
	movcc	r0, r3
	ldr	r3, [r4, #2048]
	and	r3, r3, #15
	cmp	r0, r3
	movcc	r0, r3
	ldr	r3, [r4, #2048]
	and	r3, r3, #15
	cmp	r0, r3
	movcc	r0, r3
	ldmfd	sp!, {r4, pc}
.L296:
	.align	2
.L295:
	.word	.LANCHOR0
	.fnend
	.size	flash_read_ecc, .-flash_read_ecc
	.align	2
	.global	NandcWaitFlashReadyNoDelay
	.type	NandcWaitFlashReadyNoDelay, %function
NandcWaitFlashReadyNoDelay:
	.fnstart
	@ args = 0, pretend = 0, frame = 8
	@ frame_needed = 0, uses_anonymous_args = 0
	ldr	r3, .L303
	stmfd	sp!, {r0, r1, r2, r4, r5, lr}
	.save {r4, r5, lr}
	.pad #12
	add	r0, r3, r0, asl #3
	ldr	r4, .L303+4
	ldr	r5, [r0, #12]
.L299:
	ldr	r3, [r5]
	str	r3, [sp, #4]
	ldr	r3, [sp, #4]
	tst	r3, #512
	bne	.L300
	mov	r0, #10
	bl	NandcDelayns
	subs	r4, r4, #1
	bne	.L299
	mvn	r0, #0
	b	.L298
.L300:
	mov	r0, #0
.L298:
	add	sp, sp, #12
	@ sp needed
	ldmfd	sp!, {r4, r5, pc}
.L304:
	.align	2
.L303:
	.word	.LANCHOR0
	.word	100000
	.fnend
	.size	NandcWaitFlashReadyNoDelay, .-NandcWaitFlashReadyNoDelay
	.align	2
	.global	NandcWaitFlashReady
	.type	NandcWaitFlashReady, %function
NandcWaitFlashReady:
	.fnstart
	@ args = 0, pretend = 0, frame = 8
	@ frame_needed = 0, uses_anonymous_args = 0
	ldr	r3, .L311
	stmfd	sp!, {r0, r1, r2, r4, r5, lr}
	.save {r4, r5, lr}
	.pad #12
	add	r0, r3, r0, asl #3
	ldr	r4, .L311+4
	ldr	r5, [r0, #12]
	mov	r0, #150
	bl	NandcDelayns
.L307:
	ldr	r3, [r5]
	str	r3, [sp, #4]
	ldr	r3, [sp, #4]
	tst	r3, #512
	bne	.L308
	mov	r0, #10
	bl	NandcDelayns
	subs	r4, r4, #1
	bne	.L307
	mvn	r0, #0
	b	.L306
.L308:
	mov	r0, #0
.L306:
	add	sp, sp, #12
	@ sp needed
	ldmfd	sp!, {r4, r5, pc}
.L312:
	.align	2
.L311:
	.word	.LANCHOR0
	.word	100000
	.fnend
	.size	NandcWaitFlashReady, .-NandcWaitFlashReady
	.align	2
	.global	FlashReset
	.type	FlashReset, %function
FlashReset:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	ldr	r3, .L315
	stmfd	sp!, {r4, r5, r6, lr}
	.save {r4, r5, r6, lr}
	add	r3, r3, r0, asl #3
	mov	r4, r0
	ldrb	r6, [r3, #16]	@ zero_extendqisi2
	ldr	r5, [r3, #12]
	bl	NandcFlashCs
	mov	r3, #255
	mov	r0, r4
	add	r5, r5, r6, asl #8
	str	r3, [r5, #2056]
	bl	NandcWaitFlashReady
	mov	r0, r4
	ldmfd	sp!, {r4, r5, r6, lr}
	b	NandcFlashDeCs
.L316:
	.align	2
.L315:
	.word	.LANCHOR0
	.fnend
	.size	FlashReset, .-FlashReset
	.align	2
	.global	flash_enter_slc_mode
	.type	flash_enter_slc_mode, %function
flash_enter_slc_mode:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	stmfd	sp!, {r3, r4, r5, lr}
	.save {r3, r4, r5, lr}
	ldr	r3, .L320
	ldrb	r3, [r3, #-2744]	@ zero_extendqisi2
	cmp	r3, #0
	ldmeqfd	sp!, {r3, r4, r5, pc}
	mov	r5, r0
	bl	NandcFlashCs
	ldr	r3, .L320+4
	mov	r0, #100
	add	r3, r3, r5, asl #3
	ldr	r4, [r3, #12]
	ldrb	r3, [r3, #16]	@ zero_extendqisi2
	add	r4, r4, r3, asl #8
	mov	r3, #239
	str	r3, [r4, #2056]
	mov	r3, #145
	str	r3, [r4, #2052]
	bl	NandcDelayns
	mov	r3, #0
	mov	r2, #1
	str	r3, [r4, #2048]
	mov	r0, #100
	str	r2, [r4, #2048]
	str	r3, [r4, #2048]
	str	r3, [r4, #2048]
	bl	NandcDelayns
	mov	r0, r5
	bl	NandcWaitFlashReadyNoDelay
	mov	r3, #218
	mov	r0, r5
	str	r3, [r4, #2056]
	ldmfd	sp!, {r3, r4, r5, lr}
	b	NandcWaitFlashReady
.L321:
	.align	2
.L320:
	.word	.LANCHOR2
	.word	.LANCHOR0
	.fnend
	.size	flash_enter_slc_mode, .-flash_enter_slc_mode
	.align	2
	.global	flash_exit_slc_mode
	.type	flash_exit_slc_mode, %function
flash_exit_slc_mode:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	stmfd	sp!, {r3, r4, r5, lr}
	.save {r3, r4, r5, lr}
	ldr	r3, .L325
	ldrb	r3, [r3, #-2744]	@ zero_extendqisi2
	cmp	r3, #0
	ldmeqfd	sp!, {r3, r4, r5, pc}
	mov	r5, r0
	bl	NandcFlashCs
	ldr	r3, .L325+4
	mov	r0, #100
	add	r3, r3, r5, asl #3
	ldr	r4, [r3, #12]
	ldrb	r3, [r3, #16]	@ zero_extendqisi2
	add	r4, r4, r3, asl #8
	mov	r3, #239
	str	r3, [r4, #2056]
	mov	r3, #145
	str	r3, [r4, #2052]
	bl	NandcDelayns
	mov	r3, #2
	mov	r0, #100
	str	r3, [r4, #2048]
	mov	r3, #1
	str	r3, [r4, #2048]
	mov	r3, #0
	str	r3, [r4, #2048]
	str	r3, [r4, #2048]
	bl	NandcDelayns
	mov	r0, r5
	bl	NandcWaitFlashReadyNoDelay
	mov	r3, #223
	mov	r0, r5
	str	r3, [r4, #2056]
	ldmfd	sp!, {r3, r4, r5, lr}
	b	NandcWaitFlashReady
.L326:
	.align	2
.L325:
	.word	.LANCHOR2
	.word	.LANCHOR0
	.fnend
	.size	flash_exit_slc_mode, .-flash_exit_slc_mode
	.align	2
	.global	FlashEraseBlock
	.type	FlashEraseBlock, %function
FlashEraseBlock:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	stmfd	sp!, {r4, r5, r6, lr}
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
	ldmfd	sp!, {r4, r5, r6, pc}
	.fnend
	.size	FlashEraseBlock, .-FlashEraseBlock
	.align	2
	.global	FlashSetInterfaceMode
	.type	FlashSetInterfaceMode, %function
FlashSetInterfaceMode:
	.fnstart
	@ args = 0, pretend = 0, frame = 8
	@ frame_needed = 0, uses_anonymous_args = 0
	ldr	r3, .L352
	stmfd	sp!, {r0, r1, r2, r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.save {r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.pad #12
	mov	lr, #0
	ldrb	r2, [r3, #-1876]	@ zero_extendqisi2
	mov	r4, #239
	ldr	r7, .L352+4
	mov	r5, #128
	and	r3, r2, #1
	and	r2, r2, #4
	str	r3, [sp, #4]
	mov	r6, #1
	uxtb	r3, r2
	mov	r8, #35
	mov	r2, lr
	mov	r9, #32
	mov	r10, #5
	str	r3, [sp]
.L339:
	ldr	r3, .L352+8
	ldrb	ip, [lr, r7]	@ zero_extendqisi2
	ldr	r1, [r3, lr]!
	cmp	ip, #69
	cmpne	ip, #152
	ldrb	r3, [r3, #4]	@ zero_extendqisi2
	beq	.L330
	cmp	ip, #44
	cmpne	ip, #173
	bne	.L331
.L330:
	cmp	r0, #1
	bne	.L332
	ldr	fp, [sp, #4]
	cmp	fp, #0
	beq	.L331
	mov	r3, r3, asl #8
	cmp	ip, #173
	add	fp, r1, r3
	str	r4, [fp, #2056]
	streq	r0, [fp, #2052]
	beq	.L351
	cmp	ip, #44
	streq	r0, [fp, #2052]
	strne	r5, [fp, #2052]
	streq	r10, [fp, #2048]
	strne	r0, [fp, #2048]
	b	.L337
.L332:
	ldr	fp, [sp]
	cmp	fp, #0
	beq	.L331
	mov	r3, r3, asl #8
	cmp	ip, #173
	add	fp, r1, r3
	str	r4, [fp, #2056]
	streq	r6, [fp, #2052]
	streq	r9, [fp, #2048]
	beq	.L337
	cmp	ip, #44
	streq	r6, [fp, #2052]
	streq	r8, [fp, #2048]
	beq	.L337
	str	r5, [fp, #2052]
.L351:
	str	r2, [fp, #2048]
.L337:
	add	r3, r1, r3
	str	r2, [r3, #2048]
	str	r2, [r3, #2048]
	str	r2, [r3, #2048]
.L331:
	add	lr, lr, #8
	cmp	lr, #32
	bne	.L339
	mov	r0, #0
	bl	NandcWaitFlashReady
	mov	r0, #0
	add	sp, sp, #12
	@ sp needed
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L353:
	.align	2
.L352:
	.word	.LANCHOR2
	.word	IDByte
	.word	.LANCHOR0+12
	.fnend
	.size	FlashSetInterfaceMode, .-FlashSetInterfaceMode
	.align	2
	.global	FlashReadSpare
	.type	FlashReadSpare, %function
FlashReadSpare:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	ldr	ip, .L356
	stmfd	sp!, {r3, r4, r5, lr}
	.save {r3, r4, r5, lr}
	add	ip, ip, r0, asl #3
	ldr	r3, .L356+4
	mov	r5, r2
	ldrb	r2, [ip, #16]	@ zero_extendqisi2
	ldr	r4, [ip, #12]
	ldrb	r3, [r3, #265]	@ zero_extendqisi2
	add	r4, r4, r2, asl #8
	mov	r2, #0
	mov	r3, r3, asl #9
	str	r2, [r4, #2056]
	str	r3, [r4, #2052]
	mov	r3, r3, lsr #8
	str	r3, [r4, #2052]
	uxtb	r3, r1
	str	r3, [r4, #2052]
	mov	r3, r1, lsr #8
	mov	r1, r1, lsr #16
	str	r3, [r4, #2052]
	mov	r3, #48
	str	r1, [r4, #2052]
	str	r3, [r4, #2056]
	bl	NandcWaitFlashReady
	ldr	r3, [r4, #2048]
	strb	r3, [r5]
	ldmfd	sp!, {r3, r4, r5, pc}
.L357:
	.align	2
.L356:
	.word	.LANCHOR0
	.word	.LANCHOR1
	.fnend
	.size	FlashReadSpare, .-FlashReadSpare
	.align	2
	.global	SandiskProgTestBadBlock
	.type	SandiskProgTestBadBlock, %function
SandiskProgTestBadBlock:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	ldr	r2, .L360
	stmfd	sp!, {r4, lr}
	.save {r4, lr}
	add	r2, r2, r0, asl #3
	ldrb	r4, [r2, #16]	@ zero_extendqisi2
	ldr	r3, [r2, #12]
	add	r4, r3, r4, asl #8
	mov	r3, #162
	str	r3, [r4, #2056]
	mov	r3, #128
	str	r3, [r4, #2056]
	mov	r3, #0
	str	r3, [r4, #2052]
	str	r3, [r4, #2052]
	uxtb	r3, r1
	str	r3, [r4, #2052]
	mov	r3, r1, lsr #8
	mov	r1, r1, lsr #16
	str	r3, [r4, #2052]
	str	r1, [r4, #2052]
	mov	r3, #16
	str	r3, [r4, #2056]
	bl	NandcWaitFlashReady
	mov	r3, #112
	mov	r0, #80
	str	r3, [r4, #2056]
	bl	NandcDelayns
	ldr	r0, [r4, #2048]
	and	r0, r0, #1
	ldmfd	sp!, {r4, pc}
.L361:
	.align	2
.L360:
	.word	.LANCHOR0
	.fnend
	.size	SandiskProgTestBadBlock, .-SandiskProgTestBadBlock
	.align	2
	.global	SandiskSetRRPara
	.type	SandiskSetRRPara, %function
SandiskSetRRPara:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	stmfd	sp!, {r3, r4, r5, lr}
	.save {r3, r4, r5, lr}
	mov	r3, #239
	mov	r5, r0
	str	r3, [r0, #8]
	mov	r3, #17
	str	r3, [r0, #4]
	mov	r0, #200
	mov	r4, r1
	bl	NandcDelayns
	ldr	r1, .L370
	ldr	r0, .L370+4
	add	r4, r4, r4, asl #2
	ldr	ip, .L370+8
	sub	lr, r1, #48
	mov	r2, #0
.L363:
	ldrb	r3, [r0]	@ zero_extendqisi2
	cmp	r2, r3
	bcs	.L369
	ldrb	r3, [ip]	@ zero_extendqisi2
	cmp	r3, #67
	add	r3, r2, r4
	addeq	r3, lr, r3
	addne	r3, r1, r3
	add	r2, r2, #1
	ldrsb	r3, [r3, #5]
	str	r3, [r5]
	b	.L363
.L369:
	mov	r0, #0
	ldmfd	sp!, {r3, r4, r5, lr}
	b	NandcWaitFlashReady
.L371:
	.align	2
.L370:
	.word	.LANCHOR1+3120
	.word	g_maxRegNum
	.word	g_retryMode
	.fnend
	.size	SandiskSetRRPara, .-SandiskSetRRPara
	.align	2
	.global	micron_auto_read_calibration_config
	.type	micron_auto_read_calibration_config, %function
micron_auto_read_calibration_config:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	stmfd	sp!, {r3, r4, r5, lr}
	.save {r3, r4, r5, lr}
	mov	r4, r0
	mov	r5, r1
	bl	NandcWaitFlashReady
	ldr	r3, .L374
	mov	r0, #200
	add	r2, r3, r4, asl #3
	ldrb	r4, [r2, #16]	@ zero_extendqisi2
	ldr	r3, [r2, #12]
	add	r4, r3, r4, asl #8
	mov	r3, #239
	str	r3, [r4, #2056]
	mov	r3, #150
	str	r3, [r4, #2052]
	bl	NandcDelayns
	str	r5, [r4, #2048]
	mov	r3, #0
	str	r3, [r4, #2048]
	str	r3, [r4, #2048]
	str	r3, [r4, #2048]
	ldmfd	sp!, {r3, r4, r5, pc}
.L375:
	.align	2
.L374:
	.word	.LANCHOR0
	.fnend
	.size	micron_auto_read_calibration_config, .-micron_auto_read_calibration_config
	.align	2
	.global	FlashEraseSLc2KBlocks
	.type	FlashEraseSLc2KBlocks, %function
FlashEraseSLc2KBlocks:
	.fnstart
	@ args = 0, pretend = 0, frame = 8
	@ frame_needed = 0, uses_anonymous_args = 0
	stmfd	sp!, {r4, r5, r6, r7, r8, r9, lr}
	.save {r4, r5, r6, r7, r8, r9, lr}
	mov	r5, #0
	ldr	r8, .L388
	.pad #20
	sub	sp, sp, #20
	mov	r6, r0
	mov	r9, r1
	mov	r7, r5
.L377:
	cmp	r7, r9
	beq	.L387
	rsb	r3, r7, r9
	add	r2, sp, #8
	add	r0, r6, r5
	mov	r1, #0
	uxtb	r3, r3
	str	r3, [sp]
	add	r3, sp, #12
	bl	LogAddr2PhyAddr
	ldrb	r2, [r8, #3148]	@ zero_extendqisi2
	ldr	r3, [sp, #12]
	cmp	r3, r2
	mvncs	r3, #0
	strcs	r3, [r6, r5]
	bcs	.L379
	add	r2, r8, r3
	add	r3, r8, r3, asl #4
	ldrb	r4, [r2, #3152]	@ zero_extendqisi2
	strb	r4, [r3, #3192]
	mov	r0, r4
	bl	NandcWaitFlashReady
	mov	r0, r4
	bl	NandcFlashCs
	mov	r2, #0
	mov	r0, r4
	ldr	r1, [sp, #8]
	bl	FlashEraseCmd
	mov	r0, r4
	bl	NandcWaitFlashReady
	mov	r0, r4
	ldr	r1, [sp, #8]
	bl	FlashReadStatus
	mov	r2, #0
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
	tst	r0, #1
	mvnne	r3, #0
	strne	r3, [r6, r5]
	ldr	r3, [r6, r5]
	cmn	r3, #1
	bne	.L381
	ldr	r0, .L388+4
	ldr	r1, [sp, #8]
	bl	printk
.L381:
	mov	r0, r4
	bl	NandcFlashDeCs
.L379:
	add	r7, r7, #1
	add	r5, r5, #36
	b	.L377
.L387:
	mov	r0, #0
	add	sp, sp, #20
	@ sp needed
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, pc}
.L389:
	.align	2
.L388:
	.word	.LANCHOR0
	.word	.LC1
	.fnend
	.size	FlashEraseSLc2KBlocks, .-FlashEraseSLc2KBlocks
	.align	2
	.global	HynixGetReadRetryDefault
	.type	HynixGetReadRetryDefault, %function
HynixGetReadRetryDefault:
	.fnstart
	@ args = 0, pretend = 0, frame = 40
	@ frame_needed = 0, uses_anonymous_args = 0
	ldr	r3, .L499
	mvn	r2, #83
	stmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.save {r4, r5, r6, r7, r8, r9, r10, fp, lr}
	mov	r4, r0
	cmp	r4, #2
	strb	r0, [r3, #-2732]
	strb	r2, [r3, #-2728]
	mvn	r0, #82
	mvn	r1, #81
	mvn	r2, #80
	.pad #44
	sub	sp, sp, #44
	strb	r0, [r3, #-2727]
	strb	r1, [r3, #-2726]
	strb	r2, [r3, #-2725]
	bne	.L391
	mvn	r2, #88
	strb	r2, [r3, #-2728]
	ldr	r3, .L499+4
	mvn	r2, #8
	mov	r5, #7
	strb	r2, [r3, #3305]
	b	.L452
.L391:
	cmp	r4, #3
	bne	.L393
	mvn	r2, #79
	strb	r2, [r3, #-2728]
	mvn	r2, #78
	strb	r2, [r3, #-2727]
	mvn	r2, #77
	strb	r2, [r3, #-2726]
	mvn	r2, #76
	strb	r2, [r3, #-2725]
	mvn	r2, #75
	strb	r2, [r3, #-2724]
	mvn	r2, #74
	strb	r2, [r3, #-2723]
	mvn	r2, #73
	strb	r2, [r3, #-2722]
	mvn	r2, #72
	b	.L493
.L393:
	cmp	r4, #4
	bne	.L394
	mvn	ip, #51
	strb	r0, [r3, #-2723]
	strb	ip, [r3, #-2728]
	mvn	ip, #64
	strb	r1, [r3, #-2722]
	strb	ip, [r3, #-2727]
	mvn	ip, #85
	strb	ip, [r3, #-2726]
	mvn	ip, #84
	strb	ip, [r3, #-2725]
	mvn	ip, #50
	strb	ip, [r3, #-2724]
.L493:
	mov	r5, #8
	strb	r2, [r3, #-2721]
	mov	r6, r5
	b	.L392
.L394:
	cmp	r4, #5
	bne	.L395
	mov	r2, #56
	mov	r5, #8
	strb	r2, [r3, #-2728]
	mov	r2, #57
	strb	r2, [r3, #-2727]
	mov	r2, #58
	strb	r2, [r3, #-2726]
	mov	r2, #59
	strb	r2, [r3, #-2725]
	b	.L452
.L395:
	cmp	r4, #6
	bne	.L396
	mov	r2, #14
	mov	r5, #12
	strb	r2, [r3, #-2728]
	mov	r2, #15
	strb	r2, [r3, #-2727]
	mov	r2, #16
	strb	r2, [r3, #-2726]
	mov	r2, #17
	strb	r2, [r3, #-2725]
	b	.L452
.L396:
	cmp	r4, #7
	bne	.L397
	mvn	r2, #79
	mov	r5, #12
	strb	r2, [r3, #-2728]
	mov	r6, #10
	mvn	r2, #78
	strb	r2, [r3, #-2727]
	mvn	r2, #77
	strb	r2, [r3, #-2726]
	mvn	r2, #76
	strb	r2, [r3, #-2725]
	mvn	r2, #75
	strb	r2, [r3, #-2724]
	mvn	r2, #74
	strb	r2, [r3, #-2723]
	mvn	r2, #73
	strb	r2, [r3, #-2722]
	mvn	r2, #72
	strb	r2, [r3, #-2721]
	mvn	r2, #43
	strb	r2, [r3, #-2720]
	mvn	r2, #42
	strb	r2, [r3, #-2719]
	b	.L392
.L397:
	cmp	r4, #8
	mov	r5, #7
	bne	.L452
	mov	r2, #6
	strb	r5, [r3, #-2727]
	strb	r2, [r3, #-2728]
	mov	r5, #50
	mov	r2, #9
	strb	r4, [r3, #-2726]
	strb	r2, [r3, #-2725]
	mov	r6, #5
	mov	r2, #10
	strb	r2, [r3, #-2724]
	b	.L392
.L452:
	mov	r6, #4
.L392:
	sub	r3, r4, #1
	cmp	r3, #1
	bhi	.L489
	ldr	r10, .L499+8
	mov	r9, #0
.L398:
	ldr	r3, .L499+12
	uxtb	r2, r9
	ldrb	r1, [r3, #3148]	@ zero_extendqisi2
	cmp	r1, r2
	bls	.L405
	add	r2, r3, r2
	ldr	r8, .L499+16
	ldrb	r2, [r2, #3152]	@ zero_extendqisi2
	add	r3, r3, r2, asl #3
	add	r4, r8, r2, asl #6
	add	r4, r4, #20
	add	r8, r8, #3
	ldrb	r7, [r3, #16]	@ zero_extendqisi2
	ldr	r1, [r3, #12]
	mov	fp, r4
	mov	r2, r4
	add	r7, r1, r7, asl #8
	mov	r1, #55
.L400:
	str	r1, [r7, #2056]
	mov	r0, #80
	ldrb	r3, [r8, #1]!	@ zero_extendqisi2
	str	r2, [sp, #4]
	str	r1, [sp]
	str	r3, [r7, #2052]
	bl	NandcDelayns
	ldr	r3, [r7, #2048]
	ldr	r2, [sp, #4]
	ldr	r1, [sp]
	strb	r3, [r2], #1
	rsb	r3, r4, r2
	uxtb	r3, r3
	cmp	r3, r6
	bcc	.L400
	mov	r2, #0
.L401:
	add	r0, r10, r2
	mov	r3, #1
.L402:
	ldrb	r1, [r0, r3, asl #2]	@ zero_extendqisi2
	ldrb	ip, [fp]	@ zero_extendqisi2
	add	r1, r1, ip
	strb	r1, [fp, r3, asl #3]
	add	r3, r3, #1
	cmp	r3, #7
	bne	.L402
	add	r2, r2, #1
	add	fp, fp, #1
	cmp	r2, #4
	bne	.L401
	mov	r3, #0
	add	r9, r9, #1
	strb	r3, [r4, #16]
	strb	r3, [r4, #24]
	strb	r3, [r4, #32]
	strb	r3, [r4, #40]
	strb	r3, [r4, #48]
	strb	r3, [r4, #41]
	strb	r3, [r4, #49]
	b	.L398
.L489:
	sub	r3, r4, #3
	cmp	r3, #5
	bhi	.L405
	smulbb	r8, r6, r5
	ldr	fp, .L499
	mov	r3, r8, asl #4
	mov	r8, r8, asr #1
	str	r3, [sp, #36]
	mov	r3, r8, asl #1
	str	r3, [sp, #4]
	mov	r3, #0
	str	r3, [sp, #16]
.L406:
	ldrb	r3, [sp, #16]	@ zero_extendqisi2
	str	r3, [sp, #12]
	ldr	r3, .L499+12
	ldr	r2, [sp, #12]
	ldrb	r3, [r3, #3148]	@ zero_extendqisi2
	cmp	r3, r2
	bhi	.L450
.L405:
	ldr	r3, .L499
	strb	r6, [r3, #-2731]
	strb	r5, [r3, #-2730]
	add	sp, sp, #44
	@ sp needed
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L450:
	ldr	r2, [sp, #12]
	ldr	r3, .L499+12
	add	r3, r3, r2
	ldrb	r10, [r3, #3152]	@ zero_extendqisi2
	ldr	r3, .L499+12
	add	r3, r3, r10, asl #3
	mov	r0, r10
	ldr	r8, [r3, #12]
	ldrb	r3, [r3, #16]	@ zero_extendqisi2
	mov	r9, r3, asl #8
	str	r3, [sp, #8]
	add	r7, r8, r9
	mov	r3, #255
	str	r3, [r7, #2056]
	bl	NandcWaitFlashReady
	cmp	r4, #7
	ldreq	r7, .L499+16
	moveq	r3, #160
	mlaeq	r7, r3, r10, r7
	addeq	r3, r7, #28
	beq	.L494
.L407:
	cmp	r4, #8
	beq	.L409
	ldr	r7, .L499+16
	add	r7, r7, r10, asl #6
	add	r3, r7, #20
.L494:
	str	r3, [sp, #20]
	cmp	r4, #4
	add	r3, r8, r9
	mov	r2, #54
	str	r2, [r3, #2056]
	bne	.L410
	mov	r2, #255
	str	r2, [r3, #2052]
	mov	r2, #64
	str	r2, [r3, #2048]
	mov	r2, #204
	b	.L495
.L410:
	sub	r2, r4, #5
	cmp	r2, #1
	ldrlsb	r2, [fp, #-2728]	@ zero_extendqisi2
	strls	r2, [r3, #2052]
	movls	r2, #82
	bls	.L496
	cmp	r4, #7
	bne	.L411
	mov	r2, #174
	str	r2, [r3, #2052]
	mov	r2, #0
	str	r2, [r3, #2048]
	mov	r2, #176
.L495:
	str	r2, [r3, #2052]
	mov	r2, #77
.L496:
	str	r2, [r3, #2048]
.L411:
	add	r9, r8, r9
	cmp	r4, #6
	mov	r3, #22
	str	r3, [r9, #2056]
	mov	r3, #23
	str	r3, [r9, #2056]
	mov	r3, #4
	str	r3, [r9, #2056]
	mov	r3, #25
	str	r3, [r9, #2056]
	mov	r3, #0
	str	r3, [r9, #2056]
	str	r3, [r9, #2052]
	str	r3, [r9, #2052]
	moveq	r3, #31
	str	r3, [r9, #2052]
	mov	r3, #2
	str	r3, [r9, #2052]
	mov	r3, #0
	str	r3, [r9, #2052]
.L451:
	ldr	r3, [sp, #8]
	mov	r2, #48
	mov	r0, r10
	add	r3, r8, r3, asl #8
	str	r2, [r3, #2056]
	bl	NandcWaitFlashReady
	sub	r3, r4, #5
	cmp	r3, #1
	movhi	r3, #0
	movls	r3, #1
	str	r3, [sp, #24]
	sub	r3, r4, #8
	clz	r3, r3
	mov	r3, r3, lsr #5
	str	r3, [sp]
	ldr	r2, [sp]
	ldr	r3, [sp, #24]
	orrs	r3, r3, r2
	movne	ip, #16
	bne	.L415
	cmp	r4, #7
	movne	ip, #2
	moveq	ip, #32
.L415:
	ldr	r2, [sp, #8]
	ldr	r3, [fp, #-1872]
	add	r1, r8, r2, asl #8
	mov	r0, r3
.L416:
	ldr	r2, [r1, #2048]
	strb	r2, [r0], #1
	rsb	r2, r3, r0
	uxtb	r2, r2
	cmp	r2, ip
	bcc	.L416
	ldr	r2, [sp]
	cmp	r2, #0
	beq	.L417
	mov	r2, #0
.L419:
	ldrb	ip, [r3, r2, asl #2]	@ zero_extendqisi2
	uxtb	r0, r2
	cmp	ip, #50
	beq	.L418
	add	ip, r3, r2, asl #2
	ldrb	ip, [ip, #1]	@ zero_extendqisi2
	cmp	ip, #5
	beq	.L418
	add	r2, r2, #1
	cmp	r2, #8
	bne	.L419
	b	.L420
.L418:
	cmp	r0, #6
	bls	.L421
.L420:
	ldr	r0, .L499+20
	mov	r1, #0
	bl	printk
.L422:
	b	.L422
.L417:
	cmp	r4, #7
	bne	.L423
	ldr	r2, [sp]
.L425:
	ldrb	ip, [r3, r2, asl #2]	@ zero_extendqisi2
	uxtb	r0, r2
	cmp	ip, #12
	beq	.L424
	add	ip, r3, r2, asl #2
	ldrb	ip, [ip, #1]	@ zero_extendqisi2
	cmp	ip, #10
	beq	.L424
	add	r2, r2, #1
	cmp	r2, #8
	bne	.L425
	b	.L426
.L424:
	cmp	r0, #7
	bne	.L421
.L426:
	ldr	r0, .L499+20
	mov	r1, #0
	bl	printk
.L427:
	b	.L427
.L423:
	cmp	r4, #6
	bne	.L421
	sub	r2, r3, #1
	add	r3, r3, #7
.L428:
	ldrb	r0, [r2, #1]!	@ zero_extendqisi2
	cmp	r0, #12
	beq	.L421
	ldrb	r0, [r2, #8]	@ zero_extendqisi2
	cmp	r0, #4
	beq	.L421
	cmp	r2, r3
	bne	.L428
	ldr	r0, .L499+20
	mov	r1, #0
	bl	printk
.L430:
	b	.L430
.L421:
	ldr	r2, [fp, #-1872]
	ldr	r0, [sp, #36]
	add	r0, r2, r0
	mov	r3, r2
.L431:
	cmp	r3, r0
	ldrne	ip, [r1, #2048]
	strneb	ip, [r3], #1
	bne	.L431
.L498:
	ldr	r3, .L499
	mov	r0, #8
	ldr	r1, [sp, #4]
	ldr	r3, [r3, #-1872]
	add	r1, r3, r1
	str	r1, [sp, #28]
.L434:
	ldr	ip, [sp, #4]
	add	lr, r1, ip
.L433:
	ldrh	ip, [r1]
	mvn	ip, ip
	strh	ip, [r1], #2	@ movhi
	cmp	r1, lr
	bne	.L433
	ldr	ip, [sp, #4]
	subs	r0, r0, #1
	add	r1, r1, ip
	bne	.L434
.L435:
	mov	ip, #0
	mov	r0, ip
.L438:
	mov	r1, #1
	mov	lr, #0
	mov	r1, r1, asl r0
	mov	r7, #16
	str	r7, [sp, #32]
	mov	r7, lr
.L436:
	ldrh	r9, [r3, lr]
	and	r9, r9, r1
	cmp	r9, r1
	addeq	r7, r7, #1
	ldr	r9, [sp, #4]
	add	lr, lr, r9
	ldr	r9, [sp, #32]
	subs	r9, r9, #1
	str	r9, [sp, #32]
	bne	.L436
	cmp	r7, #8
	add	r0, r0, #1
	orrhi	ip, ip, r1
	uxthhi	ip, ip
	cmp	r0, #16
	bne	.L438
	ldr	r1, [sp, #28]
	strh	ip, [r3], #2	@ movhi
	cmp	r3, r1
	bne	.L435
	ldr	r1, [fp, #-1872]
	mov	r3, #0
	sub	r0, r1, #4
	add	ip, r1, #28
.L440:
	ldr	lr, [r0, #4]!
	cmp	lr, #0
	addeq	r3, r3, #1
	cmp	r0, ip
	bne	.L440
	cmp	r3, #7
	ble	.L441
	ldr	r0, .L499+24
	mov	r2, #1
	mov	r3, #1024
	bl	rknand_print_hex
	ldr	r0, .L499+20
	mov	r1, #0
	bl	printk
.L442:
	b	.L442
.L441:
	cmp	r4, #6
	moveq	r0, #4
	beq	.L443
	cmp	r4, #7
	moveq	r0, #10
	beq	.L443
	ldr	r3, [sp]
	cmp	r3, #0
	moveq	r0, #8
	movne	r0, #5
.L443:
	sub	r9, r6, #1
	ldr	r1, [sp, #20]
	mov	ip, #0
	uxtb	r9, r9
	add	r9, r9, #1
.L444:
	mov	r7, r1
	mov	r3, r2
.L445:
	ldrb	lr, [r3], #1	@ zero_extendqisi2
	strb	lr, [r7], #1
	rsb	lr, r2, r3
	uxtb	lr, lr
	cmp	lr, r6
	bcc	.L445
	add	ip, ip, #1
	add	r2, r2, r9
	cmp	ip, r5
	add	r1, r1, r0
	blt	.L444
	ldr	r3, [sp, #8]
	mov	r0, r10
	add	r7, r8, r3, asl #8
	mov	r3, #255
	str	r3, [r7, #2056]
	bl	NandcWaitFlashReady
	ldr	r3, [sp, #24]
	cmp	r3, #0
	beq	.L447
	mov	r3, #54
	str	r3, [r7, #2056]
	ldrb	r3, [fp, #-2728]	@ zero_extendqisi2
	mvn	r1, #0
	ldr	r0, [sp, #12]
	str	r3, [r7, #2052]
	mov	r3, #0
	str	r3, [r7, #2048]
	mov	r3, #22
	str	r3, [r7, #2056]
	bl	FlashReadCmd
	b	.L448
.L447:
	ldr	r3, [sp]
	cmp	r3, #0
	movne	r3, #190
	moveq	r3, #56
	str	r3, [r7, #2056]
.L448:
	mov	r0, r10
	bl	NandcWaitFlashReady
	ldr	r3, [sp, #16]
	add	r3, r3, #1
	str	r3, [sp, #16]
	b	.L406
.L409:
	mov	r3, #120
	mov	r2, #23
	str	r3, [r7, #2056]
	mov	r1, #25
	mov	r3, #0
	str	r3, [r7, #2052]
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
	ldr	r3, .L499+28
	str	r3, [sp, #20]
	b	.L451
.L500:
	.align	2
.L499:
	.word	.LANCHOR2
	.word	.LANCHOR1
	.word	.LANCHOR1+3288
	.word	.LANCHOR0
	.word	.LANCHOR2-2732
	.word	.LC2
	.word	.LC3
	.word	.LANCHOR2-2704
	.fnend
	.size	HynixGetReadRetryDefault, .-HynixGetReadRetryDefault
	.align	2
	.global	FlashGetReadRetryDefault
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
	bhi	.L503
	b	HynixGetReadRetryDefault
.L503:
	cmp	r3, #49
	bne	.L504
	ldr	r2, .L515
	ldr	r0, .L515+4
	ldr	r1, .L515+8
	strb	r3, [r2, #-2732]
	mov	r3, #4
	strb	r3, [r2, #-2731]
	mov	r3, #15
	strb	r3, [r2, #-2730]
	mov	r2, #64
	b	.L513
.L504:
	sub	r2, r3, #65
	cmp	r3, #33
	cmpne	r2, #1
	ldrls	r2, .L515
	strlsb	r3, [r2, #-2732]
	movls	r3, #4
	bls	.L514
.L505:
	cmp	r3, #34
	cmpne	r3, #67
	bne	.L506
	ldr	r2, .L515
	strb	r3, [r2, #-2732]
	mov	r3, #5
.L514:
	strb	r3, [r2, #-2731]
	mov	r3, #7
	ldr	r0, .L515+4
	strb	r3, [r2, #-2730]
	mov	r2, #45
	ldr	r1, .L515+12
	b	.L513
.L506:
	cmp	r3, #35
	cmpne	r3, #68
	bxne	lr
	ldr	r2, .L515
	ldr	r0, .L515+4
	ldr	r1, .L515+16
	strb	r3, [r2, #-2732]
	mov	r3, #5
	strb	r3, [r2, #-2731]
	mov	r3, #17
	strb	r3, [r2, #-2730]
	mov	r2, #95
.L513:
	b	ftl_memcpy
.L516:
	.align	2
.L515:
	.word	.LANCHOR2
	.word	.LANCHOR2-2728
	.word	.LANCHOR1+3224
	.word	.LANCHOR1+3072
	.word	.LANCHOR1+3120
	.fnend
	.size	FlashGetReadRetryDefault, .-FlashGetReadRetryDefault
	.align	2
	.global	FlashReadDpCmd
	.type	FlashReadDpCmd, %function
FlashReadDpCmd:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	stmfd	sp!, {r3, r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.save {r3, r4, r5, r6, r7, r8, r9, r10, fp, lr}
	mov	r6, r1
	ldr	r3, .L522
	mov	r7, r0
	uxtb	lr, r6
	mov	ip, r6, lsr #8
	add	r1, r3, r0, asl #3
	uxtb	r9, r2
	ldrb	r10, [r3, #52]	@ zero_extendqisi2
	mov	r8, r2, lsr #8
	ldrb	r4, [r1, #16]	@ zero_extendqisi2
	mov	r5, r2, lsr #16
	ldr	fp, [r1, #12]
	ldrb	r1, [r3, #60]	@ zero_extendqisi2
	mov	r4, r4, asl #8
	ldrb	r3, [r3, #53]	@ zero_extendqisi2
	cmp	r1, #1
	add	r4, fp, r4
	mov	r1, r6, lsr #16
	str	r10, [r4, #2056]
	bne	.L518
	mov	r10, #0
	str	r10, [r4, #2052]
	str	r10, [r4, #2052]
	str	lr, [r4, #2052]
	str	ip, [r4, #2052]
	str	r1, [r4, #2052]
	str	r3, [r4, #2056]
	bl	NandcWaitFlashReady
	str	r10, [r4, #2056]
	str	r10, [r4, #2052]
	str	r10, [r4, #2052]
	b	.L521
.L518:
	str	lr, [r4, #2052]
	str	ip, [r4, #2052]
	str	r1, [r4, #2052]
	str	r3, [r4, #2056]
.L521:
	str	r9, [r4, #2052]
	mov	r3, #48
	str	r8, [r4, #2052]
	mov	r0, r7
	str	r5, [r4, #2052]
	mov	r1, r6
	str	r3, [r4, #2056]
	ldmfd	sp!, {r3, r4, r5, r6, r7, r8, r9, r10, fp, lr}
	b	FlashSetRandomizer
.L523:
	.align	2
.L522:
	.word	.LANCHOR0
	.fnend
	.size	FlashReadDpCmd, .-FlashReadDpCmd
	.align	2
	.global	ftl_flash_de_init
	.type	ftl_flash_de_init, %function
ftl_flash_de_init:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	stmfd	sp!, {r3, r4, r5, lr}
	.save {r3, r4, r5, lr}
	mov	r0, #0
	bl	NandcWaitFlashReady
	bl	FlashSetReadRetryDefault
	ldr	r3, .L535
	ldr	r0, [r3, #-1868]
	mov	r4, r3
	cmp	r0, #0
	beq	.L525
	mov	r0, #0
	bl	flash_enter_slc_mode
	b	.L526
.L525:
	bl	flash_exit_slc_mode
.L526:
	ldrb	r3, [r4, #-1864]	@ zero_extendqisi2
	ldr	r5, .L535
	cmp	r3, #0
	beq	.L527
	ldrb	r3, [r5, #-1876]	@ zero_extendqisi2
	tst	r3, #1
	beq	.L527
	mov	r0, #1
	bl	FlashSetInterfaceMode
	mov	r0, #1
	bl	NandcSetMode
	mov	r3, #0
	strb	r3, [r5, #-1864]
.L527:
	ldr	r3, .L535+4
	mov	r0, #0
	ldr	r3, [r3, #12]
	str	r0, [r3, #336]
	ldmfd	sp!, {r3, r4, r5, pc}
.L536:
	.align	2
.L535:
	.word	.LANCHOR2
	.word	.LANCHOR0
	.fnend
	.size	ftl_flash_de_init, .-ftl_flash_de_init
	.align	2
	.global	NandcRandmzSel
	.type	NandcRandmzSel, %function
NandcRandmzSel:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	@ link register save eliminated.
	ldr	r3, .L538
	add	r0, r3, r0, asl #3
	ldr	r3, [r0, #12]
	str	r1, [r3, #336]
	bx	lr
.L539:
	.align	2
.L538:
	.word	.LANCHOR0
	.fnend
	.size	NandcRandmzSel, .-NandcRandmzSel
	.global	__aeabi_idiv
	.align	2
	.global	NandcTimeCfg
	.type	NandcTimeCfg, %function
NandcTimeCfg:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	stmfd	sp!, {r4, lr}
	.save {r4, lr}
	mov	r4, r0
	mov	r0, #0
	bl	rknand_get_clk_rate
	ldr	r1, .L551
	bl	__aeabi_idiv
	ldr	r3, .L551+4
	ldr	r3, [r3, #-2808]
	cmp	r0, #250
	movwgt	r2, #8354
	bgt	.L549
	cmp	r0, #220
	bgt	.L550
	cmp	r0, #185
	movwgt	r2, #4226
	bgt	.L549
	cmp	r0, #160
	movwgt	r2, #4194
	bgt	.L549
	cmp	r4, #35
	movwls	r2, #4193
	bls	.L549
	cmp	r4, #99
	movwls	r2, #4225
	bls	.L549
.L550:
	movw	r2, #8322
.L549:
	str	r2, [r3, #4]
	ldmfd	sp!, {r4, pc}
.L552:
	.align	2
.L551:
	.word	1000000
	.word	.LANCHOR2
	.fnend
	.size	NandcTimeCfg, .-NandcTimeCfg
	.align	2
	.global	FlashTimingCfg
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
	bls	.L554
	movw	r3, #8322
	cmp	r0, r3
	bne	.L555
.L554:
	ldr	r3, .L556
	ldr	r3, [r3, #-2808]
	str	r0, [r3, #4]
.L555:
	ldr	r3, .L556+4
	ldrb	r0, [r3, #277]	@ zero_extendqisi2
	b	NandcTimeCfg
.L557:
	.align	2
.L556:
	.word	.LANCHOR2
	.word	.LANCHOR1
	.fnend
	.size	FlashTimingCfg, .-FlashTimingCfg
	.align	2
	.global	NandcInit
	.type	NandcInit, %function
NandcInit:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	stmfd	sp!, {r3, r4, r5, lr}
	.save {r3, r4, r5, lr}
	mov	r2, #0
	ldr	r4, .L561
	mov	r1, #1
	ldr	r3, .L561+4
	mov	r5, #0
	str	r0, [r4, #-2808]
	str	r2, [r3, #16]
	str	r0, [r3, #12]
	str	r1, [r3, #24]
	mov	r1, #2
	str	r0, [r3, #20]
	str	r1, [r3, #32]
	mov	r1, #3
	str	r0, [r3, #28]
	str	r1, [r3, #40]
	str	r0, [r3, #36]
	ldr	r3, [r0]
	and	r3, r3, #253952
	ubfx	r1, r3, #13, #1
	bfi	r3, r2, #13, #1
	ldr	r2, [r0, #352]
	orr	r3, r3, #256
	str	r1, [r4, #-1860]
	movw	r1, #2049
	ubfx	r2, r2, #16, #4
	str	r2, [r4, #-1856]
	ldr	r2, [r0, #352]
	cmp	r2, r1
	str	r2, [r4, #-1852]
	moveq	r2, #8
	streq	r2, [r4, #-1856]
	str	r3, [r0]
	mov	r0, #40
	ldr	r3, [r4, #-2808]
	str	r5, [r3, #336]
	bl	NandcTimeCfg
	ldr	r3, [r4, #-2808]
	movw	r2, #8322
	mov	r0, #36864
	str	r2, [r3, #344]
	ldr	r2, .L561+8
	str	r2, [r3, #304]
	bl	ftl_malloc
	str	r5, [r4, #-1820]
	str	r5, [r4, #-1812]
	str	r0, [r4, #-1848]
	str	r0, [r4, #-1844]
	add	r0, r0, #32768
	str	r0, [r4, #-1840]
	ldmfd	sp!, {r3, r4, r5, pc}
.L562:
	.align	2
.L561:
	.word	.LANCHOR2
	.word	.LANCHOR0
	.word	1579009
	.fnend
	.size	NandcInit, .-NandcInit
	.align	2
	.global	NandcGetTimeCfg
	.type	NandcGetTimeCfg, %function
NandcGetTimeCfg:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	ldr	ip, .L565
	str	lr, [sp, #-4]!
	.save {lr}
	ldr	lr, [ip, #-2808]
	ldr	lr, [lr, #4]
	str	lr, [r0]
	ldr	r0, [ip, #-2808]
	ldr	r0, [r0]
	str	r0, [r1]
	ldr	r1, [ip, #-2808]
	ldr	r1, [r1, #304]
	str	r1, [r2]
	ldr	r1, [ip, #-2808]
	ldr	r2, [r1, #308]
	ldr	r1, [r1, #344]
	uxtb	r2, r2
	orr	r2, r2, r1, asl #16
	str	r2, [r3]
	ldr	pc, [sp], #4
.L566:
	.align	2
.L565:
	.word	.LANCHOR2
	.fnend
	.size	NandcGetTimeCfg, .-NandcGetTimeCfg
	.align	2
	.global	NandcBchSel
	.type	NandcBchSel, %function
NandcBchSel:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	@ link register save eliminated.
	ldr	r3, .L575
	mov	r1, #1
	ldr	r2, [r3, #-2808]
	str	r0, [r3, #-1808]
	mov	r3, #0
	str	r1, [r2, #8]
	mov	r1, #16
	cmp	r0, r1
	bfi	r3, r1, #8, #8
	bfc	r3, #18, #1
	bne	.L568
.L571:
	bfc	r3, #4, #1
	b	.L569
.L568:
	cmp	r0, #24
	orreq	r3, r3, #16
	beq	.L569
	cmp	r0, #40
	orr	r3, r3, #262144
	orr	r3, r3, #16
	beq	.L571
.L569:
	orr	r3, r3, #1
	str	r3, [r2, #12]
	bx	lr
.L576:
	.align	2
.L575:
	.word	.LANCHOR2
	.fnend
	.size	NandcBchSel, .-NandcBchSel
	.align	2
	.global	FlashBchSel
	.type	FlashBchSel, %function
FlashBchSel:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	@ link register save eliminated.
	ldr	r3, .L578
	strb	r0, [r3, #-2743]
	b	NandcBchSel
.L579:
	.align	2
.L578:
	.word	.LANCHOR2
	.fnend
	.size	FlashBchSel, .-FlashBchSel
	.align	2
	.global	ftl_flash_resume
	.type	ftl_flash_resume, %function
ftl_flash_resume:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	ldr	r3, .L589
	stmfd	sp!, {r4, r5, r6, lr}
	.save {r4, r5, r6, lr}
	mov	r5, #0
	ldr	r2, [r3, #-2808]
	mov	r4, r3
	ldr	r1, [r3, #-2804]
	ldr	r6, .L589+4
	str	r1, [r2]
	ldr	r1, [r3, #-2800]
	ldr	r2, [r3, #-2808]
	str	r1, [r2, #4]
	ldr	r1, [r3, #-2796]
	str	r1, [r2, #8]
	ldr	r1, [r3, #-2792]
	str	r1, [r2, #12]
	ldr	r1, [r3, #-2788]
	str	r1, [r2, #304]
	ldr	r1, [r3, #-2784]
	str	r1, [r2, #308]
	ldr	r1, [r3, #-2780]
	str	r1, [r2, #336]
	ldr	r1, [r3, #-2776]
	str	r1, [r2, #344]
.L582:
	ldrb	r3, [r6, r5, asl #3]	@ zero_extendqisi2
	sub	r3, r3, #1
	uxtb	r3, r3
	cmp	r3, #253
	bhi	.L581
	uxtb	r0, r5
	bl	FlashReset
.L581:
	add	r5, r5, #1
	cmp	r5, #4
	bne	.L582
	ldrb	r3, [r4, #-1864]	@ zero_extendqisi2
	ldr	r5, .L589
	cmp	r3, #0
	beq	.L583
	mov	r0, #1
	bl	NandcSetMode
	ldrb	r0, [r5, #-1876]	@ zero_extendqisi2
	bl	FlashSetInterfaceMode
	ldrb	r0, [r5, #-1876]	@ zero_extendqisi2
	bl	NandcSetMode
	ldrb	r0, [r5, #-2787]	@ zero_extendqisi2
	bl	NandcSetDdrPara
.L583:
	ldr	r3, .L589+8
	ldmfd	sp!, {r4, r5, r6, lr}
	ldr	r3, [r3, #3320]
	ldrb	r0, [r3, #20]	@ zero_extendqisi2
	b	FlashBchSel
.L590:
	.align	2
.L589:
	.word	.LANCHOR2
	.word	IDByte
	.word	.LANCHOR0
	.fnend
	.size	ftl_flash_resume, .-ftl_flash_resume
	.align	2
	.global	ftl_nandc_get_irq_status
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
	.type	NandcIqrWaitFlashReady, %function
NandcIqrWaitFlashReady:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	stmfd	sp!, {r4, lr}
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
	tst	r3, #512
	bne	.L593
	mov	r0, r4
	ldmfd	sp!, {r4, lr}
	b	wait_for_nand_flash_ready
.L593:
	ldr	r3, [r4, #368]
	orr	r3, r3, #2
	str	r3, [r4, #368]
	ldr	r3, [r4, #364]
	bic	r3, r3, #2
	str	r3, [r4, #364]
	ldmfd	sp!, {r4, pc}
	.fnend
	.size	NandcIqrWaitFlashReady, .-NandcIqrWaitFlashReady
	.align	2
	.global	FlashEraseBlocks
	.type	FlashEraseBlocks, %function
FlashEraseBlocks:
	.fnstart
	@ args = 0, pretend = 0, frame = 16
	@ frame_needed = 0, uses_anonymous_args = 0
	stmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.save {r4, r5, r6, r7, r8, r9, r10, fp, lr}
	mov	r8, r2
	ldr	r4, .L630
	.pad #28
	sub	sp, sp, #28
	ldrb	r5, [r4]	@ zero_extendqisi2
	cmp	r5, #0
	beq	.L613
	mov	r1, r2
	bl	FlashEraseSLc2KBlocks
	b	.L597
.L613:
	ldr	fp, .L630+4
	mov	r9, r0
	mov	r10, r1
.L596:
	cmp	r5, r8
	bcs	.L628
	mov	r3, #36
	mov	r1, #0
	mul	r6, r3, r5
	add	r2, sp, #16
	add	r3, r9, r6
	str	r3, [sp, #12]
	rsb	r3, r5, r8
	ldr	r0, [sp, #12]
	uxtb	r3, r3
	str	r3, [sp]
	add	r3, sp, #20
	bl	LogAddr2PhyAddr
	ldrb	r3, [r4, #3148]	@ zero_extendqisi2
	mov	r7, r0
	ldr	r0, [sp, #20]
	cmp	r0, r3
	mvncs	r3, #0
	strcs	r3, [r9, r6]
	bcs	.L599
	ldrb	r3, [fp, #-1804]	@ zero_extendqisi2
	cmp	r3, #0
	add	r3, r4, r0, asl #4
	ldr	r3, [r3, #3200]
	moveq	r7, #0
	cmp	r3, #0
	beq	.L601
	uxtb	r0, r0
	bl	FlashWaitCmdDone
.L601:
	ldr	r2, [sp, #20]
	cmp	r7, #0
	ldr	r0, [sp, #12]
	addne	ip, r6, #36
	addne	ip, r9, ip
	mov	r3, r2, asl #4
	add	r2, r4, r2
	add	r1, r4, r3
	add	r3, r4, r3
	ldrb	r6, [r2, #3152]	@ zero_extendqisi2
	str	r0, [r1, #3200]
	mov	r0, #0
	str	r0, [r1, #3204]
	ldr	r0, [sp, #16]
	strne	ip, [r1, #3204]
	strb	r6, [r3, #3192]
	str	r0, [r1, #3196]
	mov	r0, r6
	bl	NandcFlashCs
	cmp	r10, #1
	mov	r0, r6
	bne	.L603
	ldr	r3, .L630+4
	ldrb	r3, [r3, #-2744]	@ zero_extendqisi2
	cmp	r3, #0
	beq	.L603
	bl	flash_enter_slc_mode
	b	.L604
.L603:
	bl	flash_exit_slc_mode
.L604:
	ldr	r3, [sp, #20]
	mov	r0, r6
	ldr	r1, [sp, #16]
	add	r5, r5, r7
	add	r3, r4, r3, asl #2
	ldr	r2, [r3, #3160]
	adds	r2, r2, #0
	movne	r2, #1
	bl	FlashWaitReadyEN
	mov	r0, r6
	mov	r2, r7
	ldr	r1, [sp, #16]
	bl	FlashEraseCmd
	mov	r0, r6
	bl	NandcFlashDeCs
.L599:
	add	r5, r5, #1
	b	.L596
.L628:
	ldr	r5, .L630+4
	mov	r6, #0
	ldr	r7, .L630+8
	ldr	r0, [r5, #-2808]
	bl	NandcIqrWaitFlashReady
.L606:
	ldrb	r3, [r4, #3148]	@ zero_extendqisi2
	cmp	r6, r3
	bcs	.L629
	uxtb	r0, r6
	bl	FlashWaitCmdDone
	cmp	r10, #1
	bne	.L607
	ldrb	r3, [r5, #-2744]	@ zero_extendqisi2
	cmp	r3, #0
	beq	.L607
	ldrb	r0, [r7, r6, asl #4]	@ zero_extendqisi2
	bl	flash_exit_slc_mode
.L607:
	add	r6, r6, #1
	b	.L606
.L629:
	ldr	r3, [r5, #-1800]
	cmp	r3, #0
	bne	.L609
.L611:
	mov	r0, #0
	b	.L597
.L609:
	ldr	r3, .L630+12
	ldrb	r3, [r3]	@ zero_extendqisi2
	cmp	r3, #69
	bne	.L611
	mov	r3, #0
	mov	r2, #36
	mov	r1, r3
.L610:
	cmp	r3, r8
	beq	.L611
	mul	r0, r2, r3
	add	r3, r3, #1
	str	r1, [r9, r0]
	b	.L610
.L597:
	add	sp, sp, #28
	@ sp needed
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L631:
	.align	2
.L630:
	.word	.LANCHOR0
	.word	.LANCHOR2
	.word	.LANCHOR0+3192
	.word	IDByte
	.fnend
	.size	FlashEraseBlocks, .-FlashEraseBlocks
	.align	2
	.global	NandcSendDumpDataStart
	.type	NandcSendDumpDataStart, %function
NandcSendDumpDataStart:
	.fnstart
	@ args = 0, pretend = 0, frame = 8
	@ frame_needed = 0, uses_anonymous_args = 0
	@ link register save eliminated.
	ldr	r2, [r0, #16]
	.pad #8
	sub	sp, sp, #8
	ldr	r3, .L634
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
.L635:
	.align	2
.L634:
	.word	538969130
	.fnend
	.size	NandcSendDumpDataStart, .-NandcSendDumpDataStart
	.align	2
	.global	NandcSendDumpDataDone
	.type	NandcSendDumpDataDone, %function
NandcSendDumpDataDone:
	.fnstart
	@ args = 0, pretend = 0, frame = 8
	@ frame_needed = 0, uses_anonymous_args = 0
	@ link register save eliminated.
	.pad #8
	sub	sp, sp, #8
.L637:
	ldr	r3, [r0, #8]
	str	r3, [sp, #4]
	ldr	r3, [sp, #4]
	tst	r3, #1048576
	beq	.L637
	add	sp, sp, #8
	@ sp needed
	bx	lr
	.fnend
	.size	NandcSendDumpDataDone, .-NandcSendDumpDataDone
	.align	2
	.global	NandcXferStart
	.type	NandcXferStart, %function
NandcXferStart:
	.fnstart
	@ args = 8, pretend = 0, frame = 24
	@ frame_needed = 0, uses_anonymous_args = 0
	stmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.save {r4, r5, r6, r7, r8, r9, r10, fp, lr}
	mov	r4, #0
	ldr	lr, .L659
	.pad #28
	sub	sp, sp, #28
	ldr	r5, .L659+4
	add	r0, lr, r0, asl #3
	mov	lr, #16
	ldr	r8, [sp, #64]
	ldr	r6, [r0, #12]
	ldrb	r0, [r0, #16]	@ zero_extendqisi2
	ldr	ip, [sp, #68]
	ldr	r7, [r6, #12]
	bfi	r7, lr, #8, #8
	bfi	r7, r4, #3, #1
	bfi	r4, r1, #1, #1
	orr	r4, r4, #8
	bfi	r7, r0, #5, #3
	mov	r0, #1
	bfi	r4, r0, #5, #2
	orr	r4, r4, #536870912
	mov	r3, r3, lsr r0
	orr	r4, r4, #1024
	bfi	r4, r3, #4, #1
	ldr	r3, [r5, #-1856]
	cmp	r3, #3
	bls	.L642
	ldr	r3, [r6, #16]
	str	r3, [sp, #20]
	ldr	r3, [sp, #20]
	bic	r3, r3, #4
	str	r3, [sp, #20]
	adds	r3, ip, #0
	movne	r3, #1
	cmp	ip, #0
	cmpeq	r8, #0
	str	r3, [sp, #8]
	beq	.L643
	cmp	r1, #0
	bne	.L644
.L652:
	add	r2, r2, #1
	cmp	r8, #0
	mov	r2, r2, asr #1
	movne	r0, r8
	bfi	r4, r2, #22, #6
	ldreq	r0, [r5, #-1844]
	b	.L646
.L644:
	ldr	r3, [r5, #-1808]
	mov	r9, r5
	cmp	r3, #25
	movcc	r3, #64
	movcs	r3, #128
	str	r3, [sp, #4]
	mov	r3, r2, lsr #1
	str	r3, [sp, #12]
	mov	r3, #0
	mov	r0, r3
.L648:
	ldr	lr, [sp, #12]
	cmp	r0, lr
	bcs	.L652
	ldr	lr, [sp, #8]
	mov	r10, r3, lsr #2
	add	r0, r0, #1
	cmp	lr, #0
	ldrneh	fp, [ip, #2]
	mvneq	fp, #0
	ldrneh	lr, [ip], #4
	ldreq	lr, [r9, #-1840]
	orrne	lr, lr, fp, asl #16
	ldrne	fp, [r9, #-1840]
	streq	fp, [lr, r10, asl #2]
	strne	lr, [fp, r10, asl #2]
	ldr	lr, [sp, #4]
	add	r3, r3, lr
	b	.L648
.L646:
	ldr	r3, [r5, #-1840]
	ubfx	r10, r4, #22, #5
	mov	r9, r1
	str	r0, [r5, #-1836]
	mov	r2, r9
	mov	r1, r10, asl #10
	str	r3, [r5, #-1832]
	bl	rknand_dma_map_single
	mov	r2, r9
	mov	r1, r10, asl #7
	clz	r9, r9
	mov	r9, r9, lsr #5
	str	r0, [r5, #-1828]
	ldr	r0, [r5, #-1832]
	bl	rknand_dma_map_single
	mov	r3, #1
	str	r3, [r5, #-1820]
	tst	r8, #3
	ldr	r3, [r5, #-1828]
	str	r0, [r5, #-1824]
	str	r3, [r6, #20]
	ldr	r3, [r5, #-1824]
	str	r3, [r6, #24]
	mov	r3, #0
	str	r3, [sp, #20]
	ldr	r3, [sp, #20]
	bic	r3, r3, #15872
	orr	r3, r3, #8192
	str	r3, [sp, #20]
	ldr	r3, [sp, #20]
	orr	r3, r3, #448
	str	r3, [sp, #20]
	ldreq	r3, [sp, #20]
	biceq	r3, r3, #56
	orreq	r3, r3, #16
	streq	r3, [sp, #20]
	ldr	r3, [sp, #20]
	orr	r3, r3, #4
	str	r3, [sp, #20]
	ldr	r3, [sp, #20]
	bic	r3, r3, #2
	orr	r9, r3, r9, asl #1
	str	r9, [sp, #20]
	ldr	r3, [sp, #20]
	orr	r3, r3, #1
	str	r3, [sp, #20]
.L643:
	ldr	r3, [sp, #20]
	str	r3, [r6, #16]
.L642:
	str	r7, [r6, #12]
	str	r4, [r6, #8]
	orr	r4, r4, #4
	str	r4, [r6, #8]
	add	sp, sp, #28
	@ sp needed
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L660:
	.align	2
.L659:
	.word	.LANCHOR0
	.word	.LANCHOR2
	.fnend
	.size	NandcXferStart, .-NandcXferStart
	.align	2
	.global	NandcXferComp
	.type	NandcXferComp, %function
NandcXferComp:
	.fnstart
	@ args = 0, pretend = 0, frame = 8
	@ frame_needed = 0, uses_anonymous_args = 0
	stmfd	sp!, {r0, r1, r4, r5, r6, lr}
	.save {r4, r5, r6, lr}
	.pad #8
	ldr	r5, .L701
	ldr	r3, .L701+4
	add	r0, r3, r0, asl #3
	ldr	r3, [r5, #-1856]
	cmp	r3, #3
	ldr	r4, [r0, #12]
	bls	.L692
	ldr	r3, [r4, #16]
	tst	r3, #4
	beq	.L692
	ldr	r6, [r4, #16]
	ldr	r3, [r4, #8]
	ubfx	r6, r6, #1, #1
	cmp	r6, #0
	str	r3, [sp]
	movne	r6, #0
	beq	.L674
.L664:
	ldr	r2, [r4, #28]
	ldr	r3, [sp]
	ubfx	r2, r2, #16, #5
	ubfx	r3, r3, #22, #6
	cmp	r2, r3
	bge	.L672
	ldr	r3, [r5, #-1856]
	cmp	r3, #5
	bhi	.L665
.L668:
	add	r6, r6, #1
	bic	r3, r6, #-16777216
	cmp	r3, #0
	bne	.L667
	ldr	r2, [r4, #28]
	mov	r1, r6
	ldr	r3, [sp]
	ubfx	r2, r2, #16, #5
	ldr	r0, .L701+8
	ubfx	r3, r3, #22, #6
	bl	printk
	ldr	r0, .L701+12
	mov	r1, r4
	mov	r2, #4
	mov	r3, #512
	bl	rknand_print_hex
	b	.L667
.L665:
	ldr	r3, [r4]
	str	r3, [sp, #4]
	ldr	r3, [sp, #4]
	tst	r3, #8192
	beq	.L668
	ldr	r3, [sp, #4]
	tst	r3, #131072
	beq	.L668
.L672:
	ldr	r3, [r5, #-1820]
	ldr	r4, .L701
	cmp	r3, #0
	beq	.L673
	ldr	r1, [sp]
	mov	r2, #0
	ldr	r0, [r4, #-1828]
	ubfx	r1, r1, #22, #5
	mov	r1, r1, asl #10
	bl	rknand_dma_unmap_single
	ldr	r0, [r4, #-1824]
	mov	r2, #0
	ldr	r1, [sp]
	ubfx	r1, r1, #22, #5
	mov	r1, r1, asl #7
	bl	rknand_dma_unmap_single
	b	.L673
.L667:
	mov	r0, #5
	mov	r1, #10
	bl	usleep_range
	b	.L664
.L676:
	ldr	r3, [r4, #8]
	add	r6, r6, #1
	str	r3, [sp]
	bic	r3, r6, #-16777216
	cmp	r3, #0
	bne	.L675
	ldr	r2, [sp]
	mov	r1, r6
	ldr	r3, [r4, #28]
	ldr	r0, .L701+16
	ubfx	r3, r3, #16, #5
	bl	printk
	ldr	r0, .L701+12
	mov	r1, r4
	mov	r2, #4
	mov	r3, #512
	bl	rknand_print_hex
.L675:
	mov	r0, #5
	mov	r1, #10
	bl	usleep_range
.L674:
	ldr	r3, [sp]
	tst	r3, #1048576
	beq	.L676
	ldr	r3, [r5, #-1812]
	cmp	r3, #0
	beq	.L677
	mov	r0, r4
	bl	NandcSendDumpDataStart
.L677:
	ldr	r3, [r5, #-1820]
	ldr	r6, .L701
	cmp	r3, #0
	beq	.L678
	ldr	r1, [sp]
	mov	r2, #1
	ldr	r0, [r6, #-1828]
	ubfx	r1, r1, #22, #5
	mov	r1, r1, asl #10
	bl	rknand_dma_unmap_single
	ldr	r0, [r6, #-1824]
	mov	r2, #1
	ldr	r1, [sp]
	ubfx	r1, r1, #22, #5
	mov	r1, r1, asl #7
	bl	rknand_dma_unmap_single
.L678:
	ldr	r3, [r5, #-1812]
	cmp	r3, #0
	beq	.L673
	mov	r0, r4
	bl	NandcSendDumpDataDone
.L673:
	mov	r3, #0
	str	r3, [r5, #-1820]
	b	.L661
.L692:
	ldr	r3, [r4, #8]
	str	r3, [sp]
	ldr	r3, [sp]
	tst	r3, #1048576
	beq	.L692
.L661:
	add	sp, sp, #8
	@ sp needed
	ldmfd	sp!, {r4, r5, r6, pc}
.L702:
	.align	2
.L701:
	.word	.LANCHOR2
	.word	.LANCHOR0
	.word	.LC4
	.word	.LC5
	.word	.LC6
	.fnend
	.size	NandcXferComp, .-NandcXferComp
	.align	2
	.global	NandcCopy1KB
	.type	NandcCopy1KB, %function
NandcCopy1KB:
	.fnstart
	@ args = 4, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	cmp	r1, #1
	stmfd	sp!, {r4, r5, r6, lr}
	.save {r4, r5, r6, lr}
	mov	r4, r2
	add	r2, r0, #4096
	ldr	r5, [sp, #16]
	add	r6, r0, #512
	add	r2, r2, r4, asl #9
	bne	.L704
	cmp	r3, #0
	beq	.L705
	mov	r0, r2
	mov	r1, r3
	mov	r2, #1024
	bl	ftl_memcpy
.L705:
	cmp	r5, #0
	ldmeqfd	sp!, {r4, r5, r6, pc}
	ldrb	r3, [r5, #2]	@ zero_extendqisi2
	mov	r4, r4, lsr #1
	ldrb	r2, [r5, #1]	@ zero_extendqisi2
	add	r4, r4, r4, asl #1
	mov	r3, r3, asl #16
	orr	r2, r3, r2, asl #8
	ldrb	r3, [r5]	@ zero_extendqisi2
	orr	r3, r2, r3
	ldrb	r2, [r5, #3]	@ zero_extendqisi2
	orr	r3, r3, r2, asl #24
	str	r3, [r6, r4, asl #4]
	ldmfd	sp!, {r4, r5, r6, pc}
.L704:
	cmp	r3, #0
	beq	.L708
	mov	r1, r2
	mov	r0, r3
	mov	r2, #1024
	bl	ftl_memcpy
.L708:
	cmp	r5, #0
	ldmeqfd	sp!, {r4, r5, r6, pc}
	mov	r4, r4, lsr #1
	add	r4, r4, r4, asl #1
	ldr	r3, [r6, r4, asl #4]
	mov	r2, r3, lsr #8
	strb	r3, [r5]
	strb	r2, [r5, #1]
	mov	r2, r3, lsr #16
	mov	r3, r3, lsr #24
	strb	r2, [r5, #2]
	strb	r3, [r5, #3]
	ldmfd	sp!, {r4, r5, r6, pc}
	.fnend
	.size	NandcCopy1KB, .-NandcCopy1KB
	.align	2
	.global	NandcXferData
	.type	NandcXferData, %function
NandcXferData:
	.fnstart
	@ args = 4, pretend = 0, frame = 80
	@ frame_needed = 0, uses_anonymous_args = 0
	stmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.save {r4, r5, r6, r7, r8, r9, r10, fp, lr}
	mov	fp, r3
	ldr	r3, .L768
	.pad #92
	sub	sp, sp, #92
	tst	fp, #63
	mov	r10, r0
	add	r3, r3, r0, asl #3
	mov	r7, r1
	mov	r8, r2
	ldr	r5, [sp, #128]
	ldr	r9, [r3, #12]
	bne	.L721
	cmp	r5, #0
	bne	.L722
	add	r0, sp, #24
	mov	r1, #255
	mov	r2, #64
	bl	ftl_memset
	add	r5, sp, #24
.L722:
	mov	r0, r10
	mov	r1, r7
	mov	r2, r8
	mov	r3, #0
	str	fp, [sp]
	str	r5, [sp, #4]
	bl	NandcXferStart
	mov	r0, r10
	mov	r1, r7
	bl	NandcXferComp
	cmp	r7, #0
	movne	r6, #0
	bne	.L723
	ldr	r4, .L768+4
	mov	r1, r8, lsr #1
	mov	r2, r7
	ldr	r3, [r4, #-1808]
	cmp	r3, #25
	mov	r3, r7
	movcc	ip, #64
	movcs	ip, #128
.L725:
	cmp	r2, r1
	add	r5, r5, #4
	add	r0, r3, ip
	bcs	.L766
	ldr	lr, [r4, #-1840]
	mov	r3, r3, lsr #2
	add	r2, r2, #1
	ldr	r3, [lr, r3, asl #2]
	mov	lr, r3, lsr #8
	strb	r3, [r5, #-4]
	strb	lr, [r5, #-3]
	mov	lr, r3, lsr #16
	mov	r3, r3, lsr #24
	strb	lr, [r5, #-2]
	strb	r3, [r5, #-1]
	mov	r3, r0
	b	.L725
.L766:
	ldr	r0, [r4, #-1808]
	mov	r2, #0
	ldr	r1, [r4, #-1856]
	mov	r8, r8, lsr #2
	mov	r6, r2
.L727:
	cmp	r2, r8
	bcs	.L723
	cmp	r0, #0
	beq	.L723
	add	r3, r2, #8
	ldr	r3, [r9, r3, asl #2]
	str	r3, [sp, #20]
	ldr	r3, [sp, #20]
	tst	r3, #4
	bne	.L751
	ldr	r4, [sp, #20]
	ubfx	r4, r4, #15, #1
	cmp	r4, #0
	bne	.L751
	cmp	r1, #5
	bls	.L729
	ldr	ip, [sp, #20]
	ldr	r4, [sp, #20]
	ldr	r3, [sp, #20]
	ubfx	ip, ip, #3, #5
	ldr	lr, [sp, #20]
	ubfx	r4, r4, #27, #1
	ubfx	r3, r3, #16, #5
	ubfx	lr, lr, #29, #1
	orr	ip, ip, r4, asl #5
	orr	r3, r3, lr, asl #5
	cmp	ip, r3
	ldr	r3, [sp, #20]
	ldrhi	r4, [sp, #20]
	ubfxhi	r3, r3, #3, #5
	ldrls	r4, [sp, #20]
	ubfxls	r3, r3, #16, #5
	ubfxhi	r4, r4, #27, #1
	ubfxls	r4, r4, #29, #1
	b	.L765
.L729:
	cmp	r1, #3
	bls	.L731
	ldr	ip, [sp, #20]
	ldr	r4, [sp, #20]
	ldr	r3, [sp, #20]
	ubfx	ip, ip, #3, #5
	ldr	lr, [sp, #20]
	ubfx	r4, r4, #28, #1
	ubfx	r3, r3, #16, #5
	ubfx	lr, lr, #30, #1
	orr	ip, ip, r4, asl #5
	orr	r3, r3, lr, asl #5
	cmp	ip, r3
	bls	.L732
	ldr	r3, [sp, #20]
	ldr	r4, [sp, #20]
	ubfx	r3, r3, #3, #5
	ubfx	r4, r4, #28, #1
.L765:
	orr	r4, r3, r4, asl #5
	b	.L731
.L732:
	ldr	r5, [sp, #20]
	ldr	r4, [sp, #20]
	ubfx	r5, r5, #16, #5
	ubfx	r4, r4, #30, #1
	orr	r4, r5, r4, asl #5
.L731:
	cmp	r6, r4
	movcc	r6, r4
	b	.L728
.L751:
	mvn	r6, #0
.L728:
	add	r2, r2, #1
	b	.L727
.L723:
	mov	r3, #0
	str	r3, [r9, #16]
	b	.L734
.L721:
	cmp	r1, #1
	mov	r4, #0
	bne	.L763
	mov	r6, r4
.L735:
	cmp	r4, r8
	bcs	.L767
	cmp	fp, #0
	and	ip, r4, #3
	addne	r3, fp, r4, asl #9
	mov	r0, r9
	moveq	r3, fp
	cmp	r5, #0
	mov	r1, #1
	str	ip, [sp, #8]
	movne	r2, #2
	moveq	r2, #0
	mla	r2, r4, r2, r5
	add	r4, r4, #2
	str	r2, [sp]
	mov	r2, ip
	bl	NandcCopy1KB
	mov	r0, r10
	mov	r1, #1
	mov	r2, #2
	ldr	ip, [sp, #8]
	str	r6, [sp]
	str	r6, [sp, #4]
	mov	r3, ip
	bl	NandcXferStart
	mov	r0, r10
	mov	r1, #1
	bl	NandcXferComp
	b	.L735
.L767:
	mov	r6, #0
	b	.L734
.L763:
	str	r4, [sp]
	mov	r1, r4
	str	r4, [sp, #4]
	mov	r2, #2
	mov	r3, r4
	mov	r6, r4
	bl	NandcXferStart
	str	fp, [sp, #8]
.L740:
	cmp	r4, r8
	bcs	.L734
	mov	r0, r10
	mov	r1, r7
	bl	NandcXferComp
	ldr	r3, [r9, #32]
	add	ip, r4, #2
	cmp	ip, r8
	str	r3, [sp, #20]
	bcs	.L741
	mov	r3, #0
	mov	r0, r10
	str	r3, [sp]
	mov	r1, r3
	str	r3, [sp, #4]
	mov	r2, #2
	and	r3, ip, #3
	str	ip, [sp, #12]
	bl	NandcXferStart
	ldr	ip, [sp, #12]
.L741:
	ldr	r3, [sp, #20]
	tst	r3, #4
	mvnne	r6, #0
	bne	.L742
	ldr	r2, [sp, #20]
	ldr	r3, [sp, #20]
	ubfx	r2, r2, #3, #5
	ubfx	r3, r3, #27, #1
	orr	r3, r2, r3, asl #5
	cmp	r6, r3
	movcc	r6, r3
.L742:
	cmp	fp, #0
	ldr	r3, [sp, #8]
	sub	r2, ip, #2
	mov	r0, r9
	moveq	r3, #0
	cmp	r5, #0
	and	r2, r2, #3
	str	ip, [sp, #12]
	movne	r1, #2
	moveq	r1, #0
	mla	r4, r4, r1, r5
	mov	r1, #0
	str	r4, [sp]
	bl	NandcCopy1KB
	ldr	ip, [sp, #12]
	ldr	r3, [sp, #8]
	mov	r4, ip
	add	r3, r3, #1024
	str	r3, [sp, #8]
	b	.L740
.L734:
	ldr	r3, .L768+4
	clz	r7, r7
	mov	r7, r7, lsr #5
	ldr	r3, [r3, #-1856]
	cmp	r3, #5
	movls	r7, #0
	cmp	r7, #0
	beq	.L746
	ldr	r3, [r9]
	and	r2, r3, #139264
	cmp	r2, #139264
	orreq	r3, r3, #131072
	streq	r3, [r9]
	mvneq	r6, #0
.L746:
	mov	r0, r6
	add	sp, sp, #92
	@ sp needed
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L769:
	.align	2
.L768:
	.word	.LANCHOR0
	.word	.LANCHOR2
	.fnend
	.size	NandcXferData, .-NandcXferData
	.align	2
	.global	FlashReadRawPage
	.type	FlashReadRawPage, %function
FlashReadRawPage:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	stmfd	sp!, {r0, r1, r4, r5, r6, r7, r8, lr}
	.save {r4, r5, r6, r7, r8, lr}
	.pad #8
	mov	r8, r3
	ldr	r3, .L773
	subs	r4, r0, #0
	mov	r6, r1
	mov	r7, r2
	ldrb	r5, [r3, #265]	@ zero_extendqisi2
	bne	.L771
	ldr	r2, .L773+4
	ldrb	r3, [r2, #1]	@ zero_extendqisi2
	ldr	r2, [r2, #4]
	mul	r2, r2, r3
	cmp	r1, r2
	movcc	r5, #4
.L771:
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
	mov	r1, #0
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
	ldmfd	sp!, {r4, r5, r6, r7, r8, pc}
.L774:
	.align	2
.L773:
	.word	.LANCHOR1
	.word	.LANCHOR0
	.fnend
	.size	FlashReadRawPage, .-FlashReadRawPage
	.align	2
	.global	FlashDdrTunningRead
	.type	FlashDdrTunningRead, %function
FlashDdrTunningRead:
	.fnstart
	@ args = 4, pretend = 0, frame = 16
	@ frame_needed = 0, uses_anonymous_args = 0
	stmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.save {r4, r5, r6, r7, r8, r9, r10, fp, lr}
	mov	r7, r3
	ldr	r4, .L802
	.pad #20
	sub	sp, sp, #20
	mov	r10, r0
	mov	fp, r2
	str	r1, [sp]
	ldr	r3, [r4, #-2808]
	ldr	r3, [r3, #304]
	str	r3, [sp, #8]
	ldr	r3, [r4, #-1856]
	cmp	r3, #8
	ldr	r3, [sp, #56]
	movcc	r9, #6
	movcs	r9, #12
	cmp	r3, #0
	moveq	r6, #1024
	beq	.L777
	mov	r0, #1
	bl	FlashSetInterfaceMode
	mov	r0, #1
	bl	NandcSetMode
	mov	r0, r10
	bl	FlashReset
	mov	r2, fp
	mov	r3, r7
	mov	r0, r10
	ldr	r1, [sp]
	bl	FlashReadRawPage
	mov	r6, r0
	ldrb	r0, [r4, #-1876]	@ zero_extendqisi2
	bl	FlashSetInterfaceMode
	ldrb	r0, [r4, #-1876]	@ zero_extendqisi2
	bl	NandcSetMode
	cmn	r6, #1
	bne	.L778
.L787:
	mvn	r6, #0
	b	.L779
.L778:
	ldr	r0, .L802+4
	mov	r2, r6
	ldr	r1, [sp]
	bl	printk
	cmp	r6, #9
	bhi	.L780
	ldr	r3, .L802+8
	add	r3, r3, r10, asl #3
	ldr	r3, [r3, #12]
	ldr	r2, [r3, #3840]
	ldr	r2, [r3]
	orr	r2, r2, #131072
	str	r2, [r3]
.L780:
	ldr	r3, [r4, #-1796]
	add	r3, r3, #1
	str	r3, [r4, #-1796]
	cmp	r3, #2048
	bcc	.L779
	ldr	r3, .L802
	mov	r7, #0
	mov	fp, r7
	str	r7, [r3, #-1796]
.L777:
	mov	r4, #0
	mvn	r8, #0
	mov	ip, r4
	mov	r5, r4
	str	r4, [sp, #4]
.L785:
	uxtb	r0, r9
	str	ip, [sp, #12]
	bl	NandcSetDdrPara
	mov	r3, r7
	mov	r0, r10
	mov	r2, fp
	ldr	r1, [sp]
	bl	FlashReadRawPage
	add	r3, r6, #1
	cmp	r0, r3
	ldr	ip, [sp, #12]
	bhi	.L781
	cmp	r0, #2
	bhi	.L791
	add	r5, r5, #1
	cmp	r5, #9
	bls	.L791
	rsb	r4, r5, r9
	mov	r6, r0
	mov	r8, #0
	b	.L783
.L781:
	ldr	r3, [sp, #4]
	cmp	r3, r5
	bcs	.L792
	cmp	r5, #7
	rsb	ip, r5, r4
	bhi	.L793
	str	r5, [sp, #4]
	b	.L792
.L791:
	mov	r8, #0
	mov	r4, r9
	mov	r6, r0
	mov	r7, r8
	mov	fp, r8
	b	.L782
.L792:
	mov	r5, #0
.L782:
	add	r9, r9, #2
	cmp	r9, #69
	bls	.L785
.L783:
	ldr	r3, [sp, #4]
	cmp	r3, r5
	movcs	r4, ip
	b	.L784
.L793:
	mov	r4, ip
.L784:
	cmp	r4, #0
	beq	.L786
	ldr	r0, .L802+12
	mov	r1, r4
	bl	printk
	uxtb	r0, r4
	bl	NandcSetDdrPara
.L786:
	cmn	r8, #1
	bne	.L779
	ldr	r0, .L802+16
	mov	r1, r10
	ldr	r2, [sp]
	bl	printk
	ldr	r3, [sp, #56]
	cmp	r3, #0
	beq	.L787
	ldr	r3, [sp, #8]
	ubfx	r0, r3, #8, #8
	bl	NandcSetDdrPara
.L779:
	mov	r0, r6
	add	sp, sp, #20
	@ sp needed
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L803:
	.align	2
.L802:
	.word	.LANCHOR2
	.word	.LC7
	.word	.LANCHOR0
	.word	.LC8
	.word	.LC9
	.fnend
	.size	FlashDdrTunningRead, .-FlashDdrTunningRead
	.align	2
	.global	FlashReadPage
	.type	FlashReadPage, %function
FlashReadPage:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	stmfd	sp!, {r0, r1, r2, r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.save {r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.pad #12
	mov	r5, r0
	mov	r6, r1
	mov	r8, r2
	mov	r7, r3
	bl	FlashReadRawPage
	ldr	r10, .L825
	cmn	r0, #1
	mov	r4, r0
	bne	.L805
	ldr	r9, .L825+4
	ldrb	fp, [r9, #8]	@ zero_extendqisi2
	cmp	fp, #0
	bne	.L806
.L808:
	ldrb	r3, [r10, #-1864]	@ zero_extendqisi2
	ldr	r9, .L825
	cmp	r3, #0
	beq	.L805
	b	.L824
.L806:
	mov	r3, #0
	mov	r0, r5
	strb	r3, [r9, #8]
	mov	r1, r6
	mov	r2, r8
	mov	r3, r7
	bl	FlashReadRawPage
	strb	fp, [r9, #8]
	cmn	r0, #1
	movne	r4, r0
	beq	.L808
	b	.L805
.L824:
	ldr	r3, [r9, #-2808]
	mov	r0, r5
	mov	r1, r6
	mov	r2, r8
	ldr	fp, [r3, #304]
	mov	r3, #1
	str	r3, [sp]
	mov	r3, r7
	bl	FlashDdrTunningRead
	cmn	r0, #1
	mov	r4, r0
	beq	.L809
	ldrb	r3, [r9, #-2743]	@ zero_extendqisi2
	cmp	r0, r3, lsr #1
	bls	.L805
.L809:
	ubfx	r0, fp, #8, #8
	bl	NandcSetDdrPara
.L805:
	ldr	ip, [r10, #-1792]
	ldr	r9, .L825
	adds	r3, ip, #0
	movne	r3, #1
	cmn	r4, #1
	movne	r3, #0
	cmp	r3, #0
	beq	.L810
	mov	r1, r6
	mov	r2, r8
	mov	r3, r7
	mov	r0, r5
	blx	ip
	mov	r2, r5
	mov	r3, r6
	mov	r4, r0
	ldr	r0, .L825+8
	mov	r1, r4
	bl	printk
	cmn	r4, #1
	bne	.L810
	ldrb	r3, [r9, #-2744]	@ zero_extendqisi2
	cmp	r3, #0
	beq	.L810
	mov	r0, r5
	bl	flash_enter_slc_mode
	ldr	ip, [r9, #-1792]
	mov	r0, r5
	mov	r1, r6
	mov	r2, r8
	mov	r3, r7
	blx	ip
	mov	r4, r0
	mov	r0, r5
	bl	flash_exit_slc_mode
.L810:
	mov	r0, r4
	add	sp, sp, #12
	@ sp needed
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L826:
	.align	2
.L825:
	.word	.LANCHOR2
	.word	.LANCHOR0
	.word	.LC10
	.fnend
	.size	FlashReadPage, .-FlashReadPage
	.align	2
	.global	FlashDdrParaScan
	.type	FlashDdrParaScan, %function
FlashDdrParaScan:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	stmfd	sp!, {r0, r1, r4, r5, r6, r7, r8, lr}
	.save {r4, r5, r6, r7, r8, lr}
	.pad #8
	mov	r7, r0
	ldr	r5, .L837
	mov	r6, r1
	mov	r4, #0
	ldrb	r0, [r5, #-1876]	@ zero_extendqisi2
	bl	FlashSetInterfaceMode
	ldrb	r0, [r5, #-1876]	@ zero_extendqisi2
	bl	NandcSetMode
	mov	r1, r6
	mov	r2, r4
	mov	r3, r4
	mov	r0, r7
	str	r4, [sp]
	bl	FlashDdrTunningRead
	mov	r3, r4
	mov	r1, r6
	mov	r2, r4
	mov	r8, r0
	mov	r0, r7
	bl	FlashReadRawPage
	mov	r3, r5
	cmn	r0, #1
	cmnne	r8, #1
	bne	.L828
	ldrb	r2, [r5, #-1876]	@ zero_extendqisi2
	tst	r2, #1
	beq	.L828
	mov	r0, #1
	bl	FlashSetInterfaceMode
	mov	r0, #1
	bl	NandcSetMode
	strb	r4, [r5, #-1864]
	b	.L829
.L828:
	mov	r2, #1
	strb	r2, [r3, #-1864]
.L829:
	mov	r0, #0
	add	sp, sp, #8
	@ sp needed
	ldmfd	sp!, {r4, r5, r6, r7, r8, pc}
.L838:
	.align	2
.L837:
	.word	.LANCHOR2
	.fnend
	.size	FlashDdrParaScan, .-FlashDdrParaScan
	.align	2
	.global	FlashLoadPhyInfo
	.type	FlashLoadPhyInfo, %function
FlashLoadPhyInfo:
	.fnstart
	@ args = 0, pretend = 0, frame = 16
	@ frame_needed = 0, uses_anonymous_args = 0
	stmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.save {r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.pad #20
	sub	sp, sp, #20
	ldr	r3, .L855
	mov	r4, #0
	ldr	r8, .L855+4
	mov	r9, #4
	ldr	r5, .L855+8
	mvn	r7, #0
	ldr	r0, [r3]	@ unaligned
	ldr	r3, [r8, #4]
	str	r4, [r5, #-1784]
	mov	r6, r5
	str	r0, [sp, #12]	@ unaligned
	mov	r0, r4
	str	r3, [sp, #4]
	ldr	r3, [r5, #-1872]
	str	r3, [r5, #-1788]
	bl	flash_enter_slc_mode
.L840:
	add	fp, r4, #1
	mov	r10, #0
.L842:
	add	r3, sp, #12
	ldrb	r0, [r3, r10]	@ zero_extendqisi2
	bl	FlashBchSel
	mov	r0, #0
	mov	r1, r4
	ldr	r2, [r5, #-1872]
	mov	r3, r0
	bl	FlashReadRawPage
	cmn	r0, #1
	bne	.L841
	mov	r0, #0
	mov	r1, fp
	ldr	r2, [r6, #-1872]
	mov	r3, r0
	bl	FlashReadRawPage
	cmn	r0, #1
	bne	.L841
	add	r10, r10, #1
	cmp	r10, #4
	beq	.L843
	b	.L842
.L844:
	add	r0, fp, #12
	movw	r1, #2036
	bl	js_hash
	ldr	r3, [fp, #8]
	cmp	r3, r0
	mvnne	r7, #0
	bne	.L843
	ldr	r7, .L855+12
	add	r1, fp, #160
	mov	r2, #32
	mov	r0, r7
	bl	ftl_memcpy
	ldr	r1, [r6, #-1788]
	mov	r2, #32
	ldr	r0, .L855+16
	add	r1, r1, #192
	bl	ftl_memcpy
	ldr	r1, [r6, #-1788]
	mov	r2, #852
	ldr	r0, .L855+20
	add	r1, r1, #224
	bl	ftl_memcpy
	ldrh	r0, [r7, #10]
	bl	FlashBlockAlignInit
	ldr	r7, [r6, #-1788]
	str	r4, [r6, #-1784]
	mov	r0, r4
	ldr	r1, [r8, #4]
	ldr	r3, [r7, #1076]
	strb	r3, [r6, #-1864]
	bl	__aeabi_uidiv
	add	r0, r0, #1
	cmp	r0, #1
	strhi	r0, [r6, #-1780]
	movls	r3, #2
	strls	r3, [r6, #-1780]
	ldrh	r3, [r7, #14]
	mov	r7, #0
	strb	r3, [r5, #-1776]
.L843:
	ldr	r3, [sp, #4]
	subs	r9, r9, #1
	add	r4, r4, r3
	bne	.L840
	mov	r0, r9
.L854:
	bl	flash_exit_slc_mode
	mov	r0, r7
	add	sp, sp, #20
	@ sp needed
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L841:
	ldr	fp, [r5, #-1788]
	ldr	r2, .L855+24
	ldr	r3, [fp]
	cmp	r3, r2
	bne	.L843
	cmp	r7, #0
	bne	.L844
	mov	r0, r4
	ldr	r1, [r8, #4]
	bl	__aeabi_uidiv
	ldr	r3, .L855+8
	add	r0, r0, #1
	str	r0, [r3, #-1780]
	mov	r0, r7
	b	.L854
.L856:
	.align	2
.L855:
	.word	.LANCHOR3
	.word	.LANCHOR0
	.word	.LANCHOR2
	.word	.LANCHOR1+256
	.word	.LANCHOR0+44
	.word	.LANCHOR2-2732
	.word	1312902724
	.fnend
	.size	FlashLoadPhyInfo, .-FlashLoadPhyInfo
	.align	2
	.global	ToshibaReadRetrial
	.type	ToshibaReadRetrial, %function
ToshibaReadRetrial:
	.fnstart
	@ args = 0, pretend = 0, frame = 24
	@ frame_needed = 0, uses_anonymous_args = 0
	stmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.save {r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.pad #28
	sub	sp, sp, #28
	mov	r8, r0
	mov	r10, r3
	str	r1, [sp, #20]
	str	r2, [sp, #8]
	bl	NandcWaitFlashReady
	ldr	r3, .L886
	add	r3, r3, r8, asl #3
	ldr	r5, [r3, #12]
	ldrb	r3, [r3, #16]	@ zero_extendqisi2
	add	r6, r3, #8
	add	r6, r5, r6, asl #8
	str	r3, [sp, #4]
	ldr	r3, .L886+4
	ldrb	r7, [r3]	@ zero_extendqisi2
	str	r3, [sp, #12]
	sub	r4, r7, #67
	ldr	r3, [sp, #4]
	cmp	r4, #1
	mov	r3, r3, asl #8
	movls	r4, #0
	str	r3, [sp, #16]
	bls	.L858
	ldr	r3, .L886+8
	ldrb	r4, [r3, #-1864]	@ zero_extendqisi2
	cmp	r4, #0
	beq	.L859
	mov	r0, #0
	mov	r4, #1
	bl	NandcSetDdrMode
.L859:
	ldr	r3, [sp, #16]
	mov	r2, #92
	add	r3, r5, r3
	str	r2, [r3, #2056]
	mov	r2, #197
	str	r2, [r3, #2056]
.L858:
	mov	r7, #1
	mvn	r9, #0
.L860:
	ldr	r3, .L886+12
	ldrb	r3, [r3]	@ zero_extendqisi2
	add	r3, r3, #1
	cmp	r7, r3
	bcs	.L885
	ldr	r3, [sp, #12]
	mov	r0, r6
	uxtb	r1, r7
	ldrb	r3, [r3]	@ zero_extendqisi2
	sub	r3, r3, #67
	cmp	r3, #1
	bhi	.L861
	bl	SandiskSetRRPara
	b	.L862
.L861:
	bl	ToshibaSetRRPara
.L862:
	ldr	r3, [sp, #12]
	ldrb	r3, [r3]	@ zero_extendqisi2
	cmp	r3, #34
	bne	.L863
	ldr	r3, .L886+12
	ldrb	r3, [r3]	@ zero_extendqisi2
	sub	r3, r3, #3
	cmp	r7, r3
	ldreq	r3, [sp, #4]
	moveq	r2, #179
	addeq	r3, r5, r3, asl #8
	streq	r2, [r3, #2056]
.L863:
	ldr	r3, [sp, #16]
	cmp	r4, #0
	mov	r2, #38
	add	r3, r5, r3
	str	r2, [r3, #2056]
	mov	r2, #93
	str	r2, [r3, #2056]
	beq	.L864
	mov	r0, #4
	bl	NandcSetDdrMode
	mov	r0, r8
	mov	r3, r10
	ldr	r1, [sp, #20]
	ldr	r2, [sp, #8]
	bl	FlashReadRawPage
	mov	fp, r0
	mov	r0, #0
	bl	NandcSetDdrMode
	b	.L865
.L864:
	mov	r0, r8
	ldr	r1, [sp, #20]
	ldr	r2, [sp, #8]
	mov	r3, r10
	bl	FlashReadRawPage
	mov	fp, r0
.L865:
	cmn	fp, #1
	beq	.L866
	ldr	r3, .L886+8
	cmn	r9, #1
	moveq	r9, fp
	ldrb	r2, [r3, #-2743]	@ zero_extendqisi2
	add	r2, r2, r2, asl #1
	cmp	fp, r2, asr #2
	bcc	.L868
	mov	r10, #0
	str	r10, [sp, #8]
.L866:
	add	r7, r7, #1
	b	.L860
.L885:
	mov	fp, r9
.L868:
	ldr	r3, [sp, #12]
	mov	r0, r6
	mov	r1, #0
	ldrb	r2, [r3]	@ zero_extendqisi2
	sub	r2, r2, #67
	cmp	r2, #1
	bhi	.L870
	bl	SandiskSetRRPara
	b	.L871
.L870:
	bl	ToshibaSetRRPara
.L871:
	ldr	r3, [sp, #16]
	mov	r2, #255
	add	r5, r5, r3
	str	r2, [r5, #2056]
	ldr	r2, .L886+8
	ldrb	r2, [r2, #-2743]	@ zero_extendqisi2
	add	r2, r2, r2, asl #1
	cmp	fp, r2, asr #2
	bcc	.L872
	cmn	fp, #1
	movne	fp, #256
.L872:
	mov	r0, r8
	bl	NandcWaitFlashReady
	cmp	r4, #0
	beq	.L873
	mov	r0, #4
	bl	NandcSetDdrMode
.L873:
	mov	r0, fp
	add	sp, sp, #28
	@ sp needed
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L887:
	.align	2
.L886:
	.word	.LANCHOR0
	.word	g_retryMode
	.word	.LANCHOR2
	.word	g_maxRetryCount
	.fnend
	.size	ToshibaReadRetrial, .-ToshibaReadRetrial
	.align	2
	.global	SamsungReadRetrial
	.type	SamsungReadRetrial, %function
SamsungReadRetrial:
	.fnstart
	@ args = 0, pretend = 0, frame = 8
	@ frame_needed = 0, uses_anonymous_args = 0
	stmfd	sp!, {r0, r1, r2, r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.save {r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.pad #12
	mov	r8, r0
	ldr	r4, .L902
	mov	r7, r3
	mov	fp, r1
	mov	r9, r2
	add	r4, r4, r8, asl #3
	bl	NandcWaitFlashReady
	ldr	r10, .L902+4
	mov	r6, #1
	ldrb	r3, [r4, #16]	@ zero_extendqisi2
	ldr	r5, [r4, #12]
	mvn	r4, #0
	add	r3, r3, #8
	ldr	ip, .L902+8
	add	r5, r5, r3, asl #8
.L889:
	ldrb	r3, [r10]	@ zero_extendqisi2
	add	r3, r3, #1
	cmp	r6, r3
	bcs	.L892
	mov	r0, r5
	uxtb	r1, r6
	str	ip, [sp, #4]
	bl	SamsungSetRRPara
	mov	r0, r8
	mov	r1, fp
	mov	r2, r9
	mov	r3, r7
	bl	FlashReadRawPage
	cmn	r0, #1
	ldr	ip, [sp, #4]
	beq	.L890
	ldrb	r3, [ip, #-2743]	@ zero_extendqisi2
	cmn	r4, #1
	moveq	r4, r0
	add	r3, r3, r3, asl #1
	cmp	r0, r3, asr #2
	bcc	.L895
	mov	r7, #0
	mov	r9, r7
.L890:
	add	r6, r6, #1
	b	.L889
.L895:
	mov	r4, r0
.L892:
	mov	r0, r5
	mov	r1, #0
	bl	SamsungSetRRPara
	ldr	r3, .L902+8
	ldrb	r3, [r3, #-2743]	@ zero_extendqisi2
	add	r3, r3, r3, asl #1
	cmp	r4, r3, asr #2
	bcc	.L894
	cmn	r4, #1
	movne	r4, #256
.L894:
	mov	r0, r4
	add	sp, sp, #12
	@ sp needed
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L903:
	.align	2
.L902:
	.word	.LANCHOR0
	.word	g_maxRetryCount
	.word	.LANCHOR2
	.fnend
	.size	SamsungReadRetrial, .-SamsungReadRetrial
	.align	2
	.global	MicronReadRetrial
	.type	MicronReadRetrial, %function
MicronReadRetrial:
	.fnstart
	@ args = 0, pretend = 0, frame = 24
	@ frame_needed = 0, uses_anonymous_args = 0
.L906:
	stmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.save {r4, r5, r6, r7, r8, r9, r10, fp, lr}
	mov	r8, r3
	ldr	r3, .L930
	mov	fp, r2
	.pad #36
	sub	sp, sp, #36
	mov	r6, r0
	mov	r10, #0
	ldrb	r5, [r3, #-2743]	@ zero_extendqisi2
	ldrb	r3, [r3, #-2744]	@ zero_extendqisi2
	str	r1, [sp, #20]
	cmp	r3, #0
	addeq	r5, r5, r5, asl #1
	ldrne	r2, .L930+4
	ubfxeq	r5, r5, #2, #8
	smullne	r2, r3, r5, r2
	uxtbne	r5, r3
	ldr	r3, .L930+8
	add	r3, r3, r0, asl #3
	str	r3, [sp, #24]
.L916:
	mov	r0, r6
	mov	r9, #0
	bl	NandcWaitFlashReady
	mvn	r4, #0
	ldr	r3, [sp, #24]
	ldr	r3, [r3, #12]
	str	r3, [sp, #12]
	ldr	r3, [sp, #24]
	ldrb	r3, [r3, #16]	@ zero_extendqisi2
	str	r3, [sp, #16]
	ldr	r2, [sp, #16]
	ldr	r3, [sp, #12]
	add	r7, r3, r2, asl #8
.L907:
	ldr	r3, .L930+12
	ldrb	r3, [r3]	@ zero_extendqisi2
	cmp	r9, r3
	bcs	.L910
	mov	r3, #239
	mov	r0, #200
	str	r3, [r7, #2056]
	mov	r3, #137
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
	cmn	r0, #1
	beq	.L908
	cmn	r4, #1
	ldr	ip, [sp, #28]
	moveq	r4, r0
	cmp	r0, r5
	bcc	.L918
	mov	r8, ip
	mov	fp, ip
.L908:
	ldr	r9, [sp, #8]
	b	.L907
.L918:
	mov	r4, r0
	mov	r8, ip
	mov	fp, ip
.L910:
	ldr	r2, [sp, #16]
	mov	r0, #200
	ldr	r3, [sp, #12]
	add	r7, r3, r2, asl #8
	mov	r3, #239
	str	r3, [r7, #2056]
	mov	r3, #137
	str	r3, [r7, #2052]
	bl	NandcDelayns
	cmp	r4, r5
	mov	r3, #0
	str	r3, [r7, #2048]
	str	r3, [r7, #2048]
	str	r3, [r7, #2048]
	str	r3, [r7, #2048]
	bcc	.L912
	cmn	r4, #1
	movne	r4, #256
.L912:
	cmn	r4, #1
	movne	r7, #0
	moveq	r7, #1
	cmp	r4, #256
	movne	r1, r7
	orreq	r1, r7, #1
	cmp	r1, #0
	beq	.L913
	str	r4, [sp]
	mov	r1, r9
	ldr	r0, .L930+16
	mov	r3, r9
	ldr	r2, [sp, #20]
	bl	printk
	cmp	r10, #0
	bne	.L914
	ldr	r3, .L930
	ldrb	r3, [r3, #-2744]	@ zero_extendqisi2
	cmp	r3, #0
	moveq	r7, #0
	andne	r7, r7, #1
	cmp	r7, #0
	beq	.L923
	mov	r0, r6
	mov	r1, #3
	bl	micron_auto_read_calibration_config
	mov	r10, #1
	b	.L916
.L914:
	mov	r0, r6
	mov	r1, #0
	bl	micron_auto_read_calibration_config
	cmn	r4, #1
	movne	r4, #256
	b	.L923
.L913:
	cmp	r10, #0
	beq	.L923
	mov	r0, r6
	mov	r4, #256
	bl	micron_auto_read_calibration_config
.L923:
	mov	r0, r4
	add	sp, sp, #36
	@ sp needed
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L931:
	.align	2
.L930:
	.word	.LANCHOR2
	.word	1431655766
	.word	.LANCHOR0
	.word	g_maxRetryCount
	.word	.LC11
	.fnend
	.size	MicronReadRetrial, .-MicronReadRetrial
	.align	2
	.global	HynixReadRetrial
	.type	HynixReadRetrial, %function
HynixReadRetrial:
	.fnstart
	@ args = 0, pretend = 0, frame = 8
	@ frame_needed = 0, uses_anonymous_args = 0
	stmfd	sp!, {r0, r1, r2, r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.save {r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.pad #12
	mov	r10, r2
	ldr	r2, .L950
	mov	fp, r1
	ldr	ip, .L950+4
	mov	r8, r3
	mov	r5, r0
	ldr	r1, [r2, #3320]
	add	r3, ip, r0
	ldrb	r9, [ip, #-2730]	@ zero_extendqisi2
	mov	r7, ip
	ldrb	r4, [r3, #-2720]	@ zero_extendqisi2
	ldrb	r6, [r1, #19]	@ zero_extendqisi2
	str	r2, [sp]
	sub	r6, r6, #7
	cmp	r6, #1
	mvn	r6, #0
	ldrlsb	r4, [r3, #-2712]	@ zero_extendqisi2
	bl	NandcWaitFlashReady
	mov	ip, #0
.L934:
	cmp	ip, r9
	bcs	.L938
	add	r4, r4, #1
	mov	r0, r5
	ldrb	r1, [r7, #-2731]	@ zero_extendqisi2
	uxtb	r4, r4
	ldr	r2, .L950+8
	cmp	r4, r9
	str	ip, [sp, #4]
	movcs	r4, #0
	mov	r3, r4
	bl	HynixSetRRPara
	mov	r0, r5
	mov	r1, fp
	mov	r2, r10
	mov	r3, r8
	bl	FlashReadRawPage
	cmn	r0, #1
	ldr	ip, [sp, #4]
	beq	.L936
	ldrb	r3, [r7, #-2743]	@ zero_extendqisi2
	cmn	r6, #1
	moveq	r6, r0
	add	r3, r3, r3, asl #1
	cmp	r0, r3, asr #2
	bcc	.L943
	mov	r8, #0
	mov	r10, r8
.L936:
	add	ip, ip, #1
	b	.L934
.L943:
	mov	r6, r0
.L938:
	ldr	r3, [sp]
	add	r5, r7, r5
	ldr	r3, [r3, #3320]
	ldrb	r3, [r3, #19]	@ zero_extendqisi2
	sub	r3, r3, #7
	cmp	r3, #1
	ldrb	r3, [r7, #-2743]	@ zero_extendqisi2
	strlsb	r4, [r5, #-2712]
	strhib	r4, [r5, #-2720]
	add	r3, r3, r3, asl #1
	cmp	r6, r3, asr #2
	bcc	.L942
	cmn	r6, #1
	movne	r6, #256
.L942:
	mov	r0, r6
	add	sp, sp, #12
	@ sp needed
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L951:
	.align	2
.L950:
	.word	.LANCHOR0
	.word	.LANCHOR2
	.word	.LANCHOR2-2728
	.fnend
	.size	HynixReadRetrial, .-HynixReadRetrial
	.align	2
	.global	FlashProgPage
	.type	FlashProgPage, %function
FlashProgPage:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	stmfd	sp!, {r0, r1, r4, r5, r6, r7, r8, lr}
	.save {r4, r5, r6, r7, r8, lr}
	.pad #8
	mov	r8, r3
	ldr	r3, .L956
	subs	r4, r0, #0
	mov	r5, r1
	mov	r7, r2
	ldrb	r6, [r3, #265]	@ zero_extendqisi2
	bne	.L953
	ldr	r2, .L956+4
	ldrb	r3, [r2, #1]	@ zero_extendqisi2
	ldr	r1, [r2, #4]
	mul	r1, r1, r3
	cmp	r5, r1
	bcs	.L953
	ldrb	r3, [r2]	@ zero_extendqisi2
	cmp	r3, #0
	movne	r6, #4
.L953:
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
	mov	r1, #1
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
	ldmfd	sp!, {r4, r5, r6, r7, r8, pc}
.L957:
	.align	2
.L956:
	.word	.LANCHOR1
	.word	.LANCHOR0
	.fnend
	.size	FlashProgPage, .-FlashProgPage
	.align	2
	.global	FlashSavePhyInfo
	.type	FlashSavePhyInfo, %function
FlashSavePhyInfo:
	.fnstart
	@ args = 0, pretend = 0, frame = 8
	@ frame_needed = 0, uses_anonymous_args = 0
	stmfd	sp!, {r0, r1, r2, r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.save {r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.pad #12
	ldr	r4, .L968
	ldr	r5, .L968+4
	ldr	r8, .L968+8
	ldr	r3, [r4, #-1872]
	mov	r9, r4
	ldrb	r0, [r4, #-1775]	@ zero_extendqisi2
	mov	r10, r5
	str	r3, [r4, #-1788]
	bl	FlashBchSel
	mov	r1, #0
	mov	r2, #2048
	ldr	r0, [r4, #-1872]
	bl	ftl_memset
	ldr	r3, [r4, #-1788]
	ldr	r1, .L968+12
	mov	r2, #32
	str	r8, [r3]
	ldr	r0, [r4, #-1788]
	ldrb	r3, [r5, #3148]	@ zero_extendqisi2
	add	r0, r0, #16
	strh	r3, [r0, #-4]	@ movhi
	ldrb	r3, [r5, #1]	@ zero_extendqisi2
	strh	r3, [r0, #-2]	@ movhi
	ldrb	r3, [r4, #-1864]	@ zero_extendqisi2
	str	r3, [r0, #1060]
	bl	ftl_memcpy
	ldr	r0, [r4, #-1788]
	add	r1, r5, #3152
	mov	r2, #8
	add	r0, r0, #80
	bl	ftl_memcpy
	ldr	r0, [r4, #-1788]
	ldr	r1, .L968+16
	mov	r2, #32
	add	r0, r0, #96
	bl	ftl_memcpy
	ldr	r0, [r4, #-1788]
	ldr	r1, .L968+20
	mov	r2, #32
	add	r0, r0, #160
	bl	ftl_memcpy
	ldr	r0, [r4, #-1788]
	add	r1, r5, #44
	mov	r2, #32
	add	r0, r0, #192
	bl	ftl_memcpy
	ldr	r0, [r4, #-1788]
	mov	r2, #852
	ldr	r1, .L968+24
	add	r0, r0, #224
	bl	ftl_memcpy
	ldr	r6, [r4, #-1788]
	movw	r1, #2036
	add	r0, r6, #12
	bl	js_hash
	movw	r3, #1592
	str	r3, [r6, #4]
	ldr	r3, [r4, #-1772]
	str	r3, [r4, #-1788]
	str	r0, [r6, #8]
	mov	r0, #0
	bl	flash_enter_slc_mode
	mov	r6, #0
	mov	r7, r6
.L961:
	ldr	r1, [r5, #4]
	mov	r0, #0
	mov	r2, r0
	mul	r1, r1, r7
	bl	FlashEraseBlock
	ldr	r1, [r5, #4]
	mov	r0, #0
	ldr	r2, [r4, #-1872]
	mov	r3, r0
	mul	r1, r1, r7
	bl	FlashProgPage
	ldr	r1, [r5, #4]
	mov	r0, #0
	ldr	r2, [r4, #-1872]
	mov	r3, r0
	mul	r1, r1, r7
	add	r1, r1, #1
	bl	FlashProgPage
	ldr	r1, [r5, #4]
	mov	r0, #0
	ldr	r2, [r4, #-1772]
	mov	r3, r0
	mul	r1, r1, r7
	bl	FlashReadRawPage
	add	r2, r7, #1
	cmn	r0, #1
	beq	.L959
	ldr	fp, [r9, #-1788]
	ldr	r3, [fp]
	cmp	r3, r8
	bne	.L959
	add	r0, fp, #12
	movw	r1, #2036
	str	r2, [sp, #4]
	bl	js_hash
	ldr	r3, [fp, #8]
	cmp	r3, r0
	ldr	r2, [sp, #4]
	bne	.L959
	ldr	r3, [r10, #4]
	cmp	r6, #1
	str	r2, [r9, #-1780]
	mul	r7, r3, r7
	str	r7, [r9, #-1784]
	beq	.L962
	mov	r6, #1
.L959:
	cmp	r2, #4
	mov	r7, r2
	bne	.L961
	b	.L960
.L962:
	mov	r6, #2
.L960:
	mov	r0, #0
	bl	flash_exit_slc_mode
	clz	r0, r6
	mov	r0, r0, lsr #5
	rsb	r0, r0, #0
	add	sp, sp, #12
	@ sp needed
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L969:
	.align	2
.L968:
	.word	.LANCHOR2
	.word	.LANCHOR0
	.word	1312902724
	.word	IDByte
	.word	.LANCHOR0+3160
	.word	.LANCHOR1+256
	.word	.LANCHOR2-2732
	.fnend
	.size	FlashSavePhyInfo, .-FlashSavePhyInfo
	.align	2
	.global	FlashReadIdbDataRaw
	.type	FlashReadIdbDataRaw, %function
FlashReadIdbDataRaw:
	.fnstart
	@ args = 0, pretend = 0, frame = 16
	@ frame_needed = 0, uses_anonymous_args = 0
	stmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.save {r4, r5, r6, r7, r8, r9, r10, fp, lr}
	mov	r9, r0
	ldr	r3, .L990
	.pad #20
	sub	sp, sp, #20
	ldr	r5, .L990+4
	ldr	r0, [r3]	@ unaligned
	ldrb	r3, [r5, #-2743]	@ zero_extendqisi2
	str	r0, [sp, #12]	@ unaligned
	str	r3, [sp]
	ldr	r3, [r5, #-1868]
	cmp	r3, #0
	beq	.L971
	mov	r0, #0
	bl	flash_enter_slc_mode
.L971:
	mov	r0, r9
	mov	r1, #0
	mov	r2, #2048
	ldr	r10, .L990+8
	bl	ftl_memset
	mvn	r8, #0
	mov	r4, #2
	mov	fp, r10
.L972:
	ldrb	r3, [r10, #1]	@ zero_extendqisi2
	cmp	r4, r3
	bcs	.L976
	mov	r7, #0
.L974:
	add	r3, sp, #12
	ldr	r6, .L990+4
	ldrb	ip, [r7, r3]	@ zero_extendqisi2
	mov	r0, ip
	str	ip, [sp, #4]
	bl	FlashBchSel
	ldr	r1, [fp, #4]
	mov	r0, #0
	ldr	r2, [r5, #-1872]
	mov	r3, r0
	mul	r1, r1, r4
	bl	FlashReadRawPage
	cmn	r0, #1
	ldr	ip, [sp, #4]
	bne	.L973
	add	r7, r7, #1
	cmp	r7, #4
	bne	.L974
	b	.L975
.L973:
	ldr	r3, [r6, #-1872]
	ldr	r2, .L990+12
	ldr	r3, [r3]
	cmp	r3, r2
	bne	.L975
	mov	r1, ip
	ldr	r0, .L990+16
	bl	printk
	mov	r0, r9
	ldr	r1, [r6, #-1872]
	mov	r2, #2048
	bl	ftl_memcpy
	ldr	r3, [r6, #-1872]
	ldr	r3, [r3, #512]
	strb	r3, [fp, #1]
	ldr	r3, [r6, #-1780]
	cmp	r3, r4
	bls	.L979
	str	r4, [r6, #-1780]
	bl	FlashSavePhyInfo
	mov	r8, #0
.L975:
	add	r4, r4, #1
	b	.L972
.L979:
	mov	r8, #0
.L976:
	ldr	r0, [sp]
	bl	FlashBchSel
	ldr	r3, [r5, #-1868]
	cmp	r3, #0
	beq	.L983
	mov	r0, #0
	bl	flash_exit_slc_mode
.L983:
	mov	r0, r8
	add	sp, sp, #20
	@ sp needed
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L991:
	.align	2
.L990:
	.word	.LANCHOR3
	.word	.LANCHOR2
	.word	.LANCHOR0
	.word	-52655045
	.word	.LC12
	.fnend
	.size	FlashReadIdbDataRaw, .-FlashReadIdbDataRaw
	.align	2
	.global	FlashInit
	.type	FlashInit, %function
FlashInit:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	stmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.save {r4, r5, r6, r7, r8, r9, r10, fp, lr}
	mov	r6, r0
	.pad #20
	sub	sp, sp, #20
	mov	r0, #32768
	bl	ftl_malloc
	ldr	r4, .L1087
	ldr	r5, .L1087+4
	mov	r7, #0
	str	r0, [r4, #-1872]
	mov	r0, #32768
	bl	ftl_malloc
	str	r0, [r4, #-1772]
	mov	r0, #4096
	bl	ftl_malloc
	str	r0, [r4, #-1768]
	mov	r0, #32768
	bl	ftl_malloc
	str	r0, [r4, #-1764]
	mov	r0, #4096
	bl	ftl_malloc
	mov	r3, #50
	str	r7, [r4, #-1780]
	strb	r3, [r5, #1]
	strb	r3, [r4, #-1776]
	mov	r3, #128
	strb	r7, [r4, #-1864]
	str	r3, [r5, #4]
	mov	r3, #60
	str	r7, [r4, #-1796]
	strb	r7, [r5]
	strb	r7, [r4, #-1756]
	strb	r3, [r4, #-1775]
	str	r0, [r4, #-1760]
	mov	r0, r6
	bl	NandcInit
	ldr	r6, .L1087+8
	mov	r8, r6
.L998:
	ldr	r3, .L1087+12
	uxtb	r9, r7
	add	r2, r3, r7, asl #3
	mov	r0, r9
	ldr	r10, [r3, r7, asl #3]
	ldrb	fp, [r2, #4]	@ zero_extendqisi2
	bl	FlashReset
	mov	r0, r9
	bl	NandcFlashCs
	mov	r2, #144
	add	fp, r10, fp, asl #8
	mov	r0, #200
	mov	r10, #0
	str	r2, [fp, #2056]
	str	r10, [fp, #2052]
	bl	NandcDelayns
	ldr	r1, [fp, #2048]
	uxtb	r1, r1
	strb	r1, [r6]
	ldr	r0, [fp, #2048]
	cmp	r1, #44
	strb	r0, [r6, #1]
	ldr	r0, [fp, #2048]
	strb	r0, [r6, #2]
	ldr	r0, [fp, #2048]
	strb	r0, [r6, #3]
	ldr	r0, [fp, #2048]
	strb	r0, [r6, #4]
	ldr	r0, [fp, #2048]
	strb	r0, [r6, #5]
	bne	.L993
	mov	r1, #239
	mov	r0, #200
	str	r1, [fp, #2056]
	mov	r1, #1
	str	r1, [fp, #2052]
	bl	NandcDelayns
	mov	r1, #4
	str	r1, [fp, #2048]
	str	r10, [fp, #2048]
	str	r10, [fp, #2048]
	str	r10, [fp, #2048]
.L993:
	mov	r0, r9
	bl	NandcFlashDeCs
	ldrb	r2, [r6]	@ zero_extendqisi2
	sub	r3, r2, #1
	uxtb	r3, r3
	cmp	r3, #253
	bhi	.L994
	ldrb	r1, [r6, #2]	@ zero_extendqisi2
	ldrb	r3, [r6, #1]	@ zero_extendqisi2
	ldr	r0, .L1087+16
	str	r1, [sp]
	ldrb	r1, [r6, #3]	@ zero_extendqisi2
	str	r1, [sp, #4]
	ldrb	r1, [r6, #4]	@ zero_extendqisi2
	str	r1, [sp, #8]
	ldrb	r1, [r6, #5]	@ zero_extendqisi2
	str	r1, [sp, #12]
	add	r1, r7, #1
	bl	printk
.L994:
	cmp	r7, #0
	bne	.L995
	ldrb	r3, [r8]	@ zero_extendqisi2
	sub	r3, r3, #1
	uxtb	r3, r3
	cmp	r3, #253
	bhi	.L1044
	ldr	r3, .L1087+8
	ldrb	r3, [r3, #1]	@ zero_extendqisi2
	cmp	r3, #255
	beq	.L1044
	bl	FlashCs123Init
.L995:
	ldrb	r3, [r6]	@ zero_extendqisi2
	add	r7, r7, #1
	add	r6, r6, #8
	cmp	r3, #181
	moveq	r3, #44
	streqb	r3, [r6, #-8]
	cmp	r7, #4
	bne	.L998
	ldrb	r3, [r8]	@ zero_extendqisi2
	cmp	r3, #173
	beq	.L999
	ldr	r0, [r4, #-1860]
	bl	NandcSetDdrMode
.L999:
	mov	r2, #852
	ldr	r0, .L1087+20
	mov	r1, #0
	ldr	r7, .L1087+24
	bl	ftl_memset
	ldr	r6, [r4, #-1852]
	ldr	r3, .L1087+28
	cmp	r6, r7
	ldrb	r0, [r8, #1]	@ zero_extendqisi2
	str	r3, [r5, #3320]
	mov	r3, #0
	strb	r3, [r5, #8]
	sub	ip, r0, #218
	ldreq	r3, .L1087
	clz	ip, ip
	moveq	r2, #1
	mov	ip, ip, lsr #5
	streq	r2, [r3, #-1868]
	cmp	r0, #161
	cmpne	r0, #241
	and	r2, r0, #253
	moveq	r3, #1
	movne	r3, #0
	orr	r3, ip, r3
	cmp	r2, #209
	orreq	r3, r3, #1
	cmp	r3, #0
	bne	.L1001
	cmp	r0, #220
	bne	.L1002
	ldr	r3, .L1087+8
	ldrb	r3, [r3, #3]	@ zero_extendqisi2
	cmp	r3, #149
	bne	.L1002
.L1001:
	ldrb	lr, [r8]	@ zero_extendqisi2
	mov	r3, #16
	strb	r3, [r5, #1]
	mov	r1, #1
	strb	r3, [r4, #-1775]
	cmp	lr, #152
	ldr	r3, .L1087+32
	strb	r1, [r5]
	ldr	r2, .L1087
	strb	lr, [r3, #3317]
	strb	r0, [r3, #3318]
	bne	.L1004
	ldr	lr, .L1087+8
	ldrsb	lr, [lr, #4]
	cmp	lr, #0
	strltb	r1, [r2, #-1756]
	movge	r1, #24
	strgeb	r1, [r2, #-1775]
.L1004:
	movw	r2, #2049
	cmp	r6, r7
	cmpne	r6, r2
	moveq	r2, #16
	streqb	r2, [r4, #-1775]
	cmp	ip, #0
	ldrne	r2, .L1087+36
	movne	r1, #2048
	strneh	r1, [r2, #14]	@ movhi
	mvnne	r2, #37
	bne	.L1081
.L1008:
	cmp	r0, #220
	bne	.L1010
	ldr	r2, .L1087+36
	mov	r1, #4096
	strh	r1, [r2, #14]	@ movhi
	mvn	r2, #35
.L1081:
	strb	r2, [r3, #3318]
	b	.L1009
.L1010:
	cmp	r0, #211
	ldreq	r2, .L1087+36
	moveq	r1, #4096
	streqh	r1, [r2, #14]	@ movhi
	moveq	r2, #2
	streqb	r2, [r3, #3329]
.L1009:
	ldr	r1, .L1087+40
	mov	r2, #32
	ldr	r0, .L1087+44
	bl	ftl_memcpy
	ldr	r0, .L1087+28
	ldr	r1, .L1087+36
	mov	r2, #32
	bl	ftl_memcpy
.L1002:
	ldrb	r3, [r5]	@ zero_extendqisi2
	cmp	r3, #0
	bne	.L1011
	bl	FlashLoadPhyInfoInRam
	cmp	r0, #0
	bne	.L1013
	ldr	r3, .L1087+4
	ldr	r6, .L1087
	ldr	r3, [r3, #3320]
	ldrh	r3, [r3, #16]
	mov	r3, r3, lsr #8
	tst	r3, #1
	and	r0, r3, #7
	strb	r0, [r4, #-1876]
	bne	.L1013
	mov	r3, #1
	strb	r3, [r6, #-1864]
	bl	FlashSetInterfaceMode
	ldrb	r0, [r6, #-1876]	@ zero_extendqisi2
	bl	NandcSetMode
.L1013:
	ldr	r3, [r5, #3320]
	ldr	r6, .L1087
	ldrb	r3, [r3, #26]	@ zero_extendqisi2
	strb	r3, [r4, #-2744]
	bl	FlashLoadPhyInfo
	cmp	r0, #0
	beq	.L1011
	ldr	r3, [r6, #-1860]
	cmp	r3, #0
	beq	.L1016
	mov	r0, #1
	bl	FlashSetInterfaceMode
	mov	r0, #1
	b	.L1082
.L1016:
	ldrb	r0, [r6, #-1876]	@ zero_extendqisi2
	bl	FlashSetInterfaceMode
	ldrb	r0, [r6, #-1876]	@ zero_extendqisi2
.L1082:
	bl	NandcSetMode
	bl	FlashLoadPhyInfo
	cmp	r0, #0
	beq	.L1011
	mov	r0, #1
	ldr	r6, .L1087+4
	bl	FlashSetInterfaceMode
	mov	r0, #1
	bl	NandcSetMode
	ldr	r3, [r5, #3320]
	ldr	r0, .L1087+48
	ldrh	r1, [r3, #14]
	bl	printk
	bl	FlashLoadPhyInfoInRam
	cmn	r0, #1
	beq	.L1057
	bl	FlashDieInfoInit
	ldr	r3, [r6, #3320]
	ldrb	r0, [r3, #19]	@ zero_extendqisi2
	bl	FlashGetReadRetryDefault
	movw	r3, #3324
	ldr	r2, [r6, #3320]
	ldrh	r3, [r6, r3]
	add	r3, r3, #4080
	ldrb	r1, [r2, #9]	@ zero_extendqisi2
	add	r3, r3, #15
	cmp	r1, r3, asr #12
	ldrh	r3, [r2, #14]
	blt	.L1018
	add	r0, r3, #255
	cmp	r1, r0, asr #8
	bge	.L1019
.L1018:
	bic	r3, r3, #255
	strh	r3, [r2, #14]	@ movhi
.L1019:
	ldrb	r3, [r4, #-1876]	@ zero_extendqisi2
	tst	r3, #6
	beq	.L1020
	bl	FlashSavePhyInfo
	ldr	r3, .L1087
	mov	r0, #0
	ldr	r1, [r3, #-1784]
	bl	FlashDdrParaScan
.L1020:
	bl	FlashSavePhyInfo
.L1011:
	ldr	r2, [r5, #3320]
	ldr	r6, .L1087
	ldrb	r3, [r2, #26]	@ zero_extendqisi2
	ldrh	r0, [r2, #10]
	ldrb	r9, [r2, #18]	@ zero_extendqisi2
	strb	r3, [r4, #-2744]
	ldrh	r3, [r2, #16]
	ubfx	r1, r3, #7, #1
	strb	r1, [r5, #8]
	ubfx	r1, r3, #3, #1
	strb	r1, [r4, #-1755]
	ubfx	r1, r3, #4, #1
	ubfx	r3, r3, #8, #3
	strb	r1, [r4, #-1804]
	strb	r3, [r4, #-1876]
	mov	r3, #0
	ldrb	r1, [r2, #12]	@ zero_extendqisi2
	str	r3, [r4, #-1792]
	bl	__aeabi_idiv
	mov	r1, r0
	mov	r0, r9
	bl	BuildFlashLsbPageTable
	bl	FlashDieInfoInit
	ldr	r3, [r5, #3320]
	ldrh	r2, [r3, #16]
	tst	r2, #64
	beq	.L1022
	ldrb	r0, [r3, #19]	@ zero_extendqisi2
	ldr	r3, .L1087+52
	ldrb	r2, [r6, #-2731]	@ zero_extendqisi2
	ldr	r1, .L1087+56
	strb	r0, [r3]
	ldr	r3, .L1087+60
	strb	r2, [r3]
	mov	ip, r3
	ldrb	r2, [r6, #-2730]	@ zero_extendqisi2
	strb	r2, [r1]
	sub	r2, r0, #1
	cmp	r2, #7
	bhi	.L1023
	ldr	r3, .L1087+64
	sub	r2, r0, #8
	clz	r2, r2
	str	r3, [r6, #-1792]
	sub	r3, r0, #5
	cmp	r0, #8
	cmpne	r3, #1
	mov	r2, r2, lsr #5
	movls	r3, #1
	strls	r3, [r6, #-1812]
	cmp	r0, #7
	ldreq	r6, .L1087+68
	beq	.L1026
	ldr	r3, .L1087+68
	cmp	r2, #0
	sub	r6, r3, #8
	movne	r6, r3
.L1026:
	sub	r2, r6, #1
	add	r6, r6, #31
	mov	r3, #0
.L1027:
	ldrsb	r1, [r2, #1]!
	cmp	r1, #0
	addeq	r3, r3, #1
	cmp	r2, r6
	bne	.L1027
	cmp	r3, #27
	bls	.L1022
	bl	FlashGetReadRetryDefault
	bl	FlashSavePhyInfo
	b	.L1022
.L1023:
	sub	r2, r0, #17
	cmp	r2, #2
	bhi	.L1029
	ldr	r3, .L1087+72
	cmp	r0, #19
	str	r3, [r6, #-1792]
	moveq	r3, #15
	bne	.L1086
	b	.L1084
.L1029:
	sub	r2, r0, #65
	cmp	r0, #33
	cmpne	r2, #1
	bhi	.L1031
	ldr	r2, .L1087+76
	str	r2, [r6, #-1792]
	mov	r2, #4
	strb	r2, [r3]
.L1086:
	mov	r3, #7
.L1084:
	strb	r3, [r1]
	b	.L1022
.L1031:
	sub	r3, r0, #67
	sub	r2, r0, #34
	cmp	r3, #1
	movhi	lr, #0
	movls	lr, #1
	cmp	r3, #1
	cmphi	r2, #1
	bhi	.L1032
	ldr	r3, .L1087+76
	cmp	r0, #68
	cmpne	r0, #35
	str	r3, [r6, #-1792]
	movne	r3, #7
	moveq	r3, #17
	cmp	lr, #0
	strb	r3, [r1]
	movne	r3, #4
	moveq	r3, #5
	strb	r3, [ip]
	b	.L1022
.L1032:
	cmp	r0, #49
	ldreq	r3, .L1087+80
	streq	r3, [r6, #-1792]
.L1022:
	ldr	r3, [r4, #-1852]
	cmp	r3, r7
	bne	.L1036
	ldr	r2, .L1087
	ldrb	r2, [r2, #-2744]	@ zero_extendqisi2
	cmp	r2, #0
	ldrne	r2, [r5, #3320]
	movne	r1, #0
	strneb	r1, [r2, #18]
.L1036:
	ldrb	r2, [r8]	@ zero_extendqisi2
	cmp	r2, #44
	bne	.L1037
	ldrb	r2, [r4, #-1864]	@ zero_extendqisi2
	cmp	r2, #0
	beq	.L1037
	cmp	r3, r7
	bne	.L1038
	ldr	r3, .L1087
	ldrb	r3, [r3, #-2744]	@ zero_extendqisi2
	cmp	r3, #0
	bne	.L1037
.L1038:
	mov	r0, #1
	mov	r3, #0
	strb	r3, [r4, #-1864]
	bl	FlashSetInterfaceMode
	mov	r0, #1
	bl	NandcSetMode
.L1037:
	ldrb	r3, [r4, #-1876]	@ zero_extendqisi2
	tst	r3, #6
	beq	.L1039
	ldr	r2, .L1087
	ldrb	r2, [r2, #-1864]	@ zero_extendqisi2
	cmp	r2, #0
	bne	.L1040
	tst	r3, #1
	bne	.L1039
.L1040:
	mov	r0, #0
	bl	flash_enter_slc_mode
	mov	r0, #0
	ldr	r1, [r4, #-1784]
	bl	FlashDdrParaScan
	mov	r0, #0
	bl	flash_exit_slc_mode
.L1039:
	ldr	r3, [r5, #3320]
	mov	r10, #16
	ldr	r9, .L1087+4
	ldr	r6, .L1087+84
	ldrb	r0, [r3, #20]	@ zero_extendqisi2
	bl	FlashBchSel
	add	r0, r9, #3328
	bl	FlashReadIdbDataRaw
	ldr	r0, .L1087+88
	strb	r10, [r5, #1]
	bl	FlashTimingCfg
	ldr	r7, [r5, #3320]
	ldrb	r3, [r7, #12]	@ zero_extendqisi2
	strh	r3, [r6, #8]	@ movhi
	ldrb	r3, [r7, #7]	@ zero_extendqisi2
	str	r3, [r4, #-2768]
	ldr	r3, [r8]	@ unaligned
	str	r3, [r4, #-2772]
	ldrb	r3, [r5, #3148]	@ zero_extendqisi2
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
	ldrb	r1, [r7, #9]	@ zero_extendqisi2
	ldrh	r3, [r7, #10]
	smulbb	r3, r1, r3
	mov	r1, #512
	strh	r1, [r6, #24]	@ movhi
	ldrb	r1, [r5, #1]	@ zero_extendqisi2
	strh	r1, [r6, #26]	@ movhi
	uxth	r3, r3
	ldrb	r1, [r5]	@ zero_extendqisi2
	strh	r3, [r6, #22]	@ movhi
	cmp	r1, #1
	bne	.L1042
	mov	r3, r3, asl #1
	mov	r1, r4, lsr #1
	mov	r2, r2, asl #1
	strh	r3, [r6, #22]	@ movhi
	strb	r10, [r9, #1]
	mov	r3, #8
	strh	r1, [r6, #14]	@ movhi
	strh	r2, [r6, #20]	@ movhi
	strh	r3, [r6, #26]	@ movhi
.L1042:
	ldrb	r0, [r7, #20]	@ zero_extendqisi2
	bl	FlashBchSel
	bl	ftl_flash_suspend
	mov	r0, #0
	b	.L1057
.L1044:
	mvn	r0, #1
.L1057:
	add	sp, sp, #20
	@ sp needed
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L1088:
	.align	2
.L1087:
	.word	.LANCHOR2
	.word	.LANCHOR0
	.word	IDByte
	.word	.LANCHOR0+12
	.word	.LC13
	.word	.LANCHOR2-2732
	.word	1446522928
	.word	.LANCHOR1+256
	.word	.LANCHOR1
	.word	.LANCHOR1+3316
	.word	.LANCHOR1+2976
	.word	.LANCHOR0+44
	.word	.LC14
	.word	g_retryMode
	.word	g_maxRetryCount
	.word	g_maxRegNum
	.word	HynixReadRetrial
	.word	.LANCHOR2-2704
	.word	MicronReadRetrial
	.word	ToshibaReadRetrial
	.word	SamsungReadRetrial
	.word	.LANCHOR2-2772
	.word	150000
	.fnend
	.size	FlashInit, .-FlashInit
	.align	2
	.global	FlashPageProgMsbFFData
	.type	FlashPageProgMsbFFData, %function
FlashPageProgMsbFFData:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	stmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, lr}
	.save {r4, r5, r6, r7, r8, r9, r10, lr}
	mov	r4, r2
	ldr	r5, .L1103
	mov	r7, r1
	ldr	r2, .L1103+4
	mov	r6, r0
	ldrb	r1, [r5, #-2744]	@ zero_extendqisi2
	ldr	r3, [r2, #3320]
	mov	r8, r2
	cmp	r1, #0
	ldrb	r3, [r3, #19]	@ zero_extendqisi2
	beq	.L1090
	ldr	r2, [r5, #-1868]
	cmp	r2, #0
	ldmnefd	sp!, {r4, r5, r6, r7, r8, r9, r10, pc}
.L1090:
	sub	r2, r3, #5
	cmp	r3, #68
	cmpne	r2, #2
	bls	.L1091
	sub	r3, r3, #19
	tst	r3, #239
	ldmnefd	sp!, {r4, r5, r6, r7, r8, r9, r10, pc}
.L1091:
	ldr	r9, .L1103+8
	movw	r10, #65535
.L1093:
	ldr	r3, [r8, #3320]
	ldrh	r3, [r3, #10]
	cmp	r3, r4
	bls	.L1102
	mov	r3, r4, asl #1
	ldrh	r3, [r9, r3]
	cmp	r3, r10
	ldmnefd	sp!, {r4, r5, r6, r7, r8, r9, r10, pc}
	mov	r1, #255
	mov	r2, #32768
	ldr	r0, [r5, #-1772]
	bl	ftl_memset
	ldr	r2, [r5, #-1772]
	add	r1, r4, r7
	add	r4, r4, #1
	mov	r0, r6
	mov	r3, r2
	uxth	r4, r4
	bl	FlashProgPage
	b	.L1093
.L1102:
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, pc}
.L1104:
	.align	2
.L1103:
	.word	.LANCHOR2
	.word	.LANCHOR0
	.word	.LANCHOR0+1100
	.fnend
	.size	FlashPageProgMsbFFData, .-FlashPageProgMsbFFData
	.align	2
	.global	FlashReadSlc2KPages
	.type	FlashReadSlc2KPages, %function
FlashReadSlc2KPages:
	.fnstart
	@ args = 0, pretend = 0, frame = 24
	@ frame_needed = 0, uses_anonymous_args = 0
	ldr	r3, .L1156
	stmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.save {r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.pad #36
	sub	sp, sp, #36
	ldrb	r3, [r3, #265]	@ zero_extendqisi2
	mov	r4, r0
	ldr	r10, .L1156+4
	mov	r9, #0
	str	r1, [sp, #16]
	str	r2, [sp, #20]
	str	r3, [sp, #12]
.L1106:
	ldr	r3, [sp, #16]
	cmp	r9, r3
	beq	.L1155
	ldr	r3, [sp, #16]
	mov	r0, r4
	ldr	r1, [sp, #20]
	add	r2, sp, #28
	rsb	r3, r9, r3
	ldr	r8, .L1156+8
	uxtb	r3, r3
	str	r3, [sp]
	add	r3, sp, #24
	bl	LogAddr2PhyAddr
	ldrb	r3, [r8, #3148]	@ zero_extendqisi2
	ldr	r5, [sp, #24]
	cmp	r5, r3
	mvncs	r3, #0
	strcs	r3, [r4]
	bcs	.L1108
	add	r5, r8, r5
	mov	r7, #0
	ldrb	r5, [r5, #3152]	@ zero_extendqisi2
	mov	r0, r5
	bl	NandcWaitFlashReady
	mov	r0, r5
	bl	NandcFlashCs
.L1109:
	ldr	r1, [sp, #28]
	mov	r0, r5
	bl	FlashReadCmd
	mov	r0, r5
	bl	NandcWaitFlashReady
	ldr	r3, [r4, #12]
	mov	r0, r5
	mov	r1, #0
	str	r3, [sp]
	ldr	r2, [sp, #12]
	ldr	r3, [r4, #8]
	bl	NandcXferData
	ldr	r3, .L1156+4
	ldrb	r3, [r3, #-1756]	@ zero_extendqisi2
	cmp	r3, #0
	mov	r6, r0
	beq	.L1110
	mov	r0, r5
	bl	flash_read_ecc
	cmp	r0, #5
	movhi	r6, #256
.L1110:
	cmp	r7, #9
	cmnls	r6, #1
	moveq	r3, #1
	movne	r3, #0
	addeq	r7, r7, #1
	beq	.L1109
.L1111:
	cmp	r7, #0
	mov	r7, r3
	movne	r6, #256
.L1113:
	ldr	r3, [sp, #28]
	mov	r0, r5
	ldr	r1, [r8, #4]
	add	r1, r1, r3
	bl	FlashReadCmd
	mov	r0, r5
	bl	NandcWaitFlashReady
	ldr	r3, [r4, #8]
	ldr	r2, [r4, #12]
	mov	r0, r5
	cmp	r3, #0
	mov	r1, #0
	addne	r3, r3, #2048
	cmp	r2, #0
	addne	r2, r2, #8
	str	r2, [sp]
	ldr	r2, [sp, #12]
	bl	NandcXferData
	ldrb	r2, [r10, #-1756]	@ zero_extendqisi2
	cmp	r2, #0
	mov	fp, r0
	beq	.L1116
	mov	r0, r5
	bl	flash_read_ecc
	cmp	r0, #5
	movhi	fp, #256
.L1116:
	cmp	r7, #9
	cmnls	fp, #1
	addeq	r7, r7, #1
	beq	.L1113
.L1117:
	cmp	r7, #0
	mov	r0, r5
	movne	fp, #256
	bl	NandcFlashDeCs
	ldrb	r2, [r10, #-2743]	@ zero_extendqisi2
	cmp	fp, r6
	movcs	r3, fp
	movcc	r3, r6
	add	r2, r2, r2, asl #1
	cmp	r3, r2, asr #2
	bls	.L1119
	cmn	r3, #1
	movne	r3, #256
.L1119:
	cmp	r3, #256
	cmnne	r3, #1
	movne	r3, #0
	str	r3, [r4]
	ldr	r3, [r4, #12]
	cmp	r3, #0
	beq	.L1122
	ldr	r2, [r3, #12]
	cmn	r2, #1
	bne	.L1122
	ldr	r2, [r3, #8]
	cmn	r2, #1
	bne	.L1122
	ldr	r3, [r3]
	cmn	r3, #1
	strne	r2, [r4]
.L1122:
	ldr	r3, [r4]
	cmn	r3, #1
	bne	.L1108
	ldr	r1, [r4, #4]
	ldr	r0, .L1156+12
	ldrb	r2, [r10, #-2743]	@ zero_extendqisi2
	bl	printk
	ldr	r1, [r4, #8]
	cmp	r1, #0
	beq	.L1124
	ldr	r0, .L1156+16
	mov	r2, #4
	mov	r3, #8
	bl	rknand_print_hex
.L1124:
	ldr	r1, [r4, #12]
	cmp	r1, #0
	beq	.L1108
	mov	r2, #4
	ldr	r0, .L1156+20
	mov	r3, r2
	bl	rknand_print_hex
.L1108:
	add	r9, r9, #1
	add	r4, r4, #36
	b	.L1106
.L1155:
	mov	r0, #0
	add	sp, sp, #36
	@ sp needed
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L1157:
	.align	2
.L1156:
	.word	.LANCHOR1
	.word	.LANCHOR2
	.word	.LANCHOR0
	.word	.LC15
	.word	.LC16
	.word	.LC17
	.fnend
	.size	FlashReadSlc2KPages, .-FlashReadSlc2KPages
	.align	2
	.global	FlashReadPages
	.type	FlashReadPages, %function
FlashReadPages:
	.fnstart
	@ args = 0, pretend = 0, frame = 40
	@ frame_needed = 0, uses_anonymous_args = 0
	ldr	r3, .L1233
	stmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.save {r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.pad #52
	sub	sp, sp, #52
	ldr	r9, .L1233+4
	ldrb	r3, [r3, #265]	@ zero_extendqisi2
	str	r1, [sp, #20]
	ldrb	r8, [r9]	@ zero_extendqisi2
	str	r3, [sp, #16]
	ldrb	r3, [r9, #8]	@ zero_extendqisi2
	cmp	r8, #0
	str	r2, [sp, #24]
	str	r3, [sp, #28]
	beq	.L1195
	bl	FlashReadSlc2KPages
	b	.L1228
.L1195:
	ldr	r7, .L1233+8
	mov	r10, r0
	mov	fp, r8
.L1159:
	ldr	r3, [sp, #20]
	cmp	fp, r3
	bcs	.L1231
	mov	r3, #36
	add	r2, sp, #44
	mul	r3, r3, fp
	ldr	r1, [sp, #24]
	add	r6, r10, r3
	str	r3, [sp, #8]
	mov	r0, r6
	ldr	r3, [r6, #4]
	str	r3, [sp, #12]
	ldr	r3, [sp, #20]
	rsb	r3, fp, r3
	uxtb	r3, r3
	str	r3, [sp]
	add	r3, sp, #40
	bl	LogAddr2PhyAddr
	ldrb	r2, [r9, #3148]	@ zero_extendqisi2
	ldr	r3, [sp, #40]
	mov	r5, r0
	cmp	r3, r2
	ldrcs	r2, [sp, #8]
	mvncs	r3, #0
	strcs	r3, [r10, r2]
	bcs	.L1162
	add	r3, r9, r3
	ldrb	r4, [r3, #3152]	@ zero_extendqisi2
	ldrb	r3, [r7, #-1755]	@ zero_extendqisi2
	cmp	r3, #0
	mov	r0, r4
	moveq	r5, #0
	bl	NandcWaitFlashReady
	ldr	r3, .L1233+4
	ldr	r3, [r3, #3320]
	ldrb	r2, [r3, #19]	@ zero_extendqisi2
	sub	r3, r2, #1
	cmp	r3, #7
	bhi	.L1164
	sub	r2, r2, #7
	add	r1, r7, r4
	cmp	r2, #1
	ldr	r2, .L1233+8
	ldrb	r3, [r1, #-2720]	@ zero_extendqisi2
	add	r2, r2, r4
	ldrlsb	r3, [r1, #-2712]	@ zero_extendqisi2
	ldrb	r2, [r2, #-1880]	@ zero_extendqisi2
	cmp	r2, r3
	beq	.L1164
	mov	r0, r4
	ldrb	r1, [r7, #-2731]	@ zero_extendqisi2
	ldr	r2, .L1233+12
	bl	HynixSetRRPara
.L1164:
	mov	r0, r4
	bl	NandcFlashCs
	mov	r0, r4
	ldr	r3, [sp, #12]
	ldr	r2, [sp, #24]
	mov	r3, r3, lsr #31
	cmp	r2, #1
	orreq	r3, r3, #1
	str	r3, [sp, #12]
	cmp	r3, #0
	beq	.L1166
	ldrb	r3, [r7, #-2744]	@ zero_extendqisi2
	cmp	r3, #0
	beq	.L1166
	bl	flash_enter_slc_mode
	b	.L1167
.L1166:
	bl	flash_exit_slc_mode
.L1167:
	ldr	r1, [sp, #44]
	cmn	r1, #1
	cmpeq	r4, #255
	moveq	r3, #0
	movne	r3, #1
	moveq	r5, r3
	beq	.L1169
	cmp	r5, #0
	beq	.L1170
	ldr	r2, [r9, #4]
	mov	r0, r4
	add	r2, r1, r2
	bl	FlashReadDpCmd
	b	.L1171
.L1170:
	mov	r0, r4
	bl	FlashReadCmd
.L1171:
	mov	r0, r4
	bl	NandcWaitFlashReady
	cmp	r5, #0
	beq	.L1169
	mov	r0, r4
	ldr	r1, [sp, #44]
	bl	FlashReadDpDataOutCmd
.L1169:
	ldr	r3, [r6, #12]
	mov	r0, r4
	ldr	r2, [sp, #16]
	mov	r1, #0
	str	r3, [sp]
	ldr	r3, [r6, #8]
	bl	NandcXferData
	ldrb	r2, [r9, #8]	@ zero_extendqisi2
	adds	r2, r2, #0
	movne	r2, #1
	cmn	r0, #1
	mov	ip, r0
	movne	r2, #0
	cmp	r2, #0
	movne	r3, #0
	strneb	r3, [r9, #8]
	movne	r5, r3
	bne	.L1167
.L1172:
	cmp	r5, #0
	beq	.L1173
	ldr	r3, .L1233+4
	str	r0, [sp, #32]
	mov	r0, r4
	str	r2, [sp, #36]
	ldr	r1, [r3, #4]
	ldr	r3, [sp, #44]
	add	r1, r1, r3
	bl	FlashReadDpDataOutCmd
	mov	r0, r4
	ldr	r3, [sp, #8]
	ldr	r2, [sp, #36]
	add	r3, r3, #36
	add	r3, r10, r3
	ldr	r1, [r3, #12]
	str	r1, [sp]
	mov	r1, r2
	ldr	r3, [r3, #8]
	ldr	r2, [sp, #16]
	bl	NandcXferData
	cmn	r0, #1
	ldr	ip, [sp, #32]
	mov	r8, r0
	moveq	r5, #0
.L1173:
	mov	r0, r4
	str	ip, [sp, #32]
	bl	NandcFlashDeCs
	ldr	ip, [sp, #32]
	ldrb	r3, [sp, #28]	@ zero_extendqisi2
	cmn	ip, #1
	strb	r3, [r9, #8]
	bne	.L1180
	ldrb	r3, [r7, #-1864]	@ zero_extendqisi2
	cmp	r3, #0
	bne	.L1175
.L1179:
	ldr	r5, [r7, #-1792]
	cmp	r5, #0
	bne	.L1176
	b	.L1232
.L1175:
	ldr	r3, [r7, #-2808]
	mov	r0, r4
	ldr	r1, [sp, #44]
	ldr	r5, [r3, #304]
	mov	r3, #1
	str	r3, [sp]
	ldr	r2, [r6, #8]
	ldr	r3, [r6, #12]
	bl	FlashDdrTunningRead
	cmn	r0, #1
	mov	ip, r0
	beq	.L1178
	ldrb	r3, [r7, #-2743]	@ zero_extendqisi2
	cmp	r0, r3, lsr #1
	bls	.L1198
.L1178:
	ubfx	r0, r5, #8, #8
	str	ip, [sp, #32]
	bl	NandcSetDdrPara
	ldr	ip, [sp, #32]
	cmn	ip, #1
	beq	.L1179
	b	.L1198
.L1176:
	mov	r0, r4
	ldr	r1, [sp, #44]
	ldr	r2, [r6, #8]
	ldr	r3, [r6, #12]
	blx	r5
	cmn	r0, #1
	mov	ip, r0
	bne	.L1200
	ldr	r3, [r9, #3320]
	ldrb	r3, [r3, #19]	@ zero_extendqisi2
	sub	r3, r3, #1
	cmp	r3, #7
	bhi	.L1182
	mov	r0, r4
	ldrb	r1, [r7, #-2731]	@ zero_extendqisi2
	ldr	r2, .L1233+12
	mov	r3, #0
	bl	HynixSetRRPara
.L1182:
	ldr	r1, [sp, #44]
	mov	r0, r4
	ldr	r2, [r6, #8]
	ldr	r3, [r6, #12]
	bl	FlashReadRawPage
	ldr	r1, [r6, #4]
	ldrb	r2, [r7, #-2743]	@ zero_extendqisi2
	mov	ip, r0
	ldr	r0, .L1233+16
	mov	r3, ip
	str	ip, [sp, #32]
	bl	printk
	ldr	ip, [sp, #32]
	cmn	ip, #1
	bne	.L1200
	ldrb	r5, [r7, #-2744]	@ zero_extendqisi2
	cmp	r5, #0
	beq	.L1181
	ldr	r3, [sp, #12]
	mov	r0, r4
	cmp	r3, #0
	beq	.L1183
	bl	flash_enter_slc_mode
	b	.L1184
.L1183:
	bl	flash_exit_slc_mode
.L1184:
	ldr	r3, .L1233+8
	mov	r0, r4
	ldr	r1, [sp, #44]
	ldr	r2, [r6, #8]
	ldr	ip, [r3, #-1792]
	ldr	r3, [r6, #12]
	blx	ip
	mov	ip, r0
	b	.L1200
.L1232:
	mov	r0, r4
	ldr	r1, [sp, #44]
	ldr	r2, [r6, #8]
	ldr	r3, [r6, #12]
	bl	FlashReadRawPage
	mov	ip, r0
	b	.L1181
.L1198:
	mov	r5, #0
.L1180:
	ldrb	r3, [r7, #-2743]	@ zero_extendqisi2
	add	r3, r3, r3, asl #1
	cmp	ip, r3, asr #2
	bls	.L1181
	ldr	r3, [r7, #-1792]
	cmp	r3, #0
	moveq	ip, #256
	b	.L1181
.L1200:
	mov	r5, #0
.L1181:
	cmp	ip, #256
	cmnne	ip, #1
	ldreq	r3, [sp, #8]
	movne	r3, #0
	ldrne	r2, [sp, #8]
	streq	ip, [r10, r3]
	strne	r3, [r10, r2]
	ldr	r3, [sp, #8]
	ldr	r3, [r10, r3]
	cmn	r3, #1
	bne	.L1188
	ldr	r1, [r6, #4]
	ldr	r0, .L1233+20
	ldrb	r2, [r7, #-2743]	@ zero_extendqisi2
	bl	printk
	ldr	r1, [r6, #12]
	cmp	r1, #0
	beq	.L1188
	mov	r2, #4
	ldr	r0, .L1233+24
	mov	r3, r2
	bl	rknand_print_hex
.L1188:
	cmp	r5, #0
	beq	.L1190
	ldrb	r3, [r7, #-2743]	@ zero_extendqisi2
	add	r3, r3, r3, asl #1
	cmp	r8, r3, asr #2
	bls	.L1191
	ldr	r3, [r7, #-1792]
	cmp	r3, #0
	moveq	r8, #256
.L1191:
	ldr	r3, [sp, #8]
	cmp	r8, #256
	cmnne	r8, #1
	add	r3, r3, #36
	movne	r2, #0
	streq	r8, [r10, r3]
	strne	r2, [r10, r3]
.L1190:
	ldr	r3, [sp, #12]
	add	fp, fp, r5
	cmp	r3, #0
	beq	.L1162
	ldrb	r3, [r7, #-2744]	@ zero_extendqisi2
	cmp	r3, #0
	beq	.L1162
	mov	r0, r4
	bl	flash_exit_slc_mode
.L1162:
	add	fp, fp, #1
	b	.L1159
.L1231:
	mov	r0, #0
.L1228:
	add	sp, sp, #52
	@ sp needed
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L1234:
	.align	2
.L1233:
	.word	.LANCHOR1
	.word	.LANCHOR0
	.word	.LANCHOR2
	.word	.LANCHOR2-2728
	.word	.LC18
	.word	.LC15
	.word	.LC17
	.fnend
	.size	FlashReadPages, .-FlashReadPages
	.align	2
	.global	FlashLoadFactorBbt
	.type	FlashLoadFactorBbt, %function
FlashLoadFactorBbt:
	.fnstart
	@ args = 0, pretend = 0, frame = 48
	@ frame_needed = 0, uses_anonymous_args = 0
	stmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.save {r4, r5, r6, r7, r8, r9, r10, fp, lr}
	mov	r1, #0
	ldr	r5, .L1248
	.pad #52
	sub	sp, sp, #52
	mov	r2, #16
	ldr	r9, .L1248+4
	add	r0, r5, #1020
	ldr	fp, .L1248+8
	ldrh	r7, [r5, #12]
	mvn	r10, #0
	ldrh	r4, [r5, #14]
	bl	ftl_memset
	ldr	r3, [r9, #-1768]
	mov	r5, #0
	mov	r8, r5
	smulbb	r7, r7, r4
	uxth	r6, r7
	str	r5, [sp, #20]
	str	r3, [sp, #24]
.L1236:
	ldrb	r3, [fp, #3148]	@ zero_extendqisi2
	uxtb	r7, r5
	cmp	r3, r7
	bls	.L1247
	mul	ip, r6, r7
	sub	r4, r6, #1
	sub	r3, r6, #12
	uxth	r4, r4
.L1237:
	cmp	r4, r3
	ble	.L1239
	add	r2, ip, r4
	mov	r1, #1
	add	r0, sp, #12
	str	r3, [sp, #4]
	mov	r2, r2, asl #10
	str	r2, [sp, #16]
	mov	r2, r1
	str	ip, [sp]
	bl	FlashReadPages
	ldr	r2, [sp, #12]
	ldr	ip, [sp]
	cmn	r2, #1
	ldr	r3, [sp, #4]
	beq	.L1238
	ldr	r2, [r9, #-1768]
	ldrh	r1, [r2]
	movw	r2, #61664
	cmp	r1, r2
	bne	.L1238
	mov	r1, r7
	ldr	r0, .L1248+12
	mov	r2, r4
	mov	r7, r7, asl #1
	bl	printk
	ldr	r3, .L1248+16
	add	r8, r8, #1
	strh	r4, [r3, r7]	@ movhi
	uxth	r8, r8
	b	.L1239
.L1238:
	sub	r4, r4, #1
	uxth	r4, r4
	b	.L1237
.L1239:
	ldr	r3, .L1248+8
	add	r5, r5, #1
	ldrb	r3, [r3, #3148]	@ zero_extendqisi2
	cmp	r3, r8
	moveq	r10, #0
	b	.L1236
.L1247:
	mov	r0, r10
	add	sp, sp, #52
	@ sp needed
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L1249:
	.align	2
.L1248:
	.word	.LANCHOR2-2772
	.word	.LANCHOR2
	.word	.LANCHOR0
	.word	.LC19
	.word	.LANCHOR2-1752
	.fnend
	.size	FlashLoadFactorBbt, .-FlashLoadFactorBbt
	.align	2
	.global	FlashReadFacBbtData
	.type	FlashReadFacBbtData, %function
FlashReadFacBbtData:
	.fnstart
	@ args = 0, pretend = 0, frame = 40
	@ frame_needed = 0, uses_anonymous_args = 0
	ldr	r3, .L1265
	stmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, lr}
	.save {r4, r5, r6, r7, r8, r9, r10, lr}
	mov	r8, r2
	ldrh	r4, [r3, #14]
	.pad #40
	sub	sp, sp, #40
	ldrh	r2, [r3, #12]
	mov	r5, r0
	ldr	r9, .L1265+4
	mov	r7, r1
	smulbb	r4, r2, r4
	ldr	r2, [r9, #-1872]
	str	r2, [sp, #12]
	ldr	r2, [r9, #-1768]
	uxth	r3, r4
	sub	r6, r3, #1
	sub	r4, r3, #16
	mul	r10, r3, r1
	uxth	r6, r6
	str	r2, [sp, #16]
.L1251:
	cmp	r6, r4
	ble	.L1264
	mov	r1, #1
	add	r3, r6, r10
	add	r0, sp, #4
	mov	r2, r1
	mov	r3, r3, asl #10
	str	r3, [sp, #8]
	bl	FlashReadPages
	ldr	r3, [sp, #4]
	cmn	r3, #1
	beq	.L1252
	ldr	r3, [r9, #-1768]
	ldrh	r2, [r3]
	movw	r3, #61664
	cmp	r2, r3
	bne	.L1252
	cmp	r5, #0
	moveq	r0, r5
	beq	.L1253
	cmp	r7, #0
	ldreq	ip, .L1265+4
	moveq	lr, #1
	beq	.L1254
.L1256:
	ldr	r1, [r9, #-1872]
	mov	r2, r8
	mov	r0, r5
	bl	ftl_memcpy
	mov	r2, #4
	ldr	r0, .L1265+8
	mov	r1, r5
	mov	r3, r2
	bl	rknand_print_hex
	mov	r0, #0
	b	.L1253
.L1254:
	ldr	r3, [r9, #-1780]
	uxth	r4, r7
	add	r7, r7, #1
	cmp	r4, r3
	bcs	.L1256
	ldr	r1, [ip, #-1872]
	mov	r0, r4, lsr #5
	and	r3, r4, #31
	ldr	r2, [r1, r0, asl #2]
	orr	r3, r2, lr, asl r3
	str	r3, [r1, r0, asl #2]
	b	.L1254
.L1252:
	sub	r6, r6, #1
	uxth	r6, r6
	b	.L1251
.L1264:
	mvn	r0, #0
.L1253:
	add	sp, sp, #40
	@ sp needed
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, pc}
.L1266:
	.align	2
.L1265:
	.word	.LANCHOR2-2772
	.word	.LANCHOR2
	.word	.LC20
	.fnend
	.size	FlashReadFacBbtData, .-FlashReadFacBbtData
	.align	2
	.global	FlashGetBadBlockList
	.type	FlashGetBadBlockList, %function
FlashGetBadBlockList:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	ldr	r3, .L1278
	stmfd	sp!, {r4, r5, r6, r7, r8, lr}
	.save {r4, r5, r6, r7, r8, lr}
	mov	r5, r0
	ldr	r3, [r3, #3320]
	ldr	r6, .L1278+4
	ldrb	r4, [r3, #13]	@ zero_extendqisi2
	ldrh	r3, [r3, #14]
	ldr	r0, [r6, #-1772]
	smulbb	r4, r4, r3
	uxth	r4, r4
	add	r2, r4, #7
	mov	r2, r2, asr #3
	bl	FlashReadFacBbtData
	cmn	r0, #1
	bne	.L1268
.L1272:
	mov	r3, #0
	b	.L1269
.L1268:
	mov	lr, r4, lsr #4
	mov	ip, #0
	sub	r4, r4, #1
	mov	r3, ip
	mov	r7, #1
.L1270:
	uxth	r0, ip
	cmp	r0, r4
	bge	.L1269
	ldr	r8, [r6, #-1772]
	mov	r1, r0, lsr #5
	and	r2, r0, #31
	add	ip, ip, #1
	ldr	r1, [r8, r1, asl #2]
	ands	r2, r1, r7, asl r2
	addne	r2, r3, #1
	movne	r3, r3, asl #1
	strneh	r0, [r5, r3]	@ movhi
	uxthne	r3, r2
	cmp	r3, lr
	bcc	.L1270
	b	.L1272
.L1269:
	mov	r3, r3, asl #1
	mvn	r2, #0
	mov	r0, #0
	strh	r2, [r5, r3]	@ movhi
	ldmfd	sp!, {r4, r5, r6, r7, r8, pc}
.L1279:
	.align	2
.L1278:
	.word	.LANCHOR0
	.word	.LANCHOR2
	.fnend
	.size	FlashGetBadBlockList, .-FlashGetBadBlockList
	.align	2
	.global	FlashProgSlc2KPages
	.type	FlashProgSlc2KPages, %function
FlashProgSlc2KPages:
	.fnstart
	@ args = 0, pretend = 0, frame = 56
	@ frame_needed = 0, uses_anonymous_args = 0
	stmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.save {r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.pad #68
	sub	sp, sp, #68
	ldr	r8, .L1311
	mov	r6, r1
	str	r3, [sp, #12]
	mov	r9, r2
	ldr	r3, .L1311+4
	mov	r4, r0
	mov	r10, r0
	mov	r7, #0
	mov	fp, r8
	ldrb	r3, [r3, #265]	@ zero_extendqisi2
	str	r3, [sp, #8]
.L1281:
	cmp	r7, r6
	beq	.L1310
	rsb	r3, r7, r6
	add	r2, sp, #20
	mov	r0, r10
	mov	r1, r9
	uxtb	r3, r3
	str	r3, [sp]
	add	r3, sp, #24
	bl	LogAddr2PhyAddr
	ldrb	r2, [r8, #3148]	@ zero_extendqisi2
	ldr	r3, [sp, #24]
	cmp	r3, r2
	mvncs	r3, #0
	strcs	r3, [r10]
	bcs	.L1283
	add	r3, r8, r3
	ldrb	r5, [r3, #3152]	@ zero_extendqisi2
	mov	r0, r5
	bl	NandcWaitFlashReady
	mov	r0, r5
	bl	NandcFlashCs
	mov	r0, r5
	ldr	r1, [sp, #20]
	bl	FlashProgFirstCmd
	ldr	r3, [r10, #12]
	mov	r1, #1
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
	ldr	r2, [r10, #12]
	mov	r1, #1
	cmp	r3, #0
	mov	r0, r5
	addne	r3, r3, #2048
	cmp	r2, #0
	addne	r2, r2, #8
	str	r2, [sp]
	ldr	r2, [sp, #8]
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
	tst	r0, #1
	mov	r0, r5
	mvnne	r3, #0
	strne	r3, [r10]
	bl	NandcFlashDeCs
.L1283:
	add	r7, r7, #1
	add	r10, r10, #36
	b	.L1281
.L1310:
	ldr	r3, [sp, #12]
	cmp	r3, #0
	beq	.L1308
	ldr	r5, .L1311+8
	mov	r7, #0
	mov	r8, r5
.L1288:
	cmp	r7, r6
	beq	.L1308
	ldr	r3, [r4]
	cmn	r3, #1
	bne	.L1289
	ldr	r1, [r4, #4]
	ldr	r0, .L1311+12
	bl	printk
	b	.L1290
.L1289:
	rsb	r3, r7, r6
	mov	r1, r9
	add	r2, sp, #20
	mov	r0, r4
	uxtb	r3, r3
	str	r3, [sp]
	add	r3, sp, #24
	bl	LogAddr2PhyAddr
	ldr	r2, [r5, #-1764]
	mov	r3, #0
	mov	lr, r4
	str	r3, [r2]
	ldr	r2, [r5, #-1760]
	str	r3, [r2]
	ldmia	lr!, {r0, r1, r2, r3}
	add	ip, sp, #28
	stmia	ip!, {r0, r1, r2, r3}
	ldmia	lr!, {r0, r1, r2, r3}
	stmia	ip!, {r0, r1, r2, r3}
	add	r0, sp, #28
	ldr	r3, [lr]
	mov	r1, #1
	mov	r2, r9
	str	r3, [ip]
	ldr	r3, [r5, #-1764]
	str	r3, [sp, #36]
	ldr	r3, [r5, #-1760]
	str	r3, [sp, #40]
	bl	FlashReadPages
	ldr	r10, [sp, #28]
	cmn	r10, #1
	bne	.L1291
	ldr	r0, .L1311+16
	ldr	r1, [r4, #4]
	bl	printk
	str	r10, [r4]
.L1291:
	ldr	r3, [r4, #12]
	cmp	r3, #0
	beq	.L1292
	ldr	r2, [r3]
	ldr	r3, [r8, #-1760]
	ldr	r3, [r3]
	cmp	r2, r3
	beq	.L1292
	ldr	r0, .L1311+20
	ldr	r1, [r4, #4]
	bl	printk
	mvn	r3, #0
	str	r3, [r4]
.L1292:
	ldr	r3, [r4, #8]
	cmp	r3, #0
	beq	.L1290
	ldr	r2, [r3]
	ldr	r3, [r8, #-1764]
	ldr	r3, [r3]
	cmp	r2, r3
	beq	.L1290
	ldr	r0, .L1311+24
	ldr	r1, [r4, #4]
	bl	printk
	mvn	r3, #0
	str	r3, [r4]
.L1290:
	add	r7, r7, #1
	add	r4, r4, #36
	b	.L1288
.L1308:
	mov	r0, #0
	add	sp, sp, #68
	@ sp needed
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L1312:
	.align	2
.L1311:
	.word	.LANCHOR0
	.word	.LANCHOR1
	.word	.LANCHOR2
	.word	.LC21
	.word	.LC22
	.word	.LC23
	.word	.LC24
	.fnend
	.size	FlashProgSlc2KPages, .-FlashProgSlc2KPages
	.align	2
	.global	FlashProgPages
	.type	FlashProgPages, %function
FlashProgPages:
	.fnstart
	@ args = 0, pretend = 0, frame = 72
	@ frame_needed = 0, uses_anonymous_args = 0
	stmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.save {r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.pad #84
	sub	sp, sp, #84
	ldr	r6, .L1369
	str	r1, [sp, #16]
	str	r3, [sp, #28]
	ldr	ip, [r6, #3320]
	ldrb	r7, [r6]	@ zero_extendqisi2
	ldrb	ip, [ip, #19]	@ zero_extendqisi2
	cmp	r7, #0
	str	ip, [sp, #24]
	ldr	ip, .L1369+4
	ldrb	ip, [ip, #265]	@ zero_extendqisi2
	str	ip, [sp, #20]
	beq	.L1341
	bl	FlashProgSlc2KPages
	b	.L1315
.L1341:
	ldr	r9, .L1369+8
	mov	r4, r0
	mov	r8, r2
.L1314:
	ldr	r3, [sp, #16]
	ldr	r5, .L1369
	cmp	r7, r3
	bcs	.L1367
	mov	r3, #36
	add	r2, sp, #36
	mul	r3, r3, r7
	mov	r1, r8
	add	fp, r4, r3
	str	r3, [sp, #12]
	mov	r0, fp
	ldr	r3, [sp, #16]
	rsb	r3, r7, r3
	uxtb	r3, r3
	str	r3, [sp]
	add	r3, sp, #40
	bl	LogAddr2PhyAddr
	ldrb	r2, [r6, #3148]	@ zero_extendqisi2
	ldr	r3, [sp, #40]
	mov	r10, r0
	cmp	r3, r2
	ldrcs	r2, [sp, #12]
	mvncs	r3, #0
	strcs	r3, [r4, r2]
	bcs	.L1317
	ldrb	r1, [r9, #-1804]	@ zero_extendqisi2
	add	r3, r6, r3, asl #4
	cmp	r1, #0
	ldr	r3, [r3, #3200]
	moveq	r10, #0
	cmp	r3, #0
	beq	.L1319
	cmp	r2, #1
	bne	.L1320
	ldr	r3, .L1369+8
	ldr	r0, [r3, #-2808]
	bl	NandcIqrWaitFlashReady
.L1320:
	ldrb	r0, [sp, #40]	@ zero_extendqisi2
	bl	FlashWaitCmdDone
.L1319:
	ldr	r3, [sp, #40]
	mov	r1, #0
	cmp	r10, #0
	add	r2, r6, r3, asl #4
	str	r1, [r2, #3204]
	ldr	r1, [sp, #36]
	str	fp, [r2, #3200]
	str	r1, [r2, #3196]
	ldrne	r1, [sp, #12]
	addne	r1, r1, #36
	addne	r1, r4, r1
	strne	r1, [r2, #3204]
	add	r2, r6, r3
	add	r3, r6, r3, asl #4
	ldrb	r5, [r2, #3152]	@ zero_extendqisi2
	strb	r5, [r3, #3192]
	mov	r0, r5
	ldrb	r3, [r6, #3148]	@ zero_extendqisi2
	cmp	r3, #1
	bne	.L1322
	bl	NandcWaitFlashReady
	b	.L1323
.L1322:
	bl	NandcFlashCs
	mov	r0, r5
	ldr	r3, [sp, #40]
	ldr	r1, [sp, #36]
	add	r3, r6, r3, asl #2
	ldr	r2, [r3, #3160]
	adds	r2, r2, #0
	movne	r2, #1
	bl	FlashWaitReadyEN
	mov	r0, r5
	bl	NandcFlashDeCs
.L1323:
	ldr	r3, [sp, #24]
	sub	r3, r3, #1
	cmp	r3, #7
	bhi	.L1324
	add	r3, r9, r5
	ldrb	r3, [r3, #-1880]	@ zero_extendqisi2
	cmp	r3, #0
	beq	.L1324
	mov	r0, r5
	ldrb	r1, [r9, #-2731]	@ zero_extendqisi2
	ldr	r2, .L1369+12
	mov	r3, #0
	bl	HynixSetRRPara
.L1324:
	mov	r0, r5
	bl	NandcFlashCs
	cmp	r8, #1
	mov	r0, r5
	bne	.L1325
	ldrb	r3, [r9, #-2744]	@ zero_extendqisi2
	cmp	r3, #0
	beq	.L1325
	bl	flash_enter_slc_mode
	b	.L1326
.L1325:
	bl	flash_exit_slc_mode
.L1326:
	mov	r0, r5
	ldr	r1, [sp, #36]
	bl	FlashProgFirstCmd
	ldr	r3, [fp, #12]
	mov	r0, r5
	mov	r1, #1
	str	r3, [sp]
	ldr	r2, [sp, #20]
	ldr	r3, [fp, #8]
	bl	NandcXferData
	cmp	r10, #0
	beq	.L1327
	mov	r0, r5
	ldr	r1, [sp, #36]
	bl	FlashProgDpFirstCmd
	mov	r0, r5
	ldr	r3, [sp, #40]
	ldr	r1, [sp, #36]
	add	r3, r6, r3, asl #2
	ldr	r2, [r3, #3160]
	adds	r2, r2, #0
	movne	r2, #1
	bl	FlashWaitReadyEN
	ldr	r1, [r6, #4]
	mov	r0, r5
	ldr	r3, [sp, #36]
	add	r1, r1, r3
	bl	FlashProgDpSecondCmd
	mov	r0, r5
	mov	r1, #1
	ldr	r3, [sp, #12]
	add	r3, r3, #36
	add	r3, r4, r3
	ldr	r2, [r3, #12]
	str	r2, [sp]
	ldr	r2, [sp, #20]
	ldr	r3, [r3, #8]
	bl	NandcXferData
.L1327:
	mov	r0, r5
	ldr	r1, [sp, #36]
	bl	FlashProgSecondCmd
	mov	r0, r5
	bl	NandcFlashDeCs
	add	r7, r7, r10
.L1317:
	add	r7, r7, #1
	b	.L1314
.L1367:
	ldr	r6, .L1369+8
	mov	r7, #0
	ldr	r9, .L1369+16
	ldr	r0, [r6, #-2808]
	bl	NandcIqrWaitFlashReady
.L1329:
	ldrb	r3, [r5, #3148]	@ zero_extendqisi2
	cmp	r7, r3
	bcs	.L1368
	uxtb	r0, r7
	bl	FlashWaitCmdDone
	cmp	r8, #1
	bne	.L1330
	ldrb	r3, [r6, #-2744]	@ zero_extendqisi2
	cmp	r3, #0
	beq	.L1330
	ldrb	r0, [r9, r7, asl #4]	@ zero_extendqisi2
	bl	flash_exit_slc_mode
.L1330:
	add	r7, r7, #1
	b	.L1329
.L1368:
	ldr	r3, [sp, #28]
	cmp	r3, #0
	movne	r5, #0
	bne	.L1333
.L1340:
	mov	r0, #0
	b	.L1315
.L1339:
	ldr	r3, [r4]
	cmn	r3, #1
	bne	.L1334
	ldr	r0, .L1369+20
	ldr	r1, [r4, #4]
	bl	printk
.L1335:
	add	r5, r5, #1
	add	r4, r4, #36
.L1333:
	ldr	r3, [sp, #16]
	cmp	r5, r3
	bne	.L1339
	b	.L1340
.L1334:
	ldr	r3, [sp, #16]
	mov	r1, r8
	add	r2, sp, #36
	mov	r0, r4
	rsb	r3, r5, r3
	uxtb	r3, r3
	str	r3, [sp]
	add	r3, sp, #40
	bl	LogAddr2PhyAddr
	ldr	r2, [r6, #-1764]
	mov	r3, #0
	mov	lr, r4
	str	r3, [r2]
	ldr	r2, [r6, #-1760]
	str	r3, [r2]
	ldmia	lr!, {r0, r1, r2, r3}
	add	ip, sp, #44
	stmia	ip!, {r0, r1, r2, r3}
	ldmia	lr!, {r0, r1, r2, r3}
	stmia	ip!, {r0, r1, r2, r3}
	add	r0, sp, #44
	ldr	r3, [lr]
	mov	r1, #1
	mov	r2, r8
	str	r3, [ip]
	ldr	r3, [r6, #-1764]
	str	r3, [sp, #52]
	ldr	r3, [r6, #-1760]
	str	r3, [sp, #56]
	bl	FlashReadPages
	ldr	r7, [sp, #44]
	cmn	r7, #1
	bne	.L1336
	ldr	r0, .L1369+24
	ldr	r1, [r4, #4]
	bl	printk
	str	r7, [r4]
.L1336:
	ldr	r3, [r4, #12]
	cmp	r3, #0
	beq	.L1337
	ldr	r2, [r3]
	ldr	r3, [r6, #-1760]
	ldr	r3, [r3]
	cmp	r2, r3
	beq	.L1337
	ldr	r0, .L1369+28
	ldr	r1, [r4, #4]
	bl	printk
	mvn	r3, #0
	str	r3, [r4]
.L1337:
	ldr	r3, [r4, #8]
	cmp	r3, #0
	beq	.L1335
	ldr	r2, [r3]
	ldr	r3, [r6, #-1764]
	ldr	r3, [r3]
	cmp	r2, r3
	beq	.L1335
	ldr	r0, .L1369+32
	ldr	r1, [r4, #4]
	bl	printk
	mvn	r3, #0
	str	r3, [r4]
	b	.L1335
.L1315:
	add	sp, sp, #84
	@ sp needed
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L1370:
	.align	2
.L1369:
	.word	.LANCHOR0
	.word	.LANCHOR1
	.word	.LANCHOR2
	.word	.LANCHOR2-2728
	.word	.LANCHOR0+3192
	.word	.LC21
	.word	.LC22
	.word	.LC23
	.word	.LC24
	.fnend
	.size	FlashProgPages, .-FlashProgPages
	.align	2
	.global	FlashTestBlk
	.type	FlashTestBlk, %function
FlashTestBlk:
	.fnstart
	@ args = 0, pretend = 0, frame = 104
	@ frame_needed = 0, uses_anonymous_args = 0
	stmfd	sp!, {r4, r5, lr}
	.save {r4, r5, lr}
	.pad #108
	sub	sp, sp, #108
	ldr	r5, .L1375
	ldr	r3, [r5, #-1780]
	cmp	r0, r3
	movcc	r4, #0
	bcc	.L1372
	ldr	r3, [r5, #-1772]
	mov	r4, r0
	mov	r1, #165
	add	r0, sp, #40
	mov	r2, #32
	str	r0, [sp, #16]
	str	r3, [sp, #12]
	bl	ftl_memset
	mov	r1, #90
	mov	r2, #8
	ldr	r0, [r5, #-1772]
	mov	r4, r4, asl #10
	bl	ftl_memset
	mov	r1, #1
	mov	r2, r1
	add	r0, sp, #4
	str	r4, [sp, #8]
	bl	FlashEraseBlocks
	mov	r1, #1
	mov	r2, r1
	mov	r3, r1
	add	r0, sp, #4
	bl	FlashProgPages
	mov	r1, #0
	mov	r2, #1
	ldr	r4, [sp, #4]
	add	r0, sp, #4
	adds	r4, r4, #0
	movne	r4, #1
	rsb	r4, r4, #0
	bl	FlashEraseBlocks
.L1372:
	mov	r0, r4
	add	sp, sp, #108
	@ sp needed
	ldmfd	sp!, {r4, r5, pc}
.L1376:
	.align	2
.L1375:
	.word	.LANCHOR2
	.fnend
	.size	FlashTestBlk, .-FlashTestBlk
	.align	2
	.global	FlashMakeFactorBbt
	.type	FlashMakeFactorBbt, %function
FlashMakeFactorBbt:
	.fnstart
	@ args = 0, pretend = 0, frame = 80
	@ frame_needed = 0, uses_anonymous_args = 0
	stmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.save {r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.pad #84
	sub	sp, sp, #84
	ldr	r4, .L1432
	mov	r1, #1
	ldr	r5, .L1432+4
	ldr	r0, .L1432+8
	ldr	r3, [r4, #-1768]
	ldrh	r8, [r5, #12]
	ldr	r7, .L1432+12
	str	r3, [sp, #20]
	ldrh	r3, [r5, #14]
	smulbb	r8, r8, r3
	ldr	r3, .L1432+12
	ldr	r2, [r3, #3320]
	uxth	r8, r8
	ldrb	r2, [r2, #24]	@ zero_extendqisi2
	str	r2, [sp, #24]
	ldrh	r2, [r3, #4]
	ldrb	r3, [r3]	@ zero_extendqisi2
	cmp	r3, #1
	str	r2, [sp, #16]
	moveq	r3, r2
	moveq	r3, r3, asl #1
	uxtheq	r3, r3
	streq	r3, [sp, #16]
	bl	printk
	ldr	r0, [r4, #-1768]
	mov	r1, #0
	mov	r2, #4096
	bl	ftl_memset
	ldr	r4, .L1432
	mov	r3, r8, lsr #4
	str	r3, [sp, #28]
	mov	r3, #0
	str	r3, [sp, #8]
.L1379:
	ldrb	r6, [sp, #8]	@ zero_extendqisi2
	ldrb	r3, [r7, #3148]	@ zero_extendqisi2
	cmp	r3, r6
	bls	.L1428
	ldr	r3, .L1432+16
	mov	r2, r6, asl #1
	ldrh	r5, [r3, r2]
	cmp	r5, #0
	bne	.L1408
	sub	r3, r3, #1020
	ldr	r0, [r4, #-1872]
	mov	r1, r5
	mov	r9, r5
	ldrh	r2, [r3, #20]
	mov	r2, r2, asl #9
	bl	ftl_memset
	add	r3, r7, r6
	ldrb	r10, [r3, #3152]	@ zero_extendqisi2
	mov	r3, r6, asl #2
	add	fp, r7, r3
	str	r5, [sp, #4]
	str	r3, [sp, #32]
.L1381:
	ldrh	r3, [sp, #4]
	cmp	r3, r8
	str	r3, [sp, #12]
	bcs	.L1390
	mvn	r3, #0
	strb	r3, [sp, #42]
	strb	r3, [sp, #43]
	ldr	r3, [sp, #24]
	tst	r3, #1
	beq	.L1383
	ldr	r3, [fp, #3160]
	add	r2, sp, #42
	mov	r0, r10
	add	r3, r5, r3
	str	r3, [sp, #36]
	mov	r1, r3
	bl	FlashReadSpare
	ldrb	r2, [r7]	@ zero_extendqisi2
	cmp	r2, #1
	ldr	r3, [sp, #36]
	bne	.L1383
	ldr	r1, [r7, #4]
	add	r2, sp, #43
	mov	r0, r10
	add	r1, r3, r1
	bl	FlashReadSpare
	ldrb	r3, [sp, #42]	@ zero_extendqisi2
	ldrb	r2, [sp, #43]	@ zero_extendqisi2
	and	r3, r3, r2
	strb	r3, [sp, #42]
.L1383:
	ldr	r3, [sp, #24]
	tst	r3, #2
	beq	.L1385
	ldr	r3, [r7, #3320]
	mov	r0, r10
	add	r2, sp, #43
	ldrh	r1, [r3, #10]
	ldr	r3, [fp, #3160]
	sub	r1, r1, #1
	add	r1, r1, r3
	add	r1, r1, r5
	bl	FlashReadSpare
.L1385:
	ldr	r3, [r7, #3320]
	ldrb	r3, [r3, #7]	@ zero_extendqisi2
	cmp	r3, #1
	cmpne	r3, #8
	ldrb	r3, [sp, #42]	@ zero_extendqisi2
	bne	.L1386
	cmp	r3, #0
	ldrneb	r0, [sp, #43]	@ zero_extendqisi2
	clzne	r0, r0
	movne	r0, r0, lsr #5
	bne	.L1387
	b	.L1407
.L1386:
	cmp	r3, #255
	bne	.L1407
	ldrb	r0, [sp, #43]	@ zero_extendqisi2
	subs	r0, r0, #255
	movne	r0, #1
	b	.L1387
.L1407:
	mov	r0, #1
.L1387:
	ldr	r3, [sp, #24]
	tst	r3, #4
	beq	.L1388
	ldr	r3, .L1432+12
	mov	r0, r10
	ldr	r2, [sp, #32]
	add	r3, r3, r2
	ldr	r1, [r3, #3160]
	add	r1, r5, r1
	bl	SandiskProgTestBadBlock
.L1388:
	cmp	r0, #0
	beq	.L1389
	mov	r1, r6
	ldr	r2, [sp, #4]
	ldr	r0, .L1432+20
	add	r9, r9, #1
	bl	printk
	ldr	r1, [r4, #-1872]
	mov	ip, #1
	uxth	r9, r9
	ldr	r3, [sp, #12]
	mov	r0, r3, lsr #5
	and	r3, r3, #31
	ldr	r2, [r1, r0, asl #2]
	orr	r3, r2, ip, asl r3
	ldr	r2, [sp, #28]
	str	r3, [r1, r0, asl #2]
	ldrb	r3, [r7, #3148]	@ zero_extendqisi2
	mul	r3, r3, r2
	cmp	r9, r3
	bgt	.L1390
.L1389:
	ldr	r3, [sp, #4]
	add	r3, r3, #1
	str	r3, [sp, #4]
	ldr	r3, [sp, #16]
	add	r5, r5, r3
	b	.L1381
.L1390:
	mov	r2, r9
	ldr	r0, .L1432+24
	mov	r1, r6
	bl	printk
	ldrb	r3, [r7, #3148]	@ zero_extendqisi2
	ldr	r2, [sp, #28]
	mul	r3, r3, r2
	cmp	r9, r3
	blt	.L1392
	ldr	r3, .L1432+4
	mov	r1, #0
	ldr	r0, [r4, #-1872]
	ldrh	r2, [r3, #20]
	mov	r2, r2, asl #9
	bl	ftl_memset
.L1392:
	cmp	r6, #0
	bne	.L1394
	ldr	r3, [r4, #-1780]
	mov	r5, r6
	mov	r9, #1
	uxth	r10, r3
.L1395:
	ldr	r3, .L1432+12
	ldrb	r3, [r3, #1]	@ zero_extendqisi2
	cmp	r3, r10
	bls	.L1429
	mov	r0, r10
	bl	FlashTestBlk
	cmp	r0, #0
	beq	.L1396
	mov	r1, r10
	ldr	r0, .L1432+28
	bl	printk
	ldr	r1, [r4, #-1872]
	mov	r0, r10, lsr #5
	add	r5, r5, #1
	and	r3, r10, #31
	ldr	r2, [r1, r0, asl #2]
	uxth	r5, r5
	orr	r3, r2, r9, asl r3
	str	r3, [r1, r0, asl #2]
.L1396:
	add	r10, r10, #1
	uxth	r10, r10
	b	.L1395
.L1429:
	sub	fp, r8, #1
	sub	r9, r8, #50
	mov	r10, #1
	uxth	fp, fp
.L1398:
	cmp	fp, r9
	ble	.L1430
	mov	r0, fp
	bl	FlashTestBlk
	cmp	r0, #0
	beq	.L1399
	mov	r1, fp
	ldr	r0, .L1432+28
	bl	printk
	ldr	r1, [r4, #-1872]
	mov	r0, fp, lsr #5
	and	r3, fp, #31
	ldr	r2, [r1, r0, asl #2]
	orr	r3, r2, r10, asl r3
	str	r3, [r1, r0, asl #2]
.L1399:
	sub	fp, fp, #1
	uxth	fp, fp
	b	.L1398
.L1430:
	ldr	r3, .L1432+12
	ldr	r2, [r4, #-1780]
	ldrb	r3, [r3, #1]	@ zero_extendqisi2
	rsb	r3, r2, r3
	cmp	r5, r3
	bcc	.L1394
	ldr	r3, .L1432+4
	mov	r1, #0
	ldr	r0, [r4, #-1872]
	ldrh	r2, [r3, #20]
	mov	r2, r2, asl #9
	bl	ftl_memset
.L1394:
	ldrb	r5, [sp, #8]	@ zero_extendqisi2
	sub	r10, r8, #1
	ldr	r9, .L1432+16
	uxth	r10, r10
	mul	r5, r8, r5
	add	r9, r9, r6, asl #1
.L1402:
	mov	r1, r6
	ldr	r0, .L1432+32
	mov	r2, r10
	bl	printk
	ldr	r1, [r4, #-1872]
.L1403:
	mov	r2, r10, lsr #5
	and	r3, r10, #31
	ldr	r2, [r1, r2, asl #2]
	mov	r3, r2, lsr r3
	ands	r3, r3, #1
	subne	r10, r10, #1
	uxthne	r10, r10
	bne	.L1403
.L1431:
	ldr	r1, [sp, #20]
	add	r0, sp, #44
	ldr	r2, .L1432+36
	strh	r10, [r9]	@ movhi
	strh	r10, [r1, #2]	@ movhi
	strh	r2, [r1]	@ movhi
	strh	r3, [r1, #8]	@ movhi
	mov	r1, #1
	ldr	r3, [r4, #-1872]
	mov	r2, r1
	str	r3, [sp, #52]
	ldr	r3, [r4, #-1768]
	str	r3, [sp, #56]
	add	r3, r10, r5
	mov	r3, r3, asl #10
	str	r3, [sp, #48]
	bl	FlashEraseBlocks
	mov	r1, #1
	mov	r3, r1
	mov	r2, r1
	add	r0, sp, #44
	bl	FlashProgPages
	ldr	r3, [sp, #44]
	cmp	r3, #0
	subne	r10, r10, #1
	uxthne	r10, r10
	bne	.L1402
.L1408:
	ldr	r3, [sp, #8]
	add	r3, r3, #1
	str	r3, [sp, #8]
	b	.L1379
.L1428:
	add	sp, sp, #84
	@ sp needed
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L1433:
	.align	2
.L1432:
	.word	.LANCHOR2
	.word	.LANCHOR2-2772
	.word	.LC25
	.word	.LANCHOR0
	.word	.LANCHOR2-1752
	.word	.LC26
	.word	.LC27
	.word	.LC28
	.word	.LC29
	.word	-3872
	.fnend
	.size	FlashMakeFactorBbt, .-FlashMakeFactorBbt
	.align	2
	.global	Ftl_log2
	.type	Ftl_log2, %function
Ftl_log2:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	@ link register save eliminated.
	mov	r1, #0
	mov	r2, #1
.L1435:
	cmp	r2, r0
	uxth	r3, r1
	add	r1, r1, #1
	movls	r2, r2, asl #1
	bls	.L1435
.L1437:
	sub	r0, r3, #1
	uxth	r0, r0
	bx	lr
	.fnend
	.size	Ftl_log2, .-Ftl_log2
	.align	2
	.global	FtlPrintInfo
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
	.type	FtlSysBlkNumInit, %function
FtlSysBlkNumInit:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	@ link register save eliminated.
	ldr	r3, .L1441
	cmp	r0, #23
	sub	r2, r3, #1728
	movls	r0, #24
	sub	ip, r3, #1712
	str	r0, [r3, #-1736]
	ldrh	r2, [r2, #-4]
	ldrh	r1, [ip, #-10]
	mul	r2, r2, r0
	rsb	r0, r0, r1
	ldr	r1, [r3, #-1716]
	strh	r0, [ip, #-12]	@ movhi
	mov	r0, #0
	str	r2, [r3, #-1728]
	rsb	r2, r2, r1
	str	r2, [r3, #-1720]
	bx	lr
.L1442:
	.align	2
.L1441:
	.word	.LANCHOR2
	.fnend
	.size	FtlSysBlkNumInit, .-FtlSysBlkNumInit
	.align	2
	.global	FtlConstantsInit
	.type	FtlConstantsInit, %function
FtlConstantsInit:
	.fnstart
	@ args = 0, pretend = 0, frame = 24
	@ frame_needed = 0, uses_anonymous_args = 0
	stmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.save {r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.pad #28
	sub	sp, sp, #28
	ldr	r9, .L1471
	mov	r5, r0
	ldrh	r7, [r0, #8]
	mov	r8, #0
	ldrh	ip, [r0, #10]
	add	r2, r9, #8
	ldrh	r6, [r0, #12]
	mov	r3, r9
	ldrh	r4, [r0, #14]
	strh	r7, [r3], #16	@ movhi
	strh	ip, [r3, #-14]	@ movhi
	strh	r6, [r3, #-12]	@ movhi
	strh	r4, [r9, #-10]	@ movhi
	str	r3, [sp, #4]
.L1444:
	strb	r8, [r8, r2]
	add	r8, r8, #1
	cmp	r8, #32
	bne	.L1444
	ldrh	r1, [r5, #14]
	ldrh	r2, [r5, #20]
	cmp	r2, r1, lsr #8
	bcs	.L1445
	uxtb	r10, r6
	ldr	r8, .L1471+4
	mov	r2, r10, asl #1
	uxtb	r2, r2
	str	r2, [sp, #12]
	sub	r2, ip, #1
	mul	r2, r6, r2
	str	r2, [sp, #8]
	mov	r2, #0
.L1446:
	cmp	r2, r6
	bcs	.L1448
	ldr	r3, [sp, #8]
	uxtb	r1, r2
	rsb	fp, r6, r2
	add	r0, r2, r3
	add	r3, r8, r0
	mov	r0, #0
	mov	lr, r0
	str	r3, [sp, #16]
	add	r3, r8, fp
	str	r3, [sp, #20]
.L1449:
	cmp	lr, ip
	add	r0, r0, r6
	bcs	.L1470
	ldr	fp, [sp, #20]
	add	lr, lr, #1
	ldr	r3, [sp, #16]
	strb	r1, [fp, r0]
	add	fp, r1, r10
	strb	fp, [r3, r0]
	ldr	r3, [sp, #12]
	add	r1, r1, r3
	uxtb	r1, r1
	b	.L1449
.L1470:
	add	r2, r2, #1
	b	.L1446
.L1448:
	ldr	r3, [sp, #4]
	mov	ip, ip, asl #1
	mov	r4, r4, lsr #1
	strh	r4, [r9, #-10]	@ movhi
	strh	ip, [r3, #-14]	@ movhi
.L1445:
	ldr	r3, [sp, #4]
	cmp	r7, #1
	ldr	r2, .L1471+8
	mov	r1, #5
	ldrh	r4, [r9, #-10]
	ldr	ip, .L1471+12
	strh	r1, [r2, #-8]	@ movhi
	mov	r1, #0
	streqh	r7, [r2, #-8]	@ movhi
	sub	r8, ip, #1664
	ldrh	r7, [r3, #-14]
	sub	r3, ip, #1728
	strh	r1, [r2, #-6]	@ movhi
	mov	r1, #4352
	strh	r1, [r2, #-4]	@ movhi
	smulbb	r7, r7, r6
	ldr	r2, .L1471+16
	ldrh	fp, [r5, #16]
	smulbb	r6, r4, r6
	ldrb	r10, [r2]	@ zero_extendqisi2
	strh	fp, [r8]	@ movhi
	cmp	r10, #0
	uxth	r7, r7
	strh	r7, [r3, #-4]	@ movhi
	ldrne	r2, .L1471+8
	movne	r1, #384
	smulbb	r3, r7, fp
	strh	r6, [r8, #-2]	@ movhi
	strneh	r1, [r2, #-4]	@ movhi
	sub	r6, ip, #1648
	ldrh	r2, [r5, #20]
	ldrh	r1, [r5, #18]
	str	ip, [sp, #12]
	mov	r0, r2
	strh	r3, [r6, #-12]	@ movhi
	strh	r1, [r6, #-14]	@ movhi
	strh	r2, [r6, #-10]	@ movhi
	str	r1, [sp, #8]
	str	r2, [sp, #4]
	bl	Ftl_log2
	cmp	r4, #1024
	ldr	r2, [sp, #4]
	mov	r3, r0
	strh	r0, [r6, #-8]	@ movhi
	ldr	ip, [sp, #12]
	mov	r0, r2, asl #9
	ldr	r1, [sp, #8]
	str	r3, [sp, #4]
	uxth	r0, r0
	strh	r0, [r6, #-6]	@ movhi
	mul	r1, r1, r2
	mov	r0, r0, lsr #8
	strh	r0, [r6, #-4]	@ movhi
	ldrh	r0, [r5, #26]
	mov	r5, ip
	strh	r0, [r6, #-2]	@ movhi
	mul	r0, r4, r7
	str	r0, [ip, #-1716]
	uxtbhi	r0, r4
	strhih	r0, [r8, #-6]	@ movhi
	ldrh	r0, [r8, #-6]
	rsb	r0, r0, r4
	mov	r4, r4, asl #6
	mul	r0, r0, r7
	mul	r0, r2, r0
	mul	fp, fp, r0
	ldrh	r0, [r8, #-4]
	mov	r0, r0, asl #3
	mov	fp, fp, asr #11
	str	fp, [ip, #-1648]
	bl	__aeabi_idiv
	ldr	fp, .L1471+20
	mov	r1, r7
	uxth	r0, r0
	ldr	r3, [sp, #4]
	cmp	r0, #4
	strhih	r0, [fp, #-12]	@ movhi
	add	r0, r3, #9
	movls	r2, #4
	strlsh	r2, [fp, #-12]	@ movhi
	cmp	r10, #0
	mov	r0, r4, asr r0
	strh	r0, [fp, #-8]	@ movhi
	movne	r2, #640
	strneh	r2, [r8, #-4]	@ movhi
	ldrh	r2, [r8, #-4]
	uxth	r0, r0
	add	r4, r0, #8
	ldr	r10, .L1471+12
	mov	r2, r2, asr r3
	mul	r3, r7, r0
	add	r2, r2, #2
	ldrh	r0, [fp, #-12]
	strh	r2, [fp, #-10]	@ movhi
	str	r3, [r5, #-1636]
	bl	__aeabi_uidiv
	cmp	r7, #1
	ldr	r7, .L1471+12
	uxtah	r0, r4, r0
	addeq	r0, r0, #4
	str	r0, [r10, #-1736]
	ldr	r3, [r5, #-1736]
	uxth	r0, r3
	bl	FtlSysBlkNumInit
	ldr	r3, [r5, #-1736]
	ldr	r2, [r5, #-1720]
	mov	r0, #2048
	ldrh	r4, [r6, #-8]
	str	r3, [r5, #-1632]
	ldrh	r3, [r8]
	mov	r2, r2, asl #2
	add	r4, r4, #9
	ldrh	r6, [r6, #-10]
	sub	r8, r7, #1616
	mul	r3, r3, r2
	mov	r1, r6
	mov	r4, r3, lsr r4
	add	r4, r4, #2
	uxth	r4, r4
	strh	r4, [r8, #-12]	@ movhi
	bl	__aeabi_idiv
	mov	r3, #0
	str	r3, [r5, #-2740]
	sub	r1, r7, #1632
	ldrh	r3, [fp, #-12]
	add	r2, r3, #3
	strh	r2, [fp, #-12]	@ movhi
	ldr	fp, [r5, #-1636]
	add	r2, fp, #3
	str	r2, [r5, #-1636]
	strh	r0, [r8, #-10]	@ movhi
	ldrb	r0, [r5, #-2744]	@ zero_extendqisi2
	cmp	r0, #0
	addne	r3, r3, #4
	addne	fp, fp, #5
	strneh	r3, [r1, #-12]	@ movhi
	strne	fp, [r7, #-1636]
	bne	.L1459
.L1458:
	cmp	r2, #7
	movls	r3, #8
	strls	r3, [r7, #-1636]
.L1459:
	ldrh	r2, [r9, #-12]
	mov	r3, #0
	strh	r3, [r8, #-8]	@ movhi
	mov	r0, #0
	mov	r3, r2, lsr #3
	add	r3, r3, r2, asl #1
	add	r3, r3, #52
	add	r4, r3, r4, asl #2
	cmp	r4, r6, asl #9
	ldrcc	r3, .L1471+24
	movcc	r2, #1
	strcch	r2, [r3, #-8]	@ movhi
	add	sp, sp, #28
	@ sp needed
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L1472:
	.align	2
.L1471:
	.word	.LANCHOR2-1712
	.word	.LANCHOR2-1704
	.word	.LANCHOR2-1664
	.word	.LANCHOR2
	.word	.LANCHOR0
	.word	.LANCHOR2-1632
	.word	.LANCHOR2-1616
	.fnend
	.size	FtlConstantsInit, .-FtlConstantsInit
	.align	2
	.global	FtlMemInit
	.type	FtlMemInit, %function
FtlMemInit:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	stmfd	sp!, {r3, r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.save {r3, r4, r5, r6, r7, r8, r9, r10, fp, lr}
	mov	r5, #0
	ldr	r4, .L1578
	movw	r3, #65535
	mov	r0, #1024
	mov	r10, #36
	sub	r2, r4, #1536
	sub	r6, r4, #1520
	sub	r7, r4, #1616
	str	r3, [r4, #-1552]
	strh	r5, [r6, #-4]	@ movhi
	mvn	r3, #0
	strh	r5, [r6, #-2]	@ movhi
	sub	r9, r4, #1728
	strh	r3, [r2]	@ movhi
	strh	r3, [r6, #-14]	@ movhi
	strh	r3, [r6, #-12]	@ movhi
	strh	r3, [r6, #-10]	@ movhi
	mov	r3, #32
	strh	r5, [r6]	@ movhi
	strh	r3, [r6, #-8]	@ movhi
	mov	r3, #128
	strh	r3, [r6, #-6]	@ movhi
	sub	r3, r4, #1504
	sub	r6, r4, #1648
	strh	r5, [r7, #-6]	@ movhi
	strh	r5, [r3, #-14]	@ movhi
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
	ldrh	r1, [r6, #-10]
	bl	__aeabi_idiv
	ldrh	r8, [r9, #-4]
	str	r5, [r4, #-1512]
	mov	r8, r8, asl #2
	cmp	r0, r8
	str	r0, [r4, #-1516]
	ldrh	r0, [r6, #-12]
	strhi	r8, [r4, #-1516]
	mov	r8, #12
	mov	r0, r0, asl #1
	bl	ftl_malloc
	str	r0, [r4, #-1508]
	ldrh	r0, [r6, #-12]
	mul	r0, r8, r0
	bl	ftl_malloc
	ldrh	r5, [r9, #-4]
	mul	r5, r10, r5
	mov	fp, r5, asl #3
	str	r0, [r4, #-1504]
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
	ldrh	r5, [r6, #-6]
	ldrh	r3, [r9, #-4]
	mov	r3, r3, asl #1
	add	r3, r3, #1
	str	r3, [r4, #-1476]
	str	r0, [r4, #-1480]
	mov	r0, r5
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
	ldrh	r3, [r9, #-4]
	ldrh	r5, [r6, #-4]
	mul	r5, r3, r5
	str	r0, [r4, #-1444]
	mov	r0, r5
	bl	ftl_malloc
	str	r0, [r4, #-1440]
	mov	r0, r5, asl #3
	bl	ftl_malloc
	ldrh	r3, [r6, #-4]
	ldr	r5, .L1578+4
	add	r9, r5, #288
	str	r0, [r4, #-1436]
	ldr	r0, [r4, #-1476]
	mul	r0, r0, r3
	bl	ftl_malloc
	ldrh	r3, [r6, #-4]
	str	r0, [r4, #-1432]
	ldr	r0, [r4, #-1516]
	mul	r0, r0, r3
	bl	ftl_malloc
	str	r0, [r4, #-1428]
	ldrh	r0, [r5, #-10]
	mov	r0, r0, asl #1
	uxth	r0, r0
	strh	r0, [r9]	@ movhi
	bl	ftl_malloc
	str	r0, [r4, #-1420]
	ldrh	r0, [r9]
	add	r0, r0, #544
	add	r0, r0, #3
	mov	r0, r0, lsr #9
	strh	r0, [r9]	@ movhi
	mov	r0, r0, asl #9
	bl	ftl_malloc
	ldrh	r9, [r5, #-10]
	mov	r9, r9, asl #1
	str	r0, [r4, #-1416]
	add	r0, r0, #32
	str	r0, [r4, #-1412]
	mov	r0, r9
	bl	ftl_malloc
	str	r0, [r4, #-1408]
	mov	r0, r9
	bl	ftl_malloc
	ldr	r9, [r4, #-1636]
	mov	r9, r9, asl #1
	str	r0, [r4, #-1404]
	mov	r0, r9
	bl	ftl_malloc
	str	r0, [r4, #-1400]
	mov	r0, r9
	bl	ftl_malloc
	str	r0, [r4, #-1396]
	ldrh	r0, [r5, #-10]
	mov	r0, r0, lsr #3
	add	r0, r0, #4
	bl	ftl_malloc
	str	r0, [r4, #-1392]
	ldrh	r0, [r5, #68]
	mov	r0, r0, asl #1
	bl	ftl_malloc
	str	r0, [r4, #-1388]
	ldrh	r0, [r5, #68]
	mov	r0, r0, asl #1
	bl	ftl_malloc
	str	r0, [r4, #-1384]
	ldrh	r0, [r5, #68]
	mov	r0, r0, asl #2
	bl	ftl_malloc
	str	r0, [r4, #-1380]
	ldrh	r0, [r5, #70]
	mov	r0, r0, asl #2
	bl	ftl_malloc
	ldrh	r2, [r5, #70]
	mov	r1, #0
	mov	r2, r2, asl #2
	str	r0, [r4, #-1376]
	bl	ftl_memset
	ldrh	r9, [r7, #-12]
	mov	r9, r9, asl #2
	mov	r0, r9
	bl	ftl_malloc
	str	r0, [r4, #-1372]
	mov	r0, r9
	bl	ftl_malloc
	str	r0, [r4, #-1368]
	ldr	r0, [r4, #-1636]
	mov	r0, r0, asl #2
	bl	ftl_malloc
	str	r0, [r4, #-1364]
	ldrh	r0, [r7, #-10]
	mul	r0, r8, r0
	bl	ftl_malloc
	ldrh	r3, [r7, #-10]
	add	r7, r5, #16
	str	r0, [r4, #-1360]
	ldrh	r0, [r6, #-6]
	add	r6, r5, #368
	mul	r0, r0, r3
	bl	ftl_malloc
	ldrh	r3, [r5, #-10]
	str	r0, [r4, #-1356]
	mov	r0, #6
	mul	r0, r0, r3
	bl	ftl_malloc
	ldrh	r3, [r5, #46]
	add	r3, r3, #31
	mov	r3, r3, asr #5
	strh	r3, [r6, #-4]	@ movhi
	str	r0, [r4, #-1352]
	ldrh	r0, [r5, #2]
	mul	r0, r0, r3
	mov	r0, r0, asl #2
	bl	ftl_malloc
	ldrh	r2, [r6, #-4]
	ldrh	ip, [r5, #2]
	add	r5, r5, #396
	mov	r3, #1
	mov	r2, r2, asl #2
	mov	r1, r2
	str	r0, [r4, #-1316]
.L1475:
	cmp	r3, ip
	bcs	.L1576
	ldr	r0, [r4, #-1316]
	add	r3, r3, #1
	add	r0, r0, r1
	add	r1, r1, r2
	str	r0, [r5, #4]!
	b	.L1475
.L1576:
	mov	r2, #0
.L1477:
	cmp	r3, #8
	addne	r1, r6, r3, asl #2
	addne	r3, r3, #1
	strne	r2, [r1, #28]
	bne	.L1477
.L1577:
	ldr	r2, [r4, #-1400]
	ldr	r3, .L1578
	cmp	r2, #0
	bne	.L1479
.L1481:
	ldr	r1, .L1578+8
	ldr	r0, .L1578+12
	bl	printk
	mvn	r0, #0
	ldmfd	sp!, {r3, r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L1479:
	ldr	r2, [r3, #-1396]
	cmp	r2, #0
	beq	.L1481
	ldr	r2, [r3, #-1372]
	cmp	r2, #0
	beq	.L1481
	ldr	r2, [r3, #-1364]
	cmp	r2, #0
	beq	.L1481
	ldr	r2, [r3, #-1360]
	cmp	r2, #0
	beq	.L1481
	ldr	r2, [r3, #-1356]
	cmp	r2, #0
	beq	.L1481
	ldr	r2, [r3, #-1352]
	cmp	r2, #0
	beq	.L1481
	ldr	r2, [r3, #-1316]
	cmp	r2, #0
	beq	.L1481
	ldr	r3, [r3, #-1404]
	cmp	r3, #0
	beq	.L1481
	ldr	r2, [r4, #-1508]
	ldr	r3, .L1578
	cmp	r2, #0
	beq	.L1481
	ldr	r2, [r3, #-1504]
	cmp	r2, #0
	beq	.L1481
	ldr	r2, [r3, #-1500]
	cmp	r2, #0
	beq	.L1481
	ldr	r2, [r3, #-1492]
	cmp	r2, #0
	beq	.L1481
	ldr	r2, [r3, #-1488]
	cmp	r2, #0
	beq	.L1481
	ldr	r2, [r3, #-1484]
	cmp	r2, #0
	beq	.L1481
	ldr	r2, [r3, #-1496]
	cmp	r2, #0
	beq	.L1481
	ldr	r2, [r3, #-1472]
	cmp	r2, #0
	beq	.L1481
	ldr	r2, [r3, #-1468]
	cmp	r2, #0
	beq	.L1481
	ldr	r3, [r3, #-1464]
	cmp	r3, #0
	beq	.L1481
	ldr	r2, [r4, #-1460]
	ldr	r3, .L1578
	cmp	r2, #0
	beq	.L1481
	ldr	r2, [r3, #-1452]
	cmp	r2, #0
	beq	.L1481
	ldr	r2, [r3, #-1448]
	cmp	r2, #0
	beq	.L1481
	ldr	r2, [r3, #-1444]
	cmp	r2, #0
	beq	.L1481
	ldr	r2, [r3, #-1440]
	cmp	r2, #0
	beq	.L1481
	ldr	r2, [r3, #-1436]
	cmp	r2, #0
	beq	.L1481
	ldr	r2, [r3, #-1432]
	cmp	r2, #0
	beq	.L1481
	ldr	r2, [r3, #-1412]
	cmp	r2, #0
	beq	.L1481
	ldr	r2, [r3, #-1420]
	cmp	r2, #0
	beq	.L1481
	ldr	r3, [r3, #-1388]
	cmp	r3, #0
	beq	.L1481
	ldr	r3, .L1578
	ldr	r2, [r3, #-1384]
	cmp	r2, #0
	beq	.L1481
	ldr	r2, [r3, #-1380]
	cmp	r2, #0
	beq	.L1481
	ldr	r3, [r3, #-1376]
	cmp	r3, #0
	beq	.L1481
	mov	r0, #0
	ldmfd	sp!, {r3, r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L1579:
	.align	2
.L1578:
	.word	.LANCHOR2
	.word	.LANCHOR2-1712
	.word	.LANCHOR3+4
	.word	.LC30
	.fnend
	.size	FtlMemInit, .-FtlMemInit
	.align	2
	.global	IsBlkInVendorPart
	.type	IsBlkInVendorPart, %function
IsBlkInVendorPart:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	@ link register save eliminated.
	ldr	r2, .L1587
	sub	r3, r2, #1280
	ldrh	r3, [r3, #-4]
	cmp	r3, #0
	beq	.L1586
	ldr	r3, [r2, #-1388]
	sub	r2, r2, #1632
	ldrh	r2, [r2, #-12]
	add	r2, r3, r2, asl #1
.L1582:
	cmp	r3, r2
	beq	.L1586
	ldrh	r1, [r3], #2
	cmp	r1, r0
	bne	.L1582
	mov	r0, #1
	bx	lr
.L1586:
	mov	r0, #0
	bx	lr
.L1588:
	.align	2
.L1587:
	.word	.LANCHOR2
	.fnend
	.size	IsBlkInVendorPart, .-IsBlkInVendorPart
	.align	2
	.global	FtlCacheMetchLpa
	.type	FtlCacheMetchLpa, %function
FtlCacheMetchLpa:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	ldr	r2, .L1596
	ldr	r3, [r2, #-1512]
	cmp	r3, #0
	beq	.L1592
	stmfd	sp!, {r4, r5, lr}
	.save {r4, r5, lr}
	mov	r5, #36
	ldr	r4, [r2, #-1480]
	mov	r2, #0
.L1591:
	mla	ip, r5, r2, r4
	ldr	lr, [ip, #16]
	cmp	lr, r0
	movcc	ip, #0
	movcs	ip, #1
	cmp	lr, r1
	movhi	ip, #0
	cmp	ip, #0
	bne	.L1593
	add	r2, r2, #1
	cmp	r2, r3
	bne	.L1591
	mov	r0, ip
	ldmfd	sp!, {r4, r5, pc}
.L1592:
	mov	r0, r3
	bx	lr
.L1593:
	mov	r0, #1
	ldmfd	sp!, {r4, r5, pc}
.L1597:
	.align	2
.L1596:
	.word	.LANCHOR2
	.fnend
	.size	FtlCacheMetchLpa, .-FtlCacheMetchLpa
	.align	2
	.global	FtlGetCap
	.type	FtlGetCap, %function
FtlGetCap:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	@ link register save eliminated.
	ldr	r3, .L1599
	ldr	r0, [r3, #-2740]
	bx	lr
.L1600:
	.align	2
.L1599:
	.word	.LANCHOR2
	.fnend
	.size	FtlGetCap, .-FtlGetCap
	.align	2
	.global	ftl_get_density
	.type	ftl_get_density, %function
ftl_get_density:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	@ link register save eliminated.
	ldr	r3, .L1602
	ldr	r0, [r3, #-2740]
	bx	lr
.L1603:
	.align	2
.L1602:
	.word	.LANCHOR2
	.fnend
	.size	ftl_get_density, .-ftl_get_density
	.align	2
	.global	FtlGetLpn
	.type	FtlGetLpn, %function
FtlGetLpn:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	@ link register save eliminated.
	ldr	r3, .L1605
	ldr	r0, [r3, #-1280]
	bx	lr
.L1606:
	.align	2
.L1605:
	.word	.LANCHOR2
	.fnend
	.size	FtlGetLpn, .-FtlGetLpn
	.align	2
	.global	FtlBbmMapBadBlock
	.type	FtlBbmMapBadBlock, %function
FtlBbmMapBadBlock:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	stmfd	sp!, {r0, r1, r4, r5, r6, lr}
	.save {r4, r5, r6, lr}
	.pad #8
	mov	r6, r0
	ldr	r5, .L1609
	sub	r3, r5, #1664
	ldrh	r4, [r3, #-2]
	mov	r1, r4
	bl	__aeabi_uidiv
	uxth	r2, r0
	smulbb	r3, r2, r4
	add	r1, r5, r2, asl #2
	mov	r4, #1
	sub	r5, r5, #1344
	ldr	ip, [r1, #-1316]
	rsb	r3, r3, r6
	uxth	r3, r3
	and	r1, r3, #31
	mov	lr, r3, lsr #5
	ldr	r0, [ip, lr, asl #2]
	orr	r1, r0, r4, asl r1
	ldr	r0, .L1609+4
	str	r1, [ip, lr, asl #2]
	str	r1, [sp]
	mov	r1, r6
	bl	printk
	ldrh	r3, [r5, #6]
	mov	r0, #0
	add	r3, r3, r4
	strh	r3, [r5, #6]	@ movhi
	add	sp, sp, #8
	@ sp needed
	ldmfd	sp!, {r4, r5, r6, pc}
.L1610:
	.align	2
.L1609:
	.word	.LANCHOR2
	.word	.LC31
	.fnend
	.size	FtlBbmMapBadBlock, .-FtlBbmMapBadBlock
	.global	__aeabi_uidivmod
	.align	2
	.global	FtlBbmIsBadBlock
	.type	FtlBbmIsBadBlock, %function
FtlBbmIsBadBlock:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	stmfd	sp!, {r3, r4, r5, r6, r7, lr}
	.save {r3, r4, r5, r6, r7, lr}
	mov	r7, r0
	ldr	r5, .L1613
	sub	r3, r5, #1664
	ldrh	r6, [r3, #-2]
	mov	r1, r6
	bl	__aeabi_uidivmod
	mov	r0, r7
	uxth	r4, r1
	mov	r1, r6
	bl	__aeabi_uidiv
	mov	r2, r4, lsr #5
	and	r4, r4, #31
	uxth	r0, r0
	add	r5, r5, r0, asl #2
	ldr	r3, [r5, #-1316]
	ldr	r0, [r3, r2, asl #2]
	mov	r0, r0, lsr r4
	and	r0, r0, #1
	ldmfd	sp!, {r3, r4, r5, r6, r7, pc}
.L1614:
	.align	2
.L1613:
	.word	.LANCHOR2
	.fnend
	.size	FtlBbmIsBadBlock, .-FtlBbmIsBadBlock
	.align	2
	.global	FtlBbtInfoPrint
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
	.type	FtlBbt2Bitmap, %function
FtlBbt2Bitmap:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	stmfd	sp!, {r4, r5, r6, lr}
	.save {r4, r5, r6, lr}
	mov	r4, r0
	ldr	r5, .L1622
	mov	r0, r1
	mov	r6, r1
	mov	r1, #0
	ldrh	r2, [r5, #-4]
	mov	r2, r2, asl #2
	bl	ftl_memset
	add	r3, r4, #1020
	add	r3, r3, #2
	sub	r1, r4, #2
	mov	lr, #1
	movw	r4, #65535
.L1618:
	ldrh	r2, [r1, #2]!
	cmp	r2, r4
	ldmeqfd	sp!, {r4, r5, r6, pc}
	mov	ip, r2, lsr #5
	and	r2, r2, #31
	cmp	r1, r3
	ldr	r0, [r6, ip, asl #2]
	orr	r2, r0, lr, asl r2
	str	r2, [r6, ip, asl #2]
	ldrh	r2, [r5, #6]
	add	r2, r2, #1
	strh	r2, [r5, #6]	@ movhi
	bne	.L1618
	ldmfd	sp!, {r4, r5, r6, pc}
.L1623:
	.align	2
.L1622:
	.word	.LANCHOR2-1344
	.fnend
	.size	FtlBbt2Bitmap, .-FtlBbt2Bitmap
	.align	2
	.global	FtlBbmTblFlush
	.type	FtlBbmTblFlush, %function
FtlBbmTblFlush:
	.fnstart
	@ args = 0, pretend = 0, frame = 8
	@ frame_needed = 0, uses_anonymous_args = 0
	stmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.save {r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.pad #20
	sub	sp, sp, #20
	ldr	r4, .L1642
	ldr	r5, [r4, #-1276]
	cmp	r5, #0
	bne	.L1626
	ldr	r3, [r4, #-1440]
	mov	r1, r5
	ldr	r0, [r4, #-1472]
	sub	r7, r4, #1344
	ldr	r8, .L1642+4
	sub	fp, r7, #4
	str	r3, [r4, #-1260]
	sub	r3, r4, #1648
	str	r0, [r4, #-1264]
	mov	r10, r4
	ldrh	r2, [r3, #-6]
	bl	ftl_memset
	ldr	r9, .L1642+8
.L1627:
	ldrh	r3, [r9]
	ldr	r6, .L1642
	cmp	r5, r3
	bge	.L1641
	ldrh	r2, [fp]
	ldr	r3, [r10, #-1264]
	ldr	r1, [r8, #4]!
	mul	r0, r2, r5
	mov	r2, r2, asl #2
	add	r5, r5, #1
	add	r0, r3, r0, asl #2
	bl	ftl_memcpy
	b	.L1627
.L1641:
	ldr	r5, [r6, #-1260]
	mov	r1, #255
	mov	r2, #16
	ldr	r10, .L1642+12
	mov	r8, #0
	mov	r0, r5
	mov	r9, r8
	bl	ftl_memset
	ldr	r3, .L1642+16
	strh	r3, [r5]	@ movhi
	ldr	r3, [r6, #-1336]
	str	r3, [r5, #4]
	ldrh	r3, [r7]
	strh	r3, [r5, #2]	@ movhi
	ldrh	r3, [r7, #4]
	strh	r3, [r5, #8]	@ movhi
	ldrh	r3, [r7, #6]
	strh	r3, [r5, #10]	@ movhi
	ldr	r3, [r6, #-1736]
	strh	r3, [r5, #12]	@ movhi
.L1629:
	ldr	r3, [r4, #-1472]
	mov	ip, #0
	ldrh	r1, [r7]
	ldrh	r2, [r7, #2]
	str	r3, [r4, #-1264]
	ldr	r3, [r4, #-1440]
	str	ip, [r4, #-1272]
	str	ip, [sp, #12]
	str	r3, [r4, #-1260]
	orr	r3, r2, r1, asl #10
	ldrh	r0, [r5, #10]
	str	r3, [r4, #-1268]
	ldrh	r3, [r7, #4]
	str	r0, [sp]
	ldr	r0, .L1642+20
	bl	printk
	ldrh	r3, [r10]
	ldrh	r2, [r7, #2]
	sub	r3, r3, #1
	ldr	fp, .L1642+24
	cmp	r2, r3
	ldr	ip, [sp, #12]
	blt	.L1630
	ldr	r3, [r6, #-1336]
	mov	r1, #1
	ldrh	r2, [fp]
	add	r3, r3, #1
	ldr	r0, [r6, #-1488]
	str	r3, [r6, #-1336]
	str	r3, [r5, #4]
	ldrh	r3, [fp, #4]
	strh	r2, [r5, #8]	@ movhi
	strh	r2, [fp, #4]	@ movhi
	mov	r2, r1
	strh	r3, [fp]	@ movhi
	mov	r3, r3, asl #10
	str	r3, [r6, #-1268]
	str	r3, [r0, #4]
	strh	ip, [fp, #2]	@ movhi
	bl	FlashEraseBlocks
.L1630:
	mov	r1, #1
	ldr	r0, .L1642+28
	mov	r3, r1
	mov	r2, r1
	bl	FlashProgPages
	ldrh	r3, [r7, #2]
	ldr	fp, .L1642
	add	r3, r3, #1
	strh	r3, [r7, #2]	@ movhi
	ldr	r3, [r4, #-1272]
	cmn	r3, #1
	bne	.L1631
	add	r8, r8, #1
	ldr	r0, .L1642+32
	ldr	r1, [r6, #-1268]
	uxth	r8, r8
	bl	printk
	cmp	r8, #3
	bls	.L1629
	ldr	r0, .L1642+36
	mov	r2, r8
	ldr	r1, [fp, #-1268]
	bl	printk
	mov	r3, #1
	str	r3, [fp, #-1276]
	b	.L1626
.L1631:
	add	r9, r9, #1
	cmp	r9, #1
	beq	.L1629
	cmp	r3, #256
	beq	.L1629
.L1626:
	mov	r0, #0
	add	sp, sp, #20
	@ sp needed
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L1643:
	.align	2
.L1642:
	.word	.LANCHOR2
	.word	.LANCHOR2-1320
	.word	.LANCHOR2-1710
	.word	.LANCHOR2-1662
	.word	-3887
	.word	.LC32
	.word	.LANCHOR2-1344
	.word	.LANCHOR2-1272
	.word	.LC33
	.word	.LC34
	.fnend
	.size	FtlBbmTblFlush, .-FtlBbmTblFlush
	.align	2
	.global	FtlLoadFactoryBbt
	.type	FtlLoadFactoryBbt, %function
FtlLoadFactoryBbt:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	ldr	r2, .L1656
	stmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, lr}
	.save {r4, r5, r6, r7, r8, r9, r10, lr}
	mov	r5, #0
	ldr	r6, .L1656+4
	mov	r9, r2
	ldr	r3, [r2, #-1472]
	ldr	r7, [r2, #-1440]
	sub	r8, r6, #376
	str	r3, [r2, #-1264]
	str	r7, [r2, #-1260]
.L1645:
	ldrh	r3, [r8]
	cmp	r5, r3
	bcs	.L1655
	ldr	r2, .L1656+8
	mvn	r3, #0
	strh	r3, [r6, #2]!	@ movhi
	ldrh	r3, [r2]
	mov	r10, r2
	sub	r3, r3, #1
	uxth	r4, r3
.L1646:
	ldrh	r3, [r10]
	sub	r2, r3, #16
	cmp	r4, r2
	ble	.L1648
	mla	r3, r3, r5, r4
	mov	r1, #1
	ldr	r0, .L1656+12
	mov	r2, r1
	mov	r3, r3, asl #10
	str	r3, [r9, #-1268]
	bl	FlashReadPages
	ldr	r3, [r9, #-1272]
	cmn	r3, #1
	beq	.L1647
	ldrh	r2, [r7]
	movw	r3, #61664
	cmp	r2, r3
	streqh	r4, [r6]	@ movhi
	beq	.L1648
.L1647:
	sub	r4, r4, #1
	uxth	r4, r4
	b	.L1646
.L1648:
	add	r5, r5, #1
	b	.L1645
.L1655:
	mov	r0, #0
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, pc}
.L1657:
	.align	2
.L1656:
	.word	.LANCHOR2
	.word	.LANCHOR2-1334
	.word	.LANCHOR2-1666
	.word	.LANCHOR2-1272
	.fnend
	.size	FtlLoadFactoryBbt, .-FtlLoadFactoryBbt
	.align	2
	.global	FtlBbtMemInit
	.type	FtlBbtMemInit, %function
FtlBbtMemInit:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	@ link register save eliminated.
	ldr	r0, .L1659
	mvn	r3, #0
	mov	r1, #255
	mov	r2, #16
	add	r0, r0, #12
	strh	r3, [r0, #-12]	@ movhi
	mov	r3, #0
	strh	r3, [r0, #-6]	@ movhi
	b	ftl_memset
.L1660:
	.align	2
.L1659:
	.word	.LANCHOR2-1344
	.fnend
	.size	FtlBbtMemInit, .-FtlBbtMemInit
	.align	2
	.global	FtlBbtCalcTotleCnt
	.type	FtlBbtCalcTotleCnt, %function
FtlBbtCalcTotleCnt:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	stmfd	sp!, {r4, r5, r6, lr}
	.save {r4, r5, r6, lr}
	mov	r5, #0
	ldr	r4, .L1670
	ldrh	r6, [r4, #-2]
	ldrh	r3, [r4, #-46]
	mov	r4, r5
	mul	r6, r3, r6
.L1662:
	uxth	r0, r5
	cmp	r0, r6
	bge	.L1669
	bl	FtlBbmIsBadBlock
	add	r5, r5, #1
	cmp	r0, #0
	addne	r4, r4, #1
	uxthne	r4, r4
	b	.L1662
.L1669:
	mov	r0, r4
	ldmfd	sp!, {r4, r5, r6, pc}
.L1671:
	.align	2
.L1670:
	.word	.LANCHOR2-1664
	.fnend
	.size	FtlBbtCalcTotleCnt, .-FtlBbtCalcTotleCnt
	.align	2
	.global	FtlMakeBbt
	.type	FtlMakeBbt, %function
FtlMakeBbt:
	.fnstart
	@ args = 0, pretend = 0, frame = 8
	@ frame_needed = 0, uses_anonymous_args = 0
	stmfd	sp!, {r0, r1, r2, r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.save {r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.pad #12
	ldr	r7, .L1697
	ldr	r6, [r7, #-1276]
	cmp	r6, #0
	bne	.L1673
	bl	FtlBbtMemInit
	ldr	r8, .L1697+4
	bl	FtlLoadFactoryBbt
	sub	r4, r7, #1344
	sub	r9, r8, #18
	mov	r5, r7
.L1674:
	ldr	r3, .L1697+8
	ldrh	r2, [r3]
	cmp	r6, r2
	bcs	.L1694
	ldrh	r2, [r9, #2]!
	movw	r1, #65535
	ldr	r0, [r5, #-1472]
	ldr	r10, [r5, #-1440]
	cmp	r2, r1
	str	r0, [r5, #-1264]
	str	r10, [r5, #-1260]
	beq	.L1675
	ldrh	fp, [r3, #44]
	mov	r1, #1
	ldr	r0, .L1697+12
	str	r3, [sp, #4]
	mla	fp, fp, r6, r2
	mov	r2, fp, asl #10
	str	r2, [r5, #-1268]
	mov	r2, r1
	bl	FlashReadPages
	ldr	r0, [r8]
	ldr	r1, [r5, #-1264]
	ldr	r3, [sp, #4]
	ldrh	r2, [r3, #44]
	add	r2, r2, #7
	mov	r2, r2, asr #3
	bl	ftl_memcpy
	b	.L1676
.L1675:
	mov	r1, r6
	str	r3, [sp, #4]
	bl	FlashGetBadBlockList
	ldr	r0, [r5, #-1264]
	ldr	r1, [r8]
	bl	FtlBbt2Bitmap
	ldr	r3, [sp, #4]
	ldrh	r3, [r3, #44]
.L1693:
	sub	r3, r3, #1
	uxth	r3, r3
	str	r3, [sp, #4]
.L1677:
	ldr	fp, .L1697+16
	ldr	r3, [sp, #4]
	ldrh	r0, [fp]
	smlabb	r0, r0, r6, r3
	uxth	r0, r0
	bl	FtlBbmIsBadBlock
	cmp	r0, #1
	ldreq	r3, [sp, #4]
	beq	.L1693
.L1695:
	ldrh	r3, [sp, #4]
	mov	r1, #0
	mov	r2, #16
	ldr	r0, [r5, #-1440]
	strh	r3, [r9]	@ movhi
	bl	ftl_memset
	ldr	r3, .L1697+20
	strh	r3, [r10]	@ movhi
	mov	r3, #0
	str	r3, [r10, #4]
	ldrh	r3, [r9]
	ldrh	fp, [fp]
	strh	r3, [r10, #2]	@ movhi
	ldrh	r3, [r9]
	ldr	r1, [r8]
	ldr	r0, [r5, #-1264]
	mla	fp, fp, r6, r3
	mov	r3, fp, asl #10
	str	r3, [r5, #-1268]
	ldr	r3, .L1697+24
	ldrh	r2, [r3]
	mov	r2, r2, asl #2
	bl	ftl_memcpy
	mov	r1, #1
	mov	r2, r1
	ldr	r0, .L1697+12
	bl	FlashEraseBlocks
	mov	r1, #1
	mov	r3, r1
	ldr	r0, .L1697+12
	mov	r2, r1
	bl	FlashProgPages
	ldr	r3, [r5, #-1272]
	cmn	r3, #1
	bne	.L1676
	uxth	r0, fp
	bl	FtlBbmMapBadBlock
	b	.L1677
.L1676:
	uxth	r0, fp
	add	r6, r6, #1
	bl	FtlBbmMapBadBlock
	add	r8, r8, #4
	b	.L1674
.L1694:
	add	r5, r3, #60
	mov	r6, #0
.L1681:
	ldrh	r3, [r5]
	uxth	r0, r6
	add	r6, r6, #1
	cmp	r3, r0
	bls	.L1696
	bl	FtlBbmMapBadBlock
	b	.L1681
.L1696:
	ldrh	r5, [r4, #12]
	movw	r8, #65535
	ldr	r6, .L1697+28
	sub	r5, r5, #1
	uxth	r5, r5
.L1683:
	ldrh	r3, [r4, #12]
	sub	r3, r3, #48
	cmp	r5, r3
	ble	.L1687
	mov	r0, r5
	bl	FtlBbmIsBadBlock
	cmp	r0, #1
	beq	.L1684
	mov	r0, r5
	bl	FlashTestBlk
	cmp	r0, #0
	beq	.L1685
	mov	r0, r5
	bl	FtlBbmMapBadBlock
	b	.L1684
.L1685:
	ldrh	r3, [r6]
	cmp	r3, r8
	streqh	r5, [r6]	@ movhi
.L1686:
	ldrne	r3, .L1697+28
	strneh	r5, [r3, #4]	@ movhi
	bne	.L1687
.L1684:
	sub	r5, r5, #1
	uxth	r5, r5
	b	.L1683
.L1687:
	ldr	r0, [r7, #-1488]
	mov	r5, #0
	ldrh	r3, [r4]
	mov	r2, #2
	str	r5, [r7, #-1336]
	mov	r1, #1
	strh	r5, [r4, #2]	@ movhi
	mov	r3, r3, asl #10
	str	r3, [r0, #4]
	ldrh	r3, [r4, #4]
	mov	r3, r3, asl #10
	str	r3, [r0, #40]
	bl	FlashEraseBlocks
	ldrh	r0, [r4]
	bl	FtlBbmMapBadBlock
	ldrh	r0, [r4, #4]
	bl	FtlBbmMapBadBlock
	bl	FtlBbmTblFlush
	ldr	r3, [r7, #-1336]
	ldrh	r2, [r4, #4]
	add	r3, r3, #1
	str	r3, [r7, #-1336]
	ldrh	r3, [r4]
	strh	r5, [r4, #2]	@ movhi
	strh	r2, [r4]	@ movhi
	strh	r3, [r4, #4]	@ movhi
	bl	FtlBbmTblFlush
.L1673:
	mov	r0, #0
	add	sp, sp, #12
	@ sp needed
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L1698:
	.align	2
.L1697:
	.word	.LANCHOR2
	.word	.LANCHOR2-1316
	.word	.LANCHOR2-1710
	.word	.LANCHOR2-1272
	.word	.LANCHOR2-1666
	.word	-3872
	.word	.LANCHOR2-1348
	.word	.LANCHOR2-1344
	.fnend
	.size	FtlMakeBbt, .-FtlMakeBbt
	.align	2
	.global	V2P_block
	.type	V2P_block, %function
V2P_block:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	stmfd	sp!, {r3, r4, r5, r6, r7, lr}
	.save {r3, r4, r5, r6, r7, lr}
	mov	r5, r1
	ldr	r4, .L1701
	mov	r7, r0
	sub	r3, r4, #1696
	sub	r4, r4, #1664
	ldrh	r6, [r3, #-12]
	mov	r1, r6
	bl	__aeabi_uidivmod
	mov	r0, r7
	smlabb	r5, r5, r6, r1
	mov	r1, r6
	bl	__aeabi_uidiv
	ldrh	r3, [r4, #-2]
	smlabb	r0, r3, r0, r5
	uxth	r0, r0
	ldmfd	sp!, {r3, r4, r5, r6, r7, pc}
.L1702:
	.align	2
.L1701:
	.word	.LANCHOR2
	.fnend
	.size	V2P_block, .-V2P_block
	.align	2
	.global	P2V_plane
	.type	P2V_plane, %function
P2V_plane:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	ldr	r3, .L1705
	stmfd	sp!, {r4, r5, r6, lr}
	.save {r4, r5, r6, lr}
	sub	r2, r3, #1696
	sub	r3, r3, #1664
	mov	r6, r0
	ldrh	r5, [r2, #-12]
	ldrh	r1, [r3, #-2]
	bl	__aeabi_uidiv
	mov	r1, r5
	smulbb	r4, r0, r5
	mov	r0, r6
	bl	__aeabi_uidivmod
	add	r1, r4, r1
	uxth	r0, r1
	ldmfd	sp!, {r4, r5, r6, pc}
.L1706:
	.align	2
.L1705:
	.word	.LANCHOR2
	.fnend
	.size	P2V_plane, .-P2V_plane
	.align	2
	.global	P2V_block_in_plane
	.type	P2V_block_in_plane, %function
P2V_block_in_plane:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	stmfd	sp!, {r4, lr}
	.save {r4, lr}
	ldr	r4, .L1709
	sub	r3, r4, #1664
	sub	r4, r4, #1696
	ldrh	r1, [r3, #-2]
	bl	__aeabi_uidivmod
	uxth	r0, r1
	ldrh	r1, [r4, #-12]
	bl	__aeabi_uidiv
	uxth	r0, r0
	ldmfd	sp!, {r4, pc}
.L1710:
	.align	2
.L1709:
	.word	.LANCHOR2
	.fnend
	.size	P2V_block_in_plane, .-P2V_block_in_plane
	.align	2
	.global	ftl_cmp_data_ver
	.type	ftl_cmp_data_ver, %function
ftl_cmp_data_ver:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	@ link register save eliminated.
	cmp	r0, r1
	bls	.L1712
	rsb	r0, r1, r0
	cmp	r0, #-2147483648
	movhi	r0, #0
	movls	r0, #1
	bx	lr
.L1712:
	rsb	r0, r0, r1
	cmp	r0, #-2147483648
	movls	r0, #0
	movhi	r0, #1
	bx	lr
	.fnend
	.size	ftl_cmp_data_ver, .-ftl_cmp_data_ver
	.align	2
	.global	FtlGetLastWrittenPage
	.type	FtlGetLastWrittenPage, %function
FtlGetLastWrittenPage:
	.fnstart
	@ args = 0, pretend = 0, frame = 104
	@ frame_needed = 0, uses_anonymous_args = 0
	cmp	r1, #1
	stmfd	sp!, {r4, r5, r6, r7, r8, lr}
	.save {r4, r5, r6, r7, r8, lr}
	.pad #104
	sub	sp, sp, #104
	ldreq	r3, .L1726
	mov	r8, r1
	ldrne	r3, .L1726+4
	mov	r6, r0, asl #10
	mov	r1, #1
	add	r0, sp, #4
	ldreqh	r4, [r3, #-14]
	mov	r2, r8
	ldrneh	r4, [r3]
	add	r3, sp, #40
	str	r3, [sp, #16]
	mov	r7, #0
	sub	r5, r4, #1
	str	r7, [sp, #12]
	uxth	r5, r5
	sxth	r3, r5
	orr	r3, r3, r6
	str	r3, [sp, #8]
	bl	FlashReadPages
	ldr	r3, [sp, #40]
	cmn	r3, #1
	bne	.L1717
.L1718:
	sxth	r4, r7
	sxth	r3, r5
	cmp	r4, r3
	bgt	.L1717
	add	r4, r4, r3
	add	r0, sp, #4
	mov	r1, #1
	mov	r2, r8
	add	r4, r4, r4, lsr #31
	mov	r4, r4, asr #1
	sxth	r3, r4
	orr	r3, r3, r6
	str	r3, [sp, #8]
	bl	FlashReadPages
	ldr	r3, [sp, #40]
	cmn	r3, #1
	bne	.L1719
	ldr	r3, [sp, #44]
	cmn	r3, #1
	bne	.L1719
	ldr	r3, [sp, #4]
	cmn	r3, #1
	subne	r4, r4, #1
	uxthne	r5, r4
	bne	.L1718
.L1719:
	add	r3, r4, #1
	uxth	r7, r3
	b	.L1718
.L1717:
	sxth	r0, r5
	add	sp, sp, #104
	@ sp needed
	ldmfd	sp!, {r4, r5, r6, r7, r8, pc}
.L1727:
	.align	2
.L1726:
	.word	.LANCHOR2-1648
	.word	.LANCHOR2-1664
	.fnend
	.size	FtlGetLastWrittenPage, .-FtlGetLastWrittenPage
	.align	2
	.global	FtlLoadBbt
	.type	FtlLoadBbt, %function
FtlLoadBbt:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	stmfd	sp!, {r4, r5, r6, r7, r8, lr}
	.save {r4, r5, r6, r7, r8, lr}
	ldr	r4, .L1761
	ldr	r3, [r4, #-1472]
	mov	r7, r4
	ldr	r6, [r4, #-1440]
	str	r3, [r4, #-1264]
	str	r6, [r4, #-1260]
	bl	FtlBbtMemInit
	sub	r3, r4, #1664
	sub	r8, r3, #2
	ldrh	r5, [r3, #-2]
	sub	r5, r5, #1
	uxth	r5, r5
.L1729:
	ldrh	r3, [r8]
	sub	r3, r3, #48
	cmp	r5, r3
	ble	.L1732
	mov	r1, #1
	ldr	r0, .L1761+4
	mov	r2, r1
	mov	r3, r5, asl #10
	str	r3, [r7, #-1268]
	bl	FlashReadPages
	ldr	r3, [r7, #-1272]
	cmn	r3, #1
	bne	.L1730
	ldr	r3, [r7, #-1268]
	mov	r1, #1
	ldr	r0, .L1761+4
	mov	r2, r1
	add	r3, r3, #1
	str	r3, [r7, #-1268]
	bl	FlashReadPages
.L1730:
	ldr	r3, [r4, #-1272]
	ldr	r2, .L1761
	cmn	r3, #1
	beq	.L1731
	ldrh	r1, [r6]
	movw	r3, #61649
	cmp	r1, r3
	bne	.L1731
	ldr	r1, [r6, #4]
	sub	r3, r2, #1344
	strh	r5, [r3]	@ movhi
	str	r1, [r2, #-1336]
	ldrh	r2, [r6, #8]
	strh	r2, [r3, #4]	@ movhi
	b	.L1732
.L1731:
	sub	r5, r5, #1
	uxth	r5, r5
	b	.L1729
.L1732:
	ldr	r7, .L1761
	movw	r2, #65535
	sub	r5, r7, #1344
	ldrh	r3, [r5]
	cmp	r3, r2
	beq	.L1746
	ldrh	r3, [r5, #4]
	cmp	r3, r2
	beq	.L1736
	mov	r1, #1
	add	r0, r5, #72
	mov	r2, r1
	mov	r3, r3, asl #10
	str	r3, [r7, #-1268]
	bl	FlashReadPages
	ldr	r3, [r7, #-1272]
	cmn	r3, #1
	beq	.L1736
	ldrh	r2, [r6]
	movw	r3, #61649
	cmp	r2, r3
	bne	.L1736
	ldr	r3, [r6, #4]
	ldr	r2, [r7, #-1336]
	cmp	r3, r2
	strhi	r3, [r7, #-1336]
	ldrhih	r2, [r5, #4]
	ldrhih	r3, [r6, #8]
	strhih	r2, [r5]	@ movhi
	strhih	r3, [r5, #4]	@ movhi
.L1736:
	ldrh	r0, [r5]
	mov	r1, #1
	bl	FtlGetLastWrittenPage
	movw	r8, #61649
	uxth	r7, r0
	add	r0, r0, #1
	strh	r0, [r5, #2]	@ movhi
.L1738:
	sxth	r3, r7
	cmp	r3, #0
	blt	.L1743
	ldrh	r2, [r5]
	mov	r1, #1
	ldr	r0, .L1761+4
	orr	r3, r3, r2, asl #10
	str	r3, [r4, #-1268]
	ldr	r3, [r4, #-1472]
	mov	r2, r1
	str	r3, [r4, #-1264]
	bl	FlashReadPages
	ldr	r3, [r4, #-1272]
	cmn	r3, #1
	beq	.L1739
	ldrh	r3, [r6]
	cmp	r3, r8
	bne	.L1739
.L1743:
	ldrh	r2, [r6, #10]
	ldrh	r0, [r6, #12]
	ldr	r3, .L1761
	strh	r2, [r5, #6]	@ movhi
	movw	r2, #65535
	cmp	r0, r2
	bne	.L1740
	b	.L1741
.L1739:
	sub	r7, r7, #1
	uxth	r7, r7
	b	.L1738
.L1740:
	ldr	r2, [r3, #-1736]
	cmp	r0, r2
	beq	.L1741
	sub	r3, r3, #1712
	ldrh	r3, [r3, #-10]
	mov	r3, r3, lsr #2
	cmp	r0, r3
	cmpcc	r2, r3
	bcs	.L1741
	bl	FtlSysBlkNumInit
.L1741:
	ldr	r5, .L1761+8
	mov	r4, #0
	ldr	r7, .L1761
	ldr	r6, .L1761+12
	sub	r8, r5, #28
.L1744:
	ldrh	r3, [r6]
	cmp	r4, r3
	bcs	.L1760
	ldrh	r2, [r8]
	ldr	r1, [r7, #-1264]
	ldr	r0, [r5, #4]!
	mov	r2, r2, asl #2
	mla	r1, r4, r2, r1
	bl	ftl_memcpy
	add	r4, r4, #1
	b	.L1744
.L1760:
	mov	r0, #0
	ldmfd	sp!, {r4, r5, r6, r7, r8, pc}
.L1746:
	mvn	r0, #0
	ldmfd	sp!, {r4, r5, r6, r7, r8, pc}
.L1762:
	.align	2
.L1761:
	.word	.LANCHOR2
	.word	.LANCHOR2-1272
	.word	.LANCHOR2-1320
	.word	.LANCHOR2-1710
	.fnend
	.size	FtlLoadBbt, .-FtlLoadBbt
	.align	2
	.global	FtlFreeSysBlkQueueInit
	.type	FtlFreeSysBlkQueueInit, %function
FtlFreeSysBlkQueueInit:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	ldr	r3, .L1765
	stmfd	sp!, {r4, lr}
	.save {r4, lr}
	sub	r2, r3, #4
	mov	r4, #0
	strh	r0, [r3, #-4]	@ movhi
	strh	r4, [r2, #4]	@ movhi
	add	r0, r3, #4
	mov	r1, r4
	mov	r2, #2048
	strh	r4, [r3, #-2]	@ movhi
	strh	r4, [r3, #2]	@ movhi
	bl	ftl_memset
	mov	r0, r4
	ldmfd	sp!, {r4, pc}
.L1766:
	.align	2
.L1765:
	.word	.LANCHOR2-1232
	.fnend
	.size	FtlFreeSysBlkQueueInit, .-FtlFreeSysBlkQueueInit
	.align	2
	.global	FtlFreeSysBlkQueueEmpty
	.type	FtlFreeSysBlkQueueEmpty, %function
FtlFreeSysBlkQueueEmpty:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	@ link register save eliminated.
	ldr	r3, .L1768
	ldrh	r0, [r3, #6]
	clz	r0, r0
	mov	r0, r0, lsr #5
	bx	lr
.L1769:
	.align	2
.L1768:
	.word	.LANCHOR2-1236
	.fnend
	.size	FtlFreeSysBlkQueueEmpty, .-FtlFreeSysBlkQueueEmpty
	.align	2
	.global	FtlFreeSysBlkQueueFull
	.type	FtlFreeSysBlkQueueFull, %function
FtlFreeSysBlkQueueFull:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	@ link register save eliminated.
	ldr	r3, .L1771
	ldrh	r0, [r3, #6]
	sub	r0, r0, #1024
	clz	r0, r0
	mov	r0, r0, lsr #5
	bx	lr
.L1772:
	.align	2
.L1771:
	.word	.LANCHOR2-1236
	.fnend
	.size	FtlFreeSysBlkQueueFull, .-FtlFreeSysBlkQueueFull
	.align	2
	.global	FtlFreeSysBlkQueueIn
	.type	FtlFreeSysBlkQueueIn, %function
FtlFreeSysBlkQueueIn:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	stmfd	sp!, {r3, r4, r5, r6, r7, lr}
	.save {r3, r4, r5, r6, r7, lr}
	sub	r3, r0, #1
	movw	r2, #65533
	mov	r7, r0
	uxth	r3, r3
	cmp	r3, r2
	ldmhifd	sp!, {r3, r4, r5, r6, r7, pc}
	ldr	r4, .L1783
	ldr	r5, .L1783+4
	ldrh	r3, [r4, #6]
	cmp	r3, #1024
	ldmeqfd	sp!, {r3, r4, r5, r6, r7, pc}
	cmp	r1, #0
	beq	.L1775
	ldr	r3, [r5, #-1276]
	cmp	r3, #0
	bne	.L1775
	bl	P2V_block_in_plane
	mov	r1, #1
	mov	r3, r7, asl #10
	mov	r2, r1
	mov	r6, r0
	ldr	r0, [r5, #-1488]
	str	r3, [r0, #4]
	bl	FlashEraseBlocks
	ldr	r1, [r5, #-1412]
	mov	r3, r6, asl #1
	ldrh	r2, [r1, r3]
	add	r2, r2, #1
	strh	r2, [r1, r3]	@ movhi
	ldr	r3, [r5, #-1572]
	add	r3, r3, #1
	str	r3, [r5, #-1572]
.L1775:
	ldrh	r3, [r4, #6]
	add	r3, r3, #1
	strh	r3, [r4, #6]	@ movhi
	ldrh	r3, [r4, #4]
	add	r2, r4, r3, asl #1
	add	r3, r3, #1
	ubfx	r3, r3, #0, #10
	strh	r3, [r4, #4]	@ movhi
	strh	r7, [r2, #8]	@ movhi
	ldmfd	sp!, {r3, r4, r5, r6, r7, pc}
.L1784:
	.align	2
.L1783:
	.word	.LANCHOR2-1236
	.word	.LANCHOR2
	.fnend
	.size	FtlFreeSysBlkQueueIn, .-FtlFreeSysBlkQueueIn
	.align	2
	.global	FtlLowFormatEraseBlock
	.type	FtlLowFormatEraseBlock, %function
FtlLowFormatEraseBlock:
	.fnstart
	@ args = 0, pretend = 0, frame = 24
	@ frame_needed = 0, uses_anonymous_args = 0
	stmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.save {r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.pad #28
	sub	sp, sp, #28
	ldr	r9, .L1835
	ldr	r5, [r9, #-1276]
	ldrb	r3, [r9, #-1804]	@ zero_extendqisi2
	cmp	r5, #0
	str	r3, [sp, #16]
	movne	r0, #0
	bne	.L1786
	ldrb	r3, [r9, #-2744]	@ zero_extendqisi2
	mov	fp, r1
	mov	r10, r5
	mov	r4, r5
	mov	r7, r9
	mov	r8, #36
	str	r0, [sp, #4]
	str	r3, [sp, #8]
	str	r0, [r9, #-1540]
.L1787:
	ldr	r3, .L1835+4
	uxth	r1, r10
	ldr	r6, .L1835
	ldrh	r0, [r3]
	cmp	r0, r1
	bls	.L1830
	mul	r0, r8, r1
	ldr	ip, [r7, #-1488]
	add	r1, r7, r1
	mov	r3, #0
	str	r3, [ip, r0]
	ldrb	r0, [r1, #-1704]	@ zero_extendqisi2
	ldr	r1, [sp, #4]
	bl	V2P_block
	cmp	fp, #0
	mov	r6, r0
	beq	.L1788
	bl	IsBlkInVendorPart
	cmp	r0, #0
	bne	.L1789
.L1788:
	mov	r0, r6
	bl	FtlBbmIsBadBlock
	cmp	r0, #0
	addne	r4, r4, #1
	uxthne	r4, r4
	bne	.L1789
	ldr	r3, .L1835+8
	mov	r6, r6, asl #10
	ldr	ip, [r9, #-1488]
	ldrh	r1, [r3]
	mla	ip, r8, r5, ip
	mul	r1, r1, r5
	add	r5, r5, #1
	uxth	r5, r5
	cmp	r1, #0
	str	r0, [ip, #8]
	add	r0, r1, #3
	str	r6, [ip, #4]
	movlt	r1, r0
	ldr	r0, [r9, #-1436]
	bic	r1, r1, #3
	add	r1, r0, r1
	str	r1, [ip, #12]
.L1789:
	add	r10, r10, #1
	b	.L1787
.L1830:
	cmp	r5, #0
	beq	.L1810
	ldr	r3, [sp, #8]
	mov	r2, r5
	ldr	r0, [r6, #-1488]
	mov	r8, #0
	adds	r7, r3, #0
	strb	r8, [r6, #-1804]
	movne	r7, #1
	mov	r1, r7
	bl	FlashEraseBlocks
	ldrb	r3, [sp, #16]	@ zero_extendqisi2
	strb	r3, [r6, #-1804]
	mov	r6, #36
.L1793:
	uxth	r2, r8
	cmp	r2, r5
	bcs	.L1831
	mul	r2, r6, r8
	ldr	r1, [r9, #-1488]
	add	r0, r1, r2
	ldr	r2, [r1, r2]
	cmn	r2, #1
	bne	.L1794
	ldr	r0, [r0, #4]
	add	r4, r4, #1
	ubfx	r0, r0, #10, #16
	uxth	r4, r4
	bl	FtlBbmMapBadBlock
.L1794:
	add	r8, r8, #1
	b	.L1793
.L1831:
	cmp	fp, #0
	bne	.L1796
	mov	r3, #6
	uxth	r6, r7
	str	r3, [sp, #12]
	mov	r3, #1
	str	r3, [sp, #8]
	b	.L1797
.L1796:
	ldr	r2, .L1835+12
	ldrh	r3, [r2, #-14]
	str	r3, [sp, #8]
	ldrb	r3, [r9, #-2744]	@ zero_extendqisi2
	cmp	r3, #0
	ldreq	r3, [sp, #8]
	moveq	r6, #1
	movne	r6, #1
	strne	r6, [sp, #12]
	moveq	r3, r3, lsr #2
	streq	r3, [sp, #12]
.L1797:
	ldr	r10, .L1835
	mov	r7, #0
.L1806:
	mov	r8, #0
	mov	r5, r8
.L1798:
	ldr	r3, .L1835+4
	ldr	r9, .L1835
	ldrh	r2, [r3]
	uxth	r3, r8
	cmp	r2, r3
	bls	.L1832
	mov	r2, #36
	ldr	r1, [r10, #-1488]
	mul	r2, r2, r3
	add	r3, r10, r3
	mov	r0, #0
	str	r0, [r1, r2]
	ldr	r1, [sp, #4]
	ldrb	r0, [r3, #-1704]	@ zero_extendqisi2
	bl	V2P_block
	cmp	fp, #0
	mov	r9, r0
	beq	.L1799
	bl	IsBlkInVendorPart
	cmp	r0, #0
	bne	.L1800
.L1799:
	mov	r0, r9
	bl	FtlBbmIsBadBlock
	cmp	r0, #0
	bne	.L1800
	ldr	r2, [r10, #-1488]
	mov	r3, #36
	add	r9, r7, r9, asl #10
	mla	r2, r3, r5, r2
	ldr	r3, [r10, #-1452]
	str	r3, [r2, #8]
	ldr	r3, .L1835+8
	str	r9, [r2, #4]
	ldrh	r3, [r3]
	mul	r3, r3, r5
	add	r5, r5, #1
	uxth	r5, r5
	add	r1, r3, #3
	cmp	r3, #0
	movlt	r3, r1
	ldr	r1, [r10, #-1448]
	bic	r3, r3, #3
	add	r3, r1, r3
	str	r3, [r2, #12]
.L1800:
	add	r8, r8, #1
	b	.L1798
.L1832:
	cmp	r5, #0
	beq	.L1810
	ldr	r2, .L1835
	mov	r3, #0
	mov	r1, r5
	mov	r8, #0
	strb	r3, [r2, #-1804]
	mov	r3, #1
	ldr	r0, [r2, #-1488]
	mov	r2, r6
	bl	FlashProgPages
	ldr	r3, .L1835
	mov	r1, #36
	ldrb	r2, [sp, #16]	@ zero_extendqisi2
	strb	r2, [r3, #-1804]
.L1803:
	uxth	r3, r8
	cmp	r3, r5
	bcs	.L1833
	mul	r3, r1, r8
	ldr	r2, .L1835
	ldr	r2, [r2, #-1488]
	add	r0, r2, r3
	ldr	r3, [r2, r3]
	cmp	r3, #0
	beq	.L1804
	ldr	r0, [r0, #4]
	add	r4, r4, #1
	str	r1, [sp, #20]
	ubfx	r0, r0, #10, #16
	uxth	r4, r4
	bl	FtlBbmMapBadBlock
	ldr	r1, [sp, #20]
.L1804:
	add	r8, r8, #1
	b	.L1803
.L1833:
	ldr	r3, [sp, #12]
	add	r7, r7, r3
	ldr	r3, [sp, #8]
	uxth	r7, r7
	cmp	r7, r3
	bcc	.L1806
	mov	r7, #0
	mov	r8, #36
.L1807:
	uxth	r3, r7
	cmp	r3, r5
	bcs	.L1834
	cmp	fp, #0
	beq	.L1808
	mul	r3, r8, r7
	ldr	r2, [r9, #-1488]
	add	r1, r2, r3
	ldr	r3, [r2, r3]
	cmp	r3, #0
	bne	.L1808
	ldr	r0, [r1, #4]
	mov	r1, #1
	ubfx	r0, r0, #10, #16
	bl	FtlFreeSysBlkQueueIn
.L1808:
	add	r7, r7, #1
	b	.L1807
.L1834:
	adds	r3, fp, #0
	ldr	r2, [sp, #4]
	movne	r3, #1
	cmp	r2, #63
	orrls	r3, r3, #1
	cmp	r3, #0
	beq	.L1810
	ldr	r0, [r9, #-1488]
	mov	r1, r6
	mov	r2, r5
	bl	FlashEraseBlocks
.L1810:
	mov	r0, r4
.L1786:
	add	sp, sp, #28
	@ sp needed
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L1836:
	.align	2
.L1835:
	.word	.LANCHOR2
	.word	.LANCHOR2-1732
	.word	.LANCHOR2-1652
	.word	.LANCHOR2-1648
	.fnend
	.size	FtlLowFormatEraseBlock, .-FtlLowFormatEraseBlock
	.align	2
	.global	FtlFreeSysBLkSort
	.type	FtlFreeSysBLkSort, %function
FtlFreeSysBLkSort:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	ldr	r3, .L1850
	ldrh	ip, [r3, #28]
	ldr	r3, .L1850+4
	ldrh	r2, [r3, #6]
	cmp	r2, #0
	bxeq	lr
	stmfd	sp!, {r4, lr}
	.save {r4, lr}
	mov	r0, #0
	ldrh	r1, [r3, #2]
	and	ip, ip, #31
	ldrh	r2, [r3, #4]
	mov	r4, r0
.L1839:
	uxth	lr, r0
	add	r0, r0, #1
	cmp	lr, ip
	bge	.L1849
	add	lr, r3, r1, asl #1
	add	r1, r1, #1
	ubfx	r1, r1, #0, #10
	ldrh	r4, [lr, #8]
	add	lr, r3, r2, asl #1
	strh	r4, [lr, #8]	@ movhi
	mov	r4, #1
	add	r2, r2, r4
	ubfx	r2, r2, #0, #10
	b	.L1839
.L1849:
	cmp	r4, #0
	strneh	r1, [r3, #2]	@ movhi
	strneh	r2, [r3, #4]	@ movhi
	ldmfd	sp!, {r4, pc}
.L1851:
	.align	2
.L1850:
	.word	.LANCHOR2+820
	.word	.LANCHOR2-1236
	.fnend
	.size	FtlFreeSysBLkSort, .-FtlFreeSysBLkSort
	.align	2
	.global	FtlFreeSysBlkQueueOut
	.type	FtlFreeSysBlkQueueOut, %function
FtlFreeSysBlkQueueOut:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	stmfd	sp!, {r3, r4, r5, r6, r7, r8, r9, lr}
	.save {r3, r4, r5, r6, r7, r8, r9, lr}
	ldr	r6, .L1863
	ldr	r5, .L1863+4
	mov	r7, r6
.L1853:
	ldrh	r1, [r5, #6]
	cmp	r1, #0
	beq	.L1854
	ldrh	r3, [r5, #2]
	sub	r1, r1, #1
	ldr	r9, [r6, #-1276]
	strh	r1, [r5, #6]	@ movhi
	add	r2, r5, r3, asl #1
	cmp	r9, #0
	add	r3, r3, #1
	ubfx	r3, r3, #0, #10
	ldrh	r4, [r2, #8]
	strh	r3, [r5, #2]	@ movhi
	bne	.L1855
	mov	r0, r4
	bl	P2V_block_in_plane
	mov	r3, r4, asl #10
	mov	r8, r0
	ldr	r0, [r6, #-1488]
	str	r3, [r0, #4]
	ldrb	r3, [r6, #-2744]	@ zero_extendqisi2
	cmp	r3, #0
	beq	.L1856
	mov	r1, r9
	mov	r2, #1
	bl	FlashEraseBlocks
.L1856:
	mov	r1, #1
	ldr	r0, [r7, #-1488]
	mov	r2, r1
	bl	FlashEraseBlocks
	ldr	r1, [r7, #-1412]
	mov	r3, r8, asl #1
	ldrh	r2, [r1, r3]
	add	r2, r2, #1
	strh	r2, [r1, r3]	@ movhi
	ldr	r3, [r7, #-1572]
	add	r3, r3, #1
	str	r3, [r7, #-1572]
	b	.L1855
.L1854:
	ldr	r0, .L1863+8
	bl	printk
.L1857:
	b	.L1857
.L1855:
	sub	r3, r4, #1
	movw	r2, #65533
	uxth	r3, r3
	cmp	r3, r2
	bls	.L1858
	ldr	r3, .L1863+4
	mov	r1, r4
	ldr	r0, .L1863+12
	ldrh	r2, [r3, #6]
	bl	printk
	b	.L1853
.L1858:
	mov	r0, r4
	ldmfd	sp!, {r3, r4, r5, r6, r7, r8, r9, pc}
.L1864:
	.align	2
.L1863:
	.word	.LANCHOR2
	.word	.LANCHOR2-1236
	.word	.LC35
	.word	.LC36
	.fnend
	.size	FtlFreeSysBlkQueueOut, .-FtlFreeSysBlkQueueOut
	.align	2
	.global	test_node_in_list
	.type	test_node_in_list, %function
test_node_in_list:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	ldr	r3, .L1871
	str	lr, [sp, #-4]!
	.save {lr}
	movw	lr, #65535
	ldr	ip, [r3, #-1352]
	ldr	r2, [r0]
	ldr	r3, .L1871+4
	rsb	r0, ip, r2
	mov	r0, r0, asr #1
	mul	r3, r3, r0
	mov	r0, #6
	uxth	r3, r3
.L1867:
	cmp	r1, r3
	beq	.L1868
	ldrh	r3, [r2]
	cmp	r3, lr
	beq	.L1869
	mla	r2, r0, r3, ip
	b	.L1867
.L1868:
	mov	r0, #1
	ldr	pc, [sp], #4
.L1869:
	mov	r0, #0
	ldr	pc, [sp], #4
.L1872:
	.align	2
.L1871:
	.word	.LANCHOR2
	.word	-1431655765
	.fnend
	.size	test_node_in_list, .-test_node_in_list
	.align	2
	.global	insert_data_list
	.type	insert_data_list, %function
insert_data_list:
	.fnstart
	@ args = 0, pretend = 0, frame = 8
	@ frame_needed = 0, uses_anonymous_args = 0
	stmfd	sp!, {r0, r1, r2, r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.save {r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.pad #12
	ldr	r2, .L1890
	sub	r5, r2, #1712
	ldrh	r3, [r5, #-12]
	cmp	r3, r0
	bls	.L1875
	mov	lr, #6
	ldr	r6, [r2, #-1352]
	mul	lr, lr, r0
	mvn	ip, #0
	add	r1, r6, lr
	strh	ip, [r1, #2]	@ movhi
	strh	ip, [r6, lr]	@ movhi
	ldr	r3, [r2, #868]
	cmp	r3, #0
	beq	.L1889
	ldr	r8, [r2, #-1404]
	mov	r4, r0, asl #1
	mov	r10, r2
	ldrh	r2, [r1, #4]
	ldrh	r5, [r5, #-12]
	ldrh	r7, [r8, r4]
	cmp	r2, #0
	str	r5, [sp]
	mulne	ip, r2, r7
	ldr	r7, [r10, #-1352]
	ldr	r2, .L1890+4
	rsb	r9, r7, r3
	mov	r9, r9, asr #1
	mul	r2, r2, r9
	ldr	r9, [r10, #-1412]
	add	r4, r9, r4
	str	r4, [sp, #4]
	mov	r4, #0
	uxth	r2, r2
.L1884:
	add	r4, r4, #1
	ldr	r5, [sp]
	uxth	r4, r4
	cmp	r4, r5
	movls	r5, #0
	movhi	r5, #1
	cmp	r0, r2
	orreq	r5, r5, #1
	cmp	r5, #0
	bne	.L1875
	mov	r10, r2, asl #1
	ldrh	r5, [r3, #4]
	ldrh	fp, [r8, r10]
	cmp	r5, #0
	mvneq	r5, #0
	mulne	r5, r5, fp
	cmp	r5, ip
	bne	.L1880
	ldr	r5, [sp, #4]
	ldrh	r10, [r9, r10]
	ldrh	r5, [r5]
	cmp	r10, r5
	bcc	.L1882
	b	.L1881
.L1880:
	bhi	.L1881
.L1882:
	ldrh	r5, [r3]
	movw	r10, #65535
	cmp	r5, r10
	streqh	r2, [r1, #2]	@ movhi
	streqh	r0, [r3]	@ movhi
	ldreq	r3, .L1890
	streq	r1, [r3, #872]
	beq	.L1875
.L1883:
	mov	r3, #6
	mov	r2, r5
	mla	r3, r3, r5, r7
	b	.L1884
.L1881:
	strh	r2, [r6, lr]	@ movhi
	ldrh	r2, [r3, #2]
	strh	r2, [r1, #2]	@ movhi
	ldr	r2, .L1890
	ldr	ip, [r2, #868]
	cmp	r3, ip
	bne	.L1885
	strh	r0, [r3, #2]	@ movhi
.L1889:
	str	r1, [r2, #868]
	b	.L1875
.L1885:
	ldrh	ip, [r3, #2]
	ldr	r1, [r2, #-1352]
	mov	r2, #6
	mul	r2, r2, ip
	strh	r0, [r1, r2]	@ movhi
	strh	r0, [r3, #2]	@ movhi
.L1875:
	mov	r0, #0
	add	sp, sp, #12
	@ sp needed
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L1891:
	.align	2
.L1890:
	.word	.LANCHOR2
	.word	-1431655765
	.fnend
	.size	insert_data_list, .-insert_data_list
	.align	2
	.global	INSERT_DATA_LIST
	.type	INSERT_DATA_LIST, %function
INSERT_DATA_LIST:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	stmfd	sp!, {r3, lr}
	.save {r3, lr}
	bl	insert_data_list
	ldr	r2, .L1894
	ldrh	r3, [r2]
	add	r3, r3, #1
	strh	r3, [r2]	@ movhi
	ldmfd	sp!, {r3, pc}
.L1895:
	.align	2
.L1894:
	.word	.LANCHOR2+876
	.fnend
	.size	INSERT_DATA_LIST, .-INSERT_DATA_LIST
	.align	2
	.global	insert_free_list
	.type	insert_free_list, %function
insert_free_list:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	stmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, lr}
	.save {r4, r5, r6, r7, r8, r9, r10, lr}
	movw	r4, #65535
	cmp	r0, r4
	beq	.L1897
	ldr	r2, .L1905
	mov	lr, #6
	mul	r6, lr, r0
	mvn	r3, #0
	ldr	r7, [r2, #-1352]
	mov	r5, r2
	add	ip, r7, r6
	strh	r3, [ip, #2]	@ movhi
	strh	r3, [r7, r6]	@ movhi
	ldr	r3, [r2, #880]
	cmp	r3, #0
	beq	.L1904
	ldr	r9, [r2, #-1412]
	mov	r2, r0, asl #1
	ldr	r8, [r5, #-1352]
	rsb	r1, r8, r3
	ldrh	r10, [r9, r2]
	ldr	r2, .L1905+4
	mov	r1, r1, asr #1
	mul	r1, r2, r1
	uxth	r2, r1
.L1901:
	mov	r1, r2, asl #1
	ldrh	r1, [r9, r1]
	cmp	r1, r10
	bcs	.L1899
	ldrh	r1, [r3]
	cmp	r1, r4
	streqh	r2, [ip, #2]	@ movhi
	streqh	r0, [r3]	@ movhi
	beq	.L1897
.L1900:
	mla	r3, lr, r1, r8
	mov	r2, r1
	b	.L1901
.L1899:
	ldrh	r1, [r3, #2]
	strh	r1, [ip, #2]	@ movhi
	strh	r2, [r7, r6]	@ movhi
	ldr	r1, [r5, #880]
	ldr	r2, .L1905
	cmp	r3, r1
	bne	.L1902
	strh	r0, [r3, #2]	@ movhi
.L1904:
	str	ip, [r2, #880]
	b	.L1897
.L1902:
	ldrh	ip, [r3, #2]
	ldr	r1, [r2, #-1352]
	mov	r2, #6
	mul	r2, r2, ip
	strh	r0, [r1, r2]	@ movhi
	strh	r0, [r3, #2]	@ movhi
.L1897:
	mov	r0, #0
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, pc}
.L1906:
	.align	2
.L1905:
	.word	.LANCHOR2
	.word	-1431655765
	.fnend
	.size	insert_free_list, .-insert_free_list
	.align	2
	.global	INSERT_FREE_LIST
	.type	INSERT_FREE_LIST, %function
INSERT_FREE_LIST:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	stmfd	sp!, {r3, lr}
	.save {r3, lr}
	bl	insert_free_list
	ldr	r2, .L1909
	ldrh	r3, [r2]
	add	r3, r3, #1
	strh	r3, [r2]	@ movhi
	ldmfd	sp!, {r3, pc}
.L1910:
	.align	2
.L1909:
	.word	.LANCHOR2+884
	.fnend
	.size	INSERT_FREE_LIST, .-INSERT_FREE_LIST
	.align	2
	.global	List_remove_node
	.type	List_remove_node, %function
List_remove_node:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	stmfd	sp!, {r4, r5, lr}
	.save {r4, r5, lr}
	mov	ip, #6
	ldr	r4, .L1917
	movw	r5, #65535
	mul	r1, ip, r1
	ldr	r3, [r0]
	ldr	r2, [r4, #-1352]
	add	lr, r2, r1
	cmp	lr, r3
	ldrh	r3, [r2, r1]
	bne	.L1912
	cmp	r3, r5
	mlane	r3, ip, r3, r2
	moveq	r3, #0
	streq	r3, [r0]
	strne	r3, [r0]
	mvnne	r0, #0
	strneh	r0, [r3, #2]	@ movhi
	b	.L1914
.L1912:
	cmp	r3, r5
	ldrh	r0, [lr, #2]
	bne	.L1915
	cmp	r0, r3
	mulne	r0, ip, r0
	mvnne	r3, #0
	strneh	r3, [r2, r0]	@ movhi
	b	.L1914
.L1915:
	mla	r3, ip, r3, r2
	strh	r0, [r3, #2]	@ movhi
	ldrh	r5, [lr, #2]
	ldrh	r0, [r2, r1]
	ldr	r3, [r4, #-1352]
	mul	ip, ip, r5
	strh	r0, [r3, ip]	@ movhi
.L1914:
	mvn	r3, #0
	mov	r0, #0
	strh	r3, [r2, r1]	@ movhi
	strh	r3, [lr, #2]	@ movhi
	ldmfd	sp!, {r4, r5, pc}
.L1918:
	.align	2
.L1917:
	.word	.LANCHOR2
	.fnend
	.size	List_remove_node, .-List_remove_node
	.align	2
	.global	List_pop_index_node
	.type	List_pop_index_node, %function
List_pop_index_node:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	ldr	r3, [r0]
	cmp	r3, #0
	beq	.L1925
	ldr	r2, .L1928
	movw	ip, #65535
	stmfd	sp!, {r4, lr}
	.save {r4, lr}
	mov	lr, #6
	ldr	r4, [r2, #-1352]
.L1921:
	cmp	r1, #0
	bne	.L1922
.L1924:
	rsb	r4, r4, r3
	ldr	r3, .L1928+4
	mov	r4, r4, asr #1
	mul	r4, r3, r4
	uxth	r4, r4
	mov	r1, r4
	bl	List_remove_node
	mov	r0, r4
	ldmfd	sp!, {r4, pc}
.L1922:
	ldrh	r2, [r3]
	cmp	r2, ip
	beq	.L1924
	sub	r1, r1, #1
	mla	r3, lr, r2, r4
	uxth	r1, r1
	b	.L1921
.L1925:
	movw	r0, #65535
	bx	lr
.L1929:
	.align	2
.L1928:
	.word	.LANCHOR2
	.word	-1431655765
	.fnend
	.size	List_pop_index_node, .-List_pop_index_node
	.align	2
	.global	List_get_gc_head_node
	.type	List_get_gc_head_node, %function
List_get_gc_head_node:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	@ link register save eliminated.
	ldr	r2, .L1937
	ldr	r3, [r2, #868]
	cmp	r3, #0
	ldrne	r1, [r2, #-1352]
	movne	ip, #6
	movwne	r2, #65535
	beq	.L1935
.L1932:
	cmp	r0, #0
	beq	.L1933
	ldrh	r3, [r3]
	cmp	r3, r2
	subne	r0, r0, #1
	mlane	r3, ip, r3, r1
	uxthne	r0, r0
	bne	.L1932
.L1935:
	movw	r0, #65535
	bx	lr
.L1933:
	rsb	r3, r1, r3
	ldr	r0, .L1937+4
	mov	r3, r3, asr #1
	mul	r0, r0, r3
	uxth	r0, r0
	bx	lr
.L1938:
	.align	2
.L1937:
	.word	.LANCHOR2
	.word	-1431655765
	.fnend
	.size	List_get_gc_head_node, .-List_get_gc_head_node
	.align	2
	.global	List_update_data_list
	.type	List_update_data_list, %function
List_update_data_list:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	ldr	r3, .L1948
	add	r2, r3, #888
	ldrh	r2, [r2]
	cmp	r2, r0
	beq	.L1947
	add	r2, r3, #936
	ldrh	r2, [r2]
	cmp	r2, r0
	beq	.L1947
	add	r2, r3, #984
	ldrh	r2, [r2]
	cmp	r2, r0
	beq	.L1947
	stmfd	sp!, {r4, lr}
	.save {r4, lr}
	mov	lr, #6
	mul	lr, lr, r0
	ldr	r1, [r3, #-1352]
	ldr	r2, [r3, #868]
	add	ip, r1, lr
	cmp	ip, r2
	beq	.L1940
	ldr	r4, [r3, #-1404]
	mov	r3, r0, asl #1
	ldrh	r2, [r4, r3]
	ldrh	r3, [ip, #4]
	ldrh	ip, [ip, #2]
	cmp	r3, #0
	mulne	r2, r3, r2
	mvneq	r2, #0
	movw	r3, #65535
	cmp	ip, r3
	bne	.L1942
	ldrh	r3, [r1, lr]
	cmp	r3, ip
	beq	.L1940
.L1942:
	mov	r3, #6
	mul	ip, r3, ip
	ldr	r3, .L1948+4
	add	r1, r1, ip
	mov	lr, ip, asr #1
	mul	r3, r3, lr
	mov	r3, r3, asl #1
	ldrh	lr, [r4, r3]
	ldrh	r3, [r1, #4]
	cmp	r3, #0
	mulne	r3, r3, lr
	mvneq	r3, #0
	cmp	r2, r3
	bcs	.L1940
	mov	r4, r0
	ldr	r0, .L1948+8
	mov	r1, r4
	bl	List_remove_node
	ldr	r2, .L1948+12
	mov	r0, r4
	ldrh	r3, [r2]
	sub	r3, r3, #1
	strh	r3, [r2]	@ movhi
	bl	INSERT_DATA_LIST
.L1940:
	mov	r0, #0
	ldmfd	sp!, {r4, pc}
.L1947:
	mov	r0, #0
	bx	lr
.L1949:
	.align	2
.L1948:
	.word	.LANCHOR2
	.word	-1431655765
	.word	.LANCHOR2+868
	.word	.LANCHOR2+876
	.fnend
	.size	List_update_data_list, .-List_update_data_list
	.align	2
	.global	ftl_free_no_use_map_blk
	.type	ftl_free_no_use_map_blk, %function
ftl_free_no_use_map_blk:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	stmfd	sp!, {r3, r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.save {r3, r4, r5, r6, r7, r8, r9, r10, fp, lr}
	mov	r1, #0
	ldrh	r2, [r0, #10]
	mov	r4, r0
	ldr	r5, [r0, #20]
	ldr	r7, [r0, #12]
	ldr	r6, [r0, #24]
	mov	r2, r2, asl #1
	mov	r0, r5
	bl	ftl_memset
	mov	r2, #0
.L1951:
	ldrh	r1, [r4, #6]
	uxth	r3, r2
	cmp	r1, r3
	bls	.L1971
	ldr	r0, [r6, r3, asl #2]
	mov	r1, #0
	ubfx	r0, r0, #10, #16
.L1952:
	ldrh	ip, [r4, #10]
	uxth	r3, r1
	cmp	ip, r3
	bls	.L1972
	mov	r3, r3, asl #1
	add	r1, r1, #1
	ldrh	ip, [r7, r3]
	rsb	lr, ip, r0
	cmp	ip, #0
	clz	lr, lr
	mov	lr, lr, lsr #5
	moveq	lr, #0
	cmp	lr, #0
	ldrneh	ip, [r5, r3]
	addne	ip, ip, #1
	strneh	ip, [r5, r3]	@ movhi
	b	.L1952
.L1972:
	add	r2, r2, #1
	b	.L1951
.L1971:
	ldr	r3, .L1974
	mov	r8, #0
	mov	r1, r8
	mov	fp, r8
	ldrh	r2, [r3, #-14]
	ldrh	r3, [r4]
	mov	r3, r3, asl #1
	strh	r2, [r5, r3]	@ movhi
	ldrh	r9, [r5]
.L1956:
	ldrh	r3, [r4, #10]
	uxth	r6, r8
	cmp	r3, r6
	bls	.L1973
	mov	r2, r6, asl #1
	ldrh	r3, [r5, r2]
	cmp	r9, r3
	bls	.L1957
	ldrh	r0, [r7, r2]
	add	r10, r7, r2
	cmp	r0, #0
	bne	.L1958
	b	.L1959
.L1957:
	cmp	r3, #0
	bne	.L1959
	ldrh	r0, [r7, r2]
	add	r10, r7, r2
	cmp	r0, #0
	movne	r6, r1
	beq	.L1959
	b	.L1961
.L1958:
	cmp	r3, #0
	movne	r1, r6
	movne	r9, r3
	bne	.L1959
	mov	r9, r3
.L1961:
	mov	r1, #1
	bl	FtlFreeSysBlkQueueIn
	strh	fp, [r10]	@ movhi
	ldrh	r3, [r4, #8]
	mov	r1, r6
	sub	r3, r3, #1
	strh	r3, [r4, #8]	@ movhi
.L1959:
	add	r8, r8, #1
	b	.L1956
.L1973:
	mov	r0, r1
	ldmfd	sp!, {r3, r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L1975:
	.align	2
.L1974:
	.word	.LANCHOR2-1648
	.fnend
	.size	ftl_free_no_use_map_blk, .-ftl_free_no_use_map_blk
	.align	2
	.global	ftl_map_blk_alloc_new_blk
	.type	ftl_map_blk_alloc_new_blk, %function
ftl_map_blk_alloc_new_blk:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	stmfd	sp!, {r3, r4, r5, r6, r7, lr}
	.save {r3, r4, r5, r6, r7, lr}
	mov	r3, #0
	ldrh	r1, [r0, #10]
	ldr	r2, [r0, #12]
.L1977:
	uxth	r5, r3
	cmp	r5, r1
	bcs	.L1980
	mov	r7, r2
	add	r3, r3, #1
	ldrh	r6, [r7]
	add	r2, r2, #2
	cmp	r6, #0
	bne	.L1977
	mov	r4, r0
	bl	FtlFreeSysBlkQueueOut
	movw	r2, #65533
	sub	r3, r0, #1
	mov	r1, r0
	strh	r0, [r7]	@ movhi
	uxth	r3, r3
	cmp	r3, r2
	bls	.L1978
	ldr	r3, .L1984
	ldr	r0, .L1984+4
	ldrh	r2, [r3, #6]
	bl	printk
.L1979:
	b	.L1979
.L1978:
	ldr	r3, [r4, #28]
	strh	r6, [r4, #2]	@ movhi
	add	r3, r3, #1
	str	r3, [r4, #28]
	ldrh	r3, [r4, #8]
	strh	r5, [r4]	@ movhi
	add	r3, r3, #1
	strh	r3, [r4, #8]	@ movhi
.L1980:
	mov	r0, #0
	ldmfd	sp!, {r3, r4, r5, r6, r7, pc}
.L1985:
	.align	2
.L1984:
	.word	.LANCHOR2-1236
	.word	.LC37
	.fnend
	.size	ftl_map_blk_alloc_new_blk, .-ftl_map_blk_alloc_new_blk
	.align	2
	.global	FtlMapWritePage
	.type	FtlMapWritePage, %function
FtlMapWritePage:
	.fnstart
	@ args = 0, pretend = 0, frame = 8
	@ frame_needed = 0, uses_anonymous_args = 0
	stmfd	sp!, {r0, r1, r2, r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.save {r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.pad #12
	mov	r4, r0
	ldr	fp, .L2005
	mov	r8, r1
	ldr	r9, .L2005+4
	mov	r5, #0
	str	r2, [sp]
	mov	r10, fp
.L1987:
	ldr	r3, [fp, #-1588]
	ldr	r6, .L2005
	add	r3, r3, #1
	str	r3, [fp, #-1588]
	ldrh	r3, [r9]
	ldrh	r2, [r4, #2]
	sub	r3, r3, #1
	cmp	r2, r3
	bge	.L1988
	ldrh	r2, [r4]
	movw	r3, #65535
	cmp	r2, r3
	bne	.L1989
.L1988:
	mov	r0, r4
	bl	Ftl_write_map_blk_to_last_page
.L1989:
	ldr	r1, [r10, #-1276]
	cmp	r1, #0
	bne	.L1990
	ldrh	r3, [r4]
	ldr	r2, [r4, #12]
	ldr	r0, [fp, #-1440]
	mov	r3, r3, asl #1
	ldrh	r7, [r2, r3]
	mov	r2, #16
	ldrh	r3, [r4, #2]
	str	r0, [fp, #-1260]
	orr	r3, r3, r7, asl #10
	str	r3, [fp, #-1268]
	ldr	r3, [sp]
	str	r3, [fp, #-1264]
	bl	ftl_memset
	ldr	r2, [r4, #28]
	ldr	r3, [fp, #-1260]
	str	r2, [r3, #4]
	strh	r8, [r3, #8]	@ movhi
	ldrh	r2, [r4, #4]
	strh	r7, [r3, #2]	@ movhi
	strh	r2, [r3]	@ movhi
	ldr	r2, .L2005+8
	ldrb	r2, [r2]	@ zero_extendqisi2
	cmp	r2, #0
	beq	.L1991
	ldr	r2, .L2005+12
	ldr	r0, [fp, #-1264]
	str	r3, [sp, #4]
	ldrh	r1, [r2]
	bl	js_hash
	ldr	r3, [sp, #4]
	str	r0, [r3, #12]
.L1991:
	mov	r1, #1
	ldr	r0, .L2005+16
	mov	r2, r1
	mov	r3, r1
	bl	FlashProgPages
	ldrh	r3, [r4, #2]
	add	r3, r3, #1
	uxth	r3, r3
	strh	r3, [r4, #2]	@ movhi
	ldr	r2, [r10, #-1272]
	cmn	r2, #1
	bne	.L1992
	ldr	r0, .L2005+20
	add	r5, r5, #1
	ldr	r1, [fp, #-1268]
	bl	printk
	ldrh	r3, [r4, #2]
	uxth	r5, r5
	cmp	r3, #2
	ldrlsh	r3, [r9]
	subls	r3, r3, #1
	strlsh	r3, [r4, #2]	@ movhi
	cmp	r5, #3
	bls	.L1987
	ldr	r0, .L2005+24
	mov	r2, r5
	ldr	r1, [r6, #-1268]
	bl	printk
	mov	r3, #1
	str	r3, [r6, #-1276]
	b	.L1990
.L1992:
	cmp	r2, #0
	strneh	r7, [r4, #40]	@ movhi
	cmp	r2, #256
	cmpne	r3, #1
	beq	.L1996
	ldr	r3, [r4, #36]
	cmp	r3, #0
	beq	.L1997
.L1996:
	mov	r3, #0
	str	r3, [r4, #36]
	b	.L1987
.L1997:
	ldr	r2, [r6, #-1268]
	ldr	r3, [r4, #24]
	str	r2, [r3, r8, asl #2]
.L1990:
	mov	r0, #0
	add	sp, sp, #12
	@ sp needed
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L2006:
	.align	2
.L2005:
	.word	.LANCHOR2
	.word	.LANCHOR2-1662
	.word	.LANCHOR0
	.word	.LANCHOR2-1654
	.word	.LANCHOR2-1272
	.word	.LC38
	.word	.LC39
	.fnend
	.size	FtlMapWritePage, .-FtlMapWritePage
	.align	2
	.global	ftl_map_blk_gc
	.type	ftl_map_blk_gc, %function
ftl_map_blk_gc:
	.fnstart
	@ args = 0, pretend = 0, frame = 8
	@ frame_needed = 0, uses_anonymous_args = 0
	stmfd	sp!, {r0, r1, r2, r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.save {r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.pad #12
	mov	r4, r0
	ldr	r5, [r0, #12]
	ldr	r7, [r0, #24]
	bl	ftl_free_no_use_map_blk
	ldrh	r3, [r4, #10]
	ldrh	r2, [r4, #8]
	sub	r3, r3, #4
	ldr	r8, .L2021
	cmp	r2, r3
	blt	.L2008
	uxth	r0, r0
	mov	r0, r0, asl #1
	ldrh	r10, [r5, r0]
	cmp	r10, #0
	beq	.L2008
	ldr	r3, [r4, #32]
	cmp	r3, #0
	bne	.L2008
	mov	r2, #1
	str	r2, [r4, #32]
	strh	r3, [r5, r0]	@ movhi
	ldrh	r3, [r4, #8]
	ldrh	r2, [r4, #2]
	sub	r3, r3, #1
	strh	r3, [r4, #8]	@ movhi
	ldrh	r3, [r8, #-14]
	cmp	r2, r3
	bcc	.L2009
	mov	r0, r4
	bl	ftl_map_blk_alloc_new_blk
.L2009:
	ldr	r5, .L2021+4
	mov	fp, #0
.L2010:
	ldrh	r3, [r4, #6]
	uxth	r6, fp
	cmp	r3, r6
	bls	.L2020
	ldr	r3, [r7, r6, asl #2]
	add	ip, r7, r6, asl #2
	cmp	r10, r3, lsr #10
	bne	.L2011
	ldr	r3, [r5, #-1468]
	mov	r1, #1
	ldr	r9, [r5, #-1440]
	mov	r2, r1
	ldr	r0, .L2021+8
	str	r3, [r5, #-1264]
	str	r9, [r5, #-1260]
	ldr	r3, [r7, r6, asl #2]
	str	ip, [sp, #4]
	str	r3, [r5, #-1268]
	bl	FlashReadPages
	ldr	r3, [r5, #-1272]
	cmn	r3, #1
	ldr	r3, .L2021+4
	ldr	ip, [sp, #4]
	bne	.L2012
.L2014:
	mov	r2, #0
	ldr	r0, .L2021+12
	str	r2, [ip]
	ldr	r1, [r3, #-1268]
	ldrh	r2, [r9, #8]
	str	r3, [sp, #4]
	bl	printk
	mov	r2, #1
	ldr	r3, [sp, #4]
	str	r2, [r3, #-1276]
	b	.L2013
.L2012:
	ldrh	r1, [r9, #8]
	cmp	r1, r6
	bne	.L2014
	ldrh	r0, [r9]
	ldrh	r2, [r4, #4]
	cmp	r0, r2
	bne	.L2014
	mov	r0, r4
	ldr	r2, [r5, #-1264]
	bl	FtlMapWritePage
.L2011:
	add	fp, fp, #1
	b	.L2010
.L2020:
	mov	r0, r10
	mov	r1, #1
	bl	FtlFreeSysBlkQueueIn
	mov	r3, #0
	str	r3, [r4, #32]
.L2008:
	ldrh	r2, [r4, #2]
	ldrh	r3, [r8, #-14]
	cmp	r2, r3
	bcc	.L2013
	mov	r0, r4
	bl	ftl_map_blk_alloc_new_blk
.L2013:
	mov	r0, #0
	add	sp, sp, #12
	@ sp needed
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L2022:
	.align	2
.L2021:
	.word	.LANCHOR2-1648
	.word	.LANCHOR2
	.word	.LANCHOR2-1272
	.word	.LC40
	.fnend
	.size	ftl_map_blk_gc, .-ftl_map_blk_gc
	.align	2
	.global	Ftl_write_map_blk_to_last_page
	.type	Ftl_write_map_blk_to_last_page, %function
Ftl_write_map_blk_to_last_page:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	stmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, lr}
	.save {r4, r5, r6, r7, r8, r9, r10, lr}
	ldr	r5, .L2035
	ldr	r7, [r0, #12]
	ldr	r8, [r0, #24]
	ldr	r6, [r5, #-1276]
	cmp	r6, #0
	bne	.L2024
	ldrh	r3, [r0]
	movw	r2, #65535
	mov	r4, r0
	cmp	r3, r2
	bne	.L2025
	ldrh	r3, [r0, #8]
	add	r3, r3, #1
	strh	r3, [r0, #8]	@ movhi
	bl	FtlFreeSysBlkQueueOut
	strh	r0, [r7]	@ movhi
	ldr	r3, [r4, #28]
	strh	r6, [r4, #2]	@ movhi
	add	r3, r3, #1
	strh	r6, [r4]	@ movhi
	str	r3, [r4, #28]
	b	.L2024
.L2025:
	mov	r3, r3, asl #1
	sub	r10, r5, #1648
	mov	r1, #255
	ldrh	r9, [r7, r3]
	ldrh	r3, [r0, #2]
	ldr	r7, [r5, #-1440]
	orr	r3, r3, r9, asl #10
	str	r3, [r5, #-1268]
	ldr	r3, [r5, #-1472]
	str	r7, [r5, #-1260]
	str	r3, [r5, #-1264]
	ldr	r3, [r0, #28]
	str	r3, [r7, #4]
	ldr	r3, .L2035+4
	strh	r3, [r7, #8]	@ movhi
	ldrh	r3, [r0, #4]
	strh	r9, [r7, #2]	@ movhi
	strh	r3, [r7]	@ movhi
	ldrh	r2, [r10, #-14]
	ldr	r0, [r5, #-1472]
	mov	r2, r2, asl #3
	bl	ftl_memset
	mov	r2, r6
.L2026:
	ldrh	r1, [r4, #6]
	uxth	r3, r2
	cmp	r1, r3
	bls	.L2034
	ldr	r1, [r8, r3, asl #2]
	cmp	r9, r1, lsr #10
	bne	.L2027
	add	r6, r6, #1
	ldr	r1, [r5, #-1472]
	uxth	r6, r6
	str	r3, [r1, r6, asl #3]
	ldr	r1, [r8, r3, asl #2]
	ldr	r3, [r5, #-1472]
	add	r3, r3, r6, asl #3
	str	r1, [r3, #4]
.L2027:
	add	r2, r2, #1
	b	.L2026
.L2034:
	ldr	r3, .L2035+8
	ldrb	r3, [r3]	@ zero_extendqisi2
	cmp	r3, #0
	beq	.L2029
	ldr	r0, [r5, #-1264]
	ldrh	r1, [r10, #-6]
	bl	js_hash
	str	r0, [r7, #12]
.L2029:
	mov	r1, #1
	mov	r3, #0
	ldr	r0, .L2035+12
	mov	r2, r1
	bl	FlashProgPages
	ldrh	r3, [r4, #2]
	mov	r0, r4
	add	r3, r3, #1
	strh	r3, [r4, #2]	@ movhi
	bl	ftl_map_blk_gc
.L2024:
	mov	r0, #0
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, pc}
.L2036:
	.align	2
.L2035:
	.word	.LANCHOR2
	.word	-1291
	.word	.LANCHOR0
	.word	.LANCHOR2-1272
	.fnend
	.size	Ftl_write_map_blk_to_last_page, .-Ftl_write_map_blk_to_last_page
	.align	2
	.global	flush_l2p_region
	.type	flush_l2p_region, %function
flush_l2p_region:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	stmfd	sp!, {r3, r4, r5, lr}
	.save {r3, r4, r5, lr}
	mov	r4, #12
	ldr	r5, .L2039
	mul	r4, r4, r0
	add	r0, r5, #1024
	ldr	r3, [r5, #-1360]
	add	r0, r0, #8
	add	r2, r3, r4
	ldrh	r1, [r3, r4]
	ldr	r2, [r2, #8]
	bl	FtlMapWritePage
	ldr	r3, [r5, #-1360]
	mov	r0, #0
	add	r4, r3, r4
	ldr	r3, [r4, #4]
	bic	r3, r3, #-2147483648
	str	r3, [r4, #4]
	ldmfd	sp!, {r3, r4, r5, pc}
.L2040:
	.align	2
.L2039:
	.word	.LANCHOR2
	.fnend
	.size	flush_l2p_region, .-flush_l2p_region
	.align	2
	.global	select_l2p_ram_region
	.type	select_l2p_ram_region, %function
select_l2p_ram_region:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	stmfd	sp!, {r4, r5, r6, r7, lr}
	.save {r4, r5, r6, r7, lr}
	mov	r1, #0
	ldr	r4, .L2057
	mov	ip, #12
	movw	lr, #65535
	sub	r3, r4, #1616
	ldrh	r2, [r3, #-10]
	ldr	r3, [r4, #-1360]
.L2042:
	uxth	r0, r1
	cmp	r0, r2
	bcs	.L2054
	add	r1, r1, #1
	mla	r5, ip, r1, r3
	ldrh	r5, [r5, #-12]
	cmp	r5, lr
	bne	.L2042
	ldmfd	sp!, {r4, r5, r6, r7, pc}
.L2054:
	mov	r0, r2
	mov	r1, #0
	mov	ip, #-2147483648
	mov	r6, #12
.L2045:
	uxth	r5, r1
	cmp	r5, r2
	bcs	.L2055
	mla	lr, r6, r1, r3
	add	r1, r1, #1
	ldr	lr, [lr, #4]
	cmp	lr, ip
	mvn	r7, lr
	mov	r7, r7, lsr #31
	movcs	r7, #0
	cmp	r7, #0
	movne	ip, lr
	movne	r0, r5
	b	.L2045
.L2055:
	cmp	r0, r2
	ldmccfd	sp!, {r4, r5, r6, r7, pc}
	movw	r1, #1076
	mov	r0, r2
	ldrh	r5, [r4, r1]
	mvn	ip, #0
	mov	r1, #0
.L2048:
	uxth	lr, r1
	cmp	lr, r2
	bcs	.L2056
	ldr	r4, [r3, #4]
	cmp	r4, ip
	bcs	.L2049
	ldrh	r6, [r3]
	cmp	r6, r5
	movne	ip, r4
	movne	r0, lr
.L2049:
	add	r1, r1, #1
	add	r3, r3, #12
	b	.L2048
.L2056:
	ldmfd	sp!, {r4, r5, r6, r7, pc}
.L2058:
	.align	2
.L2057:
	.word	.LANCHOR2
	.fnend
	.size	select_l2p_ram_region, .-select_l2p_ram_region
	.align	2
	.global	log2phys
	.type	log2phys, %function
log2phys:
	.fnstart
	@ args = 0, pretend = 0, frame = 16
	@ frame_needed = 0, uses_anonymous_args = 0
	stmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.save {r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.pad #20
	sub	sp, sp, #20
	ldr	r6, .L2076
	sub	r3, r6, #1648
	str	r3, [sp, #8]
	ldr	ip, [r6, #-1280]
	ldrh	r10, [r3, #-8]
	cmp	r0, ip
	bcs	.L2060
	add	r10, r10, #7
	mov	fp, r6
	mov	r6, r0, lsr r10
	sub	r3, fp, #1616
	str	r2, [sp, #12]
	mov	r9, r1
	ldrh	r2, [r3, #-10]
	uxth	r6, r6
	str	r0, [sp, #4]
	mov	r3, #0
	ldr	r4, [fp, #-1360]
	mov	r1, #12
	b	.L2061
.L2060:
	cmp	r2, #0
	mvn	r0, #0
	streq	r0, [r1]
	b	.L2062
.L2066:
	add	r3, r3, #1
	mla	r0, r1, r3, r4
	ldrh	r0, [r0, #-12]
	cmp	r0, r6
	beq	.L2063
.L2061:
	uxth	r7, r3
	cmp	r7, r2
	bcc	.L2066
	bl	select_l2p_ram_region
	mov	r5, #12
	movw	r2, #65535
	mul	r5, r5, r0
	mov	r7, r0
	add	r3, r4, r5
	ldrh	r1, [r4, r5]
	cmp	r1, r2
	bne	.L2075
.L2067:
	ldr	r3, [fp, #-1372]
	ldr	r4, .L2076
	ldr	r8, [r3, r6, asl #2]
	cmp	r8, #0
	bne	.L2068
	ldr	r2, [r4, #-1360]
	mov	r1, #255
	ldr	r3, [sp, #8]
	add	r2, r2, r5
	ldr	r0, [r2, #8]
	ldrh	r2, [r3, #-6]
	bl	ftl_memset
	ldr	r2, [r4, #-1360]
	strh	r6, [r2, r5]	@ movhi
	ldr	r2, [r4, #-1360]
	add	r5, r2, r5
	str	r8, [r5, #4]
.L2063:
	ldr	r3, [sp, #4]
	mvn	r2, #0
	bic	r10, r3, r2, asl r10
	ldr	r3, [sp, #12]
	cmp	r3, #0
	uxth	r10, r10
	mov	r3, #12
	bne	.L2064
	ldr	r2, [fp, #-1360]
	mla	r3, r3, r7, r2
	ldr	r3, [r3, #8]
	ldr	r3, [r3, r10, asl #2]
	str	r3, [r9]
	b	.L2065
.L2064:
	mul	r3, r3, r7
	ldr	r2, [fp, #-1360]
	ldr	r1, [r9]
	add	r2, r2, r3
	ldr	r2, [r2, #8]
	str	r1, [r2, r10, asl #2]
	ldr	r2, [fp, #-1360]
	add	r3, r2, r3
	ldr	r2, [r3, #4]
	orr	r2, r2, #-2147483648
	str	r2, [r3, #4]
	movw	r3, #1076
	strh	r6, [fp, r3]	@ movhi
.L2065:
	ldr	r2, [fp, #-1360]
	mov	r3, #12
	mov	r0, #0
	mla	r7, r3, r7, r2
	ldr	r3, [r7, #4]
	cmn	r3, #1
	addne	r3, r3, #1
	strne	r3, [r7, #4]
	b	.L2062
.L2075:
	ldr	r3, [r3, #4]
	cmp	r3, #0
	bge	.L2067
	bl	flush_l2p_region
	b	.L2067
.L2068:
	ldr	r2, [r4, #-1360]
	mov	r1, #1
	ldr	r0, .L2076+4
	add	r2, r2, r5
	str	r8, [r4, #-1268]
	ldr	r2, [r2, #8]
	str	r2, [r4, #-1264]
	ldr	r2, [r4, #-1440]
	str	r2, [r4, #-1260]
	mov	r2, r1
	bl	FlashReadPages
	ldr	r2, [r4, #-1260]
	ldrh	r2, [r2, #8]
	cmp	r2, r6
	beq	.L2069
	mov	r1, r6
	mov	r2, r8
	ldr	r0, .L2076+8
	bl	printk
	mov	r2, #4
	mov	r3, r2
	ldr	r0, .L2076+12
	ldr	r1, [r4, #-1260]
	bl	rknand_print_hex
	sub	r3, r4, #1616
	ldr	r0, .L2076+16
	mov	r2, #4
	ldrh	r3, [r3, #-12]
	ldr	r1, [r4, #-1372]
	bl	rknand_print_hex
	mov	r3, #1
	str	r3, [r4, #-1276]
	b	.L2070
.L2069:
	ldr	r2, [r4, #-1272]
	cmp	r2, #256
	bne	.L2070
	mov	r1, r6
	mov	r2, r8
	ldr	r0, .L2076+20
	bl	printk
	ldr	r3, [r4, #-1360]
	ldr	r0, .L2076+24
	mov	r1, r6
	add	r3, r3, r5
	ldr	r2, [r3, #8]
	bl	FtlMapWritePage
.L2070:
	ldr	r3, [fp, #-1360]
	mov	r1, #0
	add	r2, r3, r5
	str	r1, [r2, #4]
	strh	r6, [r3, r5]	@ movhi
	b	.L2063
.L2062:
	add	sp, sp, #20
	@ sp needed
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L2077:
	.align	2
.L2076:
	.word	.LANCHOR2
	.word	.LANCHOR2-1272
	.word	.LC41
	.word	.LC17
	.word	.LC42
	.word	.LC43
	.word	.LANCHOR2+1032
	.fnend
	.size	log2phys, .-log2phys
	.align	2
	.global	FtlVendorPartWrite
	.type	FtlVendorPartWrite, %function
FtlVendorPartWrite:
	.fnstart
	@ args = 0, pretend = 0, frame = 56
	@ frame_needed = 0, uses_anonymous_args = 0
	stmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.save {r4, r5, r6, r7, r8, r9, r10, fp, lr}
	mov	fp, r2
	ldr	r6, .L2089
	add	r2, r0, r1
	mov	r7, r0
	.pad #60
	sub	sp, sp, #60
	mov	r4, r1
	ldrh	r3, [r6, #-20]
	ldrh	r5, [r6, #-8]
	cmp	r2, r3
	mvnhi	r0, #0
	bhi	.L2079
	mov	r5, r7, lsr r5
	mov	r3, #0
	str	r3, [sp]
	add	r3, r6, #1648
	mov	r10, r5, asl #2
	str	r3, [sp, #4]
	mov	r8, r3
.L2080:
	cmp	r4, #0
	beq	.L2088
	ldr	r3, [sp, #4]
	mov	r0, r7
	ldr	r3, [r3, #-1376]
	ldr	ip, [r3, r10]
	ldr	r3, .L2089+4
	str	ip, [sp, #12]
	ldrh	r2, [r3]
	mov	r1, r2
	str	r2, [sp, #8]
	bl	__aeabi_uidivmod
	ldr	r2, [sp, #8]
	mov	r9, r1
	ldr	ip, [sp, #12]
	rsb	r3, r1, r2
	uxth	r6, r3
	cmp	r6, r4
	uxthhi	r6, r4
	cmp	r6, r2
	cmpne	ip, #0
	movne	r1, #1
	moveq	r1, #0
	beq	.L2082
	ldr	r2, [r8, #-1464]
	mov	r1, #1
	add	r0, sp, #20
	str	ip, [sp, #24]
	str	r2, [sp, #28]
	mov	r2, #0
	str	r2, [sp, #32]
	mov	r2, r1
	bl	FlashReadPages
	b	.L2083
.L2082:
	ldr	r2, .L2089+8
	ldr	r0, [r8, #-1464]
	ldrh	r2, [r2]
	bl	ftl_memset
.L2083:
	mov	ip, r6, asl #9
	ldr	r0, [r8, #-1464]
	uxth	r9, r9
	mov	r1, fp
	mov	r2, ip
	str	ip, [sp, #8]
	add	r0, r0, r9, asl #9
	rsb	r4, r6, r4
	bl	ftl_memcpy
	mov	r1, r5
	ldr	r0, .L2089+12
	add	r5, r5, #1
	ldr	r2, [r8, #-1464]
	add	r7, r7, r6
	bl	FtlMapWritePage
	add	r10, r10, #4
	ldr	r3, [sp]
	cmn	r0, #1
	ldr	ip, [sp, #8]
	mvneq	r3, #0
	add	fp, fp, ip
	str	r3, [sp]
	b	.L2080
.L2088:
	ldr	r0, [sp]
.L2079:
	add	sp, sp, #60
	@ sp needed
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L2090:
	.align	2
.L2089:
	.word	.LANCHOR2-1648
	.word	.LANCHOR2-1658
	.word	.LANCHOR2-1654
	.word	.LANCHOR2+1080
	.fnend
	.size	FtlVendorPartWrite, .-FtlVendorPartWrite
	.align	2
	.global	FtlVendorPartRead
	.type	FtlVendorPartRead, %function
FtlVendorPartRead:
	.fnstart
	@ args = 0, pretend = 0, frame = 56
	@ frame_needed = 0, uses_anonymous_args = 0
	stmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.save {r4, r5, r6, r7, r8, r9, r10, fp, lr}
	mov	r9, r2
	ldr	r6, .L2103
	add	r2, r0, r1
	mov	r8, r0
	.pad #60
	sub	sp, sp, #60
	mov	r7, r1
	ldrh	r3, [r6, #-20]
	ldrh	r5, [r6, #-8]
	cmp	r2, r3
	mvnhi	r0, #0
	bhi	.L2092
	add	r6, r6, #1648
	mov	r5, r8, lsr r5
	mov	r10, r6
	mov	r3, r5, asl #2
	str	r3, [sp, #4]
	mov	r3, #0
	str	r3, [sp]
.L2093:
	cmp	r7, #0
	beq	.L2102
	ldr	r2, [sp, #4]
	mov	r0, r8
	ldr	r3, [r6, #-1376]
	ldr	r3, [r3, r2]
	str	r3, [sp, #12]
	ldr	r3, .L2103+4
	ldrh	r4, [r3]
	mov	r1, r4
	bl	__aeabi_uidivmod
	rsb	r4, r1, r4
	ldr	r3, [sp, #12]
	str	r1, [sp, #8]
	uxth	r4, r4
	cmp	r4, r7
	uxthhi	r4, r7
	cmp	r3, #0
	mov	fp, r4, asl #9
	beq	.L2095
	ldr	r2, [r10, #-1464]
	mov	r1, #1
	add	r0, sp, #20
	str	r3, [sp, #24]
	str	r3, [sp, #12]
	str	r2, [sp, #28]
	mov	r2, #0
	str	r2, [sp, #32]
	mov	r2, r1
	bl	FlashReadPages
	ldr	r2, [sp, #20]
	ldr	r3, [sp]
	cmn	r2, #1
	ldr	r2, [r10, #-1272]
	mvneq	r3, #0
	cmp	r2, #256
	str	r3, [sp]
	ldr	r3, [sp, #12]
	bne	.L2097
	mov	r1, r5
	mov	r2, r3
	ldr	r0, .L2103+8
	bl	printk
	ldr	r0, .L2103+12
	mov	r1, r5
	ldr	r2, [r6, #-1464]
	bl	FtlMapWritePage
.L2097:
	ldrh	r3, [sp, #8]
	mov	r0, r9
	ldr	r1, [r10, #-1464]
	mov	r2, fp
	add	r1, r1, r3, asl #9
	bl	ftl_memcpy
	b	.L2098
.L2095:
	mov	r0, r9
	mov	r1, r3
	mov	r2, fp
	bl	ftl_memset
.L2098:
	ldr	r3, [sp, #4]
	add	r5, r5, #1
	rsb	r7, r4, r7
	add	r8, r8, r4
	add	r3, r3, #4
	add	r9, r9, fp
	str	r3, [sp, #4]
	b	.L2093
.L2102:
	ldr	r0, [sp]
.L2092:
	add	sp, sp, #60
	@ sp needed
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L2104:
	.align	2
.L2103:
	.word	.LANCHOR2-1648
	.word	.LANCHOR2-1658
	.word	.LC44
	.word	.LANCHOR2+1080
	.fnend
	.size	FtlVendorPartRead, .-FtlVendorPartRead
	.align	2
	.global	FtlUpdateVaildLpn
	.type	FtlUpdateVaildLpn, %function
FtlUpdateVaildLpn:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	ldr	ip, .L2113
	movw	r1, #1124
	ldrh	r2, [ip, r1]
	cmp	r2, #4
	cmpls	r0, #0
	addeq	r2, r2, #1
	streqh	r2, [ip, r1]	@ movhi
	bxeq	lr
	str	lr, [sp, #-4]!
	.save {lr}
	mov	r2, #0
	strh	r2, [ip, r1]	@ movhi
	sub	r1, ip, #1712
	str	r2, [ip, #1128]
	mov	r3, ip
	ldrh	r0, [r1, #-12]
	ldr	r2, [ip, #-1404]
	movw	ip, #65535
	add	r0, r2, r0, asl #1
.L2107:
	cmp	r2, r0
	beq	.L2112
	ldrh	r1, [r2], #2
	cmp	r1, ip
	ldrne	lr, [r3, #1128]
	addne	r1, r1, lr
	strne	r1, [r3, #1128]
	b	.L2107
.L2112:
	ldr	pc, [sp], #4
.L2114:
	.align	2
.L2113:
	.word	.LANCHOR2
	.fnend
	.size	FtlUpdateVaildLpn, .-FtlUpdateVaildLpn
	.align	2
	.global	FtlMapBlkWriteDumpData
	.type	FtlMapBlkWriteDumpData, %function
FtlMapBlkWriteDumpData:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	stmfd	sp!, {r4, r5, r6, lr}
	.save {r4, r5, r6, lr}
	ldr	r2, [r0, #36]
	ldrh	r5, [r0, #6]
	cmp	r2, #0
	ldr	r3, [r0, #24]
	ldmeqfd	sp!, {r4, r5, r6, pc}
	ldr	r4, .L2122
	mov	r2, #0
	str	r2, [r0, #36]
	ldr	r2, [r4, #-1276]
	cmp	r2, #0
	ldmnefd	sp!, {r4, r5, r6, pc}
	sub	r5, r5, #1
	mov	r6, r0
	ldr	r2, [r4, #-1440]
	ldr	r0, [r4, #-1468]
	uxth	r5, r5
	str	r2, [r4, #-1260]
	str	r0, [r4, #-1264]
	ldr	r3, [r3, r5, asl #2]
	cmp	r3, #0
	str	r3, [r4, #-1268]
	beq	.L2119
	mov	r1, #1
	ldr	r0, .L2122+4
	mov	r2, r1
	bl	FlashReadPages
	b	.L2120
.L2119:
	sub	r3, r4, #1648
	mov	r1, #255
	ldrh	r2, [r3, #-6]
	bl	ftl_memset
.L2120:
	mov	r0, r6
	mov	r1, r5
	ldr	r2, [r4, #-1264]
	ldmfd	sp!, {r4, r5, r6, lr}
	b	FtlMapWritePage
.L2123:
	.align	2
.L2122:
	.word	.LANCHOR2
	.word	.LANCHOR2-1272
	.fnend
	.size	FtlMapBlkWriteDumpData, .-FtlMapBlkWriteDumpData
	.align	2
	.global	FtlVpcTblFlush
	.type	FtlVpcTblFlush, %function
FtlVpcTblFlush:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	stmfd	sp!, {r3, r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.save {r3, r4, r5, r6, r7, r8, r9, r10, fp, lr}
	ldr	r4, .L2142
	ldr	r3, [r4, #-1276]
	cmp	r3, #0
	bne	.L2126
	ldr	r2, [r4, #-1472]
	add	r5, r4, #820
	ldr	r6, [r4, #-1440]
	mov	r1, #255
	ldr	r7, .L2142+4
	str	r2, [r4, #-1264]
	movw	r2, #1132
	ldrh	r2, [r4, r2]
	str	r6, [r4, #-1260]
	str	r3, [r6, #12]
	strh	r2, [r6, #2]	@ movhi
	ldr	r2, .L2142+8
	strh	r2, [r6]	@ movhi
	ldr	r2, [r4, #1140]
	stmib	r6, {r2, r3}
	ldr	r3, .L2142+12
	str	r3, [r4, #820]
	ldr	r3, .L2142+16
	str	r3, [r4, #824]
	ldrh	r3, [r7, #6]
	strh	r3, [r5, #8]	@ movhi
	sub	r3, r4, #1696
	ldrh	r3, [r3, #-14]
	strb	r3, [r4, #830]
	add	r3, r4, #888
	ldrh	r2, [r3]
	strh	r2, [r5, #14]	@ movhi
	ldrh	r2, [r3, #2]
	ldrb	r3, [r4, #894]	@ zero_extendqisi2
	orr	r3, r3, r2, asl #6
	strh	r3, [r5, #16]	@ movhi
	ldrb	r3, [r4, #896]	@ zero_extendqisi2
	strb	r3, [r4, #831]
	add	r3, r4, #936
	ldrh	r2, [r3]
	strh	r2, [r5, #18]	@ movhi
	ldrh	r2, [r3, #2]
	ldrb	r3, [r4, #942]	@ zero_extendqisi2
	orr	r3, r3, r2, asl #6
	strh	r3, [r5, #20]	@ movhi
	ldrb	r3, [r4, #944]	@ zero_extendqisi2
	strb	r3, [r4, #832]
	add	r3, r4, #984
	ldrh	r2, [r3]
	strh	r2, [r5, #22]	@ movhi
	ldrh	r2, [r3, #2]
	ldrb	r3, [r4, #990]	@ zero_extendqisi2
	ldr	r0, [r4, #-1264]
	orr	r3, r3, r2, asl #6
	strh	r3, [r5, #24]	@ movhi
	ldrb	r3, [r4, #992]	@ zero_extendqisi2
	strb	r3, [r4, #833]
	ldr	r3, [r4, #-1580]
	str	r3, [r4, #852]
	ldr	r3, [r4, #-1612]
	str	r3, [r4, #860]
	ldr	r3, [r4, #-1608]
	str	r3, [r4, #856]
	sub	r3, r4, #1536
	ldrh	r3, [r3]
	strh	r3, [r5, #44]	@ movhi
	sub	r3, r4, #1520
	ldrh	r3, [r3, #-14]
	strh	r3, [r5, #46]	@ movhi
	sub	r3, r4, #1648
	ldrh	r2, [r3, #-6]
	bl	ftl_memset
	mov	r1, r5
	sub	r5, r4, #1712
	mov	r2, #48
	ldr	r0, [r4, #-1264]
	bl	ftl_memcpy
	ldrh	r2, [r5, #-12]
	ldr	r0, [r4, #-1264]
	ldr	r1, [r4, #-1404]
	mov	r2, r2, asl #1
	add	r0, r0, #48
	bl	ftl_memcpy
	ldrh	r2, [r5, #-12]
	ldr	r0, [r4, #-1264]
	add	r3, r2, #24
	ldr	r1, [r4, #-1392]
	ubfx	r3, r3, #1, #30
	mov	r2, r2, lsr #3
	add	r2, r2, #4
	add	r0, r0, r3, asl #2
	bl	ftl_memcpy
	sub	r2, r4, #1616
	ldrh	r3, [r2, #-8]
	cmp	r3, #0
	beq	.L2127
	ldrh	r1, [r5, #-12]
	ldrh	r2, [r2, #-12]
	ldr	r0, [r4, #-1264]
	mov	r3, r1, lsr #3
	add	r3, r3, r1, asl #1
	mov	r2, r2, asl #2
	add	r3, r3, #52
	ldr	r1, [r4, #-1372]
	ubfx	r3, r3, #2, #14
	add	r0, r0, r3, asl #2
	bl	ftl_memcpy
.L2127:
	mov	r0, #0
	ldr	r9, .L2142
	bl	FtlUpdateVaildLpn
	ldr	fp, .L2142+4
	mov	r8, #0
	movw	r10, #65535
.L2128:
	ldr	r3, [r4, #-1472]
	ldrh	r2, [r7]
	ldrh	r1, [r7, #2]
	str	r3, [r4, #-1264]
	ldr	r3, [r4, #-1440]
	str	r3, [r4, #-1260]
	orr	r3, r1, r2, asl #10
	str	r3, [r4, #-1268]
	ldr	r3, .L2142+20
	ldrh	r3, [r3]
	sub	r3, r3, #1
	cmp	r1, r3
	blt	.L2129
	mov	r3, #0
	ldrh	r10, [fp, #4]
	strh	r3, [fp, #2]	@ movhi
	strh	r2, [fp, #4]	@ movhi
	bl	FtlFreeSysBlkQueueOut
	ldr	r3, [r9, #-1612]
	add	r2, r3, #1
	str	r2, [r9, #-1612]
	str	r3, [r9, #1140]
	mov	r2, r0, asl #10
	strh	r0, [fp]	@ movhi
	str	r2, [r9, #-1268]
	str	r3, [r6, #4]
	strh	r0, [r6, #2]	@ movhi
.L2129:
	ldr	r3, .L2142+24
	ldrb	r3, [r3]	@ zero_extendqisi2
	cmp	r3, #0
	beq	.L2130
	ldr	r3, .L2142+28
	ldr	r0, [r4, #-1472]
	ldrh	r1, [r3]
	bl	js_hash
	str	r0, [r6, #12]
.L2130:
	mov	r1, #1
	ldr	r0, .L2142+32
	mov	r3, r1
	mov	r2, r1
	bl	FlashProgPages
	ldrh	r5, [r7, #2]
	ldr	r3, [r4, #-1272]
	add	r5, r5, #1
	cmn	r3, #1
	uxth	r5, r5
	strh	r5, [r7, #2]	@ movhi
	bne	.L2131
	cmp	r5, #1
	add	r8, r8, #1
	ldreq	r3, .L2142+20
	uxth	r8, r8
	ldreqh	r3, [r3]
	subeq	r3, r3, #1
	streqh	r3, [fp, #2]	@ movhi
	cmp	r8, #3
	bls	.L2128
	ldr	r0, .L2142+36
	mov	r2, r8
	ldr	r1, [r4, #-1268]
	bl	printk
	mov	r3, #1
	str	r3, [r4, #-1276]
	b	.L2126
.L2131:
	cmp	r3, #256
	cmpne	r5, #1
	beq	.L2128
	movw	r3, #65535
	cmp	r10, r3
	beq	.L2126
	mov	r0, r10
	mov	r1, #1
	bl	FtlFreeSysBlkQueueIn
.L2126:
	mov	r0, #0
	ldmfd	sp!, {r3, r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L2143:
	.align	2
.L2142:
	.word	.LANCHOR2
	.word	.LANCHOR2+1132
	.word	-3932
	.word	1179929683
	.word	1342177376
	.word	.LANCHOR2-1662
	.word	.LANCHOR0
	.word	.LANCHOR2-1654
	.word	.LANCHOR2-1272
	.word	.LC45
	.fnend
	.size	FtlVpcTblFlush, .-FtlVpcTblFlush
	.align	2
	.global	FtlScanSysBlk
	.type	FtlScanSysBlk, %function
FtlScanSysBlk:
	.fnstart
	@ args = 0, pretend = 0, frame = 24
	@ frame_needed = 0, uses_anonymous_args = 0
	stmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.save {r4, r5, r6, r7, r8, r9, r10, fp, lr}
	movw	r3, #1148
	ldr	r4, .L2230
	mov	r5, #0
	mov	r1, r5
	.pad #28
	sub	sp, sp, #28
	sub	r6, r4, #1632
	ldr	r2, [r4, #-1636]
	mov	r8, r4
	strh	r5, [r4, r3]	@ movhi
	sub	r3, r4, #1280
	ldr	r0, [r4, #-1364]
	mov	r2, r2, asl #2
	strh	r5, [r3, #-4]	@ movhi
	bl	ftl_memset
	ldr	r2, [r4, #-1636]
	mov	r1, r5
	ldr	r0, [r4, #-1400]
	mov	r2, r2, asl #1
	bl	ftl_memset
	ldrh	r2, [r6, #-12]
	mov	r1, r5
	ldr	r0, [r4, #-1380]
	mov	r2, r2, asl #2
	bl	ftl_memset
	ldrh	r2, [r6, #-12]
	mov	r1, r5
	ldr	r0, [r4, #-1388]
	mov	r5, r4
	mov	r2, r2, asl #1
	bl	ftl_memset
	ldr	r0, .L2230+4
	mov	r1, #255
	mov	r2, #16
	bl	ftl_memset
	sub	r3, r4, #1712
	ldrh	r3, [r3, #-12]
	str	r3, [sp]
.L2145:
	ldr	r3, .L2230+8
	ldr	r2, [sp]
	ldr	r1, .L2230
	ldrh	r3, [r3]
	cmp	r3, r2
	bls	.L2186
	ldr	r1, [r5, #-1432]
	mov	r6, #0
	ldr	r3, .L2230+12
	mov	fp, r6
	ldr	r2, [r5, #-1500]
	mov	r9, #36
	str	r1, [sp, #4]
	ldr	r1, .L2230+16
	ldrh	ip, [r3]
	ldr	r3, [r5, #-1460]
	sub	r7, r1, #52
	ldrh	r10, [r1]
.L2187:
	uxth	r1, r6
	cmp	r1, ip
	bcs	.L2225
	ldr	r1, [sp]
	ldrb	r0, [r7, r6]	@ zero_extendqisi2
	str	r3, [sp, #20]
	str	r2, [sp, #16]
	str	ip, [sp, #12]
	bl	V2P_block
	str	r0, [sp, #8]
	bl	FtlBbmIsBadBlock
	cmp	r0, #0
	ldr	r1, [sp, #8]
	ldr	ip, [sp, #12]
	ldr	r2, [sp, #16]
	ldr	r3, [sp, #20]
	bne	.L2146
	mla	r0, r9, fp, r2
	mov	r1, r1, asl #10
	stmib	r0, {r1, r3}
	mul	r1, r10, fp
	add	lr, r1, #3
	cmp	r1, #0
	movlt	r1, lr
	ldr	lr, [sp, #4]
	bic	r1, r1, #3
	add	r1, lr, r1
	str	r1, [r0, #12]
	add	r1, fp, #1
	uxth	fp, r1
.L2146:
	add	r6, r6, #1
	b	.L2187
.L2225:
	cmp	fp, #0
	beq	.L2149
	ldr	r0, [r4, #-1500]
	mov	r1, fp
	mov	r2, #1
	bl	FlashReadPages
	mov	r3, #0
.L2223:
	str	r3, [sp, #4]
	ldrh	r3, [sp, #4]
	cmp	r3, fp
	bcs	.L2149
	ldr	r3, [sp, #4]
	mov	r9, #36
	mul	r9, r9, r3
	ldr	r3, [r4, #-1500]
	add	r2, r3, r9
	ldr	r3, [r3, r9]
	ldr	r6, [r2, #4]
	cmn	r3, #1
	ldr	r7, [r2, #12]
	ubfx	r6, r6, #10, #16
	bne	.L2152
	mov	r10, #16
	movw	ip, #65535
.L2151:
	ldr	r0, [r4, #-1500]
	mov	r1, #1
	mov	r2, r1
	str	ip, [sp, #8]
	add	r0, r0, r9
	ldr	r3, [r0, #4]
	add	r3, r3, #1
	str	r3, [r0, #4]
	bl	FlashReadPages
	ldrh	r3, [r7]
	ldr	ip, [sp, #8]
	cmp	r3, ip
	ldreq	r3, [r8, #-1500]
	mvneq	r2, #0
	streq	r2, [r3, r9]
	beq	.L2152
.L2153:
	ldr	r3, [r5, #-1500]
	ldr	r3, [r3, r9]
	cmn	r3, #1
	bne	.L2152
	sub	r10, r10, #1
	uxth	r10, r10
	cmp	r10, #0
	bne	.L2151
.L2152:
	ldr	r3, [r5, #-1500]
	ldr	r3, [r3, r9]
	cmn	r3, #1
	beq	.L2155
	ldr	r2, [r5, #-1612]
	ldr	r3, [r7, #4]
	cmn	r2, #1
	beq	.L2156
	cmp	r2, r3
	bhi	.L2157
.L2156:
	cmn	r3, #1
	addne	r2, r3, #1
	strne	r2, [r8, #-1612]
.L2157:
	ldrh	r2, [r7]
	movw	r1, #61604
	cmp	r2, r1
	beq	.L2159
	bhi	.L2160
	movw	r3, #61574
	cmp	r2, r3
	bne	.L2158
	ldr	ip, .L2230+20
	ldr	r9, [r4, #-1380]
	add	r2, ip, #360
	ldrh	lr, [ip]
	ldrh	r1, [r2]
	sub	r0, lr, #1
	rsb	r2, r1, r0
	str	r2, [sp, #8]
	uxth	r3, r0
	b	.L2173
.L2160:
	movw	r3, #61634
	cmp	r2, r3
	beq	.L2162
	movw	r3, #65535
	cmp	r2, r3
	moveq	r0, r6
	beq	.L2224
	b	.L2158
.L2162:
	ldr	lr, [r4, #-1636]
	ldr	ip, .L2230+24
	ldr	r9, [r4, #-1364]
	uxth	r1, lr
	ldrh	r2, [ip]
	sub	r3, r1, #1
	rsb	r1, r2, r1
	uxth	r3, r3
	sub	r1, r1, #1
	sxth	r1, r1
	str	r1, [sp, #12]
.L2164:
	ldr	r1, [sp, #12]
	sxth	r0, r3
	cmp	r0, r1
	ble	.L2226
	mov	r10, r0, asl #2
	ldr	r1, [r7, #4]
	str	r10, [sp, #8]
	ldr	r10, [r9, r0, asl #2]
	cmp	r1, r10
	bls	.L2165
	ldr	r1, [r9]
	cmp	r1, #0
	bne	.L2166
	cmp	r2, lr
	addne	r2, r2, #1
	strneh	r2, [ip]	@ movhi
.L2166:
	uxth	r9, r3
	mov	r1, #0
.L2167:
	uxth	r2, r1
	cmp	r2, r9
	bcs	.L2227
	ldr	lr, [r5, #-1364]
	sxth	r2, r2
	add	r1, r1, #1
	add	r10, lr, r2, asl #2
	ldr	r10, [r10, #4]
	str	r10, [lr, r2, asl #2]
	mov	r2, r2, asl #1
	ldr	lr, [r5, #-1400]
	add	r10, lr, r2
	ldrh	r10, [r10, #2]
	strh	r10, [lr, r2]	@ movhi
	b	.L2167
.L2227:
	ldr	r2, [r8, #-1364]
	mov	r0, r0, asl #1
	ldr	r1, [r7, #4]
	ldr	lr, [sp, #8]
	str	r1, [r2, lr]
	ldr	r2, [r8, #-1400]
	strh	r6, [r2, r0]	@ movhi
	sxth	r0, r3
	cmp	r0, #0
	bge	.L2169
	b	.L2158
.L2165:
	sub	r3, r3, #1
	uxth	r3, r3
	b	.L2164
.L2226:
	cmp	r0, #0
	bge	.L2200
	b	.L2158
.L2169:
	ldrh	r2, [ip]
	ldr	r1, [r5, #-1636]
	rsb	r1, r2, r1
	sub	r1, r1, #1
	sxth	r1, r1
	cmp	r0, r1
	bgt	.L2158
.L2200:
	add	r2, r2, #1
	ldr	r1, [r7, #4]
	strh	r2, [ip]	@ movhi
	sxth	r3, r3
	ldr	r2, [r4, #-1364]
	str	r1, [r2, r3, asl #2]
	mov	r3, r3, asl #1
	ldr	r2, [r4, #-1400]
	b	.L2221
.L2179:
	mov	r10, r2, asl #2
	ldr	r0, [r7, #4]
	str	r10, [sp, #12]
	ldr	r10, [r9, r2, asl #2]
	cmp	r0, r10
	bhi	.L2228
	sub	r3, r3, #1
	uxth	r3, r3
.L2173:
	ldr	r0, [sp, #8]
	sxth	r2, r3
	cmp	r2, r0
	bgt	.L2179
	b	.L2178
.L2228:
	ldr	r0, [r9]
	cmp	r0, #0
	bne	.L2175
	cmp	r1, lr
	addne	r1, r1, #1
	ldrne	r0, .L2230+28
	strneh	r1, [r0]	@ movhi
.L2175:
	uxth	r9, r3
	mov	r0, #0
.L2176:
	uxth	r1, r0
	cmp	r1, r9
	bcs	.L2229
	ldr	lr, [r5, #-1380]
	sxth	r1, r1
	add	r0, r0, #1
	add	r10, lr, r1, asl #2
	ldr	r10, [r10, #4]
	str	r10, [lr, r1, asl #2]
	mov	r1, r1, asl #1
	ldr	lr, [r5, #-1388]
	add	r10, lr, r1
	ldrh	r10, [r10, #2]
	strh	r10, [lr, r1]	@ movhi
	b	.L2176
.L2229:
	ldr	r1, [r8, #-1380]
	mov	r2, r2, asl #1
	ldr	r0, [r7, #4]
	ldr	lr, [sp, #12]
	str	r0, [r1, lr]
	ldr	r1, [r8, #-1388]
	strh	r6, [r1, r2]	@ movhi
.L2178:
	sxth	r3, r3
	cmp	r3, #0
	blt	.L2158
	ldr	r1, .L2230+28
	ldrh	ip, [ip]
	ldrh	r2, [r1]
	sub	ip, ip, #1
	rsb	ip, r2, ip
	sxth	ip, ip
	cmp	r3, ip
	bgt	.L2158
	add	r2, r2, #1
	strh	r2, [r1]	@ movhi
	ldr	r2, [r5, #-1380]
	ldr	r1, [r7, #4]
	str	r1, [r2, r3, asl #2]
	mov	r3, r3, asl #1
	ldr	r2, [r5, #-1388]
.L2221:
	strh	r6, [r2, r3]	@ movhi
	b	.L2158
.L2159:
	ldr	r2, .L2230+4
	ldr	r9, .L2230+4
	ldrh	r1, [r2]
	movw	r2, #65535
	cmp	r1, r2
	moveq	r2, r9
	streqh	r6, [r2]	@ movhi
	beq	.L2222
	ldr	r3, .L2230+4
	ldrh	r0, [r3, #4]
	cmp	r0, r2
	beq	.L2181
	mov	r1, #1
	bl	FtlFreeSysBlkQueueIn
.L2181:
	ldr	r3, [r7, #4]
	ldr	r2, [r8, #1140]
	cmp	r2, r3
	strcsh	r6, [r9, #4]	@ movhi
	bcs	.L2158
	ldrh	r3, [r9]
	strh	r6, [r9]	@ movhi
	strh	r3, [r9, #4]	@ movhi
	ldr	r3, [r7, #4]
.L2222:
	str	r3, [r5, #1140]
	b	.L2158
.L2155:
	ldrb	r1, [r5, #-2744]	@ zero_extendqisi2
	mov	r0, r6
	cmp	r1, #0
	beq	.L2183
.L2224:
	mov	r1, #0
.L2183:
	bl	FtlFreeSysBlkQueueIn
.L2158:
	ldr	r3, [sp, #4]
	add	r3, r3, #1
	b	.L2223
.L2149:
	ldr	r3, [sp]
	add	r6, r3, #1
	uxth	r3, r6
	str	r3, [sp]
	b	.L2145
.L2186:
	ldr	ip, [r1, #-1400]
	ldrh	r2, [ip]
	cmp	r2, #0
	beq	.L2188
.L2191:
	ldr	r0, [r1, #-1388]
	ldrh	r2, [r0]
	cmp	r2, #0
	beq	.L2189
	b	.L2212
.L2188:
	movw	r3, #1148
	ldrh	r3, [r1, r3]
	cmp	r3, #0
	ldrne	lr, [r1, #-1636]
	beq	.L2191
.L2192:
	uxth	r3, r2
	sxth	r0, r3
	cmp	r0, lr
	bcs	.L2191
	mov	r4, r0, asl #1
	add	r2, r2, #1
	ldrh	r4, [ip, r4]
	cmp	r4, #0
	beq	.L2192
	ldr	ip, .L2230
	mov	r6, #0
.L2193:
	ldr	lr, [r1, #-1636]
	sxth	r2, r3
	cmp	r2, lr
	bcs	.L2191
	ldr	r4, [ip, #-1400]
	mov	lr, r2, asl #1
	rsb	r5, r0, r2
	add	r3, r3, #1
	ldrh	r8, [r4, lr]
	mov	r7, r5, asl #1
	uxth	r3, r3
	strh	r8, [r4, r7]	@ movhi
	ldr	r4, [ip, #-1364]
	ldr	r2, [r4, r2, asl #2]
	str	r2, [r4, r5, asl #2]
	ldr	r2, [ip, #-1400]
	strh	r6, [r2, lr]	@ movhi
	b	.L2193
.L2189:
	ldr	r3, .L2230+32
	ldrh	r1, [r3, #-4]
	cmp	r1, #0
	subne	r3, r3, #352
	ldrneh	ip, [r3, #-12]
	beq	.L2212
.L2196:
	uxth	r3, r2
	sxth	r1, r3
	cmp	r1, ip
	bge	.L2212
	mov	lr, r1, asl #1
	add	r2, r2, #1
	ldrh	lr, [r0, lr]
	cmp	lr, #0
	beq	.L2196
	ldr	r0, .L2230
	mov	r6, #0
	ldr	r5, .L2230+20
.L2197:
	ldrh	ip, [r5]
	sxth	r2, r3
	cmp	r2, ip
	bge	.L2212
	ldr	lr, [r0, #-1388]
	mov	ip, r2, asl #1
	rsb	r4, r1, r2
	add	r3, r3, #1
	ldrh	r8, [lr, ip]
	mov	r7, r4, asl #1
	uxth	r3, r3
	strh	r8, [lr, r7]	@ movhi
	ldr	lr, [r0, #-1380]
	ldr	r2, [lr, r2, asl #2]
	str	r2, [lr, r4, asl #2]
	ldr	r2, [r0, #-1388]
	strh	r6, [r2, ip]	@ movhi
	b	.L2197
.L2212:
	mov	r0, #0
	add	sp, sp, #28
	@ sp needed
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L2231:
	.align	2
.L2230:
	.word	.LANCHOR2
	.word	.LANCHOR2+1132
	.word	.LANCHOR2-1722
	.word	.LANCHOR2-1732
	.word	.LANCHOR2-1652
	.word	.LANCHOR2-1644
	.word	.LANCHOR2+1148
	.word	.LANCHOR2-1284
	.word	.LANCHOR2-1280
	.fnend
	.size	FtlScanSysBlk, .-FtlScanSysBlk
	.align	2
	.global	FtlLoadEctTbl
	.type	FtlLoadEctTbl, %function
FtlLoadEctTbl:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	stmfd	sp!, {r3, r4, r5, lr}
	.save {r3, r4, r5, lr}
	mov	r0, #64
	ldr	r4, .L2235
	sub	r5, r4, #1424
	ldr	r2, [r4, #-1416]
	ldrh	r1, [r5]
	bl	FtlVendorPartRead
	ldr	r3, [r4, #-1416]
	ldr	r2, [r3]
	ldr	r3, .L2235+4
	cmp	r2, r3
	beq	.L2233
	ldr	r1, .L2235+8
	ldr	r0, .L2235+12
	bl	printk
	ldrh	r2, [r5]
	ldr	r0, [r4, #-1416]
	mov	r1, #0
	mov	r2, r2, asl #9
	bl	ftl_memset
.L2233:
	mov	r0, #0
	ldmfd	sp!, {r3, r4, r5, pc}
.L2236:
	.align	2
.L2235:
	.word	.LANCHOR2
	.word	1112818501
	.word	.LC47
	.word	.LC46
	.fnend
	.size	FtlLoadEctTbl, .-FtlLoadEctTbl
	.align	2
	.global	ftl_set_blk_mode
	.type	ftl_set_blk_mode, %function
ftl_set_blk_mode:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	@ link register save eliminated.
	ldr	r2, .L2241
	mov	r3, r0, lsr #5
	cmp	r1, #0
	and	r0, r0, #31
	uxth	r3, r3
	mov	ip, #1
	ldr	r1, [r2, #-1392]
	ldr	r2, [r1, r3, asl #2]
	orrne	r0, r2, ip, asl r0
	biceq	r0, r2, ip, asl r0
	str	r0, [r1, r3, asl #2]
	bx	lr
.L2242:
	.align	2
.L2241:
	.word	.LANCHOR2
	.fnend
	.size	ftl_set_blk_mode, .-ftl_set_blk_mode
	.align	2
	.global	ftl_get_blk_mode
	.type	ftl_get_blk_mode, %function
ftl_get_blk_mode:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	@ link register save eliminated.
	ldr	r3, .L2244
	mov	r2, r0, lsr #5
	and	r0, r0, #31
	ldr	r3, [r3, #-1392]
	ldr	r3, [r3, r2, asl #2]
	mov	r0, r3, lsr r0
	and	r0, r0, #1
	bx	lr
.L2245:
	.align	2
.L2244:
	.word	.LANCHOR2
	.fnend
	.size	ftl_get_blk_mode, .-ftl_get_blk_mode
	.align	2
	.global	FtlCheckVpc
	.type	FtlCheckVpc, %function
FtlCheckVpc:
	.fnstart
	@ args = 0, pretend = 0, frame = 8
	@ frame_needed = 0, uses_anonymous_args = 0
	stmfd	sp!, {r0, r1, r2, r4, r5, r6, r7, r8, r9, lr}
	.save {r4, r5, r6, r7, r8, r9, lr}
	.pad #12
	mov	r4, #0
	ldr	r1, .L2267
	ldr	r0, .L2267+4
	bl	printk
	ldr	r0, .L2267+8
	mov	r1, #0
	mov	r2, #8192
	bl	memset
	ldr	r5, .L2267+12
	add	r7, r5, #1152
.L2247:
	ldr	r3, [r5, #-1280]
	ldr	r6, .L2267+12
	cmp	r4, r3
	bcs	.L2265
	mov	r0, r4
	add	r1, sp, #4
	mov	r2, #0
	bl	log2phys
	ldr	r0, [sp, #4]
	cmn	r0, #1
	beq	.L2248
	ubfx	r0, r0, #10, #16
	bl	P2V_block_in_plane
	mov	r0, r0, asl #1
	ldrh	r3, [r7, r0]
	add	r3, r3, #1
	strh	r3, [r7, r0]	@ movhi
.L2248:
	add	r4, r4, #1
	b	.L2247
.L2265:
	ldr	r5, .L2267+16
	add	r8, r6, #1152
	mov	r4, #0
	mov	r7, r6
.L2250:
	ldrh	r3, [r5]
	uxth	r1, r4
	ldr	r0, .L2267+12
	cmp	r3, r1
	bls	.L2266
	ldr	r2, [r7, #-1404]
	mov	r3, r1, asl #1
	ldrh	r2, [r2, r3]
	ldrh	r3, [r8, r3]
	cmp	r2, r3
	beq	.L2251
	ldr	r0, .L2267+20
	bl	printk
.L2251:
	add	r4, r4, #1
	b	.L2250
.L2266:
	ldr	r3, [r0, #880]
	cmp	r3, #0
	beq	.L2246
	add	r2, r0, #884
	add	r8, r0, #1152
	mov	r5, #0
	mov	r9, #6
	ldrh	r7, [r2]
	ldr	r2, [r0, #-1352]
	rsb	r3, r2, r3
	ldr	r2, .L2267+24
	mov	r3, r3, asr #1
	mul	r3, r2, r3
	uxth	r4, r3
.L2254:
	uxth	r3, r5
	cmp	r3, r7
	bcs	.L2246
	ldr	r2, [r6, #-1404]
	mov	r3, r4, asl #1
	ldrh	r2, [r2, r3]
	cmp	r2, #0
	beq	.L2255
	ldr	r0, .L2267+28
	mov	r1, r4
	ldrh	r3, [r8, r3]
	bl	printk
.L2255:
	mul	r4, r9, r4
	ldr	r3, [r6, #-1352]
	add	r5, r5, #1
	ldrh	r4, [r3, r4]
	movw	r3, #65535
	cmp	r4, r3
	bne	.L2254
.L2246:
	add	sp, sp, #12
	@ sp needed
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, pc}
.L2268:
	.align	2
.L2267:
	.word	.LANCHOR3+16
	.word	.LC48
	.word	.LANCHOR2+1152
	.word	.LANCHOR2
	.word	.LANCHOR2-1724
	.word	.LC49
	.word	-1431655765
	.word	.LC50
	.fnend
	.size	FtlCheckVpc, .-FtlCheckVpc
	.align	2
	.global	FtlDumpSysBlock
	.type	FtlDumpSysBlock, %function
FtlDumpSysBlock:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	stmfd	sp!, {r4, r5, r6, r7, r8, lr}
	.save {r4, r5, r6, r7, r8, lr}
	mov	r7, r0, asl #10
	ldr	r4, .L2278
	.pad #24
	sub	sp, sp, #24
	ldr	r8, .L2278+4
	mov	r6, r0
	mov	r5, #0
	ldr	r3, [r4, #-1472]
	str	r3, [r4, #-1264]
	ldr	r3, [r4, #-1440]
	str	r3, [r4, #-1260]
.L2270:
	ldrh	r2, [r8]
	sxth	r3, r5
	cmp	r3, r2
	bge	.L2277
	mov	r1, #1
	ldr	r0, .L2278+8
	mov	r2, r1
	orr	r3, r3, r7
	str	r3, [r4, #-1268]
	bl	FlashReadPages
	ldr	r3, [r4, #-1260]
	mov	r1, r6
	ldr	r0, .L2278+12
	ldr	r2, [r3]
	str	r2, [sp]
	ldr	r2, [r3, #4]
	str	r2, [sp, #4]
	ldr	r2, [r3, #8]
	str	r2, [sp, #8]
	ldr	r3, [r3, #12]
	ldr	r2, [r4, #-1272]
	str	r3, [sp, #12]
	ldr	r3, [r4, #-1264]
	ldr	r3, [r3]
	str	r3, [sp, #16]
	ldr	r3, [r4, #-1268]
	bl	printk
	ldr	r3, [r4, #-1260]
	ldr	r3, [r3]
	cmn	r3, #1
	beq	.L2271
	ldr	r0, .L2278+16
	mov	r2, #4
	ldr	r1, [r4, #-1472]
	mov	r3, #768
	bl	rknand_print_hex
.L2271:
	add	r5, r5, #1
	b	.L2270
.L2277:
	add	sp, sp, #24
	@ sp needed
	ldmfd	sp!, {r4, r5, r6, r7, r8, pc}
.L2279:
	.align	2
.L2278:
	.word	.LANCHOR2
	.word	.LANCHOR2-1662
	.word	.LANCHOR2-1272
	.word	.LC51
	.word	.LC52
	.fnend
	.size	FtlDumpSysBlock, .-FtlDumpSysBlock
	.align	2
	.global	Ftlscanalldata
	.type	Ftlscanalldata, %function
Ftlscanalldata:
	.fnstart
	@ args = 0, pretend = 0, frame = 8
	@ frame_needed = 0, uses_anonymous_args = 0
	stmfd	sp!, {r4, r5, r6, r7, lr}
	.save {r4, r5, r6, r7, lr}
	mov	r1, #0
	.pad #36
	sub	sp, sp, #36
	ldr	r0, .L2290
	bl	printk
	ldr	r6, .L2290+4
	mov	r5, #0
	mov	r4, r6
.L2281:
	ldr	r3, [r6, #-1280]
	cmp	r5, r3
	bcs	.L2289
	mov	r0, r5
	add	r1, sp, #28
	mov	r2, #0
	bl	log2phys
	ubfx	r3, r5, #0, #11
	cmp	r3, #0
	bne	.L2282
	ldr	r0, .L2290+8
	mov	r1, r5
	ldr	r2, [sp, #28]
	bl	printk
.L2282:
	ldr	r3, [sp, #28]
	cmn	r3, #1
	beq	.L2284
	str	r3, [r4, #-1268]
	mov	r2, #0
	ldr	r3, [r4, #-1472]
	mov	r1, #1
	ldr	r7, [r4, #-1440]
	ldr	r0, .L2290+12
	str	r3, [r4, #-1264]
	str	r5, [r4, #-1256]
	str	r7, [r4, #-1260]
	str	r2, [r4, #-1272]
	bl	FlashReadPages
	ldr	r3, [r4, #-1272]
	cmn	r3, #1
	cmpne	r3, #256
	beq	.L2285
	ldr	r3, [r7, #8]
	cmp	r3, r5
	beq	.L2284
.L2285:
	ldr	r3, [r4, #-1260]
	ldr	r2, [r4, #-1264]
	ldr	r0, .L2290+16
	ldr	r1, [r3, #4]
	str	r1, [sp]
	ldr	r1, [r3, #8]
	str	r1, [sp, #4]
	ldr	r1, [r3, #12]
	str	r1, [sp, #8]
	ldr	r1, [r2]
	str	r1, [sp, #12]
	mov	r1, r5
	ldr	r2, [r2, #4]
	str	r2, [sp, #16]
	ldr	r2, [r4, #-1268]
	ldr	r3, [r3]
	bl	printk
.L2284:
	add	r5, r5, #1
	b	.L2281
.L2289:
	add	sp, sp, #36
	@ sp needed
	ldmfd	sp!, {r4, r5, r6, r7, pc}
.L2291:
	.align	2
.L2290:
	.word	.LC53
	.word	.LANCHOR2
	.word	.LC54
	.word	.LANCHOR2-1272
	.word	.LC55
	.fnend
	.size	Ftlscanalldata, .-Ftlscanalldata
	.align	2
	.global	dump_map_info
	.type	dump_map_info, %function
dump_map_info:
	.fnstart
	@ args = 0, pretend = 0, frame = 24
	@ frame_needed = 0, uses_anonymous_args = 0
	stmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.save {r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.pad #52
	sub	sp, sp, #52
	ldr	r5, .L2310
	ldr	fp, .L2310+4
	ldrh	r6, [r5, #-12]
	add	r5, r5, #1712
	mov	r8, r5
.L2293:
	ldrh	r3, [fp]
	cmp	r3, r6
	bls	.L2306
	ldr	r1, .L2310+8
	mov	r7, #0
	ldr	r2, [r5, #-1500]
	mov	r4, r7
	ldr	r3, [r5, #-1460]
	add	r1, r1, #28
	ldr	r10, [r5, #-1432]
	ldrh	ip, [r1, #-28]
	ldrh	r9, [r1, #52]
	str	r1, [sp, #28]
.L2302:
	uxth	r1, r7
	cmp	r1, ip
	bcs	.L2308
	str	r3, [sp, #44]
	mov	r1, r6
	ldr	r3, [sp, #28]
	str	r2, [sp, #40]
	str	ip, [sp, #36]
	ldrb	r0, [r3, r7]	@ zero_extendqisi2
	bl	V2P_block
	str	r0, [sp, #32]
	bl	FtlBbmIsBadBlock
	cmp	r0, #0
	ldr	r1, [sp, #32]
	ldr	ip, [sp, #36]
	ldr	r2, [sp, #40]
	ldr	r3, [sp, #44]
	bne	.L2294
	mov	r0, #36
	mov	r1, r1, asl #10
	mla	r0, r0, r4, r2
	stmib	r0, {r1, r3}
	mul	r1, r9, r4
	add	r4, r4, #1
	uxth	r4, r4
	add	lr, r1, #3
	cmp	r1, #0
	movlt	r1, lr
	bic	r1, r1, #3
	add	r1, r10, r1
	str	r1, [r0, #12]
.L2294:
	add	r7, r7, #1
	b	.L2302
.L2308:
	cmp	r4, #0
	beq	.L2297
	ldr	r0, [r8, #-1500]
	mov	r1, r4
	mov	r2, #1
	mov	r7, #0
	bl	FlashReadPages
	mov	r9, #36
.L2298:
	uxth	r3, r7
	cmp	r3, r4
	bcs	.L2297
	ldr	r3, [r8, #-1500]
	ldr	r0, .L2310+12
	mla	r3, r9, r7, r3
	add	r7, r7, #1
	ldmib	r3, {r2, r3, ip}
	ldr	r1, [ip, #4]
	str	r1, [sp]
	ldr	r1, [ip, #8]
	str	r1, [sp, #4]
	ldr	r1, [ip, #12]
	str	r1, [sp, #8]
	ldr	r1, [r3]
	str	r1, [sp, #12]
	ubfx	r1, r2, #10, #16
	ldr	r3, [r3, #4]
	str	r3, [sp, #16]
	ldr	r3, [ip]
	bl	printk
	b	.L2298
.L2297:
	add	r6, r6, #1
	uxth	r6, r6
	b	.L2293
.L2306:
	ldr	r4, .L2310+16
	mov	r7, #0
	ldr	r8, .L2310+20
.L2301:
	ldrh	r3, [r8]
	sxth	r6, r7
	ldr	r5, .L2310+16
	cmp	r6, r3
	bge	.L2304
	ldr	r9, .L2310+24
	mov	r6, r6, asl #1
	mov	r5, #0
.L2305:
	ldrh	r2, [r9]
	sxth	r3, r5
	add	r5, r5, #1
	cmp	r3, r2
	bge	.L2309
	ldr	r2, [r4, #-1400]
	mov	r1, #1
	ldr	r0, .L2310+28
	ldrh	r2, [r2, r6]
	orr	r3, r3, r2, asl #10
	mov	r2, r1
	str	r3, [r4, #-1268]
	bl	FlashReadPages
	ldr	r3, [r4, #-1260]
	ldr	r1, [r4, #-1400]
	ldr	r2, [r4, #-1264]
	ldr	r0, [r3]
	ldrh	r1, [r1, r6]
	str	r0, [sp]
	ldr	r0, [r3, #4]
	str	r0, [sp, #4]
	ldr	r0, [r3, #8]
	str	r0, [sp, #8]
	ldr	r3, [r3, #12]
	ldr	r0, .L2310+32
	str	r3, [sp, #12]
	ldr	r3, [r2]
	str	r3, [sp, #16]
	ldr	r3, [r2, #4]
	str	r3, [sp, #20]
	ldr	r2, [r4, #-1272]
	ldr	r3, [r4, #-1268]
	bl	printk
	b	.L2305
.L2309:
	add	r7, r7, #1
	b	.L2301
.L2304:
	sub	r4, r5, #1616
	ldr	r1, [r5, #-1400]
	ldr	r3, [r5, #-1636]
	mov	r2, #2
	ldr	r0, .L2310+36
	bl	rknand_print_hex
	ldr	r1, [r5, #-1372]
	ldrh	r3, [r4, #-12]
	mov	r2, #4
	ldr	r0, .L2310+40
	bl	rknand_print_hex
	ldr	r0, .L2310+44
	ldr	r1, [r5, #-1368]
	mov	r2, #4
	ldrh	r3, [r4, #-12]
	add	sp, sp, #52
	@ sp needed
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, fp, lr}
	b	rknand_print_hex
.L2311:
	.align	2
.L2310:
	.word	.LANCHOR2-1712
	.word	.LANCHOR2-1722
	.word	.LANCHOR2-1732
	.word	.LC56
	.word	.LANCHOR2
	.word	.LANCHOR2+1148
	.word	.LANCHOR2-1662
	.word	.LANCHOR2-1272
	.word	.LC57
	.word	.LC58
	.word	.LC59
	.word	.LC60
	.fnend
	.size	dump_map_info, .-dump_map_info
	.align	2
	.global	FtlMapTblRecovery
	.type	FtlMapTblRecovery, %function
FtlMapTblRecovery:
	.fnstart
	@ args = 0, pretend = 0, frame = 24
	@ frame_needed = 0, uses_anonymous_args = 0
	stmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.save {r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.pad #28
	sub	sp, sp, #28
	ldr	r3, [r0, #16]
	mov	r4, r0
	ldrh	r9, [r0, #6]
	mov	r1, #0
	ldr	r7, [r0, #24]
	str	r3, [sp, #12]
	ldrh	r3, [r0, #8]
	mov	r2, r9, asl #2
	ldr	r8, [r0, #12]
	mov	r0, r7
	ldr	r5, .L2355
	str	r3, [sp, #8]
	bl	ftl_memset
	mov	r1, #0
	str	r1, [r4, #32]
	ldr	r3, [r5, #-1472]
	mov	r10, r5
	ldr	r6, [r5, #-1440]
	str	r1, [r4, #28]
	str	r3, [r5, #-1264]
	mvn	r3, #0
	str	r6, [r5, #-1260]
	strh	r3, [r4]	@ movhi
	strh	r3, [r4, #2]	@ movhi
	mov	r3, #1
	str	r3, [r4, #36]
	str	r1, [sp, #4]
.L2313:
	ldrh	r2, [sp, #4]
	ldr	r3, [sp, #8]
	sxth	fp, r2
	cmp	fp, r3
	bge	.L2331
	ldr	r3, [sp, #8]
	sub	r1, r3, #1
	cmp	fp, r1
	mov	r1, fp, asl #1
	bne	.L2314
	ldrh	r0, [r8, r1]
	add	r10, r8, r1
	mov	r1, #1
	str	r2, [sp, #4]
	bl	FtlGetLastWrittenPage
	mov	r8, #0
	ldr	r2, [sp, #4]
	add	r3, r0, #1
	strh	r3, [r4, #2]	@ movhi
	sxth	r0, r0
	ldr	r3, [sp, #12]
	strh	r2, [r4]	@ movhi
	ldr	r3, [r3, fp, asl #2]
	ldr	fp, .L2355
	str	r3, [r4, #28]
	add	r3, r0, #1
	str	r3, [sp, #4]
.L2315:
	ldr	r2, [sp, #4]
	sxth	r3, r8
	cmp	r3, r2
	bge	.L2331
	ldrh	r2, [r10]
	mov	r1, #1
	ldr	r0, .L2355+4
	orr	r3, r3, r2, asl #10
	mov	r2, r1
	str	r3, [r5, #-1268]
	bl	FlashReadPages
	ldr	r3, .L2355+8
	ldrb	r3, [r3]	@ zero_extendqisi2
	cmp	r3, #0
	beq	.L2316
	ldr	r3, [fp, #-1260]
	ldr	r3, [r3, #12]
	cmp	r3, #0
	beq	.L2316
	ldr	r2, .L2355+12
	ldr	r0, [fp, #-1264]
	str	r3, [sp, #8]
	ldrh	r1, [r2]
	bl	js_hash
	ldr	r3, [sp, #8]
	cmp	r3, r0
	mvnne	r3, #0
	strne	r3, [fp, #-1272]
.L2316:
	ldr	r3, [fp, #-1272]
	cmn	r3, #1
	beq	.L2317
	ldrh	r3, [r6, #8]
	cmp	r3, r9
	bcs	.L2317
	ldrh	r2, [r4, #4]
	ldrh	r1, [r6]
	cmp	r1, r2
	ldreq	r2, [fp, #-1268]
	streq	r2, [r7, r3, asl #2]
.L2317:
	add	r8, r8, #1
	b	.L2315
.L2331:
	mov	r0, r4
	bl	ftl_free_no_use_map_blk
	ldr	r3, .L2355+16
	ldrh	r2, [r4, #2]
	ldrh	r3, [r3, #-14]
	cmp	r2, r3
	bne	.L2320
	mov	r0, r4
	bl	ftl_map_blk_alloc_new_blk
	b	.L2320
.L2314:
	ldr	r2, [r5, #-1472]
	add	r3, r8, r1
	str	r3, [sp, #16]
	ldr	r3, .L2355+20
	str	r2, [r5, #-1264]
	ldrh	r1, [r8, r1]
	ldrh	r2, [r3]
	ldr	r0, .L2355+4
	sub	r2, r2, #1
	orr	r2, r2, r1, asl #10
	mov	r1, #1
	str	r2, [r5, #-1268]
	mov	r2, r1
	bl	FlashReadPages
	ldr	r2, [r5, #-1272]
	cmn	r2, #1
	beq	.L2333
	ldrh	r1, [r6]
	ldrh	r2, [r4, #4]
	cmp	r1, r2
	bne	.L2333
	ldrh	r1, [r6, #8]
	movw	r2, #64245
	cmp	r1, r2
	bne	.L2333
	mov	r0, #0
	mov	fp, #8
	mov	lr, #4
.L2322:
	ldr	r3, .L2355+20
	uxth	r2, r0
	sxth	r1, r2
	ldrh	ip, [r3]
	sub	ip, ip, #1
	cmp	r1, ip
	bge	.L2325
	ldr	ip, [r10, #-1472]
	add	r0, r0, #1
	ldr	r1, [ip, r1, asl #3]
	uxth	r1, r1
	cmp	r1, r9
	smlabbcc	r2, r2, fp, lr
	ldrcc	r2, [ip, r2]
	strcc	r2, [r7, r1, asl #2]
	b	.L2322
.L2333:
	mov	fp, #0
.L2353:
	ldr	r3, .L2355+20
	sxth	r2, fp
	ldrh	r1, [r3]
	cmp	r2, r1
	bge	.L2325
	ldr	r3, [sp, #16]
	ldr	r0, .L2355+4
	ldrh	r1, [r3]
	orr	r2, r2, r1, asl #10
	mov	r1, #1
	str	r2, [r10, #-1268]
	mov	r2, r1
	bl	FlashReadPages
	ldr	r2, .L2355+8
	ldrb	r2, [r2]	@ zero_extendqisi2
	cmp	r2, #0
	beq	.L2326
	ldr	r2, [r10, #-1260]
	ldr	r2, [r2, #12]
	cmp	r2, #0
	beq	.L2326
	ldr	r1, .L2355+12
	ldr	r0, [r10, #-1264]
	str	r2, [sp, #20]
	ldrh	r1, [r1]
	bl	js_hash
	ldr	r2, [sp, #20]
	cmp	r2, r0
	mvnne	r2, #0
	strne	r2, [r10, #-1272]
.L2326:
	ldr	r2, [r10, #-1272]
	cmn	r2, #1
	beq	.L2327
	ldrh	r2, [r6, #8]
	cmp	r2, r9
	bcs	.L2327
	ldrh	r1, [r4, #4]
	ldrh	r0, [r6]
	cmp	r0, r1
	ldreq	r1, [r10, #-1268]
	streq	r1, [r7, r2, asl #2]
.L2327:
	add	fp, fp, #1
	b	.L2353
.L2325:
	ldr	r3, [sp, #4]
	add	r3, r3, #1
	str	r3, [sp, #4]
	b	.L2313
.L2320:
	mov	r0, r4
	bl	ftl_map_blk_gc
	mov	r0, r4
	bl	ftl_map_blk_gc
	mov	r0, #0
	add	sp, sp, #28
	@ sp needed
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L2356:
	.align	2
.L2355:
	.word	.LANCHOR2
	.word	.LANCHOR2-1272
	.word	.LANCHOR0
	.word	.LANCHOR2-1654
	.word	.LANCHOR2-1648
	.word	.LANCHOR2-1662
	.fnend
	.size	FtlMapTblRecovery, .-FtlMapTblRecovery
	.align	2
	.global	FtlLoadVonderInfo
	.type	FtlLoadVonderInfo, %function
FtlLoadVonderInfo:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	stmfd	sp!, {r3, lr}
	.save {r3, lr}
	ldr	r3, .L2359
	sub	r2, r3, #1632
	add	r0, r3, #1072
	add	r0, r0, #8
	ldrh	r1, [r2, #-12]
	ldrh	r2, [r2, #-10]
	strh	r1, [r0, #10]	@ movhi
	strh	r2, [r0, #6]	@ movhi
	ldr	r2, [r3, #-1388]
	ldr	r1, .L2359+4
	str	r2, [r3, #1092]
	ldr	r2, [r3, #-1380]
	strh	r1, [r0, #4]	@ movhi
	sub	r1, r3, #1280
	str	r2, [r3, #1096]
	ldr	r2, [r3, #-1384]
	ldrh	r1, [r1, #-4]
	str	r2, [r3, #1100]
	ldr	r2, [r3, #-1376]
	strh	r1, [r0, #8]	@ movhi
	str	r2, [r3, #1104]
	bl	FtlMapTblRecovery
	mov	r0, #0
	ldmfd	sp!, {r3, pc}
.L2360:
	.align	2
.L2359:
	.word	.LANCHOR2
	.word	-3962
	.fnend
	.size	FtlLoadVonderInfo, .-FtlLoadVonderInfo
	.align	2
	.global	FtlL2PDataInit
	.type	FtlL2PDataInit, %function
FtlL2PDataInit:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	stmfd	sp!, {r4, r5, r6, r7, r8, lr}
	.save {r4, r5, r6, r7, r8, lr}
	mov	r1, #0
	ldr	r4, .L2366
	mvn	r6, #0
	ldr	r2, [r4, #-1636]
	ldr	r0, [r4, #-1396]
	mov	r2, r2, asl #1
	bl	ftl_memset
	sub	r3, r4, #1648
	sub	r2, r4, #1616
	mov	r1, #255
	ldrh	r3, [r3, #-6]
	ldrh	r2, [r2, #-10]
	ldr	r0, [r4, #-1356]
	mul	r2, r2, r3
	bl	ftl_memset
	ldr	r0, .L2366+4
	mov	r1, #0
	mov	lr, #12
	sub	r7, r0, #28
	mov	r5, r1
.L2362:
	ldrh	r2, [r0]
	add	ip, r1, #1
	uxth	r1, r1
	ldr	r3, .L2366
	cmp	r2, r1
	sub	r8, r3, #1616
	bls	.L2365
	mul	r8, lr, r1
	ldr	r3, [r4, #-1360]
	add	r2, r3, r8
	str	r5, [r2, #4]
	strh	r6, [r3, r8]	@ movhi
	ldrh	r2, [r7]
	ldr	r3, [r4, #-1360]
	mul	r2, r1, r2
	add	r3, r3, r8
	ldr	r1, [r4, #-1356]
	bic	r2, r2, #3
	add	r2, r1, r2
	mov	r1, ip
	str	r2, [r3, #8]
	b	.L2362
.L2365:
	ldr	r2, .L2366+8
	movw	r0, #1032
	mvn	r1, #0
	strh	r1, [r3, r0]	@ movhi
	strh	r1, [r2, #2]	@ movhi
	ldr	r1, [r3, #-1636]
	strh	r1, [r2, #10]	@ movhi
	ldr	r1, .L2366+12
	strh	r1, [r2, #4]	@ movhi
	movw	r1, #1148
	ldrh	r1, [r3, r1]
	strh	r1, [r2, #8]	@ movhi
	ldrh	r1, [r8, #-12]
	strh	r1, [r2, #6]	@ movhi
	ldr	r2, [r3, #-1400]
	str	r2, [r3, #1044]
	ldr	r2, [r3, #-1364]
	str	r2, [r3, #1048]
	ldr	r2, [r3, #-1396]
	str	r2, [r3, #1052]
	ldr	r2, [r3, #-1372]
	str	r2, [r3, #1056]
	ldmfd	sp!, {r4, r5, r6, r7, r8, pc}
.L2367:
	.align	2
.L2366:
	.word	.LANCHOR2
	.word	.LANCHOR2-1626
	.word	.LANCHOR2+1032
	.word	-3902
	.fnend
	.size	FtlL2PDataInit, .-FtlL2PDataInit
	.align	2
	.global	FtlLoadMapInfo
	.type	FtlLoadMapInfo, %function
FtlLoadMapInfo:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	stmfd	sp!, {r3, lr}
	.save {r3, lr}
	bl	FtlL2PDataInit
	ldr	r0, .L2370
	bl	FtlMapTblRecovery
	mov	r0, #0
	ldmfd	sp!, {r3, pc}
.L2371:
	.align	2
.L2370:
	.word	.LANCHOR2+1032
	.fnend
	.size	FtlLoadMapInfo, .-FtlLoadMapInfo
	.align	2
	.global	ftl_sb_update_avl_pages
	.type	ftl_sb_update_avl_pages, %function
ftl_sb_update_avl_pages:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	mov	r3, #0
	strh	r3, [r0, #4]	@ movhi
	ldr	r3, .L2382
	stmfd	sp!, {r4, r5, lr}
	.save {r4, r5, lr}
	movw	r4, #65535
	ldrh	lr, [r3, #-4]
	add	r3, r2, #7
	add	r3, r0, r3, asl #1
.L2373:
	cmp	r2, lr
	bcs	.L2380
	ldrh	ip, [r3, #2]!
	add	r2, r2, #1
	cmp	ip, r4
	uxth	r2, r2
	ldrneh	ip, [r0, #4]
	addne	ip, ip, #1
	strneh	ip, [r0, #4]	@ movhi
	b	.L2373
.L2380:
	ldr	r3, .L2382+4
	add	ip, r0, #14
	mov	r2, #0
	movw	r5, #65535
	ldrh	r4, [r3]
.L2376:
	uxth	r3, r2
	cmp	r3, lr
	bcs	.L2381
	ldrh	r3, [ip, #2]!
	add	r2, r2, #1
	cmp	r3, r5
	ldrneh	r3, [r0, #4]
	addne	r3, r4, r3
	subne	r3, r3, #1
	rsbne	r3, r1, r3
	strneh	r3, [r0, #4]	@ movhi
	b	.L2376
.L2381:
	ldmfd	sp!, {r4, r5, pc}
.L2383:
	.align	2
.L2382:
	.word	.LANCHOR2-1728
	.word	.LANCHOR2-1664
	.fnend
	.size	ftl_sb_update_avl_pages, .-ftl_sb_update_avl_pages
	.align	2
	.global	FtlReUsePrevPpa
	.type	FtlReUsePrevPpa, %function
FtlReUsePrevPpa:
	.fnstart
	@ args = 0, pretend = 0, frame = 8
	@ frame_needed = 0, uses_anonymous_args = 0
	stmfd	sp!, {r0, r1, r2, r4, r5, r6, r7, r8, r9, lr}
	.save {r4, r5, r6, r7, r8, r9, lr}
	.pad #12
	mov	r5, r0
	ubfx	r0, r1, #10, #16
	str	r1, [sp, #4]
	bl	P2V_block_in_plane
	ldr	r6, .L2394
	ldr	r4, [r6, #-1404]
	mov	r7, r0, asl #1
	ldrh	r3, [r4, r7]
	cmp	r3, #0
	addne	r3, r3, #1
	strneh	r3, [r4, r7]	@ movhi
	bne	.L2386
	ldr	r4, [r6, #880]
	cmp	r4, #0
	beq	.L2386
	ldr	r1, [r6, #-1352]
	add	r2, r6, #884
	ldr	lr, .L2394+4
	movw	r9, #65535
	rsb	r4, r1, r4
	ldrh	ip, [r2]
	mov	r8, r2
	mov	r4, r4, asr #1
	mul	r4, lr, r4
	mov	lr, #6
	uxth	r4, r4
.L2387:
	uxth	r2, r3
	cmp	r2, ip
	bcs	.L2386
	cmp	r4, r0
	bne	.L2388
	mov	r1, r4
	ldr	r0, .L2394+8
	bl	List_remove_node
	ldrh	r3, [r8]
	mov	r0, r4
	sub	r3, r3, #1
	strh	r3, [r8]	@ movhi
	bl	INSERT_DATA_LIST
	ldr	r2, [r6, #-1404]
	ldrh	r3, [r2, r7]
	add	r3, r3, #1
	strh	r3, [r2, r7]	@ movhi
	b	.L2386
.L2388:
	mul	r4, lr, r4
	add	r3, r3, #1
	ldrh	r4, [r1, r4]
	cmp	r4, r9
	bne	.L2387
.L2386:
	mov	r0, r5
	add	r1, sp, #4
	mov	r2, #1
	bl	log2phys
	add	sp, sp, #12
	@ sp needed
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, pc}
.L2395:
	.align	2
.L2394:
	.word	.LANCHOR2
	.word	-1431655765
	.word	.LANCHOR2+880
	.fnend
	.size	FtlReUsePrevPpa, .-FtlReUsePrevPpa
	.align	2
	.global	make_superblock
	.type	make_superblock, %function
make_superblock:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	ldr	r3, .L2410
	stmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, lr}
	.save {r4, r5, r6, r7, r8, r9, r10, lr}
	add	r6, r0, #16
	ldrh	r8, [r3, #-4]
	add	r7, r3, #24
	mov	r4, r0
	mvn	r9, #0
	mov	r5, #0
	strh	r5, [r0, #4]	@ movhi
	strb	r5, [r0, #7]
.L2397:
	uxth	r3, r5
	cmp	r3, r8
	bcs	.L2409
	ldrb	r0, [r7, r5]	@ zero_extendqisi2
	add	r6, r6, #2
	ldrh	r1, [r4]
	add	r5, r5, #1
	bl	V2P_block
	strh	r9, [r6, #-2]	@ movhi
	mov	r10, r0
	bl	FtlBbmIsBadBlock
	cmp	r0, #0
	streqh	r10, [r6, #-2]	@ movhi
	ldreqb	r3, [r4, #7]	@ zero_extendqisi2
	addeq	r3, r3, #1
	streqb	r3, [r4, #7]
	b	.L2397
.L2409:
	ldr	r2, .L2410+4
	ldrb	r1, [r4, #7]	@ zero_extendqisi2
	sub	r3, r2, #1664
	ldrh	r3, [r3]
	smulbb	r3, r1, r3
	strh	r3, [r4, #4]	@ movhi
	mov	r3, #0
	strb	r3, [r4, #9]
	ldr	r3, [r2, #-1800]
	cmp	r3, #0
	beq	.L2400
	ldrh	r3, [r4]
	ldr	r2, [r2, #-1412]
	mov	r3, r3, asl #1
	ldrh	r3, [r2, r3]
	cmp	r3, #79
	movls	r3, #1
	strlsb	r3, [r4, #9]
.L2400:
	ldr	r3, .L2410+8
	mov	r0, #0
	ldrb	r3, [r3]	@ zero_extendqisi2
	cmp	r3, #0
	movne	r3, #1
	strneb	r3, [r4, #9]
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, pc}
.L2411:
	.align	2
.L2410:
	.word	.LANCHOR2-1728
	.word	.LANCHOR2
	.word	.LANCHOR0
	.fnend
	.size	make_superblock, .-make_superblock
	.align	2
	.global	FtlLoadSysInfo
	.type	FtlLoadSysInfo, %function
FtlLoadSysInfo:
	.fnstart
	@ args = 0, pretend = 0, frame = 16
	@ frame_needed = 0, uses_anonymous_args = 0
	stmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.save {r4, r5, r6, r7, r8, r9, r10, fp, lr}
	mov	r1, #0
	ldr	r4, .L2441
	.pad #44
	sub	sp, sp, #44
	movw	r6, #1132
	ldr	r8, .L2441+4
	sub	r5, r4, #1712
	ldr	r3, [r4, #-1472]
	ldrh	r2, [r5, #-12]
	ldr	r0, [r4, #-1404]
	str	r3, [r4, #-1264]
	ldr	r3, [r4, #-1440]
	mov	r2, r2, asl #1
	str	r3, [r4, #-1260]
	bl	ftl_memset
	ldrh	r0, [r4, r6]
	movw	r3, #65535
	cmp	r0, r3
	str	r5, [sp, #28]
	bne	.L2413
.L2424:
	mvn	r0, #0
	b	.L2414
.L2413:
	mov	r1, #1
	bl	FtlGetLastWrittenPage
	ldrh	r9, [r4, r6]
	mov	r6, r4
	uxth	r5, r0
	add	r0, r0, #1
	strh	r0, [r8, #2]	@ movhi
.L2415:
	sxth	r3, r5
	cmp	r3, #0
	blt	.L2423
	sxth	fp, r9
	mov	r1, #1
	ldr	r0, .L2441+8
	mov	r2, r1
	orr	r3, r3, fp, asl #10
	str	r3, [r4, #-1268]
	ldr	r3, [r4, #-1472]
	str	r3, [r4, #-1264]
	bl	FlashReadPages
	ldr	r3, .L2441+12
	ldrb	r3, [r3]	@ zero_extendqisi2
	cmp	r3, #0
	beq	.L2416
	ldr	r7, [r6, #-1260]
	ldr	r3, [r7, #12]
	cmp	r3, #0
	beq	.L2416
	ldr	r2, [r6, #-1264]
	ldr	r10, .L2441+16
	str	r3, [sp, #36]
	mov	r0, r2
	str	r2, [sp, #32]
	ldrh	r1, [r10]
	bl	js_hash
	ldr	r3, [sp, #36]
	cmp	r3, r0
	beq	.L2416
	cmp	r5, #0
	ldr	r2, [sp, #32]
	bne	.L2417
	ldrh	r1, [r8, #4]
	ldr	ip, .L2441+4
	cmp	fp, r1
	beq	.L2417
	ldr	r0, [r7]
	ldrh	r1, [ip]
	str	ip, [sp, #32]
	str	r0, [sp]
	ldr	r0, [r7, #4]
	str	r0, [sp, #4]
	ldr	r0, [r7, #8]
	str	r3, [sp, #12]
	str	r0, [sp, #8]
	ldr	r3, [r2]
	ldr	r0, .L2441+20
	str	r3, [sp, #16]
	ldr	r2, [r6, #-1272]
	ldr	r3, [r6, #-1268]
	bl	printk
	ldrh	r5, [r10, #-8]
	ldr	ip, [sp, #32]
	ldrh	r9, [ip, #4]
	b	.L2419
.L2417:
	mvn	r3, #0
	str	r3, [r4, #-1272]
.L2416:
	ldr	r3, [r4, #-1272]
	cmn	r3, #1
	beq	.L2419
	ldr	r3, [r6, #-1472]
	ldr	r2, .L2441+24
	ldr	r3, [r3]
	cmp	r3, r2
	bne	.L2419
	ldr	r3, [r6, #-1440]
	ldrh	r2, [r3]
	movw	r3, #61604
	cmp	r2, r3
	bne	.L2419
.L2423:
	ldr	r5, .L2441
	mov	r2, #48
	ldr	r1, [r4, #-1264]
	add	r0, r5, #820
	sub	r6, r5, #1712
	bl	ftl_memcpy
	ldr	r1, [r4, #-1264]
	ldr	r0, [r4, #-1404]
	add	r1, r1, #48
	ldr	r3, [sp, #28]
	ldrh	r2, [r3, #-12]
	mov	r2, r2, asl #1
	bl	ftl_memcpy
	ldr	r1, [r4, #-1264]
	ldr	r0, [r4, #-1392]
	ldr	r3, [sp, #28]
	ldrh	r2, [r3, #-12]
	add	r3, r2, #24
	ubfx	r3, r3, #1, #30
	mov	r2, r2, lsr #3
	add	r2, r2, #4
	add	r1, r1, r3, asl #2
	bl	ftl_memcpy
	sub	r2, r5, #1616
	ldrh	r3, [r2, #-8]
	cmp	r3, #0
	beq	.L2421
	ldrh	r1, [r6, #-12]
	ldrh	r2, [r2, #-12]
	ldr	r0, [r5, #-1368]
	mov	r3, r1, lsr #3
	add	r3, r3, r1, asl #1
	ldr	r1, [r5, #-1264]
	add	r3, r3, #52
	mov	r2, r2, asl #2
	ubfx	r3, r3, #2, #14
	add	r1, r1, r3, asl #2
	bl	ftl_memcpy
	b	.L2421
.L2419:
	sub	r5, r5, #1
	uxth	r5, r5
	b	.L2415
.L2421:
	ldr	r2, [r4, #820]
	ldr	r3, .L2441+24
	ldr	r5, .L2441
	cmp	r2, r3
	bne	.L2424
	sub	r2, r5, #1696
	add	r6, r5, #820
	ldrb	r1, [r5, #830]	@ zero_extendqisi2
	ldrh	r2, [r2, #-14]
	ldrh	r3, [r6, #8]
	cmp	r1, r2
	strh	r3, [r8, #6]	@ movhi
	bne	.L2424
	sub	r2, r5, #1664
	sub	r1, r5, #1648
	ldr	r7, .L2441+28
	ldrh	r2, [r2]
	ldrh	r1, [r1, #-10]
	str	r3, [r7, #1160]
	mul	r2, r3, r2
	str	r2, [r5, #-1280]
	mul	r2, r1, r2
	str	r2, [r5, #-2740]
	sub	r2, r5, #1344
	ldrh	r0, [r2, #6]
	ldr	r2, [r5, #-1720]
	rsb	r0, r0, r2
	sub	r2, r5, #1728
	rsb	r0, r3, r0
	ldrh	r1, [r2, #-4]
	bl	__aeabi_uidiv
	movw	r3, #1164
	ldrh	ip, [r6, #14]
	add	r2, r5, #888
	add	lr, r7, #1168
	strh	ip, [r2]	@ movhi
	mov	r8, lr
	strh	r0, [r7, r3]	@ movhi
	ldrh	r3, [r6, #16]
	mov	r1, r3, lsr #6
	strh	r1, [r2, #2]	@ movhi
	ldrh	r1, [r6, #18]
	add	r2, r5, #936
	and	r3, r3, #63
	strb	r3, [r5, #894]
	ldrb	r3, [r5, #831]	@ zero_extendqisi2
	strh	r1, [r2]	@ movhi
	ldrh	r1, [r6, #20]
	strb	r3, [r5, #896]
	mvn	r3, #0
	strh	r3, [lr]	@ movhi
	mov	r3, #0
	mov	r0, r1, lsr #6
	and	r1, r1, #63
	strb	r1, [r5, #942]
	ldrb	r1, [r5, #832]	@ zero_extendqisi2
	strh	r0, [r2, #2]	@ movhi
	add	r0, r5, #984
	strb	r3, [r7, #1174]
	strb	r1, [r5, #944]
	ldrh	r1, [r6, #22]
	strb	r3, [r7, #1176]
	mov	r7, r0
	strh	r3, [lr, #2]	@ movhi
	strh	r1, [r0]	@ movhi
	ldrh	r1, [r6, #24]
	str	r3, [r5, #-1604]
	mov	r6, r1, lsr #6
	and	r1, r1, #63
	strb	r1, [r5, #990]
	ldrb	r1, [r5, #833]	@ zero_extendqisi2
	strh	r6, [r0, #2]	@ movhi
	mov	r6, r2
	strb	r1, [r5, #992]
	str	r3, [r5, #-1600]
	ldr	r1, [r5, #852]
	str	r3, [r5, #-1584]
	str	r3, [r5, #-1588]
	str	r3, [r5, #-1576]
	str	r3, [r5, #-1568]
	str	r3, [r5, #-1592]
	ldr	r3, [r5, #860]
	str	r1, [r5, #-1580]
	ldr	r1, [r5, #-1612]
	ldr	r2, [r4, #-1608]
	cmp	r3, r1
	strhi	r3, [r5, #-1612]
	ldr	r3, [r4, #856]
	cmp	r3, r2
	ldrhi	r2, .L2441
	strhi	r3, [r2, #-1608]
	movw	r3, #65535
	cmp	ip, r3
	beq	.L2427
	ldr	r0, .L2441+32
	bl	make_superblock
.L2427:
	ldrh	r2, [r6]
	movw	r3, #65535
	cmp	r2, r3
	beq	.L2428
	ldr	r0, .L2441+36
	bl	make_superblock
.L2428:
	ldrh	r2, [r7]
	movw	r3, #65535
	cmp	r2, r3
	beq	.L2429
	ldr	r0, .L2441+40
	bl	make_superblock
.L2429:
	ldrh	r2, [r8]
	movw	r3, #65535
	cmp	r2, r3
	beq	.L2430
	ldr	r0, .L2441+44
	bl	make_superblock
.L2430:
	mov	r0, #0
.L2414:
	add	sp, sp, #44
	@ sp needed
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L2442:
	.align	2
.L2441:
	.word	.LANCHOR2
	.word	.LANCHOR2+1132
	.word	.LANCHOR2-1272
	.word	.LANCHOR0
	.word	.LANCHOR2-1654
	.word	.LC61
	.word	1179929683
	.word	.LANCHOR4
	.word	.LANCHOR2+888
	.word	.LANCHOR2+936
	.word	.LANCHOR2+984
	.word	.LANCHOR4+1168
	.fnend
	.size	FtlLoadSysInfo, .-FtlLoadSysInfo
	.align	2
	.global	FtlDumpBlockInfo
	.type	FtlDumpBlockInfo, %function
FtlDumpBlockInfo:
	.fnstart
	@ args = 0, pretend = 0, frame = 72
	@ frame_needed = 0, uses_anonymous_args = 0
	stmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.save {r4, r5, r6, r7, r8, r9, r10, fp, lr}
	ubfx	r0, r0, #10, #16
	.pad #100
	sub	sp, sp, #100
	mov	r4, r1
	bl	P2V_block_in_plane
	ldr	r6, .L2457
	ldr	r1, .L2457+4
	sub	r5, r6, #1664
	ldrh	r9, [r5]
	mov	r7, r0
	ldr	r0, .L2457+8
	bl	printk
	ldr	r2, [r6, #-1404]
	mov	r3, r7, asl #1
	mov	r1, r7
	ldr	r0, .L2457+12
	ldrh	r2, [r2, r3]
	bl	printk
	add	r0, sp, #96
	strh	r7, [r0, #-48]!	@ movhi
	bl	make_superblock
	ldrb	r2, [r6, #-2744]	@ zero_extendqisi2
	clz	r3, r4
	cmp	r2, #0
	mov	r3, r3, lsr #5
	moveq	r3, #0
	cmp	r3, #0
	moveq	r4, r3
	beq	.L2444
	mov	r0, r7
	bl	ftl_get_blk_mode
	cmp	r0, #1
	mov	r4, r0
	subeq	r3, r6, #1648
	ldreqh	r9, [r3, #-14]
.L2444:
	ldr	r0, .L2457+16
	mov	r1, r4
	mov	r2, r9
	ldrh	r3, [r5]
	bl	printk
	ldr	r8, .L2457
	mov	r7, #0
.L2445:
	ldr	r3, .L2457+20
	mov	r2, #0
	add	r0, sp, #62
	mov	r5, r2
	movw	lr, #65535
	mov	r10, #36
	ldrh	r3, [r3]
	str	r3, [sp, #28]
	ldr	r3, [r8, #-1500]
	str	r3, [sp, #32]
	ldr	r3, [r8, #-1460]
	str	r3, [sp, #36]
	ldr	r3, .L2457+24
	ldrh	r3, [r3]
	str	r3, [sp, #40]
	ldr	r3, [r8, #-1432]
	str	r3, [sp, #44]
	ldr	r3, .L2457+28
	ldrh	ip, [r3]
.L2446:
	ldr	r1, [sp, #28]
	uxth	r3, r2
	cmp	r3, r1
	bcs	.L2455
	ldrh	r3, [r0, #2]!
	cmp	r3, lr
	beq	.L2447
	ldr	r1, [sp, #32]
	orr	r3, r7, r3, asl #10
	mla	r1, r10, r5, r1
	str	r3, [r1, #4]
	ldr	r3, [sp, #40]
	mul	r3, r3, r5
	add	fp, r3, #3
	cmp	r3, #0
	movlt	r3, fp
	ldr	fp, [sp, #36]
	bic	r3, r3, #3
	add	r3, fp, r3
	str	r3, [r1, #8]
	mul	r3, ip, r5
	add	r5, r5, #1
	uxth	r5, r5
	add	fp, r3, #3
	cmp	r3, #0
	movlt	r3, fp
	ldr	fp, [sp, #44]
	bic	r3, r3, #3
	add	r3, fp, r3
	str	r3, [r1, #12]
.L2447:
	add	r2, r2, #1
	b	.L2446
.L2455:
	ldr	r0, [r8, #-1500]
	mov	r1, r5
	mov	r2, r4
	mov	r10, #0
	bl	FlashReadPages
.L2449:
	uxth	r3, r10
	cmp	r3, r5
	bcs	.L2456
	mov	r3, #36
	ldr	lr, [r6, #-1500]
	mul	r2, r3, r10
	ldrh	r1, [sp, #48]
	add	r10, r10, #1
	add	ip, lr, r2
	ldr	r3, [ip, #12]
	ldr	r0, [ip, #8]
	ldr	fp, [r3]
	str	fp, [sp]
	ldr	fp, [r3, #4]
	str	fp, [sp, #4]
	ldr	fp, [r3, #8]
	str	fp, [sp, #8]
	ldr	r3, [r3, #12]
	str	r3, [sp, #12]
	ldr	r3, [r0]
	str	r3, [sp, #16]
	ldr	r3, [r0, #4]
	ldr	r0, .L2457+32
	str	r3, [sp, #20]
	ldr	r2, [lr, r2]
	ldr	r3, [ip, #4]
	bl	printk
	b	.L2449
.L2456:
	add	r7, r7, #1
	uxth	r7, r7
	cmp	r7, r9
	bne	.L2445
.L2451:
	mov	r0, #0
	add	sp, sp, #100
	@ sp needed
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L2458:
	.align	2
.L2457:
	.word	.LANCHOR2
	.word	.LANCHOR3+28
	.word	.LC48
	.word	.LC62
	.word	.LC63
	.word	.LANCHOR2-1732
	.word	.LANCHOR2-1654
	.word	.LANCHOR2-1652
	.word	.LC57
	.fnend
	.size	FtlDumpBlockInfo, .-FtlDumpBlockInfo
	.align	2
	.global	FtlScanAllBlock
	.type	FtlScanAllBlock, %function
FtlScanAllBlock:
	.fnstart
	@ args = 0, pretend = 0, frame = 64
	@ frame_needed = 0, uses_anonymous_args = 0
	stmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.save {r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.pad #92
	sub	sp, sp, #92
	ldr	r0, .L2474
	mov	r7, #0
	ldr	r1, .L2474+4
	bl	printk
	ldr	r5, .L2474+8
	mov	r6, r5
.L2460:
	ldr	r3, .L2474+12
	uxth	r4, r7
	ldrh	r3, [r3]
	cmp	r3, r4
	bls	.L2470
	add	r8, sp, #88
	mov	r0, r4
	movw	r9, #65535
	mov	r10, #36
	strh	r4, [r8, #-48]!	@ movhi
	bl	ftl_get_blk_mode
	ldr	ip, [r5, #-1404]
	mov	r2, r4, asl #1
	mov	r1, r4
	ldrh	r2, [ip, r2]
	mov	r3, r0
	ldr	r0, .L2474+16
	bl	printk
	mov	r0, r8
	bl	make_superblock
	ldr	r3, .L2474+20
	ldr	lr, [r5, #-1432]
	ldrh	r2, [r3]
	ldrh	ip, [r3, #78]
	ldrh	r8, [r3, #80]
	str	r2, [sp, #28]
	add	r0, sp, #54
	ldr	r2, [r5, #-1500]
	str	r2, [sp, #32]
	ldr	r2, [r5, #-1460]
	str	r2, [sp, #36]
	mov	r2, #0
	mov	r4, r2
.L2461:
	ldr	r1, [sp, #28]
	uxth	r3, r2
	cmp	r3, r1
	bcs	.L2471
	ldrh	r3, [r0, #2]!
	cmp	r3, r9
	beq	.L2462
	ldr	r1, [sp, #32]
	mov	r3, r3, asl #10
	mla	r1, r10, r4, r1
	str	r3, [r1, #4]
	mul	r3, ip, r4
	add	fp, r3, #3
	cmp	r3, #0
	movlt	r3, fp
	ldr	fp, [sp, #36]
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
	add	r3, lr, r3
	str	r3, [r1, #12]
.L2462:
	add	r2, r2, #1
	b	.L2461
.L2471:
	ldr	r0, [r6, #-1500]
	mov	r1, r4
	mov	r2, #0
	mov	r8, #0
	bl	FlashReadPages
	mov	r9, #36
.L2464:
	uxth	r3, r8
	cmp	r3, r4
	bcs	.L2472
	mul	r2, r9, r8
	ldr	lr, [r6, #-1500]
	ldrh	r1, [sp, #40]
	add	r8, r8, #1
	add	ip, lr, r2
	ldr	r3, [ip, #12]
	ldr	r0, [ip, #8]
	ldr	r10, [r3]
	str	r10, [sp]
	ldr	r10, [r3, #4]
	str	r10, [sp, #4]
	ldr	r10, [r3, #8]
	str	r10, [sp, #8]
	ldr	r3, [r3, #12]
	str	r3, [sp, #12]
	ldr	r3, [r0]
	str	r3, [sp, #16]
	ldr	r3, [r0, #4]
	ldr	r0, .L2474+24
	str	r3, [sp, #20]
	ldr	r2, [lr, r2]
	ldr	r3, [ip, #4]
	bl	printk
	b	.L2464
.L2472:
	ldr	r0, [r6, #-1500]
	mov	r1, r4
	mov	r2, #1
	mov	r8, #0
	bl	FlashReadPages
	mov	r9, #36
.L2466:
	uxth	r3, r8
	cmp	r3, r4
	bcs	.L2473
	mul	r2, r9, r8
	ldr	lr, [r6, #-1500]
	ldrh	r1, [sp, #40]
	add	r8, r8, #1
	add	ip, lr, r2
	ldr	r3, [ip, #12]
	ldr	r0, [ip, #8]
	ldr	r10, [r3]
	str	r10, [sp]
	ldr	r10, [r3, #4]
	str	r10, [sp, #4]
	ldr	r10, [r3, #8]
	str	r10, [sp, #8]
	ldr	r3, [r3, #12]
	str	r3, [sp, #12]
	ldr	r3, [r0]
	str	r3, [sp, #16]
	ldr	r3, [r0, #4]
	ldr	r0, .L2474+28
	str	r3, [sp, #20]
	ldr	r2, [lr, r2]
	ldr	r3, [ip, #4]
	bl	printk
	b	.L2466
.L2473:
	add	r7, r7, #1
	b	.L2460
.L2470:
	mov	r0, #0
	add	sp, sp, #92
	@ sp needed
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L2475:
	.align	2
.L2474:
	.word	.LC48
	.word	.LANCHOR3+48
	.word	.LANCHOR2
	.word	.LANCHOR2-1722
	.word	.LC64
	.word	.LANCHOR2-1732
	.word	.LC65
	.word	.LC66
	.fnend
	.size	FtlScanAllBlock, .-FtlScanAllBlock
	.align	2
	.global	SupperBlkListInit
	.type	SupperBlkListInit, %function
SupperBlkListInit:
	.fnstart
	@ args = 0, pretend = 0, frame = 24
	@ frame_needed = 0, uses_anonymous_args = 0
	stmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.save {r4, r5, r6, r7, r8, r9, r10, fp, lr}
	mov	r2, #6
	ldr	r6, .L2488
	mov	r1, #0
	.pad #28
	sub	sp, sp, #28
	sub	r3, r6, #1712
	ldr	r0, [r6, #-1352]
	mov	r5, r6
	ldrh	r3, [r3, #-10]
	mov	fp, r6
	mul	r2, r2, r3
	bl	ftl_memset
	mov	r3, #0
	add	r1, r6, #876
	add	r2, r6, #884
	sub	r0, r6, #1616
	mov	r4, r3
	mov	r8, r3
	mov	r9, r3
	str	r3, [r6, #880]
	str	r3, [r6, #868]
	str	r3, [r6, #872]
	strh	r3, [r1]	@ movhi
	strh	r3, [r2]	@ movhi
	strh	r3, [r0, #-6]	@ movhi
	str	r1, [sp, #8]
	str	r2, [sp, #12]
.L2477:
	ldr	r3, .L2488+4
	uxth	r7, r4
	sxth	r10, r7
	ldrh	r2, [r3]
	cmp	r10, r2
	bge	.L2484
	sub	r3, r3, #8
	ldr	r2, .L2488+8
	mov	ip, r7
	ldrh	r3, [r3]
	str	r3, [sp]
	ldrh	r3, [r2]
	mov	r2, #0
	mov	r6, r2
	str	r3, [sp, #4]
.L2485:
	ldr	r3, [sp]
	sxth	r1, r2
	cmp	r1, r3
	bge	.L2487
	add	r1, r5, r1
	str	r2, [sp, #20]
	str	ip, [sp, #16]
	ldrb	r0, [r1, #-1704]	@ zero_extendqisi2
	mov	r1, ip
	bl	V2P_block
	bl	FtlBbmIsBadBlock
	cmp	r0, #0
	ldr	r2, [sp, #20]
	ldr	ip, [sp, #16]
	ldreq	r3, [sp, #4]
	add	r2, r2, #1
	addeq	r6, r3, r6
	uxtheq	r6, r6
	b	.L2485
.L2487:
	cmp	r6, #0
	beq	.L2480
	sxth	r1, r6
	mov	r0, #32768
	bl	__aeabi_idiv
	uxth	r6, r0
	b	.L2481
.L2480:
	sxth	r7, r7
	ldr	r2, [r5, #-1404]
	mvn	r1, #0
	mov	r7, r7, asl #1
	strh	r1, [r2, r7]	@ movhi
.L2481:
	mov	r1, r10, asl #1
	ldr	r2, [r5, #-1352]
	add	r0, r1, r10
	add	r2, r2, r0, asl #1
	strh	r6, [r2, #4]	@ movhi
	ldr	r2, .L2488+12
	ldrh	r0, [r2]
	cmp	r10, r0
	beq	.L2482
	ldrh	r0, [r2, #48]
	cmp	r10, r0
	beq	.L2482
	ldrh	r2, [r2, #96]
	cmp	r10, r2
	beq	.L2482
	ldr	r3, [fp, #-1404]
	uxth	r0, r4
	ldrh	r3, [r3, r1]
	cmp	r3, #0
	bne	.L2483
	add	r8, r8, #1
	uxth	r8, r8
	bl	INSERT_FREE_LIST
	b	.L2482
.L2483:
	add	r9, r9, #1
	uxth	r9, r9
	bl	INSERT_DATA_LIST
.L2482:
	add	r4, r4, #1
	b	.L2477
.L2484:
	ldr	r3, [sp, #8]
	mov	r0, #0
	strh	r9, [r3]	@ movhi
	ldr	r3, [sp, #12]
	strh	r8, [r3]	@ movhi
	add	sp, sp, #28
	@ sp needed
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L2489:
	.align	2
.L2488:
	.word	.LANCHOR2
	.word	.LANCHOR2-1724
	.word	.LANCHOR2-1664
	.word	.LANCHOR2+888
	.fnend
	.size	SupperBlkListInit, .-SupperBlkListInit
	.align	2
	.global	Ftl_save_ext_data
	.type	Ftl_save_ext_data, %function
Ftl_save_ext_data:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	@ link register save eliminated.
	ldr	r2, .L2492
	ldr	r3, .L2492+4
	ldr	r1, [r2, #1216]
	cmp	r1, r3
	bxne	lr
	ldr	r3, .L2492+8
	mov	r0, #0
	add	r2, r2, #1216
	str	r3, [r2, #4]
	ldr	r3, [r2, #512]
	str	r3, [r2, #88]
	ldr	r3, [r2, #516]
	str	r3, [r2, #92]
	ldr	r3, .L2492+12
	ldr	r1, [r3, #-1604]
	str	r1, [r2, #8]
	ldr	r1, [r3, #-1600]
	str	r1, [r2, #12]
	ldr	r1, [r3, #-1584]
	str	r1, [r2, #16]
	ldr	r1, [r3, #-1588]
	str	r1, [r2, #20]
	ldr	r1, [r3, #-1576]
	str	r1, [r2, #28]
	ldr	r1, [r3, #-1572]
	str	r1, [r2, #32]
	ldr	r1, [r3, #-1596]
	str	r1, [r2, #36]
	ldr	r1, [r3, #-1592]
	str	r1, [r2, #40]
	ldr	r1, [r3, #-1568]
	str	r1, [r2, #44]
	ldr	r1, [r3, #-1564]
	str	r1, [r2, #48]
	ldr	r1, [r3, #-1616]
	ldr	r3, [r3, #-1620]
	str	r1, [r2, #60]
	mov	r1, #1
	str	r3, [r2, #64]
	b	FtlVendorPartWrite
.L2493:
	.align	2
.L2492:
	.word	.LANCHOR4
	.word	1179929683
	.word	1342177376
	.word	.LANCHOR2
	.fnend
	.size	Ftl_save_ext_data, .-Ftl_save_ext_data
	.align	2
	.global	FtlEctTblFlush
	.type	FtlEctTblFlush, %function
FtlEctTblFlush:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	stmfd	sp!, {r3, lr}
	.save {r3, lr}
	ldr	r3, .L2502
	ldr	r2, [r3, #-1800]
	cmp	r2, #0
	moveq	r2, #32
	beq	.L2495
	ldr	r2, [r3, #-1564]
	cmp	r2, #39
	movhi	r2, #32
	movls	r2, #4
.L2495:
	ldr	lr, .L2502+4
	movw	ip, #1736
	ldrh	r1, [lr, ip]
	cmp	r1, #31
	addls	r1, r1, #1
	strlsh	r1, [lr, ip]	@ movhi
	movls	r2, #1
	cmp	r0, #0
	bne	.L2497
	ldr	r1, [r3, #-1416]
	ldr	r0, [r1, #20]
	ldr	r1, [r1, #16]
	add	r2, r2, r0
	cmp	r1, r2
	bcc	.L2498
.L2497:
	ldr	r2, [r3, #-1416]
	mov	r0, #64
	ldr	r1, [r2, #16]
	str	r1, [r2, #20]
	ldr	r1, .L2502+8
	str	r1, [r2]
	ldr	r2, [r3, #-1416]
	ldr	r3, .L2502+12
	ldrh	r1, [r3]
	mov	r3, r1, asl #9
	str	r3, [r2, #12]
	ldr	r3, [r2, #8]
	add	r3, r3, #1
	str	r3, [r2, #8]
	mov	r3, #0
	str	r3, [r2, #4]
	bl	FtlVendorPartWrite
	bl	Ftl_save_ext_data
.L2498:
	mov	r0, #0
	ldmfd	sp!, {r3, pc}
.L2503:
	.align	2
.L2502:
	.word	.LANCHOR2
	.word	.LANCHOR4
	.word	1112818501
	.word	.LANCHOR2-1424
	.fnend
	.size	FtlEctTblFlush, .-FtlEctTblFlush
	.align	2
	.global	Ftl_load_ext_data
	.type	Ftl_load_ext_data, %function
Ftl_load_ext_data:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	stmfd	sp!, {r3, r4, r5, lr}
	.save {r3, r4, r5, lr}
	mov	r0, #0
	ldr	r5, .L2510
	mov	r1, #1
	ldr	r4, .L2510+4
	add	r2, r5, #1216
	bl	FtlVendorPartRead
	ldr	r3, [r5, #1216]
	cmp	r3, r4
	beq	.L2505
	add	r0, r5, #1216
	mov	r1, #0
	mov	r2, #512
	bl	ftl_memset
	str	r4, [r5, #1216]
.L2505:
	ldr	r2, [r5, #1216]
	ldr	r3, .L2510
	cmp	r2, r4
	ldr	r4, .L2510+8
	bne	.L2506
	ldr	r2, [r3, #1304]
	str	r2, [r3, #1728]
	ldr	r2, [r3, #1308]
	str	r2, [r3, #1732]
	ldr	r2, [r3, #1224]
	str	r2, [r4, #-1604]
	ldr	r2, [r3, #1228]
	str	r2, [r4, #-1600]
	ldr	r2, [r3, #1232]
	str	r2, [r4, #-1584]
	ldr	r2, [r3, #1236]
	str	r2, [r4, #-1588]
	ldr	r2, [r3, #1244]
	str	r2, [r4, #-1576]
	ldr	r2, [r3, #1248]
	str	r2, [r4, #-1572]
	ldr	r2, [r3, #1252]
	str	r2, [r4, #-1596]
	ldr	r2, [r3, #1256]
	str	r2, [r4, #-1592]
	ldr	r2, [r3, #1260]
	str	r2, [r4, #-1568]
	ldr	r2, [r3, #1264]
	ldr	r3, [r3, #1276]
	str	r2, [r4, #-1564]
	str	r3, [r4, #-1616]
.L2506:
	ldr	r0, [r5, #1284]
	mov	r2, #0
	ldr	r1, .L2510+12
	ldr	r3, .L2510+8
	cmp	r0, r1
	str	r2, [r4, #-1620]
	bne	.L2507
	ldrb	r1, [r3, #-2744]	@ zero_extendqisi2
	cmp	r1, r2
	beq	.L2508
	ldr	r3, .L2510
	str	r2, [r3, #1284]
	bl	Ftl_save_ext_data
	b	.L2507
.L2508:
	ldr	r0, .L2510+16
	mov	r2, #1
	ldr	r1, .L2510+20
	str	r2, [r3, #-1800]
	bl	printk
.L2507:
	ldr	r3, .L2510+24
	ldr	r2, [r4, #-1576]
	ldr	r0, [r4, #-1580]
	ldrh	r1, [r3, #-8]
	mla	r0, r0, r1, r2
	ldrh	r1, [r3, #-60]
	bl	__aeabi_uidiv
	str	r0, [r5, #1740]
	ldmfd	sp!, {r3, r4, r5, pc}
.L2511:
	.align	2
.L2510:
	.word	.LANCHOR4
	.word	1179929683
	.word	.LANCHOR2
	.word	305432421
	.word	.LC46
	.word	.LC67
	.word	.LANCHOR2-1664
	.fnend
	.size	Ftl_load_ext_data, .-Ftl_load_ext_data
	.align	2
	.global	ftl_scan_all_ppa
	.type	ftl_scan_all_ppa, %function
ftl_scan_all_ppa:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	stmfd	sp!, {r4, r5, r6, r7, r8, r9, lr}
	.save {r4, r5, r6, r7, r8, r9, lr}
	.pad #28
	sub	sp, sp, #28
	ldr	r5, .L2519
	mov	r7, #0
	ldr	r0, .L2519+4
	sub	r9, r5, #2
	add	r4, r5, #1664
	ldrh	r1, [r5, #-2]
	ldrh	r2, [r5]
	bl	printk
.L2513:
	ldrh	r3, [r9]
	cmp	r7, r3
	bcs	.L2515
	mov	r8, r7, asl #10
	mov	r6, #0
.L2516:
	ldrh	r3, [r5]
	cmp	r6, r3
	bcs	.L2518
	add	r3, r6, r8
	str	r3, [r4, #-1268]
	ldr	r3, [r4, #-1472]
	mov	r2, #0
	mov	r1, #1
	ldr	r0, .L2519+8
	str	r2, [r4, #-1272]
	add	r6, r6, #1
	str	r3, [r4, #-1264]
	ldr	r3, [r4, #-1440]
	str	r3, [r4, #-1260]
	bl	FlashReadPages
	ldr	r3, [r4, #-1260]
	ldr	r2, [r4, #-1264]
	ldr	r0, .L2519+12
	ldr	r1, [r3, #4]
	str	r1, [sp]
	ldr	r1, [r3, #8]
	str	r1, [sp, #4]
	ldr	r1, [r3, #12]
	str	r1, [sp, #8]
	ldr	r1, [r2]
	str	r1, [sp, #12]
	ldr	r2, [r2, #4]
	str	r2, [sp, #16]
	ldr	r1, [r4, #-1268]
	ldr	r2, [r4, #-1272]
	ldr	r3, [r3]
	bl	printk
	b	.L2516
.L2518:
	add	r7, r7, #1
	b	.L2513
.L2515:
	ldr	r0, .L2519+16
	ldr	r1, .L2519+20
	add	sp, sp, #28
	@ sp needed
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, lr}
	b	printk
.L2520:
	.align	2
.L2519:
	.word	.LANCHOR2-1664
	.word	.LC68
	.word	.LANCHOR2-1272
	.word	.LC69
	.word	.LC70
	.word	.LANCHOR3+64
	.fnend
	.size	ftl_scan_all_ppa, .-ftl_scan_all_ppa
	.align	2
	.global	update_multiplier_value
	.type	update_multiplier_value, %function
update_multiplier_value:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	stmfd	sp!, {r3, r4, r5, r6, r7, r8, r9, lr}
	.save {r3, r4, r5, r6, r7, r8, r9, lr}
	mov	r5, #0
	ldr	r3, .L2531
	mov	r7, r0
	mov	r4, r5
	add	r6, r3, #24
	ldrh	r8, [r3, #-4]
	ldrh	r9, [r3, #64]
.L2522:
	uxth	r3, r5
	cmp	r3, r8
	bcs	.L2530
	ldrb	r0, [r6, r5]	@ zero_extendqisi2
	mov	r1, r7
	bl	V2P_block
	add	r5, r5, #1
	bl	FtlBbmIsBadBlock
	cmp	r0, #0
	addeq	r4, r4, r9
	uxtheq	r4, r4
	b	.L2522
.L2530:
	cmp	r4, #0
	beq	.L2525
	mov	r1, r4
	mov	r0, #32768
	bl	__aeabi_idiv
	uxth	r4, r0
.L2525:
	ldr	r3, .L2531+4
	mov	r2, #6
	mov	r0, #0
	ldr	r3, [r3, #-1352]
	mla	r7, r2, r7, r3
	strh	r4, [r7, #4]	@ movhi
	ldmfd	sp!, {r3, r4, r5, r6, r7, r8, r9, pc}
.L2532:
	.align	2
.L2531:
	.word	.LANCHOR2-1728
	.word	.LANCHOR2
	.fnend
	.size	update_multiplier_value, .-update_multiplier_value
	.align	2
	.global	GetFreeBlockMinEraseCount
	.type	GetFreeBlockMinEraseCount, %function
GetFreeBlockMinEraseCount:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	@ link register save eliminated.
	ldr	r2, .L2536
	ldr	r0, [r2, #880]
	cmp	r0, #0
	bxeq	lr
	ldr	r3, [r2, #-1352]
	rsb	r0, r3, r0
	ldr	r3, .L2536+4
	mov	r0, r0, asr #1
	mul	r0, r3, r0
	ldr	r3, [r2, #-1412]
	uxth	r0, r0
	mov	r0, r0, asl #1
	ldrh	r0, [r3, r0]
	bx	lr
.L2537:
	.align	2
.L2536:
	.word	.LANCHOR2
	.word	-1431655765
	.fnend
	.size	GetFreeBlockMinEraseCount, .-GetFreeBlockMinEraseCount
	.align	2
	.global	GetFreeBlockMaxEraseCount
	.type	GetFreeBlockMaxEraseCount, %function
GetFreeBlockMaxEraseCount:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	ldr	r1, .L2548
	ldr	r3, [r1, #880]
	cmp	r3, #0
	beq	.L2544
	add	r2, r1, #884
	stmfd	sp!, {r4, r5, lr}
	.save {r4, r5, lr}
	mov	r4, #6
	ldrh	r2, [r2]
	movw	r5, #65535
	ldr	ip, [r1, #-1352]
	rsb	r2, r2, r2, asl #3
	rsb	r3, ip, r3
	mov	r2, r2, asr #3
	mov	r3, r3, asr #1
	cmp	r0, r2
	uxthgt	r0, r2
	ldr	r2, .L2548+4
	mul	r3, r2, r3
	mov	r2, #0
	uxth	r3, r3
.L2541:
	uxth	lr, r2
	cmp	lr, r0
	bcs	.L2543
	mul	lr, r4, r3
	add	r2, r2, #1
	ldrh	lr, [ip, lr]
	cmp	lr, r5
	bne	.L2545
.L2543:
	ldr	r2, [r1, #-1412]
	mov	r3, r3, asl #1
	ldrh	r0, [r2, r3]
	ldmfd	sp!, {r4, r5, pc}
.L2545:
	mov	r3, lr
	b	.L2541
.L2544:
	mov	r0, r3
	bx	lr
.L2549:
	.align	2
.L2548:
	.word	.LANCHOR2
	.word	-1431655765
	.fnend
	.size	GetFreeBlockMaxEraseCount, .-GetFreeBlockMaxEraseCount
	.align	2
	.global	FtlPrintInfo2buf
	.type	FtlPrintInfo2buf, %function
FtlPrintInfo2buf:
	.fnstart
	@ args = 0, pretend = 0, frame = 16
	@ frame_needed = 0, uses_anonymous_args = 0
	stmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, lr}
	.save {r4, r5, r6, r7, r8, r9, r10, lr}
	mov	r7, r0
	ldr	r6, .L2561
	add	r5, r7, #12
	ldr	r1, .L2561+4
	.pad #32
	sub	sp, sp, #32
	bl	strcpy
	mov	r0, r5
	ldr	r1, .L2561+8
	ldr	r2, [r6, #-2772]
	bl	sprintf
	ldr	r1, .L2561+12
	ldr	r2, [r6, #-1648]
	add	r5, r5, r0
	mov	r0, r5
	bl	sprintf
	ldr	r3, .L2561+16
	ldr	r3, [r3, #3348]
	cmp	r3, #1
	add	r4, r5, r0
	bne	.L2556
	add	r0, sp, #16
	add	r1, sp, #20
	add	r2, sp, #24
	add	r3, sp, #28
	bl	NandcGetTimeCfg
	mov	r0, r4
	ldr	r1, .L2561+20
	sub	r8, r6, #1344
	add	r9, r6, #884
	add	r10, r6, #820
	ldr	r3, [sp, #24]
	ldr	r2, [sp, #16]
	str	r3, [sp]
	ldr	r3, [sp, #28]
	str	r3, [sp, #4]
	ldr	r3, [sp, #20]
	bl	sprintf
	ldr	r1, .L2561+24
	add	r4, r4, r0
	add	r5, r4, #10
	mov	r0, r4
	ldr	r4, .L2561+28
	bl	strcpy
	mov	r0, r5
	ldr	r1, .L2561+32
	ldr	r2, [r6, #-1280]
	bl	sprintf
	ldr	r1, .L2561+36
	ldr	r2, [r6, #1128]
	add	r5, r5, r0
	mov	r0, r5
	bl	sprintf
	ldr	r1, .L2561+40
	ldr	r2, [r6, #-1584]
	add	r5, r5, r0
	mov	r0, r5
	bl	sprintf
	ldr	r1, .L2561+44
	ldr	r2, [r6, #-1596]
	add	r5, r5, r0
	mov	r0, r5
	bl	sprintf
	ldr	r1, .L2561+48
	ldr	r2, [r6, #-1600]
	add	r5, r5, r0
	mov	r0, r5
	bl	sprintf
	ldr	r1, .L2561+52
	ldr	r2, [r6, #-1592]
	add	r5, r5, r0
	mov	r0, r5
	bl	sprintf
	ldr	r1, .L2561+56
	ldr	r2, [r6, #-1588]
	add	r5, r5, r0
	mov	r0, r5
	bl	sprintf
	ldr	r1, .L2561+60
	ldr	r2, [r6, #-1604]
	add	r5, r5, r0
	mov	r0, r5
	bl	sprintf
	ldr	r2, [r4, #1728]
	ldr	r1, .L2561+64
	mov	r2, r2, lsr #11
	add	r5, r5, r0
	mov	r0, r5
	bl	sprintf
	ldr	r2, [r4, #1732]
	ldr	r1, .L2561+68
	mov	r2, r2, lsr #11
	add	r5, r5, r0
	mov	r0, r5
	bl	sprintf
	ldr	r1, .L2561+72
	ldr	r2, [r6, #-1612]
	add	r5, r5, r0
	mov	r0, r5
	bl	sprintf
	ldr	r1, .L2561+76
	ldr	r2, [r6, #-1608]
	add	r5, r5, r0
	mov	r0, r5
	bl	sprintf
	add	r5, r5, r0
	bl	FtlBbtCalcTotleCnt
	ldrh	r2, [r8, #6]
	ldr	r1, .L2561+80
	mov	r3, r0
	mov	r0, r5
	bl	sprintf
	ldr	r1, .L2561+84
	ldrh	r2, [r9]
	add	r5, r5, r0
	mov	r0, r5
	bl	sprintf
	ldr	r1, .L2561+88
	ldr	r2, [r6, #-1580]
	add	r5, r5, r0
	mov	r0, r5
	bl	sprintf
	ldr	r1, .L2561+92
	ldr	r2, [r6, #-1576]
	add	r5, r5, r0
	mov	r0, r5
	bl	sprintf
	ldr	r2, [r4, #1740]
	ldr	r1, .L2561+96
	add	r5, r5, r0
	mov	r0, r5
	bl	sprintf
	ldr	r1, .L2561+100
	ldr	r2, [r6, #-1572]
	add	r5, r5, r0
	mov	r0, r5
	bl	sprintf
	ldr	r1, .L2561+104
	ldr	r2, [r6, #-1568]
	add	r5, r5, r0
	mov	r0, r5
	bl	sprintf
	ldr	r1, .L2561+108
	ldr	r2, [r6, #-1564]
	add	r5, r5, r0
	mov	r0, r5
	bl	sprintf
	ldrh	r2, [r10, #30]
	ldr	r1, .L2561+112
	add	r5, r5, r0
	mov	r0, r5
	bl	sprintf
	ldrh	r2, [r10, #28]
	ldr	r1, .L2561+116
	sub	r10, r6, #1520
	add	r5, r5, r0
	mov	r0, r5
	bl	sprintf
	ldr	r1, .L2561+120
	ldr	r2, [r6, #-2740]
	add	r5, r5, r0
	mov	r0, r5
	bl	sprintf
	ldr	r1, .L2561+124
	ldr	r2, [r6, #-1632]
	add	r5, r5, r0
	mov	r0, r5
	bl	sprintf
	ldr	r1, .L2561+128
	ldr	r2, [r6, #-1736]
	add	r5, r5, r0
	mov	r0, r5
	bl	sprintf
	ldrh	r2, [r8, #114]
	ldr	r1, .L2561+132
	add	r5, r5, r0
	mov	r0, r5
	bl	sprintf
	sub	r3, r6, #1712
	ldr	r1, .L2561+136
	ldrh	r2, [r3, #-12]
	add	r5, r5, r0
	mov	r0, r5
	bl	sprintf
	movw	r3, #1164
	ldrh	r2, [r4, r3]
	ldr	r1, .L2561+140
	add	r5, r5, r0
	mov	r0, r5
	bl	sprintf
	ldr	r1, .L2561+144
	ldr	r2, [r6, #-1720]
	add	r5, r5, r0
	mov	r0, r5
	bl	sprintf
	movw	r3, #1132
	ldrh	r2, [r6, r3]
	ldr	r1, .L2561+148
	add	r5, r5, r0
	mov	r0, r5
	bl	sprintf
	ldrh	r2, [r8]
	ldr	r1, .L2561+152
	add	r8, r6, #888
	add	r5, r5, r0
	mov	r0, r5
	bl	sprintf
	ldrh	r2, [r8, #2]
	ldr	r1, .L2561+156
	add	r5, r5, r0
	mov	r0, r5
	bl	sprintf
	ldr	r1, .L2561+160
	ldrb	r2, [r6, #894]	@ zero_extendqisi2
	add	r5, r5, r0
	mov	r0, r5
	bl	sprintf
	ldrh	r2, [r8]
	ldr	r1, .L2561+164
	add	r5, r5, r0
	mov	r0, r5
	bl	sprintf
	ldr	r1, .L2561+168
	ldrb	r2, [r6, #896]	@ zero_extendqisi2
	add	r5, r5, r0
	mov	r0, r5
	bl	sprintf
	ldrh	r2, [r8, #4]
	ldr	r1, .L2561+172
	add	r5, r5, r0
	mov	r0, r5
	bl	sprintf
	ldrh	r3, [r8]
	ldr	r2, [r6, #-1404]
	add	r8, r6, #936
	ldr	r1, .L2561+176
	mov	r3, r3, asl #1
	ldrh	r2, [r2, r3]
	add	r5, r5, r0
	mov	r0, r5
	bl	sprintf
	ldrh	r2, [r8, #2]
	ldr	r1, .L2561+180
	add	r5, r5, r0
	mov	r0, r5
	bl	sprintf
	ldr	r1, .L2561+184
	ldrb	r2, [r6, #942]	@ zero_extendqisi2
	add	r5, r5, r0
	mov	r0, r5
	bl	sprintf
	ldrh	r2, [r8]
	ldr	r1, .L2561+188
	add	r5, r5, r0
	mov	r0, r5
	bl	sprintf
	ldr	r1, .L2561+192
	ldrb	r2, [r6, #944]	@ zero_extendqisi2
	add	r5, r5, r0
	mov	r0, r5
	bl	sprintf
	ldrh	r2, [r8, #4]
	ldr	r1, .L2561+196
	add	r5, r5, r0
	mov	r0, r5
	bl	sprintf
	ldrh	r3, [r8]
	ldr	r2, [r6, #-1404]
	add	r8, r6, #984
	ldr	r1, .L2561+200
	mov	r3, r3, asl #1
	ldrh	r2, [r2, r3]
	add	r5, r5, r0
	mov	r0, r5
	bl	sprintf
	ldrh	r2, [r8, #2]
	ldr	r1, .L2561+204
	add	r5, r5, r0
	mov	r0, r5
	bl	sprintf
	ldr	r1, .L2561+208
	ldrb	r2, [r6, #990]	@ zero_extendqisi2
	add	r5, r5, r0
	mov	r0, r5
	bl	sprintf
	ldrh	r2, [r8]
	ldr	r1, .L2561+212
	add	r5, r5, r0
	mov	r0, r5
	bl	sprintf
	ldr	r1, .L2561+216
	ldrb	r2, [r6, #992]	@ zero_extendqisi2
	add	r5, r5, r0
	mov	r0, r5
	bl	sprintf
	ldrh	r2, [r8, #4]
	ldr	r1, .L2561+220
	add	r8, r4, #1168
	add	r5, r5, r0
	mov	r0, r5
	bl	sprintf
	ldr	r1, .L2561+224
	ldrh	r2, [r8, #2]
	add	r5, r5, r0
	mov	r0, r5
	bl	sprintf
	ldrb	r2, [r4, #1174]	@ zero_extendqisi2
	ldr	r1, .L2561+228
	add	r5, r5, r0
	mov	r0, r5
	bl	sprintf
	ldr	r1, .L2561+232
	ldrh	r2, [r8]
	add	r5, r5, r0
	mov	r0, r5
	bl	sprintf
	ldrb	r2, [r4, #1176]	@ zero_extendqisi2
	ldr	r1, .L2561+236
	add	r5, r5, r0
	mov	r0, r5
	bl	sprintf
	ldr	r1, .L2561+240
	ldrh	r2, [r8, #4]
	add	r5, r5, r0
	mov	r0, r5
	bl	sprintf
	ldr	r1, [r4, #1300]
	ldr	r2, [r6, #-1616]
	ldr	r3, [r6, #-1800]
	orr	r2, r3, r2, asl #8
	str	r1, [sp]
	add	r5, r5, r0
	ldr	r1, [r4, #1292]
	mov	r0, r5
	str	r1, [sp, #4]
	ldr	r1, .L2561+244
	ldr	r3, [r4, #1296]
	bl	sprintf
	ldr	r2, [r4, #1288]
	ldr	r1, .L2561+248
	add	r5, r5, r0
	mov	r0, r5
	bl	sprintf
	ldr	r2, [r4, #1312]
	ldr	r1, .L2561+252
	add	r5, r5, r0
	mov	r0, r5
	bl	sprintf
	ldr	r1, .L2561+256
	ldrh	r2, [r10, #-8]
	add	r5, r5, r0
	mov	r0, r5
	bl	sprintf
	ldr	r1, .L2561+260
	ldrh	r2, [r10, #-6]
	add	r4, r5, r0
	mov	r0, r4
	bl	sprintf
	ldr	r1, .L2561+264
	ldr	r2, [r6, #-1544]
	add	r4, r4, r0
	mov	r0, r4
	bl	sprintf
	ldr	r1, .L2561+268
	ldrh	r2, [r10, #-4]
	add	r4, r4, r0
	mov	r0, r4
	bl	sprintf
	add	r4, r4, r0
	bl	GetFreeBlockMinEraseCount
	ldr	r1, .L2561+272
	mov	r2, r0
	mov	r0, r4
	bl	sprintf
	add	r4, r4, r0
	ldrh	r0, [r9]
	bl	GetFreeBlockMaxEraseCount
	ldr	r1, .L2561+276
	mov	r2, r0
	mov	r0, r4
	bl	sprintf
	ldrh	r3, [r8]
	movw	r2, #65535
	cmp	r3, r2
	add	r4, r4, r0
	beq	.L2553
	ldr	r2, [r6, #-1404]
	mov	r3, r3, asl #1
	mov	r0, r4
	ldr	r1, .L2561+280
	ldrh	r2, [r2, r3]
	bl	sprintf
	add	r4, r4, r0
.L2553:
	mov	r0, #0
	mov	r5, #0
	bl	List_get_gc_head_node
	movw	r10, #65535
	mov	r9, #6
	uxth	r3, r0
.L2555:
	cmp	r3, r10
	beq	.L2554
	ldr	r2, [r6, #-1404]
	mov	r1, r3, asl #1
	mul	r8, r9, r3
	mov	r0, r4
	ldrh	r2, [r2, r1]
	str	r2, [sp]
	ldr	r2, [r6, #-1352]
	add	r2, r2, r8
	ldrh	r2, [r2, #4]
	str	r2, [sp, #4]
	ldr	r2, [r6, #-1412]
	ldrh	r2, [r2, r1]
	ldr	r1, .L2561+284
	str	r2, [sp, #8]
	mov	r2, r5
	bl	sprintf
	add	r5, r5, #1
	ldr	r3, [r6, #-1352]
	cmp	r5, #16
	ldrh	r3, [r3, r8]
	add	r4, r4, r0
	bne	.L2555
.L2554:
	ldr	r2, [r6, #-1352]
	mov	r5, #0
	ldr	r3, [r6, #880]
	movw	r10, #65535
	mov	r9, #6
	rsb	r3, r2, r3
	ldr	r2, .L2561+288
	mov	r3, r3, asr #1
	mul	r3, r2, r3
	uxth	r3, r3
.L2557:
	cmp	r3, r10
	beq	.L2556
	mul	r8, r9, r3
	ldr	r2, [r6, #-1352]
	ldr	r1, [r6, #-1412]
	mov	r0, r4
	add	r2, r2, r8
	ldrh	r2, [r2, #4]
	str	r2, [sp]
	mov	r2, r3, asl #1
	ldrh	r2, [r1, r2]
	ldr	r1, .L2561+292
	str	r2, [sp, #4]
	mov	r2, r5
	bl	sprintf
	add	r5, r5, #1
	ldr	r3, [r6, #-1352]
	cmp	r5, #4
	ldrh	r3, [r3, r8]
	add	r4, r4, r0
	bne	.L2557
.L2556:
	rsb	r0, r7, r4
	add	sp, sp, #32
	@ sp needed
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, pc}
.L2562:
	.align	2
.L2561:
	.word	.LANCHOR2
	.word	.LC71
	.word	.LC72
	.word	.LC73
	.word	.LANCHOR1
	.word	.LC74
	.word	.LC75
	.word	.LANCHOR4
	.word	.LC76
	.word	.LC77
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
	.word	-1431655765
	.word	.LC140
	.fnend
	.size	FtlPrintInfo2buf, .-FtlPrintInfo2buf
	.align	2
	.global	ftl_proc_ftl_read
	.type	ftl_proc_ftl_read, %function
ftl_proc_ftl_read:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	stmfd	sp!, {r3, r4, r5, lr}
	.save {r3, r4, r5, lr}
	mov	r5, r0
	ldr	r1, .L2565
	ldr	r2, .L2565+4
	bl	sprintf
	add	r4, r5, r0
	mov	r0, r4
	bl	FtlPrintInfo2buf
	add	r0, r4, r0
	rsb	r0, r5, r0
	ldmfd	sp!, {r3, r4, r5, pc}
.L2566:
	.align	2
.L2565:
	.word	.LC46
	.word	.LC141
	.fnend
	.size	ftl_proc_ftl_read, .-ftl_proc_ftl_read
	.align	2
	.global	GetSwlReplaceBlock
	.type	GetSwlReplaceBlock, %function
GetSwlReplaceBlock:
	.fnstart
	@ args = 0, pretend = 0, frame = 8
	@ frame_needed = 0, uses_anonymous_args = 0
	stmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.save {r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.pad #28
	sub	sp, sp, #28
	ldr	r5, .L2597
	ldr	r4, .L2597+4
	ldr	r6, [r5, #1740]
	ldr	r3, [r4, #-1564]
	cmp	r6, r3
	bcs	.L2568
	sub	r3, r4, #1712
	mov	r6, #0
	str	r6, [r4, #-1580]
	ldrh	r1, [r3, #-12]
	ldr	r3, [r4, #-1412]
	sub	r3, r3, #2
.L2569:
	cmp	r6, r1
	bcs	.L2596
	ldrh	r2, [r3, #2]!
	add	r6, r6, #1
	ldr	r0, [r4, #-1580]
	add	r2, r2, r0
	str	r2, [r4, #-1580]
	b	.L2569
.L2596:
	ldr	r6, [r4, #-1580]
	mov	r0, r6
	bl	__aeabi_uidiv
	ldr	r3, .L2597+8
	ldrh	r1, [r3, #-8]
	str	r0, [r5, #1740]
	ldr	r0, [r4, #-1576]
	rsb	r0, r0, r6
	bl	__aeabi_uidiv
	str	r0, [r4, #-1580]
	b	.L2571
.L2568:
	ldr	r3, [r4, #-1568]
	cmp	r6, r3
	bls	.L2571
	ldr	ip, .L2597+12
	add	r3, r3, #1
	mov	lr, r4
	str	r3, [r4, #-1568]
	mov	r3, #0
.L2573:
	ldrh	r2, [ip]
	cmp	r3, r2
	bcs	.L2571
	ldr	r0, [lr, #-1412]
	mov	r1, r3, asl #1
	add	r3, r3, #1
	ldrh	r2, [r0, r1]
	add	r2, r2, #1
	strh	r2, [r0, r1]	@ movhi
	b	.L2573
.L2571:
	ldr	r6, [r4, #-1564]
	ldr	r8, [r5, #1740]
	add	r3, r6, #256
	ldr	r2, .L2597+4
	cmp	r3, r8
	bls	.L2576
	ldr	r1, [r2, #-1568]
	add	r3, r6, #768
	cmp	r3, r1
	bls	.L2576
	cmp	r6, #40
	ldr	r2, [r2, #-1800]
	movls	r3, #0
	movhi	r3, #1
	cmp	r2, #0
	orreq	r3, r3, #1
	cmp	r3, #0
	beq	.L2576
.L2578:
	movw	r0, #65535
	b	.L2577
.L2576:
	ldr	r10, .L2597+4
	add	r3, r10, #884
	ldrh	r0, [r3]
	add	r0, r0, r0, asl #1
	ubfx	r0, r0, #2, #16
	bl	GetFreeBlockMaxEraseCount
	add	r1, r6, #64
	cmp	r0, r1
	mov	r9, r0
	movcs	r1, #0
	movcc	r1, #1
	cmp	r6, #40
	movls	r1, #0
	cmp	r1, #0
	bne	.L2578
	ldr	r3, [r10, #868]
	cmp	r3, #0
	beq	.L2578
	sub	r2, r10, #1712
	ldr	r0, [r10, #-1352]
	ldr	ip, .L2597+16
	movw	r7, #65535
	ldrh	r2, [r2, #-12]
	mov	r5, r7
	ldr	r10, [r10, #-1412]
	mov	lr, #6
	str	r2, [sp, #20]
.L2579:
	ldrh	r2, [r3]
	movw	fp, #65535
	cmp	r2, fp
	beq	.L2581
	add	r1, r1, #1
	ldr	fp, [sp, #20]
	uxth	r1, r1
	cmp	r1, fp
	bhi	.L2578
	ldrh	fp, [r3, #4]
	cmp	fp, #0
	beq	.L2580
	rsb	r3, r0, r3
	mov	r3, r3, asr #1
	mul	r3, ip, r3
	uxth	r3, r3
	mov	fp, r3, asl #1
	ldrh	fp, [r10, fp]
	cmp	fp, r6
	bls	.L2585
	cmp	fp, r7
	movcc	r7, fp
	movcc	r5, r3
.L2580:
	mla	r3, lr, r2, r0
	b	.L2579
.L2585:
	mov	r5, r3
.L2581:
	movw	r3, #65535
	cmp	r5, r3
	beq	.L2578
	mov	r3, r5, asl #1
	ldrh	fp, [r10, r3]
	cmp	fp, r6
	bls	.L2583
	str	r3, [sp, #20]
	bl	GetFreeBlockMinEraseCount
	ldr	r3, [sp, #20]
	cmp	r0, r6
	strhi	r7, [r4, #-1564]
.L2583:
	cmp	fp, r8
	bcs	.L2578
	add	r2, fp, #128
	cmp	r9, r2
	ble	.L2578
	add	r2, fp, #256
	cmp	r2, r8
	bcc	.L2584
	ldr	r2, [r4, #-1568]
	add	fp, fp, #768
	cmp	fp, r2
	bcs	.L2578
.L2584:
	ldr	r2, [r4, #-1404]
	mov	r1, r5
	ldr	r0, .L2597+20
	ldrh	r2, [r2, r3]
	str	r2, [sp]
	mov	r2, r8
	ldrh	r3, [r10, r3]
	stmib	sp, {r3, r9}
	ldr	r3, [r4, #-1568]
	bl	printk
	mov	r0, r5
	mov	r3, #1
	str	r3, [r4, #-1556]
.L2577:
	add	sp, sp, #28
	@ sp needed
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L2598:
	.align	2
.L2597:
	.word	.LANCHOR4
	.word	.LANCHOR2
	.word	.LANCHOR2-1664
	.word	.LANCHOR2-1724
	.word	-1431655765
	.word	.LC142
	.fnend
	.size	GetSwlReplaceBlock, .-GetSwlReplaceBlock
	.align	2
	.global	free_data_superblock
	.type	free_data_superblock, %function
free_data_superblock:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	movw	r2, #65535
	cmp	r0, r2
	stmfd	sp!, {r3, lr}
	.save {r3, lr}
	beq	.L2600
	ldr	r2, .L2602
	mov	r3, r0, asl #1
	mov	r1, #0
	ldr	r2, [r2, #-1404]
	strh	r1, [r2, r3]	@ movhi
	bl	INSERT_FREE_LIST
.L2600:
	mov	r0, #0
	ldmfd	sp!, {r3, pc}
.L2603:
	.align	2
.L2602:
	.word	.LANCHOR2
	.fnend
	.size	free_data_superblock, .-free_data_superblock
	.align	2
	.global	allocate_data_superblock
	.type	allocate_data_superblock, %function
allocate_data_superblock:
	.fnstart
	@ args = 0, pretend = 0, frame = 16
	@ frame_needed = 0, uses_anonymous_args = 0
	stmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.save {r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.pad #20
	sub	sp, sp, #20
	ldr	r4, .L2659
	ldr	r3, [r4, #-1276]
	cmp	r3, #0
	bne	.L2605
	mov	r5, r0
	mov	r6, r4
.L2606:
	ldr	r3, .L2659+4
	ldrb	r2, [r5, #8]	@ zero_extendqisi2
	cmp	r5, r3
	sub	r7, r3, #100
	bne	.L2607
	ldrh	r3, [r7]
	ldr	ip, [r6, #-1556]
	mov	r0, r3, lsr #1
	mul	lr, ip, r3
	add	r1, r0, #1
	add	r1, r1, lr, lsr #2
	ldr	lr, [r6, #-1800]
	cmp	lr, #0
	uxth	r1, r1
	beq	.L2608
	ldr	lr, [r6, #-1564]
	cmp	lr, #39
	bhi	.L2608
	cmp	lr, #2
	bls	.L2633
	cmp	ip, #0
	movne	r3, #0
	andeq	r3, r3, #1
	cmp	r3, #0
	moveq	r1, r0
	beq	.L2608
	b	.L2633
.L2607:
	cmp	r2, #1
	bne	.L2633
	ldr	r3, .L2659+8
	ldrh	r3, [r3]
	cmp	r3, #1
	beq	.L2633
	ldrb	r3, [r6, #-2744]	@ zero_extendqisi2
	cmp	r3, #0
	bne	.L2633
	ldr	r0, [r6, #-1800]
	ldrh	r3, [r7]
	cmp	r0, #0
	mov	r1, r3, lsr #3
	beq	.L2608
	ldr	r0, [r6, #-1564]
	cmp	r0, #1
	rsbls	r3, r3, r3, asl #3
	ubfxls	r1, r3, #3, #16
.L2608:
	cmp	r1, #0
	subne	r1, r1, #1
	uxthne	r1, r1
	b	.L2609
.L2633:
	mov	r1, #0
.L2609:
	ldr	r0, .L2659+12
	bl	List_pop_index_node
	ldr	r10, .L2659+16
	ldrh	r3, [r7]
	sub	r3, r3, #1
	strh	r3, [r7]	@ movhi
	ldrh	r3, [r10]
	uxth	r9, r0
	cmp	r3, r9
	bls	.L2606
	ldr	r3, [r6, #-1404]
	mov	r8, r9, asl #1
	ldrh	r7, [r3, r8]
	cmp	r7, #0
	bne	.L2606
	strh	r9, [r5]	@ movhi
	mov	r0, r5
	bl	make_superblock
	ldrb	r3, [r5, #7]	@ zero_extendqisi2
	cmp	r3, #0
	beq	.L2611
	ldr	r0, [r6, #-1488]
	mov	r2, #36
	ldrh	lr, [r10, #-8]
	add	ip, r5, #14
	mov	r3, r0
	mla	r1, r2, lr, r0
	mov	lr, r7
	str	r1, [sp, #4]
	b	.L2612
.L2611:
	ldr	r3, [r6, #-1404]
	b	.L2654
.L2612:
	ldr	r1, [sp, #4]
	cmp	r3, r1
	beq	.L2656
	str	lr, [r3, #8]
	movw	fp, #65535
	str	lr, [r3, #12]
	add	r3, r3, #36
	ldrh	r1, [ip, #2]!
	cmp	r1, fp
	movne	r1, r1, asl #10
	mlane	fp, r2, r7, r0
	addne	r7, r7, #1
	uxthne	r7, r7
	strne	r1, [fp, #4]
	b	.L2612
.L2656:
	ldr	r3, .L2659+20
	ldr	r2, [r4, #-1800]
	rsb	r3, r3, r5
	clz	r3, r3
	cmp	r2, #0
	mov	r3, r3, lsr #5
	moveq	r3, #0
	cmp	r3, #0
	beq	.L2615
	ldr	r3, [r6, #-1412]
	ldrh	r3, [r3, r8]
	cmp	r3, #40
	movhi	r3, #0
	strhib	r3, [r6, #896]
.L2615:
	ldrb	r3, [r5, #8]	@ zero_extendqisi2
	ldr	r2, [r4, #-1412]
	cmp	r3, #0
	ldrh	r3, [r2, r8]
	bne	.L2616
	cmp	r3, #0
	mov	r0, r9
	ldrne	r1, .L2659+24
	moveq	r3, #2
	ldrneh	r1, [r1]
	addne	r3, r3, r1
	mov	r1, #0
	uxthne	r3, r3
	strh	r3, [r2, r8]	@ movhi
	ldr	r3, [r4, #-1580]
	add	r3, r3, #1
	str	r3, [r4, #-1580]
	bl	ftl_set_blk_mode
	b	.L2618
.L2616:
	add	r3, r3, #1
	strh	r3, [r2, r8]	@ movhi
	ldr	r1, [r4, #-1392]
	mov	r0, r9, lsr #5
	ldr	r3, [r4, #-1576]
	mov	ip, #1
	add	r3, r3, #1
	str	r3, [r4, #-1576]
	ldr	r2, [r1, r0, asl #2]
	and	r3, r9, #31
	orr	r3, r2, ip, asl r3
	str	r3, [r1, r0, asl #2]
.L2618:
	ldr	r3, [r4, #-1412]
	ldr	r2, [r4, #-1568]
	ldr	r0, [r4, #-1580]
	ldrh	r3, [r3, r8]
	ldrh	r1, [r10]
	cmp	r3, r2
	strhi	r3, [r6, #-1568]
	ldr	r3, .L2659+24
	ldrh	r2, [r3]
	ldr	r3, [r4, #-1576]
	mla	r0, r0, r2, r3
	bl	__aeabi_uidiv
	ldr	r2, [r4, #-1416]
	ldr	r3, .L2659+28
	str	r0, [r3, #1740]
	ldr	r3, [r2, #16]
	add	r3, r3, #1
	str	r3, [r2, #16]
	ldr	r2, [r4, #-1488]
	mov	r3, #36
	add	r1, r2, #4
	mla	r3, r3, r7, r2
	add	r3, r3, #40
.L2620:
	add	r1, r1, #36
	cmp	r1, r3
	ldrne	r2, [r1, #-36]
	bicne	r2, r2, #1020
	bicne	r2, r2, #3
	strne	r2, [r1, #-36]
	bne	.L2620
.L2657:
	ldrb	r3, [r4, #-2744]	@ zero_extendqisi2
	cmp	r3, #0
	beq	.L2622
	ldrb	r3, [r5, #8]	@ zero_extendqisi2
	mov	r2, r7
	ldr	r0, [r6, #-1488]
	cmp	r3, #1
	moveq	r1, #0
	movne	r1, #1
	bl	FlashEraseBlocks
.L2622:
	ldrb	r1, [r5, #8]	@ zero_extendqisi2
	mov	r2, r7
	ldr	r0, [r4, #-1488]
	mov	r10, #0
	bl	FlashEraseBlocks
	add	r2, r5, #16
	mov	fp, r10
	mov	r1, #36
.L2624:
	uxth	r3, r10
	cmp	r3, r7
	bcs	.L2658
	mul	r3, r1, r10
	ldr	ip, [r4, #-1488]
	add	r0, ip, r3
	ldr	r3, [ip, r3]
	cmn	r3, #1
	bne	.L2625
	ldr	r0, [r0, #4]
	add	fp, fp, #1
	str	r1, [sp, #12]
	ubfx	r0, r0, #10, #16
	stmib	sp, {r2, r3}
	bl	FtlBbmMapBadBlock
	ldmib	sp, {r2, r3}
	ldr	r1, [sp, #12]
	strh	r3, [r2]	@ movhi
	ldrb	r3, [r5, #7]	@ zero_extendqisi2
	sub	r3, r3, #1
	strb	r3, [r5, #7]
.L2625:
	add	r10, r10, #1
	add	r2, r2, #2
	b	.L2624
.L2658:
	cmp	fp, #0
	beq	.L2627
	mov	r0, r9
	bl	update_multiplier_value
	bl	FtlBbmTblFlush
.L2627:
	ldrb	r3, [r5, #7]	@ zero_extendqisi2
	cmp	r3, #0
	bne	.L2628
	ldr	r3, [r4, #-1404]
.L2654:
	mvn	r2, #0
	strh	r2, [r3, r8]	@ movhi
	b	.L2606
.L2628:
	ldr	r2, .L2659+32
	ldrh	r2, [r2]
	strh	r9, [r5]	@ movhi
	smulbb	r3, r2, r3
	mov	r2, #0
	strh	r2, [r5, #2]	@ movhi
	strb	r2, [r5, #6]
	ldr	r2, [r4, #-1612]
	ldr	r1, [r4, #-1404]
	uxth	r3, r3
	strh	r3, [r5, #4]	@ movhi
	str	r2, [r5, #12]
	add	r2, r2, #1
	str	r2, [r4, #-1612]
	ldrh	r2, [r5]
	mov	r2, r2, asl #1
	strh	r3, [r1, r2]	@ movhi
.L2605:
	mov	r0, #0
	add	sp, sp, #20
	@ sp needed
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L2660:
	.align	2
.L2659:
	.word	.LANCHOR2
	.word	.LANCHOR2+984
	.word	.LANCHOR2-1712
	.word	.LANCHOR2+880
	.word	.LANCHOR2-1724
	.word	.LANCHOR2+888
	.word	.LANCHOR2-1672
	.word	.LANCHOR4
	.word	.LANCHOR2-1664
	.fnend
	.size	allocate_data_superblock, .-allocate_data_superblock
	.align	2
	.global	FtlGcBufInit
	.type	FtlGcBufInit, %function
FtlGcBufInit:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	stmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, lr}
	.save {r4, r5, r6, r7, r8, r9, r10, lr}
	mov	r4, #12
	ldr	lr, .L2669
	mov	r5, #1
	ldr	r2, .L2669+4
	mov	r7, #36
	ldr	r1, .L2669+8
	add	r6, lr, #78
	mov	r3, #0
	str	r3, [r2, #1744]
.L2662:
	ldrh	r2, [lr]
	add	ip, r3, #1
	uxth	r3, r3
	ldr	r0, .L2669+8
	cmp	r3, r2
	bcs	.L2667
	mul	r0, r4, r3
	ldr	r8, [r1, #-1444]
	add	r2, r8, r0
	str	r5, [r2, #8]
	ldrh	r2, [r6]
	mul	r2, r2, r3
	add	r9, r2, #3
	cmp	r2, #0
	movlt	r2, r9
	ldr	r9, [r1, #-1460]
	bic	r2, r2, #3
	add	r2, r9, r2
	str	r2, [r8, r0]
	ldr	r2, .L2669+12
	ldr	r9, [r1, #-1444]
	ldrh	r2, [r2]
	add	r8, r9, r0
	mul	r2, r2, r3
	add	r10, r2, #3
	cmp	r2, #0
	movlt	r2, r10
	ldr	r10, [r1, #-1432]
	bic	r2, r2, #3
	add	r2, r10, r2
	str	r2, [r8, #4]
	ldr	r2, [r1, #-1484]
	mla	r3, r7, r3, r2
	ldr	r2, [r9, r0]
	str	r2, [r3, #8]
	ldr	r2, [r8, #4]
	str	r2, [r3, #12]
	mov	r3, ip
	b	.L2662
.L2667:
	ldr	r4, .L2669+16
	mov	r1, r0
	mov	ip, #12
	mov	lr, #0
.L2664:
	ldr	r3, [r0, #-1476]
	cmp	r2, r3
	bcs	.L2668
	mul	r5, ip, r2
	ldr	r7, [r1, #-1444]
	add	r3, r7, r5
	str	lr, [r3, #8]
	ldrh	r3, [r4]
	mul	r3, r3, r2
	add	r6, r3, #3
	cmp	r3, #0
	movlt	r3, r6
	ldr	r6, [r1, #-1460]
	bic	r3, r3, #3
	add	r3, r6, r3
	str	r3, [r7, r5]
	ldr	r3, .L2669+12
	ldr	r6, [r1, #-1444]
	ldrh	r3, [r3]
	add	r5, r6, r5
	mul	r3, r3, r2
	add	r2, r2, #1
	uxth	r2, r2
	add	r6, r3, #3
	cmp	r3, #0
	movlt	r3, r6
	ldr	r6, [r1, #-1432]
	bic	r3, r3, #3
	add	r3, r6, r3
	str	r3, [r5, #4]
	b	.L2664
.L2668:
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, pc}
.L2670:
	.align	2
.L2669:
	.word	.LANCHOR2-1732
	.word	.LANCHOR4
	.word	.LANCHOR2
	.word	.LANCHOR2-1652
	.word	.LANCHOR2-1654
	.fnend
	.size	FtlGcBufInit, .-FtlGcBufInit
	.align	2
	.global	FtlVariablesInit
	.type	FtlVariablesInit, %function
FtlVariablesInit:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	stmfd	sp!, {r3, r4, r5, r6, r7, lr}
	.save {r3, r4, r5, r6, r7, lr}
	movw	r2, #1752
	ldr	r6, .L2673
	mvn	r3, #0
	ldr	r5, .L2673+4
	mov	r4, #0
	mov	r1, r4
	strh	r3, [r6, r2]	@ movhi
	sub	r7, r5, #1712
	str	r3, [r6, #1764]
	sub	r3, r5, #1280
	ldr	r0, [r5, #-1388]
	strh	r4, [r3, #-4]	@ movhi
	sub	r3, r5, #1632
	str	r4, [r6, #1748]
	ldrh	r2, [r3, #-12]
	str	r4, [r6, #1756]
	str	r4, [r6, #1760]
	mov	r2, r2, asl #1
	str	r4, [r5, #-1800]
	bl	ftl_memset
	ldrh	r2, [r7, #-10]
	mov	r1, r4
	ldr	r0, [r5, #-1412]
	mov	r2, r2, asl #1
	bl	ftl_memset
	ldrh	r2, [r7, #-10]
	mov	r1, r4
	ldr	r0, [r5, #-1420]
	mov	r2, r2, asl #1
	bl	ftl_memset
	mov	r1, r4
	add	r0, r5, #820
	mov	r2, #48
	bl	ftl_memset
	mov	r1, r4
	mov	r2, #512
	add	r0, r6, #1216
	bl	ftl_memset
	bl	FtlGcBufInit
	bl	FtlL2PDataInit
	mov	r0, r4
	ldmfd	sp!, {r3, r4, r5, r6, r7, pc}
.L2674:
	.align	2
.L2673:
	.word	.LANCHOR4
	.word	.LANCHOR2
	.fnend
	.size	FtlVariablesInit, .-FtlVariablesInit
	.align	2
	.global	FtlGcBufFree
	.type	FtlGcBufFree, %function
FtlGcBufFree:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	ldr	r3, .L2683
	stmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, lr}
	.save {r4, r5, r6, r7, r8, r9, r10, lr}
	mov	lr, #0
	ldr	r6, [r3, #-1476]
	mov	r5, #36
	ldr	r4, [r3, #-1444]
	mov	r7, lr
	mov	r8, #12
.L2676:
	uxth	ip, lr
	cmp	ip, r1
	ldmcsfd	sp!, {r4, r5, r6, r7, r8, r9, r10, pc}
	mla	ip, r5, ip, r0
	mov	r2, #0
.L2677:
	uxth	r3, r2
	cmp	r3, r6
	bcs	.L2678
	mul	r3, r8, r3
	add	r2, r2, #1
	add	r9, r4, r3
	ldr	r10, [r4, r3]
	ldr	r3, [ip, #8]
	cmp	r10, r3
	bne	.L2677
	str	r7, [r9, #8]
.L2678:
	add	lr, lr, #1
	b	.L2676
.L2684:
	.align	2
.L2683:
	.word	.LANCHOR2
	.fnend
	.size	FtlGcBufFree, .-FtlGcBufFree
	.align	2
	.global	FtlGcBufAlloc
	.type	FtlGcBufAlloc, %function
FtlGcBufAlloc:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	ldr	r3, .L2694
	mov	ip, #0
	stmfd	sp!, {r4, r5, r6, r7, r8, r9, lr}
	.save {r4, r5, r6, r7, r8, r9, lr}
	mov	r6, #12
	ldr	r4, [r3, #-1476]
	mov	r7, #1
	ldr	r5, [r3, #-1444]
	mov	r8, #36
.L2686:
	uxth	r2, ip
	cmp	r2, r1
	bcs	.L2693
	mov	lr, #0
.L2687:
	uxth	r3, lr
	cmp	r3, r4
	bcs	.L2688
	mla	r3, r6, r3, r5
	add	lr, lr, #1
	ldr	r9, [r3, #8]
	cmp	r9, #0
	bne	.L2687
	mla	r2, r8, r2, r0
	ldr	lr, [r3]
	str	r7, [r3, #8]
	str	lr, [r2, #8]
	ldr	r3, [r3, #4]
	str	r3, [r2, #12]
.L2688:
	add	ip, ip, #1
	b	.L2686
.L2693:
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, pc}
.L2695:
	.align	2
.L2694:
	.word	.LANCHOR2
	.fnend
	.size	FtlGcBufAlloc, .-FtlGcBufAlloc
	.align	2
	.global	IsBlkInGcList
	.type	IsBlkInGcList, %function
IsBlkInGcList:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	@ link register save eliminated.
	ldr	r1, .L2702
	movw	r2, #1768
	ldr	r3, .L2702+4
	ldrh	r2, [r1, r2]
	ldr	r3, [r3, #-1508]
	add	r2, r3, r2, asl #1
.L2697:
	cmp	r3, r2
	beq	.L2701
	ldrh	r1, [r3], #2
	cmp	r1, r0
	bne	.L2697
	mov	r0, #1
	bx	lr
.L2701:
	mov	r0, #0
	bx	lr
.L2703:
	.align	2
.L2702:
	.word	.LANCHOR4
	.word	.LANCHOR2
	.fnend
	.size	IsBlkInGcList, .-IsBlkInGcList
	.align	2
	.global	FtlGcUpdatePage
	.type	FtlGcUpdatePage, %function
FtlGcUpdatePage:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	stmfd	sp!, {r3, r4, r5, r6, r7, r8, r9, lr}
	.save {r3, r4, r5, r6, r7, r8, r9, lr}
	mov	r4, r0
	ubfx	r0, r0, #10, #16
	mov	r5, r1
	mov	r6, r2
	bl	P2V_block_in_plane
	ldr	lr, .L2712
	movw	r2, #1768
	ldr	r3, .L2712+4
	ldr	r1, [lr, #-1508]
	ldrh	r7, [r3, r2]
	mov	r2, #0
	sub	r8, r1, #2
.L2705:
	uxth	ip, r2
	cmp	ip, r7
	bcs	.L2709
	ldrh	r9, [r8, #2]!
	add	r2, r2, #1
	cmp	r9, r0
	bne	.L2705
.L2709:
	cmp	ip, r7
	bne	.L2707
	mov	ip, ip, asl #1
	movw	r2, #1768
	strh	r0, [r1, ip]	@ movhi
	ldrh	r0, [r3, r2]
	add	r0, r0, #1
	strh	r0, [r3, r2]	@ movhi
.L2707:
	movw	r0, #1770
	mov	ip, #12
	ldrh	r2, [r3, r0]
	mul	ip, ip, r2
	ldr	r2, [lr, #-1504]
	add	r1, r2, ip
	stmib	r1, {r5, r6}
	str	r4, [r2, ip]
	ldrh	r2, [r3, r0]
	add	r2, r2, #1
	strh	r2, [r3, r0]	@ movhi
	ldmfd	sp!, {r3, r4, r5, r6, r7, r8, r9, pc}
.L2713:
	.align	2
.L2712:
	.word	.LANCHOR2
	.word	.LANCHOR4
	.fnend
	.size	FtlGcUpdatePage, .-FtlGcUpdatePage
	.align	2
	.global	FtlGcPageVarInit
	.type	FtlGcPageVarInit, %function
FtlGcPageVarInit:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	stmfd	sp!, {r3, r4, r5, lr}
	.save {r3, r4, r5, lr}
	movw	r1, #1768
	ldr	r3, .L2716
	mov	r2, #0
	ldr	r4, .L2716+4
	sub	r5, r4, #1648
	strh	r2, [r3, r1]	@ movhi
	movw	r1, #1770
	ldr	r0, [r4, #-1508]
	strh	r2, [r3, r1]	@ movhi
	mov	r1, #255
	ldrh	r2, [r5, #-12]
	mov	r2, r2, asl #1
	bl	ftl_memset
	ldrh	r3, [r5, #-12]
	mov	r2, #12
	ldr	r0, [r4, #-1504]
	mov	r1, #255
	mul	r2, r2, r3
	bl	ftl_memset
	ldmfd	sp!, {r3, r4, r5, lr}
	b	FtlGcBufInit
.L2717:
	.align	2
.L2716:
	.word	.LANCHOR4
	.word	.LANCHOR2
	.fnend
	.size	FtlGcPageVarInit, .-FtlGcPageVarInit
	.align	2
	.global	FtlGcScanTempBlk
	.type	FtlGcScanTempBlk, %function
FtlGcScanTempBlk:
	.fnstart
	@ args = 0, pretend = 0, frame = 64
	@ frame_needed = 0, uses_anonymous_args = 0
	ldr	r2, .L2771
	movw	r3, #3352
	stmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.save {r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.pad #68
	sub	sp, sp, #68
	ldrh	r5, [r2, r3]
	movw	r3, #65535
	mov	r4, r0
	str	r1, [sp, #8]
	cmp	r5, r3
	beq	.L2752
	cmp	r5, #0
	bne	.L2719
	b	.L2720
.L2752:
	mov	r5, #0
.L2719:
	ldr	r3, .L2771+4
	ldr	r2, [sp, #8]
	ldrh	r3, [r3]
	cmp	r2, r3
	bne	.L2721
.L2720:
	bl	FtlGcPageVarInit
.L2721:
	ldr	r6, .L2771+8
	mov	r2, #0
	mvn	r3, #0
	stmia	sp, {r2, r3}
.L2722:
	ldrh	r1, [r4]
	movw	r3, #65535
	mov	r2, #0
	strb	r2, [r4, #8]
	cmp	r1, r3
	beq	.L2723
.L2724:
.L2749:
	ldr	r3, .L2771+12
	mov	r7, #0
	ldr	r0, [r6, #-1432]
	add	r1, r4, #14
	mov	r8, r7
	movw	lr, #65535
	ldrh	r3, [r3]
	mov	r9, #36
	str	r3, [sp, #12]
	ldr	r3, [r6, #-1500]
	str	r3, [sp, #16]
	ldr	r3, [r6, #-1460]
	str	r3, [sp, #20]
	ldr	r3, .L2771+16
	ldrh	fp, [r3]
	ldrh	ip, [r3, #2]
.L2725:
	ldr	r2, [sp, #12]
	uxth	r3, r7
	cmp	r3, r2
	bcs	.L2769
	ldrh	r3, [r1, #2]!
	cmp	r3, lr
	beq	.L2726
	ldr	r2, [sp, #16]
	orr	r3, r5, r3, asl #10
	mla	r2, r9, r8, r2
	str	r3, [r2, #4]
	mul	r3, fp, r8
	add	r10, r3, #3
	cmp	r3, #0
	movlt	r3, r10
	ldr	r10, [sp, #20]
	bic	r3, r3, #3
	add	r3, r10, r3
	str	r3, [r2, #8]
	mul	r3, ip, r8
	add	r8, r8, #1
	uxth	r8, r8
	add	r10, r3, #3
	cmp	r3, #0
	movlt	r3, r10
	bic	r3, r3, #3
	add	r3, r0, r3
	str	r3, [r2, #12]
.L2726:
	add	r7, r7, #1
	b	.L2725
.L2769:
	ldr	r0, [r6, #-1500]
	mov	r1, r8
	mov	r2, #0
	mov	r10, #0
	bl	FlashReadPages
.L2728:
	uxth	r3, r10
	cmp	r3, r8
	bcs	.L2770
	ldr	r3, .L2771+8
	mov	r9, #36
	mul	r9, r9, r10
	ldr	r7, [r3, #-1500]
	add	r3, r7, r9
	str	r3, [sp, #12]
	ldr	fp, [r3, #4]
	ubfx	r0, fp, #10, #16
	bl	P2V_plane
	ldr	r7, [r7, r9]
	ldr	ip, .L2771+8
	cmp	r7, #0
	ldr	r3, [sp, #12]
	mov	r2, r0
	ldr	r3, [r3, #12]
	bne	.L2729
	ldrh	r0, [r3]
	movw	r1, #65535
	cmp	r0, r1
	bne	.L2730
.L2733:
	ldrb	r1, [ip, #-2744]	@ zero_extendqisi2
	cmp	r1, #0
	beq	.L2764
	ldr	r3, .L2771+20
	mov	r1, #1
	str	r1, [r3, #1760]
	b	.L2723
.L2730:
	ldr	r0, [r3, #8]
	ldr	r1, [r6, #-1280]
	cmp	r0, r1
	bls	.L2765
	b	.L2733
.L2764:
	ldrh	r3, [r4]
	ldr	r2, [r6, #-1404]
	mov	r3, r3, asl #1
	b	.L2768
.L2765:
	ldr	r2, .L2771+24
	ldrb	r2, [r2]	@ zero_extendqisi2
	cmp	r2, #0
	beq	.L2737
	add	r1, sp, #24
	mov	r2, r7
	str	r3, [sp, #12]
	bl	log2phys
	ldr	r3, [sp, #12]
	ldr	r1, [sp, #24]
	ldr	r2, [r3, #12]
	cmn	r1, #1
	rsb	r0, r2, r1
	clz	r0, r0
	mov	r0, r0, lsr #5
	moveq	r0, #0
	cmp	r0, #0
	beq	.L2737
	str	r2, [sp, #32]
	mov	r1, #1
	ldr	r2, [r6, #-1448]
	add	r0, sp, #28
	str	r2, [sp, #36]
	ldr	r2, [r6, #-1436]
	str	r2, [sp, #40]
	mov	r2, r7
	bl	FlashReadPages
	ldr	r2, .L2771+28
	ldr	r1, [r6, #-1500]
	ldrh	r2, [r2]
	add	r9, r1, r9
	mov	r2, r2, asl #7
	ldr	r1, [sp, #36]
	ldr	r3, [sp, #12]
	b	.L2739
.L2740:
	add	r7, r7, #1
.L2739:
	cmp	r7, r2
	beq	.L2737
	ldr	r0, [r9, #8]
	ldr	ip, [r0, r7, asl #2]
	ldr	r0, [r1, r7, asl #2]
	cmp	ip, r0
	beq	.L2740
	ldrh	r1, [r4]
	ldr	r2, [sp, #32]
	ldr	r0, .L2771+32
	bl	printk
	ldrh	r3, [r4]
	ldr	r2, [r6, #-1404]
	mov	r1, #0
	mov	r3, r3, asl #1
.L2768:
	strh	r1, [r2, r3]	@ movhi
	ldrh	r0, [r4]
	bl	INSERT_FREE_LIST
	ldr	r2, .L2771+36
	mvn	r3, #0
	strh	r3, [r4]	@ movhi
	strh	r3, [r2]	@ movhi
	b	.L2767
.L2737:
	ldr	r0, [r3, #12]
	mov	r1, fp
	ldr	r2, [r3, #8]
	add	r10, r10, #1
	bl	FtlGcUpdatePage
	b	.L2728
.L2729:
	ldr	r0, .L2771+40
	mov	r2, fp
	ldrh	r1, [r4]
	str	ip, [sp, #12]
	bl	printk
	ldr	r3, .L2771+8
	ldrh	r5, [r4]
	ldr	r3, [r3, #-1800]
	cmp	r3, #0
	ldr	ip, [sp, #12]
	bne	.L2743
	ldr	r3, .L2771+8
	ldrb	r3, [r3, #-2744]	@ zero_extendqisi2
	cmp	r3, #0
	beq	.L2744
.L2743:
	ldr	r2, [ip, #-1412]
	mov	r3, r5, asl #1
	ldrh	r3, [r2, r3]
	cmp	r3, #159
	bls	.L2745
.L2744:
	ldr	r3, [ip, #-1500]
	ldr	r3, [r3, r9]
	cmn	r3, #1
	bne	.L2746
.L2745:
	ldr	r3, [ip, #-1500]
	add	r9, r3, r9
	ldr	r3, [r9, #4]
	str	r3, [sp, #4]
.L2746:
	ldr	r3, .L2771+8
	mov	r5, r5, asl #1
	mov	r2, #0
	ldr	r3, [r3, #-1404]
	strh	r2, [r3, r5]	@ movhi
	ldrh	r0, [r4]
	bl	INSERT_FREE_LIST
	mvn	r3, #0
	strh	r3, [r4]	@ movhi
.L2767:
	bl	FtlGcPageVarInit
	mov	r5, #0
	b	.L2722
.L2770:
	ldr	r3, [sp]
	add	r5, r5, #1
	ldr	r2, [sp, #8]
	add	r3, r3, #1
	uxth	r5, r5
	cmp	r3, r2
	str	r3, [sp]
	ldr	r2, .L2771+4
	bcs	.L2748
.L2750:
	ldrh	r3, [r2]
	cmp	r3, r5
	bhi	.L2749
	mov	r2, #0
	b	.L2723
.L2748:
	ldr	r1, .L2771+44
	movw	r0, #65535
	ldrh	r3, [r1]
	cmp	r3, r0
	beq	.L2750
	ldr	r0, [sp]
	add	r3, r3, r0
	strh	r3, [r1]	@ movhi
	ldrh	r3, [r2]
	cmp	r3, r5
	bls	.L2750
	b	.L2751
.L2723:
	ldr	r1, .L2771
	movw	r3, #3352
	mvn	r0, #0
	strh	r5, [r4, #2]	@ movhi
	strb	r2, [r4, #6]
	strh	r0, [r1, r3]	@ movhi
	mov	r0, r4
	mov	r1, r5
	bl	ftl_sb_update_avl_pages
.L2751:
	ldr	r0, [sp, #4]
	add	sp, sp, #68
	@ sp needed
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L2772:
	.align	2
.L2771:
	.word	.LANCHOR1
	.word	.LANCHOR2-1664
	.word	.LANCHOR2
	.word	.LANCHOR2-1732
	.word	.LANCHOR2-1654
	.word	.LANCHOR4
	.word	.LANCHOR0
	.word	.LANCHOR2-1658
	.word	.LC143
	.word	.LANCHOR4+1168
	.word	.LC144
	.word	.LANCHOR1+3352
	.fnend
	.size	FtlGcScanTempBlk, .-FtlGcScanTempBlk
	.align	2
	.global	FtlGcRefreshOpenBlock
	.type	FtlGcRefreshOpenBlock, %function
FtlGcRefreshOpenBlock:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	stmfd	sp!, {r4, r5, r6, lr}
	.save {r4, r5, r6, lr}
	mov	r5, r0
	ldr	r6, .L2781
	ldrh	r3, [r6]
	cmp	r3, r0
	beq	.L2775
	add	r4, r6, #16
	ldrh	r3, [r4, #-14]
	cmp	r3, r0
	beq	.L2775
	ldrh	r3, [r4, #-12]
	cmp	r3, r0
	beq	.L2775
	ldrh	r3, [r4, #-10]
	cmp	r3, r0
	beq	.L2775
	ldr	r0, .L2781+4
	mov	r1, r5
	bl	printk
	ldrh	r2, [r6]
	movw	r3, #65535
	cmp	r2, r3
	streqh	r5, [r6]	@ movhi
	beq	.L2775
	ldrh	r2, [r4, #-14]
	cmp	r2, r3
	streqh	r5, [r4, #-14]	@ movhi
	beq	.L2775
	ldrh	r2, [r4, #-12]
	cmp	r2, r3
	streqh	r5, [r4, #-12]	@ movhi
	beq	.L2775
	ldrh	r2, [r4, #-10]
	cmp	r2, r3
	streqh	r5, [r4, #-10]	@ movhi
.L2775:
	mov	r0, #0
	ldmfd	sp!, {r4, r5, r6, pc}
.L2782:
	.align	2
.L2781:
	.word	.LANCHOR2-1536
	.word	.LC145
	.fnend
	.size	FtlGcRefreshOpenBlock, .-FtlGcRefreshOpenBlock
	.align	2
	.global	FtlGcRefreshBlock
	.type	FtlGcRefreshBlock, %function
FtlGcRefreshBlock:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	stmfd	sp!, {r4, r5, r6, lr}
	.save {r4, r5, r6, lr}
	mov	r5, r0
	ldr	r6, .L2794
	ldrh	r3, [r6]
	cmp	r3, r0
	beq	.L2791
	add	r4, r6, #16
	ldrh	r3, [r4, #-14]
	cmp	r3, r0
	beq	.L2791
	ldrh	r3, [r4, #-12]
	cmp	r3, r0
	beq	.L2791
	ldrh	r3, [r4, #-10]
	cmp	r3, r0
	beq	.L2791
	ldr	r0, .L2794+4
	mov	r1, r5
	bl	printk
	ldrh	r2, [r6]
	movw	r3, #65535
	cmp	r2, r3
	streqh	r5, [r6]	@ movhi
	beq	.L2791
	ldrh	r2, [r4, #-14]
	cmp	r2, r3
	streqh	r5, [r4, #-14]	@ movhi
	beq	.L2791
	ldrh	r2, [r4, #-12]
	cmp	r2, r3
	streqh	r5, [r4, #-12]	@ movhi
	beq	.L2791
	ldrh	r2, [r4, #-10]
	cmp	r2, r3
	bne	.L2792
	strh	r5, [r4, #-10]	@ movhi
.L2791:
	mov	r0, #0
	ldmfd	sp!, {r4, r5, r6, pc}
.L2792:
	mvn	r0, #0
	ldmfd	sp!, {r4, r5, r6, pc}
.L2795:
	.align	2
.L2794:
	.word	.LANCHOR2-1536
	.word	.LC145
	.fnend
	.size	FtlGcRefreshBlock, .-FtlGcRefreshBlock
	.align	2
	.global	FtlGcMarkBadPhyBlk
	.type	FtlGcMarkBadPhyBlk, %function
FtlGcMarkBadPhyBlk:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	stmfd	sp!, {r3, r4, r5, r6, r7, lr}
	.save {r3, r4, r5, r6, r7, lr}
	mov	r5, r0
	bl	P2V_block_in_plane
	ldr	r6, .L2806
	mov	r2, r5
	sub	r7, r6, #1520
	ldrh	r1, [r7, #-2]
	mov	r4, r0
	ldr	r0, .L2806+4
	bl	printk
	mov	r0, r4
	bl	FtlGcRefreshBlock
	ldr	r3, [r6, #-1800]
	cmp	r3, #0
	beq	.L2797
	ldr	r2, [r6, #-1412]
	mov	r4, r4, asl #1
	ldrh	r3, [r2, r4]
	cmp	r3, #39
	subhi	r3, r3, #40
	strhih	r3, [r2, r4]	@ movhi
.L2797:
	ldrh	r3, [r7, #-2]
	mov	r2, #0
	ldr	r0, .L2806+8
.L2798:
	uxth	r1, r2
	cmp	r1, r3
	bcs	.L2805
	add	r2, r2, #1
	add	r1, r0, r2, asl #1
	ldrh	r1, [r1, #-2]
	cmp	r1, r5
	bne	.L2798
	b	.L2799
.L2805:
	cmp	r3, #15
	addls	r2, r3, #1
	strlsh	r2, [r7, #-2]	@ movhi
	ldrls	r2, .L2806+8
	movls	r3, r3, asl #1
	strlsh	r5, [r2, r3]	@ movhi
.L2799:
	mov	r0, #0
	ldmfd	sp!, {r3, r4, r5, r6, r7, pc}
.L2807:
	.align	2
.L2806:
	.word	.LANCHOR2
	.word	.LC146
	.word	.LANCHOR4+1772
	.fnend
	.size	FtlGcMarkBadPhyBlk, .-FtlGcMarkBadPhyBlk
	.align	2
	.global	FtlGcReFreshBadBlk
	.type	FtlGcReFreshBadBlk, %function
FtlGcReFreshBadBlk:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	stmfd	sp!, {r4, lr}
	.save {r4, lr}
	ldr	r4, .L2815
	ldrh	r3, [r4, #-2]
	cmp	r3, #0
	beq	.L2809
	ldrh	r1, [r4, #-16]
	movw	r2, #65535
	cmp	r1, r2
	bne	.L2809
	add	r4, r4, #16
	ldrh	r2, [r4, #-14]
	cmp	r2, r3
	ldr	r2, .L2815+4
	movcs	r3, #0
	strcsh	r3, [r4, #-14]	@ movhi
	ldrh	r3, [r4, #-14]
	mov	r3, r3, asl #1
	ldrh	r0, [r2, r3]
	bl	P2V_block_in_plane
	bl	FtlGcRefreshBlock
	ldrh	r3, [r4, #-14]
	add	r3, r3, #1
	strh	r3, [r4, #-14]	@ movhi
.L2809:
	mov	r0, #0
	ldmfd	sp!, {r4, pc}
.L2816:
	.align	2
.L2815:
	.word	.LANCHOR2-1520
	.word	.LANCHOR4+1772
	.fnend
	.size	FtlGcReFreshBadBlk, .-FtlGcReFreshBadBlk
	.align	2
	.global	FtlGcFreeBadSuperBlk
	.type	FtlGcFreeBadSuperBlk, %function
FtlGcFreeBadSuperBlk:
	.fnstart
	@ args = 0, pretend = 0, frame = 8
	@ frame_needed = 0, uses_anonymous_args = 0
	stmfd	sp!, {r0, r1, r2, r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.save {r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.pad #12
	ldr	r5, .L2831
	ldrh	r3, [r5, #-2]
	cmp	r3, #0
	beq	.L2819
	sub	r5, r5, #2
	mov	r8, r0
	mov	r7, #0
	mov	r9, r5
.L2818:
	ldr	r3, .L2831+4
	uxth	r2, r7
	ldrh	r3, [r3]
	cmp	r3, r2
	bls	.L2828
	ldr	r3, .L2831+8
	mov	r1, r8
	mov	r10, #0
	add	r3, r3, r2
	ldrb	r0, [r3, #-1704]	@ zero_extendqisi2
	bl	V2P_block
	ldr	ip, .L2831+12
	mov	fp, r0
.L2820:
	ldrh	r3, [r5]
	uxth	r4, r10
	cmp	r3, r4
	bls	.L2829
	mov	r3, r4, asl #1
	add	r6, ip, r3
	ldrh	r3, [ip, r3]
	cmp	r3, fp
	bne	.L2821
	mov	r1, fp
	ldr	r0, .L2831+16
	str	ip, [sp, #4]
	bl	printk
	mov	r0, fp
	bl	FtlBbmMapBadBlock
	bl	FtlBbmTblFlush
	ldrh	r2, [r5]
	mov	r3, r6
	ldr	ip, [sp, #4]
.L2822:
	cmp	r4, r2
	ldrcch	r1, [r3, #2]
	addcc	r4, r4, #1
	uxthcc	r4, r4
	strcch	r1, [r3], #2	@ movhi
	bcc	.L2822
.L2830:
	sub	r2, r2, #1
	strh	r2, [r9]	@ movhi
.L2821:
	add	r10, r10, #1
	b	.L2820
.L2829:
	add	r7, r7, #1
	b	.L2818
.L2828:
	bl	FtlGcReFreshBadBlk
.L2819:
	mov	r0, #0
	add	sp, sp, #12
	@ sp needed
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L2832:
	.align	2
.L2831:
	.word	.LANCHOR2-1520
	.word	.LANCHOR2-1732
	.word	.LANCHOR2
	.word	.LANCHOR4+1772
	.word	.LC147
	.fnend
	.size	FtlGcFreeBadSuperBlk, .-FtlGcFreeBadSuperBlk
	.align	2
	.global	update_vpc_list
	.type	update_vpc_list, %function
update_vpc_list:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	ldr	r2, .L2843
	mov	r3, r0, asl #1
	stmfd	sp!, {r4, lr}
	.save {r4, lr}
	mov	r4, r0
	ldr	r1, [r2, #-1404]
	ldrh	r3, [r1, r3]
	cmp	r3, #0
	bne	.L2834
	ldr	r1, .L2843+4
	ldrh	r0, [r1]
	cmp	r0, r4
	mvneq	r3, #0
	streqh	r3, [r1]	@ movhi
	beq	.L2836
	add	r1, r2, #888
	ldrh	r1, [r1]
	cmp	r1, r4
	beq	.L2842
	add	r1, r2, #936
	ldrh	r1, [r1]
	cmp	r1, r4
	beq	.L2842
	add	r2, r2, #984
	ldrh	r2, [r2]
	cmp	r2, r4
	beq	.L2842
.L2836:
	mov	r1, r4
	ldr	r0, .L2843+8
	bl	List_remove_node
	ldr	r2, .L2843+12
	mov	r0, r4
	ldrh	r3, [r2]
	sub	r3, r3, #1
	strh	r3, [r2]	@ movhi
	bl	free_data_superblock
	mov	r0, r4
	bl	FtlGcFreeBadSuperBlk
	mov	r0, #1
	ldmfd	sp!, {r4, pc}
.L2834:
	bl	List_update_data_list
.L2842:
	mov	r0, #0
	ldmfd	sp!, {r4, pc}
.L2844:
	.align	2
.L2843:
	.word	.LANCHOR2
	.word	.LANCHOR4+1168
	.word	.LANCHOR2+868
	.word	.LANCHOR2+876
	.fnend
	.size	update_vpc_list, .-update_vpc_list
	.align	2
	.global	decrement_vpc_count
	.type	decrement_vpc_count, %function
decrement_vpc_count:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	movw	r3, #65535
	cmp	r0, r3
	stmfd	sp!, {r4, r5, r6, lr}
	.save {r4, r5, r6, lr}
	mov	r4, r0
	beq	.L2846
	ldr	r5, .L2856
	mov	r6, r0, asl #1
	ldr	r3, [r5, #-1404]
	ldrh	r2, [r3, r6]
	cmp	r2, #0
	subne	r2, r2, #1
	strneh	r2, [r3, r6]	@ movhi
	bne	.L2846
	mov	r1, r4
	ldr	r0, .L2856+4
	bl	printk
	ldr	r3, [r5, #-1404]
	mov	r2, #32
	add	r0, r5, #880
	mov	r1, r4
	strh	r2, [r3, r6]	@ movhi
	bl	test_node_in_list
	cmp	r0, #0
	beq	.L2848
	mov	r1, r4
	add	r0, r5, #880
	bl	List_remove_node
	add	r2, r5, #884
	mov	r0, r4
	ldrh	r3, [r2]
	sub	r3, r3, #1
	strh	r3, [r2]	@ movhi
	bl	INSERT_DATA_LIST
	ldr	r3, [r5, #-1404]
	ldr	r0, .L2856+8
	mov	r1, r4
	ldrh	r2, [r3, r6]
	bl	printk
.L2848:
	mov	r0, r4
	bl	FtlGcRefreshBlock
	b	.L2851
.L2846:
	ldr	r6, .L2856+12
	movw	r5, #1752
	movw	r3, #65535
	ldrh	r0, [r6, r5]
	cmp	r0, r3
	streqh	r4, [r6, r5]	@ movhi
	beq	.L2851
	cmp	r0, r4
	beq	.L2851
	bl	update_vpc_list
	strh	r4, [r6, r5]	@ movhi
	adds	r0, r0, #0
	movne	r0, #1
	ldmfd	sp!, {r4, r5, r6, pc}
.L2851:
	mov	r0, #0
	ldmfd	sp!, {r4, r5, r6, pc}
.L2857:
	.align	2
.L2856:
	.word	.LANCHOR2
	.word	.LC148
	.word	.LC149
	.word	.LANCHOR4
	.fnend
	.size	decrement_vpc_count, .-decrement_vpc_count
	.align	2
	.global	FtlRecoverySuperblock
	.type	FtlRecoverySuperblock, %function
FtlRecoverySuperblock:
	.fnstart
	@ args = 0, pretend = 0, frame = 64
	@ frame_needed = 0, uses_anonymous_args = 0
	ldrh	r3, [r0]
	movw	r2, #65535
	mov	r1, r0
	cmp	r3, r2
	beq	.L3002
	stmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.save {r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.pad #68
	sub	sp, sp, #68
	ldrh	r3, [r0, #2]
	str	r3, [sp, #8]
	ldrb	r3, [r0, #6]	@ zero_extendqisi2
	ldr	r0, [sp, #8]
	str	r3, [sp]
	ldr	r3, .L3017
	ldrh	r3, [r3]
	cmp	r3, r0
	mov	r3, #0
	streqh	r3, [r1, #4]	@ movhi
	streqb	r3, [r1, #6]
	ldrneh	r0, [r1, #16]
	beq	.L2999
.L2862:
	cmp	r0, r2
	add	r3, r3, #1
	uxtheq	r0, r3
	addeq	r0, r1, r0, asl #1
	ldreqh	r0, [r0, #16]
	beq	.L2862
.L3012:
	mov	r9, r1
	ldrb	r1, [r1, #8]	@ zero_extendqisi2
	cmp	r1, #1
	bne	.L2864
	bl	FtlGetLastWrittenPage
	cmn	r0, #1
	mov	r4, r0
	beq	.L2865
	ldr	r3, .L3017+4
	ldrb	r3, [r3, #-2744]	@ zero_extendqisi2
	cmp	r3, #0
	bne	.L3003
	ldr	r3, .L3017+8
	add	r3, r3, r0, asl #1
	ldrh	r6, [r3, #76]
	b	.L2936
.L2864:
	mov	r1, #0
	bl	FtlGetLastWrittenPage
	cmn	r0, #1
	mov	r4, r0
	beq	.L2865
.L3003:
	mov	r6, r4
.L2936:
	ldr	r3, .L3017+4
	movw	r8, #65535
	sub	r2, r3, #1728
	sub	r3, r3, #1648
	ldr	ip, [r3, #148]
	ldr	lr, [r3, #216]
	ldrh	r2, [r2, #-4]
	ldrh	r7, [r3, #-4]
	add	r3, r9, #14
	str	r3, [sp, #20]
	str	r2, [sp, #4]
	mov	r2, #0
	mov	r0, r3
	mov	r5, r2
	mov	r10, r2
	b	.L2867
.L2865:
	mov	r3, #0
	strh	r3, [r9, #2]	@ movhi
	strb	r3, [r9, #6]
	b	.L2999
.L2869:
	ldrh	r3, [r0, #2]!
	cmp	r3, r8
	beq	.L2868
	mov	r1, #36
	orr	r3, r6, r3, asl #10
	mla	r1, r1, r5, ip
	stmib	r1, {r3, r10}
	mul	r3, r7, r5
	add	r5, r5, #1
	uxth	r5, r5
	add	fp, r3, #3
	cmp	r3, #0
	movlt	r3, fp
	bic	r3, r3, #3
	add	r3, lr, r3
	str	r3, [r1, #12]
.L2868:
	add	r2, r2, #1
.L2867:
	ldr	r1, [sp, #4]
	uxth	r3, r2
	cmp	r3, r1
	bcc	.L2869
	ldrb	r3, [r9, #8]	@ zero_extendqisi2
	ldr	fp, .L3017+4
	cmp	r3, #1
	movne	r3, #0
	bne	.L3004
	ldrb	lr, [fp, #-2744]	@ zero_extendqisi2
	adds	r3, lr, #0
	movne	r3, #1
.L3004:
	str	r3, [sp, #24]
	mov	r1, r5
	ldr	r2, [sp, #24]
	mov	r10, #0
	ldr	r0, [fp, #-1500]
	bl	FlashReadPages
	ldr	r3, [fp, #-1608]
	ldr	r2, .L3017+4
	str	r3, [sp, #28]
	movw	r3, #65535
	str	r3, [sp, #16]
.L2871:
	uxth	r7, r10
	cmp	r7, r5
	bcs	.L2878
	mov	r1, #36
	ldr	r0, [r2, #-1500]
	mul	r1, r1, r10
	add	ip, r0, r1
	ldr	r1, [r0, r1]
	cmp	r1, #0
	bne	.L2872
	ldr	ip, [ip, #12]
	ldr	r8, [ip, #4]
	cmn	r8, #1
	beq	.L2873
	ldr	r1, [r2, #-1608]
	mov	r0, r8
	str	ip, [sp, #12]
	str	r2, [sp, #4]
	bl	ftl_cmp_data_ver
	ldr	r2, [sp, #4]
	cmp	r0, #0
	ldr	ip, [sp, #12]
	addne	r8, r8, #1
	strne	r8, [r2, #-1608]
.L2873:
	ldr	r1, [ip]
	cmn	r1, #1
	bne	.L2874
.L2878:
	cmp	r7, r5
	ldr	r5, .L3017+4
	bne	.L3000
	add	fp, r4, #1
	uxth	r3, fp
	str	r3, [sp, #4]
	ldr	r3, [r5, #-1500]
	ldr	r0, [r3, #4]
	b	.L3005
.L2872:
	ldr	r1, [ip, #4]
	ldr	r0, .L3017+12
	str	r2, [sp, #4]
	bl	printk
	uxth	r3, r6
	ldrh	r1, [r9]
	str	r3, [sp, #16]
	ldr	r2, [sp, #4]
	ldr	r3, .L3017+16
	strh	r1, [r3]	@ movhi
.L2874:
	add	r10, r10, #1
	b	.L2871
.L3000:
	uxth	r3, r4
	str	r3, [sp, #4]
	ldr	r3, [fp, #-1500]
	mov	r2, #36
	mla	r7, r2, r7, r3
	ldr	r0, [r7, #4]
.L3005:
	ubfx	r0, r0, #10, #16
	bl	P2V_plane
	ldrb	r3, [r9, #8]	@ zero_extendqisi2
	cmp	r3, #1
	str	r0, [sp, #12]
	bne	.L2880
	ldrb	r2, [r5, #-2744]	@ zero_extendqisi2
	cmp	r2, #0
	ldreq	r1, [sp, #4]
	ldreq	r2, .L3017+8
	addeq	r2, r2, r1, asl #1
	ldreqh	r2, [r2, #76]
	streq	r2, [sp, #4]
.L2880:
	ldr	r2, .L3017
	ldr	r1, [sp, #4]
	ldr	r0, [sp, #12]
	ldrh	r2, [r2]
	cmp	r2, r1
	ldr	r1, [sp, #8]
	ldreqh	r2, [sp, #4]
	streqh	r2, [r9, #2]	@ movhi
	moveq	r2, #0
	streqb	r2, [r9, #6]
	streqh	r2, [r9, #4]	@ movhi
	ldrh	r2, [sp]
	str	r2, [sp, #32]
	ldr	ip, [sp, #32]
	ldr	r2, [sp, #4]
	cmp	r2, r1
	cmpeq	r0, ip
	moveq	r0, r9
	moveq	r1, r2
	beq	.L3010
	clz	r3, r3
	ldr	r2, [sp, #28]
	ldr	r1, [sp, #16]
	mov	r3, r3, lsr #5
	sub	r2, r2, #1
	str	r2, [sp]
	movw	r2, #65535
	cmp	r1, r2
	orrne	r3, r3, #1
	cmp	r3, #0
	beq	.L2884
	ldr	r3, .L3017+20
	uxth	fp, r6
	ldr	r8, .L3017+4
	mvn	r7, #0
	mov	r6, r7
	ldr	r2, [r3, #1764]
	cmn	r2, #1
	ldreq	r2, [sp]
	streq	r2, [r3, #1764]
	ldr	r10, [r3, #1764]
	ldr	r3, [sp, #8]
	add	r3, r3, #7
	cmp	fp, r3
	subgt	r4, fp, #7
	ldrle	r4, [sp, #8]
	uxthgt	r4, r4
.L2887:
	cmp	r4, fp
	bhi	.L2900
	ldr	r3, .L3017+24
	mov	r0, #36
	ldr	lr, [r8, #-1500]
	ldr	r1, [sp, #20]
	ldrh	r3, [r3]
	str	r3, [sp, #16]
	mov	r3, #0
	mov	r5, r3
.L2901:
	ldr	ip, [sp, #16]
	uxth	r2, r3
	cmp	r2, ip
	bcs	.L3013
	ldrh	r2, [r1, #2]!
	movw	ip, #65535
	add	r3, r3, #1
	cmp	r2, ip
	orrne	r2, r4, r2, asl #10
	mlane	ip, r0, r5, lr
	addne	r5, r5, #1
	uxthne	r5, r5
	strne	r2, [ip, #4]
	b	.L2901
.L3013:
	mov	r1, r5
	ldr	r2, [sp, #24]
	ldr	r0, [r8, #-1500]
	bl	FlashReadPages
	ldr	r3, [r8, #-1500]
	mov	r2, #36
	ldrb	ip, [r8, #-2744]	@ zero_extendqisi2
	movw	r1, #65535
	mla	r5, r2, r5, r3
	ldr	r2, .L3017+28
	add	r2, r2, r4, asl #1
.L2890:
	cmp	r3, r5
	beq	.L3014
	ldr	r0, [r3]
	cmp	r0, #0
	bne	.L2891
	ldr	r0, [r3, #12]
	ldrh	lr, [r0]
	cmp	lr, r1
	beq	.L2892
	ldr	r0, [r0, #4]
	cmn	r0, #1
	beq	.L2892
	ldr	lr, .L3017+20
	cmn	r7, #1
	ldr	r6, [lr, #1764]
	str	r0, [lr, #1764]
	bne	.L2892
	ldrh	r0, [r2]
	cmp	r0, r1
	bne	.L2893
	cmp	ip, #0
	beq	.L2892
.L2893:
	ldr	r0, [sp]
	cmp	r6, r0
	mvneq	r7, #0
	movne	r7, r6
	b	.L2892
.L2891:
	ldrh	r1, [r9]
	movw	r2, #1806
	ldr	r3, .L3017+20
	strh	r1, [r3, r2]	@ movhi
	ldrb	r2, [r9, #8]	@ zero_extendqisi2
	cmp	r2, #0
	bne	.L2884
	ldr	r2, .L3017+28
	mov	r4, r4, asl #1
	ldrh	r1, [r2, r4]
	movw	r2, #65535
	cmp	r1, r2
	bne	.L2895
	cmn	r7, #1
	strne	r7, [r3, #1764]
	bne	.L2884
	ldr	r2, [sp]
	cmp	r10, r2
	strne	r10, [r3, #1764]
	ldreq	r2, [r3, #1764]
	bne	.L2884
	b	.L3011
.L2895:
	ldr	r2, [sp]
	cmp	r6, r2
	beq	.L2898
	cmn	r6, #1
	strne	r6, [r3, #1764]
	b	.L2884
.L2898:
	ldr	r2, [r3, #1764]
	ldr	r1, [sp]
	cmp	r2, r1
	beq	.L2884
.L3011:
	sub	r2, r2, #1
	b	.L3006
.L2892:
	add	r3, r3, #36
	b	.L2890
.L3014:
	add	r4, r4, #1
	uxth	r4, r4
	b	.L2887
.L2900:
	ldr	r3, .L3017+20
	mvn	r2, #0
.L3006:
	str	r2, [r3, #1764]
.L2884:
	ldr	fp, .L3017+32
	mov	r3, #1
	ldr	r10, [sp, #8]
	strh	r3, [fp]	@ movhi
	sub	fp, fp, #1808
.L2902:
	ldr	r3, .L3017+4
	movw	lr, #65535
	ldr	r2, .L3017+24
	mov	r1, #36
	ldr	r5, [sp, #20]
	mov	r4, #0
	ldr	r7, [r3, #-1500]
	ldrh	r6, [r2]
	ldrb	r8, [r3, #-2744]	@ zero_extendqisi2
	str	r4, [sp, #16]
.L2903:
	uxth	r3, r4
	cmp	r3, r6
	bcs	.L3015
	ldrh	r3, [r5, #2]!
	cmp	r3, lr
	beq	.L2904
	ldr	r2, [sp, #16]
	orr	r3, r10, r3, asl #10
	mla	r2, r1, r2, r7
	str	r3, [r2, #4]
	ldrb	r0, [r9, #8]	@ zero_extendqisi2
	cmp	r0, #1
	bne	.L2905
	cmp	r8, #0
	orrne	r3, r3, #-2147483648
	strne	r3, [r2, #4]
.L2905:
	ldr	r3, [sp, #16]
	add	ip, r3, #1
	uxth	r3, ip
	str	r3, [sp, #16]
.L2904:
	add	r4, r4, #1
	b	.L2903
.L3015:
	ldr	r4, .L3017+4
	ldr	r1, [sp, #16]
	ldr	r2, [sp, #24]
	ldr	r0, [r4, #-1500]
	bl	FlashReadPages
	mov	r3, #0
.L3009:
	str	r3, [sp, #28]
	ldr	r2, [sp, #16]
	ldrh	r3, [sp, #28]
	cmp	r3, r2
	bcs	.L3016
	ldr	r3, [sp, #28]
	mov	r5, #36
	ldr	r8, [r4, #-1500]
	mul	r5, r5, r3
	add	r7, r8, r5
	ldr	r6, [r7, #4]
	ubfx	r0, r6, #10, #16
	str	r6, [sp, #60]
	bl	P2V_plane
	ldr	r3, [sp, #8]
	cmp	r10, r3
	bcc	.L2908
	ldr	r3, [sp, #32]
	ldr	r2, [sp, #8]
	cmp	r0, r3
	movcs	r3, #0
	movcc	r3, #1
	cmp	r10, r2
	movne	r3, #0
	cmp	r3, #0
	bne	.L2908
	ldr	r3, [sp, #12]
	ldr	r2, [sp, #4]
	cmp	r0, r3
	cmpeq	r10, r2
	beq	.L2909
	ldr	r3, [r8, r5]
	cmn	r3, #1
	beq	.L2910
	ldr	r7, [r7, #12]
	movw	r3, #61589
	ldrh	r2, [r7]
	cmp	r2, r3
	ldrneh	r0, [r9]
	bne	.L3007
	ldr	r3, [r7, #4]
	cmn	r3, #1
	str	r3, [sp]
	beq	.L2912
	mov	r0, r3
	ldr	r1, [r4, #-1608]
	bl	ftl_cmp_data_ver
	cmp	r0, #0
	ldrne	r3, [sp]
	addne	r3, r3, #1
	strne	r3, [r4, #-1608]
.L2912:
	ldr	r6, [r7, #8]
	add	r1, sp, #56
	ldr	r3, [r7, #12]
	mov	r2, #0
	mov	r0, r6
	str	r3, [sp, #52]
	bl	log2phys
	ldr	r1, [fp, #1764]
	cmn	r1, #1
	beq	.L2913
	ldr	r0, [sp]
	bl	ftl_cmp_data_ver
	cmp	r0, #0
	beq	.L2913
	ldr	r3, [sp, #52]
	cmn	r3, #1
	beq	.L2914
	ldr	r0, [r4, #-1500]
	mov	r2, #0
	mov	r1, #1
	add	r0, r0, r5
	str	r3, [r0, #4]
	ldr	r8, [r0, #12]
	bl	FlashReadPages
	ldr	r2, [r4, #-1500]
	ldr	r3, [r8, #4]
	add	ip, r2, r5
	str	r3, [sp, #36]
	ldr	r3, [r2, r5]
	cmn	r3, #1
	bne	.L2915
	b	.L2916
.L2914:
	ldr	r3, [sp, #60]
	ldr	r2, [sp, #56]
	cmp	r2, r3
	bne	.L2908
	mov	r0, r6
	add	r1, sp, #52
	mov	r2, #1
	bl	log2phys
	b	.L2908
.L2915:
	ldr	r7, [r8, #8]
	cmp	r7, r6
	bne	.L2916
	ldr	r0, [fp, #1764]
	ldr	r1, [sp, #36]
	str	r2, [sp, #44]
	str	ip, [sp, #40]
	bl	ftl_cmp_data_ver
	cmp	r0, #0
	ldr	ip, [sp, #40]
	ldr	r2, [sp, #44]
	beq	.L2916
	ldr	r3, [sp, #56]
	ldr	r1, [sp, #60]
	cmp	r3, r1
	beq	.L2921
	ldr	r1, [sp, #52]
	cmp	r3, r1
	beq	.L2916
	cmn	r3, #1
	streq	r3, [r2, r5]
	beq	.L2920
	str	r3, [ip, #4]
	mov	r0, ip
	mov	r1, #1
	mov	r2, #0
	ldr	r8, [ip, #12]
	bl	FlashReadPages
.L2920:
	ldr	r3, [r4, #-1500]
	ldr	r3, [r3, r5]
	cmn	r3, #1
	beq	.L2921
	ldr	r5, [r8, #4]
	ldr	r0, [fp, #1764]
	mov	r1, r5
	bl	ftl_cmp_data_ver
	cmp	r0, #0
	beq	.L2921
	ldr	r0, [sp, #36]
	mov	r1, r5
	bl	ftl_cmp_data_ver
	cmp	r0, #0
	beq	.L2916
.L2921:
	mov	r0, r7
	ldr	r1, [sp, #52]
	bl	FtlReUsePrevPpa
.L2916:
	mvn	r3, #0
	str	r3, [sp, #52]
	b	.L2923
.L2913:
	ldr	r3, [sp, #60]
	ldr	r2, [sp, #56]
	cmp	r2, r3
	beq	.L2923
	ldr	r3, [sp, #52]
	cmn	r3, #1
	beq	.L2925
	ldr	r2, [r4, #-1716]
	ubfx	r3, r3, #10, #21
	cmp	r3, r2
	bcs	.L2908
.L2925:
	mov	r0, r6
	add	r1, sp, #60
	mov	r2, #1
	bl	log2phys
	ldr	r8, [sp, #56]
	cmn	r8, #1
	beq	.L2923
	ldr	r3, [sp, #52]
	cmp	r8, r3
	beq	.L2923
	ubfx	r0, r8, #10, #16
	ldr	r5, .L3017+36
	bl	P2V_block_in_plane
	ldrh	r3, [r5]
	sub	r5, r5, #888
	cmp	r3, r0
	beq	.L2927
	add	r3, r5, #936
	ldrh	r3, [r3]
	cmp	r3, r0
	beq	.L2927
	add	r3, r5, #984
	ldrh	r3, [r3]
	cmp	r3, r0
	bne	.L2923
.L2927:
	ldr	r0, [r5, #-1500]
	mov	r1, #1
	mov	r2, #0
	str	r8, [r0, #4]
	ldr	r7, [r0, #12]
	bl	FlashReadPages
	ldr	r3, [r5, #-1500]
	ldr	r1, [r7, #4]
	ldr	r3, [r3]
	cmn	r3, #1
	beq	.L2923
	ldr	r0, [sp]
	bl	ftl_cmp_data_ver
	cmp	r0, #0
	bne	.L2923
	mov	r0, r6
	add	r1, sp, #56
	mov	r2, #1
	bl	log2phys
.L2923:
	ldr	r0, [sp, #52]
	cmn	r0, #1
	beq	.L2908
	ubfx	r0, r0, #10, #16
	bl	P2V_block_in_plane
	ldr	r2, [r4, #-1404]
	mov	r3, r0, asl #1
	mov	r1, r0
	ldrh	r3, [r2, r3]
	cmp	r3, #0
	beq	.L2928
.L3007:
	bl	decrement_vpc_count
	b	.L2908
.L2928:
	ldr	r0, .L3017+40
	bl	printk
	b	.L2908
.L2910:
	ldrh	r2, [r9]
	mov	r1, r6
	ldr	r3, .L3017+16
	ldr	r0, .L3017+44
	strh	r2, [r3]	@ movhi
	ldr	r2, [sp]
	bl	printk
	ldr	r3, [fp, #1812]
	cmp	r3, #31
	addls	r2, fp, r3, asl #2
	addls	r3, r3, #1
	strls	r3, [fp, #1812]
	ldrls	r1, [sp, #60]
	strls	r1, [r2, #1816]
	ldrh	r0, [r9]
	bl	decrement_vpc_count
	ldr	r3, .L3017+20
	ldr	r3, [r3, #1764]
	cmn	r3, #1
	ldreq	r3, [sp]
	beq	.L3008
	ldr	r2, [sp]
	cmp	r3, r2
	bls	.L2908
	mov	r3, r2
.L3008:
	str	r3, [fp, #1764]
.L2908:
	ldr	r3, [sp, #28]
	add	r3, r3, #1
	b	.L3009
.L3016:
	ldrb	r3, [r9, #8]	@ zero_extendqisi2
	add	r10, r10, #1
	cmp	r3, #1
	uxth	r10, r10
	bne	.L2932
	ldr	r3, .L3017+4
	ldrb	r3, [r3, #-2744]	@ zero_extendqisi2
	cmp	r3, #0
	beq	.L2932
	ldr	r3, .L3017+48
	ldr	r2, [sp, #4]
	ldrh	r3, [r3]
	cmp	r2, r10
	cmpeq	r3, r10
	beq	.L2909
.L2932:
	ldr	r2, .L3017
	ldrh	r3, [r2]
	cmp	r10, r3
	bne	.L2902
	ldrh	r1, [r2, #-68]
	movw	r0, #65535
	mov	r3, #0
	strh	r10, [r9, #2]	@ movhi
	strh	r3, [r9, #4]	@ movhi
.L2933:
	uxth	r2, r3
	cmp	r2, r1
	bcs	.L2999
	ldr	lr, [sp, #20]
	add	r3, r3, #1
	ldrh	ip, [lr, #2]!
	cmp	ip, r0
	str	lr, [sp, #20]
	beq	.L2933
	strb	r2, [r9, #6]
	b	.L2999
.L2909:
	ldrb	r3, [sp, #12]	@ zero_extendqisi2
	mov	r0, r9
	ldr	r1, [sp, #4]
	strb	r3, [r9, #6]
	ldrh	r3, [sp, #4]
	strh	r3, [r9, #2]	@ movhi
.L3010:
	ldr	r2, [sp, #12]
	bl	ftl_sb_update_avl_pages
.L2999:
	mov	r0, #0
	add	sp, sp, #68
	@ sp needed
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L3002:
	mov	r0, #0
	bx	lr
.L3018:
	.align	2
.L3017:
	.word	.LANCHOR2-1664
	.word	.LANCHOR2
	.word	.LANCHOR0
	.word	.LC150
	.word	.LANCHOR4+1806
	.word	.LANCHOR4
	.word	.LANCHOR2-1732
	.word	.LANCHOR0+1100
	.word	.LANCHOR4+1808
	.word	.LANCHOR2+888
	.word	.LC151
	.word	.LC152
	.word	.LANCHOR2-1662
	.fnend
	.size	FtlRecoverySuperblock, .-FtlRecoverySuperblock
	.align	2
	.global	FtlSlcSuperblockCheck
	.type	FtlSlcSuperblockCheck, %function
FtlSlcSuperblockCheck:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	stmfd	sp!, {r3, r4, r5, r6, r7, lr}
	.save {r3, r4, r5, r6, r7, lr}
	ldrh	r3, [r0, #4]
	cmp	r3, #0
	ldmeqfd	sp!, {r3, r4, r5, r6, r7, pc}
	ldrh	r2, [r0]
	movw	r3, #65535
	cmp	r2, r3
	ldmeqfd	sp!, {r3, r4, r5, r6, r7, pc}
	ldrb	r3, [r0, #6]	@ zero_extendqisi2
	mov	r4, r0
	ldr	r5, .L3033
	ldr	r6, .L3033+4
	add	r3, r0, r3, asl #1
	ldr	r7, .L3033+8
	ldrh	r3, [r3, #16]
.L3023:
	movw	r1, #65535
	cmp	r3, r1
	bne	.L3032
.L3025:
	ldrb	r3, [r4, #6]	@ zero_extendqisi2
	ldrh	r2, [r7]
	add	r3, r3, #1
	uxtb	r3, r3
	strb	r3, [r4, #6]
	cmp	r2, r3
	ldreqh	r3, [r4, #2]
	addeq	r3, r3, #1
	streqh	r3, [r4, #2]	@ movhi
	moveq	r3, #0
	streqb	r3, [r4, #6]
	ldrb	r3, [r4, #6]	@ zero_extendqisi2
	add	r3, r4, r3, asl #1
	ldrh	r3, [r3, #16]
	b	.L3023
.L3032:
	ldrb	r2, [r4, #8]	@ zero_extendqisi2
	cmp	r2, #1
	bne	.L3026
	ldrb	r3, [r5, #-2744]	@ zero_extendqisi2
	cmp	r3, #0
	bne	.L3026
	ldrh	r3, [r4, #2]
	mov	r3, r3, asl #1
	ldrh	r3, [r6, r3]
	cmp	r3, r1
	bne	.L3026
	ldrh	r3, [r4, #4]
	ldrh	r0, [r4]
	sub	r3, r3, #1
	strh	r3, [r4, #4]	@ movhi
	bl	decrement_vpc_count
	ldrh	r2, [r4, #4]
	cmp	r2, #0
	bne	.L3025
	ldrh	r3, [r4, #2]
	add	r3, r3, #1
	b	.L3031
.L3026:
	ldr	r3, .L3033
	ldrb	r1, [r3, #-2744]	@ zero_extendqisi2
	cmp	r1, #0
	ldmeqfd	sp!, {r3, r4, r5, r6, r7, pc}
	cmp	r2, #1
	ldmnefd	sp!, {r3, r4, r5, r6, r7, pc}
	sub	r2, r3, #1648
	ldrh	r1, [r4, #2]
	ldrh	r2, [r2, #-14]
	cmp	r1, r2
	ldmccfd	sp!, {r3, r4, r5, r6, r7, pc}
	ldrh	r2, [r4]
	sub	r3, r3, #1664
	ldr	r0, [r3, #260]
	ldrh	ip, [r4, #4]
	mov	r2, r2, asl #1
	ldrh	r1, [r0, r2]
	rsb	r1, ip, r1
	strh	r1, [r0, r2]	@ movhi
	ldrh	r3, [r3]
	mov	r2, #0
	strh	r2, [r4, #4]	@ movhi
.L3031:
	strh	r3, [r4, #2]	@ movhi
	strb	r2, [r4, #6]
	ldmfd	sp!, {r3, r4, r5, r6, r7, pc}
.L3034:
	.align	2
.L3033:
	.word	.LANCHOR2
	.word	.LANCHOR0+1100
	.word	.LANCHOR2-1732
	.fnend
	.size	FtlSlcSuperblockCheck, .-FtlSlcSuperblockCheck
	.align	2
	.global	get_new_active_ppa
	.type	get_new_active_ppa, %function
get_new_active_ppa:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	stmfd	sp!, {r3, r4, r5, r6, r7, r8, r9, lr}
	.save {r3, r4, r5, r6, r7, r8, r9, lr}
	mov	r3, #0
	strb	r3, [r0, #10]
	mov	r4, r0
	ldrb	r3, [r0, #6]	@ zero_extendqisi2
	ldr	r7, .L3053
	ldr	r8, .L3053+4
	add	r3, r0, r3, asl #1
	ldr	r9, .L3053+8
	ldrh	r2, [r3, #16]
.L3036:
	movw	r1, #65535
	cmp	r2, r1
	ldr	r6, .L3053
	bne	.L3052
.L3037:
	ldrb	r3, [r4, #6]	@ zero_extendqisi2
	ldrh	r2, [r7]
	add	r3, r3, #1
	uxtb	r3, r3
	strb	r3, [r4, #6]
	cmp	r2, r3
	ldreqh	r3, [r4, #2]
	addeq	r3, r3, #1
	streqh	r3, [r4, #2]	@ movhi
	moveq	r3, #0
	streqb	r3, [r4, #6]
	ldrb	r3, [r4, #6]	@ zero_extendqisi2
	add	r3, r4, r3, asl #1
	ldrh	r2, [r3, #16]
	b	.L3036
.L3052:
	ldrb	r3, [r4, #8]	@ zero_extendqisi2
	ldrh	r5, [r4, #2]
	cmp	r3, #1
	ldrh	r3, [r4, #4]
	bne	.L3039
	ldrb	r0, [r8, #-2744]	@ zero_extendqisi2
	cmp	r0, #0
	bne	.L3039
	mov	r0, r5, asl #1
	ldrh	r0, [r9, r0]
	cmp	r0, r1
	bne	.L3039
	sub	r3, r3, #1
	ldrh	r0, [r4]
	strh	r3, [r4, #4]	@ movhi
	bl	decrement_vpc_count
	b	.L3037
.L3039:
	ldr	r7, .L3053+4
	orr	r5, r5, r2, asl #10
	sub	r3, r3, #1
	strh	r3, [r4, #4]	@ movhi
.L3040:
	ldrb	r3, [r4, #6]	@ zero_extendqisi2
	movw	r2, #65535
	ldrh	r0, [r6]
.L3042:
	add	r3, r3, #1
	uxtb	r3, r3
	cmp	r3, r0
	ldreqh	r3, [r4, #2]
	addeq	r3, r3, #1
	streqh	r3, [r4, #2]	@ movhi
	moveq	r3, #0
	add	r1, r4, r3, asl #1
	ldrh	r1, [r1, #16]
	cmp	r1, r2
	beq	.L3042
	strb	r3, [r4, #6]
	ldrb	r3, [r4, #8]	@ zero_extendqisi2
	cmp	r3, #1
	bne	.L3047
	ldrb	r3, [r7, #-2744]	@ zero_extendqisi2
	cmp	r3, #0
	bne	.L3044
	ldrh	r3, [r4, #2]
	ldr	r2, .L3053+8
	mov	r3, r3, asl #1
	ldrh	r2, [r2, r3]
	movw	r3, #65535
	cmp	r2, r3
	bne	.L3044
	ldrh	r3, [r4, #4]
	cmp	r3, #0
	beq	.L3044
	sub	r3, r3, #1
	ldrh	r0, [r4]
	strh	r3, [r4, #4]	@ movhi
	bl	decrement_vpc_count
	b	.L3040
.L3044:
	ldr	r3, .L3053+4
	ldrb	r2, [r3, #-2744]	@ zero_extendqisi2
	cmp	r2, #0
	beq	.L3047
	sub	r2, r3, #1648
	ldrh	r1, [r4, #2]
	ldrh	r2, [r2, #-14]
	cmp	r1, r2
	bcc	.L3047
	ldrh	r2, [r4]
	sub	r3, r3, #1664
	ldr	r0, [r3, #260]
	ldrh	ip, [r4, #4]
	mov	r2, r2, asl #1
	ldrh	r1, [r0, r2]
	rsb	r1, ip, r1
	strh	r1, [r0, r2]	@ movhi
	ldrh	r3, [r3]
	mov	r2, #0
	strh	r2, [r4, #4]	@ movhi
	strb	r2, [r4, #6]
	strh	r3, [r4, #2]	@ movhi
.L3047:
	mov	r0, r5
	ldmfd	sp!, {r3, r4, r5, r6, r7, r8, r9, pc}
.L3054:
	.align	2
.L3053:
	.word	.LANCHOR2-1732
	.word	.LANCHOR2
	.word	.LANCHOR0+1100
	.fnend
	.size	get_new_active_ppa, .-get_new_active_ppa
	.align	2
	.global	FtlWriteDumpData
	.type	FtlWriteDumpData, %function
FtlWriteDumpData:
	.fnstart
	@ args = 0, pretend = 0, frame = 40
	@ frame_needed = 0, uses_anonymous_args = 0
	stmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.save {r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.pad #44
	sub	sp, sp, #44
	ldr	r4, .L3074
	ldr	r3, [r4, #-1276]
	cmp	r3, #0
	bne	.L3055
	add	r7, r4, #888
	ldrh	r5, [r7, #4]
	cmp	r5, #0
	beq	.L3057
	ldrb	r3, [r4, #896]	@ zero_extendqisi2
	cmp	r3, #0
	bne	.L3057
	sub	r3, r4, #1664
	ldrb	r2, [r4, #895]	@ zero_extendqisi2
	ldrh	r3, [r3]
	mul	r3, r3, r2
	cmp	r5, r3
	beq	.L3057
	ldrb	r8, [r4, #898]	@ zero_extendqisi2
	sub	r3, r4, #1728
	ldr	r6, [r4, #-1280]
	cmp	r8, #0
	ldrh	r9, [r3, #-4]
	bne	.L3055
	sub	r6, r6, #1
	mov	r1, sp
	mov	r2, r8
	mov	r0, r6
	bl	log2phys
	ldr	r5, [r4, #-1440]
	ldr	r0, [r4, #-1472]
	ldr	r3, [sp]
	str	r6, [sp, #20]
	cmn	r3, #1
	str	r0, [sp, #12]
	str	r3, [sp, #8]
	str	r5, [sp, #16]
	str	r8, [r5, #4]
	beq	.L3059
	mov	r1, #1
	mov	r2, r8
	add	r0, sp, #4
	bl	FlashReadPages
	b	.L3060
.L3059:
	sub	r3, r4, #1648
	mov	r1, #255
	ldrh	r2, [r3, #-6]
	bl	ftl_memset
.L3060:
	ldr	r3, .L3074+4
	mov	r8, #0
	ldr	fp, .L3074
	mov	r9, r9, asl #2
	mov	r10, r8
	strh	r3, [r5]	@ movhi
.L3061:
	cmp	r8, r9
	beq	.L3062
	ldrh	r3, [r7, #4]
	ldr	r0, .L3074+8
	cmp	r3, #0
	beq	.L3062
	ldr	r3, [sp, #8]
	add	r8, r8, #1
	str	r6, [r5, #8]
	str	r3, [r5, #12]
	ldrh	r3, [r0]
	strh	r3, [r5, #2]	@ movhi
	bl	get_new_active_ppa
	ldr	r3, [fp, #-1608]
	mov	r2, #0
	mov	r1, #1
	str	r0, [sp, #8]
	add	r0, sp, #4
	str	r3, [r5, #4]
	add	r3, r3, #1
	cmn	r3, #1
	moveq	r3, r10
	str	r3, [fp, #-1608]
	mov	r3, r2
	bl	FlashProgPages
	ldrh	r0, [r7]
	bl	decrement_vpc_count
	b	.L3061
.L3062:
	mov	r3, #1
	b	.L3073
.L3057:
	mov	r3, #0
.L3073:
	strb	r3, [r4, #898]
.L3055:
	add	sp, sp, #44
	@ sp needed
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L3075:
	.align	2
.L3074:
	.word	.LANCHOR2
	.word	-3947
	.word	.LANCHOR2+888
	.fnend
	.size	FtlWriteDumpData, .-FtlWriteDumpData
	.align	2
	.global	l2p_flush
	.type	l2p_flush, %function
l2p_flush:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	stmfd	sp!, {r3, r4, r5, r6, r7, lr}
	.save {r3, r4, r5, r6, r7, lr}
	bl	FtlWriteDumpData
	mov	r4, #0
	ldr	r6, .L3082
	mov	r7, #12
	ldr	r5, .L3082+4
.L3077:
	ldrh	r3, [r5]
	uxth	r0, r4
	cmp	r3, r0
	bls	.L3081
	ldr	r3, [r6, #-1360]
	mla	r3, r7, r0, r3
	ldr	r3, [r3, #4]
	cmp	r3, #0
	bge	.L3078
	bl	flush_l2p_region
.L3078:
	add	r4, r4, #1
	b	.L3077
.L3081:
	mov	r0, #0
	ldmfd	sp!, {r3, r4, r5, r6, r7, pc}
.L3083:
	.align	2
.L3082:
	.word	.LANCHOR2
	.word	.LANCHOR2-1626
	.fnend
	.size	l2p_flush, .-l2p_flush
	.align	2
	.global	FtlSuperblockPowerLostFix
	.type	FtlSuperblockPowerLostFix, %function
FtlSuperblockPowerLostFix:
	.fnstart
	@ args = 0, pretend = 0, frame = 40
	@ frame_needed = 0, uses_anonymous_args = 0
	stmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, lr}
	.save {r4, r5, r6, r7, r8, r9, r10, lr}
	.pad #40
	sub	sp, sp, #40
	ldr	r5, .L3101
	ldr	r3, [r5, #-1276]
	cmp	r3, #0
	bne	.L3084
	ldrb	r6, [r5, #-2744]	@ zero_extendqisi2
	cmp	r6, #0
	beq	.L3100
	ldrb	r6, [r0, #8]	@ zero_extendqisi2
	cmp	r6, #1
	ldreqh	r10, [r0, #4]
	beq	.L3086
	mov	r6, r3
.L3100:
	mov	r10, #12
.L3086:
	ldr	r7, [r5, #-1440]
	mvn	r3, #0
	str	r3, [sp, #20]
	mov	r8, #0
	ldr	r3, [r5, #-1472]
	movw	r2, #61589
	str	r7, [sp, #16]
	mov	r4, r0
	ldr	r9, .L3101
	str	r3, [sp, #12]
	mvn	r3, #2
	str	r3, [r7, #8]
	mvn	r3, #1
	str	r3, [r7, #12]
	ldrh	r3, [r0]
	strh	r8, [r7]	@ movhi
	strh	r3, [r7, #2]	@ movhi
	ldr	r3, [r5, #-1472]
	str	r2, [r3]
	ldr	r2, .L3101+4
	ldr	r3, [r5, #-1472]
	str	r2, [r3, #4]
.L3087:
	subs	r10, r10, #1
	bcc	.L3090
	ldrh	r3, [r4, #4]
	cmp	r3, #0
	bne	.L3088
.L3090:
	ldrh	r3, [r4]
	ldr	r1, [r5, #-1404]
	ldrh	r0, [r4, #4]
	mov	r3, r3, asl #1
	ldrh	r2, [r1, r3]
	rsb	r2, r0, r2
	strh	r2, [r1, r3]	@ movhi
	ldr	r3, .L3101+8
	ldrh	r3, [r3]
	strh	r3, [r4, #2]	@ movhi
	mov	r3, #0
	strb	r3, [r4, #6]
	strh	r3, [r4, #4]	@ movhi
	b	.L3084
.L3088:
	mov	r0, r4
	bl	get_new_active_ppa
	cmn	r0, #1
	str	r0, [sp, #8]
	beq	.L3090
	ldr	r3, [r5, #-1608]
	mov	r1, #1
	mov	r2, r6
	add	r0, sp, #4
	str	r3, [r7, #4]
	add	r3, r3, #1
	cmn	r3, #1
	moveq	r3, r8
	str	r3, [r9, #-1608]
	mov	r3, #0
	bl	FlashProgPages
	ldrh	r0, [r4]
	bl	decrement_vpc_count
	b	.L3087
.L3084:
	add	sp, sp, #40
	@ sp needed
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, pc}
.L3102:
	.align	2
.L3101:
	.word	.LANCHOR2
	.word	305419896
	.word	.LANCHOR2-1664
	.fnend
	.size	FtlSuperblockPowerLostFix, .-FtlSuperblockPowerLostFix
	.align	2
	.global	FtlVpcCheckAndModify
	.type	FtlVpcCheckAndModify, %function
FtlVpcCheckAndModify:
	.fnstart
	@ args = 0, pretend = 0, frame = 8
	@ frame_needed = 0, uses_anonymous_args = 0
	stmfd	sp!, {r0, r1, r2, r4, r5, r6, r7, r8, r9, lr}
	.save {r4, r5, r6, r7, r8, r9, lr}
	.pad #12
	mov	r5, #0
	ldr	r4, .L3120
	ldr	r1, .L3120+4
	ldr	r0, .L3120+8
	bl	printk
	sub	r3, r4, #1712
	ldr	r0, [r4, #-1408]
	mov	r1, #0
	ldrh	r2, [r3, #-10]
	mov	r6, r4
	mov	r2, r2, asl #1
	bl	ftl_memset
.L3104:
	ldr	r3, [r4, #-1280]
	cmp	r5, r3
	bcs	.L3118
	mov	r0, r5
	add	r1, sp, #4
	mov	r2, #0
	bl	log2phys
	ldr	r0, [sp, #4]
	cmn	r0, #1
	beq	.L3105
	ubfx	r0, r0, #10, #16
	bl	P2V_block_in_plane
	ldr	r2, [r6, #-1408]
	mov	r0, r0, asl #1
	ldrh	r3, [r2, r0]
	add	r3, r3, #1
	strh	r3, [r2, r0]	@ movhi
.L3105:
	add	r5, r5, #1
	b	.L3104
.L3118:
	ldr	r6, .L3120
	mov	r7, #0
	ldr	r8, .L3120+12
	add	r9, r6, #888
.L3107:
	ldrh	r3, [r8]
	uxth	r4, r7
	cmp	r3, r4
	bls	.L3119
	ldr	r3, [r6, #-1404]
	mov	r5, r4, asl #1
	movw	r1, #65535
	ldrh	r2, [r3, r5]
	ldr	r3, [r6, #-1408]
	ldrh	r3, [r3, r5]
	cmp	r2, r1
	cmpne	r2, r3
	beq	.L3108
	ldrh	r1, [r9]
	cmp	r1, r4
	beq	.L3108
	ldr	r1, .L3120+16
	ldrh	r0, [r1]
	cmp	r0, r4
	beq	.L3108
	ldrh	r1, [r1, #-48]
	cmp	r1, r4
	beq	.L3108
	ldr	r0, .L3120+20
	mov	r1, r4
	bl	printk
	ldr	r3, [r6, #-1404]
	ldrh	r2, [r3, r5]
	cmp	r2, #0
	ldr	r2, [r6, #-1408]
	ldrh	r2, [r2, r5]
	strh	r2, [r3, r5]	@ movhi
	beq	.L3108
	mov	r0, r4
	bl	update_vpc_list
.L3108:
	add	r7, r7, #1
	b	.L3107
.L3119:
	bl	l2p_flush
	bl	FtlVpcTblFlush
	add	sp, sp, #12
	@ sp needed
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, pc}
.L3121:
	.align	2
.L3120:
	.word	.LANCHOR2
	.word	.LANCHOR3+84
	.word	.LC48
	.word	.LANCHOR2-1724
	.word	.LANCHOR2+984
	.word	.LC153
	.fnend
	.size	FtlVpcCheckAndModify, .-FtlVpcCheckAndModify
	.align	2
	.global	allocate_new_data_superblock
	.type	allocate_new_data_superblock, %function
allocate_new_data_superblock:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	stmfd	sp!, {r3, r4, r5, r6, r7, lr}
	.save {r3, r4, r5, r6, r7, lr}
	ldr	r5, .L3149
	ldrh	r7, [r0]
	ldr	r3, [r5, #-1276]
	cmp	r3, #0
	bne	.L3123
	movw	r3, #65535
	cmp	r7, r3
	mov	r4, r0
	beq	.L3124
	ldr	r2, [r5, #-1404]
	mov	r3, r7, asl #1
	mov	r0, r7
	ldrh	r3, [r2, r3]
	cmp	r3, #0
	beq	.L3125
	bl	INSERT_DATA_LIST
	b	.L3124
.L3125:
	bl	INSERT_FREE_LIST
.L3124:
	mov	r3, #0
	strb	r3, [r4, #8]
	ldr	r3, .L3149
	add	r2, r3, #936
	cmp	r4, r2
	beq	.L3126
	sub	r2, r3, #1712
	ldrh	r2, [r2]
	cmp	r2, #1
	beq	.L3126
	ldrb	r1, [r3, #-2744]	@ zero_extendqisi2
	cmp	r1, #0
	beq	.L3127
.L3126:
	mov	r3, #1
	strb	r3, [r4, #8]
	b	.L3128
.L3127:
	add	r1, r3, #888
	cmp	r4, r1
	bne	.L3128
	cmp	r2, #3
	beq	.L3130
	ldr	r3, [r3, #-1616]
	cmp	r3, #1
	bne	.L3131
.L3130:
	mov	r3, #1
	strb	r3, [r5, #896]
.L3131:
	ldr	r2, [r5, #-1800]
	ldr	r3, .L3149
	cmp	r2, #0
	beq	.L3128
	ldr	r2, [r3, #-1564]
	cmp	r2, #39
	movls	r2, #1
	strlsb	r2, [r3, #896]
.L3128:
	ldr	r3, .L3149+4
	movw	r2, #1752
	ldrh	r0, [r3, r2]
	movw	r2, #65535
	mov	r6, r3
	cmp	r0, r2
	beq	.L3133
	cmp	r7, r0
	bne	.L3134
	ldr	r2, [r5, #-1404]
	mov	r3, r0, asl #1
	ldrh	r3, [r2, r3]
	cmp	r3, #0
	beq	.L3135
.L3134:
	bl	update_vpc_list
.L3135:
	movw	r3, #1752
	mvn	r2, #0
	strh	r2, [r6, r3]	@ movhi
.L3133:
	mov	r0, r4
	bl	allocate_data_superblock
	bl	l2p_flush
	mov	r0, #0
	bl	FtlEctTblFlush
	bl	FtlVpcTblFlush
.L3123:
	mov	r0, #0
	ldmfd	sp!, {r3, r4, r5, r6, r7, pc}
.L3150:
	.align	2
.L3149:
	.word	.LANCHOR2
	.word	.LANCHOR4
	.fnend
	.size	allocate_new_data_superblock, .-allocate_new_data_superblock
	.align	2
	.global	FtlReadRefresh
	.type	FtlReadRefresh, %function
FtlReadRefresh:
	.fnstart
	@ args = 0, pretend = 0, frame = 40
	@ frame_needed = 0, uses_anonymous_args = 0
	stmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, lr}
	.save {r4, r5, r6, r7, r8, r9, r10, lr}
	.pad #40
	sub	sp, sp, #40
	ldr	r7, .L3168
	ldr	r5, .L3168+4
	ldr	r10, [r7, #1296]
	mov	r6, r7
	cmp	r10, #0
	beq	.L3152
	ldr	r2, [r7, #1300]
	ldr	r3, [r5, #-1280]
	cmp	r2, r3
	bcs	.L3153
	mov	r4, #2048
.L3158:
	ldr	r0, [r6, #1300]
	ldr	r3, [r5, #-1280]
	cmp	r0, r3
	bcs	.L3157
	mov	r2, #0
	mov	r1, sp
	bl	log2phys
	ldr	r3, [r7, #1300]
	add	r3, r3, #1
	str	r3, [r7, #1300]
	ldr	r2, [sp]
	cmn	r2, #1
	beq	.L3156
	add	r0, sp, #40
	str	r2, [sp, #8]
	mov	r1, #1
	mov	r2, #0
	str	r2, [r0, #-36]!
	str	r3, [sp, #20]
	str	r2, [sp, #12]
	str	r2, [sp, #16]
	bl	FlashReadPages
	ldr	r3, [sp, #4]
	cmp	r3, #256
	bne	.L3157
	ldr	r0, [sp]
	ubfx	r0, r0, #10, #16
	bl	P2V_block_in_plane
	bl	FtlGcRefreshBlock
.L3157:
	mvn	r0, #0
	b	.L3160
.L3156:
	subs	r4, r4, #1
	bne	.L3158
	b	.L3157
.L3153:
	ldr	r3, [r5, #-1584]
	mov	r0, #0
	str	r0, [r7, #1296]
	str	r0, [r7, #1300]
	str	r3, [r7, #1292]
	b	.L3160
.L3152:
	ldr	r1, [r5, #-1568]
	movw	r4, #10000
	ldr	r9, [r5, #-1584]
	add	r5, r5, #820
	cmp	r1, r4
	ldr	r8, [r7, #1292]
	add	r2, r9, #1048576
	ldr	r3, [r5, #-2100]
	movhi	r4, #31
	movls	r4, #63
	cmp	r8, r2
	bhi	.L3162
	mov	r1, r1, lsr #10
	mov	r0, #1000
	mul	r0, r0, r3
	add	r1, r1, #1
	bl	__aeabi_uidiv
	add	r0, r0, r8
	cmp	r0, r9
	bcc	.L3162
	ldrh	r3, [r5, #28]
	ands	r0, r4, r3
	movne	r0, r10
	bne	.L3160
	ldr	r2, [r7, #1316]
	cmp	r2, r3
	beq	.L3160
.L3162:
	ldrh	r3, [r5, #28]
	mov	r0, #0
	str	r9, [r6, #1292]
	str	r0, [r6, #1300]
	str	r3, [r6, #1316]
	mov	r3, #1
	str	r3, [r6, #1296]
.L3160:
	add	sp, sp, #40
	@ sp needed
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, pc}
.L3169:
	.align	2
.L3168:
	.word	.LANCHOR4
	.word	.LANCHOR2
	.fnend
	.size	FtlReadRefresh, .-FtlReadRefresh
	.align	2
	.global	ftl_do_gc
	.type	ftl_do_gc, %function
ftl_do_gc:
	.fnstart
	@ args = 0, pretend = 0, frame = 32
	@ frame_needed = 0, uses_anonymous_args = 0
	ldr	r3, .L3333
	ldr	r2, [r3, #-1276]
	cmp	r2, #0
	bne	.L3264
	stmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.save {r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.pad #44
	sub	sp, sp, #44
	ldr	r9, .L3333+4
	ldr	ip, [r9, #3348]
	cmn	ip, #1
	beq	.L3266
	ldr	r4, [r3, #-1560]
	cmp	r4, #0
	bne	.L3266
	add	r2, r3, #876
	ldrh	r2, [r2]
	cmp	r2, #47
	movls	r0, r4
	bls	.L3323
	movw	r2, #3352
	str	r1, [sp, #12]
	ldrh	r1, [r9, r2]
	movw	r2, #65535
	mov	r5, r3
	str	r0, [sp, #20]
	cmp	r1, r2
	bne	.L3172
.L3175:
	ldr	r4, .L3333+8
	movw	r1, #65535
	ldrh	ip, [r4, #-14]
	cmp	ip, r1
	bne	.L3173
	b	.L3174
.L3172:
	add	r3, r3, #984
	ldrh	r3, [r3]
	cmp	r3, r2
	beq	.L3175
	mov	r0, #1
	bl	FtlGcFreeTempBlock
	cmp	r0, #0
	beq	.L3175
	mov	r0, #1
	b	.L3323
.L3173:
	mov	r3, r4
	ldrh	r2, [r3, #-16]!
	cmp	r2, r1
	bne	.L3174
	ldrh	r0, [r4, #-12]
	cmp	r0, r2
	beq	.L3174
	ldrh	r1, [r4, #-10]
	cmp	r1, r2
	strneh	ip, [r3]	@ movhi
	strneh	r0, [r4, #-14]	@ movhi
	mvnne	r3, #0
	strneh	r1, [r4, #-12]	@ movhi
	strneh	r3, [r4, #-10]	@ movhi
.L3174:
	ldr	r2, [sp, #20]
	ldr	r7, [r5, #-1544]
	cmp	r2, #1
	ldr	r3, .L3333
	add	r7, r7, #1
	ldr	r6, .L3333+12
	add	r7, r7, r2, asl #7
	str	r7, [r5, #-1544]
	bne	.L3186
	ldr	r2, [r3, #-1800]
	cmp	r2, #0
	bne	.L3177
	ldrb	r3, [r3, #-2744]	@ zero_extendqisi2
	cmp	r3, #0
	beq	.L3186
.L3177:
	ldr	r3, [r5, #-1564]
	ldr	r10, .L3333
	cmp	r3, #39
	bhi	.L3186
	ldr	r9, .L3333+16
	movw	r3, #1944
	ldrh	r3, [r9, r3]
	add	r7, r7, r3
	str	r7, [r10, #-1544]
	bl	FtlGcReFreshBadBlk
	ldrh	r3, [r6]
	movw	r2, #65535
	cmp	r3, r2
	bne	.L3186
	sub	r2, r10, #1536
	ldrh	r2, [r2]
	cmp	r2, r3
	bne	.L3186
	ldr	r3, [r10, #-1544]
	add	r10, r10, #884
	cmp	r3, #1024
	bhi	.L3178
	ldrh	r3, [r10]
	cmp	r3, #63
	bhi	.L3186
.L3178:
	ldrh	r2, [r4, #-6]
	movw	r3, #1944
	ldrh	lr, [r10]
	mov	ip, #0
	add	r2, r2, #64
	ldr	r1, .L3333+16
	cmp	lr, r2
	strh	ip, [r9, r3]	@ movhi
	ldr	r0, .L3333
	bgt	.L3186
	ldr	r2, [r0, #-1564]
	str	ip, [r0, #-1544]
	cmp	r2, ip
	moveq	r2, #6
	beq	.L3325
	cmp	r2, #5
	bhi	.L3180
	mov	r2, #18
.L3325:
	strh	r2, [r1, r3]	@ movhi
.L3180:
	mov	r0, #32
	bl	List_get_gc_head_node
	movw	r3, #65535
	uxth	r10, r0
	cmp	r10, r3
	beq	.L3185
	ldrh	r0, [r4, #-4]
	ldr	r7, .L3333
	cmp	r0, #0
	sub	r8, r7, #1520
	beq	.L3182
	sub	r1, r7, #1648
	ldr	r2, [r7, #-1404]
	mov	r10, r10, asl #1
	ldrh	fp, [r1, #-14]
	sub	r1, r7, #1728
	ldrh	lr, [r2, r10]
	ldrh	r1, [r1, #-4]
	mul	r1, r1, fp
	add	r1, r1, #1
	cmp	lr, r1
	bgt	.L3185
	add	r1, r0, #1
	str	r3, [sp, #28]
	str	r2, [sp, #24]
	mov	r3, #0
	uxth	r1, r1
	str	r3, [r7, #-1556]
	strh	r1, [r8, #-4]	@ movhi
	str	r1, [sp, #16]
	bl	List_get_gc_head_node
	ldr	r3, [sp, #28]
	uxth	fp, r0
	ldr	r1, [sp, #16]
	cmp	fp, r3
	ldr	r2, [sp, #24]
	beq	.L3185
	mov	r9, fp, asl #1
	ldr	r0, .L3333+20
	ldrh	r3, [r2, r9]
	ldrh	r2, [r2, r10]
	str	r2, [sp]
	mov	r2, fp
	bl	printk
	ldrh	r3, [r8, #-4]
	cmp	r3, #40
	bls	.L3183
	ldr	r3, [r7, #-1404]
	ldrh	r3, [r3, r9]
	cmp	r3, #32
	movhi	r3, #0
	strhih	r3, [r8, #-4]	@ movhi
.L3183:
	ldr	r2, .L3333+16
	movw	r3, #1944
	mov	r1, #6
	strh	r1, [r2, r3]	@ movhi
	b	.L3187
.L3182:
	mov	r3, #1
	strh	r3, [r8, #-4]	@ movhi
.L3185:
	bl	GetSwlReplaceBlock
	movw	r3, #65535
	cmp	r0, r3
	mov	fp, r0
	bne	.L3187
	movw	r3, #1944
	mov	r2, #0
	strh	r2, [r9, r3]	@ movhi
.L3186:
	ldrh	r9, [r6]
	movw	r0, #65535
	cmp	r9, r0
	bne	.L3328
	ldr	r7, .L3333
	add	r3, r7, #984
	ldrh	r0, [r3]
	cmp	r0, r9
	movne	fp, r9
	beq	.L3330
.L3187:
	movw	ip, #65535
	rsb	r3, ip, fp
	clz	r3, r3
	ldr	r2, [sp, #20]
	mov	r3, r3, lsr #5
	cmp	r2, #0
	movne	r0, #0
	andeq	r0, r3, #1
	cmp	r0, #0
	beq	.L3200
	ldr	r3, .L3333+24
	ldrh	r3, [r3]
	cmp	r3, #24
	movhi	r7, #1
	bhi	.L3201
	ldr	r2, .L3333+28
	cmp	r3, #16
	ldrh	r7, [r2]
	movhi	r7, r7, lsr #5
	bhi	.L3201
	cmp	r3, #12
	movhi	r7, r7, lsr #4
	bhi	.L3201
	cmp	r3, #8
	movhi	r7, r7, lsr #2
.L3201:
	ldrh	r1, [r4, #-8]
	ldr	r2, .L3333
	cmp	r1, r3
	bcs	.L3205
	add	r3, r2, #984
	movw	r1, #65535
	ldrh	r3, [r3]
	cmp	r3, r1
	ldr	r1, .L3333+16
	bne	.L3206
	sub	r0, r2, #1536
	ldrh	r0, [r0]
	cmp	r0, r3
	bne	.L3206
	movw	r3, #1944
	ldrh	r0, [r1, r3]
	cmp	r0, #0
	bne	.L3207
	ldr	r3, [r2, #-1280]
	ldr	r2, [r2, #1128]
	add	r3, r3, r3, asl #1
	cmp	r2, r3, lsr #2
	movcs	r3, #18
	bcs	.L3208
.L3207:
	movw	r3, #1164
	ldrh	r3, [r1, r3]
	add	r3, r3, r3, asl #1
	ubfx	r3, r3, #2, #16
.L3208:
	strh	r3, [r4, #-8]	@ movhi
	mov	r3, #0
	str	r3, [r5, #-1556]
	b	.L3323
.L3206:
	movw	r3, #1164
	ldrh	r3, [r1, r3]
	add	r3, r3, r3, asl #1
	mov	r3, r3, asr #2
	strh	r3, [r4, #-8]	@ movhi
.L3205:
	ldr	r8, [sp, #12]
	movw	fp, #65535
	ldr	r3, [r5, #-1800]
	cmp	r8, #2
	movhi	r8, #0
	movls	r8, #1
	cmp	r3, #0
	moveq	r8, #0
	cmp	r8, #0
	addne	r7, r7, #1
	uxthne	r7, r7
	b	.L3210
.L3200:
	ldr	r1, .L3333
	add	r2, r1, #984
	ldrh	r2, [r2]
	cmp	r2, ip
	bne	.L3211
	sub	ip, r1, #1536
	ldrh	ip, [ip]
	cmp	ip, r2
	movne	r3, #0
	andeq	r3, r3, #1
	cmp	r3, #0
	beq	.L3211
	ldrh	r3, [r6]
	ldr	r10, .L3333+16
	cmp	r3, r2
	beq	.L3212
.L3216:
	movw	fp, #65535
	b	.L3211
.L3212:
	add	r7, r1, #884
	ldrh	r3, [r4, #-8]
	str	r0, [r1, #-1556]
	ldrh	r2, [r7]
	cmp	r2, r3
	bls	.L3213
	movw	r3, #1944
	ldrh	r3, [r10, r3]
	cmp	r3, #0
	bne	.L3214
	ldr	r3, [r1, #-1280]
	ldr	r2, [r1, #1128]
	add	r3, r3, r3, asl #1
	cmp	r2, r3, lsr #2
	movcs	r3, #18
	bcs	.L3215
.L3214:
	movw	r3, #1164
	ldrh	r3, [r10, r3]
	add	r3, r3, r3, asl #1
	ubfx	r3, r3, #2, #16
.L3215:
	strh	r3, [r4, #-8]	@ movhi
	bl	FtlReadRefresh
	mov	r0, #0
	bl	List_get_gc_head_node
	ldr	r3, [r5, #-1404]
	uxth	r0, r0
	mov	r0, r0, asl #1
	ldrh	r3, [r3, r0]
	cmp	r3, #4
	movwhi	r3, #1944
	ldrhih	r0, [r10, r3]
	bhi	.L3323
.L3213:
	movw	r8, #1944
	ldr	r9, .L3333+16
	ldrh	r0, [r10, r8]
	cmp	r0, #0
	bne	.L3216
	movw	r3, #1164
	ldrh	r10, [r9, r3]
	add	r3, r10, r10, asl #1
	mov	r3, r3, asr #2
	strh	r3, [r4, #-8]	@ movhi
	bl	List_get_gc_head_node
	ldr	r3, [r5, #-1404]
	uxth	r0, r0
	mov	r0, r0, asl #1
	ldrh	r2, [r3, r0]
	ldr	r3, .L3333+32
	ldrh	r1, [r3, #-14]
	ldrh	r4, [r3, #-84]
	mul	r4, r4, r1
	add	r4, r4, r4, lsr #31
	cmp	r2, r4, asr #1
	ble	.L3217
	ldrh	r1, [r7]
	sub	r3, r10, #1
	cmp	r1, r3
	blt	.L3217
	bl	FtlReadRefresh
	ldrh	r0, [r9, r8]
	b	.L3323
.L3217:
	cmp	r2, #0
	bne	.L3216
	movw	r0, #65535
	bl	decrement_vpc_count
	ldrh	r0, [r7]
	add	r0, r0, #1
	b	.L3323
.L3211:
	ldr	r3, [r5, #-1800]
	cmp	r3, #0
	moveq	r7, #1
	movne	r7, #2
.L3210:
	ldrh	r3, [r6]
	movw	r2, #65535
	cmp	r3, r2
	bne	.L3219
	cmp	fp, r3
	ldrne	r3, .L3333+12
	strneh	fp, [r3]	@ movhi
	bne	.L3221
	ldr	r1, .L3333
	sub	r3, r1, #1536
	ldrh	r2, [r3]
	cmp	r2, fp
	beq	.L3221
	ldr	r1, [r1, #-1404]
	mov	r2, r2, asl #1
	ldrh	r2, [r1, r2]
	cmp	r2, #0
	mvneq	r2, #0
	streqh	r2, [r3]	@ movhi
	ldrh	r2, [r3]
	strh	r2, [r6]	@ movhi
	mvn	r2, #0
	strh	r2, [r3]	@ movhi
.L3221:
	ldr	r3, .L3333+16
	mov	r2, #0
	ldrh	r0, [r6]
	add	r8, r3, #1168
	strb	r2, [r3, #1176]
	movw	r2, #65535
	cmp	r0, r2
	mov	r4, r3
	beq	.L3219
	bl	IsBlkInGcList
	cmp	r0, #0
	mvnne	r3, #0
	strneh	r3, [r8]	@ movhi
	ldrb	r3, [r5, #-2744]	@ zero_extendqisi2
	cmp	r3, #0
	beq	.L3225
	ldrh	r0, [r6]
	bl	ftl_get_blk_mode
	strb	r0, [r4, #1176]
.L3225:
	ldrh	r2, [r6]
	movw	r3, #65535
	ldr	r8, .L3333+16
	cmp	r2, r3
	add	r4, r8, #1168
	beq	.L3219
	mov	r0, r4
	bl	make_superblock
	movw	r2, #1946
	mov	r3, #0
	strh	r3, [r8, r2]	@ movhi
	strh	r3, [r4, #2]	@ movhi
	strb	r3, [r8, #1174]
	ldrh	r3, [r4]
	ldr	r2, [r5, #-1404]
	mov	r3, r3, asl #1
	ldrh	r2, [r2, r3]
	movw	r3, #1948
	strh	r2, [r8, r3]	@ movhi
.L3219:
	ldr	r2, .L3333+36
	ldrh	r3, [r6]
	ldrh	r1, [r2]
	cmp	r1, r3
	beq	.L3226
	ldrh	r1, [r2, #48]
	cmp	r1, r3
	beq	.L3226
	ldrh	r2, [r2, #96]
	cmp	r2, r3
	bne	.L3260
.L3226:
	mvn	r3, #0
	strh	r3, [r6]	@ movhi
.L3260:
	ldr	r6, .L3333+12
	movw	r3, #65535
	ldr	r5, .L3333
	ldrh	r2, [r6]
	cmp	r2, r3
	bne	.L3228
	mov	r3, #0
	str	r3, [r5, #-1556]
.L3229:
	ldr	r9, .L3333+40
	ldr	r8, .L3333+16
	ldrh	r4, [r9]
	mov	r0, r4
	bl	List_get_gc_head_node
	ldr	r3, .L3333+12
	uxth	r10, r0
	strh	r10, [r3]	@ movhi
	movw	r3, #65535
	cmp	r10, r3
	moveq	r3, #0
	moveq	r0, #8
	streqh	r3, [r9]	@ movhi
	beq	.L3323
.L3230:
	mov	r0, r10
	add	r4, r4, #1
	bl	IsBlkInGcList
	cmp	r0, #0
	ldrne	r3, .L3333+40
	strneh	r4, [r3]	@ movhi
	bne	.L3229
	ldr	r3, .L3333+40
	uxth	r4, r4
	ldrh	r2, [r9, #-208]
	mov	r1, r10, asl #1
	strh	r4, [r3]	@ movhi
	ldr	r3, .L3333
	ldr	r0, [r3, #-1404]
	ldrh	r3, [r9, #-140]
	ldrh	ip, [r0, r1]
	mul	r3, r2, r3
	add	r2, r3, r3, lsr #31
	cmp	ip, r2, asr #1
	bgt	.L3233
	cmp	ip, #8
	cmphi	r4, #48
	bls	.L3234
	ldr	r2, .L3333+44
	ldrh	r2, [r2]
	cmp	r2, #35
	bhi	.L3234
.L3233:
	ldr	ip, .L3333+40
	mov	r2, #0
	strh	r2, [ip]	@ movhi
.L3234:
	ldrh	r2, [r0, r1]
	movw	r1, #65535
	cmp	r2, r3
	cmpge	fp, r1
	bne	.L3235
	ldr	r3, .L3333+40
	ldrh	r3, [r3]
	cmp	r3, #3
	bhi	.L3235
	ldr	r2, .L3333+12
	mvn	r3, #0
	strh	r3, [r2]	@ movhi
	mov	r2, #0
	ldr	r3, .L3333+40
	strh	r2, [r3]	@ movhi
	movw	r3, #1944
	ldrh	r0, [r8, r3]
	b	.L3323
.L3235:
	cmp	r2, #0
	bne	.L3236
	movw	r0, #65535
	bl	decrement_vpc_count
	ldr	r3, .L3333+40
	ldr	r2, .L3333+40
	ldrh	r3, [r3]
	add	r3, r3, #1
	strh	r3, [r2]	@ movhi
	b	.L3229
.L3236:
	mov	r3, #0
	strb	r3, [r8, #1176]
	ldrb	r3, [r5, #-2744]	@ zero_extendqisi2
	cmp	r3, #0
	beq	.L3237
	mov	r0, r10
	bl	ftl_get_blk_mode
	ldr	r3, .L3333+16
	strb	r0, [r3, #1176]
.L3237:
	ldr	r0, .L3333+12
	bl	make_superblock
	ldrh	r2, [r6]
	ldr	r1, .L3333+48
	mov	r3, #0
	ldr	r0, [r5, #-1404]
	mov	r2, r2, asl #1
	strh	r3, [r1]	@ movhi
	ldrh	r2, [r0, r2]
	strh	r3, [r6, #2]	@ movhi
	strb	r3, [r8, #1174]
	strh	r2, [r1, #2]	@ movhi
.L3228:
	ldr	r3, [sp, #20]
	cmp	r3, #1
	bne	.L3238
	bl	FtlReadRefresh
.L3238:
	mov	r3, #1
	str	r3, [r5, #-1560]
	ldr	r3, .L3333+28
	ldrh	r2, [r3]
	str	r2, [sp, #12]
	ldrb	r2, [r5, #-2744]	@ zero_extendqisi2
	cmp	r2, #0
	beq	.L3239
	ldr	r2, .L3333+16
	ldrb	r2, [r2, #1176]	@ zero_extendqisi2
	cmp	r2, #1
	ldreqh	r3, [r3, #2]
	streq	r3, [sp, #12]
.L3239:
	ldrh	r3, [r6, #2]
	ldr	r1, [sp, #12]
	add	r2, r3, r7
	ldr	r6, .L3333+16
	cmp	r2, r1
	movgt	r2, r1
	rsbgt	r3, r3, r2
	uxthgt	r7, r3
	mov	r3, #0
	str	r3, [sp, #16]
.L3241:
	ldrh	r3, [sp, #16]
	ldr	r5, .L3333
	cmp	r3, r7
	ldr	r4, .L3333+12
	bcs	.L3248
	ldr	r3, .L3333+52
	mov	r2, #0
	ldrh	r0, [r4, #2]
	movw	lr, #65535
	ldr	r8, [r5, #-1484]
	mov	ip, #36
	ldrh	r9, [r3]
	ldr	r3, [sp, #16]
	add	r0, r0, r3
	add	r3, r4, #14
	mov	r4, r2
.L3249:
	uxth	r1, r2
	cmp	r1, r9
	bcs	.L3331
	ldrh	r1, [r3, #2]!
	add	r2, r2, #1
	cmp	r1, lr
	orrne	r1, r0, r1, asl #10
	mlane	r10, ip, r4, r8
	addne	r4, r4, #1
	uxthne	r4, r4
	strne	r1, [r10, #4]
	b	.L3249
.L3331:
	ldr	r0, [r5, #-1484]
	mov	r1, r4
	ldrb	r2, [r6, #1176]	@ zero_extendqisi2
	mov	r9, #0
	bl	FlashReadPages
	ldr	r5, .L3333
.L3244:
	uxth	r3, r9
	cmp	r3, r4
	bcs	.L3332
	mov	r3, #36
	ldr	r2, [r5, #-1484]
	mul	r8, r3, r9
	add	r1, r2, r8
	ldr	r2, [r2, r8]
	ldr	r10, [r1, #12]
	cmn	r2, #1
	beq	.L3280
	ldrh	r1, [r10]
	movw	r2, #61589
	cmp	r1, r2
	bne	.L3280
	add	r1, sp, #32
	mov	r2, #0
	ldr	r0, [r10, #8]
	str	r3, [sp, #24]
	bl	log2phys
	ldr	r2, [r5, #-1484]
	add	r2, r2, r8
	ldr	r0, [r2, #4]
	ldr	r1, [sp, #32]
	ldr	r3, [sp, #24]
	bic	r1, r1, #-2147483648
	cmp	r1, r0
	bne	.L3280
	ldr	r0, .L3333+48
	ldr	r2, [r2, #16]
	str	r3, [sp, #28]
	ldrh	r1, [r0]
	add	r1, r1, #1
	strh	r1, [r0]	@ movhi
	ldr	r0, [r6, #1744]
	ldr	r1, [r5, #-1496]
	mla	r1, r3, r0, r1
	str	r2, [r1, #16]
	str	r1, [sp, #24]
	bl	Ftl_get_new_temp_ppa
	ldr	r2, [r5, #-1496]
	ldr	r1, [sp, #24]
	ldr	r3, [sp, #28]
	str	r0, [r1, #4]
	ldr	r1, [r6, #1744]
	mla	r3, r3, r1, r2
	ldr	r2, [r5, #-1484]
	add	r2, r2, r8
	ldr	r1, [r2, #8]
	str	r1, [r3, #8]
	mov	r1, #1
	ldr	r2, [r2, #12]
	str	r2, [r3, #12]
	ldr	r3, [sp, #32]
	str	r3, [r10, #12]
	ldr	r3, .L3333+56
	ldrh	r2, [r3]
	str	r3, [sp, #24]
	strh	r2, [r10, #2]	@ movhi
	ldr	r2, [r5, #-1608]
	ldr	r0, [r5, #-1484]
	str	r2, [r10, #4]
	add	r0, r0, r8
	ldr	r2, [r6, #1744]
	add	r2, r2, #1
	str	r2, [r6, #1744]
	bl	FtlGcBufAlloc
	ldrb	r2, [r5, #-2744]	@ zero_extendqisi2
	cmp	r2, #0
	bne	.L3246
	ldrb	r2, [r5, #991]	@ zero_extendqisi2
	ldr	r1, [r6, #1744]
	cmp	r1, r2
	beq	.L3246
	ldr	r3, [sp, #24]
	ldrh	r3, [r3, #4]
	cmp	r3, #0
	bne	.L3280
.L3246:
	bl	Ftl_gc_temp_data_write_back
	cmp	r0, #0
	beq	.L3280
	ldr	r3, .L3333
	mov	r2, #0
	mvn	r1, #0
	str	r2, [r3, #-1560]
	ldr	r3, .L3333+12
	strh	r2, [r3, #2]	@ movhi
	sub	r2, r3, #1168
	strh	r1, [r3]	@ movhi
	movw	r3, #1944
	ldrh	r0, [r2, r3]
	b	.L3323
.L3280:
	add	r9, r9, #1
	b	.L3244
.L3332:
	ldr	r3, [sp, #16]
	add	r3, r3, #1
	str	r3, [sp, #16]
	b	.L3241
.L3248:
	ldrh	r3, [r4, #2]
	sub	r6, r4, #1168
	add	r7, r7, r3
	ldr	r3, [sp, #12]
	uxth	r7, r7
	strh	r7, [r4, #2]	@ movhi
	cmp	r7, r3
	mov	r7, r6
	bcc	.L3250
	ldr	r3, [r4, #576]
	cmp	r3, #0
	beq	.L3251
	bl	Ftl_gc_temp_data_write_back
	cmp	r0, #0
	movne	r3, #0
	strne	r3, [r5, #-1560]
	movwne	r3, #1944
	ldrneh	r0, [r6, r3]
	bne	.L3323
.L3251:
	ldr	r3, .L3333+48
	ldrh	r6, [r3]
	cmp	r6, #0
	bne	.L3252
	ldrh	r3, [r4]
	ldr	r2, [r5, #-1404]
	mov	r3, r3, asl #1
	ldrh	r3, [r2, r3]
	cmp	r3, #0
	beq	.L3252
.L3253:
	ldr	r3, [r5, #-1280]
	cmp	r6, r3
	bcs	.L3258
	mov	r0, r6
	add	r1, sp, #36
	mov	r2, #0
	bl	log2phys
	ldr	r0, [sp, #36]
	cmn	r0, #1
	beq	.L3254
	ubfx	r0, r0, #10, #16
	bl	P2V_block_in_plane
	ldrh	r3, [r4]
	cmp	r3, r0
	bne	.L3254
.L3258:
	ldr	r3, [r5, #-1280]
	cmp	r6, r3
	bcc	.L3252
	ldr	r2, .L3333
	mov	r1, #0
	ldrh	r3, [r4]
	ldr	r2, [r2, #-1404]
	mov	r3, r3, asl #1
	strh	r1, [r2, r3]	@ movhi
	ldrh	r0, [r4]
	bl	update_vpc_list
	bl	FtlCacheWriteBack
	bl	l2p_flush
	bl	FtlVpcTblFlush
	b	.L3252
.L3254:
	add	r6, r6, #1
	b	.L3253
.L3252:
	mvn	r3, #0
	strh	r3, [r4]	@ movhi
.L3250:
	ldr	r5, .L3333
	add	r3, r5, #884
	ldrh	r3, [r3]
	cmp	r3, #2
	subls	r5, r5, #1664
	ldrlsh	r7, [r5]
	bls	.L3260
.L3259:
	mov	r2, #0
	str	r2, [r5, #-1560]
	movw	r2, #1944
	ldrh	r0, [r7, r2]
	cmp	r0, #0
	addeq	r0, r3, #1
	b	.L3323
.L3264:
	mov	r0, #0
	bx	lr
.L3266:
	mov	r0, r2
	b	.L3323
.L3330:
	sub	r3, r7, #1536
	ldrh	r10, [r3]
	cmp	r10, r0
	bne	.L3328
	add	r9, r7, #884
	ldr	r2, [r7, #-1544]
	ldrh	r3, [r9]
	cmp	r3, #24
	movcc	r3, #5120
	movcs	r3, #1024
	cmp	r2, r3
	movls	fp, r10
	bls	.L3187
	ldr	r1, .L3333+16
	movw	r2, #1944
	mov	r3, #0
	str	r3, [r5, #-1544]
	strh	r3, [r1, r2]	@ movhi
	bl	GetSwlReplaceBlock
	cmp	r0, r10
	mov	fp, r0
	bne	.L3189
	ldrh	r2, [r4, #-6]
	sub	r3, r7, #1520
	ldrh	r1, [r9]
	cmp	r1, r2
	movcs	r2, #80
	strcsh	r2, [r3, #-6]	@ movhi
	bcs	.L3199
	mov	r0, #64
	bl	List_get_gc_head_node
	uxth	r3, r0
	cmp	r3, fp
	beq	.L3199
	ldr	r2, [r7, #-1620]
	sub	r1, r7, #1712
	cmp	r2, #0
	bne	.L3192
	ldrh	r2, [r1]
	cmp	r2, #3
	beq	.L3192
	ldr	r2, [r7, #-1616]
	cmp	r2, #0
	bne	.L3192
	ldr	r2, [r7, #-1800]
	cmp	r2, #0
	bne	.L3192
	ldrb	r0, [r7, #-2744]	@ zero_extendqisi2
	cmp	r0, #0
	beq	.L3193
.L3192:
	ldr	r2, [r5, #-1404]
	mov	r3, r3, asl #1
	ldrh	r1, [r1]
	ldrh	r0, [r2, r3]
	cmp	r1, #3
	ldr	r2, .L3333+32
	ldrh	r3, [r2, #-14]
	ldrh	r2, [r2, #-84]
	mul	r2, r2, r3
	moveq	r3, r3, lsr #1
	movne	r3, #0
	add	r3, r2, r3
	cmp	r0, r3
	bgt	.L3195
	mov	r0, #0
	bl	List_get_gc_head_node
	ldr	r3, [r5, #-1280]
	ldr	r2, [r5, #1128]
	add	r3, r3, r3, asl #1
	cmp	r2, r3, lsr #2
	movls	r3, #160
	uxth	fp, r0
	bls	.L3326
	b	.L3329
.L3193:
	ldr	r2, [r7, #-1404]
	mov	r3, r3, asl #1
	ldrh	r3, [r2, r3]
	cmp	r3, #7
	bhi	.L3198
	bl	List_get_gc_head_node
	uxth	fp, r0
.L3329:
	mov	r3, #128
.L3326:
	strh	r3, [r4, #-6]	@ movhi
	movw	r3, #65535
	cmp	fp, r3
	beq	.L3199
	b	.L3189
.L3195:
	mov	r3, #128
	b	.L3327
.L3198:
	mov	r3, #64
.L3327:
	strh	r3, [r4, #-6]	@ movhi
	b	.L3199
.L3189:
	ldr	r0, [r5, #-1412]
	mov	r1, fp, asl #1
	ldr	r3, [r5, #-1404]
	ldrh	r2, [r9]
	ldrh	r3, [r3, r1]
	ldrh	r1, [r0, r1]
	ldr	r0, .L3333+60
	str	r1, [sp]
	ldrh	r1, [r4, #-8]
	str	r1, [sp, #4]
	mov	r1, fp
	bl	printk
.L3199:
	bl	FtlGcReFreshBadBlk
	b	.L3187
.L3328:
	mov	fp, r0
	b	.L3187
.L3323:
	add	sp, sp, #44
	@ sp needed
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L3334:
	.align	2
.L3333:
	.word	.LANCHOR2
	.word	.LANCHOR1
	.word	.LANCHOR2-1520
	.word	.LANCHOR4+1168
	.word	.LANCHOR4
	.word	.LC154
	.word	.LANCHOR2+884
	.word	.LANCHOR2-1664
	.word	.LANCHOR2-1648
	.word	.LANCHOR2+888
	.word	.LANCHOR2-1524
	.word	.LANCHOR4+1768
	.word	.LANCHOR4+1946
	.word	.LANCHOR2-1732
	.word	.LANCHOR2+984
	.word	.LC155
	.fnend
	.size	ftl_do_gc, .-ftl_do_gc
	.align	2
	.global	FtlCacheWriteBack
	.type	FtlCacheWriteBack, %function
FtlCacheWriteBack:
	.fnstart
	@ args = 0, pretend = 0, frame = 8
	@ frame_needed = 0, uses_anonymous_args = 0
	stmfd	sp!, {r0, r1, r2, r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.save {r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.pad #12
	ldr	r4, .L3379
	ldr	r8, .L3379+4
	ldr	r3, [r4, #-1276]
	ldr	r5, [r8, #1952]
	cmp	r3, #0
	bne	.L3337
	ldr	r1, [r4, #-1512]
	cmp	r1, #0
	beq	.L3337
	ldrb	r6, [r4, #-2744]	@ zero_extendqisi2
	mov	r7, #0
	ldr	r0, [r4, #-1480]
	mov	r10, #36
	cmp	r6, #0
	ldrb	r3, [r5, #9]	@ zero_extendqisi2
	ldr	r9, .L3379
	ldrneb	r6, [r5, #8]	@ zero_extendqisi2
	subne	r6, r6, #1
	clzne	r6, r6
	movne	r6, r6, lsr #5
	mov	r2, r6
	bl	FlashProgPages
.L3340:
	ldr	r3, [r4, #-1512]
	cmp	r7, r3
	bcs	.L3358
	mul	fp, r10, r7
	ldr	r3, [r9, #-1480]
	add	r2, r3, fp
	ldr	r3, [r3, fp]
	cmn	r3, #1
	beq	.L3361
	ldr	r3, [r2, #4]
	cmp	r6, #0
	ldr	r0, [r2, #16]
	add	r1, sp, #4
	mov	r2, #1
	orrne	r3, r3, #-2147483648
	str	r3, [sp, #4]
	bl	log2phys
	ldr	r3, [r4, #-1480]
	add	r3, r3, fp
	ldr	r3, [r3, #12]
	ldr	r0, [r3, #12]
	cmn	r0, #1
	beq	.L3344
	ubfx	r0, r0, #10, #16
	bl	P2V_block_in_plane
	ldr	r2, [r9, #-1404]
	mov	r3, r0, asl #1
	mov	fp, r0
	ldrh	r2, [r2, r3]
	cmp	r2, #0
	bne	.L3345
	ldr	r0, .L3379+8
	mov	r1, fp
	bl	printk
.L3345:
	mov	r0, fp
	bl	decrement_vpc_count
.L3344:
	add	r7, r7, #1
	b	.L3340
.L3377:
	ldr	r6, .L3379+12
	movw	r5, #16386
.L3357:
	ldrh	r3, [r6]
	cmp	r3, #0
	beq	.L3358
	mov	r0, #1
	mov	r1, r0
	bl	ftl_do_gc
	subs	r5, r5, #1
	bne	.L3357
.L3358:
	mov	r3, #0
	str	r3, [r4, #-1512]
	b	.L3337
.L3361:
	ldr	r9, .L3379
	mov	r10, #0
.L3341:
	ldr	r3, [r4, #-1512]
	cmp	r10, r3
	bcs	.L3377
	mov	r7, #36
	ldr	r3, [r9, #-1480]
	mul	r7, r7, r10
	mov	fp, #0
	mvn	r2, #0
	str	r2, [r3, r7]
.L3347:
	ldr	r3, [r4, #-1480]
	add	r2, r3, r7
	ldr	r3, [r3, r7]
	cmn	r3, #1
	bne	.L3378
	ldr	r0, [r2, #4]
	ubfx	r0, r0, #10, #16
	bl	P2V_block_in_plane
	ldrh	r3, [r5]
	cmp	r3, r0
	bne	.L3348
	ldr	r1, [r9, #-1404]
	mov	r3, r3, asl #1
	ldrh	r0, [r5, #4]
	ldrh	r2, [r1, r3]
	rsb	r2, r0, r2
	strh	r2, [r1, r3]	@ movhi
	ldr	r3, .L3379+16
	strb	fp, [r5, #6]
	strh	fp, [r5, #4]	@ movhi
	ldrh	r3, [r3]
	strh	r3, [r5, #2]	@ movhi
.L3348:
	ldrh	r3, [r5, #4]
	cmp	r3, #0
	bne	.L3349
	mov	r0, r5
	bl	allocate_new_data_superblock
.L3349:
	ldr	r3, [r8, #1312]
	add	r3, r3, #1
	str	r3, [r8, #1312]
	ldr	r3, [r4, #-1480]
	add	r3, r3, r7
	ldr	r0, [r3, #4]
	ubfx	r0, r0, #10, #16
	bl	FtlGcMarkBadPhyBlk
	mov	r0, r5
	bl	get_new_active_ppa
	ldr	r3, [r4, #-1480]
	mov	r1, #1
	mov	r2, r6
	add	r3, r3, r7
	str	r0, [sp, #4]
	str	r0, [r3, #4]
	mov	r0, r3
	ldrb	r3, [r5, #9]	@ zero_extendqisi2
	bl	FlashProgPages
	ldr	r3, [r4, #-1480]
	ldr	r3, [r3, r7]
	cmn	r3, #1
	moveq	r3, #1
	streq	r3, [r9, #-1276]
	ldr	r3, [r4, #-1276]
	cmp	r3, #0
	beq	.L3347
	b	.L3337
.L3378:
	ldr	r3, [r2, #4]
	cmp	r6, #0
	ldr	r0, [r2, #16]
	add	r1, sp, #4
	mov	r2, #1
	orrne	r3, r3, #-2147483648
	str	r3, [sp, #4]
	bl	log2phys
	ldr	r3, [r4, #-1480]
	add	r7, r3, r7
	ldr	r3, [r7, #12]
	ldr	r0, [r3, #12]
	cmn	r0, #1
	beq	.L3354
	ubfx	r0, r0, #10, #16
	bl	P2V_block_in_plane
	ldr	r2, [r9, #-1404]
	mov	r3, r0, asl #1
	mov	r7, r0
	ldrh	r2, [r2, r3]
	cmp	r2, #0
	bne	.L3355
	ldr	r0, .L3379+8
	mov	r1, r7
	bl	printk
.L3355:
	mov	r0, r7
	bl	decrement_vpc_count
.L3354:
	add	r10, r10, #1
	b	.L3341
.L3337:
	mov	r0, #0
	add	sp, sp, #12
	@ sp needed
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L3380:
	.align	2
.L3379:
	.word	.LANCHOR2
	.word	.LANCHOR4
	.word	.LC156
	.word	.LANCHOR2-1522
	.word	.LANCHOR2-1664
	.fnend
	.size	FtlCacheWriteBack, .-FtlCacheWriteBack
	.align	2
	.global	FtlSysFlush
	.type	FtlSysFlush, %function
FtlSysFlush:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	stmfd	sp!, {r3, lr}
	.save {r3, lr}
	ldr	r3, .L3387
	ldr	r3, [r3, #-1276]
	cmp	r3, #0
	bne	.L3382
	ldr	r3, .L3387+4
	ldr	r3, [r3, #3348]
	cmn	r3, #1
	beq	.L3382
	bl	FtlCacheWriteBack
	bl	l2p_flush
	mov	r0, #1
	bl	FtlEctTblFlush
	bl	FtlVpcTblFlush
.L3382:
	mov	r0, #0
	ldmfd	sp!, {r3, pc}
.L3388:
	.align	2
.L3387:
	.word	.LANCHOR2
	.word	.LANCHOR1
	.fnend
	.size	FtlSysFlush, .-FtlSysFlush
	.align	2
	.global	FtlDeInit
	.type	FtlDeInit, %function
FtlDeInit:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	stmfd	sp!, {r3, lr}
	.save {r3, lr}
	ldr	r3, .L3392
	ldr	r3, [r3, #3348]
	cmp	r3, #1
	bne	.L3390
	bl	FtlSysFlush
.L3390:
	mov	r0, #0
	ldmfd	sp!, {r3, pc}
.L3393:
	.align	2
.L3392:
	.word	.LANCHOR1
	.fnend
	.size	FtlDeInit, .-FtlDeInit
	.align	2
	.global	ftl_deinit
	.type	ftl_deinit, %function
ftl_deinit:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	stmfd	sp!, {r3, lr}
	.save {r3, lr}
	bl	ftl_flash_de_init
	bl	FtlDeInit
	ldmfd	sp!, {r3, lr}
	b	ftl_flash_de_init
	.fnend
	.size	ftl_deinit, .-ftl_deinit
	.align	2
	.global	ftl_cache_flush
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
	.type	ftl_discard, %function
ftl_discard:
	.fnstart
	@ args = 0, pretend = 0, frame = 8
	@ frame_needed = 0, uses_anonymous_args = 0
	stmfd	sp!, {r0, r1, r4, r5, r6, r7, r8, lr}
	.save {r4, r5, r6, r7, r8, lr}
	.pad #8
	add	r2, r0, r1
	ldr	r4, .L3415
	mov	r6, r0
	mov	r5, r1
	ldr	r3, [r4, #-2740]
	cmp	r2, r3
	mvnhi	r0, #0
	bhi	.L3398
	ldr	r3, .L3415+4
	ldr	r0, [r3, #3348]
	cmn	r0, #1
	beq	.L3398
	cmp	r1, #31
	bhi	.L3399
.L3400:
	mov	r0, #0
	b	.L3398
.L3399:
	ldr	r3, [r4, #-1276]
	cmp	r3, #0
	bne	.L3400
	sub	r4, r4, #1648
	bl	FtlCacheWriteBack
	mov	r0, r6
	ldrh	r4, [r4, #-10]
	mov	r1, r4
	bl	__aeabi_uidiv
	smulbb	r3, r0, r4
	mov	r7, r0
	rsb	r6, r3, r6
	uxth	r6, r6
	cmp	r6, #0
	beq	.L3401
	rsb	r4, r6, r4
	add	r7, r0, #1
	cmp	r4, r5
	movcs	r4, r5
	uxth	r4, r4
	rsb	r5, r4, r5
.L3401:
	ldr	r4, .L3415+8
	mvn	r3, #0
	ldr	r8, .L3415
	str	r3, [sp, #4]
	mov	r6, r4
.L3402:
	ldrh	r3, [r4]
	cmp	r5, r3
	bcc	.L3414
	mov	r0, r7
	mov	r1, sp
	mov	r2, #0
	bl	log2phys
	ldr	r3, [sp]
	cmn	r3, #1
	beq	.L3403
	ldr	r2, .L3415+12
	add	r1, sp, #4
	mov	r0, r7
	ldr	r3, [r2, #1956]
	add	r3, r3, #1
	str	r3, [r2, #1956]
	ldr	r3, [r8, #-1596]
	mov	r2, #1
	add	r3, r3, #1
	str	r3, [r8, #-1596]
	bl	log2phys
	ldr	r0, [sp]
	ubfx	r0, r0, #10, #16
	bl	P2V_block_in_plane
	bl	decrement_vpc_count
.L3403:
	ldrh	r3, [r6]
	add	r7, r7, #1
	rsb	r5, r3, r5
	b	.L3402
.L3414:
	ldr	r3, .L3415+12
	ldr	r2, [r3, #1956]
	cmp	r2, #32
	bls	.L3400
	mov	r4, #0
	str	r4, [r3, #1956]
	bl	l2p_flush
	bl	FtlVpcTblFlush
	b	.L3400
.L3398:
	add	sp, sp, #8
	@ sp needed
	ldmfd	sp!, {r4, r5, r6, r7, r8, pc}
.L3416:
	.align	2
.L3415:
	.word	.LANCHOR2
	.word	.LANCHOR1
	.word	.LANCHOR2-1658
	.word	.LANCHOR4
	.fnend
	.size	ftl_discard, .-ftl_discard
	.align	2
	.global	FtlGcFreeTempBlock
	.type	FtlGcFreeTempBlock, %function
FtlGcFreeTempBlock:
	.fnstart
	@ args = 0, pretend = 0, frame = 16
	@ frame_needed = 0, uses_anonymous_args = 0
	stmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.save {r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.pad #20
	sub	sp, sp, #20
	ldr	r6, .L3456
	sub	r9, r6, #1664
	ldr	r8, [r6, #-1276]
	ldrh	r1, [r9]
	cmp	r8, #0
	bne	.L3454
	add	r4, r6, #984
	mov	r5, r6
	movw	ip, #65535
	ldrh	r6, [r4]
	cmp	r6, ip
	bne	.L3420
.L3429:
	ldrh	r2, [r4]
	movw	r3, #65535
	ldr	r6, .L3456+4
	mov	r7, #0
	ldr	r8, .L3456
	cmp	r2, r3
	str	r7, [r6, #1760]
	add	r10, r8, #984
	beq	.L3454
	bl	FtlCacheWriteBack
	ldrh	r2, [r9]
	ldrb	r0, [r8, #991]	@ zero_extendqisi2
	ldrh	r3, [r10]
	mov	r10, #12
	ldr	r1, [r8, #-1404]
	smulbb	r2, r0, r2
	mov	r3, r3, asl #1
	ldr	r9, .L3456+8
	strh	r2, [r1, r3]	@ movhi
	movw	r3, #1770
	ldr	r2, [r8, #-1604]
	ldrh	r3, [r6, r3]
	add	r3, r3, r2
	str	r3, [r8, #-1604]
	b	.L3430
.L3420:
	cmp	r0, #0
	beq	.L3423
	ldr	r2, .L3456+12
	movw	r3, #3352
	ldrh	r0, [r2, r3]
	cmp	r0, ip
	beq	.L3424
.L3425:
	mov	r1, #2
	b	.L3423
.L3424:
	strh	r8, [r2, r3]	@ movhi
	add	r3, r5, #884
	ldrh	r3, [r3]
	cmp	r3, #17
	bhi	.L3425
.L3423:
	ldr	r7, .L3456
	add	r0, r7, #984
	bl	FtlGcScanTempBlk
	cmn	r0, #1
	str	r0, [sp, #12]
	beq	.L3426
	ldr	r2, [r7, #-1412]
	mov	r6, r6, asl #1
	ldrh	r3, [r2, r6]
	cmp	r3, #4
	bls	.L3427
	sub	r3, r3, #5
	mov	r0, #1
	strh	r3, [r2, r6]	@ movhi
	bl	FtlEctTblFlush
.L3427:
	ldr	r4, .L3456+4
	ldr	r3, [r4, #1760]
	cmp	r3, #0
	bne	.L3428
	ldr	r0, [sp, #12]
	ldr	r3, [r4, #1312]
	ubfx	r0, r0, #10, #16
	add	r3, r3, #1
	str	r3, [r4, #1312]
	bl	FtlBbmMapBadBlock
	bl	FtlBbmTblFlush
.L3428:
	mov	r3, #0
	str	r3, [r4, #1760]
	b	.L3440
.L3426:
	ldr	r2, .L3456+12
	movw	r3, #3352
	ldrh	r2, [r2, r3]
	movw	r3, #65535
	cmp	r2, r3
	bne	.L3440
	b	.L3429
.L3433:
	ldr	r3, [fp, #4]
	cmp	r0, r3
	bne	.L3452
.L3432:
	add	r7, r7, #1
.L3430:
	ldrh	r3, [r9]
	uxth	r8, r7
	cmp	r3, r8
	bls	.L3455
	mul	r8, r10, r8
	ldr	r3, [r5, #-1504]
	ldr	r2, [r5, #-1280]
	add	fp, r3, r8
	ldr	r0, [fp, #8]
	cmp	r0, r2
	bcs	.L3452
	add	r1, sp, #12
	mov	r2, #0
	str	r3, [sp, #4]
	bl	log2phys
	ldr	r3, [sp, #4]
	ldr	r0, [sp, #12]
	ldr	r3, [r3, r8]
	cmp	r0, r3
	bne	.L3433
	ubfx	r0, r0, #10, #16
	bl	P2V_block_in_plane
	add	r1, fp, #4
	mov	r2, #1
	mov	r8, r0
	ldr	r0, [fp, #8]
	bl	log2phys
	mov	r0, r8
	b	.L3453
.L3452:
	ldrh	r0, [r4]
.L3453:
	bl	decrement_vpc_count
	b	.L3432
.L3455:
	movw	r0, #65535
	bl	decrement_vpc_count
	ldrb	r3, [r5, #-2744]	@ zero_extendqisi2
	cmp	r3, #0
	beq	.L3435
	ldr	r0, .L3456+16
	ldrh	r1, [r4]
	bl	printk
.L3435:
	ldrh	r0, [r4]
	ldr	r2, [r5, #-1404]
	mov	r3, r0, asl #1
	ldrh	r3, [r2, r3]
	cmp	r3, #0
	beq	.L3436
	bl	INSERT_DATA_LIST
	b	.L3437
.L3436:
	bl	INSERT_FREE_LIST
.L3437:
	movw	r3, #1770
	mvn	r9, #0
	ldr	r8, .L3456+4
	strh	r9, [r4]	@ movhi
	mov	r4, #0
	strh	r4, [r6, r3]	@ movhi
	movw	r3, #1768
	strh	r4, [r6, r3]	@ movhi
	bl	l2p_flush
	bl	FtlVpcTblFlush
	add	r3, r8, #1168
	ldr	r7, .L3456
	strh	r9, [r3]	@ movhi
	ldr	r3, [r5, #-1800]
	add	r1, r7, #884
	cmp	r3, r4
	beq	.L3438
	ldr	r3, [r7, #-1564]
	cmp	r3, #39
	bhi	.L3438
	movw	r3, #1164
	ldrh	r2, [r1]
	ldrh	r3, [r8, r3]
	cmp	r2, r3
	subcc	r7, r7, #1520
	movcc	r3, r3, asl #1
	strcch	r3, [r7, #-8]	@ movhi
	b	.L3454
.L3438:
	movw	r3, #1164
	ldrh	r1, [r1]
	ldrh	r2, [r6, r3]
	ldr	r3, .L3456
	add	r0, r2, r2, asl #1
	cmp	r1, r0, asr #2
	ble	.L3454
	ldrb	r0, [r3, #-2744]	@ zero_extendqisi2
	sub	r3, r3, #1520
	cmp	r0, #0
	moveq	r2, #20
	streqh	r2, [r3, #-8]	@ movhi
	beq	.L3419
	sub	r2, r2, #2
	strh	r2, [r3, #-8]	@ movhi
.L3454:
	mov	r0, #0
	b	.L3419
.L3440:
	mov	r0, #1
.L3419:
	add	sp, sp, #20
	@ sp needed
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L3457:
	.align	2
.L3456:
	.word	.LANCHOR2
	.word	.LANCHOR4
	.word	.LANCHOR4+1770
	.word	.LANCHOR1
	.word	.LC157
	.fnend
	.size	FtlGcFreeTempBlock, .-FtlGcFreeTempBlock
	.align	2
	.global	FtlGcPageRecovery
	.type	FtlGcPageRecovery, %function
FtlGcPageRecovery:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	stmfd	sp!, {r3, r4, r5, lr}
	.save {r3, r4, r5, lr}
	ldr	r4, .L3461
	ldr	r5, .L3461+4
	mov	r0, r4
	ldrh	r1, [r5]
	bl	FtlGcScanTempBlk
	ldrh	r2, [r4, #2]
	ldrh	r3, [r5]
	cmp	r2, r3
	ldmccfd	sp!, {r3, r4, r5, pc}
	add	r0, r4, #48
	bl	FtlMapBlkWriteDumpData
	mov	r0, #0
	bl	FtlGcFreeTempBlock
	ldr	r3, .L3461+8
	mov	r2, #0
	str	r2, [r3, #1760]
	ldmfd	sp!, {r3, r4, r5, pc}
.L3462:
	.align	2
.L3461:
	.word	.LANCHOR2+984
	.word	.LANCHOR2-1664
	.word	.LANCHOR4
	.fnend
	.size	FtlGcPageRecovery, .-FtlGcPageRecovery
	.align	2
	.global	FtlPowerLostRecovery
	.type	FtlPowerLostRecovery, %function
FtlPowerLostRecovery:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	stmfd	sp!, {r4, r5, r6, lr}
	.save {r4, r5, r6, lr}
	mov	r5, #0
	ldr	r4, .L3465
	ldr	r3, .L3465+4
	add	r6, r4, #888
	add	r4, r4, #936
	mov	r0, r6
	str	r5, [r3, #1812]
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
	ldmfd	sp!, {r4, r5, r6, pc}
.L3466:
	.align	2
.L3465:
	.word	.LANCHOR2
	.word	.LANCHOR4
	.fnend
	.size	FtlPowerLostRecovery, .-FtlPowerLostRecovery
	.align	2
	.global	FtlSysBlkInit
	.type	FtlSysBlkInit, %function
FtlSysBlkInit:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	ldr	r3, .L3485
	mov	r2, #0
	stmfd	sp!, {r4, r5, r6, r7, r8, lr}
	.save {r4, r5, r6, r7, r8, lr}
	add	r8, r3, #1808
	ldr	r4, .L3485+4
	mvn	r1, #0
	strh	r2, [r8]	@ movhi
	movw	r2, #1806
	strh	r1, [r3, r2]	@ movhi
	ldr	r3, [r4, #-1728]
	uxth	r0, r3
	bl	FtlFreeSysBlkQueueInit
	bl	FtlScanSysBlk
	movw	r3, #1132
	ldrh	r2, [r4, r3]
	movw	r3, #65535
	cmp	r2, r3
	bne	.L3468
.L3470:
	mvn	r7, #0
	b	.L3469
.L3468:
	bl	FtlLoadSysInfo
	subs	r7, r0, #0
	bne	.L3470
	bl	FtlLoadMapInfo
	mov	r6, r7
	bl	FtlLoadVonderInfo
	bl	Ftl_load_ext_data
	bl	FtlLoadEctTbl
	bl	FtlFreeSysBLkSort
	bl	SupperBlkListInit
	bl	FtlPowerLostRecovery
	mov	r0, #1
	bl	FtlUpdateVaildLpn
	sub	r3, r4, #1616
	mov	r1, #12
	ldrh	r2, [r3, #-10]
	ldr	r3, [r4, #-1360]
.L3471:
	cmp	r6, r2
	bge	.L3476
	mla	r0, r1, r6, r3
	ldr	r0, [r0, #4]
	cmp	r0, #0
	bge	.L3472
.L3476:
	ldr	r5, .L3485+8
	cmp	r6, r2
	add	r6, r5, #68
	ldrh	r3, [r5, #28]
	add	r3, r3, #1
	strh	r3, [r5, #28]	@ movhi
	bge	.L3483
	b	.L3473
.L3472:
	add	r6, r6, #1
	b	.L3471
.L3483:
	ldrh	r3, [r8]
	cmp	r3, #0
	beq	.L3477
.L3473:
	ldrh	r3, [r6]
	ldr	r1, [r4, #-1404]
	ldr	ip, .L3485+12
	mov	r3, r3, asl #1
	ldrh	r0, [r6, #4]
	ldrh	r2, [r1, r3]
	rsb	r2, r0, r2
	strh	r2, [r1, r3]	@ movhi
	ldrh	r3, [ip]
	mov	r2, #0
	ldr	lr, [r4, #-1404]
	strb	r2, [r4, #894]
	strh	r3, [r6, #2]	@ movhi
	ldr	r3, .L3485+16
	strh	r2, [r6, #4]	@ movhi
	ldrh	r1, [r3]
	ldrh	r8, [r3, #4]
	mov	r1, r1, asl #1
	ldrh	r0, [lr, r1]
	rsb	r0, r8, r0
	strh	r0, [lr, r1]	@ movhi
	ldrh	r1, [ip]
	strh	r2, [r3, #4]	@ movhi
	strb	r2, [r4, #942]
	strh	r1, [r3, #2]	@ movhi
	ldrh	r3, [r5, #30]
	add	r3, r3, #1
	strh	r3, [r5, #30]	@ movhi
	bl	l2p_flush
	bl	FtlVpcTblFlush
	bl	FtlVpcTblFlush
.L3477:
	ldrh	r0, [r6]
	movw	r3, #65535
	ldr	r8, .L3485+20
	cmp	r0, r3
	beq	.L3478
	ldrh	r3, [r8, #4]
	cmp	r3, #0
	bne	.L3478
	ldrh	r3, [r8, #52]
	add	r4, r8, #48
	cmp	r3, #0
	bne	.L3478
	bl	FtlGcRefreshOpenBlock
	ldrh	r0, [r8, #48]
	bl	FtlGcRefreshOpenBlock
	bl	FtlVpcTblFlush
	mov	r0, r8
	bl	allocate_new_data_superblock
	mov	r0, r4
	bl	allocate_new_data_superblock
.L3478:
	ldr	r3, .L3485+24
	ldrb	r3, [r3]	@ zero_extendqisi2
	cmp	r3, #0
	bne	.L3479
	ldrh	r3, [r5, #28]
	tst	r3, #31
	bne	.L3469
.L3479:
	bl	FtlVpcCheckAndModify
.L3469:
	mov	r0, r7
	ldmfd	sp!, {r4, r5, r6, r7, r8, pc}
.L3486:
	.align	2
.L3485:
	.word	.LANCHOR4
	.word	.LANCHOR2
	.word	.LANCHOR2+820
	.word	.LANCHOR2-1664
	.word	.LANCHOR2+936
	.word	.LANCHOR2+888
	.word	.LANCHOR0
	.fnend
	.size	FtlSysBlkInit, .-FtlSysBlkInit
	.align	2
	.global	FtlLowFormat
	.type	FtlLowFormat, %function
FtlLowFormat:
	.fnstart
	@ args = 0, pretend = 0, frame = 8
	@ frame_needed = 0, uses_anonymous_args = 0
	stmfd	sp!, {r0, r1, r2, r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.save {r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.pad #12
	ldr	r4, .L3522
	ldr	r5, [r4, #-1276]
	cmp	r5, #0
	bne	.L3489
	sub	r6, r4, #1616
	mov	r1, r5
	ldr	r0, [r4, #-1368]
	ldrh	r2, [r6, #-12]
	mov	r2, r2, asl #2
	bl	ftl_memset
	ldrh	r2, [r6, #-12]
	mov	r1, r5
	ldr	r0, [r4, #-1372]
	mov	r2, r2, asl #2
	bl	ftl_memset
	ldr	r3, [r4, #-1728]
	str	r5, [r4, #-1612]
	str	r5, [r4, #-1608]
	uxth	r0, r3
	bl	FtlFreeSysBlkQueueInit
	bl	FtlLoadBbt
	cmp	r0, #0
	beq	.L3490
	bl	FtlMakeBbt
.L3490:
	ldr	r0, .L3522
	mov	r2, #0
	ldr	ip, .L3522+4
	ldr	lr, .L3522+8
.L3491:
	ldrh	r1, [ip]
	uxth	r3, r2
	add	r2, r2, #1
	cmp	r3, r1, asl #7
	bge	.L3519
	ldr	r5, [r0, #-1452]
	mvn	r1, r3
	orr	r1, r3, r1, asl #16
	str	r1, [r5, r3, asl #2]
	ldr	r1, [r0, #-1448]
	str	lr, [r1, r3, asl #2]
	b	.L3491
.L3519:
	ldr	r8, .L3522+12
	mov	r7, #0
	sub	fp, r8, #10
	ldrh	r6, [r8, #-12]
.L3493:
	ldrh	r3, [fp]
	ldr	r9, .L3522
	cmp	r3, r6
	sub	r10, r9, #1712
	sub	r5, r10, #10
	bls	.L3520
	mov	r0, r6
	mov	r1, #1
	bl	FtlLowFormatEraseBlock
	add	r6, r6, #1
	uxth	r6, r6
	add	r0, r7, r0
	uxth	r7, r0
	b	.L3493
.L3520:
	sub	r6, r9, #1728
	sub	r3, r7, #3
	ldrh	r1, [r6, #-4]
	cmp	r3, r1, asl #1
	bge	.L3495
.L3499:
	mov	r5, #0
	ldr	r9, .L3522+16
	mov	r7, r5
	b	.L3496
.L3495:
	mov	r0, r7
	bl	__aeabi_uidiv
	ldr	r3, [r9, #-1632]
	add	r0, r0, r3
	uxth	r0, r0
	bl	FtlSysBlkNumInit
	ldr	r0, [r9, #-1728]
	uxth	r0, r0
	bl	FtlFreeSysBlkQueueInit
	ldrh	r7, [r10, #-12]
.L3497:
	ldrh	r3, [r5]
	cmp	r3, r7
	bls	.L3499
	mov	r0, r7
	add	r7, r7, #1
	mov	r1, #1
	bl	FtlLowFormatEraseBlock
	uxth	r7, r7
	b	.L3497
.L3496:
	ldrh	r1, [r9]
	uxth	r0, r5
	ldr	fp, .L3522
	add	r5, r5, #1
	cmp	r1, r0
	sub	r2, fp, #1712
	bls	.L3521
	mov	r1, #0
	bl	FtlLowFormatEraseBlock
	add	r0, r7, r0
	uxth	r7, r0
	b	.L3496
.L3521:
	ldrh	r2, [r2, #-10]
	ldrh	r6, [r6, #-4]
	ldr	r5, .L3522+20
	str	r2, [fp, #-1540]
	ldr	r2, [fp, #-1720]
	mov	r1, r6
	mov	r0, r2
	str	r2, [sp, #4]
	bl	__aeabi_uidiv
	movw	r3, #1164
	ubfx	r9, r0, #5, #16
	mov	r10, r0
	add	r1, r9, #36
	strh	r1, [r5, r3]	@ movhi
	mov	r1, #24
	str	r0, [fp, #-1280]
	mul	r1, r1, r6
	ldr	r2, [sp, #4]
	cmp	r7, r1
	ble	.L3501
	rsb	r0, r7, r2
	mov	r1, r6
	str	r3, [sp, #4]
	bl	__aeabi_uidiv
	ldr	r3, [sp, #4]
	str	r0, [fp, #-1280]
	mov	r0, r0, lsr #5
	add	r0, r0, #24
	strh	r0, [r5, r3]	@ movhi
.L3501:
	ldr	r3, [r4, #-1800]
	cmp	r3, #1
	bne	.L3502
	movw	r3, #1164
	mov	r0, r7
	mov	r1, r6
	ldrh	fp, [r5, r3]
	str	r3, [sp, #4]
	bl	__aeabi_uidiv
	ldr	r3, [sp, #4]
	uxtah	r0, fp, r0
	add	fp, fp, r0, asr #2
	strh	fp, [r5, r3]	@ movhi
.L3502:
	ldrb	r3, [r4, #-2744]	@ zero_extendqisi2
	cmp	r3, #0
	beq	.L3503
	movw	r3, #1164
	mov	r0, r7
	mov	r1, r6
	ldrh	fp, [r5, r3]
	str	r3, [sp, #4]
	bl	__aeabi_uidiv
	ldr	r3, [sp, #4]
	uxtah	r0, fp, r0
	add	fp, fp, r0, asr #2
	strh	fp, [r5, r3]	@ movhi
.L3503:
	ldr	ip, .L3522
	sub	r2, ip, #1664
	ldrh	r3, [r2, #-6]
	cmp	r3, #0
	beq	.L3505
	movw	r1, #1164
	ldrh	r0, [r5, r1]
	add	r0, r0, r3, lsr #1
	strh	r0, [r5, r1]	@ movhi
	mul	r0, r6, r3
	cmp	r0, r7
	strgt	r10, [ip, #-1280]
	addgt	r3, r3, #32
	ldrgt	r0, .L3522+20
	addgt	r3, r9, r3
	strgth	r3, [r0, r1]	@ movhi
.L3505:
	movw	r3, #1164
	ldr	r10, .L3522+24
	ldrh	r1, [r5, r3]
	mvn	r9, #0
	ldr	r3, [r4, #-1280]
	ldr	r7, .L3522+28
	rsb	r3, r1, r3
	mul	r6, r6, r3
	ldrh	r3, [r2]
	ldrh	r2, [r10, #-10]
	add	r10, r10, #1648
	mov	fp, r7
	mul	r3, r3, r6
	str	r6, [r5, #1160]
	mov	r6, #0
	str	r3, [r4, #-1280]
	mul	r3, r2, r3
	str	r3, [r4, #-2740]
	bl	FtlBbmTblFlush
	ldrh	r2, [r8, #-10]
	mov	r1, #0
	ldr	r0, [r4, #-1404]
	mov	r2, r2, asl #1
	bl	ftl_memset
	ldrh	r2, [r8, #-12]
	ldr	r3, .L3522+32
	mov	r1, #255
	ldr	r0, [r4, #-1392]
	mov	r2, r2, lsr #3
	str	r6, [r4, #1128]
	strh	r9, [r3]	@ movhi
	strh	r6, [r3, #2]	@ movhi
	mov	r3, #1
	strb	r6, [r5, #1174]
	strb	r6, [r5, #1176]
	strh	r6, [r7, #2]	@ movhi
	strb	r6, [r4, #894]
	strh	r6, [r7]	@ movhi
	strb	r3, [r4, #896]
	bl	ftl_memset
.L3507:
	ldr	r6, .L3522
	add	r8, r6, #888
	mov	r0, r8
	bl	make_superblock
	ldrb	r3, [r4, #895]	@ zero_extendqisi2
	cmp	r3, #0
	ldrh	r3, [r7]
	bne	.L3508
	ldr	r2, [r10, #-1404]
	mov	r3, r3, asl #1
	strh	r9, [r2, r3]	@ movhi
	ldrh	r3, [fp]
	add	r8, r3, #1
	strh	r8, [fp]	@ movhi
	b	.L3507
.L3508:
	ldr	r2, [r6, #-1612]
	mov	r3, r3, asl #1
	ldrh	r1, [r8, #4]
	mvn	fp, #0
	str	r2, [r6, #900]
	add	r2, r2, #1
	str	r2, [r6, #-1612]
	ldr	r2, [r6, #-1404]
	strh	r1, [r2, r3]	@ movhi
	add	r2, r6, #936
	mov	r3, #0
	strb	r3, [r6, #942]
	strh	r3, [r2, #2]	@ movhi
	mov	r10, r2
	ldrh	r3, [r8]
	mov	r8, r2
	add	r3, r3, #1
	strh	r3, [r2]	@ movhi
	mov	r3, #1
	strb	r3, [r6, #944]
.L3509:
	ldr	r7, .L3522
	add	r9, r7, #936
	mov	r0, r9
	bl	make_superblock
	ldrb	r3, [r4, #943]	@ zero_extendqisi2
	cmp	r3, #0
	ldrh	r3, [r8]
	bne	.L3510
	ldr	r2, [r6, #-1404]
	mov	r3, r3, asl #1
	strh	fp, [r2, r3]	@ movhi
	ldrh	r3, [r10]
	add	r3, r3, #1
	strh	r3, [r10]	@ movhi
	b	.L3509
.L3510:
	ldr	r2, [r7, #-1612]
	mov	r3, r3, asl #1
	ldrh	r1, [r9, #4]
	mvn	r4, #0
	str	r2, [r7, #948]
	add	r2, r2, #1
	str	r2, [r7, #-1612]
	ldr	r2, [r7, #-1404]
	strh	r1, [r2, r3]	@ movhi
	add	r3, r7, #984
	strh	r4, [r3]	@ movhi
	bl	FtlFreeSysBlkQueueOut
	ldr	r3, .L3522+36
	movw	r2, #1132
	strh	r4, [r3, #4]	@ movhi
	strh	r0, [r7, r2]	@ movhi
	mov	r2, #0
	strh	r2, [r3, #2]	@ movhi
	ldr	r2, [r5, #1160]
	strh	r2, [r3, #6]	@ movhi
	ldr	r3, [r7, #-1612]
	str	r3, [r7, #1140]
	add	r3, r3, #1
	str	r3, [r7, #-1612]
	bl	FtlVpcTblFlush
	bl	FtlSysBlkInit
	cmp	r0, #0
	ldreq	r3, .L3522+40
	moveq	r2, #1
	streq	r2, [r3, #3348]
.L3489:
	mov	r0, #0
	add	sp, sp, #12
	@ sp needed
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L3523:
	.align	2
.L3522:
	.word	.LANCHOR2
	.word	.LANCHOR2-1658
	.word	168778952
	.word	.LANCHOR2-1712
	.word	.LANCHOR2-1724
	.word	.LANCHOR4
	.word	.LANCHOR2-1648
	.word	.LANCHOR2+888
	.word	.LANCHOR4+1168
	.word	.LANCHOR2+1132
	.word	.LANCHOR1
	.fnend
	.size	FtlLowFormat, .-FtlLowFormat
	.align	2
	.global	FtlReInitForSDUpdata
	.type	FtlReInitForSDUpdata, %function
FtlReInitForSDUpdata:
	.fnstart
	@ args = 0, pretend = 0, frame = 16
	@ frame_needed = 0, uses_anonymous_args = 0
	stmfd	sp!, {r0, r1, r2, r3, r4, lr}
	.save {r4, lr}
	.pad #16
	ldr	r4, .L3553
	ldrb	r3, [r4, #-2744]	@ zero_extendqisi2
	cmp	r3, #0
	beq	.L3525
.L3527:
	mov	r0, #0
	b	.L3526
.L3525:
	ldr	r3, .L3553+4
	ldr	r0, [r3]
	bl	FlashInit
	cmp	r0, #0
	bne	.L3527
	bl	FlashLoadFactorBbt
	cmp	r0, #0
	beq	.L3528
	bl	FlashMakeFactorBbt
.L3528:
	ldr	r0, [r4, #-1772]
	bl	FlashReadIdbDataRaw
	cmp	r0, #0
	beq	.L3529
	mov	r1, #0
	mov	r2, #16
	mov	r0, sp
	bl	FlashReadFacBbtData
	mov	r3, #0
	mov	r2, r3
	mov	ip, #1
	ldr	r1, [sp]
.L3530:
	ands	lr, r1, ip, asl r2
	add	r0, r3, #1
	add	r2, r2, #1
	movne	r3, r0
	cmp	r2, #16
	bne	.L3530
	cmp	r3, #6
	ldrls	r3, .L3553+8
	bls	.L3549
	mov	r2, #0
	mov	ip, #1
.L3533:
	ands	lr, r1, ip, asl r2
	add	r0, r3, #1
	add	r2, r2, #1
	movne	r3, r0
	cmp	r2, #24
	bne	.L3533
	cmp	r3, #17
	ldr	r3, .L3553+8
	movhi	r2, #36
.L3549:
	strb	r2, [r3, #1]
	ldr	r3, .L3553+8
	ldrb	r2, [r3, #1]	@ zero_extendqisi2
	ldr	r3, .L3553+12
	strh	r2, [r3, #26]	@ movhi
.L3529:
	ldr	r1, .L3553+16
	ldr	r0, .L3553+20
	bl	printk
	ldr	r0, .L3553+12
	bl	FtlConstantsInit
	bl	FtlVariablesInit
	ldr	r0, [r4, #-1728]
	mov	r4, #1
	uxth	r0, r0
	bl	FtlFreeSysBlkQueueInit
.L3535:
	bl	FtlLoadBbt
	cmp	r0, #0
	beq	.L3536
.L3551:
	bl	FtlLowFormat
	cmp	r4, #3
	addls	r4, r4, #1
	bls	.L3535
.L3552:
	mvn	r0, #0
	b	.L3526
.L3536:
	bl	FtlSysBlkInit
	cmp	r0, #0
	bne	.L3551
	ldr	r3, .L3553+24
	mov	r2, #1
	str	r2, [r3, #3348]
.L3526:
	add	sp, sp, #16
	@ sp needed
	ldmfd	sp!, {r4, pc}
.L3554:
	.align	2
.L3553:
	.word	.LANCHOR2
	.word	RK29_NANDC_REG_BASE
	.word	.LANCHOR0
	.word	.LANCHOR2-2772
	.word	.LC141
	.word	.LC46
	.word	.LANCHOR1
	.fnend
	.size	FtlReInitForSDUpdata, .-FtlReInitForSDUpdata
	.align	2
	.global	Ftl_gc_temp_data_write_back
	.type	Ftl_gc_temp_data_write_back, %function
Ftl_gc_temp_data_write_back:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	stmfd	sp!, {r4, r5, r6, r7, r8, lr}
	.save {r4, r5, r6, r7, r8, lr}
	ldr	r4, .L3572
	ldr	r3, [r4, #-1276]
	cmp	r3, #0
	beq	.L3556
.L3559:
	mov	r0, #0
	ldmfd	sp!, {r4, r5, r6, r7, r8, pc}
.L3556:
	ldrb	r3, [r4, #-2744]	@ zero_extendqisi2
	ldr	r6, .L3572+4
	cmp	r3, #0
	beq	.L3558
	ldr	r3, [r6, #1744]
	tst	r3, #1
	beq	.L3558
	add	r3, r4, #984
	ldrh	r3, [r3, #4]
	cmp	r3, #0
	bne	.L3559
.L3558:
	mov	r2, #0
	ldr	r0, [r4, #-1496]
	ldr	r1, [r6, #1744]
	mov	r3, r2
	bl	FlashProgPages
	mov	r7, #0
	mov	r8, #36
.L3560:
	ldr	r1, [r6, #1744]
	uxth	r3, r7
	ldr	r5, .L3572+4
	cmp	r3, r1
	bcs	.L3571
	mul	r3, r8, r3
	ldr	r0, [r4, #-1496]
	ldr	r2, .L3572
	add	r7, r7, #1
	add	r1, r0, r3
	ldr	lr, [r0, r3]
	ldr	ip, [r1, #12]
	cmn	lr, #1
	bne	.L3561
	add	r0, r2, #984
	ldr	ip, [r2, #-1404]
	mov	r4, #0
	ldrh	r1, [r0]
	mov	r1, r1, asl #1
	strh	r4, [ip, r1]	@ movhi
	ldr	r2, [r2, #-1496]
	ldr	r1, [r5, #1312]
	add	r3, r2, r3
	strh	lr, [r0]	@ movhi
	add	r1, r1, #1
	str	r1, [r5, #1312]
	ldr	r0, [r3, #4]
	ubfx	r0, r0, #10, #16
	bl	FtlBbmMapBadBlock
	bl	FtlBbmTblFlush
	bl	FtlGcPageVarInit
	b	.L3570
.L3561:
	ldr	r0, [ip, #12]
	ldr	r1, [r1, #4]
	ldr	r2, [ip, #8]
	bl	FtlGcUpdatePage
	b	.L3560
.L3571:
	ldr	r0, [r4, #-1496]
	bl	FtlGcBufFree
	mov	r3, #0
	str	r3, [r5, #1744]
	ldr	r3, .L3572+8
	ldrh	r3, [r3, #4]
	cmp	r3, #0
	bne	.L3559
	mov	r0, #1
	bl	FtlGcFreeTempBlock
.L3570:
	mov	r0, #1
	ldmfd	sp!, {r4, r5, r6, r7, r8, pc}
.L3573:
	.align	2
.L3572:
	.word	.LANCHOR2
	.word	.LANCHOR4
	.word	.LANCHOR2+984
	.fnend
	.size	Ftl_gc_temp_data_write_back, .-Ftl_gc_temp_data_write_back
	.align	2
	.global	Ftl_get_new_temp_ppa
	.type	Ftl_get_new_temp_ppa, %function
Ftl_get_new_temp_ppa:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	ldr	r3, .L3578
	movw	r2, #65535
	stmfd	sp!, {r4, lr}
	.save {r4, lr}
	ldrh	r1, [r3]
	cmp	r1, r2
	beq	.L3575
	ldrh	r3, [r3, #4]
	cmp	r3, #0
	bne	.L3576
.L3575:
	bl	FtlCacheWriteBack
	mov	r0, #0
	bl	FtlGcFreeTempBlock
	ldr	r0, .L3578+4
	mov	r4, #0
	add	r0, r0, #984
	strb	r4, [r0, #8]
	bl	allocate_data_superblock
	ldr	r3, .L3578+8
	movw	r2, #1768
	strh	r4, [r3, r2]	@ movhi
	movw	r2, #1770
	strh	r4, [r3, r2]	@ movhi
	bl	l2p_flush
	mov	r0, r4
	bl	FtlEctTblFlush
	bl	FtlVpcTblFlush
.L3576:
	ldr	r0, .L3578
	ldmfd	sp!, {r4, lr}
	b	get_new_active_ppa
.L3579:
	.align	2
.L3578:
	.word	.LANCHOR2+984
	.word	.LANCHOR2
	.word	.LANCHOR4
	.fnend
	.size	Ftl_get_new_temp_ppa, .-Ftl_get_new_temp_ppa
	.align	2
	.global	ftl_read
	.type	ftl_read, %function
ftl_read:
	.fnstart
	@ args = 0, pretend = 0, frame = 56
	@ frame_needed = 0, uses_anonymous_args = 0
	stmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.save {r4, r5, r6, r7, r8, r9, r10, fp, lr}
	mov	r4, r1
	ldr	r1, .L3627
	.pad #84
	sub	sp, sp, #84
	ldr	r5, [r1, #3348]
	cmn	r5, #1
	moveq	r0, r5
	beq	.L3581
	cmp	r0, #16
	mov	r8, r3
	str	r2, [sp, #36]
	bne	.L3582
	mov	r1, r2
	add	r0, r4, #256
	mov	r2, r3
	bl	FtlVendorPartRead
	b	.L3581
.L3582:
	ldr	r3, [sp, #36]
	ldr	r5, .L3627+4
	add	r3, r4, r3
	str	r3, [sp, #40]
	ldr	r2, [sp, #40]
	ldr	r3, [r5, #-2740]
	cmp	r2, r3
	mvnhi	r0, #0
	bhi	.L3581
	sub	r3, r5, #1648
	mov	r0, r4
	ldrh	r6, [r3, #-10]
	mov	r1, r6
	bl	__aeabi_uidiv
	mov	r1, r6
	ldr	r3, [sp, #40]
	str	r0, [sp, #28]
	sub	r0, r3, #1
	bl	__aeabi_uidiv
	ldr	r2, .L3627+8
	ldr	r3, [sp, #28]
	ldr	r1, [sp, #36]
	rsb	r3, r3, #1
	str	r0, [sp, #32]
	add	r3, r3, r0
	str	r3, [sp, #24]
	ldr	r3, [r2, #1732]
	ldr	r0, [sp, #28]
	add	r3, r1, r3
	str	r3, [r2, #1732]
	ldr	r3, [r5, #-1584]
	ldr	r2, [sp, #24]
	ldr	r1, [sp, #32]
	add	r3, r2, r3
	str	r3, [r5, #-1584]
	bl	FtlCacheMetchLpa
	cmp	r0, #0
	beq	.L3583
	bl	FtlCacheWriteBack
.L3583:
	ldr	r6, [sp, #28]
	mov	r9, #0
	ldr	r5, .L3627+4
	mov	r7, r9
	str	r9, [sp, #48]
	str	r9, [sp, #52]
.L3584:
	ldr	r3, [sp, #24]
	cmp	r3, #0
	beq	.L3626
	mov	r0, r6
	add	r1, sp, #76
	mov	r2, #0
	bl	log2phys
	ldr	r3, [sp, #76]
	cmn	r3, #1
	bne	.L3622
	mov	r10, #0
.L3585:
	ldr	r3, .L3627+12
	ldrh	r0, [r3]
	cmp	r10, r0
	bcs	.L3589
	mla	r0, r0, r6, r10
	ldr	r2, [sp, #40]
	cmp	r0, r4
	movcs	r3, #1
	movcc	r3, #0
	cmp	r0, r2
	movcs	r3, #0
	cmp	r3, #0
	beq	.L3587
	rsb	r0, r4, r0
	mov	r1, #0
	mov	r2, #512
	add	r0, r8, r0, asl #9
	bl	ftl_memset
.L3587:
	add	r10, r10, #1
	b	.L3585
.L3622:
	ldr	r2, [r5, #-1500]
	mov	r10, #36
	mla	r10, r10, r7, r2
	str	r3, [r10, #4]
	ldr	r3, [sp, #28]
	cmp	r6, r3
	bne	.L3590
	ldr	r3, [r5, #-1452]
	mov	r0, r4
	str	r3, [r10, #8]
	ldr	r3, .L3627+12
	ldrh	fp, [r3]
	mov	r1, fp
	bl	__aeabi_uidivmod
	ldr	r2, [sp, #36]
	rsb	r3, r1, fp
	str	r1, [sp, #56]
	cmp	r3, r2
	movcs	r3, r2
	cmp	r3, fp
	str	r3, [sp, #48]
	streq	r8, [r10, #8]
	b	.L3591
.L3590:
	ldr	r3, [sp, #32]
	cmp	r6, r3
	bne	.L3592
	ldr	r3, [r5, #-1448]
	ldr	r1, [sp, #40]
	str	r3, [r10, #8]
	ldr	r3, .L3627+12
	ldrh	r2, [r3]
	mul	r3, r2, r6
	rsb	r9, r3, r1
	cmp	r9, r2
	bne	.L3591
	b	.L3624
.L3592:
	ldr	r3, .L3627+12
	ldrh	r3, [r3]
	mul	r3, r3, r6
.L3624:
	rsb	r3, r4, r3
	add	r3, r8, r3, asl #9
	str	r3, [r10, #8]
.L3591:
	ldr	r3, .L3627+16
	ldr	r2, [r5, #-1436]
	str	r6, [r10, #16]
	ldrh	r3, [r3]
	mul	r3, r3, r7
	add	r7, r7, #1
	bic	r3, r3, #3
	add	r3, r2, r3
	str	r3, [r10, #12]
.L3589:
	ldr	r3, [sp, #24]
	add	r6, r6, #1
	subs	r3, r3, #1
	str	r3, [sp, #24]
	beq	.L3593
	ldr	r3, .L3627+20
	ldrh	r3, [r3]
	cmp	r7, r3, asl #3
	bne	.L3584
.L3593:
	cmp	r7, #0
	beq	.L3584
	ldr	r0, [r5, #-1500]
	mov	r1, r7
	mov	r2, #0
	ldr	fp, .L3627+8
	bl	FlashReadPages
	ldr	r3, [sp, #56]
	mov	r3, r3, asl #9
	str	r3, [sp, #60]
	ldr	r3, [sp, #48]
	mov	r3, r3, asl #9
	str	r3, [sp, #64]
	mov	r3, r9, asl #9
	str	r3, [sp, #68]
	mov	r3, #0
	str	r3, [sp, #44]
.L3600:
	ldr	r3, [sp, #44]
	mov	ip, #36
	ldr	r1, [sp, #28]
	mul	r10, ip, r3
	ldr	r3, [r5, #-1500]
	add	r3, r3, r10
	ldr	r2, [r3, #16]
	cmp	r2, r1
	bne	.L3595
	ldr	r1, [r3, #8]
	ldr	r3, [r5, #-1452]
	cmp	r1, r3
	bne	.L3596
	ldr	r3, [sp, #60]
	mov	r0, r8
	ldr	r2, [sp, #64]
	add	r1, r1, r3
	b	.L3625
.L3595:
	ldr	r1, [sp, #32]
	cmp	r2, r1
	bne	.L3596
	ldr	r1, [r3, #8]
	ldr	r3, [r5, #-1448]
	cmp	r1, r3
	bne	.L3596
	ldr	r3, .L3627+12
	ldr	r2, [sp, #68]
	ldrh	r0, [r3]
	ldr	r3, [sp, #32]
	mul	r0, r0, r3
	rsb	r0, r4, r0
	add	r0, r8, r0, asl #9
.L3625:
	bl	ftl_memcpy
.L3596:
	ldr	r2, [r5, #-1500]
	add	r3, r2, r10
	ldr	r1, [r2, r10]
	cmn	r1, #1
	streq	r1, [sp, #52]
	ldreq	r2, [fp, #1288]
	addeq	r2, r2, #1
	streq	r2, [fp, #1288]
	ldr	r2, [r3, #12]
	ldr	r1, [r3, #16]
	ldr	r2, [r2, #8]
	cmp	r1, r2
	beq	.L3598
	ldr	r2, [fp, #1288]
	ldr	r0, .L3627+24
	add	r2, r2, #1
	str	r2, [fp, #1288]
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
.L3598:
	ldr	r3, [r5, #-1500]
	add	r2, r3, r10
	ldr	r3, [r3, r10]
	cmp	r3, #256
	bne	.L3599
	ldr	r0, [r2, #4]
	ubfx	r0, r0, #10, #16
	bl	P2V_block_in_plane
	bl	FtlGcRefreshBlock
.L3599:
	ldr	r3, [sp, #44]
	add	r3, r3, #1
	str	r3, [sp, #44]
	cmp	r3, r7
	bne	.L3600
	mov	r7, #0
	b	.L3584
.L3626:
	ldr	r3, .L3627+28
	ldrh	r3, [r3, #-2]
	cmp	r3, #0
	beq	.L3602
	ldr	r0, [sp, #24]
	mov	r1, #1
	bl	ftl_do_gc
.L3602:
	ldr	r0, [sp, #52]
.L3581:
	add	sp, sp, #84
	@ sp needed
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L3628:
	.align	2
.L3627:
	.word	.LANCHOR1
	.word	.LANCHOR2
	.word	.LANCHOR4
	.word	.LANCHOR2-1658
	.word	.LANCHOR2-1652
	.word	.LANCHOR2-1732
	.word	.LC55
	.word	.LANCHOR2-1520
	.fnend
	.size	ftl_read, .-ftl_read
	.align	2
	.global	ftl_vendor_read
	.type	ftl_vendor_read, %function
ftl_vendor_read:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	str	lr, [sp, #-4]!
	.save {lr}
	mov	ip, r1
	mov	lr, r0
	mov	r3, r2
	mov	r1, lr
	mov	r0, #16
	mov	r2, ip
	ldr	lr, [sp], #4
	b	ftl_read
	.fnend
	.size	ftl_vendor_read, .-ftl_vendor_read
	.align	2
	.global	ftl_sys_read
	.type	ftl_sys_read, %function
ftl_sys_read:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	@ link register save eliminated.
	mov	ip, r1
	mov	r3, r2
	add	r1, r0, #256
	mov	r2, ip
	mov	r0, #16
	b	ftl_read
	.fnend
	.size	ftl_sys_read, .-ftl_sys_read
	.align	2
	.global	FtlInit
	.type	FtlInit, %function
FtlInit:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	stmfd	sp!, {r3, r4, r5, r6, r7, r8, r9, lr}
	.save {r3, r4, r5, r6, r7, r8, r9, lr}
	mvn	r3, #0
	ldr	r4, .L3649
	ldr	r6, .L3649+4
	ldr	r8, .L3649+8
	ldr	r1, .L3649+12
	ldr	r0, .L3649+16
	str	r3, [r6, #3348]
	mov	r3, #0
	str	r3, [r8, #1960]
	str	r3, [r4, #-1276]
	bl	printk
	ldr	r0, .L3649+20
	bl	FtlConstantsInit
	bl	FtlMemInit
	bl	FtlVariablesInit
	ldr	r3, [r4, #-1728]
	uxth	r0, r3
	bl	FtlFreeSysBlkQueueInit
	bl	FtlLoadBbt
	cmp	r0, #0
	ldrne	r0, .L3649+24
	bne	.L3648
	bl	FtlSysBlkInit
	subs	r5, r0, #0
	beq	.L3635
	ldr	r0, .L3649+28
.L3648:
	ldr	r1, .L3649+32
	bl	printk
	b	.L3634
.L3635:
	mov	r1, #1
	str	r1, [r6, #3348]
	bl	ftl_do_gc
	add	r3, r4, #884
	ldrh	r9, [r3]
	mov	r7, r3
	cmp	r9, #15
	bhi	.L3636
	add	r8, r8, #1168
	sub	r4, r4, #1536
	movw	r6, #65535
.L3639:
	ldrh	r3, [r8]
	cmp	r3, r6
	bne	.L3637
	ldrh	r3, [r4]
	cmp	r3, r6
	bne	.L3637
	and	r0, r5, #63
	bl	List_get_gc_head_node
	uxth	r0, r0
	bl	FtlGcRefreshBlock
.L3637:
	mov	r0, #1
	mov	r1, r0
	bl	ftl_do_gc
	mov	r0, #0
	mov	r1, #1
	bl	ftl_do_gc
	ldrh	r2, [r7]
	add	r3, r9, #2
	cmp	r2, r3
	bhi	.L3634
	add	r5, r5, #1
	cmp	r5, #4096
	bne	.L3639
	b	.L3634
.L3636:
	ldrb	r3, [r4, #-2744]	@ zero_extendqisi2
	cmp	r3, #0
	beq	.L3634
	mov	r4, #128
.L3641:
	mov	r0, #1
	mov	r1, r0
	bl	ftl_do_gc
	subs	r4, r4, #1
	bne	.L3641
.L3634:
	mov	r0, #0
	ldmfd	sp!, {r3, r4, r5, r6, r7, r8, r9, pc}
.L3650:
	.align	2
.L3649:
	.word	.LANCHOR2
	.word	.LANCHOR1
	.word	.LANCHOR4
	.word	.LC141
	.word	.LC46
	.word	.LANCHOR2-2772
	.word	.LC158
	.word	.LC159
	.word	.LANCHOR3+108
	.fnend
	.size	FtlInit, .-FtlInit
	.align	2
	.global	ftl_write
	.type	ftl_write, %function
ftl_write:
	.fnstart
	@ args = 0, pretend = 0, frame = 96
	@ frame_needed = 0, uses_anonymous_args = 0
	stmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.save {r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.pad #100
	sub	sp, sp, #100
	ldr	r10, .L3724
	str	r3, [sp, #8]
	ldr	r3, [r10, #-1276]
	cmp	r3, #0
	bne	.L3692
	mov	r8, r2
	ldr	r2, .L3724+4
	ldr	r2, [r2, #3348]
	cmn	r2, #1
	moveq	r0, r3
	beq	.L3652
	cmp	r0, #16
	mov	r7, r1
	bne	.L3653
	add	r0, r1, #256
	ldr	r2, [sp, #8]
	mov	r1, r8
	bl	FtlVendorPartWrite
	b	.L3652
.L3653:
	ldr	r3, [r10, #-2740]
	add	r5, r1, r8
	cmp	r5, r3
	mvnhi	r0, #0
	bhi	.L3652
	ldr	r6, .L3724+8
	mov	r3, #2048
	mov	r0, r1
	str	r3, [r6, #1964]
	sub	r3, r10, #1648
	ldrh	r4, [r3, #-10]
	mov	r1, r4
	bl	__aeabi_uidiv
	mov	r1, r4
	str	r0, [sp, #12]
	sub	r0, r5, #1
	bl	__aeabi_uidiv
	cmp	r8, r4, asl #1
	ldr	r2, [sp, #12]
	str	r0, [sp, #28]
	rsb	r5, r2, r0
	add	r3, r5, #1
	str	r3, [sp, #4]
	ldr	r2, [sp, #4]
	ldr	r3, [r10, #-1600]
	add	r3, r2, r3
	ldr	r2, [r10, #-1512]
	str	r3, [r10, #-1600]
	ldr	r3, [r6, #1728]
	add	r3, r8, r3
	str	r3, [r6, #1728]
	movcs	r3, #1
	movcc	r3, #0
	cmp	r2, #0
	str	r3, [sp, #24]
	beq	.L3655
	mov	r3, #36
	ldr	r9, [r10, #-1480]
	mul	r3, r3, r2
	ldr	r2, [sp, #12]
	sub	r3, r3, #36
	add	r9, r9, r3
	ldr	r3, [r9, #16]
	cmp	r2, r3
	bne	.L3656
	ldr	r3, [r10, #-1592]
	mov	r1, r4
	mov	r0, r7
	add	r3, r3, #1
	str	r3, [r10, #-1592]
	ldr	r3, [r6, #1968]
	add	r3, r3, #1
	str	r3, [r6, #1968]
	bl	__aeabi_uidivmod
	ldr	r0, [r9, #8]
	rsb	r4, r1, r4
	add	r0, r0, r1, asl #9
	cmp	r4, r8
	ldr	r1, [sp, #8]
	movcs	r4, r8
	mov	r10, r4, asl #9
	mov	r2, r10
	bl	ftl_memcpy
	cmp	r5, #0
	bne	.L3657
	ldr	r3, [r6, #1968]
	cmp	r3, #2
	ble	.L3692
.L3657:
	ldr	r3, [sp, #8]
	rsb	r8, r4, r8
	add	r7, r7, r4
	str	r5, [sp, #4]
	add	r3, r3, r10
	str	r3, [sp, #8]
	ldr	r3, [sp, #12]
	add	r3, r3, #1
	str	r3, [sp, #12]
.L3656:
	mov	r3, #0
	str	r3, [r6, #1968]
.L3655:
	ldr	r0, [sp, #12]
	ldr	r1, [sp, #28]
	bl	FtlCacheMetchLpa
	cmp	r0, #0
	beq	.L3658
	bl	FtlCacheWriteBack
.L3658:
	ldr	r5, .L3724+12
	mov	r3, #0
	str	r3, [sp, #16]
	sub	r4, r5, #888
	str	r3, [sp, #32]
	str	r5, [r6, #1952]
	ldr	r6, [sp, #12]
.L3659:
	ldr	r3, [sp, #4]
	cmp	r3, #0
	beq	.L3723
	ldrh	r2, [r5, #4]
	cmp	r2, #0
	bne	.L3660
	ldr	r3, .L3724+12
	ldr	r9, .L3724+4
	cmp	r5, r3
	bne	.L3661
	add	r0, r5, #48
	ldrh	r10, [r0, #4]
	cmp	r10, #0
	bne	.L3662
	bl	allocate_new_data_superblock
	str	r10, [r9, #3356]
.L3662:
	ldr	r0, .L3724+12
	bl	allocate_new_data_superblock
	ldr	r3, [r9, #3356]
	cmp	r3, #0
	ldrne	r5, .L3724+16
	bne	.L3663
.L3664:
	ldr	r5, .L3724+12
	b	.L3663
.L3661:
	ldrh	r3, [r3, #4]
	str	r2, [r9, #3356]
	cmp	r3, #0
	bne	.L3664
	mov	r0, r5
	bl	allocate_new_data_superblock
.L3663:
	ldrh	r3, [r5, #4]
	cmp	r3, #0
	bne	.L3665
	mov	r0, r5
	bl	allocate_new_data_superblock
.L3665:
	ldr	r3, .L3724+8
	str	r5, [r3, #1952]
.L3660:
	ldr	r3, [r4, #-1516]
	ldr	r1, [r4, #-1512]
	ldrh	r2, [r5, #4]
	rsb	r3, r1, r3
	cmp	r2, r3
	movcs	r2, r3
	ldr	r3, [sp, #4]
	cmp	r2, r3
	movcc	r3, r2
	str	r3, [sp, #44]
	mov	r3, #0
.L3721:
	str	r3, [sp, #20]
	ldr	r3, [sp, #20]
	ldr	r2, [sp, #44]
	cmp	r3, r2
	beq	.L3667
	ldrh	r3, [r5, #4]
	cmp	r3, #0
	beq	.L3667
	ldr	r3, [sp, #28]
	ldr	r2, [sp, #20]
	rsb	ip, r3, r6
	ldr	r3, [sp, #24]
	clz	ip, ip
	mov	ip, ip, lsr #5
	and	r3, ip, r3
	cmp	r2, #0
	moveq	r3, #0
	andne	r3, r3, #1
	cmp	r3, #0
	beq	.L3668
	ldr	r3, .L3724+20
	ldrh	r2, [r3]
	add	r3, r8, r7
	mls	r3, r2, r6, r3
	cmp	r3, r2
	bne	.L3667
.L3668:
	add	r1, sp, #56
	mov	r2, #0
	mov	r0, r6
	str	ip, [sp, #52]
	bl	log2phys
	mov	r0, r5
	bl	get_new_active_ppa
	ldr	r10, [r4, #-1512]
	ldr	r1, [r4, #-1480]
	mov	r3, #36
	ldr	fp, .L3724+24
	mla	r1, r3, r10, r1
	ldrh	r2, [fp]
	str	r6, [r1, #16]
	str	r0, [r1, #4]
	mul	r0, r10, r2
	str	r3, [sp, #48]
	bic	r3, r0, #3
	str	r3, [sp, #36]
	ldr	r0, [sp, #36]
	ldr	r3, [r4, #-1428]
	add	r9, r3, r0
	ldrh	r0, [fp, #-2]
	str	r9, [r1, #12]
	str	r3, [sp, #40]
	mul	r10, r10, r0
	ldr	r0, [r4, #-1456]
	bic	r10, r10, #3
	add	r10, r0, r10
	mov	r0, r9
	str	r10, [r1, #8]
	mov	r1, #0
	bl	ftl_memset
	ldr	r3, [sp, #12]
	ldr	ip, [sp, #52]
	rsb	r10, r3, r6
	clz	r10, r10
	mov	r10, r10, lsr #5
	orrs	r3, r10, ip
	ldr	r3, [sp, #48]
	beq	.L3669
	cmp	r10, #0
	beq	.L3670
	ldrh	fp, [fp, #-6]
	mov	r0, r7
	mov	r1, fp
	bl	__aeabi_uidivmod
	rsb	r2, r1, fp
	mov	r3, r1
	cmp	r2, r8
	str	r1, [sp, #32]
	movcc	r3, r2
	movcs	r3, r8
	str	r3, [sp, #16]
	b	.L3671
.L3670:
	cmp	ip, #0
	beq	.L3671
	ldr	r3, .L3724+20
	add	r2, r8, r7
	str	r10, [sp, #32]
	ldrh	r1, [r3]
	smulbb	r1, r1, r6
	rsb	r2, r1, r2
	uxth	r3, r2
	str	r3, [sp, #16]
.L3671:
	ldr	r3, .L3724+20
	ldr	r2, [sp, #16]
	ldrh	r3, [r3]
	cmp	r2, r3
	bne	.L3672
	cmp	r10, #0
	ldr	r0, .L3724
	moveq	r3, r2
	ldr	r2, .L3724
	muleq	r1, r6, r3
	ldreq	r3, [sp, #8]
	ldr	r2, [r2, #-1512]
	ldr	r0, [r0, #-1480]
	rsbeq	r1, r7, r1
	ldrne	r1, [sp, #8]
	addeq	r1, r3, r1, asl #9
	ldr	r3, [sp, #24]
	cmp	r3, #0
	mov	r3, #36
	mla	r3, r3, r2, r0
	strne	r1, [r3, #8]
	bne	.L3675
	ldr	r0, [r3, #8]
	ldr	r3, .L3724+28
	ldrh	r2, [r3]
	b	.L3719
.L3672:
	ldr	r2, [sp, #56]
	mov	r3, #36
	cmn	r2, #1
	beq	.L3676
	ldr	r1, [r4, #-1480]
	add	r0, sp, #60
	str	r2, [sp, #64]
	ldr	r2, [r4, #-1512]
	str	r6, [sp, #76]
	mla	r3, r3, r2, r1
	mov	r1, #1
	ldr	r2, [r3, #8]
	ldr	r3, [r3, #12]
	str	r2, [sp, #68]
	mov	r2, #0
	str	r3, [sp, #72]
	bl	FlashReadPages
	ldr	r3, [sp, #60]
	cmn	r3, #1
	ldreq	r2, .L3724+8
	ldreq	r3, [r2, #1288]
	addeq	r3, r3, #1
	streq	r3, [r2, #1288]
	beq	.L3679
.L3677:
	ldr	r3, [r9, #8]
	cmp	r3, r6
	beq	.L3679
	ldr	r2, .L3724+8
	ldr	r0, .L3724+32
	ldr	r3, [r2, #1288]
	add	r3, r3, #1
	str	r3, [r2, #1288]
	mov	r2, r6
	ldr	r1, [r9, #8]
	bl	printk
	b	.L3679
.L3676:
	ldr	r2, [r4, #-1512]
	ldr	r1, [r4, #-1480]
	mla	r3, r3, r2, r1
	mov	r1, #0
	ldr	r0, [r3, #8]
	ldr	r3, .L3724+28
	ldrh	r2, [r3]
	bl	ftl_memset
.L3679:
	cmp	r10, #0
	mov	r3, #36
	beq	.L3680
	ldr	r1, [r4, #-1480]
	ldr	r2, [r4, #-1512]
	mla	r3, r3, r2, r1
	ldr	r1, [sp, #8]
	ldr	r0, [r3, #8]
	ldr	r3, [sp, #32]
	add	r0, r0, r3, asl #9
	b	.L3722
.L3680:
	ldr	r1, [r4, #-1512]
	ldr	r2, [r4, #-1480]
	mla	r3, r3, r1, r2
	ldr	r2, .L3724+20
	ldrh	r1, [r2]
	ldr	r0, [r3, #8]
	mul	r1, r1, r6
	ldr	r3, [sp, #8]
	rsb	r1, r7, r1
	add	r1, r3, r1, asl #9
.L3722:
	ldr	r3, [sp, #16]
	mov	r2, r3, asl #9
	b	.L3719
.L3669:
	ldr	r2, [sp, #24]
	cmp	r2, #0
	ldr	r2, [r4, #-1512]
	beq	.L3681
	ldr	r1, [r4, #-1480]
	mla	r3, r3, r2, r1
	ldr	r2, .L3724+20
	ldrh	fp, [r2]
	ldr	r2, [sp, #8]
	mul	fp, fp, r6
	rsb	fp, r7, fp
	add	fp, r2, fp, asl #9
	str	fp, [r3, #8]
	b	.L3675
.L3681:
	ldr	r0, [r4, #-1480]
	mla	r3, r3, r2, r0
	ldr	r2, .L3724+20
	ldrh	r1, [r2]
	ldrh	r2, [fp, #-2]
	ldr	r0, [r3, #8]
	mul	r1, r1, r6
	ldr	r3, [sp, #8]
	rsb	r1, r7, r1
	add	r1, r3, r1, asl #9
.L3719:
	bl	ftl_memcpy
.L3675:
	ldr	r3, .L3724+36
	ldr	r2, [sp, #40]
	ldr	r1, [sp, #36]
	strh	r3, [r2, r1]	@ movhi
	ldr	r3, [r4, #-1608]
	str	r6, [r9, #8]
	add	r6, r6, #1
	str	r3, [r9, #4]
	add	r3, r3, #1
	cmn	r3, #1
	moveq	r3, #0
	str	r3, [r4, #-1608]
	ldr	r3, [sp, #56]
	str	r3, [r9, #12]
	ldrh	r3, [r5]
	strh	r3, [r9, #2]	@ movhi
	ldr	r3, [r4, #-1512]
	add	r3, r3, #1
	str	r3, [r4, #-1512]
	ldr	r3, [sp, #20]
	add	r3, r3, #1
	b	.L3721
.L3667:
	ldr	r3, [sp, #4]
	ldr	r2, [sp, #20]
	ldr	r1, [sp, #24]
	rsb	r3, r2, r3
	ldr	r2, [r4, #-1516]
	str	r3, [sp, #4]
	ldr	r3, [r4, #-1512]
	cmp	r3, r2
	orrcs	r1, r1, #1
	cmp	r1, #0
	bne	.L3685
	ldrh	r3, [r5, #4]
	cmp	r3, #0
	beq	.L3685
.L3687:
	mov	r3, #0
	str	r3, [sp, #24]
	b	.L3659
.L3685:
	bl	FtlCacheWriteBack
	ldr	r2, .L3724
	mov	r3, #0
	str	r3, [r2, #-1512]
	ldr	r3, [sp, #4]
	cmp	r3, #1
	bhi	.L3659
	b	.L3687
.L3723:
	mov	r0, r3
	ldr	r2, [sp, #12]
	ldr	r3, [sp, #28]
	ldr	r5, .L3724+40
	rsb	r1, r2, r3
	bl	ftl_do_gc
	ldrh	r4, [r5]
	cmp	r4, #5
	bls	.L3697
	cmp	r4, #31
	bhi	.L3692
	ldr	r3, .L3724+44
	ldrb	r3, [r3]	@ zero_extendqisi2
	cmp	r3, #0
	bne	.L3692
.L3697:
	ldr	r6, .L3724
	ldr	r4, [sp, #4]
	ldr	r7, .L3724+48
	sub	r8, r6, #1536
.L3710:
	ldrh	r2, [r7]
	movw	r3, #65535
	cmp	r2, r3
	bne	.L3691
	ldrh	r3, [r8]
	cmp	r3, r2
	bne	.L3691
	ldr	r2, .L3724+52
	ldrh	r2, [r2]
	cmp	r2, r3
	bne	.L3691
	and	r0, r4, #7
	bl	List_get_gc_head_node
	uxth	r0, r0
	bl	FtlGcRefreshBlock
.L3691:
	ldr	r3, .L3724+56
	mov	r0, #1
	mov	r1, r0
	mov	r2, #128
	strh	r2, [r3]	@ movhi
	strh	r2, [r3, #-2]	@ movhi
	bl	ftl_do_gc
	mov	r0, #0
	mov	r1, #1
	bl	ftl_do_gc
	ldr	r3, [r6, #-1276]
	cmp	r3, #0
	bne	.L3692
	ldrh	r3, [r5]
	cmp	r3, #2
	bhi	.L3692
	add	r4, r4, #1
	cmp	r4, #256
	bne	.L3710
.L3692:
	mov	r0, #0
.L3652:
	add	sp, sp, #100
	@ sp needed
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L3725:
	.align	2
.L3724:
	.word	.LANCHOR2
	.word	.LANCHOR1
	.word	.LANCHOR4
	.word	.LANCHOR2+888
	.word	.LANCHOR2+936
	.word	.LANCHOR2-1658
	.word	.LANCHOR2-1652
	.word	.LANCHOR2-1654
	.word	.LC160
	.word	-3947
	.word	.LANCHOR2+884
	.word	.LANCHOR0
	.word	.LANCHOR4+1168
	.word	.LANCHOR2-1534
	.word	.LANCHOR2-1526
	.fnend
	.size	ftl_write, .-ftl_write
	.align	2
	.global	ftl_vendor_write
	.type	ftl_vendor_write, %function
ftl_vendor_write:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	str	lr, [sp, #-4]!
	.save {lr}
	mov	ip, r1
	mov	lr, r0
	mov	r3, r2
	mov	r1, lr
	mov	r0, #16
	mov	r2, ip
	ldr	lr, [sp], #4
	b	ftl_write
	.fnend
	.size	ftl_vendor_write, .-ftl_vendor_write
	.align	2
	.global	ftl_sys_write
	.type	ftl_sys_write, %function
ftl_sys_write:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	@ link register save eliminated.
	mov	ip, r1
	mov	r3, r2
	add	r1, r0, #256
	mov	r2, ip
	mov	r0, #16
	b	ftl_write
	.fnend
	.size	ftl_sys_write, .-ftl_sys_write
	.align	2
	.global	ftl_fix_nand_power_lost_error
	.type	ftl_fix_nand_power_lost_error, %function
ftl_fix_nand_power_lost_error:
	.fnstart
	@ args = 0, pretend = 0, frame = 48
	@ frame_needed = 0, uses_anonymous_args = 0
	stmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.save {r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.pad #52
	sub	sp, sp, #52
	ldr	r4, .L3745
	ldrb	r3, [r4, #-2744]	@ zero_extendqisi2
	cmp	r3, #0
	beq	.L3729
	ldr	r8, .L3745+4
	movw	r3, #1806
	add	r9, r4, #888
	add	r5, r4, #936
	ldr	r0, .L3745+8
	ldrh	r7, [r8, r3]
	ldr	r3, [r4, #-1404]
	mov	r6, r7, asl #1
	mov	r1, r7
	ldrh	r2, [r3, r6]
	bl	printk
	ldrh	r0, [r9]
	bl	FtlGcRefreshOpenBlock
	ldrh	r0, [r5]
	bl	FtlGcRefreshOpenBlock
	mov	r0, r9
	bl	allocate_new_data_superblock
	mov	r0, r5
	bl	allocate_new_data_superblock
	movw	r5, #4097
.L3731:
	subs	r5, r5, #1
	beq	.L3735
	mov	r0, #1
	mov	r1, r0
	bl	ftl_do_gc
	ldr	r3, [r4, #-1404]
	ldrh	r3, [r3, r6]
	cmp	r3, #0
	bne	.L3731
.L3735:
	ldr	r3, [r4, #-1404]
	mov	r1, r7
	ldr	r0, .L3745+8
	ldr	r9, .L3745
	ldrh	r2, [r3, r6]
	bl	printk
	ldr	r3, [r4, #-1404]
	ldrh	r5, [r3, r6]
	cmp	r5, #0
	bne	.L3733
	add	r0, sp, #48
	movw	r10, #65535
	mov	fp, #36
	strh	r7, [r0, #-48]!	@ movhi
	bl	make_superblock
	sub	r3, r9, #1728
	ldr	r9, [r9, #-1488]
	ldrh	lr, [r3, #-4]
	mov	r3, r5
	mov	ip, r3
	add	r0, sp, #14
.L3736:
	uxth	r2, r3
	cmp	r2, lr
	bcs	.L3744
	ldrh	r2, [r0, #2]!
	add	r3, r3, #1
	cmp	r2, r10
	movne	r2, r2, asl #10
	mlane	r1, fp, r5, r9
	addne	r5, r5, #1
	uxthne	r5, r5
	stmneib	r1, {r2, ip}
	strne	ip, [r1, #12]
	b	.L3736
.L3744:
	ldr	r3, [r4, #-1404]
	mov	r1, r7
	ldr	r0, .L3745+12
	ldrh	r2, [r3, r6]
	bl	printk
	mov	r1, #0
	mov	r2, r5
	ldr	r0, [r4, #-1488]
	bl	FlashEraseBlocks
	ldr	r0, [r4, #-1488]
	mov	r1, #1
	mov	r2, r5
	bl	FlashEraseBlocks
.L3733:
	movw	r3, #1806
	mvn	r2, #0
	strh	r2, [r8, r3]	@ movhi
.L3729:
	add	sp, sp, #52
	@ sp needed
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L3746:
	.align	2
.L3745:
	.word	.LANCHOR2
	.word	.LANCHOR4
	.word	.LC161
	.word	.LC162
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
	.align	2
.LANCHOR3 = . + 0
.LC0:
	.byte	60
	.byte	40
	.byte	24
	.byte	16
	.type	__func__.20261, %object
	.size	__func__.20261, 11
__func__.20261:
	.ascii	"FtlMemInit\000"
	.space	1
	.type	__func__.21004, %object
	.size	__func__.21004, 12
__func__.21004:
	.ascii	"FtlCheckVpc\000"
	.type	__func__.21036, %object
	.size	__func__.21036, 17
__func__.21036:
	.ascii	"FtlDumpBlockInfo\000"
	.space	3
	.type	__func__.21055, %object
	.size	__func__.21055, 16
__func__.21055:
	.ascii	"FtlScanAllBlock\000"
	.type	__func__.21321, %object
	.size	__func__.21321, 17
__func__.21321:
	.ascii	"ftl_scan_all_ppa\000"
	.space	3
	.type	__func__.21303, %object
	.size	__func__.21303, 21
__func__.21303:
	.ascii	"FtlVpcCheckAndModify\000"
	.space	3
	.type	__func__.20334, %object
	.size	__func__.20334, 8
__func__.20334:
	.ascii	"FtlInit\000"
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
	.ascii	"ECC:%d\012\000"
.LC13:
	.ascii	"No.%d FLASH ID:%x %x %x %x %x %x\012\000"
.LC14:
	.ascii	"FlashLoadPhyInfo fail %x!!\012\000"
.LC15:
	.ascii	"Read pageadd=%x  ecc=%x err=%x\012\000"
.LC16:
	.ascii	"data:\000"
.LC17:
	.ascii	"spare:\000"
.LC18:
	.ascii	"ReadRetry pageadd=%x ecc=%x err=%x\012\000"
.LC19:
	.ascii	"FLFB:%d %d\012\000"
.LC20:
	.ascii	"BBT:\000"
.LC21:
	.ascii	"prog error: = %x\012\000"
.LC22:
	.ascii	"prog read error: = %x\012\000"
.LC23:
	.ascii	"prog read s error: = %x %x %x\012\000"
.LC24:
	.ascii	"prog read d error: = %x %x %x\012\000"
.LC25:
	.ascii	"FlashMakeFactorBbt %d\012\000"
.LC26:
	.ascii	"bad block:%d %d\012\000"
.LC27:
	.ascii	"FMFB:%d %d\012\000"
.LC28:
	.ascii	"E:bad block:%d\012\000"
.LC29:
	.ascii	"FMFB:Save %d %d\012\000"
.LC30:
	.ascii	"%s error allocating memory. return -1\012\000"
.LC31:
	.ascii	"phyBlk = 0x%x die = %d block_in_die = 0x%x 0x%8x\012"
	.ascii	"\000"
.LC32:
	.ascii	"FtlBbmTblFlush id=%x,page=%x,previd=%x cnt=%d\012\000"
.LC33:
	.ascii	"FtlBbmTblFlush error:%x\012\000"
.LC34:
	.ascii	"FtlBbmTblFlush error = %x error count = %d\012\000"
.LC35:
	.ascii	"FtlFreeSysBlkQueueOut free count = %d\012\000"
.LC36:
	.ascii	"FtlFreeSysBlkQueueOut = %x, free count = %d, error\012"
	.ascii	"\000"
.LC37:
	.ascii	"FtlFreeSysBlkQueueOut = %x, free count = %d\012\000"
.LC38:
	.ascii	"FtlMapWritePage error = %x\012\000"
.LC39:
	.ascii	"FtlMapWritePage error = %x error count = %d\012\000"
.LC40:
	.ascii	"page map lost: %x %x\012\000"
.LC41:
	.ascii	"region_id = %x phyAddr = %x\012\000"
.LC42:
	.ascii	"map_ppn:\000"
.LC43:
	.ascii	"load_l2p_region refresh = %x phyAddr = %x\012\000"
.LC44:
	.ascii	"FtlVendorPartRead refresh = %x phyAddr = %x\012\000"
.LC45:
	.ascii	"FtlVpcTblFlush error = %x error count = %d\012\000"
.LC46:
	.ascii	"%s\012\000"
.LC47:
	.ascii	"no ect\000"
.LC48:
	.ascii	"...%s enter...\012\000"
.LC49:
	.ascii	"FtlCheckVpc2 %x = %x  %x\012\000"
.LC50:
	.ascii	"free blk vpc error %x = %x  %x\012\000"
.LC51:
	.ascii	"id = %x,%x addr= %x,spare= %x %x %x %x data = %x\012"
	.ascii	"\000"
.LC52:
	.ascii	":\000"
.LC53:
	.ascii	"Ftlscanalldata = %x\012\000"
.LC54:
	.ascii	"scan lpa = %x ppa= %x\012\000"
.LC55:
	.ascii	"lba = %x,addr= %x,spare= %x %x %x %x data=%x %x\012"
	.ascii	"\000"
.LC56:
	.ascii	"phyBlk = %x,addr= %x,spare= %x %x %x %x data=%x %x\012"
	.ascii	"\000"
.LC57:
	.ascii	"id = %x,%x addr= %x,spare= %x %x %x %x data=%x %x\012"
	.ascii	"\000"
.LC58:
	.ascii	"Mblk:\000"
.LC59:
	.ascii	"L2P:\000"
.LC60:
	.ascii	"L2PC:\000"
.LC61:
	.ascii	"id = %x,%x addr= %x,spare= %x %x %x %x data= %x\012"
	.ascii	"\000"
.LC62:
	.ascii	"superBlkID = %x vpc=%x\012\000"
.LC63:
	.ascii	"flashmode = %x pagenum = %x %x\012\000"
.LC64:
	.ascii	"blk = %x vpc=%x mode = %x\012\000"
.LC65:
	.ascii	"mlc id = %x,%x addr= %x,spare= %x %x %x %x data=%x "
	.ascii	"%x\012\000"
.LC66:
	.ascii	"slc id = %x,%x addr= %x,spare= %x %x %x %x data=%x "
	.ascii	"%x\012\000"
.LC67:
	.ascii	"slc mode\000"
.LC68:
	.ascii	"ftl_scan_all_ppa blk %x page %x\012\000"
.LC69:
	.ascii	"addr= %x, status= %d,spare= %x %x %x %x data=%x %x\012"
	.ascii	"\000"
.LC70:
	.ascii	"%s finished\012\000"
.LC71:
	.ascii	"FLASH INFO:\012\000"
.LC72:
	.ascii	"FLASH ID: %x\012\000"
.LC73:
	.ascii	"Device Capacity: %d MB\012\000"
.LC74:
	.ascii	"FMWAIT: %x %x %x %x\012\000"
.LC75:
	.ascii	"FTL INFO:\012\000"
.LC76:
	.ascii	"g_MaxLpn = 0x%x\012\000"
.LC77:
	.ascii	"g_VaildLpn = 0x%x\012\000"
.LC78:
	.ascii	"read_page_count = 0x%x\012\000"
.LC79:
	.ascii	"discard_page_count = 0x%x\012\000"
.LC80:
	.ascii	"write_page_count = 0x%x\012\000"
.LC81:
	.ascii	"cache_write_count = 0x%x\012\000"
.LC82:
	.ascii	"l2p_write_count = 0x%x\012\000"
.LC83:
	.ascii	"gc_page_count = 0x%x\012\000"
.LC84:
	.ascii	"totle_write = %d MB\012\000"
.LC85:
	.ascii	"totle_read = %d MB\012\000"
.LC86:
	.ascii	"GSV = 0x%x\012\000"
.LC87:
	.ascii	"GDV = 0x%x\012\000"
.LC88:
	.ascii	"bad blk num = %d %d\012\000"
.LC89:
	.ascii	"free_superblocks = 0x%x\012\000"
.LC90:
	.ascii	"mlc_EC = 0x%x\012\000"
.LC91:
	.ascii	"slc_EC = 0x%x\012\000"
.LC92:
	.ascii	"avg_EC = 0x%x\012\000"
.LC93:
	.ascii	"sys_EC = 0x%x\012\000"
.LC94:
	.ascii	"max_EC = 0x%x\012\000"
.LC95:
	.ascii	"min_EC = 0x%x\012\000"
.LC96:
	.ascii	"PLT = 0x%x\012\000"
.LC97:
	.ascii	"POT = 0x%x\012\000"
.LC98:
	.ascii	"MaxSector = 0x%x\012\000"
.LC99:
	.ascii	"init_sys_blks_pp = 0x%x\012\000"
.LC100:
	.ascii	"sys_blks_pp = 0x%x\012\000"
.LC101:
	.ascii	"free sysblock = 0x%x\012\000"
.LC102:
	.ascii	"data_blks_pp = 0x%x\012\000"
.LC103:
	.ascii	"data_op_blks_pp = 0x%x\012\000"
.LC104:
	.ascii	"max_data_blks = 0x%x\012\000"
.LC105:
	.ascii	"Sys.id = 0x%x\012\000"
.LC106:
	.ascii	"Bbt.id = 0x%x\012\000"
.LC107:
	.ascii	"ACT.page = 0x%x\012\000"
.LC108:
	.ascii	"ACT.plane = 0x%x\012\000"
.LC109:
	.ascii	"ACT.id = 0x%x\012\000"
.LC110:
	.ascii	"ACT.mode = 0x%x\012\000"
.LC111:
	.ascii	"ACT.a_pages = 0x%x\012\000"
.LC112:
	.ascii	"ACT VPC = 0x%x\012\000"
.LC113:
	.ascii	"BUF.page = 0x%x\012\000"
.LC114:
	.ascii	"BUF.plane = 0x%x\012\000"
.LC115:
	.ascii	"BUF.id = 0x%x\012\000"
.LC116:
	.ascii	"BUF.mode = 0x%x\012\000"
.LC117:
	.ascii	"BUF.a_pages = 0x%x\012\000"
.LC118:
	.ascii	"BUF VPC = 0x%x\012\000"
.LC119:
	.ascii	"TMP.page = 0x%x\012\000"
.LC120:
	.ascii	"TMP.plane = 0x%x\012\000"
.LC121:
	.ascii	"TMP.id = 0x%x\012\000"
.LC122:
	.ascii	"TMP.mode = 0x%x\012\000"
.LC123:
	.ascii	"TMP.a_pages = 0x%x\012\000"
.LC124:
	.ascii	"GC.page = 0x%x\012\000"
.LC125:
	.ascii	"GC.plane = 0x%x\012\000"
.LC126:
	.ascii	"GC.id = 0x%x\012\000"
.LC127:
	.ascii	"GC.mode = 0x%x\012\000"
.LC128:
	.ascii	"GC.a_pages = 0x%x\012\000"
.LC129:
	.ascii	"WR_CHK = 0x%x %x %x %x\012\000"
.LC130:
	.ascii	"Read Err = 0x%x\012\000"
.LC131:
	.ascii	"Prog Err = 0x%x\012\000"
.LC132:
	.ascii	"gc_free_blk_th= 0x%x\012\000"
.LC133:
	.ascii	"gc_merge_free_blk_th= 0x%x\012\000"
.LC134:
	.ascii	"gc_skip_write_count= 0x%x\012\000"
.LC135:
	.ascii	"gc_blk_index= 0x%x\012\000"
.LC136:
	.ascii	"free min EC= 0x%x\012\000"
.LC137:
	.ascii	"free max EC= 0x%x\012\000"
.LC138:
	.ascii	"GC__SB VPC = 0x%x\012\000"
.LC139:
	.ascii	"%d. [0x%x]=0x%x 0x%x  0x%x\012\000"
.LC140:
	.ascii	"free %d. [0x%x] 0x%x  0x%x\012\000"
.LC141:
	.ascii	"FTL version: 5.0.60 20200218\000"
.LC142:
	.ascii	"swblk %x ,avg = %x max= %x vpc= %x,ec=%x ,max ec=%x"
	.ascii	"\012\000"
.LC143:
	.ascii	"FtlGcScanTempBlk Error ID %x %x!!!!!!! \012\000"
.LC144:
	.ascii	"FtlGcScanTempBlkError ID %x %x!!!!!!!\012\000"
.LC145:
	.ascii	"FtlGcRefreshBlock  0x%x\012\000"
.LC146:
	.ascii	"FtlGcMarkBadPhyBlk %d 0x%x\012\000"
.LC147:
	.ascii	"FtlGcFreeBadSuperBlk 0x%x\012\000"
.LC148:
	.ascii	"decrement_vpc_count %x = %d\012\000"
.LC149:
	.ascii	"decrement_vpc_count %x = %d in free list\012\000"
.LC150:
	.ascii	"RSB refresh addr %x\012\000"
.LC151:
	.ascii	"spuer block %x vpn is 0\012 \000"
.LC152:
	.ascii	"g_recovery_ppa %x ver %x\012 \000"
.LC153:
	.ascii	"FtlCheckVpc %x = %x  %x\012\000"
.LC154:
	.ascii	"%d GC datablk  = %x vpc %x %x\012\000"
.LC155:
	.ascii	"SWL %x, FSB = %x vpc= %x,ec=%x th=%x\012\000"
.LC156:
	.ascii	"Ftlwrite decrement_vpc_count %x = %d\012\000"
.LC157:
	.ascii	"GC des block %x done\012\000"
.LC158:
	.ascii	"...%s: no bad block mapping table, format device\012"
	.ascii	"\000"
.LC159:
	.ascii	"...%s FtlSysBlkInit error ,format device!\012\000"
.LC160:
	.ascii	"FtlWrite: lpa error:%x %x\012\000"
.LC161:
	.ascii	"fix power lost blk = %x vpc=%x\012\000"
.LC162:
	.ascii	"erase power lost blk = %x vpc=%x\012\000"
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
.LANCHOR0 = . + 0
.LANCHOR2 = . + 8184
.LANCHOR4 = . + 16368
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
	.type	gpNandParaInfo, %object
	.size	gpNandParaInfo, 4
gpNandParaInfo:
	.space	4
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
	.type	gFlashInterfaceMode, %object
	.size	gFlashInterfaceMode, 1
gFlashInterfaceMode:
	.space	1
	.space	3
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
	.type	gMultiPageProgEn, %object
	.size	gMultiPageProgEn, 1
gMultiPageProgEn:
	.space	1
	.space	3
	.type	g_inkDie_check_enable, %object
	.size	g_inkDie_check_enable, 4
g_inkDie_check_enable:
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
	.space	2
	.type	FbbtBlk, %object
	.size	FbbtBlk, 16
FbbtBlk:
	.space	16
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
	.type	p_blk_mode_table, %object
	.size	p_blk_mode_table, 4
p_blk_mode_table:
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
	.type	check_valid_page_count_table, %object
	.size	check_valid_page_count_table, 8192
check_valid_page_count_table:
	.space	8192
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
