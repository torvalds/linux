/*
 * Copyright (c) 2016-2018, Fuzhou Rockchip Electronics Co., Ltd
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 * date: 2021-07-16
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
	mov	r0, r1, lsr #6
	bx	r3	@ indirect register sibling call
.L3:
	.align	2
.L2:
	.word	274877907
	.word	arm_delay_ops
	.fnend
	.size	ndelay, .-ndelay
	.align	2
	.type	flash_read_ecc, %function
flash_read_ecc:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	ldr	r2, .L6
	stmfd	sp!, {r4, lr}
	.save {r4, lr}
	ldr	r3, [r2, r0, asl #3]
	add	r2, r2, r0, asl #3
	mov	r0, #80
	ldrb	r4, [r2, #4]	@ zero_extendqisi2
	add	r4, r3, r4, asl #8
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
	cmp	r0, r3
	movcc	r0, r3
	ldr	r3, [r4, #2048]
	and	r3, r3, #15
	cmp	r0, r3
	movcc	r0, r3
	ldmfd	sp!, {r4, pc}
.L7:
	.align	2
.L6:
	.word	.LANCHOR0
	.fnend
	.size	flash_read_ecc, .-flash_read_ecc
	.align	2
	.global	FlashMemCmp8
	.type	FlashMemCmp8, %function
FlashMemCmp8:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	ldr	r3, .L18
	str	lr, [sp, #-4]!
	.save {lr}
	ldrb	r3, [r3, #32]	@ zero_extendqisi2
	cmp	r3, #0
	beq	.L11
	ldrb	r3, [r1, #1]	@ zero_extendqisi2
	ldrb	ip, [r0, #1]	@ zero_extendqisi2
	cmp	ip, r3
	movne	r3, #0
	beq	.L15
.L11:
	cmp	r3, r2
	beq	.L15
	ldrb	lr, [r0, r3]	@ zero_extendqisi2
	ldrb	ip, [r1, r3]	@ zero_extendqisi2
	add	r3, r3, #1
	cmp	lr, ip
	beq	.L11
	mov	r0, r3
	ldr	pc, [sp], #4
.L15:
	mov	r0, #0
	ldr	pc, [sp], #4
.L19:
	.align	2
.L18:
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
	ldr	r3, .L21
	ldrb	r2, [r3, #33]	@ zero_extendqisi2
	ldr	r3, [r3, #36]
	mul	r3, r3, r2
	cmp	r1, r3
	movcs	r2, #0
	movcc	r2, #1
	cmp	r0, #0
	movne	r2, #0
	eor	r0, r2, #1
	bx	lr
.L22:
	.align	2
.L21:
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
	ldr	r2, .L32
	stmfd	sp!, {r4, lr}
	.save {r4, lr}
	mov	r3, r3, asl #1
	ldrh	r4, [r2, r3]
	ldr	r3, .L32+4
	ldrb	r3, [r3, #40]	@ zero_extendqisi2
	cmp	r3, #0
	beq	.L24
	bl	FlashRsvdBlkChk
	cmp	r0, #0
	orrne	r4, r4, #-1073741824
.L24:
	mov	r0, r4
	ldmfd	sp!, {r4, pc}
.L33:
	.align	2
.L32:
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
	ldr	r2, .L43
	stmfd	sp!, {r4, r5, r6, lr}
	.save {r4, r5, r6, lr}
	mov	r3, r3, asl #1
	mov	r6, r0
	ldrh	r5, [r2, r3]
	ldr	r3, .L43+4
	ldrb	r2, [r3, #40]	@ zero_extendqisi2
	mov	r4, r3
	cmp	r2, #0
	beq	.L35
	bl	FlashRsvdBlkChk
	cmp	r0, #0
	orrne	r5, r5, #-1073741824
.L35:
	ldr	r3, [r4, r6, asl #3]
	str	r5, [r3, #336]
	ldmfd	sp!, {r4, r5, r6, pc}
.L44:
	.align	2
.L43:
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
	ldr	r3, .L51
	movhi	r2, #1024
	bhi	.L50
	cmp	r0, #256
	movhi	r2, #512
	bhi	.L50
	cmp	r0, #128
	strls	r0, [r3, #36]
	bxls	lr
	mov	r2, #256
.L50:
	str	r2, [r3, #36]
	bx	lr
.L52:
	.align	2
.L51:
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
	ldr	ip, .L56
	str	lr, [sp, #-4]!
	.save {lr}
	add	r3, ip, r0, asl #3
	ldr	r2, [ip, r0, asl #3]
	ldr	ip, [ip, #44]
	ldrb	r3, [r3, #4]	@ zero_extendqisi2
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
.L57:
	.align	2
.L56:
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
	ldr	ip, .L63
	stmfd	sp!, {r4, lr}
	.save {r4, lr}
	add	r3, ip, r0, asl #3
	ldr	r2, [ip, r0, asl #3]
	uxtb	r4, r1
	ldrb	ip, [ip, #64]	@ zero_extendqisi2
	mov	lr, r1, lsr #8
	ldrb	r3, [r3, #4]	@ zero_extendqisi2
	cmp	ip, #1
	mov	ip, r1, lsr #16
	mov	r3, r3, asl #8
	add	r3, r2, r3
	bne	.L59
	mov	r2, #6
	str	r2, [r3, #2056]
	mov	r2, #0
	str	r2, [r3, #2052]
	str	r2, [r3, #2052]
	str	r4, [r3, #2052]
	str	lr, [r3, #2052]
	str	ip, [r3, #2052]
	b	.L62
.L59:
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
.L62:
	mov	r2, #224
	str	r2, [r3, #2056]
	ldmfd	sp!, {r4, lr}
	b	FlashSetRandomizer
.L64:
	.align	2
.L63:
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
	ldr	ip, .L67
	mov	r2, r1, lsr #16
	str	lr, [sp, #-4]!
	.save {lr}
	ldr	r3, [ip, r0, asl #3]
	add	ip, ip, r0, asl #3
	ldrb	ip, [ip, #4]	@ zero_extendqisi2
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
	ldr	lr, [sp], #4
	b	FlashSetRandomizer
.L68:
	.align	2
.L67:
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
	ldr	lr, .L75
	ldr	ip, [lr, r0, asl #3]
	add	r0, lr, r0, asl #3
	ldrb	r3, [r0, #4]	@ zero_extendqisi2
	mov	r3, r3, asl #8
	beq	.L70
	add	r2, ip, r3
	mov	r0, #96
	str	r0, [r2, #2056]
	uxtb	r0, r1
	str	r0, [r2, #2052]
	mov	r0, r1, lsr #8
	str	r0, [r2, #2052]
	mov	r0, r1, lsr #16
	str	r0, [r2, #2052]
	ldr	r2, [lr, #36]
	add	r1, r1, r2
.L70:
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
.L76:
	.align	2
.L75:
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
	stmfd	sp!, {r4, lr}
	.save {r4, lr}
	mov	r2, r1, lsr #16
	ldr	lr, .L79
	add	ip, lr, r0, asl #3
	ldr	r3, [lr, r0, asl #3]
	ldrb	r4, [ip, #4]	@ zero_extendqisi2
	ldrb	ip, [lr, #59]	@ zero_extendqisi2
	add	r3, r3, r4, asl #8
	str	ip, [r3, #2056]
	mov	ip, #0
	str	ip, [r3, #2052]
	str	ip, [r3, #2052]
	uxtb	ip, r1
	str	ip, [r3, #2052]
	mov	ip, r1, lsr #8
	str	ip, [r3, #2052]
	str	r2, [r3, #2052]
	ldmfd	sp!, {r4, lr}
	b	FlashSetRandomizer
.L80:
	.align	2
.L79:
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
	ldr	r3, .L83
	ldr	r4, [r3, r0, asl #3]
	add	r3, r3, r0, asl #3
	ldr	r0, .L83+4
	ldrb	r5, [r3, #4]	@ zero_extendqisi2
	ldr	r3, .L83+8
	add	r4, r4, r5, asl #8
	ldr	r3, [r3, #4]
	blx	r3
	mov	r3, #16
	str	r3, [r4, #2056]
	ldmfd	sp!, {r3, r4, r5, pc}
.L84:
	.align	2
.L83:
	.word	.LANCHOR0
	.word	64424500
	.word	arm_delay_ops
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
	ldr	r2, .L86
	ldr	r3, [r2, r0, asl #3]
	add	r0, r2, r0, asl #3
	ldrb	r2, [r2, #58]	@ zero_extendqisi2
	ldrb	r1, [r0, #4]	@ zero_extendqisi2
	add	r3, r3, r1, asl #8
	str	r2, [r3, #2056]
	bx	lr
.L87:
	.align	2
.L86:
	.word	.LANCHOR0
	.fnend
	.size	FlashProgDpFirstCmd, .-FlashProgDpFirstCmd
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
	ldr	r3, .L90
	ldr	r5, [r3, r0, asl #3]
	add	r3, r3, r0, asl #3
	mov	r0, #80
	ldrb	r4, [r3, #4]	@ zero_extendqisi2
	add	r3, r5, r4, asl #8
	add	r4, r4, #8
	str	r2, [r3, #2056]
	bl	ndelay
	ldr	r0, [r5, r4, asl #8]
	ldmfd	sp!, {r3, r4, r5, pc}
.L91:
	.align	2
.L90:
	.word	.LANCHOR0
	.fnend
	.size	FlashReadStatus, .-FlashReadStatus
	.align	2
	.global	js_hash
	.type	js_hash, %function
js_hash:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	@ link register save eliminated.
	ldr	r3, .L96
	add	r1, r0, r1
.L93:
	cmp	r0, r1
	beq	.L95
	mov	r2, r3, asl #5
	ldrb	ip, [r0], #1	@ zero_extendqisi2
	add	r2, r2, r3, lsr #2
	add	r2, r2, ip
	eor	r3, r3, r2
	b	.L93
.L95:
	mov	r0, r3
	bx	lr
.L97:
	.align	2
.L96:
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
	bne	.L100
	ldr	r3, .L143
.L101:
	mov	r2, r0, asl #1
	strh	r0, [r2, r3]	@ movhi
	add	r0, r0, #1
	cmp	r0, #512
	bne	.L101
.L105:
	mov	r2, #2048
	ldr	r0, .L143+4
	mov	r1, #255
	uxth	r4, r4
	bl	ftl_memset
	ldr	r2, .L143
	mov	r3, #0
	add	r0, r2, #1024
	b	.L102
.L100:
	cmp	r0, #1
	bne	.L103
	ldr	ip, .L143
	mov	r3, #0
.L104:
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
	bne	.L104
	b	.L105
.L103:
	cmp	r0, #2
	bne	.L106
	ldr	r1, .L143
	mov	r3, #0
.L107:
	uxth	r2, r3
	mov	r0, r3, asl #1
	cmp	r2, #1
	add	r3, r3, #1
	mov	ip, r2, asl #1
	subhi	r2, ip, #1
	cmp	r3, #512
	strh	r2, [r0, r1]	@ movhi
	bne	.L107
	b	.L105
.L106:
	cmp	r0, #3
	bne	.L108
	ldr	ip, .L143
	mov	r3, #0
.L109:
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
	bne	.L109
	b	.L105
.L108:
	cmp	r0, #4
	mov	r3, #0
	bne	.L110
	ldr	r2, .L143+8
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
.L111:
	tst	r3, #1
	movne	r1, #7
	moveq	r1, #6
	rsb	r1, r1, r3, asl #1
	add	r3, r3, #1
	strh	r1, [r2, #2]!	@ movhi
	uxth	r3, r3
	cmp	r3, #512
	bne	.L111
	b	.L105
.L110:
	cmp	r0, #5
	bne	.L112
	ldr	r2, .L143
.L113:
	mov	r1, r3, asl #1
	strh	r3, [r1, r2]	@ movhi
	add	r3, r3, #1
	cmp	r3, #16
	bne	.L113
	ldr	r2, .L143+12
.L114:
	strh	r3, [r2, #2]!	@ movhi
	add	r3, r3, #2
	uxth	r3, r3
	cmp	r3, #1008
	bne	.L114
	b	.L105
.L112:
	cmp	r0, #6
	bne	.L115
	ldr	r1, .L143+16
.L116:
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
	bne	.L116
	b	.L105
.L115:
	cmp	r0, #9
	bne	.L117
	ldr	r2, .L143+8
	movw	r1, #1021
	strh	r3, [r2, #80]	@ movhi
	mov	r3, #1
	strh	r3, [r2, #82]	@ movhi
	mov	r3, #2
	strh	r3, [r2, #84]!	@ movhi
	mov	r3, #3
.L118:
	strh	r3, [r2, #2]!	@ movhi
	add	r3, r3, #2
	uxth	r3, r3
	cmp	r3, r1
	bne	.L118
	b	.L105
.L117:
	cmp	r0, #10
	bne	.L119
	ldr	r2, .L143
.L120:
	mov	r1, r3, asl #1
	strh	r3, [r1, r2]	@ movhi
	add	r3, r3, #1
	cmp	r3, #63
	bne	.L120
	ldr	r2, .L143+20
	movw	r1, #961
.L121:
	strh	r3, [r2, #2]!	@ movhi
	add	r3, r3, #2
	uxth	r3, r3
	cmp	r3, r1
	bne	.L121
	b	.L105
.L119:
	cmp	r0, #11
	bne	.L122
	ldr	r2, .L143
	mov	r3, #0
.L123:
	mov	r1, r3, asl #1
	strh	r3, [r1, r2]	@ movhi
	add	r3, r3, #1
	cmp	r3, #8
	bne	.L123
	ldr	r1, .L143+24
.L124:
	tst	r3, #1
	movne	r2, #7
	moveq	r2, #6
	rsb	r2, r2, r3, asl #1
	add	r3, r3, #1
	strh	r2, [r1, #2]!	@ movhi
	uxth	r3, r3
	cmp	r3, #512
	bne	.L124
	b	.L105
.L122:
	cmp	r0, #12
	bne	.L105
	ldr	r3, .L143+8
	mov	r2, #0
	strh	r2, [r3, #80]	@ movhi
	mov	r2, #1
	strh	r2, [r3, #82]	@ movhi
	mov	r2, #2
	strh	r2, [r3, #84]	@ movhi
	mov	r2, #3
	strh	r2, [r3, #86]!	@ movhi
	mov	r2, #4
.L125:
	sub	r1, r2, #1
	add	r1, r1, r2, lsr #1
	add	r2, r2, #1
	strh	r1, [r3, #2]!	@ movhi
	uxth	r2, r2
	cmp	r2, #512
	bne	.L125
	b	.L105
.L102:
	uxth	r1, r3
	cmp	r1, r4
	bcs	.L142
	mov	r1, r3, asl #1
	add	r3, r3, #1
	ldrh	r1, [r1, r2]
	mov	ip, r1, asl #1
	strh	r1, [r0, ip]	@ movhi
	b	.L102
.L142:
	ldmfd	sp!, {r4, pc}
.L144:
	.align	2
.L143:
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
	.global	ToshibaSetRRPara
	.type	ToshibaSetRRPara, %function
ToshibaSetRRPara:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	stmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, lr}
	.save {r4, r5, r6, r7, r8, r9, r10, lr}
	add	r7, r1, r1, asl #2
	ldr	r8, .L156
	mov	r5, r0
	ldr	r10, .L156+4
	mov	r6, r1
	add	r9, r8, #256
	mov	r4, #0
.L147:
	ldrb	r3, [r10]	@ zero_extendqisi2
	cmp	r4, r3
	bcs	.L155
	mov	r3, #85
	str	r3, [r5, #8]
	ldrsb	r3, [r4, r9]
	mov	r0, #200
	str	r3, [r5, #4]
	bl	ndelay
	ldr	r3, .L156+8
	ldrb	r3, [r3]	@ zero_extendqisi2
	cmp	r3, #34
	addeq	r3, r4, r7
	addeq	r3, r9, r3
	beq	.L154
	cmp	r3, #35
	addne	r3, r8, r6
	addne	r3, r3, #400
	ldrnesb	r3, [r3]
	bne	.L153
	ldr	r3, .L156+12
	add	r2, r4, r7
	add	r3, r3, r2
.L154:
	ldrsb	r3, [r3, #5]
.L153:
	str	r3, [r5]
	add	r4, r4, #1
	b	.L147
.L155:
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, pc}
.L157:
	.align	2
.L156:
	.word	.LANCHOR1
	.word	g_maxRegNum
	.word	g_retryMode
	.word	.LANCHOR1+304
	.fnend
	.size	ToshibaSetRRPara, .-ToshibaSetRRPara
	.align	2
	.global	SamsungSetRRPara
	.type	SamsungSetRRPara, %function
SamsungSetRRPara:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	ldr	r3, .L163
	stmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, lr}
	.save {r4, r5, r6, r7, r8, r9, r10, lr}
	add	r1, r3, r1, asl #2
	ldr	r8, .L163+4
	mov	r4, #0
	add	r5, r1, #3
	mov	r6, r0
	mov	r7, r3
	mov	r9, #161
	mov	r10, r4
.L159:
	ldrb	r3, [r8]	@ zero_extendqisi2
	cmp	r4, r3
	bcs	.L162
	str	r9, [r6, #8]
	mov	r0, #300
	str	r10, [r6]
	ldrsb	r3, [r7, r4]
	add	r4, r4, #1
	str	r3, [r6]
	ldrsb	r3, [r5, #1]!
	str	r3, [r6]
	bl	ndelay
	b	.L159
.L162:
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, pc}
.L164:
	.align	2
.L163:
	.word	.LANCHOR1+408
	.word	g_maxRegNum
	.fnend
	.size	SamsungSetRRPara, .-SamsungSetRRPara
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
	ldr	r3, .L180
	ldr	r4, .L180+4
	ldr	r9, .L180+8
	ldrh	r0, [r3, #10]
	strb	r5, [r4, #3152]
	strb	r5, [r4, #3153]
	bl	FlashBlockAlignInit
	mov	r1, r5
	mov	r2, #8
	ldr	r0, .L180+12
	bl	ftl_memset
	mov	r1, r5
	mov	r2, #32
	ldr	r0, .L180+16
	bl	ftl_memset
	ldr	r0, .L180+20
	mov	r1, r5
	mov	r2, #128
	bl	ftl_memset
	ldr	r7, [r4, #44]
	add	r6, r7, #1
.L167:
	mov	r0, r6
	add	r1, r9, r5, asl #3
	ldrb	r2, [r7]	@ zero_extendqisi2
	bl	FlashMemCmp8
	ldr	r8, .L180+8
	cmp	r0, #0
	bne	.L166
	ldrb	r3, [r4, #3152]	@ zero_extendqisi2
	add	r2, r4, r3, asl #2
	str	r0, [r2, #3164]
	add	r2, r3, #1
	add	r3, r4, r3
	strb	r2, [r4, #3152]
	strb	r5, [r3, #3156]
.L166:
	add	r5, r5, #1
	cmp	r5, #4
	bne	.L167
	ldrb	r3, [r4, #3152]	@ zero_extendqisi2
	strb	r3, [r4, #3153]
	ldrb	r3, [r7, #8]	@ zero_extendqisi2
	cmp	r3, #2
	beq	.L168
.L172:
	ldrb	r3, [r7, #13]	@ zero_extendqisi2
	ldrb	r2, [r4, #3152]	@ zero_extendqisi2
	smulbb	r2, r2, r3
	ldrh	r3, [r7, #14]
	smulbb	r3, r2, r3
	movw	r2, #3324
	strh	r3, [r4, r2]	@ movhi
	ldmfd	sp!, {r3, r4, r5, r6, r7, r8, r9, pc}
.L168:
	ldr	r3, .L180+4
	mov	r5, #0
	ldr	r9, [r3, #36]
.L171:
	mov	r0, r6
	add	r1, r8, r5, asl #3
	ldrb	r2, [r7]	@ zero_extendqisi2
	bl	FlashMemCmp8
	cmp	r0, #0
	bne	.L169
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
.L169:
	add	r5, r5, #1
	cmp	r5, #4
	bne	.L171
	b	.L172
.L181:
	.align	2
.L180:
	.word	.LANCHOR1+472
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
	ldr	r1, .L184
	bl	ftl_memcpy
	mov	r0, #0
	ldmfd	sp!, {r3, pc}
.L185:
	.align	2
.L184:
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
	mov	r4, #0
	ldr	r8, .L199
.L190:
	mov	r7, r4, asl #5
	ldr	r1, .L199+4
	add	r0, r7, #1
	ldrb	r2, [r8, r4, asl #5]	@ zero_extendqisi2
	add	r0, r8, r0
	ldr	r6, .L199+8
	bl	FlashMemCmp8
	add	r9, r6, #504
	subs	r5, r0, #0
	bne	.L187
	adds	r9, r9, r7
	beq	.L193
	add	r4, r6, r7
	mov	r3, r5
	ldr	r1, .L199+12
	ldrb	r2, [r4, #526]	@ zero_extendqisi2
	b	.L192
.L187:
	add	r4, r4, #1
	cmp	r4, #86
	bne	.L190
	b	.L193
.L198:
	add	r3, r3, #1
	cmp	r3, #4
	beq	.L191
.L192:
	ldrb	r0, [r1, r3, asl #5]	@ zero_extendqisi2
	cmp	r0, r2
	bne	.L198
.L191:
	ldr	r4, .L199+16
	mov	r2, #32
	ldr	r1, .L199+12
	ldr	r0, .L199+20
	add	r1, r1, r3, asl #5
	bl	ftl_memcpy
	mov	r0, r4
	mov	r1, r9
	mov	r2, #32
	bl	ftl_memcpy
	ldrh	r0, [r4, #10]
	bl	FlashBlockAlignInit
	b	.L188
.L193:
	mvn	r5, #0
.L188:
	mov	r0, r5
	ldmfd	sp!, {r3, r4, r5, r6, r7, r8, r9, pc}
.L200:
	.align	2
.L199:
	.word	.LANCHOR1+504
	.word	IDByte
	.word	.LANCHOR1
	.word	.LANCHOR1+3256
	.word	.LANCHOR1+472
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
	ldr	r3, .L202
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
.L203:
	.align	2
.L202:
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
	ldr	r3, .L210
	mov	fp, r1
	ldr	r7, .L210+4
	mov	r5, r0
	mov	r8, r2
	ldrh	r4, [r3, #12]
	ldrh	r3, [r3, #14]
	ldrh	r10, [r7, #36]
	smulbb	r4, r4, r3
	ldrb	r3, [r7, #32]	@ zero_extendqisi2
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
	bne	.L206
	ldr	r3, .L210+8
	ldrb	r3, [r3, #-2744]	@ zero_extendqisi2
	cmp	r3, #0
	addeq	r1, r7, r1, asl #1
	ldreqh	r1, [r1, #80]
.L206:
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
.L211:
	.align	2
.L210:
	.word	.LANCHOR2-2772
	.word	.LANCHOR0
	.word	.LANCHOR2
	.fnend
	.size	LogAddr2PhyAddr, .-LogAddr2PhyAddr
	.align	2
	.global	FlashReadStatusEN
	.type	FlashReadStatusEN, %function
FlashReadStatusEN:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	ldr	ip, .L223
	stmfd	sp!, {r3, r4, r5, lr}
	.save {r3, r4, r5, lr}
	ldr	r4, [ip, r0, asl #3]
	add	r0, ip, r0, asl #3
	ldr	r3, [ip, #44]
	ldrb	r5, [r0, #4]	@ zero_extendqisi2
	ldrb	r3, [r3, #8]	@ zero_extendqisi2
	cmp	r3, #2
	mov	r3, r5, asl #8
	addne	r3, r4, r3
	add	r5, r5, #8
	movne	r2, #112
	strne	r2, [r3, #2056]
	bne	.L217
	cmp	r2, #0
	add	r3, r4, r3
	ldrneb	r2, [ip, #62]	@ zero_extendqisi2
	ldreqb	r2, [ip, #61]	@ zero_extendqisi2
	str	r2, [r3, #2056]
	ldrb	r0, [ip, #63]	@ zero_extendqisi2
	cmp	r0, #0
	addne	ip, r4, r5, asl #8
	movne	r2, #0
	beq	.L217
.L216:
	cmp	r2, r0
	bcs	.L217
	mov	r3, r2, asl #3
	add	r2, r2, #1
	mov	r3, r1, lsr r3
	uxtb	r3, r3
	str	r3, [ip, #4]
	b	.L216
.L217:
	mov	r0, #80
	bl	ndelay
	ldr	r0, [r4, r5, asl #8]
	uxtb	r0, r0
	ldmfd	sp!, {r3, r4, r5, pc}
.L224:
	.align	2
.L223:
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
.L226:
	mov	r0, r4
	mov	r1, r5
	mov	r2, r6
	bl	FlashReadStatusEN
	cmp	r0, #255
	beq	.L226
	tst	r0, #64
	ldmnefd	sp!, {r4, r5, r6, pc}
	mov	r0, #1
	mov	r1, #3
	bl	usleep_range
	b	.L226
	.fnend
	.size	FlashWaitReadyEN, .-FlashWaitReadyEN
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
	ldr	r2, .L237
	ldr	r0, .L237+4
	mov	ip, #1
	ldr	r3, [r2, #44]
	ldrb	r1, [r3, #9]	@ zero_extendqisi2
	ldr	r3, [r2, #36]
	smulbb	r3, r1, r3
	strh	r3, [r4, #4]	@ unaligned
	ldr	r3, .L237+8
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
.L233:
	uxtb	r2, r3
	cmp	r2, r1
	bcs	.L236
	ldrb	lr, [r3, r0]	@ zero_extendqisi2
	add	r3, r3, #1
	ldrb	r2, [r4, #10]	@ zero_extendqisi2
	orr	r2, r2, ip, asl lr
	strb	r2, [r4, #10]
	b	.L233
.L236:
	ldmfd	sp!, {r4, pc}
.L238:
	.align	2
.L237:
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
	ldr	r3, .L240
	ldr	r2, [r3, #-2736]
	str	r0, [r3, #-2736]
	mov	r0, r2
	bx	lr
.L241:
	.align	2
.L240:
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
	ldr	r3, .L243
	ldr	r3, [r3, #44]
	ldrb	r0, [r3, #9]	@ zero_extendqisi2
	bx	lr
.L244:
	.align	2
.L243:
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
	ldr	r2, .L247
	add	r3, r2, r0, asl #3
	ldr	r0, [r2, r0, asl #3]
	ldrb	r3, [r3, #4]	@ zero_extendqisi2
	add	r3, r3, #8
	add	r0, r0, r3, asl #8
	bx	lr
.L248:
	.align	2
.L247:
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
	ldr	r3, .L250
	ldr	r2, [r3, #-2808]
	mov	r3, r0, asl #8
	orr	r0, r3, r0, asl #16
	orr	r3, r0, #1
	str	r3, [r2, #304]
	bx	lr
.L251:
	.align	2
.L250:
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
	ldr	r3, .L253
	orr	r0, r0, #16640
	ldr	r3, [r3, #-2808]
	str	r0, [r3, #344]
	bx	lr
.L254:
	.align	2
.L253:
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
	ldr	r3, .L258
	cmp	r0, #0
	ldr	r2, [r3, #-2808]
	ldr	r3, [r2]
	bfieq	r3, r0, #13, #1
	orrne	r3, r3, #253952
	str	r3, [r2]
	bx	lr
.L259:
	.align	2
.L258:
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
	ldr	r3, .L267
	ands	r1, r0, #6
	ldr	r2, [r3, #-2808]
	ldr	r3, [r2]
	bfieq	r3, r1, #13, #1
	beq	.L263
	orr	r3, r3, #24576
	movw	r1, #8322
	bfc	r3, #15, #1
	str	r1, [r2, #344]
	orr	r3, r3, #196608
	ldr	r1, .L267+4
	tst	r0, #4
	orrne	r3, r3, #32768
	str	r1, [r2, #304]
	mov	r1, #38
	str	r1, [r2, #308]
	mov	r1, #39
	str	r1, [r2, #308]
.L263:
	str	r3, [r2]
	mov	r0, #0
	bx	lr
.L268:
	.align	2
.L267:
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
	ldr	r3, .L270
	mov	r2, #1
	ldr	r1, [r3, r0, asl #3]
	add	r0, r3, r0, asl #3
	ldrb	r0, [r0, #4]	@ zero_extendqisi2
	ldr	r3, [r1]
	mov	r2, r2, asl r0
	bfi	r3, r2, #0, #8
	str	r3, [r1]
	bx	lr
.L271:
	.align	2
.L270:
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
	ldr	r3, .L273
	ldr	r2, [r3, r0, asl #3]
	ldr	r3, [r2]
	bfc	r3, #0, #8
	bfc	r3, #17, #1
	str	r3, [r2]
	bx	lr
.L274:
	.align	2
.L273:
	.word	.LANCHOR0
	.fnend
	.size	NandcFlashDeCs, .-NandcFlashDeCs
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
	mov	r6, r0
	ldr	r0, .L284
	mov	r7, r3
	mov	r9, r1
	mov	r10, r2
	ldr	r3, [r0, #44]
	ldrb	r4, [r3, #19]	@ zero_extendqisi2
	mov	r3, r6, asl #3
	cmp	r4, #6
	ldreq	r4, .L284+4
	addeq	r4, r4, r6, asl #6
	addeq	r4, r4, #20
	addeq	r4, r4, r7, asl #2
	beq	.L277
.L276:
	cmp	r4, #7
	bne	.L278
	ldr	r2, .L284+4
	mov	ip, #160
	add	r4, r7, r7, asl #2
	mla	ip, ip, r6, r2
	add	ip, ip, #28
	add	r4, ip, r4, asl #1
	b	.L277
.L278:
	cmp	r4, #8
	addne	r4, r7, r3
	addeq	r2, r7, r7, asl #2
	ldrne	r2, .L284+4
	ldreq	r4, .L284+8
	addne	r4, r2, r4, asl #3
	addeq	r4, r4, r2
	addne	r4, r4, #20
.L277:
	ldr	r8, [r0, r6, asl #3]
	add	r0, r0, r3
	sub	r9, r9, #1
	sub	r10, r10, #1
	ldrb	fp, [r0, #4]	@ zero_extendqisi2
	mov	r0, r6
	bl	NandcFlashCs
	sub	ip, r4, #1
	add	r4, r4, r9
	mov	r3, #54
	mov	fp, fp, asl #8
	add	r5, r8, fp
	str	r3, [r5, #2056]
.L280:
	cmp	ip, r4
	beq	.L283
	ldrb	r3, [r10, #1]!	@ zero_extendqisi2
	mov	r0, #200
	str	ip, [sp, #4]
	str	r3, [r5, #2052]
	bl	ndelay
	ldr	ip, [sp, #4]
	ldrsb	r3, [ip, #1]!
	str	r3, [r5, #2048]
	b	.L280
.L283:
	add	r8, r8, fp
	mov	r3, #22
	mov	r0, r6
	str	r3, [r8, #2056]
	bl	NandcFlashDeCs
	ldr	r3, .L284+12
	add	r6, r3, r6
	strb	r7, [r6, #-1880]
	add	sp, sp, #12
	@ sp needed
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L285:
	.align	2
.L284:
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
	ldr	r3, .L294
	ldr	r3, [r3, #44]
	ldrb	r3, [r3, #19]	@ zero_extendqisi2
	sub	r3, r3, #1
	cmp	r3, #7
	bxhi	lr
	stmfd	sp!, {r4, r5, r6, lr}
	.save {r4, r5, r6, lr}
	mov	r4, #0
	ldr	r5, .L294+4
	ldr	r6, .L294+8
.L287:
	ldrb	r3, [r5, r4, asl #3]	@ zero_extendqisi2
	uxtb	r0, r4
	cmp	r3, #173
	bne	.L288
	ldrb	r1, [r6, #-2731]	@ zero_extendqisi2
	mov	r3, #0
	ldr	r2, .L294+12
	bl	HynixSetRRPara
.L288:
	add	r4, r4, #1
	cmp	r4, #4
	bne	.L287
	ldmfd	sp!, {r4, r5, r6, pc}
.L295:
	.align	2
.L294:
	.word	.LANCHOR0
	.word	IDByte
	.word	.LANCHOR2
	.word	.LANCHOR2-2728
	.fnend
	.size	FlashSetReadRetryDefault, .-FlashSetReadRetryDefault
	.align	2
	.global	FlashWaitCmdDone
	.type	FlashWaitCmdDone, %function
FlashWaitCmdDone:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	stmfd	sp!, {r3, r4, r5, r6, r7, lr}
	.save {r3, r4, r5, r6, r7, lr}
	ldr	r5, .L304
	add	r4, r5, r0, asl #4
	ldr	r3, [r4, #3204]
	ldrb	r7, [r4, #3196]	@ zero_extendqisi2
	cmp	r3, #0
	beq	.L298
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
.L298:
	mov	r0, #0
	ldmfd	sp!, {r3, r4, r5, r6, r7, pc}
.L305:
	.align	2
.L304:
	.word	.LANCHOR0
	.fnend
	.size	FlashWaitCmdDone, .-FlashWaitCmdDone
	.align	2
	.global	NandcDelayns
	.type	NandcDelayns, %function
NandcDelayns:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	stmfd	sp!, {r3, lr}
	.save {r3, lr}
	bl	ndelay
	mov	r0, #0
	ldmfd	sp!, {r3, pc}
	.fnend
	.size	NandcDelayns, .-NandcDelayns
	.align	2
	.global	NandcWaitFlashReadyNoDelay
	.type	NandcWaitFlashReadyNoDelay, %function
NandcWaitFlashReadyNoDelay:
	.fnstart
	@ args = 0, pretend = 0, frame = 8
	@ frame_needed = 0, uses_anonymous_args = 0
	ldr	r3, .L314
	stmfd	sp!, {r0, r1, r2, r4, r5, lr}
	.save {r4, r5, lr}
	.pad #12
	ldr	r4, .L314+4
	ldr	r5, [r3, r0, asl #3]
.L310:
	ldr	r3, [r5]
	str	r3, [sp, #4]
	ldr	r3, [sp, #4]
	tst	r3, #512
	bne	.L311
	mov	r0, #10
	bl	ndelay
	subs	r4, r4, #1
	bne	.L310
	mvn	r0, #0
	b	.L309
.L311:
	mov	r0, #0
.L309:
	add	sp, sp, #12
	@ sp needed
	ldmfd	sp!, {r4, r5, pc}
.L315:
	.align	2
.L314:
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
	ldr	r3, .L322
	stmfd	sp!, {r0, r1, r2, r4, r5, lr}
	.save {r4, r5, lr}
	.pad #12
	ldr	r5, [r3, r0, asl #3]
	mov	r0, #130
	bl	ndelay
	ldr	r4, .L322+4
.L318:
	ldr	r3, [r5]
	str	r3, [sp, #4]
	ldr	r3, [sp, #4]
	tst	r3, #512
	bne	.L319
	mov	r0, #1
	mov	r1, #2
	bl	usleep_range
	subs	r4, r4, #1
	bne	.L318
	mvn	r0, #0
	b	.L317
.L319:
	mov	r0, #0
.L317:
	add	sp, sp, #12
	@ sp needed
	ldmfd	sp!, {r4, r5, pc}
.L323:
	.align	2
.L322:
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
	ldr	r3, .L326
	stmfd	sp!, {r4, r5, r6, lr}
	.save {r4, r5, r6, lr}
	mov	r4, r0
	ldr	r5, [r3, r0, asl #3]
	add	r3, r3, r0, asl #3
	ldrb	r6, [r3, #4]	@ zero_extendqisi2
	bl	NandcFlashCs
	mov	r3, #255
	mov	r0, r4
	add	r5, r5, r6, asl #8
	str	r3, [r5, #2056]
	bl	NandcWaitFlashReady
	mov	r0, r4
	ldmfd	sp!, {r4, r5, r6, lr}
	b	NandcFlashDeCs
.L327:
	.align	2
.L326:
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
	ldr	r6, .L335
	ldrb	r3, [r6, #-2744]	@ zero_extendqisi2
	cmp	r3, #0
	ldmeqfd	sp!, {r4, r5, r6, r7, r8, pc}
	mov	r5, r0
	bl	NandcFlashCs
	ldr	r3, .L335+4
	ldr	r7, [r3, r5, asl #3]
	add	r3, r3, r5, asl #3
	ldrb	r8, [r3, #4]	@ zero_extendqisi2
	ldr	r3, .L335+8
	mov	r8, r8, asl #8
	ldrb	r3, [r3, r5, asl #3]	@ zero_extendqisi2
	cmp	r3, #44
	bne	.L330
	add	r4, r7, r8
	mov	r3, #239
	mov	r0, #50
	str	r3, [r4, #2056]
	mov	r3, #145
	str	r3, [r4, #2052]
	bl	ndelay
	mov	r3, #0
	mov	r2, #1
	str	r3, [r4, #2048]
	mov	r0, #100
	str	r2, [r4, #2048]
	str	r3, [r4, #2048]
	str	r3, [r4, #2048]
	bl	ndelay
.L330:
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
.L336:
	.align	2
.L335:
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
	ldr	r6, .L344
	ldrb	r3, [r6, #-2744]	@ zero_extendqisi2
	cmp	r3, #0
	ldmeqfd	sp!, {r4, r5, r6, r7, r8, pc}
	mov	r5, r0
	bl	NandcFlashCs
	ldr	r3, .L344+4
	ldr	r7, [r3, r5, asl #3]
	add	r3, r3, r5, asl #3
	ldrb	r8, [r3, #4]	@ zero_extendqisi2
	ldr	r3, .L344+8
	mov	r8, r8, asl #8
	ldrb	r3, [r3, r5, asl #3]	@ zero_extendqisi2
	cmp	r3, #44
	bne	.L339
	add	r4, r7, r8
	mov	r3, #239
	mov	r0, #50
	str	r3, [r4, #2056]
	mov	r3, #145
	str	r3, [r4, #2052]
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
.L339:
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
.L345:
	.align	2
.L344:
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
	ldr	r3, .L371
	stmfd	sp!, {r0, r1, r2, r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.save {r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.pad #12
	mov	lr, #0
	ldrb	r2, [r3, #-1875]	@ zero_extendqisi2
	mov	r4, #239
	ldr	r7, .L371+4
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
.L358:
	ldr	r3, .L371+8
	ldrb	ip, [lr, r7]	@ zero_extendqisi2
	ldr	r1, [r3, lr]!
	cmp	ip, #69
	cmpne	ip, #152
	ldrb	r3, [r3, #4]	@ zero_extendqisi2
	beq	.L349
	cmp	ip, #44
	cmpne	ip, #173
	bne	.L350
.L349:
	cmp	r0, #1
	bne	.L351
	ldr	fp, [sp, #4]
	cmp	fp, #0
	beq	.L350
	mov	r3, r3, asl #8
	cmp	ip, #173
	add	fp, r1, r3
	str	r4, [fp, #2056]
	streq	r0, [fp, #2052]
	beq	.L370
	cmp	ip, #44
	streq	r0, [fp, #2052]
	strne	r5, [fp, #2052]
	streq	r10, [fp, #2048]
	strne	r0, [fp, #2048]
	b	.L356
.L351:
	ldr	fp, [sp]
	cmp	fp, #0
	beq	.L350
	mov	r3, r3, asl #8
	cmp	ip, #173
	add	fp, r1, r3
	str	r4, [fp, #2056]
	streq	r6, [fp, #2052]
	streq	r9, [fp, #2048]
	beq	.L356
	cmp	ip, #44
	streq	r6, [fp, #2052]
	streq	r8, [fp, #2048]
	beq	.L356
	str	r5, [fp, #2052]
.L370:
	str	r2, [fp, #2048]
.L356:
	add	r3, r1, r3
	str	r2, [r3, #2048]
	str	r2, [r3, #2048]
	str	r2, [r3, #2048]
.L350:
	add	lr, lr, #8
	cmp	lr, #32
	bne	.L358
	mov	r0, #0
	bl	NandcWaitFlashReady
	mov	r0, #0
	add	sp, sp, #12
	@ sp needed
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L372:
	.align	2
.L371:
	.word	.LANCHOR2
	.word	IDByte
	.word	.LANCHOR0
	.fnend
	.size	FlashSetInterfaceMode, .-FlashSetInterfaceMode
	.align	2
	.global	FlashReadSpare
	.type	FlashReadSpare, %function
FlashReadSpare:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	ldr	ip, .L375
	stmfd	sp!, {r3, r4, r5, lr}
	.save {r3, r4, r5, lr}
	mov	r5, r2
	ldr	r3, .L375+4
	ldr	r4, [ip, r0, asl #3]
	add	ip, ip, r0, asl #3
	ldrb	r3, [r3, #481]	@ zero_extendqisi2
	ldrb	r2, [ip, #4]	@ zero_extendqisi2
	mov	r3, r3, asl #9
	add	r4, r4, r2, asl #8
	mov	r2, #0
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
.L376:
	.align	2
.L375:
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
	ldr	r2, .L379
	stmfd	sp!, {r4, lr}
	.save {r4, lr}
	ldr	r3, [r2, r0, asl #3]
	add	r2, r2, r0, asl #3
	ldrb	r4, [r2, #4]	@ zero_extendqisi2
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
	bl	ndelay
	ldr	r0, [r4, #2048]
	and	r0, r0, #1
	ldmfd	sp!, {r4, pc}
.L380:
	.align	2
.L379:
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
	bl	ndelay
	ldr	r1, .L389
	ldr	r0, .L389+4
	add	r4, r4, r4, asl #2
	ldr	ip, .L389+8
	sub	lr, r1, #48
	mov	r2, #0
.L382:
	ldrb	r3, [r0]	@ zero_extendqisi2
	cmp	r2, r3
	bcs	.L388
	ldrb	r3, [ip]	@ zero_extendqisi2
	cmp	r3, #67
	add	r3, r2, r4
	addeq	r3, lr, r3
	addne	r3, r1, r3
	add	r2, r2, #1
	ldrsb	r3, [r3, #5]
	str	r3, [r5]
	b	.L382
.L388:
	mov	r0, #0
	ldmfd	sp!, {r3, r4, r5, lr}
	b	NandcWaitFlashReady
.L390:
	.align	2
.L389:
	.word	.LANCHOR1+304
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
	ldr	r2, .L393
	mov	r0, #200
	ldr	r3, [r2, r4, asl #3]
	add	r2, r2, r4, asl #3
	ldrb	r4, [r2, #4]	@ zero_extendqisi2
	add	r4, r3, r4, asl #8
	mov	r3, #239
	str	r3, [r4, #2056]
	mov	r3, #150
	str	r3, [r4, #2052]
	bl	ndelay
	str	r5, [r4, #2048]
	mov	r3, #0
	str	r3, [r4, #2048]
	str	r3, [r4, #2048]
	str	r3, [r4, #2048]
	ldmfd	sp!, {r3, r4, r5, pc}
.L394:
	.align	2
.L393:
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
	ldr	r8, .L407
	.pad #20
	sub	sp, sp, #20
	mov	r6, r0
	mov	r9, r1
	mov	r7, r5
.L396:
	cmp	r7, r9
	beq	.L406
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
	bcs	.L398
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
	ldr	r1, [r8, #36]
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
	bne	.L400
	ldr	r0, .L407+4
	ldr	r1, [sp, #8]
	bl	printk
.L400:
	mov	r0, r4
	bl	NandcFlashDeCs
.L398:
	add	r7, r7, #1
	add	r5, r5, #36
	b	.L396
.L406:
	mov	r0, #0
	add	sp, sp, #20
	@ sp needed
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, pc}
.L408:
	.align	2
.L407:
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
	ldr	r4, .L444
	.pad #28
	sub	sp, sp, #28
	ldrb	r5, [r4, #32]	@ zero_extendqisi2
	cmp	r5, #0
	beq	.L427
	mov	r1, r2
	bl	FlashEraseSLc2KBlocks
	b	.L411
.L427:
	ldr	fp, .L444+4
	mov	r9, r0
	mov	r10, r1
.L410:
	cmp	r5, r8
	bcs	.L442
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
	bcs	.L413
	ldrb	r3, [fp, #-1874]	@ zero_extendqisi2
	cmp	r3, #0
	add	r3, r4, r0, asl #4
	ldr	r3, [r3, #3204]
	moveq	r7, #0
	cmp	r3, #0
	beq	.L415
	uxtb	r0, r0
	bl	FlashWaitCmdDone
.L415:
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
	bne	.L417
	ldr	r3, .L444+4
	ldrb	r3, [r3, #-2744]	@ zero_extendqisi2
	cmp	r3, #0
	beq	.L417
	bl	flash_enter_slc_mode
	b	.L418
.L417:
	bl	flash_exit_slc_mode
.L418:
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
.L413:
	add	r5, r5, #1
	b	.L410
.L442:
	ldr	r6, .L444+4
	mov	r5, #0
	ldr	r7, .L444+8
.L420:
	ldrb	r3, [r4, #3152]	@ zero_extendqisi2
	cmp	r5, r3
	bcs	.L443
	uxtb	r0, r5
	bl	FlashWaitCmdDone
	cmp	r10, #1
	bne	.L421
	ldrb	r3, [r6, #-2744]	@ zero_extendqisi2
	cmp	r3, #0
	beq	.L421
	ldrb	r0, [r7, r5, asl #4]	@ zero_extendqisi2
	bl	flash_exit_slc_mode
.L421:
	add	r5, r5, #1
	b	.L420
.L443:
	ldr	r3, .L444+4
	ldr	r3, [r3, #-1872]
	cmp	r3, #0
	bne	.L423
.L425:
	mov	r0, #0
	b	.L411
.L423:
	ldr	r3, .L444+12
	ldrb	r3, [r3]	@ zero_extendqisi2
	cmp	r3, #69
	bne	.L425
	mov	r3, #0
	mov	r2, #36
	mov	r1, r3
.L424:
	cmp	r3, r8
	beq	.L425
	mul	r0, r2, r3
	add	r3, r3, #1
	str	r1, [r9, r0]
	b	.L424
.L411:
	add	sp, sp, #28
	@ sp needed
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L445:
	.align	2
.L444:
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
	ldr	r3, .L555
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
	bne	.L447
	mvn	r2, #88
	strb	r2, [r3, #-2728]
	ldr	r3, .L555+4
	mvn	r2, #8
	mov	r5, #7
	strb	r2, [r3, #3401]
	b	.L508
.L447:
	cmp	r4, #3
	bne	.L449
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
	b	.L549
.L449:
	cmp	r4, #4
	bne	.L450
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
.L549:
	mov	r5, #8
	strb	r2, [r3, #-2721]
	mov	r6, r5
	b	.L448
.L450:
	cmp	r4, #5
	bne	.L451
	mov	r2, #56
	mov	r5, #8
	strb	r2, [r3, #-2728]
	mov	r2, #57
	strb	r2, [r3, #-2727]
	mov	r2, #58
	strb	r2, [r3, #-2726]
	mov	r2, #59
	strb	r2, [r3, #-2725]
	b	.L508
.L451:
	cmp	r4, #6
	bne	.L452
	mov	r2, #14
	mov	r5, #12
	strb	r2, [r3, #-2728]
	mov	r2, #15
	strb	r2, [r3, #-2727]
	mov	r2, #16
	strb	r2, [r3, #-2726]
	mov	r2, #17
	strb	r2, [r3, #-2725]
	b	.L508
.L452:
	cmp	r4, #7
	bne	.L453
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
	b	.L448
.L453:
	cmp	r4, #8
	mov	r5, #7
	bne	.L508
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
	b	.L448
.L508:
	mov	r6, #4
.L448:
	sub	r3, r4, #1
	cmp	r3, #1
	bhi	.L545
	ldr	r10, .L555+8
	mov	r9, #0
.L454:
	ldr	r3, .L555+12
	uxtb	r2, r9
	ldrb	r1, [r3, #3152]	@ zero_extendqisi2
	cmp	r1, r2
	bls	.L461
	add	r2, r3, r2
	ldr	r8, .L555+16
	ldrb	r2, [r2, #3156]	@ zero_extendqisi2
	ldr	r1, [r3, r2, asl #3]
	add	r3, r3, r2, asl #3
	add	r4, r8, r2, asl #6
	add	r8, r8, #3
	ldrb	r7, [r3, #4]	@ zero_extendqisi2
	add	r4, r4, #20
	mov	fp, r4
	mov	r2, r4
	add	r7, r1, r7, asl #8
	mov	r1, #55
.L456:
	str	r1, [r7, #2056]
	mov	r0, #80
	ldrb	r3, [r8, #1]!	@ zero_extendqisi2
	str	r2, [sp, #4]
	str	r1, [sp]
	str	r3, [r7, #2052]
	bl	ndelay
	ldr	r3, [r7, #2048]
	ldr	r2, [sp, #4]
	ldr	r1, [sp]
	strb	r3, [r2], #1
	rsb	r3, r4, r2
	uxtb	r3, r3
	cmp	r3, r6
	bcc	.L456
	mov	r2, #0
.L457:
	add	r0, r10, r2
	mov	r3, #1
.L458:
	ldrb	r1, [r0, r3, asl #2]	@ zero_extendqisi2
	ldrb	ip, [fp]	@ zero_extendqisi2
	add	r1, r1, ip
	strb	r1, [fp, r3, asl #3]
	add	r3, r3, #1
	cmp	r3, #7
	bne	.L458
	add	r2, r2, #1
	add	fp, fp, #1
	cmp	r2, #4
	bne	.L457
	mov	r3, #0
	add	r9, r9, #1
	strb	r3, [r4, #16]
	strb	r3, [r4, #24]
	strb	r3, [r4, #32]
	strb	r3, [r4, #40]
	strb	r3, [r4, #48]
	strb	r3, [r4, #41]
	strb	r3, [r4, #49]
	b	.L454
.L545:
	sub	r3, r4, #3
	cmp	r3, #5
	bhi	.L461
	smulbb	r8, r6, r5
	ldr	fp, .L555
	mov	r3, r8, asl #4
	mov	r8, r8, asr #1
	str	r3, [sp, #36]
	mov	r3, r8, asl #1
	str	r3, [sp, #4]
	mov	r3, #0
	str	r3, [sp, #16]
.L462:
	ldrb	r3, [sp, #16]	@ zero_extendqisi2
	str	r3, [sp, #12]
	ldr	r3, .L555+12
	ldr	r2, [sp, #12]
	ldrb	r3, [r3, #3152]	@ zero_extendqisi2
	cmp	r3, r2
	bhi	.L506
.L461:
	ldr	r3, .L555
	strb	r6, [r3, #-2731]
	strb	r5, [r3, #-2730]
	add	sp, sp, #44
	@ sp needed
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L506:
	ldr	r2, [sp, #12]
	ldr	r3, .L555+12
	add	r3, r3, r2
	ldrb	r10, [r3, #3156]	@ zero_extendqisi2
	ldr	r3, .L555+12
	mov	r0, r10
	ldr	r8, [r3, r10, asl #3]
	add	r3, r3, r10, asl #3
	ldrb	r3, [r3, #4]	@ zero_extendqisi2
	mov	r9, r3, asl #8
	str	r3, [sp, #8]
	add	r7, r8, r9
	mov	r3, #255
	str	r3, [r7, #2056]
	bl	NandcWaitFlashReady
	cmp	r4, #7
	ldreq	r7, .L555+16
	moveq	r3, #160
	mlaeq	r7, r3, r10, r7
	addeq	r3, r7, #28
	beq	.L550
.L463:
	cmp	r4, #8
	beq	.L465
	ldr	r7, .L555+16
	add	r7, r7, r10, asl #6
	add	r3, r7, #20
.L550:
	str	r3, [sp, #20]
	cmp	r4, #4
	add	r3, r8, r9
	mov	r2, #54
	str	r2, [r3, #2056]
	bne	.L466
	mov	r2, #255
	str	r2, [r3, #2052]
	mov	r2, #64
	str	r2, [r3, #2048]
	mov	r2, #204
	b	.L551
.L466:
	sub	r2, r4, #5
	cmp	r2, #1
	ldrlsb	r2, [fp, #-2728]	@ zero_extendqisi2
	strls	r2, [r3, #2052]
	movls	r2, #82
	bls	.L552
	cmp	r4, #7
	bne	.L467
	mov	r2, #174
	str	r2, [r3, #2052]
	mov	r2, #0
	str	r2, [r3, #2048]
	mov	r2, #176
.L551:
	str	r2, [r3, #2052]
	mov	r2, #77
.L552:
	str	r2, [r3, #2048]
.L467:
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
.L507:
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
	bne	.L471
	cmp	r4, #7
	movne	ip, #2
	moveq	ip, #32
.L471:
	ldr	r2, [sp, #8]
	ldr	r3, [fp, #-1868]
	add	r1, r8, r2, asl #8
	mov	r0, r3
.L472:
	ldr	r2, [r1, #2048]
	strb	r2, [r0], #1
	rsb	r2, r3, r0
	uxtb	r2, r2
	cmp	r2, ip
	bcc	.L472
	ldr	r2, [sp]
	cmp	r2, #0
	beq	.L473
	mov	r2, #0
.L475:
	ldrb	ip, [r3, r2, asl #2]	@ zero_extendqisi2
	uxtb	r0, r2
	cmp	ip, #50
	beq	.L474
	add	ip, r3, r2, asl #2
	ldrb	ip, [ip, #1]	@ zero_extendqisi2
	cmp	ip, #5
	beq	.L474
	add	r2, r2, #1
	cmp	r2, #8
	bne	.L475
	b	.L476
.L474:
	cmp	r0, #6
	bls	.L477
.L476:
	ldr	r0, .L555+20
	mov	r1, #0
	bl	printk
.L478:
	b	.L478
.L473:
	cmp	r4, #7
	bne	.L479
	ldr	r2, [sp]
.L481:
	ldrb	ip, [r3, r2, asl #2]	@ zero_extendqisi2
	uxtb	r0, r2
	cmp	ip, #12
	beq	.L480
	add	ip, r3, r2, asl #2
	ldrb	ip, [ip, #1]	@ zero_extendqisi2
	cmp	ip, #10
	beq	.L480
	add	r2, r2, #1
	cmp	r2, #8
	bne	.L481
	b	.L482
.L480:
	cmp	r0, #7
	bne	.L477
.L482:
	ldr	r0, .L555+20
	mov	r1, #0
	bl	printk
.L483:
	b	.L483
.L479:
	cmp	r4, #6
	bne	.L477
	sub	r2, r3, #1
	add	r3, r3, #7
.L484:
	ldrb	r0, [r2, #1]!	@ zero_extendqisi2
	cmp	r0, #12
	beq	.L477
	ldrb	r0, [r2, #8]	@ zero_extendqisi2
	cmp	r0, #4
	beq	.L477
	cmp	r2, r3
	bne	.L484
	ldr	r0, .L555+20
	mov	r1, #0
	bl	printk
.L486:
	b	.L486
.L477:
	ldr	r2, [fp, #-1868]
	ldr	r0, [sp, #36]
	add	r0, r2, r0
	mov	r3, r2
.L487:
	cmp	r3, r0
	ldrne	ip, [r1, #2048]
	strneb	ip, [r3], #1
	bne	.L487
.L554:
	ldr	r3, .L555
	mov	r0, #8
	ldr	r1, [sp, #4]
	ldr	r3, [r3, #-1868]
	add	r1, r3, r1
	str	r1, [sp, #28]
.L490:
	ldr	ip, [sp, #4]
	add	lr, r1, ip
.L489:
	ldrh	ip, [r1]
	mvn	ip, ip
	strh	ip, [r1], #2	@ movhi
	cmp	r1, lr
	bne	.L489
	ldr	ip, [sp, #4]
	subs	r0, r0, #1
	add	r1, r1, ip
	bne	.L490
.L491:
	mov	ip, #0
	mov	r0, ip
.L494:
	mov	r1, #1
	mov	lr, #0
	mov	r1, r1, asl r0
	mov	r7, #16
	str	r7, [sp, #32]
	mov	r7, lr
.L492:
	ldrh	r9, [r3, lr]
	and	r9, r9, r1
	cmp	r9, r1
	addeq	r7, r7, #1
	ldr	r9, [sp, #4]
	add	lr, lr, r9
	ldr	r9, [sp, #32]
	subs	r9, r9, #1
	str	r9, [sp, #32]
	bne	.L492
	cmp	r7, #8
	add	r0, r0, #1
	orrhi	ip, ip, r1
	uxthhi	ip, ip
	cmp	r0, #16
	bne	.L494
	ldr	r1, [sp, #28]
	strh	ip, [r3], #2	@ movhi
	cmp	r3, r1
	bne	.L491
	ldr	r1, [fp, #-1868]
	mov	r3, #0
	sub	r0, r1, #4
	add	ip, r1, #28
.L496:
	ldr	lr, [r0, #4]!
	cmp	lr, #0
	addeq	r3, r3, #1
	cmp	r0, ip
	bne	.L496
	cmp	r3, #7
	ble	.L497
	ldr	r0, .L555+24
	mov	r2, #1
	mov	r3, #1024
	bl	rknand_print_hex
	ldr	r0, .L555+20
	mov	r1, #0
	bl	printk
.L498:
	b	.L498
.L497:
	cmp	r4, #6
	moveq	r0, #4
	beq	.L499
	cmp	r4, #7
	moveq	r0, #10
	beq	.L499
	ldr	r3, [sp]
	cmp	r3, #0
	moveq	r0, #8
	movne	r0, #5
.L499:
	sub	r9, r6, #1
	ldr	r1, [sp, #20]
	mov	ip, #0
	uxtb	r9, r9
	add	r9, r9, #1
.L500:
	mov	r7, r1
	mov	r3, r2
.L501:
	ldrb	lr, [r3], #1	@ zero_extendqisi2
	strb	lr, [r7], #1
	rsb	lr, r2, r3
	uxtb	lr, lr
	cmp	lr, r6
	bcc	.L501
	add	ip, ip, #1
	add	r2, r2, r9
	cmp	ip, r5
	add	r1, r1, r0
	blt	.L500
	ldr	r3, [sp, #8]
	mov	r0, r10
	add	r7, r8, r3, asl #8
	mov	r3, #255
	str	r3, [r7, #2056]
	bl	NandcWaitFlashReady
	ldr	r3, [sp, #24]
	cmp	r3, #0
	beq	.L503
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
	b	.L504
.L503:
	ldr	r3, [sp]
	cmp	r3, #0
	movne	r3, #190
	moveq	r3, #56
	str	r3, [r7, #2056]
.L504:
	mov	r0, r10
	bl	NandcWaitFlashReady
	ldr	r3, [sp, #16]
	add	r3, r3, #1
	str	r3, [sp, #16]
	b	.L462
.L465:
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
	ldr	r3, .L555+28
	str	r3, [sp, #20]
	b	.L507
.L556:
	.align	2
.L555:
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
	bhi	.L559
	b	HynixGetReadRetryDefault
.L559:
	cmp	r3, #49
	bne	.L560
	ldr	r2, .L571
	ldr	r0, .L571+4
	ldr	r1, .L571+8
	strb	r3, [r2, #-2732]
	mov	r3, #4
	strb	r3, [r2, #-2731]
	mov	r3, #15
	strb	r3, [r2, #-2730]
	mov	r2, #64
	b	.L569
.L560:
	sub	r2, r3, #65
	cmp	r3, #33
	cmpne	r2, #1
	ldrls	r2, .L571
	strlsb	r3, [r2, #-2732]
	movls	r3, #4
	bls	.L570
.L561:
	cmp	r3, #34
	cmpne	r3, #67
	bne	.L562
	ldr	r2, .L571
	strb	r3, [r2, #-2732]
	mov	r3, #5
.L570:
	strb	r3, [r2, #-2731]
	mov	r3, #7
	ldr	r0, .L571+4
	strb	r3, [r2, #-2730]
	mov	r2, #45
	ldr	r1, .L571+12
	b	.L569
.L562:
	cmp	r3, #35
	cmpne	r3, #68
	bxne	lr
	ldr	r2, .L571
	ldr	r0, .L571+4
	ldr	r1, .L571+16
	strb	r3, [r2, #-2732]
	mov	r3, #5
	strb	r3, [r2, #-2731]
	mov	r3, #17
	strb	r3, [r2, #-2730]
	mov	r2, #95
.L569:
	b	ftl_memcpy
.L572:
	.align	2
.L571:
	.word	.LANCHOR2
	.word	.LANCHOR2-2728
	.word	.LANCHOR1+408
	.word	.LANCHOR1+256
	.word	.LANCHOR1+304
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
	ldr	r0, .L579
	mov	r7, r1
	uxtb	r10, r2
	mov	r9, r2, lsr #8
	add	r3, r0, r8, asl #3
	mov	r6, r2, lsr #16
	ldrb	r1, [r0, #64]	@ zero_extendqisi2
	uxtb	lr, r7
	ldrb	r3, [r3, #4]	@ zero_extendqisi2
	mov	ip, r7, lsr #8
	ldr	r2, [r0, #44]
	cmp	r1, #1
	ldr	r4, [r0, r8, asl #3]
	mov	r1, r7, lsr #16
	mov	r3, r3, asl #8
	ldrb	r2, [r2, #7]	@ zero_extendqisi2
	bne	.L574
	cmp	r2, #1
	addeq	r2, r4, r3
	add	r4, r4, r3
	moveq	r5, #38
	streq	r5, [r2, #2056]
	mov	r5, #0
	ldrb	r3, [r0, #57]	@ zero_extendqisi2
	ldrb	r2, [r0, #56]	@ zero_extendqisi2
	mov	r0, r8
	str	r2, [r4, #2056]
	str	r5, [r4, #2052]
	str	r5, [r4, #2052]
	str	lr, [r4, #2052]
	str	ip, [r4, #2052]
	str	r1, [r4, #2052]
	str	r3, [r4, #2056]
	bl	NandcWaitFlashReady
	str	r5, [r4, #2056]
	mov	r3, #48
	str	r5, [r4, #2052]
	str	r5, [r4, #2052]
	str	r10, [r4, #2052]
	str	r9, [r4, #2052]
	str	r6, [r4, #2052]
	str	r3, [r4, #2056]
	b	.L576
.L574:
	cmp	r2, #1
	addeq	r2, r4, r3
	add	r3, r4, r3
	moveq	r5, #38
	streq	r5, [r2, #2056]
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
	str	r6, [r3, #2052]
	str	r2, [r3, #2056]
.L576:
	mov	r0, r8
	mov	r1, r7
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, lr}
	b	FlashSetRandomizer
.L580:
	.align	2
.L579:
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
	ldr	r3, .L592
	ldr	r0, [r3, #-1864]
	mov	r4, r3
	cmp	r0, #0
	beq	.L582
	mov	r0, #0
	bl	flash_enter_slc_mode
	b	.L583
.L582:
	bl	flash_exit_slc_mode
.L583:
	ldrb	r3, [r4, #-1860]	@ zero_extendqisi2
	ldr	r5, .L592
	cmp	r3, #0
	beq	.L584
	ldrb	r3, [r5, #-1875]	@ zero_extendqisi2
	tst	r3, #1
	beq	.L584
	mov	r0, #1
	bl	FlashSetInterfaceMode
	mov	r0, #1
	bl	NandcSetMode
	mov	r3, #0
	strb	r3, [r5, #-1860]
.L584:
	ldr	r3, .L592+4
	mov	r0, #0
	ldr	r3, [r3]
	str	r0, [r3, #336]
	ldmfd	sp!, {r3, r4, r5, pc}
.L593:
	.align	2
.L592:
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
	ldr	r3, .L595
	ldr	r3, [r3, r0, asl #3]
	str	r1, [r3, #336]
	bx	lr
.L596:
	.align	2
.L595:
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
	ldr	r1, .L608
	bl	__aeabi_idiv
	ldr	r3, .L608+4
	ldr	r3, [r3, #-2808]
	cmp	r0, #250
	movwgt	r2, #8354
	bgt	.L606
	cmp	r0, #220
	bgt	.L607
	cmp	r0, #185
	movwgt	r2, #4226
	bgt	.L606
	cmp	r0, #160
	movwgt	r2, #4194
	bgt	.L606
	cmp	r4, #35
	movwls	r2, #4193
	bls	.L606
	cmp	r4, #99
	movwls	r2, #4225
	bls	.L606
.L607:
	movw	r2, #8322
.L606:
	str	r2, [r3, #4]
	ldmfd	sp!, {r4, pc}
.L609:
	.align	2
.L608:
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
	bls	.L611
	movw	r3, #8322
	cmp	r0, r3
	bne	.L612
.L611:
	ldr	r3, .L613
	ldr	r3, [r3, #-2808]
	str	r0, [r3, #4]
.L612:
	ldr	r3, .L613+4
	ldrb	r0, [r3, #493]	@ zero_extendqisi2
	b	NandcTimeCfg
.L614:
	.align	2
.L613:
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
	ldr	r4, .L618
	mov	r1, #1
	ldr	r3, .L618+4
	mov	r5, #0
	str	r0, [r4, #-2808]
	stmia	r3, {r0, r2}
	str	r1, [r3, #12]
	mov	r1, #2
	str	r0, [r3, #8]
	str	r1, [r3, #20]
	mov	r1, #3
	str	r0, [r3, #16]
	str	r1, [r3, #28]
	str	r0, [r3, #24]
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
	ldr	r2, .L618+8
	str	r2, [r3, #304]
	bl	ftl_dma32_malloc
	str	r5, [r4, #-1816]
	str	r5, [r4, #-1808]
	str	r0, [r4, #-1844]
	str	r0, [r4, #-1840]
	add	r0, r0, #32768
	str	r0, [r4, #-1836]
	ldmfd	sp!, {r3, r4, r5, pc}
.L619:
	.align	2
.L618:
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
	ldr	ip, .L622
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
.L623:
	.align	2
.L622:
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
	ldr	r3, .L632
	mov	r1, #1
	ldr	r2, [r3, #-2808]
	str	r0, [r3, #-1804]
	mov	r3, #0
	str	r1, [r2, #8]
	mov	r1, #16
	cmp	r0, r1
	bfi	r3, r1, #8, #8
	bfc	r3, #18, #1
	bne	.L625
.L628:
	bfc	r3, #4, #1
	b	.L626
.L625:
	cmp	r0, #24
	orreq	r3, r3, #16
	beq	.L626
	cmp	r0, #40
	orr	r3, r3, #262144
	orr	r3, r3, #16
	beq	.L628
.L626:
	orr	r3, r3, #1
	str	r3, [r2, #12]
	bx	lr
.L633:
	.align	2
.L632:
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
	ldr	r3, .L635
	strb	r0, [r3, #-2743]
	b	NandcBchSel
.L636:
	.align	2
.L635:
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
	ldr	r3, .L646
	stmfd	sp!, {r4, r5, r6, lr}
	.save {r4, r5, r6, lr}
	mov	r5, #0
	ldr	r2, [r3, #-2808]
	mov	r4, r3
	ldr	r1, [r3, #-2804]
	ldr	r6, .L646+4
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
.L639:
	ldrb	r3, [r6, r5, asl #3]	@ zero_extendqisi2
	sub	r3, r3, #1
	uxtb	r3, r3
	cmp	r3, #253
	bhi	.L638
	uxtb	r0, r5
	bl	FlashReset
.L638:
	add	r5, r5, #1
	cmp	r5, #4
	bne	.L639
	ldrb	r3, [r4, #-1860]	@ zero_extendqisi2
	ldr	r5, .L646
	cmp	r3, #0
	beq	.L640
	mov	r0, #1
	bl	NandcSetMode
	ldrb	r0, [r5, #-1875]	@ zero_extendqisi2
	bl	FlashSetInterfaceMode
	ldrb	r0, [r5, #-1875]	@ zero_extendqisi2
	bl	NandcSetMode
	ldrb	r0, [r5, #-2787]	@ zero_extendqisi2
	bl	NandcSetDdrPara
.L640:
	ldr	r3, .L646+8
	ldmfd	sp!, {r4, r5, r6, lr}
	ldr	r3, [r3, #44]
	ldrb	r0, [r3, #20]	@ zero_extendqisi2
	b	FlashBchSel
.L647:
	.align	2
.L646:
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
	ldr	r3, .L652
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
.L653:
	.align	2
.L652:
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
.L655:
	ldr	r3, [r0, #8]
	str	r3, [sp, #4]
	ldr	r3, [sp, #4]
	tst	r3, #1048576
	beq	.L655
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
	ldr	lr, .L677
	.pad #28
	sub	sp, sp, #28
	ldr	r5, .L677+4
	ldr	r8, [sp, #64]
	ldr	r6, [lr, r0, asl #3]
	add	lr, lr, r0, asl #3
	ldr	ip, [sp, #68]
	ldrb	r0, [lr, #4]	@ zero_extendqisi2
	mov	lr, #16
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
	bls	.L660
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
	beq	.L661
	cmp	r1, #0
	bne	.L662
.L670:
	add	r2, r2, #1
	cmp	r8, #0
	mov	r2, r2, asr #1
	movne	r0, r8
	bfi	r4, r2, #22, #6
	ldreq	r0, [r5, #-1840]
	b	.L664
.L662:
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
.L666:
	ldr	lr, [sp, #12]
	cmp	r0, lr
	bcs	.L670
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
	b	.L666
.L664:
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
.L661:
	ldr	r3, [sp, #20]
	str	r3, [r6, #16]
.L660:
	str	r7, [r6, #12]
	str	r4, [r6, #8]
	orr	r4, r4, #4
	str	r4, [r6, #8]
	add	sp, sp, #28
	@ sp needed
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L678:
	.align	2
.L677:
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
	ldr	r3, .L719
	ldr	r5, .L719+4
	ldr	r4, [r3, r0, asl #3]
	ldr	r3, [r5, #-1852]
	cmp	r3, #3
	bls	.L710
	ldr	r3, [r4, #16]
	tst	r3, #4
	beq	.L710
	ldr	r6, [r4, #16]
	ldr	r3, [r4, #8]
	ubfx	r6, r6, #1, #1
	cmp	r6, #0
	str	r3, [sp]
	movne	r6, #0
	beq	.L692
.L682:
	ldr	r2, [r4, #28]
	ldr	r3, [sp]
	ubfx	r2, r2, #16, #5
	ubfx	r3, r3, #22, #6
	cmp	r2, r3
	bge	.L690
	ldr	r3, [r5, #-1852]
	cmp	r3, #5
	bhi	.L683
.L686:
	add	r6, r6, #1
	bic	r3, r6, #-16777216
	cmp	r3, #0
	bne	.L685
	ldr	r2, [r4, #28]
	mov	r1, r6
	ldr	r3, [sp]
	ubfx	r2, r2, #16, #5
	ldr	r0, .L719+8
	ubfx	r3, r3, #22, #6
	bl	printk
	ldr	r0, .L719+12
	mov	r1, r4
	mov	r2, #4
	mov	r3, #512
	bl	rknand_print_hex
	b	.L685
.L683:
	ldr	r3, [r4]
	str	r3, [sp, #4]
	ldr	r3, [sp, #4]
	tst	r3, #8192
	beq	.L686
	ldr	r3, [sp, #4]
	tst	r3, #131072
	beq	.L686
.L690:
	ldr	r3, [r5, #-1816]
	ldr	r4, .L719+4
	cmp	r3, #0
	beq	.L691
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
	b	.L691
.L685:
	mov	r0, #1
	mov	r1, #5
	bl	usleep_range
	b	.L682
.L694:
	ldr	r3, [r4, #8]
	add	r6, r6, #1
	str	r3, [sp]
	bic	r3, r6, #-16777216
	cmp	r3, #0
	bne	.L693
	ldr	r2, [sp]
	mov	r1, r6
	ldr	r3, [r4, #28]
	ldr	r0, .L719+16
	ubfx	r3, r3, #16, #5
	bl	printk
	ldr	r0, .L719+12
	mov	r1, r4
	mov	r2, #4
	mov	r3, #512
	bl	rknand_print_hex
.L693:
	mov	r0, #1
	mov	r1, #5
	bl	usleep_range
.L692:
	ldr	r3, [sp]
	tst	r3, #1048576
	beq	.L694
	ldr	r3, [r5, #-1808]
	cmp	r3, #0
	beq	.L695
	mov	r0, r4
	bl	NandcSendDumpDataStart
.L695:
	ldr	r3, [r5, #-1816]
	ldr	r6, .L719+4
	cmp	r3, #0
	beq	.L696
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
.L696:
	ldr	r3, [r5, #-1808]
	cmp	r3, #0
	beq	.L691
	mov	r0, r4
	bl	NandcSendDumpDataDone
.L691:
	mov	r3, #0
	str	r3, [r5, #-1816]
	b	.L679
.L710:
	ldr	r3, [r4, #8]
	str	r3, [sp]
	ldr	r3, [sp]
	tst	r3, #1048576
	beq	.L710
.L679:
	add	sp, sp, #8
	@ sp needed
	ldmfd	sp!, {r4, r5, r6, pc}
.L720:
	.align	2
.L719:
	.word	.LANCHOR0
	.word	.LANCHOR2
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
	bne	.L722
	cmp	r3, #0
	beq	.L723
	mov	r0, r2
	mov	r1, r3
	mov	r2, #1024
	bl	ftl_memcpy
.L723:
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
.L722:
	cmp	r3, #0
	beq	.L726
	mov	r1, r2
	mov	r0, r3
	mov	r2, #1024
	bl	ftl_memcpy
.L726:
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
	ldr	r3, .L786
	.pad #92
	sub	sp, sp, #92
	tst	fp, #63
	mov	r10, r0
	mov	r7, r1
	mov	r8, r2
	ldr	r5, [sp, #128]
	ldr	r9, [r3, r0, asl #3]
	bne	.L739
	cmp	r5, #0
	bne	.L740
	add	r0, sp, #24
	mov	r1, #255
	mov	r2, #64
	bl	ftl_memset
	add	r5, sp, #24
.L740:
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
	bne	.L741
	ldr	r4, .L786+4
	mov	r1, r8, lsr #1
	mov	r2, r7
	ldr	r3, [r4, #-1804]
	cmp	r3, #25
	mov	r3, r7
	movcc	ip, #64
	movcs	ip, #128
.L743:
	cmp	r2, r1
	add	r5, r5, #4
	add	r0, r3, ip
	bcs	.L784
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
	b	.L743
.L784:
	ldr	r0, [r4, #-1804]
	mov	r2, #0
	ldr	r1, [r4, #-1852]
	mov	r8, r8, lsr #2
	mov	r6, r2
.L745:
	cmp	r2, r8
	bcs	.L741
	cmp	r0, #0
	beq	.L741
	add	r3, r2, #8
	ldr	r3, [r9, r3, asl #2]
	str	r3, [sp, #20]
	ldr	r3, [sp, #20]
	tst	r3, #4
	bne	.L769
	ldr	r4, [sp, #20]
	ubfx	r4, r4, #15, #1
	cmp	r4, #0
	bne	.L769
	cmp	r1, #5
	bls	.L747
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
	b	.L783
.L747:
	cmp	r1, #3
	bls	.L749
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
	bls	.L750
	ldr	r3, [sp, #20]
	ldr	r4, [sp, #20]
	ubfx	r3, r3, #3, #5
	ubfx	r4, r4, #28, #1
.L783:
	orr	r4, r3, r4, asl #5
	b	.L749
.L750:
	ldr	r5, [sp, #20]
	ldr	r4, [sp, #20]
	ubfx	r5, r5, #16, #5
	ubfx	r4, r4, #30, #1
	orr	r4, r5, r4, asl #5
.L749:
	cmp	r6, r4
	movcc	r6, r4
	b	.L746
.L769:
	mvn	r6, #0
.L746:
	add	r2, r2, #1
	b	.L745
.L741:
	mov	r3, #0
	str	r3, [r9, #16]
	b	.L752
.L739:
	cmp	r1, #1
	mov	r4, #0
	bne	.L781
	mov	r6, r4
.L753:
	cmp	r4, r8
	bcs	.L785
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
	b	.L753
.L785:
	mov	r6, #0
	b	.L752
.L781:
	str	r4, [sp]
	mov	r1, r4
	str	r4, [sp, #4]
	mov	r2, #2
	mov	r3, r4
	mov	r6, r4
	bl	NandcXferStart
	str	fp, [sp, #8]
.L758:
	cmp	r4, r8
	bcs	.L752
	mov	r0, r10
	mov	r1, r7
	bl	NandcXferComp
	ldr	r3, [r9, #32]
	add	ip, r4, #2
	cmp	ip, r8
	str	r3, [sp, #20]
	bcs	.L759
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
.L759:
	ldr	r3, [sp, #20]
	tst	r3, #4
	mvnne	r6, #0
	bne	.L760
	ldr	r2, [sp, #20]
	ldr	r3, [sp, #20]
	ubfx	r2, r2, #3, #5
	ubfx	r3, r3, #27, #1
	orr	r3, r2, r3, asl #5
	cmp	r6, r3
	movcc	r6, r3
.L760:
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
	b	.L758
.L752:
	ldr	r3, .L786+4
	clz	r7, r7
	mov	r7, r7, lsr #5
	ldr	r3, [r3, #-1852]
	cmp	r3, #5
	movls	r7, #0
	cmp	r7, #0
	beq	.L764
	ldr	r3, [r9]
	and	r2, r3, #139264
	cmp	r2, #139264
	orreq	r3, r3, #131072
	streq	r3, [r9]
	mvneq	r6, #0
.L764:
	mov	r0, r6
	add	sp, sp, #92
	@ sp needed
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L787:
	.align	2
.L786:
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
	ldr	r3, .L791
	subs	r4, r0, #0
	mov	r6, r1
	mov	r7, r2
	ldrb	r5, [r3, #481]	@ zero_extendqisi2
	bne	.L789
	ldr	r2, .L791+4
	ldrb	r3, [r2, #33]	@ zero_extendqisi2
	ldr	r2, [r2, #36]
	mul	r2, r2, r3
	cmp	r1, r2
	movcc	r5, #4
.L789:
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
.L792:
	.align	2
.L791:
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
	ldr	r4, .L820
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
	beq	.L795
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
	bne	.L796
.L805:
	mvn	r6, #0
	b	.L797
.L796:
	mov	r2, r6
	ldr	r0, .L820+4
	ldr	r1, [sp]
	bl	printk
	cmp	r6, #9
	ldrls	r3, .L820+8
	ldrls	r3, [r3, r10, asl #3]
	ldrls	r2, [r3, #3840]
	ldrls	r2, [r3]
	orrls	r2, r2, #131072
	strls	r2, [r3]
	ldr	r3, [r4, #-1800]
	add	r3, r3, #1
	str	r3, [r4, #-1800]
	cmp	r3, #2048
	bcc	.L797
	ldr	r3, .L820
	mov	r7, #0
	mov	fp, r7
	str	r7, [r3, #-1800]
.L795:
	mov	r4, #0
	mvn	r8, #0
	mov	ip, r4
	mov	r5, r4
	str	r4, [sp, #4]
.L803:
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
	bhi	.L799
	cmp	r0, #2
	bhi	.L809
	add	r5, r5, #1
	cmp	r5, #9
	bls	.L809
	rsb	r4, r5, r9
	mov	r6, r0
	mov	r8, #0
	b	.L801
.L799:
	ldr	r3, [sp, #4]
	cmp	r3, r5
	bcs	.L810
	cmp	r5, #7
	rsb	ip, r5, r4
	bhi	.L811
	str	r5, [sp, #4]
	b	.L810
.L809:
	mov	r8, #0
	mov	r4, r9
	mov	r6, r0
	mov	r7, r8
	mov	fp, r8
	b	.L800
.L810:
	mov	r5, #0
.L800:
	add	r9, r9, #2
	cmp	r9, #69
	bls	.L803
.L801:
	ldr	r3, [sp, #4]
	cmp	r3, r5
	movcs	r4, ip
	b	.L802
.L811:
	mov	r4, ip
.L802:
	cmp	r4, #0
	beq	.L804
	ldr	r0, .L820+12
	mov	r1, r4
	bl	printk
	uxtb	r0, r4
	bl	NandcSetDdrPara
.L804:
	cmn	r8, #1
	bne	.L797
	ldr	r0, .L820+16
	mov	r1, r10
	ldr	r2, [sp]
	bl	printk
	ldr	r3, [sp, #56]
	cmp	r3, #0
	beq	.L805
	ldr	r3, [sp, #8]
	ubfx	r0, r3, #8, #8
	bl	NandcSetDdrPara
.L797:
	mov	r0, r6
	add	sp, sp, #20
	@ sp needed
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L821:
	.align	2
.L820:
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
	ldr	r10, .L843
	cmn	r0, #1
	mov	r4, r0
	bne	.L823
	ldr	r9, .L843+4
	ldrb	fp, [r9, #40]	@ zero_extendqisi2
	cmp	fp, #0
	bne	.L824
.L826:
	ldrb	r3, [r10, #-1860]	@ zero_extendqisi2
	ldr	r9, .L843
	cmp	r3, #0
	beq	.L823
	b	.L842
.L824:
	mov	r3, #0
	mov	r0, r5
	strb	r3, [r9, #40]
	mov	r1, r6
	mov	r2, r8
	mov	r3, r7
	bl	FlashReadRawPage
	strb	fp, [r9, #40]
	cmn	r0, #1
	movne	r4, r0
	beq	.L826
	b	.L823
.L842:
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
	beq	.L827
	ldrb	r3, [r9, #-2743]	@ zero_extendqisi2
	cmp	r0, r3, lsr #1
	bls	.L823
.L827:
	ubfx	r0, fp, #8, #8
	bl	NandcSetDdrPara
.L823:
	ldr	ip, [r10, #-1796]
	ldr	r9, .L843
	adds	r3, ip, #0
	movne	r3, #1
	cmn	r4, #1
	movne	r3, #0
	cmp	r3, #0
	beq	.L828
	mov	r1, r6
	mov	r2, r8
	mov	r3, r7
	mov	r0, r5
	blx	ip
	mov	r2, r5
	mov	r3, r6
	mov	r4, r0
	ldr	r0, .L843+8
	mov	r1, r4
	bl	printk
	cmn	r4, #1
	bne	.L828
	ldrb	r3, [r9, #-2744]	@ zero_extendqisi2
	cmp	r3, #0
	beq	.L828
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
.L828:
	mov	r0, r4
	add	sp, sp, #12
	@ sp needed
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L844:
	.align	2
.L843:
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
	ldr	r5, .L855
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
	bne	.L846
	ldrb	r2, [r5, #-1875]	@ zero_extendqisi2
	tst	r2, #1
	beq	.L846
	mov	r0, #1
	bl	FlashSetInterfaceMode
	mov	r0, #1
	bl	NandcSetMode
	strb	r4, [r5, #-1860]
	b	.L847
.L846:
	mov	r2, #1
	strb	r2, [r3, #-1860]
.L847:
	mov	r0, #0
	add	sp, sp, #8
	@ sp needed
	ldmfd	sp!, {r4, r5, r6, r7, r8, pc}
.L856:
	.align	2
.L855:
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
	ldr	r3, .L873
	mov	r4, #0
	ldr	r8, .L873+4
	mov	r9, #4
	ldr	r5, .L873+8
	mvn	r7, #0
	ldr	r0, [r3]	@ unaligned
	ldr	r3, [r8, #36]
	str	r4, [r5, #-1788]
	mov	r6, r5
	str	r0, [sp, #12]	@ unaligned
	mov	r0, r4
	str	r3, [sp, #4]
	ldr	r3, [r5, #-1868]
	str	r3, [r5, #-1792]
	bl	flash_enter_slc_mode
.L858:
	add	fp, r4, #1
	mov	r10, #0
.L860:
	add	r3, sp, #12
	ldrb	r0, [r3, r10]	@ zero_extendqisi2
	bl	FlashBchSel
	mov	r0, #0
	mov	r1, r4
	ldr	r2, [r5, #-1868]
	mov	r3, r0
	bl	FlashReadRawPage
	cmn	r0, #1
	bne	.L859
	mov	r0, #0
	mov	r1, fp
	ldr	r2, [r6, #-1868]
	mov	r3, r0
	bl	FlashReadRawPage
	cmn	r0, #1
	bne	.L859
	add	r10, r10, #1
	cmp	r10, #4
	beq	.L861
	b	.L860
.L862:
	add	r0, fp, #12
	movw	r1, #2036
	bl	js_hash
	ldr	r3, [fp, #8]
	cmp	r3, r0
	mvnne	r7, #0
	bne	.L861
	ldr	r7, .L873+12
	add	r1, fp, #160
	mov	r2, #32
	mov	r0, r7
	bl	ftl_memcpy
	ldr	r1, [r6, #-1792]
	mov	r2, #32
	ldr	r0, .L873+16
	add	r1, r1, #192
	bl	ftl_memcpy
	ldr	r1, [r6, #-1792]
	mov	r2, #852
	ldr	r0, .L873+20
	add	r1, r1, #224
	bl	ftl_memcpy
	ldrh	r0, [r7, #10]
	bl	FlashBlockAlignInit
	ldr	r7, [r6, #-1792]
	str	r4, [r6, #-1788]
	mov	r0, r4
	ldr	r1, [r8, #36]
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
.L861:
	ldr	r3, [sp, #4]
	subs	r9, r9, #1
	add	r4, r4, r3
	bne	.L858
	mov	r0, r9
.L872:
	bl	flash_exit_slc_mode
	mov	r0, r7
	add	sp, sp, #20
	@ sp needed
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L859:
	ldr	fp, [r5, #-1792]
	ldr	r2, .L873+24
	ldr	r3, [fp]
	cmp	r3, r2
	bne	.L861
	cmp	r7, #0
	bne	.L862
	mov	r0, r4
	ldr	r1, [r8, #36]
	bl	__aeabi_uidiv
	ldr	r3, .L873+8
	add	r0, r0, #1
	str	r0, [r3, #-1784]
	mov	r0, r7
	b	.L872
.L874:
	.align	2
.L873:
	.word	.LANCHOR3
	.word	.LANCHOR0
	.word	.LANCHOR2
	.word	.LANCHOR1+472
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
	mov	r7, r0
	mov	r10, r3
	str	r1, [sp, #20]
	str	r2, [sp, #8]
	bl	NandcWaitFlashReady
	ldr	r3, .L904
	ldr	r5, [r3, r7, asl #3]
	add	r3, r3, r7, asl #3
	ldrb	r3, [r3, #4]	@ zero_extendqisi2
	add	r6, r3, #8
	add	r6, r5, r6, asl #8
	str	r3, [sp, #4]
	ldr	r3, .L904+4
	ldrb	r8, [r3]	@ zero_extendqisi2
	str	r3, [sp, #12]
	sub	r4, r8, #67
	ldr	r3, [sp, #4]
	cmp	r4, #1
	mov	r3, r3, asl #8
	movls	r4, #0
	str	r3, [sp, #16]
	bls	.L876
	ldr	r3, .L904+8
	ldrb	r4, [r3, #-1860]	@ zero_extendqisi2
	cmp	r4, #0
	beq	.L877
	mov	r0, #0
	mov	r4, #1
	bl	NandcSetDdrMode
.L877:
	ldr	r3, [sp, #16]
	mov	r2, #92
	add	r3, r5, r3
	str	r2, [r3, #2056]
	mov	r2, #197
	str	r2, [r3, #2056]
.L876:
	mov	r8, #1
	mvn	r9, #0
.L878:
	ldr	r3, .L904+12
	ldrb	r3, [r3]	@ zero_extendqisi2
	add	r3, r3, #1
	cmp	r8, r3
	bcs	.L903
	ldr	r3, [sp, #12]
	mov	r0, r6
	uxtb	r1, r8
	ldrb	r3, [r3]	@ zero_extendqisi2
	sub	r3, r3, #67
	cmp	r3, #1
	bhi	.L879
	bl	SandiskSetRRPara
	b	.L880
.L879:
	bl	ToshibaSetRRPara
.L880:
	ldr	r3, [sp, #12]
	ldrb	r3, [r3]	@ zero_extendqisi2
	cmp	r3, #34
	bne	.L881
	ldr	r3, .L904+12
	ldrb	r3, [r3]	@ zero_extendqisi2
	sub	r3, r3, #3
	cmp	r8, r3
	ldreq	r3, [sp, #4]
	moveq	r2, #179
	addeq	r3, r5, r3, asl #8
	streq	r2, [r3, #2056]
.L881:
	ldr	r3, [sp, #16]
	cmp	r4, #0
	mov	r2, #38
	add	r3, r5, r3
	str	r2, [r3, #2056]
	mov	r2, #93
	str	r2, [r3, #2056]
	beq	.L882
	mov	r0, #4
	bl	NandcSetDdrMode
	mov	r0, r7
	mov	r3, r10
	ldr	r1, [sp, #20]
	ldr	r2, [sp, #8]
	bl	FlashReadRawPage
	mov	fp, r0
	mov	r0, #0
	bl	NandcSetDdrMode
	b	.L883
.L882:
	mov	r0, r7
	ldr	r1, [sp, #20]
	ldr	r2, [sp, #8]
	mov	r3, r10
	bl	FlashReadRawPage
	mov	fp, r0
.L883:
	cmn	fp, #1
	beq	.L884
	ldr	r3, .L904+8
	cmn	r9, #1
	moveq	r9, fp
	ldrb	r2, [r3, #-2743]	@ zero_extendqisi2
	add	r2, r2, r2, asl #1
	cmp	fp, r2, asr #2
	bcc	.L886
	mov	r10, #0
	str	r10, [sp, #8]
.L884:
	add	r8, r8, #1
	b	.L878
.L903:
	mov	fp, r9
.L886:
	ldr	r3, [sp, #12]
	mov	r0, r6
	mov	r1, #0
	ldrb	r2, [r3]	@ zero_extendqisi2
	sub	r2, r2, #67
	cmp	r2, #1
	bhi	.L888
	bl	SandiskSetRRPara
	b	.L889
.L888:
	bl	ToshibaSetRRPara
.L889:
	ldr	r3, [sp, #16]
	mov	r2, #255
	add	r5, r5, r3
	str	r2, [r5, #2056]
	ldr	r2, .L904+8
	ldrb	r2, [r2, #-2743]	@ zero_extendqisi2
	add	r2, r2, r2, asl #1
	cmp	fp, r2, asr #2
	bcc	.L890
	cmn	fp, #1
	movne	fp, #256
.L890:
	mov	r0, r7
	bl	NandcWaitFlashReady
	cmp	r4, #0
	beq	.L891
	mov	r0, #4
	bl	NandcSetDdrMode
.L891:
	mov	r0, fp
	add	sp, sp, #28
	@ sp needed
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L905:
	.align	2
.L904:
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
	mov	r7, r0
	mov	r9, r2
	mov	r8, r3
	mov	fp, r1
	bl	NandcWaitFlashReady
	ldr	r2, .L920
	mov	r6, #1
	ldr	r10, .L920+4
	mvn	r4, #0
	add	r3, r2, r7, asl #3
	ldr	ip, .L920+8
	ldr	r5, [r2, r7, asl #3]
	ldrb	r3, [r3, #4]	@ zero_extendqisi2
	add	r3, r3, #8
	add	r5, r5, r3, asl #8
.L907:
	ldrb	r3, [r10]	@ zero_extendqisi2
	add	r3, r3, #1
	cmp	r6, r3
	bcs	.L910
	mov	r0, r5
	uxtb	r1, r6
	str	ip, [sp, #4]
	bl	SamsungSetRRPara
	mov	r0, r7
	mov	r1, fp
	mov	r2, r9
	mov	r3, r8
	bl	FlashReadRawPage
	cmn	r0, #1
	ldr	ip, [sp, #4]
	beq	.L908
	ldrb	r3, [ip, #-2743]	@ zero_extendqisi2
	cmn	r4, #1
	moveq	r4, r0
	add	r3, r3, r3, asl #1
	cmp	r0, r3, asr #2
	bcc	.L913
	mov	r8, #0
	mov	r9, r8
.L908:
	add	r6, r6, #1
	b	.L907
.L913:
	mov	r4, r0
.L910:
	mov	r0, r5
	mov	r1, #0
	bl	SamsungSetRRPara
	ldr	r3, .L920+8
	ldrb	r3, [r3, #-2743]	@ zero_extendqisi2
	add	r3, r3, r3, asl #1
	cmp	r4, r3, asr #2
	bcc	.L912
	cmn	r4, #1
	movne	r4, #256
.L912:
	mov	r0, r4
	add	sp, sp, #12
	@ sp needed
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L921:
	.align	2
.L920:
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
.L924:
	stmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.save {r4, r5, r6, r7, r8, r9, r10, fp, lr}
	mov	r8, r3
	ldr	r3, .L948
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
	ldrne	r2, .L948+4
	ubfxeq	r5, r5, #2, #8
	smullne	r2, r3, r5, r2
	uxtbne	r5, r3
	ldr	r3, .L948+8
	add	r3, r3, r0, asl #3
	str	r3, [sp, #24]
.L934:
	mov	r0, r6
	mov	r9, #0
	bl	NandcWaitFlashReady
	ldr	r3, .L948+8
	mvn	r4, #0
	ldr	r3, [r3, r6, asl #3]
	str	r3, [sp, #12]
	ldr	r3, [sp, #24]
	ldrb	r3, [r3, #4]	@ zero_extendqisi2
	str	r3, [sp, #16]
	ldr	r2, [sp, #16]
	ldr	r3, [sp, #12]
	add	r7, r3, r2, asl #8
.L925:
	ldr	r3, .L948+12
	ldrb	r3, [r3]	@ zero_extendqisi2
	cmp	r9, r3
	bcs	.L928
	mov	r3, #239
	mov	r0, #200
	str	r3, [r7, #2056]
	mov	r3, #137
	str	r3, [r7, #2052]
	bl	ndelay
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
	beq	.L926
	cmn	r4, #1
	ldr	ip, [sp, #28]
	moveq	r4, r0
	cmp	r0, r5
	bcc	.L936
	mov	r8, ip
	mov	fp, ip
.L926:
	ldr	r9, [sp, #8]
	b	.L925
.L936:
	mov	r4, r0
	mov	r8, ip
	mov	fp, ip
.L928:
	ldr	r2, [sp, #16]
	mov	r0, #200
	ldr	r3, [sp, #12]
	add	r7, r3, r2, asl #8
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
	bcc	.L930
	cmn	r4, #1
	movne	r4, #256
.L930:
	cmn	r4, #1
	movne	r7, #0
	moveq	r7, #1
	cmp	r4, #256
	movne	r1, r7
	orreq	r1, r7, #1
	cmp	r1, #0
	beq	.L931
	str	r4, [sp]
	mov	r1, r9
	ldr	r0, .L948+16
	mov	r3, r9
	ldr	r2, [sp, #20]
	bl	printk
	cmp	r10, #0
	bne	.L932
	ldr	r3, .L948
	ldrb	r3, [r3, #-2744]	@ zero_extendqisi2
	cmp	r3, #0
	moveq	r7, #0
	andne	r7, r7, #1
	cmp	r7, #0
	beq	.L941
	mov	r0, r6
	mov	r1, #3
	bl	micron_auto_read_calibration_config
	mov	r10, #1
	b	.L934
.L932:
	mov	r0, r6
	mov	r1, #0
	bl	micron_auto_read_calibration_config
	cmn	r4, #1
	movne	r4, #256
	b	.L941
.L931:
	cmp	r10, #0
	beq	.L941
	mov	r0, r6
	mov	r4, #256
	bl	micron_auto_read_calibration_config
.L941:
	mov	r0, r4
	add	sp, sp, #36
	@ sp needed
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L949:
	.align	2
.L948:
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
	ldr	r2, .L968
	mov	fp, r1
	ldr	ip, .L968+4
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
.L952:
	cmp	ip, r9
	bcs	.L956
	add	r4, r4, #1
	mov	r0, r5
	ldrb	r1, [r7, #-2731]	@ zero_extendqisi2
	uxtb	r4, r4
	ldr	r2, .L968+8
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
	beq	.L954
	ldrb	r3, [r7, #-2743]	@ zero_extendqisi2
	cmn	r6, #1
	moveq	r6, r0
	add	r3, r3, r3, asl #1
	cmp	r0, r3, asr #2
	bcc	.L961
	mov	r8, #0
	mov	r10, r8
.L954:
	add	ip, ip, #1
	b	.L952
.L961:
	mov	r6, r0
.L956:
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
	bcc	.L960
	cmn	r6, #1
	movne	r6, #256
.L960:
	mov	r0, r6
	add	sp, sp, #12
	@ sp needed
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L969:
	.align	2
.L968:
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
	ldr	r3, .L1000
	ldr	r4, [r3, r10, asl #3]
	add	r3, r3, r10, asl #3
	ldrb	r3, [r3, #4]	@ zero_extendqisi2
	str	r3, [sp, #8]
	ldr	r3, .L1000+4
	ldrb	r2, [r3, #-1876]	@ zero_extendqisi2
	str	r3, [sp, #16]
	cmp	r2, #0
	bne	.L971
	ldr	r3, [sp, #8]
	mvn	r5, #0
	mov	r6, #1
	mov	r9, r3, asl #8
	add	r7, r4, r9
.L975:
	mov	r3, #239
	str	r3, [r7, #2056]
	mov	r3, #141
	str	r3, [r7, #2052]
	ldr	r3, .L1000+8
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
	beq	.L972
	ldr	r3, [sp, #16]
	cmn	r5, #1
	moveq	r5, r0
	ldrb	r3, [r3, #-2743]	@ zero_extendqisi2
	add	r3, r3, r3, asl #1
	cmp	r0, r3, asr #2
	bcc	.L983
	ldr	ip, [sp, #20]
	mov	r8, ip
	mov	fp, ip
.L972:
	add	r6, r6, #1
	cmp	r6, #26
	bne	.L975
	b	.L974
.L983:
	mov	r5, r0
.L974:
	add	r9, r4, r9
	mov	r3, #239
	str	r3, [r9, #2056]
	ldr	r3, [sp, #8]
	add	r4, r4, r3, asl #8
	mov	r3, #141
	b	.L999
.L971:
	ldr	r3, [sp, #8]
	mvn	r5, #0
	ldr	r7, .L1000+12
	mov	r6, #1
	mov	ip, r3, asl #8
	add	r9, r4, ip
.L980:
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
	beq	.L977
	ldr	r3, [sp, #16]
	cmn	r5, #1
	moveq	r5, r0
	ldrb	r3, [r3, #-2743]	@ zero_extendqisi2
	add	r3, r3, r3, asl #1
	cmp	r0, r3, asr #2
	bcc	.L984
	mov	r8, #0
	mov	fp, r8
.L977:
	add	r6, r6, #1
	add	r7, r7, #4
	cmp	r6, #26
	bne	.L980
	b	.L979
.L984:
	mov	r5, r0
.L979:
	add	ip, r4, ip
	mov	r3, #239
	str	r3, [ip, #2056]
	ldr	r3, [sp, #8]
	add	r4, r4, r3, asl #8
	mov	r3, #137
.L999:
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
	bcc	.L981
	cmn	r5, #1
	movne	r5, #256
.L981:
	cmn	r5, #1
	cmpne	r5, #256
	bne	.L982
	str	r5, [sp]
	mov	r1, r6
	ldr	r0, .L1000+16
	mov	r3, r6
	ldr	r2, [sp, #12]
	bl	printk
.L982:
	mov	r0, r10
	bl	NandcWaitFlashReady
	mov	r0, r5
	add	sp, sp, #28
	@ sp needed
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L1001:
	.align	2
.L1000:
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
	ldr	r3, .L1006
	subs	r4, r0, #0
	mov	r5, r1
	mov	r7, r2
	ldrb	r6, [r3, #481]	@ zero_extendqisi2
	bne	.L1003
	ldr	r2, .L1006+4
	ldrb	r3, [r2, #33]	@ zero_extendqisi2
	ldr	r1, [r2, #36]
	mul	r1, r1, r3
	cmp	r5, r1
	bcs	.L1003
	ldrb	r3, [r2, #32]	@ zero_extendqisi2
	cmp	r3, #0
	movne	r6, #4
.L1003:
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
.L1007:
	.align	2
.L1006:
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
	ldr	r4, .L1022
	ldr	r7, .L1022+4
	ldr	r9, .L1022+8
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
	ldr	r1, .L1022+12
	mov	r2, #32
	str	r9, [r3]
	ldr	r0, [r4, #-1792]
	ldrb	r3, [r7, #3152]	@ zero_extendqisi2
	add	r0, r0, #16
	strh	r3, [r0, #-4]	@ movhi
	ldrb	r3, [r7, #33]	@ zero_extendqisi2
	strh	r3, [r0, #-2]	@ movhi
	ldrb	r3, [r4, #-1860]	@ zero_extendqisi2
	str	r3, [r0, #1060]
	bl	ftl_memcpy
	ldr	r0, [r4, #-1792]
	ldr	r1, .L1022+16
	mov	r2, #8
	add	r0, r0, #80
	bl	ftl_memcpy
	ldr	r0, [r4, #-1792]
	ldr	r1, .L1022+20
	mov	r2, #32
	add	r0, r0, #96
	bl	ftl_memcpy
	ldr	r0, [r4, #-1792]
	ldr	r1, .L1022+24
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
	ldr	r1, .L1022+28
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
.L1014:
	ldr	r1, [r7, #36]
	mov	r0, #0
	mov	r2, r0
	mul	r1, r1, r6
	bl	FlashEraseBlock
	ldrb	r10, [r4, #-2744]	@ zero_extendqisi2
	cmp	r10, #0
	beq	.L1009
	mov	r10, #0
.L1010:
	ldr	r1, [r7, #36]
	mov	r0, #0
	ldr	r2, [r4, #-1868]
	mov	r3, r0
	mla	r1, r1, r6, r10
	add	r10, r10, #1
	bl	FlashProgPage
	cmp	r10, #10
	bne	.L1010
	b	.L1011
.L1009:
	ldr	r1, [fp, #36]
	mov	r3, r10
	ldr	r2, [r8, #-1868]
	mov	r0, r10
	mul	r1, r1, r6
	bl	FlashProgPage
	ldr	r1, [fp, #36]
	mov	r0, r10
	ldr	r2, [r8, #-1868]
	mov	r3, r10
	mul	r1, r1, r6
	add	r1, r1, #1
	bl	FlashProgPage
.L1011:
	ldr	r1, [r7, #36]
	mov	r0, #0
	ldr	r2, [r4, #-1776]
	mov	r3, r0
	mul	r1, r1, r6
	bl	FlashReadRawPage
	add	r2, r6, #1
	cmn	r0, #1
	beq	.L1012
	ldr	r10, [r8, #-1792]
	ldr	r3, [r10]
	cmp	r3, r9
	bne	.L1012
	add	r0, r10, #12
	movw	r1, #2036
	str	r2, [sp, #4]
	bl	js_hash
	ldr	r3, [r10, #8]
	cmp	r3, r0
	ldr	r2, [sp, #4]
	bne	.L1012
	ldr	r3, [fp, #36]
	cmp	r5, #1
	str	r2, [r8, #-1784]
	mul	r6, r3, r6
	str	r6, [r8, #-1788]
	beq	.L1015
	mov	r5, #1
.L1012:
	cmp	r2, #4
	mov	r6, r2
	bne	.L1014
	b	.L1013
.L1015:
	mov	r5, #2
.L1013:
	mov	r0, #0
	bl	flash_exit_slc_mode
	clz	r0, r5
	mov	r0, r0, lsr #5
	rsb	r0, r0, #0
	add	sp, sp, #12
	@ sp needed
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L1023:
	.align	2
.L1022:
	.word	.LANCHOR2
	.word	.LANCHOR0
	.word	1312902724
	.word	IDByte
	.word	.LANCHOR0+3156
	.word	.LANCHOR0+3164
	.word	.LANCHOR1+472
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
	ldr	r3, .L1044
	.pad #20
	sub	sp, sp, #20
	ldr	r5, .L1044+4
	ldr	r0, [r3]	@ unaligned
	ldrb	r3, [r5, #-2743]	@ zero_extendqisi2
	str	r0, [sp, #12]	@ unaligned
	str	r3, [sp]
	ldr	r3, [r5, #-1864]
	cmp	r3, #0
	beq	.L1025
	mov	r0, #0
	bl	flash_enter_slc_mode
.L1025:
	mov	r0, r9
	mov	r1, #0
	mov	r2, #2048
	ldr	r10, .L1044+8
	bl	ftl_memset
	mvn	r8, #0
	mov	r4, #2
	mov	fp, r10
.L1026:
	ldrb	r3, [r10, #33]	@ zero_extendqisi2
	cmp	r4, r3
	bcs	.L1030
	mov	r7, #0
.L1028:
	add	r3, sp, #12
	ldr	r6, .L1044+4
	ldrb	ip, [r7, r3]	@ zero_extendqisi2
	mov	r0, ip
	str	ip, [sp, #4]
	bl	FlashBchSel
	ldr	r1, [fp, #36]
	mov	r0, #0
	ldr	r2, [r5, #-1868]
	mov	r3, r0
	mul	r1, r1, r4
	bl	FlashReadRawPage
	cmn	r0, #1
	ldr	ip, [sp, #4]
	bne	.L1027
	add	r7, r7, #1
	cmp	r7, #4
	bne	.L1028
	b	.L1029
.L1027:
	ldr	r3, [r6, #-1868]
	ldr	r2, .L1044+12
	ldr	r3, [r3]
	cmp	r3, r2
	bne	.L1029
	mov	r1, ip
	ldr	r0, .L1044+16
	bl	printk
	mov	r0, r9
	ldr	r1, [r6, #-1868]
	mov	r2, #2048
	bl	ftl_memcpy
	ldr	r3, [r6, #-1868]
	ldr	r3, [r3, #512]
	strb	r3, [fp, #33]
	ldr	r3, [r6, #-1784]
	cmp	r3, r4
	bls	.L1033
	str	r4, [r6, #-1784]
	bl	FlashSavePhyInfo
	mov	r8, #0
.L1029:
	add	r4, r4, #1
	b	.L1026
.L1033:
	mov	r8, #0
.L1030:
	ldr	r0, [sp]
	bl	FlashBchSel
	ldr	r3, [r5, #-1864]
	cmp	r3, #0
	beq	.L1037
	mov	r0, #0
	bl	flash_exit_slc_mode
.L1037:
	mov	r0, r8
	add	sp, sp, #20
	@ sp needed
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L1045:
	.align	2
.L1044:
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
	bl	ftl_dma32_malloc
	ldr	r4, .L1145
	ldr	r5, .L1145+4
	mov	r7, #0
	str	r0, [r4, #-1868]
	mov	r0, #32768
	bl	ftl_dma32_malloc
	str	r0, [r4, #-1776]
	mov	r0, #4096
	bl	ftl_dma32_malloc
	str	r0, [r4, #-1772]
	mov	r0, #32768
	bl	ftl_dma32_malloc
	str	r0, [r4, #-1768]
	mov	r0, #4096
	bl	ftl_dma32_malloc
	mov	r3, #50
	str	r7, [r4, #-1784]
	strb	r3, [r5, #33]
	strb	r3, [r4, #-1780]
	mov	r3, #128
	strb	r7, [r4, #-1860]
	str	r3, [r5, #36]
	mov	r3, #60
	str	r7, [r4, #-1800]
	strb	r7, [r5, #32]
	strb	r7, [r4, #-1760]
	strb	r3, [r4, #-1779]
	str	r0, [r4, #-1764]
	mov	r0, r6
	bl	NandcInit
	ldr	r6, .L1145+8
	mov	r8, r6
.L1052:
	add	r2, r5, r7, asl #3
	uxtb	r9, r7
	ldr	r10, [r5, r7, asl #3]
	ldrb	fp, [r2, #4]	@ zero_extendqisi2
	mov	r0, r9
	bl	FlashReset
	mov	r0, r9
	bl	NandcFlashCs
	mov	r3, #144
	add	r10, r10, fp, asl #8
	mov	r0, #200
	mov	fp, #0
	str	r3, [r10, #2056]
	str	fp, [r10, #2052]
	bl	ndelay
	ldr	r1, [r10, #2048]
	uxtb	r1, r1
	strb	r1, [r6]
	ldr	r0, [r10, #2048]
	cmp	r1, #44
	strb	r0, [r6, #1]
	ldr	r0, [r10, #2048]
	strb	r0, [r6, #2]
	ldr	r0, [r10, #2048]
	strb	r0, [r6, #3]
	ldr	r0, [r10, #2048]
	strb	r0, [r6, #4]
	ldr	r0, [r10, #2048]
	strb	r0, [r6, #5]
	bne	.L1047
	mov	r1, #239
	mov	r0, #200
	str	r1, [r10, #2056]
	mov	r1, #1
	str	r1, [r10, #2052]
	bl	ndelay
	mov	r1, #4
	str	r1, [r10, #2048]
	str	fp, [r10, #2048]
	str	fp, [r10, #2048]
	str	fp, [r10, #2048]
.L1047:
	mov	r0, r9
	bl	NandcFlashDeCs
	ldrb	r2, [r6]	@ zero_extendqisi2
	sub	r3, r2, #1
	uxtb	r3, r3
	cmp	r3, #253
	bhi	.L1048
	ldrb	r1, [r6, #2]	@ zero_extendqisi2
	ldrb	r3, [r6, #1]	@ zero_extendqisi2
	ldr	r0, .L1145+12
	str	r1, [sp]
	ldrb	r1, [r6, #3]	@ zero_extendqisi2
	str	r1, [sp, #4]
	ldrb	r1, [r6, #4]	@ zero_extendqisi2
	str	r1, [sp, #8]
	ldrb	r1, [r6, #5]	@ zero_extendqisi2
	str	r1, [sp, #12]
	add	r1, r7, #1
	bl	printk
.L1048:
	cmp	r7, #0
	bne	.L1049
	ldrb	r3, [r8]	@ zero_extendqisi2
	sub	r3, r3, #1
	uxtb	r3, r3
	cmp	r3, #253
	bhi	.L1099
	ldr	r3, .L1145+8
	ldrb	r3, [r3, #1]	@ zero_extendqisi2
	cmp	r3, #255
	beq	.L1099
	bl	FlashCs123Init
.L1049:
	ldrb	r3, [r6]	@ zero_extendqisi2
	add	r7, r7, #1
	add	r6, r6, #8
	cmp	r3, #181
	moveq	r3, #44
	streqb	r3, [r6, #-8]
	cmp	r7, #4
	bne	.L1052
	ldrb	r3, [r8]	@ zero_extendqisi2
	cmp	r3, #173
	beq	.L1053
	ldr	r0, [r4, #-1856]
	bl	NandcSetDdrMode
.L1053:
	mov	r2, #852
	ldr	r0, .L1145+16
	mov	r1, #0
	ldr	r7, .L1145+20
	bl	ftl_memset
	ldr	r2, [r4, #-1848]
	ldr	r0, .L1145+24
	cmp	r2, r7
	add	r3, r0, #472
	str	r3, [r5, #44]
	mov	r3, #0
	strb	r3, [r5, #40]
	bne	.L1054
	ldrb	r3, [r0, #491]	@ zero_extendqisi2
	cmp	r3, #50
	ldrne	r3, .L1145
	movne	r1, #1
	strne	r1, [r3, #-1864]
.L1054:
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
	bne	.L1055
	cmp	r6, #220
	bne	.L1056
	ldr	r3, .L1145+8
	ldrb	r3, [r3, #3]	@ zero_extendqisi2
	cmp	r3, #149
	bne	.L1056
.L1055:
	mov	lr, #16
	strb	lr, [r5, #33]
	strb	lr, [r4, #-1779]
	mov	r1, #1
	ldrb	lr, [r8]	@ zero_extendqisi2
	strb	r1, [r5, #32]
	cmp	lr, #152
	ldr	r3, .L1145
	strb	lr, [r0, #3413]
	strb	r6, [r0, #3414]
	bne	.L1058
	ldr	lr, .L1145+8
	ldrsb	lr, [lr, #4]
	cmp	lr, #0
	strltb	r1, [r3, #-1760]
	movge	r1, #24
	strgeb	r1, [r3, #-1779]
.L1058:
	movw	r3, #2049
	cmp	r2, r7
	cmpne	r2, r3
	moveq	r3, #16
	streqb	r3, [r4, #-1779]
	cmp	ip, #0
	ldrne	r3, .L1145+28
	movne	r2, #2048
	strneh	r2, [r3, #14]	@ movhi
	mvnne	r3, #37
	bne	.L1139
.L1062:
	cmp	r6, #220
	bne	.L1064
	ldr	r3, .L1145+28
	mov	r2, #4096
	strh	r2, [r3, #14]	@ movhi
	mvn	r3, #35
.L1139:
	strb	r3, [r0, #3414]
	b	.L1063
.L1064:
	cmp	r6, #211
	ldreq	r3, .L1145+28
	moveq	r2, #4096
	streqh	r2, [r3, #14]	@ movhi
	moveq	r3, #2
	streqb	r3, [r0, #3425]
.L1063:
	ldr	r1, .L1145+32
	mov	r2, #32
	ldr	r0, .L1145+36
	bl	ftl_memcpy
	ldr	r0, .L1145+40
	ldr	r1, .L1145+28
	mov	r2, #32
	bl	ftl_memcpy
.L1056:
	ldrb	r3, [r5, #32]	@ zero_extendqisi2
	cmp	r3, #0
	bne	.L1065
	bl	FlashLoadPhyInfoInRam
	cmp	r0, #0
	bne	.L1067
	ldr	r3, .L1145+4
	ldr	r6, .L1145
	ldr	r3, [r3, #44]
	ldrh	r3, [r3, #16]
	mov	r3, r3, lsr #8
	tst	r3, #1
	and	r0, r3, #7
	strb	r0, [r4, #-1875]
	bne	.L1067
	mov	r3, #1
	strb	r3, [r6, #-1860]
	bl	FlashSetInterfaceMode
	ldrb	r0, [r6, #-1875]	@ zero_extendqisi2
	bl	NandcSetMode
.L1067:
	ldr	r3, [r5, #44]
	ldr	r6, .L1145
	ldrb	r3, [r3, #26]	@ zero_extendqisi2
	strb	r3, [r4, #-2744]
	bl	FlashLoadPhyInfo
	cmp	r0, #0
	beq	.L1065
	ldr	r3, [r6, #-1856]
	cmp	r3, #0
	beq	.L1070
	mov	r0, #1
	bl	FlashSetInterfaceMode
	mov	r0, #1
	b	.L1140
.L1070:
	ldrb	r0, [r6, #-1875]	@ zero_extendqisi2
	bl	FlashSetInterfaceMode
	ldrb	r0, [r6, #-1875]	@ zero_extendqisi2
.L1140:
	bl	NandcSetMode
	bl	FlashLoadPhyInfo
	cmp	r0, #0
	beq	.L1065
	mov	r0, #1
	ldr	r6, .L1145+4
	bl	FlashSetInterfaceMode
	mov	r0, #1
	bl	NandcSetMode
	ldr	r3, [r5, #44]
	ldr	r0, .L1145+44
	ldrh	r1, [r3, #14]
	bl	printk
	bl	FlashLoadPhyInfoInRam
	cmn	r0, #1
	beq	.L1113
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
	blt	.L1072
	add	r0, r3, #255
	cmp	r1, r0, asr #8
	bge	.L1073
.L1072:
	bic	r3, r3, #255
	strh	r3, [r2, #14]	@ movhi
.L1073:
	ldrb	r3, [r4, #-1875]	@ zero_extendqisi2
	tst	r3, #6
	beq	.L1074
	bl	FlashSavePhyInfo
	mov	r0, #0
	bl	flash_enter_slc_mode
	ldr	r3, .L1145
	mov	r0, #0
	ldr	r1, [r3, #-1788]
	bl	FlashDdrParaScan
	mov	r0, #0
	bl	flash_exit_slc_mode
.L1074:
	bl	FlashSavePhyInfo
.L1065:
	ldr	r2, [r5, #44]
	ldr	r6, .L1145
	ldrb	r3, [r2, #26]	@ zero_extendqisi2
	ldrh	r0, [r2, #10]
	ldrb	r9, [r2, #18]	@ zero_extendqisi2
	strb	r3, [r4, #-2744]
	ldrh	r3, [r2, #16]
	ubfx	r1, r3, #7, #1
	strb	r1, [r5, #40]
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
	beq	.L1076
	ldrb	r0, [r3, #19]	@ zero_extendqisi2
	ldr	r3, .L1145+48
	ldr	r2, .L1145+52
	ldrb	r1, [r6, #-2730]	@ zero_extendqisi2
	strb	r0, [r3]
	ldrb	r3, [r6, #-2731]	@ zero_extendqisi2
	mov	ip, r2
	strb	r3, [r2]
	ldr	r3, .L1145+56
	strb	r1, [r3]
	sub	r1, r0, #1
	cmp	r1, #7
	bhi	.L1077
	ldr	r3, .L1145+60
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
	ldreq	r3, .L1145+64
	beq	.L1080
	ldr	r2, .L1145+64
	cmp	r1, #0
	sub	r3, r2, #8
	movne	r3, r2
.L1080:
	sub	r1, r3, #1
	add	r3, r3, #31
	mov	r2, #0
.L1081:
	ldrsb	ip, [r1, #1]!
	cmp	ip, #0
	addeq	r2, r2, #1
	cmp	r1, r3
	bne	.L1081
	cmp	r2, #27
	bls	.L1076
	bl	FlashGetReadRetryDefault
	bl	FlashSavePhyInfo
	b	.L1076
.L1077:
	sub	r1, r0, #17
	cmp	r1, #2
	bhi	.L1083
	ldr	r2, .L1145+68
	cmp	r0, #19
	str	r2, [r6, #-1796]
	moveq	r2, #15
	bne	.L1144
	b	.L1142
.L1083:
	sub	r1, r0, #65
	cmp	r0, #33
	cmpne	r1, #1
	bhi	.L1085
	ldr	r1, .L1145+72
	str	r1, [r6, #-1796]
	mov	r1, #4
	strb	r1, [r2]
.L1144:
	mov	r2, #7
.L1142:
	strb	r2, [r3]
	b	.L1076
.L1085:
	sub	r1, r0, #67
	sub	r2, r0, #34
	cmp	r1, #1
	movhi	r1, #0
	movls	r1, #1
	cmp	r2, #1
	movhi	r2, r1
	orrls	r2, r1, #1
	cmp	r2, #0
	beq	.L1086
	ldr	r2, .L1145+72
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
	b	.L1076
.L1086:
	cmp	r0, #49
	ldreq	r3, .L1145+76
	streq	r3, [r6, #-1796]
	beq	.L1076
	cmp	r0, #50
	streq	r2, [r6, #-1864]
	ldreq	r3, .L1145+80
	streq	r3, [r6, #-1796]
.L1076:
	ldr	r3, [r4, #-1848]
	cmp	r3, r7
	bne	.L1091
	ldr	r2, .L1145
	ldrb	r2, [r2, #-2744]	@ zero_extendqisi2
	cmp	r2, #0
	ldrne	r2, [r5, #44]
	movne	r1, #0
	strneb	r1, [r2, #18]
.L1091:
	ldrb	r2, [r8]	@ zero_extendqisi2
	cmp	r2, #44
	bne	.L1092
	ldrb	r2, [r4, #-1860]	@ zero_extendqisi2
	cmp	r2, #0
	beq	.L1092
	cmp	r3, r7
	bne	.L1093
	ldr	r3, .L1145
	ldrb	r3, [r3, #-2744]	@ zero_extendqisi2
	cmp	r3, #0
	bne	.L1092
.L1093:
	mov	r0, #1
	mov	r3, #0
	strb	r3, [r4, #-1860]
	bl	FlashSetInterfaceMode
	mov	r0, #1
	bl	NandcSetMode
.L1092:
	ldrb	r3, [r4, #-1875]	@ zero_extendqisi2
	tst	r3, #6
	beq	.L1094
	ldr	r2, .L1145
	ldrb	r2, [r2, #-1860]	@ zero_extendqisi2
	cmp	r2, #0
	bne	.L1095
	tst	r3, #1
	bne	.L1094
.L1095:
	mov	r0, #0
	bl	flash_enter_slc_mode
	mov	r0, #0
	ldr	r1, [r4, #-1788]
	bl	FlashDdrParaScan
	mov	r0, #0
	bl	flash_exit_slc_mode
.L1094:
	ldr	r3, [r5, #44]
	mov	r10, #16
	ldr	r9, .L1145+4
	ldr	r6, .L1145+84
	ldrb	r0, [r3, #20]	@ zero_extendqisi2
	bl	FlashBchSel
	add	r0, r9, #3328
	bl	FlashReadIdbDataRaw
	ldr	r0, .L1145+88
	strb	r10, [r5, #33]
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
	ldrb	r1, [r5, #33]	@ zero_extendqisi2
	strh	r1, [r6, #26]	@ movhi
	uxth	r3, r3
	ldrb	r1, [r5, #32]	@ zero_extendqisi2
	strh	r3, [r6, #22]	@ movhi
	cmp	r1, #1
	bne	.L1097
	mov	r3, r3, asl #1
	mov	r4, r4, lsr #1
	mov	r2, r2, asl #1
	strh	r3, [r6, #22]	@ movhi
	strb	r10, [r9, #33]
	mov	r3, #8
	strh	r4, [r6, #14]	@ movhi
	strh	r2, [r6, #20]	@ movhi
	strh	r3, [r6, #26]	@ movhi
.L1097:
	ldrb	r0, [r7, #20]	@ zero_extendqisi2
	bl	FlashBchSel
	bl	ftl_flash_suspend
	mov	r0, #0
	b	.L1113
.L1099:
	mvn	r0, #1
.L1113:
	add	sp, sp, #20
	@ sp needed
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L1146:
	.align	2
.L1145:
	.word	.LANCHOR2
	.word	.LANCHOR0
	.word	IDByte
	.word	.LC14
	.word	.LANCHOR2-2732
	.word	1446522928
	.word	.LANCHOR1
	.word	.LANCHOR1+3412
	.word	.LANCHOR1+3288
	.word	.LANCHOR0+48
	.word	.LANCHOR1+472
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
	ldr	r5, .L1164
	mov	r4, r2
	ldr	r1, .L1164+4
	mov	r6, r0
	ldrb	r2, [r5, #-2744]	@ zero_extendqisi2
	ldr	r3, [r1, #44]
	mov	r8, r1
	cmp	r2, #0
	ldrb	r3, [r3, #19]	@ zero_extendqisi2
	beq	.L1148
	ldr	r2, [r5, #-1864]
	cmp	r2, #0
	ldmnefd	sp!, {r4, r5, r6, r7, r8, r9, r10, pc}
.L1148:
	sub	r2, r3, #5
	cmp	r3, #50
	cmpne	r2, #2
	bls	.L1149
	sub	r2, r3, #19
	tst	r2, #239
	moveq	r2, #1
	movne	r2, #0
	cmp	r3, #68
	movne	r3, r2
	orreq	r3, r2, #1
	cmp	r3, #0
	ldmeqfd	sp!, {r4, r5, r6, r7, r8, r9, r10, pc}
.L1149:
	ldr	r9, .L1164+8
	movw	r10, #65535
.L1151:
	ldr	r3, [r8, #44]
	ldrh	r3, [r3, #10]
	cmp	r3, r4
	bls	.L1163
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
	b	.L1151
.L1163:
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, pc}
.L1165:
	.align	2
.L1164:
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
	ldr	r3, .L1217
	stmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.save {r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.pad #36
	sub	sp, sp, #36
	ldrb	r3, [r3, #481]	@ zero_extendqisi2
	mov	r4, r0
	ldr	r10, .L1217+4
	mov	r9, #0
	str	r1, [sp, #16]
	str	r2, [sp, #20]
	str	r3, [sp, #12]
.L1167:
	ldr	r3, [sp, #16]
	cmp	r9, r3
	beq	.L1216
	ldr	r3, [sp, #16]
	mov	r0, r4
	ldr	r1, [sp, #20]
	add	r2, sp, #28
	rsb	r3, r9, r3
	ldr	r8, .L1217+8
	uxtb	r3, r3
	str	r3, [sp]
	add	r3, sp, #24
	bl	LogAddr2PhyAddr
	ldrb	r3, [r8, #3152]	@ zero_extendqisi2
	ldr	r5, [sp, #24]
	cmp	r5, r3
	mvncs	r3, #0
	strcs	r3, [r4]
	bcs	.L1169
	add	r5, r8, r5
	mov	r7, #0
	ldrb	r5, [r5, #3156]	@ zero_extendqisi2
	mov	r0, r5
	bl	NandcWaitFlashReady
	mov	r0, r5
	bl	NandcFlashCs
.L1170:
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
	ldr	r3, .L1217+4
	ldrb	r3, [r3, #-1760]	@ zero_extendqisi2
	cmp	r3, #0
	mov	r6, r0
	beq	.L1171
	mov	r0, r5
	bl	flash_read_ecc
	cmp	r0, #5
	movhi	r6, #256
.L1171:
	cmp	r7, #9
	cmnls	r6, #1
	moveq	r3, #1
	movne	r3, #0
	addeq	r7, r7, #1
	beq	.L1170
.L1172:
	cmp	r7, #0
	mov	r7, r3
	movne	r6, #256
.L1174:
	ldr	r3, [sp, #28]
	mov	r0, r5
	ldr	r1, [r8, #36]
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
	beq	.L1177
	mov	r0, r5
	bl	flash_read_ecc
	cmp	r0, #5
	movhi	fp, #256
.L1177:
	cmp	r7, #9
	cmnls	fp, #1
	addeq	r7, r7, #1
	beq	.L1174
.L1178:
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
	bls	.L1180
	cmn	r3, #1
	movne	r3, #256
.L1180:
	cmp	r3, #256
	cmnne	r3, #1
	movne	r3, #0
	str	r3, [r4]
	ldr	r3, [r4, #12]
	cmp	r3, #0
	beq	.L1183
	ldr	r2, [r3, #12]
	cmn	r2, #1
	bne	.L1183
	ldr	r2, [r3, #8]
	cmn	r2, #1
	bne	.L1183
	ldr	r3, [r3]
	cmn	r3, #1
	strne	r2, [r4]
.L1183:
	ldr	r3, [r4]
	cmn	r3, #1
	bne	.L1169
	ldr	r1, [r4, #4]
	ldr	r0, .L1217+12
	ldrb	r2, [r10, #-2743]	@ zero_extendqisi2
	bl	printk
	ldr	r1, [r4, #8]
	cmp	r1, #0
	beq	.L1185
	ldr	r0, .L1217+16
	mov	r2, #4
	mov	r3, #8
	bl	rknand_print_hex
.L1185:
	ldr	r1, [r4, #12]
	cmp	r1, #0
	beq	.L1169
	mov	r2, #4
	ldr	r0, .L1217+20
	mov	r3, r2
	bl	rknand_print_hex
.L1169:
	add	r9, r9, #1
	add	r4, r4, #36
	b	.L1167
.L1216:
	mov	r0, #0
	add	sp, sp, #36
	@ sp needed
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L1218:
	.align	2
.L1217:
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
	ldr	r3, .L1294
	stmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.save {r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.pad #52
	sub	sp, sp, #52
	ldr	r9, .L1294+4
	ldrb	r3, [r3, #481]	@ zero_extendqisi2
	str	r1, [sp, #20]
	ldrb	r8, [r9, #32]	@ zero_extendqisi2
	str	r3, [sp, #16]
	ldrb	r3, [r9, #40]	@ zero_extendqisi2
	cmp	r8, #0
	str	r2, [sp, #24]
	str	r3, [sp, #28]
	beq	.L1256
	bl	FlashReadSlc2KPages
	b	.L1289
.L1256:
	ldr	r7, .L1294+8
	mov	r10, r0
	mov	fp, r8
.L1220:
	ldr	r3, [sp, #20]
	cmp	fp, r3
	bcs	.L1292
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
	bcs	.L1223
	add	r3, r9, r3
	ldrb	r4, [r3, #3156]	@ zero_extendqisi2
	ldrb	r3, [r7, #-1759]	@ zero_extendqisi2
	cmp	r3, #0
	mov	r0, r4
	moveq	r5, #0
	bl	NandcWaitFlashReady
	ldr	r3, .L1294+4
	ldr	r3, [r3, #44]
	ldrb	r2, [r3, #19]	@ zero_extendqisi2
	sub	r3, r2, #1
	cmp	r3, #7
	bhi	.L1225
	sub	r2, r2, #7
	add	r1, r7, r4
	cmp	r2, #1
	ldr	r2, .L1294+8
	ldrb	r3, [r1, #-2720]	@ zero_extendqisi2
	add	r2, r2, r4
	ldrlsb	r3, [r1, #-2712]	@ zero_extendqisi2
	ldrb	r2, [r2, #-1880]	@ zero_extendqisi2
	cmp	r2, r3
	beq	.L1225
	mov	r0, r4
	ldrb	r1, [r7, #-2731]	@ zero_extendqisi2
	ldr	r2, .L1294+12
	bl	HynixSetRRPara
.L1225:
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
	beq	.L1227
	ldrb	r3, [r7, #-2744]	@ zero_extendqisi2
	cmp	r3, #0
	beq	.L1227
	bl	flash_enter_slc_mode
	b	.L1228
.L1227:
	bl	flash_exit_slc_mode
.L1228:
	ldr	r1, [sp, #44]
	cmn	r1, #1
	cmpeq	r4, #255
	moveq	r3, #0
	movne	r3, #1
	moveq	r5, r3
	beq	.L1230
	cmp	r5, #0
	beq	.L1231
	ldr	r2, [r9, #36]
	mov	r0, r4
	add	r2, r1, r2
	bl	FlashReadDpCmd
	b	.L1232
.L1231:
	mov	r0, r4
	bl	FlashReadCmd
.L1232:
	mov	r0, r4
	bl	NandcWaitFlashReady
	cmp	r5, #0
	beq	.L1230
	mov	r0, r4
	ldr	r1, [sp, #44]
	bl	FlashReadDpDataOutCmd
.L1230:
	ldr	r3, [r6, #12]
	mov	r0, r4
	ldr	r2, [sp, #16]
	mov	r1, #0
	str	r3, [sp]
	ldr	r3, [r6, #8]
	bl	NandcXferData
	ldrb	r2, [r9, #40]	@ zero_extendqisi2
	adds	r2, r2, #0
	movne	r2, #1
	cmn	r0, #1
	mov	ip, r0
	movne	r2, #0
	cmp	r2, #0
	movne	r3, #0
	strneb	r3, [r9, #40]
	movne	r5, r3
	bne	.L1228
.L1233:
	cmp	r5, #0
	beq	.L1234
	ldr	r3, .L1294+4
	str	r0, [sp, #32]
	mov	r0, r4
	str	r2, [sp, #36]
	ldr	r1, [r3, #36]
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
.L1234:
	mov	r0, r4
	str	ip, [sp, #32]
	bl	NandcFlashDeCs
	ldr	ip, [sp, #32]
	ldrb	r3, [sp, #28]	@ zero_extendqisi2
	cmn	ip, #1
	strb	r3, [r9, #40]
	bne	.L1241
	ldrb	r3, [r7, #-1860]	@ zero_extendqisi2
	cmp	r3, #0
	bne	.L1236
.L1240:
	ldr	r5, [r7, #-1796]
	cmp	r5, #0
	bne	.L1237
	b	.L1293
.L1236:
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
	beq	.L1239
	ldrb	r3, [r7, #-2743]	@ zero_extendqisi2
	cmp	r0, r3, lsr #1
	bls	.L1259
.L1239:
	ubfx	r0, r5, #8, #8
	str	ip, [sp, #32]
	bl	NandcSetDdrPara
	ldr	ip, [sp, #32]
	cmn	ip, #1
	beq	.L1240
	b	.L1259
.L1237:
	mov	r0, r4
	ldr	r1, [sp, #44]
	ldr	r2, [r6, #8]
	ldr	r3, [r6, #12]
	blx	r5
	cmn	r0, #1
	mov	ip, r0
	bne	.L1261
	ldr	r3, [r9, #44]
	ldrb	r3, [r3, #19]	@ zero_extendqisi2
	sub	r3, r3, #1
	cmp	r3, #7
	bhi	.L1243
	mov	r0, r4
	ldrb	r1, [r7, #-2731]	@ zero_extendqisi2
	ldr	r2, .L1294+12
	mov	r3, #0
	bl	HynixSetRRPara
.L1243:
	ldr	r1, [sp, #44]
	mov	r0, r4
	ldr	r2, [r6, #8]
	ldr	r3, [r6, #12]
	bl	FlashReadRawPage
	ldr	r1, [r6, #4]
	ldrb	r2, [r7, #-2743]	@ zero_extendqisi2
	mov	ip, r0
	ldr	r0, .L1294+16
	mov	r3, ip
	str	ip, [sp, #32]
	bl	printk
	ldr	ip, [sp, #32]
	cmn	ip, #1
	bne	.L1261
	ldrb	r5, [r7, #-2744]	@ zero_extendqisi2
	cmp	r5, #0
	beq	.L1242
	ldr	r3, [sp, #12]
	mov	r0, r4
	cmp	r3, #0
	beq	.L1244
	bl	flash_enter_slc_mode
	b	.L1245
.L1244:
	bl	flash_exit_slc_mode
.L1245:
	ldr	r3, .L1294+8
	mov	r0, r4
	ldr	r1, [sp, #44]
	ldr	r2, [r6, #8]
	ldr	ip, [r3, #-1796]
	ldr	r3, [r6, #12]
	blx	ip
	mov	ip, r0
	b	.L1261
.L1293:
	mov	r0, r4
	ldr	r1, [sp, #44]
	ldr	r2, [r6, #8]
	ldr	r3, [r6, #12]
	bl	FlashReadRawPage
	mov	ip, r0
	b	.L1242
.L1259:
	mov	r5, #0
.L1241:
	ldrb	r3, [r7, #-2743]	@ zero_extendqisi2
	add	r3, r3, r3, asl #1
	cmp	ip, r3, asr #2
	bls	.L1242
	ldr	r3, [r7, #-1796]
	cmp	r3, #0
	moveq	ip, #256
	b	.L1242
.L1261:
	mov	r5, #0
.L1242:
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
	bne	.L1249
	ldr	r1, [r6, #4]
	ldr	r0, .L1294+20
	ldrb	r2, [r7, #-2743]	@ zero_extendqisi2
	bl	printk
	ldr	r1, [r6, #12]
	cmp	r1, #0
	beq	.L1249
	mov	r2, #4
	ldr	r0, .L1294+24
	mov	r3, r2
	bl	rknand_print_hex
.L1249:
	cmp	r5, #0
	beq	.L1251
	ldrb	r3, [r7, #-2743]	@ zero_extendqisi2
	add	r3, r3, r3, asl #1
	cmp	r8, r3, asr #2
	bls	.L1252
	ldr	r3, [r7, #-1796]
	cmp	r3, #0
	moveq	r8, #256
.L1252:
	ldr	r3, [sp, #8]
	cmp	r8, #256
	cmnne	r8, #1
	add	r3, r3, #36
	movne	r2, #0
	streq	r8, [r10, r3]
	strne	r2, [r10, r3]
.L1251:
	ldr	r3, [sp, #12]
	add	fp, fp, r5
	cmp	r3, #0
	beq	.L1223
	ldrb	r3, [r7, #-2744]	@ zero_extendqisi2
	cmp	r3, #0
	beq	.L1223
	mov	r0, r4
	bl	flash_exit_slc_mode
.L1223:
	add	fp, fp, #1
	b	.L1220
.L1292:
	mov	r0, #0
.L1289:
	add	sp, sp, #52
	@ sp needed
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L1295:
	.align	2
.L1294:
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
	ldr	r5, .L1309
	.pad #52
	sub	sp, sp, #52
	mov	r2, #16
	ldr	r9, .L1309+4
	add	r0, r5, #1016
	ldr	fp, .L1309+8
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
.L1297:
	ldrb	r3, [fp, #3152]	@ zero_extendqisi2
	uxtb	r7, r5
	cmp	r3, r7
	bls	.L1308
	mul	ip, r6, r7
	sub	r4, r6, #1
	sub	r3, r6, #12
	uxth	r4, r4
.L1298:
	cmp	r4, r3
	ble	.L1300
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
	beq	.L1299
	ldr	r2, [r9, #-1772]
	ldrh	r1, [r2]
	movw	r2, #61664
	cmp	r1, r2
	bne	.L1299
	mov	r1, r7
	ldr	r0, .L1309+12
	mov	r2, r4
	mov	r7, r7, asl #1
	bl	printk
	ldr	r3, .L1309+16
	add	r8, r8, #1
	strh	r4, [r3, r7]	@ movhi
	uxth	r8, r8
	b	.L1300
.L1299:
	sub	r4, r4, #1
	uxth	r4, r4
	b	.L1298
.L1300:
	ldr	r3, .L1309+8
	add	r5, r5, #1
	ldrb	r3, [r3, #3152]	@ zero_extendqisi2
	cmp	r3, r8
	moveq	r10, #0
	b	.L1297
.L1308:
	mov	r0, r10
	add	sp, sp, #52
	@ sp needed
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L1310:
	.align	2
.L1309:
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
	ldr	r3, .L1326
	stmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, lr}
	.save {r4, r5, r6, r7, r8, r9, r10, lr}
	mov	r8, r2
	ldrh	r4, [r3, #14]
	.pad #40
	sub	sp, sp, #40
	ldrh	r2, [r3, #12]
	mov	r5, r0
	ldr	r9, .L1326+4
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
.L1312:
	cmp	r6, r4
	ble	.L1325
	mov	r1, #1
	add	r3, r6, r10
	add	r0, sp, #4
	mov	r2, r1
	mov	r3, r3, asl #10
	str	r3, [sp, #8]
	bl	FlashReadPages
	ldr	r3, [sp, #4]
	cmn	r3, #1
	beq	.L1313
	ldr	r3, [r9, #-1772]
	ldrh	r2, [r3]
	movw	r3, #61664
	cmp	r2, r3
	bne	.L1313
	cmp	r5, #0
	moveq	r0, r5
	beq	.L1314
	cmp	r7, #0
	ldreq	ip, .L1326+4
	moveq	lr, #1
	beq	.L1315
.L1317:
	ldr	r1, [r9, #-1868]
	mov	r2, r8
	mov	r0, r5
	bl	ftl_memcpy
	mov	r2, #4
	ldr	r0, .L1326+8
	mov	r1, r5
	mov	r3, r2
	bl	rknand_print_hex
	mov	r0, #0
	b	.L1314
.L1315:
	ldr	r3, [r9, #-1784]
	uxth	r4, r7
	add	r7, r7, #1
	cmp	r4, r3
	bcs	.L1317
	ldr	r1, [ip, #-1868]
	mov	r0, r4, lsr #5
	and	r3, r4, #31
	ldr	r2, [r1, r0, asl #2]
	orr	r3, r2, lr, asl r3
	str	r3, [r1, r0, asl #2]
	b	.L1315
.L1313:
	sub	r6, r6, #1
	uxth	r6, r6
	b	.L1312
.L1325:
	mvn	r0, #0
.L1314:
	add	sp, sp, #40
	@ sp needed
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, pc}
.L1327:
	.align	2
.L1326:
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
	ldr	r3, .L1339
	stmfd	sp!, {r4, r5, r6, r7, r8, lr}
	.save {r4, r5, r6, r7, r8, lr}
	mov	r5, r0
	ldr	r3, [r3, #44]
	ldr	r6, .L1339+4
	ldrb	r4, [r3, #13]	@ zero_extendqisi2
	ldrh	r3, [r3, #14]
	ldr	r0, [r6, #-1776]
	smulbb	r4, r4, r3
	uxth	r4, r4
	add	r2, r4, #7
	mov	r2, r2, asr #3
	bl	FlashReadFacBbtData
	cmn	r0, #1
	bne	.L1329
.L1333:
	mov	r3, #0
	b	.L1330
.L1329:
	mov	lr, r4, lsr #4
	mov	ip, #0
	sub	r4, r4, #1
	mov	r3, ip
	mov	r7, #1
.L1331:
	uxth	r0, ip
	cmp	r0, r4
	bge	.L1330
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
	bcc	.L1331
	b	.L1333
.L1330:
	mov	r3, r3, asl #1
	mvn	r2, #0
	mov	r0, #0
	strh	r2, [r5, r3]	@ movhi
	ldmfd	sp!, {r4, r5, r6, r7, r8, pc}
.L1340:
	.align	2
.L1339:
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
	ldr	r3, .L1371
	stmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.save {r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.pad #68
	sub	sp, sp, #68
	ldr	r8, .L1371+4
	mov	r6, r1
	ldrb	r3, [r3, #481]	@ zero_extendqisi2
	mov	r9, r2
	mov	r4, r0
	mov	r10, r0
	mov	r7, #0
	mov	fp, r8
	str	r3, [sp, #12]
.L1342:
	cmp	r7, r6
	beq	.L1369
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
	bcs	.L1344
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
	ldr	r1, [r8, #36]
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
	ldr	r1, [fp, #36]
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
.L1344:
	add	r7, r7, #1
	add	r10, r10, #36
	b	.L1342
.L1369:
	ldr	r5, .L1371+8
	mov	r7, #0
	mov	r8, r5
.L1349:
	cmp	r7, r6
	beq	.L1370
	ldr	r3, [r4]
	cmn	r3, #1
	bne	.L1350
	ldr	r1, [r4, #4]
	ldr	r0, .L1371+12
	bl	printk
	b	.L1351
.L1350:
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
	bne	.L1352
	ldr	r0, .L1371+16
	ldr	r1, [r4, #4]
	bl	printk
	str	r10, [r4]
.L1352:
	ldr	r10, [sp, #28]
	cmp	r10, #256
	bne	.L1353
	ldr	r0, .L1371+20
	ldr	r1, [r4, #4]
	bl	printk
	str	r10, [r4]
.L1353:
	ldr	r3, [r4, #12]
	cmp	r3, #0
	beq	.L1354
	ldr	r2, [r3]
	ldr	r3, [r8, #-1764]
	ldr	r3, [r3]
	cmp	r2, r3
	beq	.L1354
	ldr	r0, .L1371+24
	ldr	r1, [r4, #4]
	bl	printk
	mvn	r3, #0
	str	r3, [r4]
.L1354:
	ldr	r3, [r4, #8]
	cmp	r3, #0
	beq	.L1351
	ldr	r2, [r3]
	ldr	r3, [r8, #-1768]
	ldr	r3, [r3]
	cmp	r2, r3
	beq	.L1351
	ldr	r0, .L1371+28
	ldr	r1, [r4, #4]
	bl	printk
	mvn	r3, #0
	str	r3, [r4]
.L1351:
	add	r7, r7, #1
	add	r4, r4, #36
	b	.L1349
.L1370:
	mov	r0, #0
	add	sp, sp, #68
	@ sp needed
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L1372:
	.align	2
.L1371:
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
	ldr	r6, .L1429
	mov	r4, r0
	str	r1, [sp, #8]
	mov	r9, r2
	str	r3, [sp, #20]
	ldr	ip, [r6, #44]
	ldrb	r8, [r6, #32]	@ zero_extendqisi2
	ldrb	ip, [ip, #19]	@ zero_extendqisi2
	cmp	r8, #0
	str	ip, [sp, #16]
	ldr	ip, .L1429+4
	ldrb	ip, [ip, #481]	@ zero_extendqisi2
	str	ip, [sp, #12]
	beq	.L1374
	bl	FlashProgSlc2KPages
	b	.L1375
.L1387:
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
	bcc	.L1426
.L1377:
	add	r8, r8, #1
.L1374:
	ldr	r3, [sp, #8]
	ldr	r5, .L1429
	cmp	r8, r3
	bcc	.L1387
	b	.L1427
.L1426:
	ldr	r3, .L1429+8
	ldrb	r3, [r3, #-1874]	@ zero_extendqisi2
	cmp	r3, #0
	add	r3, r6, r0, asl #4
	ldr	r3, [r3, #3204]
	moveq	r10, #0
	cmp	r3, #0
	beq	.L1379
	uxtb	r0, r0
	bl	FlashWaitCmdDone
.L1379:
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
	bne	.L1381
	bl	NandcWaitFlashReady
	b	.L1382
.L1381:
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
.L1382:
	ldr	r3, [sp, #16]
	sub	r3, r3, #1
	cmp	r3, #7
	bhi	.L1383
	ldr	r3, .L1429+8
	add	r3, r3, r5
	ldrb	r3, [r3, #-1880]	@ zero_extendqisi2
	cmp	r3, #0
	beq	.L1383
	ldr	r3, .L1429+8
	mov	r0, r5
	ldr	r2, .L1429+12
	ldrb	r1, [r3, #-2731]	@ zero_extendqisi2
	mov	r3, #0
	bl	HynixSetRRPara
.L1383:
	mov	r0, r5
	bl	NandcFlashCs
	cmp	r9, #1
	mov	r0, r5
	bne	.L1384
	ldr	r3, .L1429+8
	ldrb	r3, [r3, #-2744]	@ zero_extendqisi2
	cmp	r3, #0
	beq	.L1384
	bl	flash_enter_slc_mode
	b	.L1385
.L1384:
	bl	flash_exit_slc_mode
.L1385:
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
	beq	.L1386
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
	ldr	r1, [r6, #36]
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
.L1386:
	mov	r0, r5
	ldr	r1, [sp, #28]
	bl	FlashProgSecondCmd
	mov	r0, r5
	bl	NandcFlashDeCs
	add	r8, r8, r10
	b	.L1377
.L1427:
	ldr	r7, .L1429+8
	mov	r6, #0
	ldr	r8, .L1429+16
.L1388:
	ldrb	r3, [r5, #3152]	@ zero_extendqisi2
	cmp	r6, r3
	bcs	.L1428
	uxtb	r0, r6
	bl	FlashWaitCmdDone
	cmp	r9, #1
	bne	.L1389
	ldrb	r3, [r7, #-2744]	@ zero_extendqisi2
	cmp	r3, #0
	beq	.L1389
	ldrb	r0, [r8, r6, asl #4]	@ zero_extendqisi2
	bl	flash_exit_slc_mode
.L1389:
	add	r6, r6, #1
	b	.L1388
.L1428:
	ldr	r3, [sp, #20]
	cmp	r3, #0
	bne	.L1391
.L1399:
	mov	r0, #0
	b	.L1375
.L1391:
	ldr	r5, .L1429+8
	mov	r6, #0
	mov	r7, r5
.L1392:
	ldr	r3, [sp, #8]
	cmp	r6, r3
	beq	.L1399
	ldr	r3, [r4]
	cmn	r3, #1
	bne	.L1393
	ldr	r1, [r4, #4]
	ldr	r0, .L1429+20
	bl	printk
	b	.L1394
.L1393:
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
	bne	.L1395
	ldr	r0, .L1429+24
	ldr	r1, [r4, #4]
	bl	printk
	str	r8, [r4]
.L1395:
	ldr	r3, [r4, #12]
	cmp	r3, #0
	beq	.L1396
	ldr	r2, [r3]
	ldr	r3, [r7, #-1764]
	ldr	r3, [r3]
	cmp	r2, r3
	beq	.L1396
	ldr	r0, .L1429+28
	ldr	r1, [r4, #4]
	bl	printk
	mvn	r3, #0
	str	r3, [r4]
.L1396:
	ldr	r3, [r4, #8]
	cmp	r3, #0
	beq	.L1394
	ldr	r2, [r3]
	ldr	r3, [r7, #-1768]
	ldr	r3, [r3]
	cmp	r2, r3
	beq	.L1394
	ldr	r0, .L1429+32
	ldr	r1, [r4, #4]
	bl	printk
	mvn	r3, #0
	str	r3, [r4]
.L1394:
	add	r6, r6, #1
	add	r4, r4, #36
	b	.L1392
.L1375:
	add	sp, sp, #76
	@ sp needed
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L1430:
	.align	2
.L1429:
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
	ldr	r5, .L1435
	ldr	r3, [r5, #-1784]
	cmp	r0, r3
	movcc	r4, #0
	bcc	.L1432
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
.L1432:
	mov	r0, r4
	add	sp, sp, #108
	@ sp needed
	ldmfd	sp!, {r4, r5, pc}
.L1436:
	.align	2
.L1435:
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
	ldr	r4, .L1493
	mov	r1, #1
	ldr	r5, .L1493+4
	ldr	r0, .L1493+8
	ldr	r3, [r4, #-1772]
	ldrh	r8, [r5, #12]
	ldr	r7, .L1493+12
	str	r3, [sp, #20]
	ldrh	r3, [r5, #14]
	smulbb	r8, r8, r3
	ldr	r3, .L1493+12
	ldr	r2, [r3, #44]
	uxth	r8, r8
	ldrb	r2, [r2, #24]	@ zero_extendqisi2
	str	r2, [sp, #24]
	ldrh	r2, [r3, #36]
	ldrb	r3, [r3, #32]	@ zero_extendqisi2
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
	ldr	r4, .L1493
	mov	r3, r8, lsr #4
	str	r3, [sp, #28]
	mov	r3, #0
	str	r3, [sp, #8]
.L1439:
	ldrb	r6, [sp, #8]	@ zero_extendqisi2
	ldrb	r3, [r7, #3152]	@ zero_extendqisi2
	cmp	r3, r6
	bls	.L1489
	ldr	r3, .L1493+16
	mov	r2, r6, asl #1
	ldrh	r5, [r3, r2]
	cmp	r5, #0
	bne	.L1469
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
.L1441:
	ldrh	r3, [sp, #4]
	cmp	r3, r8
	str	r3, [sp, #12]
	bcs	.L1451
	mvn	r3, #0
	strb	r3, [sp, #42]
	strb	r3, [sp, #43]
	ldr	r3, [sp, #24]
	tst	r3, #1
	beq	.L1443
	ldr	r3, [fp, #3164]
	add	r2, sp, #42
	mov	r0, r10
	add	r3, r5, r3
	str	r3, [sp, #36]
	mov	r1, r3
	bl	FlashReadSpare
	ldrb	r2, [r7, #32]	@ zero_extendqisi2
	cmp	r2, #1
	ldr	r3, [sp, #36]
	bne	.L1443
	ldr	r1, [r7, #36]
	add	r2, sp, #43
	mov	r0, r10
	add	r1, r3, r1
	bl	FlashReadSpare
	ldrb	r3, [sp, #42]	@ zero_extendqisi2
	ldrb	r2, [sp, #43]	@ zero_extendqisi2
	and	r3, r3, r2
	strb	r3, [sp, #42]
.L1443:
	ldr	r3, [sp, #24]
	tst	r3, #2
	beq	.L1445
	ldr	r3, [r7, #44]
	mov	r0, r10
	add	r2, sp, #43
	ldrh	r1, [r3, #10]
	ldr	r3, [fp, #3164]
	sub	r1, r1, #1
	add	r1, r1, r3
	add	r1, r1, r5
	bl	FlashReadSpare
.L1445:
	ldr	r2, [r7, #44]
	ldrb	r3, [r2, #7]	@ zero_extendqisi2
	cmp	r3, #1
	cmpne	r3, #8
	ldrb	r3, [sp, #42]	@ zero_extendqisi2
	beq	.L1446
	ldrb	r2, [r2, #18]	@ zero_extendqisi2
	cmp	r2, #12
	bne	.L1447
.L1446:
	cmp	r3, #0
	ldrneb	r0, [sp, #43]	@ zero_extendqisi2
	clzne	r0, r0
	movne	r0, r0, lsr #5
	bne	.L1448
	b	.L1468
.L1447:
	cmp	r3, #255
	bne	.L1468
	ldrb	r0, [sp, #43]	@ zero_extendqisi2
	subs	r0, r0, #255
	movne	r0, #1
	b	.L1448
.L1468:
	mov	r0, #1
.L1448:
	ldr	r3, [sp, #24]
	tst	r3, #4
	beq	.L1449
	ldr	r3, .L1493+12
	mov	r0, r10
	ldr	r2, [sp, #32]
	add	r3, r3, r2
	ldr	r1, [r3, #3164]
	add	r1, r5, r1
	bl	SandiskProgTestBadBlock
.L1449:
	cmp	r0, #0
	beq	.L1450
	mov	r1, r6
	ldr	r2, [sp, #4]
	ldr	r0, .L1493+20
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
	bgt	.L1451
.L1450:
	ldr	r3, [sp, #4]
	add	r3, r3, #1
	str	r3, [sp, #4]
	ldr	r3, [sp, #16]
	add	r5, r5, r3
	b	.L1441
.L1451:
	mov	r2, r9
	ldr	r0, .L1493+24
	mov	r1, r6
	bl	printk
	ldrb	r3, [r7, #3152]	@ zero_extendqisi2
	ldr	r2, [sp, #28]
	mul	r3, r3, r2
	cmp	r9, r3
	blt	.L1453
	ldr	r3, .L1493+4
	mov	r1, #0
	ldr	r0, [r4, #-1868]
	ldrh	r2, [r3, #20]
	mov	r2, r2, asl #9
	bl	ftl_memset
.L1453:
	cmp	r6, #0
	bne	.L1455
	ldr	r3, [r4, #-1784]
	mov	r5, r6
	mov	r9, #1
	uxth	r10, r3
.L1456:
	ldr	r3, .L1493+12
	ldrb	r3, [r3, #33]	@ zero_extendqisi2
	cmp	r3, r10
	bls	.L1490
	mov	r0, r10
	bl	FlashTestBlk
	cmp	r0, #0
	beq	.L1457
	mov	r1, r10
	ldr	r0, .L1493+28
	bl	printk
	ldr	r1, [r4, #-1868]
	mov	r0, r10, lsr #5
	add	r5, r5, #1
	and	r3, r10, #31
	ldr	r2, [r1, r0, asl #2]
	uxth	r5, r5
	orr	r3, r2, r9, asl r3
	str	r3, [r1, r0, asl #2]
.L1457:
	add	r10, r10, #1
	uxth	r10, r10
	b	.L1456
.L1490:
	sub	fp, r8, #1
	sub	r9, r8, #50
	mov	r10, #1
	uxth	fp, fp
.L1459:
	cmp	fp, r9
	ble	.L1491
	mov	r0, fp
	bl	FlashTestBlk
	cmp	r0, #0
	beq	.L1460
	mov	r1, fp
	ldr	r0, .L1493+28
	bl	printk
	ldr	r1, [r4, #-1868]
	mov	r0, fp, lsr #5
	and	r3, fp, #31
	ldr	r2, [r1, r0, asl #2]
	orr	r3, r2, r10, asl r3
	str	r3, [r1, r0, asl #2]
.L1460:
	sub	fp, fp, #1
	uxth	fp, fp
	b	.L1459
.L1491:
	ldr	r3, .L1493+12
	ldr	r2, [r4, #-1784]
	ldrb	r3, [r3, #33]	@ zero_extendqisi2
	rsb	r3, r2, r3
	cmp	r5, r3
	bcc	.L1455
	ldr	r3, .L1493+4
	mov	r1, #0
	ldr	r0, [r4, #-1868]
	ldrh	r2, [r3, #20]
	mov	r2, r2, asl #9
	bl	ftl_memset
.L1455:
	ldrb	r5, [sp, #8]	@ zero_extendqisi2
	sub	r10, r8, #1
	ldr	r9, .L1493+16
	uxth	r10, r10
	mul	r5, r8, r5
	add	r9, r9, r6, asl #1
.L1463:
	mov	r1, r6
	ldr	r0, .L1493+32
	mov	r2, r10
	bl	printk
	ldr	r1, [r4, #-1868]
.L1464:
	mov	r2, r10, lsr #5
	and	r3, r10, #31
	ldr	r2, [r1, r2, asl #2]
	mov	r3, r2, lsr r3
	ands	r3, r3, #1
	subne	r10, r10, #1
	uxthne	r10, r10
	bne	.L1464
.L1492:
	ldr	r1, [sp, #20]
	add	r0, sp, #44
	ldr	r2, .L1493+36
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
	bne	.L1463
.L1469:
	ldr	r3, [sp, #8]
	add	r3, r3, #1
	str	r3, [sp, #8]
	b	.L1439
.L1489:
	add	sp, sp, #84
	@ sp needed
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L1494:
	.align	2
.L1493:
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
.L1496:
	cmp	r2, r0
	uxth	r3, r1
	add	r1, r1, #1
	movls	r2, r2, asl #1
	bls	.L1496
.L1498:
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
	ldr	r3, .L1502
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
.L1503:
	.align	2
.L1502:
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
	ldr	r4, .L1531
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
.L1505:
	strb	r10, [r10, r3]
	add	r10, r10, #1
	cmp	r10, #32
	bne	.L1505
	ldrh	r2, [ip, #14]
	ldrh	r3, [ip, #20]
	cmp	r3, r2, lsr #8
	bcs	.L1506
	sub	r3, r5, #1
	uxtb	r9, r7
	mul	r3, r7, r3
	mov	fp, r9, asl #1
	uxtb	fp, fp
	str	r3, [sp, #4]
	mov	r3, #0
.L1507:
	cmp	r3, r7
	bcs	.L1509
	ldr	r1, [sp, #4]
	uxtb	r2, r3
	ldr	r0, .L1531+4
	add	r1, r3, r1
	ldr	lr, .L1531+4
	add	r1, r0, r1
	str	r1, [sp, #8]
	mov	r1, #0
	rsb	r0, r7, r3
	mov	r10, r1
	add	r0, lr, r0
	str	r0, [sp, #12]
.L1510:
	cmp	r10, r5
	add	r1, r1, r7
	bcs	.L1530
	ldr	r0, [sp, #12]
	add	r10, r10, #1
	strb	r2, [r0, r1]
	add	r0, r2, r9
	add	r2, r2, fp
	mov	lr, r0
	ldr	r0, [sp, #8]
	uxtb	r2, r2
	strb	lr, [r0, r1]
	b	.L1510
.L1530:
	add	r3, r3, #1
	b	.L1507
.L1509:
	mov	r5, r5, asl #1
	mov	r6, r6, lsr #1
	strh	r5, [r4, #-2]	@ movhi
	strh	r6, [r4, #-14]	@ movhi
.L1506:
	ldr	r6, .L1531+8
	cmp	r8, #1
	mov	r3, #5
	ldrh	r9, [ip, #16]
	ldr	r5, .L1531+12
	strh	r3, [r6, #-12]	@ movhi
	mov	r3, #0
	streqh	r8, [r6, #-12]	@ movhi
	sub	r10, r5, #1728
	ldrh	r8, [r4, #-2]
	strh	r3, [r6, #-10]	@ movhi
	mov	r3, #4352
	strh	r3, [r6, #-8]	@ movhi
	ldr	r3, .L1531+16
	smulbb	r8, r8, r7
	ldrh	r4, [r4, #-14]
	ldrb	fp, [r3, #32]	@ zero_extendqisi2
	ldrh	r1, [ip, #18]
	cmp	fp, #0
	strh	r9, [r6, #-4]	@ movhi
	smulbb	r7, r4, r7
	ldrne	r3, .L1531+8
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
	ldr	fp, .L1531+12
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
	ldr	r4, .L1531+12
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
	bne	.L1520
.L1519:
	cmp	r2, #7
	movls	r3, #8
	strls	r3, [r4, #-1640]
.L1520:
	ldrh	r2, [r10]
	mov	r3, #0
	strh	r3, [r9, #-12]	@ movhi
	mov	r0, #0
	mov	r3, r2, lsr #3
	add	r3, r3, r2, asl #1
	add	r3, r3, #52
	add	r8, r3, r8, asl #2
	cmp	r8, r6, asl #9
	ldrcc	r3, .L1531+20
	movcc	r2, #1
	strcch	r2, [r3, #-12]	@ movhi
	add	sp, sp, #20
	@ sp needed
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L1532:
	.align	2
.L1531:
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
	ldr	r4, .L1638
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
	ldr	r5, .L1638+4
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
.L1535:
	cmp	r3, ip
	bcs	.L1636
	ldr	r0, [r4, #-1320]
	add	r3, r3, #1
	add	r0, r0, r1
	add	r1, r1, r2
	str	r0, [r5, #4]!
	b	.L1535
.L1636:
	ldr	r2, .L1638+8
	mov	r1, #0
.L1537:
	cmp	r3, #8
	addne	r0, r2, r3, asl #2
	addne	r3, r3, #1
	strne	r1, [r0, #28]
	bne	.L1537
.L1637:
	ldr	r2, [r4, #-1404]
	ldr	r3, .L1638
	cmp	r2, #0
	bne	.L1539
.L1541:
	ldr	r0, .L1638+12
	ldr	r1, .L1638+16
	bl	printk
	mvn	r0, #0
	b	.L1540
.L1539:
	ldr	r2, [r3, #-1400]
	cmp	r2, #0
	beq	.L1541
	ldr	r2, [r3, #-1376]
	cmp	r2, #0
	beq	.L1541
	ldr	r2, [r3, #-1368]
	cmp	r2, #0
	beq	.L1541
	ldr	r2, [r3, #-1364]
	cmp	r2, #0
	beq	.L1541
	ldr	r2, [r3, #-1360]
	cmp	r2, #0
	beq	.L1541
	ldr	r2, [r3, #-1356]
	cmp	r2, #0
	beq	.L1541
	ldr	r2, [r3, #-1320]
	cmp	r2, #0
	beq	.L1541
	ldr	r3, [r3, #-1408]
	cmp	r3, #0
	beq	.L1541
	ldr	r2, [r4, #-1512]
	ldr	r3, .L1638
	cmp	r2, #0
	beq	.L1541
	ldr	r2, [r3, #-1508]
	cmp	r2, #0
	beq	.L1541
	ldr	r2, [r3, #-1504]
	cmp	r2, #0
	beq	.L1541
	ldr	r2, [r3, #-1496]
	cmp	r2, #0
	beq	.L1541
	ldr	r2, [r3, #-1492]
	cmp	r2, #0
	beq	.L1541
	ldr	r2, [r3, #-1488]
	cmp	r2, #0
	beq	.L1541
	ldr	r2, [r3, #-1500]
	cmp	r2, #0
	beq	.L1541
	ldr	r2, [r3, #-1476]
	cmp	r2, #0
	beq	.L1541
	ldr	r2, [r3, #-1472]
	cmp	r2, #0
	beq	.L1541
	ldr	r3, [r3, #-1468]
	cmp	r3, #0
	beq	.L1541
	ldr	r2, [r4, #-1464]
	ldr	r3, .L1638
	cmp	r2, #0
	beq	.L1541
	ldr	r2, [r3, #-1456]
	cmp	r2, #0
	beq	.L1541
	ldr	r2, [r3, #-1452]
	cmp	r2, #0
	beq	.L1541
	ldr	r2, [r3, #-1448]
	cmp	r2, #0
	beq	.L1541
	ldr	r2, [r3, #-1444]
	cmp	r2, #0
	beq	.L1541
	ldr	r2, [r3, #-1440]
	cmp	r2, #0
	beq	.L1541
	ldr	r2, [r3, #-1436]
	cmp	r2, #0
	beq	.L1541
	ldr	r2, [r3, #-1416]
	cmp	r2, #0
	beq	.L1541
	ldr	r2, [r3, #-1424]
	cmp	r2, #0
	beq	.L1541
	ldr	r3, [r3, #-1392]
	cmp	r3, #0
	beq	.L1541
	ldr	r3, .L1638
	ldr	r2, [r3, #-1388]
	cmp	r2, #0
	beq	.L1541
	ldr	r2, [r3, #-1384]
	cmp	r2, #0
	beq	.L1541
	ldr	r3, [r3, #-1380]
	cmp	r3, #0
	beq	.L1541
	mov	r0, #0
.L1540:
	add	sp, sp, #12
	@ sp needed
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L1639:
	.align	2
.L1638:
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
	ldr	r2, .L1647
	sub	r3, r2, #1280
	ldrh	r3, [r3, #-8]
	cmp	r3, #0
	beq	.L1646
	ldr	r3, [r2, #-1392]
	sub	r2, r2, #1648
	ldrh	r2, [r2]
	add	r2, r3, r2, asl #1
.L1642:
	cmp	r3, r2
	beq	.L1646
	ldrh	r1, [r3], #2
	cmp	r1, r0
	bne	.L1642
	mov	r0, #1
	bx	lr
.L1646:
	mov	r0, #0
	bx	lr
.L1648:
	.align	2
.L1647:
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
	ldr	r2, .L1656
	ldr	r3, [r2, #-1516]
	cmp	r3, #0
	beq	.L1652
	stmfd	sp!, {r4, r5, lr}
	.save {r4, r5, lr}
	mov	r5, #36
	ldr	r4, [r2, #-1484]
	mov	r2, #0
.L1651:
	mla	ip, r5, r2, r4
	ldr	lr, [ip, #16]
	cmp	lr, r0
	movcc	ip, #0
	movcs	ip, #1
	cmp	lr, r1
	movhi	ip, #0
	cmp	ip, #0
	bne	.L1653
	add	r2, r2, #1
	cmp	r2, r3
	bne	.L1651
	mov	r0, ip
	ldmfd	sp!, {r4, r5, pc}
.L1652:
	mov	r0, r3
	bx	lr
.L1653:
	mov	r0, #1
	ldmfd	sp!, {r4, r5, pc}
.L1657:
	.align	2
.L1656:
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
	ldr	r3, .L1659
	ldr	r0, [r3, #-2740]
	bx	lr
.L1660:
	.align	2
.L1659:
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
	ldr	r3, .L1662
	ldr	r0, [r3, #-2740]
	bx	lr
.L1663:
	.align	2
.L1662:
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
	ldr	r3, .L1665
	ldr	r0, [r3, #-2740]
	bx	lr
.L1666:
	.align	2
.L1665:
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
	ldr	r3, .L1668
	ldr	r0, [r3, #-1284]
	bx	lr
.L1669:
	.align	2
.L1668:
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
	ldr	r5, .L1672
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
	ldr	r0, .L1672+4
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
.L1673:
	.align	2
.L1672:
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
	ldr	r5, .L1676
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
.L1677:
	.align	2
.L1676:
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
	ldr	r5, .L1685
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
.L1681:
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
	bne	.L1681
	ldmfd	sp!, {r4, r5, r6, pc}
.L1686:
	.align	2
.L1685:
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
	ldr	r4, .L1705
	ldr	r5, [r4, #-1280]
	cmp	r5, #0
	bne	.L1689
	ldr	r3, [r4, #-1444]
	mov	r1, r5
	ldr	r0, [r4, #-1476]
	sub	r8, r4, #1344
	ldr	r6, .L1705+4
	mov	r10, r4
	str	r3, [r4, #-1264]
	sub	r3, r4, #1648
	str	r0, [r4, #-1268]
	sub	fp, r6, #28
	ldrh	r2, [r3, #-10]
	bl	ftl_memset
	ldr	r9, .L1705+8
.L1690:
	ldrh	r3, [r9]
	ldr	r7, .L1705
	cmp	r5, r3
	bge	.L1704
	ldrh	r2, [fp]
	ldr	r3, [r10, #-1268]
	ldr	r1, [r6, #4]!
	mul	r0, r2, r5
	mov	r2, r2, asl #2
	add	r5, r5, #1
	add	r0, r3, r0, asl #2
	bl	ftl_memcpy
	b	.L1690
.L1704:
	ldr	r6, [r7, #-1264]
	mov	r1, #255
	mov	r2, #16
	ldr	r5, .L1705+12
	mov	r0, r6
	bl	ftl_memset
	ldr	r3, .L1705+16
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
.L1692:
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
	ldr	r0, .L1705+20
	bl	printk
	ldr	r3, .L1705+24
	ldrh	r2, [r5, #2]
	ldrh	r3, [r3]
	sub	r3, r3, #1
	cmp	r2, r3
	blt	.L1693
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
.L1693:
	mov	r1, #1
	ldr	r0, .L1705+28
	mov	r3, r1
	mov	r2, r1
	bl	FlashProgPages
	ldrh	r3, [r10, #2]
	ldr	fp, .L1705
	add	r3, r3, #1
	strh	r3, [r10, #2]	@ movhi
	ldr	r3, [r4, #-1276]
	cmn	r3, #1
	bne	.L1694
	add	r8, r8, #1
	ldr	r0, .L1705+32
	ldr	r1, [r7, #-1272]
	uxth	r8, r8
	bl	printk
	cmp	r8, #3
	bls	.L1692
	ldr	r0, .L1705+36
	mov	r2, r8
	ldr	r1, [fp, #-1272]
	bl	printk
	mov	r3, #1
	str	r3, [fp, #-1280]
	b	.L1689
.L1694:
	add	r9, r9, #1
	cmp	r9, #1
	beq	.L1692
	cmp	r3, #256
	beq	.L1692
.L1689:
	mov	r0, #0
	add	sp, sp, #12
	@ sp needed
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L1706:
	.align	2
.L1705:
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
	ldr	r2, .L1719
	stmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, lr}
	.save {r4, r5, r6, r7, r8, r9, r10, lr}
	mov	r5, #0
	ldr	r6, .L1719+4
	mov	r9, r2
	ldr	r3, [r2, #-1476]
	ldr	r7, [r2, #-1444]
	sub	r8, r6, #376
	str	r3, [r2, #-1268]
	str	r7, [r2, #-1264]
.L1708:
	ldrh	r3, [r8]
	cmp	r5, r3
	bcs	.L1718
	ldr	r2, .L1719+8
	mvn	r3, #0
	strh	r3, [r6, #2]!	@ movhi
	ldrh	r3, [r2]
	mov	r10, r2
	sub	r3, r3, #1
	uxth	r4, r3
.L1709:
	ldrh	r3, [r10]
	sub	r2, r3, #16
	cmp	r4, r2
	ble	.L1711
	mla	r3, r3, r5, r4
	mov	r1, #1
	ldr	r0, .L1719+12
	mov	r2, r1
	mov	r3, r3, asl #10
	str	r3, [r9, #-1272]
	bl	FlashReadPages
	ldr	r3, [r9, #-1276]
	cmn	r3, #1
	beq	.L1710
	ldrh	r2, [r7]
	movw	r3, #61664
	cmp	r2, r3
	streqh	r4, [r6]	@ movhi
	beq	.L1711
.L1710:
	sub	r4, r4, #1
	uxth	r4, r4
	b	.L1709
.L1711:
	add	r5, r5, #1
	b	.L1708
.L1718:
	mov	r0, #0
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, pc}
.L1720:
	.align	2
.L1719:
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
	ldr	r0, .L1722
	mvn	r2, #0
	mov	r1, #255
	add	r0, r0, #8
	strh	r2, [r0, #-12]	@ movhi
	mov	r2, #0
	strh	r2, [r0, #-6]	@ movhi
	mov	r2, #16
	b	ftl_memset
.L1723:
	.align	2
.L1722:
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
	ldr	r4, .L1733
	ldrh	r6, [r4, #-6]
	ldrh	r3, [r4, #-50]
	mov	r4, r5
	mul	r6, r3, r6
.L1725:
	uxth	r0, r5
	cmp	r0, r6
	bge	.L1732
	bl	FtlBbmIsBadBlock
	add	r5, r5, #1
	cmp	r0, #0
	addne	r4, r4, #1
	uxthne	r4, r4
	b	.L1725
.L1732:
	mov	r0, r4
	ldmfd	sp!, {r4, r5, r6, pc}
.L1734:
	.align	2
.L1733:
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
	ldr	r7, .L1758
	ldr	r5, [r7, #-1280]
	cmp	r5, #0
	bne	.L1736
	bl	FtlBbtMemInit
	ldr	r8, .L1758+4
	bl	FtlLoadFactoryBbt
	mov	r4, r7
	sub	r9, r8, #18
.L1737:
	ldr	r6, .L1758+8
	ldrh	r3, [r6]
	cmp	r5, r3
	bcs	.L1756
	ldrh	r3, [r9, #2]!
	movw	r2, #65535
	ldr	r0, [r4, #-1476]
	ldr	r10, [r4, #-1444]
	cmp	r3, r2
	str	r0, [r4, #-1268]
	str	r10, [r4, #-1264]
	beq	.L1738
	ldrh	fp, [r6, #44]
	mov	r1, #1
	mov	r2, r1
	ldr	r0, .L1758+12
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
	b	.L1739
.L1738:
	mov	r1, r5
	bl	FlashGetBadBlockList
	ldr	r0, [r4, #-1268]
	ldr	r1, [r8]
	bl	FtlBbt2Bitmap
	ldrh	r6, [r6, #44]
.L1741:
	sub	r6, r6, #1
	uxth	r6, r6
.L1740:
	ldr	fp, .L1758+16
	ldrh	r0, [fp]
	smlabb	r0, r0, r5, r6
	uxth	r0, r0
	bl	FtlBbmIsBadBlock
	cmp	r0, #1
	beq	.L1741
	mov	r1, #0
	mov	r2, #16
	strh	r6, [r9]	@ movhi
	ldr	r0, [r4, #-1444]
	bl	ftl_memset
	ldr	r3, .L1758+20
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
	ldr	r3, .L1758+24
	ldrh	r2, [r3]
	mov	r2, r2, asl #2
	bl	ftl_memcpy
	mov	r1, #1
	mov	r2, r1
	ldr	r0, .L1758+12
	bl	FlashEraseBlocks
	mov	r1, #1
	mov	r3, r1
	ldr	r0, .L1758+12
	mov	r2, r1
	bl	FlashProgPages
	ldr	r3, [r4, #-1276]
	cmn	r3, #1
	bne	.L1739
	uxth	r0, fp
	bl	FtlBbmMapBadBlock
	b	.L1740
.L1739:
	uxth	r0, fp
	add	r5, r5, #1
	bl	FtlBbmMapBadBlock
	add	r8, r8, #4
	b	.L1737
.L1756:
	add	r6, r6, #60
	mov	r4, #0
.L1744:
	ldrh	r3, [r6]
	uxth	r0, r4
	add	r4, r4, #1
	cmp	r3, r0
	bls	.L1757
	bl	FtlBbmMapBadBlock
	b	.L1744
.L1757:
	ldr	r6, .L1758+28
	movw	r8, #65535
	ldrh	r4, [r6, #12]
	sub	r4, r4, #1
	uxth	r4, r4
.L1746:
	ldrh	r3, [r6, #12]
	ldr	r5, .L1758+28
	sub	r3, r3, #48
	cmp	r4, r3
	ble	.L1750
	mov	r0, r4
	bl	FtlBbmIsBadBlock
	cmp	r0, #1
	beq	.L1747
	mov	r0, r4
	bl	FlashTestBlk
	cmp	r0, #0
	beq	.L1748
	mov	r0, r4
	bl	FtlBbmMapBadBlock
	b	.L1747
.L1748:
	ldrh	r3, [r6]
	cmp	r3, r8
	streqh	r4, [r6]	@ movhi
.L1749:
	strneh	r4, [r5, #4]	@ movhi
	bne	.L1750
.L1747:
	sub	r4, r4, #1
	uxth	r4, r4
	b	.L1746
.L1750:
	ldr	r3, .L1758+32
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
	ldr	r3, .L1758+32
	ldrh	r0, [r3, #-4]
	bl	FtlBbmMapBadBlock
	ldrh	r0, [r5, #4]
	bl	FtlBbmMapBadBlock
	bl	FtlBbmTblFlush
	ldr	r3, [r7, #-1340]
	ldrh	r2, [r5, #4]
	add	r3, r3, #1
	str	r3, [r7, #-1340]
	ldr	r3, .L1758+32
	ldr	r1, .L1758+32
	strh	r4, [r5, #2]	@ movhi
	ldrh	r3, [r3, #-4]
	strh	r2, [r1, #-4]	@ movhi
	strh	r3, [r5, #4]	@ movhi
	bl	FtlBbmTblFlush
.L1736:
	mov	r0, #0
	ldmfd	sp!, {r3, r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L1759:
	.align	2
.L1758:
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
	ldr	r4, .L1762
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
.L1763:
	.align	2
.L1762:
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
	ldr	r3, .L1766
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
.L1767:
	.align	2
.L1766:
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
	ldr	r4, .L1770
	sub	r3, r4, #1664
	sub	r4, r4, #1712
	ldrh	r1, [r3, #-6]
	bl	__aeabi_uidivmod
	uxth	r0, r1
	ldrh	r1, [r4]
	bl	__aeabi_uidiv
	uxth	r0, r0
	ldmfd	sp!, {r4, pc}
.L1771:
	.align	2
.L1770:
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
	bls	.L1773
	rsb	r0, r1, r0
	cmp	r0, #-2147483648
	movhi	r0, #0
	movls	r0, #1
	bx	lr
.L1773:
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
	ldr	r3, .L1787
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
	bne	.L1778
.L1779:
	sxth	r4, r7
	sxth	r3, r5
	cmp	r4, r3
	bgt	.L1778
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
	bne	.L1780
	ldr	r3, [sp, #44]
	cmn	r3, #1
	bne	.L1780
	ldr	r3, [sp, #4]
	cmn	r3, #1
	subne	r4, r4, #1
	uxthne	r5, r4
	bne	.L1779
.L1780:
	add	r3, r4, #1
	uxth	r7, r3
	b	.L1779
.L1778:
	sxth	r0, r5
	add	sp, sp, #104
	@ sp needed
	ldmfd	sp!, {r4, r5, r6, r7, r8, pc}
.L1788:
	.align	2
.L1787:
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
	ldr	r4, .L1822
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
.L1790:
	ldrh	r3, [r8]
	sub	r3, r3, #48
	cmp	r5, r3
	ble	.L1793
	mov	r1, #1
	ldr	r0, .L1822+4
	mov	r2, r1
	mov	r3, r5, asl #10
	str	r3, [r7, #-1272]
	bl	FlashReadPages
	ldr	r3, [r7, #-1276]
	cmn	r3, #1
	bne	.L1791
	ldr	r3, [r7, #-1272]
	mov	r1, #1
	ldr	r0, .L1822+4
	mov	r2, r1
	add	r3, r3, #1
	str	r3, [r7, #-1272]
	bl	FlashReadPages
.L1791:
	ldr	r2, [r4, #-1276]
	ldr	r3, .L1822
	cmn	r2, #1
	beq	.L1792
	ldrh	r1, [r6]
	movw	r2, #61649
	cmp	r1, r2
	bne	.L1792
	sub	r2, r3, #1344
	strh	r5, [r2, #-4]	@ movhi
	ldr	r2, [r6, #4]
	str	r2, [r3, #-1340]
	ldr	r3, .L1822+8
	ldrh	r2, [r6, #8]
	strh	r2, [r3, #4]	@ movhi
	b	.L1793
.L1792:
	sub	r5, r5, #1
	uxth	r5, r5
	b	.L1790
.L1793:
	ldr	r8, .L1822
	movw	r2, #65535
	sub	r7, r8, #1344
	sub	r5, r7, #4
	ldrh	r3, [r7, #-4]
	cmp	r3, r2
	beq	.L1807
	ldrh	r3, [r5, #4]
	cmp	r3, r2
	beq	.L1797
	mov	r1, #1
	add	r0, r5, #72
	mov	r2, r1
	mov	r3, r3, asl #10
	str	r3, [r8, #-1272]
	bl	FlashReadPages
	ldr	r3, [r8, #-1276]
	cmn	r3, #1
	beq	.L1797
	ldrh	r2, [r6]
	movw	r3, #61649
	cmp	r2, r3
	bne	.L1797
	ldr	r3, [r6, #4]
	ldr	r2, [r8, #-1340]
	cmp	r3, r2
	strhi	r3, [r8, #-1340]
	ldrhih	r2, [r5, #4]
	ldrhih	r3, [r6, #8]
	strhih	r2, [r7, #-4]	@ movhi
	strhih	r3, [r5, #4]	@ movhi
.L1797:
	ldrh	r0, [r7, #-4]
	mov	r1, #1
	bl	FtlGetLastWrittenPage
	movw	r8, #61649
	uxth	r7, r0
	add	r0, r0, #1
	strh	r0, [r5, #2]	@ movhi
.L1799:
	sxth	r3, r7
	cmp	r3, #0
	blt	.L1804
	ldrh	r2, [r5]
	mov	r1, #1
	ldr	r0, .L1822+4
	orr	r3, r3, r2, asl #10
	str	r3, [r4, #-1272]
	ldr	r3, [r4, #-1476]
	mov	r2, r1
	str	r3, [r4, #-1268]
	bl	FlashReadPages
	ldr	r3, [r4, #-1276]
	cmn	r3, #1
	beq	.L1800
	ldrh	r3, [r6]
	cmp	r3, r8
	bne	.L1800
.L1804:
	ldrh	r2, [r6, #10]
	ldrh	r0, [r6, #12]
	ldr	r3, .L1822
	strh	r2, [r5, #6]	@ movhi
	movw	r2, #65535
	cmp	r0, r2
	bne	.L1801
	b	.L1802
.L1800:
	sub	r7, r7, #1
	uxth	r7, r7
	b	.L1799
.L1801:
	ldr	r2, [r3, #-1740]
	cmp	r0, r2
	beq	.L1802
	sub	r3, r3, #1712
	ldrh	r3, [r3, #-14]
	mov	r3, r3, lsr #2
	cmp	r0, r3
	cmpcc	r2, r3
	bcs	.L1802
	bl	FtlSysBlkNumInit
.L1802:
	ldr	r5, .L1822+12
	mov	r4, #0
	ldr	r7, .L1822
	ldr	r6, .L1822+16
	sub	r8, r5, #28
.L1805:
	ldrh	r3, [r6]
	cmp	r4, r3
	bcs	.L1821
	ldrh	r2, [r8]
	ldr	r1, [r7, #-1268]
	ldr	r0, [r5, #4]!
	mov	r2, r2, asl #2
	mla	r1, r4, r2, r1
	bl	ftl_memcpy
	add	r4, r4, #1
	b	.L1805
.L1821:
	mov	r0, #0
	ldmfd	sp!, {r4, r5, r6, r7, r8, pc}
.L1807:
	mvn	r0, #0
	ldmfd	sp!, {r4, r5, r6, r7, r8, pc}
.L1823:
	.align	2
.L1822:
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
	ldr	r3, .L1826
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
.L1827:
	.align	2
.L1826:
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
	ldr	r3, .L1829
	ldrh	r0, [r3, #6]
	clz	r0, r0
	mov	r0, r0, lsr #5
	bx	lr
.L1830:
	.align	2
.L1829:
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
	ldr	r3, .L1832
	ldrh	r0, [r3, #6]
	sub	r0, r0, #1024
	clz	r0, r0
	mov	r0, r0, lsr #5
	bx	lr
.L1833:
	.align	2
.L1832:
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
	ldr	r4, .L1844
	ldr	r5, .L1844+4
	ldrh	r3, [r4, #6]
	cmp	r3, #1024
	ldmeqfd	sp!, {r3, r4, r5, r6, r7, pc}
	cmp	r1, #0
	beq	.L1836
	ldr	r3, [r5, #-1280]
	cmp	r3, #0
	bne	.L1836
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
.L1836:
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
.L1845:
	.align	2
.L1844:
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
	ldr	r9, .L1896
	ldr	r5, [r9, #-1280]
	ldrb	r3, [r9, #-1874]	@ zero_extendqisi2
	cmp	r5, #0
	str	r3, [sp, #16]
	movne	r0, #0
	bne	.L1847
	ldrb	r3, [r9, #-2744]	@ zero_extendqisi2
	mov	fp, r1
	mov	r10, r5
	mov	r4, r5
	mov	r7, r9
	mov	r8, #36
	str	r0, [sp, #4]
	str	r3, [sp, #8]
	str	r0, [r9, #-1544]
.L1848:
	ldr	r3, .L1896+4
	uxth	r1, r10
	ldr	r6, .L1896
	ldrh	r0, [r3]
	cmp	r0, r1
	bls	.L1891
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
	beq	.L1849
	bl	IsBlkInVendorPart
	cmp	r0, #0
	bne	.L1850
.L1849:
	mov	r0, r6
	bl	FtlBbmIsBadBlock
	cmp	r0, #0
	addne	r4, r4, #1
	uxthne	r4, r4
	bne	.L1850
	ldr	r3, .L1896+8
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
.L1850:
	add	r10, r10, #1
	b	.L1848
.L1891:
	cmp	r5, #0
	beq	.L1871
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
.L1854:
	uxth	r2, r8
	cmp	r2, r5
	bcs	.L1892
	mul	r2, r6, r8
	ldr	r1, [r9, #-1492]
	add	r0, r1, r2
	ldr	r2, [r1, r2]
	cmn	r2, #1
	bne	.L1855
	ldr	r0, [r0, #4]
	add	r4, r4, #1
	ubfx	r0, r0, #10, #16
	uxth	r4, r4
	bl	FtlBbmMapBadBlock
.L1855:
	add	r8, r8, #1
	b	.L1854
.L1892:
	cmp	fp, #0
	bne	.L1857
	mov	r3, #6
	uxth	r6, r7
	str	r3, [sp, #12]
	mov	r3, #1
	str	r3, [sp, #8]
	b	.L1858
.L1857:
	ldr	r2, .L1896+12
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
.L1858:
	ldr	r10, .L1896
	mov	r7, #0
.L1867:
	mov	r8, #0
	mov	r5, r8
.L1859:
	ldr	r3, .L1896+4
	ldr	r9, .L1896
	ldrh	r2, [r3]
	uxth	r3, r8
	cmp	r2, r3
	bls	.L1893
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
	beq	.L1860
	bl	IsBlkInVendorPart
	cmp	r0, #0
	bne	.L1861
.L1860:
	mov	r0, r9
	bl	FtlBbmIsBadBlock
	cmp	r0, #0
	bne	.L1861
	ldr	r2, [r10, #-1492]
	mov	r3, #36
	add	r9, r7, r9, asl #10
	mla	r2, r3, r5, r2
	ldr	r3, [r10, #-1456]
	str	r3, [r2, #8]
	ldr	r3, .L1896+8
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
.L1861:
	add	r8, r8, #1
	b	.L1859
.L1893:
	cmp	r5, #0
	beq	.L1871
	ldr	r2, .L1896
	mov	r3, #0
	mov	r1, r5
	mov	r8, #0
	strb	r3, [r2, #-1874]
	mov	r3, #1
	ldr	r0, [r2, #-1492]
	mov	r2, r6
	bl	FlashProgPages
	ldr	r3, .L1896
	mov	r1, #36
	ldrb	r2, [sp, #16]	@ zero_extendqisi2
	strb	r2, [r3, #-1874]
.L1864:
	uxth	r3, r8
	cmp	r3, r5
	bcs	.L1894
	mul	r3, r1, r8
	ldr	r2, .L1896
	ldr	r2, [r2, #-1492]
	add	r0, r2, r3
	ldr	r3, [r2, r3]
	cmp	r3, #0
	beq	.L1865
	ldr	r0, [r0, #4]
	add	r4, r4, #1
	str	r1, [sp, #20]
	ubfx	r0, r0, #10, #16
	uxth	r4, r4
	bl	FtlBbmMapBadBlock
	ldr	r1, [sp, #20]
.L1865:
	add	r8, r8, #1
	b	.L1864
.L1894:
	ldr	r3, [sp, #12]
	add	r7, r7, r3
	ldr	r3, [sp, #8]
	uxth	r7, r7
	cmp	r7, r3
	bcc	.L1867
	mov	r7, #0
	mov	r8, #36
.L1868:
	uxth	r3, r7
	cmp	r3, r5
	bcs	.L1895
	cmp	fp, #0
	beq	.L1869
	mul	r3, r8, r7
	ldr	r2, [r9, #-1492]
	add	r1, r2, r3
	ldr	r3, [r2, r3]
	cmp	r3, #0
	bne	.L1869
	ldr	r0, [r1, #4]
	mov	r1, #1
	ubfx	r0, r0, #10, #16
	bl	FtlFreeSysBlkQueueIn
.L1869:
	add	r7, r7, #1
	b	.L1868
.L1895:
	adds	r3, fp, #0
	ldr	r2, [sp, #4]
	movne	r3, #1
	cmp	r2, #63
	orrls	r3, r3, #1
	cmp	r3, #0
	beq	.L1871
	ldr	r0, [r9, #-1492]
	mov	r1, r6
	mov	r2, r5
	bl	FlashEraseBlocks
.L1871:
	mov	r0, r4
.L1847:
	add	sp, sp, #28
	@ sp needed
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L1897:
	.align	2
.L1896:
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
	ldr	r3, .L1911
	ldrh	ip, [r3, #28]
	ldr	r3, .L1911+4
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
.L1900:
	uxth	lr, r0
	add	r0, r0, #1
	cmp	lr, ip
	bge	.L1910
	add	lr, r3, r1, asl #1
	add	r1, r1, #1
	ubfx	r1, r1, #0, #10
	ldrh	r4, [lr, #8]
	add	lr, r3, r2, asl #1
	strh	r4, [lr, #8]	@ movhi
	mov	r4, #1
	add	r2, r2, r4
	ubfx	r2, r2, #0, #10
	b	.L1900
.L1910:
	cmp	r4, #0
	strneh	r1, [r3, #2]	@ movhi
	strneh	r2, [r3, #4]	@ movhi
	ldmfd	sp!, {r4, pc}
.L1912:
	.align	2
.L1911:
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
	ldr	r6, .L1924
	ldr	r5, .L1924+4
	mov	r7, r6
.L1914:
	ldrh	r1, [r5, #6]
	cmp	r1, #0
	beq	.L1915
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
	bne	.L1916
	mov	r0, r4
	bl	P2V_block_in_plane
	mov	r3, r4, asl #10
	mov	r8, r0
	ldr	r0, [r6, #-1492]
	str	r3, [r0, #4]
	ldrb	r3, [r6, #-2744]	@ zero_extendqisi2
	cmp	r3, #0
	beq	.L1917
	mov	r1, r9
	mov	r2, #1
	bl	FlashEraseBlocks
.L1917:
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
	b	.L1916
.L1915:
	ldr	r0, .L1924+8
	bl	printk
.L1918:
	b	.L1918
.L1916:
	sub	r3, r4, #1
	movw	r2, #65533
	uxth	r3, r3
	cmp	r3, r2
	bls	.L1919
	ldr	r3, .L1924+4
	mov	r1, r4
	ldr	r0, .L1924+12
	ldrh	r2, [r3, #6]
	bl	printk
	b	.L1914
.L1919:
	mov	r0, r4
	ldmfd	sp!, {r3, r4, r5, r6, r7, r8, r9, pc}
.L1925:
	.align	2
.L1924:
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
	ldr	r3, .L1932
	str	lr, [sp, #-4]!
	.save {lr}
	movw	lr, #65535
	ldr	ip, [r3, #-1356]
	ldr	r2, [r0]
	ldr	r3, .L1932+4
	rsb	r0, ip, r2
	mov	r0, r0, asr #1
	mul	r3, r3, r0
	mov	r0, #6
	uxth	r3, r3
.L1928:
	cmp	r1, r3
	beq	.L1929
	ldrh	r3, [r2]
	cmp	r3, lr
	beq	.L1930
	mla	r2, r0, r3, ip
	b	.L1928
.L1929:
	mov	r0, #1
	ldr	pc, [sp], #4
.L1930:
	mov	r0, #0
	ldr	pc, [sp], #4
.L1933:
	.align	2
.L1932:
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
	ldr	r2, .L1951
	sub	r5, r2, #1728
	ldrh	r3, [r5]
	cmp	r3, r0
	bls	.L1936
	mov	lr, #6
	ldr	r6, [r2, #-1356]
	mul	lr, lr, r0
	mvn	ip, #0
	add	r1, r6, lr
	strh	ip, [r1, #2]	@ movhi
	strh	ip, [r6, lr]	@ movhi
	ldr	r3, [r2, #864]
	cmp	r3, #0
	beq	.L1950
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
	ldr	r2, .L1951+4
	rsb	r9, r7, r3
	mov	r9, r9, asr #1
	mul	r2, r2, r9
	ldr	r9, [r10, #-1416]
	add	r4, r9, r4
	str	r4, [sp, #4]
	mov	r4, #0
	uxth	r2, r2
.L1945:
	add	r4, r4, #1
	ldr	r5, [sp]
	uxth	r4, r4
	cmp	r4, r5
	movls	r5, #0
	movhi	r5, #1
	cmp	r0, r2
	orreq	r5, r5, #1
	cmp	r5, #0
	bne	.L1936
	mov	r10, r2, asl #1
	ldrh	r5, [r3, #4]
	ldrh	fp, [r8, r10]
	cmp	r5, #0
	mvneq	r5, #0
	mulne	r5, r5, fp
	cmp	r5, ip
	bne	.L1941
	ldr	r5, [sp, #4]
	ldrh	r10, [r9, r10]
	ldrh	r5, [r5]
	cmp	r10, r5
	bcc	.L1943
	b	.L1942
.L1941:
	bhi	.L1942
.L1943:
	ldrh	r5, [r3]
	movw	r10, #65535
	cmp	r5, r10
	streqh	r2, [r1, #2]	@ movhi
	streqh	r0, [r3]	@ movhi
	ldreq	r3, .L1951
	streq	r1, [r3, #868]
	beq	.L1936
.L1944:
	mov	r3, #6
	mov	r2, r5
	mla	r3, r3, r5, r7
	b	.L1945
.L1942:
	strh	r2, [r6, lr]	@ movhi
	ldrh	r2, [r3, #2]
	strh	r2, [r1, #2]	@ movhi
	ldr	r2, .L1951
	ldr	ip, [r2, #864]
	cmp	r3, ip
	bne	.L1946
	strh	r0, [r3, #2]	@ movhi
.L1950:
	str	r1, [r2, #864]
	b	.L1936
.L1946:
	ldrh	ip, [r3, #2]
	ldr	r1, [r2, #-1356]
	mov	r2, #6
	mul	r2, r2, ip
	strh	r0, [r1, r2]	@ movhi
	strh	r0, [r3, #2]	@ movhi
.L1936:
	mov	r0, #0
	add	sp, sp, #12
	@ sp needed
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L1952:
	.align	2
.L1951:
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
	ldr	r2, .L1955
	ldrh	r3, [r2]
	add	r3, r3, #1
	strh	r3, [r2]	@ movhi
	ldmfd	sp!, {r3, pc}
.L1956:
	.align	2
.L1955:
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
	beq	.L1958
	ldr	r2, .L1966
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
	beq	.L1965
	ldr	r9, [r2, #-1416]
	mov	r2, r0, asl #1
	ldr	r8, [r5, #-1356]
	rsb	r1, r8, r3
	ldrh	r10, [r9, r2]
	ldr	r2, .L1966+4
	mov	r1, r1, asr #1
	mul	r1, r2, r1
	uxth	r2, r1
.L1962:
	mov	r1, r2, asl #1
	ldrh	r1, [r9, r1]
	cmp	r1, r10
	bcs	.L1960
	ldrh	r1, [r3]
	cmp	r1, r4
	streqh	r2, [ip, #2]	@ movhi
	streqh	r0, [r3]	@ movhi
	beq	.L1958
.L1961:
	mla	r3, lr, r1, r8
	mov	r2, r1
	b	.L1962
.L1960:
	ldrh	r1, [r3, #2]
	strh	r1, [ip, #2]	@ movhi
	strh	r2, [r7, r6]	@ movhi
	ldr	r1, [r5, #876]
	ldr	r2, .L1966
	cmp	r3, r1
	bne	.L1963
	strh	r0, [r3, #2]	@ movhi
.L1965:
	str	ip, [r2, #876]
	b	.L1958
.L1963:
	ldrh	ip, [r3, #2]
	ldr	r1, [r2, #-1356]
	mov	r2, #6
	mul	r2, r2, ip
	strh	r0, [r1, r2]	@ movhi
	strh	r0, [r3, #2]	@ movhi
.L1958:
	mov	r0, #0
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, pc}
.L1967:
	.align	2
.L1966:
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
	ldr	r2, .L1970
	ldrh	r3, [r2]
	add	r3, r3, #1
	strh	r3, [r2]	@ movhi
	ldmfd	sp!, {r3, pc}
.L1971:
	.align	2
.L1970:
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
	ldr	r4, .L1978
	movw	r5, #65535
	mul	r1, ip, r1
	ldr	r3, [r0]
	ldr	r2, [r4, #-1356]
	add	lr, r2, r1
	cmp	lr, r3
	ldrh	r3, [r2, r1]
	bne	.L1973
	cmp	r3, r5
	mlane	r3, ip, r3, r2
	moveq	r3, #0
	streq	r3, [r0]
	strne	r3, [r0]
	mvnne	r0, #0
	strneh	r0, [r3, #2]	@ movhi
	b	.L1975
.L1973:
	cmp	r3, r5
	ldrh	r0, [lr, #2]
	bne	.L1976
	cmp	r0, r3
	mulne	r0, ip, r0
	mvnne	r3, #0
	strneh	r3, [r2, r0]	@ movhi
	b	.L1975
.L1976:
	mla	r3, ip, r3, r2
	strh	r0, [r3, #2]	@ movhi
	ldrh	r5, [lr, #2]
	ldrh	r0, [r2, r1]
	ldr	r3, [r4, #-1356]
	mul	ip, ip, r5
	strh	r0, [r3, ip]	@ movhi
.L1975:
	mvn	r3, #0
	mov	r0, #0
	strh	r3, [r2, r1]	@ movhi
	strh	r3, [lr, #2]	@ movhi
	ldmfd	sp!, {r4, r5, pc}
.L1979:
	.align	2
.L1978:
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
	beq	.L1986
	ldr	r2, .L1989
	movw	ip, #65535
	stmfd	sp!, {r4, lr}
	.save {r4, lr}
	mov	lr, #6
	ldr	r4, [r2, #-1356]
.L1982:
	cmp	r1, #0
	bne	.L1983
.L1985:
	rsb	r4, r4, r3
	ldr	r3, .L1989+4
	mov	r4, r4, asr #1
	mul	r4, r3, r4
	uxth	r4, r4
	mov	r1, r4
	bl	List_remove_node
	mov	r0, r4
	ldmfd	sp!, {r4, pc}
.L1983:
	ldrh	r2, [r3]
	cmp	r2, ip
	beq	.L1985
	sub	r1, r1, #1
	mla	r3, lr, r2, r4
	uxth	r1, r1
	b	.L1982
.L1986:
	movw	r0, #65535
	bx	lr
.L1990:
	.align	2
.L1989:
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
	ldr	r2, .L1998
	ldr	r3, [r2, #864]
	cmp	r3, #0
	ldrne	r1, [r2, #-1356]
	movne	ip, #6
	movwne	r2, #65535
	beq	.L1996
.L1993:
	cmp	r0, #0
	beq	.L1994
	ldrh	r3, [r3]
	cmp	r3, r2
	subne	r0, r0, #1
	mlane	r3, ip, r3, r1
	uxthne	r0, r0
	bne	.L1993
.L1996:
	movw	r0, #65535
	bx	lr
.L1994:
	rsb	r3, r1, r3
	ldr	r0, .L1998+4
	mov	r3, r3, asr #1
	mul	r0, r0, r3
	uxth	r0, r0
	bx	lr
.L1999:
	.align	2
.L1998:
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
	ldr	r3, .L2009
	add	r2, r3, #884
	ldrh	r2, [r2]
	cmp	r2, r0
	beq	.L2008
	add	r2, r3, #932
	ldrh	r2, [r2]
	cmp	r2, r0
	beq	.L2008
	add	r2, r3, #980
	ldrh	r2, [r2]
	cmp	r2, r0
	beq	.L2008
	stmfd	sp!, {r4, lr}
	.save {r4, lr}
	mov	lr, #6
	mul	lr, lr, r0
	ldr	r1, [r3, #-1356]
	ldr	r2, [r3, #864]
	add	ip, r1, lr
	cmp	ip, r2
	beq	.L2001
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
	bne	.L2003
	ldrh	r3, [r1, lr]
	cmp	r3, ip
	beq	.L2001
.L2003:
	mov	r3, #6
	mul	ip, r3, ip
	ldr	r3, .L2009+4
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
	bcs	.L2001
	mov	r4, r0
	ldr	r0, .L2009+8
	mov	r1, r4
	bl	List_remove_node
	ldr	r2, .L2009+12
	mov	r0, r4
	ldrh	r3, [r2]
	sub	r3, r3, #1
	strh	r3, [r2]	@ movhi
	bl	INSERT_DATA_LIST
.L2001:
	mov	r0, #0
	ldmfd	sp!, {r4, pc}
.L2008:
	mov	r0, #0
	bx	lr
.L2010:
	.align	2
.L2009:
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
.L2012:
	ldrh	r1, [r4, #6]
	uxth	r3, r2
	cmp	r1, r3
	bls	.L2032
	ldr	r0, [r6, r3, asl #2]
	mov	r1, #0
	ubfx	r0, r0, #10, #16
.L2013:
	ldrh	ip, [r4, #10]
	uxth	r3, r1
	cmp	ip, r3
	bls	.L2033
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
	b	.L2013
.L2033:
	add	r2, r2, #1
	b	.L2012
.L2032:
	ldr	r3, .L2035
	mov	r8, #0
	mov	r1, r8
	mov	fp, r8
	ldrh	r2, [r3, #-2]
	ldrh	r3, [r4]
	mov	r3, r3, asl #1
	strh	r2, [r5, r3]	@ movhi
	ldrh	r9, [r5]
.L2017:
	ldrh	r3, [r4, #10]
	uxth	r6, r8
	cmp	r3, r6
	bls	.L2034
	mov	r2, r6, asl #1
	ldrh	r3, [r5, r2]
	cmp	r9, r3
	bls	.L2018
	ldrh	r0, [r7, r2]
	add	r10, r7, r2
	cmp	r0, #0
	bne	.L2019
	b	.L2020
.L2018:
	cmp	r3, #0
	bne	.L2020
	ldrh	r0, [r7, r2]
	add	r10, r7, r2
	cmp	r0, #0
	movne	r6, r1
	beq	.L2020
	b	.L2022
.L2019:
	cmp	r3, #0
	movne	r1, r6
	movne	r9, r3
	bne	.L2020
	mov	r9, r3
.L2022:
	mov	r1, #1
	bl	FtlFreeSysBlkQueueIn
	strh	fp, [r10]	@ movhi
	ldrh	r3, [r4, #8]
	mov	r1, r6
	sub	r3, r3, #1
	strh	r3, [r4, #8]	@ movhi
.L2020:
	add	r8, r8, #1
	b	.L2017
.L2034:
	mov	r0, r1
	ldmfd	sp!, {r3, r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L2036:
	.align	2
.L2035:
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
.L2038:
	uxth	r5, r3
	cmp	r5, r1
	bcs	.L2041
	mov	r7, r2
	add	r3, r3, #1
	ldrh	r6, [r7]
	add	r2, r2, #2
	cmp	r6, #0
	bne	.L2038
	mov	r4, r0
	bl	FtlFreeSysBlkQueueOut
	movw	r2, #65533
	sub	r3, r0, #1
	mov	r1, r0
	strh	r0, [r7]	@ movhi
	uxth	r3, r3
	cmp	r3, r2
	bls	.L2039
	ldr	r3, .L2045
	ldr	r0, .L2045+4
	ldrh	r2, [r3, #6]
	bl	printk
.L2040:
	b	.L2040
.L2039:
	ldr	r3, [r4, #28]
	strh	r6, [r4, #2]	@ movhi
	add	r3, r3, #1
	str	r3, [r4, #28]
	ldrh	r3, [r4, #8]
	strh	r5, [r4]	@ movhi
	add	r3, r3, #1
	strh	r3, [r4, #8]	@ movhi
.L2041:
	mov	r0, #0
	ldmfd	sp!, {r3, r4, r5, r6, r7, pc}
.L2046:
	.align	2
.L2045:
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
	ldr	fp, .L2066
	mov	r8, r1
	ldr	r9, .L2066+4
	mov	r5, #0
	str	r2, [sp]
	mov	r10, fp
.L2048:
	ldr	r3, [fp, #-1592]
	ldr	r6, .L2066
	add	r3, r3, #1
	str	r3, [fp, #-1592]
	ldrh	r3, [r9]
	ldrh	r2, [r4, #2]
	sub	r3, r3, #1
	cmp	r2, r3
	bge	.L2049
	ldrh	r2, [r4]
	movw	r3, #65535
	cmp	r2, r3
	bne	.L2050
.L2049:
	mov	r0, r4
	bl	Ftl_write_map_blk_to_last_page
.L2050:
	ldr	r1, [r10, #-1280]
	cmp	r1, #0
	bne	.L2051
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
	ldr	r2, .L2066+8
	ldrb	r2, [r2, #32]	@ zero_extendqisi2
	cmp	r2, #0
	beq	.L2052
	ldr	r2, .L2066+12
	ldr	r0, [fp, #-1268]
	str	r3, [sp, #4]
	ldrh	r1, [r2]
	bl	js_hash
	ldr	r3, [sp, #4]
	str	r0, [r3, #12]
.L2052:
	mov	r1, #1
	ldr	r0, .L2066+16
	mov	r2, r1
	mov	r3, r1
	bl	FlashProgPages
	ldrh	r3, [r4, #2]
	add	r3, r3, #1
	uxth	r3, r3
	strh	r3, [r4, #2]	@ movhi
	ldr	r2, [r10, #-1276]
	cmn	r2, #1
	bne	.L2053
	ldr	r0, .L2066+20
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
	bls	.L2048
	ldr	r0, .L2066+24
	mov	r2, r5
	ldr	r1, [r6, #-1272]
	bl	printk
	mov	r3, #1
	str	r3, [r6, #-1280]
	b	.L2051
.L2053:
	cmp	r2, #0
	strneh	r7, [r4, #40]	@ movhi
	cmp	r2, #256
	cmpne	r3, #1
	beq	.L2057
	ldr	r3, [r4, #36]
	cmp	r3, #0
	beq	.L2058
.L2057:
	mov	r3, #0
	str	r3, [r4, #36]
	b	.L2048
.L2058:
	ldr	r2, [r6, #-1272]
	ldr	r3, [r4, #24]
	str	r2, [r3, r8, asl #2]
.L2051:
	mov	r0, #0
	add	sp, sp, #12
	@ sp needed
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L2067:
	.align	2
.L2066:
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
	ldr	r8, .L2082
	cmp	r2, r3
	blt	.L2069
	uxth	r0, r0
	mov	r0, r0, asl #1
	ldrh	r10, [r5, r0]
	cmp	r10, #0
	beq	.L2069
	ldr	r3, [r4, #32]
	cmp	r3, #0
	bne	.L2069
	mov	r2, #1
	str	r2, [r4, #32]
	strh	r3, [r5, r0]	@ movhi
	ldrh	r3, [r4, #8]
	ldrh	r2, [r4, #2]
	sub	r3, r3, #1
	strh	r3, [r4, #8]	@ movhi
	ldrh	r3, [r8, #-2]
	cmp	r2, r3
	bcc	.L2070
	mov	r0, r4
	bl	ftl_map_blk_alloc_new_blk
.L2070:
	ldr	r5, .L2082+4
	mov	fp, #0
.L2071:
	ldrh	r3, [r4, #6]
	uxth	r6, fp
	cmp	r3, r6
	bls	.L2081
	ldr	r3, [r7, r6, asl #2]
	add	ip, r7, r6, asl #2
	cmp	r10, r3, lsr #10
	bne	.L2072
	ldr	r3, [r5, #-1472]
	mov	r1, #1
	ldr	r9, [r5, #-1444]
	mov	r2, r1
	ldr	r0, .L2082+8
	str	r3, [r5, #-1268]
	str	r9, [r5, #-1264]
	ldr	r3, [r7, r6, asl #2]
	str	ip, [sp, #4]
	str	r3, [r5, #-1272]
	bl	FlashReadPages
	ldr	r3, [r5, #-1276]
	cmn	r3, #1
	ldr	r3, .L2082+4
	ldr	ip, [sp, #4]
	bne	.L2073
.L2075:
	mov	r2, #0
	ldr	r0, .L2082+12
	str	r2, [ip]
	ldr	r1, [r3, #-1272]
	ldrh	r2, [r9, #8]
	str	r3, [sp, #4]
	bl	printk
	mov	r2, #1
	ldr	r3, [sp, #4]
	str	r2, [r3, #-1280]
	b	.L2074
.L2073:
	ldrh	r1, [r9, #8]
	cmp	r1, r6
	bne	.L2075
	ldrh	r0, [r9]
	ldrh	r2, [r4, #4]
	cmp	r0, r2
	bne	.L2075
	mov	r0, r4
	ldr	r2, [r5, #-1268]
	bl	FtlMapWritePage
.L2072:
	add	fp, fp, #1
	b	.L2071
.L2081:
	mov	r0, r10
	mov	r1, #1
	bl	FtlFreeSysBlkQueueIn
	mov	r3, #0
	str	r3, [r4, #32]
.L2069:
	ldrh	r2, [r4, #2]
	ldrh	r3, [r8, #-2]
	cmp	r2, r3
	bcc	.L2074
	mov	r0, r4
	bl	ftl_map_blk_alloc_new_blk
.L2074:
	mov	r0, #0
	add	sp, sp, #12
	@ sp needed
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L2083:
	.align	2
.L2082:
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
	ldr	r5, .L2096
	ldr	r7, [r0, #12]
	ldr	r8, [r0, #24]
	ldr	r6, [r5, #-1280]
	cmp	r6, #0
	bne	.L2085
	ldrh	r3, [r0]
	movw	r2, #65535
	mov	r4, r0
	cmp	r3, r2
	bne	.L2086
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
	b	.L2085
.L2086:
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
	ldr	r3, .L2096+4
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
.L2087:
	ldrh	r1, [r4, #6]
	uxth	r2, r6
	cmp	r1, r2
	bls	.L2095
	ldr	r1, [r8, r2, asl #2]
	cmp	r9, r1, lsr #10
	bne	.L2088
	add	r3, r3, #1
	ldr	r1, [r5, #-1476]
	uxth	r3, r3
	str	r2, [r1, r3, asl #3]
	ldr	r1, [r8, r2, asl #2]
	ldr	r2, [r5, #-1476]
	add	r2, r2, r3, asl #3
	str	r1, [r2, #4]
.L2088:
	add	r6, r6, #1
	b	.L2087
.L2095:
	ldr	r3, .L2096+8
	ldrb	r3, [r3, #32]	@ zero_extendqisi2
	cmp	r3, #0
	beq	.L2090
	ldr	r3, .L2096+12
	ldr	r0, [r5, #-1268]
	ldrh	r1, [r3, #-10]
	bl	js_hash
	str	r0, [r7, #12]
.L2090:
	mov	r1, #1
	mov	r3, #0
	ldr	r0, .L2096+16
	mov	r2, r1
	bl	FlashProgPages
	ldrh	r3, [r4, #2]
	mov	r0, r4
	add	r3, r3, #1
	strh	r3, [r4, #2]	@ movhi
	bl	ftl_map_blk_gc
.L2085:
	mov	r0, #0
	ldmfd	sp!, {r3, r4, r5, r6, r7, r8, r9, pc}
.L2097:
	.align	2
.L2096:
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
	ldr	r5, .L2100
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
.L2101:
	.align	2
.L2100:
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
	ldr	r3, .L2118
	mov	r1, #0
	stmfd	sp!, {r4, r5, r6, lr}
	.save {r4, r5, r6, lr}
	sub	r2, r3, #1616
	ldr	r3, [r3, #-1364]
	mov	ip, #12
	ldrh	r2, [r2, #-14]
	movw	lr, #65535
.L2103:
	uxth	r0, r1
	cmp	r0, r2
	bcs	.L2115
	add	r1, r1, #1
	mla	r4, ip, r1, r3
	ldrh	r4, [r4, #-12]
	cmp	r4, lr
	bne	.L2103
	ldmfd	sp!, {r4, r5, r6, pc}
.L2115:
	mov	r0, r2
	mov	r1, #0
	mov	ip, #-2147483648
	mov	r5, #12
.L2106:
	uxth	r4, r1
	cmp	r4, r2
	bcs	.L2116
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
	b	.L2106
.L2116:
	cmp	r0, r2
	ldmccfd	sp!, {r4, r5, r6, pc}
	ldr	r1, .L2118+4
	mov	r0, r2
	mvn	ip, #0
	ldrh	r5, [r1]
	mov	r1, #0
.L2109:
	uxth	lr, r1
	cmp	lr, r2
	bcs	.L2117
	ldr	r4, [r3, #4]
	cmp	r4, ip
	bcs	.L2110
	ldrh	r6, [r3]
	cmp	r6, r5
	movne	ip, r4
	movne	r0, lr
.L2110:
	add	r1, r1, #1
	add	r3, r3, #12
	b	.L2109
.L2117:
	ldmfd	sp!, {r4, r5, r6, pc}
.L2119:
	.align	2
.L2118:
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
	ldr	r6, .L2137
	sub	r3, r6, #1648
	str	r3, [sp, #8]
	ldr	ip, [r6, #-1284]
	ldrh	r10, [r3, #-12]
	cmp	r0, ip
	bcs	.L2121
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
	b	.L2122
.L2121:
	cmp	r2, #0
	mvn	r0, #0
	streq	r0, [r1]
	b	.L2123
.L2127:
	add	r3, r3, #1
	mla	r0, r1, r3, r4
	ldrh	r0, [r0, #-12]
	cmp	r0, r6
	beq	.L2124
.L2122:
	uxth	r7, r3
	cmp	r7, r2
	bcc	.L2127
	bl	select_l2p_ram_region
	mov	r5, #12
	movw	r2, #65535
	mul	r5, r5, r0
	mov	r7, r0
	add	r3, r4, r5
	ldrh	r1, [r4, r5]
	cmp	r1, r2
	bne	.L2136
.L2128:
	ldr	r3, [fp, #-1376]
	ldr	r4, .L2137
	ldr	r8, [r3, r6, asl #2]
	cmp	r8, #0
	bne	.L2129
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
.L2124:
	ldr	r3, [sp, #4]
	mvn	r2, #0
	bic	r10, r3, r2, asl r10
	ldr	r3, [sp, #12]
	cmp	r3, #0
	uxth	r10, r10
	mov	r3, #12
	bne	.L2125
	ldr	r2, [fp, #-1364]
	mla	r3, r3, r7, r2
	ldr	r3, [r3, #8]
	ldr	r3, [r3, r10, asl #2]
	str	r3, [r9]
	b	.L2126
.L2125:
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
	ldr	r3, .L2137+4
	strh	r6, [r3]	@ movhi
.L2126:
	ldr	r2, [fp, #-1364]
	mov	r3, #12
	mov	r0, #0
	mla	r7, r3, r7, r2
	ldr	r3, [r7, #4]
	cmn	r3, #1
	addne	r3, r3, #1
	strne	r3, [r7, #4]
	b	.L2123
.L2136:
	ldr	r3, [r3, #4]
	cmp	r3, #0
	bge	.L2128
	bl	flush_l2p_region
	b	.L2128
.L2129:
	ldr	r2, [r4, #-1364]
	mov	r1, #1
	ldr	r0, .L2137+8
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
	beq	.L2130
	mov	r1, r6
	mov	r2, r8
	ldr	r0, .L2137+12
	bl	printk
	mov	r2, #4
	mov	r3, r2
	ldr	r0, .L2137+16
	ldr	r1, [r4, #-1264]
	bl	rknand_print_hex
	sub	r3, r4, #1632
	ldr	r0, .L2137+20
	mov	r2, #4
	ldrh	r3, [r3]
	ldr	r1, [r4, #-1376]
	bl	rknand_print_hex
	mov	r3, #1
	str	r3, [r4, #-1280]
	b	.L2131
.L2130:
	ldr	r2, [r4, #-1276]
	cmp	r2, #256
	bne	.L2131
	mov	r1, r6
	mov	r2, r8
	ldr	r0, .L2137+24
	bl	printk
	ldr	r3, [r4, #-1364]
	ldr	r0, .L2137+28
	mov	r1, r6
	add	r3, r3, r5
	ldr	r2, [r3, #8]
	bl	FtlMapWritePage
.L2131:
	ldr	r3, [fp, #-1364]
	mov	r1, #0
	add	r2, r3, r5
	str	r1, [r2, #4]
	strh	r6, [r3, r5]	@ movhi
	b	.L2124
.L2123:
	add	sp, sp, #20
	@ sp needed
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L2138:
	.align	2
.L2137:
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
	ldr	r6, .L2150
	add	r2, r0, r1
	mov	r7, r0
	.pad #60
	sub	sp, sp, #60
	mov	r4, r1
	ldrh	r3, [r6, #-24]
	ldrh	r5, [r6, #-12]
	cmp	r2, r3
	mvnhi	r0, #0
	bhi	.L2140
	mov	r5, r7, lsr r5
	mov	r3, #0
	str	r3, [sp]
	add	r3, r6, #1648
	mov	r10, r5, asl #2
	str	r3, [sp, #4]
	mov	r8, r3
.L2141:
	cmp	r4, #0
	beq	.L2149
	ldr	r3, [sp, #4]
	mov	r0, r7
	ldr	r3, [r3, #-1380]
	ldr	ip, [r3, r10]
	ldr	r3, .L2150+4
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
	beq	.L2143
	ldr	r2, [r8, #-1468]
	mov	r1, #1
	add	r0, sp, #20
	str	ip, [sp, #24]
	str	r2, [sp, #28]
	mov	r2, #0
	str	r2, [sp, #32]
	mov	r2, r1
	bl	FlashReadPages
	b	.L2144
.L2143:
	ldr	r2, .L2150+8
	ldr	r0, [r8, #-1468]
	ldrh	r2, [r2]
	bl	ftl_memset
.L2144:
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
	ldr	r0, .L2150+12
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
	b	.L2141
.L2149:
	ldr	r0, [sp]
.L2140:
	add	sp, sp, #60
	@ sp needed
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L2151:
	.align	2
.L2150:
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
	ldr	r6, .L2164
	add	r2, r0, r1
	mov	r8, r0
	.pad #60
	sub	sp, sp, #60
	mov	r7, r1
	ldrh	r3, [r6, #-24]
	ldrh	r5, [r6, #-12]
	cmp	r2, r3
	mvnhi	r0, #0
	bhi	.L2153
	add	r6, r6, #1648
	mov	r5, r8, lsr r5
	mov	r10, r6
	mov	r3, r5, asl #2
	str	r3, [sp, #4]
	mov	r3, #0
	str	r3, [sp]
.L2154:
	cmp	r7, #0
	beq	.L2163
	ldr	r2, [sp, #4]
	mov	r0, r8
	ldr	r3, [r6, #-1380]
	ldr	r3, [r3, r2]
	str	r3, [sp, #12]
	ldr	r3, .L2164+4
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
	beq	.L2156
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
	bne	.L2158
	mov	r1, r5
	mov	r2, r3
	ldr	r0, .L2164+8
	bl	printk
	ldr	r0, .L2164+12
	mov	r1, r5
	ldr	r2, [r6, #-1468]
	bl	FtlMapWritePage
.L2158:
	ldrh	r3, [sp, #8]
	mov	r0, r9
	ldr	r1, [r10, #-1468]
	mov	r2, fp
	add	r1, r1, r3, asl #9
	bl	ftl_memcpy
	b	.L2159
.L2156:
	mov	r0, r9
	mov	r1, r3
	mov	r2, fp
	bl	ftl_memset
.L2159:
	ldr	r3, [sp, #4]
	add	r5, r5, #1
	rsb	r7, r4, r7
	add	r8, r8, r4
	add	r3, r3, #4
	add	r9, r9, fp
	str	r3, [sp, #4]
	b	.L2154
.L2163:
	ldr	r0, [sp]
.L2153:
	add	sp, sp, #60
	@ sp needed
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L2165:
	.align	2
.L2164:
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
	ldr	r3, .L2174
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
.L2168:
	cmp	r2, r0
	beq	.L2173
	ldrh	r1, [r2], #2
	cmp	r1, ip
	ldrne	lr, [r3, #1124]
	addne	r1, r1, lr
	strne	r1, [r3, #1124]
	b	.L2168
.L2173:
	ldr	pc, [sp], #4
.L2175:
	.align	2
.L2174:
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
	ldr	r4, .L2183
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
	beq	.L2180
	mov	r1, #1
	ldr	r0, .L2183+4
	mov	r2, r1
	bl	FlashReadPages
	b	.L2181
.L2180:
	sub	r3, r4, #1648
	mov	r1, #255
	ldrh	r2, [r3, #-10]
	bl	ftl_memset
.L2181:
	mov	r0, r6
	mov	r1, r5
	ldr	r2, [r4, #-1268]
	ldmfd	sp!, {r4, r5, r6, lr}
	b	FtlMapWritePage
.L2184:
	.align	2
.L2183:
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
	ldr	r4, .L2203
	ldr	r3, [r4, #-1280]
	cmp	r3, #0
	bne	.L2187
	ldr	r2, [r4, #-1476]
	add	r5, r4, #816
	ldr	r6, [r4, #-1444]
	mov	r1, #255
	ldr	r7, .L2203+4
	str	r2, [r4, #-1268]
	movw	r2, #1128
	ldrh	r2, [r4, r2]
	str	r6, [r4, #-1264]
	str	r3, [r6, #12]
	strh	r2, [r6, #2]	@ movhi
	ldr	r2, .L2203+8
	strh	r2, [r6]	@ movhi
	ldr	r2, [r4, #1136]
	stmib	r6, {r2, r3}
	ldr	r3, .L2203+12
	str	r3, [r4, #816]
	ldr	r3, .L2203+16
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
	beq	.L2188
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
.L2188:
	mov	r0, #0
	ldr	r9, .L2203
	bl	FtlUpdateVaildLpn
	ldr	fp, .L2203+4
	mov	r8, #0
	movw	r10, #65535
.L2189:
	ldr	r3, [r4, #-1476]
	ldrh	r2, [r7]
	ldrh	r1, [r7, #2]
	str	r3, [r4, #-1268]
	ldr	r3, [r4, #-1444]
	str	r3, [r4, #-1264]
	orr	r3, r1, r2, asl #10
	str	r3, [r4, #-1272]
	ldr	r3, .L2203+20
	ldrh	r3, [r3]
	sub	r3, r3, #1
	cmp	r1, r3
	blt	.L2190
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
.L2190:
	ldr	r3, .L2203+24
	ldrb	r3, [r3, #32]	@ zero_extendqisi2
	cmp	r3, #0
	beq	.L2191
	ldr	r3, .L2203+28
	ldr	r0, [r4, #-1476]
	ldrh	r1, [r3]
	bl	js_hash
	str	r0, [r6, #12]
.L2191:
	mov	r1, #1
	ldr	r0, .L2203+32
	mov	r3, r1
	mov	r2, r1
	bl	FlashProgPages
	ldrh	r5, [r7, #2]
	ldr	r3, [r4, #-1276]
	add	r5, r5, #1
	cmn	r3, #1
	uxth	r5, r5
	strh	r5, [r7, #2]	@ movhi
	bne	.L2192
	cmp	r5, #1
	add	r8, r8, #1
	ldreq	r3, .L2203+20
	uxth	r8, r8
	ldreqh	r3, [r3]
	subeq	r3, r3, #1
	streqh	r3, [fp, #2]	@ movhi
	cmp	r8, #3
	bls	.L2189
	ldr	r0, .L2203+36
	mov	r2, r8
	ldr	r1, [r4, #-1272]
	bl	printk
	mov	r3, #1
	str	r3, [r4, #-1280]
	b	.L2187
.L2192:
	cmp	r3, #256
	cmpne	r5, #1
	beq	.L2189
	movw	r3, #65535
	cmp	r10, r3
	beq	.L2187
	mov	r0, r10
	mov	r1, #1
	bl	FtlFreeSysBlkQueueIn
.L2187:
	mov	r0, #0
	ldmfd	sp!, {r3, r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L2204:
	.align	2
.L2203:
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
	ldr	r9, .L2291
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
	ldr	r0, .L2291+4
	mov	r1, #255
	mov	r2, #16
	bl	ftl_memset
	sub	r3, r9, #1728
	ldrh	r3, [r3]
	str	r5, [sp, #16]
	mov	r5, r9
	str	r3, [sp, #8]
.L2206:
	ldr	r3, .L2291+8
	ldr	r2, [sp, #8]
	ldr	r1, .L2291
	ldrh	r3, [r3]
	cmp	r3, r2
	bls	.L2247
	ldr	r3, .L2291+12
	mov	r8, #36
	ldr	r1, .L2291+16
	mov	r7, #0
	ldr	r2, [r5, #-1504]
	ldrh	ip, [r3]
	sub	r9, r1, #52
	ldr	r3, [r5, #-1464]
	ldr	fp, [r5, #-1436]
	ldrh	r10, [r1]
	str	r7, [sp, #4]
.L2248:
	uxth	r1, r7
	cmp	r1, ip
	bcs	.L2286
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
	bne	.L2207
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
.L2207:
	add	r7, r7, #1
	b	.L2248
.L2286:
	ldr	r3, [sp, #4]
	cmp	r3, #0
	beq	.L2210
	mov	r1, r3
	ldr	r0, [r4, #-1504]
	mov	r2, #1
	bl	FlashReadPages
	mov	r3, #0
.L2284:
	str	r3, [sp, #12]
	ldr	r2, [sp, #4]
	ldrh	r3, [sp, #12]
	cmp	r3, r2
	bcs	.L2210
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
	bne	.L2213
	mov	r10, #16
	movw	fp, #65535
.L2212:
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
	beq	.L2213
.L2214:
	ldr	r3, [r5, #-1504]
	ldr	r3, [r3, r9]
	cmn	r3, #1
	bne	.L2213
	sub	r10, r10, #1
	uxth	r10, r10
	cmp	r10, #0
	bne	.L2212
.L2213:
	ldr	r3, [r5, #-1504]
	ldr	r3, [r3, r9]
	cmn	r3, #1
	beq	.L2216
	ldr	r2, [r5, #-1616]
	ldr	r3, [r8, #4]
	cmn	r2, #1
	beq	.L2217
	cmp	r2, r3
	bhi	.L2218
.L2217:
	cmn	r3, #1
	addne	r2, r3, #1
	strne	r2, [r6, #-1616]
.L2218:
	ldrh	r2, [r8]
	movw	r1, #61604
	cmp	r2, r1
	beq	.L2220
	bhi	.L2221
	movw	r3, #61574
	cmp	r2, r3
	bne	.L2219
	ldr	r3, [sp, #16]
	ldr	r2, .L2291+20
	ldr	lr, [r4, #-1384]
	ldrh	ip, [r3]
	ldrh	r1, [r2]
	sub	r0, ip, #1
	uxth	r3, r0
	rsb	r0, r1, r0
	b	.L2234
.L2221:
	movw	r3, #61634
	cmp	r2, r3
	beq	.L2223
	movw	r3, #65535
	cmp	r2, r3
	moveq	r0, r7
	beq	.L2285
	b	.L2219
.L2223:
	ldr	ip, [r4, #-1640]
	ldr	fp, .L2291+24
	ldr	lr, [r4, #-1368]
	uxth	r1, ip
	ldrh	r2, [fp]
	sub	r3, r1, #1
	rsb	r1, r2, r1
	uxth	r3, r3
	sub	r1, r1, #1
	sxth	r1, r1
	str	r1, [sp, #20]
.L2225:
	ldr	r1, [sp, #20]
	sxth	r0, r3
	cmp	r0, r1
	ble	.L2287
	ldr	r9, [lr, r0, asl #2]
	mov	r10, r0, asl #2
	ldr	r1, [r8, #4]
	cmp	r1, r9
	bls	.L2226
	ldr	r1, [lr]
	cmp	r1, #0
	bne	.L2227
	cmp	r2, ip
	addne	r2, r2, #1
	ldrne	r1, .L2291+24
	strneh	r2, [r1]	@ movhi
.L2227:
	uxth	lr, r3
	mov	r1, #0
.L2228:
	uxth	r2, r1
	cmp	r2, lr
	bcs	.L2288
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
	b	.L2228
.L2288:
	ldr	r2, [r6, #-1368]
	mov	r0, r0, asl #1
	ldr	r1, [r8, #4]
	str	r1, [r2, r10]
	ldr	r2, [r6, #-1404]
	strh	r7, [r2, r0]	@ movhi
	sxth	r0, r3
	cmp	r0, #0
	bge	.L2230
	b	.L2219
.L2226:
	sub	r3, r3, #1
	uxth	r3, r3
	b	.L2225
.L2287:
	cmp	r0, #0
	bge	.L2261
	b	.L2219
.L2230:
	ldr	r2, .L2291+24
	ldr	r1, [r5, #-1640]
	ldrh	r2, [r2]
	rsb	r1, r2, r1
	sub	r1, r1, #1
	sxth	r1, r1
	cmp	r0, r1
	bgt	.L2219
.L2261:
	add	r2, r2, #1
	ldr	r1, [r8, #4]
	strh	r2, [fp]	@ movhi
	sxth	r3, r3
	ldr	r2, [r4, #-1368]
	str	r1, [r2, r3, asl #2]
	mov	r3, r3, asl #1
	ldr	r2, [r4, #-1404]
	b	.L2282
.L2240:
	ldr	r10, [r8, #4]
	mov	fp, r2, asl #2
	ldr	r9, [lr, r2, asl #2]
	cmp	r10, r9
	bhi	.L2289
	sub	r3, r3, #1
	uxth	r3, r3
.L2234:
	sxth	r2, r3
	cmp	r2, r0
	bgt	.L2240
	b	.L2239
.L2289:
	ldr	r0, [lr]
	cmp	r0, #0
	bne	.L2236
	cmp	r1, ip
	addne	r1, r1, #1
	ldrne	r0, .L2291+20
	strneh	r1, [r0]	@ movhi
.L2236:
	uxth	lr, r3
	mov	r0, #0
.L2237:
	uxth	r1, r0
	cmp	r1, lr
	bcs	.L2290
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
	b	.L2237
.L2290:
	ldr	r1, [r6, #-1384]
	mov	r2, r2, asl #1
	ldr	r0, [r8, #4]
	str	r0, [r1, fp]
	ldr	r1, [r6, #-1392]
	strh	r7, [r1, r2]	@ movhi
.L2239:
	sxth	r3, r3
	cmp	r3, #0
	blt	.L2219
	ldr	r0, .L2291+20
	sub	r2, r0, #360
	ldrh	r1, [r0]
	ldrh	r2, [r2]
	sub	r2, r2, #1
	rsb	r2, r1, r2
	sxth	r2, r2
	cmp	r3, r2
	bgt	.L2219
	add	r1, r1, #1
	ldr	r2, [r5, #-1384]
	strh	r1, [r0]	@ movhi
	ldr	r1, [r8, #4]
	str	r1, [r2, r3, asl #2]
	mov	r3, r3, asl #1
	ldr	r2, [r5, #-1392]
.L2282:
	strh	r7, [r2, r3]	@ movhi
	b	.L2219
.L2220:
	ldr	r2, .L2291+4
	ldr	r9, .L2291+4
	ldrh	r1, [r2]
	movw	r2, #65535
	cmp	r1, r2
	moveq	r2, r9
	streqh	r7, [r2]	@ movhi
	beq	.L2283
	ldr	r3, .L2291+4
	ldrh	r0, [r3, #4]
	cmp	r0, r2
	beq	.L2242
	mov	r1, #1
	bl	FtlFreeSysBlkQueueIn
.L2242:
	ldr	r3, [r8, #4]
	ldr	r2, [r6, #1136]
	cmp	r2, r3
	strcsh	r7, [r9, #4]	@ movhi
	bcs	.L2219
	ldrh	r3, [r9]
	strh	r7, [r9]	@ movhi
	strh	r3, [r9, #4]	@ movhi
	ldr	r3, [r8, #4]
.L2283:
	str	r3, [r5, #1136]
	b	.L2219
.L2216:
	ldrb	r1, [r5, #-2744]	@ zero_extendqisi2
	mov	r0, r7
	cmp	r1, #0
	beq	.L2244
.L2285:
	mov	r1, #0
.L2244:
	bl	FtlFreeSysBlkQueueIn
.L2219:
	ldr	r3, [sp, #12]
	add	r3, r3, #1
	b	.L2284
.L2210:
	ldr	r3, [sp, #8]
	add	r7, r3, #1
	uxth	r3, r7
	str	r3, [sp, #8]
	b	.L2206
.L2247:
	ldr	ip, [r1, #-1404]
	ldrh	r2, [ip]
	cmp	r2, #0
	beq	.L2249
.L2252:
	ldr	ip, [r1, #-1392]
	ldrh	r2, [ip]
	cmp	r2, #0
	beq	.L2250
	b	.L2273
.L2249:
	movw	r3, #1144
	ldrh	r3, [r1, r3]
	cmp	r3, #0
	ldrne	lr, [r1, #-1640]
	beq	.L2252
.L2253:
	uxth	r3, r2
	sxth	r0, r3
	cmp	r0, lr
	bcs	.L2252
	mov	r4, r0, asl #1
	add	r2, r2, #1
	ldrh	r4, [ip, r4]
	cmp	r4, #0
	beq	.L2253
	ldr	ip, .L2291
	mov	r6, #0
.L2254:
	ldr	lr, [r1, #-1640]
	sxth	r2, r3
	cmp	r2, lr
	bcs	.L2252
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
	b	.L2254
.L2250:
	ldr	r1, .L2291+28
	ldrh	r3, [r1, #-8]
	cmp	r3, #0
	subne	r1, r1, #368
	ldrneh	lr, [r1]
	beq	.L2273
.L2257:
	uxth	r3, r2
	sxth	r0, r3
	cmp	r0, lr
	bge	.L2273
	mov	r4, r0, asl #1
	add	r2, r2, #1
	ldrh	r4, [ip, r4]
	cmp	r4, #0
	beq	.L2257
	ldr	ip, .L2291
	mov	r6, #0
.L2258:
	ldrh	lr, [r1]
	sxth	r2, r3
	cmp	r2, lr
	bge	.L2273
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
	b	.L2258
.L2273:
	mov	r0, #0
	add	sp, sp, #36
	@ sp needed
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L2292:
	.align	2
.L2291:
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
	ldr	r4, .L2296
	sub	r5, r4, #1424
	ldr	r2, [r4, #-1420]
	ldrh	r1, [r5, #-4]
	bl	FtlVendorPartRead
	ldr	r3, [r4, #-1420]
	ldr	r2, [r3]
	ldr	r3, .L2296+4
	cmp	r2, r3
	beq	.L2294
	ldr	r1, .L2296+8
	ldr	r0, .L2296+12
	bl	printk
	ldrh	r2, [r5, #-4]
	ldr	r0, [r4, #-1420]
	mov	r1, #0
	mov	r2, r2, asl #9
	bl	ftl_memset
.L2294:
	mov	r0, #0
	ldmfd	sp!, {r3, r4, r5, pc}
.L2297:
	.align	2
.L2296:
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
	ldr	r2, .L2302
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
.L2303:
	.align	2
.L2302:
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
	ldr	r3, .L2305
	mov	r2, r0, lsr #5
	and	r0, r0, #31
	ldr	r3, [r3, #-1396]
	ldr	r3, [r3, r2, asl #2]
	mov	r0, r3, lsr r0
	and	r0, r0, #1
	bx	lr
.L2306:
	.align	2
.L2305:
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
	ldr	r1, .L2330
	ldr	r0, .L2330+4
	bl	printk
	ldr	r0, .L2330+8
	mov	r1, #0
	mov	r2, #8192
	bl	memset
	ldr	r7, .L2330+12
	ldr	r5, .L2330+8
.L2308:
	ldr	r3, [r7, #-1284]
	ldr	r6, .L2330+12
	cmp	r4, r3
	bcs	.L2328
	mov	r0, r4
	add	r1, sp, #4
	mov	r2, #0
	bl	log2phys
	ldr	r0, [sp, #4]
	cmn	r0, #1
	beq	.L2309
	ubfx	r0, r0, #10, #16
	bl	P2V_block_in_plane
	mov	r0, r0, asl #1
	ldrh	r3, [r5, r0]
	add	r3, r3, #1
	strh	r3, [r5, r0]	@ movhi
.L2309:
	add	r4, r4, #1
	b	.L2308
.L2328:
	ldr	r9, .L2330+8
	mov	r4, #0
	mov	r5, r4
	mov	r8, r6
.L2311:
	ldr	r2, .L2330+12
	uxth	r1, r4
	sub	r3, r2, #1728
	ldrh	r3, [r3]
	cmp	r3, r1
	bls	.L2329
	ldr	r3, [r8, #-1408]
	mov	r7, r1, asl #1
	ldrh	r2, [r3, r7]
	ldrh	r3, [r9, r7]
	cmp	r2, r3
	beq	.L2312
	ldr	r0, .L2330+16
	bl	printk
	ldr	r3, [r8, #-1408]
	movw	r2, #65535
	ldrh	r3, [r3, r7]
	cmp	r3, r2
	beq	.L2312
	ldrh	r2, [r9, r7]
	cmp	r2, r3
	movhi	r5, #1
.L2312:
	add	r4, r4, #1
	b	.L2311
.L2329:
	ldr	r3, [r2, #876]
	cmp	r3, #0
	beq	.L2314
	add	r1, r2, #880
	ldr	r2, [r2, #-1356]
	add	r9, r1, #268
	mov	r8, #0
	rsb	r3, r2, r3
	ldr	r2, .L2330+20
	ldrh	r7, [r1]
	mov	r3, r3, asr #1
	mul	r3, r2, r3
	uxth	r4, r3
.L2315:
	uxth	r3, r8
	cmp	r3, r7
	bcs	.L2314
	ldr	r2, [r6, #-1408]
	mov	r3, r4, asl #1
	ldrh	r2, [r2, r3]
	cmp	r2, #0
	beq	.L2316
	ldr	r0, .L2330+24
	mov	r1, r4
	ldrh	r3, [r9, r3]
	mov	r5, #1
	bl	printk
.L2316:
	mov	r3, #6
	ldr	r2, [r6, #-1356]
	mul	r4, r3, r4
	movw	r3, #65535
	add	r8, r8, #1
	ldrh	r4, [r2, r4]
	cmp	r4, r3
	bne	.L2315
.L2314:
	mov	r1, r5
	ldr	r0, .L2330+28
	bl	printk
	add	sp, sp, #12
	@ sp needed
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, pc}
.L2331:
	.align	2
.L2330:
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
	ldr	r4, .L2341
	.pad #24
	sub	sp, sp, #24
	ldr	r8, .L2341+4
	mov	r6, r0
	mov	r5, #0
	ldr	r3, [r4, #-1476]
	str	r3, [r4, #-1268]
	ldr	r3, [r4, #-1444]
	str	r3, [r4, #-1264]
.L2333:
	ldrh	r2, [r8]
	sxth	r3, r5
	cmp	r3, r2
	bge	.L2340
	mov	r1, #1
	ldr	r0, .L2341+8
	mov	r2, r1
	orr	r3, r3, r7
	str	r3, [r4, #-1272]
	bl	FlashReadPages
	ldr	r3, [r4, #-1264]
	mov	r1, r6
	ldr	r0, .L2341+12
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
	beq	.L2334
	ldr	r0, .L2341+16
	mov	r2, #4
	ldr	r1, [r4, #-1476]
	mov	r3, #768
	bl	rknand_print_hex
.L2334:
	add	r5, r5, #1
	b	.L2333
.L2340:
	add	sp, sp, #24
	@ sp needed
	ldmfd	sp!, {r4, r5, r6, r7, r8, pc}
.L2342:
	.align	2
.L2341:
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
	ldr	r0, .L2353
	bl	printk
	ldr	r6, .L2353+4
	mov	r5, #0
	mov	r4, r6
.L2344:
	ldr	r3, [r6, #-1284]
	cmp	r5, r3
	bcs	.L2352
	mov	r0, r5
	add	r1, sp, #28
	mov	r2, #0
	bl	log2phys
	ubfx	r3, r5, #0, #11
	cmp	r3, #0
	bne	.L2345
	ldr	r0, .L2353+8
	mov	r1, r5
	ldr	r2, [sp, #28]
	bl	printk
.L2345:
	ldr	r3, [sp, #28]
	cmn	r3, #1
	beq	.L2347
	str	r3, [r4, #-1272]
	mov	r2, #0
	ldr	r3, [r4, #-1476]
	mov	r1, #1
	ldr	r7, [r4, #-1444]
	ldr	r0, .L2353+12
	str	r3, [r4, #-1268]
	str	r5, [r4, #-1260]
	str	r7, [r4, #-1264]
	str	r2, [r4, #-1276]
	bl	FlashReadPages
	ldr	r3, [r4, #-1276]
	cmn	r3, #1
	cmpne	r3, #256
	beq	.L2348
	ldr	r3, [r7, #8]
	cmp	r3, r5
	beq	.L2347
.L2348:
	ldr	r3, [r4, #-1264]
	ldr	r2, [r4, #-1268]
	ldr	r0, .L2353+16
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
.L2347:
	add	r5, r5, #1
	b	.L2344
.L2352:
	add	sp, sp, #36
	@ sp needed
	ldmfd	sp!, {r4, r5, r6, r7, pc}
.L2354:
	.align	2
.L2353:
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
	ldr	r5, .L2373
	ldr	fp, .L2373+4
	ldrh	r6, [r5]
	add	r5, r5, #1728
	mov	r8, r5
.L2356:
	ldrh	r3, [fp]
	cmp	r3, r6
	bls	.L2369
	ldr	r1, .L2373+8
	mov	r7, #0
	ldr	r2, [r5, #-1504]
	mov	r4, r7
	ldr	r3, [r5, #-1464]
	add	r1, r1, #28
	ldr	r10, [r5, #-1436]
	ldrh	ip, [r1, #-28]
	ldrh	r9, [r1, #52]
	str	r1, [sp, #28]
.L2365:
	uxth	r1, r7
	cmp	r1, ip
	bcs	.L2371
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
	bne	.L2357
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
.L2357:
	add	r7, r7, #1
	b	.L2365
.L2371:
	cmp	r4, #0
	beq	.L2360
	ldr	r0, [r8, #-1504]
	mov	r1, r4
	mov	r2, #1
	mov	r7, #0
	bl	FlashReadPages
	mov	r9, #36
.L2361:
	uxth	r3, r7
	cmp	r3, r4
	bcs	.L2360
	ldr	r3, [r8, #-1504]
	ldr	r0, .L2373+12
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
	b	.L2361
.L2360:
	add	r6, r6, #1
	uxth	r6, r6
	b	.L2356
.L2369:
	ldr	r4, .L2373+16
	mov	r7, #0
	ldr	r8, .L2373+20
.L2364:
	ldrh	r3, [r8]
	sxth	r6, r7
	ldr	r5, .L2373+16
	cmp	r6, r3
	bge	.L2367
	ldr	r9, .L2373+24
	mov	r6, r6, asl #1
	mov	r5, #0
.L2368:
	ldrh	r2, [r9]
	sxth	r3, r5
	add	r5, r5, #1
	cmp	r3, r2
	bge	.L2372
	ldr	r2, [r4, #-1404]
	mov	r1, #1
	ldr	r0, .L2373+28
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
	ldr	r0, .L2373+32
	str	r3, [sp, #12]
	ldr	r3, [r2]
	str	r3, [sp, #16]
	ldr	r3, [r2, #4]
	str	r3, [sp, #20]
	ldr	r2, [r4, #-1276]
	ldr	r3, [r4, #-1272]
	bl	printk
	b	.L2368
.L2372:
	add	r7, r7, #1
	b	.L2364
.L2367:
	sub	r4, r5, #1632
	ldr	r1, [r5, #-1404]
	ldr	r3, [r5, #-1640]
	mov	r2, #2
	ldr	r0, .L2373+36
	bl	rknand_print_hex
	ldr	r1, [r5, #-1376]
	ldrh	r3, [r4]
	mov	r2, #4
	ldr	r0, .L2373+40
	bl	rknand_print_hex
	ldr	r0, .L2373+44
	ldr	r1, [r5, #-1372]
	mov	r2, #4
	ldrh	r3, [r4]
	add	sp, sp, #52
	@ sp needed
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, fp, lr}
	b	rknand_print_hex
.L2374:
	.align	2
.L2373:
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
	ldr	r5, .L2418
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
.L2376:
	ldrh	r2, [sp, #4]
	ldr	r3, [sp, #8]
	sxth	fp, r2
	cmp	fp, r3
	bge	.L2394
	ldr	r3, [sp, #8]
	sub	r1, r3, #1
	cmp	fp, r1
	mov	r1, fp, asl #1
	bne	.L2377
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
	ldr	fp, .L2418
	str	r3, [r4, #28]
	add	r3, r0, #1
	str	r3, [sp, #4]
.L2378:
	ldr	r2, [sp, #4]
	sxth	r3, r8
	cmp	r3, r2
	bge	.L2394
	ldrh	r2, [r10]
	mov	r1, #1
	ldr	r0, .L2418+4
	orr	r3, r3, r2, asl #10
	mov	r2, r1
	str	r3, [r5, #-1272]
	bl	FlashReadPages
	ldr	r3, .L2418+8
	ldrb	r3, [r3, #32]	@ zero_extendqisi2
	cmp	r3, #0
	beq	.L2379
	ldr	r3, [fp, #-1264]
	ldr	r3, [r3, #12]
	cmp	r3, #0
	beq	.L2379
	ldr	r2, .L2418+12
	ldr	r0, [fp, #-1268]
	str	r3, [sp, #8]
	ldrh	r1, [r2]
	bl	js_hash
	ldr	r3, [sp, #8]
	cmp	r3, r0
	mvnne	r3, #0
	strne	r3, [fp, #-1276]
.L2379:
	ldr	r3, [fp, #-1276]
	cmn	r3, #1
	beq	.L2380
	ldrh	r3, [r6, #8]
	cmp	r3, r9
	bcs	.L2380
	ldrh	r2, [r4, #4]
	ldrh	r1, [r6]
	cmp	r1, r2
	ldreq	r2, [fp, #-1272]
	streq	r2, [r7, r3, asl #2]
.L2380:
	add	r8, r8, #1
	b	.L2378
.L2394:
	mov	r0, r4
	bl	ftl_free_no_use_map_blk
	ldr	r3, .L2418+16
	ldrh	r2, [r4, #2]
	ldrh	r3, [r3, #-2]
	cmp	r2, r3
	bne	.L2383
	mov	r0, r4
	bl	ftl_map_blk_alloc_new_blk
	b	.L2383
.L2377:
	ldr	r2, [r5, #-1476]
	add	r3, r8, r1
	str	r3, [sp, #16]
	ldr	r3, .L2418+20
	str	r2, [r5, #-1268]
	ldrh	r1, [r8, r1]
	ldrh	r2, [r3]
	ldr	r0, .L2418+4
	sub	r2, r2, #1
	orr	r2, r2, r1, asl #10
	mov	r1, #1
	str	r2, [r5, #-1272]
	mov	r2, r1
	bl	FlashReadPages
	ldr	r2, [r5, #-1276]
	cmn	r2, #1
	beq	.L2396
	ldrh	r1, [r6]
	ldrh	r2, [r4, #4]
	cmp	r1, r2
	bne	.L2396
	ldrh	r1, [r6, #8]
	movw	r2, #64245
	cmp	r1, r2
	bne	.L2396
	mov	r0, #0
	mov	fp, #8
	mov	lr, #4
.L2385:
	ldr	r3, .L2418+20
	uxth	r2, r0
	sxth	r1, r2
	ldrh	ip, [r3]
	sub	ip, ip, #1
	cmp	r1, ip
	bge	.L2388
	ldr	ip, [r10, #-1476]
	add	r0, r0, #1
	ldr	r1, [ip, r1, asl #3]
	uxth	r1, r1
	cmp	r1, r9
	smlabbcc	r2, r2, fp, lr
	ldrcc	r2, [ip, r2]
	strcc	r2, [r7, r1, asl #2]
	b	.L2385
.L2396:
	mov	fp, #0
.L2416:
	ldr	r3, .L2418+20
	sxth	r2, fp
	ldrh	r1, [r3]
	cmp	r2, r1
	bge	.L2388
	ldr	r3, [sp, #16]
	ldr	r0, .L2418+4
	ldrh	r1, [r3]
	orr	r2, r2, r1, asl #10
	mov	r1, #1
	str	r2, [r10, #-1272]
	mov	r2, r1
	bl	FlashReadPages
	ldr	r2, .L2418+8
	ldrb	r2, [r2, #32]	@ zero_extendqisi2
	cmp	r2, #0
	beq	.L2389
	ldr	r2, [r10, #-1264]
	ldr	r2, [r2, #12]
	cmp	r2, #0
	beq	.L2389
	ldr	r1, .L2418+12
	ldr	r0, [r10, #-1268]
	str	r2, [sp, #20]
	ldrh	r1, [r1]
	bl	js_hash
	ldr	r2, [sp, #20]
	cmp	r2, r0
	mvnne	r2, #0
	strne	r2, [r10, #-1276]
.L2389:
	ldr	r2, [r10, #-1276]
	cmn	r2, #1
	beq	.L2390
	ldrh	r2, [r6, #8]
	cmp	r2, r9
	bcs	.L2390
	ldrh	r1, [r4, #4]
	ldrh	r0, [r6]
	cmp	r0, r1
	ldreq	r1, [r10, #-1272]
	streq	r1, [r7, r2, asl #2]
.L2390:
	add	fp, fp, #1
	b	.L2416
.L2388:
	ldr	r3, [sp, #4]
	add	r3, r3, #1
	str	r3, [sp, #4]
	b	.L2376
.L2383:
	mov	r0, r4
	bl	ftl_map_blk_gc
	mov	r0, r4
	bl	ftl_map_blk_gc
	mov	r0, #0
	add	sp, sp, #28
	@ sp needed
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L2419:
	.align	2
.L2418:
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
	ldr	r3, .L2422
	sub	r2, r3, #1648
	add	r0, r3, #1072
	add	r0, r0, #4
	ldrh	r2, [r2]
	strh	r2, [r0, #10]	@ movhi
	ldr	r2, .L2422+4
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
.L2423:
	.align	2
.L2422:
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
	ldr	r4, .L2429
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
	ldr	r0, .L2429+4
	mov	r1, #0
	mov	lr, #12
	sub	r7, r0, #28
	mov	r5, r1
.L2425:
	ldrh	r2, [r0]
	add	ip, r1, #1
	uxth	r1, r1
	ldr	r3, .L2429
	cmp	r2, r1
	bls	.L2428
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
	b	.L2425
.L2428:
	ldr	r2, .L2429+8
	movw	r0, #1028
	mvn	r1, #0
	strh	r1, [r3, r0]	@ movhi
	strh	r1, [r2, #2]	@ movhi
	ldr	r1, [r3, #-1640]
	strh	r1, [r2, #10]	@ movhi
	ldr	r1, .L2429+12
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
.L2430:
	.align	2
.L2429:
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
	ldr	r0, .L2433
	bl	FtlMapTblRecovery
	mov	r0, #0
	ldmfd	sp!, {r3, pc}
.L2434:
	.align	2
.L2433:
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
	ldr	r3, .L2445
	stmfd	sp!, {r4, r5, lr}
	.save {r4, r5, lr}
	movw	r4, #65535
	ldrh	lr, [r3, #-8]
	add	r3, r2, #7
	add	r3, r0, r3, asl #1
.L2436:
	cmp	r2, lr
	bcs	.L2443
	ldrh	ip, [r3, #2]!
	add	r2, r2, #1
	cmp	ip, r4
	uxth	r2, r2
	ldrneh	ip, [r0, #4]
	addne	ip, ip, #1
	strneh	ip, [r0, #4]	@ movhi
	b	.L2436
.L2443:
	ldr	r3, .L2445+4
	add	ip, r0, #14
	mov	r2, #0
	movw	r5, #65535
	ldrh	r4, [r3, #-4]
.L2439:
	uxth	r3, r2
	cmp	r3, lr
	bcs	.L2444
	ldrh	r3, [ip, #2]!
	add	r2, r2, #1
	cmp	r3, r5
	ldrneh	r3, [r0, #4]
	addne	r3, r4, r3
	subne	r3, r3, #1
	rsbne	r3, r1, r3
	strneh	r3, [r0, #4]	@ movhi
	b	.L2439
.L2444:
	ldmfd	sp!, {r4, r5, pc}
.L2446:
	.align	2
.L2445:
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
	ldr	r6, .L2457
	ldr	r4, [r6, #-1408]
	mov	r7, r0, asl #1
	ldrh	r3, [r4, r7]
	cmp	r3, #0
	addne	r3, r3, #1
	strneh	r3, [r4, r7]	@ movhi
	bne	.L2449
	ldr	r4, [r6, #876]
	cmp	r4, #0
	beq	.L2449
	ldr	r1, [r6, #-1356]
	add	r2, r6, #880
	ldr	lr, .L2457+4
	movw	r9, #65535
	rsb	r4, r1, r4
	ldrh	ip, [r2]
	mov	r8, r2
	mov	r4, r4, asr #1
	mul	r4, lr, r4
	mov	lr, #6
	uxth	r4, r4
.L2450:
	uxth	r2, r3
	cmp	r2, ip
	bcs	.L2449
	cmp	r4, r0
	bne	.L2451
	mov	r1, r4
	ldr	r0, .L2457+8
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
	b	.L2449
.L2451:
	mul	r4, lr, r4
	add	r3, r3, #1
	ldrh	r4, [r1, r4]
	cmp	r4, r9
	bne	.L2450
.L2449:
	mov	r0, r5
	add	r1, sp, #4
	mov	r2, #1
	bl	log2phys
	add	sp, sp, #12
	@ sp needed
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, pc}
.L2458:
	.align	2
.L2457:
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
	ldr	r3, .L2473
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
.L2460:
	uxth	r3, r5
	cmp	r3, r8
	bcs	.L2472
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
	b	.L2460
.L2472:
	ldr	r2, .L2473+4
	ldrb	r1, [r4, #7]	@ zero_extendqisi2
	sub	r3, r2, #1664
	ldrh	r3, [r3, #-4]
	smulbb	r3, r1, r3
	strh	r3, [r4, #4]	@ movhi
	mov	r3, #0
	strb	r3, [r4, #9]
	ldr	r3, [r2, #-1872]
	cmp	r3, #0
	beq	.L2463
	ldrh	r3, [r4]
	ldr	r2, [r2, #-1416]
	mov	r3, r3, asl #1
	ldrh	r3, [r2, r3]
	cmp	r3, #79
	movls	r3, #1
	strlsb	r3, [r4, #9]
.L2463:
	ldr	r3, .L2473+8
	mov	r0, #0
	ldrb	r3, [r3, #32]	@ zero_extendqisi2
	cmp	r3, #0
	movne	r3, #1
	strneb	r3, [r4, #9]
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, pc}
.L2474:
	.align	2
.L2473:
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
	ldr	r4, .L2504
	.pad #44
	sub	sp, sp, #44
	movw	r7, #1128
	ldr	r9, .L2504+4
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
	bne	.L2476
.L2487:
	mvn	r0, #0
	b	.L2477
.L2476:
	mov	r1, #1
	bl	FtlGetLastWrittenPage
	ldrh	r3, [r4, r7]
	mov	r7, r4
	uxth	r5, r0
	str	r3, [sp, #28]
	add	r0, r0, #1
	strh	r0, [r9, #2]	@ movhi
.L2478:
	sxth	r3, r5
	cmp	r3, #0
	blt	.L2486
	ldrsh	fp, [sp, #28]
	mov	r1, #1
	ldr	r0, .L2504+8
	mov	r2, r1
	orr	r3, r3, fp, asl #10
	str	r3, [r4, #-1272]
	ldr	r3, [r4, #-1476]
	str	r3, [r4, #-1268]
	bl	FlashReadPages
	ldr	r3, .L2504+12
	ldrb	r3, [r3, #32]	@ zero_extendqisi2
	cmp	r3, #0
	beq	.L2479
	ldr	r8, [r7, #-1264]
	ldr	r3, [r8, #12]
	cmp	r3, #0
	beq	.L2479
	ldr	r2, [r7, #-1268]
	ldr	r10, .L2504+16
	str	r3, [sp, #36]
	mov	r0, r2
	str	r2, [sp, #32]
	ldrh	r1, [r10]
	bl	js_hash
	ldr	r3, [sp, #36]
	cmp	r3, r0
	beq	.L2479
	cmp	r5, #0
	ldr	r2, [sp, #32]
	bne	.L2480
	ldrh	r1, [r9, #4]
	ldr	ip, .L2504+4
	cmp	fp, r1
	beq	.L2480
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
	ldr	r0, .L2504+20
	str	r3, [sp, #16]
	ldr	r3, [r7, #-1272]
	ldr	r2, [r7, #-1276]
	bl	printk
	ldrh	r5, [r10, #-8]
	ldr	ip, [sp, #28]
	ldrh	r3, [ip, #4]
	str	r3, [sp, #28]
	b	.L2482
.L2480:
	mvn	r3, #0
	str	r3, [r4, #-1276]
.L2479:
	ldr	r3, [r4, #-1276]
	cmn	r3, #1
	beq	.L2482
	ldr	r3, [r7, #-1476]
	ldr	r2, .L2504+24
	ldr	r3, [r3]
	cmp	r3, r2
	bne	.L2482
	ldr	r3, [r7, #-1444]
	ldrh	r2, [r3]
	movw	r3, #61604
	cmp	r2, r3
	bne	.L2482
.L2486:
	ldr	r5, .L2504
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
	beq	.L2484
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
	b	.L2484
.L2482:
	sub	r5, r5, #1
	uxth	r5, r5
	b	.L2478
.L2484:
	ldr	r2, [r4, #816]
	ldr	r3, .L2504+24
	ldr	r5, .L2504
	cmp	r2, r3
	bne	.L2487
	sub	r2, r5, #1712
	add	r8, r5, #816
	ldrb	r1, [r5, #826]	@ zero_extendqisi2
	ldrh	r2, [r2, #-2]
	ldrh	r3, [r8, #8]
	cmp	r1, r2
	strh	r3, [r9, #6]	@ movhi
	bne	.L2487
	sub	r2, r5, #1664
	sub	r1, r5, #1648
	ldr	r7, .L2504+28
	ldrh	r2, [r2, #-4]
	ldrh	r1, [r1, #-14]
	str	r3, [r7, #1156]
	mul	r2, r3, r2
	str	r2, [r5, #-1284]
	mul	r2, r1, r2
	ldrh	r1, [r6, #-8]
	mov	r6, #0
	str	r2, [r5, #-2740]
	ldr	r2, .L2504+32
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
	ldr	r3, .L2504+36
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
	ldrhi	r2, .L2504
	strhi	r3, [r2, #-1612]
	movw	r3, #65535
	cmp	r1, r3
	beq	.L2490
	ldr	r0, .L2504+40
	bl	make_superblock
.L2490:
	ldrh	r2, [r6]
	movw	r3, #65535
	cmp	r2, r3
	beq	.L2491
	ldr	r0, .L2504+44
	bl	make_superblock
.L2491:
	ldrh	r2, [r9]
	movw	r3, #65535
	cmp	r2, r3
	beq	.L2492
	ldr	r0, .L2504+48
	bl	make_superblock
.L2492:
	movw	r3, #1164
	ldrh	r2, [r7, r3]
	movw	r3, #65535
	cmp	r2, r3
	beq	.L2493
	ldr	r0, .L2504+36
	bl	make_superblock
.L2493:
	mov	r0, #0
.L2477:
	add	sp, sp, #44
	@ sp needed
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L2505:
	.align	2
.L2504:
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
	ldr	r7, .L2520
	ldr	r1, .L2520+4
	sub	r5, r7, #1664
	ldrh	r9, [r5, #-4]
	mov	r6, r0
	ldr	r0, .L2520+8
	bl	printk
	ldr	r2, [r7, #-1408]
	mov	r3, r6, asl #1
	mov	r1, r6
	ldr	r0, .L2520+12
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
	beq	.L2507
	mov	r0, r6
	bl	ftl_get_blk_mode
	cmp	r0, #1
	mov	r4, r0
	ldreqh	r9, [r5, #-2]
.L2507:
	ldr	r0, .L2520+16
	mov	r1, r4
	mov	r2, r9
	ldrh	r3, [r5, #-4]
	bl	printk
	ldr	r8, .L2520
	mov	r6, #0
.L2508:
	ldr	r3, .L2520+20
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
	ldr	r3, .L2520+24
	ldrh	r3, [r3]
	str	r3, [sp, #36]
	ldr	r3, [r8, #-1436]
	str	r3, [sp, #40]
	ldr	r3, .L2520+28
	ldrh	ip, [r3]
.L2509:
	uxth	r3, r2
	cmp	r3, r7
	bcs	.L2518
	ldrh	r3, [r0, #2]!
	cmp	r3, lr
	beq	.L2510
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
.L2510:
	add	r2, r2, #1
	b	.L2509
.L2518:
	ldr	r0, [r8, #-1504]
	mov	r1, r5
	mov	r2, r4
	mov	r10, #0
	bl	FlashReadPages
	mov	fp, #36
.L2512:
	uxth	r3, r10
	cmp	r3, r5
	bcs	.L2519
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
	ldr	r0, .L2520+32
	str	r3, [sp, #20]
	ldr	r2, [lr, r2]
	ldr	r3, [ip, #4]
	bl	printk
	b	.L2512
.L2519:
	add	r6, r6, #1
	uxth	r6, r6
	cmp	r6, r9
	bne	.L2508
.L2514:
	mov	r0, #0
	add	sp, sp, #100
	@ sp needed
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L2521:
	.align	2
.L2520:
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
	ldr	r0, .L2537
	mov	r7, #0
	ldr	r1, .L2537+4
	bl	printk
	ldr	r5, .L2537+8
	mov	r6, r5
.L2523:
	ldr	r3, .L2537+12
	uxth	r4, r7
	ldrh	r3, [r3]
	cmp	r3, r4
	bls	.L2533
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
	ldr	r0, .L2537+16
	bl	printk
	mov	r0, r8
	bl	make_superblock
	ldr	r3, .L2537+20
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
.L2524:
	ldr	r1, [sp, #28]
	uxth	r3, r2
	cmp	r3, r1
	bcs	.L2534
	ldrh	r3, [r0, #2]!
	cmp	r3, r9
	beq	.L2525
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
.L2525:
	add	r2, r2, #1
	b	.L2524
.L2534:
	ldr	r0, [r6, #-1504]
	mov	r1, r4
	mov	r2, #0
	mov	r8, #0
	bl	FlashReadPages
	mov	r9, #36
.L2527:
	uxth	r3, r8
	cmp	r3, r4
	bcs	.L2535
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
	ldr	r0, .L2537+24
	str	r3, [sp, #20]
	ldr	r2, [lr, r2]
	ldr	r3, [ip, #4]
	bl	printk
	b	.L2527
.L2535:
	ldr	r0, [r6, #-1504]
	mov	r1, r4
	mov	r2, #1
	mov	r8, #0
	bl	FlashReadPages
	mov	r9, #36
.L2529:
	uxth	r3, r8
	cmp	r3, r4
	bcs	.L2536
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
	ldr	r0, .L2537+28
	str	r3, [sp, #20]
	ldr	r2, [lr, r2]
	ldr	r3, [ip, #4]
	bl	printk
	b	.L2529
.L2536:
	add	r7, r7, #1
	b	.L2523
.L2533:
	mov	r0, #0
	add	sp, sp, #92
	@ sp needed
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L2538:
	.align	2
.L2537:
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
	ldr	r4, .L2551
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
.L2540:
	ldr	r3, .L2551+4
	uxth	r7, r5
	sxth	r10, r7
	ldrh	r2, [r3]
	cmp	r10, r2
	bge	.L2547
	sub	r3, r3, #8
	ldr	r2, .L2551+8
	mov	ip, r7
	ldrh	r3, [r3]
	str	r3, [sp]
	ldrh	r3, [r2]
	mov	r2, #0
	mov	r6, r2
	str	r3, [sp, #4]
.L2548:
	ldr	r3, [sp]
	sxth	r1, r2
	cmp	r1, r3
	bge	.L2550
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
	b	.L2548
.L2550:
	cmp	r6, #0
	beq	.L2543
	sxth	r1, r6
	mov	r0, #32768
	bl	__aeabi_idiv
	uxth	r6, r0
	b	.L2544
.L2543:
	sxth	r7, r7
	ldr	r2, [r4, #-1408]
	mvn	r1, #0
	mov	r7, r7, asl #1
	strh	r1, [r2, r7]	@ movhi
.L2544:
	mov	r1, r10, asl #1
	ldr	r2, [r4, #-1356]
	add	r0, r1, r10
	add	r2, r2, r0, asl #1
	strh	r6, [r2, #4]	@ movhi
	ldr	r2, .L2551+12
	ldrh	r0, [r2]
	cmp	r10, r0
	beq	.L2545
	ldrh	r0, [r2, #48]
	cmp	r10, r0
	beq	.L2545
	ldrh	r2, [r2, #96]
	cmp	r10, r2
	beq	.L2545
	ldr	r3, [fp, #-1408]
	uxth	r0, r5
	ldrh	r3, [r3, r1]
	cmp	r3, #0
	bne	.L2546
	add	r8, r8, #1
	uxth	r8, r8
	bl	INSERT_FREE_LIST
	b	.L2545
.L2546:
	add	r9, r9, #1
	uxth	r9, r9
	bl	INSERT_DATA_LIST
.L2545:
	add	r5, r5, #1
	b	.L2540
.L2547:
	ldr	r3, [sp, #8]
	mov	r0, #0
	strh	r9, [r3]	@ movhi
	ldr	r3, [sp, #12]
	strh	r8, [r3]	@ movhi
	add	sp, sp, #28
	@ sp needed
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L2552:
	.align	2
.L2551:
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
	ldr	r3, .L2555
	ldr	r2, .L2555+4
	ldr	r1, [r3, #1212]
	cmp	r1, r2
	bxne	lr
	ldr	r2, .L2555+8
	mov	r0, #0
	str	r2, [r3, #1216]
	ldr	r2, [r3, #1724]
	str	r2, [r3, #1300]
	ldr	r2, [r3, #1728]
	str	r2, [r3, #1304]
	ldr	r2, .L2555+12
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
	ldr	r2, .L2555+16
	b	FtlVendorPartWrite
.L2556:
	.align	2
.L2555:
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
	ldr	r3, .L2565
	ldr	r2, [r3, #-1872]
	cmp	r2, #0
	moveq	r2, #32
	beq	.L2558
	ldr	r2, [r3, #-1568]
	cmp	r2, #39
	movhi	r2, #32
	movls	r2, #4
.L2558:
	ldr	lr, .L2565+4
	movw	ip, #1732
	ldrh	r1, [lr, ip]
	cmp	r1, #31
	addls	r1, r1, #1
	strlsh	r1, [lr, ip]	@ movhi
	movls	r2, #1
	cmp	r0, #0
	bne	.L2560
	ldr	r1, [r3, #-1420]
	ldr	r0, [r1, #20]
	ldr	r1, [r1, #16]
	add	r2, r2, r0
	cmp	r1, r2
	bcc	.L2561
.L2560:
	ldr	r2, [r3, #-1420]
	mov	r0, #64
	ldr	r1, [r2, #16]
	str	r1, [r2, #20]
	ldr	r1, .L2565+8
	str	r1, [r2]
	ldr	r2, [r3, #-1420]
	ldr	r3, .L2565+12
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
.L2561:
	mov	r0, #0
	ldmfd	sp!, {r3, pc}
.L2566:
	.align	2
.L2565:
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
	ldr	r5, .L2573
	mov	r1, #1
	ldr	r2, .L2573+4
	bl	FtlVendorPartRead
	ldr	r4, .L2573+8
	ldr	r3, [r5, #1212]
	cmp	r3, r4
	beq	.L2568
	ldr	r0, .L2573+4
	mov	r1, #0
	mov	r2, #512
	bl	ftl_memset
	str	r4, [r5, #1212]
.L2568:
	ldr	r2, [r5, #1212]
	ldr	r3, .L2573
	cmp	r2, r4
	ldr	r4, .L2573+12
	bne	.L2569
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
.L2569:
	ldr	r0, [r5, #1280]
	mov	r2, #0
	ldr	r1, .L2573+16
	ldr	r3, .L2573+12
	cmp	r0, r1
	str	r2, [r4, #-1624]
	bne	.L2570
	ldrb	r1, [r3, #-2744]	@ zero_extendqisi2
	cmp	r1, r2
	beq	.L2571
	ldr	r3, .L2573
	str	r2, [r3, #1280]
	bl	Ftl_save_ext_data
	b	.L2570
.L2571:
	ldr	r0, .L2573+20
	mov	r2, #1
	ldr	r1, .L2573+24
	str	r2, [r3, #-1872]
	bl	printk
.L2570:
	ldr	r3, .L2573+28
	ldr	r2, [r4, #-1580]
	ldr	r0, [r4, #-1584]
	ldrh	r1, [r3, #-12]
	mla	r0, r0, r1, r2
	ldrh	r1, [r3, #-64]
	bl	__aeabi_uidiv
	str	r0, [r5, #1736]
	ldmfd	sp!, {r3, r4, r5, pc}
.L2574:
	.align	2
.L2573:
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
	ldr	r7, .L2594
	ldr	r9, .L2594+4
	ldrh	r4, [r7, #-6]
	add	r7, r7, #1664
	sub	r4, r4, #16
	mov	r5, r7
.L2576:
	ldrh	r3, [r9]
	cmp	r4, r3
	bge	.L2592
	uxth	r10, r4
	mov	r0, r10
	bl	ftl_get_blk_mode
	ldrb	r3, [r7, #-2744]	@ zero_extendqisi2
	cmp	r3, #0
	beq	.L2577
	ldr	r3, .L2594+8
	ldrh	r2, [r3]
	cmp	r4, r2
	bge	.L2578
	ldrh	r3, [r3, #74]
	cmp	r4, r3
	blt	.L2578
.L2577:
	cmp	r0, #1
	bne	.L2579
.L2578:
	ldr	r3, .L2594+12
	mov	r6, #-2147483648
	ldrh	r8, [r3]
	b	.L2580
.L2579:
	ldr	r3, .L2594+16
	mov	r6, #0
	ldrh	r8, [r3]
.L2580:
	mov	r1, r4
	mov	r2, r8
	mov	r3, r6
	ldr	r0, .L2594+20
	bl	printk
	mov	r0, r10
	bl	FtlBbmIsBadBlock
	cmp	r0, #0
	beq	.L2581
	ldr	r0, .L2594+24
	mov	r1, r4
	mov	r2, r8
	mov	r3, r6
	bl	printk
.L2581:
	add	r6, r6, r4, asl #10
	mov	r10, #0
.L2582:
	cmp	r10, r8
	beq	.L2593
	add	r3, r6, r10
	str	r3, [r5, #-1272]
	ldr	r3, [r5, #-1476]
	mov	r2, #0
	mov	r1, #1
	ldr	r0, .L2594+28
	str	r2, [r5, #-1276]
	add	r10, r10, #1
	str	r3, [r5, #-1268]
	ldr	r3, [r5, #-1444]
	str	r3, [r5, #-1264]
	bl	FlashReadPages
	ldr	r3, [r5, #-1264]
	ldr	r2, [r5, #-1268]
	ldr	r0, .L2594+32
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
	b	.L2582
.L2593:
	add	r4, r4, #1
	b	.L2576
.L2592:
	ldr	r0, .L2594+36
	ldr	r1, .L2594+40
	add	sp, sp, #24
	@ sp needed
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, lr}
	b	printk
.L2595:
	.align	2
.L2594:
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
	ldr	r3, .L2606
	mov	r7, r0
	mov	r4, r5
	add	r6, r3, #20
	ldrh	r8, [r3, #-8]
	ldrh	r9, [r3, #60]
.L2597:
	uxth	r3, r5
	cmp	r3, r8
	bcs	.L2605
	ldrb	r0, [r6, r5]	@ zero_extendqisi2
	mov	r1, r7
	bl	V2P_block
	add	r5, r5, #1
	bl	FtlBbmIsBadBlock
	cmp	r0, #0
	addeq	r4, r4, r9
	uxtheq	r4, r4
	b	.L2597
.L2605:
	cmp	r4, #0
	beq	.L2600
	mov	r1, r4
	mov	r0, #32768
	bl	__aeabi_idiv
	uxth	r4, r0
.L2600:
	ldr	r3, .L2606+4
	mov	r2, #6
	mov	r0, #0
	ldr	r3, [r3, #-1356]
	mla	r7, r2, r7, r3
	strh	r4, [r7, #4]	@ movhi
	ldmfd	sp!, {r3, r4, r5, r6, r7, r8, r9, pc}
.L2607:
	.align	2
.L2606:
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
	ldr	r2, .L2611
	ldr	r0, [r2, #876]
	cmp	r0, #0
	bxeq	lr
	ldr	r3, [r2, #-1356]
	rsb	r0, r3, r0
	ldr	r3, .L2611+4
	mov	r0, r0, asr #1
	mul	r0, r3, r0
	ldr	r3, [r2, #-1416]
	uxth	r0, r0
	mov	r0, r0, asl #1
	ldrh	r0, [r3, r0]
	bx	lr
.L2612:
	.align	2
.L2611:
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
	ldr	r1, .L2623
	ldr	r3, [r1, #876]
	cmp	r3, #0
	beq	.L2619
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
	ldr	r2, .L2623+4
	mul	r3, r2, r3
	mov	r2, #0
	uxth	r3, r3
.L2616:
	uxth	lr, r2
	cmp	lr, r0
	bcs	.L2618
	mul	lr, r4, r3
	add	r2, r2, #1
	ldrh	lr, [ip, lr]
	cmp	lr, r5
	bne	.L2620
.L2618:
	ldr	r2, [r1, #-1416]
	mov	r3, r3, asl #1
	ldrh	r0, [r2, r3]
	ldmfd	sp!, {r4, r5, pc}
.L2620:
	mov	r3, lr
	b	.L2616
.L2619:
	mov	r0, r3
	bx	lr
.L2624:
	.align	2
.L2623:
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
	ldr	r6, .L2636
	add	r5, r7, #12
	ldr	r1, .L2636+4
	.pad #32
	sub	sp, sp, #32
	bl	strcpy
	mov	r0, r5
	ldr	r1, .L2636+8
	ldr	r2, [r6, #-2772]
	bl	sprintf
	ldr	r1, .L2636+12
	ldr	r2, [r6, #-1652]
	add	r5, r5, r0
	mov	r0, r5
	bl	sprintf
	ldr	r3, .L2636+16
	ldr	r3, [r3, #3444]
	cmp	r3, #1
	add	r4, r5, r0
	bne	.L2631
	add	r0, sp, #16
	add	r1, sp, #20
	add	r2, sp, #24
	add	r3, sp, #28
	bl	NandcGetTimeCfg
	mov	r0, r4
	ldr	r1, .L2636+20
	add	r10, r6, #880
	ldr	r8, .L2636+24
	add	r9, r6, #816
	ldr	r3, [sp, #24]
	ldr	r2, [sp, #16]
	str	r3, [sp]
	ldr	r3, [sp, #28]
	str	r3, [sp, #4]
	ldr	r3, [sp, #20]
	bl	sprintf
	ldr	r1, .L2636+28
	add	r4, r4, r0
	add	r5, r4, #10
	mov	r0, r4
	sub	r4, r6, #1344
	bl	strcpy
	mov	r0, r5
	ldr	r1, .L2636+32
	ldr	r2, [r6, #-1284]
	bl	sprintf
	ldr	r1, .L2636+36
	ldr	r2, [r6, #1124]
	add	r5, r5, r0
	mov	r0, r5
	bl	sprintf
	ldr	r1, .L2636+40
	ldr	r2, [r6, #-1588]
	add	r5, r5, r0
	mov	r0, r5
	bl	sprintf
	ldr	r1, .L2636+44
	ldr	r2, [r6, #-1600]
	add	r5, r5, r0
	mov	r0, r5
	bl	sprintf
	ldr	r1, .L2636+48
	ldr	r2, [r6, #-1604]
	add	r5, r5, r0
	mov	r0, r5
	bl	sprintf
	ldr	r1, .L2636+52
	ldr	r2, [r6, #-1596]
	add	r5, r5, r0
	mov	r0, r5
	bl	sprintf
	ldr	r1, .L2636+56
	ldr	r2, [r6, #-1592]
	add	r5, r5, r0
	mov	r0, r5
	bl	sprintf
	ldr	r1, .L2636+60
	ldr	r2, [r6, #-1608]
	add	r5, r5, r0
	mov	r0, r5
	bl	sprintf
	ldr	r2, [r8, #1724]
	ldr	r1, .L2636+64
	mov	r2, r2, lsr #11
	add	r5, r5, r0
	mov	r0, r5
	bl	sprintf
	ldr	r2, [r8, #1728]
	ldr	r1, .L2636+68
	mov	r2, r2, lsr #11
	add	r5, r5, r0
	mov	r0, r5
	bl	sprintf
	ldr	r1, .L2636+72
	ldr	r2, [r6, #-1616]
	add	r5, r5, r0
	mov	r0, r5
	bl	sprintf
	ldr	r1, .L2636+76
	ldr	r2, [r6, #-1612]
	add	r5, r5, r0
	mov	r0, r5
	bl	sprintf
	add	r5, r5, r0
	bl	FtlBbtCalcTotleCnt
	ldrh	r2, [r4, #2]
	ldr	r1, .L2636+80
	mov	r3, r0
	mov	r0, r5
	bl	sprintf
	ldr	r1, .L2636+84
	ldrh	r2, [r10]
	add	r5, r5, r0
	mov	r0, r5
	bl	sprintf
	ldr	r1, .L2636+88
	ldr	r2, [r6, #-1584]
	add	r5, r5, r0
	mov	r0, r5
	bl	sprintf
	ldr	r1, .L2636+92
	ldr	r2, [r6, #-1580]
	add	r5, r5, r0
	mov	r0, r5
	bl	sprintf
	ldr	r1, .L2636+96
	ldr	r2, [r8, #1736]
	add	r5, r5, r0
	mov	r0, r5
	bl	sprintf
	ldr	r1, .L2636+100
	ldr	r2, [r6, #-1576]
	add	r5, r5, r0
	mov	r0, r5
	bl	sprintf
	ldr	r1, .L2636+104
	ldr	r2, [r6, #-1572]
	add	r5, r5, r0
	mov	r0, r5
	bl	sprintf
	ldr	r1, .L2636+108
	ldr	r2, [r6, #-1568]
	add	r5, r5, r0
	mov	r0, r5
	bl	sprintf
	ldrh	r2, [r9, #30]
	ldr	r1, .L2636+112
	add	r5, r5, r0
	mov	r0, r5
	bl	sprintf
	ldrh	r2, [r9, #28]
	ldr	r1, .L2636+116
	movw	r9, #1164
	add	r5, r5, r0
	mov	r0, r5
	bl	sprintf
	ldr	r1, .L2636+120
	ldr	r2, [r6, #-2740]
	add	r5, r5, r0
	mov	r0, r5
	bl	sprintf
	ldr	r1, .L2636+124
	ldr	r2, [r6, #-1636]
	add	r5, r5, r0
	mov	r0, r5
	bl	sprintf
	ldr	r1, .L2636+128
	ldr	r2, [r6, #-1740]
	add	r5, r5, r0
	mov	r0, r5
	bl	sprintf
	ldrh	r2, [r4, #110]
	ldr	r1, .L2636+132
	add	r5, r5, r0
	mov	r0, r5
	bl	sprintf
	sub	r3, r6, #1728
	ldr	r1, .L2636+136
	ldrh	r2, [r3]
	add	r5, r5, r0
	mov	r0, r5
	bl	sprintf
	movw	r3, #1160
	ldrh	r2, [r8, r3]
	ldr	r1, .L2636+140
	add	r5, r5, r0
	mov	r0, r5
	bl	sprintf
	ldr	r1, .L2636+144
	ldr	r2, [r6, #-1724]
	add	r5, r5, r0
	mov	r0, r5
	bl	sprintf
	movw	r3, #1128
	ldrh	r2, [r6, r3]
	ldr	r1, .L2636+148
	add	r5, r5, r0
	mov	r0, r5
	bl	sprintf
	ldrh	r2, [r4, #-4]
	ldr	r1, .L2636+152
	add	r4, r6, #884
	add	r5, r5, r0
	mov	r0, r5
	bl	sprintf
	ldrh	r2, [r4, #2]
	ldr	r1, .L2636+156
	add	r5, r5, r0
	mov	r0, r5
	bl	sprintf
	ldr	r1, .L2636+160
	ldrb	r2, [r6, #890]	@ zero_extendqisi2
	add	r5, r5, r0
	mov	r0, r5
	bl	sprintf
	ldrh	r2, [r4]
	ldr	r1, .L2636+164
	add	r5, r5, r0
	mov	r0, r5
	bl	sprintf
	ldr	r1, .L2636+168
	ldrb	r2, [r6, #892]	@ zero_extendqisi2
	add	r5, r5, r0
	mov	r0, r5
	bl	sprintf
	ldrh	r2, [r4, #4]
	ldr	r1, .L2636+172
	add	r5, r5, r0
	mov	r0, r5
	bl	sprintf
	ldrh	r3, [r4]
	ldr	r2, [r6, #-1408]
	add	r4, r6, #932
	ldr	r1, .L2636+176
	mov	r3, r3, asl #1
	ldrh	r2, [r2, r3]
	add	r5, r5, r0
	mov	r0, r5
	bl	sprintf
	ldrh	r2, [r4, #2]
	ldr	r1, .L2636+180
	add	r5, r5, r0
	mov	r0, r5
	bl	sprintf
	ldr	r1, .L2636+184
	ldrb	r2, [r6, #938]	@ zero_extendqisi2
	add	r5, r5, r0
	mov	r0, r5
	bl	sprintf
	ldrh	r2, [r4]
	ldr	r1, .L2636+188
	add	r5, r5, r0
	mov	r0, r5
	bl	sprintf
	ldr	r1, .L2636+192
	ldrb	r2, [r6, #940]	@ zero_extendqisi2
	add	r5, r5, r0
	mov	r0, r5
	bl	sprintf
	ldrh	r2, [r4, #4]
	ldr	r1, .L2636+196
	add	r5, r5, r0
	mov	r0, r5
	bl	sprintf
	ldrh	r3, [r4]
	ldr	r2, [r6, #-1408]
	add	r4, r6, #980
	ldr	r1, .L2636+200
	mov	r3, r3, asl #1
	ldrh	r2, [r2, r3]
	add	r5, r5, r0
	mov	r0, r5
	bl	sprintf
	ldrh	r2, [r4, #2]
	ldr	r1, .L2636+204
	add	r5, r5, r0
	mov	r0, r5
	bl	sprintf
	ldr	r1, .L2636+208
	ldrb	r2, [r6, #986]	@ zero_extendqisi2
	add	r5, r5, r0
	mov	r0, r5
	bl	sprintf
	ldrh	r2, [r4]
	ldr	r1, .L2636+212
	add	r5, r5, r0
	mov	r0, r5
	bl	sprintf
	ldr	r1, .L2636+216
	ldrb	r2, [r6, #988]	@ zero_extendqisi2
	add	r5, r5, r0
	mov	r0, r5
	bl	sprintf
	ldrh	r2, [r4, #4]
	ldr	r1, .L2636+220
	ldr	r4, .L2636+224
	add	r5, r5, r0
	mov	r0, r5
	bl	sprintf
	ldrh	r2, [r4, #2]
	ldr	r1, .L2636+228
	add	r5, r5, r0
	mov	r0, r5
	bl	sprintf
	ldr	r1, .L2636+232
	ldrb	r2, [r8, #1170]	@ zero_extendqisi2
	add	r5, r5, r0
	mov	r0, r5
	bl	sprintf
	ldr	r1, .L2636+236
	ldrh	r2, [r8, r9]
	add	r5, r5, r0
	mov	r0, r5
	bl	sprintf
	ldr	r1, .L2636+240
	ldrb	r2, [r8, #1172]	@ zero_extendqisi2
	add	r5, r5, r0
	mov	r0, r5
	bl	sprintf
	ldrh	r2, [r4, #4]
	ldr	r1, .L2636+244
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
	ldr	r1, .L2636+248
	ldr	r3, [r8, #1292]
	bl	sprintf
	ldr	r1, .L2636+252
	ldr	r2, [r8, #1284]
	add	r4, r5, r0
	sub	r5, r6, #1520
	mov	r0, r4
	bl	sprintf
	ldr	r1, .L2636+256
	ldr	r2, [r8, #1308]
	add	r4, r4, r0
	mov	r0, r4
	bl	sprintf
	ldr	r1, .L2636+260
	ldrh	r2, [r5, #-12]
	add	r4, r4, r0
	mov	r0, r4
	bl	sprintf
	ldr	r1, .L2636+264
	ldrh	r2, [r5, #-10]
	add	r4, r4, r0
	mov	r0, r4
	bl	sprintf
	ldr	r1, .L2636+268
	ldr	r2, [r6, #-1548]
	add	r4, r4, r0
	mov	r0, r4
	bl	sprintf
	ldr	r1, .L2636+272
	ldrh	r2, [r5, #-8]
	add	r4, r4, r0
	mov	r0, r4
	bl	sprintf
	add	r4, r4, r0
	bl	GetFreeBlockMinEraseCount
	ldr	r1, .L2636+276
	mov	r2, r0
	mov	r0, r4
	bl	sprintf
	add	r4, r4, r0
	ldrh	r0, [r10]
	bl	GetFreeBlockMaxEraseCount
	ldr	r1, .L2636+280
	mov	r2, r0
	mov	r0, r4
	bl	sprintf
	ldrh	r3, [r8, r9]
	movw	r2, #65535
	cmp	r3, r2
	add	r4, r4, r0
	beq	.L2628
	ldr	r2, [r6, #-1408]
	mov	r3, r3, asl #1
	mov	r0, r4
	ldr	r1, .L2636+284
	ldrh	r2, [r2, r3]
	bl	sprintf
	add	r4, r4, r0
.L2628:
	mov	r0, #0
	mov	r5, #0
	bl	List_get_gc_head_node
	movw	r10, #65535
	mov	r9, #6
	uxth	r3, r0
.L2630:
	cmp	r3, r10
	beq	.L2629
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
	ldr	r1, .L2636+288
	str	r2, [sp, #8]
	mov	r2, r5
	bl	sprintf
	add	r5, r5, #1
	ldr	r3, [r6, #-1356]
	cmp	r5, #16
	ldrh	r3, [r3, r8]
	add	r4, r4, r0
	bne	.L2630
.L2629:
	ldr	r2, [r6, #-1356]
	mov	r5, #0
	ldr	r3, [r6, #876]
	movw	r10, #65535
	mov	r9, #6
	rsb	r3, r2, r3
	ldr	r2, .L2636+292
	mov	r3, r3, asr #1
	mul	r3, r2, r3
	uxth	r3, r3
.L2632:
	cmp	r3, r10
	beq	.L2631
	mul	r8, r9, r3
	ldr	r2, [r6, #-1356]
	ldr	r1, [r6, #-1416]
	mov	r0, r4
	add	r2, r2, r8
	ldrh	r2, [r2, #4]
	str	r2, [sp]
	mov	r2, r3, asl #1
	ldrh	r2, [r1, r2]
	ldr	r1, .L2636+296
	str	r2, [sp, #4]
	mov	r2, r5
	bl	sprintf
	add	r5, r5, #1
	ldr	r3, [r6, #-1356]
	cmp	r5, #4
	ldrh	r3, [r3, r8]
	add	r4, r4, r0
	bne	.L2632
.L2631:
	rsb	r0, r7, r4
	add	sp, sp, #32
	@ sp needed
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, pc}
.L2637:
	.align	2
.L2636:
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
	ldr	r1, .L2640
	ldr	r2, .L2640+4
	bl	sprintf
	add	r4, r5, r0
	mov	r0, r4
	bl	FtlPrintInfo2buf
	add	r0, r4, r0
	rsb	r0, r5, r0
	ldmfd	sp!, {r3, r4, r5, pc}
.L2641:
	.align	2
.L2640:
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
	ldr	r5, .L2672
	ldr	r4, .L2672+4
	ldr	r6, [r5, #1736]
	ldr	r3, [r4, #-1568]
	cmp	r6, r3
	bcs	.L2643
	sub	r3, r4, #1728
	mov	r6, #0
	str	r6, [r4, #-1584]
	ldrh	r1, [r3]
	ldr	r3, [r4, #-1416]
	sub	r3, r3, #2
.L2644:
	cmp	r6, r1
	bcs	.L2671
	ldrh	r2, [r3, #2]!
	add	r6, r6, #1
	ldr	r0, [r4, #-1584]
	add	r2, r2, r0
	str	r2, [r4, #-1584]
	b	.L2644
.L2671:
	ldr	r6, [r4, #-1584]
	mov	r0, r6
	bl	__aeabi_uidiv
	ldr	r3, .L2672+8
	ldrh	r1, [r3, #-12]
	str	r0, [r5, #1736]
	ldr	r0, [r4, #-1580]
	rsb	r0, r0, r6
	bl	__aeabi_uidiv
	str	r0, [r4, #-1584]
	b	.L2646
.L2643:
	ldr	r3, [r4, #-1572]
	cmp	r6, r3
	bls	.L2646
	add	r3, r3, #1
	mov	ip, r4
	str	r3, [r4, #-1572]
	mov	r3, #0
.L2648:
	ldr	r2, .L2672+12
	ldrh	r2, [r2]
	cmp	r3, r2
	bcs	.L2646
	ldr	r0, [ip, #-1416]
	mov	r1, r3, asl #1
	add	r3, r3, #1
	ldrh	r2, [r0, r1]
	add	r2, r2, #1
	strh	r2, [r0, r1]	@ movhi
	b	.L2648
.L2646:
	ldr	r6, [r4, #-1568]
	ldr	r8, [r5, #1736]
	add	r3, r6, #256
	ldr	r2, .L2672+4
	cmp	r3, r8
	bls	.L2651
	ldr	r1, [r2, #-1572]
	add	r3, r6, #768
	cmp	r3, r1
	bls	.L2651
	cmp	r6, #40
	ldr	r2, [r2, #-1872]
	movls	r3, #0
	movhi	r3, #1
	cmp	r2, #0
	orreq	r3, r3, #1
	cmp	r3, #0
	beq	.L2651
.L2653:
	movw	r0, #65535
	b	.L2652
.L2651:
	ldr	r10, .L2672+4
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
	bne	.L2653
	ldr	r3, [r10, #864]
	cmp	r3, #0
	beq	.L2653
	sub	r2, r10, #1728
	ldr	r0, [r10, #-1356]
	ldr	ip, .L2672+16
	movw	r7, #65535
	ldrh	r2, [r2]
	mov	r5, r7
	ldr	r10, [r10, #-1416]
	mov	lr, #6
	str	r2, [sp, #20]
.L2654:
	ldrh	r2, [r3]
	movw	fp, #65535
	cmp	r2, fp
	beq	.L2656
	add	r1, r1, #1
	ldr	fp, [sp, #20]
	uxth	r1, r1
	cmp	r1, fp
	bhi	.L2653
	ldrh	fp, [r3, #4]
	cmp	fp, #0
	beq	.L2655
	rsb	r3, r0, r3
	mov	r3, r3, asr #1
	mul	r3, ip, r3
	uxth	r3, r3
	mov	fp, r3, asl #1
	ldrh	fp, [r10, fp]
	cmp	fp, r6
	bls	.L2660
	cmp	fp, r7
	movcc	r7, fp
	movcc	r5, r3
.L2655:
	mla	r3, lr, r2, r0
	b	.L2654
.L2660:
	mov	r5, r3
.L2656:
	movw	r3, #65535
	cmp	r5, r3
	beq	.L2653
	mov	r3, r5, asl #1
	ldrh	fp, [r10, r3]
	cmp	fp, r6
	bls	.L2658
	str	r3, [sp, #20]
	bl	GetFreeBlockMinEraseCount
	ldr	r3, [sp, #20]
	cmp	r0, r6
	strhi	r7, [r4, #-1568]
.L2658:
	cmp	fp, r8
	bcs	.L2653
	add	r2, fp, #128
	cmp	r9, r2
	ble	.L2653
	add	r2, fp, #256
	cmp	r2, r8
	bcc	.L2659
	ldr	r2, [r4, #-1572]
	add	fp, fp, #768
	cmp	fp, r2
	bcs	.L2653
.L2659:
	ldr	r2, [r4, #-1408]
	mov	r1, r5
	ldr	r0, .L2672+20
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
.L2652:
	add	sp, sp, #28
	@ sp needed
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L2673:
	.align	2
.L2672:
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
	beq	.L2675
	ldr	r2, .L2677
	mov	r3, r0, asl #1
	mov	r1, #0
	ldr	r2, [r2, #-1408]
	strh	r1, [r2, r3]	@ movhi
	bl	INSERT_FREE_LIST
.L2675:
	mov	r0, #0
	ldmfd	sp!, {r3, pc}
.L2678:
	.align	2
.L2677:
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
	ldr	r4, .L2734
	ldr	r3, [r4, #-1280]
	cmp	r3, #0
	bne	.L2680
	sub	r10, r4, #1728
	mov	r5, r0
	mov	r6, r4
.L2681:
	ldr	r7, .L2734+4
	ldrb	r2, [r5, #8]	@ zero_extendqisi2
	cmp	r5, r7
	bne	.L2682
	ldrh	r3, [r7, #-100]
	ldr	ip, [r6, #-1560]
	mov	r0, r3, lsr #1
	mul	lr, ip, r3
	add	r1, r0, #1
	add	r1, r1, lr, lsr #2
	ldr	lr, [r6, #-1872]
	cmp	lr, #0
	uxth	r1, r1
	beq	.L2683
	ldr	lr, [r6, #-1568]
	cmp	lr, #39
	bhi	.L2683
	cmp	lr, #2
	bls	.L2708
	cmp	ip, #0
	movne	r3, #0
	andeq	r3, r3, #1
	cmp	r3, #0
	moveq	r1, r0
	beq	.L2683
	b	.L2708
.L2682:
	cmp	r2, #1
	bne	.L2708
	ldr	r3, .L2734+8
	ldrh	r3, [r3]
	cmp	r3, #1
	beq	.L2708
	ldrb	r3, [r6, #-2744]	@ zero_extendqisi2
	cmp	r3, #0
	bne	.L2708
	ldr	r0, [r6, #-1872]
	ldrh	r3, [r7, #-100]
	cmp	r0, #0
	mov	r1, r3, lsr #3
	beq	.L2683
	ldr	r0, [r6, #-1568]
	cmp	r0, #1
	rsbls	r3, r3, r3, asl #3
	ubfxls	r1, r3, #3, #16
.L2683:
	cmp	r1, #0
	subne	r1, r1, #1
	uxthne	r1, r1
	b	.L2684
.L2708:
	mov	r1, #0
.L2684:
	ldr	r0, .L2734+12
	bl	List_pop_index_node
	ldrh	r3, [r7, #-100]
	sub	r3, r3, #1
	strh	r3, [r7, #-100]	@ movhi
	ldrh	r3, [r10]
	uxth	r9, r0
	cmp	r3, r9
	bls	.L2681
	ldr	r3, [r6, #-1408]
	mov	r8, r9, asl #1
	ldrh	r7, [r3, r8]
	cmp	r7, #0
	bne	.L2681
	strh	r9, [r5]	@ movhi
	mov	r0, r5
	bl	make_superblock
	ldrb	r3, [r5, #7]	@ zero_extendqisi2
	cmp	r3, #0
	beq	.L2686
	ldr	r2, .L2734+16
	add	ip, r5, #14
	ldr	r0, [r6, #-1492]
	ldrh	lr, [r2]
	mov	r2, #36
	mov	r3, r0
	mla	r1, r2, lr, r0
	mov	lr, r7
	str	r1, [sp]
	b	.L2687
.L2686:
	ldr	r3, [r6, #-1408]
	b	.L2729
.L2687:
	ldr	r1, [sp]
	cmp	r3, r1
	beq	.L2731
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
	b	.L2687
.L2731:
	ldr	r3, .L2734+20
	ldr	r2, [r4, #-1872]
	rsb	r3, r3, r5
	clz	r3, r3
	cmp	r2, #0
	mov	r3, r3, lsr #5
	moveq	r3, #0
	cmp	r3, #0
	beq	.L2690
	ldr	r3, [r6, #-1416]
	ldrh	r3, [r3, r8]
	cmp	r3, #40
	movhi	r3, #0
	strhib	r3, [r6, #892]
.L2690:
	ldrb	r3, [r5, #8]	@ zero_extendqisi2
	ldr	r2, [r4, #-1416]
	cmp	r3, #0
	ldrh	r3, [r2, r8]
	bne	.L2691
	cmp	r3, #0
	mov	r0, r9
	ldrne	r1, .L2734+24
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
	b	.L2693
.L2691:
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
.L2693:
	ldr	r3, [r4, #-1416]
	ldr	r2, [r4, #-1572]
	ldr	r0, [r4, #-1584]
	ldrh	r3, [r3, r8]
	cmp	r3, r2
	strhi	r3, [r6, #-1572]
	ldr	r3, .L2734+24
	ldrh	r2, [r3]
	ldr	r3, [r4, #-1580]
	mla	r0, r0, r2, r3
	ldr	r3, .L2734+28
	ldrh	r1, [r3]
	bl	__aeabi_uidiv
	ldr	r2, [r4, #-1420]
	ldr	r3, .L2734+32
	str	r0, [r3, #1736]
	ldr	r3, [r2, #16]
	add	r3, r3, #1
	str	r3, [r2, #16]
	ldr	r2, [r4, #-1492]
	mov	r3, #36
	add	r1, r2, #4
	mla	r3, r3, r7, r2
	add	r3, r3, #40
.L2695:
	add	r1, r1, #36
	cmp	r1, r3
	ldrne	r2, [r1, #-36]
	bicne	r2, r2, #1020
	bicne	r2, r2, #3
	strne	r2, [r1, #-36]
	bne	.L2695
.L2732:
	ldrb	r3, [r4, #-2744]	@ zero_extendqisi2
	cmp	r3, #0
	beq	.L2697
	ldrb	r3, [r5, #8]	@ zero_extendqisi2
	mov	r2, r7
	ldr	r0, [r6, #-1492]
	cmp	r3, #1
	moveq	r1, #0
	movne	r1, #1
	bl	FlashEraseBlocks
.L2697:
	ldrb	r1, [r5, #8]	@ zero_extendqisi2
	mov	r2, r7
	ldr	r0, [r4, #-1492]
	mov	fp, #0
	bl	FlashEraseBlocks
	add	r1, r5, #16
	mov	r2, fp
	mov	ip, #36
.L2699:
	uxth	r3, fp
	cmp	r3, r7
	bcs	.L2733
	mul	r3, ip, fp
	ldr	lr, [r4, #-1492]
	add	r0, lr, r3
	ldr	r3, [lr, r3]
	cmn	r3, #1
	bne	.L2700
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
.L2700:
	add	fp, fp, #1
	add	r1, r1, #2
	b	.L2699
.L2733:
	cmp	r2, #0
	beq	.L2702
	mov	r0, r9
	bl	update_multiplier_value
	bl	FtlBbmTblFlush
.L2702:
	ldrb	r3, [r5, #7]	@ zero_extendqisi2
	cmp	r3, #0
	bne	.L2703
	ldr	r3, [r4, #-1408]
.L2729:
	mvn	r2, #0
	strh	r2, [r3, r8]	@ movhi
	b	.L2681
.L2703:
	ldr	r2, .L2734+36
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
.L2680:
	mov	r0, #0
	add	sp, sp, #20
	@ sp needed
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L2735:
	.align	2
.L2734:
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
	ldr	lr, .L2744
	mov	r5, #1
	ldr	r2, .L2744+4
	mov	r7, #36
	ldr	r1, .L2744+8
	add	r6, lr, #78
	mov	r3, #0
	str	r3, [r2, #1740]
.L2737:
	ldrh	r2, [lr]
	add	ip, r3, #1
	uxth	r3, r3
	ldr	r0, .L2744+8
	cmp	r3, r2
	bcs	.L2742
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
	ldr	r2, .L2744+12
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
	b	.L2737
.L2742:
	ldr	r4, .L2744+16
	mov	r1, r0
	mov	ip, #12
	mov	lr, #0
.L2739:
	ldr	r3, [r0, #-1480]
	cmp	r2, r3
	bcs	.L2743
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
	ldr	r3, .L2744+12
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
	b	.L2739
.L2743:
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, pc}
.L2745:
	.align	2
.L2744:
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
	ldr	r6, .L2748
	mvn	r3, #0
	ldr	r5, .L2748+4
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
.L2749:
	.align	2
.L2748:
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
	ldr	r3, .L2758
	stmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, lr}
	.save {r4, r5, r6, r7, r8, r9, r10, lr}
	mov	lr, #0
	ldr	r6, [r3, #-1480]
	mov	r5, #36
	ldr	r4, [r3, #-1448]
	mov	r7, lr
	mov	r8, #12
.L2751:
	uxth	ip, lr
	cmp	ip, r1
	ldmcsfd	sp!, {r4, r5, r6, r7, r8, r9, r10, pc}
	mla	ip, r5, ip, r0
	mov	r2, #0
.L2752:
	uxth	r3, r2
	cmp	r3, r6
	bcs	.L2753
	mul	r3, r8, r3
	add	r2, r2, #1
	add	r9, r4, r3
	ldr	r10, [r4, r3]
	ldr	r3, [ip, #8]
	cmp	r10, r3
	bne	.L2752
	str	r7, [r9, #8]
.L2753:
	add	lr, lr, #1
	b	.L2751
.L2759:
	.align	2
.L2758:
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
	ldr	r3, .L2769
	mov	ip, #0
	stmfd	sp!, {r4, r5, r6, r7, r8, r9, lr}
	.save {r4, r5, r6, r7, r8, r9, lr}
	mov	r6, #12
	ldr	r4, [r3, #-1480]
	mov	r7, #1
	ldr	r5, [r3, #-1448]
	mov	r8, #36
.L2761:
	uxth	r2, ip
	cmp	r2, r1
	bcs	.L2768
	mov	lr, #0
.L2762:
	uxth	r3, lr
	cmp	r3, r4
	bcs	.L2763
	mla	r3, r6, r3, r5
	add	lr, lr, #1
	ldr	r9, [r3, #8]
	cmp	r9, #0
	bne	.L2762
	mla	r2, r8, r2, r0
	ldr	lr, [r3]
	str	r7, [r3, #8]
	str	lr, [r2, #8]
	ldr	r3, [r3, #4]
	str	r3, [r2, #12]
.L2763:
	add	ip, ip, #1
	b	.L2761
.L2768:
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, pc}
.L2770:
	.align	2
.L2769:
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
	ldr	r1, .L2777
	movw	r2, #1764
	ldr	r3, .L2777+4
	ldrh	r2, [r1, r2]
	ldr	r3, [r3, #-1512]
	add	r2, r3, r2, asl #1
.L2772:
	cmp	r3, r2
	beq	.L2776
	ldrh	r1, [r3], #2
	cmp	r1, r0
	bne	.L2772
	mov	r0, #1
	bx	lr
.L2776:
	mov	r0, #0
	bx	lr
.L2778:
	.align	2
.L2777:
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
	ldr	lr, .L2787
	movw	r2, #1764
	ldr	r3, .L2787+4
	ldr	r1, [lr, #-1512]
	ldrh	r7, [r3, r2]
	mov	r2, #0
	sub	r8, r1, #2
.L2780:
	uxth	ip, r2
	cmp	ip, r7
	bcs	.L2784
	ldrh	r9, [r8, #2]!
	add	r2, r2, #1
	cmp	r9, r0
	bne	.L2780
.L2784:
	cmp	ip, r7
	bne	.L2782
	mov	ip, ip, asl #1
	movw	r2, #1764
	strh	r0, [r1, ip]	@ movhi
	ldrh	r0, [r3, r2]
	add	r0, r0, #1
	strh	r0, [r3, r2]	@ movhi
.L2782:
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
.L2788:
	.align	2
.L2787:
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
	ldr	r3, .L2791
	mov	r2, #0
	ldr	r4, .L2791+4
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
.L2792:
	.align	2
.L2791:
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
	ldr	r2, .L2846
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
	beq	.L2827
	cmp	r5, #0
	bne	.L2794
	b	.L2795
.L2827:
	mov	r5, #0
.L2794:
	ldr	r3, .L2846+4
	ldr	r2, [sp, #8]
	ldrh	r3, [r3, #-4]
	cmp	r2, r3
	bne	.L2796
.L2795:
	bl	FtlGcPageVarInit
.L2796:
	ldr	r6, .L2846+8
	mov	r2, #0
	mvn	r3, #0
	stmia	sp, {r2, r3}
.L2797:
	ldrh	r1, [r4]
	movw	r3, #65535
	mov	r2, #0
	strb	r2, [r4, #8]
	cmp	r1, r3
	beq	.L2798
.L2799:
.L2824:
	ldr	r3, .L2846+12
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
	ldr	r3, .L2846+16
	ldrh	fp, [r3]
	ldrh	ip, [r3, #2]
.L2800:
	ldr	r2, [sp, #12]
	uxth	r3, r7
	cmp	r3, r2
	bcs	.L2844
	ldrh	r3, [r1, #2]!
	cmp	r3, lr
	beq	.L2801
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
.L2801:
	add	r7, r7, #1
	b	.L2800
.L2844:
	ldr	r0, [r6, #-1504]
	mov	r1, r8
	mov	r2, #0
	mov	r10, #0
	bl	FlashReadPages
.L2803:
	uxth	r3, r10
	cmp	r3, r8
	bcs	.L2845
	ldr	r3, .L2846+8
	mov	r9, #36
	mul	r9, r9, r10
	ldr	r7, [r3, #-1504]
	add	r3, r7, r9
	str	r3, [sp, #12]
	ldr	fp, [r3, #4]
	ubfx	r0, fp, #10, #16
	bl	P2V_plane
	ldr	r7, [r7, r9]
	ldr	ip, .L2846+8
	cmp	r7, #0
	ldr	r3, [sp, #12]
	mov	r2, r0
	ldr	r3, [r3, #12]
	bne	.L2804
	ldrh	r0, [r3]
	movw	r1, #65535
	cmp	r0, r1
	bne	.L2805
.L2808:
	ldrb	r1, [ip, #-2744]	@ zero_extendqisi2
	cmp	r1, #0
	beq	.L2839
	ldr	r3, .L2846+20
	mov	r1, #1
	str	r1, [r3, #1756]
	b	.L2798
.L2805:
	ldr	r0, [r3, #8]
	ldr	r1, [r6, #-1284]
	cmp	r0, r1
	bls	.L2840
	b	.L2808
.L2839:
	ldrh	r3, [r4]
	ldr	r2, [r6, #-1408]
	mov	r3, r3, asl #1
	b	.L2843
.L2840:
	ldr	r2, .L2846+24
	ldrb	r2, [r2, #32]	@ zero_extendqisi2
	cmp	r2, #0
	beq	.L2812
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
	beq	.L2812
	str	r2, [sp, #32]
	mov	r1, #1
	ldr	r2, [r6, #-1452]
	add	r0, sp, #28
	str	r2, [sp, #36]
	ldr	r2, [r6, #-1440]
	str	r2, [sp, #40]
	mov	r2, r7
	bl	FlashReadPages
	ldr	r2, .L2846+28
	ldr	r1, [r6, #-1504]
	ldrh	r2, [r2]
	add	r9, r1, r9
	mov	r2, r2, asl #7
	ldr	r1, [sp, #36]
	ldr	r3, [sp, #12]
	b	.L2814
.L2815:
	add	r7, r7, #1
.L2814:
	cmp	r7, r2
	beq	.L2812
	ldr	r0, [r9, #8]
	ldr	ip, [r0, r7, asl #2]
	ldr	r0, [r1, r7, asl #2]
	cmp	ip, r0
	beq	.L2815
	ldrh	r1, [r4]
	ldr	r2, [sp, #32]
	ldr	r0, .L2846+32
	bl	printk
	ldrh	r3, [r4]
	ldr	r2, [r6, #-1408]
	mov	r1, #0
	mov	r3, r3, asl #1
.L2843:
	strh	r1, [r2, r3]	@ movhi
	ldrh	r0, [r4]
	bl	INSERT_FREE_LIST
	ldr	r2, .L2846+36
	mvn	r3, #0
	strh	r3, [r4]	@ movhi
	strh	r3, [r2]	@ movhi
	b	.L2842
.L2812:
	ldr	r0, [r3, #12]
	mov	r1, fp
	ldr	r2, [r3, #8]
	add	r10, r10, #1
	bl	FtlGcUpdatePage
	b	.L2803
.L2804:
	ldr	r0, .L2846+40
	mov	r2, fp
	ldrh	r1, [r4]
	str	ip, [sp, #12]
	bl	printk
	ldr	r3, .L2846+8
	ldrh	r5, [r4]
	ldr	r3, [r3, #-1872]
	cmp	r3, #0
	ldr	ip, [sp, #12]
	bne	.L2818
	ldr	r3, .L2846+8
	ldrb	r3, [r3, #-2744]	@ zero_extendqisi2
	cmp	r3, #0
	beq	.L2819
.L2818:
	ldr	r2, [ip, #-1416]
	mov	r3, r5, asl #1
	ldrh	r3, [r2, r3]
	cmp	r3, #159
	bls	.L2820
.L2819:
	ldr	r3, [ip, #-1504]
	ldr	r3, [r3, r9]
	cmn	r3, #1
	bne	.L2821
.L2820:
	ldr	r3, [ip, #-1504]
	add	r9, r3, r9
	ldr	r3, [r9, #4]
	str	r3, [sp, #4]
.L2821:
	ldr	r3, .L2846+8
	mov	r5, r5, asl #1
	mov	r2, #0
	ldr	r3, [r3, #-1408]
	strh	r2, [r3, r5]	@ movhi
	ldrh	r0, [r4]
	bl	INSERT_FREE_LIST
	mvn	r3, #0
	strh	r3, [r4]	@ movhi
.L2842:
	bl	FtlGcPageVarInit
	mov	r5, #0
	b	.L2797
.L2845:
	ldr	r3, [sp]
	add	r5, r5, #1
	ldr	r2, [sp, #8]
	add	r3, r3, #1
	uxth	r5, r5
	cmp	r3, r2
	str	r3, [sp]
	ldr	r2, .L2846+44
	bcs	.L2823
.L2825:
	ldrh	r3, [r2]
	cmp	r3, r5
	bhi	.L2824
	mov	r2, #0
	b	.L2798
.L2823:
	ldr	r1, .L2846+48
	movw	r0, #65535
	ldrh	r3, [r1]
	cmp	r3, r0
	beq	.L2825
	ldr	r0, [sp]
	add	r3, r3, r0
	strh	r3, [r1]	@ movhi
	ldrh	r3, [r2]
	cmp	r3, r5
	bls	.L2825
	b	.L2826
.L2798:
	ldr	r1, .L2846
	movw	r3, #3448
	mvn	r0, #0
	strh	r5, [r4, #2]	@ movhi
	strb	r2, [r4, #6]
	strh	r0, [r1, r3]	@ movhi
	mov	r0, r4
	mov	r1, r5
	bl	ftl_sb_update_avl_pages
.L2826:
	ldr	r0, [sp, #4]
	add	sp, sp, #68
	@ sp needed
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L2847:
	.align	2
.L2846:
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
	ldr	r4, .L2856
	ldrh	r3, [r4, #-4]
	cmp	r3, r0
	beq	.L2850
	ldrh	r3, [r4, #-2]
	cmp	r3, r0
	beq	.L2850
	ldrh	r3, [r4]
	cmp	r3, r0
	beq	.L2850
	add	r6, r4, #16
	ldrh	r3, [r6, #-14]
	cmp	r3, r0
	beq	.L2850
	ldr	r0, .L2856+4
	mov	r1, r5
	bl	printk
	ldrh	r2, [r4, #-4]
	movw	r3, #65535
	cmp	r2, r3
	streqh	r5, [r4, #-4]	@ movhi
	beq	.L2850
	ldrh	r2, [r4, #-2]
	cmp	r2, r3
	streqh	r5, [r4, #-2]	@ movhi
	beq	.L2850
	ldrh	r2, [r4]
	cmp	r2, r3
	streqh	r5, [r4]	@ movhi
	beq	.L2850
	ldrh	r2, [r6, #-14]
	cmp	r2, r3
	streqh	r5, [r6, #-14]	@ movhi
.L2850:
	mov	r0, #0
	ldmfd	sp!, {r4, r5, r6, pc}
.L2857:
	.align	2
.L2856:
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
	ldr	r4, .L2869
	ldrh	r3, [r4, #-4]
	cmp	r3, r0
	beq	.L2866
	ldrh	r3, [r4, #-2]
	cmp	r3, r0
	beq	.L2866
	ldrh	r3, [r4]
	cmp	r3, r0
	beq	.L2866
	add	r6, r4, #16
	ldrh	r3, [r6, #-14]
	cmp	r3, r0
	beq	.L2866
	ldr	r0, .L2869+4
	mov	r1, r5
	bl	printk
	ldrh	r2, [r4, #-4]
	movw	r3, #65535
	cmp	r2, r3
	streqh	r5, [r4, #-4]	@ movhi
	beq	.L2866
	ldrh	r2, [r4, #-2]
	cmp	r2, r3
	streqh	r5, [r4, #-2]	@ movhi
	beq	.L2866
	ldrh	r2, [r4]
	cmp	r2, r3
	streqh	r5, [r4]	@ movhi
	beq	.L2866
	ldrh	r2, [r6, #-14]
	cmp	r2, r3
	bne	.L2867
	strh	r5, [r6, #-14]	@ movhi
.L2866:
	mov	r0, #0
	ldmfd	sp!, {r4, r5, r6, pc}
.L2867:
	mvn	r0, #0
	ldmfd	sp!, {r4, r5, r6, pc}
.L2870:
	.align	2
.L2869:
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
	ldr	r6, .L2881
	mov	r2, r5
	sub	r7, r6, #1520
	ldrh	r1, [r7, #-6]
	mov	r4, r0
	ldr	r0, .L2881+4
	bl	printk
	mov	r0, r4
	bl	FtlGcRefreshBlock
	ldr	r3, [r6, #-1872]
	cmp	r3, #0
	beq	.L2872
	ldr	r2, [r6, #-1416]
	mov	r4, r4, asl #1
	ldrh	r3, [r2, r4]
	cmp	r3, #39
	subhi	r3, r3, #40
	strhih	r3, [r2, r4]	@ movhi
.L2872:
	ldrh	r3, [r7, #-6]
	mov	r2, #0
	ldr	r0, .L2881+8
.L2873:
	uxth	r1, r2
	cmp	r1, r3
	bcs	.L2880
	add	r2, r2, #1
	add	r1, r0, r2, asl #1
	ldrh	r1, [r1, #-2]
	cmp	r1, r5
	bne	.L2873
	b	.L2874
.L2880:
	cmp	r3, #15
	addls	r2, r3, #1
	strlsh	r2, [r7, #-6]	@ movhi
	ldrls	r2, .L2881+8
	movls	r3, r3, asl #1
	strlsh	r5, [r2, r3]	@ movhi
.L2874:
	mov	r0, #0
	ldmfd	sp!, {r3, r4, r5, r6, r7, pc}
.L2882:
	.align	2
.L2881:
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
	ldr	r4, .L2890
	ldrh	r3, [r4, #-6]
	cmp	r3, #0
	beq	.L2884
	ldrh	r1, [r4, #-20]
	movw	r2, #65535
	cmp	r1, r2
	bne	.L2884
	ldrh	r2, [r4, #-2]
	cmp	r2, r3
	ldr	r2, .L2890+4
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
.L2884:
	mov	r0, #0
	ldmfd	sp!, {r4, pc}
.L2891:
	.align	2
.L2890:
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
	ldr	r5, .L2906
	ldrh	r3, [r5, #-6]
	cmp	r3, #0
	beq	.L2894
	sub	r5, r5, #6
	mov	r8, r0
	mov	r7, #0
	mov	r9, r5
.L2893:
	ldr	r3, .L2906+4
	uxth	r2, r7
	ldrh	r3, [r3]
	cmp	r3, r2
	bls	.L2903
	ldr	r3, .L2906+8
	mov	r1, r8
	mov	r10, #0
	add	r3, r3, r2
	ldrb	r0, [r3, #-1708]	@ zero_extendqisi2
	bl	V2P_block
	ldr	ip, .L2906+12
	mov	fp, r0
.L2895:
	ldrh	r3, [r5]
	uxth	r4, r10
	cmp	r3, r4
	bls	.L2904
	mov	r3, r4, asl #1
	add	r6, ip, r3
	ldrh	r3, [ip, r3]
	cmp	r3, fp
	bne	.L2896
	mov	r1, fp
	ldr	r0, .L2906+16
	str	ip, [sp, #4]
	bl	printk
	mov	r0, fp
	bl	FtlBbmMapBadBlock
	bl	FtlBbmTblFlush
	ldrh	r2, [r5]
	mov	r3, r6
	ldr	ip, [sp, #4]
.L2897:
	cmp	r4, r2
	ldrcch	r1, [r3, #2]
	addcc	r4, r4, #1
	uxthcc	r4, r4
	strcch	r1, [r3], #2	@ movhi
	bcc	.L2897
.L2905:
	sub	r2, r2, #1
	strh	r2, [r9]	@ movhi
.L2896:
	add	r10, r10, #1
	b	.L2895
.L2904:
	add	r7, r7, #1
	b	.L2893
.L2903:
	bl	FtlGcReFreshBadBlk
.L2894:
	mov	r0, #0
	add	sp, sp, #12
	@ sp needed
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L2907:
	.align	2
.L2906:
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
	ldr	r2, .L2918
	mov	r3, r0, asl #1
	stmfd	sp!, {r4, lr}
	.save {r4, lr}
	mov	r4, r0
	ldr	r1, [r2, #-1408]
	ldrh	r3, [r1, r3]
	cmp	r3, #0
	bne	.L2909
	ldr	r0, .L2918+4
	movw	r1, #1164
	ldrh	ip, [r0, r1]
	cmp	ip, r4
	mvneq	r3, #0
	streqh	r3, [r0, r1]	@ movhi
	beq	.L2911
	add	r1, r2, #884
	ldrh	r1, [r1]
	cmp	r1, r4
	beq	.L2917
	add	r1, r2, #932
	ldrh	r1, [r1]
	cmp	r1, r4
	beq	.L2917
	add	r2, r2, #980
	ldrh	r2, [r2]
	cmp	r2, r4
	beq	.L2917
.L2911:
	mov	r1, r4
	ldr	r0, .L2918+8
	bl	List_remove_node
	ldr	r2, .L2918+12
	mov	r0, r4
	ldrh	r3, [r2]
	sub	r3, r3, #1
	strh	r3, [r2]	@ movhi
	bl	free_data_superblock
	mov	r0, r4
	bl	FtlGcFreeBadSuperBlk
	mov	r0, #1
	ldmfd	sp!, {r4, pc}
.L2909:
	bl	List_update_data_list
.L2917:
	mov	r0, #0
	ldmfd	sp!, {r4, pc}
.L2919:
	.align	2
.L2918:
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
	beq	.L2921
	ldr	r5, .L2931
	mov	r6, r0, asl #1
	ldr	r3, [r5, #-1408]
	ldrh	r2, [r3, r6]
	cmp	r2, #0
	subne	r2, r2, #1
	strneh	r2, [r3, r6]	@ movhi
	bne	.L2921
	mov	r1, r4
	ldr	r0, .L2931+4
	bl	printk
	ldr	r3, [r5, #-1408]
	mov	r2, #32
	add	r0, r5, #876
	mov	r1, r4
	strh	r2, [r3, r6]	@ movhi
	bl	test_node_in_list
	cmp	r0, #0
	beq	.L2923
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
	ldr	r0, .L2931+8
	mov	r1, r4
	ldrh	r2, [r3, r6]
	bl	printk
.L2923:
	mov	r0, r4
	bl	FtlGcRefreshBlock
	b	.L2926
.L2921:
	ldr	r6, .L2931+12
	movw	r5, #1748
	movw	r3, #65535
	ldrh	r0, [r6, r5]
	cmp	r0, r3
	streqh	r4, [r6, r5]	@ movhi
	beq	.L2926
	cmp	r0, r4
	beq	.L2926
	bl	update_vpc_list
	strh	r4, [r6, r5]	@ movhi
	adds	r0, r0, #0
	movne	r0, #1
	ldmfd	sp!, {r4, r5, r6, pc}
.L2926:
	mov	r0, #0
	ldmfd	sp!, {r4, r5, r6, pc}
.L2932:
	.align	2
.L2931:
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
	beq	.L3077
	stmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.save {r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.pad #68
	sub	sp, sp, #68
	ldrh	r3, [r0, #2]
	str	r3, [sp, #8]
	ldrb	r3, [r0, #6]	@ zero_extendqisi2
	ldr	r0, [sp, #8]
	str	r3, [sp]
	ldr	r3, .L3092
	ldrh	r3, [r3, #-4]
	cmp	r3, r0
	mov	r3, #0
	streqh	r3, [r1, #4]	@ movhi
	streqb	r3, [r1, #6]
	ldrneh	r0, [r1, #16]
	beq	.L3074
.L2937:
	cmp	r0, r2
	add	r3, r3, #1
	uxtheq	r0, r3
	addeq	r0, r1, r0, asl #1
	ldreqh	r0, [r0, #16]
	beq	.L2937
.L3087:
	mov	r9, r1
	ldrb	r1, [r1, #8]	@ zero_extendqisi2
	cmp	r1, #1
	bne	.L2939
	bl	FtlGetLastWrittenPage
	cmn	r0, #1
	mov	r4, r0
	beq	.L2940
	ldr	r3, .L3092+4
	ldrb	r3, [r3, #-2744]	@ zero_extendqisi2
	cmp	r3, #0
	bne	.L3078
	ldr	r3, .L3092+8
	add	r3, r3, r0, asl #1
	ldrh	r6, [r3, #80]
	b	.L3011
.L2939:
	mov	r1, #0
	bl	FtlGetLastWrittenPage
	cmn	r0, #1
	mov	r4, r0
	beq	.L2940
.L3078:
	mov	r6, r4
.L3011:
	ldr	r3, .L3092+4
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
	b	.L2942
.L2940:
	mov	r3, #0
	strh	r3, [r9, #2]	@ movhi
	strb	r3, [r9, #6]
	b	.L3074
.L2944:
	ldrh	r3, [r0, #2]!
	cmp	r3, r8
	beq	.L2943
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
.L2943:
	add	r2, r2, #1
.L2942:
	ldr	r1, [sp, #4]
	uxth	r3, r2
	cmp	r3, r1
	bcc	.L2944
	ldrb	r3, [r9, #8]	@ zero_extendqisi2
	ldr	fp, .L3092+4
	cmp	r3, #1
	movne	r3, #0
	bne	.L3079
	ldrb	lr, [fp, #-2744]	@ zero_extendqisi2
	adds	r3, lr, #0
	movne	r3, #1
.L3079:
	str	r3, [sp, #24]
	mov	r1, r5
	ldr	r2, [sp, #24]
	mov	r10, #0
	ldr	r0, [fp, #-1504]
	bl	FlashReadPages
	ldr	r3, [fp, #-1612]
	ldr	r2, .L3092+4
	str	r3, [sp, #28]
	movw	r3, #65535
	str	r3, [sp, #16]
.L2946:
	uxth	r7, r10
	cmp	r7, r5
	bcs	.L2953
	mov	r1, #36
	ldr	r0, [r2, #-1504]
	mul	r1, r1, r10
	add	ip, r0, r1
	ldr	r1, [r0, r1]
	cmp	r1, #0
	bne	.L2947
	ldr	ip, [ip, #12]
	ldr	r8, [ip, #4]
	cmn	r8, #1
	beq	.L2948
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
.L2948:
	ldr	r1, [ip]
	cmn	r1, #1
	bne	.L2949
.L2953:
	cmp	r7, r5
	ldr	r5, .L3092+4
	bne	.L3075
	add	fp, r4, #1
	uxth	r3, fp
	str	r3, [sp, #4]
	ldr	r3, [r5, #-1504]
	ldr	r0, [r3, #4]
	b	.L3080
.L2947:
	ldr	r1, [ip, #4]
	ldr	r0, .L3092+12
	str	r2, [sp, #4]
	bl	printk
	uxth	r3, r6
	ldrh	r1, [r9]
	str	r3, [sp, #16]
	ldr	r2, [sp, #4]
	ldr	r3, .L3092+16
	strh	r1, [r3]	@ movhi
.L2949:
	add	r10, r10, #1
	b	.L2946
.L3075:
	uxth	r3, r4
	str	r3, [sp, #4]
	ldr	r3, [fp, #-1504]
	mov	r2, #36
	mla	r7, r2, r7, r3
	ldr	r0, [r7, #4]
.L3080:
	ubfx	r0, r0, #10, #16
	bl	P2V_plane
	ldrb	r3, [r9, #8]	@ zero_extendqisi2
	cmp	r3, #1
	str	r0, [sp, #12]
	bne	.L2955
	ldrb	r2, [r5, #-2744]	@ zero_extendqisi2
	cmp	r2, #0
	ldreq	r1, [sp, #4]
	ldreq	r2, .L3092+8
	addeq	r2, r2, r1, asl #1
	ldreqh	r2, [r2, #80]
	streq	r2, [sp, #4]
.L2955:
	ldr	r2, .L3092
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
	beq	.L3085
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
	beq	.L2959
	ldr	r3, .L3092+20
	uxth	fp, r6
	ldr	r8, .L3092+4
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
.L2962:
	cmp	r4, fp
	bhi	.L2975
	ldr	r3, .L3092+24
	mov	r0, #36
	ldr	lr, [r8, #-1504]
	ldr	r1, [sp, #20]
	ldrh	r3, [r3]
	str	r3, [sp, #16]
	mov	r3, #0
	mov	r5, r3
.L2976:
	ldr	ip, [sp, #16]
	uxth	r2, r3
	cmp	r2, ip
	bcs	.L3088
	ldrh	r2, [r1, #2]!
	movw	ip, #65535
	add	r3, r3, #1
	cmp	r2, ip
	orrne	r2, r4, r2, asl #10
	mlane	ip, r0, r5, lr
	addne	r5, r5, #1
	uxthne	r5, r5
	strne	r2, [ip, #4]
	b	.L2976
.L3088:
	mov	r1, r5
	ldr	r2, [sp, #24]
	ldr	r0, [r8, #-1504]
	bl	FlashReadPages
	ldr	r3, [r8, #-1504]
	mov	r2, #36
	ldrb	ip, [r8, #-2744]	@ zero_extendqisi2
	movw	r1, #65535
	mla	r5, r2, r5, r3
	ldr	r2, .L3092+28
	add	r2, r2, r4, asl #1
.L2965:
	cmp	r3, r5
	beq	.L3089
	ldr	r0, [r3]
	cmp	r0, #0
	bne	.L2966
	ldr	r0, [r3, #12]
	ldrh	lr, [r0]
	cmp	lr, r1
	beq	.L2967
	ldr	r0, [r0, #4]
	cmn	r0, #1
	beq	.L2967
	ldr	lr, .L3092+20
	cmn	r7, #1
	ldr	r6, [lr, #1760]
	str	r0, [lr, #1760]
	bne	.L2967
	ldrh	r0, [r2]
	cmp	r0, r1
	bne	.L2968
	cmp	ip, #0
	beq	.L2967
.L2968:
	ldr	r0, [sp]
	cmp	r6, r0
	mvneq	r7, #0
	movne	r7, r6
	b	.L2967
.L2966:
	ldrh	r1, [r9]
	movw	r2, #1802
	ldr	r3, .L3092+20
	strh	r1, [r3, r2]	@ movhi
	ldrb	r2, [r9, #8]	@ zero_extendqisi2
	cmp	r2, #0
	bne	.L2959
	ldr	r2, .L3092+28
	mov	r4, r4, asl #1
	ldrh	r1, [r2, r4]
	movw	r2, #65535
	cmp	r1, r2
	bne	.L2970
	cmn	r7, #1
	strne	r7, [r3, #1760]
	bne	.L2959
	ldr	r2, [sp]
	cmp	r10, r2
	strne	r10, [r3, #1760]
	ldreq	r2, [r3, #1760]
	bne	.L2959
	b	.L3086
.L2970:
	ldr	r2, [sp]
	cmp	r6, r2
	beq	.L2973
	cmn	r6, #1
	strne	r6, [r3, #1760]
	b	.L2959
.L2973:
	ldr	r2, [r3, #1760]
	ldr	r1, [sp]
	cmp	r2, r1
	beq	.L2959
.L3086:
	sub	r2, r2, #1
	b	.L3081
.L2967:
	add	r3, r3, #36
	b	.L2965
.L3089:
	add	r4, r4, #1
	uxth	r4, r4
	b	.L2962
.L2975:
	ldr	r3, .L3092+20
	mvn	r2, #0
.L3081:
	str	r2, [r3, #1760]
.L2959:
	ldr	fp, .L3092+20
	movw	r3, #1804
	ldr	r10, [sp, #8]
	mov	r2, #1
	strh	r2, [fp, r3]	@ movhi
.L2977:
	ldr	r3, .L3092+4
	movw	lr, #65535
	ldr	r2, .L3092+24
	mov	r1, #36
	ldr	r5, [sp, #20]
	mov	r4, #0
	ldr	r7, [r3, #-1504]
	ldrh	r6, [r2]
	ldrb	r8, [r3, #-2744]	@ zero_extendqisi2
	str	r4, [sp, #16]
.L2978:
	uxth	r3, r4
	cmp	r3, r6
	bcs	.L3090
	ldrh	r3, [r5, #2]!
	cmp	r3, lr
	beq	.L2979
	ldr	r2, [sp, #16]
	orr	r3, r10, r3, asl #10
	mla	r2, r1, r2, r7
	str	r3, [r2, #4]
	ldrb	r0, [r9, #8]	@ zero_extendqisi2
	cmp	r0, #1
	bne	.L2980
	cmp	r8, #0
	orrne	r3, r3, #-2147483648
	strne	r3, [r2, #4]
.L2980:
	ldr	r3, [sp, #16]
	add	ip, r3, #1
	uxth	r3, ip
	str	r3, [sp, #16]
.L2979:
	add	r4, r4, #1
	b	.L2978
.L3090:
	ldr	r4, .L3092+4
	ldr	r1, [sp, #16]
	ldr	r2, [sp, #24]
	ldr	r0, [r4, #-1504]
	bl	FlashReadPages
	mov	r3, #0
.L3084:
	str	r3, [sp, #28]
	ldr	r2, [sp, #16]
	ldrh	r3, [sp, #28]
	cmp	r3, r2
	bcs	.L3091
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
	bcc	.L2983
	ldr	r3, [sp, #32]
	ldr	r2, [sp, #8]
	cmp	r0, r3
	movcs	r3, #0
	movcc	r3, #1
	cmp	r10, r2
	movne	r3, #0
	cmp	r3, #0
	bne	.L2983
	ldr	r3, [sp, #12]
	ldr	r2, [sp, #4]
	cmp	r0, r3
	cmpeq	r10, r2
	beq	.L2984
	ldr	r3, [r8, r5]
	cmn	r3, #1
	beq	.L2985
	ldr	r7, [r7, #12]
	movw	r3, #61589
	ldrh	r2, [r7]
	cmp	r2, r3
	ldrneh	r0, [r9]
	bne	.L3082
	ldr	r3, [r7, #4]
	cmn	r3, #1
	str	r3, [sp]
	beq	.L2987
	mov	r0, r3
	ldr	r1, [r4, #-1612]
	bl	ftl_cmp_data_ver
	cmp	r0, #0
	ldrne	r3, [sp]
	addne	r3, r3, #1
	strne	r3, [r4, #-1612]
.L2987:
	ldr	r6, [r7, #8]
	add	r1, sp, #56
	ldr	r3, [r7, #12]
	mov	r2, #0
	mov	r0, r6
	str	r3, [sp, #52]
	bl	log2phys
	ldr	r1, [fp, #1760]
	cmn	r1, #1
	beq	.L2988
	ldr	r0, [sp]
	bl	ftl_cmp_data_ver
	cmp	r0, #0
	beq	.L2988
	ldr	r3, [sp, #52]
	cmn	r3, #1
	beq	.L2989
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
	bne	.L2990
	b	.L2991
.L2989:
	ldr	r3, [sp, #60]
	ldr	r2, [sp, #56]
	cmp	r2, r3
	bne	.L2983
	mov	r0, r6
	add	r1, sp, #52
	mov	r2, #1
	bl	log2phys
	b	.L2983
.L2990:
	ldr	r7, [r8, #8]
	cmp	r7, r6
	bne	.L2991
	ldr	r0, [fp, #1760]
	ldr	r1, [sp, #36]
	str	r2, [sp, #44]
	str	ip, [sp, #40]
	bl	ftl_cmp_data_ver
	cmp	r0, #0
	ldr	ip, [sp, #40]
	ldr	r2, [sp, #44]
	beq	.L2991
	ldr	r3, [sp, #56]
	ldr	r1, [sp, #60]
	cmp	r3, r1
	beq	.L2996
	ldr	r1, [sp, #52]
	cmp	r3, r1
	beq	.L2991
	cmn	r3, #1
	streq	r3, [r2, r5]
	beq	.L2995
	str	r3, [ip, #4]
	mov	r0, ip
	mov	r1, #1
	mov	r2, #0
	ldr	r8, [ip, #12]
	bl	FlashReadPages
.L2995:
	ldr	r3, [r4, #-1504]
	ldr	r3, [r3, r5]
	cmn	r3, #1
	beq	.L2996
	ldr	r5, [r8, #4]
	ldr	r0, [fp, #1760]
	mov	r1, r5
	bl	ftl_cmp_data_ver
	cmp	r0, #0
	beq	.L2996
	ldr	r0, [sp, #36]
	mov	r1, r5
	bl	ftl_cmp_data_ver
	cmp	r0, #0
	beq	.L2991
.L2996:
	mov	r0, r7
	ldr	r1, [sp, #52]
	bl	FtlReUsePrevPpa
.L2991:
	mvn	r3, #0
	str	r3, [sp, #52]
	b	.L2998
.L2988:
	ldr	r3, [sp, #60]
	ldr	r2, [sp, #56]
	cmp	r2, r3
	beq	.L2998
	ldr	r3, [sp, #52]
	cmn	r3, #1
	beq	.L3000
	ldr	r2, [r4, #-1720]
	ubfx	r3, r3, #10, #21
	cmp	r3, r2
	bcs	.L2983
.L3000:
	mov	r0, r6
	add	r1, sp, #60
	mov	r2, #1
	bl	log2phys
	ldr	r8, [sp, #56]
	cmn	r8, #1
	beq	.L2998
	ldr	r3, [sp, #52]
	cmp	r8, r3
	beq	.L2998
	ubfx	r0, r8, #10, #16
	ldr	r5, .L3092+32
	bl	P2V_block_in_plane
	ldrh	r3, [r5]
	cmp	r3, r0
	beq	.L3002
	add	r2, r5, #48
	ldrh	r2, [r2]
	cmp	r2, r0
	beq	.L3002
	add	r3, r5, #96
	ldrh	r3, [r3]
	cmp	r3, r0
	bne	.L2998
.L3002:
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
	beq	.L2998
	ldr	r0, [sp]
	bl	ftl_cmp_data_ver
	cmp	r0, #0
	bne	.L2998
	mov	r0, r6
	add	r1, sp, #56
	mov	r2, #1
	bl	log2phys
.L2998:
	ldr	r0, [sp, #52]
	cmn	r0, #1
	beq	.L2983
	ubfx	r0, r0, #10, #16
	bl	P2V_block_in_plane
	ldr	r2, [r4, #-1408]
	mov	r3, r0, asl #1
	mov	r1, r0
	ldrh	r3, [r2, r3]
	cmp	r3, #0
	beq	.L3003
.L3082:
	bl	decrement_vpc_count
	b	.L2983
.L3003:
	ldr	r0, .L3092+36
	bl	printk
	b	.L2983
.L2985:
	ldrh	r2, [r9]
	mov	r1, r6
	ldr	r3, .L3092+16
	ldr	r0, .L3092+40
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
	ldr	r3, .L3092+20
	ldr	r3, [r3, #1760]
	cmn	r3, #1
	ldreq	r3, [sp]
	beq	.L3083
	ldr	r2, [sp]
	cmp	r3, r2
	bls	.L2983
	mov	r3, r2
.L3083:
	str	r3, [fp, #1760]
.L2983:
	ldr	r3, [sp, #28]
	add	r3, r3, #1
	b	.L3084
.L3091:
	ldrb	r3, [r9, #8]	@ zero_extendqisi2
	add	r10, r10, #1
	cmp	r3, #1
	uxth	r10, r10
	bne	.L3007
	ldr	r3, .L3092+4
	ldrb	r3, [r3, #-2744]	@ zero_extendqisi2
	cmp	r3, #0
	beq	.L3007
	ldr	r3, .L3092+44
	ldr	r2, [sp, #4]
	ldrh	r3, [r3]
	cmp	r2, r10
	cmpeq	r3, r10
	beq	.L2984
.L3007:
	ldr	r2, .L3092+48
	ldrh	r3, [r2]
	cmp	r10, r3
	bne	.L2977
	ldrh	r1, [r2, #-68]
	movw	r0, #65535
	mov	r3, #0
	strh	r10, [r9, #2]	@ movhi
	strh	r3, [r9, #4]	@ movhi
.L3008:
	uxth	r2, r3
	cmp	r2, r1
	bcs	.L3074
	ldr	lr, [sp, #20]
	add	r3, r3, #1
	ldrh	ip, [lr, #2]!
	cmp	ip, r0
	str	lr, [sp, #20]
	beq	.L3008
	strb	r2, [r9, #6]
	b	.L3074
.L2984:
	ldrb	r3, [sp, #12]	@ zero_extendqisi2
	mov	r0, r9
	ldr	r1, [sp, #4]
	strb	r3, [r9, #6]
	ldrh	r3, [sp, #4]
	strh	r3, [r9, #2]	@ movhi
.L3085:
	ldr	r2, [sp, #12]
	bl	ftl_sb_update_avl_pages
.L3074:
	mov	r0, #0
	add	sp, sp, #68
	@ sp needed
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L3077:
	mov	r0, #0
	bx	lr
.L3093:
	.align	2
.L3092:
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
	ldr	r5, .L3107
	ldr	r6, .L3107+4
	add	r3, r0, r3, asl #1
	ldr	r7, .L3107+8
	ldrh	r3, [r3, #16]
.L3098:
	movw	r2, #65535
	cmp	r3, r2
	bne	.L3106
.L3100:
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
	b	.L3098
.L3106:
	ldrb	r1, [r4, #8]	@ zero_extendqisi2
	cmp	r1, #1
	bne	.L3101
	ldrb	r3, [r5, #-2744]	@ zero_extendqisi2
	cmp	r3, #0
	bne	.L3101
	ldrh	r3, [r4, #2]
	mov	r3, r3, asl #1
	ldrh	r3, [r6, r3]
	cmp	r3, r2
	bne	.L3101
	ldrh	r3, [r4, #4]
	ldrh	r0, [r4]
	sub	r3, r3, #1
	strh	r3, [r4, #4]	@ movhi
	bl	decrement_vpc_count
	ldrh	r2, [r4, #4]
	cmp	r2, #0
	bne	.L3100
	ldrh	r3, [r4, #2]
	strb	r2, [r4, #6]
	add	r3, r3, #1
	strh	r3, [r4, #2]	@ movhi
	ldmfd	sp!, {r3, r4, r5, r6, r7, pc}
.L3101:
	ldr	r2, .L3107
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
.L3108:
	.align	2
.L3107:
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
	ldr	r7, .L3127
	ldr	r8, .L3127+4
	add	r3, r0, r3, asl #1
	ldr	r9, .L3127+8
	ldrh	r2, [r3, #16]
.L3110:
	movw	r1, #65535
	cmp	r2, r1
	ldr	r6, .L3127
	bne	.L3126
.L3111:
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
	b	.L3110
.L3126:
	ldrb	r3, [r4, #8]	@ zero_extendqisi2
	ldrh	r5, [r4, #2]
	cmp	r3, #1
	ldrh	r3, [r4, #4]
	bne	.L3113
	ldrb	r0, [r8, #-2744]	@ zero_extendqisi2
	cmp	r0, #0
	bne	.L3113
	mov	r0, r5, asl #1
	ldrh	r0, [r9, r0]
	cmp	r0, r1
	bne	.L3113
	sub	r3, r3, #1
	ldrh	r0, [r4]
	strh	r3, [r4, #4]	@ movhi
	bl	decrement_vpc_count
	b	.L3111
.L3113:
	ldr	r7, .L3127+4
	orr	r5, r5, r2, asl #10
	sub	r3, r3, #1
	strh	r3, [r4, #4]	@ movhi
.L3114:
	ldrb	r3, [r4, #6]	@ zero_extendqisi2
	movw	r2, #65535
	ldrh	r0, [r6]
.L3116:
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
	beq	.L3116
	strb	r3, [r4, #6]
	ldrb	r3, [r4, #8]	@ zero_extendqisi2
	cmp	r3, #1
	bne	.L3121
	ldrb	r3, [r7, #-2744]	@ zero_extendqisi2
	cmp	r3, #0
	bne	.L3118
	ldrh	r3, [r4, #2]
	ldr	r2, .L3127+8
	mov	r3, r3, asl #1
	ldrh	r2, [r2, r3]
	movw	r3, #65535
	cmp	r2, r3
	bne	.L3118
	ldrh	r3, [r4, #4]
	cmp	r3, #0
	beq	.L3118
	sub	r3, r3, #1
	ldrh	r0, [r4]
	strh	r3, [r4, #4]	@ movhi
	bl	decrement_vpc_count
	b	.L3114
.L3118:
	ldr	r2, .L3127+4
	ldrb	r3, [r2, #-2744]	@ zero_extendqisi2
	cmp	r3, #0
	beq	.L3121
	sub	r1, r2, #1664
	ldrh	r0, [r4, #2]
	ldrh	r3, [r1, #-2]
	cmp	r0, r3
	bcc	.L3121
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
.L3121:
	mov	r0, r5
	ldmfd	sp!, {r3, r4, r5, r6, r7, r8, r9, pc}
.L3128:
	.align	2
.L3127:
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
	ldr	r4, .L3148
	ldr	r3, [r4, #-1280]
	cmp	r3, #0
	bne	.L3129
	add	r7, r4, #884
	ldrh	r5, [r7, #4]
	cmp	r5, #0
	beq	.L3131
	ldrb	r3, [r4, #892]	@ zero_extendqisi2
	cmp	r3, #0
	bne	.L3131
	sub	r3, r4, #1664
	ldrb	r2, [r4, #891]	@ zero_extendqisi2
	ldrh	r3, [r3, #-4]
	mul	r3, r3, r2
	cmp	r5, r3
	beq	.L3131
	ldrb	r8, [r4, #894]	@ zero_extendqisi2
	sub	r3, r4, #1728
	ldr	r6, [r4, #-1284]
	cmp	r8, #0
	ldrh	r9, [r3, #-8]
	bne	.L3129
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
	beq	.L3133
	mov	r1, #1
	mov	r2, r8
	add	r0, sp, #4
	bl	FlashReadPages
	b	.L3134
.L3133:
	sub	r3, r4, #1648
	mov	r1, #255
	ldrh	r2, [r3, #-10]
	bl	ftl_memset
.L3134:
	ldr	r3, .L3148+4
	mov	r8, #0
	ldr	fp, .L3148
	mov	r9, r9, asl #2
	mov	r10, r8
	strh	r3, [r5]	@ movhi
.L3135:
	cmp	r8, r9
	beq	.L3136
	ldrh	r3, [r7, #4]
	ldr	r0, .L3148+8
	cmp	r3, #0
	beq	.L3136
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
	b	.L3135
.L3136:
	mov	r3, #1
	b	.L3147
.L3131:
	mov	r3, #0
.L3147:
	strb	r3, [r4, #894]
.L3129:
	add	sp, sp, #44
	@ sp needed
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L3149:
	.align	2
.L3148:
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
	ldr	r6, .L3156
	mov	r7, #12
	ldr	r5, .L3156+4
.L3151:
	ldrh	r3, [r5]
	uxth	r0, r4
	cmp	r3, r0
	bls	.L3155
	ldr	r3, [r6, #-1364]
	mla	r3, r7, r0, r3
	ldr	r3, [r3, #4]
	cmp	r3, #0
	bge	.L3152
	bl	flush_l2p_region
.L3152:
	add	r4, r4, #1
	b	.L3151
.L3155:
	mov	r0, #0
	ldmfd	sp!, {r3, r4, r5, r6, r7, pc}
.L3157:
	.align	2
.L3156:
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
	ldr	r5, .L3175
	ldr	r3, [r5, #-1280]
	cmp	r3, #0
	bne	.L3158
	ldrb	r6, [r5, #-2744]	@ zero_extendqisi2
	cmp	r6, #0
	beq	.L3174
	ldrb	r6, [r0, #8]	@ zero_extendqisi2
	cmp	r6, #1
	ldreqh	r10, [r0, #4]
	beq	.L3160
	mov	r6, r3
.L3174:
	mov	r10, #12
.L3160:
	ldr	r7, [r5, #-1444]
	mvn	r3, #0
	str	r3, [sp, #20]
	mov	r8, #0
	ldr	r3, [r5, #-1476]
	movw	r2, #61589
	str	r7, [sp, #16]
	mov	r4, r0
	ldr	r9, .L3175
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
	ldr	r2, .L3175+4
	ldr	r3, [r5, #-1476]
	str	r2, [r3, #4]
.L3161:
	subs	r10, r10, #1
	bcc	.L3164
	ldrh	r3, [r4, #4]
	cmp	r3, #0
	bne	.L3162
.L3164:
	ldrh	r3, [r4]
	ldr	r1, [r5, #-1408]
	ldrh	r0, [r4, #4]
	mov	r3, r3, asl #1
	ldrh	r2, [r1, r3]
	rsb	r2, r0, r2
	strh	r2, [r1, r3]	@ movhi
	ldr	r3, .L3175+8
	ldrh	r3, [r3, #-4]
	strh	r3, [r4, #2]	@ movhi
	mov	r3, #0
	strb	r3, [r4, #6]
	strh	r3, [r4, #4]	@ movhi
	b	.L3158
.L3162:
	mov	r0, r4
	bl	get_new_active_ppa
	cmn	r0, #1
	str	r0, [sp, #8]
	beq	.L3164
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
	b	.L3161
.L3158:
	add	sp, sp, #40
	@ sp needed
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, pc}
.L3176:
	.align	2
.L3175:
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
	ldr	r4, .L3194
	ldr	r1, .L3194+4
	ldr	r0, .L3194+8
	bl	printk
	sub	r3, r4, #1712
	ldr	r0, [r4, #-1412]
	mov	r1, #0
	ldrh	r2, [r3, #-14]
	mov	r6, r4
	mov	r2, r2, asl #1
	bl	ftl_memset
.L3178:
	ldr	r3, [r4, #-1284]
	cmp	r5, r3
	bcs	.L3192
	mov	r0, r5
	add	r1, sp, #4
	mov	r2, #0
	bl	log2phys
	ldr	r0, [sp, #4]
	cmn	r0, #1
	beq	.L3179
	ubfx	r0, r0, #10, #16
	bl	P2V_block_in_plane
	ldr	r2, [r6, #-1412]
	mov	r0, r0, asl #1
	ldrh	r3, [r2, r0]
	add	r3, r3, #1
	strh	r3, [r2, r0]	@ movhi
.L3179:
	add	r5, r5, #1
	b	.L3178
.L3192:
	ldr	r5, .L3194
	mov	r7, #0
	add	r8, r5, #884
	add	r9, r5, #980
.L3181:
	ldr	r3, .L3194+12
	uxth	r4, r7
	ldrh	r3, [r3]
	cmp	r3, r4
	bls	.L3193
	ldr	r3, [r5, #-1408]
	mov	r6, r4, asl #1
	movw	r1, #65535
	ldrh	r2, [r3, r6]
	ldr	r3, [r5, #-1412]
	ldrh	r3, [r3, r6]
	cmp	r2, r1
	cmpne	r2, r3
	beq	.L3182
	ldrh	r1, [r8]
	cmp	r1, r4
	beq	.L3182
	ldrh	r1, [r9]
	cmp	r1, r4
	beq	.L3182
	ldr	r1, .L3194+16
	ldrh	r1, [r1]
	cmp	r1, r4
	beq	.L3182
	ldr	r0, .L3194+20
	mov	r1, r4
	bl	printk
	ldr	r3, [r5, #-1408]
	ldrh	r2, [r3, r6]
	cmp	r2, #0
	ldr	r2, [r5, #-1412]
	ldrh	r2, [r2, r6]
	strh	r2, [r3, r6]	@ movhi
	beq	.L3182
	mov	r0, r4
	bl	update_vpc_list
.L3182:
	add	r7, r7, #1
	b	.L3181
.L3193:
	bl	l2p_flush
	bl	FtlVpcTblFlush
	add	sp, sp, #12
	@ sp needed
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, pc}
.L3195:
	.align	2
.L3194:
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
	ldr	r5, .L3223
	ldrh	r7, [r0]
	ldr	r3, [r5, #-1280]
	cmp	r3, #0
	bne	.L3197
	movw	r3, #65535
	cmp	r7, r3
	mov	r4, r0
	beq	.L3198
	ldr	r2, [r5, #-1408]
	mov	r3, r7, asl #1
	mov	r0, r7
	ldrh	r3, [r2, r3]
	cmp	r3, #0
	beq	.L3199
	bl	INSERT_DATA_LIST
	b	.L3198
.L3199:
	bl	INSERT_FREE_LIST
.L3198:
	mov	r3, #0
	strb	r3, [r4, #8]
	ldr	r3, .L3223
	add	r2, r3, #932
	cmp	r4, r2
	beq	.L3200
	sub	r2, r3, #1712
	ldrh	r2, [r2, #-4]
	cmp	r2, #1
	beq	.L3200
	ldrb	r1, [r3, #-2744]	@ zero_extendqisi2
	cmp	r1, #0
	beq	.L3201
.L3200:
	mov	r3, #1
	strb	r3, [r4, #8]
	b	.L3202
.L3201:
	add	r1, r3, #884
	cmp	r4, r1
	bne	.L3202
	cmp	r2, #3
	beq	.L3204
	ldr	r3, [r3, #-1620]
	cmp	r3, #1
	bne	.L3205
.L3204:
	mov	r3, #1
	strb	r3, [r5, #892]
.L3205:
	ldr	r2, [r5, #-1872]
	ldr	r3, .L3223
	cmp	r2, #0
	beq	.L3202
	ldr	r2, [r3, #-1568]
	cmp	r2, #39
	movls	r2, #1
	strlsb	r2, [r3, #892]
.L3202:
	ldr	r3, .L3223+4
	movw	r2, #1748
	ldrh	r0, [r3, r2]
	movw	r2, #65535
	mov	r6, r3
	cmp	r0, r2
	beq	.L3207
	cmp	r7, r0
	bne	.L3208
	ldr	r2, [r5, #-1408]
	mov	r3, r0, asl #1
	ldrh	r3, [r2, r3]
	cmp	r3, #0
	beq	.L3209
.L3208:
	bl	update_vpc_list
.L3209:
	movw	r3, #1748
	mvn	r2, #0
	strh	r2, [r6, r3]	@ movhi
.L3207:
	mov	r0, r4
	bl	allocate_data_superblock
	bl	l2p_flush
	mov	r0, #0
	bl	FtlEctTblFlush
	bl	FtlVpcTblFlush
.L3197:
	mov	r0, #0
	ldmfd	sp!, {r3, r4, r5, r6, r7, pc}
.L3224:
	.align	2
.L3223:
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
	ldr	r7, .L3242
	ldr	r5, .L3242+4
	ldr	r10, [r7, #1292]
	mov	r6, r7
	cmp	r10, #0
	beq	.L3226
	ldr	r2, [r7, #1296]
	ldr	r3, [r5, #-1284]
	cmp	r2, r3
	bcs	.L3227
	mov	r4, #2048
.L3232:
	ldr	r0, [r6, #1296]
	ldr	r3, [r5, #-1284]
	cmp	r0, r3
	bcs	.L3231
	mov	r2, #0
	mov	r1, sp
	bl	log2phys
	ldr	r3, [r7, #1296]
	add	r3, r3, #1
	str	r3, [r7, #1296]
	ldr	r2, [sp]
	cmn	r2, #1
	beq	.L3230
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
	bne	.L3231
	ldr	r0, [sp]
	ubfx	r0, r0, #10, #16
	bl	P2V_block_in_plane
	bl	FtlGcRefreshBlock
.L3231:
	mvn	r0, #0
	b	.L3234
.L3230:
	subs	r4, r4, #1
	bne	.L3232
	b	.L3231
.L3227:
	ldr	r3, [r5, #-1588]
	mov	r0, #0
	str	r0, [r7, #1292]
	str	r0, [r7, #1296]
	str	r3, [r7, #1288]
	b	.L3234
.L3226:
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
	bhi	.L3236
	mov	r1, r1, lsr #10
	mov	r0, #1000
	mul	r0, r0, r3
	add	r1, r1, #1
	bl	__aeabi_uidiv
	add	r0, r0, r8
	cmp	r0, r9
	bcc	.L3236
	ldrh	r3, [r5, #28]
	ands	r0, r4, r3
	movne	r0, r10
	bne	.L3234
	ldr	r2, [r7, #1312]
	cmp	r2, r3
	beq	.L3234
.L3236:
	ldrh	r3, [r5, #28]
	mov	r0, #0
	str	r9, [r6, #1288]
	str	r0, [r6, #1296]
	str	r3, [r6, #1312]
	mov	r3, #1
	str	r3, [r6, #1292]
.L3234:
	add	sp, sp, #40
	@ sp needed
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, pc}
.L3243:
	.align	2
.L3242:
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
	ldr	r5, .L3408
	ldr	r8, [r5, #-1280]
	cmp	r8, #0
	movne	r0, #0
	bne	.L3397
	ldr	r2, .L3408+4
	ldr	r6, [r2, #3444]
	cmp	r6, #1
	bne	.L3340
	ldr	r3, [r5, #-1564]
	cmp	r3, #0
	bne	.L3340
	mov	r4, r0
	add	r0, r5, #872
	ldrh	r0, [r0]
	cmp	r0, #47
	movls	r0, r3
	bls	.L3397
	movw	r3, #3448
	mov	r7, r5
	ldrh	r2, [r2, r3]
	movw	r3, #65535
	mov	r9, r1
	str	r4, [sp, #20]
	cmp	r2, r3
	bne	.L3246
.L3249:
	ldr	r8, .L3408+8
	movw	r5, #65535
	ldrh	r4, [r8, #-2]
	cmp	r4, r5
	bne	.L3247
	b	.L3248
.L3246:
	add	r5, r5, #980
	ldrh	r2, [r5]
	cmp	r2, r3
	beq	.L3249
	mov	r0, r6
	bl	FtlGcFreeTempBlock
	cmp	r0, #0
	beq	.L3249
	mov	r0, r6
	b	.L3397
.L3247:
	ldrh	r3, [r8, #-4]
	cmp	r3, r5
	bne	.L3248
	ldrh	r0, [r8]
	cmp	r0, r3
	beq	.L3248
	ldrh	r1, [r8, #2]
	cmp	r1, r3
	strneh	r4, [r8, #-4]	@ movhi
	strneh	r0, [r8, #-2]	@ movhi
	mvnne	r3, #0
	strneh	r1, [r8]	@ movhi
	strneh	r3, [r8, #2]	@ movhi
.L3248:
	ldr	r1, [sp, #20]
	ldr	r3, [r7, #-1548]
	cmp	r1, #1
	ldr	r2, .L3408
	add	r3, r3, #1
	ldr	r6, .L3408+12
	add	r3, r3, r1, asl #7
	str	r3, [r7, #-1548]
	bne	.L3260
	ldr	r1, [r2, #-1872]
	cmp	r1, #0
	bne	.L3251
	ldrb	r2, [r2, #-2744]	@ zero_extendqisi2
	cmp	r2, #0
	beq	.L3260
.L3251:
	ldr	r2, [r7, #-1568]
	ldr	r4, .L3408
	cmp	r2, #39
	bhi	.L3260
	movw	r2, #1940
	ldrh	r2, [r6, r2]
	add	r3, r3, r2
	str	r3, [r4, #-1548]
	bl	FtlGcReFreshBadBlk
	movw	r3, #1164
	ldrh	r3, [r6, r3]
	movw	r2, #65535
	cmp	r3, r2
	bne	.L3260
	ldrh	r2, [r8, #-4]
	cmp	r2, r3
	bne	.L3260
	ldr	r3, [r4, #-1548]
	add	r4, r4, #880
	cmp	r3, #1024
	bhi	.L3252
	ldrh	r3, [r4]
	cmp	r3, #63
	bhi	.L3260
.L3252:
	ldr	ip, .L3408
	movw	r3, #1940
	ldrh	r0, [r4]
	mov	r1, #0
	sub	r4, ip, #1520
	ldr	r2, .L3408+12
	strh	r1, [r6, r3]	@ movhi
	ldrh	r10, [r4, #-10]
	add	r5, r10, #64
	cmp	r0, r5
	bgt	.L3260
	str	r1, [ip, #-1548]
	ldr	r1, [ip, #-1568]
	cmp	r1, #0
	moveq	r1, #6
	beq	.L3399
	cmp	r1, #5
	bhi	.L3254
	mov	r1, #18
.L3399:
	strh	r1, [r2, r3]	@ movhi
.L3254:
	mov	r0, #32
	bl	List_get_gc_head_node
	movw	ip, #65535
	uxth	r2, r0
	cmp	r2, ip
	beq	.L3259
	ldrh	r0, [r4, #-8]
	ldr	r5, .L3408
	cmp	r0, #0
	sub	r10, r5, #1520
	beq	.L3256
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
	bgt	.L3259
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
	beq	.L3259
	mov	ip, r4, asl #1
	ldr	r0, .L3408+16
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
	bls	.L3257
	ldr	r3, [r5, #-1408]
	ldrh	r3, [r3, ip]
	cmp	r3, #32
	strhih	fp, [r10, #-8]	@ movhi
.L3257:
	movw	r3, #1940
	mov	r2, #6
	strh	r2, [r6, r3]	@ movhi
	b	.L3261
.L3256:
	mov	r3, #1
	strh	r3, [r10, #-8]	@ movhi
.L3259:
	bl	GetSwlReplaceBlock
	movw	r3, #65535
	cmp	r0, r3
	mov	r4, r0
	bne	.L3261
	movw	r3, #1940
	mov	r2, #0
	strh	r2, [r6, r3]	@ movhi
.L3260:
	movw	r3, #1164
	movw	r4, #65535
	ldrh	r3, [r6, r3]
	cmp	r3, r4
	bne	.L3261
	ldr	r5, .L3408
	add	r2, r5, #980
	ldrh	r4, [r2]
	cmp	r4, r3
	movne	r4, r3
	beq	.L3404
.L3261:
	movw	r0, #65535
	rsb	r3, r0, r4
	clz	r3, r3
	ldr	r2, [sp, #20]
	mov	r3, r3, lsr #5
	cmp	r2, #0
	movne	r1, #0
	andeq	r1, r3, #1
	cmp	r1, #0
	beq	.L3274
	ldr	r3, .L3408+20
	ldrh	r0, [r3]
	cmp	r0, #24
	movhi	r5, #1
	bhi	.L3275
	sub	r3, r3, #2544
	cmp	r0, #16
	ldrh	r5, [r3, #-4]
	movhi	r5, r5, lsr #5
	bhi	.L3275
	cmp	r0, #12
	movhi	r5, r5, lsr #4
	bhi	.L3275
	cmp	r0, #8
	movhi	r5, r5, lsr #2
.L3275:
	ldr	r1, .L3408
	sub	r2, r1, #1520
	ldrh	r3, [r2, #-12]
	cmp	r3, r0
	bcs	.L3279
	add	r3, r1, #980
	movw	r0, #65535
	ldrh	r3, [r3]
	cmp	r3, r0
	bne	.L3280
	ldrh	r0, [r8, #-4]
	cmp	r0, r3
	bne	.L3280
	movw	r3, #1940
	ldrh	r0, [r6, r3]
	cmp	r0, #0
	bne	.L3281
	ldr	r3, [r1, #-1284]
	ldr	r1, [r1, #1124]
	add	r3, r3, r3, asl #1
	cmp	r1, r3, lsr #2
	movcs	r3, #18
	bcs	.L3282
.L3281:
	movw	r3, #1160
	ldrh	r3, [r6, r3]
	add	r3, r3, r3, asl #1
	ubfx	r3, r3, #2, #16
.L3282:
	strh	r3, [r2, #-12]	@ movhi
	mov	r3, #0
	str	r3, [r7, #-1560]
	b	.L3397
.L3280:
	movw	r3, #1160
	ldrh	r3, [r6, r3]
	add	r3, r3, r3, asl #1
	mov	r3, r3, asr #2
	strh	r3, [r2, #-12]	@ movhi
.L3279:
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
	b	.L3284
.L3274:
	ldr	r5, .L3408
	add	r2, r5, #980
	ldrh	r2, [r2]
	cmp	r2, r0
	bne	.L3285
	ldrh	r0, [r8, #-4]
	cmp	r0, r2
	movne	r3, #0
	andeq	r3, r3, #1
	cmp	r3, #0
	beq	.L3285
	movw	r3, #1164
	ldrh	r3, [r6, r3]
	cmp	r3, r2
	beq	.L3286
.L3290:
	movw	r4, #65535
	b	.L3285
.L3286:
	add	r4, r5, #880
	sub	r10, r5, #1520
	str	r1, [r5, #-1560]
	ldrh	r2, [r4]
	ldrh	r3, [r10, #-12]
	cmp	r2, r3
	bls	.L3287
	ldr	r2, .L3408+12
	movw	r3, #1940
	ldrh	r3, [r2, r3]
	cmp	r3, #0
	bne	.L3288
	ldr	r3, [r5, #-1284]
	ldr	r2, [r5, #1124]
	add	r3, r3, r3, asl #1
	cmp	r2, r3, lsr #2
	movcs	r3, #18
	bcs	.L3289
.L3288:
	movw	r3, #1160
	ldrh	r3, [r6, r3]
	add	r3, r3, r3, asl #1
	ubfx	r3, r3, #2, #16
.L3289:
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
	bhi	.L3397
.L3287:
	movw	r5, #1940
	ldr	r9, .L3408+12
	ldrh	r0, [r6, r5]
	cmp	r0, #0
	bne	.L3290
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
	ldr	r3, .L3408+24
	ldrh	r2, [r3, #-2]
	ldrh	r3, [r3, #-72]
	mul	r3, r3, r2
	add	r3, r3, r3, lsr #31
	cmp	r1, r3, asr #1
	ble	.L3291
	ldrh	r3, [r4]
	sub	r2, fp, #1
	cmp	r3, r2
	blt	.L3291
	bl	FtlReadRefresh
	ldrh	r0, [r9, r5]
	b	.L3397
.L3291:
	cmp	r1, #0
	bne	.L3290
	movw	r0, #65535
	bl	decrement_vpc_count
	ldrh	r0, [r4]
	add	r0, r0, #1
	b	.L3397
.L3285:
	ldr	r3, [r7, #-1872]
	cmp	r3, #0
	moveq	r5, #1
	movne	r5, #2
.L3284:
	movw	r3, #1164
	movw	r1, #65535
	ldrh	r2, [r6, r3]
	cmp	r2, r1
	bne	.L3293
	cmp	r4, r2
	ldrne	r2, .L3408+12
	strneh	r4, [r2, r3]	@ movhi
	bne	.L3295
	ldrh	r3, [r8, #-4]
	ldr	r2, .L3408
	cmp	r3, r4
	sub	r1, r2, #1536
	beq	.L3295
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
.L3295:
	movw	r8, #1164
	mov	r3, #0
	ldrh	r0, [r6, r8]
	strb	r3, [r6, #1172]
	movw	r3, #65535
	cmp	r0, r3
	beq	.L3293
	bl	IsBlkInGcList
	cmp	r0, #0
	ldrne	r3, .L3408+12
	mvnne	r2, #0
	strneh	r2, [r3, r8]	@ movhi
	ldrb	r3, [r7, #-2744]	@ zero_extendqisi2
	cmp	r3, #0
	beq	.L3299
	movw	r3, #1164
	ldrh	r0, [r6, r3]
	bl	ftl_get_blk_mode
	strb	r0, [r6, #1172]
.L3299:
	movw	r9, #1164
	movw	r3, #65535
	ldrh	r2, [r6, r9]
	ldr	r8, .L3408+12
	cmp	r2, r3
	ldr	r10, .L3408+28
	beq	.L3293
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
.L3293:
	ldr	r2, .L3408+32
	movw	r3, #1164
	ldrh	r3, [r6, r3]
	ldrh	r1, [r2]
	cmp	r1, r3
	beq	.L3300
	ldrh	r1, [r2, #48]
	cmp	r1, r3
	beq	.L3300
	ldrh	r2, [r2, #96]
	cmp	r2, r3
	bne	.L3334
.L3300:
	movw	r3, #1164
	mvn	r2, #0
	strh	r2, [r6, r3]	@ movhi
.L3334:
	ldr	r8, .L3408+28
	movw	r3, #65535
	ldr	r7, .L3408
	ldrh	r2, [r8]
	cmp	r2, r3
	bne	.L3302
	mov	fp, r7
	mov	r3, #0
	str	r3, [r7, #-1560]
.L3303:
	ldr	r10, .L3408+36
	ldr	r9, .L3408+12
	ldrh	r6, [r10]
	mov	r0, r6
	bl	List_get_gc_head_node
	ldr	r2, .L3408+28
	uxth	r3, r0
	strh	r3, [r2]	@ movhi
	movw	r2, #65535
	cmp	r3, r2
	moveq	r3, #0
	moveq	r0, #8
	streqh	r3, [r10]	@ movhi
	beq	.L3397
.L3304:
	mov	r0, r3
	str	r3, [sp, #12]
	bl	IsBlkInGcList
	add	r6, r6, #1
	cmp	r0, #0
	ldr	r3, [sp, #12]
	ldrne	r3, .L3408+36
	strneh	r6, [r3]	@ movhi
	bne	.L3303
	ldr	r2, .L3408+36
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
	bgt	.L3307
	cmp	lr, #8
	cmphi	r6, #48
	bls	.L3308
	ldr	r1, .L3408+40
	ldrh	r1, [r1]
	cmp	r1, #35
	bhi	.L3308
.L3307:
	ldr	lr, .L3408+36
	mov	r1, #0
	strh	r1, [lr]	@ movhi
.L3308:
	ldrh	r1, [ip, r0]
	movw	r0, #65535
	cmp	r1, r2
	cmpge	r4, r0
	bne	.L3309
	ldr	r2, .L3408+36
	ldrh	r2, [r2]
	cmp	r2, #3
	bhi	.L3309
	movw	r3, #1164
	mvn	r2, #0
	strh	r2, [r9, r3]	@ movhi
	mov	r2, #0
	ldr	r3, .L3408+36
	strh	r2, [r3]	@ movhi
	b	.L3402
.L3309:
	cmp	r1, #0
	bne	.L3310
	movw	r0, #65535
	bl	decrement_vpc_count
	ldr	r3, .L3408+36
	ldr	r2, .L3408+36
	ldrh	r3, [r3]
	add	r3, r3, #1
	strh	r3, [r2]	@ movhi
	b	.L3303
.L3310:
	mov	r2, #0
	strb	r2, [r9, #1172]
	ldrb	r2, [r7, #-2744]	@ zero_extendqisi2
	cmp	r2, #0
	beq	.L3311
	mov	r0, r3
	bl	ftl_get_blk_mode
	ldr	r3, .L3408+12
	strb	r0, [r3, #1172]
.L3311:
	ldr	r0, .L3408+28
	bl	make_superblock
	ldrh	r2, [r8]
	ldr	r1, .L3408+44
	mov	r3, #0
	ldr	r0, [r7, #-1408]
	mov	r2, r2, asl #1
	strh	r3, [r1]	@ movhi
	ldrh	r2, [r0, r2]
	strh	r3, [r8, #2]	@ movhi
	strb	r3, [r9, #1170]
	strh	r2, [r1, #2]	@ movhi
.L3302:
	ldr	r3, [sp, #20]
	cmp	r3, #1
	bne	.L3312
	bl	FtlReadRefresh
.L3312:
	mov	r3, #1
	str	r3, [r7, #-1564]
	ldr	r3, .L3408+48
	ldrh	r2, [r3]
	str	r2, [sp, #12]
	ldrb	r2, [r7, #-2744]	@ zero_extendqisi2
	cmp	r2, #0
	beq	.L3313
	ldr	r2, .L3408+12
	ldrb	r2, [r2, #1172]	@ zero_extendqisi2
	cmp	r2, #1
	ldreqh	r3, [r3, #2]
	streq	r3, [sp, #12]
.L3313:
	ldrh	r3, [r8, #2]
	ldr	r1, [sp, #12]
	add	r2, r3, r5
	ldr	r8, .L3408+12
	cmp	r2, r1
	movgt	r2, r1
	rsbgt	r3, r3, r2
	uxthgt	r5, r3
	mov	r3, #0
	str	r3, [sp, #16]
	b	.L3315
.L3407:
	ldr	r0, [r7, #-1488]
	mov	r1, r6
	ldrb	r2, [r8, #1172]	@ zero_extendqisi2
	mov	r10, #0
	bl	FlashReadPages
	ldr	r7, .L3408
.L3318:
	uxth	r3, r10
	cmp	r3, r6
	bcs	.L3405
	mov	r3, #36
	ldr	r2, [r7, #-1488]
	mul	r9, r3, r10
	add	r1, r2, r9
	ldr	r2, [r2, r9]
	ldr	fp, [r1, #12]
	cmn	r2, #1
	beq	.L3354
	ldrh	r1, [fp]
	movw	r2, #61589
	cmp	r1, r2
	bne	.L3354
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
	bne	.L3354
	ldr	r0, .L3408+44
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
	ldr	r3, .L3408+52
	ldrh	r2, [r3]
	str	r3, [sp, #24]
	strh	r2, [fp, #2]	@ movhi
	ldr	r2, [r7, #-1612]
	ldr	r0, [r7, #-1488]
	str	r2, [fp, #4]
	add	r0, r0, r9
	ldr	r2, [r8, #1740]
	ldr	r9, .L3408+12
	add	r2, r2, #1
	str	r2, [r8, #1740]
	bl	FtlGcBufAlloc
	ldrb	r2, [r7, #-2744]	@ zero_extendqisi2
	cmp	r2, #0
	beq	.L3406
.L3320:
	bl	Ftl_gc_temp_data_write_back
	cmp	r0, #0
	beq	.L3354
	ldr	r2, .L3408
	mov	r3, #0
	mvn	r1, #0
	str	r3, [r2, #-1564]
	movw	r2, #1164
	strh	r1, [r9, r2]	@ movhi
	ldr	r2, .L3408+28
	strh	r3, [r2, #2]	@ movhi
.L3402:
	movw	r3, #1940
	ldrh	r0, [r9, r3]
	b	.L3397
.L3405:
	ldr	r3, [sp, #16]
	add	r3, r3, #1
	str	r3, [sp, #16]
.L3315:
	ldrh	r3, [sp, #16]
	ldr	r7, .L3408
	cmp	r3, r5
	ldr	r6, .L3408+28
	bcs	.L3322
	ldr	r3, .L3408+56
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
.L3323:
	uxth	r1, r2
	cmp	r1, r10
	bcs	.L3407
	ldrh	r1, [r3, #2]!
	add	r2, r2, #1
	cmp	r1, lr
	orrne	r1, r0, r1, asl #10
	mlane	fp, ip, r6, r9
	addne	r6, r6, #1
	uxthne	r6, r6
	strne	r1, [fp, #4]
	b	.L3323
.L3406:
	ldrb	r2, [r7, #987]	@ zero_extendqisi2
	ldr	r1, [r8, #1740]
	cmp	r1, r2
	beq	.L3320
	ldr	r3, [sp, #24]
	ldrh	r3, [r3, #4]
	cmp	r3, #0
	beq	.L3320
.L3354:
	add	r10, r10, #1
	b	.L3318
.L3322:
	ldrh	r3, [r6, #2]
	ldr	r8, .L3408+12
	add	r5, r5, r3
	ldr	r3, [sp, #12]
	uxth	r5, r5
	mov	r9, r8
	cmp	r5, r3
	strh	r5, [r6, #2]	@ movhi
	bcc	.L3324
	ldr	r3, [r8, #1740]
	cmp	r3, #0
	beq	.L3325
	bl	Ftl_gc_temp_data_write_back
	cmp	r0, #0
	movne	r3, #0
	strne	r3, [r7, #-1564]
	movwne	r3, #1940
	ldrneh	r0, [r8, r3]
	bne	.L3397
.L3325:
	ldr	r3, .L3408+44
	ldrh	r5, [r3]
	cmp	r5, #0
	bne	.L3326
	ldrh	r3, [r6]
	ldr	r2, [r7, #-1408]
	mov	r3, r3, asl #1
	ldrh	r3, [r2, r3]
	cmp	r3, #0
	beq	.L3326
.L3327:
	ldr	r3, [r7, #-1284]
	cmp	r5, r3
	bcs	.L3332
	mov	r0, r5
	add	r1, sp, #36
	mov	r2, #0
	bl	log2phys
	ldr	r0, [sp, #36]
	cmn	r0, #1
	beq	.L3328
	ubfx	r0, r0, #10, #16
	bl	P2V_block_in_plane
	ldrh	r3, [r6]
	cmp	r3, r0
	bne	.L3328
.L3332:
	ldr	r3, [r7, #-1284]
	cmp	r5, r3
	bcc	.L3326
	ldr	r2, .L3408
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
	b	.L3326
.L3328:
	add	r5, r5, #1
	b	.L3327
.L3326:
	mvn	r3, #0
	strh	r3, [r6]	@ movhi
.L3324:
	ldr	r2, .L3408
	add	r3, r2, #880
	ldrh	r3, [r3]
	cmp	r3, #2
	ldrls	r3, .L3408+48
	ldrlsh	r5, [r3]
	bls	.L3334
.L3333:
	mov	r1, #0
	str	r1, [r2, #-1564]
	movw	r2, #1940
	ldrh	r0, [r9, r2]
	cmp	r0, r1
	addeq	r0, r3, #1
	b	.L3397
.L3340:
	mov	r0, r8
	b	.L3397
.L3404:
	ldrh	fp, [r8, #-4]
	cmp	fp, r4
	bne	.L3261
	add	r10, r5, #880
	ldr	r2, [r5, #-1548]
	ldrh	r3, [r10]
	cmp	r3, #24
	movcc	r3, #5120
	movcs	r3, #1024
	cmp	r2, r3
	movls	r4, fp
	bls	.L3261
	movw	r2, #1940
	mov	r3, #0
	str	r3, [r7, #-1548]
	strh	r3, [r6, r2]	@ movhi
	bl	GetSwlReplaceBlock
	cmp	r0, fp
	mov	r4, r0
	sub	fp, r5, #1520
	bne	.L3263
	ldrh	r2, [r10]
	ldrh	r3, [fp, #-10]
	cmp	r2, r3
	bcs	.L3264
	mov	r0, #64
	bl	List_get_gc_head_node
	uxth	r3, r0
	cmp	r3, r4
	beq	.L3273
	ldr	r2, [r5, #-1624]
	sub	r1, r5, #1712
	cmp	r2, #0
	bne	.L3266
	ldrh	r2, [r1, #-4]
	cmp	r2, #3
	beq	.L3266
	ldr	r2, [r5, #-1620]
	cmp	r2, #0
	bne	.L3266
	ldr	r2, [r5, #-1872]
	cmp	r2, #0
	bne	.L3266
	ldrb	r0, [r5, #-2744]	@ zero_extendqisi2
	cmp	r0, #0
	beq	.L3267
.L3266:
	ldr	r2, [r7, #-1408]
	mov	r3, r3, asl #1
	ldrh	r1, [r1, #-4]
	ldrh	r0, [r2, r3]
	cmp	r1, #3
	ldr	r2, .L3408+24
	ldrh	r3, [r2, #-2]
	ldrh	r2, [r2, #-72]
	mul	r2, r2, r3
	moveq	r3, r3, lsr #1
	movne	r3, #0
	add	r3, r2, r3
	cmp	r0, r3
	bgt	.L3269
	mov	r0, #0
	bl	List_get_gc_head_node
	ldr	r3, [r7, #-1284]
	ldr	r2, [r7, #1124]
	add	r3, r3, r3, asl #1
	cmp	r2, r3, lsr #2
	movls	r3, #160
	uxth	r4, r0
	bls	.L3400
	b	.L3403
.L3267:
	ldr	r2, [r5, #-1408]
	mov	r3, r3, asl #1
	ldrh	r3, [r2, r3]
	cmp	r3, #7
	bhi	.L3272
	bl	List_get_gc_head_node
	uxth	r4, r0
.L3403:
	mov	r3, #128
.L3400:
	strh	r3, [fp, #-10]	@ movhi
	movw	r3, #65535
	cmp	r4, r3
	beq	.L3273
	b	.L3263
.L3269:
	mov	r3, #128
	b	.L3401
.L3272:
	mov	r3, #64
	b	.L3401
.L3264:
	mov	r3, #80
.L3401:
	strh	r3, [fp, #-10]	@ movhi
	b	.L3273
.L3263:
	ldr	r0, [r7, #-1416]
	mov	r1, r4, asl #1
	ldr	r3, [r7, #-1408]
	ldrh	r2, [r10]
	ldrh	r3, [r3, r1]
	ldrh	r1, [r0, r1]
	ldr	r0, .L3408+60
	str	r1, [sp]
	ldrh	r1, [fp, #-12]
	str	r1, [sp, #4]
	mov	r1, r4
	bl	printk
.L3273:
	bl	FtlGcReFreshBadBlk
	b	.L3261
.L3397:
	add	sp, sp, #44
	@ sp needed
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L3409:
	.align	2
.L3408:
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
	ldr	r4, .L3454
	ldr	r8, .L3454+4
	ldr	r3, [r4, #-1280]
	ldr	r5, [r8, #1948]
	cmp	r3, #0
	bne	.L3412
	ldr	r1, [r4, #-1516]
	cmp	r1, #0
	beq	.L3412
	ldrb	r6, [r4, #-2744]	@ zero_extendqisi2
	mov	r7, #0
	ldr	r0, [r4, #-1484]
	mov	r10, #36
	cmp	r6, #0
	ldrb	r3, [r5, #9]	@ zero_extendqisi2
	ldr	r9, .L3454
	ldrneb	r6, [r5, #8]	@ zero_extendqisi2
	subne	r6, r6, #1
	clzne	r6, r6
	movne	r6, r6, lsr #5
	mov	r2, r6
	bl	FlashProgPages
.L3415:
	ldr	r3, [r4, #-1516]
	cmp	r7, r3
	bcs	.L3433
	mul	fp, r10, r7
	ldr	r3, [r9, #-1484]
	add	r2, r3, fp
	ldr	r3, [r3, fp]
	cmn	r3, #1
	beq	.L3436
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
	beq	.L3419
	ubfx	r0, r0, #10, #16
	bl	P2V_block_in_plane
	ldr	r2, [r9, #-1408]
	mov	r3, r0, asl #1
	mov	fp, r0
	ldrh	r2, [r2, r3]
	cmp	r2, #0
	bne	.L3420
	ldr	r0, .L3454+8
	mov	r1, fp
	bl	printk
.L3420:
	mov	r0, fp
	bl	decrement_vpc_count
.L3419:
	add	r7, r7, #1
	b	.L3415
.L3452:
	ldr	r6, .L3454+12
	movw	r5, #16386
.L3432:
	ldrh	r3, [r6]
	cmp	r3, #0
	beq	.L3433
	mov	r0, #1
	mov	r1, r0
	bl	ftl_do_gc
	subs	r5, r5, #1
	bne	.L3432
.L3433:
	mov	r3, #0
	str	r3, [r4, #-1516]
	b	.L3412
.L3436:
	ldr	r10, .L3454
	mov	r9, #0
.L3416:
	ldr	r3, [r4, #-1516]
	cmp	r9, r3
	bcs	.L3452
	mov	r7, #36
	ldr	r3, [r10, #-1484]
	mul	r7, r7, r9
	mov	fp, #0
	mvn	r2, #0
	str	r2, [r3, r7]
.L3422:
	ldr	r3, [r4, #-1484]
	add	r2, r3, r7
	ldr	r3, [r3, r7]
	cmn	r3, #1
	bne	.L3453
	ldr	r0, [r2, #4]
	ubfx	r0, r0, #10, #16
	bl	P2V_block_in_plane
	ldrh	r3, [r5]
	cmp	r3, r0
	bne	.L3423
	ldr	r1, [r10, #-1408]
	mov	r3, r3, asl #1
	ldrh	r0, [r5, #4]
	ldrh	r2, [r1, r3]
	rsb	r2, r0, r2
	strh	r2, [r1, r3]	@ movhi
	ldr	r3, .L3454+16
	strb	fp, [r5, #6]
	strh	fp, [r5, #4]	@ movhi
	ldrh	r3, [r3]
	strh	r3, [r5, #2]	@ movhi
.L3423:
	ldrh	r3, [r5, #4]
	cmp	r3, #0
	bne	.L3424
	mov	r0, r5
	bl	allocate_new_data_superblock
.L3424:
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
	beq	.L3422
	b	.L3412
.L3453:
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
	beq	.L3429
	ubfx	r0, r0, #10, #16
	bl	P2V_block_in_plane
	ldr	r2, [r10, #-1408]
	mov	r3, r0, asl #1
	mov	r7, r0
	ldrh	r2, [r2, r3]
	cmp	r2, #0
	bne	.L3430
	ldr	r0, .L3454+8
	mov	r1, r7
	bl	printk
.L3430:
	mov	r0, r7
	bl	decrement_vpc_count
.L3429:
	add	r9, r9, #1
	b	.L3416
.L3412:
	mov	r0, #0
	add	sp, sp, #12
	@ sp needed
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L3455:
	.align	2
.L3454:
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
	ldr	r3, .L3460
	ldr	r3, [r3, #-1280]
	cmp	r3, #0
	bne	.L3459
	ldr	r3, .L3460+4
	stmfd	sp!, {r4, lr}
	.save {r4, lr}
	ldr	r4, [r3, #3444]
	cmp	r4, #1
	bne	.L3457
	bl	FtlCacheWriteBack
	bl	l2p_flush
	mov	r0, r4
	bl	FtlEctTblFlush
	bl	FtlVpcTblFlush
.L3457:
	mov	r0, #0
	ldmfd	sp!, {r4, pc}
.L3459:
	mov	r0, #0
	bx	lr
.L3461:
	.align	2
.L3460:
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
	ldr	r3, .L3465
	ldr	r3, [r3, #3444]
	cmp	r3, #1
	bne	.L3463
	bl	FtlSysFlush
.L3463:
	mov	r0, #0
	ldmfd	sp!, {r3, pc}
.L3466:
	.align	2
.L3465:
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
	ldr	r5, .L3488
	mov	r4, r1
	ldr	r3, [r5, #-2740]
	cmp	r1, r3
	cmpls	r0, r3
	bcs	.L3479
	add	r2, r0, r1
	cmp	r2, r3
	bhi	.L3479
	cmp	r1, #31
	bhi	.L3472
.L3473:
	mov	r0, #0
	b	.L3471
.L3472:
	ldr	r3, [r5, #-1280]
	cmp	r3, #0
	bne	.L3473
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
	beq	.L3474
	rsb	r5, r6, r5
	add	r7, r0, #1
	cmp	r5, r4
	movcs	r5, r4
	uxth	r5, r5
	rsb	r4, r5, r4
.L3474:
	ldr	r5, .L3488+4
	mvn	r3, #0
	ldr	r8, .L3488
	str	r3, [sp, #4]
	mov	r6, r5
.L3475:
	ldrh	r3, [r5]
	cmp	r4, r3
	bcc	.L3487
	mov	r0, r7
	mov	r1, sp
	mov	r2, #0
	bl	log2phys
	ldr	r3, [sp]
	cmn	r3, #1
	beq	.L3476
	ldr	r2, .L3488+8
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
.L3476:
	ldrh	r3, [r6]
	add	r7, r7, #1
	rsb	r4, r3, r4
	b	.L3475
.L3487:
	ldr	r3, .L3488+8
	ldr	r2, [r3, #1952]
	cmp	r2, #32
	bls	.L3473
	mov	r4, #0
	str	r4, [r3, #1952]
	bl	l2p_flush
	bl	FtlVpcTblFlush
	b	.L3473
.L3479:
	mvn	r0, #0
.L3471:
	add	sp, sp, #8
	@ sp needed
	ldmfd	sp!, {r4, r5, r6, r7, r8, pc}
.L3489:
	.align	2
.L3488:
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
	ldr	r6, .L3529
	sub	r9, r6, #1664
	ldr	r8, [r6, #-1280]
	ldrh	r1, [r9, #-4]
	cmp	r8, #0
	bne	.L3527
	add	r4, r6, #980
	mov	r5, r6
	movw	ip, #65535
	ldrh	r6, [r4]
	cmp	r6, ip
	bne	.L3493
.L3502:
	ldrh	r2, [r4]
	movw	r3, #65535
	ldr	r6, .L3529+4
	mov	r7, #0
	ldr	r8, .L3529
	cmp	r2, r3
	str	r7, [r6, #1756]
	add	r10, r8, #980
	beq	.L3527
	bl	FtlCacheWriteBack
	ldrh	r2, [r9, #-4]
	ldrb	r0, [r8, #987]	@ zero_extendqisi2
	ldrh	r3, [r10]
	mov	r10, #12
	ldr	r1, [r8, #-1408]
	smulbb	r2, r0, r2
	mov	r3, r3, asl #1
	ldr	r9, .L3529+8
	strh	r2, [r1, r3]	@ movhi
	movw	r3, #1766
	ldr	r2, [r8, #-1608]
	ldrh	r3, [r6, r3]
	add	r3, r3, r2
	str	r3, [r8, #-1608]
	b	.L3503
.L3493:
	cmp	r0, #0
	beq	.L3496
	ldr	r2, .L3529+12
	movw	r3, #3448
	ldrh	r0, [r2, r3]
	cmp	r0, ip
	beq	.L3497
.L3498:
	mov	r1, #2
	b	.L3496
.L3497:
	strh	r8, [r2, r3]	@ movhi
	add	r3, r5, #880
	ldrh	r3, [r3]
	cmp	r3, #17
	bhi	.L3498
.L3496:
	ldr	r7, .L3529
	add	r0, r7, #980
	bl	FtlGcScanTempBlk
	cmn	r0, #1
	str	r0, [sp, #12]
	beq	.L3499
	ldr	r2, [r7, #-1416]
	mov	r6, r6, asl #1
	ldrh	r3, [r2, r6]
	cmp	r3, #4
	bls	.L3500
	sub	r3, r3, #5
	mov	r0, #1
	strh	r3, [r2, r6]	@ movhi
	bl	FtlEctTblFlush
.L3500:
	ldr	r4, .L3529+4
	ldr	r3, [r4, #1756]
	cmp	r3, #0
	bne	.L3501
	ldr	r0, [sp, #12]
	ldr	r3, [r4, #1308]
	ubfx	r0, r0, #10, #16
	add	r3, r3, #1
	str	r3, [r4, #1308]
	bl	FtlBbmMapBadBlock
	bl	FtlBbmTblFlush
.L3501:
	mov	r3, #0
	str	r3, [r4, #1756]
	b	.L3513
.L3499:
	ldr	r2, .L3529+12
	movw	r3, #3448
	ldrh	r2, [r2, r3]
	movw	r3, #65535
	cmp	r2, r3
	bne	.L3513
	b	.L3502
.L3506:
	ldr	r3, [fp, #4]
	cmp	r0, r3
	bne	.L3525
.L3505:
	add	r7, r7, #1
.L3503:
	ldrh	r3, [r9]
	uxth	r8, r7
	cmp	r3, r8
	bls	.L3528
	mul	r8, r10, r8
	ldr	r3, [r5, #-1508]
	ldr	r2, [r5, #-1284]
	add	fp, r3, r8
	ldr	r0, [fp, #8]
	cmp	r0, r2
	bcs	.L3525
	add	r1, sp, #12
	mov	r2, #0
	str	r3, [sp, #4]
	bl	log2phys
	ldr	r3, [sp, #4]
	ldr	r0, [sp, #12]
	ldr	r3, [r3, r8]
	cmp	r0, r3
	bne	.L3506
	ubfx	r0, r0, #10, #16
	bl	P2V_block_in_plane
	add	r1, fp, #4
	mov	r2, #1
	mov	r8, r0
	ldr	r0, [fp, #8]
	bl	log2phys
	mov	r0, r8
	b	.L3526
.L3525:
	ldrh	r0, [r4]
.L3526:
	bl	decrement_vpc_count
	b	.L3505
.L3528:
	movw	r0, #65535
	bl	decrement_vpc_count
	ldrb	r3, [r5, #-2744]	@ zero_extendqisi2
	cmp	r3, #0
	beq	.L3508
	ldr	r0, .L3529+16
	ldrh	r1, [r4]
	bl	printk
.L3508:
	ldrh	r0, [r4]
	ldr	r2, [r5, #-1408]
	mov	r3, r0, asl #1
	ldrh	r3, [r2, r3]
	cmp	r3, #0
	beq	.L3509
	bl	INSERT_DATA_LIST
	b	.L3510
.L3509:
	bl	INSERT_FREE_LIST
.L3510:
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
	ldr	r7, .L3529
	cmp	r3, r4
	add	r1, r7, #880
	beq	.L3511
	ldr	r3, [r7, #-1568]
	cmp	r3, #39
	bhi	.L3511
	ldr	r2, .L3529+4
	movw	r3, #1160
	ldrh	r3, [r2, r3]
	ldrh	r2, [r1]
	cmp	r2, r3
	subcc	r7, r7, #1520
	movcc	r3, r3, asl #1
	strcch	r3, [r7, #-12]	@ movhi
	b	.L3527
.L3511:
	movw	r3, #1160
	ldrh	r1, [r1]
	ldrh	r2, [r6, r3]
	ldr	r3, .L3529
	add	r0, r2, r2, asl #1
	cmp	r1, r0, asr #2
	ble	.L3527
	ldrb	r0, [r3, #-2744]	@ zero_extendqisi2
	sub	r3, r3, #1520
	cmp	r0, #0
	moveq	r2, #20
	streqh	r2, [r3, #-12]	@ movhi
	beq	.L3492
	sub	r2, r2, #2
	strh	r2, [r3, #-12]	@ movhi
.L3527:
	mov	r0, #0
	b	.L3492
.L3513:
	mov	r0, #1
.L3492:
	add	sp, sp, #20
	@ sp needed
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L3530:
	.align	2
.L3529:
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
	ldr	r4, .L3534
	ldr	r5, .L3534+4
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
	ldr	r3, .L3534+8
	mov	r2, #0
	str	r2, [r3, #1756]
	ldmfd	sp!, {r3, r4, r5, pc}
.L3535:
	.align	2
.L3534:
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
	ldr	r4, .L3538
	ldr	r3, .L3538+4
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
.L3539:
	.align	2
.L3538:
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
	ldr	r7, .L3558
	mov	r2, #0
	ldr	r4, .L3558+4
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
	bne	.L3541
.L3543:
	mvn	r8, #0
	b	.L3542
.L3541:
	bl	FtlLoadSysInfo
	subs	r8, r0, #0
	bne	.L3543
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
.L3544:
	cmp	r3, r1
	bge	.L3549
	mla	ip, r0, r3, r2
	ldr	ip, [ip, #4]
	cmp	ip, #0
	bge	.L3545
.L3549:
	ldr	r5, .L3558+8
	cmp	r3, r1
	add	r6, r5, #68
	ldrh	r2, [r5, #28]
	add	r2, r2, #1
	strh	r2, [r5, #28]	@ movhi
	bge	.L3556
	b	.L3546
.L3545:
	add	r3, r3, #1
	b	.L3544
.L3556:
	movw	r3, #1804
	ldrh	r3, [r7, r3]
	cmp	r3, #0
	beq	.L3550
.L3546:
	ldrh	r3, [r6]
	ldr	r1, [r4, #-1408]
	ldr	ip, .L3558+12
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
	ldr	r3, .L3558+16
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
.L3550:
	ldrh	r0, [r6]
	movw	r3, #65535
	ldr	r7, .L3558+20
	cmp	r0, r3
	beq	.L3551
	ldrh	r3, [r7, #4]
	cmp	r3, #0
	bne	.L3551
	ldrh	r3, [r7, #52]
	add	r4, r7, #48
	cmp	r3, #0
	bne	.L3551
	bl	FtlGcRefreshOpenBlock
	ldrh	r0, [r7, #48]
	bl	FtlGcRefreshOpenBlock
	bl	FtlVpcTblFlush
	mov	r0, r7
	bl	allocate_new_data_superblock
	mov	r0, r4
	bl	allocate_new_data_superblock
.L3551:
	ldr	r3, .L3558+24
	ldrb	r3, [r3, #32]	@ zero_extendqisi2
	cmp	r3, #0
	bne	.L3552
	ldrh	r3, [r5, #28]
	tst	r3, #31
	bne	.L3542
.L3552:
	bl	FtlVpcCheckAndModify
.L3542:
	mov	r0, r8
	ldmfd	sp!, {r4, r5, r6, r7, r8, pc}
.L3559:
	.align	2
.L3558:
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
	ldr	r4, .L3595
	ldr	r5, [r4, #-1280]
	cmp	r5, #0
	bne	.L3562
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
	beq	.L3563
	bl	FtlMakeBbt
.L3563:
	ldr	r0, .L3595
	mov	r2, #0
	ldr	ip, .L3595+4
	ldr	lr, .L3595+8
.L3564:
	ldrh	r1, [ip]
	uxth	r3, r2
	add	r2, r2, #1
	cmp	r3, r1, asl #7
	bge	.L3592
	ldr	r5, [r0, #-1456]
	mvn	r1, r3
	orr	r1, r3, r1, asl #16
	str	r1, [r5, r3, asl #2]
	ldr	r1, [r0, #-1452]
	str	lr, [r1, r3, asl #2]
	b	.L3564
.L3592:
	ldr	r9, .L3595+12
	mov	r7, #0
	add	r10, r9, #2
	ldrh	r5, [r9]
.L3566:
	ldrh	r3, [r10]
	ldr	r6, .L3595
	cmp	r3, r5
	ldr	r8, .L3595+16
	bls	.L3593
	mov	r0, r5
	mov	r1, #1
	bl	FtlLowFormatEraseBlock
	add	r5, r5, #1
	uxth	r5, r5
	add	r0, r7, r0
	uxth	r7, r0
	b	.L3566
.L3593:
	ldrh	r1, [r9, #-8]
	sub	r3, r7, #3
	sub	r5, r6, #1728
	cmp	r3, r1, asl #1
	blt	.L3568
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
.L3569:
	ldrh	r3, [r8]
	cmp	r3, r5
	bls	.L3568
	mov	r0, r5
	mov	r1, #1
	bl	FtlLowFormatEraseBlock
	add	r5, r5, #1
	uxth	r5, r5
	add	r0, r7, r0
	uxth	r7, r0
	b	.L3569
.L3568:
	mov	r5, #0
	mov	r6, r5
.L3571:
	ldrh	r1, [r9]
	uxth	r0, r5
	ldr	r10, .L3595
	add	r5, r5, #1
	cmp	r1, r0
	sub	r2, r10, #1728
	bls	.L3594
	mov	r1, #0
	bl	FtlLowFormatEraseBlock
	add	r0, r6, r0
	uxth	r6, r0
	b	.L3571
.L3594:
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
	ldr	r0, .L3595+20
	str	r5, [sp, #4]
	str	r3, [sp]
	strh	r1, [r0, r2]	@ movhi
	mov	r1, #24
	mul	r1, r1, r8
	mov	r5, r0
	ldr	ip, [sp, #8]
	cmp	r6, r1
	ble	.L3573
	rsb	r0, r6, ip
	mov	r1, r8
	str	r2, [sp, #8]
	bl	__aeabi_uidiv
	ldr	r2, [sp, #8]
	str	r0, [r10, #-1284]
	mov	r0, r0, lsr #5
	add	r0, r0, #24
	strh	r0, [r5, r2]	@ movhi
.L3573:
	ldr	r3, [r4, #-1872]
	cmp	r3, #1
	bne	.L3574
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
.L3574:
	ldrb	r3, [r4, #-2744]	@ zero_extendqisi2
	cmp	r3, #0
	beq	.L3575
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
.L3575:
	ldr	ip, .L3595
	sub	r2, ip, #1664
	ldrh	r3, [r2, #-10]
	cmp	r3, #0
	beq	.L3577
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
	ldrgt	r0, .L3595+20
	strgth	r3, [r0, r1]	@ movhi
.L3577:
	movw	r3, #1160
	ldr	r10, .L3595+24
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
	bls	.L3579
	ldr	r0, .L3595+28
	mov	r2, r2, lsr #5
	bl	printk
.L3579:
	ldr	r3, [sp, #4]
	mov	r1, #0
	ldr	r0, [r4, #-1408]
	mvn	r7, #0
	ldr	r6, .L3595+32
	ldrh	r2, [r3, #-14]
	sub	fp, r6, #884
	mov	r10, r6
	mov	r2, r2, asl #1
	bl	ftl_memset
	movw	r2, #1164
	strh	r7, [r5, r2]	@ movhi
	mov	r3, #0
	ldr	r2, .L3595+36
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
.L3580:
	ldr	r9, .L3595
	add	r8, r9, #884
	mov	r0, r8
	bl	make_superblock
	ldrb	r3, [r4, #891]	@ zero_extendqisi2
	cmp	r3, #0
	ldrh	r3, [r6]
	bne	.L3581
	ldr	r2, [fp, #-1408]
	mov	r3, r3, asl #1
	strh	r7, [r2, r3]	@ movhi
	ldrh	r3, [r10]
	add	r3, r3, #1
	strh	r3, [r10]	@ movhi
	b	.L3580
.L3581:
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
.L3582:
	ldr	r6, .L3595
	add	r8, r6, #932
	mov	r0, r8
	bl	make_superblock
	ldrb	r3, [r4, #939]	@ zero_extendqisi2
	cmp	r3, #0
	ldrh	r3, [r7]
	bne	.L3583
	ldr	r2, [r9, #-1408]
	mov	r3, r3, asl #1
	strh	fp, [r2, r3]	@ movhi
	ldrh	r3, [r10]
	add	r3, r3, #1
	strh	r3, [r10]	@ movhi
	b	.L3582
.L3583:
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
	ldr	r3, .L3595+40
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
	ldreq	r3, .L3595+44
	moveq	r2, #1
	streq	r2, [r3, #3444]
.L3562:
	mov	r0, #0
	add	sp, sp, #20
	@ sp needed
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L3596:
	.align	2
.L3595:
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
	ldr	r4, .L3626
	ldrb	r3, [r4, #-2744]	@ zero_extendqisi2
	cmp	r3, #0
	beq	.L3598
.L3600:
	mov	r0, #0
	b	.L3599
.L3598:
	ldr	r3, .L3626+4
	ldr	r0, [r3]
	bl	FlashInit
	cmp	r0, #0
	bne	.L3600
	bl	FlashLoadFactorBbt
	cmp	r0, #0
	beq	.L3601
	bl	FlashMakeFactorBbt
.L3601:
	ldr	r0, [r4, #-1776]
	bl	FlashReadIdbDataRaw
	cmp	r0, #0
	beq	.L3602
	mov	r1, #0
	mov	r2, #16
	mov	r0, sp
	bl	FlashReadFacBbtData
	mov	r3, #0
	mov	r2, r3
	mov	ip, #1
	ldr	r1, [sp]
.L3603:
	ands	lr, r1, ip, asl r2
	add	r0, r3, #1
	add	r2, r2, #1
	movne	r3, r0
	cmp	r2, #16
	bne	.L3603
	cmp	r3, #6
	ldrls	r3, .L3626+8
	bls	.L3622
	mov	r2, #0
	mov	ip, #1
.L3606:
	ands	lr, r1, ip, asl r2
	add	r0, r3, #1
	add	r2, r2, #1
	movne	r3, r0
	cmp	r2, #24
	bne	.L3606
	cmp	r3, #17
	ldr	r3, .L3626+8
	movhi	r2, #36
.L3622:
	strb	r2, [r3, #33]
	ldr	r3, .L3626+8
	ldrb	r2, [r3, #33]	@ zero_extendqisi2
	ldr	r3, .L3626+12
	strh	r2, [r3, #26]	@ movhi
.L3602:
	ldr	r1, .L3626+16
	ldr	r0, .L3626+20
	bl	printk
	ldr	r0, .L3626+12
	bl	FtlConstantsInit
	bl	FtlVariablesInit
	ldr	r0, [r4, #-1732]
	mov	r4, #1
	uxth	r0, r0
	bl	FtlFreeSysBlkQueueInit
.L3608:
	bl	FtlLoadBbt
	cmp	r0, #0
	beq	.L3609
.L3624:
	bl	FtlLowFormat
	cmp	r4, #3
	addls	r4, r4, #1
	bls	.L3608
.L3625:
	mvn	r0, #0
	b	.L3599
.L3609:
	bl	FtlSysBlkInit
	cmp	r0, #0
	bne	.L3624
	ldr	r3, .L3626+24
	mov	r2, #1
	str	r2, [r3, #3444]
.L3599:
	add	sp, sp, #16
	@ sp needed
	ldmfd	sp!, {r4, pc}
.L3627:
	.align	2
.L3626:
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
	ldr	r4, .L3645
	ldr	r3, [r4, #-1280]
	cmp	r3, #0
	beq	.L3629
.L3632:
	mov	r0, #0
	ldmfd	sp!, {r4, r5, r6, r7, r8, pc}
.L3629:
	ldrb	r3, [r4, #-2744]	@ zero_extendqisi2
	ldr	r6, .L3645+4
	cmp	r3, #0
	beq	.L3631
	ldr	r3, [r6, #1740]
	tst	r3, #1
	beq	.L3631
	add	r3, r4, #980
	ldrh	r3, [r3, #4]
	cmp	r3, #0
	bne	.L3632
.L3631:
	mov	r2, #0
	ldr	r0, [r4, #-1500]
	ldr	r1, [r6, #1740]
	mov	r3, r2
	bl	FlashProgPages
	mov	r7, #0
	mov	r8, #36
.L3633:
	ldr	r1, [r6, #1740]
	uxth	r3, r7
	ldr	r5, .L3645+4
	cmp	r3, r1
	bcs	.L3644
	mul	r3, r8, r3
	ldr	r0, [r4, #-1500]
	ldr	r2, .L3645
	add	r7, r7, #1
	add	r1, r0, r3
	ldr	lr, [r0, r3]
	ldr	ip, [r1, #12]
	cmn	lr, #1
	bne	.L3634
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
	b	.L3643
.L3634:
	ldr	r0, [ip, #12]
	ldr	r1, [r1, #4]
	ldr	r2, [ip, #8]
	bl	FtlGcUpdatePage
	b	.L3633
.L3644:
	ldr	r0, [r4, #-1500]
	bl	FtlGcBufFree
	mov	r3, #0
	str	r3, [r5, #1740]
	ldr	r3, .L3645+8
	ldrh	r3, [r3, #4]
	cmp	r3, #0
	bne	.L3632
	mov	r0, #1
	bl	FtlGcFreeTempBlock
.L3643:
	mov	r0, #1
	ldmfd	sp!, {r4, r5, r6, r7, r8, pc}
.L3646:
	.align	2
.L3645:
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
	ldr	r3, .L3651
	movw	r2, #65535
	stmfd	sp!, {r4, lr}
	.save {r4, lr}
	ldrh	r1, [r3]
	cmp	r1, r2
	beq	.L3648
	ldrh	r3, [r3, #4]
	cmp	r3, #0
	bne	.L3649
.L3648:
	bl	FtlCacheWriteBack
	mov	r0, #0
	bl	FtlGcFreeTempBlock
	ldr	r0, .L3651+4
	mov	r4, #0
	add	r0, r0, #980
	strb	r4, [r0, #8]
	bl	allocate_data_superblock
	ldr	r3, .L3651+8
	movw	r2, #1764
	strh	r4, [r3, r2]	@ movhi
	movw	r2, #1766
	strh	r4, [r3, r2]	@ movhi
	bl	l2p_flush
	mov	r0, r4
	bl	FtlEctTblFlush
	bl	FtlVpcTblFlush
.L3649:
	ldr	r0, .L3651
	ldmfd	sp!, {r4, lr}
	b	get_new_active_ppa
.L3652:
	.align	2
.L3651:
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
	ldr	r1, .L3701
	.pad #84
	sub	sp, sp, #84
	ldr	r1, [r1, #3444]
	cmp	r1, #1
	bne	.L3678
	cmp	r0, #16
	mov	r8, r3
	mov	r9, r2
	bne	.L3655
	mov	r1, r2
	add	r0, r4, #256
	mov	r2, r3
	bl	FtlVendorPartRead
	b	.L3654
.L3655:
	ldr	r5, .L3701+4
	ldr	r3, [r5, #-2740]
	cmp	r2, r3
	cmpls	r4, r3
	bcs	.L3678
	add	r2, r4, r2
	str	r2, [sp, #40]
	cmp	r2, r3
	bhi	.L3678
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
	ldr	r2, .L3701+8
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
	beq	.L3656
	bl	FtlCacheWriteBack
.L3656:
	ldr	r6, [sp, #32]
	mov	r3, #0
	ldr	r5, .L3701+4
	mov	r7, r3
	str	r3, [sp, #24]
	str	r3, [sp, #48]
	str	r3, [sp, #52]
.L3657:
	ldr	r3, [sp, #28]
	cmp	r3, #0
	beq	.L3700
	mov	r0, r6
	add	r1, sp, #76
	mov	r2, #0
	bl	log2phys
	ldr	r3, [sp, #76]
	cmn	r3, #1
	bne	.L3696
	mov	r10, #0
.L3658:
	ldr	r3, .L3701+12
	ldrh	r0, [r3]
	cmp	r10, r0
	bcs	.L3662
	mla	r0, r0, r6, r10
	ldr	r2, [sp, #40]
	cmp	r0, r4
	movcs	r3, #1
	movcc	r3, #0
	cmp	r0, r2
	movcs	r3, #0
	cmp	r3, #0
	beq	.L3660
	rsb	r0, r4, r0
	mov	r1, #0
	mov	r2, #512
	add	r0, r8, r0, asl #9
	bl	ftl_memset
.L3660:
	add	r10, r10, #1
	b	.L3658
.L3696:
	ldr	r2, [r5, #-1504]
	mov	r10, #36
	mla	r10, r10, r7, r2
	str	r3, [r10, #4]
	ldr	r3, [sp, #32]
	cmp	r6, r3
	bne	.L3663
	ldr	r3, [r5, #-1456]
	mov	r0, r4
	str	r3, [r10, #8]
	ldr	r3, .L3701+12
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
	b	.L3664
.L3663:
	ldr	r3, [sp, #36]
	cmp	r6, r3
	bne	.L3665
	ldr	r3, [r5, #-1452]
	ldr	r1, [sp, #40]
	str	r3, [r10, #8]
	ldr	r3, .L3701+12
	ldrh	r2, [r3]
	mul	r3, r2, r6
	rsb	r1, r3, r1
	str	r1, [sp, #24]
	cmp	r1, r2
	bne	.L3664
	b	.L3698
.L3665:
	ldr	r3, .L3701+12
	ldrh	r3, [r3]
	mul	r3, r3, r6
.L3698:
	rsb	r3, r4, r3
	add	r3, r8, r3, asl #9
	str	r3, [r10, #8]
.L3664:
	ldr	r3, .L3701+16
	ldr	r2, [r5, #-1440]
	str	r6, [r10, #16]
	ldrh	r3, [r3]
	mul	r3, r3, r7
	add	r7, r7, #1
	bic	r3, r3, #3
	add	r3, r2, r3
	str	r3, [r10, #12]
.L3662:
	ldr	r3, [sp, #28]
	add	r6, r6, #1
	subs	r3, r3, #1
	str	r3, [sp, #28]
	beq	.L3666
	ldr	r3, .L3701+20
	ldrh	r3, [r3]
	cmp	r7, r3, asl #3
	bne	.L3657
.L3666:
	cmp	r7, #0
	beq	.L3657
	ldr	r0, [r5, #-1504]
	mov	r1, r7
	mov	r2, #0
	ldr	fp, .L3701+8
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
.L3673:
	ldr	r3, [sp, #44]
	mov	ip, #36
	ldr	r1, [sp, #32]
	mul	r10, ip, r3
	ldr	r3, [r5, #-1504]
	add	r3, r3, r10
	ldr	r2, [r3, #16]
	cmp	r2, r1
	bne	.L3668
	ldr	r1, [r3, #8]
	ldr	r3, [r5, #-1456]
	cmp	r1, r3
	bne	.L3669
	ldr	r3, [sp, #60]
	mov	r0, r8
	ldr	r2, [sp, #64]
	add	r1, r1, r3
	b	.L3699
.L3668:
	ldr	r1, [sp, #36]
	cmp	r2, r1
	bne	.L3669
	ldr	r1, [r3, #8]
	ldr	r3, [r5, #-1452]
	cmp	r1, r3
	bne	.L3669
	ldr	r3, .L3701+12
	ldr	r2, [sp, #68]
	ldrh	r0, [r3]
	ldr	r3, [sp, #36]
	mul	r0, r0, r3
	rsb	r0, r4, r0
	add	r0, r8, r0, asl #9
.L3699:
	bl	ftl_memcpy
.L3669:
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
	beq	.L3671
	ldr	r2, [fp, #1284]
	ldr	r0, .L3701+24
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
.L3671:
	ldr	r3, [r5, #-1504]
	add	r2, r3, r10
	ldr	r3, [r3, r10]
	cmp	r3, #256
	bne	.L3672
	ldr	r0, [r2, #4]
	ubfx	r0, r0, #10, #16
	bl	P2V_block_in_plane
	bl	FtlGcRefreshBlock
.L3672:
	ldr	r3, [sp, #44]
	add	r3, r3, #1
	str	r3, [sp, #44]
	cmp	r3, r7
	bne	.L3673
	mov	r7, #0
	b	.L3657
.L3700:
	ldr	r3, .L3701+28
	ldrh	r3, [r3, #-6]
	cmp	r3, #0
	beq	.L3675
	ldr	r0, [sp, #28]
	mov	r1, #1
	bl	ftl_do_gc
.L3675:
	ldr	r0, [sp, #52]
	b	.L3654
.L3678:
	mvn	r0, #0
.L3654:
	add	sp, sp, #84
	@ sp needed
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L3702:
	.align	2
.L3701:
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
	ldr	r2, .L3723
	ldr	r5, .L3723+4
	ldr	r6, .L3723+8
	ldr	r1, .L3723+12
	ldr	r0, .L3723+16
	str	r3, [r6, #3444]
	mov	r3, #0
	str	r3, [r2, #1956]
	str	r3, [r5, #-1280]
	bl	printk
	ldr	r0, .L3723+20
	bl	FtlConstantsInit
	bl	FtlMemInit
	bl	FtlVariablesInit
	ldr	r3, [r5, #-1732]
	uxth	r0, r3
	bl	FtlFreeSysBlkQueueInit
	bl	FtlLoadBbt
	cmp	r0, #0
	ldrne	r0, .L3723+24
	bne	.L3722
	bl	FtlSysBlkInit
	subs	r4, r0, #0
	beq	.L3709
	ldr	r0, .L3723+28
.L3722:
	ldr	r1, .L3723+32
	bl	printk
	b	.L3708
.L3709:
	mov	r1, #1
	str	r1, [r6, #3444]
	bl	ftl_do_gc
	add	r3, r5, #880
	ldrh	r7, [r3]
	mov	r6, r3
	cmp	r7, #15
	bhi	.L3710
	ldr	r8, .L3723+36
	movw	r5, #65535
	ldr	r9, .L3723+40
.L3713:
	ldrh	r3, [r8]
	cmp	r3, r5
	bne	.L3711
	ldrh	r3, [r9]
	cmp	r3, r5
	bne	.L3711
	and	r0, r4, #63
	bl	List_get_gc_head_node
	uxth	r0, r0
	bl	FtlGcRefreshBlock
.L3711:
	mov	r0, #1
	mov	r1, r0
	bl	ftl_do_gc
	mov	r0, #0
	mov	r1, #1
	bl	ftl_do_gc
	ldrh	r2, [r6]
	add	r3, r7, #2
	cmp	r2, r3
	bhi	.L3708
	add	r4, r4, #1
	cmp	r4, #4096
	bne	.L3713
	b	.L3708
.L3710:
	ldrb	r3, [r5, #-2744]	@ zero_extendqisi2
	cmp	r3, #0
	beq	.L3708
	mov	r4, #128
.L3715:
	mov	r0, #1
	mov	r1, r0
	bl	ftl_do_gc
	subs	r4, r4, #1
	bne	.L3715
.L3708:
	mov	r0, #0
	ldmfd	sp!, {r3, r4, r5, r6, r7, r8, r9, pc}
.L3724:
	.align	2
.L3723:
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
	ldr	r10, .L3799
	str	r3, [sp, #8]
	ldr	r3, [r10, #-1280]
	cmp	r3, #0
	bne	.L3766
	mov	r8, r2
	ldr	r2, .L3799+4
	ldr	r2, [r2, #3444]
	cmp	r2, #1
	movne	r0, r3
	bne	.L3726
	cmp	r0, #16
	mov	r7, r1
	bne	.L3727
	add	r0, r1, #256
	ldr	r2, [sp, #8]
	mov	r1, r8
	bl	FtlVendorPartWrite
	b	.L3726
.L3727:
	ldr	r3, [r10, #-2740]
	cmp	r8, r3
	cmpls	r1, r3
	bcs	.L3769
	add	r5, r1, r8
	cmp	r5, r3
	bhi	.L3769
	ldr	r6, .L3799+8
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
	beq	.L3729
	mov	r3, #36
	ldr	r9, [r10, #-1484]
	mul	r3, r3, r2
	ldr	r2, [sp, #12]
	sub	r3, r3, #36
	add	r9, r9, r3
	ldr	r3, [r9, #16]
	cmp	r2, r3
	bne	.L3730
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
	bne	.L3731
	ldr	r3, [r6, #1964]
	cmp	r3, #2
	ble	.L3766
.L3731:
	ldr	r3, [sp, #8]
	rsb	r8, r4, r8
	add	r7, r7, r4
	str	r5, [sp, #4]
	add	r3, r3, r10
	str	r3, [sp, #8]
	ldr	r3, [sp, #12]
	add	r3, r3, #1
	str	r3, [sp, #12]
.L3730:
	mov	r3, #0
	str	r3, [r6, #1964]
.L3729:
	ldr	r0, [sp, #12]
	ldr	r1, [sp, #28]
	bl	FtlCacheMetchLpa
	cmp	r0, #0
	beq	.L3732
	bl	FtlCacheWriteBack
.L3732:
	ldr	r5, .L3799+12
	mov	r3, #0
	str	r3, [sp, #16]
	sub	r4, r5, #884
	str	r3, [sp, #32]
	str	r5, [r6, #1948]
	ldr	r6, [sp, #12]
.L3733:
	ldr	r3, [sp, #4]
	cmp	r3, #0
	beq	.L3798
	ldrh	r2, [r5, #4]
	cmp	r2, #0
	bne	.L3734
	ldr	r3, .L3799+12
	ldr	r9, .L3799+4
	cmp	r5, r3
	bne	.L3735
	add	r0, r5, #48
	ldrh	r10, [r0, #4]
	cmp	r10, #0
	bne	.L3736
	bl	allocate_new_data_superblock
	str	r10, [r9, #3452]
.L3736:
	ldr	r0, .L3799+12
	bl	allocate_new_data_superblock
	ldr	r3, [r9, #3452]
	cmp	r3, #0
	ldrne	r5, .L3799+16
	bne	.L3737
.L3738:
	ldr	r5, .L3799+12
	b	.L3737
.L3735:
	ldrh	r3, [r3, #4]
	str	r2, [r9, #3452]
	cmp	r3, #0
	bne	.L3738
	mov	r0, r5
	bl	allocate_new_data_superblock
.L3737:
	ldrh	r3, [r5, #4]
	cmp	r3, #0
	bne	.L3739
	mov	r0, r5
	bl	allocate_new_data_superblock
.L3739:
	ldr	r3, .L3799+8
	str	r5, [r3, #1948]
.L3734:
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
.L3796:
	str	r3, [sp, #20]
	ldr	r3, [sp, #20]
	ldr	r2, [sp, #44]
	cmp	r3, r2
	beq	.L3741
	ldrh	r3, [r5, #4]
	cmp	r3, #0
	beq	.L3741
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
	beq	.L3742
	ldr	r3, .L3799+20
	ldrh	r2, [r3]
	add	r3, r8, r7
	mls	r3, r2, r6, r3
	cmp	r3, r2
	bne	.L3741
.L3742:
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
	ldr	fp, .L3799+24
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
	beq	.L3743
	cmp	r10, #0
	beq	.L3744
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
	b	.L3745
.L3744:
	cmp	ip, #0
	beq	.L3745
	ldr	r3, .L3799+20
	add	r2, r8, r7
	str	r10, [sp, #32]
	ldrh	r1, [r3]
	smulbb	r1, r1, r6
	rsb	r2, r1, r2
	uxth	r3, r2
	str	r3, [sp, #16]
.L3745:
	ldr	r3, .L3799+20
	ldr	r2, [sp, #16]
	ldrh	r3, [r3]
	cmp	r2, r3
	bne	.L3746
	cmp	r10, #0
	ldr	r0, .L3799
	moveq	r3, r2
	ldr	r2, .L3799
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
	bne	.L3749
	ldr	r0, [r3, #8]
	ldr	r3, .L3799+28
	ldrh	r2, [r3]
	b	.L3794
.L3746:
	ldr	r2, [sp, #56]
	mov	r3, #36
	cmn	r2, #1
	beq	.L3750
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
	ldreq	r2, .L3799+8
	ldreq	r3, [r2, #1284]
	addeq	r3, r3, #1
	streq	r3, [r2, #1284]
	beq	.L3753
.L3751:
	ldr	r3, [r9, #8]
	cmp	r3, r6
	beq	.L3753
	ldr	r2, .L3799+8
	ldr	r0, .L3799+32
	ldr	r3, [r2, #1284]
	add	r3, r3, #1
	str	r3, [r2, #1284]
	mov	r2, r6
	ldr	r1, [r9, #8]
	bl	printk
	b	.L3753
.L3750:
	ldr	r2, [r4, #-1516]
	ldr	r1, [r4, #-1484]
	mla	r3, r3, r2, r1
	mov	r1, #0
	ldr	r0, [r3, #8]
	ldr	r3, .L3799+28
	ldrh	r2, [r3]
	bl	ftl_memset
.L3753:
	cmp	r10, #0
	mov	r3, #36
	beq	.L3754
	ldr	r1, [r4, #-1484]
	ldr	r2, [r4, #-1516]
	mla	r3, r3, r2, r1
	ldr	r1, [sp, #8]
	ldr	r0, [r3, #8]
	ldr	r3, [sp, #32]
	add	r0, r0, r3, asl #9
	b	.L3797
.L3754:
	ldr	r1, [r4, #-1516]
	ldr	r2, [r4, #-1484]
	mla	r3, r3, r1, r2
	ldr	r2, .L3799+20
	ldrh	r1, [r2]
	ldr	r0, [r3, #8]
	mul	r1, r1, r6
	ldr	r3, [sp, #8]
	rsb	r1, r7, r1
	add	r1, r3, r1, asl #9
.L3797:
	ldr	r3, [sp, #16]
	mov	r2, r3, asl #9
	b	.L3794
.L3743:
	ldr	r2, [sp, #24]
	cmp	r2, #0
	ldr	r2, [r4, #-1516]
	beq	.L3755
	ldr	r1, [r4, #-1484]
	mla	r3, r3, r2, r1
	ldr	r2, .L3799+20
	ldrh	fp, [r2]
	ldr	r2, [sp, #8]
	mul	fp, fp, r6
	rsb	fp, r7, fp
	add	fp, r2, fp, asl #9
	str	fp, [r3, #8]
	b	.L3749
.L3755:
	ldr	r0, [r4, #-1484]
	mla	r3, r3, r2, r0
	ldr	r2, .L3799+20
	ldrh	r1, [r2]
	ldrh	r2, [fp, #-2]
	ldr	r0, [r3, #8]
	mul	r1, r1, r6
	ldr	r3, [sp, #8]
	rsb	r1, r7, r1
	add	r1, r3, r1, asl #9
.L3794:
	bl	ftl_memcpy
.L3749:
	ldr	r3, .L3799+36
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
	b	.L3796
.L3741:
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
	bne	.L3759
	ldrh	r3, [r5, #4]
	cmp	r3, #0
	beq	.L3759
.L3761:
	mov	r3, #0
	str	r3, [sp, #24]
	b	.L3733
.L3759:
	bl	FtlCacheWriteBack
	ldr	r2, .L3799
	mov	r3, #0
	str	r3, [r2, #-1516]
	ldr	r3, [sp, #4]
	cmp	r3, #1
	bhi	.L3733
	b	.L3761
.L3798:
	mov	r0, r3
	ldr	r2, [sp, #12]
	ldr	r3, [sp, #28]
	rsb	r1, r2, r3
	bl	ftl_do_gc
	ldr	r1, .L3799+40
	ldrh	r3, [r1]
	mov	r6, r1
	cmp	r3, #5
	bls	.L3772
	cmp	r3, #31
	bhi	.L3766
	ldr	r3, .L3799+44
	ldrb	r3, [r3, #32]	@ zero_extendqisi2
	cmp	r3, #0
	bne	.L3766
.L3772:
	ldr	r5, .L3799+48
	ldr	r4, [sp, #4]
	ldr	r7, .L3799+52
	add	r8, r5, #2
.L3785:
	ldrh	r2, [r7]
	movw	r3, #65535
	cmp	r2, r3
	bne	.L3765
	ldrh	r3, [r5]
	cmp	r3, r2
	bne	.L3765
	ldrh	r2, [r8]
	cmp	r2, r3
	bne	.L3765
	and	r0, r4, #7
	bl	List_get_gc_head_node
	uxth	r0, r0
	bl	FtlGcRefreshBlock
.L3765:
	ldr	r2, .L3799+56
	mov	r0, #1
	mov	r1, r0
	mov	r3, #128
	strh	r3, [r2]	@ movhi
	strh	r3, [r2, #-2]	@ movhi
	bl	ftl_do_gc
	mov	r0, #0
	mov	r1, #1
	bl	ftl_do_gc
	ldr	r3, .L3799
	ldr	r3, [r3, #-1280]
	cmp	r3, #0
	bne	.L3766
	ldrh	r3, [r6]
	cmp	r3, #2
	bhi	.L3766
	add	r4, r4, #1
	cmp	r4, #256
	bne	.L3785
	b	.L3766
.L3769:
	mvn	r0, #0
	b	.L3726
.L3766:
	mov	r0, #0
.L3726:
	add	sp, sp, #100
	@ sp needed
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L3800:
	.align	2
.L3799:
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
	ldr	r4, .L3820
	ldrb	r3, [r4, #-2744]	@ zero_extendqisi2
	cmp	r3, #0
	beq	.L3804
	ldr	r8, .L3820+4
	movw	r3, #1802
	add	r9, r4, #884
	add	r5, r4, #932
	ldr	r0, .L3820+8
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
.L3806:
	subs	r5, r5, #1
	beq	.L3810
	mov	r0, #1
	mov	r1, r0
	bl	ftl_do_gc
	ldr	r3, [r4, #-1408]
	ldrh	r3, [r3, r6]
	cmp	r3, #0
	bne	.L3806
.L3810:
	ldr	r3, [r4, #-1408]
	mov	r1, r7
	ldr	r0, .L3820+8
	ldr	r9, .L3820
	ldrh	r2, [r3, r6]
	bl	printk
	ldr	r3, [r4, #-1408]
	ldrh	r5, [r3, r6]
	cmp	r5, #0
	bne	.L3808
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
.L3811:
	uxth	r2, r3
	cmp	r2, lr
	bcs	.L3819
	ldrh	r2, [r0, #2]!
	add	r3, r3, #1
	cmp	r2, r10
	movne	r2, r2, asl #10
	mlane	r1, fp, r5, r9
	addne	r5, r5, #1
	uxthne	r5, r5
	stmneib	r1, {r2, ip}
	strne	ip, [r1, #12]
	b	.L3811
.L3819:
	ldr	r3, [r4, #-1408]
	mov	r1, r7
	ldr	r0, .L3820+12
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
.L3808:
	movw	r3, #1802
	mvn	r2, #0
	strh	r2, [r8, r3]	@ movhi
.L3804:
	add	sp, sp, #52
	@ sp needed
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L3821:
	.align	2
.L3820:
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
	.type	__func__.23829, %object
	.size	__func__.23829, 11
__func__.23829:
	.ascii	"FtlMemInit\000"
	.space	1
	.type	__func__.24576, %object
	.size	__func__.24576, 12
__func__.24576:
	.ascii	"FtlCheckVpc\000"
	.type	__func__.24608, %object
	.size	__func__.24608, 17
__func__.24608:
	.ascii	"FtlDumpBlockInfo\000"
	.space	3
	.type	__func__.24627, %object
	.size	__func__.24627, 16
__func__.24627:
	.ascii	"FtlScanAllBlock\000"
	.type	__func__.24895, %object
	.size	__func__.24895, 17
__func__.24895:
	.ascii	"ftl_scan_all_ppa\000"
	.space	3
	.type	__func__.24875, %object
	.size	__func__.24875, 21
__func__.24875:
	.ascii	"FtlVpcCheckAndModify\000"
	.space	3
	.type	__func__.23902, %object
	.size	__func__.23902, 8
__func__.23902:
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
.LANCHOR0 = . + 0
.LANCHOR2 = . + 8184
.LANCHOR4 = . + 16368
	.type	gNandChipMap, %object
	.size	gNandChipMap, 32
gNandChipMap:
	.space	32
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
