/*
 * Copyright (c) 2016-2018, Fuzhou Rockchip Electronics Co., Ltd
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 * date: 2019-09-29
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
	.eabi_attribute 18, 2
	.file	"rk_ftlv5_arm_v7.S"
#APP
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
	cmp	r0, #256
	ldr	r3, .L43
	movhi	r2, #512
	bhi	.L42
	cmp	r0, #128
	strls	r0, [r3, #4]
	bxls	lr
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
	.align	2
	.global	FlashReadCmd
	.type	FlashReadCmd, %function
FlashReadCmd:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	@ link register save eliminated.
	ldr	ip, .L46
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
.L47:
	.align	2
.L46:
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
	ldr	ip, .L53
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
	bne	.L49
	mov	r2, #6
	str	r2, [r3, #2056]
	mov	r2, #0
	str	r2, [r3, #2052]
	str	r2, [r3, #2052]
	str	r4, [r3, #2052]
	str	lr, [r3, #2052]
	str	ip, [r3, #2052]
	b	.L52
.L49:
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
.L52:
	mov	r2, #224
	str	r2, [r3, #2056]
	ldmfd	sp!, {r4, lr}
	b	FlashSetRandomizer
.L54:
	.align	2
.L53:
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
	ldr	ip, .L56
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
.L57:
	.align	2
.L56:
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
	ldr	lr, .L64
	add	r0, lr, r0, asl #3
	ldrb	r3, [r0, #16]	@ zero_extendqisi2
	ldr	ip, [r0, #12]
	mov	r3, r3, asl #8
	beq	.L59
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
.L59:
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
.L65:
	.align	2
.L64:
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
	ldr	ip, .L68
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
.L69:
	.align	2
.L68:
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
	ldr	r3, .L72
	add	r0, r3, r0, asl #3
	ldr	r3, .L72+4
	ldrb	r5, [r0, #16]	@ zero_extendqisi2
	ldr	r4, [r0, #12]
	ldr	r3, [r3, #4]
	add	r4, r4, r5, asl #8
	ldr	r0, .L72+8
	blx	r3
	mov	r3, #16
	str	r3, [r4, #2056]
	ldmfd	sp!, {r3, r4, r5, pc}
.L73:
	.align	2
.L72:
	.word	.LANCHOR0
	.word	arm_delay_ops
	.word	64424500
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
	ldr	r2, .L75
	add	r0, r2, r0, asl #3
	ldrb	r2, [r2, #54]	@ zero_extendqisi2
	ldrb	r1, [r0, #16]	@ zero_extendqisi2
	ldr	r3, [r0, #12]
	add	r3, r3, r1, asl #8
	str	r2, [r3, #2056]
	bx	lr
.L76:
	.align	2
.L75:
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
	ldr	r3, .L81
	add	r1, r0, r1
.L78:
	cmp	r0, r1
	beq	.L80
	mov	r2, r3, asl #5
	ldrb	ip, [r0], #1	@ zero_extendqisi2
	add	r2, r2, r3, lsr #2
	add	r2, r2, ip
	eor	r3, r3, r2
	b	.L78
.L80:
	mov	r0, r3
	bx	lr
.L82:
	.align	2
.L81:
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
	bne	.L85
	ldr	r3, .L115
.L86:
	mov	r2, r0, asl #1
	strh	r0, [r2, r3]	@ movhi
	add	r0, r0, #1
	cmp	r0, #256
	bne	.L86
.L90:
	mov	r2, #1024
	ldr	r0, .L115+4
	mov	r1, #255
	uxth	r4, r4
	bl	ftl_memset
	ldr	r2, .L115
	mov	r3, #0
	add	r0, r2, #512
	b	.L87
.L85:
	cmp	r0, #1
	bne	.L88
	ldr	ip, .L115
	mov	r3, #0
.L89:
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
	bne	.L89
	b	.L90
.L88:
	cmp	r0, #2
	bne	.L91
	ldr	r1, .L115
	mov	r3, #0
.L92:
	uxth	r2, r3
	mov	r0, r3, asl #1
	cmp	r2, #1
	add	r3, r3, #1
	mov	ip, r2, asl #1
	subhi	r2, ip, #1
	cmp	r3, #256
	strh	r2, [r0, r1]	@ movhi
	bne	.L92
	b	.L90
.L91:
	cmp	r0, #3
	bne	.L93
	ldr	ip, .L115
	mov	r3, #0
.L94:
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
	bne	.L94
	b	.L90
.L93:
	cmp	r0, #4
	mov	r3, #0
	bne	.L95
	ldr	r2, .L115+8
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
.L96:
	tst	r3, #1
	movne	r1, #7
	moveq	r1, #6
	rsb	r1, r1, r3, asl #1
	add	r3, r3, #1
	strh	r1, [r2, #2]!	@ movhi
	uxth	r3, r3
	cmp	r3, #256
	bne	.L96
	b	.L90
.L95:
	cmp	r0, #5
	bne	.L97
	ldr	r2, .L115
.L98:
	mov	r1, r3, asl #1
	strh	r3, [r1, r2]	@ movhi
	add	r3, r3, #1
	cmp	r3, #16
	bne	.L98
	ldr	r2, .L115+12
.L99:
	strh	r3, [r2, #2]!	@ movhi
	add	r3, r3, #2
	uxth	r3, r3
	cmp	r3, #496
	bne	.L99
	b	.L90
.L97:
	cmp	r0, #6
	bne	.L100
	ldr	r1, .L115+16
.L101:
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
	bne	.L101
	b	.L90
.L100:
	cmp	r0, #9
	bne	.L90
	ldr	r2, .L115+8
	movw	r1, #509
	strh	r3, [r2, #76]	@ movhi
	mov	r3, #1
	strh	r3, [r2, #78]	@ movhi
	mov	r3, #2
	strh	r3, [r2, #80]!	@ movhi
	mov	r3, #3
.L102:
	strh	r3, [r2, #2]!	@ movhi
	add	r3, r3, #2
	uxth	r3, r3
	cmp	r3, r1
	bne	.L102
	b	.L90
.L87:
	uxth	r1, r3
	cmp	r1, r4
	bcs	.L114
	mov	r1, r3, asl #1
	add	r3, r3, #1
	ldrh	r1, [r1, r2]
	mov	ip, r1, asl #1
	strh	r1, [r0, ip]	@ movhi
	b	.L87
.L114:
	ldmfd	sp!, {r4, pc}
.L116:
	.align	2
.L115:
	.word	.LANCHOR0+76
	.word	.LANCHOR0+588
	.word	.LANCHOR0
	.word	.LANCHOR0+106
	.word	.LANCHOR0+74
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
	ldr	r3, .L133
	ldr	r4, .L133+4
	ldr	r9, .L133+8
	ldrh	r0, [r3, #10]
	strb	r5, [r4, #1612]
	strb	r5, [r4, #1613]
	bl	FlashBlockAlignInit
	mov	r1, r5
	mov	r2, #8
	add	r0, r4, #1616
	bl	ftl_memset
	mov	r1, r5
	mov	r2, #32
	ldr	r0, .L133+12
	bl	ftl_memset
	ldr	r0, .L133+16
	mov	r1, r5
	mov	r2, #128
	bl	ftl_memset
	ldr	r7, [r4, #1784]
	add	r6, r7, #1
.L120:
	mov	r0, r6
	add	r1, r9, r5, asl #3
	ldrb	r2, [r7]	@ zero_extendqisi2
	bl	FlashMemCmp8
	ldr	r8, .L133+8
	cmp	r0, #0
	bne	.L119
	ldrb	r3, [r4, #1612]	@ zero_extendqisi2
	add	r2, r4, r3, asl #2
	str	r0, [r2, #1624]
	add	r2, r3, #1
	add	r3, r4, r3
	strb	r2, [r4, #1612]
	strb	r5, [r3, #1616]
.L119:
	add	r5, r5, #1
	cmp	r5, #4
	bne	.L120
	ldrb	r3, [r4, #1612]	@ zero_extendqisi2
	strb	r3, [r4, #1613]
	ldrb	r3, [r7, #8]	@ zero_extendqisi2
	cmp	r3, #2
	beq	.L121
.L125:
	ldrb	r3, [r7, #13]	@ zero_extendqisi2
	ldrb	r2, [r4, #1612]	@ zero_extendqisi2
	smulbb	r2, r2, r3
	ldrh	r3, [r7, #14]
	smulbb	r3, r2, r3
	movw	r2, #1788
	strh	r3, [r4, r2]	@ movhi
	ldmfd	sp!, {r3, r4, r5, r6, r7, r8, r9, pc}
.L121:
	ldr	r3, .L133+4
	mov	r5, #0
	ldr	r9, [r3, #4]
.L124:
	mov	r0, r6
	add	r1, r8, r5, asl #3
	ldrb	r2, [r7]	@ zero_extendqisi2
	bl	FlashMemCmp8
	cmp	r0, #0
	bne	.L122
	ldrb	r1, [r7, #13]	@ zero_extendqisi2
	ldrh	r3, [r7, #14]
	ldrb	r2, [r4, #1612]	@ zero_extendqisi2
	mul	r1, r9, r1
	and	r3, r3, #65280
	add	r0, r4, r2, asl #2
	mul	r3, r3, r1
	str	r3, [r0, #1624]
	ldrb	r1, [r7, #23]	@ zero_extendqisi2
	cmp	r1, #0
	movne	r3, r3, asl #1
	strne	r3, [r0, #1624]
	add	r3, r2, #1
	add	r2, r4, r2
	strb	r3, [r4, #1612]
	strb	r5, [r2, #1616]
.L122:
	add	r5, r5, #1
	cmp	r5, #4
	bne	.L124
	b	.L125
.L134:
	.align	2
.L133:
	.word	.LANCHOR1+256
	.word	.LANCHOR0
	.word	IDByte
	.word	.LANCHOR0+1624
	.word	.LANCHOR0+1656
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
	ldr	r1, .L137
	bl	ftl_memcpy
	mov	r0, #0
	ldmfd	sp!, {r3, pc}
.L138:
	.align	2
.L137:
	.word	.LANCHOR0+1792
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
	ldr	r8, .L152
.L143:
	mov	r4, r6, asl #5
	ldr	r1, .L152+4
	add	r0, r4, #1
	ldrb	r2, [r8, r6, asl #5]	@ zero_extendqisi2
	add	r0, r8, r0
	ldr	r5, .L152+8
	bl	FlashMemCmp8
	add	r9, r5, #288
	subs	r7, r0, #0
	bne	.L140
	adds	r9, r9, r4
	beq	.L146
	add	r4, r5, r4
	add	r5, r5, #2880
	ldrb	r3, [r4, #310]	@ zero_extendqisi2
	mov	r4, r7
	b	.L145
.L140:
	add	r6, r6, #1
	cmp	r6, #81
	bne	.L143
	b	.L146
.L151:
	add	r4, r4, #1
	cmp	r4, #4
	beq	.L144
.L145:
	ldrb	r2, [r5, r4, asl #5]	@ zero_extendqisi2
	cmp	r2, r3
	bne	.L151
.L144:
	ldr	r6, .L152+12
	mov	r2, #32
	ldr	r1, .L152+16
	ldr	r0, .L152+20
	add	r1, r1, r4, asl #5
	bl	ftl_memcpy
	mov	r0, r6
	mov	r1, r9
	mov	r2, #32
	bl	ftl_memcpy
	ldrh	r0, [r6, #10]
	bl	FlashBlockAlignInit
	b	.L141
.L146:
	mvn	r7, #0
.L141:
	mov	r0, r7
	ldmfd	sp!, {r3, r4, r5, r6, r7, r8, r9, pc}
.L153:
	.align	2
.L152:
	.word	.LANCHOR1+288
	.word	IDByte
	.word	.LANCHOR1
	.word	.LANCHOR1+256
	.word	.LANCHOR1+2880
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
	ldr	r3, .L155
	ldr	r2, [r3, #3840]
	ldr	r1, [r2]
	str	r1, [r3, #3844]
	ldr	r1, [r2, #4]
	str	r1, [r3, #3848]
	ldr	r1, [r2, #8]
	str	r1, [r3, #3852]
	ldr	r1, [r2, #12]
	str	r1, [r3, #3856]
	ldr	r1, [r2, #304]
	str	r1, [r3, #3860]
	ldr	r1, [r2, #308]
	str	r1, [r3, #3864]
	ldr	r1, [r2, #336]
	ldr	r2, [r2, #344]
	str	r1, [r3, #3868]
	str	r2, [r3, #3872]
	bx	lr
.L156:
	.align	2
.L155:
	.word	.LANCHOR0
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
	ldr	r3, .L163
	mov	r10, r1
	ldr	r7, .L163+4
	mov	r6, r0
	ldr	ip, [r0, #4]
	mov	r8, r2
	ldrh	r4, [r3, #12]
	ldrh	r3, [r3, #14]
	bic	ip, ip, #-2147483648
	ldrh	fp, [r7, #4]
	ubfx	r5, ip, #10, #16
	str	ip, [sp, #4]
	smulbb	r4, r4, r3
	ldrb	r3, [r7]	@ zero_extendqisi2
	mov	r0, r5
	cmp	r3, #1
	moveq	fp, fp, asl #1
	uxth	r4, r4
	uxtheq	fp, fp
	mov	r1, r4
	bl	__aeabi_uidiv
	cmp	r10, #1
	uxth	r0, r0
	ldr	ip, [sp, #4]
	smulbb	r4, r0, r4
	rsb	r5, r4, r5
	ubfx	r4, ip, #0, #10
	uxth	r5, r5
	bne	.L159
	ldrb	r3, [r7, #3904]	@ zero_extendqisi2
	cmp	r3, #0
	ldreq	r3, .L163+4
	addeq	r4, r3, r4, asl #1
	ldreqh	r4, [r4, #76]
.L159:
	add	ip, r7, r0, asl #2
	ldrb	r3, [sp, #48]	@ zero_extendqisi2
	ldr	r1, [ip, #1624]
	cmp	r3, #1
	mla	r1, fp, r5, r1
	add	r4, r1, r4
	str	r4, [r8]
	str	r0, [r9]
	movls	r0, #0
	ldrhi	r0, [r6, #4]
	ldrhi	r3, [r6, #40]
	addhi	r0, r0, #1024
	rsbhi	r0, r3, r0
	clzhi	r0, r0
	movhi	r0, r0, lsr #5
	add	sp, sp, #12
	@ sp needed
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L164:
	.align	2
.L163:
	.word	.LANCHOR0+3876
	.word	.LANCHOR0
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
	ldr	r3, .L170
	mov	ip, #1
	add	r3, r3, #1616
	ldr	r2, [r3, #168]
	ldrb	r1, [r2, #9]	@ zero_extendqisi2
	ldr	r2, [r3, #-1612]
	smulbb	r2, r1, r2
	strh	r2, [r4, #4]	@ unaligned
	ldrb	r2, [r3, #2289]	@ zero_extendqisi2
	strb	r2, [r4, #7]
	ldr	r2, [r3, #2292]
	str	r2, [r4]	@ unaligned
	ldr	r2, [r3, #168]
	ldrb	r0, [r3, #-4]	@ zero_extendqisi2
	ldrb	r1, [r2, #9]	@ zero_extendqisi2
	strb	r1, [r4, #6]
	mov	r1, #32
	strb	r1, [r4, #8]
	ldrb	r2, [r2, #7]	@ zero_extendqisi2
	strb	r2, [r4, #9]
	mov	r2, #0
	strb	r2, [r4, #10]
.L166:
	uxtb	r1, r2
	cmp	r1, r0
	bcs	.L169
	ldrb	lr, [r2, r3]	@ zero_extendqisi2
	add	r2, r2, #1
	ldrb	r1, [r4, #10]	@ zero_extendqisi2
	orr	r1, r1, ip, asl lr
	strb	r1, [r4, #10]
	b	.L166
.L169:
	ldmfd	sp!, {r4, pc}
.L171:
	.align	2
.L170:
	.word	.LANCHOR0
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
	ldr	r3, .L173
	ldr	r2, [r3, #3912]
	str	r0, [r3, #3912]
	mov	r0, r2
	bx	lr
.L174:
	.align	2
.L173:
	.word	.LANCHOR0
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
	ldr	r3, .L176
	ldr	r3, [r3, #1784]
	ldrb	r0, [r3, #9]	@ zero_extendqisi2
	bx	lr
.L177:
	.align	2
.L176:
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
	ldr	r3, .L180
	add	r0, r3, r0, asl #3
	ldrb	r2, [r0, #16]	@ zero_extendqisi2
	ldr	r0, [r0, #12]
	add	r2, r2, #8
	add	r0, r0, r2, asl #8
	bx	lr
.L181:
	.align	2
.L180:
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
	ldr	r3, .L183
	ldr	r2, [r3, #3840]
	mov	r3, r0, asl #8
	orr	r0, r3, r0, asl #16
	orr	r3, r0, #1
	str	r3, [r2, #304]
	bx	lr
.L184:
	.align	2
.L183:
	.word	.LANCHOR0
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
	ldr	r3, .L186
	orr	r0, r0, #16640
	ldr	r3, [r3, #3840]
	str	r0, [r3, #344]
	bx	lr
.L187:
	.align	2
.L186:
	.word	.LANCHOR0
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
	ldr	r3, .L191
	cmp	r0, #0
	ldr	r2, [r3, #3840]
	ldr	r3, [r2]
	bfieq	r3, r0, #13, #1
	orrne	r3, r3, #253952
	str	r3, [r2]
	bx	lr
.L192:
	.align	2
.L191:
	.word	.LANCHOR0
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
	ldr	r3, .L200
	ands	r1, r0, #6
	ldr	r2, [r3, #3840]
	ldr	r3, [r2]
	bfieq	r3, r1, #13, #1
	beq	.L196
	orr	r3, r3, #24576
	movw	r1, #8322
	bfc	r3, #15, #1
	str	r1, [r2, #344]
	orr	r3, r3, #196608
	ldr	r1, .L200+4
	tst	r0, #4
	orrne	r3, r3, #32768
	str	r1, [r2, #304]
	mov	r1, #38
	str	r1, [r2, #308]
	mov	r1, #39
	str	r1, [r2, #308]
.L196:
	str	r3, [r2]
	mov	r0, #0
	bx	lr
.L201:
	.align	2
.L200:
	.word	.LANCHOR0
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
	ldr	r2, .L203
	add	r0, r2, r0, asl #3
	mov	r2, #1
	ldr	r1, [r0, #12]
	ldrb	r0, [r0, #16]	@ zero_extendqisi2
	ldr	r3, [r1]
	mov	r2, r2, asl r0
	bfi	r3, r2, #0, #8
	str	r3, [r1]
	bx	lr
.L204:
	.align	2
.L203:
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
	ldr	r3, .L206
	add	r0, r3, r0, asl #3
	ldr	r2, [r0, #12]
	ldr	r3, [r2]
	bfc	r3, #0, #8
	bfc	r3, #17, #1
	str	r3, [r2]
	bx	lr
.L207:
	.align	2
.L206:
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
	ldr	r3, .L210
	add	r0, r0, #3
	umull	r0, r1, r0, r3
	ldr	r3, .L210+4
	ldr	r3, [r3, #8]
	mov	r0, r1, lsr #6
	blx	r3
	mov	r0, #0
	ldmfd	sp!, {r3, pc}
.L211:
	.align	2
.L210:
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
	ldr	r3, .L214
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
.L215:
	.align	2
.L214:
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
	ldr	r8, .L226
	mov	r5, r0
	ldr	r10, .L226+4
	mov	r6, r1
	add	r9, r8, #3008
	mov	r4, #0
.L217:
	ldrb	r3, [r10]	@ zero_extendqisi2
	cmp	r4, r3
	bcs	.L225
	mov	r3, #85
	str	r3, [r5, #8]
	ldrsb	r3, [r4, r9]
	mov	r0, #200
	str	r3, [r5, #4]
	bl	NandcDelayns
	ldr	r3, .L226+8
	ldrb	r3, [r3]	@ zero_extendqisi2
	cmp	r3, #34
	addeq	r3, r4, r7
	addeq	r3, r9, r3
	beq	.L224
	cmp	r3, #35
	addne	r3, r8, r6
	addne	r3, r3, #3152
	ldrnesb	r3, [r3]
	bne	.L223
	ldr	r3, .L226+12
	add	r2, r4, r7
	add	r3, r3, r2
.L224:
	ldrsb	r3, [r3, #5]
.L223:
	str	r3, [r5]
	add	r4, r4, #1
	b	.L217
.L225:
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, pc}
.L227:
	.align	2
.L226:
	.word	.LANCHOR1
	.word	g_maxRegNum
	.word	g_retryMode
	.word	.LANCHOR1+3056
	.fnend
	.size	ToshibaSetRRPara, .-ToshibaSetRRPara
	.align	2
	.global	SamsungSetRRPara
	.type	SamsungSetRRPara, %function
SamsungSetRRPara:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	ldr	r3, .L233
	stmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, lr}
	.save {r4, r5, r6, r7, r8, r9, r10, lr}
	add	r1, r3, r1, asl #2
	ldr	r8, .L233+4
	mov	r4, #0
	add	r5, r1, #3
	mov	r6, r0
	mov	r7, r3
	mov	r9, #161
	mov	r10, r4
.L229:
	ldrb	r3, [r8]	@ zero_extendqisi2
	cmp	r4, r3
	bcs	.L232
	str	r9, [r6, #8]
	mov	r0, #300
	str	r10, [r6]
	ldrsb	r3, [r4, r7]
	add	r4, r4, #1
	str	r3, [r6]
	ldrsb	r3, [r5, #1]!
	str	r3, [r6]
	bl	NandcDelayns
	b	.L229
.L232:
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, pc}
.L234:
	.align	2
.L233:
	.word	.LANCHOR1+3160
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
	mov	r5, r3
	ldr	r3, .L244
	mov	r10, r2
	ldr	r4, .L244+4
	mov	r6, r0
	mov	r9, r1
	ldr	r2, [r3, #1784]
	ldrb	r8, [r2, #19]	@ zero_extendqisi2
	mov	r2, r0, asl #3
	cmp	r8, #6
	addeq	r4, r4, r0, asl #6
	addeq	r4, r4, #20
	addeq	r4, r4, r5, asl #2
	beq	.L237
	cmp	r8, #7
	bne	.L238
	mov	r8, #160
	add	r1, r5, r5, asl #2
	mla	r4, r8, r0, r4
	add	r4, r4, #28
	add	r4, r4, r1, asl #1
	b	.L237
.L238:
	cmp	r8, #8
	addne	r1, r5, r2
	addeq	r1, r5, r5, asl #2
	ldreq	r4, .L244+8
	addne	r4, r4, r1, asl #3
	addeq	r4, r4, r1
	addne	r4, r4, #20
.L237:
	add	r3, r3, r2
	mov	r0, r6
	sub	r9, r9, #1
	sub	r4, r4, #1
	ldrb	fp, [r3, #16]	@ zero_extendqisi2
	ldr	r8, [r3, #12]
	bl	NandcFlashCs
	sub	r2, r10, #1
	add	r10, r10, r9
	mov	fp, fp, asl #8
	mov	r3, #54
	add	r7, r8, fp
	str	r3, [r7, #2056]
.L240:
	cmp	r2, r10
	beq	.L243
	ldrb	r3, [r2, #1]!	@ zero_extendqisi2
	mov	r0, #200
	str	r3, [r7, #2052]
	str	r2, [sp, #4]
	bl	NandcDelayns
	ldrsb	r3, [r4, #1]!
	str	r3, [r7, #2048]
	ldr	r2, [sp, #4]
	b	.L240
.L243:
	add	r8, r8, fp
	mov	r3, #22
	mov	r0, r6
	str	r3, [r8, #2056]
	bl	NandcFlashDeCs
	ldr	r3, .L244+12
	add	r6, r3, r6
	strb	r5, [r6, #-3416]
	add	sp, sp, #12
	@ sp needed
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L245:
	.align	2
.L244:
	.word	.LANCHOR0
	.word	.LANCHOR0+3916
	.word	.LANCHOR0+3944
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
	ldr	r3, .L254
	ldr	r2, [r3, #1784]
	ldrb	r2, [r2, #19]	@ zero_extendqisi2
	sub	r2, r2, #1
	cmp	r2, #7
	bxhi	lr
	stmfd	sp!, {r4, r5, r6, lr}
	.save {r4, r5, r6, lr}
	mov	r5, r3
	ldr	r6, .L254+4
	mov	r4, #0
.L247:
	ldrb	r3, [r6, r4, asl #3]	@ zero_extendqisi2
	uxtb	r0, r4
	cmp	r3, #173
	bne	.L248
	ldrb	r1, [r5, #3917]	@ zero_extendqisi2
	mov	r3, #0
	ldr	r2, .L254+8
	bl	HynixSetRRPara
.L248:
	add	r4, r4, #1
	cmp	r4, #4
	bne	.L247
	ldmfd	sp!, {r4, r5, r6, pc}
.L255:
	.align	2
.L254:
	.word	.LANCHOR0
	.word	IDByte
	.word	.LANCHOR0+3920
	.fnend
	.size	FlashSetReadRetryDefault, .-FlashSetReadRetryDefault
	.align	2
	.global	FlashReadStatusEN
	.type	FlashReadStatusEN, %function
FlashReadStatusEN:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	ldr	ip, .L267
	stmfd	sp!, {r3, r4, r5, lr}
	.save {r3, r4, r5, lr}
	add	r0, ip, r0, asl #3
	ldr	r3, [ip, #1784]
	ldrb	r5, [r0, #16]	@ zero_extendqisi2
	ldr	r4, [r0, #12]
	ldrb	r3, [r3, #8]	@ zero_extendqisi2
	cmp	r3, #2
	mov	r3, r5, asl #8
	addne	r3, r4, r3
	add	r5, r5, #8
	movne	r2, #112
	strne	r2, [r3, #2056]
	bne	.L261
	cmp	r2, #0
	add	r3, r4, r3
	ldrneb	r2, [ip, #58]	@ zero_extendqisi2
	ldreqb	r2, [ip, #57]	@ zero_extendqisi2
	str	r2, [r3, #2056]
	ldrb	r0, [ip, #59]	@ zero_extendqisi2
	cmp	r0, #0
	addne	ip, r4, r5, asl #8
	movne	r2, #0
	beq	.L261
.L260:
	cmp	r2, r0
	bcs	.L261
	mov	r3, r2, asl #3
	add	r2, r2, #1
	mov	r3, r1, lsr r3
	uxtb	r3, r3
	str	r3, [ip, #4]
	b	.L260
.L261:
	mov	r0, #80
	bl	NandcDelayns
	ldr	r0, [r4, r5, asl #8]
	uxtb	r0, r0
	ldmfd	sp!, {r3, r4, r5, pc}
.L268:
	.align	2
.L267:
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
.L273:
	mov	r0, r4
	mov	r1, r5
	mov	r2, r6
	bl	FlashReadStatusEN
	cmp	r0, #255
	beq	.L273
	tst	r0, #64
	beq	.L273
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
	ldr	r5, .L285
	add	r4, r5, r0, asl #4
	ldr	r3, [r4, #1664]
	ldrb	r7, [r4, #1656]	@ zero_extendqisi2
	cmp	r3, #0
	beq	.L279
	mov	r6, r0
	mov	r0, r7
	add	r5, r5, r6, asl #2
	bl	NandcFlashCs
	ldr	r1, [r4, #1660]
	mov	r0, r7
	ldr	r2, [r5, #1624]
	adds	r2, r2, #0
	movne	r2, #1
	bl	FlashWaitReadyEN
	mov	r5, r0
	mov	r0, r7
	bl	NandcFlashDeCs
	ldr	r2, [r4, #1664]
	sbfx	r3, r5, #0, #1
	str	r3, [r2]
	mov	r2, #0
	ldr	r1, [r4, #1668]
	str	r2, [r4, #1664]
	cmp	r1, r2
	strne	r3, [r1]
	strne	r2, [r4, #1668]
.L279:
	mov	r0, #0
	ldmfd	sp!, {r3, r4, r5, r6, r7, pc}
.L286:
	.align	2
.L285:
	.word	.LANCHOR0
	.fnend
	.size	FlashWaitCmdDone, .-FlashWaitCmdDone
	.align	2
	.type	flash_read_ecc, %function
flash_read_ecc:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	ldr	r2, .L289
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
.L290:
	.align	2
.L289:
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
	ldr	r3, .L297
	stmfd	sp!, {r0, r1, r2, r4, r5, lr}
	.save {r4, r5, lr}
	.pad #12
	add	r0, r3, r0, asl #3
	ldr	r4, .L297+4
	ldr	r5, [r0, #12]
.L293:
	ldr	r3, [r5]
	str	r3, [sp, #4]
	ldr	r3, [sp, #4]
	tst	r3, #512
	bne	.L294
	mov	r0, #10
	bl	NandcDelayns
	subs	r4, r4, #1
	bne	.L293
	mvn	r0, #0
	b	.L292
.L294:
	mov	r0, #0
.L292:
	add	sp, sp, #12
	@ sp needed
	ldmfd	sp!, {r4, r5, pc}
.L298:
	.align	2
.L297:
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
	ldr	r3, .L305
	stmfd	sp!, {r0, r1, r2, r4, r5, lr}
	.save {r4, r5, lr}
	.pad #12
	add	r0, r3, r0, asl #3
	ldr	r4, .L305+4
	ldr	r5, [r0, #12]
	mov	r0, #150
	bl	NandcDelayns
.L301:
	ldr	r3, [r5]
	str	r3, [sp, #4]
	ldr	r3, [sp, #4]
	tst	r3, #512
	bne	.L302
	mov	r0, #10
	bl	NandcDelayns
	subs	r4, r4, #1
	bne	.L301
	mvn	r0, #0
	b	.L300
.L302:
	mov	r0, #0
.L300:
	add	sp, sp, #12
	@ sp needed
	ldmfd	sp!, {r4, r5, pc}
.L306:
	.align	2
.L305:
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
	ldr	r3, .L309
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
.L310:
	.align	2
.L309:
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
	ldr	r4, .L314
	ldrb	r3, [r4, #3904]	@ zero_extendqisi2
	cmp	r3, #0
	ldmeqfd	sp!, {r3, r4, r5, pc}
	mov	r5, r0
	bl	NandcFlashCs
	add	r3, r4, r5, asl #3
	mov	r0, #100
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
.L315:
	.align	2
.L314:
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
	ldr	r4, .L319
	ldrb	r3, [r4, #3904]	@ zero_extendqisi2
	cmp	r3, #0
	ldmeqfd	sp!, {r3, r4, r5, pc}
	mov	r5, r0
	bl	NandcFlashCs
	add	r3, r4, r5, asl #3
	mov	r0, #100
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
.L320:
	.align	2
.L319:
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
	ldr	r3, .L346
	stmfd	sp!, {r0, r1, r2, r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.save {r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.pad #12
	mov	lr, #0
	ldrb	r2, [r3, #-3412]	@ zero_extendqisi2
	mov	r4, #239
	ldr	r7, .L346+4
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
.L333:
	ldr	r3, .L346+8
	ldrb	ip, [lr, r7]	@ zero_extendqisi2
	ldr	r1, [r3, lr]!
	cmp	ip, #69
	cmpne	ip, #152
	ldrb	r3, [r3, #4]	@ zero_extendqisi2
	beq	.L324
	cmp	ip, #44
	cmpne	ip, #173
	bne	.L325
.L324:
	cmp	r0, #1
	bne	.L326
	ldr	fp, [sp, #4]
	cmp	fp, #0
	beq	.L325
	mov	r3, r3, asl #8
	cmp	ip, #173
	add	fp, r1, r3
	str	r4, [fp, #2056]
	streq	r0, [fp, #2052]
	beq	.L345
	cmp	ip, #44
	streq	r0, [fp, #2052]
	strne	r5, [fp, #2052]
	streq	r10, [fp, #2048]
	strne	r0, [fp, #2048]
	b	.L331
.L326:
	ldr	fp, [sp]
	cmp	fp, #0
	beq	.L325
	mov	r3, r3, asl #8
	cmp	ip, #173
	add	fp, r1, r3
	str	r4, [fp, #2056]
	streq	r6, [fp, #2052]
	streq	r9, [fp, #2048]
	beq	.L331
	cmp	ip, #44
	streq	r6, [fp, #2052]
	streq	r8, [fp, #2048]
	beq	.L331
	str	r5, [fp, #2052]
.L345:
	str	r2, [fp, #2048]
.L331:
	add	r3, r1, r3
	str	r2, [r3, #2048]
	str	r2, [r3, #2048]
	str	r2, [r3, #2048]
.L325:
	add	lr, lr, #8
	cmp	lr, #32
	bne	.L333
	mov	r0, #0
	bl	NandcWaitFlashReady
	mov	r0, #0
	add	sp, sp, #12
	@ sp needed
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L347:
	.align	2
.L346:
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
	ldr	ip, .L350
	stmfd	sp!, {r3, r4, r5, lr}
	.save {r3, r4, r5, lr}
	add	ip, ip, r0, asl #3
	ldr	r3, .L350+4
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
.L351:
	.align	2
.L350:
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
	ldr	r2, .L354
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
.L355:
	.align	2
.L354:
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
	ldr	r1, .L364
	ldr	r0, .L364+4
	add	r4, r4, r4, asl #2
	ldr	ip, .L364+8
	sub	lr, r1, #48
	mov	r2, #0
.L357:
	ldrb	r3, [r0]	@ zero_extendqisi2
	cmp	r2, r3
	bcs	.L363
	ldrb	r3, [ip]	@ zero_extendqisi2
	cmp	r3, #67
	add	r3, r2, r4
	addeq	r3, lr, r3
	addne	r3, r1, r3
	add	r2, r2, #1
	ldrsb	r3, [r3, #5]
	str	r3, [r5]
	b	.L357
.L363:
	mov	r0, #0
	ldmfd	sp!, {r3, r4, r5, lr}
	b	NandcWaitFlashReady
.L365:
	.align	2
.L364:
	.word	.LANCHOR1+3056
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
	ldr	r3, .L368
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
.L369:
	.align	2
.L368:
	.word	.LANCHOR0
	.fnend
	.size	micron_auto_read_calibration_config, .-micron_auto_read_calibration_config
	.align	2
	.global	FlashEraseSLc2KBlocks
	.type	FlashEraseSLc2KBlocks, %function
FlashEraseSLc2KBlocks:
	.fnstart
	@ args = 0, pretend = 0, frame = 16
	@ frame_needed = 0, uses_anonymous_args = 0
	stmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, lr}
	.save {r4, r5, r6, r7, r8, r9, r10, lr}
	mov	r5, #0
	ldr	r7, .L383
	.pad #24
	sub	sp, sp, #24
	ldr	r9, .L383+4
	mov	r6, r0
	mov	r10, r1
	mov	r8, r5
	ldr	r3, [r7]
	str	r3, [sp, #20]
.L371:
	cmp	r8, r10
	beq	.L382
	rsb	r3, r8, r10
	add	r2, sp, #12
	add	r0, r6, r5
	mov	r1, #0
	uxtb	r3, r3
	str	r3, [sp]
	add	r3, sp, #16
	bl	LogAddr2PhyAddr
	ldrb	r2, [r9, #1612]	@ zero_extendqisi2
	ldr	r3, [sp, #16]
	cmp	r3, r2
	mvncs	r3, #0
	strcs	r3, [r6, r5]
	bcs	.L373
	add	r2, r9, r3
	add	r3, r9, r3, asl #4
	ldrb	r4, [r2, #1616]	@ zero_extendqisi2
	strb	r4, [r3, #1656]
	mov	r0, r4
	bl	NandcWaitFlashReady
	mov	r0, r4
	bl	NandcFlashCs
	mov	r2, #0
	mov	r0, r4
	ldr	r1, [sp, #12]
	bl	FlashEraseCmd
	mov	r0, r4
	bl	NandcWaitFlashReady
	mov	r0, r4
	ldr	r1, [sp, #12]
	bl	FlashReadStatus
	mov	r2, #0
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
	tst	r0, #1
	mvnne	r3, #0
	strne	r3, [r6, r5]
	ldr	r3, [r6, r5]
	cmn	r3, #1
	bne	.L375
	ldr	r0, .L383+8
	ldr	r1, [sp, #12]
	bl	printk
.L375:
	mov	r0, r4
	bl	NandcFlashDeCs
.L373:
	add	r8, r8, #1
	add	r5, r5, #36
	b	.L371
.L382:
	ldr	r2, [sp, #20]
	mov	r0, #0
	ldr	r3, [r7]
	cmp	r2, r3
	beq	.L377
	bl	__stack_chk_fail
.L377:
	add	sp, sp, #24
	@ sp needed
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, pc}
.L384:
	.align	2
.L383:
	.word	__stack_chk_guard
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
	stmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.save {r4, r5, r6, r7, r8, r9, r10, fp, lr}
	cmp	r0, #2
	ldr	r7, .L494
	mvn	r3, #83
	mvn	r1, #82
	mvn	r2, #81
	.pad #44
	sub	sp, sp, #44
	mov	r4, r0
	strb	r3, [r7, #3920]
	mvn	r3, #80
	strb	r0, [r7, #3916]
	strb	r1, [r7, #3921]
	strb	r2, [r7, #3922]
	strb	r3, [r7, #3923]
	bne	.L386
	mvn	r3, #88
	strb	r3, [r7, #3920]
	ldr	r3, .L494+4
	mvn	r2, #8
	mov	r5, #7
	strb	r2, [r3, #3241]
	b	.L447
.L386:
	cmp	r0, #3
	bne	.L388
	mvn	r3, #79
	strb	r3, [r7, #3920]
	mvn	r3, #78
	strb	r3, [r7, #3921]
	mvn	r3, #77
	strb	r3, [r7, #3922]
	mvn	r3, #76
	strb	r3, [r7, #3923]
	mvn	r3, #75
	strb	r3, [r7, #3924]
	mvn	r3, #74
	strb	r3, [r7, #3925]
	mvn	r3, #73
	strb	r3, [r7, #3926]
	mvn	r3, #72
	b	.L488
.L388:
	cmp	r0, #4
	bne	.L389
	mvn	r0, #51
	strb	r1, [r7, #3925]
	strb	r0, [r7, #3920]
	mvn	r0, #64
	strb	r2, [r7, #3926]
	strb	r0, [r7, #3921]
	mvn	r0, #85
	strb	r0, [r7, #3922]
	mvn	r0, #84
	strb	r0, [r7, #3923]
	mvn	r0, #50
	strb	r0, [r7, #3924]
.L488:
	mov	r5, #8
	strb	r3, [r7, #3927]
	mov	r6, r5
	b	.L387
.L389:
	cmp	r0, #5
	bne	.L390
	mov	r3, #56
	mov	r5, #8
	strb	r3, [r7, #3920]
	mov	r3, #57
	strb	r3, [r7, #3921]
	mov	r3, #58
	strb	r3, [r7, #3922]
	mov	r3, #59
	strb	r3, [r7, #3923]
	b	.L447
.L390:
	cmp	r0, #6
	bne	.L391
	mov	r3, #14
	mov	r5, #12
	strb	r3, [r7, #3920]
	mov	r3, #15
	strb	r3, [r7, #3921]
	mov	r3, #16
	strb	r3, [r7, #3922]
	mov	r3, #17
	strb	r3, [r7, #3923]
	b	.L447
.L391:
	cmp	r0, #7
	bne	.L392
	mvn	r3, #79
	mov	r5, #12
	strb	r3, [r7, #3920]
	mov	r6, #10
	mvn	r3, #78
	strb	r3, [r7, #3921]
	mvn	r3, #77
	strb	r3, [r7, #3922]
	mvn	r3, #76
	strb	r3, [r7, #3923]
	mvn	r3, #75
	strb	r3, [r7, #3924]
	mvn	r3, #74
	strb	r3, [r7, #3925]
	mvn	r3, #73
	strb	r3, [r7, #3926]
	mvn	r3, #72
	strb	r3, [r7, #3927]
	mvn	r3, #43
	strb	r3, [r7, #3928]
	mvn	r3, #42
	strb	r3, [r7, #3929]
	b	.L387
.L392:
	cmp	r0, #8
	mov	r5, #7
	bne	.L447
	mov	r3, #6
	strb	r5, [r7, #3921]
	strb	r3, [r7, #3920]
	mov	r5, #50
	mov	r3, #9
	strb	r0, [r7, #3922]
	strb	r3, [r7, #3923]
	mov	r6, #5
	mov	r3, #10
	strb	r3, [r7, #3924]
	b	.L387
.L447:
	mov	r6, #4
.L387:
	sub	r3, r4, #1
	cmp	r3, #1
	bhi	.L484
	ldr	r10, .L494+8
	mov	r9, #0
.L393:
	ldrb	r1, [r7, #1612]	@ zero_extendqisi2
	uxtb	r2, r9
	ldr	r3, .L494
	cmp	r1, r2
	bls	.L400
	add	r2, r3, r2
	ldr	r4, .L494+12
	ldr	r1, .L494+16
	mov	ip, #55
	ldrb	r2, [r2, #1616]	@ zero_extendqisi2
	add	r3, r3, r2, asl #3
	add	r4, r4, r2, asl #6
	add	r4, r4, #20
	ldrb	r8, [r3, #16]	@ zero_extendqisi2
	ldr	r0, [r3, #12]
	mov	fp, r4
	mov	r2, r4
	add	r8, r0, r8, asl #8
.L395:
	str	ip, [r8, #2056]
	mov	r0, #80
	ldrb	r3, [r1, #1]!	@ zero_extendqisi2
	str	r2, [sp, #12]
	str	ip, [sp, #8]
	str	r3, [r8, #2052]
	str	r1, [sp, #4]
	str	r1, [sp, #16]
	bl	NandcDelayns
	ldr	r3, [r8, #2048]
	ldr	r2, [sp, #12]
	ldmib	sp, {r1, ip}
	strb	r3, [r2], #1
	rsb	r3, r4, r2
	uxtb	r3, r3
	cmp	r3, r6
	bcc	.L395
	mov	r2, #0
.L396:
	add	r0, r10, r2
	mov	r3, #1
.L397:
	ldrb	r1, [r0, r3, asl #2]	@ zero_extendqisi2
	ldrb	ip, [fp]	@ zero_extendqisi2
	add	r1, r1, ip
	strb	r1, [fp, r3, asl #3]
	add	r3, r3, #1
	cmp	r3, #7
	bne	.L397
	add	r2, r2, #1
	add	fp, fp, #1
	cmp	r2, #4
	bne	.L396
	mov	r3, #0
	add	r9, r9, #1
	strb	r3, [r4, #16]
	strb	r3, [r4, #24]
	strb	r3, [r4, #32]
	strb	r3, [r4, #40]
	strb	r3, [r4, #48]
	strb	r3, [r4, #41]
	strb	r3, [r4, #49]
	b	.L393
.L484:
	sub	r3, r4, #3
	cmp	r3, #5
	bhi	.L400
	smulbb	r2, r6, r5
	mov	r3, r2, asl #4
	mov	r2, r2, asr #1
	str	r3, [sp, #36]
	mov	r3, r2, asl #1
	str	r3, [sp, #8]
	mov	r3, #0
.L492:
	str	r3, [sp, #16]
	ldrb	r3, [sp, #16]	@ zero_extendqisi2
	str	r3, [sp, #12]
	ldr	r3, .L494
	ldr	r2, [sp, #12]
	ldrb	r3, [r3, #1612]	@ zero_extendqisi2
	cmp	r3, r2
	bhi	.L445
.L400:
	ldr	r3, .L494
	strb	r6, [r3, #3917]
	strb	r5, [r3, #3918]
	add	sp, sp, #44
	@ sp needed
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L445:
	ldr	r2, [sp, #12]
	ldr	r3, .L494
	add	r3, r3, r2
	ldrb	r10, [r3, #1616]	@ zero_extendqisi2
	ldr	r3, .L494
	add	r3, r3, r10, asl #3
	mov	r0, r10
	ldrb	fp, [r3, #16]	@ zero_extendqisi2
	ldr	r8, [r3, #12]
	mov	r3, #255
	mov	r9, fp, asl #8
	add	r7, r8, r9
	str	r3, [r7, #2056]
	bl	NandcWaitFlashReady
	cmp	r4, #7
	ldreq	r3, .L494+12
	moveq	r7, #160
	mlaeq	r7, r7, r10, r3
	addeq	r3, r7, #28
	beq	.L489
.L402:
	cmp	r4, #8
	beq	.L404
	ldr	r3, .L494+12
	add	r7, r3, r10, asl #6
	add	r3, r7, #20
.L489:
	str	r3, [sp, #20]
	cmp	r4, #4
	add	r3, r8, fp, asl #8
	mov	r2, #54
	str	r2, [r3, #2056]
	bne	.L405
	add	r9, r8, r9
	mov	r3, #255
	str	r3, [r9, #2052]
	mov	r3, #64
	str	r3, [r9, #2048]
	mov	r3, #204
	b	.L490
.L405:
	sub	r2, r4, #5
	cmp	r2, #1
	bhi	.L407
	ldr	r2, .L494
	ldrb	r2, [r2, #3920]	@ zero_extendqisi2
	str	r2, [r3, #2052]
	mov	r2, #82
	str	r2, [r3, #2048]
	b	.L406
.L407:
	cmp	r4, #7
	bne	.L406
	add	r9, r8, r9
	mov	r3, #174
	str	r3, [r9, #2052]
	mov	r3, #0
	str	r3, [r9, #2048]
	mov	r3, #176
.L490:
	str	r3, [r9, #2052]
	mov	r3, #77
	str	r3, [r9, #2048]
.L406:
	add	r3, r8, fp, asl #8
	cmp	r4, #6
	mov	r2, #22
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
.L446:
	add	r3, r8, fp, asl #8
	mov	r2, #48
	mov	r0, r10
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
	str	r3, [sp, #4]
	ldr	r2, [sp, #4]
	ldr	r3, [sp, #24]
	orrs	r3, r3, r2
	movne	ip, #16
	bne	.L410
	cmp	r4, #7
	movne	ip, #2
	moveq	ip, #32
.L410:
	ldr	r3, .L494+20
	add	r0, r8, fp, asl #8
	ldr	r3, [r3, #-3408]
	mov	r1, r3
.L411:
	ldr	r2, [r0, #2048]
	strb	r2, [r1], #1
	rsb	r2, r3, r1
	uxtb	r2, r2
	cmp	r2, ip
	bcc	.L411
	ldr	r2, [sp, #4]
	cmp	r2, #0
	beq	.L412
	mov	r2, #0
.L414:
	ldrb	ip, [r3, r2, asl #2]	@ zero_extendqisi2
	uxtb	r1, r2
	cmp	ip, #50
	beq	.L413
	add	ip, r3, r2, asl #2
	ldrb	ip, [ip, #1]	@ zero_extendqisi2
	cmp	ip, #5
	beq	.L413
	add	r2, r2, #1
	cmp	r2, #8
	bne	.L414
	b	.L415
.L413:
	cmp	r1, #6
	bls	.L416
.L415:
	ldr	r0, .L494+24
	mov	r1, #0
	bl	printk
.L417:
	b	.L417
.L412:
	cmp	r4, #7
	bne	.L418
	ldr	r2, [sp, #4]
.L420:
	ldrb	ip, [r3, r2, asl #2]	@ zero_extendqisi2
	uxtb	r1, r2
	cmp	ip, #12
	beq	.L419
	add	ip, r3, r2, asl #2
	ldrb	ip, [ip, #1]	@ zero_extendqisi2
	cmp	ip, #10
	beq	.L419
	add	r2, r2, #1
	cmp	r2, #8
	bne	.L420
	b	.L421
.L419:
	cmp	r1, #7
	bne	.L416
.L421:
	ldr	r0, .L494+24
	mov	r1, #0
	bl	printk
.L422:
	b	.L422
.L418:
	cmp	r4, #6
	bne	.L416
	sub	r2, r3, #1
	add	r3, r3, #7
.L423:
	ldrb	r1, [r2, #1]!	@ zero_extendqisi2
	cmp	r1, #12
	beq	.L416
	ldrb	r1, [r2, #8]	@ zero_extendqisi2
	cmp	r1, #4
	beq	.L416
	cmp	r2, r3
	bne	.L423
	ldr	r0, .L494+24
	mov	r1, #0
	bl	printk
.L425:
	b	.L425
.L416:
	ldr	r1, .L494+20
	ldr	ip, [sp, #36]
	ldr	r2, [r1, #-3408]
	add	ip, r2, ip
	mov	r3, r2
.L426:
	cmp	r3, ip
	ldrne	lr, [r0, #2048]
	strneb	lr, [r3], #1
	bne	.L426
.L493:
	ldr	r3, [r1, #-3408]
	mov	r0, #8
	ldr	r1, [sp, #8]
	add	r1, r3, r1
	str	r1, [sp, #28]
.L429:
	ldr	ip, [sp, #8]
	add	lr, r1, ip
.L428:
	ldrh	ip, [r1]
	mvn	ip, ip
	strh	ip, [r1], #2	@ movhi
	cmp	r1, lr
	bne	.L428
	ldr	ip, [sp, #8]
	subs	r0, r0, #1
	add	r1, r1, ip
	bne	.L429
.L430:
	mov	ip, #0
	mov	r0, ip
.L433:
	mov	r1, #1
	mov	lr, #0
	mov	r1, r1, asl r0
	mov	r7, #16
	str	r7, [sp, #32]
	mov	r7, lr
.L431:
	ldrh	r9, [r3, lr]
	and	r9, r9, r1
	cmp	r9, r1
	addeq	r7, r7, #1
	ldr	r9, [sp, #8]
	add	lr, lr, r9
	ldr	r9, [sp, #32]
	subs	r9, r9, #1
	str	r9, [sp, #32]
	bne	.L431
	cmp	r7, #8
	add	r0, r0, #1
	orrhi	ip, ip, r1
	uxthhi	ip, ip
	cmp	r0, #16
	bne	.L433
	ldr	r1, [sp, #28]
	strh	ip, [r3], #2	@ movhi
	cmp	r3, r1
	bne	.L430
	ldr	r3, .L494+20
	ldr	r1, [r3, #-3408]
	mov	r3, #0
	sub	r0, r1, #4
	add	ip, r1, #28
.L435:
	ldr	lr, [r0, #4]!
	cmp	lr, #0
	addeq	r3, r3, #1
	cmp	r0, ip
	bne	.L435
	cmp	r3, #7
	ble	.L436
	ldr	r0, .L494+28
	mov	r2, #1
	mov	r3, #1024
	bl	rknand_print_hex
	ldr	r0, .L494+24
	mov	r1, #0
	bl	printk
.L437:
	b	.L437
.L436:
	cmp	r4, #6
	moveq	r0, #4
	beq	.L438
	cmp	r4, #7
	moveq	r0, #10
	beq	.L438
	ldr	r3, [sp, #4]
	cmp	r3, #0
	moveq	r0, #8
	movne	r0, #5
.L438:
	sub	r9, r6, #1
	ldr	r1, [sp, #20]
	mov	ip, #0
	uxtb	r9, r9
	add	r9, r9, #1
.L439:
	mov	r7, r1
	mov	r3, r2
.L440:
	ldrb	lr, [r3], #1	@ zero_extendqisi2
	strb	lr, [r7], #1
	rsb	lr, r2, r3
	uxtb	lr, lr
	cmp	lr, r6
	bcc	.L440
	add	ip, ip, #1
	add	r2, r2, r9
	cmp	ip, r5
	add	r1, r1, r0
	blt	.L439
	add	r7, r8, fp, asl #8
	mov	r3, #255
	mov	r0, r10
	str	r3, [r7, #2056]
	bl	NandcWaitFlashReady
	ldr	r3, [sp, #24]
	cmp	r3, #0
	beq	.L442
	mov	r3, #54
	str	r3, [r7, #2056]
	ldr	r3, .L494
	mvn	r1, #0
	ldr	r0, [sp, #12]
	ldrb	r3, [r3, #3920]	@ zero_extendqisi2
	str	r3, [r7, #2052]
	mov	r3, #0
	str	r3, [r7, #2048]
	mov	r3, #22
	str	r3, [r7, #2056]
	bl	FlashReadCmd
	b	.L443
.L442:
	ldr	r3, [sp, #4]
	cmp	r3, #0
	movne	r3, #190
	moveq	r3, #56
	str	r3, [r7, #2056]
.L443:
	mov	r0, r10
	bl	NandcWaitFlashReady
	ldr	r3, [sp, #16]
	add	r3, r3, #1
	b	.L492
.L404:
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
	ldr	r3, .L494+32
	str	r3, [sp, #20]
	b	.L446
.L495:
	.align	2
.L494:
	.word	.LANCHOR0
	.word	.LANCHOR1
	.word	.LANCHOR1+3224
	.word	.LANCHOR0+3916
	.word	.LANCHOR0+3919
	.word	.LANCHOR2
	.word	.LC2
	.word	.LC3
	.word	.LANCHOR0+3944
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
	bhi	.L498
	b	HynixGetReadRetryDefault
.L498:
	cmp	r3, #49
	bne	.L499
	ldr	r0, .L510
	mov	r2, #64
	ldr	r1, .L510+4
	add	r0, r0, #3920
	strb	r3, [r0, #-4]
	mov	r3, #4
	strb	r3, [r0, #-3]
	mov	r3, #15
	strb	r3, [r0, #-2]
	b	.L508
.L499:
	sub	r2, r3, #65
	cmp	r3, #33
	cmpne	r2, #1
	ldrls	r0, .L510
	strlsb	r3, [r0, #3916]
	movls	r3, #4
	bls	.L509
.L500:
	cmp	r3, #34
	cmpne	r3, #67
	bne	.L501
	ldr	r0, .L510
	strb	r3, [r0, #3916]
	mov	r3, #5
.L509:
	strb	r3, [r0, #3917]
	mov	r2, #45
	mov	r3, #7
	add	r0, r0, #3920
	strb	r3, [r0, #-2]
	ldr	r1, .L510+8
	b	.L508
.L501:
	cmp	r3, #35
	cmpne	r3, #68
	bxne	lr
	ldr	r0, .L510
	mov	r2, #95
	ldr	r1, .L510+12
	add	r0, r0, #3920
	strb	r3, [r0, #-4]
	mov	r3, #5
	strb	r3, [r0, #-3]
	mov	r3, #17
	strb	r3, [r0, #-2]
.L508:
	b	ftl_memcpy
.L511:
	.align	2
.L510:
	.word	.LANCHOR0
	.word	.LANCHOR1+3160
	.word	.LANCHOR1+3008
	.word	.LANCHOR1+3056
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
	ldr	r3, .L517
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
	bne	.L513
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
	b	.L516
.L513:
	str	lr, [r4, #2052]
	str	ip, [r4, #2052]
	str	r1, [r4, #2052]
	str	r3, [r4, #2056]
.L516:
	str	r9, [r4, #2052]
	mov	r3, #48
	str	r8, [r4, #2052]
	mov	r0, r7
	str	r5, [r4, #2052]
	mov	r1, r6
	str	r3, [r4, #2056]
	ldmfd	sp!, {r3, r4, r5, r6, r7, r8, r9, r10, fp, lr}
	b	FlashSetRandomizer
.L518:
	.align	2
.L517:
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
	ldr	r3, .L530
	ldr	r0, [r3, #-3404]
	mov	r4, r3
	cmp	r0, #0
	beq	.L520
	mov	r0, #0
	bl	flash_enter_slc_mode
	b	.L521
.L520:
	bl	flash_exit_slc_mode
.L521:
	ldrb	r3, [r4, #-3400]	@ zero_extendqisi2
	ldr	r5, .L530
	cmp	r3, #0
	beq	.L522
	ldrb	r3, [r5, #-3412]	@ zero_extendqisi2
	tst	r3, #1
	beq	.L522
	mov	r0, #1
	bl	FlashSetInterfaceMode
	mov	r0, #1
	bl	NandcSetMode
	mov	r3, #0
	strb	r3, [r5, #-3400]
.L522:
	ldr	r3, .L530+4
	mov	r0, #0
	ldr	r3, [r3, #12]
	str	r0, [r3, #336]
	ldmfd	sp!, {r3, r4, r5, pc}
.L531:
	.align	2
.L530:
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
	ldr	r3, .L533
	add	r0, r3, r0, asl #3
	ldr	r3, [r0, #12]
	str	r1, [r3, #336]
	bx	lr
.L534:
	.align	2
.L533:
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
	ldr	r1, .L546
	bl	__aeabi_idiv
	ldr	r3, .L546+4
	ldr	r3, [r3, #3840]
	cmp	r0, #250
	movwgt	r2, #8354
	bgt	.L544
	cmp	r0, #220
	bgt	.L545
	cmp	r0, #185
	movwgt	r2, #4226
	bgt	.L544
	cmp	r0, #160
	movwgt	r2, #4194
	bgt	.L544
	cmp	r4, #35
	movwls	r2, #4193
	bls	.L544
	cmp	r4, #99
	movwls	r2, #4225
	bls	.L544
.L545:
	movw	r2, #8322
.L544:
	str	r2, [r3, #4]
	ldmfd	sp!, {r4, pc}
.L547:
	.align	2
.L546:
	.word	1000000
	.word	.LANCHOR0
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
	bls	.L549
	movw	r3, #8322
	cmp	r0, r3
	bne	.L550
.L549:
	ldr	r3, .L551
	ldr	r3, [r3, #3840]
	str	r0, [r3, #4]
.L550:
	ldr	r3, .L551+4
	ldrb	r0, [r3, #277]	@ zero_extendqisi2
	b	NandcTimeCfg
.L552:
	.align	2
.L551:
	.word	.LANCHOR0
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
	ldr	r3, .L556
	mov	r1, #0
	stmfd	sp!, {r4, r5, r6, lr}
	.save {r4, r5, r6, lr}
	mov	r2, #1
	str	r1, [r3, #16]
	mov	r4, r3
	str	r0, [r3, #12]
	mov	r6, #0
	str	r2, [r3, #24]
	mov	r2, #2
	str	r0, [r3, #20]
	str	r2, [r3, #32]
	mov	r2, #3
	str	r0, [r3, #28]
	str	r0, [r3, #36]
	str	r0, [r3, #3840]
	str	r2, [r3, #40]
	ldr	r2, [r0]
	ldr	r5, .L556+4
	and	r2, r2, #253952
	ubfx	ip, r2, #13, #1
	bfi	r2, r1, #13, #1
	ldr	r1, [r0, #352]
	orr	r2, r2, #256
	str	ip, [r5, #-3396]
	movw	ip, #2049
	ubfx	r1, r1, #16, #4
	str	r1, [r5, #-3392]
	ldr	r1, [r0, #352]
	cmp	r1, ip
	str	r1, [r5, #-3388]
	moveq	r3, #8
	streq	r3, [r5, #-3392]
	str	r2, [r0]
	mov	r0, #40
	ldr	r3, [r4, #3840]
	str	r6, [r3, #336]
	bl	NandcTimeCfg
	ldr	r3, [r4, #3840]
	movw	r2, #8322
	mov	r0, #36864
	str	r2, [r3, #344]
	ldr	r2, .L556+8
	str	r2, [r3, #304]
	bl	ftl_malloc
	str	r6, [r5, #-3356]
	str	r6, [r5, #-3348]
	str	r0, [r5, #-3384]
	str	r0, [r5, #-3380]
	add	r0, r0, #32768
	str	r0, [r5, #-3376]
	ldmfd	sp!, {r4, r5, r6, pc}
.L557:
	.align	2
.L556:
	.word	.LANCHOR0
	.word	.LANCHOR2
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
	ldr	ip, .L560
	str	lr, [sp, #-4]!
	.save {lr}
	ldr	lr, [ip, #3840]
	ldr	lr, [lr, #4]
	str	lr, [r0]
	ldr	r0, [ip, #3840]
	ldr	r0, [r0]
	str	r0, [r1]
	ldr	r1, [ip, #3840]
	ldr	r1, [r1, #304]
	str	r1, [r2]
	ldr	r1, [ip, #3840]
	ldr	r2, [r1, #308]
	ldr	r1, [r1, #344]
	uxtb	r2, r2
	orr	r2, r2, r1, asl #16
	str	r2, [r3]
	ldr	pc, [sp], #4
.L561:
	.align	2
.L560:
	.word	.LANCHOR0
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
	ldr	r3, .L570
	mov	r1, #16
	cmp	r0, r1
	ldr	r2, [r3, #3840]
	mov	r3, #1
	str	r3, [r2, #8]
	ldr	r3, .L570+4
	str	r0, [r3, #-3344]
	mov	r3, #0
	bfi	r3, r1, #8, #8
	bfc	r3, #18, #1
	bne	.L563
.L566:
	bfc	r3, #4, #1
	b	.L564
.L563:
	cmp	r0, #24
	orreq	r3, r3, #16
	beq	.L564
	cmp	r0, #40
	orr	r3, r3, #262144
	orr	r3, r3, #16
	beq	.L566
.L564:
	orr	r3, r3, #1
	str	r3, [r2, #12]
	bx	lr
.L571:
	.align	2
.L570:
	.word	.LANCHOR0
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
	ldr	r3, .L573
	strb	r0, [r3, #3905]
	b	NandcBchSel
.L574:
	.align	2
.L573:
	.word	.LANCHOR0
	.fnend
	.size	FlashBchSel, .-FlashBchSel
	.align	2
	.global	ftl_flash_resume
	.type	ftl_flash_resume, %function
ftl_flash_resume:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	ldr	r3, .L584
	stmfd	sp!, {r4, r5, r6, lr}
	.save {r4, r5, r6, lr}
	mov	r5, #0
	ldr	r2, [r3, #3840]
	mov	r4, r3
	ldr	r1, [r3, #3844]
	ldr	r6, .L584+4
	str	r1, [r2]
	ldr	r1, [r3, #3848]
	ldr	r2, [r3, #3840]
	str	r1, [r2, #4]
	ldr	r1, [r3, #3852]
	str	r1, [r2, #8]
	ldr	r1, [r3, #3856]
	str	r1, [r2, #12]
	ldr	r1, [r3, #3860]
	str	r1, [r2, #304]
	ldr	r1, [r3, #3864]
	str	r1, [r2, #308]
	ldr	r1, [r3, #3868]
	str	r1, [r2, #336]
	ldr	r1, [r3, #3872]
	str	r1, [r2, #344]
.L577:
	ldrb	r3, [r6, r5, asl #3]	@ zero_extendqisi2
	sub	r3, r3, #1
	uxtb	r3, r3
	cmp	r3, #253
	bhi	.L576
	uxtb	r0, r5
	bl	FlashReset
.L576:
	add	r5, r5, #1
	cmp	r5, #4
	bne	.L577
	ldr	r5, .L584+8
	ldrb	r3, [r5, #-3400]	@ zero_extendqisi2
	cmp	r3, #0
	beq	.L578
	mov	r0, #1
	bl	NandcSetMode
	ldrb	r0, [r5, #-3412]	@ zero_extendqisi2
	bl	FlashSetInterfaceMode
	ldrb	r0, [r5, #-3412]	@ zero_extendqisi2
	bl	NandcSetMode
	ldrb	r0, [r4, #3861]	@ zero_extendqisi2
	bl	NandcSetDdrPara
.L578:
	ldr	r3, [r4, #1784]
	ldmfd	sp!, {r4, r5, r6, lr}
	ldrb	r0, [r3, #20]	@ zero_extendqisi2
	b	FlashBchSel
.L585:
	.align	2
.L584:
	.word	.LANCHOR0
	.word	IDByte
	.word	.LANCHOR2
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
	bne	.L588
	mov	r0, r4
	ldmfd	sp!, {r4, lr}
	b	wait_for_nand_flash_ready
.L588:
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
	@ args = 0, pretend = 0, frame = 24
	@ frame_needed = 0, uses_anonymous_args = 0
	stmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.save {r4, r5, r6, r7, r8, r9, r10, fp, lr}
	mov	r8, r2
	ldr	r4, .L627
	.pad #36
	sub	sp, sp, #36
	ldr	r3, .L627+4
	mov	r9, r0
	mov	r10, r1
	ldrb	r5, [r4]	@ zero_extendqisi2
	ldr	r2, [r3]
	cmp	r5, #0
	str	r3, [sp, #12]
	str	r2, [sp, #28]
	beq	.L591
	mov	r1, r8
	bl	FlashEraseSLc2KBlocks
	b	.L592
.L600:
	mov	r3, #36
	mov	r1, #0
	mul	fp, r3, r5
	rsb	r3, r5, r8
	add	r2, sp, #20
	uxtb	r3, r3
	str	r3, [sp]
	add	r6, r9, fp
	add	r3, sp, #24
	mov	r0, r6
	bl	LogAddr2PhyAddr
	ldrb	r3, [r4, #1612]	@ zero_extendqisi2
	mov	r7, r0
	ldr	r0, [sp, #24]
	cmp	r0, r3
	mvncs	r3, #0
	strcs	r3, [r9, fp]
	bcc	.L624
.L594:
	add	r5, r5, #1
.L591:
	cmp	r5, r8
	bcc	.L600
	b	.L625
.L624:
	ldr	r3, .L627+8
	ldrb	r3, [r3, #-3340]	@ zero_extendqisi2
	cmp	r3, #0
	add	r3, r4, r0, asl #4
	ldr	r3, [r3, #1664]
	moveq	r7, #0
	cmp	r3, #0
	beq	.L596
	uxtb	r0, r0
	bl	FlashWaitCmdDone
.L596:
	ldr	r2, [sp, #24]
	mov	r0, #0
	cmp	r7, #0
	addne	ip, fp, #36
	mov	r3, r2, asl #4
	add	r2, r4, r2
	add	r1, r4, r3
	add	r3, r4, r3
	addne	ip, r9, ip
	str	r0, [r1, #1668]
	str	r6, [r1, #1664]
	ldr	r0, [sp, #20]
	ldrb	r6, [r2, #1616]	@ zero_extendqisi2
	strne	ip, [r1, #1668]
	str	r0, [r1, #1660]
	strb	r6, [r3, #1656]
	mov	r0, r6
	bl	NandcFlashCs
	cmp	r10, #1
	mov	r0, r6
	bne	.L598
	ldr	r3, .L627
	ldrb	r3, [r3, #3904]	@ zero_extendqisi2
	cmp	r3, #0
	beq	.L598
	bl	flash_enter_slc_mode
	b	.L599
.L598:
	bl	flash_exit_slc_mode
.L599:
	ldr	r3, [sp, #24]
	mov	r0, r6
	ldr	r1, [sp, #20]
	add	r5, r5, r7
	add	r3, r4, r3, asl #2
	ldr	r2, [r3, #1624]
	adds	r2, r2, #0
	movne	r2, #1
	bl	FlashWaitReadyEN
	mov	r0, r6
	mov	r2, r7
	ldr	r1, [sp, #20]
	bl	FlashEraseCmd
	mov	r0, r6
	bl	NandcFlashDeCs
	b	.L594
.L625:
	ldr	r0, [r4, #3840]
	mov	r5, #0
	bl	NandcIqrWaitFlashReady
	ldr	r6, .L627
	ldr	r7, .L627+12
.L601:
	ldrb	r3, [r4, #1612]	@ zero_extendqisi2
	cmp	r5, r3
	bcs	.L626
	uxtb	r0, r5
	bl	FlashWaitCmdDone
	cmp	r10, #1
	bne	.L602
	ldrb	r3, [r6, #3904]	@ zero_extendqisi2
	cmp	r3, #0
	beq	.L602
	ldrb	r0, [r7, r5, asl #4]	@ zero_extendqisi2
	bl	flash_exit_slc_mode
.L602:
	add	r5, r5, #1
	b	.L601
.L626:
	ldr	r3, .L627+8
	ldr	r3, [r3, #-3336]
	cmp	r3, #0
	bne	.L604
.L606:
	mov	r0, #0
	b	.L592
.L604:
	ldr	r3, .L627+16
	ldrb	r3, [r3]	@ zero_extendqisi2
	cmp	r3, #69
	bne	.L606
	mov	r3, #0
	mov	r2, #36
	mov	r1, r3
.L605:
	cmp	r3, r8
	beq	.L606
	mul	r0, r2, r3
	add	r3, r3, #1
	str	r1, [r9, r0]
	b	.L605
.L592:
	ldr	r3, [sp, #12]
	ldr	r2, [sp, #28]
	ldr	r3, [r3]
	cmp	r2, r3
	beq	.L608
	bl	__stack_chk_fail
.L608:
	add	sp, sp, #36
	@ sp needed
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L628:
	.align	2
.L627:
	.word	.LANCHOR0
	.word	__stack_chk_guard
	.word	.LANCHOR2
	.word	.LANCHOR0+1656
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
	ldr	r3, .L631
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
.L632:
	.align	2
.L631:
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
.L634:
	ldr	r3, [r0, #8]
	str	r3, [sp, #4]
	ldr	r3, [sp, #4]
	tst	r3, #1048576
	beq	.L634
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
	ldr	lr, .L656
	.pad #28
	sub	sp, sp, #28
	ldr	r5, .L656+4
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
	ldr	r3, [r5, #-3392]
	cmp	r3, #3
	bls	.L639
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
	beq	.L640
	cmp	r1, #0
	bne	.L641
.L649:
	mov	r0, r6
	mov	r10, r2
	mov	r9, r1
	bl	rk_nandc_xfer_irq_flag_init
	ldr	r3, [r6, #368]
	add	r10, r10, #1
	cmp	r8, #0
	orr	r3, r3, #1
	str	r3, [r6, #368]
	ldr	r3, [r6, #364]
	mov	r10, r10, asr #1
	bfi	r4, r10, #22, #6
	movne	r0, r8
	orr	r3, r3, #1
	str	r3, [r6, #364]
	ldreq	r0, [r5, #-3380]
	b	.L643
.L641:
	ldr	r3, [r5, #-3344]
	mov	r9, r5
	cmp	r3, #25
	movcc	r3, #64
	movcs	r3, #128
	str	r3, [sp, #4]
	mov	r3, r2, lsr #1
	str	r3, [sp, #12]
	mov	r3, #0
	mov	r0, r3
.L645:
	ldr	lr, [sp, #12]
	cmp	r0, lr
	bcs	.L649
	ldr	lr, [sp, #8]
	mov	r10, r3, lsr #2
	add	r0, r0, #1
	cmp	lr, #0
	ldrneh	fp, [ip, #2]
	mvneq	fp, #0
	ldrneh	lr, [ip], #4
	ldreq	lr, [r9, #-3376]
	orrne	lr, lr, fp, asl #16
	ldrne	fp, [r9, #-3376]
	streq	fp, [lr, r10, asl #2]
	strne	lr, [fp, r10, asl #2]
	ldr	lr, [sp, #4]
	add	r3, r3, lr
	b	.L645
.L643:
	ldr	r3, [r5, #-3376]
	ubfx	r10, r4, #22, #5
	mov	r2, r9
	str	r0, [r5, #-3372]
	mov	r1, r10, asl #10
	str	r3, [r5, #-3368]
	bl	rknand_dma_map_single
	mov	r2, r9
	mov	r1, r10, asl #7
	clz	r9, r9
	mov	r9, r9, lsr #5
	str	r0, [r5, #-3364]
	ldr	r0, [r5, #-3368]
	bl	rknand_dma_map_single
	mov	r3, #1
	str	r3, [r5, #-3356]
	tst	r8, #3
	ldr	r3, [r5, #-3364]
	str	r0, [r5, #-3360]
	str	r3, [r6, #20]
	ldr	r3, [r5, #-3360]
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
.L640:
	ldr	r3, [sp, #20]
	str	r3, [r6, #16]
.L639:
	str	r7, [r6, #12]
	str	r4, [r6, #8]
	orr	r4, r4, #4
	str	r4, [r6, #8]
	add	sp, sp, #28
	@ sp needed
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L657:
	.align	2
.L656:
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
	ldr	r5, .L698
	ldr	r3, .L698+4
	add	r0, r3, r0, asl #3
	ldr	r3, [r5, #-3392]
	cmp	r3, #3
	ldr	r4, [r0, #12]
	bls	.L689
	ldr	r3, [r4, #16]
	tst	r3, #4
	beq	.L689
	mov	r0, r4
	bl	wait_for_nandc_xfer_completed
	ldr	r6, [r4, #16]
	ldr	r3, [r4, #8]
	ubfx	r6, r6, #1, #1
	cmp	r6, #0
	movne	r6, #0
	str	r3, [sp]
	beq	.L671
.L661:
	ldr	r2, [r4, #28]
	ldr	r3, [sp]
	ubfx	r2, r2, #16, #5
	ubfx	r3, r3, #22, #6
	cmp	r2, r3
	bge	.L669
	ldr	r3, [r5, #-3392]
	cmp	r3, #5
	bhi	.L662
.L665:
	add	r6, r6, #1
	bic	r3, r6, #-16777216
	cmp	r3, #0
	bne	.L661
	ldr	r2, [r4, #28]
	mov	r1, r6
	ldr	r3, [sp]
	ubfx	r2, r2, #16, #5
	ldr	r0, .L698+8
	ubfx	r3, r3, #22, #6
	bl	printk
	ldr	r0, .L698+12
	mov	r1, r4
	mov	r2, #4
	mov	r3, #512
	bl	rknand_print_hex
	b	.L661
.L662:
	ldr	r3, [r4]
	str	r3, [sp, #4]
	ldr	r3, [sp, #4]
	tst	r3, #8192
	beq	.L665
	ldr	r3, [sp, #4]
	tst	r3, #131072
	beq	.L665
.L669:
	ldr	r3, [r5, #-3356]
	ldr	r4, .L698
	cmp	r3, #0
	beq	.L670
	ldr	r1, [sp]
	mov	r2, #0
	ldr	r0, [r4, #-3364]
	ubfx	r1, r1, #22, #5
	mov	r1, r1, asl #10
	bl	rknand_dma_unmap_single
	ldr	r0, [r4, #-3360]
	mov	r2, #0
	ldr	r1, [sp]
	ubfx	r1, r1, #22, #5
	mov	r1, r1, asl #7
	bl	rknand_dma_unmap_single
	b	.L670
.L673:
	ldr	r3, [r4, #8]
	add	r6, r6, #1
	str	r3, [sp]
	bic	r3, r6, #-16777216
	cmp	r3, #0
	bne	.L671
	ldr	r2, [sp]
	mov	r1, r6
	ldr	r3, [r4, #28]
	ldr	r0, .L698+16
	ubfx	r3, r3, #16, #5
	bl	printk
	ldr	r0, .L698+12
	mov	r1, r4
	mov	r2, #4
	mov	r3, #512
	bl	rknand_print_hex
.L671:
	ldr	r3, [sp]
	tst	r3, #1048576
	beq	.L673
	ldr	r3, [r5, #-3348]
	cmp	r3, #0
	beq	.L674
	mov	r0, r4
	bl	NandcSendDumpDataStart
.L674:
	ldr	r3, [r5, #-3356]
	ldr	r6, .L698
	cmp	r3, #0
	beq	.L675
	ldr	r1, [sp]
	mov	r2, #1
	ldr	r0, [r6, #-3364]
	ubfx	r1, r1, #22, #5
	mov	r1, r1, asl #10
	bl	rknand_dma_unmap_single
	ldr	r0, [r6, #-3360]
	mov	r2, #1
	ldr	r1, [sp]
	ubfx	r1, r1, #22, #5
	mov	r1, r1, asl #7
	bl	rknand_dma_unmap_single
.L675:
	ldr	r3, [r5, #-3348]
	cmp	r3, #0
	beq	.L670
	mov	r0, r4
	bl	NandcSendDumpDataDone
.L670:
	mov	r3, #0
	str	r3, [r5, #-3356]
	b	.L658
.L689:
	ldr	r3, [r4, #8]
	str	r3, [sp]
	ldr	r3, [sp]
	tst	r3, #1048576
	beq	.L689
.L658:
	add	sp, sp, #8
	@ sp needed
	ldmfd	sp!, {r4, r5, r6, pc}
.L699:
	.align	2
.L698:
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
	bne	.L701
	cmp	r3, #0
	beq	.L702
	mov	r0, r2
	mov	r1, r3
	mov	r2, #1024
	bl	ftl_memcpy
.L702:
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
.L701:
	cmp	r3, #0
	beq	.L705
	mov	r1, r2
	mov	r0, r3
	mov	r2, #1024
	bl	ftl_memcpy
.L705:
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
	@ args = 4, pretend = 0, frame = 88
	@ frame_needed = 0, uses_anonymous_args = 0
	stmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.save {r4, r5, r6, r7, r8, r9, r10, fp, lr}
	mov	fp, r3
	ldr	r3, .L766
	mov	r8, r2
	ldr	r4, .L766+4
	.pad #100
	sub	sp, sp, #100
	tst	fp, #63
	mov	r10, r0
	ldr	r2, [r3]
	add	ip, r4, r0, asl #3
	mov	r7, r1
	ldr	r5, [sp, #136]
	ldr	r9, [ip, #12]
	str	r2, [sp, #92]
	str	r3, [sp, #16]
	bne	.L718
	cmp	r5, #0
	bne	.L719
	add	r0, sp, #28
	mov	r1, #255
	mov	r2, #64
	bl	ftl_memset
	add	r5, sp, #28
.L719:
	mov	r0, r10
	mov	r1, r7
	mov	r2, r8
	mov	r3, #0
	str	fp, [sp]
	str	r5, [sp, #4]
	bl	NandcXferStart
	mov	r0, r10
	bl	NandcXferComp
	cmp	r7, #0
	movne	r6, #0
	bne	.L720
	ldr	r4, .L766+8
	mov	r1, r8, lsr #1
	mov	r2, r7
	ldr	r3, [r4, #-3344]
	cmp	r3, #25
	mov	r3, r7
	movcc	r0, #64
	movcs	r0, #128
.L722:
	cmp	r2, r1
	add	r5, r5, #4
	add	ip, r3, r0
	bcs	.L764
	ldr	lr, [r4, #-3376]
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
	mov	r3, ip
	b	.L722
.L764:
	ldr	r0, [r4, #-3344]
	mov	r2, #0
	ldr	r1, [r4, #-3392]
	mov	r8, r8, lsr #2
	mov	r6, r2
.L724:
	cmp	r2, r8
	bcs	.L720
	cmp	r0, #0
	beq	.L720
	add	r3, r2, #8
	ldr	r3, [r9, r3, asl #2]
	str	r3, [sp, #24]
	ldr	r3, [sp, #24]
	tst	r3, #4
	bne	.L749
	ldr	r4, [sp, #24]
	ubfx	r4, r4, #15, #1
	cmp	r4, #0
	bne	.L749
	cmp	r1, #5
	bls	.L726
	ldr	ip, [sp, #24]
	ldr	r4, [sp, #24]
	ldr	r3, [sp, #24]
	ubfx	ip, ip, #3, #5
	ldr	lr, [sp, #24]
	ubfx	r4, r4, #27, #1
	ubfx	r3, r3, #16, #5
	ubfx	lr, lr, #29, #1
	orr	ip, ip, r4, asl #5
	orr	r3, r3, lr, asl #5
	cmp	ip, r3
	ldr	r3, [sp, #24]
	ldrhi	r4, [sp, #24]
	ubfxhi	r3, r3, #3, #5
	ldrls	r4, [sp, #24]
	ubfxls	r3, r3, #16, #5
	ubfxhi	r4, r4, #27, #1
	ubfxls	r4, r4, #29, #1
	b	.L763
.L726:
	cmp	r1, #3
	bls	.L728
	ldr	ip, [sp, #24]
	ldr	r4, [sp, #24]
	ldr	r3, [sp, #24]
	ubfx	ip, ip, #3, #5
	ldr	lr, [sp, #24]
	ubfx	r4, r4, #28, #1
	ubfx	r3, r3, #16, #5
	ubfx	lr, lr, #30, #1
	orr	ip, ip, r4, asl #5
	orr	r3, r3, lr, asl #5
	cmp	ip, r3
	bls	.L729
	ldr	r3, [sp, #24]
	ldr	r4, [sp, #24]
	ubfx	r3, r3, #3, #5
	ubfx	r4, r4, #28, #1
.L763:
	orr	r4, r3, r4, asl #5
	b	.L728
.L729:
	ldr	r5, [sp, #24]
	ldr	r4, [sp, #24]
	ubfx	r5, r5, #16, #5
	ubfx	r4, r4, #30, #1
	orr	r4, r5, r4, asl #5
.L728:
	cmp	r6, r4
	movcc	r6, r4
	b	.L725
.L749:
	mvn	r6, #0
.L725:
	add	r2, r2, #1
	b	.L724
.L720:
	mov	r3, #0
	str	r3, [r9, #16]
	b	.L731
.L718:
	cmp	r1, #1
	mov	r4, #0
	bne	.L761
	mov	r6, r4
.L732:
	cmp	r4, r8
	bcs	.L765
	cmp	fp, #0
	and	ip, r4, #3
	addne	r3, fp, r4, asl #9
	mov	r0, r9
	moveq	r3, fp
	cmp	r5, #0
	mov	r1, #1
	str	ip, [sp, #12]
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
	ldr	ip, [sp, #12]
	str	r6, [sp]
	str	r6, [sp, #4]
	mov	r3, ip
	bl	NandcXferStart
	mov	r0, r10
	bl	NandcXferComp
	b	.L732
.L765:
	mov	r6, #0
	b	.L731
.L761:
	str	r4, [sp]
	mov	r1, r4
	str	r4, [sp, #4]
	mov	r2, #2
	mov	r3, r4
	mov	r6, r4
	bl	NandcXferStart
	str	fp, [sp, #12]
.L737:
	cmp	r4, r8
	bcs	.L731
	mov	r0, r10
	bl	NandcXferComp
	ldr	r3, [r9, #32]
	add	ip, r4, #2
	cmp	ip, r8
	str	r3, [sp, #24]
	bcs	.L738
	mov	r3, #0
	mov	r0, r10
	str	r3, [sp]
	mov	r1, r3
	str	r3, [sp, #4]
	mov	r2, #2
	and	r3, ip, #3
	str	ip, [sp, #20]
	bl	NandcXferStart
	ldr	ip, [sp, #20]
.L738:
	ldr	r3, [sp, #24]
	tst	r3, #4
	mvnne	r6, #0
	bne	.L739
	ldr	r2, [sp, #24]
	ldr	r3, [sp, #24]
	ubfx	r2, r2, #3, #5
	ubfx	r3, r3, #27, #1
	orr	r3, r2, r3, asl #5
	cmp	r6, r3
	movcc	r6, r3
.L739:
	cmp	fp, #0
	ldr	r3, [sp, #12]
	sub	r2, ip, #2
	mov	r0, r9
	moveq	r3, #0
	cmp	r5, #0
	and	r2, r2, #3
	str	ip, [sp, #20]
	movne	r1, #2
	moveq	r1, #0
	mla	r4, r4, r1, r5
	mov	r1, #0
	str	r4, [sp]
	bl	NandcCopy1KB
	ldr	ip, [sp, #20]
	ldr	r3, [sp, #12]
	mov	r4, ip
	add	r3, r3, #1024
	str	r3, [sp, #12]
	b	.L737
.L731:
	ldr	r3, .L766+8
	clz	r7, r7
	mov	r7, r7, lsr #5
	ldr	r3, [r3, #-3392]
	cmp	r3, #5
	movls	r7, #0
	cmp	r7, #0
	beq	.L743
	ldr	r3, [r9]
	and	r2, r3, #139264
	cmp	r2, #139264
	orreq	r3, r3, #131072
	streq	r3, [r9]
	mvneq	r6, #0
.L743:
	ldr	r3, [sp, #16]
	mov	r0, r6
	ldr	r2, [sp, #92]
	ldr	r3, [r3]
	cmp	r2, r3
	beq	.L745
	bl	__stack_chk_fail
.L745:
	add	sp, sp, #100
	@ sp needed
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L767:
	.align	2
.L766:
	.word	__stack_chk_guard
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
	ldr	r3, .L771
	subs	r4, r0, #0
	mov	r6, r1
	mov	r7, r2
	ldrb	r5, [r3, #265]	@ zero_extendqisi2
	bne	.L769
	ldr	r2, .L771+4
	ldrb	r3, [r2, #1]	@ zero_extendqisi2
	ldr	r2, [r2, #4]
	mul	r2, r2, r3
	cmp	r1, r2
	movcc	r5, #4
.L769:
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
.L772:
	.align	2
.L771:
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
	ldr	r5, .L800
	.pad #20
	sub	sp, sp, #20
	ldr	r4, .L800+4
	mov	r10, r0
	str	r1, [sp]
	mov	fp, r2
	ldr	r3, [r5, #3840]
	ldr	r3, [r3, #304]
	str	r3, [sp, #8]
	ldr	r3, [r4, #-3392]
	cmp	r3, #8
	ldr	r3, [sp, #56]
	movcc	r9, #6
	movcs	r9, #12
	cmp	r3, #0
	moveq	r6, #1024
	beq	.L775
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
	ldrb	r0, [r4, #-3412]	@ zero_extendqisi2
	bl	FlashSetInterfaceMode
	ldrb	r0, [r4, #-3412]	@ zero_extendqisi2
	bl	NandcSetMode
	cmn	r6, #1
	bne	.L776
.L785:
	mvn	r6, #0
	b	.L777
.L776:
	mov	r2, r6
	ldr	r0, .L800+8
	ldr	r1, [sp]
	bl	printk
	cmp	r6, #9
	addls	r5, r5, r10, asl #3
	ldrls	r3, [r5, #12]
	ldrls	r2, [r3, #3840]
	ldrls	r2, [r3]
	orrls	r2, r2, #131072
	strls	r2, [r3]
	ldr	r3, [r4, #-3332]
	add	r3, r3, #1
	str	r3, [r4, #-3332]
	cmp	r3, #2048
	bcc	.L777
	ldr	r3, .L800+4
	mov	r7, #0
	mov	fp, r7
	str	r7, [r3, #-3332]
.L775:
	mov	r4, #0
	mvn	r8, #0
	mov	ip, r4
	mov	r5, r4
	str	r4, [sp, #4]
.L783:
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
	bhi	.L779
	cmp	r0, #2
	bhi	.L789
	add	r5, r5, #1
	cmp	r5, #9
	bls	.L789
	rsb	r4, r5, r9
	mov	r6, r0
	mov	r8, #0
	b	.L781
.L779:
	ldr	r3, [sp, #4]
	cmp	r3, r5
	bcs	.L790
	cmp	r5, #7
	rsb	ip, r5, r4
	bhi	.L791
	str	r5, [sp, #4]
	b	.L790
.L789:
	mov	r8, #0
	mov	r4, r9
	mov	r6, r0
	mov	r7, r8
	mov	fp, r8
	b	.L780
.L790:
	mov	r5, #0
.L780:
	add	r9, r9, #2
	cmp	r9, #69
	bls	.L783
.L781:
	ldr	r3, [sp, #4]
	cmp	r3, r5
	movcs	r4, ip
	b	.L782
.L791:
	mov	r4, ip
.L782:
	cmp	r4, #0
	beq	.L784
	ldr	r0, .L800+12
	mov	r1, r4
	bl	printk
	uxtb	r0, r4
	bl	NandcSetDdrPara
.L784:
	cmn	r8, #1
	bne	.L777
	ldr	r0, .L800+16
	mov	r1, r10
	ldr	r2, [sp]
	bl	printk
	ldr	r3, [sp, #56]
	cmp	r3, #0
	beq	.L785
	ldr	r3, [sp, #8]
	ubfx	r0, r3, #8, #8
	bl	NandcSetDdrPara
.L777:
	mov	r0, r6
	add	sp, sp, #20
	@ sp needed
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L801:
	.align	2
.L800:
	.word	.LANCHOR0
	.word	.LANCHOR2
	.word	.LC7
	.word	.LC8
	.word	.LC9
	.fnend
	.size	FlashDdrTunningRead, .-FlashDdrTunningRead
	.align	2
	.global	FlashReadPage
	.type	FlashReadPage, %function
FlashReadPage:
	.fnstart
	@ args = 0, pretend = 0, frame = 8
	@ frame_needed = 0, uses_anonymous_args = 0
	stmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.save {r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.pad #20
	sub	sp, sp, #20
	mov	r5, r0
	mov	r6, r1
	mov	r8, r2
	mov	r7, r3
	bl	FlashReadRawPage
	ldr	r10, .L823
	cmn	r0, #1
	mov	r4, r0
	bne	.L803
	ldr	r9, .L823+4
	ldrb	fp, [r9, #8]	@ zero_extendqisi2
	mov	ip, r9
	cmp	fp, #0
	bne	.L804
.L806:
	ldrb	r3, [r10, #-3400]	@ zero_extendqisi2
	cmp	r3, #0
	beq	.L803
	b	.L822
.L804:
	mov	r3, #0
	mov	r0, r5
	strb	r3, [r9, #8]
	mov	r1, r6
	mov	r2, r8
	mov	r3, r7
	str	r9, [sp, #12]
	bl	FlashReadRawPage
	strb	fp, [r9, #8]
	cmn	r0, #1
	ldr	ip, [sp, #12]
	movne	r4, r0
	beq	.L806
	b	.L803
.L822:
	ldr	r3, [ip, #3840]
	mov	r0, r5
	mov	r1, r6
	mov	r2, r8
	ldr	r9, [r3, #304]
	mov	r3, #1
	str	r3, [sp]
	mov	r3, r7
	bl	FlashDdrTunningRead
	cmn	r0, #1
	mov	r4, r0
	beq	.L807
	ldr	r3, .L823+4
	ldrb	r3, [r3, #3905]	@ zero_extendqisi2
	cmp	r0, r3, lsr #1
	bls	.L803
.L807:
	ubfx	r0, r9, #8, #8
	bl	NandcSetDdrPara
.L803:
	ldr	ip, [r10, #-3328]
	adds	r3, ip, #0
	movne	r3, #1
	cmn	r4, #1
	movne	r3, #0
	cmp	r3, #0
	beq	.L808
	mov	r1, r6
	mov	r2, r8
	mov	r3, r7
	mov	r0, r5
	blx	ip
	mov	r2, r5
	mov	r3, r6
	mov	r4, r0
	ldr	r0, .L823+8
	mov	r1, r4
	bl	printk
	cmn	r4, #1
	bne	.L808
	ldr	r3, .L823+4
	ldrb	r3, [r3, #3904]	@ zero_extendqisi2
	cmp	r3, #0
	beq	.L808
	mov	r0, r5
	bl	flash_enter_slc_mode
	ldr	r3, .L823
	mov	r0, r5
	mov	r1, r6
	mov	r2, r8
	ldr	ip, [r3, #-3328]
	mov	r3, r7
	blx	ip
	mov	r4, r0
	mov	r0, r5
	bl	flash_exit_slc_mode
.L808:
	mov	r0, r4
	add	sp, sp, #20
	@ sp needed
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L824:
	.align	2
.L823:
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
	ldr	r5, .L835
	mov	r6, r1
	mov	r4, #0
	ldrb	r0, [r5, #-3412]	@ zero_extendqisi2
	bl	FlashSetInterfaceMode
	ldrb	r0, [r5, #-3412]	@ zero_extendqisi2
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
	bne	.L826
	ldrb	r2, [r5, #-3412]	@ zero_extendqisi2
	tst	r2, #1
	beq	.L826
	mov	r0, #1
	bl	FlashSetInterfaceMode
	mov	r0, #1
	bl	NandcSetMode
	strb	r4, [r5, #-3400]
	b	.L827
.L826:
	mov	r2, #1
	strb	r2, [r3, #-3400]
.L827:
	mov	r0, #0
	add	sp, sp, #8
	@ sp needed
	ldmfd	sp!, {r4, r5, r6, r7, r8, pc}
.L836:
	.align	2
.L835:
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
	ldr	r10, .L854
	mov	r6, #0
	ldr	r8, .L854+4
	mov	r4, r6
	ldr	r5, .L854+8
	mov	r9, #4
	ldr	r3, [r10]
	mvn	r7, #0
	str	r6, [r5, #-3320]
	str	r3, [sp, #12]
	ldr	r3, .L854+12
	ldr	r0, [r3]	@ unaligned
	ldr	r3, [r8, #4]
	str	r0, [sp, #8]	@ unaligned
	mov	r0, r6
	str	r3, [sp]
	mov	r6, r5
	ldr	r3, [r5, #-3408]
	str	r3, [r5, #-3324]
	bl	flash_enter_slc_mode
	str	r10, [sp, #4]
.L838:
	add	fp, r4, #1
	mov	r10, #0
.L840:
	add	r3, sp, #8
	ldrb	r0, [r3, r10]	@ zero_extendqisi2
	bl	FlashBchSel
	mov	r0, #0
	mov	r1, r4
	ldr	r2, [r5, #-3408]
	mov	r3, r0
	bl	FlashReadRawPage
	cmn	r0, #1
	bne	.L839
	mov	r0, #0
	mov	r1, fp
	ldr	r2, [r6, #-3408]
	mov	r3, r0
	bl	FlashReadRawPage
	cmn	r0, #1
	bne	.L839
	add	r10, r10, #1
	cmp	r10, #4
	beq	.L841
	b	.L840
.L842:
	add	r0, fp, #12
	movw	r1, #2036
	bl	js_hash
	ldr	r3, [fp, #8]
	cmp	r3, r0
	mvnne	r7, #0
	bne	.L841
	ldr	r7, .L854+16
	add	r1, fp, #160
	mov	r2, #32
	mov	r0, r7
	bl	ftl_memcpy
	ldr	r1, [r6, #-3324]
	mov	r2, #32
	ldr	r0, .L854+20
	add	r1, r1, #192
	bl	ftl_memcpy
	ldr	r1, [r6, #-3324]
	mov	r2, #852
	ldr	r0, .L854+24
	add	r1, r1, #224
	bl	ftl_memcpy
	ldrh	r0, [r7, #10]
	bl	FlashBlockAlignInit
	ldr	r7, [r6, #-3324]
	str	r4, [r6, #-3320]
	mov	r0, r4
	ldr	r1, [r8, #4]
	ldr	r3, [r7, #1076]
	strb	r3, [r6, #-3400]
	bl	__aeabi_uidiv
	add	r0, r0, #1
	cmp	r0, #1
	strhi	r0, [r6, #-3316]
	movls	r3, #2
	strls	r3, [r6, #-3316]
	ldrh	r3, [r7, #14]
	mov	r7, #0
	strb	r3, [r5, #-3312]
.L841:
	ldr	r3, [sp]
	subs	r9, r9, #1
	add	r4, r4, r3
	bne	.L838
	mov	r0, r9
.L853:
	bl	flash_exit_slc_mode
	mov	r0, r7
	ldr	r3, [sp, #4]
	ldr	r2, [sp, #12]
	ldr	r3, [r3]
	cmp	r2, r3
	beq	.L848
	bl	__stack_chk_fail
.L839:
	ldr	fp, [r5, #-3324]
	ldr	r2, .L854+28
	ldr	r3, [fp]
	cmp	r3, r2
	bne	.L841
	cmp	r7, #0
	bne	.L842
	mov	r0, r4
	ldr	r1, [r8, #4]
	bl	__aeabi_uidiv
	ldr	r3, .L854+8
	add	r0, r0, #1
	str	r0, [r3, #-3316]
	mov	r0, r7
	b	.L853
.L848:
	add	sp, sp, #20
	@ sp needed
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L855:
	.align	2
.L854:
	.word	__stack_chk_guard
	.word	.LANCHOR0
	.word	.LANCHOR2
	.word	.LANCHOR3
	.word	.LANCHOR1+256
	.word	.LANCHOR0+44
	.word	.LANCHOR0+3916
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
	mov	fp, r3
	str	r2, [sp, #4]
	str	r1, [sp, #12]
	bl	NandcWaitFlashReady
	ldr	r2, .L885
	add	r3, r2, r8, asl #3
	ldr	r5, [r3, #12]
	ldrb	r9, [r3, #16]	@ zero_extendqisi2
	ldr	r3, .L885+4
	add	r6, r9, #8
	ldrb	r7, [r3]	@ zero_extendqisi2
	add	r6, r5, r6, asl #8
	sub	r4, r7, #67
	cmp	r4, #1
	str	r3, [sp, #8]
	movls	r3, #0
	str	r2, [sp, #16]
	strls	r3, [sp]
	bls	.L857
	ldr	r3, .L885+8
	ldrb	r4, [r3, #-3400]	@ zero_extendqisi2
	cmp	r4, #0
	streq	r4, [sp]
	beq	.L858
	mov	r0, #0
	bl	NandcSetDdrMode
	mov	r3, #1
	str	r3, [sp]
.L858:
	add	r3, r5, r9, asl #8
	mov	r2, #92
	str	r2, [r3, #2056]
	mov	r2, #197
	str	r2, [r3, #2056]
.L857:
	mov	r7, #1
	mvn	r10, #0
	mov	r3, r9, asl #8
	str	r3, [sp, #20]
.L859:
	ldr	r3, .L885+12
	ldrb	r3, [r3]	@ zero_extendqisi2
	add	r3, r3, #1
	cmp	r7, r3
	bcs	.L884
	ldr	r3, [sp, #8]
	mov	r0, r6
	uxtb	r1, r7
	ldrb	r3, [r3]	@ zero_extendqisi2
	sub	r3, r3, #67
	cmp	r3, #1
	bhi	.L860
	bl	SandiskSetRRPara
	b	.L861
.L860:
	bl	ToshibaSetRRPara
.L861:
	ldr	r3, [sp, #8]
	ldrb	r3, [r3]	@ zero_extendqisi2
	cmp	r3, #34
	bne	.L862
	ldr	r3, .L885+12
	ldrb	r3, [r3]	@ zero_extendqisi2
	sub	r3, r3, #3
	cmp	r7, r3
	ldreq	r3, [sp, #20]
	moveq	r2, #179
	addeq	r3, r5, r3
	streq	r2, [r3, #2056]
.L862:
	ldr	r3, [sp, #20]
	mov	r2, #38
	add	r3, r5, r3
	str	r2, [r3, #2056]
	mov	r2, #93
	str	r2, [r3, #2056]
	ldr	r3, [sp]
	cmp	r3, #0
	beq	.L863
	mov	r0, #4
	bl	NandcSetDdrMode
	mov	r0, r8
	mov	r3, fp
	ldr	r1, [sp, #12]
	ldr	r2, [sp, #4]
	bl	FlashReadRawPage
	mov	r4, r0
	mov	r0, #0
	bl	NandcSetDdrMode
	b	.L864
.L863:
	mov	r0, r8
	ldr	r1, [sp, #12]
	ldr	r2, [sp, #4]
	mov	r3, fp
	bl	FlashReadRawPage
	mov	r4, r0
.L864:
	cmn	r4, #1
	beq	.L865
	ldr	r3, [sp, #16]
	cmn	r10, #1
	moveq	r10, r4
	ldrb	r2, [r3, #3905]	@ zero_extendqisi2
	add	r2, r2, r2, asl #1
	cmp	r4, r2, asr #2
	bcc	.L867
	mov	fp, #0
	str	fp, [sp, #4]
.L865:
	add	r7, r7, #1
	b	.L859
.L884:
	mov	r4, r10
.L867:
	ldr	r3, [sp, #8]
	mov	r0, r6
	mov	r1, #0
	ldrb	r2, [r3]	@ zero_extendqisi2
	sub	r2, r2, #67
	cmp	r2, #1
	bhi	.L869
	bl	SandiskSetRRPara
	b	.L870
.L869:
	bl	ToshibaSetRRPara
.L870:
	ldr	r3, [sp, #16]
	add	r5, r5, r9, asl #8
	mov	r2, #255
	str	r2, [r5, #2056]
	ldrb	r2, [r3, #3905]	@ zero_extendqisi2
	add	r2, r2, r2, asl #1
	cmp	r4, r2, asr #2
	bcc	.L871
	cmn	r4, #1
	movne	r4, #256
.L871:
	mov	r0, r8
	bl	NandcWaitFlashReady
	ldr	r3, [sp]
	cmp	r3, #0
	beq	.L872
	mov	r0, #4
	bl	NandcSetDdrMode
.L872:
	mov	r0, r4
	add	sp, sp, #28
	@ sp needed
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L886:
	.align	2
.L885:
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
	mov	r9, r0
	ldr	r5, .L901
	mov	r10, r2
	mov	r8, r3
	mov	fp, r1
	bl	NandcWaitFlashReady
	add	r2, r5, r9, asl #3
	ldr	ip, .L901+4
	mov	r7, #1
	ldrb	r3, [r2, #16]	@ zero_extendqisi2
	mvn	r4, #0
	ldr	r6, [r2, #12]
	add	r3, r3, #8
	add	r6, r6, r3, asl #8
.L888:
	ldrb	r3, [ip]	@ zero_extendqisi2
	add	r3, r3, #1
	cmp	r7, r3
	bcs	.L891
	mov	r0, r6
	uxtb	r1, r7
	str	ip, [sp, #4]
	bl	SamsungSetRRPara
	mov	r0, r9
	mov	r1, fp
	mov	r2, r10
	mov	r3, r8
	bl	FlashReadRawPage
	cmn	r0, #1
	ldr	ip, [sp, #4]
	beq	.L889
	ldrb	r3, [r5, #3905]	@ zero_extendqisi2
	cmn	r4, #1
	moveq	r4, r0
	add	r3, r3, r3, asl #1
	cmp	r0, r3, asr #2
	bcc	.L894
	mov	r8, #0
	mov	r10, r8
.L889:
	add	r7, r7, #1
	b	.L888
.L894:
	mov	r4, r0
.L891:
	mov	r0, r6
	mov	r1, #0
	bl	SamsungSetRRPara
	ldrb	r3, [r5, #3905]	@ zero_extendqisi2
	add	r3, r3, r3, asl #1
	cmp	r4, r3, asr #2
	bcc	.L893
	cmn	r4, #1
	movne	r4, #256
.L893:
	mov	r0, r4
	add	sp, sp, #12
	@ sp needed
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L902:
	.align	2
.L901:
	.word	.LANCHOR0
	.word	g_maxRetryCount
	.fnend
	.size	SamsungReadRetrial, .-SamsungReadRetrial
	.align	2
	.global	MicronReadRetrial
	.type	MicronReadRetrial, %function
MicronReadRetrial:
	.fnstart
	@ args = 0, pretend = 0, frame = 24
	@ frame_needed = 0, uses_anonymous_args = 0
.L905:
	stmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.save {r4, r5, r6, r7, r8, r9, r10, fp, lr}
	mov	r8, r3
	ldr	r3, .L929
	mov	fp, r2
	.pad #36
	sub	sp, sp, #36
	mov	r6, r0
	mov	r10, #0
	ldrb	r5, [r3, #3905]	@ zero_extendqisi2
	ldrb	r3, [r3, #3904]	@ zero_extendqisi2
	str	r1, [sp, #20]
	cmp	r3, #0
	addeq	r5, r5, r5, asl #1
	ldrne	r2, .L929+4
	ubfxeq	r5, r5, #2, #8
	smullne	r2, r3, r5, r2
	uxtbne	r5, r3
	ldr	r3, .L929
	add	r3, r3, r0, asl #3
	str	r3, [sp, #24]
.L915:
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
.L906:
	ldr	r3, .L929+8
	ldrb	r3, [r3]	@ zero_extendqisi2
	cmp	r9, r3
	bcs	.L909
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
	beq	.L907
	cmn	r4, #1
	ldr	ip, [sp, #28]
	moveq	r4, r0
	cmp	r0, r5
	bcc	.L917
	mov	r8, ip
	mov	fp, ip
.L907:
	ldr	r9, [sp, #8]
	b	.L906
.L917:
	mov	r4, r0
	mov	r8, ip
	mov	fp, ip
.L909:
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
	bcc	.L911
	cmn	r4, #1
	movne	r4, #256
.L911:
	cmn	r4, #1
	movne	r7, #0
	moveq	r7, #1
	cmp	r4, #256
	movne	r1, r7
	orreq	r1, r7, #1
	cmp	r1, #0
	beq	.L912
	str	r4, [sp]
	mov	r1, r9
	ldr	r0, .L929+12
	mov	r3, r9
	ldr	r2, [sp, #20]
	bl	printk
	cmp	r10, #0
	bne	.L913
	ldr	r3, .L929
	ldrb	r3, [r3, #3904]	@ zero_extendqisi2
	cmp	r3, #0
	moveq	r7, #0
	andne	r7, r7, #1
	cmp	r7, #0
	beq	.L922
	mov	r0, r6
	mov	r1, #3
	bl	micron_auto_read_calibration_config
	mov	r10, #1
	b	.L915
.L913:
	mov	r0, r6
	mov	r1, #0
	bl	micron_auto_read_calibration_config
	cmn	r4, #1
	movne	r4, #256
	b	.L922
.L912:
	cmp	r10, #0
	beq	.L922
	mov	r0, r6
	mov	r4, #256
	bl	micron_auto_read_calibration_config
.L922:
	mov	r0, r4
	add	sp, sp, #36
	@ sp needed
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L930:
	.align	2
.L929:
	.word	.LANCHOR0
	.word	1431655766
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
	mov	r8, r3
	ldr	r5, .L949
	mov	r10, r2
	mov	r7, r0
	mov	fp, r1
	add	r2, r5, r0
	mvn	r6, #0
	ldr	r3, [r5, #1784]
	ldrb	r4, [r2, #3928]	@ zero_extendqisi2
	ldrb	r9, [r5, #3918]	@ zero_extendqisi2
	ldrb	r3, [r3, #19]	@ zero_extendqisi2
	sub	r3, r3, #7
	cmp	r3, #1
	ldrlsb	r4, [r2, #3936]	@ zero_extendqisi2
	bl	NandcWaitFlashReady
	mov	ip, #0
.L933:
	cmp	ip, r9
	bcs	.L937
	add	r4, r4, #1
	mov	r0, r7
	ldrb	r1, [r5, #3917]	@ zero_extendqisi2
	uxtb	r4, r4
	ldr	r2, .L949+4
	cmp	r4, r9
	str	ip, [sp, #4]
	movcs	r4, #0
	mov	r3, r4
	bl	HynixSetRRPara
	mov	r0, r7
	mov	r1, fp
	mov	r2, r10
	mov	r3, r8
	bl	FlashReadRawPage
	cmn	r0, #1
	ldr	ip, [sp, #4]
	beq	.L935
	ldrb	r3, [r5, #3905]	@ zero_extendqisi2
	cmn	r6, #1
	moveq	r6, r0
	add	r3, r3, r3, asl #1
	cmp	r0, r3, asr #2
	bcc	.L942
	mov	r8, #0
	mov	r10, r8
.L935:
	add	ip, ip, #1
	b	.L933
.L942:
	mov	r6, r0
.L937:
	ldr	r3, [r5, #1784]
	add	r7, r5, r7
	ldrb	r3, [r3, #19]	@ zero_extendqisi2
	sub	r3, r3, #7
	cmp	r3, #1
	ldrb	r3, [r5, #3905]	@ zero_extendqisi2
	strlsb	r4, [r7, #3936]
	strhib	r4, [r7, #3928]
	add	r3, r3, r3, asl #1
	cmp	r6, r3, asr #2
	bcc	.L941
	cmn	r6, #1
	movne	r6, #256
.L941:
	mov	r0, r6
	add	sp, sp, #12
	@ sp needed
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L950:
	.align	2
.L949:
	.word	.LANCHOR0
	.word	.LANCHOR0+3920
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
	ldr	r3, .L955
	subs	r4, r0, #0
	mov	r5, r1
	mov	r7, r2
	ldrb	r6, [r3, #265]	@ zero_extendqisi2
	bne	.L952
	ldr	r2, .L955+4
	ldrb	r3, [r2, #1]	@ zero_extendqisi2
	ldr	r1, [r2, #4]
	mul	r1, r1, r3
	cmp	r5, r1
	bcs	.L952
	ldrb	r3, [r2]	@ zero_extendqisi2
	cmp	r3, #0
	subeq	r6, r6, #2
	movne	r6, #4
.L952:
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
.L956:
	.align	2
.L955:
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
	ldr	r4, .L967
	ldr	r5, .L967+4
	ldr	r8, .L967+8
	ldr	r3, [r4, #-3408]
	mov	r9, r4
	ldrb	r0, [r4, #-3311]	@ zero_extendqisi2
	mov	r10, r5
	str	r3, [r4, #-3324]
	bl	FlashBchSel
	mov	r1, #0
	mov	r2, #2048
	ldr	r0, [r4, #-3408]
	bl	ftl_memset
	ldr	r3, [r4, #-3324]
	ldr	r1, .L967+12
	mov	r2, #32
	str	r8, [r3]
	ldr	r0, [r4, #-3324]
	ldrb	r3, [r5, #1612]	@ zero_extendqisi2
	add	r0, r0, #16
	strh	r3, [r0, #-4]	@ movhi
	ldrb	r3, [r5, #1]	@ zero_extendqisi2
	strh	r3, [r0, #-2]	@ movhi
	ldrb	r3, [r4, #-3400]	@ zero_extendqisi2
	str	r3, [r0, #1060]
	bl	ftl_memcpy
	ldr	r0, [r4, #-3324]
	add	r1, r5, #1616
	mov	r2, #8
	add	r0, r0, #80
	bl	ftl_memcpy
	ldr	r0, [r4, #-3324]
	ldr	r1, .L967+16
	mov	r2, #32
	add	r0, r0, #96
	bl	ftl_memcpy
	ldr	r0, [r4, #-3324]
	ldr	r1, .L967+20
	mov	r2, #32
	add	r0, r0, #160
	bl	ftl_memcpy
	ldr	r0, [r4, #-3324]
	add	r1, r5, #44
	mov	r2, #32
	add	r0, r0, #192
	bl	ftl_memcpy
	ldr	r0, [r4, #-3324]
	mov	r2, #852
	ldr	r1, .L967+24
	add	r0, r0, #224
	bl	ftl_memcpy
	ldr	r6, [r4, #-3324]
	movw	r1, #2036
	add	r0, r6, #12
	bl	js_hash
	movw	r3, #1592
	str	r3, [r6, #4]
	ldr	r3, [r4, #-3308]
	str	r3, [r4, #-3324]
	str	r0, [r6, #8]
	mov	r0, #0
	bl	flash_enter_slc_mode
	mov	r6, #0
	mov	r7, r6
.L960:
	ldr	r1, [r5, #4]
	mov	r0, #0
	mov	r2, r0
	mul	r1, r1, r7
	bl	FlashEraseBlock
	ldr	r1, [r5, #4]
	mov	r0, #0
	ldr	r2, [r4, #-3408]
	mov	r3, r0
	mul	r1, r1, r7
	bl	FlashProgPage
	ldr	r1, [r5, #4]
	mov	r0, #0
	ldr	r2, [r4, #-3408]
	mov	r3, r0
	mul	r1, r1, r7
	add	r1, r1, #1
	bl	FlashProgPage
	ldr	r1, [r5, #4]
	mov	r0, #0
	ldr	r2, [r4, #-3308]
	mov	r3, r0
	mul	r1, r1, r7
	bl	FlashReadRawPage
	add	r2, r7, #1
	cmn	r0, #1
	beq	.L958
	ldr	fp, [r9, #-3324]
	ldr	r3, [fp]
	cmp	r3, r8
	bne	.L958
	add	r0, fp, #12
	movw	r1, #2036
	str	r2, [sp, #4]
	bl	js_hash
	ldr	r3, [fp, #8]
	cmp	r3, r0
	ldr	r2, [sp, #4]
	bne	.L958
	ldr	r3, [r10, #4]
	cmp	r6, #1
	str	r2, [r9, #-3316]
	mul	r7, r3, r7
	str	r7, [r9, #-3320]
	beq	.L961
	mov	r6, #1
.L958:
	cmp	r2, #4
	mov	r7, r2
	bne	.L960
	b	.L959
.L961:
	mov	r6, #2
.L959:
	mov	r0, #0
	bl	flash_exit_slc_mode
	clz	r0, r6
	mov	r0, r0, lsr #5
	rsb	r0, r0, #0
	add	sp, sp, #12
	@ sp needed
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L968:
	.align	2
.L967:
	.word	.LANCHOR2
	.word	.LANCHOR0
	.word	1312902724
	.word	IDByte
	.word	.LANCHOR0+1624
	.word	.LANCHOR1+256
	.word	.LANCHOR0+3916
	.fnend
	.size	FlashSavePhyInfo, .-FlashSavePhyInfo
	.align	2
	.global	FlashReadIdbDataRaw
	.type	FlashReadIdbDataRaw, %function
FlashReadIdbDataRaw:
	.fnstart
	@ args = 0, pretend = 0, frame = 16
	@ frame_needed = 0, uses_anonymous_args = 0
	ldr	r3, .L990
	stmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.save {r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.pad #20
	sub	sp, sp, #20
	ldr	r2, [r3]
	mov	r9, r0
	ldr	r6, .L990+4
	str	r3, [sp]
	str	r2, [sp, #12]
	ldr	r2, .L990+8
	ldrb	r10, [r6, #3905]	@ zero_extendqisi2
	ldr	r0, [r2]	@ unaligned
	ldr	r2, .L990+12
	str	r0, [sp, #8]	@ unaligned
	ldr	r2, [r2, #-3404]
	cmp	r2, #0
	beq	.L970
	mov	r0, #0
	bl	flash_enter_slc_mode
.L970:
	mov	r0, r9
	mov	r1, #0
	mov	r2, #2048
	mvn	r8, #0
	bl	ftl_memset
	mov	r4, #2
.L971:
	ldrb	r3, [r6, #1]	@ zero_extendqisi2
	cmp	r4, r3
	bcs	.L975
	ldr	fp, .L990+12
	mov	r7, #0
.L973:
	add	r3, sp, #8
	ldr	r5, .L990+12
	ldrb	ip, [r3, r7]	@ zero_extendqisi2
	mov	r0, ip
	str	ip, [sp, #4]
	bl	FlashBchSel
	ldr	r1, [r6, #4]
	mov	r0, #0
	ldr	r2, [fp, #-3408]
	mov	r3, r0
	mul	r1, r1, r4
	bl	FlashReadRawPage
	cmn	r0, #1
	ldr	ip, [sp, #4]
	bne	.L972
	add	r7, r7, #1
	cmp	r7, #4
	bne	.L973
	b	.L974
.L972:
	ldr	r3, [r5, #-3408]
	ldr	r2, .L990+16
	ldr	r3, [r3]
	cmp	r3, r2
	bne	.L974
	mov	r1, ip
	ldr	r0, .L990+20
	bl	printk
	mov	r2, #2048
	mov	r0, r9
	ldr	r1, [r5, #-3408]
	bl	ftl_memcpy
	ldr	r3, [r5, #-3408]
	ldr	r2, .L990+4
	ldr	r3, [r3, #512]
	strb	r3, [r2, #1]
	ldr	r3, [r5, #-3316]
	cmp	r3, r4
	bls	.L979
	str	r4, [r5, #-3316]
	bl	FlashSavePhyInfo
	mov	r8, #0
.L974:
	add	r4, r4, #1
	b	.L971
.L979:
	mov	r8, #0
.L975:
	mov	r0, r10
	bl	FlashBchSel
	ldr	r3, .L990+12
	ldr	r3, [r3, #-3404]
	cmp	r3, #0
	beq	.L976
	mov	r0, #0
	bl	flash_exit_slc_mode
.L976:
	ldr	r3, [sp]
	mov	r0, r8
	ldr	r2, [sp, #12]
	ldr	r3, [r3]
	cmp	r2, r3
	beq	.L978
	bl	__stack_chk_fail
.L978:
	add	sp, sp, #20
	@ sp needed
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L991:
	.align	2
.L990:
	.word	__stack_chk_guard
	.word	.LANCHOR0
	.word	.LANCHOR3
	.word	.LANCHOR2
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
	str	r0, [r4, #-3408]
	mov	r0, #32768
	bl	ftl_malloc
	str	r0, [r4, #-3308]
	mov	r0, #4096
	bl	ftl_malloc
	str	r0, [r4, #-3304]
	mov	r0, #32768
	bl	ftl_malloc
	str	r0, [r4, #-3300]
	mov	r0, #4096
	bl	ftl_malloc
	mov	r3, #50
	str	r7, [r4, #-3316]
	strb	r3, [r5, #1]
	strb	r3, [r4, #-3312]
	mov	r3, #128
	strb	r7, [r4, #-3400]
	str	r3, [r5, #4]
	mov	r3, #60
	str	r7, [r4, #-3332]
	strb	r7, [r5]
	strb	r7, [r4, #-3292]
	strb	r3, [r4, #-3311]
	str	r0, [r4, #-3296]
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
	ldr	r0, [r4, #-3396]
	bl	NandcSetDdrMode
.L999:
	mov	r1, #0
	mov	r2, #852
	ldr	r0, .L1087+20
	bl	ftl_memset
	ldr	r6, .L1087+24
	ldr	r2, [r4, #-3388]
	ldr	r3, .L1087+28
	cmp	r2, r6
	ldrb	r0, [r8, #1]	@ zero_extendqisi2
	str	r3, [r5, #1784]
	mov	r3, #0
	strb	r3, [r5, #8]
	sub	lr, r0, #218
	ldreq	r3, .L1087
	clz	lr, lr
	moveq	r1, #1
	mov	lr, lr, lsr #5
	streq	r1, [r3, #-3404]
	cmp	r0, #161
	cmpne	r0, #241
	and	r1, r0, #253
	moveq	r3, #1
	movne	r3, #0
	orr	r3, lr, r3
	cmp	r1, #209
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
	ldrb	r7, [r8]	@ zero_extendqisi2
	mov	r3, #16
	strb	r3, [r5, #1]
	mov	ip, #1
	strb	r3, [r4, #-3311]
	cmp	r7, #152
	ldr	r3, .L1087+32
	strb	ip, [r5]
	ldr	r1, .L1087
	strb	r7, [r3, #3253]
	strb	r0, [r3, #3254]
	bne	.L1004
	ldr	r7, .L1087+8
	ldrsb	r7, [r7, #4]
	cmp	r7, #0
	strltb	ip, [r1, #-3292]
	movge	ip, #24
	strgeb	ip, [r1, #-3311]
.L1004:
	movw	r1, #2049
	cmp	r2, r6
	cmpne	r2, r1
	moveq	r2, #16
	streqb	r2, [r4, #-3311]
	cmp	lr, #0
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
	strb	r2, [r3, #3254]
	b	.L1009
.L1010:
	cmp	r0, #211
	ldreq	r2, .L1087+36
	moveq	r1, #4096
	streqh	r1, [r2, #14]	@ movhi
	moveq	r2, #2
	streqb	r2, [r3, #3265]
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
	ldr	r7, .L1087
	ldr	r3, [r3, #1784]
	ldrh	r3, [r3, #16]
	mov	r3, r3, lsr #8
	tst	r3, #1
	and	r0, r3, #7
	strb	r0, [r4, #-3412]
	bne	.L1013
	mov	r3, #1
	strb	r3, [r7, #-3400]
	bl	FlashSetInterfaceMode
	ldrb	r0, [r7, #-3412]	@ zero_extendqisi2
	bl	NandcSetMode
.L1013:
	ldr	r3, [r5, #1784]
	ldrb	r3, [r3, #26]	@ zero_extendqisi2
	strb	r3, [r5, #3904]
	bl	FlashLoadPhyInfo
	cmp	r0, #0
	beq	.L1011
	ldr	r3, [r4, #-3396]
	ldr	r7, .L1087
	cmp	r3, #0
	beq	.L1016
	mov	r0, #1
	bl	FlashSetInterfaceMode
	mov	r0, #1
	b	.L1082
.L1016:
	ldrb	r0, [r7, #-3412]	@ zero_extendqisi2
	bl	FlashSetInterfaceMode
	ldrb	r0, [r7, #-3412]	@ zero_extendqisi2
.L1082:
	bl	NandcSetMode
	bl	FlashLoadPhyInfo
	cmp	r0, #0
	beq	.L1011
	mov	r0, #1
	ldr	r7, .L1087+4
	bl	FlashSetInterfaceMode
	mov	r0, #1
	bl	NandcSetMode
	ldr	r3, [r5, #1784]
	ldr	r0, .L1087+48
	ldrh	r1, [r3, #14]
	bl	printk
	bl	FlashLoadPhyInfoInRam
	cmn	r0, #1
	beq	.L1057
	bl	FlashDieInfoInit
	ldr	r3, [r7, #1784]
	ldrb	r0, [r3, #19]	@ zero_extendqisi2
	bl	FlashGetReadRetryDefault
	movw	r3, #1788
	ldr	r2, [r7, #1784]
	ldrh	r3, [r7, r3]
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
	ldrb	r3, [r4, #-3412]	@ zero_extendqisi2
	tst	r3, #6
	beq	.L1020
	bl	FlashSavePhyInfo
	ldr	r3, .L1087
	mov	r0, #0
	ldr	r1, [r3, #-3320]
	bl	FlashDdrParaScan
.L1020:
	bl	FlashSavePhyInfo
.L1011:
	ldr	r2, [r5, #1784]
	ldr	r9, .L1087+4
	ldr	r7, .L1087
	ldrb	r3, [r2, #26]	@ zero_extendqisi2
	ldrh	r0, [r2, #10]
	ldrb	r10, [r2, #18]	@ zero_extendqisi2
	strb	r3, [r5, #3904]
	ldrh	r3, [r2, #16]
	ubfx	r1, r3, #7, #1
	strb	r1, [r5, #8]
	ubfx	r1, r3, #3, #1
	strb	r1, [r4, #-3291]
	ubfx	r1, r3, #4, #1
	ubfx	r3, r3, #8, #3
	strb	r1, [r4, #-3340]
	strb	r3, [r4, #-3412]
	mov	r3, #0
	ldrb	r1, [r2, #12]	@ zero_extendqisi2
	str	r3, [r4, #-3328]
	bl	__aeabi_idiv
	mov	r1, r0
	mov	r0, r10
	bl	BuildFlashLsbPageTable
	bl	FlashDieInfoInit
	ldr	r3, [r5, #1784]
	ldrh	r2, [r3, #16]
	tst	r2, #64
	beq	.L1022
	ldrb	r0, [r3, #19]	@ zero_extendqisi2
	ldr	r3, .L1087+52
	ldrb	r2, [r9, #3917]	@ zero_extendqisi2
	ldr	r1, .L1087+56
	strb	r0, [r3]
	ldr	r3, .L1087+60
	strb	r2, [r3]
	mov	ip, r3
	ldrb	r2, [r9, #3918]	@ zero_extendqisi2
	strb	r2, [r1]
	sub	r2, r0, #1
	cmp	r2, #7
	bhi	.L1023
	ldr	r3, .L1087+64
	sub	r2, r0, #5
	cmp	r0, #8
	cmpne	r2, #1
	sub	r1, r0, #8
	clz	r1, r1
	str	r3, [r7, #-3328]
	movls	r3, #1
	strls	r3, [r7, #-3348]
	cmp	r0, #7
	mov	r1, r1, lsr #5
	ldreq	r3, .L1087+68
	beq	.L1026
	ldr	r2, .L1087+68
	cmp	r1, #0
	sub	r3, r2, #8
	movne	r3, r2
.L1026:
	sub	r1, r3, #1
	add	r3, r3, #31
	mov	r2, #0
.L1027:
	ldrsb	ip, [r1, #1]!
	cmp	ip, #0
	addeq	r2, r2, #1
	cmp	r1, r3
	bne	.L1027
	cmp	r2, #27
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
	str	r3, [r7, #-3328]
	moveq	r3, #15
	bne	.L1086
	b	.L1084
.L1029:
	sub	r2, r0, #65
	cmp	r0, #33
	cmpne	r2, #1
	bhi	.L1031
	ldr	r2, .L1087+76
	str	r2, [r7, #-3328]
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
	str	r3, [r7, #-3328]
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
	streq	r3, [r7, #-3328]
.L1022:
	ldr	r3, [r4, #-3388]
	cmp	r3, r6
	bne	.L1036
	ldrb	r2, [r5, #3904]	@ zero_extendqisi2
	cmp	r2, #0
	ldrne	r2, .L1087+4
	movne	r1, #0
	ldrne	r2, [r2, #1784]
	strneb	r1, [r2, #18]
.L1036:
	ldrb	r2, [r8]	@ zero_extendqisi2
	cmp	r2, #44
	bne	.L1037
	ldrb	r2, [r4, #-3400]	@ zero_extendqisi2
	cmp	r2, #0
	beq	.L1037
	cmp	r3, r6
	bne	.L1038
	ldrb	r3, [r5, #3904]	@ zero_extendqisi2
	cmp	r3, #0
	bne	.L1037
.L1038:
	mov	r0, #1
	mov	r3, #0
	strb	r3, [r4, #-3400]
	bl	FlashSetInterfaceMode
	mov	r0, #1
	bl	NandcSetMode
.L1037:
	ldrb	r3, [r4, #-3412]	@ zero_extendqisi2
	tst	r3, #6
	beq	.L1039
	ldr	r2, .L1087
	ldrb	r2, [r2, #-3400]	@ zero_extendqisi2
	cmp	r2, #0
	bne	.L1040
	tst	r3, #1
	bne	.L1039
.L1040:
	mov	r0, #0
	bl	flash_enter_slc_mode
	mov	r0, #0
	ldr	r1, [r4, #-3320]
	bl	FlashDdrParaScan
	mov	r0, #0
	bl	flash_exit_slc_mode
.L1039:
	ldr	r3, [r5, #1784]
	mov	r9, #16
	ldr	r7, .L1087+4
	ldr	r4, .L1087+84
	ldrb	r0, [r3, #20]	@ zero_extendqisi2
	bl	FlashBchSel
	add	r0, r7, #1792
	bl	FlashReadIdbDataRaw
	ldr	r0, .L1087+88
	strb	r9, [r5, #1]
	bl	FlashTimingCfg
	ldr	r6, [r5, #1784]
	ldrb	r3, [r6, #12]	@ zero_extendqisi2
	strh	r3, [r4, #8]	@ movhi
	ldrb	r3, [r6, #7]	@ zero_extendqisi2
	str	r3, [r5, #3880]
	ldr	r3, [r8]	@ unaligned
	ldrh	r8, [r6, #14]
	str	r3, [r5, #3876]
	ldrb	r3, [r5, #1612]	@ zero_extendqisi2
	strh	r3, [r4, #10]	@ movhi
	ldrb	r3, [r6, #13]	@ zero_extendqisi2
	strh	r8, [r4, #14]	@ movhi
	strh	r3, [r4, #12]	@ movhi
	ldrh	r3, [r6, #10]
	strh	r3, [r4, #16]	@ movhi
	ldrb	r1, [r6, #12]	@ zero_extendqisi2
	ldrh	r0, [r6, #10]
	bl	__aeabi_idiv
	strh	r0, [r4, #18]	@ movhi
	ldrb	r2, [r6, #9]	@ zero_extendqisi2
	strh	r2, [r4, #20]	@ movhi
	ldrb	r1, [r6, #9]	@ zero_extendqisi2
	ldrh	r3, [r6, #10]
	smulbb	r3, r1, r3
	mov	r1, #512
	strh	r1, [r4, #24]	@ movhi
	ldrb	r1, [r5, #1]	@ zero_extendqisi2
	strh	r1, [r4, #26]	@ movhi
	uxth	r3, r3
	ldrb	r1, [r5]	@ zero_extendqisi2
	strh	r3, [r4, #22]	@ movhi
	cmp	r1, #1
	bne	.L1042
	mov	r3, r3, asl #1
	mov	r1, r8, lsr #1
	mov	r2, r2, asl #1
	strh	r3, [r4, #22]	@ movhi
	strb	r9, [r7, #1]
	mov	r3, #8
	strh	r1, [r4, #14]	@ movhi
	strh	r2, [r4, #20]	@ movhi
	strh	r3, [r4, #26]	@ movhi
.L1042:
	ldrb	r0, [r6, #20]	@ zero_extendqisi2
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
	.word	.LANCHOR0+3916
	.word	1446522928
	.word	.LANCHOR1+256
	.word	.LANCHOR1
	.word	.LANCHOR1+3252
	.word	.LANCHOR1+2912
	.word	.LANCHOR0+44
	.word	.LC14
	.word	g_retryMode
	.word	g_maxRetryCount
	.word	g_maxRegNum
	.word	HynixReadRetrial
	.word	.LANCHOR0+3944
	.word	MicronReadRetrial
	.word	ToshibaReadRetrial
	.word	SamsungReadRetrial
	.word	.LANCHOR0+3876
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
	stmfd	sp!, {r3, r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.save {r3, r4, r5, r6, r7, r8, r9, r10, fp, lr}
	mov	r4, r2
	ldr	r3, .L1106
	mov	r8, r0
	mov	r9, r1
	ldr	r2, [r3, #1784]
	mov	r6, r3
	ldrb	r7, [r2, #19]	@ zero_extendqisi2
	ldrb	r2, [r3, #3904]	@ zero_extendqisi2
	cmp	r2, #0
	beq	.L1090
	ldr	r3, .L1106+4
	ldr	r3, [r3, #-3404]
	cmp	r3, #0
	ldmnefd	sp!, {r3, r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L1090:
	sub	r3, r7, #5
	cmp	r7, #68
	cmpne	r3, #3
	bls	.L1091
	sub	r3, r7, #19
	tst	r3, #239
	ldmnefd	sp!, {r3, r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L1091:
	ldr	r10, .L1106+8
	movw	fp, #65535
	ldr	r5, .L1106+4
.L1093:
	ldr	r3, [r6, #1784]
	ldrh	r3, [r3, #10]
	cmp	r3, r4
	bls	.L1105
	mov	r3, r4, asl #1
	ldrh	r3, [r10, r3]
	cmp	r3, fp
	ldmnefd	sp!, {r3, r4, r5, r6, r7, r8, r9, r10, fp, pc}
	cmp	r7, #8
	mov	r2, #32768
	ldr	r0, [r5, #-3308]
	moveq	r1, #0
	movne	r1, #255
	bl	ftl_memset
	ldr	r2, [r5, #-3308]
	add	r1, r4, r9
	add	r4, r4, #1
	mov	r0, r8
	mov	r3, r2
	uxth	r4, r4
	bl	FlashProgPage
	b	.L1093
.L1105:
	ldmfd	sp!, {r3, r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L1107:
	.align	2
.L1106:
	.word	.LANCHOR0
	.word	.LANCHOR2
	.word	.LANCHOR0+588
	.fnend
	.size	FlashPageProgMsbFFData, .-FlashPageProgMsbFFData
	.align	2
	.global	FlashReadSlc2KPages
	.type	FlashReadSlc2KPages, %function
FlashReadSlc2KPages:
	.fnstart
	@ args = 0, pretend = 0, frame = 32
	@ frame_needed = 0, uses_anonymous_args = 0
	ldr	r3, .L1160
	stmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.save {r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.pad #44
	sub	sp, sp, #44
	ldr	r9, .L1160+4
	mov	r4, r0
	str	r2, [sp, #16]
	mov	r8, #0
	ldr	r2, [r3]
	mov	r10, r9
	str	r1, [sp, #12]
	str	r3, [sp, #20]
	str	r2, [sp, #36]
	ldr	r2, .L1160+8
	ldrb	r2, [r2, #265]	@ zero_extendqisi2
	str	r2, [sp, #8]
.L1109:
	ldr	r3, [sp, #12]
	cmp	r8, r3
	beq	.L1159
	ldr	r3, [sp, #12]
	add	r2, sp, #32
	mov	r0, r4
	ldr	r1, [sp, #16]
	rsb	r3, r8, r3
	uxtb	r3, r3
	str	r3, [sp]
	add	r3, sp, #28
	bl	LogAddr2PhyAddr
	ldrb	r2, [r9, #1612]	@ zero_extendqisi2
	ldr	r3, [sp, #28]
	cmp	r3, r2
	mvncs	r3, #0
	strcs	r3, [r4]
	bcs	.L1111
	add	r3, r9, r3
	mov	r7, #0
	ldrb	r5, [r3, #1616]	@ zero_extendqisi2
	mov	r0, r5
	bl	NandcWaitFlashReady
	mov	r0, r5
	bl	NandcFlashCs
.L1112:
	ldr	r1, [sp, #32]
	mov	r0, r5
	bl	FlashReadCmd
	mov	r0, r5
	bl	NandcWaitFlashReady
	ldr	r3, [r4, #12]
	mov	r0, r5
	mov	r1, #0
	str	r3, [sp]
	ldr	r2, [sp, #8]
	ldr	r3, [r4, #8]
	bl	NandcXferData
	ldr	r3, .L1160+12
	ldrb	r3, [r3, #-3292]	@ zero_extendqisi2
	cmp	r3, #0
	mov	r6, r0
	beq	.L1113
	mov	r0, r5
	bl	flash_read_ecc
	cmp	r0, #5
	movhi	r6, #256
.L1113:
	cmp	r7, #9
	cmnls	r6, #1
	moveq	r3, #1
	movne	r3, #0
	addeq	r7, r7, #1
	beq	.L1112
.L1114:
	cmp	r7, #0
	mov	r7, r3
	movne	r6, #256
.L1116:
	ldr	r3, [sp, #32]
	mov	r0, r5
	ldr	r1, [r10, #4]
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
	ldr	r2, [sp, #8]
	bl	NandcXferData
	ldr	r3, .L1160+12
	ldrb	r2, [r3, #-3292]	@ zero_extendqisi2
	cmp	r2, #0
	mov	fp, r0
	beq	.L1119
	mov	r0, r5
	bl	flash_read_ecc
	cmp	r0, #5
	movhi	fp, #256
.L1119:
	cmp	r7, #9
	cmnls	fp, #1
	addeq	r7, r7, #1
	beq	.L1116
.L1120:
	cmp	r7, #0
	mov	r0, r5
	movne	fp, #256
	bl	NandcFlashDeCs
	ldrb	r2, [r10, #3905]	@ zero_extendqisi2
	cmp	fp, r6
	movcs	r3, fp
	movcc	r3, r6
	add	r2, r2, r2, asl #1
	cmp	r3, r2, asr #2
	bls	.L1122
	cmn	r3, #1
	movne	r3, #256
.L1122:
	cmp	r3, #256
	cmnne	r3, #1
	movne	r3, #0
	str	r3, [r4]
	ldr	r3, [r4, #12]
	cmp	r3, #0
	beq	.L1125
	ldr	r2, [r3, #12]
	cmn	r2, #1
	bne	.L1125
	ldr	r2, [r3, #8]
	cmn	r2, #1
	bne	.L1125
	ldr	r3, [r3]
	cmn	r3, #1
	strne	r2, [r4]
.L1125:
	ldr	r3, [r4]
	cmn	r3, #1
	bne	.L1111
	ldr	r1, [r4, #4]
	ldr	r0, .L1160+16
	ldrb	r2, [r10, #3905]	@ zero_extendqisi2
	bl	printk
	ldr	r1, [r4, #8]
	cmp	r1, #0
	beq	.L1127
	ldr	r0, .L1160+20
	mov	r2, #4
	mov	r3, #8
	bl	rknand_print_hex
.L1127:
	ldr	r1, [r4, #12]
	cmp	r1, #0
	beq	.L1111
	mov	r2, #4
	ldr	r0, .L1160+24
	mov	r3, r2
	bl	rknand_print_hex
.L1111:
	add	r8, r8, #1
	add	r4, r4, #36
	b	.L1109
.L1159:
	ldr	r3, [sp, #20]
	mov	r0, #0
	ldr	r2, [sp, #36]
	ldr	r3, [r3]
	cmp	r2, r3
	beq	.L1130
	bl	__stack_chk_fail
.L1130:
	add	sp, sp, #44
	@ sp needed
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L1161:
	.align	2
.L1160:
	.word	__stack_chk_guard
	.word	.LANCHOR0
	.word	.LANCHOR1
	.word	.LANCHOR2
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
	@ args = 0, pretend = 0, frame = 48
	@ frame_needed = 0, uses_anonymous_args = 0
	ldr	r3, .L1238
	stmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.save {r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.pad #60
	sub	sp, sp, #60
	ldr	r3, [r3]
	ldr	r6, .L1238+4
	str	r1, [sp, #20]
	str	r3, [sp, #52]
	ldr	r3, .L1238+8
	ldrb	r8, [r6]	@ zero_extendqisi2
	str	r2, [sp, #24]
	ldrb	r3, [r3, #265]	@ zero_extendqisi2
	cmp	r8, #0
	str	r3, [sp, #16]
	ldrb	r3, [r6, #8]	@ zero_extendqisi2
	str	r3, [sp, #28]
	beq	.L1200
	bl	FlashReadSlc2KPages
	b	.L1164
.L1200:
	ldr	r10, .L1238+12
	mov	r9, r0
	mov	fp, r8
.L1163:
	ldr	r3, [sp, #20]
	cmp	fp, r3
	bcs	.L1236
	mov	r3, #36
	add	r2, sp, #48
	mul	r3, r3, fp
	ldr	r1, [sp, #24]
	add	r7, r9, r3
	str	r3, [sp, #8]
	mov	r0, r7
	ldr	r3, [r7, #4]
	str	r3, [sp, #12]
	ldr	r3, [sp, #20]
	rsb	r3, fp, r3
	uxtb	r3, r3
	str	r3, [sp]
	add	r3, sp, #44
	bl	LogAddr2PhyAddr
	ldrb	r2, [r6, #1612]	@ zero_extendqisi2
	ldr	r3, [sp, #44]
	mov	r5, r0
	cmp	r3, r2
	ldrcs	r2, [sp, #8]
	mvncs	r3, #0
	strcs	r3, [r9, r2]
	bcs	.L1166
	add	r3, r6, r3
	ldrb	r4, [r3, #1616]	@ zero_extendqisi2
	ldrb	r3, [r10, #-3291]	@ zero_extendqisi2
	cmp	r3, #0
	mov	r0, r4
	moveq	r5, #0
	bl	NandcWaitFlashReady
	ldr	r3, .L1238+4
	ldr	r3, [r3, #1784]
	ldrb	r2, [r3, #19]	@ zero_extendqisi2
	sub	r3, r2, #1
	cmp	r3, #7
	bhi	.L1168
	sub	r2, r2, #7
	add	r1, r6, r4
	cmp	r2, #1
	add	r2, r10, r4
	ldrb	r3, [r1, #3928]	@ zero_extendqisi2
	ldrb	r2, [r2, #-3416]	@ zero_extendqisi2
	ldrlsb	r3, [r1, #3936]	@ zero_extendqisi2
	cmp	r2, r3
	beq	.L1168
	mov	r0, r4
	ldrb	r1, [r6, #3917]	@ zero_extendqisi2
	ldr	r2, .L1238+16
	bl	HynixSetRRPara
.L1168:
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
	beq	.L1170
	ldrb	r3, [r6, #3904]	@ zero_extendqisi2
	cmp	r3, #0
	beq	.L1170
	bl	flash_enter_slc_mode
	b	.L1171
.L1170:
	bl	flash_exit_slc_mode
.L1171:
	ldr	r1, [sp, #48]
	cmn	r1, #1
	cmpeq	r4, #255
	moveq	r3, #0
	movne	r3, #1
	moveq	r5, r3
	beq	.L1173
	cmp	r5, #0
	beq	.L1174
	ldr	r2, [r6, #4]
	mov	r0, r4
	add	r2, r1, r2
	bl	FlashReadDpCmd
	b	.L1175
.L1174:
	mov	r0, r4
	bl	FlashReadCmd
.L1175:
	mov	r0, r4
	bl	NandcWaitFlashReady
	cmp	r5, #0
	beq	.L1173
	mov	r0, r4
	ldr	r1, [sp, #48]
	bl	FlashReadDpDataOutCmd
.L1173:
	ldr	r3, [r7, #12]
	mov	r0, r4
	ldr	r2, [sp, #16]
	mov	r1, #0
	str	r3, [sp]
	ldr	r3, [r7, #8]
	bl	NandcXferData
	ldrb	r2, [r6, #8]	@ zero_extendqisi2
	adds	r2, r2, #0
	movne	r2, #1
	cmn	r0, #1
	mov	ip, r0
	movne	r2, #0
	cmp	r2, #0
	movne	r3, #0
	strneb	r3, [r6, #8]
	movne	r5, r3
	bne	.L1171
.L1176:
	cmp	r5, #0
	beq	.L1177
	ldr	r3, .L1238+4
	str	r0, [sp, #32]
	mov	r0, r4
	str	r2, [sp, #36]
	ldr	r1, [r3, #4]
	ldr	r3, [sp, #48]
	add	r1, r1, r3
	bl	FlashReadDpDataOutCmd
	mov	r0, r4
	ldr	r3, [sp, #8]
	ldr	r2, [sp, #36]
	add	r3, r3, #36
	add	r3, r9, r3
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
.L1177:
	mov	r0, r4
	str	ip, [sp, #32]
	bl	NandcFlashDeCs
	ldr	ip, [sp, #32]
	ldrb	r3, [sp, #28]	@ zero_extendqisi2
	cmn	ip, #1
	strb	r3, [r6, #8]
	bne	.L1184
	ldrb	r3, [r10, #-3400]	@ zero_extendqisi2
	cmp	r3, #0
	bne	.L1179
.L1183:
	ldr	r5, [r10, #-3328]
	cmp	r5, #0
	bne	.L1180
	b	.L1237
.L1179:
	ldr	r3, [r6, #3840]
	mov	r0, r4
	ldr	r1, [sp, #48]
	ldr	r5, [r3, #304]
	mov	r3, #1
	str	r3, [sp]
	ldr	r2, [r7, #8]
	ldr	r3, [r7, #12]
	bl	FlashDdrTunningRead
	cmn	r0, #1
	mov	ip, r0
	beq	.L1182
	ldrb	r3, [r6, #3905]	@ zero_extendqisi2
	cmp	r0, r3, lsr #1
	bls	.L1203
.L1182:
	ubfx	r0, r5, #8, #8
	str	ip, [sp, #32]
	bl	NandcSetDdrPara
	ldr	ip, [sp, #32]
	cmn	ip, #1
	beq	.L1183
	b	.L1203
.L1180:
	mov	r0, r4
	ldr	r1, [sp, #48]
	ldr	r2, [r7, #8]
	ldr	r3, [r7, #12]
	blx	r5
	cmn	r0, #1
	mov	ip, r0
	bne	.L1205
	ldr	r3, [r6, #1784]
	ldrb	r3, [r3, #19]	@ zero_extendqisi2
	sub	r3, r3, #1
	cmp	r3, #7
	bhi	.L1186
	mov	r0, r4
	ldrb	r1, [r6, #3917]	@ zero_extendqisi2
	ldr	r2, .L1238+16
	mov	r3, #0
	bl	HynixSetRRPara
.L1186:
	ldr	r1, [sp, #48]
	mov	r0, r4
	ldr	r2, [r7, #8]
	ldr	r3, [r7, #12]
	bl	FlashReadRawPage
	ldr	r1, [r7, #4]
	ldrb	r2, [r6, #3905]	@ zero_extendqisi2
	mov	ip, r0
	ldr	r0, .L1238+20
	mov	r3, ip
	str	ip, [sp, #32]
	bl	printk
	ldr	ip, [sp, #32]
	cmn	ip, #1
	bne	.L1205
	ldrb	r5, [r6, #3904]	@ zero_extendqisi2
	cmp	r5, #0
	beq	.L1185
	ldr	r3, [sp, #12]
	mov	r0, r4
	cmp	r3, #0
	beq	.L1187
	bl	flash_enter_slc_mode
	b	.L1188
.L1187:
	bl	flash_exit_slc_mode
.L1188:
	ldr	ip, [r10, #-3328]
	mov	r0, r4
	ldr	r1, [sp, #48]
	ldr	r2, [r7, #8]
	ldr	r3, [r7, #12]
	blx	ip
	mov	ip, r0
	b	.L1205
.L1237:
	mov	r0, r4
	ldr	r1, [sp, #48]
	ldr	r2, [r7, #8]
	ldr	r3, [r7, #12]
	bl	FlashReadRawPage
	mov	ip, r0
	b	.L1185
.L1203:
	mov	r5, #0
.L1184:
	ldrb	r3, [r6, #3905]	@ zero_extendqisi2
	add	r3, r3, r3, asl #1
	cmp	ip, r3, asr #2
	bls	.L1185
	ldr	r3, [r10, #-3328]
	cmp	r3, #0
	moveq	ip, #256
	b	.L1185
.L1205:
	mov	r5, #0
.L1185:
	cmp	ip, #256
	cmnne	ip, #1
	ldreq	r3, [sp, #8]
	movne	r3, #0
	ldrne	r2, [sp, #8]
	streq	ip, [r9, r3]
	strne	r3, [r9, r2]
	ldr	r3, [sp, #8]
	ldr	r3, [r9, r3]
	cmn	r3, #1
	bne	.L1192
	ldr	r1, [r7, #4]
	ldr	r0, .L1238+24
	ldrb	r2, [r6, #3905]	@ zero_extendqisi2
	bl	printk
	ldr	r1, [r7, #12]
	cmp	r1, #0
	beq	.L1192
	mov	r2, #4
	ldr	r0, .L1238+28
	mov	r3, r2
	bl	rknand_print_hex
.L1192:
	cmp	r5, #0
	beq	.L1194
	ldrb	r3, [r6, #3905]	@ zero_extendqisi2
	add	r3, r3, r3, asl #1
	cmp	r8, r3, asr #2
	bls	.L1195
	ldr	r3, [r10, #-3328]
	cmp	r3, #0
	moveq	r8, #256
.L1195:
	ldr	r3, [sp, #8]
	cmp	r8, #256
	cmnne	r8, #1
	add	r3, r3, #36
	movne	r2, #0
	streq	r8, [r9, r3]
	strne	r2, [r9, r3]
.L1194:
	ldr	r3, [sp, #12]
	add	fp, fp, r5
	cmp	r3, #0
	beq	.L1166
	ldrb	r3, [r6, #3904]	@ zero_extendqisi2
	cmp	r3, #0
	beq	.L1166
	mov	r0, r4
	bl	flash_exit_slc_mode
.L1166:
	add	fp, fp, #1
	b	.L1163
.L1236:
	mov	r0, #0
.L1164:
	ldr	r3, .L1238
	ldr	r2, [sp, #52]
	ldr	r3, [r3]
	cmp	r2, r3
	beq	.L1199
	bl	__stack_chk_fail
.L1199:
	add	sp, sp, #60
	@ sp needed
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L1239:
	.align	2
.L1238:
	.word	__stack_chk_guard
	.word	.LANCHOR0
	.word	.LANCHOR1
	.word	.LANCHOR2
	.word	.LANCHOR0+3920
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
	@ args = 0, pretend = 0, frame = 56
	@ frame_needed = 0, uses_anonymous_args = 0
	stmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.save {r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.pad #60
	sub	sp, sp, #60
	ldr	r4, .L1254
	mov	r1, #0
	ldr	r5, .L1254+4
	mov	r2, #16
	ldr	r0, .L1254+8
	mvn	r10, #0
	ldr	r3, [r4]
	ldrh	ip, [r5, #14]
	ldr	r9, .L1254+12
	str	r3, [sp, #52]
	ldrh	r3, [r5, #12]
	mov	r5, #0
	ldr	fp, .L1254+16
	mov	r8, r5
	smulbb	r3, r3, ip
	uxth	r6, r3
	bl	ftl_memset
	ldr	r3, [r9, #-3304]
	str	r5, [sp, #24]
	str	r3, [sp, #28]
	str	r4, [sp, #4]
.L1241:
	ldrb	r3, [fp, #1612]	@ zero_extendqisi2
	uxtb	r7, r5
	cmp	r3, r7
	bls	.L1253
	mul	ip, r6, r7
	sub	r4, r6, #1
	sub	r3, r6, #12
	uxth	r4, r4
.L1242:
	cmp	r4, r3
	ble	.L1244
	add	r2, ip, r4
	mov	r1, #1
	add	r0, sp, #16
	str	r3, [sp, #12]
	mov	r2, r2, asl #10
	str	r2, [sp, #20]
	mov	r2, r1
	str	ip, [sp, #8]
	bl	FlashReadPages
	ldr	r2, [sp, #16]
	ldr	ip, [sp, #8]
	cmn	r2, #1
	ldr	r3, [sp, #12]
	beq	.L1243
	ldr	r2, [r9, #-3304]
	ldrh	r1, [r2]
	movw	r2, #61664
	cmp	r1, r2
	bne	.L1243
	mov	r1, r7
	ldr	r0, .L1254+20
	mov	r2, r4
	mov	r7, r7, asl #1
	bl	printk
	ldr	r3, .L1254+8
	add	r8, r8, #1
	strh	r4, [r3, r7]	@ movhi
	uxth	r8, r8
	b	.L1244
.L1243:
	sub	r4, r4, #1
	uxth	r4, r4
	b	.L1242
.L1244:
	ldr	r3, .L1254+16
	add	r5, r5, #1
	ldrb	r3, [r3, #1612]	@ zero_extendqisi2
	cmp	r3, r8
	moveq	r10, #0
	b	.L1241
.L1253:
	ldr	r3, [sp, #4]
	mov	r0, r10
	ldr	r2, [sp, #52]
	ldr	r3, [r3]
	cmp	r2, r3
	beq	.L1248
	bl	__stack_chk_fail
.L1248:
	add	sp, sp, #60
	@ sp needed
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L1255:
	.align	2
.L1254:
	.word	__stack_chk_guard
	.word	.LANCHOR0+3876
	.word	.LANCHOR2-3288
	.word	.LANCHOR2
	.word	.LANCHOR0
	.word	.LC19
	.fnend
	.size	FlashLoadFactorBbt, .-FlashLoadFactorBbt
	.align	2
	.global	FlashReadFacBbtData
	.type	FlashReadFacBbtData, %function
FlashReadFacBbtData:
	.fnstart
	@ args = 0, pretend = 0, frame = 40
	@ frame_needed = 0, uses_anonymous_args = 0
	stmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.save {r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.pad #44
	sub	sp, sp, #44
	ldr	r9, .L1272
	mov	r10, r2
	ldr	r8, .L1272+4
	mov	r5, r0
	mov	r7, r1
	ldr	r3, [r9]
	str	r3, [sp, #36]
	ldr	r3, .L1272+8
	ldrh	r2, [r3, #12]
	ldrh	r4, [r3, #14]
	ldr	r3, [r8, #-3408]
	smulbb	r4, r2, r4
	str	r3, [sp, #8]
	ldr	r3, [r8, #-3304]
	str	r3, [sp, #12]
	uxth	r0, r4
	sub	r6, r0, #1
	sub	r4, r0, #16
	mul	fp, r0, r1
	uxth	r6, r6
.L1257:
	cmp	r6, r4
	ble	.L1271
	mov	r1, #1
	add	r3, r6, fp
	mov	r0, sp
	mov	r2, r1
	mov	r3, r3, asl #10
	str	r3, [sp, #4]
	bl	FlashReadPages
	ldr	r3, [sp]
	cmn	r3, #1
	beq	.L1258
	ldr	r3, [r8, #-3304]
	ldrh	r2, [r3]
	movw	r3, #61664
	cmp	r2, r3
	bne	.L1258
	cmp	r5, #0
	moveq	r0, r5
	beq	.L1259
	cmp	r7, #0
	ldreq	ip, .L1272+4
	moveq	lr, #1
	beq	.L1260
.L1262:
	ldr	r1, [r8, #-3408]
	mov	r2, r10
	mov	r0, r5
	bl	ftl_memcpy
	mov	r2, #4
	ldr	r0, .L1272+12
	mov	r1, r5
	mov	r3, r2
	bl	rknand_print_hex
	mov	r0, #0
	b	.L1259
.L1260:
	ldr	r3, [r8, #-3316]
	uxth	r4, r7
	add	r7, r7, #1
	cmp	r4, r3
	bcs	.L1262
	ldr	r2, [ip, #-3408]
	mov	r1, r4, lsr #5
	and	r0, r4, #31
	ldr	r3, [r2, r1, asl #2]
	orr	r0, r3, lr, asl r0
	str	r0, [r2, r1, asl #2]
	b	.L1260
.L1258:
	sub	r6, r6, #1
	uxth	r6, r6
	b	.L1257
.L1271:
	mvn	r0, #0
.L1259:
	ldr	r2, [sp, #36]
	ldr	r3, [r9]
	cmp	r2, r3
	beq	.L1264
	bl	__stack_chk_fail
.L1264:
	add	sp, sp, #44
	@ sp needed
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L1273:
	.align	2
.L1272:
	.word	__stack_chk_guard
	.word	.LANCHOR2
	.word	.LANCHOR0+3876
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
	ldr	r3, .L1285
	stmfd	sp!, {r4, r5, r6, r7, r8, lr}
	.save {r4, r5, r6, r7, r8, lr}
	mov	r5, r0
	ldr	r3, [r3, #1784]
	ldr	r6, .L1285+4
	ldrb	r4, [r3, #13]	@ zero_extendqisi2
	ldrh	r3, [r3, #14]
	ldr	r0, [r6, #-3308]
	smulbb	r4, r4, r3
	uxth	r4, r4
	add	r2, r4, #7
	mov	r2, r2, asr #3
	bl	FlashReadFacBbtData
	cmn	r0, #1
	bne	.L1275
.L1279:
	mov	r3, #0
	b	.L1276
.L1275:
	mov	lr, r4, lsr #4
	mov	ip, #0
	sub	r4, r4, #1
	mov	r3, ip
	mov	r7, #1
.L1277:
	uxth	r0, ip
	cmp	r0, r4
	bge	.L1276
	ldr	r8, [r6, #-3308]
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
	bcc	.L1277
	b	.L1279
.L1276:
	mov	r3, r3, asl #1
	mvn	r2, #0
	mov	r0, #0
	strh	r2, [r5, r3]	@ movhi
	ldmfd	sp!, {r4, r5, r6, r7, r8, pc}
.L1286:
	.align	2
.L1285:
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
	mov	r9, r2
	ldr	r8, .L1319
	str	r3, [sp, #8]
	mov	r6, r1
	ldr	r3, .L1319+4
	mov	r4, r0
	mov	fp, r0
	mov	r7, #0
	ldr	r2, [r3]
	str	r3, [sp, #12]
	str	r2, [sp, #60]
	ldr	r2, .L1319+8
	ldrb	r10, [r2, #265]	@ zero_extendqisi2
.L1288:
	cmp	r7, r6
	beq	.L1318
	rsb	r3, r7, r6
	add	r2, sp, #16
	mov	r0, fp
	mov	r1, r9
	uxtb	r3, r3
	str	r3, [sp]
	add	r3, sp, #20
	bl	LogAddr2PhyAddr
	ldrb	r2, [r8, #1612]	@ zero_extendqisi2
	ldr	r3, [sp, #20]
	cmp	r3, r2
	mvncs	r3, #0
	strcs	r3, [fp]
	bcs	.L1290
	add	r3, r8, r3
	ldrb	r5, [r3, #1616]	@ zero_extendqisi2
	mov	r0, r5
	bl	NandcWaitFlashReady
	mov	r0, r5
	bl	NandcFlashCs
	mov	r0, r5
	ldr	r1, [sp, #16]
	bl	FlashProgFirstCmd
	ldr	r3, [fp, #12]
	mov	r2, r10
	mov	r1, #1
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
	ldr	r2, [fp, #12]
	mov	r1, #1
	cmp	r3, #0
	mov	r0, r5
	addne	r3, r3, #2048
	cmp	r2, #0
	addne	r2, r2, #8
	str	r2, [sp]
	mov	r2, r10
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
	tst	r0, #1
	mov	r0, r5
	mvnne	r3, #0
	strne	r3, [fp]
	bl	NandcFlashDeCs
.L1290:
	add	r7, r7, #1
	add	fp, fp, #36
	b	.L1288
.L1318:
	ldr	r3, [sp, #8]
	cmp	r3, #0
	bne	.L1306
.L1302:
	ldr	r3, [sp, #12]
	mov	r0, #0
	ldr	r2, [sp, #60]
	ldr	r3, [r3]
	cmp	r2, r3
	beq	.L1303
	bl	__stack_chk_fail
.L1306:
	ldr	r5, .L1319+12
	mov	r7, #0
	mov	r8, r5
.L1295:
	cmp	r7, r6
	beq	.L1302
	ldr	r3, [r4]
	cmn	r3, #1
	bne	.L1296
	ldr	r1, [r4, #4]
	ldr	r0, .L1319+16
	bl	printk
	b	.L1297
.L1296:
	rsb	r3, r7, r6
	mov	r1, r9
	add	r2, sp, #16
	mov	r0, r4
	uxtb	r3, r3
	str	r3, [sp]
	add	r3, sp, #20
	bl	LogAddr2PhyAddr
	ldr	r2, [r5, #-3300]
	mov	r3, #0
	mov	lr, r4
	str	r3, [r2]
	ldr	r2, [r5, #-3296]
	str	r3, [r2]
	ldmia	lr!, {r0, r1, r2, r3}
	add	ip, sp, #24
	stmia	ip!, {r0, r1, r2, r3}
	ldmia	lr!, {r0, r1, r2, r3}
	stmia	ip!, {r0, r1, r2, r3}
	add	r0, sp, #24
	ldr	r3, [lr]
	mov	r1, #1
	mov	r2, r9
	str	r3, [ip]
	ldr	r3, [r5, #-3300]
	str	r3, [sp, #32]
	ldr	r3, [r5, #-3296]
	str	r3, [sp, #36]
	bl	FlashReadPages
	ldr	r10, [sp, #24]
	cmn	r10, #1
	bne	.L1298
	ldr	r0, .L1319+20
	ldr	r1, [r4, #4]
	bl	printk
	str	r10, [r4]
.L1298:
	ldr	r3, [r4, #12]
	cmp	r3, #0
	beq	.L1299
	ldr	r2, [r3]
	ldr	r3, [r8, #-3296]
	ldr	r3, [r3]
	cmp	r2, r3
	beq	.L1299
	ldr	r0, .L1319+24
	ldr	r1, [r4, #4]
	bl	printk
	mvn	r3, #0
	str	r3, [r4]
.L1299:
	ldr	r3, [r4, #8]
	cmp	r3, #0
	beq	.L1297
	ldr	r2, [r3]
	ldr	r3, [r8, #-3300]
	ldr	r3, [r3]
	cmp	r2, r3
	beq	.L1297
	ldr	r0, .L1319+28
	ldr	r1, [r4, #4]
	bl	printk
	mvn	r3, #0
	str	r3, [r4]
.L1297:
	add	r7, r7, #1
	add	r4, r4, #36
	b	.L1295
.L1303:
	add	sp, sp, #68
	@ sp needed
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L1320:
	.align	2
.L1319:
	.word	.LANCHOR0
	.word	__stack_chk_guard
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
	@ args = 0, pretend = 0, frame = 64
	@ frame_needed = 0, uses_anonymous_args = 0
	ldr	ip, .L1379
	stmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.save {r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.pad #76
	sub	sp, sp, #76
	ldr	ip, [ip]
	mov	r4, r0
	ldr	r6, .L1379+4
	mov	r9, r2
	str	r1, [sp, #8]
	str	ip, [sp, #68]
	ldr	ip, [r6, #1784]
	ldrb	r8, [r6]	@ zero_extendqisi2
	str	r3, [sp, #20]
	ldrb	ip, [ip, #19]	@ zero_extendqisi2
	cmp	r8, #0
	str	ip, [sp, #16]
	ldr	ip, .L1379+8
	ldrb	ip, [ip, #265]	@ zero_extendqisi2
	str	ip, [sp, #12]
	beq	.L1322
	bl	FlashProgSlc2KPages
	b	.L1323
.L1336:
	mov	r7, #36
	ldr	r3, [sp, #8]
	mul	r7, r7, r8
	rsb	r3, r8, r3
	add	r2, sp, #24
	mov	r1, r9
	uxtb	r3, r3
	str	r3, [sp]
	add	fp, r4, r7
	add	r3, sp, #28
	mov	r0, fp
	bl	LogAddr2PhyAddr
	ldrb	r2, [r6, #1612]	@ zero_extendqisi2
	ldr	r3, [sp, #28]
	mov	r10, r0
	cmp	r3, r2
	mvncs	r3, #0
	strcs	r3, [r4, r7]
	bcc	.L1376
.L1325:
	add	r8, r8, #1
.L1322:
	ldr	r3, [sp, #8]
	ldr	r5, .L1379+4
	cmp	r8, r3
	bcc	.L1336
	b	.L1377
.L1376:
	ldr	r1, .L1379+12
	add	r3, r6, r3, asl #4
	ldr	r3, [r3, #1664]
	ldrb	r1, [r1, #-3340]	@ zero_extendqisi2
	cmp	r1, #0
	moveq	r10, #0
	cmp	r3, #0
	beq	.L1327
	cmp	r2, #1
	bne	.L1328
	ldr	r0, [r6, #3840]
	bl	NandcIqrWaitFlashReady
.L1328:
	ldrb	r0, [sp, #28]	@ zero_extendqisi2
	bl	FlashWaitCmdDone
.L1327:
	ldr	r3, [sp, #28]
	mov	r1, #0
	cmp	r10, #0
	add	r2, r6, r3, asl #4
	str	r1, [r2, #1668]
	ldr	r1, [sp, #24]
	str	fp, [r2, #1664]
	str	r1, [r2, #1660]
	addne	r1, r7, #36
	addne	r1, r4, r1
	strne	r1, [r2, #1668]
	add	r2, r6, r3
	add	r3, r6, r3, asl #4
	ldrb	r5, [r2, #1616]	@ zero_extendqisi2
	strb	r5, [r3, #1656]
	mov	r0, r5
	ldrb	r3, [r6, #1612]	@ zero_extendqisi2
	cmp	r3, #1
	bne	.L1330
	bl	NandcWaitFlashReady
	b	.L1331
.L1330:
	bl	NandcFlashCs
	mov	r0, r5
	ldr	r3, [sp, #28]
	ldr	r1, [sp, #24]
	add	r3, r6, r3, asl #2
	ldr	r2, [r3, #1624]
	adds	r2, r2, #0
	movne	r2, #1
	bl	FlashWaitReadyEN
	mov	r0, r5
	bl	NandcFlashDeCs
.L1331:
	ldr	r3, [sp, #16]
	sub	r3, r3, #1
	cmp	r3, #7
	bhi	.L1332
	ldr	r3, .L1379+12
	add	r3, r3, r5
	ldrb	r3, [r3, #-3416]	@ zero_extendqisi2
	cmp	r3, #0
	beq	.L1332
	mov	r0, r5
	ldrb	r1, [r6, #3917]	@ zero_extendqisi2
	ldr	r2, .L1379+16
	mov	r3, #0
	bl	HynixSetRRPara
.L1332:
	mov	r0, r5
	bl	NandcFlashCs
	cmp	r9, #1
	mov	r0, r5
	bne	.L1333
	ldrb	r3, [r6, #3904]	@ zero_extendqisi2
	cmp	r3, #0
	beq	.L1333
	bl	flash_enter_slc_mode
	b	.L1334
.L1333:
	bl	flash_exit_slc_mode
.L1334:
	mov	r0, r5
	ldr	r1, [sp, #24]
	bl	FlashProgFirstCmd
	ldr	r3, [fp, #12]
	mov	r0, r5
	mov	r1, #1
	str	r3, [sp]
	ldr	r2, [sp, #12]
	ldr	r3, [fp, #8]
	bl	NandcXferData
	cmp	r10, #0
	beq	.L1335
	mov	r0, r5
	ldr	r1, [sp, #24]
	bl	FlashProgDpFirstCmd
	mov	r0, r5
	add	r7, r7, #36
	add	r7, r4, r7
	ldr	r3, [sp, #28]
	ldr	r1, [sp, #24]
	add	r3, r6, r3, asl #2
	ldr	r2, [r3, #1624]
	adds	r2, r2, #0
	movne	r2, #1
	bl	FlashWaitReadyEN
	ldr	r1, [r6, #4]
	mov	r0, r5
	ldr	r3, [sp, #24]
	add	r1, r1, r3
	bl	FlashProgDpSecondCmd
	ldr	r3, [r7, #12]
	mov	r0, r5
	mov	r1, #1
	str	r3, [sp]
	ldr	r2, [sp, #12]
	ldr	r3, [r7, #8]
	bl	NandcXferData
.L1335:
	mov	r0, r5
	ldr	r1, [sp, #24]
	bl	FlashProgSecondCmd
	mov	r0, r5
	bl	NandcFlashDeCs
	add	r8, r8, r10
	b	.L1325
.L1377:
	ldr	r0, [r5, #3840]
	mov	r6, #0
	bl	NandcIqrWaitFlashReady
	ldr	r8, .L1379+20
	mov	r7, r5
.L1337:
	ldrb	r3, [r5, #1612]	@ zero_extendqisi2
	cmp	r6, r3
	bcs	.L1378
	uxtb	r0, r6
	bl	FlashWaitCmdDone
	cmp	r9, #1
	bne	.L1338
	ldrb	r3, [r7, #3904]	@ zero_extendqisi2
	cmp	r3, #0
	beq	.L1338
	ldrb	r0, [r8, r6, asl #4]	@ zero_extendqisi2
	bl	flash_exit_slc_mode
.L1338:
	add	r6, r6, #1
	b	.L1337
.L1378:
	ldr	r3, [sp, #20]
	cmp	r3, #0
	bne	.L1340
.L1348:
	mov	r0, #0
	b	.L1323
.L1340:
	ldr	r5, .L1379+12
	mov	r6, #0
	mov	r7, r5
.L1341:
	ldr	r3, [sp, #8]
	cmp	r6, r3
	beq	.L1348
	ldr	r3, [r4]
	cmn	r3, #1
	bne	.L1342
	ldr	r1, [r4, #4]
	ldr	r0, .L1379+24
	bl	printk
	b	.L1343
.L1342:
	ldr	r3, [sp, #8]
	mov	r1, r9
	add	r2, sp, #24
	mov	r0, r4
	rsb	r3, r6, r3
	uxtb	r3, r3
	str	r3, [sp]
	add	r3, sp, #28
	bl	LogAddr2PhyAddr
	ldr	r2, [r5, #-3300]
	mov	r3, #0
	mov	lr, r4
	str	r3, [r2]
	ldr	r2, [r5, #-3296]
	str	r3, [r2]
	ldmia	lr!, {r0, r1, r2, r3}
	add	ip, sp, #32
	stmia	ip!, {r0, r1, r2, r3}
	ldmia	lr!, {r0, r1, r2, r3}
	stmia	ip!, {r0, r1, r2, r3}
	add	r0, sp, #32
	ldr	r3, [lr]
	mov	r1, #1
	mov	r2, r9
	str	r3, [ip]
	ldr	r3, [r5, #-3300]
	str	r3, [sp, #40]
	ldr	r3, [r5, #-3296]
	str	r3, [sp, #44]
	bl	FlashReadPages
	ldr	r8, [sp, #32]
	cmn	r8, #1
	bne	.L1344
	ldr	r0, .L1379+28
	ldr	r1, [r4, #4]
	bl	printk
	str	r8, [r4]
.L1344:
	ldr	r3, [r4, #12]
	cmp	r3, #0
	beq	.L1345
	ldr	r2, [r3]
	ldr	r3, [r7, #-3296]
	ldr	r3, [r3]
	cmp	r2, r3
	beq	.L1345
	ldr	r0, .L1379+32
	ldr	r1, [r4, #4]
	bl	printk
	mvn	r3, #0
	str	r3, [r4]
.L1345:
	ldr	r3, [r4, #8]
	cmp	r3, #0
	beq	.L1343
	ldr	r2, [r3]
	ldr	r3, [r7, #-3300]
	ldr	r3, [r3]
	cmp	r2, r3
	beq	.L1343
	ldr	r0, .L1379+36
	ldr	r1, [r4, #4]
	bl	printk
	mvn	r3, #0
	str	r3, [r4]
.L1343:
	add	r6, r6, #1
	add	r4, r4, #36
	b	.L1341
.L1323:
	ldr	r3, .L1379
	ldr	r2, [sp, #68]
	ldr	r3, [r3]
	cmp	r2, r3
	beq	.L1349
	bl	__stack_chk_fail
.L1349:
	add	sp, sp, #76
	@ sp needed
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L1380:
	.align	2
.L1379:
	.word	__stack_chk_guard
	.word	.LANCHOR0
	.word	.LANCHOR1
	.word	.LANCHOR2
	.word	.LANCHOR0+3920
	.word	.LANCHOR0+1656
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
	ldr	r3, .L1386
	stmfd	sp!, {r4, r5, r6, lr}
	.save {r4, r5, r6, lr}
	.pad #104
	sub	sp, sp, #104
	ldr	r2, [r3]
	mov	r4, r0
	ldr	r5, .L1386+4
	mov	r6, r3
	str	r2, [sp, #100]
	ldr	r2, [r5, #-3316]
	cmp	r0, r2
	movcc	r4, #0
	bcc	.L1382
	ldr	r3, [r5, #-3308]
	add	r0, sp, #36
	mov	r1, #165
	mov	r2, #32
	str	r0, [sp, #12]
	mov	r4, r4, asl #10
	str	r3, [sp, #8]
	bl	ftl_memset
	mov	r1, #90
	mov	r2, #8
	ldr	r0, [r5, #-3308]
	bl	ftl_memset
	mov	r1, #1
	mov	r2, r1
	mov	r0, sp
	str	r4, [sp, #4]
	bl	FlashEraseBlocks
	mov	r1, #1
	mov	r2, r1
	mov	r3, r1
	mov	r0, sp
	bl	FlashProgPages
	mov	r1, #0
	mov	r2, #1
	ldr	r4, [sp]
	mov	r0, sp
	adds	r4, r4, #0
	movne	r4, #1
	rsb	r4, r4, #0
	bl	FlashEraseBlocks
.L1382:
	ldr	r2, [sp, #100]
	mov	r0, r4
	ldr	r3, [r6]
	cmp	r2, r3
	beq	.L1383
	bl	__stack_chk_fail
.L1383:
	add	sp, sp, #104
	@ sp needed
	ldmfd	sp!, {r4, r5, r6, pc}
.L1387:
	.align	2
.L1386:
	.word	__stack_chk_guard
	.word	.LANCHOR2
	.fnend
	.size	FlashTestBlk, .-FlashTestBlk
	.align	2
	.global	FlashMakeFactorBbt
	.type	FlashMakeFactorBbt, %function
FlashMakeFactorBbt:
	.fnstart
	@ args = 0, pretend = 0, frame = 88
	@ frame_needed = 0, uses_anonymous_args = 0
	ldr	r3, .L1444
	mov	r1, #1
	stmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.save {r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.pad #92
	sub	sp, sp, #92
	ldr	r3, [r3]
	ldr	r4, .L1444+4
	ldr	r6, .L1444+8
	str	r3, [sp, #84]
	ldr	r3, [r4, #-3304]
	ldr	r5, .L1444+12
	ldrh	r8, [r6, #12]
	str	r3, [sp, #20]
	ldrh	r3, [r6, #14]
	ldr	r0, .L1444+16
	ldr	r7, .L1444+12
	smulbb	r8, r8, r3
	ldr	r3, [r5, #1784]
	ldrb	r3, [r3, #24]	@ zero_extendqisi2
	uxth	r8, r8
	str	r3, [sp, #24]
	ldrh	r3, [r5, #4]
	str	r3, [sp, #16]
	ldrb	r3, [r5]	@ zero_extendqisi2
	cmp	r3, #1
	ldreq	r3, [sp, #16]
	moveq	r3, r3, asl #1
	uxtheq	r3, r3
	streq	r3, [sp, #16]
	bl	printk
	ldr	r0, [r4, #-3304]
	mov	r1, #0
	mov	r2, #4096
	bl	ftl_memset
	ldr	r4, .L1444+4
	mov	r3, r8, lsr #4
	str	r3, [sp, #28]
	mov	r3, #0
	str	r3, [sp, #8]
.L1390:
	ldrb	r6, [sp, #8]	@ zero_extendqisi2
	ldrb	r3, [r7, #1612]	@ zero_extendqisi2
	cmp	r3, r6
	bls	.L1440
	ldr	r2, .L1444+20
	mov	r3, r6, asl #1
	ldrh	r5, [r2, r3]
	cmp	r5, #0
	bne	.L1420
	ldr	r3, .L1444+8
	mov	r1, r5
	ldr	r0, [r4, #-3408]
	mov	r9, r5
	ldrh	r2, [r3, #20]
	mov	r2, r2, asl #9
	bl	ftl_memset
	add	r3, r7, r6
	ldrb	r10, [r3, #1616]	@ zero_extendqisi2
	mov	r3, r6, asl #2
	add	fp, r7, r3
	str	r5, [sp, #4]
	str	r3, [sp, #32]
.L1392:
	ldrh	r3, [sp, #4]
	cmp	r3, r8
	str	r3, [sp, #12]
	bcs	.L1401
	mvn	r3, #0
	strb	r3, [sp, #46]
	strb	r3, [sp, #47]
	ldr	r3, [sp, #24]
	tst	r3, #1
	beq	.L1394
	ldr	r3, [fp, #1624]
	add	r2, sp, #46
	mov	r0, r10
	add	r3, r5, r3
	str	r3, [sp, #36]
	mov	r1, r3
	bl	FlashReadSpare
	ldrb	r2, [r7]	@ zero_extendqisi2
	cmp	r2, #1
	ldr	r3, [sp, #36]
	bne	.L1394
	ldr	r1, [r7, #4]
	add	r2, sp, #47
	mov	r0, r10
	add	r1, r3, r1
	bl	FlashReadSpare
	ldrb	r3, [sp, #46]	@ zero_extendqisi2
	ldrb	r2, [sp, #47]	@ zero_extendqisi2
	and	r3, r3, r2
	strb	r3, [sp, #46]
.L1394:
	ldr	r3, [sp, #24]
	tst	r3, #2
	beq	.L1396
	ldr	r3, [r7, #1784]
	mov	r0, r10
	add	r2, sp, #47
	ldrh	r1, [r3, #10]
	ldr	r3, [fp, #1624]
	sub	r1, r1, #1
	add	r1, r1, r3
	add	r1, r1, r5
	bl	FlashReadSpare
.L1396:
	ldr	r3, [r7, #1784]
	ldrb	r3, [r3, #7]	@ zero_extendqisi2
	cmp	r3, #1
	cmpne	r3, #8
	ldrb	r3, [sp, #46]	@ zero_extendqisi2
	bne	.L1397
	cmp	r3, #0
	ldrneb	r0, [sp, #47]	@ zero_extendqisi2
	clzne	r0, r0
	movne	r0, r0, lsr #5
	bne	.L1398
	b	.L1419
.L1397:
	cmp	r3, #255
	bne	.L1419
	ldrb	r0, [sp, #47]	@ zero_extendqisi2
	subs	r0, r0, #255
	movne	r0, #1
	b	.L1398
.L1419:
	mov	r0, #1
.L1398:
	ldr	r3, [sp, #24]
	tst	r3, #4
	beq	.L1399
	ldr	r3, .L1444+12
	mov	r0, r10
	ldr	r2, [sp, #32]
	add	r3, r3, r2
	ldr	r1, [r3, #1624]
	add	r1, r5, r1
	bl	SandiskProgTestBadBlock
.L1399:
	cmp	r0, #0
	beq	.L1400
	mov	r1, r6
	ldr	r2, [sp, #4]
	ldr	r0, .L1444+24
	add	r9, r9, #1
	bl	printk
	ldr	r1, [r4, #-3408]
	mov	ip, #1
	uxth	r9, r9
	ldr	r3, [sp, #12]
	mov	r0, r3, lsr #5
	and	r3, r3, #31
	ldr	r2, [r1, r0, asl #2]
	orr	r3, r2, ip, asl r3
	ldr	r2, [sp, #28]
	str	r3, [r1, r0, asl #2]
	ldrb	r3, [r7, #1612]	@ zero_extendqisi2
	mul	r3, r3, r2
	cmp	r9, r3
	bgt	.L1401
.L1400:
	ldr	r3, [sp, #4]
	add	r3, r3, #1
	str	r3, [sp, #4]
	ldr	r3, [sp, #16]
	add	r5, r5, r3
	b	.L1392
.L1401:
	mov	r2, r9
	ldr	r0, .L1444+28
	mov	r1, r6
	bl	printk
	ldrb	r3, [r7, #1612]	@ zero_extendqisi2
	ldr	r2, [sp, #28]
	mul	r3, r3, r2
	cmp	r9, r3
	blt	.L1403
	ldr	r3, .L1444+8
	mov	r1, #0
	ldr	r0, [r4, #-3408]
	ldrh	r2, [r3, #20]
	mov	r2, r2, asl #9
	bl	ftl_memset
.L1403:
	cmp	r6, #0
	bne	.L1405
	ldr	r3, [r4, #-3316]
	mov	r5, r6
	mov	r9, #1
	uxth	r10, r3
.L1406:
	ldr	r3, .L1444+12
	ldrb	r3, [r3, #1]	@ zero_extendqisi2
	cmp	r3, r10
	bls	.L1441
	mov	r0, r10
	bl	FlashTestBlk
	cmp	r0, #0
	beq	.L1407
	mov	r1, r10
	ldr	r0, .L1444+32
	bl	printk
	ldr	r1, [r4, #-3408]
	mov	r0, r10, lsr #5
	add	r5, r5, #1
	and	r3, r10, #31
	ldr	r2, [r1, r0, asl #2]
	uxth	r5, r5
	orr	r3, r2, r9, asl r3
	str	r3, [r1, r0, asl #2]
.L1407:
	add	r10, r10, #1
	uxth	r10, r10
	b	.L1406
.L1441:
	sub	fp, r8, #1
	sub	r9, r8, #50
	mov	r10, #1
	uxth	fp, fp
.L1409:
	cmp	fp, r9
	ble	.L1442
	mov	r0, fp
	bl	FlashTestBlk
	cmp	r0, #0
	beq	.L1410
	mov	r1, fp
	ldr	r0, .L1444+32
	bl	printk
	ldr	r1, [r4, #-3408]
	mov	r0, fp, lsr #5
	and	r3, fp, #31
	ldr	r2, [r1, r0, asl #2]
	orr	r3, r2, r10, asl r3
	str	r3, [r1, r0, asl #2]
.L1410:
	sub	fp, fp, #1
	uxth	fp, fp
	b	.L1409
.L1442:
	ldr	r3, .L1444+12
	ldr	r2, [r4, #-3316]
	ldrb	r3, [r3, #1]	@ zero_extendqisi2
	rsb	r3, r2, r3
	cmp	r5, r3
	bcc	.L1405
	ldr	r3, .L1444+8
	mov	r1, #0
	ldr	r0, [r4, #-3408]
	ldrh	r2, [r3, #20]
	mov	r2, r2, asl #9
	bl	ftl_memset
.L1405:
	ldrb	r5, [sp, #8]	@ zero_extendqisi2
	sub	r10, r8, #1
	ldr	r9, .L1444+20
	uxth	r10, r10
	mul	r5, r8, r5
	add	r9, r9, r6, asl #1
.L1413:
	mov	r1, r6
	ldr	r0, .L1444+36
	mov	r2, r10
	bl	printk
	ldr	r1, [r4, #-3408]
.L1414:
	mov	r2, r10, lsr #5
	and	r3, r10, #31
	ldr	r2, [r1, r2, asl #2]
	mov	r3, r2, lsr r3
	ands	r3, r3, #1
	subne	r10, r10, #1
	uxthne	r10, r10
	bne	.L1414
.L1443:
	ldr	r1, [sp, #20]
	add	r0, sp, #48
	ldr	r2, .L1444+40
	strh	r10, [r9]	@ movhi
	strh	r10, [r1, #2]	@ movhi
	strh	r2, [r1]	@ movhi
	strh	r3, [r1, #8]	@ movhi
	mov	r1, #1
	ldr	r3, [r4, #-3408]
	mov	r2, r1
	str	r3, [sp, #56]
	ldr	r3, [r4, #-3304]
	str	r3, [sp, #60]
	add	r3, r10, r5
	mov	r3, r3, asl #10
	str	r3, [sp, #52]
	bl	FlashEraseBlocks
	mov	r1, #1
	mov	r3, r1
	mov	r2, r1
	add	r0, sp, #48
	bl	FlashProgPages
	ldr	r3, [sp, #48]
	cmp	r3, #0
	subne	r10, r10, #1
	uxthne	r10, r10
	bne	.L1413
.L1420:
	ldr	r3, [sp, #8]
	add	r3, r3, #1
	str	r3, [sp, #8]
	b	.L1390
.L1440:
	ldr	r3, .L1444
	ldr	r2, [sp, #84]
	ldr	r3, [r3]
	cmp	r2, r3
	beq	.L1417
	bl	__stack_chk_fail
.L1417:
	add	sp, sp, #92
	@ sp needed
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L1445:
	.align	2
.L1444:
	.word	__stack_chk_guard
	.word	.LANCHOR2
	.word	.LANCHOR0+3876
	.word	.LANCHOR0
	.word	.LC25
	.word	.LANCHOR2-3288
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
.L1447:
	cmp	r2, r0
	uxth	r3, r1
	add	r1, r1, #1
	movls	r2, r2, asl #1
	bls	.L1447
.L1449:
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
	ldr	r3, .L1453
	cmp	r0, #23
	sub	r2, r3, #3264
	movls	r0, #24
	sub	ip, r3, #3248
	str	r0, [r3, #-3272]
	ldrh	r2, [r2, #-4]
	ldrh	r1, [ip, #-10]
	mul	r2, r2, r0
	rsb	r0, r0, r1
	ldr	r1, [r3, #-3252]
	strh	r0, [ip, #-12]	@ movhi
	mov	r0, #0
	str	r2, [r3, #-3264]
	rsb	r2, r2, r1
	str	r2, [r3, #-3256]
	bx	lr
.L1454:
	.align	2
.L1453:
	.word	.LANCHOR2
	.fnend
	.size	FtlSysBlkNumInit, .-FtlSysBlkNumInit
	.align	2
	.global	FtlConstantsInit
	.type	FtlConstantsInit, %function
FtlConstantsInit:
	.fnstart
	@ args = 0, pretend = 0, frame = 16
	@ frame_needed = 0, uses_anonymous_args = 0
	stmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.save {r4, r5, r6, r7, r8, r9, r10, fp, lr}
	mov	r5, r0
	ldrh	r7, [r0, #8]
	.pad #20
	sub	sp, sp, #20
	ldr	r0, .L1483
	mov	r9, #0
	ldrh	ip, [r5, #10]
	ldrh	r6, [r5, #12]
	add	r3, r0, #8
	ldrh	r8, [r5, #14]
	mov	r4, r0
	strh	r7, [r4], #16	@ movhi
	str	r0, [sp]
	strh	ip, [r4, #-14]	@ movhi
	strh	r6, [r4, #-12]	@ movhi
	strh	r8, [r0, #-10]	@ movhi
.L1456:
	strb	r9, [r9, r3]
	add	r9, r9, #1
	cmp	r9, #32
	bne	.L1456
	ldrh	r2, [r5, #14]
	ldrh	r3, [r5, #20]
	cmp	r3, r2, lsr #8
	bcs	.L1457
	uxtb	r9, r6
	mov	r3, r9, asl #1
	uxtb	r3, r3
	str	r3, [sp, #8]
	sub	r3, ip, #1
	mul	r3, r6, r3
	str	r3, [sp, #4]
	mov	r3, #0
.L1458:
	cmp	r3, r6
	bcs	.L1460
	ldr	r1, [sp, #4]
	rsb	fp, r6, r3
	ldr	r0, .L1483+4
	uxtb	r2, r3
	ldr	lr, .L1483+4
	add	r1, r3, r1
	add	r1, r0, r1
	str	r1, [sp, #12]
	add	fp, lr, fp
	mov	r1, #0
	mov	r0, r1
.L1461:
	cmp	r0, ip
	add	r1, r1, r6
	bcs	.L1482
	ldr	lr, [sp, #12]
	add	r10, r2, r9
	strb	r2, [fp, r1]
	add	r0, r0, #1
	strb	r10, [lr, r1]
	ldr	lr, [sp, #8]
	add	r2, r2, lr
	uxtb	r2, r2
	b	.L1461
.L1482:
	add	r3, r3, #1
	b	.L1458
.L1460:
	ldr	r3, [sp]
	mov	ip, ip, asl #1
	mov	r8, r8, lsr #1
	strh	ip, [r4, #-14]	@ movhi
	strh	r8, [r3, #-10]	@ movhi
.L1457:
	ldr	r9, .L1483+8
	mov	r3, #5
	cmp	r7, #1
	ldr	ip, .L1483+12
	ldrh	fp, [r5, #16]
	strh	r3, [r9, #-8]	@ movhi
	mov	r3, #0
	strh	r3, [r9, #-6]	@ movhi
	mov	r3, #4352
	strh	r3, [r9, #-4]	@ movhi
	sub	r8, ip, #3200
	ldr	r3, .L1483+16
	streqh	r7, [r9, #-8]	@ movhi
	ldrh	r7, [r4, #-14]
	ldrb	r10, [r3]	@ zero_extendqisi2
	ldrh	r1, [r5, #18]
	cmp	r10, #0
	strh	fp, [r8]	@ movhi
	smulbb	r7, r7, r6
	ldrne	r3, .L1483+8
	movne	r2, #384
	str	ip, [sp, #12]
	str	r1, [sp, #8]
	strneh	r2, [r3, #-4]	@ movhi
	sub	r3, ip, #3264
	ldrh	r2, [r5, #20]
	uxth	r7, r7
	strh	r7, [r3, #-4]	@ movhi
	ldr	r3, [sp]
	mov	r0, r2
	str	r2, [sp, #4]
	ldrh	r4, [r3, #-10]
	smulbb	r3, r7, fp
	smulbb	r6, r4, r6
	strh	r6, [r8, #-2]	@ movhi
	sub	r6, ip, #3184
	strh	r1, [r6, #-14]	@ movhi
	strh	r3, [r6, #-12]	@ movhi
	strh	r2, [r6, #-10]	@ movhi
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
	str	r0, [ip, #-3252]
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
	str	fp, [ip, #-3184]
	bl	__aeabi_idiv
	ldr	fp, .L1483+20
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
	ldr	r10, .L1483+12
	mov	r2, r2, asr r3
	mul	r3, r7, r0
	add	r2, r2, #2
	ldrh	r0, [fp, #-12]
	strh	r2, [fp, #-10]	@ movhi
	str	r3, [r5, #-3172]
	bl	__aeabi_uidiv
	cmp	r7, #1
	ldr	r7, .L1483+12
	sub	r8, r7, #3152
	uxtah	r0, r4, r0
	addeq	r0, r0, #4
	str	r0, [r10, #-3272]
	ldr	r3, [r5, #-3272]
	uxth	r0, r3
	bl	FtlSysBlkNumInit
	ldr	r3, [r5, #-3272]
	ldr	r2, [r5, #-3256]
	mov	r0, #2048
	ldrh	r4, [r6, #-8]
	str	r3, [r5, #-3168]
	ldrh	r3, [r9]
	mov	r2, r2, asl #2
	add	r4, r4, #9
	ldrh	r6, [r6, #-10]
	mul	r3, r3, r2
	mov	r1, r6
	mov	r4, r3, lsr r4
	add	r4, r4, #2
	uxth	r4, r4
	strh	r4, [r8, #-12]	@ movhi
	bl	__aeabi_idiv
	ldr	r1, .L1483+16
	mov	r3, #0
	str	r3, [r1, #3908]
	ldrh	r3, [fp, #-12]
	ldrb	r1, [r1, #3904]	@ zero_extendqisi2
	add	r2, r3, #3
	strh	r2, [fp, #-12]	@ movhi
	cmp	r1, #0
	ldr	fp, [r5, #-3172]
	add	r2, fp, #3
	str	r2, [r5, #-3172]
	strh	r0, [r8, #-10]	@ movhi
	sub	r0, r7, #3168
	addne	r3, r3, #4
	addne	fp, fp, #5
	strneh	r3, [r0, #-12]	@ movhi
	strne	fp, [r7, #-3172]
	bne	.L1471
.L1470:
	cmp	r2, #7
	movls	r3, #8
	strls	r3, [r7, #-3172]
.L1471:
	mov	r3, #0
	strh	r3, [r8, #-8]	@ movhi
	ldr	r3, [sp]
	mov	r0, #0
	ldrh	r2, [r3, #-12]
	mov	r3, r2, lsr #3
	add	r3, r3, r2, asl #1
	add	r3, r3, #52
	add	r4, r3, r4, asl #2
	cmp	r4, r6, asl #9
	ldrcc	r3, .L1483+24
	movcc	r2, #1
	strcch	r2, [r3, #-8]	@ movhi
	add	sp, sp, #20
	@ sp needed
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L1484:
	.align	2
.L1483:
	.word	.LANCHOR2-3248
	.word	.LANCHOR2-3240
	.word	.LANCHOR2-3200
	.word	.LANCHOR2
	.word	.LANCHOR0
	.word	.LANCHOR2-3168
	.word	.LANCHOR2-3152
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
	ldr	r4, .L1590
	movw	r3, #65535
	mov	r0, #1024
	mov	r10, #36
	sub	r2, r4, #3072
	sub	r6, r4, #3056
	sub	r7, r4, #3152
	str	r3, [r4, #-3088]
	strh	r5, [r6, #-4]	@ movhi
	mvn	r3, #0
	strh	r5, [r6, #-2]	@ movhi
	sub	r9, r4, #3264
	strh	r3, [r2]	@ movhi
	strh	r3, [r6, #-14]	@ movhi
	strh	r3, [r6, #-12]	@ movhi
	strh	r3, [r6, #-10]	@ movhi
	mov	r3, #32
	strh	r5, [r6]	@ movhi
	strh	r3, [r6, #-8]	@ movhi
	mov	r3, #128
	strh	r3, [r6, #-6]	@ movhi
	sub	r3, r4, #3040
	sub	r6, r4, #3184
	strh	r5, [r7, #-6]	@ movhi
	strh	r5, [r3, #-14]	@ movhi
	str	r5, [r4, #-3156]
	str	r5, [r4, #-3152]
	str	r5, [r4, #-3148]
	str	r5, [r4, #-3144]
	str	r5, [r4, #-3140]
	str	r5, [r4, #-3136]
	str	r5, [r4, #-3132]
	str	r5, [r4, #-3128]
	str	r5, [r4, #-3124]
	str	r5, [r4, #-3120]
	str	r5, [r4, #-3116]
	str	r5, [r4, #-3112]
	str	r5, [r4, #-3108]
	str	r5, [r4, #-3104]
	str	r5, [r4, #-3100]
	str	r5, [r4, #-3096]
	str	r5, [r4, #-3092]
	str	r5, [r4, #-3084]
	str	r5, [r4, #-3080]
	str	r5, [r4, #-3076]
	ldrh	r1, [r6, #-10]
	bl	__aeabi_idiv
	ldrh	r8, [r9, #-4]
	str	r5, [r4, #-3048]
	mov	r8, r8, asl #2
	cmp	r0, r8
	str	r0, [r4, #-3052]
	ldrh	r0, [r6, #-12]
	strhi	r8, [r4, #-3052]
	mov	r8, #12
	mov	r0, r0, asl #1
	bl	ftl_malloc
	str	r0, [r4, #-3044]
	ldrh	r0, [r6, #-12]
	mul	r0, r8, r0
	bl	ftl_malloc
	ldrh	r5, [r9, #-4]
	mul	r5, r10, r5
	mov	fp, r5, asl #3
	str	r0, [r4, #-3040]
	mov	r0, fp
	bl	ftl_malloc
	str	r0, [r4, #-3036]
	mov	r0, r5
	bl	ftl_malloc
	str	r0, [r4, #-3032]
	mov	r0, fp
	bl	ftl_malloc
	str	r0, [r4, #-3028]
	mov	r0, r5
	bl	ftl_malloc
	str	r0, [r4, #-3024]
	mov	r0, r5
	bl	ftl_malloc
	str	r0, [r4, #-3020]
	ldr	r0, [r4, #-3052]
	mul	r0, r10, r0
	bl	ftl_malloc
	ldrh	r5, [r6, #-6]
	ldrh	r3, [r9, #-4]
	mov	r3, r3, asl #1
	add	r3, r3, #1
	str	r3, [r4, #-3012]
	str	r0, [r4, #-3016]
	mov	r0, r5
	bl	ftl_malloc
	str	r0, [r4, #-3008]
	mov	r0, r5
	bl	ftl_malloc
	str	r0, [r4, #-3004]
	mov	r0, r5
	bl	ftl_malloc
	str	r0, [r4, #-3000]
	ldr	r0, [r4, #-3012]
	mul	r0, r0, r5
	bl	ftl_malloc
	str	r0, [r4, #-2996]
	ldr	r0, [r4, #-3052]
	mul	r0, r0, r5
	bl	ftl_malloc
	str	r0, [r4, #-2992]
	mov	r0, r5
	bl	ftl_malloc
	str	r0, [r4, #-2988]
	mov	r0, r5
	bl	ftl_malloc
	str	r0, [r4, #-2984]
	ldr	r0, [r4, #-3012]
	mul	r0, r8, r0
	bl	ftl_malloc
	ldrh	r3, [r9, #-4]
	ldrh	r5, [r6, #-4]
	mul	r5, r3, r5
	str	r0, [r4, #-2980]
	mov	r0, r5
	bl	ftl_malloc
	str	r0, [r4, #-2976]
	mov	r0, r5, asl #3
	bl	ftl_malloc
	ldrh	r3, [r6, #-4]
	ldr	r5, .L1590+4
	add	r9, r5, #288
	str	r0, [r4, #-2972]
	ldr	r0, [r4, #-3012]
	mul	r0, r0, r3
	bl	ftl_malloc
	ldrh	r3, [r6, #-4]
	str	r0, [r4, #-2968]
	ldr	r0, [r4, #-3052]
	mul	r0, r0, r3
	bl	ftl_malloc
	str	r0, [r4, #-2964]
	ldrh	r0, [r5, #-10]
	mov	r0, r0, asl #1
	uxth	r0, r0
	strh	r0, [r9]	@ movhi
	bl	ftl_malloc
	str	r0, [r4, #-2956]
	ldrh	r0, [r9]
	add	r0, r0, #544
	add	r0, r0, #3
	mov	r0, r0, lsr #9
	strh	r0, [r9]	@ movhi
	mov	r0, r0, asl #9
	bl	ftl_malloc
	ldrh	r9, [r5, #-10]
	mov	r9, r9, asl #1
	str	r0, [r4, #-2952]
	add	r0, r0, #32
	str	r0, [r4, #-2948]
	mov	r0, r9
	bl	ftl_malloc
	str	r0, [r4, #-2944]
	mov	r0, r9
	bl	ftl_malloc
	ldr	r9, [r4, #-3172]
	mov	r9, r9, asl #1
	str	r0, [r4, #-2940]
	mov	r0, r9
	bl	ftl_malloc
	str	r0, [r4, #-2936]
	mov	r0, r9
	bl	ftl_malloc
	str	r0, [r4, #-2932]
	ldrh	r0, [r5, #-10]
	mov	r0, r0, lsr #3
	add	r0, r0, #4
	bl	ftl_malloc
	str	r0, [r4, #-2928]
	ldrh	r0, [r5, #68]
	mov	r0, r0, asl #1
	bl	ftl_malloc
	str	r0, [r4, #-2924]
	ldrh	r0, [r5, #68]
	mov	r0, r0, asl #1
	bl	ftl_malloc
	str	r0, [r4, #-2920]
	ldrh	r0, [r5, #68]
	mov	r0, r0, asl #2
	bl	ftl_malloc
	str	r0, [r4, #-2916]
	ldrh	r0, [r5, #70]
	mov	r0, r0, asl #2
	bl	ftl_malloc
	ldrh	r2, [r5, #70]
	mov	r1, #0
	mov	r2, r2, asl #2
	str	r0, [r4, #-2912]
	bl	ftl_memset
	ldrh	r9, [r7, #-12]
	mov	r9, r9, asl #2
	mov	r0, r9
	bl	ftl_malloc
	str	r0, [r4, #-2908]
	mov	r0, r9
	bl	ftl_malloc
	str	r0, [r4, #-2904]
	ldr	r0, [r4, #-3172]
	mov	r0, r0, asl #2
	bl	ftl_malloc
	str	r0, [r4, #-2900]
	ldrh	r0, [r7, #-10]
	mul	r0, r8, r0
	bl	ftl_malloc
	ldrh	r3, [r7, #-10]
	add	r7, r5, #16
	str	r0, [r4, #-2896]
	ldrh	r0, [r6, #-6]
	add	r6, r5, #368
	mul	r0, r0, r3
	bl	ftl_malloc
	ldrh	r3, [r5, #-10]
	str	r0, [r4, #-2892]
	mov	r0, #6
	mul	r0, r0, r3
	bl	ftl_malloc
	ldrh	r3, [r5, #46]
	add	r3, r3, #31
	mov	r3, r3, asr #5
	strh	r3, [r6, #-4]	@ movhi
	str	r0, [r4, #-2888]
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
	str	r0, [r4, #-2852]
.L1487:
	cmp	r3, ip
	bcs	.L1588
	ldr	r0, [r4, #-2852]
	add	r3, r3, #1
	add	r0, r0, r1
	add	r1, r1, r2
	str	r0, [r5, #4]!
	b	.L1487
.L1588:
	mov	r2, #0
.L1489:
	cmp	r3, #8
	addne	r1, r6, r3, asl #2
	addne	r3, r3, #1
	strne	r2, [r1, #28]
	bne	.L1489
.L1589:
	ldr	r2, [r4, #-2936]
	ldr	r3, .L1590
	cmp	r2, #0
	bne	.L1491
.L1493:
	ldr	r1, .L1590+8
	ldr	r0, .L1590+12
	bl	printk
	mvn	r0, #0
	ldmfd	sp!, {r3, r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L1491:
	ldr	r2, [r3, #-2932]
	cmp	r2, #0
	beq	.L1493
	ldr	r2, [r3, #-2908]
	cmp	r2, #0
	beq	.L1493
	ldr	r2, [r3, #-2900]
	cmp	r2, #0
	beq	.L1493
	ldr	r2, [r3, #-2896]
	cmp	r2, #0
	beq	.L1493
	ldr	r2, [r3, #-2892]
	cmp	r2, #0
	beq	.L1493
	ldr	r2, [r3, #-2888]
	cmp	r2, #0
	beq	.L1493
	ldr	r2, [r3, #-2852]
	cmp	r2, #0
	beq	.L1493
	ldr	r3, [r3, #-2940]
	cmp	r3, #0
	beq	.L1493
	ldr	r2, [r4, #-3044]
	ldr	r3, .L1590
	cmp	r2, #0
	beq	.L1493
	ldr	r2, [r3, #-3040]
	cmp	r2, #0
	beq	.L1493
	ldr	r2, [r3, #-3036]
	cmp	r2, #0
	beq	.L1493
	ldr	r2, [r3, #-3028]
	cmp	r2, #0
	beq	.L1493
	ldr	r2, [r3, #-3024]
	cmp	r2, #0
	beq	.L1493
	ldr	r2, [r3, #-3020]
	cmp	r2, #0
	beq	.L1493
	ldr	r2, [r3, #-3032]
	cmp	r2, #0
	beq	.L1493
	ldr	r2, [r3, #-3008]
	cmp	r2, #0
	beq	.L1493
	ldr	r2, [r3, #-3004]
	cmp	r2, #0
	beq	.L1493
	ldr	r3, [r3, #-3000]
	cmp	r3, #0
	beq	.L1493
	ldr	r2, [r4, #-2996]
	ldr	r3, .L1590
	cmp	r2, #0
	beq	.L1493
	ldr	r2, [r3, #-2988]
	cmp	r2, #0
	beq	.L1493
	ldr	r2, [r3, #-2984]
	cmp	r2, #0
	beq	.L1493
	ldr	r2, [r3, #-2980]
	cmp	r2, #0
	beq	.L1493
	ldr	r2, [r3, #-2976]
	cmp	r2, #0
	beq	.L1493
	ldr	r2, [r3, #-2972]
	cmp	r2, #0
	beq	.L1493
	ldr	r2, [r3, #-2968]
	cmp	r2, #0
	beq	.L1493
	ldr	r2, [r3, #-2948]
	cmp	r2, #0
	beq	.L1493
	ldr	r2, [r3, #-2956]
	cmp	r2, #0
	beq	.L1493
	ldr	r3, [r3, #-2924]
	cmp	r3, #0
	beq	.L1493
	ldr	r3, .L1590
	ldr	r2, [r3, #-2920]
	cmp	r2, #0
	beq	.L1493
	ldr	r2, [r3, #-2916]
	cmp	r2, #0
	beq	.L1493
	ldr	r3, [r3, #-2912]
	cmp	r3, #0
	beq	.L1493
	mov	r0, #0
	ldmfd	sp!, {r3, r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L1591:
	.align	2
.L1590:
	.word	.LANCHOR2
	.word	.LANCHOR2-3248
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
	ldr	r2, .L1599
	sub	r3, r2, #2816
	ldrh	r3, [r3, #-4]
	cmp	r3, #0
	beq	.L1598
	ldr	r3, [r2, #-2924]
	sub	r2, r2, #3168
	ldrh	r2, [r2, #-12]
	add	r2, r3, r2, asl #1
.L1594:
	cmp	r3, r2
	beq	.L1598
	ldrh	r1, [r3], #2
	cmp	r1, r0
	bne	.L1594
	mov	r0, #1
	bx	lr
.L1598:
	mov	r0, #0
	bx	lr
.L1600:
	.align	2
.L1599:
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
	ldr	r2, .L1608
	ldr	r3, [r2, #-3048]
	cmp	r3, #0
	beq	.L1604
	stmfd	sp!, {r4, r5, lr}
	.save {r4, r5, lr}
	mov	r5, #36
	ldr	r4, [r2, #-3016]
	mov	r2, #0
.L1603:
	mla	ip, r5, r2, r4
	ldr	lr, [ip, #16]
	cmp	lr, r0
	movcc	ip, #0
	movcs	ip, #1
	cmp	lr, r1
	movhi	ip, #0
	cmp	ip, #0
	bne	.L1605
	add	r2, r2, #1
	cmp	r2, r3
	bne	.L1603
	mov	r0, ip
	ldmfd	sp!, {r4, r5, pc}
.L1604:
	mov	r0, r3
	bx	lr
.L1605:
	mov	r0, #1
	ldmfd	sp!, {r4, r5, pc}
.L1609:
	.align	2
.L1608:
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
	ldr	r3, .L1611
	ldr	r0, [r3, #3908]
	bx	lr
.L1612:
	.align	2
.L1611:
	.word	.LANCHOR0
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
	ldr	r3, .L1614
	ldr	r0, [r3, #3908]
	bx	lr
.L1615:
	.align	2
.L1614:
	.word	.LANCHOR0
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
	ldr	r3, .L1617
	ldr	r0, [r3, #-2816]
	bx	lr
.L1618:
	.align	2
.L1617:
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
	ldr	r5, .L1621
	sub	r3, r5, #3200
	ldrh	r4, [r3, #-2]
	mov	r1, r4
	bl	__aeabi_uidiv
	uxth	r2, r0
	smulbb	r3, r2, r4
	add	r1, r5, r2, asl #2
	mov	r4, #1
	sub	r5, r5, #2880
	ldr	ip, [r1, #-2852]
	rsb	r3, r3, r6
	uxth	r3, r3
	and	r1, r3, #31
	mov	lr, r3, lsr #5
	ldr	r0, [ip, lr, asl #2]
	orr	r1, r0, r4, asl r1
	ldr	r0, .L1621+4
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
.L1622:
	.align	2
.L1621:
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
	ldr	r5, .L1625
	sub	r3, r5, #3200
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
	ldr	r3, [r5, #-2852]
	ldr	r0, [r3, r2, asl #2]
	mov	r0, r0, lsr r4
	and	r0, r0, #1
	ldmfd	sp!, {r3, r4, r5, r6, r7, pc}
.L1626:
	.align	2
.L1625:
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
	ldr	r5, .L1634
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
.L1630:
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
	bne	.L1630
	ldmfd	sp!, {r4, r5, r6, pc}
.L1635:
	.align	2
.L1634:
	.word	.LANCHOR2-2880
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
	ldr	r4, .L1654
	ldr	r5, [r4, #-2812]
	cmp	r5, #0
	bne	.L1638
	ldr	r3, [r4, #-2976]
	mov	r1, r5
	ldr	r0, [r4, #-3008]
	sub	r7, r4, #2880
	ldr	r8, .L1654+4
	sub	fp, r7, #4
	str	r3, [r4, #-2796]
	sub	r3, r4, #3184
	str	r0, [r4, #-2800]
	mov	r10, r4
	ldrh	r2, [r3, #-6]
	bl	ftl_memset
	ldr	r9, .L1654+8
.L1639:
	ldrh	r3, [r9]
	ldr	r6, .L1654
	cmp	r5, r3
	bge	.L1653
	ldrh	r2, [fp]
	ldr	r3, [r10, #-2800]
	ldr	r1, [r8, #4]!
	mul	r0, r2, r5
	mov	r2, r2, asl #2
	add	r5, r5, #1
	add	r0, r3, r0, asl #2
	bl	ftl_memcpy
	b	.L1639
.L1653:
	ldr	r5, [r6, #-2796]
	mov	r1, #255
	mov	r2, #16
	ldr	r10, .L1654+12
	mov	r8, #0
	mov	r0, r5
	mov	r9, r8
	bl	ftl_memset
	ldr	r3, .L1654+16
	strh	r3, [r5]	@ movhi
	ldr	r3, [r6, #-2872]
	str	r3, [r5, #4]
	ldrh	r3, [r7]
	strh	r3, [r5, #2]	@ movhi
	ldrh	r3, [r7, #4]
	strh	r3, [r5, #8]	@ movhi
	ldrh	r3, [r7, #6]
	strh	r3, [r5, #10]	@ movhi
	ldr	r3, [r6, #-3272]
	strh	r3, [r5, #12]	@ movhi
.L1641:
	ldr	r3, [r4, #-3008]
	mov	ip, #0
	ldrh	r1, [r7]
	ldrh	r2, [r7, #2]
	str	r3, [r4, #-2800]
	ldr	r3, [r4, #-2976]
	str	ip, [r4, #-2808]
	str	ip, [sp, #12]
	str	r3, [r4, #-2796]
	orr	r3, r2, r1, asl #10
	ldrh	r0, [r5, #10]
	str	r3, [r4, #-2804]
	ldrh	r3, [r7, #4]
	str	r0, [sp]
	ldr	r0, .L1654+20
	bl	printk
	ldrh	r3, [r10]
	ldrh	r2, [r7, #2]
	sub	r3, r3, #1
	ldr	fp, .L1654+24
	cmp	r2, r3
	ldr	ip, [sp, #12]
	blt	.L1642
	ldr	r3, [r6, #-2872]
	mov	r1, #1
	ldrh	r2, [fp]
	add	r3, r3, #1
	ldr	r0, [r6, #-3024]
	str	r3, [r6, #-2872]
	str	r3, [r5, #4]
	ldrh	r3, [fp, #4]
	strh	r2, [r5, #8]	@ movhi
	strh	r2, [fp, #4]	@ movhi
	mov	r2, r1
	strh	r3, [fp]	@ movhi
	mov	r3, r3, asl #10
	str	r3, [r6, #-2804]
	str	r3, [r0, #4]
	strh	ip, [fp, #2]	@ movhi
	bl	FlashEraseBlocks
.L1642:
	mov	r1, #1
	ldr	r0, .L1654+28
	mov	r3, r1
	mov	r2, r1
	bl	FlashProgPages
	ldrh	r3, [r7, #2]
	ldr	fp, .L1654
	add	r3, r3, #1
	strh	r3, [r7, #2]	@ movhi
	ldr	r3, [r4, #-2808]
	cmn	r3, #1
	bne	.L1643
	add	r8, r8, #1
	ldr	r0, .L1654+32
	ldr	r1, [r6, #-2804]
	uxth	r8, r8
	bl	printk
	cmp	r8, #3
	bls	.L1641
	ldr	r0, .L1654+36
	mov	r2, r8
	ldr	r1, [fp, #-2804]
	bl	printk
	mov	r3, #1
	str	r3, [fp, #-2812]
	b	.L1638
.L1643:
	add	r9, r9, #1
	cmp	r9, #1
	beq	.L1641
	cmp	r3, #256
	beq	.L1641
.L1638:
	mov	r0, #0
	add	sp, sp, #20
	@ sp needed
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L1655:
	.align	2
.L1654:
	.word	.LANCHOR2
	.word	.LANCHOR2-2856
	.word	.LANCHOR2-3246
	.word	.LANCHOR2-3198
	.word	-3887
	.word	.LC32
	.word	.LANCHOR2-2880
	.word	.LANCHOR2-2808
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
	ldr	r2, .L1668
	stmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, lr}
	.save {r4, r5, r6, r7, r8, r9, r10, lr}
	mov	r5, #0
	ldr	r6, .L1668+4
	mov	r9, r2
	ldr	r3, [r2, #-3008]
	ldr	r7, [r2, #-2976]
	sub	r8, r6, #376
	str	r3, [r2, #-2800]
	str	r7, [r2, #-2796]
.L1657:
	ldrh	r3, [r8]
	cmp	r5, r3
	bcs	.L1667
	ldr	r2, .L1668+8
	mvn	r3, #0
	strh	r3, [r6, #2]!	@ movhi
	ldrh	r3, [r2]
	mov	r10, r2
	sub	r3, r3, #1
	uxth	r4, r3
.L1658:
	ldrh	r3, [r10]
	sub	r2, r3, #16
	cmp	r4, r2
	ble	.L1660
	mla	r3, r3, r5, r4
	mov	r1, #1
	ldr	r0, .L1668+12
	mov	r2, r1
	mov	r3, r3, asl #10
	str	r3, [r9, #-2804]
	bl	FlashReadPages
	ldr	r3, [r9, #-2808]
	cmn	r3, #1
	beq	.L1659
	ldrh	r2, [r7]
	movw	r3, #61664
	cmp	r2, r3
	streqh	r4, [r6]	@ movhi
	beq	.L1660
.L1659:
	sub	r4, r4, #1
	uxth	r4, r4
	b	.L1658
.L1660:
	add	r5, r5, #1
	b	.L1657
.L1667:
	mov	r0, #0
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, pc}
.L1669:
	.align	2
.L1668:
	.word	.LANCHOR2
	.word	.LANCHOR2-2870
	.word	.LANCHOR2-3202
	.word	.LANCHOR2-2808
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
	ldr	r0, .L1671
	mvn	r3, #0
	mov	r1, #255
	mov	r2, #16
	add	r0, r0, #12
	strh	r3, [r0, #-12]	@ movhi
	mov	r3, #0
	strh	r3, [r0, #-6]	@ movhi
	b	ftl_memset
.L1672:
	.align	2
.L1671:
	.word	.LANCHOR2-2880
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
	ldr	r4, .L1682
	ldrh	r6, [r4, #-2]
	ldrh	r3, [r4, #-46]
	mov	r4, r5
	mul	r6, r3, r6
.L1674:
	uxth	r0, r5
	cmp	r0, r6
	bge	.L1681
	bl	FtlBbmIsBadBlock
	add	r5, r5, #1
	cmp	r0, #0
	addne	r4, r4, #1
	uxthne	r4, r4
	b	.L1674
.L1681:
	mov	r0, r4
	ldmfd	sp!, {r4, r5, r6, pc}
.L1683:
	.align	2
.L1682:
	.word	.LANCHOR2-3200
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
	ldr	r7, .L1709
	ldr	r6, [r7, #-2812]
	cmp	r6, #0
	bne	.L1685
	bl	FtlBbtMemInit
	ldr	r8, .L1709+4
	bl	FtlLoadFactoryBbt
	sub	r4, r7, #2880
	sub	r9, r8, #18
	mov	r5, r7
.L1686:
	ldr	r3, .L1709+8
	ldrh	r2, [r3]
	cmp	r6, r2
	bcs	.L1706
	ldrh	r2, [r9, #2]!
	movw	r1, #65535
	ldr	r0, [r5, #-3008]
	ldr	r10, [r5, #-2976]
	cmp	r2, r1
	str	r0, [r5, #-2800]
	str	r10, [r5, #-2796]
	beq	.L1687
	ldrh	fp, [r3, #44]
	mov	r1, #1
	ldr	r0, .L1709+12
	str	r3, [sp, #4]
	mla	fp, fp, r6, r2
	mov	r2, fp, asl #10
	str	r2, [r5, #-2804]
	mov	r2, r1
	bl	FlashReadPages
	ldr	r0, [r8]
	ldr	r1, [r5, #-2800]
	ldr	r3, [sp, #4]
	ldrh	r2, [r3, #44]
	add	r2, r2, #7
	mov	r2, r2, asr #3
	bl	ftl_memcpy
	b	.L1688
.L1687:
	mov	r1, r6
	str	r3, [sp, #4]
	bl	FlashGetBadBlockList
	ldr	r0, [r5, #-2800]
	ldr	r1, [r8]
	bl	FtlBbt2Bitmap
	ldr	r3, [sp, #4]
	ldrh	r3, [r3, #44]
.L1705:
	sub	r3, r3, #1
	uxth	r3, r3
	str	r3, [sp, #4]
.L1689:
	ldr	fp, .L1709+16
	ldr	r3, [sp, #4]
	ldrh	r0, [fp]
	smlabb	r0, r0, r6, r3
	uxth	r0, r0
	bl	FtlBbmIsBadBlock
	cmp	r0, #1
	ldreq	r3, [sp, #4]
	beq	.L1705
.L1707:
	ldrh	r3, [sp, #4]
	mov	r1, #0
	mov	r2, #16
	ldr	r0, [r5, #-2976]
	strh	r3, [r9]	@ movhi
	bl	ftl_memset
	ldr	r3, .L1709+20
	strh	r3, [r10]	@ movhi
	mov	r3, #0
	str	r3, [r10, #4]
	ldrh	r3, [r9]
	ldrh	fp, [fp]
	strh	r3, [r10, #2]	@ movhi
	ldrh	r3, [r9]
	ldr	r1, [r8]
	ldr	r0, [r5, #-2800]
	mla	fp, fp, r6, r3
	mov	r3, fp, asl #10
	str	r3, [r5, #-2804]
	ldr	r3, .L1709+24
	ldrh	r2, [r3]
	mov	r2, r2, asl #2
	bl	ftl_memcpy
	mov	r1, #1
	mov	r2, r1
	ldr	r0, .L1709+12
	bl	FlashEraseBlocks
	mov	r1, #1
	mov	r3, r1
	ldr	r0, .L1709+12
	mov	r2, r1
	bl	FlashProgPages
	ldr	r3, [r5, #-2808]
	cmn	r3, #1
	bne	.L1688
	uxth	r0, fp
	bl	FtlBbmMapBadBlock
	b	.L1689
.L1688:
	uxth	r0, fp
	add	r6, r6, #1
	bl	FtlBbmMapBadBlock
	add	r8, r8, #4
	b	.L1686
.L1706:
	add	r5, r3, #60
	mov	r6, #0
.L1693:
	ldrh	r3, [r5]
	uxth	r0, r6
	add	r6, r6, #1
	cmp	r3, r0
	bls	.L1708
	bl	FtlBbmMapBadBlock
	b	.L1693
.L1708:
	ldrh	r5, [r4, #12]
	movw	r8, #65535
	ldr	r6, .L1709+28
	sub	r5, r5, #1
	uxth	r5, r5
.L1695:
	ldrh	r3, [r4, #12]
	sub	r3, r3, #48
	cmp	r5, r3
	ble	.L1699
	mov	r0, r5
	bl	FtlBbmIsBadBlock
	cmp	r0, #1
	beq	.L1696
	mov	r0, r5
	bl	FlashTestBlk
	cmp	r0, #0
	beq	.L1697
	mov	r0, r5
	bl	FtlBbmMapBadBlock
	b	.L1696
.L1697:
	ldrh	r3, [r6]
	cmp	r3, r8
	streqh	r5, [r6]	@ movhi
.L1698:
	ldrne	r3, .L1709+28
	strneh	r5, [r3, #4]	@ movhi
	bne	.L1699
.L1696:
	sub	r5, r5, #1
	uxth	r5, r5
	b	.L1695
.L1699:
	ldr	r0, [r7, #-3024]
	mov	r5, #0
	ldrh	r3, [r4]
	mov	r2, #2
	str	r5, [r7, #-2872]
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
	ldr	r3, [r7, #-2872]
	ldrh	r2, [r4, #4]
	add	r3, r3, #1
	str	r3, [r7, #-2872]
	ldrh	r3, [r4]
	strh	r5, [r4, #2]	@ movhi
	strh	r2, [r4]	@ movhi
	strh	r3, [r4, #4]	@ movhi
	bl	FtlBbmTblFlush
.L1685:
	mov	r0, #0
	add	sp, sp, #12
	@ sp needed
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L1710:
	.align	2
.L1709:
	.word	.LANCHOR2
	.word	.LANCHOR2-2852
	.word	.LANCHOR2-3246
	.word	.LANCHOR2-2808
	.word	.LANCHOR2-3202
	.word	-3872
	.word	.LANCHOR2-2884
	.word	.LANCHOR2-2880
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
	ldr	r4, .L1713
	mov	r7, r0
	sub	r3, r4, #3232
	sub	r4, r4, #3200
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
.L1714:
	.align	2
.L1713:
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
	ldr	r3, .L1717
	stmfd	sp!, {r4, r5, r6, lr}
	.save {r4, r5, r6, lr}
	sub	r2, r3, #3232
	sub	r3, r3, #3200
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
.L1718:
	.align	2
.L1717:
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
	ldr	r4, .L1721
	sub	r3, r4, #3200
	sub	r4, r4, #3232
	ldrh	r1, [r3, #-2]
	bl	__aeabi_uidivmod
	uxth	r0, r1
	ldrh	r1, [r4, #-12]
	bl	__aeabi_uidiv
	uxth	r0, r0
	ldmfd	sp!, {r4, pc}
.L1722:
	.align	2
.L1721:
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
	bls	.L1724
	rsb	r0, r1, r0
	cmp	r0, #-2147483648
	movhi	r0, #0
	movls	r0, #1
	bx	lr
.L1724:
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
	stmfd	sp!, {r4, r5, r6, r7, r8, r9, lr}
	.save {r4, r5, r6, r7, r8, r9, lr}
	cmp	r1, #1
	ldr	r6, .L1739
	.pad #108
	sub	sp, sp, #108
	mov	r9, r1
	mov	r7, r0, asl #10
	mov	r1, #1
	mov	r0, sp
	ldr	r3, [r6]
	mov	r2, r9
	mov	r8, #0
	str	r8, [sp, #8]
	str	r3, [sp, #100]
	ldreq	r3, .L1739+4
	ldrne	r3, .L1739+8
	ldreqh	r4, [r3, #-14]
	ldrneh	r4, [r3]
	add	r3, sp, #36
	str	r3, [sp, #12]
	sub	r5, r4, #1
	uxth	r5, r5
	sxth	r3, r5
	orr	r3, r3, r7
	str	r3, [sp, #4]
	bl	FlashReadPages
	ldr	r3, [sp, #36]
	cmn	r3, #1
	bne	.L1729
.L1730:
	sxth	r4, r8
	sxth	r3, r5
	cmp	r4, r3
	bgt	.L1729
	add	r4, r4, r3
	mov	r0, sp
	mov	r1, #1
	mov	r2, r9
	add	r4, r4, r4, lsr #31
	mov	r4, r4, asr #1
	sxth	r3, r4
	orr	r3, r3, r7
	str	r3, [sp, #4]
	bl	FlashReadPages
	ldr	r3, [sp, #36]
	cmn	r3, #1
	bne	.L1731
	ldr	r3, [sp, #40]
	cmn	r3, #1
	bne	.L1731
	ldr	r3, [sp]
	cmn	r3, #1
	subne	r5, r4, #1
	uxthne	r5, r5
	bne	.L1730
.L1731:
	add	r3, r4, #1
	uxth	r8, r3
	b	.L1730
.L1729:
	ldr	r2, [sp, #100]
	sxth	r0, r5
	ldr	r3, [r6]
	cmp	r2, r3
	beq	.L1734
	bl	__stack_chk_fail
.L1734:
	add	sp, sp, #108
	@ sp needed
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, pc}
.L1740:
	.align	2
.L1739:
	.word	__stack_chk_guard
	.word	.LANCHOR2-3184
	.word	.LANCHOR2-3200
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
	ldr	r4, .L1774
	ldr	r3, [r4, #-3008]
	mov	r7, r4
	ldr	r6, [r4, #-2976]
	str	r3, [r4, #-2800]
	str	r6, [r4, #-2796]
	bl	FtlBbtMemInit
	sub	r3, r4, #3200
	sub	r8, r3, #2
	ldrh	r5, [r3, #-2]
	sub	r5, r5, #1
	uxth	r5, r5
.L1742:
	ldrh	r3, [r8]
	sub	r3, r3, #48
	cmp	r5, r3
	ble	.L1745
	mov	r1, #1
	ldr	r0, .L1774+4
	mov	r2, r1
	mov	r3, r5, asl #10
	str	r3, [r7, #-2804]
	bl	FlashReadPages
	ldr	r3, [r7, #-2808]
	cmn	r3, #1
	bne	.L1743
	ldr	r3, [r7, #-2804]
	mov	r1, #1
	ldr	r0, .L1774+4
	mov	r2, r1
	add	r3, r3, #1
	str	r3, [r7, #-2804]
	bl	FlashReadPages
.L1743:
	ldr	r3, [r4, #-2808]
	ldr	r2, .L1774
	cmn	r3, #1
	beq	.L1744
	ldrh	r1, [r6]
	movw	r3, #61649
	cmp	r1, r3
	bne	.L1744
	ldr	r1, [r6, #4]
	sub	r3, r2, #2880
	strh	r5, [r3]	@ movhi
	str	r1, [r2, #-2872]
	ldrh	r2, [r6, #8]
	strh	r2, [r3, #4]	@ movhi
	b	.L1745
.L1744:
	sub	r5, r5, #1
	uxth	r5, r5
	b	.L1742
.L1745:
	ldr	r7, .L1774
	movw	r2, #65535
	sub	r5, r7, #2880
	ldrh	r3, [r5]
	cmp	r3, r2
	beq	.L1759
	ldrh	r3, [r5, #4]
	cmp	r3, r2
	beq	.L1749
	mov	r1, #1
	add	r0, r5, #72
	mov	r2, r1
	mov	r3, r3, asl #10
	str	r3, [r7, #-2804]
	bl	FlashReadPages
	ldr	r3, [r7, #-2808]
	cmn	r3, #1
	beq	.L1749
	ldrh	r2, [r6]
	movw	r3, #61649
	cmp	r2, r3
	bne	.L1749
	ldr	r3, [r6, #4]
	ldr	r2, [r7, #-2872]
	cmp	r3, r2
	strhi	r3, [r7, #-2872]
	ldrhih	r2, [r5, #4]
	ldrhih	r3, [r6, #8]
	strhih	r2, [r5]	@ movhi
	strhih	r3, [r5, #4]	@ movhi
.L1749:
	ldrh	r0, [r5]
	mov	r1, #1
	bl	FtlGetLastWrittenPage
	movw	r8, #61649
	uxth	r7, r0
	add	r0, r0, #1
	strh	r0, [r5, #2]	@ movhi
.L1751:
	sxth	r3, r7
	cmp	r3, #0
	blt	.L1756
	ldrh	r2, [r5]
	mov	r1, #1
	ldr	r0, .L1774+4
	orr	r3, r3, r2, asl #10
	str	r3, [r4, #-2804]
	ldr	r3, [r4, #-3008]
	mov	r2, r1
	str	r3, [r4, #-2800]
	bl	FlashReadPages
	ldr	r3, [r4, #-2808]
	cmn	r3, #1
	beq	.L1752
	ldrh	r3, [r6]
	cmp	r3, r8
	bne	.L1752
.L1756:
	ldrh	r2, [r6, #10]
	ldrh	r0, [r6, #12]
	ldr	r3, .L1774
	strh	r2, [r5, #6]	@ movhi
	movw	r2, #65535
	cmp	r0, r2
	bne	.L1753
	b	.L1754
.L1752:
	sub	r7, r7, #1
	uxth	r7, r7
	b	.L1751
.L1753:
	ldr	r2, [r3, #-3272]
	cmp	r0, r2
	beq	.L1754
	sub	r3, r3, #3248
	ldrh	r3, [r3, #-10]
	mov	r3, r3, lsr #2
	cmp	r0, r3
	cmpcc	r2, r3
	bcs	.L1754
	bl	FtlSysBlkNumInit
.L1754:
	ldr	r5, .L1774+8
	mov	r4, #0
	ldr	r7, .L1774
	ldr	r6, .L1774+12
	sub	r8, r5, #28
.L1757:
	ldrh	r3, [r6]
	cmp	r4, r3
	bcs	.L1773
	ldrh	r2, [r8]
	ldr	r1, [r7, #-2800]
	ldr	r0, [r5, #4]!
	mov	r2, r2, asl #2
	mla	r1, r4, r2, r1
	bl	ftl_memcpy
	add	r4, r4, #1
	b	.L1757
.L1773:
	mov	r0, #0
	ldmfd	sp!, {r4, r5, r6, r7, r8, pc}
.L1759:
	mvn	r0, #0
	ldmfd	sp!, {r4, r5, r6, r7, r8, pc}
.L1775:
	.align	2
.L1774:
	.word	.LANCHOR2
	.word	.LANCHOR2-2808
	.word	.LANCHOR2-2856
	.word	.LANCHOR2-3246
	.fnend
	.size	FtlLoadBbt, .-FtlLoadBbt
	.align	2
	.global	FtlFreeSysBlkQueueInit
	.type	FtlFreeSysBlkQueueInit, %function
FtlFreeSysBlkQueueInit:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	ldr	r3, .L1778
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
.L1779:
	.align	2
.L1778:
	.word	.LANCHOR2-2768
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
	ldr	r3, .L1781
	ldrh	r0, [r3, #6]
	clz	r0, r0
	mov	r0, r0, lsr #5
	bx	lr
.L1782:
	.align	2
.L1781:
	.word	.LANCHOR2-2772
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
	ldr	r3, .L1784
	ldrh	r0, [r3, #6]
	sub	r0, r0, #1024
	clz	r0, r0
	mov	r0, r0, lsr #5
	bx	lr
.L1785:
	.align	2
.L1784:
	.word	.LANCHOR2-2772
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
	ldr	r4, .L1796
	ldr	r5, .L1796+4
	ldrh	r3, [r4, #6]
	cmp	r3, #1024
	ldmeqfd	sp!, {r3, r4, r5, r6, r7, pc}
	cmp	r1, #0
	beq	.L1788
	ldr	r3, [r5, #-2812]
	cmp	r3, #0
	bne	.L1788
	bl	P2V_block_in_plane
	mov	r1, #1
	mov	r3, r7, asl #10
	mov	r2, r1
	mov	r6, r0
	ldr	r0, [r5, #-3024]
	str	r3, [r0, #4]
	bl	FlashEraseBlocks
	ldr	r1, [r5, #-2948]
	mov	r3, r6, asl #1
	ldrh	r2, [r1, r3]
	add	r2, r2, #1
	strh	r2, [r1, r3]	@ movhi
	ldr	r3, [r5, #-3108]
	add	r3, r3, #1
	str	r3, [r5, #-3108]
.L1788:
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
.L1797:
	.align	2
.L1796:
	.word	.LANCHOR2-2772
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
	ldr	r6, .L1848
	ldr	r5, [r6, #-2812]
	ldrb	r3, [r6, #-3340]	@ zero_extendqisi2
	cmp	r5, #0
	str	r3, [sp, #16]
	movne	r0, #0
	bne	.L1799
	ldr	r2, .L1848+4
	mov	r9, r6
	mov	fp, r1
	mov	r10, r5
	mov	r4, r5
	mov	r7, r6
	ldrb	r3, [r2, #3904]	@ zero_extendqisi2
	mov	r8, #36
	str	r0, [sp, #4]
	str	r0, [r9, #-3076]
	str	r3, [sp, #8]
	str	r2, [sp, #12]
.L1800:
	ldr	r3, .L1848+8
	uxth	r2, r10
	ldr	ip, .L1848
	ldrh	r1, [r3]
	cmp	r1, r2
	bls	.L1843
	mul	r1, r8, r2
	ldr	r0, [r7, #-3024]
	add	r2, r7, r2
	mov	r3, #0
	str	r3, [r0, r1]
	ldr	r1, [sp, #4]
	ldrb	r0, [r2, #-3240]	@ zero_extendqisi2
	bl	V2P_block
	cmp	fp, #0
	mov	r6, r0
	beq	.L1801
	bl	IsBlkInVendorPart
	cmp	r0, #0
	bne	.L1802
.L1801:
	mov	r0, r6
	bl	FtlBbmIsBadBlock
	cmp	r0, #0
	addne	r4, r4, #1
	uxthne	r4, r4
	bne	.L1802
	ldr	r3, .L1848+12
	mov	r6, r6, asl #10
	ldr	r1, [r9, #-3024]
	ldrh	r2, [r3]
	mla	r1, r8, r5, r1
	mul	r2, r2, r5
	add	r5, r5, #1
	uxth	r5, r5
	cmp	r2, #0
	str	r0, [r1, #8]
	add	r0, r2, #3
	str	r6, [r1, #4]
	movlt	r2, r0
	ldr	r0, [r9, #-2972]
	bic	r2, r2, #3
	add	r2, r0, r2
	str	r2, [r1, #12]
.L1802:
	add	r10, r10, #1
	b	.L1800
.L1843:
	cmp	r5, #0
	beq	.L1823
	ldr	r3, [sp, #8]
	mov	r2, r5
	ldr	r0, [ip, #-3024]
	mov	r7, #0
	adds	r6, r3, #0
	strb	r7, [ip, #-3340]
	str	ip, [sp, #8]
	mov	r8, #36
	movne	r6, #1
	mov	r1, r6
	bl	FlashEraseBlocks
	ldrb	r3, [sp, #16]	@ zero_extendqisi2
	ldr	ip, [sp, #8]
	strb	r3, [ip, #-3340]
.L1806:
	uxth	r3, r7
	cmp	r3, r5
	bcs	.L1844
	mul	r3, r8, r7
	ldr	r2, [r9, #-3024]
	add	r0, r2, r3
	ldr	r3, [r2, r3]
	cmn	r3, #1
	bne	.L1807
	ldr	r0, [r0, #4]
	add	r4, r4, #1
	ubfx	r0, r0, #10, #16
	uxth	r4, r4
	bl	FtlBbmMapBadBlock
.L1807:
	add	r7, r7, #1
	b	.L1806
.L1844:
	cmp	fp, #0
	bne	.L1809
	mov	r3, #6
	uxth	r6, r6
	str	r3, [sp, #12]
	mov	r3, #1
	str	r3, [sp, #8]
	b	.L1810
.L1809:
	ldr	r3, .L1848+16
	ldrh	r3, [r3, #-14]
	str	r3, [sp, #8]
	ldr	r3, [sp, #12]
	ldrb	r3, [r3, #3904]	@ zero_extendqisi2
	cmp	r3, #0
	ldreq	r3, [sp, #8]
	moveq	r6, #1
	movne	r6, #1
	strne	r6, [sp, #12]
	moveq	r3, r3, lsr #2
	streq	r3, [sp, #12]
.L1810:
	ldr	r10, .L1848
	mov	r7, #0
.L1819:
	mov	r8, #0
	mov	r5, r8
.L1811:
	ldr	r3, .L1848+8
	ldr	r9, .L1848
	ldrh	r2, [r3]
	uxth	r3, r8
	cmp	r2, r3
	bls	.L1845
	mov	r2, #36
	ldr	r1, [r10, #-3024]
	mul	r2, r2, r3
	add	r3, r10, r3
	mov	r0, #0
	str	r0, [r1, r2]
	ldr	r1, [sp, #4]
	ldrb	r0, [r3, #-3240]	@ zero_extendqisi2
	bl	V2P_block
	cmp	fp, #0
	mov	r9, r0
	beq	.L1812
	bl	IsBlkInVendorPart
	cmp	r0, #0
	bne	.L1813
.L1812:
	mov	r0, r9
	bl	FtlBbmIsBadBlock
	cmp	r0, #0
	bne	.L1813
	ldr	r2, [r10, #-3024]
	mov	r3, #36
	add	r9, r7, r9, asl #10
	mla	r2, r3, r5, r2
	ldr	r3, [r10, #-2988]
	str	r3, [r2, #8]
	ldr	r3, .L1848+12
	str	r9, [r2, #4]
	ldrh	r3, [r3]
	mul	r3, r3, r5
	add	r5, r5, #1
	uxth	r5, r5
	add	r1, r3, #3
	cmp	r3, #0
	movlt	r3, r1
	ldr	r1, [r10, #-2984]
	bic	r3, r3, #3
	add	r3, r1, r3
	str	r3, [r2, #12]
.L1813:
	add	r8, r8, #1
	b	.L1811
.L1845:
	cmp	r5, #0
	beq	.L1823
	ldr	r2, .L1848
	mov	r3, #0
	mov	r1, r5
	mov	r8, #0
	strb	r3, [r2, #-3340]
	mov	r3, #1
	ldr	r0, [r2, #-3024]
	mov	r2, r6
	bl	FlashProgPages
	ldr	r3, .L1848
	mov	r1, #36
	ldrb	r2, [sp, #16]	@ zero_extendqisi2
	strb	r2, [r3, #-3340]
.L1816:
	uxth	r3, r8
	cmp	r3, r5
	bcs	.L1846
	mul	r3, r1, r8
	ldr	r2, .L1848
	ldr	r2, [r2, #-3024]
	add	r0, r2, r3
	ldr	r3, [r2, r3]
	cmp	r3, #0
	beq	.L1817
	ldr	r0, [r0, #4]
	add	r4, r4, #1
	str	r1, [sp, #20]
	ubfx	r0, r0, #10, #16
	uxth	r4, r4
	bl	FtlBbmMapBadBlock
	ldr	r1, [sp, #20]
.L1817:
	add	r8, r8, #1
	b	.L1816
.L1846:
	ldr	r3, [sp, #12]
	add	r7, r7, r3
	ldr	r3, [sp, #8]
	uxth	r7, r7
	cmp	r7, r3
	bcc	.L1819
	mov	r7, #0
	mov	r8, #36
.L1820:
	uxth	r3, r7
	cmp	r3, r5
	bcs	.L1847
	cmp	fp, #0
	beq	.L1821
	mul	r3, r8, r7
	ldr	r2, [r9, #-3024]
	add	r1, r2, r3
	ldr	r3, [r2, r3]
	cmp	r3, #0
	bne	.L1821
	ldr	r0, [r1, #4]
	mov	r1, #1
	ubfx	r0, r0, #10, #16
	bl	FtlFreeSysBlkQueueIn
.L1821:
	add	r7, r7, #1
	b	.L1820
.L1847:
	adds	r3, fp, #0
	ldr	r2, [sp, #4]
	movne	r3, #1
	cmp	r2, #63
	orrls	r3, r3, #1
	cmp	r3, #0
	beq	.L1823
	ldr	r0, [r9, #-3024]
	mov	r1, r6
	mov	r2, r5
	bl	FlashEraseBlocks
.L1823:
	mov	r0, r4
.L1799:
	add	sp, sp, #28
	@ sp needed
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L1849:
	.align	2
.L1848:
	.word	.LANCHOR2
	.word	.LANCHOR0
	.word	.LANCHOR2-3268
	.word	.LANCHOR2-3188
	.word	.LANCHOR2-3184
	.fnend
	.size	FtlLowFormatEraseBlock, .-FtlLowFormatEraseBlock
	.align	2
	.global	FtlFreeSysBLkSort
	.type	FtlFreeSysBLkSort, %function
FtlFreeSysBLkSort:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	ldr	r3, .L1863
	ldrh	ip, [r3, #28]
	ldr	r3, .L1863+4
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
.L1852:
	uxth	lr, r0
	add	r0, r0, #1
	cmp	lr, ip
	bge	.L1862
	add	lr, r3, r1, asl #1
	add	r1, r1, #1
	ubfx	r1, r1, #0, #10
	ldrh	r4, [lr, #8]
	add	lr, r3, r2, asl #1
	strh	r4, [lr, #8]	@ movhi
	mov	r4, #1
	add	r2, r2, r4
	ubfx	r2, r2, #0, #10
	b	.L1852
.L1862:
	cmp	r4, #0
	strneh	r1, [r3, #2]	@ movhi
	strneh	r2, [r3, #4]	@ movhi
	ldmfd	sp!, {r4, pc}
.L1864:
	.align	2
.L1863:
	.word	.LANCHOR2-716
	.word	.LANCHOR2-2772
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
	ldr	r7, .L1876
	ldr	r5, .L1876+4
	mov	r6, r7
.L1866:
	ldrh	r1, [r5, #6]
	cmp	r1, #0
	beq	.L1867
	ldrh	r3, [r5, #2]
	sub	r1, r1, #1
	ldr	r9, [r7, #-2812]
	strh	r1, [r5, #6]	@ movhi
	add	r2, r5, r3, asl #1
	cmp	r9, #0
	add	r3, r3, #1
	ubfx	r3, r3, #0, #10
	ldrh	r4, [r2, #8]
	strh	r3, [r5, #2]	@ movhi
	bne	.L1868
	mov	r0, r4
	bl	P2V_block_in_plane
	mov	r3, r4, asl #10
	mov	r8, r0
	ldr	r0, [r7, #-3024]
	str	r3, [r0, #4]
	ldr	r3, .L1876+8
	ldrb	r3, [r3, #3904]	@ zero_extendqisi2
	cmp	r3, #0
	beq	.L1869
	mov	r1, r9
	mov	r2, #1
	bl	FlashEraseBlocks
.L1869:
	mov	r1, #1
	ldr	r0, [r6, #-3024]
	mov	r2, r1
	bl	FlashEraseBlocks
	ldr	r1, [r6, #-2948]
	mov	r3, r8, asl #1
	ldrh	r2, [r1, r3]
	add	r2, r2, #1
	strh	r2, [r1, r3]	@ movhi
	ldr	r3, [r6, #-3108]
	add	r3, r3, #1
	str	r3, [r6, #-3108]
	b	.L1868
.L1867:
	ldr	r0, .L1876+12
	bl	printk
.L1870:
	b	.L1870
.L1868:
	sub	r3, r4, #1
	movw	r2, #65533
	uxth	r3, r3
	cmp	r3, r2
	bls	.L1871
	ldr	r3, .L1876+4
	mov	r1, r4
	ldr	r0, .L1876+16
	ldrh	r2, [r3, #6]
	bl	printk
	b	.L1866
.L1871:
	mov	r0, r4
	ldmfd	sp!, {r3, r4, r5, r6, r7, r8, r9, pc}
.L1877:
	.align	2
.L1876:
	.word	.LANCHOR2
	.word	.LANCHOR2-2772
	.word	.LANCHOR0
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
	ldr	r3, .L1884
	str	lr, [sp, #-4]!
	.save {lr}
	movw	lr, #65535
	ldr	ip, [r3, #-2888]
	ldr	r2, [r0]
	ldr	r3, .L1884+4
	rsb	r0, ip, r2
	mov	r0, r0, asr #1
	mul	r3, r3, r0
	mov	r0, #6
	uxth	r3, r3
.L1880:
	cmp	r1, r3
	beq	.L1881
	ldrh	r3, [r2]
	cmp	r3, lr
	beq	.L1882
	mla	r2, r0, r3, ip
	b	.L1880
.L1881:
	mov	r0, #1
	ldr	pc, [sp], #4
.L1882:
	mov	r0, #0
	ldr	pc, [sp], #4
.L1885:
	.align	2
.L1884:
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
	ldr	r2, .L1903
	sub	r5, r2, #3248
	ldrh	r3, [r5, #-12]
	cmp	r3, r0
	bls	.L1888
	mov	lr, #6
	ldr	r6, [r2, #-2888]
	mul	lr, lr, r0
	mvn	ip, #0
	add	r1, r6, lr
	strh	ip, [r1, #2]	@ movhi
	strh	ip, [r6, lr]	@ movhi
	ldr	r3, [r2, #-668]
	cmp	r3, #0
	beq	.L1902
	ldr	r8, [r2, #-2940]
	mov	r4, r0, asl #1
	mov	r10, r2
	ldrh	r2, [r1, #4]
	ldrh	r5, [r5, #-12]
	ldrh	r7, [r8, r4]
	cmp	r2, #0
	str	r5, [sp]
	mulne	ip, r2, r7
	ldr	r7, [r10, #-2888]
	ldr	r2, .L1903+4
	rsb	r9, r7, r3
	mov	r9, r9, asr #1
	mul	r2, r2, r9
	ldr	r9, [r10, #-2948]
	add	r4, r9, r4
	str	r4, [sp, #4]
	mov	r4, #0
	uxth	r2, r2
.L1897:
	add	r4, r4, #1
	ldr	r5, [sp]
	uxth	r4, r4
	cmp	r4, r5
	movls	r5, #0
	movhi	r5, #1
	cmp	r0, r2
	orreq	r5, r5, #1
	cmp	r5, #0
	bne	.L1888
	mov	r10, r2, asl #1
	ldrh	r5, [r3, #4]
	ldrh	fp, [r8, r10]
	cmp	r5, #0
	mvneq	r5, #0
	mulne	r5, r5, fp
	cmp	r5, ip
	bne	.L1893
	ldr	r5, [sp, #4]
	ldrh	r10, [r9, r10]
	ldrh	r5, [r5]
	cmp	r10, r5
	bcc	.L1895
	b	.L1894
.L1893:
	bhi	.L1894
.L1895:
	ldrh	r5, [r3]
	movw	r10, #65535
	cmp	r5, r10
	streqh	r2, [r1, #2]	@ movhi
	streqh	r0, [r3]	@ movhi
	ldreq	r3, .L1903
	streq	r1, [r3, #-664]
	beq	.L1888
.L1896:
	mov	r3, #6
	mov	r2, r5
	mla	r3, r3, r5, r7
	b	.L1897
.L1894:
	strh	r2, [r6, lr]	@ movhi
	ldrh	r2, [r3, #2]
	strh	r2, [r1, #2]	@ movhi
	ldr	r2, .L1903
	ldr	ip, [r2, #-668]
	cmp	r3, ip
	bne	.L1898
	strh	r0, [r3, #2]	@ movhi
.L1902:
	str	r1, [r2, #-668]
	b	.L1888
.L1898:
	ldrh	ip, [r3, #2]
	ldr	r1, [r2, #-2888]
	mov	r2, #6
	mul	r2, r2, ip
	strh	r0, [r1, r2]	@ movhi
	strh	r0, [r3, #2]	@ movhi
.L1888:
	mov	r0, #0
	add	sp, sp, #12
	@ sp needed
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L1904:
	.align	2
.L1903:
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
	ldr	r2, .L1907
	ldrh	r3, [r2]
	add	r3, r3, #1
	strh	r3, [r2]	@ movhi
	ldmfd	sp!, {r3, pc}
.L1908:
	.align	2
.L1907:
	.word	.LANCHOR2-660
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
	beq	.L1910
	ldr	r2, .L1918
	mov	lr, #6
	mul	r6, lr, r0
	mvn	r3, #0
	ldr	r7, [r2, #-2888]
	mov	r5, r2
	add	ip, r7, r6
	strh	r3, [ip, #2]	@ movhi
	strh	r3, [r7, r6]	@ movhi
	ldr	r3, [r2, #-656]
	cmp	r3, #0
	beq	.L1917
	ldr	r9, [r2, #-2948]
	mov	r2, r0, asl #1
	ldr	r8, [r5, #-2888]
	rsb	r1, r8, r3
	ldrh	r10, [r9, r2]
	ldr	r2, .L1918+4
	mov	r1, r1, asr #1
	mul	r1, r2, r1
	uxth	r2, r1
.L1914:
	mov	r1, r2, asl #1
	ldrh	r1, [r9, r1]
	cmp	r1, r10
	bcs	.L1912
	ldrh	r1, [r3]
	cmp	r1, r4
	streqh	r2, [ip, #2]	@ movhi
	streqh	r0, [r3]	@ movhi
	beq	.L1910
.L1913:
	mla	r3, lr, r1, r8
	mov	r2, r1
	b	.L1914
.L1912:
	ldrh	r1, [r3, #2]
	strh	r1, [ip, #2]	@ movhi
	strh	r2, [r7, r6]	@ movhi
	ldr	r1, [r5, #-656]
	ldr	r2, .L1918
	cmp	r3, r1
	bne	.L1915
	strh	r0, [r3, #2]	@ movhi
.L1917:
	str	ip, [r2, #-656]
	b	.L1910
.L1915:
	ldrh	ip, [r3, #2]
	ldr	r1, [r2, #-2888]
	mov	r2, #6
	mul	r2, r2, ip
	strh	r0, [r1, r2]	@ movhi
	strh	r0, [r3, #2]	@ movhi
.L1910:
	mov	r0, #0
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, pc}
.L1919:
	.align	2
.L1918:
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
	ldr	r2, .L1922
	ldrh	r3, [r2]
	add	r3, r3, #1
	strh	r3, [r2]	@ movhi
	ldmfd	sp!, {r3, pc}
.L1923:
	.align	2
.L1922:
	.word	.LANCHOR2-652
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
	ldr	r4, .L1930
	movw	r5, #65535
	mul	r1, ip, r1
	ldr	r3, [r0]
	ldr	r2, [r4, #-2888]
	add	lr, r2, r1
	cmp	lr, r3
	ldrh	r3, [r2, r1]
	bne	.L1925
	cmp	r3, r5
	mlane	r3, ip, r3, r2
	moveq	r3, #0
	streq	r3, [r0]
	strne	r3, [r0]
	mvnne	r0, #0
	strneh	r0, [r3, #2]	@ movhi
	b	.L1927
.L1925:
	cmp	r3, r5
	ldrh	r0, [lr, #2]
	bne	.L1928
	cmp	r0, r3
	mulne	r0, ip, r0
	mvnne	r3, #0
	strneh	r3, [r2, r0]	@ movhi
	b	.L1927
.L1928:
	mla	r3, ip, r3, r2
	strh	r0, [r3, #2]	@ movhi
	ldrh	r5, [lr, #2]
	ldrh	r0, [r2, r1]
	ldr	r3, [r4, #-2888]
	mul	ip, ip, r5
	strh	r0, [r3, ip]	@ movhi
.L1927:
	mvn	r3, #0
	mov	r0, #0
	strh	r3, [r2, r1]	@ movhi
	strh	r3, [lr, #2]	@ movhi
	ldmfd	sp!, {r4, r5, pc}
.L1931:
	.align	2
.L1930:
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
	beq	.L1938
	ldr	r2, .L1941
	movw	ip, #65535
	stmfd	sp!, {r4, lr}
	.save {r4, lr}
	mov	lr, #6
	ldr	r4, [r2, #-2888]
.L1934:
	cmp	r1, #0
	bne	.L1935
.L1937:
	rsb	r4, r4, r3
	ldr	r3, .L1941+4
	mov	r4, r4, asr #1
	mul	r4, r3, r4
	uxth	r4, r4
	mov	r1, r4
	bl	List_remove_node
	mov	r0, r4
	ldmfd	sp!, {r4, pc}
.L1935:
	ldrh	r2, [r3]
	cmp	r2, ip
	beq	.L1937
	sub	r1, r1, #1
	mla	r3, lr, r2, r4
	uxth	r1, r1
	b	.L1934
.L1938:
	movw	r0, #65535
	bx	lr
.L1942:
	.align	2
.L1941:
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
	ldr	r2, .L1950
	ldr	r3, [r2, #-668]
	cmp	r3, #0
	ldrne	r1, [r2, #-2888]
	movne	ip, #6
	movwne	r2, #65535
	beq	.L1948
.L1945:
	cmp	r0, #0
	beq	.L1946
	ldrh	r3, [r3]
	cmp	r3, r2
	subne	r0, r0, #1
	mlane	r3, ip, r3, r1
	uxthne	r0, r0
	bne	.L1945
.L1948:
	movw	r0, #65535
	bx	lr
.L1946:
	rsb	r3, r1, r3
	ldr	r0, .L1950+4
	mov	r3, r3, asr #1
	mul	r0, r0, r3
	uxth	r0, r0
	bx	lr
.L1951:
	.align	2
.L1950:
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
	ldr	r3, .L1961
	sub	r2, r3, #648
	ldrh	r2, [r2]
	cmp	r2, r0
	beq	.L1960
	sub	r2, r3, #600
	ldrh	r2, [r2]
	cmp	r2, r0
	beq	.L1960
	sub	r2, r3, #552
	ldrh	r2, [r2]
	cmp	r2, r0
	beq	.L1960
	stmfd	sp!, {r4, lr}
	.save {r4, lr}
	mov	lr, #6
	mul	lr, lr, r0
	ldr	r1, [r3, #-2888]
	ldr	r2, [r3, #-668]
	add	ip, r1, lr
	cmp	ip, r2
	beq	.L1953
	ldr	r4, [r3, #-2940]
	mov	r3, r0, asl #1
	ldrh	r2, [r4, r3]
	ldrh	r3, [ip, #4]
	ldrh	ip, [ip, #2]
	cmp	r3, #0
	mulne	r2, r3, r2
	mvneq	r2, #0
	movw	r3, #65535
	cmp	ip, r3
	bne	.L1955
	ldrh	r3, [r1, lr]
	cmp	r3, ip
	beq	.L1953
.L1955:
	mov	r3, #6
	mul	ip, r3, ip
	ldr	r3, .L1961+4
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
	bcs	.L1953
	mov	r4, r0
	ldr	r0, .L1961+8
	mov	r1, r4
	bl	List_remove_node
	ldr	r2, .L1961+12
	mov	r0, r4
	ldrh	r3, [r2]
	sub	r3, r3, #1
	strh	r3, [r2]	@ movhi
	bl	INSERT_DATA_LIST
.L1953:
	mov	r0, #0
	ldmfd	sp!, {r4, pc}
.L1960:
	mov	r0, #0
	bx	lr
.L1962:
	.align	2
.L1961:
	.word	.LANCHOR2
	.word	-1431655765
	.word	.LANCHOR2-668
	.word	.LANCHOR2-660
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
.L1964:
	ldrh	r1, [r4, #6]
	uxth	r3, r2
	cmp	r1, r3
	bls	.L1984
	ldr	r0, [r6, r3, asl #2]
	mov	r1, #0
	ubfx	r0, r0, #10, #16
.L1965:
	ldrh	ip, [r4, #10]
	uxth	r3, r1
	cmp	ip, r3
	bls	.L1985
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
	b	.L1965
.L1985:
	add	r2, r2, #1
	b	.L1964
.L1984:
	ldr	r3, .L1987
	mov	r8, #0
	mov	r1, r8
	mov	fp, r8
	ldrh	r2, [r3, #-14]
	ldrh	r3, [r4]
	mov	r3, r3, asl #1
	strh	r2, [r5, r3]	@ movhi
	ldrh	r9, [r5]
.L1969:
	ldrh	r3, [r4, #10]
	uxth	r6, r8
	cmp	r3, r6
	bls	.L1986
	mov	r2, r6, asl #1
	ldrh	r3, [r5, r2]
	cmp	r9, r3
	bls	.L1970
	ldrh	r0, [r7, r2]
	add	r10, r7, r2
	cmp	r0, #0
	bne	.L1971
	b	.L1972
.L1970:
	cmp	r3, #0
	bne	.L1972
	ldrh	r0, [r7, r2]
	add	r10, r7, r2
	cmp	r0, #0
	movne	r6, r1
	beq	.L1972
	b	.L1974
.L1971:
	cmp	r3, #0
	movne	r1, r6
	movne	r9, r3
	bne	.L1972
	mov	r9, r3
.L1974:
	mov	r1, #1
	bl	FtlFreeSysBlkQueueIn
	strh	fp, [r10]	@ movhi
	ldrh	r3, [r4, #8]
	mov	r1, r6
	sub	r3, r3, #1
	strh	r3, [r4, #8]	@ movhi
.L1972:
	add	r8, r8, #1
	b	.L1969
.L1986:
	mov	r0, r1
	ldmfd	sp!, {r3, r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L1988:
	.align	2
.L1987:
	.word	.LANCHOR2-3184
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
.L1990:
	uxth	r5, r3
	cmp	r5, r1
	bcs	.L1993
	mov	r7, r2
	add	r3, r3, #1
	ldrh	r6, [r7]
	add	r2, r2, #2
	cmp	r6, #0
	bne	.L1990
	mov	r4, r0
	bl	FtlFreeSysBlkQueueOut
	movw	r2, #65533
	sub	r3, r0, #1
	mov	r1, r0
	strh	r0, [r7]	@ movhi
	uxth	r3, r3
	cmp	r3, r2
	bls	.L1991
	ldr	r3, .L1997
	ldr	r0, .L1997+4
	ldrh	r2, [r3, #6]
	bl	printk
.L1992:
	b	.L1992
.L1991:
	ldr	r3, [r4, #28]
	strh	r6, [r4, #2]	@ movhi
	add	r3, r3, #1
	str	r3, [r4, #28]
	ldrh	r3, [r4, #8]
	strh	r5, [r4]	@ movhi
	add	r3, r3, #1
	strh	r3, [r4, #8]	@ movhi
.L1993:
	mov	r0, #0
	ldmfd	sp!, {r3, r4, r5, r6, r7, pc}
.L1998:
	.align	2
.L1997:
	.word	.LANCHOR2-2772
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
	ldr	fp, .L2018
	mov	r8, r1
	ldr	r9, .L2018+4
	mov	r5, #0
	str	r2, [sp]
	mov	r10, fp
.L2000:
	ldr	r3, [fp, #-3124]
	ldr	r6, .L2018
	add	r3, r3, #1
	str	r3, [fp, #-3124]
	ldrh	r3, [r9]
	ldrh	r2, [r4, #2]
	sub	r3, r3, #1
	cmp	r2, r3
	bge	.L2001
	ldrh	r2, [r4]
	movw	r3, #65535
	cmp	r2, r3
	bne	.L2002
.L2001:
	mov	r0, r4
	bl	Ftl_write_map_blk_to_last_page
.L2002:
	ldr	r1, [r10, #-2812]
	cmp	r1, #0
	bne	.L2003
	ldrh	r3, [r4]
	ldr	r2, [r4, #12]
	ldr	r0, [fp, #-2976]
	mov	r3, r3, asl #1
	ldrh	r7, [r2, r3]
	mov	r2, #16
	ldrh	r3, [r4, #2]
	str	r0, [fp, #-2796]
	orr	r3, r3, r7, asl #10
	str	r3, [fp, #-2804]
	ldr	r3, [sp]
	str	r3, [fp, #-2800]
	bl	ftl_memset
	ldr	r2, [r4, #28]
	ldr	r3, [fp, #-2796]
	str	r2, [r3, #4]
	strh	r8, [r3, #8]	@ movhi
	ldrh	r2, [r4, #4]
	strh	r7, [r3, #2]	@ movhi
	strh	r2, [r3]	@ movhi
	ldr	r2, .L2018+8
	ldrb	r2, [r2]	@ zero_extendqisi2
	cmp	r2, #0
	beq	.L2004
	ldr	r2, .L2018+12
	ldr	r0, [fp, #-2800]
	str	r3, [sp, #4]
	ldrh	r1, [r2]
	bl	js_hash
	ldr	r3, [sp, #4]
	str	r0, [r3, #12]
.L2004:
	mov	r1, #1
	ldr	r0, .L2018+16
	mov	r2, r1
	mov	r3, r1
	bl	FlashProgPages
	ldrh	r3, [r4, #2]
	add	r3, r3, #1
	uxth	r3, r3
	strh	r3, [r4, #2]	@ movhi
	ldr	r2, [r10, #-2808]
	cmn	r2, #1
	bne	.L2005
	ldr	r0, .L2018+20
	add	r5, r5, #1
	ldr	r1, [fp, #-2804]
	bl	printk
	ldrh	r3, [r4, #2]
	uxth	r5, r5
	cmp	r3, #2
	ldrlsh	r3, [r9]
	subls	r3, r3, #1
	strlsh	r3, [r4, #2]	@ movhi
	cmp	r5, #3
	bls	.L2000
	ldr	r0, .L2018+24
	mov	r2, r5
	ldr	r1, [r6, #-2804]
	bl	printk
	mov	r3, #1
	str	r3, [r6, #-2812]
	b	.L2003
.L2005:
	cmp	r2, #0
	strneh	r7, [r4, #40]	@ movhi
	cmp	r2, #256
	cmpne	r3, #1
	beq	.L2009
	ldr	r3, [r4, #36]
	cmp	r3, #0
	beq	.L2010
.L2009:
	mov	r3, #0
	str	r3, [r4, #36]
	b	.L2000
.L2010:
	ldr	r2, [r6, #-2804]
	ldr	r3, [r4, #24]
	str	r2, [r3, r8, asl #2]
.L2003:
	mov	r0, #0
	add	sp, sp, #12
	@ sp needed
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L2019:
	.align	2
.L2018:
	.word	.LANCHOR2
	.word	.LANCHOR2-3198
	.word	.LANCHOR0
	.word	.LANCHOR2-3190
	.word	.LANCHOR2-2808
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
	ldr	r8, .L2034
	cmp	r2, r3
	blt	.L2021
	uxth	r0, r0
	mov	r0, r0, asl #1
	ldrh	r10, [r5, r0]
	cmp	r10, #0
	beq	.L2021
	ldr	r3, [r4, #32]
	cmp	r3, #0
	bne	.L2021
	mov	r2, #1
	str	r2, [r4, #32]
	strh	r3, [r5, r0]	@ movhi
	ldrh	r3, [r4, #8]
	ldrh	r2, [r4, #2]
	sub	r3, r3, #1
	strh	r3, [r4, #8]	@ movhi
	ldrh	r3, [r8, #-14]
	cmp	r2, r3
	bcc	.L2022
	mov	r0, r4
	bl	ftl_map_blk_alloc_new_blk
.L2022:
	ldr	r5, .L2034+4
	mov	fp, #0
.L2023:
	ldrh	r3, [r4, #6]
	uxth	r6, fp
	cmp	r3, r6
	bls	.L2033
	ldr	r3, [r7, r6, asl #2]
	add	ip, r7, r6, asl #2
	cmp	r10, r3, lsr #10
	bne	.L2024
	ldr	r3, [r5, #-3004]
	mov	r1, #1
	ldr	r9, [r5, #-2976]
	mov	r2, r1
	ldr	r0, .L2034+8
	str	r3, [r5, #-2800]
	str	r9, [r5, #-2796]
	ldr	r3, [r7, r6, asl #2]
	str	ip, [sp, #4]
	str	r3, [r5, #-2804]
	bl	FlashReadPages
	ldr	r3, [r5, #-2808]
	cmn	r3, #1
	ldr	r3, .L2034+4
	ldr	ip, [sp, #4]
	bne	.L2025
.L2027:
	mov	r2, #0
	ldr	r0, .L2034+12
	str	r2, [ip]
	ldr	r1, [r3, #-2804]
	ldrh	r2, [r9, #8]
	str	r3, [sp, #4]
	bl	printk
	mov	r2, #1
	ldr	r3, [sp, #4]
	str	r2, [r3, #-2812]
	b	.L2026
.L2025:
	ldrh	r1, [r9, #8]
	cmp	r1, r6
	bne	.L2027
	ldrh	r0, [r9]
	ldrh	r2, [r4, #4]
	cmp	r0, r2
	bne	.L2027
	mov	r0, r4
	ldr	r2, [r5, #-2800]
	bl	FtlMapWritePage
.L2024:
	add	fp, fp, #1
	b	.L2023
.L2033:
	mov	r0, r10
	mov	r1, #1
	bl	FtlFreeSysBlkQueueIn
	mov	r3, #0
	str	r3, [r4, #32]
.L2021:
	ldrh	r2, [r4, #2]
	ldrh	r3, [r8, #-14]
	cmp	r2, r3
	bcc	.L2026
	mov	r0, r4
	bl	ftl_map_blk_alloc_new_blk
.L2026:
	mov	r0, #0
	add	sp, sp, #12
	@ sp needed
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L2035:
	.align	2
.L2034:
	.word	.LANCHOR2-3184
	.word	.LANCHOR2
	.word	.LANCHOR2-2808
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
	ldr	r5, .L2048
	ldr	r7, [r0, #12]
	ldr	r8, [r0, #24]
	ldr	r6, [r5, #-2812]
	cmp	r6, #0
	bne	.L2037
	ldrh	r3, [r0]
	movw	r2, #65535
	mov	r4, r0
	cmp	r3, r2
	bne	.L2038
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
	b	.L2037
.L2038:
	mov	r3, r3, asl #1
	sub	r10, r5, #3184
	mov	r1, #255
	ldrh	r9, [r7, r3]
	ldrh	r3, [r0, #2]
	ldr	r7, [r5, #-2976]
	orr	r3, r3, r9, asl #10
	str	r3, [r5, #-2804]
	ldr	r3, [r5, #-3008]
	str	r7, [r5, #-2796]
	str	r3, [r5, #-2800]
	ldr	r3, [r0, #28]
	str	r3, [r7, #4]
	ldr	r3, .L2048+4
	strh	r3, [r7, #8]	@ movhi
	ldrh	r3, [r0, #4]
	strh	r9, [r7, #2]	@ movhi
	strh	r3, [r7]	@ movhi
	ldrh	r2, [r10, #-14]
	ldr	r0, [r5, #-3008]
	mov	r2, r2, asl #3
	bl	ftl_memset
	mov	r2, r6
.L2039:
	ldrh	r1, [r4, #6]
	uxth	r3, r2
	cmp	r1, r3
	bls	.L2047
	ldr	r1, [r8, r3, asl #2]
	cmp	r9, r1, lsr #10
	bne	.L2040
	add	r6, r6, #1
	ldr	r1, [r5, #-3008]
	uxth	r6, r6
	str	r3, [r1, r6, asl #3]
	ldr	r1, [r8, r3, asl #2]
	ldr	r3, [r5, #-3008]
	add	r3, r3, r6, asl #3
	str	r1, [r3, #4]
.L2040:
	add	r2, r2, #1
	b	.L2039
.L2047:
	ldr	r3, .L2048+8
	ldrb	r3, [r3]	@ zero_extendqisi2
	cmp	r3, #0
	beq	.L2042
	ldr	r0, [r5, #-2800]
	ldrh	r1, [r10, #-6]
	bl	js_hash
	str	r0, [r7, #12]
.L2042:
	mov	r1, #1
	mov	r3, #0
	ldr	r0, .L2048+12
	mov	r2, r1
	bl	FlashProgPages
	ldrh	r3, [r4, #2]
	mov	r0, r4
	add	r3, r3, #1
	strh	r3, [r4, #2]	@ movhi
	bl	ftl_map_blk_gc
.L2037:
	mov	r0, #0
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, pc}
.L2049:
	.align	2
.L2048:
	.word	.LANCHOR2
	.word	-1291
	.word	.LANCHOR0
	.word	.LANCHOR2-2808
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
	ldr	r5, .L2052
	mul	r4, r4, r0
	sub	r0, r5, #504
	ldr	r3, [r5, #-2896]
	add	r2, r3, r4
	ldrh	r1, [r3, r4]
	ldr	r2, [r2, #8]
	bl	FtlMapWritePage
	ldr	r3, [r5, #-2896]
	mov	r0, #0
	add	r4, r3, r4
	ldr	r3, [r4, #4]
	bic	r3, r3, #-2147483648
	str	r3, [r4, #4]
	ldmfd	sp!, {r3, r4, r5, pc}
.L2053:
	.align	2
.L2052:
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
	ldr	r3, .L2070
	mov	r1, #0
	stmfd	sp!, {r4, r5, r6, lr}
	.save {r4, r5, r6, lr}
	sub	r2, r3, #3152
	ldr	r3, [r3, #-2896]
	mov	ip, #12
	ldrh	r2, [r2, #-10]
	movw	lr, #65535
.L2055:
	uxth	r0, r1
	cmp	r0, r2
	bcs	.L2067
	add	r1, r1, #1
	mla	r4, ip, r1, r3
	ldrh	r4, [r4, #-12]
	cmp	r4, lr
	bne	.L2055
	ldmfd	sp!, {r4, r5, r6, pc}
.L2067:
	mov	r0, r2
	mov	r1, #0
	mov	ip, #-2147483648
	mov	r5, #12
.L2058:
	uxth	r4, r1
	cmp	r4, r2
	bcs	.L2068
	mla	lr, r5, r1, r3
	add	r1, r1, #1
	ldr	lr, [lr, #4]
	cmp	lr, ip
	mvn	r6, lr
	mov	r6, r6, lsr #31
	movcs	r6, #0
	cmp	r6, #0
	movne	ip, lr
	movne	r0, r4
	b	.L2058
.L2068:
	cmp	r0, r2
	ldmccfd	sp!, {r4, r5, r6, pc}
	ldr	r1, .L2070+4
	mov	r0, r2
	mvn	ip, #0
	ldrh	r5, [r1]
	mov	r1, #0
.L2061:
	uxth	lr, r1
	cmp	lr, r2
	bcs	.L2069
	ldr	r4, [r3, #4]
	cmp	r4, ip
	bcs	.L2062
	ldrh	r6, [r3]
	cmp	r6, r5
	movne	ip, r4
	movne	r0, lr
.L2062:
	add	r1, r1, #1
	add	r3, r3, #12
	b	.L2061
.L2069:
	ldmfd	sp!, {r4, r5, r6, pc}
.L2071:
	.align	2
.L2070:
	.word	.LANCHOR2
	.word	.LANCHOR2-460
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
	ldr	r6, .L2089
	sub	r3, r6, #3184
	str	r3, [sp, #8]
	ldr	ip, [r6, #-2816]
	ldrh	r10, [r3, #-8]
	cmp	r0, ip
	bcs	.L2073
	add	r10, r10, #7
	mov	fp, r6
	mov	r6, r0, lsr r10
	sub	r3, fp, #3152
	str	r2, [sp, #12]
	mov	r9, r1
	ldrh	r2, [r3, #-10]
	uxth	r6, r6
	str	r0, [sp, #4]
	mov	r3, #0
	ldr	r4, [fp, #-2896]
	mov	r1, #12
	b	.L2074
.L2073:
	cmp	r2, #0
	mvn	r0, #0
	streq	r0, [r1]
	b	.L2075
.L2079:
	add	r3, r3, #1
	mla	r0, r1, r3, r4
	ldrh	r0, [r0, #-12]
	cmp	r0, r6
	beq	.L2076
.L2074:
	uxth	r7, r3
	cmp	r7, r2
	bcc	.L2079
	bl	select_l2p_ram_region
	mov	r5, #12
	movw	r2, #65535
	mul	r5, r5, r0
	mov	r7, r0
	add	r3, r4, r5
	ldrh	r1, [r4, r5]
	cmp	r1, r2
	bne	.L2088
.L2080:
	ldr	r3, [fp, #-2908]
	ldr	r4, .L2089
	ldr	r8, [r3, r6, asl #2]
	cmp	r8, #0
	bne	.L2081
	ldr	r2, [r4, #-2896]
	mov	r1, #255
	ldr	r3, [sp, #8]
	add	r2, r2, r5
	ldr	r0, [r2, #8]
	ldrh	r2, [r3, #-6]
	bl	ftl_memset
	ldr	r2, [r4, #-2896]
	strh	r6, [r2, r5]	@ movhi
	ldr	r2, [r4, #-2896]
	add	r5, r2, r5
	str	r8, [r5, #4]
.L2076:
	ldr	r3, [sp, #4]
	mvn	r2, #0
	bic	r10, r3, r2, asl r10
	ldr	r3, [sp, #12]
	cmp	r3, #0
	uxth	r10, r10
	mov	r3, #12
	bne	.L2077
	ldr	r2, [fp, #-2896]
	mla	r3, r3, r7, r2
	ldr	r3, [r3, #8]
	ldr	r3, [r3, r10, asl #2]
	str	r3, [r9]
	b	.L2078
.L2077:
	mul	r3, r3, r7
	ldr	r2, [fp, #-2896]
	ldr	r1, [r9]
	add	r2, r2, r3
	ldr	r2, [r2, #8]
	str	r1, [r2, r10, asl #2]
	ldr	r2, [fp, #-2896]
	add	r3, r2, r3
	ldr	r2, [r3, #4]
	orr	r2, r2, #-2147483648
	str	r2, [r3, #4]
	ldr	r3, .L2089+4
	strh	r6, [r3]	@ movhi
.L2078:
	ldr	r2, [fp, #-2896]
	mov	r3, #12
	mov	r0, #0
	mla	r7, r3, r7, r2
	ldr	r3, [r7, #4]
	cmn	r3, #1
	addne	r3, r3, #1
	strne	r3, [r7, #4]
	b	.L2075
.L2088:
	ldr	r3, [r3, #4]
	cmp	r3, #0
	bge	.L2080
	bl	flush_l2p_region
	b	.L2080
.L2081:
	ldr	r2, [r4, #-2896]
	mov	r1, #1
	ldr	r0, .L2089+8
	add	r2, r2, r5
	str	r8, [r4, #-2804]
	ldr	r2, [r2, #8]
	str	r2, [r4, #-2800]
	ldr	r2, [r4, #-2976]
	str	r2, [r4, #-2796]
	mov	r2, r1
	bl	FlashReadPages
	ldr	r2, [r4, #-2796]
	ldrh	r2, [r2, #8]
	cmp	r2, r6
	beq	.L2082
	mov	r1, r6
	mov	r2, r8
	ldr	r0, .L2089+12
	bl	printk
	mov	r2, #4
	mov	r3, r2
	ldr	r0, .L2089+16
	ldr	r1, [r4, #-2796]
	bl	rknand_print_hex
	sub	r3, r4, #3152
	ldr	r0, .L2089+20
	mov	r2, #4
	ldrh	r3, [r3, #-12]
	ldr	r1, [r4, #-2908]
	bl	rknand_print_hex
	mov	r3, #1
	str	r3, [r4, #-2812]
	b	.L2083
.L2082:
	ldr	r2, [r4, #-2808]
	cmp	r2, #256
	bne	.L2083
	mov	r1, r6
	mov	r2, r8
	ldr	r0, .L2089+24
	bl	printk
	ldr	r3, [r4, #-2896]
	sub	r0, r4, #504
	mov	r1, r6
	add	r3, r3, r5
	ldr	r2, [r3, #8]
	bl	FtlMapWritePage
.L2083:
	ldr	r3, [fp, #-2896]
	mov	r1, #0
	add	r2, r3, r5
	str	r1, [r2, #4]
	strh	r6, [r3, r5]	@ movhi
	b	.L2076
.L2075:
	add	sp, sp, #20
	@ sp needed
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L2090:
	.align	2
.L2089:
	.word	.LANCHOR2
	.word	.LANCHOR2-460
	.word	.LANCHOR2-2808
	.word	.LC41
	.word	.LC17
	.word	.LC42
	.word	.LC43
	.fnend
	.size	log2phys, .-log2phys
	.align	2
	.global	FtlVendorPartWrite
	.type	FtlVendorPartWrite, %function
FtlVendorPartWrite:
	.fnstart
	@ args = 0, pretend = 0, frame = 64
	@ frame_needed = 0, uses_anonymous_args = 0
	ldr	r3, .L2103
	stmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.save {r4, r5, r6, r7, r8, r9, r10, fp, lr}
	mov	fp, r2
	ldr	r6, .L2103+4
	.pad #68
	sub	sp, sp, #68
	ldr	r2, [r3]
	mov	r4, r1
	add	r1, r0, r1
	mov	r7, r0
	ldrh	r5, [r6, #-8]
	str	r2, [sp, #60]
	ldrh	r2, [r6, #-20]
	str	r3, [sp, #12]
	cmp	r1, r2
	mvnhi	r0, #0
	bhi	.L2092
	mov	r5, r7, lsr r5
	mov	r3, #0
	str	r3, [sp, #4]
	add	r3, r6, #3184
	mov	r10, r5, asl #2
	str	r3, [sp, #8]
	mov	r8, r3
.L2093:
	cmp	r4, #0
	beq	.L2102
	ldr	r3, [sp, #8]
	mov	r0, r7
	ldr	r3, [r3, #-2912]
	ldr	ip, [r3, r10]
	ldr	r3, .L2103+8
	str	ip, [sp, #20]
	ldrh	r2, [r3]
	mov	r1, r2
	str	r2, [sp, #16]
	bl	__aeabi_uidivmod
	ldr	r2, [sp, #16]
	mov	r9, r1
	ldr	ip, [sp, #20]
	rsb	r3, r1, r2
	uxth	r6, r3
	cmp	r6, r4
	uxthhi	r6, r4
	cmp	r6, r2
	cmpne	ip, #0
	movne	r1, #1
	moveq	r1, #0
	beq	.L2095
	ldr	r2, [r8, #-3000]
	mov	r1, #1
	add	r0, sp, #24
	str	ip, [sp, #28]
	str	r2, [sp, #32]
	mov	r2, #0
	str	r2, [sp, #36]
	mov	r2, r1
	bl	FlashReadPages
	b	.L2096
.L2095:
	ldr	r2, .L2103+12
	ldr	r0, [r8, #-3000]
	ldrh	r2, [r2]
	bl	ftl_memset
.L2096:
	mov	ip, r6, asl #9
	ldr	r0, [r8, #-3000]
	uxth	r9, r9
	mov	r1, fp
	mov	r2, ip
	str	ip, [sp, #16]
	add	r0, r0, r9, asl #9
	rsb	r4, r6, r4
	bl	ftl_memcpy
	mov	r1, r5
	ldr	r0, .L2103+16
	add	r5, r5, #1
	ldr	r2, [r8, #-3000]
	add	r7, r7, r6
	bl	FtlMapWritePage
	add	r10, r10, #4
	ldr	r3, [sp, #4]
	cmn	r0, #1
	ldr	ip, [sp, #16]
	mvneq	r3, #0
	add	fp, fp, ip
	str	r3, [sp, #4]
	b	.L2093
.L2102:
	ldr	r0, [sp, #4]
.L2092:
	ldr	r3, [sp, #12]
	ldr	r2, [sp, #60]
	ldr	r3, [r3]
	cmp	r2, r3
	beq	.L2099
	bl	__stack_chk_fail
.L2099:
	add	sp, sp, #68
	@ sp needed
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L2104:
	.align	2
.L2103:
	.word	__stack_chk_guard
	.word	.LANCHOR2-3184
	.word	.LANCHOR2-3194
	.word	.LANCHOR2-3190
	.word	.LANCHOR2-456
	.fnend
	.size	FtlVendorPartWrite, .-FtlVendorPartWrite
	.align	2
	.global	FtlVendorPartRead
	.type	FtlVendorPartRead, %function
FtlVendorPartRead:
	.fnstart
	@ args = 0, pretend = 0, frame = 56
	@ frame_needed = 0, uses_anonymous_args = 0
	ldr	r3, .L2118
	stmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.save {r4, r5, r6, r7, r8, r9, r10, fp, lr}
	mov	r8, r2
	ldr	r5, .L2118+4
	.pad #60
	sub	sp, sp, #60
	ldr	r2, [r3]
	mov	r6, r1
	add	r1, r0, r1
	mov	r7, r0
	ldrh	r4, [r5, #-8]
	str	r2, [sp, #52]
	ldrh	r2, [r5, #-20]
	str	r3, [sp, #12]
	cmp	r1, r2
	mvnhi	r0, #0
	bhi	.L2106
	add	r5, r5, #3184
	mov	r4, r7, lsr r4
	mov	fp, r5
	mov	r3, r4, asl #2
	str	r3, [sp, #4]
	mov	r3, #0
	str	r3, [sp]
.L2107:
	cmp	r6, #0
	beq	.L2117
	ldr	r3, [r5, #-2912]
	mov	r0, r7
	ldr	r2, [sp, #4]
	ldr	r9, [r3, r2]
	ldr	r3, .L2118+8
	ldrh	r10, [r3]
	mov	r1, r10
	bl	__aeabi_uidivmod
	rsb	r3, r1, r10
	str	r1, [sp, #8]
	uxth	r10, r3
	cmp	r10, r6
	uxthhi	r10, r6
	cmp	r9, #0
	beq	.L2109
	ldr	r2, [fp, #-3000]
	mov	r1, #1
	add	r0, sp, #16
	str	r9, [sp, #20]
	str	r2, [sp, #24]
	mov	r2, #0
	str	r2, [sp, #28]
	mov	r2, r1
	bl	FlashReadPages
	ldr	r2, [sp, #16]
	ldr	r3, [sp]
	cmn	r2, #1
	ldr	r2, [fp, #-2808]
	mvneq	r3, #0
	cmp	r2, #256
	str	r3, [sp]
	bne	.L2111
	mov	r1, r4
	mov	r2, r9
	ldr	r0, .L2118+12
	bl	printk
	ldr	r0, .L2118+16
	mov	r1, r4
	ldr	r2, [r5, #-3000]
	bl	FtlMapWritePage
.L2111:
	ldrh	r9, [sp, #8]
	mov	r0, r8
	ldr	r1, [fp, #-3000]
	mov	r2, r10, asl #9
	add	r1, r1, r9, asl #9
	bl	ftl_memcpy
	b	.L2112
.L2109:
	mov	r0, r8
	mov	r1, r9
	mov	r2, r10, asl #9
	bl	ftl_memset
.L2112:
	ldr	r3, [sp, #4]
	add	r4, r4, #1
	rsb	r6, r10, r6
	add	r7, r7, r10
	add	r3, r3, #4
	add	r8, r8, r10, asl #9
	str	r3, [sp, #4]
	b	.L2107
.L2117:
	ldr	r0, [sp]
.L2106:
	ldr	r3, [sp, #12]
	ldr	r2, [sp, #52]
	ldr	r3, [r3]
	cmp	r2, r3
	beq	.L2114
	bl	__stack_chk_fail
.L2114:
	add	sp, sp, #60
	@ sp needed
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L2119:
	.align	2
.L2118:
	.word	__stack_chk_guard
	.word	.LANCHOR2-3184
	.word	.LANCHOR2-3194
	.word	.LC44
	.word	.LANCHOR2-456
	.fnend
	.size	FtlVendorPartRead, .-FtlVendorPartRead
	.align	2
	.global	FtlUpdateVaildLpn
	.type	FtlUpdateVaildLpn, %function
FtlUpdateVaildLpn:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	ldr	r3, .L2128
	sub	r1, r3, #412
	ldrh	r2, [r1]
	cmp	r2, #4
	cmpls	r0, #0
	addeq	r2, r2, #1
	streqh	r2, [r1]	@ movhi
	bxeq	lr
	str	lr, [sp, #-4]!
	.save {lr}
	mov	r2, #0
	strh	r2, [r1]	@ movhi
	sub	r1, r3, #3248
	str	r2, [r3, #-408]
	movw	ip, #65535
	ldrh	r0, [r1, #-12]
	ldr	r2, [r3, #-2940]
	add	r0, r2, r0, asl #1
.L2122:
	cmp	r2, r0
	beq	.L2127
	ldrh	r1, [r2], #2
	cmp	r1, ip
	ldrne	lr, [r3, #-408]
	addne	r1, r1, lr
	strne	r1, [r3, #-408]
	b	.L2122
.L2127:
	ldr	pc, [sp], #4
.L2129:
	.align	2
.L2128:
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
	ldr	r4, .L2137
	mov	r2, #0
	str	r2, [r0, #36]
	ldr	r2, [r4, #-2812]
	cmp	r2, #0
	ldmnefd	sp!, {r4, r5, r6, pc}
	sub	r5, r5, #1
	mov	r6, r0
	ldr	r2, [r4, #-2976]
	ldr	r0, [r4, #-3004]
	uxth	r5, r5
	str	r2, [r4, #-2796]
	str	r0, [r4, #-2800]
	ldr	r3, [r3, r5, asl #2]
	cmp	r3, #0
	str	r3, [r4, #-2804]
	beq	.L2134
	mov	r1, #1
	ldr	r0, .L2137+4
	mov	r2, r1
	bl	FlashReadPages
	b	.L2135
.L2134:
	sub	r3, r4, #3184
	mov	r1, #255
	ldrh	r2, [r3, #-6]
	bl	ftl_memset
.L2135:
	mov	r0, r6
	mov	r1, r5
	ldr	r2, [r4, #-2800]
	ldmfd	sp!, {r4, r5, r6, lr}
	b	FtlMapWritePage
.L2138:
	.align	2
.L2137:
	.word	.LANCHOR2
	.word	.LANCHOR2-2808
	.fnend
	.size	FtlMapBlkWriteDumpData, .-FtlMapBlkWriteDumpData
	.align	2
	.global	FtlVpcTblFlush
	.type	FtlVpcTblFlush, %function
FtlVpcTblFlush:
	.fnstart
	@ args = 0, pretend = 0, frame = 8
	@ frame_needed = 0, uses_anonymous_args = 0
	stmfd	sp!, {r0, r1, r2, r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.save {r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.pad #12
	ldr	r4, .L2157
	ldr	r3, [r4, #-2812]
	cmp	r3, #0
	bne	.L2141
	ldr	r2, [r4, #-3008]
	sub	r7, r4, #404
	ldr	r6, [r4, #-2976]
	sub	r5, r4, #716
	mov	r1, #255
	str	r2, [r4, #-2800]
	ldrh	r2, [r7]
	str	r6, [r4, #-2796]
	str	r3, [r6, #12]
	strh	r2, [r6, #2]	@ movhi
	ldr	r2, .L2157+4
	strh	r2, [r6]	@ movhi
	ldr	r2, [r4, #-396]
	stmib	r6, {r2, r3}
	ldr	r3, .L2157+8
	str	r3, [r4, #-716]
	ldr	r3, .L2157+12
	str	r3, [r4, #-712]
	ldrh	r3, [r7, #6]
	strh	r3, [r5, #8]	@ movhi
	sub	r3, r4, #3232
	ldrh	r3, [r3, #-14]
	strb	r3, [r4, #-706]
	sub	r3, r4, #648
	ldrh	r2, [r3]
	strh	r2, [r5, #14]	@ movhi
	ldrh	r2, [r3, #2]
	ldrb	r3, [r4, #-642]	@ zero_extendqisi2
	orr	r3, r3, r2, asl #6
	strh	r3, [r5, #16]	@ movhi
	ldrb	r3, [r4, #-640]	@ zero_extendqisi2
	strb	r3, [r4, #-705]
	sub	r3, r4, #600
	ldrh	r2, [r3]
	strh	r2, [r5, #18]	@ movhi
	ldrh	r2, [r3, #2]
	ldrb	r3, [r4, #-594]	@ zero_extendqisi2
	orr	r3, r3, r2, asl #6
	strh	r3, [r5, #20]	@ movhi
	ldrb	r3, [r4, #-592]	@ zero_extendqisi2
	strb	r3, [r4, #-704]
	sub	r3, r4, #552
	ldrh	r2, [r3]
	strh	r2, [r5, #22]	@ movhi
	ldrh	r2, [r3, #2]
	ldrb	r3, [r4, #-546]	@ zero_extendqisi2
	ldr	r0, [r4, #-2800]
	orr	r3, r3, r2, asl #6
	strh	r3, [r5, #24]	@ movhi
	ldrb	r3, [r4, #-544]	@ zero_extendqisi2
	strb	r3, [r4, #-703]
	ldr	r3, [r4, #-3116]
	str	r3, [r4, #-684]
	ldr	r3, [r4, #-3148]
	str	r3, [r4, #-676]
	ldr	r3, [r4, #-3144]
	str	r3, [r4, #-680]
	sub	r3, r4, #3072
	ldrh	r3, [r3]
	strh	r3, [r5, #44]	@ movhi
	sub	r3, r4, #3056
	ldrh	r3, [r3, #-14]
	strh	r3, [r5, #46]	@ movhi
	sub	r3, r4, #3184
	ldrh	r2, [r3, #-6]
	bl	ftl_memset
	mov	r1, r5
	sub	r5, r4, #3248
	mov	r2, #48
	ldr	r0, [r4, #-2800]
	bl	ftl_memcpy
	ldrh	r2, [r5, #-12]
	ldr	r0, [r4, #-2800]
	ldr	r1, [r4, #-2940]
	mov	r2, r2, asl #1
	add	r0, r0, #48
	bl	ftl_memcpy
	ldrh	r2, [r5, #-12]
	ldr	r0, [r4, #-2800]
	add	r3, r2, #24
	ldr	r1, [r4, #-2928]
	ubfx	r3, r3, #1, #30
	mov	r2, r2, lsr #3
	add	r2, r2, #4
	add	r0, r0, r3, asl #2
	bl	ftl_memcpy
	sub	r2, r4, #3152
	ldrh	r3, [r2, #-8]
	cmp	r3, #0
	beq	.L2142
	ldrh	r1, [r5, #-12]
	ldrh	r2, [r2, #-12]
	ldr	r0, [r4, #-2800]
	mov	r3, r1, lsr #3
	add	r3, r3, r1, asl #1
	mov	r2, r2, asl #2
	add	r3, r3, #52
	ldr	r1, [r4, #-2908]
	ubfx	r3, r3, #2, #14
	add	r0, r0, r3, asl #2
	bl	ftl_memcpy
.L2142:
	mov	r0, #0
	ldr	r8, .L2157
	bl	FtlUpdateVaildLpn
	ldr	r10, .L2157+16
	ldr	fp, .L2157+20
	mov	r5, #0
	movw	r9, #65535
.L2143:
	ldrh	r1, [r7]
	ldrh	r0, [r7, #2]
	ldr	r3, [r4, #-3008]
	orr	r2, r0, r1, asl #10
	str	r2, [r4, #-2804]
	ldrh	r2, [r10]
	str	r3, [r4, #-2800]
	sub	r2, r2, #1
	ldr	r3, [r4, #-2976]
	cmp	r0, r2
	str	r3, [r4, #-2796]
	ldr	r3, .L2157+24
	blt	.L2144
	ldrh	r9, [r3, #4]
	mov	r2, #0
	strh	r1, [r3, #4]	@ movhi
	strh	r2, [r3, #2]	@ movhi
	str	r3, [sp, #4]
	bl	FtlFreeSysBlkQueueOut
	ldr	r3, [sp, #4]
	strh	r0, [r3]	@ movhi
	ldr	r3, [r8, #-3148]
	add	r2, r3, #1
	str	r2, [r8, #-3148]
	str	r3, [r8, #-396]
	mov	r2, r0, asl #10
	str	r2, [r8, #-2804]
	str	r3, [r6, #4]
	strh	r0, [r6, #2]	@ movhi
.L2144:
	ldrb	r3, [fp]	@ zero_extendqisi2
	cmp	r3, #0
	beq	.L2145
	ldr	r3, .L2157+28
	ldr	r0, [r4, #-3008]
	ldrh	r1, [r3]
	bl	js_hash
	str	r0, [r6, #12]
.L2145:
	mov	r1, #1
	ldr	r0, .L2157+32
	mov	r2, r1
	mov	r3, r1
	bl	FlashProgPages
	ldrh	r3, [r7, #2]
	ldr	r2, [r4, #-2808]
	add	r3, r3, #1
	cmn	r2, #1
	uxth	r3, r3
	strh	r3, [r7, #2]	@ movhi
	bne	.L2146
	cmp	r3, #1
	add	r5, r5, #1
	ldreqh	r3, [r10]
	uxth	r5, r5
	ldreq	r2, .L2157+24
	subeq	r3, r3, #1
	streqh	r3, [r2, #2]	@ movhi
	cmp	r5, #3
	bls	.L2143
	ldr	r0, .L2157+36
	mov	r2, r5
	ldr	r1, [r4, #-2804]
	bl	printk
	mov	r3, #1
	str	r3, [r4, #-2812]
	b	.L2141
.L2146:
	cmp	r2, #256
	cmpne	r3, #1
	beq	.L2143
	movw	r3, #65535
	cmp	r9, r3
	beq	.L2141
	mov	r0, r9
	mov	r1, #1
	bl	FtlFreeSysBlkQueueIn
.L2141:
	mov	r0, #0
	add	sp, sp, #12
	@ sp needed
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L2158:
	.align	2
.L2157:
	.word	.LANCHOR2
	.word	-3932
	.word	1179929683
	.word	1342177367
	.word	.LANCHOR2-3198
	.word	.LANCHOR0
	.word	.LANCHOR2-404
	.word	.LANCHOR2-3190
	.word	.LANCHOR2-2808
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
	mov	fp, #0
	ldr	r10, .L2245
	mov	r1, fp
	.pad #28
	sub	sp, sp, #28
	sub	r3, r10, #2816
	sub	r5, r10, #388
	ldr	r2, [r10, #-3172]
	sub	r4, r10, #3168
	ldr	r0, [r10, #-2900]
	strh	fp, [r3, #-4]	@ movhi
	mov	r2, r2, asl #2
	strh	fp, [r5]	@ movhi
	bl	ftl_memset
	ldr	r2, [r10, #-3172]
	mov	r1, fp
	ldr	r0, [r10, #-2936]
	mov	r2, r2, asl #1
	bl	ftl_memset
	ldrh	r2, [r4, #-12]
	mov	r1, fp
	ldr	r0, [r10, #-2916]
	mov	r2, r2, asl #2
	bl	ftl_memset
	ldrh	r2, [r4, #-12]
	mov	r1, fp
	ldr	r0, [r10, #-2924]
	mov	r4, r10
	mov	r2, r2, asl #1
	bl	ftl_memset
	sub	r0, r10, #404
	mov	r1, #255
	mov	r2, #16
	bl	ftl_memset
	sub	r3, r10, #3248
	ldrh	fp, [r3, #-12]
	str	r5, [sp, #12]
	mov	r5, r10
.L2160:
	ldr	r3, .L2245+4
	ldr	r1, .L2245
	ldrh	r3, [r3]
	cmp	r3, fp
	bls	.L2201
	ldr	r1, [r5, #-2968]
	mov	r6, #0
	ldr	r3, .L2245+8
	mov	r8, r6
	ldr	r2, [r5, #-3036]
	mov	r7, #36
	str	r1, [sp]
	ldr	r1, .L2245+12
	ldrh	ip, [r3]
	ldr	r3, [r5, #-2996]
	sub	r9, r1, #52
	ldrh	r10, [r1]
.L2202:
	uxth	r1, r6
	cmp	r1, ip
	bcs	.L2240
	mov	r1, fp
	ldrb	r0, [r9, r6]	@ zero_extendqisi2
	str	r3, [sp, #20]
	str	r2, [sp, #16]
	str	ip, [sp, #8]
	bl	V2P_block
	str	r0, [sp, #4]
	bl	FtlBbmIsBadBlock
	cmp	r0, #0
	ldmib	sp, {r1, ip}
	ldr	r2, [sp, #16]
	ldr	r3, [sp, #20]
	bne	.L2161
	mla	r0, r7, r8, r2
	mov	r1, r1, asl #10
	stmib	r0, {r1, r3}
	mul	r1, r10, r8
	add	r8, r8, #1
	uxth	r8, r8
	add	lr, r1, #3
	cmp	r1, #0
	movlt	r1, lr
	ldr	lr, [sp]
	bic	r1, r1, #3
	add	r1, lr, r1
	str	r1, [r0, #12]
.L2161:
	add	r6, r6, #1
	b	.L2202
.L2240:
	cmp	r8, #0
	beq	.L2164
	ldr	r0, [r4, #-3036]
	mov	r1, r8
	mov	r2, #1
	bl	FlashReadPages
	mov	r3, #0
.L2238:
	str	r3, [sp]
	ldrh	r3, [sp]
	cmp	r3, r8
	bcs	.L2164
	ldr	r3, [sp]
	mov	r9, #36
	mul	r9, r9, r3
	ldr	r3, [r4, #-3036]
	add	r2, r3, r9
	ldr	r3, [r3, r9]
	ldr	r6, [r2, #4]
	cmn	r3, #1
	ldr	r7, [r2, #12]
	ubfx	r6, r6, #10, #16
	bne	.L2167
	mov	r10, #16
	movw	ip, #65535
.L2166:
	ldr	r0, [r4, #-3036]
	mov	r1, #1
	mov	r2, r1
	str	ip, [sp, #4]
	add	r0, r0, r9
	ldr	r3, [r0, #4]
	add	r3, r3, #1
	str	r3, [r0, #4]
	bl	FlashReadPages
	ldrh	r3, [r7]
	ldr	ip, [sp, #4]
	cmp	r3, ip
	ldreq	r3, .L2245
	mvneq	r2, #0
	ldreq	r3, [r3, #-3036]
	streq	r2, [r3, r9]
	beq	.L2167
.L2168:
	ldr	r3, [r5, #-3036]
	ldr	r3, [r3, r9]
	cmn	r3, #1
	bne	.L2167
	sub	r10, r10, #1
	uxth	r10, r10
	cmp	r10, #0
	bne	.L2166
.L2167:
	ldr	r3, [r5, #-3036]
	ldr	r3, [r3, r9]
	ldr	r9, .L2245
	cmn	r3, #1
	beq	.L2170
	ldr	r2, [r5, #-3148]
	ldr	r3, [r7, #4]
	cmn	r2, #1
	beq	.L2171
	cmp	r2, r3
	bhi	.L2172
.L2171:
	cmn	r3, #1
	addne	r2, r3, #1
	strne	r2, [r9, #-3148]
.L2172:
	ldrh	r2, [r7]
	movw	r1, #61604
	cmp	r2, r1
	beq	.L2174
	bhi	.L2175
	movw	r3, #61574
	cmp	r2, r3
	bne	.L2173
	ldr	r2, .L2245+16
	ldr	lr, [r4, #-2916]
	ldrh	ip, [r2]
	add	r2, r2, #360
	sub	r0, ip, #1
	ldrh	r1, [r2]
	uxth	r3, r0
	rsb	r2, r1, r0
	str	r2, [sp, #4]
	b	.L2188
.L2175:
	movw	r3, #61634
	cmp	r2, r3
	beq	.L2177
	movw	r3, #65535
	cmp	r2, r3
	moveq	r0, r6
	beq	.L2239
	b	.L2173
.L2177:
	ldr	ip, [r4, #-3172]
	ldr	r2, [sp, #12]
	ldr	lr, [r4, #-2900]
	uxth	r1, ip
	ldrh	r2, [r2]
	sub	r3, r1, #1
	rsb	r1, r2, r1
	uxth	r3, r3
	sub	r1, r1, #1
	sxth	r1, r1
	str	r1, [sp, #8]
.L2179:
	ldr	r1, [sp, #8]
	sxth	r0, r3
	cmp	r0, r1
	ble	.L2241
	mov	r10, r0, asl #2
	ldr	r1, [r7, #4]
	str	r10, [sp, #4]
	ldr	r10, [lr, r0, asl #2]
	cmp	r1, r10
	bls	.L2180
	ldr	r1, [lr]
	cmp	r1, #0
	bne	.L2181
	cmp	r2, ip
	addne	r2, r2, #1
	ldrne	r1, .L2245+20
	strneh	r2, [r1]	@ movhi
.L2181:
	uxth	lr, r3
	mov	r1, #0
.L2182:
	uxth	r2, r1
	cmp	r2, lr
	bcs	.L2242
	ldr	ip, [r5, #-2900]
	sxth	r2, r2
	add	r1, r1, #1
	add	r10, ip, r2, asl #2
	ldr	r10, [r10, #4]
	str	r10, [ip, r2, asl #2]
	mov	r2, r2, asl #1
	ldr	ip, [r5, #-2936]
	add	r10, ip, r2
	ldrh	r10, [r10, #2]
	strh	r10, [ip, r2]	@ movhi
	b	.L2182
.L2242:
	ldr	r2, [r9, #-2900]
	mov	r0, r0, asl #1
	ldr	r1, [r7, #4]
	ldr	ip, [sp, #4]
	str	r1, [r2, ip]
	ldr	r2, [r9, #-2936]
	strh	r6, [r2, r0]	@ movhi
	sxth	r0, r3
	cmp	r0, #0
	bge	.L2184
	b	.L2173
.L2180:
	sub	r3, r3, #1
	uxth	r3, r3
	b	.L2179
.L2241:
	cmp	r0, #0
	bge	.L2215
	b	.L2173
.L2184:
	ldr	r2, .L2245+20
	ldr	r1, [r5, #-3172]
	ldrh	r2, [r2]
	rsb	r1, r2, r1
	sub	r1, r1, #1
	sxth	r1, r1
	cmp	r0, r1
	bgt	.L2173
.L2215:
	ldr	r1, .L2245+20
	add	r2, r2, #1
	sxth	r3, r3
	strh	r2, [r1]	@ movhi
	ldr	r2, [r4, #-2900]
	ldr	r1, [r7, #4]
	str	r1, [r2, r3, asl #2]
	mov	r3, r3, asl #1
	ldr	r2, [r4, #-2936]
	b	.L2236
.L2194:
	mov	r10, r2, asl #2
	ldr	r0, [r7, #4]
	str	r10, [sp, #8]
	ldr	r10, [lr, r2, asl #2]
	cmp	r0, r10
	bhi	.L2243
	sub	r3, r3, #1
	uxth	r3, r3
.L2188:
	ldr	r0, [sp, #4]
	sxth	r2, r3
	cmp	r2, r0
	bgt	.L2194
	b	.L2193
.L2243:
	ldr	r0, [lr]
	cmp	r0, #0
	bne	.L2190
	cmp	r1, ip
	addne	r1, r1, #1
	ldrne	r0, .L2245+24
	strneh	r1, [r0]	@ movhi
.L2190:
	uxth	lr, r3
	mov	r0, #0
.L2191:
	uxth	r1, r0
	cmp	r1, lr
	bcs	.L2244
	ldr	ip, [r5, #-2916]
	sxth	r1, r1
	add	r0, r0, #1
	add	r10, ip, r1, asl #2
	ldr	r10, [r10, #4]
	str	r10, [ip, r1, asl #2]
	mov	r1, r1, asl #1
	ldr	ip, [r5, #-2924]
	add	r10, ip, r1
	ldrh	r10, [r10, #2]
	strh	r10, [ip, r1]	@ movhi
	b	.L2191
.L2244:
	ldr	r1, [r9, #-2916]
	mov	r2, r2, asl #1
	ldr	r0, [r7, #4]
	ldr	ip, [sp, #8]
	str	r0, [r1, ip]
	ldr	r1, [r9, #-2924]
	strh	r6, [r1, r2]	@ movhi
.L2193:
	sxth	r3, r3
	cmp	r3, #0
	blt	.L2173
	ldr	r1, .L2245+24
	sub	r0, r1, #360
	ldrh	r2, [r1]
	ldrh	r9, [r0]
	sub	r9, r9, #1
	rsb	r9, r2, r9
	sxth	r9, r9
	cmp	r3, r9
	bgt	.L2173
	add	r2, r2, #1
	strh	r2, [r1]	@ movhi
	ldr	r2, [r5, #-2916]
	ldr	r1, [r7, #4]
	str	r1, [r2, r3, asl #2]
	mov	r3, r3, asl #1
	ldr	r2, [r5, #-2924]
.L2236:
	strh	r6, [r2, r3]	@ movhi
	b	.L2173
.L2174:
	ldr	r2, .L2245+28
	ldrh	r1, [r2]
	movw	r2, #65535
	cmp	r1, r2
	ldreq	r2, .L2245+28
	streqh	r6, [r2]	@ movhi
	beq	.L2237
	ldr	r3, .L2245+28
	ldrh	r0, [r3, #4]
	cmp	r0, r2
	beq	.L2196
	mov	r1, #1
	bl	FtlFreeSysBlkQueueIn
.L2196:
	ldr	r3, [r7, #4]
	ldr	r2, [r9, #-396]
	cmp	r2, r3
	ldr	r3, .L2245+28
	strcsh	r6, [r3, #4]	@ movhi
	bcs	.L2173
	ldrh	r3, [r3]
	ldr	r2, .L2245+28
	strh	r3, [r2, #4]	@ movhi
	ldr	r3, [r7, #4]
	strh	r6, [r2]	@ movhi
.L2237:
	str	r3, [r5, #-396]
	b	.L2173
.L2170:
	ldr	r3, .L2245+32
	mov	r0, r6
	ldrb	r1, [r3, #3904]	@ zero_extendqisi2
	cmp	r1, #0
	beq	.L2198
.L2239:
	mov	r1, #0
.L2198:
	bl	FtlFreeSysBlkQueueIn
.L2173:
	ldr	r3, [sp]
	add	r3, r3, #1
	b	.L2238
.L2164:
	add	r8, fp, #1
	uxth	fp, r8
	b	.L2160
.L2201:
	ldr	ip, [r1, #-2936]
	ldrh	r2, [ip]
	cmp	r2, #0
	beq	.L2203
.L2206:
	ldr	r0, [r1, #-2924]
	ldrh	r2, [r0]
	cmp	r2, #0
	beq	.L2204
	b	.L2227
.L2203:
	sub	r3, r1, #388
	ldrh	r3, [r3]
	cmp	r3, #0
	ldrne	lr, [r1, #-3172]
	beq	.L2206
.L2207:
	uxth	r3, r2
	sxth	r0, r3
	cmp	r0, lr
	bcs	.L2206
	mov	r4, r0, asl #1
	add	r2, r2, #1
	ldrh	r4, [ip, r4]
	cmp	r4, #0
	beq	.L2207
	ldr	ip, .L2245
	mov	r6, #0
.L2208:
	ldr	lr, [r1, #-3172]
	sxth	r2, r3
	cmp	r2, lr
	bcs	.L2206
	ldr	r4, [ip, #-2936]
	mov	lr, r2, asl #1
	rsb	r5, r0, r2
	add	r3, r3, #1
	ldrh	r8, [r4, lr]
	mov	r7, r5, asl #1
	uxth	r3, r3
	strh	r8, [r4, r7]	@ movhi
	ldr	r4, [ip, #-2900]
	ldr	r2, [r4, r2, asl #2]
	str	r2, [r4, r5, asl #2]
	ldr	r2, [ip, #-2936]
	strh	r6, [r2, lr]	@ movhi
	b	.L2208
.L2204:
	ldr	r3, .L2245+36
	ldrh	r1, [r3, #-4]
	cmp	r1, #0
	subne	r3, r3, #352
	ldrneh	ip, [r3, #-12]
	beq	.L2227
.L2211:
	uxth	r3, r2
	sxth	r1, r3
	cmp	r1, ip
	bge	.L2227
	mov	lr, r1, asl #1
	add	r2, r2, #1
	ldrh	lr, [r0, lr]
	cmp	lr, #0
	beq	.L2211
	ldr	r0, .L2245
	mov	r6, #0
	ldr	r5, .L2245+16
.L2212:
	ldrh	ip, [r5]
	sxth	r2, r3
	cmp	r2, ip
	bge	.L2227
	ldr	lr, [r0, #-2924]
	mov	ip, r2, asl #1
	rsb	r4, r1, r2
	add	r3, r3, #1
	ldrh	r8, [lr, ip]
	mov	r7, r4, asl #1
	uxth	r3, r3
	strh	r8, [lr, r7]	@ movhi
	ldr	lr, [r0, #-2916]
	ldr	r2, [lr, r2, asl #2]
	str	r2, [lr, r4, asl #2]
	ldr	r2, [r0, #-2924]
	strh	r6, [r2, ip]	@ movhi
	b	.L2212
.L2227:
	mov	r0, #0
	add	sp, sp, #28
	@ sp needed
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L2246:
	.align	2
.L2245:
	.word	.LANCHOR2
	.word	.LANCHOR2-3258
	.word	.LANCHOR2-3268
	.word	.LANCHOR2-3188
	.word	.LANCHOR2-3180
	.word	.LANCHOR2-388
	.word	.LANCHOR2-2820
	.word	.LANCHOR2-404
	.word	.LANCHOR0
	.word	.LANCHOR2-2816
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
	ldr	r4, .L2250
	sub	r5, r4, #2960
	ldr	r2, [r4, #-2952]
	ldrh	r1, [r5]
	bl	FtlVendorPartRead
	ldr	r3, [r4, #-2952]
	ldr	r2, [r3]
	ldr	r3, .L2250+4
	cmp	r2, r3
	beq	.L2248
	ldr	r1, .L2250+8
	ldr	r0, .L2250+12
	bl	printk
	ldrh	r2, [r5]
	ldr	r0, [r4, #-2952]
	mov	r1, #0
	mov	r2, r2, asl #9
	bl	ftl_memset
.L2248:
	mov	r0, #0
	ldmfd	sp!, {r3, r4, r5, pc}
.L2251:
	.align	2
.L2250:
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
	ldr	r2, .L2256
	mov	r3, r0, lsr #5
	cmp	r1, #0
	and	r0, r0, #31
	uxth	r3, r3
	mov	ip, #1
	ldr	r1, [r2, #-2928]
	ldr	r2, [r1, r3, asl #2]
	orrne	r0, r2, ip, asl r0
	biceq	r0, r2, ip, asl r0
	str	r0, [r1, r3, asl #2]
	bx	lr
.L2257:
	.align	2
.L2256:
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
	ldr	r3, .L2259
	mov	r2, r0, lsr #5
	and	r0, r0, #31
	ldr	r3, [r3, #-2928]
	ldr	r3, [r3, r2, asl #2]
	mov	r0, r3, lsr r0
	and	r0, r0, #1
	bx	lr
.L2260:
	.align	2
.L2259:
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
	stmfd	sp!, {r0, r1, r4, r5, r6, r7, r8, r9, r10, lr}
	.save {r4, r5, r6, r7, r8, r9, r10, lr}
	.pad #8
	mov	r4, #0
	ldr	r6, .L2283
	ldr	r1, .L2283+4
	ldr	r0, .L2283+8
	ldr	r3, [r6]
	ldr	r5, .L2283+12
	str	r3, [sp, #4]
	bl	printk
	ldr	r0, .L2283+16
	mov	r1, #0
	mov	r2, #8192
	sub	r8, r5, #384
	bl	memset
.L2262:
	ldr	r3, [r5, #-2816]
	ldr	r7, .L2283+12
	cmp	r4, r3
	bcs	.L2281
	mov	r0, r4
	mov	r1, sp
	mov	r2, #0
	bl	log2phys
	ldr	r0, [sp]
	cmn	r0, #1
	beq	.L2263
	ubfx	r0, r0, #10, #16
	bl	P2V_block_in_plane
	mov	r0, r0, asl #1
	ldrh	r3, [r8, r0]
	add	r3, r3, #1
	strh	r3, [r8, r0]	@ movhi
.L2263:
	add	r4, r4, #1
	b	.L2262
.L2281:
	ldr	r5, .L2283+20
	sub	r9, r7, #384
	mov	r4, #0
	mov	r8, r7
.L2265:
	ldrh	r3, [r5]
	uxth	r1, r4
	ldr	r2, .L2283+12
	cmp	r3, r1
	bls	.L2282
	ldr	r2, [r8, #-2940]
	mov	r3, r1, asl #1
	ldrh	r2, [r2, r3]
	ldrh	r3, [r9, r3]
	cmp	r2, r3
	beq	.L2266
	ldr	r0, .L2283+24
	bl	printk
.L2266:
	add	r4, r4, #1
	b	.L2265
.L2282:
	ldr	r4, [r2, #-656]
	cmp	r4, #0
	beq	.L2261
	sub	r3, r2, #652
	sub	r9, r2, #384
	mov	r5, #0
	mov	r10, #6
	ldrh	r8, [r3]
	ldr	r3, [r2, #-2888]
	rsb	r4, r3, r4
	ldr	r3, .L2283+28
	mov	r4, r4, asr #1
	mul	r4, r3, r4
	uxth	r4, r4
.L2269:
	uxth	r3, r5
	cmp	r3, r8
	bcs	.L2261
	ldr	r2, [r7, #-2940]
	mov	r3, r4, asl #1
	ldrh	r2, [r2, r3]
	cmp	r2, #0
	beq	.L2270
	ldr	r0, .L2283+32
	mov	r1, r4
	ldrh	r3, [r9, r3]
	bl	printk
.L2270:
	mul	r4, r10, r4
	ldr	r3, [r7, #-2888]
	add	r5, r5, #1
	ldrh	r4, [r3, r4]
	movw	r3, #65535
	cmp	r4, r3
	bne	.L2269
.L2261:
	ldr	r2, [sp, #4]
	ldr	r3, [r6]
	cmp	r2, r3
	beq	.L2273
	bl	__stack_chk_fail
.L2273:
	add	sp, sp, #8
	@ sp needed
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, pc}
.L2284:
	.align	2
.L2283:
	.word	__stack_chk_guard
	.word	.LANCHOR3+16
	.word	.LC48
	.word	.LANCHOR2
	.word	.LANCHOR2-384
	.word	.LANCHOR2-3260
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
	ldr	r4, .L2294
	.pad #24
	sub	sp, sp, #24
	ldr	r8, .L2294+4
	mov	r6, r0
	mov	r5, #0
	ldr	r3, [r4, #-3008]
	str	r3, [r4, #-2800]
	ldr	r3, [r4, #-2976]
	str	r3, [r4, #-2796]
.L2286:
	ldrh	r2, [r8]
	sxth	r3, r5
	cmp	r3, r2
	bge	.L2293
	mov	r1, #1
	ldr	r0, .L2294+8
	mov	r2, r1
	orr	r3, r3, r7
	str	r3, [r4, #-2804]
	bl	FlashReadPages
	ldr	r3, [r4, #-2796]
	mov	r1, r6
	ldr	r0, .L2294+12
	ldr	r2, [r3]
	str	r2, [sp]
	ldr	r2, [r3, #4]
	str	r2, [sp, #4]
	ldr	r2, [r3, #8]
	str	r2, [sp, #8]
	ldr	r3, [r3, #12]
	ldr	r2, [r4, #-2808]
	str	r3, [sp, #12]
	ldr	r3, [r4, #-2800]
	ldr	r3, [r3]
	str	r3, [sp, #16]
	ldr	r3, [r4, #-2804]
	bl	printk
	ldr	r3, [r4, #-2796]
	ldr	r3, [r3]
	cmn	r3, #1
	beq	.L2287
	ldr	r0, .L2294+16
	mov	r2, #4
	ldr	r1, [r4, #-3008]
	mov	r3, #768
	bl	rknand_print_hex
.L2287:
	add	r5, r5, #1
	b	.L2286
.L2293:
	add	sp, sp, #24
	@ sp needed
	ldmfd	sp!, {r4, r5, r6, r7, r8, pc}
.L2295:
	.align	2
.L2294:
	.word	.LANCHOR2
	.word	.LANCHOR2-3198
	.word	.LANCHOR2-2808
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
	stmfd	sp!, {r4, r5, r6, r7, r8, lr}
	.save {r4, r5, r6, r7, r8, lr}
	.pad #32
	sub	sp, sp, #32
	ldr	r6, .L2307
	mov	r1, #0
	ldr	r0, .L2307+4
	mov	r5, #0
	ldr	r7, .L2307+8
	ldr	r3, [r6]
	mov	r4, r7
	str	r3, [sp, #28]
	bl	printk
.L2297:
	ldr	r3, [r7, #-2816]
	cmp	r5, r3
	bcs	.L2306
	mov	r0, r5
	add	r1, sp, #24
	mov	r2, #0
	bl	log2phys
	ubfx	r3, r5, #0, #11
	cmp	r3, #0
	bne	.L2298
	ldr	r0, .L2307+12
	mov	r1, r5
	ldr	r2, [sp, #24]
	bl	printk
.L2298:
	ldr	r3, [sp, #24]
	cmn	r3, #1
	beq	.L2300
	str	r3, [r4, #-2804]
	mov	r2, #0
	ldr	r3, [r4, #-3008]
	mov	r1, #1
	ldr	r8, [r4, #-2976]
	ldr	r0, .L2307+16
	str	r3, [r4, #-2800]
	str	r5, [r4, #-2792]
	str	r8, [r4, #-2796]
	str	r2, [r4, #-2808]
	bl	FlashReadPages
	ldr	r3, [r4, #-2808]
	cmn	r3, #1
	cmpne	r3, #256
	beq	.L2301
	ldr	r3, [r8, #8]
	cmp	r3, r5
	beq	.L2300
.L2301:
	ldr	r3, [r4, #-2796]
	ldr	r2, [r4, #-2800]
	ldr	r0, .L2307+20
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
	ldr	r2, [r4, #-2804]
	ldr	r3, [r3]
	bl	printk
.L2300:
	add	r5, r5, #1
	b	.L2297
.L2306:
	ldr	r2, [sp, #28]
	ldr	r3, [r6]
	cmp	r2, r3
	beq	.L2304
	bl	__stack_chk_fail
.L2304:
	add	sp, sp, #32
	@ sp needed
	ldmfd	sp!, {r4, r5, r6, r7, r8, pc}
.L2308:
	.align	2
.L2307:
	.word	__stack_chk_guard
	.word	.LC53
	.word	.LANCHOR2
	.word	.LC54
	.word	.LANCHOR2-2808
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
	ldr	r5, .L2327
	ldr	fp, .L2327+4
	ldrh	r6, [r5, #-12]
	add	r5, r5, #3248
	mov	r8, r5
.L2310:
	ldrh	r3, [fp]
	cmp	r3, r6
	bls	.L2323
	ldr	r1, .L2327+8
	mov	r7, #0
	ldr	r2, [r5, #-3036]
	mov	r4, r7
	ldr	r3, [r5, #-2996]
	add	r1, r1, #28
	ldr	r10, [r5, #-2968]
	ldrh	ip, [r1, #-28]
	ldrh	r9, [r1, #52]
	str	r1, [sp, #28]
.L2319:
	uxth	r1, r7
	cmp	r1, ip
	bcs	.L2325
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
	bne	.L2311
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
.L2311:
	add	r7, r7, #1
	b	.L2319
.L2325:
	cmp	r4, #0
	beq	.L2314
	ldr	r0, [r8, #-3036]
	mov	r1, r4
	mov	r2, #1
	mov	r7, #0
	bl	FlashReadPages
	mov	r9, #36
.L2315:
	uxth	r3, r7
	cmp	r3, r4
	bcs	.L2314
	ldr	r3, [r8, #-3036]
	ldr	r0, .L2327+12
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
	b	.L2315
.L2314:
	add	r6, r6, #1
	uxth	r6, r6
	b	.L2310
.L2323:
	ldr	r4, .L2327+16
	mov	r7, #0
	ldr	r8, .L2327+20
.L2318:
	ldr	r5, .L2327+16
	sxth	r6, r7
	sub	r3, r5, #388
	ldrh	r3, [r3]
	cmp	r6, r3
	bge	.L2321
	mov	r6, r6, asl #1
	mov	r5, #0
.L2322:
	ldrh	r2, [r8]
	sxth	r3, r5
	add	r5, r5, #1
	cmp	r3, r2
	bge	.L2326
	ldr	r2, [r4, #-2936]
	mov	r1, #1
	ldr	r0, .L2327+24
	ldrh	r2, [r2, r6]
	orr	r3, r3, r2, asl #10
	mov	r2, r1
	str	r3, [r4, #-2804]
	bl	FlashReadPages
	ldr	r3, [r4, #-2796]
	ldr	r1, [r4, #-2936]
	ldr	r2, [r4, #-2800]
	ldr	r0, [r3]
	ldrh	r1, [r1, r6]
	str	r0, [sp]
	ldr	r0, [r3, #4]
	str	r0, [sp, #4]
	ldr	r0, [r3, #8]
	str	r0, [sp, #8]
	ldr	r3, [r3, #12]
	ldr	r0, .L2327+28
	str	r3, [sp, #12]
	ldr	r3, [r2]
	str	r3, [sp, #16]
	ldr	r3, [r2, #4]
	str	r3, [sp, #20]
	ldr	r2, [r4, #-2808]
	ldr	r3, [r4, #-2804]
	bl	printk
	b	.L2322
.L2326:
	add	r7, r7, #1
	b	.L2318
.L2321:
	sub	r4, r5, #3152
	ldr	r1, [r5, #-2936]
	ldr	r3, [r5, #-3172]
	mov	r2, #2
	ldr	r0, .L2327+32
	bl	rknand_print_hex
	ldr	r1, [r5, #-2908]
	ldrh	r3, [r4, #-12]
	mov	r2, #4
	ldr	r0, .L2327+36
	bl	rknand_print_hex
	ldr	r0, .L2327+40
	ldr	r1, [r5, #-2904]
	mov	r2, #4
	ldrh	r3, [r4, #-12]
	add	sp, sp, #52
	@ sp needed
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, fp, lr}
	b	rknand_print_hex
.L2328:
	.align	2
.L2327:
	.word	.LANCHOR2-3248
	.word	.LANCHOR2-3258
	.word	.LANCHOR2-3268
	.word	.LC56
	.word	.LANCHOR2
	.word	.LANCHOR2-3198
	.word	.LANCHOR2-2808
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
	ldr	r5, .L2372
	str	r3, [sp, #8]
	bl	ftl_memset
	mov	r1, #0
	str	r1, [r4, #32]
	ldr	r3, [r5, #-3008]
	mov	r10, r5
	ldr	r6, [r5, #-2976]
	str	r1, [r4, #28]
	str	r3, [r5, #-2800]
	mvn	r3, #0
	str	r6, [r5, #-2796]
	strh	r3, [r4]	@ movhi
	strh	r3, [r4, #2]	@ movhi
	mov	r3, #1
	str	r3, [r4, #36]
	str	r1, [sp, #4]
.L2330:
	ldrh	r2, [sp, #4]
	ldr	r3, [sp, #8]
	sxth	fp, r2
	cmp	fp, r3
	bge	.L2348
	ldr	r3, [sp, #8]
	sub	r1, r3, #1
	cmp	fp, r1
	mov	r1, fp, asl #1
	bne	.L2331
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
	ldr	fp, .L2372
	str	r3, [r4, #28]
	add	r3, r0, #1
	str	r3, [sp, #4]
.L2332:
	ldr	r2, [sp, #4]
	sxth	r3, r8
	cmp	r3, r2
	bge	.L2348
	ldrh	r2, [r10]
	mov	r1, #1
	ldr	r0, .L2372+4
	orr	r3, r3, r2, asl #10
	mov	r2, r1
	str	r3, [r5, #-2804]
	bl	FlashReadPages
	ldr	r3, .L2372+8
	ldrb	r3, [r3]	@ zero_extendqisi2
	cmp	r3, #0
	beq	.L2333
	ldr	r3, [fp, #-2796]
	ldr	r3, [r3, #12]
	cmp	r3, #0
	beq	.L2333
	ldr	r2, .L2372+12
	ldr	r0, [fp, #-2800]
	str	r3, [sp, #8]
	ldrh	r1, [r2]
	bl	js_hash
	ldr	r3, [sp, #8]
	cmp	r3, r0
	mvnne	r3, #0
	strne	r3, [fp, #-2808]
.L2333:
	ldr	r3, [fp, #-2808]
	cmn	r3, #1
	beq	.L2334
	ldrh	r3, [r6, #8]
	cmp	r3, r9
	bcs	.L2334
	ldrh	r2, [r4, #4]
	ldrh	r1, [r6]
	cmp	r1, r2
	ldreq	r2, [fp, #-2804]
	streq	r2, [r7, r3, asl #2]
.L2334:
	add	r8, r8, #1
	b	.L2332
.L2348:
	mov	r0, r4
	bl	ftl_free_no_use_map_blk
	ldr	r3, .L2372+16
	ldrh	r2, [r4, #2]
	ldrh	r3, [r3, #-14]
	cmp	r2, r3
	bne	.L2337
	mov	r0, r4
	bl	ftl_map_blk_alloc_new_blk
	b	.L2337
.L2331:
	ldr	r2, [r5, #-3008]
	add	r3, r8, r1
	str	r3, [sp, #16]
	ldr	r3, .L2372+20
	str	r2, [r5, #-2800]
	ldrh	r1, [r8, r1]
	ldrh	r2, [r3]
	ldr	r0, .L2372+4
	sub	r2, r2, #1
	orr	r2, r2, r1, asl #10
	mov	r1, #1
	str	r2, [r5, #-2804]
	mov	r2, r1
	bl	FlashReadPages
	ldr	r2, [r5, #-2808]
	cmn	r2, #1
	beq	.L2350
	ldrh	r1, [r6]
	ldrh	r2, [r4, #4]
	cmp	r1, r2
	bne	.L2350
	ldrh	r1, [r6, #8]
	movw	r2, #64245
	cmp	r1, r2
	bne	.L2350
	mov	r0, #0
	mov	fp, #8
	mov	lr, #4
.L2339:
	ldr	r3, .L2372+20
	uxth	r2, r0
	sxth	r1, r2
	ldrh	ip, [r3]
	sub	ip, ip, #1
	cmp	r1, ip
	bge	.L2342
	ldr	ip, [r10, #-3008]
	add	r0, r0, #1
	ldr	r1, [ip, r1, asl #3]
	uxth	r1, r1
	cmp	r1, r9
	smlabbcc	r2, r2, fp, lr
	ldrcc	r2, [ip, r2]
	strcc	r2, [r7, r1, asl #2]
	b	.L2339
.L2350:
	mov	fp, #0
.L2370:
	ldr	r3, .L2372+20
	sxth	r2, fp
	ldrh	r1, [r3]
	cmp	r2, r1
	bge	.L2342
	ldr	r3, [sp, #16]
	ldr	r0, .L2372+4
	ldrh	r1, [r3]
	orr	r2, r2, r1, asl #10
	mov	r1, #1
	str	r2, [r10, #-2804]
	mov	r2, r1
	bl	FlashReadPages
	ldr	r2, .L2372+8
	ldrb	r2, [r2]	@ zero_extendqisi2
	cmp	r2, #0
	beq	.L2343
	ldr	r2, [r10, #-2796]
	ldr	r2, [r2, #12]
	cmp	r2, #0
	beq	.L2343
	ldr	r1, .L2372+12
	ldr	r0, [r10, #-2800]
	str	r2, [sp, #20]
	ldrh	r1, [r1]
	bl	js_hash
	ldr	r2, [sp, #20]
	cmp	r2, r0
	mvnne	r2, #0
	strne	r2, [r10, #-2808]
.L2343:
	ldr	r2, [r10, #-2808]
	cmn	r2, #1
	beq	.L2344
	ldrh	r2, [r6, #8]
	cmp	r2, r9
	bcs	.L2344
	ldrh	r1, [r4, #4]
	ldrh	r0, [r6]
	cmp	r0, r1
	ldreq	r1, [r10, #-2804]
	streq	r1, [r7, r2, asl #2]
.L2344:
	add	fp, fp, #1
	b	.L2370
.L2342:
	ldr	r3, [sp, #4]
	add	r3, r3, #1
	str	r3, [sp, #4]
	b	.L2330
.L2337:
	mov	r0, r4
	bl	ftl_map_blk_gc
	mov	r0, r4
	bl	ftl_map_blk_gc
	mov	r0, #0
	add	sp, sp, #28
	@ sp needed
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L2373:
	.align	2
.L2372:
	.word	.LANCHOR2
	.word	.LANCHOR2-2808
	.word	.LANCHOR0
	.word	.LANCHOR2-3190
	.word	.LANCHOR2-3184
	.word	.LANCHOR2-3198
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
	ldr	r3, .L2376
	sub	r2, r3, #3168
	sub	r0, r3, #456
	ldrh	r1, [r2, #-12]
	ldrh	r2, [r2, #-10]
	strh	r1, [r0, #10]	@ movhi
	strh	r2, [r0, #6]	@ movhi
	ldr	r2, [r3, #-2924]
	ldr	r1, .L2376+4
	str	r2, [r3, #-444]
	ldr	r2, [r3, #-2916]
	strh	r1, [r0, #4]	@ movhi
	sub	r1, r3, #2816
	str	r2, [r3, #-440]
	ldr	r2, [r3, #-2920]
	ldrh	r1, [r1, #-4]
	str	r2, [r3, #-436]
	ldr	r2, [r3, #-2912]
	strh	r1, [r0, #8]	@ movhi
	str	r2, [r3, #-432]
	bl	FtlMapTblRecovery
	mov	r0, #0
	ldmfd	sp!, {r3, pc}
.L2377:
	.align	2
.L2376:
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
	ldr	r4, .L2383
	mvn	r6, #0
	ldr	r2, [r4, #-3172]
	ldr	r0, [r4, #-2932]
	mov	r2, r2, asl #1
	bl	ftl_memset
	sub	r3, r4, #3184
	sub	r2, r4, #3152
	mov	r1, #255
	ldrh	r3, [r3, #-6]
	ldrh	r2, [r2, #-10]
	ldr	r0, [r4, #-2892]
	mul	r2, r2, r3
	bl	ftl_memset
	ldr	r0, .L2383+4
	mov	r1, #0
	mov	lr, #12
	sub	r7, r0, #28
	mov	r5, r1
.L2379:
	ldrh	r2, [r0]
	add	ip, r1, #1
	uxth	r1, r1
	ldr	r3, .L2383
	cmp	r2, r1
	sub	r8, r3, #3152
	bls	.L2382
	mul	r8, lr, r1
	ldr	r3, [r4, #-2896]
	add	r2, r3, r8
	str	r5, [r2, #4]
	strh	r6, [r3, r8]	@ movhi
	ldrh	r2, [r7]
	ldr	r3, [r4, #-2896]
	mul	r2, r1, r2
	add	r3, r3, r8
	ldr	r1, [r4, #-2892]
	bic	r2, r2, #3
	add	r2, r1, r2
	mov	r1, ip
	str	r2, [r3, #8]
	b	.L2379
.L2382:
	sub	r2, r3, #504
	mvn	r1, #0
	strh	r1, [r2, #2]	@ movhi
	strh	r1, [r2]	@ movhi
	ldr	r1, [r3, #-3172]
	strh	r1, [r2, #10]	@ movhi
	ldr	r1, .L2383+8
	strh	r1, [r2, #4]	@ movhi
	sub	r1, r3, #388
	ldrh	r1, [r1]
	strh	r1, [r2, #8]	@ movhi
	ldrh	r1, [r8, #-12]
	strh	r1, [r2, #6]	@ movhi
	ldr	r2, [r3, #-2936]
	str	r2, [r3, #-492]
	ldr	r2, [r3, #-2900]
	str	r2, [r3, #-488]
	ldr	r2, [r3, #-2932]
	str	r2, [r3, #-484]
	ldr	r2, [r3, #-2908]
	str	r2, [r3, #-480]
	ldmfd	sp!, {r4, r5, r6, r7, r8, pc}
.L2384:
	.align	2
.L2383:
	.word	.LANCHOR2
	.word	.LANCHOR2-3162
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
	ldr	r0, .L2387
	bl	FtlMapTblRecovery
	mov	r0, #0
	ldmfd	sp!, {r3, pc}
.L2388:
	.align	2
.L2387:
	.word	.LANCHOR2-504
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
	ldr	r3, .L2399
	stmfd	sp!, {r4, r5, lr}
	.save {r4, r5, lr}
	movw	r4, #65535
	ldrh	lr, [r3, #-4]
	add	r3, r2, #7
	add	r3, r0, r3, asl #1
.L2390:
	cmp	r2, lr
	bcs	.L2397
	ldrh	ip, [r3, #2]!
	add	r2, r2, #1
	cmp	ip, r4
	uxth	r2, r2
	ldrneh	ip, [r0, #4]
	addne	ip, ip, #1
	strneh	ip, [r0, #4]	@ movhi
	b	.L2390
.L2397:
	ldr	r3, .L2399+4
	add	ip, r0, #14
	mov	r2, #0
	movw	r5, #65535
	ldrh	r4, [r3]
.L2393:
	uxth	r3, r2
	cmp	r3, lr
	bcs	.L2398
	ldrh	r3, [ip, #2]!
	add	r2, r2, #1
	cmp	r3, r5
	ldrneh	r3, [r0, #4]
	addne	r3, r4, r3
	subne	r3, r3, #1
	rsbne	r3, r1, r3
	strneh	r3, [r0, #4]	@ movhi
	b	.L2393
.L2398:
	ldmfd	sp!, {r4, r5, pc}
.L2400:
	.align	2
.L2399:
	.word	.LANCHOR2-3264
	.word	.LANCHOR2-3200
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
	ldr	r6, .L2411
	ldr	r4, [r6, #-2940]
	mov	r7, r0, asl #1
	ldrh	r3, [r4, r7]
	cmp	r3, #0
	addne	r3, r3, #1
	strneh	r3, [r4, r7]	@ movhi
	bne	.L2403
	ldr	r4, [r6, #-656]
	cmp	r4, #0
	beq	.L2403
	ldr	r1, [r6, #-2888]
	sub	r2, r6, #652
	ldr	lr, .L2411+4
	movw	r9, #65535
	rsb	r4, r1, r4
	ldrh	ip, [r2]
	mov	r8, r2
	mov	r4, r4, asr #1
	mul	r4, lr, r4
	mov	lr, #6
	uxth	r4, r4
.L2404:
	uxth	r2, r3
	cmp	r2, ip
	bcs	.L2403
	cmp	r4, r0
	bne	.L2405
	mov	r1, r4
	ldr	r0, .L2411+8
	bl	List_remove_node
	ldrh	r3, [r8]
	mov	r0, r4
	sub	r3, r3, #1
	strh	r3, [r8]	@ movhi
	bl	INSERT_DATA_LIST
	ldr	r2, [r6, #-2940]
	ldrh	r3, [r2, r7]
	add	r3, r3, #1
	strh	r3, [r2, r7]	@ movhi
	b	.L2403
.L2405:
	mul	r4, lr, r4
	add	r3, r3, #1
	ldrh	r4, [r1, r4]
	cmp	r4, r9
	bne	.L2404
.L2403:
	mov	r0, r5
	add	r1, sp, #4
	mov	r2, #1
	bl	log2phys
	add	sp, sp, #12
	@ sp needed
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, pc}
.L2412:
	.align	2
.L2411:
	.word	.LANCHOR2
	.word	-1431655765
	.word	.LANCHOR2-656
	.fnend
	.size	FtlReUsePrevPpa, .-FtlReUsePrevPpa
	.align	2
	.global	make_superblock
	.type	make_superblock, %function
make_superblock:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	ldr	r3, .L2427
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
.L2414:
	uxth	r3, r5
	cmp	r3, r8
	bcs	.L2426
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
	b	.L2414
.L2426:
	ldr	r2, .L2427+4
	ldrb	r1, [r4, #7]	@ zero_extendqisi2
	sub	r3, r2, #3200
	ldrh	r3, [r3]
	smulbb	r3, r1, r3
	strh	r3, [r4, #4]	@ movhi
	mov	r3, #0
	strb	r3, [r4, #9]
	ldr	r3, [r2, #-3336]
	cmp	r3, #0
	beq	.L2417
	ldrh	r3, [r4]
	ldr	r2, [r2, #-2948]
	mov	r3, r3, asl #1
	ldrh	r3, [r2, r3]
	cmp	r3, #79
	movls	r3, #1
	strlsb	r3, [r4, #9]
.L2417:
	ldr	r3, .L2427+8
	mov	r0, #0
	ldrb	r3, [r3]	@ zero_extendqisi2
	cmp	r3, #0
	movne	r3, #1
	strneb	r3, [r4, #9]
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, pc}
.L2428:
	.align	2
.L2427:
	.word	.LANCHOR2-3264
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
	ldr	r5, .L2458
	.pad #44
	sub	sp, sp, #44
	sub	r7, r5, #3248
	sub	r6, r5, #404
	ldr	r3, [r5, #-3008]
	ldrh	r2, [r7, #-12]
	ldr	r0, [r5, #-2940]
	str	r3, [r5, #-2800]
	ldr	r3, [r5, #-2976]
	mov	r2, r2, asl #1
	str	r3, [r5, #-2796]
	bl	ftl_memset
	ldrh	r0, [r6]
	movw	r3, #65535
	cmp	r0, r3
	str	r7, [sp, #28]
	bne	.L2430
.L2441:
	mvn	r0, #0
	b	.L2431
.L2430:
	mov	r1, #1
	mov	r4, r5
	bl	FtlGetLastWrittenPage
	ldrh	r9, [r6]
	mov	r7, r4
	uxth	r5, r0
	add	r0, r0, #1
	strh	r0, [r6, #2]	@ movhi
.L2432:
	sxth	r3, r5
	cmp	r3, #0
	blt	.L2440
	sxth	fp, r9
	mov	r1, #1
	ldr	r0, .L2458+4
	mov	r2, r1
	orr	r3, r3, fp, asl #10
	str	r3, [r4, #-2804]
	ldr	r3, [r4, #-3008]
	str	r3, [r4, #-2800]
	bl	FlashReadPages
	ldr	r3, .L2458+8
	ldrb	r3, [r3]	@ zero_extendqisi2
	cmp	r3, #0
	beq	.L2433
	ldr	r8, [r7, #-2796]
	ldr	r3, [r8, #12]
	cmp	r3, #0
	beq	.L2433
	ldr	r2, [r7, #-2800]
	ldr	r10, .L2458+12
	str	r3, [sp, #36]
	mov	r0, r2
	str	r2, [sp, #32]
	ldrh	r1, [r10]
	bl	js_hash
	ldr	r3, [sp, #36]
	cmp	r3, r0
	beq	.L2433
	cmp	r5, #0
	ldr	r2, [sp, #32]
	bne	.L2434
	ldrh	r1, [r6, #4]
	ldr	ip, .L2458+16
	cmp	fp, r1
	beq	.L2434
	ldr	r0, [r8]
	ldrh	r1, [ip]
	str	ip, [sp, #32]
	str	r0, [sp]
	ldr	r0, [r8, #4]
	str	r0, [sp, #4]
	ldr	r0, [r8, #8]
	str	r3, [sp, #12]
	str	r0, [sp, #8]
	ldr	r3, [r2]
	ldr	r0, .L2458+20
	str	r3, [sp, #16]
	ldr	r2, [r7, #-2808]
	ldr	r3, [r7, #-2804]
	bl	printk
	ldrh	r5, [r10, #-8]
	ldr	ip, [sp, #32]
	ldrh	r9, [ip, #4]
	b	.L2436
.L2434:
	mvn	r3, #0
	str	r3, [r4, #-2808]
.L2433:
	ldr	r3, [r4, #-2808]
	cmn	r3, #1
	beq	.L2436
	ldr	r3, [r7, #-3008]
	ldr	r2, .L2458+24
	ldr	r3, [r3]
	cmp	r3, r2
	bne	.L2436
	ldr	r3, [r7, #-2976]
	ldrh	r2, [r3]
	movw	r3, #61604
	cmp	r2, r3
	bne	.L2436
.L2440:
	ldr	r5, .L2458
	mov	r2, #48
	ldr	r1, [r4, #-2800]
	sub	r0, r5, #716
	sub	r7, r5, #3248
	bl	ftl_memcpy
	ldr	r1, [r4, #-2800]
	ldr	r0, [r4, #-2940]
	add	r1, r1, #48
	ldr	r3, [sp, #28]
	ldrh	r2, [r3, #-12]
	mov	r2, r2, asl #1
	bl	ftl_memcpy
	ldr	r1, [r4, #-2800]
	ldr	r0, [r4, #-2928]
	ldr	r3, [sp, #28]
	ldrh	r2, [r3, #-12]
	add	r3, r2, #24
	ubfx	r3, r3, #1, #30
	mov	r2, r2, lsr #3
	add	r2, r2, #4
	add	r1, r1, r3, asl #2
	bl	ftl_memcpy
	sub	r2, r5, #3152
	ldrh	r3, [r2, #-8]
	cmp	r3, #0
	beq	.L2438
	ldrh	r1, [r7, #-12]
	ldrh	r2, [r2, #-12]
	ldr	r0, [r5, #-2904]
	mov	r3, r1, lsr #3
	add	r3, r3, r1, asl #1
	ldr	r1, [r5, #-2800]
	add	r3, r3, #52
	mov	r2, r2, asl #2
	ubfx	r3, r3, #2, #14
	add	r1, r1, r3, asl #2
	bl	ftl_memcpy
	b	.L2438
.L2436:
	sub	r5, r5, #1
	uxth	r5, r5
	b	.L2432
.L2438:
	ldr	r2, [r4, #-716]
	ldr	r3, .L2458+24
	ldr	r5, .L2458
	cmp	r2, r3
	bne	.L2441
	sub	r2, r5, #3232
	sub	r7, r5, #716
	ldrb	r1, [r5, #-706]	@ zero_extendqisi2
	ldrh	r2, [r2, #-14]
	ldrh	r3, [r7, #8]
	cmp	r1, r2
	strh	r3, [r6, #6]	@ movhi
	bne	.L2441
	sub	r2, r5, #3200
	sub	r1, r5, #3184
	ldr	r6, .L2458+28
	ldrh	r2, [r2]
	ldrh	r1, [r1, #-10]
	sub	r8, r6, #372
	str	r3, [r6, #-376]
	mul	r2, r3, r2
	str	r2, [r5, #-2816]
	mul	r2, r1, r2
	ldr	r1, .L2458+8
	str	r2, [r1, #3908]
	sub	r2, r5, #2880
	ldrh	r0, [r2, #6]
	ldr	r2, [r5, #-3256]
	rsb	r0, r0, r2
	sub	r2, r5, #3264
	rsb	r0, r3, r0
	ldrh	r1, [r2, #-4]
	bl	__aeabi_uidiv
	ldrh	r3, [r7, #16]
	ldrh	ip, [r7, #14]
	sub	r2, r5, #648
	sub	lr, r6, #368
	mov	r1, r3, lsr #6
	strh	r1, [r2, #2]	@ movhi
	ldrh	r1, [r7, #18]
	and	r3, r3, #63
	strh	ip, [r2]	@ movhi
	sub	r2, r5, #600
	strb	r3, [r5, #-642]
	strh	r1, [r2]	@ movhi
	ldrh	r1, [r7, #20]
	ldrb	r3, [r5, #-705]	@ zero_extendqisi2
	strb	r3, [r5, #-640]
	mvn	r3, #0
	strh	r3, [lr]	@ movhi
	mov	r3, #0
	strb	r3, [r6, #-362]
	strb	r3, [r6, #-360]
	strh	r3, [lr, #2]	@ movhi
	str	r3, [r5, #-3140]
	strh	r0, [r8]	@ movhi
	mov	r0, r1, lsr #6
	and	r1, r1, #63
	strb	r1, [r5, #-594]
	ldrb	r1, [r5, #-704]	@ zero_extendqisi2
	mov	r8, lr
	strh	r0, [r2, #2]	@ movhi
	sub	r0, r5, #552
	strb	r1, [r5, #-592]
	ldrh	r1, [r7, #22]
	strh	r1, [r0]	@ movhi
	ldrh	r1, [r7, #24]
	mov	r7, r2
	mov	r6, r1, lsr #6
	and	r1, r1, #63
	strb	r1, [r5, #-546]
	ldrb	r1, [r5, #-703]	@ zero_extendqisi2
	strh	r6, [r0, #2]	@ movhi
	mov	r6, r0
	strb	r1, [r5, #-544]
	str	r3, [r5, #-3136]
	ldr	r1, [r5, #-684]
	str	r3, [r5, #-3120]
	str	r3, [r5, #-3124]
	str	r3, [r5, #-3112]
	str	r3, [r5, #-3104]
	str	r3, [r5, #-3128]
	ldr	r3, [r5, #-676]
	str	r1, [r5, #-3116]
	ldr	r1, [r5, #-3148]
	ldr	r2, [r4, #-3144]
	cmp	r3, r1
	strhi	r3, [r5, #-3148]
	ldr	r3, [r4, #-680]
	cmp	r3, r2
	ldrhi	r2, .L2458
	strhi	r3, [r2, #-3144]
	movw	r3, #65535
	cmp	ip, r3
	beq	.L2444
	ldr	r0, .L2458+32
	bl	make_superblock
.L2444:
	ldrh	r2, [r7]
	movw	r3, #65535
	cmp	r2, r3
	beq	.L2445
	ldr	r0, .L2458+36
	bl	make_superblock
.L2445:
	ldrh	r2, [r6]
	movw	r3, #65535
	cmp	r2, r3
	beq	.L2446
	ldr	r0, .L2458+40
	bl	make_superblock
.L2446:
	ldrh	r2, [r8]
	movw	r3, #65535
	cmp	r2, r3
	beq	.L2447
	ldr	r0, .L2458+44
	bl	make_superblock
.L2447:
	mov	r0, #0
.L2431:
	add	sp, sp, #44
	@ sp needed
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L2459:
	.align	2
.L2458:
	.word	.LANCHOR2
	.word	.LANCHOR2-2808
	.word	.LANCHOR0
	.word	.LANCHOR2-3190
	.word	.LANCHOR2-404
	.word	.LC61
	.word	1179929683
	.word	.LANCHOR4
	.word	.LANCHOR2-648
	.word	.LANCHOR2-600
	.word	.LANCHOR2-552
	.word	.LANCHOR4-368
	.fnend
	.size	FtlLoadSysInfo, .-FtlLoadSysInfo
	.align	2
	.global	FtlDumpBlockInfo
	.type	FtlDumpBlockInfo, %function
FtlDumpBlockInfo:
	.fnstart
	@ args = 0, pretend = 0, frame = 80
	@ frame_needed = 0, uses_anonymous_args = 0
	ldr	r3, .L2475
	ubfx	r0, r0, #10, #16
	stmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.save {r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.pad #108
	sub	sp, sp, #108
	ldr	r3, [r3]
	mov	r4, r1
	ldr	r7, .L2475+4
	str	r3, [sp, #100]
	bl	P2V_block_in_plane
	sub	r5, r7, #3200
	ldr	r1, .L2475+8
	ldrh	r9, [r5]
	mov	r6, r0
	ldr	r0, .L2475+12
	bl	printk
	ldr	r2, [r7, #-2940]
	mov	r3, r6, asl #1
	mov	r1, r6
	ldr	r0, .L2475+16
	ldrh	r2, [r2, r3]
	bl	printk
	add	r0, sp, #104
	strh	r6, [r0, #-52]!	@ movhi
	bl	make_superblock
	ldr	r3, .L2475+20
	ldrb	r2, [r3, #3904]	@ zero_extendqisi2
	clz	r3, r4
	cmp	r2, #0
	mov	r3, r3, lsr #5
	moveq	r3, #0
	cmp	r3, #0
	moveq	r4, r3
	beq	.L2461
	mov	r0, r6
	bl	ftl_get_blk_mode
	cmp	r0, #1
	mov	r4, r0
	subeq	r3, r7, #3184
	ldreqh	r9, [r3, #-14]
.L2461:
	ldr	r0, .L2475+24
	mov	r1, r4
	mov	r2, r9
	ldrh	r3, [r5]
	bl	printk
	ldr	r8, .L2475+4
	mov	r6, #0
.L2462:
	ldr	r3, .L2475+28
	mov	r2, #0
	add	r0, sp, #66
	mov	r5, r2
	movw	lr, #65535
	mov	r10, #36
	ldrh	r3, [r3]
	str	r3, [sp, #28]
	ldr	r3, [r8, #-3036]
	str	r3, [sp, #32]
	ldr	r3, [r8, #-2996]
	str	r3, [sp, #36]
	ldr	r3, .L2475+32
	ldrh	r3, [r3]
	str	r3, [sp, #40]
	ldr	r3, [r8, #-2968]
	str	r3, [sp, #44]
	ldr	r3, .L2475+36
	ldrh	ip, [r3]
.L2463:
	ldr	r1, [sp, #28]
	uxth	r3, r2
	cmp	r3, r1
	bcs	.L2473
	ldrh	r3, [r0, #2]!
	cmp	r3, lr
	beq	.L2464
	ldr	r1, [sp, #32]
	orr	r3, r6, r3, asl #10
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
.L2464:
	add	r2, r2, #1
	b	.L2463
.L2473:
	ldr	r0, [r8, #-3036]
	mov	r1, r5
	mov	r2, r4
	mov	r10, #0
	bl	FlashReadPages
.L2466:
	uxth	r3, r10
	cmp	r3, r5
	bcs	.L2474
	mov	r3, #36
	ldr	lr, [r7, #-3036]
	mul	r2, r3, r10
	ldrh	r1, [sp, #52]
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
	ldr	r0, .L2475+40
	str	r3, [sp, #20]
	ldr	r2, [lr, r2]
	ldr	r3, [ip, #4]
	bl	printk
	b	.L2466
.L2474:
	add	r6, r6, #1
	uxth	r6, r6
	cmp	r6, r9
	bne	.L2462
.L2468:
	ldr	r3, .L2475
	mov	r0, #0
	ldr	r2, [sp, #100]
	ldr	r3, [r3]
	cmp	r2, r3
	beq	.L2469
	bl	__stack_chk_fail
.L2469:
	add	sp, sp, #108
	@ sp needed
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L2476:
	.align	2
.L2475:
	.word	__stack_chk_guard
	.word	.LANCHOR2
	.word	.LANCHOR3+28
	.word	.LC48
	.word	.LC62
	.word	.LANCHOR0
	.word	.LC63
	.word	.LANCHOR2-3268
	.word	.LANCHOR2-3190
	.word	.LANCHOR2-3188
	.word	.LC57
	.fnend
	.size	FtlDumpBlockInfo, .-FtlDumpBlockInfo
	.align	2
	.global	FtlScanAllBlock
	.type	FtlScanAllBlock, %function
FtlScanAllBlock:
	.fnstart
	@ args = 0, pretend = 0, frame = 72
	@ frame_needed = 0, uses_anonymous_args = 0
	stmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.save {r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.pad #100
	sub	sp, sp, #100
	ldr	r5, .L2493
	mov	r6, #0
	ldr	r0, .L2493+4
	ldr	r1, .L2493+8
	ldr	r3, [r5]
	str	r3, [sp, #92]
	bl	printk
	str	r5, [sp, #36]
	ldr	r5, .L2493+12
	mov	r7, r5
.L2478:
	ldr	r3, .L2493+16
	uxth	r4, r6
	ldrh	r3, [r3]
	cmp	r3, r4
	bls	.L2489
	add	r8, sp, #96
	mov	r0, r4
	movw	r9, #65535
	mov	r10, #36
	strh	r4, [r8, #-52]!	@ movhi
	bl	ftl_get_blk_mode
	ldr	ip, [r5, #-2940]
	mov	r2, r4, asl #1
	mov	r1, r4
	ldrh	r2, [ip, r2]
	mov	r3, r0
	ldr	r0, .L2493+20
	bl	printk
	mov	r0, r8
	bl	make_superblock
	ldr	r3, .L2493+24
	ldr	lr, [r5, #-2968]
	ldrh	r2, [r3]
	ldrh	ip, [r3, #78]
	ldrh	r8, [r3, #80]
	str	r2, [sp, #24]
	add	r0, sp, #58
	ldr	r2, [r5, #-3036]
	str	r2, [sp, #28]
	ldr	r2, [r5, #-2996]
	str	r2, [sp, #32]
	mov	r2, #0
	mov	r4, r2
.L2479:
	ldr	r1, [sp, #24]
	uxth	r3, r2
	cmp	r3, r1
	bcs	.L2490
	ldrh	r3, [r0, #2]!
	cmp	r3, r9
	beq	.L2480
	ldr	r1, [sp, #28]
	mov	r3, r3, asl #10
	mla	r1, r10, r4, r1
	str	r3, [r1, #4]
	mul	r3, ip, r4
	add	fp, r3, #3
	cmp	r3, #0
	movlt	r3, fp
	ldr	fp, [sp, #32]
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
.L2480:
	add	r2, r2, #1
	b	.L2479
.L2490:
	ldr	r0, [r5, #-3036]
	mov	r1, r4
	mov	r2, #0
	mov	r8, #0
	bl	FlashReadPages
	mov	r9, #36
.L2482:
	uxth	r3, r8
	cmp	r3, r4
	bcs	.L2491
	mul	r2, r9, r8
	ldr	lr, [r7, #-3036]
	ldrh	r1, [sp, #44]
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
	ldr	r0, .L2493+28
	str	r3, [sp, #20]
	ldr	r2, [lr, r2]
	ldr	r3, [ip, #4]
	bl	printk
	b	.L2482
.L2491:
	ldr	r0, [r7, #-3036]
	mov	r1, r4
	mov	r2, #1
	mov	r8, #0
	bl	FlashReadPages
	mov	r9, #36
.L2484:
	uxth	r3, r8
	cmp	r3, r4
	bcs	.L2492
	mul	r2, r9, r8
	ldr	lr, [r7, #-3036]
	ldrh	r1, [sp, #44]
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
	ldr	r0, .L2493+32
	str	r3, [sp, #20]
	ldr	r2, [lr, r2]
	ldr	r3, [ip, #4]
	bl	printk
	b	.L2484
.L2492:
	add	r6, r6, #1
	b	.L2478
.L2489:
	ldr	r3, [sp, #36]
	mov	r0, #0
	ldr	r2, [sp, #92]
	ldr	r3, [r3]
	cmp	r2, r3
	beq	.L2487
	bl	__stack_chk_fail
.L2487:
	add	sp, sp, #100
	@ sp needed
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L2494:
	.align	2
.L2493:
	.word	__stack_chk_guard
	.word	.LC48
	.word	.LANCHOR3+48
	.word	.LANCHOR2
	.word	.LANCHOR2-3258
	.word	.LC64
	.word	.LANCHOR2-3268
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
	ldr	r6, .L2507
	mov	r1, #0
	.pad #28
	sub	sp, sp, #28
	sub	r3, r6, #3248
	ldr	r0, [r6, #-2888]
	mov	r5, r6
	ldrh	r3, [r3, #-10]
	mov	fp, r6
	mul	r2, r2, r3
	bl	ftl_memset
	mov	r3, #0
	sub	r1, r6, #660
	sub	r2, r6, #652
	sub	r0, r6, #3152
	mov	r4, r3
	mov	r8, r3
	mov	r9, r3
	str	r3, [r6, #-656]
	str	r3, [r6, #-668]
	str	r3, [r6, #-664]
	strh	r3, [r1]	@ movhi
	strh	r3, [r2]	@ movhi
	strh	r3, [r0, #-6]	@ movhi
	str	r1, [sp, #8]
	str	r2, [sp, #12]
.L2496:
	ldr	r3, .L2507+4
	uxth	r7, r4
	sxth	r10, r7
	ldrh	r2, [r3]
	cmp	r10, r2
	bge	.L2503
	sub	r3, r3, #8
	ldr	r2, .L2507+8
	mov	ip, r7
	ldrh	r3, [r3]
	str	r3, [sp]
	ldrh	r3, [r2]
	mov	r2, #0
	mov	r6, r2
	str	r3, [sp, #4]
.L2504:
	ldr	r3, [sp]
	sxth	r1, r2
	cmp	r1, r3
	bge	.L2506
	add	r1, r5, r1
	str	r2, [sp, #20]
	str	ip, [sp, #16]
	ldrb	r0, [r1, #-3240]	@ zero_extendqisi2
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
	b	.L2504
.L2506:
	cmp	r6, #0
	beq	.L2499
	sxth	r1, r6
	mov	r0, #32768
	bl	__aeabi_idiv
	uxth	r6, r0
	b	.L2500
.L2499:
	sxth	r7, r7
	ldr	r2, [r5, #-2940]
	mvn	r1, #0
	mov	r7, r7, asl #1
	strh	r1, [r2, r7]	@ movhi
.L2500:
	mov	r1, r10, asl #1
	ldr	r2, [r5, #-2888]
	add	r0, r1, r10
	add	r2, r2, r0, asl #1
	strh	r6, [r2, #4]	@ movhi
	ldr	r2, .L2507+12
	ldrh	r0, [r2]
	cmp	r10, r0
	beq	.L2501
	ldrh	r0, [r2, #48]
	cmp	r10, r0
	beq	.L2501
	ldrh	r2, [r2, #96]
	cmp	r10, r2
	beq	.L2501
	ldr	r3, [fp, #-2940]
	uxth	r0, r4
	ldrh	r3, [r3, r1]
	cmp	r3, #0
	bne	.L2502
	add	r8, r8, #1
	uxth	r8, r8
	bl	INSERT_FREE_LIST
	b	.L2501
.L2502:
	add	r9, r9, #1
	uxth	r9, r9
	bl	INSERT_DATA_LIST
.L2501:
	add	r4, r4, #1
	b	.L2496
.L2503:
	ldr	r3, [sp, #8]
	mov	r0, #0
	strh	r9, [r3]	@ movhi
	ldr	r3, [sp, #12]
	strh	r8, [r3]	@ movhi
	add	sp, sp, #28
	@ sp needed
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L2508:
	.align	2
.L2507:
	.word	.LANCHOR2
	.word	.LANCHOR2-3260
	.word	.LANCHOR2-3200
	.word	.LANCHOR2-648
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
	ldr	r2, .L2511
	ldr	r3, .L2511+4
	ldr	r1, [r2, #-320]
	cmp	r1, r3
	bxne	lr
	ldr	r3, .L2511+8
	mov	r0, #0
	sub	r2, r2, #320
	str	r3, [r2, #4]
	ldr	r3, [r2, #512]
	str	r3, [r2, #88]
	ldr	r3, [r2, #516]
	str	r3, [r2, #92]
	ldr	r3, .L2511+12
	ldr	r1, [r3, #-3140]
	str	r1, [r2, #8]
	ldr	r1, [r3, #-3136]
	str	r1, [r2, #12]
	ldr	r1, [r3, #-3120]
	str	r1, [r2, #16]
	ldr	r1, [r3, #-3124]
	str	r1, [r2, #20]
	ldr	r1, [r3, #-3112]
	str	r1, [r2, #28]
	ldr	r1, [r3, #-3108]
	str	r1, [r2, #32]
	ldr	r1, [r3, #-3132]
	str	r1, [r2, #36]
	ldr	r1, [r3, #-3128]
	str	r1, [r2, #40]
	ldr	r1, [r3, #-3104]
	str	r1, [r2, #44]
	ldr	r1, [r3, #-3100]
	str	r1, [r2, #48]
	ldr	r1, [r3, #-3152]
	ldr	r3, [r3, #-3156]
	str	r1, [r2, #60]
	mov	r1, #1
	str	r3, [r2, #64]
	b	FtlVendorPartWrite
.L2512:
	.align	2
.L2511:
	.word	.LANCHOR4
	.word	1179929683
	.word	1342177367
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
	ldr	r3, .L2521
	ldr	r2, [r3, #-3336]
	cmp	r2, #0
	moveq	r2, #32
	beq	.L2514
	ldr	r2, [r3, #-3100]
	cmp	r2, #39
	movhi	r2, #32
	movls	r2, #4
.L2514:
	ldr	ip, .L2521+4
	ldrh	r1, [ip, #200]
	cmp	r1, #31
	addls	r1, r1, #1
	strlsh	r1, [ip, #200]	@ movhi
	movls	r2, #1
	cmp	r0, #0
	bne	.L2516
	ldr	r1, [r3, #-2952]
	ldr	r0, [r1, #20]
	ldr	r1, [r1, #16]
	add	r2, r2, r0
	cmp	r1, r2
	bcc	.L2517
.L2516:
	ldr	r2, [r3, #-2952]
	mov	r0, #64
	ldr	r1, [r2, #16]
	str	r1, [r2, #20]
	ldr	r1, .L2521+8
	str	r1, [r2]
	ldr	r2, [r3, #-2952]
	ldr	r3, .L2521+12
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
.L2517:
	mov	r0, #0
	ldmfd	sp!, {r3, pc}
.L2522:
	.align	2
.L2521:
	.word	.LANCHOR2
	.word	.LANCHOR4
	.word	1112818501
	.word	.LANCHOR2-2960
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
	ldr	r5, .L2528
	mov	r1, #1
	ldr	r4, .L2528+4
	sub	r2, r5, #320
	bl	FtlVendorPartRead
	ldr	r3, [r5, #-320]
	cmp	r3, r4
	beq	.L2524
	sub	r0, r5, #320
	mov	r1, #0
	mov	r2, #512
	bl	ftl_memset
	str	r4, [r5, #-320]
.L2524:
	ldr	r2, [r5, #-320]
	ldr	r3, .L2528
	cmp	r2, r4
	ldr	r4, .L2528+8
	bne	.L2525
	ldr	r2, [r3, #-232]
	str	r2, [r3, #192]
	ldr	r2, [r3, #-228]
	str	r2, [r3, #196]
	ldr	r2, [r3, #-312]
	str	r2, [r4, #-3140]
	ldr	r2, [r3, #-308]
	str	r2, [r4, #-3136]
	ldr	r2, [r3, #-304]
	str	r2, [r4, #-3120]
	ldr	r2, [r3, #-300]
	str	r2, [r4, #-3124]
	ldr	r2, [r3, #-292]
	str	r2, [r4, #-3112]
	ldr	r2, [r3, #-288]
	str	r2, [r4, #-3108]
	ldr	r2, [r3, #-284]
	str	r2, [r4, #-3132]
	ldr	r2, [r3, #-280]
	str	r2, [r4, #-3128]
	ldr	r2, [r3, #-276]
	str	r2, [r4, #-3104]
	ldr	r2, [r3, #-272]
	ldr	r3, [r3, #-260]
	str	r2, [r4, #-3100]
	str	r3, [r4, #-3152]
.L2525:
	mov	r3, #0
	ldr	r2, [r5, #-252]
	str	r3, [r4, #-3156]
	ldr	r3, .L2528+12
	cmp	r2, r3
	bne	.L2526
	ldr	r3, .L2528+8
	mov	r2, #1
	ldr	r0, .L2528+16
	ldr	r1, .L2528+20
	str	r2, [r3, #-3336]
	bl	printk
.L2526:
	ldr	r3, .L2528+24
	ldr	r2, [r4, #-3112]
	ldr	r0, [r4, #-3116]
	ldrh	r1, [r3, #-8]
	mla	r0, r0, r1, r2
	ldrh	r1, [r3, #-60]
	bl	__aeabi_uidiv
	str	r0, [r5, #204]
	ldmfd	sp!, {r3, r4, r5, pc}
.L2529:
	.align	2
.L2528:
	.word	.LANCHOR4
	.word	1179929683
	.word	.LANCHOR2
	.word	305432421
	.word	.LC46
	.word	.LC67
	.word	.LANCHOR2-3200
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
	ldr	r5, .L2537
	mov	r7, #0
	ldr	r0, .L2537+4
	sub	r9, r5, #2
	add	r4, r5, #3200
	ldrh	r1, [r5, #-2]
	ldrh	r2, [r5]
	bl	printk
.L2531:
	ldrh	r3, [r9]
	cmp	r7, r3
	bcs	.L2533
	mov	r8, r7, asl #10
	mov	r6, #0
.L2534:
	ldrh	r3, [r5]
	cmp	r6, r3
	bcs	.L2536
	add	r3, r6, r8
	str	r3, [r4, #-2804]
	ldr	r3, [r4, #-3008]
	mov	r2, #0
	mov	r1, #1
	ldr	r0, .L2537+8
	str	r2, [r4, #-2808]
	add	r6, r6, #1
	str	r3, [r4, #-2800]
	ldr	r3, [r4, #-2976]
	str	r3, [r4, #-2796]
	bl	FlashReadPages
	ldr	r3, [r4, #-2796]
	ldr	r2, [r4, #-2800]
	ldr	r0, .L2537+12
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
	ldr	r1, [r4, #-2804]
	ldr	r2, [r4, #-2808]
	ldr	r3, [r3]
	bl	printk
	b	.L2534
.L2536:
	add	r7, r7, #1
	b	.L2531
.L2533:
	ldr	r0, .L2537+16
	ldr	r1, .L2537+20
	add	sp, sp, #28
	@ sp needed
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, lr}
	b	printk
.L2538:
	.align	2
.L2537:
	.word	.LANCHOR2-3200
	.word	.LC68
	.word	.LANCHOR2-2808
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
	ldr	r3, .L2549
	mov	r7, r0
	mov	r4, r5
	add	r6, r3, #24
	ldrh	r8, [r3, #-4]
	ldrh	r9, [r3, #64]
.L2540:
	uxth	r3, r5
	cmp	r3, r8
	bcs	.L2548
	ldrb	r0, [r6, r5]	@ zero_extendqisi2
	mov	r1, r7
	bl	V2P_block
	add	r5, r5, #1
	bl	FtlBbmIsBadBlock
	cmp	r0, #0
	addeq	r4, r4, r9
	uxtheq	r4, r4
	b	.L2540
.L2548:
	cmp	r4, #0
	beq	.L2543
	mov	r1, r4
	mov	r0, #32768
	bl	__aeabi_idiv
	uxth	r4, r0
.L2543:
	ldr	r3, .L2549+4
	mov	r2, #6
	mov	r0, #0
	ldr	r3, [r3, #-2888]
	mla	r7, r2, r7, r3
	strh	r4, [r7, #4]	@ movhi
	ldmfd	sp!, {r3, r4, r5, r6, r7, r8, r9, pc}
.L2550:
	.align	2
.L2549:
	.word	.LANCHOR2-3264
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
	ldr	r2, .L2554
	ldr	r0, [r2, #-656]
	cmp	r0, #0
	bxeq	lr
	ldr	r3, [r2, #-2888]
	rsb	r0, r3, r0
	ldr	r3, .L2554+4
	mov	r0, r0, asr #1
	mul	r0, r3, r0
	ldr	r3, [r2, #-2948]
	uxth	r0, r0
	mov	r0, r0, asl #1
	ldrh	r0, [r3, r0]
	bx	lr
.L2555:
	.align	2
.L2554:
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
	ldr	r1, .L2566
	ldr	r3, [r1, #-656]
	cmp	r3, #0
	beq	.L2562
	sub	r2, r1, #652
	stmfd	sp!, {r4, r5, lr}
	.save {r4, r5, lr}
	mov	r4, #6
	ldrh	r2, [r2]
	movw	r5, #65535
	ldr	ip, [r1, #-2888]
	rsb	r2, r2, r2, asl #3
	rsb	r3, ip, r3
	mov	r2, r2, asr #3
	mov	r3, r3, asr #1
	cmp	r0, r2
	uxthgt	r0, r2
	ldr	r2, .L2566+4
	mul	r3, r2, r3
	mov	r2, #0
	uxth	r3, r3
.L2559:
	uxth	lr, r2
	cmp	lr, r0
	bcs	.L2561
	mul	lr, r4, r3
	add	r2, r2, #1
	ldrh	lr, [ip, lr]
	cmp	lr, r5
	bne	.L2563
.L2561:
	ldr	r2, [r1, #-2948]
	mov	r3, r3, asl #1
	ldrh	r0, [r2, r3]
	ldmfd	sp!, {r4, r5, pc}
.L2563:
	mov	r3, lr
	b	.L2559
.L2562:
	mov	r0, r3
	bx	lr
.L2567:
	.align	2
.L2566:
	.word	.LANCHOR2
	.word	-1431655765
	.fnend
	.size	GetFreeBlockMaxEraseCount, .-GetFreeBlockMaxEraseCount
	.align	2
	.global	FtlPrintInfo2buf
	.type	FtlPrintInfo2buf, %function
FtlPrintInfo2buf:
	.fnstart
	@ args = 0, pretend = 0, frame = 32
	@ frame_needed = 0, uses_anonymous_args = 0
	stmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.save {r4, r5, r6, r7, r8, r9, r10, fp, lr}
	mov	r8, r0
	ldr	r7, .L2580
	add	r6, r8, #12
	ldr	r9, .L2580+4
	.pad #52
	sub	sp, sp, #52
	ldr	r1, .L2580+8
	ldr	r3, [r7]
	str	r3, [sp, #44]
	bl	strcpy
	mov	r0, r6
	ldr	r1, .L2580+12
	ldr	r2, [r9, #3876]
	bl	sprintf
	ldr	r1, .L2580+16
	add	r5, r6, r0
	ldr	r6, .L2580+20
	mov	r0, r5
	ldr	r2, [r6, #-3184]
	bl	sprintf
	ldr	r3, .L2580+24
	ldr	r3, [r3, #3284]
	cmp	r3, #1
	add	r4, r5, r0
	bne	.L2574
	add	r0, sp, #28
	add	r1, sp, #32
	add	r2, sp, #36
	add	r3, sp, #40
	bl	NandcGetTimeCfg
	mov	r0, r4
	ldr	r1, .L2580+28
	sub	fp, r6, #2880
	sub	r10, r6, #652
	ldr	r3, [sp, #36]
	ldr	r2, [sp, #28]
	str	r3, [sp]
	ldr	r3, [sp, #40]
	str	r3, [sp, #4]
	ldr	r3, [sp, #32]
	bl	sprintf
	ldr	r1, .L2580+32
	add	r4, r4, r0
	add	r5, r4, #10
	mov	r0, r4
	ldr	r4, .L2580+36
	bl	strcpy
	mov	r0, r5
	ldr	r1, .L2580+40
	ldr	r2, [r6, #-2816]
	bl	sprintf
	ldr	r1, .L2580+44
	ldr	r2, [r6, #-408]
	add	r5, r5, r0
	mov	r0, r5
	bl	sprintf
	ldr	r1, .L2580+48
	ldr	r2, [r6, #-3120]
	add	r5, r5, r0
	mov	r0, r5
	bl	sprintf
	ldr	r1, .L2580+52
	ldr	r2, [r6, #-3132]
	add	r5, r5, r0
	mov	r0, r5
	bl	sprintf
	ldr	r1, .L2580+56
	ldr	r2, [r6, #-3136]
	add	r5, r5, r0
	mov	r0, r5
	bl	sprintf
	ldr	r1, .L2580+60
	ldr	r2, [r6, #-3128]
	add	r5, r5, r0
	mov	r0, r5
	bl	sprintf
	ldr	r1, .L2580+64
	ldr	r2, [r6, #-3124]
	add	r5, r5, r0
	mov	r0, r5
	bl	sprintf
	ldr	r1, .L2580+68
	ldr	r2, [r6, #-3140]
	add	r5, r5, r0
	mov	r0, r5
	bl	sprintf
	ldr	r2, [r4, #192]
	ldr	r1, .L2580+72
	mov	r2, r2, lsr #11
	add	r5, r5, r0
	mov	r0, r5
	bl	sprintf
	ldr	r2, [r4, #196]
	ldr	r1, .L2580+76
	mov	r2, r2, lsr #11
	add	r5, r5, r0
	mov	r0, r5
	bl	sprintf
	ldr	r1, .L2580+80
	ldr	r2, [r6, #-3148]
	add	r5, r5, r0
	mov	r0, r5
	bl	sprintf
	ldr	r1, .L2580+84
	ldr	r2, [r6, #-3144]
	add	r5, r5, r0
	mov	r0, r5
	bl	sprintf
	add	r5, r5, r0
	bl	FtlBbtCalcTotleCnt
	ldrh	r2, [fp, #6]
	ldr	r1, .L2580+88
	mov	r3, r0
	mov	r0, r5
	bl	sprintf
	ldr	r1, .L2580+92
	ldrh	r2, [r10]
	add	r5, r5, r0
	mov	r0, r5
	bl	sprintf
	ldr	r1, .L2580+96
	ldr	r2, [r6, #-3116]
	add	r5, r5, r0
	mov	r0, r5
	bl	sprintf
	ldr	r1, .L2580+100
	ldr	r2, [r6, #-3112]
	add	r5, r5, r0
	mov	r0, r5
	bl	sprintf
	ldr	r2, [r4, #204]
	ldr	r1, .L2580+104
	add	r5, r5, r0
	mov	r0, r5
	bl	sprintf
	ldr	r1, .L2580+108
	ldr	r2, [r6, #-3108]
	add	r5, r5, r0
	mov	r0, r5
	bl	sprintf
	ldr	r1, .L2580+112
	ldr	r2, [r6, #-3104]
	add	r5, r5, r0
	mov	r0, r5
	bl	sprintf
	ldr	r1, .L2580+116
	ldr	r2, [r6, #-3100]
	add	r5, r5, r0
	mov	r0, r5
	bl	sprintf
	sub	r3, r6, #716
	ldr	r1, .L2580+120
	ldrh	r2, [r3, #30]
	add	r5, r5, r0
	str	r3, [sp, #20]
	mov	r0, r5
	bl	sprintf
	ldr	r1, .L2580+124
	ldr	r3, [sp, #20]
	add	r5, r5, r0
	mov	r0, r5
	ldrh	r2, [r3, #28]
	bl	sprintf
	ldr	r2, [r9, #3908]
	ldr	r1, .L2580+128
	sub	r9, r6, #648
	add	r5, r5, r0
	mov	r0, r5
	bl	sprintf
	ldr	r1, .L2580+132
	ldr	r2, [r6, #-3168]
	add	r5, r5, r0
	mov	r0, r5
	bl	sprintf
	ldr	r1, .L2580+136
	ldr	r2, [r6, #-3272]
	add	r5, r5, r0
	mov	r0, r5
	bl	sprintf
	ldrh	r2, [fp, #114]
	ldr	r1, .L2580+140
	add	r5, r5, r0
	mov	r0, r5
	bl	sprintf
	sub	r3, r6, #3248
	ldr	r1, .L2580+144
	ldrh	r2, [r3, #-12]
	add	r5, r5, r0
	mov	r0, r5
	bl	sprintf
	sub	r3, r4, #372
	ldr	r1, .L2580+148
	ldrh	r2, [r3]
	add	r5, r5, r0
	mov	r0, r5
	bl	sprintf
	ldr	r1, .L2580+152
	ldr	r2, [r6, #-3256]
	add	r5, r5, r0
	mov	r0, r5
	bl	sprintf
	sub	r3, r6, #404
	ldr	r1, .L2580+156
	ldrh	r2, [r3]
	add	r5, r5, r0
	mov	r0, r5
	bl	sprintf
	ldrh	r2, [fp]
	ldr	r1, .L2580+160
	sub	fp, r6, #3056
	add	r5, r5, r0
	mov	r0, r5
	bl	sprintf
	ldrh	r2, [r9, #2]
	ldr	r1, .L2580+164
	add	r5, r5, r0
	mov	r0, r5
	bl	sprintf
	ldr	r1, .L2580+168
	ldrb	r2, [r6, #-642]	@ zero_extendqisi2
	add	r5, r5, r0
	mov	r0, r5
	bl	sprintf
	ldrh	r2, [r9]
	ldr	r1, .L2580+172
	add	r5, r5, r0
	mov	r0, r5
	bl	sprintf
	ldr	r1, .L2580+176
	ldrb	r2, [r6, #-640]	@ zero_extendqisi2
	add	r5, r5, r0
	mov	r0, r5
	bl	sprintf
	ldrh	r2, [r9, #4]
	ldr	r1, .L2580+180
	add	r5, r5, r0
	mov	r0, r5
	bl	sprintf
	ldrh	r3, [r9]
	ldr	r2, [r6, #-2940]
	sub	r9, r6, #600
	ldr	r1, .L2580+184
	mov	r3, r3, asl #1
	ldrh	r2, [r2, r3]
	add	r5, r5, r0
	mov	r0, r5
	bl	sprintf
	ldrh	r2, [r9, #2]
	ldr	r1, .L2580+188
	add	r5, r5, r0
	mov	r0, r5
	bl	sprintf
	ldr	r1, .L2580+192
	ldrb	r2, [r6, #-594]	@ zero_extendqisi2
	add	r5, r5, r0
	mov	r0, r5
	bl	sprintf
	ldrh	r2, [r9]
	ldr	r1, .L2580+196
	add	r5, r5, r0
	mov	r0, r5
	bl	sprintf
	ldr	r1, .L2580+200
	ldrb	r2, [r6, #-592]	@ zero_extendqisi2
	add	r5, r5, r0
	mov	r0, r5
	bl	sprintf
	ldrh	r2, [r9, #4]
	ldr	r1, .L2580+204
	add	r5, r5, r0
	mov	r0, r5
	bl	sprintf
	ldrh	r3, [r9]
	ldr	r2, [r6, #-2940]
	sub	r9, r6, #552
	ldr	r1, .L2580+208
	mov	r3, r3, asl #1
	ldrh	r2, [r2, r3]
	add	r5, r5, r0
	mov	r0, r5
	bl	sprintf
	ldrh	r2, [r9, #2]
	ldr	r1, .L2580+212
	add	r5, r5, r0
	mov	r0, r5
	bl	sprintf
	ldr	r1, .L2580+216
	ldrb	r2, [r6, #-546]	@ zero_extendqisi2
	add	r5, r5, r0
	mov	r0, r5
	bl	sprintf
	ldrh	r2, [r9]
	ldr	r1, .L2580+220
	add	r5, r5, r0
	mov	r0, r5
	bl	sprintf
	ldr	r1, .L2580+224
	ldrb	r2, [r6, #-544]	@ zero_extendqisi2
	add	r5, r5, r0
	mov	r0, r5
	bl	sprintf
	ldrh	r2, [r9, #4]
	ldr	r1, .L2580+228
	sub	r9, r4, #368
	add	r5, r5, r0
	mov	r0, r5
	bl	sprintf
	ldr	r1, .L2580+232
	ldrh	r2, [r9, #2]
	add	r5, r5, r0
	mov	r0, r5
	bl	sprintf
	ldrb	r2, [r4, #-362]	@ zero_extendqisi2
	ldr	r1, .L2580+236
	add	r5, r5, r0
	mov	r0, r5
	bl	sprintf
	ldr	r1, .L2580+240
	ldrh	r2, [r9]
	add	r5, r5, r0
	mov	r0, r5
	bl	sprintf
	ldrb	r2, [r4, #-360]	@ zero_extendqisi2
	ldr	r1, .L2580+244
	add	r5, r5, r0
	mov	r0, r5
	bl	sprintf
	ldr	r1, .L2580+248
	ldrh	r2, [r9, #4]
	add	r5, r5, r0
	mov	r0, r5
	bl	sprintf
	ldr	r1, [r4, #-236]
	ldr	r3, [r6, #-3336]
	ldr	r2, [r6, #-3152]
	orr	r2, r3, r2, asl #8
	str	r1, [sp]
	add	r5, r5, r0
	ldr	r1, [r4, #-244]
	mov	r0, r5
	str	r1, [sp, #4]
	ldr	r1, .L2580+252
	ldr	r3, [r4, #-240]
	bl	sprintf
	ldr	r2, [r4, #-248]
	ldr	r1, .L2580+256
	add	r5, r5, r0
	mov	r0, r5
	bl	sprintf
	ldr	r2, [r4, #-224]
	ldr	r1, .L2580+260
	add	r5, r5, r0
	mov	r0, r5
	bl	sprintf
	ldr	r1, .L2580+264
	ldrh	r2, [fp, #-8]
	add	r5, r5, r0
	mov	r0, r5
	bl	sprintf
	ldr	r1, .L2580+268
	ldrh	r2, [fp, #-6]
	add	r4, r5, r0
	mov	r0, r4
	bl	sprintf
	ldr	r1, .L2580+272
	ldr	r2, [r6, #-3080]
	add	r4, r4, r0
	mov	r0, r4
	bl	sprintf
	ldr	r1, .L2580+276
	ldrh	r2, [fp, #-4]
	add	r4, r4, r0
	mov	r0, r4
	bl	sprintf
	add	r4, r4, r0
	bl	GetFreeBlockMinEraseCount
	ldr	r1, .L2580+280
	mov	r2, r0
	mov	r0, r4
	bl	sprintf
	add	r4, r4, r0
	ldrh	r0, [r10]
	bl	GetFreeBlockMaxEraseCount
	ldr	r1, .L2580+284
	mov	r2, r0
	mov	r0, r4
	bl	sprintf
	ldrh	r3, [r9]
	movw	r2, #65535
	cmp	r3, r2
	add	r4, r4, r0
	beq	.L2571
	ldr	r2, [r6, #-2940]
	mov	r3, r3, asl #1
	mov	r0, r4
	ldr	r1, .L2580+288
	ldrh	r2, [r2, r3]
	bl	sprintf
	add	r4, r4, r0
.L2571:
	mov	r0, #0
	mov	r5, #0
	bl	List_get_gc_head_node
	movw	fp, #65535
	mov	r10, #6
	uxth	r3, r0
.L2573:
	cmp	r3, fp
	beq	.L2572
	ldr	r2, [r6, #-2940]
	mov	r1, r3, asl #1
	mul	r9, r10, r3
	mov	r0, r4
	ldrh	r2, [r2, r1]
	str	r2, [sp]
	ldr	r2, [r6, #-2888]
	add	r2, r2, r9
	ldrh	r2, [r2, #4]
	str	r2, [sp, #4]
	ldr	r2, [r6, #-2948]
	ldrh	r2, [r2, r1]
	ldr	r1, .L2580+292
	str	r2, [sp, #8]
	mov	r2, r5
	bl	sprintf
	add	r5, r5, #1
	ldr	r3, [r6, #-2888]
	cmp	r5, #16
	ldrh	r3, [r3, r9]
	add	r4, r4, r0
	bne	.L2573
.L2572:
	ldr	r2, [r6, #-2888]
	mov	r5, #0
	ldr	r3, [r6, #-656]
	movw	fp, #65535
	mov	r10, #6
	rsb	r3, r2, r3
	ldr	r2, .L2580+296
	mov	r3, r3, asr #1
	mul	r3, r2, r3
	uxth	r3, r3
.L2575:
	cmp	r3, fp
	beq	.L2574
	mul	r9, r10, r3
	ldr	r2, [r6, #-2888]
	mov	r0, r4
	add	r2, r2, r9
	ldrh	r2, [r2, #4]
	str	r2, [sp]
	mov	r2, r3, asl #1
	ldr	r1, [r6, #-2948]
	ldrh	r2, [r1, r2]
	ldr	r1, .L2580+300
	str	r2, [sp, #4]
	mov	r2, r5
	bl	sprintf
	add	r5, r5, #1
	ldr	r3, [r6, #-2888]
	cmp	r5, #4
	ldrh	r3, [r3, r9]
	add	r4, r4, r0
	bne	.L2575
.L2574:
	ldr	r2, [sp, #44]
	rsb	r0, r8, r4
	ldr	r3, [r7]
	cmp	r2, r3
	beq	.L2576
	bl	__stack_chk_fail
.L2576:
	add	sp, sp, #52
	@ sp needed
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L2581:
	.align	2
.L2580:
	.word	__stack_chk_guard
	.word	.LANCHOR0
	.word	.LC71
	.word	.LC72
	.word	.LC73
	.word	.LANCHOR2
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
	ldr	r1, .L2584
	ldr	r2, .L2584+4
	bl	sprintf
	add	r4, r5, r0
	mov	r0, r4
	bl	FtlPrintInfo2buf
	add	r0, r4, r0
	rsb	r0, r5, r0
	ldmfd	sp!, {r3, r4, r5, pc}
.L2585:
	.align	2
.L2584:
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
	ldr	r5, .L2616
	ldr	r4, .L2616+4
	ldr	r6, [r5, #204]
	ldr	r3, [r4, #-3100]
	cmp	r6, r3
	bcs	.L2587
	sub	r3, r4, #3248
	mov	r6, #0
	str	r6, [r4, #-3116]
	ldrh	r1, [r3, #-12]
	ldr	r3, [r4, #-2948]
	sub	r3, r3, #2
.L2588:
	cmp	r6, r1
	bcs	.L2615
	ldrh	r2, [r3, #2]!
	add	r6, r6, #1
	ldr	r0, [r4, #-3116]
	add	r2, r2, r0
	str	r2, [r4, #-3116]
	b	.L2588
.L2615:
	ldr	r6, [r4, #-3116]
	mov	r0, r6
	bl	__aeabi_uidiv
	ldr	r3, .L2616+8
	ldrh	r1, [r3, #-8]
	str	r0, [r5, #204]
	ldr	r0, [r4, #-3112]
	rsb	r0, r0, r6
	bl	__aeabi_uidiv
	str	r0, [r4, #-3116]
	b	.L2590
.L2587:
	ldr	r3, [r4, #-3104]
	cmp	r6, r3
	bls	.L2590
	ldr	ip, .L2616+12
	add	r3, r3, #1
	mov	lr, r4
	str	r3, [r4, #-3104]
	mov	r3, #0
.L2592:
	ldrh	r2, [ip]
	cmp	r3, r2
	bcs	.L2590
	ldr	r0, [lr, #-2948]
	mov	r1, r3, asl #1
	add	r3, r3, #1
	ldrh	r2, [r0, r1]
	add	r2, r2, #1
	strh	r2, [r0, r1]	@ movhi
	b	.L2592
.L2590:
	ldr	r6, [r4, #-3100]
	ldr	r8, [r5, #204]
	add	r3, r6, #256
	ldr	r2, .L2616+4
	cmp	r3, r8
	bls	.L2595
	ldr	r1, [r2, #-3104]
	add	r3, r6, #768
	cmp	r3, r1
	bls	.L2595
	cmp	r6, #40
	ldr	r2, [r2, #-3336]
	movls	r3, #0
	movhi	r3, #1
	cmp	r2, #0
	orreq	r3, r3, #1
	cmp	r3, #0
	beq	.L2595
.L2597:
	movw	r0, #65535
	b	.L2596
.L2595:
	ldr	r10, .L2616+4
	sub	r3, r10, #652
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
	bne	.L2597
	ldr	r3, [r10, #-668]
	cmp	r3, #0
	beq	.L2597
	sub	r2, r10, #3248
	ldr	r0, [r10, #-2888]
	ldr	ip, .L2616+16
	movw	r7, #65535
	ldrh	r2, [r2, #-12]
	mov	r5, r7
	ldr	r10, [r10, #-2948]
	mov	lr, #6
	str	r2, [sp, #20]
.L2598:
	ldrh	r2, [r3]
	movw	fp, #65535
	cmp	r2, fp
	beq	.L2600
	add	r1, r1, #1
	ldr	fp, [sp, #20]
	uxth	r1, r1
	cmp	r1, fp
	bhi	.L2597
	ldrh	fp, [r3, #4]
	cmp	fp, #0
	beq	.L2599
	rsb	r3, r0, r3
	mov	r3, r3, asr #1
	mul	r3, ip, r3
	uxth	r3, r3
	mov	fp, r3, asl #1
	ldrh	fp, [r10, fp]
	cmp	fp, r6
	bls	.L2604
	cmp	fp, r7
	movcc	r7, fp
	movcc	r5, r3
.L2599:
	mla	r3, lr, r2, r0
	b	.L2598
.L2604:
	mov	r5, r3
.L2600:
	movw	r3, #65535
	cmp	r5, r3
	beq	.L2597
	mov	r3, r5, asl #1
	ldrh	fp, [r10, r3]
	cmp	fp, r6
	bls	.L2602
	str	r3, [sp, #20]
	bl	GetFreeBlockMinEraseCount
	ldr	r3, [sp, #20]
	cmp	r0, r6
	strhi	r7, [r4, #-3100]
.L2602:
	cmp	fp, r8
	bcs	.L2597
	add	r2, fp, #128
	cmp	r9, r2
	ble	.L2597
	add	r2, fp, #256
	cmp	r2, r8
	bcc	.L2603
	ldr	r2, [r4, #-3104]
	add	fp, fp, #768
	cmp	fp, r2
	bcs	.L2597
.L2603:
	ldr	r2, [r4, #-2940]
	mov	r1, r5
	ldr	r0, .L2616+20
	ldrh	r2, [r2, r3]
	str	r2, [sp]
	mov	r2, r8
	ldrh	r3, [r10, r3]
	stmib	sp, {r3, r9}
	ldr	r3, [r4, #-3104]
	bl	printk
	mov	r0, r5
	mov	r3, #1
	str	r3, [r4, #-3092]
.L2596:
	add	sp, sp, #28
	@ sp needed
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L2617:
	.align	2
.L2616:
	.word	.LANCHOR4
	.word	.LANCHOR2
	.word	.LANCHOR2-3200
	.word	.LANCHOR2-3260
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
	beq	.L2619
	ldr	r2, .L2621
	mov	r3, r0, asl #1
	mov	r1, #0
	ldr	r2, [r2, #-2940]
	strh	r1, [r2, r3]	@ movhi
	bl	INSERT_FREE_LIST
.L2619:
	mov	r0, #0
	ldmfd	sp!, {r3, pc}
.L2622:
	.align	2
.L2621:
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
	ldr	r4, .L2678
	ldr	r3, [r4, #-2812]
	cmp	r3, #0
	bne	.L2624
	mov	r5, r0
	mov	r7, r4
.L2625:
	ldr	r3, .L2678+4
	ldrb	r2, [r5, #8]	@ zero_extendqisi2
	cmp	r5, r3
	sub	r6, r3, #100
	bne	.L2626
	ldrh	r3, [r6]
	ldr	ip, [r7, #-3092]
	mov	r0, r3, lsr #1
	mul	lr, ip, r3
	add	r1, r0, #1
	add	r1, r1, lr, lsr #2
	ldr	lr, [r7, #-3336]
	cmp	lr, #0
	uxth	r1, r1
	beq	.L2627
	ldr	lr, [r7, #-3100]
	cmp	lr, #39
	bhi	.L2627
	cmp	lr, #2
	bls	.L2652
	cmp	ip, #0
	movne	r3, #0
	andeq	r3, r3, #1
	cmp	r3, #0
	moveq	r1, r0
	beq	.L2627
	b	.L2652
.L2626:
	cmp	r2, #1
	bne	.L2652
	ldr	r3, .L2678+8
	ldrh	r3, [r3]
	cmp	r3, #1
	beq	.L2652
	ldr	r3, .L2678+12
	ldrb	r3, [r3, #3904]	@ zero_extendqisi2
	cmp	r3, #0
	bne	.L2652
	ldr	r0, [r7, #-3336]
	ldrh	r3, [r6]
	cmp	r0, #0
	mov	r1, r3, lsr #3
	beq	.L2627
	ldr	r0, [r7, #-3100]
	cmp	r0, #1
	rsbls	r3, r3, r3, asl #3
	ubfxls	r1, r3, #3, #16
.L2627:
	cmp	r1, #0
	subne	r1, r1, #1
	uxthne	r1, r1
	b	.L2628
.L2652:
	mov	r1, #0
.L2628:
	ldr	r0, .L2678+16
	bl	List_pop_index_node
	ldr	r10, .L2678+20
	ldrh	r3, [r6]
	sub	r3, r3, #1
	strh	r3, [r6]	@ movhi
	ldrh	r3, [r10]
	uxth	r9, r0
	cmp	r3, r9
	bls	.L2625
	ldr	r3, [r7, #-2940]
	mov	r8, r9, asl #1
	ldrh	r6, [r3, r8]
	cmp	r6, #0
	bne	.L2625
	strh	r9, [r5]	@ movhi
	mov	r0, r5
	bl	make_superblock
	ldrb	r3, [r5, #7]	@ zero_extendqisi2
	cmp	r3, #0
	beq	.L2630
	ldr	r0, [r7, #-3024]
	mov	r2, #36
	ldrh	lr, [r10, #-8]
	add	ip, r5, #14
	mov	r3, r0
	mla	r1, r2, lr, r0
	mov	lr, r6
	str	r1, [sp, #4]
	b	.L2631
.L2630:
	ldr	r3, [r7, #-2940]
	b	.L2673
.L2631:
	ldr	r1, [sp, #4]
	cmp	r3, r1
	beq	.L2675
	str	lr, [r3, #8]
	movw	fp, #65535
	str	lr, [r3, #12]
	add	r3, r3, #36
	ldrh	r1, [ip, #2]!
	cmp	r1, fp
	movne	r1, r1, asl #10
	mlane	fp, r2, r6, r0
	addne	r6, r6, #1
	uxthne	r6, r6
	strne	r1, [fp, #4]
	b	.L2631
.L2675:
	ldr	r3, .L2678+24
	ldr	r2, [r4, #-3336]
	rsb	r3, r3, r5
	clz	r3, r3
	cmp	r2, #0
	mov	r3, r3, lsr #5
	moveq	r3, #0
	cmp	r3, #0
	beq	.L2634
	ldr	r3, [r7, #-2948]
	ldrh	r3, [r3, r8]
	cmp	r3, #40
	movhi	r3, #0
	strhib	r3, [r7, #-640]
.L2634:
	ldrb	r3, [r5, #8]	@ zero_extendqisi2
	ldr	r2, [r4, #-2948]
	cmp	r3, #0
	ldrh	r3, [r2, r8]
	bne	.L2635
	cmp	r3, #0
	mov	r0, r9
	ldrne	r1, .L2678+28
	moveq	r3, #2
	ldrneh	r1, [r1]
	addne	r3, r3, r1
	mov	r1, #0
	uxthne	r3, r3
	strh	r3, [r2, r8]	@ movhi
	ldr	r3, [r4, #-3116]
	add	r3, r3, #1
	str	r3, [r4, #-3116]
	bl	ftl_set_blk_mode
	b	.L2637
.L2635:
	add	r3, r3, #1
	strh	r3, [r2, r8]	@ movhi
	ldr	r1, [r4, #-2928]
	mov	r0, r9, lsr #5
	ldr	r3, [r4, #-3112]
	mov	ip, #1
	add	r3, r3, #1
	str	r3, [r4, #-3112]
	ldr	r2, [r1, r0, asl #2]
	and	r3, r9, #31
	orr	r3, r2, ip, asl r3
	str	r3, [r1, r0, asl #2]
.L2637:
	ldr	r3, [r4, #-2948]
	ldr	r2, [r4, #-3104]
	ldr	r0, [r4, #-3116]
	ldrh	r3, [r3, r8]
	ldrh	r1, [r10]
	cmp	r3, r2
	strhi	r3, [r7, #-3104]
	ldr	r3, .L2678+28
	ldrh	r2, [r3]
	ldr	r3, [r4, #-3112]
	mla	r0, r0, r2, r3
	bl	__aeabi_uidiv
	ldr	r2, [r4, #-2952]
	ldr	r3, .L2678+32
	str	r0, [r3, #204]
	ldr	r3, [r2, #16]
	add	r3, r3, #1
	str	r3, [r2, #16]
	ldr	r2, [r4, #-3024]
	mov	r3, #36
	add	r1, r2, #4
	mla	r3, r3, r6, r2
	add	r3, r3, #40
.L2639:
	add	r1, r1, #36
	cmp	r1, r3
	ldrne	r2, [r1, #-36]
	bicne	r2, r2, #1020
	bicne	r2, r2, #3
	strne	r2, [r1, #-36]
	bne	.L2639
.L2676:
	ldr	r3, .L2678+12
	ldrb	r3, [r3, #3904]	@ zero_extendqisi2
	cmp	r3, #0
	beq	.L2641
	ldrb	r3, [r5, #8]	@ zero_extendqisi2
	mov	r2, r6
	ldr	r0, [r4, #-3024]
	cmp	r3, #1
	moveq	r1, #0
	movne	r1, #1
	bl	FlashEraseBlocks
.L2641:
	ldrb	r1, [r5, #8]	@ zero_extendqisi2
	mov	r2, r6
	ldr	r0, [r4, #-3024]
	mov	r10, #0
	bl	FlashEraseBlocks
	add	r2, r5, #16
	mov	fp, r10
	mov	r1, #36
.L2643:
	uxth	r3, r10
	cmp	r3, r6
	bcs	.L2677
	mul	r3, r1, r10
	ldr	ip, [r4, #-3024]
	add	r0, ip, r3
	ldr	r3, [ip, r3]
	cmn	r3, #1
	bne	.L2644
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
.L2644:
	add	r10, r10, #1
	add	r2, r2, #2
	b	.L2643
.L2677:
	cmp	fp, #0
	beq	.L2646
	mov	r0, r9
	bl	update_multiplier_value
	bl	FtlBbmTblFlush
.L2646:
	ldrb	r3, [r5, #7]	@ zero_extendqisi2
	cmp	r3, #0
	bne	.L2647
	ldr	r3, [r4, #-2940]
.L2673:
	mvn	r2, #0
	strh	r2, [r3, r8]	@ movhi
	b	.L2625
.L2647:
	ldr	r2, .L2678+36
	ldrh	r2, [r2]
	strh	r9, [r5]	@ movhi
	smulbb	r3, r2, r3
	mov	r2, #0
	strh	r2, [r5, #2]	@ movhi
	strb	r2, [r5, #6]
	ldr	r2, [r4, #-3148]
	ldr	r1, [r4, #-2940]
	uxth	r3, r3
	strh	r3, [r5, #4]	@ movhi
	str	r2, [r5, #12]
	add	r2, r2, #1
	str	r2, [r4, #-3148]
	ldrh	r2, [r5]
	mov	r2, r2, asl #1
	strh	r3, [r1, r2]	@ movhi
.L2624:
	mov	r0, #0
	add	sp, sp, #20
	@ sp needed
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L2679:
	.align	2
.L2678:
	.word	.LANCHOR2
	.word	.LANCHOR2-552
	.word	.LANCHOR2-3248
	.word	.LANCHOR0
	.word	.LANCHOR2-656
	.word	.LANCHOR2-3260
	.word	.LANCHOR2-648
	.word	.LANCHOR2-3208
	.word	.LANCHOR4
	.word	.LANCHOR2-3200
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
	ldr	lr, .L2688
	mov	r5, #1
	ldr	r2, .L2688+4
	mov	r7, #36
	ldr	r1, .L2688+8
	add	r6, lr, #78
	mov	r3, #0
	str	r3, [r2, #208]
.L2681:
	ldrh	r2, [lr]
	add	ip, r3, #1
	uxth	r3, r3
	ldr	r0, .L2688+8
	cmp	r3, r2
	bcs	.L2686
	mul	r0, r4, r3
	ldr	r8, [r1, #-2980]
	add	r2, r8, r0
	str	r5, [r2, #8]
	ldrh	r2, [r6]
	mul	r2, r2, r3
	add	r9, r2, #3
	cmp	r2, #0
	movlt	r2, r9
	ldr	r9, [r1, #-2996]
	bic	r2, r2, #3
	add	r2, r9, r2
	str	r2, [r8, r0]
	ldr	r2, .L2688+12
	ldr	r9, [r1, #-2980]
	ldrh	r2, [r2]
	add	r8, r9, r0
	mul	r2, r2, r3
	add	r10, r2, #3
	cmp	r2, #0
	movlt	r2, r10
	ldr	r10, [r1, #-2968]
	bic	r2, r2, #3
	add	r2, r10, r2
	str	r2, [r8, #4]
	ldr	r2, [r1, #-3020]
	mla	r3, r7, r3, r2
	ldr	r2, [r9, r0]
	str	r2, [r3, #8]
	ldr	r2, [r8, #4]
	str	r2, [r3, #12]
	mov	r3, ip
	b	.L2681
.L2686:
	ldr	r4, .L2688+16
	mov	r1, r0
	mov	ip, #12
	mov	lr, #0
.L2683:
	ldr	r3, [r0, #-3012]
	cmp	r2, r3
	bcs	.L2687
	mul	r5, ip, r2
	ldr	r7, [r1, #-2980]
	add	r3, r7, r5
	str	lr, [r3, #8]
	ldrh	r3, [r4]
	mul	r3, r3, r2
	add	r6, r3, #3
	cmp	r3, #0
	movlt	r3, r6
	ldr	r6, [r1, #-2996]
	bic	r3, r3, #3
	add	r3, r6, r3
	str	r3, [r7, r5]
	ldr	r3, .L2688+12
	ldr	r6, [r1, #-2980]
	ldrh	r3, [r3]
	add	r5, r6, r5
	mul	r3, r3, r2
	add	r2, r2, #1
	uxth	r2, r2
	add	r6, r3, #3
	cmp	r3, #0
	movlt	r3, r6
	ldr	r6, [r1, #-2968]
	bic	r3, r3, #3
	add	r3, r6, r3
	str	r3, [r5, #4]
	b	.L2683
.L2687:
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, pc}
.L2689:
	.align	2
.L2688:
	.word	.LANCHOR2-3268
	.word	.LANCHOR4
	.word	.LANCHOR2
	.word	.LANCHOR2-3188
	.word	.LANCHOR2-3190
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
	mvn	r3, #0
	ldr	r6, .L2692
	mov	r4, #0
	ldr	r5, .L2692+4
	mov	r1, r4
	strh	r3, [r6, #216]	@ movhi
	sub	r7, r5, #3248
	str	r3, [r6, #228]
	sub	r3, r5, #2816
	ldr	r0, [r5, #-2924]
	strh	r4, [r3, #-4]	@ movhi
	sub	r3, r5, #3168
	str	r4, [r6, #212]
	ldrh	r2, [r3, #-12]
	str	r4, [r6, #220]
	str	r4, [r6, #224]
	mov	r2, r2, asl #1
	str	r4, [r5, #-3336]
	bl	ftl_memset
	ldrh	r2, [r7, #-10]
	mov	r1, r4
	ldr	r0, [r5, #-2948]
	mov	r2, r2, asl #1
	bl	ftl_memset
	ldrh	r2, [r7, #-10]
	mov	r1, r4
	ldr	r0, [r5, #-2956]
	mov	r2, r2, asl #1
	bl	ftl_memset
	mov	r1, r4
	sub	r0, r5, #716
	mov	r2, #48
	bl	ftl_memset
	mov	r1, r4
	mov	r2, #512
	sub	r0, r6, #320
	bl	ftl_memset
	bl	FtlGcBufInit
	bl	FtlL2PDataInit
	mov	r0, r4
	ldmfd	sp!, {r3, r4, r5, r6, r7, pc}
.L2693:
	.align	2
.L2692:
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
	ldr	r3, .L2702
	stmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, lr}
	.save {r4, r5, r6, r7, r8, r9, r10, lr}
	mov	lr, #0
	ldr	r6, [r3, #-3012]
	mov	r5, #36
	ldr	r4, [r3, #-2980]
	mov	r7, lr
	mov	r8, #12
.L2695:
	uxth	ip, lr
	cmp	ip, r1
	ldmcsfd	sp!, {r4, r5, r6, r7, r8, r9, r10, pc}
	mla	ip, r5, ip, r0
	mov	r2, #0
.L2696:
	uxth	r3, r2
	cmp	r3, r6
	bcs	.L2697
	mul	r3, r8, r3
	add	r2, r2, #1
	add	r9, r4, r3
	ldr	r10, [r4, r3]
	ldr	r3, [ip, #8]
	cmp	r10, r3
	bne	.L2696
	str	r7, [r9, #8]
.L2697:
	add	lr, lr, #1
	b	.L2695
.L2703:
	.align	2
.L2702:
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
	ldr	r3, .L2713
	mov	ip, #0
	stmfd	sp!, {r4, r5, r6, r7, r8, r9, lr}
	.save {r4, r5, r6, r7, r8, r9, lr}
	mov	r6, #12
	ldr	r4, [r3, #-3012]
	mov	r7, #1
	ldr	r5, [r3, #-2980]
	mov	r8, #36
.L2705:
	uxth	r2, ip
	cmp	r2, r1
	bcs	.L2712
	mov	lr, #0
.L2706:
	uxth	r3, lr
	cmp	r3, r4
	bcs	.L2707
	mla	r3, r6, r3, r5
	add	lr, lr, #1
	ldr	r9, [r3, #8]
	cmp	r9, #0
	bne	.L2706
	mla	r2, r8, r2, r0
	ldr	lr, [r3]
	str	r7, [r3, #8]
	str	lr, [r2, #8]
	ldr	r3, [r3, #4]
	str	r3, [r2, #12]
.L2707:
	add	ip, ip, #1
	b	.L2705
.L2712:
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, pc}
.L2714:
	.align	2
.L2713:
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
	ldr	r2, .L2721
	ldr	r3, .L2721+4
	ldrh	r2, [r2, #232]
	ldr	r3, [r3, #-3044]
	add	r2, r3, r2, asl #1
.L2716:
	cmp	r3, r2
	beq	.L2720
	ldrh	r1, [r3], #2
	cmp	r1, r0
	bne	.L2716
	mov	r0, #1
	bx	lr
.L2720:
	mov	r0, #0
	bx	lr
.L2722:
	.align	2
.L2721:
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
	mov	r5, r0
	ubfx	r0, r0, #10, #16
	mov	r6, r1
	mov	r7, r2
	bl	P2V_block_in_plane
	ldr	lr, .L2731
	mov	r4, #0
	ldr	r3, .L2731+4
	ldr	r2, [lr, #-3044]
	ldrh	r1, [r3, #232]
	sub	r8, r2, #2
.L2724:
	uxth	ip, r4
	cmp	ip, r1
	bcs	.L2728
	ldrh	r9, [r8, #2]!
	add	r4, r4, #1
	cmp	r9, r0
	bne	.L2724
.L2728:
	cmp	ip, r1
	moveq	ip, ip, asl #1
	streqh	r0, [r2, ip]	@ movhi
	ldreqh	r0, [r3, #232]
	addeq	r0, r0, #1
	streqh	r0, [r3, #232]	@ movhi
.L2726:
	ldrh	r2, [r3, #234]
	mov	ip, #12
	mul	ip, ip, r2
	ldr	r2, [lr, #-3040]
	add	r1, r2, ip
	stmib	r1, {r6, r7}
	str	r5, [r2, ip]
	ldrh	r2, [r3, #234]
	add	r2, r2, #1
	strh	r2, [r3, #234]	@ movhi
	ldmfd	sp!, {r3, r4, r5, r6, r7, r8, r9, pc}
.L2732:
	.align	2
.L2731:
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
	mov	r2, #0
	ldr	r4, .L2735
	mov	r1, #255
	ldr	r3, .L2735+4
	sub	r5, r4, #3184
	ldr	r0, [r4, #-3044]
	strh	r2, [r3, #232]	@ movhi
	strh	r2, [r3, #234]	@ movhi
	ldrh	r2, [r5, #-12]
	mov	r2, r2, asl #1
	bl	ftl_memset
	ldrh	r3, [r5, #-12]
	mov	r2, #12
	ldr	r0, [r4, #-3040]
	mov	r1, #255
	mul	r2, r2, r3
	bl	ftl_memset
	ldmfd	sp!, {r3, r4, r5, lr}
	b	FtlGcBufInit
.L2736:
	.align	2
.L2735:
	.word	.LANCHOR2
	.word	.LANCHOR4
	.fnend
	.size	FtlGcPageVarInit, .-FtlGcPageVarInit
	.align	2
	.global	FtlGcScanTempBlk
	.type	FtlGcScanTempBlk, %function
FtlGcScanTempBlk:
	.fnstart
	@ args = 0, pretend = 0, frame = 80
	@ frame_needed = 0, uses_anonymous_args = 0
	ldr	r3, .L2785
	stmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.save {r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.pad #84
	sub	sp, sp, #84
	ldr	r3, [r3]
	mov	r4, r0
	ldr	r2, .L2785+4
	str	r1, [sp, #16]
	str	r3, [sp, #76]
	movw	r3, #3288
	ldrh	r5, [r2, r3]
	movw	r3, #65535
	cmp	r5, r3
	beq	.L2769
	cmp	r5, #0
	bne	.L2738
	b	.L2739
.L2769:
	mov	r5, #0
.L2738:
	ldr	r3, .L2785+8
	ldr	r2, [sp, #16]
	ldrh	r3, [r3]
	cmp	r2, r3
	bne	.L2740
.L2739:
	bl	FtlGcPageVarInit
.L2740:
	ldr	r6, .L2785+12
	mvn	r3, #0
	str	r3, [sp, #12]
	mov	r3, #0
	str	r3, [sp, #4]
	mov	r8, r6
.L2741:
	ldrh	r2, [r4]
	mov	r3, #0
	strb	r3, [r4, #8]
	movw	r3, #65535
	cmp	r2, r3
	beq	.L2742
.L2743:
.L2765:
	ldr	r3, .L2785+16
	add	r0, r4, #14
	ldr	ip, [r6, #-2968]
	movw	r9, #65535
	mov	r10, #36
	ldrh	r3, [r3]
	str	r3, [sp, #8]
	ldr	r3, [r6, #-3036]
	str	r3, [sp, #20]
	ldr	r3, [r6, #-2996]
	str	r3, [sp, #24]
	ldr	r3, .L2785+20
	ldrh	r2, [r3]
	ldrh	lr, [r3, #2]
	str	r2, [sp, #28]
	mov	r2, #0
	mov	r7, r2
.L2744:
	ldr	r1, [sp, #8]
	uxth	r3, r2
	cmp	r3, r1
	bcs	.L2783
	ldrh	r3, [r0, #2]!
	cmp	r3, r9
	beq	.L2745
	ldr	r1, [sp, #20]
	orr	r3, r5, r3, asl #10
	mla	r1, r10, r7, r1
	str	r3, [r1, #4]
	ldr	r3, [sp, #28]
	mul	r3, r3, r7
	add	fp, r3, #3
	cmp	r3, #0
	movlt	r3, fp
	ldr	fp, [sp, #24]
	bic	r3, r3, #3
	add	r3, fp, r3
	str	r3, [r1, #8]
	mul	r3, lr, r7
	add	r7, r7, #1
	uxth	r7, r7
	add	fp, r3, #3
	cmp	r3, #0
	movlt	r3, fp
	bic	r3, r3, #3
	add	r3, ip, r3
	str	r3, [r1, #12]
.L2745:
	add	r2, r2, #1
	b	.L2744
.L2783:
	ldr	r0, [r6, #-3036]
	mov	r1, r7
	mov	r2, #0
	mov	fp, #0
	bl	FlashReadPages
.L2747:
	uxth	r3, fp
	cmp	r3, r7
	bcs	.L2784
	mov	r10, #36
	ldr	r2, [r6, #-3036]
	mul	r10, r10, fp
	ldr	ip, .L2785+12
	add	r3, r2, r10
	ldr	r9, [r2, r10]
	ldr	r1, [r3, #4]
	cmp	r9, #0
	ldr	r3, [r3, #12]
	str	r1, [sp, #8]
	bne	.L2748
	ldrh	r1, [r3]
	movw	r2, #65535
	cmp	r1, r2
	bne	.L2749
.L2751:
	ldrh	r3, [r4]
	ldr	r2, [r8, #-2940]
	b	.L2782
.L2749:
	ldr	r0, [r3, #8]
	ldr	r2, [r6, #-2816]
	cmp	r0, r2
	bhi	.L2751
	ldr	r2, .L2785+24
	ldrb	r2, [r2]	@ zero_extendqisi2
	cmp	r2, #0
	beq	.L2753
	add	r1, sp, #36
	mov	r2, r9
	str	r3, [sp, #20]
	bl	log2phys
	ldr	r3, [sp, #20]
	ldr	r1, [sp, #36]
	ldr	r2, [r3, #12]
	cmn	r1, #1
	rsb	r0, r2, r1
	clz	r0, r0
	mov	r0, r0, lsr #5
	moveq	r0, #0
	cmp	r0, #0
	beq	.L2753
	str	r2, [sp, #44]
	mov	r1, #1
	ldr	r2, [r6, #-2984]
	add	r0, sp, #40
	str	r2, [sp, #48]
	ldr	r2, [r6, #-2972]
	str	r2, [sp, #52]
	mov	r2, r9
	bl	FlashReadPages
	ldr	r2, .L2785+28
	ldr	r1, [r6, #-3036]
	ldrh	r2, [r2]
	add	r10, r1, r10
	mov	r2, r2, asl #7
	ldr	r1, [sp, #48]
	ldr	r3, [sp, #20]
	b	.L2755
.L2756:
	add	r9, r9, #1
.L2755:
	cmp	r9, r2
	beq	.L2753
	ldr	r0, [r10, #8]
	ldr	ip, [r0, r9, asl #2]
	ldr	r0, [r1, r9, asl #2]
	cmp	ip, r0
	beq	.L2756
	ldr	r2, [sp, #44]
	ldr	r0, .L2785+32
	ldrh	r1, [r4]
	bl	printk
	ldrh	r3, [r4]
	ldr	r2, [r6, #-2940]
.L2782:
	mov	r3, r3, asl #1
	mov	r1, #0
	strh	r1, [r2, r3]	@ movhi
	ldrh	r0, [r4]
	bl	INSERT_FREE_LIST
	ldr	r2, .L2785+36
	mvn	r3, #0
	strh	r3, [r4]	@ movhi
	strh	r3, [r2]	@ movhi
	b	.L2781
.L2753:
	ldr	r0, [r3, #12]
	add	fp, fp, #1
	ldr	r1, [sp, #8]
	ldr	r2, [r3, #8]
	bl	FtlGcUpdatePage
	b	.L2747
.L2748:
	ldr	r0, .L2785+40
	ldrh	r1, [r4]
	ldr	r2, [sp, #8]
	str	ip, [sp, #20]
	bl	printk
	ldr	r3, [r8, #-3336]
	ldrh	r5, [r4]
	cmp	r3, #0
	ldr	ip, [sp, #20]
	bne	.L2759
	ldr	r3, .L2785+24
	ldrb	r3, [r3, #3904]	@ zero_extendqisi2
	cmp	r3, #0
	beq	.L2760
.L2759:
	ldr	r2, [ip, #-2948]
	mov	r3, r5, asl #1
	ldrh	r3, [r2, r3]
	cmp	r3, #159
	bls	.L2761
.L2760:
	ldr	r3, [ip, #-3036]
	ldr	r3, [r3, r10]
	cmn	r3, #1
	bne	.L2762
.L2761:
	ldr	r3, [ip, #-3036]
	add	r10, r3, r10
	ldr	r3, [r10, #4]
	str	r3, [sp, #12]
.L2762:
	ldr	r3, [r8, #-2940]
	mov	r5, r5, asl #1
	mov	r2, #0
	strh	r2, [r3, r5]	@ movhi
	ldrh	r0, [r4]
	bl	INSERT_FREE_LIST
	mvn	r3, #0
	strh	r3, [r4]	@ movhi
.L2781:
	bl	FtlGcPageVarInit
	mov	r5, #0
	b	.L2741
.L2784:
	ldr	r3, [sp, #4]
	add	r5, r5, #1
	ldr	r2, [sp, #16]
	add	r3, r3, #1
	uxth	r5, r5
	cmp	r3, r2
	str	r3, [sp, #4]
	ldr	r2, .L2785+8
	bcs	.L2764
.L2766:
	ldrh	r3, [r2]
	cmp	r3, r5
	bhi	.L2765
	b	.L2742
.L2764:
	ldr	r1, .L2785+44
	movw	r0, #65535
	ldrh	r3, [r1]
	cmp	r3, r0
	beq	.L2766
	ldr	r0, [sp, #4]
	add	r3, r3, r0
	strh	r3, [r1]	@ movhi
	ldrh	r3, [r2]
	cmp	r3, r5
	bls	.L2766
	b	.L2767
.L2742:
	ldr	r2, .L2785+4
	movw	r3, #3288
	mvn	r1, #0
	strh	r5, [r4, #2]	@ movhi
	mov	r0, r4
	strh	r1, [r2, r3]	@ movhi
	mov	r2, #0
	mov	r1, r5
	strb	r2, [r4, #6]
	bl	ftl_sb_update_avl_pages
.L2767:
	ldr	r3, .L2785
	ldr	r2, [sp, #76]
	ldr	r0, [sp, #12]
	ldr	r3, [r3]
	cmp	r2, r3
	beq	.L2768
	bl	__stack_chk_fail
.L2768:
	add	sp, sp, #84
	@ sp needed
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L2786:
	.align	2
.L2785:
	.word	__stack_chk_guard
	.word	.LANCHOR1
	.word	.LANCHOR2-3200
	.word	.LANCHOR2
	.word	.LANCHOR2-3268
	.word	.LANCHOR2-3190
	.word	.LANCHOR0
	.word	.LANCHOR2-3194
	.word	.LC143
	.word	.LANCHOR4-368
	.word	.LC144
	.word	.LANCHOR1+3288
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
	ldr	r6, .L2795
	ldrh	r3, [r6]
	cmp	r3, r0
	beq	.L2789
	add	r4, r6, #16
	ldrh	r3, [r4, #-14]
	cmp	r3, r0
	beq	.L2789
	ldrh	r3, [r4, #-12]
	cmp	r3, r0
	beq	.L2789
	ldrh	r3, [r4, #-10]
	cmp	r3, r0
	beq	.L2789
	ldr	r0, .L2795+4
	mov	r1, r5
	bl	printk
	ldrh	r2, [r6]
	movw	r3, #65535
	cmp	r2, r3
	streqh	r5, [r6]	@ movhi
	beq	.L2789
	ldrh	r2, [r4, #-14]
	cmp	r2, r3
	streqh	r5, [r4, #-14]	@ movhi
	beq	.L2789
	ldrh	r2, [r4, #-12]
	cmp	r2, r3
	streqh	r5, [r4, #-12]	@ movhi
	beq	.L2789
	ldrh	r2, [r4, #-10]
	cmp	r2, r3
	streqh	r5, [r4, #-10]	@ movhi
.L2789:
	mov	r0, #0
	ldmfd	sp!, {r4, r5, r6, pc}
.L2796:
	.align	2
.L2795:
	.word	.LANCHOR2-3072
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
	ldr	r6, .L2808
	ldrh	r3, [r6]
	cmp	r3, r0
	beq	.L2805
	add	r4, r6, #16
	ldrh	r3, [r4, #-14]
	cmp	r3, r0
	beq	.L2805
	ldrh	r3, [r4, #-12]
	cmp	r3, r0
	beq	.L2805
	ldrh	r3, [r4, #-10]
	cmp	r3, r0
	beq	.L2805
	ldr	r0, .L2808+4
	mov	r1, r5
	bl	printk
	ldrh	r2, [r6]
	movw	r3, #65535
	cmp	r2, r3
	streqh	r5, [r6]	@ movhi
	beq	.L2805
	ldrh	r2, [r4, #-14]
	cmp	r2, r3
	streqh	r5, [r4, #-14]	@ movhi
	beq	.L2805
	ldrh	r2, [r4, #-12]
	cmp	r2, r3
	streqh	r5, [r4, #-12]	@ movhi
	beq	.L2805
	ldrh	r2, [r4, #-10]
	cmp	r2, r3
	bne	.L2806
	strh	r5, [r4, #-10]	@ movhi
.L2805:
	mov	r0, #0
	ldmfd	sp!, {r4, r5, r6, pc}
.L2806:
	mvn	r0, #0
	ldmfd	sp!, {r4, r5, r6, pc}
.L2809:
	.align	2
.L2808:
	.word	.LANCHOR2-3072
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
	ldr	r6, .L2820
	mov	r2, r5
	sub	r7, r6, #3056
	ldrh	r1, [r7, #-2]
	mov	r4, r0
	ldr	r0, .L2820+4
	bl	printk
	mov	r0, r4
	bl	FtlGcRefreshBlock
	ldr	r3, [r6, #-3336]
	cmp	r3, #0
	beq	.L2811
	ldr	r2, [r6, #-2948]
	mov	r4, r4, asl #1
	ldrh	r3, [r2, r4]
	cmp	r3, #39
	subhi	r3, r3, #40
	strhih	r3, [r2, r4]	@ movhi
.L2811:
	ldrh	r3, [r7, #-2]
	mov	r2, #0
	ldr	r0, .L2820+8
.L2812:
	uxth	r1, r2
	cmp	r1, r3
	bcs	.L2819
	add	r2, r2, #1
	add	r1, r0, r2, asl #1
	ldrh	r1, [r1, #-2]
	cmp	r1, r5
	bne	.L2812
	b	.L2813
.L2819:
	cmp	r3, #15
	addls	r2, r3, #1
	strlsh	r2, [r7, #-2]	@ movhi
	ldrls	r2, .L2820+12
	addls	r3, r2, r3, asl #1
	strlsh	r5, [r3, #236]	@ movhi
.L2813:
	mov	r0, #0
	ldmfd	sp!, {r3, r4, r5, r6, r7, pc}
.L2821:
	.align	2
.L2820:
	.word	.LANCHOR2
	.word	.LC146
	.word	.LANCHOR4+236
	.word	.LANCHOR4
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
	ldr	r4, .L2829
	ldrh	r3, [r4, #-2]
	cmp	r3, #0
	beq	.L2823
	ldrh	r1, [r4, #-16]
	movw	r2, #65535
	cmp	r1, r2
	bne	.L2823
	add	r4, r4, #16
	ldrh	r2, [r4, #-14]
	cmp	r2, r3
	movcs	r3, #0
	strcsh	r3, [r4, #-14]	@ movhi
	ldrh	r2, [r4, #-14]
	ldr	r3, .L2829+4
	add	r3, r3, r2, asl #1
	ldrh	r0, [r3, #236]
	bl	P2V_block_in_plane
	bl	FtlGcRefreshBlock
	ldrh	r3, [r4, #-14]
	add	r3, r3, #1
	strh	r3, [r4, #-14]	@ movhi
.L2823:
	mov	r0, #0
	ldmfd	sp!, {r4, pc}
.L2830:
	.align	2
.L2829:
	.word	.LANCHOR2-3056
	.word	.LANCHOR4
	.fnend
	.size	FtlGcReFreshBadBlk, .-FtlGcReFreshBadBlk
	.align	2
	.global	FtlGcFreeBadSuperBlk
	.type	FtlGcFreeBadSuperBlk, %function
FtlGcFreeBadSuperBlk:
	.fnstart
	@ args = 0, pretend = 0, frame = 8
	@ frame_needed = 0, uses_anonymous_args = 0
	ldr	r3, .L2845
	stmfd	sp!, {r0, r1, r2, r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.save {r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.pad #12
	ldrh	r2, [r3, #-2]
	cmp	r2, #0
	beq	.L2833
	sub	r5, r3, #2
	mov	r7, r0
	mov	r6, #0
	mov	r8, r5
.L2832:
	ldr	r3, .L2845+4
	uxth	r2, r6
	ldrh	r3, [r3]
	cmp	r3, r2
	bls	.L2842
	ldr	r3, .L2845+8
	mov	r1, r7
	ldr	fp, .L2845+12
	mov	r9, #0
	add	r3, r3, r2
	ldrb	r0, [r3, #-3240]	@ zero_extendqisi2
	bl	V2P_block
	mov	r10, r0
.L2834:
	ldrh	r3, [r5]
	uxth	r4, r9
	cmp	r3, r4
	bls	.L2843
	mov	ip, r4, asl #1
	add	r3, fp, ip
	ldrh	r3, [r3, #236]
	cmp	r3, r10
	bne	.L2835
	mov	r1, r10
	ldr	r0, .L2845+16
	str	ip, [sp, #4]
	bl	printk
	mov	r0, r10
	bl	FtlBbmMapBadBlock
	bl	FtlBbmTblFlush
	ldr	r3, .L2845+20
	ldrh	r2, [r5]
	ldr	ip, [sp, #4]
	add	r3, r3, ip
.L2836:
	cmp	r4, r2
	ldrcch	r1, [r3, #2]
	addcc	r4, r4, #1
	uxthcc	r4, r4
	strcch	r1, [r3], #2	@ movhi
	bcc	.L2836
.L2844:
	sub	r2, r2, #1
	strh	r2, [r8]	@ movhi
.L2835:
	add	r9, r9, #1
	b	.L2834
.L2843:
	add	r6, r6, #1
	b	.L2832
.L2842:
	bl	FtlGcReFreshBadBlk
.L2833:
	mov	r0, #0
	add	sp, sp, #12
	@ sp needed
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L2846:
	.align	2
.L2845:
	.word	.LANCHOR2-3056
	.word	.LANCHOR2-3268
	.word	.LANCHOR2
	.word	.LANCHOR4
	.word	.LC147
	.word	.LANCHOR4+236
	.fnend
	.size	FtlGcFreeBadSuperBlk, .-FtlGcFreeBadSuperBlk
	.align	2
	.global	update_vpc_list
	.type	update_vpc_list, %function
update_vpc_list:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	ldr	r2, .L2857
	mov	r3, r0, asl #1
	stmfd	sp!, {r4, lr}
	.save {r4, lr}
	mov	r4, r0
	ldr	r1, [r2, #-2940]
	ldrh	r3, [r1, r3]
	cmp	r3, #0
	bne	.L2848
	ldr	r1, .L2857+4
	ldrh	r0, [r1]
	cmp	r0, r4
	mvneq	r3, #0
	streqh	r3, [r1]	@ movhi
	beq	.L2850
	sub	r1, r2, #648
	ldrh	r1, [r1]
	cmp	r1, r4
	beq	.L2856
	sub	r1, r2, #600
	ldrh	r1, [r1]
	cmp	r1, r4
	beq	.L2856
	sub	r2, r2, #552
	ldrh	r2, [r2]
	cmp	r2, r4
	beq	.L2856
.L2850:
	mov	r1, r4
	ldr	r0, .L2857+8
	bl	List_remove_node
	ldr	r2, .L2857+12
	mov	r0, r4
	ldrh	r3, [r2]
	sub	r3, r3, #1
	strh	r3, [r2]	@ movhi
	bl	free_data_superblock
	mov	r0, r4
	bl	FtlGcFreeBadSuperBlk
	mov	r0, #1
	ldmfd	sp!, {r4, pc}
.L2848:
	bl	List_update_data_list
.L2856:
	mov	r0, #0
	ldmfd	sp!, {r4, pc}
.L2858:
	.align	2
.L2857:
	.word	.LANCHOR2
	.word	.LANCHOR4-368
	.word	.LANCHOR2-668
	.word	.LANCHOR2-660
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
	beq	.L2860
	ldr	r5, .L2870
	mov	r6, r0, asl #1
	ldr	r3, [r5, #-2940]
	ldrh	r2, [r3, r6]
	cmp	r2, #0
	subne	r2, r2, #1
	strneh	r2, [r3, r6]	@ movhi
	bne	.L2860
	mov	r1, r4
	ldr	r0, .L2870+4
	bl	printk
	ldr	r3, [r5, #-2940]
	mov	r2, #32
	sub	r0, r5, #656
	mov	r1, r4
	strh	r2, [r3, r6]	@ movhi
	bl	test_node_in_list
	cmp	r0, #0
	beq	.L2862
	mov	r1, r4
	sub	r0, r5, #656
	bl	List_remove_node
	sub	r2, r5, #652
	mov	r0, r4
	ldrh	r3, [r2]
	sub	r3, r3, #1
	strh	r3, [r2]	@ movhi
	bl	INSERT_DATA_LIST
	ldr	r3, [r5, #-2940]
	ldr	r0, .L2870+8
	mov	r1, r4
	ldrh	r2, [r3, r6]
	bl	printk
.L2862:
	mov	r0, r4
	bl	FtlGcRefreshBlock
	b	.L2865
.L2860:
	ldr	r5, .L2870+12
	movw	r3, #65535
	ldrh	r0, [r5, #216]
	cmp	r0, r3
	streqh	r4, [r5, #216]	@ movhi
	beq	.L2865
	cmp	r0, r4
	beq	.L2865
	bl	update_vpc_list
	strh	r4, [r5, #216]	@ movhi
	adds	r0, r0, #0
	movne	r0, #1
	ldmfd	sp!, {r4, r5, r6, pc}
.L2865:
	mov	r0, #0
	ldmfd	sp!, {r4, r5, r6, pc}
.L2871:
	.align	2
.L2870:
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
	ldr	r3, .L3032
	movw	r2, #65535
	stmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.save {r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.pad #68
	sub	sp, sp, #68
	ldr	r3, [r3]
	mov	r9, r0
	str	r3, [sp, #60]
	ldrh	r3, [r0]
	cmp	r3, r2
	beq	.L2874
	ldrh	r3, [r0, #2]
	str	r3, [sp, #8]
	ldrb	r3, [r0, #6]	@ zero_extendqisi2
	ldr	r1, [sp, #8]
	str	r3, [sp, #16]
	ldr	r3, .L3032+4
	ldrh	r3, [r3]
	cmp	r3, r1
	mov	r3, #0
	streqh	r3, [r0, #4]	@ movhi
	ldrneh	r0, [r0, #16]
	beq	.L3024
.L2876:
	cmp	r0, r2
	add	r3, r3, #1
	uxtheq	r1, r3
	addeq	r1, r9, r1, asl #1
	ldreqh	r0, [r1, #16]
	beq	.L2876
.L3027:
	ldrb	r1, [r9, #8]	@ zero_extendqisi2
	cmp	r1, #1
	bne	.L2878
	bl	FtlGetLastWrittenPage
	cmn	r0, #1
	mov	r4, r0
	beq	.L2879
	ldr	r3, .L3032+8
	ldrb	r2, [r3, #3904]	@ zero_extendqisi2
	cmp	r2, #0
	addeq	r3, r3, r0, asl #1
	ldreqh	r5, [r3, #76]
	beq	.L2950
	b	.L3017
.L2878:
	mov	r1, #0
	bl	FtlGetLastWrittenPage
	cmn	r0, #1
	mov	r4, r0
	beq	.L2879
.L3017:
	mov	r5, r4
.L2950:
	ldr	r3, .L3032+12
	movw	r8, #65535
	sub	r2, r3, #3264
	sub	r3, r3, #3184
	ldr	ip, [r3, #148]
	ldr	lr, [r3, #216]
	ldrh	r2, [r2, #-4]
	ldrh	r7, [r3, #-4]
	add	r3, r9, #14
	str	r3, [sp, #20]
	str	r2, [sp]
	mov	r2, #0
	mov	r0, r3
	mov	r6, r2
	mov	r10, r2
	b	.L2881
.L2879:
	mov	r3, #0
	strh	r3, [r9, #2]	@ movhi
.L3024:
	strb	r3, [r9, #6]
	b	.L2874
.L2883:
	ldrh	r3, [r0, #2]!
	cmp	r3, r8
	beq	.L2882
	mov	r1, #36
	orr	r3, r5, r3, asl #10
	mla	r1, r1, r6, ip
	stmib	r1, {r3, r10}
	mul	r3, r7, r6
	add	r6, r6, #1
	uxth	r6, r6
	add	fp, r3, #3
	cmp	r3, #0
	movlt	r3, fp
	bic	r3, r3, #3
	add	r3, lr, r3
	str	r3, [r1, #12]
.L2882:
	add	r2, r2, #1
.L2881:
	ldr	r1, [sp]
	uxth	r3, r2
	cmp	r3, r1
	bcc	.L2883
	ldrb	r3, [r9, #8]	@ zero_extendqisi2
	cmp	r3, #1
	movne	r3, #0
	bne	.L3018
	ldr	r3, .L3032+8
	ldrb	lr, [r3, #3904]	@ zero_extendqisi2
	adds	r3, lr, #0
	movne	r3, #1
.L3018:
	ldr	r7, .L3032+12
	mov	r1, r6
	str	r3, [sp, #24]
	mov	fp, #0
	ldr	r2, [sp, #24]
	movw	r10, #65535
	ldr	r0, [r7, #-3036]
	bl	FlashReadPages
	ldr	r3, [r7, #-3144]
	str	r7, [sp]
	str	r3, [sp, #28]
.L2885:
	uxth	r8, fp
	cmp	r8, r6
	bcs	.L2892
	mov	r2, #36
	ldr	r0, [r7, #-3036]
	mul	r2, r2, fp
	add	r1, r0, r2
	ldr	r2, [r0, r2]
	cmp	r2, #0
	bne	.L2886
	ldr	r2, [r1, #12]
	ldr	r3, [r2, #4]
	cmn	r3, #1
	beq	.L2887
	mov	r0, r3
	ldr	r1, [r7, #-3144]
	str	r2, [sp, #12]
	str	r3, [sp, #4]
	bl	ftl_cmp_data_ver
	ldr	r3, [sp, #4]
	cmp	r0, #0
	ldr	r2, [sp, #12]
	addne	r3, r3, #1
	strne	r3, [r7, #-3144]
.L2887:
	ldr	r2, [r2]
	cmn	r2, #1
	bne	.L2888
.L2892:
	cmp	r8, r6
	bne	.L3014
	add	fp, r4, #1
	uxth	r3, fp
	str	r3, [sp, #4]
	ldr	r3, .L3032+12
	ldr	r3, [r3, #-3036]
	ldr	r0, [r3, #4]
	b	.L3019
.L2886:
	ldr	r0, .L3032+16
	uxth	r10, r5
	ldr	r1, [r1, #4]
	bl	printk
	ldrh	r2, [r9]
	ldr	r3, .L3032+20
	strh	r2, [r3]	@ movhi
.L2888:
	add	fp, fp, #1
	b	.L2885
.L3014:
	uxth	r3, r4
	str	r3, [sp, #4]
	ldr	r3, [sp]
	mov	r2, #36
	ldr	r3, [r3, #-3036]
	mla	r8, r2, r8, r3
	ldr	r0, [r8, #4]
.L3019:
	ubfx	r0, r0, #10, #16
	bl	P2V_plane
	ldrb	r3, [r9, #8]	@ zero_extendqisi2
	cmp	r3, #1
	str	r0, [sp, #12]
	bne	.L2894
	ldr	r2, .L3032+8
	ldrb	r1, [r2, #3904]	@ zero_extendqisi2
	cmp	r1, #0
	ldreq	r1, [sp, #4]
	addeq	r2, r2, r1, asl #1
	ldreqh	r2, [r2, #76]
	streq	r2, [sp, #4]
.L2894:
	ldr	r2, .L3032+4
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
	ldrh	r2, [sp, #16]
	str	r2, [sp, #32]
	ldr	ip, [sp, #32]
	ldr	r2, [sp, #4]
	cmp	r2, r1
	cmpeq	r0, ip
	moveq	r0, r9
	moveq	r1, r2
	beq	.L3025
	clz	r3, r3
	ldr	r2, [sp, #28]
	mov	r3, r3, lsr #5
	sub	r2, r2, #1
	str	r2, [sp]
	movw	r2, #65535
	cmp	r10, r2
	orrne	r3, r3, #1
	cmp	r3, #0
	beq	.L2898
	ldr	r3, .L3032+24
	uxth	r5, r5
	ldr	fp, .L3032+12
	mvn	r8, #0
	mov	r7, r8
	ldr	r2, [r3, #228]
	cmn	r2, #1
	ldreq	r2, [sp]
	streq	r2, [r3, #228]
	ldr	r10, [r3, #228]
	ldr	r3, [sp, #8]
	add	r3, r3, #7
	cmp	r5, r3
	subgt	r4, r5, #7
	ldrle	r4, [sp, #8]
	uxthgt	r4, r4
.L2901:
	cmp	r4, r5
	bhi	.L2914
	ldr	r3, .L3032+28
	mov	r0, #36
	ldr	lr, [fp, #-3036]
	ldr	r1, [sp, #20]
	ldrh	r3, [r3]
	str	r3, [sp, #16]
	mov	r3, #0
	mov	r6, r3
.L2915:
	ldr	ip, [sp, #16]
	uxth	r2, r3
	cmp	r2, ip
	bcs	.L3028
	ldrh	r2, [r1, #2]!
	movw	ip, #65535
	add	r3, r3, #1
	cmp	r2, ip
	orrne	r2, r4, r2, asl #10
	mlane	ip, r0, r6, lr
	addne	r6, r6, #1
	uxthne	r6, r6
	strne	r2, [ip, #4]
	b	.L2915
.L3028:
	mov	r1, r6
	ldr	r2, [sp, #24]
	ldr	r0, [fp, #-3036]
	bl	FlashReadPages
	ldr	r2, .L3032+8
	ldr	r3, [fp, #-3036]
	mov	r1, #36
	add	r2, r2, #588
	ldrb	ip, [r2, #3316]	@ zero_extendqisi2
	add	r2, r2, r4, asl #1
	mla	r6, r1, r6, r3
	movw	r1, #65535
.L2904:
	cmp	r3, r6
	beq	.L3029
	ldr	r0, [r3]
	cmp	r0, #0
	bne	.L2905
	ldr	r0, [r3, #12]
	ldrh	lr, [r0]
	cmp	lr, r1
	beq	.L2906
	ldr	r0, [r0, #4]
	cmn	r0, #1
	beq	.L2906
	ldr	lr, .L3032+24
	cmn	r8, #1
	ldr	r7, [lr, #228]
	str	r0, [lr, #228]
	bne	.L2906
	ldrh	r0, [r2]
	cmp	r0, r1
	bne	.L2907
	cmp	ip, #0
	beq	.L2906
.L2907:
	ldr	r0, [sp]
	cmp	r7, r0
	mvneq	r8, #0
	movne	r8, r7
	b	.L2906
.L2905:
	ldrh	r1, [r9]
	movw	r2, #270
	ldr	r3, .L3032+24
	strh	r1, [r3, r2]	@ movhi
	ldrb	r2, [r9, #8]	@ zero_extendqisi2
	cmp	r2, #0
	bne	.L2898
	ldr	r2, .L3032+32
	mov	r4, r4, asl #1
	ldrh	r1, [r2, r4]
	movw	r2, #65535
	cmp	r1, r2
	bne	.L2909
	cmn	r8, #1
	strne	r8, [r3, #228]
	bne	.L2898
	ldr	r2, [sp]
	cmp	r10, r2
	strne	r10, [r3, #228]
	ldreq	r2, [r3, #228]
	bne	.L2898
	b	.L3026
.L2909:
	ldr	r2, [sp]
	cmp	r7, r2
	beq	.L2912
	cmn	r7, #1
	strne	r7, [r3, #228]
	b	.L2898
.L2912:
	ldr	r2, [r3, #228]
	ldr	r1, [sp]
	cmp	r2, r1
	beq	.L2898
.L3026:
	sub	r2, r2, #1
	b	.L3020
.L2906:
	add	r3, r3, #36
	b	.L2904
.L3029:
	add	r4, r4, #1
	uxth	r4, r4
	b	.L2901
.L2914:
	ldr	r3, .L3032+24
	mvn	r2, #0
.L3020:
	str	r2, [r3, #228]
.L2898:
	ldr	fp, .L3032+36
	mov	r3, #1
	ldr	r10, [sp, #8]
	strh	r3, [fp]	@ movhi
	sub	fp, fp, #272
.L2916:
	ldr	r3, .L3032+28
	movw	lr, #65535
	ldr	r5, [sp, #20]
	mov	r1, #36
	mov	r4, #0
	str	r4, [sp, #16]
	ldrh	r6, [r3]
	ldr	r3, .L3032+12
	ldr	r7, [r3, #-3036]
	ldr	r3, .L3032+8
	ldrb	r8, [r3, #3904]	@ zero_extendqisi2
.L2917:
	uxth	r3, r4
	cmp	r3, r6
	bcs	.L3030
	ldrh	r3, [r5, #2]!
	cmp	r3, lr
	beq	.L2918
	ldr	r2, [sp, #16]
	orr	r3, r10, r3, asl #10
	mla	r2, r1, r2, r7
	str	r3, [r2, #4]
	ldrb	r0, [r9, #8]	@ zero_extendqisi2
	cmp	r0, #1
	bne	.L2919
	cmp	r8, #0
	orrne	r3, r3, #-2147483648
	strne	r3, [r2, #4]
.L2919:
	ldr	r3, [sp, #16]
	add	ip, r3, #1
	uxth	r3, ip
	str	r3, [sp, #16]
.L2918:
	add	r4, r4, #1
	b	.L2917
.L3030:
	ldr	r4, .L3032+12
	ldr	r1, [sp, #16]
	ldr	r2, [sp, #24]
	ldr	r0, [r4, #-3036]
	bl	FlashReadPages
	mov	r3, #0
.L3023:
	str	r3, [sp, #28]
	ldr	r2, [sp, #16]
	ldrh	r3, [sp, #28]
	cmp	r3, r2
	bcs	.L3031
	ldr	r3, [sp, #28]
	mov	r5, #36
	ldr	r8, [r4, #-3036]
	mul	r5, r5, r3
	add	r7, r8, r5
	ldr	r6, [r7, #4]
	ubfx	r0, r6, #10, #16
	str	r6, [sp, #56]
	bl	P2V_plane
	ldr	r3, [sp, #8]
	cmp	r10, r3
	bcc	.L2922
	ldr	r3, [sp, #32]
	ldr	r2, [sp, #8]
	cmp	r0, r3
	movcs	r3, #0
	movcc	r3, #1
	cmp	r10, r2
	movne	r3, #0
	cmp	r3, #0
	bne	.L2922
	ldr	r3, [sp, #12]
	ldr	r2, [sp, #4]
	cmp	r0, r3
	cmpeq	r10, r2
	beq	.L2923
	ldr	r3, [r8, r5]
	cmn	r3, #1
	beq	.L2924
	ldr	r7, [r7, #12]
	movw	r3, #61589
	ldrh	r2, [r7]
	cmp	r2, r3
	ldrneh	r0, [r9]
	bne	.L3021
	ldr	r3, [r7, #4]
	cmn	r3, #1
	str	r3, [sp]
	beq	.L2926
	mov	r0, r3
	ldr	r1, [r4, #-3144]
	bl	ftl_cmp_data_ver
	cmp	r0, #0
	ldrne	r3, [sp]
	addne	r3, r3, #1
	strne	r3, [r4, #-3144]
.L2926:
	ldr	r6, [r7, #8]
	add	r1, sp, #52
	ldr	r3, [r7, #12]
	mov	r2, #0
	mov	r0, r6
	str	r3, [sp, #48]
	bl	log2phys
	ldr	r1, [fp, #228]
	cmn	r1, #1
	beq	.L2927
	ldr	r0, [sp]
	bl	ftl_cmp_data_ver
	cmp	r0, #0
	beq	.L2927
	ldr	r3, [sp, #48]
	cmn	r3, #1
	beq	.L2928
	ldr	r0, [r4, #-3036]
	mov	r2, #0
	mov	r1, #1
	add	r0, r0, r5
	str	r3, [r0, #4]
	ldr	r8, [r0, #12]
	bl	FlashReadPages
	ldr	r2, [r4, #-3036]
	ldr	r3, [r8, #4]
	add	ip, r2, r5
	str	r3, [sp, #36]
	ldr	r3, [r2, r5]
	cmn	r3, #1
	bne	.L2929
	b	.L2930
.L2928:
	ldr	r3, [sp, #56]
	ldr	r2, [sp, #52]
	cmp	r2, r3
	bne	.L2922
	mov	r0, r6
	add	r1, sp, #48
	mov	r2, #1
	bl	log2phys
	b	.L2922
.L2929:
	ldr	r7, [r8, #8]
	cmp	r7, r6
	bne	.L2930
	ldr	r0, [fp, #228]
	ldr	r1, [sp, #36]
	str	r2, [sp, #44]
	str	ip, [sp, #40]
	bl	ftl_cmp_data_ver
	cmp	r0, #0
	ldr	ip, [sp, #40]
	ldr	r2, [sp, #44]
	beq	.L2930
	ldr	r3, [sp, #52]
	ldr	r1, [sp, #56]
	cmp	r3, r1
	beq	.L2935
	ldr	r1, [sp, #48]
	cmp	r3, r1
	beq	.L2930
	cmn	r3, #1
	streq	r3, [r2, r5]
	beq	.L2934
	str	r3, [ip, #4]
	mov	r0, ip
	mov	r1, #1
	mov	r2, #0
	ldr	r8, [ip, #12]
	bl	FlashReadPages
.L2934:
	ldr	r3, [r4, #-3036]
	ldr	r3, [r3, r5]
	cmn	r3, #1
	beq	.L2935
	ldr	r5, [r8, #4]
	ldr	r0, [fp, #228]
	mov	r1, r5
	bl	ftl_cmp_data_ver
	cmp	r0, #0
	beq	.L2935
	ldr	r0, [sp, #36]
	mov	r1, r5
	bl	ftl_cmp_data_ver
	cmp	r0, #0
	beq	.L2930
.L2935:
	mov	r0, r7
	ldr	r1, [sp, #48]
	bl	FtlReUsePrevPpa
.L2930:
	mvn	r3, #0
	str	r3, [sp, #48]
	b	.L2937
.L2927:
	ldr	r3, [sp, #56]
	ldr	r2, [sp, #52]
	cmp	r2, r3
	beq	.L2937
	ldr	r3, [sp, #48]
	cmn	r3, #1
	beq	.L2939
	ldr	r2, [r4, #-3252]
	ubfx	r3, r3, #10, #21
	cmp	r3, r2
	bcs	.L2922
.L2939:
	mov	r0, r6
	add	r1, sp, #56
	mov	r2, #1
	bl	log2phys
	ldr	r8, [sp, #52]
	cmn	r8, #1
	beq	.L2937
	ldr	r3, [sp, #48]
	cmp	r8, r3
	beq	.L2937
	ubfx	r0, r8, #10, #16
	ldr	r5, .L3032+40
	bl	P2V_block_in_plane
	ldrh	r3, [r5]
	add	r5, r5, #648
	cmp	r3, r0
	beq	.L2941
	sub	r3, r5, #600
	ldrh	r3, [r3]
	cmp	r3, r0
	beq	.L2941
	sub	r3, r5, #552
	ldrh	r3, [r3]
	cmp	r3, r0
	bne	.L2937
.L2941:
	ldr	r0, [r5, #-3036]
	mov	r1, #1
	mov	r2, #0
	str	r8, [r0, #4]
	ldr	r7, [r0, #12]
	bl	FlashReadPages
	ldr	r3, [r5, #-3036]
	ldr	r1, [r7, #4]
	ldr	r3, [r3]
	cmn	r3, #1
	beq	.L2937
	ldr	r0, [sp]
	bl	ftl_cmp_data_ver
	cmp	r0, #0
	bne	.L2937
	mov	r0, r6
	add	r1, sp, #52
	mov	r2, #1
	bl	log2phys
.L2937:
	ldr	r0, [sp, #48]
	cmn	r0, #1
	beq	.L2922
	ubfx	r0, r0, #10, #16
	bl	P2V_block_in_plane
	ldr	r2, [r4, #-2940]
	mov	r3, r0, asl #1
	mov	r1, r0
	ldrh	r3, [r2, r3]
	cmp	r3, #0
	beq	.L2942
.L3021:
	bl	decrement_vpc_count
	b	.L2922
.L2942:
	ldr	r0, .L3032+44
	bl	printk
	b	.L2922
.L2924:
	ldrh	r2, [r9]
	mov	r1, r6
	ldr	r3, .L3032+20
	ldr	r0, .L3032+48
	strh	r2, [r3]	@ movhi
	ldr	r2, [sp]
	bl	printk
	ldr	r3, [fp, #276]
	cmp	r3, #31
	addls	r2, fp, r3, asl #2
	addls	r3, r3, #1
	strls	r3, [fp, #276]
	ldrls	r1, [sp, #56]
	strls	r1, [r2, #280]
	ldrh	r0, [r9]
	bl	decrement_vpc_count
	ldr	r3, .L3032+24
	ldr	r3, [r3, #228]
	cmn	r3, #1
	ldreq	r3, [sp]
	beq	.L3022
	ldr	r2, [sp]
	cmp	r3, r2
	bls	.L2922
	mov	r3, r2
.L3022:
	str	r3, [fp, #228]
.L2922:
	ldr	r3, [sp, #28]
	add	r3, r3, #1
	b	.L3023
.L3031:
	ldrb	r3, [r9, #8]	@ zero_extendqisi2
	add	r10, r10, #1
	cmp	r3, #1
	uxth	r10, r10
	bne	.L2946
	ldr	r3, .L3032+8
	ldrb	r3, [r3, #3904]	@ zero_extendqisi2
	cmp	r3, #0
	beq	.L2946
	ldr	r3, .L3032+52
	ldr	r2, [sp, #4]
	ldrh	r3, [r3]
	cmp	r2, r10
	cmpeq	r3, r10
	beq	.L2923
.L2946:
	ldr	r2, .L3032+4
	ldrh	r3, [r2]
	cmp	r10, r3
	bne	.L2916
	ldrh	r1, [r2, #-68]
	movw	r0, #65535
	mov	r3, #0
	strh	r10, [r9, #2]	@ movhi
	strh	r3, [r9, #4]	@ movhi
.L2947:
	uxth	r2, r3
	cmp	r2, r1
	bcs	.L2874
	ldr	lr, [sp, #20]
	add	r3, r3, #1
	ldrh	ip, [lr, #2]!
	cmp	ip, r0
	str	lr, [sp, #20]
	beq	.L2947
	strb	r2, [r9, #6]
	b	.L2874
.L2923:
	ldrb	r3, [sp, #12]	@ zero_extendqisi2
	mov	r0, r9
	ldr	r1, [sp, #4]
	strb	r3, [r9, #6]
	ldrh	r3, [sp, #4]
	strh	r3, [r9, #2]	@ movhi
.L3025:
	ldr	r2, [sp, #12]
	bl	ftl_sb_update_avl_pages
.L2874:
	ldr	r3, .L3032
	mov	r0, #0
	ldr	r2, [sp, #60]
	ldr	r3, [r3]
	cmp	r2, r3
	beq	.L2951
	bl	__stack_chk_fail
.L2951:
	add	sp, sp, #68
	@ sp needed
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L3033:
	.align	2
.L3032:
	.word	__stack_chk_guard
	.word	.LANCHOR2-3200
	.word	.LANCHOR0
	.word	.LANCHOR2
	.word	.LC150
	.word	.LANCHOR4+270
	.word	.LANCHOR4
	.word	.LANCHOR2-3268
	.word	.LANCHOR0+588
	.word	.LANCHOR4+272
	.word	.LANCHOR2-648
	.word	.LC151
	.word	.LC152
	.word	.LANCHOR2-3198
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
	ldr	r5, .L3048
	ldr	r7, .L3048+4
	add	r3, r0, r3, asl #1
	add	r6, r5, #588
	ldrh	r3, [r3, #16]
.L3038:
	movw	r1, #65535
	cmp	r3, r1
	bne	.L3047
.L3040:
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
	b	.L3038
.L3047:
	ldrb	r2, [r4, #8]	@ zero_extendqisi2
	cmp	r2, #1
	bne	.L3041
	ldrb	r3, [r5, #3904]	@ zero_extendqisi2
	cmp	r3, #0
	bne	.L3041
	ldrh	r3, [r4, #2]
	mov	r3, r3, asl #1
	ldrh	r3, [r6, r3]
	cmp	r3, r1
	bne	.L3041
	ldrh	r3, [r4, #4]
	ldrh	r0, [r4]
	sub	r3, r3, #1
	strh	r3, [r4, #4]	@ movhi
	bl	decrement_vpc_count
	ldrh	r2, [r4, #4]
	cmp	r2, #0
	bne	.L3040
	ldrh	r3, [r4, #2]
	add	r3, r3, #1
	b	.L3046
.L3041:
	ldr	r3, .L3048
	ldrb	r3, [r3, #3904]	@ zero_extendqisi2
	cmp	r3, #0
	ldmeqfd	sp!, {r3, r4, r5, r6, r7, pc}
	cmp	r2, #1
	ldmnefd	sp!, {r3, r4, r5, r6, r7, pc}
	ldr	r3, .L3048+8
	ldrh	r1, [r4, #2]
	sub	r2, r3, #3184
	ldrh	r2, [r2, #-14]
	cmp	r1, r2
	ldmccfd	sp!, {r3, r4, r5, r6, r7, pc}
	ldrh	r2, [r4]
	sub	r3, r3, #3200
	ldr	r0, [r3, #260]
	ldrh	ip, [r4, #4]
	mov	r2, r2, asl #1
	ldrh	r1, [r0, r2]
	rsb	r1, ip, r1
	strh	r1, [r0, r2]	@ movhi
	ldrh	r3, [r3]
	mov	r2, #0
	strh	r2, [r4, #4]	@ movhi
.L3046:
	strh	r3, [r4, #2]	@ movhi
	strb	r2, [r4, #6]
	ldmfd	sp!, {r3, r4, r5, r6, r7, pc}
.L3049:
	.align	2
.L3048:
	.word	.LANCHOR0
	.word	.LANCHOR2-3268
	.word	.LANCHOR2
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
	ldr	r7, .L3068
	ldr	r8, .L3068+4
	add	r3, r0, r3, asl #1
	add	r9, r7, #588
	ldrh	r2, [r3, #16]
.L3051:
	movw	r1, #65535
	cmp	r2, r1
	ldr	r6, .L3068+4
	bne	.L3067
.L3052:
	ldrb	r3, [r4, #6]	@ zero_extendqisi2
	ldrh	r2, [r8]
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
	b	.L3051
.L3067:
	ldrb	r3, [r4, #8]	@ zero_extendqisi2
	ldrh	r5, [r4, #2]
	cmp	r3, #1
	ldrh	r3, [r4, #4]
	bne	.L3054
	ldrb	r0, [r7, #3904]	@ zero_extendqisi2
	cmp	r0, #0
	bne	.L3054
	mov	r0, r5, asl #1
	ldrh	r0, [r9, r0]
	cmp	r0, r1
	bne	.L3054
	sub	r3, r3, #1
	ldrh	r0, [r4]
	strh	r3, [r4, #4]	@ movhi
	bl	decrement_vpc_count
	b	.L3052
.L3054:
	ldr	r7, .L3068
	orr	r5, r5, r2, asl #10
	sub	r3, r3, #1
	strh	r3, [r4, #4]	@ movhi
.L3055:
	ldrb	r3, [r4, #6]	@ zero_extendqisi2
	movw	r2, #65535
	ldrh	r0, [r6]
.L3057:
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
	beq	.L3057
	strb	r3, [r4, #6]
	ldrb	r3, [r4, #8]	@ zero_extendqisi2
	cmp	r3, #1
	bne	.L3062
	ldrb	r3, [r7, #3904]	@ zero_extendqisi2
	ldr	r2, .L3068
	cmp	r3, #0
	bne	.L3059
	ldrh	r3, [r4, #2]
	add	r1, r2, #588
	mov	r3, r3, asl #1
	ldrh	r1, [r1, r3]
	movw	r3, #65535
	cmp	r1, r3
	bne	.L3059
	ldrh	r3, [r4, #4]
	cmp	r3, #0
	beq	.L3059
	sub	r3, r3, #1
	ldrh	r0, [r4]
	strh	r3, [r4, #4]	@ movhi
	bl	decrement_vpc_count
	b	.L3055
.L3059:
	ldrb	r3, [r2, #3904]	@ zero_extendqisi2
	cmp	r3, #0
	beq	.L3062
	ldr	r3, .L3068+8
	ldrh	r1, [r4, #2]
	sub	r2, r3, #3184
	ldrh	r2, [r2, #-14]
	cmp	r1, r2
	bcc	.L3062
	ldrh	r2, [r4]
	sub	r3, r3, #3200
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
.L3062:
	mov	r0, r5
	ldmfd	sp!, {r3, r4, r5, r6, r7, r8, r9, pc}
.L3069:
	.align	2
.L3068:
	.word	.LANCHOR0
	.word	.LANCHOR2-3268
	.word	.LANCHOR2
	.fnend
	.size	get_new_active_ppa, .-get_new_active_ppa
	.align	2
	.global	FtlWriteDumpData
	.type	FtlWriteDumpData, %function
FtlWriteDumpData:
	.fnstart
	@ args = 0, pretend = 0, frame = 56
	@ frame_needed = 0, uses_anonymous_args = 0
	stmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.save {r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.pad #60
	sub	sp, sp, #60
	ldr	r8, .L3090
	ldr	r4, .L3090+4
	ldr	r3, [r8]
	str	r8, [sp, #4]
	str	r3, [sp, #52]
	ldr	r3, [r4, #-2812]
	cmp	r3, #0
	bne	.L3070
	sub	r7, r4, #648
	ldrh	r5, [r7, #4]
	cmp	r5, #0
	beq	.L3072
	ldrb	r3, [r4, #-640]	@ zero_extendqisi2
	cmp	r3, #0
	bne	.L3072
	sub	r3, r4, #3200
	ldrb	r2, [r4, #-641]	@ zero_extendqisi2
	ldrh	r3, [r3]
	mul	r3, r3, r2
	cmp	r5, r3
	beq	.L3072
	ldrb	r9, [r4, #-638]	@ zero_extendqisi2
	sub	r3, r4, #3264
	ldr	r6, [r4, #-2816]
	cmp	r9, #0
	ldrh	r10, [r3, #-4]
	bne	.L3070
	sub	r6, r6, #1
	add	r1, sp, #12
	mov	r2, r9
	mov	r0, r6
	bl	log2phys
	ldr	r5, [r4, #-2976]
	ldr	r0, [r4, #-3008]
	ldr	r3, [sp, #12]
	str	r6, [sp, #32]
	cmn	r3, #1
	str	r0, [sp, #24]
	str	r3, [sp, #20]
	str	r5, [sp, #28]
	str	r9, [r5, #4]
	beq	.L3074
	mov	r1, #1
	mov	r2, r9
	add	r0, sp, #16
	bl	FlashReadPages
	b	.L3075
.L3074:
	sub	r3, r4, #3184
	mov	r1, #255
	ldrh	r2, [r3, #-6]
	bl	ftl_memset
.L3075:
	ldr	r3, .L3090+8
	mov	r9, #0
	ldr	r8, .L3090+4
	mov	r10, r10, asl #2
	mov	fp, r9
	strh	r3, [r5]	@ movhi
.L3076:
	cmp	r9, r10
	beq	.L3077
	ldrh	r3, [r7, #4]
	ldr	r0, .L3090+12
	cmp	r3, #0
	beq	.L3077
	ldr	r3, [sp, #20]
	add	r9, r9, #1
	str	r6, [r5, #8]
	str	r3, [r5, #12]
	ldrh	r3, [r0]
	strh	r3, [r5, #2]	@ movhi
	bl	get_new_active_ppa
	ldr	r3, [r8, #-3144]
	mov	r2, #0
	mov	r1, #1
	str	r0, [sp, #20]
	add	r0, sp, #16
	str	r3, [r5, #4]
	add	r3, r3, #1
	cmn	r3, #1
	moveq	r3, fp
	str	r3, [r8, #-3144]
	mov	r3, r2
	bl	FlashProgPages
	ldrh	r0, [r7]
	bl	decrement_vpc_count
	b	.L3076
.L3077:
	mov	r3, #1
	b	.L3089
.L3072:
	mov	r3, #0
.L3089:
	strb	r3, [r4, #-638]
.L3070:
	ldr	r3, [sp, #4]
	ldr	r2, [sp, #52]
	ldr	r3, [r3]
	cmp	r2, r3
	beq	.L3081
	bl	__stack_chk_fail
.L3081:
	add	sp, sp, #60
	@ sp needed
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L3091:
	.align	2
.L3090:
	.word	__stack_chk_guard
	.word	.LANCHOR2
	.word	-3947
	.word	.LANCHOR2-648
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
	ldr	r6, .L3098
	mov	r7, #12
	ldr	r5, .L3098+4
.L3093:
	ldrh	r3, [r5]
	uxth	r0, r4
	cmp	r3, r0
	bls	.L3097
	ldr	r3, [r6, #-2896]
	mla	r3, r7, r0, r3
	ldr	r3, [r3, #4]
	cmp	r3, #0
	bge	.L3094
	bl	flush_l2p_region
.L3094:
	add	r4, r4, #1
	b	.L3093
.L3097:
	mov	r0, #0
	ldmfd	sp!, {r3, r4, r5, r6, r7, pc}
.L3099:
	.align	2
.L3098:
	.word	.LANCHOR2
	.word	.LANCHOR2-3162
	.fnend
	.size	l2p_flush, .-l2p_flush
	.align	2
	.global	FtlSuperblockPowerLostFix
	.type	FtlSuperblockPowerLostFix, %function
FtlSuperblockPowerLostFix:
	.fnstart
	@ args = 0, pretend = 0, frame = 40
	@ frame_needed = 0, uses_anonymous_args = 0
	stmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.save {r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.pad #44
	sub	sp, sp, #44
	ldr	r8, .L3118
	ldr	r5, .L3118+4
	ldr	r3, [r8]
	str	r3, [sp, #36]
	ldr	r3, [r5, #-2812]
	cmp	r3, #0
	bne	.L3100
	ldr	r2, .L3118+8
	mov	r4, r0
	ldrb	r6, [r2, #3904]	@ zero_extendqisi2
	cmp	r6, #0
	beq	.L3117
	ldrb	r6, [r0, #8]	@ zero_extendqisi2
	cmp	r6, #1
	ldreqh	fp, [r0, #4]
	beq	.L3102
	mov	r6, r3
.L3117:
	mov	fp, #12
.L3102:
	ldr	r7, [r5, #-2976]
	mvn	r3, #0
	str	r3, [sp, #16]
	mov	r9, #0
	ldr	r3, [r5, #-3008]
	movw	r2, #61589
	str	r7, [sp, #12]
	ldr	r10, .L3118+4
	str	r3, [sp, #8]
	mvn	r3, #2
	str	r3, [r7, #8]
	mvn	r3, #1
	str	r3, [r7, #12]
	ldrh	r3, [r4]
	strh	r9, [r7]	@ movhi
	strh	r3, [r7, #2]	@ movhi
	ldr	r3, [r5, #-3008]
	str	r2, [r3]
	ldr	r2, .L3118+12
	ldr	r3, [r5, #-3008]
	str	r2, [r3, #4]
.L3103:
	subs	fp, fp, #1
	bcc	.L3106
	ldrh	r3, [r4, #4]
	cmp	r3, #0
	bne	.L3104
.L3106:
	ldrh	r3, [r4]
	ldr	r1, [r5, #-2940]
	ldrh	r0, [r4, #4]
	mov	r3, r3, asl #1
	ldrh	r2, [r1, r3]
	rsb	r2, r0, r2
	strh	r2, [r1, r3]	@ movhi
	ldr	r3, .L3118+16
	ldrh	r3, [r3]
	strh	r3, [r4, #2]	@ movhi
	mov	r3, #0
	strb	r3, [r4, #6]
	strh	r3, [r4, #4]	@ movhi
	b	.L3100
.L3104:
	mov	r0, r4
	bl	get_new_active_ppa
	cmn	r0, #1
	str	r0, [sp, #4]
	beq	.L3106
	ldr	r3, [r5, #-3144]
	mov	r1, #1
	mov	r2, r6
	mov	r0, sp
	str	r3, [r7, #4]
	add	r3, r3, #1
	cmn	r3, #1
	moveq	r3, r9
	str	r3, [r10, #-3144]
	mov	r3, #0
	bl	FlashProgPages
	ldrh	r0, [r4]
	bl	decrement_vpc_count
	b	.L3103
.L3100:
	ldr	r2, [sp, #36]
	ldr	r3, [r8]
	cmp	r2, r3
	beq	.L3110
	bl	__stack_chk_fail
.L3110:
	add	sp, sp, #44
	@ sp needed
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L3119:
	.align	2
.L3118:
	.word	__stack_chk_guard
	.word	.LANCHOR2
	.word	.LANCHOR0
	.word	305419896
	.word	.LANCHOR2-3200
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
	ldr	r7, .L3137
	ldr	r4, .L3137+4
	ldr	r1, .L3137+8
	ldr	r3, [r7]
	ldr	r0, .L3137+12
	mov	r6, r4
	str	r3, [sp, #4]
	bl	printk
	sub	r3, r4, #3248
	ldr	r0, [r4, #-2944]
	mov	r1, #0
	ldrh	r2, [r3, #-10]
	mov	r2, r2, asl #1
	bl	ftl_memset
.L3121:
	ldr	r3, [r4, #-2816]
	cmp	r5, r3
	bcs	.L3135
	mov	r0, r5
	mov	r1, sp
	mov	r2, #0
	bl	log2phys
	ldr	r0, [sp]
	cmn	r0, #1
	beq	.L3122
	ubfx	r0, r0, #10, #16
	bl	P2V_block_in_plane
	ldr	r2, [r6, #-2944]
	mov	r0, r0, asl #1
	ldrh	r3, [r2, r0]
	add	r3, r3, #1
	strh	r3, [r2, r0]	@ movhi
.L3122:
	add	r5, r5, #1
	b	.L3121
.L3135:
	ldr	r6, .L3137+4
	mov	r8, #0
	ldr	r9, .L3137+16
.L3124:
	ldrh	r3, [r9]
	uxth	r4, r8
	cmp	r3, r4
	bls	.L3136
	ldr	r3, [r6, #-2940]
	mov	r5, r4, asl #1
	movw	r1, #65535
	ldrh	r2, [r3, r5]
	ldr	r3, [r6, #-2944]
	ldrh	r3, [r3, r5]
	cmp	r2, r3
	cmpne	r2, r1
	beq	.L3126
	ldr	r0, .L3137+20
	mov	r1, r4
	bl	printk
	ldr	r3, .L3137+24
	ldrh	r2, [r3]
	cmp	r2, r4
	beq	.L3126
	ldrh	r2, [r3, #96]
	cmp	r2, r4
	beq	.L3126
	ldrh	r3, [r3, #48]
	cmp	r3, r4
	beq	.L3126
	ldr	r3, [r6, #-2940]
	ldrh	r2, [r3, r5]
	cmp	r2, #0
	ldr	r2, [r6, #-2944]
	ldrh	r2, [r2, r5]
	strh	r2, [r3, r5]	@ movhi
	beq	.L3126
	mov	r0, r4
	bl	update_vpc_list
.L3126:
	add	r8, r8, #1
	b	.L3124
.L3136:
	bl	l2p_flush
	bl	FtlVpcTblFlush
	ldr	r2, [sp, #4]
	ldr	r3, [r7]
	cmp	r2, r3
	beq	.L3130
	bl	__stack_chk_fail
.L3130:
	add	sp, sp, #12
	@ sp needed
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, pc}
.L3138:
	.align	2
.L3137:
	.word	__stack_chk_guard
	.word	.LANCHOR2
	.word	.LANCHOR3+84
	.word	.LC48
	.word	.LANCHOR2-3260
	.word	.LC153
	.word	.LANCHOR2-648
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
	ldr	r5, .L3166
	ldrh	r7, [r0]
	ldr	r3, [r5, #-2812]
	cmp	r3, #0
	bne	.L3140
	movw	r3, #65535
	cmp	r7, r3
	mov	r4, r0
	beq	.L3141
	ldr	r2, [r5, #-2940]
	mov	r3, r7, asl #1
	mov	r0, r7
	ldrh	r3, [r2, r3]
	cmp	r3, #0
	beq	.L3142
	bl	INSERT_DATA_LIST
	b	.L3141
.L3142:
	bl	INSERT_FREE_LIST
.L3141:
	mov	r3, #0
	strb	r3, [r4, #8]
	ldr	r3, .L3166
	sub	r2, r3, #600
	cmp	r4, r2
	beq	.L3143
	sub	r2, r3, #3248
	ldrh	r2, [r2]
	cmp	r2, #1
	beq	.L3143
	ldr	r1, .L3166+4
	ldrb	r1, [r1, #3904]	@ zero_extendqisi2
	cmp	r1, #0
	beq	.L3144
.L3143:
	mov	r3, #1
	strb	r3, [r4, #8]
	b	.L3145
.L3144:
	sub	r1, r3, #648
	cmp	r4, r1
	bne	.L3145
	cmp	r2, #3
	beq	.L3147
	ldr	r3, [r3, #-3152]
	cmp	r3, #1
	bne	.L3148
.L3147:
	mov	r3, #1
	strb	r3, [r5, #-640]
.L3148:
	ldr	r2, [r5, #-3336]
	ldr	r3, .L3166
	cmp	r2, #0
	beq	.L3145
	ldr	r2, [r3, #-3100]
	cmp	r2, #39
	movls	r2, #1
	strlsb	r2, [r3, #-640]
.L3145:
	ldr	r3, .L3166+8
	movw	r2, #65535
	ldrh	r0, [r3, #216]
	mov	r6, r3
	cmp	r0, r2
	beq	.L3150
	cmp	r7, r0
	bne	.L3151
	ldr	r2, [r5, #-2940]
	mov	r3, r0, asl #1
	ldrh	r3, [r2, r3]
	cmp	r3, #0
	beq	.L3152
.L3151:
	bl	update_vpc_list
.L3152:
	mvn	r3, #0
	strh	r3, [r6, #216]	@ movhi
.L3150:
	mov	r0, r4
	bl	allocate_data_superblock
	bl	l2p_flush
	mov	r0, #0
	bl	FtlEctTblFlush
	bl	FtlVpcTblFlush
.L3140:
	mov	r0, #0
	ldmfd	sp!, {r3, r4, r5, r6, r7, pc}
.L3167:
	.align	2
.L3166:
	.word	.LANCHOR2
	.word	.LANCHOR0
	.word	.LANCHOR4
	.fnend
	.size	allocate_new_data_superblock, .-allocate_new_data_superblock
	.align	2
	.global	FtlReadRefresh
	.type	FtlReadRefresh, %function
FtlReadRefresh:
	.fnstart
	@ args = 0, pretend = 0, frame = 48
	@ frame_needed = 0, uses_anonymous_args = 0
	stmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.save {r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.pad #52
	sub	sp, sp, #52
	ldr	r8, .L3186
	ldr	r6, .L3186+4
	ldr	r5, .L3186+8
	ldr	fp, [r8, #-240]
	mov	r7, r8
	ldr	r3, [r6]
	cmp	fp, #0
	str	r3, [sp, #44]
	beq	.L3169
	ldr	r2, [r8, #-236]
	ldr	r3, [r5, #-2816]
	cmp	r2, r3
	bcs	.L3170
	mov	r4, #2048
.L3175:
	ldr	r0, [r7, #-236]
	ldr	r3, [r5, #-2816]
	cmp	r0, r3
	bcs	.L3174
	mov	r2, #0
	add	r1, sp, #4
	bl	log2phys
	ldr	r3, [r8, #-236]
	add	r3, r3, #1
	str	r3, [r8, #-236]
	ldr	r2, [sp, #4]
	cmn	r2, #1
	beq	.L3173
	add	r0, sp, #48
	str	r2, [sp, #12]
	mov	r1, #1
	mov	r2, #0
	str	r2, [r0, #-40]!
	str	r3, [sp, #24]
	str	r2, [sp, #16]
	str	r2, [sp, #20]
	bl	FlashReadPages
	ldr	r3, [sp, #8]
	cmp	r3, #256
	bne	.L3174
	ldr	r0, [sp, #4]
	ubfx	r0, r0, #10, #16
	bl	P2V_block_in_plane
	bl	FtlGcRefreshBlock
.L3174:
	mvn	r0, #0
	b	.L3177
.L3173:
	subs	r4, r4, #1
	bne	.L3175
	b	.L3174
.L3170:
	ldr	r3, [r5, #-3120]
	mov	r0, #0
	str	r0, [r8, #-240]
	str	r0, [r8, #-236]
	str	r3, [r8, #-244]
	b	.L3177
.L3169:
	ldr	r1, [r5, #-3104]
	movw	r4, #10000
	ldr	r10, [r5, #-3120]
	sub	r5, r5, #716
	cmp	r1, r4
	ldr	r9, [r8, #-244]
	add	r2, r10, #1048576
	ldr	r3, [r5, #-2100]
	movhi	r4, #31
	movls	r4, #63
	cmp	r9, r2
	bhi	.L3179
	mov	r1, r1, lsr #10
	mov	r0, #1000
	mul	r0, r0, r3
	add	r1, r1, #1
	bl	__aeabi_uidiv
	add	r0, r0, r9
	cmp	r0, r10
	bcc	.L3179
	ldrh	r3, [r5, #28]
	ands	r0, r4, r3
	movne	r0, fp
	bne	.L3177
	ldr	r2, [r8, #-220]
	cmp	r2, r3
	beq	.L3177
.L3179:
	ldrh	r3, [r5, #28]
	mov	r0, #0
	str	r10, [r7, #-244]
	str	r0, [r7, #-236]
	str	r3, [r7, #-220]
	mov	r3, #1
	str	r3, [r7, #-240]
.L3177:
	ldr	r2, [sp, #44]
	ldr	r3, [r6]
	cmp	r2, r3
	beq	.L3180
	bl	__stack_chk_fail
.L3180:
	add	sp, sp, #52
	@ sp needed
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L3187:
	.align	2
.L3186:
	.word	.LANCHOR4
	.word	__stack_chk_guard
	.word	.LANCHOR2
	.fnend
	.size	FtlReadRefresh, .-FtlReadRefresh
	.align	2
	.global	ftl_do_gc
	.type	ftl_do_gc, %function
ftl_do_gc:
	.fnstart
	@ args = 0, pretend = 0, frame = 40
	@ frame_needed = 0, uses_anonymous_args = 0
	stmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.save {r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.pad #52
	sub	sp, sp, #52
	ldr	r4, .L3353
	ldr	r3, .L3353+4
	str	r0, [sp, #20]
	ldr	r0, [r4, #-2812]
	ldr	r3, [r3]
	cmp	r0, #0
	str	r3, [sp, #44]
	movne	r0, #0
	bne	.L3189
	ldr	r3, .L3353+8
	ldr	r2, [r3, #3284]
	cmn	r2, #1
	beq	.L3189
	ldr	r5, [r4, #-3096]
	cmp	r5, #0
	bne	.L3189
	sub	r2, r4, #660
	ldrh	r2, [r2]
	cmp	r2, #47
	movls	r0, r5
	bls	.L3189
	movw	r2, #3288
	mov	r10, r1
	ldrh	r2, [r3, r2]
	movw	r3, #65535
	mov	r7, r4
	cmp	r2, r3
	bne	.L3190
.L3193:
	ldr	r5, .L3353+12
	movw	r1, #65535
	ldrh	r4, [r5, #-14]
	cmp	r4, r1
	bne	.L3191
	b	.L3192
.L3190:
	sub	r4, r4, #552
	ldrh	r2, [r4]
	cmp	r2, r3
	beq	.L3193
	mov	r0, #1
	bl	FtlGcFreeTempBlock
	cmp	r0, #0
	beq	.L3193
	mov	r0, #1
	b	.L3189
.L3191:
	mov	r3, r5
	ldrh	r2, [r3, #-16]!
	cmp	r2, r1
	bne	.L3192
	ldrh	r0, [r5, #-12]
	cmp	r0, r2
	beq	.L3192
	ldrh	r1, [r5, #-10]
	cmp	r1, r2
	strneh	r4, [r3]	@ movhi
	strneh	r0, [r5, #-14]	@ movhi
	mvnne	r3, #0
	strneh	r1, [r5, #-12]	@ movhi
	strneh	r3, [r5, #-10]	@ movhi
.L3192:
	ldr	r2, [sp, #20]
	ldr	r3, [r7, #-3080]
	cmp	r2, #1
	ldr	r6, .L3353+16
	add	r3, r3, #1
	add	r3, r3, r2, asl #7
	str	r3, [r7, #-3080]
	bne	.L3204
	ldr	r2, .L3353
	ldr	r2, [r2, #-3336]
	cmp	r2, #0
	bne	.L3195
	ldr	r2, .L3353+20
	ldrb	r2, [r2, #3904]	@ zero_extendqisi2
	cmp	r2, #0
	beq	.L3204
.L3195:
	ldr	r2, [r7, #-3100]
	ldr	r4, .L3353
	cmp	r2, #39
	bhi	.L3204
	ldr	r8, .L3353+24
	ldrh	r2, [r8]
	add	r3, r3, r2
	str	r3, [r4, #-3080]
	bl	FtlGcReFreshBadBlk
	ldrh	r1, [r6]
	movw	r3, #65535
	cmp	r1, r3
	bne	.L3204
	sub	r3, r4, #3072
	ldrh	r3, [r3]
	cmp	r3, r1
	bne	.L3204
	ldr	r3, [r4, #-3080]
	cmp	r3, #1024
	sub	r3, r4, #652
	bhi	.L3196
	ldrh	r2, [r3]
	cmp	r2, #63
	bhi	.L3204
.L3196:
	ldrh	ip, [r3]
	mov	r1, #0
	ldrh	r3, [r5, #-6]
	ldr	r0, .L3353+24
	add	r3, r3, #64
	strh	r1, [r8]	@ movhi
	cmp	ip, r3
	ldr	r2, .L3353
	bgt	.L3204
	ldr	r3, [r2, #-3100]
	str	r1, [r2, #-3080]
	cmp	r3, r1
	moveq	r3, #6
	beq	.L3344
	cmp	r3, #5
	bhi	.L3198
	mov	r3, #18
.L3344:
	strh	r3, [r0]	@ movhi
.L3198:
	mov	r0, #32
	bl	List_get_gc_head_node
	movw	ip, #65535
	uxth	r2, r0
	cmp	r2, ip
	beq	.L3203
	ldrh	r0, [r5, #-4]
	ldr	r8, .L3353
	cmp	r0, #0
	sub	r9, r8, #3056
	beq	.L3200
	sub	r1, r8, #3184
	ldr	r3, [r8, #-2940]
	mov	r2, r2, asl #1
	ldrh	r4, [r1, #-14]
	sub	r1, r8, #3264
	ldrh	lr, [r3, r2]
	ldrh	r1, [r1, #-4]
	mul	r1, r1, r4
	add	r1, r1, #1
	cmp	lr, r1
	bgt	.L3203
	add	r1, r0, #1
	str	r2, [sp, #28]
	str	ip, [sp, #24]
	mov	fp, #0
	uxth	r1, r1
	str	r3, [sp, #16]
	strh	r1, [r9, #-4]	@ movhi
	str	r1, [sp, #12]
	str	fp, [r8, #-3092]
	bl	List_get_gc_head_node
	ldr	ip, [sp, #24]
	uxth	r4, r0
	ldr	r1, [sp, #12]
	cmp	r4, ip
	ldr	r3, [sp, #16]
	ldr	r2, [sp, #28]
	beq	.L3203
	mov	ip, r4, asl #1
	ldr	r0, .L3353+28
	str	ip, [sp, #12]
	ldrh	lr, [r3, ip]
	ldrh	r3, [r3, r2]
	mov	r2, r4
	str	r3, [sp]
	mov	r3, lr
	bl	printk
	ldrh	r3, [r9, #-4]
	cmp	r3, #40
	ldr	ip, [sp, #12]
	bls	.L3201
	ldr	r3, [r8, #-2940]
	ldrh	r3, [r3, ip]
	cmp	r3, #32
	strhih	fp, [r9, #-4]	@ movhi
.L3201:
	ldr	r3, .L3353+24
	mov	r2, #6
	strh	r2, [r3]	@ movhi
	b	.L3205
.L3200:
	mov	r3, #1
	strh	r3, [r9, #-4]	@ movhi
.L3203:
	bl	GetSwlReplaceBlock
	movw	r3, #65535
	cmp	r0, r3
	mov	r4, r0
	bne	.L3205
	ldr	r3, .L3353+24
	mov	r2, #0
	strh	r2, [r3]	@ movhi
.L3204:
	ldrh	r3, [r6]
	movw	r4, #65535
	cmp	r3, r4
	bne	.L3205
	ldr	r8, .L3353
	sub	r2, r8, #552
	ldrh	r4, [r2]
	cmp	r4, r3
	movne	r4, r3
	beq	.L3349
.L3205:
	movw	r0, #65535
	rsb	r3, r0, r4
	clz	r3, r3
	ldr	r2, [sp, #20]
	mov	r3, r3, lsr #5
	cmp	r2, #0
	movne	ip, #0
	andeq	ip, r3, #1
	cmp	ip, #0
	beq	.L3218
	ldr	r3, .L3353+32
	ldrh	r2, [r3]
	cmp	r2, #24
	movhi	fp, #1
	bhi	.L3219
	ldr	r3, .L3353+36
	cmp	r2, #16
	ldrhih	r3, [r3]
	movhi	fp, r3, lsr #5
	bhi	.L3219
	cmp	r2, #12
	ldrhih	r3, [r3]
	movhi	fp, r3, lsr #4
	bhi	.L3219
	cmp	r2, #8
	ldrhih	r3, [r3]
	ldrlsh	fp, [r3]
	movhi	fp, r3, lsr #2
.L3219:
	ldrh	r3, [r5, #-8]
	ldr	r1, .L3353
	cmp	r3, r2
	bcs	.L3223
	sub	r3, r1, #552
	movw	r2, #65535
	ldrh	r3, [r3]
	cmp	r3, r2
	bne	.L3224
	sub	r2, r1, #3072
	ldrh	r2, [r2]
	cmp	r2, r3
	bne	.L3224
	ldr	r3, .L3353+24
	ldrh	r0, [r3]
	cmp	r0, #0
	bne	.L3225
	ldr	r3, [r1, #-2816]
	ldr	r2, [r1, #-408]
	add	r3, r3, r3, asl #1
	cmp	r2, r3, lsr #2
	movcs	r3, #18
	bcs	.L3226
.L3225:
	ldr	r3, .L3353+40
	ldrh	r3, [r3]
	add	r3, r3, r3, asl #1
	ubfx	r3, r3, #2, #16
.L3226:
	strh	r3, [r5, #-8]	@ movhi
	mov	r3, #0
	str	r3, [r7, #-3092]
	b	.L3189
.L3224:
	ldr	r3, .L3353+40
	ldrh	r3, [r3]
	add	r3, r3, r3, asl #1
	mov	r3, r3, asr #2
	strh	r3, [r5, #-8]	@ movhi
.L3223:
	cmp	r10, #2
	ldr	r3, [r7, #-3336]
	movw	r4, #65535
	movhi	r10, #0
	movls	r10, #1
	cmp	r3, #0
	moveq	r10, #0
	cmp	r10, #0
	addne	r3, fp, #1
	uxthne	fp, r3
	b	.L3228
.L3218:
	ldr	r1, .L3353
	sub	r2, r1, #552
	ldrh	r2, [r2]
	cmp	r2, r0
	bne	.L3229
	sub	r0, r1, #3072
	ldrh	r0, [r0]
	cmp	r0, r2
	movne	r3, #0
	andeq	r3, r3, #1
	cmp	r3, #0
	beq	.L3229
	ldrh	r3, [r6]
	cmp	r3, r2
	beq	.L3230
.L3234:
	movw	r4, #65535
	b	.L3229
.L3230:
	sub	r4, r1, #652
	ldrh	r3, [r5, #-8]
	str	ip, [r1, #-3092]
	ldrh	r2, [r4]
	ldr	r9, .L3353+24
	cmp	r2, r3
	bls	.L3231
	ldrh	r3, [r9]
	cmp	r3, #0
	bne	.L3232
	ldr	r3, [r1, #-2816]
	ldr	r2, [r1, #-408]
	add	r3, r3, r3, asl #1
	cmp	r2, r3, lsr #2
	movcs	r3, #18
	bcs	.L3233
.L3232:
	ldr	r3, .L3353+40
	ldrh	r3, [r3]
	add	r3, r3, r3, asl #1
	ubfx	r3, r3, #2, #16
.L3233:
	strh	r3, [r5, #-8]	@ movhi
	bl	FtlReadRefresh
	mov	r0, #0
	bl	List_get_gc_head_node
	ldr	r3, [r7, #-2940]
	uxth	r0, r0
	mov	r0, r0, asl #1
	ldrh	r3, [r3, r0]
	cmp	r3, #4
	ldrhih	r0, [r9]
	bhi	.L3189
.L3231:
	ldrh	r0, [r9]
	ldr	r8, .L3353+24
	cmp	r0, #0
	bne	.L3234
	sub	r3, r8, #780
	ldrh	r9, [r3]
	add	r3, r9, r9, asl #1
	mov	r3, r3, asr #2
	strh	r3, [r5, #-8]	@ movhi
	bl	List_get_gc_head_node
	ldr	r3, [r7, #-2940]
	uxth	r0, r0
	mov	r0, r0, asl #1
	ldrh	r2, [r3, r0]
	ldr	r3, .L3353+44
	ldrh	r1, [r3, #-14]
	ldrh	r5, [r3, #-84]
	mul	r5, r5, r1
	add	r5, r5, r5, lsr #31
	cmp	r2, r5, asr #1
	ble	.L3235
	ldrh	r1, [r4]
	sub	r3, r9, #1
	cmp	r1, r3
	blt	.L3235
	bl	FtlReadRefresh
	ldrh	r0, [r8]
	b	.L3189
.L3235:
	cmp	r2, #0
	bne	.L3234
	movw	r0, #65535
	bl	decrement_vpc_count
	ldrh	r0, [r4]
	add	r0, r0, #1
	b	.L3189
.L3229:
	ldr	r3, [r7, #-3336]
	cmp	r3, #0
	moveq	fp, #1
	movne	fp, #2
.L3228:
	ldrh	r3, [r6]
	movw	r2, #65535
	cmp	r3, r2
	bne	.L3237
	cmp	r4, r3
	ldrne	r3, .L3353+16
	strneh	r4, [r3]	@ movhi
	bne	.L3239
	ldr	r1, .L3353
	sub	r3, r1, #3072
	ldrh	r2, [r3]
	cmp	r2, r4
	beq	.L3239
	ldr	r1, [r1, #-2940]
	mov	r2, r2, asl #1
	ldrh	r2, [r1, r2]
	cmp	r2, #0
	mvneq	r2, #0
	streqh	r2, [r3]	@ movhi
	ldrh	r2, [r3]
	strh	r2, [r6]	@ movhi
	mvn	r2, #0
	strh	r2, [r3]	@ movhi
.L3239:
	ldr	r3, .L3353+48
	mov	r2, #0
	ldrh	r0, [r6]
	sub	r8, r3, #368
	strb	r2, [r3, #-360]
	movw	r2, #65535
	cmp	r0, r2
	mov	r5, r3
	beq	.L3237
	bl	IsBlkInGcList
	cmp	r0, #0
	mvnne	r3, #0
	strneh	r3, [r8]	@ movhi
	ldr	r3, .L3353+20
	ldrb	r3, [r3, #3904]	@ zero_extendqisi2
	cmp	r3, #0
	beq	.L3243
	ldrh	r0, [r6]
	bl	ftl_get_blk_mode
	strb	r0, [r5, #-360]
.L3243:
	ldrh	r2, [r6]
	movw	r3, #65535
	ldr	r5, .L3353+48
	cmp	r2, r3
	sub	r8, r5, #368
	beq	.L3237
	mov	r0, r8
	bl	make_superblock
	movw	r2, #410
	mov	r3, #0
	strh	r3, [r5, r2]	@ movhi
	strh	r3, [r8, #2]	@ movhi
	strb	r3, [r5, #-362]
	ldrh	r3, [r8]
	ldr	r2, [r7, #-2940]
	mov	r3, r3, asl #1
	ldrh	r2, [r2, r3]
	add	r3, r5, #412
	strh	r2, [r3]	@ movhi
.L3237:
	ldr	r2, .L3353+52
	ldrh	r3, [r6]
	ldrh	r1, [r2]
	cmp	r1, r3
	beq	.L3244
	ldrh	r1, [r2, #48]
	cmp	r1, r3
	beq	.L3244
	ldrh	r2, [r2, #96]
	cmp	r2, r3
	bne	.L3245
.L3244:
	mvn	r3, #0
	strh	r3, [r6]	@ movhi
.L3245:
	ldr	r5, .L3353+48
.L3278:
	ldr	r8, .L3353+16
	movw	r3, #65535
	ldr	r9, .L3353
	ldrh	r2, [r8]
	mov	r10, r8
	cmp	r2, r3
	bne	.L3246
	mov	r3, #0
	str	r3, [r9, #-3092]
.L3247:
	ldr	r6, .L3353+56
	ldrh	r7, [r6]
	mov	r0, r7
	bl	List_get_gc_head_node
	movw	r2, #65535
	uxth	r3, r0
	strh	r3, [r10]	@ movhi
	cmp	r3, r2
	moveq	r3, #0
	moveq	r0, #8
	streqh	r3, [r6]	@ movhi
	beq	.L3189
.L3248:
	mov	r0, r3
	str	r3, [sp, #12]
	bl	IsBlkInGcList
	add	r7, r7, #1
	cmp	r0, #0
	ldr	r3, [sp, #12]
	strneh	r7, [r6]	@ movhi
	bne	.L3247
	ldr	r2, .L3353
	mov	r1, r3, asl #1
	ldrh	lr, [r6, #-208]
	uxth	r7, r7
	strh	r7, [r6]	@ movhi
	ldr	r0, [r2, #-2940]
	ldrh	r2, [r6, #-140]
	ldrh	ip, [r0, r1]
	mul	r2, lr, r2
	add	lr, r2, r2, lsr #31
	cmp	ip, lr, asr #1
	bgt	.L3251
	cmp	ip, #8
	cmphi	r7, #48
	bls	.L3252
	ldrh	ip, [r5, #232]
	cmp	ip, #35
	bhi	.L3252
.L3251:
	mov	ip, #0
	strh	ip, [r6]	@ movhi
.L3252:
	ldrh	r1, [r0, r1]
	movw	r0, #65535
	cmp	r1, r2
	cmpge	r4, r0
	bne	.L3253
	ldrh	r2, [r6]
	cmp	r2, #3
	bhi	.L3253
	mvn	r3, #0
	strh	r3, [r10]	@ movhi
	ldr	r3, .L3353+56
	mov	r2, #0
	strh	r2, [r3]	@ movhi
	ldr	r3, .L3353+24
	b	.L3347
.L3253:
	cmp	r1, #0
	bne	.L3254
	movw	r0, #65535
	bl	decrement_vpc_count
	ldrh	r3, [r6]
	add	r3, r3, #1
	strh	r3, [r6]	@ movhi
	b	.L3247
.L3254:
	ldr	r1, .L3353+48
	mov	r2, #0
	strb	r2, [r1, #-360]
	ldr	r2, .L3353+20
	ldrb	r2, [r2, #3904]	@ zero_extendqisi2
	cmp	r2, #0
	beq	.L3255
	mov	r0, r3
	bl	ftl_get_blk_mode
	strb	r0, [r5, #-360]
.L3255:
	ldr	r0, .L3353+16
	bl	make_superblock
	ldrh	r2, [r8]
	ldr	r1, .L3353+60
	mov	r3, #0
	ldr	r0, [r9, #-2940]
	mov	r2, r2, asl #1
	strh	r3, [r1]	@ movhi
	ldrh	r2, [r0, r2]
	strh	r3, [r8, #2]	@ movhi
	strh	r2, [r1, #2]	@ movhi
	ldr	r2, .L3353+48
	strb	r3, [r2, #-362]
.L3246:
	ldr	r3, [sp, #20]
	cmp	r3, #1
	bne	.L3256
	bl	FtlReadRefresh
.L3256:
	mov	r3, #1
	str	r3, [r9, #-3096]
	ldr	r3, .L3353+36
	ldrh	r2, [r3]
	str	r2, [sp, #12]
	ldr	r2, .L3353+20
	ldrb	r2, [r2, #3904]	@ zero_extendqisi2
	cmp	r2, #0
	beq	.L3257
	ldrb	r2, [r5, #-360]	@ zero_extendqisi2
	cmp	r2, #1
	ldreqh	r3, [r3, #2]
	streq	r3, [sp, #12]
.L3257:
	ldrh	r3, [r8, #2]
	ldr	r1, [sp, #12]
	add	r2, r3, fp
	ldr	r6, .L3353
	cmp	r2, r1
	movgt	r2, r1
	rsbgt	r3, r3, r2
	uxthgt	fp, r3
	mov	r3, #0
	str	r3, [sp, #16]
	b	.L3259
.L3352:
	ldr	r0, [r6, #-3020]
	mov	r1, r7
	ldrb	r2, [r5, #-360]	@ zero_extendqisi2
	mov	r9, #0
	bl	FlashReadPages
.L3262:
	uxth	r3, r9
	cmp	r3, r7
	bcs	.L3350
	mov	r3, #36
	ldr	r2, [r6, #-3020]
	mul	r8, r3, r9
	add	r1, r2, r8
	ldr	r2, [r2, r8]
	ldr	r10, [r1, #12]
	cmn	r2, #1
	beq	.L3299
	ldrh	r1, [r10]
	movw	r2, #61589
	cmp	r1, r2
	bne	.L3299
	add	r1, sp, r3
	mov	r2, #0
	ldr	r0, [r10, #8]
	str	r3, [sp, #24]
	bl	log2phys
	ldr	r2, [r6, #-3020]
	add	r2, r2, r8
	ldr	r0, [r2, #4]
	ldr	r1, [sp, #36]
	ldr	r3, [sp, #24]
	bic	r1, r1, #-2147483648
	cmp	r1, r0
	bne	.L3299
	ldr	r1, .L3353+60
	ldr	r0, .L3353+60
	ldr	r2, [r2, #16]
	ldrh	r1, [r1]
	str	r3, [sp, #28]
	add	r1, r1, #1
	strh	r1, [r0]	@ movhi
	ldr	r0, [r5, #208]
	ldr	r1, [r6, #-3032]
	mla	r1, r3, r0, r1
	str	r2, [r1, #16]
	str	r1, [sp, #24]
	bl	Ftl_get_new_temp_ppa
	ldr	r2, [r6, #-3032]
	ldr	r1, [sp, #24]
	ldr	r3, [sp, #28]
	str	r0, [r1, #4]
	ldr	r1, [r5, #208]
	mla	r3, r3, r1, r2
	ldr	r2, [r6, #-3020]
	add	r2, r2, r8
	ldr	r1, [r2, #8]
	str	r1, [r3, #8]
	mov	r1, #1
	ldr	r2, [r2, #12]
	str	r2, [r3, #12]
	ldr	r3, [sp, #36]
	str	r3, [r10, #12]
	ldr	r3, .L3353+64
	ldrh	r2, [r3]
	str	r3, [sp, #24]
	strh	r2, [r10, #2]	@ movhi
	ldr	r2, [r6, #-3144]
	ldr	r0, [r6, #-3020]
	str	r2, [r10, #4]
	add	r0, r0, r8
	ldr	r2, [r5, #208]
	add	r2, r2, #1
	str	r2, [r5, #208]
	bl	FtlGcBufAlloc
	ldr	r2, .L3353+20
	ldrb	r2, [r2, #3904]	@ zero_extendqisi2
	cmp	r2, #0
	beq	.L3351
.L3264:
	bl	Ftl_gc_temp_data_write_back
	cmp	r0, #0
	beq	.L3299
	ldr	r3, .L3353
	mov	r2, #0
	mvn	r1, #0
	str	r2, [r3, #-3096]
	ldr	r3, .L3353+16
	strh	r1, [r3]	@ movhi
	strh	r2, [r3, #2]	@ movhi
	add	r3, r3, #776
.L3347:
	ldrh	r0, [r3]
	b	.L3189
.L3350:
	ldr	r3, [sp, #16]
	add	r3, r3, #1
	str	r3, [sp, #16]
.L3259:
	ldrh	r3, [sp, #16]
	ldr	r8, .L3353
	cmp	r3, fp
	ldr	r7, .L3353+16
	bcs	.L3266
	ldr	r3, .L3353+68
	mov	r2, #0
	ldrh	r0, [r7, #2]
	movw	lr, #65535
	ldr	r8, [r6, #-3020]
	mov	ip, #36
	ldrh	r9, [r3]
	ldr	r3, [sp, #16]
	add	r0, r0, r3
	add	r3, r7, #14
	mov	r7, r2
.L3267:
	uxth	r1, r2
	cmp	r1, r9
	bcs	.L3352
	ldrh	r1, [r3, #2]!
	add	r2, r2, #1
	cmp	r1, lr
	orrne	r1, r0, r1, asl #10
	mlane	r10, ip, r7, r8
	addne	r7, r7, #1
	uxthne	r7, r7
	strne	r1, [r10, #4]
	b	.L3267
.L3351:
	ldrb	r2, [r6, #-545]	@ zero_extendqisi2
	ldr	r1, [r5, #208]
	cmp	r1, r2
	beq	.L3264
	ldr	r3, [sp, #24]
	ldrh	r3, [r3, #4]
	cmp	r3, #0
	beq	.L3264
.L3299:
	add	r9, r9, #1
	b	.L3262
.L3266:
	ldrh	r3, [r7, #2]
	ldr	r2, [sp, #12]
	add	r3, fp, r3
	uxth	r3, r3
	strh	r3, [r7, #2]	@ movhi
	cmp	r3, r2
	bcc	.L3268
	ldr	r3, [r5, #208]
	cmp	r3, #0
	beq	.L3269
	bl	Ftl_gc_temp_data_write_back
	cmp	r0, #0
	addne	r7, r7, #776
	movne	r3, #0
	strne	r3, [r8, #-3096]
	ldrneh	r0, [r7]
	bne	.L3189
.L3269:
	ldr	r3, .L3353+60
	ldrh	r6, [r3]
	cmp	r6, #0
	bne	.L3270
	ldrh	r3, [r7]
	ldr	r2, [r8, #-2940]
	mov	r3, r3, asl #1
	ldrh	r3, [r2, r3]
	cmp	r3, #0
	beq	.L3270
.L3271:
	ldr	r3, [r8, #-2816]
	cmp	r6, r3
	bcs	.L3276
	mov	r0, r6
	add	r1, sp, #40
	mov	r2, #0
	bl	log2phys
	ldr	r0, [sp, #40]
	cmn	r0, #1
	beq	.L3272
	ubfx	r0, r0, #10, #16
	bl	P2V_block_in_plane
	ldrh	r3, [r7]
	cmp	r3, r0
	bne	.L3272
.L3276:
	ldr	r3, [r8, #-2816]
	cmp	r6, r3
	bcc	.L3270
	ldr	r2, .L3353
	mov	r1, #0
	ldrh	r3, [r7]
	ldr	r2, [r2, #-2940]
	mov	r3, r3, asl #1
	strh	r1, [r2, r3]	@ movhi
	ldrh	r0, [r7]
	bl	update_vpc_list
	bl	FtlCacheWriteBack
	bl	l2p_flush
	bl	FtlVpcTblFlush
	b	.L3270
.L3272:
	add	r6, r6, #1
	b	.L3271
.L3270:
	mvn	r3, #0
	strh	r3, [r7]	@ movhi
.L3268:
	ldr	r7, .L3353
	sub	r3, r7, #652
	ldrh	r3, [r3]
	cmp	r3, #2
	subls	r7, r7, #3200
	ldrlsh	fp, [r7]
	bls	.L3278
.L3277:
	mov	r2, #0
	str	r2, [r7, #-3096]
	ldr	r2, .L3353+24
	ldrh	r0, [r2]
	cmp	r0, #0
	addeq	r0, r3, #1
.L3189:
	ldr	r3, .L3353+4
	ldr	r2, [sp, #44]
	ldr	r3, [r3]
	cmp	r2, r3
	beq	.L3282
	bl	__stack_chk_fail
.L3349:
	sub	r3, r8, #3072
	ldrh	fp, [r3]
	cmp	fp, r4
	bne	.L3205
	sub	r9, r8, #652
	ldr	r2, [r8, #-3080]
	ldrh	r3, [r9]
	cmp	r3, #24
	movcc	r3, #5120
	movcs	r3, #1024
	cmp	r2, r3
	movls	r4, fp
	bls	.L3205
	ldr	r2, .L3353+24
	mov	r3, #0
	str	r3, [r7, #-3080]
	strh	r3, [r2]	@ movhi
	bl	GetSwlReplaceBlock
	cmp	r0, fp
	mov	r4, r0
	bne	.L3207
	ldrh	r2, [r5, #-6]
	sub	r3, r8, #3056
	ldrh	r1, [r9]
	cmp	r1, r2
	movcs	r2, #80
	strcsh	r2, [r3, #-6]	@ movhi
	bcs	.L3217
	mov	r0, #64
	bl	List_get_gc_head_node
	uxth	r3, r0
	cmp	r3, r4
	beq	.L3217
	ldr	r2, [r8, #-3156]
	sub	r1, r8, #3248
	cmp	r2, #0
	bne	.L3210
	ldrh	r2, [r1]
	cmp	r2, #3
	beq	.L3210
	ldr	r2, [r8, #-3152]
	cmp	r2, #0
	bne	.L3210
	ldr	r2, [r8, #-3336]
	cmp	r2, #0
	bne	.L3210
	ldr	r2, .L3353+20
	ldrb	r0, [r2, #3904]	@ zero_extendqisi2
	cmp	r0, #0
	beq	.L3211
.L3210:
	ldr	r2, [r7, #-2940]
	mov	r3, r3, asl #1
	ldrh	r1, [r1]
	ldrh	r0, [r2, r3]
	cmp	r1, #3
	ldr	r2, .L3353+44
	ldrh	r3, [r2, #-14]
	ldrh	r2, [r2, #-84]
	mul	r2, r2, r3
	moveq	r3, r3, lsr #1
	movne	r3, #0
	add	r3, r2, r3
	cmp	r0, r3
	bgt	.L3213
	mov	r0, #0
	bl	List_get_gc_head_node
	ldr	r3, [r7, #-2816]
	ldr	r2, [r7, #-408]
	add	r3, r3, r3, asl #1
	cmp	r2, r3, lsr #2
	movls	r3, #160
	uxth	r4, r0
	bls	.L3345
	b	.L3348
.L3211:
	ldr	r2, [r8, #-2940]
	mov	r3, r3, asl #1
	ldrh	r3, [r2, r3]
	cmp	r3, #7
	bhi	.L3216
	bl	List_get_gc_head_node
	uxth	r4, r0
.L3348:
	mov	r3, #128
.L3345:
	strh	r3, [r5, #-6]	@ movhi
	movw	r3, #65535
	cmp	r4, r3
	beq	.L3217
	b	.L3207
.L3213:
	mov	r3, #128
	b	.L3346
.L3216:
	mov	r3, #64
.L3346:
	strh	r3, [r5, #-6]	@ movhi
	b	.L3217
.L3207:
	ldr	r0, [r7, #-2948]
	mov	r1, r4, asl #1
	ldr	r3, [r7, #-2940]
	ldrh	r2, [r9]
	ldrh	r3, [r3, r1]
	ldrh	r1, [r0, r1]
	ldr	r0, .L3353+72
	str	r1, [sp]
	ldrh	r1, [r5, #-8]
	str	r1, [sp, #4]
	mov	r1, r4
	bl	printk
.L3217:
	bl	FtlGcReFreshBadBlk
	b	.L3205
.L3282:
	add	sp, sp, #52
	@ sp needed
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L3354:
	.align	2
.L3353:
	.word	.LANCHOR2
	.word	__stack_chk_guard
	.word	.LANCHOR1
	.word	.LANCHOR2-3056
	.word	.LANCHOR4-368
	.word	.LANCHOR0
	.word	.LANCHOR4+408
	.word	.LC154
	.word	.LANCHOR2-652
	.word	.LANCHOR2-3200
	.word	.LANCHOR4-372
	.word	.LANCHOR2-3184
	.word	.LANCHOR4
	.word	.LANCHOR2-648
	.word	.LANCHOR2-3060
	.word	.LANCHOR4+410
	.word	.LANCHOR2-552
	.word	.LANCHOR2-3268
	.word	.LC155
	.fnend
	.size	ftl_do_gc, .-ftl_do_gc
	.align	2
	.global	FtlCacheWriteBack
	.type	FtlCacheWriteBack, %function
FtlCacheWriteBack:
	.fnstart
	@ args = 0, pretend = 0, frame = 16
	@ frame_needed = 0, uses_anonymous_args = 0
	ldr	r3, .L3400
	stmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.save {r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.pad #20
	sub	sp, sp, #20
	ldr	r2, [r3]
	ldr	r4, .L3400+4
	ldr	r8, .L3400+8
	str	r2, [sp, #12]
	ldr	r2, [r4, #-2812]
	ldr	r5, [r8, #416]
	cmp	r2, #0
	str	r3, [sp, #4]
	bne	.L3357
	ldr	r1, [r4, #-3048]
	cmp	r1, #0
	beq	.L3357
	ldr	r3, .L3400+12
	mov	r7, #0
	ldr	r0, [r4, #-3016]
	mov	r10, #36
	ldr	r9, .L3400+4
	ldrb	r6, [r3, #3904]	@ zero_extendqisi2
	ldrb	r3, [r5, #9]	@ zero_extendqisi2
	cmp	r6, #0
	ldrneb	r6, [r5, #8]	@ zero_extendqisi2
	subne	r6, r6, #1
	clzne	r6, r6
	movne	r6, r6, lsr #5
	mov	r2, r6
	bl	FlashProgPages
.L3360:
	ldr	r3, [r4, #-3048]
	cmp	r7, r3
	bcs	.L3378
	mul	fp, r10, r7
	ldr	r3, [r9, #-3016]
	add	r2, r3, fp
	ldr	r3, [r3, fp]
	cmn	r3, #1
	beq	.L3382
	ldr	r3, [r2, #4]
	cmp	r6, #0
	ldr	r0, [r2, #16]
	add	r1, sp, #8
	mov	r2, #1
	orrne	r3, r3, #-2147483648
	str	r3, [sp, #8]
	bl	log2phys
	ldr	r3, [r4, #-3016]
	add	fp, r3, fp
	ldr	r3, [fp, #12]
	ldr	r0, [r3, #12]
	cmn	r0, #1
	beq	.L3364
	ubfx	r0, r0, #10, #16
	bl	P2V_block_in_plane
	ldr	r2, [r9, #-2940]
	mov	r3, r0, asl #1
	mov	fp, r0
	ldrh	r2, [r2, r3]
	cmp	r2, #0
	bne	.L3365
	ldr	r0, .L3400+16
	mov	r1, fp
	bl	printk
.L3365:
	mov	r0, fp
	bl	decrement_vpc_count
.L3364:
	add	r7, r7, #1
	b	.L3360
.L3398:
	ldr	r6, .L3400+20
	movw	r5, #16386
.L3377:
	ldrh	r3, [r6]
	cmp	r3, #0
	beq	.L3378
	mov	r0, #1
	mov	r1, r0
	bl	ftl_do_gc
	subs	r5, r5, #1
	bne	.L3377
.L3378:
	mov	r3, #0
	str	r3, [r4, #-3048]
	b	.L3357
.L3382:
	ldr	r9, .L3400+4
	mov	r10, #0
.L3361:
	ldr	r3, [r4, #-3048]
	cmp	r10, r3
	bcs	.L3398
	mov	r7, #36
	ldr	r3, [r9, #-3016]
	mul	r7, r7, r10
	mov	fp, #0
	mvn	r2, #0
	str	r2, [r3, r7]
.L3367:
	ldr	r3, [r4, #-3016]
	add	r2, r3, r7
	ldr	r3, [r3, r7]
	cmn	r3, #1
	bne	.L3399
	ldr	r0, [r2, #4]
	ubfx	r0, r0, #10, #16
	bl	P2V_block_in_plane
	ldrh	r3, [r5]
	cmp	r3, r0
	bne	.L3368
	ldr	r1, [r9, #-2940]
	mov	r3, r3, asl #1
	ldrh	r0, [r5, #4]
	ldrh	r2, [r1, r3]
	rsb	r2, r0, r2
	strh	r2, [r1, r3]	@ movhi
	ldr	r3, .L3400+24
	strb	fp, [r5, #6]
	strh	fp, [r5, #4]	@ movhi
	ldrh	r3, [r3]
	strh	r3, [r5, #2]	@ movhi
.L3368:
	ldrh	r3, [r5, #4]
	cmp	r3, #0
	bne	.L3369
	mov	r0, r5
	bl	allocate_new_data_superblock
.L3369:
	ldr	r3, [r8, #-224]
	add	r3, r3, #1
	str	r3, [r8, #-224]
	ldr	r3, [r4, #-3016]
	add	r3, r3, r7
	ldr	r0, [r3, #4]
	ubfx	r0, r0, #10, #16
	bl	FtlGcMarkBadPhyBlk
	mov	r0, r5
	bl	get_new_active_ppa
	ldr	r3, [r4, #-3016]
	mov	r1, #1
	mov	r2, r6
	add	r3, r3, r7
	str	r0, [sp, #8]
	str	r0, [r3, #4]
	mov	r0, r3
	ldrb	r3, [r5, #9]	@ zero_extendqisi2
	bl	FlashProgPages
	ldr	r3, [r4, #-3016]
	ldr	r3, [r3, r7]
	cmn	r3, #1
	moveq	r3, #1
	streq	r3, [r9, #-2812]
	ldr	r3, [r4, #-2812]
	cmp	r3, #0
	beq	.L3367
	b	.L3357
.L3399:
	ldr	r3, [r2, #4]
	cmp	r6, #0
	ldr	r0, [r2, #16]
	add	r1, sp, #8
	mov	r2, #1
	orrne	r3, r3, #-2147483648
	str	r3, [sp, #8]
	bl	log2phys
	ldr	r3, [r4, #-3016]
	add	r7, r3, r7
	ldr	r3, [r7, #12]
	ldr	r0, [r3, #12]
	cmn	r0, #1
	beq	.L3374
	ubfx	r0, r0, #10, #16
	bl	P2V_block_in_plane
	ldr	r2, [r9, #-2940]
	mov	r3, r0, asl #1
	mov	r7, r0
	ldrh	r2, [r2, r3]
	cmp	r2, #0
	bne	.L3375
	ldr	r0, .L3400+16
	mov	r1, r7
	bl	printk
.L3375:
	mov	r0, r7
	bl	decrement_vpc_count
.L3374:
	add	r10, r10, #1
	b	.L3361
.L3357:
	ldr	r3, [sp, #4]
	mov	r0, #0
	ldr	r2, [sp, #12]
	ldr	r3, [r3]
	cmp	r2, r3
	beq	.L3380
	bl	__stack_chk_fail
.L3380:
	add	sp, sp, #20
	@ sp needed
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L3401:
	.align	2
.L3400:
	.word	__stack_chk_guard
	.word	.LANCHOR2
	.word	.LANCHOR4
	.word	.LANCHOR0
	.word	.LC156
	.word	.LANCHOR2-3058
	.word	.LANCHOR2-3200
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
	ldr	r3, .L3408
	ldr	r3, [r3, #-2812]
	cmp	r3, #0
	bne	.L3403
	ldr	r3, .L3408+4
	ldr	r3, [r3, #3284]
	cmn	r3, #1
	beq	.L3403
	bl	FtlCacheWriteBack
	bl	l2p_flush
	mov	r0, #1
	bl	FtlEctTblFlush
	bl	FtlVpcTblFlush
.L3403:
	mov	r0, #0
	ldmfd	sp!, {r3, pc}
.L3409:
	.align	2
.L3408:
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
	ldr	r3, .L3413
	ldr	r3, [r3, #3284]
	cmp	r3, #1
	bne	.L3411
	bl	FtlSysFlush
.L3411:
	mov	r0, #0
	ldmfd	sp!, {r3, pc}
.L3414:
	.align	2
.L3413:
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
	@ args = 0, pretend = 0, frame = 16
	@ frame_needed = 0, uses_anonymous_args = 0
	stmfd	sp!, {r4, r5, r6, r7, r8, r9, lr}
	.save {r4, r5, r6, r7, r8, r9, lr}
	.pad #20
	sub	sp, sp, #20
	ldr	r5, .L3437
	add	r2, r0, r1
	mov	r7, r0
	mov	r6, r1
	ldr	r3, [r5]
	str	r3, [sp, #12]
	ldr	r3, .L3437+4
	ldr	r3, [r3, #3908]
	cmp	r2, r3
	mvnhi	r0, #0
	bhi	.L3419
	ldr	r3, .L3437+8
	ldr	r0, [r3, #3284]
	cmn	r0, #1
	beq	.L3419
	cmp	r1, #31
	bhi	.L3420
.L3421:
	mov	r0, #0
	b	.L3419
.L3420:
	ldr	r4, .L3437+12
	ldr	r3, [r4, #-2812]
	cmp	r3, #0
	bne	.L3421
	sub	r4, r4, #3184
	bl	FtlCacheWriteBack
	mov	r0, r7
	ldrh	r4, [r4, #-10]
	mov	r1, r4
	bl	__aeabi_uidiv
	smulbb	r3, r0, r4
	mov	r8, r0
	rsb	r7, r3, r7
	uxth	r7, r7
	cmp	r7, #0
	beq	.L3422
	rsb	r4, r7, r4
	add	r8, r0, #1
	cmp	r4, r6
	movcs	r4, r6
	uxth	r4, r4
	rsb	r6, r4, r6
.L3422:
	ldr	r4, .L3437+16
	mvn	r3, #0
	ldr	r9, .L3437+12
	str	r3, [sp, #8]
	mov	r7, r4
.L3423:
	ldrh	r3, [r4]
	cmp	r6, r3
	bcc	.L3436
	mov	r0, r8
	add	r1, sp, #4
	mov	r2, #0
	bl	log2phys
	ldr	r3, [sp, #4]
	cmn	r3, #1
	beq	.L3424
	ldr	r2, .L3437+20
	add	r1, sp, #8
	mov	r0, r8
	ldr	r3, [r2, #420]
	add	r3, r3, #1
	str	r3, [r2, #420]
	ldr	r3, [r9, #-3132]
	mov	r2, #1
	add	r3, r3, #1
	str	r3, [r9, #-3132]
	bl	log2phys
	ldr	r0, [sp, #4]
	ubfx	r0, r0, #10, #16
	bl	P2V_block_in_plane
	bl	decrement_vpc_count
.L3424:
	ldrh	r3, [r7]
	add	r8, r8, #1
	rsb	r6, r3, r6
	b	.L3423
.L3436:
	ldr	r3, .L3437+20
	ldr	r2, [r3, #420]
	cmp	r2, #32
	bls	.L3421
	mov	r4, #0
	str	r4, [r3, #420]
	bl	l2p_flush
	bl	FtlVpcTblFlush
	b	.L3421
.L3419:
	ldr	r2, [sp, #12]
	ldr	r3, [r5]
	cmp	r2, r3
	beq	.L3426
	bl	__stack_chk_fail
.L3426:
	add	sp, sp, #20
	@ sp needed
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, pc}
.L3438:
	.align	2
.L3437:
	.word	__stack_chk_guard
	.word	.LANCHOR0
	.word	.LANCHOR1
	.word	.LANCHOR2
	.word	.LANCHOR2-3194
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
	ldr	r6, .L3479
	ldr	r8, .L3479+4
	sub	r4, r6, #3200
	ldr	r7, [r6, #-2812]
	ldr	r3, [r8]
	cmp	r7, #0
	ldrh	r1, [r4]
	str	r3, [sp, #12]
	bne	.L3477
	mov	r10, r4
	sub	r4, r6, #552
	mov	r5, r6
	movw	ip, #65535
	ldrh	r6, [r4]
	cmp	r6, ip
	bne	.L3442
.L3451:
	ldrh	r2, [r4]
	movw	r3, #65535
	ldr	r6, .L3479+8
	mov	r7, #0
	ldr	r9, .L3479
	cmp	r2, r3
	str	r7, [r6, #224]
	sub	fp, r9, #552
	beq	.L3477
	bl	FtlCacheWriteBack
	ldrh	r2, [r10]
	ldrb	r0, [r9, #-545]	@ zero_extendqisi2
	mov	r10, #12
	ldrh	r3, [fp]
	ldr	r1, [r9, #-2940]
	smulbb	r2, r0, r2
	mov	r3, r3, asl #1
	strh	r2, [r1, r3]	@ movhi
	ldr	r2, [r9, #-3140]
	ldrh	r3, [r6, #234]
	add	r3, r3, r2
	str	r3, [r9, #-3140]
	b	.L3452
.L3442:
	cmp	r0, #0
	beq	.L3445
	ldr	r2, .L3479+12
	movw	r3, #3288
	ldrh	r0, [r2, r3]
	cmp	r0, ip
	beq	.L3446
.L3447:
	mov	r1, #2
	b	.L3445
.L3446:
	strh	r7, [r2, r3]	@ movhi
	sub	r3, r5, #652
	ldrh	r3, [r3]
	cmp	r3, #17
	bhi	.L3447
.L3445:
	ldr	r7, .L3479
	sub	r0, r7, #552
	bl	FtlGcScanTempBlk
	cmn	r0, #1
	str	r0, [sp, #8]
	beq	.L3448
	ldr	r2, [r7, #-2948]
	mov	r6, r6, asl #1
	ldrh	r3, [r2, r6]
	cmp	r3, #4
	bls	.L3449
	sub	r3, r3, #5
	mov	r0, #1
	strh	r3, [r2, r6]	@ movhi
	bl	FtlEctTblFlush
.L3449:
	ldr	r4, .L3479+8
	ldr	r3, [r4, #224]
	cmp	r3, #0
	bne	.L3450
	ldr	r0, [sp, #8]
	ldr	r3, [r4, #-224]
	ubfx	r0, r0, #10, #16
	add	r3, r3, #1
	str	r3, [r4, #-224]
	bl	FtlBbmMapBadBlock
	bl	FtlBbmTblFlush
.L3450:
	mov	r3, #0
	str	r3, [r4, #224]
	b	.L3463
.L3448:
	ldr	r2, .L3479+12
	movw	r3, #3288
	ldrh	r2, [r2, r3]
	movw	r3, #65535
	cmp	r2, r3
	bne	.L3463
	b	.L3451
.L3455:
	ldr	r3, [fp, #4]
	cmp	r0, r3
	bne	.L3475
.L3454:
	add	r7, r7, #1
.L3452:
	ldrh	r3, [r6, #234]
	uxth	r9, r7
	cmp	r3, r9
	bls	.L3478
	mul	r9, r10, r9
	ldr	r3, [r5, #-3040]
	ldr	r2, [r5, #-2816]
	add	fp, r3, r9
	ldr	r0, [fp, #8]
	cmp	r0, r2
	bcs	.L3475
	add	r1, sp, #8
	mov	r2, #0
	str	r3, [sp, #4]
	bl	log2phys
	ldr	r3, [sp, #4]
	ldr	r0, [sp, #8]
	ldr	r3, [r3, r9]
	cmp	r0, r3
	bne	.L3455
	ubfx	r0, r0, #10, #16
	bl	P2V_block_in_plane
	add	r1, fp, #4
	mov	r2, #1
	mov	r9, r0
	ldr	r0, [fp, #8]
	bl	log2phys
	mov	r0, r9
	b	.L3476
.L3475:
	ldrh	r0, [r4]
.L3476:
	bl	decrement_vpc_count
	b	.L3454
.L3478:
	ldr	r9, .L3479+16
	movw	r0, #65535
	bl	decrement_vpc_count
	ldrb	r3, [r9, #3904]	@ zero_extendqisi2
	cmp	r3, #0
	beq	.L3457
	ldr	r0, .L3479+20
	ldrh	r1, [r4]
	bl	printk
.L3457:
	ldrh	r0, [r4]
	ldr	r2, [r5, #-2940]
	mov	r3, r0, asl #1
	ldrh	r3, [r2, r3]
	cmp	r3, #0
	beq	.L3458
	bl	INSERT_DATA_LIST
	b	.L3459
.L3458:
	bl	INSERT_FREE_LIST
.L3459:
	mvn	r10, #0
	strh	r10, [r4]	@ movhi
	mov	r4, #0
	strh	r4, [r6, #234]	@ movhi
	strh	r4, [r6, #232]	@ movhi
	bl	l2p_flush
	bl	FtlVpcTblFlush
	ldr	r7, .L3479
	ldr	r2, [r5, #-3336]
	ldr	r3, .L3479+24
	cmp	r2, r4
	sub	r2, r7, #652
	strh	r10, [r3], #-4	@ movhi
	ldrh	r2, [r2]
	ldrh	r3, [r3]
	beq	.L3460
	ldr	r1, [r7, #-3100]
	cmp	r1, #39
	bhi	.L3460
	cmp	r2, r3
	subcc	r7, r7, #3056
	movcc	r3, r3, asl #1
	strcch	r3, [r7, #-8]	@ movhi
	b	.L3477
.L3460:
	add	r1, r3, r3, asl #1
	cmp	r2, r1, asr #2
	ble	.L3477
	ldrb	r0, [r9, #3904]	@ zero_extendqisi2
	ldr	r2, .L3479+28
	cmp	r0, #0
	moveq	r3, #20
	streqh	r3, [r2, #-8]	@ movhi
	beq	.L3441
	sub	r3, r3, #2
	strh	r3, [r2, #-8]	@ movhi
.L3477:
	mov	r0, #0
	b	.L3441
.L3463:
	mov	r0, #1
.L3441:
	ldr	r2, [sp, #12]
	ldr	r3, [r8]
	cmp	r2, r3
	beq	.L3462
	bl	__stack_chk_fail
.L3462:
	add	sp, sp, #20
	@ sp needed
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L3480:
	.align	2
.L3479:
	.word	.LANCHOR2
	.word	__stack_chk_guard
	.word	.LANCHOR4
	.word	.LANCHOR1
	.word	.LANCHOR0
	.word	.LC157
	.word	.LANCHOR4-368
	.word	.LANCHOR2-3056
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
	ldr	r4, .L3484
	ldr	r5, .L3484+4
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
	ldr	r3, .L3484+8
	mov	r2, #0
	str	r2, [r3, #224]
	ldmfd	sp!, {r3, r4, r5, pc}
.L3485:
	.align	2
.L3484:
	.word	.LANCHOR2-552
	.word	.LANCHOR2-3200
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
	ldr	r4, .L3488
	ldr	r3, .L3488+4
	sub	r6, r4, #648
	sub	r4, r4, #600
	mov	r0, r6
	str	r5, [r3, #276]
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
.L3489:
	.align	2
.L3488:
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
	ldr	r3, .L3508
	mov	r2, #0
	stmfd	sp!, {r4, r5, r6, r7, r8, lr}
	.save {r4, r5, r6, r7, r8, lr}
	add	r8, r3, #272
	ldr	r4, .L3508+4
	mvn	r1, #0
	strh	r2, [r8]	@ movhi
	movw	r2, #270
	strh	r1, [r3, r2]	@ movhi
	ldr	r3, [r4, #-3264]
	uxth	r0, r3
	bl	FtlFreeSysBlkQueueInit
	bl	FtlScanSysBlk
	sub	r3, r4, #404
	ldrh	r2, [r3]
	movw	r3, #65535
	cmp	r2, r3
	bne	.L3491
.L3493:
	mvn	r7, #0
	b	.L3492
.L3491:
	bl	FtlLoadSysInfo
	subs	r7, r0, #0
	bne	.L3493
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
	sub	r3, r4, #3152
	mov	r1, #12
	ldrh	r2, [r3, #-10]
	ldr	r3, [r4, #-2896]
.L3494:
	cmp	r6, r2
	bge	.L3499
	mla	r0, r1, r6, r3
	ldr	r0, [r0, #4]
	cmp	r0, #0
	bge	.L3495
.L3499:
	ldr	r5, .L3508+8
	cmp	r6, r2
	add	r6, r5, #68
	ldrh	r3, [r5, #28]
	add	r3, r3, #1
	strh	r3, [r5, #28]	@ movhi
	bge	.L3506
	b	.L3496
.L3495:
	add	r6, r6, #1
	b	.L3494
.L3506:
	ldrh	r3, [r8]
	cmp	r3, #0
	beq	.L3500
.L3496:
	ldrh	r3, [r6]
	ldr	r1, [r4, #-2940]
	ldr	ip, .L3508+12
	mov	r3, r3, asl #1
	ldrh	r0, [r6, #4]
	ldrh	r2, [r1, r3]
	rsb	r2, r0, r2
	strh	r2, [r1, r3]	@ movhi
	ldrh	r3, [ip]
	mov	r2, #0
	ldr	lr, [r4, #-2940]
	strb	r2, [r4, #-642]
	strh	r3, [r6, #2]	@ movhi
	ldr	r3, .L3508+16
	strh	r2, [r6, #4]	@ movhi
	ldrh	r1, [r3]
	ldrh	r8, [r3, #4]
	mov	r1, r1, asl #1
	ldrh	r0, [lr, r1]
	rsb	r0, r8, r0
	strh	r0, [lr, r1]	@ movhi
	ldrh	r1, [ip]
	strh	r2, [r3, #4]	@ movhi
	strb	r2, [r4, #-594]
	strh	r1, [r3, #2]	@ movhi
	ldrh	r3, [r5, #30]
	add	r3, r3, #1
	strh	r3, [r5, #30]	@ movhi
	bl	l2p_flush
	bl	FtlVpcTblFlush
	bl	FtlVpcTblFlush
.L3500:
	ldrh	r0, [r6]
	movw	r3, #65535
	ldr	r8, .L3508+20
	cmp	r0, r3
	beq	.L3501
	ldrh	r3, [r8, #4]
	cmp	r3, #0
	bne	.L3501
	ldrh	r3, [r8, #52]
	add	r4, r8, #48
	cmp	r3, #0
	bne	.L3501
	bl	FtlGcRefreshOpenBlock
	ldrh	r0, [r8, #48]
	bl	FtlGcRefreshOpenBlock
	bl	FtlVpcTblFlush
	mov	r0, r8
	bl	allocate_new_data_superblock
	mov	r0, r4
	bl	allocate_new_data_superblock
.L3501:
	ldr	r3, .L3508+24
	ldrb	r3, [r3]	@ zero_extendqisi2
	cmp	r3, #0
	bne	.L3502
	ldrh	r3, [r5, #28]
	tst	r3, #31
	bne	.L3492
.L3502:
	bl	FtlVpcCheckAndModify
.L3492:
	mov	r0, r7
	ldmfd	sp!, {r4, r5, r6, r7, r8, pc}
.L3509:
	.align	2
.L3508:
	.word	.LANCHOR4
	.word	.LANCHOR2
	.word	.LANCHOR2-716
	.word	.LANCHOR2-3200
	.word	.LANCHOR2-600
	.word	.LANCHOR2-648
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
	ldr	r4, .L3545
	ldr	r5, [r4, #-2812]
	cmp	r5, #0
	bne	.L3512
	sub	r6, r4, #3152
	mov	r1, r5
	ldr	r0, [r4, #-2904]
	ldrh	r2, [r6, #-12]
	mov	r2, r2, asl #2
	bl	ftl_memset
	ldrh	r2, [r6, #-12]
	mov	r1, r5
	ldr	r0, [r4, #-2908]
	mov	r2, r2, asl #2
	bl	ftl_memset
	ldr	r3, [r4, #-3264]
	str	r5, [r4, #-3148]
	str	r5, [r4, #-3144]
	uxth	r0, r3
	bl	FtlFreeSysBlkQueueInit
	bl	FtlLoadBbt
	cmp	r0, #0
	beq	.L3513
	bl	FtlMakeBbt
.L3513:
	ldr	r0, .L3545
	mov	r2, #0
	ldr	ip, .L3545+4
	ldr	lr, .L3545+8
.L3514:
	ldrh	r1, [ip]
	uxth	r3, r2
	add	r2, r2, #1
	cmp	r3, r1, asl #7
	bge	.L3542
	ldr	r5, [r0, #-2988]
	mvn	r1, r3
	orr	r1, r3, r1, asl #16
	str	r1, [r5, r3, asl #2]
	ldr	r1, [r0, #-2984]
	str	lr, [r1, r3, asl #2]
	b	.L3514
.L3542:
	ldr	r8, .L3545+12
	mov	r7, #0
	sub	r6, r8, #10
	ldrh	r5, [r8, #-12]
.L3516:
	ldrh	r3, [r6]
	ldr	r10, .L3545
	cmp	r3, r5
	sub	fp, r10, #3248
	sub	r9, fp, #10
	bls	.L3543
	mov	r0, r5
	mov	r1, #1
	bl	FtlLowFormatEraseBlock
	add	r5, r5, #1
	uxth	r5, r5
	add	r0, r7, r0
	uxth	r7, r0
	b	.L3516
.L3543:
	sub	r6, r10, #3264
	sub	r3, r7, #3
	ldrh	r1, [r6, #-4]
	cmp	r3, r1, asl #1
	bge	.L3518
.L3522:
	mov	r5, #0
	ldr	r9, .L3545+16
	mov	r7, r5
	b	.L3519
.L3518:
	mov	r0, r7
	bl	__aeabi_uidiv
	ldr	r3, [r10, #-3168]
	add	r0, r0, r3
	uxth	r0, r0
	bl	FtlSysBlkNumInit
	ldr	r0, [r10, #-3264]
	uxth	r0, r0
	bl	FtlFreeSysBlkQueueInit
	ldrh	r7, [fp, #-12]
.L3520:
	ldrh	r3, [r9]
	cmp	r3, r7
	bls	.L3522
	mov	r0, r7
	add	r7, r7, #1
	mov	r1, #1
	bl	FtlLowFormatEraseBlock
	uxth	r7, r7
	b	.L3520
.L3519:
	ldrh	r2, [r9]
	uxth	r0, r5
	ldr	fp, .L3545
	add	r5, r5, #1
	cmp	r2, r0
	sub	r3, fp, #3248
	bls	.L3544
	mov	r1, #0
	bl	FtlLowFormatEraseBlock
	add	r0, r7, r0
	uxth	r7, r0
	b	.L3519
.L3544:
	ldr	r2, [fp, #-3256]
	ldrh	r3, [r3, #-10]
	ldrh	r6, [r6, #-4]
	mov	r0, r2
	str	r2, [sp, #4]
	str	r3, [fp, #-3076]
	mov	r1, r6
	ldr	r5, .L3545+20
	bl	__aeabi_uidiv
	ubfx	r9, r0, #5, #16
	mov	r10, r0
	add	r3, r9, #36
	strh	r3, [r5]	@ movhi
	mov	r3, #24
	str	r0, [fp, #-2816]
	mul	r3, r3, r6
	ldr	r2, [sp, #4]
	cmp	r7, r3
	ble	.L3524
	rsb	r0, r7, r2
	mov	r1, r6
	bl	__aeabi_uidiv
	str	r0, [fp, #-2816]
	mov	r0, r0, lsr #5
	add	r0, r0, #24
	strh	r0, [r5]	@ movhi
.L3524:
	ldr	r3, [r4, #-3336]
	cmp	r3, #1
	bne	.L3525
	mov	r0, r7
	mov	r1, r6
	bl	__aeabi_uidiv
	ldrh	fp, [r5]
	uxtah	r0, fp, r0
	add	fp, fp, r0, asr #2
	strh	fp, [r5]	@ movhi
.L3525:
	ldr	fp, .L3545+24
	ldrb	r3, [fp, #3904]	@ zero_extendqisi2
	cmp	r3, #0
	beq	.L3526
	ldrh	r3, [r5]
	mov	r0, r7
	mov	r1, r6
	str	r3, [sp, #4]
	bl	__aeabi_uidiv
	ldr	r3, [sp, #4]
	uxtah	r0, r3, r0
	add	r3, r3, r0, asr #2
	strh	r3, [r5]	@ movhi
.L3526:
	ldr	r0, .L3545
	sub	r2, r0, #3200
	ldrh	r3, [r2, #-6]
	cmp	r3, #0
	beq	.L3528
	ldrh	r1, [r5]
	add	r1, r1, r3, lsr #1
	strh	r1, [r5]	@ movhi
	mul	r1, r6, r3
	cmp	r1, r7
	strgt	r10, [r0, #-2816]
	addgt	r3, r3, #32
	ldrgt	r1, .L3545+20
	addgt	r3, r9, r3
	strgth	r3, [r1]	@ movhi
.L3528:
	ldrh	r1, [r5]
	mvn	r9, #0
	ldr	r3, [r4, #-2816]
	mov	r5, #0
	ldr	r7, .L3545+28
	rsb	r3, r1, r3
	mul	r6, r6, r3
	ldrh	r3, [r2]
	str	r6, [r7, #-376]
	mul	r3, r3, r6
	ldr	r6, .L3545+32
	ldrh	r2, [r6, #-10]
	str	r3, [r4, #-2816]
	mul	r3, r2, r3
	str	r3, [fp, #3908]
	bl	FtlBbmTblFlush
	ldrh	r2, [r8, #-10]
	mov	r1, #0
	ldr	r0, [r4, #-2940]
	ldr	fp, .L3545+36
	mov	r2, r2, asl #1
	bl	ftl_memset
	ldrh	r2, [r8, #-12]
	sub	r3, r7, #368
	ldr	r0, [r4, #-2928]
	mov	r1, #255
	str	r5, [r4, #-408]
	mov	r2, r2, lsr #3
	strh	r9, [r3]	@ movhi
	strh	r5, [r3, #2]	@ movhi
	mov	r3, #1
	strb	r5, [r7, #-362]
	mov	r10, fp
	strb	r5, [r7, #-360]
	strh	r5, [fp, #2]	@ movhi
	strb	r5, [r4, #-642]
	strh	r5, [fp]	@ movhi
	strb	r3, [r4, #-640]
	bl	ftl_memset
	add	r2, r6, #3184
.L3530:
	ldr	r5, .L3545
	str	r2, [sp, #4]
	sub	r6, r5, #648
	mov	r0, r6
	bl	make_superblock
	ldrb	r3, [r4, #-641]	@ zero_extendqisi2
	cmp	r3, #0
	ldrh	r3, [fp]
	bne	.L3531
	ldr	r2, [sp, #4]
	mov	r3, r3, asl #1
	ldr	r1, [r2, #-2940]
	strh	r9, [r1, r3]	@ movhi
	ldrh	r3, [r10]
	add	r8, r3, #1
	strh	r8, [r10]	@ movhi
	b	.L3530
.L3531:
	ldr	r2, [r5, #-3148]
	mov	r3, r3, asl #1
	ldrh	r1, [r6, #4]
	mvn	fp, #0
	str	r2, [r5, #-636]
	add	r2, r2, #1
	str	r2, [r5, #-3148]
	ldr	r2, [r5, #-2940]
	strh	r1, [r2, r3]	@ movhi
	sub	r2, r5, #600
	mov	r3, #0
	strb	r3, [r5, #-594]
	strh	r3, [r2, #2]	@ movhi
	mov	r8, r2
	ldrh	r3, [r6]
	mov	r10, r2
	add	r3, r3, #1
	strh	r3, [r2]	@ movhi
	mov	r3, #1
	strb	r3, [r5, #-592]
.L3532:
	ldr	r6, .L3545
	sub	r9, r6, #600
	mov	r0, r9
	bl	make_superblock
	ldrb	r3, [r4, #-593]	@ zero_extendqisi2
	cmp	r3, #0
	ldrh	r3, [r8]
	bne	.L3533
	ldr	r2, [r5, #-2940]
	mov	r3, r3, asl #1
	strh	fp, [r2, r3]	@ movhi
	ldrh	r3, [r10]
	add	r3, r3, #1
	strh	r3, [r10]	@ movhi
	b	.L3532
.L3533:
	ldr	r2, [r6, #-3148]
	mov	r3, r3, asl #1
	ldrh	r1, [r9, #4]
	mvn	r4, #0
	str	r2, [r6, #-588]
	add	r2, r2, #1
	str	r2, [r6, #-3148]
	ldr	r2, [r6, #-2940]
	strh	r1, [r2, r3]	@ movhi
	sub	r3, r6, #552
	strh	r4, [r3]	@ movhi
	bl	FtlFreeSysBlkQueueOut
	sub	r3, r6, #404
	mov	r2, #0
	strh	r2, [r3, #2]	@ movhi
	ldr	r2, [r7, #-376]
	strh	r4, [r3, #4]	@ movhi
	strh	r2, [r3, #6]	@ movhi
	strh	r0, [r3]	@ movhi
	ldr	r3, [r6, #-3148]
	str	r3, [r6, #-396]
	add	r3, r3, #1
	str	r3, [r6, #-3148]
	bl	FtlVpcTblFlush
	bl	FtlSysBlkInit
	cmp	r0, #0
	ldreq	r3, .L3545+40
	moveq	r2, #1
	streq	r2, [r3, #3284]
.L3512:
	mov	r0, #0
	add	sp, sp, #12
	@ sp needed
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L3546:
	.align	2
.L3545:
	.word	.LANCHOR2
	.word	.LANCHOR2-3194
	.word	168778952
	.word	.LANCHOR2-3248
	.word	.LANCHOR2-3260
	.word	.LANCHOR4-372
	.word	.LANCHOR0
	.word	.LANCHOR4
	.word	.LANCHOR2-3184
	.word	.LANCHOR2-648
	.word	.LANCHOR1
	.fnend
	.size	FtlLowFormat, .-FtlLowFormat
	.align	2
	.global	FtlReInitForSDUpdata
	.type	FtlReInitForSDUpdata, %function
FtlReInitForSDUpdata:
	.fnstart
	@ args = 0, pretend = 0, frame = 24
	@ frame_needed = 0, uses_anonymous_args = 0
	stmfd	sp!, {r4, r5, r6, lr}
	.save {r4, r5, r6, lr}
	.pad #24
	sub	sp, sp, #24
	ldr	r5, .L3577
	ldr	r0, .L3577+4
	ldr	r3, [r5]
	str	r3, [sp, #20]
	ldrb	r3, [r0, #3904]	@ zero_extendqisi2
	cmp	r3, #0
	beq	.L3548
.L3550:
	mov	r0, #0
	b	.L3549
.L3548:
	ldr	r3, .L3577+8
	mov	r4, r0
	ldr	r0, [r3]
	bl	FlashInit
	cmp	r0, #0
	bne	.L3550
	bl	FlashLoadFactorBbt
	cmp	r0, #0
	beq	.L3551
	bl	FlashMakeFactorBbt
.L3551:
	ldr	r6, .L3577+12
	ldr	r0, [r6, #-3308]
	bl	FlashReadIdbDataRaw
	cmp	r0, #0
	beq	.L3552
	mov	r2, #16
	add	r0, sp, #4
	mov	r1, #0
	bl	FlashReadFacBbtData
	mov	r3, #0
	mov	r0, r3
	mov	ip, #1
	ldr	r2, [sp, #4]
.L3553:
	ands	lr, r2, ip, asl r0
	add	r1, r3, #1
	add	r0, r0, #1
	movne	r3, r1
	cmp	r0, #16
	bne	.L3553
	cmp	r3, #6
	bls	.L3573
	mov	r0, #0
	mov	ip, #1
.L3556:
	ands	lr, r2, ip, asl r0
	add	r1, r3, #1
	add	r0, r0, #1
	movne	r3, r1
	cmp	r0, #24
	bne	.L3556
	cmp	r3, #17
	movhi	r3, #36
	strhib	r3, [r4, #1]
	bhi	.L3555
.L3573:
	strb	r0, [r4, #1]
.L3555:
	ldrb	r2, [r4, #1]	@ zero_extendqisi2
	ldr	r3, .L3577+16
	strh	r2, [r3, #26]	@ movhi
.L3552:
	ldr	r1, .L3577+20
	mov	r4, #1
	ldr	r0, .L3577+24
	bl	printk
	ldr	r0, .L3577+16
	bl	FtlConstantsInit
	bl	FtlVariablesInit
	ldr	r0, [r6, #-3264]
	uxth	r0, r0
	bl	FtlFreeSysBlkQueueInit
.L3558:
	bl	FtlLoadBbt
	cmp	r0, #0
	beq	.L3559
.L3575:
	bl	FtlLowFormat
	cmp	r4, #3
	addls	r4, r4, #1
	bls	.L3558
.L3576:
	mvn	r0, #0
	b	.L3549
.L3559:
	bl	FtlSysBlkInit
	cmp	r0, #0
	bne	.L3575
	ldr	r3, .L3577+28
	mov	r2, #1
	str	r2, [r3, #3284]
.L3549:
	ldr	r2, [sp, #20]
	ldr	r3, [r5]
	cmp	r2, r3
	beq	.L3563
	bl	__stack_chk_fail
.L3563:
	add	sp, sp, #24
	@ sp needed
	ldmfd	sp!, {r4, r5, r6, pc}
.L3578:
	.align	2
.L3577:
	.word	__stack_chk_guard
	.word	.LANCHOR0
	.word	RK29_NANDC_REG_BASE
	.word	.LANCHOR2
	.word	.LANCHOR0+3876
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
	ldr	r4, .L3596
	ldr	r3, [r4, #-2812]
	cmp	r3, #0
	beq	.L3580
.L3583:
	mov	r0, #0
	ldmfd	sp!, {r4, r5, r6, r7, r8, pc}
.L3580:
	ldr	r3, .L3596+4
	ldr	r6, .L3596+8
	ldrb	r3, [r3, #3904]	@ zero_extendqisi2
	cmp	r3, #0
	beq	.L3582
	ldr	r3, [r6, #208]
	tst	r3, #1
	beq	.L3582
	sub	r3, r4, #552
	ldrh	r3, [r3, #4]
	cmp	r3, #0
	bne	.L3583
.L3582:
	mov	r2, #0
	ldr	r0, [r4, #-3032]
	ldr	r1, [r6, #208]
	mov	r3, r2
	bl	FlashProgPages
	mov	r7, #0
	mov	r8, #36
.L3584:
	ldr	r1, [r6, #208]
	uxth	r3, r7
	ldr	r5, .L3596+8
	cmp	r3, r1
	bcs	.L3595
	mul	r3, r8, r3
	ldr	r0, [r4, #-3032]
	ldr	r2, .L3596
	add	r7, r7, #1
	add	r1, r0, r3
	ldr	lr, [r0, r3]
	ldr	ip, [r1, #12]
	cmn	lr, #1
	bne	.L3585
	sub	r0, r2, #552
	ldr	ip, [r2, #-2940]
	mov	r4, #0
	ldrh	r1, [r0]
	mov	r1, r1, asl #1
	strh	r4, [ip, r1]	@ movhi
	ldr	r2, [r2, #-3032]
	ldr	r1, [r5, #-224]
	add	r3, r2, r3
	strh	lr, [r0]	@ movhi
	add	r1, r1, #1
	str	r1, [r5, #-224]
	ldr	r0, [r3, #4]
	ubfx	r0, r0, #10, #16
	bl	FtlBbmMapBadBlock
	bl	FtlBbmTblFlush
	bl	FtlGcPageVarInit
	b	.L3594
.L3585:
	ldr	r0, [ip, #12]
	ldr	r1, [r1, #4]
	ldr	r2, [ip, #8]
	bl	FtlGcUpdatePage
	b	.L3584
.L3595:
	ldr	r0, [r4, #-3032]
	bl	FtlGcBufFree
	mov	r3, #0
	str	r3, [r5, #208]
	ldr	r3, .L3596+12
	ldrh	r3, [r3, #4]
	cmp	r3, #0
	bne	.L3583
	mov	r0, #1
	bl	FtlGcFreeTempBlock
.L3594:
	mov	r0, #1
	ldmfd	sp!, {r4, r5, r6, r7, r8, pc}
.L3597:
	.align	2
.L3596:
	.word	.LANCHOR2
	.word	.LANCHOR0
	.word	.LANCHOR4
	.word	.LANCHOR2-552
	.fnend
	.size	Ftl_gc_temp_data_write_back, .-Ftl_gc_temp_data_write_back
	.align	2
	.global	Ftl_get_new_temp_ppa
	.type	Ftl_get_new_temp_ppa, %function
Ftl_get_new_temp_ppa:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	ldr	r3, .L3602
	movw	r2, #65535
	stmfd	sp!, {r4, lr}
	.save {r4, lr}
	ldrh	r1, [r3]
	cmp	r1, r2
	beq	.L3599
	ldrh	r3, [r3, #4]
	cmp	r3, #0
	bne	.L3600
.L3599:
	bl	FtlCacheWriteBack
	mov	r0, #0
	bl	FtlGcFreeTempBlock
	ldr	r0, .L3602+4
	mov	r4, #0
	sub	r0, r0, #552
	strb	r4, [r0, #8]
	bl	allocate_data_superblock
	ldr	r3, .L3602+8
	strh	r4, [r3, #232]	@ movhi
	strh	r4, [r3, #234]	@ movhi
	bl	l2p_flush
	mov	r0, r4
	bl	FtlEctTblFlush
	bl	FtlVpcTblFlush
.L3600:
	ldr	r0, .L3602
	ldmfd	sp!, {r4, lr}
	b	get_new_active_ppa
.L3603:
	.align	2
.L3602:
	.word	.LANCHOR2-552
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
	mov	r8, r3
	ldr	r3, .L3652
	.pad #84
	sub	sp, sp, #84
	str	r2, [sp, #36]
	ldr	r3, [r3]
	str	r3, [sp, #76]
	ldr	r3, .L3652+4
	ldr	r3, [r3, #3284]
	cmn	r3, #1
	moveq	r0, r3
	beq	.L3605
	cmp	r0, #16
	mov	r7, r1
	bne	.L3606
	add	r0, r1, #256
	mov	r1, r2
	mov	r2, r8
	bl	FtlVendorPartRead
	b	.L3605
.L3606:
	ldr	r3, [sp, #36]
	add	r3, r1, r3
	str	r3, [sp, #40]
	ldr	r3, .L3652+8
	ldr	r2, [sp, #40]
	ldr	r3, [r3, #3908]
	cmp	r2, r3
	mvnhi	r0, #0
	bhi	.L3605
	ldr	r4, .L3652+12
	mov	r0, r1
	sub	r3, r4, #3184
	ldrh	r5, [r3, #-10]
	mov	r1, r5
	bl	__aeabi_uidiv
	mov	r1, r5
	ldr	r3, [sp, #40]
	str	r0, [sp, #28]
	sub	r0, r3, #1
	bl	__aeabi_uidiv
	ldr	r2, .L3652+16
	ldr	r3, [sp, #28]
	ldr	r1, [sp, #36]
	rsb	r3, r3, #1
	str	r0, [sp, #32]
	add	r3, r3, r0
	str	r3, [sp, #24]
	ldr	r3, [r2, #196]
	ldr	r0, [sp, #28]
	add	r3, r1, r3
	str	r3, [r2, #196]
	ldr	r3, [r4, #-3120]
	ldr	r2, [sp, #24]
	ldr	r1, [sp, #32]
	add	r3, r2, r3
	str	r3, [r4, #-3120]
	bl	FtlCacheMetchLpa
	cmp	r0, #0
	beq	.L3607
	bl	FtlCacheWriteBack
.L3607:
	ldr	r5, [sp, #28]
	mov	r9, #0
	ldr	r4, .L3652+12
	mov	r6, r9
	str	r9, [sp, #48]
	str	r9, [sp, #52]
.L3608:
	ldr	r3, [sp, #24]
	cmp	r3, #0
	beq	.L3651
	mov	r0, r5
	add	r1, sp, #72
	mov	r2, #0
	bl	log2phys
	ldr	r3, [sp, #72]
	cmn	r3, #1
	bne	.L3647
	mov	r10, #0
.L3609:
	ldr	r3, .L3652+20
	ldrh	r0, [r3]
	cmp	r10, r0
	bcs	.L3613
	mla	r0, r0, r5, r10
	ldr	r2, [sp, #40]
	cmp	r0, r7
	movcs	r3, #1
	movcc	r3, #0
	cmp	r0, r2
	movcs	r3, #0
	cmp	r3, #0
	beq	.L3611
	rsb	r0, r7, r0
	mov	r1, #0
	mov	r2, #512
	add	r0, r8, r0, asl #9
	bl	ftl_memset
.L3611:
	add	r10, r10, #1
	b	.L3609
.L3647:
	ldr	r2, [r4, #-3036]
	mov	r10, #36
	mla	r10, r10, r6, r2
	str	r3, [r10, #4]
	ldr	r3, [sp, #28]
	cmp	r5, r3
	bne	.L3614
	ldr	r3, [r4, #-2988]
	mov	r0, r7
	str	r3, [r10, #8]
	ldr	r3, .L3652+20
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
	b	.L3615
.L3614:
	ldr	r3, [sp, #32]
	cmp	r5, r3
	bne	.L3616
	ldr	r3, [r4, #-2984]
	ldr	r1, [sp, #40]
	str	r3, [r10, #8]
	ldr	r3, .L3652+20
	ldrh	r2, [r3]
	mul	r3, r2, r5
	rsb	r9, r3, r1
	cmp	r9, r2
	bne	.L3615
	b	.L3649
.L3616:
	ldr	r3, .L3652+20
	ldrh	r3, [r3]
	mul	r3, r3, r5
.L3649:
	rsb	r3, r7, r3
	add	r3, r8, r3, asl #9
	str	r3, [r10, #8]
.L3615:
	ldr	r3, .L3652+24
	ldr	r2, [r4, #-2972]
	str	r5, [r10, #16]
	ldrh	r3, [r3]
	mul	r3, r3, r6
	add	r6, r6, #1
	bic	r3, r3, #3
	add	r3, r2, r3
	str	r3, [r10, #12]
.L3613:
	ldr	r3, [sp, #24]
	add	r5, r5, #1
	subs	r3, r3, #1
	str	r3, [sp, #24]
	beq	.L3617
	ldr	r3, .L3652+28
	ldrh	r3, [r3]
	cmp	r6, r3, asl #3
	bne	.L3608
.L3617:
	cmp	r6, #0
	beq	.L3608
	ldr	r0, [r4, #-3036]
	mov	r1, r6
	mov	r2, #0
	ldr	fp, .L3652+16
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
.L3624:
	ldr	r3, [sp, #44]
	mov	ip, #36
	ldr	r1, [sp, #28]
	mul	r10, ip, r3
	ldr	r3, [r4, #-3036]
	add	r3, r3, r10
	ldr	r2, [r3, #16]
	cmp	r2, r1
	bne	.L3619
	ldr	r1, [r3, #8]
	ldr	r3, [r4, #-2988]
	cmp	r1, r3
	bne	.L3620
	ldr	r3, [sp, #60]
	mov	r0, r8
	ldr	r2, [sp, #64]
	add	r1, r1, r3
	b	.L3650
.L3619:
	ldr	r1, [sp, #32]
	cmp	r2, r1
	bne	.L3620
	ldr	r1, [r3, #8]
	ldr	r3, [r4, #-2984]
	cmp	r1, r3
	bne	.L3620
	ldr	r3, .L3652+20
	ldr	r2, [sp, #68]
	ldrh	r0, [r3]
	ldr	r3, [sp, #32]
	mul	r0, r0, r3
	rsb	r0, r7, r0
	add	r0, r8, r0, asl #9
.L3650:
	bl	ftl_memcpy
.L3620:
	ldr	r2, [r4, #-3036]
	add	r3, r2, r10
	ldr	r1, [r2, r10]
	cmn	r1, #1
	streq	r1, [sp, #52]
	ldreq	r2, [fp, #-248]
	addeq	r2, r2, #1
	streq	r2, [fp, #-248]
	ldr	r2, [r3, #12]
	ldr	r1, [r3, #16]
	ldr	r2, [r2, #8]
	cmp	r1, r2
	beq	.L3622
	ldr	r2, [fp, #-248]
	ldr	r0, .L3652+32
	add	r2, r2, #1
	str	r2, [fp, #-248]
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
.L3622:
	ldr	r3, [r4, #-3036]
	add	r2, r3, r10
	ldr	r3, [r3, r10]
	cmp	r3, #256
	bne	.L3623
	ldr	r0, [r2, #4]
	ubfx	r0, r0, #10, #16
	bl	P2V_block_in_plane
	bl	FtlGcRefreshBlock
.L3623:
	ldr	r3, [sp, #44]
	add	r3, r3, #1
	str	r3, [sp, #44]
	cmp	r3, r6
	bne	.L3624
	mov	r6, #0
	b	.L3608
.L3651:
	ldr	r3, .L3652+36
	ldrh	r3, [r3, #-2]
	cmp	r3, #0
	beq	.L3626
	ldr	r0, [sp, #24]
	mov	r1, #1
	bl	ftl_do_gc
.L3626:
	ldr	r0, [sp, #52]
.L3605:
	ldr	r3, .L3652
	ldr	r2, [sp, #76]
	ldr	r3, [r3]
	cmp	r2, r3
	beq	.L3627
	bl	__stack_chk_fail
.L3627:
	add	sp, sp, #84
	@ sp needed
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L3653:
	.align	2
.L3652:
	.word	__stack_chk_guard
	.word	.LANCHOR1
	.word	.LANCHOR0
	.word	.LANCHOR2
	.word	.LANCHOR4
	.word	.LANCHOR2-3194
	.word	.LANCHOR2-3188
	.word	.LANCHOR2-3268
	.word	.LC55
	.word	.LANCHOR2-3056
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
	ldr	r5, .L3674
	ldr	r6, .L3674+4
	ldr	r8, .L3674+8
	ldr	r1, .L3674+12
	ldr	r0, .L3674+16
	str	r3, [r6, #3284]
	mov	r3, #0
	str	r3, [r8, #424]
	str	r3, [r5, #-2812]
	bl	printk
	ldr	r0, .L3674+20
	bl	FtlConstantsInit
	bl	FtlMemInit
	bl	FtlVariablesInit
	ldr	r3, [r5, #-3264]
	uxth	r0, r3
	bl	FtlFreeSysBlkQueueInit
	bl	FtlLoadBbt
	cmp	r0, #0
	ldrne	r0, .L3674+24
	bne	.L3673
	bl	FtlSysBlkInit
	subs	r4, r0, #0
	beq	.L3660
	ldr	r0, .L3674+28
.L3673:
	ldr	r1, .L3674+32
	bl	printk
	b	.L3659
.L3660:
	mov	r1, #1
	str	r1, [r6, #3284]
	bl	ftl_do_gc
	sub	r3, r5, #652
	ldrh	r9, [r3]
	mov	r7, r3
	cmp	r9, #15
	bhi	.L3661
	sub	r8, r8, #368
	sub	r5, r5, #3072
	movw	r6, #65535
.L3664:
	ldrh	r3, [r8]
	cmp	r3, r6
	bne	.L3662
	ldrh	r3, [r5]
	cmp	r3, r6
	bne	.L3662
	and	r0, r4, #7
	bl	List_get_gc_head_node
	uxth	r0, r0
	bl	FtlGcRefreshBlock
.L3662:
	mov	r0, #1
	mov	r1, r0
	bl	ftl_do_gc
	mov	r0, #0
	mov	r1, #1
	bl	ftl_do_gc
	ldrh	r2, [r7]
	add	r3, r9, #2
	cmp	r2, r3
	bhi	.L3659
	add	r4, r4, #1
	cmp	r4, #1024
	bne	.L3664
	b	.L3659
.L3661:
	ldr	r3, .L3674+36
	ldrb	r3, [r3, #3904]	@ zero_extendqisi2
	cmp	r3, #0
	beq	.L3659
	mov	r4, #128
.L3666:
	mov	r0, #1
	mov	r1, r0
	bl	ftl_do_gc
	subs	r4, r4, #1
	bne	.L3666
.L3659:
	mov	r0, #0
	ldmfd	sp!, {r3, r4, r5, r6, r7, r8, r9, pc}
.L3675:
	.align	2
.L3674:
	.word	.LANCHOR2
	.word	.LANCHOR1
	.word	.LANCHOR4
	.word	.LC141
	.word	.LC46
	.word	.LANCHOR0+3876
	.word	.LC158
	.word	.LC159
	.word	.LANCHOR3+108
	.word	.LANCHOR0
	.fnend
	.size	FtlInit, .-FtlInit
	.align	2
	.global	ftl_write
	.type	ftl_write, %function
ftl_write:
	.fnstart
	@ args = 0, pretend = 0, frame = 104
	@ frame_needed = 0, uses_anonymous_args = 0
	stmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.save {r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.pad #108
	sub	sp, sp, #108
	ldr	r10, .L3750
	str	r3, [sp, #8]
	ldr	r3, .L3750+4
	ldr	r3, [r3]
	str	r3, [sp, #100]
	ldr	r3, [r10, #-2812]
	cmp	r3, #0
	bne	.L3718
	mov	r8, r2
	ldr	r2, .L3750+8
	ldr	r2, [r2, #3284]
	cmn	r2, #1
	moveq	r0, r3
	beq	.L3677
	cmp	r0, #16
	mov	r7, r1
	bne	.L3678
	add	r0, r1, #256
	ldr	r2, [sp, #8]
	mov	r1, r8
	bl	FtlVendorPartWrite
	b	.L3677
.L3678:
	ldr	r3, .L3750+12
	add	r5, r1, r8
	ldr	r3, [r3, #3908]
	cmp	r5, r3
	mvnhi	r0, #0
	bhi	.L3677
	ldr	r6, .L3750+16
	mov	r3, #2048
	mov	r0, r1
	str	r3, [r6, #428]
	sub	r3, r10, #3184
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
	ldr	r3, [r10, #-3136]
	add	r3, r2, r3
	ldr	r2, [r10, #-3048]
	str	r3, [r10, #-3136]
	ldr	r3, [r6, #192]
	add	r3, r8, r3
	str	r3, [r6, #192]
	movcs	r3, #1
	movcc	r3, #0
	cmp	r2, #0
	str	r3, [sp, #24]
	beq	.L3680
	mov	r3, #36
	ldr	r9, [r10, #-3016]
	mul	r3, r3, r2
	ldr	r2, [sp, #12]
	sub	r3, r3, #36
	add	r9, r9, r3
	ldr	r3, [r9, #16]
	cmp	r2, r3
	bne	.L3681
	ldr	r3, [r10, #-3128]
	mov	r1, r4
	mov	r0, r7
	add	r3, r3, #1
	str	r3, [r10, #-3128]
	ldr	r3, [r6, #432]
	add	r3, r3, #1
	str	r3, [r6, #432]
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
	bne	.L3682
	ldr	r3, [r6, #432]
	cmp	r3, #2
	ble	.L3718
.L3682:
	ldr	r3, [sp, #8]
	rsb	r8, r4, r8
	add	r7, r7, r4
	str	r5, [sp, #4]
	add	r3, r3, r10
	str	r3, [sp, #8]
	ldr	r3, [sp, #12]
	add	r3, r3, #1
	str	r3, [sp, #12]
.L3681:
	mov	r3, #0
	str	r3, [r6, #432]
.L3680:
	ldr	r0, [sp, #12]
	ldr	r1, [sp, #28]
	bl	FtlCacheMetchLpa
	cmp	r0, #0
	beq	.L3683
	bl	FtlCacheWriteBack
.L3683:
	ldr	r5, .L3750+20
	mov	r3, #0
	str	r3, [sp, #16]
	add	r4, r5, #648
	str	r3, [sp, #32]
	str	r5, [r6, #416]
	ldr	r6, [sp, #12]
.L3684:
	ldr	r3, [sp, #4]
	cmp	r3, #0
	beq	.L3749
	ldrh	r2, [r5, #4]
	cmp	r2, #0
	bne	.L3685
	ldr	r3, .L3750+20
	ldr	r9, .L3750+8
	cmp	r5, r3
	bne	.L3686
	add	r0, r5, #48
	ldrh	r10, [r0, #4]
	cmp	r10, #0
	bne	.L3687
	bl	allocate_new_data_superblock
	str	r10, [r9, #3292]
.L3687:
	ldr	r0, .L3750+20
	bl	allocate_new_data_superblock
	ldr	r3, [r9, #3292]
	cmp	r3, #0
	ldrne	r5, .L3750+24
	bne	.L3688
.L3689:
	ldr	r5, .L3750+20
	b	.L3688
.L3686:
	ldrh	r3, [r3, #4]
	str	r2, [r9, #3292]
	cmp	r3, #0
	bne	.L3689
	mov	r0, r5
	bl	allocate_new_data_superblock
.L3688:
	ldrh	r3, [r5, #4]
	cmp	r3, #0
	bne	.L3690
	mov	r0, r5
	bl	allocate_new_data_superblock
.L3690:
	ldr	r3, .L3750+16
	str	r5, [r3, #416]
.L3685:
	ldr	r3, [r4, #-3052]
	ldr	r1, [r4, #-3048]
	ldrh	r2, [r5, #4]
	rsb	r3, r1, r3
	cmp	r2, r3
	movcs	r2, r3
	ldr	r3, [sp, #4]
	cmp	r2, r3
	movcc	r3, r2
	str	r3, [sp, #44]
	mov	r3, #0
.L3747:
	str	r3, [sp, #20]
	ldr	r3, [sp, #20]
	ldr	r2, [sp, #44]
	cmp	r3, r2
	beq	.L3692
	ldrh	r3, [r5, #4]
	cmp	r3, #0
	beq	.L3692
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
	beq	.L3693
	ldr	r3, .L3750+28
	ldrh	r2, [r3]
	add	r3, r8, r7
	mls	r3, r2, r6, r3
	cmp	r3, r2
	bne	.L3692
.L3693:
	add	r1, sp, #60
	mov	r2, #0
	mov	r0, r6
	str	ip, [sp, #52]
	bl	log2phys
	mov	r0, r5
	bl	get_new_active_ppa
	ldr	r10, [r4, #-3048]
	ldr	r1, [r4, #-3016]
	mov	r3, #36
	ldr	fp, .L3750+32
	mla	r1, r3, r10, r1
	ldrh	r2, [fp]
	str	r6, [r1, #16]
	str	r0, [r1, #4]
	mul	r0, r10, r2
	str	r3, [sp, #48]
	bic	r3, r0, #3
	str	r3, [sp, #36]
	ldr	r0, [sp, #36]
	ldr	r3, [r4, #-2964]
	add	r9, r3, r0
	ldrh	r0, [fp, #-2]
	str	r9, [r1, #12]
	str	r3, [sp, #40]
	mul	r10, r10, r0
	ldr	r0, [r4, #-2992]
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
	beq	.L3694
	cmp	r10, #0
	beq	.L3695
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
	b	.L3696
.L3695:
	cmp	ip, #0
	beq	.L3696
	ldr	r3, .L3750+28
	add	r2, r8, r7
	str	r10, [sp, #32]
	ldrh	r1, [r3]
	smulbb	r1, r1, r6
	rsb	r2, r1, r2
	uxth	r3, r2
	str	r3, [sp, #16]
.L3696:
	ldr	r3, .L3750+28
	ldr	r2, [sp, #16]
	ldrh	r3, [r3]
	cmp	r2, r3
	bne	.L3697
	cmp	r10, #0
	ldr	r0, .L3750
	moveq	r3, r2
	ldr	r2, .L3750
	muleq	r1, r6, r3
	ldreq	r3, [sp, #8]
	ldr	r2, [r2, #-3048]
	ldr	r0, [r0, #-3016]
	rsbeq	r1, r7, r1
	ldrne	r1, [sp, #8]
	addeq	r1, r3, r1, asl #9
	ldr	r3, [sp, #24]
	cmp	r3, #0
	mov	r3, #36
	mla	r3, r3, r2, r0
	strne	r1, [r3, #8]
	bne	.L3700
	ldr	r0, [r3, #8]
	ldr	r3, .L3750+36
	ldrh	r2, [r3]
	b	.L3745
.L3697:
	ldr	r2, [sp, #60]
	mov	r3, #36
	cmn	r2, #1
	beq	.L3701
	ldr	r1, [r4, #-3016]
	add	r0, sp, #64
	str	r2, [sp, #68]
	ldr	r2, [r4, #-3048]
	str	r6, [sp, #80]
	mla	r3, r3, r2, r1
	mov	r1, #1
	ldr	r2, [r3, #8]
	ldr	r3, [r3, #12]
	str	r2, [sp, #72]
	mov	r2, #0
	str	r3, [sp, #76]
	bl	FlashReadPages
	ldr	r3, [sp, #64]
	cmn	r3, #1
	ldreq	r2, .L3750+16
	ldreq	r3, [r2, #-248]
	addeq	r3, r3, #1
	streq	r3, [r2, #-248]
	beq	.L3704
.L3702:
	ldr	r3, [r9, #8]
	cmp	r3, r6
	beq	.L3704
	ldr	r2, .L3750+16
	ldr	r0, .L3750+40
	ldr	r3, [r2, #-248]
	add	r3, r3, #1
	str	r3, [r2, #-248]
	mov	r2, r6
	ldr	r1, [r9, #8]
	bl	printk
	b	.L3704
.L3701:
	ldr	r2, [r4, #-3048]
	ldr	r1, [r4, #-3016]
	mla	r3, r3, r2, r1
	mov	r1, #0
	ldr	r0, [r3, #8]
	ldr	r3, .L3750+36
	ldrh	r2, [r3]
	bl	ftl_memset
.L3704:
	cmp	r10, #0
	mov	r3, #36
	beq	.L3705
	ldr	r1, [r4, #-3016]
	ldr	r2, [r4, #-3048]
	mla	r3, r3, r2, r1
	ldr	r1, [sp, #8]
	ldr	r0, [r3, #8]
	ldr	r3, [sp, #32]
	add	r0, r0, r3, asl #9
	b	.L3748
.L3705:
	ldr	r1, [r4, #-3048]
	ldr	r2, [r4, #-3016]
	mla	r3, r3, r1, r2
	ldr	r2, .L3750+28
	ldrh	r1, [r2]
	ldr	r0, [r3, #8]
	mul	r1, r1, r6
	ldr	r3, [sp, #8]
	rsb	r1, r7, r1
	add	r1, r3, r1, asl #9
.L3748:
	ldr	r3, [sp, #16]
	mov	r2, r3, asl #9
	b	.L3745
.L3694:
	ldr	r2, [sp, #24]
	cmp	r2, #0
	ldr	r2, [r4, #-3048]
	beq	.L3706
	ldr	r1, [r4, #-3016]
	mla	r3, r3, r2, r1
	ldr	r2, .L3750+28
	ldrh	fp, [r2]
	ldr	r2, [sp, #8]
	mul	fp, fp, r6
	rsb	fp, r7, fp
	add	fp, r2, fp, asl #9
	str	fp, [r3, #8]
	b	.L3700
.L3706:
	ldr	r0, [r4, #-3016]
	mla	r3, r3, r2, r0
	ldr	r2, .L3750+28
	ldrh	r1, [r2]
	ldrh	r2, [fp, #-2]
	ldr	r0, [r3, #8]
	mul	r1, r1, r6
	ldr	r3, [sp, #8]
	rsb	r1, r7, r1
	add	r1, r3, r1, asl #9
.L3745:
	bl	ftl_memcpy
.L3700:
	ldr	r3, .L3750+44
	ldr	r2, [sp, #40]
	ldr	r1, [sp, #36]
	strh	r3, [r2, r1]	@ movhi
	ldr	r3, [r4, #-3144]
	str	r6, [r9, #8]
	add	r6, r6, #1
	str	r3, [r9, #4]
	add	r3, r3, #1
	cmn	r3, #1
	moveq	r3, #0
	str	r3, [r4, #-3144]
	ldr	r3, [sp, #60]
	str	r3, [r9, #12]
	ldrh	r3, [r5]
	strh	r3, [r9, #2]	@ movhi
	ldr	r3, [r4, #-3048]
	add	r3, r3, #1
	str	r3, [r4, #-3048]
	ldr	r3, [sp, #20]
	add	r3, r3, #1
	b	.L3747
.L3692:
	ldr	r3, [sp, #4]
	ldr	r2, [sp, #20]
	ldr	r1, [sp, #24]
	rsb	r3, r2, r3
	ldr	r2, [r4, #-3052]
	str	r3, [sp, #4]
	ldr	r3, [r4, #-3048]
	cmp	r3, r2
	orrcs	r1, r1, #1
	cmp	r1, #0
	bne	.L3710
	ldrh	r3, [r5, #4]
	cmp	r3, #0
	beq	.L3710
.L3712:
	mov	r3, #0
	str	r3, [sp, #24]
	b	.L3684
.L3710:
	bl	FtlCacheWriteBack
	ldr	r2, .L3750
	mov	r3, #0
	str	r3, [r2, #-3048]
	ldr	r3, [sp, #4]
	cmp	r3, #1
	bhi	.L3684
	b	.L3712
.L3749:
	mov	r0, r3
	ldr	r2, [sp, #12]
	ldr	r3, [sp, #28]
	ldr	r5, .L3750+48
	rsb	r1, r2, r3
	bl	ftl_do_gc
	ldrh	r4, [r5]
	cmp	r4, #5
	bls	.L3723
	cmp	r4, #31
	bhi	.L3718
	ldr	r3, .L3750+12
	ldrb	r3, [r3]	@ zero_extendqisi2
	cmp	r3, #0
	bne	.L3718
.L3723:
	ldr	r6, .L3750
	ldr	r4, [sp, #4]
	ldr	r7, .L3750+52
	sub	r8, r6, #3072
.L3736:
	ldrh	r2, [r7]
	movw	r3, #65535
	cmp	r2, r3
	bne	.L3716
	ldrh	r3, [r8]
	cmp	r3, r2
	bne	.L3716
	ldr	r2, .L3750+56
	ldrh	r2, [r2]
	cmp	r2, r3
	bne	.L3716
	and	r0, r4, #7
	bl	List_get_gc_head_node
	uxth	r0, r0
	bl	FtlGcRefreshBlock
.L3716:
	ldr	r3, .L3750+60
	mov	r0, #1
	mov	r1, r0
	mov	r2, #128
	strh	r2, [r3]	@ movhi
	strh	r2, [r3, #-2]	@ movhi
	bl	ftl_do_gc
	mov	r0, #0
	mov	r1, #1
	bl	ftl_do_gc
	ldr	r3, [r6, #-2812]
	cmp	r3, #0
	bne	.L3718
	ldrh	r3, [r5]
	cmp	r3, #2
	bhi	.L3718
	add	r4, r4, #1
	cmp	r4, #256
	bne	.L3736
.L3718:
	mov	r0, #0
.L3677:
	ldr	r3, .L3750+4
	ldr	r2, [sp, #100]
	ldr	r3, [r3]
	cmp	r2, r3
	beq	.L3717
	bl	__stack_chk_fail
.L3717:
	add	sp, sp, #108
	@ sp needed
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L3751:
	.align	2
.L3750:
	.word	.LANCHOR2
	.word	__stack_chk_guard
	.word	.LANCHOR1
	.word	.LANCHOR0
	.word	.LANCHOR4
	.word	.LANCHOR2-648
	.word	.LANCHOR2-600
	.word	.LANCHOR2-3194
	.word	.LANCHOR2-3188
	.word	.LANCHOR2-3190
	.word	.LC160
	.word	-3947
	.word	.LANCHOR2-652
	.word	.LANCHOR4-368
	.word	.LANCHOR2-3070
	.word	.LANCHOR2-3062
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
	@ args = 0, pretend = 0, frame = 64
	@ frame_needed = 0, uses_anonymous_args = 0
	ldr	r3, .L3772
	stmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.save {r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.pad #68
	sub	sp, sp, #68
	ldr	r2, [r3]
	str	r3, [sp, #4]
	str	r2, [sp, #60]
	ldr	r2, .L3772+4
	ldrb	r2, [r2, #3904]	@ zero_extendqisi2
	cmp	r2, #0
	beq	.L3755
	ldr	r8, .L3772+8
	movw	r3, #270
	ldr	r4, .L3772+12
	ldr	r0, .L3772+16
	ldrh	r7, [r8, r3]
	sub	r9, r4, #648
	ldr	r3, [r4, #-2940]
	sub	r5, r4, #600
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
.L3757:
	subs	r5, r5, #1
	beq	.L3761
	mov	r0, #1
	mov	r1, r0
	bl	ftl_do_gc
	ldr	r3, [r4, #-2940]
	ldrh	r3, [r3, r6]
	cmp	r3, #0
	bne	.L3757
.L3761:
	ldr	r3, [r4, #-2940]
	mov	r1, r7
	ldr	r0, .L3772+16
	ldr	r5, .L3772+12
	ldrh	r2, [r3, r6]
	bl	printk
	ldr	r3, [r4, #-2940]
	ldrh	r10, [r3, r6]
	cmp	r10, #0
	bne	.L3759
	add	r0, sp, #64
	mov	fp, #36
	strh	r7, [r0, #-52]!	@ movhi
	bl	make_superblock
	sub	r3, r5, #3264
	ldr	r9, [r5, #-3024]
	mov	r5, r10
	ldrh	lr, [r3, #-4]
	mov	r3, r10
	mov	ip, r3
	movw	r10, #65535
	add	r0, sp, #26
.L3762:
	uxth	r2, r3
	cmp	r2, lr
	bcs	.L3771
	ldrh	r2, [r0, #2]!
	add	r3, r3, #1
	cmp	r2, r10
	movne	r2, r2, asl #10
	mlane	r1, fp, r5, r9
	addne	r5, r5, #1
	uxthne	r5, r5
	stmneib	r1, {r2, ip}
	strne	ip, [r1, #12]
	b	.L3762
.L3771:
	ldr	r3, [r4, #-2940]
	mov	r1, r7
	ldr	r0, .L3772+20
	ldrh	r2, [r3, r6]
	bl	printk
	mov	r1, #0
	mov	r2, r5
	ldr	r0, [r4, #-3024]
	bl	FlashEraseBlocks
	ldr	r0, [r4, #-3024]
	mov	r1, #1
	mov	r2, r5
	bl	FlashEraseBlocks
.L3759:
	movw	r3, #270
	mvn	r2, #0
	strh	r2, [r8, r3]	@ movhi
.L3755:
	ldr	r3, [sp, #4]
	ldr	r2, [sp, #60]
	ldr	r3, [r3]
	cmp	r2, r3
	beq	.L3765
	bl	__stack_chk_fail
.L3765:
	add	sp, sp, #68
	@ sp needed
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L3773:
	.align	2
.L3772:
	.word	__stack_chk_guard
	.word	.LANCHOR0
	.word	.LANCHOR4
	.word	.LANCHOR2
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
	.type	__func__.20209, %object
	.size	__func__.20209, 11
__func__.20209:
	.ascii	"FtlMemInit\000"
	.space	1
	.type	__func__.20952, %object
	.size	__func__.20952, 12
__func__.20952:
	.ascii	"FtlCheckVpc\000"
	.type	__func__.20984, %object
	.size	__func__.20984, 17
__func__.20984:
	.ascii	"FtlDumpBlockInfo\000"
	.space	3
	.type	__func__.21003, %object
	.size	__func__.21003, 16
__func__.21003:
	.ascii	"FtlScanAllBlock\000"
	.type	__func__.21269, %object
	.size	__func__.21269, 17
__func__.21269:
	.ascii	"ftl_scan_all_ppa\000"
	.space	3
	.type	__func__.21251, %object
	.size	__func__.21251, 21
__func__.21251:
	.ascii	"FtlVpcCheckAndModify\000"
	.space	3
	.type	__func__.20282, %object
	.size	__func__.20282, 8
__func__.20282:
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
	.ascii	"FTL version: 5.0.57 20190927\000"
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
