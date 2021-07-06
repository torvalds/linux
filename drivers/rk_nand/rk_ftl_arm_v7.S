/*
 * Copyright (c) 2016-2018, Fuzhou Rockchip Electronics Co., Ltd
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 * date: 2021-07-16
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
	.file	"rk_ftl_arm_v7.S"
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
	add	r8, r1, r1, asl #2
	ldr	r9, .L110
	mov	r5, r0
	ldr	r7, .L110+4
	mov	r6, r1
	add	r10, r9, #256
	mov	r4, #0
.L101:
	ldrb	r3, [r7, #81]	@ zero_extendqisi2
	cmp	r4, r3
	bcs	.L109
	mov	r3, #85
	str	r3, [r5, #8]
	ldrsb	r3, [r4, r10]
	mov	r0, #200
	str	r3, [r5, #4]
	bl	ndelay
	ldrb	r3, [r7, #80]	@ zero_extendqisi2
	cmp	r3, #34
	addeq	r3, r4, r8
	addeq	r3, r10, r3
	beq	.L108
	cmp	r3, #35
	addne	r3, r9, r6
	addne	r3, r3, #400
	ldrnesb	r3, [r3]
	bne	.L107
	ldr	r3, .L110+8
	add	r2, r4, r8
	add	r3, r3, r2
.L108:
	ldrsb	r3, [r3, #5]
.L107:
	str	r3, [r5]
	add	r4, r4, #1
	b	.L101
.L109:
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, pc}
.L111:
	.align	2
.L110:
	.word	.LANCHOR1
	.word	.LANCHOR0
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
	ldr	r3, .L117
	stmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, lr}
	.save {r4, r5, r6, r7, r8, r9, r10, lr}
	add	r1, r3, r1, asl #2
	ldr	r8, .L117+4
	mov	r4, #0
	add	r5, r1, #3
	mov	r6, r0
	mov	r7, r3
	mov	r9, #161
	mov	r10, r4
.L113:
	ldrb	r3, [r8, #81]	@ zero_extendqisi2
	cmp	r4, r3
	bcs	.L116
	str	r9, [r6, #8]
	mov	r0, #300
	str	r10, [r6]
	ldrsb	r3, [r7, r4]
	add	r4, r4, #1
	str	r3, [r6]
	ldrsb	r3, [r5, #1]!
	str	r3, [r6]
	bl	ndelay
	b	.L113
.L116:
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, pc}
.L118:
	.align	2
.L117:
	.word	.LANCHOR1+408
	.word	.LANCHOR0
	.fnend
	.size	SamsungSetRRPara, .-SamsungSetRRPara
	.align	2
	.global	ftl_flash_suspend
	.type	ftl_flash_suspend, %function
ftl_flash_suspend:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	@ link register save eliminated.
	ldr	r3, .L120
	ldr	r2, [r3, #84]
	ldr	r1, [r2]
	str	r1, [r3, #88]
	ldr	r1, [r2, #4]
	str	r1, [r3, #92]
	ldr	r1, [r2, #8]
	str	r1, [r3, #96]
	ldr	r1, [r2, #12]
	str	r1, [r3, #100]
	ldr	r1, [r2, #304]
	str	r1, [r3, #104]
	ldr	r1, [r2, #308]
	str	r1, [r3, #108]
	ldr	r1, [r2, #336]
	ldr	r2, [r2, #344]
	str	r1, [r3, #112]
	str	r2, [r3, #116]
	bx	lr
.L121:
	.align	2
.L120:
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
	mov	r5, r0
	ldr	r0, .L128
	mov	r8, r3
	ldr	ip, [r5, #4]
	mov	r9, r1
	mov	r7, r2
	ldrh	r3, [r0, #134]
	bic	ip, ip, #-2147483648
	ldrh	r4, [r0, #132]
	ubfx	r6, ip, #10, #16
	ldrh	r10, [r0, #36]
	mov	fp, r0
	str	ip, [sp, #4]
	smulbb	r4, r4, r3
	ldrb	r3, [r0, #32]	@ zero_extendqisi2
	mov	r0, r6
	cmp	r3, #1
	moveq	r10, r10, asl #1
	uxth	r4, r4
	uxtheq	r10, r10
	mov	r1, r4
	bl	__aeabi_uidiv
	cmp	r9, #1
	uxth	r0, r0
	ldr	ip, [sp, #4]
	smulbb	r4, r0, r4
	rsb	r6, r4, r6
	ubfx	r4, ip, #0, #10
	uxth	r6, r6
	bne	.L124
	ldrb	r3, [fp, #148]	@ zero_extendqisi2
	cmp	r3, #0
	ldreq	r3, .L128
	addeq	r4, r3, r4, asl #1
	ldreqh	r4, [r4, #152]
.L124:
	add	ip, fp, r0, asl #2
	ldrb	r3, [sp, #48]	@ zero_extendqisi2
	ldr	r1, [ip, #1176]
	cmp	r3, #1
	mla	r1, r10, r6, r1
	add	r4, r1, r4
	str	r4, [r7]
	str	r0, [r8]
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
.L129:
	.align	2
.L128:
	.word	.LANCHOR0
	.fnend
	.size	LogAddr2PhyAddr, .-LogAddr2PhyAddr
	.align	2
	.global	FlashReadStatusEN
	.type	FlashReadStatusEN, %function
FlashReadStatusEN:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	ldr	ip, .L141
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
	bne	.L135
	cmp	r2, #0
	add	r3, r4, r3
	ldrneb	r2, [ip, #62]	@ zero_extendqisi2
	ldreqb	r2, [ip, #61]	@ zero_extendqisi2
	str	r2, [r3, #2056]
	ldrb	r0, [ip, #63]	@ zero_extendqisi2
	cmp	r0, #0
	addne	ip, r4, r5, asl #8
	movne	r2, #0
	beq	.L135
.L134:
	cmp	r2, r0
	bcs	.L135
	mov	r3, r2, asl #3
	add	r2, r2, #1
	mov	r3, r1, lsr r3
	uxtb	r3, r3
	str	r3, [ip, #4]
	b	.L134
.L135:
	mov	r0, #80
	bl	ndelay
	ldr	r0, [r4, r5, asl #8]
	uxtb	r0, r0
	ldmfd	sp!, {r3, r4, r5, pc}
.L142:
	.align	2
.L141:
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
.L144:
	mov	r0, r4
	mov	r1, r5
	mov	r2, r6
	bl	FlashReadStatusEN
	cmp	r0, #255
	beq	.L144
	tst	r0, #64
	ldmnefd	sp!, {r4, r5, r6, pc}
	mov	r0, #1
	mov	r1, #3
	bl	usleep_range
	b	.L144
	.fnend
	.size	FlashWaitReadyEN, .-FlashWaitReadyEN
	.align	2
	.global	FlashScheduleEnSet
	.type	FlashScheduleEnSet, %function
FlashScheduleEnSet:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	@ link register save eliminated.
	ldr	r3, .L151
	ldr	r2, [r3, #1208]
	str	r0, [r3, #1208]
	mov	r0, r2
	bx	lr
.L152:
	.align	2
.L151:
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
	ldr	r3, .L154
	ldr	r3, [r3, #44]
	ldrb	r0, [r3, #9]	@ zero_extendqisi2
	bx	lr
.L155:
	.align	2
.L154:
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
	ldr	r2, .L158
	add	r3, r2, r0, asl #3
	ldr	r0, [r2, r0, asl #3]
	ldrb	r3, [r3, #4]	@ zero_extendqisi2
	add	r3, r3, #8
	add	r0, r0, r3, asl #8
	bx	lr
.L159:
	.align	2
.L158:
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
	ldr	r3, .L161
	ldr	r2, [r3, #84]
	mov	r3, r0, asl #8
	orr	r0, r3, r0, asl #16
	orr	r3, r0, #1
	str	r3, [r2, #304]
	bx	lr
.L162:
	.align	2
.L161:
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
	ldr	r3, .L164
	orr	r0, r0, #16640
	ldr	r3, [r3, #84]
	str	r0, [r3, #344]
	bx	lr
.L165:
	.align	2
.L164:
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
	ldr	r3, .L169
	cmp	r0, #0
	ldr	r2, [r3, #84]
	ldr	r3, [r2]
	bfieq	r3, r0, #13, #1
	orrne	r3, r3, #253952
	str	r3, [r2]
	bx	lr
.L170:
	.align	2
.L169:
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
	ldr	r3, .L178
	ands	r1, r0, #6
	ldr	r2, [r3, #84]
	ldr	r3, [r2]
	bfieq	r3, r1, #13, #1
	beq	.L174
	orr	r3, r3, #24576
	movw	r1, #8322
	bfc	r3, #15, #1
	str	r1, [r2, #344]
	orr	r3, r3, #196608
	ldr	r1, .L178+4
	tst	r0, #4
	orrne	r3, r3, #32768
	str	r1, [r2, #304]
	mov	r1, #38
	str	r1, [r2, #308]
	mov	r1, #39
	str	r1, [r2, #308]
.L174:
	str	r3, [r2]
	mov	r0, #0
	bx	lr
.L179:
	.align	2
.L178:
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
	ldr	r3, .L181
	mov	r2, #1
	ldr	r1, [r3, r0, asl #3]
	add	r0, r3, r0, asl #3
	ldrb	r0, [r0, #4]	@ zero_extendqisi2
	ldr	r3, [r1]
	mov	r2, r2, asl r0
	bfi	r3, r2, #0, #8
	str	r3, [r1]
	bx	lr
.L182:
	.align	2
.L181:
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
	ldr	r3, .L184
	ldr	r2, [r3, r0, asl #3]
	ldr	r3, [r2]
	bfc	r3, #0, #8
	bfc	r3, #17, #1
	str	r3, [r2]
	bx	lr
.L185:
	.align	2
.L184:
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
	mov	r6, r3
	ldr	r5, .L195
	mov	fp, r2
	ldr	r4, .L195+4
	mov	r7, r0
	mov	r10, r1
	ldr	r3, [r5, #44]
	ldrb	r2, [r3, #19]	@ zero_extendqisi2
	mov	r3, r0, asl #3
	cmp	r2, #6
	addeq	r4, r4, r0, asl #6
	addeq	r4, r4, #20
	addeq	r4, r4, r6, asl #2
	beq	.L188
	cmp	r2, #7
	bne	.L189
	mov	r2, #160
	mla	r4, r2, r0, r4
	add	r2, r6, r6, asl #2
	add	r4, r4, #28
	add	r4, r4, r2, asl #1
	b	.L188
.L189:
	cmp	r2, #8
	addne	r2, r6, r3
	addeq	r2, r6, r6, asl #2
	ldreq	r4, .L195+8
	addne	r4, r4, r2, asl #3
	addeq	r4, r4, r2
	addne	r4, r4, #20
.L188:
	add	r3, r5, r3
	ldr	r9, [r5, r7, asl #3]
	mov	r0, r7
	sub	r10, r10, #1
	ldrb	r8, [r3, #4]	@ zero_extendqisi2
	bl	NandcFlashCs
	sub	fp, fp, #1
	sub	ip, r4, #1
	add	r4, r4, r10
	mov	r2, #54
	mov	r3, r8, asl #8
	add	r8, r9, r3
	str	r2, [r8, #2056]
.L191:
	cmp	ip, r4
	beq	.L194
	ldrb	r2, [fp, #1]!	@ zero_extendqisi2
	mov	r0, #200
	str	r3, [sp, #4]
	str	ip, [sp]
	str	r2, [r8, #2052]
	bl	ndelay
	ldr	ip, [sp]
	ldr	r3, [sp, #4]
	ldrsb	r2, [ip, #1]!
	str	r2, [r8, #2048]
	b	.L191
.L194:
	add	r9, r9, r3
	mov	r0, r7
	mov	r3, #22
	add	r5, r5, r7
	str	r3, [r9, #2056]
	bl	NandcFlashDeCs
	strb	r6, [r5, #2064]
	add	sp, sp, #12
	@ sp needed
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L196:
	.align	2
.L195:
	.word	.LANCHOR0
	.word	.LANCHOR0+1212
	.word	.LANCHOR0+1240
	.fnend
	.size	HynixSetRRPara, .-HynixSetRRPara
	.align	2
	.global	FlashSetReadRetryDefault
	.type	FlashSetReadRetryDefault, %function
FlashSetReadRetryDefault:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	ldr	r3, .L205
	ldr	r2, [r3, #44]
	ldrb	r2, [r2, #19]	@ zero_extendqisi2
	sub	r2, r2, #1
	cmp	r2, #7
	bxhi	lr
	stmfd	sp!, {r4, r5, r6, lr}
	.save {r4, r5, r6, lr}
	mov	r4, #0
	ldr	r6, .L205+4
	mov	r5, r3
.L198:
	ldrb	r3, [r6, r4, asl #3]	@ zero_extendqisi2
	uxtb	r0, r4
	cmp	r3, #173
	bne	.L199
	ldrb	r1, [r5, #1213]	@ zero_extendqisi2
	mov	r3, #0
	ldr	r2, .L205+8
	bl	HynixSetRRPara
.L199:
	add	r4, r4, #1
	cmp	r4, #4
	bne	.L198
	ldmfd	sp!, {r4, r5, r6, pc}
.L206:
	.align	2
.L205:
	.word	.LANCHOR0
	.word	.LANCHOR0+2068
	.word	.LANCHOR0+1216
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
	ldr	r5, .L215
	add	r4, r5, r0, asl #4
	ldr	r3, [r4, #2108]
	ldrb	r7, [r4, #2100]	@ zero_extendqisi2
	cmp	r3, #0
	beq	.L209
	mov	r6, r0
	mov	r0, r7
	add	r5, r5, r6, asl #2
	bl	NandcFlashCs
	ldr	r1, [r4, #2104]
	mov	r0, r7
	ldr	r2, [r5, #1176]
	adds	r2, r2, #0
	movne	r2, #1
	bl	FlashWaitReadyEN
	mov	r5, r0
	mov	r0, r7
	bl	NandcFlashDeCs
	ldr	r2, [r4, #2108]
	sbfx	r3, r5, #0, #1
	str	r3, [r2]
	mov	r2, #0
	ldr	r1, [r4, #2112]
	str	r2, [r4, #2108]
	cmp	r1, r2
	strne	r3, [r1]
	strne	r2, [r4, #2112]
.L209:
	mov	r0, #0
	ldmfd	sp!, {r3, r4, r5, r6, r7, pc}
.L216:
	.align	2
.L215:
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
	ldr	r3, .L225
	stmfd	sp!, {r0, r1, r2, r4, r5, lr}
	.save {r4, r5, lr}
	.pad #12
	ldr	r4, .L225+4
	ldr	r5, [r3, r0, asl #3]
.L221:
	ldr	r3, [r5]
	str	r3, [sp, #4]
	ldr	r3, [sp, #4]
	tst	r3, #512
	bne	.L222
	mov	r0, #10
	bl	ndelay
	subs	r4, r4, #1
	bne	.L221
	mvn	r0, #0
	b	.L220
.L222:
	mov	r0, #0
.L220:
	add	sp, sp, #12
	@ sp needed
	ldmfd	sp!, {r4, r5, pc}
.L226:
	.align	2
.L225:
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
	ldr	r3, .L233
	stmfd	sp!, {r0, r1, r2, r4, r5, lr}
	.save {r4, r5, lr}
	.pad #12
	ldr	r5, [r3, r0, asl #3]
	mov	r0, #130
	bl	ndelay
	ldr	r4, .L233+4
.L229:
	ldr	r3, [r5]
	str	r3, [sp, #4]
	ldr	r3, [sp, #4]
	tst	r3, #512
	bne	.L230
	mov	r0, #1
	mov	r1, #2
	bl	usleep_range
	subs	r4, r4, #1
	bne	.L229
	mvn	r0, #0
	b	.L228
.L230:
	mov	r0, #0
.L228:
	add	sp, sp, #12
	@ sp needed
	ldmfd	sp!, {r4, r5, pc}
.L234:
	.align	2
.L233:
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
	ldr	r3, .L237
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
.L238:
	.align	2
.L237:
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
	ldr	r5, .L246
	ldrb	r3, [r5, #148]	@ zero_extendqisi2
	cmp	r3, #0
	ldmeqfd	sp!, {r4, r5, r6, r7, r8, pc}
	mov	r6, r0
	bl	NandcFlashCs
	add	r4, r5, r6, asl #3
	ldr	r7, [r5, r6, asl #3]
	ldrb	r3, [r4, #2068]	@ zero_extendqisi2
	ldrb	r8, [r4, #4]	@ zero_extendqisi2
	cmp	r3, #44
	mov	r8, r8, asl #8
	bne	.L241
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
.L241:
	add	r7, r7, r8
	mov	r0, r6
	bl	NandcWaitFlashReadyNoDelay
	mov	r3, #218
	mov	r0, r6
	str	r3, [r7, #2056]
	bl	NandcWaitFlashReady
	mov	r3, #2
	strb	r3, [r5, #2228]
	ldmfd	sp!, {r4, r5, r6, r7, r8, pc}
.L247:
	.align	2
.L246:
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
	stmfd	sp!, {r4, r5, r6, r7, r8, lr}
	.save {r4, r5, r6, r7, r8, lr}
	ldr	r5, .L255
	ldrb	r3, [r5, #148]	@ zero_extendqisi2
	cmp	r3, #0
	ldmeqfd	sp!, {r4, r5, r6, r7, r8, pc}
	mov	r6, r0
	bl	NandcFlashCs
	add	r4, r5, r6, asl #3
	ldr	r7, [r5, r6, asl #3]
	ldrb	r3, [r4, #2068]	@ zero_extendqisi2
	ldrb	r8, [r4, #4]	@ zero_extendqisi2
	cmp	r3, #44
	mov	r8, r8, asl #8
	bne	.L250
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
.L250:
	add	r7, r7, r8
	mov	r0, r6
	bl	NandcWaitFlashReadyNoDelay
	mov	r3, #223
	mov	r0, r6
	str	r3, [r7, #2056]
	bl	NandcWaitFlashReady
	mov	r3, #0
	strb	r3, [r5, #2228]
	ldmfd	sp!, {r4, r5, r6, r7, r8, pc}
.L256:
	.align	2
.L255:
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
	@ args = 0, pretend = 0, frame = 16
	@ frame_needed = 0, uses_anonymous_args = 0
	ldr	r2, .L282
	stmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.save {r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.pad #20
	sub	sp, sp, #20
	ldrb	fp, [r2, #2229]	@ zero_extendqisi2
	mov	lr, #0
	ldr	r7, .L282+4
	mov	r4, #239
	and	r3, fp, #1
	str	r3, [sp, #8]
	and	r3, fp, #4
	str	r2, [sp, #12]
	mov	r5, #128
	mov	r2, lr
	mov	r6, #1
	mov	r8, #35
	mov	r9, #32
	mov	r10, #5
	uxtb	r3, r3
	str	r3, [sp, #4]
.L269:
	ldr	r3, [sp, #12]
	ldrb	ip, [lr, r7]	@ zero_extendqisi2
	ldr	r1, [r3, lr]!
	cmp	ip, #69
	cmpne	ip, #152
	ldrb	r3, [r3, #4]	@ zero_extendqisi2
	beq	.L260
	cmp	ip, #44
	cmpne	ip, #173
	bne	.L261
.L260:
	cmp	r0, #1
	bne	.L262
	ldr	fp, [sp, #8]
	cmp	fp, #0
	beq	.L261
	mov	r3, r3, asl #8
	cmp	ip, #173
	add	fp, r1, r3
	str	r4, [fp, #2056]
	streq	r0, [fp, #2052]
	beq	.L281
	cmp	ip, #44
	streq	r0, [fp, #2052]
	strne	r5, [fp, #2052]
	streq	r10, [fp, #2048]
	strne	r0, [fp, #2048]
	b	.L267
.L262:
	ldr	fp, [sp, #4]
	cmp	fp, #0
	beq	.L261
	mov	r3, r3, asl #8
	cmp	ip, #173
	add	fp, r1, r3
	str	r4, [fp, #2056]
	streq	r6, [fp, #2052]
	streq	r9, [fp, #2048]
	beq	.L267
	cmp	ip, #44
	streq	r6, [fp, #2052]
	streq	r8, [fp, #2048]
	beq	.L267
	str	r5, [fp, #2052]
.L281:
	str	r2, [fp, #2048]
.L267:
	add	r3, r1, r3
	str	r2, [r3, #2048]
	str	r2, [r3, #2048]
	str	r2, [r3, #2048]
.L261:
	add	lr, lr, #8
	cmp	lr, #32
	bne	.L269
	mov	r0, #0
	bl	NandcWaitFlashReady
	mov	r0, #0
	add	sp, sp, #20
	@ sp needed
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L283:
	.align	2
.L282:
	.word	.LANCHOR0
	.word	.LANCHOR0+2068
	.fnend
	.size	FlashSetInterfaceMode, .-FlashSetInterfaceMode
	.align	2
	.global	FlashReadSpare
	.type	FlashReadSpare, %function
FlashReadSpare:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	ldr	ip, .L286
	stmfd	sp!, {r3, r4, r5, lr}
	.save {r3, r4, r5, lr}
	mov	r5, r2
	ldr	r3, .L286+4
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
.L287:
	.align	2
.L286:
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
	ldr	r2, .L290
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
.L291:
	.align	2
.L290:
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
	ldr	r0, .L300
	ldr	r1, .L300+4
	add	r4, r4, r4, asl #2
	sub	ip, r0, #48
	mov	r2, #0
.L293:
	ldrb	r3, [r1, #81]	@ zero_extendqisi2
	cmp	r2, r3
	bcs	.L299
	ldrb	r3, [r1, #80]	@ zero_extendqisi2
	cmp	r3, #67
	add	r3, r2, r4
	addeq	r3, ip, r3
	addne	r3, r0, r3
	add	r2, r2, #1
	ldrsb	r3, [r3, #5]
	str	r3, [r5]
	b	.L293
.L299:
	mov	r0, #0
	ldmfd	sp!, {r3, r4, r5, lr}
	b	NandcWaitFlashReady
.L301:
	.align	2
.L300:
	.word	.LANCHOR1+304
	.word	.LANCHOR0
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
	ldr	r2, .L304
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
.L305:
	.align	2
.L304:
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
	ldr	r8, .L318
	.pad #20
	sub	sp, sp, #20
	mov	r6, r0
	mov	r9, r1
	mov	r7, r5
.L307:
	cmp	r7, r9
	beq	.L317
	rsb	r3, r7, r9
	add	r2, sp, #8
	add	r0, r6, r5
	mov	r1, #0
	uxtb	r3, r3
	str	r3, [sp]
	add	r3, sp, #12
	bl	LogAddr2PhyAddr
	ldrb	r2, [r8, #2230]	@ zero_extendqisi2
	ldr	r3, [sp, #12]
	cmp	r3, r2
	mvncs	r3, #0
	strcs	r3, [r6, r5]
	bcs	.L309
	add	r2, r8, r3
	add	r3, r8, r3, asl #4
	ldrb	r4, [r2, #2232]	@ zero_extendqisi2
	strb	r4, [r3, #2100]
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
	bne	.L311
	ldr	r0, .L318+4
	ldr	r1, [sp, #8]
	bl	printk
.L311:
	mov	r0, r4
	bl	NandcFlashDeCs
.L309:
	add	r7, r7, #1
	add	r5, r5, #36
	b	.L307
.L317:
	mov	r0, #0
	add	sp, sp, #20
	@ sp needed
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, pc}
.L319:
	.align	2
.L318:
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
	ldr	r4, .L356
	.pad #28
	sub	sp, sp, #28
	ldrb	r5, [r4, #32]	@ zero_extendqisi2
	cmp	r5, #0
	moveq	r9, r0
	moveq	r10, r1
	moveq	fp, r4
	beq	.L321
	mov	r1, r2
	bl	FlashEraseSLc2KBlocks
	b	.L322
.L330:
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
	ldrb	r3, [r4, #2230]	@ zero_extendqisi2
	mov	r7, r0
	ldr	r0, [sp, #20]
	cmp	r0, r3
	mvncs	r3, #0
	strcs	r3, [r9, r6]
	bcc	.L353
.L324:
	add	r5, r5, #1
.L321:
	cmp	r5, r8
	bcc	.L330
	b	.L354
.L353:
	ldrb	r3, [fp, #2240]	@ zero_extendqisi2
	cmp	r3, #0
	add	r3, r4, r0, asl #4
	ldr	r3, [r3, #2108]
	moveq	r7, #0
	cmp	r3, #0
	beq	.L326
	uxtb	r0, r0
	bl	FlashWaitCmdDone
.L326:
	ldr	r2, [sp, #20]
	cmp	r7, #0
	ldr	r0, [sp, #12]
	addne	ip, r6, #36
	addne	ip, r9, ip
	mov	r3, r2, asl #4
	add	r2, r4, r2
	add	r1, r4, r3
	add	r3, r4, r3
	ldrb	r6, [r2, #2232]	@ zero_extendqisi2
	str	r0, [r1, #2108]
	mov	r0, #0
	str	r0, [r1, #2112]
	ldr	r0, [sp, #16]
	strne	ip, [r1, #2112]
	strb	r6, [r3, #2100]
	str	r0, [r1, #2104]
	mov	r0, r6
	bl	NandcFlashCs
	cmp	r10, #1
	mov	r0, r6
	bne	.L328
	ldrb	r3, [fp, #148]	@ zero_extendqisi2
	cmp	r3, #0
	beq	.L328
	bl	flash_enter_slc_mode
	b	.L329
.L328:
	bl	flash_exit_slc_mode
.L329:
	ldr	r3, [sp, #20]
	mov	r0, r6
	ldr	r1, [sp, #16]
	add	r5, r5, r7
	add	r3, r4, r3, asl #2
	ldr	r2, [r3, #1176]
	adds	r2, r2, #0
	movne	r2, #1
	bl	FlashWaitReadyEN
	mov	r0, r6
	mov	r2, r7
	ldr	r1, [sp, #16]
	bl	FlashEraseCmd
	mov	r0, r6
	bl	NandcFlashDeCs
	b	.L324
.L354:
	ldr	r6, .L356
	mov	r5, #0
	ldr	r7, .L356+4
.L331:
	ldrb	r2, [r4, #2230]	@ zero_extendqisi2
	ldr	r3, .L356
	cmp	r5, r2
	bcs	.L355
	uxtb	r0, r5
	bl	FlashWaitCmdDone
	cmp	r10, #1
	bne	.L332
	ldrb	r3, [r6, #148]	@ zero_extendqisi2
	cmp	r3, #0
	beq	.L332
	ldrb	r0, [r7, r5, asl #4]	@ zero_extendqisi2
	bl	flash_exit_slc_mode
.L332:
	add	r5, r5, #1
	b	.L331
.L355:
	ldr	r2, [r3, #2244]
	cmp	r2, #0
	bne	.L334
.L336:
	mov	r0, #0
	b	.L322
.L334:
	ldrb	r3, [r3, #2068]	@ zero_extendqisi2
	cmp	r3, #69
	bne	.L336
	mov	r3, #0
	mov	r2, #36
	mov	r1, r3
.L335:
	cmp	r3, r8
	beq	.L336
	mul	r0, r2, r3
	add	r3, r3, #1
	str	r1, [r9, r0]
	b	.L335
.L322:
	add	sp, sp, #28
	@ sp needed
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L357:
	.align	2
.L356:
	.word	.LANCHOR0
	.word	.LANCHOR0+2100
	.fnend
	.size	FlashEraseBlocks, .-FlashEraseBlocks
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
	ldr	r0, .L364
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
	bne	.L359
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
	b	.L361
.L359:
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
.L361:
	mov	r0, r8
	mov	r1, r7
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, lr}
	b	FlashSetRandomizer
.L365:
	.align	2
.L364:
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
	ldr	r4, .L377
	bl	NandcWaitFlashReady
	bl	FlashSetReadRetryDefault
	ldr	r0, [r4, #2248]
	cmp	r0, #0
	beq	.L367
	mov	r0, #0
	bl	flash_enter_slc_mode
	b	.L368
.L367:
	bl	flash_exit_slc_mode
.L368:
	ldrb	r3, [r4, #2252]	@ zero_extendqisi2
	ldr	r5, .L377
	cmp	r3, #0
	beq	.L369
	ldrb	r3, [r5, #2229]	@ zero_extendqisi2
	tst	r3, #1
	beq	.L369
	mov	r0, #1
	bl	FlashSetInterfaceMode
	mov	r0, #1
	bl	NandcSetMode
	mov	r3, #0
	strb	r3, [r5, #2252]
.L369:
	ldr	r3, [r4]
	mov	r0, #0
	str	r0, [r3, #336]
	ldmfd	sp!, {r3, r4, r5, pc}
.L378:
	.align	2
.L377:
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
	ldr	r3, .L380
	ldr	r3, [r3, r0, asl #3]
	str	r1, [r3, #336]
	bx	lr
.L381:
	.align	2
.L380:
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
	ldr	r1, .L393
	bl	__aeabi_idiv
	ldr	r3, .L393+4
	ldr	r3, [r3, #84]
	cmp	r0, #250
	movwgt	r2, #8354
	bgt	.L391
	cmp	r0, #220
	bgt	.L392
	cmp	r0, #185
	movwgt	r2, #4226
	bgt	.L391
	cmp	r0, #160
	movwgt	r2, #4194
	bgt	.L391
	cmp	r4, #35
	movwls	r2, #4193
	bls	.L391
	cmp	r4, #99
	movwls	r2, #4225
	bls	.L391
.L392:
	movw	r2, #8322
.L391:
	str	r2, [r3, #4]
	ldmfd	sp!, {r4, pc}
.L394:
	.align	2
.L393:
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
	bls	.L396
	movw	r3, #8322
	cmp	r0, r3
	bne	.L397
.L396:
	ldr	r3, .L398
	ldr	r3, [r3, #84]
	str	r0, [r3, #4]
.L397:
	ldr	r3, .L398+4
	ldrb	r0, [r3, #493]	@ zero_extendqisi2
	b	NandcTimeCfg
.L399:
	.align	2
.L398:
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
	stmfd	sp!, {r3, r4, r5, lr}
	.save {r3, r4, r5, lr}
	mov	r1, #0
	ldr	r3, .L403
	mov	r2, #1
	mov	r5, #0
	stmia	r3, {r0, r1}
	mov	r4, r3
	str	r2, [r3, #12]
	mov	r2, #2
	str	r0, [r3, #8]
	str	r2, [r3, #20]
	mov	r2, #3
	str	r0, [r3, #16]
	str	r0, [r3, #24]
	str	r0, [r3, #84]
	str	r2, [r3, #28]
	ldr	r2, [r0]
	and	r2, r2, #253952
	ubfx	ip, r2, #13, #1
	bfi	r2, r1, #13, #1
	ldr	r1, [r0, #352]
	orr	r2, r2, #256
	str	ip, [r3, #2256]
	movw	ip, #2049
	ubfx	r1, r1, #16, #4
	str	r1, [r3, #2260]
	ldr	r1, [r0, #352]
	cmp	r1, ip
	str	r1, [r3, #2264]
	moveq	r3, #8
	streq	r3, [r4, #2260]
	str	r2, [r0]
	mov	r0, #40
	ldr	r3, [r4, #84]
	str	r5, [r3, #336]
	bl	NandcTimeCfg
	ldr	r3, [r4, #84]
	movw	r2, #8322
	mov	r0, #36864
	str	r2, [r3, #344]
	ldr	r2, .L403+4
	str	r2, [r3, #304]
	bl	ftl_malloc
	str	r5, [r4, #2296]
	str	r5, [r4, #2304]
	str	r0, [r4, #2268]
	str	r0, [r4, #2272]
	add	r0, r0, #32768
	str	r0, [r4, #2276]
	ldmfd	sp!, {r3, r4, r5, pc}
.L404:
	.align	2
.L403:
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
	ldr	ip, .L407
	str	lr, [sp, #-4]!
	.save {lr}
	ldr	lr, [ip, #84]
	ldr	lr, [lr, #4]
	str	lr, [r0]
	ldr	r0, [ip, #84]
	ldr	r0, [r0]
	str	r0, [r1]
	ldr	r1, [ip, #84]
	ldr	r1, [r1, #304]
	str	r1, [r2]
	ldr	r1, [ip, #84]
	ldr	r2, [r1, #308]
	ldr	r1, [r1, #344]
	uxtb	r2, r2
	orr	r2, r2, r1, asl #16
	str	r2, [r3]
	ldr	pc, [sp], #4
.L408:
	.align	2
.L407:
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
	ldr	r3, .L417
	mov	r1, #1
	ldr	r2, [r3, #84]
	str	r0, [r3, #2308]
	mov	r3, #0
	str	r1, [r2, #8]
	mov	r1, #16
	cmp	r0, r1
	bfi	r3, r1, #8, #8
	bfc	r3, #18, #1
	bne	.L410
.L413:
	bfc	r3, #4, #1
	b	.L411
.L410:
	cmp	r0, #24
	orreq	r3, r3, #16
	beq	.L411
	cmp	r0, #40
	orr	r3, r3, #262144
	orr	r3, r3, #16
	beq	.L413
.L411:
	orr	r3, r3, #1
	str	r3, [r2, #12]
	bx	lr
.L418:
	.align	2
.L417:
	.word	.LANCHOR0
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
	ldr	r3, .L420
	strb	r0, [r3, #2312]
	b	NandcBchSel
.L421:
	.align	2
.L420:
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
	ldr	r3, .L431
	stmfd	sp!, {r4, r5, r6, lr}
	.save {r4, r5, r6, lr}
	mov	r5, #0
	ldr	r2, [r3, #84]
	mov	r4, r3
	ldr	r1, [r3, #88]
	ldr	r6, .L431+4
	str	r1, [r2]
	ldr	r1, [r3, #92]
	ldr	r2, [r3, #84]
	str	r1, [r2, #4]
	ldr	r1, [r3, #96]
	str	r1, [r2, #8]
	ldr	r1, [r3, #100]
	str	r1, [r2, #12]
	ldr	r1, [r3, #104]
	str	r1, [r2, #304]
	ldr	r1, [r3, #108]
	str	r1, [r2, #308]
	ldr	r1, [r3, #112]
	str	r1, [r2, #336]
	ldr	r1, [r3, #116]
	str	r1, [r2, #344]
.L424:
	ldrb	r3, [r6, r5, asl #3]	@ zero_extendqisi2
	sub	r3, r3, #1
	uxtb	r3, r3
	cmp	r3, #253
	bhi	.L423
	uxtb	r0, r5
	bl	FlashReset
.L423:
	add	r5, r5, #1
	cmp	r5, #4
	bne	.L424
	ldrb	r3, [r4, #2252]	@ zero_extendqisi2
	ldr	r5, .L431
	cmp	r3, #0
	beq	.L425
	mov	r0, #1
	bl	NandcSetMode
	ldrb	r0, [r5, #2229]	@ zero_extendqisi2
	bl	FlashSetInterfaceMode
	ldrb	r0, [r5, #2229]	@ zero_extendqisi2
	bl	NandcSetMode
	ldrb	r0, [r5, #105]	@ zero_extendqisi2
	bl	NandcSetDdrPara
.L425:
	ldr	r3, [r4, #44]
	ldmfd	sp!, {r4, r5, r6, lr}
	ldrb	r0, [r3, #20]	@ zero_extendqisi2
	b	FlashBchSel
.L432:
	.align	2
.L431:
	.word	.LANCHOR0
	.word	.LANCHOR0+2068
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
	.global	rk_nandc_flash_ready
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
	.type	NandcSendDumpDataStart, %function
NandcSendDumpDataStart:
	.fnstart
	@ args = 0, pretend = 0, frame = 8
	@ frame_needed = 0, uses_anonymous_args = 0
	@ link register save eliminated.
	ldr	r2, [r0, #16]
	.pad #8
	sub	sp, sp, #8
	ldr	r3, .L439
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
.L440:
	.align	2
.L439:
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
.L442:
	ldr	r3, [r0, #8]
	str	r3, [sp, #4]
	ldr	r3, [sp, #4]
	tst	r3, #1048576
	beq	.L442
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
	mov	lr, #16
	ldr	r4, .L464
	mov	r5, #0
	.pad #28
	sub	sp, sp, #28
	ldr	r6, [r4, r0, asl #3]
	add	r0, r4, r0, asl #3
	ldr	r8, [sp, #64]
	ldrb	r0, [r0, #4]	@ zero_extendqisi2
	ldr	r7, [r6, #12]
	ldr	ip, [sp, #68]
	bfi	r7, lr, #8, #8
	bfi	r7, r5, #3, #1
	bfi	r5, r1, #1, #1
	orr	r5, r5, #8
	bfi	r7, r0, #5, #3
	mov	r0, #1
	bfi	r5, r0, #5, #2
	orr	r5, r5, #536870912
	mov	r3, r3, lsr r0
	orr	r5, r5, #1024
	bfi	r5, r3, #4, #1
	ldr	r3, [r4, #2260]
	cmp	r3, #3
	bls	.L447
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
	beq	.L448
	cmp	r1, #0
	bne	.L449
.L457:
	add	r2, r2, #1
	cmp	r8, #0
	mov	r2, r2, asr #1
	movne	r0, r8
	bfi	r5, r2, #22, #6
	ldreq	r0, [r4, #2272]
	b	.L451
.L449:
	ldr	r3, [r4, #2308]
	mov	r9, r4
	cmp	r3, #25
	movcc	r3, #64
	movcs	r3, #128
	str	r3, [sp, #4]
	mov	r3, r2, lsr #1
	str	r3, [sp, #12]
	mov	r3, #0
	mov	r0, r3
.L453:
	ldr	lr, [sp, #12]
	cmp	r0, lr
	bcs	.L457
	ldr	lr, [sp, #8]
	mov	r10, r3, lsr #2
	add	r0, r0, #1
	cmp	lr, #0
	ldrneh	fp, [ip, #2]
	mvneq	fp, #0
	ldrneh	lr, [ip], #4
	ldreq	lr, [r9, #2276]
	orrne	lr, lr, fp, asl #16
	ldrne	fp, [r9, #2276]
	streq	fp, [lr, r10, asl #2]
	strne	lr, [fp, r10, asl #2]
	ldr	lr, [sp, #4]
	add	r3, r3, lr
	b	.L453
.L451:
	ldr	r3, [r4, #2276]
	ubfx	r10, r5, #22, #5
	mov	r9, r1
	str	r0, [r4, #2280]
	mov	r2, r9
	mov	r1, r10, asl #10
	str	r3, [r4, #2284]
	bl	rknand_dma_map_single
	mov	r2, r9
	mov	r1, r10, asl #7
	clz	r9, r9
	mov	r9, r9, lsr #5
	str	r0, [r4, #2288]
	ldr	r0, [r4, #2284]
	bl	rknand_dma_map_single
	mov	r3, #1
	str	r3, [r4, #2296]
	tst	r8, #3
	ldr	r3, [r4, #2288]
	str	r0, [r4, #2292]
	str	r3, [r6, #20]
	ldr	r3, [r4, #2292]
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
.L448:
	ldr	r3, [sp, #20]
	str	r3, [r6, #16]
.L447:
	str	r7, [r6, #12]
	str	r5, [r6, #8]
	orr	r5, r5, #4
	str	r5, [r6, #8]
	add	sp, sp, #28
	@ sp needed
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L465:
	.align	2
.L464:
	.word	.LANCHOR0
	.fnend
	.size	NandcXferStart, .-NandcXferStart
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
.L467:
	cmp	r2, r0
	uxth	r3, r1
	add	r1, r1, #1
	movls	r2, r2, asl #1
	bls	.L467
.L469:
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
	ldr	r3, .L473
	cmp	r0, #23
	movw	r1, #2330
	add	r2, r3, #2320
	movls	r0, #24
	ldrh	r1, [r3, r1]
	ldrh	r2, [r2]
	str	r0, [r3, #2316]
	mul	r2, r2, r0
	rsb	r0, r0, r1
	movw	r1, #2328
	strh	r0, [r3, r1]	@ movhi
	mov	r0, #0
	ldr	r1, [r3, #2336]
	str	r2, [r3, #2324]
	rsb	r2, r2, r1
	str	r2, [r3, #2332]
	bx	lr
.L474:
	.align	2
.L473:
	.word	.LANCHOR0
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
	movw	r3, #2340
	ldr	r4, .L504
	movw	r1, #2344
	ldrh	r6, [r0, #8]
	mov	r5, r0
	ldrh	r2, [r0, #10]
	.pad #20
	sub	sp, sp, #20
	ldrh	lr, [r0, #14]
	strh	r6, [r4, r3]	@ movhi
	movw	r3, #2342
	strh	r2, [r4, r3]	@ movhi
	ldrh	r3, [r0, #12]
	ldr	r0, .L504+4
	strh	r3, [r4, r1]	@ movhi
	movw	r1, #2330
	strh	lr, [r4, r1]	@ movhi
	mov	r1, #0
.L476:
	strb	r1, [r1, r0]
	add	r1, r1, #1
	cmp	r1, #32
	bne	.L476
	ldrh	r0, [r5, #14]
	ldrh	r1, [r5, #20]
	cmp	r1, r0, lsr #8
	bcs	.L477
	uxtb	r10, r3
	ldr	r9, .L504+4
	mov	r1, r10, asl #1
	uxtb	r1, r1
	str	r1, [sp, #4]
	sub	r1, r2, #1
	mul	r1, r3, r1
	str	r1, [sp]
	mov	r1, #0
.L478:
	cmp	r1, r3
	bcs	.L480
	ldr	ip, [sp]
	uxtb	r0, r1
	rsb	r7, r3, r1
	add	ip, r1, ip
	add	r7, r9, r7
	add	ip, r9, ip
	str	ip, [sp, #8]
	mov	ip, #0
	str	r7, [sp, #12]
	mov	r8, ip
.L481:
	cmp	r8, r2
	add	ip, ip, r3
	bcs	.L503
	ldr	r7, [sp, #12]
	add	fp, r0, r10
	add	r8, r8, #1
	strb	r0, [r7, ip]
	ldr	r7, [sp, #8]
	strb	fp, [r7, ip]
	ldr	r7, [sp, #4]
	add	r0, r0, r7
	uxtb	r0, r0
	b	.L481
.L503:
	add	r1, r1, #1
	b	.L478
.L480:
	movw	r1, #2342
	mov	r2, r2, asl #1
	strh	r2, [r4, r1]	@ movhi
	movw	r2, #2330
	mov	lr, lr, lsr #1
	strh	lr, [r4, r2]	@ movhi
.L477:
	cmp	r6, #1
	movw	r2, #2380
	mov	r1, #5
	strh	r1, [r4, r2]	@ movhi
	movw	r1, #2382
	mov	r0, #0
	strh	r0, [r4, r1]	@ movhi
	ldreq	r1, .L504
	ldrb	r9, [r4, #32]	@ zero_extendqisi2
	ldr	r8, .L504+8
	streqh	r6, [r1, r2]	@ movhi
	movw	r1, #2342
	ldrh	r7, [r4, r1]
	cmp	r9, #0
	mov	r2, #4352
	strh	r2, [r8]	@ movhi
	movne	r2, #384
	strneh	r2, [r8]	@ movhi
	smulbb	r7, r7, r3
	ldr	r2, .L504
	ldrh	r10, [r5, #16]
	add	r1, r2, #2320
	str	r2, [sp, #8]
	uxth	r7, r7
	strh	r7, [r1]	@ movhi
	movw	r1, #2330
	ldrh	r6, [r4, r1]
	movw	r1, #2386
	smulbb	r0, r7, r10
	smulbb	r3, r6, r3
	strh	r3, [r4, r1]	@ movhi
	movw	r3, #2388
	ldrh	r1, [r5, #18]
	strh	r10, [r4, r3]	@ movhi
	movw	r3, #2390
	strh	r1, [r4, r3]	@ movhi
	movw	r3, #2392
	strh	r0, [r4, r3]	@ movhi
	movw	r0, #2394
	ldrh	r3, [r5, #20]
	str	r1, [sp, #4]
	strh	r3, [r4, r0]	@ movhi
	mov	r0, r3
	str	r3, [sp]
	bl	Ftl_log2
	movw	ip, #2398
	cmp	r6, #1024
	ldr	r3, [sp]
	mov	fp, r0
	movw	r0, #2396
	ldr	r2, [sp, #8]
	strh	fp, [r4, r0]	@ movhi
	mov	r0, r3, asl #9
	ldr	r1, [sp, #4]
	uxth	r0, r0
	strh	r0, [r4, ip]	@ movhi
	add	ip, r2, #2400
	mov	r0, r0, lsr #8
	strh	r0, [ip]	@ movhi
	movw	r0, #2402
	ldrh	ip, [r5, #26]
	mul	r1, r1, r3
	ldr	r5, .L504
	strh	ip, [r4, r0]	@ movhi
	movwhi	ip, #2382
	mul	r0, r6, r7
	str	r0, [r4, #2336]
	uxtbhi	r0, r6
	strhih	r0, [r2, ip]	@ movhi
	movw	r2, #2382
	ldrh	r2, [r4, r2]
	ldrh	r0, [r8]
	rsb	r2, r2, r6
	mov	r6, r6, asl #6
	mul	r2, r2, r7
	mov	r0, r0, asl #3
	mul	r2, r3, r2
	mul	r10, r10, r2
	mov	r10, r10, asr #11
	str	r10, [r4, #2404]
	bl	__aeabi_idiv
	movw	r3, #2408
	mov	r1, r7
	ldr	r10, .L504
	uxth	r0, r0
	cmp	r0, #4
	strhih	r0, [r5, r3]	@ movhi
	movls	r2, #4
	strlsh	r2, [r5, r3]	@ movhi
	cmp	r9, #0
	movw	r2, #2410
	movne	r3, #640
	strneh	r3, [r8]	@ movhi
	ldrh	r3, [r8]
	mov	r3, r3, asr fp
	add	fp, fp, #9
	mov	r0, r6, asr fp
	add	r3, r3, #2
	strh	r3, [r4, r2]	@ movhi
	movw	r3, #2412
	strh	r0, [r4, r3]	@ movhi
	uxth	r0, r0
	add	r6, r0, #8
	mul	r3, r7, r0
	str	r3, [r4, #2416]
	movw	r3, #2408
	ldrh	r0, [r4, r3]
	bl	__aeabi_uidiv
	cmp	r7, #1
	uxtah	r0, r6, r0
	ldr	r6, .L504
	addeq	r0, r0, #4
	str	r0, [r10, #2316]
	ldr	r3, [r4, #2316]
	uxth	r0, r3
	bl	FtlSysBlkNumInit
	ldr	r3, [r4, #2316]
	ldr	r2, [r4, #2332]
	mov	r0, #2048
	str	r3, [r4, #2420]
	movw	r3, #2388
	ldrh	r3, [r4, r3]
	mov	r2, r2, asl #2
	mul	r3, r3, r2
	movw	r2, #2396
	ldrh	r5, [r4, r2]
	add	r5, r5, #9
	mov	r5, r3, lsr r5
	movw	r3, #2424
	add	r5, r5, #2
	uxth	r5, r5
	strh	r5, [r4, r3]	@ movhi
	movw	r3, #2394
	ldrh	r7, [r4, r3]
	mov	r1, r7
	bl	__aeabi_idiv
	movw	r1, #2408
	ldrh	r2, [r4, r1]
	movw	r3, #2426
	ldrb	ip, [r4, #148]	@ zero_extendqisi2
	cmp	ip, #0
	strh	r0, [r4, r3]	@ movhi
	mov	r3, #0
	str	r3, [r4, #2428]
	add	r3, r2, #3
	strh	r3, [r4, r1]	@ movhi
	addne	r2, r2, #4
	ldr	r3, [r4, #2416]
	strneh	r2, [r6, r1]	@ movhi
	add	r0, r3, #3
	addne	r3, r3, #5
	str	r0, [r4, #2416]
	bne	.L502
	cmp	r0, #7
	bhi	.L491
	mov	r3, #8
.L502:
	str	r3, [r6, #2416]
.L491:
	ldr	r2, .L504+12
	mov	r3, #0
	mov	r0, #0
	strh	r3, [r2]	@ movhi
	movw	r3, #2328
	ldrh	r1, [r4, r3]
	mov	r3, r1, lsr #3
	add	r3, r3, r1, asl #1
	add	r3, r3, #52
	add	r5, r3, r5, asl #2
	cmp	r5, r7, asl #9
	movcc	r3, #1
	strcch	r3, [r2]	@ movhi
	add	sp, sp, #20
	@ sp needed
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L505:
	.align	2
.L504:
	.word	.LANCHOR0
	.word	.LANCHOR0+2348
	.word	.LANCHOR0+2384
	.word	.LANCHOR0+2432
	.fnend
	.size	FtlConstantsInit, .-FtlConstantsInit
	.align	2
	.global	IsBlkInVendorPart
	.type	IsBlkInVendorPart, %function
IsBlkInVendorPart:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	@ link register save eliminated.
	ldr	r2, .L513
	movw	r3, #2434
	ldrh	r3, [r2, r3]
	cmp	r3, #0
	beq	.L512
	movw	r1, #2408
	ldr	r3, [r2, #2436]
	ldrh	r2, [r2, r1]
	add	r2, r3, r2, asl #1
.L508:
	cmp	r3, r2
	beq	.L512
	ldrh	r1, [r3], #2
	cmp	r1, r0
	bne	.L508
	mov	r0, #1
	bx	lr
.L512:
	mov	r0, #0
	bx	lr
.L514:
	.align	2
.L513:
	.word	.LANCHOR0
	.fnend
	.size	IsBlkInVendorPart, .-IsBlkInVendorPart
	.align	2
	.global	FtlCacheMetchLpa
	.type	FtlCacheMetchLpa, %function
FtlCacheMetchLpa:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	ldr	r2, .L522
	ldr	r3, [r2, #2440]
	cmp	r3, #0
	beq	.L518
	stmfd	sp!, {r4, r5, lr}
	.save {r4, r5, lr}
	mov	r5, #36
	ldr	r4, [r2, #2444]
	mov	r2, #0
.L517:
	mla	ip, r5, r2, r4
	ldr	lr, [ip, #16]
	cmp	lr, r0
	movcc	ip, #0
	movcs	ip, #1
	cmp	lr, r1
	movhi	ip, #0
	cmp	ip, #0
	bne	.L519
	add	r2, r2, #1
	cmp	r2, r3
	bne	.L517
	mov	r0, ip
	ldmfd	sp!, {r4, r5, pc}
.L518:
	mov	r0, r3
	bx	lr
.L519:
	mov	r0, #1
	ldmfd	sp!, {r4, r5, pc}
.L523:
	.align	2
.L522:
	.word	.LANCHOR0
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
	ldr	r3, .L525
	ldr	r0, [r3, #2428]
	bx	lr
.L526:
	.align	2
.L525:
	.word	.LANCHOR0
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
	ldr	r3, .L528
	ldr	r0, [r3, #2428]
	bx	lr
.L529:
	.align	2
.L528:
	.word	.LANCHOR0
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
	ldr	r3, .L531
	ldr	r0, [r3, #2428]
	bx	lr
.L532:
	.align	2
.L531:
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
	ldr	r3, .L534
	ldr	r0, [r3, #2448]
	bx	lr
.L535:
	.align	2
.L534:
	.word	.LANCHOR0
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
	movw	r3, #2386
	ldr	r5, .L538
	mov	r6, r0
	ldrh	r4, [r5, r3]
	mov	r1, r4
	bl	__aeabi_uidiv
	uxth	r2, r0
	smulbb	r3, r2, r4
	add	r1, r5, r2, asl #2
	mov	r4, #1
	ldr	ip, [r1, #2480]
	rsb	r3, r3, r6
	uxth	r3, r3
	and	r1, r3, #31
	mov	lr, r3, lsr #5
	ldr	r0, [ip, lr, asl #2]
	orr	r1, r0, r4, asl r1
	ldr	r0, .L538+4
	str	r1, [ip, lr, asl #2]
	str	r1, [sp]
	mov	r1, r6
	bl	printk
	add	r2, r5, #2448
	mov	r0, #0
	ldrh	r3, [r2, #10]
	add	r3, r3, r4
	strh	r3, [r2, #10]	@ movhi
	add	sp, sp, #8
	@ sp needed
	ldmfd	sp!, {r4, r5, r6, pc}
.L539:
	.align	2
.L538:
	.word	.LANCHOR0
	.word	.LC2
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
	movw	r3, #2386
	ldr	r5, .L542
	mov	r7, r0
	ldrh	r6, [r5, r3]
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
	ldr	r3, [r5, #2480]
	ldr	r0, [r3, r2, asl #2]
	mov	r0, r0, lsr r4
	and	r0, r0, #1
	ldmfd	sp!, {r3, r4, r5, r6, r7, pc}
.L543:
	.align	2
.L542:
	.word	.LANCHOR0
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
	.global	FtlBbtCalcTotleCnt
	.type	FtlBbtCalcTotleCnt, %function
FtlBbtCalcTotleCnt:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	stmfd	sp!, {r4, r5, r6, lr}
	.save {r4, r5, r6, lr}
	movw	r3, #2386
	ldr	r4, .L554
	mov	r5, #0
	ldrh	r6, [r4, r3]
	movw	r3, #2342
	ldrh	r3, [r4, r3]
	mov	r4, r5
	mul	r6, r3, r6
.L546:
	uxth	r0, r5
	cmp	r0, r6
	bge	.L553
	bl	FtlBbmIsBadBlock
	add	r5, r5, #1
	cmp	r0, #0
	addne	r4, r4, #1
	uxthne	r4, r4
	b	.L546
.L553:
	mov	r0, r4
	ldmfd	sp!, {r4, r5, r6, pc}
.L555:
	.align	2
.L554:
	.word	.LANCHOR0
	.fnend
	.size	FtlBbtCalcTotleCnt, .-FtlBbtCalcTotleCnt
	.align	2
	.global	V2P_block
	.type	V2P_block, %function
V2P_block:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	stmfd	sp!, {r3, r4, r5, r6, r7, lr}
	.save {r3, r4, r5, r6, r7, lr}
	movw	r3, #2344
	ldr	r6, .L558
	mov	r4, r1
	mov	r7, r0
	ldrh	r5, [r6, r3]
	mov	r1, r5
	bl	__aeabi_uidivmod
	mov	r0, r7
	smlabb	r4, r4, r5, r1
	mov	r1, r5
	bl	__aeabi_uidiv
	movw	r3, #2386
	ldrh	r3, [r6, r3]
	smlabb	r0, r3, r0, r4
	uxth	r0, r0
	ldmfd	sp!, {r3, r4, r5, r6, r7, pc}
.L559:
	.align	2
.L558:
	.word	.LANCHOR0
	.fnend
	.size	V2P_block, .-V2P_block
	.align	2
	.global	P2V_plane
	.type	P2V_plane, %function
P2V_plane:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	ldr	r3, .L562
	movw	r2, #2344
	stmfd	sp!, {r4, r5, r6, lr}
	.save {r4, r5, r6, lr}
	mov	r6, r0
	ldrh	r5, [r3, r2]
	movw	r2, #2386
	ldrh	r1, [r3, r2]
	bl	__aeabi_uidiv
	mov	r1, r5
	smulbb	r4, r0, r5
	mov	r0, r6
	bl	__aeabi_uidivmod
	add	r1, r4, r1
	uxth	r0, r1
	ldmfd	sp!, {r4, r5, r6, pc}
.L563:
	.align	2
.L562:
	.word	.LANCHOR0
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
	movw	r3, #2386
	ldr	r4, .L566
	ldrh	r1, [r4, r3]
	bl	__aeabi_uidivmod
	movw	r3, #2344
	uxth	r0, r1
	ldrh	r1, [r4, r3]
	bl	__aeabi_uidiv
	uxth	r0, r0
	ldmfd	sp!, {r4, pc}
.L567:
	.align	2
.L566:
	.word	.LANCHOR0
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
	bls	.L569
	rsb	r0, r1, r0
	cmp	r0, #-2147483648
	movhi	r0, #0
	movls	r0, #1
	bx	lr
.L569:
	rsb	r0, r0, r1
	cmp	r0, #-2147483648
	movls	r0, #0
	movhi	r0, #1
	bx	lr
	.fnend
	.size	ftl_cmp_data_ver, .-ftl_cmp_data_ver
	.align	2
	.global	FtlFreeSysBlkQueueEmpty
	.type	FtlFreeSysBlkQueueEmpty, %function
FtlFreeSysBlkQueueEmpty:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	@ link register save eliminated.
	ldr	r3, .L572
	ldrh	r0, [r3, #6]
	clz	r0, r0
	mov	r0, r0, lsr #5
	bx	lr
.L573:
	.align	2
.L572:
	.word	.LANCHOR0+2512
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
	ldr	r3, .L575
	ldrh	r0, [r3, #6]
	sub	r0, r0, #1024
	clz	r0, r0
	mov	r0, r0, lsr #5
	bx	lr
.L576:
	.align	2
.L575:
	.word	.LANCHOR0+2512
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
	ldr	r4, .L587
	ldrh	r3, [r4, #6]
	cmp	r3, #1024
	ldmeqfd	sp!, {r3, r4, r5, r6, r7, pc}
	cmp	r1, #0
	beq	.L579
	ldr	r5, .L587+4
	ldr	r3, [r5, #-3616]
	cmp	r3, #0
	bne	.L579
	bl	P2V_block_in_plane
	mov	r1, #1
	mov	r3, r7, asl #10
	mov	r2, r1
	mov	r6, r0
	ldr	r0, [r5, #-3612]
	str	r3, [r0, #4]
	bl	FlashEraseBlocks
	ldr	r1, [r5, #-3608]
	mov	r3, r6, asl #1
	ldrh	r2, [r1, r3]
	add	r2, r2, #1
	strh	r2, [r1, r3]	@ movhi
	ldr	r3, [r5, #-3604]
	add	r3, r3, #1
	str	r3, [r5, #-3604]
.L579:
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
.L588:
	.align	2
.L587:
	.word	.LANCHOR0+2512
	.word	.LANCHOR2
	.fnend
	.size	FtlFreeSysBlkQueueIn, .-FtlFreeSysBlkQueueIn
	.align	2
	.global	FtlFreeSysBLkSort
	.type	FtlFreeSysBLkSort, %function
FtlFreeSysBLkSort:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	ldr	r3, .L602
	ldrh	ip, [r3, #28]
	ldr	r3, .L602+4
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
.L591:
	uxth	lr, r0
	add	r0, r0, #1
	cmp	lr, ip
	bge	.L601
	add	lr, r3, r1, asl #1
	add	r1, r1, #1
	ubfx	r1, r1, #0, #10
	ldrh	r4, [lr, #8]
	add	lr, r3, r2, asl #1
	strh	r4, [lr, #8]	@ movhi
	mov	r4, #1
	add	r2, r2, r4
	ubfx	r2, r2, #0, #10
	b	.L591
.L601:
	cmp	r4, #0
	strneh	r1, [r3, #2]	@ movhi
	strneh	r2, [r3, #4]	@ movhi
	ldmfd	sp!, {r4, pc}
.L603:
	.align	2
.L602:
	.word	.LANCHOR2-3600
	.word	.LANCHOR0+2512
	.fnend
	.size	FtlFreeSysBLkSort, .-FtlFreeSysBLkSort
	.align	2
	.global	FtlFreeSysBlkQueueOut
	.type	FtlFreeSysBlkQueueOut, %function
FtlFreeSysBlkQueueOut:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	stmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, lr}
	.save {r4, r5, r6, r7, r8, r9, r10, lr}
	ldr	r7, .L615
	add	r4, r7, #2512
	mov	r8, r4
.L605:
	ldrh	r1, [r4, #6]
	cmp	r1, #0
	beq	.L606
	ldr	r5, .L615+4
	sub	r1, r1, #1
	ldrh	r3, [r4, #2]
	strh	r1, [r4, #6]	@ movhi
	ldr	r10, [r5, #-3616]
	add	r2, r4, r3, asl #1
	add	r3, r3, #1
	cmp	r10, #0
	ubfx	r3, r3, #0, #10
	ldrh	r6, [r2, #8]
	strh	r3, [r4, #2]	@ movhi
	bne	.L607
	mov	r0, r6
	bl	P2V_block_in_plane
	mov	r3, r6, asl #10
	mov	r9, r0
	ldr	r0, [r5, #-3612]
	str	r3, [r0, #4]
	ldrb	r3, [r7, #148]	@ zero_extendqisi2
	cmp	r3, #0
	beq	.L608
	mov	r1, r10
	mov	r2, #1
	bl	FlashEraseBlocks
.L608:
	mov	r1, #1
	ldr	r0, [r5, #-3612]
	mov	r2, r1
	bl	FlashEraseBlocks
	ldr	r1, [r5, #-3608]
	mov	r3, r9, asl #1
	ldrh	r2, [r1, r3]
	add	r2, r2, #1
	strh	r2, [r1, r3]	@ movhi
	ldr	r3, [r5, #-3604]
	add	r3, r3, #1
	str	r3, [r5, #-3604]
	b	.L607
.L606:
	ldr	r0, .L615+8
	bl	printk
.L609:
	b	.L609
.L607:
	sub	r3, r6, #1
	movw	r2, #65533
	uxth	r3, r3
	cmp	r3, r2
	bls	.L610
	mov	r1, r6
	ldrh	r2, [r8, #6]
	ldr	r0, .L615+12
	bl	printk
	b	.L605
.L610:
	mov	r0, r6
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, pc}
.L616:
	.align	2
.L615:
	.word	.LANCHOR0
	.word	.LANCHOR2
	.word	.LC3
	.word	.LC4
	.fnend
	.size	FtlFreeSysBlkQueueOut, .-FtlFreeSysBlkQueueOut
	.align	2
	.global	test_node_in_list
	.type	test_node_in_list, %function
test_node_in_list:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	ldr	r3, .L623
	str	lr, [sp, #-4]!
	.save {lr}
	movw	lr, #65535
	ldr	ip, [r3, #-3552]
	ldr	r2, [r0]
	ldr	r3, .L623+4
	rsb	r0, ip, r2
	mov	r0, r0, asr #1
	mul	r3, r3, r0
	mov	r0, #6
	uxth	r3, r3
.L619:
	cmp	r1, r3
	beq	.L620
	ldrh	r3, [r2]
	cmp	r3, lr
	beq	.L621
	mla	r2, r0, r3, ip
	b	.L619
.L620:
	mov	r0, #1
	ldr	pc, [sp], #4
.L621:
	mov	r0, #0
	ldr	pc, [sp], #4
.L624:
	.align	2
.L623:
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
	movw	r3, #2328
	ldr	r5, .L642
	ldrh	r3, [r5, r3]
	cmp	r3, r0
	bls	.L627
	ldr	r2, .L642+4
	mov	lr, #6
	mul	lr, lr, r0
	mvn	ip, #0
	ldr	r6, [r2, #-3552]
	mov	r10, r2
	add	r1, r6, lr
	strh	ip, [r1, #2]	@ movhi
	strh	ip, [r6, lr]	@ movhi
	ldr	r3, [r2, #-3548]
	cmp	r3, #0
	beq	.L641
	ldr	r8, [r2, #-3544]
	mov	r4, r0, asl #1
	ldrh	r2, [r1, #4]
	ldrh	r7, [r8, r4]
	cmp	r2, #0
	mulne	ip, r2, r7
	ldr	r7, [r10, #-3552]
	ldr	r2, .L642+8
	rsb	r9, r7, r3
	mov	r9, r9, asr #1
	mul	r2, r2, r9
	movw	r9, #2328
	ldrh	r5, [r5, r9]
	ldr	r9, [r10, #-3608]
	add	r4, r9, r4
	str	r4, [sp, #4]
	uxth	r2, r2
	mov	r4, #0
	str	r5, [sp]
.L636:
	add	r4, r4, #1
	ldr	r5, [sp]
	uxth	r4, r4
	cmp	r4, r5
	movls	r5, #0
	movhi	r5, #1
	cmp	r0, r2
	orreq	r5, r5, #1
	cmp	r5, #0
	bne	.L627
	mov	r10, r2, asl #1
	ldrh	r5, [r3, #4]
	ldrh	fp, [r8, r10]
	cmp	r5, #0
	mvneq	r5, #0
	mulne	r5, r5, fp
	cmp	r5, ip
	bne	.L632
	ldr	r5, [sp, #4]
	ldrh	r10, [r9, r10]
	ldrh	r5, [r5]
	cmp	r10, r5
	bcc	.L634
	b	.L633
.L632:
	bhi	.L633
.L634:
	ldrh	r5, [r3]
	movw	r10, #65535
	cmp	r5, r10
	streqh	r2, [r1, #2]	@ movhi
	streqh	r0, [r3]	@ movhi
	ldreq	r3, .L642+4
	streq	r1, [r3, #-3540]
	beq	.L627
.L635:
	mov	r3, #6
	mov	r2, r5
	mla	r3, r3, r5, r7
	b	.L636
.L633:
	strh	r2, [r6, lr]	@ movhi
	ldrh	r2, [r3, #2]
	strh	r2, [r1, #2]	@ movhi
	ldr	r2, .L642+4
	ldr	ip, [r2, #-3548]
	cmp	r3, ip
	bne	.L637
	strh	r0, [r3, #2]	@ movhi
.L641:
	str	r1, [r2, #-3548]
	b	.L627
.L637:
	ldrh	ip, [r3, #2]
	ldr	r1, [r2, #-3552]
	mov	r2, #6
	mul	r2, r2, ip
	strh	r0, [r1, r2]	@ movhi
	strh	r0, [r3, #2]	@ movhi
.L627:
	mov	r0, #0
	add	sp, sp, #12
	@ sp needed
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, fp, pc}
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
	.type	INSERT_DATA_LIST, %function
INSERT_DATA_LIST:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	stmfd	sp!, {r3, lr}
	.save {r3, lr}
	bl	insert_data_list
	ldr	r2, .L646
	ldrh	r3, [r2]
	add	r3, r3, #1
	strh	r3, [r2]	@ movhi
	ldmfd	sp!, {r3, pc}
.L647:
	.align	2
.L646:
	.word	.LANCHOR2-3536
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
	beq	.L649
	ldr	r2, .L657
	mov	lr, #6
	mul	r6, lr, r0
	mvn	r3, #0
	ldr	r7, [r2, #-3552]
	mov	r5, r2
	add	ip, r7, r6
	strh	r3, [ip, #2]	@ movhi
	strh	r3, [r7, r6]	@ movhi
	ldr	r3, [r2, #-3532]
	cmp	r3, #0
	beq	.L656
	ldr	r9, [r2, #-3608]
	mov	r2, r0, asl #1
	ldr	r8, [r5, #-3552]
	rsb	r1, r8, r3
	ldrh	r10, [r9, r2]
	ldr	r2, .L657+4
	mov	r1, r1, asr #1
	mul	r1, r2, r1
	uxth	r2, r1
.L653:
	mov	r1, r2, asl #1
	ldrh	r1, [r9, r1]
	cmp	r1, r10
	bcs	.L651
	ldrh	r1, [r3]
	cmp	r1, r4
	streqh	r2, [ip, #2]	@ movhi
	streqh	r0, [r3]	@ movhi
	beq	.L649
.L652:
	mla	r3, lr, r1, r8
	mov	r2, r1
	b	.L653
.L651:
	ldrh	r1, [r3, #2]
	strh	r1, [ip, #2]	@ movhi
	strh	r2, [r7, r6]	@ movhi
	ldr	r1, [r5, #-3532]
	ldr	r2, .L657
	cmp	r3, r1
	bne	.L654
	strh	r0, [r3, #2]	@ movhi
.L656:
	str	ip, [r2, #-3532]
	b	.L649
.L654:
	ldrh	ip, [r3, #2]
	ldr	r1, [r2, #-3552]
	mov	r2, #6
	mul	r2, r2, ip
	strh	r0, [r1, r2]	@ movhi
	strh	r0, [r3, #2]	@ movhi
.L649:
	mov	r0, #0
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, pc}
.L658:
	.align	2
.L657:
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
	ldr	r2, .L661
	ldrh	r3, [r2, #-8]
	add	r3, r3, #1
	strh	r3, [r2, #-8]	@ movhi
	ldmfd	sp!, {r3, pc}
.L662:
	.align	2
.L661:
	.word	.LANCHOR2-3520
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
	ldr	r4, .L669
	movw	r5, #65535
	mul	r1, ip, r1
	ldr	r3, [r0]
	ldr	r2, [r4, #-3552]
	add	lr, r2, r1
	cmp	lr, r3
	ldrh	r3, [r2, r1]
	bne	.L664
	cmp	r3, r5
	mlane	r3, ip, r3, r2
	moveq	r3, #0
	streq	r3, [r0]
	strne	r3, [r0]
	mvnne	r0, #0
	strneh	r0, [r3, #2]	@ movhi
	b	.L666
.L664:
	cmp	r3, r5
	ldrh	r0, [lr, #2]
	bne	.L667
	cmp	r0, r3
	mulne	r0, ip, r0
	mvnne	r3, #0
	strneh	r3, [r2, r0]	@ movhi
	b	.L666
.L667:
	mla	r3, ip, r3, r2
	strh	r0, [r3, #2]	@ movhi
	ldrh	r5, [lr, #2]
	ldrh	r0, [r2, r1]
	ldr	r3, [r4, #-3552]
	mul	ip, ip, r5
	strh	r0, [r3, ip]	@ movhi
.L666:
	mvn	r3, #0
	mov	r0, #0
	strh	r3, [r2, r1]	@ movhi
	strh	r3, [lr, #2]	@ movhi
	ldmfd	sp!, {r4, r5, pc}
.L670:
	.align	2
.L669:
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
	beq	.L677
	ldr	r2, .L680
	movw	ip, #65535
	stmfd	sp!, {r4, lr}
	.save {r4, lr}
	mov	lr, #6
	ldr	r4, [r2, #-3552]
.L673:
	cmp	r1, #0
	bne	.L674
.L676:
	rsb	r4, r4, r3
	ldr	r3, .L680+4
	mov	r4, r4, asr #1
	mul	r4, r3, r4
	uxth	r4, r4
	mov	r1, r4
	bl	List_remove_node
	mov	r0, r4
	ldmfd	sp!, {r4, pc}
.L674:
	ldrh	r2, [r3]
	cmp	r2, ip
	beq	.L676
	sub	r1, r1, #1
	mla	r3, lr, r2, r4
	uxth	r1, r1
	b	.L673
.L677:
	movw	r0, #65535
	bx	lr
.L681:
	.align	2
.L680:
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
	ldr	r2, .L689
	ldr	r3, [r2, #-3548]
	cmp	r3, #0
	ldrne	r1, [r2, #-3552]
	movne	ip, #6
	movwne	r2, #65535
	beq	.L687
.L684:
	cmp	r0, #0
	beq	.L685
	ldrh	r3, [r3]
	cmp	r3, r2
	subne	r0, r0, #1
	mlane	r3, ip, r3, r1
	uxthne	r0, r0
	bne	.L684
.L687:
	movw	r0, #65535
	bx	lr
.L685:
	rsb	r3, r1, r3
	ldr	r0, .L689+4
	mov	r3, r3, asr #1
	mul	r0, r0, r3
	uxth	r0, r0
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
	.type	List_update_data_list, %function
List_update_data_list:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	stmfd	sp!, {r3, r4, r5, lr}
	.save {r3, r4, r5, lr}
	ldr	r3, .L699
	sub	r2, r3, #3520
	ldrh	r2, [r2, #-4]
	cmp	r2, r0
	beq	.L692
	sub	r2, r3, #3472
	ldrh	r2, [r2, #-4]
	cmp	r2, r0
	beq	.L692
	sub	r2, r3, #3424
	ldrh	r2, [r2, #-4]
	cmp	r2, r0
	beq	.L692
	mov	lr, #6
	ldr	r1, [r3, #-3552]
	mul	lr, lr, r0
	ldr	r2, [r3, #-3548]
	add	ip, r1, lr
	cmp	ip, r2
	beq	.L692
	ldr	r4, [r3, #-3544]
	mov	r3, r0, asl #1
	ldrh	r2, [r4, r3]
	ldrh	r3, [ip, #4]
	ldrh	ip, [ip, #2]
	cmp	r3, #0
	mulne	r2, r3, r2
	mvneq	r2, #0
	movw	r3, #65535
	cmp	ip, r3
	bne	.L694
	ldrh	r3, [r1, lr]
	cmp	r3, ip
	beq	.L692
.L694:
	mov	r3, #6
	mul	ip, r3, ip
	ldr	r3, .L699+4
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
	bcs	.L692
	ldr	r5, .L699+8
	mov	r4, r0
	mov	r1, r4
	sub	r0, r5, #12
	bl	List_remove_node
	ldrh	r3, [r5]
	mov	r0, r4
	sub	r3, r3, #1
	strh	r3, [r5]	@ movhi
	bl	INSERT_DATA_LIST
.L692:
	mov	r0, #0
	ldmfd	sp!, {r3, r4, r5, pc}
.L700:
	.align	2
.L699:
	.word	.LANCHOR2
	.word	-1431655765
	.word	.LANCHOR2-3536
	.fnend
	.size	List_update_data_list, .-List_update_data_list
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
	movw	r2, #65533
	sub	r3, r0, #1
	mov	r1, r0
	strh	r0, [r7]	@ movhi
	uxth	r3, r3
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
	add	r3, r3, #1
	str	r3, [r4, #28]
	ldrh	r3, [r4, #8]
	strh	r5, [r4]	@ movhi
	add	r3, r3, #1
	strh	r3, [r4, #8]	@ movhi
.L705:
	mov	r0, #0
	ldmfd	sp!, {r3, r4, r5, r6, r7, pc}
.L710:
	.align	2
.L709:
	.word	.LANCHOR0+2512
	.word	.LC5
	.fnend
	.size	ftl_map_blk_alloc_new_blk, .-ftl_map_blk_alloc_new_blk
	.align	2
	.global	select_l2p_ram_region
	.type	select_l2p_ram_region, %function
select_l2p_ram_region:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	ldr	r2, .L727
	movw	r3, #2426
	stmfd	sp!, {r4, r5, r6, lr}
	.save {r4, r5, r6, lr}
	mov	r1, #0
	ldrh	r2, [r2, r3]
	mov	ip, #12
	ldr	r3, .L727+4
	movw	lr, #65535
	ldr	r3, [r3, #-3380]
.L712:
	uxth	r0, r1
	cmp	r0, r2
	bcs	.L724
	add	r1, r1, #1
	mla	r4, ip, r1, r3
	ldrh	r4, [r4, #-12]
	cmp	r4, lr
	bne	.L712
	ldmfd	sp!, {r4, r5, r6, pc}
.L724:
	mov	r0, r2
	mov	r1, #0
	mov	ip, #-2147483648
	mov	r5, #12
.L715:
	uxth	r4, r1
	cmp	r4, r2
	bcs	.L725
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
	b	.L715
.L725:
	cmp	r0, r2
	ldmccfd	sp!, {r4, r5, r6, pc}
	ldr	r1, .L727+8
	mov	r0, r2
	mvn	ip, #0
	ldrh	r5, [r1]
	mov	r1, #0
.L718:
	uxth	lr, r1
	cmp	lr, r2
	bcs	.L726
	ldr	r4, [r3, #4]
	cmp	r4, ip
	bcs	.L719
	ldrh	r6, [r3]
	cmp	r6, r5
	movne	ip, r4
	movne	r0, lr
.L719:
	add	r1, r1, #1
	add	r3, r3, #12
	b	.L718
.L726:
	ldmfd	sp!, {r4, r5, r6, pc}
.L728:
	.align	2
.L727:
	.word	.LANCHOR0
	.word	.LANCHOR2
	.word	.LANCHOR2-3376
	.fnend
	.size	select_l2p_ram_region, .-select_l2p_ram_region
	.align	2
	.global	FtlUpdateVaildLpn
	.type	FtlUpdateVaildLpn, %function
FtlUpdateVaildLpn:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	ldr	r3, .L737
	sub	r1, r3, #3360
	ldrh	r2, [r1, #-14]
	cmp	r2, #4
	cmpls	r0, #0
	addeq	r2, r2, #1
	streqh	r2, [r1, #-14]	@ movhi
	bxeq	lr
	ldr	r0, .L737+4
	mov	r2, #0
	str	lr, [sp, #-4]!
	.save {lr}
	movw	ip, #65535
	strh	r2, [r1, #-14]	@ movhi
	movw	r1, #2328
	ldrh	r0, [r0, r1]
	str	r2, [r3, #-3372]
	ldr	r2, [r3, #-3544]
	add	r0, r2, r0, asl #1
.L731:
	cmp	r2, r0
	beq	.L736
	ldrh	r1, [r2], #2
	cmp	r1, ip
	ldrne	lr, [r3, #-3372]
	addne	r1, r1, lr
	strne	r1, [r3, #-3372]
	b	.L731
.L736:
	ldr	pc, [sp], #4
.L738:
	.align	2
.L737:
	.word	.LANCHOR2
	.word	.LANCHOR0
	.fnend
	.size	FtlUpdateVaildLpn, .-FtlUpdateVaildLpn
	.align	2
	.global	ftl_set_blk_mode
	.type	ftl_set_blk_mode, %function
ftl_set_blk_mode:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	@ link register save eliminated.
	ldr	r2, .L743
	mov	r3, r0, lsr #5
	cmp	r1, #0
	and	r0, r0, #31
	uxth	r3, r3
	mov	ip, #1
	ldr	r1, [r2, #-3368]
	ldr	r2, [r1, r3, asl #2]
	orrne	r0, r2, ip, asl r0
	biceq	r0, r2, ip, asl r0
	str	r0, [r1, r3, asl #2]
	bx	lr
.L744:
	.align	2
.L743:
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
	ldr	r3, .L746
	mov	r2, r0, lsr #5
	and	r0, r0, #31
	ldr	r3, [r3, #-3368]
	ldr	r3, [r3, r2, asl #2]
	mov	r0, r3, lsr r0
	and	r0, r0, #1
	bx	lr
.L747:
	.align	2
.L746:
	.word	.LANCHOR2
	.fnend
	.size	ftl_get_blk_mode, .-ftl_get_blk_mode
	.align	2
	.global	ftl_sb_update_avl_pages
	.type	ftl_sb_update_avl_pages, %function
ftl_sb_update_avl_pages:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	mov	r3, #0
	strh	r3, [r0, #4]	@ movhi
	ldr	r3, .L758
	stmfd	sp!, {r4, r5, lr}
	.save {r4, r5, lr}
	movw	r4, #65535
	ldrh	lr, [r3]
	add	r3, r2, #7
	add	r3, r0, r3, asl #1
.L749:
	cmp	r2, lr
	bcs	.L756
	ldrh	ip, [r3, #2]!
	add	r2, r2, #1
	cmp	ip, r4
	uxth	r2, r2
	ldrneh	ip, [r0, #4]
	addne	ip, ip, #1
	strneh	ip, [r0, #4]	@ movhi
	b	.L749
.L756:
	ldr	r2, .L758+4
	movw	r3, #2388
	add	ip, r0, #14
	movw	r5, #65535
	ldrh	r4, [r2, r3]
	mov	r2, #0
.L752:
	uxth	r3, r2
	cmp	r3, lr
	bcs	.L757
	ldrh	r3, [ip, #2]!
	add	r2, r2, #1
	cmp	r3, r5
	ldrneh	r3, [r0, #4]
	addne	r3, r4, r3
	subne	r3, r3, #1
	rsbne	r3, r1, r3
	strneh	r3, [r0, #4]	@ movhi
	b	.L752
.L757:
	ldmfd	sp!, {r4, r5, pc}
.L759:
	.align	2
.L758:
	.word	.LANCHOR0+2320
	.word	.LANCHOR0
	.fnend
	.size	ftl_sb_update_avl_pages, .-ftl_sb_update_avl_pages
	.align	2
	.global	make_superblock
	.type	make_superblock, %function
make_superblock:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	stmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, lr}
	.save {r4, r5, r6, r7, r8, r9, r10, lr}
	add	r6, r0, #16
	ldr	r7, .L774
	mov	r4, r0
	mvn	r9, #0
	mov	r5, #0
	strh	r5, [r0, #4]	@ movhi
	ldrh	r8, [r7], #28
	strb	r5, [r0, #7]
.L761:
	uxth	r3, r5
	cmp	r3, r8
	bcs	.L773
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
	b	.L761
.L773:
	ldr	r3, .L774+4
	movw	r2, #2388
	ldrb	r1, [r4, #7]	@ zero_extendqisi2
	ldrh	r2, [r3, r2]
	smulbb	r2, r1, r2
	strh	r2, [r4, #4]	@ movhi
	mov	r2, #0
	strb	r2, [r4, #9]
	ldr	r2, [r3, #2244]
	cmp	r2, #0
	beq	.L764
	ldr	r1, .L774+8
	ldrh	r2, [r4]
	ldr	r1, [r1, #-3608]
	mov	r2, r2, asl #1
	ldrh	r2, [r1, r2]
	cmp	r2, #79
	movls	r2, #1
	strlsb	r2, [r4, #9]
.L764:
	ldrb	r3, [r3, #32]	@ zero_extendqisi2
	mov	r0, #0
	cmp	r3, #0
	movne	r3, #1
	strneb	r3, [r4, #9]
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, pc}
.L775:
	.align	2
.L774:
	.word	.LANCHOR0+2320
	.word	.LANCHOR0
	.word	.LANCHOR2
	.fnend
	.size	make_superblock, .-make_superblock
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
	ldr	r3, .L786
	mov	r6, r0
	ldr	r9, .L786+4
	mov	r4, r5
	add	r2, r3, #2320
	ldrh	r7, [r2]
	movw	r2, #2388
	ldrh	r8, [r3, r2]
.L777:
	uxth	r3, r5
	cmp	r3, r7
	bcs	.L785
	ldrb	r0, [r9, r5]	@ zero_extendqisi2
	mov	r1, r6
	bl	V2P_block
	add	r5, r5, #1
	bl	FtlBbmIsBadBlock
	cmp	r0, #0
	addeq	r4, r4, r8
	uxtheq	r4, r4
	b	.L777
.L785:
	cmp	r4, #0
	beq	.L780
	mov	r1, r4
	mov	r0, #32768
	bl	__aeabi_idiv
	uxth	r4, r0
.L780:
	ldr	r3, .L786+8
	mov	r2, #6
	mov	r0, #0
	ldr	r3, [r3, #-3552]
	mla	r6, r2, r6, r3
	strh	r4, [r6, #4]	@ movhi
	ldmfd	sp!, {r3, r4, r5, r6, r7, r8, r9, pc}
.L787:
	.align	2
.L786:
	.word	.LANCHOR0
	.word	.LANCHOR0+2348
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
	ldr	r2, .L791
	ldr	r0, [r2, #-3532]
	cmp	r0, #0
	bxeq	lr
	ldr	r3, [r2, #-3552]
	rsb	r0, r3, r0
	ldr	r3, .L791+4
	mov	r0, r0, asr #1
	mul	r0, r3, r0
	ldr	r3, [r2, #-3608]
	uxth	r0, r0
	mov	r0, r0, asl #1
	ldrh	r0, [r3, r0]
	bx	lr
.L792:
	.align	2
.L791:
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
	ldr	r1, .L803
	ldr	r3, [r1, #-3532]
	cmp	r3, #0
	beq	.L799
	sub	r2, r1, #3520
	stmfd	sp!, {r4, r5, lr}
	.save {r4, r5, lr}
	mov	r4, #6
	ldrh	r2, [r2, #-8]
	movw	r5, #65535
	ldr	ip, [r1, #-3552]
	rsb	r2, r2, r2, asl #3
	rsb	r3, ip, r3
	mov	r2, r2, asr #3
	mov	r3, r3, asr #1
	cmp	r0, r2
	uxthgt	r0, r2
	ldr	r2, .L803+4
	mul	r3, r2, r3
	mov	r2, #0
	uxth	r3, r3
.L796:
	uxth	lr, r2
	cmp	lr, r0
	bcs	.L798
	mul	lr, r4, r3
	add	r2, r2, #1
	ldrh	lr, [ip, lr]
	cmp	lr, r5
	bne	.L800
.L798:
	ldr	r2, [r1, #-3608]
	mov	r3, r3, asl #1
	ldrh	r0, [r2, r3]
	ldmfd	sp!, {r4, r5, pc}
.L800:
	mov	r3, lr
	b	.L796
.L799:
	mov	r0, r3
	bx	lr
.L804:
	.align	2
.L803:
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
	ldr	r8, .L816
	add	r4, r7, #12
	ldr	r1, .L816+4
	.pad #32
	sub	sp, sp, #32
	bl	strcpy
	mov	r0, r4
	ldr	r1, .L816+8
	ldr	r2, [r8, #120]
	bl	sprintf
	ldr	r1, .L816+12
	ldr	r2, [r8, #2404]
	add	r4, r4, r0
	mov	r0, r4
	bl	sprintf
	ldr	r3, .L816+16
	ldr	r3, [r3, #504]
	cmp	r3, #1
	add	r4, r4, r0
	bne	.L811
	add	r0, sp, #16
	add	r1, sp, #20
	add	r2, sp, #24
	add	r3, sp, #28
	bl	NandcGetTimeCfg
	mov	r0, r4
	ldr	r1, .L816+20
	ldr	r6, .L816+24
	sub	r10, r6, #3520
	ldr	r3, [sp, #24]
	ldr	r2, [sp, #16]
	str	r3, [sp]
	ldr	r3, [sp, #28]
	str	r3, [sp, #4]
	ldr	r3, [sp, #20]
	bl	sprintf
	ldr	r1, .L816+28
	add	r4, r4, r0
	add	r5, r4, #10
	mov	r0, r4
	sub	r4, r6, #3600
	bl	strcpy
	mov	r0, r5
	ldr	r1, .L816+32
	ldr	r2, [r8, #2448]
	bl	sprintf
	ldr	r1, .L816+36
	ldr	r2, [r6, #-3372]
	add	r5, r5, r0
	mov	r0, r5
	bl	sprintf
	ldr	r1, .L816+40
	ldr	r2, [r6, #-3364]
	add	r5, r5, r0
	mov	r0, r5
	bl	sprintf
	ldr	r1, .L816+44
	ldr	r2, [r6, #-3360]
	add	r5, r5, r0
	mov	r0, r5
	bl	sprintf
	ldr	r1, .L816+48
	ldr	r2, [r6, #-3356]
	add	r5, r5, r0
	mov	r0, r5
	bl	sprintf
	ldr	r1, .L816+52
	ldr	r2, [r6, #-3352]
	add	r5, r5, r0
	mov	r0, r5
	bl	sprintf
	ldr	r1, .L816+56
	ldr	r2, [r6, #-3348]
	add	r5, r5, r0
	mov	r0, r5
	bl	sprintf
	ldr	r1, .L816+60
	ldr	r2, [r6, #-3344]
	add	r5, r5, r0
	mov	r0, r5
	bl	sprintf
	ldr	r2, [r6, #-3340]
	ldr	r1, .L816+64
	mov	r2, r2, lsr #11
	add	r5, r5, r0
	mov	r0, r5
	bl	sprintf
	ldr	r2, [r6, #-3336]
	ldr	r1, .L816+68
	mov	r2, r2, lsr #11
	add	r5, r5, r0
	mov	r0, r5
	bl	sprintf
	ldr	r1, .L816+72
	ldr	r2, [r6, #-3332]
	add	r5, r5, r0
	mov	r0, r5
	bl	sprintf
	ldr	r1, .L816+76
	ldr	r2, [r6, #-3328]
	add	r5, r5, r0
	mov	r0, r5
	bl	sprintf
	add	r5, r5, r0
	bl	FtlBbtCalcTotleCnt
	ldr	r2, .L816+80
	ldr	r1, .L816+84
	ldrh	r2, [r2, #6]
	mov	r3, r0
	mov	r0, r5
	bl	sprintf
	ldr	r1, .L816+88
	ldrh	r2, [r10, #-8]
	add	r5, r5, r0
	mov	r0, r5
	bl	sprintf
	ldr	r1, .L816+92
	ldr	r2, [r6, #-3324]
	add	r5, r5, r0
	mov	r0, r5
	bl	sprintf
	ldr	r1, .L816+96
	ldr	r2, [r6, #-3320]
	add	r5, r5, r0
	mov	r0, r5
	bl	sprintf
	ldr	r1, .L816+100
	ldr	r2, [r6, #-3316]
	add	r5, r5, r0
	mov	r0, r5
	bl	sprintf
	ldr	r1, .L816+104
	ldr	r2, [r6, #-3604]
	add	r5, r5, r0
	mov	r0, r5
	bl	sprintf
	ldr	r1, .L816+108
	ldr	r2, [r6, #-3312]
	add	r5, r5, r0
	mov	r0, r5
	bl	sprintf
	ldr	r1, .L816+112
	ldr	r2, [r6, #-3308]
	add	r5, r5, r0
	mov	r0, r5
	bl	sprintf
	ldrh	r2, [r4, #30]
	ldr	r1, .L816+116
	add	r5, r5, r0
	mov	r0, r5
	bl	sprintf
	ldrh	r2, [r4, #28]
	ldr	r1, .L816+120
	sub	r4, r6, #3296
	add	r5, r5, r0
	mov	r0, r5
	bl	sprintf
	ldr	r1, .L816+124
	ldr	r2, [r8, #2428]
	add	r5, r5, r0
	mov	r0, r5
	bl	sprintf
	ldr	r1, .L816+128
	ldr	r2, [r8, #2420]
	add	r5, r5, r0
	mov	r0, r5
	bl	sprintf
	ldr	r1, .L816+132
	ldr	r2, [r8, #2316]
	add	r5, r5, r0
	mov	r0, r5
	bl	sprintf
	add	r3, r8, #2512
	ldr	r1, .L816+136
	ldrh	r2, [r3, #6]
	add	r5, r5, r0
	mov	r0, r5
	bl	sprintf
	movw	r3, #2328
	ldrh	r2, [r8, r3]
	ldr	r1, .L816+140
	add	r5, r5, r0
	mov	r0, r5
	bl	sprintf
	ldrh	r2, [r4, #-8]
	ldr	r1, .L816+144
	add	r5, r5, r0
	mov	r0, r5
	bl	sprintf
	ldr	r1, .L816+148
	ldr	r2, [r8, #2332]
	add	r5, r5, r0
	mov	r0, r5
	bl	sprintf
	ldrh	r2, [r4, #-4]
	ldr	r1, .L816+152
	add	r5, r5, r0
	mov	r0, r5
	bl	sprintf
	movw	r3, #2452
	ldrh	r2, [r8, r3]
	ldr	r1, .L816+156
	add	r5, r5, r0
	mov	r0, r5
	bl	sprintf
	ldrh	r2, [r4, #-226]
	ldr	r1, .L816+160
	add	r5, r5, r0
	mov	r0, r5
	bl	sprintf
	ldr	r1, .L816+164
	ldrb	r2, [r6, #-3518]	@ zero_extendqisi2
	add	r5, r5, r0
	mov	r0, r5
	bl	sprintf
	ldr	r1, .L816+168
	ldrh	r2, [r10, #-4]
	add	r5, r5, r0
	mov	r0, r5
	bl	sprintf
	ldr	r1, .L816+172
	ldrb	r2, [r6, #-3516]	@ zero_extendqisi2
	add	r5, r5, r0
	mov	r0, r5
	bl	sprintf
	ldrh	r2, [r4, #-224]
	ldr	r1, .L816+176
	sub	r4, r6, #3472
	sub	r9, r4, #4
	add	r5, r5, r0
	mov	r0, r5
	bl	sprintf
	ldrh	r3, [r10, #-4]
	ldr	r2, [r6, #-3544]
	ldr	r1, .L816+180
	mov	r3, r3, asl #1
	ldrh	r2, [r2, r3]
	add	r5, r5, r0
	mov	r0, r5
	bl	sprintf
	ldrh	r2, [r4, #-2]
	ldr	r1, .L816+184
	add	r5, r5, r0
	mov	r0, r5
	bl	sprintf
	ldr	r1, .L816+188
	ldrb	r2, [r6, #-3470]	@ zero_extendqisi2
	add	r5, r5, r0
	mov	r0, r5
	bl	sprintf
	ldrh	r2, [r4, #-4]
	ldr	r1, .L816+192
	add	r5, r5, r0
	mov	r0, r5
	bl	sprintf
	ldr	r1, .L816+196
	ldrb	r2, [r6, #-3468]	@ zero_extendqisi2
	add	r5, r5, r0
	mov	r0, r5
	bl	sprintf
	ldrh	r2, [r9, #4]
	ldr	r1, .L816+200
	sub	r9, r6, #3424
	add	r5, r5, r0
	mov	r0, r5
	bl	sprintf
	ldrh	r3, [r4, #-4]
	ldr	r2, [r6, #-3544]
	ldr	r1, .L816+204
	mov	r3, r3, asl #1
	ldrh	r2, [r2, r3]
	add	r5, r5, r0
	mov	r0, r5
	bl	sprintf
	ldrh	r2, [r4, #46]
	ldr	r1, .L816+208
	add	r5, r5, r0
	mov	r0, r5
	bl	sprintf
	ldr	r1, .L816+212
	ldrb	r2, [r6, #-3422]	@ zero_extendqisi2
	add	r5, r5, r0
	mov	r0, r5
	bl	sprintf
	ldrh	r2, [r9, #-4]
	ldr	r1, .L816+216
	sub	r9, r6, #3280
	add	r5, r5, r0
	mov	r0, r5
	bl	sprintf
	ldr	r1, .L816+220
	ldrb	r2, [r6, #-3420]	@ zero_extendqisi2
	add	r5, r5, r0
	mov	r0, r5
	bl	sprintf
	ldrh	r2, [r4, #48]
	ldr	r1, .L816+224
	add	r5, r5, r0
	mov	r0, r5
	bl	sprintf
	ldrh	r2, [r4, #190]
	ldr	r1, .L816+228
	add	r5, r5, r0
	mov	r0, r5
	bl	sprintf
	ldr	r1, .L816+232
	ldrb	r2, [r6, #-3278]	@ zero_extendqisi2
	add	r5, r5, r0
	mov	r0, r5
	bl	sprintf
	ldr	r1, .L816+236
	ldrh	r2, [r9, #-4]
	add	r5, r5, r0
	mov	r0, r5
	bl	sprintf
	ldr	r1, .L816+240
	ldrb	r2, [r6, #-3276]	@ zero_extendqisi2
	add	r5, r5, r0
	mov	r0, r5
	bl	sprintf
	ldrh	r2, [r4, #192]
	ldr	r1, .L816+244
	add	r5, r5, r0
	mov	r0, r5
	bl	sprintf
	ldr	r1, [r6, #-3148]
	ldr	r3, [r8, #2244]
	ldr	r2, [r6, #-3236]
	orr	r2, r3, r2, asl #8
	ldr	r3, [r6, #-3152]
	str	r1, [sp]
	add	r5, r5, r0
	ldr	r1, [r6, #-3156]
	mov	r0, r5
	str	r1, [sp, #4]
	ldr	r1, .L816+248
	bl	sprintf
	ldr	r1, .L816+252
	ldr	r2, [r6, #-3160]
	add	r4, r5, r0
	sub	r5, r6, #2704
	mov	r0, r4
	bl	sprintf
	ldr	r1, .L816+256
	ldr	r2, [r6, #-3136]
	add	r4, r4, r0
	mov	r0, r4
	bl	sprintf
	sub	r3, r6, #2720
	ldr	r1, .L816+260
	ldrh	r2, [r3]
	add	r4, r4, r0
	mov	r0, r4
	bl	sprintf
	ldr	r1, .L816+264
	ldrh	r2, [r5, #-14]
	add	r4, r4, r0
	mov	r0, r4
	bl	sprintf
	ldr	r1, .L816+268
	ldr	r2, [r6, #-2716]
	add	r4, r4, r0
	mov	r0, r4
	bl	sprintf
	ldr	r1, .L816+272
	ldrh	r2, [r5, #-8]
	add	r4, r4, r0
	mov	r0, r4
	bl	sprintf
	add	r4, r4, r0
	bl	GetFreeBlockMinEraseCount
	ldr	r1, .L816+276
	mov	r2, r0
	mov	r0, r4
	bl	sprintf
	add	r4, r4, r0
	ldrh	r0, [r10, #-8]
	bl	GetFreeBlockMaxEraseCount
	ldr	r1, .L816+280
	mov	r2, r0
	mov	r0, r4
	bl	sprintf
	ldrh	r3, [r9, #-4]
	movw	r2, #65535
	cmp	r3, r2
	add	r4, r4, r0
	beq	.L808
	ldr	r2, [r6, #-3544]
	mov	r3, r3, asl #1
	mov	r0, r4
	ldr	r1, .L816+284
	ldrh	r2, [r2, r3]
	bl	sprintf
	add	r4, r4, r0
.L808:
	mov	r0, #0
	mov	r5, #0
	bl	List_get_gc_head_node
	movw	r10, #65535
	mov	r9, #6
	uxth	r3, r0
.L810:
	cmp	r3, r10
	beq	.L809
	ldr	r2, [r6, #-3544]
	mov	r1, r3, asl #1
	mul	r8, r9, r3
	mov	r0, r4
	ldrh	r2, [r2, r1]
	str	r2, [sp]
	ldr	r2, [r6, #-3552]
	add	r2, r2, r8
	ldrh	r2, [r2, #4]
	str	r2, [sp, #4]
	ldr	r2, [r6, #-3608]
	ldrh	r2, [r2, r1]
	ldr	r1, .L816+288
	str	r2, [sp, #8]
	mov	r2, r5
	bl	sprintf
	add	r5, r5, #1
	ldr	r3, [r6, #-3552]
	cmp	r5, #16
	ldrh	r3, [r3, r8]
	add	r4, r4, r0
	bne	.L810
.L809:
	ldr	r2, [r6, #-3552]
	mov	r5, #0
	ldr	r3, [r6, #-3532]
	movw	r10, #65535
	mov	r9, #6
	rsb	r3, r2, r3
	ldr	r2, .L816+292
	mov	r3, r3, asr #1
	mul	r3, r2, r3
	uxth	r3, r3
.L812:
	cmp	r3, r10
	beq	.L811
	mul	r8, r9, r3
	ldr	r2, [r6, #-3552]
	ldr	r1, [r6, #-3608]
	mov	r0, r4
	add	r2, r2, r8
	ldrh	r2, [r2, #4]
	str	r2, [sp]
	mov	r2, r3, asl #1
	ldrh	r2, [r1, r2]
	ldr	r1, .L816+296
	str	r2, [sp, #4]
	mov	r2, r5
	bl	sprintf
	add	r5, r5, #1
	ldr	r3, [r6, #-3552]
	cmp	r5, #4
	ldrh	r3, [r3, r8]
	add	r4, r4, r0
	bne	.L812
.L811:
	rsb	r0, r7, r4
	add	sp, sp, #32
	@ sp needed
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, pc}
.L817:
	.align	2
.L816:
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
	.word	.LANCHOR0+2452
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
	ldr	r1, .L820
	ldr	r2, .L820+4
	bl	sprintf
	add	r4, r5, r0
	mov	r0, r4
	bl	FtlPrintInfo2buf
	add	r0, r4, r0
	rsb	r0, r5, r0
	ldmfd	sp!, {r3, r4, r5, pc}
.L821:
	.align	2
.L820:
	.word	.LC76
	.word	.LC77
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
	ldr	r4, .L852
	ldr	r2, [r4, #-3316]
	ldr	r3, [r4, #-3308]
	cmp	r2, r3
	bcs	.L823
	ldr	r2, .L852+4
	movw	r1, #2328
	ldr	r0, [r4, #-3608]
	mov	r3, #0
	str	r3, [r4, #-3324]
	ldrh	r1, [r2, r1]
	sub	r0, r0, #2
	mov	r5, r2
.L824:
	cmp	r3, r1
	bcs	.L851
	ldrh	r2, [r0, #2]!
	add	r3, r3, #1
	ldr	ip, [r4, #-3324]
	add	r2, r2, ip
	str	r2, [r4, #-3324]
	b	.L824
.L851:
	ldr	r6, [r4, #-3324]
	mov	r0, r6
	bl	__aeabi_uidiv
	movw	r3, #2380
	ldrh	r1, [r5, r3]
	str	r0, [r4, #-3316]
	ldr	r0, [r4, #-3320]
	rsb	r0, r0, r6
	bl	__aeabi_uidiv
	str	r0, [r4, #-3324]
	b	.L826
.L823:
	ldr	r3, [r4, #-3312]
	cmp	r2, r3
	bls	.L826
	ldr	ip, .L852+8
	add	r3, r3, #1
	str	r3, [r4, #-3312]
	mov	r3, #0
.L828:
	ldrh	r2, [ip]
	cmp	r3, r2
	bcs	.L826
	ldr	r0, [r4, #-3608]
	mov	r1, r3, asl #1
	add	r3, r3, #1
	ldrh	r2, [r0, r1]
	add	r2, r2, #1
	strh	r2, [r0, r1]	@ movhi
	b	.L828
.L826:
	ldr	r6, [r4, #-3308]
	ldr	r8, [r4, #-3316]
	add	r3, r6, #256
	cmp	r3, r8
	bls	.L831
	ldr	r2, .L852
	add	r3, r6, #768
	ldr	r2, [r2, #-3312]
	cmp	r3, r2
	bls	.L831
	ldr	r3, .L852+4
	cmp	r6, #40
	ldr	r2, [r3, #2244]
	movls	r3, #0
	movhi	r3, #1
	cmp	r2, #0
	orreq	r3, r3, #1
	cmp	r3, #0
	beq	.L831
.L833:
	movw	r0, #65535
	b	.L832
.L831:
	ldr	r5, .L852
	sub	r3, r5, #3520
	ldrh	r0, [r3, #-8]
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
	bne	.L833
	ldr	r3, [r5, #-3548]
	cmp	r3, #0
	beq	.L833
	ldr	r0, .L852+4
	movw	r2, #2328
	ldr	ip, .L852+12
	movw	r7, #65535
	ldr	r10, [r5, #-3608]
	mov	lr, #6
	ldrh	r2, [r0, r2]
	ldr	r0, [r5, #-3552]
	mov	r5, r7
	str	r2, [sp, #20]
.L834:
	ldrh	r2, [r3]
	movw	fp, #65535
	cmp	r2, fp
	beq	.L836
	add	r1, r1, #1
	ldr	fp, [sp, #20]
	uxth	r1, r1
	cmp	r1, fp
	bhi	.L833
	ldrh	fp, [r3, #4]
	cmp	fp, #0
	beq	.L835
	rsb	r3, r0, r3
	mov	r3, r3, asr #1
	mul	r3, ip, r3
	uxth	r3, r3
	mov	fp, r3, asl #1
	ldrh	fp, [r10, fp]
	cmp	fp, r6
	bls	.L840
	cmp	fp, r7
	movcc	r7, fp
	movcc	r5, r3
.L835:
	mla	r3, lr, r2, r0
	b	.L834
.L840:
	mov	r5, r3
.L836:
	movw	r3, #65535
	cmp	r5, r3
	beq	.L833
	mov	r3, r5, asl #1
	ldrh	fp, [r10, r3]
	cmp	fp, r6
	bls	.L838
	str	r3, [sp, #20]
	bl	GetFreeBlockMinEraseCount
	ldr	r3, [sp, #20]
	cmp	r0, r6
	strhi	r7, [r4, #-3308]
.L838:
	cmp	fp, r8
	bcs	.L833
	add	r2, fp, #128
	cmp	r9, r2
	ble	.L833
	add	r2, fp, #256
	cmp	r2, r8
	bcc	.L839
	ldr	r2, [r4, #-3312]
	add	fp, fp, #768
	cmp	fp, r2
	bcs	.L833
.L839:
	ldr	r2, [r4, #-3544]
	mov	r1, r5
	ldr	r0, .L852+16
	ldrh	r2, [r2, r3]
	str	r2, [sp]
	mov	r2, r8
	ldrh	r3, [r10, r3]
	stmib	sp, {r3, r9}
	ldr	r3, [r4, #-3312]
	bl	printk
	mov	r0, r5
	mov	r3, #1
	str	r3, [r4, #-2708]
.L832:
	add	sp, sp, #28
	@ sp needed
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L853:
	.align	2
.L852:
	.word	.LANCHOR2
	.word	.LANCHOR0
	.word	.LANCHOR0+2328
	.word	-1431655765
	.word	.LC78
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
	beq	.L855
	ldr	r2, .L857
	mov	r3, r0, asl #1
	mov	r1, #0
	ldr	r2, [r2, #-3544]
	strh	r1, [r2, r3]	@ movhi
	bl	INSERT_FREE_LIST
.L855:
	mov	r0, #0
	ldmfd	sp!, {r3, pc}
.L858:
	.align	2
.L857:
	.word	.LANCHOR2
	.fnend
	.size	free_data_superblock, .-free_data_superblock
	.align	2
	.global	FtlGcBufInit
	.type	FtlGcBufInit, %function
FtlGcBufInit:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	ldr	ip, .L867
	mov	r3, #0
	ldr	r1, .L867+4
	stmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, lr}
	.save {r4, r5, r6, r7, r8, r9, r10, lr}
	add	r7, ip, #78
	mov	r5, #12
	mov	r6, #1
	mov	r8, #36
	str	r3, [r1, #-2704]
.L860:
	ldrh	r2, [ip]
	add	r4, r3, #1
	uxth	r3, r3
	cmp	r3, r2
	bcs	.L865
	mul	r0, r5, r3
	ldr	lr, [r1, #-2700]
	add	r2, lr, r0
	str	r6, [r2, #8]
	ldrh	r2, [r7]
	mul	r2, r2, r3
	add	r9, r2, #3
	cmp	r2, #0
	movlt	r2, r9
	ldr	r9, [r1, #-2696]
	bic	r2, r2, #3
	add	r2, r9, r2
	str	r2, [lr, r0]
	ldr	r2, .L867+8
	ldr	r9, [r1, #-2700]
	ldrh	r2, [r2]
	add	lr, r9, r0
	mul	r2, r2, r3
	add	r10, r2, #3
	cmp	r2, #0
	movlt	r2, r10
	ldr	r10, [r1, #-2692]
	bic	r2, r2, #3
	add	r2, r10, r2
	str	r2, [lr, #4]
	ldr	r2, [r1, #-2688]
	mla	r3, r8, r3, r2
	ldr	r2, [r9, r0]
	str	r2, [r3, #8]
	ldr	r2, [lr, #4]
	str	r2, [r3, #12]
	mov	r3, r4
	b	.L860
.L865:
	ldr	r0, .L867+4
	mov	ip, #12
	ldr	r4, .L867+12
	mov	lr, #0
.L862:
	ldr	r3, [r1, #-2684]
	cmp	r2, r3
	bcs	.L866
	mul	r5, ip, r2
	ldr	r7, [r0, #-2700]
	add	r3, r7, r5
	str	lr, [r3, #8]
	ldrh	r3, [r4]
	mul	r3, r3, r2
	add	r6, r3, #3
	cmp	r3, #0
	movlt	r3, r6
	ldr	r6, [r0, #-2696]
	bic	r3, r3, #3
	add	r3, r6, r3
	str	r3, [r7, r5]
	ldr	r3, .L867+8
	ldr	r6, [r0, #-2700]
	ldrh	r3, [r3]
	add	r5, r6, r5
	mul	r3, r3, r2
	add	r2, r2, #1
	uxth	r2, r2
	add	r6, r3, #3
	cmp	r3, #0
	movlt	r3, r6
	ldr	r6, [r0, #-2692]
	bic	r3, r3, #3
	add	r3, r6, r3
	str	r3, [r5, #4]
	b	.L862
.L866:
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, pc}
.L868:
	.align	2
.L867:
	.word	.LANCHOR0+2320
	.word	.LANCHOR2
	.word	.LANCHOR0+2400
	.word	.LANCHOR0+2398
	.fnend
	.size	FtlGcBufInit, .-FtlGcBufInit
	.align	2
	.global	FtlGcBufFree
	.type	FtlGcBufFree, %function
FtlGcBufFree:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	ldr	r3, .L877
	stmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, lr}
	.save {r4, r5, r6, r7, r8, r9, r10, lr}
	mov	lr, #0
	ldr	r6, [r3, #-2684]
	mov	r5, #36
	ldr	r4, [r3, #-2700]
	mov	r7, lr
	mov	r8, #12
.L870:
	uxth	ip, lr
	cmp	ip, r1
	ldmcsfd	sp!, {r4, r5, r6, r7, r8, r9, r10, pc}
	mla	ip, r5, ip, r0
	mov	r2, #0
.L871:
	uxth	r3, r2
	cmp	r3, r6
	bcs	.L872
	mul	r3, r8, r3
	add	r2, r2, #1
	add	r9, r4, r3
	ldr	r10, [r4, r3]
	ldr	r3, [ip, #8]
	cmp	r10, r3
	bne	.L871
	str	r7, [r9, #8]
.L872:
	add	lr, lr, #1
	b	.L870
.L878:
	.align	2
.L877:
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
	ldr	r3, .L888
	mov	ip, #0
	stmfd	sp!, {r4, r5, r6, r7, r8, r9, lr}
	.save {r4, r5, r6, r7, r8, r9, lr}
	mov	r6, #12
	ldr	r4, [r3, #-2684]
	mov	r7, #1
	ldr	r5, [r3, #-2700]
	mov	r8, #36
.L880:
	uxth	r2, ip
	cmp	r2, r1
	bcs	.L887
	mov	lr, #0
.L881:
	uxth	r3, lr
	cmp	r3, r4
	bcs	.L882
	mla	r3, r6, r3, r5
	add	lr, lr, #1
	ldr	r9, [r3, #8]
	cmp	r9, #0
	bne	.L881
	mla	r2, r8, r2, r0
	ldr	lr, [r3]
	str	r7, [r3, #8]
	str	lr, [r2, #8]
	ldr	r3, [r3, #4]
	str	r3, [r2, #12]
.L882:
	add	ip, ip, #1
	b	.L880
.L887:
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, pc}
.L889:
	.align	2
.L888:
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
	ldr	r2, .L896
	sub	r2, r2, #2672
	ldr	r3, [r2, #-8]
	ldrh	r2, [r2, #-4]
	add	r2, r3, r2, asl #1
.L891:
	cmp	r3, r2
	beq	.L895
	ldrh	r1, [r3], #2
	cmp	r1, r0
	bne	.L891
	mov	r0, #1
	bx	lr
.L895:
	mov	r0, #0
	bx	lr
.L897:
	.align	2
.L896:
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
	ldr	r2, .L906
	mov	lr, #0
	sub	r3, r2, #2672
	ldr	r8, [r2, #-2680]
	ldrh	r1, [r3, #-4]
	sub	r7, r8, #2
.L899:
	uxth	ip, lr
	cmp	ip, r1
	bcs	.L903
	ldrh	r9, [r7, #2]!
	add	lr, lr, #1
	cmp	r9, r0
	bne	.L899
.L903:
	cmp	ip, r1
	moveq	ip, ip, asl #1
	streqh	r0, [r8, ip]	@ movhi
	ldreqh	ip, [r3, #-4]
	addeq	ip, ip, #1
	streqh	ip, [r3, #-4]	@ movhi
.L901:
	ldr	r3, .L906+4
	mov	ip, #12
	ldr	r2, [r2, #-2672]
	ldrh	r1, [r3, #-12]
	mul	ip, ip, r1
	add	r1, r2, ip
	stmib	r1, {r5, r6}
	str	r4, [r2, ip]
	ldrh	r2, [r3, #-12]
	add	r2, r2, #1
	strh	r2, [r3, #-12]	@ movhi
	ldmfd	sp!, {r3, r4, r5, r6, r7, r8, r9, pc}
.L907:
	.align	2
.L906:
	.word	.LANCHOR2
	.word	.LANCHOR2-2656
	.fnend
	.size	FtlGcUpdatePage, .-FtlGcUpdatePage
	.align	2
	.global	FtlGcRefreshOpenBlock
	.type	FtlGcRefreshOpenBlock, %function
FtlGcRefreshOpenBlock:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	stmfd	sp!, {r3, r4, r5, lr}
	.save {r3, r4, r5, lr}
	mov	r5, r0
	ldr	r4, .L916
	ldrh	r3, [r4, #-10]
	cmp	r3, r0
	beq	.L910
	ldrh	r3, [r4, #-8]
	cmp	r3, r0
	beq	.L910
	ldrh	r3, [r4, #-6]
	cmp	r3, r0
	beq	.L910
	ldrh	r3, [r4, #-4]
	cmp	r3, r0
	beq	.L910
	ldr	r0, .L916+4
	mov	r1, r5
	bl	printk
	ldrh	r2, [r4, #-10]
	movw	r3, #65535
	cmp	r2, r3
	streqh	r5, [r4, #-10]	@ movhi
	beq	.L910
	ldrh	r2, [r4, #-8]
	cmp	r2, r3
	streqh	r5, [r4, #-8]	@ movhi
	beq	.L910
	ldrh	r2, [r4, #-6]
	cmp	r2, r3
	streqh	r5, [r4, #-6]	@ movhi
	beq	.L910
	ldrh	r2, [r4, #-4]
	cmp	r2, r3
	streqh	r5, [r4, #-4]	@ movhi
.L910:
	mov	r0, #0
	ldmfd	sp!, {r3, r4, r5, pc}
.L917:
	.align	2
.L916:
	.word	.LANCHOR2-2656
	.word	.LC79
	.fnend
	.size	FtlGcRefreshOpenBlock, .-FtlGcRefreshOpenBlock
	.align	2
	.global	FtlGcRefreshBlock
	.type	FtlGcRefreshBlock, %function
FtlGcRefreshBlock:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	stmfd	sp!, {r3, r4, r5, lr}
	.save {r3, r4, r5, lr}
	mov	r5, r0
	ldr	r4, .L929
	ldrh	r3, [r4, #-10]
	cmp	r3, r0
	beq	.L926
	ldrh	r3, [r4, #-8]
	cmp	r3, r0
	beq	.L926
	ldrh	r3, [r4, #-6]
	cmp	r3, r0
	beq	.L926
	ldrh	r3, [r4, #-4]
	cmp	r3, r0
	beq	.L926
	ldr	r0, .L929+4
	mov	r1, r5
	bl	printk
	ldrh	r2, [r4, #-10]
	movw	r3, #65535
	cmp	r2, r3
	streqh	r5, [r4, #-10]	@ movhi
	beq	.L926
	ldrh	r2, [r4, #-8]
	cmp	r2, r3
	streqh	r5, [r4, #-8]	@ movhi
	beq	.L926
	ldrh	r2, [r4, #-6]
	cmp	r2, r3
	streqh	r5, [r4, #-6]	@ movhi
	beq	.L926
	ldrh	r2, [r4, #-4]
	cmp	r2, r3
	bne	.L927
	strh	r5, [r4, #-4]	@ movhi
.L926:
	mov	r0, #0
	ldmfd	sp!, {r3, r4, r5, pc}
.L927:
	mvn	r0, #0
	ldmfd	sp!, {r3, r4, r5, pc}
.L930:
	.align	2
.L929:
	.word	.LANCHOR2-2656
	.word	.LC79
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
	ldr	r6, .L941
	mov	r2, r5
	sub	r7, r6, #2656
	ldrh	r1, [r7, #-2]
	mov	r4, r0
	ldr	r0, .L941+4
	bl	printk
	mov	r0, r4
	bl	FtlGcRefreshBlock
	ldr	r3, .L941+8
	ldr	r3, [r3, #2244]
	cmp	r3, #0
	mov	r3, r7
	beq	.L932
	ldr	r1, [r6, #-3608]
	mov	r4, r4, asl #1
	ldrh	r2, [r1, r4]
	cmp	r2, #39
	subhi	r2, r2, #40
	strhih	r2, [r1, r4]	@ movhi
.L932:
	ldrh	r2, [r3, #-2]
	mov	r1, #0
.L933:
	uxth	r0, r1
	cmp	r0, r2
	bcs	.L940
	add	r1, r1, #1
	add	r0, r3, r1, asl #1
	ldrh	r0, [r0, #-2]
	cmp	r0, r5
	bne	.L933
	b	.L934
.L940:
	cmp	r2, #15
	addls	r1, r2, #1
	movls	r2, r2, asl #1
	strlsh	r1, [r3, #-2]	@ movhi
	strlsh	r5, [r3, r2]	@ movhi
.L934:
	mov	r0, #0
	ldmfd	sp!, {r3, r4, r5, r6, r7, pc}
.L942:
	.align	2
.L941:
	.word	.LANCHOR2
	.word	.LC80
	.word	.LANCHOR0
	.fnend
	.size	FtlGcMarkBadPhyBlk, .-FtlGcMarkBadPhyBlk
	.align	2
	.global	FtlGcReFreshBadBlk
	.type	FtlGcReFreshBadBlk, %function
FtlGcReFreshBadBlk:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	ldr	r3, .L951
	ldrh	r2, [r3, #-2]
	cmp	r2, #0
	beq	.L950
	ldrh	r0, [r3, #-10]
	movw	r1, #65535
	cmp	r0, r1
	bne	.L950
	stmfd	sp!, {r4, lr}
	.save {r4, lr}
	add	r4, r3, #48
	ldrh	r1, [r4, #-14]
	cmp	r1, r2
	movcs	r2, #0
	strcsh	r2, [r4, #-14]	@ movhi
	ldrh	r2, [r4, #-14]
	mov	r2, r2, asl #1
	ldrh	r0, [r3, r2]
	bl	P2V_block_in_plane
	bl	FtlGcRefreshBlock
	ldrh	r3, [r4, #-14]
	mov	r0, #0
	add	r3, r3, #1
	strh	r3, [r4, #-14]	@ movhi
	ldmfd	sp!, {r4, pc}
.L950:
	mov	r0, #0
	bx	lr
.L952:
	.align	2
.L951:
	.word	.LANCHOR2-2656
	.fnend
	.size	FtlGcReFreshBadBlk, .-FtlGcReFreshBadBlk
	.align	2
	.global	ftl_memset
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
	.type	BuildFlashLsbPageTable, %function
BuildFlashLsbPageTable:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	cmp	r0, #0
	stmfd	sp!, {r4, lr}
	.save {r4, lr}
	mov	r4, r1
	bne	.L955
	ldr	r3, .L998
.L956:
	mov	r2, r0, asl #1
	strh	r0, [r2, r3]	@ movhi
	add	r0, r0, #1
	cmp	r0, #512
	bne	.L956
.L960:
	mov	r1, #255
	ldr	r0, .L998+4
	mov	r2, #2048
	uxth	r4, r4
	bl	ftl_memset
	mov	r3, #0
	ldr	r1, .L998
	ldr	r0, .L998+4
	b	.L957
.L955:
	cmp	r0, #1
	bne	.L958
	ldr	ip, .L998
	mov	r3, #0
.L959:
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
	bne	.L959
	b	.L960
.L958:
	cmp	r0, #2
	bne	.L961
	ldr	r1, .L998
	mov	r3, #0
.L962:
	uxth	r2, r3
	mov	r0, r3, asl #1
	cmp	r2, #1
	add	r3, r3, #1
	mov	ip, r2, asl #1
	subhi	r2, ip, #1
	cmp	r3, #512
	strh	r2, [r0, r1]	@ movhi
	bne	.L962
	b	.L960
.L961:
	cmp	r0, #3
	bne	.L963
	ldr	ip, .L998
	mov	r3, #0
.L964:
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
	bne	.L964
	b	.L960
.L963:
	cmp	r0, #4
	mov	r3, #0
	bne	.L965
	ldr	r2, .L998+8
	strh	r3, [r2, #152]	@ movhi
	mov	r3, #1
	strh	r0, [r2, #160]	@ movhi
	strh	r3, [r2, #154]	@ movhi
	mov	r3, #2
	strh	r3, [r2, #156]	@ movhi
	mov	r3, #3
	strh	r3, [r2, #158]	@ movhi
	mov	r3, #5
	strh	r3, [r2, #162]	@ movhi
	mov	r3, #7
	strh	r3, [r2, #164]	@ movhi
	mov	r3, #8
	strh	r3, [r2, #166]!	@ movhi
.L966:
	tst	r3, #1
	movne	r1, #7
	moveq	r1, #6
	rsb	r1, r1, r3, asl #1
	add	r3, r3, #1
	strh	r1, [r2, #2]!	@ movhi
	uxth	r3, r3
	cmp	r3, #512
	bne	.L966
	b	.L960
.L965:
	cmp	r0, #5
	bne	.L967
	ldr	r2, .L998
.L968:
	mov	r1, r3, asl #1
	strh	r3, [r1, r2]	@ movhi
	add	r3, r3, #1
	cmp	r3, #16
	bne	.L968
	ldr	r2, .L998+12
.L969:
	strh	r3, [r2, #2]!	@ movhi
	add	r3, r3, #2
	uxth	r3, r3
	cmp	r3, #1008
	bne	.L969
	b	.L960
.L967:
	cmp	r0, #6
	bne	.L970
	ldr	r1, .L998+16
.L971:
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
	bne	.L971
	b	.L960
.L970:
	cmp	r0, #9
	bne	.L972
	ldr	r2, .L998+8
	movw	r1, #1021
	strh	r3, [r2, #152]	@ movhi
	mov	r3, #1
	strh	r3, [r2, #154]	@ movhi
	mov	r3, #2
	strh	r3, [r2, #156]!	@ movhi
	mov	r3, #3
.L973:
	strh	r3, [r2, #2]!	@ movhi
	add	r3, r3, #2
	uxth	r3, r3
	cmp	r3, r1
	bne	.L973
	b	.L960
.L972:
	cmp	r0, #10
	bne	.L974
	ldr	r2, .L998
.L975:
	mov	r1, r3, asl #1
	strh	r3, [r1, r2]	@ movhi
	add	r3, r3, #1
	cmp	r3, #63
	bne	.L975
	ldr	r2, .L998+20
	movw	r1, #961
.L976:
	strh	r3, [r2, #2]!	@ movhi
	add	r3, r3, #2
	uxth	r3, r3
	cmp	r3, r1
	bne	.L976
	b	.L960
.L974:
	cmp	r0, #11
	bne	.L977
	ldr	r2, .L998
	mov	r3, #0
.L978:
	mov	r1, r3, asl #1
	strh	r3, [r1, r2]	@ movhi
	add	r3, r3, #1
	cmp	r3, #8
	bne	.L978
	ldr	r1, .L998+24
.L979:
	tst	r3, #1
	movne	r2, #7
	moveq	r2, #6
	rsb	r2, r2, r3, asl #1
	add	r3, r3, #1
	strh	r2, [r1, #2]!	@ movhi
	uxth	r3, r3
	cmp	r3, #512
	bne	.L979
	b	.L960
.L977:
	cmp	r0, #12
	bne	.L960
	ldr	r3, .L998+8
	mov	r2, #0
	strh	r2, [r3, #152]	@ movhi
	mov	r2, #1
	strh	r2, [r3, #154]	@ movhi
	mov	r2, #2
	strh	r2, [r3, #156]	@ movhi
	mov	r2, #3
	strh	r2, [r3, #158]!	@ movhi
	mov	r2, #4
.L980:
	sub	r1, r2, #1
	add	r1, r1, r2, lsr #1
	add	r2, r2, #1
	strh	r1, [r3, #2]!	@ movhi
	uxth	r2, r2
	cmp	r2, #512
	bne	.L980
	b	.L960
.L957:
	uxth	r2, r3
	cmp	r2, r4
	bcs	.L997
	mov	r2, r3, asl #1
	add	r3, r3, #1
	ldrh	r2, [r2, r1]
	mov	ip, r2, asl #1
	strh	r2, [r0, ip]	@ movhi
	b	.L957
.L997:
	ldmfd	sp!, {r4, pc}
.L999:
	.align	2
.L998:
	.word	.LANCHOR0+152
	.word	.LANCHOR2-2620
	.word	.LANCHOR0
	.word	.LANCHOR0+182
	.word	.LANCHOR0+150
	.word	.LANCHOR0+276
	.word	.LANCHOR0+166
	.fnend
	.size	BuildFlashLsbPageTable, .-BuildFlashLsbPageTable
	.align	2
	.global	FlashDieInfoInit
	.type	FlashDieInfoInit, %function
FlashDieInfoInit:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	stmfd	sp!, {r3, r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.save {r3, r4, r5, r6, r7, r8, r9, r10, fp, lr}
	mov	r6, #0
	ldr	r3, .L1015
	ldr	r5, .L1015+4
	ldr	r9, .L1015+8
	ldrh	r0, [r3, #10]
	strb	r6, [r5, #2230]
	mov	r7, r5
	strb	r6, [r9, #-572]
	bl	FlashBlockAlignInit
	mov	r1, r6
	mov	r2, #8
	ldr	r0, .L1015+12
	bl	ftl_memset
	mov	r1, r6
	mov	r2, #32
	ldr	r0, .L1015+16
	bl	ftl_memset
	ldr	r0, .L1015+20
	mov	r1, r6
	mov	r2, #128
	bl	ftl_memset
	ldr	r4, [r5, #44]
	ldr	fp, .L1015+24
	add	r8, r4, #1
.L1002:
	mov	r0, r8
	add	r1, fp, r6, asl #3
	ldrb	r2, [r4]	@ zero_extendqisi2
	bl	FlashMemCmp8
	ldr	r10, .L1015+24
	cmp	r0, #0
	bne	.L1001
	ldrb	r3, [r7, #2230]	@ zero_extendqisi2
	add	r2, r7, r3, asl #2
	str	r0, [r2, #1176]
	add	r2, r3, #1
	add	r3, r7, r3
	strb	r2, [r7, #2230]
	strb	r6, [r3, #2232]
.L1001:
	add	r6, r6, #1
	cmp	r6, #4
	bne	.L1002
	ldrb	r3, [r5, #2230]	@ zero_extendqisi2
	ldr	r7, .L1015+4
	strb	r3, [r9, #-572]
	ldrb	r3, [r4, #8]	@ zero_extendqisi2
	cmp	r3, #2
	beq	.L1003
.L1007:
	ldrb	r3, [r4, #13]	@ zero_extendqisi2
	ldrb	r2, [r5, #2230]	@ zero_extendqisi2
	smulbb	r2, r2, r3
	ldrh	r3, [r4, #14]
	smulbb	r3, r2, r3
	ldr	r2, .L1015+28
	strh	r3, [r2, #-2]	@ movhi
	ldmfd	sp!, {r3, r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L1003:
	ldr	r9, [r7, #36]
	mov	r6, #0
.L1006:
	mov	r0, r8
	add	r1, r10, r6, asl #3
	ldrb	r2, [r4]	@ zero_extendqisi2
	bl	FlashMemCmp8
	cmp	r0, #0
	bne	.L1004
	ldrb	r1, [r4, #13]	@ zero_extendqisi2
	ldrh	r3, [r4, #14]
	ldrb	r2, [r7, #2230]	@ zero_extendqisi2
	mul	r1, r9, r1
	and	r3, r3, #65280
	add	r0, r7, r2, asl #2
	mul	r3, r3, r1
	str	r3, [r0, #1176]
	ldrb	r1, [r4, #23]	@ zero_extendqisi2
	cmp	r1, #0
	movne	r3, r3, asl #1
	strne	r3, [r0, #1176]
	add	r3, r2, #1
	add	r2, r5, r2
	strb	r3, [r5, #2230]
	strb	r6, [r2, #2232]
.L1004:
	add	r6, r6, #1
	cmp	r6, #4
	bne	.L1006
	b	.L1007
.L1016:
	.align	2
.L1015:
	.word	.LANCHOR1+472
	.word	.LANCHOR0
	.word	.LANCHOR2
	.word	.LANCHOR0+2232
	.word	.LANCHOR0+1176
	.word	.LANCHOR0+2100
	.word	.LANCHOR0+2068
	.word	.LANCHOR2-568
	.fnend
	.size	FlashDieInfoInit, .-FlashDieInfoInit
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
	ldr	r2, .L1022
	ldr	r0, .L1022+4
	mov	ip, #1
	ldr	r3, [r2, #44]
	ldrb	r1, [r3, #9]	@ zero_extendqisi2
	ldr	r3, [r2, #36]
	smulbb	r3, r1, r3
	strh	r3, [r4, #4]	@ unaligned
	ldrb	r3, [r2, #2312]	@ zero_extendqisi2
	strb	r3, [r4, #7]
	ldr	r3, [r2, #2428]
	str	r3, [r4]	@ unaligned
	ldr	r3, [r2, #44]
	ldrb	r1, [r3, #9]	@ zero_extendqisi2
	strb	r1, [r4, #6]
	mov	r1, #32
	strb	r1, [r4, #8]
	ldrb	r1, [r2, #2230]	@ zero_extendqisi2
	ldrb	r3, [r3, #7]	@ zero_extendqisi2
	strb	r3, [r4, #9]
	mov	r3, #0
	strb	r3, [r4, #10]
.L1018:
	uxtb	r2, r3
	cmp	r2, r1
	bcs	.L1021
	ldrb	lr, [r3, r0]	@ zero_extendqisi2
	add	r3, r3, #1
	ldrb	r2, [r4, #10]	@ zero_extendqisi2
	orr	r2, r2, ip, asl lr
	strb	r2, [r4, #10]
	b	.L1018
.L1021:
	ldmfd	sp!, {r4, pc}
.L1023:
	.align	2
.L1022:
	.word	.LANCHOR0
	.word	.LANCHOR0+2232
	.fnend
	.size	ftl_read_flash_info, .-ftl_read_flash_info
	.align	2
	.global	FtlMemInit
	.type	FtlMemInit, %function
FtlMemInit:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	stmfd	sp!, {r3, r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.save {r3, r4, r5, r6, r7, r8, r9, r10, fp, lr}
	mov	r6, #0
	ldr	r4, .L1129
	mvn	r2, #0
	ldr	r5, .L1129+4
	mov	r1, #32
	sub	r3, r4, #568
	mov	r0, #1024
	str	r6, [r4, #-564]
	add	r7, r5, #2320
	strh	r6, [r3]	@ movhi
	movw	r3, #65535
	str	r3, [r4, #-556]
	sub	r3, r4, #2656
	str	r6, [r4, #-3236]
	mov	r8, #36
	strh	r2, [r3, #-10]	@ movhi
	strh	r2, [r3, #-8]	@ movhi
	strh	r2, [r3, #-6]	@ movhi
	strh	r2, [r3, #-4]	@ movhi
	sub	r2, r4, #2720
	strh	r6, [r3, #-2]	@ movhi
	sub	r3, r4, #544
	strh	r1, [r2]	@ movhi
	sub	r2, r4, #2704
	strh	r6, [r3]	@ movhi
	sub	r3, r4, #2608
	strh	r6, [r2, #-8]	@ movhi
	mov	r1, #128
	strh	r6, [r3, #-14]	@ movhi
	movw	r3, #2394
	strh	r1, [r2, #-14]	@ movhi
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
	str	r6, [r4, #-3604]
	str	r6, [r4, #-3312]
	str	r6, [r4, #-3308]
	str	r6, [r4, #-560]
	str	r6, [r4, #-2708]
	str	r6, [r4, #-552]
	str	r6, [r4, #-2716]
	str	r6, [r4, #-548]
	ldrh	r1, [r5, r3]
	bl	__aeabi_idiv
	ldrh	r10, [r7]
	str	r6, [r5, #2440]
	movw	r6, #2392
	mov	r9, r10, asl #2
	cmp	r0, r9
	str	r0, [r4, #-540]
	ldrh	r0, [r5, r6]
	strhi	r9, [r4, #-540]
	mov	r9, #12
	mov	r0, r0, asl #1
	bl	ftl_malloc
	str	r0, [r4, #-2680]
	ldrh	r0, [r5, r6]
	mul	r0, r9, r0
	bl	ftl_malloc
	ldrh	r6, [r7]
	mul	r6, r8, r6
	mov	r10, r6, asl #3
	str	r0, [r4, #-2672]
	mov	r0, r10
	bl	ftl_malloc
	str	r0, [r4, #-536]
	mov	r0, r6
	bl	ftl_malloc
	str	r0, [r4, #-532]
	mov	r0, r10
	bl	ftl_malloc
	movw	r10, #2398
	str	r0, [r4, #-528]
	mov	r0, r6
	bl	ftl_malloc
	str	r0, [r4, #-3612]
	mov	r0, r6
	bl	ftl_malloc
	str	r0, [r4, #-2688]
	ldr	r0, [r4, #-540]
	mul	r0, r8, r0
	bl	ftl_malloc
	ldrh	r6, [r5, r10]
	ldrh	r3, [r7]
	movw	r8, #2330
	mov	r3, r3, asl #1
	add	r3, r3, #1
	str	r3, [r4, #-2684]
	str	r0, [r5, #2444]
	mov	r0, r6
	bl	ftl_malloc
	str	r0, [r4, #-524]
	mov	r0, r6
	bl	ftl_malloc
	str	r0, [r4, #-520]
	mov	r0, r6
	bl	ftl_malloc
	str	r0, [r4, #-516]
	ldr	r0, [r4, #-2684]
	mul	r0, r0, r6
	bl	ftl_malloc
	str	r0, [r4, #-2696]
	ldr	r0, [r4, #-540]
	mul	r0, r0, r6
	bl	ftl_malloc
	str	r0, [r4, #-512]
	mov	r0, r6
	bl	ftl_malloc
	str	r0, [r4, #-508]
	mov	r0, r6
	bl	ftl_malloc
	ldr	r6, .L1129+8
	str	r0, [r4, #-504]
	ldr	r0, [r4, #-2684]
	mul	r0, r9, r0
	bl	ftl_malloc
	ldrh	r3, [r6]
	ldrh	r7, [r7]
	mul	r7, r7, r3
	str	r0, [r4, #-2700]
	mov	r0, r7
	bl	ftl_malloc
	str	r0, [r4, #-500]
	mov	r0, r7, asl #3
	bl	ftl_malloc
	ldrh	r3, [r6]
	ldr	r7, .L1129+12
	str	r0, [r4, #-496]
	ldr	r0, [r4, #-2684]
	mul	r0, r0, r3
	bl	ftl_malloc
	ldrh	r3, [r6], #80
	str	r0, [r4, #-2692]
	ldr	r0, [r4, #-540]
	mul	r0, r0, r3
	bl	ftl_malloc
	str	r0, [r4, #-492]
	ldrh	r0, [r5, r8]
	mov	r0, r0, asl #1
	uxth	r0, r0
	strh	r0, [r7]	@ movhi
	bl	ftl_malloc
	str	r0, [r4, #-484]
	ldrh	r0, [r7]
	add	r0, r0, #544
	add	r0, r0, #3
	mov	r0, r0, lsr #9
	strh	r0, [r7]	@ movhi
	mov	r0, r0, asl #9
	bl	ftl_malloc
	ldrh	fp, [r5, r8]
	mov	fp, fp, asl #1
	str	r0, [r4, #-480]
	add	r0, r0, #32
	str	r0, [r4, #-3608]
	mov	r0, fp
	bl	ftl_malloc
	str	r0, [r4, #-476]
	mov	r0, fp
	bl	ftl_malloc
	ldr	fp, [r5, #2416]
	mov	fp, fp, asl #1
	str	r0, [r4, #-3544]
	mov	r0, fp
	bl	ftl_malloc
	str	r0, [r4, #-472]
	mov	r0, fp
	bl	ftl_malloc
	movw	fp, #2408
	str	r0, [r4, #-468]
	ldrh	r0, [r5, r8]
	mov	r0, r0, lsr #3
	add	r0, r0, #4
	bl	ftl_malloc
	str	r0, [r4, #-3368]
	ldrh	r0, [r5, fp]
	mov	r0, r0, asl #1
	bl	ftl_malloc
	str	r0, [r5, #2436]
	ldrh	r0, [r5, fp]
	mov	r0, r0, asl #1
	bl	ftl_malloc
	str	r0, [r4, #-464]
	ldrh	r0, [r5, fp]
	movw	fp, #2410
	mov	r0, r0, asl #2
	bl	ftl_malloc
	str	r0, [r4, #-460]
	ldrh	r0, [r5, fp]
	mov	r0, r0, asl #2
	bl	ftl_malloc
	ldrh	r2, [r5, fp]
	mov	r1, #0
	mov	r2, r2, asl #2
	str	r0, [r4, #-456]
	bl	ftl_memset
	movw	r3, #2424
	ldrh	fp, [r5, r3]
	mov	fp, fp, asl #2
	mov	r0, fp
	bl	ftl_malloc
	str	r0, [r4, #-452]
	mov	r0, fp
	bl	ftl_malloc
	movw	fp, #2426
	str	r0, [r4, #-448]
	ldr	r0, [r5, #2416]
	mov	r0, r0, asl #2
	bl	ftl_malloc
	str	r0, [r4, #-444]
	ldrh	r0, [r5, fp]
	mul	r0, r9, r0
	bl	ftl_malloc
	ldrh	r3, [r5, fp]
	str	r0, [r4, #-3380]
	ldrh	r0, [r5, r10]
	mul	r0, r0, r3
	bl	ftl_malloc
	ldrh	r3, [r5, r8]
	movw	r8, #2342
	str	r0, [r4, #-440]
	mov	r0, #6
	mul	r0, r0, r3
	bl	ftl_malloc
	movw	r3, #2386
	ldrh	r3, [r5, r3]
	add	r3, r3, #31
	mov	r3, r3, asr #5
	strh	r3, [r7, #52]!	@ movhi
	str	r0, [r4, #-3552]
	ldrh	r0, [r5, r8]
	mul	r0, r0, r3
	mov	r0, r0, asl #2
	bl	ftl_malloc
	ldrh	r2, [r7]
	ldrh	ip, [r5, r8]
	mov	r3, #1
	mov	r2, r2, asl #2
	mov	r1, r2
	str	r0, [r5, #2480]
.L1026:
	cmp	r3, ip
	bcs	.L1127
	ldr	r0, [r5, #2480]
	add	r3, r3, #1
	add	r0, r0, r1
	add	r1, r1, r2
	str	r0, [r6, #4]!
	b	.L1026
.L1127:
	ldr	r2, .L1129+16
	mov	r1, #0
.L1028:
	cmp	r3, #8
	addne	r0, r2, r3, asl #2
	addne	r3, r3, #1
	strne	r1, [r0, #28]
	bne	.L1028
.L1128:
	ldr	r2, [r4, #-472]
	ldr	r3, .L1129
	cmp	r2, #0
	bne	.L1030
.L1032:
	ldr	r1, .L1129+20
	ldr	r0, .L1129+24
	bl	printk
	mvn	r0, #0
	ldmfd	sp!, {r3, r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L1030:
	ldr	r2, [r3, #-468]
	cmp	r2, #0
	beq	.L1032
	ldr	r2, [r3, #-452]
	cmp	r2, #0
	beq	.L1032
	ldr	r2, [r3, #-444]
	cmp	r2, #0
	beq	.L1032
	ldr	r2, [r3, #-3380]
	cmp	r2, #0
	beq	.L1032
	ldr	r2, [r3, #-440]
	cmp	r2, #0
	beq	.L1032
	ldr	r2, [r3, #-3552]
	cmp	r2, #0
	beq	.L1032
	ldr	r2, [r5, #2480]
	cmp	r2, #0
	beq	.L1032
	ldr	r3, [r3, #-3544]
	cmp	r3, #0
	beq	.L1032
	ldr	r2, [r4, #-2680]
	ldr	r3, .L1129
	cmp	r2, #0
	beq	.L1032
	ldr	r2, [r3, #-2672]
	cmp	r2, #0
	beq	.L1032
	ldr	r2, [r3, #-536]
	cmp	r2, #0
	beq	.L1032
	ldr	r2, [r3, #-528]
	cmp	r2, #0
	beq	.L1032
	ldr	r2, [r3, #-3612]
	cmp	r2, #0
	beq	.L1032
	ldr	r2, [r3, #-2688]
	cmp	r2, #0
	beq	.L1032
	ldr	r2, [r3, #-532]
	cmp	r2, #0
	beq	.L1032
	ldr	r2, [r3, #-524]
	cmp	r2, #0
	beq	.L1032
	ldr	r2, [r3, #-520]
	cmp	r2, #0
	beq	.L1032
	ldr	r3, [r3, #-516]
	cmp	r3, #0
	beq	.L1032
	ldr	r2, [r4, #-2696]
	ldr	r3, .L1129
	cmp	r2, #0
	beq	.L1032
	ldr	r2, [r3, #-508]
	cmp	r2, #0
	beq	.L1032
	ldr	r2, [r3, #-504]
	cmp	r2, #0
	beq	.L1032
	ldr	r2, [r3, #-2700]
	cmp	r2, #0
	beq	.L1032
	ldr	r2, [r3, #-500]
	cmp	r2, #0
	beq	.L1032
	ldr	r2, [r3, #-496]
	cmp	r2, #0
	beq	.L1032
	ldr	r2, [r3, #-2692]
	cmp	r2, #0
	beq	.L1032
	ldr	r2, [r3, #-3608]
	cmp	r2, #0
	beq	.L1032
	ldr	r3, [r3, #-484]
	cmp	r3, #0
	beq	.L1032
	ldr	r3, .L1129+4
	ldr	r3, [r3, #2436]
	cmp	r3, #0
	beq	.L1032
	ldr	r3, .L1129
	ldr	r2, [r3, #-464]
	cmp	r2, #0
	beq	.L1032
	ldr	r2, [r3, #-460]
	cmp	r2, #0
	beq	.L1032
	ldr	r3, [r3, #-456]
	cmp	r3, #0
	beq	.L1032
	mov	r0, #0
	ldmfd	sp!, {r3, r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L1130:
	.align	2
.L1129:
	.word	.LANCHOR2
	.word	.LANCHOR0
	.word	.LANCHOR0+2400
	.word	.LANCHOR2-488
	.word	.LANCHOR0+2452
	.word	.LANCHOR3
	.word	.LC81
	.fnend
	.size	FtlMemInit, .-FtlMemInit
	.align	2
	.global	FtlBbt2Bitmap
	.type	FtlBbt2Bitmap, %function
FtlBbt2Bitmap:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	ldr	r3, .L1137
	stmfd	sp!, {r4, r5, r6, lr}
	.save {r4, r5, r6, lr}
	mov	r4, r0
	ldrh	r2, [r3]
	mov	r0, r1
	mov	r5, r1
	mov	r1, #0
	movw	r6, #65535
	mov	r2, r2, asl #2
	bl	ftl_memset
	add	r3, r4, #1020
	ldr	ip, .L1137+4
	add	r3, r3, #2
	sub	r1, r4, #2
	mov	r4, #1
.L1133:
	ldrh	r2, [r1, #2]!
	cmp	r2, r6
	ldmeqfd	sp!, {r4, r5, r6, pc}
	mov	lr, r2, lsr #5
	and	r2, r2, #31
	cmp	r1, r3
	ldr	r0, [r5, lr, asl #2]
	orr	r2, r0, r4, asl r2
	str	r2, [r5, lr, asl #2]
	ldrh	r2, [ip, #6]
	add	r2, r2, #1
	strh	r2, [ip, #6]	@ movhi
	bne	.L1133
	ldmfd	sp!, {r4, r5, r6, pc}
.L1138:
	.align	2
.L1137:
	.word	.LANCHOR2-436
	.word	.LANCHOR0+2452
	.fnend
	.size	FtlBbt2Bitmap, .-FtlBbt2Bitmap
	.align	2
	.global	FtlBbtMemInit
	.type	FtlBbtMemInit, %function
FtlBbtMemInit:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	@ link register save eliminated.
	ldr	r0, .L1140
	movw	r3, #2452
	mvn	r1, #0
	add	r2, r0, r3
	strh	r1, [r0, r3]	@ movhi
	mov	r3, #0
	add	r0, r0, #2464
	strh	r3, [r2, #6]	@ movhi
	mov	r1, #255
	mov	r2, #16
	b	ftl_memset
.L1141:
	.align	2
.L1140:
	.word	.LANCHOR0
	.fnend
	.size	FtlBbtMemInit, .-FtlBbtMemInit
	.align	2
	.global	FtlFreeSysBlkQueueInit
	.type	FtlFreeSysBlkQueueInit, %function
FtlFreeSysBlkQueueInit:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	ldr	r3, .L1144
	mov	r2, #2048
	stmfd	sp!, {r4, lr}
	.save {r4, lr}
	mov	r4, #0
	mov	r1, r4
	strh	r4, [r3, #2]	@ movhi
	strh	r4, [r3, #4]	@ movhi
	strh	r4, [r3, #6]	@ movhi
	strh	r0, [r3], #8	@ movhi
	mov	r0, r3
	bl	ftl_memset
	mov	r0, r4
	ldmfd	sp!, {r4, pc}
.L1145:
	.align	2
.L1144:
	.word	.LANCHOR0+2512
	.fnend
	.size	FtlFreeSysBlkQueueInit, .-FtlFreeSysBlkQueueInit
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
.L1147:
	ldrh	r1, [r4, #6]
	uxth	r3, r2
	cmp	r1, r3
	bls	.L1167
	ldr	r0, [r6, r3, asl #2]
	mov	r1, #0
	ubfx	r0, r0, #10, #16
.L1148:
	ldrh	ip, [r4, #10]
	uxth	r3, r1
	cmp	ip, r3
	bls	.L1168
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
	b	.L1148
.L1168:
	add	r2, r2, #1
	b	.L1147
.L1167:
	ldr	r2, .L1170
	movw	r3, #2390
	mov	r8, #0
	mov	r1, r8
	mov	fp, r8
	ldrh	r2, [r2, r3]
	ldrh	r3, [r4]
	mov	r3, r3, asl #1
	strh	r2, [r5, r3]	@ movhi
	ldrh	r9, [r5]
.L1152:
	ldrh	r3, [r4, #10]
	uxth	r6, r8
	cmp	r3, r6
	bls	.L1169
	mov	r2, r6, asl #1
	ldrh	r3, [r5, r2]
	cmp	r9, r3
	bls	.L1153
	ldrh	r0, [r7, r2]
	add	r10, r7, r2
	cmp	r0, #0
	bne	.L1154
	b	.L1155
.L1153:
	cmp	r3, #0
	bne	.L1155
	ldrh	r0, [r7, r2]
	add	r10, r7, r2
	cmp	r0, #0
	movne	r6, r1
	beq	.L1155
	b	.L1157
.L1154:
	cmp	r3, #0
	movne	r1, r6
	movne	r9, r3
	bne	.L1155
	mov	r9, r3
.L1157:
	mov	r1, #1
	bl	FtlFreeSysBlkQueueIn
	strh	fp, [r10]	@ movhi
	ldrh	r3, [r4, #8]
	mov	r1, r6
	sub	r3, r3, #1
	strh	r3, [r4, #8]	@ movhi
.L1155:
	add	r8, r8, #1
	b	.L1152
.L1169:
	mov	r0, r1
	ldmfd	sp!, {r3, r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L1171:
	.align	2
.L1170:
	.word	.LANCHOR0
	.fnend
	.size	ftl_free_no_use_map_blk, .-ftl_free_no_use_map_blk
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
	ldr	r5, .L1177
	mvn	r6, #0
	ldr	r4, .L1177+4
	ldr	r2, [r5, #2416]
	ldr	r0, [r4, #-468]
	mov	r2, r2, asl #1
	bl	ftl_memset
	movw	r3, #2398
	movw	r2, #2426
	ldrh	r3, [r5, r3]
	ldrh	r2, [r5, r2]
	mov	r1, #255
	ldr	r0, [r4, #-440]
	mul	r2, r2, r3
	bl	ftl_memset
	ldr	ip, .L1177+8
	mov	r1, #0
	mov	r3, r4
	sub	r7, ip, #28
	mov	r4, #12
	mov	r5, r1
.L1173:
	ldrh	r2, [ip]
	add	lr, r1, #1
	uxth	r1, r1
	ldr	r0, .L1177
	cmp	r2, r1
	bls	.L1176
	mul	r0, r4, r1
	ldr	r2, [r3, #-3380]
	add	r8, r2, r0
	str	r5, [r8, #4]
	strh	r6, [r2, r0]	@ movhi
	ldr	r2, [r3, #-3380]
	add	r0, r2, r0
	ldrh	r2, [r7]
	mul	r2, r1, r2
	ldr	r1, [r3, #-440]
	bic	r2, r2, #3
	add	r2, r1, r2
	mov	r1, lr
	str	r2, [r0, #8]
	b	.L1173
.L1176:
	ldr	r2, .L1177+12
	mvn	r1, #0
	strh	r1, [r2, #2]	@ movhi
	strh	r1, [r2]	@ movhi
	ldr	r1, [r0, #2416]
	strh	r1, [r2, #10]	@ movhi
	ldr	r1, .L1177+16
	strh	r1, [r2, #4]	@ movhi
	ldrh	r1, [r2, #44]
	strh	r1, [r2, #8]	@ movhi
	movw	r1, #2424
	ldrh	r1, [r0, r1]
	strh	r1, [r2, #6]	@ movhi
	ldr	r2, [r3, #-472]
	str	r2, [r3, #-420]
	ldr	r2, [r3, #-444]
	str	r2, [r3, #-416]
	ldr	r2, [r3, #-468]
	str	r2, [r3, #-412]
	ldr	r2, [r3, #-452]
	str	r2, [r3, #-408]
	ldmfd	sp!, {r4, r5, r6, r7, r8, pc}
.L1178:
	.align	2
.L1177:
	.word	.LANCHOR0
	.word	.LANCHOR2
	.word	.LANCHOR0+2426
	.word	.LANCHOR2-432
	.word	-3902
	.fnend
	.size	FtlL2PDataInit, .-FtlL2PDataInit
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
	ldr	r5, .L1181
	mov	r4, #0
	ldr	r6, .L1181+4
	mov	r1, r4
	sub	r2, r5, #380
	movw	r7, #2330
	str	r3, [r5, #-368]
	strh	r3, [r2]	@ movhi
	movw	r3, #2434
	strh	r4, [r6, r3]	@ movhi
	movw	r3, #2408
	ldrh	r2, [r6, r3]
	ldr	r0, [r6, #2436]
	str	r4, [r5, #-384]
	mov	r2, r2, asl #1
	str	r4, [r5, #-376]
	str	r4, [r5, #-372]
	str	r4, [r6, #2244]
	bl	ftl_memset
	ldrh	r2, [r6, r7]
	mov	r1, r4
	ldr	r0, [r5, #-3608]
	mov	r2, r2, asl #1
	bl	ftl_memset
	ldrh	r2, [r6, r7]
	mov	r1, r4
	ldr	r0, [r5, #-484]
	mov	r2, r2, asl #1
	bl	ftl_memset
	mov	r1, r4
	sub	r0, r5, #3600
	mov	r2, #48
	bl	ftl_memset
	mov	r1, r4
	mov	r2, #512
	sub	r0, r5, #3232
	bl	ftl_memset
	bl	FtlGcBufInit
	bl	FtlL2PDataInit
	mov	r0, r4
	ldmfd	sp!, {r3, r4, r5, r6, r7, pc}
.L1182:
	.align	2
.L1181:
	.word	.LANCHOR2
	.word	.LANCHOR0
	.fnend
	.size	FtlVariablesInit, .-FtlVariablesInit
	.align	2
	.global	SupperBlkListInit
	.type	SupperBlkListInit, %function
SupperBlkListInit:
	.fnstart
	@ args = 0, pretend = 0, frame = 24
	@ frame_needed = 0, uses_anonymous_args = 0
	stmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.save {r4, r5, r6, r7, r8, r9, r10, fp, lr}
	movw	r3, #2330
	ldr	r10, .L1195
	mov	r2, #6
	ldr	r6, .L1195+4
	mov	r1, #0
	.pad #28
	sub	sp, sp, #28
	mov	r7, #0
	ldrh	r3, [r10, r3]
	mov	r4, r7
	ldr	r0, [r6, #-3552]
	mov	r8, r7
	mov	r9, r7
	mov	r5, r6
	mul	r2, r2, r3
	bl	ftl_memset
	sub	r3, r6, #3520
	sub	r1, r6, #3536
	mov	fp, r6
	strh	r7, [r3, #-8]	@ movhi
	sub	r3, r6, #568
	str	r7, [r6, #-3532]
	str	r7, [r6, #-3548]
	str	r7, [r6, #-3540]
	strh	r7, [r1]	@ movhi
	strh	r7, [r3]	@ movhi
	str	r10, [sp, #8]
	str	r1, [sp, #12]
.L1184:
	ldr	r3, .L1195+8
	uxth	r7, r4
	sxth	r10, r7
	ldrh	r2, [r3]
	cmp	r10, r2
	bge	.L1191
	sub	r3, r3, #8
	ldr	r2, .L1195+12
	mov	ip, r7
	ldrh	r3, [r3]
	str	r3, [sp]
	ldrh	r3, [r2]
	mov	r2, #0
	mov	r6, r2
	str	r3, [sp, #4]
.L1192:
	ldr	r3, [sp]
	sxth	r1, r2
	cmp	r1, r3
	bge	.L1194
	ldr	r3, [sp, #8]
	str	r2, [sp, #20]
	add	r1, r3, r1
	str	ip, [sp, #16]
	ldrb	r0, [r1, #2348]	@ zero_extendqisi2
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
	b	.L1192
.L1194:
	cmp	r6, #0
	beq	.L1187
	sxth	r1, r6
	mov	r0, #32768
	bl	__aeabi_idiv
	uxth	r6, r0
	b	.L1188
.L1187:
	sxth	r7, r7
	ldr	r2, [r5, #-3544]
	mvn	r1, #0
	mov	r7, r7, asl #1
	strh	r1, [r2, r7]	@ movhi
.L1188:
	mov	r1, r10, asl #1
	ldr	r2, [r5, #-3552]
	add	r0, r1, r10
	add	r2, r2, r0, asl #1
	strh	r6, [r2, #4]	@ movhi
	ldr	r2, .L1195+16
	ldrh	r0, [r2]
	cmp	r10, r0
	beq	.L1189
	ldrh	r0, [r2, #48]
	cmp	r10, r0
	beq	.L1189
	ldrh	r2, [r2, #96]
	cmp	r10, r2
	beq	.L1189
	ldr	r3, [fp, #-3544]
	uxth	r0, r4
	ldrh	r3, [r3, r1]
	cmp	r3, #0
	bne	.L1190
	add	r8, r8, #1
	uxth	r8, r8
	bl	INSERT_FREE_LIST
	b	.L1189
.L1190:
	add	r9, r9, #1
	uxth	r9, r9
	bl	INSERT_DATA_LIST
.L1189:
	add	r4, r4, #1
	b	.L1184
.L1191:
	ldr	r3, [sp, #12]
	mov	r0, #0
	strh	r9, [r3]	@ movhi
	ldr	r3, .L1195+20
	strh	r8, [r3, #-8]	@ movhi
	add	sp, sp, #28
	@ sp needed
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L1196:
	.align	2
.L1195:
	.word	.LANCHOR0
	.word	.LANCHOR2
	.word	.LANCHOR0+2328
	.word	.LANCHOR0+2388
	.word	.LANCHOR2-3524
	.word	.LANCHOR2-3520
	.fnend
	.size	SupperBlkListInit, .-SupperBlkListInit
	.align	2
	.global	FtlGcPageVarInit
	.type	FtlGcPageVarInit, %function
FtlGcPageVarInit:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	stmfd	sp!, {r4, r5, r6, lr}
	.save {r4, r5, r6, lr}
	mov	r3, #0
	ldr	r4, .L1199
	movw	r5, #2392
	ldr	r6, .L1199+4
	mov	r1, #255
	sub	r2, r4, #2672
	ldr	r0, [r4, #-2680]
	strh	r3, [r2, #-4]	@ movhi
	sub	r2, r4, #2656
	strh	r3, [r2, #-12]	@ movhi
	ldrh	r2, [r6, r5]
	mov	r2, r2, asl #1
	bl	ftl_memset
	ldrh	r3, [r6, r5]
	mov	r2, #12
	ldr	r0, [r4, #-2672]
	mov	r1, #255
	mul	r2, r2, r3
	bl	ftl_memset
	ldmfd	sp!, {r4, r5, r6, lr}
	b	FtlGcBufInit
.L1200:
	.align	2
.L1199:
	.word	.LANCHOR2
	.word	.LANCHOR0
	.fnend
	.size	FtlGcPageVarInit, .-FtlGcPageVarInit
	.align	2
	.global	ftl_memcpy
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
	.type	FlashReadIdbData, %function
FlashReadIdbData:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	stmfd	sp!, {r3, lr}
	.save {r3, lr}
	mov	r2, #2048
	ldr	r1, .L1204
	bl	ftl_memcpy
	mov	r0, #0
	ldmfd	sp!, {r3, pc}
.L1205:
	.align	2
.L1204:
	.word	.LANCHOR2-364
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
	ldr	r8, .L1219
.L1210:
	mov	r7, r4, asl #5
	ldr	r1, .L1219+4
	add	r0, r7, #1
	ldrb	r2, [r8, r4, asl #5]	@ zero_extendqisi2
	add	r0, r8, r0
	ldr	r6, .L1219+8
	bl	FlashMemCmp8
	add	r9, r6, #508
	subs	r5, r0, #0
	bne	.L1207
	adds	r9, r9, r7
	beq	.L1213
	add	r4, r6, r7
	mov	r3, r5
	ldr	r1, .L1219+12
	ldrb	r2, [r4, #530]	@ zero_extendqisi2
	b	.L1212
.L1207:
	add	r4, r4, #1
	cmp	r4, #86
	bne	.L1210
	b	.L1213
.L1218:
	add	r3, r3, #1
	cmp	r3, #4
	beq	.L1211
.L1212:
	ldrb	r0, [r1, r3, asl #5]	@ zero_extendqisi2
	cmp	r0, r2
	bne	.L1218
.L1211:
	ldr	r4, .L1219+16
	mov	r2, #32
	ldr	r1, .L1219+12
	ldr	r0, .L1219+20
	add	r1, r1, r3, asl #5
	bl	ftl_memcpy
	mov	r0, r4
	mov	r1, r9
	mov	r2, #32
	bl	ftl_memcpy
	ldrh	r0, [r4, #10]
	bl	FlashBlockAlignInit
	b	.L1208
.L1213:
	mvn	r5, #0
.L1208:
	mov	r0, r5
	ldmfd	sp!, {r3, r4, r5, r6, r7, r8, r9, pc}
.L1220:
	.align	2
.L1219:
	.word	.LANCHOR1+508
	.word	.LANCHOR0+2068
	.word	.LANCHOR1
	.word	.LANCHOR1+3260
	.word	.LANCHOR1+472
	.word	.LANCHOR0+48
	.fnend
	.size	FlashLoadPhyInfoInRam, .-FlashLoadPhyInfoInRam
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
	bne	.L1222
	cmp	r3, #0
	beq	.L1223
	mov	r0, r2
	mov	r1, r3
	mov	r2, #1024
	bl	ftl_memcpy
.L1223:
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
.L1222:
	cmp	r3, #0
	beq	.L1226
	mov	r1, r2
	mov	r0, r3
	mov	r2, #1024
	bl	ftl_memcpy
.L1226:
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
	.global	ftl_memcpy32
	.type	ftl_memcpy32, %function
ftl_memcpy32:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	@ link register save eliminated.
	mov	r3, #0
.L1239:
	cmp	r3, r2
	ldrne	ip, [r1, r3, asl #2]
	strne	ip, [r0, r3, asl #2]
	addne	r3, r3, #1
	bne	.L1239
.L1241:
	bx	lr
	.fnend
	.size	ftl_memcpy32, .-ftl_memcpy32
	.align	2
	.global	ftl_memcmp
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
	.type	timer_get_time, %function
timer_get_time:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	@ link register save eliminated.
	ldr	r3, .L1244
	ldr	r0, [r3]
	b	jiffies_to_msecs
.L1245:
	.align	2
.L1244:
	.word	jiffies
	.fnend
	.size	timer_get_time, .-timer_get_time
	.align	2
	.global	FlashSramLoadStore
	.type	FlashSramLoadStore, %function
FlashSramLoadStore:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	ldr	ip, .L1250
	cmp	r2, #0
	moveq	r2, r3
	ldr	ip, [ip, #1684]
	add	ip, ip, #4096
	add	r1, ip, r1
	beq	.L1249
	str	lr, [sp, #-4]!
	.save {lr}
	mov	lr, r0
	mov	r0, r1
	mov	r1, lr
	ldr	lr, [sp], #4
	mov	r2, r3
.L1249:
	b	ftl_memcpy
.L1251:
	.align	2
.L1250:
	.word	.LANCHOR2
	.fnend
	.size	FlashSramLoadStore, .-FlashSramLoadStore
	.align	2
	.global	FlashCs123Init
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
	.type	ftl_dma32_malloc, %function
ftl_dma32_malloc:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	cmp	r0, #8192
	stmfd	sp!, {r3, r4, r5, lr}
	.save {r3, r4, r5, lr}
	ble	.L1254
	ldmfd	sp!, {r3, r4, r5, lr}
	b	ftl_malloc
.L1254:
	ldr	r5, .L1257
	add	r4, r0, #63
	bic	r4, r4, #63
	ldr	r3, [r5, #1688]
	cmp	r3, r4
	bge	.L1255
	mov	r0, #16384
	bl	ftl_malloc
	mov	r3, #16384
	str	r3, [r5, #1688]
	str	r0, [r5, #1692]
.L1255:
	ldr	r3, [r5, #1688]
	ldr	r0, [r5, #1692]
	rsb	r3, r4, r3
	str	r3, [r5, #1688]
	add	r4, r0, r4
	str	r4, [r5, #1692]
	ldmfd	sp!, {r3, r4, r5, pc}
.L1258:
	.align	2
.L1257:
	.word	.LANCHOR2
	.fnend
	.size	ftl_dma32_malloc, .-ftl_dma32_malloc
	.align	2
	.global	rk_nand_suspend
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
	.type	rk_ftl_get_capacity, %function
rk_ftl_get_capacity:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	@ link register save eliminated.
	ldr	r3, .L1262
	ldr	r0, [r3, #2428]
	bx	lr
.L1263:
	.align	2
.L1262:
	.word	.LANCHOR0
	.fnend
	.size	rk_ftl_get_capacity, .-rk_ftl_get_capacity
	.align	2
	.global	rk_nandc_get_irq_status
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
	.type	rknand_print_hex, %function
rknand_print_hex:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	stmfd	sp!, {r3, r4, r5, r6, r7, r8, r9, lr}
	.save {r3, r4, r5, r6, r7, r8, r9, lr}
	mov	r5, #0
	mov	r9, r0
	mov	r6, r1
	mov	r7, r2
	mov	r8, r3
	mov	r4, r5
.L1268:
	cmp	r4, r8
	beq	.L1277
	cmp	r5, #0
	bne	.L1269
	ldr	r0, .L1278
	mov	r1, r9
	mov	r2, r6
	mov	r3, r4
	bl	printk
.L1269:
	cmp	r7, #4
	ldreq	r0, .L1278+4
	ldreq	r1, [r6, r4, asl #2]
	beq	.L1276
	cmp	r7, #2
	moveq	r3, r4, asl #1
	ldreq	r0, .L1278+4
	ldreqsh	r1, [r6, r3]
	ldrne	r0, .L1278+4
	ldrneb	r1, [r6, r4]	@ zero_extendqisi2
.L1276:
	add	r5, r5, #1
	bl	printk
	cmp	r5, #15
	bls	.L1273
	ldr	r0, .L1278+8
	mov	r5, #0
	ldr	r1, .L1278+12
	bl	printk
.L1273:
	add	r4, r4, #1
	b	.L1268
.L1277:
	ldr	r0, .L1278+8
	ldr	r1, .L1278+12
	ldmfd	sp!, {r3, r4, r5, r6, r7, r8, r9, lr}
	b	printk
.L1279:
	.align	2
.L1278:
	.word	.LC82
	.word	.LC83
	.word	.LC76
	.word	.LC84
	.fnend
	.size	rknand_print_hex, .-rknand_print_hex
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
	ldr	r7, .L1389
	mvn	r3, #83
	mvn	r1, #82
	mvn	r2, #81
	.pad #44
	sub	sp, sp, #44
	mov	r4, r0
	strb	r3, [r7, #1216]
	mvn	r3, #80
	strb	r0, [r7, #1212]
	strb	r1, [r7, #1217]
	strb	r2, [r7, #1218]
	strb	r3, [r7, #1219]
	bne	.L1281
	mvn	r3, #88
	strb	r3, [r7, #1216]
	ldr	r3, .L1389+4
	mvn	r2, #8
	mov	r5, #7
	strb	r2, [r3, #3405]
	b	.L1342
.L1281:
	cmp	r0, #3
	bne	.L1283
	mvn	r3, #79
	strb	r3, [r7, #1216]
	mvn	r3, #78
	strb	r3, [r7, #1217]
	mvn	r3, #77
	strb	r3, [r7, #1218]
	mvn	r3, #76
	strb	r3, [r7, #1219]
	mvn	r3, #75
	strb	r3, [r7, #1220]
	mvn	r3, #74
	strb	r3, [r7, #1221]
	mvn	r3, #73
	strb	r3, [r7, #1222]
	mvn	r3, #72
	b	.L1383
.L1283:
	cmp	r0, #4
	bne	.L1284
	mvn	r0, #51
	strb	r1, [r7, #1221]
	strb	r0, [r7, #1216]
	mvn	r0, #64
	strb	r2, [r7, #1222]
	strb	r0, [r7, #1217]
	mvn	r0, #85
	strb	r0, [r7, #1218]
	mvn	r0, #84
	strb	r0, [r7, #1219]
	mvn	r0, #50
	strb	r0, [r7, #1220]
.L1383:
	mov	r5, #8
	strb	r3, [r7, #1223]
	mov	r6, r5
	b	.L1282
.L1284:
	cmp	r0, #5
	bne	.L1285
	mov	r3, #56
	mov	r5, #8
	strb	r3, [r7, #1216]
	mov	r3, #57
	strb	r3, [r7, #1217]
	mov	r3, #58
	strb	r3, [r7, #1218]
	mov	r3, #59
	strb	r3, [r7, #1219]
	b	.L1342
.L1285:
	cmp	r0, #6
	bne	.L1286
	mov	r3, #14
	mov	r5, #12
	strb	r3, [r7, #1216]
	mov	r3, #15
	strb	r3, [r7, #1217]
	mov	r3, #16
	strb	r3, [r7, #1218]
	mov	r3, #17
	strb	r3, [r7, #1219]
	b	.L1342
.L1286:
	cmp	r0, #7
	bne	.L1287
	mvn	r3, #79
	mov	r5, #12
	strb	r3, [r7, #1216]
	mov	r6, #10
	mvn	r3, #78
	strb	r3, [r7, #1217]
	mvn	r3, #77
	strb	r3, [r7, #1218]
	mvn	r3, #76
	strb	r3, [r7, #1219]
	mvn	r3, #75
	strb	r3, [r7, #1220]
	mvn	r3, #74
	strb	r3, [r7, #1221]
	mvn	r3, #73
	strb	r3, [r7, #1222]
	mvn	r3, #72
	strb	r3, [r7, #1223]
	mvn	r3, #43
	strb	r3, [r7, #1224]
	mvn	r3, #42
	strb	r3, [r7, #1225]
	b	.L1282
.L1287:
	cmp	r0, #8
	mov	r5, #7
	bne	.L1342
	mov	r3, #6
	strb	r5, [r7, #1217]
	strb	r3, [r7, #1216]
	mov	r5, #50
	mov	r3, #9
	strb	r0, [r7, #1218]
	strb	r3, [r7, #1219]
	mov	r6, #5
	mov	r3, #10
	strb	r3, [r7, #1220]
	b	.L1282
.L1342:
	mov	r6, #4
.L1282:
	sub	r3, r4, #1
	cmp	r3, #1
	bhi	.L1379
	ldr	r10, .L1389+8
	mov	r9, #0
.L1288:
	ldrb	r1, [r7, #2230]	@ zero_extendqisi2
	uxtb	r2, r9
	ldr	r3, .L1389
	cmp	r1, r2
	bls	.L1295
	add	r2, r3, r2
	ldr	r4, .L1389+12
	ldr	r1, .L1389+16
	mov	ip, #55
	ldrb	r2, [r2, #2232]	@ zero_extendqisi2
	ldr	r0, [r3, r2, asl #3]
	add	r3, r3, r2, asl #3
	add	r4, r4, r2, asl #6
	ldrb	r8, [r3, #4]	@ zero_extendqisi2
	add	r4, r4, #20
	mov	fp, r4
	mov	r2, r4
	add	r8, r0, r8, asl #8
.L1290:
	str	ip, [r8, #2056]
	mov	r0, #80
	ldrb	r3, [r1, #1]!	@ zero_extendqisi2
	str	r2, [sp, #12]
	str	ip, [sp, #8]
	str	r3, [r8, #2052]
	str	r1, [sp, #4]
	str	r1, [sp, #16]
	bl	ndelay
	ldr	r3, [r8, #2048]
	ldr	r2, [sp, #12]
	ldmib	sp, {r1, ip}
	strb	r3, [r2], #1
	rsb	r3, r4, r2
	uxtb	r3, r3
	cmp	r3, r6
	bcc	.L1290
	mov	r2, #0
.L1291:
	add	r0, r10, r2
	mov	r3, #1
.L1292:
	ldrb	r1, [r0, r3, asl #2]	@ zero_extendqisi2
	ldrb	ip, [fp]	@ zero_extendqisi2
	add	r1, r1, ip
	strb	r1, [fp, r3, asl #3]
	add	r3, r3, #1
	cmp	r3, #7
	bne	.L1292
	add	r2, r2, #1
	add	fp, fp, #1
	cmp	r2, #4
	bne	.L1291
	mov	r3, #0
	add	r9, r9, #1
	strb	r3, [r4, #16]
	strb	r3, [r4, #24]
	strb	r3, [r4, #32]
	strb	r3, [r4, #40]
	strb	r3, [r4, #48]
	strb	r3, [r4, #41]
	strb	r3, [r4, #49]
	b	.L1288
.L1379:
	sub	r3, r4, #3
	cmp	r3, #5
	bhi	.L1295
	smulbb	r2, r6, r5
	mov	r3, r2, asl #4
	mov	r2, r2, asr #1
	str	r3, [sp, #36]
	mov	r3, r2, asl #1
	str	r3, [sp, #8]
	mov	r3, #0
.L1387:
	str	r3, [sp, #16]
	ldrb	r3, [sp, #16]	@ zero_extendqisi2
	str	r3, [sp, #12]
	ldr	r3, .L1389
	ldr	r2, [sp, #12]
	ldrb	r3, [r3, #2230]	@ zero_extendqisi2
	cmp	r3, r2
	bhi	.L1340
.L1295:
	ldr	r3, .L1389
	strb	r6, [r3, #1213]
	strb	r5, [r3, #1214]
	add	sp, sp, #44
	@ sp needed
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L1340:
	ldr	r2, [sp, #12]
	ldr	r3, .L1389
	add	r3, r3, r2
	ldrb	r10, [r3, #2232]	@ zero_extendqisi2
	ldr	r3, .L1389
	mov	r0, r10
	ldr	r8, [r3, r10, asl #3]
	add	r3, r3, r10, asl #3
	ldrb	fp, [r3, #4]	@ zero_extendqisi2
	mov	r3, #255
	mov	r9, fp, asl #8
	add	r7, r8, r9
	str	r3, [r7, #2056]
	bl	NandcWaitFlashReady
	cmp	r4, #7
	ldreq	r3, .L1389+12
	moveq	r7, #160
	mlaeq	r7, r7, r10, r3
	addeq	r3, r7, #28
	beq	.L1384
.L1297:
	cmp	r4, #8
	beq	.L1299
	ldr	r3, .L1389+12
	add	r7, r3, r10, asl #6
	add	r3, r7, #20
.L1384:
	str	r3, [sp, #20]
	cmp	r4, #4
	add	r3, r8, fp, asl #8
	mov	r2, #54
	str	r2, [r3, #2056]
	bne	.L1300
	add	r9, r8, r9
	mov	r3, #255
	str	r3, [r9, #2052]
	mov	r3, #64
	str	r3, [r9, #2048]
	mov	r3, #204
	b	.L1385
.L1300:
	sub	r2, r4, #5
	cmp	r2, #1
	bhi	.L1302
	ldr	r2, .L1389
	ldrb	r2, [r2, #1216]	@ zero_extendqisi2
	str	r2, [r3, #2052]
	mov	r2, #82
	str	r2, [r3, #2048]
	b	.L1301
.L1302:
	cmp	r4, #7
	bne	.L1301
	add	r9, r8, r9
	mov	r3, #174
	str	r3, [r9, #2052]
	mov	r3, #0
	str	r3, [r9, #2048]
	mov	r3, #176
.L1385:
	str	r3, [r9, #2052]
	mov	r3, #77
	str	r3, [r9, #2048]
.L1301:
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
.L1341:
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
	bne	.L1305
	cmp	r4, #7
	movne	ip, #2
	moveq	ip, #32
.L1305:
	ldr	r3, .L1389+20
	add	r0, r8, fp, asl #8
	ldr	r3, [r3, #1696]
	mov	r1, r3
.L1306:
	ldr	r2, [r0, #2048]
	strb	r2, [r1], #1
	rsb	r2, r3, r1
	uxtb	r2, r2
	cmp	r2, ip
	bcc	.L1306
	ldr	r2, [sp, #4]
	cmp	r2, #0
	beq	.L1307
	mov	r2, #0
.L1309:
	ldrb	ip, [r3, r2, asl #2]	@ zero_extendqisi2
	uxtb	r1, r2
	cmp	ip, #50
	beq	.L1308
	add	ip, r3, r2, asl #2
	ldrb	ip, [ip, #1]	@ zero_extendqisi2
	cmp	ip, #5
	beq	.L1308
	add	r2, r2, #1
	cmp	r2, #8
	bne	.L1309
	b	.L1310
.L1308:
	cmp	r1, #6
	bls	.L1311
.L1310:
	ldr	r0, .L1389+24
	mov	r1, #0
	bl	printk
.L1312:
	b	.L1312
.L1307:
	cmp	r4, #7
	bne	.L1313
	ldr	r2, [sp, #4]
.L1315:
	ldrb	ip, [r3, r2, asl #2]	@ zero_extendqisi2
	uxtb	r1, r2
	cmp	ip, #12
	beq	.L1314
	add	ip, r3, r2, asl #2
	ldrb	ip, [ip, #1]	@ zero_extendqisi2
	cmp	ip, #10
	beq	.L1314
	add	r2, r2, #1
	cmp	r2, #8
	bne	.L1315
	b	.L1316
.L1314:
	cmp	r1, #7
	bne	.L1311
.L1316:
	ldr	r0, .L1389+24
	mov	r1, #0
	bl	printk
.L1317:
	b	.L1317
.L1313:
	cmp	r4, #6
	bne	.L1311
	sub	r2, r3, #1
	add	r3, r3, #7
.L1318:
	ldrb	r1, [r2, #1]!	@ zero_extendqisi2
	cmp	r1, #12
	beq	.L1311
	ldrb	r1, [r2, #8]	@ zero_extendqisi2
	cmp	r1, #4
	beq	.L1311
	cmp	r2, r3
	bne	.L1318
	ldr	r0, .L1389+24
	mov	r1, #0
	bl	printk
.L1320:
	b	.L1320
.L1311:
	ldr	r1, .L1389+20
	ldr	ip, [sp, #36]
	ldr	r2, [r1, #1696]
	add	ip, r2, ip
	mov	r3, r2
.L1321:
	cmp	r3, ip
	ldrne	lr, [r0, #2048]
	strneb	lr, [r3], #1
	bne	.L1321
.L1388:
	ldr	r3, [r1, #1696]
	mov	r0, #8
	ldr	r1, [sp, #8]
	add	r1, r3, r1
	str	r1, [sp, #28]
.L1324:
	ldr	ip, [sp, #8]
	add	lr, r1, ip
.L1323:
	ldrh	ip, [r1]
	mvn	ip, ip
	strh	ip, [r1], #2	@ movhi
	cmp	r1, lr
	bne	.L1323
	ldr	ip, [sp, #8]
	subs	r0, r0, #1
	add	r1, r1, ip
	bne	.L1324
.L1325:
	mov	ip, #0
	mov	r0, ip
.L1328:
	mov	r1, #1
	mov	lr, #0
	mov	r1, r1, asl r0
	mov	r7, #16
	str	r7, [sp, #32]
	mov	r7, lr
.L1326:
	ldrh	r9, [r3, lr]
	and	r9, r9, r1
	cmp	r9, r1
	addeq	r7, r7, #1
	ldr	r9, [sp, #8]
	add	lr, lr, r9
	ldr	r9, [sp, #32]
	subs	r9, r9, #1
	str	r9, [sp, #32]
	bne	.L1326
	cmp	r7, #8
	add	r0, r0, #1
	orrhi	ip, ip, r1
	uxthhi	ip, ip
	cmp	r0, #16
	bne	.L1328
	ldr	r1, [sp, #28]
	strh	ip, [r3], #2	@ movhi
	cmp	r3, r1
	bne	.L1325
	ldr	r3, .L1389+20
	ldr	r1, [r3, #1696]
	mov	r3, #0
	sub	r0, r1, #4
	add	ip, r1, #28
.L1330:
	ldr	lr, [r0, #4]!
	cmp	lr, #0
	addeq	r3, r3, #1
	cmp	r0, ip
	bne	.L1330
	cmp	r3, #7
	ble	.L1331
	ldr	r0, .L1389+28
	mov	r2, #1
	mov	r3, #1024
	bl	rknand_print_hex
	ldr	r0, .L1389+24
	mov	r1, #0
	bl	printk
.L1332:
	b	.L1332
.L1331:
	cmp	r4, #6
	moveq	r0, #4
	beq	.L1333
	cmp	r4, #7
	moveq	r0, #10
	beq	.L1333
	ldr	r3, [sp, #4]
	cmp	r3, #0
	moveq	r0, #8
	movne	r0, #5
.L1333:
	sub	r9, r6, #1
	ldr	r1, [sp, #20]
	mov	ip, #0
	uxtb	r9, r9
	add	r9, r9, #1
.L1334:
	mov	r7, r1
	mov	r3, r2
.L1335:
	ldrb	lr, [r3], #1	@ zero_extendqisi2
	strb	lr, [r7], #1
	rsb	lr, r2, r3
	uxtb	lr, lr
	cmp	lr, r6
	bcc	.L1335
	add	ip, ip, #1
	add	r2, r2, r9
	cmp	ip, r5
	add	r1, r1, r0
	blt	.L1334
	add	r7, r8, fp, asl #8
	mov	r3, #255
	mov	r0, r10
	str	r3, [r7, #2056]
	bl	NandcWaitFlashReady
	ldr	r3, [sp, #24]
	cmp	r3, #0
	beq	.L1337
	mov	r3, #54
	str	r3, [r7, #2056]
	ldr	r3, .L1389
	mvn	r1, #0
	ldr	r0, [sp, #12]
	ldrb	r3, [r3, #1216]	@ zero_extendqisi2
	str	r3, [r7, #2052]
	mov	r3, #0
	str	r3, [r7, #2048]
	mov	r3, #22
	str	r3, [r7, #2056]
	bl	FlashReadCmd
	b	.L1338
.L1337:
	ldr	r3, [sp, #4]
	cmp	r3, #0
	movne	r3, #190
	moveq	r3, #56
	str	r3, [r7, #2056]
.L1338:
	mov	r0, r10
	bl	NandcWaitFlashReady
	ldr	r3, [sp, #16]
	add	r3, r3, #1
	b	.L1387
.L1299:
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
	ldr	r3, .L1389+32
	str	r3, [sp, #20]
	b	.L1341
.L1390:
	.align	2
.L1389:
	.word	.LANCHOR0
	.word	.LANCHOR1
	.word	.LANCHOR1+3388
	.word	.LANCHOR0+1212
	.word	.LANCHOR0+1215
	.word	.LANCHOR2
	.word	.LC85
	.word	.LC86
	.word	.LANCHOR0+1240
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
	bhi	.L1393
	b	HynixGetReadRetryDefault
.L1393:
	cmp	r3, #49
	bne	.L1394
	ldr	r0, .L1405
	mov	r2, #64
	ldr	r1, .L1405+4
	add	r0, r0, #1216
	strb	r3, [r0, #-4]
	mov	r3, #4
	strb	r3, [r0, #-3]
	mov	r3, #15
	strb	r3, [r0, #-2]
	b	.L1403
.L1394:
	sub	r2, r3, #65
	cmp	r3, #33
	cmpne	r2, #1
	ldrls	r0, .L1405
	strlsb	r3, [r0, #1212]
	movls	r3, #4
	bls	.L1404
.L1395:
	cmp	r3, #34
	cmpne	r3, #67
	bne	.L1396
	ldr	r0, .L1405
	strb	r3, [r0, #1212]
	mov	r3, #5
.L1404:
	strb	r3, [r0, #1213]
	mov	r2, #45
	mov	r3, #7
	add	r0, r0, #1216
	strb	r3, [r0, #-2]
	ldr	r1, .L1405+8
	b	.L1403
.L1396:
	cmp	r3, #35
	cmpne	r3, #68
	bxne	lr
	ldr	r0, .L1405
	mov	r2, #95
	ldr	r1, .L1405+12
	add	r0, r0, #1216
	strb	r3, [r0, #-4]
	mov	r3, #5
	strb	r3, [r0, #-3]
	mov	r3, #17
	strb	r3, [r0, #-2]
.L1403:
	b	ftl_memcpy
.L1406:
	.align	2
.L1405:
	.word	.LANCHOR0
	.word	.LANCHOR1+408
	.word	.LANCHOR1+256
	.word	.LANCHOR1+304
	.fnend
	.size	FlashGetReadRetryDefault, .-FlashGetReadRetryDefault
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
	ldr	r5, .L1447
	ldr	r3, [r5, #2260]
	ldr	r4, [r5, r0, asl #3]
	cmp	r3, #3
	bls	.L1438
	ldr	r3, [r4, #16]
	tst	r3, #4
	beq	.L1438
	ldr	r6, [r4, #16]
	ldr	r3, [r4, #8]
	ubfx	r6, r6, #1, #1
	cmp	r6, #0
	str	r3, [sp]
	movne	r6, #0
	beq	.L1420
.L1410:
	ldr	r2, [r4, #28]
	ldr	r3, [sp]
	ubfx	r2, r2, #16, #5
	ubfx	r3, r3, #22, #6
	cmp	r2, r3
	bge	.L1418
	ldr	r3, [r5, #2260]
	cmp	r3, #5
	bhi	.L1411
.L1414:
	add	r6, r6, #1
	bic	r3, r6, #-16777216
	cmp	r3, #0
	bne	.L1413
	ldr	r2, [r4, #28]
	mov	r1, r6
	ldr	r3, [sp]
	ubfx	r2, r2, #16, #5
	ldr	r0, .L1447+4
	ubfx	r3, r3, #22, #6
	bl	printk
	ldr	r0, .L1447+8
	mov	r1, r4
	mov	r2, #4
	mov	r3, #512
	bl	rknand_print_hex
	b	.L1413
.L1411:
	ldr	r3, [r4]
	str	r3, [sp, #4]
	ldr	r3, [sp, #4]
	tst	r3, #8192
	beq	.L1414
	ldr	r3, [sp, #4]
	tst	r3, #131072
	beq	.L1414
.L1418:
	ldr	r3, [r5, #2296]
	ldr	r4, .L1447
	cmp	r3, #0
	beq	.L1419
	ldr	r1, [sp]
	mov	r2, #0
	ldr	r0, [r4, #2288]
	ubfx	r1, r1, #22, #5
	mov	r1, r1, asl #10
	bl	rknand_dma_unmap_single
	ldr	r0, [r4, #2292]
	mov	r2, #0
	ldr	r1, [sp]
	ubfx	r1, r1, #22, #5
	mov	r1, r1, asl #7
	bl	rknand_dma_unmap_single
	b	.L1419
.L1413:
	mov	r0, #1
	mov	r1, #5
	bl	usleep_range
	b	.L1410
.L1422:
	ldr	r3, [r4, #8]
	add	r6, r6, #1
	str	r3, [sp]
	bic	r3, r6, #-16777216
	cmp	r3, #0
	bne	.L1421
	ldr	r2, [sp]
	mov	r1, r6
	ldr	r3, [r4, #28]
	ldr	r0, .L1447+12
	ubfx	r3, r3, #16, #5
	bl	printk
	ldr	r0, .L1447+8
	mov	r1, r4
	mov	r2, #4
	mov	r3, #512
	bl	rknand_print_hex
.L1421:
	mov	r0, #1
	mov	r1, #5
	bl	usleep_range
.L1420:
	ldr	r3, [sp]
	tst	r3, #1048576
	beq	.L1422
	ldr	r3, [r5, #2304]
	cmp	r3, #0
	beq	.L1423
	mov	r0, r4
	bl	NandcSendDumpDataStart
.L1423:
	ldr	r3, [r5, #2296]
	ldr	r6, .L1447
	cmp	r3, #0
	beq	.L1424
	ldr	r1, [sp]
	mov	r2, #1
	ldr	r0, [r6, #2288]
	ubfx	r1, r1, #22, #5
	mov	r1, r1, asl #10
	bl	rknand_dma_unmap_single
	ldr	r0, [r6, #2292]
	mov	r2, #1
	ldr	r1, [sp]
	ubfx	r1, r1, #22, #5
	mov	r1, r1, asl #7
	bl	rknand_dma_unmap_single
.L1424:
	ldr	r3, [r5, #2304]
	cmp	r3, #0
	beq	.L1419
	mov	r0, r4
	bl	NandcSendDumpDataDone
.L1419:
	mov	r3, #0
	str	r3, [r5, #2296]
	b	.L1407
.L1438:
	ldr	r3, [r4, #8]
	str	r3, [sp]
	ldr	r3, [sp]
	tst	r3, #1048576
	beq	.L1438
.L1407:
	add	sp, sp, #8
	@ sp needed
	ldmfd	sp!, {r4, r5, r6, pc}
.L1448:
	.align	2
.L1447:
	.word	.LANCHOR0
	.word	.LC87
	.word	.LC88
	.word	.LC89
	.fnend
	.size	NandcXferComp, .-NandcXferComp
	.align	2
	.global	NandcXferData
	.type	NandcXferData, %function
NandcXferData:
	.fnstart
	@ args = 4, pretend = 0, frame = 80
	@ frame_needed = 0, uses_anonymous_args = 0
	stmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.save {r4, r5, r6, r7, r8, r9, r10, fp, lr}
	tst	r3, #63
	ldr	r6, .L1497
	.pad #92
	sub	sp, sp, #92
	mov	r9, r0
	mov	r5, r1
	mov	r7, r2
	mov	r10, r3
	ldr	r8, [r6, r0, asl #3]
	bne	.L1450
	ldr	r3, [sp, #128]
	cmp	r3, #0
	bne	.L1451
	add	r0, sp, #24
	mov	r1, #255
	mov	r2, #64
	bl	ftl_memset
	add	r3, sp, #24
	str	r3, [sp, #128]
.L1451:
	ldr	r3, [sp, #128]
	mov	r0, r9
	mov	r1, r5
	mov	r2, r7
	str	r10, [sp]
	str	r3, [sp, #4]
	mov	r3, #0
	bl	NandcXferStart
	mov	r0, r9
	mov	r1, r5
	bl	NandcXferComp
	cmp	r5, #0
	movne	fp, #0
	bne	.L1452
	ldr	r3, [r6, #2308]
	mov	r0, r7, lsr #1
	mov	r2, r5
	mov	r1, r5
	cmp	r3, #25
	ldr	r3, [sp, #128]
	movcc	lr, #64
	movcs	lr, #128
.L1454:
	cmp	r1, r0
	add	r3, r3, #4
	add	ip, r2, lr
	bcs	.L1495
	ldr	r4, [r6, #2276]
	mov	r2, r2, lsr #2
	add	r1, r1, #1
	ldr	r2, [r4, r2, asl #2]
	mov	r4, r2, lsr #8
	strb	r2, [r3, #-4]
	strb	r4, [r3, #-3]
	mov	r4, r2, lsr #16
	mov	r2, r2, lsr #24
	strb	r4, [r3, #-2]
	strb	r2, [r3, #-1]
	mov	r2, ip
	b	.L1454
.L1495:
	ldr	r0, [r6, #2308]
	mov	r2, #0
	ldr	r1, [r6, #2260]
	mov	r7, r7, lsr #2
	mov	fp, r2
.L1456:
	cmp	r2, r7
	bcs	.L1452
	cmp	r0, #0
	beq	.L1452
	add	r3, r2, #8
	ldr	r3, [r8, r3, asl #2]
	str	r3, [sp, #20]
	ldr	r3, [sp, #20]
	tst	r3, #4
	bne	.L1480
	ldr	r4, [sp, #20]
	ubfx	r4, r4, #15, #1
	cmp	r4, #0
	bne	.L1480
	cmp	r1, #5
	bls	.L1458
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
	b	.L1494
.L1458:
	cmp	r1, #3
	bls	.L1460
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
	ldr	r3, [sp, #20]
	ldrhi	r4, [sp, #20]
	ubfxhi	r3, r3, #3, #5
	ldrls	r4, [sp, #20]
	ubfxls	r3, r3, #16, #5
	ubfxhi	r4, r4, #28, #1
	ubfxls	r4, r4, #30, #1
.L1494:
	orr	r4, r3, r4, asl #5
.L1460:
	cmp	fp, r4
	movcc	fp, r4
	b	.L1457
.L1480:
	mvn	fp, #0
.L1457:
	add	r2, r2, #1
	b	.L1456
.L1452:
	mov	r3, #0
	str	r3, [r8, #16]
	b	.L1463
.L1450:
	cmp	r1, #1
	mov	r4, #0
	bne	.L1492
	mov	fp, r4
.L1464:
	cmp	r4, r7
	bcs	.L1496
	ldr	r2, [sp, #128]
	cmp	r10, #0
	addne	r3, r10, r4, asl #9
	ldr	r1, [sp, #128]
	moveq	r3, r10
	cmp	r2, #0
	and	ip, r4, #3
	mov	r0, r8
	movne	r2, #2
	moveq	r2, #0
	mla	r2, r4, r2, r1
	mov	r1, #1
	str	ip, [sp, #8]
	add	r4, r4, #2
	str	r2, [sp]
	mov	r2, ip
	bl	NandcCopy1KB
	mov	r0, r9
	mov	r1, #1
	mov	r2, #2
	ldr	ip, [sp, #8]
	str	fp, [sp]
	str	fp, [sp, #4]
	mov	r3, ip
	bl	NandcXferStart
	mov	r0, r9
	mov	r1, #1
	bl	NandcXferComp
	b	.L1464
.L1496:
	mov	fp, #0
	b	.L1463
.L1492:
	str	r4, [sp]
	mov	r1, r4
	str	r4, [sp, #4]
	mov	r2, #2
	mov	r3, r4
	mov	fp, r4
	bl	NandcXferStart
	str	r10, [sp, #8]
.L1469:
	cmp	r4, r7
	bcs	.L1463
	mov	r0, r9
	mov	r1, r5
	bl	NandcXferComp
	ldr	r3, [r8, #32]
	add	ip, r4, #2
	cmp	ip, r7
	str	r3, [sp, #20]
	bcs	.L1470
	mov	r3, #0
	mov	r0, r9
	str	r3, [sp]
	mov	r1, r3
	str	r3, [sp, #4]
	mov	r2, #2
	and	r3, ip, #3
	str	ip, [sp, #12]
	bl	NandcXferStart
	ldr	ip, [sp, #12]
.L1470:
	ldr	r3, [sp, #20]
	tst	r3, #4
	mvnne	fp, #0
	bne	.L1471
	ldr	r2, [sp, #20]
	ldr	r3, [sp, #20]
	ubfx	r2, r2, #3, #5
	ubfx	r3, r3, #27, #1
	orr	r3, r2, r3, asl #5
	cmp	fp, r3
	movcc	fp, r3
.L1471:
	ldr	r1, [sp, #128]
	cmp	r10, #0
	ldr	r3, [sp, #8]
	sub	r2, ip, #2
	moveq	r3, #0
	ldr	r0, [sp, #128]
	cmp	r1, #0
	and	r2, r2, #3
	str	ip, [sp, #12]
	movne	r1, #2
	moveq	r1, #0
	mla	r4, r4, r1, r0
	mov	r0, r8
	mov	r1, #0
	str	r4, [sp]
	bl	NandcCopy1KB
	ldr	ip, [sp, #12]
	ldr	r3, [sp, #8]
	mov	r4, ip
	add	r3, r3, #1024
	str	r3, [sp, #8]
	b	.L1469
.L1463:
	ldr	r3, [r6, #2260]
	clz	r5, r5
	cmp	r3, #5
	mov	r5, r5, lsr #5
	movls	r5, #0
	cmp	r5, #0
	beq	.L1475
	ldr	r3, [r8]
	and	r2, r3, #139264
	cmp	r2, #139264
	orreq	r3, r3, #131072
	streq	r3, [r8]
	mvneq	fp, #0
.L1475:
	mov	r0, fp
	add	sp, sp, #92
	@ sp needed
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L1498:
	.align	2
.L1497:
	.word	.LANCHOR0
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
	ldr	r3, .L1502
	subs	r4, r0, #0
	mov	r6, r1
	mov	r7, r2
	ldrb	r5, [r3, #481]	@ zero_extendqisi2
	bne	.L1500
	ldr	r2, .L1502+4
	ldrb	r3, [r2, #33]	@ zero_extendqisi2
	ldr	r2, [r2, #36]
	mul	r2, r2, r3
	cmp	r1, r2
	movcc	r5, #4
.L1500:
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
.L1503:
	.align	2
.L1502:
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
	ldr	r4, .L1531
	.pad #20
	sub	sp, sp, #20
	mov	r10, r0
	mov	fp, r2
	str	r1, [sp]
	ldr	r3, [r4, #84]
	ldr	r3, [r3, #304]
	str	r3, [sp, #8]
	ldr	r3, [r4, #2260]
	cmp	r3, #8
	ldr	r3, [sp, #56]
	movcc	r9, #6
	movcs	r9, #12
	cmp	r3, #0
	moveq	r6, #1024
	beq	.L1506
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
	ldrb	r0, [r4, #2229]	@ zero_extendqisi2
	bl	FlashSetInterfaceMode
	ldrb	r0, [r4, #2229]	@ zero_extendqisi2
	bl	NandcSetMode
	cmn	r6, #1
	bne	.L1507
.L1516:
	mvn	r6, #0
	b	.L1508
.L1507:
	mov	r2, r6
	ldr	r0, .L1531+4
	ldr	r1, [sp]
	bl	printk
	cmp	r6, #9
	ldrls	r3, [r4, r10, asl #3]
	ldr	r4, .L1531+8
	ldrls	r2, [r3, #3840]
	ldrls	r2, [r3]
	orrls	r2, r2, #131072
	strls	r2, [r3]
	ldr	r3, [r4, #1700]
	add	r3, r3, #1
	str	r3, [r4, #1700]
	cmp	r3, #2048
	movcs	r7, #0
	strcs	r7, [r4, #1700]
	movcs	fp, r7
	bcc	.L1508
.L1506:
	mov	r4, #0
	mvn	r8, #0
	mov	ip, r4
	mov	r5, r4
	str	r4, [sp, #4]
.L1514:
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
	bhi	.L1510
	cmp	r0, #2
	bhi	.L1520
	add	r5, r5, #1
	cmp	r5, #9
	bls	.L1520
	rsb	r4, r5, r9
	mov	r6, r0
	mov	r8, #0
	b	.L1512
.L1510:
	ldr	r3, [sp, #4]
	cmp	r3, r5
	bcs	.L1521
	cmp	r5, #7
	rsb	ip, r5, r4
	bhi	.L1522
	str	r5, [sp, #4]
	b	.L1521
.L1520:
	mov	r8, #0
	mov	r4, r9
	mov	r6, r0
	mov	r7, r8
	mov	fp, r8
	b	.L1511
.L1521:
	mov	r5, #0
.L1511:
	add	r9, r9, #2
	cmp	r9, #69
	bls	.L1514
.L1512:
	ldr	r3, [sp, #4]
	cmp	r3, r5
	movcs	r4, ip
	b	.L1513
.L1522:
	mov	r4, ip
.L1513:
	cmp	r4, #0
	beq	.L1515
	ldr	r0, .L1531+12
	mov	r1, r4
	bl	printk
	uxtb	r0, r4
	bl	NandcSetDdrPara
.L1515:
	cmn	r8, #1
	bne	.L1508
	ldr	r0, .L1531+16
	mov	r1, r10
	ldr	r2, [sp]
	bl	printk
	ldr	r3, [sp, #56]
	cmp	r3, #0
	beq	.L1516
	ldr	r3, [sp, #8]
	ubfx	r0, r3, #8, #8
	bl	NandcSetDdrPara
.L1508:
	mov	r0, r6
	add	sp, sp, #20
	@ sp needed
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L1532:
	.align	2
.L1531:
	.word	.LANCHOR0
	.word	.LC90
	.word	.LANCHOR2
	.word	.LC91
	.word	.LC92
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
	cmn	r0, #1
	mov	r4, r0
	bne	.L1534
	ldr	r9, .L1554
	ldrb	fp, [r9, #40]	@ zero_extendqisi2
	mov	r10, r9
	cmp	fp, #0
	bne	.L1535
.L1537:
	ldrb	r3, [r10, #2252]	@ zero_extendqisi2
	ldr	r9, .L1554
	cmp	r3, #0
	beq	.L1534
	b	.L1553
.L1535:
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
	beq	.L1537
	b	.L1534
.L1553:
	ldr	r3, [r9, #84]
	mov	r0, r5
	mov	r1, r6
	mov	r2, r8
	ldr	r10, [r3, #304]
	mov	r3, #1
	str	r3, [sp]
	mov	r3, r7
	bl	FlashDdrTunningRead
	cmn	r0, #1
	mov	r4, r0
	beq	.L1538
	ldrb	r3, [r9, #2312]	@ zero_extendqisi2
	cmp	r0, r3, lsr #1
	bls	.L1534
.L1538:
	ubfx	r0, r10, #8, #8
	bl	NandcSetDdrPara
.L1534:
	ldr	r9, .L1554+4
	ldr	ip, [r9, #1704]
	adds	r3, ip, #0
	movne	r3, #1
	cmn	r4, #1
	movne	r3, #0
	cmp	r3, #0
	beq	.L1539
	mov	r1, r6
	mov	r2, r8
	mov	r3, r7
	mov	r0, r5
	blx	ip
	mov	r2, r5
	mov	r3, r6
	mov	r4, r0
	ldr	r0, .L1554+8
	mov	r1, r4
	bl	printk
	cmn	r4, #1
	bne	.L1539
	ldr	r3, .L1554
	ldrb	r3, [r3, #148]	@ zero_extendqisi2
	cmp	r3, #0
	beq	.L1539
	mov	r0, r5
	bl	flash_enter_slc_mode
	ldr	ip, [r9, #1704]
	mov	r0, r5
	mov	r1, r6
	mov	r2, r8
	mov	r3, r7
	blx	ip
	mov	r4, r0
	mov	r0, r5
	bl	flash_exit_slc_mode
.L1539:
	mov	r0, r4
	add	sp, sp, #12
	@ sp needed
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L1555:
	.align	2
.L1554:
	.word	.LANCHOR0
	.word	.LANCHOR2
	.word	.LC93
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
	ldr	r5, .L1566
	mov	r6, r1
	mov	r4, #0
	ldrb	r0, [r5, #2229]	@ zero_extendqisi2
	bl	FlashSetInterfaceMode
	ldrb	r0, [r5, #2229]	@ zero_extendqisi2
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
	bne	.L1557
	ldrb	r2, [r5, #2229]	@ zero_extendqisi2
	tst	r2, #1
	beq	.L1557
	mov	r0, #1
	bl	FlashSetInterfaceMode
	mov	r0, #1
	bl	NandcSetMode
	strb	r4, [r5, #2252]
	b	.L1558
.L1557:
	mov	r2, #1
	strb	r2, [r3, #2252]
.L1558:
	mov	r0, #0
	add	sp, sp, #8
	@ sp needed
	ldmfd	sp!, {r4, r5, r6, r7, r8, pc}
.L1567:
	.align	2
.L1566:
	.word	.LANCHOR0
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
	ldr	r3, .L1584
	mov	r4, #0
	ldr	r7, .L1584+4
	mov	r9, #4
	ldr	r5, .L1584+8
	mvn	r8, #0
	ldr	r0, [r3]	@ unaligned
	ldr	r3, [r7, #36]
	str	r4, [r5, #1712]
	mov	r6, r5
	str	r0, [sp, #12]	@ unaligned
	mov	r0, r4
	str	r3, [sp, #4]
	ldr	r3, [r5, #1696]
	str	r3, [r5, #1708]
	bl	flash_enter_slc_mode
.L1569:
	add	fp, r4, #1
	mov	r10, #0
.L1571:
	add	r3, sp, #12
	ldrb	r0, [r3, r10]	@ zero_extendqisi2
	bl	FlashBchSel
	mov	r0, #0
	mov	r1, r4
	ldr	r2, [r5, #1696]
	mov	r3, r0
	bl	FlashReadRawPage
	cmn	r0, #1
	bne	.L1570
	mov	r0, #0
	mov	r1, fp
	ldr	r2, [r6, #1696]
	mov	r3, r0
	bl	FlashReadRawPage
	cmn	r0, #1
	bne	.L1570
	add	r10, r10, #1
	cmp	r10, #4
	beq	.L1572
	b	.L1571
.L1573:
	add	r0, fp, #12
	movw	r1, #2036
	bl	js_hash
	ldr	r3, [fp, #8]
	cmp	r3, r0
	mvnne	r8, #0
	bne	.L1572
	ldr	r8, .L1584+12
	add	r1, fp, #160
	mov	r2, #32
	mov	r0, r8
	bl	ftl_memcpy
	ldr	r1, [r6, #1708]
	mov	r2, #32
	ldr	r0, .L1584+16
	add	r1, r1, #192
	bl	ftl_memcpy
	ldr	r1, [r6, #1708]
	mov	r2, #852
	ldr	r0, .L1584+20
	add	r1, r1, #224
	bl	ftl_memcpy
	ldrh	r0, [r8, #10]
	bl	FlashBlockAlignInit
	ldr	r8, [r6, #1708]
	str	r4, [r6, #1712]
	mov	r0, r4
	ldr	r1, [r7, #36]
	ldr	r3, [r8, #1076]
	strb	r3, [r7, #2252]
	bl	__aeabi_uidiv
	add	r0, r0, #1
	cmp	r0, #1
	strhi	r0, [r6, #1716]
	movls	r3, #2
	strls	r3, [r6, #1716]
	ldrh	r3, [r8, #14]
	mov	r8, #0
	strb	r3, [r5, #1720]
.L1572:
	ldr	r3, [sp, #4]
	subs	r9, r9, #1
	add	r4, r4, r3
	bne	.L1569
	mov	r0, r9
.L1583:
	bl	flash_exit_slc_mode
	mov	r0, r8
	add	sp, sp, #20
	@ sp needed
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L1570:
	ldr	fp, [r5, #1708]
	ldr	r2, .L1584+24
	ldr	r3, [fp]
	cmp	r3, r2
	bne	.L1572
	cmp	r8, #0
	bne	.L1573
	mov	r0, r4
	ldr	r1, [r7, #36]
	bl	__aeabi_uidiv
	ldr	r3, .L1584+8
	add	r0, r0, #1
	str	r0, [r3, #1716]
	mov	r0, r8
	b	.L1583
.L1585:
	.align	2
.L1584:
	.word	.LANCHOR3+11
	.word	.LANCHOR0
	.word	.LANCHOR2
	.word	.LANCHOR1+472
	.word	.LANCHOR0+48
	.word	.LANCHOR0+1212
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
	str	r2, [sp, #8]
	str	r1, [sp, #16]
	bl	NandcWaitFlashReady
	ldr	r4, .L1615
	add	r3, r4, r8, asl #3
	ldr	r6, [r4, r8, asl #3]
	ldrb	r2, [r3, #4]	@ zero_extendqisi2
	ldrb	r3, [r4, #80]	@ zero_extendqisi2
	add	r7, r2, #8
	sub	r3, r3, #67
	cmp	r3, #1
	add	r7, r6, r7, asl #8
	mov	r3, r2, asl #8
	str	r3, [sp, #12]
	movls	r3, #0
	strls	r3, [sp, #4]
	bls	.L1587
	ldrb	r5, [r4, #2252]	@ zero_extendqisi2
	cmp	r5, #0
	streq	r5, [sp, #4]
	beq	.L1588
	mov	r0, #0
	str	r2, [sp, #20]
	bl	NandcSetDdrMode
	mov	r3, #1
	ldr	r2, [sp, #20]
	str	r3, [sp, #4]
.L1588:
	ldr	r3, [sp, #12]
	mov	r1, #92
	add	r3, r6, r3
	str	r1, [r3, #2056]
	mov	r1, #197
	str	r1, [r3, #2056]
.L1587:
	mov	r9, #1
	mvn	r10, #0
	mov	r3, r2, asl #8
	str	r3, [sp, #20]
.L1589:
	ldr	r3, .L1615+4
	ldrb	r3, [r3, #1721]	@ zero_extendqisi2
	add	r3, r3, #1
	cmp	r9, r3
	bcs	.L1614
	ldrb	r3, [r4, #80]	@ zero_extendqisi2
	mov	r0, r7
	uxtb	r1, r9
	sub	r3, r3, #67
	cmp	r3, #1
	bhi	.L1590
	bl	SandiskSetRRPara
	b	.L1591
.L1590:
	bl	ToshibaSetRRPara
.L1591:
	ldrb	r3, [r4, #80]	@ zero_extendqisi2
	cmp	r3, #34
	bne	.L1592
	ldr	r3, .L1615+4
	ldrb	r3, [r3, #1721]	@ zero_extendqisi2
	sub	r3, r3, #3
	cmp	r9, r3
	ldreq	r3, [sp, #20]
	moveq	r2, #179
	addeq	r3, r6, r3
	streq	r2, [r3, #2056]
.L1592:
	ldr	r3, [sp, #12]
	mov	r2, #38
	add	r3, r6, r3
	str	r2, [r3, #2056]
	mov	r2, #93
	str	r2, [r3, #2056]
	ldr	r3, [sp, #4]
	cmp	r3, #0
	beq	.L1593
	mov	r0, #4
	bl	NandcSetDdrMode
	mov	r0, r8
	mov	r3, fp
	ldr	r1, [sp, #16]
	ldr	r2, [sp, #8]
	bl	FlashReadRawPage
	mov	r5, r0
	mov	r0, #0
	bl	NandcSetDdrMode
	b	.L1594
.L1593:
	mov	r0, r8
	ldr	r1, [sp, #16]
	ldr	r2, [sp, #8]
	mov	r3, fp
	bl	FlashReadRawPage
	mov	r5, r0
.L1594:
	cmn	r5, #1
	beq	.L1595
	ldrb	r2, [r4, #2312]	@ zero_extendqisi2
	cmn	r10, #1
	moveq	r10, r5
	add	r2, r2, r2, asl #1
	cmp	r5, r2, asr #2
	bcc	.L1597
	mov	fp, #0
	str	fp, [sp, #8]
.L1595:
	add	r9, r9, #1
	b	.L1589
.L1614:
	mov	r5, r10
.L1597:
	ldrb	r2, [r4, #80]	@ zero_extendqisi2
	mov	r0, r7
	mov	r1, #0
	sub	r2, r2, #67
	cmp	r2, #1
	bhi	.L1599
	bl	SandiskSetRRPara
	b	.L1600
.L1599:
	bl	ToshibaSetRRPara
.L1600:
	ldr	r3, [sp, #12]
	mov	r2, #255
	add	r6, r6, r3
	str	r2, [r6, #2056]
	ldrb	r2, [r4, #2312]	@ zero_extendqisi2
	add	r2, r2, r2, asl #1
	cmp	r5, r2, asr #2
	bcc	.L1601
	cmn	r5, #1
	movne	r5, #256
.L1601:
	mov	r0, r8
	bl	NandcWaitFlashReady
	ldr	r3, [sp, #4]
	cmp	r3, #0
	beq	.L1602
	mov	r0, #4
	bl	NandcSetDdrMode
.L1602:
	mov	r0, r5
	add	sp, sp, #28
	@ sp needed
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L1616:
	.align	2
.L1615:
	.word	.LANCHOR0
	.word	.LANCHOR2
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
	ldr	r5, .L1631
	mov	r9, r3
	mov	fp, r1
	mov	r10, r2
	bl	NandcWaitFlashReady
	add	r3, r5, r8, asl #3
	ldr	r6, [r5, r8, asl #3]
	mov	r7, #1
	ldrb	r3, [r3, #4]	@ zero_extendqisi2
	mvn	r4, #0
	ldr	ip, .L1631+4
	add	r3, r3, #8
	add	r6, r6, r3, asl #8
.L1618:
	ldrb	r3, [ip, #1721]	@ zero_extendqisi2
	add	r3, r3, #1
	cmp	r7, r3
	bcs	.L1621
	mov	r0, r6
	uxtb	r1, r7
	str	ip, [sp, #4]
	bl	SamsungSetRRPara
	mov	r0, r8
	mov	r1, fp
	mov	r2, r10
	mov	r3, r9
	bl	FlashReadRawPage
	cmn	r0, #1
	ldr	ip, [sp, #4]
	beq	.L1619
	ldrb	r3, [r5, #2312]	@ zero_extendqisi2
	cmn	r4, #1
	moveq	r4, r0
	add	r3, r3, r3, asl #1
	cmp	r0, r3, asr #2
	bcc	.L1624
	mov	r9, #0
	mov	r10, r9
.L1619:
	add	r7, r7, #1
	b	.L1618
.L1624:
	mov	r4, r0
.L1621:
	mov	r0, r6
	mov	r1, #0
	bl	SamsungSetRRPara
	ldrb	r3, [r5, #2312]	@ zero_extendqisi2
	add	r3, r3, r3, asl #1
	cmp	r4, r3, asr #2
	bcc	.L1623
	cmn	r4, #1
	movne	r4, #256
.L1623:
	mov	r0, r4
	add	sp, sp, #12
	@ sp needed
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L1632:
	.align	2
.L1631:
	.word	.LANCHOR0
	.word	.LANCHOR2
	.fnend
	.size	SamsungReadRetrial, .-SamsungReadRetrial
	.align	2
	.global	MicronReadRetrial
	.type	MicronReadRetrial, %function
MicronReadRetrial:
	.fnstart
	@ args = 0, pretend = 0, frame = 32
	@ frame_needed = 0, uses_anonymous_args = 0
.L1635:
	stmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.save {r4, r5, r6, r7, r8, r9, r10, fp, lr}
	mov	r8, r3
	ldr	r3, .L1659
	mov	r10, r2
	.pad #44
	sub	sp, sp, #44
	ldr	fp, .L1659
	mov	r6, r0
	ldrb	r5, [r3, #2312]	@ zero_extendqisi2
	ldrb	r3, [r3, #148]	@ zero_extendqisi2
	str	r1, [sp, #28]
	cmp	r3, #0
	addeq	r5, r5, r5, asl #1
	ldrne	r2, .L1659+4
	ubfxeq	r5, r5, #2, #8
	smullne	r2, r3, r5, r2
	uxtbne	r5, r3
	mov	r3, #0
	str	r3, [sp, #12]
	add	r3, fp, r0, asl #3
	str	r3, [sp, #32]
.L1645:
	mov	r0, r6
	mov	r9, #0
	bl	NandcWaitFlashReady
	ldr	r3, [fp, r6, asl #3]
	mvn	r4, #0
	str	r3, [sp, #20]
	ldr	r3, [sp, #32]
	ldrb	r3, [r3, #4]	@ zero_extendqisi2
	str	r3, [sp, #24]
	ldr	r2, [sp, #24]
	ldr	r3, [sp, #20]
	add	r7, r3, r2, asl #8
.L1636:
	ldr	r3, .L1659+8
	ldrb	r3, [r3, #1721]	@ zero_extendqisi2
	cmp	r9, r3
	bcs	.L1639
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
	mov	r2, r10
	str	ip, [r7, #2048]
	str	ip, [r7, #2048]
	str	ip, [r7, #2048]
	str	r3, [sp, #16]
	mov	r3, r8
	ldr	r1, [sp, #28]
	str	ip, [sp, #36]
	bl	FlashReadRawPage
	cmn	r0, #1
	beq	.L1637
	cmn	r4, #1
	ldr	ip, [sp, #36]
	moveq	r4, r0
	cmp	r0, r5
	bcc	.L1647
	mov	r8, ip
	mov	r10, ip
.L1637:
	ldr	r9, [sp, #16]
	b	.L1636
.L1647:
	mov	r4, r0
	mov	r8, ip
	mov	r10, ip
.L1639:
	ldr	r2, [sp, #24]
	mov	r0, #200
	ldr	r3, [sp, #20]
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
	bcc	.L1641
	cmn	r4, #1
	movne	r4, #256
.L1641:
	cmn	r4, #1
	movne	r7, #0
	moveq	r7, #1
	cmp	r4, #256
	movne	r1, r7
	orreq	r1, r7, #1
	cmp	r1, #0
	beq	.L1642
	mov	r3, r9
	str	r4, [sp]
	ldr	r0, .L1659+12
	mov	r1, r9
	ldr	r2, [sp, #28]
	bl	printk
	ldr	r3, [sp, #12]
	cmp	r3, #0
	bne	.L1643
	ldrb	r3, [fp, #148]	@ zero_extendqisi2
	cmp	r3, #0
	moveq	r7, #0
	andne	r7, r7, #1
	cmp	r7, #0
	beq	.L1652
	mov	r0, r6
	mov	r1, #3
	bl	micron_auto_read_calibration_config
	mov	r3, #1
	str	r3, [sp, #12]
	b	.L1645
.L1643:
	mov	r0, r6
	mov	r1, #0
	bl	micron_auto_read_calibration_config
	cmn	r4, #1
	movne	r4, #256
	b	.L1652
.L1642:
	ldr	r3, [sp, #12]
	cmp	r3, #0
	beq	.L1652
	mov	r0, r6
	mov	r4, #256
	bl	micron_auto_read_calibration_config
.L1652:
	mov	r0, r4
	add	sp, sp, #44
	@ sp needed
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L1660:
	.align	2
.L1659:
	.word	.LANCHOR0
	.word	1431655766
	.word	.LANCHOR2
	.word	.LC94
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
	ldr	r5, .L1679
	mov	r10, r2
	mov	r7, r0
	mov	fp, r1
	add	r2, r5, r0
	mvn	r6, #0
	ldr	r3, [r5, #44]
	ldrb	r4, [r2, #1224]	@ zero_extendqisi2
	ldrb	r9, [r5, #1214]	@ zero_extendqisi2
	ldrb	r3, [r3, #19]	@ zero_extendqisi2
	sub	r3, r3, #7
	cmp	r3, #1
	ldrlsb	r4, [r2, #1232]	@ zero_extendqisi2
	bl	NandcWaitFlashReady
	mov	ip, #0
.L1663:
	cmp	ip, r9
	bcs	.L1667
	add	r4, r4, #1
	mov	r0, r7
	ldrb	r1, [r5, #1213]	@ zero_extendqisi2
	uxtb	r4, r4
	ldr	r2, .L1679+4
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
	beq	.L1665
	ldrb	r3, [r5, #2312]	@ zero_extendqisi2
	cmn	r6, #1
	moveq	r6, r0
	add	r3, r3, r3, asl #1
	cmp	r0, r3, asr #2
	bcc	.L1672
	mov	r8, #0
	mov	r10, r8
.L1665:
	add	ip, ip, #1
	b	.L1663
.L1672:
	mov	r6, r0
.L1667:
	ldr	r3, [r5, #44]
	add	r7, r5, r7
	ldrb	r3, [r3, #19]	@ zero_extendqisi2
	sub	r3, r3, #7
	cmp	r3, #1
	ldrb	r3, [r5, #2312]	@ zero_extendqisi2
	strlsb	r4, [r7, #1232]
	strhib	r4, [r7, #1224]
	add	r3, r3, r3, asl #1
	cmp	r6, r3, asr #2
	bcc	.L1671
	cmn	r6, #1
	movne	r6, #256
.L1671:
	mov	r0, r6
	add	sp, sp, #12
	@ sp needed
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L1680:
	.align	2
.L1679:
	.word	.LANCHOR0
	.word	.LANCHOR0+1216
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
	ldr	r3, .L1711
	add	r2, r3, r10, asl #3
	ldr	r4, [r3, r10, asl #3]
	ldrb	r2, [r2, #4]	@ zero_extendqisi2
	str	r2, [sp, #8]
	ldrb	r2, [r3, #2228]	@ zero_extendqisi2
	str	r3, [sp, #16]
	cmp	r2, #0
	bne	.L1682
	ldr	r3, [sp, #8]
	mvn	r5, #0
	mov	r6, #1
	mov	r9, r3, asl #8
	add	r7, r4, r9
.L1686:
	mov	r3, #239
	str	r3, [r7, #2056]
	mov	r3, #141
	str	r3, [r7, #2052]
	ldr	r3, .L1711+4
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
	beq	.L1683
	ldr	r3, [sp, #16]
	cmn	r5, #1
	moveq	r5, r0
	ldrb	r3, [r3, #2312]	@ zero_extendqisi2
	add	r3, r3, r3, asl #1
	cmp	r0, r3, asr #2
	bcc	.L1694
	ldr	ip, [sp, #20]
	mov	r8, ip
	mov	fp, ip
.L1683:
	add	r6, r6, #1
	cmp	r6, #26
	bne	.L1686
	b	.L1685
.L1694:
	mov	r5, r0
.L1685:
	add	r9, r4, r9
	mov	r3, #239
	str	r3, [r9, #2056]
	ldr	r3, [sp, #8]
	add	r4, r4, r3, asl #8
	mov	r3, #141
	b	.L1710
.L1682:
	ldr	r3, [sp, #8]
	mvn	r5, #0
	ldr	r7, .L1711+8
	mov	r6, #1
	mov	ip, r3, asl #8
	add	r9, r4, ip
.L1691:
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
	beq	.L1688
	ldr	r3, [sp, #16]
	cmn	r5, #1
	moveq	r5, r0
	ldrb	r3, [r3, #2312]	@ zero_extendqisi2
	add	r3, r3, r3, asl #1
	cmp	r0, r3, asr #2
	bcc	.L1695
	mov	r8, #0
	mov	fp, r8
.L1688:
	add	r6, r6, #1
	add	r7, r7, #4
	cmp	r6, #26
	bne	.L1691
	b	.L1690
.L1695:
	mov	r5, r0
.L1690:
	add	ip, r4, ip
	mov	r3, #239
	str	r3, [ip, #2056]
	ldr	r3, [sp, #8]
	add	r4, r4, r3, asl #8
	mov	r3, #137
.L1710:
	str	r3, [r4, #2052]
	mov	r3, #0
	str	r3, [r4, #2048]
	str	r3, [r4, #2048]
	str	r3, [r4, #2048]
	str	r3, [r4, #2048]
	ldr	r3, [sp, #16]
	ldrb	r3, [r3, #2312]	@ zero_extendqisi2
	add	r3, r3, r3, asl #1
	cmp	r5, r3, asr #2
	bcc	.L1692
	cmn	r5, #1
	movne	r5, #256
.L1692:
	cmn	r5, #1
	cmpne	r5, #256
	bne	.L1693
	str	r5, [sp]
	mov	r1, r6
	ldr	r0, .L1711+12
	mov	r3, r6
	ldr	r2, [sp, #12]
	bl	printk
.L1693:
	mov	r0, r10
	bl	NandcWaitFlashReady
	mov	r0, r5
	add	sp, sp, #28
	@ sp needed
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L1712:
	.align	2
.L1711:
	.word	.LANCHOR0
	.word	.LANCHOR3+16
	.word	.LANCHOR3+44
	.word	.LC95
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
	ldr	r3, .L1717
	subs	r4, r0, #0
	mov	r5, r1
	mov	r7, r2
	ldrb	r6, [r3, #481]	@ zero_extendqisi2
	bne	.L1714
	ldr	r2, .L1717+4
	ldrb	r3, [r2, #33]	@ zero_extendqisi2
	ldr	r1, [r2, #36]
	mul	r1, r1, r3
	cmp	r5, r1
	bcs	.L1714
	ldrb	r3, [r2, #32]	@ zero_extendqisi2
	cmp	r3, #0
	movne	r6, #4
.L1714:
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
.L1718:
	.align	2
.L1717:
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
	ldr	r4, .L1733
	ldr	r5, .L1733+4
	ldr	r8, .L1733+8
	ldr	r3, [r4, #1696]
	mov	fp, r4
	ldrb	r0, [r4, #1722]	@ zero_extendqisi2
	mov	r10, r5
	str	r3, [r4, #1708]
	bl	FlashBchSel
	mov	r1, #0
	mov	r2, #2048
	ldr	r0, [r4, #1696]
	bl	ftl_memset
	ldr	r3, [r4, #1708]
	ldr	r1, .L1733+12
	mov	r2, #32
	str	r8, [r3]
	ldr	r0, [r4, #1708]
	ldrb	r3, [r5, #2230]	@ zero_extendqisi2
	add	r0, r0, #16
	strh	r3, [r0, #-4]	@ movhi
	ldrb	r3, [r5, #33]	@ zero_extendqisi2
	strh	r3, [r0, #-2]	@ movhi
	ldrb	r3, [r5, #2252]	@ zero_extendqisi2
	str	r3, [r0, #1060]
	bl	ftl_memcpy
	ldr	r0, [r4, #1708]
	ldr	r1, .L1733+16
	mov	r2, #8
	add	r0, r0, #80
	bl	ftl_memcpy
	ldr	r0, [r4, #1708]
	ldr	r1, .L1733+20
	mov	r2, #32
	add	r0, r0, #96
	bl	ftl_memcpy
	ldr	r0, [r4, #1708]
	ldr	r1, .L1733+24
	mov	r2, #32
	add	r0, r0, #160
	bl	ftl_memcpy
	ldr	r0, [r4, #1708]
	add	r1, r5, #48
	mov	r2, #32
	add	r0, r0, #192
	bl	ftl_memcpy
	ldr	r0, [r4, #1708]
	mov	r2, #852
	ldr	r1, .L1733+28
	add	r0, r0, #224
	bl	ftl_memcpy
	ldr	r6, [r4, #1708]
	movw	r1, #2036
	add	r0, r6, #12
	bl	js_hash
	movw	r3, #1592
	str	r3, [r6, #4]
	ldr	r3, [r4, #1724]
	str	r3, [r4, #1708]
	str	r0, [r6, #8]
	mov	r0, #0
	bl	flash_enter_slc_mode
	mov	r6, #0
	mov	r7, r6
.L1725:
	ldr	r1, [r5, #36]
	mov	r0, #0
	mov	r2, r0
	mul	r1, r1, r7
	bl	FlashEraseBlock
	ldrb	r9, [r5, #148]	@ zero_extendqisi2
	cmp	r9, #0
	beq	.L1720
	mov	r9, #0
.L1721:
	ldr	r1, [r5, #36]
	mov	r0, #0
	ldr	r2, [r4, #1696]
	mov	r3, r0
	mla	r1, r1, r7, r9
	add	r9, r9, #1
	bl	FlashProgPage
	cmp	r9, #10
	bne	.L1721
	b	.L1722
.L1720:
	ldr	r1, [r10, #36]
	mov	r3, r9
	ldr	r2, [r4, #1696]
	mov	r0, r9
	mul	r1, r1, r7
	bl	FlashProgPage
	ldr	r1, [r10, #36]
	mov	r0, r9
	ldr	r2, [r4, #1696]
	mov	r3, r9
	mul	r1, r1, r7
	add	r1, r1, #1
	bl	FlashProgPage
.L1722:
	ldr	r1, [r5, #36]
	mov	r0, #0
	ldr	r2, [r4, #1724]
	mov	r3, r0
	mul	r1, r1, r7
	bl	FlashReadRawPage
	add	r2, r7, #1
	cmn	r0, #1
	beq	.L1723
	ldr	r9, [fp, #1708]
	ldr	r3, [r9]
	cmp	r3, r8
	bne	.L1723
	add	r0, r9, #12
	movw	r1, #2036
	str	r2, [sp, #4]
	bl	js_hash
	ldr	r3, [r9, #8]
	cmp	r3, r0
	ldr	r2, [sp, #4]
	bne	.L1723
	ldr	r3, [r10, #36]
	cmp	r6, #1
	str	r2, [fp, #1716]
	mul	r7, r3, r7
	str	r7, [fp, #1712]
	beq	.L1726
	mov	r6, #1
.L1723:
	cmp	r2, #4
	mov	r7, r2
	bne	.L1725
	b	.L1724
.L1726:
	mov	r6, #2
.L1724:
	mov	r0, #0
	bl	flash_exit_slc_mode
	clz	r0, r6
	mov	r0, r0, lsr #5
	rsb	r0, r0, #0
	add	sp, sp, #12
	@ sp needed
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L1734:
	.align	2
.L1733:
	.word	.LANCHOR2
	.word	.LANCHOR0
	.word	1312902724
	.word	.LANCHOR0+2068
	.word	.LANCHOR0+2232
	.word	.LANCHOR0+1176
	.word	.LANCHOR1+472
	.word	.LANCHOR0+1212
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
	ldr	r3, .L1755
	.pad #20
	sub	sp, sp, #20
	ldr	r4, .L1755+4
	ldr	r0, [r3]	@ unaligned
	ldr	r3, [r4, #2248]
	ldrb	r10, [r4, #2312]	@ zero_extendqisi2
	cmp	r3, #0
	str	r0, [sp, #12]	@ unaligned
	beq	.L1736
	mov	r0, #0
	bl	flash_enter_slc_mode
.L1736:
	mov	r0, r9
	mov	r1, #0
	mov	r2, #2048
	mvn	r8, #0
	bl	ftl_memset
	mov	r5, #2
.L1737:
	ldrb	r3, [r4, #33]	@ zero_extendqisi2
	cmp	r5, r3
	bcs	.L1741
	ldr	fp, .L1755+8
	mov	r7, #0
.L1739:
	add	r3, sp, #12
	ldr	r6, .L1755+8
	ldrb	ip, [r7, r3]	@ zero_extendqisi2
	mov	r0, ip
	str	ip, [sp, #4]
	bl	FlashBchSel
	ldr	r1, [r4, #36]
	mov	r0, #0
	ldr	r2, [fp, #1696]
	mov	r3, r0
	mul	r1, r1, r5
	bl	FlashReadRawPage
	cmn	r0, #1
	ldr	ip, [sp, #4]
	bne	.L1738
	add	r7, r7, #1
	cmp	r7, #4
	bne	.L1739
	b	.L1740
.L1738:
	ldr	r3, [r6, #1696]
	ldr	r2, .L1755+12
	ldr	r3, [r3]
	cmp	r3, r2
	bne	.L1740
	mov	r1, ip
	ldr	r0, .L1755+16
	bl	printk
	mov	r2, #2048
	mov	r0, r9
	ldr	r1, [r6, #1696]
	bl	ftl_memcpy
	ldr	r3, [r6, #1696]
	ldr	r2, .L1755+4
	ldr	r3, [r3, #512]
	strb	r3, [r2, #33]
	ldr	r3, [r6, #1716]
	cmp	r3, r5
	bls	.L1744
	str	r5, [r6, #1716]
	bl	FlashSavePhyInfo
	mov	r8, #0
.L1740:
	add	r5, r5, #1
	b	.L1737
.L1744:
	mov	r8, #0
.L1741:
	mov	r0, r10
	bl	FlashBchSel
	ldr	r3, [r4, #2248]
	cmp	r3, #0
	beq	.L1748
	mov	r0, #0
	bl	flash_exit_slc_mode
.L1748:
	mov	r0, r8
	add	sp, sp, #20
	@ sp needed
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L1756:
	.align	2
.L1755:
	.word	.LANCHOR3+11
	.word	.LANCHOR0
	.word	.LANCHOR2
	.word	-52655045
	.word	.LC96
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
	mov	r5, r0
	.pad #20
	sub	sp, sp, #20
	mov	r0, #32768
	bl	ftl_malloc
	ldr	r8, .L1856
	ldr	r4, .L1856+4
	mov	r7, #0
	mov	r10, r4
	str	r0, [r8, #1696]
	mov	r0, #32768
	bl	ftl_malloc
	str	r0, [r8, #1724]
	mov	r0, #4096
	bl	ftl_dma32_malloc
	str	r0, [r8, #1728]
	mov	r0, #32768
	bl	ftl_malloc
	str	r0, [r8, #1732]
	mov	r0, #4096
	bl	ftl_dma32_malloc
	mov	r3, #50
	str	r7, [r8, #1716]
	strb	r3, [r4, #33]
	strb	r3, [r8, #1720]
	mov	r3, #128
	strb	r7, [r4, #2252]
	str	r3, [r4, #36]
	mov	r3, #60
	str	r7, [r8, #1700]
	strb	r7, [r4, #32]
	strb	r7, [r8, #1740]
	strb	r3, [r8, #1722]
	str	r0, [r8, #1736]
	mov	r0, r5
	bl	NandcInit
	ldr	r5, .L1856+8
.L1763:
	add	r3, r4, r7, asl #3
	uxtb	fp, r7
	ldr	r6, [r4, r7, asl #3]
	ldrb	r9, [r3, #4]	@ zero_extendqisi2
	mov	r0, fp
	bl	FlashReset
	mov	r0, fp
	bl	NandcFlashCs
	mov	r3, #144
	add	r6, r6, r9, asl #8
	mov	r0, #200
	mov	r9, #0
	str	r3, [r6, #2056]
	str	r9, [r6, #2052]
	bl	ndelay
	ldr	r2, [r6, #2048]
	uxtb	r2, r2
	strb	r2, [r5]
	ldr	r1, [r6, #2048]
	cmp	r2, #44
	strb	r1, [r5, #1]
	ldr	r1, [r6, #2048]
	strb	r1, [r5, #2]
	ldr	r1, [r6, #2048]
	strb	r1, [r5, #3]
	ldr	r1, [r6, #2048]
	strb	r1, [r5, #4]
	ldr	r1, [r6, #2048]
	strb	r1, [r5, #5]
	bne	.L1758
	mov	r2, #239
	mov	r0, #200
	str	r2, [r6, #2056]
	mov	r2, #1
	str	r2, [r6, #2052]
	bl	ndelay
	mov	r2, #4
	str	r2, [r6, #2048]
	str	r9, [r6, #2048]
	str	r9, [r6, #2048]
	str	r9, [r6, #2048]
.L1758:
	mov	r0, fp
	bl	NandcFlashDeCs
	ldrb	r2, [r5]	@ zero_extendqisi2
	sub	r3, r2, #1
	uxtb	r3, r3
	cmp	r3, #253
	bhi	.L1759
	ldrb	r1, [r5, #2]	@ zero_extendqisi2
	ldrb	r3, [r5, #1]	@ zero_extendqisi2
	ldr	r0, .L1856+12
	str	r1, [sp]
	ldrb	r1, [r5, #3]	@ zero_extendqisi2
	str	r1, [sp, #4]
	ldrb	r1, [r5, #4]	@ zero_extendqisi2
	str	r1, [sp, #8]
	ldrb	r1, [r5, #5]	@ zero_extendqisi2
	str	r1, [sp, #12]
	add	r1, r7, #1
	bl	printk
.L1759:
	cmp	r7, #0
	bne	.L1760
	ldrb	r3, [r4, #2068]	@ zero_extendqisi2
	sub	r3, r3, #1
	uxtb	r3, r3
	cmp	r3, #253
	bhi	.L1810
	ldrb	r3, [r10, #2069]	@ zero_extendqisi2
	cmp	r3, #255
	beq	.L1810
.L1760:
	ldrb	r3, [r5]	@ zero_extendqisi2
	add	r7, r7, #1
	add	r5, r5, #8
	cmp	r3, #181
	moveq	r3, #44
	streqb	r3, [r5, #-8]
	cmp	r7, #4
	bne	.L1763
	ldrb	r3, [r4, #2068]	@ zero_extendqisi2
	cmp	r3, #173
	beq	.L1764
	ldr	r3, .L1856+4
	ldr	r0, [r3, #2256]
	bl	NandcSetDdrMode
.L1764:
	mov	r2, #852
	ldr	r0, .L1856+16
	mov	r1, #0
	ldr	r5, .L1856+20
	bl	ftl_memset
	ldr	r2, [r4, #2264]
	ldr	r0, .L1856+24
	cmp	r2, r5
	add	r3, r0, #472
	str	r3, [r4, #44]
	mov	r3, #0
	strb	r3, [r4, #40]
	bne	.L1765
	ldrb	r3, [r0, #491]	@ zero_extendqisi2
	cmp	r3, #50
	ldrne	r3, .L1856+4
	movne	r1, #1
	strne	r1, [r3, #2248]
.L1765:
	ldrb	r6, [r4, #2069]	@ zero_extendqisi2
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
	bne	.L1766
	cmp	r6, #220
	bne	.L1767
	ldr	r3, .L1856+4
	ldrb	r3, [r3, #2071]	@ zero_extendqisi2
	cmp	r3, #149
	bne	.L1767
.L1766:
	mov	lr, #16
	strb	lr, [r4, #33]
	strb	lr, [r8, #1722]
	mov	r1, #1
	ldrb	lr, [r4, #2068]	@ zero_extendqisi2
	strb	r1, [r4, #32]
	cmp	lr, #152
	ldr	r3, .L1856
	strb	lr, [r0, #3417]
	strb	r6, [r0, #3418]
	bne	.L1769
	ldr	r7, .L1856+4
	movw	lr, #2072
	ldrsb	lr, [r7, lr]
	cmp	lr, #0
	strltb	r1, [r3, #1740]
	movge	r1, #24
	strgeb	r1, [r3, #1722]
.L1769:
	movw	r3, #2049
	cmp	r2, r5
	cmpne	r2, r3
	moveq	r3, #16
	streqb	r3, [r8, #1722]
	cmp	ip, #0
	ldrne	r3, .L1856+28
	movne	r2, #2048
	strneh	r2, [r3, #14]	@ movhi
	mvnne	r3, #37
	bne	.L1850
.L1773:
	cmp	r6, #220
	bne	.L1775
	ldr	r3, .L1856+28
	mov	r2, #4096
	strh	r2, [r3, #14]	@ movhi
	mvn	r3, #35
.L1850:
	strb	r3, [r0, #3418]
	b	.L1774
.L1775:
	cmp	r6, #211
	ldreq	r3, .L1856+28
	moveq	r2, #4096
	streqh	r2, [r3, #14]	@ movhi
	moveq	r3, #2
	streqb	r3, [r0, #3429]
.L1774:
	ldr	r1, .L1856+32
	mov	r2, #32
	ldr	r0, .L1856+36
	bl	ftl_memcpy
	ldr	r0, .L1856+40
	mov	r2, #32
	add	r1, r0, #2944
	bl	ftl_memcpy
.L1767:
	ldrb	r3, [r4, #32]	@ zero_extendqisi2
	ldr	r6, .L1856+4
	cmp	r3, #0
	bne	.L1776
	bl	FlashLoadPhyInfoInRam
	cmp	r0, #0
	bne	.L1778
	ldr	r3, [r6, #44]
	ldrh	r3, [r3, #16]
	mov	r3, r3, lsr #8
	tst	r3, #1
	and	r0, r3, #7
	strb	r0, [r6, #2229]
	bne	.L1778
	mov	r3, #1
	strb	r3, [r6, #2252]
	bl	FlashSetInterfaceMode
	ldrb	r0, [r6, #2229]	@ zero_extendqisi2
	bl	NandcSetMode
.L1778:
	ldr	r3, [r4, #44]
	ldr	r6, .L1856+4
	ldrb	r3, [r3, #26]	@ zero_extendqisi2
	strb	r3, [r4, #148]
	bl	FlashLoadPhyInfo
	cmp	r0, #0
	beq	.L1776
	ldr	r3, [r6, #2256]
	cmp	r3, #0
	beq	.L1781
	mov	r0, #1
	bl	FlashSetInterfaceMode
	mov	r0, #1
	b	.L1851
.L1781:
	ldrb	r0, [r6, #2229]	@ zero_extendqisi2
	bl	FlashSetInterfaceMode
	ldrb	r0, [r6, #2229]	@ zero_extendqisi2
.L1851:
	bl	NandcSetMode
	bl	FlashLoadPhyInfo
	cmp	r0, #0
	beq	.L1776
	mov	r0, #1
	ldr	r6, .L1856+4
	bl	FlashSetInterfaceMode
	mov	r0, #1
	bl	NandcSetMode
	ldr	r3, [r4, #44]
	ldr	r0, .L1856+44
	ldrh	r1, [r3, #14]
	bl	printk
	bl	FlashLoadPhyInfoInRam
	cmn	r0, #1
	beq	.L1824
	bl	FlashDieInfoInit
	ldr	r3, [r6, #44]
	ldrb	r0, [r3, #19]	@ zero_extendqisi2
	bl	FlashGetReadRetryDefault
	ldr	r3, .L1856+48
	ldr	r2, [r6, #44]
	ldrh	r3, [r3, #-2]
	ldrb	r1, [r2, #9]	@ zero_extendqisi2
	add	r3, r3, #4080
	add	r3, r3, #15
	cmp	r1, r3, asr #12
	ldrh	r3, [r2, #14]
	blt	.L1783
	add	r0, r3, #255
	cmp	r1, r0, asr #8
	bge	.L1784
.L1783:
	bic	r3, r3, #255
	strh	r3, [r2, #14]	@ movhi
.L1784:
	ldrb	r3, [r4, #2229]	@ zero_extendqisi2
	tst	r3, #6
	beq	.L1785
	bl	FlashSavePhyInfo
	mov	r0, #0
	bl	flash_enter_slc_mode
	mov	r0, #0
	ldr	r1, [r8, #1712]
	bl	FlashDdrParaScan
	mov	r0, #0
	bl	flash_exit_slc_mode
.L1785:
	bl	FlashSavePhyInfo
.L1776:
	ldr	r2, [r4, #44]
	ldr	r7, .L1856+4
	ldr	r6, .L1856
	ldrb	r3, [r2, #26]	@ zero_extendqisi2
	ldrh	r0, [r2, #10]
	ldrb	r9, [r2, #18]	@ zero_extendqisi2
	strb	r3, [r4, #148]
	ldrh	r3, [r2, #16]
	ubfx	r1, r3, #7, #1
	strb	r1, [r4, #40]
	ubfx	r1, r3, #3, #1
	strb	r1, [r8, #1741]
	ubfx	r1, r3, #4, #1
	ubfx	r3, r3, #8, #3
	strb	r1, [r4, #2240]
	strb	r3, [r4, #2229]
	mov	r3, #0
	ldrb	r1, [r2, #12]	@ zero_extendqisi2
	str	r3, [r8, #1704]
	bl	__aeabi_idiv
	mov	r1, r0
	mov	r0, r9
	bl	BuildFlashLsbPageTable
	bl	FlashDieInfoInit
	ldr	r3, [r4, #44]
	ldrh	r2, [r3, #16]
	tst	r2, #64
	beq	.L1787
	ldrb	r0, [r3, #19]	@ zero_extendqisi2
	ldrb	r3, [r7, #1213]	@ zero_extendqisi2
	strb	r0, [r7, #80]
	strb	r3, [r7, #81]
	ldrb	r3, [r7, #1214]	@ zero_extendqisi2
	strb	r3, [r6, #1721]
	sub	r3, r0, #1
	cmp	r3, #7
	bhi	.L1788
	ldr	r3, .L1856+52
	str	r3, [r6, #1704]
	sub	r3, r0, #5
	cmp	r0, #8
	cmpne	r3, #1
	sub	r6, r0, #8
	clz	r6, r6
	movls	r3, #1
	strls	r3, [r7, #2304]
	cmp	r0, #7
	mov	r6, r6, lsr #5
	ldreq	r6, .L1856+56
	beq	.L1791
	ldr	r3, .L1856+56
	cmp	r6, #0
	sub	r2, r3, #8
	movne	r6, r3
	moveq	r6, r2
.L1791:
	sub	r2, r6, #1
	add	r6, r6, #31
	mov	r3, #0
.L1792:
	ldrsb	r1, [r2, #1]!
	cmp	r1, #0
	addeq	r3, r3, #1
	cmp	r2, r6
	bne	.L1792
	cmp	r3, #27
	bls	.L1787
	bl	FlashGetReadRetryDefault
	bl	FlashSavePhyInfo
	b	.L1787
.L1788:
	sub	r3, r0, #17
	cmp	r3, #2
	bhi	.L1794
	ldr	r3, .L1856+60
	cmp	r0, #19
	str	r3, [r6, #1704]
	moveq	r3, #15
	bne	.L1855
	b	.L1853
.L1794:
	sub	r3, r0, #65
	cmp	r0, #33
	cmpne	r3, #1
	bhi	.L1796
	ldr	r3, .L1856+64
	str	r3, [r6, #1704]
	mov	r3, #4
	strb	r3, [r7, #81]
.L1855:
	mov	r3, #7
.L1853:
	strb	r3, [r6, #1721]
	b	.L1787
.L1796:
	sub	r2, r0, #67
	sub	r3, r0, #34
	cmp	r2, #1
	movhi	r2, #0
	movls	r2, #1
	cmp	r3, #1
	movhi	r3, r2
	orrls	r3, r2, #1
	cmp	r3, #0
	beq	.L1797
	ldr	r3, .L1856+64
	cmp	r0, #68
	cmpne	r0, #35
	str	r3, [r6, #1704]
	movne	r3, #7
	moveq	r3, #17
	cmp	r2, #0
	strb	r3, [r6, #1721]
	movne	r3, #4
	moveq	r3, #5
	strb	r3, [r4, #81]
	b	.L1787
.L1797:
	cmp	r0, #49
	ldreq	r3, .L1856+68
	streq	r3, [r6, #1704]
	beq	.L1787
	cmp	r0, #50
	streq	r3, [r7, #2248]
	ldreq	r2, .L1856+72
	streq	r2, [r6, #1704]
.L1787:
	ldr	r2, [r4, #2264]
	ldr	r3, .L1856+4
	cmp	r2, r5
	bne	.L1802
	ldrb	r1, [r3, #148]	@ zero_extendqisi2
	cmp	r1, #0
	ldrne	r3, [r3, #44]
	movne	r1, #0
	strneb	r1, [r3, #18]
.L1802:
	ldrb	r1, [r4, #2068]	@ zero_extendqisi2
	ldr	r3, .L1856+4
	cmp	r1, #44
	bne	.L1803
	ldrb	r1, [r3, #2252]	@ zero_extendqisi2
	cmp	r1, #0
	beq	.L1803
	cmp	r2, r5
	bne	.L1804
	ldrb	r3, [r3, #148]	@ zero_extendqisi2
	cmp	r3, #0
	bne	.L1803
.L1804:
	mov	r0, #1
	mov	r3, #0
	strb	r3, [r4, #2252]
	bl	FlashSetInterfaceMode
	mov	r0, #1
	bl	NandcSetMode
.L1803:
	ldrb	r3, [r4, #2229]	@ zero_extendqisi2
	tst	r3, #6
	beq	.L1805
	ldr	r2, .L1856+4
	ldrb	r2, [r2, #2252]	@ zero_extendqisi2
	cmp	r2, #0
	bne	.L1806
	tst	r3, #1
	bne	.L1805
.L1806:
	mov	r0, #0
	bl	flash_enter_slc_mode
	mov	r0, #0
	ldr	r1, [r8, #1712]
	bl	FlashDdrParaScan
	mov	r0, #0
	bl	flash_exit_slc_mode
.L1805:
	ldr	r3, [r4, #44]
	mov	r7, #16
	ldr	r6, .L1856+4
	ldrb	r0, [r3, #20]	@ zero_extendqisi2
	bl	FlashBchSel
	ldr	r0, .L1856+76
	bl	FlashReadIdbDataRaw
	ldr	r0, .L1856+80
	strb	r7, [r4, #33]
	bl	FlashTimingCfg
	ldr	r5, [r4, #44]
	ldrb	r2, [r4, #2069]	@ zero_extendqisi2
	ldrb	r3, [r5, #12]	@ zero_extendqisi2
	ldrh	r8, [r5, #14]
	strh	r3, [r4, #128]	@ movhi
	ldrb	r3, [r5, #7]	@ zero_extendqisi2
	str	r3, [r4, #124]
	mov	r3, r2, asl r7
	orr	r2, r3, r2, asl #8
	ldrb	r3, [r4, #2068]	@ zero_extendqisi2
	orr	r3, r2, r3
	ldrb	r2, [r4, #2071]	@ zero_extendqisi2
	orr	r3, r3, r2, asl #24
	str	r3, [r4, #120]
	ldrb	r3, [r4, #2230]	@ zero_extendqisi2
	strh	r3, [r4, #130]	@ movhi
	ldrb	r3, [r5, #13]	@ zero_extendqisi2
	strh	r8, [r4, #134]	@ movhi
	strh	r3, [r4, #132]	@ movhi
	ldrh	r3, [r5, #10]
	strh	r3, [r4, #136]	@ movhi
	ldrb	r1, [r5, #12]	@ zero_extendqisi2
	ldrh	r0, [r5, #10]
	bl	__aeabi_idiv
	strh	r0, [r4, #138]	@ movhi
	ldrb	r2, [r5, #9]	@ zero_extendqisi2
	strh	r2, [r4, #140]	@ movhi
	ldrb	r1, [r5, #9]	@ zero_extendqisi2
	ldrh	r3, [r5, #10]
	smulbb	r3, r1, r3
	mov	r1, #512
	strh	r1, [r4, #144]	@ movhi
	ldrb	r1, [r4, #33]	@ zero_extendqisi2
	strh	r1, [r4, #146]	@ movhi
	uxth	r3, r3
	ldrb	r1, [r4, #32]	@ zero_extendqisi2
	strh	r3, [r4, #142]	@ movhi
	cmp	r1, #1
	bne	.L1808
	mov	r3, r3, asl #1
	mov	r8, r8, lsr #1
	mov	r2, r2, asl #1
	strh	r3, [r6, #142]	@ movhi
	strb	r7, [r6, #33]
	mov	r3, #8
	strh	r8, [r6, #134]	@ movhi
	strh	r2, [r6, #140]	@ movhi
	strh	r3, [r6, #146]	@ movhi
.L1808:
	ldrb	r0, [r5, #20]	@ zero_extendqisi2
	bl	FlashBchSel
	bl	ftl_flash_suspend
	mov	r0, #0
	b	.L1824
.L1810:
	mvn	r0, #1
.L1824:
	add	sp, sp, #20
	@ sp needed
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L1857:
	.align	2
.L1856:
	.word	.LANCHOR2
	.word	.LANCHOR0
	.word	.LANCHOR0+2068
	.word	.LC97
	.word	.LANCHOR0+1212
	.word	1446522928
	.word	.LANCHOR1
	.word	.LANCHOR1+3416
	.word	.LANCHOR1+3292
	.word	.LANCHOR0+48
	.word	.LANCHOR1+472
	.word	.LC98
	.word	.LANCHOR2-568
	.word	HynixReadRetrial
	.word	.LANCHOR0+1240
	.word	MicronReadRetrial
	.word	ToshibaReadRetrial
	.word	SamsungReadRetrial
	.word	samsung_read_retrial
	.word	.LANCHOR2-364
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
	ldr	r1, .L1875
	mov	r4, r2
	mov	r6, r0
	ldrb	r2, [r1, #148]	@ zero_extendqisi2
	mov	r5, r1
	ldr	r3, [r1, #44]
	cmp	r2, #0
	ldrb	r3, [r3, #19]	@ zero_extendqisi2
	beq	.L1859
	ldr	r2, [r1, #2248]
	cmp	r2, #0
	ldmnefd	sp!, {r4, r5, r6, r7, r8, r9, r10, pc}
.L1859:
	sub	r2, r3, #5
	cmp	r3, #50
	cmpne	r2, #2
	bls	.L1860
	sub	r2, r3, #19
	tst	r2, #239
	moveq	r2, #1
	movne	r2, #0
	cmp	r3, #68
	movne	r3, r2
	orreq	r3, r2, #1
	cmp	r3, #0
	ldmeqfd	sp!, {r4, r5, r6, r7, r8, r9, r10, pc}
.L1860:
	ldr	r8, .L1875+4
	movw	r10, #65535
	ldr	r9, .L1875+8
.L1862:
	ldr	r3, [r5, #44]
	ldrh	r3, [r3, #10]
	cmp	r3, r4
	bls	.L1874
	mov	r3, r4, asl #1
	ldrh	r3, [r9, r3]
	cmp	r3, r10
	ldmnefd	sp!, {r4, r5, r6, r7, r8, r9, r10, pc}
	mov	r1, #255
	mov	r2, #32768
	ldr	r0, [r8, #1724]
	bl	ftl_memset
	ldr	r2, [r8, #1724]
	add	r1, r4, r7
	add	r4, r4, #1
	mov	r0, r6
	mov	r3, r2
	uxth	r4, r4
	bl	FlashProgPage
	b	.L1862
.L1874:
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, pc}
.L1876:
	.align	2
.L1875:
	.word	.LANCHOR0
	.word	.LANCHOR2
	.word	.LANCHOR2-2620
	.fnend
	.size	FlashPageProgMsbFFData, .-FlashPageProgMsbFFData
	.align	2
	.global	FlashReadSlc2KPages
	.type	FlashReadSlc2KPages, %function
FlashReadSlc2KPages:
	.fnstart
	@ args = 0, pretend = 0, frame = 24
	@ frame_needed = 0, uses_anonymous_args = 0
	ldr	r3, .L1928
	stmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.save {r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.pad #36
	sub	sp, sp, #36
	ldrb	r3, [r3, #481]	@ zero_extendqisi2
	mov	r4, r0
	mov	r8, #0
	str	r1, [sp, #16]
	str	r2, [sp, #20]
	str	r3, [sp, #12]
.L1878:
	ldr	r3, [sp, #16]
	cmp	r8, r3
	beq	.L1927
	ldr	r3, [sp, #16]
	add	r2, sp, #28
	mov	r0, r4
	ldr	r1, [sp, #20]
	rsb	r3, r8, r3
	ldr	r9, .L1928+4
	uxtb	r3, r3
	str	r3, [sp]
	add	r3, sp, #24
	mov	fp, r9
	bl	LogAddr2PhyAddr
	ldrb	r2, [r9, #2230]	@ zero_extendqisi2
	ldr	r3, [sp, #24]
	cmp	r3, r2
	mvncs	r3, #0
	strcs	r3, [r4]
	bcs	.L1880
	add	r3, r9, r3
	mov	r7, #0
	ldrb	r5, [r3, #2232]	@ zero_extendqisi2
	mov	r0, r5
	bl	NandcWaitFlashReady
	mov	r0, r5
	bl	NandcFlashCs
.L1881:
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
	ldr	r3, .L1928+8
	ldrb	r3, [r3, #1740]	@ zero_extendqisi2
	cmp	r3, #0
	mov	r6, r0
	beq	.L1882
	mov	r0, r5
	bl	flash_read_ecc
	cmp	r0, #5
	movhi	r6, #256
.L1882:
	cmp	r7, #9
	cmnls	r6, #1
	moveq	r3, #1
	movne	r3, #0
	addeq	r7, r7, #1
	beq	.L1881
.L1883:
	cmp	r7, #0
	mov	r7, r3
	movne	r6, #256
.L1885:
	ldr	r3, [sp, #28]
	mov	r0, r5
	ldr	r1, [fp, #36]
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
	ldr	r3, .L1928+8
	ldrb	r2, [r3, #1740]	@ zero_extendqisi2
	cmp	r2, #0
	mov	r10, r0
	beq	.L1888
	mov	r0, r5
	bl	flash_read_ecc
	cmp	r0, #5
	movhi	r10, #256
.L1888:
	cmp	r7, #9
	cmnls	r10, #1
	addeq	r7, r7, #1
	beq	.L1885
.L1889:
	cmp	r7, #0
	mov	r0, r5
	movne	r10, #256
	bl	NandcFlashDeCs
	ldrb	r2, [r9, #2312]	@ zero_extendqisi2
	cmp	r10, r6
	movcs	r3, r10
	movcc	r3, r6
	add	r2, r2, r2, asl #1
	cmp	r3, r2, asr #2
	bls	.L1891
	cmn	r3, #1
	movne	r3, #256
.L1891:
	cmp	r3, #256
	cmnne	r3, #1
	movne	r3, #0
	str	r3, [r4]
	ldr	r3, [r4, #12]
	cmp	r3, #0
	beq	.L1894
	ldr	r2, [r3, #12]
	cmn	r2, #1
	bne	.L1894
	ldr	r2, [r3, #8]
	cmn	r2, #1
	bne	.L1894
	ldr	r3, [r3]
	cmn	r3, #1
	strne	r2, [r4]
.L1894:
	ldr	r3, [r4]
	cmn	r3, #1
	bne	.L1880
	ldr	r1, [r4, #4]
	ldr	r0, .L1928+12
	ldrb	r2, [r9, #2312]	@ zero_extendqisi2
	bl	printk
	ldr	r1, [r4, #8]
	cmp	r1, #0
	beq	.L1896
	ldr	r0, .L1928+16
	mov	r2, #4
	mov	r3, #8
	bl	rknand_print_hex
.L1896:
	ldr	r1, [r4, #12]
	cmp	r1, #0
	beq	.L1880
	mov	r2, #4
	ldr	r0, .L1928+20
	mov	r3, r2
	bl	rknand_print_hex
.L1880:
	add	r8, r8, #1
	add	r4, r4, #36
	b	.L1878
.L1927:
	mov	r0, #0
	add	sp, sp, #36
	@ sp needed
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L1929:
	.align	2
.L1928:
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
	.type	FlashReadPages, %function
FlashReadPages:
	.fnstart
	@ args = 0, pretend = 0, frame = 40
	@ frame_needed = 0, uses_anonymous_args = 0
	ldr	r3, .L2005
	stmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.save {r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.pad #52
	sub	sp, sp, #52
	ldr	r6, .L2005+4
	ldrb	r3, [r3, #481]	@ zero_extendqisi2
	str	r1, [sp, #20]
	ldrb	r8, [r6, #32]	@ zero_extendqisi2
	str	r3, [sp, #16]
	ldrb	r3, [r6, #40]	@ zero_extendqisi2
	cmp	r8, #0
	str	r2, [sp, #24]
	str	r3, [sp, #28]
	beq	.L1967
	bl	FlashReadSlc2KPages
	b	.L2000
.L1967:
	ldr	r10, .L2005+8
	mov	r9, r0
	mov	fp, r8
.L1931:
	ldr	r3, [sp, #20]
	cmp	fp, r3
	bcs	.L2003
	mov	r3, #36
	add	r2, sp, #44
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
	add	r3, sp, #40
	bl	LogAddr2PhyAddr
	ldrb	r2, [r6, #2230]	@ zero_extendqisi2
	ldr	r3, [sp, #40]
	mov	r5, r0
	cmp	r3, r2
	ldrcs	r2, [sp, #8]
	mvncs	r3, #0
	strcs	r3, [r9, r2]
	bcs	.L1934
	add	r3, r6, r3
	ldrb	r4, [r3, #2232]	@ zero_extendqisi2
	ldrb	r3, [r10, #1741]	@ zero_extendqisi2
	cmp	r3, #0
	mov	r0, r4
	moveq	r5, #0
	bl	NandcWaitFlashReady
	ldr	r3, .L2005+4
	ldr	r3, [r3, #44]
	ldrb	r2, [r3, #19]	@ zero_extendqisi2
	sub	r3, r2, #1
	cmp	r3, #7
	bhi	.L1936
	sub	r2, r2, #7
	add	r1, r6, r4
	cmp	r2, #1
	ldr	r2, .L2005+4
	ldrb	r3, [r1, #1224]	@ zero_extendqisi2
	add	r2, r2, r4
	ldrlsb	r3, [r1, #1232]	@ zero_extendqisi2
	ldrb	r2, [r2, #2064]	@ zero_extendqisi2
	cmp	r2, r3
	beq	.L1936
	mov	r0, r4
	ldrb	r1, [r6, #1213]	@ zero_extendqisi2
	ldr	r2, .L2005+12
	bl	HynixSetRRPara
.L1936:
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
	beq	.L1938
	ldr	r3, .L2005+4
	ldrb	r3, [r3, #148]	@ zero_extendqisi2
	cmp	r3, #0
	beq	.L1938
	bl	flash_enter_slc_mode
	b	.L1939
.L1938:
	bl	flash_exit_slc_mode
.L1939:
	ldr	r1, [sp, #44]
	cmn	r1, #1
	cmpeq	r4, #255
	moveq	r3, #0
	movne	r3, #1
	moveq	r5, r3
	beq	.L1941
	cmp	r5, #0
	beq	.L1942
	ldr	r2, [r6, #36]
	mov	r0, r4
	add	r2, r1, r2
	bl	FlashReadDpCmd
	b	.L1943
.L1942:
	mov	r0, r4
	bl	FlashReadCmd
.L1943:
	mov	r0, r4
	bl	NandcWaitFlashReady
	cmp	r5, #0
	beq	.L1941
	mov	r0, r4
	ldr	r1, [sp, #44]
	bl	FlashReadDpDataOutCmd
.L1941:
	ldr	r3, [r7, #12]
	mov	r0, r4
	ldr	r2, [sp, #16]
	mov	r1, #0
	str	r3, [sp]
	ldr	r3, [r7, #8]
	bl	NandcXferData
	ldrb	r2, [r6, #40]	@ zero_extendqisi2
	adds	r2, r2, #0
	movne	r2, #1
	cmn	r0, #1
	mov	ip, r0
	movne	r2, #0
	cmp	r2, #0
	movne	r3, #0
	strneb	r3, [r6, #40]
	movne	r5, r3
	bne	.L1939
.L1944:
	cmp	r5, #0
	beq	.L1945
	ldr	r3, .L2005+4
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
.L1945:
	mov	r0, r4
	str	ip, [sp, #32]
	bl	NandcFlashDeCs
	ldr	ip, [sp, #32]
	ldrb	r3, [sp, #28]	@ zero_extendqisi2
	cmn	ip, #1
	strb	r3, [r6, #40]
	bne	.L1952
	ldrb	r3, [r6, #2252]	@ zero_extendqisi2
	cmp	r3, #0
	bne	.L1947
.L1951:
	ldr	r5, [r10, #1704]
	cmp	r5, #0
	bne	.L1948
	b	.L2004
.L1947:
	ldr	r3, [r6, #84]
	mov	r0, r4
	ldr	r1, [sp, #44]
	ldr	r5, [r3, #304]
	mov	r3, #1
	str	r3, [sp]
	ldr	r2, [r7, #8]
	ldr	r3, [r7, #12]
	bl	FlashDdrTunningRead
	cmn	r0, #1
	mov	ip, r0
	beq	.L1950
	ldrb	r3, [r6, #2312]	@ zero_extendqisi2
	cmp	r0, r3, lsr #1
	bls	.L1970
.L1950:
	ubfx	r0, r5, #8, #8
	str	ip, [sp, #32]
	bl	NandcSetDdrPara
	ldr	ip, [sp, #32]
	cmn	ip, #1
	beq	.L1951
	b	.L1970
.L1948:
	mov	r0, r4
	ldr	r1, [sp, #44]
	ldr	r2, [r7, #8]
	ldr	r3, [r7, #12]
	blx	r5
	cmn	r0, #1
	mov	ip, r0
	bne	.L1972
	ldr	r3, [r6, #44]
	ldrb	r3, [r3, #19]	@ zero_extendqisi2
	sub	r3, r3, #1
	cmp	r3, #7
	bhi	.L1954
	mov	r0, r4
	ldrb	r1, [r6, #1213]	@ zero_extendqisi2
	ldr	r2, .L2005+12
	mov	r3, #0
	bl	HynixSetRRPara
.L1954:
	ldr	r1, [sp, #44]
	mov	r0, r4
	ldr	r2, [r7, #8]
	ldr	r3, [r7, #12]
	bl	FlashReadRawPage
	ldr	r1, [r7, #4]
	ldrb	r2, [r6, #2312]	@ zero_extendqisi2
	mov	ip, r0
	ldr	r0, .L2005+16
	mov	r3, ip
	str	ip, [sp, #32]
	bl	printk
	ldr	ip, [sp, #32]
	cmn	ip, #1
	bne	.L1972
	ldrb	r5, [r6, #148]	@ zero_extendqisi2
	cmp	r5, #0
	beq	.L1953
	ldr	r3, [sp, #12]
	mov	r0, r4
	cmp	r3, #0
	beq	.L1955
	bl	flash_enter_slc_mode
	b	.L1956
.L1955:
	bl	flash_exit_slc_mode
.L1956:
	ldr	ip, [r10, #1704]
	mov	r0, r4
	ldr	r1, [sp, #44]
	ldr	r2, [r7, #8]
	ldr	r3, [r7, #12]
	blx	ip
	mov	ip, r0
	b	.L1972
.L2004:
	mov	r0, r4
	ldr	r1, [sp, #44]
	ldr	r2, [r7, #8]
	ldr	r3, [r7, #12]
	bl	FlashReadRawPage
	mov	ip, r0
	b	.L1953
.L1970:
	mov	r5, #0
.L1952:
	ldrb	r3, [r6, #2312]	@ zero_extendqisi2
	add	r3, r3, r3, asl #1
	cmp	ip, r3, asr #2
	bls	.L1953
	ldr	r3, [r10, #1704]
	cmp	r3, #0
	moveq	ip, #256
	b	.L1953
.L1972:
	mov	r5, #0
.L1953:
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
	bne	.L1960
	ldr	r1, [r7, #4]
	ldr	r0, .L2005+20
	ldrb	r2, [r6, #2312]	@ zero_extendqisi2
	bl	printk
	ldr	r1, [r7, #12]
	cmp	r1, #0
	beq	.L1960
	mov	r2, #4
	ldr	r0, .L2005+24
	mov	r3, r2
	bl	rknand_print_hex
.L1960:
	cmp	r5, #0
	beq	.L1962
	ldrb	r3, [r6, #2312]	@ zero_extendqisi2
	add	r3, r3, r3, asl #1
	cmp	r8, r3, asr #2
	bls	.L1963
	ldr	r3, [r10, #1704]
	cmp	r3, #0
	moveq	r8, #256
.L1963:
	ldr	r3, [sp, #8]
	cmp	r8, #256
	cmnne	r8, #1
	add	r3, r3, #36
	movne	r2, #0
	streq	r8, [r9, r3]
	strne	r2, [r9, r3]
.L1962:
	ldr	r3, [sp, #12]
	add	fp, fp, r5
	cmp	r3, #0
	beq	.L1934
	ldrb	r3, [r6, #148]	@ zero_extendqisi2
	cmp	r3, #0
	beq	.L1934
	mov	r0, r4
	bl	flash_exit_slc_mode
.L1934:
	add	fp, fp, #1
	b	.L1931
.L2003:
	mov	r0, #0
.L2000:
	add	sp, sp, #52
	@ sp needed
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L2006:
	.align	2
.L2005:
	.word	.LANCHOR1
	.word	.LANCHOR0
	.word	.LANCHOR2
	.word	.LANCHOR0+1216
	.word	.LC102
	.word	.LC99
	.word	.LC101
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
	mov	r2, #16
	ldr	r8, .L2020
	.pad #60
	sub	sp, sp, #60
	ldr	r4, .L2020+4
	mov	r1, #0
	mov	r5, #0
	mvn	fp, #0
	ldrh	r3, [r8, #134]
	add	r0, r4, #1744
	ldrh	r6, [r8, #132]
	mov	r9, r5
	mov	r10, r4
	smulbb	r6, r6, r3
	bl	ftl_memset
	ldr	r3, [r4, #1728]
	uxth	r6, r6
	add	r2, r6, fp
	str	r3, [sp, #32]
	uxth	r3, r2
	str	r5, [sp, #28]
	str	r3, [sp, #4]
.L2008:
	ldrb	r3, [r8, #2230]	@ zero_extendqisi2
	uxtb	r7, r5
	cmp	r3, r7
	bls	.L2019
	mul	ip, r6, r7
	ldr	r4, [sp, #4]
	sub	r3, r6, #12
.L2009:
	cmp	r4, r3
	ble	.L2011
	add	r2, ip, r4
	mov	r1, #1
	add	r0, sp, #20
	str	r3, [sp, #12]
	mov	r2, r2, asl #10
	str	r2, [sp, #24]
	mov	r2, r1
	str	ip, [sp, #8]
	bl	FlashReadPages
	ldr	r2, [sp, #20]
	ldr	ip, [sp, #8]
	cmn	r2, #1
	ldr	r3, [sp, #12]
	beq	.L2010
	ldr	r2, [r10, #1728]
	ldrh	r1, [r2]
	movw	r2, #61664
	cmp	r1, r2
	bne	.L2010
	mov	r1, r7
	ldr	r0, .L2020+8
	mov	r2, r4
	mov	r7, r7, asl #1
	bl	printk
	ldr	r3, .L2020+12
	add	r9, r9, #1
	strh	r4, [r3, r7]	@ movhi
	uxth	r9, r9
	b	.L2011
.L2010:
	sub	r4, r4, #1
	uxth	r4, r4
	b	.L2009
.L2011:
	ldrb	r3, [r8, #2230]	@ zero_extendqisi2
	add	r5, r5, #1
	cmp	r3, r9
	moveq	fp, #0
	b	.L2008
.L2019:
	mov	r0, fp
	add	sp, sp, #60
	@ sp needed
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L2021:
	.align	2
.L2020:
	.word	.LANCHOR0
	.word	.LANCHOR2
	.word	.LC103
	.word	.LANCHOR2+1744
	.fnend
	.size	FlashLoadFactorBbt, .-FlashLoadFactorBbt
	.align	2
	.global	FlashProgSlc2KPages
	.type	FlashProgSlc2KPages, %function
FlashProgSlc2KPages:
	.fnstart
	@ args = 0, pretend = 0, frame = 56
	@ frame_needed = 0, uses_anonymous_args = 0
	ldr	r3, .L2052
	stmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.save {r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.pad #68
	sub	sp, sp, #68
	ldr	r8, .L2052+4
	mov	r6, r1
	ldrb	r3, [r3, #481]	@ zero_extendqisi2
	mov	r9, r2
	mov	r4, r0
	mov	r10, r0
	mov	r7, #0
	mov	fp, r8
	str	r3, [sp, #12]
.L2023:
	cmp	r7, r6
	beq	.L2050
	rsb	r3, r7, r6
	add	r2, sp, #20
	mov	r0, r10
	mov	r1, r9
	uxtb	r3, r3
	str	r3, [sp]
	add	r3, sp, #24
	bl	LogAddr2PhyAddr
	ldrb	r2, [r8, #2230]	@ zero_extendqisi2
	ldr	r3, [sp, #24]
	cmp	r3, r2
	mvncs	r3, #0
	strcs	r3, [r10]
	bcs	.L2025
	add	r3, r8, r3
	ldrb	r5, [r3, #2232]	@ zero_extendqisi2
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
.L2025:
	add	r7, r7, #1
	add	r10, r10, #36
	b	.L2023
.L2050:
	ldr	r5, .L2052+8
	mov	r7, #0
	mov	r8, r5
.L2030:
	cmp	r7, r6
	beq	.L2051
	ldr	r3, [r4]
	cmn	r3, #1
	bne	.L2031
	ldr	r1, [r4, #4]
	ldr	r0, .L2052+12
	bl	printk
	b	.L2032
.L2031:
	rsb	r3, r7, r6
	mov	r1, r9
	add	r2, sp, #20
	mov	r0, r4
	uxtb	r3, r3
	str	r3, [sp]
	add	r3, sp, #24
	bl	LogAddr2PhyAddr
	ldr	r2, [r5, #1732]
	mov	r3, #0
	mov	lr, r4
	str	r3, [r2]
	ldr	r2, [r5, #1736]
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
	ldr	r3, [r5, #1732]
	str	r3, [sp, #36]
	ldr	r3, [r5, #1736]
	str	r3, [sp, #40]
	bl	FlashReadPages
	ldr	r10, [sp, #28]
	cmn	r10, #1
	bne	.L2033
	ldr	r0, .L2052+16
	ldr	r1, [r4, #4]
	bl	printk
	str	r10, [r4]
.L2033:
	ldr	r10, [sp, #28]
	cmp	r10, #256
	bne	.L2034
	ldr	r0, .L2052+20
	ldr	r1, [r4, #4]
	bl	printk
	str	r10, [r4]
.L2034:
	ldr	r3, [r4, #12]
	cmp	r3, #0
	beq	.L2035
	ldr	r2, [r3]
	ldr	r3, [r8, #1736]
	ldr	r3, [r3]
	cmp	r2, r3
	beq	.L2035
	ldr	r0, .L2052+24
	ldr	r1, [r4, #4]
	bl	printk
	mvn	r3, #0
	str	r3, [r4]
.L2035:
	ldr	r3, [r4, #8]
	cmp	r3, #0
	beq	.L2032
	ldr	r2, [r3]
	ldr	r3, [r8, #1732]
	ldr	r3, [r3]
	cmp	r2, r3
	beq	.L2032
	ldr	r0, .L2052+28
	ldr	r1, [r4, #4]
	bl	printk
	mvn	r3, #0
	str	r3, [r4]
.L2032:
	add	r7, r7, #1
	add	r4, r4, #36
	b	.L2030
.L2051:
	mov	r0, #0
	add	sp, sp, #68
	@ sp needed
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L2053:
	.align	2
.L2052:
	.word	.LANCHOR1
	.word	.LANCHOR0
	.word	.LANCHOR2
	.word	.LC104
	.word	.LC105
	.word	.LC106
	.word	.LC107
	.word	.LC108
	.fnend
	.size	FlashProgSlc2KPages, .-FlashProgSlc2KPages
	.align	2
	.global	FtlLoadFactoryBbt
	.type	FtlLoadFactoryBbt, %function
FtlLoadFactoryBbt:
	.fnstart
	@ args = 0, pretend = 0, frame = 8
	@ frame_needed = 0, uses_anonymous_args = 0
	stmfd	sp!, {r0, r1, r2, r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.save {r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.pad #12
	mov	r7, #0
	ldr	r5, .L2066
	ldr	r6, .L2066+4
	ldr	r3, [r5, #-524]
	sub	r9, r6, #76
	ldr	r8, [r5, #-500]
	sub	r10, r6, #120
	mov	fp, r9
	str	r3, [r5, #1768]
	str	r8, [r5, #1772]
.L2055:
	ldrh	r3, [r10]
	cmp	r7, r3
	bcs	.L2065
	ldrh	r4, [r9]
	mvn	r3, #0
	movw	ip, #61664
	strh	r3, [r6, #2]!	@ movhi
	add	r4, r4, r3
	uxth	r4, r4
.L2056:
	ldrh	r3, [fp]
	sub	r2, r3, #16
	cmp	r4, r2
	ble	.L2058
	mla	r3, r3, r7, r4
	mov	r1, #1
	ldr	r0, .L2066+8
	mov	r2, r1
	str	ip, [sp, #4]
	mov	r3, r3, asl #10
	str	r3, [r5, #1764]
	bl	FlashReadPages
	ldr	r3, [r5, #1760]
	cmn	r3, #1
	ldr	ip, [sp, #4]
	beq	.L2057
	ldrh	r3, [r8]
	cmp	r3, ip
	streqh	r4, [r6]	@ movhi
	beq	.L2058
.L2057:
	sub	r4, r4, #1
	uxth	r4, r4
	b	.L2056
.L2058:
	add	r7, r7, #1
	b	.L2055
.L2065:
	mov	r0, #0
	add	sp, sp, #12
	@ sp needed
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L2067:
	.align	2
.L2066:
	.word	.LANCHOR2
	.word	.LANCHOR0+2462
	.word	.LANCHOR2+1760
	.fnend
	.size	FtlLoadFactoryBbt, .-FtlLoadFactoryBbt
	.align	2
	.global	FtlGetLastWrittenPage
	.type	FtlGetLastWrittenPage, %function
FtlGetLastWrittenPage:
	.fnstart
	@ args = 0, pretend = 0, frame = 104
	@ frame_needed = 0, uses_anonymous_args = 0
	cmp	r1, #1
	ldr	r3, .L2081
	stmfd	sp!, {r4, r5, r6, r7, r8, lr}
	.save {r4, r5, r6, r7, r8, lr}
	movweq	r2, #2390
	movwne	r2, #2388
	.pad #104
	sub	sp, sp, #104
	ldrh	r4, [r3, r2]
	mov	r8, r1
	add	r3, sp, #40
	mov	r6, r0, asl #10
	sub	r5, r4, #1
	str	r3, [sp, #16]
	add	r0, sp, #4
	mov	r1, #1
	uxth	r5, r5
	mov	r2, r8
	mov	r7, #0
	str	r7, [sp, #12]
	sxth	r3, r5
	orr	r3, r3, r6
	str	r3, [sp, #8]
	bl	FlashReadPages
	ldr	r3, [sp, #40]
	cmn	r3, #1
	bne	.L2071
.L2072:
	sxth	r4, r7
	sxth	r3, r5
	cmp	r4, r3
	bgt	.L2071
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
	bne	.L2073
	ldr	r3, [sp, #44]
	cmn	r3, #1
	bne	.L2073
	ldr	r3, [sp, #4]
	cmn	r3, #1
	subne	r4, r4, #1
	uxthne	r5, r4
	bne	.L2072
.L2073:
	add	r3, r4, #1
	uxth	r7, r3
	b	.L2072
.L2071:
	sxth	r0, r5
	add	sp, sp, #104
	@ sp needed
	ldmfd	sp!, {r4, r5, r6, r7, r8, pc}
.L2082:
	.align	2
.L2081:
	.word	.LANCHOR0
	.fnend
	.size	FtlGetLastWrittenPage, .-FtlGetLastWrittenPage
	.align	2
	.global	FtlLoadBbt
	.type	FtlLoadBbt, %function
FtlLoadBbt:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	stmfd	sp!, {r3, r4, r5, r6, r7, r8, r9, lr}
	.save {r3, r4, r5, r6, r7, r8, r9, lr}
	ldr	r4, .L2116
	ldr	r7, .L2116+4
	ldr	r8, .L2116+8
	ldr	r3, [r4, #-524]
	mov	r9, r4
	ldr	r6, [r4, #-500]
	str	r3, [r4, #1768]
	str	r6, [r4, #1772]
	bl	FtlBbtMemInit
	movw	r3, #2386
	ldrh	r5, [r7, r3]
	sub	r5, r5, #1
	uxth	r5, r5
.L2084:
	ldrh	r3, [r8]
	sub	r3, r3, #48
	cmp	r5, r3
	ble	.L2087
	mov	r1, #1
	ldr	r0, .L2116+12
	mov	r2, r1
	mov	r3, r5, asl #10
	str	r3, [r4, #1764]
	bl	FlashReadPages
	ldr	r3, [r4, #1760]
	cmn	r3, #1
	bne	.L2085
	ldr	r3, [r9, #1764]
	mov	r1, #1
	ldr	r0, .L2116+12
	mov	r2, r1
	add	r3, r3, #1
	str	r3, [r9, #1764]
	bl	FlashReadPages
.L2085:
	ldr	r3, [r4, #1760]
	cmn	r3, #1
	beq	.L2086
	ldrh	r2, [r6]
	movw	r3, #61649
	cmp	r2, r3
	bne	.L2086
	movw	r3, #2452
	strh	r5, [r7, r3]	@ movhi
	ldr	r3, [r6, #4]
	str	r3, [r7, #2460]
	ldr	r3, .L2116+16
	ldrh	r2, [r6, #8]
	strh	r2, [r3, #4]	@ movhi
	b	.L2087
.L2086:
	sub	r5, r5, #1
	uxth	r5, r5
	b	.L2084
.L2087:
	movw	r9, #2452
	movw	r2, #65535
	ldrh	r3, [r7, r9]
	ldr	r8, .L2116+4
	cmp	r3, r2
	ldr	r5, .L2116+16
	beq	.L2101
	ldrh	r3, [r5, #4]
	cmp	r3, r2
	beq	.L2091
	mov	r1, #1
	ldr	r0, .L2116+12
	mov	r2, r1
	mov	r3, r3, asl #10
	str	r3, [r4, #1764]
	bl	FlashReadPages
	ldr	r3, [r4, #1760]
	cmn	r3, #1
	beq	.L2091
	ldrh	r2, [r6]
	movw	r3, #61649
	cmp	r2, r3
	bne	.L2091
	ldr	r3, [r6, #4]
	ldr	r2, [r8, #2460]
	cmp	r3, r2
	strhi	r3, [r8, #2460]
	ldrhih	r2, [r5, #4]
	ldrhih	r3, [r6, #8]
	strhih	r2, [r8, r9]	@ movhi
	strhih	r3, [r5, #4]	@ movhi
.L2091:
	movw	r3, #2452
	mov	r1, #1
	ldrh	r0, [r7, r3]
	movw	r8, #61649
	bl	FtlGetLastWrittenPage
	uxth	r7, r0
	add	r0, r0, #1
	strh	r0, [r5, #2]	@ movhi
.L2093:
	sxth	r3, r7
	cmp	r3, #0
	blt	.L2098
	ldrh	r2, [r5]
	mov	r1, #1
	ldr	r0, .L2116+12
	orr	r3, r3, r2, asl #10
	str	r3, [r4, #1764]
	ldr	r3, [r4, #-524]
	mov	r2, r1
	str	r3, [r4, #1768]
	bl	FlashReadPages
	ldr	r3, [r4, #1760]
	cmn	r3, #1
	beq	.L2094
	ldrh	r3, [r6]
	cmp	r3, r8
	bne	.L2094
.L2098:
	ldrh	r2, [r6, #10]
	ldrh	r0, [r6, #12]
	ldr	r3, .L2116+4
	strh	r2, [r5, #6]	@ movhi
	movw	r2, #65535
	cmp	r0, r2
	bne	.L2095
	b	.L2096
.L2094:
	sub	r7, r7, #1
	uxth	r7, r7
	b	.L2093
.L2095:
	ldr	r2, [r3, #2316]
	cmp	r0, r2
	beq	.L2096
	movw	r1, #2330
	ldrh	r3, [r3, r1]
	mov	r3, r3, lsr #2
	cmp	r0, r3
	cmpcc	r2, r3
	bcs	.L2096
	bl	FtlSysBlkNumInit
.L2096:
	ldr	r6, .L2116+20
	mov	r5, #0
	ldr	r8, .L2116+24
	sub	r7, r6, #134
.L2099:
	ldrh	r3, [r7]
	cmp	r5, r3
	bcs	.L2115
	ldrh	r2, [r8]
	ldr	r1, [r4, #1768]
	ldr	r0, [r6, #4]!
	mov	r2, r2, asl #2
	mla	r1, r5, r2, r1
	bl	ftl_memcpy
	add	r5, r5, #1
	b	.L2099
.L2115:
	mov	r0, #0
	ldmfd	sp!, {r3, r4, r5, r6, r7, r8, r9, pc}
.L2101:
	mvn	r0, #0
	ldmfd	sp!, {r3, r4, r5, r6, r7, r8, r9, pc}
.L2117:
	.align	2
.L2116:
	.word	.LANCHOR2
	.word	.LANCHOR0
	.word	.LANCHOR0+2386
	.word	.LANCHOR2+1760
	.word	.LANCHOR0+2452
	.word	.LANCHOR0+2476
	.word	.LANCHOR2-436
	.fnend
	.size	FtlLoadBbt, .-FtlLoadBbt
	.align	2
	.global	FtlScanSysBlk
	.type	FtlScanSysBlk, %function
FtlScanSysBlk:
	.fnstart
	@ args = 0, pretend = 0, frame = 32
	@ frame_needed = 0, uses_anonymous_args = 0
	stmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.save {r4, r5, r6, r7, r8, r9, r10, fp, lr}
	mov	r5, #0
	ldr	r7, .L2204
	mov	r1, r5
	ldr	r4, .L2204+4
	.pad #36
	sub	sp, sp, #36
	movw	r6, #2408
	ldr	r2, [r7, #2416]
	sub	r3, r4, #388
	ldr	r0, [r4, #-444]
	mov	r8, r4
	strh	r5, [r3]	@ movhi
	movw	r3, #2434
	mov	r2, r2, asl #2
	strh	r5, [r7, r3]	@ movhi
	bl	ftl_memset
	ldr	r2, [r7, #2416]
	mov	r1, r5
	ldr	r0, [r4, #-472]
	mov	r2, r2, asl #1
	bl	ftl_memset
	ldrh	r2, [r7, r6]
	mov	r1, r5
	ldr	r0, [r4, #-460]
	mov	r2, r2, asl #2
	bl	ftl_memset
	ldrh	r2, [r7, r6]
	ldr	r0, [r7, #2436]
	mov	r1, r5
	mov	r2, r2, asl #1
	bl	ftl_memset
	ldr	r0, .L2204+8
	mov	r1, #255
	mov	r2, #16
	bl	ftl_memset
	movw	r3, #2328
	ldrh	r3, [r7, r3]
	str	r7, [sp, #16]
	mov	r7, r4
	str	r3, [sp, #4]
.L2119:
	ldr	r3, .L2204+12
	ldr	r2, [sp, #4]
	ldr	r1, .L2204
	ldrh	r3, [r3]
	cmp	r3, r2
	mov	r0, r1
	bls	.L2160
	add	r3, r1, #2320
	add	r1, r1, #2400
	ldr	r0, [r4, #-2692]
	mov	r5, #0
	ldrh	ip, [r3]
	sub	r6, r1, #52
	ldr	r3, [r4, #-536]
	mov	fp, r5
	ldr	r2, [r4, #-2696]
	mov	r9, #36
	ldrh	r10, [r1]
	str	r0, [sp, #8]
.L2161:
	uxth	r1, r5
	cmp	r1, ip
	bcs	.L2199
	ldr	r1, [sp, #4]
	ldrb	r0, [r6, r5]	@ zero_extendqisi2
	str	r2, [sp, #28]
	str	r3, [sp, #24]
	str	ip, [sp, #20]
	bl	V2P_block
	str	r0, [sp, #12]
	bl	FtlBbmIsBadBlock
	cmp	r0, #0
	ldr	r1, [sp, #12]
	ldr	ip, [sp, #20]
	ldr	r3, [sp, #24]
	ldr	r2, [sp, #28]
	bne	.L2120
	mla	r0, r9, fp, r3
	mov	r1, r1, asl #10
	stmib	r0, {r1, r2}
	mul	r1, r10, fp
	add	lr, r1, #3
	cmp	r1, #0
	movlt	r1, lr
	ldr	lr, [sp, #8]
	bic	r1, r1, #3
	add	r1, lr, r1
	str	r1, [r0, #12]
	add	r0, fp, #1
	uxth	fp, r0
.L2120:
	add	r5, r5, #1
	b	.L2161
.L2199:
	cmp	fp, #0
	beq	.L2123
	ldr	r0, [r4, #-536]
	mov	r1, fp
	mov	r2, #1
	bl	FlashReadPages
	mov	r3, #0
.L2197:
	str	r3, [sp, #8]
	ldrh	r3, [sp, #8]
	cmp	r3, fp
	bcs	.L2123
	ldr	r3, [sp, #8]
	mov	r9, #36
	mul	r9, r9, r3
	ldr	r3, [r4, #-536]
	add	r2, r3, r9
	ldr	r3, [r3, r9]
	ldr	r5, [r2, #4]
	cmn	r3, #1
	ldr	r6, [r2, #12]
	ubfx	r5, r5, #10, #16
	bne	.L2126
	mov	r10, #16
	movw	ip, #65535
.L2125:
	ldr	r0, [r4, #-536]
	mov	r1, #1
	mov	r2, r1
	str	ip, [sp, #12]
	add	r0, r0, r9
	ldr	r3, [r0, #4]
	add	r3, r3, #1
	str	r3, [r0, #4]
	bl	FlashReadPages
	ldrh	r3, [r6]
	ldr	ip, [sp, #12]
	cmp	r3, ip
	ldreq	r3, [r8, #-536]
	mvneq	r2, #0
	streq	r2, [r3, r9]
	beq	.L2126
.L2127:
	ldr	r3, [r7, #-536]
	ldr	r3, [r3, r9]
	cmn	r3, #1
	bne	.L2126
	sub	r10, r10, #1
	uxth	r10, r10
	cmp	r10, #0
	bne	.L2125
.L2126:
	ldr	r3, [r7, #-536]
	ldr	r3, [r3, r9]
	cmn	r3, #1
	beq	.L2129
	ldr	r2, [r7, #-3332]
	ldr	r3, [r6, #4]
	cmn	r2, #1
	beq	.L2130
	cmp	r2, r3
	bhi	.L2131
.L2130:
	cmn	r3, #1
	addne	r2, r3, #1
	strne	r2, [r8, #-3332]
.L2131:
	ldrh	r2, [r6]
	movw	r1, #61604
	cmp	r2, r1
	beq	.L2133
	bhi	.L2134
	movw	r3, #61574
	cmp	r2, r3
	bne	.L2132
	ldr	r2, .L2204+16
	ldr	lr, [r8, #-460]
	ldrh	ip, [r2]
	ldrh	r1, [r2, #26]
	sub	r0, ip, #1
	rsb	r2, r1, r0
	str	r2, [sp, #12]
	uxth	r3, r0
	b	.L2147
.L2134:
	movw	r3, #61634
	cmp	r2, r3
	beq	.L2136
	movw	r3, #65535
	cmp	r2, r3
	moveq	r0, r5
	beq	.L2198
	b	.L2132
.L2136:
	ldr	r3, [sp, #16]
	ldr	ip, .L2204+20
	ldr	r9, [r4, #-444]
	ldr	lr, [r3, #2416]
	ldrh	r2, [ip]
	uxth	r1, lr
	sub	r3, r1, #1
	rsb	r1, r2, r1
	sub	r1, r1, #1
	uxth	r3, r3
	sxth	r1, r1
	str	r1, [sp, #20]
.L2138:
	ldr	r1, [sp, #20]
	sxth	r0, r3
	cmp	r0, r1
	ble	.L2200
	mov	r10, r0, asl #2
	ldr	r1, [r6, #4]
	str	r10, [sp, #12]
	ldr	r10, [r9, r0, asl #2]
	cmp	r1, r10
	bls	.L2139
	ldr	r1, [r9]
	cmp	r1, #0
	bne	.L2140
	cmp	r2, lr
	addne	r2, r2, #1
	strneh	r2, [ip]	@ movhi
.L2140:
	uxth	r9, r3
	mov	r1, #0
.L2141:
	uxth	r2, r1
	cmp	r2, r9
	bcs	.L2201
	ldr	lr, [r7, #-444]
	sxth	r2, r2
	add	r1, r1, #1
	add	r10, lr, r2, asl #2
	ldr	r10, [r10, #4]
	str	r10, [lr, r2, asl #2]
	mov	r2, r2, asl #1
	ldr	lr, [r7, #-472]
	add	r10, lr, r2
	ldrh	r10, [r10, #2]
	strh	r10, [lr, r2]	@ movhi
	b	.L2141
.L2201:
	ldr	r2, [r4, #-444]
	mov	r0, r0, asl #1
	ldr	r1, [r6, #4]
	ldr	lr, [sp, #12]
	str	r1, [r2, lr]
	ldr	r2, [r4, #-472]
	strh	r5, [r2, r0]	@ movhi
	sxth	r0, r3
	cmp	r0, #0
	bge	.L2143
	b	.L2132
.L2139:
	sub	r3, r3, #1
	uxth	r3, r3
	b	.L2138
.L2200:
	cmp	r0, #0
	bge	.L2174
	b	.L2132
.L2143:
	ldr	r1, .L2204
	ldrh	r2, [ip]
	ldr	r1, [r1, #2416]
	rsb	r1, r2, r1
	sub	r1, r1, #1
	sxth	r1, r1
	cmp	r0, r1
	bgt	.L2132
.L2174:
	add	r2, r2, #1
	ldr	r1, [r6, #4]
	strh	r2, [ip]	@ movhi
	sxth	r3, r3
	ldr	r2, [r4, #-444]
	str	r1, [r2, r3, asl #2]
	mov	r3, r3, asl #1
	ldr	r2, [r4, #-472]
	b	.L2195
.L2153:
	ldr	r9, [lr, r2, asl #2]
	mov	r10, r2, asl #2
	ldr	r0, [r6, #4]
	cmp	r0, r9
	bhi	.L2202
	sub	r3, r3, #1
	uxth	r3, r3
.L2147:
	ldr	r0, [sp, #12]
	sxth	r2, r3
	cmp	r2, r0
	bgt	.L2153
	b	.L2152
.L2202:
	ldr	r0, [lr]
	cmp	r0, #0
	bne	.L2149
	cmp	r1, ip
	addne	r1, r1, #1
	ldrne	r0, .L2204+24
	strneh	r1, [r0]	@ movhi
.L2149:
	uxth	lr, r3
	mov	r0, #0
.L2150:
	uxth	r1, r0
	cmp	r1, lr
	bcs	.L2203
	ldr	ip, [r4, #-460]
	sxth	r1, r1
	add	r0, r0, #1
	add	r9, ip, r1, asl #2
	ldr	r9, [r9, #4]
	str	r9, [ip, r1, asl #2]
	mov	r1, r1, asl #1
	ldr	ip, .L2204
	ldr	ip, [ip, #2436]
	add	r9, ip, r1
	ldrh	r9, [r9, #2]
	strh	r9, [ip, r1]	@ movhi
	b	.L2150
.L2203:
	ldr	r1, [r8, #-460]
	mov	r2, r2, asl #1
	ldr	r0, [r6, #4]
	str	r0, [r1, r10]
	ldr	r1, .L2204
	ldr	r1, [r1, #2436]
	strh	r5, [r1, r2]	@ movhi
.L2152:
	sxth	r3, r3
	cmp	r3, #0
	blt	.L2132
	ldr	r0, .L2204+24
	ldrh	r2, [r0, #-26]
	ldrh	r1, [r0]
	sub	r2, r2, #1
	rsb	r2, r1, r2
	sxth	r2, r2
	cmp	r3, r2
	bgt	.L2132
	add	r1, r1, #1
	ldr	r2, [r8, #-460]
	strh	r1, [r0]	@ movhi
	ldr	r1, [r6, #4]
	str	r1, [r2, r3, asl #2]
	mov	r3, r3, asl #1
	ldr	r2, .L2204
	ldr	r2, [r2, #2436]
.L2195:
	strh	r5, [r2, r3]	@ movhi
	b	.L2132
.L2133:
	ldr	r2, .L2204+8
	movw	r1, #65535
	ldrh	r0, [r2]
	cmp	r0, r1
	streqh	r5, [r2]	@ movhi
	beq	.L2196
	ldrh	r0, [r2, #4]
	cmp	r0, r1
	beq	.L2155
	mov	r1, #1
	bl	FtlFreeSysBlkQueueIn
.L2155:
	ldr	r3, [r6, #4]
	ldr	r2, [r8, #-3292]
	cmp	r2, r3
	ldr	r3, .L2204+8
	strcsh	r5, [r3, #4]	@ movhi
	bcs	.L2132
	ldrh	r2, [r3]
	strh	r5, [r3]	@ movhi
	strh	r2, [r3, #4]	@ movhi
	ldr	r3, [r6, #4]
.L2196:
	str	r3, [r7, #-3292]
	b	.L2132
.L2129:
	ldr	r3, .L2204
	mov	r0, r5
	ldrb	r1, [r3, #148]	@ zero_extendqisi2
	cmp	r1, #0
	beq	.L2157
.L2198:
	mov	r1, #0
.L2157:
	bl	FtlFreeSysBlkQueueIn
.L2132:
	ldr	r3, [sp, #8]
	add	r3, r3, #1
	b	.L2197
.L2123:
	ldr	r3, [sp, #4]
	add	r5, r3, #1
	uxth	r3, r5
	str	r3, [sp, #4]
	b	.L2119
.L2160:
	ldr	r3, .L2204+4
	ldr	ip, [r3, #-472]
	ldrh	r2, [ip]
	cmp	r2, #0
	beq	.L2162
.L2165:
	ldr	r0, [r0, #2436]
	ldr	r3, .L2204
	ldrh	r2, [r0]
	cmp	r2, #0
	beq	.L2163
	b	.L2186
.L2162:
	sub	r3, r3, #388
	ldrh	r3, [r3]
	cmp	r3, #0
	ldrne	lr, [r1, #2416]
	beq	.L2165
.L2166:
	uxth	r3, r2
	sxth	r1, r3
	cmp	r1, lr
	bcs	.L2165
	mov	r5, r1, asl #1
	add	r2, r2, #1
	ldrh	r5, [ip, r5]
	cmp	r5, #0
	beq	.L2166
	mov	r6, #0
.L2167:
	ldr	ip, [r0, #2416]
	sxth	r2, r3
	cmp	r2, ip
	bcs	.L2165
	ldr	lr, [r4, #-472]
	mov	ip, r2, asl #1
	rsb	r5, r1, r2
	add	r3, r3, #1
	ldrh	r8, [lr, ip]
	mov	r7, r5, asl #1
	uxth	r3, r3
	strh	r8, [lr, r7]	@ movhi
	ldr	lr, [r4, #-444]
	ldr	r2, [lr, r2, asl #2]
	str	r2, [lr, r5, asl #2]
	ldr	r2, [r4, #-472]
	strh	r6, [r2, ip]	@ movhi
	b	.L2167
.L2163:
	movw	r1, #2434
	ldrh	r1, [r3, r1]
	cmp	r1, #0
	movwne	r1, #2408
	ldrneh	ip, [r3, r1]
	beq	.L2186
.L2170:
	uxth	r3, r2
	sxth	r1, r3
	cmp	r1, ip
	bge	.L2186
	mov	lr, r1, asl #1
	add	r2, r2, #1
	ldrh	lr, [r0, lr]
	cmp	lr, #0
	beq	.L2170
	ldr	r0, .L2204
	mov	r7, #0
	ldr	r6, .L2204+16
.L2171:
	ldrh	ip, [r6]
	sxth	r2, r3
	cmp	r2, ip
	bge	.L2186
	ldr	lr, [r0, #2436]
	mov	ip, r2, asl #1
	rsb	r5, r1, r2
	add	r3, r3, #1
	ldrh	r9, [lr, ip]
	mov	r8, r5, asl #1
	uxth	r3, r3
	strh	r9, [lr, r8]	@ movhi
	ldr	lr, [r4, #-460]
	ldr	r2, [lr, r2, asl #2]
	str	r2, [lr, r5, asl #2]
	ldr	r2, [r0, #2436]
	strh	r7, [r2, ip]	@ movhi
	b	.L2171
.L2186:
	mov	r0, #0
	add	sp, sp, #36
	@ sp needed
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L2205:
	.align	2
.L2204:
	.word	.LANCHOR0
	.word	.LANCHOR2
	.word	.LANCHOR2-3300
	.word	.LANCHOR0+2330
	.word	.LANCHOR0+2408
	.word	.LANCHOR2-388
	.word	.LANCHOR0+2434
	.fnend
	.size	FtlScanSysBlk, .-FtlScanSysBlk
	.align	2
	.global	FtlLoadSysInfo
	.type	FtlLoadSysInfo, %function
FtlLoadSysInfo:
	.fnstart
	@ args = 0, pretend = 0, frame = 8
	@ frame_needed = 0, uses_anonymous_args = 0
	stmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.save {r4, r5, r6, r7, r8, r9, r10, fp, lr}
	mov	r1, #0
	ldr	r4, .L2235
	.pad #36
	sub	sp, sp, #36
	ldr	r6, .L2235+4
	sub	r7, r4, #3296
	ldr	r3, [r4, #-524]
	ldr	r0, [r4, #-3544]
	str	r3, [r4, #1768]
	ldr	r3, [r4, #-500]
	str	r3, [r4, #1772]
	movw	r3, #2328
	ldrh	r2, [r6, r3]
	mov	r2, r2, asl #1
	bl	ftl_memset
	ldrh	r0, [r7, #-4]
	movw	r3, #65535
	cmp	r0, r3
	bne	.L2207
.L2218:
	mvn	r0, #0
	b	.L2208
.L2207:
	mov	r1, #1
	bl	FtlGetLastWrittenPage
	ldr	r3, .L2235+8
	ldrh	r9, [r7, #-4]
	mov	r7, r4
	uxth	r5, r0
	add	r0, r0, #1
	strh	r0, [r3, #2]	@ movhi
.L2209:
	sxth	r3, r5
	cmp	r3, #0
	blt	.L2217
	sxth	fp, r9
	mov	r1, #1
	ldr	r0, .L2235+12
	mov	r2, r1
	orr	r3, r3, fp, asl #10
	str	r3, [r4, #1764]
	ldr	r3, [r4, #-524]
	str	r3, [r4, #1768]
	bl	FlashReadPages
	ldrb	r3, [r6, #32]	@ zero_extendqisi2
	cmp	r3, #0
	beq	.L2210
	ldr	r8, [r7, #1772]
	ldr	r3, [r8, #12]
	cmp	r3, #0
	beq	.L2210
	ldr	r2, [r7, #1768]
	ldr	r10, .L2235+16
	str	r3, [sp, #28]
	mov	r0, r2
	str	r2, [sp, #24]
	ldrh	r1, [r10]
	bl	js_hash
	ldr	r3, [sp, #28]
	cmp	r3, r0
	beq	.L2210
	cmp	r5, #0
	ldr	r2, [sp, #24]
	bne	.L2211
	ldr	ip, .L2235+8
	ldrh	r1, [ip, #4]
	cmp	fp, r1
	beq	.L2211
	ldr	r0, [r8]
	ldrh	r1, [ip]
	str	ip, [sp, #24]
	str	r0, [sp]
	ldr	r0, [r8, #4]
	str	r0, [sp, #4]
	ldr	r0, [r8, #8]
	str	r3, [sp, #12]
	str	r0, [sp, #8]
	ldr	r3, [r2]
	ldr	r0, .L2235+20
	str	r3, [sp, #16]
	ldr	r2, [r7, #1760]
	ldr	r3, [r7, #1764]
	bl	printk
	ldrh	r5, [r10, #-8]
	ldr	ip, [sp, #24]
	ldrh	r9, [ip, #4]
	b	.L2213
.L2211:
	mvn	r3, #0
	str	r3, [r4, #1760]
.L2210:
	ldr	r3, [r4, #1760]
	cmn	r3, #1
	beq	.L2213
	ldr	r3, [r7, #-524]
	ldr	r2, .L2235+24
	ldr	r3, [r3]
	cmp	r3, r2
	bne	.L2213
	ldr	r3, [r7, #-500]
	ldrh	r2, [r3]
	movw	r3, #61604
	cmp	r2, r3
	bne	.L2213
.L2217:
	ldr	r5, .L2235
	mov	r2, #48
	ldr	r1, [r4, #1768]
	movw	r8, #2328
	sub	r0, r5, #3600
	ldr	r7, .L2235+4
	bl	ftl_memcpy
	ldrh	r2, [r6, r8]
	ldr	r1, [r4, #1768]
	ldr	r0, [r4, #-3544]
	add	r1, r1, #48
	mov	r2, r2, asl #1
	bl	ftl_memcpy
	ldrh	r2, [r6, r8]
	ldr	r1, [r4, #1768]
	ldr	r0, [r4, #-3368]
	mov	r3, r2, asl #1
	mov	r2, r2, lsr #3
	add	r3, r3, #51
	add	r2, r2, #4
	bic	r3, r3, #3
	add	r1, r1, r3
	bl	ftl_memcpy
	add	r3, r7, #2432
	ldrh	r3, [r3]
	cmp	r3, #0
	beq	.L2215
	ldrh	r2, [r7, r8]
	ldr	r1, [r5, #1768]
	ldr	r0, [r5, #-448]
	mov	r3, r2, lsr #3
	add	r3, r3, r2, asl #1
	movw	r2, #2424
	ldrh	r2, [r7, r2]
	add	r3, r3, #52
	ubfx	r3, r3, #2, #14
	mov	r2, r2, asl #2
	add	r1, r1, r3, asl #2
	bl	ftl_memcpy
	b	.L2215
.L2213:
	sub	r5, r5, #1
	uxth	r5, r5
	b	.L2209
.L2215:
	ldr	r2, [r4, #-3600]
	ldr	r3, .L2235+24
	ldr	r5, .L2235
	cmp	r2, r3
	bne	.L2218
	sub	r7, r5, #3600
	movw	r1, #2342
	ldrb	r0, [r5, #-3590]	@ zero_extendqisi2
	add	r3, r7, #300
	ldrh	r1, [r6, r1]
	sub	r8, r5, #3296
	ldrh	r2, [r7, #8]
	cmp	r0, r1
	strh	r2, [r3, #6]	@ movhi
	ldr	r3, .L2235+4
	bne	.L2218
	movw	r1, #2388
	movw	r0, #2394
	ldrh	r1, [r3, r1]
	ldrh	r0, [r3, r0]
	add	r3, r3, #2320
	str	r2, [r5, #1796]
	mul	r1, r2, r1
	str	r1, [r3, #128]
	mul	r1, r0, r1
	str	r1, [r3, #108]
	ldr	r1, .L2235+28
	ldrh	r0, [r1, #6]
	ldr	r1, [r3, #12]
	rsb	r0, r0, r1
	ldrh	r1, [r3]
	rsb	r0, r2, r0
	bl	__aeabi_uidiv
	sub	r3, r5, #3520
	add	r2, r7, #76
	sub	lr, r5, #3280
	sub	ip, r5, #3424
	strh	r0, [r8, #-8]	@ movhi
	mov	r8, ip
	ldrh	r0, [r7, #14]
	strh	r0, [r3, #-4]	@ movhi
	ldrh	r3, [r7, #16]
	mov	r1, r3, lsr #6
	and	r3, r3, #63
	strb	r3, [r5, #-3518]
	ldrb	r3, [r5, #-3589]	@ zero_extendqisi2
	strh	r1, [r7, #78]	@ movhi
	ldrh	r1, [r7, #18]
	strb	r3, [r5, #-3516]
	mvn	r3, #0
	strh	r3, [lr, #-4]	@ movhi
	mov	r3, #0
	strh	r3, [r2, #242]	@ movhi
	sub	r2, r5, #3472
	strb	r3, [r5, #-3278]
	strh	r1, [r2, #-4]	@ movhi
	ldrh	r1, [r7, #20]
	strb	r3, [r5, #-3276]
	str	r3, [r5, #-3344]
	mov	r6, r1, lsr #6
	and	r1, r1, #63
	strb	r1, [r5, #-3470]
	ldrb	r1, [r5, #-3588]	@ zero_extendqisi2
	strh	r6, [r2, #-2]	@ movhi
	mov	r6, r2
	strb	r1, [r5, #-3468]
	ldrh	r1, [r7, #22]
	strh	r1, [ip, #-4]	@ movhi
	ldrh	r1, [r7, #24]
	mov	r7, r1, lsr #6
	and	r1, r1, #63
	strb	r1, [r5, #-3422]
	ldrb	r1, [r5, #-3587]	@ zero_extendqisi2
	strh	r7, [r2, #46]	@ movhi
	mov	r7, lr
	strb	r1, [r5, #-3420]
	str	r3, [r5, #-3356]
	ldr	r1, [r5, #-3568]
	str	r3, [r5, #-3364]
	str	r3, [r5, #-3348]
	str	r3, [r5, #-3320]
	str	r3, [r5, #-3312]
	str	r3, [r5, #-3352]
	ldr	r3, [r5, #-3560]
	str	r1, [r5, #-3324]
	ldr	r1, [r5, #-3332]
	ldr	r2, [r4, #-3328]
	cmp	r3, r1
	strhi	r3, [r5, #-3332]
	ldr	r3, [r4, #-3564]
	cmp	r3, r2
	ldrhi	r2, .L2235
	strhi	r3, [r2, #-3328]
	movw	r3, #65535
	cmp	r0, r3
	beq	.L2221
	ldr	r0, .L2235+32
	bl	make_superblock
.L2221:
	ldrh	r2, [r6, #-4]
	movw	r3, #65535
	cmp	r2, r3
	beq	.L2222
	ldr	r0, .L2235+36
	bl	make_superblock
.L2222:
	ldrh	r2, [r8, #-4]
	movw	r3, #65535
	cmp	r2, r3
	beq	.L2223
	ldr	r0, .L2235+40
	bl	make_superblock
.L2223:
	ldrh	r2, [r7, #-4]
	movw	r3, #65535
	cmp	r2, r3
	beq	.L2224
	ldr	r0, .L2235+44
	bl	make_superblock
.L2224:
	mov	r0, #0
.L2208:
	add	sp, sp, #36
	@ sp needed
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L2236:
	.align	2
.L2235:
	.word	.LANCHOR2
	.word	.LANCHOR0
	.word	.LANCHOR2-3300
	.word	.LANCHOR2+1760
	.word	.LANCHOR0+2398
	.word	.LC109
	.word	1179929683
	.word	.LANCHOR0+2452
	.word	.LANCHOR2-3524
	.word	.LANCHOR2-3476
	.word	.LANCHOR2-3428
	.word	.LANCHOR2-3284
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
	movw	r3, #2388
	ldr	r5, .L2251
	.pad #100
	sub	sp, sp, #100
	ubfx	r0, r0, #10, #16
	mov	r4, r1
	ldr	r7, .L2251+4
	ldrh	r9, [r5, r3]
	bl	P2V_block_in_plane
	ldr	r1, .L2251+8
	mov	r6, r0
	ldr	r0, .L2251+12
	bl	printk
	ldr	r2, [r7, #-3544]
	mov	r3, r6, asl #1
	mov	r1, r6
	ldr	r0, .L2251+16
	ldrh	r2, [r2, r3]
	bl	printk
	add	r0, sp, #96
	strh	r6, [r0, #-48]!	@ movhi
	bl	make_superblock
	ldrb	r2, [r5, #148]	@ zero_extendqisi2
	clz	r3, r4
	cmp	r2, #0
	mov	r3, r3, lsr #5
	moveq	r3, #0
	cmp	r3, #0
	moveq	r4, r3
	str	r7, [sp, #44]
	beq	.L2238
	mov	r0, r6
	bl	ftl_get_blk_mode
	cmp	r0, #1
	mov	r4, r0
	movweq	r3, #2390
	ldreqh	r9, [r5, r3]
.L2238:
	movw	r3, #2388
	ldr	r0, .L2251+20
	mov	r1, r4
	mov	r2, r9
	ldrh	r3, [r5, r3]
	mov	r6, #0
	bl	printk
	ldr	r8, .L2251+4
.L2239:
	ldr	r3, .L2251+24
	add	r0, sp, #62
	movw	lr, #65535
	mov	r10, #36
	ldrh	r3, [r3]
	mov	r7, r3
	ldr	r3, [r8, #-536]
	str	r3, [sp, #28]
	ldr	r3, [r8, #-2696]
	str	r3, [sp, #32]
	ldr	r3, .L2251+28
	ldrh	r2, [r3]
	ldrh	ip, [r3, #2]
	str	r2, [sp, #36]
	ldr	r2, [r8, #-2692]
	str	r2, [sp, #40]
	mov	r2, #0
	mov	r5, r2
.L2240:
	uxth	r3, r2
	cmp	r3, r7
	bcs	.L2249
	ldrh	r3, [r0, #2]!
	cmp	r3, lr
	beq	.L2241
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
.L2241:
	add	r2, r2, #1
	b	.L2240
.L2249:
	ldr	r0, [r8, #-536]
	mov	r1, r5
	mov	r2, r4
	mov	r10, #0
	bl	FlashReadPages
	mov	fp, #36
.L2243:
	uxth	r3, r10
	cmp	r3, r5
	bcs	.L2250
	ldr	r3, [sp, #44]
	mul	r2, fp, r10
	ldrh	r1, [sp, #48]
	ldr	lr, [r3, #-536]
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
	ldr	r0, .L2251+32
	str	r3, [sp, #20]
	ldr	r2, [lr, r2]
	ldr	r3, [ip, #4]
	bl	printk
	b	.L2243
.L2250:
	add	r6, r6, #1
	uxth	r6, r6
	cmp	r6, r9
	bne	.L2239
.L2245:
	mov	r0, #0
	add	sp, sp, #100
	@ sp needed
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L2252:
	.align	2
.L2251:
	.word	.LANCHOR0
	.word	.LANCHOR2
	.word	.LANCHOR3+148
	.word	.LC110
	.word	.LC111
	.word	.LC112
	.word	.LANCHOR0+2320
	.word	.LANCHOR0+2398
	.word	.LC113
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
	ldr	r0, .L2268
	mov	r7, #0
	ldr	r1, .L2268+4
	bl	printk
	ldr	r5, .L2268+8
	mov	r6, r5
.L2254:
	ldr	r3, .L2268+12
	uxth	r4, r7
	ldrh	r3, [r3]
	cmp	r3, r4
	bls	.L2264
	add	r8, sp, #88
	mov	r0, r4
	movw	r9, #65535
	mov	r10, #36
	strh	r4, [r8, #-48]!	@ movhi
	bl	ftl_get_blk_mode
	ldr	ip, [r5, #-3544]
	mov	r2, r4, asl #1
	mov	r1, r4
	ldrh	r2, [ip, r2]
	mov	r3, r0
	ldr	r0, .L2268+16
	bl	printk
	mov	r0, r8
	bl	make_superblock
	ldr	r3, .L2268+20
	ldr	lr, [r5, #-2692]
	ldrh	r2, [r3]
	ldrh	ip, [r3, #78]
	ldrh	r8, [r3, #80]
	str	r2, [sp, #28]
	add	r0, sp, #54
	ldr	r2, [r5, #-536]
	str	r2, [sp, #32]
	ldr	r2, [r5, #-2696]
	str	r2, [sp, #36]
	mov	r2, #0
	mov	r4, r2
.L2255:
	ldr	r1, [sp, #28]
	uxth	r3, r2
	cmp	r3, r1
	bcs	.L2265
	ldrh	r3, [r0, #2]!
	cmp	r3, r9
	beq	.L2256
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
.L2256:
	add	r2, r2, #1
	b	.L2255
.L2265:
	ldr	r0, [r6, #-536]
	mov	r1, r4
	mov	r2, #0
	mov	r8, #0
	bl	FlashReadPages
	mov	r9, #36
.L2258:
	uxth	r3, r8
	cmp	r3, r4
	bcs	.L2266
	mul	r2, r9, r8
	ldr	lr, [r6, #-536]
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
	ldr	r0, .L2268+24
	str	r3, [sp, #20]
	ldr	r2, [lr, r2]
	ldr	r3, [ip, #4]
	bl	printk
	b	.L2258
.L2266:
	ldr	r0, [r6, #-536]
	mov	r1, r4
	mov	r2, #1
	mov	r8, #0
	bl	FlashReadPages
	mov	r9, #36
.L2260:
	uxth	r3, r8
	cmp	r3, r4
	bcs	.L2267
	mul	r2, r9, r8
	ldr	lr, [r6, #-536]
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
	ldr	r0, .L2268+28
	str	r3, [sp, #20]
	ldr	r2, [lr, r2]
	ldr	r3, [ip, #4]
	bl	printk
	b	.L2260
.L2267:
	add	r7, r7, #1
	b	.L2254
.L2264:
	mov	r0, #0
	add	sp, sp, #92
	@ sp needed
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L2269:
	.align	2
.L2268:
	.word	.LC110
	.word	.LANCHOR3+168
	.word	.LANCHOR2
	.word	.LANCHOR0+2330
	.word	.LC114
	.word	.LANCHOR0+2320
	.word	.LC115
	.word	.LC116
	.fnend
	.size	FtlScanAllBlock, .-FtlScanAllBlock
	.align	2
	.global	ftl_scan_all_ppa
	.type	ftl_scan_all_ppa, %function
ftl_scan_all_ppa:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	stmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, lr}
	.save {r4, r5, r6, r7, r8, r9, r10, lr}
	movw	r3, #2386
	ldr	r6, .L2289
	.pad #24
	sub	sp, sp, #24
	ldr	r5, .L2289+4
	ldrh	r4, [r6, r3]
	sub	r4, r4, #16
.L2271:
	ldr	r7, .L2289+8
	ldrh	r3, [r7]
	cmp	r4, r3
	bge	.L2287
	uxth	r8, r4
	mov	r0, r8
	bl	ftl_get_blk_mode
	ldrb	r3, [r6, #148]	@ zero_extendqisi2
	cmp	r3, #0
	beq	.L2272
	ldrh	r3, [r7, #-58]
	cmp	r4, r3
	bge	.L2273
	ldrh	r3, [r7, #16]
	cmp	r4, r3
	blt	.L2273
.L2272:
	cmp	r0, #1
	bne	.L2274
.L2273:
	ldr	r3, .L2289+12
	mov	r9, #-2147483648
	ldrh	r7, [r3]
	b	.L2275
.L2274:
	ldr	r3, .L2289+16
	mov	r9, #0
	ldrh	r7, [r3]
.L2275:
	mov	r1, r4
	mov	r2, r7
	mov	r3, r9
	ldr	r0, .L2289+20
	bl	printk
	mov	r0, r8
	bl	FtlBbmIsBadBlock
	cmp	r0, #0
	beq	.L2276
	ldr	r0, .L2289+24
	mov	r1, r4
	mov	r2, r7
	mov	r3, r9
	bl	printk
.L2276:
	mov	r10, r4, asl #10
	mov	r8, #0
.L2277:
	cmp	r8, r7
	beq	.L2288
	add	r3, r9, r10
	mov	r2, #0
	add	r3, r3, r8
	str	r3, [r5, #1764]
	ldr	r3, [r5, #-524]
	mov	r1, #1
	ldr	r0, .L2289+28
	add	r8, r8, #1
	str	r2, [r5, #1760]
	str	r3, [r5, #1768]
	ldr	r3, [r5, #-500]
	str	r3, [r5, #1772]
	bl	FlashReadPages
	ldr	r3, [r5, #1772]
	ldr	r2, [r5, #1768]
	ldr	r0, .L2289+32
	ldr	r1, [r3, #4]
	str	r1, [sp]
	ldr	r1, [r3, #8]
	str	r1, [sp, #4]
	ldr	r1, [r3, #12]
	str	r1, [sp, #8]
	ldr	r1, [r2]
	str	r1, [sp, #12]
	ldr	r2, [r2, #4]
	ldr	r1, [r5, #1764]
	str	r2, [sp, #16]
	ldr	r2, [r5, #1760]
	ldr	r3, [r3]
	bl	printk
	b	.L2277
.L2288:
	add	r4, r4, #1
	b	.L2271
.L2287:
	ldr	r0, .L2289+36
	ldr	r1, .L2289+40
	add	sp, sp, #24
	@ sp needed
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, lr}
	b	printk
.L2290:
	.align	2
.L2289:
	.word	.LANCHOR0
	.word	.LANCHOR2
	.word	.LANCHOR0+2386
	.word	.LANCHOR0+2390
	.word	.LANCHOR0+2388
	.word	.LC117
	.word	.LC118
	.word	.LANCHOR2+1760
	.word	.LC119
	.word	.LC120
	.word	.LANCHOR3+184
	.fnend
	.size	ftl_scan_all_ppa, .-ftl_scan_all_ppa
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
	ldr	r6, .L2347
	mov	r4, r0
	str	r1, [sp, #8]
	mov	r9, r2
	str	r3, [sp, #20]
	ldr	ip, [r6, #44]
	ldrb	r7, [r6, #32]	@ zero_extendqisi2
	ldrb	ip, [ip, #19]	@ zero_extendqisi2
	cmp	r7, #0
	str	ip, [sp, #16]
	ldr	ip, .L2347+4
	ldrb	ip, [ip, #481]	@ zero_extendqisi2
	str	ip, [sp, #12]
	beq	.L2292
	bl	FlashProgSlc2KPages
	b	.L2293
.L2305:
	mov	r3, #36
	mov	r1, r9
	mul	r8, r3, r7
	ldr	r3, [sp, #8]
	add	r2, sp, #28
	rsb	r3, r7, r3
	add	fp, r4, r8
	uxtb	r3, r3
	str	r3, [sp]
	mov	r0, fp
	add	r3, sp, #32
	bl	LogAddr2PhyAddr
	ldrb	r3, [r6, #2230]	@ zero_extendqisi2
	mov	r10, r0
	ldr	r0, [sp, #32]
	cmp	r0, r3
	mvncs	r3, #0
	strcs	r3, [r4, r8]
	bcc	.L2344
.L2295:
	add	r7, r7, #1
.L2292:
	ldr	r3, [sp, #8]
	ldr	r5, .L2347
	cmp	r7, r3
	bcc	.L2305
	b	.L2345
.L2344:
	ldrb	r3, [r6, #2240]	@ zero_extendqisi2
	cmp	r3, #0
	add	r3, r6, r0, asl #4
	ldr	r3, [r3, #2108]
	moveq	r10, #0
	cmp	r3, #0
	beq	.L2297
	uxtb	r0, r0
	bl	FlashWaitCmdDone
.L2297:
	ldr	r3, [sp, #32]
	mov	r1, #0
	cmp	r10, #0
	add	r2, r6, r3, asl #4
	str	r1, [r2, #2112]
	ldr	r1, [sp, #28]
	str	fp, [r2, #2108]
	str	r1, [r2, #2104]
	addne	r1, r8, #36
	addne	r1, r4, r1
	strne	r1, [r2, #2112]
	add	r2, r6, r3
	add	r3, r6, r3, asl #4
	ldrb	r5, [r2, #2232]	@ zero_extendqisi2
	strb	r5, [r3, #2100]
	mov	r0, r5
	ldrb	r3, [r6, #2230]	@ zero_extendqisi2
	cmp	r3, #1
	bne	.L2299
	bl	NandcWaitFlashReady
	b	.L2300
.L2299:
	bl	NandcFlashCs
	mov	r0, r5
	ldr	r3, [sp, #32]
	ldr	r1, [sp, #28]
	add	r3, r6, r3, asl #2
	ldr	r2, [r3, #1176]
	adds	r2, r2, #0
	movne	r2, #1
	bl	FlashWaitReadyEN
	mov	r0, r5
	bl	NandcFlashDeCs
.L2300:
	ldr	r3, [sp, #16]
	sub	r3, r3, #1
	cmp	r3, #7
	bhi	.L2301
	add	r3, r6, r5
	ldrb	r3, [r3, #2064]	@ zero_extendqisi2
	cmp	r3, #0
	beq	.L2301
	mov	r0, r5
	ldrb	r1, [r6, #1213]	@ zero_extendqisi2
	ldr	r2, .L2347+8
	mov	r3, #0
	bl	HynixSetRRPara
.L2301:
	mov	r0, r5
	bl	NandcFlashCs
	cmp	r9, #1
	mov	r0, r5
	bne	.L2302
	ldrb	r3, [r6, #148]	@ zero_extendqisi2
	cmp	r3, #0
	beq	.L2302
	bl	flash_enter_slc_mode
	b	.L2303
.L2302:
	bl	flash_exit_slc_mode
.L2303:
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
	beq	.L2304
	mov	r0, r5
	ldr	r1, [sp, #28]
	bl	FlashProgDpFirstCmd
	mov	r0, r5
	add	r8, r8, #36
	add	r8, r4, r8
	ldr	r3, [sp, #32]
	ldr	r1, [sp, #28]
	add	r3, r6, r3, asl #2
	ldr	r2, [r3, #1176]
	adds	r2, r2, #0
	movne	r2, #1
	bl	FlashWaitReadyEN
	ldr	r1, [r6, #36]
	mov	r0, r5
	ldr	r3, [sp, #28]
	add	r1, r1, r3
	bl	FlashProgDpSecondCmd
	ldr	r3, [r8, #12]
	mov	r0, r5
	mov	r1, #1
	str	r3, [sp]
	ldr	r2, [sp, #12]
	ldr	r3, [r8, #8]
	bl	NandcXferData
.L2304:
	mov	r0, r5
	ldr	r1, [sp, #28]
	bl	FlashProgSecondCmd
	mov	r0, r5
	bl	NandcFlashDeCs
	add	r7, r7, r10
	b	.L2295
.L2345:
	ldr	r8, .L2347+12
	mov	r6, #0
	mov	r7, r5
.L2306:
	ldrb	r3, [r5, #2230]	@ zero_extendqisi2
	cmp	r6, r3
	bcs	.L2346
	uxtb	r0, r6
	bl	FlashWaitCmdDone
	cmp	r9, #1
	bne	.L2307
	ldrb	r3, [r7, #148]	@ zero_extendqisi2
	cmp	r3, #0
	beq	.L2307
	ldrb	r0, [r8, r6, asl #4]	@ zero_extendqisi2
	bl	flash_exit_slc_mode
.L2307:
	add	r6, r6, #1
	b	.L2306
.L2346:
	ldr	r3, [sp, #20]
	cmp	r3, #0
	bne	.L2309
.L2317:
	mov	r0, #0
	b	.L2293
.L2309:
	ldr	r5, .L2347+16
	mov	r6, #0
	mov	r7, r5
.L2310:
	ldr	r3, [sp, #8]
	cmp	r6, r3
	beq	.L2317
	ldr	r3, [r4]
	cmn	r3, #1
	bne	.L2311
	ldr	r1, [r4, #4]
	ldr	r0, .L2347+20
	bl	printk
	b	.L2312
.L2311:
	ldr	r3, [sp, #8]
	mov	r1, r9
	add	r2, sp, #28
	mov	r0, r4
	rsb	r3, r6, r3
	uxtb	r3, r3
	str	r3, [sp]
	add	r3, sp, #32
	bl	LogAddr2PhyAddr
	ldr	r2, [r5, #1732]
	mov	r3, #0
	mov	lr, r4
	str	r3, [r2]
	ldr	r2, [r5, #1736]
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
	ldr	r3, [r5, #1732]
	str	r3, [sp, #44]
	ldr	r3, [r5, #1736]
	str	r3, [sp, #48]
	bl	FlashReadPages
	ldr	r8, [sp, #36]
	cmn	r8, #1
	bne	.L2313
	ldr	r0, .L2347+24
	ldr	r1, [r4, #4]
	bl	printk
	str	r8, [r4]
.L2313:
	ldr	r3, [r4, #12]
	cmp	r3, #0
	beq	.L2314
	ldr	r2, [r3]
	ldr	r3, [r7, #1736]
	ldr	r3, [r3]
	cmp	r2, r3
	beq	.L2314
	ldr	r0, .L2347+28
	ldr	r1, [r4, #4]
	bl	printk
	mvn	r3, #0
	str	r3, [r4]
.L2314:
	ldr	r3, [r4, #8]
	cmp	r3, #0
	beq	.L2312
	ldr	r2, [r3]
	ldr	r3, [r7, #1732]
	ldr	r3, [r3]
	cmp	r2, r3
	beq	.L2312
	ldr	r0, .L2347+32
	ldr	r1, [r4, #4]
	bl	printk
	mvn	r3, #0
	str	r3, [r4]
.L2312:
	add	r6, r6, #1
	add	r4, r4, #36
	b	.L2310
.L2293:
	add	sp, sp, #76
	@ sp needed
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L2348:
	.align	2
.L2347:
	.word	.LANCHOR0
	.word	.LANCHOR1
	.word	.LANCHOR0+1216
	.word	.LANCHOR0+2100
	.word	.LANCHOR2
	.word	.LC104
	.word	.LC105
	.word	.LC107
	.word	.LC108
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
	ldr	r5, .L2353
	ldr	r3, [r5, #1716]
	cmp	r0, r3
	movcc	r4, #0
	bcc	.L2350
	ldr	r3, [r5, #1724]
	mov	r4, r0
	mov	r1, #165
	add	r0, sp, #40
	mov	r2, #32
	str	r0, [sp, #16]
	str	r3, [sp, #12]
	bl	ftl_memset
	mov	r1, #90
	mov	r2, #8
	ldr	r0, [r5, #1724]
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
.L2350:
	mov	r0, r4
	add	sp, sp, #108
	@ sp needed
	ldmfd	sp!, {r4, r5, pc}
.L2354:
	.align	2
.L2353:
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
	ldr	r5, .L2411
	mov	r1, #1
	ldr	r4, .L2411+4
	ldr	r0, .L2411+8
	ldr	r3, [r5, #1728]
	ldrh	r2, [r4, #132]
	str	r3, [sp, #20]
	ldrh	r3, [r4, #134]
	smulbb	r3, r2, r3
	uxth	r3, r3
	str	r3, [sp, #4]
	ldr	r3, [r4, #44]
	ldrb	r3, [r3, #24]	@ zero_extendqisi2
	str	r3, [sp, #24]
	ldrh	r3, [r4, #36]
	str	r3, [sp, #16]
	ldrb	r3, [r4, #32]	@ zero_extendqisi2
	ldr	r4, .L2411+4
	cmp	r3, #1
	ldreq	r3, [sp, #16]
	moveq	r3, r3, asl #1
	uxtheq	r3, r3
	streq	r3, [sp, #16]
	bl	printk
	ldr	r0, [r5, #1728]
	mov	r1, #0
	mov	r2, #4096
	bl	ftl_memset
	ldr	r5, .L2411
	ldr	r3, [sp, #4]
	mov	r3, r3, lsr #4
	str	r3, [sp, #28]
	mov	r3, #0
	str	r3, [sp, #8]
.L2357:
	ldrb	r7, [sp, #8]	@ zero_extendqisi2
	ldrb	r3, [r4, #2230]	@ zero_extendqisi2
	cmp	r3, r7
	bls	.L2407
	ldr	r2, .L2411+12
	mov	r3, r7, asl #1
	ldrh	r6, [r2, r3]
	cmp	r6, #0
	bne	.L2387
	ldrh	r2, [r4, #140]
	mov	r1, r6
	ldr	r0, [r5, #1696]
	mov	r9, r6
	mov	r8, r6
	mov	r2, r2, asl #9
	bl	ftl_memset
	add	r3, r4, r7
	ldrb	r10, [r3, #2232]	@ zero_extendqisi2
	mov	r3, r7, asl #2
	add	fp, r4, r3
	str	r3, [sp, #32]
.L2359:
	ldr	r2, [sp, #4]
	uxth	r3, r9
	str	r3, [sp, #12]
	cmp	r3, r2
	bcs	.L2369
	mvn	r3, #0
	strb	r3, [sp, #42]
	strb	r3, [sp, #43]
	ldr	r3, [sp, #24]
	tst	r3, #1
	beq	.L2361
	ldr	r3, [fp, #1176]
	add	r2, sp, #42
	mov	r0, r10
	add	r3, r6, r3
	str	r3, [sp, #36]
	mov	r1, r3
	bl	FlashReadSpare
	ldrb	r2, [r4, #32]	@ zero_extendqisi2
	cmp	r2, #1
	ldr	r3, [sp, #36]
	bne	.L2361
	ldr	r1, [r4, #36]
	add	r2, sp, #43
	mov	r0, r10
	add	r1, r3, r1
	bl	FlashReadSpare
	ldrb	r3, [sp, #42]	@ zero_extendqisi2
	ldrb	r2, [sp, #43]	@ zero_extendqisi2
	and	r3, r3, r2
	strb	r3, [sp, #42]
.L2361:
	ldr	r3, [sp, #24]
	tst	r3, #2
	beq	.L2363
	ldr	r3, [r4, #44]
	mov	r0, r10
	add	r2, sp, #43
	ldrh	r1, [r3, #10]
	ldr	r3, [fp, #1176]
	sub	r1, r1, #1
	add	r1, r1, r3
	add	r1, r1, r6
	bl	FlashReadSpare
.L2363:
	ldr	r2, [r4, #44]
	ldrb	r3, [r2, #7]	@ zero_extendqisi2
	cmp	r3, #1
	cmpne	r3, #8
	ldrb	r3, [sp, #42]	@ zero_extendqisi2
	beq	.L2364
	ldrb	r2, [r2, #18]	@ zero_extendqisi2
	cmp	r2, #12
	bne	.L2365
.L2364:
	cmp	r3, #0
	ldrneb	r0, [sp, #43]	@ zero_extendqisi2
	clzne	r0, r0
	movne	r0, r0, lsr #5
	bne	.L2366
	b	.L2386
.L2365:
	cmp	r3, #255
	bne	.L2386
	ldrb	r0, [sp, #43]	@ zero_extendqisi2
	subs	r0, r0, #255
	movne	r0, #1
	b	.L2366
.L2386:
	mov	r0, #1
.L2366:
	ldr	r3, [sp, #24]
	tst	r3, #4
	beq	.L2367
	ldr	r3, .L2411+4
	mov	r0, r10
	ldr	r2, [sp, #32]
	add	r3, r3, r2
	ldr	r1, [r3, #1176]
	add	r1, r6, r1
	bl	SandiskProgTestBadBlock
.L2367:
	cmp	r0, #0
	beq	.L2368
	mov	r1, r7
	mov	r2, r9
	ldr	r0, .L2411+16
	add	r8, r8, #1
	bl	printk
	ldr	r1, [r5, #1696]
	mov	ip, #1
	uxth	r8, r8
	ldr	r3, [sp, #12]
	mov	r0, r3, lsr #5
	and	r3, r3, #31
	ldr	r2, [r1, r0, asl #2]
	orr	r3, r2, ip, asl r3
	ldr	r2, [sp, #28]
	str	r3, [r1, r0, asl #2]
	ldrb	r3, [r4, #2230]	@ zero_extendqisi2
	mul	r3, r3, r2
	cmp	r8, r3
	bgt	.L2369
.L2368:
	ldr	r3, [sp, #16]
	add	r9, r9, #1
	add	r6, r6, r3
	b	.L2359
.L2369:
	mov	r2, r8
	ldr	r0, .L2411+20
	mov	r1, r7
	bl	printk
	ldrb	r3, [r4, #2230]	@ zero_extendqisi2
	ldr	r2, [sp, #28]
	mul	r3, r3, r2
	cmp	r8, r3
	ldr	r8, .L2411+4
	blt	.L2371
	ldrh	r2, [r4, #140]
	mov	r1, #0
	ldr	r0, [r5, #1696]
	mov	r2, r2, asl #9
	bl	ftl_memset
.L2371:
	cmp	r7, #0
	bne	.L2373
	ldr	r3, [r5, #1716]
	mov	r6, r7
	mov	r9, #1
	uxth	r10, r3
.L2374:
	ldrb	r3, [r8, #33]	@ zero_extendqisi2
	cmp	r3, r10
	bls	.L2408
	mov	r0, r10
	bl	FlashTestBlk
	cmp	r0, #0
	beq	.L2375
	mov	r1, r10
	ldr	r0, .L2411+24
	bl	printk
	ldr	r1, [r5, #1696]
	mov	r0, r10, lsr #5
	add	r6, r6, #1
	and	r3, r10, #31
	ldr	r2, [r1, r0, asl #2]
	uxth	r6, r6
	orr	r3, r2, r9, asl r3
	str	r3, [r1, r0, asl #2]
.L2375:
	add	r10, r10, #1
	uxth	r10, r10
	b	.L2374
.L2408:
	ldr	r3, [sp, #4]
	mov	r10, #1
	sub	fp, r3, #1
	sub	r9, r3, #50
	uxth	fp, fp
.L2377:
	cmp	fp, r9
	ble	.L2409
	mov	r0, fp
	bl	FlashTestBlk
	cmp	r0, #0
	beq	.L2378
	mov	r1, fp
	ldr	r0, .L2411+24
	bl	printk
	ldr	r1, [r5, #1696]
	mov	r0, fp, lsr #5
	and	r3, fp, #31
	ldr	r2, [r1, r0, asl #2]
	orr	r3, r2, r10, asl r3
	str	r3, [r1, r0, asl #2]
.L2378:
	sub	fp, fp, #1
	uxth	fp, fp
	b	.L2377
.L2409:
	ldrb	r3, [r8, #33]	@ zero_extendqisi2
	ldr	r2, [r5, #1716]
	rsb	r3, r2, r3
	cmp	r6, r3
	bcc	.L2373
	ldrh	r2, [r4, #140]
	mov	r1, #0
	ldr	r0, [r5, #1696]
	mov	r2, r2, asl #9
	bl	ftl_memset
.L2373:
	ldr	r3, [sp, #4]
	ldrb	r6, [sp, #8]	@ zero_extendqisi2
	sub	r9, r3, #1
	ldr	r8, .L2411+12
	mul	r6, r3, r6
	uxth	r9, r9
	add	r8, r8, r7, asl #1
.L2381:
	mov	r1, r7
	ldr	r0, .L2411+28
	mov	r2, r9
	bl	printk
	ldr	r1, [r5, #1696]
.L2382:
	mov	r2, r9, lsr #5
	and	r3, r9, #31
	ldr	r2, [r1, r2, asl #2]
	mov	r3, r2, lsr r3
	ands	r3, r3, #1
	subne	r9, r9, #1
	uxthne	r9, r9
	bne	.L2382
.L2410:
	ldr	r1, [sp, #20]
	add	r0, sp, #44
	ldr	r2, .L2411+32
	strh	r9, [r8]	@ movhi
	strh	r9, [r1, #2]	@ movhi
	strh	r2, [r1]	@ movhi
	strh	r3, [r1, #8]	@ movhi
	mov	r1, #1
	ldr	r3, [r5, #1696]
	mov	r2, r1
	str	r3, [sp, #52]
	ldr	r3, [r5, #1728]
	str	r3, [sp, #56]
	add	r3, r9, r6
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
	subne	r9, r9, #1
	uxthne	r9, r9
	bne	.L2381
.L2387:
	ldr	r3, [sp, #8]
	add	r3, r3, #1
	str	r3, [sp, #8]
	b	.L2357
.L2407:
	add	sp, sp, #84
	@ sp needed
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L2412:
	.align	2
.L2411:
	.word	.LANCHOR2
	.word	.LANCHOR0
	.word	.LC121
	.word	.LANCHOR2+1744
	.word	.LC122
	.word	.LC123
	.word	.LC124
	.word	.LC125
	.word	-3872
	.fnend
	.size	FlashMakeFactorBbt, .-FlashMakeFactorBbt
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
	ldr	r9, .L2463
	ldr	fp, .L2463+4
	ldr	r5, [r9, #-3616]
	ldrb	r3, [fp, #2240]	@ zero_extendqisi2
	cmp	r5, #0
	str	r3, [sp, #16]
	movne	r0, #0
	bne	.L2414
	ldrb	r3, [fp, #148]	@ zero_extendqisi2
	mov	r6, fp
	add	fp, fp, #2400
	mov	r10, r9
	mov	r4, r5
	mov	r9, r5
	mov	r8, #36
	str	r1, [sp]
	str	r0, [sp, #4]
	str	r3, [sp, #8]
	str	r0, [r10, #-548]
.L2415:
	ldr	ip, .L2463+4
	add	r1, ip, #2320
	ldrh	r0, [r1]
	uxth	r1, r9
	cmp	r0, r1
	bls	.L2458
	mul	r0, r8, r1
	ldr	ip, [r10, #-3612]
	mov	r3, #0
	str	r3, [ip, r0]
	ldr	r3, .L2463+4
	add	r1, r3, r1
	ldrb	r0, [r1, #2348]	@ zero_extendqisi2
	ldr	r1, [sp, #4]
	bl	V2P_block
	ldr	r3, [sp]
	mov	r7, r0
	cmp	r3, #0
	beq	.L2416
	bl	IsBlkInVendorPart
	cmp	r0, #0
	bne	.L2417
.L2416:
	mov	r0, r7
	bl	FtlBbmIsBadBlock
	cmp	r0, #0
	addne	r4, r4, #1
	uxthne	r4, r4
	bne	.L2417
	ldr	ip, [r10, #-3612]
	mov	r7, r7, asl #10
	ldrh	r1, [fp]
	mla	ip, r8, r5, ip
	mul	r1, r1, r5
	add	r5, r5, #1
	uxth	r5, r5
	cmp	r1, #0
	str	r0, [ip, #8]
	add	r0, r1, #3
	str	r7, [ip, #4]
	movlt	r1, r0
	ldr	r0, [r10, #-496]
	bic	r1, r1, #3
	add	r1, r0, r1
	str	r1, [ip, #12]
.L2417:
	add	r9, r9, #1
	b	.L2415
.L2458:
	cmp	r5, #0
	beq	.L2438
	ldr	r3, [sp, #8]
	mov	r2, r5
	ldr	r0, [r10, #-3612]
	mov	r8, #0
	adds	r7, r3, #0
	strb	r8, [ip, #2240]
	str	ip, [sp, #8]
	mov	r9, #36
	movne	r7, #1
	mov	r1, r7
	bl	FlashEraseBlocks
	ldrb	r3, [sp, #16]	@ zero_extendqisi2
	ldr	ip, [sp, #8]
	strb	r3, [ip, #2240]
.L2421:
	uxth	r2, r8
	cmp	r2, r5
	bcs	.L2459
	mul	r2, r9, r8
	ldr	r1, [r10, #-3612]
	add	r0, r1, r2
	ldr	r2, [r1, r2]
	cmn	r2, #1
	bne	.L2422
	ldr	r0, [r0, #4]
	add	r4, r4, #1
	ubfx	r0, r0, #10, #16
	uxth	r4, r4
	bl	FtlBbmMapBadBlock
.L2422:
	add	r8, r8, #1
	b	.L2421
.L2459:
	ldr	r3, [sp]
	cmp	r3, #0
	bne	.L2424
	mov	r3, #6
	uxth	r7, r7
	str	r3, [sp, #12]
	mov	r3, #1
	str	r3, [sp, #8]
	b	.L2425
.L2424:
	movw	r3, #2390
	ldrh	r3, [r6, r3]
	str	r3, [sp, #8]
	ldrb	r3, [r6, #148]	@ zero_extendqisi2
	cmp	r3, #0
	ldreq	r3, [sp, #8]
	moveq	r7, #1
	movne	r7, #1
	strne	r7, [sp, #12]
	moveq	r3, r3, lsr #2
	streq	r3, [sp, #12]
.L2425:
	ldr	r10, .L2463
	mov	r5, #0
	mov	r8, r10
.L2434:
	mov	fp, #0
	mov	r6, fp
.L2426:
	ldr	r9, .L2463+4
	add	r3, r9, #2320
	ldrh	r1, [r3]
	uxth	r3, fp
	cmp	r1, r3
	bls	.L2460
	mov	r2, #36
	ldr	r0, [r10, #-3612]
	mul	r1, r2, r3
	mov	r2, #0
	str	r2, [r0, r1]
	ldr	r2, .L2463+4
	ldr	r1, [sp, #4]
	add	r3, r2, r3
	ldrb	r0, [r3, #2348]	@ zero_extendqisi2
	bl	V2P_block
	ldr	r3, [sp]
	mov	r9, r0
	cmp	r3, #0
	beq	.L2427
	bl	IsBlkInVendorPart
	cmp	r0, #0
	bne	.L2428
.L2427:
	mov	r0, r9
	bl	FtlBbmIsBadBlock
	cmp	r0, #0
	bne	.L2428
	ldr	r1, [r8, #-3612]
	mov	r3, #36
	add	r9, r5, r9, asl #10
	mla	r1, r3, r6, r1
	ldr	r3, [r8, #-508]
	str	r3, [r1, #8]
	ldr	r3, .L2463+8
	str	r9, [r1, #4]
	ldrh	r3, [r3]
	mul	r3, r3, r6
	add	r6, r6, #1
	uxth	r6, r6
	add	r0, r3, #3
	cmp	r3, #0
	movlt	r3, r0
	ldr	r0, [r8, #-504]
	bic	r3, r3, #3
	add	r3, r0, r3
	str	r3, [r1, #12]
.L2428:
	add	fp, fp, #1
	b	.L2426
.L2460:
	cmp	r6, #0
	beq	.L2438
	mov	r2, r7
	mov	r3, #1
	ldr	r0, [r10, #-3612]
	mov	r1, r6
	mov	fp, #0
	strb	fp, [r9, #2240]
	bl	FlashProgPages
	mov	r2, #36
	ldrb	r3, [sp, #16]	@ zero_extendqisi2
	strb	r3, [r9, #2240]
	ldr	r9, .L2463
.L2431:
	uxth	r3, fp
	cmp	r3, r6
	bcs	.L2461
	mul	r3, r2, fp
	ldr	r1, [r8, #-3612]
	add	r0, r1, r3
	ldr	r3, [r1, r3]
	cmp	r3, #0
	beq	.L2432
	ldr	r0, [r0, #4]
	add	r4, r4, #1
	str	r2, [sp, #20]
	ubfx	r0, r0, #10, #16
	uxth	r4, r4
	bl	FtlBbmMapBadBlock
	ldr	r2, [sp, #20]
.L2432:
	add	fp, fp, #1
	b	.L2431
.L2461:
	ldr	r3, [sp, #12]
	add	r5, r5, r3
	ldr	r3, [sp, #8]
	uxth	r5, r5
	cmp	r5, r3
	bcc	.L2434
	mov	r5, #0
	mov	r8, #36
.L2435:
	uxth	r3, r5
	cmp	r3, r6
	ldr	r3, [sp]
	bcs	.L2462
	cmp	r3, #0
	beq	.L2436
	mul	r3, r8, r5
	ldr	r2, [r9, #-3612]
	add	r1, r2, r3
	ldr	r3, [r2, r3]
	cmp	r3, #0
	bne	.L2436
	ldr	r0, [r1, #4]
	mov	r1, #1
	ubfx	r0, r0, #10, #16
	bl	FtlFreeSysBlkQueueIn
.L2436:
	add	r5, r5, #1
	b	.L2435
.L2462:
	adds	r1, r3, #0
	ldr	r3, [sp, #4]
	movne	r1, #1
	cmp	r3, #63
	orrls	r1, r1, #1
	cmp	r1, #0
	beq	.L2438
	ldr	r0, [r9, #-3612]
	mov	r1, r7
	mov	r2, r6
	bl	FlashEraseBlocks
.L2438:
	mov	r0, r4
.L2414:
	add	sp, sp, #28
	@ sp needed
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L2464:
	.align	2
.L2463:
	.word	.LANCHOR2
	.word	.LANCHOR0
	.word	.LANCHOR0+2400
	.fnend
	.size	FtlLowFormatEraseBlock, .-FtlLowFormatEraseBlock
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
	ldr	r4, .L2483
	ldr	r5, [r4, #-3616]
	cmp	r5, #0
	bne	.L2467
	ldr	r3, [r4, #-500]
	mov	r1, r5
	ldr	r2, .L2483+4
	sub	r9, r4, #436
	ldr	r0, [r4, #-524]
	str	r3, [r4, #1772]
	movw	r3, #2398
	ldrh	r2, [r2, r3]
	str	r0, [r4, #1768]
	bl	ftl_memset
	ldr	r6, .L2483+8
	sub	r7, r6, #134
.L2468:
	ldrh	r3, [r7]
	ldr	r8, .L2483+4
	cmp	r5, r3
	bge	.L2482
	ldrh	r2, [r9]
	ldr	r3, [r4, #1768]
	ldr	r1, [r6, #4]!
	mul	r0, r2, r5
	mov	r2, r2, asl #2
	add	r5, r5, #1
	add	r0, r3, r0, asl #2
	bl	ftl_memcpy
	b	.L2468
.L2482:
	ldr	r5, [r4, #1772]
	mov	r1, #255
	mov	r2, #16
	ldr	r6, .L2483+12
	ldr	fp, .L2483
	mov	r7, #0
	mov	r0, r5
	mov	r10, r7
	bl	ftl_memset
	ldr	r3, .L2483+16
	strh	r3, [r5]	@ movhi
	ldr	r3, [r8, #2460]
	str	r3, [r5, #4]
	movw	r3, #2452
	ldrh	r3, [r8, r3]
	strh	r3, [r5, #2]	@ movhi
	ldrh	r3, [r6, #4]
	strh	r3, [r5, #8]	@ movhi
	ldrh	r3, [r6, #6]
	strh	r3, [r5, #10]	@ movhi
	ldr	r3, [r8, #2316]
	strh	r3, [r5, #12]	@ movhi
.L2470:
	ldr	r3, [r4, #-524]
	mov	ip, #0
	ldrh	r1, [r6]
	ldrh	r2, [r6, #2]
	str	r3, [r4, #1768]
	ldr	r3, [r4, #-500]
	str	ip, [r4, #1760]
	str	ip, [sp, #12]
	str	r3, [r4, #1772]
	orr	r3, r2, r1, asl #10
	ldrh	r0, [r5, #10]
	str	r3, [r4, #1764]
	ldrh	r3, [r6, #4]
	str	r0, [sp]
	ldr	r0, .L2483+20
	bl	printk
	ldr	r9, .L2483+12
	ldrh	r2, [r6, #2]
	ldrh	r3, [r9, #-62]
	sub	r3, r3, #1
	cmp	r2, r3
	ldr	ip, [sp, #12]
	blt	.L2471
	ldr	r3, [r8, #2460]
	mov	r1, #1
	ldrh	r2, [r9]
	add	r3, r3, #1
	ldr	r0, [fp, #-3612]
	str	r3, [r8, #2460]
	str	r3, [r5, #4]
	ldrh	r3, [r9, #4]
	strh	r2, [r5, #8]	@ movhi
	strh	r2, [r9, #4]	@ movhi
	mov	r2, r1
	strh	r3, [r9]	@ movhi
	mov	r3, r3, asl #10
	str	r3, [fp, #1764]
	str	r3, [r0, #4]
	strh	ip, [r9, #2]	@ movhi
	bl	FlashEraseBlocks
.L2471:
	ldr	r9, .L2483
	mov	r1, #1
	mov	r3, r1
	mov	r2, r1
	add	r0, r9, #1760
	bl	FlashProgPages
	ldrh	r3, [r6, #2]
	add	r3, r3, #1
	strh	r3, [r6, #2]	@ movhi
	ldr	r3, [r4, #1760]
	cmn	r3, #1
	bne	.L2472
	add	r7, r7, #1
	ldr	r0, .L2483+24
	ldr	r1, [fp, #1764]
	uxth	r7, r7
	bl	printk
	cmp	r7, #3
	bls	.L2470
	ldr	r0, .L2483+28
	mov	r2, r7
	ldr	r1, [r9, #1764]
	bl	printk
	mov	r3, #1
	str	r3, [r9, #-3616]
	b	.L2467
.L2472:
	add	r10, r10, #1
	cmp	r10, #1
	beq	.L2470
	cmp	r3, #256
	beq	.L2470
.L2467:
	mov	r0, #0
	add	sp, sp, #20
	@ sp needed
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L2484:
	.align	2
.L2483:
	.word	.LANCHOR2
	.word	.LANCHOR0
	.word	.LANCHOR0+2476
	.word	.LANCHOR0+2452
	.word	-3887
	.word	.LC126
	.word	.LC127
	.word	.LC128
	.fnend
	.size	FtlBbmTblFlush, .-FtlBbmTblFlush
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
	ldr	r4, .L2540
	ldr	r3, [r4, #-3616]
	cmp	r3, #0
	bne	.L2486
	mov	r5, r0
	mov	r8, r4
.L2487:
	ldr	r3, .L2540+4
	ldr	r10, .L2540+8
	cmp	r5, r3
	ldrb	r2, [r5, #8]	@ zero_extendqisi2
	bne	.L2488
	ldrh	r3, [r5, #-100]
	ldr	ip, [r8, #-2708]
	mov	r0, r3, lsr #1
	mul	lr, ip, r3
	add	r1, r0, #1
	add	r1, r1, lr, lsr #2
	ldr	lr, [r10, #2244]
	cmp	lr, #0
	uxth	r1, r1
	beq	.L2489
	ldr	lr, [r8, #-3308]
	cmp	lr, #39
	bhi	.L2489
	cmp	lr, #2
	bls	.L2514
	cmp	ip, #0
	movne	r3, #0
	andeq	r3, r3, #1
	cmp	r3, #0
	moveq	r1, r0
	beq	.L2489
	b	.L2514
.L2488:
	cmp	r2, #1
	bne	.L2514
	ldr	r1, .L2540+12
	ldrh	r1, [r1]
	cmp	r1, #1
	beq	.L2514
	ldrb	r1, [r10, #148]	@ zero_extendqisi2
	cmp	r1, #0
	bne	.L2514
	ldr	r0, [r10, #2244]
	ldrh	r3, [r3, #-100]
	cmp	r0, #0
	mov	r1, r3, lsr #3
	beq	.L2489
	ldr	r0, [r8, #-3308]
	cmp	r0, #1
	rsbls	r3, r3, r3, asl #3
	ubfxls	r1, r3, #3, #16
.L2489:
	cmp	r1, #0
	subne	r1, r1, #1
	uxthne	r1, r1
	b	.L2490
.L2514:
	mov	r1, #0
.L2490:
	ldr	r0, .L2540+16
	bl	List_pop_index_node
	ldr	r2, .L2540+20
	ldrh	r3, [r2]
	sub	r3, r3, #1
	strh	r3, [r2]	@ movhi
	ldr	r3, .L2540+24
	ldrh	r3, [r3]
	uxth	r9, r0
	cmp	r3, r9
	bls	.L2487
	ldr	r3, [r8, #-3544]
	mov	r7, r9, asl #1
	ldrh	r6, [r3, r7]
	cmp	r6, #0
	bne	.L2487
	strh	r9, [r5]	@ movhi
	mov	r0, r5
	bl	make_superblock
	ldrb	r3, [r5, #7]	@ zero_extendqisi2
	cmp	r3, #0
	beq	.L2492
	ldr	r2, .L2540+28
	add	ip, r5, #14
	ldr	r0, [r8, #-3612]
	ldrh	lr, [r2]
	mov	r2, #36
	mov	r3, r0
	mla	r1, r2, lr, r0
	mov	lr, r6
	str	r1, [sp]
	b	.L2493
.L2492:
	ldr	r3, [r8, #-3544]
	b	.L2535
.L2493:
	ldr	r1, [sp]
	cmp	r3, r1
	beq	.L2537
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
	b	.L2493
.L2537:
	ldr	r3, .L2540+32
	ldr	r2, [r10, #2244]
	rsb	r3, r3, r5
	clz	r3, r3
	cmp	r2, #0
	mov	r3, r3, lsr #5
	moveq	r3, #0
	cmp	r3, #0
	beq	.L2496
	ldr	r3, [r8, #-3608]
	ldrh	r3, [r3, r7]
	cmp	r3, #40
	movhi	r3, #0
	strhib	r3, [r8, #-3516]
.L2496:
	ldrb	r3, [r5, #8]	@ zero_extendqisi2
	ldr	r2, [r4, #-3608]
	cmp	r3, #0
	ldr	fp, .L2540+36
	ldrh	r3, [r2, r7]
	bne	.L2497
	cmp	r3, #0
	mov	r0, r9
	ldrneh	r1, [fp]
	moveq	r3, #2
	addne	r3, r3, r1
	mov	r1, #0
	uxthne	r3, r3
	strh	r3, [r2, r7]	@ movhi
	ldr	r3, [r4, #-3324]
	add	r3, r3, #1
	str	r3, [r4, #-3324]
	bl	ftl_set_blk_mode
	b	.L2499
.L2497:
	add	r3, r3, #1
	strh	r3, [r2, r7]	@ movhi
	ldr	r2, [r4, #-3368]
	mov	r1, r9, lsr #5
	ldr	r3, [r4, #-3320]
	mov	r0, #1
	add	r3, r3, #1
	str	r3, [r4, #-3320]
	ldr	ip, [r2, r1, asl #2]
	and	r3, r9, #31
	orr	r3, ip, r0, asl r3
	str	r3, [r2, r1, asl #2]
.L2499:
	ldr	r3, [r4, #-3608]
	ldr	r2, [r4, #-3312]
	ldr	r0, [r4, #-3324]
	ldrh	r3, [r3, r7]
	cmp	r3, r2
	ldrh	r2, [fp]
	strhi	r3, [r8, #-3312]
	ldr	r3, [r4, #-3320]
	mla	r0, r0, r2, r3
	ldr	r3, .L2540+24
	ldrh	r1, [r3]
	bl	__aeabi_uidiv
	ldr	r2, [r4, #-480]
	ldr	r1, [r4, #-3612]
	ldr	r3, [r2, #16]
	add	r3, r3, #1
	str	r3, [r2, #16]
	mov	r3, #36
	add	r2, r1, #4
	mla	r3, r3, r6, r1
	add	r3, r3, #40
	str	r0, [r4, #-3316]
.L2501:
	add	r2, r2, #36
	cmp	r2, r3
	ldrne	r1, [r2, #-36]
	bicne	r1, r1, #1020
	bicne	r1, r1, #3
	strne	r1, [r2, #-36]
	bne	.L2501
.L2538:
	ldrb	r3, [r10, #148]	@ zero_extendqisi2
	cmp	r3, #0
	beq	.L2503
	ldrb	r3, [r5, #8]	@ zero_extendqisi2
	mov	r2, r6
	ldr	r0, [r4, #-3612]
	cmp	r3, #1
	moveq	r1, #0
	movne	r1, #1
	bl	FlashEraseBlocks
.L2503:
	ldrb	r1, [r5, #8]	@ zero_extendqisi2
	mov	r2, r6
	ldr	r0, [r4, #-3612]
	mov	fp, #0
	bl	FlashEraseBlocks
	add	r1, r5, #16
	mov	r2, fp
	mov	ip, #36
.L2505:
	uxth	r3, fp
	cmp	r3, r6
	bcs	.L2539
	mul	r3, ip, fp
	ldr	lr, [r4, #-3612]
	add	r0, lr, r3
	ldr	r3, [lr, r3]
	cmn	r3, #1
	bne	.L2506
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
.L2506:
	add	fp, fp, #1
	add	r1, r1, #2
	b	.L2505
.L2539:
	cmp	r2, #0
	beq	.L2508
	mov	r0, r9
	bl	update_multiplier_value
	bl	FtlBbmTblFlush
.L2508:
	ldrb	r3, [r5, #7]	@ zero_extendqisi2
	cmp	r3, #0
	bne	.L2509
	ldr	r3, [r4, #-3544]
.L2535:
	mvn	r2, #0
	strh	r2, [r3, r7]	@ movhi
	b	.L2487
.L2509:
	movw	r2, #2388
	ldrh	r2, [r10, r2]
	strh	r9, [r5]	@ movhi
	smulbb	r3, r2, r3
	mov	r2, #0
	strh	r2, [r5, #2]	@ movhi
	strb	r2, [r5, #6]
	ldr	r2, [r4, #-3332]
	ldr	r1, [r4, #-3544]
	uxth	r3, r3
	strh	r3, [r5, #4]	@ movhi
	str	r2, [r5, #12]
	add	r2, r2, #1
	str	r2, [r4, #-3332]
	ldrh	r2, [r5]
	mov	r2, r2, asl #1
	strh	r3, [r1, r2]	@ movhi
.L2486:
	mov	r0, #0
	add	sp, sp, #20
	@ sp needed
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L2541:
	.align	2
.L2540:
	.word	.LANCHOR2
	.word	.LANCHOR2-3428
	.word	.LANCHOR0
	.word	.LANCHOR0+2340
	.word	.LANCHOR2-3532
	.word	.LANCHOR2-3528
	.word	.LANCHOR0+2328
	.word	.LANCHOR0+2320
	.word	.LANCHOR2-3524
	.word	.LANCHOR0+2380
	.fnend
	.size	allocate_data_superblock, .-allocate_data_superblock
	.align	2
	.global	FtlGcFreeBadSuperBlk
	.type	FtlGcFreeBadSuperBlk, %function
FtlGcFreeBadSuperBlk:
	.fnstart
	@ args = 0, pretend = 0, frame = 8
	@ frame_needed = 0, uses_anonymous_args = 0
	ldr	r3, .L2556
	stmfd	sp!, {r0, r1, r2, r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.save {r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.pad #12
	ldrh	r2, [r3, #-2]
	cmp	r2, #0
	beq	.L2544
	sub	r10, r3, #2
	mov	r7, r0
	mov	r9, #0
	mov	r5, r3
	mov	r8, r10
.L2543:
	ldr	r3, .L2556+4
	add	r2, r3, #2320
	ldrh	r1, [r2]
	uxth	r2, r9
	cmp	r1, r2
	bls	.L2553
	add	r3, r3, r2
	mov	r1, r7
	mov	fp, #0
	ldrb	r0, [r3, #2348]	@ zero_extendqisi2
	bl	V2P_block
	mov	r6, r0
.L2545:
	ldrh	r3, [r10]
	uxth	r4, fp
	cmp	r3, r4
	bls	.L2554
	mov	r3, r4, asl #1
	add	ip, r5, r3
	ldrh	r3, [r5, r3]
	cmp	r3, r6
	bne	.L2546
	mov	r1, r6
	ldr	r0, .L2556+8
	str	ip, [sp, #4]
	bl	printk
	mov	r0, r6
	bl	FtlBbmMapBadBlock
	bl	FtlBbmTblFlush
	ldrh	r2, [r10]
	ldr	ip, [sp, #4]
	mov	r3, ip
.L2547:
	cmp	r4, r2
	ldrcch	r1, [r3, #2]
	addcc	r4, r4, #1
	uxthcc	r4, r4
	strcch	r1, [r3], #2	@ movhi
	bcc	.L2547
.L2555:
	sub	r2, r2, #1
	strh	r2, [r8]	@ movhi
.L2546:
	add	fp, fp, #1
	b	.L2545
.L2554:
	add	r9, r9, #1
	b	.L2543
.L2553:
	bl	FtlGcReFreshBadBlk
.L2544:
	mov	r0, #0
	add	sp, sp, #12
	@ sp needed
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L2557:
	.align	2
.L2556:
	.word	.LANCHOR2-2656
	.word	.LANCHOR0
	.word	.LC129
	.fnend
	.size	FtlGcFreeBadSuperBlk, .-FtlGcFreeBadSuperBlk
	.align	2
	.global	update_vpc_list
	.type	update_vpc_list, %function
update_vpc_list:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	ldr	r2, .L2568
	stmfd	sp!, {r3, r4, r5, lr}
	.save {r3, r4, r5, lr}
	mov	r3, r0, asl #1
	ldr	r1, [r2, #-3544]
	mov	r4, r0
	ldrh	r3, [r1, r3]
	cmp	r3, #0
	bne	.L2559
	sub	r1, r2, #3280
	ldrh	r0, [r1, #-4]
	cmp	r0, r4
	mvneq	r3, #0
	streqh	r3, [r1, #-4]	@ movhi
	beq	.L2561
	sub	r1, r2, #3520
	ldrh	r1, [r1, #-4]
	cmp	r1, r4
	beq	.L2567
	sub	r1, r2, #3472
	ldrh	r1, [r1, #-4]
	cmp	r1, r4
	beq	.L2567
	sub	r2, r2, #3424
	ldrh	r2, [r2, #-4]
	cmp	r2, r4
	beq	.L2567
.L2561:
	ldr	r5, .L2568+4
	mov	r1, r4
	sub	r0, r5, #12
	bl	List_remove_node
	ldrh	r3, [r5]
	mov	r0, r4
	sub	r3, r3, #1
	strh	r3, [r5]	@ movhi
	bl	free_data_superblock
	mov	r0, r4
	bl	FtlGcFreeBadSuperBlk
	mov	r0, #1
	ldmfd	sp!, {r3, r4, r5, pc}
.L2559:
	bl	List_update_data_list
.L2567:
	mov	r0, #0
	ldmfd	sp!, {r3, r4, r5, pc}
.L2569:
	.align	2
.L2568:
	.word	.LANCHOR2
	.word	.LANCHOR2-3536
	.fnend
	.size	update_vpc_list, .-update_vpc_list
	.align	2
	.global	decrement_vpc_count
	.type	decrement_vpc_count, %function
decrement_vpc_count:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	stmfd	sp!, {r3, r4, r5, r6, r7, lr}
	.save {r3, r4, r5, r6, r7, lr}
	movw	r3, #65535
	cmp	r0, r3
	mov	r4, r0
	beq	.L2571
	ldr	r6, .L2581
	mov	r5, r0, asl #1
	ldr	r3, [r6, #-3544]
	ldrh	r2, [r3, r5]
	cmp	r2, #0
	subne	r2, r2, #1
	strneh	r2, [r3, r5]	@ movhi
	bne	.L2571
	mov	r1, r4
	ldr	r0, .L2581+4
	bl	printk
	ldr	r3, [r6, #-3544]
	sub	r7, r6, #3520
	mov	r2, #32
	sub	r0, r7, #12
	mov	r1, r4
	strh	r2, [r3, r5]	@ movhi
	bl	test_node_in_list
	cmp	r0, #0
	beq	.L2573
	mov	r1, r4
	sub	r0, r7, #12
	bl	List_remove_node
	ldrh	r3, [r7, #-8]
	mov	r0, r4
	sub	r3, r3, #1
	strh	r3, [r7, #-8]	@ movhi
	bl	INSERT_DATA_LIST
	ldr	r3, [r6, #-3544]
	ldr	r0, .L2581+8
	mov	r1, r4
	ldrh	r2, [r3, r5]
	bl	printk
.L2573:
	mov	r0, r4
	bl	FtlGcRefreshBlock
	b	.L2576
.L2571:
	ldr	r5, .L2581+12
	movw	r3, #65535
	ldrh	r0, [r5]
	cmp	r0, r3
	streqh	r4, [r5]	@ movhi
	beq	.L2576
	cmp	r0, r4
	beq	.L2576
	bl	update_vpc_list
	strh	r4, [r5]	@ movhi
	adds	r0, r0, #0
	movne	r0, #1
	ldmfd	sp!, {r3, r4, r5, r6, r7, pc}
.L2576:
	mov	r0, #0
	ldmfd	sp!, {r3, r4, r5, r6, r7, pc}
.L2582:
	.align	2
.L2581:
	.word	.LANCHOR2
	.word	.LC130
	.word	.LC131
	.word	.LANCHOR2-380
	.fnend
	.size	decrement_vpc_count, .-decrement_vpc_count
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
	ldr	r5, .L2596
	ldr	r6, .L2596+4
	add	r3, r0, r3, asl #1
	add	r7, r5, #2320
	ldrh	r3, [r3, #16]
.L2587:
	movw	r1, #65535
	cmp	r3, r1
	bne	.L2595
.L2589:
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
	b	.L2587
.L2595:
	ldrb	r2, [r4, #8]	@ zero_extendqisi2
	cmp	r2, #1
	bne	.L2590
	ldrb	r3, [r5, #148]	@ zero_extendqisi2
	cmp	r3, #0
	bne	.L2590
	ldrh	r3, [r4, #2]
	mov	r3, r3, asl #1
	ldrh	r3, [r6, r3]
	cmp	r3, r1
	bne	.L2590
	ldrh	r3, [r4, #4]
	ldrh	r0, [r4]
	sub	r3, r3, #1
	strh	r3, [r4, #4]	@ movhi
	bl	decrement_vpc_count
	ldrh	r2, [r4, #4]
	cmp	r2, #0
	bne	.L2589
	ldrh	r3, [r4, #2]
	strb	r2, [r4, #6]
	add	r3, r3, #1
	strh	r3, [r4, #2]	@ movhi
	ldmfd	sp!, {r3, r4, r5, r6, r7, pc}
.L2590:
	ldr	r1, .L2596
	ldrb	r3, [r1, #148]	@ zero_extendqisi2
	cmp	r3, #0
	ldmeqfd	sp!, {r3, r4, r5, r6, r7, pc}
	cmp	r2, #1
	ldmnefd	sp!, {r3, r4, r5, r6, r7, pc}
	movw	r3, #2390
	ldrh	r2, [r4, #2]
	ldrh	r3, [r1, r3]
	cmp	r2, r3
	ldmccfd	sp!, {r3, r4, r5, r6, r7, pc}
	ldr	r2, .L2596+8
	ldrh	r3, [r4]
	ldrh	ip, [r4, #4]
	ldr	r0, [r2, #-3544]
	mov	r3, r3, asl #1
	ldrh	r2, [r0, r3]
	rsb	r2, ip, r2
	strh	r2, [r0, r3]	@ movhi
	movw	r2, #2388
	mov	r3, #0
	ldrh	r2, [r1, r2]
	strh	r3, [r4, #4]	@ movhi
	strb	r3, [r4, #6]
	strh	r2, [r4, #2]	@ movhi
	ldmfd	sp!, {r3, r4, r5, r6, r7, pc}
.L2597:
	.align	2
.L2596:
	.word	.LANCHOR0
	.word	.LANCHOR2-2620
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
	ldr	r7, .L2616
	ldr	r9, .L2616+4
	add	r3, r0, r3, asl #1
	sub	r8, r7, #2320
	ldrh	r2, [r3, #16]
.L2599:
	movw	r1, #65535
	cmp	r2, r1
	ldr	r6, .L2616
	bne	.L2615
.L2600:
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
	b	.L2599
.L2615:
	ldrb	r3, [r4, #8]	@ zero_extendqisi2
	ldrh	r5, [r4, #2]
	cmp	r3, #1
	ldrh	r3, [r4, #4]
	bne	.L2602
	ldrb	r0, [r8, #148]	@ zero_extendqisi2
	cmp	r0, #0
	bne	.L2602
	mov	r0, r5, asl #1
	ldrh	r0, [r9, r0]
	cmp	r0, r1
	bne	.L2602
	sub	r3, r3, #1
	ldrh	r0, [r4]
	strh	r3, [r4, #4]	@ movhi
	bl	decrement_vpc_count
	b	.L2600
.L2602:
	ldr	r7, .L2616+8
	orr	r5, r5, r2, asl #10
	sub	r3, r3, #1
	strh	r3, [r4, #4]	@ movhi
.L2603:
	ldrb	r3, [r4, #6]	@ zero_extendqisi2
	movw	r2, #65535
	ldrh	r0, [r6]
.L2605:
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
	beq	.L2605
	strb	r3, [r4, #6]
	ldrb	r3, [r4, #8]	@ zero_extendqisi2
	cmp	r3, #1
	bne	.L2610
	ldrb	r3, [r7, #148]	@ zero_extendqisi2
	cmp	r3, #0
	bne	.L2607
	ldrh	r3, [r4, #2]
	ldr	r2, .L2616+4
	mov	r3, r3, asl #1
	ldrh	r2, [r2, r3]
	movw	r3, #65535
	cmp	r2, r3
	bne	.L2607
	ldrh	r3, [r4, #4]
	cmp	r3, #0
	beq	.L2607
	sub	r3, r3, #1
	ldrh	r0, [r4]
	strh	r3, [r4, #4]	@ movhi
	bl	decrement_vpc_count
	b	.L2603
.L2607:
	ldr	r1, .L2616+8
	ldrb	r3, [r1, #148]	@ zero_extendqisi2
	cmp	r3, #0
	beq	.L2610
	movw	r3, #2390
	ldrh	r2, [r4, #2]
	ldrh	r3, [r1, r3]
	cmp	r2, r3
	bcc	.L2610
	ldr	r2, .L2616+12
	ldrh	r3, [r4]
	ldrh	ip, [r4, #4]
	ldr	r0, [r2, #-3544]
	mov	r3, r3, asl #1
	ldrh	r2, [r0, r3]
	rsb	r2, ip, r2
	strh	r2, [r0, r3]	@ movhi
	movw	r2, #2388
	mov	r3, #0
	ldrh	r2, [r1, r2]
	strh	r3, [r4, #4]	@ movhi
	strb	r3, [r4, #6]
	strh	r2, [r4, #2]	@ movhi
.L2610:
	mov	r0, r5
	ldmfd	sp!, {r3, r4, r5, r6, r7, r8, r9, pc}
.L2617:
	.align	2
.L2616:
	.word	.LANCHOR0+2320
	.word	.LANCHOR2-2620
	.word	.LANCHOR0
	.word	.LANCHOR2
	.fnend
	.size	get_new_active_ppa, .-get_new_active_ppa
	.align	2
	.global	FtlVpcTblFlush
	.type	FtlVpcTblFlush, %function
FtlVpcTblFlush:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	stmfd	sp!, {r3, r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.save {r3, r4, r5, r6, r7, r8, r9, r10, fp, lr}
	ldr	r4, .L2636
	ldr	r3, [r4, #-3616]
	cmp	r3, #0
	bne	.L2620
	ldr	r2, [r4, #-524]
	sub	r6, r4, #3600
	ldr	r7, [r4, #-500]
	mov	r1, #255
	ldr	r5, .L2636+4
	str	r2, [r4, #1768]
	sub	r2, r4, #3296
	str	r7, [r4, #1772]
	ldrh	r2, [r2, #-4]
	str	r3, [r7, #12]
	strh	r2, [r7, #2]	@ movhi
	ldr	r2, .L2636+8
	strh	r2, [r7]	@ movhi
	ldr	r2, [r4, #-3292]
	stmib	r7, {r2, r3}
	ldr	r3, .L2636+12
	ldrh	r2, [r6, #78]
	str	r3, [r4, #-3600]
	ldr	r3, .L2636+16
	str	r3, [r4, #-3596]
	add	r3, r6, #300
	ldrh	r3, [r3, #6]
	strh	r3, [r6, #8]	@ movhi
	movw	r3, #2342
	ldrh	r3, [r5, r3]
	strb	r3, [r4, #-3590]
	sub	r3, r4, #3520
	ldrh	r3, [r3, #-4]
	strh	r3, [r6, #14]	@ movhi
	ldrb	r3, [r4, #-3518]	@ zero_extendqisi2
	orr	r3, r3, r2, asl #6
	strh	r3, [r6, #16]	@ movhi
	ldrb	r3, [r4, #-3516]	@ zero_extendqisi2
	ldrh	r2, [r6, #126]
	strb	r3, [r4, #-3589]
	sub	r3, r4, #3472
	ldrh	r3, [r3, #-4]
	strh	r3, [r6, #18]	@ movhi
	ldrb	r3, [r4, #-3470]	@ zero_extendqisi2
	orr	r3, r3, r2, asl #6
	strh	r3, [r6, #20]	@ movhi
	ldrb	r3, [r4, #-3468]	@ zero_extendqisi2
	strb	r3, [r4, #-3588]
	sub	r3, r4, #3424
	ldrh	r3, [r3, #-4]
	strh	r3, [r6, #22]	@ movhi
	ldrh	r2, [r6, #174]
	ldrb	r3, [r4, #-3422]	@ zero_extendqisi2
	ldr	r0, [r4, #1768]
	orr	r3, r3, r2, asl #6
	strh	r3, [r6, #24]	@ movhi
	ldrb	r3, [r4, #-3420]	@ zero_extendqisi2
	strb	r3, [r4, #-3587]
	ldr	r3, [r4, #-3324]
	str	r3, [r4, #-3568]
	ldr	r3, [r4, #-3332]
	str	r3, [r4, #-3560]
	ldr	r3, [r4, #-3328]
	str	r3, [r4, #-3564]
	sub	r3, r4, #2656
	ldrh	r2, [r3, #-10]
	ldrh	r3, [r3, #-8]
	strh	r2, [r6, #44]	@ movhi
	strh	r3, [r6, #46]	@ movhi
	movw	r3, #2398
	ldrh	r2, [r5, r3]
	bl	ftl_memset
	mov	r1, r6
	mov	r2, #48
	movw	r6, #2328
	ldr	r0, [r4, #1768]
	bl	ftl_memcpy
	ldrh	r2, [r5, r6]
	ldr	r0, [r4, #1768]
	ldr	r1, [r4, #-3544]
	mov	r2, r2, asl #1
	add	r0, r0, #48
	bl	ftl_memcpy
	ldrh	r2, [r5, r6]
	ldr	r0, [r4, #1768]
	ldr	r1, [r4, #-3368]
	mov	r3, r2, asl #1
	mov	r2, r2, lsr #3
	add	r3, r3, #51
	add	r2, r2, #4
	bic	r3, r3, #3
	add	r0, r0, r3
	bl	ftl_memcpy
	add	r3, r5, #2432
	ldrh	r3, [r3]
	cmp	r3, #0
	beq	.L2621
	ldrh	r2, [r5, r6]
	ldr	r0, [r4, #1768]
	ldr	r1, [r4, #-452]
	mov	r3, r2, lsr #3
	add	r3, r3, r2, asl #1
	movw	r2, #2424
	add	r3, r3, #52
	ldrh	r2, [r5, r2]
	ubfx	r3, r3, #2, #14
	mov	r2, r2, asl #2
	add	r0, r0, r3, asl #2
	bl	ftl_memcpy
.L2621:
	mov	r0, #0
	ldr	r8, .L2636
	bl	FtlUpdateVaildLpn
	ldr	r10, .L2636+20
	mov	r6, #0
	movw	r9, #65535
.L2622:
	ldr	r3, [r4, #-524]
	ldrh	r2, [r10]
	ldr	fp, .L2636+24
	str	r3, [r4, #1768]
	ldr	r3, [r4, #-500]
	ldrh	r1, [r10, #2]
	str	r3, [r4, #1772]
	orr	r3, r1, r2, asl #10
	str	r3, [r4, #1764]
	ldrh	r3, [fp]
	sub	r3, r3, #1
	cmp	r1, r3
	blt	.L2623
	mov	r3, #0
	ldrh	r9, [r10, #4]
	strh	r3, [r10, #2]	@ movhi
	strh	r2, [r10, #4]	@ movhi
	bl	FtlFreeSysBlkQueueOut
	ldr	r3, [r8, #-3332]
	add	r2, r3, #1
	str	r2, [r8, #-3332]
	str	r3, [r8, #-3292]
	mov	r2, r0, asl #10
	strh	r0, [r10]	@ movhi
	str	r2, [r8, #1764]
	str	r3, [r7, #4]
	strh	r0, [r7, #2]	@ movhi
.L2623:
	ldrb	r3, [r5, #32]	@ zero_extendqisi2
	cmp	r3, #0
	beq	.L2624
	ldr	r3, .L2636+28
	ldr	r0, [r4, #-524]
	ldrh	r1, [r3]
	bl	js_hash
	str	r0, [r7, #12]
.L2624:
	mov	r1, #1
	ldr	r0, .L2636+32
	mov	r2, r1
	mov	r3, r1
	bl	FlashProgPages
	ldr	r3, .L2636+20
	ldr	r2, .L2636+20
	ldrh	r3, [r3, #2]
	add	r3, r3, #1
	uxth	r3, r3
	strh	r3, [r2, #2]	@ movhi
	ldr	r2, [r4, #1760]
	cmn	r2, #1
	bne	.L2625
	cmp	r3, #1
	add	r6, r6, #1
	ldreqh	r3, [fp]
	uxth	r6, r6
	subeq	r3, r3, #1
	streqh	r3, [r10, #2]	@ movhi
	cmp	r6, #3
	bls	.L2622
	ldr	r0, .L2636+36
	mov	r2, r6
	ldr	r1, [r4, #1764]
	bl	printk
	mov	r3, #1
	str	r3, [r4, #-3616]
	b	.L2620
.L2625:
	cmp	r2, #256
	cmpne	r3, #1
	beq	.L2622
	movw	r3, #65535
	cmp	r9, r3
	beq	.L2620
	mov	r0, r9
	mov	r1, #1
	bl	FtlFreeSysBlkQueueIn
.L2620:
	mov	r0, #0
	ldmfd	sp!, {r3, r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L2637:
	.align	2
.L2636:
	.word	.LANCHOR2
	.word	.LANCHOR0
	.word	-3932
	.word	1179929683
	.word	1342177379
	.word	.LANCHOR2-3300
	.word	.LANCHOR0+2390
	.word	.LANCHOR0+2398
	.word	.LANCHOR2+1760
	.word	.LC132
	.fnend
	.size	FtlVpcTblFlush, .-FtlVpcTblFlush
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
	ldr	r5, .L2655
	ldr	r3, [r5, #-3616]
	cmp	r3, #0
	bne	.L2638
	ldr	r8, .L2655+4
	ldrb	r6, [r8, #148]	@ zero_extendqisi2
	cmp	r6, #0
	beq	.L2654
	ldrb	r6, [r0, #8]	@ zero_extendqisi2
	cmp	r6, #1
	ldreqh	fp, [r0, #4]
	beq	.L2640
	mov	r6, r3
.L2654:
	mov	fp, #12
.L2640:
	ldr	r7, [r5, #-500]
	mvn	r3, #0
	str	r3, [sp, #20]
	mov	r9, #0
	ldr	r3, [r5, #-524]
	movw	r2, #61589
	str	r7, [sp, #16]
	mov	r4, r0
	ldr	r10, .L2655
	str	r3, [sp, #12]
	mvn	r3, #2
	str	r3, [r7, #8]
	mvn	r3, #1
	str	r3, [r7, #12]
	ldrh	r3, [r0]
	strh	r9, [r7]	@ movhi
	strh	r3, [r7, #2]	@ movhi
	ldr	r3, [r5, #-524]
	str	r2, [r3]
	ldr	r2, .L2655+8
	ldr	r3, [r5, #-524]
	str	r2, [r3, #4]
.L2641:
	subs	fp, fp, #1
	bcc	.L2644
	ldrh	r3, [r4, #4]
	cmp	r3, #0
	bne	.L2642
.L2644:
	ldrh	r3, [r4]
	ldr	r1, [r5, #-3544]
	ldrh	r0, [r4, #4]
	mov	r3, r3, asl #1
	ldrh	r2, [r1, r3]
	rsb	r2, r0, r2
	strh	r2, [r1, r3]	@ movhi
	movw	r3, #2388
	ldrh	r3, [r8, r3]
	strh	r3, [r4, #2]	@ movhi
	mov	r3, #0
	strb	r3, [r4, #6]
	strh	r3, [r4, #4]	@ movhi
	b	.L2638
.L2642:
	mov	r0, r4
	bl	get_new_active_ppa
	cmn	r0, #1
	str	r0, [sp, #8]
	beq	.L2644
	ldr	r3, [r5, #-3328]
	mov	r1, #1
	mov	r2, r6
	add	r0, sp, #4
	str	r3, [r7, #4]
	add	r3, r3, #1
	cmn	r3, #1
	moveq	r3, r9
	str	r3, [r10, #-3328]
	mov	r3, #0
	bl	FlashProgPages
	ldrh	r0, [r4]
	bl	decrement_vpc_count
	b	.L2641
.L2638:
	add	sp, sp, #44
	@ sp needed
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L2656:
	.align	2
.L2655:
	.word	.LANCHOR2
	.word	.LANCHOR0
	.word	305419896
	.fnend
	.size	FtlSuperblockPowerLostFix, .-FtlSuperblockPowerLostFix
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
	ldr	r8, .L2671
	cmp	r2, r3
	blt	.L2658
	uxth	r0, r0
	mov	r0, r0, asl #1
	ldrh	r10, [r5, r0]
	cmp	r10, #0
	beq	.L2658
	ldr	r3, [r4, #32]
	cmp	r3, #0
	bne	.L2658
	mov	r2, #1
	str	r2, [r4, #32]
	strh	r3, [r5, r0]	@ movhi
	ldrh	r3, [r4, #8]
	ldrh	r2, [r4, #2]
	sub	r3, r3, #1
	strh	r3, [r4, #8]	@ movhi
	movw	r3, #2390
	ldrh	r3, [r8, r3]
	cmp	r2, r3
	bcc	.L2659
	mov	r0, r4
	bl	ftl_map_blk_alloc_new_blk
.L2659:
	ldr	r5, .L2671+4
	mov	fp, #0
.L2660:
	ldrh	r3, [r4, #6]
	uxth	r6, fp
	cmp	r3, r6
	bls	.L2670
	ldr	r3, [r7, r6, asl #2]
	add	ip, r7, r6, asl #2
	cmp	r10, r3, lsr #10
	bne	.L2661
	ldr	r3, [r5, #-520]
	mov	r1, #1
	ldr	r9, [r5, #-500]
	mov	r2, r1
	ldr	r0, .L2671+8
	str	r3, [r5, #1768]
	str	r9, [r5, #1772]
	ldr	r3, [r7, r6, asl #2]
	str	ip, [sp, #4]
	str	r3, [r5, #1764]
	bl	FlashReadPages
	ldr	r3, [r5, #1760]
	cmn	r3, #1
	ldr	r3, .L2671+4
	ldr	ip, [sp, #4]
	bne	.L2662
.L2664:
	mov	r2, #0
	ldr	r0, .L2671+12
	str	r2, [ip]
	ldr	r1, [r3, #1764]
	ldrh	r2, [r9, #8]
	str	r3, [sp, #4]
	bl	printk
	mov	r2, #1
	ldr	r3, [sp, #4]
	str	r2, [r3, #-3616]
	b	.L2663
.L2662:
	ldrh	r1, [r9, #8]
	cmp	r1, r6
	bne	.L2664
	ldrh	r0, [r9]
	ldrh	r2, [r4, #4]
	cmp	r0, r2
	bne	.L2664
	mov	r0, r4
	ldr	r2, [r5, #1768]
	bl	FtlMapWritePage
.L2661:
	add	fp, fp, #1
	b	.L2660
.L2670:
	mov	r0, r10
	mov	r1, #1
	bl	FtlFreeSysBlkQueueIn
	mov	r3, #0
	str	r3, [r4, #32]
.L2658:
	movw	r3, #2390
	ldrh	r2, [r4, #2]
	ldrh	r3, [r8, r3]
	cmp	r2, r3
	bcc	.L2663
	mov	r0, r4
	bl	ftl_map_blk_alloc_new_blk
.L2663:
	mov	r0, #0
	add	sp, sp, #12
	@ sp needed
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L2672:
	.align	2
.L2671:
	.word	.LANCHOR0
	.word	.LANCHOR2
	.word	.LANCHOR2+1760
	.word	.LC133
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
	ldr	r5, .L2685
	ldr	r7, [r0, #12]
	ldr	r9, [r0, #24]
	ldr	r6, [r5, #-3616]
	cmp	r6, #0
	bne	.L2674
	ldrh	r3, [r0]
	movw	r2, #65535
	mov	r4, r0
	cmp	r3, r2
	bne	.L2675
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
	b	.L2674
.L2675:
	mov	r3, r3, asl #1
	ldr	r10, .L2685+4
	mov	r1, #255
	ldrh	r8, [r7, r3]
	ldrh	r3, [r0, #2]
	ldr	r7, [r5, #-500]
	orr	r3, r3, r8, asl #10
	str	r3, [r5, #1764]
	ldr	r3, [r5, #-524]
	str	r7, [r5, #1772]
	str	r3, [r5, #1768]
	ldr	r3, [r0, #28]
	str	r3, [r7, #4]
	ldr	r3, .L2685+8
	strh	r3, [r7, #8]	@ movhi
	ldrh	r3, [r0, #4]
	strh	r8, [r7, #2]	@ movhi
	strh	r3, [r7]	@ movhi
	movw	r3, #2390
	ldrh	r2, [r10, r3]
	ldr	r0, [r5, #-524]
	mov	r2, r2, asl #3
	bl	ftl_memset
	mov	r2, r6
.L2676:
	ldrh	r1, [r4, #6]
	uxth	r3, r2
	cmp	r1, r3
	bls	.L2684
	ldr	r1, [r9, r3, asl #2]
	cmp	r8, r1, lsr #10
	bne	.L2677
	add	r6, r6, #1
	ldr	r1, [r5, #-524]
	uxth	r6, r6
	str	r3, [r1, r6, asl #3]
	ldr	r1, [r9, r3, asl #2]
	ldr	r3, [r5, #-524]
	add	r3, r3, r6, asl #3
	str	r1, [r3, #4]
.L2677:
	add	r2, r2, #1
	b	.L2676
.L2684:
	ldrb	r3, [r10, #32]	@ zero_extendqisi2
	cmp	r3, #0
	beq	.L2679
	ldr	r2, .L2685+4
	movw	r3, #2398
	ldr	r0, [r5, #1768]
	ldrh	r1, [r2, r3]
	bl	js_hash
	str	r0, [r7, #12]
.L2679:
	mov	r1, #1
	mov	r3, #0
	ldr	r0, .L2685+12
	mov	r2, r1
	bl	FlashProgPages
	ldrh	r3, [r4, #2]
	mov	r0, r4
	add	r3, r3, #1
	strh	r3, [r4, #2]	@ movhi
	bl	ftl_map_blk_gc
.L2674:
	mov	r0, #0
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, pc}
.L2686:
	.align	2
.L2685:
	.word	.LANCHOR2
	.word	.LANCHOR0
	.word	-1291
	.word	.LANCHOR2+1760
	.fnend
	.size	Ftl_write_map_blk_to_last_page, .-Ftl_write_map_blk_to_last_page
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
	ldr	r10, .L2706
	mov	r8, r1
	ldr	fp, .L2706+4
	mov	r5, #0
	str	r2, [sp]
	mov	r9, r10
.L2688:
	ldr	r3, [r10, #-3348]
	ldr	r6, .L2706
	add	r3, r3, #1
	str	r3, [r10, #-3348]
	ldrh	r3, [fp]
	ldrh	r2, [r4, #2]
	sub	r3, r3, #1
	cmp	r2, r3
	bge	.L2689
	ldrh	r2, [r4]
	movw	r3, #65535
	cmp	r2, r3
	bne	.L2690
.L2689:
	mov	r0, r4
	bl	Ftl_write_map_blk_to_last_page
.L2690:
	ldr	r1, [r9, #-3616]
	cmp	r1, #0
	bne	.L2691
	ldrh	r3, [r4]
	ldr	r2, [r4, #12]
	ldr	r0, [r10, #-500]
	mov	r3, r3, asl #1
	ldrh	r7, [r2, r3]
	mov	r2, #16
	ldrh	r3, [r4, #2]
	str	r0, [r10, #1772]
	orr	r3, r3, r7, asl #10
	str	r3, [r10, #1764]
	ldr	r3, [sp]
	str	r3, [r10, #1768]
	bl	ftl_memset
	ldr	r2, [r4, #28]
	ldr	r3, [r10, #1772]
	str	r2, [r3, #4]
	strh	r8, [r3, #8]	@ movhi
	ldrh	r2, [r4, #4]
	strh	r7, [r3, #2]	@ movhi
	strh	r2, [r3]	@ movhi
	ldr	r2, .L2706+8
	ldrb	r2, [r2, #32]	@ zero_extendqisi2
	cmp	r2, #0
	beq	.L2692
	ldr	r2, .L2706+12
	ldr	r0, [r10, #1768]
	str	r3, [sp, #4]
	ldrh	r1, [r2]
	bl	js_hash
	ldr	r3, [sp, #4]
	str	r0, [r3, #12]
.L2692:
	mov	r1, #1
	ldr	r0, .L2706+16
	mov	r2, r1
	mov	r3, r1
	bl	FlashProgPages
	ldrh	r3, [r4, #2]
	add	r3, r3, #1
	uxth	r3, r3
	strh	r3, [r4, #2]	@ movhi
	ldr	r2, [r9, #1760]
	cmn	r2, #1
	bne	.L2693
	ldr	r0, .L2706+20
	add	r5, r5, #1
	ldr	r1, [r10, #1764]
	bl	printk
	ldrh	r3, [r4, #2]
	uxth	r5, r5
	cmp	r3, #2
	ldrls	r3, .L2706+4
	ldrlsh	r3, [r3]
	subls	r3, r3, #1
	strlsh	r3, [r4, #2]	@ movhi
	cmp	r5, #3
	bls	.L2688
	ldr	r0, .L2706+24
	mov	r2, r5
	ldr	r1, [r6, #1764]
	bl	printk
	mov	r3, #1
	str	r3, [r6, #-3616]
	b	.L2691
.L2693:
	cmp	r2, #0
	strneh	r7, [r4, #40]	@ movhi
	cmp	r2, #256
	cmpne	r3, #1
	beq	.L2697
	ldr	r3, [r4, #36]
	cmp	r3, #0
	beq	.L2698
.L2697:
	mov	r3, #0
	str	r3, [r4, #36]
	b	.L2688
.L2698:
	ldr	r2, [r6, #1764]
	ldr	r3, [r4, #24]
	str	r2, [r3, r8, asl #2]
.L2691:
	mov	r0, #0
	add	sp, sp, #12
	@ sp needed
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L2707:
	.align	2
.L2706:
	.word	.LANCHOR2
	.word	.LANCHOR0+2390
	.word	.LANCHOR0
	.word	.LANCHOR0+2398
	.word	.LANCHOR2+1760
	.word	.LC134
	.word	.LC135
	.fnend
	.size	FtlMapWritePage, .-FtlMapWritePage
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
	ldr	r5, .L2710
	mul	r4, r4, r0
	sub	r0, r5, #432
	ldr	r3, [r5, #-3380]
	add	r2, r3, r4
	ldrh	r1, [r3, r4]
	ldr	r2, [r2, #8]
	bl	FtlMapWritePage
	ldr	r3, [r5, #-3380]
	mov	r0, #0
	add	r4, r3, r4
	ldr	r3, [r4, #4]
	bic	r3, r3, #-2147483648
	str	r3, [r4, #4]
	ldmfd	sp!, {r3, r4, r5, pc}
.L2711:
	.align	2
.L2710:
	.word	.LANCHOR2
	.fnend
	.size	flush_l2p_region, .-flush_l2p_region
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
	ldrh	r10, [r0, #6]
	mov	r1, #0
	ldr	r8, [r0, #24]
	mov	r6, #0
	ldr	r5, .L2755
	str	r3, [sp, #4]
	mov	r2, r10, asl #2
	ldrh	r3, [r0, #8]
	ldr	r9, [r0, #12]
	mov	r0, r8
	ldr	fp, .L2755+4
	str	r3, [sp]
	bl	ftl_memset
	ldr	r3, [r5, #-524]
	ldr	r7, [r5, #-500]
	str	r6, [r4, #32]
	str	r3, [r5, #1768]
	mvn	r3, #0
	str	r7, [r5, #1772]
	str	r6, [r4, #28]
	strh	r3, [r4]	@ movhi
	strh	r3, [r4, #2]	@ movhi
	mov	r3, #1
	str	r3, [r4, #36]
.L2713:
	uxth	r2, r6
	ldr	r1, [sp]
	sxth	r3, r2
	cmp	r3, r1
	bge	.L2731
	ldr	r1, [sp]
	sub	r1, r1, #1
	cmp	r3, r1
	mov	r1, r3, asl #1
	bne	.L2714
	ldrh	r0, [r9, r1]
	mov	r6, #0
	str	r3, [sp, #12]
	add	r3, r9, r1
	mov	r1, #1
	str	r2, [sp, #8]
	str	r3, [sp]
	bl	FtlGetLastWrittenPage
	ldr	fp, .L2755
	ldr	r2, [sp, #8]
	add	r1, r0, #1
	ldr	r3, [sp, #12]
	sxth	r0, r0
	strh	r1, [r4, #2]	@ movhi
	add	r9, r0, #1
	strh	r2, [r4]	@ movhi
	ldr	r2, [sp, #4]
	ldr	r3, [r2, r3, asl #2]
	str	r3, [r4, #28]
.L2715:
	sxth	r3, r6
	cmp	r3, r9
	bge	.L2731
	ldr	r2, [sp]
	mov	r1, #1
	ldr	r0, .L2755+8
	ldrh	r2, [r2]
	orr	r3, r3, r2, asl #10
	mov	r2, r1
	str	r3, [r5, #1764]
	bl	FlashReadPages
	ldr	r3, .L2755+12
	ldrb	r3, [r3, #32]	@ zero_extendqisi2
	cmp	r3, #0
	beq	.L2716
	ldr	r3, [fp, #1772]
	ldr	r3, [r3, #12]
	cmp	r3, #0
	beq	.L2716
	ldr	r2, .L2755+16
	ldr	r0, [fp, #1768]
	str	r3, [sp, #4]
	ldrh	r1, [r2]
	bl	js_hash
	ldr	r3, [sp, #4]
	cmp	r3, r0
	mvnne	r3, #0
	strne	r3, [fp, #1760]
.L2716:
	ldr	r3, [fp, #1760]
	cmn	r3, #1
	beq	.L2717
	ldrh	r3, [r7, #8]
	cmp	r3, r10
	bcs	.L2717
	ldrh	r2, [r4, #4]
	ldrh	r1, [r7]
	cmp	r1, r2
	ldreq	r2, [fp, #1764]
	streq	r2, [r8, r3, asl #2]
.L2717:
	add	r6, r6, #1
	b	.L2715
.L2731:
	mov	r0, r4
	bl	ftl_free_no_use_map_blk
	ldr	r1, .L2755+12
	movw	r3, #2390
	ldrh	r2, [r4, #2]
	ldrh	r3, [r1, r3]
	cmp	r2, r3
	bne	.L2720
	mov	r0, r4
	bl	ftl_map_blk_alloc_new_blk
	b	.L2720
.L2714:
	ldr	r3, [r5, #-524]
	ldr	r0, .L2755+8
	str	r3, [r5, #1768]
	add	r3, r9, r1
	ldrh	r2, [r9, r1]
	mov	r1, #1
	str	r3, [sp, #8]
	ldrh	r3, [fp]
	sub	r3, r3, #1
	orr	r3, r3, r2, asl #10
	mov	r2, r1
	str	r3, [r5, #1764]
	bl	FlashReadPages
	ldr	r3, [r5, #1760]
	cmn	r3, #1
	beq	.L2733
	ldrh	r2, [r7]
	ldrh	r3, [r4, #4]
	cmp	r2, r3
	bne	.L2733
	ldrh	r2, [r7, #8]
	movw	r3, #64245
	cmp	r2, r3
	bne	.L2733
	mov	r1, #0
	mov	ip, #8
	mov	lr, #4
.L2722:
	ldr	r0, .L2755+4
	uxth	r3, r1
	sxth	r2, r3
	ldrh	r0, [r0]
	sub	r0, r0, #1
	cmp	r2, r0
	bge	.L2725
	ldr	r0, [r5, #-524]
	add	r1, r1, #1
	ldr	r2, [r0, r2, asl #3]
	uxth	r2, r2
	cmp	r2, r10
	smlabbcc	r3, r3, ip, lr
	ldrcc	r3, [r0, r3]
	strcc	r3, [r8, r2, asl #2]
	b	.L2722
.L2733:
	ldr	ip, .L2755
	mov	r3, #0
.L2753:
	ldrh	r1, [fp]
	sxth	r2, r3
	cmp	r2, r1
	bge	.L2725
	str	r3, [sp, #16]
	ldr	r3, [sp, #8]
	ldr	r0, .L2755+8
	str	ip, [sp, #12]
	ldrh	r1, [r3]
	orr	r2, r2, r1, asl #10
	mov	r1, #1
	str	r2, [ip, #1764]
	mov	r2, r1
	bl	FlashReadPages
	ldr	r3, .L2755+12
	ldrb	r2, [r3, #32]	@ zero_extendqisi2
	cmp	r2, #0
	ldr	ip, [sp, #12]
	ldr	r3, [sp, #16]
	beq	.L2726
	ldr	r2, [ip, #1772]
	ldr	r2, [r2, #12]
	cmp	r2, #0
	beq	.L2726
	ldr	r1, .L2755+16
	ldr	r0, [ip, #1768]
	str	r2, [sp, #20]
	ldrh	r1, [r1]
	bl	js_hash
	ldr	r2, [sp, #20]
	ldr	ip, [sp, #12]
	ldr	r3, [sp, #16]
	cmp	r2, r0
	mvnne	r2, #0
	strne	r2, [ip, #1760]
.L2726:
	ldr	r2, .L2755
	ldr	r2, [r2, #1760]
	cmn	r2, #1
	beq	.L2727
	ldrh	r2, [r7, #8]
	cmp	r2, r10
	bcs	.L2727
	ldrh	r1, [r4, #4]
	ldrh	r0, [r7]
	cmp	r0, r1
	ldreq	r1, [ip, #1764]
	streq	r1, [r8, r2, asl #2]
.L2727:
	add	r3, r3, #1
	b	.L2753
.L2725:
	add	r6, r6, #1
	b	.L2713
.L2720:
	mov	r0, r4
	bl	ftl_map_blk_gc
	mov	r0, r4
	bl	ftl_map_blk_gc
	mov	r0, #0
	add	sp, sp, #28
	@ sp needed
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L2756:
	.align	2
.L2755:
	.word	.LANCHOR2
	.word	.LANCHOR0+2390
	.word	.LANCHOR2+1760
	.word	.LANCHOR0
	.word	.LANCHOR0+2398
	.fnend
	.size	FtlMapTblRecovery, .-FtlMapTblRecovery
	.align	2
	.global	FtlLoadVonderInfo
	.type	FtlLoadVonderInfo, %function
FtlLoadVonderInfo:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	ldr	r2, .L2759
	movw	r1, #2408
	stmfd	sp!, {r3, lr}
	.save {r3, lr}
	ldr	r3, .L2759+4
	ldrh	r1, [r2, r1]
	add	r0, r3, #1792
	add	r0, r0, #8
	strh	r1, [r0, #10]	@ movhi
	ldr	r1, .L2759+8
	strh	r1, [r0, #4]	@ movhi
	movw	r1, #2434
	ldrh	r1, [r2, r1]
	strh	r1, [r0, #8]	@ movhi
	movw	r1, #2410
	ldrh	r1, [r2, r1]
	ldr	r2, [r2, #2436]
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
	ldmfd	sp!, {r3, pc}
.L2760:
	.align	2
.L2759:
	.word	.LANCHOR0
	.word	.LANCHOR2
	.word	-3962
	.fnend
	.size	FtlLoadVonderInfo, .-FtlLoadVonderInfo
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
	ldr	r0, .L2763
	bl	FtlMapTblRecovery
	mov	r0, #0
	ldmfd	sp!, {r3, pc}
.L2764:
	.align	2
.L2763:
	.word	.LANCHOR2-432
	.fnend
	.size	FtlLoadMapInfo, .-FtlLoadMapInfo
	.align	2
	.global	FtlVendorPartWrite
	.type	FtlVendorPartWrite, %function
FtlVendorPartWrite:
	.fnstart
	@ args = 0, pretend = 0, frame = 56
	@ frame_needed = 0, uses_anonymous_args = 0
	ldr	r3, .L2776
	stmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.save {r4, r5, r6, r7, r8, r9, r10, fp, lr}
	mov	r10, r2
	movw	r2, #2396
	mov	r6, r0
	ldrh	r5, [r3, r2]
	add	r3, r3, #2384
	add	r2, r0, r1
	.pad #60
	sub	sp, sp, #60
	ldrh	r3, [r3]
	mov	r4, r1
	cmp	r2, r3
	mvnhi	r0, #0
	bhi	.L2766
	ldr	fp, .L2776+4
	mov	r5, r6, lsr r5
	mov	r3, r5, asl #2
	str	r3, [sp, #4]
	mov	r7, fp
	mov	r3, #0
	str	r3, [sp]
.L2767:
	cmp	r4, #0
	beq	.L2775
	ldr	r2, [sp, #4]
	mov	r0, r6
	ldr	r3, [fp, #-456]
	ldr	ip, [r3, r2]
	ldr	r3, .L2776+8
	str	ip, [sp, #12]
	ldrh	r2, [r3]
	mov	r1, r2
	str	r2, [sp, #8]
	bl	__aeabi_uidivmod
	ldr	r2, [sp, #8]
	mov	r8, r1
	ldr	ip, [sp, #12]
	rsb	r3, r1, r2
	uxth	r9, r3
	cmp	r9, r4
	uxthhi	r9, r4
	cmp	r9, r2
	cmpne	ip, #0
	movne	r1, #1
	moveq	r1, #0
	beq	.L2769
	ldr	r2, [r7, #-516]
	mov	r1, #1
	add	r0, sp, #20
	str	ip, [sp, #24]
	str	r2, [sp, #28]
	mov	r2, #0
	str	r2, [sp, #32]
	mov	r2, r1
	bl	FlashReadPages
	b	.L2770
.L2769:
	ldr	r2, .L2776+12
	ldr	r0, [r7, #-516]
	ldrh	r2, [r2]
	bl	ftl_memset
.L2770:
	mov	ip, r9, asl #9
	ldr	r0, [r7, #-516]
	uxth	r8, r8
	mov	r1, r10
	mov	r2, ip
	str	ip, [sp, #8]
	add	r0, r0, r8, asl #9
	rsb	r4, r9, r4
	bl	ftl_memcpy
	mov	r1, r5
	ldr	r0, .L2776+16
	add	r5, r5, #1
	ldr	r2, [r7, #-516]
	add	r6, r6, r9
	bl	FtlMapWritePage
	ldr	r3, [sp]
	cmn	r0, #1
	ldr	ip, [sp, #8]
	mvneq	r3, #0
	str	r3, [sp]
	add	r10, r10, ip
	ldr	r3, [sp, #4]
	add	r3, r3, #4
	str	r3, [sp, #4]
	b	.L2767
.L2775:
	ldr	r0, [sp]
.L2766:
	add	sp, sp, #60
	@ sp needed
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L2777:
	.align	2
.L2776:
	.word	.LANCHOR0
	.word	.LANCHOR2
	.word	.LANCHOR0+2394
	.word	.LANCHOR0+2398
	.word	.LANCHOR2+1800
	.fnend
	.size	FtlVendorPartWrite, .-FtlVendorPartWrite
	.align	2
	.global	Ftl_save_ext_data
	.type	Ftl_save_ext_data, %function
Ftl_save_ext_data:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	@ link register save eliminated.
	ldr	r2, .L2780
	ldr	r3, .L2780+4
	ldr	r1, [r2, #-3232]
	cmp	r1, r3
	bxne	lr
	ldr	r3, .L2780+8
	mov	r0, #0
	mov	r1, #1
	sub	r2, r2, #3232
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
	ldr	r3, [r2, #-372]
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
	ldr	r3, [r2, #2668]
	str	r3, [r2, #64]
	b	FtlVendorPartWrite
.L2781:
	.align	2
.L2780:
	.word	.LANCHOR2
	.word	1179929683
	.word	1342177379
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
	ldr	r3, .L2790
	ldr	r3, [r3, #2244]
	cmp	r3, #0
	ldr	r3, .L2790+4
	moveq	r2, #32
	beq	.L2783
	ldr	r2, [r3, #-3308]
	cmp	r2, #39
	movhi	r2, #32
	movls	r2, #4
.L2783:
	movw	ip, #1844
	ldrh	r1, [r3, ip]
	cmp	r1, #31
	addls	r1, r1, #1
	ldrls	r2, .L2790+4
	strlsh	r1, [r2, ip]	@ movhi
	movls	r2, #1
	cmp	r0, #0
	bne	.L2785
	ldr	r1, [r3, #-480]
	ldr	r0, [r1, #20]
	ldr	r1, [r1, #16]
	add	r2, r2, r0
	cmp	r1, r2
	bcc	.L2786
.L2785:
	ldr	r2, [r3, #-480]
	mov	r0, #64
	ldr	r1, [r2, #16]
	str	r1, [r2, #20]
	ldr	r1, .L2790+8
	str	r1, [r2]
	ldr	r2, [r3, #-480]
	ldr	r3, .L2790+12
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
.L2786:
	mov	r0, #0
	ldmfd	sp!, {r3, pc}
.L2791:
	.align	2
.L2790:
	.word	.LANCHOR0
	.word	.LANCHOR2
	.word	1112818501
	.word	.LANCHOR2-488
	.fnend
	.size	FtlEctTblFlush, .-FtlEctTblFlush
	.align	2
	.global	FtlVendorPartRead
	.type	FtlVendorPartRead, %function
FtlVendorPartRead:
	.fnstart
	@ args = 0, pretend = 0, frame = 56
	@ frame_needed = 0, uses_anonymous_args = 0
	stmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.save {r4, r5, r6, r7, r8, r9, r10, fp, lr}
	movw	r3, #2396
	ldr	r4, .L2804
	mov	r8, r2
	add	r2, r0, r1
	mov	r7, r0
	.pad #60
	sub	sp, sp, #60
	mov	r6, r1
	ldrh	r5, [r4, r3]
	add	r4, r4, #2384
	ldrh	r3, [r4]
	cmp	r2, r3
	mvnhi	r0, #0
	bhi	.L2793
	ldr	r9, .L2804+4
	mov	r5, r7, lsr r5
	mov	r3, r5, asl #2
	str	r3, [sp, #4]
	mov	r10, r9
	mov	r3, #0
	str	r3, [sp]
.L2794:
	cmp	r6, #0
	beq	.L2803
	ldr	r2, [sp, #4]
	mov	r0, r7
	ldr	r3, [r9, #-456]
	ldr	r3, [r3, r2]
	str	r3, [sp, #12]
	ldr	r3, .L2804+8
	ldrh	r4, [r3]
	mov	r1, r4
	bl	__aeabi_uidivmod
	rsb	r4, r1, r4
	ldr	r3, [sp, #12]
	str	r1, [sp, #8]
	uxth	r4, r4
	cmp	r4, r6
	uxthhi	r4, r6
	cmp	r3, #0
	mov	fp, r4, asl #9
	beq	.L2796
	ldr	r2, [r10, #-516]
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
	ldr	r2, [r10, #1760]
	mvneq	r3, #0
	cmp	r2, #256
	str	r3, [sp]
	ldr	r3, [sp, #12]
	bne	.L2798
	mov	r1, r5
	mov	r2, r3
	ldr	r0, .L2804+12
	bl	printk
	ldr	r0, .L2804+16
	mov	r1, r5
	ldr	r2, [r9, #-516]
	bl	FtlMapWritePage
.L2798:
	ldrh	r3, [sp, #8]
	mov	r0, r8
	ldr	r1, [r10, #-516]
	mov	r2, fp
	add	r1, r1, r3, asl #9
	bl	ftl_memcpy
	b	.L2799
.L2796:
	mov	r0, r8
	mov	r1, r3
	mov	r2, fp
	bl	ftl_memset
.L2799:
	ldr	r3, [sp, #4]
	add	r5, r5, #1
	rsb	r6, r4, r6
	add	r7, r7, r4
	add	r3, r3, #4
	add	r8, r8, fp
	str	r3, [sp, #4]
	b	.L2794
.L2803:
	ldr	r0, [sp]
.L2793:
	add	sp, sp, #60
	@ sp needed
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L2805:
	.align	2
.L2804:
	.word	.LANCHOR0
	.word	.LANCHOR2
	.word	.LANCHOR0+2394
	.word	.LC136
	.word	.LANCHOR2+1800
	.fnend
	.size	FtlVendorPartRead, .-FtlVendorPartRead
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
	ldr	r4, .L2809
	sub	r5, r4, #488
	ldr	r2, [r4, #-480]
	ldrh	r1, [r5]
	bl	FtlVendorPartRead
	ldr	r3, [r4, #-480]
	ldr	r2, [r3]
	ldr	r3, .L2809+4
	cmp	r2, r3
	beq	.L2807
	ldr	r1, .L2809+8
	ldr	r0, .L2809+12
	bl	printk
	ldrh	r2, [r5]
	ldr	r0, [r4, #-480]
	mov	r1, #0
	mov	r2, r2, asl #9
	bl	ftl_memset
.L2807:
	mov	r0, #0
	ldmfd	sp!, {r3, r4, r5, pc}
.L2810:
	.align	2
.L2809:
	.word	.LANCHOR2
	.word	1112818501
	.word	.LC137
	.word	.LC76
	.fnend
	.size	FtlLoadEctTbl, .-FtlLoadEctTbl
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
	ldr	r4, .L2817
	mov	r1, #1
	ldr	r5, .L2817+4
	sub	r2, r4, #3232
	bl	FtlVendorPartRead
	ldr	r3, [r4, #-3232]
	cmp	r3, r5
	beq	.L2812
	sub	r0, r4, #3232
	mov	r1, #0
	mov	r2, #512
	bl	ftl_memset
	str	r5, [r4, #-3232]
.L2812:
	ldr	r2, [r4, #-3232]
	ldr	r3, .L2817
	cmp	r2, r5
	bne	.L2813
	ldr	r2, [r3, #-3144]
	str	r2, [r3, #-3340]
	ldr	r2, [r3, #-3140]
	str	r2, [r3, #-3336]
	ldr	r2, [r3, #-3224]
	str	r2, [r3, #-3344]
	ldr	r2, [r3, #-3220]
	str	r2, [r3, #-3356]
	ldr	r2, [r3, #-3216]
	str	r2, [r3, #-3364]
	ldr	r2, [r3, #-3212]
	str	r2, [r3, #-3348]
	ldr	r2, [r3, #-3204]
	str	r2, [r3, #-3320]
	ldr	r2, [r3, #-3200]
	str	r2, [r3, #-3604]
	ldr	r2, [r3, #-3196]
	str	r2, [r3, #-3360]
	ldr	r2, [r3, #-3192]
	str	r2, [r3, #-3352]
	ldr	r2, [r3, #-3188]
	str	r2, [r3, #-3312]
	ldr	r2, [r3, #-3184]
	str	r2, [r3, #-3308]
	ldr	r2, [r3, #-3172]
	str	r2, [r3, #-3236]
.L2813:
	ldr	r1, [r4, #-3164]
	mov	r3, #0
	ldr	r2, .L2817+8
	str	r3, [r4, #-564]
	cmp	r1, r2
	ldr	r5, .L2817+12
	bne	.L2814
	ldrb	r2, [r5, #148]	@ zero_extendqisi2
	cmp	r2, r3
	beq	.L2815
	ldr	r2, .L2817
	str	r3, [r2, #-3164]
	bl	Ftl_save_ext_data
	b	.L2814
.L2815:
	ldr	r0, .L2817+16
	mov	r3, #1
	ldr	r1, .L2817+20
	str	r3, [r5, #2244]
	bl	printk
.L2814:
	movw	r3, #2380
	ldr	r0, [r4, #-3324]
	ldrh	r2, [r5, r3]
	ldr	r3, [r4, #-3320]
	mla	r0, r0, r2, r3
	movw	r3, #2328
	ldrh	r1, [r5, r3]
	bl	__aeabi_uidiv
	str	r0, [r4, #-3316]
	ldmfd	sp!, {r3, r4, r5, pc}
.L2818:
	.align	2
.L2817:
	.word	.LANCHOR2
	.word	1179929683
	.word	305432421
	.word	.LANCHOR0
	.word	.LC76
	.word	.LC138
	.fnend
	.size	Ftl_load_ext_data, .-Ftl_load_ext_data
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
	ldr	r4, .L2826
	mov	r2, #0
	str	r2, [r0, #36]
	ldr	r2, [r4, #-3616]
	cmp	r2, #0
	ldmnefd	sp!, {r4, r5, r6, pc}
	sub	r5, r5, #1
	mov	r6, r0
	ldr	r2, [r4, #-500]
	ldr	r0, [r4, #-520]
	uxth	r5, r5
	str	r2, [r4, #1772]
	str	r0, [r4, #1768]
	ldr	r3, [r3, r5, asl #2]
	cmp	r3, #0
	str	r3, [r4, #1764]
	beq	.L2823
	mov	r1, #1
	add	r0, r4, #1760
	mov	r2, r1
	bl	FlashReadPages
	b	.L2824
.L2823:
	ldr	r2, .L2826+4
	movw	r3, #2398
	mov	r1, #255
	ldrh	r2, [r2, r3]
	bl	ftl_memset
.L2824:
	mov	r0, r6
	mov	r1, r5
	ldr	r2, [r4, #1768]
	ldmfd	sp!, {r4, r5, r6, lr}
	b	FtlMapWritePage
.L2827:
	.align	2
.L2826:
	.word	.LANCHOR2
	.word	.LANCHOR0
	.fnend
	.size	FtlMapBlkWriteDumpData, .-FtlMapBlkWriteDumpData
	.align	2
	.global	FlashReadFacBbtData
	.type	FlashReadFacBbtData, %function
FlashReadFacBbtData:
	.fnstart
	@ args = 0, pretend = 0, frame = 40
	@ frame_needed = 0, uses_anonymous_args = 0
	ldr	r3, .L2843
	stmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, lr}
	.save {r4, r5, r6, r7, r8, r9, r10, lr}
	mov	r8, r2
	ldrh	r2, [r3, #132]
	.pad #40
	sub	sp, sp, #40
	ldrh	r3, [r3, #134]
	mov	r5, r0
	ldr	r9, .L2843+4
	mov	r7, r1
	smulbb	r3, r2, r3
	ldr	r2, [r9, #1696]
	str	r2, [sp, #12]
	ldr	r2, [r9, #1728]
	uxth	r3, r3
	sub	r6, r3, #1
	sub	r4, r3, #16
	mul	r10, r3, r1
	uxth	r6, r6
	str	r2, [sp, #16]
.L2829:
	cmp	r6, r4
	ble	.L2842
	mov	r1, #1
	add	r3, r6, r10
	add	r0, sp, #4
	mov	r2, r1
	mov	r3, r3, asl #10
	str	r3, [sp, #8]
	bl	FlashReadPages
	ldr	r3, [sp, #4]
	cmn	r3, #1
	beq	.L2830
	ldr	r3, [r9, #1728]
	ldrh	r2, [r3]
	movw	r3, #61664
	cmp	r2, r3
	bne	.L2830
	cmp	r5, #0
	moveq	r0, r5
	beq	.L2831
	cmp	r7, #0
	ldreq	ip, .L2843+4
	moveq	lr, #1
	beq	.L2832
.L2834:
	ldr	r1, [r9, #1696]
	mov	r2, r8
	mov	r0, r5
	bl	ftl_memcpy
	mov	r2, #4
	ldr	r0, .L2843+8
	mov	r1, r5
	mov	r3, r2
	bl	rknand_print_hex
	mov	r0, #0
	b	.L2831
.L2832:
	ldr	r2, [r9, #1716]
	uxth	r3, r7
	add	r7, r7, #1
	cmp	r3, r2
	bcs	.L2834
	ldr	r1, [ip, #1696]
	mov	r0, r3, lsr #5
	and	r4, r3, #31
	ldr	r2, [r1, r0, asl #2]
	orr	r4, r2, lr, asl r4
	str	r4, [r1, r0, asl #2]
	b	.L2832
.L2830:
	sub	r6, r6, #1
	uxth	r6, r6
	b	.L2829
.L2842:
	mvn	r0, #0
.L2831:
	add	sp, sp, #40
	@ sp needed
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, pc}
.L2844:
	.align	2
.L2843:
	.word	.LANCHOR0
	.word	.LANCHOR2
	.word	.LC139
	.fnend
	.size	FlashReadFacBbtData, .-FlashReadFacBbtData
	.align	2
	.global	FlashGetBadBlockList
	.type	FlashGetBadBlockList, %function
FlashGetBadBlockList:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	ldr	r3, .L2856
	stmfd	sp!, {r4, r5, r6, r7, r8, lr}
	.save {r4, r5, r6, r7, r8, lr}
	mov	r5, r0
	ldr	r3, [r3, #44]
	ldr	r6, .L2856+4
	ldrb	r4, [r3, #13]	@ zero_extendqisi2
	ldrh	r3, [r3, #14]
	ldr	r0, [r6, #1724]
	smulbb	r4, r4, r3
	uxth	r4, r4
	add	r2, r4, #7
	mov	r2, r2, asr #3
	bl	FlashReadFacBbtData
	cmn	r0, #1
	bne	.L2846
.L2850:
	mov	r3, #0
	b	.L2847
.L2846:
	mov	lr, r4, lsr #4
	mov	ip, #0
	sub	r4, r4, #1
	mov	r3, ip
	mov	r7, #1
.L2848:
	uxth	r0, ip
	cmp	r0, r4
	bge	.L2847
	ldr	r8, [r6, #1724]
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
	bcc	.L2848
	b	.L2850
.L2847:
	mov	r3, r3, asl #1
	mvn	r2, #0
	mov	r0, #0
	strh	r2, [r5, r3]	@ movhi
	ldmfd	sp!, {r4, r5, r6, r7, r8, pc}
.L2857:
	.align	2
.L2856:
	.word	.LANCHOR0
	.word	.LANCHOR2
	.fnend
	.size	FlashGetBadBlockList, .-FlashGetBadBlockList
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
	ldr	r4, .L2882
	ldr	r5, [r4, #-3616]
	cmp	r5, #0
	bne	.L2859
	bl	FtlBbtMemInit
	ldr	r7, .L2882+4
	bl	FtlLoadFactoryBbt
	sub	r9, r7, #18
	sub	r10, r7, #94
.L2860:
	ldr	r3, .L2882+8
	ldr	r6, .L2882+12
	ldrh	r3, [r3]
	cmp	r5, r3
	bcs	.L2879
	ldrh	r3, [r9, #2]!
	movw	r2, #65535
	ldr	r0, [r4, #-524]
	ldr	fp, [r4, #-500]
	cmp	r3, r2
	ldr	r8, .L2882
	str	r0, [r4, #1768]
	str	fp, [r4, #1772]
	beq	.L2861
	ldrh	ip, [r10]
	mov	r1, #1
	mov	r2, r1
	add	r0, r8, #1760
	mla	ip, ip, r5, r3
	mov	r3, ip, asl #10
	str	ip, [sp, #4]
	str	r3, [r8, #1764]
	bl	FlashReadPages
	ldrh	r2, [r10]
	ldr	r0, [r7]
	add	r2, r2, #7
	ldr	r1, [r8, #1768]
	mov	r2, r2, asr #3
	bl	ftl_memcpy
	ldr	ip, [sp, #4]
	b	.L2862
.L2861:
	mov	r1, r5
	sub	r8, r8, #436
	bl	FlashGetBadBlockList
	ldr	r0, [r8, #2204]
	ldr	r1, [r7]
	bl	FtlBbt2Bitmap
	ldrh	r6, [r10]
	sub	r6, r6, #1
	uxth	r6, r6
.L2863:
	ldrh	r0, [r10]
	smlabb	r0, r0, r5, r6
	uxth	r0, r0
	bl	FtlBbmIsBadBlock
	cmp	r0, #1
	subeq	r6, r6, #1
	uxtheq	r6, r6
	beq	.L2863
.L2880:
	mov	r1, #0
	mov	r2, #16
	strh	r6, [r9]	@ movhi
	ldr	r0, [r4, #-500]
	bl	ftl_memset
	ldr	r3, .L2882+16
	strh	r3, [fp]	@ movhi
	mov	r3, #0
	str	r3, [fp, #4]
	ldrh	r3, [r9]
	ldrh	ip, [r10]
	ldrh	r2, [r8]
	strh	r3, [fp, #2]	@ movhi
	ldrh	r3, [r9]
	ldr	r1, [r7]
	mov	r2, r2, asl #2
	ldr	r0, [r4, #1768]
	mla	ip, ip, r5, r3
	mov	r3, ip, asl #10
	str	r3, [r4, #1764]
	str	ip, [sp, #4]
	bl	ftl_memcpy
	mov	r1, #1
	ldr	r0, .L2882+20
	mov	r2, r1
	bl	FlashEraseBlocks
	mov	r1, #1
	mov	r3, r1
	ldr	r0, .L2882+20
	mov	r2, r1
	bl	FlashProgPages
	ldr	r3, [r4, #1760]
	cmn	r3, #1
	ldr	ip, [sp, #4]
	bne	.L2862
	uxth	r0, ip
	bl	FtlBbmMapBadBlock
	b	.L2863
.L2862:
	uxth	r0, ip
	add	r5, r5, #1
	bl	FtlBbmMapBadBlock
	add	r7, r7, #4
	b	.L2860
.L2879:
	ldr	r7, .L2882+24
	mov	r5, #0
.L2867:
	ldrh	r3, [r7]
	uxth	r0, r5
	add	r5, r5, #1
	cmp	r3, r0
	bls	.L2881
	bl	FtlBbmMapBadBlock
	b	.L2867
.L2881:
	ldr	r7, .L2882+28
	movw	r9, #65535
	ldrh	r5, [r7, #12]
	mov	r8, r7
	sub	r5, r5, #1
	uxth	r5, r5
.L2869:
	ldrh	r3, [r7, #12]
	sub	r3, r3, #48
	cmp	r5, r3
	ble	.L2873
	mov	r0, r5
	bl	FtlBbmIsBadBlock
	cmp	r0, #1
	beq	.L2870
	mov	r0, r5
	bl	FlashTestBlk
	cmp	r0, #0
	beq	.L2871
	mov	r0, r5
	bl	FtlBbmMapBadBlock
	b	.L2870
.L2871:
	ldrh	r3, [r8]
	cmp	r3, r9
	streqh	r5, [r8]	@ movhi
.L2872:
	ldrne	r3, .L2882+28
	strneh	r5, [r3, #4]	@ movhi
	bne	.L2873
.L2870:
	sub	r5, r5, #1
	uxth	r5, r5
	b	.L2869
.L2873:
	movw	r5, #2452
	ldr	r0, [r4, #-3612]
	ldrh	r3, [r6, r5]
	mov	r8, #0
	str	r8, [r6, #2460]
	mov	r2, #2
	mov	r1, #1
	strh	r8, [r7, #2]	@ movhi
	mov	r3, r3, asl #10
	str	r3, [r0, #4]
	ldrh	r3, [r7, #4]
	mov	r3, r3, asl #10
	str	r3, [r0, #40]
	bl	FlashEraseBlocks
	ldrh	r0, [r6, r5]
	bl	FtlBbmMapBadBlock
	ldrh	r0, [r7, #4]
	bl	FtlBbmMapBadBlock
	bl	FtlBbmTblFlush
	ldr	r3, [r6, #2460]
	ldrh	r2, [r7, #4]
	add	r3, r3, #1
	str	r3, [r6, #2460]
	ldrh	r3, [r6, r5]
	strh	r8, [r7, #2]	@ movhi
	strh	r2, [r6, r5]	@ movhi
	strh	r3, [r7, #4]	@ movhi
	bl	FtlBbmTblFlush
.L2859:
	mov	r0, #0
	add	sp, sp, #12
	@ sp needed
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L2883:
	.align	2
.L2882:
	.word	.LANCHOR2
	.word	.LANCHOR0+2480
	.word	.LANCHOR0+2342
	.word	.LANCHOR0
	.word	-3872
	.word	.LANCHOR2+1760
	.word	.LANCHOR0+2402
	.word	.LANCHOR0+2452
	.fnend
	.size	FtlMakeBbt, .-FtlMakeBbt
	.align	2
	.global	log2phys
	.type	log2phys, %function
log2phys:
	.fnstart
	@ args = 0, pretend = 0, frame = 8
	@ frame_needed = 0, uses_anonymous_args = 0
	stmfd	sp!, {r0, r1, r2, r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.save {r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.pad #12
	movw	r3, #2396
	ldr	r7, .L2901
	ldrh	r10, [r7, r3]
	ldr	r3, [r7, #2448]
	cmp	r0, r3
	bcs	.L2885
	add	r10, r10, #7
	ldr	fp, .L2901+4
	mov	r5, r0, lsr r10
	movw	r3, #2426
	str	r2, [sp, #4]
	mov	r9, r1
	mov	r6, r7
	str	r0, [sp]
	uxth	r5, r5
	ldrh	r2, [r7, r3]
	ldr	r8, [fp, #-3380]
	mov	r4, #0
	mov	r1, #12
	b	.L2886
.L2885:
	cmp	r2, #0
	mvn	r0, #0
	streq	r0, [r1]
	b	.L2887
.L2891:
	add	r4, r4, #1
	mla	r0, r1, r4, r8
	ldrh	r0, [r0, #-12]
	cmp	r0, r5
	beq	.L2888
.L2886:
	uxth	r7, r4
	cmp	r7, r2
	bcc	.L2891
	bl	select_l2p_ram_region
	mov	r4, #12
	movw	r3, #65535
	mul	r4, r4, r0
	mov	r7, r0
	add	r2, r8, r4
	ldrh	r1, [r8, r4]
	cmp	r1, r3
	bne	.L2900
.L2892:
	ldr	r3, [fp, #-452]
	ldr	r8, .L2901+4
	ldr	fp, [r3, r5, asl #2]
	cmp	fp, #0
	bne	.L2893
	ldr	r3, [r8, #-3380]
	mov	r1, #255
	add	r3, r3, r4
	ldr	r0, [r3, #8]
	movw	r3, #2398
	ldrh	r2, [r6, r3]
	bl	ftl_memset
	ldr	r3, [r8, #-3380]
	strh	r5, [r3, r4]	@ movhi
	ldr	r3, [r8, #-3380]
	add	r4, r3, r4
	str	fp, [r4, #4]
.L2888:
	ldr	r2, [sp]
	mvn	r3, #0
	mov	r6, #12
	bic	r10, r2, r3, asl r10
	ldr	r3, [sp, #4]
	ldr	r2, .L2901+4
	cmp	r3, #0
	uxth	r10, r10
	bne	.L2889
	ldr	r3, [r2, #-3380]
	mla	r6, r6, r7, r3
	ldr	r3, [r6, #8]
	ldr	r3, [r3, r10, asl #2]
	str	r3, [r9]
	b	.L2890
.L2889:
	mul	r6, r6, r7
	ldr	r3, [r2, #-3380]
	ldr	r1, [r9]
	add	r3, r3, r6
	ldr	r3, [r3, #8]
	str	r1, [r3, r10, asl #2]
	ldr	r3, [r2, #-3380]
	add	r6, r3, r6
	ldr	r3, [r6, #4]
	orr	r3, r3, #-2147483648
	str	r3, [r6, #4]
	sub	r3, r2, #3376
	strh	r5, [r3]	@ movhi
.L2890:
	ldr	r2, [r2, #-3380]
	mov	r3, #12
	mov	r0, #0
	mla	r7, r3, r7, r2
	ldr	r3, [r7, #4]
	cmn	r3, #1
	addne	r3, r3, #1
	strne	r3, [r7, #4]
	b	.L2887
.L2900:
	ldr	r3, [r2, #4]
	cmp	r3, #0
	bge	.L2892
	bl	flush_l2p_region
	b	.L2892
.L2893:
	ldr	r3, [r8, #-3380]
	mov	r1, #1
	add	r0, r8, #1760
	mov	r2, r1
	add	r3, r3, r4
	str	fp, [r8, #1764]
	ldr	r3, [r3, #8]
	str	r3, [r8, #1768]
	ldr	r3, [r8, #-500]
	str	r3, [r8, #1772]
	bl	FlashReadPages
	ldr	r3, [r8, #1772]
	ldrh	r3, [r3, #8]
	cmp	r3, r5
	beq	.L2894
	mov	r1, r5
	mov	r2, fp
	ldr	r0, .L2901+8
	bl	printk
	mov	r2, #4
	mov	r3, r2
	ldr	r0, .L2901+12
	ldr	r1, [r8, #1772]
	bl	rknand_print_hex
	movw	r3, #2424
	ldrh	r3, [r6, r3]
	mov	r2, #4
	ldr	r0, .L2901+16
	ldr	r1, [r8, #-452]
	bl	rknand_print_hex
	mov	r3, #1
	str	r3, [r8, #-3616]
	b	.L2895
.L2894:
	ldr	r3, [r8, #1760]
	cmp	r3, #256
	bne	.L2895
	mov	r1, r5
	mov	r2, fp
	ldr	r0, .L2901+20
	bl	printk
	ldr	r3, [r8, #-3380]
	sub	r0, r8, #432
	mov	r1, r5
	add	r3, r3, r4
	ldr	r2, [r3, #8]
	bl	FtlMapWritePage
.L2895:
	ldr	r3, .L2901+4
	mov	r1, #0
	ldr	r3, [r3, #-3380]
	add	r2, r3, r4
	str	r1, [r2, #4]
	strh	r5, [r3, r4]	@ movhi
	b	.L2888
.L2887:
	add	sp, sp, #12
	@ sp needed
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L2902:
	.align	2
.L2901:
	.word	.LANCHOR0
	.word	.LANCHOR2
	.word	.LC140
	.word	.LC101
	.word	.LC141
	.word	.LC142
	.fnend
	.size	log2phys, .-log2phys
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
	ldr	r4, .L2923
	ldr	r3, [r4, #-3616]
	cmp	r3, #0
	bne	.L2903
	ldr	r3, .L2923+4
	ldrh	r2, [r3, #4]
	cmp	r2, #0
	beq	.L2905
	ldrb	r3, [r4, #-3516]	@ zero_extendqisi2
	cmp	r3, #0
	bne	.L2905
	ldr	r8, .L2923+8
	movw	r3, #2388
	ldrb	r1, [r4, #-3517]	@ zero_extendqisi2
	ldrh	r3, [r8, r3]
	mul	r3, r3, r1
	cmp	r2, r3
	beq	.L2905
	ldrb	r9, [r4, #-3514]	@ zero_extendqisi2
	add	r3, r8, #2320
	ldr	r6, [r8, #2448]
	cmp	r9, #0
	ldrh	r7, [r3]
	bne	.L2903
	sub	r6, r6, #1
	mov	r1, sp
	mov	r2, r9
	mov	r0, r6
	bl	log2phys
	ldr	r5, [r4, #-500]
	ldr	r0, [r4, #-524]
	ldr	r3, [sp]
	str	r6, [sp, #20]
	cmn	r3, #1
	str	r0, [sp, #12]
	str	r3, [sp, #8]
	str	r5, [sp, #16]
	str	r9, [r5, #4]
	beq	.L2907
	mov	r1, #1
	mov	r2, r9
	add	r0, sp, #4
	bl	FlashReadPages
	b	.L2908
.L2907:
	movw	r3, #2398
	mov	r1, #255
	ldrh	r2, [r8, r3]
	bl	ftl_memset
.L2908:
	ldr	fp, .L2923+4
	mov	r7, r7, asl #2
	ldr	r3, .L2923+12
	mov	r8, #0
	ldr	r9, .L2923
	mov	r10, fp
	strh	r3, [r5]	@ movhi
.L2909:
	cmp	r8, r7
	beq	.L2910
	ldrh	r3, [fp, #4]
	cmp	r3, #0
	beq	.L2910
	ldr	r3, [sp, #8]
	add	r8, r8, #1
	str	r6, [r5, #8]
	ldr	r0, .L2923+4
	str	r3, [r5, #12]
	ldrh	r3, [fp]
	strh	r3, [r5, #2]	@ movhi
	bl	get_new_active_ppa
	ldr	r3, [r9, #-3328]
	mov	r2, #0
	mov	r1, #1
	str	r0, [sp, #8]
	add	r0, sp, #4
	str	r3, [r5, #4]
	add	r3, r3, #1
	cmn	r3, #1
	moveq	r3, #0
	str	r3, [r9, #-3328]
	mov	r3, r2
	bl	FlashProgPages
	ldrh	r0, [r10]
	bl	decrement_vpc_count
	b	.L2909
.L2910:
	mov	r3, #1
	b	.L2922
.L2905:
	mov	r3, #0
.L2922:
	strb	r3, [r4, #-3514]
.L2903:
	add	sp, sp, #44
	@ sp needed
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L2924:
	.align	2
.L2923:
	.word	.LANCHOR2
	.word	.LANCHOR2-3524
	.word	.LANCHOR0
	.word	-3947
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
	ldr	r5, .L2931
	mov	r7, #12
	ldr	r6, .L2931+4
.L2926:
	ldrh	r3, [r5]
	uxth	r0, r4
	cmp	r3, r0
	bls	.L2930
	ldr	r3, [r6, #-3380]
	mla	r3, r7, r0, r3
	ldr	r3, [r3, #4]
	cmp	r3, #0
	bge	.L2927
	bl	flush_l2p_region
.L2927:
	add	r4, r4, #1
	b	.L2926
.L2930:
	mov	r0, #0
	ldmfd	sp!, {r3, r4, r5, r6, r7, pc}
.L2932:
	.align	2
.L2931:
	.word	.LANCHOR0+2426
	.word	.LANCHOR2
	.fnend
	.size	l2p_flush, .-l2p_flush
	.align	2
	.global	allocate_new_data_superblock
	.type	allocate_new_data_superblock, %function
allocate_new_data_superblock:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	stmfd	sp!, {r4, r5, r6, lr}
	.save {r4, r5, r6, lr}
	ldr	r5, .L2960
	ldrh	r6, [r0]
	ldr	r3, [r5, #-3616]
	cmp	r3, #0
	bne	.L2934
	movw	r3, #65535
	cmp	r6, r3
	mov	r4, r0
	beq	.L2935
	ldr	r2, [r5, #-3544]
	mov	r3, r6, asl #1
	mov	r0, r6
	ldrh	r3, [r2, r3]
	cmp	r3, #0
	beq	.L2936
	bl	INSERT_DATA_LIST
	b	.L2935
.L2936:
	bl	INSERT_FREE_LIST
.L2935:
	mov	r3, #0
	strb	r3, [r4, #8]
	ldr	r3, .L2960+4
	cmp	r4, r3
	beq	.L2937
	ldr	r3, .L2960+8
	movw	r2, #2340
	ldrh	r2, [r3, r2]
	cmp	r2, #1
	beq	.L2937
	ldrb	r1, [r3, #148]	@ zero_extendqisi2
	cmp	r1, #0
	beq	.L2938
.L2937:
	mov	r3, #1
	strb	r3, [r4, #8]
	b	.L2939
.L2938:
	ldr	r1, .L2960+12
	cmp	r4, r1
	bne	.L2939
	cmp	r2, #3
	beq	.L2941
	ldr	r2, .L2960
	ldr	r2, [r2, #-3236]
	cmp	r2, #1
	bne	.L2942
.L2941:
	mov	r2, #1
	strb	r2, [r5, #-3516]
.L2942:
	ldr	r3, [r3, #2244]
	cmp	r3, #0
	beq	.L2939
	ldr	r3, [r5, #-3308]
	cmp	r3, #39
	ldrls	r3, .L2960
	movls	r2, #1
	strlsb	r2, [r3, #-3516]
.L2939:
	ldr	r2, .L2960
	movw	r1, #65535
	sub	r3, r2, #380
	ldrh	r0, [r3]
	mov	r5, r3
	cmp	r0, r1
	beq	.L2944
	cmp	r6, r0
	bne	.L2945
	ldr	r2, [r2, #-3544]
	mov	r3, r0, asl #1
	ldrh	r3, [r2, r3]
	cmp	r3, #0
	beq	.L2946
.L2945:
	bl	update_vpc_list
.L2946:
	mvn	r3, #0
	strh	r3, [r5]	@ movhi
.L2944:
	mov	r0, r4
	bl	allocate_data_superblock
	bl	l2p_flush
	mov	r0, #0
	bl	FtlEctTblFlush
	bl	FtlVpcTblFlush
.L2934:
	mov	r0, #0
	ldmfd	sp!, {r4, r5, r6, pc}
.L2961:
	.align	2
.L2960:
	.word	.LANCHOR2
	.word	.LANCHOR2-3476
	.word	.LANCHOR0
	.word	.LANCHOR2-3524
	.fnend
	.size	allocate_new_data_superblock, .-allocate_new_data_superblock
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
	ldr	r1, .L2985
	ldr	r0, .L2985+4
	bl	printk
	ldr	r0, .L2985+8
	mov	r1, #0
	mov	r2, #8192
	bl	memset
	ldr	r6, .L2985+12
	ldr	r5, .L2985+8
.L2963:
	ldr	r3, [r6, #2448]
	cmp	r4, r3
	bcs	.L2983
	mov	r0, r4
	add	r1, sp, #4
	mov	r2, #0
	bl	log2phys
	ldr	r0, [sp, #4]
	cmn	r0, #1
	beq	.L2964
	ubfx	r0, r0, #10, #16
	bl	P2V_block_in_plane
	mov	r0, r0, asl #1
	ldrh	r3, [r5, r0]
	add	r3, r3, #1
	strh	r3, [r5, r0]	@ movhi
.L2964:
	add	r4, r4, #1
	b	.L2963
.L2983:
	ldr	r9, .L2985+16
	mov	r4, #0
	ldr	r7, .L2985+20
	mov	r6, r4
	ldr	r8, .L2985+8
.L2966:
	ldrh	r3, [r9]
	uxth	r1, r4
	ldr	r5, .L2985+20
	cmp	r3, r1
	bls	.L2984
	ldr	r3, [r7, #-3544]
	mov	r5, r1, asl #1
	ldrh	r2, [r3, r5]
	ldrh	r3, [r8, r5]
	cmp	r2, r3
	beq	.L2967
	ldr	r0, .L2985+24
	bl	printk
	ldr	r3, [r7, #-3544]
	movw	r2, #65535
	ldrh	r3, [r3, r5]
	cmp	r3, r2
	beq	.L2967
	ldrh	r2, [r8, r5]
	cmp	r2, r3
	movhi	r6, #1
.L2967:
	add	r4, r4, #1
	b	.L2966
.L2984:
	ldr	r3, [r5, #-3532]
	cmp	r3, #0
	beq	.L2969
	sub	r2, r5, #3520
	ldr	r9, .L2985+8
	mov	r8, #0
	ldrh	r7, [r2, #-8]
	ldr	r2, [r5, #-3552]
	rsb	r3, r2, r3
	ldr	r2, .L2985+28
	mov	r3, r3, asr #1
	mul	r3, r2, r3
	uxth	r4, r3
.L2970:
	uxth	r3, r8
	cmp	r3, r7
	bcs	.L2969
	ldr	r2, [r5, #-3544]
	mov	r3, r4, asl #1
	ldrh	r2, [r2, r3]
	cmp	r2, #0
	beq	.L2971
	ldr	r0, .L2985+32
	mov	r1, r4
	ldrh	r3, [r9, r3]
	mov	r6, #1
	bl	printk
.L2971:
	mov	r3, #6
	ldr	r2, [r5, #-3552]
	mul	r4, r3, r4
	movw	r3, #65535
	add	r8, r8, #1
	ldrh	r4, [r2, r4]
	cmp	r4, r3
	bne	.L2970
.L2969:
	mov	r1, r6
	ldr	r0, .L2985+36
	bl	printk
	add	sp, sp, #12
	@ sp needed
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, pc}
.L2986:
	.align	2
.L2985:
	.word	.LANCHOR3+204
	.word	.LC110
	.word	.LANCHOR2+1848
	.word	.LANCHOR0
	.word	.LANCHOR0+2328
	.word	.LANCHOR2
	.word	.LC143
	.word	-1431655765
	.word	.LC144
	.word	.LC145
	.fnend
	.size	FtlCheckVpc, .-FtlCheckVpc
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
	ldr	r0, .L2997
	bl	printk
	ldr	r7, .L2997+4
	ldr	r5, .L2997+8
	mov	r4, #0
.L2988:
	ldr	r3, [r7, #2448]
	cmp	r4, r3
	bcs	.L2996
	mov	r0, r4
	add	r1, sp, #28
	mov	r2, #0
	bl	log2phys
	ubfx	r3, r4, #0, #11
	cmp	r3, #0
	bne	.L2989
	ldr	r0, .L2997+12
	mov	r1, r4
	ldr	r2, [sp, #28]
	bl	printk
.L2989:
	ldr	r3, [sp, #28]
	cmn	r3, #1
	beq	.L2991
	str	r3, [r5, #1764]
	mov	r2, #0
	ldr	r3, [r5, #-524]
	mov	r1, #1
	ldr	r6, [r5, #-500]
	ldr	r0, .L2997+16
	str	r3, [r5, #1768]
	str	r2, [r5, #1760]
	str	r4, [r5, #1776]
	str	r6, [r5, #1772]
	bl	FlashReadPages
	ldr	r3, [r5, #1760]
	ldr	r2, .L2997+8
	cmn	r3, #1
	cmpne	r3, #256
	beq	.L2992
	ldr	r3, [r6, #8]
	cmp	r3, r4
	beq	.L2991
.L2992:
	ldr	r3, [r2, #1772]
	ldr	r1, [r2, #1768]
	ldr	r0, [r3, #4]
	str	r0, [sp]
	ldr	r0, [r3, #8]
	str	r0, [sp, #4]
	ldr	r0, [r3, #12]
	str	r0, [sp, #8]
	ldr	r0, [r1]
	str	r0, [sp, #12]
	ldr	r1, [r1, #4]
	ldr	r0, .L2997+20
	str	r1, [sp, #16]
	mov	r1, r4
	ldr	r2, [r2, #1764]
	ldr	r3, [r3]
	bl	printk
.L2991:
	add	r4, r4, #1
	b	.L2988
.L2996:
	add	sp, sp, #36
	@ sp needed
	ldmfd	sp!, {r4, r5, r6, r7, pc}
.L2998:
	.align	2
.L2997:
	.word	.LC146
	.word	.LANCHOR0
	.word	.LANCHOR2
	.word	.LC147
	.word	.LANCHOR2+1760
	.word	.LC148
	.fnend
	.size	Ftlscanalldata, .-Ftlscanalldata
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
	ldr	r6, .L3009
	ldr	r4, [r6, #-3544]
	mov	r7, r0, asl #1
	ldrh	r3, [r4, r7]
	cmp	r3, #0
	addne	r3, r3, #1
	strneh	r3, [r4, r7]	@ movhi
	bne	.L3001
	ldr	r4, [r6, #-3532]
	cmp	r4, #0
	beq	.L3001
	ldr	r1, [r6, #-3552]
	sub	r2, r6, #3520
	ldr	lr, .L3009+4
	movw	r9, #65535
	rsb	r4, r1, r4
	ldrh	ip, [r2, #-8]
	mov	r8, r2
	mov	r4, r4, asr #1
	mul	r4, lr, r4
	mov	lr, #6
	uxth	r4, r4
.L3002:
	uxth	r2, r3
	cmp	r2, ip
	bcs	.L3001
	cmp	r4, r0
	bne	.L3003
	mov	r1, r4
	ldr	r0, .L3009+8
	bl	List_remove_node
	ldrh	r3, [r8, #-8]
	mov	r0, r4
	sub	r3, r3, #1
	strh	r3, [r8, #-8]	@ movhi
	bl	INSERT_DATA_LIST
	ldr	r2, [r6, #-3544]
	ldrh	r3, [r2, r7]
	add	r3, r3, #1
	strh	r3, [r2, r7]	@ movhi
	b	.L3001
.L3003:
	mul	r4, lr, r4
	add	r3, r3, #1
	ldrh	r4, [r1, r4]
	cmp	r4, r9
	bne	.L3002
.L3001:
	mov	r0, r5
	add	r1, sp, #4
	mov	r2, #1
	bl	log2phys
	add	sp, sp, #12
	@ sp needed
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, pc}
.L3010:
	.align	2
.L3009:
	.word	.LANCHOR2
	.word	-1431655765
	.word	.LANCHOR2-3532
	.fnend
	.size	FtlReUsePrevPpa, .-FtlReUsePrevPpa
	.align	2
	.global	FtlRecoverySuperblock
	.type	FtlRecoverySuperblock, %function
FtlRecoverySuperblock:
	.fnstart
	@ args = 0, pretend = 0, frame = 64
	@ frame_needed = 0, uses_anonymous_args = 0
	stmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.save {r4, r5, r6, r7, r8, r9, r10, fp, lr}
	movw	r2, #65535
	ldrh	r3, [r0]
	.pad #68
	sub	sp, sp, #68
	mov	r4, r0
	cmp	r3, r2
	beq	.L3152
	ldrh	r3, [r0, #2]
	ldr	r5, .L3169
	str	r3, [sp, #8]
	ldrb	r3, [r0, #6]	@ zero_extendqisi2
	ldr	r1, [sp, #8]
	str	r3, [sp, #16]
	movw	r3, #2388
	ldrh	r3, [r5, r3]
	cmp	r3, r1
	mov	r3, #0
	streqh	r3, [r0, #4]	@ movhi
	streqb	r3, [r0, #6]
	ldrneh	r0, [r0, #16]
	beq	.L3152
.L3015:
	cmp	r0, r2
	add	r3, r3, #1
	uxtheq	r1, r3
	addeq	r1, r4, r1, asl #1
	ldreqh	r0, [r1, #16]
	beq	.L3015
.L3164:
	ldrb	r1, [r4, #8]	@ zero_extendqisi2
	mov	r9, r4
	cmp	r1, #1
	bne	.L3017
	bl	FtlGetLastWrittenPage
	cmn	r0, #1
	mov	r4, r0
	beq	.L3018
	ldrb	r3, [r5, #148]	@ zero_extendqisi2
	cmp	r3, #0
	bne	.L3155
	ldr	r3, .L3169
	add	r3, r3, r0, asl #1
	ldrh	r6, [r3, #152]
	b	.L3089
.L3017:
	mov	r1, #0
	bl	FtlGetLastWrittenPage
	cmn	r0, #1
	mov	r4, r0
	beq	.L3018
.L3155:
	mov	r6, r4
.L3089:
	ldr	r3, .L3169+4
	movw	r8, #65535
	ldrh	r2, [r3], #80
	str	r2, [sp, #4]
	ldr	r2, .L3169+8
	ldrh	r7, [r3]
	add	r3, r9, #14
	str	r3, [sp, #20]
	ldr	ip, [r2, #-536]
	mov	r0, r3
	ldr	lr, [r2, #-2692]
	mov	r2, #0
	mov	r5, r2
	mov	r10, r2
	b	.L3020
.L3018:
	mov	r3, #0
	strh	r3, [r9, #2]	@ movhi
	strb	r3, [r9, #6]
	b	.L3152
.L3022:
	ldrh	r3, [r0, #2]!
	cmp	r3, r8
	beq	.L3021
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
.L3021:
	add	r2, r2, #1
.L3020:
	ldr	r1, [sp, #4]
	uxth	r3, r2
	cmp	r3, r1
	bcc	.L3022
	ldrb	r3, [r9, #8]	@ zero_extendqisi2
	cmp	r3, #1
	movne	r3, #0
	bne	.L3156
	ldr	r3, .L3169
	ldrb	lr, [r3, #148]	@ zero_extendqisi2
	adds	r3, lr, #0
	movne	r3, #1
.L3156:
	ldr	r7, .L3169+8
	mov	r1, r5
	str	r3, [sp, #24]
	mov	fp, #0
	ldr	r2, [sp, #24]
	movw	r10, #65535
	ldr	r0, [r7, #-536]
	bl	FlashReadPages
	ldr	r3, [r7, #-3328]
	str	r7, [sp, #12]
	str	r3, [sp, #28]
.L3024:
	uxth	r8, fp
	cmp	r8, r5
	bcs	.L3031
	mov	r1, #36
	ldr	r0, [r7, #-536]
	mul	r1, r1, fp
	add	ip, r0, r1
	ldr	r1, [r0, r1]
	cmp	r1, #0
	bne	.L3025
	ldr	ip, [ip, #12]
	ldr	r3, [ip, #4]
	cmn	r3, #1
	beq	.L3026
	mov	r0, r3
	ldr	r1, [r7, #-3328]
	str	ip, [sp, #32]
	str	r3, [sp, #4]
	bl	ftl_cmp_data_ver
	ldr	r3, [sp, #4]
	cmp	r0, #0
	ldr	ip, [sp, #32]
	addne	r3, r3, #1
	strne	r3, [r7, #-3328]
.L3026:
	ldr	r1, [ip]
	cmn	r1, #1
	bne	.L3027
.L3031:
	cmp	r8, r5
	ldr	r5, .L3169+8
	bne	.L3153
	add	fp, r4, #1
	uxth	r3, fp
	str	r3, [sp, #4]
	ldr	r3, [r5, #-536]
	ldr	r0, [r3, #4]
	b	.L3157
.L3025:
	ldr	r1, [ip, #4]
	uxth	r10, r6
	ldr	r0, .L3169+12
	bl	printk
	ldrh	r1, [r9]
	ldr	r3, .L3169+16
	strh	r1, [r3]	@ movhi
.L3027:
	add	fp, fp, #1
	b	.L3024
.L3153:
	uxth	r3, r4
	str	r3, [sp, #4]
	ldr	r3, [sp, #12]
	mov	r2, #36
	ldr	r3, [r3, #-536]
	mla	r8, r2, r8, r3
	ldr	r0, [r8, #4]
.L3157:
	ubfx	r0, r0, #10, #16
	bl	P2V_plane
	ldr	r2, .L3169
	str	r0, [sp, #12]
	ldrb	r0, [r9, #8]	@ zero_extendqisi2
	cmp	r0, #1
	bne	.L3033
	ldrb	r3, [r2, #148]	@ zero_extendqisi2
	cmp	r3, #0
	ldreq	r3, [sp, #4]
	addeq	fp, r2, r3, asl #1
	ldreqh	r3, [fp, #152]
	streq	r3, [sp, #4]
.L3033:
	movw	r3, #2388
	ldr	r1, [sp, #12]
	ldrh	r3, [r2, r3]
	ldr	r2, [sp, #4]
	cmp	r3, r2
	ldr	r2, [sp, #8]
	ldreqh	r3, [sp, #4]
	streqh	r3, [r9, #2]	@ movhi
	moveq	r3, #0
	streqb	r3, [r9, #6]
	streqh	r3, [r9, #4]	@ movhi
	ldrh	r3, [sp, #16]
	str	r3, [sp, #32]
	ldr	ip, [sp, #32]
	ldr	r3, [sp, #4]
	cmp	r3, r2
	cmpeq	r1, ip
	moveq	r0, r9
	moveq	r1, r3
	beq	.L3162
	ldr	r3, [sp, #28]
	movw	r2, #65535
	sub	fp, r3, #1
	clz	r3, r0
	mov	r3, r3, lsr #5
	cmp	r10, r2
	orrne	r3, r3, #1
	cmp	r3, #0
	beq	.L3037
	ldr	r3, [r5, #-368]
	uxth	r10, r6
	ldr	r7, .L3169+8
	mvn	r8, #0
	cmn	r3, #1
	mov	r6, r8
	ldreq	r3, .L3169+8
	streq	fp, [r3, #-368]
	ldr	r3, [r5, #-368]
	str	r3, [sp, #16]
	ldr	r3, [sp, #8]
	add	r3, r3, #7
	cmp	r10, r3
	subgt	r4, r10, #7
	ldrle	r4, [sp, #8]
	uxthgt	r4, r4
.L3040:
	cmp	r4, r10
	bhi	.L3053
	ldr	r3, .L3169+4
	mov	r0, #36
	ldr	lr, [r7, #-536]
	ldr	r1, [sp, #20]
	ldrh	r3, [r3]
	str	r3, [sp, #28]
	mov	r3, #0
	mov	r5, r3
.L3054:
	ldr	ip, [sp, #28]
	uxth	r2, r3
	cmp	r2, ip
	bcs	.L3165
	ldrh	r2, [r1, #2]!
	movw	ip, #65535
	add	r3, r3, #1
	cmp	r2, ip
	orrne	r2, r4, r2, asl #10
	mlane	ip, r0, r5, lr
	addne	r5, r5, #1
	uxthne	r5, r5
	strne	r2, [ip, #4]
	b	.L3054
.L3165:
	mov	r1, r5
	ldr	r2, [sp, #24]
	ldr	r0, [r7, #-536]
	bl	FlashReadPages
	ldr	r3, .L3169
	mov	r2, #36
	movw	r1, #65535
	ldrb	ip, [r3, #148]	@ zero_extendqisi2
	ldr	r3, [r7, #-536]
	mla	r5, r2, r5, r3
	ldr	r2, .L3169+20
	add	r2, r2, r4, asl #1
.L3043:
	cmp	r3, r5
	beq	.L3166
	ldr	r0, [r3]
	cmp	r0, #0
	bne	.L3044
	ldr	r0, [r3, #12]
	ldrh	lr, [r0]
	cmp	lr, r1
	beq	.L3045
	ldr	r0, [r0, #4]
	cmn	r0, #1
	beq	.L3045
	cmn	r8, #1
	ldr	r6, [r7, #-368]
	str	r0, [r7, #-368]
	bne	.L3045
	ldrh	r0, [r2]
	cmp	r0, r1
	bne	.L3046
	cmp	ip, #0
	beq	.L3045
.L3046:
	cmp	r6, fp
	mvneq	r8, #0
	movne	r8, r6
	b	.L3045
.L3044:
	ldr	r3, .L3169+16
	ldrh	r2, [r9]
	strh	r2, [r3]	@ movhi
	ldrb	r3, [r9, #8]	@ zero_extendqisi2
	cmp	r3, #0
	bne	.L3037
	ldr	r2, .L3169+20
	mov	r4, r4, asl #1
	ldr	r3, .L3169+8
	ldrh	r1, [r2, r4]
	movw	r2, #65535
	cmp	r1, r2
	bne	.L3048
	cmn	r8, #1
	strne	r8, [r3, #-368]
	bne	.L3037
	ldr	r2, [sp, #16]
	cmp	r2, fp
	bne	.L3158
	ldr	r2, [r3, #-368]
	b	.L3163
.L3048:
	cmp	r6, fp
	beq	.L3051
	cmn	r6, #1
	strne	r6, [r3, #-368]
	b	.L3037
.L3051:
	ldr	r2, [r3, #-368]
	cmp	r2, fp
	beq	.L3037
.L3163:
	sub	r2, r2, #1
	b	.L3158
.L3045:
	add	r3, r3, #36
	b	.L3043
.L3166:
	add	r4, r4, #1
	uxth	r4, r4
	b	.L3040
.L3053:
	ldr	r3, .L3169+8
	mvn	r2, #0
.L3158:
	str	r2, [r3, #-368]
.L3037:
	ldr	r2, .L3169+24
	movw	r3, #1858
	ldr	r10, [sp, #8]
	mov	r1, #1
	ldr	r4, .L3169+8
	strh	r1, [r2, r3]	@ movhi
.L3055:
	ldr	r3, .L3169+4
	movw	r1, #65535
	ldr	r8, [r4, #-536]
	mov	r0, #36
	ldr	r6, [sp, #20]
	mov	r5, #0
	ldrh	r7, [r3]
	ldrb	lr, [r3, #-2172]	@ zero_extendqisi2
	str	r5, [sp, #16]
.L3056:
	uxth	r3, r5
	cmp	r3, r7
	bcs	.L3167
	ldrh	r3, [r6, #2]!
	cmp	r3, r1
	beq	.L3057
	ldr	r2, [sp, #16]
	orr	r3, r10, r3, asl #10
	mla	r2, r0, r2, r8
	str	r3, [r2, #4]
	ldrb	ip, [r9, #8]	@ zero_extendqisi2
	cmp	ip, #1
	bne	.L3058
	cmp	lr, #0
	orrne	r3, r3, #-2147483648
	strne	r3, [r2, #4]
.L3058:
	ldr	r3, [sp, #16]
	add	r3, r3, #1
	uxth	r3, r3
	str	r3, [sp, #16]
.L3057:
	add	r5, r5, #1
	b	.L3056
.L3167:
	ldr	r0, [r4, #-536]
	ldr	r1, [sp, #16]
	ldr	r2, [sp, #24]
	bl	FlashReadPages
	mov	r3, #0
.L3161:
	str	r3, [sp, #28]
	ldr	r2, [sp, #16]
	ldrh	r3, [sp, #28]
	cmp	r3, r2
	bcs	.L3168
	ldr	r3, [sp, #28]
	mov	r5, #36
	ldr	r8, [r4, #-536]
	mul	r5, r5, r3
	add	r7, r8, r5
	ldr	r6, [r7, #4]
	ubfx	r0, r6, #10, #16
	str	r6, [sp, #60]
	bl	P2V_plane
	ldr	r3, [sp, #8]
	cmp	r10, r3
	bcc	.L3061
	ldr	r3, [sp, #32]
	ldr	r2, [sp, #8]
	cmp	r0, r3
	movcs	r3, #0
	movcc	r3, #1
	cmp	r10, r2
	movne	r3, #0
	cmp	r3, #0
	bne	.L3061
	ldr	r3, [sp, #12]
	ldr	r2, [sp, #4]
	cmp	r0, r3
	cmpeq	r10, r2
	beq	.L3062
	ldr	r3, [r8, r5]
	cmn	r3, #1
	beq	.L3063
	ldr	r7, [r7, #12]
	movw	r3, #61589
	ldrh	r2, [r7]
	cmp	r2, r3
	ldrneh	r0, [r9]
	bne	.L3159
	ldr	fp, [r7, #4]
	cmn	fp, #1
	beq	.L3065
	ldr	r1, [r4, #-3328]
	mov	r0, fp
	bl	ftl_cmp_data_ver
	cmp	r0, #0
	addne	r3, fp, #1
	strne	r3, [r4, #-3328]
.L3065:
	ldr	r6, [r7, #8]
	add	r1, sp, #56
	ldr	r3, [r7, #12]
	mov	r2, #0
	mov	r0, r6
	str	r3, [sp, #52]
	bl	log2phys
	ldr	r1, [r4, #-368]
	cmn	r1, #1
	beq	.L3066
	mov	r0, fp
	bl	ftl_cmp_data_ver
	cmp	r0, #0
	beq	.L3066
	ldr	r3, [sp, #52]
	cmn	r3, #1
	beq	.L3067
	ldr	r0, [r4, #-536]
	mov	r2, #0
	mov	r1, #1
	add	r0, r0, r5
	str	r3, [r0, #4]
	ldr	r8, [r0, #12]
	bl	FlashReadPages
	ldr	r2, [r4, #-536]
	ldr	r3, [r8, #4]
	add	ip, r2, r5
	str	r3, [sp, #36]
	ldr	r3, [r2, r5]
	cmn	r3, #1
	bne	.L3068
	b	.L3069
.L3067:
	ldr	r3, [sp, #60]
	ldr	r2, [sp, #56]
	cmp	r2, r3
	bne	.L3061
	mov	r0, r6
	add	r1, sp, #52
	mov	r2, #1
	bl	log2phys
	b	.L3061
.L3068:
	ldr	r7, [r8, #8]
	cmp	r7, r6
	bne	.L3069
	ldr	r0, [r4, #-368]
	ldr	r1, [sp, #36]
	str	r2, [sp, #44]
	str	ip, [sp, #40]
	bl	ftl_cmp_data_ver
	cmp	r0, #0
	ldr	ip, [sp, #40]
	ldr	r2, [sp, #44]
	beq	.L3069
	ldr	r3, [sp, #56]
	ldr	r1, [sp, #60]
	cmp	r3, r1
	beq	.L3074
	ldr	r1, [sp, #52]
	cmp	r3, r1
	beq	.L3069
	cmn	r3, #1
	streq	r3, [r2, r5]
	beq	.L3073
	str	r3, [ip, #4]
	mov	r0, ip
	mov	r1, #1
	mov	r2, #0
	ldr	r8, [ip, #12]
	bl	FlashReadPages
.L3073:
	ldr	r3, [r4, #-536]
	ldr	r3, [r3, r5]
	cmn	r3, #1
	beq	.L3074
	ldr	r5, [r8, #4]
	ldr	r0, [r4, #-368]
	mov	r1, r5
	bl	ftl_cmp_data_ver
	cmp	r0, #0
	beq	.L3074
	ldr	r0, [sp, #36]
	mov	r1, r5
	bl	ftl_cmp_data_ver
	cmp	r0, #0
	beq	.L3069
.L3074:
	mov	r0, r7
	ldr	r1, [sp, #52]
	bl	FtlReUsePrevPpa
.L3069:
	mvn	r3, #0
	str	r3, [sp, #52]
	b	.L3076
.L3066:
	ldr	r3, [sp, #60]
	ldr	r2, [sp, #56]
	cmp	r2, r3
	beq	.L3076
	ldr	r3, [sp, #52]
	cmn	r3, #1
	beq	.L3078
	ldr	r2, .L3169
	ubfx	r3, r3, #10, #21
	ldr	r2, [r2, #2336]
	cmp	r3, r2
	bcs	.L3061
.L3078:
	mov	r0, r6
	add	r1, sp, #60
	mov	r2, #1
	bl	log2phys
	ldr	r5, [sp, #56]
	cmn	r5, #1
	beq	.L3076
	ldr	r3, [sp, #52]
	cmp	r5, r3
	beq	.L3076
	ubfx	r0, r5, #10, #16
	bl	P2V_block_in_plane
	ldr	r3, .L3169+28
	ldrh	r2, [r3]
	cmp	r2, r0
	beq	.L3080
	ldrh	r2, [r3, #48]
	cmp	r2, r0
	beq	.L3080
	ldrh	r3, [r3, #96]
	cmp	r3, r0
	bne	.L3076
.L3080:
	ldr	r0, [r4, #-536]
	mov	r1, #1
	mov	r2, #0
	str	r5, [r0, #4]
	ldr	r7, [r0, #12]
	bl	FlashReadPages
	ldr	r3, [r4, #-536]
	ldr	r1, [r7, #4]
	ldr	r3, [r3]
	cmn	r3, #1
	beq	.L3076
	mov	r0, fp
	bl	ftl_cmp_data_ver
	cmp	r0, #0
	bne	.L3076
	mov	r0, r6
	add	r1, sp, #56
	mov	r2, #1
	bl	log2phys
.L3076:
	ldr	r0, [sp, #52]
	cmn	r0, #1
	beq	.L3061
	ubfx	r0, r0, #10, #16
	bl	P2V_block_in_plane
	ldr	r2, [r4, #-3544]
	mov	r3, r0, asl #1
	mov	r1, r0
	ldrh	r3, [r2, r3]
	cmp	r3, #0
	beq	.L3081
.L3159:
	bl	decrement_vpc_count
	b	.L3061
.L3081:
	ldr	r0, .L3169+32
	bl	printk
	b	.L3061
.L3063:
	ldrh	r2, [r9]
	mov	r1, r6
	ldr	r3, .L3169+16
	ldr	r0, .L3169+36
	strh	r2, [r3]	@ movhi
	mov	r2, fp
	bl	printk
	ldr	r3, .L3169+24
	ldr	r3, [r3, #1860]
	cmp	r3, #31
	bhi	.L3082
	ldr	r2, .L3169+24
	ldr	r1, [sp, #60]
	add	r2, r2, r3, asl #2
	add	r3, r3, #1
	str	r1, [r2, #1864]
	ldr	r2, .L3169+24
	str	r3, [r2, #1860]
.L3082:
	ldrh	r0, [r9]
	bl	decrement_vpc_count
	ldr	r3, [r4, #-368]
	cmn	r3, #1
	beq	.L3160
	cmp	r3, fp
	bls	.L3061
.L3160:
	str	fp, [r4, #-368]
.L3061:
	ldr	r3, [sp, #28]
	add	r3, r3, #1
	b	.L3161
.L3168:
	ldrb	r3, [r9, #8]	@ zero_extendqisi2
	add	r10, r10, #1
	cmp	r3, #1
	uxth	r10, r10
	bne	.L3085
	ldr	r3, .L3169
	ldrb	r3, [r3, #148]	@ zero_extendqisi2
	cmp	r3, #0
	beq	.L3085
	ldr	r3, .L3169+40
	ldr	r2, [sp, #4]
	ldrh	r3, [r3]
	cmp	r2, r10
	cmpeq	r3, r10
	beq	.L3062
.L3085:
	ldr	r2, .L3169+44
	ldrh	r3, [r2]
	cmp	r10, r3
	bne	.L3055
	ldrh	r1, [r2, #-68]
	movw	r0, #65535
	mov	r3, #0
	strh	r10, [r9, #2]	@ movhi
	strh	r3, [r9, #4]	@ movhi
.L3086:
	uxth	r2, r3
	cmp	r2, r1
	bcs	.L3152
	ldr	lr, [sp, #20]
	add	r3, r3, #1
	ldrh	ip, [lr, #2]!
	cmp	ip, r0
	str	lr, [sp, #20]
	beq	.L3086
	strb	r2, [r9, #6]
	b	.L3152
.L3062:
	ldrb	r3, [sp, #12]	@ zero_extendqisi2
	mov	r0, r9
	ldr	r1, [sp, #4]
	strb	r3, [r9, #6]
	ldrh	r3, [sp, #4]
	strh	r3, [r9, #2]	@ movhi
.L3162:
	ldr	r2, [sp, #12]
	bl	ftl_sb_update_avl_pages
.L3152:
	mov	r0, #0
	add	sp, sp, #68
	@ sp needed
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L3170:
	.align	2
.L3169:
	.word	.LANCHOR0
	.word	.LANCHOR0+2320
	.word	.LANCHOR2
	.word	.LC149
	.word	.LANCHOR4+1856
	.word	.LANCHOR2-2620
	.word	.LANCHOR4
	.word	.LANCHOR2-3524
	.word	.LC150
	.word	.LC151
	.word	.LANCHOR0+2390
	.word	.LANCHOR0+2388
	.fnend
	.size	FtlRecoverySuperblock, .-FtlRecoverySuperblock
	.align	2
	.global	FtlVpcCheckAndModify
	.type	FtlVpcCheckAndModify, %function
FtlVpcCheckAndModify:
	.fnstart
	@ args = 0, pretend = 0, frame = 8
	@ frame_needed = 0, uses_anonymous_args = 0
	stmfd	sp!, {r0, r1, r4, r5, r6, r7, r8, r9, r10, lr}
	.save {r4, r5, r6, r7, r8, r9, r10, lr}
	.pad #8
	mov	r4, #0
	ldr	r5, .L3188
	ldr	r1, .L3188+4
	ldr	r0, .L3188+8
	bl	printk
	movw	r3, #2330
	ldrh	r2, [r5, r3]
	mov	r1, #0
	ldr	r6, .L3188+12
	mov	r2, r2, asl #1
	ldr	r0, [r6, #-476]
	bl	ftl_memset
.L3172:
	ldr	r3, [r5, #2448]
	cmp	r4, r3
	bcs	.L3186
	mov	r0, r4
	add	r1, sp, #4
	mov	r2, #0
	bl	log2phys
	ldr	r0, [sp, #4]
	cmn	r0, #1
	beq	.L3173
	ubfx	r0, r0, #10, #16
	bl	P2V_block_in_plane
	ldr	r2, [r6, #-476]
	mov	r0, r0, asl #1
	ldrh	r3, [r2, r0]
	add	r3, r3, #1
	strh	r3, [r2, r0]	@ movhi
.L3173:
	add	r4, r4, #1
	b	.L3172
.L3186:
	ldr	r8, .L3188+16
	mov	r7, #0
	ldr	r10, .L3188+12
	ldr	r9, .L3188+20
.L3175:
	ldrh	r3, [r8]
	uxth	r4, r7
	cmp	r3, r4
	bls	.L3187
	ldr	r3, [r6, #-3544]
	mov	r5, r4, asl #1
	movw	r1, #65535
	ldrh	r2, [r3, r5]
	ldr	r3, [r6, #-476]
	ldrh	r3, [r3, r5]
	cmp	r2, r1
	cmpne	r2, r3
	beq	.L3176
	ldrh	r1, [r9]
	cmp	r1, r4
	beq	.L3176
	ldr	r1, .L3188+24
	ldrh	r0, [r1]
	cmp	r0, r4
	beq	.L3176
	ldrh	r1, [r1, #-48]
	cmp	r1, r4
	beq	.L3176
	ldr	r0, .L3188+28
	mov	r1, r4
	bl	printk
	ldr	r3, [r10, #-3544]
	ldrh	r2, [r3, r5]
	cmp	r2, #0
	ldr	r2, [r10, #-476]
	ldrh	r2, [r2, r5]
	strh	r2, [r3, r5]	@ movhi
	beq	.L3176
	mov	r0, r4
	bl	update_vpc_list
.L3176:
	add	r7, r7, #1
	b	.L3175
.L3187:
	bl	l2p_flush
	bl	FtlVpcTblFlush
	add	sp, sp, #8
	@ sp needed
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, pc}
.L3189:
	.align	2
.L3188:
	.word	.LANCHOR0
	.word	.LANCHOR3+216
	.word	.LC110
	.word	.LANCHOR2
	.word	.LANCHOR0+2328
	.word	.LANCHOR2-3524
	.word	.LANCHOR2-3428
	.word	.LC152
	.fnend
	.size	FtlVpcCheckAndModify, .-FtlVpcCheckAndModify
	.align	2
	.global	FtlGcScanTempBlk
	.type	FtlGcScanTempBlk, %function
FtlGcScanTempBlk:
	.fnstart
	@ args = 0, pretend = 0, frame = 64
	@ frame_needed = 0, uses_anonymous_args = 0
	ldr	r2, .L3243
	movw	r3, #3448
	stmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.save {r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.pad #68
	sub	sp, sp, #68
	ldrh	r5, [r2, r3]
	movw	r3, #65535
	mov	r4, r0
	str	r1, [sp, #12]
	cmp	r5, r3
	beq	.L3224
	cmp	r5, #0
	bne	.L3191
	b	.L3192
.L3224:
	mov	r5, #0
.L3191:
	ldr	r2, .L3243+4
	movw	r3, #2388
	ldrh	r3, [r2, r3]
	ldr	r2, [sp, #12]
	cmp	r2, r3
	bne	.L3193
.L3192:
	bl	FtlGcPageVarInit
.L3193:
	ldr	r6, .L3243+8
	mvn	r3, #0
	str	r3, [sp, #8]
	mov	r3, #0
	str	r3, [sp]
.L3194:
	ldrh	r1, [r4]
	movw	r3, #65535
	mov	r2, #0
	strb	r2, [r4, #8]
	cmp	r1, r3
	beq	.L3195
.L3196:
.L3221:
	ldr	r3, .L3243+12
	mov	r7, #0
	ldr	ip, [r6, #-2692]
	add	r1, r4, #14
	mov	r8, r7
	movw	r9, #65535
	ldrh	r3, [r3]
	mov	r10, #36
	str	r3, [sp, #4]
	ldr	r3, [r6, #-536]
	str	r3, [sp, #16]
	ldr	r3, [r6, #-2696]
	str	r3, [sp, #20]
	ldr	r3, .L3243+16
	ldrh	r0, [r3]
	ldrh	lr, [r3, #2]
.L3197:
	ldr	r2, [sp, #4]
	uxth	r3, r7
	cmp	r3, r2
	bcs	.L3241
	ldrh	r3, [r1, #2]!
	cmp	r3, r9
	beq	.L3198
	ldr	r2, [sp, #16]
	orr	r3, r5, r3, asl #10
	mla	r2, r10, r8, r2
	str	r3, [r2, #4]
	mul	r3, r0, r8
	add	fp, r3, #3
	cmp	r3, #0
	movlt	r3, fp
	ldr	fp, [sp, #20]
	bic	r3, r3, #3
	add	r3, fp, r3
	str	r3, [r2, #8]
	mul	r3, lr, r8
	add	r8, r8, #1
	uxth	r8, r8
	add	fp, r3, #3
	cmp	r3, #0
	movlt	r3, fp
	bic	r3, r3, #3
	add	r3, ip, r3
	str	r3, [r2, #12]
.L3198:
	add	r7, r7, #1
	b	.L3197
.L3241:
	ldr	r0, [r6, #-536]
	mov	r1, r8
	mov	r2, #0
	mov	fp, #0
	bl	FlashReadPages
.L3200:
	uxth	r3, fp
	cmp	r3, r8
	bcs	.L3242
	ldr	r3, .L3243+8
	mov	r9, #36
	mul	r9, r9, fp
	ldr	r7, [r3, #-536]
	add	r10, r7, r9
	ldr	r3, [r10, #4]
	ubfx	r0, r3, #10, #16
	str	r3, [sp, #4]
	bl	P2V_plane
	ldr	r7, [r7, r9]
	ldr	r10, [r10, #12]
	cmp	r7, #0
	ldr	ip, .L3243+4
	ldr	r3, .L3243+8
	mov	r2, r0
	bne	.L3201
	ldrh	r0, [r10]
	movw	r1, #65535
	cmp	r0, r1
	bne	.L3202
.L3205:
	ldrb	r1, [ip, #148]	@ zero_extendqisi2
	cmp	r1, #0
	beq	.L3236
	mov	r1, #1
	str	r1, [r3, #-372]
	b	.L3195
.L3202:
	ldr	r1, .L3243+4
	ldr	r0, [r10, #8]
	ldr	r1, [r1, #2448]
	cmp	r0, r1
	bls	.L3237
	b	.L3205
.L3236:
	ldrh	r3, [r4]
	ldr	r2, .L3243+8
	mov	r3, r3, asl #1
	ldr	r2, [r2, #-3544]
	b	.L3240
.L3237:
	ldr	r3, .L3243+4
	ldrb	r3, [r3, #32]	@ zero_extendqisi2
	cmp	r3, #0
	beq	.L3209
	add	r1, sp, #24
	mov	r2, r7
	bl	log2phys
	ldr	r3, [r10, #12]
	ldr	r2, [sp, #24]
	rsb	r1, r3, r2
	cmn	r2, #1
	clz	r1, r1
	mov	r1, r1, lsr #5
	moveq	r1, #0
	cmp	r1, #0
	beq	.L3209
	str	r3, [sp, #32]
	mov	r2, r7
	ldr	r3, [r6, #-504]
	add	r0, sp, #28
	mov	r1, #1
	str	r3, [sp, #36]
	ldr	r3, [r6, #-496]
	str	r3, [sp, #40]
	bl	FlashReadPages
	ldr	r3, .L3243+20
	ldr	r2, [r6, #-536]
	ldrh	r3, [r3]
	add	r9, r2, r9
	mov	r3, r3, asl #7
	ldr	r2, [sp, #36]
	b	.L3211
.L3212:
	add	r7, r7, #1
.L3211:
	cmp	r7, r3
	beq	.L3209
	ldr	r1, [r9, #8]
	ldr	r0, [r1, r7, asl #2]
	ldr	r1, [r2, r7, asl #2]
	cmp	r0, r1
	beq	.L3212
	ldrh	r1, [r4]
	ldr	r2, [sp, #32]
	ldr	r0, .L3243+24
	bl	printk
	ldrh	r3, [r4]
	ldr	r2, [r6, #-3544]
	mov	r1, #0
	mov	r3, r3, asl #1
.L3240:
	strh	r1, [r2, r3]	@ movhi
	ldrh	r0, [r4]
	bl	INSERT_FREE_LIST
	ldr	r2, .L3243+28
	mvn	r3, #0
	strh	r3, [r4]	@ movhi
	strh	r3, [r2]	@ movhi
	b	.L3239
.L3209:
	ldr	r0, [r10, #12]
	add	fp, fp, #1
	ldr	r1, [sp, #4]
	ldr	r2, [r10, #8]
	bl	FtlGcUpdatePage
	b	.L3200
.L3201:
	ldr	r2, [sp, #4]
	ldr	r0, .L3243+32
	ldrh	r1, [r4]
	str	r3, [sp, #20]
	str	ip, [sp, #16]
	bl	printk
	ldrh	r5, [r4]
	ldr	ip, [sp, #16]
	ldr	r3, [sp, #20]
	ldr	r2, [ip, #2244]
	cmp	r2, #0
	bne	.L3215
	ldrb	r2, [ip, #148]	@ zero_extendqisi2
	cmp	r2, #0
	beq	.L3216
.L3215:
	ldr	r1, [r3, #-3608]
	mov	r2, r5, asl #1
	ldrh	r2, [r1, r2]
	cmp	r2, #159
	bls	.L3217
.L3216:
	ldr	r2, [r3, #-536]
	ldr	r2, [r2, r9]
	cmn	r2, #1
	bne	.L3218
.L3217:
	ldr	r3, [r3, #-536]
	add	r9, r3, r9
	ldr	r3, [r9, #4]
	str	r3, [sp, #8]
.L3218:
	ldr	r3, .L3243+8
	mov	r5, r5, asl #1
	mov	r2, #0
	ldr	r3, [r3, #-3544]
	strh	r2, [r3, r5]	@ movhi
	ldrh	r0, [r4]
	bl	INSERT_FREE_LIST
	mvn	r3, #0
	strh	r3, [r4]	@ movhi
.L3239:
	bl	FtlGcPageVarInit
	mov	r5, #0
	b	.L3194
.L3242:
	ldr	r3, [sp]
	add	r5, r5, #1
	ldr	r2, [sp, #12]
	add	r3, r3, #1
	uxth	r5, r5
	cmp	r3, r2
	str	r3, [sp]
	ldr	r2, .L3243+36
	bcs	.L3220
.L3222:
	ldrh	r3, [r2]
	cmp	r3, r5
	bhi	.L3221
	mov	r2, #0
	b	.L3195
.L3220:
	ldr	r1, .L3243+40
	movw	r0, #65535
	ldrh	r3, [r1]
	cmp	r3, r0
	beq	.L3222
	ldr	r0, [sp]
	add	r3, r3, r0
	strh	r3, [r1]	@ movhi
	ldrh	r3, [r2]
	cmp	r3, r5
	bls	.L3222
	b	.L3223
.L3195:
	ldr	r1, .L3243
	movw	r3, #3448
	mvn	r0, #0
	strh	r5, [r4, #2]	@ movhi
	strb	r2, [r4, #6]
	strh	r0, [r1, r3]	@ movhi
	mov	r0, r4
	mov	r1, r5
	bl	ftl_sb_update_avl_pages
.L3223:
	ldr	r0, [sp, #8]
	add	sp, sp, #68
	@ sp needed
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L3244:
	.align	2
.L3243:
	.word	.LANCHOR1
	.word	.LANCHOR0
	.word	.LANCHOR2
	.word	.LANCHOR0+2320
	.word	.LANCHOR0+2398
	.word	.LANCHOR0+2394
	.word	.LC153
	.word	.LANCHOR2-3284
	.word	.LC154
	.word	.LANCHOR0+2388
	.word	.LANCHOR1+3448
	.fnend
	.size	FtlGcScanTempBlk, .-FtlGcScanTempBlk
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
	ldr	r5, .L3262
	ldr	r4, .L3262+4
	ldr	r9, [r5, #-3152]
	mov	r6, r5
	cmp	r9, #0
	beq	.L3246
	ldr	r2, [r5, #-3148]
	ldr	r3, [r4, #2448]
	cmp	r2, r3
	bcs	.L3247
	mov	r5, #2048
	mov	r7, r6
.L3252:
	ldr	r0, [r6, #-3148]
	ldr	r3, [r4, #2448]
	cmp	r0, r3
	bcs	.L3251
	mov	r2, #0
	mov	r1, sp
	bl	log2phys
	ldr	r3, [r7, #-3148]
	add	r3, r3, #1
	str	r3, [r7, #-3148]
	ldr	r2, [sp]
	cmn	r2, #1
	beq	.L3250
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
	bne	.L3251
	ldr	r0, [sp]
	ubfx	r0, r0, #10, #16
	bl	P2V_block_in_plane
	bl	FtlGcRefreshBlock
.L3251:
	mvn	r0, #0
	b	.L3254
.L3250:
	subs	r5, r5, #1
	bne	.L3252
	b	.L3251
.L3247:
	ldr	r3, [r5, #-3364]
	mov	r0, #0
	str	r0, [r5, #-3152]
	str	r0, [r5, #-3148]
	str	r3, [r5, #-3156]
	b	.L3254
.L3246:
	ldr	r1, [r5, #-3312]
	sub	r10, r5, #3600
	ldr	r8, [r5, #-3364]
	ldr	r3, [r4, #2448]
	movw	r4, #10000
	ldr	r7, [r5, #-3156]
	cmp	r1, r4
	add	r2, r8, #1048576
	movhi	r4, #31
	movls	r4, #63
	cmp	r7, r2
	bhi	.L3256
	mov	r1, r1, lsr #10
	mov	r0, #1000
	mul	r0, r0, r3
	add	r1, r1, #1
	bl	__aeabi_uidiv
	add	r0, r0, r7
	cmp	r0, r8
	bcc	.L3256
	ldrh	r3, [r10, #28]
	ands	r0, r4, r3
	movne	r0, r9
	bne	.L3254
	ldr	r2, [r5, #-3132]
	cmp	r2, r3
	beq	.L3254
.L3256:
	ldrh	r3, [r10, #28]
	mov	r0, #0
	str	r8, [r6, #-3156]
	str	r0, [r6, #-3148]
	str	r3, [r6, #-3132]
	mov	r3, #1
	str	r3, [r6, #-3152]
.L3254:
	add	sp, sp, #40
	@ sp needed
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, pc}
.L3263:
	.align	2
.L3262:
	.word	.LANCHOR2
	.word	.LANCHOR0
	.fnend
	.size	FtlReadRefresh, .-FtlReadRefresh
	.align	2
	.global	FtlGcFreeTempBlock
	.type	FtlGcFreeTempBlock, %function
FtlGcFreeTempBlock:
	.fnstart
	@ args = 0, pretend = 0, frame = 24
	@ frame_needed = 0, uses_anonymous_args = 0
	stmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.save {r4, r5, r6, r7, r8, r9, r10, fp, lr}
	movw	r3, #2388
	ldr	r4, .L3303
	.pad #28
	sub	sp, sp, #28
	ldr	r6, .L3303+4
	ldr	ip, [r4, #-3616]
	ldrh	r1, [r6, r3]
	cmp	ip, #0
	bne	.L3301
	sub	r7, r4, #3424
	movw	lr, #65535
	ldrh	r5, [r7, #-4]
	cmp	r5, lr
	bne	.L3267
.L3276:
	ldrh	r2, [r7, #-4]
	movw	r3, #65535
	ldr	r5, .L3303
	mov	r8, #0
	cmp	r2, r3
	str	r8, [r4, #-372]
	sub	r9, r5, #3424
	beq	.L3301
	bl	FtlCacheWriteBack
	movw	r2, #2388
	ldrb	r0, [r5, #-3421]	@ zero_extendqisi2
	ldrh	r2, [r6, r2]
	ldrh	r3, [r9, #-4]
	ldr	r1, [r5, #-3544]
	smulbb	r2, r0, r2
	mov	r3, r3, asl #1
	strh	r2, [r1, r3]	@ movhi
	sub	r2, r5, #2656
	ldr	r3, [r5, #-3344]
	sub	fp, r2, #12
	ldrh	ip, [r2, #-12]
	sub	r10, r2, #772
	add	r3, ip, r3
	str	r3, [r5, #-3344]
	stmib	sp, {r2, r5}
	b	.L3277
.L3267:
	cmp	r0, #0
	beq	.L3270
	ldr	r2, .L3303+8
	movw	r3, #3448
	ldrh	r0, [r2, r3]
	cmp	r0, lr
	beq	.L3271
.L3272:
	mov	r1, #2
	b	.L3270
.L3271:
	strh	ip, [r2, r3]	@ movhi
	sub	r3, r4, #3520
	ldrh	r3, [r3, #-8]
	cmp	r3, #17
	bhi	.L3272
.L3270:
	ldr	r0, .L3303+12
	bl	FtlGcScanTempBlk
	cmn	r0, #1
	str	r0, [sp, #20]
	beq	.L3273
	ldr	r3, .L3303
	mov	r5, r5, asl #1
	ldr	r2, [r3, #-3608]
	ldrh	r3, [r2, r5]
	cmp	r3, #4
	bls	.L3274
	sub	r3, r3, #5
	mov	r0, #1
	strh	r3, [r2, r5]	@ movhi
	bl	FtlEctTblFlush
.L3274:
	ldr	r3, [r4, #-372]
	ldr	r2, .L3303
	cmp	r3, #0
	bne	.L3275
	ldr	r0, [sp, #20]
	ldr	r3, [r2, #-3136]
	ubfx	r0, r0, #10, #16
	add	r3, r3, #1
	str	r3, [r2, #-3136]
	bl	FtlBbmMapBadBlock
	bl	FtlBbmTblFlush
.L3275:
	mov	r3, #0
	str	r3, [r4, #-372]
	b	.L3287
.L3273:
	ldr	r2, .L3303+8
	movw	r3, #3448
	ldrh	r2, [r2, r3]
	movw	r3, #65535
	cmp	r2, r3
	bne	.L3287
	b	.L3276
.L3280:
	ldr	r3, [r5, #4]
	cmp	r0, r3
	bne	.L3299
.L3279:
	add	r8, r8, #1
.L3277:
	ldrh	r2, [fp]
	uxth	r3, r8
	cmp	r2, r3
	bls	.L3302
	mov	r9, #12
	ldr	r2, [r6, #2448]
	mul	r9, r9, r3
	ldr	r3, [sp, #8]
	ldr	ip, [r3, #-2672]
	add	r5, ip, r9
	ldr	r0, [r5, #8]
	cmp	r0, r2
	bcs	.L3299
	mov	r2, #0
	add	r1, sp, #20
	str	ip, [sp, #12]
	bl	log2phys
	ldr	ip, [sp, #12]
	ldr	r0, [sp, #20]
	ldr	r2, [ip, r9]
	cmp	r0, r2
	bne	.L3280
	ubfx	r0, r0, #10, #16
	bl	P2V_block_in_plane
	add	r1, r5, #4
	mov	r2, #1
	mov	r9, r0
	ldr	r0, [r5, #8]
	bl	log2phys
	mov	r0, r9
	b	.L3300
.L3299:
	ldrh	r0, [r10]
.L3300:
	bl	decrement_vpc_count
	b	.L3279
.L3302:
	movw	r0, #65535
	bl	decrement_vpc_count
	ldrb	r3, [r6, #148]	@ zero_extendqisi2
	cmp	r3, #0
	beq	.L3282
	ldr	r0, .L3303+16
	ldrh	r1, [r7, #-4]
	bl	printk
.L3282:
	ldrh	r0, [r7, #-4]
	ldr	r2, [r4, #-3544]
	mov	r3, r0, asl #1
	ldrh	r3, [r2, r3]
	cmp	r3, #0
	beq	.L3283
	bl	INSERT_DATA_LIST
	b	.L3284
.L3283:
	bl	INSERT_FREE_LIST
.L3284:
	ldr	r3, [sp, #4]
	mvn	r5, #0
	ldr	r4, .L3303
	strh	r5, [r7, #-4]	@ movhi
	mov	r7, #0
	strh	r7, [r3, #-12]	@ movhi
	sub	r3, r4, #2672
	strh	r7, [r3, #-4]	@ movhi
	bl	l2p_flush
	bl	FtlVpcTblFlush
	sub	r3, r4, #3280
	sub	r2, r4, #3520
	strh	r5, [r3, #-4]	@ movhi
	ldr	r3, [r6, #2244]
	ldrh	r2, [r2, #-8]
	cmp	r3, r7
	sub	r3, r4, #3296
	ldrh	r3, [r3, #-8]
	beq	.L3285
	ldr	r1, [r4, #-3308]
	cmp	r1, #39
	bhi	.L3285
	cmp	r2, r3
	subcc	r4, r4, #2720
	movcc	r3, r3, asl #1
	strcch	r3, [r4]	@ movhi
	b	.L3301
.L3285:
	add	r1, r3, r3, asl #1
	cmp	r2, r1, asr #2
	ble	.L3301
	ldrb	r0, [r6, #148]	@ zero_extendqisi2
	ldr	r2, .L3303+20
	cmp	r0, #0
	moveq	r3, #20
	streqh	r3, [r2]	@ movhi
	beq	.L3266
	sub	r3, r3, #2
	strh	r3, [r2]	@ movhi
.L3301:
	mov	r0, #0
	b	.L3266
.L3287:
	mov	r0, #1
.L3266:
	add	sp, sp, #28
	@ sp needed
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L3304:
	.align	2
.L3303:
	.word	.LANCHOR2
	.word	.LANCHOR0
	.word	.LANCHOR1
	.word	.LANCHOR2-3428
	.word	.LC155
	.word	.LANCHOR2-2720
	.fnend
	.size	FtlGcFreeTempBlock, .-FtlGcFreeTempBlock
	.align	2
	.global	FtlGcPageRecovery
	.type	FtlGcPageRecovery, %function
FtlGcPageRecovery:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	stmfd	sp!, {r3, r4, r5, r6, r7, lr}
	.save {r3, r4, r5, r6, r7, lr}
	movw	r5, #2388
	ldr	r7, .L3308
	ldr	r6, .L3308+4
	ldr	r4, .L3308+8
	mov	r0, r7
	ldrh	r1, [r6, r5]
	bl	FtlGcScanTempBlk
	ldrh	r2, [r7, #2]
	ldrh	r3, [r6, r5]
	cmp	r2, r3
	ldmccfd	sp!, {r3, r4, r5, r6, r7, pc}
	sub	r0, r4, #432
	bl	FtlMapBlkWriteDumpData
	mov	r0, #0
	bl	FtlGcFreeTempBlock
	mov	r3, #0
	str	r3, [r4, #-372]
	ldmfd	sp!, {r3, r4, r5, r6, r7, pc}
.L3309:
	.align	2
.L3308:
	.word	.LANCHOR2-3428
	.word	.LANCHOR0
	.word	.LANCHOR2
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
	mov	r6, #0
	ldr	r4, .L3312
	ldr	r3, .L3312+4
	sub	r5, r4, #3520
	sub	r4, r4, #3472
	sub	r5, r5, #4
	sub	r4, r4, #4
	str	r6, [r3, #1860]
	mov	r0, r5
	bl	FtlRecoverySuperblock
	mov	r0, r5
	bl	FtlSlcSuperblockCheck
	mov	r0, r4
	bl	FtlRecoverySuperblock
	mov	r0, r4
	bl	FtlSlcSuperblockCheck
	bl	FtlGcPageRecovery
	movw	r0, #65535
	bl	decrement_vpc_count
	mov	r0, r6
	ldmfd	sp!, {r4, r5, r6, pc}
.L3313:
	.align	2
.L3312:
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
	stmfd	sp!, {r3, r4, r5, r6, r7, r8, r9, lr}
	.save {r3, r4, r5, r6, r7, r8, r9, lr}
	movw	r3, #1858
	ldr	r4, .L3332
	mov	r2, #0
	ldr	r6, .L3332+4
	ldr	r5, .L3332+8
	strh	r2, [r4, r3]	@ movhi
	add	r3, r4, #1856
	mvn	r2, #0
	strh	r2, [r3]	@ movhi
	ldr	r3, [r6, #2324]
	uxth	r0, r3
	bl	FtlFreeSysBlkQueueInit
	bl	FtlScanSysBlk
	sub	r3, r5, #3296
	ldrh	r2, [r3, #-4]
	movw	r3, #65535
	cmp	r2, r3
	bne	.L3315
.L3317:
	mvn	r8, #0
	b	.L3316
.L3315:
	bl	FtlLoadSysInfo
	subs	r8, r0, #0
	bne	.L3317
	bl	FtlLoadMapInfo
	mov	r7, r4
	bl	FtlLoadVonderInfo
	bl	Ftl_load_ext_data
	bl	FtlLoadEctTbl
	bl	FtlFreeSysBLkSort
	bl	SupperBlkListInit
	bl	FtlPowerLostRecovery
	mov	r0, #1
	bl	FtlUpdateVaildLpn
	ldr	r2, [r5, #-3380]
	movw	r3, #2426
	mov	r0, #12
	ldrh	r1, [r6, r3]
	mov	r3, r8
.L3318:
	cmp	r3, r1
	bge	.L3323
	mla	ip, r0, r3, r2
	ldr	ip, [ip, #4]
	cmp	ip, #0
	bge	.L3319
.L3323:
	ldr	r4, .L3332+12
	cmp	r3, r1
	add	r9, r4, #80
	ldrh	r2, [r4, #28]
	add	r2, r2, #1
	strh	r2, [r4, #28]	@ movhi
	bge	.L3330
	b	.L3320
.L3319:
	add	r3, r3, #1
	b	.L3318
.L3330:
	movw	r3, #1858
	ldrh	r3, [r7, r3]
	cmp	r3, #0
	beq	.L3324
.L3320:
	ldrh	r2, [r9, #-4]
	ldr	r3, .L3332+16
	ldr	r0, [r5, #-3544]
	mov	r2, r2, asl #1
	ldrh	ip, [r3, #4]
	ldrh	r1, [r0, r2]
	rsb	r1, ip, r1
	movw	ip, #2388
	strh	r1, [r0, r2]	@ movhi
	add	r1, r3, #48
	ldrh	r2, [r6, ip]
	ldr	lr, [r5, #-3544]
	ldrh	r7, [r1, #4]
	strh	r2, [r3, #2]	@ movhi
	mov	r2, #0
	strh	r2, [r3, #4]	@ movhi
	ldrh	r3, [r3, #48]
	strb	r2, [r5, #-3518]
	mov	r3, r3, asl #1
	ldrh	r0, [lr, r3]
	rsb	r0, r7, r0
	strh	r0, [lr, r3]	@ movhi
	ldrh	r3, [r6, ip]
	strb	r2, [r5, #-3470]
	strh	r2, [r1, #4]	@ movhi
	strh	r3, [r1, #2]	@ movhi
	ldrh	r3, [r4, #30]
	add	r3, r3, #1
	strh	r3, [r4, #30]	@ movhi
	bl	l2p_flush
	bl	FtlVpcTblFlush
	bl	FtlVpcTblFlush
.L3324:
	ldrh	r0, [r9, #-4]
	movw	r3, #65535
	ldr	r5, .L3332+16
	cmp	r0, r3
	beq	.L3325
	ldrh	r3, [r5, #4]
	cmp	r3, #0
	bne	.L3325
	ldrh	r3, [r5, #52]
	add	r9, r5, #52
	add	r7, r5, #48
	cmp	r3, #0
	bne	.L3325
	bl	FtlGcRefreshOpenBlock
	ldrh	r0, [r5, #48]
	bl	FtlGcRefreshOpenBlock
	bl	FtlVpcTblFlush
	mov	r0, r5
	bl	allocate_new_data_superblock
	mov	r0, r7
	bl	allocate_new_data_superblock
.L3325:
	ldrb	r3, [r6, #32]	@ zero_extendqisi2
	cmp	r3, #0
	bne	.L3326
	ldrh	r3, [r4, #28]
	tst	r3, #31
	bne	.L3316
.L3326:
	bl	FtlVpcCheckAndModify
.L3316:
	mov	r0, r8
	ldmfd	sp!, {r3, r4, r5, r6, r7, r8, r9, pc}
.L3333:
	.align	2
.L3332:
	.word	.LANCHOR4
	.word	.LANCHOR0
	.word	.LANCHOR2
	.word	.LANCHOR2-3600
	.word	.LANCHOR2-3524
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
	ldr	r4, .L3369
	ldr	r6, [r4, #-3616]
	cmp	r6, #0
	bne	.L3336
	ldr	r5, .L3369+4
	movw	r7, #2424
	mov	r1, r6
	ldr	r0, [r4, #-448]
	ldrh	r2, [r5, r7]
	mov	r2, r2, asl #2
	bl	ftl_memset
	ldrh	r2, [r5, r7]
	mov	r1, r6
	ldr	r0, [r4, #-452]
	mov	r2, r2, asl #2
	bl	ftl_memset
	ldr	r3, [r5, #2324]
	str	r6, [r4, #-3332]
	str	r6, [r4, #-3328]
	uxth	r0, r3
	bl	FtlFreeSysBlkQueueInit
	bl	FtlLoadBbt
	cmp	r0, #0
	beq	.L3337
	bl	FtlMakeBbt
.L3337:
	ldr	r0, .L3369+8
	mov	r2, #0
	ldr	ip, .L3369+12
.L3338:
	ldrh	r1, [r0]
	uxth	r3, r2
	add	r2, r2, #1
	cmp	r3, r1, asl #7
	bge	.L3366
	ldr	lr, [r4, #-508]
	mvn	r1, r3
	orr	r1, r3, r1, asl #16
	str	r1, [lr, r3, asl #2]
	ldr	r1, [r4, #-504]
	str	ip, [r1, r3, asl #2]
	b	.L3338
.L3366:
	ldr	r2, .L3369+4
	movw	r3, #2328
	ldr	r7, .L3369+16
	mov	r8, #0
	ldrh	r6, [r2, r3]
.L3340:
	ldrh	r3, [r7]
	ldr	r10, .L3369+4
	cmp	r3, r6
	ldr	r9, .L3369+16
	bls	.L3367
	mov	r0, r6
	mov	r1, #1
	bl	FtlLowFormatEraseBlock
	add	r6, r6, #1
	uxth	r6, r6
	add	r0, r8, r0
	uxth	r8, r0
	b	.L3340
.L3367:
	add	r7, r10, #2320
	sub	r3, r8, #3
	ldrh	r1, [r7]
	cmp	r3, r1, asl #1
	blt	.L3342
	mov	r0, r8
	mov	r8, #0
	bl	__aeabi_uidiv
	ldr	r3, [r10, #2420]
	add	r0, r0, r3
	uxth	r0, r0
	bl	FtlSysBlkNumInit
	ldr	r3, [r10, #2324]
	uxth	r0, r3
	bl	FtlFreeSysBlkQueueInit
	movw	r3, #2328
	ldrh	r6, [r10, r3]
.L3343:
	ldrh	r3, [r9]
	cmp	r3, r6
	bls	.L3342
	mov	r0, r6
	mov	r1, #1
	bl	FtlLowFormatEraseBlock
	add	r6, r6, #1
	uxth	r6, r6
	add	r0, r8, r0
	uxth	r8, r0
	b	.L3343
.L3342:
	ldr	r10, .L3369+20
	mov	r9, #0
	mov	r6, r9
.L3345:
	ldrh	r2, [r10]
	uxth	r0, r9
	ldr	r3, .L3369+4
	add	r9, r9, #1
	cmp	r2, r0
	bls	.L3368
	mov	r1, #0
	bl	FtlLowFormatEraseBlock
	add	r0, r6, r0
	uxth	r6, r0
	b	.L3345
.L3368:
	movw	r2, #2330
	ldrh	r9, [r7]
	ldrh	r2, [r3, r2]
	str	r3, [sp, #4]
	mov	r1, r9
	ldr	r7, .L3369+24
	str	r2, [r4, #-548]
	ldr	r2, [r3, #2332]
	mov	r0, r2
	str	r2, [sp]
	bl	__aeabi_uidiv
	ubfx	r10, r0, #5, #16
	ldr	r3, [sp, #4]
	add	r1, r10, #36
	strh	r1, [r7, #-8]	@ movhi
	mov	r1, #24
	mov	fp, r0
	mul	r1, r1, r9
	str	r0, [r3, #2448]
	ldr	r2, [sp]
	cmp	r6, r1
	ble	.L3347
	rsb	r0, r6, r2
	mov	r1, r9
	str	r3, [sp]
	bl	__aeabi_uidiv
	ldr	r3, [sp]
	str	r0, [r3, #2448]
	mov	r0, r0, lsr #5
	add	r0, r0, #24
	strh	r0, [r7, #-8]	@ movhi
.L3347:
	ldr	r3, [r5, #2244]
	cmp	r3, #1
	bne	.L3348
	ldrh	r3, [r7, #-8]
	mov	r0, r6
	mov	r1, r9
	str	r3, [sp]
	bl	__aeabi_uidiv
	ldr	r3, [sp]
	uxtah	r0, r3, r0
	add	r3, r3, r0, asr #2
	strh	r3, [r7, #-8]	@ movhi
.L3348:
	ldrb	r3, [r5, #148]	@ zero_extendqisi2
	cmp	r3, #0
	beq	.L3349
	ldrh	r3, [r7, #-8]
	mov	r0, r6
	mov	r1, r9
	str	r3, [sp]
	bl	__aeabi_uidiv
	ldr	r3, [sp]
	uxtah	r0, r3, r0
	add	r3, r3, r0, asr #2
	strh	r3, [r7, #-8]	@ movhi
.L3349:
	movw	r3, #2382
	ldrh	r3, [r5, r3]
	cmp	r3, #0
	beq	.L3351
	ldrh	r2, [r7, #-8]
	add	r2, r2, r3, lsr #1
	strh	r2, [r7, #-8]	@ movhi
	mul	r2, r9, r3
	cmp	r2, r6
	addgt	r3, r3, #32
	ldrgt	r2, .L3369+4
	addgt	r3, r10, r3
	strgt	fp, [r2, #2448]
	ldrgt	r2, .L3369+24
	strgth	r3, [r2, #-8]	@ movhi
.L3351:
	ldrh	r2, [r7, #-8]
	ldr	r3, [r5, #2448]
	rsb	r3, r2, r3
	mul	r9, r9, r3
	movw	r3, #2388
	ldrh	r3, [r5, r3]
	str	r9, [r4, #1796]
	mul	r9, r3, r9
	movw	r3, #2394
	ldrh	r3, [r5, r3]
	str	r9, [r5, #2448]
	mul	r9, r3, r9
	str	r9, [r5, #2428]
	bl	FtlBbmTblFlush
	ldr	r2, [r5, #2336]
	movw	r3, #2402
	ldrh	r3, [r5, r3]
	add	r1, r6, r8
	add	r3, r3, r2, lsr #3
	cmp	r1, r3
	bls	.L3353
	ldr	r0, .L3369+28
	mov	r2, r2, lsr #5
	bl	printk
.L3353:
	movw	r3, #2330
	ldr	r6, .L3369+32
	ldrh	r2, [r5, r3]
	mov	r1, #0
	sub	r8, r6, #244
	ldr	r0, [r4, #-3544]
	mvn	r9, #0
	mov	r2, r2, asl #1
	mov	fp, r8
	bl	ftl_memset
	mov	r3, #0
	strh	r3, [r6, #-2]	@ movhi
	mov	r1, #255
	strh	r3, [r6, #-244]	@ movhi
	str	r3, [r4, #-3372]
	strb	r3, [r4, #-3278]
	strb	r3, [r4, #-3276]
	strh	r3, [r8, #2]	@ movhi
	strb	r3, [r4, #-3518]
	mov	r3, #1
	strb	r3, [r4, #-3516]
	movw	r3, #2328
	ldrh	r2, [r5, r3]
	ldr	r0, [r4, #-3368]
	strh	r9, [r6, #-4]	@ movhi
	add	r6, r6, #3280
	mov	r2, r2, lsr #3
	bl	ftl_memset
.L3354:
	ldr	r10, .L3369+36
	ldr	r5, .L3369
	mov	r0, r10
	bl	make_superblock
	ldrb	r3, [r4, #-3517]	@ zero_extendqisi2
	cmp	r3, #0
	ldrh	r3, [r8]
	bne	.L3355
	ldr	r2, [r6, #-3544]
	mov	r3, r3, asl #1
	strh	r9, [r2, r3]	@ movhi
	ldrh	r3, [fp]
	add	r3, r3, #1
	strh	r3, [fp]	@ movhi
	b	.L3354
.L3355:
	ldr	r2, [r5, #-3332]
	mov	r3, r3, asl #1
	ldrh	r1, [r10, #4]
	mov	r9, r5
	str	r2, [r5, #-3512]
	add	r2, r2, #1
	str	r2, [r5, #-3332]
	ldr	r2, [r5, #-3544]
	strh	r1, [r2, r3]	@ movhi
	sub	r2, r5, #3472
	sub	r6, r2, #4
	mov	r3, #0
	strb	r3, [r5, #-3470]
	strh	r3, [r6, #2]	@ movhi
	ldrh	r3, [r10]
	mvn	r10, #0
	add	r3, r3, #1
	strh	r3, [r2, #-4]	@ movhi
	mov	r3, #1
	strb	r3, [r5, #-3468]
.L3356:
	ldr	r8, .L3369+40
	ldr	r5, .L3369
	mov	r0, r8
	bl	make_superblock
	ldrb	r3, [r4, #-3469]	@ zero_extendqisi2
	cmp	r3, #0
	ldrh	r3, [r6]
	bne	.L3357
	ldr	r2, [r9, #-3544]
	mov	r3, r3, asl #1
	strh	r10, [r2, r3]	@ movhi
	ldrh	r3, [r6]
	add	r3, r3, #1
	strh	r3, [r6]	@ movhi
	b	.L3356
.L3357:
	ldr	r2, [r5, #-3332]
	mov	r3, r3, asl #1
	ldrh	r1, [r8, #4]
	mvn	r4, #0
	str	r2, [r5, #-3464]
	add	r2, r2, #1
	str	r2, [r5, #-3332]
	ldr	r2, [r5, #-3544]
	strh	r1, [r2, r3]	@ movhi
	sub	r3, r5, #3424
	strh	r4, [r3, #-4]	@ movhi
	bl	FtlFreeSysBlkQueueOut
	ldr	r3, .L3369+44
	mov	r2, #0
	strh	r2, [r3, #2]	@ movhi
	ldr	r2, [r5, #1796]
	strh	r4, [r3, #4]	@ movhi
	strh	r2, [r3, #6]	@ movhi
	ldr	r3, [r5, #-3332]
	str	r3, [r5, #-3292]
	add	r3, r3, #1
	str	r3, [r5, #-3332]
	strh	r0, [r7, #-4]	@ movhi
	bl	FtlVpcTblFlush
	bl	FtlSysBlkInit
	cmp	r0, #0
	ldreq	r3, .L3369+48
	moveq	r2, #1
	streq	r2, [r3, #504]
.L3336:
	mov	r0, #0
	add	sp, sp, #12
	@ sp needed
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L3370:
	.align	2
.L3369:
	.word	.LANCHOR2
	.word	.LANCHOR0
	.word	.LANCHOR0+2394
	.word	168778952
	.word	.LANCHOR0+2330
	.word	.LANCHOR0+2328
	.word	.LANCHOR2-3296
	.word	.LC156
	.word	.LANCHOR2-3280
	.word	.LANCHOR2-3524
	.word	.LANCHOR2-3476
	.word	.LANCHOR2-3300
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
	stmfd	sp!, {r4, r5, lr}
	.save {r4, r5, lr}
	.pad #20
	sub	sp, sp, #20
	ldr	r4, .L3400
	ldrb	r3, [r4, #148]	@ zero_extendqisi2
	cmp	r3, #0
	beq	.L3372
.L3374:
	mov	r0, #0
	b	.L3373
.L3372:
	ldr	r5, .L3400+4
	ldr	r0, [r5, #1684]
	bl	FlashInit
	cmp	r0, #0
	bne	.L3374
	bl	FlashLoadFactorBbt
	cmp	r0, #0
	beq	.L3375
	bl	FlashMakeFactorBbt
.L3375:
	ldr	r0, [r5, #1724]
	bl	FlashReadIdbDataRaw
	cmp	r0, #0
	beq	.L3376
	mov	r1, #0
	mov	r2, #16
	mov	r0, sp
	bl	FlashReadFacBbtData
	mov	r3, #0
	mov	r2, r3
	mov	ip, #1
	ldr	r1, [sp]
.L3377:
	ands	lr, r1, ip, asl r2
	add	r0, r3, #1
	add	r2, r2, #1
	movne	r3, r0
	cmp	r2, #16
	bne	.L3377
	cmp	r3, #6
	bls	.L3396
	mov	r2, #0
	mov	ip, #1
.L3380:
	ands	lr, r1, ip, asl r2
	add	r0, r3, #1
	add	r2, r2, #1
	movne	r3, r0
	cmp	r2, #24
	bne	.L3380
	cmp	r3, #17
	movhi	r3, #36
	strhib	r3, [r4, #33]
	bhi	.L3379
.L3396:
	strb	r2, [r4, #33]
.L3379:
	ldrb	r3, [r4, #33]	@ zero_extendqisi2
	strh	r3, [r4, #146]	@ movhi
.L3376:
	ldr	r1, .L3400+8
	ldr	r0, .L3400+12
	bl	printk
	ldr	r0, .L3400+16
	bl	FtlConstantsInit
	bl	FtlVariablesInit
	ldr	r0, [r4, #2324]
	mov	r4, #1
	uxth	r0, r0
	bl	FtlFreeSysBlkQueueInit
.L3382:
	bl	FtlLoadBbt
	cmp	r0, #0
	beq	.L3383
.L3398:
	bl	FtlLowFormat
	cmp	r4, #3
	addls	r4, r4, #1
	bls	.L3382
.L3399:
	mvn	r0, #0
	b	.L3373
.L3383:
	bl	FtlSysBlkInit
	cmp	r0, #0
	bne	.L3398
	ldr	r3, .L3400+20
	mov	r2, #1
	str	r2, [r3, #504]
.L3373:
	add	sp, sp, #20
	@ sp needed
	ldmfd	sp!, {r4, r5, pc}
.L3401:
	.align	2
.L3400:
	.word	.LANCHOR0
	.word	.LANCHOR2
	.word	.LC77
	.word	.LC76
	.word	.LANCHOR0+120
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
	ldr	r5, .L3419
	ldr	r3, [r5, #-3616]
	cmp	r3, #0
	beq	.L3403
.L3406:
	mov	r0, #0
	ldmfd	sp!, {r4, r5, r6, r7, r8, pc}
.L3403:
	ldr	r3, .L3419+4
	ldrb	r3, [r3, #148]	@ zero_extendqisi2
	cmp	r3, #0
	beq	.L3405
	ldr	r3, [r5, #-2704]
	tst	r3, #1
	beq	.L3405
	ldr	r3, .L3419+8
	ldrh	r3, [r3, #4]
	cmp	r3, #0
	bne	.L3406
.L3405:
	mov	r2, #0
	ldr	r0, [r5, #-532]
	ldr	r1, [r5, #-2704]
	mov	r3, r2
	bl	FlashProgPages
	ldr	r7, .L3419
	mov	r6, #0
	mov	r8, #36
.L3407:
	ldr	r1, [r5, #-2704]
	uxth	r3, r6
	ldr	r4, .L3419
	cmp	r3, r1
	bcs	.L3418
	mul	r3, r8, r3
	ldr	r0, [r7, #-532]
	add	r6, r6, #1
	add	r1, r0, r3
	ldr	r0, [r0, r3]
	ldr	r2, [r1, #12]
	cmn	r0, #1
	bne	.L3408
	sub	r1, r4, #3424
	ldr	ip, [r4, #-3544]
	mov	lr, #0
	ldrh	r2, [r1, #-4]
	mov	r2, r2, asl #1
	strh	lr, [ip, r2]	@ movhi
	ldr	r2, [r4, #-3136]
	strh	r0, [r1, #-4]	@ movhi
	add	r2, r2, #1
	str	r2, [r4, #-3136]
	ldr	r2, [r4, #-532]
	add	r3, r2, r3
	ldr	r0, [r3, #4]
	ubfx	r0, r0, #10, #16
	bl	FtlBbmMapBadBlock
	bl	FtlBbmTblFlush
	bl	FtlGcPageVarInit
	b	.L3417
.L3408:
	ldr	r0, [r2, #12]
	ldr	r1, [r1, #4]
	ldr	r2, [r2, #8]
	bl	FtlGcUpdatePage
	b	.L3407
.L3418:
	ldr	r0, [r4, #-532]
	bl	FtlGcBufFree
	mov	r3, #0
	str	r3, [r4, #-2704]
	ldr	r3, .L3419+8
	ldrh	r3, [r3, #4]
	cmp	r3, #0
	bne	.L3406
	mov	r0, #1
	bl	FtlGcFreeTempBlock
.L3417:
	mov	r0, #1
	ldmfd	sp!, {r4, r5, r6, r7, r8, pc}
.L3420:
	.align	2
.L3419:
	.word	.LANCHOR2
	.word	.LANCHOR0
	.word	.LANCHOR2-3428
	.fnend
	.size	Ftl_gc_temp_data_write_back, .-Ftl_gc_temp_data_write_back
	.align	2
	.global	Ftl_get_new_temp_ppa
	.type	Ftl_get_new_temp_ppa, %function
Ftl_get_new_temp_ppa:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	stmfd	sp!, {r3, r4, r5, lr}
	.save {r3, r4, r5, lr}
	ldr	r3, .L3425
	sub	r2, r3, #4
	ldrh	r1, [r3, #-4]
	movw	r3, #65535
	cmp	r1, r3
	beq	.L3422
	ldrh	r3, [r2, #4]
	cmp	r3, #0
	bne	.L3423
.L3422:
	ldr	r4, .L3425+4
	bl	FtlCacheWriteBack
	mov	r0, #0
	mov	r5, #0
	bl	FtlGcFreeTempBlock
	ldr	r0, .L3425+8
	strb	r5, [r4, #-3420]
	bl	allocate_data_superblock
	sub	r3, r4, #2672
	sub	r4, r4, #2656
	strh	r5, [r3, #-4]	@ movhi
	strh	r5, [r4, #-12]	@ movhi
	bl	l2p_flush
	mov	r0, r5
	bl	FtlEctTblFlush
	bl	FtlVpcTblFlush
.L3423:
	ldr	r0, .L3425+8
	ldmfd	sp!, {r3, r4, r5, lr}
	b	get_new_active_ppa
.L3426:
	.align	2
.L3425:
	.word	.LANCHOR2-3424
	.word	.LANCHOR2
	.word	.LANCHOR2-3428
	.fnend
	.size	Ftl_get_new_temp_ppa, .-Ftl_get_new_temp_ppa
	.align	2
	.global	ftl_do_gc
	.type	ftl_do_gc, %function
ftl_do_gc:
	.fnstart
	@ args = 0, pretend = 0, frame = 32
	@ frame_needed = 0, uses_anonymous_args = 0
	ldr	r3, .L3592
	ldr	ip, [r3, #-3616]
	cmp	ip, #0
	bne	.L3521
	stmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.save {r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.pad #44
	sub	sp, sp, #44
	ldr	r4, .L3592+4
	ldr	r8, [r4, #504]
	cmp	r8, #1
	bne	.L3523
	ldr	r2, [r3, #-560]
	cmp	r2, #0
	bne	.L3523
	mov	r9, r0
	sub	r0, r3, #3536
	ldrh	r0, [r0]
	cmp	r0, #47
	movls	r0, r2
	bls	.L3580
	movw	r2, #3448
	str	r1, [sp, #12]
	ldrh	r1, [r4, r2]
	movw	r2, #65535
	mov	r5, r3
	str	r9, [sp, #16]
	cmp	r1, r2
	bne	.L3429
.L3432:
	ldr	r4, .L3592+8
	movw	r2, #65535
	ldrh	r0, [r4, #-8]
	cmp	r0, r2
	bne	.L3430
	b	.L3431
.L3429:
	sub	r3, r3, #3424
	ldrh	r3, [r3, #-4]
	cmp	r3, r2
	beq	.L3432
	mov	r0, r8
	bl	FtlGcFreeTempBlock
	cmp	r0, #0
	beq	.L3432
	mov	r0, r8
	b	.L3580
.L3430:
	ldrh	r3, [r4, #-10]
	cmp	r3, r2
	bne	.L3431
	ldrh	r1, [r4, #-6]
	cmp	r1, r3
	beq	.L3431
	ldrh	r2, [r4, #-4]
	cmp	r2, r3
	strneh	r0, [r4, #-10]	@ movhi
	strneh	r1, [r4, #-8]	@ movhi
	mvnne	r3, #0
	strneh	r2, [r4, #-6]	@ movhi
	strneh	r3, [r4, #-4]	@ movhi
.L3431:
	ldr	r3, [sp, #16]
	ldr	r8, [r5, #-2716]
	ldr	r6, .L3592+12
	cmp	r3, #1
	add	r8, r8, #1
	add	r8, r8, r3, asl #7
	sub	r7, r6, #240
	str	r8, [r5, #-2716]
	bne	.L3443
	ldr	r9, .L3592+16
	ldr	r3, [r9, #2244]
	cmp	r3, #0
	bne	.L3434
	ldrb	r3, [r9, #148]	@ zero_extendqisi2
	cmp	r3, #0
	beq	.L3443
.L3434:
	ldr	r3, [r5, #-3308]
	ldr	fp, .L3592
	cmp	r3, #39
	bhi	.L3443
	ldr	r10, .L3592+20
	movw	r3, #1992
	ldrh	r3, [r10, r3]
	add	r8, r8, r3
	str	r8, [fp, #-2716]
	bl	FtlGcReFreshBadBlk
	ldrh	r8, [r6, #-4]
	movw	r3, #65535
	cmp	r8, r3
	bne	.L3443
	ldrh	r3, [r4, #-10]
	cmp	r3, r8
	bne	.L3443
	ldr	r3, [fp, #-2716]
	cmp	r3, #1024
	bhi	.L3435
	ldrh	r3, [r7, #-8]
	cmp	r3, #63
	bhi	.L3443
.L3435:
	movw	ip, #1992
	mov	r2, #0
	strh	r2, [r10, ip]	@ movhi
	ldr	r10, .L3592
	ldrh	r1, [r7, #-8]
	sub	r8, r10, #2704
	ldr	r3, .L3592+20
	ldrh	fp, [r8, #-14]
	add	fp, fp, #64
	cmp	r1, fp
	bgt	.L3443
	str	r2, [r10, #-2716]
	ldr	r2, [r10, #-3308]
	cmp	r2, #0
	moveq	r2, #6
	beq	.L3582
	cmp	r2, #5
	bhi	.L3437
	mov	r2, #18
.L3582:
	strh	r2, [r3, ip]	@ movhi
.L3437:
	mov	r0, #32
	bl	List_get_gc_head_node
	movw	ip, #65535
	uxth	r3, r0
	cmp	r3, ip
	beq	.L3442
	ldrh	r0, [r8, #-8]
	ldr	r2, .L3592
	cmp	r0, #0
	sub	r10, r2, #2704
	beq	.L3439
	mov	r8, r3, asl #1
	movw	r3, #2390
	ldrh	lr, [r9, r3]
	ldr	r3, .L3592+24
	ldr	r2, [r2, #-3544]
	ldrh	r3, [r3]
	ldrh	r1, [r2, r8]
	mul	r3, r3, lr
	add	r3, r3, #1
	cmp	r1, r3
	bgt	.L3442
	add	r1, r0, #1
	str	ip, [sp, #28]
	str	r2, [sp, #24]
	mov	r9, #0
	uxth	r1, r1
	str	r9, [r10, #-4]
	strh	r1, [r10, #-8]	@ movhi
	str	r1, [sp, #20]
	bl	List_get_gc_head_node
	ldr	ip, [sp, #28]
	uxth	fp, r0
	ldr	r1, [sp, #20]
	cmp	fp, ip
	ldr	r2, [sp, #24]
	beq	.L3442
	mov	ip, fp, asl #1
	ldr	r0, .L3592+28
	str	ip, [sp, #20]
	ldrh	r3, [r2, ip]
	ldrh	r2, [r2, r8]
	str	r2, [sp]
	mov	r2, fp
	bl	printk
	ldrh	r3, [r10, #-8]
	cmp	r3, #40
	ldr	ip, [sp, #20]
	bls	.L3440
	ldr	r3, [r10, #-840]
	ldrh	r3, [r3, ip]
	cmp	r3, #32
	strhih	r9, [r10, #-8]	@ movhi
.L3440:
	ldr	r2, .L3592+20
	movw	r3, #1992
	mov	r1, #6
	strh	r1, [r2, r3]	@ movhi
	b	.L3444
.L3439:
	mov	r3, #1
	strh	r3, [r10, #-8]	@ movhi
.L3442:
	bl	GetSwlReplaceBlock
	movw	r3, #65535
	cmp	r0, r3
	mov	fp, r0
	bne	.L3444
	ldr	r2, .L3592+20
	movw	r3, #1992
	mov	r1, #0
	strh	r1, [r2, r3]	@ movhi
.L3443:
	ldrh	ip, [r6, #-4]
	movw	r3, #65535
	ldr	r8, .L3592
	cmp	ip, r3
	bne	.L3585
	sub	r3, r8, #3424
	ldrh	r3, [r3, #-4]
	cmp	r3, ip
	movne	fp, ip
	beq	.L3588
.L3444:
	movw	r2, #65535
	rsb	r3, r2, fp
	clz	r3, r3
	ldr	r1, [sp, #16]
	ldr	r8, .L3592+32
	mov	r3, r3, lsr #5
	cmp	r1, #0
	movne	r0, #0
	andeq	r0, r3, #1
	cmp	r0, #0
	beq	.L3457
	ldrh	r0, [r8, #-8]
	cmp	r0, #24
	movhi	r9, #1
	bhi	.L3458
	ldr	r2, .L3592+16
	movw	r3, #2388
	cmp	r0, #16
	ldrh	r9, [r2, r3]
	movhi	r9, r9, lsr #5
	bhi	.L3458
	cmp	r0, #12
	movhi	r9, r9, lsr #4
	bhi	.L3458
	cmp	r0, #8
	movhi	r9, r9, lsr #2
.L3458:
	ldr	r1, .L3592
	sub	r2, r1, #2720
	ldrh	r3, [r2]
	cmp	r3, r0
	bcs	.L3462
	sub	r3, r1, #3424
	movw	r0, #65535
	ldrh	r3, [r3, #-4]
	cmp	r3, r0
	bne	.L3463
	ldrh	r0, [r4, #-10]
	cmp	r0, r3
	bne	.L3463
	ldr	r0, .L3592+20
	movw	r3, #1992
	ldrh	r0, [r0, r3]
	cmp	r0, #0
	bne	.L3464
	ldr	r3, .L3592+16
	ldr	r1, [r1, #-3372]
	ldr	r3, [r3, #2448]
	add	r3, r3, r3, asl #1
	cmp	r1, r3, lsr #2
	movcs	r3, #18
	bcs	.L3465
.L3464:
	ldr	r3, .L3592+36
	ldrh	r3, [r3, #-8]
	add	r3, r3, r3, asl #1
	ubfx	r3, r3, #2, #16
.L3465:
	strh	r3, [r2]	@ movhi
	mov	r3, #0
	str	r3, [r5, #-2708]
	b	.L3580
.L3463:
	ldr	r3, .L3592+36
	ldrh	r3, [r3, #-8]
	add	r3, r3, r3, asl #1
	mov	r3, r3, asr #2
	strh	r3, [r2]	@ movhi
.L3462:
	ldr	r3, .L3592+16
	movw	fp, #65535
	ldr	r2, [r3, #2244]
	ldr	r3, [sp, #12]
	cmp	r3, #2
	movhi	r3, #0
	movls	r3, #1
	cmp	r2, #0
	moveq	r3, #0
	cmp	r3, #0
	addne	r9, r9, #1
	uxthne	r9, r9
	b	.L3467
.L3457:
	ldrh	r1, [r8, #92]
	add	r9, r8, #3520
	cmp	r1, r2
	bne	.L3468
	ldrh	r2, [r4, #-10]
	cmp	r2, r1
	movne	r3, #0
	andeq	r3, r3, #1
	cmp	r3, #0
	beq	.L3468
	ldrh	r3, [r6, #-4]
	cmp	r3, r1
	beq	.L3469
.L3473:
	movw	fp, #65535
	b	.L3468
.L3469:
	sub	fp, r9, #2720
	ldrh	r2, [r7, #-8]
	str	r0, [r9, #-2708]
	ldrh	r3, [fp]
	ldr	r10, .L3592+20
	cmp	r2, r3
	bls	.L3470
	movw	r3, #1992
	ldrh	r3, [r10, r3]
	cmp	r3, #0
	bne	.L3471
	ldr	r3, .L3592+16
	ldr	r2, [r9, #-3372]
	ldr	r3, [r3, #2448]
	add	r3, r3, r3, asl #1
	cmp	r2, r3, lsr #2
	movcs	r3, #18
	bcs	.L3472
.L3471:
	ldr	r3, .L3592+36
	ldrh	r3, [r3, #-8]
	add	r3, r3, r3, asl #1
	ubfx	r3, r3, #2, #16
.L3472:
	strh	r3, [fp]	@ movhi
	bl	FtlReadRefresh
	mov	r0, #0
	bl	List_get_gc_head_node
	ldr	r3, [r5, #-3544]
	uxth	r0, r0
	mov	r0, r0, asl #1
	ldrh	r3, [r3, r0]
	cmp	r3, #4
	movwhi	r3, #1992
	ldrhih	r0, [r10, r3]
	bhi	.L3580
.L3470:
	movw	r9, #1992
	ldrh	r0, [r10, r9]
	cmp	r0, #0
	bne	.L3473
	ldr	r3, .L3592+36
	ldrh	r10, [r3, #-8]
	add	r3, r10, r10, asl #1
	mov	r3, r3, asr #2
	strh	r3, [fp]	@ movhi
	bl	List_get_gc_head_node
	ldr	r3, [r5, #-3544]
	movw	r2, #2390
	uxth	r0, r0
	mov	r0, r0, asl #1
	ldrh	r1, [r3, r0]
	ldr	r3, .L3592+16
	ldrh	r2, [r3, r2]
	add	r3, r3, #2320
	ldrh	r3, [r3]
	mul	r3, r3, r2
	add	r3, r3, r3, lsr #31
	cmp	r1, r3, asr #1
	ble	.L3474
	ldrh	r3, [r7, #-8]
	sub	r2, r10, #1
	cmp	r3, r2
	blt	.L3474
	bl	FtlReadRefresh
	ldr	r3, .L3592+20
	ldrh	r0, [r3, r9]
	b	.L3580
.L3474:
	cmp	r1, #0
	bne	.L3473
	movw	r0, #65535
	bl	decrement_vpc_count
	ldrh	r0, [r8, #-8]
	add	r0, r0, #1
	b	.L3580
.L3468:
	ldr	r3, .L3592+16
	ldr	r3, [r3, #2244]
	cmp	r3, #0
	moveq	r9, #1
	movne	r9, #2
.L3467:
	ldrh	r3, [r6, #-4]
	movw	r0, #65535
	ldr	r2, .L3592
	cmp	r3, r0
	sub	r1, r2, #3280
	bne	.L3476
	cmp	fp, r3
	strneh	fp, [r1, #-4]	@ movhi
	bne	.L3478
	ldrh	r3, [r4, #-10]
	sub	r1, r2, #2656
	cmp	r3, fp
	beq	.L3478
	ldr	r2, [r2, #-3544]
	mov	r3, r3, asl #1
	ldrh	r3, [r2, r3]
	cmp	r3, #0
	mvneq	r3, #0
	streqh	r3, [r1, #-10]	@ movhi
	ldrh	r3, [r4, #-10]
	strh	r3, [r6, #-4]	@ movhi
	mvn	r3, #0
	strh	r3, [r4, #-10]	@ movhi
.L3478:
	ldrh	r0, [r6, #-4]
	mov	r3, #0
	strb	r3, [r5, #-3276]
	movw	r3, #65535
	cmp	r0, r3
	beq	.L3476
	bl	IsBlkInGcList
	cmp	r0, #0
	ldrne	r3, .L3592+12
	mvnne	r2, #0
	strneh	r2, [r3, #-4]	@ movhi
	ldr	r3, .L3592+16
	ldrb	r3, [r3, #148]	@ zero_extendqisi2
	cmp	r3, #0
	beq	.L3482
	ldrh	r0, [r6, #-4]
	bl	ftl_get_blk_mode
	strb	r0, [r5, #-3276]
.L3482:
	ldrh	r2, [r6, #-4]
	movw	r3, #65535
	ldr	r5, .L3592
	cmp	r2, r3
	sub	r7, r5, #3280
	sub	r4, r7, #4
	beq	.L3476
	mov	r0, r4
	bl	make_superblock
	ldr	r2, .L3592+20
	movw	r1, #1994
	mov	r3, #0
	strh	r3, [r7, #-2]	@ movhi
	strh	r3, [r2, r1]	@ movhi
	strb	r3, [r5, #-3278]
	ldrh	r3, [r7, #-4]
	ldr	r1, [r5, #-3544]
	mov	r3, r3, asl #1
	ldrh	r1, [r1, r3]
	movw	r3, #1996
	strh	r1, [r2, r3]	@ movhi
.L3476:
	ldrh	r3, [r6, #-4]
	ldrh	r2, [r8, #-4]
	cmp	r2, r3
	beq	.L3483
	ldr	r2, .L3592+40
	ldrh	r1, [r2, #-4]
	cmp	r1, r3
	beq	.L3483
	ldrh	r2, [r2, #44]
	cmp	r2, r3
	bne	.L3517
.L3483:
	mvn	r3, #0
	strh	r3, [r6, #-4]	@ movhi
.L3517:
	ldr	r6, .L3592+44
	movw	r3, #65535
	ldr	r5, .L3592
	ldrh	r2, [r6]
	mov	r10, r6
	cmp	r2, r3
	bne	.L3485
	mov	r3, #0
	str	r5, [sp, #12]
	str	r3, [r5, #-2708]
.L3486:
	ldr	r8, .L3592+48
	ldrh	r4, [r8]
	mov	r0, r4
	bl	List_get_gc_head_node
	movw	r3, #65535
	uxth	r7, r0
	strh	r7, [r10]	@ movhi
	cmp	r7, r3
	moveq	r3, #0
	moveq	r0, #8
	streqh	r3, [r8]	@ movhi
	beq	.L3580
.L3487:
	mov	r0, r7
	add	r4, r4, #1
	bl	IsBlkInGcList
	cmp	r0, #0
	ldrne	r3, .L3592+48
	strneh	r4, [r3]	@ movhi
	bne	.L3486
	ldr	r3, .L3592+48
	uxth	r4, r4
	mov	r2, r7, asl #1
	strh	r4, [r3]	@ movhi
	ldr	r3, [sp, #12]
	ldr	r1, [r3, #-3544]
	ldr	r3, .L3592+52
	ldrh	r0, [r1, r2]
	ldrh	ip, [r3]
	ldrh	r3, [r3, #-68]
	mul	r3, r3, ip
	add	ip, r3, r3, lsr #31
	cmp	r0, ip, asr #1
	bgt	.L3490
	cmp	r0, #8
	cmphi	r4, #48
	bls	.L3491
	ldrh	r0, [r8, #36]
	cmp	r0, #35
	bhi	.L3491
.L3490:
	ldr	ip, .L3592+48
	mov	r0, #0
	strh	r0, [ip]	@ movhi
.L3491:
	ldrh	r2, [r1, r2]
	movw	r1, #65535
	cmp	r2, r3
	cmpge	fp, r1
	bne	.L3492
	ldr	r3, .L3592+48
	ldrh	r1, [r3]
	cmp	r1, #3
	bhi	.L3492
	sub	r2, r3, #568
	mvn	r1, #0
	strh	r1, [r2, #-4]	@ movhi
	mov	r2, #0
	strh	r2, [r3]	@ movhi
	b	.L3586
.L3492:
	cmp	r2, #0
	bne	.L3493
	movw	r0, #65535
	bl	decrement_vpc_count
	ldr	r3, .L3592+48
	ldr	r2, .L3592+48
	ldrh	r3, [r3]
	add	r3, r3, #1
	strh	r3, [r2]	@ movhi
	b	.L3486
.L3493:
	mov	r3, #0
	strb	r3, [r5, #-3276]
	ldr	r3, .L3592+16
	ldrb	r3, [r3, #148]	@ zero_extendqisi2
	cmp	r3, #0
	beq	.L3494
	mov	r0, r7
	bl	ftl_get_blk_mode
	ldr	r3, .L3592
	strb	r0, [r3, #-3276]
.L3494:
	ldr	r0, .L3592+44
	bl	make_superblock
	ldrh	r2, [r6]
	ldr	r1, .L3592+56
	mov	r3, #0
	ldr	r0, [r5, #-3544]
	mov	r2, r2, asl #1
	strh	r3, [r1]	@ movhi
	ldrh	r2, [r0, r2]
	strh	r3, [r6, #2]	@ movhi
	strb	r3, [r5, #-3278]
	strh	r2, [r1, #2]	@ movhi
.L3485:
	ldr	r3, [sp, #16]
	cmp	r3, #1
	bne	.L3495
	bl	FtlReadRefresh
.L3495:
	mov	r3, #1
	str	r3, [r5, #-560]
	ldr	r3, .L3592+52
	ldrh	r3, [r3]
	str	r3, [sp, #12]
	ldr	r3, .L3592+16
	ldrb	r3, [r3, #148]	@ zero_extendqisi2
	cmp	r3, #0
	beq	.L3496
	ldr	r3, .L3592
	ldrb	r3, [r3, #-3276]	@ zero_extendqisi2
	cmp	r3, #1
	ldreq	r3, .L3592+60
	ldreqh	r3, [r3]
	streq	r3, [sp, #12]
.L3496:
	ldrh	r3, [r6, #2]
	mov	r6, #0
	ldr	r1, [sp, #12]
	add	r2, r3, r9
	ldr	r4, .L3592
	cmp	r2, r1
	movgt	r2, r1
	rsbgt	r3, r3, r2
	uxthgt	r9, r3
	b	.L3498
.L3591:
	ldr	r0, [r4, #-2688]
	mov	r1, r5
	ldrb	r2, [r4, #-3276]	@ zero_extendqisi2
	mov	r8, #0
	bl	FlashReadPages
.L3501:
	uxth	r3, r8
	cmp	r3, r5
	bcs	.L3589
	mov	r3, #36
	ldr	r2, [r4, #-2688]
	mul	r7, r3, r8
	add	r1, r2, r7
	ldr	r2, [r2, r7]
	ldr	r10, [r1, #12]
	cmn	r2, #1
	beq	.L3537
	ldrh	r1, [r10]
	movw	r2, #61589
	cmp	r1, r2
	bne	.L3537
	add	r1, sp, #32
	mov	r2, #0
	ldr	r0, [r10, #8]
	str	r3, [sp, #20]
	bl	log2phys
	ldr	r2, [r4, #-2688]
	add	r2, r2, r7
	ldr	r0, [r2, #4]
	ldr	r1, [sp, #32]
	ldr	r3, [sp, #20]
	bic	r1, r1, #-2147483648
	cmp	r1, r0
	bne	.L3537
	ldr	r1, .L3592+56
	ldr	r0, .L3592+56
	ldr	r2, [r2, #16]
	ldrh	r1, [r1]
	str	r3, [sp, #24]
	add	r1, r1, #1
	strh	r1, [r0]	@ movhi
	ldr	r0, [r4, #-2704]
	ldr	r1, [r4, #-532]
	mla	r1, r3, r0, r1
	str	r2, [r1, #16]
	str	r1, [sp, #20]
	bl	Ftl_get_new_temp_ppa
	ldr	r2, [r4, #-532]
	ldr	r1, [sp, #20]
	ldr	r3, [sp, #24]
	str	r0, [r1, #4]
	ldr	r1, [r4, #-2704]
	mla	r3, r3, r1, r2
	ldr	r2, [r4, #-2688]
	add	r2, r2, r7
	ldr	r1, [r2, #8]
	str	r1, [r3, #8]
	mov	r1, #1
	ldr	r2, [r2, #12]
	str	r2, [r3, #12]
	ldr	r3, [sp, #32]
	str	r3, [r10, #12]
	ldr	r3, .L3592+64
	ldrh	r3, [r3]
	strh	r3, [r10, #2]	@ movhi
	ldr	r3, [r4, #-3328]
	ldr	r0, [r4, #-2688]
	str	r3, [r10, #4]
	add	r0, r0, r7
	ldr	r3, [r4, #-2704]
	add	r3, r3, #1
	str	r3, [r4, #-2704]
	bl	FtlGcBufAlloc
	ldr	r3, .L3592+16
	ldrb	r3, [r3, #148]	@ zero_extendqisi2
	cmp	r3, #0
	beq	.L3590
.L3503:
	bl	Ftl_gc_temp_data_write_back
	cmp	r0, #0
	beq	.L3537
	ldr	r3, .L3592
	mov	r2, #0
	mvn	r1, #0
	sub	r3, r3, #3280
	str	r2, [r3, #2720]
	strh	r1, [r3, #-4]	@ movhi
	strh	r2, [r3, #-2]	@ movhi
.L3586:
	ldr	r2, .L3592+20
	movw	r3, #1992
	ldrh	r0, [r2, r3]
	b	.L3580
.L3589:
	add	r6, r6, #1
.L3498:
	uxth	r3, r6
	ldr	r7, .L3592
	cmp	r3, r9
	ldr	r5, .L3592+44
	bcs	.L3505
	ldr	r3, .L3592+24
	add	r0, r5, #14
	ldrh	r1, [r5, #2]
	movw	lr, #65535
	ldr	r7, [r4, #-2688]
	mov	ip, #36
	ldrh	r8, [r3]
	add	r1, r1, r6
	mov	r3, #0
	mov	r5, r3
.L3506:
	uxth	r2, r3
	cmp	r2, r8
	bcs	.L3591
	ldrh	r2, [r0, #2]!
	add	r3, r3, #1
	cmp	r2, lr
	orrne	r2, r1, r2, asl #10
	mlane	r10, ip, r5, r7
	addne	r5, r5, #1
	uxthne	r5, r5
	strne	r2, [r10, #4]
	b	.L3506
.L3590:
	ldrb	r3, [r4, #-3421]	@ zero_extendqisi2
	ldr	r2, [r4, #-2704]
	cmp	r2, r3
	beq	.L3503
	ldr	r3, .L3592+64
	ldrh	r3, [r3, #4]
	cmp	r3, #0
	beq	.L3503
.L3537:
	add	r8, r8, #1
	b	.L3501
.L3505:
	ldrh	r3, [r5, #2]
	add	r9, r9, r3
	ldr	r3, [sp, #12]
	uxth	r9, r9
	strh	r9, [r5, #2]	@ movhi
	cmp	r9, r3
	bcc	.L3507
	ldr	r3, [r7, #-2704]
	cmp	r3, #0
	beq	.L3508
	bl	Ftl_gc_temp_data_write_back
	cmp	r0, #0
	movne	r3, #0
	strne	r3, [r7, #-560]
	bne	.L3586
.L3508:
	ldr	r3, .L3592+56
	ldrh	r4, [r3]
	cmp	r4, #0
	bne	.L3509
	ldrh	r3, [r5]
	ldr	r2, [r7, #-3544]
	mov	r3, r3, asl #1
	ldrh	r3, [r2, r3]
	cmp	r3, #0
	beq	.L3509
	ldr	r6, .L3592+16
.L3510:
	ldr	r3, [r6, #2448]
	cmp	r4, r3
	bcs	.L3515
	mov	r0, r4
	add	r1, sp, #36
	mov	r2, #0
	bl	log2phys
	ldr	r0, [sp, #36]
	cmn	r0, #1
	beq	.L3511
	ubfx	r0, r0, #10, #16
	bl	P2V_block_in_plane
	ldrh	r3, [r5]
	cmp	r3, r0
	bne	.L3511
.L3515:
	ldr	r3, .L3592+16
	ldr	r3, [r3, #2448]
	cmp	r4, r3
	bcc	.L3509
	ldrh	r3, [r5]
	mov	r1, #0
	ldr	r2, [r7, #-3544]
	mov	r3, r3, asl #1
	strh	r1, [r2, r3]	@ movhi
	ldrh	r0, [r5]
	bl	update_vpc_list
	bl	FtlCacheWriteBack
	bl	l2p_flush
	bl	FtlVpcTblFlush
	b	.L3509
.L3511:
	add	r4, r4, #1
	b	.L3510
.L3509:
	mvn	r3, #0
	strh	r3, [r5]	@ movhi
.L3507:
	ldr	r3, .L3592+68
	ldrh	r3, [r3]
	cmp	r3, #2
	ldrls	r3, .L3592+52
	ldrlsh	r9, [r3]
	bls	.L3517
.L3516:
	ldr	r2, .L3592
	mov	r1, #0
	str	r1, [r2, #-560]
	movw	r2, #1992
	ldr	r1, .L3592+20
	ldrh	r0, [r1, r2]
	cmp	r0, #0
	addeq	r0, r3, #1
	b	.L3580
.L3521:
	mov	r0, #0
	bx	lr
.L3523:
	mov	r0, ip
	b	.L3580
.L3588:
	ldrh	r9, [r4, #-10]
	cmp	r9, r3
	bne	.L3585
	sub	r3, r8, #3520
	ldr	r2, [r8, #-2716]
	ldrh	r3, [r3, #-8]
	cmp	r3, #24
	movcc	r3, #5120
	movcs	r3, #1024
	cmp	r2, r3
	movls	fp, r9
	bls	.L3444
	ldr	r1, .L3592+20
	movw	r2, #1992
	mov	r3, #0
	str	r3, [r5, #-2716]
	strh	r3, [r1, r2]	@ movhi
	bl	GetSwlReplaceBlock
	cmp	r0, r9
	mov	fp, r0
	bne	.L3446
	sub	r3, r8, #2704
	ldrh	r1, [r7, #-8]
	ldrh	r2, [r3, #-14]
	mov	r9, r3
	cmp	r1, r2
	movcs	r2, #80
	strcsh	r2, [r3, #-14]	@ movhi
	bcs	.L3456
	mov	r0, #64
	bl	List_get_gc_head_node
	uxth	r3, r0
	cmp	r3, fp
	beq	.L3456
	ldr	r2, [r8, #-564]
	ldr	r10, .L3592+16
	cmp	r2, #0
	bne	.L3449
	movw	r1, #2340
	ldrh	r1, [r10, r1]
	cmp	r1, #3
	beq	.L3449
	ldr	r1, [r8, #-3236]
	cmp	r1, #0
	bne	.L3449
	ldr	r1, [r10, #2244]
	cmp	r1, #0
	bne	.L3449
	ldrb	r0, [r10, #148]	@ zero_extendqisi2
	cmp	r0, #0
	beq	.L3450
.L3449:
	ldr	r2, [r5, #-3544]
	mov	r3, r3, asl #1
	movw	r0, #2340
	ldrh	r0, [r10, r0]
	ldrh	r1, [r2, r3]
	movw	r3, #2390
	ldr	r2, .L3592+24
	cmp	r0, #3
	ldrh	r3, [r10, r3]
	ldrh	r2, [r2]
	mul	r2, r2, r3
	moveq	r3, r3, lsr #1
	movne	r3, #0
	add	r3, r2, r3
	cmp	r1, r3
	bgt	.L3452
	mov	r0, #0
	bl	List_get_gc_head_node
	ldr	r3, [r10, #2448]
	ldr	r2, [r5, #-3372]
	add	r3, r3, r3, asl #1
	cmp	r2, r3, lsr #2
	movls	r3, #160
	uxth	fp, r0
	bls	.L3583
	b	.L3587
.L3450:
	ldr	r2, [r8, #-3544]
	mov	r3, r3, asl #1
	ldrh	r3, [r2, r3]
	cmp	r3, #7
	bhi	.L3455
	bl	List_get_gc_head_node
	uxth	fp, r0
.L3587:
	mov	r3, #128
.L3583:
	strh	r3, [r9, #-14]	@ movhi
	movw	r3, #65535
	cmp	fp, r3
	beq	.L3456
	b	.L3446
.L3452:
	mov	r3, #128
	b	.L3584
.L3455:
	mov	r3, #64
.L3584:
	strh	r3, [r9, #-14]	@ movhi
	b	.L3456
.L3446:
	ldr	r0, [r5, #-3608]
	mov	r1, fp, asl #1
	ldr	r3, [r5, #-3544]
	ldrh	r2, [r7, #-8]
	ldrh	r3, [r3, r1]
	ldrh	r1, [r0, r1]
	ldr	r0, .L3592+72
	str	r1, [sp]
	ldr	r1, .L3592+76
	ldrh	r1, [r1]
	str	r1, [sp, #4]
	mov	r1, fp
	bl	printk
.L3456:
	bl	FtlGcReFreshBadBlk
	b	.L3444
.L3585:
	mov	fp, r3
	b	.L3444
.L3580:
	add	sp, sp, #44
	@ sp needed
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L3593:
	.align	2
.L3592:
	.word	.LANCHOR2
	.word	.LANCHOR1
	.word	.LANCHOR2-2656
	.word	.LANCHOR2-3280
	.word	.LANCHOR0
	.word	.LANCHOR4
	.word	.LANCHOR0+2320
	.word	.LC157
	.word	.LANCHOR2-3520
	.word	.LANCHOR2-3296
	.word	.LANCHOR2-3472
	.word	.LANCHOR2-3284
	.word	.LANCHOR2-2712
	.word	.LANCHOR0+2388
	.word	.LANCHOR4+1994
	.word	.LANCHOR0+2390
	.word	.LANCHOR2-3428
	.word	.LANCHOR2-3528
	.word	.LC158
	.word	.LANCHOR2-2720
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
	ldr	r3, .L3638
	ldr	r7, .L3638+4
	ldr	r5, [r3, #2000]
	ldr	r3, [r7, #-3616]
	cmp	r3, #0
	bne	.L3596
	ldr	r4, .L3638+8
	ldr	r1, [r4, #2440]
	cmp	r1, #0
	beq	.L3596
	ldrb	r6, [r4, #148]	@ zero_extendqisi2
	mov	r8, #0
	ldr	r0, [r4, #2444]
	mov	r10, #36
	cmp	r6, #0
	ldrb	r3, [r5, #9]	@ zero_extendqisi2
	ldr	r9, .L3638+8
	ldrneb	r6, [r5, #8]	@ zero_extendqisi2
	subne	r6, r6, #1
	clzne	r6, r6
	movne	r6, r6, lsr #5
	mov	r2, r6
	bl	FlashProgPages
.L3599:
	ldr	r3, [r4, #2440]
	cmp	r8, r3
	bcs	.L3617
	mul	fp, r10, r8
	ldr	r3, [r9, #2444]
	add	r2, r3, fp
	ldr	r3, [r3, fp]
	cmn	r3, #1
	beq	.L3620
	ldr	r3, [r2, #4]
	cmp	r6, #0
	ldr	r0, [r2, #16]
	add	r1, sp, #4
	mov	r2, #1
	orrne	r3, r3, #-2147483648
	str	r3, [sp, #4]
	bl	log2phys
	ldr	r3, [r4, #2444]
	add	r3, r3, fp
	ldr	r3, [r3, #12]
	ldr	r0, [r3, #12]
	cmn	r0, #1
	beq	.L3603
	ubfx	r0, r0, #10, #16
	bl	P2V_block_in_plane
	ldr	r2, [r7, #-3544]
	mov	r3, r0, asl #1
	mov	fp, r0
	ldrh	r2, [r2, r3]
	cmp	r2, #0
	bne	.L3604
	ldr	r0, .L3638+12
	mov	r1, fp
	bl	printk
.L3604:
	mov	r0, fp
	bl	decrement_vpc_count
.L3603:
	add	r8, r8, #1
	b	.L3599
.L3636:
	ldr	r6, .L3638+16
	movw	r5, #16386
.L3616:
	ldrh	r3, [r6]
	cmp	r3, #0
	beq	.L3617
	mov	r0, #1
	mov	r1, r0
	bl	ftl_do_gc
	subs	r5, r5, #1
	bne	.L3616
.L3617:
	mov	r3, #0
	str	r3, [r4, #2440]
	b	.L3596
.L3620:
	ldr	fp, .L3638+8
	mov	r9, #0
.L3600:
	ldr	r3, [r4, #2440]
	cmp	r9, r3
	bcs	.L3636
	mov	r8, #36
	ldr	r3, [fp, #2444]
	mul	r8, r8, r9
	mov	r10, #0
	mvn	r2, #0
	str	r2, [r3, r8]
.L3606:
	ldr	r3, [r4, #2444]
	add	r2, r3, r8
	ldr	r3, [r3, r8]
	cmn	r3, #1
	bne	.L3637
	ldr	r0, [r2, #4]
	ubfx	r0, r0, #10, #16
	bl	P2V_block_in_plane
	ldrh	r3, [r5]
	cmp	r3, r0
	bne	.L3607
	ldr	r1, [r7, #-3544]
	mov	r3, r3, asl #1
	ldrh	r0, [r5, #4]
	ldrh	r2, [r1, r3]
	rsb	r2, r0, r2
	strh	r2, [r1, r3]	@ movhi
	ldr	r3, .L3638+20
	strb	r10, [r5, #6]
	strh	r10, [r5, #4]	@ movhi
	ldrh	r3, [r3]
	strh	r3, [r5, #2]	@ movhi
.L3607:
	ldrh	r3, [r5, #4]
	cmp	r3, #0
	bne	.L3608
	mov	r0, r5
	bl	allocate_new_data_superblock
.L3608:
	ldr	r3, [r7, #-3136]
	add	r3, r3, #1
	str	r3, [r7, #-3136]
	ldr	r3, [r4, #2444]
	add	r3, r3, r8
	ldr	r0, [r3, #4]
	ubfx	r0, r0, #10, #16
	bl	FtlGcMarkBadPhyBlk
	mov	r0, r5
	bl	get_new_active_ppa
	ldr	r3, [r4, #2444]
	mov	r2, r6
	mov	r1, #1
	add	r3, r3, r8
	str	r0, [sp, #4]
	str	r0, [r3, #4]
	mov	r0, r3
	ldrb	r3, [r5, #9]	@ zero_extendqisi2
	bl	FlashProgPages
	ldr	r3, [r4, #2444]
	ldr	r3, [r3, r8]
	cmn	r3, #1
	ldreq	r2, .L3638+4
	moveq	r3, #1
	streq	r3, [r2, #-3616]
	ldr	r3, [r7, #-3616]
	cmp	r3, #0
	beq	.L3606
	b	.L3596
.L3637:
	ldr	r3, [r2, #4]
	cmp	r6, #0
	ldr	r0, [r2, #16]
	add	r1, sp, #4
	mov	r2, #1
	orrne	r3, r3, #-2147483648
	str	r3, [sp, #4]
	bl	log2phys
	ldr	r3, [r4, #2444]
	add	r8, r3, r8
	ldr	r3, [r8, #12]
	ldr	r0, [r3, #12]
	cmn	r0, #1
	beq	.L3613
	ubfx	r0, r0, #10, #16
	bl	P2V_block_in_plane
	ldr	r2, [r7, #-3544]
	mov	r3, r0, asl #1
	mov	r8, r0
	ldrh	r2, [r2, r3]
	cmp	r2, #0
	bne	.L3614
	ldr	r0, .L3638+12
	mov	r1, r8
	bl	printk
.L3614:
	mov	r0, r8
	bl	decrement_vpc_count
.L3613:
	add	r9, r9, #1
	b	.L3600
.L3596:
	mov	r0, #0
	add	sp, sp, #12
	@ sp needed
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L3639:
	.align	2
.L3638:
	.word	.LANCHOR4
	.word	.LANCHOR2
	.word	.LANCHOR0
	.word	.LC159
	.word	.LANCHOR2-2658
	.word	.LANCHOR0+2388
	.fnend
	.size	FtlCacheWriteBack, .-FtlCacheWriteBack
	.align	2
	.global	FtlSysFlush
	.type	FtlSysFlush, %function
FtlSysFlush:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	ldr	r3, .L3644
	ldr	r3, [r3, #-3616]
	cmp	r3, #0
	bne	.L3643
	ldr	r3, .L3644+4
	stmfd	sp!, {r4, lr}
	.save {r4, lr}
	ldr	r4, [r3, #504]
	cmp	r4, #1
	bne	.L3641
	bl	FtlCacheWriteBack
	bl	l2p_flush
	mov	r0, r4
	bl	FtlEctTblFlush
	bl	FtlVpcTblFlush
.L3641:
	mov	r0, #0
	ldmfd	sp!, {r4, pc}
.L3643:
	mov	r0, #0
	bx	lr
.L3645:
	.align	2
.L3644:
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
	ldr	r3, .L3649
	ldr	r3, [r3, #504]
	cmp	r3, #1
	bne	.L3647
	bl	FtlSysFlush
.L3647:
	mov	r0, #0
	ldmfd	sp!, {r3, pc}
.L3650:
	.align	2
.L3649:
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
	.global	rk_ftl_de_init
	.type	rk_ftl_de_init, %function
rk_ftl_de_init:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	stmfd	sp!, {r3, lr}
	.save {r3, lr}
	mov	r1, #0
	ldr	r0, .L3655
	bl	printk
	ldmfd	sp!, {r3, lr}
	b	ftl_deinit
.L3656:
	.align	2
.L3655:
	.word	.LC160
	.fnend
	.size	rk_ftl_de_init, .-rk_ftl_de_init
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
	.global	rk_ftl_cache_write_back
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
	.type	ftl_discard, %function
ftl_discard:
	.fnstart
	@ args = 0, pretend = 0, frame = 8
	@ frame_needed = 0, uses_anonymous_args = 0
	stmfd	sp!, {r0, r1, r2, r4, r5, r6, r7, r8, r9, lr}
	.save {r4, r5, r6, r7, r8, r9, lr}
	.pad #12
	mov	r6, r0
	ldr	r8, .L3678
	mov	r4, r1
	ldr	r3, [r8, #2428]
	cmp	r1, r3
	cmpls	r0, r3
	movcs	r5, #1
	movcc	r5, #0
	bcs	.L3667
	add	r2, r0, r1
	cmp	r2, r3
	bhi	.L3667
	cmp	r1, #31
	bls	.L3665
	ldr	r7, .L3678+4
	ldr	r3, [r7, #-3616]
	cmp	r3, #0
	movne	r0, r5
	bne	.L3660
	bl	FtlCacheWriteBack
	movw	r3, #2394
	ldrh	r5, [r8, r3]
	mov	r0, r6
	mov	r1, r5
	bl	__aeabi_uidiv
	smulbb	r3, r0, r5
	mov	r8, r0
	rsb	r6, r3, r6
	uxth	r6, r6
	cmp	r6, #0
	beq	.L3661
	rsb	r5, r6, r5
	add	r8, r0, #1
	cmp	r5, r4
	movcs	r5, r4
	uxth	r5, r5
	rsb	r4, r5, r4
.L3661:
	ldr	r5, .L3678+8
	mvn	r3, #0
	ldr	r9, .L3678+12
	str	r3, [sp, #4]
	mov	r6, r5
.L3662:
	ldrh	r3, [r5]
	cmp	r4, r3
	bcc	.L3677
	mov	r0, r8
	mov	r1, sp
	mov	r2, #0
	bl	log2phys
	ldr	r3, [sp]
	cmn	r3, #1
	beq	.L3663
	ldr	r3, [r9, #2004]
	add	r1, sp, #4
	mov	r2, #1
	mov	r0, r8
	add	r3, r3, #1
	str	r3, [r9, #2004]
	ldr	r3, [r7, #-3360]
	add	r3, r3, #1
	str	r3, [r7, #-3360]
	bl	log2phys
	ldr	r0, [sp]
	ubfx	r0, r0, #10, #16
	bl	P2V_block_in_plane
	bl	decrement_vpc_count
.L3663:
	ldrh	r3, [r6]
	add	r8, r8, #1
	rsb	r4, r3, r4
	b	.L3662
.L3677:
	ldr	r3, .L3678+12
	ldr	r2, [r3, #2004]
	cmp	r2, #32
	bls	.L3665
	mov	r2, #0
	str	r2, [r3, #2004]
	bl	l2p_flush
	bl	FtlVpcTblFlush
.L3665:
	mov	r0, #0
	b	.L3660
.L3667:
	mvn	r0, #0
.L3660:
	add	sp, sp, #12
	@ sp needed
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, pc}
.L3679:
	.align	2
.L3678:
	.word	.LANCHOR0
	.word	.LANCHOR2
	.word	.LANCHOR0+2394
	.word	.LANCHOR4
	.fnend
	.size	ftl_discard, .-ftl_discard
	.align	2
	.global	FtlDiscard
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
	.type	ftl_read, %function
ftl_read:
	.fnstart
	@ args = 0, pretend = 0, frame = 56
	@ frame_needed = 0, uses_anonymous_args = 0
	stmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.save {r4, r5, r6, r7, r8, r9, r10, fp, lr}
	mov	r5, r1
	ldr	r1, .L3729
	.pad #84
	sub	sp, sp, #84
	ldr	r1, [r1, #504]
	cmp	r1, #1
	bne	.L3706
	cmp	r0, #16
	mov	r8, r3
	mov	r9, r2
	bne	.L3683
	mov	r1, r2
	add	r0, r5, #256
	mov	r2, r3
	bl	FtlVendorPartRead
	b	.L3682
.L3683:
	ldr	r2, .L3729+4
	ldr	r3, [r2, #2428]
	cmp	r9, r3
	cmpls	r5, r3
	bcs	.L3706
	add	r1, r5, r9
	str	r1, [sp, #40]
	cmp	r1, r3
	bhi	.L3706
	movw	r3, #2394
	mov	r0, r5
	ldrh	r4, [r2, r3]
	mov	r1, r4
	bl	__aeabi_uidiv
	mov	r1, r4
	ldr	r3, [sp, #40]
	str	r0, [sp, #32]
	sub	r0, r3, #1
	bl	__aeabi_uidiv
	ldr	r3, [sp, #32]
	str	r0, [sp, #36]
	rsb	r3, r3, #1
	add	r3, r3, r0
	str	r3, [sp, #28]
	ldr	r3, .L3729+8
	ldr	r1, [sp, #28]
	ldr	r0, [sp, #32]
	ldr	r2, [r3, #-3336]
	add	r2, r9, r2
	str	r2, [r3, #-3336]
	ldr	r2, [r3, #-3364]
	add	r2, r1, r2
	ldr	r1, [sp, #36]
	str	r2, [r3, #-3364]
	bl	FtlCacheMetchLpa
	cmp	r0, #0
	beq	.L3684
	bl	FtlCacheWriteBack
.L3684:
	ldr	r6, [sp, #32]
	mov	r10, #0
	ldr	r4, .L3729+8
	mov	r7, r10
	str	r10, [sp, #48]
	str	r10, [sp, #52]
.L3685:
	ldr	r3, [sp, #28]
	cmp	r3, #0
	beq	.L3728
	mov	r0, r6
	add	r1, sp, #76
	mov	r2, #0
	bl	log2phys
	ldr	r3, [sp, #76]
	cmn	r3, #1
	bne	.L3724
	mov	fp, #0
.L3686:
	ldr	r3, .L3729+12
	ldrh	r0, [r3]
	cmp	fp, r0
	bcs	.L3690
	mla	r0, r0, r6, fp
	ldr	r2, [sp, #40]
	cmp	r0, r5
	movcs	r3, #1
	movcc	r3, #0
	cmp	r0, r2
	movcs	r3, #0
	cmp	r3, #0
	beq	.L3688
	rsb	r0, r5, r0
	mov	r1, #0
	mov	r2, #512
	add	r0, r8, r0, asl #9
	bl	ftl_memset
.L3688:
	add	fp, fp, #1
	b	.L3686
.L3724:
	ldr	r2, [r4, #-536]
	mov	fp, #36
	mla	fp, fp, r7, r2
	str	r3, [fp, #4]
	ldr	r3, [sp, #32]
	cmp	r6, r3
	bne	.L3691
	ldr	r3, [r4, #-508]
	mov	r0, r5
	str	r3, [fp, #8]
	ldr	r3, .L3729+12
	ldrh	ip, [r3]
	mov	r1, ip
	str	ip, [sp, #44]
	bl	__aeabi_uidivmod
	ldr	ip, [sp, #44]
	str	r1, [sp, #56]
	rsb	r3, r1, ip
	cmp	r3, r9
	movcs	r3, r9
	cmp	r3, ip
	str	r3, [sp, #48]
	streq	r8, [fp, #8]
	b	.L3692
.L3691:
	ldr	r3, [sp, #36]
	cmp	r6, r3
	bne	.L3693
	ldr	r3, [r4, #-504]
	ldr	r1, [sp, #40]
	str	r3, [fp, #8]
	ldr	r3, .L3729+12
	ldrh	r2, [r3]
	mul	r3, r2, r6
	rsb	r10, r3, r1
	cmp	r10, r2
	bne	.L3692
	b	.L3726
.L3693:
	ldr	r3, .L3729+12
	ldrh	r3, [r3]
	mul	r3, r3, r6
.L3726:
	rsb	r3, r5, r3
	add	r3, r8, r3, asl #9
	str	r3, [fp, #8]
.L3692:
	ldr	r3, .L3729+16
	ldr	r2, [r4, #-496]
	str	r6, [fp, #16]
	ldrh	r3, [r3]
	mul	r3, r3, r7
	add	r7, r7, #1
	bic	r3, r3, #3
	add	r3, r2, r3
	str	r3, [fp, #12]
.L3690:
	ldr	r3, [sp, #28]
	add	r6, r6, #1
	subs	r3, r3, #1
	str	r3, [sp, #28]
	beq	.L3694
	ldr	r3, .L3729+20
	ldrh	r3, [r3]
	cmp	r7, r3, asl #3
	bne	.L3685
.L3694:
	cmp	r7, #0
	beq	.L3685
	ldr	r0, [r4, #-536]
	mov	r1, r7
	mov	r2, #0
	bl	FlashReadPages
	ldr	r3, [sp, #56]
	mov	r3, r3, asl #9
	str	r3, [sp, #60]
	ldr	r3, [sp, #48]
	mov	r3, r3, asl #9
	str	r3, [sp, #64]
	mov	r3, r10, asl #9
	str	r3, [sp, #68]
	mov	r3, #0
	str	r3, [sp, #44]
.L3701:
	ldr	r3, [sp, #44]
	mov	ip, #36
	ldr	r1, [sp, #32]
	mul	fp, ip, r3
	ldr	r3, [r4, #-536]
	add	r3, r3, fp
	ldr	r2, [r3, #16]
	cmp	r2, r1
	bne	.L3696
	ldr	r1, [r3, #8]
	ldr	r3, [r4, #-508]
	cmp	r1, r3
	bne	.L3697
	ldr	r3, [sp, #60]
	mov	r0, r8
	ldr	r2, [sp, #64]
	add	r1, r1, r3
	b	.L3727
.L3696:
	ldr	r1, [sp, #36]
	cmp	r2, r1
	bne	.L3697
	ldr	r1, [r3, #8]
	ldr	r3, [r4, #-504]
	cmp	r1, r3
	bne	.L3697
	ldr	r3, .L3729+12
	ldr	r2, [sp, #68]
	ldrh	r0, [r3]
	ldr	r3, [sp, #36]
	mul	r0, r0, r3
	rsb	r0, r5, r0
	add	r0, r8, r0, asl #9
.L3727:
	bl	ftl_memcpy
.L3697:
	ldr	r2, [r4, #-536]
	add	r3, r2, fp
	ldr	r1, [r2, fp]
	cmn	r1, #1
	streq	r1, [sp, #52]
	ldreq	r2, [r4, #-3160]
	addeq	r2, r2, #1
	streq	r2, [r4, #-3160]
	ldr	r2, [r3, #12]
	ldr	r1, [r3, #16]
	ldr	r2, [r2, #8]
	cmp	r1, r2
	beq	.L3699
	ldr	r2, [r4, #-3160]
	ldr	r0, .L3729+24
	add	r2, r2, #1
	str	r2, [r4, #-3160]
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
.L3699:
	ldr	r3, [r4, #-536]
	add	r2, r3, fp
	ldr	r3, [r3, fp]
	cmp	r3, #256
	bne	.L3700
	ldr	r0, [r2, #4]
	ubfx	r0, r0, #10, #16
	bl	P2V_block_in_plane
	bl	FtlGcRefreshBlock
.L3700:
	ldr	r3, [sp, #44]
	add	r3, r3, #1
	str	r3, [sp, #44]
	cmp	r3, r7
	bne	.L3701
	mov	r7, #0
	b	.L3685
.L3728:
	ldr	r3, .L3729+28
	ldrh	r3, [r3, #-2]
	cmp	r3, #0
	beq	.L3703
	ldr	r0, [sp, #28]
	mov	r1, #1
	bl	ftl_do_gc
.L3703:
	ldr	r0, [sp, #52]
	b	.L3682
.L3706:
	mvn	r0, #0
.L3682:
	add	sp, sp, #84
	@ sp needed
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L3730:
	.align	2
.L3729:
	.word	.LANCHOR1
	.word	.LANCHOR0
	.word	.LANCHOR2
	.word	.LANCHOR0+2394
	.word	.LANCHOR0+2400
	.word	.LANCHOR0+2320
	.word	.LC148
	.word	.LANCHOR2-2656
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
	.global	FlashBootVendorRead
	.type	FlashBootVendorRead, %function
FlashBootVendorRead:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	stmfd	sp!, {r4, r5, r6, lr}
	.save {r4, r5, r6, lr}
	mov	r4, r2
	mov	r6, r0
	mov	r5, r1
	bl	rknand_device_lock
	ldr	r3, .L3737
	ldr	r3, [r3, #504]
	cmp	r3, #1
	mvnne	r4, #0
	bne	.L3734
	mov	r2, r4
	mov	r0, r6
	mov	r1, r5
	bl	ftl_vendor_read
	mov	r4, r0
.L3734:
	bl	rknand_device_unlock
	mov	r0, r4
	ldmfd	sp!, {r4, r5, r6, pc}
.L3738:
	.align	2
.L3737:
	.word	.LANCHOR1
	.fnend
	.size	FlashBootVendorRead, .-FlashBootVendorRead
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
	.global	StorageSysDataLoad
	.type	StorageSysDataLoad, %function
StorageSysDataLoad:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	stmfd	sp!, {r3, r4, r5, lr}
	.save {r3, r4, r5, lr}
	mov	r2, #512
	mov	r5, r0
	mov	r4, r1
	mov	r0, r1
	mov	r1, #0
	bl	ftl_memset
	bl	rknand_device_lock
	mov	r2, r4
	mov	r1, #1
	mov	r0, r5
	bl	ftl_sys_read
	mov	r4, r0
	bl	rknand_device_unlock
	mov	r0, r4
	ldmfd	sp!, {r3, r4, r5, pc}
	.fnend
	.size	StorageSysDataLoad, .-StorageSysDataLoad
	.align	2
	.global	FtlRead
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
	.type	FtlInit, %function
FtlInit:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	stmfd	sp!, {r4, r5, r6, r7, r8, lr}
	.save {r4, r5, r6, r7, r8, lr}
	mvn	r3, #0
	ldr	r2, .L3760
	ldr	r7, .L3760+4
	ldr	r5, .L3760+8
	ldr	r6, .L3760+12
	ldr	r1, .L3760+16
	ldr	r0, .L3760+20
	str	r3, [r7, #504]
	mov	r3, #0
	str	r3, [r2, #2008]
	str	r3, [r5, #-3616]
	bl	printk
	add	r0, r6, #120
	bl	FtlConstantsInit
	bl	FtlMemInit
	bl	FtlVariablesInit
	ldr	r3, [r6, #2324]
	uxth	r0, r3
	bl	FtlFreeSysBlkQueueInit
	bl	FtlLoadBbt
	cmp	r0, #0
	ldrne	r0, .L3760+24
	bne	.L3759
	bl	FtlSysBlkInit
	subs	r4, r0, #0
	beq	.L3746
	ldr	r0, .L3760+28
.L3759:
	ldr	r1, .L3760+32
	bl	printk
	b	.L3745
.L3746:
	sub	r5, r5, #3520
	mov	r1, #1
	str	r1, [r7, #504]
	bl	ftl_do_gc
	ldrh	r5, [r5, #-8]
	cmp	r5, #15
	bhi	.L3747
	ldr	r6, .L3760+36
	ldr	r7, .L3760+40
	sub	r8, r6, #244
.L3750:
	ldrh	r3, [r6]
	movw	r2, #65535
	cmp	r3, r2
	bne	.L3748
	ldrh	r2, [r7]
	cmp	r2, r3
	bne	.L3748
	and	r0, r4, #63
	bl	List_get_gc_head_node
	uxth	r0, r0
	bl	FtlGcRefreshBlock
.L3748:
	mov	r0, #1
	mov	r1, r0
	bl	ftl_do_gc
	mov	r0, #0
	mov	r1, #1
	bl	ftl_do_gc
	ldrh	r2, [r8]
	add	r3, r5, #2
	cmp	r2, r3
	bhi	.L3745
	add	r4, r4, #1
	cmp	r4, #4096
	bne	.L3750
	b	.L3745
.L3747:
	ldrb	r3, [r6, #148]	@ zero_extendqisi2
	cmp	r3, #0
	beq	.L3745
	mov	r4, #128
.L3752:
	mov	r0, #1
	mov	r1, r0
	bl	ftl_do_gc
	subs	r4, r4, #1
	bne	.L3752
.L3745:
	mov	r0, #0
	ldmfd	sp!, {r4, r5, r6, r7, r8, pc}
.L3761:
	.align	2
.L3760:
	.word	.LANCHOR4
	.word	.LANCHOR1
	.word	.LANCHOR2
	.word	.LANCHOR0
	.word	.LC77
	.word	.LC76
	.word	.LC161
	.word	.LC162
	.word	.LANCHOR3+240
	.word	.LANCHOR2-3284
	.word	.LANCHOR2-2666
	.fnend
	.size	FtlInit, .-FtlInit
	.align	2
	.global	rk_ftl_init
	.type	rk_ftl_init, %function
rk_ftl_init:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	stmfd	sp!, {r4, r5, r6, lr}
	.save {r4, r5, r6, lr}
	mov	r0, #2048
	bl	ftl_dma32_malloc
	ldr	r6, .L3767
	ldr	r4, .L3767+4
	mov	r5, #0
	mov	r1, r6
	str	r5, [r1, #2016]!
	str	r5, [r4, #1688]
	str	r5, [r4, #1684]
	str	r0, [r6, #2012]
	ldr	r0, .L3767+8
	bl	rknand_get_reg_addr
	ldr	r3, [r4, #1684]
	cmp	r3, r5
	beq	.L3765
	bl	rk_nandc_irq_init
	mov	r1, r5
	mov	r2, r5
	mov	r3, #2048
	ldr	r0, [r6, #2012]
	bl	FlashSramLoadStore
	bl	rknand_flash_cs_init
	ldr	r0, [r4, #1684]
	bl	FlashInit
	subs	r4, r0, #0
	bne	.L3764
	bl	FtlInit
.L3764:
	mov	r1, r4
	ldr	r0, .L3767+12
	bl	printk
	mov	r0, r4
	ldmfd	sp!, {r4, r5, r6, pc}
.L3765:
	mvn	r0, #0
	ldmfd	sp!, {r4, r5, r6, pc}
.L3768:
	.align	2
.L3767:
	.word	.LANCHOR4
	.word	.LANCHOR2
	.word	.LANCHOR2+1684
	.word	.LC163
	.fnend
	.size	rk_ftl_init, .-rk_ftl_init
	.align	2
	.global	ftl_fix_nand_power_lost_error
	.type	ftl_fix_nand_power_lost_error, %function
ftl_fix_nand_power_lost_error:
	.fnstart
	@ args = 0, pretend = 0, frame = 48
	@ frame_needed = 0, uses_anonymous_args = 0
	ldr	r3, .L3786
	ldrb	r3, [r3, #148]	@ zero_extendqisi2
	cmp	r3, #0
	bxeq	lr
	stmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.save {r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.pad #52
	sub	sp, sp, #52
	ldr	r8, .L3786+4
	movw	r5, #4097
	ldr	r4, .L3786+8
	ldr	r0, .L3786+12
	ldrh	r7, [r8]
	ldr	r3, [r4, #-3544]
	mov	r6, r7, asl #1
	mov	r1, r7
	ldrh	r2, [r3, r6]
	bl	printk
	sub	r3, r4, #3520
	ldrh	r0, [r3, #-4]
	bl	FtlGcRefreshOpenBlock
	sub	r3, r4, #3472
	ldrh	r0, [r3, #-4]
	bl	FtlGcRefreshOpenBlock
	ldr	r0, .L3786+16
	bl	allocate_new_data_superblock
	ldr	r0, .L3786+20
	bl	allocate_new_data_superblock
.L3771:
	subs	r5, r5, #1
	beq	.L3775
	mov	r0, #1
	mov	r1, r0
	bl	ftl_do_gc
	ldr	r3, [r4, #-3544]
	ldrh	r3, [r3, r6]
	cmp	r3, #0
	bne	.L3771
.L3775:
	ldr	r3, [r4, #-3544]
	mov	r1, r7
	ldr	r0, .L3786+12
	ldrh	r2, [r3, r6]
	bl	printk
	ldr	r3, [r4, #-3544]
	ldrh	r5, [r3, r6]
	cmp	r5, #0
	bne	.L3773
	add	r0, sp, #48
	movw	r10, #65535
	mov	fp, #36
	strh	r7, [r0, #-48]!	@ movhi
	bl	make_superblock
	ldr	r3, .L3786+24
	ldrh	lr, [r3]
	ldr	r3, .L3786+8
	ldr	r9, [r3, #-3612]
	mov	r3, r5
	mov	ip, r3
	add	r0, sp, #14
.L3776:
	uxth	r2, r3
	cmp	r2, lr
	bcs	.L3785
	ldrh	r2, [r0, #2]!
	add	r3, r3, #1
	cmp	r2, r10
	movne	r2, r2, asl #10
	mlane	r1, fp, r5, r9
	addne	r5, r5, #1
	uxthne	r5, r5
	stmneib	r1, {r2, ip}
	strne	ip, [r1, #12]
	b	.L3776
.L3785:
	ldr	r3, [r4, #-3544]
	mov	r1, r7
	ldr	r0, .L3786+28
	ldrh	r2, [r3, r6]
	bl	printk
	mov	r1, #0
	mov	r2, r5
	ldr	r0, [r4, #-3612]
	bl	FlashEraseBlocks
	ldr	r0, [r4, #-3612]
	mov	r1, #1
	mov	r2, r5
	bl	FlashEraseBlocks
.L3773:
	mvn	r3, #0
	strh	r3, [r8]	@ movhi
	add	sp, sp, #52
	@ sp needed
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L3787:
	.align	2
.L3786:
	.word	.LANCHOR0
	.word	.LANCHOR4+1856
	.word	.LANCHOR2
	.word	.LC164
	.word	.LANCHOR2-3524
	.word	.LANCHOR2-3476
	.word	.LANCHOR0+2320
	.word	.LC165
	.fnend
	.size	ftl_fix_nand_power_lost_error, .-ftl_fix_nand_power_lost_error
	.align	2
	.global	rk_ftl_garbage_collect
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
	.type	ftl_write, %function
ftl_write:
	.fnstart
	@ args = 0, pretend = 0, frame = 96
	@ frame_needed = 0, uses_anonymous_args = 0
	stmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.save {r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.pad #100
	sub	sp, sp, #100
	ldr	r10, .L3863
	str	r3, [sp, #4]
	ldr	r3, [r10, #-3616]
	cmp	r3, #0
	bne	.L3830
	mov	r8, r2
	ldr	r2, .L3863+4
	ldr	r2, [r2, #504]
	cmp	r2, #1
	movne	r0, r3
	bne	.L3790
	cmp	r0, #16
	mov	r7, r1
	bne	.L3791
	add	r0, r1, #256
	ldr	r2, [sp, #4]
	mov	r1, r8
	bl	FtlVendorPartWrite
	b	.L3790
.L3791:
	ldr	fp, .L3863+8
	ldr	r3, [fp, #2428]
	cmp	r8, r3
	cmpls	r1, r3
	bcs	.L3833
	add	r5, r1, r8
	cmp	r5, r3
	bhi	.L3833
	ldr	r6, .L3863+12
	mov	r3, #2048
	mov	r0, r1
	str	r3, [r6, #2020]
	movw	r3, #2394
	ldrh	r4, [fp, r3]
	mov	r1, r4
	bl	__aeabi_uidiv
	mov	r1, r4
	str	r0, [sp, #8]
	sub	r0, r5, #1
	bl	__aeabi_uidiv
	cmp	r8, r4, asl #1
	ldr	r2, [sp, #8]
	str	r0, [sp, #28]
	rsb	r5, r2, r0
	add	r3, r5, #1
	str	r3, [sp]
	ldr	r2, [sp]
	ldr	r3, [r10, #-3356]
	add	r3, r2, r3
	ldr	r2, [fp, #2440]
	str	r3, [r10, #-3356]
	ldr	r3, [r10, #-3340]
	add	r3, r8, r3
	str	r3, [r10, #-3340]
	movcs	r3, #1
	movcc	r3, #0
	cmp	r2, #0
	str	r3, [sp, #20]
	beq	.L3793
	mov	r3, #36
	ldr	r9, [fp, #2444]
	mul	r3, r3, r2
	ldr	r2, [sp, #8]
	sub	r3, r3, #36
	add	r9, r9, r3
	ldr	r3, [r9, #16]
	cmp	r2, r3
	bne	.L3794
	ldr	r3, [r10, #-3352]
	mov	r1, r4
	mov	r0, r7
	add	r3, r3, #1
	str	r3, [r10, #-3352]
	ldr	r3, [r6, #2024]
	add	r3, r3, #1
	str	r3, [r6, #2024]
	bl	__aeabi_uidivmod
	ldr	r0, [r9, #8]
	rsb	r4, r1, r4
	add	r0, r0, r1, asl #9
	cmp	r4, r8
	ldr	r1, [sp, #4]
	movcs	r4, r8
	mov	r10, r4, asl #9
	mov	r2, r10
	bl	ftl_memcpy
	cmp	r5, #0
	bne	.L3795
	ldr	r3, [r6, #2024]
	cmp	r3, #2
	ble	.L3830
.L3795:
	ldr	r3, [sp, #4]
	rsb	r8, r4, r8
	add	r7, r7, r4
	str	r5, [sp]
	add	r3, r3, r10
	str	r3, [sp, #4]
	ldr	r3, [sp, #8]
	add	r3, r3, #1
	str	r3, [sp, #8]
.L3794:
	mov	r3, #0
	str	r3, [r6, #2024]
.L3793:
	ldr	r0, [sp, #8]
	ldr	r1, [sp, #28]
	bl	FtlCacheMetchLpa
	cmp	r0, #0
	beq	.L3796
	bl	FtlCacheWriteBack
.L3796:
	ldr	r5, .L3863+16
	mov	r3, #0
	ldr	r4, .L3863+8
	str	r3, [sp, #12]
	str	r5, [r6, #2000]
	ldr	r6, [sp, #8]
	mov	r10, r4
	str	r3, [sp, #32]
.L3797:
	ldr	r3, [sp]
	cmp	r3, #0
	beq	.L3862
	ldrh	r2, [r5, #4]
	cmp	r2, #0
	bne	.L3798
	ldr	r3, .L3863+16
	ldr	r9, .L3863+4
	cmp	r5, r3
	bne	.L3799
	add	r0, r3, #48
	ldrh	r5, [r0, #4]
	cmp	r5, #0
	bne	.L3800
	bl	allocate_new_data_superblock
	str	r5, [r9, #3452]
.L3800:
	ldr	r0, .L3863+16
	bl	allocate_new_data_superblock
	ldr	r3, [r9, #3452]
	cmp	r3, #0
	ldrne	r5, .L3863+20
	bne	.L3801
.L3802:
	ldr	r5, .L3863+16
	b	.L3801
.L3799:
	ldrh	r3, [r3, #4]
	str	r2, [r9, #3452]
	cmp	r3, #0
	bne	.L3802
	mov	r0, r5
	bl	allocate_new_data_superblock
.L3801:
	ldrh	r3, [r5, #4]
	cmp	r3, #0
	bne	.L3803
	mov	r0, r5
	bl	allocate_new_data_superblock
.L3803:
	ldr	r3, .L3863+12
	str	r5, [r3, #2000]
.L3798:
	ldr	r2, .L3863
	ldr	r1, [r4, #2440]
	ldrh	r3, [r5, #4]
	ldr	r2, [r2, #-540]
	rsb	r2, r1, r2
	cmp	r3, r2
	movcs	r3, r2
	ldr	r2, [sp]
	cmp	r3, r2
	movcs	r3, r2
	str	r3, [sp, #44]
	mov	r3, #0
.L3860:
	str	r3, [sp, #16]
	ldr	r3, [sp, #16]
	ldr	r2, [sp, #44]
	cmp	r3, r2
	beq	.L3805
	ldrh	r3, [r5, #4]
	cmp	r3, #0
	beq	.L3805
	ldr	r3, [sp, #28]
	ldr	r2, [sp, #16]
	rsb	ip, r3, r6
	ldr	r3, [sp, #20]
	clz	ip, ip
	mov	ip, ip, lsr #5
	and	r3, ip, r3
	cmp	r2, #0
	moveq	r3, #0
	andne	r3, r3, #1
	cmp	r3, #0
	beq	.L3806
	ldr	r3, .L3863+24
	ldrh	r2, [r3]
	add	r3, r8, r7
	mls	r3, r2, r6, r3
	cmp	r3, r2
	bne	.L3805
.L3806:
	add	r1, sp, #56
	mov	r2, #0
	mov	r0, r6
	str	ip, [sp, #52]
	bl	log2phys
	mov	r0, r5
	bl	get_new_active_ppa
	ldr	r2, [r4, #2440]
	ldr	r1, [r4, #2444]
	mov	r3, #36
	ldr	fp, .L3863+28
	mla	r1, r3, r2, r1
	ldrh	r2, [fp]
	str	r6, [r1, #16]
	str	r3, [sp, #48]
	ldr	r3, [r4, #2440]
	str	r0, [r1, #4]
	ldr	r0, .L3863
	mul	lr, r3, r2
	bic	r3, lr, #3
	str	r3, [sp, #36]
	ldr	r3, [r0, #-492]
	ldr	ip, [sp, #36]
	ldrh	lr, [fp, #-2]
	add	r9, r3, ip
	str	r3, [sp, #40]
	ldr	r3, [r4, #2440]
	ldr	r0, [r0, #-512]
	str	r9, [r1, #12]
	mul	lr, r3, lr
	bic	lr, lr, #3
	add	lr, r0, lr
	mov	r0, r9
	str	lr, [r1, #8]
	mov	r1, #0
	bl	ftl_memset
	ldr	r3, [sp, #8]
	ldr	ip, [sp, #52]
	rsb	r3, r3, r6
	clz	r3, r3
	mov	r3, r3, lsr #5
	str	r3, [sp, #24]
	orrs	r3, r3, ip
	ldr	r3, [sp, #48]
	beq	.L3807
	ldr	r3, [sp, #24]
	cmp	r3, #0
	beq	.L3808
	ldrh	fp, [fp, #-6]
	mov	r0, r7
	mov	r1, fp
	bl	__aeabi_uidivmod
	rsb	fp, r1, fp
	mov	r3, r1
	cmp	fp, r8
	str	r1, [sp, #32]
	movcc	r3, fp
	movcs	r3, r8
	str	r3, [sp, #12]
	b	.L3809
.L3808:
	cmp	ip, #0
	beq	.L3809
	ldr	r2, .L3863+24
	add	r3, r8, r7
	ldrh	r2, [r2]
	smulbb	r2, r2, r6
	rsb	r3, r2, r3
	uxth	r3, r3
	str	r3, [sp, #12]
	ldr	r3, [sp, #24]
	str	r3, [sp, #32]
.L3809:
	ldr	r3, .L3863+24
	ldr	r2, [sp, #12]
	ldrh	r3, [r3]
	cmp	r2, r3
	bne	.L3810
	ldr	r3, [sp, #24]
	ldr	r0, [r10, #2444]
	cmp	r3, #0
	moveq	r3, r2
	ldr	r2, [r10, #2440]
	muleq	r1, r6, r3
	ldreq	r3, [sp, #4]
	ldrne	r1, [sp, #4]
	rsbeq	r1, r7, r1
	addeq	r1, r3, r1, asl #9
	ldr	r3, [sp, #20]
	cmp	r3, #0
	mov	r3, #36
	mla	r3, r3, r2, r0
	strne	r1, [r3, #8]
	bne	.L3813
	ldr	r0, [r3, #8]
	ldr	r3, .L3863+32
	ldrh	r2, [r3]
	b	.L3858
.L3810:
	ldr	r2, [sp, #56]
	mov	r3, #36
	cmn	r2, #1
	beq	.L3814
	ldr	r1, [r4, #2444]
	add	r0, sp, #60
	str	r2, [sp, #64]
	ldr	r2, [r4, #2440]
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
	ldr	r3, .L3863
	ldreq	r2, [r3, #-3160]
	addeq	r2, r2, #1
	streq	r2, [r3, #-3160]
	beq	.L3817
	ldr	r2, [r9, #8]
	cmp	r2, r6
	beq	.L3817
	ldr	r2, [r3, #-3160]
	ldr	r0, .L3863+36
	add	r2, r2, #1
	str	r2, [r3, #-3160]
	mov	r2, r6
	ldr	r1, [r9, #8]
	bl	printk
	b	.L3817
.L3814:
	ldr	r2, [r4, #2440]
	ldr	r1, [r4, #2444]
	mla	r3, r3, r2, r1
	mov	r1, #0
	ldr	r0, [r3, #8]
	ldr	r3, .L3863+32
	ldrh	r2, [r3]
	bl	ftl_memset
.L3817:
	ldr	r3, [sp, #24]
	cmp	r3, #0
	mov	r3, #36
	beq	.L3818
	ldr	r1, [r4, #2444]
	ldr	r2, [r4, #2440]
	mla	r3, r3, r2, r1
	ldr	r1, [sp, #4]
	ldr	r0, [r3, #8]
	ldr	r3, [sp, #32]
	add	r0, r0, r3, asl #9
	b	.L3861
.L3818:
	ldr	r1, [r4, #2440]
	ldr	r2, [r4, #2444]
	mla	r3, r3, r1, r2
	ldr	r2, .L3863+24
	ldrh	r1, [r2]
	ldr	r0, [r3, #8]
	mul	r1, r1, r6
	ldr	r3, [sp, #4]
	rsb	r1, r7, r1
	add	r1, r3, r1, asl #9
.L3861:
	ldr	r3, [sp, #12]
	mov	r2, r3, asl #9
	b	.L3858
.L3807:
	ldr	r2, [sp, #20]
	cmp	r2, #0
	ldr	r2, [r4, #2440]
	beq	.L3819
	ldr	r1, [r4, #2444]
	mla	r3, r3, r2, r1
	ldr	r2, .L3863+24
	ldrh	fp, [r2]
	ldr	r2, [sp, #4]
	mul	fp, fp, r6
	rsb	fp, r7, fp
	add	fp, r2, fp, asl #9
	str	fp, [r3, #8]
	b	.L3813
.L3819:
	ldr	r0, [r4, #2444]
	mla	r3, r3, r2, r0
	ldr	r2, .L3863+24
	ldrh	r1, [r2]
	ldrh	r2, [fp, #-2]
	ldr	r0, [r3, #8]
	mul	r1, r1, r6
	ldr	r3, [sp, #4]
	rsb	r1, r7, r1
	add	r1, r3, r1, asl #9
.L3858:
	bl	ftl_memcpy
.L3813:
	ldr	r3, .L3863+40
	ldr	r2, [sp, #40]
	ldr	r1, [sp, #36]
	strh	r3, [r2, r1]	@ movhi
	ldr	r2, .L3863
	str	r6, [r9, #8]
	add	r6, r6, #1
	ldr	r3, [r2, #-3328]
	str	r3, [r9, #4]
	add	r3, r3, #1
	cmn	r3, #1
	moveq	r3, #0
	str	r3, [r2, #-3328]
	ldr	r3, [sp, #56]
	str	r3, [r9, #12]
	ldrh	r3, [r5]
	strh	r3, [r9, #2]	@ movhi
	ldr	r3, [r4, #2440]
	add	r3, r3, #1
	str	r3, [r4, #2440]
	ldr	r3, [sp, #16]
	add	r3, r3, #1
	b	.L3860
.L3805:
	ldr	r3, [sp]
	ldr	r2, [sp, #16]
	ldr	r1, [sp, #20]
	rsb	r3, r2, r3
	ldr	r2, .L3863
	str	r3, [sp]
	ldr	r3, [r4, #2440]
	ldr	r2, [r2, #-540]
	cmp	r3, r2
	orrcs	r1, r1, #1
	cmp	r1, #0
	bne	.L3823
	ldrh	r3, [r5, #4]
	cmp	r3, #0
	beq	.L3823
.L3825:
	mov	r3, #0
	str	r3, [sp, #20]
	b	.L3797
.L3823:
	bl	FtlCacheWriteBack
	mov	r3, #0
	str	r3, [r10, #2440]
	ldr	r3, [sp]
	cmp	r3, #1
	bhi	.L3797
	b	.L3825
.L3862:
	mov	r0, r3
	ldr	r2, [sp, #8]
	ldr	r3, [sp, #28]
	rsb	r1, r2, r3
	bl	ftl_do_gc
	ldr	r3, .L3863+44
	ldrh	r3, [r3, #-8]
	cmp	r3, #5
	bls	.L3836
	cmp	r3, #31
	bhi	.L3830
	ldr	r3, .L3863+8
	ldrb	r3, [r3, #32]	@ zero_extendqisi2
	cmp	r3, #0
	bne	.L3830
.L3836:
	ldr	r4, [sp]
	ldr	r5, .L3863+48
	ldr	r6, .L3863
	ldr	r7, .L3863+52
.L3849:
	ldrh	r2, [r5]
	movw	r3, #65535
	cmp	r2, r3
	bne	.L3829
	ldrh	r3, [r7]
	cmp	r3, r2
	bne	.L3829
	ldr	r2, .L3863+56
	ldrh	r2, [r2]
	cmp	r2, r3
	bne	.L3829
	and	r0, r4, #7
	bl	List_get_gc_head_node
	uxth	r0, r0
	bl	FtlGcRefreshBlock
.L3829:
	ldr	r3, .L3863+60
	mov	r0, #1
	mov	r1, r0
	mov	r2, #128
	strh	r2, [r3]	@ movhi
	strh	r2, [r3, #-2]	@ movhi
	bl	ftl_do_gc
	mov	r0, #0
	mov	r1, #1
	bl	ftl_do_gc
	ldr	r3, [r6, #-3616]
	cmp	r3, #0
	bne	.L3830
	ldr	r3, .L3863+64
	ldrh	r3, [r3]
	cmp	r3, #2
	bhi	.L3830
	add	r4, r4, #1
	cmp	r4, #256
	bne	.L3849
	b	.L3830
.L3833:
	mvn	r0, #0
	b	.L3790
.L3830:
	mov	r0, #0
.L3790:
	add	sp, sp, #100
	@ sp needed
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L3864:
	.align	2
.L3863:
	.word	.LANCHOR2
	.word	.LANCHOR1
	.word	.LANCHOR0
	.word	.LANCHOR4
	.word	.LANCHOR2-3524
	.word	.LANCHOR2-3476
	.word	.LANCHOR0+2394
	.word	.LANCHOR0+2400
	.word	.LANCHOR0+2398
	.word	.LC166
	.word	-3947
	.word	.LANCHOR2-3520
	.word	.LANCHOR2-3284
	.word	.LANCHOR2-2666
	.word	.LANCHOR2-2664
	.word	.LANCHOR2-2718
	.word	.LANCHOR2-3528
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
	.global	FlashBootVendorWrite
	.type	FlashBootVendorWrite, %function
FlashBootVendorWrite:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	stmfd	sp!, {r4, r5, r6, lr}
	.save {r4, r5, r6, lr}
	mov	r4, r2
	mov	r6, r0
	mov	r5, r1
	bl	rknand_device_lock
	ldr	r3, .L3871
	ldr	r3, [r3, #504]
	cmp	r3, #1
	mvnne	r4, #0
	bne	.L3868
	mov	r2, r4
	mov	r0, r6
	mov	r1, r5
	bl	ftl_vendor_write
	mov	r4, r0
.L3868:
	bl	rknand_device_unlock
	mov	r0, r4
	ldmfd	sp!, {r4, r5, r6, pc}
.L3872:
	.align	2
.L3871:
	.word	.LANCHOR1
	.fnend
	.size	FlashBootVendorWrite, .-FlashBootVendorWrite
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
	.global	StorageSysDataStore
	.type	StorageSysDataStore, %function
StorageSysDataStore:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	stmfd	sp!, {r3, r4, r5, lr}
	.save {r3, r4, r5, lr}
	mov	r4, r1
	mov	r5, r0
	bl	rknand_device_lock
	mov	r2, r4
	mov	r1, #1
	mov	r0, r5
	bl	ftl_sys_write
	mov	r4, r0
	bl	rknand_device_unlock
	mov	r0, r4
	ldmfd	sp!, {r3, r4, r5, pc}
	.fnend
	.size	StorageSysDataStore, .-StorageSysDataStore
	.align	2
	.global	FtlDumpSysBlock
	.type	FtlDumpSysBlock, %function
FtlDumpSysBlock:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	stmfd	sp!, {r4, r5, r6, r7, r8, r9, lr}
	.save {r4, r5, r6, r7, r8, r9, lr}
	mov	r8, r0, asl #10
	ldr	r4, .L3885
	.pad #28
	sub	sp, sp, #28
	ldr	r7, .L3885+4
	mov	r6, r0
	mov	r5, #0
	ldr	r3, [r4, #-524]
	mov	r9, r4
	str	r3, [r4, #1768]
	ldr	r3, [r4, #-500]
	str	r3, [r4, #1772]
.L3877:
	ldrh	r2, [r7]
	sxth	r3, r5
	cmp	r3, r2
	bge	.L3884
	mov	r1, #1
	ldr	r0, .L3885+8
	mov	r2, r1
	orr	r3, r3, r8
	str	r3, [r4, #1764]
	bl	FlashReadPages
	ldr	r3, [r4, #1772]
	mov	r1, r6
	ldr	r0, .L3885+12
	ldr	r2, [r3]
	str	r2, [sp]
	ldr	r2, [r3, #4]
	str	r2, [sp, #4]
	ldr	r2, [r3, #8]
	str	r2, [sp, #8]
	ldr	r3, [r3, #12]
	ldr	r2, [r4, #1760]
	str	r3, [sp, #12]
	ldr	r3, [r4, #1768]
	ldr	r3, [r3]
	str	r3, [sp, #16]
	ldr	r3, [r4, #1764]
	bl	printk
	ldr	r3, [r4, #1772]
	ldr	r3, [r3]
	cmn	r3, #1
	beq	.L3878
	ldr	r0, .L3885+16
	mov	r2, #4
	ldr	r1, [r9, #-524]
	mov	r3, #768
	bl	rknand_print_hex
.L3878:
	add	r5, r5, #1
	b	.L3877
.L3884:
	add	sp, sp, #28
	@ sp needed
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, pc}
.L3886:
	.align	2
.L3885:
	.word	.LANCHOR2
	.word	.LANCHOR0+2390
	.word	.LANCHOR2+1760
	.word	.LC167
	.word	.LC168
	.fnend
	.size	FtlDumpSysBlock, .-FtlDumpSysBlock
	.align	2
	.global	dump_map_info
	.type	dump_map_info, %function
dump_map_info:
	.fnstart
	@ args = 0, pretend = 0, frame = 24
	@ frame_needed = 0, uses_anonymous_args = 0
	stmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.save {r4, r5, r6, r7, r8, r9, r10, fp, lr}
	movw	r3, #2328
	ldr	r6, .L3905
	.pad #52
	sub	sp, sp, #52
	ldr	r10, .L3905+4
	ldrh	r7, [r6, r3]
	add	fp, r10, #18
.L3888:
	ldrh	r3, [r10]
	ldr	r4, .L3905+8
	cmp	r3, r7
	bls	.L3901
	ldr	r1, .L3905+12
	mov	r8, #0
	ldr	r2, [r4, #-536]
	mov	r5, r8
	ldr	r3, [r4, #-2696]
	ldrh	ip, [r1]
	ldr	r9, [r4, #-2692]
	ldrh	r1, [r1, #80]
	str	r1, [sp, #28]
.L3897:
	uxth	r1, r8
	cmp	r1, ip
	bcs	.L3903
	mov	r1, r7
	ldrb	r0, [fp, r8]	@ zero_extendqisi2
	str	r3, [sp, #44]
	str	r2, [sp, #40]
	str	ip, [sp, #36]
	bl	V2P_block
	str	r0, [sp, #32]
	bl	FtlBbmIsBadBlock
	cmp	r0, #0
	ldr	r1, [sp, #32]
	ldr	ip, [sp, #36]
	ldr	r2, [sp, #40]
	ldr	r3, [sp, #44]
	bne	.L3889
	mov	r0, #36
	mov	r1, r1, asl #10
	mla	r0, r0, r5, r2
	stmib	r0, {r1, r3}
	ldr	r1, [sp, #28]
	mul	r1, r1, r5
	add	r5, r5, #1
	uxth	r5, r5
	add	lr, r1, #3
	cmp	r1, #0
	movlt	r1, lr
	bic	r1, r1, #3
	add	r1, r9, r1
	str	r1, [r0, #12]
.L3889:
	add	r8, r8, #1
	b	.L3897
.L3903:
	cmp	r5, #0
	beq	.L3892
	ldr	r0, [r4, #-536]
	mov	r1, r5
	mov	r2, #1
	mov	r8, #0
	bl	FlashReadPages
	mov	r9, #36
.L3893:
	uxth	r3, r8
	cmp	r3, r5
	bcs	.L3892
	ldr	r3, [r4, #-536]
	ldr	r0, .L3905+16
	mla	r3, r9, r8, r3
	add	r8, r8, #1
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
	b	.L3893
.L3892:
	add	r7, r7, #1
	uxth	r7, r7
	b	.L3888
.L3901:
	ldr	r9, .L3905+20
	mov	r8, #0
.L3896:
	ldr	r7, .L3905+8
	sxth	r5, r8
	sub	r3, r7, #388
	ldrh	r3, [r3]
	cmp	r5, r3
	bge	.L3899
	mov	r5, r5, asl #1
	mov	r7, #0
.L3900:
	ldrh	r2, [r9]
	sxth	r3, r7
	add	r7, r7, #1
	cmp	r3, r2
	bge	.L3904
	ldr	r2, [r4, #-472]
	mov	r1, #1
	ldr	r0, .L3905+24
	ldrh	r2, [r2, r5]
	orr	r3, r3, r2, asl #10
	mov	r2, r1
	str	r3, [r4, #1764]
	bl	FlashReadPages
	ldr	r3, [r4, #1772]
	ldr	r1, [r4, #-472]
	ldr	r2, [r4, #1768]
	ldr	r0, [r3]
	ldrh	r1, [r1, r5]
	str	r0, [sp]
	ldr	r0, [r3, #4]
	str	r0, [sp, #4]
	ldr	r0, [r3, #8]
	str	r0, [sp, #8]
	ldr	r3, [r3, #12]
	ldr	r0, .L3905+28
	str	r3, [sp, #12]
	ldr	r3, [r2]
	str	r3, [sp, #16]
	ldr	r3, [r2, #4]
	str	r3, [sp, #20]
	ldr	r2, [r4, #1760]
	ldr	r3, [r4, #1764]
	bl	printk
	b	.L3900
.L3904:
	add	r8, r8, #1
	b	.L3896
.L3899:
	ldr	r1, [r7, #-472]
	movw	r4, #2424
	ldr	r3, [r6, #2416]
	mov	r2, #2
	ldr	r0, .L3905+32
	bl	rknand_print_hex
	ldr	r1, [r7, #-452]
	ldrh	r3, [r6, r4]
	mov	r2, #4
	ldr	r0, .L3905+36
	bl	rknand_print_hex
	ldr	r0, .L3905+40
	ldr	r1, [r7, #-448]
	mov	r2, #4
	ldrh	r3, [r6, r4]
	add	sp, sp, #52
	@ sp needed
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, fp, lr}
	b	rknand_print_hex
.L3906:
	.align	2
.L3905:
	.word	.LANCHOR0
	.word	.LANCHOR0+2330
	.word	.LANCHOR2
	.word	.LANCHOR0+2320
	.word	.LC169
	.word	.LANCHOR0+2390
	.word	.LANCHOR2+1760
	.word	.LC113
	.word	.LC170
	.word	.LC171
	.word	.LC172
	.fnend
	.size	dump_map_info, .-dump_map_info
	.align	2
	.global	flash_boot_enter_slc_mode
	.type	flash_boot_enter_slc_mode, %function
flash_boot_enter_slc_mode:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	@ link register save eliminated.
	ldr	r3, .L3909
	ldr	r2, [r3, #2264]
	ldr	r3, .L3909+4
	cmp	r2, r3
	bxne	lr
	b	flash_enter_slc_mode
.L3910:
	.align	2
.L3909:
	.word	.LANCHOR0
	.word	1446522928
	.fnend
	.size	flash_boot_enter_slc_mode, .-flash_boot_enter_slc_mode
	.align	2
	.global	flash_boot_exit_slc_mode
	.type	flash_boot_exit_slc_mode, %function
flash_boot_exit_slc_mode:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	@ link register save eliminated.
	ldr	r3, .L3913
	ldr	r2, [r3, #2264]
	ldr	r3, .L3913+4
	cmp	r2, r3
	bxne	lr
	b	flash_exit_slc_mode
.L3914:
	.align	2
.L3913:
	.word	.LANCHOR0
	.word	1446522928
	.fnend
	.size	flash_boot_exit_slc_mode, .-flash_boot_exit_slc_mode
	.align	2
	.global	write_idblock
	.type	write_idblock, %function
write_idblock:
	.fnstart
	@ args = 0, pretend = 0, frame = 120
	@ frame_needed = 0, uses_anonymous_args = 0
	stmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.save {r4, r5, r6, r7, r8, r9, r10, fp, lr}
	mov	r8, r0
	ldr	r7, .L3968
	.pad #132
	sub	sp, sp, #132
	mov	r0, #256000
	mov	fp, r1
	mov	r6, r2
	ldr	r3, [r7, #44]
	ldr	r4, [r7, #36]
	ldrb	r5, [r3, #9]	@ zero_extendqisi2
	bl	ftl_malloc
	subs	r3, r0, #0
	str	r3, [sp, #8]
	beq	.L3942
	add	ip, r8, #508
	add	ip, ip, #3
	mov	r10, ip, lsr #9
	cmp	r10, #8
	bls	.L3940
	cmp	r10, #500
	bhi	.L3942
	b	.L3917
.L3940:
	mov	r10, #8
.L3917:
	ldr	r2, [fp]
	ldr	r3, .L3968+4
	cmp	r2, r3
	bne	.L3942
	smulbb	r3, r5, r4
	uxth	r3, r3
	str	r3, [sp, #12]
	sub	r0, r3, #1
	mov	r1, r3
	add	r0, r0, r10
	bl	__aeabi_uidiv
	mov	r3, #0
	movw	r2, #63871
	str	r0, [sp, #36]
	add	r0, fp, #254976
	add	r0, r0, #512
.L3921:
	ldr	r1, [r0, #-4]!
	cmp	r1, #0
	bne	.L3918
	ldr	r1, [fp, r3, asl #2]
	add	r3, r3, #1
	cmp	r3, #4096
	sub	r2, r2, #1
	movhi	r3, #0
	cmp	r2, #4096
	str	r1, [r0, #512]
	bne	.L3921
	b	.L3920
.L3918:
	ldr	r0, .L3968+8
	bl	printk
.L3920:
	mov	r3, #5
	ldr	r0, .L3968+12
	mov	r1, r6
	mov	r2, #4
	bl	rknand_print_hex
	ldr	r1, [fp, #512]
	ldrb	r2, [r7, #33]	@ zero_extendqisi2
	sub	r4, r6, #4
	ldr	r0, .L3968+16
	bl	printk
	ldr	r2, .L3968+20
	ldrh	r3, [r7, #146]
	mov	r1, r10
	ldr	r0, .L3968+24
	ldr	r2, [r2, #1716]
	ldr	r5, .L3968
	str	r2, [sp]
	mov	r2, r10
	bl	printk
	ldrb	r3, [r7, #33]	@ zero_extendqisi2
	ldr	r2, [fp, #512]
	cmp	r2, r3
	strhi	r3, [fp, #512]
	mov	r3, r10, asl #7
	str	r3, [sp, #40]
	mov	r3, #0
	str	r3, [sp, #20]
	str	r3, [sp, #16]
.L3938:
	ldr	r2, [r4, #4]
	ldrb	r3, [r5, #33]	@ zero_extendqisi2
	ldr	r7, .L3968
	cmp	r2, r3
	bcs	.L3923
	ldr	r3, .L3968+20
	ldr	r3, [r3, #1716]
	cmp	r2, r3
	bcc	.L3923
	ldr	r3, [sp, #36]
	ldr	r1, [sp, #36]
	cmp	r3, #1
	movls	r3, #0
	movhi	r3, #1
	str	r3, [sp, #44]
	ldr	r3, [sp, #16]
	cmp	r3, #0
	cmpne	r1, #1
	bls	.L3924
	ldr	r3, [r4]
	add	r3, r3, #1
	cmp	r2, r3
	beq	.L3923
.L3924:
	mov	r1, #0
	mov	r2, #512
	ldr	r0, [sp, #8]
	bl	memset
	ldr	r6, [r4, #4]
	mov	r2, r10
	ldr	r0, .L3968+28
	ldr	r3, [sp, #12]
	mul	r6, r6, r3
	ldr	r3, [r7, #44]
	ldr	r7, [r7, #36]
	ldrb	r9, [r3, #9]	@ zero_extendqisi2
	mov	r1, r6
	bl	printk
	mov	r0, #0
	bl	flash_boot_enter_slc_mode
	mov	r1, r9
	mov	r0, r6
	bl	__aeabi_uidiv
	smulbb	r7, r7, r9
	uxth	r7, r7
	mov	r1, r0
	mov	r0, #0
	mov	r2, r0
	bl	FlashEraseBlock
	cmp	r10, r7
	movls	r8, #1
	bls	.L3925
	mov	r0, #0
	add	r1, r6, r7
	mov	r2, r0
	mov	r8, #2
	bl	FlashEraseBlock
.L3925:
	mov	r0, #0
	bl	flash_boot_exit_slc_mode
	ldr	r3, [r5, #44]
	ldrh	r0, [r3, #10]
	ldrb	r1, [r3, #12]	@ zero_extendqisi2
	mov	r0, r0, asl #2
	mul	r0, r8, r0
	bl	__aeabi_idiv
	mov	r1, r7
	mov	r8, #0
	str	r0, [sp, #48]
	mov	r0, r6
	bl	__aeabi_uidivmod
	mov	ip, r1
	rsb	r3, r1, r6
	str	fp, [sp, #24]
	str	r3, [sp, #28]
.L3926:
	ldr	r3, [sp, #48]
	ldr	r7, .L3968
	cmp	r8, r3
	bcs	.L3966
	add	r3, r8, ip
	ubfx	r3, r3, #2, #16
	cmp	r3, #0
	beq	.L3927
	add	r2, r3, #1
	add	r1, r5, r2, asl #1
	ldrh	r7, [r1, #152]
	ldrb	r1, [r5, #148]	@ zero_extendqisi2
	cmp	r1, #0
	beq	.L3928
	ldr	r1, [r5, #2264]
	ldr	r0, .L3968+32
	cmp	r1, r0
	moveq	r7, r2
.L3928:
	sub	r7, r7, #-1073741823
	mov	r7, r7, asl #2
	str	r7, [sp, #64]
.L3927:
	movw	r2, #61424
	str	r2, [sp, #68]
	add	r2, r5, r3, asl #1
	ldrh	r7, [r2, #152]
	ldrb	r2, [r5, #148]	@ zero_extendqisi2
	cmp	r2, #0
	beq	.L3929
	ldr	r2, [r5, #2264]
	ldr	r1, .L3968+32
	cmp	r2, r1
	moveq	r7, r3
.L3929:
	ldr	r3, [sp, #28]
	add	r8, r8, #4
	str	ip, [sp, #56]
	uxth	r8, r8
	mla	r2, r9, r7, r3
	ldr	r3, .L3968
	ldrb	r3, [r3, #2312]	@ zero_extendqisi2
	str	r2, [sp, #52]
	str	r3, [sp, #32]
	ldr	r3, .L3968+20
	ldrb	r0, [r3, #1722]	@ zero_extendqisi2
	bl	FlashBchSel
	mov	r0, #0
	bl	flash_boot_enter_slc_mode
	ldr	r3, .L3968
	ldr	r3, [r3, #44]
	ldrb	r1, [r3, #9]	@ zero_extendqisi2
	ldr	r2, [sp, #52]
	mov	r0, r2
	bl	__aeabi_uidiv
	add	r3, sp, #64
	mov	r1, r0
	ldr	r2, [sp, #24]
	mov	r0, #0
	bl	FlashProgPage
	mov	r0, #0
	bl	flash_boot_exit_slc_mode
	ldr	r0, [sp, #32]
	bl	FlashBchSel
	mov	r1, r9
	ldr	r0, [sp, #28]
	bl	__aeabi_uidiv
	add	r2, r7, #1
	uxth	r2, r2
	mov	r1, r0
	mov	r0, #0
	bl	FlashPageProgMsbFFData
	ldr	r3, [sp, #24]
	ldr	ip, [sp, #56]
	add	r3, r3, #2048
	str	r3, [sp, #24]
	b	.L3926
.L3966:
	mov	r1, r6
	mov	r2, r10
	mov	r3, #0
	ldr	r0, .L3968+36
	bl	printk
	ldr	r6, [r4, #4]
	ldr	r8, [r7, #36]
	mov	r2, r10
	ldr	r0, .L3968+40
	mov	r9, #0
	ldr	r3, [sp, #12]
	mul	r6, r6, r3
	ldr	r3, [r7, #44]
	ldrb	r3, [r3, #9]	@ zero_extendqisi2
	mov	r1, r6
	str	r3, [sp, #24]
	ldrh	r3, [sp, #24]
	smulbb	r8, r8, r3
	bl	printk
	mov	r0, r6
	uxth	r8, r8
	mov	r1, r8
	bl	__aeabi_uidivmod
	rsb	r3, r1, r6
	str	r3, [sp, #48]
	ldr	r3, [sp, #24]
	str	r1, [sp, #28]
	mul	ip, r3, r1
	ldr	r3, [sp, #8]
	ubfx	ip, ip, #2, #2
	str	r3, [sp, #32]
.L3931:
	cmp	r9, r10
	bcs	.L3967
	ldr	r3, [sp, #28]
	rsb	r8, ip, #4
	ldrb	r1, [r5, #148]	@ zero_extendqisi2
	add	r3, r9, r3
	uxth	r8, r8
	ubfx	r3, r3, #2, #16
	cmp	r1, #0
	add	r2, r5, r3, asl #1
	ldrh	r2, [r2, #152]
	beq	.L3932
	ldr	r1, [r5, #2264]
	ldr	r0, .L3968+32
	cmp	r1, r0
	moveq	r2, r3
.L3932:
	ldr	r3, [sp, #48]
	add	r9, r8, r9
	ldr	r1, [sp, #24]
	add	r3, ip, r3
	ldrb	ip, [r7, #2312]	@ zero_extendqisi2
	uxth	r9, r9
	mla	r3, r1, r2, r3
	ldr	r2, [r7, #44]
	str	ip, [sp, #52]
	ldrb	r1, [r2, #9]	@ zero_extendqisi2
	ldr	r2, .L3968+20
	str	r3, [sp, #60]
	str	r1, [sp, #56]
	ldrb	r0, [r2, #1722]	@ zero_extendqisi2
	bl	FlashBchSel
	mov	r0, #0
	bl	flash_boot_enter_slc_mode
	ldr	r3, [sp, #60]
	ldr	r1, [sp, #56]
	mov	r0, r3
	bl	__aeabi_uidiv
	mov	r1, r0
	mov	r0, #0
	mov	r3, r0
	ldr	r2, [sp, #32]
	bl	FlashReadPage
	mov	r0, #0
	bl	flash_boot_exit_slc_mode
	ldr	ip, [sp, #52]
	mov	r0, ip
	bl	FlashBchSel
	mov	ip, #0
	ldr	r3, [sp, #32]
	add	r3, r3, r8, asl #9
	str	r3, [sp, #32]
	b	.L3931
.L3967:
	mov	r1, r6
	mov	r2, r10
	mov	r3, #0
	ldr	r0, .L3968+44
	bl	printk
	mov	r6, #0
	mov	r3, fp
	ldr	r2, [sp, #8]
.L3937:
	mov	r8, r2
	mov	r7, r3
	ldr	r0, [r8]
	add	r2, r2, #4
	ldr	r1, [r7]
	add	r3, r3, #4
	cmp	r0, r1
	beq	.L3934
	mov	r1, #0
	mov	r2, #512
	ldr	r0, [sp, #8]
	bl	memset
	ldr	r3, [r8]
	ldr	r0, .L3968+48
	str	r3, [sp]
	ldr	r3, [r7]
	bic	r7, r6, #255
	ldr	r1, [sp, #16]
	mov	r7, r7, asl #2
	str	r3, [sp, #4]
	mov	r3, r6
	ldr	r2, [r4, #4]
	bl	printk
	ldr	r0, .L3968+52
	add	r1, fp, r7
	mov	r2, #4
	mov	r3, #256
	bl	rknand_print_hex
	mov	r2, #4
	ldr	r0, .L3968+56
	ldr	r3, [sp, #8]
	add	r1, r3, r7
	mov	r3, #256
	bl	rknand_print_hex
	mov	r0, #0
	bl	flash_boot_enter_slc_mode
	ldr	r1, [r4, #4]
	mov	r0, #0
	mov	r2, r0
	ldr	r3, [sp, #12]
	mul	r1, r1, r3
	bl	FlashEraseBlock
	ldr	r3, [sp, #44]
	cmp	r3, #0
	beq	.L3935
	ldr	r1, [r4, #4]
	mov	r0, #0
	ldr	r3, [sp, #12]
	mov	r2, r0
	mla	r1, r1, r3, r3
	bl	FlashEraseBlock
.L3935:
	mov	r0, #0
	bl	flash_boot_exit_slc_mode
	ldr	r0, .L3968+60
	ldr	r1, [r4, #4]
	bl	printk
	ldr	r3, [sp, #40]
	cmp	r6, r3
	bcc	.L3923
	b	.L3936
.L3934:
	ldr	r1, [sp, #40]
	add	r6, r6, #1
	cmp	r6, r1
	bne	.L3937
.L3936:
	ldr	r3, [sp, #20]
	add	r3, r3, #1
	str	r3, [sp, #20]
.L3923:
	ldr	r3, [sp, #16]
	add	r4, r4, #4
	add	r3, r3, #1
	str	r3, [sp, #16]
	cmp	r3, #5
	bne	.L3938
	ldr	r0, [sp, #8]
	bl	ftl_free
	ldr	r3, [sp, #20]
	clz	r0, r3
	mov	r0, r0, lsr #5
	rsb	r0, r0, #0
	b	.L3916
.L3942:
	mvn	r0, #0
.L3916:
	add	sp, sp, #132
	@ sp needed
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L3969:
	.align	2
.L3968:
	.word	.LANCHOR0
	.word	-52655045
	.word	.LC173
	.word	.LC174
	.word	.LC175
	.word	.LANCHOR2
	.word	.LC176
	.word	.LC177
	.word	1446522928
	.word	.LC178
	.word	.LC179
	.word	.LC180
	.word	.LC181
	.word	.LC182
	.word	.LC183
	.word	.LC184
	.fnend
	.size	write_idblock, .-write_idblock
	.align	2
	.global	write_loader_lba
	.type	write_loader_lba, %function
write_loader_lba:
	.fnstart
	@ args = 0, pretend = 0, frame = 40
	@ frame_needed = 0, uses_anonymous_args = 0
	cmp	r0, #64
	stmfd	sp!, {r4, r5, r6, r7, r8, r9, lr}
	.save {r4, r5, r6, r7, r8, r9, lr}
	mov	r5, r0
	.pad #52
	sub	sp, sp, #52
	mov	r6, r1
	mov	r8, r2
	ldr	r4, .L3993
	bne	.L3971
	ldr	r2, [r2]
	ldr	r3, .L3993+4
	cmp	r2, r3
	bne	.L3971
	mov	r0, #256000
	mov	r3, #1
	strb	r3, [r4, #2028]
	bl	ftl_malloc
	mov	r1, #0
	mov	r2, #256000
	str	r0, [r4, #2032]
	bl	ftl_memset
	str	r5, [r4, #2036]
.L3971:
	str	r6, [sp]
	mov	r3, r5
	ldr	r0, .L3993+8
	ldr	r1, [r4, #2032]
	ldr	r2, [r8]
	bl	printk
	ldrb	r3, [r4, #2028]	@ zero_extendqisi2
	ldr	r9, .L3993
	cmp	r3, #0
	beq	.L3970
	sub	r0, r5, #64
	ldr	r7, [r9, #2032]
	cmp	r0, #500
	bcs	.L3973
	rsb	r2, r5, #564
	add	r0, r7, r0, asl #9
	cmp	r2, r6
	mov	r1, r8
	movcs	r2, r6
	mov	r2, r2, asl #9
	bl	ftl_memcpy
	b	.L3974
.L3973:
	cmp	r5, #564
	bcs	.L3982
.L3974:
	ldr	r3, [r4, #2036]
	cmp	r3, r5
	beq	.L3980
	ldr	r2, .L3993
	mov	r3, #0
	cmp	r7, r3
	mov	r8, r3
	strb	r3, [r2, #2028]
	beq	.L3981
	mov	r0, r7
	bl	ftl_free
.L3981:
	str	r8, [r4, #2032]
.L3980:
	add	r5, r5, r6
	str	r5, [r4, #2036]
	b	.L3970
.L3982:
	ldr	r3, .L3993+12
	ldr	r0, [r9, #2036]
	ldr	r3, [r3, #44]
	sub	r0, r0, #64
	cmp	r0, #500
	ldrb	r3, [r3, #9]	@ zero_extendqisi2
	movcs	r0, #500
	cmp	r3, #4
	beq	.L3983
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
	b	.L3976
.L3983:
	mov	r3, #0
.L3975:
	cmp	r0, #256
	add	r1, sp, #8
	mov	r2, r3, asl #1
	movls	r2, r3
	str	r2, [r1, r3, asl #2]
	add	r3, r3, #1
	cmp	r3, #5
	bne	.L3975
.L3976:
	movw	r3, #63872
.L3979:
	ldr	r2, [r7, r3, asl #2]
	cmp	r2, #0
	addne	r3, r3, #128
	movne	r0, r3, asl #2
	bne	.L3978
.L3977:
	sub	r3, r3, #1
	cmp	r3, #4096
	bne	.L3979
	mov	r0, r0, asl #9
.L3978:
	mov	r1, r7
	add	r2, sp, #8
	bl	write_idblock
	ldr	r0, [r4, #2032]
	mov	r7, #0
	strb	r7, [r4, #2028]
	bl	ftl_free
	str	r7, [r4, #2032]
	b	.L3980
.L3970:
	add	sp, sp, #52
	@ sp needed
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, pc}
.L3994:
	.align	2
.L3993:
	.word	.LANCHOR4
	.word	-52655045
	.word	.LC185
	.word	.LANCHOR0
	.fnend
	.size	write_loader_lba, .-write_loader_lba
	.align	2
	.global	FtlWrite
	.type	FtlWrite, %function
FtlWrite:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	stmfd	sp!, {r4, r5, r6, r7, r8, lr}
	.save {r4, r5, r6, r7, r8, lr}
	mov	r6, r2
	sub	r2, r1, #64
	mov	r4, r1
	cmp	r2, #1984
	mov	r5, r3
	mov	r7, r0
	movcs	r2, #0
	movcc	r2, #1
	cmp	r0, #0
	movne	r2, #0
	cmp	r2, #0
	beq	.L3996
	mov	r0, r1
	mov	r2, r3
	mov	r1, r6
	bl	write_loader_lba
.L3996:
	mov	r0, r7
	mov	r1, r4
	mov	r2, r6
	mov	r3, r5
	ldmfd	sp!, {r4, r5, r6, r7, r8, lr}
	b	ftl_write
	.fnend
	.size	FtlWrite, .-FtlWrite
	.align	2
	.global	rknand_sys_storage_ioctl
	.type	rknand_sys_storage_ioctl, %function
rknand_sys_storage_ioctl:
	.fnstart
	@ args = 0, pretend = 0, frame = 520
	@ frame_needed = 0, uses_anonymous_args = 0
	ldr	r3, .L4058
	stmfd	sp!, {r4, r5, r6, r7, lr}
	.save {r4, r5, r6, r7, lr}
	cmp	r1, r3
	.pad #524
	sub	sp, sp, #524
	mov	r4, r1
	mov	r5, r2
	beq	.L4003
	bhi	.L4004
	sub	r3, r3, #2080
	sub	r3, r3, #6
	cmp	r1, r3
	beq	.L4005
	bhi	.L4006
	sub	r3, r3, #238
	cmp	r1, r3
	beq	.L4007
	add	r3, r3, #237
	cmp	r1, r3
	beq	.L4008
	b	.L4038
.L4006:
	ldr	r3, .L4058+4
	cmp	r1, r3
	beq	.L4009
	add	r3, r3, #1
	cmp	r1, r3
	beq	.L4010
	sub	r3, r3, #124
	cmp	r1, r3
	bne	.L4038
	b	.L4056
.L4004:
	ldr	r3, .L4058+8
	cmp	r1, r3
	mov	r6, r3
	beq	.L4012
	bhi	.L4013
	sub	r3, r3, #2512
	sub	r3, r3, #14
	cmp	r1, r3
	beq	.L4003
	add	r3, r3, #10
	cmp	r1, r3
	beq	.L4003
	b	.L4038
.L4013:
	ldr	r3, .L4058+12
	cmp	r1, r3
	beq	.L4012
	bcc	.L4014
	add	r3, r3, #1
	cmp	r1, r3
	beq	.L4014
	b	.L4038
.L4008:
	ldr	r0, .L4058+16
	bl	printk
	mov	r1, r5
	mov	r2, #520
	mov	r0, sp
	bl	rk_copy_from_user
	cmp	r0, #0
	beq	.L4015
.L4021:
	ldr	r0, .L4058+20
	bl	printk
	b	.L4053
.L4015:
	ldr	r2, [sp]
	ldr	r3, .L4058+24
	cmp	r2, r3
	beq	.L4016
.L4018:
	mvn	r4, #0
	b	.L4017
.L4016:
	ldr	r3, [sp, #4]
	cmp	r3, #512
	bhi	.L4018
	ldr	r4, .L4058+28
	mov	r2, #512
	mov	r0, sp
	ldr	r1, [r4, #2040]
	bl	memcpy
	ldr	r2, [r4, #2044]
	ldr	r3, .L4058+32
	cmp	r2, r3
	beq	.L4019
	mov	r1, #0
	add	r0, sp, #64
	mov	r2, #128
	str	r1, [sp, #8]
	str	r1, [sp, #12]
	bl	memset
.L4019:
	mov	r1, #0
	add	r0, sp, #256
	mov	r2, #256
	str	r1, [sp, #16]
	bl	memset
.L4048:
	mov	r0, r5
	mov	r1, sp
	mov	r2, #520
	bl	rk_copy_to_user
	cmp	r0, #0
	bne	.L4053
	b	.L4052
.L4005:
	ldr	r0, .L4058+36
	bl	printk
	mov	r1, r5
	mov	r2, #520
	mov	r0, sp
	bl	rk_copy_from_user
	cmp	r0, #0
	bne	.L4021
	ldr	r2, [sp]
	ldr	r3, .L4058+24
	cmp	r2, r3
	bne	.L4018
	ldr	r3, [sp, #4]
	cmp	r3, #512
	bhi	.L4018
	ldr	r2, .L4058+28
	ldr	r3, .L4058+32
	ldr	r1, [r2, #2044]
	cmp	r1, r3
	mvnne	r0, #1
	bne	.L4002
	ldr	r3, [sp, #12]
	sub	r1, r3, #1
	cmp	r1, #127
	mvnhi	r0, #2
	bhi	.L4002
	ldr	r4, [r2, #2040]
	add	r1, sp, #64
	add	r0, r4, #64
	str	r3, [r4, #12]
	ldr	r2, [sp, #12]
	bl	memcpy
	mov	r0, #1
	mov	r1, r4
	b	.L4051
.L4010:
	ldr	r0, .L4058+40
	bl	printk
	mov	r1, r5
	mov	r2, #520
	mov	r0, sp
	bl	rk_copy_from_user
	cmp	r0, #0
	bne	.L4021
	ldr	r2, [sp]
	ldr	r3, .L4058+44
	cmp	r2, r3
	bne	.L4018
	ldr	r3, [sp, #4]
	cmp	r3, #512
	bhi	.L4018
	ldr	r5, .L4058+28
	ldr	r3, [r5, #2048]
	cmp	r3, #0
	bne	.L4022
.L4025:
	mov	r0, #0
	b	.L4002
.L4022:
	ldr	r3, [r5, #2052]
	ldr	r2, .L4058+48
	ldr	r1, [r3]
	cmp	r1, r2
	beq	.L4023
	str	r2, [r3]
	mov	r2, #504
	ldr	r3, .L4058+28
	ldr	r3, [r3, #2052]
	str	r2, [r3, #4]
	mov	r2, #0
	str	r2, [r3, #8]
	str	r2, [r3, #12]
.L4023:
	ldr	r1, [r5, #2052]
	mov	r4, #0
	mov	r0, r4
	str	r4, [r1, #16]
	bl	StorageSysDataStore
	ldr	r3, [r5, #2040]
	ldr	r2, .L4058+24
	ldr	r1, [r3]
	cmp	r1, r2
	strne	r2, [r3]
	ldr	r6, [r5, #2040]
	ldrne	r3, .L4058+28
	movne	r2, #504
	add	r0, r6, #64
	ldrne	r3, [r3, #2040]
	stmneib	r3, {r2, r4}
	mov	r4, #0
	mov	r1, r4
	mov	r2, #128
	str	r4, [r6, #12]
	bl	memset
	mov	r0, #1
	mov	r1, r6
	bl	StorageSysDataStore
	str	r4, [r5, #2048]
	str	r4, [r5, #2044]
	b	.L4017
.L4009:
	ldr	r0, .L4058+52
	bl	printk
	mov	r1, r5
	mov	r2, #520
	mov	r0, sp
	bl	rk_copy_from_user
	cmp	r0, #0
	bne	.L4021
	ldr	r2, [sp]
	ldr	r3, .L4058+56
	cmp	r2, r3
	bne	.L4018
	ldr	r3, [sp, #4]
	cmp	r3, #512
	bhi	.L4018
	ldr	r5, .L4058+28
	ldr	r3, [r5, #2048]
	cmp	r3, #1
	beq	.L4025
	ldr	r2, [r5, #2052]
	ldr	r3, .L4058+48
	ldr	r1, [r2]
	cmp	r1, r3
	beq	.L4026
	str	r3, [r2]
	mov	r2, #504
	ldr	r3, [r5, #2052]
	str	r2, [r3, #4]
	mov	r2, #0
	str	r2, [r3, #8]
	str	r2, [r3, #12]
.L4026:
	ldr	r1, [r5, #2052]
	mov	r3, #1
	mov	r0, #0
	mov	r4, #0
	str	r3, [r1, #16]
	bl	StorageSysDataStore
	ldr	r3, [r5, #2040]
	ldr	r2, .L4058+24
	ldr	r1, [r3]
	cmp	r1, r2
	strne	r2, [r3]
	ldr	r6, [r5, #2040]
	ldrne	r3, .L4058+28
	movne	r1, #504
	movne	r2, #0
	add	r0, r6, #64
	ldrne	r3, [r3, #2040]
	stmneib	r3, {r1, r2}
	mov	r1, r4
	mov	r2, #128
	str	r4, [r6, #12]
	bl	memset
	mov	r0, #1
	mov	r1, r6
	bl	StorageSysDataStore
	mov	r3, #1
	str	r3, [r5, #2048]
	b	.L4017
.L4056:
	ldr	r0, .L4058+60
	bl	printk
	mov	r1, r5
	mov	r2, #520
	mov	r0, sp
	bl	rk_copy_from_user
	cmp	r0, #0
	bne	.L4021
	ldr	r2, [sp]
	ldr	r3, .L4058+64
	cmp	r2, r3
	bne	.L4018
	ldr	r2, [sp, #4]
	cmp	r2, #512
	addls	r0, sp, #8
	ldrls	r1, .L4058+68
	bls	.L4054
	b	.L4018
.L4003:
	ldr	r0, .L4058+72
	cmp	r4, r0
	mov	r7, r0
	ldreq	r0, .L4058+76
	beq	.L4050
	ldr	r3, .L4058+80
	cmp	r4, r3
	ldreq	r0, .L4058+84
	ldrne	r0, .L4058+88
.L4050:
	bl	printk
	mov	r1, r5
	mov	r2, #520
	mov	r0, sp
	bl	rk_copy_from_user
	cmp	r0, #0
	bne	.L4021
	ldr	r2, [sp]
	ldr	r3, .L4058+92
	cmp	r2, r3
	bne	.L4053
	ldr	r3, .L4058+80
	ldr	r6, .L4058+28
	cmp	r4, r3
	bne	.L4032
	ldr	r3, [r6, #2040]
	mov	r0, r5
	mov	r1, sp
	mov	r2, #16
	ldr	r3, [r3, #20]
	str	r3, [sp, #4]
	strb	r3, [sp, #8]
	bl	rk_copy_to_user
	cmp	r0, #0
	beq	.L4002
	b	.L4053
.L4032:
	ldr	r3, [r6, #2568]
	cmp	r3, #10
	bhi	.L4053
	ldr	r1, [r6, #2040]
	ldr	r2, [sp, #4]
	ldr	r3, [r1, #24]
	cmp	r3, r2
	cmpne	r3, #0
	movne	r3, #1
	moveq	r3, #0
	beq	.L4033
	ldr	r0, .L4058+96
	mov	r1, r2
	bl	printk
	ldr	r3, [r6, #2568]
	add	r3, r3, #1
	str	r3, [r6, #2568]
.L4053:
	mvn	r0, #13
	b	.L4002
.L4033:
	cmp	r4, r7
	str	r3, [r6, #2568]
	mov	r0, #1
	moveq	r2, r3
	movne	r3, #1
	moveq	r3, r2
	str	r2, [r1, #24]
	str	r3, [r1, #20]
	bl	StorageSysDataStore
	cmn	r0, #1
	bne	.L4052
	b	.L4057
.L4012:
	ldr	r0, .L4058+100
	bl	printk
	mov	r1, r5
	mov	r2, #520
	mov	r0, sp
	bl	rk_copy_from_user
	cmp	r0, #0
	bne	.L4021
	ldr	r2, [sp]
	ldr	r3, .L4058+104
	cmp	r2, r3
	bne	.L4018
	ldr	r2, [sp, #4]
	cmp	r2, #504
	bhi	.L4018
	ldr	r3, .L4058+28
	cmp	r4, r6
	add	r0, sp, #8
	ldreq	r1, [r3, #2572]
	ldrne	r1, [r3, #2576]
	add	r1, r1, #8
.L4054:
	bl	memcpy
	b	.L4048
.L4014:
	ldr	r0, .L4058+108
	bl	printk
	mov	r1, r5
	mov	r2, #520
	mov	r0, sp
	bl	rk_copy_from_user
	cmp	r0, #0
	bne	.L4021
	ldr	r2, [sp]
	ldr	r3, .L4058+104
	cmp	r2, r3
	bne	.L4018
	ldr	r2, [sp, #4]
	cmp	r2, #504
	bhi	.L4018
	ldr	r3, .L4058+112
	add	r2, r2, #8
	cmp	r4, r3
	ldr	r4, .L4058+28
	bne	.L4037
	mov	r1, sp
	ldr	r0, [r4, #2572]
	bl	memcpy
	mov	r0, #2
	ldr	r1, [r4, #2572]
	b	.L4051
.L4037:
	mov	r1, sp
	ldr	r0, [r4, #2576]
	bl	memcpy
	ldr	r1, [r4, #2576]
	mov	r0, #3
.L4051:
	bl	StorageSysDataStore
	mov	r4, r0
	b	.L4017
.L4007:
	bl	rknand_dev_flush
.L4052:
	mov	r4, #0
	b	.L4017
.L4057:
	mvn	r4, #1
.L4017:
	ldr	r0, .L4058+116
	mov	r1, r4
	bl	printk
	mov	r0, r4
	b	.L4002
.L4038:
	mvn	r0, #21
.L4002:
	add	sp, sp, #524
	@ sp needed
	ldmfd	sp!, {r4, r5, r6, r7, pc}
.L4059:
	.align	2
.L4058:
	.word	1074031656
	.word	1074029694
	.word	1074034192
	.word	1074034194
	.word	.LC186
	.word	.LC187
	.word	1263358532
	.word	.LANCHOR4
	.word	-1067903959
	.word	.LC188
	.word	.LC189
	.word	1112753220
	.word	1146313043
	.word	.LC190
	.word	1112755781
	.word	.LC191
	.word	1094995539
	.word	.LANCHOR4+2056
	.word	1074031666
	.word	.LC192
	.word	1074031676
	.word	.LC193
	.word	.LC194
	.word	1280262987
	.word	.LC195
	.word	.LC196
	.word	1145980246
	.word	.LC197
	.word	1074034193
	.word	.LC198
	.fnend
	.size	rknand_sys_storage_ioctl, .-rknand_sys_storage_ioctl
	.align	2
	.global	rk_ftl_storage_sys_init
	.type	rk_ftl_storage_sys_init, %function
rk_ftl_storage_sys_init:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	stmfd	sp!, {r3, r4, r5, r6, r7, lr}
	.save {r3, r4, r5, r6, r7, lr}
	mov	r2, #512
	ldr	r4, .L4070
	mvn	r3, #0
	ldr	r0, .L4070+4
	mov	r5, #0
	ldr	r1, [r4, #2012]
	str	r3, [r4, #2036]
	add	r3, r1, #512
	str	r3, [r4, #2040]
	str	r1, [r4, #2052]
	add	r3, r1, #1024
	add	r1, r1, #1536
	str	r3, [r4, #2572]
	strb	r5, [r4, #2028]
	str	r5, [r4, #2032]
	str	r5, [r4, #2580]
	str	r1, [r4, #2576]
	bl	ftl_memcpy
	ldr	r6, [r4, #2052]
	str	r5, [r4, #2044]
	str	r5, [r4, #2568]
	ldr	r7, [r6, #508]
	ldr	r3, [r6, #16]
	cmp	r7, r5
	str	r3, [r4, #2048]
	beq	.L4061
	mov	r0, r6
	mov	r1, #508
	bl	js_hash
	cmp	r7, r0
	beq	.L4061
	str	r5, [r6, #16]
	ldr	r0, .L4070+8
	str	r5, [r4, #2048]
	bl	printk
.L4061:
	ldr	r3, [r4, #2048]
	mov	r0, #2
	ldr	r1, [r4, #2572]
	cmp	r3, #0
	ldrne	r3, .L4070
	ldrne	r2, .L4070+12
	strne	r2, [r3, #2044]
	bl	StorageSysDataLoad
	ldr	r1, [r4, #2576]
	mov	r0, #3
	bl	StorageSysDataLoad
	ldmfd	sp!, {r3, r4, r5, r6, r7, lr}
	b	rknand_sys_storage_init
.L4071:
	.align	2
.L4070:
	.word	.LANCHOR4
	.word	.LANCHOR4+2056
	.word	.LC199
	.word	-1067903959
	.fnend
	.size	rk_ftl_storage_sys_init, .-rk_ftl_storage_sys_init
	.align	2
	.global	StorageSysDataDeInit
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
	.type	rk_ftl_vendor_storage_init, %function
rk_ftl_vendor_storage_init:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	stmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, lr}
	.save {r4, r5, r6, r7, r8, r9, r10, lr}
	mov	r0, #65536
	bl	ftl_malloc
	ldr	r6, .L4084
	cmp	r0, #0
	str	r0, [r6, #2584]
	beq	.L4079
	ldr	r9, .L4084+4
	mov	r8, #0
	mov	r4, r8
	mov	r7, r8
	mov	r10, r6
.L4077:
	mov	r0, r7, asl #7
	mov	r1, #128
	ldr	r2, [r6, #2584]
	bl	FlashBootVendorRead
	cmp	r0, #0
	bne	.L4075
	ldr	r3, [r10, #2584]
	ldr	r0, .L4084+8
	add	r2, r3, #61440
	ldr	r1, [r3]
	ldr	r2, [r2, #4092]
	ldr	r3, [r3, #4]
	bl	printk
	ldr	r5, [r10, #2584]
	ldr	r3, [r5]
	cmp	r3, r9
	bne	.L4076
	add	r2, r5, #61440
	ldr	r3, [r5, #4]
	ldr	r1, [r2, #4092]
	cmp	r4, r3
	movcs	r2, #0
	movcc	r2, #1
	cmp	r1, r3
	movne	r2, #0
	cmp	r2, #0
	movne	r8, r7
	movne	r4, r3
.L4076:
	cmp	r7, #1
	movne	r7, #1
	bne	.L4077
.L4083:
	cmp	r4, #0
	beq	.L4078
	mov	r0, r8, asl #7
	mov	r1, #128
	mov	r2, r5
	bl	FlashBootVendorRead
	cmp	r0, #0
	bne	.L4075
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, pc}
.L4078:
	mov	r0, r5
	mov	r1, r4
	mov	r2, #65536
	bl	memset
	ldr	r3, .L4084+4
	str	r7, [r5, #4]
	mov	r0, r4
	str	r3, [r5]
	add	r3, r5, #61440
	str	r7, [r3, #4092]
	ldr	r3, .L4084+12
	strh	r4, [r5, #12]	@ movhi
	strh	r3, [r5, #14]	@ movhi
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, pc}
.L4075:
	ldr	r0, [r6, #2584]
	bl	kfree
	mov	r3, #0
	mvn	r0, #0
	str	r3, [r6, #2584]
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, pc}
.L4079:
	mvn	r0, #11
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, pc}
.L4085:
	.align	2
.L4084:
	.word	.LANCHOR4
	.word	1380668996
	.word	.LC200
	.word	-1032
	.fnend
	.size	rk_ftl_vendor_storage_init, .-rk_ftl_vendor_storage_init
	.align	2
	.global	rk_ftl_vendor_read
	.type	rk_ftl_vendor_read, %function
rk_ftl_vendor_read:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	stmfd	sp!, {r3, r4, r5, lr}
	.save {r3, r4, r5, lr}
	ldr	r3, .L4093
	ldr	ip, [r3, #2584]
	cmp	ip, #0
	ldrneh	r4, [ip, #10]
	movne	r3, #0
	beq	.L4091
.L4088:
	cmp	r3, r4
	bcs	.L4091
	add	lr, ip, r3, asl #3
	ldrh	r5, [lr, #16]
	cmp	r5, r0
	bne	.L4089
	ldrh	r3, [lr, #20]
	mov	r0, r1
	ldrh	r1, [lr, #18]
	mov	r4, r2
	cmp	r2, r3
	add	r1, r1, #1024
	movcs	r4, r3
	add	r1, ip, r1
	mov	r2, r4
	bl	memcpy
	mov	r0, r4
	ldmfd	sp!, {r3, r4, r5, pc}
.L4089:
	add	r3, r3, #1
	b	.L4088
.L4091:
	mvn	r0, #0
	ldmfd	sp!, {r3, r4, r5, pc}
.L4094:
	.align	2
.L4093:
	.word	.LANCHOR4
	.fnend
	.size	rk_ftl_vendor_read, .-rk_ftl_vendor_read
	.align	2
	.global	rk_ftl_vendor_write
	.type	rk_ftl_vendor_write, %function
rk_ftl_vendor_write:
	.fnstart
	@ args = 0, pretend = 0, frame = 24
	@ frame_needed = 0, uses_anonymous_args = 0
	stmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.save {r4, r5, r6, r7, r8, r9, r10, fp, lr}
	mov	r9, r2
	ldr	r2, .L4118
	.pad #28
	sub	sp, sp, #28
	ldr	r4, [r2, #2584]
	cmp	r4, #0
	beq	.L4110
	mov	r3, r1
	add	r7, r9, #63
	ldrh	r1, [r4, #8]
	bic	r7, r7, #63
	ldrh	r2, [r4, #10]
	mov	ip, r0
	mov	r6, #0
	str	r1, [sp, #4]
.L4097:
	cmp	r6, r2
	bcs	.L4116
	add	r5, r4, r6, asl #3
	ldrh	r1, [r5, #16]
	cmp	r1, ip
	bne	.L4098
	ldrh	r1, [r5, #20]
	add	fp, r4, #1024
	add	r1, r1, #63
	bic	r1, r1, #63
	str	r1, [sp, #8]
	cmp	r9, r1
	bls	.L4099
	ldrh	r1, [r4, #14]
	cmp	r1, r7
	subcs	r2, r2, #1
	strcs	r2, [sp, #12]
	ldrcsh	r8, [r5, #18]
	bcc	.L4110
.L4100:
	ldr	r2, [sp, #12]
	add	r5, r5, #8
	cmp	r6, r2
	bcs	.L4117
	ldrh	r10, [r5, #20]
	add	r0, fp, r8
	ldrh	r2, [r5, #16]
	add	r6, r6, #1
	ldrh	r1, [r5, #18]
	strh	r10, [r5, #12]	@ movhi
	add	r10, r10, #63
	bic	r10, r10, #63
	strh	r2, [r5, #8]	@ movhi
	strh	r8, [r5, #10]	@ movhi
	add	r1, fp, r1
	mov	r2, r10
	str	r3, [sp, #20]
	str	ip, [sp, #16]
	bl	memcpy
	add	r8, r8, r10
	ldr	r3, [sp, #20]
	ldr	ip, [sp, #16]
	b	.L4100
.L4117:
	add	r6, r4, r6, asl #3
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
	add	r5, r3, r5
	rsb	r7, r7, r5
	strh	r7, [r4, #14]	@ movhi
	b	.L4115
.L4099:
	ldrh	r0, [r5, #18]
	mov	r1, r3
	mov	r2, r9
	add	r0, fp, r0
	bl	memcpy
	strh	r9, [r5, #20]	@ movhi
	b	.L4115
.L4098:
	add	r6, r6, #1
	b	.L4097
.L4116:
	ldrh	r1, [r4, #14]
	cmp	r1, r7
	bcc	.L4110
	add	r2, r4, r2, asl #3
	uxth	r7, r7
	rsb	r1, r7, r1
	strh	ip, [r2, #16]	@ movhi
	ldrh	r0, [r4, #12]
	strh	r9, [r2, #20]	@ movhi
	strh	r0, [r2, #18]	@ movhi
	add	r0, r7, r0
	strh	r1, [r4, #14]	@ movhi
	mov	r1, r3
	strh	r0, [r4, #12]	@ movhi
	ldrh	r0, [r2, #18]
	mov	r2, r9
	add	r0, r0, #1024
	add	r0, r4, r0
	bl	memcpy
	ldrh	r3, [r4, #10]
	add	r3, r3, #1
	strh	r3, [r4, #10]	@ movhi
.L4115:
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
	mov	r0, r3, asl #7
	bl	FlashBootVendorWrite
	mov	r0, #0
	b	.L4096
.L4110:
	mvn	r0, #0
.L4096:
	add	sp, sp, #28
	@ sp needed
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L4119:
	.align	2
.L4118:
	.word	.LANCHOR4
	.fnend
	.size	rk_ftl_vendor_write, .-rk_ftl_vendor_write
	.align	2
	.global	rk_ftl_vendor_storage_ioctl
	.type	rk_ftl_vendor_storage_ioctl, %function
rk_ftl_vendor_storage_ioctl:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	stmfd	sp!, {r4, r5, r6, lr}
	.save {r4, r5, r6, lr}
	mov	r0, #4096
	mov	r5, r2
	mov	r6, r1
	bl	ftl_malloc
	subs	r4, r0, #0
	mvneq	r5, #0
	beq	.L4121
	ldr	r3, .L4137
	cmp	r6, r3
	beq	.L4123
	add	r3, r3, #1
	cmp	r6, r3
	beq	.L4124
	b	.L4135
.L4123:
	mov	r1, r5
	mov	r2, #8
	bl	rk_copy_from_user
	cmp	r0, #0
	bne	.L4135
	ldr	r2, [r4]
	ldr	r3, .L4137+4
	cmp	r2, r3
	beq	.L4126
.L4127:
	mvn	r5, #0
	b	.L4122
.L4126:
	ldrh	r0, [r4, #4]
	add	r1, r4, #8
	ldrh	r2, [r4, #6]
	bl	rk_ftl_vendor_read
	cmn	r0, #1
	beq	.L4127
	uxth	r2, r0
	strh	r0, [r4, #6]	@ movhi
	mov	r1, r4
	mov	r0, r5
	add	r2, r2, #8
	bl	rk_copy_to_user
	cmp	r0, #0
	moveq	r5, #0
	mvnne	r5, #13
	b	.L4122
.L4124:
	mov	r1, r5
	mov	r2, #8
	bl	rk_copy_from_user
	cmp	r0, #0
	bne	.L4135
	ldr	r2, [r4]
	ldr	r3, .L4137+4
	cmp	r2, r3
	bne	.L4127
	ldrh	r2, [r4, #6]
	movw	r3, #4087
	cmp	r2, r3
	bhi	.L4127
	mov	r0, r4
	mov	r1, r5
	add	r2, r2, #8
	bl	rk_copy_from_user
	cmp	r0, #0
	bne	.L4135
	ldrh	r0, [r4, #4]
	add	r1, r4, #8
	ldrh	r2, [r4, #6]
	bl	rk_ftl_vendor_write
	mov	r5, r0
	b	.L4122
.L4135:
	mvn	r5, #13
.L4122:
	mov	r0, r4
	bl	kfree
.L4121:
	mov	r0, r5
	ldmfd	sp!, {r4, r5, r6, pc}
.L4138:
	.align	2
.L4137:
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
	.align	2
.LANCHOR3 = . + 0
	.type	__func__.23841, %object
	.size	__func__.23841, 11
__func__.23841:
	.ascii	"FtlMemInit\000"
.LC0:
	.byte	60
	.byte	40
	.byte	24
	.byte	16
	.space	1
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
	.type	__func__.24620, %object
	.size	__func__.24620, 17
__func__.24620:
	.ascii	"FtlDumpBlockInfo\000"
	.space	3
	.type	__func__.24639, %object
	.size	__func__.24639, 16
__func__.24639:
	.ascii	"FtlScanAllBlock\000"
	.type	__func__.24907, %object
	.size	__func__.24907, 17
__func__.24907:
	.ascii	"ftl_scan_all_ppa\000"
	.space	3
	.type	__func__.24588, %object
	.size	__func__.24588, 12
__func__.24588:
	.ascii	"FtlCheckVpc\000"
	.type	__func__.24887, %object
	.size	__func__.24887, 21
__func__.24887:
	.ascii	"FtlVpcCheckAndModify\000"
	.space	3
	.type	__func__.23914, %object
	.size	__func__.23914, 8
__func__.23914:
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
	.ascii	"FTL version: 5.0.63 20210616\000"
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
