/*
 * Copyright (c) 2016-2018, Fuzhou Rockchip Electronics Co., Ltd
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 * date: 2020-09-23
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
	.file	"rk_ftlv5_arm_v8.c"
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
	ldr	ip, .L49
	str	lr, [sp, #-4]!
	.save {lr}
	add	r3, ip, r0, asl #3
	ldr	ip, [ip, #44]
	ldr	r2, [r3, #12]
	ldrb	r3, [r3, #16]	@ zero_extendqisi2
	ldrb	ip, [ip, #7]	@ zero_extendqisi2
	mov	r3, r3, asl #8
	cmp	ip, #1
	addeq	ip, r2, r3
	add	r3, r2, r3
	moveq	lr, #38
	mov	r2, #0
	streq	lr, [ip, #2056]
	str	r2, [r3, #2056]
	str	r2, [r3, #2052]
	str	r2, [r3, #2052]
	uxtb	r2, r1
	str	r2, [r3, #2052]
	mov	r2, r1, lsr #8
	str	r2, [r3, #2052]
	mov	r2, r1, lsr #16
	str	r2, [r3, #2052]
	mov	r2, #48
	str	r2, [r3, #2056]
	ldr	lr, [sp], #4
	b	FlashSetRandomizer
.L50:
	.align	2
.L49:
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
	ldr	ip, .L56
	stmfd	sp!, {r4, lr}
	.save {r4, lr}
	add	r3, ip, r0, asl #3
	ldrb	ip, [ip, #64]	@ zero_extendqisi2
	uxtb	r4, r1
	ldr	r2, [r3, #12]
	mov	lr, r1, lsr #8
	ldrb	r3, [r3, #16]	@ zero_extendqisi2
	cmp	ip, #1
	mov	ip, r1, lsr #16
	mov	r3, r3, asl #8
	add	r3, r2, r3
	bne	.L52
	mov	r2, #6
	str	r2, [r3, #2056]
	mov	r2, #0
	str	r2, [r3, #2052]
	str	r2, [r3, #2052]
	str	r4, [r3, #2052]
	str	lr, [r3, #2052]
	str	ip, [r3, #2052]
	b	.L55
.L52:
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
.L55:
	mov	r2, #224
	str	r2, [r3, #2056]
	ldmfd	sp!, {r4, lr}
	b	FlashSetRandomizer
.L57:
	.align	2
.L56:
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
	ldr	ip, .L59
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
.L60:
	.align	2
.L59:
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
	ldr	lr, .L67
	add	r0, lr, r0, asl #3
	ldrb	r3, [r0, #16]	@ zero_extendqisi2
	ldr	ip, [r0, #12]
	mov	r3, r3, asl #8
	beq	.L62
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
.L62:
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
.L68:
	.align	2
.L67:
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
	ldr	ip, .L71
	mov	r2, r1, lsr #16
	str	lr, [sp, #-4]!
	.save {lr}
	add	lr, ip, r0, asl #3
	ldrb	ip, [ip, #59]	@ zero_extendqisi2
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
.L72:
	.align	2
.L71:
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
	ldr	r3, .L75
	add	r0, r3, r0, asl #3
	ldr	r3, .L75+4
	ldrb	r5, [r0, #16]	@ zero_extendqisi2
	ldr	r4, [r0, #12]
	ldr	r3, [r3, #4]
	add	r4, r4, r5, asl #8
	ldr	r0, .L75+8
	blx	r3
	mov	r3, #16
	str	r3, [r4, #2056]
	ldmfd	sp!, {r3, r4, r5, pc}
.L76:
	.align	2
.L75:
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
	ldr	r2, .L78
	add	r0, r2, r0, asl #3
	ldrb	r2, [r2, #58]	@ zero_extendqisi2
	ldrb	r1, [r0, #16]	@ zero_extendqisi2
	ldr	r3, [r0, #12]
	add	r3, r3, r1, asl #8
	str	r2, [r3, #2056]
	bx	lr
.L79:
	.align	2
.L78:
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
	ldr	r3, .L84
	add	r1, r0, r1
.L81:
	cmp	r0, r1
	beq	.L83
	mov	r2, r3, asl #5
	ldrb	ip, [r0], #1	@ zero_extendqisi2
	add	r2, r2, r3, lsr #2
	add	r2, r2, ip
	eor	r3, r3, r2
	b	.L81
.L83:
	mov	r0, r3
	bx	lr
.L85:
	.align	2
.L84:
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
	bne	.L88
	ldr	r3, .L131
.L89:
	mov	r2, r0, asl #1
	strh	r0, [r2, r3]	@ movhi
	add	r0, r0, #1
	cmp	r0, #512
	bne	.L89
.L93:
	mov	r2, #2048
	ldr	r0, .L131+4
	mov	r1, #255
	uxth	r4, r4
	bl	ftl_memset
	ldr	r2, .L131
	mov	r3, #0
	add	r0, r2, #1024
	b	.L90
.L88:
	cmp	r0, #1
	bne	.L91
	ldr	ip, .L131
	mov	r3, #0
.L92:
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
	cmp	r3, #512
	strh	r2, [lr, ip]	@ movhi
	bne	.L92
	b	.L93
.L91:
	cmp	r0, #2
	bne	.L94
	ldr	r1, .L131
	mov	r3, #0
.L95:
	uxth	r2, r3
	mov	r0, r3, asl #1
	cmp	r2, #1
	add	r3, r3, #1
	mov	ip, r2, asl #1
	subhi	r2, ip, #1
	cmp	r3, #512
	strh	r2, [r0, r1]	@ movhi
	bne	.L95
	b	.L93
.L94:
	cmp	r0, #3
	bne	.L96
	ldr	ip, .L131
	mov	r3, #0
.L97:
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
	cmp	r3, #512
	strh	r2, [lr, ip]	@ movhi
	bne	.L97
	b	.L93
.L96:
	cmp	r0, #4
	mov	r3, #0
	bne	.L98
	ldr	r2, .L131+8
	strh	r3, [r2, #80]	@ movhi
	mov	r3, #1
	strh	r0, [r2, #88]	@ movhi
	strh	r3, [r2, #82]	@ movhi
	mov	r3, #2
	strh	r3, [r2, #84]	@ movhi
	mov	r3, #3
	strh	r3, [r2, #86]	@ movhi
	mov	r3, #5
	strh	r3, [r2, #90]	@ movhi
	mov	r3, #7
	strh	r3, [r2, #92]	@ movhi
	mov	r3, #8
	strh	r3, [r2, #94]!	@ movhi
.L99:
	tst	r3, #1
	movne	r1, #7
	moveq	r1, #6
	rsb	r1, r1, r3, asl #1
	add	r3, r3, #1
	strh	r1, [r2, #2]!	@ movhi
	uxth	r3, r3
	cmp	r3, #512
	bne	.L99
	b	.L93
.L98:
	cmp	r0, #5
	bne	.L100
	ldr	r2, .L131
.L101:
	mov	r1, r3, asl #1
	strh	r3, [r1, r2]	@ movhi
	add	r3, r3, #1
	cmp	r3, #16
	bne	.L101
	ldr	r2, .L131+12
.L102:
	strh	r3, [r2, #2]!	@ movhi
	add	r3, r3, #2
	uxth	r3, r3
	cmp	r3, #1008
	bne	.L102
	b	.L93
.L100:
	cmp	r0, #6
	bne	.L103
	ldr	r1, .L131+16
.L104:
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
	cmp	r3, #512
	bne	.L104
	b	.L93
.L103:
	cmp	r0, #9
	bne	.L105
	ldr	r2, .L131+8
	movw	r1, #1021
	strh	r3, [r2, #80]	@ movhi
	mov	r3, #1
	strh	r3, [r2, #82]	@ movhi
	mov	r3, #2
	strh	r3, [r2, #84]!	@ movhi
	mov	r3, #3
.L106:
	strh	r3, [r2, #2]!	@ movhi
	add	r3, r3, #2
	uxth	r3, r3
	cmp	r3, r1
	bne	.L106
	b	.L93
.L105:
	cmp	r0, #10
	bne	.L107
	ldr	r2, .L131
.L108:
	mov	r1, r3, asl #1
	strh	r3, [r1, r2]	@ movhi
	add	r3, r3, #1
	cmp	r3, #63
	bne	.L108
	ldr	r2, .L131+20
	movw	r1, #961
.L109:
	strh	r3, [r2, #2]!	@ movhi
	add	r3, r3, #2
	uxth	r3, r3
	cmp	r3, r1
	bne	.L109
	b	.L93
.L107:
	cmp	r0, #11
	bne	.L110
	ldr	r2, .L131
	mov	r3, #0
.L111:
	mov	r1, r3, asl #1
	strh	r3, [r1, r2]	@ movhi
	add	r3, r3, #1
	cmp	r3, #8
	bne	.L111
	ldr	r1, .L131+24
.L112:
	tst	r3, #1
	movne	r2, #7
	moveq	r2, #6
	rsb	r2, r2, r3, asl #1
	add	r3, r3, #1
	strh	r2, [r1, #2]!	@ movhi
	uxth	r3, r3
	cmp	r3, #512
	bne	.L112
	b	.L93
.L110:
	cmp	r0, #12
	bne	.L93
	ldr	r3, .L131+8
	mov	r2, #0
	strh	r2, [r3, #80]	@ movhi
	mov	r2, #1
	strh	r2, [r3, #82]	@ movhi
	mov	r2, #2
	strh	r2, [r3, #84]	@ movhi
	mov	r2, #3
	strh	r2, [r3, #86]!	@ movhi
	mov	r2, #4
.L113:
	sub	r1, r2, #1
	add	r1, r1, r2, lsr #1
	add	r2, r2, #1
	strh	r1, [r3, #2]!	@ movhi
	uxth	r2, r2
	cmp	r2, #512
	bne	.L113
	b	.L93
.L90:
	uxth	r1, r3
	cmp	r1, r4
	bcs	.L130
	mov	r1, r3, asl #1
	add	r3, r3, #1
	ldrh	r1, [r1, r2]
	mov	ip, r1, asl #1
	strh	r1, [r0, ip]	@ movhi
	b	.L90
.L130:
	ldmfd	sp!, {r4, pc}
.L132:
	.align	2
.L131:
	.word	.LANCHOR0+80
	.word	.LANCHOR0+1104
	.word	.LANCHOR0
	.word	.LANCHOR0+110
	.word	.LANCHOR0+78
	.word	.LANCHOR0+204
	.word	.LANCHOR0+94
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
	ldr	r3, .L149
	ldr	r4, .L149+4
	ldr	r9, .L149+8
	ldrh	r0, [r3, #10]
	strb	r5, [r4, #3152]
	strb	r5, [r4, #3153]
	bl	FlashBlockAlignInit
	mov	r1, r5
	mov	r2, #8
	ldr	r0, .L149+12
	bl	ftl_memset
	mov	r1, r5
	mov	r2, #32
	ldr	r0, .L149+16
	bl	ftl_memset
	ldr	r0, .L149+20
	mov	r1, r5
	mov	r2, #128
	bl	ftl_memset
	ldr	r7, [r4, #44]
	add	r6, r7, #1
.L136:
	mov	r0, r6
	add	r1, r9, r5, asl #3
	ldrb	r2, [r7]	@ zero_extendqisi2
	bl	FlashMemCmp8
	ldr	r8, .L149+8
	cmp	r0, #0
	bne	.L135
	ldrb	r3, [r4, #3152]	@ zero_extendqisi2
	add	r2, r4, r3, asl #2
	str	r0, [r2, #3164]
	add	r2, r3, #1
	add	r3, r4, r3
	strb	r2, [r4, #3152]
	strb	r5, [r3, #3156]
.L135:
	add	r5, r5, #1
	cmp	r5, #4
	bne	.L136
	ldrb	r3, [r4, #3152]	@ zero_extendqisi2
	strb	r3, [r4, #3153]
	ldrb	r3, [r7, #8]	@ zero_extendqisi2
	cmp	r3, #2
	beq	.L137
.L141:
	ldrb	r3, [r7, #13]	@ zero_extendqisi2
	ldrb	r2, [r4, #3152]	@ zero_extendqisi2
	smulbb	r2, r2, r3
	ldrh	r3, [r7, #14]
	smulbb	r3, r2, r3
	movw	r2, #3324
	strh	r3, [r4, r2]	@ movhi
	ldmfd	sp!, {r3, r4, r5, r6, r7, r8, r9, pc}
.L137:
	ldr	r3, .L149+4
	mov	r5, #0
	ldr	r9, [r3, #4]
.L140:
	mov	r0, r6
	add	r1, r8, r5, asl #3
	ldrb	r2, [r7]	@ zero_extendqisi2
	bl	FlashMemCmp8
	cmp	r0, #0
	bne	.L138
	ldrb	r1, [r7, #13]	@ zero_extendqisi2
	ldrh	r3, [r7, #14]
	ldrb	r2, [r4, #3152]	@ zero_extendqisi2
	mul	r1, r9, r1
	and	r3, r3, #65280
	add	r0, r4, r2, asl #2
	mul	r3, r3, r1
	str	r3, [r0, #3164]
	ldrb	r1, [r7, #23]	@ zero_extendqisi2
	cmp	r1, #0
	movne	r3, r3, asl #1
	strne	r3, [r0, #3164]
	add	r3, r2, #1
	add	r2, r4, r2
	strb	r3, [r4, #3152]
	strb	r5, [r2, #3156]
.L138:
	add	r5, r5, #1
	cmp	r5, #4
	bne	.L140
	b	.L141
.L150:
	.align	2
.L149:
	.word	.LANCHOR1+256
	.word	.LANCHOR0
	.word	IDByte
	.word	.LANCHOR0+3156
	.word	.LANCHOR0+3164
	.word	.LANCHOR0+3196
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
	ldr	r1, .L153
	bl	ftl_memcpy
	mov	r0, #0
	ldmfd	sp!, {r3, pc}
.L154:
	.align	2
.L153:
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
	ldr	r8, .L168
.L159:
	mov	r4, r6, asl #5
	ldr	r1, .L168+4
	add	r0, r4, #1
	ldrb	r2, [r8, r6, asl #5]	@ zero_extendqisi2
	add	r0, r8, r0
	ldr	r5, .L168+8
	bl	FlashMemCmp8
	add	r9, r5, #288
	subs	r7, r0, #0
	bne	.L156
	adds	r9, r9, r4
	beq	.L162
	add	r4, r5, r4
	add	r5, r5, #3040
	ldrb	r3, [r4, #310]	@ zero_extendqisi2
	mov	r4, r7
	b	.L161
.L156:
	add	r6, r6, #1
	cmp	r6, #86
	bne	.L159
	b	.L162
.L167:
	add	r4, r4, #1
	cmp	r4, #4
	beq	.L160
.L161:
	ldrb	r2, [r5, r4, asl #5]	@ zero_extendqisi2
	cmp	r2, r3
	bne	.L167
.L160:
	ldr	r6, .L168+12
	mov	r2, #32
	ldr	r1, .L168+16
	ldr	r0, .L168+20
	add	r1, r1, r4, asl #5
	bl	ftl_memcpy
	mov	r0, r6
	mov	r1, r9
	mov	r2, #32
	bl	ftl_memcpy
	ldrh	r0, [r6, #10]
	bl	FlashBlockAlignInit
	b	.L157
.L162:
	mvn	r7, #0
.L157:
	mov	r0, r7
	ldmfd	sp!, {r3, r4, r5, r6, r7, r8, r9, pc}
.L169:
	.align	2
.L168:
	.word	.LANCHOR1+288
	.word	IDByte
	.word	.LANCHOR1
	.word	.LANCHOR1+256
	.word	.LANCHOR1+3040
	.word	.LANCHOR0+48
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
	ldr	r3, .L171
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
.L172:
	.align	2
.L171:
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
	ldr	r3, .L179
	mov	fp, r1
	ldr	r7, .L179+4
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
	bne	.L175
	ldr	r3, .L179+8
	ldrb	r3, [r3, #-2744]	@ zero_extendqisi2
	cmp	r3, #0
	addeq	r1, r7, r1, asl #1
	ldreqh	r1, [r1, #80]
.L175:
	add	lr, r7, r0, asl #2
	ldrb	r3, [sp, #48]	@ zero_extendqisi2
	ldr	ip, [lr, #3164]
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
.L180:
	.align	2
.L179:
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
	ldr	r2, .L186
	ldr	r0, .L186+4
	mov	ip, #1
	ldr	r3, [r2, #44]
	ldrb	r1, [r3, #9]	@ zero_extendqisi2
	ldr	r3, [r2, #4]
	smulbb	r3, r1, r3
	strh	r3, [r4, #4]	@ unaligned
	ldr	r3, .L186+8
	ldrb	r1, [r3, #-2743]	@ zero_extendqisi2
	ldr	r3, [r3, #-2740]
	strb	r1, [r4, #7]
	str	r3, [r4]	@ unaligned
	ldr	r3, [r2, #44]
	ldrb	r1, [r3, #9]	@ zero_extendqisi2
	strb	r1, [r4, #6]
	mov	r1, #32
	strb	r1, [r4, #8]
	ldrb	r1, [r2, #3152]	@ zero_extendqisi2
	ldrb	r3, [r3, #7]	@ zero_extendqisi2
	strb	r3, [r4, #9]
	mov	r3, #0
	strb	r3, [r4, #10]
.L182:
	uxtb	r2, r3
	cmp	r2, r1
	bcs	.L185
	ldrb	lr, [r3, r0]	@ zero_extendqisi2
	add	r3, r3, #1
	ldrb	r2, [r4, #10]	@ zero_extendqisi2
	orr	r2, r2, ip, asl lr
	strb	r2, [r4, #10]
	b	.L182
.L185:
	ldmfd	sp!, {r4, pc}
.L187:
	.align	2
.L186:
	.word	.LANCHOR0
	.word	.LANCHOR0+3156
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
	ldr	r3, .L189
	ldr	r2, [r3, #-2736]
	str	r0, [r3, #-2736]
	mov	r0, r2
	bx	lr
.L190:
	.align	2
.L189:
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
	ldr	r3, .L192
	ldr	r3, [r3, #44]
	ldrb	r0, [r3, #9]	@ zero_extendqisi2
	bx	lr
.L193:
	.align	2
.L192:
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
	ldr	r3, .L196
	add	r0, r3, r0, asl #3
	ldrb	r2, [r0, #16]	@ zero_extendqisi2
	ldr	r0, [r0, #12]
	add	r2, r2, #8
	add	r0, r0, r2, asl #8
	bx	lr
.L197:
	.align	2
.L196:
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
	ldr	r3, .L199
	ldr	r2, [r3, #-2808]
	mov	r3, r0, asl #8
	orr	r0, r3, r0, asl #16
	orr	r3, r0, #1
	str	r3, [r2, #304]
	bx	lr
.L200:
	.align	2
.L199:
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
	ldr	r3, .L202
	orr	r0, r0, #16640
	ldr	r3, [r3, #-2808]
	str	r0, [r3, #344]
	bx	lr
.L203:
	.align	2
.L202:
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
	ldr	r3, .L207
	cmp	r0, #0
	ldr	r2, [r3, #-2808]
	ldr	r3, [r2]
	bfieq	r3, r0, #13, #1
	orrne	r3, r3, #253952
	str	r3, [r2]
	bx	lr
.L208:
	.align	2
.L207:
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
	ldr	r3, .L216
	ands	r1, r0, #6
	ldr	r2, [r3, #-2808]
	ldr	r3, [r2]
	bfieq	r3, r1, #13, #1
	beq	.L212
	orr	r3, r3, #24576
	movw	r1, #8322
	bfc	r3, #15, #1
	str	r1, [r2, #344]
	orr	r3, r3, #196608
	ldr	r1, .L216+4
	tst	r0, #4
	orrne	r3, r3, #32768
	str	r1, [r2, #304]
	mov	r1, #38
	str	r1, [r2, #308]
	mov	r1, #39
	str	r1, [r2, #308]
.L212:
	str	r3, [r2]
	mov	r0, #0
	bx	lr
.L217:
	.align	2
.L216:
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
	ldr	r2, .L219
	add	r0, r2, r0, asl #3
	mov	r2, #1
	ldr	r1, [r0, #12]
	ldrb	r0, [r0, #16]	@ zero_extendqisi2
	ldr	r3, [r1]
	mov	r2, r2, asl r0
	bfi	r3, r2, #0, #8
	str	r3, [r1]
	bx	lr
.L220:
	.align	2
.L219:
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
	ldr	r3, .L222
	add	r0, r3, r0, asl #3
	ldr	r2, [r0, #12]
	ldr	r3, [r2]
	bfc	r3, #0, #8
	bfc	r3, #17, #1
	str	r3, [r2]
	bx	lr
.L223:
	.align	2
.L222:
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
	ldr	r3, .L226
	add	r0, r0, #3
	umull	r0, r1, r0, r3
	ldr	r3, .L226+4
	ldr	r3, [r3, #8]
	mov	r0, r1, lsr #6
	blx	r3
	mov	r0, #0
	ldmfd	sp!, {r3, pc}
.L227:
	.align	2
.L226:
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
	ldr	r3, .L230
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
.L231:
	.align	2
.L230:
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
	ldr	r8, .L242
	mov	r5, r0
	ldr	r10, .L242+4
	mov	r6, r1
	add	r9, r8, #3168
	mov	r4, #0
.L233:
	ldrb	r3, [r10]	@ zero_extendqisi2
	cmp	r4, r3
	bcs	.L241
	mov	r3, #85
	str	r3, [r5, #8]
	ldrsb	r3, [r4, r9]
	mov	r0, #200
	str	r3, [r5, #4]
	bl	NandcDelayns
	ldr	r3, .L242+8
	ldrb	r3, [r3]	@ zero_extendqisi2
	cmp	r3, #34
	addeq	r3, r4, r7
	addeq	r3, r9, r3
	beq	.L240
	cmp	r3, #35
	addne	r3, r8, r6
	addne	r3, r3, #3312
	ldrnesb	r3, [r3]
	bne	.L239
	ldr	r3, .L242+12
	add	r2, r4, r7
	add	r3, r3, r2
.L240:
	ldrsb	r3, [r3, #5]
.L239:
	str	r3, [r5]
	add	r4, r4, #1
	b	.L233
.L241:
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, pc}
.L243:
	.align	2
.L242:
	.word	.LANCHOR1
	.word	g_maxRegNum
	.word	g_retryMode
	.word	.LANCHOR1+3216
	.fnend
	.size	ToshibaSetRRPara, .-ToshibaSetRRPara
	.align	2
	.global	SamsungSetRRPara
	.type	SamsungSetRRPara, %function
SamsungSetRRPara:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	ldr	r3, .L249
	stmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, lr}
	.save {r4, r5, r6, r7, r8, r9, r10, lr}
	add	r1, r3, r1, asl #2
	ldr	r8, .L249+4
	mov	r4, #0
	add	r5, r1, #3
	mov	r6, r0
	mov	r7, r3
	mov	r9, #161
	mov	r10, r4
.L245:
	ldrb	r3, [r8]	@ zero_extendqisi2
	cmp	r4, r3
	bcs	.L248
	str	r9, [r6, #8]
	mov	r0, #300
	str	r10, [r6]
	ldrsb	r3, [r4, r7]
	add	r4, r4, #1
	str	r3, [r6]
	ldrsb	r3, [r5, #1]!
	str	r3, [r6]
	bl	NandcDelayns
	b	.L245
.L248:
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, pc}
.L250:
	.align	2
.L249:
	.word	.LANCHOR1+3320
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
	ldr	r3, .L260
	mov	r10, r2
	mov	r7, r0
	mov	r9, r1
	ldr	r2, [r3, #44]
	ldrb	r4, [r2, #19]	@ zero_extendqisi2
	mov	r2, r0, asl #3
	cmp	r4, #6
	ldreq	r4, .L260+4
	addeq	r4, r4, r0, asl #6
	addeq	r4, r4, #20
	addeq	r4, r4, r6, asl #2
	beq	.L253
.L252:
	cmp	r4, #7
	bne	.L254
	ldr	r1, .L260+4
	mov	r4, #160
	mla	r4, r4, r0, r1
	add	r1, r6, r6, asl #2
	add	r4, r4, #28
	add	r4, r4, r1, asl #1
	b	.L253
.L254:
	cmp	r4, #8
	addne	r4, r6, r2
	addeq	r1, r6, r6, asl #2
	ldrne	r1, .L260+4
	ldreq	r4, .L260+8
	addne	r4, r1, r4, asl #3
	addeq	r4, r4, r1
	addne	r4, r4, #20
.L253:
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
.L256:
	cmp	r2, r10
	beq	.L259
	ldrb	r3, [r2, #1]!	@ zero_extendqisi2
	mov	r0, #200
	str	r3, [r5, #2052]
	str	r2, [sp, #4]
	bl	NandcDelayns
	ldrsb	r3, [r4, #1]!
	str	r3, [r5, #2048]
	ldr	r2, [sp, #4]
	b	.L256
.L259:
	add	r8, r8, fp
	mov	r3, #22
	mov	r0, r7
	str	r3, [r8, #2056]
	bl	NandcFlashDeCs
	ldr	r3, .L260+12
	add	r7, r3, r7
	strb	r6, [r7, #-1880]
	add	sp, sp, #12
	@ sp needed
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L261:
	.align	2
.L260:
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
	ldr	r3, .L270
	ldr	r3, [r3, #44]
	ldrb	r3, [r3, #19]	@ zero_extendqisi2
	sub	r3, r3, #1
	cmp	r3, #7
	bxhi	lr
	stmfd	sp!, {r4, r5, r6, lr}
	.save {r4, r5, r6, lr}
	mov	r4, #0
	ldr	r5, .L270+4
	ldr	r6, .L270+8
.L263:
	ldrb	r3, [r5, r4, asl #3]	@ zero_extendqisi2
	uxtb	r0, r4
	cmp	r3, #173
	bne	.L264
	ldrb	r1, [r6, #-2731]	@ zero_extendqisi2
	mov	r3, #0
	ldr	r2, .L270+12
	bl	HynixSetRRPara
.L264:
	add	r4, r4, #1
	cmp	r4, #4
	bne	.L263
	ldmfd	sp!, {r4, r5, r6, pc}
.L271:
	.align	2
.L270:
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
	ldr	ip, .L283
	stmfd	sp!, {r3, r4, r5, lr}
	.save {r3, r4, r5, lr}
	add	r0, ip, r0, asl #3
	ldr	r3, [ip, #44]
	ldrb	r5, [r0, #16]	@ zero_extendqisi2
	ldr	r4, [r0, #12]
	ldrb	r3, [r3, #8]	@ zero_extendqisi2
	cmp	r3, #2
	mov	r3, r5, asl #8
	addne	r3, r4, r3
	add	r5, r5, #8
	movne	r2, #112
	strne	r2, [r3, #2056]
	bne	.L277
	cmp	r2, #0
	add	r3, r4, r3
	ldrneb	r2, [ip, #62]	@ zero_extendqisi2
	ldreqb	r2, [ip, #61]	@ zero_extendqisi2
	str	r2, [r3, #2056]
	ldrb	r0, [ip, #63]	@ zero_extendqisi2
	cmp	r0, #0
	addne	ip, r4, r5, asl #8
	movne	r2, #0
	beq	.L277
.L276:
	cmp	r2, r0
	bcs	.L277
	mov	r3, r2, asl #3
	add	r2, r2, #1
	mov	r3, r1, lsr r3
	uxtb	r3, r3
	str	r3, [ip, #4]
	b	.L276
.L277:
	mov	r0, #80
	bl	NandcDelayns
	ldr	r0, [r4, r5, asl #8]
	uxtb	r0, r0
	ldmfd	sp!, {r3, r4, r5, pc}
.L284:
	.align	2
.L283:
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
.L286:
	mov	r0, r4
	mov	r1, r5
	mov	r2, r6
	bl	FlashReadStatusEN
	cmp	r0, #255
	beq	.L286
	tst	r0, #64
	ldmnefd	sp!, {r4, r5, r6, pc}
	mov	r0, #1
	mov	r1, #3
	bl	usleep_range
	b	.L286
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
	ldr	r5, .L300
	add	r4, r5, r0, asl #4
	ldr	r3, [r4, #3204]
	ldrb	r7, [r4, #3196]	@ zero_extendqisi2
	cmp	r3, #0
	beq	.L294
	mov	r6, r0
	mov	r0, r7
	add	r5, r5, r6, asl #2
	bl	NandcFlashCs
	ldr	r1, [r4, #3200]
	mov	r0, r7
	ldr	r2, [r5, #3164]
	adds	r2, r2, #0
	movne	r2, #1
	bl	FlashWaitReadyEN
	mov	r5, r0
	mov	r0, r7
	bl	NandcFlashDeCs
	ldr	r2, [r4, #3204]
	sbfx	r3, r5, #0, #1
	str	r3, [r2]
	mov	r2, #0
	ldr	r1, [r4, #3208]
	str	r2, [r4, #3204]
	cmp	r1, r2
	strne	r3, [r1]
	strne	r2, [r4, #3208]
.L294:
	mov	r0, #0
	ldmfd	sp!, {r3, r4, r5, r6, r7, pc}
.L301:
	.align	2
.L300:
	.word	.LANCHOR0
	.fnend
	.size	FlashWaitCmdDone, .-FlashWaitCmdDone
	.align	2
	.type	flash_read_ecc, %function
flash_read_ecc:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	ldr	r2, .L304
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
.L305:
	.align	2
.L304:
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
	ldr	r3, .L312
	stmfd	sp!, {r0, r1, r2, r4, r5, lr}
	.save {r4, r5, lr}
	.pad #12
	add	r0, r3, r0, asl #3
	ldr	r4, .L312+4
	ldr	r5, [r0, #12]
.L308:
	ldr	r3, [r5]
	str	r3, [sp, #4]
	ldr	r3, [sp, #4]
	tst	r3, #512
	bne	.L309
	mov	r0, #10
	bl	NandcDelayns
	subs	r4, r4, #1
	bne	.L308
	mvn	r0, #0
	b	.L307
.L309:
	mov	r0, #0
.L307:
	add	sp, sp, #12
	@ sp needed
	ldmfd	sp!, {r4, r5, pc}
.L313:
	.align	2
.L312:
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
	ldr	r3, .L320
	stmfd	sp!, {r0, r1, r2, r4, r5, lr}
	.save {r4, r5, lr}
	.pad #12
	add	r0, r3, r0, asl #3
	ldr	r4, .L320+4
	ldr	r5, [r0, #12]
	mov	r0, #130
	bl	NandcDelayns
.L316:
	ldr	r3, [r5]
	str	r3, [sp, #4]
	ldr	r3, [sp, #4]
	tst	r3, #512
	bne	.L317
	mov	r0, #1
	mov	r1, #2
	bl	usleep_range
	subs	r4, r4, #1
	bne	.L316
	mvn	r0, #0
	b	.L315
.L317:
	mov	r0, #0
.L315:
	add	sp, sp, #12
	@ sp needed
	ldmfd	sp!, {r4, r5, pc}
.L321:
	.align	2
.L320:
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
	ldr	r3, .L324
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
.L325:
	.align	2
.L324:
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
	stmfd	sp!, {r4, r5, r6, r7, r8, lr}
	.save {r4, r5, r6, r7, r8, lr}
	ldr	r6, .L333
	ldrb	r3, [r6, #-2744]	@ zero_extendqisi2
	cmp	r3, #0
	ldmeqfd	sp!, {r4, r5, r6, r7, r8, pc}
	mov	r5, r0
	bl	NandcFlashCs
	ldr	r3, .L333+4
	add	r3, r3, r5, asl #3
	ldrb	r8, [r3, #16]	@ zero_extendqisi2
	ldr	r7, [r3, #12]
	ldr	r3, .L333+8
	mov	r8, r8, asl #8
	ldrb	r3, [r3, r5, asl #3]	@ zero_extendqisi2
	cmp	r3, #44
	bne	.L328
	add	r4, r7, r8
	mov	r3, #239
	mov	r0, #50
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
.L328:
	add	r7, r7, r8
	mov	r0, r5
	bl	NandcWaitFlashReadyNoDelay
	mov	r3, #218
	mov	r0, r5
	str	r3, [r7, #2056]
	bl	NandcWaitFlashReady
	mov	r3, #2
	strb	r3, [r6, #-1876]
	ldmfd	sp!, {r4, r5, r6, r7, r8, pc}
.L334:
	.align	2
.L333:
	.word	.LANCHOR2
	.word	.LANCHOR0
	.word	IDByte
	.fnend
	.size	flash_enter_slc_mode, .-flash_enter_slc_mode
	.align	2
	.global	flash_exit_slc_mode
	.type	flash_exit_slc_mode, %function
flash_exit_slc_mode:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	stmfd	sp!, {r4, r5, r6, r7, r8, lr}
	.save {r4, r5, r6, r7, r8, lr}
	ldr	r6, .L342
	ldrb	r3, [r6, #-2744]	@ zero_extendqisi2
	cmp	r3, #0
	ldmeqfd	sp!, {r4, r5, r6, r7, r8, pc}
	mov	r5, r0
	bl	NandcFlashCs
	ldr	r3, .L342+4
	add	r3, r3, r5, asl #3
	ldrb	r8, [r3, #16]	@ zero_extendqisi2
	ldr	r7, [r3, #12]
	ldr	r3, .L342+8
	mov	r8, r8, asl #8
	ldrb	r3, [r3, r5, asl #3]	@ zero_extendqisi2
	cmp	r3, #44
	bne	.L337
	add	r4, r7, r8
	mov	r3, #239
	mov	r0, #50
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
.L337:
	add	r7, r7, r8
	mov	r0, r5
	bl	NandcWaitFlashReadyNoDelay
	mov	r3, #223
	mov	r0, r5
	str	r3, [r7, #2056]
	bl	NandcWaitFlashReady
	mov	r3, #0
	strb	r3, [r6, #-1876]
	ldmfd	sp!, {r4, r5, r6, r7, r8, pc}
.L343:
	.align	2
.L342:
	.word	.LANCHOR2
	.word	.LANCHOR0
	.word	IDByte
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
	ldr	r3, .L369
	stmfd	sp!, {r0, r1, r2, r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.save {r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.pad #12
	mov	lr, #0
	ldrb	r2, [r3, #-1875]	@ zero_extendqisi2
	mov	r4, #239
	ldr	r7, .L369+4
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
.L356:
	ldr	r3, .L369+8
	ldrb	ip, [lr, r7]	@ zero_extendqisi2
	ldr	r1, [r3, lr]!
	cmp	ip, #69
	cmpne	ip, #152
	ldrb	r3, [r3, #4]	@ zero_extendqisi2
	beq	.L347
	cmp	ip, #44
	cmpne	ip, #173
	bne	.L348
.L347:
	cmp	r0, #1
	bne	.L349
	ldr	fp, [sp, #4]
	cmp	fp, #0
	beq	.L348
	mov	r3, r3, asl #8
	cmp	ip, #173
	add	fp, r1, r3
	str	r4, [fp, #2056]
	streq	r0, [fp, #2052]
	beq	.L368
	cmp	ip, #44
	streq	r0, [fp, #2052]
	strne	r5, [fp, #2052]
	streq	r10, [fp, #2048]
	strne	r0, [fp, #2048]
	b	.L354
.L349:
	ldr	fp, [sp]
	cmp	fp, #0
	beq	.L348
	mov	r3, r3, asl #8
	cmp	ip, #173
	add	fp, r1, r3
	str	r4, [fp, #2056]
	streq	r6, [fp, #2052]
	streq	r9, [fp, #2048]
	beq	.L354
	cmp	ip, #44
	streq	r6, [fp, #2052]
	streq	r8, [fp, #2048]
	beq	.L354
	str	r5, [fp, #2052]
.L368:
	str	r2, [fp, #2048]
.L354:
	add	r3, r1, r3
	str	r2, [r3, #2048]
	str	r2, [r3, #2048]
	str	r2, [r3, #2048]
.L348:
	add	lr, lr, #8
	cmp	lr, #32
	bne	.L356
	mov	r0, #0
	bl	NandcWaitFlashReady
	mov	r0, #0
	add	sp, sp, #12
	@ sp needed
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L370:
	.align	2
.L369:
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
	ldr	ip, .L373
	stmfd	sp!, {r3, r4, r5, lr}
	.save {r3, r4, r5, lr}
	add	ip, ip, r0, asl #3
	ldr	r3, .L373+4
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
.L374:
	.align	2
.L373:
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
	ldr	r2, .L377
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
.L378:
	.align	2
.L377:
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
	ldr	r1, .L387
	ldr	r0, .L387+4
	add	r4, r4, r4, asl #2
	ldr	ip, .L387+8
	sub	lr, r1, #48
	mov	r2, #0
.L380:
	ldrb	r3, [r0]	@ zero_extendqisi2
	cmp	r2, r3
	bcs	.L386
	ldrb	r3, [ip]	@ zero_extendqisi2
	cmp	r3, #67
	add	r3, r2, r4
	addeq	r3, lr, r3
	addne	r3, r1, r3
	add	r2, r2, #1
	ldrsb	r3, [r3, #5]
	str	r3, [r5]
	b	.L380
.L386:
	mov	r0, #0
	ldmfd	sp!, {r3, r4, r5, lr}
	b	NandcWaitFlashReady
.L388:
	.align	2
.L387:
	.word	.LANCHOR1+3216
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
	ldr	r3, .L391
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
.L392:
	.align	2
.L391:
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
	ldr	r8, .L405
	.pad #20
	sub	sp, sp, #20
	mov	r6, r0
	mov	r9, r1
	mov	r7, r5
.L394:
	cmp	r7, r9
	beq	.L404
	rsb	r3, r7, r9
	add	r2, sp, #8
	add	r0, r6, r5
	mov	r1, #0
	uxtb	r3, r3
	str	r3, [sp]
	add	r3, sp, #12
	bl	LogAddr2PhyAddr
	ldrb	r2, [r8, #3152]	@ zero_extendqisi2
	ldr	r3, [sp, #12]
	cmp	r3, r2
	mvncs	r3, #0
	strcs	r3, [r6, r5]
	bcs	.L396
	add	r2, r8, r3
	add	r3, r8, r3, asl #4
	ldrb	r4, [r2, #3156]	@ zero_extendqisi2
	strb	r4, [r3, #3196]
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
	bne	.L398
	ldr	r0, .L405+4
	ldr	r1, [sp, #8]
	bl	printk
.L398:
	mov	r0, r4
	bl	NandcFlashDeCs
.L396:
	add	r7, r7, #1
	add	r5, r5, #36
	b	.L394
.L404:
	mov	r0, #0
	add	sp, sp, #20
	@ sp needed
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, pc}
.L406:
	.align	2
.L405:
	.word	.LANCHOR0
	.word	.LC1
	.fnend
	.size	FlashEraseSLc2KBlocks, .-FlashEraseSLc2KBlocks
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
	ldr	r4, .L442
	.pad #28
	sub	sp, sp, #28
	ldrb	r5, [r4]	@ zero_extendqisi2
	cmp	r5, #0
	beq	.L425
	mov	r1, r2
	bl	FlashEraseSLc2KBlocks
	b	.L409
.L425:
	ldr	fp, .L442+4
	mov	r9, r0
	mov	r10, r1
.L408:
	cmp	r5, r8
	bcs	.L440
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
	ldrb	r3, [r4, #3152]	@ zero_extendqisi2
	mov	r7, r0
	ldr	r0, [sp, #20]
	cmp	r0, r3
	mvncs	r3, #0
	strcs	r3, [r9, r6]
	bcs	.L411
	ldrb	r3, [fp, #-1874]	@ zero_extendqisi2
	cmp	r3, #0
	add	r3, r4, r0, asl #4
	ldr	r3, [r3, #3204]
	moveq	r7, #0
	cmp	r3, #0
	beq	.L413
	uxtb	r0, r0
	bl	FlashWaitCmdDone
.L413:
	ldr	r2, [sp, #20]
	cmp	r7, #0
	ldr	r0, [sp, #12]
	addne	ip, r6, #36
	addne	ip, r9, ip
	mov	r3, r2, asl #4
	add	r2, r4, r2
	add	r1, r4, r3
	add	r3, r4, r3
	ldrb	r6, [r2, #3156]	@ zero_extendqisi2
	str	r0, [r1, #3204]
	mov	r0, #0
	str	r0, [r1, #3208]
	ldr	r0, [sp, #16]
	strne	ip, [r1, #3208]
	strb	r6, [r3, #3196]
	str	r0, [r1, #3200]
	mov	r0, r6
	bl	NandcFlashCs
	cmp	r10, #1
	mov	r0, r6
	bne	.L415
	ldr	r3, .L442+4
	ldrb	r3, [r3, #-2744]	@ zero_extendqisi2
	cmp	r3, #0
	beq	.L415
	bl	flash_enter_slc_mode
	b	.L416
.L415:
	bl	flash_exit_slc_mode
.L416:
	ldr	r3, [sp, #20]
	mov	r0, r6
	ldr	r1, [sp, #16]
	add	r5, r5, r7
	add	r3, r4, r3, asl #2
	ldr	r2, [r3, #3164]
	adds	r2, r2, #0
	movne	r2, #1
	bl	FlashWaitReadyEN
	mov	r0, r6
	mov	r2, r7
	ldr	r1, [sp, #16]
	bl	FlashEraseCmd
	mov	r0, r6
	bl	NandcFlashDeCs
.L411:
	add	r5, r5, #1
	b	.L408
.L440:
	ldr	r6, .L442+4
	mov	r5, #0
	ldr	r7, .L442+8
.L418:
	ldrb	r3, [r4, #3152]	@ zero_extendqisi2
	cmp	r5, r3
	bcs	.L441
	uxtb	r0, r5
	bl	FlashWaitCmdDone
	cmp	r10, #1
	bne	.L419
	ldrb	r3, [r6, #-2744]	@ zero_extendqisi2
	cmp	r3, #0
	beq	.L419
	ldrb	r0, [r7, r5, asl #4]	@ zero_extendqisi2
	bl	flash_exit_slc_mode
.L419:
	add	r5, r5, #1
	b	.L418
.L441:
	ldr	r3, .L442+4
	ldr	r3, [r3, #-1872]
	cmp	r3, #0
	bne	.L421
.L423:
	mov	r0, #0
	b	.L409
.L421:
	ldr	r3, .L442+12
	ldrb	r3, [r3]	@ zero_extendqisi2
	cmp	r3, #69
	bne	.L423
	mov	r3, #0
	mov	r2, #36
	mov	r1, r3
.L422:
	cmp	r3, r8
	beq	.L423
	mul	r0, r2, r3
	add	r3, r3, #1
	str	r1, [r9, r0]
	b	.L422
.L409:
	add	sp, sp, #28
	@ sp needed
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L443:
	.align	2
.L442:
	.word	.LANCHOR0
	.word	.LANCHOR2
	.word	.LANCHOR0+3196
	.word	IDByte
	.fnend
	.size	FlashEraseBlocks, .-FlashEraseBlocks
	.align	2
	.global	HynixGetReadRetryDefault
	.type	HynixGetReadRetryDefault, %function
HynixGetReadRetryDefault:
	.fnstart
	@ args = 0, pretend = 0, frame = 40
	@ frame_needed = 0, uses_anonymous_args = 0
	ldr	r3, .L553
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
	bne	.L445
	mvn	r2, #88
	strb	r2, [r3, #-2728]
	ldr	r3, .L553+4
	mvn	r2, #8
	mov	r5, #7
	strb	r2, [r3, #3401]
	b	.L506
.L445:
	cmp	r4, #3
	bne	.L447
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
	b	.L547
.L447:
	cmp	r4, #4
	bne	.L448
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
.L547:
	mov	r5, #8
	strb	r2, [r3, #-2721]
	mov	r6, r5
	b	.L446
.L448:
	cmp	r4, #5
	bne	.L449
	mov	r2, #56
	mov	r5, #8
	strb	r2, [r3, #-2728]
	mov	r2, #57
	strb	r2, [r3, #-2727]
	mov	r2, #58
	strb	r2, [r3, #-2726]
	mov	r2, #59
	strb	r2, [r3, #-2725]
	b	.L506
.L449:
	cmp	r4, #6
	bne	.L450
	mov	r2, #14
	mov	r5, #12
	strb	r2, [r3, #-2728]
	mov	r2, #15
	strb	r2, [r3, #-2727]
	mov	r2, #16
	strb	r2, [r3, #-2726]
	mov	r2, #17
	strb	r2, [r3, #-2725]
	b	.L506
.L450:
	cmp	r4, #7
	bne	.L451
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
	b	.L446
.L451:
	cmp	r4, #8
	mov	r5, #7
	bne	.L506
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
	b	.L446
.L506:
	mov	r6, #4
.L446:
	sub	r3, r4, #1
	cmp	r3, #1
	bhi	.L543
	ldr	r10, .L553+8
	mov	r9, #0
.L452:
	ldr	r3, .L553+12
	uxtb	r2, r9
	ldrb	r1, [r3, #3152]	@ zero_extendqisi2
	cmp	r1, r2
	bls	.L459
	add	r2, r3, r2
	ldr	r8, .L553+16
	ldrb	r2, [r2, #3156]	@ zero_extendqisi2
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
.L454:
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
	bcc	.L454
	mov	r2, #0
.L455:
	add	r0, r10, r2
	mov	r3, #1
.L456:
	ldrb	r1, [r0, r3, asl #2]	@ zero_extendqisi2
	ldrb	ip, [fp]	@ zero_extendqisi2
	add	r1, r1, ip
	strb	r1, [fp, r3, asl #3]
	add	r3, r3, #1
	cmp	r3, #7
	bne	.L456
	add	r2, r2, #1
	add	fp, fp, #1
	cmp	r2, #4
	bne	.L455
	mov	r3, #0
	add	r9, r9, #1
	strb	r3, [r4, #16]
	strb	r3, [r4, #24]
	strb	r3, [r4, #32]
	strb	r3, [r4, #40]
	strb	r3, [r4, #48]
	strb	r3, [r4, #41]
	strb	r3, [r4, #49]
	b	.L452
.L543:
	sub	r3, r4, #3
	cmp	r3, #5
	bhi	.L459
	smulbb	r8, r6, r5
	ldr	fp, .L553
	mov	r3, r8, asl #4
	mov	r8, r8, asr #1
	str	r3, [sp, #36]
	mov	r3, r8, asl #1
	str	r3, [sp, #4]
	mov	r3, #0
	str	r3, [sp, #16]
.L460:
	ldrb	r3, [sp, #16]	@ zero_extendqisi2
	str	r3, [sp, #12]
	ldr	r3, .L553+12
	ldr	r2, [sp, #12]
	ldrb	r3, [r3, #3152]	@ zero_extendqisi2
	cmp	r3, r2
	bhi	.L504
.L459:
	ldr	r3, .L553
	strb	r6, [r3, #-2731]
	strb	r5, [r3, #-2730]
	add	sp, sp, #44
	@ sp needed
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L504:
	ldr	r2, [sp, #12]
	ldr	r3, .L553+12
	add	r3, r3, r2
	ldrb	r10, [r3, #3156]	@ zero_extendqisi2
	ldr	r3, .L553+12
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
	ldreq	r7, .L553+16
	moveq	r3, #160
	mlaeq	r7, r3, r10, r7
	addeq	r3, r7, #28
	beq	.L548
.L461:
	cmp	r4, #8
	beq	.L463
	ldr	r7, .L553+16
	add	r7, r7, r10, asl #6
	add	r3, r7, #20
.L548:
	str	r3, [sp, #20]
	cmp	r4, #4
	add	r3, r8, r9
	mov	r2, #54
	str	r2, [r3, #2056]
	bne	.L464
	mov	r2, #255
	str	r2, [r3, #2052]
	mov	r2, #64
	str	r2, [r3, #2048]
	mov	r2, #204
	b	.L549
.L464:
	sub	r2, r4, #5
	cmp	r2, #1
	ldrlsb	r2, [fp, #-2728]	@ zero_extendqisi2
	strls	r2, [r3, #2052]
	movls	r2, #82
	bls	.L550
	cmp	r4, #7
	bne	.L465
	mov	r2, #174
	str	r2, [r3, #2052]
	mov	r2, #0
	str	r2, [r3, #2048]
	mov	r2, #176
.L549:
	str	r2, [r3, #2052]
	mov	r2, #77
.L550:
	str	r2, [r3, #2048]
.L465:
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
.L505:
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
	bne	.L469
	cmp	r4, #7
	movne	ip, #2
	moveq	ip, #32
.L469:
	ldr	r2, [sp, #8]
	ldr	r3, [fp, #-1868]
	add	r1, r8, r2, asl #8
	mov	r0, r3
.L470:
	ldr	r2, [r1, #2048]
	strb	r2, [r0], #1
	rsb	r2, r3, r0
	uxtb	r2, r2
	cmp	r2, ip
	bcc	.L470
	ldr	r2, [sp]
	cmp	r2, #0
	beq	.L471
	mov	r2, #0
.L473:
	ldrb	ip, [r3, r2, asl #2]	@ zero_extendqisi2
	uxtb	r0, r2
	cmp	ip, #50
	beq	.L472
	add	ip, r3, r2, asl #2
	ldrb	ip, [ip, #1]	@ zero_extendqisi2
	cmp	ip, #5
	beq	.L472
	add	r2, r2, #1
	cmp	r2, #8
	bne	.L473
	b	.L474
.L472:
	cmp	r0, #6
	bls	.L475
.L474:
	ldr	r0, .L553+20
	mov	r1, #0
	bl	printk
.L476:
	b	.L476
.L471:
	cmp	r4, #7
	bne	.L477
	ldr	r2, [sp]
.L479:
	ldrb	ip, [r3, r2, asl #2]	@ zero_extendqisi2
	uxtb	r0, r2
	cmp	ip, #12
	beq	.L478
	add	ip, r3, r2, asl #2
	ldrb	ip, [ip, #1]	@ zero_extendqisi2
	cmp	ip, #10
	beq	.L478
	add	r2, r2, #1
	cmp	r2, #8
	bne	.L479
	b	.L480
.L478:
	cmp	r0, #7
	bne	.L475
.L480:
	ldr	r0, .L553+20
	mov	r1, #0
	bl	printk
.L481:
	b	.L481
.L477:
	cmp	r4, #6
	bne	.L475
	sub	r2, r3, #1
	add	r3, r3, #7
.L482:
	ldrb	r0, [r2, #1]!	@ zero_extendqisi2
	cmp	r0, #12
	beq	.L475
	ldrb	r0, [r2, #8]	@ zero_extendqisi2
	cmp	r0, #4
	beq	.L475
	cmp	r2, r3
	bne	.L482
	ldr	r0, .L553+20
	mov	r1, #0
	bl	printk
.L484:
	b	.L484
.L475:
	ldr	r2, [fp, #-1868]
	ldr	r0, [sp, #36]
	add	r0, r2, r0
	mov	r3, r2
.L485:
	cmp	r3, r0
	ldrne	ip, [r1, #2048]
	strneb	ip, [r3], #1
	bne	.L485
.L552:
	ldr	r3, .L553
	mov	r0, #8
	ldr	r1, [sp, #4]
	ldr	r3, [r3, #-1868]
	add	r1, r3, r1
	str	r1, [sp, #28]
.L488:
	ldr	ip, [sp, #4]
	add	lr, r1, ip
.L487:
	ldrh	ip, [r1]
	mvn	ip, ip
	strh	ip, [r1], #2	@ movhi
	cmp	r1, lr
	bne	.L487
	ldr	ip, [sp, #4]
	subs	r0, r0, #1
	add	r1, r1, ip
	bne	.L488
.L489:
	mov	ip, #0
	mov	r0, ip
.L492:
	mov	r1, #1
	mov	lr, #0
	mov	r1, r1, asl r0
	mov	r7, #16
	str	r7, [sp, #32]
	mov	r7, lr
.L490:
	ldrh	r9, [r3, lr]
	and	r9, r9, r1
	cmp	r9, r1
	addeq	r7, r7, #1
	ldr	r9, [sp, #4]
	add	lr, lr, r9
	ldr	r9, [sp, #32]
	subs	r9, r9, #1
	str	r9, [sp, #32]
	bne	.L490
	cmp	r7, #8
	add	r0, r0, #1
	orrhi	ip, ip, r1
	uxthhi	ip, ip
	cmp	r0, #16
	bne	.L492
	ldr	r1, [sp, #28]
	strh	ip, [r3], #2	@ movhi
	cmp	r3, r1
	bne	.L489
	ldr	r1, [fp, #-1868]
	mov	r3, #0
	sub	r0, r1, #4
	add	ip, r1, #28
.L494:
	ldr	lr, [r0, #4]!
	cmp	lr, #0
	addeq	r3, r3, #1
	cmp	r0, ip
	bne	.L494
	cmp	r3, #7
	ble	.L495
	ldr	r0, .L553+24
	mov	r2, #1
	mov	r3, #1024
	bl	rknand_print_hex
	ldr	r0, .L553+20
	mov	r1, #0
	bl	printk
.L496:
	b	.L496
.L495:
	cmp	r4, #6
	moveq	r0, #4
	beq	.L497
	cmp	r4, #7
	moveq	r0, #10
	beq	.L497
	ldr	r3, [sp]
	cmp	r3, #0
	moveq	r0, #8
	movne	r0, #5
.L497:
	sub	r9, r6, #1
	ldr	r1, [sp, #20]
	mov	ip, #0
	uxtb	r9, r9
	add	r9, r9, #1
.L498:
	mov	r7, r1
	mov	r3, r2
.L499:
	ldrb	lr, [r3], #1	@ zero_extendqisi2
	strb	lr, [r7], #1
	rsb	lr, r2, r3
	uxtb	lr, lr
	cmp	lr, r6
	bcc	.L499
	add	ip, ip, #1
	add	r2, r2, r9
	cmp	ip, r5
	add	r1, r1, r0
	blt	.L498
	ldr	r3, [sp, #8]
	mov	r0, r10
	add	r7, r8, r3, asl #8
	mov	r3, #255
	str	r3, [r7, #2056]
	bl	NandcWaitFlashReady
	ldr	r3, [sp, #24]
	cmp	r3, #0
	beq	.L501
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
	b	.L502
.L501:
	ldr	r3, [sp]
	cmp	r3, #0
	movne	r3, #190
	moveq	r3, #56
	str	r3, [r7, #2056]
.L502:
	mov	r0, r10
	bl	NandcWaitFlashReady
	ldr	r3, [sp, #16]
	add	r3, r3, #1
	str	r3, [sp, #16]
	b	.L460
.L463:
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
	ldr	r3, .L553+28
	str	r3, [sp, #20]
	b	.L505
.L554:
	.align	2
.L553:
	.word	.LANCHOR2
	.word	.LANCHOR1
	.word	.LANCHOR1+3384
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
	bhi	.L557
	b	HynixGetReadRetryDefault
.L557:
	cmp	r3, #49
	bne	.L558
	ldr	r2, .L569
	ldr	r0, .L569+4
	ldr	r1, .L569+8
	strb	r3, [r2, #-2732]
	mov	r3, #4
	strb	r3, [r2, #-2731]
	mov	r3, #15
	strb	r3, [r2, #-2730]
	mov	r2, #64
	b	.L567
.L558:
	sub	r2, r3, #65
	cmp	r3, #33
	cmpne	r2, #1
	ldrls	r2, .L569
	strlsb	r3, [r2, #-2732]
	movls	r3, #4
	bls	.L568
.L559:
	cmp	r3, #34
	cmpne	r3, #67
	bne	.L560
	ldr	r2, .L569
	strb	r3, [r2, #-2732]
	mov	r3, #5
.L568:
	strb	r3, [r2, #-2731]
	mov	r3, #7
	ldr	r0, .L569+4
	strb	r3, [r2, #-2730]
	mov	r2, #45
	ldr	r1, .L569+12
	b	.L567
.L560:
	cmp	r3, #35
	cmpne	r3, #68
	bxne	lr
	ldr	r2, .L569
	ldr	r0, .L569+4
	ldr	r1, .L569+16
	strb	r3, [r2, #-2732]
	mov	r3, #5
	strb	r3, [r2, #-2731]
	mov	r3, #17
	strb	r3, [r2, #-2730]
	mov	r2, #95
.L567:
	b	ftl_memcpy
.L570:
	.align	2
.L569:
	.word	.LANCHOR2
	.word	.LANCHOR2-2728
	.word	.LANCHOR1+3320
	.word	.LANCHOR1+3168
	.word	.LANCHOR1+3216
	.fnend
	.size	FlashGetReadRetryDefault, .-FlashGetReadRetryDefault
	.align	2
	.global	FlashReadDpCmd
	.type	FlashReadDpCmd, %function
FlashReadDpCmd:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	stmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, lr}
	.save {r4, r5, r6, r7, r8, r9, r10, lr}
	mov	r8, r0
	ldr	r0, .L577
	mov	r7, r1
	uxtb	r10, r2
	mov	r9, r2, lsr #8
	add	r3, r0, r8, asl #3
	mov	r5, r2, lsr #16
	ldrb	r1, [r0, #64]	@ zero_extendqisi2
	uxtb	lr, r7
	ldr	r4, [r3, #12]
	mov	ip, r7, lsr #8
	ldrb	r3, [r3, #16]	@ zero_extendqisi2
	cmp	r1, #1
	ldr	r2, [r0, #44]
	mov	r1, r7, lsr #16
	mov	r3, r3, asl #8
	ldrb	r2, [r2, #7]	@ zero_extendqisi2
	bne	.L572
	cmp	r2, #1
	addeq	r2, r4, r3
	add	r4, r4, r3
	moveq	r6, #38
	streq	r6, [r2, #2056]
	mov	r6, #0
	ldrb	r3, [r0, #57]	@ zero_extendqisi2
	ldrb	r2, [r0, #56]	@ zero_extendqisi2
	mov	r0, r8
	str	r2, [r4, #2056]
	str	r6, [r4, #2052]
	str	r6, [r4, #2052]
	str	lr, [r4, #2052]
	str	ip, [r4, #2052]
	str	r1, [r4, #2052]
	str	r3, [r4, #2056]
	bl	NandcWaitFlashReady
	str	r6, [r4, #2056]
	mov	r3, #48
	str	r6, [r4, #2052]
	str	r6, [r4, #2052]
	str	r10, [r4, #2052]
	str	r9, [r4, #2052]
	str	r5, [r4, #2052]
	str	r3, [r4, #2056]
	b	.L574
.L572:
	cmp	r2, #1
	addeq	r2, r4, r3
	add	r3, r4, r3
	moveq	r6, #38
	streq	r6, [r2, #2056]
	ldrb	r2, [r0, #56]	@ zero_extendqisi2
	str	r2, [r3, #2056]
	ldrb	r2, [r0, #57]	@ zero_extendqisi2
	str	lr, [r3, #2052]
	str	ip, [r3, #2052]
	str	r1, [r3, #2052]
	str	r2, [r3, #2056]
	mov	r2, #48
	str	r10, [r3, #2052]
	str	r9, [r3, #2052]
	str	r5, [r3, #2052]
	str	r2, [r3, #2056]
.L574:
	mov	r0, r8
	mov	r1, r7
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, lr}
	b	FlashSetRandomizer
.L578:
	.align	2
.L577:
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
	ldr	r3, .L590
	ldr	r0, [r3, #-1864]
	mov	r4, r3
	cmp	r0, #0
	beq	.L580
	mov	r0, #0
	bl	flash_enter_slc_mode
	b	.L581
.L580:
	bl	flash_exit_slc_mode
.L581:
	ldrb	r3, [r4, #-1860]	@ zero_extendqisi2
	ldr	r5, .L590
	cmp	r3, #0
	beq	.L582
	ldrb	r3, [r5, #-1875]	@ zero_extendqisi2
	tst	r3, #1
	beq	.L582
	mov	r0, #1
	bl	FlashSetInterfaceMode
	mov	r0, #1
	bl	NandcSetMode
	mov	r3, #0
	strb	r3, [r5, #-1860]
.L582:
	ldr	r3, .L590+4
	mov	r0, #0
	ldr	r3, [r3, #12]
	str	r0, [r3, #336]
	ldmfd	sp!, {r3, r4, r5, pc}
.L591:
	.align	2
.L590:
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
	ldr	r3, .L593
	add	r0, r3, r0, asl #3
	ldr	r3, [r0, #12]
	str	r1, [r3, #336]
	bx	lr
.L594:
	.align	2
.L593:
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
	ldr	r1, .L606
	bl	__aeabi_idiv
	ldr	r3, .L606+4
	ldr	r3, [r3, #-2808]
	cmp	r0, #250
	movwgt	r2, #8354
	bgt	.L604
	cmp	r0, #220
	bgt	.L605
	cmp	r0, #185
	movwgt	r2, #4226
	bgt	.L604
	cmp	r0, #160
	movwgt	r2, #4194
	bgt	.L604
	cmp	r4, #35
	movwls	r2, #4193
	bls	.L604
	cmp	r4, #99
	movwls	r2, #4225
	bls	.L604
.L605:
	movw	r2, #8322
.L604:
	str	r2, [r3, #4]
	ldmfd	sp!, {r4, pc}
.L607:
	.align	2
.L606:
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
	bls	.L609
	movw	r3, #8322
	cmp	r0, r3
	bne	.L610
.L609:
	ldr	r3, .L611
	ldr	r3, [r3, #-2808]
	str	r0, [r3, #4]
.L610:
	ldr	r3, .L611+4
	ldrb	r0, [r3, #277]	@ zero_extendqisi2
	b	NandcTimeCfg
.L612:
	.align	2
.L611:
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
	ldr	r4, .L616
	mov	r1, #1
	ldr	r3, .L616+4
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
	str	r1, [r4, #-1856]
	movw	r1, #2049
	ubfx	r2, r2, #16, #4
	str	r2, [r4, #-1852]
	ldr	r2, [r0, #352]
	cmp	r2, r1
	str	r2, [r4, #-1848]
	moveq	r2, #8
	streq	r2, [r4, #-1852]
	str	r3, [r0]
	mov	r0, #40
	ldr	r3, [r4, #-2808]
	str	r5, [r3, #336]
	bl	NandcTimeCfg
	ldr	r3, [r4, #-2808]
	movw	r2, #8322
	mov	r0, #36864
	str	r2, [r3, #344]
	ldr	r2, .L616+8
	str	r2, [r3, #304]
	bl	ftl_malloc
	str	r5, [r4, #-1816]
	str	r5, [r4, #-1808]
	str	r0, [r4, #-1844]
	str	r0, [r4, #-1840]
	add	r0, r0, #32768
	str	r0, [r4, #-1836]
	ldmfd	sp!, {r3, r4, r5, pc}
.L617:
	.align	2
.L616:
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
	ldr	ip, .L620
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
.L621:
	.align	2
.L620:
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
	ldr	r3, .L630
	mov	r1, #1
	ldr	r2, [r3, #-2808]
	str	r0, [r3, #-1804]
	mov	r3, #0
	str	r1, [r2, #8]
	mov	r1, #16
	cmp	r0, r1
	bfi	r3, r1, #8, #8
	bfc	r3, #18, #1
	bne	.L623
.L626:
	bfc	r3, #4, #1
	b	.L624
.L623:
	cmp	r0, #24
	orreq	r3, r3, #16
	beq	.L624
	cmp	r0, #40
	orr	r3, r3, #262144
	orr	r3, r3, #16
	beq	.L626
.L624:
	orr	r3, r3, #1
	str	r3, [r2, #12]
	bx	lr
.L631:
	.align	2
.L630:
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
	ldr	r3, .L633
	strb	r0, [r3, #-2743]
	b	NandcBchSel
.L634:
	.align	2
.L633:
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
	ldr	r3, .L644
	stmfd	sp!, {r4, r5, r6, lr}
	.save {r4, r5, r6, lr}
	mov	r5, #0
	ldr	r2, [r3, #-2808]
	mov	r4, r3
	ldr	r1, [r3, #-2804]
	ldr	r6, .L644+4
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
.L637:
	ldrb	r3, [r6, r5, asl #3]	@ zero_extendqisi2
	sub	r3, r3, #1
	uxtb	r3, r3
	cmp	r3, #253
	bhi	.L636
	uxtb	r0, r5
	bl	FlashReset
.L636:
	add	r5, r5, #1
	cmp	r5, #4
	bne	.L637
	ldrb	r3, [r4, #-1860]	@ zero_extendqisi2
	ldr	r5, .L644
	cmp	r3, #0
	beq	.L638
	mov	r0, #1
	bl	NandcSetMode
	ldrb	r0, [r5, #-1875]	@ zero_extendqisi2
	bl	FlashSetInterfaceMode
	ldrb	r0, [r5, #-1875]	@ zero_extendqisi2
	bl	NandcSetMode
	ldrb	r0, [r5, #-2787]	@ zero_extendqisi2
	bl	NandcSetDdrPara
.L638:
	ldr	r3, .L644+8
	ldmfd	sp!, {r4, r5, r6, lr}
	ldr	r3, [r3, #44]
	ldrb	r0, [r3, #20]	@ zero_extendqisi2
	b	FlashBchSel
.L645:
	.align	2
.L644:
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
	@ link register save eliminated.
	bx	lr
	.fnend
	.size	NandcIqrWaitFlashReady, .-NandcIqrWaitFlashReady
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
	ldr	r3, .L650
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
.L651:
	.align	2
.L650:
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
.L653:
	ldr	r3, [r0, #8]
	str	r3, [sp, #4]
	ldr	r3, [sp, #4]
	tst	r3, #1048576
	beq	.L653
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
	ldr	lr, .L675
	.pad #28
	sub	sp, sp, #28
	ldr	r5, .L675+4
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
	ldr	r3, [r5, #-1852]
	cmp	r3, #3
	bls	.L658
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
	beq	.L659
	cmp	r1, #0
	bne	.L660
.L668:
	add	r2, r2, #1
	cmp	r8, #0
	mov	r2, r2, asr #1
	movne	r0, r8
	bfi	r4, r2, #22, #6
	ldreq	r0, [r5, #-1840]
	b	.L662
.L660:
	ldr	r3, [r5, #-1804]
	mov	r9, r5
	cmp	r3, #25
	movcc	r3, #64
	movcs	r3, #128
	str	r3, [sp, #4]
	mov	r3, r2, lsr #1
	str	r3, [sp, #12]
	mov	r3, #0
	mov	r0, r3
.L664:
	ldr	lr, [sp, #12]
	cmp	r0, lr
	bcs	.L668
	ldr	lr, [sp, #8]
	mov	r10, r3, lsr #2
	add	r0, r0, #1
	cmp	lr, #0
	ldrneh	fp, [ip, #2]
	mvneq	fp, #0
	ldrneh	lr, [ip], #4
	ldreq	lr, [r9, #-1836]
	orrne	lr, lr, fp, asl #16
	ldrne	fp, [r9, #-1836]
	streq	fp, [lr, r10, asl #2]
	strne	lr, [fp, r10, asl #2]
	ldr	lr, [sp, #4]
	add	r3, r3, lr
	b	.L664
.L662:
	ldr	r3, [r5, #-1836]
	ubfx	r10, r4, #22, #5
	mov	r9, r1
	str	r0, [r5, #-1832]
	mov	r2, r9
	mov	r1, r10, asl #10
	str	r3, [r5, #-1828]
	bl	rknand_dma_map_single
	mov	r2, r9
	mov	r1, r10, asl #7
	clz	r9, r9
	mov	r9, r9, lsr #5
	str	r0, [r5, #-1824]
	ldr	r0, [r5, #-1828]
	bl	rknand_dma_map_single
	mov	r3, #1
	str	r3, [r5, #-1816]
	tst	r8, #3
	ldr	r3, [r5, #-1824]
	str	r0, [r5, #-1820]
	str	r3, [r6, #20]
	ldr	r3, [r5, #-1820]
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
.L659:
	ldr	r3, [sp, #20]
	str	r3, [r6, #16]
.L658:
	str	r7, [r6, #12]
	str	r4, [r6, #8]
	orr	r4, r4, #4
	str	r4, [r6, #8]
	add	sp, sp, #28
	@ sp needed
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L676:
	.align	2
.L675:
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
	ldr	r5, .L717
	ldr	r3, .L717+4
	add	r0, r3, r0, asl #3
	ldr	r3, [r5, #-1852]
	cmp	r3, #3
	ldr	r4, [r0, #12]
	bls	.L708
	ldr	r3, [r4, #16]
	tst	r3, #4
	beq	.L708
	ldr	r6, [r4, #16]
	ldr	r3, [r4, #8]
	ubfx	r6, r6, #1, #1
	cmp	r6, #0
	str	r3, [sp]
	movne	r6, #0
	beq	.L690
.L680:
	ldr	r2, [r4, #28]
	ldr	r3, [sp]
	ubfx	r2, r2, #16, #5
	ubfx	r3, r3, #22, #6
	cmp	r2, r3
	bge	.L688
	ldr	r3, [r5, #-1852]
	cmp	r3, #5
	bhi	.L681
.L684:
	add	r6, r6, #1
	bic	r3, r6, #-16777216
	cmp	r3, #0
	bne	.L683
	ldr	r2, [r4, #28]
	mov	r1, r6
	ldr	r3, [sp]
	ubfx	r2, r2, #16, #5
	ldr	r0, .L717+8
	ubfx	r3, r3, #22, #6
	bl	printk
	ldr	r0, .L717+12
	mov	r1, r4
	mov	r2, #4
	mov	r3, #512
	bl	rknand_print_hex
	b	.L683
.L681:
	ldr	r3, [r4]
	str	r3, [sp, #4]
	ldr	r3, [sp, #4]
	tst	r3, #8192
	beq	.L684
	ldr	r3, [sp, #4]
	tst	r3, #131072
	beq	.L684
.L688:
	ldr	r3, [r5, #-1816]
	ldr	r4, .L717
	cmp	r3, #0
	beq	.L689
	ldr	r1, [sp]
	mov	r2, #0
	ldr	r0, [r4, #-1824]
	ubfx	r1, r1, #22, #5
	mov	r1, r1, asl #10
	bl	rknand_dma_unmap_single
	ldr	r0, [r4, #-1820]
	mov	r2, #0
	ldr	r1, [sp]
	ubfx	r1, r1, #22, #5
	mov	r1, r1, asl #7
	bl	rknand_dma_unmap_single
	b	.L689
.L683:
	mov	r0, #1
	mov	r1, #5
	bl	usleep_range
	b	.L680
.L692:
	ldr	r3, [r4, #8]
	add	r6, r6, #1
	str	r3, [sp]
	bic	r3, r6, #-16777216
	cmp	r3, #0
	bne	.L691
	ldr	r2, [sp]
	mov	r1, r6
	ldr	r3, [r4, #28]
	ldr	r0, .L717+16
	ubfx	r3, r3, #16, #5
	bl	printk
	ldr	r0, .L717+12
	mov	r1, r4
	mov	r2, #4
	mov	r3, #512
	bl	rknand_print_hex
.L691:
	mov	r0, #1
	mov	r1, #5
	bl	usleep_range
.L690:
	ldr	r3, [sp]
	tst	r3, #1048576
	beq	.L692
	ldr	r3, [r5, #-1808]
	cmp	r3, #0
	beq	.L693
	mov	r0, r4
	bl	NandcSendDumpDataStart
.L693:
	ldr	r3, [r5, #-1816]
	ldr	r6, .L717
	cmp	r3, #0
	beq	.L694
	ldr	r1, [sp]
	mov	r2, #1
	ldr	r0, [r6, #-1824]
	ubfx	r1, r1, #22, #5
	mov	r1, r1, asl #10
	bl	rknand_dma_unmap_single
	ldr	r0, [r6, #-1820]
	mov	r2, #1
	ldr	r1, [sp]
	ubfx	r1, r1, #22, #5
	mov	r1, r1, asl #7
	bl	rknand_dma_unmap_single
.L694:
	ldr	r3, [r5, #-1808]
	cmp	r3, #0
	beq	.L689
	mov	r0, r4
	bl	NandcSendDumpDataDone
.L689:
	mov	r3, #0
	str	r3, [r5, #-1816]
	b	.L677
.L708:
	ldr	r3, [r4, #8]
	str	r3, [sp]
	ldr	r3, [sp]
	tst	r3, #1048576
	beq	.L708
.L677:
	add	sp, sp, #8
	@ sp needed
	ldmfd	sp!, {r4, r5, r6, pc}
.L718:
	.align	2
.L717:
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
	bne	.L720
	cmp	r3, #0
	beq	.L721
	mov	r0, r2
	mov	r1, r3
	mov	r2, #1024
	bl	ftl_memcpy
.L721:
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
.L720:
	cmp	r3, #0
	beq	.L724
	mov	r1, r2
	mov	r0, r3
	mov	r2, #1024
	bl	ftl_memcpy
.L724:
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
	ldr	r3, .L784
	.pad #92
	sub	sp, sp, #92
	tst	fp, #63
	mov	r10, r0
	add	r3, r3, r0, asl #3
	mov	r7, r1
	mov	r8, r2
	ldr	r5, [sp, #128]
	ldr	r9, [r3, #12]
	bne	.L737
	cmp	r5, #0
	bne	.L738
	add	r0, sp, #24
	mov	r1, #255
	mov	r2, #64
	bl	ftl_memset
	add	r5, sp, #24
.L738:
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
	bne	.L739
	ldr	r4, .L784+4
	mov	r1, r8, lsr #1
	mov	r2, r7
	ldr	r3, [r4, #-1804]
	cmp	r3, #25
	mov	r3, r7
	movcc	ip, #64
	movcs	ip, #128
.L741:
	cmp	r2, r1
	add	r5, r5, #4
	add	r0, r3, ip
	bcs	.L782
	ldr	lr, [r4, #-1836]
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
	b	.L741
.L782:
	ldr	r0, [r4, #-1804]
	mov	r2, #0
	ldr	r1, [r4, #-1852]
	mov	r8, r8, lsr #2
	mov	r6, r2
.L743:
	cmp	r2, r8
	bcs	.L739
	cmp	r0, #0
	beq	.L739
	add	r3, r2, #8
	ldr	r3, [r9, r3, asl #2]
	str	r3, [sp, #20]
	ldr	r3, [sp, #20]
	tst	r3, #4
	bne	.L767
	ldr	r4, [sp, #20]
	ubfx	r4, r4, #15, #1
	cmp	r4, #0
	bne	.L767
	cmp	r1, #5
	bls	.L745
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
	b	.L781
.L745:
	cmp	r1, #3
	bls	.L747
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
	bls	.L748
	ldr	r3, [sp, #20]
	ldr	r4, [sp, #20]
	ubfx	r3, r3, #3, #5
	ubfx	r4, r4, #28, #1
.L781:
	orr	r4, r3, r4, asl #5
	b	.L747
.L748:
	ldr	r5, [sp, #20]
	ldr	r4, [sp, #20]
	ubfx	r5, r5, #16, #5
	ubfx	r4, r4, #30, #1
	orr	r4, r5, r4, asl #5
.L747:
	cmp	r6, r4
	movcc	r6, r4
	b	.L744
.L767:
	mvn	r6, #0
.L744:
	add	r2, r2, #1
	b	.L743
.L739:
	mov	r3, #0
	str	r3, [r9, #16]
	b	.L750
.L737:
	cmp	r1, #1
	mov	r4, #0
	bne	.L779
	mov	r6, r4
.L751:
	cmp	r4, r8
	bcs	.L783
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
	b	.L751
.L783:
	mov	r6, #0
	b	.L750
.L779:
	str	r4, [sp]
	mov	r1, r4
	str	r4, [sp, #4]
	mov	r2, #2
	mov	r3, r4
	mov	r6, r4
	bl	NandcXferStart
	str	fp, [sp, #8]
.L756:
	cmp	r4, r8
	bcs	.L750
	mov	r0, r10
	mov	r1, r7
	bl	NandcXferComp
	ldr	r3, [r9, #32]
	add	ip, r4, #2
	cmp	ip, r8
	str	r3, [sp, #20]
	bcs	.L757
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
.L757:
	ldr	r3, [sp, #20]
	tst	r3, #4
	mvnne	r6, #0
	bne	.L758
	ldr	r2, [sp, #20]
	ldr	r3, [sp, #20]
	ubfx	r2, r2, #3, #5
	ubfx	r3, r3, #27, #1
	orr	r3, r2, r3, asl #5
	cmp	r6, r3
	movcc	r6, r3
.L758:
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
	b	.L756
.L750:
	ldr	r3, .L784+4
	clz	r7, r7
	mov	r7, r7, lsr #5
	ldr	r3, [r3, #-1852]
	cmp	r3, #5
	movls	r7, #0
	cmp	r7, #0
	beq	.L762
	ldr	r3, [r9]
	and	r2, r3, #139264
	cmp	r2, #139264
	orreq	r3, r3, #131072
	streq	r3, [r9]
	mvneq	r6, #0
.L762:
	mov	r0, r6
	add	sp, sp, #92
	@ sp needed
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L785:
	.align	2
.L784:
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
	ldr	r3, .L789
	subs	r4, r0, #0
	mov	r6, r1
	mov	r7, r2
	ldrb	r5, [r3, #265]	@ zero_extendqisi2
	bne	.L787
	ldr	r2, .L789+4
	ldrb	r3, [r2, #1]	@ zero_extendqisi2
	ldr	r2, [r2, #4]
	mul	r2, r2, r3
	cmp	r1, r2
	movcc	r5, #4
.L787:
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
.L790:
	.align	2
.L789:
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
	ldr	r4, .L818
	.pad #20
	sub	sp, sp, #20
	mov	r10, r0
	mov	fp, r2
	str	r1, [sp]
	ldr	r3, [r4, #-2808]
	ldr	r3, [r3, #304]
	str	r3, [sp, #8]
	ldr	r3, [r4, #-1852]
	cmp	r3, #8
	ldr	r3, [sp, #56]
	movcc	r9, #6
	movcs	r9, #12
	cmp	r3, #0
	moveq	r6, #1024
	beq	.L793
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
	ldrb	r0, [r4, #-1875]	@ zero_extendqisi2
	bl	FlashSetInterfaceMode
	ldrb	r0, [r4, #-1875]	@ zero_extendqisi2
	bl	NandcSetMode
	cmn	r6, #1
	bne	.L794
.L803:
	mvn	r6, #0
	b	.L795
.L794:
	ldr	r0, .L818+4
	mov	r2, r6
	ldr	r1, [sp]
	bl	printk
	cmp	r6, #9
	bhi	.L796
	ldr	r3, .L818+8
	add	r3, r3, r10, asl #3
	ldr	r3, [r3, #12]
	ldr	r2, [r3, #3840]
	ldr	r2, [r3]
	orr	r2, r2, #131072
	str	r2, [r3]
.L796:
	ldr	r3, [r4, #-1800]
	add	r3, r3, #1
	str	r3, [r4, #-1800]
	cmp	r3, #2048
	bcc	.L795
	ldr	r3, .L818
	mov	r7, #0
	mov	fp, r7
	str	r7, [r3, #-1800]
.L793:
	mov	r4, #0
	mvn	r8, #0
	mov	ip, r4
	mov	r5, r4
	str	r4, [sp, #4]
.L801:
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
	bhi	.L797
	cmp	r0, #2
	bhi	.L807
	add	r5, r5, #1
	cmp	r5, #9
	bls	.L807
	rsb	r4, r5, r9
	mov	r6, r0
	mov	r8, #0
	b	.L799
.L797:
	ldr	r3, [sp, #4]
	cmp	r3, r5
	bcs	.L808
	cmp	r5, #7
	rsb	ip, r5, r4
	bhi	.L809
	str	r5, [sp, #4]
	b	.L808
.L807:
	mov	r8, #0
	mov	r4, r9
	mov	r6, r0
	mov	r7, r8
	mov	fp, r8
	b	.L798
.L808:
	mov	r5, #0
.L798:
	add	r9, r9, #2
	cmp	r9, #69
	bls	.L801
.L799:
	ldr	r3, [sp, #4]
	cmp	r3, r5
	movcs	r4, ip
	b	.L800
.L809:
	mov	r4, ip
.L800:
	cmp	r4, #0
	beq	.L802
	ldr	r0, .L818+12
	mov	r1, r4
	bl	printk
	uxtb	r0, r4
	bl	NandcSetDdrPara
.L802:
	cmn	r8, #1
	bne	.L795
	ldr	r0, .L818+16
	mov	r1, r10
	ldr	r2, [sp]
	bl	printk
	ldr	r3, [sp, #56]
	cmp	r3, #0
	beq	.L803
	ldr	r3, [sp, #8]
	ubfx	r0, r3, #8, #8
	bl	NandcSetDdrPara
.L795:
	mov	r0, r6
	add	sp, sp, #20
	@ sp needed
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L819:
	.align	2
.L818:
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
	ldr	r10, .L841
	cmn	r0, #1
	mov	r4, r0
	bne	.L821
	ldr	r9, .L841+4
	ldrb	fp, [r9, #8]	@ zero_extendqisi2
	cmp	fp, #0
	bne	.L822
.L824:
	ldrb	r3, [r10, #-1860]	@ zero_extendqisi2
	ldr	r9, .L841
	cmp	r3, #0
	beq	.L821
	b	.L840
.L822:
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
	beq	.L824
	b	.L821
.L840:
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
	beq	.L825
	ldrb	r3, [r9, #-2743]	@ zero_extendqisi2
	cmp	r0, r3, lsr #1
	bls	.L821
.L825:
	ubfx	r0, fp, #8, #8
	bl	NandcSetDdrPara
.L821:
	ldr	ip, [r10, #-1796]
	ldr	r9, .L841
	adds	r3, ip, #0
	movne	r3, #1
	cmn	r4, #1
	movne	r3, #0
	cmp	r3, #0
	beq	.L826
	mov	r1, r6
	mov	r2, r8
	mov	r3, r7
	mov	r0, r5
	blx	ip
	mov	r2, r5
	mov	r3, r6
	mov	r4, r0
	ldr	r0, .L841+8
	mov	r1, r4
	bl	printk
	cmn	r4, #1
	bne	.L826
	ldrb	r3, [r9, #-2744]	@ zero_extendqisi2
	cmp	r3, #0
	beq	.L826
	mov	r0, r5
	bl	flash_enter_slc_mode
	ldr	ip, [r9, #-1796]
	mov	r0, r5
	mov	r1, r6
	mov	r2, r8
	mov	r3, r7
	blx	ip
	mov	r4, r0
	mov	r0, r5
	bl	flash_exit_slc_mode
.L826:
	mov	r0, r4
	add	sp, sp, #12
	@ sp needed
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L842:
	.align	2
.L841:
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
	ldr	r5, .L853
	mov	r6, r1
	mov	r4, #0
	ldrb	r0, [r5, #-1875]	@ zero_extendqisi2
	bl	FlashSetInterfaceMode
	ldrb	r0, [r5, #-1875]	@ zero_extendqisi2
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
	bne	.L844
	ldrb	r2, [r5, #-1875]	@ zero_extendqisi2
	tst	r2, #1
	beq	.L844
	mov	r0, #1
	bl	FlashSetInterfaceMode
	mov	r0, #1
	bl	NandcSetMode
	strb	r4, [r5, #-1860]
	b	.L845
.L844:
	mov	r2, #1
	strb	r2, [r3, #-1860]
.L845:
	mov	r0, #0
	add	sp, sp, #8
	@ sp needed
	ldmfd	sp!, {r4, r5, r6, r7, r8, pc}
.L854:
	.align	2
.L853:
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
	ldr	r3, .L871
	mov	r4, #0
	ldr	r8, .L871+4
	mov	r9, #4
	ldr	r5, .L871+8
	mvn	r7, #0
	ldr	r0, [r3]	@ unaligned
	ldr	r3, [r8, #4]
	str	r4, [r5, #-1788]
	mov	r6, r5
	str	r0, [sp, #12]	@ unaligned
	mov	r0, r4
	str	r3, [sp, #4]
	ldr	r3, [r5, #-1868]
	str	r3, [r5, #-1792]
	bl	flash_enter_slc_mode
.L856:
	add	fp, r4, #1
	mov	r10, #0
.L858:
	add	r3, sp, #12
	ldrb	r0, [r3, r10]	@ zero_extendqisi2
	bl	FlashBchSel
	mov	r0, #0
	mov	r1, r4
	ldr	r2, [r5, #-1868]
	mov	r3, r0
	bl	FlashReadRawPage
	cmn	r0, #1
	bne	.L857
	mov	r0, #0
	mov	r1, fp
	ldr	r2, [r6, #-1868]
	mov	r3, r0
	bl	FlashReadRawPage
	cmn	r0, #1
	bne	.L857
	add	r10, r10, #1
	cmp	r10, #4
	beq	.L859
	b	.L858
.L860:
	add	r0, fp, #12
	movw	r1, #2036
	bl	js_hash
	ldr	r3, [fp, #8]
	cmp	r3, r0
	mvnne	r7, #0
	bne	.L859
	ldr	r7, .L871+12
	add	r1, fp, #160
	mov	r2, #32
	mov	r0, r7
	bl	ftl_memcpy
	ldr	r1, [r6, #-1792]
	mov	r2, #32
	ldr	r0, .L871+16
	add	r1, r1, #192
	bl	ftl_memcpy
	ldr	r1, [r6, #-1792]
	mov	r2, #852
	ldr	r0, .L871+20
	add	r1, r1, #224
	bl	ftl_memcpy
	ldrh	r0, [r7, #10]
	bl	FlashBlockAlignInit
	ldr	r7, [r6, #-1792]
	str	r4, [r6, #-1788]
	mov	r0, r4
	ldr	r1, [r8, #4]
	ldr	r3, [r7, #1076]
	strb	r3, [r6, #-1860]
	bl	__aeabi_uidiv
	add	r0, r0, #1
	cmp	r0, #1
	strhi	r0, [r6, #-1784]
	movls	r3, #2
	strls	r3, [r6, #-1784]
	ldrh	r3, [r7, #14]
	mov	r7, #0
	strb	r3, [r5, #-1780]
.L859:
	ldr	r3, [sp, #4]
	subs	r9, r9, #1
	add	r4, r4, r3
	bne	.L856
	mov	r0, r9
.L870:
	bl	flash_exit_slc_mode
	mov	r0, r7
	add	sp, sp, #20
	@ sp needed
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L857:
	ldr	fp, [r5, #-1792]
	ldr	r2, .L871+24
	ldr	r3, [fp]
	cmp	r3, r2
	bne	.L859
	cmp	r7, #0
	bne	.L860
	mov	r0, r4
	ldr	r1, [r8, #4]
	bl	__aeabi_uidiv
	ldr	r3, .L871+8
	add	r0, r0, #1
	str	r0, [r3, #-1784]
	mov	r0, r7
	b	.L870
.L872:
	.align	2
.L871:
	.word	.LANCHOR3
	.word	.LANCHOR0
	.word	.LANCHOR2
	.word	.LANCHOR1+256
	.word	.LANCHOR0+48
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
	ldr	r3, .L902
	add	r3, r3, r8, asl #3
	ldr	r5, [r3, #12]
	ldrb	r3, [r3, #16]	@ zero_extendqisi2
	add	r6, r3, #8
	add	r6, r5, r6, asl #8
	str	r3, [sp, #4]
	ldr	r3, .L902+4
	ldrb	r7, [r3]	@ zero_extendqisi2
	str	r3, [sp, #12]
	sub	r4, r7, #67
	ldr	r3, [sp, #4]
	cmp	r4, #1
	mov	r3, r3, asl #8
	movls	r4, #0
	str	r3, [sp, #16]
	bls	.L874
	ldr	r3, .L902+8
	ldrb	r4, [r3, #-1860]	@ zero_extendqisi2
	cmp	r4, #0
	beq	.L875
	mov	r0, #0
	mov	r4, #1
	bl	NandcSetDdrMode
.L875:
	ldr	r3, [sp, #16]
	mov	r2, #92
	add	r3, r5, r3
	str	r2, [r3, #2056]
	mov	r2, #197
	str	r2, [r3, #2056]
.L874:
	mov	r7, #1
	mvn	r9, #0
.L876:
	ldr	r3, .L902+12
	ldrb	r3, [r3]	@ zero_extendqisi2
	add	r3, r3, #1
	cmp	r7, r3
	bcs	.L901
	ldr	r3, [sp, #12]
	mov	r0, r6
	uxtb	r1, r7
	ldrb	r3, [r3]	@ zero_extendqisi2
	sub	r3, r3, #67
	cmp	r3, #1
	bhi	.L877
	bl	SandiskSetRRPara
	b	.L878
.L877:
	bl	ToshibaSetRRPara
.L878:
	ldr	r3, [sp, #12]
	ldrb	r3, [r3]	@ zero_extendqisi2
	cmp	r3, #34
	bne	.L879
	ldr	r3, .L902+12
	ldrb	r3, [r3]	@ zero_extendqisi2
	sub	r3, r3, #3
	cmp	r7, r3
	ldreq	r3, [sp, #4]
	moveq	r2, #179
	addeq	r3, r5, r3, asl #8
	streq	r2, [r3, #2056]
.L879:
	ldr	r3, [sp, #16]
	cmp	r4, #0
	mov	r2, #38
	add	r3, r5, r3
	str	r2, [r3, #2056]
	mov	r2, #93
	str	r2, [r3, #2056]
	beq	.L880
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
	b	.L881
.L880:
	mov	r0, r8
	ldr	r1, [sp, #20]
	ldr	r2, [sp, #8]
	mov	r3, r10
	bl	FlashReadRawPage
	mov	fp, r0
.L881:
	cmn	fp, #1
	beq	.L882
	ldr	r3, .L902+8
	cmn	r9, #1
	moveq	r9, fp
	ldrb	r2, [r3, #-2743]	@ zero_extendqisi2
	add	r2, r2, r2, asl #1
	cmp	fp, r2, asr #2
	bcc	.L884
	mov	r10, #0
	str	r10, [sp, #8]
.L882:
	add	r7, r7, #1
	b	.L876
.L901:
	mov	fp, r9
.L884:
	ldr	r3, [sp, #12]
	mov	r0, r6
	mov	r1, #0
	ldrb	r2, [r3]	@ zero_extendqisi2
	sub	r2, r2, #67
	cmp	r2, #1
	bhi	.L886
	bl	SandiskSetRRPara
	b	.L887
.L886:
	bl	ToshibaSetRRPara
.L887:
	ldr	r3, [sp, #16]
	mov	r2, #255
	add	r5, r5, r3
	str	r2, [r5, #2056]
	ldr	r2, .L902+8
	ldrb	r2, [r2, #-2743]	@ zero_extendqisi2
	add	r2, r2, r2, asl #1
	cmp	fp, r2, asr #2
	bcc	.L888
	cmn	fp, #1
	movne	fp, #256
.L888:
	mov	r0, r8
	bl	NandcWaitFlashReady
	cmp	r4, #0
	beq	.L889
	mov	r0, #4
	bl	NandcSetDdrMode
.L889:
	mov	r0, fp
	add	sp, sp, #28
	@ sp needed
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L903:
	.align	2
.L902:
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
	ldr	r4, .L918
	mov	r7, r3
	mov	fp, r1
	mov	r9, r2
	add	r4, r4, r8, asl #3
	bl	NandcWaitFlashReady
	ldr	r10, .L918+4
	mov	r6, #1
	ldrb	r3, [r4, #16]	@ zero_extendqisi2
	ldr	r5, [r4, #12]
	mvn	r4, #0
	add	r3, r3, #8
	ldr	ip, .L918+8
	add	r5, r5, r3, asl #8
.L905:
	ldrb	r3, [r10]	@ zero_extendqisi2
	add	r3, r3, #1
	cmp	r6, r3
	bcs	.L908
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
	beq	.L906
	ldrb	r3, [ip, #-2743]	@ zero_extendqisi2
	cmn	r4, #1
	moveq	r4, r0
	add	r3, r3, r3, asl #1
	cmp	r0, r3, asr #2
	bcc	.L911
	mov	r7, #0
	mov	r9, r7
.L906:
	add	r6, r6, #1
	b	.L905
.L911:
	mov	r4, r0
.L908:
	mov	r0, r5
	mov	r1, #0
	bl	SamsungSetRRPara
	ldr	r3, .L918+8
	ldrb	r3, [r3, #-2743]	@ zero_extendqisi2
	add	r3, r3, r3, asl #1
	cmp	r4, r3, asr #2
	bcc	.L910
	cmn	r4, #1
	movne	r4, #256
.L910:
	mov	r0, r4
	add	sp, sp, #12
	@ sp needed
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L919:
	.align	2
.L918:
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
.L922:
	stmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.save {r4, r5, r6, r7, r8, r9, r10, fp, lr}
	mov	r8, r3
	ldr	r3, .L946
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
	ldrne	r2, .L946+4
	ubfxeq	r5, r5, #2, #8
	smullne	r2, r3, r5, r2
	uxtbne	r5, r3
	ldr	r3, .L946+8
	add	r3, r3, r0, asl #3
	str	r3, [sp, #24]
.L932:
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
.L923:
	ldr	r3, .L946+12
	ldrb	r3, [r3]	@ zero_extendqisi2
	cmp	r9, r3
	bcs	.L926
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
	beq	.L924
	cmn	r4, #1
	ldr	ip, [sp, #28]
	moveq	r4, r0
	cmp	r0, r5
	bcc	.L934
	mov	r8, ip
	mov	fp, ip
.L924:
	ldr	r9, [sp, #8]
	b	.L923
.L934:
	mov	r4, r0
	mov	r8, ip
	mov	fp, ip
.L926:
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
	bcc	.L928
	cmn	r4, #1
	movne	r4, #256
.L928:
	cmn	r4, #1
	movne	r7, #0
	moveq	r7, #1
	cmp	r4, #256
	movne	r1, r7
	orreq	r1, r7, #1
	cmp	r1, #0
	beq	.L929
	str	r4, [sp]
	mov	r1, r9
	ldr	r0, .L946+16
	mov	r3, r9
	ldr	r2, [sp, #20]
	bl	printk
	cmp	r10, #0
	bne	.L930
	ldr	r3, .L946
	ldrb	r3, [r3, #-2744]	@ zero_extendqisi2
	cmp	r3, #0
	moveq	r7, #0
	andne	r7, r7, #1
	cmp	r7, #0
	beq	.L939
	mov	r0, r6
	mov	r1, #3
	bl	micron_auto_read_calibration_config
	mov	r10, #1
	b	.L932
.L930:
	mov	r0, r6
	mov	r1, #0
	bl	micron_auto_read_calibration_config
	cmn	r4, #1
	movne	r4, #256
	b	.L939
.L929:
	cmp	r10, #0
	beq	.L939
	mov	r0, r6
	mov	r4, #256
	bl	micron_auto_read_calibration_config
.L939:
	mov	r0, r4
	add	sp, sp, #36
	@ sp needed
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L947:
	.align	2
.L946:
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
	ldr	r2, .L966
	mov	fp, r1
	ldr	ip, .L966+4
	mov	r8, r3
	mov	r5, r0
	ldr	r1, [r2, #44]
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
.L950:
	cmp	ip, r9
	bcs	.L954
	add	r4, r4, #1
	mov	r0, r5
	ldrb	r1, [r7, #-2731]	@ zero_extendqisi2
	uxtb	r4, r4
	ldr	r2, .L966+8
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
	beq	.L952
	ldrb	r3, [r7, #-2743]	@ zero_extendqisi2
	cmn	r6, #1
	moveq	r6, r0
	add	r3, r3, r3, asl #1
	cmp	r0, r3, asr #2
	bcc	.L959
	mov	r8, #0
	mov	r10, r8
.L952:
	add	ip, ip, #1
	b	.L950
.L959:
	mov	r6, r0
.L954:
	ldr	r3, [sp]
	add	r5, r7, r5
	ldr	r3, [r3, #44]
	ldrb	r3, [r3, #19]	@ zero_extendqisi2
	sub	r3, r3, #7
	cmp	r3, #1
	ldrb	r3, [r7, #-2743]	@ zero_extendqisi2
	strlsb	r4, [r5, #-2712]
	strhib	r4, [r5, #-2720]
	add	r3, r3, r3, asl #1
	cmp	r6, r3, asr #2
	bcc	.L958
	cmn	r6, #1
	movne	r6, #256
.L958:
	mov	r0, r6
	add	sp, sp, #12
	@ sp needed
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L967:
	.align	2
.L966:
	.word	.LANCHOR0
	.word	.LANCHOR2
	.word	.LANCHOR2-2728
	.fnend
	.size	HynixReadRetrial, .-HynixReadRetrial
	.align	2
	.type	samsung_read_retrial, %function
samsung_read_retrial:
	.fnstart
	@ args = 0, pretend = 0, frame = 16
	@ frame_needed = 0, uses_anonymous_args = 0
	stmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.save {r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.pad #28
	sub	sp, sp, #28
	mov	r10, r0
	mov	fp, r2
	mov	r8, r3
	str	r1, [sp, #12]
	bl	NandcWaitFlashReady
	ldr	r3, .L998
	add	r3, r3, r10, asl #3
	ldr	r4, [r3, #12]
	ldrb	r3, [r3, #16]	@ zero_extendqisi2
	str	r3, [sp, #8]
	ldr	r3, .L998+4
	ldrb	r2, [r3, #-1876]	@ zero_extendqisi2
	str	r3, [sp, #16]
	cmp	r2, #0
	bne	.L969
	ldr	r3, [sp, #8]
	mvn	r5, #0
	mov	r6, #1
	mov	r9, r3, asl #8
	add	r7, r4, r9
.L973:
	mov	r3, #239
	str	r3, [r7, #2056]
	mov	r3, #141
	str	r3, [r7, #2052]
	ldr	r3, .L998+8
	mov	ip, #0
	mov	r0, r10
	ldr	r1, [sp, #12]
	mov	r2, fp
	str	ip, [sp, #20]
	ldrsb	r3, [r6, r3]
	str	r3, [r7, #2048]
	mov	r3, r8
	str	ip, [r7, #2048]
	str	ip, [r7, #2048]
	str	ip, [r7, #2048]
	bl	FlashReadRawPage
	cmn	r0, #1
	beq	.L970
	ldr	r3, [sp, #16]
	cmn	r5, #1
	moveq	r5, r0
	ldrb	r3, [r3, #-2743]	@ zero_extendqisi2
	add	r3, r3, r3, asl #1
	cmp	r0, r3, asr #2
	bcc	.L981
	ldr	ip, [sp, #20]
	mov	r8, ip
	mov	fp, ip
.L970:
	add	r6, r6, #1
	cmp	r6, #26
	bne	.L973
	b	.L972
.L981:
	mov	r5, r0
.L972:
	add	r9, r4, r9
	mov	r3, #239
	str	r3, [r9, #2056]
	ldr	r3, [sp, #8]
	add	r4, r4, r3, asl #8
	mov	r3, #141
	b	.L997
.L969:
	ldr	r3, [sp, #8]
	mvn	r5, #0
	ldr	r7, .L998+12
	mov	r6, #1
	mov	ip, r3, asl #8
	add	r9, r4, ip
.L978:
	mov	r3, #239
	str	r3, [r9, #2056]
	mov	r3, #137
	str	r3, [r9, #2052]
	ldrb	r3, [r7, #4]	@ zero_extendqisi2
	mov	r0, r10
	ldr	r1, [sp, #12]
	mov	r2, fp
	str	ip, [sp, #20]
	str	r3, [r9, #2048]
	ldrb	r3, [r7, #5]	@ zero_extendqisi2
	str	r3, [r9, #2048]
	ldrb	r3, [r7, #6]	@ zero_extendqisi2
	str	r3, [r9, #2048]
	ldrb	r3, [r7, #7]	@ zero_extendqisi2
	str	r3, [r9, #2048]
	mov	r3, r8
	bl	FlashReadRawPage
	cmn	r0, #1
	ldr	ip, [sp, #20]
	beq	.L975
	ldr	r3, [sp, #16]
	cmn	r5, #1
	moveq	r5, r0
	ldrb	r3, [r3, #-2743]	@ zero_extendqisi2
	add	r3, r3, r3, asl #1
	cmp	r0, r3, asr #2
	bcc	.L982
	mov	r8, #0
	mov	fp, r8
.L975:
	add	r6, r6, #1
	add	r7, r7, #4
	cmp	r6, #26
	bne	.L978
	b	.L977
.L982:
	mov	r5, r0
.L977:
	add	ip, r4, ip
	mov	r3, #239
	str	r3, [ip, #2056]
	ldr	r3, [sp, #8]
	add	r4, r4, r3, asl #8
	mov	r3, #137
.L997:
	str	r3, [r4, #2052]
	mov	r3, #0
	str	r3, [r4, #2048]
	str	r3, [r4, #2048]
	str	r3, [r4, #2048]
	str	r3, [r4, #2048]
	ldr	r3, [sp, #16]
	ldrb	r3, [r3, #-2743]	@ zero_extendqisi2
	add	r3, r3, r3, asl #1
	cmp	r5, r3, asr #2
	bcc	.L979
	cmn	r5, #1
	movne	r5, #256
.L979:
	cmn	r5, #1
	cmpne	r5, #256
	bne	.L980
	str	r5, [sp]
	mov	r1, r6
	ldr	r0, .L998+16
	mov	r3, r6
	ldr	r2, [sp, #12]
	bl	printk
.L980:
	mov	r0, r10
	bl	NandcWaitFlashReady
	mov	r0, r5
	add	sp, sp, #28
	@ sp needed
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L999:
	.align	2
.L998:
	.word	.LANCHOR0
	.word	.LANCHOR2
	.word	.LANCHOR3+4
	.word	.LANCHOR3+32
	.word	.LC12
	.fnend
	.size	samsung_read_retrial, .-samsung_read_retrial
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
	ldr	r3, .L1004
	subs	r4, r0, #0
	mov	r5, r1
	mov	r7, r2
	ldrb	r6, [r3, #265]	@ zero_extendqisi2
	bne	.L1001
	ldr	r2, .L1004+4
	ldrb	r3, [r2, #1]	@ zero_extendqisi2
	ldr	r1, [r2, #4]
	mul	r1, r1, r3
	cmp	r5, r1
	bcs	.L1001
	ldrb	r3, [r2]	@ zero_extendqisi2
	cmp	r3, #0
	movne	r6, #4
.L1001:
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
.L1005:
	.align	2
.L1004:
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
	ldr	r4, .L1020
	ldr	r7, .L1020+4
	ldr	r9, .L1020+8
	ldr	r3, [r4, #-1868]
	mov	r8, r4
	ldrb	r0, [r4, #-1779]	@ zero_extendqisi2
	mov	fp, r7
	str	r3, [r4, #-1792]
	bl	FlashBchSel
	mov	r1, #0
	mov	r2, #2048
	ldr	r0, [r4, #-1868]
	bl	ftl_memset
	ldr	r3, [r4, #-1792]
	ldr	r1, .L1020+12
	mov	r2, #32
	str	r9, [r3]
	ldr	r0, [r4, #-1792]
	ldrb	r3, [r7, #3152]	@ zero_extendqisi2
	add	r0, r0, #16
	strh	r3, [r0, #-4]	@ movhi
	ldrb	r3, [r7, #1]	@ zero_extendqisi2
	strh	r3, [r0, #-2]	@ movhi
	ldrb	r3, [r4, #-1860]	@ zero_extendqisi2
	str	r3, [r0, #1060]
	bl	ftl_memcpy
	ldr	r0, [r4, #-1792]
	ldr	r1, .L1020+16
	mov	r2, #8
	add	r0, r0, #80
	bl	ftl_memcpy
	ldr	r0, [r4, #-1792]
	ldr	r1, .L1020+20
	mov	r2, #32
	add	r0, r0, #96
	bl	ftl_memcpy
	ldr	r0, [r4, #-1792]
	ldr	r1, .L1020+24
	mov	r2, #32
	add	r0, r0, #160
	bl	ftl_memcpy
	ldr	r0, [r4, #-1792]
	add	r1, r7, #48
	mov	r2, #32
	add	r0, r0, #192
	bl	ftl_memcpy
	ldr	r0, [r4, #-1792]
	mov	r2, #852
	ldr	r1, .L1020+28
	add	r0, r0, #224
	bl	ftl_memcpy
	ldr	r5, [r4, #-1792]
	movw	r1, #2036
	add	r0, r5, #12
	bl	js_hash
	movw	r3, #1592
	str	r3, [r5, #4]
	ldr	r3, [r4, #-1776]
	str	r3, [r4, #-1792]
	str	r0, [r5, #8]
	mov	r0, #0
	bl	flash_enter_slc_mode
	mov	r5, #0
	mov	r6, r5
.L1012:
	ldr	r1, [r7, #4]
	mov	r0, #0
	mov	r2, r0
	mul	r1, r1, r6
	bl	FlashEraseBlock
	ldrb	r10, [r4, #-2744]	@ zero_extendqisi2
	cmp	r10, #0
	beq	.L1007
	mov	r10, #0
.L1008:
	ldr	r1, [r7, #4]
	mov	r0, #0
	ldr	r2, [r4, #-1868]
	mov	r3, r0
	mla	r1, r1, r6, r10
	add	r10, r10, #1
	bl	FlashProgPage
	cmp	r10, #10
	bne	.L1008
	b	.L1009
.L1007:
	ldr	r1, [fp, #4]
	mov	r3, r10
	ldr	r2, [r8, #-1868]
	mov	r0, r10
	mul	r1, r1, r6
	bl	FlashProgPage
	ldr	r1, [fp, #4]
	mov	r0, r10
	ldr	r2, [r8, #-1868]
	mov	r3, r10
	mul	r1, r1, r6
	add	r1, r1, #1
	bl	FlashProgPage
.L1009:
	ldr	r1, [r7, #4]
	mov	r0, #0
	ldr	r2, [r4, #-1776]
	mov	r3, r0
	mul	r1, r1, r6
	bl	FlashReadRawPage
	add	r2, r6, #1
	cmn	r0, #1
	beq	.L1010
	ldr	r10, [r8, #-1792]
	ldr	r3, [r10]
	cmp	r3, r9
	bne	.L1010
	add	r0, r10, #12
	movw	r1, #2036
	str	r2, [sp, #4]
	bl	js_hash
	ldr	r3, [r10, #8]
	cmp	r3, r0
	ldr	r2, [sp, #4]
	bne	.L1010
	ldr	r3, [fp, #4]
	cmp	r5, #1
	str	r2, [r8, #-1784]
	mul	r6, r3, r6
	str	r6, [r8, #-1788]
	beq	.L1013
	mov	r5, #1
.L1010:
	cmp	r2, #4
	mov	r6, r2
	bne	.L1012
	b	.L1011
.L1013:
	mov	r5, #2
.L1011:
	mov	r0, #0
	bl	flash_exit_slc_mode
	clz	r0, r5
	mov	r0, r0, lsr #5
	rsb	r0, r0, #0
	add	sp, sp, #12
	@ sp needed
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L1021:
	.align	2
.L1020:
	.word	.LANCHOR2
	.word	.LANCHOR0
	.word	1312902724
	.word	IDByte
	.word	.LANCHOR0+3156
	.word	.LANCHOR0+3164
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
	ldr	r3, .L1042
	.pad #20
	sub	sp, sp, #20
	ldr	r5, .L1042+4
	ldr	r0, [r3]	@ unaligned
	ldrb	r3, [r5, #-2743]	@ zero_extendqisi2
	str	r0, [sp, #12]	@ unaligned
	str	r3, [sp]
	ldr	r3, [r5, #-1864]
	cmp	r3, #0
	beq	.L1023
	mov	r0, #0
	bl	flash_enter_slc_mode
.L1023:
	mov	r0, r9
	mov	r1, #0
	mov	r2, #2048
	ldr	r10, .L1042+8
	bl	ftl_memset
	mvn	r8, #0
	mov	r4, #2
	mov	fp, r10
.L1024:
	ldrb	r3, [r10, #1]	@ zero_extendqisi2
	cmp	r4, r3
	bcs	.L1028
	mov	r7, #0
.L1026:
	add	r3, sp, #12
	ldr	r6, .L1042+4
	ldrb	ip, [r7, r3]	@ zero_extendqisi2
	mov	r0, ip
	str	ip, [sp, #4]
	bl	FlashBchSel
	ldr	r1, [fp, #4]
	mov	r0, #0
	ldr	r2, [r5, #-1868]
	mov	r3, r0
	mul	r1, r1, r4
	bl	FlashReadRawPage
	cmn	r0, #1
	ldr	ip, [sp, #4]
	bne	.L1025
	add	r7, r7, #1
	cmp	r7, #4
	bne	.L1026
	b	.L1027
.L1025:
	ldr	r3, [r6, #-1868]
	ldr	r2, .L1042+12
	ldr	r3, [r3]
	cmp	r3, r2
	bne	.L1027
	mov	r1, ip
	ldr	r0, .L1042+16
	bl	printk
	mov	r0, r9
	ldr	r1, [r6, #-1868]
	mov	r2, #2048
	bl	ftl_memcpy
	ldr	r3, [r6, #-1868]
	ldr	r3, [r3, #512]
	strb	r3, [fp, #1]
	ldr	r3, [r6, #-1784]
	cmp	r3, r4
	bls	.L1031
	str	r4, [r6, #-1784]
	bl	FlashSavePhyInfo
	mov	r8, #0
.L1027:
	add	r4, r4, #1
	b	.L1024
.L1031:
	mov	r8, #0
.L1028:
	ldr	r0, [sp]
	bl	FlashBchSel
	ldr	r3, [r5, #-1864]
	cmp	r3, #0
	beq	.L1035
	mov	r0, #0
	bl	flash_exit_slc_mode
.L1035:
	mov	r0, r8
	add	sp, sp, #20
	@ sp needed
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L1043:
	.align	2
.L1042:
	.word	.LANCHOR3
	.word	.LANCHOR2
	.word	.LANCHOR0
	.word	-52655045
	.word	.LC13
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
	ldr	r4, .L1143
	ldr	r5, .L1143+4
	mov	r7, #0
	str	r0, [r4, #-1868]
	mov	r0, #32768
	bl	ftl_malloc
	str	r0, [r4, #-1776]
	mov	r0, #4096
	bl	ftl_malloc
	str	r0, [r4, #-1772]
	mov	r0, #32768
	bl	ftl_malloc
	str	r0, [r4, #-1768]
	mov	r0, #4096
	bl	ftl_malloc
	mov	r3, #50
	str	r7, [r4, #-1784]
	strb	r3, [r5, #1]
	strb	r3, [r4, #-1780]
	mov	r3, #128
	strb	r7, [r4, #-1860]
	str	r3, [r5, #4]
	mov	r3, #60
	str	r7, [r4, #-1800]
	strb	r7, [r5]
	strb	r7, [r4, #-1760]
	strb	r3, [r4, #-1779]
	str	r0, [r4, #-1764]
	mov	r0, r6
	bl	NandcInit
	ldr	r6, .L1143+8
	mov	r8, r6
.L1050:
	ldr	r3, .L1143+12
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
	bne	.L1045
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
.L1045:
	mov	r0, r9
	bl	NandcFlashDeCs
	ldrb	r2, [r6]	@ zero_extendqisi2
	sub	r3, r2, #1
	uxtb	r3, r3
	cmp	r3, #253
	bhi	.L1046
	ldrb	r1, [r6, #2]	@ zero_extendqisi2
	ldrb	r3, [r6, #1]	@ zero_extendqisi2
	ldr	r0, .L1143+16
	str	r1, [sp]
	ldrb	r1, [r6, #3]	@ zero_extendqisi2
	str	r1, [sp, #4]
	ldrb	r1, [r6, #4]	@ zero_extendqisi2
	str	r1, [sp, #8]
	ldrb	r1, [r6, #5]	@ zero_extendqisi2
	str	r1, [sp, #12]
	add	r1, r7, #1
	bl	printk
.L1046:
	cmp	r7, #0
	bne	.L1047
	ldrb	r3, [r8]	@ zero_extendqisi2
	sub	r3, r3, #1
	uxtb	r3, r3
	cmp	r3, #253
	bhi	.L1097
	ldr	r3, .L1143+8
	ldrb	r3, [r3, #1]	@ zero_extendqisi2
	cmp	r3, #255
	beq	.L1097
	bl	FlashCs123Init
.L1047:
	ldrb	r3, [r6]	@ zero_extendqisi2
	add	r7, r7, #1
	add	r6, r6, #8
	cmp	r3, #181
	moveq	r3, #44
	streqb	r3, [r6, #-8]
	cmp	r7, #4
	bne	.L1050
	ldrb	r3, [r8]	@ zero_extendqisi2
	cmp	r3, #173
	beq	.L1051
	ldr	r0, [r4, #-1856]
	bl	NandcSetDdrMode
.L1051:
	mov	r2, #852
	ldr	r0, .L1143+20
	mov	r1, #0
	ldr	r7, .L1143+24
	bl	ftl_memset
	ldr	r2, [r4, #-1848]
	ldr	r0, .L1143+28
	cmp	r2, r7
	add	r3, r0, #256
	str	r3, [r5, #44]
	mov	r3, #0
	strb	r3, [r5, #8]
	bne	.L1052
	ldrb	r3, [r0, #275]	@ zero_extendqisi2
	cmp	r3, #50
	ldrne	r3, .L1143
	movne	r1, #1
	strne	r1, [r3, #-1864]
.L1052:
	ldrb	r6, [r8, #1]	@ zero_extendqisi2
	sub	ip, r6, #218
	cmp	r6, #161
	cmpne	r6, #241
	clz	ip, ip
	and	r1, r6, #253
	moveq	r3, #1
	movne	r3, #0
	mov	ip, ip, lsr #5
	orr	r3, ip, r3
	cmp	r1, #209
	orreq	r3, r3, #1
	cmp	r3, #0
	bne	.L1053
	cmp	r6, #220
	bne	.L1054
	ldr	r3, .L1143+8
	ldrb	r3, [r3, #3]	@ zero_extendqisi2
	cmp	r3, #149
	bne	.L1054
.L1053:
	mov	lr, #16
	strb	lr, [r5, #1]
	strb	lr, [r4, #-1779]
	mov	r1, #1
	ldrb	lr, [r8]	@ zero_extendqisi2
	strb	r1, [r5]
	cmp	lr, #152
	ldr	r3, .L1143
	strb	lr, [r0, #3413]
	strb	r6, [r0, #3414]
	bne	.L1056
	ldr	lr, .L1143+8
	ldrsb	lr, [lr, #4]
	cmp	lr, #0
	strltb	r1, [r3, #-1760]
	movge	r1, #24
	strgeb	r1, [r3, #-1779]
.L1056:
	movw	r3, #2049
	cmp	r2, r7
	cmpne	r2, r3
	moveq	r3, #16
	streqb	r3, [r4, #-1779]
	cmp	ip, #0
	ldrne	r3, .L1143+32
	movne	r2, #2048
	strneh	r2, [r3, #14]	@ movhi
	mvnne	r3, #37
	bne	.L1137
.L1060:
	cmp	r6, #220
	bne	.L1062
	ldr	r3, .L1143+32
	mov	r2, #4096
	strh	r2, [r3, #14]	@ movhi
	mvn	r3, #35
.L1137:
	strb	r3, [r0, #3414]
	b	.L1061
.L1062:
	cmp	r6, #211
	ldreq	r3, .L1143+32
	moveq	r2, #4096
	streqh	r2, [r3, #14]	@ movhi
	moveq	r3, #2
	streqb	r3, [r0, #3425]
.L1061:
	ldr	r1, .L1143+36
	mov	r2, #32
	ldr	r0, .L1143+40
	bl	ftl_memcpy
	ldr	r0, .L1143+44
	ldr	r1, .L1143+32
	mov	r2, #32
	bl	ftl_memcpy
.L1054:
	ldrb	r3, [r5]	@ zero_extendqisi2
	cmp	r3, #0
	bne	.L1063
	bl	FlashLoadPhyInfoInRam
	cmp	r0, #0
	bne	.L1065
	ldr	r3, .L1143+4
	ldr	r6, .L1143
	ldr	r3, [r3, #44]
	ldrh	r3, [r3, #16]
	mov	r3, r3, lsr #8
	tst	r3, #1
	and	r0, r3, #7
	strb	r0, [r4, #-1875]
	bne	.L1065
	mov	r3, #1
	strb	r3, [r6, #-1860]
	bl	FlashSetInterfaceMode
	ldrb	r0, [r6, #-1875]	@ zero_extendqisi2
	bl	NandcSetMode
.L1065:
	ldr	r3, [r5, #44]
	ldr	r6, .L1143
	ldrb	r3, [r3, #26]	@ zero_extendqisi2
	strb	r3, [r4, #-2744]
	bl	FlashLoadPhyInfo
	cmp	r0, #0
	beq	.L1063
	ldr	r3, [r6, #-1856]
	cmp	r3, #0
	beq	.L1068
	mov	r0, #1
	bl	FlashSetInterfaceMode
	mov	r0, #1
	b	.L1138
.L1068:
	ldrb	r0, [r6, #-1875]	@ zero_extendqisi2
	bl	FlashSetInterfaceMode
	ldrb	r0, [r6, #-1875]	@ zero_extendqisi2
.L1138:
	bl	NandcSetMode
	bl	FlashLoadPhyInfo
	cmp	r0, #0
	beq	.L1063
	mov	r0, #1
	ldr	r6, .L1143+4
	bl	FlashSetInterfaceMode
	mov	r0, #1
	bl	NandcSetMode
	ldr	r3, [r5, #44]
	ldr	r0, .L1143+48
	ldrh	r1, [r3, #14]
	bl	printk
	bl	FlashLoadPhyInfoInRam
	cmn	r0, #1
	beq	.L1111
	bl	FlashDieInfoInit
	ldr	r3, [r6, #44]
	ldrb	r0, [r3, #19]	@ zero_extendqisi2
	bl	FlashGetReadRetryDefault
	movw	r3, #3324
	ldr	r2, [r6, #44]
	ldrh	r3, [r6, r3]
	add	r3, r3, #4080
	ldrb	r1, [r2, #9]	@ zero_extendqisi2
	add	r3, r3, #15
	cmp	r1, r3, asr #12
	ldrh	r3, [r2, #14]
	blt	.L1070
	add	r0, r3, #255
	cmp	r1, r0, asr #8
	bge	.L1071
.L1070:
	bic	r3, r3, #255
	strh	r3, [r2, #14]	@ movhi
.L1071:
	ldrb	r3, [r4, #-1875]	@ zero_extendqisi2
	tst	r3, #6
	beq	.L1072
	bl	FlashSavePhyInfo
	mov	r0, #0
	bl	flash_enter_slc_mode
	ldr	r3, .L1143
	mov	r0, #0
	ldr	r1, [r3, #-1788]
	bl	FlashDdrParaScan
	mov	r0, #0
	bl	flash_exit_slc_mode
.L1072:
	bl	FlashSavePhyInfo
.L1063:
	ldr	r2, [r5, #44]
	ldr	r6, .L1143
	ldrb	r3, [r2, #26]	@ zero_extendqisi2
	ldrh	r0, [r2, #10]
	ldrb	r9, [r2, #18]	@ zero_extendqisi2
	strb	r3, [r4, #-2744]
	ldrh	r3, [r2, #16]
	ubfx	r1, r3, #7, #1
	strb	r1, [r5, #8]
	ubfx	r1, r3, #3, #1
	strb	r1, [r4, #-1759]
	ubfx	r1, r3, #4, #1
	ubfx	r3, r3, #8, #3
	strb	r1, [r4, #-1874]
	strb	r3, [r4, #-1875]
	mov	r3, #0
	ldrb	r1, [r2, #12]	@ zero_extendqisi2
	str	r3, [r4, #-1796]
	bl	__aeabi_idiv
	mov	r1, r0
	mov	r0, r9
	bl	BuildFlashLsbPageTable
	bl	FlashDieInfoInit
	ldr	r3, [r5, #44]
	ldrh	r2, [r3, #16]
	tst	r2, #64
	beq	.L1074
	ldrb	r0, [r3, #19]	@ zero_extendqisi2
	ldr	r3, .L1143+52
	ldr	r2, .L1143+56
	ldrb	r1, [r6, #-2730]	@ zero_extendqisi2
	strb	r0, [r3]
	ldrb	r3, [r6, #-2731]	@ zero_extendqisi2
	mov	ip, r2
	strb	r3, [r2]
	ldr	r3, .L1143+60
	strb	r1, [r3]
	sub	r1, r0, #1
	cmp	r1, #7
	bhi	.L1075
	ldr	r3, .L1143+64
	sub	r2, r0, #5
	cmp	r0, #8
	cmpne	r2, #1
	sub	r1, r0, #8
	clz	r1, r1
	str	r3, [r6, #-1796]
	movls	r3, #1
	strls	r3, [r6, #-1808]
	cmp	r0, #7
	mov	r1, r1, lsr #5
	ldreq	r3, .L1143+68
	beq	.L1078
	ldr	r2, .L1143+68
	cmp	r1, #0
	sub	r3, r2, #8
	movne	r3, r2
.L1078:
	sub	r1, r3, #1
	add	r3, r3, #31
	mov	r2, #0
.L1079:
	ldrsb	ip, [r1, #1]!
	cmp	ip, #0
	addeq	r2, r2, #1
	cmp	r1, r3
	bne	.L1079
	cmp	r2, #27
	bls	.L1074
	bl	FlashGetReadRetryDefault
	bl	FlashSavePhyInfo
	b	.L1074
.L1075:
	sub	r1, r0, #17
	cmp	r1, #2
	bhi	.L1081
	ldr	r2, .L1143+72
	cmp	r0, #19
	str	r2, [r6, #-1796]
	moveq	r2, #15
	bne	.L1142
	b	.L1140
.L1081:
	sub	r1, r0, #65
	cmp	r0, #33
	cmpne	r1, #1
	bhi	.L1083
	ldr	r1, .L1143+76
	str	r1, [r6, #-1796]
	mov	r1, #4
	strb	r1, [r2]
.L1142:
	mov	r2, #7
.L1140:
	strb	r2, [r3]
	b	.L1074
.L1083:
	sub	r1, r0, #67
	sub	r2, r0, #34
	cmp	r1, #1
	movhi	r1, #0
	movls	r1, #1
	cmp	r2, #1
	movhi	r2, r1
	orrls	r2, r1, #1
	cmp	r2, #0
	beq	.L1084
	ldr	r2, .L1143+76
	cmp	r0, #68
	cmpne	r0, #35
	str	r2, [r6, #-1796]
	movne	r2, #7
	moveq	r2, #17
	cmp	r1, #0
	strb	r2, [r3]
	movne	r3, #4
	moveq	r3, #5
	strb	r3, [ip]
	b	.L1074
.L1084:
	cmp	r0, #49
	ldreq	r3, .L1143+80
	streq	r3, [r6, #-1796]
	beq	.L1074
	cmp	r0, #50
	streq	r2, [r6, #-1864]
	ldreq	r3, .L1143+84
	streq	r3, [r6, #-1796]
.L1074:
	ldr	r3, [r4, #-1848]
	cmp	r3, r7
	bne	.L1089
	ldr	r2, .L1143
	ldrb	r2, [r2, #-2744]	@ zero_extendqisi2
	cmp	r2, #0
	ldrne	r2, [r5, #44]
	movne	r1, #0
	strneb	r1, [r2, #18]
.L1089:
	ldrb	r2, [r8]	@ zero_extendqisi2
	cmp	r2, #44
	bne	.L1090
	ldrb	r2, [r4, #-1860]	@ zero_extendqisi2
	cmp	r2, #0
	beq	.L1090
	cmp	r3, r7
	bne	.L1091
	ldr	r3, .L1143
	ldrb	r3, [r3, #-2744]	@ zero_extendqisi2
	cmp	r3, #0
	bne	.L1090
.L1091:
	mov	r0, #1
	mov	r3, #0
	strb	r3, [r4, #-1860]
	bl	FlashSetInterfaceMode
	mov	r0, #1
	bl	NandcSetMode
.L1090:
	ldrb	r3, [r4, #-1875]	@ zero_extendqisi2
	tst	r3, #6
	beq	.L1092
	ldr	r2, .L1143
	ldrb	r2, [r2, #-1860]	@ zero_extendqisi2
	cmp	r2, #0
	bne	.L1093
	tst	r3, #1
	bne	.L1092
.L1093:
	mov	r0, #0
	bl	flash_enter_slc_mode
	mov	r0, #0
	ldr	r1, [r4, #-1788]
	bl	FlashDdrParaScan
	mov	r0, #0
	bl	flash_exit_slc_mode
.L1092:
	ldr	r3, [r5, #44]
	mov	r10, #16
	ldr	r9, .L1143+4
	ldr	r6, .L1143+88
	ldrb	r0, [r3, #20]	@ zero_extendqisi2
	bl	FlashBchSel
	add	r0, r9, #3328
	bl	FlashReadIdbDataRaw
	ldr	r0, .L1143+92
	strb	r10, [r5, #1]
	bl	FlashTimingCfg
	ldr	r7, [r5, #44]
	ldrb	r2, [r8, #1]	@ zero_extendqisi2
	ldrb	r3, [r7, #12]	@ zero_extendqisi2
	strh	r3, [r6, #8]	@ movhi
	ldrb	r3, [r7, #7]	@ zero_extendqisi2
	str	r3, [r4, #-2768]
	mov	r3, r2, asl r10
	orr	r2, r3, r2, asl #8
	ldrb	r3, [r8]	@ zero_extendqisi2
	orr	r3, r2, r3
	ldrb	r2, [r8, #3]	@ zero_extendqisi2
	orr	r3, r3, r2, asl #24
	str	r3, [r4, #-2772]
	ldrb	r3, [r5, #3152]	@ zero_extendqisi2
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
	bne	.L1095
	mov	r3, r3, asl #1
	mov	r4, r4, lsr #1
	mov	r2, r2, asl #1
	strh	r3, [r6, #22]	@ movhi
	strb	r10, [r9, #1]
	mov	r3, #8
	strh	r4, [r6, #14]	@ movhi
	strh	r2, [r6, #20]	@ movhi
	strh	r3, [r6, #26]	@ movhi
.L1095:
	ldrb	r0, [r7, #20]	@ zero_extendqisi2
	bl	FlashBchSel
	bl	ftl_flash_suspend
	mov	r0, #0
	b	.L1111
.L1097:
	mvn	r0, #1
.L1111:
	add	sp, sp, #20
	@ sp needed
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L1144:
	.align	2
.L1143:
	.word	.LANCHOR2
	.word	.LANCHOR0
	.word	IDByte
	.word	.LANCHOR0+12
	.word	.LC14
	.word	.LANCHOR2-2732
	.word	1446522928
	.word	.LANCHOR1
	.word	.LANCHOR1+3412
	.word	.LANCHOR1+3072
	.word	.LANCHOR0+48
	.word	.LANCHOR1+256
	.word	.LC15
	.word	g_retryMode
	.word	g_maxRegNum
	.word	g_maxRetryCount
	.word	HynixReadRetrial
	.word	.LANCHOR2-2704
	.word	MicronReadRetrial
	.word	ToshibaReadRetrial
	.word	SamsungReadRetrial
	.word	samsung_read_retrial
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
	mov	r7, r1
	ldr	r5, .L1162
	mov	r4, r2
	ldr	r1, .L1162+4
	mov	r6, r0
	ldrb	r2, [r5, #-2744]	@ zero_extendqisi2
	ldr	r3, [r1, #44]
	mov	r8, r1
	cmp	r2, #0
	ldrb	r3, [r3, #19]	@ zero_extendqisi2
	beq	.L1146
	ldr	r2, [r5, #-1864]
	cmp	r2, #0
	ldmnefd	sp!, {r4, r5, r6, r7, r8, r9, r10, pc}
.L1146:
	sub	r2, r3, #5
	cmp	r3, #50
	cmpne	r2, #2
	bls	.L1147
	sub	r2, r3, #19
	tst	r2, #239
	moveq	r2, #1
	movne	r2, #0
	cmp	r3, #68
	movne	r3, r2
	orreq	r3, r2, #1
	cmp	r3, #0
	ldmeqfd	sp!, {r4, r5, r6, r7, r8, r9, r10, pc}
.L1147:
	ldr	r9, .L1162+8
	movw	r10, #65535
.L1149:
	ldr	r3, [r8, #44]
	ldrh	r3, [r3, #10]
	cmp	r3, r4
	bls	.L1161
	mov	r3, r4, asl #1
	ldrh	r3, [r9, r3]
	cmp	r3, r10
	ldmnefd	sp!, {r4, r5, r6, r7, r8, r9, r10, pc}
	mov	r1, #255
	mov	r2, #32768
	ldr	r0, [r5, #-1776]
	bl	ftl_memset
	ldr	r2, [r5, #-1776]
	add	r1, r4, r7
	add	r4, r4, #1
	mov	r0, r6
	mov	r3, r2
	uxth	r4, r4
	bl	FlashProgPage
	b	.L1149
.L1161:
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, pc}
.L1163:
	.align	2
.L1162:
	.word	.LANCHOR2
	.word	.LANCHOR0
	.word	.LANCHOR0+1104
	.fnend
	.size	FlashPageProgMsbFFData, .-FlashPageProgMsbFFData
	.align	2
	.global	FlashReadSlc2KPages
	.type	FlashReadSlc2KPages, %function
FlashReadSlc2KPages:
	.fnstart
	@ args = 0, pretend = 0, frame = 24
	@ frame_needed = 0, uses_anonymous_args = 0
	ldr	r3, .L1215
	stmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.save {r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.pad #36
	sub	sp, sp, #36
	ldrb	r3, [r3, #265]	@ zero_extendqisi2
	mov	r4, r0
	ldr	r10, .L1215+4
	mov	r9, #0
	str	r1, [sp, #16]
	str	r2, [sp, #20]
	str	r3, [sp, #12]
.L1165:
	ldr	r3, [sp, #16]
	cmp	r9, r3
	beq	.L1214
	ldr	r3, [sp, #16]
	mov	r0, r4
	ldr	r1, [sp, #20]
	add	r2, sp, #28
	rsb	r3, r9, r3
	ldr	r8, .L1215+8
	uxtb	r3, r3
	str	r3, [sp]
	add	r3, sp, #24
	bl	LogAddr2PhyAddr
	ldrb	r3, [r8, #3152]	@ zero_extendqisi2
	ldr	r5, [sp, #24]
	cmp	r5, r3
	mvncs	r3, #0
	strcs	r3, [r4]
	bcs	.L1167
	add	r5, r8, r5
	mov	r7, #0
	ldrb	r5, [r5, #3156]	@ zero_extendqisi2
	mov	r0, r5
	bl	NandcWaitFlashReady
	mov	r0, r5
	bl	NandcFlashCs
.L1168:
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
	ldr	r3, .L1215+4
	ldrb	r3, [r3, #-1760]	@ zero_extendqisi2
	cmp	r3, #0
	mov	r6, r0
	beq	.L1169
	mov	r0, r5
	bl	flash_read_ecc
	cmp	r0, #5
	movhi	r6, #256
.L1169:
	cmp	r7, #9
	cmnls	r6, #1
	moveq	r3, #1
	movne	r3, #0
	addeq	r7, r7, #1
	beq	.L1168
.L1170:
	cmp	r7, #0
	mov	r7, r3
	movne	r6, #256
.L1172:
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
	ldrb	r2, [r10, #-1760]	@ zero_extendqisi2
	cmp	r2, #0
	mov	fp, r0
	beq	.L1175
	mov	r0, r5
	bl	flash_read_ecc
	cmp	r0, #5
	movhi	fp, #256
.L1175:
	cmp	r7, #9
	cmnls	fp, #1
	addeq	r7, r7, #1
	beq	.L1172
.L1176:
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
	bls	.L1178
	cmn	r3, #1
	movne	r3, #256
.L1178:
	cmp	r3, #256
	cmnne	r3, #1
	movne	r3, #0
	str	r3, [r4]
	ldr	r3, [r4, #12]
	cmp	r3, #0
	beq	.L1181
	ldr	r2, [r3, #12]
	cmn	r2, #1
	bne	.L1181
	ldr	r2, [r3, #8]
	cmn	r2, #1
	bne	.L1181
	ldr	r3, [r3]
	cmn	r3, #1
	strne	r2, [r4]
.L1181:
	ldr	r3, [r4]
	cmn	r3, #1
	bne	.L1167
	ldr	r1, [r4, #4]
	ldr	r0, .L1215+12
	ldrb	r2, [r10, #-2743]	@ zero_extendqisi2
	bl	printk
	ldr	r1, [r4, #8]
	cmp	r1, #0
	beq	.L1183
	ldr	r0, .L1215+16
	mov	r2, #4
	mov	r3, #8
	bl	rknand_print_hex
.L1183:
	ldr	r1, [r4, #12]
	cmp	r1, #0
	beq	.L1167
	mov	r2, #4
	ldr	r0, .L1215+20
	mov	r3, r2
	bl	rknand_print_hex
.L1167:
	add	r9, r9, #1
	add	r4, r4, #36
	b	.L1165
.L1214:
	mov	r0, #0
	add	sp, sp, #36
	@ sp needed
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L1216:
	.align	2
.L1215:
	.word	.LANCHOR1
	.word	.LANCHOR2
	.word	.LANCHOR0
	.word	.LC16
	.word	.LC17
	.word	.LC18
	.fnend
	.size	FlashReadSlc2KPages, .-FlashReadSlc2KPages
	.align	2
	.global	FlashReadPages
	.type	FlashReadPages, %function
FlashReadPages:
	.fnstart
	@ args = 0, pretend = 0, frame = 40
	@ frame_needed = 0, uses_anonymous_args = 0
	ldr	r3, .L1292
	stmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.save {r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.pad #52
	sub	sp, sp, #52
	ldr	r9, .L1292+4
	ldrb	r3, [r3, #265]	@ zero_extendqisi2
	str	r1, [sp, #20]
	ldrb	r8, [r9]	@ zero_extendqisi2
	str	r3, [sp, #16]
	ldrb	r3, [r9, #8]	@ zero_extendqisi2
	cmp	r8, #0
	str	r2, [sp, #24]
	str	r3, [sp, #28]
	beq	.L1254
	bl	FlashReadSlc2KPages
	b	.L1287
.L1254:
	ldr	r7, .L1292+8
	mov	r10, r0
	mov	fp, r8
.L1218:
	ldr	r3, [sp, #20]
	cmp	fp, r3
	bcs	.L1290
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
	ldrb	r2, [r9, #3152]	@ zero_extendqisi2
	ldr	r3, [sp, #40]
	mov	r5, r0
	cmp	r3, r2
	ldrcs	r2, [sp, #8]
	mvncs	r3, #0
	strcs	r3, [r10, r2]
	bcs	.L1221
	add	r3, r9, r3
	ldrb	r4, [r3, #3156]	@ zero_extendqisi2
	ldrb	r3, [r7, #-1759]	@ zero_extendqisi2
	cmp	r3, #0
	mov	r0, r4
	moveq	r5, #0
	bl	NandcWaitFlashReady
	ldr	r3, .L1292+4
	ldr	r3, [r3, #44]
	ldrb	r2, [r3, #19]	@ zero_extendqisi2
	sub	r3, r2, #1
	cmp	r3, #7
	bhi	.L1223
	sub	r2, r2, #7
	add	r1, r7, r4
	cmp	r2, #1
	ldr	r2, .L1292+8
	ldrb	r3, [r1, #-2720]	@ zero_extendqisi2
	add	r2, r2, r4
	ldrlsb	r3, [r1, #-2712]	@ zero_extendqisi2
	ldrb	r2, [r2, #-1880]	@ zero_extendqisi2
	cmp	r2, r3
	beq	.L1223
	mov	r0, r4
	ldrb	r1, [r7, #-2731]	@ zero_extendqisi2
	ldr	r2, .L1292+12
	bl	HynixSetRRPara
.L1223:
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
	beq	.L1225
	ldrb	r3, [r7, #-2744]	@ zero_extendqisi2
	cmp	r3, #0
	beq	.L1225
	bl	flash_enter_slc_mode
	b	.L1226
.L1225:
	bl	flash_exit_slc_mode
.L1226:
	ldr	r1, [sp, #44]
	cmn	r1, #1
	cmpeq	r4, #255
	moveq	r3, #0
	movne	r3, #1
	moveq	r5, r3
	beq	.L1228
	cmp	r5, #0
	beq	.L1229
	ldr	r2, [r9, #4]
	mov	r0, r4
	add	r2, r1, r2
	bl	FlashReadDpCmd
	b	.L1230
.L1229:
	mov	r0, r4
	bl	FlashReadCmd
.L1230:
	mov	r0, r4
	bl	NandcWaitFlashReady
	cmp	r5, #0
	beq	.L1228
	mov	r0, r4
	ldr	r1, [sp, #44]
	bl	FlashReadDpDataOutCmd
.L1228:
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
	bne	.L1226
.L1231:
	cmp	r5, #0
	beq	.L1232
	ldr	r3, .L1292+4
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
.L1232:
	mov	r0, r4
	str	ip, [sp, #32]
	bl	NandcFlashDeCs
	ldr	ip, [sp, #32]
	ldrb	r3, [sp, #28]	@ zero_extendqisi2
	cmn	ip, #1
	strb	r3, [r9, #8]
	bne	.L1239
	ldrb	r3, [r7, #-1860]	@ zero_extendqisi2
	cmp	r3, #0
	bne	.L1234
.L1238:
	ldr	r5, [r7, #-1796]
	cmp	r5, #0
	bne	.L1235
	b	.L1291
.L1234:
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
	beq	.L1237
	ldrb	r3, [r7, #-2743]	@ zero_extendqisi2
	cmp	r0, r3, lsr #1
	bls	.L1257
.L1237:
	ubfx	r0, r5, #8, #8
	str	ip, [sp, #32]
	bl	NandcSetDdrPara
	ldr	ip, [sp, #32]
	cmn	ip, #1
	beq	.L1238
	b	.L1257
.L1235:
	mov	r0, r4
	ldr	r1, [sp, #44]
	ldr	r2, [r6, #8]
	ldr	r3, [r6, #12]
	blx	r5
	cmn	r0, #1
	mov	ip, r0
	bne	.L1259
	ldr	r3, [r9, #44]
	ldrb	r3, [r3, #19]	@ zero_extendqisi2
	sub	r3, r3, #1
	cmp	r3, #7
	bhi	.L1241
	mov	r0, r4
	ldrb	r1, [r7, #-2731]	@ zero_extendqisi2
	ldr	r2, .L1292+12
	mov	r3, #0
	bl	HynixSetRRPara
.L1241:
	ldr	r1, [sp, #44]
	mov	r0, r4
	ldr	r2, [r6, #8]
	ldr	r3, [r6, #12]
	bl	FlashReadRawPage
	ldr	r1, [r6, #4]
	ldrb	r2, [r7, #-2743]	@ zero_extendqisi2
	mov	ip, r0
	ldr	r0, .L1292+16
	mov	r3, ip
	str	ip, [sp, #32]
	bl	printk
	ldr	ip, [sp, #32]
	cmn	ip, #1
	bne	.L1259
	ldrb	r5, [r7, #-2744]	@ zero_extendqisi2
	cmp	r5, #0
	beq	.L1240
	ldr	r3, [sp, #12]
	mov	r0, r4
	cmp	r3, #0
	beq	.L1242
	bl	flash_enter_slc_mode
	b	.L1243
.L1242:
	bl	flash_exit_slc_mode
.L1243:
	ldr	r3, .L1292+8
	mov	r0, r4
	ldr	r1, [sp, #44]
	ldr	r2, [r6, #8]
	ldr	ip, [r3, #-1796]
	ldr	r3, [r6, #12]
	blx	ip
	mov	ip, r0
	b	.L1259
.L1291:
	mov	r0, r4
	ldr	r1, [sp, #44]
	ldr	r2, [r6, #8]
	ldr	r3, [r6, #12]
	bl	FlashReadRawPage
	mov	ip, r0
	b	.L1240
.L1257:
	mov	r5, #0
.L1239:
	ldrb	r3, [r7, #-2743]	@ zero_extendqisi2
	add	r3, r3, r3, asl #1
	cmp	ip, r3, asr #2
	bls	.L1240
	ldr	r3, [r7, #-1796]
	cmp	r3, #0
	moveq	ip, #256
	b	.L1240
.L1259:
	mov	r5, #0
.L1240:
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
	bne	.L1247
	ldr	r1, [r6, #4]
	ldr	r0, .L1292+20
	ldrb	r2, [r7, #-2743]	@ zero_extendqisi2
	bl	printk
	ldr	r1, [r6, #12]
	cmp	r1, #0
	beq	.L1247
	mov	r2, #4
	ldr	r0, .L1292+24
	mov	r3, r2
	bl	rknand_print_hex
.L1247:
	cmp	r5, #0
	beq	.L1249
	ldrb	r3, [r7, #-2743]	@ zero_extendqisi2
	add	r3, r3, r3, asl #1
	cmp	r8, r3, asr #2
	bls	.L1250
	ldr	r3, [r7, #-1796]
	cmp	r3, #0
	moveq	r8, #256
.L1250:
	ldr	r3, [sp, #8]
	cmp	r8, #256
	cmnne	r8, #1
	add	r3, r3, #36
	movne	r2, #0
	streq	r8, [r10, r3]
	strne	r2, [r10, r3]
.L1249:
	ldr	r3, [sp, #12]
	add	fp, fp, r5
	cmp	r3, #0
	beq	.L1221
	ldrb	r3, [r7, #-2744]	@ zero_extendqisi2
	cmp	r3, #0
	beq	.L1221
	mov	r0, r4
	bl	flash_exit_slc_mode
.L1221:
	add	fp, fp, #1
	b	.L1218
.L1290:
	mov	r0, #0
.L1287:
	add	sp, sp, #52
	@ sp needed
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L1293:
	.align	2
.L1292:
	.word	.LANCHOR1
	.word	.LANCHOR0
	.word	.LANCHOR2
	.word	.LANCHOR2-2728
	.word	.LC19
	.word	.LC16
	.word	.LC18
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
	ldr	r5, .L1307
	.pad #52
	sub	sp, sp, #52
	mov	r2, #16
	ldr	r9, .L1307+4
	add	r0, r5, #1016
	ldr	fp, .L1307+8
	ldrh	r7, [r5, #12]
	mvn	r10, #0
	ldrh	r4, [r5, #14]
	bl	ftl_memset
	ldr	r3, [r9, #-1772]
	mov	r5, #0
	mov	r8, r5
	smulbb	r7, r7, r4
	uxth	r6, r7
	str	r5, [sp, #20]
	str	r3, [sp, #24]
.L1295:
	ldrb	r3, [fp, #3152]	@ zero_extendqisi2
	uxtb	r7, r5
	cmp	r3, r7
	bls	.L1306
	mul	ip, r6, r7
	sub	r4, r6, #1
	sub	r3, r6, #12
	uxth	r4, r4
.L1296:
	cmp	r4, r3
	ble	.L1298
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
	beq	.L1297
	ldr	r2, [r9, #-1772]
	ldrh	r1, [r2]
	movw	r2, #61664
	cmp	r1, r2
	bne	.L1297
	mov	r1, r7
	ldr	r0, .L1307+12
	mov	r2, r4
	mov	r7, r7, asl #1
	bl	printk
	ldr	r3, .L1307+16
	add	r8, r8, #1
	strh	r4, [r3, r7]	@ movhi
	uxth	r8, r8
	b	.L1298
.L1297:
	sub	r4, r4, #1
	uxth	r4, r4
	b	.L1296
.L1298:
	ldr	r3, .L1307+8
	add	r5, r5, #1
	ldrb	r3, [r3, #3152]	@ zero_extendqisi2
	cmp	r3, r8
	moveq	r10, #0
	b	.L1295
.L1306:
	mov	r0, r10
	add	sp, sp, #52
	@ sp needed
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L1308:
	.align	2
.L1307:
	.word	.LANCHOR2-2772
	.word	.LANCHOR2
	.word	.LANCHOR0
	.word	.LC20
	.word	.LANCHOR2-1756
	.fnend
	.size	FlashLoadFactorBbt, .-FlashLoadFactorBbt
	.align	2
	.global	FlashReadFacBbtData
	.type	FlashReadFacBbtData, %function
FlashReadFacBbtData:
	.fnstart
	@ args = 0, pretend = 0, frame = 40
	@ frame_needed = 0, uses_anonymous_args = 0
	ldr	r3, .L1324
	stmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, lr}
	.save {r4, r5, r6, r7, r8, r9, r10, lr}
	mov	r8, r2
	ldrh	r4, [r3, #14]
	.pad #40
	sub	sp, sp, #40
	ldrh	r2, [r3, #12]
	mov	r5, r0
	ldr	r9, .L1324+4
	mov	r7, r1
	smulbb	r4, r2, r4
	ldr	r2, [r9, #-1868]
	str	r2, [sp, #12]
	ldr	r2, [r9, #-1772]
	uxth	r3, r4
	sub	r6, r3, #1
	sub	r4, r3, #16
	mul	r10, r3, r1
	uxth	r6, r6
	str	r2, [sp, #16]
.L1310:
	cmp	r6, r4
	ble	.L1323
	mov	r1, #1
	add	r3, r6, r10
	add	r0, sp, #4
	mov	r2, r1
	mov	r3, r3, asl #10
	str	r3, [sp, #8]
	bl	FlashReadPages
	ldr	r3, [sp, #4]
	cmn	r3, #1
	beq	.L1311
	ldr	r3, [r9, #-1772]
	ldrh	r2, [r3]
	movw	r3, #61664
	cmp	r2, r3
	bne	.L1311
	cmp	r5, #0
	moveq	r0, r5
	beq	.L1312
	cmp	r7, #0
	ldreq	ip, .L1324+4
	moveq	lr, #1
	beq	.L1313
.L1315:
	ldr	r1, [r9, #-1868]
	mov	r2, r8
	mov	r0, r5
	bl	ftl_memcpy
	mov	r2, #4
	ldr	r0, .L1324+8
	mov	r1, r5
	mov	r3, r2
	bl	rknand_print_hex
	mov	r0, #0
	b	.L1312
.L1313:
	ldr	r3, [r9, #-1784]
	uxth	r4, r7
	add	r7, r7, #1
	cmp	r4, r3
	bcs	.L1315
	ldr	r1, [ip, #-1868]
	mov	r0, r4, lsr #5
	and	r3, r4, #31
	ldr	r2, [r1, r0, asl #2]
	orr	r3, r2, lr, asl r3
	str	r3, [r1, r0, asl #2]
	b	.L1313
.L1311:
	sub	r6, r6, #1
	uxth	r6, r6
	b	.L1310
.L1323:
	mvn	r0, #0
.L1312:
	add	sp, sp, #40
	@ sp needed
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, pc}
.L1325:
	.align	2
.L1324:
	.word	.LANCHOR2-2772
	.word	.LANCHOR2
	.word	.LC21
	.fnend
	.size	FlashReadFacBbtData, .-FlashReadFacBbtData
	.align	2
	.global	FlashGetBadBlockList
	.type	FlashGetBadBlockList, %function
FlashGetBadBlockList:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	ldr	r3, .L1337
	stmfd	sp!, {r4, r5, r6, r7, r8, lr}
	.save {r4, r5, r6, r7, r8, lr}
	mov	r5, r0
	ldr	r3, [r3, #44]
	ldr	r6, .L1337+4
	ldrb	r4, [r3, #13]	@ zero_extendqisi2
	ldrh	r3, [r3, #14]
	ldr	r0, [r6, #-1776]
	smulbb	r4, r4, r3
	uxth	r4, r4
	add	r2, r4, #7
	mov	r2, r2, asr #3
	bl	FlashReadFacBbtData
	cmn	r0, #1
	bne	.L1327
.L1331:
	mov	r3, #0
	b	.L1328
.L1327:
	mov	lr, r4, lsr #4
	mov	ip, #0
	sub	r4, r4, #1
	mov	r3, ip
	mov	r7, #1
.L1329:
	uxth	r0, ip
	cmp	r0, r4
	bge	.L1328
	ldr	r8, [r6, #-1776]
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
	bcc	.L1329
	b	.L1331
.L1328:
	mov	r3, r3, asl #1
	mvn	r2, #0
	mov	r0, #0
	strh	r2, [r5, r3]	@ movhi
	ldmfd	sp!, {r4, r5, r6, r7, r8, pc}
.L1338:
	.align	2
.L1337:
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
	ldr	r3, .L1369
	stmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.save {r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.pad #68
	sub	sp, sp, #68
	ldr	r8, .L1369+4
	mov	r6, r1
	ldrb	r3, [r3, #265]	@ zero_extendqisi2
	mov	r9, r2
	mov	r4, r0
	mov	r10, r0
	mov	r7, #0
	mov	fp, r8
	str	r3, [sp, #12]
.L1340:
	cmp	r7, r6
	beq	.L1367
	rsb	r3, r7, r6
	add	r2, sp, #20
	mov	r0, r10
	mov	r1, r9
	uxtb	r3, r3
	str	r3, [sp]
	add	r3, sp, #24
	bl	LogAddr2PhyAddr
	ldrb	r2, [r8, #3152]	@ zero_extendqisi2
	ldr	r3, [sp, #24]
	cmp	r3, r2
	mvncs	r3, #0
	strcs	r3, [r10]
	bcs	.L1342
	add	r3, r8, r3
	ldrb	r5, [r3, #3156]	@ zero_extendqisi2
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
	ldr	r2, [r10, #12]
	mov	r1, #1
	cmp	r3, #0
	mov	r0, r5
	addne	r3, r3, #2048
	cmp	r2, #0
	addne	r2, r2, #8
	str	r2, [sp]
	ldr	r2, [sp, #12]
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
.L1342:
	add	r7, r7, #1
	add	r10, r10, #36
	b	.L1340
.L1367:
	ldr	r5, .L1369+8
	mov	r7, #0
	mov	r8, r5
.L1347:
	cmp	r7, r6
	beq	.L1368
	ldr	r3, [r4]
	cmn	r3, #1
	bne	.L1348
	ldr	r1, [r4, #4]
	ldr	r0, .L1369+12
	bl	printk
	b	.L1349
.L1348:
	rsb	r3, r7, r6
	mov	r1, r9
	add	r2, sp, #20
	mov	r0, r4
	uxtb	r3, r3
	str	r3, [sp]
	add	r3, sp, #24
	bl	LogAddr2PhyAddr
	ldr	r2, [r5, #-1768]
	mov	r3, #0
	mov	lr, r4
	str	r3, [r2]
	ldr	r2, [r5, #-1764]
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
	ldr	r3, [r5, #-1768]
	str	r3, [sp, #36]
	ldr	r3, [r5, #-1764]
	str	r3, [sp, #40]
	bl	FlashReadPages
	ldr	r10, [sp, #28]
	cmn	r10, #1
	bne	.L1350
	ldr	r0, .L1369+16
	ldr	r1, [r4, #4]
	bl	printk
	str	r10, [r4]
.L1350:
	ldr	r10, [sp, #28]
	cmp	r10, #256
	bne	.L1351
	ldr	r0, .L1369+20
	ldr	r1, [r4, #4]
	bl	printk
	str	r10, [r4]
.L1351:
	ldr	r3, [r4, #12]
	cmp	r3, #0
	beq	.L1352
	ldr	r2, [r3]
	ldr	r3, [r8, #-1764]
	ldr	r3, [r3]
	cmp	r2, r3
	beq	.L1352
	ldr	r0, .L1369+24
	ldr	r1, [r4, #4]
	bl	printk
	mvn	r3, #0
	str	r3, [r4]
.L1352:
	ldr	r3, [r4, #8]
	cmp	r3, #0
	beq	.L1349
	ldr	r2, [r3]
	ldr	r3, [r8, #-1768]
	ldr	r3, [r3]
	cmp	r2, r3
	beq	.L1349
	ldr	r0, .L1369+28
	ldr	r1, [r4, #4]
	bl	printk
	mvn	r3, #0
	str	r3, [r4]
.L1349:
	add	r7, r7, #1
	add	r4, r4, #36
	b	.L1347
.L1368:
	mov	r0, #0
	add	sp, sp, #68
	@ sp needed
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L1370:
	.align	2
.L1369:
	.word	.LANCHOR1
	.word	.LANCHOR0
	.word	.LANCHOR2
	.word	.LC22
	.word	.LC23
	.word	.LC24
	.word	.LC25
	.word	.LC26
	.fnend
	.size	FlashProgSlc2KPages, .-FlashProgSlc2KPages
	.align	2
	.global	FlashProgPages
	.type	FlashProgPages, %function
FlashProgPages:
	.fnstart
	@ args = 0, pretend = 0, frame = 64
	@ frame_needed = 0, uses_anonymous_args = 0
	stmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.save {r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.pad #76
	sub	sp, sp, #76
	ldr	r6, .L1427
	mov	r4, r0
	str	r1, [sp, #8]
	mov	r9, r2
	str	r3, [sp, #20]
	ldr	ip, [r6, #44]
	ldrb	r8, [r6]	@ zero_extendqisi2
	ldrb	ip, [ip, #19]	@ zero_extendqisi2
	cmp	r8, #0
	str	ip, [sp, #16]
	ldr	ip, .L1427+4
	ldrb	ip, [ip, #265]	@ zero_extendqisi2
	str	ip, [sp, #12]
	beq	.L1372
	bl	FlashProgSlc2KPages
	b	.L1373
.L1385:
	mov	r7, #36
	ldr	r3, [sp, #8]
	mul	r7, r7, r8
	rsb	r3, r8, r3
	mov	r1, r9
	add	r2, sp, #28
	uxtb	r3, r3
	str	r3, [sp]
	add	fp, r4, r7
	add	r3, sp, #32
	mov	r0, fp
	bl	LogAddr2PhyAddr
	ldrb	r3, [r6, #3152]	@ zero_extendqisi2
	mov	r10, r0
	ldr	r0, [sp, #32]
	cmp	r0, r3
	mvncs	r3, #0
	strcs	r3, [r4, r7]
	bcc	.L1424
.L1375:
	add	r8, r8, #1
.L1372:
	ldr	r3, [sp, #8]
	ldr	r5, .L1427
	cmp	r8, r3
	bcc	.L1385
	b	.L1425
.L1424:
	ldr	r3, .L1427+8
	ldrb	r3, [r3, #-1874]	@ zero_extendqisi2
	cmp	r3, #0
	add	r3, r6, r0, asl #4
	ldr	r3, [r3, #3204]
	moveq	r10, #0
	cmp	r3, #0
	beq	.L1377
	uxtb	r0, r0
	bl	FlashWaitCmdDone
.L1377:
	ldr	r3, [sp, #32]
	mov	r1, #0
	cmp	r10, #0
	add	r2, r6, r3, asl #4
	str	r1, [r2, #3208]
	ldr	r1, [sp, #28]
	str	fp, [r2, #3204]
	str	r1, [r2, #3200]
	addne	r1, r7, #36
	addne	r1, r4, r1
	strne	r1, [r2, #3208]
	add	r2, r6, r3
	add	r3, r6, r3, asl #4
	ldrb	r5, [r2, #3156]	@ zero_extendqisi2
	strb	r5, [r3, #3196]
	mov	r0, r5
	ldrb	r3, [r6, #3152]	@ zero_extendqisi2
	cmp	r3, #1
	bne	.L1379
	bl	NandcWaitFlashReady
	b	.L1380
.L1379:
	bl	NandcFlashCs
	mov	r0, r5
	ldr	r3, [sp, #32]
	ldr	r1, [sp, #28]
	add	r3, r6, r3, asl #2
	ldr	r2, [r3, #3164]
	adds	r2, r2, #0
	movne	r2, #1
	bl	FlashWaitReadyEN
	mov	r0, r5
	bl	NandcFlashDeCs
.L1380:
	ldr	r3, [sp, #16]
	sub	r3, r3, #1
	cmp	r3, #7
	bhi	.L1381
	ldr	r3, .L1427+8
	add	r3, r3, r5
	ldrb	r3, [r3, #-1880]	@ zero_extendqisi2
	cmp	r3, #0
	beq	.L1381
	ldr	r3, .L1427+8
	mov	r0, r5
	ldr	r2, .L1427+12
	ldrb	r1, [r3, #-2731]	@ zero_extendqisi2
	mov	r3, #0
	bl	HynixSetRRPara
.L1381:
	mov	r0, r5
	bl	NandcFlashCs
	cmp	r9, #1
	mov	r0, r5
	bne	.L1382
	ldr	r3, .L1427+8
	ldrb	r3, [r3, #-2744]	@ zero_extendqisi2
	cmp	r3, #0
	beq	.L1382
	bl	flash_enter_slc_mode
	b	.L1383
.L1382:
	bl	flash_exit_slc_mode
.L1383:
	mov	r0, r5
	ldr	r1, [sp, #28]
	bl	FlashProgFirstCmd
	ldr	r3, [fp, #12]
	mov	r0, r5
	mov	r1, #1
	str	r3, [sp]
	ldr	r2, [sp, #12]
	ldr	r3, [fp, #8]
	bl	NandcXferData
	cmp	r10, #0
	beq	.L1384
	mov	r0, r5
	ldr	r1, [sp, #28]
	bl	FlashProgDpFirstCmd
	mov	r0, r5
	add	r7, r7, #36
	add	r7, r4, r7
	ldr	r3, [sp, #32]
	ldr	r1, [sp, #28]
	add	r3, r6, r3, asl #2
	ldr	r2, [r3, #3164]
	adds	r2, r2, #0
	movne	r2, #1
	bl	FlashWaitReadyEN
	ldr	r1, [r6, #4]
	mov	r0, r5
	ldr	r3, [sp, #28]
	add	r1, r1, r3
	bl	FlashProgDpSecondCmd
	ldr	r3, [r7, #12]
	mov	r0, r5
	mov	r1, #1
	str	r3, [sp]
	ldr	r2, [sp, #12]
	ldr	r3, [r7, #8]
	bl	NandcXferData
.L1384:
	mov	r0, r5
	ldr	r1, [sp, #28]
	bl	FlashProgSecondCmd
	mov	r0, r5
	bl	NandcFlashDeCs
	add	r8, r8, r10
	b	.L1375
.L1425:
	ldr	r7, .L1427+8
	mov	r6, #0
	ldr	r8, .L1427+16
.L1386:
	ldrb	r3, [r5, #3152]	@ zero_extendqisi2
	cmp	r6, r3
	bcs	.L1426
	uxtb	r0, r6
	bl	FlashWaitCmdDone
	cmp	r9, #1
	bne	.L1387
	ldrb	r3, [r7, #-2744]	@ zero_extendqisi2
	cmp	r3, #0
	beq	.L1387
	ldrb	r0, [r8, r6, asl #4]	@ zero_extendqisi2
	bl	flash_exit_slc_mode
.L1387:
	add	r6, r6, #1
	b	.L1386
.L1426:
	ldr	r3, [sp, #20]
	cmp	r3, #0
	bne	.L1389
.L1397:
	mov	r0, #0
	b	.L1373
.L1389:
	ldr	r5, .L1427+8
	mov	r6, #0
	mov	r7, r5
.L1390:
	ldr	r3, [sp, #8]
	cmp	r6, r3
	beq	.L1397
	ldr	r3, [r4]
	cmn	r3, #1
	bne	.L1391
	ldr	r1, [r4, #4]
	ldr	r0, .L1427+20
	bl	printk
	b	.L1392
.L1391:
	ldr	r3, [sp, #8]
	mov	r1, r9
	add	r2, sp, #28
	mov	r0, r4
	rsb	r3, r6, r3
	uxtb	r3, r3
	str	r3, [sp]
	add	r3, sp, #32
	bl	LogAddr2PhyAddr
	ldr	r2, [r5, #-1768]
	mov	r3, #0
	mov	lr, r4
	str	r3, [r2]
	ldr	r2, [r5, #-1764]
	str	r3, [r2]
	ldmia	lr!, {r0, r1, r2, r3}
	add	ip, sp, #36
	stmia	ip!, {r0, r1, r2, r3}
	ldmia	lr!, {r0, r1, r2, r3}
	stmia	ip!, {r0, r1, r2, r3}
	add	r0, sp, #36
	ldr	r3, [lr]
	mov	r1, #1
	mov	r2, r9
	str	r3, [ip]
	ldr	r3, [r5, #-1768]
	str	r3, [sp, #44]
	ldr	r3, [r5, #-1764]
	str	r3, [sp, #48]
	bl	FlashReadPages
	ldr	r8, [sp, #36]
	cmn	r8, #1
	bne	.L1393
	ldr	r0, .L1427+24
	ldr	r1, [r4, #4]
	bl	printk
	str	r8, [r4]
.L1393:
	ldr	r3, [r4, #12]
	cmp	r3, #0
	beq	.L1394
	ldr	r2, [r3]
	ldr	r3, [r7, #-1764]
	ldr	r3, [r3]
	cmp	r2, r3
	beq	.L1394
	ldr	r0, .L1427+28
	ldr	r1, [r4, #4]
	bl	printk
	mvn	r3, #0
	str	r3, [r4]
.L1394:
	ldr	r3, [r4, #8]
	cmp	r3, #0
	beq	.L1392
	ldr	r2, [r3]
	ldr	r3, [r7, #-1768]
	ldr	r3, [r3]
	cmp	r2, r3
	beq	.L1392
	ldr	r0, .L1427+32
	ldr	r1, [r4, #4]
	bl	printk
	mvn	r3, #0
	str	r3, [r4]
.L1392:
	add	r6, r6, #1
	add	r4, r4, #36
	b	.L1390
.L1373:
	add	sp, sp, #76
	@ sp needed
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L1428:
	.align	2
.L1427:
	.word	.LANCHOR0
	.word	.LANCHOR1
	.word	.LANCHOR2
	.word	.LANCHOR2-2728
	.word	.LANCHOR0+3196
	.word	.LC22
	.word	.LC23
	.word	.LC25
	.word	.LC26
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
	ldr	r5, .L1433
	ldr	r3, [r5, #-1784]
	cmp	r0, r3
	movcc	r4, #0
	bcc	.L1430
	ldr	r3, [r5, #-1776]
	mov	r4, r0
	mov	r1, #165
	add	r0, sp, #40
	mov	r2, #32
	str	r0, [sp, #16]
	str	r3, [sp, #12]
	bl	ftl_memset
	mov	r1, #90
	mov	r2, #8
	ldr	r0, [r5, #-1776]
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
.L1430:
	mov	r0, r4
	add	sp, sp, #108
	@ sp needed
	ldmfd	sp!, {r4, r5, pc}
.L1434:
	.align	2
.L1433:
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
	ldr	r4, .L1491
	mov	r1, #1
	ldr	r5, .L1491+4
	ldr	r0, .L1491+8
	ldr	r3, [r4, #-1772]
	ldrh	r8, [r5, #12]
	ldr	r7, .L1491+12
	str	r3, [sp, #20]
	ldrh	r3, [r5, #14]
	smulbb	r8, r8, r3
	ldr	r3, .L1491+12
	ldr	r2, [r3, #44]
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
	ldr	r0, [r4, #-1772]
	mov	r1, #0
	mov	r2, #4096
	bl	ftl_memset
	ldr	r4, .L1491
	mov	r3, r8, lsr #4
	str	r3, [sp, #28]
	mov	r3, #0
	str	r3, [sp, #8]
.L1437:
	ldrb	r6, [sp, #8]	@ zero_extendqisi2
	ldrb	r3, [r7, #3152]	@ zero_extendqisi2
	cmp	r3, r6
	bls	.L1487
	ldr	r3, .L1491+16
	mov	r2, r6, asl #1
	ldrh	r5, [r3, r2]
	cmp	r5, #0
	bne	.L1467
	sub	r3, r3, #1016
	ldr	r0, [r4, #-1868]
	mov	r1, r5
	mov	r9, r5
	ldrh	r2, [r3, #20]
	mov	r2, r2, asl #9
	bl	ftl_memset
	add	r3, r7, r6
	ldrb	r10, [r3, #3156]	@ zero_extendqisi2
	mov	r3, r6, asl #2
	add	fp, r7, r3
	str	r5, [sp, #4]
	str	r3, [sp, #32]
.L1439:
	ldrh	r3, [sp, #4]
	cmp	r3, r8
	str	r3, [sp, #12]
	bcs	.L1449
	mvn	r3, #0
	strb	r3, [sp, #42]
	strb	r3, [sp, #43]
	ldr	r3, [sp, #24]
	tst	r3, #1
	beq	.L1441
	ldr	r3, [fp, #3164]
	add	r2, sp, #42
	mov	r0, r10
	add	r3, r5, r3
	str	r3, [sp, #36]
	mov	r1, r3
	bl	FlashReadSpare
	ldrb	r2, [r7]	@ zero_extendqisi2
	cmp	r2, #1
	ldr	r3, [sp, #36]
	bne	.L1441
	ldr	r1, [r7, #4]
	add	r2, sp, #43
	mov	r0, r10
	add	r1, r3, r1
	bl	FlashReadSpare
	ldrb	r3, [sp, #42]	@ zero_extendqisi2
	ldrb	r2, [sp, #43]	@ zero_extendqisi2
	and	r3, r3, r2
	strb	r3, [sp, #42]
.L1441:
	ldr	r3, [sp, #24]
	tst	r3, #2
	beq	.L1443
	ldr	r3, [r7, #44]
	mov	r0, r10
	add	r2, sp, #43
	ldrh	r1, [r3, #10]
	ldr	r3, [fp, #3164]
	sub	r1, r1, #1
	add	r1, r1, r3
	add	r1, r1, r5
	bl	FlashReadSpare
.L1443:
	ldr	r2, [r7, #44]
	ldrb	r3, [r2, #7]	@ zero_extendqisi2
	cmp	r3, #1
	cmpne	r3, #8
	ldrb	r3, [sp, #42]	@ zero_extendqisi2
	beq	.L1444
	ldrb	r2, [r2, #18]	@ zero_extendqisi2
	cmp	r2, #12
	bne	.L1445
.L1444:
	cmp	r3, #0
	ldrneb	r0, [sp, #43]	@ zero_extendqisi2
	clzne	r0, r0
	movne	r0, r0, lsr #5
	bne	.L1446
	b	.L1466
.L1445:
	cmp	r3, #255
	bne	.L1466
	ldrb	r0, [sp, #43]	@ zero_extendqisi2
	subs	r0, r0, #255
	movne	r0, #1
	b	.L1446
.L1466:
	mov	r0, #1
.L1446:
	ldr	r3, [sp, #24]
	tst	r3, #4
	beq	.L1447
	ldr	r3, .L1491+12
	mov	r0, r10
	ldr	r2, [sp, #32]
	add	r3, r3, r2
	ldr	r1, [r3, #3164]
	add	r1, r5, r1
	bl	SandiskProgTestBadBlock
.L1447:
	cmp	r0, #0
	beq	.L1448
	mov	r1, r6
	ldr	r2, [sp, #4]
	ldr	r0, .L1491+20
	add	r9, r9, #1
	bl	printk
	ldr	r1, [r4, #-1868]
	mov	ip, #1
	uxth	r9, r9
	ldr	r3, [sp, #12]
	mov	r0, r3, lsr #5
	and	r3, r3, #31
	ldr	r2, [r1, r0, asl #2]
	orr	r3, r2, ip, asl r3
	ldr	r2, [sp, #28]
	str	r3, [r1, r0, asl #2]
	ldrb	r3, [r7, #3152]	@ zero_extendqisi2
	mul	r3, r3, r2
	cmp	r9, r3
	bgt	.L1449
.L1448:
	ldr	r3, [sp, #4]
	add	r3, r3, #1
	str	r3, [sp, #4]
	ldr	r3, [sp, #16]
	add	r5, r5, r3
	b	.L1439
.L1449:
	mov	r2, r9
	ldr	r0, .L1491+24
	mov	r1, r6
	bl	printk
	ldrb	r3, [r7, #3152]	@ zero_extendqisi2
	ldr	r2, [sp, #28]
	mul	r3, r3, r2
	cmp	r9, r3
	blt	.L1451
	ldr	r3, .L1491+4
	mov	r1, #0
	ldr	r0, [r4, #-1868]
	ldrh	r2, [r3, #20]
	mov	r2, r2, asl #9
	bl	ftl_memset
.L1451:
	cmp	r6, #0
	bne	.L1453
	ldr	r3, [r4, #-1784]
	mov	r5, r6
	mov	r9, #1
	uxth	r10, r3
.L1454:
	ldr	r3, .L1491+12
	ldrb	r3, [r3, #1]	@ zero_extendqisi2
	cmp	r3, r10
	bls	.L1488
	mov	r0, r10
	bl	FlashTestBlk
	cmp	r0, #0
	beq	.L1455
	mov	r1, r10
	ldr	r0, .L1491+28
	bl	printk
	ldr	r1, [r4, #-1868]
	mov	r0, r10, lsr #5
	add	r5, r5, #1
	and	r3, r10, #31
	ldr	r2, [r1, r0, asl #2]
	uxth	r5, r5
	orr	r3, r2, r9, asl r3
	str	r3, [r1, r0, asl #2]
.L1455:
	add	r10, r10, #1
	uxth	r10, r10
	b	.L1454
.L1488:
	sub	fp, r8, #1
	sub	r9, r8, #50
	mov	r10, #1
	uxth	fp, fp
.L1457:
	cmp	fp, r9
	ble	.L1489
	mov	r0, fp
	bl	FlashTestBlk
	cmp	r0, #0
	beq	.L1458
	mov	r1, fp
	ldr	r0, .L1491+28
	bl	printk
	ldr	r1, [r4, #-1868]
	mov	r0, fp, lsr #5
	and	r3, fp, #31
	ldr	r2, [r1, r0, asl #2]
	orr	r3, r2, r10, asl r3
	str	r3, [r1, r0, asl #2]
.L1458:
	sub	fp, fp, #1
	uxth	fp, fp
	b	.L1457
.L1489:
	ldr	r3, .L1491+12
	ldr	r2, [r4, #-1784]
	ldrb	r3, [r3, #1]	@ zero_extendqisi2
	rsb	r3, r2, r3
	cmp	r5, r3
	bcc	.L1453
	ldr	r3, .L1491+4
	mov	r1, #0
	ldr	r0, [r4, #-1868]
	ldrh	r2, [r3, #20]
	mov	r2, r2, asl #9
	bl	ftl_memset
.L1453:
	ldrb	r5, [sp, #8]	@ zero_extendqisi2
	sub	r10, r8, #1
	ldr	r9, .L1491+16
	uxth	r10, r10
	mul	r5, r8, r5
	add	r9, r9, r6, asl #1
.L1461:
	mov	r1, r6
	ldr	r0, .L1491+32
	mov	r2, r10
	bl	printk
	ldr	r1, [r4, #-1868]
.L1462:
	mov	r2, r10, lsr #5
	and	r3, r10, #31
	ldr	r2, [r1, r2, asl #2]
	mov	r3, r2, lsr r3
	ands	r3, r3, #1
	subne	r10, r10, #1
	uxthne	r10, r10
	bne	.L1462
.L1490:
	ldr	r1, [sp, #20]
	add	r0, sp, #44
	ldr	r2, .L1491+36
	strh	r10, [r9]	@ movhi
	strh	r10, [r1, #2]	@ movhi
	strh	r2, [r1]	@ movhi
	strh	r3, [r1, #8]	@ movhi
	mov	r1, #1
	ldr	r3, [r4, #-1868]
	mov	r2, r1
	str	r3, [sp, #52]
	ldr	r3, [r4, #-1772]
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
	bne	.L1461
.L1467:
	ldr	r3, [sp, #8]
	add	r3, r3, #1
	str	r3, [sp, #8]
	b	.L1437
.L1487:
	add	sp, sp, #84
	@ sp needed
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L1492:
	.align	2
.L1491:
	.word	.LANCHOR2
	.word	.LANCHOR2-2772
	.word	.LC27
	.word	.LANCHOR0
	.word	.LANCHOR2-1756
	.word	.LC28
	.word	.LC29
	.word	.LC30
	.word	.LC31
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
.L1494:
	cmp	r2, r0
	uxth	r3, r1
	add	r1, r1, #1
	movls	r2, r2, asl #1
	bls	.L1494
.L1496:
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
	ldr	r3, .L1500
	cmp	r0, #23
	sub	ip, r3, #1728
	movls	r0, #24
	sub	r1, r3, #1712
	str	r0, [r3, #-1740]
	ldrh	r2, [ip, #-8]
	ldrh	r1, [r1, #-14]
	mul	r2, r2, r0
	rsb	r0, r0, r1
	ldr	r1, [r3, #-1720]
	strh	r0, [ip]	@ movhi
	mov	r0, #0
	str	r2, [r3, #-1732]
	rsb	r2, r2, r1
	str	r2, [r3, #-1724]
	bx	lr
.L1501:
	.align	2
.L1500:
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
	.pad #20
	sub	sp, sp, #20
	ldr	r4, .L1529
	mov	ip, r0
	ldrh	r8, [r0, #8]
	mov	r10, #0
	ldrh	r5, [r0, #10]
	add	r3, r4, #4
	ldrh	r7, [r0, #12]
	ldrh	r6, [r0, #14]
	strh	r8, [r4, #-4]	@ movhi
	strh	r5, [r4, #-2]	@ movhi
	strh	r7, [r4]	@ movhi
	strh	r6, [r4, #-14]	@ movhi
.L1503:
	strb	r10, [r10, r3]
	add	r10, r10, #1
	cmp	r10, #32
	bne	.L1503
	ldrh	r2, [ip, #14]
	ldrh	r3, [ip, #20]
	cmp	r3, r2, lsr #8
	bcs	.L1504
	sub	r3, r5, #1
	uxtb	r9, r7
	mul	r3, r7, r3
	mov	fp, r9, asl #1
	uxtb	fp, fp
	str	r3, [sp, #4]
	mov	r3, #0
.L1505:
	cmp	r3, r7
	bcs	.L1507
	ldr	r1, [sp, #4]
	uxtb	r2, r3
	ldr	r0, .L1529+4
	add	r1, r3, r1
	ldr	lr, .L1529+4
	add	r1, r0, r1
	str	r1, [sp, #8]
	mov	r1, #0
	rsb	r0, r7, r3
	mov	r10, r1
	add	r0, lr, r0
	str	r0, [sp, #12]
.L1508:
	cmp	r10, r5
	add	r1, r1, r7
	bcs	.L1528
	ldr	r0, [sp, #12]
	add	r10, r10, #1
	strb	r2, [r0, r1]
	add	r0, r2, r9
	add	r2, r2, fp
	mov	lr, r0
	ldr	r0, [sp, #8]
	uxtb	r2, r2
	strb	lr, [r0, r1]
	b	.L1508
.L1528:
	add	r3, r3, #1
	b	.L1505
.L1507:
	mov	r5, r5, asl #1
	mov	r6, r6, lsr #1
	strh	r5, [r4, #-2]	@ movhi
	strh	r6, [r4, #-14]	@ movhi
.L1504:
	ldr	r6, .L1529+8
	cmp	r8, #1
	mov	r3, #5
	ldrh	r9, [ip, #16]
	ldr	r5, .L1529+12
	strh	r3, [r6, #-12]	@ movhi
	mov	r3, #0
	streqh	r8, [r6, #-12]	@ movhi
	sub	r10, r5, #1728
	ldrh	r8, [r4, #-2]
	strh	r3, [r6, #-10]	@ movhi
	mov	r3, #4352
	strh	r3, [r6, #-8]	@ movhi
	ldr	r3, .L1529+16
	smulbb	r8, r8, r7
	ldrh	r4, [r4, #-14]
	ldrb	fp, [r3]	@ zero_extendqisi2
	ldrh	r1, [ip, #18]
	cmp	fp, #0
	strh	r9, [r6, #-4]	@ movhi
	smulbb	r7, r4, r7
	ldrne	r3, .L1529+8
	uxth	r8, r8
	movne	r2, #384
	strh	r1, [r6, #-2]	@ movhi
	strneh	r2, [r3, #-8]	@ movhi
	smulbb	r3, r8, r9
	ldrh	r2, [ip, #20]
	strh	r7, [r6, #-6]	@ movhi
	sub	r7, r5, #1648
	strh	r8, [r10, #-8]	@ movhi
	mov	r0, r2
	strh	r2, [r7, #-14]	@ movhi
	str	r1, [sp, #12]
	strh	r3, [r6]	@ movhi
	str	ip, [sp, #8]
	str	r2, [sp, #4]
	bl	Ftl_log2
	cmp	r4, #1024
	ldr	r2, [sp, #4]
	mov	r3, r0
	strh	r0, [r7, #-12]	@ movhi
	ldr	ip, [sp, #8]
	mov	r0, r2, asl #9
	ldr	r1, [sp, #12]
	str	r3, [sp, #4]
	uxth	r0, r0
	strh	r0, [r7, #-10]	@ movhi
	mul	r1, r1, r2
	mov	r0, r0, lsr #8
	strh	r0, [r7, #-8]	@ movhi
	ldrh	r0, [ip, #26]
	subhi	ip, r5, #1664
	strh	r0, [r7, #-6]	@ movhi
	mul	r0, r4, r8
	str	r0, [r5, #-1720]
	uxtbhi	r0, r4
	strhih	r0, [ip, #-10]	@ movhi
	ldrh	r0, [r6, #-10]
	rsb	r0, r0, r4
	mov	r4, r4, asl #6
	mul	r0, r0, r8
	mul	r0, r2, r0
	mul	r9, r9, r0
	ldrh	r0, [r6, #-8]
	mov	r0, r0, asl #3
	mov	r9, r9, asr #11
	str	r9, [r5, #-1652]
	bl	__aeabi_idiv
	mov	r1, r8
	uxth	r0, r0
	ldr	r3, [sp, #4]
	cmp	r0, #4
	strhih	r0, [r7]	@ movhi
	movls	r2, #4
	strlsh	r2, [r7]	@ movhi
	cmp	fp, #0
	ldr	fp, .L1529+12
	ldrh	r0, [r7]
	movne	r2, #640
	strneh	r2, [r6, #-8]	@ movhi
	ldrh	r2, [r6, #-8]
	sub	r9, fp, #1632
	mov	r2, r2, asr r3
	add	r3, r3, #9
	mov	r4, r4, asr r3
	strh	r4, [r9, #-12]	@ movhi
	add	r2, r2, #2
	strh	r2, [r9, #-14]	@ movhi
	uxth	r4, r4
	mul	r3, r8, r4
	add	r4, r4, #8
	str	r3, [r5, #-1640]
	bl	__aeabi_uidiv
	cmp	r8, #1
	uxtah	r0, r4, r0
	strne	r0, [fp, #-1740]
	addeq	r4, r0, #4
	streq	r4, [fp, #-1740]
	ldr	r3, [r5, #-1740]
	ldr	r4, .L1529+12
	uxth	r0, r3
	sub	fp, r4, #1648
	bl	FtlSysBlkNumInit
	ldr	r3, [r5, #-1740]
	ldr	r2, [r5, #-1724]
	mov	r0, #2048
	ldrh	r8, [r7, #-12]
	str	r3, [r5, #-1636]
	ldrh	r3, [r6, #-4]
	mov	r2, r2, asl #2
	add	r8, r8, #9
	ldrh	r6, [r7, #-14]
	mul	r3, r3, r2
	mov	r1, r6
	mov	r8, r3, lsr r8
	add	r8, r8, #2
	uxth	r8, r8
	strh	r8, [r9]	@ movhi
	bl	__aeabi_idiv
	mov	r3, #0
	ldrb	r1, [r5, #-2744]	@ zero_extendqisi2
	sub	r9, r4, #1616
	str	r3, [r5, #-2740]
	ldrh	r3, [r7]
	cmp	r1, #0
	add	r2, r3, #3
	strh	r2, [r7]	@ movhi
	ldr	r7, [r5, #-1640]
	add	r2, r7, #3
	str	r2, [r5, #-1640]
	strh	r0, [r9, #-14]	@ movhi
	addne	r3, r3, #4
	addne	r7, r7, #5
	strneh	r3, [fp]	@ movhi
	strne	r7, [r4, #-1640]
	bne	.L1518
.L1517:
	cmp	r2, #7
	movls	r3, #8
	strls	r3, [r4, #-1640]
.L1518:
	ldrh	r2, [r10]
	mov	r3, #0
	strh	r3, [r9, #-12]	@ movhi
	mov	r0, #0
	mov	r3, r2, lsr #3
	add	r3, r3, r2, asl #1
	add	r3, r3, #52
	add	r8, r3, r8, asl #2
	cmp	r8, r6, asl #9
	ldrcc	r3, .L1529+20
	movcc	r2, #1
	strcch	r2, [r3, #-12]	@ movhi
	add	sp, sp, #20
	@ sp needed
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L1530:
	.align	2
.L1529:
	.word	.LANCHOR2-1712
	.word	.LANCHOR2-1708
	.word	.LANCHOR2-1664
	.word	.LANCHOR2
	.word	.LANCHOR0
	.word	.LANCHOR2-1616
	.fnend
	.size	FtlConstantsInit, .-FtlConstantsInit
	.align	2
	.global	FtlMemInit
	.type	FtlMemInit, %function
FtlMemInit:
	.fnstart
	@ args = 0, pretend = 0, frame = 8
	@ frame_needed = 0, uses_anonymous_args = 0
	stmfd	sp!, {r0, r1, r2, r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.save {r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.pad #12
	movw	r3, #65535
	ldr	r4, .L1636
	mov	r5, #0
	mov	r0, #1024
	mov	r10, #12
	sub	r6, r4, #1520
	sub	r2, r4, #1536
	sub	r8, r4, #1616
	str	r3, [r4, #-1556]
	strh	r5, [r6, #-8]	@ movhi
	mvn	r3, #0
	strh	r5, [r6, #-6]	@ movhi
	sub	r9, r4, #1728
	strh	r3, [r2, #-4]	@ movhi
	strh	r3, [r2, #-2]	@ movhi
	strh	r3, [r2]	@ movhi
	strh	r3, [r6, #-14]	@ movhi
	mov	r3, #32
	strh	r5, [r6, #-4]	@ movhi
	strh	r3, [r6, #-12]	@ movhi
	mov	r3, #128
	strh	r5, [r6, #-2]	@ movhi
	strh	r3, [r6, #-10]	@ movhi
	sub	r6, r4, #1648
	strh	r5, [r8, #-10]	@ movhi
	str	r5, [r4, #-1624]
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
	str	r5, [r4, #-1552]
	str	r5, [r4, #-1548]
	str	r5, [r4, #-1544]
	ldrh	r1, [r6, #-14]
	bl	__aeabi_idiv
	ldrh	r7, [r9, #-8]
	str	r5, [r4, #-1516]
	ldr	r5, .L1636+4
	mov	r7, r7, asl #2
	cmp	r0, r7
	str	r0, [r4, #-1520]
	ldrh	r0, [r5]
	strhi	r7, [r4, #-1520]
	mov	r7, r5
	mov	r0, r0, asl #1
	bl	ftl_malloc
	str	r0, [r4, #-1512]
	ldrh	r0, [r7], #-48
	mul	r0, r10, r0
	bl	ftl_malloc
	ldrh	fp, [r9, #-8]
	mov	r3, #36
	mul	fp, r3, fp
	mov	r2, fp, asl #3
	str	r0, [r4, #-1508]
	mov	r0, r2
	str	r3, [sp, #4]
	str	r2, [sp]
	bl	ftl_malloc
	str	r0, [r4, #-1504]
	mov	r0, fp
	bl	ftl_malloc
	ldr	r2, [sp]
	str	r0, [r4, #-1500]
	mov	r0, r2
	bl	ftl_malloc
	str	r0, [r4, #-1496]
	mov	r0, fp
	bl	ftl_malloc
	str	r0, [r4, #-1492]
	mov	r0, fp
	bl	ftl_malloc
	ldr	r3, [sp, #4]
	str	r0, [r4, #-1488]
	ldr	r0, [r4, #-1520]
	mul	r0, r3, r0
	bl	ftl_malloc
	ldrh	fp, [r6, #-10]
	ldrh	r3, [r9, #-8]
	mov	r3, r3, asl #1
	add	r3, r3, #1
	str	r3, [r4, #-1480]
	str	r0, [r4, #-1484]
	mov	r0, fp
	bl	ftl_malloc
	str	r0, [r4, #-1476]
	mov	r0, fp
	bl	ftl_malloc
	str	r0, [r4, #-1472]
	mov	r0, fp
	bl	ftl_malloc
	str	r0, [r4, #-1468]
	ldr	r0, [r4, #-1480]
	mul	r0, r0, fp
	bl	ftl_malloc
	str	r0, [r4, #-1464]
	ldr	r0, [r4, #-1520]
	mul	r0, r0, fp
	bl	ftl_malloc
	str	r0, [r4, #-1460]
	mov	r0, fp
	bl	ftl_malloc
	str	r0, [r4, #-1456]
	mov	r0, fp
	bl	ftl_malloc
	str	r0, [r4, #-1452]
	ldr	r0, [r4, #-1480]
	mul	r0, r10, r0
	bl	ftl_malloc
	ldrh	r3, [r6, #-8]
	ldrh	r9, [r9, #-8]
	mul	r9, r9, r3
	str	r0, [r4, #-1448]
	mov	r0, r9
	bl	ftl_malloc
	str	r0, [r4, #-1444]
	mov	r0, r9, asl #3
	bl	ftl_malloc
	ldrh	r3, [r6, #-8]
	str	r0, [r4, #-1440]
	ldr	r0, [r4, #-1480]
	mul	r0, r0, r3
	bl	ftl_malloc
	ldrh	r3, [r6, #-8]
	str	r0, [r4, #-1436]
	ldr	r0, [r4, #-1520]
	mul	r0, r0, r3
	bl	ftl_malloc
	str	r0, [r4, #-1432]
	ldrh	r0, [r7, #-14]
	mov	r0, r0, asl #1
	uxth	r0, r0
	strh	r0, [r5, #236]	@ movhi
	bl	ftl_malloc
	str	r0, [r4, #-1424]
	ldrh	r0, [r5, #236]
	add	r0, r0, #544
	add	r0, r0, #3
	mov	r0, r0, lsr #9
	strh	r0, [r5, #236]	@ movhi
	mov	r0, r0, asl #9
	bl	ftl_malloc
	ldrh	r9, [r7, #-14]
	mov	r9, r9, asl #1
	str	r0, [r4, #-1420]
	add	r0, r0, #32
	str	r0, [r4, #-1416]
	mov	r0, r9
	bl	ftl_malloc
	str	r0, [r4, #-1412]
	mov	r0, r9
	bl	ftl_malloc
	ldr	r9, [r4, #-1640]
	mov	r9, r9, asl #1
	str	r0, [r4, #-1408]
	mov	r0, r9
	bl	ftl_malloc
	str	r0, [r4, #-1404]
	mov	r0, r9
	bl	ftl_malloc
	str	r0, [r4, #-1400]
	ldrh	r0, [r7, #-14]
	mov	r0, r0, lsr #3
	add	r0, r0, #4
	bl	ftl_malloc
	str	r0, [r4, #-1396]
	ldrh	r0, [r6]
	mov	r0, r0, asl #1
	bl	ftl_malloc
	str	r0, [r4, #-1392]
	ldrh	r0, [r6]
	mov	r0, r0, asl #1
	bl	ftl_malloc
	str	r0, [r4, #-1388]
	ldrh	r0, [r6]
	mov	r0, r0, asl #2
	bl	ftl_malloc
	str	r0, [r4, #-1384]
	ldrh	r0, [r5, #18]
	mov	r0, r0, asl #2
	bl	ftl_malloc
	ldrh	r2, [r5, #18]
	mov	r1, #0
	mov	r2, r2, asl #2
	str	r0, [r4, #-1380]
	bl	ftl_memset
	ldrh	r9, [r5, #32]
	mov	r9, r9, asl #2
	mov	r0, r9
	bl	ftl_malloc
	str	r0, [r4, #-1376]
	mov	r0, r9
	bl	ftl_malloc
	str	r0, [r4, #-1372]
	ldr	r0, [r4, #-1640]
	mov	r0, r0, asl #2
	bl	ftl_malloc
	str	r0, [r4, #-1368]
	ldrh	r0, [r8, #-14]
	mul	r0, r10, r0
	bl	ftl_malloc
	ldrh	r3, [r8, #-14]
	str	r0, [r4, #-1364]
	ldrh	r0, [r6, #-10]
	add	r6, r5, #320
	mul	r0, r0, r3
	bl	ftl_malloc
	ldrh	r3, [r7, #-14]
	str	r0, [r4, #-1360]
	mov	r0, #6
	mul	r0, r0, r3
	bl	ftl_malloc
	ldrh	r3, [r5, #-6]
	add	r5, r5, #344
	add	r3, r3, #31
	mov	r3, r3, asr #5
	strh	r3, [r6, #-8]	@ movhi
	str	r0, [r4, #-1356]
	ldrh	r0, [r7, #-2]
	mul	r0, r0, r3
	mov	r0, r0, asl #2
	bl	ftl_malloc
	ldrh	r2, [r6, #-8]
	ldrh	ip, [r7, #-2]
	mov	r3, #1
	mov	r2, r2, asl #2
	mov	r1, r2
	str	r0, [r4, #-1320]
.L1533:
	cmp	r3, ip
	bcs	.L1634
	ldr	r0, [r4, #-1320]
	add	r3, r3, #1
	add	r0, r0, r1
	add	r1, r1, r2
	str	r0, [r5, #4]!
	b	.L1533
.L1634:
	ldr	r2, .L1636+8
	mov	r1, #0
.L1535:
	cmp	r3, #8
	addne	r0, r2, r3, asl #2
	addne	r3, r3, #1
	strne	r1, [r0, #28]
	bne	.L1535
.L1635:
	ldr	r2, [r4, #-1404]
	ldr	r3, .L1636
	cmp	r2, #0
	bne	.L1537
.L1539:
	ldr	r0, .L1636+12
	ldr	r1, .L1636+16
	bl	printk
	mvn	r0, #0
	b	.L1538
.L1537:
	ldr	r2, [r3, #-1400]
	cmp	r2, #0
	beq	.L1539
	ldr	r2, [r3, #-1376]
	cmp	r2, #0
	beq	.L1539
	ldr	r2, [r3, #-1368]
	cmp	r2, #0
	beq	.L1539
	ldr	r2, [r3, #-1364]
	cmp	r2, #0
	beq	.L1539
	ldr	r2, [r3, #-1360]
	cmp	r2, #0
	beq	.L1539
	ldr	r2, [r3, #-1356]
	cmp	r2, #0
	beq	.L1539
	ldr	r2, [r3, #-1320]
	cmp	r2, #0
	beq	.L1539
	ldr	r3, [r3, #-1408]
	cmp	r3, #0
	beq	.L1539
	ldr	r2, [r4, #-1512]
	ldr	r3, .L1636
	cmp	r2, #0
	beq	.L1539
	ldr	r2, [r3, #-1508]
	cmp	r2, #0
	beq	.L1539
	ldr	r2, [r3, #-1504]
	cmp	r2, #0
	beq	.L1539
	ldr	r2, [r3, #-1496]
	cmp	r2, #0
	beq	.L1539
	ldr	r2, [r3, #-1492]
	cmp	r2, #0
	beq	.L1539
	ldr	r2, [r3, #-1488]
	cmp	r2, #0
	beq	.L1539
	ldr	r2, [r3, #-1500]
	cmp	r2, #0
	beq	.L1539
	ldr	r2, [r3, #-1476]
	cmp	r2, #0
	beq	.L1539
	ldr	r2, [r3, #-1472]
	cmp	r2, #0
	beq	.L1539
	ldr	r3, [r3, #-1468]
	cmp	r3, #0
	beq	.L1539
	ldr	r2, [r4, #-1464]
	ldr	r3, .L1636
	cmp	r2, #0
	beq	.L1539
	ldr	r2, [r3, #-1456]
	cmp	r2, #0
	beq	.L1539
	ldr	r2, [r3, #-1452]
	cmp	r2, #0
	beq	.L1539
	ldr	r2, [r3, #-1448]
	cmp	r2, #0
	beq	.L1539
	ldr	r2, [r3, #-1444]
	cmp	r2, #0
	beq	.L1539
	ldr	r2, [r3, #-1440]
	cmp	r2, #0
	beq	.L1539
	ldr	r2, [r3, #-1436]
	cmp	r2, #0
	beq	.L1539
	ldr	r2, [r3, #-1416]
	cmp	r2, #0
	beq	.L1539
	ldr	r2, [r3, #-1424]
	cmp	r2, #0
	beq	.L1539
	ldr	r3, [r3, #-1392]
	cmp	r3, #0
	beq	.L1539
	ldr	r3, .L1636
	ldr	r2, [r3, #-1388]
	cmp	r2, #0
	beq	.L1539
	ldr	r2, [r3, #-1384]
	cmp	r2, #0
	beq	.L1539
	ldr	r3, [r3, #-1380]
	cmp	r3, #0
	beq	.L1539
	mov	r0, #0
.L1538:
	add	sp, sp, #12
	@ sp needed
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L1637:
	.align	2
.L1636:
	.word	.LANCHOR2
	.word	.LANCHOR2-1664
	.word	.LANCHOR2-1348
	.word	.LC32
	.word	.LANCHOR3+136
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
	ldr	r2, .L1645
	sub	r3, r2, #1280
	ldrh	r3, [r3, #-8]
	cmp	r3, #0
	beq	.L1644
	ldr	r3, [r2, #-1392]
	sub	r2, r2, #1648
	ldrh	r2, [r2]
	add	r2, r3, r2, asl #1
.L1640:
	cmp	r3, r2
	beq	.L1644
	ldrh	r1, [r3], #2
	cmp	r1, r0
	bne	.L1640
	mov	r0, #1
	bx	lr
.L1644:
	mov	r0, #0
	bx	lr
.L1646:
	.align	2
.L1645:
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
	ldr	r2, .L1654
	ldr	r3, [r2, #-1516]
	cmp	r3, #0
	beq	.L1650
	stmfd	sp!, {r4, r5, lr}
	.save {r4, r5, lr}
	mov	r5, #36
	ldr	r4, [r2, #-1484]
	mov	r2, #0
.L1649:
	mla	ip, r5, r2, r4
	ldr	lr, [ip, #16]
	cmp	lr, r0
	movcc	ip, #0
	movcs	ip, #1
	cmp	lr, r1
	movhi	ip, #0
	cmp	ip, #0
	bne	.L1651
	add	r2, r2, #1
	cmp	r2, r3
	bne	.L1649
	mov	r0, ip
	ldmfd	sp!, {r4, r5, pc}
.L1650:
	mov	r0, r3
	bx	lr
.L1651:
	mov	r0, #1
	ldmfd	sp!, {r4, r5, pc}
.L1655:
	.align	2
.L1654:
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
	ldr	r3, .L1657
	ldr	r0, [r3, #-2740]
	bx	lr
.L1658:
	.align	2
.L1657:
	.word	.LANCHOR2
	.fnend
	.size	FtlGetCap, .-FtlGetCap
	.align	2
	.global	FtlGetCapacity
	.type	FtlGetCapacity, %function
FtlGetCapacity:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	@ link register save eliminated.
	ldr	r3, .L1660
	ldr	r0, [r3, #-2740]
	bx	lr
.L1661:
	.align	2
.L1660:
	.word	.LANCHOR2
	.fnend
	.size	FtlGetCapacity, .-FtlGetCapacity
	.align	2
	.global	ftl_get_density
	.type	ftl_get_density, %function
ftl_get_density:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	@ link register save eliminated.
	ldr	r3, .L1663
	ldr	r0, [r3, #-2740]
	bx	lr
.L1664:
	.align	2
.L1663:
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
	ldr	r3, .L1666
	ldr	r0, [r3, #-1284]
	bx	lr
.L1667:
	.align	2
.L1666:
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
	ldr	r5, .L1670
	sub	r3, r5, #1664
	ldrh	r4, [r3, #-6]
	mov	r1, r4
	bl	__aeabi_uidiv
	uxth	r2, r0
	smulbb	r3, r2, r4
	add	r1, r5, r2, asl #2
	mov	r4, #1
	ldr	ip, [r1, #-1320]
	rsb	r3, r3, r6
	uxth	r3, r3
	and	r1, r3, #31
	mov	lr, r3, lsr #5
	ldr	r0, [ip, lr, asl #2]
	orr	r1, r0, r4, asl r1
	ldr	r0, .L1670+4
	str	r1, [ip, lr, asl #2]
	str	r1, [sp]
	mov	r1, r6
	bl	printk
	sub	r2, r5, #1344
	mov	r0, #0
	ldrh	r3, [r2, #2]
	add	r3, r3, r4
	strh	r3, [r2, #2]	@ movhi
	add	sp, sp, #8
	@ sp needed
	ldmfd	sp!, {r4, r5, r6, pc}
.L1671:
	.align	2
.L1670:
	.word	.LANCHOR2
	.word	.LC33
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
	ldr	r5, .L1674
	sub	r3, r5, #1664
	ldrh	r6, [r3, #-6]
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
	ldr	r3, [r5, #-1320]
	ldr	r0, [r3, r2, asl #2]
	mov	r0, r0, lsr r4
	and	r0, r0, #1
	ldmfd	sp!, {r3, r4, r5, r6, r7, pc}
.L1675:
	.align	2
.L1674:
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
	ldr	r5, .L1683
	mov	r0, r1
	mov	r6, r1
	mov	r1, #0
	sub	r5, r5, #4
	ldrh	r2, [r5, #-4]
	mov	r2, r2, asl #2
	bl	ftl_memset
	add	r3, r4, #1020
	add	r3, r3, #2
	sub	r1, r4, #2
	mov	lr, #1
	movw	r4, #65535
.L1679:
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
	bne	.L1679
	ldmfd	sp!, {r4, r5, r6, pc}
.L1684:
	.align	2
.L1683:
	.word	.LANCHOR2-1344
	.fnend
	.size	FtlBbt2Bitmap, .-FtlBbt2Bitmap
	.align	2
	.global	FtlBbmTblFlush
	.type	FtlBbmTblFlush, %function
FtlBbmTblFlush:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	stmfd	sp!, {r0, r1, r2, r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.save {r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.pad #12
	ldr	r4, .L1703
	ldr	r5, [r4, #-1280]
	cmp	r5, #0
	bne	.L1687
	ldr	r3, [r4, #-1444]
	mov	r1, r5
	ldr	r0, [r4, #-1476]
	sub	r8, r4, #1344
	ldr	r6, .L1703+4
	mov	r10, r4
	str	r3, [r4, #-1264]
	sub	r3, r4, #1648
	str	r0, [r4, #-1268]
	sub	fp, r6, #28
	ldrh	r2, [r3, #-10]
	bl	ftl_memset
	ldr	r9, .L1703+8
.L1688:
	ldrh	r3, [r9]
	ldr	r7, .L1703
	cmp	r5, r3
	bge	.L1702
	ldrh	r2, [fp]
	ldr	r3, [r10, #-1268]
	ldr	r1, [r6, #4]!
	mul	r0, r2, r5
	mov	r2, r2, asl #2
	add	r5, r5, #1
	add	r0, r3, r0, asl #2
	bl	ftl_memcpy
	b	.L1688
.L1702:
	ldr	r6, [r7, #-1264]
	mov	r1, #255
	mov	r2, #16
	ldr	r5, .L1703+12
	mov	r0, r6
	bl	ftl_memset
	ldr	r3, .L1703+16
	mov	r10, r5
	strh	r3, [r6]	@ movhi
	ldr	r3, [r7, #-1340]
	str	r3, [r6, #4]
	ldrh	r3, [r8, #-4]
	mov	r8, #0
	mov	r9, r8
	strh	r3, [r6, #2]	@ movhi
	ldrh	r3, [r5, #4]
	strh	r3, [r6, #8]	@ movhi
	ldrh	r3, [r5, #6]
	strh	r3, [r6, #10]	@ movhi
	ldr	r3, [r7, #-1740]
	strh	r3, [r6, #12]	@ movhi
.L1690:
	ldr	r3, [r4, #-1476]
	mov	fp, #0
	ldrh	r1, [r5]
	ldrh	r2, [r5, #2]
	str	r3, [r4, #-1268]
	ldr	r3, [r4, #-1444]
	str	fp, [r4, #-1276]
	str	r3, [r4, #-1264]
	orr	r3, r2, r1, asl #10
	ldrh	r0, [r6, #10]
	str	r3, [r4, #-1272]
	ldrh	r3, [r5, #4]
	str	r0, [sp]
	ldr	r0, .L1703+20
	bl	printk
	ldr	r3, .L1703+24
	ldrh	r2, [r5, #2]
	ldrh	r3, [r3]
	sub	r3, r3, #1
	cmp	r2, r3
	blt	.L1691
	ldr	r3, [r7, #-1340]
	mov	r1, #1
	ldrh	r2, [r5]
	add	r3, r3, #1
	ldr	r0, [r7, #-1492]
	str	r3, [r7, #-1340]
	str	r3, [r6, #4]
	ldrh	r3, [r5, #4]
	strh	r2, [r6, #8]	@ movhi
	strh	r2, [r5, #4]	@ movhi
	mov	r2, r1
	strh	r3, [r5]	@ movhi
	mov	r3, r3, asl #10
	str	r3, [r7, #-1272]
	str	r3, [r0, #4]
	strh	fp, [r5, #2]	@ movhi
	bl	FlashEraseBlocks
.L1691:
	mov	r1, #1
	ldr	r0, .L1703+28
	mov	r3, r1
	mov	r2, r1
	bl	FlashProgPages
	ldrh	r3, [r10, #2]
	ldr	fp, .L1703
	add	r3, r3, #1
	strh	r3, [r10, #2]	@ movhi
	ldr	r3, [r4, #-1276]
	cmn	r3, #1
	bne	.L1692
	add	r8, r8, #1
	ldr	r0, .L1703+32
	ldr	r1, [r7, #-1272]
	uxth	r8, r8
	bl	printk
	cmp	r8, #3
	bls	.L1690
	ldr	r0, .L1703+36
	mov	r2, r8
	ldr	r1, [fp, #-1272]
	bl	printk
	mov	r3, #1
	str	r3, [fp, #-1280]
	b	.L1687
.L1692:
	add	r9, r9, #1
	cmp	r9, #1
	beq	.L1690
	cmp	r3, #256
	beq	.L1690
.L1687:
	mov	r0, #0
	add	sp, sp, #12
	@ sp needed
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L1704:
	.align	2
.L1703:
	.word	.LANCHOR2
	.word	.LANCHOR2-1324
	.word	.LANCHOR2-1714
	.word	.LANCHOR2-1348
	.word	-3887
	.word	.LC34
	.word	.LANCHOR2-1666
	.word	.LANCHOR2-1276
	.word	.LC35
	.word	.LC36
	.fnend
	.size	FtlBbmTblFlush, .-FtlBbmTblFlush
	.align	2
	.global	FtlLoadFactoryBbt
	.type	FtlLoadFactoryBbt, %function
FtlLoadFactoryBbt:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	ldr	r2, .L1717
	stmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, lr}
	.save {r4, r5, r6, r7, r8, r9, r10, lr}
	mov	r5, #0
	ldr	r6, .L1717+4
	mov	r9, r2
	ldr	r3, [r2, #-1476]
	ldr	r7, [r2, #-1444]
	sub	r8, r6, #376
	str	r3, [r2, #-1268]
	str	r7, [r2, #-1264]
.L1706:
	ldrh	r3, [r8]
	cmp	r5, r3
	bcs	.L1716
	ldr	r2, .L1717+8
	mvn	r3, #0
	strh	r3, [r6, #2]!	@ movhi
	ldrh	r3, [r2]
	mov	r10, r2
	sub	r3, r3, #1
	uxth	r4, r3
.L1707:
	ldrh	r3, [r10]
	sub	r2, r3, #16
	cmp	r4, r2
	ble	.L1709
	mla	r3, r3, r5, r4
	mov	r1, #1
	ldr	r0, .L1717+12
	mov	r2, r1
	mov	r3, r3, asl #10
	str	r3, [r9, #-1272]
	bl	FlashReadPages
	ldr	r3, [r9, #-1276]
	cmn	r3, #1
	beq	.L1708
	ldrh	r2, [r7]
	movw	r3, #61664
	cmp	r2, r3
	streqh	r4, [r6]	@ movhi
	beq	.L1709
.L1708:
	sub	r4, r4, #1
	uxth	r4, r4
	b	.L1707
.L1709:
	add	r5, r5, #1
	b	.L1706
.L1716:
	mov	r0, #0
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, pc}
.L1718:
	.align	2
.L1717:
	.word	.LANCHOR2
	.word	.LANCHOR2-1338
	.word	.LANCHOR2-1670
	.word	.LANCHOR2-1276
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
	ldr	r0, .L1720
	mvn	r2, #0
	mov	r1, #255
	add	r0, r0, #8
	strh	r2, [r0, #-12]	@ movhi
	mov	r2, #0
	strh	r2, [r0, #-6]	@ movhi
	mov	r2, #16
	b	ftl_memset
.L1721:
	.align	2
.L1720:
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
	ldr	r4, .L1731
	ldrh	r6, [r4, #-6]
	ldrh	r3, [r4, #-50]
	mov	r4, r5
	mul	r6, r3, r6
.L1723:
	uxth	r0, r5
	cmp	r0, r6
	bge	.L1730
	bl	FtlBbmIsBadBlock
	add	r5, r5, #1
	cmp	r0, #0
	addne	r4, r4, #1
	uxthne	r4, r4
	b	.L1723
.L1730:
	mov	r0, r4
	ldmfd	sp!, {r4, r5, r6, pc}
.L1732:
	.align	2
.L1731:
	.word	.LANCHOR2-1664
	.fnend
	.size	FtlBbtCalcTotleCnt, .-FtlBbtCalcTotleCnt
	.align	2
	.global	FtlMakeBbt
	.type	FtlMakeBbt, %function
FtlMakeBbt:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	stmfd	sp!, {r3, r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.save {r3, r4, r5, r6, r7, r8, r9, r10, fp, lr}
	ldr	r7, .L1756
	ldr	r5, [r7, #-1280]
	cmp	r5, #0
	bne	.L1734
	bl	FtlBbtMemInit
	ldr	r8, .L1756+4
	bl	FtlLoadFactoryBbt
	mov	r4, r7
	sub	r9, r8, #18
.L1735:
	ldr	r6, .L1756+8
	ldrh	r3, [r6]
	cmp	r5, r3
	bcs	.L1754
	ldrh	r3, [r9, #2]!
	movw	r2, #65535
	ldr	r0, [r4, #-1476]
	ldr	r10, [r4, #-1444]
	cmp	r3, r2
	str	r0, [r4, #-1268]
	str	r10, [r4, #-1264]
	beq	.L1736
	ldrh	fp, [r6, #44]
	mov	r1, #1
	mov	r2, r1
	ldr	r0, .L1756+12
	mla	fp, fp, r5, r3
	mov	r3, fp, asl #10
	str	r3, [r4, #-1272]
	bl	FlashReadPages
	ldrh	r2, [r6, #44]
	ldr	r0, [r8]
	add	r2, r2, #7
	ldr	r1, [r4, #-1268]
	mov	r2, r2, asr #3
	bl	ftl_memcpy
	b	.L1737
.L1736:
	mov	r1, r5
	bl	FlashGetBadBlockList
	ldr	r0, [r4, #-1268]
	ldr	r1, [r8]
	bl	FtlBbt2Bitmap
	ldrh	r6, [r6, #44]
.L1739:
	sub	r6, r6, #1
	uxth	r6, r6
.L1738:
	ldr	fp, .L1756+16
	ldrh	r0, [fp]
	smlabb	r0, r0, r5, r6
	uxth	r0, r0
	bl	FtlBbmIsBadBlock
	cmp	r0, #1
	beq	.L1739
	mov	r1, #0
	mov	r2, #16
	strh	r6, [r9]	@ movhi
	ldr	r0, [r4, #-1444]
	bl	ftl_memset
	ldr	r3, .L1756+20
	strh	r3, [r10]	@ movhi
	mov	r3, #0
	str	r3, [r10, #4]
	ldrh	r3, [r9]
	ldrh	fp, [fp]
	strh	r3, [r10, #2]	@ movhi
	ldrh	r3, [r9]
	ldr	r1, [r8]
	ldr	r0, [r4, #-1268]
	mla	fp, fp, r5, r3
	mov	r3, fp, asl #10
	str	r3, [r4, #-1272]
	ldr	r3, .L1756+24
	ldrh	r2, [r3]
	mov	r2, r2, asl #2
	bl	ftl_memcpy
	mov	r1, #1
	mov	r2, r1
	ldr	r0, .L1756+12
	bl	FlashEraseBlocks
	mov	r1, #1
	mov	r3, r1
	ldr	r0, .L1756+12
	mov	r2, r1
	bl	FlashProgPages
	ldr	r3, [r4, #-1276]
	cmn	r3, #1
	bne	.L1737
	uxth	r0, fp
	bl	FtlBbmMapBadBlock
	b	.L1738
.L1737:
	uxth	r0, fp
	add	r5, r5, #1
	bl	FtlBbmMapBadBlock
	add	r8, r8, #4
	b	.L1735
.L1754:
	add	r6, r6, #60
	mov	r4, #0
.L1742:
	ldrh	r3, [r6]
	uxth	r0, r4
	add	r4, r4, #1
	cmp	r3, r0
	bls	.L1755
	bl	FtlBbmMapBadBlock
	b	.L1742
.L1755:
	ldr	r6, .L1756+28
	movw	r8, #65535
	ldrh	r4, [r6, #12]
	sub	r4, r4, #1
	uxth	r4, r4
.L1744:
	ldrh	r3, [r6, #12]
	ldr	r5, .L1756+28
	sub	r3, r3, #48
	cmp	r4, r3
	ble	.L1748
	mov	r0, r4
	bl	FtlBbmIsBadBlock
	cmp	r0, #1
	beq	.L1745
	mov	r0, r4
	bl	FlashTestBlk
	cmp	r0, #0
	beq	.L1746
	mov	r0, r4
	bl	FtlBbmMapBadBlock
	b	.L1745
.L1746:
	ldrh	r3, [r6]
	cmp	r3, r8
	streqh	r4, [r6]	@ movhi
.L1747:
	strneh	r4, [r5, #4]	@ movhi
	bne	.L1748
.L1745:
	sub	r4, r4, #1
	uxth	r4, r4
	b	.L1744
.L1748:
	ldr	r3, .L1756+32
	mov	r4, #0
	ldr	r0, [r7, #-1492]
	mov	r1, #1
	str	r4, [r7, #-1340]
	mov	r2, #2
	ldrh	r3, [r3, #-4]
	strh	r4, [r5, #2]	@ movhi
	mov	r3, r3, asl #10
	str	r3, [r0, #4]
	ldrh	r3, [r5, #4]
	mov	r3, r3, asl #10
	str	r3, [r0, #40]
	bl	FlashEraseBlocks
	ldr	r3, .L1756+32
	ldrh	r0, [r3, #-4]
	bl	FtlBbmMapBadBlock
	ldrh	r0, [r5, #4]
	bl	FtlBbmMapBadBlock
	bl	FtlBbmTblFlush
	ldr	r3, [r7, #-1340]
	ldrh	r2, [r5, #4]
	add	r3, r3, #1
	str	r3, [r7, #-1340]
	ldr	r3, .L1756+32
	ldr	r1, .L1756+32
	strh	r4, [r5, #2]	@ movhi
	ldrh	r3, [r3, #-4]
	strh	r2, [r1, #-4]	@ movhi
	strh	r3, [r5, #4]	@ movhi
	bl	FtlBbmTblFlush
.L1734:
	mov	r0, #0
	ldmfd	sp!, {r3, r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L1757:
	.align	2
.L1756:
	.word	.LANCHOR2
	.word	.LANCHOR2-1320
	.word	.LANCHOR2-1714
	.word	.LANCHOR2-1276
	.word	.LANCHOR2-1670
	.word	-3872
	.word	.LANCHOR2-1352
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
	ldr	r4, .L1760
	mov	r7, r0
	sub	r3, r4, #1712
	sub	r4, r4, #1664
	ldrh	r6, [r3]
	mov	r1, r6
	bl	__aeabi_uidivmod
	mov	r0, r7
	smlabb	r5, r5, r6, r1
	mov	r1, r6
	bl	__aeabi_uidiv
	ldrh	r3, [r4, #-6]
	smlabb	r0, r3, r0, r5
	uxth	r0, r0
	ldmfd	sp!, {r3, r4, r5, r6, r7, pc}
.L1761:
	.align	2
.L1760:
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
	ldr	r3, .L1764
	stmfd	sp!, {r4, r5, r6, lr}
	.save {r4, r5, r6, lr}
	sub	r2, r3, #1712
	sub	r3, r3, #1664
	mov	r6, r0
	ldrh	r5, [r2]
	ldrh	r1, [r3, #-6]
	bl	__aeabi_uidiv
	mov	r1, r5
	smulbb	r4, r0, r5
	mov	r0, r6
	bl	__aeabi_uidivmod
	add	r1, r4, r1
	uxth	r0, r1
	ldmfd	sp!, {r4, r5, r6, pc}
.L1765:
	.align	2
.L1764:
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
	ldr	r4, .L1768
	sub	r3, r4, #1664
	sub	r4, r4, #1712
	ldrh	r1, [r3, #-6]
	bl	__aeabi_uidivmod
	uxth	r0, r1
	ldrh	r1, [r4]
	bl	__aeabi_uidiv
	uxth	r0, r0
	ldmfd	sp!, {r4, pc}
.L1769:
	.align	2
.L1768:
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
	bls	.L1771
	rsb	r0, r1, r0
	cmp	r0, #-2147483648
	movhi	r0, #0
	movls	r0, #1
	bx	lr
.L1771:
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
	ldr	r3, .L1785
	cmp	r1, #1
	stmfd	sp!, {r4, r5, r6, r7, r8, lr}
	.save {r4, r5, r6, r7, r8, lr}
	.pad #104
	sub	sp, sp, #104
	ldreqh	r4, [r3, #-2]
	mov	r8, r1
	ldrneh	r4, [r3, #-4]
	mov	r6, r0, asl #10
	add	r3, sp, #40
	add	r0, sp, #4
	sub	r5, r4, #1
	str	r3, [sp, #16]
	mov	r1, #1
	mov	r2, r8
	uxth	r5, r5
	mov	r7, #0
	str	r7, [sp, #12]
	sxth	r3, r5
	orr	r3, r3, r6
	str	r3, [sp, #8]
	bl	FlashReadPages
	ldr	r3, [sp, #40]
	cmn	r3, #1
	bne	.L1776
.L1777:
	sxth	r4, r7
	sxth	r3, r5
	cmp	r4, r3
	bgt	.L1776
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
	bne	.L1778
	ldr	r3, [sp, #44]
	cmn	r3, #1
	bne	.L1778
	ldr	r3, [sp, #4]
	cmn	r3, #1
	subne	r4, r4, #1
	uxthne	r5, r4
	bne	.L1777
.L1778:
	add	r3, r4, #1
	uxth	r7, r3
	b	.L1777
.L1776:
	sxth	r0, r5
	add	sp, sp, #104
	@ sp needed
	ldmfd	sp!, {r4, r5, r6, r7, r8, pc}
.L1786:
	.align	2
.L1785:
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
	ldr	r4, .L1820
	ldr	r3, [r4, #-1476]
	mov	r7, r4
	ldr	r6, [r4, #-1444]
	str	r3, [r4, #-1268]
	str	r6, [r4, #-1264]
	bl	FtlBbtMemInit
	sub	r3, r4, #1664
	sub	r8, r3, #6
	ldrh	r5, [r3, #-6]
	sub	r5, r5, #1
	uxth	r5, r5
.L1788:
	ldrh	r3, [r8]
	sub	r3, r3, #48
	cmp	r5, r3
	ble	.L1791
	mov	r1, #1
	ldr	r0, .L1820+4
	mov	r2, r1
	mov	r3, r5, asl #10
	str	r3, [r7, #-1272]
	bl	FlashReadPages
	ldr	r3, [r7, #-1276]
	cmn	r3, #1
	bne	.L1789
	ldr	r3, [r7, #-1272]
	mov	r1, #1
	ldr	r0, .L1820+4
	mov	r2, r1
	add	r3, r3, #1
	str	r3, [r7, #-1272]
	bl	FlashReadPages
.L1789:
	ldr	r2, [r4, #-1276]
	ldr	r3, .L1820
	cmn	r2, #1
	beq	.L1790
	ldrh	r1, [r6]
	movw	r2, #61649
	cmp	r1, r2
	bne	.L1790
	sub	r2, r3, #1344
	strh	r5, [r2, #-4]	@ movhi
	ldr	r2, [r6, #4]
	str	r2, [r3, #-1340]
	ldr	r3, .L1820+8
	ldrh	r2, [r6, #8]
	strh	r2, [r3, #4]	@ movhi
	b	.L1791
.L1790:
	sub	r5, r5, #1
	uxth	r5, r5
	b	.L1788
.L1791:
	ldr	r8, .L1820
	movw	r2, #65535
	sub	r7, r8, #1344
	sub	r5, r7, #4
	ldrh	r3, [r7, #-4]
	cmp	r3, r2
	beq	.L1805
	ldrh	r3, [r5, #4]
	cmp	r3, r2
	beq	.L1795
	mov	r1, #1
	add	r0, r5, #72
	mov	r2, r1
	mov	r3, r3, asl #10
	str	r3, [r8, #-1272]
	bl	FlashReadPages
	ldr	r3, [r8, #-1276]
	cmn	r3, #1
	beq	.L1795
	ldrh	r2, [r6]
	movw	r3, #61649
	cmp	r2, r3
	bne	.L1795
	ldr	r3, [r6, #4]
	ldr	r2, [r8, #-1340]
	cmp	r3, r2
	strhi	r3, [r8, #-1340]
	ldrhih	r2, [r5, #4]
	ldrhih	r3, [r6, #8]
	strhih	r2, [r7, #-4]	@ movhi
	strhih	r3, [r5, #4]	@ movhi
.L1795:
	ldrh	r0, [r7, #-4]
	mov	r1, #1
	bl	FtlGetLastWrittenPage
	movw	r8, #61649
	uxth	r7, r0
	add	r0, r0, #1
	strh	r0, [r5, #2]	@ movhi
.L1797:
	sxth	r3, r7
	cmp	r3, #0
	blt	.L1802
	ldrh	r2, [r5]
	mov	r1, #1
	ldr	r0, .L1820+4
	orr	r3, r3, r2, asl #10
	str	r3, [r4, #-1272]
	ldr	r3, [r4, #-1476]
	mov	r2, r1
	str	r3, [r4, #-1268]
	bl	FlashReadPages
	ldr	r3, [r4, #-1276]
	cmn	r3, #1
	beq	.L1798
	ldrh	r3, [r6]
	cmp	r3, r8
	bne	.L1798
.L1802:
	ldrh	r2, [r6, #10]
	ldrh	r0, [r6, #12]
	ldr	r3, .L1820
	strh	r2, [r5, #6]	@ movhi
	movw	r2, #65535
	cmp	r0, r2
	bne	.L1799
	b	.L1800
.L1798:
	sub	r7, r7, #1
	uxth	r7, r7
	b	.L1797
.L1799:
	ldr	r2, [r3, #-1740]
	cmp	r0, r2
	beq	.L1800
	sub	r3, r3, #1712
	ldrh	r3, [r3, #-14]
	mov	r3, r3, lsr #2
	cmp	r0, r3
	cmpcc	r2, r3
	bcs	.L1800
	bl	FtlSysBlkNumInit
.L1800:
	ldr	r5, .L1820+12
	mov	r4, #0
	ldr	r7, .L1820
	ldr	r6, .L1820+16
	sub	r8, r5, #28
.L1803:
	ldrh	r3, [r6]
	cmp	r4, r3
	bcs	.L1819
	ldrh	r2, [r8]
	ldr	r1, [r7, #-1268]
	ldr	r0, [r5, #4]!
	mov	r2, r2, asl #2
	mla	r1, r4, r2, r1
	bl	ftl_memcpy
	add	r4, r4, #1
	b	.L1803
.L1819:
	mov	r0, #0
	ldmfd	sp!, {r4, r5, r6, r7, r8, pc}
.L1805:
	mvn	r0, #0
	ldmfd	sp!, {r4, r5, r6, r7, r8, pc}
.L1821:
	.align	2
.L1820:
	.word	.LANCHOR2
	.word	.LANCHOR2-1276
	.word	.LANCHOR2-1348
	.word	.LANCHOR2-1324
	.word	.LANCHOR2-1714
	.fnend
	.size	FtlLoadBbt, .-FtlLoadBbt
	.align	2
	.global	FtlFreeSysBlkQueueInit
	.type	FtlFreeSysBlkQueueInit, %function
FtlFreeSysBlkQueueInit:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	ldr	r3, .L1824
	mov	r2, #2048
	stmfd	sp!, {r4, lr}
	.save {r4, lr}
	mov	r4, #0
	strh	r0, [r3, #-8]	@ movhi
	mov	r1, r4
	mov	r0, r3
	strh	r4, [r3, #-6]	@ movhi
	strh	r4, [r3, #-4]	@ movhi
	strh	r4, [r3, #-2]	@ movhi
	bl	ftl_memset
	mov	r0, r4
	ldmfd	sp!, {r4, pc}
.L1825:
	.align	2
.L1824:
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
	ldr	r3, .L1827
	ldrh	r0, [r3, #6]
	clz	r0, r0
	mov	r0, r0, lsr #5
	bx	lr
.L1828:
	.align	2
.L1827:
	.word	.LANCHOR2-1240
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
	ldr	r3, .L1830
	ldrh	r0, [r3, #6]
	sub	r0, r0, #1024
	clz	r0, r0
	mov	r0, r0, lsr #5
	bx	lr
.L1831:
	.align	2
.L1830:
	.word	.LANCHOR2-1240
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
	ldr	r4, .L1842
	ldr	r5, .L1842+4
	ldrh	r3, [r4, #6]
	cmp	r3, #1024
	ldmeqfd	sp!, {r3, r4, r5, r6, r7, pc}
	cmp	r1, #0
	beq	.L1834
	ldr	r3, [r5, #-1280]
	cmp	r3, #0
	bne	.L1834
	bl	P2V_block_in_plane
	mov	r1, #1
	mov	r3, r7, asl #10
	mov	r2, r1
	mov	r6, r0
	ldr	r0, [r5, #-1492]
	str	r3, [r0, #4]
	bl	FlashEraseBlocks
	ldr	r1, [r5, #-1416]
	mov	r3, r6, asl #1
	ldrh	r2, [r1, r3]
	add	r2, r2, #1
	strh	r2, [r1, r3]	@ movhi
	ldr	r3, [r5, #-1576]
	add	r3, r3, #1
	str	r3, [r5, #-1576]
.L1834:
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
.L1843:
	.align	2
.L1842:
	.word	.LANCHOR2-1240
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
	ldr	r9, .L1894
	ldr	r5, [r9, #-1280]
	ldrb	r3, [r9, #-1874]	@ zero_extendqisi2
	cmp	r5, #0
	str	r3, [sp, #16]
	movne	r0, #0
	bne	.L1845
	ldrb	r3, [r9, #-2744]	@ zero_extendqisi2
	mov	fp, r1
	mov	r10, r5
	mov	r4, r5
	mov	r7, r9
	mov	r8, #36
	str	r0, [sp, #4]
	str	r3, [sp, #8]
	str	r0, [r9, #-1544]
.L1846:
	ldr	r3, .L1894+4
	uxth	r1, r10
	ldr	r6, .L1894
	ldrh	r0, [r3]
	cmp	r0, r1
	bls	.L1889
	mul	r0, r8, r1
	ldr	ip, [r7, #-1492]
	add	r1, r7, r1
	mov	r3, #0
	str	r3, [ip, r0]
	ldrb	r0, [r1, #-1708]	@ zero_extendqisi2
	ldr	r1, [sp, #4]
	bl	V2P_block
	cmp	fp, #0
	mov	r6, r0
	beq	.L1847
	bl	IsBlkInVendorPart
	cmp	r0, #0
	bne	.L1848
.L1847:
	mov	r0, r6
	bl	FtlBbmIsBadBlock
	cmp	r0, #0
	addne	r4, r4, #1
	uxthne	r4, r4
	bne	.L1848
	ldr	r3, .L1894+8
	mov	r6, r6, asl #10
	ldr	ip, [r9, #-1492]
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
	ldr	r0, [r9, #-1440]
	bic	r1, r1, #3
	add	r1, r0, r1
	str	r1, [ip, #12]
.L1848:
	add	r10, r10, #1
	b	.L1846
.L1889:
	cmp	r5, #0
	beq	.L1869
	ldr	r3, [sp, #8]
	mov	r2, r5
	ldr	r0, [r6, #-1492]
	mov	r8, #0
	adds	r7, r3, #0
	strb	r8, [r6, #-1874]
	movne	r7, #1
	mov	r1, r7
	bl	FlashEraseBlocks
	ldrb	r3, [sp, #16]	@ zero_extendqisi2
	strb	r3, [r6, #-1874]
	mov	r6, #36
.L1852:
	uxth	r2, r8
	cmp	r2, r5
	bcs	.L1890
	mul	r2, r6, r8
	ldr	r1, [r9, #-1492]
	add	r0, r1, r2
	ldr	r2, [r1, r2]
	cmn	r2, #1
	bne	.L1853
	ldr	r0, [r0, #4]
	add	r4, r4, #1
	ubfx	r0, r0, #10, #16
	uxth	r4, r4
	bl	FtlBbmMapBadBlock
.L1853:
	add	r8, r8, #1
	b	.L1852
.L1890:
	cmp	fp, #0
	bne	.L1855
	mov	r3, #6
	uxth	r6, r7
	str	r3, [sp, #12]
	mov	r3, #1
	str	r3, [sp, #8]
	b	.L1856
.L1855:
	ldr	r2, .L1894+12
	ldrh	r3, [r2, #-2]
	str	r3, [sp, #8]
	ldrb	r3, [r9, #-2744]	@ zero_extendqisi2
	cmp	r3, #0
	ldreq	r3, [sp, #8]
	moveq	r6, #1
	movne	r6, #1
	strne	r6, [sp, #12]
	moveq	r3, r3, lsr #2
	streq	r3, [sp, #12]
.L1856:
	ldr	r10, .L1894
	mov	r7, #0
.L1865:
	mov	r8, #0
	mov	r5, r8
.L1857:
	ldr	r3, .L1894+4
	ldr	r9, .L1894
	ldrh	r2, [r3]
	uxth	r3, r8
	cmp	r2, r3
	bls	.L1891
	mov	r2, #36
	ldr	r1, [r10, #-1492]
	mul	r2, r2, r3
	add	r3, r10, r3
	mov	r0, #0
	str	r0, [r1, r2]
	ldr	r1, [sp, #4]
	ldrb	r0, [r3, #-1708]	@ zero_extendqisi2
	bl	V2P_block
	cmp	fp, #0
	mov	r9, r0
	beq	.L1858
	bl	IsBlkInVendorPart
	cmp	r0, #0
	bne	.L1859
.L1858:
	mov	r0, r9
	bl	FtlBbmIsBadBlock
	cmp	r0, #0
	bne	.L1859
	ldr	r2, [r10, #-1492]
	mov	r3, #36
	add	r9, r7, r9, asl #10
	mla	r2, r3, r5, r2
	ldr	r3, [r10, #-1456]
	str	r3, [r2, #8]
	ldr	r3, .L1894+8
	str	r9, [r2, #4]
	ldrh	r3, [r3]
	mul	r3, r3, r5
	add	r5, r5, #1
	uxth	r5, r5
	add	r1, r3, #3
	cmp	r3, #0
	movlt	r3, r1
	ldr	r1, [r10, #-1452]
	bic	r3, r3, #3
	add	r3, r1, r3
	str	r3, [r2, #12]
.L1859:
	add	r8, r8, #1
	b	.L1857
.L1891:
	cmp	r5, #0
	beq	.L1869
	ldr	r2, .L1894
	mov	r3, #0
	mov	r1, r5
	mov	r8, #0
	strb	r3, [r2, #-1874]
	mov	r3, #1
	ldr	r0, [r2, #-1492]
	mov	r2, r6
	bl	FlashProgPages
	ldr	r3, .L1894
	mov	r1, #36
	ldrb	r2, [sp, #16]	@ zero_extendqisi2
	strb	r2, [r3, #-1874]
.L1862:
	uxth	r3, r8
	cmp	r3, r5
	bcs	.L1892
	mul	r3, r1, r8
	ldr	r2, .L1894
	ldr	r2, [r2, #-1492]
	add	r0, r2, r3
	ldr	r3, [r2, r3]
	cmp	r3, #0
	beq	.L1863
	ldr	r0, [r0, #4]
	add	r4, r4, #1
	str	r1, [sp, #20]
	ubfx	r0, r0, #10, #16
	uxth	r4, r4
	bl	FtlBbmMapBadBlock
	ldr	r1, [sp, #20]
.L1863:
	add	r8, r8, #1
	b	.L1862
.L1892:
	ldr	r3, [sp, #12]
	add	r7, r7, r3
	ldr	r3, [sp, #8]
	uxth	r7, r7
	cmp	r7, r3
	bcc	.L1865
	mov	r7, #0
	mov	r8, #36
.L1866:
	uxth	r3, r7
	cmp	r3, r5
	bcs	.L1893
	cmp	fp, #0
	beq	.L1867
	mul	r3, r8, r7
	ldr	r2, [r9, #-1492]
	add	r1, r2, r3
	ldr	r3, [r2, r3]
	cmp	r3, #0
	bne	.L1867
	ldr	r0, [r1, #4]
	mov	r1, #1
	ubfx	r0, r0, #10, #16
	bl	FtlFreeSysBlkQueueIn
.L1867:
	add	r7, r7, #1
	b	.L1866
.L1893:
	adds	r3, fp, #0
	ldr	r2, [sp, #4]
	movne	r3, #1
	cmp	r2, #63
	orrls	r3, r3, #1
	cmp	r3, #0
	beq	.L1869
	ldr	r0, [r9, #-1492]
	mov	r1, r6
	mov	r2, r5
	bl	FlashEraseBlocks
.L1869:
	mov	r0, r4
.L1845:
	add	sp, sp, #28
	@ sp needed
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L1895:
	.align	2
.L1894:
	.word	.LANCHOR2
	.word	.LANCHOR2-1736
	.word	.LANCHOR2-1656
	.word	.LANCHOR2-1664
	.fnend
	.size	FtlLowFormatEraseBlock, .-FtlLowFormatEraseBlock
	.align	2
	.global	FtlFreeSysBLkSort
	.type	FtlFreeSysBLkSort, %function
FtlFreeSysBLkSort:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	ldr	r3, .L1909
	ldrh	ip, [r3, #28]
	ldr	r3, .L1909+4
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
.L1898:
	uxth	lr, r0
	add	r0, r0, #1
	cmp	lr, ip
	bge	.L1908
	add	lr, r3, r1, asl #1
	add	r1, r1, #1
	ubfx	r1, r1, #0, #10
	ldrh	r4, [lr, #8]
	add	lr, r3, r2, asl #1
	strh	r4, [lr, #8]	@ movhi
	mov	r4, #1
	add	r2, r2, r4
	ubfx	r2, r2, #0, #10
	b	.L1898
.L1908:
	cmp	r4, #0
	strneh	r1, [r3, #2]	@ movhi
	strneh	r2, [r3, #4]	@ movhi
	ldmfd	sp!, {r4, pc}
.L1910:
	.align	2
.L1909:
	.word	.LANCHOR2+816
	.word	.LANCHOR2-1240
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
	ldr	r6, .L1922
	ldr	r5, .L1922+4
	mov	r7, r6
.L1912:
	ldrh	r1, [r5, #6]
	cmp	r1, #0
	beq	.L1913
	ldrh	r3, [r5, #2]
	sub	r1, r1, #1
	ldr	r9, [r6, #-1280]
	strh	r1, [r5, #6]	@ movhi
	add	r2, r5, r3, asl #1
	cmp	r9, #0
	add	r3, r3, #1
	ubfx	r3, r3, #0, #10
	ldrh	r4, [r2, #8]
	strh	r3, [r5, #2]	@ movhi
	bne	.L1914
	mov	r0, r4
	bl	P2V_block_in_plane
	mov	r3, r4, asl #10
	mov	r8, r0
	ldr	r0, [r6, #-1492]
	str	r3, [r0, #4]
	ldrb	r3, [r6, #-2744]	@ zero_extendqisi2
	cmp	r3, #0
	beq	.L1915
	mov	r1, r9
	mov	r2, #1
	bl	FlashEraseBlocks
.L1915:
	mov	r1, #1
	ldr	r0, [r7, #-1492]
	mov	r2, r1
	bl	FlashEraseBlocks
	ldr	r1, [r7, #-1416]
	mov	r3, r8, asl #1
	ldrh	r2, [r1, r3]
	add	r2, r2, #1
	strh	r2, [r1, r3]	@ movhi
	ldr	r3, [r7, #-1576]
	add	r3, r3, #1
	str	r3, [r7, #-1576]
	b	.L1914
.L1913:
	ldr	r0, .L1922+8
	bl	printk
.L1916:
	b	.L1916
.L1914:
	sub	r3, r4, #1
	movw	r2, #65533
	uxth	r3, r3
	cmp	r3, r2
	bls	.L1917
	ldr	r3, .L1922+4
	mov	r1, r4
	ldr	r0, .L1922+12
	ldrh	r2, [r3, #6]
	bl	printk
	b	.L1912
.L1917:
	mov	r0, r4
	ldmfd	sp!, {r3, r4, r5, r6, r7, r8, r9, pc}
.L1923:
	.align	2
.L1922:
	.word	.LANCHOR2
	.word	.LANCHOR2-1240
	.word	.LC37
	.word	.LC38
	.fnend
	.size	FtlFreeSysBlkQueueOut, .-FtlFreeSysBlkQueueOut
	.align	2
	.global	test_node_in_list
	.type	test_node_in_list, %function
test_node_in_list:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	ldr	r3, .L1930
	str	lr, [sp, #-4]!
	.save {lr}
	movw	lr, #65535
	ldr	ip, [r3, #-1356]
	ldr	r2, [r0]
	ldr	r3, .L1930+4
	rsb	r0, ip, r2
	mov	r0, r0, asr #1
	mul	r3, r3, r0
	mov	r0, #6
	uxth	r3, r3
.L1926:
	cmp	r1, r3
	beq	.L1927
	ldrh	r3, [r2]
	cmp	r3, lr
	beq	.L1928
	mla	r2, r0, r3, ip
	b	.L1926
.L1927:
	mov	r0, #1
	ldr	pc, [sp], #4
.L1928:
	mov	r0, #0
	ldr	pc, [sp], #4
.L1931:
	.align	2
.L1930:
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
	ldr	r2, .L1949
	sub	r5, r2, #1728
	ldrh	r3, [r5]
	cmp	r3, r0
	bls	.L1934
	mov	lr, #6
	ldr	r6, [r2, #-1356]
	mul	lr, lr, r0
	mvn	ip, #0
	add	r1, r6, lr
	strh	ip, [r1, #2]	@ movhi
	strh	ip, [r6, lr]	@ movhi
	ldr	r3, [r2, #864]
	cmp	r3, #0
	beq	.L1948
	ldr	r8, [r2, #-1408]
	mov	r4, r0, asl #1
	mov	r10, r2
	ldrh	r2, [r1, #4]
	ldrh	r5, [r5]
	ldrh	r7, [r8, r4]
	cmp	r2, #0
	str	r5, [sp]
	mulne	ip, r2, r7
	ldr	r7, [r10, #-1356]
	ldr	r2, .L1949+4
	rsb	r9, r7, r3
	mov	r9, r9, asr #1
	mul	r2, r2, r9
	ldr	r9, [r10, #-1416]
	add	r4, r9, r4
	str	r4, [sp, #4]
	mov	r4, #0
	uxth	r2, r2
.L1943:
	add	r4, r4, #1
	ldr	r5, [sp]
	uxth	r4, r4
	cmp	r4, r5
	movls	r5, #0
	movhi	r5, #1
	cmp	r0, r2
	orreq	r5, r5, #1
	cmp	r5, #0
	bne	.L1934
	mov	r10, r2, asl #1
	ldrh	r5, [r3, #4]
	ldrh	fp, [r8, r10]
	cmp	r5, #0
	mvneq	r5, #0
	mulne	r5, r5, fp
	cmp	r5, ip
	bne	.L1939
	ldr	r5, [sp, #4]
	ldrh	r10, [r9, r10]
	ldrh	r5, [r5]
	cmp	r10, r5
	bcc	.L1941
	b	.L1940
.L1939:
	bhi	.L1940
.L1941:
	ldrh	r5, [r3]
	movw	r10, #65535
	cmp	r5, r10
	streqh	r2, [r1, #2]	@ movhi
	streqh	r0, [r3]	@ movhi
	ldreq	r3, .L1949
	streq	r1, [r3, #868]
	beq	.L1934
.L1942:
	mov	r3, #6
	mov	r2, r5
	mla	r3, r3, r5, r7
	b	.L1943
.L1940:
	strh	r2, [r6, lr]	@ movhi
	ldrh	r2, [r3, #2]
	strh	r2, [r1, #2]	@ movhi
	ldr	r2, .L1949
	ldr	ip, [r2, #864]
	cmp	r3, ip
	bne	.L1944
	strh	r0, [r3, #2]	@ movhi
.L1948:
	str	r1, [r2, #864]
	b	.L1934
.L1944:
	ldrh	ip, [r3, #2]
	ldr	r1, [r2, #-1356]
	mov	r2, #6
	mul	r2, r2, ip
	strh	r0, [r1, r2]	@ movhi
	strh	r0, [r3, #2]	@ movhi
.L1934:
	mov	r0, #0
	add	sp, sp, #12
	@ sp needed
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L1950:
	.align	2
.L1949:
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
	ldr	r2, .L1953
	ldrh	r3, [r2]
	add	r3, r3, #1
	strh	r3, [r2]	@ movhi
	ldmfd	sp!, {r3, pc}
.L1954:
	.align	2
.L1953:
	.word	.LANCHOR2+872
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
	beq	.L1956
	ldr	r2, .L1964
	mov	lr, #6
	mul	r6, lr, r0
	mvn	r3, #0
	ldr	r7, [r2, #-1356]
	mov	r5, r2
	add	ip, r7, r6
	strh	r3, [ip, #2]	@ movhi
	strh	r3, [r7, r6]	@ movhi
	ldr	r3, [r2, #876]
	cmp	r3, #0
	beq	.L1963
	ldr	r9, [r2, #-1416]
	mov	r2, r0, asl #1
	ldr	r8, [r5, #-1356]
	rsb	r1, r8, r3
	ldrh	r10, [r9, r2]
	ldr	r2, .L1964+4
	mov	r1, r1, asr #1
	mul	r1, r2, r1
	uxth	r2, r1
.L1960:
	mov	r1, r2, asl #1
	ldrh	r1, [r9, r1]
	cmp	r1, r10
	bcs	.L1958
	ldrh	r1, [r3]
	cmp	r1, r4
	streqh	r2, [ip, #2]	@ movhi
	streqh	r0, [r3]	@ movhi
	beq	.L1956
.L1959:
	mla	r3, lr, r1, r8
	mov	r2, r1
	b	.L1960
.L1958:
	ldrh	r1, [r3, #2]
	strh	r1, [ip, #2]	@ movhi
	strh	r2, [r7, r6]	@ movhi
	ldr	r1, [r5, #876]
	ldr	r2, .L1964
	cmp	r3, r1
	bne	.L1961
	strh	r0, [r3, #2]	@ movhi
.L1963:
	str	ip, [r2, #876]
	b	.L1956
.L1961:
	ldrh	ip, [r3, #2]
	ldr	r1, [r2, #-1356]
	mov	r2, #6
	mul	r2, r2, ip
	strh	r0, [r1, r2]	@ movhi
	strh	r0, [r3, #2]	@ movhi
.L1956:
	mov	r0, #0
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, pc}
.L1965:
	.align	2
.L1964:
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
	ldr	r2, .L1968
	ldrh	r3, [r2]
	add	r3, r3, #1
	strh	r3, [r2]	@ movhi
	ldmfd	sp!, {r3, pc}
.L1969:
	.align	2
.L1968:
	.word	.LANCHOR2+880
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
	ldr	r4, .L1976
	movw	r5, #65535
	mul	r1, ip, r1
	ldr	r3, [r0]
	ldr	r2, [r4, #-1356]
	add	lr, r2, r1
	cmp	lr, r3
	ldrh	r3, [r2, r1]
	bne	.L1971
	cmp	r3, r5
	mlane	r3, ip, r3, r2
	moveq	r3, #0
	streq	r3, [r0]
	strne	r3, [r0]
	mvnne	r0, #0
	strneh	r0, [r3, #2]	@ movhi
	b	.L1973
.L1971:
	cmp	r3, r5
	ldrh	r0, [lr, #2]
	bne	.L1974
	cmp	r0, r3
	mulne	r0, ip, r0
	mvnne	r3, #0
	strneh	r3, [r2, r0]	@ movhi
	b	.L1973
.L1974:
	mla	r3, ip, r3, r2
	strh	r0, [r3, #2]	@ movhi
	ldrh	r5, [lr, #2]
	ldrh	r0, [r2, r1]
	ldr	r3, [r4, #-1356]
	mul	ip, ip, r5
	strh	r0, [r3, ip]	@ movhi
.L1973:
	mvn	r3, #0
	mov	r0, #0
	strh	r3, [r2, r1]	@ movhi
	strh	r3, [lr, #2]	@ movhi
	ldmfd	sp!, {r4, r5, pc}
.L1977:
	.align	2
.L1976:
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
	beq	.L1984
	ldr	r2, .L1987
	movw	ip, #65535
	stmfd	sp!, {r4, lr}
	.save {r4, lr}
	mov	lr, #6
	ldr	r4, [r2, #-1356]
.L1980:
	cmp	r1, #0
	bne	.L1981
.L1983:
	rsb	r4, r4, r3
	ldr	r3, .L1987+4
	mov	r4, r4, asr #1
	mul	r4, r3, r4
	uxth	r4, r4
	mov	r1, r4
	bl	List_remove_node
	mov	r0, r4
	ldmfd	sp!, {r4, pc}
.L1981:
	ldrh	r2, [r3]
	cmp	r2, ip
	beq	.L1983
	sub	r1, r1, #1
	mla	r3, lr, r2, r4
	uxth	r1, r1
	b	.L1980
.L1984:
	movw	r0, #65535
	bx	lr
.L1988:
	.align	2
.L1987:
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
	ldr	r2, .L1996
	ldr	r3, [r2, #864]
	cmp	r3, #0
	ldrne	r1, [r2, #-1356]
	movne	ip, #6
	movwne	r2, #65535
	beq	.L1994
.L1991:
	cmp	r0, #0
	beq	.L1992
	ldrh	r3, [r3]
	cmp	r3, r2
	subne	r0, r0, #1
	mlane	r3, ip, r3, r1
	uxthne	r0, r0
	bne	.L1991
.L1994:
	movw	r0, #65535
	bx	lr
.L1992:
	rsb	r3, r1, r3
	ldr	r0, .L1996+4
	mov	r3, r3, asr #1
	mul	r0, r0, r3
	uxth	r0, r0
	bx	lr
.L1997:
	.align	2
.L1996:
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
	ldr	r3, .L2007
	add	r2, r3, #884
	ldrh	r2, [r2]
	cmp	r2, r0
	beq	.L2006
	add	r2, r3, #932
	ldrh	r2, [r2]
	cmp	r2, r0
	beq	.L2006
	add	r2, r3, #980
	ldrh	r2, [r2]
	cmp	r2, r0
	beq	.L2006
	stmfd	sp!, {r4, lr}
	.save {r4, lr}
	mov	lr, #6
	mul	lr, lr, r0
	ldr	r1, [r3, #-1356]
	ldr	r2, [r3, #864]
	add	ip, r1, lr
	cmp	ip, r2
	beq	.L1999
	ldr	r4, [r3, #-1408]
	mov	r3, r0, asl #1
	ldrh	r2, [r4, r3]
	ldrh	r3, [ip, #4]
	ldrh	ip, [ip, #2]
	cmp	r3, #0
	mulne	r2, r3, r2
	mvneq	r2, #0
	movw	r3, #65535
	cmp	ip, r3
	bne	.L2001
	ldrh	r3, [r1, lr]
	cmp	r3, ip
	beq	.L1999
.L2001:
	mov	r3, #6
	mul	ip, r3, ip
	ldr	r3, .L2007+4
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
	bcs	.L1999
	mov	r4, r0
	ldr	r0, .L2007+8
	mov	r1, r4
	bl	List_remove_node
	ldr	r2, .L2007+12
	mov	r0, r4
	ldrh	r3, [r2]
	sub	r3, r3, #1
	strh	r3, [r2]	@ movhi
	bl	INSERT_DATA_LIST
.L1999:
	mov	r0, #0
	ldmfd	sp!, {r4, pc}
.L2006:
	mov	r0, #0
	bx	lr
.L2008:
	.align	2
.L2007:
	.word	.LANCHOR2
	.word	-1431655765
	.word	.LANCHOR2+864
	.word	.LANCHOR2+872
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
.L2010:
	ldrh	r1, [r4, #6]
	uxth	r3, r2
	cmp	r1, r3
	bls	.L2030
	ldr	r0, [r6, r3, asl #2]
	mov	r1, #0
	ubfx	r0, r0, #10, #16
.L2011:
	ldrh	ip, [r4, #10]
	uxth	r3, r1
	cmp	ip, r3
	bls	.L2031
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
	b	.L2011
.L2031:
	add	r2, r2, #1
	b	.L2010
.L2030:
	ldr	r3, .L2033
	mov	r8, #0
	mov	r1, r8
	mov	fp, r8
	ldrh	r2, [r3, #-2]
	ldrh	r3, [r4]
	mov	r3, r3, asl #1
	strh	r2, [r5, r3]	@ movhi
	ldrh	r9, [r5]
.L2015:
	ldrh	r3, [r4, #10]
	uxth	r6, r8
	cmp	r3, r6
	bls	.L2032
	mov	r2, r6, asl #1
	ldrh	r3, [r5, r2]
	cmp	r9, r3
	bls	.L2016
	ldrh	r0, [r7, r2]
	add	r10, r7, r2
	cmp	r0, #0
	bne	.L2017
	b	.L2018
.L2016:
	cmp	r3, #0
	bne	.L2018
	ldrh	r0, [r7, r2]
	add	r10, r7, r2
	cmp	r0, #0
	movne	r6, r1
	beq	.L2018
	b	.L2020
.L2017:
	cmp	r3, #0
	movne	r1, r6
	movne	r9, r3
	bne	.L2018
	mov	r9, r3
.L2020:
	mov	r1, #1
	bl	FtlFreeSysBlkQueueIn
	strh	fp, [r10]	@ movhi
	ldrh	r3, [r4, #8]
	mov	r1, r6
	sub	r3, r3, #1
	strh	r3, [r4, #8]	@ movhi
.L2018:
	add	r8, r8, #1
	b	.L2015
.L2032:
	mov	r0, r1
	ldmfd	sp!, {r3, r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L2034:
	.align	2
.L2033:
	.word	.LANCHOR2-1664
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
.L2036:
	uxth	r5, r3
	cmp	r5, r1
	bcs	.L2039
	mov	r7, r2
	add	r3, r3, #1
	ldrh	r6, [r7]
	add	r2, r2, #2
	cmp	r6, #0
	bne	.L2036
	mov	r4, r0
	bl	FtlFreeSysBlkQueueOut
	movw	r2, #65533
	sub	r3, r0, #1
	mov	r1, r0
	strh	r0, [r7]	@ movhi
	uxth	r3, r3
	cmp	r3, r2
	bls	.L2037
	ldr	r3, .L2043
	ldr	r0, .L2043+4
	ldrh	r2, [r3, #6]
	bl	printk
.L2038:
	b	.L2038
.L2037:
	ldr	r3, [r4, #28]
	strh	r6, [r4, #2]	@ movhi
	add	r3, r3, #1
	str	r3, [r4, #28]
	ldrh	r3, [r4, #8]
	strh	r5, [r4]	@ movhi
	add	r3, r3, #1
	strh	r3, [r4, #8]	@ movhi
.L2039:
	mov	r0, #0
	ldmfd	sp!, {r3, r4, r5, r6, r7, pc}
.L2044:
	.align	2
.L2043:
	.word	.LANCHOR2-1240
	.word	.LC39
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
	ldr	fp, .L2064
	mov	r8, r1
	ldr	r9, .L2064+4
	mov	r5, #0
	str	r2, [sp]
	mov	r10, fp
.L2046:
	ldr	r3, [fp, #-1592]
	ldr	r6, .L2064
	add	r3, r3, #1
	str	r3, [fp, #-1592]
	ldrh	r3, [r9]
	ldrh	r2, [r4, #2]
	sub	r3, r3, #1
	cmp	r2, r3
	bge	.L2047
	ldrh	r2, [r4]
	movw	r3, #65535
	cmp	r2, r3
	bne	.L2048
.L2047:
	mov	r0, r4
	bl	Ftl_write_map_blk_to_last_page
.L2048:
	ldr	r1, [r10, #-1280]
	cmp	r1, #0
	bne	.L2049
	ldrh	r3, [r4]
	ldr	r2, [r4, #12]
	ldr	r0, [fp, #-1444]
	mov	r3, r3, asl #1
	ldrh	r7, [r2, r3]
	mov	r2, #16
	ldrh	r3, [r4, #2]
	str	r0, [fp, #-1264]
	orr	r3, r3, r7, asl #10
	str	r3, [fp, #-1272]
	ldr	r3, [sp]
	str	r3, [fp, #-1268]
	bl	ftl_memset
	ldr	r2, [r4, #28]
	ldr	r3, [fp, #-1264]
	str	r2, [r3, #4]
	strh	r8, [r3, #8]	@ movhi
	ldrh	r2, [r4, #4]
	strh	r7, [r3, #2]	@ movhi
	strh	r2, [r3]	@ movhi
	ldr	r2, .L2064+8
	ldrb	r2, [r2]	@ zero_extendqisi2
	cmp	r2, #0
	beq	.L2050
	ldr	r2, .L2064+12
	ldr	r0, [fp, #-1268]
	str	r3, [sp, #4]
	ldrh	r1, [r2]
	bl	js_hash
	ldr	r3, [sp, #4]
	str	r0, [r3, #12]
.L2050:
	mov	r1, #1
	ldr	r0, .L2064+16
	mov	r2, r1
	mov	r3, r1
	bl	FlashProgPages
	ldrh	r3, [r4, #2]
	add	r3, r3, #1
	uxth	r3, r3
	strh	r3, [r4, #2]	@ movhi
	ldr	r2, [r10, #-1276]
	cmn	r2, #1
	bne	.L2051
	ldr	r0, .L2064+20
	add	r5, r5, #1
	ldr	r1, [fp, #-1272]
	bl	printk
	ldrh	r3, [r4, #2]
	uxth	r5, r5
	cmp	r3, #2
	ldrlsh	r3, [r9]
	subls	r3, r3, #1
	strlsh	r3, [r4, #2]	@ movhi
	cmp	r5, #3
	bls	.L2046
	ldr	r0, .L2064+24
	mov	r2, r5
	ldr	r1, [r6, #-1272]
	bl	printk
	mov	r3, #1
	str	r3, [r6, #-1280]
	b	.L2049
.L2051:
	cmp	r2, #0
	strneh	r7, [r4, #40]	@ movhi
	cmp	r2, #256
	cmpne	r3, #1
	beq	.L2055
	ldr	r3, [r4, #36]
	cmp	r3, #0
	beq	.L2056
.L2055:
	mov	r3, #0
	str	r3, [r4, #36]
	b	.L2046
.L2056:
	ldr	r2, [r6, #-1272]
	ldr	r3, [r4, #24]
	str	r2, [r3, r8, asl #2]
.L2049:
	mov	r0, #0
	add	sp, sp, #12
	@ sp needed
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L2065:
	.align	2
.L2064:
	.word	.LANCHOR2
	.word	.LANCHOR2-1666
	.word	.LANCHOR0
	.word	.LANCHOR2-1658
	.word	.LANCHOR2-1276
	.word	.LC40
	.word	.LC41
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
	ldr	r8, .L2080
	cmp	r2, r3
	blt	.L2067
	uxth	r0, r0
	mov	r0, r0, asl #1
	ldrh	r10, [r5, r0]
	cmp	r10, #0
	beq	.L2067
	ldr	r3, [r4, #32]
	cmp	r3, #0
	bne	.L2067
	mov	r2, #1
	str	r2, [r4, #32]
	strh	r3, [r5, r0]	@ movhi
	ldrh	r3, [r4, #8]
	ldrh	r2, [r4, #2]
	sub	r3, r3, #1
	strh	r3, [r4, #8]	@ movhi
	ldrh	r3, [r8, #-2]
	cmp	r2, r3
	bcc	.L2068
	mov	r0, r4
	bl	ftl_map_blk_alloc_new_blk
.L2068:
	ldr	r5, .L2080+4
	mov	fp, #0
.L2069:
	ldrh	r3, [r4, #6]
	uxth	r6, fp
	cmp	r3, r6
	bls	.L2079
	ldr	r3, [r7, r6, asl #2]
	add	ip, r7, r6, asl #2
	cmp	r10, r3, lsr #10
	bne	.L2070
	ldr	r3, [r5, #-1472]
	mov	r1, #1
	ldr	r9, [r5, #-1444]
	mov	r2, r1
	ldr	r0, .L2080+8
	str	r3, [r5, #-1268]
	str	r9, [r5, #-1264]
	ldr	r3, [r7, r6, asl #2]
	str	ip, [sp, #4]
	str	r3, [r5, #-1272]
	bl	FlashReadPages
	ldr	r3, [r5, #-1276]
	cmn	r3, #1
	ldr	r3, .L2080+4
	ldr	ip, [sp, #4]
	bne	.L2071
.L2073:
	mov	r2, #0
	ldr	r0, .L2080+12
	str	r2, [ip]
	ldr	r1, [r3, #-1272]
	ldrh	r2, [r9, #8]
	str	r3, [sp, #4]
	bl	printk
	mov	r2, #1
	ldr	r3, [sp, #4]
	str	r2, [r3, #-1280]
	b	.L2072
.L2071:
	ldrh	r1, [r9, #8]
	cmp	r1, r6
	bne	.L2073
	ldrh	r0, [r9]
	ldrh	r2, [r4, #4]
	cmp	r0, r2
	bne	.L2073
	mov	r0, r4
	ldr	r2, [r5, #-1268]
	bl	FtlMapWritePage
.L2070:
	add	fp, fp, #1
	b	.L2069
.L2079:
	mov	r0, r10
	mov	r1, #1
	bl	FtlFreeSysBlkQueueIn
	mov	r3, #0
	str	r3, [r4, #32]
.L2067:
	ldrh	r2, [r4, #2]
	ldrh	r3, [r8, #-2]
	cmp	r2, r3
	bcc	.L2072
	mov	r0, r4
	bl	ftl_map_blk_alloc_new_blk
.L2072:
	mov	r0, #0
	add	sp, sp, #12
	@ sp needed
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L2081:
	.align	2
.L2080:
	.word	.LANCHOR2-1664
	.word	.LANCHOR2
	.word	.LANCHOR2-1276
	.word	.LC42
	.fnend
	.size	ftl_map_blk_gc, .-ftl_map_blk_gc
	.align	2
	.global	Ftl_write_map_blk_to_last_page
	.type	Ftl_write_map_blk_to_last_page, %function
Ftl_write_map_blk_to_last_page:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	stmfd	sp!, {r3, r4, r5, r6, r7, r8, r9, lr}
	.save {r3, r4, r5, r6, r7, r8, r9, lr}
	ldr	r5, .L2094
	ldr	r7, [r0, #12]
	ldr	r8, [r0, #24]
	ldr	r6, [r5, #-1280]
	cmp	r6, #0
	bne	.L2083
	ldrh	r3, [r0]
	movw	r2, #65535
	mov	r4, r0
	cmp	r3, r2
	bne	.L2084
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
	b	.L2083
.L2084:
	mov	r3, r3, asl #1
	mov	r1, #255
	ldrh	r9, [r7, r3]
	ldrh	r3, [r0, #2]
	ldr	r7, [r5, #-1444]
	orr	r3, r3, r9, asl #10
	str	r3, [r5, #-1272]
	ldr	r3, [r5, #-1476]
	str	r7, [r5, #-1264]
	str	r3, [r5, #-1268]
	ldr	r3, [r0, #28]
	str	r3, [r7, #4]
	ldr	r3, .L2094+4
	strh	r3, [r7, #8]	@ movhi
	ldrh	r3, [r0, #4]
	strh	r9, [r7, #2]	@ movhi
	strh	r3, [r7]	@ movhi
	sub	r3, r5, #1664
	ldr	r0, [r5, #-1476]
	ldrh	r2, [r3, #-2]
	mov	r2, r2, asl #3
	bl	ftl_memset
	mov	r3, r6
.L2085:
	ldrh	r1, [r4, #6]
	uxth	r2, r6
	cmp	r1, r2
	bls	.L2093
	ldr	r1, [r8, r2, asl #2]
	cmp	r9, r1, lsr #10
	bne	.L2086
	add	r3, r3, #1
	ldr	r1, [r5, #-1476]
	uxth	r3, r3
	str	r2, [r1, r3, asl #3]
	ldr	r1, [r8, r2, asl #2]
	ldr	r2, [r5, #-1476]
	add	r2, r2, r3, asl #3
	str	r1, [r2, #4]
.L2086:
	add	r6, r6, #1
	b	.L2085
.L2093:
	ldr	r3, .L2094+8
	ldrb	r3, [r3]	@ zero_extendqisi2
	cmp	r3, #0
	beq	.L2088
	ldr	r3, .L2094+12
	ldr	r0, [r5, #-1268]
	ldrh	r1, [r3, #-10]
	bl	js_hash
	str	r0, [r7, #12]
.L2088:
	mov	r1, #1
	mov	r3, #0
	ldr	r0, .L2094+16
	mov	r2, r1
	bl	FlashProgPages
	ldrh	r3, [r4, #2]
	mov	r0, r4
	add	r3, r3, #1
	strh	r3, [r4, #2]	@ movhi
	bl	ftl_map_blk_gc
.L2083:
	mov	r0, #0
	ldmfd	sp!, {r3, r4, r5, r6, r7, r8, r9, pc}
.L2095:
	.align	2
.L2094:
	.word	.LANCHOR2
	.word	-1291
	.word	.LANCHOR0
	.word	.LANCHOR2-1648
	.word	.LANCHOR2-1276
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
	ldr	r5, .L2098
	mul	r4, r4, r0
	add	r0, r5, #1024
	ldr	r3, [r5, #-1364]
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
	ldmfd	sp!, {r3, r4, r5, pc}
.L2099:
	.align	2
.L2098:
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
	ldr	r3, .L2116
	mov	r1, #0
	stmfd	sp!, {r4, r5, r6, lr}
	.save {r4, r5, r6, lr}
	sub	r2, r3, #1616
	ldr	r3, [r3, #-1364]
	mov	ip, #12
	ldrh	r2, [r2, #-14]
	movw	lr, #65535
.L2101:
	uxth	r0, r1
	cmp	r0, r2
	bcs	.L2113
	add	r1, r1, #1
	mla	r4, ip, r1, r3
	ldrh	r4, [r4, #-12]
	cmp	r4, lr
	bne	.L2101
	ldmfd	sp!, {r4, r5, r6, pc}
.L2113:
	mov	r0, r2
	mov	r1, #0
	mov	ip, #-2147483648
	mov	r5, #12
.L2104:
	uxth	r4, r1
	cmp	r4, r2
	bcs	.L2114
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
	b	.L2104
.L2114:
	cmp	r0, r2
	ldmccfd	sp!, {r4, r5, r6, pc}
	ldr	r1, .L2116+4
	mov	r0, r2
	mvn	ip, #0
	ldrh	r5, [r1]
	mov	r1, #0
.L2107:
	uxth	lr, r1
	cmp	lr, r2
	bcs	.L2115
	ldr	r4, [r3, #4]
	cmp	r4, ip
	bcs	.L2108
	ldrh	r6, [r3]
	cmp	r6, r5
	movne	ip, r4
	movne	r0, lr
.L2108:
	add	r1, r1, #1
	add	r3, r3, #12
	b	.L2107
.L2115:
	ldmfd	sp!, {r4, r5, r6, pc}
.L2117:
	.align	2
.L2116:
	.word	.LANCHOR2
	.word	.LANCHOR2+1072
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
	ldr	r6, .L2135
	sub	r3, r6, #1648
	str	r3, [sp, #8]
	ldr	ip, [r6, #-1284]
	ldrh	r10, [r3, #-12]
	cmp	r0, ip
	bcs	.L2119
	add	r10, r10, #7
	mov	fp, r6
	mov	r6, r0, lsr r10
	sub	r3, fp, #1616
	str	r2, [sp, #12]
	mov	r9, r1
	ldrh	r2, [r3, #-14]
	uxth	r6, r6
	str	r0, [sp, #4]
	mov	r3, #0
	ldr	r4, [fp, #-1364]
	mov	r1, #12
	b	.L2120
.L2119:
	cmp	r2, #0
	mvn	r0, #0
	streq	r0, [r1]
	b	.L2121
.L2125:
	add	r3, r3, #1
	mla	r0, r1, r3, r4
	ldrh	r0, [r0, #-12]
	cmp	r0, r6
	beq	.L2122
.L2120:
	uxth	r7, r3
	cmp	r7, r2
	bcc	.L2125
	bl	select_l2p_ram_region
	mov	r5, #12
	movw	r2, #65535
	mul	r5, r5, r0
	mov	r7, r0
	add	r3, r4, r5
	ldrh	r1, [r4, r5]
	cmp	r1, r2
	bne	.L2134
.L2126:
	ldr	r3, [fp, #-1376]
	ldr	r4, .L2135
	ldr	r8, [r3, r6, asl #2]
	cmp	r8, #0
	bne	.L2127
	ldr	r2, [r4, #-1364]
	mov	r1, #255
	ldr	r3, [sp, #8]
	add	r2, r2, r5
	ldr	r0, [r2, #8]
	ldrh	r2, [r3, #-10]
	bl	ftl_memset
	ldr	r2, [r4, #-1364]
	strh	r6, [r2, r5]	@ movhi
	ldr	r2, [r4, #-1364]
	add	r5, r2, r5
	str	r8, [r5, #4]
.L2122:
	ldr	r3, [sp, #4]
	mvn	r2, #0
	bic	r10, r3, r2, asl r10
	ldr	r3, [sp, #12]
	cmp	r3, #0
	uxth	r10, r10
	mov	r3, #12
	bne	.L2123
	ldr	r2, [fp, #-1364]
	mla	r3, r3, r7, r2
	ldr	r3, [r3, #8]
	ldr	r3, [r3, r10, asl #2]
	str	r3, [r9]
	b	.L2124
.L2123:
	mul	r3, r3, r7
	ldr	r2, [fp, #-1364]
	ldr	r1, [r9]
	add	r2, r2, r3
	ldr	r2, [r2, #8]
	str	r1, [r2, r10, asl #2]
	ldr	r2, [fp, #-1364]
	add	r3, r2, r3
	ldr	r2, [r3, #4]
	orr	r2, r2, #-2147483648
	str	r2, [r3, #4]
	ldr	r3, .L2135+4
	strh	r6, [r3]	@ movhi
.L2124:
	ldr	r2, [fp, #-1364]
	mov	r3, #12
	mov	r0, #0
	mla	r7, r3, r7, r2
	ldr	r3, [r7, #4]
	cmn	r3, #1
	addne	r3, r3, #1
	strne	r3, [r7, #4]
	b	.L2121
.L2134:
	ldr	r3, [r3, #4]
	cmp	r3, #0
	bge	.L2126
	bl	flush_l2p_region
	b	.L2126
.L2127:
	ldr	r2, [r4, #-1364]
	mov	r1, #1
	ldr	r0, .L2135+8
	add	r2, r2, r5
	str	r8, [r4, #-1272]
	ldr	r2, [r2, #8]
	str	r2, [r4, #-1268]
	ldr	r2, [r4, #-1444]
	str	r2, [r4, #-1264]
	mov	r2, r1
	bl	FlashReadPages
	ldr	r2, [r4, #-1264]
	ldrh	r2, [r2, #8]
	cmp	r2, r6
	beq	.L2128
	mov	r1, r6
	mov	r2, r8
	ldr	r0, .L2135+12
	bl	printk
	mov	r2, #4
	mov	r3, r2
	ldr	r0, .L2135+16
	ldr	r1, [r4, #-1264]
	bl	rknand_print_hex
	sub	r3, r4, #1632
	ldr	r0, .L2135+20
	mov	r2, #4
	ldrh	r3, [r3]
	ldr	r1, [r4, #-1376]
	bl	rknand_print_hex
	mov	r3, #1
	str	r3, [r4, #-1280]
	b	.L2129
.L2128:
	ldr	r2, [r4, #-1276]
	cmp	r2, #256
	bne	.L2129
	mov	r1, r6
	mov	r2, r8
	ldr	r0, .L2135+24
	bl	printk
	ldr	r3, [r4, #-1364]
	ldr	r0, .L2135+28
	mov	r1, r6
	add	r3, r3, r5
	ldr	r2, [r3, #8]
	bl	FtlMapWritePage
.L2129:
	ldr	r3, [fp, #-1364]
	mov	r1, #0
	add	r2, r3, r5
	str	r1, [r2, #4]
	strh	r6, [r3, r5]	@ movhi
	b	.L2122
.L2121:
	add	sp, sp, #20
	@ sp needed
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L2136:
	.align	2
.L2135:
	.word	.LANCHOR2
	.word	.LANCHOR2+1072
	.word	.LANCHOR2-1276
	.word	.LC43
	.word	.LC18
	.word	.LC44
	.word	.LC45
	.word	.LANCHOR2+1028
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
	ldr	r6, .L2148
	add	r2, r0, r1
	mov	r7, r0
	.pad #60
	sub	sp, sp, #60
	mov	r4, r1
	ldrh	r3, [r6, #-24]
	ldrh	r5, [r6, #-12]
	cmp	r2, r3
	mvnhi	r0, #0
	bhi	.L2138
	mov	r5, r7, lsr r5
	mov	r3, #0
	str	r3, [sp]
	add	r3, r6, #1648
	mov	r10, r5, asl #2
	str	r3, [sp, #4]
	mov	r8, r3
.L2139:
	cmp	r4, #0
	beq	.L2147
	ldr	r3, [sp, #4]
	mov	r0, r7
	ldr	r3, [r3, #-1380]
	ldr	ip, [r3, r10]
	ldr	r3, .L2148+4
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
	beq	.L2141
	ldr	r2, [r8, #-1468]
	mov	r1, #1
	add	r0, sp, #20
	str	ip, [sp, #24]
	str	r2, [sp, #28]
	mov	r2, #0
	str	r2, [sp, #32]
	mov	r2, r1
	bl	FlashReadPages
	b	.L2142
.L2141:
	ldr	r2, .L2148+8
	ldr	r0, [r8, #-1468]
	ldrh	r2, [r2]
	bl	ftl_memset
.L2142:
	mov	ip, r6, asl #9
	ldr	r0, [r8, #-1468]
	uxth	r9, r9
	mov	r1, fp
	mov	r2, ip
	str	ip, [sp, #8]
	add	r0, r0, r9, asl #9
	rsb	r4, r6, r4
	bl	ftl_memcpy
	mov	r1, r5
	ldr	r0, .L2148+12
	add	r5, r5, #1
	ldr	r2, [r8, #-1468]
	add	r7, r7, r6
	bl	FtlMapWritePage
	add	r10, r10, #4
	ldr	r3, [sp]
	cmn	r0, #1
	ldr	ip, [sp, #8]
	mvneq	r3, #0
	add	fp, fp, ip
	str	r3, [sp]
	b	.L2139
.L2147:
	ldr	r0, [sp]
.L2138:
	add	sp, sp, #60
	@ sp needed
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L2149:
	.align	2
.L2148:
	.word	.LANCHOR2-1648
	.word	.LANCHOR2-1662
	.word	.LANCHOR2-1658
	.word	.LANCHOR2+1076
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
	ldr	r6, .L2162
	add	r2, r0, r1
	mov	r8, r0
	.pad #60
	sub	sp, sp, #60
	mov	r7, r1
	ldrh	r3, [r6, #-24]
	ldrh	r5, [r6, #-12]
	cmp	r2, r3
	mvnhi	r0, #0
	bhi	.L2151
	add	r6, r6, #1648
	mov	r5, r8, lsr r5
	mov	r10, r6
	mov	r3, r5, asl #2
	str	r3, [sp, #4]
	mov	r3, #0
	str	r3, [sp]
.L2152:
	cmp	r7, #0
	beq	.L2161
	ldr	r2, [sp, #4]
	mov	r0, r8
	ldr	r3, [r6, #-1380]
	ldr	r3, [r3, r2]
	str	r3, [sp, #12]
	ldr	r3, .L2162+4
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
	beq	.L2154
	ldr	r2, [r10, #-1468]
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
	ldr	r2, [r10, #-1276]
	mvneq	r3, #0
	cmp	r2, #256
	str	r3, [sp]
	ldr	r3, [sp, #12]
	bne	.L2156
	mov	r1, r5
	mov	r2, r3
	ldr	r0, .L2162+8
	bl	printk
	ldr	r0, .L2162+12
	mov	r1, r5
	ldr	r2, [r6, #-1468]
	bl	FtlMapWritePage
.L2156:
	ldrh	r3, [sp, #8]
	mov	r0, r9
	ldr	r1, [r10, #-1468]
	mov	r2, fp
	add	r1, r1, r3, asl #9
	bl	ftl_memcpy
	b	.L2157
.L2154:
	mov	r0, r9
	mov	r1, r3
	mov	r2, fp
	bl	ftl_memset
.L2157:
	ldr	r3, [sp, #4]
	add	r5, r5, #1
	rsb	r7, r4, r7
	add	r8, r8, r4
	add	r3, r3, #4
	add	r9, r9, fp
	str	r3, [sp, #4]
	b	.L2152
.L2161:
	ldr	r0, [sp]
.L2151:
	add	sp, sp, #60
	@ sp needed
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L2163:
	.align	2
.L2162:
	.word	.LANCHOR2-1648
	.word	.LANCHOR2-1662
	.word	.LC46
	.word	.LANCHOR2+1076
	.fnend
	.size	FtlVendorPartRead, .-FtlVendorPartRead
	.align	2
	.global	FtlUpdateVaildLpn
	.type	FtlUpdateVaildLpn, %function
FtlUpdateVaildLpn:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	ldr	r3, .L2172
	add	r1, r3, #1120
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
	sub	r1, r3, #1728
	str	r2, [r3, #1124]
	movw	ip, #65535
	ldrh	r0, [r1]
	ldr	r2, [r3, #-1408]
	add	r0, r2, r0, asl #1
.L2166:
	cmp	r2, r0
	beq	.L2171
	ldrh	r1, [r2], #2
	cmp	r1, ip
	ldrne	lr, [r3, #1124]
	addne	r1, r1, lr
	strne	r1, [r3, #1124]
	b	.L2166
.L2171:
	ldr	pc, [sp], #4
.L2173:
	.align	2
.L2172:
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
	ldr	r4, .L2181
	mov	r2, #0
	str	r2, [r0, #36]
	ldr	r2, [r4, #-1280]
	cmp	r2, #0
	ldmnefd	sp!, {r4, r5, r6, pc}
	sub	r5, r5, #1
	mov	r6, r0
	ldr	r2, [r4, #-1444]
	ldr	r0, [r4, #-1472]
	uxth	r5, r5
	str	r2, [r4, #-1264]
	str	r0, [r4, #-1268]
	ldr	r3, [r3, r5, asl #2]
	cmp	r3, #0
	str	r3, [r4, #-1272]
	beq	.L2178
	mov	r1, #1
	ldr	r0, .L2181+4
	mov	r2, r1
	bl	FlashReadPages
	b	.L2179
.L2178:
	sub	r3, r4, #1648
	mov	r1, #255
	ldrh	r2, [r3, #-10]
	bl	ftl_memset
.L2179:
	mov	r0, r6
	mov	r1, r5
	ldr	r2, [r4, #-1268]
	ldmfd	sp!, {r4, r5, r6, lr}
	b	FtlMapWritePage
.L2182:
	.align	2
.L2181:
	.word	.LANCHOR2
	.word	.LANCHOR2-1276
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
	ldr	r4, .L2201
	ldr	r3, [r4, #-1280]
	cmp	r3, #0
	bne	.L2185
	ldr	r2, [r4, #-1476]
	add	r5, r4, #816
	ldr	r6, [r4, #-1444]
	mov	r1, #255
	ldr	r7, .L2201+4
	str	r2, [r4, #-1268]
	movw	r2, #1128
	ldrh	r2, [r4, r2]
	str	r6, [r4, #-1264]
	str	r3, [r6, #12]
	strh	r2, [r6, #2]	@ movhi
	ldr	r2, .L2201+8
	strh	r2, [r6]	@ movhi
	ldr	r2, [r4, #1136]
	stmib	r6, {r2, r3}
	ldr	r3, .L2201+12
	str	r3, [r4, #816]
	ldr	r3, .L2201+16
	str	r3, [r4, #820]
	ldrh	r3, [r7, #6]
	strh	r3, [r5, #8]	@ movhi
	sub	r3, r4, #1712
	ldrh	r3, [r3, #-2]
	strb	r3, [r4, #826]
	add	r3, r4, #884
	ldrh	r2, [r3]
	strh	r2, [r5, #14]	@ movhi
	ldrh	r2, [r3, #2]
	ldrb	r3, [r4, #890]	@ zero_extendqisi2
	orr	r3, r3, r2, asl #6
	strh	r3, [r5, #16]	@ movhi
	ldrb	r3, [r4, #892]	@ zero_extendqisi2
	strb	r3, [r4, #827]
	add	r3, r4, #932
	ldrh	r2, [r3]
	strh	r2, [r5, #18]	@ movhi
	ldrh	r2, [r3, #2]
	ldrb	r3, [r4, #938]	@ zero_extendqisi2
	orr	r3, r3, r2, asl #6
	strh	r3, [r5, #20]	@ movhi
	ldrb	r3, [r4, #940]	@ zero_extendqisi2
	strb	r3, [r4, #828]
	add	r3, r4, #980
	ldrh	r2, [r3]
	strh	r2, [r5, #22]	@ movhi
	ldrh	r2, [r3, #2]
	ldrb	r3, [r4, #986]	@ zero_extendqisi2
	ldr	r0, [r4, #-1268]
	orr	r3, r3, r2, asl #6
	strh	r3, [r5, #24]	@ movhi
	ldrb	r3, [r4, #988]	@ zero_extendqisi2
	strb	r3, [r4, #829]
	ldr	r3, [r4, #-1584]
	str	r3, [r4, #848]
	ldr	r3, [r4, #-1616]
	str	r3, [r4, #856]
	ldr	r3, [r4, #-1612]
	str	r3, [r4, #852]
	sub	r3, r4, #1536
	ldrh	r2, [r3, #-4]
	ldrh	r3, [r3, #-2]
	strh	r2, [r5, #44]	@ movhi
	strh	r3, [r5, #46]	@ movhi
	sub	r3, r4, #1648
	ldrh	r2, [r3, #-10]
	bl	ftl_memset
	mov	r1, r5
	sub	r5, r4, #1728
	mov	r2, #48
	ldr	r0, [r4, #-1268]
	bl	ftl_memcpy
	ldrh	r2, [r5]
	ldr	r0, [r4, #-1268]
	ldr	r1, [r4, #-1408]
	mov	r2, r2, asl #1
	add	r0, r0, #48
	bl	ftl_memcpy
	ldrh	r2, [r5]
	ldr	r0, [r4, #-1268]
	ldr	r1, [r4, #-1396]
	mov	r3, r2, asl #1
	mov	r2, r2, lsr #3
	add	r3, r3, #51
	add	r2, r2, #4
	bic	r3, r3, #3
	add	r0, r0, r3
	bl	ftl_memcpy
	sub	r3, r4, #1616
	ldrh	r3, [r3, #-12]
	cmp	r3, #0
	beq	.L2186
	ldrh	r2, [r5]
	ldr	r0, [r4, #-1268]
	ldr	r1, [r4, #-1376]
	mov	r3, r2, lsr #3
	add	r3, r3, r2, asl #1
	sub	r2, r4, #1632
	add	r3, r3, #52
	ldrh	r2, [r2]
	ubfx	r3, r3, #2, #14
	add	r0, r0, r3, asl #2
	mov	r2, r2, asl #2
	bl	ftl_memcpy
.L2186:
	mov	r0, #0
	ldr	r9, .L2201
	bl	FtlUpdateVaildLpn
	ldr	fp, .L2201+4
	mov	r8, #0
	movw	r10, #65535
.L2187:
	ldr	r3, [r4, #-1476]
	ldrh	r2, [r7]
	ldrh	r1, [r7, #2]
	str	r3, [r4, #-1268]
	ldr	r3, [r4, #-1444]
	str	r3, [r4, #-1264]
	orr	r3, r1, r2, asl #10
	str	r3, [r4, #-1272]
	ldr	r3, .L2201+20
	ldrh	r3, [r3]
	sub	r3, r3, #1
	cmp	r1, r3
	blt	.L2188
	mov	r3, #0
	ldrh	r10, [fp, #4]
	strh	r3, [fp, #2]	@ movhi
	strh	r2, [fp, #4]	@ movhi
	bl	FtlFreeSysBlkQueueOut
	ldr	r3, [r9, #-1616]
	add	r2, r3, #1
	str	r2, [r9, #-1616]
	str	r3, [r9, #1136]
	mov	r2, r0, asl #10
	strh	r0, [fp]	@ movhi
	str	r2, [r9, #-1272]
	str	r3, [r6, #4]
	strh	r0, [r6, #2]	@ movhi
.L2188:
	ldr	r3, .L2201+24
	ldrb	r3, [r3]	@ zero_extendqisi2
	cmp	r3, #0
	beq	.L2189
	ldr	r3, .L2201+28
	ldr	r0, [r4, #-1476]
	ldrh	r1, [r3]
	bl	js_hash
	str	r0, [r6, #12]
.L2189:
	mov	r1, #1
	ldr	r0, .L2201+32
	mov	r3, r1
	mov	r2, r1
	bl	FlashProgPages
	ldrh	r5, [r7, #2]
	ldr	r3, [r4, #-1276]
	add	r5, r5, #1
	cmn	r3, #1
	uxth	r5, r5
	strh	r5, [r7, #2]	@ movhi
	bne	.L2190
	cmp	r5, #1
	add	r8, r8, #1
	ldreq	r3, .L2201+20
	uxth	r8, r8
	ldreqh	r3, [r3]
	subeq	r3, r3, #1
	streqh	r3, [fp, #2]	@ movhi
	cmp	r8, #3
	bls	.L2187
	ldr	r0, .L2201+36
	mov	r2, r8
	ldr	r1, [r4, #-1272]
	bl	printk
	mov	r3, #1
	str	r3, [r4, #-1280]
	b	.L2185
.L2190:
	cmp	r3, #256
	cmpne	r5, #1
	beq	.L2187
	movw	r3, #65535
	cmp	r10, r3
	beq	.L2185
	mov	r0, r10
	mov	r1, #1
	bl	FtlFreeSysBlkQueueIn
.L2185:
	mov	r0, #0
	ldmfd	sp!, {r3, r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L2202:
	.align	2
.L2201:
	.word	.LANCHOR2
	.word	.LANCHOR2+1128
	.word	-3932
	.word	1179929683
	.word	1342177379
	.word	.LANCHOR2-1666
	.word	.LANCHOR0
	.word	.LANCHOR2-1658
	.word	.LANCHOR2-1276
	.word	.LC47
	.fnend
	.size	FtlVpcTblFlush, .-FtlVpcTblFlush
	.align	2
	.global	FtlScanSysBlk
	.type	FtlScanSysBlk, %function
FtlScanSysBlk:
	.fnstart
	@ args = 0, pretend = 0, frame = 32
	@ frame_needed = 0, uses_anonymous_args = 0
	stmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.save {r4, r5, r6, r7, r8, r9, r10, fp, lr}
	movw	r3, #1144
	ldr	r9, .L2289
	mov	r10, #0
	mov	r1, r10
	.pad #36
	sub	sp, sp, #36
	sub	r5, r9, #1648
	ldr	r2, [r9, #-1640]
	mov	r4, r9
	strh	r10, [r9, r3]	@ movhi
	sub	r3, r9, #1280
	ldr	r0, [r9, #-1368]
	mov	r6, r9
	mov	r2, r2, asl #2
	strh	r10, [r3, #-8]	@ movhi
	bl	ftl_memset
	ldr	r2, [r9, #-1640]
	mov	r1, r10
	ldr	r0, [r9, #-1404]
	mov	r2, r2, asl #1
	bl	ftl_memset
	ldrh	r2, [r5]
	mov	r1, r10
	ldr	r0, [r9, #-1384]
	mov	r2, r2, asl #2
	bl	ftl_memset
	ldrh	r2, [r5]
	mov	r1, r10
	ldr	r0, [r9, #-1392]
	mov	r2, r2, asl #1
	bl	ftl_memset
	ldr	r0, .L2289+4
	mov	r1, #255
	mov	r2, #16
	bl	ftl_memset
	sub	r3, r9, #1728
	ldrh	r3, [r3]
	str	r5, [sp, #16]
	mov	r5, r9
	str	r3, [sp, #8]
.L2204:
	ldr	r3, .L2289+8
	ldr	r2, [sp, #8]
	ldr	r1, .L2289
	ldrh	r3, [r3]
	cmp	r3, r2
	bls	.L2245
	ldr	r3, .L2289+12
	mov	r8, #36
	ldr	r1, .L2289+16
	mov	r7, #0
	ldr	r2, [r5, #-1504]
	ldrh	ip, [r3]
	sub	r9, r1, #52
	ldr	r3, [r5, #-1464]
	ldr	fp, [r5, #-1436]
	ldrh	r10, [r1]
	str	r7, [sp, #4]
.L2246:
	uxth	r1, r7
	cmp	r1, ip
	bcs	.L2284
	ldr	r1, [sp, #8]
	ldrb	r0, [r9, r7]	@ zero_extendqisi2
	str	r3, [sp, #28]
	str	r2, [sp, #24]
	str	ip, [sp, #20]
	bl	V2P_block
	str	r0, [sp, #12]
	bl	FtlBbmIsBadBlock
	cmp	r0, #0
	ldr	r1, [sp, #12]
	ldr	ip, [sp, #20]
	ldr	r2, [sp, #24]
	ldr	r3, [sp, #28]
	bne	.L2205
	ldr	r0, [sp, #4]
	mov	r1, r1, asl #10
	mla	r0, r8, r0, r2
	stmib	r0, {r1, r3}
	ldr	r1, [sp, #4]
	mul	r1, r10, r1
	add	lr, r1, #3
	cmp	r1, #0
	movlt	r1, lr
	bic	r1, r1, #3
	add	r1, fp, r1
	str	r1, [r0, #12]
	ldr	r1, [sp, #4]
	add	r1, r1, #1
	uxth	r1, r1
	str	r1, [sp, #4]
.L2205:
	add	r7, r7, #1
	b	.L2246
.L2284:
	ldr	r3, [sp, #4]
	cmp	r3, #0
	beq	.L2208
	mov	r1, r3
	ldr	r0, [r4, #-1504]
	mov	r2, #1
	bl	FlashReadPages
	mov	r3, #0
.L2282:
	str	r3, [sp, #12]
	ldr	r2, [sp, #4]
	ldrh	r3, [sp, #12]
	cmp	r3, r2
	bcs	.L2208
	ldr	r3, [sp, #12]
	mov	r9, #36
	mul	r9, r9, r3
	ldr	r3, [r4, #-1504]
	add	r2, r3, r9
	ldr	r3, [r3, r9]
	ldr	r7, [r2, #4]
	cmn	r3, #1
	ldr	r8, [r2, #12]
	ubfx	r7, r7, #10, #16
	bne	.L2211
	mov	r10, #16
	movw	fp, #65535
.L2210:
	ldr	r0, [r4, #-1504]
	mov	r1, #1
	mov	r2, r1
	add	r0, r0, r9
	ldr	r3, [r0, #4]
	add	r3, r3, #1
	str	r3, [r0, #4]
	bl	FlashReadPages
	ldrh	r3, [r8]
	cmp	r3, fp
	ldreq	r3, [r6, #-1504]
	mvneq	r2, #0
	streq	r2, [r3, r9]
	beq	.L2211
.L2212:
	ldr	r3, [r5, #-1504]
	ldr	r3, [r3, r9]
	cmn	r3, #1
	bne	.L2211
	sub	r10, r10, #1
	uxth	r10, r10
	cmp	r10, #0
	bne	.L2210
.L2211:
	ldr	r3, [r5, #-1504]
	ldr	r3, [r3, r9]
	cmn	r3, #1
	beq	.L2214
	ldr	r2, [r5, #-1616]
	ldr	r3, [r8, #4]
	cmn	r2, #1
	beq	.L2215
	cmp	r2, r3
	bhi	.L2216
.L2215:
	cmn	r3, #1
	addne	r2, r3, #1
	strne	r2, [r6, #-1616]
.L2216:
	ldrh	r2, [r8]
	movw	r1, #61604
	cmp	r2, r1
	beq	.L2218
	bhi	.L2219
	movw	r3, #61574
	cmp	r2, r3
	bne	.L2217
	ldr	r3, [sp, #16]
	ldr	r2, .L2289+20
	ldr	lr, [r4, #-1384]
	ldrh	ip, [r3]
	ldrh	r1, [r2]
	sub	r0, ip, #1
	uxth	r3, r0
	rsb	r0, r1, r0
	b	.L2232
.L2219:
	movw	r3, #61634
	cmp	r2, r3
	beq	.L2221
	movw	r3, #65535
	cmp	r2, r3
	moveq	r0, r7
	beq	.L2283
	b	.L2217
.L2221:
	ldr	ip, [r4, #-1640]
	ldr	fp, .L2289+24
	ldr	lr, [r4, #-1368]
	uxth	r1, ip
	ldrh	r2, [fp]
	sub	r3, r1, #1
	rsb	r1, r2, r1
	uxth	r3, r3
	sub	r1, r1, #1
	sxth	r1, r1
	str	r1, [sp, #20]
.L2223:
	ldr	r1, [sp, #20]
	sxth	r0, r3
	cmp	r0, r1
	ble	.L2285
	ldr	r9, [lr, r0, asl #2]
	mov	r10, r0, asl #2
	ldr	r1, [r8, #4]
	cmp	r1, r9
	bls	.L2224
	ldr	r1, [lr]
	cmp	r1, #0
	bne	.L2225
	cmp	r2, ip
	addne	r2, r2, #1
	ldrne	r1, .L2289+24
	strneh	r2, [r1]	@ movhi
.L2225:
	uxth	lr, r3
	mov	r1, #0
.L2226:
	uxth	r2, r1
	cmp	r2, lr
	bcs	.L2286
	ldr	ip, [r5, #-1368]
	sxth	r2, r2
	add	r1, r1, #1
	add	r9, ip, r2, asl #2
	ldr	r9, [r9, #4]
	str	r9, [ip, r2, asl #2]
	mov	r2, r2, asl #1
	ldr	ip, [r5, #-1404]
	add	r9, ip, r2
	ldrh	r9, [r9, #2]
	strh	r9, [ip, r2]	@ movhi
	b	.L2226
.L2286:
	ldr	r2, [r6, #-1368]
	mov	r0, r0, asl #1
	ldr	r1, [r8, #4]
	str	r1, [r2, r10]
	ldr	r2, [r6, #-1404]
	strh	r7, [r2, r0]	@ movhi
	sxth	r0, r3
	cmp	r0, #0
	bge	.L2228
	b	.L2217
.L2224:
	sub	r3, r3, #1
	uxth	r3, r3
	b	.L2223
.L2285:
	cmp	r0, #0
	bge	.L2259
	b	.L2217
.L2228:
	ldr	r2, .L2289+24
	ldr	r1, [r5, #-1640]
	ldrh	r2, [r2]
	rsb	r1, r2, r1
	sub	r1, r1, #1
	sxth	r1, r1
	cmp	r0, r1
	bgt	.L2217
.L2259:
	add	r2, r2, #1
	ldr	r1, [r8, #4]
	strh	r2, [fp]	@ movhi
	sxth	r3, r3
	ldr	r2, [r4, #-1368]
	str	r1, [r2, r3, asl #2]
	mov	r3, r3, asl #1
	ldr	r2, [r4, #-1404]
	b	.L2280
.L2238:
	ldr	r10, [r8, #4]
	mov	fp, r2, asl #2
	ldr	r9, [lr, r2, asl #2]
	cmp	r10, r9
	bhi	.L2287
	sub	r3, r3, #1
	uxth	r3, r3
.L2232:
	sxth	r2, r3
	cmp	r2, r0
	bgt	.L2238
	b	.L2237
.L2287:
	ldr	r0, [lr]
	cmp	r0, #0
	bne	.L2234
	cmp	r1, ip
	addne	r1, r1, #1
	ldrne	r0, .L2289+20
	strneh	r1, [r0]	@ movhi
.L2234:
	uxth	lr, r3
	mov	r0, #0
.L2235:
	uxth	r1, r0
	cmp	r1, lr
	bcs	.L2288
	ldr	ip, [r5, #-1384]
	sxth	r1, r1
	add	r0, r0, #1
	add	r9, ip, r1, asl #2
	ldr	r9, [r9, #4]
	str	r9, [ip, r1, asl #2]
	mov	r1, r1, asl #1
	ldr	ip, [r5, #-1392]
	add	r9, ip, r1
	ldrh	r9, [r9, #2]
	strh	r9, [ip, r1]	@ movhi
	b	.L2235
.L2288:
	ldr	r1, [r6, #-1384]
	mov	r2, r2, asl #1
	ldr	r0, [r8, #4]
	str	r0, [r1, fp]
	ldr	r1, [r6, #-1392]
	strh	r7, [r1, r2]	@ movhi
.L2237:
	sxth	r3, r3
	cmp	r3, #0
	blt	.L2217
	ldr	r0, .L2289+20
	sub	r2, r0, #360
	ldrh	r1, [r0]
	ldrh	r2, [r2]
	sub	r2, r2, #1
	rsb	r2, r1, r2
	sxth	r2, r2
	cmp	r3, r2
	bgt	.L2217
	add	r1, r1, #1
	ldr	r2, [r5, #-1384]
	strh	r1, [r0]	@ movhi
	ldr	r1, [r8, #4]
	str	r1, [r2, r3, asl #2]
	mov	r3, r3, asl #1
	ldr	r2, [r5, #-1392]
.L2280:
	strh	r7, [r2, r3]	@ movhi
	b	.L2217
.L2218:
	ldr	r2, .L2289+4
	ldr	r9, .L2289+4
	ldrh	r1, [r2]
	movw	r2, #65535
	cmp	r1, r2
	moveq	r2, r9
	streqh	r7, [r2]	@ movhi
	beq	.L2281
	ldr	r3, .L2289+4
	ldrh	r0, [r3, #4]
	cmp	r0, r2
	beq	.L2240
	mov	r1, #1
	bl	FtlFreeSysBlkQueueIn
.L2240:
	ldr	r3, [r8, #4]
	ldr	r2, [r6, #1136]
	cmp	r2, r3
	strcsh	r7, [r9, #4]	@ movhi
	bcs	.L2217
	ldrh	r3, [r9]
	strh	r7, [r9]	@ movhi
	strh	r3, [r9, #4]	@ movhi
	ldr	r3, [r8, #4]
.L2281:
	str	r3, [r5, #1136]
	b	.L2217
.L2214:
	ldrb	r1, [r5, #-2744]	@ zero_extendqisi2
	mov	r0, r7
	cmp	r1, #0
	beq	.L2242
.L2283:
	mov	r1, #0
.L2242:
	bl	FtlFreeSysBlkQueueIn
.L2217:
	ldr	r3, [sp, #12]
	add	r3, r3, #1
	b	.L2282
.L2208:
	ldr	r3, [sp, #8]
	add	r7, r3, #1
	uxth	r3, r7
	str	r3, [sp, #8]
	b	.L2204
.L2245:
	ldr	ip, [r1, #-1404]
	ldrh	r2, [ip]
	cmp	r2, #0
	beq	.L2247
.L2250:
	ldr	ip, [r1, #-1392]
	ldrh	r2, [ip]
	cmp	r2, #0
	beq	.L2248
	b	.L2271
.L2247:
	movw	r3, #1144
	ldrh	r3, [r1, r3]
	cmp	r3, #0
	ldrne	lr, [r1, #-1640]
	beq	.L2250
.L2251:
	uxth	r3, r2
	sxth	r0, r3
	cmp	r0, lr
	bcs	.L2250
	mov	r4, r0, asl #1
	add	r2, r2, #1
	ldrh	r4, [ip, r4]
	cmp	r4, #0
	beq	.L2251
	ldr	ip, .L2289
	mov	r6, #0
.L2252:
	ldr	lr, [r1, #-1640]
	sxth	r2, r3
	cmp	r2, lr
	bcs	.L2250
	ldr	r4, [ip, #-1404]
	mov	lr, r2, asl #1
	rsb	r5, r0, r2
	add	r3, r3, #1
	ldrh	r8, [r4, lr]
	mov	r7, r5, asl #1
	uxth	r3, r3
	strh	r8, [r4, r7]	@ movhi
	ldr	r4, [ip, #-1368]
	ldr	r2, [r4, r2, asl #2]
	str	r2, [r4, r5, asl #2]
	ldr	r2, [ip, #-1404]
	strh	r6, [r2, lr]	@ movhi
	b	.L2252
.L2248:
	ldr	r1, .L2289+28
	ldrh	r3, [r1, #-8]
	cmp	r3, #0
	subne	r1, r1, #368
	ldrneh	lr, [r1]
	beq	.L2271
.L2255:
	uxth	r3, r2
	sxth	r0, r3
	cmp	r0, lr
	bge	.L2271
	mov	r4, r0, asl #1
	add	r2, r2, #1
	ldrh	r4, [ip, r4]
	cmp	r4, #0
	beq	.L2255
	ldr	ip, .L2289
	mov	r6, #0
.L2256:
	ldrh	lr, [r1]
	sxth	r2, r3
	cmp	r2, lr
	bge	.L2271
	ldr	r4, [ip, #-1392]
	mov	lr, r2, asl #1
	rsb	r5, r0, r2
	add	r3, r3, #1
	ldrh	r8, [r4, lr]
	mov	r7, r5, asl #1
	uxth	r3, r3
	strh	r8, [r4, r7]	@ movhi
	ldr	r4, [ip, #-1384]
	ldr	r2, [r4, r2, asl #2]
	str	r2, [r4, r5, asl #2]
	ldr	r2, [ip, #-1392]
	strh	r6, [r2, lr]	@ movhi
	b	.L2256
.L2271:
	mov	r0, #0
	add	sp, sp, #36
	@ sp needed
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L2290:
	.align	2
.L2289:
	.word	.LANCHOR2
	.word	.LANCHOR2+1128
	.word	.LANCHOR2-1726
	.word	.LANCHOR2-1736
	.word	.LANCHOR2-1656
	.word	.LANCHOR2-1288
	.word	.LANCHOR2+1144
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
	ldr	r4, .L2294
	sub	r5, r4, #1424
	ldr	r2, [r4, #-1420]
	ldrh	r1, [r5, #-4]
	bl	FtlVendorPartRead
	ldr	r3, [r4, #-1420]
	ldr	r2, [r3]
	ldr	r3, .L2294+4
	cmp	r2, r3
	beq	.L2292
	ldr	r1, .L2294+8
	ldr	r0, .L2294+12
	bl	printk
	ldrh	r2, [r5, #-4]
	ldr	r0, [r4, #-1420]
	mov	r1, #0
	mov	r2, r2, asl #9
	bl	ftl_memset
.L2292:
	mov	r0, #0
	ldmfd	sp!, {r3, r4, r5, pc}
.L2295:
	.align	2
.L2294:
	.word	.LANCHOR2
	.word	1112818501
	.word	.LC49
	.word	.LC48
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
	ldr	r2, .L2300
	mov	r3, r0, lsr #5
	cmp	r1, #0
	and	r0, r0, #31
	uxth	r3, r3
	mov	ip, #1
	ldr	r1, [r2, #-1396]
	ldr	r2, [r1, r3, asl #2]
	orrne	r0, r2, ip, asl r0
	biceq	r0, r2, ip, asl r0
	str	r0, [r1, r3, asl #2]
	bx	lr
.L2301:
	.align	2
.L2300:
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
	ldr	r3, .L2303
	mov	r2, r0, lsr #5
	and	r0, r0, #31
	ldr	r3, [r3, #-1396]
	ldr	r3, [r3, r2, asl #2]
	mov	r0, r3, lsr r0
	and	r0, r0, #1
	bx	lr
.L2304:
	.align	2
.L2303:
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
	ldr	r1, .L2328
	ldr	r0, .L2328+4
	bl	printk
	ldr	r0, .L2328+8
	mov	r1, #0
	mov	r2, #8192
	bl	memset
	ldr	r7, .L2328+12
	ldr	r5, .L2328+8
.L2306:
	ldr	r3, [r7, #-1284]
	ldr	r6, .L2328+12
	cmp	r4, r3
	bcs	.L2326
	mov	r0, r4
	add	r1, sp, #4
	mov	r2, #0
	bl	log2phys
	ldr	r0, [sp, #4]
	cmn	r0, #1
	beq	.L2307
	ubfx	r0, r0, #10, #16
	bl	P2V_block_in_plane
	mov	r0, r0, asl #1
	ldrh	r3, [r5, r0]
	add	r3, r3, #1
	strh	r3, [r5, r0]	@ movhi
.L2307:
	add	r4, r4, #1
	b	.L2306
.L2326:
	ldr	r9, .L2328+8
	mov	r4, #0
	mov	r5, r4
	mov	r8, r6
.L2309:
	ldr	r2, .L2328+12
	uxth	r1, r4
	sub	r3, r2, #1728
	ldrh	r3, [r3]
	cmp	r3, r1
	bls	.L2327
	ldr	r3, [r8, #-1408]
	mov	r7, r1, asl #1
	ldrh	r2, [r3, r7]
	ldrh	r3, [r9, r7]
	cmp	r2, r3
	beq	.L2310
	ldr	r0, .L2328+16
	bl	printk
	ldr	r3, [r8, #-1408]
	movw	r2, #65535
	ldrh	r3, [r3, r7]
	cmp	r3, r2
	beq	.L2310
	ldrh	r2, [r9, r7]
	cmp	r2, r3
	movhi	r5, #1
.L2310:
	add	r4, r4, #1
	b	.L2309
.L2327:
	ldr	r3, [r2, #876]
	cmp	r3, #0
	beq	.L2312
	add	r1, r2, #880
	ldr	r2, [r2, #-1356]
	add	r9, r1, #268
	mov	r8, #0
	rsb	r3, r2, r3
	ldr	r2, .L2328+20
	ldrh	r7, [r1]
	mov	r3, r3, asr #1
	mul	r3, r2, r3
	uxth	r4, r3
.L2313:
	uxth	r3, r8
	cmp	r3, r7
	bcs	.L2312
	ldr	r2, [r6, #-1408]
	mov	r3, r4, asl #1
	ldrh	r2, [r2, r3]
	cmp	r2, #0
	beq	.L2314
	ldr	r0, .L2328+24
	mov	r1, r4
	ldrh	r3, [r9, r3]
	mov	r5, #1
	bl	printk
.L2314:
	mov	r3, #6
	ldr	r2, [r6, #-1356]
	mul	r4, r3, r4
	movw	r3, #65535
	add	r8, r8, #1
	ldrh	r4, [r2, r4]
	cmp	r4, r3
	bne	.L2313
.L2312:
	mov	r1, r5
	ldr	r0, .L2328+28
	bl	printk
	add	sp, sp, #12
	@ sp needed
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, pc}
.L2329:
	.align	2
.L2328:
	.word	.LANCHOR3+148
	.word	.LC50
	.word	.LANCHOR2+1148
	.word	.LANCHOR2
	.word	.LC51
	.word	-1431655765
	.word	.LC52
	.word	.LC53
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
	ldr	r4, .L2339
	.pad #24
	sub	sp, sp, #24
	ldr	r8, .L2339+4
	mov	r6, r0
	mov	r5, #0
	ldr	r3, [r4, #-1476]
	str	r3, [r4, #-1268]
	ldr	r3, [r4, #-1444]
	str	r3, [r4, #-1264]
.L2331:
	ldrh	r2, [r8]
	sxth	r3, r5
	cmp	r3, r2
	bge	.L2338
	mov	r1, #1
	ldr	r0, .L2339+8
	mov	r2, r1
	orr	r3, r3, r7
	str	r3, [r4, #-1272]
	bl	FlashReadPages
	ldr	r3, [r4, #-1264]
	mov	r1, r6
	ldr	r0, .L2339+12
	ldr	r2, [r3]
	str	r2, [sp]
	ldr	r2, [r3, #4]
	str	r2, [sp, #4]
	ldr	r2, [r3, #8]
	str	r2, [sp, #8]
	ldr	r3, [r3, #12]
	ldr	r2, [r4, #-1276]
	str	r3, [sp, #12]
	ldr	r3, [r4, #-1268]
	ldr	r3, [r3]
	str	r3, [sp, #16]
	ldr	r3, [r4, #-1272]
	bl	printk
	ldr	r3, [r4, #-1264]
	ldr	r3, [r3]
	cmn	r3, #1
	beq	.L2332
	ldr	r0, .L2339+16
	mov	r2, #4
	ldr	r1, [r4, #-1476]
	mov	r3, #768
	bl	rknand_print_hex
.L2332:
	add	r5, r5, #1
	b	.L2331
.L2338:
	add	sp, sp, #24
	@ sp needed
	ldmfd	sp!, {r4, r5, r6, r7, r8, pc}
.L2340:
	.align	2
.L2339:
	.word	.LANCHOR2
	.word	.LANCHOR2-1666
	.word	.LANCHOR2-1276
	.word	.LC54
	.word	.LC55
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
	ldr	r0, .L2351
	bl	printk
	ldr	r6, .L2351+4
	mov	r5, #0
	mov	r4, r6
.L2342:
	ldr	r3, [r6, #-1284]
	cmp	r5, r3
	bcs	.L2350
	mov	r0, r5
	add	r1, sp, #28
	mov	r2, #0
	bl	log2phys
	ubfx	r3, r5, #0, #11
	cmp	r3, #0
	bne	.L2343
	ldr	r0, .L2351+8
	mov	r1, r5
	ldr	r2, [sp, #28]
	bl	printk
.L2343:
	ldr	r3, [sp, #28]
	cmn	r3, #1
	beq	.L2345
	str	r3, [r4, #-1272]
	mov	r2, #0
	ldr	r3, [r4, #-1476]
	mov	r1, #1
	ldr	r7, [r4, #-1444]
	ldr	r0, .L2351+12
	str	r3, [r4, #-1268]
	str	r5, [r4, #-1260]
	str	r7, [r4, #-1264]
	str	r2, [r4, #-1276]
	bl	FlashReadPages
	ldr	r3, [r4, #-1276]
	cmn	r3, #1
	cmpne	r3, #256
	beq	.L2346
	ldr	r3, [r7, #8]
	cmp	r3, r5
	beq	.L2345
.L2346:
	ldr	r3, [r4, #-1264]
	ldr	r2, [r4, #-1268]
	ldr	r0, .L2351+16
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
	ldr	r2, [r4, #-1272]
	ldr	r3, [r3]
	bl	printk
.L2345:
	add	r5, r5, #1
	b	.L2342
.L2350:
	add	sp, sp, #36
	@ sp needed
	ldmfd	sp!, {r4, r5, r6, r7, pc}
.L2352:
	.align	2
.L2351:
	.word	.LC56
	.word	.LANCHOR2
	.word	.LC57
	.word	.LANCHOR2-1276
	.word	.LC58
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
	ldr	r5, .L2371
	ldr	fp, .L2371+4
	ldrh	r6, [r5]
	add	r5, r5, #1728
	mov	r8, r5
.L2354:
	ldrh	r3, [fp]
	cmp	r3, r6
	bls	.L2367
	ldr	r1, .L2371+8
	mov	r7, #0
	ldr	r2, [r5, #-1504]
	mov	r4, r7
	ldr	r3, [r5, #-1464]
	add	r1, r1, #28
	ldr	r10, [r5, #-1436]
	ldrh	ip, [r1, #-28]
	ldrh	r9, [r1, #52]
	str	r1, [sp, #28]
.L2363:
	uxth	r1, r7
	cmp	r1, ip
	bcs	.L2369
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
	bne	.L2355
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
.L2355:
	add	r7, r7, #1
	b	.L2363
.L2369:
	cmp	r4, #0
	beq	.L2358
	ldr	r0, [r8, #-1504]
	mov	r1, r4
	mov	r2, #1
	mov	r7, #0
	bl	FlashReadPages
	mov	r9, #36
.L2359:
	uxth	r3, r7
	cmp	r3, r4
	bcs	.L2358
	ldr	r3, [r8, #-1504]
	ldr	r0, .L2371+12
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
	b	.L2359
.L2358:
	add	r6, r6, #1
	uxth	r6, r6
	b	.L2354
.L2367:
	ldr	r4, .L2371+16
	mov	r7, #0
	ldr	r8, .L2371+20
.L2362:
	ldrh	r3, [r8]
	sxth	r6, r7
	ldr	r5, .L2371+16
	cmp	r6, r3
	bge	.L2365
	ldr	r9, .L2371+24
	mov	r6, r6, asl #1
	mov	r5, #0
.L2366:
	ldrh	r2, [r9]
	sxth	r3, r5
	add	r5, r5, #1
	cmp	r3, r2
	bge	.L2370
	ldr	r2, [r4, #-1404]
	mov	r1, #1
	ldr	r0, .L2371+28
	ldrh	r2, [r2, r6]
	orr	r3, r3, r2, asl #10
	mov	r2, r1
	str	r3, [r4, #-1272]
	bl	FlashReadPages
	ldr	r3, [r4, #-1264]
	ldr	r1, [r4, #-1404]
	ldr	r2, [r4, #-1268]
	ldr	r0, [r3]
	ldrh	r1, [r1, r6]
	str	r0, [sp]
	ldr	r0, [r3, #4]
	str	r0, [sp, #4]
	ldr	r0, [r3, #8]
	str	r0, [sp, #8]
	ldr	r3, [r3, #12]
	ldr	r0, .L2371+32
	str	r3, [sp, #12]
	ldr	r3, [r2]
	str	r3, [sp, #16]
	ldr	r3, [r2, #4]
	str	r3, [sp, #20]
	ldr	r2, [r4, #-1276]
	ldr	r3, [r4, #-1272]
	bl	printk
	b	.L2366
.L2370:
	add	r7, r7, #1
	b	.L2362
.L2365:
	sub	r4, r5, #1632
	ldr	r1, [r5, #-1404]
	ldr	r3, [r5, #-1640]
	mov	r2, #2
	ldr	r0, .L2371+36
	bl	rknand_print_hex
	ldr	r1, [r5, #-1376]
	ldrh	r3, [r4]
	mov	r2, #4
	ldr	r0, .L2371+40
	bl	rknand_print_hex
	ldr	r0, .L2371+44
	ldr	r1, [r5, #-1372]
	mov	r2, #4
	ldrh	r3, [r4]
	add	sp, sp, #52
	@ sp needed
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, fp, lr}
	b	rknand_print_hex
.L2372:
	.align	2
.L2371:
	.word	.LANCHOR2-1728
	.word	.LANCHOR2-1726
	.word	.LANCHOR2-1736
	.word	.LC59
	.word	.LANCHOR2
	.word	.LANCHOR2+1144
	.word	.LANCHOR2-1666
	.word	.LANCHOR2-1276
	.word	.LC60
	.word	.LC61
	.word	.LC62
	.word	.LC63
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
	ldr	r5, .L2416
	str	r3, [sp, #8]
	bl	ftl_memset
	mov	r1, #0
	str	r1, [r4, #32]
	ldr	r3, [r5, #-1476]
	mov	r10, r5
	ldr	r6, [r5, #-1444]
	str	r1, [r4, #28]
	str	r3, [r5, #-1268]
	mvn	r3, #0
	str	r6, [r5, #-1264]
	strh	r3, [r4]	@ movhi
	strh	r3, [r4, #2]	@ movhi
	mov	r3, #1
	str	r3, [r4, #36]
	str	r1, [sp, #4]
.L2374:
	ldrh	r2, [sp, #4]
	ldr	r3, [sp, #8]
	sxth	fp, r2
	cmp	fp, r3
	bge	.L2392
	ldr	r3, [sp, #8]
	sub	r1, r3, #1
	cmp	fp, r1
	mov	r1, fp, asl #1
	bne	.L2375
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
	ldr	fp, .L2416
	str	r3, [r4, #28]
	add	r3, r0, #1
	str	r3, [sp, #4]
.L2376:
	ldr	r2, [sp, #4]
	sxth	r3, r8
	cmp	r3, r2
	bge	.L2392
	ldrh	r2, [r10]
	mov	r1, #1
	ldr	r0, .L2416+4
	orr	r3, r3, r2, asl #10
	mov	r2, r1
	str	r3, [r5, #-1272]
	bl	FlashReadPages
	ldr	r3, .L2416+8
	ldrb	r3, [r3]	@ zero_extendqisi2
	cmp	r3, #0
	beq	.L2377
	ldr	r3, [fp, #-1264]
	ldr	r3, [r3, #12]
	cmp	r3, #0
	beq	.L2377
	ldr	r2, .L2416+12
	ldr	r0, [fp, #-1268]
	str	r3, [sp, #8]
	ldrh	r1, [r2]
	bl	js_hash
	ldr	r3, [sp, #8]
	cmp	r3, r0
	mvnne	r3, #0
	strne	r3, [fp, #-1276]
.L2377:
	ldr	r3, [fp, #-1276]
	cmn	r3, #1
	beq	.L2378
	ldrh	r3, [r6, #8]
	cmp	r3, r9
	bcs	.L2378
	ldrh	r2, [r4, #4]
	ldrh	r1, [r6]
	cmp	r1, r2
	ldreq	r2, [fp, #-1272]
	streq	r2, [r7, r3, asl #2]
.L2378:
	add	r8, r8, #1
	b	.L2376
.L2392:
	mov	r0, r4
	bl	ftl_free_no_use_map_blk
	ldr	r3, .L2416+16
	ldrh	r2, [r4, #2]
	ldrh	r3, [r3, #-2]
	cmp	r2, r3
	bne	.L2381
	mov	r0, r4
	bl	ftl_map_blk_alloc_new_blk
	b	.L2381
.L2375:
	ldr	r2, [r5, #-1476]
	add	r3, r8, r1
	str	r3, [sp, #16]
	ldr	r3, .L2416+20
	str	r2, [r5, #-1268]
	ldrh	r1, [r8, r1]
	ldrh	r2, [r3]
	ldr	r0, .L2416+4
	sub	r2, r2, #1
	orr	r2, r2, r1, asl #10
	mov	r1, #1
	str	r2, [r5, #-1272]
	mov	r2, r1
	bl	FlashReadPages
	ldr	r2, [r5, #-1276]
	cmn	r2, #1
	beq	.L2394
	ldrh	r1, [r6]
	ldrh	r2, [r4, #4]
	cmp	r1, r2
	bne	.L2394
	ldrh	r1, [r6, #8]
	movw	r2, #64245
	cmp	r1, r2
	bne	.L2394
	mov	r0, #0
	mov	fp, #8
	mov	lr, #4
.L2383:
	ldr	r3, .L2416+20
	uxth	r2, r0
	sxth	r1, r2
	ldrh	ip, [r3]
	sub	ip, ip, #1
	cmp	r1, ip
	bge	.L2386
	ldr	ip, [r10, #-1476]
	add	r0, r0, #1
	ldr	r1, [ip, r1, asl #3]
	uxth	r1, r1
	cmp	r1, r9
	smlabbcc	r2, r2, fp, lr
	ldrcc	r2, [ip, r2]
	strcc	r2, [r7, r1, asl #2]
	b	.L2383
.L2394:
	mov	fp, #0
.L2414:
	ldr	r3, .L2416+20
	sxth	r2, fp
	ldrh	r1, [r3]
	cmp	r2, r1
	bge	.L2386
	ldr	r3, [sp, #16]
	ldr	r0, .L2416+4
	ldrh	r1, [r3]
	orr	r2, r2, r1, asl #10
	mov	r1, #1
	str	r2, [r10, #-1272]
	mov	r2, r1
	bl	FlashReadPages
	ldr	r2, .L2416+8
	ldrb	r2, [r2]	@ zero_extendqisi2
	cmp	r2, #0
	beq	.L2387
	ldr	r2, [r10, #-1264]
	ldr	r2, [r2, #12]
	cmp	r2, #0
	beq	.L2387
	ldr	r1, .L2416+12
	ldr	r0, [r10, #-1268]
	str	r2, [sp, #20]
	ldrh	r1, [r1]
	bl	js_hash
	ldr	r2, [sp, #20]
	cmp	r2, r0
	mvnne	r2, #0
	strne	r2, [r10, #-1276]
.L2387:
	ldr	r2, [r10, #-1276]
	cmn	r2, #1
	beq	.L2388
	ldrh	r2, [r6, #8]
	cmp	r2, r9
	bcs	.L2388
	ldrh	r1, [r4, #4]
	ldrh	r0, [r6]
	cmp	r0, r1
	ldreq	r1, [r10, #-1272]
	streq	r1, [r7, r2, asl #2]
.L2388:
	add	fp, fp, #1
	b	.L2414
.L2386:
	ldr	r3, [sp, #4]
	add	r3, r3, #1
	str	r3, [sp, #4]
	b	.L2374
.L2381:
	mov	r0, r4
	bl	ftl_map_blk_gc
	mov	r0, r4
	bl	ftl_map_blk_gc
	mov	r0, #0
	add	sp, sp, #28
	@ sp needed
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L2417:
	.align	2
.L2416:
	.word	.LANCHOR2
	.word	.LANCHOR2-1276
	.word	.LANCHOR0
	.word	.LANCHOR2-1658
	.word	.LANCHOR2-1664
	.word	.LANCHOR2-1666
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
	ldr	r3, .L2420
	sub	r2, r3, #1648
	add	r0, r3, #1072
	add	r0, r0, #4
	ldrh	r2, [r2]
	strh	r2, [r0, #10]	@ movhi
	ldr	r2, .L2420+4
	strh	r2, [r0, #4]	@ movhi
	sub	r2, r3, #1280
	ldrh	r2, [r2, #-8]
	strh	r2, [r0, #8]	@ movhi
	sub	r2, r3, #1632
	ldrh	r2, [r2, #-14]
	strh	r2, [r0, #6]	@ movhi
	ldr	r2, [r3, #-1392]
	str	r2, [r3, #1088]
	ldr	r2, [r3, #-1384]
	str	r2, [r3, #1092]
	ldr	r2, [r3, #-1388]
	str	r2, [r3, #1096]
	ldr	r2, [r3, #-1380]
	str	r2, [r3, #1100]
	bl	FtlMapTblRecovery
	mov	r0, #0
	ldmfd	sp!, {r3, pc}
.L2421:
	.align	2
.L2420:
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
	ldr	r4, .L2427
	mvn	r6, #0
	ldr	r2, [r4, #-1640]
	ldr	r0, [r4, #-1400]
	mov	r2, r2, asl #1
	bl	ftl_memset
	sub	r3, r4, #1648
	sub	r2, r4, #1616
	mov	r1, #255
	ldrh	r3, [r3, #-10]
	ldrh	r2, [r2, #-14]
	ldr	r0, [r4, #-1360]
	mul	r2, r2, r3
	bl	ftl_memset
	ldr	r0, .L2427+4
	mov	r1, #0
	mov	lr, #12
	sub	r7, r0, #28
	mov	r5, r1
.L2423:
	ldrh	r2, [r0]
	add	ip, r1, #1
	uxth	r1, r1
	ldr	r3, .L2427
	cmp	r2, r1
	bls	.L2426
	mul	r8, lr, r1
	ldr	r3, [r4, #-1364]
	add	r2, r3, r8
	str	r5, [r2, #4]
	strh	r6, [r3, r8]	@ movhi
	ldrh	r2, [r7]
	ldr	r3, [r4, #-1364]
	mul	r2, r1, r2
	add	r3, r3, r8
	ldr	r1, [r4, #-1360]
	bic	r2, r2, #3
	add	r2, r1, r2
	mov	r1, ip
	str	r2, [r3, #8]
	b	.L2423
.L2426:
	ldr	r2, .L2427+8
	movw	r0, #1028
	mvn	r1, #0
	strh	r1, [r3, r0]	@ movhi
	strh	r1, [r2, #2]	@ movhi
	ldr	r1, [r3, #-1640]
	strh	r1, [r2, #10]	@ movhi
	ldr	r1, .L2427+12
	strh	r1, [r2, #4]	@ movhi
	movw	r1, #1144
	ldrh	r1, [r3, r1]
	strh	r1, [r2, #8]	@ movhi
	sub	r1, r3, #1632
	ldrh	r1, [r1]
	strh	r1, [r2, #6]	@ movhi
	ldr	r2, [r3, #-1404]
	str	r2, [r3, #1040]
	ldr	r2, [r3, #-1368]
	str	r2, [r3, #1044]
	ldr	r2, [r3, #-1400]
	str	r2, [r3, #1048]
	ldr	r2, [r3, #-1376]
	str	r2, [r3, #1052]
	ldmfd	sp!, {r4, r5, r6, r7, r8, pc}
.L2428:
	.align	2
.L2427:
	.word	.LANCHOR2
	.word	.LANCHOR2-1630
	.word	.LANCHOR2+1028
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
	ldr	r0, .L2431
	bl	FtlMapTblRecovery
	mov	r0, #0
	ldmfd	sp!, {r3, pc}
.L2432:
	.align	2
.L2431:
	.word	.LANCHOR2+1028
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
	ldr	r3, .L2443
	stmfd	sp!, {r4, r5, lr}
	.save {r4, r5, lr}
	movw	r4, #65535
	ldrh	lr, [r3, #-8]
	add	r3, r2, #7
	add	r3, r0, r3, asl #1
.L2434:
	cmp	r2, lr
	bcs	.L2441
	ldrh	ip, [r3, #2]!
	add	r2, r2, #1
	cmp	ip, r4
	uxth	r2, r2
	ldrneh	ip, [r0, #4]
	addne	ip, ip, #1
	strneh	ip, [r0, #4]	@ movhi
	b	.L2434
.L2441:
	ldr	r3, .L2443+4
	add	ip, r0, #14
	mov	r2, #0
	movw	r5, #65535
	ldrh	r4, [r3, #-4]
.L2437:
	uxth	r3, r2
	cmp	r3, lr
	bcs	.L2442
	ldrh	r3, [ip, #2]!
	add	r2, r2, #1
	cmp	r3, r5
	ldrneh	r3, [r0, #4]
	addne	r3, r4, r3
	subne	r3, r3, #1
	rsbne	r3, r1, r3
	strneh	r3, [r0, #4]	@ movhi
	b	.L2437
.L2442:
	ldmfd	sp!, {r4, r5, pc}
.L2444:
	.align	2
.L2443:
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
	ldr	r6, .L2455
	ldr	r4, [r6, #-1408]
	mov	r7, r0, asl #1
	ldrh	r3, [r4, r7]
	cmp	r3, #0
	addne	r3, r3, #1
	strneh	r3, [r4, r7]	@ movhi
	bne	.L2447
	ldr	r4, [r6, #876]
	cmp	r4, #0
	beq	.L2447
	ldr	r1, [r6, #-1356]
	add	r2, r6, #880
	ldr	lr, .L2455+4
	movw	r9, #65535
	rsb	r4, r1, r4
	ldrh	ip, [r2]
	mov	r8, r2
	mov	r4, r4, asr #1
	mul	r4, lr, r4
	mov	lr, #6
	uxth	r4, r4
.L2448:
	uxth	r2, r3
	cmp	r2, ip
	bcs	.L2447
	cmp	r4, r0
	bne	.L2449
	mov	r1, r4
	ldr	r0, .L2455+8
	bl	List_remove_node
	ldrh	r3, [r8]
	mov	r0, r4
	sub	r3, r3, #1
	strh	r3, [r8]	@ movhi
	bl	INSERT_DATA_LIST
	ldr	r2, [r6, #-1408]
	ldrh	r3, [r2, r7]
	add	r3, r3, #1
	strh	r3, [r2, r7]	@ movhi
	b	.L2447
.L2449:
	mul	r4, lr, r4
	add	r3, r3, #1
	ldrh	r4, [r1, r4]
	cmp	r4, r9
	bne	.L2448
.L2447:
	mov	r0, r5
	add	r1, sp, #4
	mov	r2, #1
	bl	log2phys
	add	sp, sp, #12
	@ sp needed
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, pc}
.L2456:
	.align	2
.L2455:
	.word	.LANCHOR2
	.word	-1431655765
	.word	.LANCHOR2+876
	.fnend
	.size	FtlReUsePrevPpa, .-FtlReUsePrevPpa
	.align	2
	.global	make_superblock
	.type	make_superblock, %function
make_superblock:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	ldr	r3, .L2471
	stmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, lr}
	.save {r4, r5, r6, r7, r8, r9, r10, lr}
	add	r6, r0, #16
	ldrh	r8, [r3, #-8]
	add	r7, r3, #20
	mov	r4, r0
	mvn	r9, #0
	mov	r5, #0
	strh	r5, [r0, #4]	@ movhi
	strb	r5, [r0, #7]
.L2458:
	uxth	r3, r5
	cmp	r3, r8
	bcs	.L2470
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
	b	.L2458
.L2470:
	ldr	r2, .L2471+4
	ldrb	r1, [r4, #7]	@ zero_extendqisi2
	sub	r3, r2, #1664
	ldrh	r3, [r3, #-4]
	smulbb	r3, r1, r3
	strh	r3, [r4, #4]	@ movhi
	mov	r3, #0
	strb	r3, [r4, #9]
	ldr	r3, [r2, #-1872]
	cmp	r3, #0
	beq	.L2461
	ldrh	r3, [r4]
	ldr	r2, [r2, #-1416]
	mov	r3, r3, asl #1
	ldrh	r3, [r2, r3]
	cmp	r3, #79
	movls	r3, #1
	strlsb	r3, [r4, #9]
.L2461:
	ldr	r3, .L2471+8
	mov	r0, #0
	ldrb	r3, [r3]	@ zero_extendqisi2
	cmp	r3, #0
	movne	r3, #1
	strneb	r3, [r4, #9]
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, pc}
.L2472:
	.align	2
.L2471:
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
	ldr	r4, .L2502
	.pad #44
	sub	sp, sp, #44
	movw	r7, #1128
	ldr	r9, .L2502+4
	sub	r6, r4, #1728
	ldr	r3, [r4, #-1476]
	ldrh	r2, [r6]
	ldr	r0, [r4, #-1408]
	str	r3, [r4, #-1268]
	ldr	r3, [r4, #-1444]
	mov	r2, r2, asl #1
	str	r3, [r4, #-1264]
	bl	ftl_memset
	ldrh	r0, [r4, r7]
	movw	r3, #65535
	cmp	r0, r3
	bne	.L2474
.L2485:
	mvn	r0, #0
	b	.L2475
.L2474:
	mov	r1, #1
	bl	FtlGetLastWrittenPage
	ldrh	r3, [r4, r7]
	mov	r7, r4
	uxth	r5, r0
	str	r3, [sp, #28]
	add	r0, r0, #1
	strh	r0, [r9, #2]	@ movhi
.L2476:
	sxth	r3, r5
	cmp	r3, #0
	blt	.L2484
	ldrsh	fp, [sp, #28]
	mov	r1, #1
	ldr	r0, .L2502+8
	mov	r2, r1
	orr	r3, r3, fp, asl #10
	str	r3, [r4, #-1272]
	ldr	r3, [r4, #-1476]
	str	r3, [r4, #-1268]
	bl	FlashReadPages
	ldr	r3, .L2502+12
	ldrb	r3, [r3]	@ zero_extendqisi2
	cmp	r3, #0
	beq	.L2477
	ldr	r8, [r7, #-1264]
	ldr	r3, [r8, #12]
	cmp	r3, #0
	beq	.L2477
	ldr	r2, [r7, #-1268]
	ldr	r10, .L2502+16
	str	r3, [sp, #36]
	mov	r0, r2
	str	r2, [sp, #32]
	ldrh	r1, [r10]
	bl	js_hash
	ldr	r3, [sp, #36]
	cmp	r3, r0
	beq	.L2477
	cmp	r5, #0
	ldr	r2, [sp, #32]
	bne	.L2478
	ldrh	r1, [r9, #4]
	ldr	ip, .L2502+4
	cmp	fp, r1
	beq	.L2478
	ldr	r0, [r8]
	ldrh	r1, [ip]
	str	ip, [sp, #28]
	str	r0, [sp]
	ldr	r0, [r8, #4]
	str	r0, [sp, #4]
	ldr	r0, [r8, #8]
	str	r3, [sp, #12]
	str	r0, [sp, #8]
	ldr	r3, [r2]
	ldr	r0, .L2502+20
	str	r3, [sp, #16]
	ldr	r3, [r7, #-1272]
	ldr	r2, [r7, #-1276]
	bl	printk
	ldrh	r5, [r10, #-8]
	ldr	ip, [sp, #28]
	ldrh	r3, [ip, #4]
	str	r3, [sp, #28]
	b	.L2480
.L2478:
	mvn	r3, #0
	str	r3, [r4, #-1276]
.L2477:
	ldr	r3, [r4, #-1276]
	cmn	r3, #1
	beq	.L2480
	ldr	r3, [r7, #-1476]
	ldr	r2, .L2502+24
	ldr	r3, [r3]
	cmp	r3, r2
	bne	.L2480
	ldr	r3, [r7, #-1444]
	ldrh	r2, [r3]
	movw	r3, #61604
	cmp	r2, r3
	bne	.L2480
.L2484:
	ldr	r5, .L2502
	mov	r2, #48
	ldr	r1, [r4, #-1268]
	add	r0, r5, #816
	sub	r7, r5, #1728
	bl	ftl_memcpy
	ldrh	r2, [r6]
	ldr	r1, [r4, #-1268]
	ldr	r0, [r4, #-1408]
	add	r1, r1, #48
	mov	r2, r2, asl #1
	bl	ftl_memcpy
	ldrh	r2, [r6]
	ldr	r1, [r4, #-1268]
	ldr	r0, [r4, #-1396]
	mov	r3, r2, asl #1
	mov	r2, r2, lsr #3
	add	r3, r3, #51
	add	r2, r2, #4
	bic	r3, r3, #3
	add	r1, r1, r3
	bl	ftl_memcpy
	sub	r3, r5, #1616
	ldrh	r3, [r3, #-12]
	cmp	r3, #0
	beq	.L2482
	ldrh	r2, [r7]
	ldr	r1, [r5, #-1268]
	ldr	r0, [r5, #-1372]
	mov	r3, r2, lsr #3
	add	r3, r3, r2, asl #1
	sub	r2, r5, #1632
	add	r3, r3, #52
	ldrh	r2, [r2]
	ubfx	r3, r3, #2, #14
	add	r1, r1, r3, asl #2
	mov	r2, r2, asl #2
	bl	ftl_memcpy
	b	.L2482
.L2480:
	sub	r5, r5, #1
	uxth	r5, r5
	b	.L2476
.L2482:
	ldr	r2, [r4, #816]
	ldr	r3, .L2502+24
	ldr	r5, .L2502
	cmp	r2, r3
	bne	.L2485
	sub	r2, r5, #1712
	add	r8, r5, #816
	ldrb	r1, [r5, #826]	@ zero_extendqisi2
	ldrh	r2, [r2, #-2]
	ldrh	r3, [r8, #8]
	cmp	r1, r2
	strh	r3, [r9, #6]	@ movhi
	bne	.L2485
	sub	r2, r5, #1664
	sub	r1, r5, #1648
	ldr	r7, .L2502+28
	ldrh	r2, [r2, #-4]
	ldrh	r1, [r1, #-14]
	str	r3, [r7, #1156]
	mul	r2, r3, r2
	str	r2, [r5, #-1284]
	mul	r2, r1, r2
	ldrh	r1, [r6, #-8]
	mov	r6, #0
	str	r2, [r5, #-2740]
	ldr	r2, .L2502+32
	ldrh	r0, [r2, #6]
	ldr	r2, [r5, #-1724]
	rsb	r0, r0, r2
	rsb	r0, r3, r0
	bl	__aeabi_uidiv
	movw	r3, #1160
	ldrh	r1, [r8, #14]
	add	r2, r5, #884
	strb	r6, [r7, #1170]
	strb	r6, [r7, #1172]
	strh	r1, [r2]	@ movhi
	str	r6, [r5, #-1608]
	strh	r0, [r7, r3]	@ movhi
	ldrh	r3, [r8, #16]
	mov	r0, r3, lsr #6
	and	r3, r3, #63
	strb	r3, [r5, #890]
	ldrb	r3, [r5, #827]	@ zero_extendqisi2
	strh	r0, [r2, #2]	@ movhi
	mvn	r2, #0
	strb	r3, [r5, #892]
	movw	r3, #1164
	strh	r2, [r7, r3]	@ movhi
	ldr	r3, .L2502+36
	ldrh	r2, [r8, #18]
	strh	r6, [r3, #2]	@ movhi
	add	r3, r5, #932
	strh	r2, [r3]	@ movhi
	ldrh	r2, [r8, #20]
	mov	r0, r2, lsr #6
	and	r2, r2, #63
	strb	r2, [r5, #938]
	ldrb	r2, [r5, #828]	@ zero_extendqisi2
	strh	r0, [r3, #2]	@ movhi
	ldrh	r0, [r8, #22]
	ldrh	r8, [r8, #24]
	strb	r2, [r5, #940]
	add	r2, r5, #980
	strh	r0, [r2]	@ movhi
	mov	r0, r8, lsr #6
	strh	r0, [r2, #2]	@ movhi
	and	r8, r8, #63
	ldrb	r0, [r5, #829]	@ zero_extendqisi2
	mov	r9, r2
	strb	r8, [r5, #986]
	strb	r0, [r5, #988]
	str	r6, [r5, #-1604]
	ldr	r0, [r5, #848]
	ldr	r2, [r4, #-1612]
	ldr	ip, [r5, #-1616]
	str	r0, [r5, #-1584]
	ldr	r0, [r5, #856]
	str	r6, [r5, #-1588]
	str	r6, [r5, #-1592]
	cmp	r0, ip
	str	r6, [r5, #-1580]
	str	r6, [r5, #-1572]
	str	r6, [r5, #-1596]
	mov	r6, r3
	ldr	r3, [r4, #852]
	strhi	r0, [r5, #-1616]
	cmp	r3, r2
	ldrhi	r2, .L2502
	strhi	r3, [r2, #-1612]
	movw	r3, #65535
	cmp	r1, r3
	beq	.L2488
	ldr	r0, .L2502+40
	bl	make_superblock
.L2488:
	ldrh	r2, [r6]
	movw	r3, #65535
	cmp	r2, r3
	beq	.L2489
	ldr	r0, .L2502+44
	bl	make_superblock
.L2489:
	ldrh	r2, [r9]
	movw	r3, #65535
	cmp	r2, r3
	beq	.L2490
	ldr	r0, .L2502+48
	bl	make_superblock
.L2490:
	movw	r3, #1164
	ldrh	r2, [r7, r3]
	movw	r3, #65535
	cmp	r2, r3
	beq	.L2491
	ldr	r0, .L2502+36
	bl	make_superblock
.L2491:
	mov	r0, #0
.L2475:
	add	sp, sp, #44
	@ sp needed
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L2503:
	.align	2
.L2502:
	.word	.LANCHOR2
	.word	.LANCHOR2+1128
	.word	.LANCHOR2-1276
	.word	.LANCHOR0
	.word	.LANCHOR2-1658
	.word	.LC64
	.word	1179929683
	.word	.LANCHOR4
	.word	.LANCHOR2-1348
	.word	.LANCHOR4+1164
	.word	.LANCHOR2+884
	.word	.LANCHOR2+932
	.word	.LANCHOR2+980
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
	ldr	r7, .L2518
	ldr	r1, .L2518+4
	sub	r5, r7, #1664
	ldrh	r9, [r5, #-4]
	mov	r6, r0
	ldr	r0, .L2518+8
	bl	printk
	ldr	r2, [r7, #-1408]
	mov	r3, r6, asl #1
	mov	r1, r6
	ldr	r0, .L2518+12
	ldrh	r2, [r2, r3]
	bl	printk
	add	r0, sp, #96
	strh	r6, [r0, #-48]!	@ movhi
	bl	make_superblock
	ldrb	r2, [r7, #-2744]	@ zero_extendqisi2
	clz	r3, r4
	cmp	r2, #0
	mov	r3, r3, lsr #5
	moveq	r3, #0
	cmp	r3, #0
	moveq	r4, r3
	str	r7, [sp, #44]
	beq	.L2505
	mov	r0, r6
	bl	ftl_get_blk_mode
	cmp	r0, #1
	mov	r4, r0
	ldreqh	r9, [r5, #-2]
.L2505:
	ldr	r0, .L2518+16
	mov	r1, r4
	mov	r2, r9
	ldrh	r3, [r5, #-4]
	bl	printk
	ldr	r8, .L2518
	mov	r6, #0
.L2506:
	ldr	r3, .L2518+20
	mov	r2, #0
	add	r0, sp, #62
	mov	r5, r2
	movw	lr, #65535
	mov	r10, #36
	ldrh	r3, [r3]
	mov	r7, r3
	ldr	r3, [r8, #-1504]
	str	r3, [sp, #28]
	ldr	r3, [r8, #-1464]
	str	r3, [sp, #32]
	ldr	r3, .L2518+24
	ldrh	r3, [r3]
	str	r3, [sp, #36]
	ldr	r3, [r8, #-1436]
	str	r3, [sp, #40]
	ldr	r3, .L2518+28
	ldrh	ip, [r3]
.L2507:
	uxth	r3, r2
	cmp	r3, r7
	bcs	.L2516
	ldrh	r3, [r0, #2]!
	cmp	r3, lr
	beq	.L2508
	ldr	r1, [sp, #28]
	orr	r3, r6, r3, asl #10
	mla	r1, r10, r5, r1
	str	r3, [r1, #4]
	ldr	r3, [sp, #36]
	mul	r3, r3, r5
	add	fp, r3, #3
	cmp	r3, #0
	movlt	r3, fp
	ldr	fp, [sp, #32]
	bic	r3, r3, #3
	add	r3, fp, r3
	str	r3, [r1, #8]
	mul	r3, ip, r5
	add	r5, r5, #1
	uxth	r5, r5
	add	fp, r3, #3
	cmp	r3, #0
	movlt	r3, fp
	ldr	fp, [sp, #40]
	bic	r3, r3, #3
	add	r3, fp, r3
	str	r3, [r1, #12]
.L2508:
	add	r2, r2, #1
	b	.L2507
.L2516:
	ldr	r0, [r8, #-1504]
	mov	r1, r5
	mov	r2, r4
	mov	r10, #0
	bl	FlashReadPages
	mov	fp, #36
.L2510:
	uxth	r3, r10
	cmp	r3, r5
	bcs	.L2517
	ldr	r3, [sp, #44]
	mul	r2, fp, r10
	ldrh	r1, [sp, #48]
	ldr	lr, [r3, #-1504]
	add	r10, r10, #1
	add	ip, lr, r2
	ldr	r3, [ip, #12]
	ldr	r0, [ip, #8]
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
	ldr	r0, .L2518+32
	str	r3, [sp, #20]
	ldr	r2, [lr, r2]
	ldr	r3, [ip, #4]
	bl	printk
	b	.L2510
.L2517:
	add	r6, r6, #1
	uxth	r6, r6
	cmp	r6, r9
	bne	.L2506
.L2512:
	mov	r0, #0
	add	sp, sp, #100
	@ sp needed
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L2519:
	.align	2
.L2518:
	.word	.LANCHOR2
	.word	.LANCHOR3+160
	.word	.LC50
	.word	.LC65
	.word	.LC66
	.word	.LANCHOR2-1736
	.word	.LANCHOR2-1658
	.word	.LANCHOR2-1656
	.word	.LC60
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
	ldr	r0, .L2535
	mov	r7, #0
	ldr	r1, .L2535+4
	bl	printk
	ldr	r5, .L2535+8
	mov	r6, r5
.L2521:
	ldr	r3, .L2535+12
	uxth	r4, r7
	ldrh	r3, [r3]
	cmp	r3, r4
	bls	.L2531
	add	r8, sp, #88
	mov	r0, r4
	movw	r9, #65535
	mov	r10, #36
	strh	r4, [r8, #-48]!	@ movhi
	bl	ftl_get_blk_mode
	ldr	ip, [r5, #-1408]
	mov	r2, r4, asl #1
	mov	r1, r4
	ldrh	r2, [ip, r2]
	mov	r3, r0
	ldr	r0, .L2535+16
	bl	printk
	mov	r0, r8
	bl	make_superblock
	ldr	r3, .L2535+20
	ldr	lr, [r5, #-1436]
	ldrh	r2, [r3]
	ldrh	ip, [r3, #78]
	ldrh	r8, [r3, #80]
	str	r2, [sp, #28]
	add	r0, sp, #54
	ldr	r2, [r5, #-1504]
	str	r2, [sp, #32]
	ldr	r2, [r5, #-1464]
	str	r2, [sp, #36]
	mov	r2, #0
	mov	r4, r2
.L2522:
	ldr	r1, [sp, #28]
	uxth	r3, r2
	cmp	r3, r1
	bcs	.L2532
	ldrh	r3, [r0, #2]!
	cmp	r3, r9
	beq	.L2523
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
.L2523:
	add	r2, r2, #1
	b	.L2522
.L2532:
	ldr	r0, [r6, #-1504]
	mov	r1, r4
	mov	r2, #0
	mov	r8, #0
	bl	FlashReadPages
	mov	r9, #36
.L2525:
	uxth	r3, r8
	cmp	r3, r4
	bcs	.L2533
	mul	r2, r9, r8
	ldr	lr, [r6, #-1504]
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
	ldr	r0, .L2535+24
	str	r3, [sp, #20]
	ldr	r2, [lr, r2]
	ldr	r3, [ip, #4]
	bl	printk
	b	.L2525
.L2533:
	ldr	r0, [r6, #-1504]
	mov	r1, r4
	mov	r2, #1
	mov	r8, #0
	bl	FlashReadPages
	mov	r9, #36
.L2527:
	uxth	r3, r8
	cmp	r3, r4
	bcs	.L2534
	mul	r2, r9, r8
	ldr	lr, [r6, #-1504]
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
	ldr	r0, .L2535+28
	str	r3, [sp, #20]
	ldr	r2, [lr, r2]
	ldr	r3, [ip, #4]
	bl	printk
	b	.L2527
.L2534:
	add	r7, r7, #1
	b	.L2521
.L2531:
	mov	r0, #0
	add	sp, sp, #92
	@ sp needed
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L2536:
	.align	2
.L2535:
	.word	.LC50
	.word	.LANCHOR3+180
	.word	.LANCHOR2
	.word	.LANCHOR2-1726
	.word	.LC67
	.word	.LANCHOR2-1736
	.word	.LC68
	.word	.LC69
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
	ldr	r4, .L2549
	mov	r1, #0
	.pad #28
	sub	sp, sp, #28
	sub	r3, r4, #1712
	ldr	r0, [r4, #-1356]
	mov	fp, r4
	ldrh	r3, [r3, #-14]
	mul	r2, r2, r3
	bl	ftl_memset
	mov	r3, #0
	add	r1, r4, #872
	add	r2, r4, #880
	sub	r0, r4, #1616
	mov	r5, r3
	mov	r8, r3
	mov	r9, r3
	str	r3, [r4, #876]
	str	r3, [r4, #864]
	str	r3, [r4, #868]
	strh	r3, [r1]	@ movhi
	strh	r3, [r2]	@ movhi
	strh	r3, [r0, #-10]	@ movhi
	str	r1, [sp, #8]
	str	r2, [sp, #12]
.L2538:
	ldr	r3, .L2549+4
	uxth	r7, r5
	sxth	r10, r7
	ldrh	r2, [r3]
	cmp	r10, r2
	bge	.L2545
	sub	r3, r3, #8
	ldr	r2, .L2549+8
	mov	ip, r7
	ldrh	r3, [r3]
	str	r3, [sp]
	ldrh	r3, [r2]
	mov	r2, #0
	mov	r6, r2
	str	r3, [sp, #4]
.L2546:
	ldr	r3, [sp]
	sxth	r1, r2
	cmp	r1, r3
	bge	.L2548
	add	r1, r4, r1
	str	r2, [sp, #20]
	str	ip, [sp, #16]
	ldrb	r0, [r1, #-1708]	@ zero_extendqisi2
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
	b	.L2546
.L2548:
	cmp	r6, #0
	beq	.L2541
	sxth	r1, r6
	mov	r0, #32768
	bl	__aeabi_idiv
	uxth	r6, r0
	b	.L2542
.L2541:
	sxth	r7, r7
	ldr	r2, [r4, #-1408]
	mvn	r1, #0
	mov	r7, r7, asl #1
	strh	r1, [r2, r7]	@ movhi
.L2542:
	mov	r1, r10, asl #1
	ldr	r2, [r4, #-1356]
	add	r0, r1, r10
	add	r2, r2, r0, asl #1
	strh	r6, [r2, #4]	@ movhi
	ldr	r2, .L2549+12
	ldrh	r0, [r2]
	cmp	r10, r0
	beq	.L2543
	ldrh	r0, [r2, #48]
	cmp	r10, r0
	beq	.L2543
	ldrh	r2, [r2, #96]
	cmp	r10, r2
	beq	.L2543
	ldr	r3, [fp, #-1408]
	uxth	r0, r5
	ldrh	r3, [r3, r1]
	cmp	r3, #0
	bne	.L2544
	add	r8, r8, #1
	uxth	r8, r8
	bl	INSERT_FREE_LIST
	b	.L2543
.L2544:
	add	r9, r9, #1
	uxth	r9, r9
	bl	INSERT_DATA_LIST
.L2543:
	add	r5, r5, #1
	b	.L2538
.L2545:
	ldr	r3, [sp, #8]
	mov	r0, #0
	strh	r9, [r3]	@ movhi
	ldr	r3, [sp, #12]
	strh	r8, [r3]	@ movhi
	add	sp, sp, #28
	@ sp needed
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L2550:
	.align	2
.L2549:
	.word	.LANCHOR2
	.word	.LANCHOR2-1728
	.word	.LANCHOR2-1668
	.word	.LANCHOR2+884
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
	ldr	r3, .L2553
	ldr	r2, .L2553+4
	ldr	r1, [r3, #1212]
	cmp	r1, r2
	bxne	lr
	ldr	r2, .L2553+8
	mov	r0, #0
	str	r2, [r3, #1216]
	ldr	r2, [r3, #1724]
	str	r2, [r3, #1300]
	ldr	r2, [r3, #1728]
	str	r2, [r3, #1304]
	ldr	r2, .L2553+12
	ldr	r1, [r2, #-1608]
	str	r1, [r3, #1220]
	ldr	r1, [r2, #-1604]
	str	r1, [r3, #1224]
	ldr	r1, [r2, #-1588]
	str	r1, [r3, #1228]
	ldr	r1, [r2, #-1592]
	str	r1, [r3, #1232]
	ldr	r1, [r2, #-1580]
	str	r1, [r3, #1240]
	ldr	r1, [r2, #-1576]
	str	r1, [r3, #1244]
	ldr	r1, [r2, #-1600]
	str	r1, [r3, #1248]
	ldr	r1, [r2, #-1596]
	str	r1, [r3, #1252]
	ldr	r1, [r2, #-1572]
	str	r1, [r3, #1256]
	ldr	r1, [r2, #-1568]
	str	r1, [r3, #1260]
	ldr	r1, [r2, #-1620]
	ldr	r2, [r2, #-1624]
	str	r1, [r3, #1272]
	mov	r1, #1
	str	r2, [r3, #1276]
	ldr	r2, .L2553+16
	b	FtlVendorPartWrite
.L2554:
	.align	2
.L2553:
	.word	.LANCHOR4
	.word	1179929683
	.word	1342177379
	.word	.LANCHOR2
	.word	.LANCHOR4+1212
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
	ldr	r3, .L2563
	ldr	r2, [r3, #-1872]
	cmp	r2, #0
	moveq	r2, #32
	beq	.L2556
	ldr	r2, [r3, #-1568]
	cmp	r2, #39
	movhi	r2, #32
	movls	r2, #4
.L2556:
	ldr	lr, .L2563+4
	movw	ip, #1732
	ldrh	r1, [lr, ip]
	cmp	r1, #31
	addls	r1, r1, #1
	strlsh	r1, [lr, ip]	@ movhi
	movls	r2, #1
	cmp	r0, #0
	bne	.L2558
	ldr	r1, [r3, #-1420]
	ldr	r0, [r1, #20]
	ldr	r1, [r1, #16]
	add	r2, r2, r0
	cmp	r1, r2
	bcc	.L2559
.L2558:
	ldr	r2, [r3, #-1420]
	mov	r0, #64
	ldr	r1, [r2, #16]
	str	r1, [r2, #20]
	ldr	r1, .L2563+8
	str	r1, [r2]
	ldr	r2, [r3, #-1420]
	ldr	r3, .L2563+12
	ldrh	r1, [r3, #-4]
	mov	r3, r1, asl #9
	str	r3, [r2, #12]
	ldr	r3, [r2, #8]
	add	r3, r3, #1
	str	r3, [r2, #8]
	mov	r3, #0
	str	r3, [r2, #4]
	bl	FtlVendorPartWrite
	bl	Ftl_save_ext_data
.L2559:
	mov	r0, #0
	ldmfd	sp!, {r3, pc}
.L2564:
	.align	2
.L2563:
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
	ldr	r5, .L2571
	mov	r1, #1
	ldr	r2, .L2571+4
	bl	FtlVendorPartRead
	ldr	r4, .L2571+8
	ldr	r3, [r5, #1212]
	cmp	r3, r4
	beq	.L2566
	ldr	r0, .L2571+4
	mov	r1, #0
	mov	r2, #512
	bl	ftl_memset
	str	r4, [r5, #1212]
.L2566:
	ldr	r2, [r5, #1212]
	ldr	r3, .L2571
	cmp	r2, r4
	ldr	r4, .L2571+12
	bne	.L2567
	ldr	r2, [r3, #1300]
	str	r2, [r3, #1724]
	ldr	r2, [r3, #1304]
	str	r2, [r3, #1728]
	ldr	r2, [r3, #1220]
	str	r2, [r4, #-1608]
	ldr	r2, [r3, #1224]
	str	r2, [r4, #-1604]
	ldr	r2, [r3, #1228]
	str	r2, [r4, #-1588]
	ldr	r2, [r3, #1232]
	str	r2, [r4, #-1592]
	ldr	r2, [r3, #1240]
	str	r2, [r4, #-1580]
	ldr	r2, [r3, #1244]
	str	r2, [r4, #-1576]
	ldr	r2, [r3, #1248]
	str	r2, [r4, #-1600]
	ldr	r2, [r3, #1252]
	str	r2, [r4, #-1596]
	ldr	r2, [r3, #1256]
	str	r2, [r4, #-1572]
	ldr	r2, [r3, #1260]
	ldr	r3, [r3, #1272]
	str	r2, [r4, #-1568]
	str	r3, [r4, #-1620]
.L2567:
	ldr	r0, [r5, #1280]
	mov	r2, #0
	ldr	r1, .L2571+16
	ldr	r3, .L2571+12
	cmp	r0, r1
	str	r2, [r4, #-1624]
	bne	.L2568
	ldrb	r1, [r3, #-2744]	@ zero_extendqisi2
	cmp	r1, r2
	beq	.L2569
	ldr	r3, .L2571
	str	r2, [r3, #1280]
	bl	Ftl_save_ext_data
	b	.L2568
.L2569:
	ldr	r0, .L2571+20
	mov	r2, #1
	ldr	r1, .L2571+24
	str	r2, [r3, #-1872]
	bl	printk
.L2568:
	ldr	r3, .L2571+28
	ldr	r2, [r4, #-1580]
	ldr	r0, [r4, #-1584]
	ldrh	r1, [r3, #-12]
	mla	r0, r0, r1, r2
	ldrh	r1, [r3, #-64]
	bl	__aeabi_uidiv
	str	r0, [r5, #1736]
	ldmfd	sp!, {r3, r4, r5, pc}
.L2572:
	.align	2
.L2571:
	.word	.LANCHOR4
	.word	.LANCHOR4+1212
	.word	1179929683
	.word	.LANCHOR2
	.word	305432421
	.word	.LC48
	.word	.LC70
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
	stmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, lr}
	.save {r4, r5, r6, r7, r8, r9, r10, lr}
	.pad #24
	sub	sp, sp, #24
	ldr	r7, .L2592
	ldr	r9, .L2592+4
	ldrh	r4, [r7, #-6]
	add	r7, r7, #1664
	sub	r4, r4, #16
	mov	r5, r7
.L2574:
	ldrh	r3, [r9]
	cmp	r4, r3
	bge	.L2590
	uxth	r10, r4
	mov	r0, r10
	bl	ftl_get_blk_mode
	ldrb	r3, [r7, #-2744]	@ zero_extendqisi2
	cmp	r3, #0
	beq	.L2575
	ldr	r3, .L2592+8
	ldrh	r2, [r3]
	cmp	r4, r2
	bge	.L2576
	ldrh	r3, [r3, #74]
	cmp	r4, r3
	blt	.L2576
.L2575:
	cmp	r0, #1
	bne	.L2577
.L2576:
	ldr	r3, .L2592+12
	mov	r6, #-2147483648
	ldrh	r8, [r3]
	b	.L2578
.L2577:
	ldr	r3, .L2592+16
	mov	r6, #0
	ldrh	r8, [r3]
.L2578:
	mov	r1, r4
	mov	r2, r8
	mov	r3, r6
	ldr	r0, .L2592+20
	bl	printk
	mov	r0, r10
	bl	FtlBbmIsBadBlock
	cmp	r0, #0
	beq	.L2579
	ldr	r0, .L2592+24
	mov	r1, r4
	mov	r2, r8
	mov	r3, r6
	bl	printk
.L2579:
	add	r6, r6, r4, asl #10
	mov	r10, #0
.L2580:
	cmp	r10, r8
	beq	.L2591
	add	r3, r6, r10
	str	r3, [r5, #-1272]
	ldr	r3, [r5, #-1476]
	mov	r2, #0
	mov	r1, #1
	ldr	r0, .L2592+28
	str	r2, [r5, #-1276]
	add	r10, r10, #1
	str	r3, [r5, #-1268]
	ldr	r3, [r5, #-1444]
	str	r3, [r5, #-1264]
	bl	FlashReadPages
	ldr	r3, [r5, #-1264]
	ldr	r2, [r5, #-1268]
	ldr	r0, .L2592+32
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
	ldr	r1, [r5, #-1272]
	ldr	r2, [r5, #-1276]
	ldr	r3, [r3]
	bl	printk
	b	.L2580
.L2591:
	add	r4, r4, #1
	b	.L2574
.L2590:
	ldr	r0, .L2592+36
	ldr	r1, .L2592+40
	add	sp, sp, #24
	@ sp needed
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, lr}
	b	printk
.L2593:
	.align	2
.L2592:
	.word	.LANCHOR2-1664
	.word	.LANCHOR2-1670
	.word	.LANCHOR2-1728
	.word	.LANCHOR2-1666
	.word	.LANCHOR2-1668
	.word	.LC71
	.word	.LC72
	.word	.LANCHOR2-1276
	.word	.LC73
	.word	.LC74
	.word	.LANCHOR3+196
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
	ldr	r3, .L2604
	mov	r7, r0
	mov	r4, r5
	add	r6, r3, #20
	ldrh	r8, [r3, #-8]
	ldrh	r9, [r3, #60]
.L2595:
	uxth	r3, r5
	cmp	r3, r8
	bcs	.L2603
	ldrb	r0, [r6, r5]	@ zero_extendqisi2
	mov	r1, r7
	bl	V2P_block
	add	r5, r5, #1
	bl	FtlBbmIsBadBlock
	cmp	r0, #0
	addeq	r4, r4, r9
	uxtheq	r4, r4
	b	.L2595
.L2603:
	cmp	r4, #0
	beq	.L2598
	mov	r1, r4
	mov	r0, #32768
	bl	__aeabi_idiv
	uxth	r4, r0
.L2598:
	ldr	r3, .L2604+4
	mov	r2, #6
	mov	r0, #0
	ldr	r3, [r3, #-1356]
	mla	r7, r2, r7, r3
	strh	r4, [r7, #4]	@ movhi
	ldmfd	sp!, {r3, r4, r5, r6, r7, r8, r9, pc}
.L2605:
	.align	2
.L2604:
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
	ldr	r2, .L2609
	ldr	r0, [r2, #876]
	cmp	r0, #0
	bxeq	lr
	ldr	r3, [r2, #-1356]
	rsb	r0, r3, r0
	ldr	r3, .L2609+4
	mov	r0, r0, asr #1
	mul	r0, r3, r0
	ldr	r3, [r2, #-1416]
	uxth	r0, r0
	mov	r0, r0, asl #1
	ldrh	r0, [r3, r0]
	bx	lr
.L2610:
	.align	2
.L2609:
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
	ldr	r1, .L2621
	ldr	r3, [r1, #876]
	cmp	r3, #0
	beq	.L2617
	add	r2, r1, #880
	stmfd	sp!, {r4, r5, lr}
	.save {r4, r5, lr}
	mov	r4, #6
	ldrh	r2, [r2]
	movw	r5, #65535
	ldr	ip, [r1, #-1356]
	rsb	r2, r2, r2, asl #3
	rsb	r3, ip, r3
	mov	r2, r2, asr #3
	mov	r3, r3, asr #1
	cmp	r0, r2
	uxthgt	r0, r2
	ldr	r2, .L2621+4
	mul	r3, r2, r3
	mov	r2, #0
	uxth	r3, r3
.L2614:
	uxth	lr, r2
	cmp	lr, r0
	bcs	.L2616
	mul	lr, r4, r3
	add	r2, r2, #1
	ldrh	lr, [ip, lr]
	cmp	lr, r5
	bne	.L2618
.L2616:
	ldr	r2, [r1, #-1416]
	mov	r3, r3, asl #1
	ldrh	r0, [r2, r3]
	ldmfd	sp!, {r4, r5, pc}
.L2618:
	mov	r3, lr
	b	.L2614
.L2617:
	mov	r0, r3
	bx	lr
.L2622:
	.align	2
.L2621:
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
	ldr	r6, .L2634
	add	r5, r7, #12
	ldr	r1, .L2634+4
	.pad #32
	sub	sp, sp, #32
	bl	strcpy
	mov	r0, r5
	ldr	r1, .L2634+8
	ldr	r2, [r6, #-2772]
	bl	sprintf
	ldr	r1, .L2634+12
	ldr	r2, [r6, #-1652]
	add	r5, r5, r0
	mov	r0, r5
	bl	sprintf
	ldr	r3, .L2634+16
	ldr	r3, [r3, #3444]
	cmp	r3, #1
	add	r4, r5, r0
	bne	.L2629
	add	r0, sp, #16
	add	r1, sp, #20
	add	r2, sp, #24
	add	r3, sp, #28
	bl	NandcGetTimeCfg
	mov	r0, r4
	ldr	r1, .L2634+20
	add	r10, r6, #880
	ldr	r8, .L2634+24
	add	r9, r6, #816
	ldr	r3, [sp, #24]
	ldr	r2, [sp, #16]
	str	r3, [sp]
	ldr	r3, [sp, #28]
	str	r3, [sp, #4]
	ldr	r3, [sp, #20]
	bl	sprintf
	ldr	r1, .L2634+28
	add	r4, r4, r0
	add	r5, r4, #10
	mov	r0, r4
	sub	r4, r6, #1344
	bl	strcpy
	mov	r0, r5
	ldr	r1, .L2634+32
	ldr	r2, [r6, #-1284]
	bl	sprintf
	ldr	r1, .L2634+36
	ldr	r2, [r6, #1124]
	add	r5, r5, r0
	mov	r0, r5
	bl	sprintf
	ldr	r1, .L2634+40
	ldr	r2, [r6, #-1588]
	add	r5, r5, r0
	mov	r0, r5
	bl	sprintf
	ldr	r1, .L2634+44
	ldr	r2, [r6, #-1600]
	add	r5, r5, r0
	mov	r0, r5
	bl	sprintf
	ldr	r1, .L2634+48
	ldr	r2, [r6, #-1604]
	add	r5, r5, r0
	mov	r0, r5
	bl	sprintf
	ldr	r1, .L2634+52
	ldr	r2, [r6, #-1596]
	add	r5, r5, r0
	mov	r0, r5
	bl	sprintf
	ldr	r1, .L2634+56
	ldr	r2, [r6, #-1592]
	add	r5, r5, r0
	mov	r0, r5
	bl	sprintf
	ldr	r1, .L2634+60
	ldr	r2, [r6, #-1608]
	add	r5, r5, r0
	mov	r0, r5
	bl	sprintf
	ldr	r2, [r8, #1724]
	ldr	r1, .L2634+64
	mov	r2, r2, lsr #11
	add	r5, r5, r0
	mov	r0, r5
	bl	sprintf
	ldr	r2, [r8, #1728]
	ldr	r1, .L2634+68
	mov	r2, r2, lsr #11
	add	r5, r5, r0
	mov	r0, r5
	bl	sprintf
	ldr	r1, .L2634+72
	ldr	r2, [r6, #-1616]
	add	r5, r5, r0
	mov	r0, r5
	bl	sprintf
	ldr	r1, .L2634+76
	ldr	r2, [r6, #-1612]
	add	r5, r5, r0
	mov	r0, r5
	bl	sprintf
	add	r5, r5, r0
	bl	FtlBbtCalcTotleCnt
	ldrh	r2, [r4, #2]
	ldr	r1, .L2634+80
	mov	r3, r0
	mov	r0, r5
	bl	sprintf
	ldr	r1, .L2634+84
	ldrh	r2, [r10]
	add	r5, r5, r0
	mov	r0, r5
	bl	sprintf
	ldr	r1, .L2634+88
	ldr	r2, [r6, #-1584]
	add	r5, r5, r0
	mov	r0, r5
	bl	sprintf
	ldr	r1, .L2634+92
	ldr	r2, [r6, #-1580]
	add	r5, r5, r0
	mov	r0, r5
	bl	sprintf
	ldr	r1, .L2634+96
	ldr	r2, [r8, #1736]
	add	r5, r5, r0
	mov	r0, r5
	bl	sprintf
	ldr	r1, .L2634+100
	ldr	r2, [r6, #-1576]
	add	r5, r5, r0
	mov	r0, r5
	bl	sprintf
	ldr	r1, .L2634+104
	ldr	r2, [r6, #-1572]
	add	r5, r5, r0
	mov	r0, r5
	bl	sprintf
	ldr	r1, .L2634+108
	ldr	r2, [r6, #-1568]
	add	r5, r5, r0
	mov	r0, r5
	bl	sprintf
	ldrh	r2, [r9, #30]
	ldr	r1, .L2634+112
	add	r5, r5, r0
	mov	r0, r5
	bl	sprintf
	ldrh	r2, [r9, #28]
	ldr	r1, .L2634+116
	movw	r9, #1164
	add	r5, r5, r0
	mov	r0, r5
	bl	sprintf
	ldr	r1, .L2634+120
	ldr	r2, [r6, #-2740]
	add	r5, r5, r0
	mov	r0, r5
	bl	sprintf
	ldr	r1, .L2634+124
	ldr	r2, [r6, #-1636]
	add	r5, r5, r0
	mov	r0, r5
	bl	sprintf
	ldr	r1, .L2634+128
	ldr	r2, [r6, #-1740]
	add	r5, r5, r0
	mov	r0, r5
	bl	sprintf
	ldrh	r2, [r4, #110]
	ldr	r1, .L2634+132
	add	r5, r5, r0
	mov	r0, r5
	bl	sprintf
	sub	r3, r6, #1728
	ldr	r1, .L2634+136
	ldrh	r2, [r3]
	add	r5, r5, r0
	mov	r0, r5
	bl	sprintf
	movw	r3, #1160
	ldrh	r2, [r8, r3]
	ldr	r1, .L2634+140
	add	r5, r5, r0
	mov	r0, r5
	bl	sprintf
	ldr	r1, .L2634+144
	ldr	r2, [r6, #-1724]
	add	r5, r5, r0
	mov	r0, r5
	bl	sprintf
	movw	r3, #1128
	ldrh	r2, [r6, r3]
	ldr	r1, .L2634+148
	add	r5, r5, r0
	mov	r0, r5
	bl	sprintf
	ldrh	r2, [r4, #-4]
	ldr	r1, .L2634+152
	add	r4, r6, #884
	add	r5, r5, r0
	mov	r0, r5
	bl	sprintf
	ldrh	r2, [r4, #2]
	ldr	r1, .L2634+156
	add	r5, r5, r0
	mov	r0, r5
	bl	sprintf
	ldr	r1, .L2634+160
	ldrb	r2, [r6, #890]	@ zero_extendqisi2
	add	r5, r5, r0
	mov	r0, r5
	bl	sprintf
	ldrh	r2, [r4]
	ldr	r1, .L2634+164
	add	r5, r5, r0
	mov	r0, r5
	bl	sprintf
	ldr	r1, .L2634+168
	ldrb	r2, [r6, #892]	@ zero_extendqisi2
	add	r5, r5, r0
	mov	r0, r5
	bl	sprintf
	ldrh	r2, [r4, #4]
	ldr	r1, .L2634+172
	add	r5, r5, r0
	mov	r0, r5
	bl	sprintf
	ldrh	r3, [r4]
	ldr	r2, [r6, #-1408]
	add	r4, r6, #932
	ldr	r1, .L2634+176
	mov	r3, r3, asl #1
	ldrh	r2, [r2, r3]
	add	r5, r5, r0
	mov	r0, r5
	bl	sprintf
	ldrh	r2, [r4, #2]
	ldr	r1, .L2634+180
	add	r5, r5, r0
	mov	r0, r5
	bl	sprintf
	ldr	r1, .L2634+184
	ldrb	r2, [r6, #938]	@ zero_extendqisi2
	add	r5, r5, r0
	mov	r0, r5
	bl	sprintf
	ldrh	r2, [r4]
	ldr	r1, .L2634+188
	add	r5, r5, r0
	mov	r0, r5
	bl	sprintf
	ldr	r1, .L2634+192
	ldrb	r2, [r6, #940]	@ zero_extendqisi2
	add	r5, r5, r0
	mov	r0, r5
	bl	sprintf
	ldrh	r2, [r4, #4]
	ldr	r1, .L2634+196
	add	r5, r5, r0
	mov	r0, r5
	bl	sprintf
	ldrh	r3, [r4]
	ldr	r2, [r6, #-1408]
	add	r4, r6, #980
	ldr	r1, .L2634+200
	mov	r3, r3, asl #1
	ldrh	r2, [r2, r3]
	add	r5, r5, r0
	mov	r0, r5
	bl	sprintf
	ldrh	r2, [r4, #2]
	ldr	r1, .L2634+204
	add	r5, r5, r0
	mov	r0, r5
	bl	sprintf
	ldr	r1, .L2634+208
	ldrb	r2, [r6, #986]	@ zero_extendqisi2
	add	r5, r5, r0
	mov	r0, r5
	bl	sprintf
	ldrh	r2, [r4]
	ldr	r1, .L2634+212
	add	r5, r5, r0
	mov	r0, r5
	bl	sprintf
	ldr	r1, .L2634+216
	ldrb	r2, [r6, #988]	@ zero_extendqisi2
	add	r5, r5, r0
	mov	r0, r5
	bl	sprintf
	ldrh	r2, [r4, #4]
	ldr	r1, .L2634+220
	ldr	r4, .L2634+224
	add	r5, r5, r0
	mov	r0, r5
	bl	sprintf
	ldrh	r2, [r4, #2]
	ldr	r1, .L2634+228
	add	r5, r5, r0
	mov	r0, r5
	bl	sprintf
	ldr	r1, .L2634+232
	ldrb	r2, [r8, #1170]	@ zero_extendqisi2
	add	r5, r5, r0
	mov	r0, r5
	bl	sprintf
	ldr	r1, .L2634+236
	ldrh	r2, [r8, r9]
	add	r5, r5, r0
	mov	r0, r5
	bl	sprintf
	ldr	r1, .L2634+240
	ldrb	r2, [r8, #1172]	@ zero_extendqisi2
	add	r5, r5, r0
	mov	r0, r5
	bl	sprintf
	ldrh	r2, [r4, #4]
	ldr	r1, .L2634+244
	add	r5, r5, r0
	mov	r0, r5
	bl	sprintf
	ldr	r1, [r8, #1296]
	ldr	r2, [r6, #-1620]
	ldr	r3, [r6, #-1872]
	orr	r2, r3, r2, asl #8
	str	r1, [sp]
	add	r5, r5, r0
	ldr	r1, [r8, #1288]
	mov	r0, r5
	str	r1, [sp, #4]
	ldr	r1, .L2634+248
	ldr	r3, [r8, #1292]
	bl	sprintf
	ldr	r1, .L2634+252
	ldr	r2, [r8, #1284]
	add	r4, r5, r0
	sub	r5, r6, #1520
	mov	r0, r4
	bl	sprintf
	ldr	r1, .L2634+256
	ldr	r2, [r8, #1308]
	add	r4, r4, r0
	mov	r0, r4
	bl	sprintf
	ldr	r1, .L2634+260
	ldrh	r2, [r5, #-12]
	add	r4, r4, r0
	mov	r0, r4
	bl	sprintf
	ldr	r1, .L2634+264
	ldrh	r2, [r5, #-10]
	add	r4, r4, r0
	mov	r0, r4
	bl	sprintf
	ldr	r1, .L2634+268
	ldr	r2, [r6, #-1548]
	add	r4, r4, r0
	mov	r0, r4
	bl	sprintf
	ldr	r1, .L2634+272
	ldrh	r2, [r5, #-8]
	add	r4, r4, r0
	mov	r0, r4
	bl	sprintf
	add	r4, r4, r0
	bl	GetFreeBlockMinEraseCount
	ldr	r1, .L2634+276
	mov	r2, r0
	mov	r0, r4
	bl	sprintf
	add	r4, r4, r0
	ldrh	r0, [r10]
	bl	GetFreeBlockMaxEraseCount
	ldr	r1, .L2634+280
	mov	r2, r0
	mov	r0, r4
	bl	sprintf
	ldrh	r3, [r8, r9]
	movw	r2, #65535
	cmp	r3, r2
	add	r4, r4, r0
	beq	.L2626
	ldr	r2, [r6, #-1408]
	mov	r3, r3, asl #1
	mov	r0, r4
	ldr	r1, .L2634+284
	ldrh	r2, [r2, r3]
	bl	sprintf
	add	r4, r4, r0
.L2626:
	mov	r0, #0
	mov	r5, #0
	bl	List_get_gc_head_node
	movw	r10, #65535
	mov	r9, #6
	uxth	r3, r0
.L2628:
	cmp	r3, r10
	beq	.L2627
	ldr	r2, [r6, #-1408]
	mov	r1, r3, asl #1
	mul	r8, r9, r3
	mov	r0, r4
	ldrh	r2, [r2, r1]
	str	r2, [sp]
	ldr	r2, [r6, #-1356]
	add	r2, r2, r8
	ldrh	r2, [r2, #4]
	str	r2, [sp, #4]
	ldr	r2, [r6, #-1416]
	ldrh	r2, [r2, r1]
	ldr	r1, .L2634+288
	str	r2, [sp, #8]
	mov	r2, r5
	bl	sprintf
	add	r5, r5, #1
	ldr	r3, [r6, #-1356]
	cmp	r5, #16
	ldrh	r3, [r3, r8]
	add	r4, r4, r0
	bne	.L2628
.L2627:
	ldr	r2, [r6, #-1356]
	mov	r5, #0
	ldr	r3, [r6, #876]
	movw	r10, #65535
	mov	r9, #6
	rsb	r3, r2, r3
	ldr	r2, .L2634+292
	mov	r3, r3, asr #1
	mul	r3, r2, r3
	uxth	r3, r3
.L2630:
	cmp	r3, r10
	beq	.L2629
	mul	r8, r9, r3
	ldr	r2, [r6, #-1356]
	ldr	r1, [r6, #-1416]
	mov	r0, r4
	add	r2, r2, r8
	ldrh	r2, [r2, #4]
	str	r2, [sp]
	mov	r2, r3, asl #1
	ldrh	r2, [r1, r2]
	ldr	r1, .L2634+296
	str	r2, [sp, #4]
	mov	r2, r5
	bl	sprintf
	add	r5, r5, #1
	ldr	r3, [r6, #-1356]
	cmp	r5, #4
	ldrh	r3, [r3, r8]
	add	r4, r4, r0
	bne	.L2630
.L2629:
	rsb	r0, r7, r4
	add	sp, sp, #32
	@ sp needed
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, pc}
.L2635:
	.align	2
.L2634:
	.word	.LANCHOR2
	.word	.LC75
	.word	.LC76
	.word	.LC77
	.word	.LANCHOR1
	.word	.LC78
	.word	.LANCHOR4
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
	.word	.LANCHOR4+1164
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
	.word	-1431655765
	.word	.LC144
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
	ldr	r1, .L2638
	ldr	r2, .L2638+4
	bl	sprintf
	add	r4, r5, r0
	mov	r0, r4
	bl	FtlPrintInfo2buf
	add	r0, r4, r0
	rsb	r0, r5, r0
	ldmfd	sp!, {r3, r4, r5, pc}
.L2639:
	.align	2
.L2638:
	.word	.LC48
	.word	.LC145
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
	ldr	r5, .L2670
	ldr	r4, .L2670+4
	ldr	r6, [r5, #1736]
	ldr	r3, [r4, #-1568]
	cmp	r6, r3
	bcs	.L2641
	sub	r3, r4, #1728
	mov	r6, #0
	str	r6, [r4, #-1584]
	ldrh	r1, [r3]
	ldr	r3, [r4, #-1416]
	sub	r3, r3, #2
.L2642:
	cmp	r6, r1
	bcs	.L2669
	ldrh	r2, [r3, #2]!
	add	r6, r6, #1
	ldr	r0, [r4, #-1584]
	add	r2, r2, r0
	str	r2, [r4, #-1584]
	b	.L2642
.L2669:
	ldr	r6, [r4, #-1584]
	mov	r0, r6
	bl	__aeabi_uidiv
	ldr	r3, .L2670+8
	ldrh	r1, [r3, #-12]
	str	r0, [r5, #1736]
	ldr	r0, [r4, #-1580]
	rsb	r0, r0, r6
	bl	__aeabi_uidiv
	str	r0, [r4, #-1584]
	b	.L2644
.L2641:
	ldr	r3, [r4, #-1572]
	cmp	r6, r3
	bls	.L2644
	add	r3, r3, #1
	mov	ip, r4
	str	r3, [r4, #-1572]
	mov	r3, #0
.L2646:
	ldr	r2, .L2670+12
	ldrh	r2, [r2]
	cmp	r3, r2
	bcs	.L2644
	ldr	r0, [ip, #-1416]
	mov	r1, r3, asl #1
	add	r3, r3, #1
	ldrh	r2, [r0, r1]
	add	r2, r2, #1
	strh	r2, [r0, r1]	@ movhi
	b	.L2646
.L2644:
	ldr	r6, [r4, #-1568]
	ldr	r8, [r5, #1736]
	add	r3, r6, #256
	ldr	r2, .L2670+4
	cmp	r3, r8
	bls	.L2649
	ldr	r1, [r2, #-1572]
	add	r3, r6, #768
	cmp	r3, r1
	bls	.L2649
	cmp	r6, #40
	ldr	r2, [r2, #-1872]
	movls	r3, #0
	movhi	r3, #1
	cmp	r2, #0
	orreq	r3, r3, #1
	cmp	r3, #0
	beq	.L2649
.L2651:
	movw	r0, #65535
	b	.L2650
.L2649:
	ldr	r10, .L2670+4
	add	r3, r10, #880
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
	bne	.L2651
	ldr	r3, [r10, #864]
	cmp	r3, #0
	beq	.L2651
	sub	r2, r10, #1728
	ldr	r0, [r10, #-1356]
	ldr	ip, .L2670+16
	movw	r7, #65535
	ldrh	r2, [r2]
	mov	r5, r7
	ldr	r10, [r10, #-1416]
	mov	lr, #6
	str	r2, [sp, #20]
.L2652:
	ldrh	r2, [r3]
	movw	fp, #65535
	cmp	r2, fp
	beq	.L2654
	add	r1, r1, #1
	ldr	fp, [sp, #20]
	uxth	r1, r1
	cmp	r1, fp
	bhi	.L2651
	ldrh	fp, [r3, #4]
	cmp	fp, #0
	beq	.L2653
	rsb	r3, r0, r3
	mov	r3, r3, asr #1
	mul	r3, ip, r3
	uxth	r3, r3
	mov	fp, r3, asl #1
	ldrh	fp, [r10, fp]
	cmp	fp, r6
	bls	.L2658
	cmp	fp, r7
	movcc	r7, fp
	movcc	r5, r3
.L2653:
	mla	r3, lr, r2, r0
	b	.L2652
.L2658:
	mov	r5, r3
.L2654:
	movw	r3, #65535
	cmp	r5, r3
	beq	.L2651
	mov	r3, r5, asl #1
	ldrh	fp, [r10, r3]
	cmp	fp, r6
	bls	.L2656
	str	r3, [sp, #20]
	bl	GetFreeBlockMinEraseCount
	ldr	r3, [sp, #20]
	cmp	r0, r6
	strhi	r7, [r4, #-1568]
.L2656:
	cmp	fp, r8
	bcs	.L2651
	add	r2, fp, #128
	cmp	r9, r2
	ble	.L2651
	add	r2, fp, #256
	cmp	r2, r8
	bcc	.L2657
	ldr	r2, [r4, #-1572]
	add	fp, fp, #768
	cmp	fp, r2
	bcs	.L2651
.L2657:
	ldr	r2, [r4, #-1408]
	mov	r1, r5
	ldr	r0, .L2670+20
	ldrh	r2, [r2, r3]
	str	r2, [sp]
	mov	r2, r8
	ldrh	r3, [r10, r3]
	stmib	sp, {r3, r9}
	ldr	r3, [r4, #-1572]
	bl	printk
	mov	r0, r5
	mov	r3, #1
	str	r3, [r4, #-1560]
.L2650:
	add	sp, sp, #28
	@ sp needed
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L2671:
	.align	2
.L2670:
	.word	.LANCHOR4
	.word	.LANCHOR2
	.word	.LANCHOR2-1664
	.word	.LANCHOR2-1728
	.word	-1431655765
	.word	.LC146
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
	beq	.L2673
	ldr	r2, .L2675
	mov	r3, r0, asl #1
	mov	r1, #0
	ldr	r2, [r2, #-1408]
	strh	r1, [r2, r3]	@ movhi
	bl	INSERT_FREE_LIST
.L2673:
	mov	r0, #0
	ldmfd	sp!, {r3, pc}
.L2676:
	.align	2
.L2675:
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
	ldr	r4, .L2732
	ldr	r3, [r4, #-1280]
	cmp	r3, #0
	bne	.L2678
	sub	r10, r4, #1728
	mov	r5, r0
	mov	r6, r4
.L2679:
	ldr	r7, .L2732+4
	ldrb	r2, [r5, #8]	@ zero_extendqisi2
	cmp	r5, r7
	bne	.L2680
	ldrh	r3, [r7, #-100]
	ldr	ip, [r6, #-1560]
	mov	r0, r3, lsr #1
	mul	lr, ip, r3
	add	r1, r0, #1
	add	r1, r1, lr, lsr #2
	ldr	lr, [r6, #-1872]
	cmp	lr, #0
	uxth	r1, r1
	beq	.L2681
	ldr	lr, [r6, #-1568]
	cmp	lr, #39
	bhi	.L2681
	cmp	lr, #2
	bls	.L2706
	cmp	ip, #0
	movne	r3, #0
	andeq	r3, r3, #1
	cmp	r3, #0
	moveq	r1, r0
	beq	.L2681
	b	.L2706
.L2680:
	cmp	r2, #1
	bne	.L2706
	ldr	r3, .L2732+8
	ldrh	r3, [r3]
	cmp	r3, #1
	beq	.L2706
	ldrb	r3, [r6, #-2744]	@ zero_extendqisi2
	cmp	r3, #0
	bne	.L2706
	ldr	r0, [r6, #-1872]
	ldrh	r3, [r7, #-100]
	cmp	r0, #0
	mov	r1, r3, lsr #3
	beq	.L2681
	ldr	r0, [r6, #-1568]
	cmp	r0, #1
	rsbls	r3, r3, r3, asl #3
	ubfxls	r1, r3, #3, #16
.L2681:
	cmp	r1, #0
	subne	r1, r1, #1
	uxthne	r1, r1
	b	.L2682
.L2706:
	mov	r1, #0
.L2682:
	ldr	r0, .L2732+12
	bl	List_pop_index_node
	ldrh	r3, [r7, #-100]
	sub	r3, r3, #1
	strh	r3, [r7, #-100]	@ movhi
	ldrh	r3, [r10]
	uxth	r9, r0
	cmp	r3, r9
	bls	.L2679
	ldr	r3, [r6, #-1408]
	mov	r8, r9, asl #1
	ldrh	r7, [r3, r8]
	cmp	r7, #0
	bne	.L2679
	strh	r9, [r5]	@ movhi
	mov	r0, r5
	bl	make_superblock
	ldrb	r3, [r5, #7]	@ zero_extendqisi2
	cmp	r3, #0
	beq	.L2684
	ldr	r2, .L2732+16
	add	ip, r5, #14
	ldr	r0, [r6, #-1492]
	ldrh	lr, [r2]
	mov	r2, #36
	mov	r3, r0
	mla	r1, r2, lr, r0
	mov	lr, r7
	str	r1, [sp]
	b	.L2685
.L2684:
	ldr	r3, [r6, #-1408]
	b	.L2727
.L2685:
	ldr	r1, [sp]
	cmp	r3, r1
	beq	.L2729
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
	b	.L2685
.L2729:
	ldr	r3, .L2732+20
	ldr	r2, [r4, #-1872]
	rsb	r3, r3, r5
	clz	r3, r3
	cmp	r2, #0
	mov	r3, r3, lsr #5
	moveq	r3, #0
	cmp	r3, #0
	beq	.L2688
	ldr	r3, [r6, #-1416]
	ldrh	r3, [r3, r8]
	cmp	r3, #40
	movhi	r3, #0
	strhib	r3, [r6, #892]
.L2688:
	ldrb	r3, [r5, #8]	@ zero_extendqisi2
	ldr	r2, [r4, #-1416]
	cmp	r3, #0
	ldrh	r3, [r2, r8]
	bne	.L2689
	cmp	r3, #0
	mov	r0, r9
	ldrne	r1, .L2732+24
	moveq	r3, #2
	ldrneh	r1, [r1]
	addne	r3, r3, r1
	mov	r1, #0
	uxthne	r3, r3
	strh	r3, [r2, r8]	@ movhi
	ldr	r3, [r4, #-1584]
	add	r3, r3, #1
	str	r3, [r4, #-1584]
	bl	ftl_set_blk_mode
	b	.L2691
.L2689:
	add	r3, r3, #1
	strh	r3, [r2, r8]	@ movhi
	ldr	r1, [r4, #-1396]
	mov	r0, r9, lsr #5
	ldr	r3, [r4, #-1580]
	mov	ip, #1
	add	r3, r3, #1
	str	r3, [r4, #-1580]
	ldr	r2, [r1, r0, asl #2]
	and	r3, r9, #31
	orr	r3, r2, ip, asl r3
	str	r3, [r1, r0, asl #2]
.L2691:
	ldr	r3, [r4, #-1416]
	ldr	r2, [r4, #-1572]
	ldr	r0, [r4, #-1584]
	ldrh	r3, [r3, r8]
	cmp	r3, r2
	strhi	r3, [r6, #-1572]
	ldr	r3, .L2732+24
	ldrh	r2, [r3]
	ldr	r3, [r4, #-1580]
	mla	r0, r0, r2, r3
	ldr	r3, .L2732+28
	ldrh	r1, [r3]
	bl	__aeabi_uidiv
	ldr	r2, [r4, #-1420]
	ldr	r3, .L2732+32
	str	r0, [r3, #1736]
	ldr	r3, [r2, #16]
	add	r3, r3, #1
	str	r3, [r2, #16]
	ldr	r2, [r4, #-1492]
	mov	r3, #36
	add	r1, r2, #4
	mla	r3, r3, r7, r2
	add	r3, r3, #40
.L2693:
	add	r1, r1, #36
	cmp	r1, r3
	ldrne	r2, [r1, #-36]
	bicne	r2, r2, #1020
	bicne	r2, r2, #3
	strne	r2, [r1, #-36]
	bne	.L2693
.L2730:
	ldrb	r3, [r4, #-2744]	@ zero_extendqisi2
	cmp	r3, #0
	beq	.L2695
	ldrb	r3, [r5, #8]	@ zero_extendqisi2
	mov	r2, r7
	ldr	r0, [r6, #-1492]
	cmp	r3, #1
	moveq	r1, #0
	movne	r1, #1
	bl	FlashEraseBlocks
.L2695:
	ldrb	r1, [r5, #8]	@ zero_extendqisi2
	mov	r2, r7
	ldr	r0, [r4, #-1492]
	mov	fp, #0
	bl	FlashEraseBlocks
	add	r1, r5, #16
	mov	r2, fp
	mov	ip, #36
.L2697:
	uxth	r3, fp
	cmp	r3, r7
	bcs	.L2731
	mul	r3, ip, fp
	ldr	lr, [r4, #-1492]
	add	r0, lr, r3
	ldr	r3, [lr, r3]
	cmn	r3, #1
	bne	.L2698
	ldr	r0, [r0, #4]
	add	r2, r2, #1
	stmib	sp, {r1, r3, ip}
	ubfx	r0, r0, #10, #16
	str	r2, [sp]
	bl	FtlBbmMapBadBlock
	ldmib	sp, {r1, r3}
	ldr	ip, [sp, #12]
	ldr	r2, [sp]
	strh	r3, [r1]	@ movhi
	ldrb	r3, [r5, #7]	@ zero_extendqisi2
	sub	r3, r3, #1
	strb	r3, [r5, #7]
.L2698:
	add	fp, fp, #1
	add	r1, r1, #2
	b	.L2697
.L2731:
	cmp	r2, #0
	beq	.L2700
	mov	r0, r9
	bl	update_multiplier_value
	bl	FtlBbmTblFlush
.L2700:
	ldrb	r3, [r5, #7]	@ zero_extendqisi2
	cmp	r3, #0
	bne	.L2701
	ldr	r3, [r4, #-1408]
.L2727:
	mvn	r2, #0
	strh	r2, [r3, r8]	@ movhi
	b	.L2679
.L2701:
	ldr	r2, .L2732+36
	ldrh	r2, [r2, #-4]
	strh	r9, [r5]	@ movhi
	smulbb	r3, r2, r3
	mov	r2, #0
	strh	r2, [r5, #2]	@ movhi
	strb	r2, [r5, #6]
	ldr	r2, [r4, #-1616]
	ldr	r1, [r4, #-1408]
	uxth	r3, r3
	strh	r3, [r5, #4]	@ movhi
	str	r2, [r5, #12]
	add	r2, r2, #1
	str	r2, [r4, #-1616]
	ldrh	r2, [r5]
	mov	r2, r2, asl #1
	strh	r3, [r1, r2]	@ movhi
.L2678:
	mov	r0, #0
	add	sp, sp, #20
	@ sp needed
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L2733:
	.align	2
.L2732:
	.word	.LANCHOR2
	.word	.LANCHOR2+980
	.word	.LANCHOR2-1716
	.word	.LANCHOR2+876
	.word	.LANCHOR2-1736
	.word	.LANCHOR2+884
	.word	.LANCHOR2-1676
	.word	.LANCHOR2-1728
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
	ldr	lr, .L2742
	mov	r5, #1
	ldr	r2, .L2742+4
	mov	r7, #36
	ldr	r1, .L2742+8
	add	r6, lr, #78
	mov	r3, #0
	str	r3, [r2, #1740]
.L2735:
	ldrh	r2, [lr]
	add	ip, r3, #1
	uxth	r3, r3
	ldr	r0, .L2742+8
	cmp	r3, r2
	bcs	.L2740
	mul	r0, r4, r3
	ldr	r8, [r1, #-1448]
	add	r2, r8, r0
	str	r5, [r2, #8]
	ldrh	r2, [r6]
	mul	r2, r2, r3
	add	r9, r2, #3
	cmp	r2, #0
	movlt	r2, r9
	ldr	r9, [r1, #-1464]
	bic	r2, r2, #3
	add	r2, r9, r2
	str	r2, [r8, r0]
	ldr	r2, .L2742+12
	ldr	r9, [r1, #-1448]
	ldrh	r2, [r2]
	add	r8, r9, r0
	mul	r2, r2, r3
	add	r10, r2, #3
	cmp	r2, #0
	movlt	r2, r10
	ldr	r10, [r1, #-1436]
	bic	r2, r2, #3
	add	r2, r10, r2
	str	r2, [r8, #4]
	ldr	r2, [r1, #-1488]
	mla	r3, r7, r3, r2
	ldr	r2, [r9, r0]
	str	r2, [r3, #8]
	ldr	r2, [r8, #4]
	str	r2, [r3, #12]
	mov	r3, ip
	b	.L2735
.L2740:
	ldr	r4, .L2742+16
	mov	r1, r0
	mov	ip, #12
	mov	lr, #0
.L2737:
	ldr	r3, [r0, #-1480]
	cmp	r2, r3
	bcs	.L2741
	mul	r5, ip, r2
	ldr	r7, [r1, #-1448]
	add	r3, r7, r5
	str	lr, [r3, #8]
	ldrh	r3, [r4]
	mul	r3, r3, r2
	add	r6, r3, #3
	cmp	r3, #0
	movlt	r3, r6
	ldr	r6, [r1, #-1464]
	bic	r3, r3, #3
	add	r3, r6, r3
	str	r3, [r7, r5]
	ldr	r3, .L2742+12
	ldr	r6, [r1, #-1448]
	ldrh	r3, [r3]
	add	r5, r6, r5
	mul	r3, r3, r2
	add	r2, r2, #1
	uxth	r2, r2
	add	r6, r3, #3
	cmp	r3, #0
	movlt	r3, r6
	ldr	r6, [r1, #-1436]
	bic	r3, r3, #3
	add	r3, r6, r3
	str	r3, [r5, #4]
	b	.L2737
.L2741:
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, pc}
.L2743:
	.align	2
.L2742:
	.word	.LANCHOR2-1736
	.word	.LANCHOR4
	.word	.LANCHOR2
	.word	.LANCHOR2-1656
	.word	.LANCHOR2-1658
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
	movw	r2, #1748
	ldr	r6, .L2746
	mvn	r3, #0
	ldr	r5, .L2746+4
	mov	r4, #0
	mov	r1, r4
	strh	r3, [r6, r2]	@ movhi
	sub	r7, r5, #1712
	str	r3, [r6, #1760]
	sub	r3, r5, #1280
	ldr	r0, [r5, #-1392]
	strh	r4, [r3, #-8]	@ movhi
	sub	r3, r5, #1648
	str	r4, [r6, #1744]
	ldrh	r2, [r3]
	str	r4, [r6, #1752]
	str	r4, [r6, #1756]
	mov	r2, r2, asl #1
	str	r4, [r5, #-1872]
	bl	ftl_memset
	ldrh	r2, [r7, #-14]
	mov	r1, r4
	ldr	r0, [r5, #-1416]
	mov	r2, r2, asl #1
	bl	ftl_memset
	ldrh	r2, [r7, #-14]
	mov	r1, r4
	ldr	r0, [r5, #-1424]
	mov	r2, r2, asl #1
	bl	ftl_memset
	mov	r1, r4
	add	r0, r5, #816
	mov	r2, #48
	bl	ftl_memset
	add	r0, r6, #1200
	mov	r1, r4
	mov	r2, #512
	add	r0, r0, #12
	bl	ftl_memset
	bl	FtlGcBufInit
	bl	FtlL2PDataInit
	mov	r0, r4
	ldmfd	sp!, {r3, r4, r5, r6, r7, pc}
.L2747:
	.align	2
.L2746:
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
	ldr	r3, .L2756
	stmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, lr}
	.save {r4, r5, r6, r7, r8, r9, r10, lr}
	mov	lr, #0
	ldr	r6, [r3, #-1480]
	mov	r5, #36
	ldr	r4, [r3, #-1448]
	mov	r7, lr
	mov	r8, #12
.L2749:
	uxth	ip, lr
	cmp	ip, r1
	ldmcsfd	sp!, {r4, r5, r6, r7, r8, r9, r10, pc}
	mla	ip, r5, ip, r0
	mov	r2, #0
.L2750:
	uxth	r3, r2
	cmp	r3, r6
	bcs	.L2751
	mul	r3, r8, r3
	add	r2, r2, #1
	add	r9, r4, r3
	ldr	r10, [r4, r3]
	ldr	r3, [ip, #8]
	cmp	r10, r3
	bne	.L2750
	str	r7, [r9, #8]
.L2751:
	add	lr, lr, #1
	b	.L2749
.L2757:
	.align	2
.L2756:
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
	ldr	r3, .L2767
	mov	ip, #0
	stmfd	sp!, {r4, r5, r6, r7, r8, r9, lr}
	.save {r4, r5, r6, r7, r8, r9, lr}
	mov	r6, #12
	ldr	r4, [r3, #-1480]
	mov	r7, #1
	ldr	r5, [r3, #-1448]
	mov	r8, #36
.L2759:
	uxth	r2, ip
	cmp	r2, r1
	bcs	.L2766
	mov	lr, #0
.L2760:
	uxth	r3, lr
	cmp	r3, r4
	bcs	.L2761
	mla	r3, r6, r3, r5
	add	lr, lr, #1
	ldr	r9, [r3, #8]
	cmp	r9, #0
	bne	.L2760
	mla	r2, r8, r2, r0
	ldr	lr, [r3]
	str	r7, [r3, #8]
	str	lr, [r2, #8]
	ldr	r3, [r3, #4]
	str	r3, [r2, #12]
.L2761:
	add	ip, ip, #1
	b	.L2759
.L2766:
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, pc}
.L2768:
	.align	2
.L2767:
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
	ldr	r1, .L2775
	movw	r2, #1764
	ldr	r3, .L2775+4
	ldrh	r2, [r1, r2]
	ldr	r3, [r3, #-1512]
	add	r2, r3, r2, asl #1
.L2770:
	cmp	r3, r2
	beq	.L2774
	ldrh	r1, [r3], #2
	cmp	r1, r0
	bne	.L2770
	mov	r0, #1
	bx	lr
.L2774:
	mov	r0, #0
	bx	lr
.L2776:
	.align	2
.L2775:
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
	ldr	lr, .L2785
	movw	r2, #1764
	ldr	r3, .L2785+4
	ldr	r1, [lr, #-1512]
	ldrh	r7, [r3, r2]
	mov	r2, #0
	sub	r8, r1, #2
.L2778:
	uxth	ip, r2
	cmp	ip, r7
	bcs	.L2782
	ldrh	r9, [r8, #2]!
	add	r2, r2, #1
	cmp	r9, r0
	bne	.L2778
.L2782:
	cmp	ip, r7
	bne	.L2780
	mov	ip, ip, asl #1
	movw	r2, #1764
	strh	r0, [r1, ip]	@ movhi
	ldrh	r0, [r3, r2]
	add	r0, r0, #1
	strh	r0, [r3, r2]	@ movhi
.L2780:
	movw	r0, #1766
	mov	ip, #12
	ldrh	r2, [r3, r0]
	mul	ip, ip, r2
	ldr	r2, [lr, #-1508]
	add	r1, r2, ip
	stmib	r1, {r5, r6}
	str	r4, [r2, ip]
	ldrh	r2, [r3, r0]
	add	r2, r2, #1
	strh	r2, [r3, r0]	@ movhi
	ldmfd	sp!, {r3, r4, r5, r6, r7, r8, r9, pc}
.L2786:
	.align	2
.L2785:
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
	movw	r1, #1764
	ldr	r3, .L2789
	mov	r2, #0
	ldr	r4, .L2789+4
	sub	r5, r4, #1664
	strh	r2, [r3, r1]	@ movhi
	movw	r1, #1766
	ldr	r0, [r4, #-1512]
	strh	r2, [r3, r1]	@ movhi
	mov	r1, #255
	ldrh	r2, [r5]
	mov	r2, r2, asl #1
	bl	ftl_memset
	ldrh	r3, [r5]
	mov	r2, #12
	ldr	r0, [r4, #-1508]
	mov	r1, #255
	mul	r2, r2, r3
	bl	ftl_memset
	ldmfd	sp!, {r3, r4, r5, lr}
	b	FtlGcBufInit
.L2790:
	.align	2
.L2789:
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
	ldr	r2, .L2844
	movw	r3, #3448
	stmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.save {r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.pad #68
	sub	sp, sp, #68
	ldrh	r5, [r2, r3]
	movw	r3, #65535
	mov	r4, r0
	str	r1, [sp, #8]
	cmp	r5, r3
	beq	.L2825
	cmp	r5, #0
	bne	.L2792
	b	.L2793
.L2825:
	mov	r5, #0
.L2792:
	ldr	r3, .L2844+4
	ldr	r2, [sp, #8]
	ldrh	r3, [r3, #-4]
	cmp	r2, r3
	bne	.L2794
.L2793:
	bl	FtlGcPageVarInit
.L2794:
	ldr	r6, .L2844+8
	mov	r2, #0
	mvn	r3, #0
	stmia	sp, {r2, r3}
.L2795:
	ldrh	r1, [r4]
	movw	r3, #65535
	mov	r2, #0
	strb	r2, [r4, #8]
	cmp	r1, r3
	beq	.L2796
.L2797:
.L2822:
	ldr	r3, .L2844+12
	mov	r7, #0
	ldr	r0, [r6, #-1436]
	add	r1, r4, #14
	mov	r8, r7
	movw	lr, #65535
	ldrh	r3, [r3]
	mov	r9, #36
	str	r3, [sp, #12]
	ldr	r3, [r6, #-1504]
	str	r3, [sp, #16]
	ldr	r3, [r6, #-1464]
	str	r3, [sp, #20]
	ldr	r3, .L2844+16
	ldrh	fp, [r3]
	ldrh	ip, [r3, #2]
.L2798:
	ldr	r2, [sp, #12]
	uxth	r3, r7
	cmp	r3, r2
	bcs	.L2842
	ldrh	r3, [r1, #2]!
	cmp	r3, lr
	beq	.L2799
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
.L2799:
	add	r7, r7, #1
	b	.L2798
.L2842:
	ldr	r0, [r6, #-1504]
	mov	r1, r8
	mov	r2, #0
	mov	r10, #0
	bl	FlashReadPages
.L2801:
	uxth	r3, r10
	cmp	r3, r8
	bcs	.L2843
	ldr	r3, .L2844+8
	mov	r9, #36
	mul	r9, r9, r10
	ldr	r7, [r3, #-1504]
	add	r3, r7, r9
	str	r3, [sp, #12]
	ldr	fp, [r3, #4]
	ubfx	r0, fp, #10, #16
	bl	P2V_plane
	ldr	r7, [r7, r9]
	ldr	ip, .L2844+8
	cmp	r7, #0
	ldr	r3, [sp, #12]
	mov	r2, r0
	ldr	r3, [r3, #12]
	bne	.L2802
	ldrh	r0, [r3]
	movw	r1, #65535
	cmp	r0, r1
	bne	.L2803
.L2806:
	ldrb	r1, [ip, #-2744]	@ zero_extendqisi2
	cmp	r1, #0
	beq	.L2837
	ldr	r3, .L2844+20
	mov	r1, #1
	str	r1, [r3, #1756]
	b	.L2796
.L2803:
	ldr	r0, [r3, #8]
	ldr	r1, [r6, #-1284]
	cmp	r0, r1
	bls	.L2838
	b	.L2806
.L2837:
	ldrh	r3, [r4]
	ldr	r2, [r6, #-1408]
	mov	r3, r3, asl #1
	b	.L2841
.L2838:
	ldr	r2, .L2844+24
	ldrb	r2, [r2]	@ zero_extendqisi2
	cmp	r2, #0
	beq	.L2810
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
	beq	.L2810
	str	r2, [sp, #32]
	mov	r1, #1
	ldr	r2, [r6, #-1452]
	add	r0, sp, #28
	str	r2, [sp, #36]
	ldr	r2, [r6, #-1440]
	str	r2, [sp, #40]
	mov	r2, r7
	bl	FlashReadPages
	ldr	r2, .L2844+28
	ldr	r1, [r6, #-1504]
	ldrh	r2, [r2]
	add	r9, r1, r9
	mov	r2, r2, asl #7
	ldr	r1, [sp, #36]
	ldr	r3, [sp, #12]
	b	.L2812
.L2813:
	add	r7, r7, #1
.L2812:
	cmp	r7, r2
	beq	.L2810
	ldr	r0, [r9, #8]
	ldr	ip, [r0, r7, asl #2]
	ldr	r0, [r1, r7, asl #2]
	cmp	ip, r0
	beq	.L2813
	ldrh	r1, [r4]
	ldr	r2, [sp, #32]
	ldr	r0, .L2844+32
	bl	printk
	ldrh	r3, [r4]
	ldr	r2, [r6, #-1408]
	mov	r1, #0
	mov	r3, r3, asl #1
.L2841:
	strh	r1, [r2, r3]	@ movhi
	ldrh	r0, [r4]
	bl	INSERT_FREE_LIST
	ldr	r2, .L2844+36
	mvn	r3, #0
	strh	r3, [r4]	@ movhi
	strh	r3, [r2]	@ movhi
	b	.L2840
.L2810:
	ldr	r0, [r3, #12]
	mov	r1, fp
	ldr	r2, [r3, #8]
	add	r10, r10, #1
	bl	FtlGcUpdatePage
	b	.L2801
.L2802:
	ldr	r0, .L2844+40
	mov	r2, fp
	ldrh	r1, [r4]
	str	ip, [sp, #12]
	bl	printk
	ldr	r3, .L2844+8
	ldrh	r5, [r4]
	ldr	r3, [r3, #-1872]
	cmp	r3, #0
	ldr	ip, [sp, #12]
	bne	.L2816
	ldr	r3, .L2844+8
	ldrb	r3, [r3, #-2744]	@ zero_extendqisi2
	cmp	r3, #0
	beq	.L2817
.L2816:
	ldr	r2, [ip, #-1416]
	mov	r3, r5, asl #1
	ldrh	r3, [r2, r3]
	cmp	r3, #159
	bls	.L2818
.L2817:
	ldr	r3, [ip, #-1504]
	ldr	r3, [r3, r9]
	cmn	r3, #1
	bne	.L2819
.L2818:
	ldr	r3, [ip, #-1504]
	add	r9, r3, r9
	ldr	r3, [r9, #4]
	str	r3, [sp, #4]
.L2819:
	ldr	r3, .L2844+8
	mov	r5, r5, asl #1
	mov	r2, #0
	ldr	r3, [r3, #-1408]
	strh	r2, [r3, r5]	@ movhi
	ldrh	r0, [r4]
	bl	INSERT_FREE_LIST
	mvn	r3, #0
	strh	r3, [r4]	@ movhi
.L2840:
	bl	FtlGcPageVarInit
	mov	r5, #0
	b	.L2795
.L2843:
	ldr	r3, [sp]
	add	r5, r5, #1
	ldr	r2, [sp, #8]
	add	r3, r3, #1
	uxth	r5, r5
	cmp	r3, r2
	str	r3, [sp]
	ldr	r2, .L2844+44
	bcs	.L2821
.L2823:
	ldrh	r3, [r2]
	cmp	r3, r5
	bhi	.L2822
	mov	r2, #0
	b	.L2796
.L2821:
	ldr	r1, .L2844+48
	movw	r0, #65535
	ldrh	r3, [r1]
	cmp	r3, r0
	beq	.L2823
	ldr	r0, [sp]
	add	r3, r3, r0
	strh	r3, [r1]	@ movhi
	ldrh	r3, [r2]
	cmp	r3, r5
	bls	.L2823
	b	.L2824
.L2796:
	ldr	r1, .L2844
	movw	r3, #3448
	mvn	r0, #0
	strh	r5, [r4, #2]	@ movhi
	strb	r2, [r4, #6]
	strh	r0, [r1, r3]	@ movhi
	mov	r0, r4
	mov	r1, r5
	bl	ftl_sb_update_avl_pages
.L2824:
	ldr	r0, [sp, #4]
	add	sp, sp, #68
	@ sp needed
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L2845:
	.align	2
.L2844:
	.word	.LANCHOR1
	.word	.LANCHOR2-1664
	.word	.LANCHOR2
	.word	.LANCHOR2-1736
	.word	.LANCHOR2-1658
	.word	.LANCHOR4
	.word	.LANCHOR0
	.word	.LANCHOR2-1662
	.word	.LC147
	.word	.LANCHOR4+1164
	.word	.LC148
	.word	.LANCHOR2-1668
	.word	.LANCHOR1+3448
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
	ldr	r4, .L2854
	ldrh	r3, [r4, #-4]
	cmp	r3, r0
	beq	.L2848
	ldrh	r3, [r4, #-2]
	cmp	r3, r0
	beq	.L2848
	ldrh	r3, [r4]
	cmp	r3, r0
	beq	.L2848
	add	r6, r4, #16
	ldrh	r3, [r6, #-14]
	cmp	r3, r0
	beq	.L2848
	ldr	r0, .L2854+4
	mov	r1, r5
	bl	printk
	ldrh	r2, [r4, #-4]
	movw	r3, #65535
	cmp	r2, r3
	streqh	r5, [r4, #-4]	@ movhi
	beq	.L2848
	ldrh	r2, [r4, #-2]
	cmp	r2, r3
	streqh	r5, [r4, #-2]	@ movhi
	beq	.L2848
	ldrh	r2, [r4]
	cmp	r2, r3
	streqh	r5, [r4]	@ movhi
	beq	.L2848
	ldrh	r2, [r6, #-14]
	cmp	r2, r3
	streqh	r5, [r6, #-14]	@ movhi
.L2848:
	mov	r0, #0
	ldmfd	sp!, {r4, r5, r6, pc}
.L2855:
	.align	2
.L2854:
	.word	.LANCHOR2-1536
	.word	.LC149
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
	ldr	r4, .L2867
	ldrh	r3, [r4, #-4]
	cmp	r3, r0
	beq	.L2864
	ldrh	r3, [r4, #-2]
	cmp	r3, r0
	beq	.L2864
	ldrh	r3, [r4]
	cmp	r3, r0
	beq	.L2864
	add	r6, r4, #16
	ldrh	r3, [r6, #-14]
	cmp	r3, r0
	beq	.L2864
	ldr	r0, .L2867+4
	mov	r1, r5
	bl	printk
	ldrh	r2, [r4, #-4]
	movw	r3, #65535
	cmp	r2, r3
	streqh	r5, [r4, #-4]	@ movhi
	beq	.L2864
	ldrh	r2, [r4, #-2]
	cmp	r2, r3
	streqh	r5, [r4, #-2]	@ movhi
	beq	.L2864
	ldrh	r2, [r4]
	cmp	r2, r3
	streqh	r5, [r4]	@ movhi
	beq	.L2864
	ldrh	r2, [r6, #-14]
	cmp	r2, r3
	bne	.L2865
	strh	r5, [r6, #-14]	@ movhi
.L2864:
	mov	r0, #0
	ldmfd	sp!, {r4, r5, r6, pc}
.L2865:
	mvn	r0, #0
	ldmfd	sp!, {r4, r5, r6, pc}
.L2868:
	.align	2
.L2867:
	.word	.LANCHOR2-1536
	.word	.LC149
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
	ldr	r6, .L2879
	mov	r2, r5
	sub	r7, r6, #1520
	ldrh	r1, [r7, #-6]
	mov	r4, r0
	ldr	r0, .L2879+4
	bl	printk
	mov	r0, r4
	bl	FtlGcRefreshBlock
	ldr	r3, [r6, #-1872]
	cmp	r3, #0
	beq	.L2870
	ldr	r2, [r6, #-1416]
	mov	r4, r4, asl #1
	ldrh	r3, [r2, r4]
	cmp	r3, #39
	subhi	r3, r3, #40
	strhih	r3, [r2, r4]	@ movhi
.L2870:
	ldrh	r3, [r7, #-6]
	mov	r2, #0
	ldr	r0, .L2879+8
.L2871:
	uxth	r1, r2
	cmp	r1, r3
	bcs	.L2878
	add	r2, r2, #1
	add	r1, r0, r2, asl #1
	ldrh	r1, [r1, #-2]
	cmp	r1, r5
	bne	.L2871
	b	.L2872
.L2878:
	cmp	r3, #15
	addls	r2, r3, #1
	strlsh	r2, [r7, #-6]	@ movhi
	ldrls	r2, .L2879+8
	movls	r3, r3, asl #1
	strlsh	r5, [r2, r3]	@ movhi
.L2872:
	mov	r0, #0
	ldmfd	sp!, {r3, r4, r5, r6, r7, pc}
.L2880:
	.align	2
.L2879:
	.word	.LANCHOR2
	.word	.LC150
	.word	.LANCHOR4+1768
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
	ldr	r4, .L2888
	ldrh	r3, [r4, #-6]
	cmp	r3, #0
	beq	.L2882
	ldrh	r1, [r4, #-20]
	movw	r2, #65535
	cmp	r1, r2
	bne	.L2882
	ldrh	r2, [r4, #-2]
	cmp	r2, r3
	ldr	r2, .L2888+4
	movcs	r3, #0
	strcsh	r3, [r4, #-2]	@ movhi
	ldrh	r3, [r4, #-2]
	mov	r3, r3, asl #1
	ldrh	r0, [r2, r3]
	bl	P2V_block_in_plane
	bl	FtlGcRefreshBlock
	ldrh	r3, [r4, #-2]
	add	r3, r3, #1
	strh	r3, [r4, #-2]	@ movhi
.L2882:
	mov	r0, #0
	ldmfd	sp!, {r4, pc}
.L2889:
	.align	2
.L2888:
	.word	.LANCHOR2-1520
	.word	.LANCHOR4+1768
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
	ldr	r5, .L2904
	ldrh	r3, [r5, #-6]
	cmp	r3, #0
	beq	.L2892
	sub	r5, r5, #6
	mov	r8, r0
	mov	r7, #0
	mov	r9, r5
.L2891:
	ldr	r3, .L2904+4
	uxth	r2, r7
	ldrh	r3, [r3]
	cmp	r3, r2
	bls	.L2901
	ldr	r3, .L2904+8
	mov	r1, r8
	mov	r10, #0
	add	r3, r3, r2
	ldrb	r0, [r3, #-1708]	@ zero_extendqisi2
	bl	V2P_block
	ldr	ip, .L2904+12
	mov	fp, r0
.L2893:
	ldrh	r3, [r5]
	uxth	r4, r10
	cmp	r3, r4
	bls	.L2902
	mov	r3, r4, asl #1
	add	r6, ip, r3
	ldrh	r3, [ip, r3]
	cmp	r3, fp
	bne	.L2894
	mov	r1, fp
	ldr	r0, .L2904+16
	str	ip, [sp, #4]
	bl	printk
	mov	r0, fp
	bl	FtlBbmMapBadBlock
	bl	FtlBbmTblFlush
	ldrh	r2, [r5]
	mov	r3, r6
	ldr	ip, [sp, #4]
.L2895:
	cmp	r4, r2
	ldrcch	r1, [r3, #2]
	addcc	r4, r4, #1
	uxthcc	r4, r4
	strcch	r1, [r3], #2	@ movhi
	bcc	.L2895
.L2903:
	sub	r2, r2, #1
	strh	r2, [r9]	@ movhi
.L2894:
	add	r10, r10, #1
	b	.L2893
.L2902:
	add	r7, r7, #1
	b	.L2891
.L2901:
	bl	FtlGcReFreshBadBlk
.L2892:
	mov	r0, #0
	add	sp, sp, #12
	@ sp needed
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L2905:
	.align	2
.L2904:
	.word	.LANCHOR2-1520
	.word	.LANCHOR2-1736
	.word	.LANCHOR2
	.word	.LANCHOR4+1768
	.word	.LC151
	.fnend
	.size	FtlGcFreeBadSuperBlk, .-FtlGcFreeBadSuperBlk
	.align	2
	.global	update_vpc_list
	.type	update_vpc_list, %function
update_vpc_list:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	ldr	r2, .L2916
	mov	r3, r0, asl #1
	stmfd	sp!, {r4, lr}
	.save {r4, lr}
	mov	r4, r0
	ldr	r1, [r2, #-1408]
	ldrh	r3, [r1, r3]
	cmp	r3, #0
	bne	.L2907
	ldr	r0, .L2916+4
	movw	r1, #1164
	ldrh	ip, [r0, r1]
	cmp	ip, r4
	mvneq	r3, #0
	streqh	r3, [r0, r1]	@ movhi
	beq	.L2909
	add	r1, r2, #884
	ldrh	r1, [r1]
	cmp	r1, r4
	beq	.L2915
	add	r1, r2, #932
	ldrh	r1, [r1]
	cmp	r1, r4
	beq	.L2915
	add	r2, r2, #980
	ldrh	r2, [r2]
	cmp	r2, r4
	beq	.L2915
.L2909:
	mov	r1, r4
	ldr	r0, .L2916+8
	bl	List_remove_node
	ldr	r2, .L2916+12
	mov	r0, r4
	ldrh	r3, [r2]
	sub	r3, r3, #1
	strh	r3, [r2]	@ movhi
	bl	free_data_superblock
	mov	r0, r4
	bl	FtlGcFreeBadSuperBlk
	mov	r0, #1
	ldmfd	sp!, {r4, pc}
.L2907:
	bl	List_update_data_list
.L2915:
	mov	r0, #0
	ldmfd	sp!, {r4, pc}
.L2917:
	.align	2
.L2916:
	.word	.LANCHOR2
	.word	.LANCHOR4
	.word	.LANCHOR2+864
	.word	.LANCHOR2+872
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
	beq	.L2919
	ldr	r5, .L2929
	mov	r6, r0, asl #1
	ldr	r3, [r5, #-1408]
	ldrh	r2, [r3, r6]
	cmp	r2, #0
	subne	r2, r2, #1
	strneh	r2, [r3, r6]	@ movhi
	bne	.L2919
	mov	r1, r4
	ldr	r0, .L2929+4
	bl	printk
	ldr	r3, [r5, #-1408]
	mov	r2, #32
	add	r0, r5, #876
	mov	r1, r4
	strh	r2, [r3, r6]	@ movhi
	bl	test_node_in_list
	cmp	r0, #0
	beq	.L2921
	mov	r1, r4
	add	r0, r5, #876
	bl	List_remove_node
	add	r2, r5, #880
	mov	r0, r4
	ldrh	r3, [r2]
	sub	r3, r3, #1
	strh	r3, [r2]	@ movhi
	bl	INSERT_DATA_LIST
	ldr	r3, [r5, #-1408]
	ldr	r0, .L2929+8
	mov	r1, r4
	ldrh	r2, [r3, r6]
	bl	printk
.L2921:
	mov	r0, r4
	bl	FtlGcRefreshBlock
	b	.L2924
.L2919:
	ldr	r6, .L2929+12
	movw	r5, #1748
	movw	r3, #65535
	ldrh	r0, [r6, r5]
	cmp	r0, r3
	streqh	r4, [r6, r5]	@ movhi
	beq	.L2924
	cmp	r0, r4
	beq	.L2924
	bl	update_vpc_list
	strh	r4, [r6, r5]	@ movhi
	adds	r0, r0, #0
	movne	r0, #1
	ldmfd	sp!, {r4, r5, r6, pc}
.L2924:
	mov	r0, #0
	ldmfd	sp!, {r4, r5, r6, pc}
.L2930:
	.align	2
.L2929:
	.word	.LANCHOR2
	.word	.LC152
	.word	.LC153
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
	beq	.L3075
	stmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.save {r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.pad #68
	sub	sp, sp, #68
	ldrh	r3, [r0, #2]
	str	r3, [sp, #8]
	ldrb	r3, [r0, #6]	@ zero_extendqisi2
	ldr	r0, [sp, #8]
	str	r3, [sp]
	ldr	r3, .L3090
	ldrh	r3, [r3, #-4]
	cmp	r3, r0
	mov	r3, #0
	streqh	r3, [r1, #4]	@ movhi
	streqb	r3, [r1, #6]
	ldrneh	r0, [r1, #16]
	beq	.L3072
.L2935:
	cmp	r0, r2
	add	r3, r3, #1
	uxtheq	r0, r3
	addeq	r0, r1, r0, asl #1
	ldreqh	r0, [r0, #16]
	beq	.L2935
.L3085:
	mov	r9, r1
	ldrb	r1, [r1, #8]	@ zero_extendqisi2
	cmp	r1, #1
	bne	.L2937
	bl	FtlGetLastWrittenPage
	cmn	r0, #1
	mov	r4, r0
	beq	.L2938
	ldr	r3, .L3090+4
	ldrb	r3, [r3, #-2744]	@ zero_extendqisi2
	cmp	r3, #0
	bne	.L3076
	ldr	r3, .L3090+8
	add	r3, r3, r0, asl #1
	ldrh	r6, [r3, #80]
	b	.L3009
.L2937:
	mov	r1, #0
	bl	FtlGetLastWrittenPage
	cmn	r0, #1
	mov	r4, r0
	beq	.L2938
.L3076:
	mov	r6, r4
.L3009:
	ldr	r3, .L3090+4
	movw	r8, #65535
	sub	r2, r3, #1728
	sub	r3, r3, #1648
	ldr	ip, [r3, #144]
	ldr	lr, [r3, #212]
	ldrh	r2, [r2, #-8]
	ldrh	r7, [r3, #-8]
	add	r3, r9, #14
	str	r3, [sp, #20]
	str	r2, [sp, #4]
	mov	r2, #0
	mov	r0, r3
	mov	r5, r2
	mov	r10, r2
	b	.L2940
.L2938:
	mov	r3, #0
	strh	r3, [r9, #2]	@ movhi
	strb	r3, [r9, #6]
	b	.L3072
.L2942:
	ldrh	r3, [r0, #2]!
	cmp	r3, r8
	beq	.L2941
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
.L2941:
	add	r2, r2, #1
.L2940:
	ldr	r1, [sp, #4]
	uxth	r3, r2
	cmp	r3, r1
	bcc	.L2942
	ldrb	r3, [r9, #8]	@ zero_extendqisi2
	ldr	fp, .L3090+4
	cmp	r3, #1
	movne	r3, #0
	bne	.L3077
	ldrb	lr, [fp, #-2744]	@ zero_extendqisi2
	adds	r3, lr, #0
	movne	r3, #1
.L3077:
	str	r3, [sp, #24]
	mov	r1, r5
	ldr	r2, [sp, #24]
	mov	r10, #0
	ldr	r0, [fp, #-1504]
	bl	FlashReadPages
	ldr	r3, [fp, #-1612]
	ldr	r2, .L3090+4
	str	r3, [sp, #28]
	movw	r3, #65535
	str	r3, [sp, #16]
.L2944:
	uxth	r7, r10
	cmp	r7, r5
	bcs	.L2951
	mov	r1, #36
	ldr	r0, [r2, #-1504]
	mul	r1, r1, r10
	add	ip, r0, r1
	ldr	r1, [r0, r1]
	cmp	r1, #0
	bne	.L2945
	ldr	ip, [ip, #12]
	ldr	r8, [ip, #4]
	cmn	r8, #1
	beq	.L2946
	ldr	r1, [r2, #-1612]
	mov	r0, r8
	str	ip, [sp, #12]
	str	r2, [sp, #4]
	bl	ftl_cmp_data_ver
	ldr	r2, [sp, #4]
	cmp	r0, #0
	ldr	ip, [sp, #12]
	addne	r8, r8, #1
	strne	r8, [r2, #-1612]
.L2946:
	ldr	r1, [ip]
	cmn	r1, #1
	bne	.L2947
.L2951:
	cmp	r7, r5
	ldr	r5, .L3090+4
	bne	.L3073
	add	fp, r4, #1
	uxth	r3, fp
	str	r3, [sp, #4]
	ldr	r3, [r5, #-1504]
	ldr	r0, [r3, #4]
	b	.L3078
.L2945:
	ldr	r1, [ip, #4]
	ldr	r0, .L3090+12
	str	r2, [sp, #4]
	bl	printk
	uxth	r3, r6
	ldrh	r1, [r9]
	str	r3, [sp, #16]
	ldr	r2, [sp, #4]
	ldr	r3, .L3090+16
	strh	r1, [r3]	@ movhi
.L2947:
	add	r10, r10, #1
	b	.L2944
.L3073:
	uxth	r3, r4
	str	r3, [sp, #4]
	ldr	r3, [fp, #-1504]
	mov	r2, #36
	mla	r7, r2, r7, r3
	ldr	r0, [r7, #4]
.L3078:
	ubfx	r0, r0, #10, #16
	bl	P2V_plane
	ldrb	r3, [r9, #8]	@ zero_extendqisi2
	cmp	r3, #1
	str	r0, [sp, #12]
	bne	.L2953
	ldrb	r2, [r5, #-2744]	@ zero_extendqisi2
	cmp	r2, #0
	ldreq	r1, [sp, #4]
	ldreq	r2, .L3090+8
	addeq	r2, r2, r1, asl #1
	ldreqh	r2, [r2, #80]
	streq	r2, [sp, #4]
.L2953:
	ldr	r2, .L3090
	ldr	r1, [sp, #4]
	ldr	r0, [sp, #12]
	ldrh	r2, [r2, #-4]
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
	beq	.L3083
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
	beq	.L2957
	ldr	r3, .L3090+20
	uxth	fp, r6
	ldr	r8, .L3090+4
	mvn	r7, #0
	mov	r6, r7
	ldr	r2, [r3, #1760]
	cmn	r2, #1
	ldreq	r2, [sp]
	streq	r2, [r3, #1760]
	ldr	r10, [r3, #1760]
	ldr	r3, [sp, #8]
	add	r3, r3, #7
	cmp	fp, r3
	subgt	r4, fp, #7
	ldrle	r4, [sp, #8]
	uxthgt	r4, r4
.L2960:
	cmp	r4, fp
	bhi	.L2973
	ldr	r3, .L3090+24
	mov	r0, #36
	ldr	lr, [r8, #-1504]
	ldr	r1, [sp, #20]
	ldrh	r3, [r3]
	str	r3, [sp, #16]
	mov	r3, #0
	mov	r5, r3
.L2974:
	ldr	ip, [sp, #16]
	uxth	r2, r3
	cmp	r2, ip
	bcs	.L3086
	ldrh	r2, [r1, #2]!
	movw	ip, #65535
	add	r3, r3, #1
	cmp	r2, ip
	orrne	r2, r4, r2, asl #10
	mlane	ip, r0, r5, lr
	addne	r5, r5, #1
	uxthne	r5, r5
	strne	r2, [ip, #4]
	b	.L2974
.L3086:
	mov	r1, r5
	ldr	r2, [sp, #24]
	ldr	r0, [r8, #-1504]
	bl	FlashReadPages
	ldr	r3, [r8, #-1504]
	mov	r2, #36
	ldrb	ip, [r8, #-2744]	@ zero_extendqisi2
	movw	r1, #65535
	mla	r5, r2, r5, r3
	ldr	r2, .L3090+28
	add	r2, r2, r4, asl #1
.L2963:
	cmp	r3, r5
	beq	.L3087
	ldr	r0, [r3]
	cmp	r0, #0
	bne	.L2964
	ldr	r0, [r3, #12]
	ldrh	lr, [r0]
	cmp	lr, r1
	beq	.L2965
	ldr	r0, [r0, #4]
	cmn	r0, #1
	beq	.L2965
	ldr	lr, .L3090+20
	cmn	r7, #1
	ldr	r6, [lr, #1760]
	str	r0, [lr, #1760]
	bne	.L2965
	ldrh	r0, [r2]
	cmp	r0, r1
	bne	.L2966
	cmp	ip, #0
	beq	.L2965
.L2966:
	ldr	r0, [sp]
	cmp	r6, r0
	mvneq	r7, #0
	movne	r7, r6
	b	.L2965
.L2964:
	ldrh	r1, [r9]
	movw	r2, #1802
	ldr	r3, .L3090+20
	strh	r1, [r3, r2]	@ movhi
	ldrb	r2, [r9, #8]	@ zero_extendqisi2
	cmp	r2, #0
	bne	.L2957
	ldr	r2, .L3090+28
	mov	r4, r4, asl #1
	ldrh	r1, [r2, r4]
	movw	r2, #65535
	cmp	r1, r2
	bne	.L2968
	cmn	r7, #1
	strne	r7, [r3, #1760]
	bne	.L2957
	ldr	r2, [sp]
	cmp	r10, r2
	strne	r10, [r3, #1760]
	ldreq	r2, [r3, #1760]
	bne	.L2957
	b	.L3084
.L2968:
	ldr	r2, [sp]
	cmp	r6, r2
	beq	.L2971
	cmn	r6, #1
	strne	r6, [r3, #1760]
	b	.L2957
.L2971:
	ldr	r2, [r3, #1760]
	ldr	r1, [sp]
	cmp	r2, r1
	beq	.L2957
.L3084:
	sub	r2, r2, #1
	b	.L3079
.L2965:
	add	r3, r3, #36
	b	.L2963
.L3087:
	add	r4, r4, #1
	uxth	r4, r4
	b	.L2960
.L2973:
	ldr	r3, .L3090+20
	mvn	r2, #0
.L3079:
	str	r2, [r3, #1760]
.L2957:
	ldr	fp, .L3090+20
	movw	r3, #1804
	ldr	r10, [sp, #8]
	mov	r2, #1
	strh	r2, [fp, r3]	@ movhi
.L2975:
	ldr	r3, .L3090+4
	movw	lr, #65535
	ldr	r2, .L3090+24
	mov	r1, #36
	ldr	r5, [sp, #20]
	mov	r4, #0
	ldr	r7, [r3, #-1504]
	ldrh	r6, [r2]
	ldrb	r8, [r3, #-2744]	@ zero_extendqisi2
	str	r4, [sp, #16]
.L2976:
	uxth	r3, r4
	cmp	r3, r6
	bcs	.L3088
	ldrh	r3, [r5, #2]!
	cmp	r3, lr
	beq	.L2977
	ldr	r2, [sp, #16]
	orr	r3, r10, r3, asl #10
	mla	r2, r1, r2, r7
	str	r3, [r2, #4]
	ldrb	r0, [r9, #8]	@ zero_extendqisi2
	cmp	r0, #1
	bne	.L2978
	cmp	r8, #0
	orrne	r3, r3, #-2147483648
	strne	r3, [r2, #4]
.L2978:
	ldr	r3, [sp, #16]
	add	ip, r3, #1
	uxth	r3, ip
	str	r3, [sp, #16]
.L2977:
	add	r4, r4, #1
	b	.L2976
.L3088:
	ldr	r4, .L3090+4
	ldr	r1, [sp, #16]
	ldr	r2, [sp, #24]
	ldr	r0, [r4, #-1504]
	bl	FlashReadPages
	mov	r3, #0
.L3082:
	str	r3, [sp, #28]
	ldr	r2, [sp, #16]
	ldrh	r3, [sp, #28]
	cmp	r3, r2
	bcs	.L3089
	ldr	r3, [sp, #28]
	mov	r5, #36
	ldr	r8, [r4, #-1504]
	mul	r5, r5, r3
	add	r7, r8, r5
	ldr	r6, [r7, #4]
	ubfx	r0, r6, #10, #16
	str	r6, [sp, #60]
	bl	P2V_plane
	ldr	r3, [sp, #8]
	cmp	r10, r3
	bcc	.L2981
	ldr	r3, [sp, #32]
	ldr	r2, [sp, #8]
	cmp	r0, r3
	movcs	r3, #0
	movcc	r3, #1
	cmp	r10, r2
	movne	r3, #0
	cmp	r3, #0
	bne	.L2981
	ldr	r3, [sp, #12]
	ldr	r2, [sp, #4]
	cmp	r0, r3
	cmpeq	r10, r2
	beq	.L2982
	ldr	r3, [r8, r5]
	cmn	r3, #1
	beq	.L2983
	ldr	r7, [r7, #12]
	movw	r3, #61589
	ldrh	r2, [r7]
	cmp	r2, r3
	ldrneh	r0, [r9]
	bne	.L3080
	ldr	r3, [r7, #4]
	cmn	r3, #1
	str	r3, [sp]
	beq	.L2985
	mov	r0, r3
	ldr	r1, [r4, #-1612]
	bl	ftl_cmp_data_ver
	cmp	r0, #0
	ldrne	r3, [sp]
	addne	r3, r3, #1
	strne	r3, [r4, #-1612]
.L2985:
	ldr	r6, [r7, #8]
	add	r1, sp, #56
	ldr	r3, [r7, #12]
	mov	r2, #0
	mov	r0, r6
	str	r3, [sp, #52]
	bl	log2phys
	ldr	r1, [fp, #1760]
	cmn	r1, #1
	beq	.L2986
	ldr	r0, [sp]
	bl	ftl_cmp_data_ver
	cmp	r0, #0
	beq	.L2986
	ldr	r3, [sp, #52]
	cmn	r3, #1
	beq	.L2987
	ldr	r0, [r4, #-1504]
	mov	r2, #0
	mov	r1, #1
	add	r0, r0, r5
	str	r3, [r0, #4]
	ldr	r8, [r0, #12]
	bl	FlashReadPages
	ldr	r2, [r4, #-1504]
	ldr	r3, [r8, #4]
	add	ip, r2, r5
	str	r3, [sp, #36]
	ldr	r3, [r2, r5]
	cmn	r3, #1
	bne	.L2988
	b	.L2989
.L2987:
	ldr	r3, [sp, #60]
	ldr	r2, [sp, #56]
	cmp	r2, r3
	bne	.L2981
	mov	r0, r6
	add	r1, sp, #52
	mov	r2, #1
	bl	log2phys
	b	.L2981
.L2988:
	ldr	r7, [r8, #8]
	cmp	r7, r6
	bne	.L2989
	ldr	r0, [fp, #1760]
	ldr	r1, [sp, #36]
	str	r2, [sp, #44]
	str	ip, [sp, #40]
	bl	ftl_cmp_data_ver
	cmp	r0, #0
	ldr	ip, [sp, #40]
	ldr	r2, [sp, #44]
	beq	.L2989
	ldr	r3, [sp, #56]
	ldr	r1, [sp, #60]
	cmp	r3, r1
	beq	.L2994
	ldr	r1, [sp, #52]
	cmp	r3, r1
	beq	.L2989
	cmn	r3, #1
	streq	r3, [r2, r5]
	beq	.L2993
	str	r3, [ip, #4]
	mov	r0, ip
	mov	r1, #1
	mov	r2, #0
	ldr	r8, [ip, #12]
	bl	FlashReadPages
.L2993:
	ldr	r3, [r4, #-1504]
	ldr	r3, [r3, r5]
	cmn	r3, #1
	beq	.L2994
	ldr	r5, [r8, #4]
	ldr	r0, [fp, #1760]
	mov	r1, r5
	bl	ftl_cmp_data_ver
	cmp	r0, #0
	beq	.L2994
	ldr	r0, [sp, #36]
	mov	r1, r5
	bl	ftl_cmp_data_ver
	cmp	r0, #0
	beq	.L2989
.L2994:
	mov	r0, r7
	ldr	r1, [sp, #52]
	bl	FtlReUsePrevPpa
.L2989:
	mvn	r3, #0
	str	r3, [sp, #52]
	b	.L2996
.L2986:
	ldr	r3, [sp, #60]
	ldr	r2, [sp, #56]
	cmp	r2, r3
	beq	.L2996
	ldr	r3, [sp, #52]
	cmn	r3, #1
	beq	.L2998
	ldr	r2, [r4, #-1720]
	ubfx	r3, r3, #10, #21
	cmp	r3, r2
	bcs	.L2981
.L2998:
	mov	r0, r6
	add	r1, sp, #60
	mov	r2, #1
	bl	log2phys
	ldr	r8, [sp, #56]
	cmn	r8, #1
	beq	.L2996
	ldr	r3, [sp, #52]
	cmp	r8, r3
	beq	.L2996
	ubfx	r0, r8, #10, #16
	ldr	r5, .L3090+32
	bl	P2V_block_in_plane
	ldrh	r3, [r5]
	cmp	r3, r0
	beq	.L3000
	add	r2, r5, #48
	ldrh	r2, [r2]
	cmp	r2, r0
	beq	.L3000
	add	r3, r5, #96
	ldrh	r3, [r3]
	cmp	r3, r0
	bne	.L2996
.L3000:
	ldr	r0, [r5, #-2388]
	mov	r1, #1
	mov	r2, #0
	str	r8, [r0, #4]
	ldr	r7, [r0, #12]
	bl	FlashReadPages
	ldr	r3, [r5, #-2388]
	ldr	r1, [r7, #4]
	ldr	r3, [r3]
	cmn	r3, #1
	beq	.L2996
	ldr	r0, [sp]
	bl	ftl_cmp_data_ver
	cmp	r0, #0
	bne	.L2996
	mov	r0, r6
	add	r1, sp, #56
	mov	r2, #1
	bl	log2phys
.L2996:
	ldr	r0, [sp, #52]
	cmn	r0, #1
	beq	.L2981
	ubfx	r0, r0, #10, #16
	bl	P2V_block_in_plane
	ldr	r2, [r4, #-1408]
	mov	r3, r0, asl #1
	mov	r1, r0
	ldrh	r3, [r2, r3]
	cmp	r3, #0
	beq	.L3001
.L3080:
	bl	decrement_vpc_count
	b	.L2981
.L3001:
	ldr	r0, .L3090+36
	bl	printk
	b	.L2981
.L2983:
	ldrh	r2, [r9]
	mov	r1, r6
	ldr	r3, .L3090+16
	ldr	r0, .L3090+40
	strh	r2, [r3]	@ movhi
	ldr	r2, [sp]
	bl	printk
	ldr	r3, [fp, #1808]
	cmp	r3, #31
	addls	r2, fp, r3, asl #2
	addls	r3, r3, #1
	strls	r3, [fp, #1808]
	ldrls	r1, [sp, #60]
	strls	r1, [r2, #1812]
	ldrh	r0, [r9]
	bl	decrement_vpc_count
	ldr	r3, .L3090+20
	ldr	r3, [r3, #1760]
	cmn	r3, #1
	ldreq	r3, [sp]
	beq	.L3081
	ldr	r2, [sp]
	cmp	r3, r2
	bls	.L2981
	mov	r3, r2
.L3081:
	str	r3, [fp, #1760]
.L2981:
	ldr	r3, [sp, #28]
	add	r3, r3, #1
	b	.L3082
.L3089:
	ldrb	r3, [r9, #8]	@ zero_extendqisi2
	add	r10, r10, #1
	cmp	r3, #1
	uxth	r10, r10
	bne	.L3005
	ldr	r3, .L3090+4
	ldrb	r3, [r3, #-2744]	@ zero_extendqisi2
	cmp	r3, #0
	beq	.L3005
	ldr	r3, .L3090+44
	ldr	r2, [sp, #4]
	ldrh	r3, [r3]
	cmp	r2, r10
	cmpeq	r3, r10
	beq	.L2982
.L3005:
	ldr	r2, .L3090+48
	ldrh	r3, [r2]
	cmp	r10, r3
	bne	.L2975
	ldrh	r1, [r2, #-68]
	movw	r0, #65535
	mov	r3, #0
	strh	r10, [r9, #2]	@ movhi
	strh	r3, [r9, #4]	@ movhi
.L3006:
	uxth	r2, r3
	cmp	r2, r1
	bcs	.L3072
	ldr	lr, [sp, #20]
	add	r3, r3, #1
	ldrh	ip, [lr, #2]!
	cmp	ip, r0
	str	lr, [sp, #20]
	beq	.L3006
	strb	r2, [r9, #6]
	b	.L3072
.L2982:
	ldrb	r3, [sp, #12]	@ zero_extendqisi2
	mov	r0, r9
	ldr	r1, [sp, #4]
	strb	r3, [r9, #6]
	ldrh	r3, [sp, #4]
	strh	r3, [r9, #2]	@ movhi
.L3083:
	ldr	r2, [sp, #12]
	bl	ftl_sb_update_avl_pages
.L3072:
	mov	r0, #0
	add	sp, sp, #68
	@ sp needed
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L3075:
	mov	r0, #0
	bx	lr
.L3091:
	.align	2
.L3090:
	.word	.LANCHOR2-1664
	.word	.LANCHOR2
	.word	.LANCHOR0
	.word	.LC154
	.word	.LANCHOR4+1802
	.word	.LANCHOR4
	.word	.LANCHOR2-1736
	.word	.LANCHOR0+1104
	.word	.LANCHOR2+884
	.word	.LC155
	.word	.LC156
	.word	.LANCHOR2-1666
	.word	.LANCHOR2-1668
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
	ldr	r5, .L3105
	ldr	r6, .L3105+4
	add	r3, r0, r3, asl #1
	ldr	r7, .L3105+8
	ldrh	r3, [r3, #16]
.L3096:
	movw	r2, #65535
	cmp	r3, r2
	bne	.L3104
.L3098:
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
	b	.L3096
.L3104:
	ldrb	r1, [r4, #8]	@ zero_extendqisi2
	cmp	r1, #1
	bne	.L3099
	ldrb	r3, [r5, #-2744]	@ zero_extendqisi2
	cmp	r3, #0
	bne	.L3099
	ldrh	r3, [r4, #2]
	mov	r3, r3, asl #1
	ldrh	r3, [r6, r3]
	cmp	r3, r2
	bne	.L3099
	ldrh	r3, [r4, #4]
	ldrh	r0, [r4]
	sub	r3, r3, #1
	strh	r3, [r4, #4]	@ movhi
	bl	decrement_vpc_count
	ldrh	r2, [r4, #4]
	cmp	r2, #0
	bne	.L3098
	ldrh	r3, [r4, #2]
	strb	r2, [r4, #6]
	add	r3, r3, #1
	strh	r3, [r4, #2]	@ movhi
	ldmfd	sp!, {r3, r4, r5, r6, r7, pc}
.L3099:
	ldr	r2, .L3105
	ldrb	r3, [r2, #-2744]	@ zero_extendqisi2
	cmp	r3, #0
	ldmeqfd	sp!, {r3, r4, r5, r6, r7, pc}
	cmp	r1, #1
	ldmnefd	sp!, {r3, r4, r5, r6, r7, pc}
	sub	r1, r2, #1664
	ldrh	r0, [r4, #2]
	ldrh	r3, [r1, #-2]
	cmp	r0, r3
	ldmccfd	sp!, {r3, r4, r5, r6, r7, pc}
	ldrh	r3, [r4]
	ldr	r0, [r2, #-1408]
	ldrh	ip, [r4, #4]
	mov	r3, r3, asl #1
	ldrh	r2, [r0, r3]
	rsb	r2, ip, r2
	strh	r2, [r0, r3]	@ movhi
	ldrh	r2, [r1, #-4]
	mov	r3, #0
	strh	r3, [r4, #4]	@ movhi
	strb	r3, [r4, #6]
	strh	r2, [r4, #2]	@ movhi
	ldmfd	sp!, {r3, r4, r5, r6, r7, pc}
.L3106:
	.align	2
.L3105:
	.word	.LANCHOR2
	.word	.LANCHOR0+1104
	.word	.LANCHOR2-1736
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
	ldr	r7, .L3125
	ldr	r8, .L3125+4
	add	r3, r0, r3, asl #1
	ldr	r9, .L3125+8
	ldrh	r2, [r3, #16]
.L3108:
	movw	r1, #65535
	cmp	r2, r1
	ldr	r6, .L3125
	bne	.L3124
.L3109:
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
	b	.L3108
.L3124:
	ldrb	r3, [r4, #8]	@ zero_extendqisi2
	ldrh	r5, [r4, #2]
	cmp	r3, #1
	ldrh	r3, [r4, #4]
	bne	.L3111
	ldrb	r0, [r8, #-2744]	@ zero_extendqisi2
	cmp	r0, #0
	bne	.L3111
	mov	r0, r5, asl #1
	ldrh	r0, [r9, r0]
	cmp	r0, r1
	bne	.L3111
	sub	r3, r3, #1
	ldrh	r0, [r4]
	strh	r3, [r4, #4]	@ movhi
	bl	decrement_vpc_count
	b	.L3109
.L3111:
	ldr	r7, .L3125+4
	orr	r5, r5, r2, asl #10
	sub	r3, r3, #1
	strh	r3, [r4, #4]	@ movhi
.L3112:
	ldrb	r3, [r4, #6]	@ zero_extendqisi2
	movw	r2, #65535
	ldrh	r0, [r6]
.L3114:
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
	beq	.L3114
	strb	r3, [r4, #6]
	ldrb	r3, [r4, #8]	@ zero_extendqisi2
	cmp	r3, #1
	bne	.L3119
	ldrb	r3, [r7, #-2744]	@ zero_extendqisi2
	cmp	r3, #0
	bne	.L3116
	ldrh	r3, [r4, #2]
	ldr	r2, .L3125+8
	mov	r3, r3, asl #1
	ldrh	r2, [r2, r3]
	movw	r3, #65535
	cmp	r2, r3
	bne	.L3116
	ldrh	r3, [r4, #4]
	cmp	r3, #0
	beq	.L3116
	sub	r3, r3, #1
	ldrh	r0, [r4]
	strh	r3, [r4, #4]	@ movhi
	bl	decrement_vpc_count
	b	.L3112
.L3116:
	ldr	r2, .L3125+4
	ldrb	r3, [r2, #-2744]	@ zero_extendqisi2
	cmp	r3, #0
	beq	.L3119
	sub	r1, r2, #1664
	ldrh	r0, [r4, #2]
	ldrh	r3, [r1, #-2]
	cmp	r0, r3
	bcc	.L3119
	ldrh	r3, [r4]
	ldr	r0, [r2, #-1408]
	ldrh	ip, [r4, #4]
	mov	r3, r3, asl #1
	ldrh	r2, [r0, r3]
	rsb	r2, ip, r2
	strh	r2, [r0, r3]	@ movhi
	ldrh	r2, [r1, #-4]
	mov	r3, #0
	strh	r3, [r4, #4]	@ movhi
	strb	r3, [r4, #6]
	strh	r2, [r4, #2]	@ movhi
.L3119:
	mov	r0, r5
	ldmfd	sp!, {r3, r4, r5, r6, r7, r8, r9, pc}
.L3126:
	.align	2
.L3125:
	.word	.LANCHOR2-1736
	.word	.LANCHOR2
	.word	.LANCHOR0+1104
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
	ldr	r4, .L3146
	ldr	r3, [r4, #-1280]
	cmp	r3, #0
	bne	.L3127
	add	r7, r4, #884
	ldrh	r5, [r7, #4]
	cmp	r5, #0
	beq	.L3129
	ldrb	r3, [r4, #892]	@ zero_extendqisi2
	cmp	r3, #0
	bne	.L3129
	sub	r3, r4, #1664
	ldrb	r2, [r4, #891]	@ zero_extendqisi2
	ldrh	r3, [r3, #-4]
	mul	r3, r3, r2
	cmp	r5, r3
	beq	.L3129
	ldrb	r8, [r4, #894]	@ zero_extendqisi2
	sub	r3, r4, #1728
	ldr	r6, [r4, #-1284]
	cmp	r8, #0
	ldrh	r9, [r3, #-8]
	bne	.L3127
	sub	r6, r6, #1
	mov	r1, sp
	mov	r2, r8
	mov	r0, r6
	bl	log2phys
	ldr	r5, [r4, #-1444]
	ldr	r0, [r4, #-1476]
	ldr	r3, [sp]
	str	r6, [sp, #20]
	cmn	r3, #1
	str	r0, [sp, #12]
	str	r3, [sp, #8]
	str	r5, [sp, #16]
	str	r8, [r5, #4]
	beq	.L3131
	mov	r1, #1
	mov	r2, r8
	add	r0, sp, #4
	bl	FlashReadPages
	b	.L3132
.L3131:
	sub	r3, r4, #1648
	mov	r1, #255
	ldrh	r2, [r3, #-10]
	bl	ftl_memset
.L3132:
	ldr	r3, .L3146+4
	mov	r8, #0
	ldr	fp, .L3146
	mov	r9, r9, asl #2
	mov	r10, r8
	strh	r3, [r5]	@ movhi
.L3133:
	cmp	r8, r9
	beq	.L3134
	ldrh	r3, [r7, #4]
	ldr	r0, .L3146+8
	cmp	r3, #0
	beq	.L3134
	ldr	r3, [sp, #8]
	add	r8, r8, #1
	str	r6, [r5, #8]
	str	r3, [r5, #12]
	ldrh	r3, [r0]
	strh	r3, [r5, #2]	@ movhi
	bl	get_new_active_ppa
	ldr	r3, [fp, #-1612]
	mov	r2, #0
	mov	r1, #1
	str	r0, [sp, #8]
	add	r0, sp, #4
	str	r3, [r5, #4]
	add	r3, r3, #1
	cmn	r3, #1
	moveq	r3, r10
	str	r3, [fp, #-1612]
	mov	r3, r2
	bl	FlashProgPages
	ldrh	r0, [r7]
	bl	decrement_vpc_count
	b	.L3133
.L3134:
	mov	r3, #1
	b	.L3145
.L3129:
	mov	r3, #0
.L3145:
	strb	r3, [r4, #894]
.L3127:
	add	sp, sp, #44
	@ sp needed
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L3147:
	.align	2
.L3146:
	.word	.LANCHOR2
	.word	-3947
	.word	.LANCHOR2+884
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
	ldr	r6, .L3154
	mov	r7, #12
	ldr	r5, .L3154+4
.L3149:
	ldrh	r3, [r5]
	uxth	r0, r4
	cmp	r3, r0
	bls	.L3153
	ldr	r3, [r6, #-1364]
	mla	r3, r7, r0, r3
	ldr	r3, [r3, #4]
	cmp	r3, #0
	bge	.L3150
	bl	flush_l2p_region
.L3150:
	add	r4, r4, #1
	b	.L3149
.L3153:
	mov	r0, #0
	ldmfd	sp!, {r3, r4, r5, r6, r7, pc}
.L3155:
	.align	2
.L3154:
	.word	.LANCHOR2
	.word	.LANCHOR2-1630
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
	ldr	r5, .L3173
	ldr	r3, [r5, #-1280]
	cmp	r3, #0
	bne	.L3156
	ldrb	r6, [r5, #-2744]	@ zero_extendqisi2
	cmp	r6, #0
	beq	.L3172
	ldrb	r6, [r0, #8]	@ zero_extendqisi2
	cmp	r6, #1
	ldreqh	r10, [r0, #4]
	beq	.L3158
	mov	r6, r3
.L3172:
	mov	r10, #12
.L3158:
	ldr	r7, [r5, #-1444]
	mvn	r3, #0
	str	r3, [sp, #20]
	mov	r8, #0
	ldr	r3, [r5, #-1476]
	movw	r2, #61589
	str	r7, [sp, #16]
	mov	r4, r0
	ldr	r9, .L3173
	str	r3, [sp, #12]
	mvn	r3, #2
	str	r3, [r7, #8]
	mvn	r3, #1
	str	r3, [r7, #12]
	ldrh	r3, [r0]
	strh	r8, [r7]	@ movhi
	strh	r3, [r7, #2]	@ movhi
	ldr	r3, [r5, #-1476]
	str	r2, [r3]
	ldr	r2, .L3173+4
	ldr	r3, [r5, #-1476]
	str	r2, [r3, #4]
.L3159:
	subs	r10, r10, #1
	bcc	.L3162
	ldrh	r3, [r4, #4]
	cmp	r3, #0
	bne	.L3160
.L3162:
	ldrh	r3, [r4]
	ldr	r1, [r5, #-1408]
	ldrh	r0, [r4, #4]
	mov	r3, r3, asl #1
	ldrh	r2, [r1, r3]
	rsb	r2, r0, r2
	strh	r2, [r1, r3]	@ movhi
	ldr	r3, .L3173+8
	ldrh	r3, [r3, #-4]
	strh	r3, [r4, #2]	@ movhi
	mov	r3, #0
	strb	r3, [r4, #6]
	strh	r3, [r4, #4]	@ movhi
	b	.L3156
.L3160:
	mov	r0, r4
	bl	get_new_active_ppa
	cmn	r0, #1
	str	r0, [sp, #8]
	beq	.L3162
	ldr	r3, [r5, #-1612]
	mov	r1, #1
	mov	r2, r6
	add	r0, sp, #4
	str	r3, [r7, #4]
	add	r3, r3, #1
	cmn	r3, #1
	moveq	r3, r8
	str	r3, [r9, #-1612]
	mov	r3, #0
	bl	FlashProgPages
	ldrh	r0, [r4]
	bl	decrement_vpc_count
	b	.L3159
.L3156:
	add	sp, sp, #40
	@ sp needed
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, pc}
.L3174:
	.align	2
.L3173:
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
	ldr	r4, .L3192
	ldr	r1, .L3192+4
	ldr	r0, .L3192+8
	bl	printk
	sub	r3, r4, #1712
	ldr	r0, [r4, #-1412]
	mov	r1, #0
	ldrh	r2, [r3, #-14]
	mov	r6, r4
	mov	r2, r2, asl #1
	bl	ftl_memset
.L3176:
	ldr	r3, [r4, #-1284]
	cmp	r5, r3
	bcs	.L3190
	mov	r0, r5
	add	r1, sp, #4
	mov	r2, #0
	bl	log2phys
	ldr	r0, [sp, #4]
	cmn	r0, #1
	beq	.L3177
	ubfx	r0, r0, #10, #16
	bl	P2V_block_in_plane
	ldr	r2, [r6, #-1412]
	mov	r0, r0, asl #1
	ldrh	r3, [r2, r0]
	add	r3, r3, #1
	strh	r3, [r2, r0]	@ movhi
.L3177:
	add	r5, r5, #1
	b	.L3176
.L3190:
	ldr	r5, .L3192
	mov	r7, #0
	add	r8, r5, #884
	add	r9, r5, #980
.L3179:
	ldr	r3, .L3192+12
	uxth	r4, r7
	ldrh	r3, [r3]
	cmp	r3, r4
	bls	.L3191
	ldr	r3, [r5, #-1408]
	mov	r6, r4, asl #1
	movw	r1, #65535
	ldrh	r2, [r3, r6]
	ldr	r3, [r5, #-1412]
	ldrh	r3, [r3, r6]
	cmp	r2, r1
	cmpne	r2, r3
	beq	.L3180
	ldrh	r1, [r8]
	cmp	r1, r4
	beq	.L3180
	ldrh	r1, [r9]
	cmp	r1, r4
	beq	.L3180
	ldr	r1, .L3192+16
	ldrh	r1, [r1]
	cmp	r1, r4
	beq	.L3180
	ldr	r0, .L3192+20
	mov	r1, r4
	bl	printk
	ldr	r3, [r5, #-1408]
	ldrh	r2, [r3, r6]
	cmp	r2, #0
	ldr	r2, [r5, #-1412]
	ldrh	r2, [r2, r6]
	strh	r2, [r3, r6]	@ movhi
	beq	.L3180
	mov	r0, r4
	bl	update_vpc_list
.L3180:
	add	r7, r7, #1
	b	.L3179
.L3191:
	bl	l2p_flush
	bl	FtlVpcTblFlush
	add	sp, sp, #12
	@ sp needed
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, pc}
.L3193:
	.align	2
.L3192:
	.word	.LANCHOR2
	.word	.LANCHOR3+216
	.word	.LC50
	.word	.LANCHOR2-1728
	.word	.LANCHOR2+932
	.word	.LC157
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
	ldr	r5, .L3221
	ldrh	r7, [r0]
	ldr	r3, [r5, #-1280]
	cmp	r3, #0
	bne	.L3195
	movw	r3, #65535
	cmp	r7, r3
	mov	r4, r0
	beq	.L3196
	ldr	r2, [r5, #-1408]
	mov	r3, r7, asl #1
	mov	r0, r7
	ldrh	r3, [r2, r3]
	cmp	r3, #0
	beq	.L3197
	bl	INSERT_DATA_LIST
	b	.L3196
.L3197:
	bl	INSERT_FREE_LIST
.L3196:
	mov	r3, #0
	strb	r3, [r4, #8]
	ldr	r3, .L3221
	add	r2, r3, #932
	cmp	r4, r2
	beq	.L3198
	sub	r2, r3, #1712
	ldrh	r2, [r2, #-4]
	cmp	r2, #1
	beq	.L3198
	ldrb	r1, [r3, #-2744]	@ zero_extendqisi2
	cmp	r1, #0
	beq	.L3199
.L3198:
	mov	r3, #1
	strb	r3, [r4, #8]
	b	.L3200
.L3199:
	add	r1, r3, #884
	cmp	r4, r1
	bne	.L3200
	cmp	r2, #3
	beq	.L3202
	ldr	r3, [r3, #-1620]
	cmp	r3, #1
	bne	.L3203
.L3202:
	mov	r3, #1
	strb	r3, [r5, #892]
.L3203:
	ldr	r2, [r5, #-1872]
	ldr	r3, .L3221
	cmp	r2, #0
	beq	.L3200
	ldr	r2, [r3, #-1568]
	cmp	r2, #39
	movls	r2, #1
	strlsb	r2, [r3, #892]
.L3200:
	ldr	r3, .L3221+4
	movw	r2, #1748
	ldrh	r0, [r3, r2]
	movw	r2, #65535
	mov	r6, r3
	cmp	r0, r2
	beq	.L3205
	cmp	r7, r0
	bne	.L3206
	ldr	r2, [r5, #-1408]
	mov	r3, r0, asl #1
	ldrh	r3, [r2, r3]
	cmp	r3, #0
	beq	.L3207
.L3206:
	bl	update_vpc_list
.L3207:
	movw	r3, #1748
	mvn	r2, #0
	strh	r2, [r6, r3]	@ movhi
.L3205:
	mov	r0, r4
	bl	allocate_data_superblock
	bl	l2p_flush
	mov	r0, #0
	bl	FtlEctTblFlush
	bl	FtlVpcTblFlush
.L3195:
	mov	r0, #0
	ldmfd	sp!, {r3, r4, r5, r6, r7, pc}
.L3222:
	.align	2
.L3221:
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
	ldr	r7, .L3240
	ldr	r5, .L3240+4
	ldr	r10, [r7, #1292]
	mov	r6, r7
	cmp	r10, #0
	beq	.L3224
	ldr	r2, [r7, #1296]
	ldr	r3, [r5, #-1284]
	cmp	r2, r3
	bcs	.L3225
	mov	r4, #2048
.L3230:
	ldr	r0, [r6, #1296]
	ldr	r3, [r5, #-1284]
	cmp	r0, r3
	bcs	.L3229
	mov	r2, #0
	mov	r1, sp
	bl	log2phys
	ldr	r3, [r7, #1296]
	add	r3, r3, #1
	str	r3, [r7, #1296]
	ldr	r2, [sp]
	cmn	r2, #1
	beq	.L3228
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
	bne	.L3229
	ldr	r0, [sp]
	ubfx	r0, r0, #10, #16
	bl	P2V_block_in_plane
	bl	FtlGcRefreshBlock
.L3229:
	mvn	r0, #0
	b	.L3232
.L3228:
	subs	r4, r4, #1
	bne	.L3230
	b	.L3229
.L3225:
	ldr	r3, [r5, #-1588]
	mov	r0, #0
	str	r0, [r7, #1292]
	str	r0, [r7, #1296]
	str	r3, [r7, #1288]
	b	.L3232
.L3224:
	ldr	r1, [r5, #-1572]
	movw	r4, #10000
	ldr	r9, [r5, #-1588]
	add	r5, r5, #816
	cmp	r1, r4
	ldr	r8, [r7, #1288]
	add	r2, r9, #1048576
	ldr	r3, [r5, #-2100]
	movhi	r4, #31
	movls	r4, #63
	cmp	r8, r2
	bhi	.L3234
	mov	r1, r1, lsr #10
	mov	r0, #1000
	mul	r0, r0, r3
	add	r1, r1, #1
	bl	__aeabi_uidiv
	add	r0, r0, r8
	cmp	r0, r9
	bcc	.L3234
	ldrh	r3, [r5, #28]
	ands	r0, r4, r3
	movne	r0, r10
	bne	.L3232
	ldr	r2, [r7, #1312]
	cmp	r2, r3
	beq	.L3232
.L3234:
	ldrh	r3, [r5, #28]
	mov	r0, #0
	str	r9, [r6, #1288]
	str	r0, [r6, #1296]
	str	r3, [r6, #1312]
	mov	r3, #1
	str	r3, [r6, #1292]
.L3232:
	add	sp, sp, #40
	@ sp needed
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, pc}
.L3241:
	.align	2
.L3240:
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
	stmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.save {r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.pad #44
	sub	sp, sp, #44
	ldr	r5, .L3406
	ldr	r8, [r5, #-1280]
	cmp	r8, #0
	movne	r0, #0
	bne	.L3395
	ldr	r2, .L3406+4
	ldr	r6, [r2, #3444]
	cmp	r6, #1
	bne	.L3338
	ldr	r3, [r5, #-1564]
	cmp	r3, #0
	bne	.L3338
	mov	r4, r0
	add	r0, r5, #872
	ldrh	r0, [r0]
	cmp	r0, #47
	movls	r0, r3
	bls	.L3395
	movw	r3, #3448
	mov	r7, r5
	ldrh	r2, [r2, r3]
	movw	r3, #65535
	mov	r9, r1
	str	r4, [sp, #20]
	cmp	r2, r3
	bne	.L3244
.L3247:
	ldr	r8, .L3406+8
	movw	r5, #65535
	ldrh	r4, [r8, #-2]
	cmp	r4, r5
	bne	.L3245
	b	.L3246
.L3244:
	add	r5, r5, #980
	ldrh	r2, [r5]
	cmp	r2, r3
	beq	.L3247
	mov	r0, r6
	bl	FtlGcFreeTempBlock
	cmp	r0, #0
	beq	.L3247
	mov	r0, r6
	b	.L3395
.L3245:
	ldrh	r3, [r8, #-4]
	cmp	r3, r5
	bne	.L3246
	ldrh	r0, [r8]
	cmp	r0, r3
	beq	.L3246
	ldrh	r1, [r8, #2]
	cmp	r1, r3
	strneh	r4, [r8, #-4]	@ movhi
	strneh	r0, [r8, #-2]	@ movhi
	mvnne	r3, #0
	strneh	r1, [r8]	@ movhi
	strneh	r3, [r8, #2]	@ movhi
.L3246:
	ldr	r1, [sp, #20]
	ldr	r3, [r7, #-1548]
	cmp	r1, #1
	ldr	r2, .L3406
	add	r3, r3, #1
	ldr	r6, .L3406+12
	add	r3, r3, r1, asl #7
	str	r3, [r7, #-1548]
	bne	.L3258
	ldr	r1, [r2, #-1872]
	cmp	r1, #0
	bne	.L3249
	ldrb	r2, [r2, #-2744]	@ zero_extendqisi2
	cmp	r2, #0
	beq	.L3258
.L3249:
	ldr	r2, [r7, #-1568]
	ldr	r4, .L3406
	cmp	r2, #39
	bhi	.L3258
	movw	r2, #1940
	ldrh	r2, [r6, r2]
	add	r3, r3, r2
	str	r3, [r4, #-1548]
	bl	FtlGcReFreshBadBlk
	movw	r3, #1164
	ldrh	r3, [r6, r3]
	movw	r2, #65535
	cmp	r3, r2
	bne	.L3258
	ldrh	r2, [r8, #-4]
	cmp	r2, r3
	bne	.L3258
	ldr	r3, [r4, #-1548]
	add	r4, r4, #880
	cmp	r3, #1024
	bhi	.L3250
	ldrh	r3, [r4]
	cmp	r3, #63
	bhi	.L3258
.L3250:
	ldr	ip, .L3406
	movw	r3, #1940
	ldrh	r0, [r4]
	mov	r1, #0
	sub	r4, ip, #1520
	ldr	r2, .L3406+12
	strh	r1, [r6, r3]	@ movhi
	ldrh	r10, [r4, #-10]
	add	r5, r10, #64
	cmp	r0, r5
	bgt	.L3258
	str	r1, [ip, #-1548]
	ldr	r1, [ip, #-1568]
	cmp	r1, #0
	moveq	r1, #6
	beq	.L3397
	cmp	r1, #5
	bhi	.L3252
	mov	r1, #18
.L3397:
	strh	r1, [r2, r3]	@ movhi
.L3252:
	mov	r0, #32
	bl	List_get_gc_head_node
	movw	ip, #65535
	uxth	r2, r0
	cmp	r2, ip
	beq	.L3257
	ldrh	r0, [r4, #-8]
	ldr	r5, .L3406
	cmp	r0, #0
	sub	r10, r5, #1520
	beq	.L3254
	sub	r1, r5, #1664
	ldr	r3, [r5, #-1408]
	mov	r2, r2, asl #1
	ldrh	r4, [r1, #-2]
	sub	r1, r5, #1728
	ldrh	lr, [r3, r2]
	ldrh	r1, [r1, #-8]
	mul	r1, r1, r4
	add	r1, r1, #1
	cmp	lr, r1
	bgt	.L3257
	add	r1, r0, #1
	str	r2, [sp, #28]
	str	ip, [sp, #24]
	mov	fp, #0
	uxth	r1, r1
	str	r3, [sp, #16]
	strh	r1, [r10, #-8]	@ movhi
	str	r1, [sp, #12]
	str	fp, [r5, #-1560]
	bl	List_get_gc_head_node
	ldr	ip, [sp, #24]
	uxth	r4, r0
	ldr	r1, [sp, #12]
	cmp	r4, ip
	ldr	r3, [sp, #16]
	ldr	r2, [sp, #28]
	beq	.L3257
	mov	ip, r4, asl #1
	ldr	r0, .L3406+16
	str	ip, [sp, #12]
	ldrh	lr, [r3, ip]
	ldrh	r3, [r3, r2]
	mov	r2, r4
	str	r3, [sp]
	mov	r3, lr
	bl	printk
	ldrh	r3, [r10, #-8]
	cmp	r3, #40
	ldr	ip, [sp, #12]
	bls	.L3255
	ldr	r3, [r5, #-1408]
	ldrh	r3, [r3, ip]
	cmp	r3, #32
	strhih	fp, [r10, #-8]	@ movhi
.L3255:
	movw	r3, #1940
	mov	r2, #6
	strh	r2, [r6, r3]	@ movhi
	b	.L3259
.L3254:
	mov	r3, #1
	strh	r3, [r10, #-8]	@ movhi
.L3257:
	bl	GetSwlReplaceBlock
	movw	r3, #65535
	cmp	r0, r3
	mov	r4, r0
	bne	.L3259
	movw	r3, #1940
	mov	r2, #0
	strh	r2, [r6, r3]	@ movhi
.L3258:
	movw	r3, #1164
	movw	r4, #65535
	ldrh	r3, [r6, r3]
	cmp	r3, r4
	bne	.L3259
	ldr	r5, .L3406
	add	r2, r5, #980
	ldrh	r4, [r2]
	cmp	r4, r3
	movne	r4, r3
	beq	.L3402
.L3259:
	movw	r0, #65535
	rsb	r3, r0, r4
	clz	r3, r3
	ldr	r2, [sp, #20]
	mov	r3, r3, lsr #5
	cmp	r2, #0
	movne	r1, #0
	andeq	r1, r3, #1
	cmp	r1, #0
	beq	.L3272
	ldr	r3, .L3406+20
	ldrh	r0, [r3]
	cmp	r0, #24
	movhi	r5, #1
	bhi	.L3273
	sub	r3, r3, #2544
	cmp	r0, #16
	ldrh	r5, [r3, #-4]
	movhi	r5, r5, lsr #5
	bhi	.L3273
	cmp	r0, #12
	movhi	r5, r5, lsr #4
	bhi	.L3273
	cmp	r0, #8
	movhi	r5, r5, lsr #2
.L3273:
	ldr	r1, .L3406
	sub	r2, r1, #1520
	ldrh	r3, [r2, #-12]
	cmp	r3, r0
	bcs	.L3277
	add	r3, r1, #980
	movw	r0, #65535
	ldrh	r3, [r3]
	cmp	r3, r0
	bne	.L3278
	ldrh	r0, [r8, #-4]
	cmp	r0, r3
	bne	.L3278
	movw	r3, #1940
	ldrh	r0, [r6, r3]
	cmp	r0, #0
	bne	.L3279
	ldr	r3, [r1, #-1284]
	ldr	r1, [r1, #1124]
	add	r3, r3, r3, asl #1
	cmp	r1, r3, lsr #2
	movcs	r3, #18
	bcs	.L3280
.L3279:
	movw	r3, #1160
	ldrh	r3, [r6, r3]
	add	r3, r3, r3, asl #1
	ubfx	r3, r3, #2, #16
.L3280:
	strh	r3, [r2, #-12]	@ movhi
	mov	r3, #0
	str	r3, [r7, #-1560]
	b	.L3395
.L3278:
	movw	r3, #1160
	ldrh	r3, [r6, r3]
	add	r3, r3, r3, asl #1
	mov	r3, r3, asr #2
	strh	r3, [r2, #-12]	@ movhi
.L3277:
	cmp	r9, #2
	ldr	r3, [r7, #-1872]
	movw	r4, #65535
	movhi	r9, #0
	movls	r9, #1
	cmp	r3, #0
	moveq	r9, #0
	cmp	r9, #0
	addne	r5, r5, #1
	uxthne	r5, r5
	b	.L3282
.L3272:
	ldr	r5, .L3406
	add	r2, r5, #980
	ldrh	r2, [r2]
	cmp	r2, r0
	bne	.L3283
	ldrh	r0, [r8, #-4]
	cmp	r0, r2
	movne	r3, #0
	andeq	r3, r3, #1
	cmp	r3, #0
	beq	.L3283
	movw	r3, #1164
	ldrh	r3, [r6, r3]
	cmp	r3, r2
	beq	.L3284
.L3288:
	movw	r4, #65535
	b	.L3283
.L3284:
	add	r4, r5, #880
	sub	r10, r5, #1520
	str	r1, [r5, #-1560]
	ldrh	r2, [r4]
	ldrh	r3, [r10, #-12]
	cmp	r2, r3
	bls	.L3285
	ldr	r2, .L3406+12
	movw	r3, #1940
	ldrh	r3, [r2, r3]
	cmp	r3, #0
	bne	.L3286
	ldr	r3, [r5, #-1284]
	ldr	r2, [r5, #1124]
	add	r3, r3, r3, asl #1
	cmp	r2, r3, lsr #2
	movcs	r3, #18
	bcs	.L3287
.L3286:
	movw	r3, #1160
	ldrh	r3, [r6, r3]
	add	r3, r3, r3, asl #1
	ubfx	r3, r3, #2, #16
.L3287:
	strh	r3, [r10, #-12]	@ movhi
	bl	FtlReadRefresh
	mov	r0, #0
	bl	List_get_gc_head_node
	ldr	r3, [r7, #-1408]
	uxth	r0, r0
	mov	r0, r0, asl #1
	ldrh	r3, [r3, r0]
	cmp	r3, #4
	movwhi	r3, #1940
	ldrhih	r0, [r6, r3]
	bhi	.L3395
.L3285:
	movw	r5, #1940
	ldr	r9, .L3406+12
	ldrh	r0, [r6, r5]
	cmp	r0, #0
	bne	.L3288
	movw	r3, #1160
	ldrh	fp, [r9, r3]
	add	r3, fp, fp, asl #1
	mov	r3, r3, asr #2
	strh	r3, [r10, #-12]	@ movhi
	bl	List_get_gc_head_node
	ldr	r3, [r7, #-1408]
	uxth	r0, r0
	mov	r0, r0, asl #1
	ldrh	r1, [r3, r0]
	ldr	r3, .L3406+24
	ldrh	r2, [r3, #-2]
	ldrh	r3, [r3, #-72]
	mul	r3, r3, r2
	add	r3, r3, r3, lsr #31
	cmp	r1, r3, asr #1
	ble	.L3289
	ldrh	r3, [r4]
	sub	r2, fp, #1
	cmp	r3, r2
	blt	.L3289
	bl	FtlReadRefresh
	ldrh	r0, [r9, r5]
	b	.L3395
.L3289:
	cmp	r1, #0
	bne	.L3288
	movw	r0, #65535
	bl	decrement_vpc_count
	ldrh	r0, [r4]
	add	r0, r0, #1
	b	.L3395
.L3283:
	ldr	r3, [r7, #-1872]
	cmp	r3, #0
	moveq	r5, #1
	movne	r5, #2
.L3282:
	movw	r3, #1164
	movw	r1, #65535
	ldrh	r2, [r6, r3]
	cmp	r2, r1
	bne	.L3291
	cmp	r4, r2
	ldrne	r2, .L3406+12
	strneh	r4, [r2, r3]	@ movhi
	bne	.L3293
	ldrh	r3, [r8, #-4]
	ldr	r2, .L3406
	cmp	r3, r4
	sub	r1, r2, #1536
	beq	.L3293
	ldr	r2, [r2, #-1408]
	mov	r3, r3, asl #1
	ldrh	r3, [r2, r3]
	cmp	r3, #0
	mvneq	r3, #0
	streqh	r3, [r1, #-4]	@ movhi
	ldrh	r2, [r8, #-4]
	movw	r3, #1164
	strh	r2, [r6, r3]	@ movhi
	mvn	r3, #0
	strh	r3, [r8, #-4]	@ movhi
.L3293:
	movw	r8, #1164
	mov	r3, #0
	ldrh	r0, [r6, r8]
	strb	r3, [r6, #1172]
	movw	r3, #65535
	cmp	r0, r3
	beq	.L3291
	bl	IsBlkInGcList
	cmp	r0, #0
	ldrne	r3, .L3406+12
	mvnne	r2, #0
	strneh	r2, [r3, r8]	@ movhi
	ldrb	r3, [r7, #-2744]	@ zero_extendqisi2
	cmp	r3, #0
	beq	.L3297
	movw	r3, #1164
	ldrh	r0, [r6, r3]
	bl	ftl_get_blk_mode
	strb	r0, [r6, #1172]
.L3297:
	movw	r9, #1164
	movw	r3, #65535
	ldrh	r2, [r6, r9]
	ldr	r8, .L3406+12
	cmp	r2, r3
	ldr	r10, .L3406+28
	beq	.L3291
	mov	r0, r10
	bl	make_superblock
	movw	r2, #1942
	mov	r3, #0
	strh	r3, [r8, r2]	@ movhi
	strh	r3, [r10, #2]	@ movhi
	strb	r3, [r8, #1170]
	ldrh	r3, [r8, r9]
	ldr	r2, [r7, #-1408]
	mov	r3, r3, asl #1
	ldrh	r2, [r2, r3]
	movw	r3, #1944
	strh	r2, [r8, r3]	@ movhi
.L3291:
	ldr	r2, .L3406+32
	movw	r3, #1164
	ldrh	r3, [r6, r3]
	ldrh	r1, [r2]
	cmp	r1, r3
	beq	.L3298
	ldrh	r1, [r2, #48]
	cmp	r1, r3
	beq	.L3298
	ldrh	r2, [r2, #96]
	cmp	r2, r3
	bne	.L3332
.L3298:
	movw	r3, #1164
	mvn	r2, #0
	strh	r2, [r6, r3]	@ movhi
.L3332:
	ldr	r8, .L3406+28
	movw	r3, #65535
	ldr	r7, .L3406
	ldrh	r2, [r8]
	cmp	r2, r3
	bne	.L3300
	mov	fp, r7
	mov	r3, #0
	str	r3, [r7, #-1560]
.L3301:
	ldr	r10, .L3406+36
	ldr	r9, .L3406+12
	ldrh	r6, [r10]
	mov	r0, r6
	bl	List_get_gc_head_node
	ldr	r2, .L3406+28
	uxth	r3, r0
	strh	r3, [r2]	@ movhi
	movw	r2, #65535
	cmp	r3, r2
	moveq	r3, #0
	moveq	r0, #8
	streqh	r3, [r10]	@ movhi
	beq	.L3395
.L3302:
	mov	r0, r3
	str	r3, [sp, #12]
	bl	IsBlkInGcList
	add	r6, r6, #1
	cmp	r0, #0
	ldr	r3, [sp, #12]
	ldrne	r3, .L3406+36
	strneh	r6, [r3]	@ movhi
	bne	.L3301
	ldr	r2, .L3406+36
	uxth	r6, r6
	ldrh	r1, [r10, #-208]
	mov	r0, r3, asl #1
	ldr	ip, [fp, #-1408]
	strh	r6, [r2]	@ movhi
	ldrh	r2, [r10, #-140]
	ldrh	lr, [ip, r0]
	mul	r2, r1, r2
	add	r1, r2, r2, lsr #31
	cmp	lr, r1, asr #1
	bgt	.L3305
	cmp	lr, #8
	cmphi	r6, #48
	bls	.L3306
	ldr	r1, .L3406+40
	ldrh	r1, [r1]
	cmp	r1, #35
	bhi	.L3306
.L3305:
	ldr	lr, .L3406+36
	mov	r1, #0
	strh	r1, [lr]	@ movhi
.L3306:
	ldrh	r1, [ip, r0]
	movw	r0, #65535
	cmp	r1, r2
	cmpge	r4, r0
	bne	.L3307
	ldr	r2, .L3406+36
	ldrh	r2, [r2]
	cmp	r2, #3
	bhi	.L3307
	movw	r3, #1164
	mvn	r2, #0
	strh	r2, [r9, r3]	@ movhi
	mov	r2, #0
	ldr	r3, .L3406+36
	strh	r2, [r3]	@ movhi
	b	.L3400
.L3307:
	cmp	r1, #0
	bne	.L3308
	movw	r0, #65535
	bl	decrement_vpc_count
	ldr	r3, .L3406+36
	ldr	r2, .L3406+36
	ldrh	r3, [r3]
	add	r3, r3, #1
	strh	r3, [r2]	@ movhi
	b	.L3301
.L3308:
	mov	r2, #0
	strb	r2, [r9, #1172]
	ldrb	r2, [r7, #-2744]	@ zero_extendqisi2
	cmp	r2, #0
	beq	.L3309
	mov	r0, r3
	bl	ftl_get_blk_mode
	ldr	r3, .L3406+12
	strb	r0, [r3, #1172]
.L3309:
	ldr	r0, .L3406+28
	bl	make_superblock
	ldrh	r2, [r8]
	ldr	r1, .L3406+44
	mov	r3, #0
	ldr	r0, [r7, #-1408]
	mov	r2, r2, asl #1
	strh	r3, [r1]	@ movhi
	ldrh	r2, [r0, r2]
	strh	r3, [r8, #2]	@ movhi
	strb	r3, [r9, #1170]
	strh	r2, [r1, #2]	@ movhi
.L3300:
	ldr	r3, [sp, #20]
	cmp	r3, #1
	bne	.L3310
	bl	FtlReadRefresh
.L3310:
	mov	r3, #1
	str	r3, [r7, #-1564]
	ldr	r3, .L3406+48
	ldrh	r2, [r3]
	str	r2, [sp, #12]
	ldrb	r2, [r7, #-2744]	@ zero_extendqisi2
	cmp	r2, #0
	beq	.L3311
	ldr	r2, .L3406+12
	ldrb	r2, [r2, #1172]	@ zero_extendqisi2
	cmp	r2, #1
	ldreqh	r3, [r3, #2]
	streq	r3, [sp, #12]
.L3311:
	ldrh	r3, [r8, #2]
	ldr	r1, [sp, #12]
	add	r2, r3, r5
	ldr	r8, .L3406+12
	cmp	r2, r1
	movgt	r2, r1
	rsbgt	r3, r3, r2
	uxthgt	r5, r3
	mov	r3, #0
	str	r3, [sp, #16]
	b	.L3313
.L3405:
	ldr	r0, [r7, #-1488]
	mov	r1, r6
	ldrb	r2, [r8, #1172]	@ zero_extendqisi2
	mov	r10, #0
	bl	FlashReadPages
	ldr	r7, .L3406
.L3316:
	uxth	r3, r10
	cmp	r3, r6
	bcs	.L3403
	mov	r3, #36
	ldr	r2, [r7, #-1488]
	mul	r9, r3, r10
	add	r1, r2, r9
	ldr	r2, [r2, r9]
	ldr	fp, [r1, #12]
	cmn	r2, #1
	beq	.L3352
	ldrh	r1, [fp]
	movw	r2, #61589
	cmp	r1, r2
	bne	.L3352
	add	r1, sp, #32
	mov	r2, #0
	ldr	r0, [fp, #8]
	str	r3, [sp, #24]
	bl	log2phys
	ldr	r1, [r7, #-1488]
	add	r1, r1, r9
	ldr	r0, [r1, #4]
	ldr	r2, [sp, #32]
	ldr	r3, [sp, #24]
	bic	r2, r2, #-2147483648
	cmp	r2, r0
	bne	.L3352
	ldr	r0, .L3406+44
	ldr	r1, [r1, #16]
	str	r3, [sp, #28]
	ldrh	r2, [r0]
	add	r2, r2, #1
	strh	r2, [r0]	@ movhi
	ldr	r0, [r8, #1740]
	ldr	r2, [r7, #-1500]
	mla	r2, r3, r0, r2
	str	r1, [r2, #16]
	str	r2, [sp, #24]
	bl	Ftl_get_new_temp_ppa
	ldr	r1, [r8, #1740]
	ldr	r2, [sp, #24]
	ldr	r3, [sp, #28]
	str	r0, [r2, #4]
	ldr	r2, [r7, #-1500]
	mla	r3, r3, r1, r2
	ldr	r2, [r7, #-1488]
	add	r2, r2, r9
	ldr	r1, [r2, #8]
	str	r1, [r3, #8]
	mov	r1, #1
	ldr	r2, [r2, #12]
	str	r2, [r3, #12]
	ldr	r3, [sp, #32]
	str	r3, [fp, #12]
	ldr	r3, .L3406+52
	ldrh	r2, [r3]
	str	r3, [sp, #24]
	strh	r2, [fp, #2]	@ movhi
	ldr	r2, [r7, #-1612]
	ldr	r0, [r7, #-1488]
	str	r2, [fp, #4]
	add	r0, r0, r9
	ldr	r2, [r8, #1740]
	ldr	r9, .L3406+12
	add	r2, r2, #1
	str	r2, [r8, #1740]
	bl	FtlGcBufAlloc
	ldrb	r2, [r7, #-2744]	@ zero_extendqisi2
	cmp	r2, #0
	beq	.L3404
.L3318:
	bl	Ftl_gc_temp_data_write_back
	cmp	r0, #0
	beq	.L3352
	ldr	r2, .L3406
	mov	r3, #0
	mvn	r1, #0
	str	r3, [r2, #-1564]
	movw	r2, #1164
	strh	r1, [r9, r2]	@ movhi
	ldr	r2, .L3406+28
	strh	r3, [r2, #2]	@ movhi
.L3400:
	movw	r3, #1940
	ldrh	r0, [r9, r3]
	b	.L3395
.L3403:
	ldr	r3, [sp, #16]
	add	r3, r3, #1
	str	r3, [sp, #16]
.L3313:
	ldrh	r3, [sp, #16]
	ldr	r7, .L3406
	cmp	r3, r5
	ldr	r6, .L3406+28
	bcs	.L3320
	ldr	r3, .L3406+56
	mov	r2, #0
	ldrh	r0, [r6, #2]
	movw	lr, #65535
	ldr	r9, [r7, #-1488]
	mov	ip, #36
	ldrh	r10, [r3]
	ldr	r3, [sp, #16]
	add	r0, r0, r3
	add	r3, r6, #14
	mov	r6, r2
.L3321:
	uxth	r1, r2
	cmp	r1, r10
	bcs	.L3405
	ldrh	r1, [r3, #2]!
	add	r2, r2, #1
	cmp	r1, lr
	orrne	r1, r0, r1, asl #10
	mlane	fp, ip, r6, r9
	addne	r6, r6, #1
	uxthne	r6, r6
	strne	r1, [fp, #4]
	b	.L3321
.L3404:
	ldrb	r2, [r7, #987]	@ zero_extendqisi2
	ldr	r1, [r8, #1740]
	cmp	r1, r2
	beq	.L3318
	ldr	r3, [sp, #24]
	ldrh	r3, [r3, #4]
	cmp	r3, #0
	beq	.L3318
.L3352:
	add	r10, r10, #1
	b	.L3316
.L3320:
	ldrh	r3, [r6, #2]
	ldr	r8, .L3406+12
	add	r5, r5, r3
	ldr	r3, [sp, #12]
	uxth	r5, r5
	mov	r9, r8
	cmp	r5, r3
	strh	r5, [r6, #2]	@ movhi
	bcc	.L3322
	ldr	r3, [r8, #1740]
	cmp	r3, #0
	beq	.L3323
	bl	Ftl_gc_temp_data_write_back
	cmp	r0, #0
	movne	r3, #0
	strne	r3, [r7, #-1564]
	movwne	r3, #1940
	ldrneh	r0, [r8, r3]
	bne	.L3395
.L3323:
	ldr	r3, .L3406+44
	ldrh	r5, [r3]
	cmp	r5, #0
	bne	.L3324
	ldrh	r3, [r6]
	ldr	r2, [r7, #-1408]
	mov	r3, r3, asl #1
	ldrh	r3, [r2, r3]
	cmp	r3, #0
	beq	.L3324
.L3325:
	ldr	r3, [r7, #-1284]
	cmp	r5, r3
	bcs	.L3330
	mov	r0, r5
	add	r1, sp, #36
	mov	r2, #0
	bl	log2phys
	ldr	r0, [sp, #36]
	cmn	r0, #1
	beq	.L3326
	ubfx	r0, r0, #10, #16
	bl	P2V_block_in_plane
	ldrh	r3, [r6]
	cmp	r3, r0
	bne	.L3326
.L3330:
	ldr	r3, [r7, #-1284]
	cmp	r5, r3
	bcc	.L3324
	ldr	r2, .L3406
	mov	r1, #0
	ldrh	r3, [r6]
	ldr	r2, [r2, #-1408]
	mov	r3, r3, asl #1
	strh	r1, [r2, r3]	@ movhi
	ldrh	r0, [r6]
	bl	update_vpc_list
	bl	FtlCacheWriteBack
	bl	l2p_flush
	bl	FtlVpcTblFlush
	b	.L3324
.L3326:
	add	r5, r5, #1
	b	.L3325
.L3324:
	mvn	r3, #0
	strh	r3, [r6]	@ movhi
.L3322:
	ldr	r2, .L3406
	add	r3, r2, #880
	ldrh	r3, [r3]
	cmp	r3, #2
	ldrls	r3, .L3406+48
	ldrlsh	r5, [r3]
	bls	.L3332
.L3331:
	mov	r1, #0
	str	r1, [r2, #-1564]
	movw	r2, #1940
	ldrh	r0, [r9, r2]
	cmp	r0, r1
	addeq	r0, r3, #1
	b	.L3395
.L3338:
	mov	r0, r8
	b	.L3395
.L3402:
	ldrh	fp, [r8, #-4]
	cmp	fp, r4
	bne	.L3259
	add	r10, r5, #880
	ldr	r2, [r5, #-1548]
	ldrh	r3, [r10]
	cmp	r3, #24
	movcc	r3, #5120
	movcs	r3, #1024
	cmp	r2, r3
	movls	r4, fp
	bls	.L3259
	movw	r2, #1940
	mov	r3, #0
	str	r3, [r7, #-1548]
	strh	r3, [r6, r2]	@ movhi
	bl	GetSwlReplaceBlock
	cmp	r0, fp
	mov	r4, r0
	sub	fp, r5, #1520
	bne	.L3261
	ldrh	r2, [r10]
	ldrh	r3, [fp, #-10]
	cmp	r2, r3
	bcs	.L3262
	mov	r0, #64
	bl	List_get_gc_head_node
	uxth	r3, r0
	cmp	r3, r4
	beq	.L3271
	ldr	r2, [r5, #-1624]
	sub	r1, r5, #1712
	cmp	r2, #0
	bne	.L3264
	ldrh	r2, [r1, #-4]
	cmp	r2, #3
	beq	.L3264
	ldr	r2, [r5, #-1620]
	cmp	r2, #0
	bne	.L3264
	ldr	r2, [r5, #-1872]
	cmp	r2, #0
	bne	.L3264
	ldrb	r0, [r5, #-2744]	@ zero_extendqisi2
	cmp	r0, #0
	beq	.L3265
.L3264:
	ldr	r2, [r7, #-1408]
	mov	r3, r3, asl #1
	ldrh	r1, [r1, #-4]
	ldrh	r0, [r2, r3]
	cmp	r1, #3
	ldr	r2, .L3406+24
	ldrh	r3, [r2, #-2]
	ldrh	r2, [r2, #-72]
	mul	r2, r2, r3
	moveq	r3, r3, lsr #1
	movne	r3, #0
	add	r3, r2, r3
	cmp	r0, r3
	bgt	.L3267
	mov	r0, #0
	bl	List_get_gc_head_node
	ldr	r3, [r7, #-1284]
	ldr	r2, [r7, #1124]
	add	r3, r3, r3, asl #1
	cmp	r2, r3, lsr #2
	movls	r3, #160
	uxth	r4, r0
	bls	.L3398
	b	.L3401
.L3265:
	ldr	r2, [r5, #-1408]
	mov	r3, r3, asl #1
	ldrh	r3, [r2, r3]
	cmp	r3, #7
	bhi	.L3270
	bl	List_get_gc_head_node
	uxth	r4, r0
.L3401:
	mov	r3, #128
.L3398:
	strh	r3, [fp, #-10]	@ movhi
	movw	r3, #65535
	cmp	r4, r3
	beq	.L3271
	b	.L3261
.L3267:
	mov	r3, #128
	b	.L3399
.L3270:
	mov	r3, #64
	b	.L3399
.L3262:
	mov	r3, #80
.L3399:
	strh	r3, [fp, #-10]	@ movhi
	b	.L3271
.L3261:
	ldr	r0, [r7, #-1416]
	mov	r1, r4, asl #1
	ldr	r3, [r7, #-1408]
	ldrh	r2, [r10]
	ldrh	r3, [r3, r1]
	ldrh	r1, [r0, r1]
	ldr	r0, .L3406+60
	str	r1, [sp]
	ldrh	r1, [fp, #-12]
	str	r1, [sp, #4]
	mov	r1, r4
	bl	printk
.L3271:
	bl	FtlGcReFreshBadBlk
	b	.L3259
.L3395:
	add	sp, sp, #44
	@ sp needed
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L3407:
	.align	2
.L3406:
	.word	.LANCHOR2
	.word	.LANCHOR1
	.word	.LANCHOR2-1536
	.word	.LANCHOR4
	.word	.LC158
	.word	.LANCHOR2+880
	.word	.LANCHOR2-1664
	.word	.LANCHOR4+1164
	.word	.LANCHOR2+884
	.word	.LANCHOR2-1528
	.word	.LANCHOR4+1764
	.word	.LANCHOR4+1942
	.word	.LANCHOR2-1668
	.word	.LANCHOR2+980
	.word	.LANCHOR2-1736
	.word	.LC159
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
	ldr	r4, .L3452
	ldr	r8, .L3452+4
	ldr	r3, [r4, #-1280]
	ldr	r5, [r8, #1948]
	cmp	r3, #0
	bne	.L3410
	ldr	r1, [r4, #-1516]
	cmp	r1, #0
	beq	.L3410
	ldrb	r6, [r4, #-2744]	@ zero_extendqisi2
	mov	r7, #0
	ldr	r0, [r4, #-1484]
	mov	r10, #36
	cmp	r6, #0
	ldrb	r3, [r5, #9]	@ zero_extendqisi2
	ldr	r9, .L3452
	ldrneb	r6, [r5, #8]	@ zero_extendqisi2
	subne	r6, r6, #1
	clzne	r6, r6
	movne	r6, r6, lsr #5
	mov	r2, r6
	bl	FlashProgPages
.L3413:
	ldr	r3, [r4, #-1516]
	cmp	r7, r3
	bcs	.L3431
	mul	fp, r10, r7
	ldr	r3, [r9, #-1484]
	add	r2, r3, fp
	ldr	r3, [r3, fp]
	cmn	r3, #1
	beq	.L3434
	ldr	r3, [r2, #4]
	cmp	r6, #0
	ldr	r0, [r2, #16]
	add	r1, sp, #4
	mov	r2, #1
	orrne	r3, r3, #-2147483648
	str	r3, [sp, #4]
	bl	log2phys
	ldr	r3, [r4, #-1484]
	add	r3, r3, fp
	ldr	r3, [r3, #12]
	ldr	r0, [r3, #12]
	cmn	r0, #1
	beq	.L3417
	ubfx	r0, r0, #10, #16
	bl	P2V_block_in_plane
	ldr	r2, [r9, #-1408]
	mov	r3, r0, asl #1
	mov	fp, r0
	ldrh	r2, [r2, r3]
	cmp	r2, #0
	bne	.L3418
	ldr	r0, .L3452+8
	mov	r1, fp
	bl	printk
.L3418:
	mov	r0, fp
	bl	decrement_vpc_count
.L3417:
	add	r7, r7, #1
	b	.L3413
.L3450:
	ldr	r6, .L3452+12
	movw	r5, #16386
.L3430:
	ldrh	r3, [r6]
	cmp	r3, #0
	beq	.L3431
	mov	r0, #1
	mov	r1, r0
	bl	ftl_do_gc
	subs	r5, r5, #1
	bne	.L3430
.L3431:
	mov	r3, #0
	str	r3, [r4, #-1516]
	b	.L3410
.L3434:
	ldr	r10, .L3452
	mov	r9, #0
.L3414:
	ldr	r3, [r4, #-1516]
	cmp	r9, r3
	bcs	.L3450
	mov	r7, #36
	ldr	r3, [r10, #-1484]
	mul	r7, r7, r9
	mov	fp, #0
	mvn	r2, #0
	str	r2, [r3, r7]
.L3420:
	ldr	r3, [r4, #-1484]
	add	r2, r3, r7
	ldr	r3, [r3, r7]
	cmn	r3, #1
	bne	.L3451
	ldr	r0, [r2, #4]
	ubfx	r0, r0, #10, #16
	bl	P2V_block_in_plane
	ldrh	r3, [r5]
	cmp	r3, r0
	bne	.L3421
	ldr	r1, [r10, #-1408]
	mov	r3, r3, asl #1
	ldrh	r0, [r5, #4]
	ldrh	r2, [r1, r3]
	rsb	r2, r0, r2
	strh	r2, [r1, r3]	@ movhi
	ldr	r3, .L3452+16
	strb	fp, [r5, #6]
	strh	fp, [r5, #4]	@ movhi
	ldrh	r3, [r3]
	strh	r3, [r5, #2]	@ movhi
.L3421:
	ldrh	r3, [r5, #4]
	cmp	r3, #0
	bne	.L3422
	mov	r0, r5
	bl	allocate_new_data_superblock
.L3422:
	ldr	r3, [r8, #1308]
	add	r3, r3, #1
	str	r3, [r8, #1308]
	ldr	r3, [r4, #-1484]
	add	r3, r3, r7
	ldr	r0, [r3, #4]
	ubfx	r0, r0, #10, #16
	bl	FtlGcMarkBadPhyBlk
	mov	r0, r5
	bl	get_new_active_ppa
	ldr	r3, [r4, #-1484]
	mov	r1, #1
	mov	r2, r6
	add	r3, r3, r7
	str	r0, [sp, #4]
	str	r0, [r3, #4]
	mov	r0, r3
	ldrb	r3, [r5, #9]	@ zero_extendqisi2
	bl	FlashProgPages
	ldr	r3, [r4, #-1484]
	ldr	r3, [r3, r7]
	cmn	r3, #1
	moveq	r3, #1
	streq	r3, [r10, #-1280]
	ldr	r3, [r4, #-1280]
	cmp	r3, #0
	beq	.L3420
	b	.L3410
.L3451:
	ldr	r3, [r2, #4]
	cmp	r6, #0
	ldr	r0, [r2, #16]
	add	r1, sp, #4
	mov	r2, #1
	orrne	r3, r3, #-2147483648
	str	r3, [sp, #4]
	bl	log2phys
	ldr	r3, [r4, #-1484]
	add	r7, r3, r7
	ldr	r3, [r7, #12]
	ldr	r0, [r3, #12]
	cmn	r0, #1
	beq	.L3427
	ubfx	r0, r0, #10, #16
	bl	P2V_block_in_plane
	ldr	r2, [r10, #-1408]
	mov	r3, r0, asl #1
	mov	r7, r0
	ldrh	r2, [r2, r3]
	cmp	r2, #0
	bne	.L3428
	ldr	r0, .L3452+8
	mov	r1, r7
	bl	printk
.L3428:
	mov	r0, r7
	bl	decrement_vpc_count
.L3427:
	add	r9, r9, #1
	b	.L3414
.L3410:
	mov	r0, #0
	add	sp, sp, #12
	@ sp needed
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L3453:
	.align	2
.L3452:
	.word	.LANCHOR2
	.word	.LANCHOR4
	.word	.LC160
	.word	.LANCHOR2-1526
	.word	.LANCHOR2-1668
	.fnend
	.size	FtlCacheWriteBack, .-FtlCacheWriteBack
	.align	2
	.global	FtlSysFlush
	.type	FtlSysFlush, %function
FtlSysFlush:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	ldr	r3, .L3458
	ldr	r3, [r3, #-1280]
	cmp	r3, #0
	bne	.L3457
	ldr	r3, .L3458+4
	stmfd	sp!, {r4, lr}
	.save {r4, lr}
	ldr	r4, [r3, #3444]
	cmp	r4, #1
	bne	.L3455
	bl	FtlCacheWriteBack
	bl	l2p_flush
	mov	r0, r4
	bl	FtlEctTblFlush
	bl	FtlVpcTblFlush
.L3455:
	mov	r0, #0
	ldmfd	sp!, {r4, pc}
.L3457:
	mov	r0, #0
	bx	lr
.L3459:
	.align	2
.L3458:
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
	ldr	r3, .L3463
	ldr	r3, [r3, #3444]
	cmp	r3, #1
	bne	.L3461
	bl	FtlSysFlush
.L3461:
	mov	r0, #0
	ldmfd	sp!, {r3, pc}
.L3464:
	.align	2
.L3463:
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
	mov	r6, r0
	ldr	r5, .L3486
	mov	r4, r1
	ldr	r3, [r5, #-2740]
	cmp	r1, r3
	cmpls	r0, r3
	bcs	.L3477
	add	r2, r0, r1
	cmp	r2, r3
	bhi	.L3477
	cmp	r1, #31
	bhi	.L3470
.L3471:
	mov	r0, #0
	b	.L3469
.L3470:
	ldr	r3, [r5, #-1280]
	cmp	r3, #0
	bne	.L3471
	sub	r5, r5, #1648
	bl	FtlCacheWriteBack
	mov	r0, r6
	ldrh	r5, [r5, #-14]
	mov	r1, r5
	bl	__aeabi_uidiv
	smulbb	r3, r0, r5
	mov	r7, r0
	rsb	r6, r3, r6
	uxth	r6, r6
	cmp	r6, #0
	beq	.L3472
	rsb	r5, r6, r5
	add	r7, r0, #1
	cmp	r5, r4
	movcs	r5, r4
	uxth	r5, r5
	rsb	r4, r5, r4
.L3472:
	ldr	r5, .L3486+4
	mvn	r3, #0
	ldr	r8, .L3486
	str	r3, [sp, #4]
	mov	r6, r5
.L3473:
	ldrh	r3, [r5]
	cmp	r4, r3
	bcc	.L3485
	mov	r0, r7
	mov	r1, sp
	mov	r2, #0
	bl	log2phys
	ldr	r3, [sp]
	cmn	r3, #1
	beq	.L3474
	ldr	r2, .L3486+8
	add	r1, sp, #4
	mov	r0, r7
	ldr	r3, [r2, #1952]
	add	r3, r3, #1
	str	r3, [r2, #1952]
	ldr	r3, [r8, #-1600]
	mov	r2, #1
	add	r3, r3, #1
	str	r3, [r8, #-1600]
	bl	log2phys
	ldr	r0, [sp]
	ubfx	r0, r0, #10, #16
	bl	P2V_block_in_plane
	bl	decrement_vpc_count
.L3474:
	ldrh	r3, [r6]
	add	r7, r7, #1
	rsb	r4, r3, r4
	b	.L3473
.L3485:
	ldr	r3, .L3486+8
	ldr	r2, [r3, #1952]
	cmp	r2, #32
	bls	.L3471
	mov	r4, #0
	str	r4, [r3, #1952]
	bl	l2p_flush
	bl	FtlVpcTblFlush
	b	.L3471
.L3477:
	mvn	r0, #0
.L3469:
	add	sp, sp, #8
	@ sp needed
	ldmfd	sp!, {r4, r5, r6, r7, r8, pc}
.L3487:
	.align	2
.L3486:
	.word	.LANCHOR2
	.word	.LANCHOR2-1662
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
	ldr	r6, .L3527
	sub	r9, r6, #1664
	ldr	r8, [r6, #-1280]
	ldrh	r1, [r9, #-4]
	cmp	r8, #0
	bne	.L3525
	add	r4, r6, #980
	mov	r5, r6
	movw	ip, #65535
	ldrh	r6, [r4]
	cmp	r6, ip
	bne	.L3491
.L3500:
	ldrh	r2, [r4]
	movw	r3, #65535
	ldr	r6, .L3527+4
	mov	r7, #0
	ldr	r8, .L3527
	cmp	r2, r3
	str	r7, [r6, #1756]
	add	r10, r8, #980
	beq	.L3525
	bl	FtlCacheWriteBack
	ldrh	r2, [r9, #-4]
	ldrb	r0, [r8, #987]	@ zero_extendqisi2
	ldrh	r3, [r10]
	mov	r10, #12
	ldr	r1, [r8, #-1408]
	smulbb	r2, r0, r2
	mov	r3, r3, asl #1
	ldr	r9, .L3527+8
	strh	r2, [r1, r3]	@ movhi
	movw	r3, #1766
	ldr	r2, [r8, #-1608]
	ldrh	r3, [r6, r3]
	add	r3, r3, r2
	str	r3, [r8, #-1608]
	b	.L3501
.L3491:
	cmp	r0, #0
	beq	.L3494
	ldr	r2, .L3527+12
	movw	r3, #3448
	ldrh	r0, [r2, r3]
	cmp	r0, ip
	beq	.L3495
.L3496:
	mov	r1, #2
	b	.L3494
.L3495:
	strh	r8, [r2, r3]	@ movhi
	add	r3, r5, #880
	ldrh	r3, [r3]
	cmp	r3, #17
	bhi	.L3496
.L3494:
	ldr	r7, .L3527
	add	r0, r7, #980
	bl	FtlGcScanTempBlk
	cmn	r0, #1
	str	r0, [sp, #12]
	beq	.L3497
	ldr	r2, [r7, #-1416]
	mov	r6, r6, asl #1
	ldrh	r3, [r2, r6]
	cmp	r3, #4
	bls	.L3498
	sub	r3, r3, #5
	mov	r0, #1
	strh	r3, [r2, r6]	@ movhi
	bl	FtlEctTblFlush
.L3498:
	ldr	r4, .L3527+4
	ldr	r3, [r4, #1756]
	cmp	r3, #0
	bne	.L3499
	ldr	r0, [sp, #12]
	ldr	r3, [r4, #1308]
	ubfx	r0, r0, #10, #16
	add	r3, r3, #1
	str	r3, [r4, #1308]
	bl	FtlBbmMapBadBlock
	bl	FtlBbmTblFlush
.L3499:
	mov	r3, #0
	str	r3, [r4, #1756]
	b	.L3511
.L3497:
	ldr	r2, .L3527+12
	movw	r3, #3448
	ldrh	r2, [r2, r3]
	movw	r3, #65535
	cmp	r2, r3
	bne	.L3511
	b	.L3500
.L3504:
	ldr	r3, [fp, #4]
	cmp	r0, r3
	bne	.L3523
.L3503:
	add	r7, r7, #1
.L3501:
	ldrh	r3, [r9]
	uxth	r8, r7
	cmp	r3, r8
	bls	.L3526
	mul	r8, r10, r8
	ldr	r3, [r5, #-1508]
	ldr	r2, [r5, #-1284]
	add	fp, r3, r8
	ldr	r0, [fp, #8]
	cmp	r0, r2
	bcs	.L3523
	add	r1, sp, #12
	mov	r2, #0
	str	r3, [sp, #4]
	bl	log2phys
	ldr	r3, [sp, #4]
	ldr	r0, [sp, #12]
	ldr	r3, [r3, r8]
	cmp	r0, r3
	bne	.L3504
	ubfx	r0, r0, #10, #16
	bl	P2V_block_in_plane
	add	r1, fp, #4
	mov	r2, #1
	mov	r8, r0
	ldr	r0, [fp, #8]
	bl	log2phys
	mov	r0, r8
	b	.L3524
.L3523:
	ldrh	r0, [r4]
.L3524:
	bl	decrement_vpc_count
	b	.L3503
.L3526:
	movw	r0, #65535
	bl	decrement_vpc_count
	ldrb	r3, [r5, #-2744]	@ zero_extendqisi2
	cmp	r3, #0
	beq	.L3506
	ldr	r0, .L3527+16
	ldrh	r1, [r4]
	bl	printk
.L3506:
	ldrh	r0, [r4]
	ldr	r2, [r5, #-1408]
	mov	r3, r0, asl #1
	ldrh	r3, [r2, r3]
	cmp	r3, #0
	beq	.L3507
	bl	INSERT_DATA_LIST
	b	.L3508
.L3507:
	bl	INSERT_FREE_LIST
.L3508:
	movw	r3, #1766
	mvn	r8, #0
	strh	r8, [r4]	@ movhi
	mov	r4, #0
	strh	r4, [r6, r3]	@ movhi
	movw	r3, #1764
	strh	r4, [r6, r3]	@ movhi
	bl	l2p_flush
	bl	FtlVpcTblFlush
	movw	r3, #1164
	strh	r8, [r6, r3]	@ movhi
	ldr	r3, [r5, #-1872]
	ldr	r7, .L3527
	cmp	r3, r4
	add	r1, r7, #880
	beq	.L3509
	ldr	r3, [r7, #-1568]
	cmp	r3, #39
	bhi	.L3509
	ldr	r2, .L3527+4
	movw	r3, #1160
	ldrh	r3, [r2, r3]
	ldrh	r2, [r1]
	cmp	r2, r3
	subcc	r7, r7, #1520
	movcc	r3, r3, asl #1
	strcch	r3, [r7, #-12]	@ movhi
	b	.L3525
.L3509:
	movw	r3, #1160
	ldrh	r1, [r1]
	ldrh	r2, [r6, r3]
	ldr	r3, .L3527
	add	r0, r2, r2, asl #1
	cmp	r1, r0, asr #2
	ble	.L3525
	ldrb	r0, [r3, #-2744]	@ zero_extendqisi2
	sub	r3, r3, #1520
	cmp	r0, #0
	moveq	r2, #20
	streqh	r2, [r3, #-12]	@ movhi
	beq	.L3490
	sub	r2, r2, #2
	strh	r2, [r3, #-12]	@ movhi
.L3525:
	mov	r0, #0
	b	.L3490
.L3511:
	mov	r0, #1
.L3490:
	add	sp, sp, #20
	@ sp needed
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L3528:
	.align	2
.L3527:
	.word	.LANCHOR2
	.word	.LANCHOR4
	.word	.LANCHOR4+1766
	.word	.LANCHOR1
	.word	.LC161
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
	ldr	r4, .L3532
	ldr	r5, .L3532+4
	mov	r0, r4
	ldrh	r1, [r5, #-4]
	bl	FtlGcScanTempBlk
	ldrh	r2, [r4, #2]
	ldrh	r3, [r5, #-4]
	cmp	r2, r3
	ldmccfd	sp!, {r3, r4, r5, pc}
	add	r0, r4, #48
	bl	FtlMapBlkWriteDumpData
	mov	r0, #0
	bl	FtlGcFreeTempBlock
	ldr	r3, .L3532+8
	mov	r2, #0
	str	r2, [r3, #1756]
	ldmfd	sp!, {r3, r4, r5, pc}
.L3533:
	.align	2
.L3532:
	.word	.LANCHOR2+980
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
	ldr	r4, .L3536
	ldr	r3, .L3536+4
	add	r6, r4, #884
	add	r4, r4, #932
	mov	r0, r6
	str	r5, [r3, #1808]
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
.L3537:
	.align	2
.L3536:
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
	stmfd	sp!, {r4, r5, r6, r7, r8, lr}
	.save {r4, r5, r6, r7, r8, lr}
	movw	r3, #1804
	ldr	r7, .L3556
	mov	r2, #0
	ldr	r4, .L3556+4
	strh	r2, [r7, r3]	@ movhi
	movw	r3, #1802
	mvn	r2, #0
	strh	r2, [r7, r3]	@ movhi
	ldr	r3, [r4, #-1732]
	uxth	r0, r3
	bl	FtlFreeSysBlkQueueInit
	bl	FtlScanSysBlk
	movw	r3, #1128
	ldrh	r2, [r4, r3]
	movw	r3, #65535
	cmp	r2, r3
	bne	.L3539
.L3541:
	mvn	r8, #0
	b	.L3540
.L3539:
	bl	FtlLoadSysInfo
	subs	r8, r0, #0
	bne	.L3541
	bl	FtlLoadMapInfo
	bl	FtlLoadVonderInfo
	bl	Ftl_load_ext_data
	bl	FtlLoadEctTbl
	bl	FtlFreeSysBLkSort
	bl	SupperBlkListInit
	bl	FtlPowerLostRecovery
	mov	r0, #1
	bl	FtlUpdateVaildLpn
	sub	r3, r4, #1616
	ldr	r2, [r4, #-1364]
	mov	r0, #12
	ldrh	r1, [r3, #-14]
	mov	r3, r8
.L3542:
	cmp	r3, r1
	bge	.L3547
	mla	ip, r0, r3, r2
	ldr	ip, [ip, #4]
	cmp	ip, #0
	bge	.L3543
.L3547:
	ldr	r5, .L3556+8
	cmp	r3, r1
	add	r6, r5, #68
	ldrh	r2, [r5, #28]
	add	r2, r2, #1
	strh	r2, [r5, #28]	@ movhi
	bge	.L3554
	b	.L3544
.L3543:
	add	r3, r3, #1
	b	.L3542
.L3554:
	movw	r3, #1804
	ldrh	r3, [r7, r3]
	cmp	r3, #0
	beq	.L3548
.L3544:
	ldrh	r3, [r6]
	ldr	r1, [r4, #-1408]
	ldr	ip, .L3556+12
	mov	r3, r3, asl #1
	ldrh	r0, [r6, #4]
	ldrh	r2, [r1, r3]
	rsb	r2, r0, r2
	strh	r2, [r1, r3]	@ movhi
	ldrh	r3, [ip, #-4]
	mov	r2, #0
	ldr	lr, [r4, #-1408]
	strb	r2, [r4, #890]
	strh	r3, [r6, #2]	@ movhi
	ldr	r3, .L3556+16
	strh	r2, [r6, #4]	@ movhi
	ldrh	r1, [r3]
	ldrh	r7, [r3, #4]
	mov	r1, r1, asl #1
	ldrh	r0, [lr, r1]
	rsb	r0, r7, r0
	strh	r0, [lr, r1]	@ movhi
	ldrh	r1, [ip, #-4]
	strh	r2, [r3, #4]	@ movhi
	strb	r2, [r4, #938]
	strh	r1, [r3, #2]	@ movhi
	ldrh	r3, [r5, #30]
	add	r3, r3, #1
	strh	r3, [r5, #30]	@ movhi
	bl	l2p_flush
	bl	FtlVpcTblFlush
	bl	FtlVpcTblFlush
.L3548:
	ldrh	r0, [r6]
	movw	r3, #65535
	ldr	r7, .L3556+20
	cmp	r0, r3
	beq	.L3549
	ldrh	r3, [r7, #4]
	cmp	r3, #0
	bne	.L3549
	ldrh	r3, [r7, #52]
	add	r4, r7, #48
	cmp	r3, #0
	bne	.L3549
	bl	FtlGcRefreshOpenBlock
	ldrh	r0, [r7, #48]
	bl	FtlGcRefreshOpenBlock
	bl	FtlVpcTblFlush
	mov	r0, r7
	bl	allocate_new_data_superblock
	mov	r0, r4
	bl	allocate_new_data_superblock
.L3549:
	ldr	r3, .L3556+24
	ldrb	r3, [r3]	@ zero_extendqisi2
	cmp	r3, #0
	bne	.L3550
	ldrh	r3, [r5, #28]
	tst	r3, #31
	bne	.L3540
.L3550:
	bl	FtlVpcCheckAndModify
.L3540:
	mov	r0, r8
	ldmfd	sp!, {r4, r5, r6, r7, r8, pc}
.L3557:
	.align	2
.L3556:
	.word	.LANCHOR4
	.word	.LANCHOR2
	.word	.LANCHOR2+816
	.word	.LANCHOR2-1664
	.word	.LANCHOR2+932
	.word	.LANCHOR2+884
	.word	.LANCHOR0
	.fnend
	.size	FtlSysBlkInit, .-FtlSysBlkInit
	.align	2
	.global	FtlLowFormat
	.type	FtlLowFormat, %function
FtlLowFormat:
	.fnstart
	@ args = 0, pretend = 0, frame = 16
	@ frame_needed = 0, uses_anonymous_args = 0
	stmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.save {r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.pad #20
	sub	sp, sp, #20
	ldr	r4, .L3593
	ldr	r5, [r4, #-1280]
	cmp	r5, #0
	bne	.L3560
	sub	r6, r4, #1632
	mov	r1, r5
	ldr	r0, [r4, #-1372]
	ldrh	r2, [r6]
	mov	r2, r2, asl #2
	bl	ftl_memset
	ldrh	r2, [r6]
	mov	r1, r5
	ldr	r0, [r4, #-1376]
	mov	r2, r2, asl #2
	bl	ftl_memset
	ldr	r3, [r4, #-1732]
	str	r5, [r4, #-1616]
	str	r5, [r4, #-1612]
	uxth	r0, r3
	bl	FtlFreeSysBlkQueueInit
	bl	FtlLoadBbt
	cmp	r0, #0
	beq	.L3561
	bl	FtlMakeBbt
.L3561:
	ldr	r0, .L3593
	mov	r2, #0
	ldr	ip, .L3593+4
	ldr	lr, .L3593+8
.L3562:
	ldrh	r1, [ip]
	uxth	r3, r2
	add	r2, r2, #1
	cmp	r3, r1, asl #7
	bge	.L3590
	ldr	r5, [r0, #-1456]
	mvn	r1, r3
	orr	r1, r3, r1, asl #16
	str	r1, [r5, r3, asl #2]
	ldr	r1, [r0, #-1452]
	str	lr, [r1, r3, asl #2]
	b	.L3562
.L3590:
	ldr	r9, .L3593+12
	mov	r7, #0
	add	r10, r9, #2
	ldrh	r5, [r9]
.L3564:
	ldrh	r3, [r10]
	ldr	r6, .L3593
	cmp	r3, r5
	ldr	r8, .L3593+16
	bls	.L3591
	mov	r0, r5
	mov	r1, #1
	bl	FtlLowFormatEraseBlock
	add	r5, r5, #1
	uxth	r5, r5
	add	r0, r7, r0
	uxth	r7, r0
	b	.L3564
.L3591:
	ldrh	r1, [r9, #-8]
	sub	r3, r7, #3
	sub	r5, r6, #1728
	cmp	r3, r1, asl #1
	blt	.L3566
	mov	r0, r7
	mov	r7, #0
	bl	__aeabi_uidiv
	ldr	r3, [r6, #-1636]
	add	r0, r0, r3
	uxth	r0, r0
	bl	FtlSysBlkNumInit
	ldr	r0, [r6, #-1732]
	uxth	r0, r0
	bl	FtlFreeSysBlkQueueInit
	ldrh	r5, [r5]
.L3567:
	ldrh	r3, [r8]
	cmp	r3, r5
	bls	.L3566
	mov	r0, r5
	mov	r1, #1
	bl	FtlLowFormatEraseBlock
	add	r5, r5, #1
	uxth	r5, r5
	add	r0, r7, r0
	uxth	r7, r0
	b	.L3567
.L3566:
	mov	r5, #0
	mov	r6, r5
.L3569:
	ldrh	r1, [r9]
	uxth	r0, r5
	ldr	r10, .L3593
	add	r5, r5, #1
	cmp	r1, r0
	sub	r2, r10, #1728
	bls	.L3592
	mov	r1, #0
	bl	FtlLowFormatEraseBlock
	add	r0, r6, r0
	uxth	r6, r0
	b	.L3569
.L3592:
	sub	r5, r10, #1712
	ldr	ip, [r10, #-1724]
	ldrh	r8, [r2, #-8]
	ldrh	r1, [r5, #-14]
	mov	r0, ip
	str	ip, [sp, #8]
	str	r1, [r10, #-1544]
	mov	r1, r8
	bl	__aeabi_uidiv
	movw	r2, #1160
	ubfx	r3, r0, #5, #16
	mov	fp, r0
	str	r0, [r10, #-1284]
	add	r1, r3, #36
	ldr	r0, .L3593+20
	str	r5, [sp, #4]
	str	r3, [sp]
	strh	r1, [r0, r2]	@ movhi
	mov	r1, #24
	mul	r1, r1, r8
	mov	r5, r0
	ldr	ip, [sp, #8]
	cmp	r6, r1
	ble	.L3571
	rsb	r0, r6, ip
	mov	r1, r8
	str	r2, [sp, #8]
	bl	__aeabi_uidiv
	ldr	r2, [sp, #8]
	str	r0, [r10, #-1284]
	mov	r0, r0, lsr #5
	add	r0, r0, #24
	strh	r0, [r5, r2]	@ movhi
.L3571:
	ldr	r3, [r4, #-1872]
	cmp	r3, #1
	bne	.L3572
	movw	r2, #1160
	mov	r0, r6
	ldrh	r3, [r5, r2]
	mov	r1, r8
	str	r2, [sp, #12]
	str	r3, [sp, #8]
	bl	__aeabi_uidiv
	ldr	r3, [sp, #8]
	ldr	r2, [sp, #12]
	uxtah	r0, r3, r0
	add	r3, r3, r0, asr #2
	strh	r3, [r5, r2]	@ movhi
.L3572:
	ldrb	r3, [r4, #-2744]	@ zero_extendqisi2
	cmp	r3, #0
	beq	.L3573
	movw	r2, #1160
	mov	r0, r6
	ldrh	r3, [r5, r2]
	mov	r1, r8
	str	r2, [sp, #12]
	str	r3, [sp, #8]
	bl	__aeabi_uidiv
	ldr	r3, [sp, #8]
	ldr	r2, [sp, #12]
	uxtah	r0, r3, r0
	add	r3, r3, r0, asr #2
	strh	r3, [r5, r2]	@ movhi
.L3573:
	ldr	ip, .L3593
	sub	r2, ip, #1664
	ldrh	r3, [r2, #-10]
	cmp	r3, #0
	beq	.L3575
	movw	r1, #1160
	ldrh	r0, [r5, r1]
	add	r0, r0, r3, lsr #1
	strh	r0, [r5, r1]	@ movhi
	mul	r0, r8, r3
	cmp	r0, r6
	strgt	fp, [ip, #-1284]
	addgt	r3, r3, #32
	ldrgt	r0, [sp]
	addgt	r3, r0, r3
	ldrgt	r0, .L3593+20
	strgth	r3, [r0, r1]	@ movhi
.L3575:
	movw	r3, #1160
	ldr	r10, .L3593+24
	ldrh	r1, [r5, r3]
	ldr	r3, [r4, #-1284]
	rsb	r3, r1, r3
	mul	r8, r8, r3
	ldrh	r3, [r2, #-4]
	str	r8, [r5, #1156]
	mul	r8, r3, r8
	ldrh	r3, [r10, #-14]
	str	r8, [r4, #-1284]
	mul	r8, r3, r8
	str	r8, [r4, #-2740]
	bl	FtlBbmTblFlush
	ldr	r2, [r4, #-1720]
	add	r1, r6, r7
	ldrh	r3, [r10, #-6]
	add	r3, r3, r2, lsr #3
	cmp	r1, r3
	bls	.L3577
	ldr	r0, .L3593+28
	mov	r2, r2, lsr #5
	bl	printk
.L3577:
	ldr	r3, [sp, #4]
	mov	r1, #0
	ldr	r0, [r4, #-1408]
	mvn	r7, #0
	ldr	r6, .L3593+32
	ldrh	r2, [r3, #-14]
	sub	fp, r6, #884
	mov	r10, r6
	mov	r2, r2, asl #1
	bl	ftl_memset
	movw	r2, #1164
	strh	r7, [r5, r2]	@ movhi
	mov	r3, #0
	ldr	r2, .L3593+36
	mov	r1, #255
	ldr	r0, [r4, #-1396]
	str	r3, [r4, #1124]
	strh	r3, [r2, #2]	@ movhi
	ldrh	r2, [r9]
	strb	r3, [r5, #1170]
	strb	r3, [r5, #1172]
	mov	r2, r2, lsr #3
	strh	r3, [r6, #2]	@ movhi
	strb	r3, [r4, #890]
	strh	r3, [r6]	@ movhi
	mov	r3, #1
	strb	r3, [r4, #892]
	bl	ftl_memset
.L3578:
	ldr	r9, .L3593
	add	r8, r9, #884
	mov	r0, r8
	bl	make_superblock
	ldrb	r3, [r4, #891]	@ zero_extendqisi2
	cmp	r3, #0
	ldrh	r3, [r6]
	bne	.L3579
	ldr	r2, [fp, #-1408]
	mov	r3, r3, asl #1
	strh	r7, [r2, r3]	@ movhi
	ldrh	r3, [r10]
	add	r3, r3, #1
	strh	r3, [r10]	@ movhi
	b	.L3578
.L3579:
	ldr	r2, [r9, #-1616]
	mov	r3, r3, asl #1
	ldrh	r1, [r8, #4]
	mvn	fp, #0
	str	r2, [r9, #896]
	add	r2, r2, #1
	str	r2, [r9, #-1616]
	ldr	r2, [r9, #-1408]
	strh	r1, [r2, r3]	@ movhi
	add	r2, r9, #932
	mov	r3, #0
	strb	r3, [r9, #938]
	strh	r3, [r2, #2]	@ movhi
	mov	r7, r2
	ldrh	r3, [r8]
	mov	r10, r2
	add	r3, r3, #1
	strh	r3, [r2]	@ movhi
	mov	r3, #1
	strb	r3, [r9, #940]
.L3580:
	ldr	r6, .L3593
	add	r8, r6, #932
	mov	r0, r8
	bl	make_superblock
	ldrb	r3, [r4, #939]	@ zero_extendqisi2
	cmp	r3, #0
	ldrh	r3, [r7]
	bne	.L3581
	ldr	r2, [r9, #-1408]
	mov	r3, r3, asl #1
	strh	fp, [r2, r3]	@ movhi
	ldrh	r3, [r10]
	add	r3, r3, #1
	strh	r3, [r10]	@ movhi
	b	.L3580
.L3581:
	ldr	r2, [r6, #-1616]
	mov	r3, r3, asl #1
	ldrh	r1, [r8, #4]
	mvn	r4, #0
	str	r2, [r6, #944]
	add	r2, r2, #1
	str	r2, [r6, #-1616]
	ldr	r2, [r6, #-1408]
	strh	r1, [r2, r3]	@ movhi
	add	r3, r6, #980
	strh	r4, [r3]	@ movhi
	bl	FtlFreeSysBlkQueueOut
	ldr	r3, .L3593+40
	movw	r2, #1128
	strh	r4, [r3, #4]	@ movhi
	strh	r0, [r6, r2]	@ movhi
	mov	r2, #0
	strh	r2, [r3, #2]	@ movhi
	ldr	r2, [r5, #1156]
	strh	r2, [r3, #6]	@ movhi
	ldr	r3, [r6, #-1616]
	str	r3, [r6, #1136]
	add	r3, r3, #1
	str	r3, [r6, #-1616]
	bl	FtlVpcTblFlush
	bl	FtlSysBlkInit
	cmp	r0, #0
	ldreq	r3, .L3593+44
	moveq	r2, #1
	streq	r2, [r3, #3444]
.L3560:
	mov	r0, #0
	add	sp, sp, #20
	@ sp needed
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L3594:
	.align	2
.L3593:
	.word	.LANCHOR2
	.word	.LANCHOR2-1662
	.word	168778952
	.word	.LANCHOR2-1728
	.word	.LANCHOR2-1726
	.word	.LANCHOR4
	.word	.LANCHOR2-1648
	.word	.LC162
	.word	.LANCHOR2+884
	.word	.LANCHOR4+1164
	.word	.LANCHOR2+1128
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
	ldr	r4, .L3624
	ldrb	r3, [r4, #-2744]	@ zero_extendqisi2
	cmp	r3, #0
	beq	.L3596
.L3598:
	mov	r0, #0
	b	.L3597
.L3596:
	ldr	r3, .L3624+4
	ldr	r0, [r3]
	bl	FlashInit
	cmp	r0, #0
	bne	.L3598
	bl	FlashLoadFactorBbt
	cmp	r0, #0
	beq	.L3599
	bl	FlashMakeFactorBbt
.L3599:
	ldr	r0, [r4, #-1776]
	bl	FlashReadIdbDataRaw
	cmp	r0, #0
	beq	.L3600
	mov	r1, #0
	mov	r2, #16
	mov	r0, sp
	bl	FlashReadFacBbtData
	mov	r3, #0
	mov	r2, r3
	mov	ip, #1
	ldr	r1, [sp]
.L3601:
	ands	lr, r1, ip, asl r2
	add	r0, r3, #1
	add	r2, r2, #1
	movne	r3, r0
	cmp	r2, #16
	bne	.L3601
	cmp	r3, #6
	ldrls	r3, .L3624+8
	bls	.L3620
	mov	r2, #0
	mov	ip, #1
.L3604:
	ands	lr, r1, ip, asl r2
	add	r0, r3, #1
	add	r2, r2, #1
	movne	r3, r0
	cmp	r2, #24
	bne	.L3604
	cmp	r3, #17
	ldr	r3, .L3624+8
	movhi	r2, #36
.L3620:
	strb	r2, [r3, #1]
	ldr	r3, .L3624+8
	ldrb	r2, [r3, #1]	@ zero_extendqisi2
	ldr	r3, .L3624+12
	strh	r2, [r3, #26]	@ movhi
.L3600:
	ldr	r1, .L3624+16
	ldr	r0, .L3624+20
	bl	printk
	ldr	r0, .L3624+12
	bl	FtlConstantsInit
	bl	FtlVariablesInit
	ldr	r0, [r4, #-1732]
	mov	r4, #1
	uxth	r0, r0
	bl	FtlFreeSysBlkQueueInit
.L3606:
	bl	FtlLoadBbt
	cmp	r0, #0
	beq	.L3607
.L3622:
	bl	FtlLowFormat
	cmp	r4, #3
	addls	r4, r4, #1
	bls	.L3606
.L3623:
	mvn	r0, #0
	b	.L3597
.L3607:
	bl	FtlSysBlkInit
	cmp	r0, #0
	bne	.L3622
	ldr	r3, .L3624+24
	mov	r2, #1
	str	r2, [r3, #3444]
.L3597:
	add	sp, sp, #16
	@ sp needed
	ldmfd	sp!, {r4, pc}
.L3625:
	.align	2
.L3624:
	.word	.LANCHOR2
	.word	RK29_NANDC_REG_BASE
	.word	.LANCHOR0
	.word	.LANCHOR2-2772
	.word	.LC145
	.word	.LC48
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
	ldr	r4, .L3643
	ldr	r3, [r4, #-1280]
	cmp	r3, #0
	beq	.L3627
.L3630:
	mov	r0, #0
	ldmfd	sp!, {r4, r5, r6, r7, r8, pc}
.L3627:
	ldrb	r3, [r4, #-2744]	@ zero_extendqisi2
	ldr	r6, .L3643+4
	cmp	r3, #0
	beq	.L3629
	ldr	r3, [r6, #1740]
	tst	r3, #1
	beq	.L3629
	add	r3, r4, #980
	ldrh	r3, [r3, #4]
	cmp	r3, #0
	bne	.L3630
.L3629:
	mov	r2, #0
	ldr	r0, [r4, #-1500]
	ldr	r1, [r6, #1740]
	mov	r3, r2
	bl	FlashProgPages
	mov	r7, #0
	mov	r8, #36
.L3631:
	ldr	r1, [r6, #1740]
	uxth	r3, r7
	ldr	r5, .L3643+4
	cmp	r3, r1
	bcs	.L3642
	mul	r3, r8, r3
	ldr	r0, [r4, #-1500]
	ldr	r2, .L3643
	add	r7, r7, #1
	add	r1, r0, r3
	ldr	lr, [r0, r3]
	ldr	ip, [r1, #12]
	cmn	lr, #1
	bne	.L3632
	add	r0, r2, #980
	ldr	ip, [r2, #-1408]
	mov	r4, #0
	ldrh	r1, [r0]
	mov	r1, r1, asl #1
	strh	r4, [ip, r1]	@ movhi
	ldr	r2, [r2, #-1500]
	ldr	r1, [r5, #1308]
	add	r3, r2, r3
	strh	lr, [r0]	@ movhi
	add	r1, r1, #1
	str	r1, [r5, #1308]
	ldr	r0, [r3, #4]
	ubfx	r0, r0, #10, #16
	bl	FtlBbmMapBadBlock
	bl	FtlBbmTblFlush
	bl	FtlGcPageVarInit
	b	.L3641
.L3632:
	ldr	r0, [ip, #12]
	ldr	r1, [r1, #4]
	ldr	r2, [ip, #8]
	bl	FtlGcUpdatePage
	b	.L3631
.L3642:
	ldr	r0, [r4, #-1500]
	bl	FtlGcBufFree
	mov	r3, #0
	str	r3, [r5, #1740]
	ldr	r3, .L3643+8
	ldrh	r3, [r3, #4]
	cmp	r3, #0
	bne	.L3630
	mov	r0, #1
	bl	FtlGcFreeTempBlock
.L3641:
	mov	r0, #1
	ldmfd	sp!, {r4, r5, r6, r7, r8, pc}
.L3644:
	.align	2
.L3643:
	.word	.LANCHOR2
	.word	.LANCHOR4
	.word	.LANCHOR2+980
	.fnend
	.size	Ftl_gc_temp_data_write_back, .-Ftl_gc_temp_data_write_back
	.align	2
	.global	Ftl_get_new_temp_ppa
	.type	Ftl_get_new_temp_ppa, %function
Ftl_get_new_temp_ppa:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	ldr	r3, .L3649
	movw	r2, #65535
	stmfd	sp!, {r4, lr}
	.save {r4, lr}
	ldrh	r1, [r3]
	cmp	r1, r2
	beq	.L3646
	ldrh	r3, [r3, #4]
	cmp	r3, #0
	bne	.L3647
.L3646:
	bl	FtlCacheWriteBack
	mov	r0, #0
	bl	FtlGcFreeTempBlock
	ldr	r0, .L3649+4
	mov	r4, #0
	add	r0, r0, #980
	strb	r4, [r0, #8]
	bl	allocate_data_superblock
	ldr	r3, .L3649+8
	movw	r2, #1764
	strh	r4, [r3, r2]	@ movhi
	movw	r2, #1766
	strh	r4, [r3, r2]	@ movhi
	bl	l2p_flush
	mov	r0, r4
	bl	FtlEctTblFlush
	bl	FtlVpcTblFlush
.L3647:
	ldr	r0, .L3649
	ldmfd	sp!, {r4, lr}
	b	get_new_active_ppa
.L3650:
	.align	2
.L3649:
	.word	.LANCHOR2+980
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
	ldr	r1, .L3699
	.pad #84
	sub	sp, sp, #84
	ldr	r1, [r1, #3444]
	cmp	r1, #1
	bne	.L3676
	cmp	r0, #16
	mov	r8, r3
	mov	r9, r2
	bne	.L3653
	mov	r1, r2
	add	r0, r4, #256
	mov	r2, r3
	bl	FtlVendorPartRead
	b	.L3652
.L3653:
	ldr	r5, .L3699+4
	ldr	r3, [r5, #-2740]
	cmp	r2, r3
	cmpls	r4, r3
	bcs	.L3676
	add	r2, r4, r2
	str	r2, [sp, #40]
	cmp	r2, r3
	bhi	.L3676
	sub	r3, r5, #1648
	mov	r0, r4
	ldrh	r6, [r3, #-14]
	mov	r1, r6
	bl	__aeabi_uidiv
	mov	r1, r6
	ldr	r3, [sp, #40]
	str	r0, [sp, #32]
	sub	r0, r3, #1
	bl	__aeabi_uidiv
	ldr	r2, .L3699+8
	ldr	r3, [sp, #32]
	str	r0, [sp, #36]
	rsb	r3, r3, #1
	ldr	r1, [sp, #36]
	add	r3, r3, r0
	str	r3, [sp, #28]
	ldr	r3, [r2, #1728]
	ldr	r0, [sp, #32]
	add	r3, r9, r3
	str	r3, [r2, #1728]
	ldr	r3, [r5, #-1588]
	ldr	r2, [sp, #28]
	add	r3, r2, r3
	str	r3, [r5, #-1588]
	bl	FtlCacheMetchLpa
	cmp	r0, #0
	beq	.L3654
	bl	FtlCacheWriteBack
.L3654:
	ldr	r6, [sp, #32]
	mov	r3, #0
	ldr	r5, .L3699+4
	mov	r7, r3
	str	r3, [sp, #24]
	str	r3, [sp, #48]
	str	r3, [sp, #52]
.L3655:
	ldr	r3, [sp, #28]
	cmp	r3, #0
	beq	.L3698
	mov	r0, r6
	add	r1, sp, #76
	mov	r2, #0
	bl	log2phys
	ldr	r3, [sp, #76]
	cmn	r3, #1
	bne	.L3694
	mov	r10, #0
.L3656:
	ldr	r3, .L3699+12
	ldrh	r0, [r3]
	cmp	r10, r0
	bcs	.L3660
	mla	r0, r0, r6, r10
	ldr	r2, [sp, #40]
	cmp	r0, r4
	movcs	r3, #1
	movcc	r3, #0
	cmp	r0, r2
	movcs	r3, #0
	cmp	r3, #0
	beq	.L3658
	rsb	r0, r4, r0
	mov	r1, #0
	mov	r2, #512
	add	r0, r8, r0, asl #9
	bl	ftl_memset
.L3658:
	add	r10, r10, #1
	b	.L3656
.L3694:
	ldr	r2, [r5, #-1504]
	mov	r10, #36
	mla	r10, r10, r7, r2
	str	r3, [r10, #4]
	ldr	r3, [sp, #32]
	cmp	r6, r3
	bne	.L3661
	ldr	r3, [r5, #-1456]
	mov	r0, r4
	str	r3, [r10, #8]
	ldr	r3, .L3699+12
	ldrh	fp, [r3]
	mov	r1, fp
	bl	__aeabi_uidivmod
	rsb	r3, r1, fp
	str	r1, [sp, #56]
	cmp	r3, r9
	movcs	r3, r9
	cmp	r3, fp
	str	r3, [sp, #48]
	streq	r8, [r10, #8]
	b	.L3662
.L3661:
	ldr	r3, [sp, #36]
	cmp	r6, r3
	bne	.L3663
	ldr	r3, [r5, #-1452]
	ldr	r1, [sp, #40]
	str	r3, [r10, #8]
	ldr	r3, .L3699+12
	ldrh	r2, [r3]
	mul	r3, r2, r6
	rsb	r1, r3, r1
	str	r1, [sp, #24]
	cmp	r1, r2
	bne	.L3662
	b	.L3696
.L3663:
	ldr	r3, .L3699+12
	ldrh	r3, [r3]
	mul	r3, r3, r6
.L3696:
	rsb	r3, r4, r3
	add	r3, r8, r3, asl #9
	str	r3, [r10, #8]
.L3662:
	ldr	r3, .L3699+16
	ldr	r2, [r5, #-1440]
	str	r6, [r10, #16]
	ldrh	r3, [r3]
	mul	r3, r3, r7
	add	r7, r7, #1
	bic	r3, r3, #3
	add	r3, r2, r3
	str	r3, [r10, #12]
.L3660:
	ldr	r3, [sp, #28]
	add	r6, r6, #1
	subs	r3, r3, #1
	str	r3, [sp, #28]
	beq	.L3664
	ldr	r3, .L3699+20
	ldrh	r3, [r3]
	cmp	r7, r3, asl #3
	bne	.L3655
.L3664:
	cmp	r7, #0
	beq	.L3655
	ldr	r0, [r5, #-1504]
	mov	r1, r7
	mov	r2, #0
	ldr	fp, .L3699+8
	bl	FlashReadPages
	ldr	r3, [sp, #56]
	mov	r3, r3, asl #9
	str	r3, [sp, #60]
	ldr	r3, [sp, #48]
	mov	r3, r3, asl #9
	str	r3, [sp, #64]
	ldr	r3, [sp, #24]
	mov	r3, r3, asl #9
	str	r3, [sp, #68]
	mov	r3, #0
	str	r3, [sp, #44]
.L3671:
	ldr	r3, [sp, #44]
	mov	ip, #36
	ldr	r1, [sp, #32]
	mul	r10, ip, r3
	ldr	r3, [r5, #-1504]
	add	r3, r3, r10
	ldr	r2, [r3, #16]
	cmp	r2, r1
	bne	.L3666
	ldr	r1, [r3, #8]
	ldr	r3, [r5, #-1456]
	cmp	r1, r3
	bne	.L3667
	ldr	r3, [sp, #60]
	mov	r0, r8
	ldr	r2, [sp, #64]
	add	r1, r1, r3
	b	.L3697
.L3666:
	ldr	r1, [sp, #36]
	cmp	r2, r1
	bne	.L3667
	ldr	r1, [r3, #8]
	ldr	r3, [r5, #-1452]
	cmp	r1, r3
	bne	.L3667
	ldr	r3, .L3699+12
	ldr	r2, [sp, #68]
	ldrh	r0, [r3]
	ldr	r3, [sp, #36]
	mul	r0, r0, r3
	rsb	r0, r4, r0
	add	r0, r8, r0, asl #9
.L3697:
	bl	ftl_memcpy
.L3667:
	ldr	r2, [r5, #-1504]
	add	r3, r2, r10
	ldr	r1, [r2, r10]
	cmn	r1, #1
	streq	r1, [sp, #52]
	ldreq	r2, [fp, #1284]
	addeq	r2, r2, #1
	streq	r2, [fp, #1284]
	ldr	r2, [r3, #12]
	ldr	r1, [r3, #16]
	ldr	r2, [r2, #8]
	cmp	r1, r2
	beq	.L3669
	ldr	r2, [fp, #1284]
	ldr	r0, .L3699+24
	add	r2, r2, #1
	str	r2, [fp, #1284]
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
.L3669:
	ldr	r3, [r5, #-1504]
	add	r2, r3, r10
	ldr	r3, [r3, r10]
	cmp	r3, #256
	bne	.L3670
	ldr	r0, [r2, #4]
	ubfx	r0, r0, #10, #16
	bl	P2V_block_in_plane
	bl	FtlGcRefreshBlock
.L3670:
	ldr	r3, [sp, #44]
	add	r3, r3, #1
	str	r3, [sp, #44]
	cmp	r3, r7
	bne	.L3671
	mov	r7, #0
	b	.L3655
.L3698:
	ldr	r3, .L3699+28
	ldrh	r3, [r3, #-6]
	cmp	r3, #0
	beq	.L3673
	ldr	r0, [sp, #28]
	mov	r1, #1
	bl	ftl_do_gc
.L3673:
	ldr	r0, [sp, #52]
	b	.L3652
.L3676:
	mvn	r0, #0
.L3652:
	add	sp, sp, #84
	@ sp needed
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L3700:
	.align	2
.L3699:
	.word	.LANCHOR1
	.word	.LANCHOR2
	.word	.LANCHOR4
	.word	.LANCHOR2-1662
	.word	.LANCHOR2-1656
	.word	.LANCHOR2-1736
	.word	.LC58
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
	ldr	r2, .L3721
	ldr	r5, .L3721+4
	ldr	r6, .L3721+8
	ldr	r1, .L3721+12
	ldr	r0, .L3721+16
	str	r3, [r6, #3444]
	mov	r3, #0
	str	r3, [r2, #1956]
	str	r3, [r5, #-1280]
	bl	printk
	ldr	r0, .L3721+20
	bl	FtlConstantsInit
	bl	FtlMemInit
	bl	FtlVariablesInit
	ldr	r3, [r5, #-1732]
	uxth	r0, r3
	bl	FtlFreeSysBlkQueueInit
	bl	FtlLoadBbt
	cmp	r0, #0
	ldrne	r0, .L3721+24
	bne	.L3720
	bl	FtlSysBlkInit
	subs	r4, r0, #0
	beq	.L3707
	ldr	r0, .L3721+28
.L3720:
	ldr	r1, .L3721+32
	bl	printk
	b	.L3706
.L3707:
	mov	r1, #1
	str	r1, [r6, #3444]
	bl	ftl_do_gc
	add	r3, r5, #880
	ldrh	r7, [r3]
	mov	r6, r3
	cmp	r7, #15
	bhi	.L3708
	ldr	r8, .L3721+36
	movw	r5, #65535
	ldr	r9, .L3721+40
.L3711:
	ldrh	r3, [r8]
	cmp	r3, r5
	bne	.L3709
	ldrh	r3, [r9]
	cmp	r3, r5
	bne	.L3709
	and	r0, r4, #63
	bl	List_get_gc_head_node
	uxth	r0, r0
	bl	FtlGcRefreshBlock
.L3709:
	mov	r0, #1
	mov	r1, r0
	bl	ftl_do_gc
	mov	r0, #0
	mov	r1, #1
	bl	ftl_do_gc
	ldrh	r2, [r6]
	add	r3, r7, #2
	cmp	r2, r3
	bhi	.L3706
	add	r4, r4, #1
	cmp	r4, #4096
	bne	.L3711
	b	.L3706
.L3708:
	ldrb	r3, [r5, #-2744]	@ zero_extendqisi2
	cmp	r3, #0
	beq	.L3706
	mov	r4, #128
.L3713:
	mov	r0, #1
	mov	r1, r0
	bl	ftl_do_gc
	subs	r4, r4, #1
	bne	.L3713
.L3706:
	mov	r0, #0
	ldmfd	sp!, {r3, r4, r5, r6, r7, r8, r9, pc}
.L3722:
	.align	2
.L3721:
	.word	.LANCHOR4
	.word	.LANCHOR2
	.word	.LANCHOR1
	.word	.LC145
	.word	.LC48
	.word	.LANCHOR2-2772
	.word	.LC163
	.word	.LC164
	.word	.LANCHOR3+240
	.word	.LANCHOR4+1164
	.word	.LANCHOR2-1540
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
	ldr	r10, .L3797
	str	r3, [sp, #8]
	ldr	r3, [r10, #-1280]
	cmp	r3, #0
	bne	.L3764
	mov	r8, r2
	ldr	r2, .L3797+4
	ldr	r2, [r2, #3444]
	cmp	r2, #1
	movne	r0, r3
	bne	.L3724
	cmp	r0, #16
	mov	r7, r1
	bne	.L3725
	add	r0, r1, #256
	ldr	r2, [sp, #8]
	mov	r1, r8
	bl	FtlVendorPartWrite
	b	.L3724
.L3725:
	ldr	r3, [r10, #-2740]
	cmp	r8, r3
	cmpls	r1, r3
	bcs	.L3767
	add	r5, r1, r8
	cmp	r5, r3
	bhi	.L3767
	ldr	r6, .L3797+8
	mov	r3, #2048
	mov	r0, r1
	str	r3, [r6, #1960]
	sub	r3, r10, #1648
	ldrh	r4, [r3, #-14]
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
	ldr	r3, [r10, #-1604]
	add	r3, r2, r3
	ldr	r2, [r10, #-1516]
	str	r3, [r10, #-1604]
	ldr	r3, [r6, #1724]
	add	r3, r8, r3
	str	r3, [r6, #1724]
	movcs	r3, #1
	movcc	r3, #0
	cmp	r2, #0
	str	r3, [sp, #24]
	beq	.L3727
	mov	r3, #36
	ldr	r9, [r10, #-1484]
	mul	r3, r3, r2
	ldr	r2, [sp, #12]
	sub	r3, r3, #36
	add	r9, r9, r3
	ldr	r3, [r9, #16]
	cmp	r2, r3
	bne	.L3728
	ldr	r3, [r10, #-1596]
	mov	r1, r4
	mov	r0, r7
	add	r3, r3, #1
	str	r3, [r10, #-1596]
	ldr	r3, [r6, #1964]
	add	r3, r3, #1
	str	r3, [r6, #1964]
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
	bne	.L3729
	ldr	r3, [r6, #1964]
	cmp	r3, #2
	ble	.L3764
.L3729:
	ldr	r3, [sp, #8]
	rsb	r8, r4, r8
	add	r7, r7, r4
	str	r5, [sp, #4]
	add	r3, r3, r10
	str	r3, [sp, #8]
	ldr	r3, [sp, #12]
	add	r3, r3, #1
	str	r3, [sp, #12]
.L3728:
	mov	r3, #0
	str	r3, [r6, #1964]
.L3727:
	ldr	r0, [sp, #12]
	ldr	r1, [sp, #28]
	bl	FtlCacheMetchLpa
	cmp	r0, #0
	beq	.L3730
	bl	FtlCacheWriteBack
.L3730:
	ldr	r5, .L3797+12
	mov	r3, #0
	str	r3, [sp, #16]
	sub	r4, r5, #884
	str	r3, [sp, #32]
	str	r5, [r6, #1948]
	ldr	r6, [sp, #12]
.L3731:
	ldr	r3, [sp, #4]
	cmp	r3, #0
	beq	.L3796
	ldrh	r2, [r5, #4]
	cmp	r2, #0
	bne	.L3732
	ldr	r3, .L3797+12
	ldr	r9, .L3797+4
	cmp	r5, r3
	bne	.L3733
	add	r0, r5, #48
	ldrh	r10, [r0, #4]
	cmp	r10, #0
	bne	.L3734
	bl	allocate_new_data_superblock
	str	r10, [r9, #3452]
.L3734:
	ldr	r0, .L3797+12
	bl	allocate_new_data_superblock
	ldr	r3, [r9, #3452]
	cmp	r3, #0
	ldrne	r5, .L3797+16
	bne	.L3735
.L3736:
	ldr	r5, .L3797+12
	b	.L3735
.L3733:
	ldrh	r3, [r3, #4]
	str	r2, [r9, #3452]
	cmp	r3, #0
	bne	.L3736
	mov	r0, r5
	bl	allocate_new_data_superblock
.L3735:
	ldrh	r3, [r5, #4]
	cmp	r3, #0
	bne	.L3737
	mov	r0, r5
	bl	allocate_new_data_superblock
.L3737:
	ldr	r3, .L3797+8
	str	r5, [r3, #1948]
.L3732:
	ldr	r3, [r4, #-1520]
	ldr	r1, [r4, #-1516]
	ldrh	r2, [r5, #4]
	rsb	r3, r1, r3
	cmp	r2, r3
	movcs	r2, r3
	ldr	r3, [sp, #4]
	cmp	r2, r3
	movcc	r3, r2
	str	r3, [sp, #44]
	mov	r3, #0
.L3794:
	str	r3, [sp, #20]
	ldr	r3, [sp, #20]
	ldr	r2, [sp, #44]
	cmp	r3, r2
	beq	.L3739
	ldrh	r3, [r5, #4]
	cmp	r3, #0
	beq	.L3739
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
	beq	.L3740
	ldr	r3, .L3797+20
	ldrh	r2, [r3]
	add	r3, r8, r7
	mls	r3, r2, r6, r3
	cmp	r3, r2
	bne	.L3739
.L3740:
	add	r1, sp, #56
	mov	r2, #0
	mov	r0, r6
	str	ip, [sp, #52]
	bl	log2phys
	mov	r0, r5
	bl	get_new_active_ppa
	ldr	r10, [r4, #-1516]
	ldr	r1, [r4, #-1484]
	mov	r3, #36
	ldr	fp, .L3797+24
	mla	r1, r3, r10, r1
	ldrh	r2, [fp]
	str	r6, [r1, #16]
	str	r0, [r1, #4]
	mul	r0, r10, r2
	str	r3, [sp, #48]
	bic	r3, r0, #3
	str	r3, [sp, #36]
	ldr	r0, [sp, #36]
	ldr	r3, [r4, #-1432]
	add	r9, r3, r0
	ldrh	r0, [fp, #-2]
	str	r9, [r1, #12]
	str	r3, [sp, #40]
	mul	r10, r10, r0
	ldr	r0, [r4, #-1460]
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
	beq	.L3741
	cmp	r10, #0
	beq	.L3742
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
	b	.L3743
.L3742:
	cmp	ip, #0
	beq	.L3743
	ldr	r3, .L3797+20
	add	r2, r8, r7
	str	r10, [sp, #32]
	ldrh	r1, [r3]
	smulbb	r1, r1, r6
	rsb	r2, r1, r2
	uxth	r3, r2
	str	r3, [sp, #16]
.L3743:
	ldr	r3, .L3797+20
	ldr	r2, [sp, #16]
	ldrh	r3, [r3]
	cmp	r2, r3
	bne	.L3744
	cmp	r10, #0
	ldr	r0, .L3797
	moveq	r3, r2
	ldr	r2, .L3797
	muleq	r1, r6, r3
	ldreq	r3, [sp, #8]
	ldr	r2, [r2, #-1516]
	ldr	r0, [r0, #-1484]
	rsbeq	r1, r7, r1
	ldrne	r1, [sp, #8]
	addeq	r1, r3, r1, asl #9
	ldr	r3, [sp, #24]
	cmp	r3, #0
	mov	r3, #36
	mla	r3, r3, r2, r0
	strne	r1, [r3, #8]
	bne	.L3747
	ldr	r0, [r3, #8]
	ldr	r3, .L3797+28
	ldrh	r2, [r3]
	b	.L3792
.L3744:
	ldr	r2, [sp, #56]
	mov	r3, #36
	cmn	r2, #1
	beq	.L3748
	ldr	r1, [r4, #-1484]
	add	r0, sp, #60
	str	r2, [sp, #64]
	ldr	r2, [r4, #-1516]
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
	ldreq	r2, .L3797+8
	ldreq	r3, [r2, #1284]
	addeq	r3, r3, #1
	streq	r3, [r2, #1284]
	beq	.L3751
.L3749:
	ldr	r3, [r9, #8]
	cmp	r3, r6
	beq	.L3751
	ldr	r2, .L3797+8
	ldr	r0, .L3797+32
	ldr	r3, [r2, #1284]
	add	r3, r3, #1
	str	r3, [r2, #1284]
	mov	r2, r6
	ldr	r1, [r9, #8]
	bl	printk
	b	.L3751
.L3748:
	ldr	r2, [r4, #-1516]
	ldr	r1, [r4, #-1484]
	mla	r3, r3, r2, r1
	mov	r1, #0
	ldr	r0, [r3, #8]
	ldr	r3, .L3797+28
	ldrh	r2, [r3]
	bl	ftl_memset
.L3751:
	cmp	r10, #0
	mov	r3, #36
	beq	.L3752
	ldr	r1, [r4, #-1484]
	ldr	r2, [r4, #-1516]
	mla	r3, r3, r2, r1
	ldr	r1, [sp, #8]
	ldr	r0, [r3, #8]
	ldr	r3, [sp, #32]
	add	r0, r0, r3, asl #9
	b	.L3795
.L3752:
	ldr	r1, [r4, #-1516]
	ldr	r2, [r4, #-1484]
	mla	r3, r3, r1, r2
	ldr	r2, .L3797+20
	ldrh	r1, [r2]
	ldr	r0, [r3, #8]
	mul	r1, r1, r6
	ldr	r3, [sp, #8]
	rsb	r1, r7, r1
	add	r1, r3, r1, asl #9
.L3795:
	ldr	r3, [sp, #16]
	mov	r2, r3, asl #9
	b	.L3792
.L3741:
	ldr	r2, [sp, #24]
	cmp	r2, #0
	ldr	r2, [r4, #-1516]
	beq	.L3753
	ldr	r1, [r4, #-1484]
	mla	r3, r3, r2, r1
	ldr	r2, .L3797+20
	ldrh	fp, [r2]
	ldr	r2, [sp, #8]
	mul	fp, fp, r6
	rsb	fp, r7, fp
	add	fp, r2, fp, asl #9
	str	fp, [r3, #8]
	b	.L3747
.L3753:
	ldr	r0, [r4, #-1484]
	mla	r3, r3, r2, r0
	ldr	r2, .L3797+20
	ldrh	r1, [r2]
	ldrh	r2, [fp, #-2]
	ldr	r0, [r3, #8]
	mul	r1, r1, r6
	ldr	r3, [sp, #8]
	rsb	r1, r7, r1
	add	r1, r3, r1, asl #9
.L3792:
	bl	ftl_memcpy
.L3747:
	ldr	r3, .L3797+36
	ldr	r2, [sp, #40]
	ldr	r1, [sp, #36]
	strh	r3, [r2, r1]	@ movhi
	ldr	r3, [r4, #-1612]
	str	r6, [r9, #8]
	add	r6, r6, #1
	str	r3, [r9, #4]
	add	r3, r3, #1
	cmn	r3, #1
	moveq	r3, #0
	str	r3, [r4, #-1612]
	ldr	r3, [sp, #56]
	str	r3, [r9, #12]
	ldrh	r3, [r5]
	strh	r3, [r9, #2]	@ movhi
	ldr	r3, [r4, #-1516]
	add	r3, r3, #1
	str	r3, [r4, #-1516]
	ldr	r3, [sp, #20]
	add	r3, r3, #1
	b	.L3794
.L3739:
	ldr	r3, [sp, #4]
	ldr	r2, [sp, #20]
	ldr	r1, [sp, #24]
	rsb	r3, r2, r3
	ldr	r2, [r4, #-1520]
	str	r3, [sp, #4]
	ldr	r3, [r4, #-1516]
	cmp	r3, r2
	orrcs	r1, r1, #1
	cmp	r1, #0
	bne	.L3757
	ldrh	r3, [r5, #4]
	cmp	r3, #0
	beq	.L3757
.L3759:
	mov	r3, #0
	str	r3, [sp, #24]
	b	.L3731
.L3757:
	bl	FtlCacheWriteBack
	ldr	r2, .L3797
	mov	r3, #0
	str	r3, [r2, #-1516]
	ldr	r3, [sp, #4]
	cmp	r3, #1
	bhi	.L3731
	b	.L3759
.L3796:
	mov	r0, r3
	ldr	r2, [sp, #12]
	ldr	r3, [sp, #28]
	rsb	r1, r2, r3
	bl	ftl_do_gc
	ldr	r1, .L3797+40
	ldrh	r3, [r1]
	mov	r6, r1
	cmp	r3, #5
	bls	.L3770
	cmp	r3, #31
	bhi	.L3764
	ldr	r3, .L3797+44
	ldrb	r3, [r3]	@ zero_extendqisi2
	cmp	r3, #0
	bne	.L3764
.L3770:
	ldr	r5, .L3797+48
	ldr	r4, [sp, #4]
	ldr	r7, .L3797+52
	add	r8, r5, #2
.L3783:
	ldrh	r2, [r7]
	movw	r3, #65535
	cmp	r2, r3
	bne	.L3763
	ldrh	r3, [r5]
	cmp	r3, r2
	bne	.L3763
	ldrh	r2, [r8]
	cmp	r2, r3
	bne	.L3763
	and	r0, r4, #7
	bl	List_get_gc_head_node
	uxth	r0, r0
	bl	FtlGcRefreshBlock
.L3763:
	ldr	r2, .L3797+56
	mov	r0, #1
	mov	r1, r0
	mov	r3, #128
	strh	r3, [r2]	@ movhi
	strh	r3, [r2, #-2]	@ movhi
	bl	ftl_do_gc
	mov	r0, #0
	mov	r1, #1
	bl	ftl_do_gc
	ldr	r3, .L3797
	ldr	r3, [r3, #-1280]
	cmp	r3, #0
	bne	.L3764
	ldrh	r3, [r6]
	cmp	r3, #2
	bhi	.L3764
	add	r4, r4, #1
	cmp	r4, #256
	bne	.L3783
	b	.L3764
.L3767:
	mvn	r0, #0
	b	.L3724
.L3764:
	mov	r0, #0
.L3724:
	add	sp, sp, #100
	@ sp needed
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L3798:
	.align	2
.L3797:
	.word	.LANCHOR2
	.word	.LANCHOR1
	.word	.LANCHOR4
	.word	.LANCHOR2+884
	.word	.LANCHOR2+932
	.word	.LANCHOR2-1662
	.word	.LANCHOR2-1656
	.word	.LANCHOR2-1658
	.word	.LC165
	.word	-3947
	.word	.LANCHOR2+880
	.word	.LANCHOR0
	.word	.LANCHOR2-1540
	.word	.LANCHOR4+1164
	.word	.LANCHOR2-1530
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
	ldr	r4, .L3818
	ldrb	r3, [r4, #-2744]	@ zero_extendqisi2
	cmp	r3, #0
	beq	.L3802
	ldr	r8, .L3818+4
	movw	r3, #1802
	add	r9, r4, #884
	add	r5, r4, #932
	ldr	r0, .L3818+8
	ldrh	r7, [r8, r3]
	ldr	r3, [r4, #-1408]
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
.L3804:
	subs	r5, r5, #1
	beq	.L3808
	mov	r0, #1
	mov	r1, r0
	bl	ftl_do_gc
	ldr	r3, [r4, #-1408]
	ldrh	r3, [r3, r6]
	cmp	r3, #0
	bne	.L3804
.L3808:
	ldr	r3, [r4, #-1408]
	mov	r1, r7
	ldr	r0, .L3818+8
	ldr	r9, .L3818
	ldrh	r2, [r3, r6]
	bl	printk
	ldr	r3, [r4, #-1408]
	ldrh	r5, [r3, r6]
	cmp	r5, #0
	bne	.L3806
	add	r0, sp, #48
	movw	r10, #65535
	mov	fp, #36
	strh	r7, [r0, #-48]!	@ movhi
	bl	make_superblock
	sub	r3, r9, #1728
	ldr	r9, [r9, #-1492]
	ldrh	lr, [r3, #-8]
	mov	r3, r5
	mov	ip, r3
	add	r0, sp, #14
.L3809:
	uxth	r2, r3
	cmp	r2, lr
	bcs	.L3817
	ldrh	r2, [r0, #2]!
	add	r3, r3, #1
	cmp	r2, r10
	movne	r2, r2, asl #10
	mlane	r1, fp, r5, r9
	addne	r5, r5, #1
	uxthne	r5, r5
	stmneib	r1, {r2, ip}
	strne	ip, [r1, #12]
	b	.L3809
.L3817:
	ldr	r3, [r4, #-1408]
	mov	r1, r7
	ldr	r0, .L3818+12
	ldrh	r2, [r3, r6]
	bl	printk
	mov	r1, #0
	mov	r2, r5
	ldr	r0, [r4, #-1492]
	bl	FlashEraseBlocks
	ldr	r0, [r4, #-1492]
	mov	r1, #1
	mov	r2, r5
	bl	FlashEraseBlocks
.L3806:
	movw	r3, #1802
	mvn	r2, #0
	strh	r2, [r8, r3]	@ movhi
.L3802:
	add	sp, sp, #52
	@ sp needed
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L3819:
	.align	2
.L3818:
	.word	.LANCHOR2
	.word	.LANCHOR4
	.word	.LC166
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
	.align	2
.LANCHOR3 = . + 0
.LC0:
	.byte	60
	.byte	40
	.byte	24
	.byte	16
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
	.type	__func__.20378, %object
	.size	__func__.20378, 11
__func__.20378:
	.ascii	"FtlMemInit\000"
	.space	1
	.type	__func__.21125, %object
	.size	__func__.21125, 12
__func__.21125:
	.ascii	"FtlCheckVpc\000"
	.type	__func__.21157, %object
	.size	__func__.21157, 17
__func__.21157:
	.ascii	"FtlDumpBlockInfo\000"
	.space	3
	.type	__func__.21176, %object
	.size	__func__.21176, 16
__func__.21176:
	.ascii	"FtlScanAllBlock\000"
	.type	__func__.21444, %object
	.size	__func__.21444, 17
__func__.21444:
	.ascii	"ftl_scan_all_ppa\000"
	.space	3
	.type	__func__.21424, %object
	.size	__func__.21424, 21
__func__.21424:
	.ascii	"FtlVpcCheckAndModify\000"
	.space	3
	.type	__func__.20451, %object
	.size	__func__.20451, 8
__func__.20451:
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
	.ascii	"%s\012\000"
.LC49:
	.ascii	"no ect\000"
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
	.ascii	"FTL version: 5.0.63 20200923\000"
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
