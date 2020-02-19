/*
 * Copyright (c) 2016-2018, Fuzhou Rockchip Electronics Co., Ltd
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 * date: 2020-02-18
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
	.global	ftl_flash_suspend
	.type	ftl_flash_suspend, %function
ftl_flash_suspend:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	@ link register save eliminated.
	ldr	r3, .L87
	ldr	r2, [r3, #76]
	ldr	r1, [r2]
	str	r1, [r3, #80]
	ldr	r1, [r2, #4]
	str	r1, [r3, #84]
	ldr	r1, [r2, #8]
	str	r1, [r3, #88]
	ldr	r1, [r2, #12]
	str	r1, [r3, #92]
	ldr	r1, [r2, #304]
	str	r1, [r3, #96]
	ldr	r1, [r2, #308]
	str	r1, [r3, #100]
	ldr	r1, [r2, #336]
	ldr	r2, [r2, #344]
	str	r1, [r3, #104]
	str	r2, [r3, #108]
	bx	lr
.L88:
	.align	2
.L87:
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
	ldr	r0, .L95
	mov	r8, r3
	ldr	ip, [r5, #4]
	mov	r9, r1
	mov	r7, r2
	ldrh	r3, [r0, #126]
	bic	ip, ip, #-2147483648
	ldrh	r4, [r0, #124]
	ubfx	r6, ip, #10, #16
	ldrh	r10, [r0, #4]
	mov	fp, r0
	str	ip, [sp, #4]
	smulbb	r4, r4, r3
	ldrb	r3, [r0]	@ zero_extendqisi2
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
	bne	.L91
	ldrb	r3, [fp, #140]	@ zero_extendqisi2
	cmp	r3, #0
	ldreq	r3, .L95
	addeq	r4, r3, r4, asl #1
	ldreqh	r4, [r4, #144]
.L91:
	add	ip, fp, r0, asl #2
	ldrb	r3, [sp, #48]	@ zero_extendqisi2
	ldr	r1, [ip, #1168]
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
.L96:
	.align	2
.L95:
	.word	.LANCHOR0
	.fnend
	.size	LogAddr2PhyAddr, .-LogAddr2PhyAddr
	.align	2
	.global	FlashScheduleEnSet
	.type	FlashScheduleEnSet, %function
FlashScheduleEnSet:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	@ link register save eliminated.
	ldr	r3, .L98
	ldr	r2, [r3, #1200]
	str	r0, [r3, #1200]
	mov	r0, r2
	bx	lr
.L99:
	.align	2
.L98:
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
	ldr	r3, .L101
	ldr	r3, [r3, #1204]
	ldrb	r0, [r3, #9]	@ zero_extendqisi2
	bx	lr
.L102:
	.align	2
.L101:
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
	ldr	r3, .L105
	add	r0, r3, r0, asl #3
	ldrb	r2, [r0, #16]	@ zero_extendqisi2
	ldr	r0, [r0, #12]
	add	r2, r2, #8
	add	r0, r0, r2, asl #8
	bx	lr
.L106:
	.align	2
.L105:
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
	ldr	r3, .L108
	ldr	r2, [r3, #76]
	mov	r3, r0, asl #8
	orr	r0, r3, r0, asl #16
	orr	r3, r0, #1
	str	r3, [r2, #304]
	bx	lr
.L109:
	.align	2
.L108:
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
	ldr	r3, .L111
	orr	r0, r0, #16640
	ldr	r3, [r3, #76]
	str	r0, [r3, #344]
	bx	lr
.L112:
	.align	2
.L111:
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
	ldr	r3, .L116
	cmp	r0, #0
	ldr	r2, [r3, #76]
	ldr	r3, [r2]
	bfieq	r3, r0, #13, #1
	orrne	r3, r3, #253952
	str	r3, [r2]
	bx	lr
.L117:
	.align	2
.L116:
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
	ldr	r3, .L125
	ands	r1, r0, #6
	ldr	r2, [r3, #76]
	ldr	r3, [r2]
	bfieq	r3, r1, #13, #1
	beq	.L121
	orr	r3, r3, #24576
	movw	r1, #8322
	bfc	r3, #15, #1
	str	r1, [r2, #344]
	orr	r3, r3, #196608
	ldr	r1, .L125+4
	tst	r0, #4
	orrne	r3, r3, #32768
	str	r1, [r2, #304]
	mov	r1, #38
	str	r1, [r2, #308]
	mov	r1, #39
	str	r1, [r2, #308]
.L121:
	str	r3, [r2]
	mov	r0, #0
	bx	lr
.L126:
	.align	2
.L125:
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
	ldr	r2, .L128
	add	r0, r2, r0, asl #3
	mov	r2, #1
	ldr	r1, [r0, #12]
	ldrb	r0, [r0, #16]	@ zero_extendqisi2
	ldr	r3, [r1]
	mov	r2, r2, asl r0
	bfi	r3, r2, #0, #8
	str	r3, [r1]
	bx	lr
.L129:
	.align	2
.L128:
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
	ldr	r3, .L131
	add	r0, r3, r0, asl #3
	ldr	r2, [r0, #12]
	ldr	r3, [r2]
	bfc	r3, #0, #8
	bfc	r3, #17, #1
	str	r3, [r2]
	bx	lr
.L132:
	.align	2
.L131:
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
	ldr	r3, .L135
	add	r0, r0, #3
	umull	r0, r1, r0, r3
	ldr	r3, .L135+4
	ldr	r3, [r3, #8]
	mov	r0, r1, lsr #6
	blx	r3
	mov	r0, #0
	ldmfd	sp!, {r3, pc}
.L136:
	.align	2
.L135:
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
	ldr	r3, .L139
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
.L140:
	.align	2
.L139:
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
	add	r8, r1, r1, asl #2
	ldr	r9, .L151
	mov	r5, r0
	ldr	r7, .L151+4
	mov	r6, r1
	add	r10, r9, #256
	mov	r4, #0
.L142:
	ldrb	r3, [r7, #1209]	@ zero_extendqisi2
	cmp	r4, r3
	bcs	.L150
	mov	r3, #85
	str	r3, [r5, #8]
	ldrsb	r3, [r4, r10]
	mov	r0, #200
	str	r3, [r5, #4]
	bl	NandcDelayns
	ldrb	r3, [r7, #1208]	@ zero_extendqisi2
	cmp	r3, #34
	addeq	r3, r4, r8
	addeq	r3, r10, r3
	beq	.L149
	cmp	r3, #35
	addne	r3, r9, r6
	addne	r3, r3, #400
	ldrnesb	r3, [r3]
	bne	.L148
	ldr	r3, .L151+8
	add	r2, r4, r8
	add	r3, r3, r2
.L149:
	ldrsb	r3, [r3, #5]
.L148:
	str	r3, [r5]
	add	r4, r4, #1
	b	.L142
.L150:
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, pc}
.L152:
	.align	2
.L151:
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
	ldr	r3, .L158
	stmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, lr}
	.save {r4, r5, r6, r7, r8, r9, r10, lr}
	add	r1, r3, r1, asl #2
	ldr	r8, .L158+4
	mov	r4, #0
	add	r5, r1, #3
	mov	r6, r0
	mov	r7, r3
	mov	r9, #161
	mov	r10, r4
.L154:
	ldrb	r3, [r8, #1209]	@ zero_extendqisi2
	cmp	r4, r3
	bcs	.L157
	str	r9, [r6, #8]
	mov	r0, #300
	str	r10, [r6]
	ldrsb	r3, [r7, r4]
	add	r4, r4, #1
	str	r3, [r6]
	ldrsb	r3, [r5, #1]!
	str	r3, [r6]
	bl	NandcDelayns
	b	.L154
.L157:
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, pc}
.L159:
	.align	2
.L158:
	.word	.LANCHOR1+408
	.word	.LANCHOR0
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
	ldr	r5, .L169
	mov	r9, r2
	ldr	r4, .L169+4
	mov	r7, r0
	mov	r8, r1
	ldr	r3, [r5, #1204]
	ldrb	r2, [r3, #19]	@ zero_extendqisi2
	mov	r3, r0, asl #3
	cmp	r2, #6
	addeq	r4, r4, r0, asl #6
	addeq	r4, r4, #20
	addeq	r4, r4, r6, asl #2
	beq	.L162
	cmp	r2, #7
	bne	.L163
	mov	r2, #160
	mla	r4, r2, r0, r4
	add	r2, r6, r6, asl #2
	add	r4, r4, #28
	add	r4, r4, r2, asl #1
	b	.L162
.L163:
	cmp	r2, #8
	addne	r2, r6, r3
	addeq	r2, r6, r6, asl #2
	ldreq	r4, .L169+8
	addne	r4, r4, r2, asl #3
	addeq	r4, r4, r2
	addne	r4, r4, #20
.L162:
	add	r3, r5, r3
	mov	r0, r7
	sub	r8, r8, #1
	sub	r4, r4, #1
	ldrb	fp, [r3, #16]	@ zero_extendqisi2
	ldr	r10, [r3, #12]
	bl	NandcFlashCs
	sub	ip, r9, #1
	add	r9, r9, r8
	mov	r3, fp, asl #8
	mov	r2, #54
	add	fp, r10, r3
	str	r2, [fp, #2056]
.L165:
	cmp	ip, r9
	beq	.L168
	ldrb	r2, [ip, #1]!	@ zero_extendqisi2
	mov	r0, #200
	str	r3, [sp, #4]
	str	r2, [fp, #2052]
	str	ip, [sp]
	bl	NandcDelayns
	ldrsb	r2, [r4, #1]!
	str	r2, [fp, #2048]
	ldr	r3, [sp, #4]
	ldr	ip, [sp]
	b	.L165
.L168:
	add	r10, r10, r3
	mov	r0, r7
	mov	r3, #22
	add	r5, r5, r7
	str	r3, [r10, #2056]
	bl	NandcFlashDeCs
	strb	r6, [r5, #2064]
	add	sp, sp, #12
	@ sp needed
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L170:
	.align	2
.L169:
	.word	.LANCHOR0
	.word	.LANCHOR0+1210
	.word	.LANCHOR0+1238
	.fnend
	.size	HynixSetRRPara, .-HynixSetRRPara
	.align	2
	.global	FlashSetReadRetryDefault
	.type	FlashSetReadRetryDefault, %function
FlashSetReadRetryDefault:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	ldr	r3, .L179
	ldr	r2, [r3, #1204]
	ldrb	r2, [r2, #19]	@ zero_extendqisi2
	sub	r2, r2, #1
	cmp	r2, #7
	bxhi	lr
	stmfd	sp!, {r4, r5, r6, lr}
	.save {r4, r5, r6, lr}
	mov	r4, #0
	ldr	r6, .L179+4
	mov	r5, r3
.L172:
	ldrb	r3, [r6, r4, asl #3]	@ zero_extendqisi2
	uxtb	r0, r4
	cmp	r3, #173
	bne	.L173
	ldrb	r1, [r5, #1211]	@ zero_extendqisi2
	mov	r3, #0
	ldr	r2, .L179+8
	bl	HynixSetRRPara
.L173:
	add	r4, r4, #1
	cmp	r4, #4
	bne	.L172
	ldmfd	sp!, {r4, r5, r6, pc}
.L180:
	.align	2
.L179:
	.word	.LANCHOR0
	.word	.LANCHOR0+2068
	.word	.LANCHOR0+1214
	.fnend
	.size	FlashSetReadRetryDefault, .-FlashSetReadRetryDefault
	.align	2
	.global	FlashReadStatusEN
	.type	FlashReadStatusEN, %function
FlashReadStatusEN:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	ldr	ip, .L192
	stmfd	sp!, {r3, r4, r5, lr}
	.save {r3, r4, r5, lr}
	add	r0, ip, r0, asl #3
	ldr	r3, [ip, #1204]
	ldrb	r5, [r0, #16]	@ zero_extendqisi2
	ldr	r4, [r0, #12]
	ldrb	r3, [r3, #8]	@ zero_extendqisi2
	cmp	r3, #2
	mov	r3, r5, asl #8
	addne	r3, r4, r3
	add	r5, r5, #8
	movne	r2, #112
	strne	r2, [r3, #2056]
	bne	.L186
	cmp	r2, #0
	add	r3, r4, r3
	ldrneb	r2, [ip, #58]	@ zero_extendqisi2
	ldreqb	r2, [ip, #57]	@ zero_extendqisi2
	str	r2, [r3, #2056]
	ldrb	r0, [ip, #59]	@ zero_extendqisi2
	cmp	r0, #0
	addne	ip, r4, r5, asl #8
	movne	r2, #0
	beq	.L186
.L185:
	cmp	r2, r0
	bcs	.L186
	mov	r3, r2, asl #3
	add	r2, r2, #1
	mov	r3, r1, lsr r3
	uxtb	r3, r3
	str	r3, [ip, #4]
	b	.L185
.L186:
	mov	r0, #80
	bl	NandcDelayns
	ldr	r0, [r4, r5, asl #8]
	uxtb	r0, r0
	ldmfd	sp!, {r3, r4, r5, pc}
.L193:
	.align	2
.L192:
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
.L198:
	mov	r0, r4
	mov	r1, r5
	mov	r2, r6
	bl	FlashReadStatusEN
	cmp	r0, #255
	beq	.L198
	tst	r0, #64
	beq	.L198
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
	ldr	r5, .L210
	add	r4, r5, r0, asl #4
	ldr	r3, [r4, #2108]
	ldrb	r7, [r4, #2100]	@ zero_extendqisi2
	cmp	r3, #0
	beq	.L204
	mov	r6, r0
	mov	r0, r7
	add	r5, r5, r6, asl #2
	bl	NandcFlashCs
	ldr	r1, [r4, #2104]
	mov	r0, r7
	ldr	r2, [r5, #1168]
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
.L204:
	mov	r0, #0
	ldmfd	sp!, {r3, r4, r5, r6, r7, pc}
.L211:
	.align	2
.L210:
	.word	.LANCHOR0
	.fnend
	.size	FlashWaitCmdDone, .-FlashWaitCmdDone
	.align	2
	.type	flash_read_ecc, %function
flash_read_ecc:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	ldr	r2, .L214
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
.L215:
	.align	2
.L214:
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
	ldr	r3, .L222
	stmfd	sp!, {r0, r1, r2, r4, r5, lr}
	.save {r4, r5, lr}
	.pad #12
	add	r0, r3, r0, asl #3
	ldr	r4, .L222+4
	ldr	r5, [r0, #12]
.L218:
	ldr	r3, [r5]
	str	r3, [sp, #4]
	ldr	r3, [sp, #4]
	tst	r3, #512
	bne	.L219
	mov	r0, #10
	bl	NandcDelayns
	subs	r4, r4, #1
	bne	.L218
	mvn	r0, #0
	b	.L217
.L219:
	mov	r0, #0
.L217:
	add	sp, sp, #12
	@ sp needed
	ldmfd	sp!, {r4, r5, pc}
.L223:
	.align	2
.L222:
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
	ldr	r3, .L230
	stmfd	sp!, {r0, r1, r2, r4, r5, lr}
	.save {r4, r5, lr}
	.pad #12
	add	r0, r3, r0, asl #3
	ldr	r4, .L230+4
	ldr	r5, [r0, #12]
	mov	r0, #150
	bl	NandcDelayns
.L226:
	ldr	r3, [r5]
	str	r3, [sp, #4]
	ldr	r3, [sp, #4]
	tst	r3, #512
	bne	.L227
	mov	r0, #10
	bl	NandcDelayns
	subs	r4, r4, #1
	bne	.L226
	mvn	r0, #0
	b	.L225
.L227:
	mov	r0, #0
.L225:
	add	sp, sp, #12
	@ sp needed
	ldmfd	sp!, {r4, r5, pc}
.L231:
	.align	2
.L230:
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
	ldr	r3, .L234
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
.L235:
	.align	2
.L234:
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
	ldr	r4, .L239
	ldrb	r3, [r4, #140]	@ zero_extendqisi2
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
.L240:
	.align	2
.L239:
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
	ldr	r4, .L244
	ldrb	r3, [r4, #140]	@ zero_extendqisi2
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
.L245:
	.align	2
.L244:
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
	ldr	r1, .L271
	stmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.save {r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.pad #20
	sub	sp, sp, #20
	ldrb	fp, [r1, #2228]	@ zero_extendqisi2
	mov	lr, #0
	ldr	r7, .L271+4
	mov	r4, #239
	and	r2, fp, #4
	and	r3, fp, #1
	mov	r5, #128
	str	r3, [sp, #8]
	mov	r6, #1
	uxtb	r3, r2
	mov	r8, #35
	mov	r2, lr
	mov	r9, #32
	mov	r10, #5
	str	r3, [sp, #4]
	add	r3, r1, #12
	str	r3, [sp, #12]
.L258:
	ldr	r3, [sp, #12]
	ldrb	ip, [lr, r7]	@ zero_extendqisi2
	ldr	r1, [r3, lr]!
	cmp	ip, #69
	cmpne	ip, #152
	ldrb	r3, [r3, #4]	@ zero_extendqisi2
	beq	.L249
	cmp	ip, #44
	cmpne	ip, #173
	bne	.L250
.L249:
	cmp	r0, #1
	bne	.L251
	ldr	fp, [sp, #8]
	cmp	fp, #0
	beq	.L250
	mov	r3, r3, asl #8
	cmp	ip, #173
	add	fp, r1, r3
	str	r4, [fp, #2056]
	streq	r0, [fp, #2052]
	beq	.L270
	cmp	ip, #44
	streq	r0, [fp, #2052]
	strne	r5, [fp, #2052]
	streq	r10, [fp, #2048]
	strne	r0, [fp, #2048]
	b	.L256
.L251:
	ldr	fp, [sp, #4]
	cmp	fp, #0
	beq	.L250
	mov	r3, r3, asl #8
	cmp	ip, #173
	add	fp, r1, r3
	str	r4, [fp, #2056]
	streq	r6, [fp, #2052]
	streq	r9, [fp, #2048]
	beq	.L256
	cmp	ip, #44
	streq	r6, [fp, #2052]
	streq	r8, [fp, #2048]
	beq	.L256
	str	r5, [fp, #2052]
.L270:
	str	r2, [fp, #2048]
.L256:
	add	r3, r1, r3
	str	r2, [r3, #2048]
	str	r2, [r3, #2048]
	str	r2, [r3, #2048]
.L250:
	add	lr, lr, #8
	cmp	lr, #32
	bne	.L258
	mov	r0, #0
	bl	NandcWaitFlashReady
	mov	r0, #0
	add	sp, sp, #20
	@ sp needed
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L272:
	.align	2
.L271:
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
	ldr	ip, .L275
	stmfd	sp!, {r3, r4, r5, lr}
	.save {r3, r4, r5, lr}
	add	ip, ip, r0, asl #3
	ldr	r3, .L275+4
	mov	r5, r2
	ldrb	r2, [ip, #16]	@ zero_extendqisi2
	ldr	r4, [ip, #12]
	ldrb	r3, [r3, #481]	@ zero_extendqisi2
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
.L276:
	.align	2
.L275:
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
	ldr	r2, .L279
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
.L280:
	.align	2
.L279:
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
	ldr	r0, .L289
	ldr	r1, .L289+4
	add	r4, r4, r4, asl #2
	sub	ip, r0, #48
	mov	r2, #0
.L282:
	ldrb	r3, [r1, #1209]	@ zero_extendqisi2
	cmp	r2, r3
	bcs	.L288
	ldrb	r3, [r1, #1208]	@ zero_extendqisi2
	cmp	r3, #67
	add	r3, r2, r4
	addeq	r3, ip, r3
	addne	r3, r0, r3
	add	r2, r2, #1
	ldrsb	r3, [r3, #5]
	str	r3, [r5]
	b	.L282
.L288:
	mov	r0, #0
	ldmfd	sp!, {r3, r4, r5, lr}
	b	NandcWaitFlashReady
.L290:
	.align	2
.L289:
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
	ldr	r3, .L293
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
.L294:
	.align	2
.L293:
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
	ldr	r8, .L307
	.pad #20
	sub	sp, sp, #20
	mov	r6, r0
	mov	r9, r1
	mov	r7, r5
.L296:
	cmp	r7, r9
	beq	.L306
	rsb	r3, r7, r9
	add	r2, sp, #8
	add	r0, r6, r5
	mov	r1, #0
	uxtb	r3, r3
	str	r3, [sp]
	add	r3, sp, #12
	bl	LogAddr2PhyAddr
	ldrb	r2, [r8, #2229]	@ zero_extendqisi2
	ldr	r3, [sp, #12]
	cmp	r3, r2
	mvncs	r3, #0
	strcs	r3, [r6, r5]
	bcs	.L298
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
	bne	.L300
	ldr	r0, .L307+4
	ldr	r1, [sp, #8]
	bl	printk
.L300:
	mov	r0, r4
	bl	NandcFlashDeCs
.L298:
	add	r7, r7, #1
	add	r5, r5, #36
	b	.L296
.L306:
	mov	r0, #0
	add	sp, sp, #20
	@ sp needed
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, pc}
.L308:
	.align	2
.L307:
	.word	.LANCHOR0
	.word	.LC1
	.fnend
	.size	FlashEraseSLc2KBlocks, .-FlashEraseSLc2KBlocks
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
	ldr	r3, .L314
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
	bne	.L310
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
	b	.L313
.L310:
	str	lr, [r4, #2052]
	str	ip, [r4, #2052]
	str	r1, [r4, #2052]
	str	r3, [r4, #2056]
.L313:
	str	r9, [r4, #2052]
	mov	r3, #48
	str	r8, [r4, #2052]
	mov	r0, r7
	str	r5, [r4, #2052]
	mov	r1, r6
	str	r3, [r4, #2056]
	ldmfd	sp!, {r3, r4, r5, r6, r7, r8, r9, r10, fp, lr}
	b	FlashSetRandomizer
.L315:
	.align	2
.L314:
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
	ldr	r4, .L327
	bl	NandcWaitFlashReady
	bl	FlashSetReadRetryDefault
	ldr	r0, [r4, #2240]
	cmp	r0, #0
	beq	.L317
	mov	r0, #0
	bl	flash_enter_slc_mode
	b	.L318
.L317:
	bl	flash_exit_slc_mode
.L318:
	ldrb	r3, [r4, #2244]	@ zero_extendqisi2
	ldr	r5, .L327
	cmp	r3, #0
	beq	.L319
	ldrb	r3, [r5, #2228]	@ zero_extendqisi2
	tst	r3, #1
	beq	.L319
	mov	r0, #1
	bl	FlashSetInterfaceMode
	mov	r0, #1
	bl	NandcSetMode
	mov	r3, #0
	strb	r3, [r5, #2244]
.L319:
	ldr	r3, [r4, #12]
	mov	r0, #0
	str	r0, [r3, #336]
	ldmfd	sp!, {r3, r4, r5, pc}
.L328:
	.align	2
.L327:
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
	ldr	r3, .L330
	add	r0, r3, r0, asl #3
	ldr	r3, [r0, #12]
	str	r1, [r3, #336]
	bx	lr
.L331:
	.align	2
.L330:
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
	ldr	r1, .L343
	bl	__aeabi_idiv
	ldr	r3, .L343+4
	ldr	r3, [r3, #76]
	cmp	r0, #250
	movwgt	r2, #8354
	bgt	.L341
	cmp	r0, #220
	bgt	.L342
	cmp	r0, #185
	movwgt	r2, #4226
	bgt	.L341
	cmp	r0, #160
	movwgt	r2, #4194
	bgt	.L341
	cmp	r4, #35
	movwls	r2, #4193
	bls	.L341
	cmp	r4, #99
	movwls	r2, #4225
	bls	.L341
.L342:
	movw	r2, #8322
.L341:
	str	r2, [r3, #4]
	ldmfd	sp!, {r4, pc}
.L344:
	.align	2
.L343:
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
	bls	.L346
	movw	r3, #8322
	cmp	r0, r3
	bne	.L347
.L346:
	ldr	r3, .L348
	ldr	r3, [r3, #76]
	str	r0, [r3, #4]
.L347:
	ldr	r3, .L348+4
	ldrb	r0, [r3, #493]	@ zero_extendqisi2
	b	NandcTimeCfg
.L349:
	.align	2
.L348:
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
	ldr	r3, .L353
	mov	r2, #1
	mov	r5, #0
	str	r1, [r3, #16]
	mov	r4, r3
	str	r0, [r3, #12]
	str	r2, [r3, #24]
	mov	r2, #2
	str	r0, [r3, #20]
	str	r2, [r3, #32]
	mov	r2, #3
	str	r0, [r3, #28]
	str	r0, [r3, #36]
	str	r0, [r3, #76]
	str	r2, [r3, #40]
	ldr	r2, [r0]
	and	r2, r2, #253952
	ubfx	ip, r2, #13, #1
	bfi	r2, r1, #13, #1
	ldr	r1, [r0, #352]
	orr	r2, r2, #256
	str	ip, [r3, #2248]
	movw	ip, #2049
	ubfx	r1, r1, #16, #4
	str	r1, [r3, #2252]
	ldr	r1, [r0, #352]
	cmp	r1, ip
	str	r1, [r3, #2256]
	moveq	r3, #8
	streq	r3, [r4, #2252]
	str	r2, [r0]
	mov	r0, #40
	ldr	r3, [r4, #76]
	str	r5, [r3, #336]
	bl	NandcTimeCfg
	ldr	r3, [r4, #76]
	movw	r2, #8322
	mov	r0, #36864
	str	r2, [r3, #344]
	ldr	r2, .L353+4
	str	r2, [r3, #304]
	bl	ftl_malloc
	str	r5, [r4, #2288]
	str	r5, [r4, #2296]
	str	r0, [r4, #2260]
	str	r0, [r4, #2264]
	add	r0, r0, #32768
	str	r0, [r4, #2268]
	ldmfd	sp!, {r3, r4, r5, pc}
.L354:
	.align	2
.L353:
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
	ldr	ip, .L357
	str	lr, [sp, #-4]!
	.save {lr}
	ldr	lr, [ip, #76]
	ldr	lr, [lr, #4]
	str	lr, [r0]
	ldr	r0, [ip, #76]
	ldr	r0, [r0]
	str	r0, [r1]
	ldr	r1, [ip, #76]
	ldr	r1, [r1, #304]
	str	r1, [r2]
	ldr	r1, [ip, #76]
	ldr	r2, [r1, #308]
	ldr	r1, [r1, #344]
	uxtb	r2, r2
	orr	r2, r2, r1, asl #16
	str	r2, [r3]
	ldr	pc, [sp], #4
.L358:
	.align	2
.L357:
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
	ldr	r3, .L367
	mov	r1, #1
	ldr	r2, [r3, #76]
	str	r0, [r3, #2300]
	mov	r3, #0
	str	r1, [r2, #8]
	mov	r1, #16
	cmp	r0, r1
	bfi	r3, r1, #8, #8
	bfc	r3, #18, #1
	bne	.L360
.L363:
	bfc	r3, #4, #1
	b	.L361
.L360:
	cmp	r0, #24
	orreq	r3, r3, #16
	beq	.L361
	cmp	r0, #40
	orr	r3, r3, #262144
	orr	r3, r3, #16
	beq	.L363
.L361:
	orr	r3, r3, #1
	str	r3, [r2, #12]
	bx	lr
.L368:
	.align	2
.L367:
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
	ldr	r3, .L370
	strb	r0, [r3, #2304]
	b	NandcBchSel
.L371:
	.align	2
.L370:
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
	ldr	r3, .L381
	stmfd	sp!, {r4, r5, r6, lr}
	.save {r4, r5, r6, lr}
	mov	r5, #0
	ldr	r2, [r3, #76]
	mov	r4, r3
	ldr	r1, [r3, #80]
	ldr	r6, .L381+4
	str	r1, [r2]
	ldr	r1, [r3, #84]
	ldr	r2, [r3, #76]
	str	r1, [r2, #4]
	ldr	r1, [r3, #88]
	str	r1, [r2, #8]
	ldr	r1, [r3, #92]
	str	r1, [r2, #12]
	ldr	r1, [r3, #96]
	str	r1, [r2, #304]
	ldr	r1, [r3, #100]
	str	r1, [r2, #308]
	ldr	r1, [r3, #104]
	str	r1, [r2, #336]
	ldr	r1, [r3, #108]
	str	r1, [r2, #344]
.L374:
	ldrb	r3, [r6, r5, asl #3]	@ zero_extendqisi2
	sub	r3, r3, #1
	uxtb	r3, r3
	cmp	r3, #253
	bhi	.L373
	uxtb	r0, r5
	bl	FlashReset
.L373:
	add	r5, r5, #1
	cmp	r5, #4
	bne	.L374
	ldrb	r3, [r4, #2244]	@ zero_extendqisi2
	ldr	r5, .L381
	cmp	r3, #0
	beq	.L375
	mov	r0, #1
	bl	NandcSetMode
	ldrb	r0, [r5, #2228]	@ zero_extendqisi2
	bl	FlashSetInterfaceMode
	ldrb	r0, [r5, #2228]	@ zero_extendqisi2
	bl	NandcSetMode
	ldrb	r0, [r5, #97]	@ zero_extendqisi2
	bl	NandcSetDdrPara
.L375:
	ldr	r3, [r4, #1204]
	ldmfd	sp!, {r4, r5, r6, lr}
	ldrb	r0, [r3, #20]	@ zero_extendqisi2
	b	FlashBchSel
.L382:
	.align	2
.L381:
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
	bne	.L386
	mov	r0, r4
	ldmfd	sp!, {r4, lr}
	b	wait_for_nand_flash_ready
.L386:
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
	ldr	r4, .L424
	.pad #28
	sub	sp, sp, #28
	ldrb	r5, [r4]	@ zero_extendqisi2
	cmp	r5, #0
	moveq	r9, r0
	moveq	r10, r1
	moveq	fp, r4
	beq	.L389
	mov	r1, r2
	bl	FlashEraseSLc2KBlocks
	b	.L390
.L398:
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
	ldrb	r3, [r4, #2229]	@ zero_extendqisi2
	mov	r7, r0
	ldr	r0, [sp, #20]
	cmp	r0, r3
	mvncs	r3, #0
	strcs	r3, [r9, r6]
	bcc	.L421
.L392:
	add	r5, r5, #1
.L389:
	cmp	r5, r8
	bcc	.L398
	b	.L422
.L421:
	ldrb	r3, [fp, #2305]	@ zero_extendqisi2
	cmp	r3, #0
	add	r3, r4, r0, asl #4
	ldr	r3, [r3, #2108]
	moveq	r7, #0
	cmp	r3, #0
	beq	.L394
	uxtb	r0, r0
	bl	FlashWaitCmdDone
.L394:
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
	bne	.L396
	ldrb	r3, [fp, #140]	@ zero_extendqisi2
	cmp	r3, #0
	beq	.L396
	bl	flash_enter_slc_mode
	b	.L397
.L396:
	bl	flash_exit_slc_mode
.L397:
	ldr	r3, [sp, #20]
	mov	r0, r6
	ldr	r1, [sp, #16]
	add	r5, r5, r7
	add	r3, r4, r3, asl #2
	ldr	r2, [r3, #1168]
	adds	r2, r2, #0
	movne	r2, #1
	bl	FlashWaitReadyEN
	mov	r0, r6
	mov	r2, r7
	ldr	r1, [sp, #16]
	bl	FlashEraseCmd
	mov	r0, r6
	bl	NandcFlashDeCs
	b	.L392
.L422:
	ldr	r0, [r4, #76]
	mov	r5, #0
	bl	NandcIqrWaitFlashReady
	ldr	r6, .L424
	ldr	r7, .L424+4
.L399:
	ldrb	r2, [r4, #2229]	@ zero_extendqisi2
	ldr	r3, .L424
	cmp	r5, r2
	bcs	.L423
	uxtb	r0, r5
	bl	FlashWaitCmdDone
	cmp	r10, #1
	bne	.L400
	ldrb	r3, [r6, #140]	@ zero_extendqisi2
	cmp	r3, #0
	beq	.L400
	ldrb	r0, [r7, r5, asl #4]	@ zero_extendqisi2
	bl	flash_exit_slc_mode
.L400:
	add	r5, r5, #1
	b	.L399
.L423:
	ldr	r2, [r3, #2308]
	cmp	r2, #0
	bne	.L402
.L404:
	mov	r0, #0
	b	.L390
.L402:
	ldrb	r3, [r3, #2068]	@ zero_extendqisi2
	cmp	r3, #69
	bne	.L404
	mov	r3, #0
	mov	r2, #36
	mov	r1, r3
.L403:
	cmp	r3, r8
	beq	.L404
	mul	r0, r2, r3
	add	r3, r3, #1
	str	r1, [r9, r0]
	b	.L403
.L390:
	add	sp, sp, #28
	@ sp needed
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L425:
	.align	2
.L424:
	.word	.LANCHOR0
	.word	.LANCHOR0+2100
	.fnend
	.size	FlashEraseBlocks, .-FlashEraseBlocks
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
.L432:
	ldr	r3, [r0, #8]
	str	r3, [sp, #4]
	ldr	r3, [sp, #4]
	tst	r3, #1048576
	beq	.L432
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
	ldr	r5, .L454
	mov	r4, #0
	.pad #28
	sub	sp, sp, #28
	add	r0, r5, r0, asl #3
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
	ldr	r3, [r5, #2252]
	cmp	r3, #3
	bls	.L437
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
	beq	.L438
	cmp	r1, #0
	bne	.L439
.L447:
	add	r2, r2, #1
	cmp	r8, #0
	mov	r2, r2, asr #1
	movne	r0, r8
	bfi	r4, r2, #22, #6
	ldreq	r0, [r5, #2264]
	b	.L441
.L439:
	ldr	r3, [r5, #2300]
	mov	r9, r5
	cmp	r3, #25
	movcc	r3, #64
	movcs	r3, #128
	str	r3, [sp, #4]
	mov	r3, r2, lsr #1
	str	r3, [sp, #12]
	mov	r3, #0
	mov	r0, r3
.L443:
	ldr	lr, [sp, #12]
	cmp	r0, lr
	bcs	.L447
	ldr	lr, [sp, #8]
	mov	r10, r3, lsr #2
	add	r0, r0, #1
	cmp	lr, #0
	ldrneh	fp, [ip, #2]
	mvneq	fp, #0
	ldrneh	lr, [ip], #4
	ldreq	lr, [r9, #2268]
	orrne	lr, lr, fp, asl #16
	ldrne	fp, [r9, #2268]
	streq	fp, [lr, r10, asl #2]
	strne	lr, [fp, r10, asl #2]
	ldr	lr, [sp, #4]
	add	r3, r3, lr
	b	.L443
.L441:
	ldr	r3, [r5, #2268]
	ubfx	r10, r4, #22, #5
	mov	r9, r1
	str	r0, [r5, #2272]
	mov	r2, r9
	mov	r1, r10, asl #10
	str	r3, [r5, #2276]
	bl	rknand_dma_map_single
	mov	r2, r9
	mov	r1, r10, asl #7
	clz	r9, r9
	mov	r9, r9, lsr #5
	str	r0, [r5, #2280]
	ldr	r0, [r5, #2276]
	bl	rknand_dma_map_single
	mov	r3, #1
	str	r3, [r5, #2288]
	tst	r8, #3
	ldr	r3, [r5, #2280]
	str	r0, [r5, #2284]
	str	r3, [r6, #20]
	ldr	r3, [r5, #2284]
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
.L438:
	ldr	r3, [sp, #20]
	str	r3, [r6, #16]
.L437:
	str	r7, [r6, #12]
	str	r4, [r6, #8]
	orr	r4, r4, #4
	str	r4, [r6, #8]
	add	sp, sp, #28
	@ sp needed
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L455:
	.align	2
.L454:
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
.L457:
	cmp	r2, r0
	uxth	r3, r1
	add	r1, r1, #1
	movls	r2, r2, asl #1
	bls	.L457
.L459:
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
	ldr	r3, .L463
	movw	r2, #2316
	cmp	r0, #23
	movw	r1, #2326
	ldrh	r2, [r3, r2]
	movls	r0, #24
	ldrh	r1, [r3, r1]
	str	r0, [r3, #2312]
	mul	r2, r2, r0
	rsb	r0, r0, r1
	movw	r1, #2324
	strh	r0, [r3, r1]	@ movhi
	mov	r0, #0
	ldr	r1, [r3, #2332]
	str	r2, [r3, #2320]
	rsb	r2, r2, r1
	str	r2, [r3, #2328]
	bx	lr
.L464:
	.align	2
.L463:
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
	movw	r1, #2340
	ldr	r4, .L493
	mov	r5, r0
	ldrh	r6, [r0, #8]
	.pad #20
	sub	sp, sp, #20
	add	r3, r4, #2336
	ldrh	r2, [r0, #10]
	ldrh	lr, [r0, #14]
	strh	r6, [r3]	@ movhi
	movw	r3, #2338
	strh	r2, [r4, r3]	@ movhi
	ldrh	r3, [r0, #12]
	ldr	r0, .L493+4
	strh	r3, [r4, r1]	@ movhi
	movw	r1, #2326
	strh	lr, [r4, r1]	@ movhi
	mov	r1, #0
.L466:
	strb	r1, [r1, r0]
	add	r1, r1, #1
	cmp	r1, #32
	bne	.L466
	ldrh	r0, [r5, #14]
	ldrh	r1, [r5, #20]
	cmp	r1, r0, lsr #8
	bcs	.L467
	uxtb	r10, r3
	ldr	r9, .L493+4
	mov	r1, r10, asl #1
	uxtb	r1, r1
	str	r1, [sp, #4]
	sub	r1, r2, #1
	mul	r1, r3, r1
	str	r1, [sp]
	mov	r1, #0
.L468:
	cmp	r1, r3
	bcs	.L470
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
.L471:
	cmp	r8, r2
	add	ip, ip, r3
	bcs	.L492
	ldr	r7, [sp, #12]
	add	fp, r0, r10
	add	r8, r8, #1
	strb	r0, [r7, ip]
	ldr	r7, [sp, #8]
	strb	fp, [r7, ip]
	ldr	r7, [sp, #4]
	add	r0, r0, r7
	uxtb	r0, r0
	b	.L471
.L492:
	add	r1, r1, #1
	b	.L468
.L470:
	movw	r1, #2338
	mov	r2, r2, asl #1
	strh	r2, [r4, r1]	@ movhi
	movw	r2, #2326
	mov	lr, lr, lsr #1
	strh	lr, [r4, r2]	@ movhi
.L467:
	cmp	r6, #1
	movw	r2, #2376
	mov	r1, #5
	strh	r1, [r4, r2]	@ movhi
	movw	r1, #2378
	mov	r0, #0
	strh	r0, [r4, r1]	@ movhi
	ldreq	r1, .L493
	ldrb	r9, [r4]	@ zero_extendqisi2
	ldrh	r10, [r5, #16]
	streqh	r6, [r1, r2]	@ movhi
	cmp	r9, #0
	movw	r2, #2380
	mov	r1, #4352
	strh	r1, [r4, r2]	@ movhi
	movne	r0, #384
	ldrne	r1, .L493
	strneh	r0, [r1, r2]	@ movhi
	movw	r1, #2338
	ldrh	r8, [r4, r1]
	movw	r1, #2316
	ldr	r2, .L493
	smulbb	r8, r8, r3
	add	r7, r2, #2384
	str	r2, [sp, #8]
	strh	r10, [r7]	@ movhi
	uxth	r8, r8
	strh	r8, [r4, r1]	@ movhi
	movw	r1, #2326
	ldrh	r6, [r4, r1]
	movw	r1, #2382
	smulbb	r0, r8, r10
	smulbb	r3, r6, r3
	strh	r3, [r4, r1]	@ movhi
	movw	r3, #2386
	ldrh	r1, [r5, #18]
	strh	r1, [r4, r3]	@ movhi
	movw	r3, #2388
	strh	r0, [r4, r3]	@ movhi
	movw	r0, #2390
	ldrh	r3, [r5, #20]
	str	r1, [sp, #4]
	strh	r3, [r4, r0]	@ movhi
	mov	r0, r3
	str	r3, [sp]
	bl	Ftl_log2
	movw	ip, #2394
	cmp	r6, #1024
	ldr	r3, [sp]
	mov	fp, r0
	movw	r0, #2392
	ldmib	sp, {r1, r2}
	strh	fp, [r4, r0]	@ movhi
	mov	r0, r3, asl #9
	mul	r1, r1, r3
	uxth	r0, r0
	strh	r0, [r4, ip]	@ movhi
	movw	ip, #2396
	mov	r0, r0, lsr #8
	strh	r0, [r4, ip]	@ movhi
	ldrh	ip, [r5, #26]
	movw	r0, #2398
	ldr	r5, .L493
	strh	ip, [r4, r0]	@ movhi
	movwhi	ip, #2378
	mul	r0, r6, r8
	str	r0, [r4, #2332]
	uxtbhi	r0, r6
	strhih	r0, [r2, ip]	@ movhi
	movw	r2, #2378
	ldrh	r2, [r4, r2]
	rsb	r2, r2, r6
	mov	r6, r6, asl #6
	mul	r2, r2, r8
	mul	r2, r3, r2
	mul	r10, r10, r2
	movw	r2, #2380
	ldrh	r0, [r4, r2]
	mov	r0, r0, asl #3
	mov	r10, r10, asr #11
	str	r10, [r4, #2400]
	bl	__aeabi_idiv
	movw	r3, #2404
	mov	r1, r8
	ldr	r10, .L493
	uxth	r0, r0
	cmp	r0, #4
	strhih	r0, [r5, r3]	@ movhi
	movls	r2, #4
	strlsh	r2, [r5, r3]	@ movhi
	cmp	r9, #0
	movwne	r3, #2380
	movne	r2, #640
	strneh	r2, [r4, r3]	@ movhi
	movw	r3, #2380
	ldrh	r3, [r4, r3]
	movw	r2, #2406
	mov	r3, r3, asr fp
	add	fp, fp, #9
	mov	r5, r6, asr fp
	add	r3, r3, #2
	strh	r3, [r4, r2]	@ movhi
	movw	r3, #2408
	strh	r5, [r4, r3]	@ movhi
	uxth	r5, r5
	ldr	r6, .L493
	mul	r3, r8, r5
	add	r5, r5, #8
	str	r3, [r4, #2412]
	movw	r3, #2404
	ldrh	r0, [r4, r3]
	bl	__aeabi_uidiv
	cmp	r8, #1
	uxtah	r0, r5, r0
	strne	r0, [r10, #2312]
	addeq	r5, r0, #4
	streq	r5, [r10, #2312]
	ldr	r3, [r4, #2312]
	uxth	r0, r3
	bl	FtlSysBlkNumInit
	ldr	r3, [r4, #2312]
	ldr	r2, [r4, #2328]
	mov	r0, #2048
	str	r3, [r4, #2416]
	ldrh	r3, [r7]
	mov	r2, r2, asl #2
	mul	r3, r3, r2
	movw	r2, #2392
	ldrh	r5, [r4, r2]
	add	r5, r5, #9
	mov	r5, r3, lsr r5
	movw	r3, #2420
	add	r5, r5, #2
	uxth	r5, r5
	strh	r5, [r4, r3]	@ movhi
	movw	r3, #2390
	ldrh	r7, [r4, r3]
	mov	r1, r7
	bl	__aeabi_idiv
	movw	r1, #2404
	ldrh	r2, [r4, r1]
	movw	r3, #2422
	ldrb	ip, [r4, #140]	@ zero_extendqisi2
	cmp	ip, #0
	strh	r0, [r4, r3]	@ movhi
	mov	r3, #0
	str	r3, [r4, #2424]
	add	r3, r2, #3
	strh	r3, [r4, r1]	@ movhi
	addne	r2, r2, #4
	ldr	r3, [r4, #2412]
	strneh	r2, [r6, r1]	@ movhi
	add	r0, r3, #3
	addne	r3, r3, #5
	str	r0, [r4, #2412]
	bne	.L491
	cmp	r0, #7
	bhi	.L481
	mov	r3, #8
.L491:
	str	r3, [r6, #2412]
.L481:
	movw	r2, #2428
	mov	r3, #0
	strh	r3, [r4, r2]	@ movhi
	movw	r3, #2324
	ldrh	r1, [r4, r3]
	mov	r0, #0
	mov	r3, r1, lsr #3
	add	r3, r3, r1, asl #1
	add	r3, r3, #52
	add	r5, r3, r5, asl #2
	cmp	r5, r7, asl #9
	ldrcc	r3, .L493
	movcc	r1, #1
	strcch	r1, [r3, r2]	@ movhi
	add	sp, sp, #20
	@ sp needed
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L494:
	.align	2
.L493:
	.word	.LANCHOR0
	.word	.LANCHOR0+2344
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
	ldr	r2, .L502
	movw	r3, #2430
	ldrh	r3, [r2, r3]
	cmp	r3, #0
	beq	.L501
	movw	r1, #2404
	ldr	r3, [r2, #2432]
	ldrh	r2, [r2, r1]
	add	r2, r3, r2, asl #1
.L497:
	cmp	r3, r2
	beq	.L501
	ldrh	r1, [r3], #2
	cmp	r1, r0
	bne	.L497
	mov	r0, #1
	bx	lr
.L501:
	mov	r0, #0
	bx	lr
.L503:
	.align	2
.L502:
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
	ldr	r2, .L511
	ldr	r3, [r2, #2436]
	cmp	r3, #0
	beq	.L507
	stmfd	sp!, {r4, r5, lr}
	.save {r4, r5, lr}
	mov	r5, #36
	ldr	r4, [r2, #2440]
	mov	r2, #0
.L506:
	mla	ip, r5, r2, r4
	ldr	lr, [ip, #16]
	cmp	lr, r0
	movcc	ip, #0
	movcs	ip, #1
	cmp	lr, r1
	movhi	ip, #0
	cmp	ip, #0
	bne	.L508
	add	r2, r2, #1
	cmp	r2, r3
	bne	.L506
	mov	r0, ip
	ldmfd	sp!, {r4, r5, pc}
.L507:
	mov	r0, r3
	bx	lr
.L508:
	mov	r0, #1
	ldmfd	sp!, {r4, r5, pc}
.L512:
	.align	2
.L511:
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
	ldr	r3, .L514
	ldr	r0, [r3, #2424]
	bx	lr
.L515:
	.align	2
.L514:
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
	ldr	r3, .L517
	ldr	r0, [r3, #2424]
	bx	lr
.L518:
	.align	2
.L517:
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
	ldr	r3, .L520
	ldr	r0, [r3, #2444]
	bx	lr
.L521:
	.align	2
.L520:
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
	movw	r3, #2382
	ldr	r5, .L524
	mov	r6, r0
	ldrh	r4, [r5, r3]
	mov	r1, r4
	bl	__aeabi_uidiv
	uxth	r2, r0
	smulbb	r3, r2, r4
	add	r1, r5, r2, asl #2
	mov	r4, #1
	add	r5, r5, #2448
	ldr	ip, [r1, #2476]
	rsb	r3, r3, r6
	uxth	r3, r3
	and	r1, r3, #31
	mov	lr, r3, lsr #5
	ldr	r0, [ip, lr, asl #2]
	orr	r1, r0, r4, asl r1
	ldr	r0, .L524+4
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
.L525:
	.align	2
.L524:
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
	movw	r3, #2382
	ldr	r5, .L528
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
	ldr	r3, [r5, #2476]
	ldr	r0, [r3, r2, asl #2]
	mov	r0, r0, lsr r4
	and	r0, r0, #1
	ldmfd	sp!, {r3, r4, r5, r6, r7, pc}
.L529:
	.align	2
.L528:
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
	movw	r3, #2382
	ldr	r4, .L540
	mov	r5, #0
	ldrh	r6, [r4, r3]
	movw	r3, #2338
	ldrh	r3, [r4, r3]
	mov	r4, r5
	mul	r6, r3, r6
.L532:
	uxth	r0, r5
	cmp	r0, r6
	bge	.L539
	bl	FtlBbmIsBadBlock
	add	r5, r5, #1
	cmp	r0, #0
	addne	r4, r4, #1
	uxthne	r4, r4
	b	.L532
.L539:
	mov	r0, r4
	ldmfd	sp!, {r4, r5, r6, pc}
.L541:
	.align	2
.L540:
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
	movw	r3, #2340
	ldr	r6, .L544
	mov	r4, r1
	mov	r7, r0
	ldrh	r5, [r6, r3]
	mov	r1, r5
	bl	__aeabi_uidivmod
	mov	r0, r7
	smlabb	r4, r4, r5, r1
	mov	r1, r5
	bl	__aeabi_uidiv
	movw	r3, #2382
	ldrh	r3, [r6, r3]
	smlabb	r0, r3, r0, r4
	uxth	r0, r0
	ldmfd	sp!, {r3, r4, r5, r6, r7, pc}
.L545:
	.align	2
.L544:
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
	ldr	r3, .L548
	movw	r2, #2340
	stmfd	sp!, {r4, r5, r6, lr}
	.save {r4, r5, r6, lr}
	mov	r6, r0
	ldrh	r5, [r3, r2]
	movw	r2, #2382
	ldrh	r1, [r3, r2]
	bl	__aeabi_uidiv
	mov	r1, r5
	smulbb	r4, r0, r5
	mov	r0, r6
	bl	__aeabi_uidivmod
	add	r1, r4, r1
	uxth	r0, r1
	ldmfd	sp!, {r4, r5, r6, pc}
.L549:
	.align	2
.L548:
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
	movw	r3, #2382
	ldr	r4, .L552
	ldrh	r1, [r4, r3]
	bl	__aeabi_uidivmod
	movw	r3, #2340
	uxth	r0, r1
	ldrh	r1, [r4, r3]
	bl	__aeabi_uidiv
	uxth	r0, r0
	ldmfd	sp!, {r4, pc}
.L553:
	.align	2
.L552:
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
	bls	.L555
	rsb	r0, r1, r0
	cmp	r0, #-2147483648
	movhi	r0, #0
	movls	r0, #1
	bx	lr
.L555:
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
	ldr	r3, .L558
	ldrh	r0, [r3, #6]
	clz	r0, r0
	mov	r0, r0, lsr #5
	bx	lr
.L559:
	.align	2
.L558:
	.word	.LANCHOR0+2508
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
	ldr	r3, .L561
	ldrh	r0, [r3, #6]
	sub	r0, r0, #1024
	clz	r0, r0
	mov	r0, r0, lsr #5
	bx	lr
.L562:
	.align	2
.L561:
	.word	.LANCHOR0+2508
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
	ldr	r4, .L573
	ldrh	r3, [r4, #6]
	cmp	r3, #1024
	ldmeqfd	sp!, {r3, r4, r5, r6, r7, pc}
	cmp	r1, #0
	beq	.L565
	ldr	r5, .L573+4
	ldr	r3, [r5, #-3620]
	cmp	r3, #0
	bne	.L565
	bl	P2V_block_in_plane
	mov	r1, #1
	mov	r3, r7, asl #10
	mov	r2, r1
	mov	r6, r0
	ldr	r0, [r5, #-3616]
	str	r3, [r0, #4]
	bl	FlashEraseBlocks
	ldr	r1, [r5, #-3612]
	mov	r3, r6, asl #1
	ldrh	r2, [r1, r3]
	add	r2, r2, #1
	strh	r2, [r1, r3]	@ movhi
	ldr	r3, [r5, #-3608]
	add	r3, r3, #1
	str	r3, [r5, #-3608]
.L565:
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
.L574:
	.align	2
.L573:
	.word	.LANCHOR0+2508
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
	ldr	r3, .L588
	ldrh	ip, [r3, #28]
	ldr	r3, .L588+4
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
.L577:
	uxth	lr, r0
	add	r0, r0, #1
	cmp	lr, ip
	bge	.L587
	add	lr, r3, r1, asl #1
	add	r1, r1, #1
	ubfx	r1, r1, #0, #10
	ldrh	r4, [lr, #8]
	add	lr, r3, r2, asl #1
	strh	r4, [lr, #8]	@ movhi
	mov	r4, #1
	add	r2, r2, r4
	ubfx	r2, r2, #0, #10
	b	.L577
.L587:
	cmp	r4, #0
	strneh	r1, [r3, #2]	@ movhi
	strneh	r2, [r3, #4]	@ movhi
	ldmfd	sp!, {r4, pc}
.L589:
	.align	2
.L588:
	.word	.LANCHOR2-3604
	.word	.LANCHOR0+2508
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
	ldr	r4, .L601
	ldr	r8, .L601+4
	mov	r7, r4
.L591:
	ldrh	r1, [r4, #6]
	cmp	r1, #0
	beq	.L592
	ldr	r5, .L601+8
	sub	r1, r1, #1
	ldrh	r3, [r4, #2]
	strh	r1, [r4, #6]	@ movhi
	ldr	r10, [r5, #-3620]
	add	r2, r4, r3, asl #1
	add	r3, r3, #1
	cmp	r10, #0
	ubfx	r3, r3, #0, #10
	ldrh	r6, [r2, #8]
	strh	r3, [r4, #2]	@ movhi
	bne	.L593
	mov	r0, r6
	bl	P2V_block_in_plane
	mov	r3, r6, asl #10
	mov	r9, r0
	ldr	r0, [r5, #-3616]
	str	r3, [r0, #4]
	ldrb	r3, [r8, #140]	@ zero_extendqisi2
	cmp	r3, #0
	beq	.L594
	mov	r1, r10
	mov	r2, #1
	bl	FlashEraseBlocks
.L594:
	mov	r1, #1
	ldr	r0, [r5, #-3616]
	mov	r2, r1
	bl	FlashEraseBlocks
	ldr	r1, [r5, #-3612]
	mov	r3, r9, asl #1
	ldrh	r2, [r1, r3]
	add	r2, r2, #1
	strh	r2, [r1, r3]	@ movhi
	ldr	r3, [r5, #-3608]
	add	r3, r3, #1
	str	r3, [r5, #-3608]
	b	.L593
.L592:
	ldr	r0, .L601+12
	bl	printk
.L595:
	b	.L595
.L593:
	sub	r3, r6, #1
	movw	r2, #65533
	uxth	r3, r3
	cmp	r3, r2
	bls	.L596
	mov	r1, r6
	ldrh	r2, [r7, #6]
	ldr	r0, .L601+16
	bl	printk
	b	.L591
.L596:
	mov	r0, r6
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, pc}
.L602:
	.align	2
.L601:
	.word	.LANCHOR0+2508
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
	ldr	r3, .L609
	str	lr, [sp, #-4]!
	.save {lr}
	movw	lr, #65535
	ldr	ip, [r3, #-3556]
	ldr	r2, [r0]
	ldr	r3, .L609+4
	rsb	r0, ip, r2
	mov	r0, r0, asr #1
	mul	r3, r3, r0
	mov	r0, #6
	uxth	r3, r3
.L605:
	cmp	r1, r3
	beq	.L606
	ldrh	r3, [r2]
	cmp	r3, lr
	beq	.L607
	mla	r2, r0, r3, ip
	b	.L605
.L606:
	mov	r0, #1
	ldr	pc, [sp], #4
.L607:
	mov	r0, #0
	ldr	pc, [sp], #4
.L610:
	.align	2
.L609:
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
	movw	r3, #2324
	ldr	r5, .L628
	ldrh	r3, [r5, r3]
	cmp	r3, r0
	bls	.L613
	ldr	r2, .L628+4
	mov	lr, #6
	mul	lr, lr, r0
	mvn	ip, #0
	ldr	r6, [r2, #-3556]
	mov	r10, r2
	add	r1, r6, lr
	strh	ip, [r1, #2]	@ movhi
	strh	ip, [r6, lr]	@ movhi
	ldr	r3, [r2, #-3552]
	cmp	r3, #0
	beq	.L627
	ldr	r8, [r2, #-3548]
	mov	r4, r0, asl #1
	ldrh	r2, [r1, #4]
	ldrh	r7, [r8, r4]
	cmp	r2, #0
	mulne	ip, r2, r7
	ldr	r7, [r10, #-3556]
	ldr	r2, .L628+8
	rsb	r9, r7, r3
	mov	r9, r9, asr #1
	mul	r2, r2, r9
	movw	r9, #2324
	ldrh	r5, [r5, r9]
	ldr	r9, [r10, #-3612]
	add	r4, r9, r4
	str	r4, [sp, #4]
	uxth	r2, r2
	mov	r4, #0
	str	r5, [sp]
.L622:
	add	r4, r4, #1
	ldr	r5, [sp]
	uxth	r4, r4
	cmp	r4, r5
	movls	r5, #0
	movhi	r5, #1
	cmp	r0, r2
	orreq	r5, r5, #1
	cmp	r5, #0
	bne	.L613
	mov	r10, r2, asl #1
	ldrh	r5, [r3, #4]
	ldrh	fp, [r8, r10]
	cmp	r5, #0
	mvneq	r5, #0
	mulne	r5, r5, fp
	cmp	r5, ip
	bne	.L618
	ldr	r5, [sp, #4]
	ldrh	r10, [r9, r10]
	ldrh	r5, [r5]
	cmp	r10, r5
	bcc	.L620
	b	.L619
.L618:
	bhi	.L619
.L620:
	ldrh	r5, [r3]
	movw	r10, #65535
	cmp	r5, r10
	streqh	r2, [r1, #2]	@ movhi
	streqh	r0, [r3]	@ movhi
	ldreq	r3, .L628+4
	streq	r1, [r3, #-3544]
	beq	.L613
.L621:
	mov	r3, #6
	mov	r2, r5
	mla	r3, r3, r5, r7
	b	.L622
.L619:
	strh	r2, [r6, lr]	@ movhi
	ldrh	r2, [r3, #2]
	strh	r2, [r1, #2]	@ movhi
	ldr	r2, .L628+4
	ldr	ip, [r2, #-3552]
	cmp	r3, ip
	bne	.L623
	strh	r0, [r3, #2]	@ movhi
.L627:
	str	r1, [r2, #-3552]
	b	.L613
.L623:
	ldrh	ip, [r3, #2]
	ldr	r1, [r2, #-3556]
	mov	r2, #6
	mul	r2, r2, ip
	strh	r0, [r1, r2]	@ movhi
	strh	r0, [r3, #2]	@ movhi
.L613:
	mov	r0, #0
	add	sp, sp, #12
	@ sp needed
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L629:
	.align	2
.L628:
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
	ldr	r2, .L632
	ldrh	r3, [r2, #-4]
	add	r3, r3, #1
	strh	r3, [r2, #-4]	@ movhi
	ldmfd	sp!, {r3, pc}
.L633:
	.align	2
.L632:
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
	beq	.L635
	ldr	r2, .L643
	mov	lr, #6
	mul	r6, lr, r0
	mvn	r3, #0
	ldr	r7, [r2, #-3556]
	mov	r5, r2
	add	ip, r7, r6
	strh	r3, [ip, #2]	@ movhi
	strh	r3, [r7, r6]	@ movhi
	ldr	r3, [r2, #-3536]
	cmp	r3, #0
	beq	.L642
	ldr	r9, [r2, #-3612]
	mov	r2, r0, asl #1
	ldr	r8, [r5, #-3556]
	rsb	r1, r8, r3
	ldrh	r10, [r9, r2]
	ldr	r2, .L643+4
	mov	r1, r1, asr #1
	mul	r1, r2, r1
	uxth	r2, r1
.L639:
	mov	r1, r2, asl #1
	ldrh	r1, [r9, r1]
	cmp	r1, r10
	bcs	.L637
	ldrh	r1, [r3]
	cmp	r1, r4
	streqh	r2, [ip, #2]	@ movhi
	streqh	r0, [r3]	@ movhi
	beq	.L635
.L638:
	mla	r3, lr, r1, r8
	mov	r2, r1
	b	.L639
.L637:
	ldrh	r1, [r3, #2]
	strh	r1, [ip, #2]	@ movhi
	strh	r2, [r7, r6]	@ movhi
	ldr	r1, [r5, #-3536]
	ldr	r2, .L643
	cmp	r3, r1
	bne	.L640
	strh	r0, [r3, #2]	@ movhi
.L642:
	str	ip, [r2, #-3536]
	b	.L635
.L640:
	ldrh	ip, [r3, #2]
	ldr	r1, [r2, #-3556]
	mov	r2, #6
	mul	r2, r2, ip
	strh	r0, [r1, r2]	@ movhi
	strh	r0, [r3, #2]	@ movhi
.L635:
	mov	r0, #0
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, pc}
.L644:
	.align	2
.L643:
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
	ldr	r2, .L647
	ldrh	r3, [r2, #-12]
	add	r3, r3, #1
	strh	r3, [r2, #-12]	@ movhi
	ldmfd	sp!, {r3, pc}
.L648:
	.align	2
.L647:
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
	ldr	r4, .L655
	movw	r5, #65535
	mul	r1, ip, r1
	ldr	r3, [r0]
	ldr	r2, [r4, #-3556]
	add	lr, r2, r1
	cmp	lr, r3
	ldrh	r3, [r2, r1]
	bne	.L650
	cmp	r3, r5
	mlane	r3, ip, r3, r2
	moveq	r3, #0
	streq	r3, [r0]
	strne	r3, [r0]
	mvnne	r0, #0
	strneh	r0, [r3, #2]	@ movhi
	b	.L652
.L650:
	cmp	r3, r5
	ldrh	r0, [lr, #2]
	bne	.L653
	cmp	r0, r3
	mulne	r0, ip, r0
	mvnne	r3, #0
	strneh	r3, [r2, r0]	@ movhi
	b	.L652
.L653:
	mla	r3, ip, r3, r2
	strh	r0, [r3, #2]	@ movhi
	ldrh	r5, [lr, #2]
	ldrh	r0, [r2, r1]
	ldr	r3, [r4, #-3556]
	mul	ip, ip, r5
	strh	r0, [r3, ip]	@ movhi
.L652:
	mvn	r3, #0
	mov	r0, #0
	strh	r3, [r2, r1]	@ movhi
	strh	r3, [lr, #2]	@ movhi
	ldmfd	sp!, {r4, r5, pc}
.L656:
	.align	2
.L655:
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
	beq	.L663
	ldr	r2, .L666
	movw	ip, #65535
	stmfd	sp!, {r4, lr}
	.save {r4, lr}
	mov	lr, #6
	ldr	r4, [r2, #-3556]
.L659:
	cmp	r1, #0
	bne	.L660
.L662:
	rsb	r4, r4, r3
	ldr	r3, .L666+4
	mov	r4, r4, asr #1
	mul	r4, r3, r4
	uxth	r4, r4
	mov	r1, r4
	bl	List_remove_node
	mov	r0, r4
	ldmfd	sp!, {r4, pc}
.L660:
	ldrh	r2, [r3]
	cmp	r2, ip
	beq	.L662
	sub	r1, r1, #1
	mla	r3, lr, r2, r4
	uxth	r1, r1
	b	.L659
.L663:
	movw	r0, #65535
	bx	lr
.L667:
	.align	2
.L666:
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
	ldr	r2, .L675
	ldr	r3, [r2, #-3552]
	cmp	r3, #0
	ldrne	r1, [r2, #-3556]
	movne	ip, #6
	movwne	r2, #65535
	beq	.L673
.L670:
	cmp	r0, #0
	beq	.L671
	ldrh	r3, [r3]
	cmp	r3, r2
	subne	r0, r0, #1
	mlane	r3, ip, r3, r1
	uxthne	r0, r0
	bne	.L670
.L673:
	movw	r0, #65535
	bx	lr
.L671:
	rsb	r3, r1, r3
	ldr	r0, .L675+4
	mov	r3, r3, asr #1
	mul	r0, r0, r3
	uxth	r0, r0
	bx	lr
.L676:
	.align	2
.L675:
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
	ldr	r3, .L686
	sub	r2, r3, #3520
	ldrh	r2, [r2, #-8]
	cmp	r2, r0
	beq	.L685
	sub	r2, r3, #3472
	ldrh	r2, [r2, #-8]
	cmp	r2, r0
	beq	.L685
	sub	r2, r3, #3424
	ldrh	r2, [r2, #-8]
	cmp	r2, r0
	beq	.L685
	stmfd	sp!, {r4, lr}
	.save {r4, lr}
	mov	lr, #6
	mul	lr, lr, r0
	ldr	r1, [r3, #-3556]
	ldr	r2, [r3, #-3552]
	add	ip, r1, lr
	cmp	ip, r2
	beq	.L678
	ldr	r4, [r3, #-3548]
	mov	r3, r0, asl #1
	ldrh	r2, [r4, r3]
	ldrh	r3, [ip, #4]
	ldrh	ip, [ip, #2]
	cmp	r3, #0
	mulne	r2, r3, r2
	mvneq	r2, #0
	movw	r3, #65535
	cmp	ip, r3
	bne	.L680
	ldrh	r3, [r1, lr]
	cmp	r3, ip
	beq	.L678
.L680:
	mov	r3, #6
	mul	ip, r3, ip
	ldr	r3, .L686+4
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
	bcs	.L678
	mov	r4, r0
	ldr	r0, .L686+8
	mov	r1, r4
	bl	List_remove_node
	ldr	r2, .L686+12
	mov	r0, r4
	ldrh	r3, [r2, #-4]
	sub	r3, r3, #1
	strh	r3, [r2, #-4]	@ movhi
	bl	INSERT_DATA_LIST
.L678:
	mov	r0, #0
	ldmfd	sp!, {r4, pc}
.L685:
	mov	r0, #0
	bx	lr
.L687:
	.align	2
.L686:
	.word	.LANCHOR2
	.word	-1431655765
	.word	.LANCHOR2-3552
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
.L689:
	uxth	r5, r3
	cmp	r5, r1
	bcs	.L692
	mov	r7, r2
	add	r3, r3, #1
	ldrh	r6, [r7]
	add	r2, r2, #2
	cmp	r6, #0
	bne	.L689
	mov	r4, r0
	bl	FtlFreeSysBlkQueueOut
	movw	r2, #65533
	sub	r3, r0, #1
	mov	r1, r0
	strh	r0, [r7]	@ movhi
	uxth	r3, r3
	cmp	r3, r2
	bls	.L690
	ldr	r3, .L696
	ldr	r0, .L696+4
	ldrh	r2, [r3, #6]
	bl	printk
.L691:
	b	.L691
.L690:
	ldr	r3, [r4, #28]
	strh	r6, [r4, #2]	@ movhi
	add	r3, r3, #1
	str	r3, [r4, #28]
	ldrh	r3, [r4, #8]
	strh	r5, [r4]	@ movhi
	add	r3, r3, #1
	strh	r3, [r4, #8]	@ movhi
.L692:
	mov	r0, #0
	ldmfd	sp!, {r3, r4, r5, r6, r7, pc}
.L697:
	.align	2
.L696:
	.word	.LANCHOR0+2508
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
	ldr	r2, .L714
	movw	r3, #2422
	stmfd	sp!, {r4, r5, r6, lr}
	.save {r4, r5, r6, lr}
	mov	r1, #0
	ldrh	r2, [r2, r3]
	mov	ip, #12
	ldr	r3, .L714+4
	movw	lr, #65535
	ldr	r3, [r3, #-3384]
.L699:
	uxth	r0, r1
	cmp	r0, r2
	bcs	.L711
	add	r1, r1, #1
	mla	r4, ip, r1, r3
	ldrh	r4, [r4, #-12]
	cmp	r4, lr
	bne	.L699
	ldmfd	sp!, {r4, r5, r6, pc}
.L711:
	mov	r0, r2
	mov	r1, #0
	mov	ip, #-2147483648
	mov	r5, #12
.L702:
	uxth	r4, r1
	cmp	r4, r2
	bcs	.L712
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
	b	.L702
.L712:
	cmp	r0, r2
	ldmccfd	sp!, {r4, r5, r6, pc}
	ldr	r1, .L714+8
	mov	r0, r2
	mvn	ip, #0
	ldrh	r5, [r1, #-4]
	mov	r1, #0
.L705:
	uxth	lr, r1
	cmp	lr, r2
	bcs	.L713
	ldr	r4, [r3, #4]
	cmp	r4, ip
	bcs	.L706
	ldrh	r6, [r3]
	cmp	r6, r5
	movne	ip, r4
	movne	r0, lr
.L706:
	add	r1, r1, #1
	add	r3, r3, #12
	b	.L705
.L713:
	ldmfd	sp!, {r4, r5, r6, pc}
.L715:
	.align	2
.L714:
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
	ldr	r3, .L724
	sub	r1, r3, #3376
	ldrh	r2, [r1, #-2]
	cmp	r2, #4
	cmpls	r0, #0
	addeq	r2, r2, #1
	streqh	r2, [r1, #-2]	@ movhi
	bxeq	lr
	ldr	r0, .L724+4
	mov	r2, #0
	str	lr, [sp, #-4]!
	.save {lr}
	movw	ip, #65535
	strh	r2, [r1, #-2]	@ movhi
	movw	r1, #2324
	ldrh	r0, [r0, r1]
	str	r2, [r3, #-3376]
	ldr	r2, [r3, #-3548]
	add	r0, r2, r0, asl #1
.L718:
	cmp	r2, r0
	beq	.L723
	ldrh	r1, [r2], #2
	cmp	r1, ip
	ldrne	lr, [r3, #-3376]
	addne	r1, r1, lr
	strne	r1, [r3, #-3376]
	b	.L718
.L723:
	ldr	pc, [sp], #4
.L725:
	.align	2
.L724:
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
	ldr	r2, .L730
	mov	r3, r0, lsr #5
	cmp	r1, #0
	and	r0, r0, #31
	uxth	r3, r3
	mov	ip, #1
	ldr	r1, [r2, #-3372]
	ldr	r2, [r1, r3, asl #2]
	orrne	r0, r2, ip, asl r0
	biceq	r0, r2, ip, asl r0
	str	r0, [r1, r3, asl #2]
	bx	lr
.L731:
	.align	2
.L730:
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
	ldr	r3, .L733
	mov	r2, r0, lsr #5
	and	r0, r0, #31
	ldr	r3, [r3, #-3372]
	ldr	r3, [r3, r2, asl #2]
	mov	r0, r3, lsr r0
	and	r0, r0, #1
	bx	lr
.L734:
	.align	2
.L733:
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
	ldr	ip, .L745
	mov	r3, #0
	strh	r3, [r0, #4]	@ movhi
	movw	r3, #2316
	stmfd	sp!, {r4, r5, lr}
	.save {r4, r5, lr}
	movw	r4, #65535
	ldrh	lr, [ip, r3]
	add	r3, r2, #7
	add	r3, r0, r3, asl #1
.L736:
	cmp	r2, lr
	bcs	.L743
	ldrh	ip, [r3, #2]!
	add	r2, r2, #1
	cmp	ip, r4
	uxth	r2, r2
	ldrneh	ip, [r0, #4]
	addne	ip, ip, #1
	strneh	ip, [r0, #4]	@ movhi
	b	.L736
.L743:
	ldr	r3, .L745+4
	add	ip, r0, #14
	mov	r2, #0
	movw	r5, #65535
	ldrh	r4, [r3]
.L739:
	uxth	r3, r2
	cmp	r3, lr
	bcs	.L744
	ldrh	r3, [ip, #2]!
	add	r2, r2, #1
	cmp	r3, r5
	ldrneh	r3, [r0, #4]
	addne	r3, r4, r3
	subne	r3, r3, #1
	rsbne	r3, r1, r3
	strneh	r3, [r0, #4]	@ movhi
	b	.L739
.L744:
	ldmfd	sp!, {r4, r5, pc}
.L746:
	.align	2
.L745:
	.word	.LANCHOR0
	.word	.LANCHOR0+2384
	.fnend
	.size	ftl_sb_update_avl_pages, .-ftl_sb_update_avl_pages
	.align	2
	.global	make_superblock
	.type	make_superblock, %function
make_superblock:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	stmfd	sp!, {r3, r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.save {r3, r4, r5, r6, r7, r8, r9, r10, fp, lr}
	movw	r2, #2316
	ldr	r6, .L761
	mov	r3, #0
	ldr	r10, .L761+4
	add	r7, r0, #16
	mov	r4, r0
	mov	r5, r3
	ldrh	r8, [r6, r2]
	mvn	r9, #0
	strh	r3, [r0, #4]	@ movhi
	strb	r3, [r0, #7]
.L748:
	uxth	r3, r5
	cmp	r3, r8
	bcs	.L760
	ldrb	r0, [r10, r5]	@ zero_extendqisi2
	add	r7, r7, #2
	ldrh	r1, [r4]
	add	r5, r5, #1
	bl	V2P_block
	strh	r9, [r7, #-2]	@ movhi
	mov	fp, r0
	bl	FtlBbmIsBadBlock
	cmp	r0, #0
	streqh	fp, [r7, #-2]	@ movhi
	ldreqb	r3, [r4, #7]	@ zero_extendqisi2
	addeq	r3, r3, #1
	streqb	r3, [r4, #7]
	b	.L748
.L760:
	ldr	r3, .L761+8
	ldrb	r2, [r4, #7]	@ zero_extendqisi2
	ldrh	r3, [r3]
	smulbb	r3, r2, r3
	strh	r3, [r4, #4]	@ movhi
	mov	r3, #0
	strb	r3, [r4, #9]
	ldr	r3, [r6, #2308]
	cmp	r3, #0
	beq	.L751
	ldr	r2, .L761+12
	ldrh	r3, [r4]
	ldr	r2, [r2, #-3612]
	mov	r3, r3, asl #1
	ldrh	r3, [r2, r3]
	cmp	r3, #79
	movls	r3, #1
	strlsb	r3, [r4, #9]
.L751:
	ldrb	r3, [r6]	@ zero_extendqisi2
	mov	r0, #0
	cmp	r3, #0
	movne	r3, #1
	strneb	r3, [r4, #9]
	ldmfd	sp!, {r3, r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L762:
	.align	2
.L761:
	.word	.LANCHOR0
	.word	.LANCHOR0+2344
	.word	.LANCHOR0+2384
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
	movw	r2, #2316
	ldr	r3, .L773
	mov	r5, #0
	mov	r6, r0
	mov	r4, r5
	ldrh	r7, [r3, r2]
	add	r3, r3, #2384
	sub	r9, r3, #40
	ldrh	r8, [r3]
.L764:
	uxth	r3, r5
	cmp	r3, r7
	bcs	.L772
	ldrb	r0, [r9, r5]	@ zero_extendqisi2
	mov	r1, r6
	bl	V2P_block
	add	r5, r5, #1
	bl	FtlBbmIsBadBlock
	cmp	r0, #0
	addeq	r4, r4, r8
	uxtheq	r4, r4
	b	.L764
.L772:
	cmp	r4, #0
	beq	.L767
	mov	r1, r4
	mov	r0, #32768
	bl	__aeabi_idiv
	uxth	r4, r0
.L767:
	ldr	r3, .L773+4
	mov	r2, #6
	mov	r0, #0
	ldr	r3, [r3, #-3556]
	mla	r6, r2, r6, r3
	strh	r4, [r6, #4]	@ movhi
	ldmfd	sp!, {r3, r4, r5, r6, r7, r8, r9, pc}
.L774:
	.align	2
.L773:
	.word	.LANCHOR0
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
	ldr	r2, .L778
	ldr	r0, [r2, #-3536]
	cmp	r0, #0
	bxeq	lr
	ldr	r3, [r2, #-3556]
	rsb	r0, r3, r0
	ldr	r3, .L778+4
	mov	r0, r0, asr #1
	mul	r0, r3, r0
	ldr	r3, [r2, #-3612]
	uxth	r0, r0
	mov	r0, r0, asl #1
	ldrh	r0, [r3, r0]
	bx	lr
.L779:
	.align	2
.L778:
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
	ldr	r1, .L790
	ldr	r3, [r1, #-3536]
	cmp	r3, #0
	beq	.L786
	sub	r2, r1, #3520
	stmfd	sp!, {r4, r5, lr}
	.save {r4, r5, lr}
	mov	r4, #6
	ldrh	r2, [r2, #-12]
	movw	r5, #65535
	ldr	ip, [r1, #-3556]
	rsb	r2, r2, r2, asl #3
	rsb	r3, ip, r3
	mov	r2, r2, asr #3
	mov	r3, r3, asr #1
	cmp	r0, r2
	uxthgt	r0, r2
	ldr	r2, .L790+4
	mul	r3, r2, r3
	mov	r2, #0
	uxth	r3, r3
.L783:
	uxth	lr, r2
	cmp	lr, r0
	bcs	.L785
	mul	lr, r4, r3
	add	r2, r2, #1
	ldrh	lr, [ip, lr]
	cmp	lr, r5
	bne	.L787
.L785:
	ldr	r2, [r1, #-3612]
	mov	r3, r3, asl #1
	ldrh	r0, [r2, r3]
	ldmfd	sp!, {r4, r5, pc}
.L787:
	mov	r3, lr
	b	.L783
.L786:
	mov	r0, r3
	bx	lr
.L791:
	.align	2
.L790:
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
	ldr	r8, .L803
	add	r4, r7, #12
	ldr	r1, .L803+4
	.pad #32
	sub	sp, sp, #32
	bl	strcpy
	mov	r0, r4
	ldr	r1, .L803+8
	ldr	r2, [r8, #112]
	bl	sprintf
	ldr	r1, .L803+12
	ldr	r2, [r8, #2400]
	add	r4, r4, r0
	mov	r0, r4
	bl	sprintf
	ldr	r3, .L803+16
	ldr	r3, [r3, #504]
	cmp	r3, #1
	add	r4, r4, r0
	bne	.L798
	add	r0, sp, #16
	add	r1, sp, #20
	add	r2, sp, #24
	add	r3, sp, #28
	bl	NandcGetTimeCfg
	mov	r0, r4
	ldr	r1, .L803+20
	ldr	r6, .L803+24
	sub	r10, r6, #3520
	sub	r9, r6, #3296
	ldr	r3, [sp, #24]
	ldr	r2, [sp, #16]
	str	r3, [sp]
	ldr	r3, [sp, #28]
	str	r3, [sp, #4]
	ldr	r3, [sp, #20]
	bl	sprintf
	ldr	r1, .L803+28
	add	r4, r4, r0
	add	r5, r4, #10
	mov	r0, r4
	add	r4, r8, #2448
	bl	strcpy
	mov	r0, r5
	ldr	r1, .L803+32
	ldr	r2, [r8, #2444]
	bl	sprintf
	ldr	r1, .L803+36
	ldr	r2, [r6, #-3376]
	add	r5, r5, r0
	mov	r0, r5
	bl	sprintf
	ldr	r1, .L803+40
	ldr	r2, [r6, #-3368]
	add	r5, r5, r0
	mov	r0, r5
	bl	sprintf
	ldr	r1, .L803+44
	ldr	r2, [r6, #-3364]
	add	r5, r5, r0
	mov	r0, r5
	bl	sprintf
	ldr	r1, .L803+48
	ldr	r2, [r6, #-3360]
	add	r5, r5, r0
	mov	r0, r5
	bl	sprintf
	ldr	r1, .L803+52
	ldr	r2, [r6, #-3356]
	add	r5, r5, r0
	mov	r0, r5
	bl	sprintf
	ldr	r1, .L803+56
	ldr	r2, [r6, #-3352]
	add	r5, r5, r0
	mov	r0, r5
	bl	sprintf
	ldr	r1, .L803+60
	ldr	r2, [r6, #-3348]
	add	r5, r5, r0
	mov	r0, r5
	bl	sprintf
	ldr	r2, [r6, #-3344]
	ldr	r1, .L803+64
	mov	r2, r2, lsr #11
	add	r5, r5, r0
	mov	r0, r5
	bl	sprintf
	ldr	r2, [r6, #-3340]
	ldr	r1, .L803+68
	mov	r2, r2, lsr #11
	add	r5, r5, r0
	mov	r0, r5
	bl	sprintf
	ldr	r1, .L803+72
	ldr	r2, [r6, #-3336]
	add	r5, r5, r0
	mov	r0, r5
	bl	sprintf
	ldr	r1, .L803+76
	ldr	r2, [r6, #-3332]
	add	r5, r5, r0
	mov	r0, r5
	bl	sprintf
	add	r5, r5, r0
	bl	FtlBbtCalcTotleCnt
	ldrh	r2, [r4, #6]
	ldr	r1, .L803+80
	mov	r3, r0
	mov	r0, r5
	bl	sprintf
	ldr	r1, .L803+84
	ldrh	r2, [r10, #-12]
	add	r5, r5, r0
	mov	r0, r5
	bl	sprintf
	ldr	r1, .L803+88
	ldr	r2, [r6, #-3328]
	add	r5, r5, r0
	mov	r0, r5
	bl	sprintf
	ldr	r1, .L803+92
	ldr	r2, [r6, #-3324]
	add	r5, r5, r0
	mov	r0, r5
	bl	sprintf
	ldr	r1, .L803+96
	ldr	r2, [r6, #-3320]
	add	r5, r5, r0
	mov	r0, r5
	bl	sprintf
	ldr	r1, .L803+100
	ldr	r2, [r6, #-3608]
	add	r5, r5, r0
	mov	r0, r5
	bl	sprintf
	ldr	r1, .L803+104
	ldr	r2, [r6, #-3316]
	add	r5, r5, r0
	mov	r0, r5
	bl	sprintf
	ldr	r1, .L803+108
	ldr	r2, [r6, #-3312]
	add	r5, r5, r0
	mov	r0, r5
	bl	sprintf
	ldr	r1, .L803+112
	ldrh	r2, [r10, #-54]
	add	r5, r5, r0
	mov	r0, r5
	bl	sprintf
	ldr	r1, .L803+116
	ldrh	r2, [r10, #-56]
	add	r5, r5, r0
	mov	r0, r5
	bl	sprintf
	ldr	r1, .L803+120
	ldr	r2, [r8, #2424]
	add	r5, r5, r0
	mov	r0, r5
	bl	sprintf
	ldr	r1, .L803+124
	ldr	r2, [r8, #2416]
	add	r5, r5, r0
	mov	r0, r5
	bl	sprintf
	ldr	r1, .L803+128
	ldr	r2, [r8, #2312]
	add	r5, r5, r0
	mov	r0, r5
	bl	sprintf
	ldrh	r2, [r4, #66]
	ldr	r1, .L803+132
	add	r5, r5, r0
	mov	r0, r5
	bl	sprintf
	movw	r3, #2324
	ldrh	r2, [r8, r3]
	ldr	r1, .L803+136
	add	r5, r5, r0
	mov	r0, r5
	bl	sprintf
	ldrh	r2, [r9, #-12]
	ldr	r1, .L803+140
	add	r5, r5, r0
	mov	r0, r5
	bl	sprintf
	ldr	r1, .L803+144
	ldr	r2, [r8, #2328]
	add	r5, r5, r0
	mov	r0, r5
	bl	sprintf
	ldrh	r2, [r9, #-8]
	ldr	r1, .L803+148
	add	r5, r5, r0
	mov	r0, r5
	bl	sprintf
	ldrh	r2, [r4]
	ldr	r1, .L803+152
	sub	r4, r6, #3472
	add	r5, r5, r0
	mov	r0, r5
	bl	sprintf
	ldrh	r2, [r9, #-230]
	ldr	r1, .L803+156
	add	r5, r5, r0
	mov	r0, r5
	bl	sprintf
	ldr	r1, .L803+160
	ldrb	r2, [r6, #-3522]	@ zero_extendqisi2
	add	r5, r5, r0
	mov	r0, r5
	bl	sprintf
	ldr	r1, .L803+164
	ldrh	r2, [r10, #-8]
	add	r5, r5, r0
	mov	r0, r5
	bl	sprintf
	ldr	r1, .L803+168
	ldrb	r2, [r6, #-3520]	@ zero_extendqisi2
	add	r5, r5, r0
	mov	r0, r5
	bl	sprintf
	ldrh	r2, [r9, #-228]
	ldr	r1, .L803+172
	add	r5, r5, r0
	mov	r0, r5
	bl	sprintf
	ldrh	r3, [r10, #-8]
	ldr	r2, [r6, #-3548]
	ldr	r1, .L803+176
	mov	r3, r3, asl #1
	ldrh	r2, [r2, r3]
	add	r5, r5, r0
	mov	r0, r5
	bl	sprintf
	ldrh	r2, [r9, #-182]
	ldr	r1, .L803+180
	add	r5, r5, r0
	mov	r0, r5
	bl	sprintf
	ldr	r1, .L803+184
	ldrb	r2, [r6, #-3474]	@ zero_extendqisi2
	add	r5, r5, r0
	mov	r0, r5
	bl	sprintf
	ldrh	r2, [r4, #-8]
	ldr	r1, .L803+188
	add	r5, r5, r0
	mov	r0, r5
	bl	sprintf
	ldr	r1, .L803+192
	ldrb	r2, [r6, #-3472]	@ zero_extendqisi2
	add	r5, r5, r0
	mov	r0, r5
	bl	sprintf
	ldrh	r2, [r9, #-180]
	ldr	r1, .L803+196
	sub	r9, r6, #3424
	add	r5, r5, r0
	mov	r0, r5
	bl	sprintf
	ldrh	r3, [r4, #-8]
	ldr	r2, [r6, #-3548]
	ldr	r1, .L803+200
	mov	r3, r3, asl #1
	ldrh	r2, [r2, r3]
	add	r5, r5, r0
	mov	r0, r5
	bl	sprintf
	ldrh	r2, [r4, #42]
	ldr	r1, .L803+204
	add	r5, r5, r0
	mov	r0, r5
	bl	sprintf
	ldr	r1, .L803+208
	ldrb	r2, [r6, #-3426]	@ zero_extendqisi2
	add	r5, r5, r0
	mov	r0, r5
	bl	sprintf
	ldrh	r2, [r9, #-8]
	ldr	r1, .L803+212
	sub	r9, r6, #3280
	add	r5, r5, r0
	mov	r0, r5
	bl	sprintf
	ldr	r1, .L803+216
	ldrb	r2, [r6, #-3424]	@ zero_extendqisi2
	add	r5, r5, r0
	mov	r0, r5
	bl	sprintf
	ldrh	r2, [r4, #44]
	ldr	r1, .L803+220
	add	r5, r5, r0
	mov	r0, r5
	bl	sprintf
	ldrh	r2, [r4, #186]
	ldr	r1, .L803+224
	add	r5, r5, r0
	mov	r0, r5
	bl	sprintf
	ldr	r1, .L803+228
	ldrb	r2, [r6, #-3282]	@ zero_extendqisi2
	add	r5, r5, r0
	mov	r0, r5
	bl	sprintf
	ldr	r1, .L803+232
	ldrh	r2, [r9, #-8]
	add	r5, r5, r0
	mov	r0, r5
	bl	sprintf
	ldr	r1, .L803+236
	ldrb	r2, [r6, #-3280]	@ zero_extendqisi2
	add	r5, r5, r0
	mov	r0, r5
	bl	sprintf
	ldrh	r2, [r4, #188]
	ldr	r1, .L803+240
	add	r5, r5, r0
	mov	r0, r5
	bl	sprintf
	ldr	r1, [r6, #-3152]
	ldr	r3, [r8, #2308]
	ldr	r2, [r6, #-3240]
	orr	r2, r3, r2, asl #8
	ldr	r3, [r6, #-3156]
	str	r1, [sp]
	add	r5, r5, r0
	ldr	r1, [r6, #-3160]
	mov	r0, r5
	str	r1, [sp, #4]
	ldr	r1, .L803+244
	bl	sprintf
	ldr	r1, .L803+248
	ldr	r2, [r6, #-3164]
	add	r4, r5, r0
	sub	r5, r6, #2720
	mov	r0, r4
	bl	sprintf
	ldr	r1, .L803+252
	ldr	r2, [r6, #-3140]
	add	r4, r4, r0
	mov	r0, r4
	bl	sprintf
	ldr	r1, .L803+256
	ldrh	r2, [r5, #-4]
	add	r4, r4, r0
	mov	r0, r4
	bl	sprintf
	ldr	r1, .L803+260
	ldrh	r2, [r5, #-2]
	add	r4, r4, r0
	mov	r0, r4
	bl	sprintf
	ldr	r1, .L803+264
	ldr	r2, [r6, #-2720]
	add	r4, r4, r0
	mov	r0, r4
	bl	sprintf
	sub	r3, r6, #2704
	ldr	r1, .L803+268
	ldrh	r2, [r3, #-12]
	add	r4, r4, r0
	mov	r0, r4
	bl	sprintf
	add	r4, r4, r0
	bl	GetFreeBlockMinEraseCount
	ldr	r1, .L803+272
	mov	r2, r0
	mov	r0, r4
	bl	sprintf
	add	r4, r4, r0
	ldrh	r0, [r10, #-12]
	bl	GetFreeBlockMaxEraseCount
	ldr	r1, .L803+276
	mov	r2, r0
	mov	r0, r4
	bl	sprintf
	ldrh	r3, [r9, #-8]
	movw	r2, #65535
	cmp	r3, r2
	add	r4, r4, r0
	beq	.L795
	ldr	r2, [r6, #-3548]
	mov	r3, r3, asl #1
	mov	r0, r4
	ldr	r1, .L803+280
	ldrh	r2, [r2, r3]
	bl	sprintf
	add	r4, r4, r0
.L795:
	mov	r0, #0
	mov	r5, #0
	bl	List_get_gc_head_node
	movw	r10, #65535
	mov	r9, #6
	uxth	r3, r0
.L797:
	cmp	r3, r10
	beq	.L796
	ldr	r2, [r6, #-3548]
	mov	r1, r3, asl #1
	mul	r8, r9, r3
	mov	r0, r4
	ldrh	r2, [r2, r1]
	str	r2, [sp]
	ldr	r2, [r6, #-3556]
	add	r2, r2, r8
	ldrh	r2, [r2, #4]
	str	r2, [sp, #4]
	ldr	r2, [r6, #-3612]
	ldrh	r2, [r2, r1]
	ldr	r1, .L803+284
	str	r2, [sp, #8]
	mov	r2, r5
	bl	sprintf
	add	r5, r5, #1
	ldr	r3, [r6, #-3556]
	cmp	r5, #16
	ldrh	r3, [r3, r8]
	add	r4, r4, r0
	bne	.L797
.L796:
	ldr	r2, [r6, #-3556]
	mov	r5, #0
	ldr	r3, [r6, #-3536]
	movw	r10, #65535
	mov	r9, #6
	rsb	r3, r2, r3
	ldr	r2, .L803+288
	mov	r3, r3, asr #1
	mul	r3, r2, r3
	uxth	r3, r3
.L799:
	cmp	r3, r10
	beq	.L798
	mul	r8, r9, r3
	ldr	r2, [r6, #-3556]
	ldr	r1, [r6, #-3612]
	mov	r0, r4
	add	r2, r2, r8
	ldrh	r2, [r2, #4]
	str	r2, [sp]
	mov	r2, r3, asl #1
	ldrh	r2, [r1, r2]
	ldr	r1, .L803+292
	str	r2, [sp, #4]
	mov	r2, r5
	bl	sprintf
	add	r5, r5, #1
	ldr	r3, [r6, #-3556]
	cmp	r5, #4
	ldrh	r3, [r3, r8]
	add	r4, r4, r0
	bne	.L799
.L798:
	rsb	r0, r7, r4
	add	sp, sp, #32
	@ sp needed
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, pc}
.L804:
	.align	2
.L803:
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
	ldr	r1, .L807
	ldr	r2, .L807+4
	bl	sprintf
	add	r4, r5, r0
	mov	r0, r4
	bl	FtlPrintInfo2buf
	add	r0, r4, r0
	rsb	r0, r5, r0
	ldmfd	sp!, {r3, r4, r5, pc}
.L808:
	.align	2
.L807:
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
	ldr	r4, .L839
	ldr	r2, [r4, #-3320]
	ldr	r3, [r4, #-3312]
	cmp	r2, r3
	bcs	.L810
	ldr	r2, .L839+4
	movw	r1, #2324
	ldr	r0, [r4, #-3612]
	mov	r3, #0
	str	r3, [r4, #-3328]
	ldrh	r1, [r2, r1]
	sub	r0, r0, #2
	mov	r5, r2
.L811:
	cmp	r3, r1
	bcs	.L838
	ldrh	r2, [r0, #2]!
	add	r3, r3, #1
	ldr	ip, [r4, #-3328]
	add	r2, r2, ip
	str	r2, [r4, #-3328]
	b	.L811
.L838:
	ldr	r6, [r4, #-3328]
	mov	r0, r6
	bl	__aeabi_uidiv
	movw	r3, #2376
	ldrh	r1, [r5, r3]
	str	r0, [r4, #-3320]
	ldr	r0, [r4, #-3324]
	rsb	r0, r0, r6
	bl	__aeabi_uidiv
	str	r0, [r4, #-3328]
	b	.L813
.L810:
	ldr	r3, [r4, #-3316]
	cmp	r2, r3
	bls	.L813
	ldr	ip, .L839+8
	add	r3, r3, #1
	str	r3, [r4, #-3316]
	mov	r3, #0
.L815:
	ldrh	r2, [ip]
	cmp	r3, r2
	bcs	.L813
	ldr	r0, [r4, #-3612]
	mov	r1, r3, asl #1
	add	r3, r3, #1
	ldrh	r2, [r0, r1]
	add	r2, r2, #1
	strh	r2, [r0, r1]	@ movhi
	b	.L815
.L813:
	ldr	r6, [r4, #-3312]
	ldr	r8, [r4, #-3320]
	add	r3, r6, #256
	cmp	r3, r8
	bls	.L818
	ldr	r2, .L839
	add	r3, r6, #768
	ldr	r2, [r2, #-3316]
	cmp	r3, r2
	bls	.L818
	ldr	r3, .L839+4
	cmp	r6, #40
	ldr	r2, [r3, #2308]
	movls	r3, #0
	movhi	r3, #1
	cmp	r2, #0
	orreq	r3, r3, #1
	cmp	r3, #0
	beq	.L818
.L820:
	movw	r0, #65535
	b	.L819
.L818:
	ldr	r5, .L839
	sub	r3, r5, #3520
	ldrh	r0, [r3, #-12]
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
	bne	.L820
	ldr	r3, [r5, #-3552]
	cmp	r3, #0
	beq	.L820
	ldr	r0, .L839+4
	movw	r2, #2324
	ldr	ip, .L839+12
	movw	r7, #65535
	ldr	r10, [r5, #-3612]
	mov	lr, #6
	ldrh	r2, [r0, r2]
	ldr	r0, [r5, #-3556]
	mov	r5, r7
	str	r2, [sp, #20]
.L821:
	ldrh	r2, [r3]
	movw	fp, #65535
	cmp	r2, fp
	beq	.L823
	add	r1, r1, #1
	ldr	fp, [sp, #20]
	uxth	r1, r1
	cmp	r1, fp
	bhi	.L820
	ldrh	fp, [r3, #4]
	cmp	fp, #0
	beq	.L822
	rsb	r3, r0, r3
	mov	r3, r3, asr #1
	mul	r3, ip, r3
	uxth	r3, r3
	mov	fp, r3, asl #1
	ldrh	fp, [r10, fp]
	cmp	fp, r6
	bls	.L827
	cmp	fp, r7
	movcc	r7, fp
	movcc	r5, r3
.L822:
	mla	r3, lr, r2, r0
	b	.L821
.L827:
	mov	r5, r3
.L823:
	movw	r3, #65535
	cmp	r5, r3
	beq	.L820
	mov	r3, r5, asl #1
	ldrh	fp, [r10, r3]
	cmp	fp, r6
	bls	.L825
	str	r3, [sp, #20]
	bl	GetFreeBlockMinEraseCount
	ldr	r3, [sp, #20]
	cmp	r0, r6
	strhi	r7, [r4, #-3312]
.L825:
	cmp	fp, r8
	bcs	.L820
	add	r2, fp, #128
	cmp	r9, r2
	ble	.L820
	add	r2, fp, #256
	cmp	r2, r8
	bcc	.L826
	ldr	r2, [r4, #-3316]
	add	fp, fp, #768
	cmp	fp, r2
	bcs	.L820
.L826:
	ldr	r2, [r4, #-3548]
	mov	r1, r5
	ldr	r0, .L839+16
	ldrh	r2, [r2, r3]
	str	r2, [sp]
	mov	r2, r8
	ldrh	r3, [r10, r3]
	stmib	sp, {r3, r9}
	ldr	r3, [r4, #-3316]
	bl	printk
	mov	r0, r5
	mov	r3, #1
	str	r3, [r4, #-2712]
.L819:
	add	sp, sp, #28
	@ sp needed
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L840:
	.align	2
.L839:
	.word	.LANCHOR2
	.word	.LANCHOR0
	.word	.LANCHOR0+2324
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
	beq	.L842
	ldr	r2, .L844
	mov	r3, r0, asl #1
	mov	r1, #0
	ldr	r2, [r2, #-3548]
	strh	r1, [r2, r3]	@ movhi
	bl	INSERT_FREE_LIST
.L842:
	mov	r0, #0
	ldmfd	sp!, {r3, pc}
.L845:
	.align	2
.L844:
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
	ldr	ip, .L854
	mov	r3, #0
	ldr	r1, .L854+4
	stmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, lr}
	.save {r4, r5, r6, r7, r8, r9, r10, lr}
	add	r7, ip, #78
	mov	r5, #12
	mov	r6, #1
	mov	r8, #36
	str	r3, [r1, #-2708]
.L847:
	ldrh	r2, [ip]
	add	r4, r3, #1
	uxth	r3, r3
	cmp	r3, r2
	bcs	.L852
	mul	r0, r5, r3
	ldr	lr, [r1, #-2704]
	add	r2, lr, r0
	str	r6, [r2, #8]
	ldrh	r2, [r7]
	mul	r2, r2, r3
	add	r9, r2, #3
	cmp	r2, #0
	movlt	r2, r9
	ldr	r9, [r1, #-2700]
	bic	r2, r2, #3
	add	r2, r9, r2
	str	r2, [lr, r0]
	ldr	r2, .L854+8
	ldr	r9, [r1, #-2704]
	ldrh	r2, [r2]
	add	lr, r9, r0
	mul	r2, r2, r3
	add	r10, r2, #3
	cmp	r2, #0
	movlt	r2, r10
	ldr	r10, [r1, #-2696]
	bic	r2, r2, #3
	add	r2, r10, r2
	str	r2, [lr, #4]
	ldr	r2, [r1, #-2692]
	mla	r3, r8, r3, r2
	ldr	r2, [r9, r0]
	str	r2, [r3, #8]
	ldr	r2, [lr, #4]
	str	r2, [r3, #12]
	mov	r3, r4
	b	.L847
.L852:
	ldr	r0, .L854+4
	mov	ip, #12
	ldr	r4, .L854+12
	mov	lr, #0
.L849:
	ldr	r3, [r1, #-2688]
	cmp	r2, r3
	bcs	.L853
	mul	r5, ip, r2
	ldr	r7, [r0, #-2704]
	add	r3, r7, r5
	str	lr, [r3, #8]
	ldrh	r3, [r4]
	mul	r3, r3, r2
	add	r6, r3, #3
	cmp	r3, #0
	movlt	r3, r6
	ldr	r6, [r0, #-2700]
	bic	r3, r3, #3
	add	r3, r6, r3
	str	r3, [r7, r5]
	ldr	r3, .L854+8
	ldr	r6, [r0, #-2704]
	ldrh	r3, [r3]
	add	r5, r6, r5
	mul	r3, r3, r2
	add	r2, r2, #1
	uxth	r2, r2
	add	r6, r3, #3
	cmp	r3, #0
	movlt	r3, r6
	ldr	r6, [r0, #-2696]
	bic	r3, r3, #3
	add	r3, r6, r3
	str	r3, [r5, #4]
	b	.L849
.L853:
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, pc}
.L855:
	.align	2
.L854:
	.word	.LANCHOR0+2316
	.word	.LANCHOR2
	.word	.LANCHOR0+2396
	.word	.LANCHOR0+2394
	.fnend
	.size	FtlGcBufInit, .-FtlGcBufInit
	.align	2
	.global	FtlGcBufFree
	.type	FtlGcBufFree, %function
FtlGcBufFree:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	ldr	r3, .L864
	stmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, lr}
	.save {r4, r5, r6, r7, r8, r9, r10, lr}
	mov	lr, #0
	ldr	r6, [r3, #-2688]
	mov	r5, #36
	ldr	r4, [r3, #-2704]
	mov	r7, lr
	mov	r8, #12
.L857:
	uxth	ip, lr
	cmp	ip, r1
	ldmcsfd	sp!, {r4, r5, r6, r7, r8, r9, r10, pc}
	mla	ip, r5, ip, r0
	mov	r2, #0
.L858:
	uxth	r3, r2
	cmp	r3, r6
	bcs	.L859
	mul	r3, r8, r3
	add	r2, r2, #1
	add	r9, r4, r3
	ldr	r10, [r4, r3]
	ldr	r3, [ip, #8]
	cmp	r10, r3
	bne	.L858
	str	r7, [r9, #8]
.L859:
	add	lr, lr, #1
	b	.L857
.L865:
	.align	2
.L864:
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
	ldr	r3, .L875
	mov	ip, #0
	stmfd	sp!, {r4, r5, r6, r7, r8, r9, lr}
	.save {r4, r5, r6, r7, r8, r9, lr}
	mov	r6, #12
	ldr	r4, [r3, #-2688]
	mov	r7, #1
	ldr	r5, [r3, #-2704]
	mov	r8, #36
.L867:
	uxth	r2, ip
	cmp	r2, r1
	bcs	.L874
	mov	lr, #0
.L868:
	uxth	r3, lr
	cmp	r3, r4
	bcs	.L869
	mla	r3, r6, r3, r5
	add	lr, lr, #1
	ldr	r9, [r3, #8]
	cmp	r9, #0
	bne	.L868
	mla	r2, r8, r2, r0
	ldr	lr, [r3]
	str	r7, [r3, #8]
	str	lr, [r2, #8]
	ldr	r3, [r3, #4]
	str	r3, [r2, #12]
.L869:
	add	ip, ip, #1
	b	.L867
.L874:
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, pc}
.L876:
	.align	2
.L875:
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
	ldr	r2, .L883
	sub	r2, r2, #2672
	ldr	r3, [r2, #-12]
	ldrh	r2, [r2, #-8]
	add	r2, r3, r2, asl #1
.L878:
	cmp	r3, r2
	beq	.L882
	ldrh	r1, [r3], #2
	cmp	r1, r0
	bne	.L878
	mov	r0, #1
	bx	lr
.L882:
	mov	r0, #0
	bx	lr
.L884:
	.align	2
.L883:
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
	ldr	r4, .L893
	ubfx	r0, r0, #10, #16
	mov	r6, r1
	mov	r7, r2
	bl	P2V_block_in_plane
	sub	r3, r4, #2672
	ldr	r2, [r4, #-2684]
	mov	lr, #0
	ldrh	r1, [r3, #-8]
	sub	r8, r2, #2
.L886:
	uxth	ip, lr
	cmp	ip, r1
	bcs	.L890
	ldrh	r9, [r8, #2]!
	add	lr, lr, #1
	cmp	r9, r0
	bne	.L886
.L890:
	cmp	ip, r1
	moveq	ip, ip, asl #1
	streqh	r0, [r2, ip]	@ movhi
	ldreqh	ip, [r3, #-8]
	addeq	ip, ip, #1
	streqh	ip, [r3, #-8]	@ movhi
.L888:
	ldrh	r2, [r3]
	mov	ip, #12
	mul	ip, ip, r2
	ldr	r2, [r4, #-2676]
	add	r1, r2, ip
	stmib	r1, {r6, r7}
	str	r5, [r2, ip]
	ldrh	r2, [r3]
	add	r2, r2, #1
	strh	r2, [r3]	@ movhi
	ldmfd	sp!, {r3, r4, r5, r6, r7, r8, r9, pc}
.L894:
	.align	2
.L893:
	.word	.LANCHOR2
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
	ldr	r4, .L903
	ldrh	r3, [r4, #-14]
	cmp	r3, r0
	beq	.L897
	ldrh	r3, [r4, #-12]
	cmp	r3, r0
	beq	.L897
	ldrh	r3, [r4, #-10]
	cmp	r3, r0
	beq	.L897
	ldrh	r3, [r4, #-8]
	cmp	r3, r0
	beq	.L897
	ldr	r0, .L903+4
	mov	r1, r5
	bl	printk
	ldrh	r2, [r4, #-14]
	movw	r3, #65535
	cmp	r2, r3
	streqh	r5, [r4, #-14]	@ movhi
	beq	.L897
	ldrh	r2, [r4, #-12]
	cmp	r2, r3
	streqh	r5, [r4, #-12]	@ movhi
	beq	.L897
	ldrh	r2, [r4, #-10]
	cmp	r2, r3
	streqh	r5, [r4, #-10]	@ movhi
	beq	.L897
	ldrh	r2, [r4, #-8]
	cmp	r2, r3
	streqh	r5, [r4, #-8]	@ movhi
.L897:
	mov	r0, #0
	ldmfd	sp!, {r3, r4, r5, pc}
.L904:
	.align	2
.L903:
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
	ldr	r4, .L916
	ldrh	r3, [r4, #-14]
	cmp	r3, r0
	beq	.L913
	ldrh	r3, [r4, #-12]
	cmp	r3, r0
	beq	.L913
	ldrh	r3, [r4, #-10]
	cmp	r3, r0
	beq	.L913
	ldrh	r3, [r4, #-8]
	cmp	r3, r0
	beq	.L913
	ldr	r0, .L916+4
	mov	r1, r5
	bl	printk
	ldrh	r2, [r4, #-14]
	movw	r3, #65535
	cmp	r2, r3
	streqh	r5, [r4, #-14]	@ movhi
	beq	.L913
	ldrh	r2, [r4, #-12]
	cmp	r2, r3
	streqh	r5, [r4, #-12]	@ movhi
	beq	.L913
	ldrh	r2, [r4, #-10]
	cmp	r2, r3
	streqh	r5, [r4, #-10]	@ movhi
	beq	.L913
	ldrh	r2, [r4, #-8]
	cmp	r2, r3
	bne	.L914
	strh	r5, [r4, #-8]	@ movhi
.L913:
	mov	r0, #0
	ldmfd	sp!, {r3, r4, r5, pc}
.L914:
	mvn	r0, #0
	ldmfd	sp!, {r3, r4, r5, pc}
.L917:
	.align	2
.L916:
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
	ldr	r6, .L928
	mov	r2, r5
	sub	r7, r6, #2656
	ldrh	r1, [r7, #-6]
	mov	r4, r0
	ldr	r0, .L928+4
	bl	printk
	mov	r0, r4
	bl	FtlGcRefreshBlock
	ldr	r3, .L928+8
	ldr	r3, [r3, #2308]
	cmp	r3, #0
	beq	.L919
	ldr	r2, [r6, #-3612]
	mov	r4, r4, asl #1
	ldrh	r3, [r2, r4]
	cmp	r3, #39
	subhi	r3, r3, #40
	strhih	r3, [r2, r4]	@ movhi
.L919:
	ldrh	r3, [r7, #-6]
	mov	r2, #0
	ldr	r0, .L928+12
.L920:
	uxth	r1, r2
	cmp	r1, r3
	bcs	.L927
	add	r2, r2, #1
	add	r1, r0, r2, asl #1
	ldrh	r1, [r1, #-2]
	cmp	r1, r5
	bne	.L920
	b	.L921
.L927:
	cmp	r3, #15
	addls	r2, r3, #1
	strlsh	r2, [r7, #-6]	@ movhi
	ldrls	r2, .L928+12
	movls	r3, r3, asl #1
	strlsh	r5, [r2, r3]	@ movhi
.L921:
	mov	r0, #0
	ldmfd	sp!, {r3, r4, r5, r6, r7, pc}
.L929:
	.align	2
.L928:
	.word	.LANCHOR2
	.word	.LC80
	.word	.LANCHOR0
	.word	.LANCHOR2-2660
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
	ldr	r4, .L937
	ldrh	r3, [r4, #-6]
	cmp	r3, #0
	beq	.L931
	ldrh	r1, [r4, #-14]
	movw	r2, #65535
	cmp	r1, r2
	bne	.L931
	add	r4, r4, #32
	ldrh	r2, [r4, #-2]
	cmp	r2, r3
	ldr	r2, .L937+4
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
.L931:
	mov	r0, #0
	ldmfd	sp!, {r4, pc}
.L938:
	.align	2
.L937:
	.word	.LANCHOR2-2656
	.word	.LANCHOR2-2660
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
	bne	.L941
	ldr	r3, .L976
.L942:
	mov	r2, r0, asl #1
	strh	r0, [r2, r3]	@ movhi
	add	r0, r0, #1
	cmp	r0, #256
	bne	.L942
.L946:
	mov	r1, #255
	ldr	r0, .L976+4
	mov	r2, #2048
	uxth	r4, r4
	bl	ftl_memset
	mov	r3, #0
	ldr	r1, .L976
	ldr	r0, .L976+4
	b	.L943
.L941:
	cmp	r0, #1
	bne	.L944
	ldr	ip, .L976
	mov	r3, #0
.L945:
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
	bne	.L945
	b	.L946
.L944:
	cmp	r0, #2
	bne	.L947
	ldr	r1, .L976
	mov	r3, #0
.L948:
	uxth	r2, r3
	mov	r0, r3, asl #1
	cmp	r2, #1
	add	r3, r3, #1
	mov	ip, r2, asl #1
	subhi	r2, ip, #1
	cmp	r3, #256
	strh	r2, [r0, r1]	@ movhi
	bne	.L948
	b	.L946
.L947:
	cmp	r0, #3
	bne	.L949
	ldr	ip, .L976
	mov	r3, #0
.L950:
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
	bne	.L950
	b	.L946
.L949:
	cmp	r0, #4
	mov	r3, #0
	bne	.L951
	ldr	r2, .L976+8
	strh	r3, [r2, #144]	@ movhi
	mov	r3, #1
	strh	r0, [r2, #152]	@ movhi
	strh	r3, [r2, #146]	@ movhi
	mov	r3, #2
	strh	r3, [r2, #148]	@ movhi
	mov	r3, #3
	strh	r3, [r2, #150]	@ movhi
	mov	r3, #5
	strh	r3, [r2, #154]	@ movhi
	mov	r3, #7
	strh	r3, [r2, #156]	@ movhi
	mov	r3, #8
	strh	r3, [r2, #158]!	@ movhi
.L952:
	tst	r3, #1
	movne	r1, #7
	moveq	r1, #6
	rsb	r1, r1, r3, asl #1
	add	r3, r3, #1
	strh	r1, [r2, #2]!	@ movhi
	uxth	r3, r3
	cmp	r3, #256
	bne	.L952
	b	.L946
.L951:
	cmp	r0, #5
	bne	.L953
	ldr	r2, .L976
.L954:
	mov	r1, r3, asl #1
	strh	r3, [r1, r2]	@ movhi
	add	r3, r3, #1
	cmp	r3, #16
	bne	.L954
	ldr	r2, .L976+12
.L955:
	strh	r3, [r2, #2]!	@ movhi
	add	r3, r3, #2
	uxth	r3, r3
	cmp	r3, #496
	bne	.L955
	b	.L946
.L953:
	cmp	r0, #6
	bne	.L956
	ldr	r1, .L976+16
.L957:
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
	bne	.L957
	b	.L946
.L956:
	cmp	r0, #9
	bne	.L958
	ldr	r2, .L976+8
	movw	r1, #509
	strh	r3, [r2, #144]	@ movhi
	mov	r3, #1
	strh	r3, [r2, #146]	@ movhi
	mov	r3, #2
	strh	r3, [r2, #148]!	@ movhi
	mov	r3, #3
.L959:
	strh	r3, [r2, #2]!	@ movhi
	add	r3, r3, #2
	uxth	r3, r3
	cmp	r3, r1
	bne	.L959
	b	.L946
.L958:
	cmp	r0, #10
	bne	.L946
	ldr	r2, .L976
.L960:
	mov	r1, r3, asl #1
	strh	r3, [r1, r2]	@ movhi
	add	r3, r3, #1
	cmp	r3, #63
	bne	.L960
	ldr	r2, .L976+20
	movw	r1, #961
.L961:
	strh	r3, [r2, #2]!	@ movhi
	add	r3, r3, #2
	uxth	r3, r3
	cmp	r3, r1
	bne	.L961
	b	.L946
.L943:
	uxth	r2, r3
	cmp	r2, r4
	bcs	.L975
	mov	r2, r3, asl #1
	add	r3, r3, #1
	ldrh	r2, [r2, r1]
	mov	ip, r2, asl #1
	strh	r2, [r0, ip]	@ movhi
	b	.L943
.L975:
	ldmfd	sp!, {r4, pc}
.L977:
	.align	2
.L976:
	.word	.LANCHOR0+144
	.word	.LANCHOR2-2624
	.word	.LANCHOR0
	.word	.LANCHOR0+174
	.word	.LANCHOR0+142
	.word	.LANCHOR0+268
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
	ldr	r3, .L993
	ldr	r4, .L993+4
	ldr	r9, .L993+8
	ldrh	r0, [r3, #10]
	strb	r6, [r4, #2229]
	mov	r7, r4
	strb	r6, [r9, #-576]
	bl	FlashBlockAlignInit
	mov	r1, r6
	mov	r2, #8
	ldr	r0, .L993+12
	bl	ftl_memset
	mov	r1, r6
	mov	r2, #32
	add	r0, r4, #1168
	bl	ftl_memset
	ldr	r0, .L993+16
	mov	r1, r6
	mov	r2, #128
	bl	ftl_memset
	ldr	r5, [r4, #1204]
	ldr	fp, .L993+20
	add	r8, r5, #1
.L980:
	mov	r0, r8
	add	r1, fp, r6, asl #3
	ldrb	r2, [r5]	@ zero_extendqisi2
	bl	FlashMemCmp8
	ldr	r10, .L993+20
	cmp	r0, #0
	bne	.L979
	ldrb	r3, [r7, #2229]	@ zero_extendqisi2
	add	r2, r7, r3, asl #2
	str	r0, [r2, #1168]
	add	r2, r3, #1
	add	r3, r7, r3
	strb	r2, [r7, #2229]
	strb	r6, [r3, #2232]
.L979:
	add	r6, r6, #1
	cmp	r6, #4
	bne	.L980
	ldrb	r3, [r4, #2229]	@ zero_extendqisi2
	ldr	r7, .L993+4
	strb	r3, [r9, #-576]
	ldrb	r3, [r5, #8]	@ zero_extendqisi2
	cmp	r3, #2
	beq	.L981
.L985:
	ldrb	r3, [r5, #13]	@ zero_extendqisi2
	ldrb	r2, [r4, #2229]	@ zero_extendqisi2
	smulbb	r2, r2, r3
	ldrh	r3, [r5, #14]
	smulbb	r3, r2, r3
	ldr	r2, .L993+24
	strh	r3, [r2, #-2]	@ movhi
	ldmfd	sp!, {r3, r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L981:
	ldr	r9, [r7, #4]
	mov	r6, #0
.L984:
	mov	r0, r8
	add	r1, r10, r6, asl #3
	ldrb	r2, [r5]	@ zero_extendqisi2
	bl	FlashMemCmp8
	cmp	r0, #0
	bne	.L982
	ldrb	r1, [r5, #13]	@ zero_extendqisi2
	ldrh	r3, [r5, #14]
	ldrb	r2, [r7, #2229]	@ zero_extendqisi2
	mul	r1, r9, r1
	and	r3, r3, #65280
	add	r0, r7, r2, asl #2
	mul	r3, r3, r1
	str	r3, [r0, #1168]
	ldrb	r1, [r5, #23]	@ zero_extendqisi2
	cmp	r1, #0
	movne	r3, r3, asl #1
	strne	r3, [r0, #1168]
	add	r3, r2, #1
	add	r2, r4, r2
	strb	r3, [r4, #2229]
	strb	r6, [r2, #2232]
.L982:
	add	r6, r6, #1
	cmp	r6, #4
	bne	.L984
	b	.L985
.L994:
	.align	2
.L993:
	.word	.LANCHOR1+472
	.word	.LANCHOR0
	.word	.LANCHOR2
	.word	.LANCHOR0+2232
	.word	.LANCHOR0+2100
	.word	.LANCHOR0+2068
	.word	.LANCHOR2-572
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
	ldr	r2, .L1000
	ldr	r0, .L1000+4
	mov	ip, #1
	ldr	r3, [r2, #1204]
	ldrb	r1, [r3, #9]	@ zero_extendqisi2
	ldr	r3, [r2, #4]
	smulbb	r3, r1, r3
	strh	r3, [r4, #4]	@ unaligned
	ldrb	r3, [r2, #2304]	@ zero_extendqisi2
	strb	r3, [r4, #7]
	ldr	r3, [r2, #2424]
	str	r3, [r4]	@ unaligned
	ldr	r3, [r2, #1204]
	ldrb	r1, [r3, #9]	@ zero_extendqisi2
	strb	r1, [r4, #6]
	mov	r1, #32
	strb	r1, [r4, #8]
	ldrb	r1, [r2, #2229]	@ zero_extendqisi2
	ldrb	r3, [r3, #7]	@ zero_extendqisi2
	strb	r3, [r4, #9]
	mov	r3, #0
	strb	r3, [r4, #10]
.L996:
	uxtb	r2, r3
	cmp	r2, r1
	bcs	.L999
	ldrb	lr, [r3, r0]	@ zero_extendqisi2
	add	r3, r3, #1
	ldrb	r2, [r4, #10]	@ zero_extendqisi2
	orr	r2, r2, ip, asl lr
	strb	r2, [r4, #10]
	b	.L996
.L999:
	ldmfd	sp!, {r4, pc}
.L1001:
	.align	2
.L1000:
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
	stmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, lr}
	.save {r4, r5, r6, r7, r8, r9, r10, lr}
	mov	r6, #0
	ldr	r4, .L1107
	mvn	r2, #0
	ldr	r5, .L1107+4
	mov	r1, #32
	sub	r3, r4, #572
	mov	r0, #1024
	str	r6, [r4, #-568]
	mov	r8, #12
	strh	r6, [r3]	@ movhi
	movw	r3, #65535
	str	r3, [r4, #-560]
	sub	r3, r4, #2656
	str	r6, [r4, #-3240]
	movw	r10, #2316
	strh	r2, [r3, #-14]	@ movhi
	mov	r7, #36
	strh	r2, [r3, #-12]	@ movhi
	strh	r2, [r3, #-10]	@ movhi
	strh	r2, [r3, #-8]	@ movhi
	sub	r2, r4, #2720
	strh	r6, [r3, #-6]	@ movhi
	sub	r3, r4, #548
	strh	r1, [r2, #-4]	@ movhi
	mov	r1, #128
	strh	r6, [r3]	@ movhi
	sub	r3, r4, #2624
	strh	r1, [r2, #-2]	@ movhi
	sub	r2, r4, #2704
	strh	r6, [r3, #-2]	@ movhi
	movw	r3, #2390
	strh	r6, [r2, #-12]	@ movhi
	str	r6, [r4, #-3336]
	str	r6, [r4, #-3332]
	str	r6, [r4, #-3348]
	str	r6, [r4, #-3360]
	str	r6, [r4, #-3364]
	str	r6, [r4, #-3356]
	str	r6, [r4, #-3352]
	str	r6, [r4, #-3368]
	str	r6, [r4, #-3328]
	str	r6, [r4, #-3324]
	str	r6, [r4, #-3608]
	str	r6, [r4, #-3316]
	str	r6, [r4, #-3312]
	str	r6, [r4, #-564]
	str	r6, [r4, #-2712]
	str	r6, [r4, #-556]
	str	r6, [r4, #-2720]
	str	r6, [r4, #-552]
	ldrh	r1, [r5, r3]
	bl	__aeabi_idiv
	movw	r3, #2316
	ldrh	r3, [r5, r3]
	str	r6, [r5, #2436]
	movw	r6, #2388
	mov	r3, r3, asl #2
	cmp	r0, r3
	str	r0, [r4, #-544]
	ldrh	r0, [r5, r6]
	strhi	r3, [r4, #-544]
	mov	r0, r0, asl #1
	bl	ftl_malloc
	str	r0, [r4, #-2684]
	ldrh	r0, [r5, r6]
	mul	r0, r8, r0
	bl	ftl_malloc
	ldrh	r6, [r5, r10]
	mul	r6, r7, r6
	mov	r9, r6, asl #3
	str	r0, [r4, #-2676]
	mov	r0, r9
	bl	ftl_malloc
	str	r0, [r4, #-540]
	mov	r0, r6
	bl	ftl_malloc
	str	r0, [r4, #-536]
	mov	r0, r9
	bl	ftl_malloc
	movw	r9, #2394
	str	r0, [r4, #-532]
	mov	r0, r6
	bl	ftl_malloc
	str	r0, [r4, #-3616]
	mov	r0, r6
	bl	ftl_malloc
	str	r0, [r4, #-2692]
	ldr	r0, [r4, #-544]
	mul	r0, r7, r0
	bl	ftl_malloc
	ldrh	r6, [r5, r9]
	ldrh	r3, [r5, r10]
	movw	r7, #2396
	mov	r3, r3, asl #1
	add	r3, r3, #1
	str	r3, [r4, #-2688]
	str	r0, [r5, #2440]
	mov	r0, r6
	bl	ftl_malloc
	str	r0, [r4, #-528]
	mov	r0, r6
	bl	ftl_malloc
	str	r0, [r4, #-524]
	mov	r0, r6
	bl	ftl_malloc
	str	r0, [r4, #-520]
	ldr	r0, [r4, #-2688]
	mul	r0, r0, r6
	bl	ftl_malloc
	str	r0, [r4, #-2700]
	ldr	r0, [r4, #-544]
	mul	r0, r0, r6
	bl	ftl_malloc
	str	r0, [r4, #-516]
	mov	r0, r6
	bl	ftl_malloc
	str	r0, [r4, #-512]
	mov	r0, r6
	bl	ftl_malloc
	str	r0, [r4, #-508]
	ldr	r0, [r4, #-2688]
	mul	r0, r8, r0
	bl	ftl_malloc
	ldrh	r3, [r5, r10]
	ldrh	r6, [r5, r7]
	mul	r6, r3, r6
	str	r0, [r4, #-2704]
	mov	r0, r6
	bl	ftl_malloc
	str	r0, [r4, #-504]
	mov	r0, r6, asl #3
	bl	ftl_malloc
	ldrh	r3, [r5, r7]
	ldr	r6, .L1107+8
	str	r0, [r4, #-500]
	ldr	r0, [r4, #-2688]
	mul	r0, r0, r3
	bl	ftl_malloc
	ldrh	r3, [r5, r7]
	movw	r7, #2326
	str	r0, [r4, #-2696]
	ldr	r0, [r4, #-544]
	mul	r0, r0, r3
	bl	ftl_malloc
	str	r0, [r4, #-496]
	ldrh	r0, [r5, r7]
	mov	r0, r0, asl #1
	uxth	r0, r0
	strh	r0, [r6]	@ movhi
	bl	ftl_malloc
	str	r0, [r4, #-488]
	ldrh	r0, [r6]
	add	r0, r0, #544
	add	r0, r0, #3
	mov	r0, r0, lsr #9
	strh	r0, [r6]	@ movhi
	mov	r0, r0, asl #9
	bl	ftl_malloc
	ldrh	r10, [r5, r7]
	mov	r10, r10, asl #1
	str	r0, [r4, #-484]
	add	r0, r0, #32
	str	r0, [r4, #-3612]
	mov	r0, r10
	bl	ftl_malloc
	str	r0, [r4, #-480]
	mov	r0, r10
	bl	ftl_malloc
	ldr	r10, [r5, #2412]
	mov	r10, r10, asl #1
	str	r0, [r4, #-3548]
	mov	r0, r10
	bl	ftl_malloc
	str	r0, [r4, #-476]
	mov	r0, r10
	bl	ftl_malloc
	movw	r10, #2404
	str	r0, [r4, #-472]
	ldrh	r0, [r5, r7]
	mov	r0, r0, lsr #3
	add	r0, r0, #4
	bl	ftl_malloc
	str	r0, [r4, #-3372]
	ldrh	r0, [r5, r10]
	mov	r0, r0, asl #1
	bl	ftl_malloc
	str	r0, [r5, #2432]
	ldrh	r0, [r5, r10]
	mov	r0, r0, asl #1
	bl	ftl_malloc
	str	r0, [r4, #-468]
	ldrh	r0, [r5, r10]
	movw	r10, #2406
	mov	r0, r0, asl #2
	bl	ftl_malloc
	str	r0, [r4, #-464]
	ldrh	r0, [r5, r10]
	mov	r0, r0, asl #2
	bl	ftl_malloc
	ldrh	r2, [r5, r10]
	mov	r1, #0
	mov	r2, r2, asl #2
	str	r0, [r4, #-460]
	bl	ftl_memset
	movw	r3, #2420
	ldrh	r10, [r5, r3]
	mov	r10, r10, asl #2
	mov	r0, r10
	bl	ftl_malloc
	str	r0, [r4, #-456]
	mov	r0, r10
	bl	ftl_malloc
	movw	r10, #2422
	str	r0, [r4, #-452]
	ldr	r0, [r5, #2412]
	mov	r0, r0, asl #2
	bl	ftl_malloc
	str	r0, [r4, #-448]
	ldrh	r0, [r5, r10]
	mul	r0, r8, r0
	bl	ftl_malloc
	ldrh	r3, [r5, r10]
	str	r0, [r4, #-3384]
	ldrh	r0, [r5, r9]
	mul	r0, r0, r3
	bl	ftl_malloc
	ldrh	r3, [r5, r7]
	movw	r7, #2338
	str	r0, [r4, #-444]
	mov	r0, #6
	mul	r0, r0, r3
	bl	ftl_malloc
	movw	r3, #2382
	ldrh	r3, [r5, r3]
	add	r3, r3, #31
	mov	r3, r3, asr #5
	strh	r3, [r6, #52]!	@ movhi
	str	r0, [r4, #-3556]
	ldrh	r0, [r5, r7]
	mul	r0, r0, r3
	mov	r0, r0, asl #2
	bl	ftl_malloc
	ldrh	r2, [r6]
	ldrh	lr, [r5, r7]
	mov	r3, #1
	ldr	ip, .L1107+12
	mov	r2, r2, asl #2
	mov	r1, r2
	str	r0, [r5, #2476]
.L1004:
	cmp	r3, lr
	bcs	.L1105
	ldr	r0, [r5, #2476]
	add	r3, r3, #1
	add	r0, r0, r1
	add	r1, r1, r2
	str	r0, [ip, #4]!
	b	.L1004
.L1105:
	ldr	r2, .L1107+16
	mov	r1, #0
.L1006:
	cmp	r3, #8
	addne	r0, r2, r3, asl #2
	addne	r3, r3, #1
	strne	r1, [r0, #28]
	bne	.L1006
.L1106:
	ldr	r2, [r4, #-476]
	ldr	r3, .L1107
	cmp	r2, #0
	bne	.L1008
.L1010:
	ldr	r1, .L1107+20
	ldr	r0, .L1107+24
	bl	printk
	mvn	r0, #0
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, pc}
.L1008:
	ldr	r2, [r3, #-472]
	cmp	r2, #0
	beq	.L1010
	ldr	r2, [r3, #-456]
	cmp	r2, #0
	beq	.L1010
	ldr	r2, [r3, #-448]
	cmp	r2, #0
	beq	.L1010
	ldr	r2, [r3, #-3384]
	cmp	r2, #0
	beq	.L1010
	ldr	r2, [r3, #-444]
	cmp	r2, #0
	beq	.L1010
	ldr	r2, [r3, #-3556]
	cmp	r2, #0
	beq	.L1010
	ldr	r2, [r5, #2476]
	cmp	r2, #0
	beq	.L1010
	ldr	r3, [r3, #-3548]
	cmp	r3, #0
	beq	.L1010
	ldr	r2, [r4, #-2684]
	ldr	r3, .L1107
	cmp	r2, #0
	beq	.L1010
	ldr	r2, [r3, #-2676]
	cmp	r2, #0
	beq	.L1010
	ldr	r2, [r3, #-540]
	cmp	r2, #0
	beq	.L1010
	ldr	r2, [r3, #-532]
	cmp	r2, #0
	beq	.L1010
	ldr	r2, [r3, #-3616]
	cmp	r2, #0
	beq	.L1010
	ldr	r2, [r3, #-2692]
	cmp	r2, #0
	beq	.L1010
	ldr	r2, [r3, #-536]
	cmp	r2, #0
	beq	.L1010
	ldr	r2, [r3, #-528]
	cmp	r2, #0
	beq	.L1010
	ldr	r2, [r3, #-524]
	cmp	r2, #0
	beq	.L1010
	ldr	r3, [r3, #-520]
	cmp	r3, #0
	beq	.L1010
	ldr	r2, [r4, #-2700]
	ldr	r3, .L1107
	cmp	r2, #0
	beq	.L1010
	ldr	r2, [r3, #-512]
	cmp	r2, #0
	beq	.L1010
	ldr	r2, [r3, #-508]
	cmp	r2, #0
	beq	.L1010
	ldr	r2, [r3, #-2704]
	cmp	r2, #0
	beq	.L1010
	ldr	r2, [r3, #-504]
	cmp	r2, #0
	beq	.L1010
	ldr	r2, [r3, #-500]
	cmp	r2, #0
	beq	.L1010
	ldr	r2, [r3, #-2696]
	cmp	r2, #0
	beq	.L1010
	ldr	r2, [r3, #-3612]
	cmp	r2, #0
	beq	.L1010
	ldr	r3, [r3, #-488]
	cmp	r3, #0
	beq	.L1010
	ldr	r3, .L1107+4
	ldr	r3, [r3, #2432]
	cmp	r3, #0
	beq	.L1010
	ldr	r3, .L1107
	ldr	r2, [r3, #-468]
	cmp	r2, #0
	beq	.L1010
	ldr	r2, [r3, #-464]
	cmp	r2, #0
	beq	.L1010
	ldr	r3, [r3, #-460]
	cmp	r3, #0
	beq	.L1010
	mov	r0, #0
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, pc}
.L1108:
	.align	2
.L1107:
	.word	.LANCHOR2
	.word	.LANCHOR0
	.word	.LANCHOR2-492
	.word	.LANCHOR0+2476
	.word	.LANCHOR0+2448
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
	ldr	r3, .L1115
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
	ldr	ip, .L1115+4
	add	r3, r3, #2
	sub	r1, r4, #2
	mov	r4, #1
.L1111:
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
	bne	.L1111
	ldmfd	sp!, {r4, r5, r6, pc}
.L1116:
	.align	2
.L1115:
	.word	.LANCHOR2-440
	.word	.LANCHOR0+2448
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
	ldr	r0, .L1118
	mvn	r3, #0
	mov	r1, #255
	mov	r2, #16
	add	r0, r0, #12
	strh	r3, [r0, #-12]	@ movhi
	mov	r3, #0
	strh	r3, [r0, #-6]	@ movhi
	b	ftl_memset
.L1119:
	.align	2
.L1118:
	.word	.LANCHOR0+2448
	.fnend
	.size	FtlBbtMemInit, .-FtlBbtMemInit
	.align	2
	.global	FtlFreeSysBlkQueueInit
	.type	FtlFreeSysBlkQueueInit, %function
FtlFreeSysBlkQueueInit:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	ldr	r1, .L1122
	movw	r2, #2508
	stmfd	sp!, {r4, lr}
	.save {r4, lr}
	add	r3, r1, r2
	mov	r4, #0
	strh	r0, [r1, r2]	@ movhi
	add	r0, r3, #8
	mov	r1, r4
	mov	r2, #2048
	strh	r4, [r3, #2]	@ movhi
	strh	r4, [r3, #4]	@ movhi
	strh	r4, [r3, #6]	@ movhi
	bl	ftl_memset
	mov	r0, r4
	ldmfd	sp!, {r4, pc}
.L1123:
	.align	2
.L1122:
	.word	.LANCHOR0
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
.L1125:
	ldrh	r1, [r4, #6]
	uxth	r3, r2
	cmp	r1, r3
	bls	.L1145
	ldr	r0, [r6, r3, asl #2]
	mov	r1, #0
	ubfx	r0, r0, #10, #16
.L1126:
	ldrh	ip, [r4, #10]
	uxth	r3, r1
	cmp	ip, r3
	bls	.L1146
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
	b	.L1126
.L1146:
	add	r2, r2, #1
	b	.L1125
.L1145:
	ldr	r2, .L1148
	movw	r3, #2386
	mov	r8, #0
	mov	r1, r8
	mov	fp, r8
	ldrh	r2, [r2, r3]
	ldrh	r3, [r4]
	mov	r3, r3, asl #1
	strh	r2, [r5, r3]	@ movhi
	ldrh	r9, [r5]
.L1130:
	ldrh	r3, [r4, #10]
	uxth	r6, r8
	cmp	r3, r6
	bls	.L1147
	mov	r2, r6, asl #1
	ldrh	r3, [r5, r2]
	cmp	r9, r3
	bls	.L1131
	ldrh	r0, [r7, r2]
	add	r10, r7, r2
	cmp	r0, #0
	bne	.L1132
	b	.L1133
.L1131:
	cmp	r3, #0
	bne	.L1133
	ldrh	r0, [r7, r2]
	add	r10, r7, r2
	cmp	r0, #0
	movne	r6, r1
	beq	.L1133
	b	.L1135
.L1132:
	cmp	r3, #0
	movne	r1, r6
	movne	r9, r3
	bne	.L1133
	mov	r9, r3
.L1135:
	mov	r1, #1
	bl	FtlFreeSysBlkQueueIn
	strh	fp, [r10]	@ movhi
	ldrh	r3, [r4, #8]
	mov	r1, r6
	sub	r3, r3, #1
	strh	r3, [r4, #8]	@ movhi
.L1133:
	add	r8, r8, #1
	b	.L1130
.L1147:
	mov	r0, r1
	ldmfd	sp!, {r3, r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L1149:
	.align	2
.L1148:
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
	ldr	r5, .L1155
	mvn	r6, #0
	ldr	r4, .L1155+4
	ldr	r2, [r5, #2412]
	ldr	r0, [r4, #-472]
	mov	r2, r2, asl #1
	bl	ftl_memset
	movw	r3, #2394
	movw	r2, #2422
	ldrh	r3, [r5, r3]
	ldrh	r2, [r5, r2]
	mov	r1, #255
	ldr	r0, [r4, #-444]
	mul	r2, r2, r3
	bl	ftl_memset
	ldr	ip, .L1155+8
	mov	r1, #0
	mov	r3, r4
	sub	r7, ip, #28
	mov	r4, #12
	mov	r5, r1
.L1151:
	ldrh	r2, [ip]
	add	lr, r1, #1
	uxth	r1, r1
	ldr	r0, .L1155
	cmp	r2, r1
	bls	.L1154
	mul	r0, r4, r1
	ldr	r2, [r3, #-3384]
	add	r8, r2, r0
	str	r5, [r8, #4]
	strh	r6, [r2, r0]	@ movhi
	ldr	r2, [r3, #-3384]
	add	r0, r2, r0
	ldrh	r2, [r7]
	mul	r2, r1, r2
	ldr	r1, [r3, #-444]
	bic	r2, r2, #3
	add	r2, r1, r2
	mov	r1, lr
	str	r2, [r0, #8]
	b	.L1151
.L1154:
	ldr	r2, .L1155+12
	mvn	r1, #0
	strh	r1, [r2, #2]	@ movhi
	strh	r1, [r2]	@ movhi
	ldr	r1, [r0, #2412]
	strh	r1, [r2, #10]	@ movhi
	ldr	r1, .L1155+16
	strh	r1, [r2, #4]	@ movhi
	ldrh	r1, [r2, #44]
	strh	r1, [r2, #8]	@ movhi
	movw	r1, #2420
	ldrh	r1, [r0, r1]
	strh	r1, [r2, #6]	@ movhi
	ldr	r2, [r3, #-476]
	str	r2, [r3, #-424]
	ldr	r2, [r3, #-448]
	str	r2, [r3, #-420]
	ldr	r2, [r3, #-472]
	str	r2, [r3, #-416]
	ldr	r2, [r3, #-456]
	str	r2, [r3, #-412]
	ldmfd	sp!, {r4, r5, r6, r7, r8, pc}
.L1156:
	.align	2
.L1155:
	.word	.LANCHOR0
	.word	.LANCHOR2
	.word	.LANCHOR0+2422
	.word	.LANCHOR2-436
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
	ldr	r5, .L1159
	mov	r4, #0
	ldr	r6, .L1159+4
	mov	r1, r4
	sub	r2, r5, #384
	movw	r7, #2326
	str	r3, [r5, #-372]
	strh	r3, [r2]	@ movhi
	movw	r3, #2430
	strh	r4, [r6, r3]	@ movhi
	movw	r3, #2404
	ldrh	r2, [r6, r3]
	ldr	r0, [r6, #2432]
	str	r4, [r5, #-388]
	mov	r2, r2, asl #1
	str	r4, [r5, #-380]
	str	r4, [r5, #-376]
	str	r4, [r6, #2308]
	bl	ftl_memset
	ldrh	r2, [r6, r7]
	mov	r1, r4
	ldr	r0, [r5, #-3612]
	mov	r2, r2, asl #1
	bl	ftl_memset
	ldrh	r2, [r6, r7]
	mov	r1, r4
	ldr	r0, [r5, #-488]
	mov	r2, r2, asl #1
	bl	ftl_memset
	sub	r0, r5, #3600
	mov	r1, r4
	mov	r2, #48
	sub	r0, r0, #4
	bl	ftl_memset
	sub	r0, r5, #3232
	mov	r1, r4
	mov	r2, #512
	sub	r0, r0, #4
	bl	ftl_memset
	bl	FtlGcBufInit
	bl	FtlL2PDataInit
	mov	r0, r4
	ldmfd	sp!, {r3, r4, r5, r6, r7, pc}
.L1160:
	.align	2
.L1159:
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
	movw	r3, #2326
	ldr	r7, .L1173
	mov	r2, #6
	ldr	r6, .L1173+4
	mov	r1, #0
	.pad #28
	sub	sp, sp, #28
	ldrh	r3, [r7, r3]
	ldr	r0, [r6, #-3556]
	mov	r5, r6
	mov	fp, r6
	mul	r2, r2, r3
	bl	ftl_memset
	sub	r2, r6, #3520
	mov	r3, #0
	sub	r1, r6, #3536
	strh	r3, [r2, #-12]	@ movhi
	sub	r2, r6, #572
	mov	r4, r3
	mov	r8, r3
	mov	r9, r3
	str	r3, [r6, #-3536]
	str	r3, [r6, #-3552]
	str	r3, [r6, #-3544]
	strh	r3, [r1, #-4]	@ movhi
	strh	r3, [r2]	@ movhi
	str	r7, [sp, #8]
	str	r1, [sp, #12]
.L1162:
	ldr	r3, .L1173+8
	uxth	r7, r4
	sxth	r10, r7
	ldrh	r2, [r3]
	cmp	r10, r2
	bge	.L1169
	sub	r3, r3, #8
	ldr	r2, .L1173+12
	mov	ip, r7
	ldrh	r3, [r3]
	str	r3, [sp]
	ldrh	r3, [r2]
	mov	r2, #0
	mov	r6, r2
	str	r3, [sp, #4]
.L1170:
	ldr	r3, [sp]
	sxth	r1, r2
	cmp	r1, r3
	bge	.L1172
	ldr	r3, [sp, #8]
	str	r2, [sp, #20]
	add	r1, r3, r1
	str	ip, [sp, #16]
	ldrb	r0, [r1, #2344]	@ zero_extendqisi2
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
	b	.L1170
.L1172:
	cmp	r6, #0
	beq	.L1165
	sxth	r1, r6
	mov	r0, #32768
	bl	__aeabi_idiv
	uxth	r6, r0
	b	.L1166
.L1165:
	sxth	r7, r7
	ldr	r2, [r5, #-3548]
	mvn	r1, #0
	mov	r7, r7, asl #1
	strh	r1, [r2, r7]	@ movhi
.L1166:
	mov	r1, r10, asl #1
	ldr	r2, [r5, #-3556]
	add	r0, r1, r10
	add	r2, r2, r0, asl #1
	strh	r6, [r2, #4]	@ movhi
	ldr	r2, .L1173+16
	ldrh	r0, [r2]
	cmp	r10, r0
	beq	.L1167
	ldrh	r0, [r2, #48]
	cmp	r10, r0
	beq	.L1167
	ldrh	r2, [r2, #96]
	cmp	r10, r2
	beq	.L1167
	ldr	r3, [fp, #-3548]
	uxth	r0, r4
	ldrh	r3, [r3, r1]
	cmp	r3, #0
	bne	.L1168
	add	r8, r8, #1
	uxth	r8, r8
	bl	INSERT_FREE_LIST
	b	.L1167
.L1168:
	add	r9, r9, #1
	uxth	r9, r9
	bl	INSERT_DATA_LIST
.L1167:
	add	r4, r4, #1
	b	.L1162
.L1169:
	ldr	r3, [sp, #12]
	mov	r0, #0
	strh	r9, [r3, #-4]	@ movhi
	ldr	r3, .L1173+20
	strh	r8, [r3, #-12]	@ movhi
	add	sp, sp, #28
	@ sp needed
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L1174:
	.align	2
.L1173:
	.word	.LANCHOR0
	.word	.LANCHOR2
	.word	.LANCHOR0+2324
	.word	.LANCHOR0+2384
	.word	.LANCHOR2-3528
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
	movw	r5, #2388
	ldr	r4, .L1177
	mov	r2, #0
	ldr	r6, .L1177+4
	mov	r1, #255
	sub	r3, r4, #2672
	ldr	r0, [r4, #-2684]
	strh	r2, [r3, #-8]	@ movhi
	strh	r2, [r3]	@ movhi
	ldrh	r2, [r6, r5]
	mov	r2, r2, asl #1
	bl	ftl_memset
	ldrh	r3, [r6, r5]
	mov	r2, #12
	ldr	r0, [r4, #-2676]
	mov	r1, #255
	mul	r2, r2, r3
	bl	ftl_memset
	ldmfd	sp!, {r4, r5, r6, lr}
	b	FtlGcBufInit
.L1178:
	.align	2
.L1177:
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
	ldr	r1, .L1182
	bl	ftl_memcpy
	mov	r0, #0
	ldmfd	sp!, {r3, pc}
.L1183:
	.align	2
.L1182:
	.word	.LANCHOR2-368
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
	ldr	r8, .L1197
.L1188:
	mov	r7, r4, asl #5
	ldr	r1, .L1197+4
	add	r0, r7, #1
	ldrb	r2, [r8, r4, asl #5]	@ zero_extendqisi2
	add	r0, r8, r0
	ldr	r6, .L1197+8
	bl	FlashMemCmp8
	add	r9, r6, #508
	subs	r5, r0, #0
	bne	.L1185
	adds	r9, r9, r7
	beq	.L1191
	add	r4, r6, r7
	mov	r3, r5
	ldr	r1, .L1197+12
	ldrb	r2, [r4, #530]	@ zero_extendqisi2
	b	.L1190
.L1185:
	add	r4, r4, #1
	cmp	r4, #83
	bne	.L1188
	b	.L1191
.L1196:
	add	r3, r3, #1
	cmp	r3, #4
	beq	.L1189
.L1190:
	ldrb	r0, [r1, r3, asl #5]	@ zero_extendqisi2
	cmp	r0, r2
	bne	.L1196
.L1189:
	ldr	r4, .L1197+16
	mov	r2, #32
	ldr	r1, .L1197+12
	ldr	r0, .L1197+20
	add	r1, r1, r3, asl #5
	bl	ftl_memcpy
	mov	r0, r4
	mov	r1, r9
	mov	r2, #32
	bl	ftl_memcpy
	ldrh	r0, [r4, #10]
	bl	FlashBlockAlignInit
	b	.L1186
.L1191:
	mvn	r5, #0
.L1186:
	mov	r0, r5
	ldmfd	sp!, {r3, r4, r5, r6, r7, r8, r9, pc}
.L1198:
	.align	2
.L1197:
	.word	.LANCHOR1+508
	.word	.LANCHOR0+2068
	.word	.LANCHOR1
	.word	.LANCHOR1+3164
	.word	.LANCHOR1+472
	.word	.LANCHOR0+44
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
	bne	.L1200
	cmp	r3, #0
	beq	.L1201
	mov	r0, r2
	mov	r1, r3
	mov	r2, #1024
	bl	ftl_memcpy
.L1201:
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
.L1200:
	cmp	r3, #0
	beq	.L1204
	mov	r1, r2
	mov	r0, r3
	mov	r2, #1024
	bl	ftl_memcpy
.L1204:
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
.L1217:
	cmp	r3, r2
	ldrne	ip, [r1, r3, asl #2]
	strne	ip, [r0, r3, asl #2]
	addne	r3, r3, #1
	bne	.L1217
.L1219:
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
	ldr	r3, .L1222
	ldr	r0, [r3]
	b	jiffies_to_msecs
.L1223:
	.align	2
.L1222:
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
	ldr	ip, .L1228
	cmp	r2, #0
	moveq	r2, r3
	ldr	ip, [ip, #1680]
	add	ip, ip, #4096
	add	r1, ip, r1
	beq	.L1227
	str	lr, [sp, #-4]!
	.save {lr}
	mov	lr, r0
	mov	r0, r1
	mov	r1, lr
	ldr	lr, [sp], #4
	mov	r2, r3
.L1227:
	b	ftl_memcpy
.L1229:
	.align	2
.L1228:
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
	ldr	r3, .L1234
	ldr	r0, [r3, #2424]
	bx	lr
.L1235:
	.align	2
.L1234:
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
.L1240:
	cmp	r4, r8
	beq	.L1249
	cmp	r5, #0
	bne	.L1241
	ldr	r0, .L1250
	mov	r1, r9
	mov	r2, r6
	mov	r3, r4
	bl	printk
.L1241:
	cmp	r7, #4
	ldreq	r0, .L1250+4
	ldreq	r1, [r6, r4, asl #2]
	beq	.L1248
	cmp	r7, #2
	moveq	r3, r4, asl #1
	ldreq	r0, .L1250+4
	ldreqsh	r1, [r6, r3]
	ldrne	r0, .L1250+4
	ldrneb	r1, [r6, r4]	@ zero_extendqisi2
.L1248:
	add	r5, r5, #1
	bl	printk
	cmp	r5, #15
	bls	.L1245
	ldr	r0, .L1250+8
	mov	r5, #0
	ldr	r1, .L1250+12
	bl	printk
.L1245:
	add	r4, r4, #1
	b	.L1240
.L1249:
	ldr	r0, .L1250+8
	ldr	r1, .L1250+12
	ldmfd	sp!, {r3, r4, r5, r6, r7, r8, r9, lr}
	b	printk
.L1251:
	.align	2
.L1250:
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
	ldr	r7, .L1361
	mvn	r3, #83
	mvn	r1, #82
	mvn	r2, #81
	.pad #44
	sub	sp, sp, #44
	mov	r4, r0
	strb	r3, [r7, #1214]
	mvn	r3, #80
	strb	r0, [r7, #1210]
	strb	r1, [r7, #1215]
	strb	r2, [r7, #1216]
	strb	r3, [r7, #1217]
	bne	.L1253
	mvn	r3, #88
	strb	r3, [r7, #1214]
	ldr	r3, .L1361+4
	mvn	r2, #8
	mov	r5, #7
	strb	r2, [r3, #3309]
	b	.L1314
.L1253:
	cmp	r0, #3
	bne	.L1255
	mvn	r3, #79
	strb	r3, [r7, #1214]
	mvn	r3, #78
	strb	r3, [r7, #1215]
	mvn	r3, #77
	strb	r3, [r7, #1216]
	mvn	r3, #76
	strb	r3, [r7, #1217]
	mvn	r3, #75
	strb	r3, [r7, #1218]
	mvn	r3, #74
	strb	r3, [r7, #1219]
	mvn	r3, #73
	strb	r3, [r7, #1220]
	mvn	r3, #72
	b	.L1355
.L1255:
	cmp	r0, #4
	bne	.L1256
	mvn	r0, #51
	strb	r1, [r7, #1219]
	strb	r0, [r7, #1214]
	mvn	r0, #64
	strb	r2, [r7, #1220]
	strb	r0, [r7, #1215]
	mvn	r0, #85
	strb	r0, [r7, #1216]
	mvn	r0, #84
	strb	r0, [r7, #1217]
	mvn	r0, #50
	strb	r0, [r7, #1218]
.L1355:
	mov	r5, #8
	strb	r3, [r7, #1221]
	mov	r6, r5
	b	.L1254
.L1256:
	cmp	r0, #5
	bne	.L1257
	mov	r3, #56
	mov	r5, #8
	strb	r3, [r7, #1214]
	mov	r3, #57
	strb	r3, [r7, #1215]
	mov	r3, #58
	strb	r3, [r7, #1216]
	mov	r3, #59
	strb	r3, [r7, #1217]
	b	.L1314
.L1257:
	cmp	r0, #6
	bne	.L1258
	mov	r3, #14
	mov	r5, #12
	strb	r3, [r7, #1214]
	mov	r3, #15
	strb	r3, [r7, #1215]
	mov	r3, #16
	strb	r3, [r7, #1216]
	mov	r3, #17
	strb	r3, [r7, #1217]
	b	.L1314
.L1258:
	cmp	r0, #7
	bne	.L1259
	mvn	r3, #79
	mov	r5, #12
	strb	r3, [r7, #1214]
	mov	r6, #10
	mvn	r3, #78
	strb	r3, [r7, #1215]
	mvn	r3, #77
	strb	r3, [r7, #1216]
	mvn	r3, #76
	strb	r3, [r7, #1217]
	mvn	r3, #75
	strb	r3, [r7, #1218]
	mvn	r3, #74
	strb	r3, [r7, #1219]
	mvn	r3, #73
	strb	r3, [r7, #1220]
	mvn	r3, #72
	strb	r3, [r7, #1221]
	mvn	r3, #43
	strb	r3, [r7, #1222]
	mvn	r3, #42
	strb	r3, [r7, #1223]
	b	.L1254
.L1259:
	cmp	r0, #8
	mov	r5, #7
	bne	.L1314
	mov	r3, #6
	strb	r5, [r7, #1215]
	strb	r3, [r7, #1214]
	mov	r5, #50
	mov	r3, #9
	strb	r0, [r7, #1216]
	strb	r3, [r7, #1217]
	mov	r6, #5
	mov	r3, #10
	strb	r3, [r7, #1218]
	b	.L1254
.L1314:
	mov	r6, #4
.L1254:
	sub	r3, r4, #1
	cmp	r3, #1
	bhi	.L1351
	ldr	r10, .L1361+8
	mov	r9, #0
.L1260:
	ldrb	r1, [r7, #2229]	@ zero_extendqisi2
	uxtb	r2, r9
	ldr	r3, .L1361
	cmp	r1, r2
	bls	.L1267
	add	r2, r3, r2
	ldr	r4, .L1361+12
	ldr	r1, .L1361+16
	mov	ip, #55
	ldrb	r2, [r2, #2232]	@ zero_extendqisi2
	add	r3, r3, r2, asl #3
	add	r4, r4, r2, asl #6
	add	r4, r4, #20
	ldrb	r8, [r3, #16]	@ zero_extendqisi2
	ldr	r0, [r3, #12]
	mov	fp, r4
	mov	r2, r4
	add	r8, r0, r8, asl #8
.L1262:
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
	bcc	.L1262
	mov	r2, #0
.L1263:
	add	r0, r10, r2
	mov	r3, #1
.L1264:
	ldrb	r1, [r0, r3, asl #2]	@ zero_extendqisi2
	ldrb	ip, [fp]	@ zero_extendqisi2
	add	r1, r1, ip
	strb	r1, [fp, r3, asl #3]
	add	r3, r3, #1
	cmp	r3, #7
	bne	.L1264
	add	r2, r2, #1
	add	fp, fp, #1
	cmp	r2, #4
	bne	.L1263
	mov	r3, #0
	add	r9, r9, #1
	strb	r3, [r4, #16]
	strb	r3, [r4, #24]
	strb	r3, [r4, #32]
	strb	r3, [r4, #40]
	strb	r3, [r4, #48]
	strb	r3, [r4, #41]
	strb	r3, [r4, #49]
	b	.L1260
.L1351:
	sub	r3, r4, #3
	cmp	r3, #5
	bhi	.L1267
	smulbb	r2, r6, r5
	mov	r3, r2, asl #4
	mov	r2, r2, asr #1
	str	r3, [sp, #36]
	mov	r3, r2, asl #1
	str	r3, [sp, #8]
	mov	r3, #0
.L1359:
	str	r3, [sp, #16]
	ldrb	r3, [sp, #16]	@ zero_extendqisi2
	str	r3, [sp, #12]
	ldr	r3, .L1361
	ldr	r2, [sp, #12]
	ldrb	r3, [r3, #2229]	@ zero_extendqisi2
	cmp	r3, r2
	bhi	.L1312
.L1267:
	ldr	r3, .L1361
	strb	r6, [r3, #1211]
	strb	r5, [r3, #1212]
	add	sp, sp, #44
	@ sp needed
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L1312:
	ldr	r2, [sp, #12]
	ldr	r3, .L1361
	add	r3, r3, r2
	ldrb	r10, [r3, #2232]	@ zero_extendqisi2
	ldr	r3, .L1361
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
	ldreq	r3, .L1361+12
	moveq	r7, #160
	mlaeq	r7, r7, r10, r3
	addeq	r3, r7, #28
	beq	.L1356
.L1269:
	cmp	r4, #8
	beq	.L1271
	ldr	r3, .L1361+12
	add	r7, r3, r10, asl #6
	add	r3, r7, #20
.L1356:
	str	r3, [sp, #20]
	cmp	r4, #4
	add	r3, r8, fp, asl #8
	mov	r2, #54
	str	r2, [r3, #2056]
	bne	.L1272
	add	r9, r8, r9
	mov	r3, #255
	str	r3, [r9, #2052]
	mov	r3, #64
	str	r3, [r9, #2048]
	mov	r3, #204
	b	.L1357
.L1272:
	sub	r2, r4, #5
	cmp	r2, #1
	bhi	.L1274
	ldr	r2, .L1361
	ldrb	r2, [r2, #1214]	@ zero_extendqisi2
	str	r2, [r3, #2052]
	mov	r2, #82
	str	r2, [r3, #2048]
	b	.L1273
.L1274:
	cmp	r4, #7
	bne	.L1273
	add	r9, r8, r9
	mov	r3, #174
	str	r3, [r9, #2052]
	mov	r3, #0
	str	r3, [r9, #2048]
	mov	r3, #176
.L1357:
	str	r3, [r9, #2052]
	mov	r3, #77
	str	r3, [r9, #2048]
.L1273:
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
.L1313:
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
	bne	.L1277
	cmp	r4, #7
	movne	ip, #2
	moveq	ip, #32
.L1277:
	ldr	r3, .L1361+20
	add	r0, r8, fp, asl #8
	ldr	r3, [r3, #1684]
	mov	r1, r3
.L1278:
	ldr	r2, [r0, #2048]
	strb	r2, [r1], #1
	rsb	r2, r3, r1
	uxtb	r2, r2
	cmp	r2, ip
	bcc	.L1278
	ldr	r2, [sp, #4]
	cmp	r2, #0
	beq	.L1279
	mov	r2, #0
.L1281:
	ldrb	ip, [r3, r2, asl #2]	@ zero_extendqisi2
	uxtb	r1, r2
	cmp	ip, #50
	beq	.L1280
	add	ip, r3, r2, asl #2
	ldrb	ip, [ip, #1]	@ zero_extendqisi2
	cmp	ip, #5
	beq	.L1280
	add	r2, r2, #1
	cmp	r2, #8
	bne	.L1281
	b	.L1282
.L1280:
	cmp	r1, #6
	bls	.L1283
.L1282:
	ldr	r0, .L1361+24
	mov	r1, #0
	bl	printk
.L1284:
	b	.L1284
.L1279:
	cmp	r4, #7
	bne	.L1285
	ldr	r2, [sp, #4]
.L1287:
	ldrb	ip, [r3, r2, asl #2]	@ zero_extendqisi2
	uxtb	r1, r2
	cmp	ip, #12
	beq	.L1286
	add	ip, r3, r2, asl #2
	ldrb	ip, [ip, #1]	@ zero_extendqisi2
	cmp	ip, #10
	beq	.L1286
	add	r2, r2, #1
	cmp	r2, #8
	bne	.L1287
	b	.L1288
.L1286:
	cmp	r1, #7
	bne	.L1283
.L1288:
	ldr	r0, .L1361+24
	mov	r1, #0
	bl	printk
.L1289:
	b	.L1289
.L1285:
	cmp	r4, #6
	bne	.L1283
	sub	r2, r3, #1
	add	r3, r3, #7
.L1290:
	ldrb	r1, [r2, #1]!	@ zero_extendqisi2
	cmp	r1, #12
	beq	.L1283
	ldrb	r1, [r2, #8]	@ zero_extendqisi2
	cmp	r1, #4
	beq	.L1283
	cmp	r2, r3
	bne	.L1290
	ldr	r0, .L1361+24
	mov	r1, #0
	bl	printk
.L1292:
	b	.L1292
.L1283:
	ldr	r1, .L1361+20
	ldr	ip, [sp, #36]
	ldr	r2, [r1, #1684]
	add	ip, r2, ip
	mov	r3, r2
.L1293:
	cmp	r3, ip
	ldrne	lr, [r0, #2048]
	strneb	lr, [r3], #1
	bne	.L1293
.L1360:
	ldr	r3, [r1, #1684]
	mov	r0, #8
	ldr	r1, [sp, #8]
	add	r1, r3, r1
	str	r1, [sp, #28]
.L1296:
	ldr	ip, [sp, #8]
	add	lr, r1, ip
.L1295:
	ldrh	ip, [r1]
	mvn	ip, ip
	strh	ip, [r1], #2	@ movhi
	cmp	r1, lr
	bne	.L1295
	ldr	ip, [sp, #8]
	subs	r0, r0, #1
	add	r1, r1, ip
	bne	.L1296
.L1297:
	mov	ip, #0
	mov	r0, ip
.L1300:
	mov	r1, #1
	mov	lr, #0
	mov	r1, r1, asl r0
	mov	r7, #16
	str	r7, [sp, #32]
	mov	r7, lr
.L1298:
	ldrh	r9, [r3, lr]
	and	r9, r9, r1
	cmp	r9, r1
	addeq	r7, r7, #1
	ldr	r9, [sp, #8]
	add	lr, lr, r9
	ldr	r9, [sp, #32]
	subs	r9, r9, #1
	str	r9, [sp, #32]
	bne	.L1298
	cmp	r7, #8
	add	r0, r0, #1
	orrhi	ip, ip, r1
	uxthhi	ip, ip
	cmp	r0, #16
	bne	.L1300
	ldr	r1, [sp, #28]
	strh	ip, [r3], #2	@ movhi
	cmp	r3, r1
	bne	.L1297
	ldr	r3, .L1361+20
	ldr	r1, [r3, #1684]
	mov	r3, #0
	sub	r0, r1, #4
	add	ip, r1, #28
.L1302:
	ldr	lr, [r0, #4]!
	cmp	lr, #0
	addeq	r3, r3, #1
	cmp	r0, ip
	bne	.L1302
	cmp	r3, #7
	ble	.L1303
	ldr	r0, .L1361+28
	mov	r2, #1
	mov	r3, #1024
	bl	rknand_print_hex
	ldr	r0, .L1361+24
	mov	r1, #0
	bl	printk
.L1304:
	b	.L1304
.L1303:
	cmp	r4, #6
	moveq	r0, #4
	beq	.L1305
	cmp	r4, #7
	moveq	r0, #10
	beq	.L1305
	ldr	r3, [sp, #4]
	cmp	r3, #0
	moveq	r0, #8
	movne	r0, #5
.L1305:
	sub	r9, r6, #1
	ldr	r1, [sp, #20]
	mov	ip, #0
	uxtb	r9, r9
	add	r9, r9, #1
.L1306:
	mov	r7, r1
	mov	r3, r2
.L1307:
	ldrb	lr, [r3], #1	@ zero_extendqisi2
	strb	lr, [r7], #1
	rsb	lr, r2, r3
	uxtb	lr, lr
	cmp	lr, r6
	bcc	.L1307
	add	ip, ip, #1
	add	r2, r2, r9
	cmp	ip, r5
	add	r1, r1, r0
	blt	.L1306
	add	r7, r8, fp, asl #8
	mov	r3, #255
	mov	r0, r10
	str	r3, [r7, #2056]
	bl	NandcWaitFlashReady
	ldr	r3, [sp, #24]
	cmp	r3, #0
	beq	.L1309
	mov	r3, #54
	str	r3, [r7, #2056]
	ldr	r3, .L1361
	mvn	r1, #0
	ldr	r0, [sp, #12]
	ldrb	r3, [r3, #1214]	@ zero_extendqisi2
	str	r3, [r7, #2052]
	mov	r3, #0
	str	r3, [r7, #2048]
	mov	r3, #22
	str	r3, [r7, #2056]
	bl	FlashReadCmd
	b	.L1310
.L1309:
	ldr	r3, [sp, #4]
	cmp	r3, #0
	movne	r3, #190
	moveq	r3, #56
	str	r3, [r7, #2056]
.L1310:
	mov	r0, r10
	bl	NandcWaitFlashReady
	ldr	r3, [sp, #16]
	add	r3, r3, #1
	b	.L1359
.L1271:
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
	ldr	r3, .L1361+32
	str	r3, [sp, #20]
	b	.L1313
.L1362:
	.align	2
.L1361:
	.word	.LANCHOR0
	.word	.LANCHOR1
	.word	.LANCHOR1+3292
	.word	.LANCHOR0+1210
	.word	.LANCHOR0+1213
	.word	.LANCHOR2
	.word	.LC85
	.word	.LC86
	.word	.LANCHOR0+1238
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
	bhi	.L1365
	b	HynixGetReadRetryDefault
.L1365:
	cmp	r3, #49
	bne	.L1366
	ldr	r2, .L1377
	ldr	r0, .L1377+4
	ldr	r1, .L1377+8
	strb	r3, [r2, #1210]
	mov	r3, #4
	strb	r3, [r2, #1211]
	mov	r3, #15
	strb	r3, [r2, #1212]
	mov	r2, #64
	b	.L1375
.L1366:
	sub	r2, r3, #65
	cmp	r3, #33
	cmpne	r2, #1
	ldrls	r2, .L1377
	strlsb	r3, [r2, #1210]
	movls	r3, #4
	bls	.L1376
.L1367:
	cmp	r3, #34
	cmpne	r3, #67
	bne	.L1368
	ldr	r2, .L1377
	strb	r3, [r2, #1210]
	mov	r3, #5
.L1376:
	strb	r3, [r2, #1211]
	mov	r3, #7
	ldr	r0, .L1377+4
	strb	r3, [r2, #1212]
	mov	r2, #45
	ldr	r1, .L1377+12
	b	.L1375
.L1368:
	cmp	r3, #35
	cmpne	r3, #68
	bxne	lr
	ldr	r2, .L1377
	ldr	r0, .L1377+4
	ldr	r1, .L1377+16
	strb	r3, [r2, #1210]
	mov	r3, #5
	strb	r3, [r2, #1211]
	mov	r3, #17
	strb	r3, [r2, #1212]
	mov	r2, #95
.L1375:
	b	ftl_memcpy
.L1378:
	.align	2
.L1377:
	.word	.LANCHOR0
	.word	.LANCHOR0+1214
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
	ldr	r5, .L1419
	add	r0, r5, r0, asl #3
	ldr	r3, [r5, #2252]
	ldr	r4, [r0, #12]
	cmp	r3, #3
	bls	.L1410
	ldr	r3, [r4, #16]
	tst	r3, #4
	beq	.L1410
	ldr	r6, [r4, #16]
	ldr	r3, [r4, #8]
	ubfx	r6, r6, #1, #1
	cmp	r6, #0
	str	r3, [sp]
	movne	r6, #0
	beq	.L1392
.L1382:
	ldr	r2, [r4, #28]
	ldr	r3, [sp]
	ubfx	r2, r2, #16, #5
	ubfx	r3, r3, #22, #6
	cmp	r2, r3
	bge	.L1390
	ldr	r3, [r5, #2252]
	cmp	r3, #5
	bhi	.L1383
.L1386:
	add	r6, r6, #1
	bic	r3, r6, #-16777216
	cmp	r3, #0
	bne	.L1385
	ldr	r2, [r4, #28]
	mov	r1, r6
	ldr	r3, [sp]
	ubfx	r2, r2, #16, #5
	ldr	r0, .L1419+4
	ubfx	r3, r3, #22, #6
	bl	printk
	ldr	r0, .L1419+8
	mov	r1, r4
	mov	r2, #4
	mov	r3, #512
	bl	rknand_print_hex
	b	.L1385
.L1383:
	ldr	r3, [r4]
	str	r3, [sp, #4]
	ldr	r3, [sp, #4]
	tst	r3, #8192
	beq	.L1386
	ldr	r3, [sp, #4]
	tst	r3, #131072
	beq	.L1386
.L1390:
	ldr	r3, [r5, #2288]
	ldr	r4, .L1419
	cmp	r3, #0
	beq	.L1391
	ldr	r1, [sp]
	mov	r2, #0
	ldr	r0, [r4, #2280]
	ubfx	r1, r1, #22, #5
	mov	r1, r1, asl #10
	bl	rknand_dma_unmap_single
	ldr	r0, [r4, #2284]
	mov	r2, #0
	ldr	r1, [sp]
	ubfx	r1, r1, #22, #5
	mov	r1, r1, asl #7
	bl	rknand_dma_unmap_single
	b	.L1391
.L1385:
	mov	r0, #5
	mov	r1, #10
	bl	usleep_range
	b	.L1382
.L1394:
	ldr	r3, [r4, #8]
	add	r6, r6, #1
	str	r3, [sp]
	bic	r3, r6, #-16777216
	cmp	r3, #0
	bne	.L1393
	ldr	r2, [sp]
	mov	r1, r6
	ldr	r3, [r4, #28]
	ldr	r0, .L1419+12
	ubfx	r3, r3, #16, #5
	bl	printk
	ldr	r0, .L1419+8
	mov	r1, r4
	mov	r2, #4
	mov	r3, #512
	bl	rknand_print_hex
.L1393:
	mov	r0, #5
	mov	r1, #10
	bl	usleep_range
.L1392:
	ldr	r3, [sp]
	tst	r3, #1048576
	beq	.L1394
	ldr	r3, [r5, #2296]
	cmp	r3, #0
	beq	.L1395
	mov	r0, r4
	bl	NandcSendDumpDataStart
.L1395:
	ldr	r3, [r5, #2288]
	ldr	r6, .L1419
	cmp	r3, #0
	beq	.L1396
	ldr	r1, [sp]
	mov	r2, #1
	ldr	r0, [r6, #2280]
	ubfx	r1, r1, #22, #5
	mov	r1, r1, asl #10
	bl	rknand_dma_unmap_single
	ldr	r0, [r6, #2284]
	mov	r2, #1
	ldr	r1, [sp]
	ubfx	r1, r1, #22, #5
	mov	r1, r1, asl #7
	bl	rknand_dma_unmap_single
.L1396:
	ldr	r3, [r5, #2296]
	cmp	r3, #0
	beq	.L1391
	mov	r0, r4
	bl	NandcSendDumpDataDone
.L1391:
	mov	r3, #0
	str	r3, [r5, #2288]
	b	.L1379
.L1410:
	ldr	r3, [r4, #8]
	str	r3, [sp]
	ldr	r3, [sp]
	tst	r3, #1048576
	beq	.L1410
.L1379:
	add	sp, sp, #8
	@ sp needed
	ldmfd	sp!, {r4, r5, r6, pc}
.L1420:
	.align	2
.L1419:
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
	mov	r10, r3
	ldr	r6, .L1469
	tst	r10, #63
	.pad #92
	sub	sp, sp, #92
	mov	r9, r0
	add	r3, r6, r0, asl #3
	mov	r5, r1
	mov	r7, r2
	ldr	r8, [r3, #12]
	bne	.L1422
	ldr	r3, [sp, #128]
	cmp	r3, #0
	bne	.L1423
	add	r0, sp, #24
	mov	r1, #255
	mov	r2, #64
	bl	ftl_memset
	add	r3, sp, #24
	str	r3, [sp, #128]
.L1423:
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
	bne	.L1424
	ldr	r3, [r6, #2300]
	mov	r0, r7, lsr #1
	mov	r2, r5
	mov	r1, r5
	cmp	r3, #25
	ldr	r3, [sp, #128]
	movcc	lr, #64
	movcs	lr, #128
.L1426:
	cmp	r1, r0
	add	r3, r3, #4
	add	ip, r2, lr
	bcs	.L1467
	ldr	r4, [r6, #2268]
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
	b	.L1426
.L1467:
	ldr	r0, [r6, #2300]
	mov	r2, #0
	ldr	r1, [r6, #2252]
	mov	r7, r7, lsr #2
	mov	fp, r2
.L1428:
	cmp	r2, r7
	bcs	.L1424
	cmp	r0, #0
	beq	.L1424
	add	r3, r2, #8
	ldr	r3, [r8, r3, asl #2]
	str	r3, [sp, #20]
	ldr	r3, [sp, #20]
	tst	r3, #4
	bne	.L1452
	ldr	r4, [sp, #20]
	ubfx	r4, r4, #15, #1
	cmp	r4, #0
	bne	.L1452
	cmp	r1, #5
	bls	.L1430
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
	b	.L1466
.L1430:
	cmp	r1, #3
	bls	.L1432
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
.L1466:
	orr	r4, r3, r4, asl #5
.L1432:
	cmp	fp, r4
	movcc	fp, r4
	b	.L1429
.L1452:
	mvn	fp, #0
.L1429:
	add	r2, r2, #1
	b	.L1428
.L1424:
	mov	r3, #0
	str	r3, [r8, #16]
	b	.L1435
.L1422:
	cmp	r1, #1
	mov	r4, #0
	bne	.L1464
	mov	fp, r4
.L1436:
	cmp	r4, r7
	bcs	.L1468
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
	b	.L1436
.L1468:
	mov	fp, #0
	b	.L1435
.L1464:
	str	r4, [sp]
	mov	r1, r4
	str	r4, [sp, #4]
	mov	r2, #2
	mov	r3, r4
	mov	fp, r4
	bl	NandcXferStart
	str	r10, [sp, #8]
.L1441:
	cmp	r4, r7
	bcs	.L1435
	mov	r0, r9
	mov	r1, r5
	bl	NandcXferComp
	ldr	r3, [r8, #32]
	add	ip, r4, #2
	cmp	ip, r7
	str	r3, [sp, #20]
	bcs	.L1442
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
.L1442:
	ldr	r3, [sp, #20]
	tst	r3, #4
	mvnne	fp, #0
	bne	.L1443
	ldr	r2, [sp, #20]
	ldr	r3, [sp, #20]
	ubfx	r2, r2, #3, #5
	ubfx	r3, r3, #27, #1
	orr	r3, r2, r3, asl #5
	cmp	fp, r3
	movcc	fp, r3
.L1443:
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
	b	.L1441
.L1435:
	ldr	r3, [r6, #2252]
	clz	r5, r5
	cmp	r3, #5
	mov	r5, r5, lsr #5
	movls	r5, #0
	cmp	r5, #0
	beq	.L1447
	ldr	r3, [r8]
	and	r2, r3, #139264
	cmp	r2, #139264
	orreq	r3, r3, #131072
	streq	r3, [r8]
	mvneq	fp, #0
.L1447:
	mov	r0, fp
	add	sp, sp, #92
	@ sp needed
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L1470:
	.align	2
.L1469:
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
	ldr	r3, .L1474
	subs	r4, r0, #0
	mov	r6, r1
	mov	r7, r2
	ldrb	r5, [r3, #481]	@ zero_extendqisi2
	bne	.L1472
	ldr	r2, .L1474+4
	ldrb	r3, [r2, #1]	@ zero_extendqisi2
	ldr	r2, [r2, #4]
	mul	r2, r2, r3
	cmp	r1, r2
	movcc	r5, #4
.L1472:
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
.L1475:
	.align	2
.L1474:
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
	ldr	r4, .L1503
	.pad #20
	sub	sp, sp, #20
	mov	r10, r0
	mov	fp, r2
	str	r1, [sp]
	ldr	r3, [r4, #76]
	ldr	r3, [r3, #304]
	str	r3, [sp, #8]
	ldr	r3, [r4, #2252]
	cmp	r3, #8
	ldr	r3, [sp, #56]
	movcc	r9, #6
	movcs	r9, #12
	cmp	r3, #0
	moveq	r6, #1024
	beq	.L1478
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
	ldrb	r0, [r4, #2228]	@ zero_extendqisi2
	bl	FlashSetInterfaceMode
	ldrb	r0, [r4, #2228]	@ zero_extendqisi2
	bl	NandcSetMode
	cmn	r6, #1
	bne	.L1479
.L1488:
	mvn	r6, #0
	b	.L1480
.L1479:
	mov	r2, r6
	ldr	r0, .L1503+4
	ldr	r1, [sp]
	bl	printk
	cmp	r6, #9
	addls	r4, r4, r10, asl #3
	ldrls	r3, [r4, #12]
	ldr	r4, .L1503+8
	ldrls	r2, [r3, #3840]
	ldrls	r2, [r3]
	orrls	r2, r2, #131072
	strls	r2, [r3]
	ldr	r3, [r4, #1688]
	add	r3, r3, #1
	str	r3, [r4, #1688]
	cmp	r3, #2048
	movcs	r7, #0
	strcs	r7, [r4, #1688]
	movcs	fp, r7
	bcc	.L1480
.L1478:
	mov	r4, #0
	mvn	r8, #0
	mov	ip, r4
	mov	r5, r4
	str	r4, [sp, #4]
.L1486:
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
	bhi	.L1482
	cmp	r0, #2
	bhi	.L1492
	add	r5, r5, #1
	cmp	r5, #9
	bls	.L1492
	rsb	r4, r5, r9
	mov	r6, r0
	mov	r8, #0
	b	.L1484
.L1482:
	ldr	r3, [sp, #4]
	cmp	r3, r5
	bcs	.L1493
	cmp	r5, #7
	rsb	ip, r5, r4
	bhi	.L1494
	str	r5, [sp, #4]
	b	.L1493
.L1492:
	mov	r8, #0
	mov	r4, r9
	mov	r6, r0
	mov	r7, r8
	mov	fp, r8
	b	.L1483
.L1493:
	mov	r5, #0
.L1483:
	add	r9, r9, #2
	cmp	r9, #69
	bls	.L1486
.L1484:
	ldr	r3, [sp, #4]
	cmp	r3, r5
	movcs	r4, ip
	b	.L1485
.L1494:
	mov	r4, ip
.L1485:
	cmp	r4, #0
	beq	.L1487
	ldr	r0, .L1503+12
	mov	r1, r4
	bl	printk
	uxtb	r0, r4
	bl	NandcSetDdrPara
.L1487:
	cmn	r8, #1
	bne	.L1480
	ldr	r0, .L1503+16
	mov	r1, r10
	ldr	r2, [sp]
	bl	printk
	ldr	r3, [sp, #56]
	cmp	r3, #0
	beq	.L1488
	ldr	r3, [sp, #8]
	ubfx	r0, r3, #8, #8
	bl	NandcSetDdrPara
.L1480:
	mov	r0, r6
	add	sp, sp, #20
	@ sp needed
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L1504:
	.align	2
.L1503:
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
	bne	.L1506
	ldr	r9, .L1526
	ldrb	fp, [r9, #8]	@ zero_extendqisi2
	mov	r10, r9
	cmp	fp, #0
	bne	.L1507
.L1509:
	ldrb	r3, [r10, #2244]	@ zero_extendqisi2
	ldr	r9, .L1526
	cmp	r3, #0
	beq	.L1506
	b	.L1525
.L1507:
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
	beq	.L1509
	b	.L1506
.L1525:
	ldr	r3, [r9, #76]
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
	beq	.L1510
	ldrb	r3, [r9, #2304]	@ zero_extendqisi2
	cmp	r0, r3, lsr #1
	bls	.L1506
.L1510:
	ubfx	r0, r10, #8, #8
	bl	NandcSetDdrPara
.L1506:
	ldr	r9, .L1526+4
	ldr	ip, [r9, #1692]
	adds	r3, ip, #0
	movne	r3, #1
	cmn	r4, #1
	movne	r3, #0
	cmp	r3, #0
	beq	.L1511
	mov	r1, r6
	mov	r2, r8
	mov	r3, r7
	mov	r0, r5
	blx	ip
	mov	r2, r5
	mov	r3, r6
	mov	r4, r0
	ldr	r0, .L1526+8
	mov	r1, r4
	bl	printk
	cmn	r4, #1
	bne	.L1511
	ldr	r3, .L1526
	ldrb	r3, [r3, #140]	@ zero_extendqisi2
	cmp	r3, #0
	beq	.L1511
	mov	r0, r5
	bl	flash_enter_slc_mode
	ldr	ip, [r9, #1692]
	mov	r0, r5
	mov	r1, r6
	mov	r2, r8
	mov	r3, r7
	blx	ip
	mov	r4, r0
	mov	r0, r5
	bl	flash_exit_slc_mode
.L1511:
	mov	r0, r4
	add	sp, sp, #12
	@ sp needed
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L1527:
	.align	2
.L1526:
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
	ldr	r5, .L1538
	mov	r6, r1
	mov	r4, #0
	ldrb	r0, [r5, #2228]	@ zero_extendqisi2
	bl	FlashSetInterfaceMode
	ldrb	r0, [r5, #2228]	@ zero_extendqisi2
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
	bne	.L1529
	ldrb	r2, [r5, #2228]	@ zero_extendqisi2
	tst	r2, #1
	beq	.L1529
	mov	r0, #1
	bl	FlashSetInterfaceMode
	mov	r0, #1
	bl	NandcSetMode
	strb	r4, [r5, #2244]
	b	.L1530
.L1529:
	mov	r2, #1
	strb	r2, [r3, #2244]
.L1530:
	mov	r0, #0
	add	sp, sp, #8
	@ sp needed
	ldmfd	sp!, {r4, r5, r6, r7, r8, pc}
.L1539:
	.align	2
.L1538:
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
	ldr	r3, .L1556
	mov	r4, #0
	ldr	r7, .L1556+4
	mov	r9, #4
	ldr	r5, .L1556+8
	mvn	r8, #0
	ldr	r0, [r3]	@ unaligned
	ldr	r3, [r7, #4]
	str	r4, [r5, #1700]
	mov	r6, r5
	str	r0, [sp, #12]	@ unaligned
	mov	r0, r4
	str	r3, [sp, #4]
	ldr	r3, [r5, #1684]
	str	r3, [r5, #1696]
	bl	flash_enter_slc_mode
.L1541:
	add	fp, r4, #1
	mov	r10, #0
.L1543:
	add	r3, sp, #12
	ldrb	r0, [r3, r10]	@ zero_extendqisi2
	bl	FlashBchSel
	mov	r0, #0
	mov	r1, r4
	ldr	r2, [r5, #1684]
	mov	r3, r0
	bl	FlashReadRawPage
	cmn	r0, #1
	bne	.L1542
	mov	r0, #0
	mov	r1, fp
	ldr	r2, [r6, #1684]
	mov	r3, r0
	bl	FlashReadRawPage
	cmn	r0, #1
	bne	.L1542
	add	r10, r10, #1
	cmp	r10, #4
	beq	.L1544
	b	.L1543
.L1545:
	add	r0, fp, #12
	movw	r1, #2036
	bl	js_hash
	ldr	r3, [fp, #8]
	cmp	r3, r0
	mvnne	r8, #0
	bne	.L1544
	ldr	r8, .L1556+12
	add	r1, fp, #160
	mov	r2, #32
	mov	r0, r8
	bl	ftl_memcpy
	ldr	r1, [r6, #1696]
	mov	r2, #32
	ldr	r0, .L1556+16
	add	r1, r1, #192
	bl	ftl_memcpy
	ldr	r1, [r6, #1696]
	mov	r2, #852
	ldr	r0, .L1556+20
	add	r1, r1, #224
	bl	ftl_memcpy
	ldrh	r0, [r8, #10]
	bl	FlashBlockAlignInit
	ldr	r8, [r6, #1696]
	str	r4, [r6, #1700]
	mov	r0, r4
	ldr	r1, [r7, #4]
	ldr	r3, [r8, #1076]
	strb	r3, [r7, #2244]
	bl	__aeabi_uidiv
	add	r0, r0, #1
	cmp	r0, #1
	strhi	r0, [r6, #1704]
	movls	r3, #2
	strls	r3, [r6, #1704]
	ldrh	r3, [r8, #14]
	mov	r8, #0
	strb	r3, [r5, #1708]
.L1544:
	ldr	r3, [sp, #4]
	subs	r9, r9, #1
	add	r4, r4, r3
	bne	.L1541
	mov	r0, r9
.L1555:
	bl	flash_exit_slc_mode
	mov	r0, r8
	add	sp, sp, #20
	@ sp needed
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L1542:
	ldr	fp, [r5, #1696]
	ldr	r2, .L1556+24
	ldr	r3, [fp]
	cmp	r3, r2
	bne	.L1544
	cmp	r8, #0
	bne	.L1545
	mov	r0, r4
	ldr	r1, [r7, #4]
	bl	__aeabi_uidiv
	ldr	r3, .L1556+8
	add	r0, r0, #1
	str	r0, [r3, #1704]
	mov	r0, r8
	b	.L1555
.L1557:
	.align	2
.L1556:
	.word	.LANCHOR3+11
	.word	.LANCHOR0
	.word	.LANCHOR2
	.word	.LANCHOR1+472
	.word	.LANCHOR0+44
	.word	.LANCHOR0+1210
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
	mov	r9, r0
	mov	fp, r3
	str	r2, [sp, #8]
	str	r1, [sp, #16]
	bl	NandcWaitFlashReady
	ldr	r4, .L1587
	add	r3, r4, r9, asl #3
	ldrb	r2, [r3, #16]	@ zero_extendqisi2
	ldr	r6, [r3, #12]
	ldrb	r3, [r4, #1208]	@ zero_extendqisi2
	add	r7, r2, #8
	sub	r3, r3, #67
	add	r7, r6, r7, asl #8
	cmp	r3, #1
	mov	r3, r2, asl #8
	str	r3, [sp, #12]
	movls	r3, #0
	strls	r3, [sp, #4]
	bls	.L1559
	ldrb	r5, [r4, #2244]	@ zero_extendqisi2
	cmp	r5, #0
	streq	r5, [sp, #4]
	beq	.L1560
	mov	r0, #0
	str	r2, [sp, #20]
	bl	NandcSetDdrMode
	mov	r3, #1
	ldr	r2, [sp, #20]
	str	r3, [sp, #4]
.L1560:
	ldr	r3, [sp, #12]
	mov	r1, #92
	add	r3, r6, r3
	str	r1, [r3, #2056]
	mov	r1, #197
	str	r1, [r3, #2056]
.L1559:
	mov	r8, #1
	mvn	r10, #0
	mov	r3, r2, asl #8
	str	r3, [sp, #20]
.L1561:
	ldr	r3, .L1587+4
	ldrb	r3, [r3, #1709]	@ zero_extendqisi2
	add	r3, r3, #1
	cmp	r8, r3
	bcs	.L1586
	ldrb	r3, [r4, #1208]	@ zero_extendqisi2
	mov	r0, r7
	uxtb	r1, r8
	sub	r3, r3, #67
	cmp	r3, #1
	bhi	.L1562
	bl	SandiskSetRRPara
	b	.L1563
.L1562:
	bl	ToshibaSetRRPara
.L1563:
	ldrb	r3, [r4, #1208]	@ zero_extendqisi2
	cmp	r3, #34
	bne	.L1564
	ldr	r3, .L1587+4
	ldrb	r3, [r3, #1709]	@ zero_extendqisi2
	sub	r3, r3, #3
	cmp	r8, r3
	ldreq	r3, [sp, #20]
	moveq	r2, #179
	addeq	r3, r6, r3
	streq	r2, [r3, #2056]
.L1564:
	ldr	r3, [sp, #12]
	mov	r2, #38
	add	r3, r6, r3
	str	r2, [r3, #2056]
	mov	r2, #93
	str	r2, [r3, #2056]
	ldr	r3, [sp, #4]
	cmp	r3, #0
	beq	.L1565
	mov	r0, #4
	bl	NandcSetDdrMode
	mov	r0, r9
	mov	r3, fp
	ldr	r1, [sp, #16]
	ldr	r2, [sp, #8]
	bl	FlashReadRawPage
	mov	r5, r0
	mov	r0, #0
	bl	NandcSetDdrMode
	b	.L1566
.L1565:
	mov	r0, r9
	ldr	r1, [sp, #16]
	ldr	r2, [sp, #8]
	mov	r3, fp
	bl	FlashReadRawPage
	mov	r5, r0
.L1566:
	cmn	r5, #1
	beq	.L1567
	ldrb	r2, [r4, #2304]	@ zero_extendqisi2
	cmn	r10, #1
	moveq	r10, r5
	add	r2, r2, r2, asl #1
	cmp	r5, r2, asr #2
	bcc	.L1569
	mov	fp, #0
	str	fp, [sp, #8]
.L1567:
	add	r8, r8, #1
	b	.L1561
.L1586:
	mov	r5, r10
.L1569:
	ldrb	r2, [r4, #1208]	@ zero_extendqisi2
	mov	r0, r7
	mov	r1, #0
	sub	r2, r2, #67
	cmp	r2, #1
	bhi	.L1571
	bl	SandiskSetRRPara
	b	.L1572
.L1571:
	bl	ToshibaSetRRPara
.L1572:
	ldr	r3, [sp, #12]
	mov	r2, #255
	add	r6, r6, r3
	str	r2, [r6, #2056]
	ldrb	r2, [r4, #2304]	@ zero_extendqisi2
	add	r2, r2, r2, asl #1
	cmp	r5, r2, asr #2
	bcc	.L1573
	cmn	r5, #1
	movne	r5, #256
.L1573:
	mov	r0, r9
	bl	NandcWaitFlashReady
	ldr	r3, [sp, #4]
	cmp	r3, #0
	beq	.L1574
	mov	r0, #4
	bl	NandcSetDdrMode
.L1574:
	mov	r0, r5
	add	sp, sp, #28
	@ sp needed
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L1588:
	.align	2
.L1587:
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
	mov	r9, r0
	ldr	r5, .L1603
	mov	r10, r2
	mov	r8, r3
	mov	fp, r1
	bl	NandcWaitFlashReady
	add	r2, r5, r9, asl #3
	ldr	ip, .L1603+4
	mov	r7, #1
	ldrb	r3, [r2, #16]	@ zero_extendqisi2
	mvn	r4, #0
	ldr	r6, [r2, #12]
	add	r3, r3, #8
	add	r6, r6, r3, asl #8
.L1590:
	ldrb	r3, [ip, #1709]	@ zero_extendqisi2
	add	r3, r3, #1
	cmp	r7, r3
	bcs	.L1593
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
	beq	.L1591
	ldrb	r3, [r5, #2304]	@ zero_extendqisi2
	cmn	r4, #1
	moveq	r4, r0
	add	r3, r3, r3, asl #1
	cmp	r0, r3, asr #2
	bcc	.L1596
	mov	r8, #0
	mov	r10, r8
.L1591:
	add	r7, r7, #1
	b	.L1590
.L1596:
	mov	r4, r0
.L1593:
	mov	r0, r6
	mov	r1, #0
	bl	SamsungSetRRPara
	ldrb	r3, [r5, #2304]	@ zero_extendqisi2
	add	r3, r3, r3, asl #1
	cmp	r4, r3, asr #2
	bcc	.L1595
	cmn	r4, #1
	movne	r4, #256
.L1595:
	mov	r0, r4
	add	sp, sp, #12
	@ sp needed
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L1604:
	.align	2
.L1603:
	.word	.LANCHOR0
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
.L1607:
	stmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.save {r4, r5, r6, r7, r8, r9, r10, fp, lr}
	mov	r8, r3
	ldr	r3, .L1631
	mov	fp, r2
	.pad #36
	sub	sp, sp, #36
	mov	r6, r0
	mov	r10, #0
	ldrb	r5, [r3, #2304]	@ zero_extendqisi2
	ldrb	r3, [r3, #140]	@ zero_extendqisi2
	str	r1, [sp, #20]
	cmp	r3, #0
	addeq	r5, r5, r5, asl #1
	ldrne	r2, .L1631+4
	ubfxeq	r5, r5, #2, #8
	smullne	r2, r3, r5, r2
	uxtbne	r5, r3
	ldr	r3, .L1631
	add	r3, r3, r0, asl #3
	str	r3, [sp, #24]
.L1617:
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
.L1608:
	ldr	r3, .L1631+8
	ldrb	r3, [r3, #1709]	@ zero_extendqisi2
	cmp	r9, r3
	bcs	.L1611
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
	beq	.L1609
	cmn	r4, #1
	ldr	ip, [sp, #28]
	moveq	r4, r0
	cmp	r0, r5
	bcc	.L1619
	mov	r8, ip
	mov	fp, ip
.L1609:
	ldr	r9, [sp, #8]
	b	.L1608
.L1619:
	mov	r4, r0
	mov	r8, ip
	mov	fp, ip
.L1611:
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
	bcc	.L1613
	cmn	r4, #1
	movne	r4, #256
.L1613:
	cmn	r4, #1
	movne	r7, #0
	moveq	r7, #1
	cmp	r4, #256
	movne	r1, r7
	orreq	r1, r7, #1
	cmp	r1, #0
	beq	.L1614
	str	r4, [sp]
	mov	r1, r9
	ldr	r0, .L1631+12
	mov	r3, r9
	ldr	r2, [sp, #20]
	bl	printk
	cmp	r10, #0
	bne	.L1615
	ldr	r3, .L1631
	ldrb	r3, [r3, #140]	@ zero_extendqisi2
	cmp	r3, #0
	moveq	r7, #0
	andne	r7, r7, #1
	cmp	r7, #0
	beq	.L1624
	mov	r0, r6
	mov	r1, #3
	bl	micron_auto_read_calibration_config
	mov	r10, #1
	b	.L1617
.L1615:
	mov	r0, r6
	mov	r1, #0
	bl	micron_auto_read_calibration_config
	cmn	r4, #1
	movne	r4, #256
	b	.L1624
.L1614:
	cmp	r10, #0
	beq	.L1624
	mov	r0, r6
	mov	r4, #256
	bl	micron_auto_read_calibration_config
.L1624:
	mov	r0, r4
	add	sp, sp, #36
	@ sp needed
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L1632:
	.align	2
.L1631:
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
	ldr	r5, .L1651
	mov	r10, r2
	mov	r7, r0
	mov	fp, r1
	add	r2, r5, r0
	mvn	r6, #0
	ldr	r3, [r5, #1204]
	ldrb	r4, [r2, #1222]	@ zero_extendqisi2
	ldrb	r9, [r5, #1212]	@ zero_extendqisi2
	ldrb	r3, [r3, #19]	@ zero_extendqisi2
	sub	r3, r3, #7
	cmp	r3, #1
	ldrlsb	r4, [r2, #1230]	@ zero_extendqisi2
	bl	NandcWaitFlashReady
	mov	ip, #0
.L1635:
	cmp	ip, r9
	bcs	.L1639
	add	r4, r4, #1
	mov	r0, r7
	ldrb	r1, [r5, #1211]	@ zero_extendqisi2
	uxtb	r4, r4
	ldr	r2, .L1651+4
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
	beq	.L1637
	ldrb	r3, [r5, #2304]	@ zero_extendqisi2
	cmn	r6, #1
	moveq	r6, r0
	add	r3, r3, r3, asl #1
	cmp	r0, r3, asr #2
	bcc	.L1644
	mov	r8, #0
	mov	r10, r8
.L1637:
	add	ip, ip, #1
	b	.L1635
.L1644:
	mov	r6, r0
.L1639:
	ldr	r3, [r5, #1204]
	add	r7, r5, r7
	ldrb	r3, [r3, #19]	@ zero_extendqisi2
	sub	r3, r3, #7
	cmp	r3, #1
	ldrb	r3, [r5, #2304]	@ zero_extendqisi2
	strlsb	r4, [r7, #1230]
	strhib	r4, [r7, #1222]
	add	r3, r3, r3, asl #1
	cmp	r6, r3, asr #2
	bcc	.L1643
	cmn	r6, #1
	movne	r6, #256
.L1643:
	mov	r0, r6
	add	sp, sp, #12
	@ sp needed
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L1652:
	.align	2
.L1651:
	.word	.LANCHOR0
	.word	.LANCHOR0+1214
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
	ldr	r3, .L1657
	subs	r4, r0, #0
	mov	r5, r1
	mov	r7, r2
	ldrb	r6, [r3, #481]	@ zero_extendqisi2
	bne	.L1654
	ldr	r2, .L1657+4
	ldrb	r3, [r2, #1]	@ zero_extendqisi2
	ldr	r1, [r2, #4]
	mul	r1, r1, r3
	cmp	r5, r1
	bcs	.L1654
	ldrb	r3, [r2]	@ zero_extendqisi2
	cmp	r3, #0
	movne	r6, #4
.L1654:
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
.L1658:
	.align	2
.L1657:
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
	ldr	r4, .L1669
	ldr	r5, .L1669+4
	ldr	r8, .L1669+8
	ldr	r3, [r4, #1684]
	mov	r9, r4
	ldrb	r0, [r4, #1710]	@ zero_extendqisi2
	mov	r10, r5
	str	r3, [r4, #1696]
	bl	FlashBchSel
	mov	r1, #0
	mov	r2, #2048
	ldr	r0, [r4, #1684]
	bl	ftl_memset
	ldr	r3, [r4, #1696]
	ldr	r1, .L1669+12
	mov	r2, #32
	str	r8, [r3]
	ldr	r0, [r4, #1696]
	ldrb	r3, [r5, #2229]	@ zero_extendqisi2
	add	r0, r0, #16
	strh	r3, [r0, #-4]	@ movhi
	ldrb	r3, [r5, #1]	@ zero_extendqisi2
	strh	r3, [r0, #-2]	@ movhi
	ldrb	r3, [r5, #2244]	@ zero_extendqisi2
	str	r3, [r0, #1060]
	bl	ftl_memcpy
	ldr	r0, [r4, #1696]
	ldr	r1, .L1669+16
	mov	r2, #8
	add	r0, r0, #80
	bl	ftl_memcpy
	ldr	r0, [r4, #1696]
	add	r1, r5, #1168
	mov	r2, #32
	add	r0, r0, #96
	bl	ftl_memcpy
	ldr	r0, [r4, #1696]
	ldr	r1, .L1669+20
	mov	r2, #32
	add	r0, r0, #160
	bl	ftl_memcpy
	ldr	r0, [r4, #1696]
	add	r1, r5, #44
	mov	r2, #32
	add	r0, r0, #192
	bl	ftl_memcpy
	ldr	r0, [r4, #1696]
	mov	r2, #852
	ldr	r1, .L1669+24
	add	r0, r0, #224
	bl	ftl_memcpy
	ldr	r6, [r4, #1696]
	movw	r1, #2036
	add	r0, r6, #12
	bl	js_hash
	movw	r3, #1592
	str	r3, [r6, #4]
	ldr	r3, [r4, #1712]
	str	r3, [r4, #1696]
	str	r0, [r6, #8]
	mov	r0, #0
	bl	flash_enter_slc_mode
	mov	r6, #0
	mov	r7, r6
.L1662:
	ldr	r1, [r5, #4]
	mov	r0, #0
	mov	r2, r0
	mul	r1, r1, r7
	bl	FlashEraseBlock
	ldr	r1, [r5, #4]
	mov	r0, #0
	ldr	r2, [r4, #1684]
	mov	r3, r0
	mul	r1, r1, r7
	bl	FlashProgPage
	ldr	r1, [r5, #4]
	mov	r0, #0
	ldr	r2, [r4, #1684]
	mov	r3, r0
	mul	r1, r1, r7
	add	r1, r1, #1
	bl	FlashProgPage
	ldr	r1, [r5, #4]
	mov	r0, #0
	ldr	r2, [r4, #1712]
	mov	r3, r0
	mul	r1, r1, r7
	bl	FlashReadRawPage
	add	r2, r7, #1
	cmn	r0, #1
	beq	.L1660
	ldr	fp, [r9, #1696]
	ldr	r3, [fp]
	cmp	r3, r8
	bne	.L1660
	add	r0, fp, #12
	movw	r1, #2036
	str	r2, [sp, #4]
	bl	js_hash
	ldr	r3, [fp, #8]
	cmp	r3, r0
	ldr	r2, [sp, #4]
	bne	.L1660
	ldr	r3, [r10, #4]
	cmp	r6, #1
	str	r2, [r9, #1704]
	mul	r7, r3, r7
	str	r7, [r9, #1700]
	beq	.L1663
	mov	r6, #1
.L1660:
	cmp	r2, #4
	mov	r7, r2
	bne	.L1662
	b	.L1661
.L1663:
	mov	r6, #2
.L1661:
	mov	r0, #0
	bl	flash_exit_slc_mode
	clz	r0, r6
	mov	r0, r0, lsr #5
	rsb	r0, r0, #0
	add	sp, sp, #12
	@ sp needed
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L1670:
	.align	2
.L1669:
	.word	.LANCHOR2
	.word	.LANCHOR0
	.word	1312902724
	.word	.LANCHOR0+2068
	.word	.LANCHOR0+2232
	.word	.LANCHOR1+472
	.word	.LANCHOR0+1210
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
	ldr	r3, .L1691
	.pad #20
	sub	sp, sp, #20
	ldr	r4, .L1691+4
	ldr	r0, [r3]	@ unaligned
	ldr	r3, [r4, #2240]
	ldrb	r10, [r4, #2304]	@ zero_extendqisi2
	cmp	r3, #0
	str	r0, [sp, #12]	@ unaligned
	beq	.L1672
	mov	r0, #0
	bl	flash_enter_slc_mode
.L1672:
	mov	r0, r9
	mov	r1, #0
	mov	r2, #2048
	mvn	r8, #0
	bl	ftl_memset
	mov	r5, #2
.L1673:
	ldrb	r3, [r4, #1]	@ zero_extendqisi2
	cmp	r5, r3
	bcs	.L1677
	ldr	fp, .L1691+8
	mov	r7, #0
.L1675:
	add	r3, sp, #12
	ldr	r6, .L1691+8
	ldrb	ip, [r7, r3]	@ zero_extendqisi2
	mov	r0, ip
	str	ip, [sp, #4]
	bl	FlashBchSel
	ldr	r1, [r4, #4]
	mov	r0, #0
	ldr	r2, [fp, #1684]
	mov	r3, r0
	mul	r1, r1, r5
	bl	FlashReadRawPage
	cmn	r0, #1
	ldr	ip, [sp, #4]
	bne	.L1674
	add	r7, r7, #1
	cmp	r7, #4
	bne	.L1675
	b	.L1676
.L1674:
	ldr	r3, [r6, #1684]
	ldr	r2, .L1691+12
	ldr	r3, [r3]
	cmp	r3, r2
	bne	.L1676
	mov	r1, ip
	ldr	r0, .L1691+16
	bl	printk
	mov	r2, #2048
	mov	r0, r9
	ldr	r1, [r6, #1684]
	bl	ftl_memcpy
	ldr	r3, [r6, #1684]
	ldr	r2, .L1691+4
	ldr	r3, [r3, #512]
	strb	r3, [r2, #1]
	ldr	r3, [r6, #1704]
	cmp	r3, r5
	bls	.L1680
	str	r5, [r6, #1704]
	bl	FlashSavePhyInfo
	mov	r8, #0
.L1676:
	add	r5, r5, #1
	b	.L1673
.L1680:
	mov	r8, #0
.L1677:
	mov	r0, r10
	bl	FlashBchSel
	ldr	r3, [r4, #2240]
	cmp	r3, #0
	beq	.L1684
	mov	r0, #0
	bl	flash_exit_slc_mode
.L1684:
	mov	r0, r8
	add	sp, sp, #20
	@ sp needed
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L1692:
	.align	2
.L1691:
	.word	.LANCHOR3+11
	.word	.LANCHOR0
	.word	.LANCHOR2
	.word	-52655045
	.word	.LC95
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
	ldr	r8, .L1788
	ldr	r4, .L1788+4
	mov	r7, #0
	add	r9, r4, #12
	str	r0, [r8, #1684]
	mov	r0, #32768
	bl	ftl_malloc
	str	r0, [r8, #1712]
	mov	r0, #4096
	bl	ftl_malloc
	str	r0, [r8, #1716]
	mov	r0, #32768
	bl	ftl_malloc
	str	r0, [r8, #1720]
	mov	r0, #4096
	bl	ftl_malloc
	mov	r3, #50
	str	r7, [r8, #1704]
	strb	r3, [r4, #1]
	strb	r3, [r8, #1708]
	mov	r3, #128
	strb	r7, [r4, #2244]
	str	r3, [r4, #4]
	mov	r3, #60
	str	r7, [r8, #1688]
	strb	r7, [r4]
	strb	r7, [r8, #1728]
	strb	r3, [r8, #1710]
	str	r0, [r8, #1724]
	mov	r0, r5
	bl	NandcInit
	ldr	r5, .L1788+8
.L1699:
	add	r3, r9, r7, asl #3
	uxtb	fp, r7
	ldr	r6, [r9, r7, asl #3]
	ldrb	r10, [r3, #4]	@ zero_extendqisi2
	mov	r0, fp
	bl	FlashReset
	mov	r0, fp
	bl	NandcFlashCs
	mov	r3, #144
	add	r6, r6, r10, asl #8
	mov	r0, #200
	mov	r10, #0
	str	r3, [r6, #2056]
	str	r10, [r6, #2052]
	bl	NandcDelayns
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
	bne	.L1694
	mov	r2, #239
	mov	r0, #200
	str	r2, [r6, #2056]
	mov	r2, #1
	str	r2, [r6, #2052]
	bl	NandcDelayns
	mov	r2, #4
	str	r2, [r6, #2048]
	str	r10, [r6, #2048]
	str	r10, [r6, #2048]
	str	r10, [r6, #2048]
.L1694:
	mov	r0, fp
	bl	NandcFlashDeCs
	ldrb	r2, [r5]	@ zero_extendqisi2
	sub	r3, r2, #1
	uxtb	r3, r3
	cmp	r3, #253
	bhi	.L1695
	ldrb	r1, [r5, #2]	@ zero_extendqisi2
	ldrb	r3, [r5, #1]	@ zero_extendqisi2
	ldr	r0, .L1788+12
	str	r1, [sp]
	ldrb	r1, [r5, #3]	@ zero_extendqisi2
	str	r1, [sp, #4]
	ldrb	r1, [r5, #4]	@ zero_extendqisi2
	str	r1, [sp, #8]
	ldrb	r1, [r5, #5]	@ zero_extendqisi2
	str	r1, [sp, #12]
	add	r1, r7, #1
	bl	printk
.L1695:
	cmp	r7, #0
	bne	.L1696
	ldrb	r3, [r4, #2068]	@ zero_extendqisi2
	sub	r3, r3, #1
	uxtb	r3, r3
	cmp	r3, #253
	bhi	.L1745
	ldr	r3, .L1788+4
	ldrb	r3, [r3, #2069]	@ zero_extendqisi2
	cmp	r3, #255
	beq	.L1745
.L1696:
	ldrb	r3, [r5]	@ zero_extendqisi2
	add	r7, r7, #1
	add	r5, r5, #8
	cmp	r3, #181
	moveq	r3, #44
	streqb	r3, [r5, #-8]
	cmp	r7, #4
	bne	.L1699
	ldrb	r3, [r4, #2068]	@ zero_extendqisi2
	cmp	r3, #173
	beq	.L1700
	ldr	r3, .L1788+4
	ldr	r0, [r3, #2248]
	bl	NandcSetDdrMode
.L1700:
	mov	r2, #852
	ldr	r0, .L1788+16
	mov	r1, #0
	ldr	r5, .L1788+20
	bl	ftl_memset
	ldr	r6, [r4, #2256]
	ldr	r3, .L1788+24
	cmp	r6, r5
	ldrb	r0, [r4, #2069]	@ zero_extendqisi2
	str	r3, [r4, #1204]
	mov	r3, #0
	strb	r3, [r4, #8]
	sub	ip, r0, #218
	ldreq	r3, .L1788+4
	clz	ip, ip
	moveq	r2, #1
	mov	ip, ip, lsr #5
	streq	r2, [r3, #2240]
	cmp	r0, #161
	cmpne	r0, #241
	and	r2, r0, #253
	moveq	r3, #1
	movne	r3, #0
	orr	r3, ip, r3
	cmp	r2, #209
	orreq	r3, r3, #1
	cmp	r3, #0
	bne	.L1702
	cmp	r0, #220
	bne	.L1703
	ldr	r3, .L1788+4
	ldrb	r3, [r3, #2071]	@ zero_extendqisi2
	cmp	r3, #149
	bne	.L1703
.L1702:
	ldrb	lr, [r4, #2068]	@ zero_extendqisi2
	mov	r3, #16
	strb	r3, [r4, #1]
	mov	r1, #1
	strb	r3, [r8, #1710]
	cmp	lr, #152
	ldr	r3, .L1788+28
	strb	r1, [r4]
	ldr	r2, .L1788
	strb	lr, [r3, #3321]
	strb	r0, [r3, #3322]
	bne	.L1705
	ldr	r7, .L1788+4
	movw	lr, #2072
	ldrsb	lr, [r7, lr]
	cmp	lr, #0
	strltb	r1, [r2, #1728]
	movge	r1, #24
	strgeb	r1, [r2, #1710]
.L1705:
	movw	r2, #2049
	cmp	r6, r5
	cmpne	r6, r2
	moveq	r2, #16
	streqb	r2, [r8, #1710]
	cmp	ip, #0
	ldrne	r2, .L1788+32
	movne	r1, #2048
	strneh	r1, [r2, #14]	@ movhi
	mvnne	r2, #37
	bne	.L1782
.L1709:
	cmp	r0, #220
	bne	.L1711
	ldr	r2, .L1788+32
	mov	r1, #4096
	strh	r1, [r2, #14]	@ movhi
	mvn	r2, #35
.L1782:
	strb	r2, [r3, #3322]
	b	.L1710
.L1711:
	cmp	r0, #211
	ldreq	r2, .L1788+32
	moveq	r1, #4096
	streqh	r1, [r2, #14]	@ movhi
	moveq	r2, #2
	streqb	r2, [r3, #3333]
.L1710:
	ldr	r1, .L1788+36
	mov	r2, #32
	ldr	r0, .L1788+40
	bl	ftl_memcpy
	ldr	r0, .L1788+24
	mov	r2, #32
	add	r1, r0, #2848
	bl	ftl_memcpy
.L1703:
	ldrb	r3, [r4]	@ zero_extendqisi2
	ldr	r6, .L1788+4
	cmp	r3, #0
	bne	.L1712
	bl	FlashLoadPhyInfoInRam
	cmp	r0, #0
	bne	.L1714
	ldr	r3, [r6, #1204]
	ldrh	r3, [r3, #16]
	mov	r3, r3, lsr #8
	tst	r3, #1
	and	r0, r3, #7
	strb	r0, [r6, #2228]
	bne	.L1714
	mov	r3, #1
	strb	r3, [r6, #2244]
	bl	FlashSetInterfaceMode
	ldrb	r0, [r6, #2228]	@ zero_extendqisi2
	bl	NandcSetMode
.L1714:
	ldr	r3, [r4, #1204]
	ldr	r6, .L1788+4
	ldrb	r3, [r3, #26]	@ zero_extendqisi2
	strb	r3, [r4, #140]
	bl	FlashLoadPhyInfo
	cmp	r0, #0
	beq	.L1712
	ldr	r3, [r6, #2248]
	cmp	r3, #0
	beq	.L1717
	mov	r0, #1
	bl	FlashSetInterfaceMode
	mov	r0, #1
	b	.L1783
.L1717:
	ldrb	r0, [r6, #2228]	@ zero_extendqisi2
	bl	FlashSetInterfaceMode
	ldrb	r0, [r6, #2228]	@ zero_extendqisi2
.L1783:
	bl	NandcSetMode
	bl	FlashLoadPhyInfo
	cmp	r0, #0
	beq	.L1712
	mov	r0, #1
	ldr	r6, .L1788+4
	bl	FlashSetInterfaceMode
	mov	r0, #1
	bl	NandcSetMode
	ldr	r3, [r4, #1204]
	ldr	r0, .L1788+44
	ldrh	r1, [r3, #14]
	bl	printk
	bl	FlashLoadPhyInfoInRam
	cmn	r0, #1
	beq	.L1758
	bl	FlashDieInfoInit
	ldr	r3, [r6, #1204]
	ldrb	r0, [r3, #19]	@ zero_extendqisi2
	bl	FlashGetReadRetryDefault
	ldr	r3, .L1788+48
	ldr	r2, [r6, #1204]
	ldrh	r3, [r3, #-2]
	ldrb	r1, [r2, #9]	@ zero_extendqisi2
	add	r3, r3, #4080
	add	r3, r3, #15
	cmp	r1, r3, asr #12
	ldrh	r3, [r2, #14]
	blt	.L1719
	add	r0, r3, #255
	cmp	r1, r0, asr #8
	bge	.L1720
.L1719:
	bic	r3, r3, #255
	strh	r3, [r2, #14]	@ movhi
.L1720:
	ldrb	r3, [r4, #2228]	@ zero_extendqisi2
	tst	r3, #6
	beq	.L1721
	bl	FlashSavePhyInfo
	mov	r0, #0
	ldr	r1, [r8, #1700]
	bl	FlashDdrParaScan
.L1721:
	bl	FlashSavePhyInfo
.L1712:
	ldr	r2, [r4, #1204]
	ldr	r7, .L1788+4
	ldr	r6, .L1788
	ldrb	r3, [r2, #26]	@ zero_extendqisi2
	ldrh	r0, [r2, #10]
	ldrb	r9, [r2, #18]	@ zero_extendqisi2
	strb	r3, [r4, #140]
	ldrh	r3, [r2, #16]
	ubfx	r1, r3, #7, #1
	strb	r1, [r4, #8]
	ubfx	r1, r3, #3, #1
	strb	r1, [r8, #1729]
	ubfx	r1, r3, #4, #1
	ubfx	r3, r3, #8, #3
	strb	r1, [r4, #2305]
	strb	r3, [r4, #2228]
	mov	r3, #0
	ldrb	r1, [r2, #12]	@ zero_extendqisi2
	str	r3, [r8, #1692]
	bl	__aeabi_idiv
	mov	r1, r0
	mov	r0, r9
	bl	BuildFlashLsbPageTable
	bl	FlashDieInfoInit
	ldr	r3, [r4, #1204]
	ldrh	r2, [r3, #16]
	tst	r2, #64
	beq	.L1723
	ldrb	r0, [r3, #19]	@ zero_extendqisi2
	ldrb	r3, [r7, #1211]	@ zero_extendqisi2
	strb	r0, [r7, #1208]
	strb	r3, [r7, #1209]
	ldrb	r3, [r7, #1212]	@ zero_extendqisi2
	strb	r3, [r6, #1709]
	sub	r3, r0, #1
	cmp	r3, #7
	bhi	.L1724
	ldr	r3, .L1788+52
	sub	r2, r0, #8
	clz	r2, r2
	str	r3, [r6, #1692]
	sub	r3, r0, #5
	cmp	r0, #8
	cmpne	r3, #1
	mov	r2, r2, lsr #5
	ldr	r6, .L1788+56
	movls	r3, #1
	strls	r3, [r7, #2296]
	cmp	r0, #7
	beq	.L1727
	sub	r3, r6, #8
	cmp	r2, #0
	moveq	r6, r3
.L1727:
	sub	r2, r6, #1
	add	r6, r6, #31
	mov	r3, #0
.L1728:
	ldrsb	r1, [r2, #1]!
	cmp	r1, #0
	addeq	r3, r3, #1
	cmp	r2, r6
	bne	.L1728
	cmp	r3, #27
	bls	.L1723
	bl	FlashGetReadRetryDefault
	bl	FlashSavePhyInfo
	b	.L1723
.L1724:
	sub	r3, r0, #17
	cmp	r3, #2
	bhi	.L1730
	ldr	r3, .L1788+60
	cmp	r0, #19
	str	r3, [r6, #1692]
	moveq	r3, #15
	bne	.L1787
	b	.L1785
.L1730:
	sub	r3, r0, #65
	cmp	r0, #33
	cmpne	r3, #1
	bhi	.L1732
	ldr	r3, .L1788+64
	str	r3, [r6, #1692]
	mov	r3, #4
	strb	r3, [r7, #1209]
.L1787:
	mov	r3, #7
.L1785:
	strb	r3, [r6, #1709]
	b	.L1723
.L1732:
	sub	r3, r0, #67
	sub	r2, r0, #34
	cmp	r3, #1
	movhi	r1, #0
	movls	r1, #1
	cmp	r3, #1
	cmphi	r2, #1
	bhi	.L1733
	ldr	r3, .L1788+64
	cmp	r0, #68
	cmpne	r0, #35
	str	r3, [r6, #1692]
	movne	r3, #7
	moveq	r3, #17
	cmp	r1, #0
	strb	r3, [r6, #1709]
	movne	r3, #4
	moveq	r3, #5
	strb	r3, [r4, #1209]
	b	.L1723
.L1733:
	cmp	r0, #49
	ldreq	r3, .L1788+68
	streq	r3, [r6, #1692]
.L1723:
	ldr	r2, [r4, #2256]
	ldr	r3, .L1788+4
	cmp	r2, r5
	bne	.L1737
	ldrb	r1, [r3, #140]	@ zero_extendqisi2
	cmp	r1, #0
	ldrne	r3, [r3, #1204]
	movne	r1, #0
	strneb	r1, [r3, #18]
.L1737:
	ldrb	r1, [r4, #2068]	@ zero_extendqisi2
	ldr	r3, .L1788+4
	cmp	r1, #44
	bne	.L1738
	ldrb	r1, [r3, #2244]	@ zero_extendqisi2
	cmp	r1, #0
	beq	.L1738
	cmp	r2, r5
	bne	.L1739
	ldrb	r3, [r3, #140]	@ zero_extendqisi2
	cmp	r3, #0
	bne	.L1738
.L1739:
	mov	r0, #1
	mov	r3, #0
	strb	r3, [r4, #2244]
	bl	FlashSetInterfaceMode
	mov	r0, #1
	bl	NandcSetMode
.L1738:
	ldrb	r3, [r4, #2228]	@ zero_extendqisi2
	tst	r3, #6
	beq	.L1740
	ldr	r2, .L1788+4
	ldrb	r2, [r2, #2244]	@ zero_extendqisi2
	cmp	r2, #0
	bne	.L1741
	tst	r3, #1
	bne	.L1740
.L1741:
	mov	r0, #0
	bl	flash_enter_slc_mode
	mov	r0, #0
	ldr	r1, [r8, #1700]
	bl	FlashDdrParaScan
	mov	r0, #0
	bl	flash_exit_slc_mode
.L1740:
	ldr	r3, [r4, #1204]
	mov	r8, #16
	ldr	r6, .L1788+4
	ldrb	r0, [r3, #20]	@ zero_extendqisi2
	bl	FlashBchSel
	ldr	r0, .L1788+72
	bl	FlashReadIdbDataRaw
	ldr	r0, .L1788+76
	strb	r8, [r4, #1]
	bl	FlashTimingCfg
	ldr	r5, [r4, #1204]
	ldrb	r3, [r5, #12]	@ zero_extendqisi2
	ldrh	r7, [r5, #14]
	strh	r3, [r4, #120]	@ movhi
	ldrb	r3, [r5, #7]	@ zero_extendqisi2
	str	r3, [r4, #116]
	ldr	r3, [r4, #2068]
	str	r3, [r4, #112]
	ldrb	r3, [r4, #2229]	@ zero_extendqisi2
	strh	r3, [r4, #122]	@ movhi
	ldrb	r3, [r5, #13]	@ zero_extendqisi2
	strh	r7, [r4, #126]	@ movhi
	strh	r3, [r4, #124]	@ movhi
	ldrh	r3, [r5, #10]
	strh	r3, [r4, #128]	@ movhi
	ldrb	r1, [r5, #12]	@ zero_extendqisi2
	ldrh	r0, [r5, #10]
	bl	__aeabi_idiv
	strh	r0, [r4, #130]	@ movhi
	ldrb	r2, [r5, #9]	@ zero_extendqisi2
	strh	r2, [r4, #132]	@ movhi
	ldrb	r1, [r5, #9]	@ zero_extendqisi2
	ldrh	r3, [r5, #10]
	smulbb	r3, r1, r3
	mov	r1, #512
	strh	r1, [r4, #136]	@ movhi
	ldrb	r1, [r4, #1]	@ zero_extendqisi2
	strh	r1, [r4, #138]	@ movhi
	uxth	r3, r3
	ldrb	r1, [r4]	@ zero_extendqisi2
	strh	r3, [r4, #134]	@ movhi
	cmp	r1, #1
	bne	.L1743
	mov	r3, r3, asl #1
	mov	r1, r7, lsr #1
	mov	r2, r2, asl #1
	strh	r3, [r6, #134]	@ movhi
	strb	r8, [r6, #1]
	mov	r3, #8
	strh	r1, [r6, #126]	@ movhi
	strh	r2, [r6, #132]	@ movhi
	strh	r3, [r6, #138]	@ movhi
.L1743:
	ldrb	r0, [r5, #20]	@ zero_extendqisi2
	bl	FlashBchSel
	bl	ftl_flash_suspend
	mov	r0, #0
	b	.L1758
.L1745:
	mvn	r0, #1
.L1758:
	add	sp, sp, #20
	@ sp needed
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L1789:
	.align	2
.L1788:
	.word	.LANCHOR2
	.word	.LANCHOR0
	.word	.LANCHOR0+2068
	.word	.LC96
	.word	.LANCHOR0+1210
	.word	1446522928
	.word	.LANCHOR1+472
	.word	.LANCHOR1
	.word	.LANCHOR1+3320
	.word	.LANCHOR1+3196
	.word	.LANCHOR0+44
	.word	.LC97
	.word	.LANCHOR2-572
	.word	HynixReadRetrial
	.word	.LANCHOR0+1238
	.word	MicronReadRetrial
	.word	ToshibaReadRetrial
	.word	SamsungReadRetrial
	.word	.LANCHOR2-368
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
	ldr	r2, .L1804
	mov	r8, r1
	mov	r7, r0
	ldrb	r1, [r2, #140]	@ zero_extendqisi2
	mov	r6, r2
	ldr	r3, [r2, #1204]
	cmp	r1, #0
	ldrb	r3, [r3, #19]	@ zero_extendqisi2
	beq	.L1791
	ldr	r2, [r2, #2240]
	cmp	r2, #0
	ldmnefd	sp!, {r4, r5, r6, r7, r8, r9, r10, pc}
.L1791:
	sub	r2, r3, #5
	cmp	r3, #68
	cmpne	r2, #2
	bls	.L1792
	sub	r3, r3, #19
	tst	r3, #239
	ldmnefd	sp!, {r4, r5, r6, r7, r8, r9, r10, pc}
.L1792:
	ldr	r5, .L1804+4
	movw	r10, #65535
	sub	r9, r5, #2624
.L1794:
	ldr	r3, [r6, #1204]
	ldrh	r3, [r3, #10]
	cmp	r3, r4
	bls	.L1803
	mov	r3, r4, asl #1
	ldrh	r3, [r9, r3]
	cmp	r3, r10
	ldmnefd	sp!, {r4, r5, r6, r7, r8, r9, r10, pc}
	mov	r1, #255
	mov	r2, #32768
	ldr	r0, [r5, #1712]
	bl	ftl_memset
	ldr	r2, [r5, #1712]
	add	r1, r4, r8
	add	r4, r4, #1
	mov	r0, r7
	mov	r3, r2
	uxth	r4, r4
	bl	FlashProgPage
	b	.L1794
.L1803:
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, pc}
.L1805:
	.align	2
.L1804:
	.word	.LANCHOR0
	.word	.LANCHOR2
	.fnend
	.size	FlashPageProgMsbFFData, .-FlashPageProgMsbFFData
	.align	2
	.global	FlashReadSlc2KPages
	.type	FlashReadSlc2KPages, %function
FlashReadSlc2KPages:
	.fnstart
	@ args = 0, pretend = 0, frame = 24
	@ frame_needed = 0, uses_anonymous_args = 0
	ldr	r3, .L1857
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
.L1807:
	ldr	r3, [sp, #16]
	cmp	r8, r3
	beq	.L1856
	ldr	r3, [sp, #16]
	add	r2, sp, #28
	mov	r0, r4
	ldr	r1, [sp, #20]
	rsb	r3, r8, r3
	ldr	r9, .L1857+4
	uxtb	r3, r3
	str	r3, [sp]
	add	r3, sp, #24
	mov	fp, r9
	bl	LogAddr2PhyAddr
	ldrb	r2, [r9, #2229]	@ zero_extendqisi2
	ldr	r3, [sp, #24]
	cmp	r3, r2
	mvncs	r3, #0
	strcs	r3, [r4]
	bcs	.L1809
	add	r3, r9, r3
	mov	r7, #0
	ldrb	r5, [r3, #2232]	@ zero_extendqisi2
	mov	r0, r5
	bl	NandcWaitFlashReady
	mov	r0, r5
	bl	NandcFlashCs
.L1810:
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
	ldr	r3, .L1857+8
	ldrb	r3, [r3, #1728]	@ zero_extendqisi2
	cmp	r3, #0
	mov	r6, r0
	beq	.L1811
	mov	r0, r5
	bl	flash_read_ecc
	cmp	r0, #5
	movhi	r6, #256
.L1811:
	cmp	r7, #9
	cmnls	r6, #1
	moveq	r3, #1
	movne	r3, #0
	addeq	r7, r7, #1
	beq	.L1810
.L1812:
	cmp	r7, #0
	mov	r7, r3
	movne	r6, #256
.L1814:
	ldr	r3, [sp, #28]
	mov	r0, r5
	ldr	r1, [fp, #4]
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
	ldr	r3, .L1857+8
	ldrb	r2, [r3, #1728]	@ zero_extendqisi2
	cmp	r2, #0
	mov	r10, r0
	beq	.L1817
	mov	r0, r5
	bl	flash_read_ecc
	cmp	r0, #5
	movhi	r10, #256
.L1817:
	cmp	r7, #9
	cmnls	r10, #1
	addeq	r7, r7, #1
	beq	.L1814
.L1818:
	cmp	r7, #0
	mov	r0, r5
	movne	r10, #256
	bl	NandcFlashDeCs
	ldrb	r2, [r9, #2304]	@ zero_extendqisi2
	cmp	r10, r6
	movcs	r3, r10
	movcc	r3, r6
	add	r2, r2, r2, asl #1
	cmp	r3, r2, asr #2
	bls	.L1820
	cmn	r3, #1
	movne	r3, #256
.L1820:
	cmp	r3, #256
	cmnne	r3, #1
	movne	r3, #0
	str	r3, [r4]
	ldr	r3, [r4, #12]
	cmp	r3, #0
	beq	.L1823
	ldr	r2, [r3, #12]
	cmn	r2, #1
	bne	.L1823
	ldr	r2, [r3, #8]
	cmn	r2, #1
	bne	.L1823
	ldr	r3, [r3]
	cmn	r3, #1
	strne	r2, [r4]
.L1823:
	ldr	r3, [r4]
	cmn	r3, #1
	bne	.L1809
	ldr	r1, [r4, #4]
	ldr	r0, .L1857+12
	ldrb	r2, [r9, #2304]	@ zero_extendqisi2
	bl	printk
	ldr	r1, [r4, #8]
	cmp	r1, #0
	beq	.L1825
	ldr	r0, .L1857+16
	mov	r2, #4
	mov	r3, #8
	bl	rknand_print_hex
.L1825:
	ldr	r1, [r4, #12]
	cmp	r1, #0
	beq	.L1809
	mov	r2, #4
	ldr	r0, .L1857+20
	mov	r3, r2
	bl	rknand_print_hex
.L1809:
	add	r8, r8, #1
	add	r4, r4, #36
	b	.L1807
.L1856:
	mov	r0, #0
	add	sp, sp, #36
	@ sp needed
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L1858:
	.align	2
.L1857:
	.word	.LANCHOR1
	.word	.LANCHOR0
	.word	.LANCHOR2
	.word	.LC98
	.word	.LC99
	.word	.LC100
	.fnend
	.size	FlashReadSlc2KPages, .-FlashReadSlc2KPages
	.align	2
	.global	FlashReadPages
	.type	FlashReadPages, %function
FlashReadPages:
	.fnstart
	@ args = 0, pretend = 0, frame = 40
	@ frame_needed = 0, uses_anonymous_args = 0
	ldr	r3, .L1934
	stmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.save {r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.pad #52
	sub	sp, sp, #52
	ldr	r6, .L1934+4
	ldrb	r3, [r3, #481]	@ zero_extendqisi2
	str	r1, [sp, #20]
	ldrb	r8, [r6]	@ zero_extendqisi2
	str	r3, [sp, #16]
	ldrb	r3, [r6, #8]	@ zero_extendqisi2
	cmp	r8, #0
	str	r2, [sp, #24]
	str	r3, [sp, #28]
	beq	.L1896
	bl	FlashReadSlc2KPages
	b	.L1929
.L1896:
	ldr	r10, .L1934+8
	mov	r9, r0
	mov	fp, r8
.L1860:
	ldr	r3, [sp, #20]
	cmp	fp, r3
	bcs	.L1932
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
	ldrb	r2, [r6, #2229]	@ zero_extendqisi2
	ldr	r3, [sp, #40]
	mov	r5, r0
	cmp	r3, r2
	ldrcs	r2, [sp, #8]
	mvncs	r3, #0
	strcs	r3, [r9, r2]
	bcs	.L1863
	add	r3, r6, r3
	ldrb	r4, [r3, #2232]	@ zero_extendqisi2
	ldrb	r3, [r10, #1729]	@ zero_extendqisi2
	cmp	r3, #0
	mov	r0, r4
	moveq	r5, #0
	bl	NandcWaitFlashReady
	ldr	r3, .L1934+4
	ldr	r3, [r3, #1204]
	ldrb	r2, [r3, #19]	@ zero_extendqisi2
	sub	r3, r2, #1
	cmp	r3, #7
	bhi	.L1865
	sub	r2, r2, #7
	add	r1, r6, r4
	cmp	r2, #1
	ldr	r2, .L1934+4
	ldrb	r3, [r1, #1222]	@ zero_extendqisi2
	add	r2, r2, r4
	ldrlsb	r3, [r1, #1230]	@ zero_extendqisi2
	ldrb	r2, [r2, #2064]	@ zero_extendqisi2
	cmp	r2, r3
	beq	.L1865
	mov	r0, r4
	ldrb	r1, [r6, #1211]	@ zero_extendqisi2
	ldr	r2, .L1934+12
	bl	HynixSetRRPara
.L1865:
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
	beq	.L1867
	ldr	r3, .L1934+4
	ldrb	r3, [r3, #140]	@ zero_extendqisi2
	cmp	r3, #0
	beq	.L1867
	bl	flash_enter_slc_mode
	b	.L1868
.L1867:
	bl	flash_exit_slc_mode
.L1868:
	ldr	r1, [sp, #44]
	cmn	r1, #1
	cmpeq	r4, #255
	moveq	r3, #0
	movne	r3, #1
	moveq	r5, r3
	beq	.L1870
	cmp	r5, #0
	beq	.L1871
	ldr	r2, [r6, #4]
	mov	r0, r4
	add	r2, r1, r2
	bl	FlashReadDpCmd
	b	.L1872
.L1871:
	mov	r0, r4
	bl	FlashReadCmd
.L1872:
	mov	r0, r4
	bl	NandcWaitFlashReady
	cmp	r5, #0
	beq	.L1870
	mov	r0, r4
	ldr	r1, [sp, #44]
	bl	FlashReadDpDataOutCmd
.L1870:
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
	bne	.L1868
.L1873:
	cmp	r5, #0
	beq	.L1874
	ldr	r3, .L1934+4
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
.L1874:
	mov	r0, r4
	str	ip, [sp, #32]
	bl	NandcFlashDeCs
	ldr	ip, [sp, #32]
	ldrb	r3, [sp, #28]	@ zero_extendqisi2
	cmn	ip, #1
	strb	r3, [r6, #8]
	bne	.L1881
	ldrb	r3, [r6, #2244]	@ zero_extendqisi2
	cmp	r3, #0
	bne	.L1876
.L1880:
	ldr	r5, [r10, #1692]
	cmp	r5, #0
	bne	.L1877
	b	.L1933
.L1876:
	ldr	r3, [r6, #76]
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
	beq	.L1879
	ldrb	r3, [r6, #2304]	@ zero_extendqisi2
	cmp	r0, r3, lsr #1
	bls	.L1899
.L1879:
	ubfx	r0, r5, #8, #8
	str	ip, [sp, #32]
	bl	NandcSetDdrPara
	ldr	ip, [sp, #32]
	cmn	ip, #1
	beq	.L1880
	b	.L1899
.L1877:
	mov	r0, r4
	ldr	r1, [sp, #44]
	ldr	r2, [r7, #8]
	ldr	r3, [r7, #12]
	blx	r5
	cmn	r0, #1
	mov	ip, r0
	bne	.L1901
	ldr	r3, [r6, #1204]
	ldrb	r3, [r3, #19]	@ zero_extendqisi2
	sub	r3, r3, #1
	cmp	r3, #7
	bhi	.L1883
	mov	r0, r4
	ldrb	r1, [r6, #1211]	@ zero_extendqisi2
	ldr	r2, .L1934+12
	mov	r3, #0
	bl	HynixSetRRPara
.L1883:
	ldr	r1, [sp, #44]
	mov	r0, r4
	ldr	r2, [r7, #8]
	ldr	r3, [r7, #12]
	bl	FlashReadRawPage
	ldr	r1, [r7, #4]
	ldrb	r2, [r6, #2304]	@ zero_extendqisi2
	mov	ip, r0
	ldr	r0, .L1934+16
	mov	r3, ip
	str	ip, [sp, #32]
	bl	printk
	ldr	ip, [sp, #32]
	cmn	ip, #1
	bne	.L1901
	ldrb	r5, [r6, #140]	@ zero_extendqisi2
	cmp	r5, #0
	beq	.L1882
	ldr	r3, [sp, #12]
	mov	r0, r4
	cmp	r3, #0
	beq	.L1884
	bl	flash_enter_slc_mode
	b	.L1885
.L1884:
	bl	flash_exit_slc_mode
.L1885:
	ldr	ip, [r10, #1692]
	mov	r0, r4
	ldr	r1, [sp, #44]
	ldr	r2, [r7, #8]
	ldr	r3, [r7, #12]
	blx	ip
	mov	ip, r0
	b	.L1901
.L1933:
	mov	r0, r4
	ldr	r1, [sp, #44]
	ldr	r2, [r7, #8]
	ldr	r3, [r7, #12]
	bl	FlashReadRawPage
	mov	ip, r0
	b	.L1882
.L1899:
	mov	r5, #0
.L1881:
	ldrb	r3, [r6, #2304]	@ zero_extendqisi2
	add	r3, r3, r3, asl #1
	cmp	ip, r3, asr #2
	bls	.L1882
	ldr	r3, [r10, #1692]
	cmp	r3, #0
	moveq	ip, #256
	b	.L1882
.L1901:
	mov	r5, #0
.L1882:
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
	bne	.L1889
	ldr	r1, [r7, #4]
	ldr	r0, .L1934+20
	ldrb	r2, [r6, #2304]	@ zero_extendqisi2
	bl	printk
	ldr	r1, [r7, #12]
	cmp	r1, #0
	beq	.L1889
	mov	r2, #4
	ldr	r0, .L1934+24
	mov	r3, r2
	bl	rknand_print_hex
.L1889:
	cmp	r5, #0
	beq	.L1891
	ldrb	r3, [r6, #2304]	@ zero_extendqisi2
	add	r3, r3, r3, asl #1
	cmp	r8, r3, asr #2
	bls	.L1892
	ldr	r3, [r10, #1692]
	cmp	r3, #0
	moveq	r8, #256
.L1892:
	ldr	r3, [sp, #8]
	cmp	r8, #256
	cmnne	r8, #1
	add	r3, r3, #36
	movne	r2, #0
	streq	r8, [r9, r3]
	strne	r2, [r9, r3]
.L1891:
	ldr	r3, [sp, #12]
	add	fp, fp, r5
	cmp	r3, #0
	beq	.L1863
	ldrb	r3, [r6, #140]	@ zero_extendqisi2
	cmp	r3, #0
	beq	.L1863
	mov	r0, r4
	bl	flash_exit_slc_mode
.L1863:
	add	fp, fp, #1
	b	.L1860
.L1932:
	mov	r0, #0
.L1929:
	add	sp, sp, #52
	@ sp needed
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L1935:
	.align	2
.L1934:
	.word	.LANCHOR1
	.word	.LANCHOR0
	.word	.LANCHOR2
	.word	.LANCHOR0+1214
	.word	.LC101
	.word	.LC98
	.word	.LC100
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
	ldr	r4, .L1949
	.pad #60
	sub	sp, sp, #60
	ldr	r0, .L1949+4
	mov	r1, #0
	ldr	r10, .L1949+8
	mov	r5, #0
	ldrh	r3, [r4, #126]
	mvn	fp, #0
	ldrh	r6, [r4, #124]
	mov	r9, r5
	mov	r8, r4
	smulbb	r6, r6, r3
	bl	ftl_memset
	ldr	r3, [r10, #1716]
	uxth	r6, r6
	add	r2, r6, fp
	str	r3, [sp, #32]
	uxth	r3, r2
	str	r5, [sp, #28]
	str	r3, [sp, #4]
.L1937:
	ldrb	r3, [r8, #2229]	@ zero_extendqisi2
	uxtb	r7, r5
	cmp	r3, r7
	bls	.L1948
	mul	ip, r6, r7
	ldr	r4, [sp, #4]
	sub	r3, r6, #12
.L1938:
	cmp	r4, r3
	ble	.L1940
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
	beq	.L1939
	ldr	r2, [r10, #1716]
	ldrh	r1, [r2]
	movw	r2, #61664
	cmp	r1, r2
	bne	.L1939
	mov	r1, r7
	ldr	r0, .L1949+12
	mov	r2, r4
	mov	r7, r7, asl #1
	bl	printk
	ldr	r3, .L1949+4
	add	r9, r9, #1
	strh	r4, [r3, r7]	@ movhi
	uxth	r9, r9
	b	.L1940
.L1939:
	sub	r4, r4, #1
	uxth	r4, r4
	b	.L1938
.L1940:
	ldrb	r3, [r8, #2229]	@ zero_extendqisi2
	add	r5, r5, #1
	cmp	r3, r9
	moveq	fp, #0
	b	.L1937
.L1948:
	mov	r0, fp
	add	sp, sp, #60
	@ sp needed
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L1950:
	.align	2
.L1949:
	.word	.LANCHOR0
	.word	.LANCHOR2+1732
	.word	.LANCHOR2
	.word	.LC102
	.fnend
	.size	FlashLoadFactorBbt, .-FlashLoadFactorBbt
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
	ldr	r8, .L1982
	mov	r6, r1
	str	r3, [sp, #12]
	mov	r9, r2
	ldr	r3, .L1982+4
	mov	r4, r0
	mov	r10, r0
	mov	r7, #0
	mov	fp, r8
	ldrb	r3, [r3, #481]	@ zero_extendqisi2
	str	r3, [sp, #8]
.L1952:
	cmp	r7, r6
	beq	.L1981
	rsb	r3, r7, r6
	add	r2, sp, #20
	mov	r0, r10
	mov	r1, r9
	uxtb	r3, r3
	str	r3, [sp]
	add	r3, sp, #24
	bl	LogAddr2PhyAddr
	ldrb	r2, [r8, #2229]	@ zero_extendqisi2
	ldr	r3, [sp, #24]
	cmp	r3, r2
	mvncs	r3, #0
	strcs	r3, [r10]
	bcs	.L1954
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
.L1954:
	add	r7, r7, #1
	add	r10, r10, #36
	b	.L1952
.L1981:
	ldr	r3, [sp, #12]
	cmp	r3, #0
	beq	.L1979
	ldr	r5, .L1982+8
	mov	r7, #0
	mov	r8, r5
.L1959:
	cmp	r7, r6
	beq	.L1979
	ldr	r3, [r4]
	cmn	r3, #1
	bne	.L1960
	ldr	r1, [r4, #4]
	ldr	r0, .L1982+12
	bl	printk
	b	.L1961
.L1960:
	rsb	r3, r7, r6
	mov	r1, r9
	add	r2, sp, #20
	mov	r0, r4
	uxtb	r3, r3
	str	r3, [sp]
	add	r3, sp, #24
	bl	LogAddr2PhyAddr
	ldr	r2, [r5, #1720]
	mov	r3, #0
	mov	lr, r4
	str	r3, [r2]
	ldr	r2, [r5, #1724]
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
	ldr	r3, [r5, #1720]
	str	r3, [sp, #36]
	ldr	r3, [r5, #1724]
	str	r3, [sp, #40]
	bl	FlashReadPages
	ldr	r10, [sp, #28]
	cmn	r10, #1
	bne	.L1962
	ldr	r0, .L1982+16
	ldr	r1, [r4, #4]
	bl	printk
	str	r10, [r4]
.L1962:
	ldr	r3, [r4, #12]
	cmp	r3, #0
	beq	.L1963
	ldr	r2, [r3]
	ldr	r3, [r8, #1724]
	ldr	r3, [r3]
	cmp	r2, r3
	beq	.L1963
	ldr	r0, .L1982+20
	ldr	r1, [r4, #4]
	bl	printk
	mvn	r3, #0
	str	r3, [r4]
.L1963:
	ldr	r3, [r4, #8]
	cmp	r3, #0
	beq	.L1961
	ldr	r2, [r3]
	ldr	r3, [r8, #1720]
	ldr	r3, [r3]
	cmp	r2, r3
	beq	.L1961
	ldr	r0, .L1982+24
	ldr	r1, [r4, #4]
	bl	printk
	mvn	r3, #0
	str	r3, [r4]
.L1961:
	add	r7, r7, #1
	add	r4, r4, #36
	b	.L1959
.L1979:
	mov	r0, #0
	add	sp, sp, #68
	@ sp needed
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L1983:
	.align	2
.L1982:
	.word	.LANCHOR0
	.word	.LANCHOR1
	.word	.LANCHOR2
	.word	.LC103
	.word	.LC104
	.word	.LC105
	.word	.LC106
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
	ldr	r5, .L1996
	ldr	r6, .L1996+4
	ldr	r3, [r5, #-528]
	sub	r9, r6, #76
	ldr	r8, [r5, #-504]
	sub	r10, r6, #120
	mov	fp, r9
	str	r3, [r5, #1756]
	str	r8, [r5, #1760]
.L1985:
	ldrh	r3, [r10]
	cmp	r7, r3
	bcs	.L1995
	ldrh	r4, [r9]
	mvn	r3, #0
	movw	ip, #61664
	strh	r3, [r6, #2]!	@ movhi
	add	r4, r4, r3
	uxth	r4, r4
.L1986:
	ldrh	r3, [fp]
	sub	r2, r3, #16
	cmp	r4, r2
	ble	.L1988
	mla	r3, r3, r7, r4
	mov	r1, #1
	ldr	r0, .L1996+8
	mov	r2, r1
	str	ip, [sp, #4]
	mov	r3, r3, asl #10
	str	r3, [r5, #1752]
	bl	FlashReadPages
	ldr	r3, [r5, #1748]
	cmn	r3, #1
	ldr	ip, [sp, #4]
	beq	.L1987
	ldrh	r3, [r8]
	cmp	r3, ip
	streqh	r4, [r6]	@ movhi
	beq	.L1988
.L1987:
	sub	r4, r4, #1
	uxth	r4, r4
	b	.L1986
.L1988:
	add	r7, r7, #1
	b	.L1985
.L1995:
	mov	r0, #0
	add	sp, sp, #12
	@ sp needed
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L1997:
	.align	2
.L1996:
	.word	.LANCHOR2
	.word	.LANCHOR0+2458
	.word	.LANCHOR2+1748
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
	stmfd	sp!, {r4, r5, r6, r7, r8, lr}
	.save {r4, r5, r6, r7, r8, lr}
	movweq	r3, #2386
	ldreq	r2, .L2010
	.pad #104
	sub	sp, sp, #104
	ldrne	r3, .L2010+4
	mov	r8, r1
	mov	r6, r0, asl #10
	mov	r1, #1
	ldreqh	r4, [r2, r3]
	add	r0, sp, #4
	ldrneh	r4, [r3]
	mov	r2, r8
	add	r3, sp, #40
	str	r3, [sp, #16]
	sub	r5, r4, #1
	mov	r7, #0
	str	r7, [sp, #12]
	uxth	r5, r5
	sxth	r3, r5
	orr	r3, r3, r6
	str	r3, [sp, #8]
	bl	FlashReadPages
	ldr	r3, [sp, #40]
	cmn	r3, #1
	bne	.L2001
.L2002:
	sxth	r4, r7
	sxth	r3, r5
	cmp	r4, r3
	bgt	.L2001
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
	bne	.L2003
	ldr	r3, [sp, #44]
	cmn	r3, #1
	bne	.L2003
	ldr	r3, [sp, #4]
	cmn	r3, #1
	subne	r4, r4, #1
	uxthne	r5, r4
	bne	.L2002
.L2003:
	add	r3, r4, #1
	uxth	r7, r3
	b	.L2002
.L2001:
	sxth	r0, r5
	add	sp, sp, #104
	@ sp needed
	ldmfd	sp!, {r4, r5, r6, r7, r8, pc}
.L2011:
	.align	2
.L2010:
	.word	.LANCHOR0
	.word	.LANCHOR0+2384
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
	ldr	r4, .L2045
	ldr	r7, .L2045+4
	ldr	r8, .L2045+8
	ldr	r3, [r4, #-528]
	mov	r9, r4
	ldr	r6, [r4, #-504]
	str	r3, [r4, #1756]
	str	r6, [r4, #1760]
	bl	FtlBbtMemInit
	movw	r3, #2382
	ldrh	r5, [r7, r3]
	sub	r5, r5, #1
	uxth	r5, r5
.L2013:
	ldrh	r3, [r8]
	sub	r3, r3, #48
	cmp	r5, r3
	ble	.L2016
	mov	r1, #1
	ldr	r0, .L2045+12
	mov	r2, r1
	mov	r3, r5, asl #10
	str	r3, [r4, #1752]
	bl	FlashReadPages
	ldr	r3, [r4, #1748]
	cmn	r3, #1
	bne	.L2014
	ldr	r3, [r9, #1752]
	mov	r1, #1
	ldr	r0, .L2045+12
	mov	r2, r1
	add	r3, r3, #1
	str	r3, [r9, #1752]
	bl	FlashReadPages
.L2014:
	ldr	r3, [r4, #1748]
	cmn	r3, #1
	beq	.L2015
	ldrh	r2, [r6]
	movw	r3, #61649
	cmp	r2, r3
	bne	.L2015
	ldr	r2, [r6, #4]
	ldr	r3, .L2045+16
	str	r2, [r7, #2456]
	ldrh	r2, [r6, #8]
	strh	r5, [r3]	@ movhi
	strh	r2, [r3, #4]	@ movhi
	b	.L2016
.L2015:
	sub	r5, r5, #1
	uxth	r5, r5
	b	.L2013
.L2016:
	ldr	r7, .L2045+4
	movw	r2, #65535
	add	r5, r7, #2448
	ldrh	r3, [r5]
	cmp	r3, r2
	beq	.L2030
	ldrh	r3, [r5, #4]
	cmp	r3, r2
	beq	.L2020
	mov	r1, #1
	ldr	r0, .L2045+12
	mov	r2, r1
	mov	r3, r3, asl #10
	str	r3, [r4, #1752]
	bl	FlashReadPages
	ldr	r3, [r4, #1748]
	cmn	r3, #1
	beq	.L2020
	ldrh	r2, [r6]
	movw	r3, #61649
	cmp	r2, r3
	bne	.L2020
	ldr	r3, [r6, #4]
	ldr	r2, [r7, #2456]
	cmp	r3, r2
	strhi	r3, [r7, #2456]
	ldrhih	r2, [r5, #4]
	ldrhih	r3, [r6, #8]
	strhih	r2, [r5]	@ movhi
	strhih	r3, [r5, #4]	@ movhi
.L2020:
	ldrh	r0, [r5]
	mov	r1, #1
	bl	FtlGetLastWrittenPage
	movw	r8, #61649
	uxth	r7, r0
	add	r0, r0, #1
	strh	r0, [r5, #2]	@ movhi
.L2022:
	sxth	r3, r7
	cmp	r3, #0
	blt	.L2027
	ldrh	r2, [r5]
	mov	r1, #1
	ldr	r0, .L2045+12
	orr	r3, r3, r2, asl #10
	str	r3, [r4, #1752]
	ldr	r3, [r4, #-528]
	mov	r2, r1
	str	r3, [r4, #1756]
	bl	FlashReadPages
	ldr	r3, [r4, #1748]
	cmn	r3, #1
	beq	.L2023
	ldrh	r3, [r6]
	cmp	r3, r8
	bne	.L2023
.L2027:
	ldrh	r2, [r6, #10]
	ldrh	r0, [r6, #12]
	ldr	r3, .L2045+4
	strh	r2, [r5, #6]	@ movhi
	movw	r2, #65535
	cmp	r0, r2
	bne	.L2024
	b	.L2025
.L2023:
	sub	r7, r7, #1
	uxth	r7, r7
	b	.L2022
.L2024:
	ldr	r2, [r3, #2312]
	cmp	r0, r2
	beq	.L2025
	movw	r1, #2326
	ldrh	r3, [r3, r1]
	mov	r3, r3, lsr #2
	cmp	r0, r3
	cmpcc	r2, r3
	bcs	.L2025
	bl	FtlSysBlkNumInit
.L2025:
	ldr	r6, .L2045+20
	mov	r5, #0
	ldr	r8, .L2045+24
	sub	r7, r6, #134
.L2028:
	ldrh	r3, [r7]
	cmp	r5, r3
	bcs	.L2044
	ldrh	r2, [r8]
	ldr	r1, [r4, #1756]
	ldr	r0, [r6, #4]!
	mov	r2, r2, asl #2
	mla	r1, r5, r2, r1
	bl	ftl_memcpy
	add	r5, r5, #1
	b	.L2028
.L2044:
	mov	r0, #0
	ldmfd	sp!, {r3, r4, r5, r6, r7, r8, r9, pc}
.L2030:
	mvn	r0, #0
	ldmfd	sp!, {r3, r4, r5, r6, r7, r8, r9, pc}
.L2046:
	.align	2
.L2045:
	.word	.LANCHOR2
	.word	.LANCHOR0
	.word	.LANCHOR0+2382
	.word	.LANCHOR2+1748
	.word	.LANCHOR0+2448
	.word	.LANCHOR0+2472
	.word	.LANCHOR2-440
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
	ldr	r7, .L2133
	mov	r1, r5
	ldr	r4, .L2133+4
	.pad #36
	sub	sp, sp, #36
	movw	r6, #2404
	ldr	r2, [r7, #2412]
	sub	r3, r4, #392
	ldr	r0, [r4, #-448]
	mov	r8, r4
	strh	r5, [r3]	@ movhi
	movw	r3, #2430
	mov	r2, r2, asl #2
	strh	r5, [r7, r3]	@ movhi
	bl	ftl_memset
	ldr	r2, [r7, #2412]
	mov	r1, r5
	ldr	r0, [r4, #-476]
	mov	r2, r2, asl #1
	bl	ftl_memset
	ldrh	r2, [r7, r6]
	mov	r1, r5
	ldr	r0, [r4, #-464]
	mov	r2, r2, asl #2
	bl	ftl_memset
	ldrh	r2, [r7, r6]
	ldr	r0, [r7, #2432]
	mov	r1, r5
	mov	r2, r2, asl #1
	bl	ftl_memset
	ldr	r0, .L2133+8
	mov	r1, #255
	mov	r2, #16
	bl	ftl_memset
	movw	r3, #2324
	ldrh	r3, [r7, r3]
	str	r7, [sp, #16]
	mov	r7, r4
	str	r3, [sp, #4]
.L2048:
	ldr	r3, .L2133+12
	ldr	r2, [sp, #4]
	ldr	r0, .L2133
	ldrh	r3, [r3]
	cmp	r3, r2
	bls	.L2089
	ldr	r1, [r4, #-2696]
	mov	r5, #0
	ldr	r3, .L2133+16
	mov	fp, r5
	ldr	r2, [r4, #-2700]
	mov	r9, #36
	str	r1, [sp, #8]
	ldr	r1, .L2133+20
	ldrh	ip, [r3]
	ldr	r3, [r4, #-540]
	sub	r6, r1, #52
	ldrh	r10, [r1]
.L2090:
	uxth	r1, r5
	cmp	r1, ip
	bcs	.L2128
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
	bne	.L2049
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
.L2049:
	add	r5, r5, #1
	b	.L2090
.L2128:
	cmp	fp, #0
	beq	.L2052
	ldr	r0, [r4, #-540]
	mov	r1, fp
	mov	r2, #1
	bl	FlashReadPages
	mov	r3, #0
.L2126:
	str	r3, [sp, #8]
	ldrh	r3, [sp, #8]
	cmp	r3, fp
	bcs	.L2052
	ldr	r3, [sp, #8]
	mov	r9, #36
	mul	r9, r9, r3
	ldr	r3, [r4, #-540]
	add	r2, r3, r9
	ldr	r3, [r3, r9]
	ldr	r5, [r2, #4]
	cmn	r3, #1
	ldr	r6, [r2, #12]
	ubfx	r5, r5, #10, #16
	bne	.L2055
	mov	r10, #16
	movw	ip, #65535
.L2054:
	ldr	r0, [r4, #-540]
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
	ldreq	r3, [r8, #-540]
	mvneq	r2, #0
	streq	r2, [r3, r9]
	beq	.L2055
.L2056:
	ldr	r3, [r7, #-540]
	ldr	r3, [r3, r9]
	cmn	r3, #1
	bne	.L2055
	sub	r10, r10, #1
	uxth	r10, r10
	cmp	r10, #0
	bne	.L2054
.L2055:
	ldr	r3, [r7, #-540]
	ldr	r3, [r3, r9]
	cmn	r3, #1
	beq	.L2058
	ldr	r2, [r7, #-3336]
	ldr	r3, [r6, #4]
	cmn	r2, #1
	beq	.L2059
	cmp	r2, r3
	bhi	.L2060
.L2059:
	cmn	r3, #1
	addne	r2, r3, #1
	strne	r2, [r8, #-3336]
.L2060:
	ldrh	r2, [r6]
	movw	r1, #61604
	cmp	r2, r1
	beq	.L2062
	bhi	.L2063
	movw	r3, #61574
	cmp	r2, r3
	bne	.L2061
	ldr	r2, .L2133+24
	ldr	lr, [r8, #-464]
	ldrh	ip, [r2]
	ldrh	r1, [r2, #26]
	sub	r0, ip, #1
	rsb	r2, r1, r0
	str	r2, [sp, #12]
	uxth	r3, r0
	b	.L2076
.L2063:
	movw	r3, #61634
	cmp	r2, r3
	beq	.L2065
	movw	r3, #65535
	cmp	r2, r3
	moveq	r0, r5
	beq	.L2127
	b	.L2061
.L2065:
	ldr	r3, [sp, #16]
	ldr	ip, .L2133+28
	ldr	r9, [r4, #-448]
	ldr	lr, [r3, #2412]
	ldrh	r2, [ip]
	uxth	r1, lr
	sub	r3, r1, #1
	rsb	r1, r2, r1
	sub	r1, r1, #1
	uxth	r3, r3
	sxth	r1, r1
	str	r1, [sp, #20]
.L2067:
	ldr	r1, [sp, #20]
	sxth	r0, r3
	cmp	r0, r1
	ble	.L2129
	mov	r10, r0, asl #2
	ldr	r1, [r6, #4]
	str	r10, [sp, #12]
	ldr	r10, [r9, r0, asl #2]
	cmp	r1, r10
	bls	.L2068
	ldr	r1, [r9]
	cmp	r1, #0
	bne	.L2069
	cmp	r2, lr
	addne	r2, r2, #1
	strneh	r2, [ip]	@ movhi
.L2069:
	uxth	r9, r3
	mov	r1, #0
.L2070:
	uxth	r2, r1
	cmp	r2, r9
	bcs	.L2130
	ldr	lr, [r7, #-448]
	sxth	r2, r2
	add	r1, r1, #1
	add	r10, lr, r2, asl #2
	ldr	r10, [r10, #4]
	str	r10, [lr, r2, asl #2]
	mov	r2, r2, asl #1
	ldr	lr, [r7, #-476]
	add	r10, lr, r2
	ldrh	r10, [r10, #2]
	strh	r10, [lr, r2]	@ movhi
	b	.L2070
.L2130:
	ldr	r2, [r4, #-448]
	mov	r0, r0, asl #1
	ldr	r1, [r6, #4]
	ldr	lr, [sp, #12]
	str	r1, [r2, lr]
	ldr	r2, [r4, #-476]
	strh	r5, [r2, r0]	@ movhi
	sxth	r0, r3
	cmp	r0, #0
	bge	.L2072
	b	.L2061
.L2068:
	sub	r3, r3, #1
	uxth	r3, r3
	b	.L2067
.L2129:
	cmp	r0, #0
	bge	.L2103
	b	.L2061
.L2072:
	ldr	r1, .L2133
	ldrh	r2, [ip]
	ldr	r1, [r1, #2412]
	rsb	r1, r2, r1
	sub	r1, r1, #1
	sxth	r1, r1
	cmp	r0, r1
	bgt	.L2061
.L2103:
	add	r2, r2, #1
	ldr	r1, [r6, #4]
	strh	r2, [ip]	@ movhi
	sxth	r3, r3
	ldr	r2, [r4, #-448]
	str	r1, [r2, r3, asl #2]
	mov	r3, r3, asl #1
	ldr	r2, [r4, #-476]
	b	.L2124
.L2082:
	ldr	r9, [lr, r2, asl #2]
	mov	r10, r2, asl #2
	ldr	r0, [r6, #4]
	cmp	r0, r9
	bhi	.L2131
	sub	r3, r3, #1
	uxth	r3, r3
.L2076:
	ldr	r0, [sp, #12]
	sxth	r2, r3
	cmp	r2, r0
	bgt	.L2082
	b	.L2081
.L2131:
	ldr	r0, [lr]
	cmp	r0, #0
	bne	.L2078
	cmp	r1, ip
	addne	r1, r1, #1
	ldrne	r0, .L2133+32
	strneh	r1, [r0]	@ movhi
.L2078:
	uxth	lr, r3
	mov	r0, #0
.L2079:
	uxth	r1, r0
	cmp	r1, lr
	bcs	.L2132
	ldr	ip, [r4, #-464]
	sxth	r1, r1
	add	r0, r0, #1
	add	r9, ip, r1, asl #2
	ldr	r9, [r9, #4]
	str	r9, [ip, r1, asl #2]
	mov	r1, r1, asl #1
	ldr	ip, .L2133
	ldr	ip, [ip, #2432]
	add	r9, ip, r1
	ldrh	r9, [r9, #2]
	strh	r9, [ip, r1]	@ movhi
	b	.L2079
.L2132:
	ldr	r1, [r8, #-464]
	mov	r2, r2, asl #1
	ldr	r0, [r6, #4]
	str	r0, [r1, r10]
	ldr	r1, .L2133
	ldr	r1, [r1, #2432]
	strh	r5, [r1, r2]	@ movhi
.L2081:
	sxth	r3, r3
	cmp	r3, #0
	blt	.L2061
	ldr	r0, .L2133+32
	ldrh	r2, [r0, #-26]
	ldrh	r1, [r0]
	sub	r2, r2, #1
	rsb	r2, r1, r2
	sxth	r2, r2
	cmp	r3, r2
	bgt	.L2061
	add	r1, r1, #1
	ldr	r2, [r8, #-464]
	strh	r1, [r0]	@ movhi
	ldr	r1, [r6, #4]
	str	r1, [r2, r3, asl #2]
	mov	r3, r3, asl #1
	ldr	r2, .L2133
	ldr	r2, [r2, #2432]
.L2124:
	strh	r5, [r2, r3]	@ movhi
	b	.L2061
.L2062:
	ldr	r2, .L2133+8
	movw	r1, #65535
	ldrh	r0, [r2]
	cmp	r0, r1
	streqh	r5, [r2]	@ movhi
	beq	.L2125
	ldrh	r0, [r2, #4]
	cmp	r0, r1
	beq	.L2084
	mov	r1, #1
	bl	FtlFreeSysBlkQueueIn
.L2084:
	ldr	r3, [r6, #4]
	ldr	r2, [r8, #-3296]
	cmp	r2, r3
	ldr	r3, .L2133+8
	strcsh	r5, [r3, #4]	@ movhi
	bcs	.L2061
	ldrh	r2, [r3]
	strh	r5, [r3]	@ movhi
	strh	r2, [r3, #4]	@ movhi
	ldr	r3, [r6, #4]
.L2125:
	str	r3, [r7, #-3296]
	b	.L2061
.L2058:
	ldr	r3, .L2133
	mov	r0, r5
	ldrb	r1, [r3, #140]	@ zero_extendqisi2
	cmp	r1, #0
	beq	.L2086
.L2127:
	mov	r1, #0
.L2086:
	bl	FtlFreeSysBlkQueueIn
.L2061:
	ldr	r3, [sp, #8]
	add	r3, r3, #1
	b	.L2126
.L2052:
	ldr	r3, [sp, #4]
	add	r5, r3, #1
	uxth	r3, r5
	str	r3, [sp, #4]
	b	.L2048
.L2089:
	ldr	r3, .L2133+4
	ldr	ip, [r3, #-476]
	ldrh	r2, [ip]
	cmp	r2, #0
	beq	.L2091
.L2094:
	ldr	r0, [r0, #2432]
	ldr	r3, .L2133
	ldrh	r2, [r0]
	cmp	r2, #0
	beq	.L2092
	b	.L2115
.L2091:
	sub	r3, r3, #392
	ldrh	r3, [r3]
	cmp	r3, #0
	ldrne	lr, [r0, #2412]
	beq	.L2094
.L2095:
	uxth	r3, r2
	sxth	r1, r3
	cmp	r1, lr
	bcs	.L2094
	mov	r5, r1, asl #1
	add	r2, r2, #1
	ldrh	r5, [ip, r5]
	cmp	r5, #0
	beq	.L2095
	mov	r6, #0
.L2096:
	ldr	ip, [r0, #2412]
	sxth	r2, r3
	cmp	r2, ip
	bcs	.L2094
	ldr	lr, [r4, #-476]
	mov	ip, r2, asl #1
	rsb	r5, r1, r2
	add	r3, r3, #1
	ldrh	r8, [lr, ip]
	mov	r7, r5, asl #1
	uxth	r3, r3
	strh	r8, [lr, r7]	@ movhi
	ldr	lr, [r4, #-448]
	ldr	r2, [lr, r2, asl #2]
	str	r2, [lr, r5, asl #2]
	ldr	r2, [r4, #-476]
	strh	r6, [r2, ip]	@ movhi
	b	.L2096
.L2092:
	movw	r1, #2430
	ldrh	r1, [r3, r1]
	cmp	r1, #0
	movwne	r1, #2404
	ldrneh	ip, [r3, r1]
	beq	.L2115
.L2099:
	uxth	r3, r2
	sxth	r1, r3
	cmp	r1, ip
	bge	.L2115
	mov	lr, r1, asl #1
	add	r2, r2, #1
	ldrh	lr, [r0, lr]
	cmp	lr, #0
	beq	.L2099
	ldr	r0, .L2133
	mov	r7, #0
	ldr	r6, .L2133+24
.L2100:
	ldrh	ip, [r6]
	sxth	r2, r3
	cmp	r2, ip
	bge	.L2115
	ldr	lr, [r0, #2432]
	mov	ip, r2, asl #1
	rsb	r5, r1, r2
	add	r3, r3, #1
	ldrh	r9, [lr, ip]
	mov	r8, r5, asl #1
	uxth	r3, r3
	strh	r9, [lr, r8]	@ movhi
	ldr	lr, [r4, #-464]
	ldr	r2, [lr, r2, asl #2]
	str	r2, [lr, r5, asl #2]
	ldr	r2, [r0, #2432]
	strh	r7, [r2, ip]	@ movhi
	b	.L2100
.L2115:
	mov	r0, #0
	add	sp, sp, #36
	@ sp needed
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L2134:
	.align	2
.L2133:
	.word	.LANCHOR0
	.word	.LANCHOR2
	.word	.LANCHOR2-3304
	.word	.LANCHOR0+2326
	.word	.LANCHOR0+2316
	.word	.LANCHOR0+2396
	.word	.LANCHOR0+2404
	.word	.LANCHOR2-392
	.word	.LANCHOR0+2430
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
	ldr	r4, .L2164
	.pad #36
	sub	sp, sp, #36
	ldr	r6, .L2164+4
	sub	r7, r4, #3296
	ldr	r3, [r4, #-528]
	ldr	r0, [r4, #-3548]
	str	r3, [r4, #1756]
	ldr	r3, [r4, #-504]
	str	r3, [r4, #1760]
	movw	r3, #2324
	ldrh	r2, [r6, r3]
	mov	r2, r2, asl #1
	bl	ftl_memset
	ldrh	r0, [r7, #-8]
	movw	r3, #65535
	cmp	r0, r3
	bne	.L2136
.L2147:
	mvn	r0, #0
	b	.L2137
.L2136:
	mov	r1, #1
	bl	FtlGetLastWrittenPage
	ldr	r3, .L2164+8
	ldrh	r9, [r7, #-8]
	mov	r7, r4
	uxth	r5, r0
	add	r0, r0, #1
	strh	r0, [r3, #2]	@ movhi
.L2138:
	sxth	r3, r5
	cmp	r3, #0
	blt	.L2146
	sxth	fp, r9
	mov	r1, #1
	ldr	r0, .L2164+12
	mov	r2, r1
	orr	r3, r3, fp, asl #10
	str	r3, [r4, #1752]
	ldr	r3, [r4, #-528]
	str	r3, [r4, #1756]
	bl	FlashReadPages
	ldrb	r3, [r6]	@ zero_extendqisi2
	cmp	r3, #0
	beq	.L2139
	ldr	r8, [r7, #1760]
	ldr	r3, [r8, #12]
	cmp	r3, #0
	beq	.L2139
	ldr	r2, [r7, #1756]
	ldr	r10, .L2164+16
	str	r3, [sp, #28]
	mov	r0, r2
	str	r2, [sp, #24]
	ldrh	r1, [r10]
	bl	js_hash
	ldr	r3, [sp, #28]
	cmp	r3, r0
	beq	.L2139
	cmp	r5, #0
	ldr	r2, [sp, #24]
	bne	.L2140
	ldr	ip, .L2164+8
	ldrh	r1, [ip, #4]
	cmp	fp, r1
	beq	.L2140
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
	ldr	r0, .L2164+20
	str	r3, [sp, #16]
	ldr	r2, [r7, #1748]
	ldr	r3, [r7, #1752]
	bl	printk
	ldrh	r5, [r10, #-8]
	ldr	ip, [sp, #24]
	ldrh	r9, [ip, #4]
	b	.L2142
.L2140:
	mvn	r3, #0
	str	r3, [r4, #1748]
.L2139:
	ldr	r3, [r4, #1748]
	cmn	r3, #1
	beq	.L2142
	ldr	r3, [r7, #-528]
	ldr	r2, .L2164+24
	ldr	r3, [r3]
	cmp	r3, r2
	bne	.L2142
	ldr	r3, [r7, #-504]
	ldrh	r2, [r3]
	movw	r3, #61604
	cmp	r2, r3
	bne	.L2142
.L2146:
	ldr	r1, [r4, #1756]
	mov	r2, #48
	movw	r8, #2324
	ldr	r0, .L2164+28
	bl	ftl_memcpy
	ldrh	r2, [r6, r8]
	ldr	r1, [r4, #1756]
	ldr	r0, [r4, #-3548]
	add	r1, r1, #48
	mov	r2, r2, asl #1
	bl	ftl_memcpy
	ldrh	r2, [r6, r8]
	ldr	r1, [r4, #1756]
	add	r3, r2, #24
	ldr	r0, [r4, #-3372]
	ubfx	r3, r3, #1, #30
	mov	r2, r2, lsr #3
	add	r2, r2, #4
	ldr	r5, .L2164
	add	r1, r1, r3, asl #2
	ldr	r7, .L2164+4
	bl	ftl_memcpy
	movw	r3, #2428
	ldrh	r3, [r6, r3]
	cmp	r3, #0
	beq	.L2144
	ldrh	r2, [r7, r8]
	ldr	r1, [r5, #1756]
	ldr	r0, [r5, #-452]
	mov	r3, r2, lsr #3
	add	r3, r3, r2, asl #1
	movw	r2, #2420
	ldrh	r2, [r7, r2]
	add	r3, r3, #52
	ubfx	r3, r3, #2, #14
	mov	r2, r2, asl #2
	add	r1, r1, r3, asl #2
	bl	ftl_memcpy
	b	.L2144
.L2142:
	sub	r5, r5, #1
	uxth	r5, r5
	b	.L2138
.L2144:
	ldr	r2, [r4, #-3604]
	ldr	r3, .L2164+24
	ldr	r5, .L2164
	cmp	r2, r3
	bne	.L2147
	ldr	r7, .L2164+28
	movw	r1, #2338
	ldrb	r0, [r5, #-3594]	@ zero_extendqisi2
	sub	r8, r5, #3296
	ldrh	r1, [r6, r1]
	add	r3, r7, #300
	ldrh	r2, [r7, #8]
	cmp	r0, r1
	strh	r2, [r3, #6]	@ movhi
	ldr	r3, .L2164+4
	bne	.L2147
	add	r1, r3, #2384
	movw	r0, #2390
	ldrh	r0, [r3, r0]
	ldrh	r1, [r1]
	str	r2, [r5, #1784]
	mul	r1, r2, r1
	str	r1, [r3, #2444]
	mul	r1, r0, r1
	str	r1, [r3, #2424]
	add	r1, r3, #2448
	ldrh	r0, [r1, #6]
	ldr	r1, [r3, #2328]
	rsb	r0, r0, r1
	rsb	r0, r2, r0
	movw	r2, #2316
	ldrh	r1, [r3, r2]
	bl	__aeabi_uidiv
	sub	r3, r5, #3520
	add	r2, r7, #76
	sub	lr, r5, #3280
	sub	ip, r5, #3424
	strh	r0, [r8, #-12]	@ movhi
	mov	r8, ip
	ldrh	r0, [r7, #14]
	strh	r0, [r3, #-8]	@ movhi
	ldrh	r3, [r7, #16]
	mov	r1, r3, lsr #6
	and	r3, r3, #63
	strb	r3, [r5, #-3522]
	ldrb	r3, [r5, #-3593]	@ zero_extendqisi2
	strh	r1, [r7, #78]	@ movhi
	ldrh	r1, [r7, #18]
	strb	r3, [r5, #-3520]
	mvn	r3, #0
	strh	r3, [lr, #-8]	@ movhi
	mov	r3, #0
	strh	r3, [r2, #242]	@ movhi
	sub	r2, r5, #3472
	strb	r3, [r5, #-3282]
	strh	r1, [r2, #-8]	@ movhi
	ldrh	r1, [r7, #20]
	strb	r3, [r5, #-3280]
	str	r3, [r5, #-3348]
	mov	r6, r1, lsr #6
	and	r1, r1, #63
	strb	r1, [r5, #-3474]
	ldrb	r1, [r5, #-3592]	@ zero_extendqisi2
	strh	r6, [r2, #-6]	@ movhi
	mov	r6, r2
	strb	r1, [r5, #-3472]
	ldrh	r1, [r7, #22]
	strh	r1, [ip, #-8]	@ movhi
	ldrh	r1, [r7, #24]
	mov	r7, r1, lsr #6
	and	r1, r1, #63
	strb	r1, [r5, #-3426]
	ldrb	r1, [r5, #-3591]	@ zero_extendqisi2
	strh	r7, [r2, #42]	@ movhi
	mov	r7, lr
	strb	r1, [r5, #-3424]
	str	r3, [r5, #-3360]
	ldr	r1, [r5, #-3572]
	str	r3, [r5, #-3368]
	str	r3, [r5, #-3352]
	str	r3, [r5, #-3324]
	str	r3, [r5, #-3316]
	str	r3, [r5, #-3356]
	ldr	r3, [r5, #-3564]
	str	r1, [r5, #-3328]
	ldr	r1, [r5, #-3336]
	ldr	r2, [r4, #-3332]
	cmp	r3, r1
	strhi	r3, [r5, #-3336]
	ldr	r3, [r4, #-3568]
	cmp	r3, r2
	ldrhi	r2, .L2164
	strhi	r3, [r2, #-3332]
	movw	r3, #65535
	cmp	r0, r3
	beq	.L2150
	ldr	r0, .L2164+32
	bl	make_superblock
.L2150:
	ldrh	r2, [r6, #-8]
	movw	r3, #65535
	cmp	r2, r3
	beq	.L2151
	ldr	r0, .L2164+36
	bl	make_superblock
.L2151:
	ldrh	r2, [r8, #-8]
	movw	r3, #65535
	cmp	r2, r3
	beq	.L2152
	ldr	r0, .L2164+40
	bl	make_superblock
.L2152:
	ldrh	r2, [r7, #-8]
	movw	r3, #65535
	cmp	r2, r3
	beq	.L2153
	ldr	r0, .L2164+44
	bl	make_superblock
.L2153:
	mov	r0, #0
.L2137:
	add	sp, sp, #36
	@ sp needed
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L2165:
	.align	2
.L2164:
	.word	.LANCHOR2
	.word	.LANCHOR0
	.word	.LANCHOR2-3304
	.word	.LANCHOR2+1748
	.word	.LANCHOR0+2394
	.word	.LC107
	.word	1179929683
	.word	.LANCHOR2-3604
	.word	.LANCHOR2-3528
	.word	.LANCHOR2-3480
	.word	.LANCHOR2-3432
	.word	.LANCHOR2-3288
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
	ldr	r8, .L2180
	ldr	r7, .L2180+4
	add	r5, r8, #2384
	ldr	r1, .L2180+8
	ldrh	r9, [r5]
	mov	r6, r0
	ldr	r0, .L2180+12
	bl	printk
	ldr	r2, [r7, #-3548]
	mov	r3, r6, asl #1
	mov	r1, r6
	ldr	r0, .L2180+16
	ldrh	r2, [r2, r3]
	bl	printk
	add	r0, sp, #96
	strh	r6, [r0, #-48]!	@ movhi
	bl	make_superblock
	ldrb	r2, [r8, #140]	@ zero_extendqisi2
	clz	r3, r4
	cmp	r2, #0
	mov	r3, r3, lsr #5
	moveq	r3, #0
	cmp	r3, #0
	moveq	r4, r3
	str	r7, [sp, #44]
	beq	.L2167
	mov	r0, r6
	bl	ftl_get_blk_mode
	cmp	r0, #1
	mov	r4, r0
	movweq	r3, #2386
	ldreqh	r9, [r8, r3]
.L2167:
	ldr	r0, .L2180+20
	mov	r1, r4
	mov	r2, r9
	ldrh	r3, [r5]
	bl	printk
	ldr	r8, .L2180+4
	mov	r6, #0
.L2168:
	ldr	r3, .L2180+24
	add	r0, sp, #62
	movw	lr, #65535
	mov	r10, #36
	ldrh	r3, [r3]
	mov	r7, r3
	ldr	r3, [r8, #-540]
	str	r3, [sp, #28]
	ldr	r3, [r8, #-2700]
	str	r3, [sp, #32]
	ldr	r3, .L2180+28
	ldrh	r2, [r3]
	ldrh	ip, [r3, #2]
	str	r2, [sp, #36]
	ldr	r2, [r8, #-2696]
	str	r2, [sp, #40]
	mov	r2, #0
	mov	r5, r2
.L2169:
	uxth	r3, r2
	cmp	r3, r7
	bcs	.L2178
	ldrh	r3, [r0, #2]!
	cmp	r3, lr
	beq	.L2170
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
.L2170:
	add	r2, r2, #1
	b	.L2169
.L2178:
	ldr	r0, [r8, #-540]
	mov	r1, r5
	mov	r2, r4
	mov	r10, #0
	bl	FlashReadPages
	mov	fp, #36
.L2172:
	uxth	r3, r10
	cmp	r3, r5
	bcs	.L2179
	ldr	r3, [sp, #44]
	mul	r2, fp, r10
	ldrh	r1, [sp, #48]
	ldr	lr, [r3, #-540]
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
	ldr	r0, .L2180+32
	str	r3, [sp, #20]
	ldr	r2, [lr, r2]
	ldr	r3, [ip, #4]
	bl	printk
	b	.L2172
.L2179:
	add	r6, r6, #1
	uxth	r6, r6
	cmp	r6, r9
	bne	.L2168
.L2174:
	mov	r0, #0
	add	sp, sp, #100
	@ sp needed
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L2181:
	.align	2
.L2180:
	.word	.LANCHOR0
	.word	.LANCHOR2
	.word	.LANCHOR3+16
	.word	.LC108
	.word	.LC109
	.word	.LC110
	.word	.LANCHOR0+2316
	.word	.LANCHOR0+2394
	.word	.LC111
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
	ldr	r0, .L2197
	mov	r7, #0
	ldr	r1, .L2197+4
	bl	printk
	ldr	r5, .L2197+8
	mov	r6, r5
.L2183:
	ldr	r3, .L2197+12
	uxth	r4, r7
	ldrh	r3, [r3]
	cmp	r3, r4
	bls	.L2193
	add	r8, sp, #88
	mov	r0, r4
	movw	r9, #65535
	mov	r10, #36
	strh	r4, [r8, #-48]!	@ movhi
	bl	ftl_get_blk_mode
	ldr	ip, [r5, #-3548]
	mov	r2, r4, asl #1
	mov	r1, r4
	ldrh	r2, [ip, r2]
	mov	r3, r0
	ldr	r0, .L2197+16
	bl	printk
	mov	r0, r8
	bl	make_superblock
	ldr	r3, .L2197+20
	ldr	lr, [r5, #-2696]
	ldrh	r2, [r3]
	ldrh	ip, [r3, #78]
	ldrh	r8, [r3, #80]
	str	r2, [sp, #28]
	add	r0, sp, #54
	ldr	r2, [r5, #-540]
	str	r2, [sp, #32]
	ldr	r2, [r5, #-2700]
	str	r2, [sp, #36]
	mov	r2, #0
	mov	r4, r2
.L2184:
	ldr	r1, [sp, #28]
	uxth	r3, r2
	cmp	r3, r1
	bcs	.L2194
	ldrh	r3, [r0, #2]!
	cmp	r3, r9
	beq	.L2185
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
.L2185:
	add	r2, r2, #1
	b	.L2184
.L2194:
	ldr	r0, [r6, #-540]
	mov	r1, r4
	mov	r2, #0
	mov	r8, #0
	bl	FlashReadPages
	mov	r9, #36
.L2187:
	uxth	r3, r8
	cmp	r3, r4
	bcs	.L2195
	mul	r2, r9, r8
	ldr	lr, [r6, #-540]
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
	ldr	r0, .L2197+24
	str	r3, [sp, #20]
	ldr	r2, [lr, r2]
	ldr	r3, [ip, #4]
	bl	printk
	b	.L2187
.L2195:
	ldr	r0, [r6, #-540]
	mov	r1, r4
	mov	r2, #1
	mov	r8, #0
	bl	FlashReadPages
	mov	r9, #36
.L2189:
	uxth	r3, r8
	cmp	r3, r4
	bcs	.L2196
	mul	r2, r9, r8
	ldr	lr, [r6, #-540]
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
	ldr	r0, .L2197+28
	str	r3, [sp, #20]
	ldr	r2, [lr, r2]
	ldr	r3, [ip, #4]
	bl	printk
	b	.L2189
.L2196:
	add	r7, r7, #1
	b	.L2183
.L2193:
	mov	r0, #0
	add	sp, sp, #92
	@ sp needed
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L2198:
	.align	2
.L2197:
	.word	.LC108
	.word	.LANCHOR3+36
	.word	.LANCHOR2
	.word	.LANCHOR0+2326
	.word	.LC112
	.word	.LANCHOR0+2316
	.word	.LC113
	.word	.LC114
	.fnend
	.size	FtlScanAllBlock, .-FtlScanAllBlock
	.align	2
	.global	ftl_scan_all_ppa
	.type	ftl_scan_all_ppa, %function
ftl_scan_all_ppa:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	ldr	r3, .L2206
	movw	r2, #2382
	stmfd	sp!, {r4, r5, r6, r7, r8, r9, lr}
	.save {r4, r5, r6, r7, r8, r9, lr}
	add	r7, r3, #2384
	ldrh	r1, [r3, r2]
	.pad #28
	sub	sp, sp, #28
	ldr	r0, .L2206+4
	sub	r9, r7, #2
	ldrh	r2, [r7]
	mov	r6, #0
	bl	printk
	ldr	r4, .L2206+8
.L2200:
	ldrh	r3, [r9]
	cmp	r6, r3
	bcs	.L2202
	mov	r8, r6, asl #10
	mov	r5, #0
.L2203:
	ldrh	r3, [r7]
	cmp	r5, r3
	bcs	.L2205
	add	r3, r5, r8
	str	r3, [r4, #1752]
	ldr	r3, [r4, #-528]
	mov	r2, #0
	mov	r1, #1
	ldr	r0, .L2206+12
	str	r2, [r4, #1748]
	add	r5, r5, #1
	str	r3, [r4, #1756]
	ldr	r3, [r4, #-504]
	str	r3, [r4, #1760]
	bl	FlashReadPages
	ldr	r3, [r4, #1760]
	ldr	r2, [r4, #1756]
	ldr	r0, .L2206+16
	ldr	r1, [r3, #4]
	str	r1, [sp]
	ldr	r1, [r3, #8]
	str	r1, [sp, #4]
	ldr	r1, [r3, #12]
	str	r1, [sp, #8]
	ldr	r1, [r2]
	str	r1, [sp, #12]
	ldr	r2, [r2, #4]
	ldr	r1, [r4, #1752]
	str	r2, [sp, #16]
	ldr	r2, [r4, #1748]
	ldr	r3, [r3]
	bl	printk
	b	.L2203
.L2205:
	add	r6, r6, #1
	b	.L2200
.L2202:
	ldr	r0, .L2206+20
	ldr	r1, .L2206+24
	add	sp, sp, #28
	@ sp needed
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, lr}
	b	printk
.L2207:
	.align	2
.L2206:
	.word	.LANCHOR0
	.word	.LC115
	.word	.LANCHOR2
	.word	.LANCHOR2+1748
	.word	.LC116
	.word	.LC117
	.word	.LANCHOR3+52
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
	ldr	r5, .L2265
	mov	r4, r0
	str	r1, [sp, #8]
	mov	r9, r2
	str	r3, [sp, #20]
	ldr	ip, [r5, #1204]
	ldrb	r7, [r5]	@ zero_extendqisi2
	ldrb	ip, [ip, #19]	@ zero_extendqisi2
	cmp	r7, #0
	str	ip, [sp, #16]
	ldr	ip, .L2265+4
	ldrb	ip, [ip, #481]	@ zero_extendqisi2
	str	ip, [sp, #12]
	beq	.L2209
	bl	FlashProgSlc2KPages
	b	.L2210
.L2223:
	mov	r3, #36
	add	r2, sp, #28
	mul	r8, r3, r7
	ldr	r3, [sp, #8]
	mov	r1, r9
	rsb	r3, r7, r3
	add	fp, r4, r8
	uxtb	r3, r3
	str	r3, [sp]
	mov	r0, fp
	add	r3, sp, #32
	bl	LogAddr2PhyAddr
	ldrb	r2, [r5, #2229]	@ zero_extendqisi2
	ldr	r3, [sp, #32]
	mov	r10, r0
	cmp	r3, r2
	mvncs	r3, #0
	strcs	r3, [r4, r8]
	bcc	.L2262
.L2212:
	add	r7, r7, #1
.L2209:
	ldr	r3, [sp, #8]
	ldr	r6, .L2265
	cmp	r7, r3
	bcc	.L2223
	b	.L2263
.L2262:
	ldrb	r1, [r5, #2305]	@ zero_extendqisi2
	add	r3, r5, r3, asl #4
	cmp	r1, #0
	ldr	r3, [r3, #2108]
	moveq	r10, #0
	cmp	r3, #0
	beq	.L2214
	cmp	r2, #1
	bne	.L2215
	ldr	r0, [r5, #76]
	bl	NandcIqrWaitFlashReady
.L2215:
	ldrb	r0, [sp, #32]	@ zero_extendqisi2
	bl	FlashWaitCmdDone
.L2214:
	ldr	r3, [sp, #32]
	mov	r1, #0
	cmp	r10, #0
	add	r2, r5, r3, asl #4
	str	r1, [r2, #2112]
	ldr	r1, [sp, #28]
	str	fp, [r2, #2108]
	str	r1, [r2, #2104]
	addne	r1, r8, #36
	addne	r1, r4, r1
	strne	r1, [r2, #2112]
	add	r2, r5, r3
	add	r3, r5, r3, asl #4
	ldrb	r6, [r2, #2232]	@ zero_extendqisi2
	strb	r6, [r3, #2100]
	mov	r0, r6
	ldrb	r3, [r5, #2229]	@ zero_extendqisi2
	cmp	r3, #1
	bne	.L2217
	bl	NandcWaitFlashReady
	b	.L2218
.L2217:
	bl	NandcFlashCs
	mov	r0, r6
	ldr	r3, [sp, #32]
	ldr	r1, [sp, #28]
	add	r3, r5, r3, asl #2
	ldr	r2, [r3, #1168]
	adds	r2, r2, #0
	movne	r2, #1
	bl	FlashWaitReadyEN
	mov	r0, r6
	bl	NandcFlashDeCs
.L2218:
	ldr	r3, [sp, #16]
	sub	r3, r3, #1
	cmp	r3, #7
	bhi	.L2219
	add	r3, r5, r6
	ldrb	r3, [r3, #2064]	@ zero_extendqisi2
	cmp	r3, #0
	beq	.L2219
	mov	r0, r6
	ldrb	r1, [r5, #1211]	@ zero_extendqisi2
	ldr	r2, .L2265+8
	mov	r3, #0
	bl	HynixSetRRPara
.L2219:
	mov	r0, r6
	bl	NandcFlashCs
	cmp	r9, #1
	mov	r0, r6
	bne	.L2220
	ldrb	r3, [r5, #140]	@ zero_extendqisi2
	cmp	r3, #0
	beq	.L2220
	bl	flash_enter_slc_mode
	b	.L2221
.L2220:
	bl	flash_exit_slc_mode
.L2221:
	mov	r0, r6
	ldr	r1, [sp, #28]
	bl	FlashProgFirstCmd
	ldr	r3, [fp, #12]
	mov	r0, r6
	mov	r1, #1
	str	r3, [sp]
	ldr	r2, [sp, #12]
	ldr	r3, [fp, #8]
	bl	NandcXferData
	cmp	r10, #0
	beq	.L2222
	mov	r0, r6
	ldr	r1, [sp, #28]
	bl	FlashProgDpFirstCmd
	mov	r0, r6
	add	r8, r8, #36
	add	r8, r4, r8
	ldr	r3, [sp, #32]
	ldr	r1, [sp, #28]
	add	r3, r5, r3, asl #2
	ldr	r2, [r3, #1168]
	adds	r2, r2, #0
	movne	r2, #1
	bl	FlashWaitReadyEN
	ldr	r1, [r5, #4]
	mov	r0, r6
	ldr	r3, [sp, #28]
	add	r1, r1, r3
	bl	FlashProgDpSecondCmd
	ldr	r3, [r8, #12]
	mov	r0, r6
	mov	r1, #1
	str	r3, [sp]
	ldr	r2, [sp, #12]
	ldr	r3, [r8, #8]
	bl	NandcXferData
.L2222:
	mov	r0, r6
	ldr	r1, [sp, #28]
	bl	FlashProgSecondCmd
	mov	r0, r6
	bl	NandcFlashDeCs
	add	r7, r7, r10
	b	.L2212
.L2263:
	ldr	r0, [r6, #76]
	mov	r5, #0
	bl	NandcIqrWaitFlashReady
	ldr	r8, .L2265+12
	mov	r7, r6
.L2224:
	ldrb	r3, [r6, #2229]	@ zero_extendqisi2
	cmp	r5, r3
	bcs	.L2264
	uxtb	r0, r5
	bl	FlashWaitCmdDone
	cmp	r9, #1
	bne	.L2225
	ldrb	r3, [r7, #140]	@ zero_extendqisi2
	cmp	r3, #0
	beq	.L2225
	ldrb	r0, [r8, r5, asl #4]	@ zero_extendqisi2
	bl	flash_exit_slc_mode
.L2225:
	add	r5, r5, #1
	b	.L2224
.L2264:
	ldr	r3, [sp, #20]
	cmp	r3, #0
	bne	.L2227
.L2235:
	mov	r0, #0
	b	.L2210
.L2227:
	ldr	r5, .L2265+16
	mov	r6, #0
	mov	r7, r5
.L2228:
	ldr	r3, [sp, #8]
	cmp	r6, r3
	beq	.L2235
	ldr	r3, [r4]
	cmn	r3, #1
	bne	.L2229
	ldr	r1, [r4, #4]
	ldr	r0, .L2265+20
	bl	printk
	b	.L2230
.L2229:
	ldr	r3, [sp, #8]
	mov	r1, r9
	add	r2, sp, #28
	mov	r0, r4
	rsb	r3, r6, r3
	uxtb	r3, r3
	str	r3, [sp]
	add	r3, sp, #32
	bl	LogAddr2PhyAddr
	ldr	r2, [r5, #1720]
	mov	r3, #0
	mov	lr, r4
	str	r3, [r2]
	ldr	r2, [r5, #1724]
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
	ldr	r3, [r5, #1720]
	str	r3, [sp, #44]
	ldr	r3, [r5, #1724]
	str	r3, [sp, #48]
	bl	FlashReadPages
	ldr	r8, [sp, #36]
	cmn	r8, #1
	bne	.L2231
	ldr	r0, .L2265+24
	ldr	r1, [r4, #4]
	bl	printk
	str	r8, [r4]
.L2231:
	ldr	r3, [r4, #12]
	cmp	r3, #0
	beq	.L2232
	ldr	r2, [r3]
	ldr	r3, [r7, #1724]
	ldr	r3, [r3]
	cmp	r2, r3
	beq	.L2232
	ldr	r0, .L2265+28
	ldr	r1, [r4, #4]
	bl	printk
	mvn	r3, #0
	str	r3, [r4]
.L2232:
	ldr	r3, [r4, #8]
	cmp	r3, #0
	beq	.L2230
	ldr	r2, [r3]
	ldr	r3, [r7, #1720]
	ldr	r3, [r3]
	cmp	r2, r3
	beq	.L2230
	ldr	r0, .L2265+32
	ldr	r1, [r4, #4]
	bl	printk
	mvn	r3, #0
	str	r3, [r4]
.L2230:
	add	r6, r6, #1
	add	r4, r4, #36
	b	.L2228
.L2210:
	add	sp, sp, #76
	@ sp needed
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L2266:
	.align	2
.L2265:
	.word	.LANCHOR0
	.word	.LANCHOR1
	.word	.LANCHOR0+1214
	.word	.LANCHOR0+2100
	.word	.LANCHOR2
	.word	.LC103
	.word	.LC104
	.word	.LC105
	.word	.LC106
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
	ldr	r5, .L2271
	ldr	r3, [r5, #1704]
	cmp	r0, r3
	movcc	r4, #0
	bcc	.L2268
	ldr	r3, [r5, #1712]
	mov	r4, r0
	mov	r1, #165
	add	r0, sp, #40
	mov	r2, #32
	str	r0, [sp, #16]
	str	r3, [sp, #12]
	bl	ftl_memset
	mov	r1, #90
	mov	r2, #8
	ldr	r0, [r5, #1712]
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
.L2268:
	mov	r0, r4
	add	sp, sp, #108
	@ sp needed
	ldmfd	sp!, {r4, r5, pc}
.L2272:
	.align	2
.L2271:
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
	ldr	r5, .L2328
	mov	r1, #1
	ldr	r4, .L2328+4
	ldr	r0, .L2328+8
	ldr	r3, [r5, #1716]
	ldrh	r8, [r4, #124]
	str	r3, [sp, #20]
	ldrh	r3, [r4, #126]
	smulbb	r8, r8, r3
	ldr	r3, [r4, #1204]
	ldrb	r3, [r3, #24]	@ zero_extendqisi2
	uxth	r8, r8
	str	r3, [sp, #24]
	ldrh	r3, [r4, #4]
	str	r3, [sp, #16]
	ldrb	r3, [r4]	@ zero_extendqisi2
	ldr	r4, .L2328+4
	cmp	r3, #1
	ldreq	r3, [sp, #16]
	moveq	r3, r3, asl #1
	uxtheq	r3, r3
	streq	r3, [sp, #16]
	bl	printk
	ldr	r0, [r5, #1716]
	mov	r1, #0
	mov	r2, #4096
	bl	ftl_memset
	ldr	r5, .L2328
	mov	r3, r8, lsr #4
	str	r3, [sp, #28]
	mov	r3, #0
	str	r3, [sp, #8]
.L2275:
	ldrb	r7, [sp, #8]	@ zero_extendqisi2
	ldrb	r3, [r4, #2229]	@ zero_extendqisi2
	cmp	r3, r7
	bls	.L2324
	ldr	r2, .L2328+12
	mov	r3, r7, asl #1
	ldrh	r6, [r2, r3]
	cmp	r6, #0
	bne	.L2304
	ldrh	r2, [r4, #132]
	mov	r1, r6
	ldr	r0, [r5, #1684]
	mov	r9, r6
	mov	r2, r2, asl #9
	bl	ftl_memset
	add	r3, r4, r7
	ldrb	r10, [r3, #2232]	@ zero_extendqisi2
	mov	r3, r7, asl #2
	add	fp, r4, r3
	str	r6, [sp, #4]
	str	r3, [sp, #32]
.L2277:
	ldrh	r3, [sp, #4]
	cmp	r3, r8
	str	r3, [sp, #12]
	bcs	.L2286
	mvn	r3, #0
	strb	r3, [sp, #42]
	strb	r3, [sp, #43]
	ldr	r3, [sp, #24]
	tst	r3, #1
	beq	.L2279
	ldr	r3, [fp, #1168]
	add	r2, sp, #42
	mov	r0, r10
	add	r3, r6, r3
	str	r3, [sp, #36]
	mov	r1, r3
	bl	FlashReadSpare
	ldrb	r2, [r4]	@ zero_extendqisi2
	cmp	r2, #1
	ldr	r3, [sp, #36]
	bne	.L2279
	ldr	r1, [r4, #4]
	add	r2, sp, #43
	mov	r0, r10
	add	r1, r3, r1
	bl	FlashReadSpare
	ldrb	r3, [sp, #42]	@ zero_extendqisi2
	ldrb	r2, [sp, #43]	@ zero_extendqisi2
	and	r3, r3, r2
	strb	r3, [sp, #42]
.L2279:
	ldr	r3, [sp, #24]
	tst	r3, #2
	beq	.L2281
	ldr	r3, [r4, #1204]
	mov	r0, r10
	add	r2, sp, #43
	ldrh	r1, [r3, #10]
	ldr	r3, [fp, #1168]
	sub	r1, r1, #1
	add	r1, r1, r3
	add	r1, r1, r6
	bl	FlashReadSpare
.L2281:
	ldr	r3, [r4, #1204]
	ldrb	r3, [r3, #7]	@ zero_extendqisi2
	cmp	r3, #1
	cmpne	r3, #8
	ldrb	r3, [sp, #42]	@ zero_extendqisi2
	bne	.L2282
	cmp	r3, #0
	ldrneb	r0, [sp, #43]	@ zero_extendqisi2
	clzne	r0, r0
	movne	r0, r0, lsr #5
	bne	.L2283
	b	.L2303
.L2282:
	cmp	r3, #255
	bne	.L2303
	ldrb	r0, [sp, #43]	@ zero_extendqisi2
	subs	r0, r0, #255
	movne	r0, #1
	b	.L2283
.L2303:
	mov	r0, #1
.L2283:
	ldr	r3, [sp, #24]
	tst	r3, #4
	beq	.L2284
	ldr	r3, .L2328+4
	mov	r0, r10
	ldr	r2, [sp, #32]
	add	r3, r3, r2
	ldr	r1, [r3, #1168]
	add	r1, r6, r1
	bl	SandiskProgTestBadBlock
.L2284:
	cmp	r0, #0
	beq	.L2285
	mov	r1, r7
	ldr	r2, [sp, #4]
	ldr	r0, .L2328+16
	add	r9, r9, #1
	bl	printk
	ldr	r1, [r5, #1684]
	mov	ip, #1
	uxth	r9, r9
	ldr	r3, [sp, #12]
	mov	r0, r3, lsr #5
	and	r3, r3, #31
	ldr	r2, [r1, r0, asl #2]
	orr	r3, r2, ip, asl r3
	ldr	r2, [sp, #28]
	str	r3, [r1, r0, asl #2]
	ldrb	r3, [r4, #2229]	@ zero_extendqisi2
	mul	r3, r3, r2
	cmp	r9, r3
	bgt	.L2286
.L2285:
	ldr	r3, [sp, #4]
	add	r3, r3, #1
	str	r3, [sp, #4]
	ldr	r3, [sp, #16]
	add	r6, r6, r3
	b	.L2277
.L2286:
	mov	r2, r9
	ldr	r0, .L2328+20
	mov	r1, r7
	bl	printk
	ldrb	r3, [r4, #2229]	@ zero_extendqisi2
	ldr	r2, [sp, #28]
	mul	r3, r3, r2
	cmp	r9, r3
	blt	.L2288
	ldrh	r2, [r4, #132]
	mov	r1, #0
	ldr	r0, [r5, #1684]
	mov	r2, r2, asl #9
	bl	ftl_memset
.L2288:
	cmp	r7, #0
	bne	.L2290
	ldr	r3, [r5, #1704]
	mov	r6, r7
	mov	r9, #1
	uxth	r10, r3
.L2291:
	ldr	r3, .L2328+4
	ldrb	r3, [r3, #1]	@ zero_extendqisi2
	cmp	r3, r10
	bls	.L2325
	mov	r0, r10
	bl	FlashTestBlk
	cmp	r0, #0
	beq	.L2292
	mov	r1, r10
	ldr	r0, .L2328+24
	bl	printk
	ldr	r1, [r5, #1684]
	mov	r0, r10, lsr #5
	add	r6, r6, #1
	and	r3, r10, #31
	ldr	r2, [r1, r0, asl #2]
	uxth	r6, r6
	orr	r3, r2, r9, asl r3
	str	r3, [r1, r0, asl #2]
.L2292:
	add	r10, r10, #1
	uxth	r10, r10
	b	.L2291
.L2325:
	sub	fp, r8, #1
	sub	r9, r8, #50
	mov	r10, #1
	uxth	fp, fp
.L2294:
	cmp	fp, r9
	ble	.L2326
	mov	r0, fp
	bl	FlashTestBlk
	cmp	r0, #0
	beq	.L2295
	mov	r1, fp
	ldr	r0, .L2328+24
	bl	printk
	ldr	r1, [r5, #1684]
	mov	r0, fp, lsr #5
	and	r3, fp, #31
	ldr	r2, [r1, r0, asl #2]
	orr	r3, r2, r10, asl r3
	str	r3, [r1, r0, asl #2]
.L2295:
	sub	fp, fp, #1
	uxth	fp, fp
	b	.L2294
.L2326:
	ldr	r3, .L2328+4
	ldr	r2, [r5, #1704]
	ldrb	r3, [r3, #1]	@ zero_extendqisi2
	rsb	r3, r2, r3
	cmp	r6, r3
	bcc	.L2290
	ldrh	r2, [r4, #132]
	mov	r1, #0
	ldr	r0, [r5, #1684]
	mov	r2, r2, asl #9
	bl	ftl_memset
.L2290:
	ldrb	r6, [sp, #8]	@ zero_extendqisi2
	sub	r10, r8, #1
	ldr	r9, .L2328+12
	uxth	r10, r10
	mul	r6, r8, r6
	add	r9, r9, r7, asl #1
.L2298:
	mov	r1, r7
	ldr	r0, .L2328+28
	mov	r2, r10
	bl	printk
	ldr	r1, [r5, #1684]
.L2299:
	mov	r2, r10, lsr #5
	and	r3, r10, #31
	ldr	r2, [r1, r2, asl #2]
	mov	r3, r2, lsr r3
	ands	r3, r3, #1
	subne	r10, r10, #1
	uxthne	r10, r10
	bne	.L2299
.L2327:
	ldr	r1, [sp, #20]
	add	r0, sp, #44
	ldr	r2, .L2328+32
	strh	r10, [r9]	@ movhi
	strh	r10, [r1, #2]	@ movhi
	strh	r2, [r1]	@ movhi
	strh	r3, [r1, #8]	@ movhi
	mov	r1, #1
	ldr	r3, [r5, #1684]
	mov	r2, r1
	str	r3, [sp, #52]
	ldr	r3, [r5, #1716]
	str	r3, [sp, #56]
	add	r3, r10, r6
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
	bne	.L2298
.L2304:
	ldr	r3, [sp, #8]
	add	r3, r3, #1
	str	r3, [sp, #8]
	b	.L2275
.L2324:
	add	sp, sp, #84
	@ sp needed
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L2329:
	.align	2
.L2328:
	.word	.LANCHOR2
	.word	.LANCHOR0
	.word	.LC118
	.word	.LANCHOR2+1732
	.word	.LC119
	.word	.LC120
	.word	.LC121
	.word	.LC122
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
	ldr	r5, .L2380
	ldr	r6, .L2380+4
	ldr	fp, [r5, #-3620]
	ldrb	r3, [r6, #2305]	@ zero_extendqisi2
	cmp	fp, #0
	str	r3, [sp, #16]
	movne	r0, #0
	bne	.L2331
	ldrb	r3, [r6, #140]	@ zero_extendqisi2
	mov	r10, r5
	mov	r8, fp
	mov	r5, fp
	mov	r4, fp
	mov	r9, r6
	mov	fp, r6
	mov	r7, #36
	str	r1, [sp]
	str	r0, [sp, #4]
	str	r3, [sp, #8]
	str	r0, [r10, #-552]
.L2332:
	ldr	r3, .L2380+8
	uxth	r1, r8
	ldr	ip, .L2380+4
	ldrh	r0, [r3]
	cmp	r0, r1
	bls	.L2375
	mul	r0, r7, r1
	ldr	ip, [r10, #-3616]
	add	r1, fp, r1
	mov	r3, #0
	str	r3, [ip, r0]
	ldrb	r0, [r1, #2344]	@ zero_extendqisi2
	ldr	r1, [sp, #4]
	bl	V2P_block
	ldr	r3, [sp]
	mov	r6, r0
	cmp	r3, #0
	beq	.L2333
	bl	IsBlkInVendorPart
	cmp	r0, #0
	bne	.L2334
.L2333:
	mov	r0, r6
	bl	FtlBbmIsBadBlock
	cmp	r0, #0
	addne	r4, r4, #1
	uxthne	r4, r4
	bne	.L2334
	ldr	r3, .L2380+12
	mov	r6, r6, asl #10
	ldr	ip, [r10, #-3616]
	ldrh	r1, [r3]
	mla	ip, r7, r5, ip
	mul	r1, r1, r5
	add	r5, r5, #1
	uxth	r5, r5
	cmp	r1, #0
	str	r0, [ip, #8]
	add	r0, r1, #3
	str	r6, [ip, #4]
	movlt	r1, r0
	ldr	r0, [r10, #-500]
	bic	r1, r1, #3
	add	r1, r0, r1
	str	r1, [ip, #12]
.L2334:
	add	r8, r8, #1
	b	.L2332
.L2375:
	cmp	r5, #0
	beq	.L2355
	ldr	r3, [sp, #8]
	mov	r2, r5
	ldr	r0, [r10, #-3616]
	mov	r7, #0
	adds	r6, r3, #0
	strb	r7, [ip, #2305]
	str	ip, [sp, #8]
	mov	r8, #36
	movne	r6, #1
	mov	r1, r6
	bl	FlashEraseBlocks
	ldrb	r3, [sp, #16]	@ zero_extendqisi2
	ldr	ip, [sp, #8]
	strb	r3, [ip, #2305]
.L2338:
	uxth	r2, r7
	cmp	r2, r5
	bcs	.L2376
	mul	r2, r8, r7
	ldr	r1, [r10, #-3616]
	add	r0, r1, r2
	ldr	r2, [r1, r2]
	cmn	r2, #1
	bne	.L2339
	ldr	r0, [r0, #4]
	add	r4, r4, #1
	ubfx	r0, r0, #10, #16
	uxth	r4, r4
	bl	FtlBbmMapBadBlock
.L2339:
	add	r7, r7, #1
	b	.L2338
.L2376:
	ldr	r3, [sp]
	cmp	r3, #0
	bne	.L2341
	mov	r3, #6
	uxth	r6, r6
	str	r3, [sp, #12]
	mov	r3, #1
	str	r3, [sp, #8]
	b	.L2342
.L2341:
	movw	r3, #2386
	ldrh	r3, [r9, r3]
	str	r3, [sp, #8]
	ldrb	r3, [r9, #140]	@ zero_extendqisi2
	cmp	r3, #0
	ldreq	r3, [sp, #8]
	moveq	r6, #1
	movne	r6, #1
	strne	r6, [sp, #12]
	moveq	r3, r3, lsr #2
	streq	r3, [sp, #12]
.L2342:
	ldr	r10, .L2380
	mov	r7, #0
	mov	r8, r10
.L2351:
	mov	fp, #0
	mov	r5, fp
.L2343:
	ldr	r3, .L2380+8
	ldr	r9, .L2380+4
	ldrh	r1, [r3]
	uxth	r3, fp
	cmp	r1, r3
	bls	.L2377
	mov	r2, #36
	ldr	r0, [r10, #-3616]
	mul	r1, r2, r3
	mov	r2, #0
	str	r2, [r0, r1]
	ldr	r2, .L2380+4
	ldr	r1, [sp, #4]
	add	r3, r2, r3
	ldrb	r0, [r3, #2344]	@ zero_extendqisi2
	bl	V2P_block
	ldr	r3, [sp]
	mov	r9, r0
	cmp	r3, #0
	beq	.L2344
	bl	IsBlkInVendorPart
	cmp	r0, #0
	bne	.L2345
.L2344:
	mov	r0, r9
	bl	FtlBbmIsBadBlock
	cmp	r0, #0
	bne	.L2345
	ldr	r1, [r8, #-3616]
	mov	r3, #36
	add	r9, r7, r9, asl #10
	mla	r1, r3, r5, r1
	ldr	r3, [r8, #-512]
	str	r3, [r1, #8]
	ldr	r3, .L2380+12
	str	r9, [r1, #4]
	ldrh	r3, [r3]
	mul	r3, r3, r5
	add	r5, r5, #1
	uxth	r5, r5
	add	r0, r3, #3
	cmp	r3, #0
	movlt	r3, r0
	ldr	r0, [r8, #-508]
	bic	r3, r3, #3
	add	r3, r0, r3
	str	r3, [r1, #12]
.L2345:
	add	fp, fp, #1
	b	.L2343
.L2377:
	cmp	r5, #0
	beq	.L2355
	mov	r2, r6
	mov	r3, #1
	ldr	r0, [r10, #-3616]
	mov	r1, r5
	mov	fp, #0
	strb	fp, [r9, #2305]
	bl	FlashProgPages
	mov	r2, #36
	ldrb	r3, [sp, #16]	@ zero_extendqisi2
	strb	r3, [r9, #2305]
	ldr	r9, .L2380
.L2348:
	uxth	r3, fp
	cmp	r3, r5
	bcs	.L2378
	mul	r3, r2, fp
	ldr	r1, [r8, #-3616]
	add	r0, r1, r3
	ldr	r3, [r1, r3]
	cmp	r3, #0
	beq	.L2349
	ldr	r0, [r0, #4]
	add	r4, r4, #1
	str	r2, [sp, #20]
	ubfx	r0, r0, #10, #16
	uxth	r4, r4
	bl	FtlBbmMapBadBlock
	ldr	r2, [sp, #20]
.L2349:
	add	fp, fp, #1
	b	.L2348
.L2378:
	ldr	r3, [sp, #12]
	add	r7, r7, r3
	ldr	r3, [sp, #8]
	uxth	r7, r7
	cmp	r7, r3
	bcc	.L2351
	mov	r7, #0
	mov	r8, #36
.L2352:
	uxth	r3, r7
	cmp	r3, r5
	ldr	r3, [sp]
	bcs	.L2379
	cmp	r3, #0
	beq	.L2353
	mul	r3, r8, r7
	ldr	r2, [r9, #-3616]
	add	r1, r2, r3
	ldr	r3, [r2, r3]
	cmp	r3, #0
	bne	.L2353
	ldr	r0, [r1, #4]
	mov	r1, #1
	ubfx	r0, r0, #10, #16
	bl	FtlFreeSysBlkQueueIn
.L2353:
	add	r7, r7, #1
	b	.L2352
.L2379:
	adds	r3, r3, #0
	ldr	r2, [sp, #4]
	movne	r3, #1
	cmp	r2, #63
	orrls	r3, r3, #1
	cmp	r3, #0
	beq	.L2355
	ldr	r0, [r9, #-3616]
	mov	r1, r6
	mov	r2, r5
	bl	FlashEraseBlocks
.L2355:
	mov	r0, r4
.L2331:
	add	sp, sp, #28
	@ sp needed
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L2381:
	.align	2
.L2380:
	.word	.LANCHOR2
	.word	.LANCHOR0
	.word	.LANCHOR0+2316
	.word	.LANCHOR0+2396
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
	ldr	r4, .L2400
	ldr	r5, [r4, #-3620]
	cmp	r5, #0
	bne	.L2384
	ldr	r3, [r4, #-504]
	mov	r1, r5
	ldr	r2, .L2400+4
	sub	r9, r4, #440
	ldr	r0, [r4, #-528]
	str	r3, [r4, #1760]
	movw	r3, #2394
	ldrh	r2, [r2, r3]
	str	r0, [r4, #1756]
	bl	ftl_memset
	ldr	r6, .L2400+8
	sub	r7, r6, #134
.L2385:
	ldrh	r3, [r7]
	ldr	r8, .L2400+4
	cmp	r5, r3
	bge	.L2399
	ldrh	r2, [r9]
	ldr	r3, [r4, #1756]
	ldr	r1, [r6, #4]!
	mul	r0, r2, r5
	mov	r2, r2, asl #2
	add	r5, r5, #1
	add	r0, r3, r0, asl #2
	bl	ftl_memcpy
	b	.L2385
.L2399:
	ldr	r6, [r4, #1760]
	mov	r1, #255
	mov	r2, #16
	add	r5, r8, #2448
	ldr	fp, .L2400
	mov	r7, #0
	mov	r0, r6
	mov	r10, r7
	bl	ftl_memset
	ldr	r3, .L2400+12
	strh	r3, [r6]	@ movhi
	ldr	r3, [r8, #2456]
	str	r3, [r6, #4]
	ldrh	r3, [r5]
	strh	r3, [r6, #2]	@ movhi
	ldrh	r3, [r5, #4]
	strh	r3, [r6, #8]	@ movhi
	ldrh	r3, [r5, #6]
	strh	r3, [r6, #10]	@ movhi
	ldr	r3, [r8, #2312]
	strh	r3, [r6, #12]	@ movhi
.L2387:
	ldr	r3, [r4, #-528]
	mov	ip, #0
	ldrh	r1, [r5]
	ldrh	r2, [r5, #2]
	str	r3, [r4, #1756]
	ldr	r3, [r4, #-504]
	str	ip, [r4, #1748]
	str	ip, [sp, #12]
	str	r3, [r4, #1760]
	orr	r3, r2, r1, asl #10
	ldrh	r0, [r6, #10]
	str	r3, [r4, #1752]
	ldrh	r3, [r5, #4]
	str	r0, [sp]
	ldr	r0, .L2400+16
	bl	printk
	ldr	r9, .L2400+20
	ldrh	r2, [r5, #2]
	ldrh	r3, [r9, #-62]
	sub	r3, r3, #1
	cmp	r2, r3
	ldr	ip, [sp, #12]
	blt	.L2388
	ldr	r3, [r8, #2456]
	mov	r1, #1
	ldrh	r2, [r9]
	add	r3, r3, #1
	ldr	r0, [fp, #-3616]
	str	r3, [r8, #2456]
	str	r3, [r6, #4]
	ldrh	r3, [r9, #4]
	strh	r2, [r6, #8]	@ movhi
	strh	r2, [r9, #4]	@ movhi
	mov	r2, r1
	strh	r3, [r9]	@ movhi
	mov	r3, r3, asl #10
	str	r3, [fp, #1752]
	str	r3, [r0, #4]
	strh	ip, [r9, #2]	@ movhi
	bl	FlashEraseBlocks
.L2388:
	mov	r1, #1
	ldr	r0, .L2400+24
	mov	r3, r1
	mov	r2, r1
	bl	FlashProgPages
	ldrh	r3, [r5, #2]
	ldr	r9, .L2400
	add	r3, r3, #1
	strh	r3, [r5, #2]	@ movhi
	ldr	r3, [r4, #1748]
	cmn	r3, #1
	bne	.L2389
	add	r7, r7, #1
	ldr	r0, .L2400+28
	ldr	r1, [fp, #1752]
	uxth	r7, r7
	bl	printk
	cmp	r7, #3
	bls	.L2387
	ldr	r0, .L2400+32
	mov	r2, r7
	ldr	r1, [r9, #1752]
	bl	printk
	mov	r3, #1
	str	r3, [r9, #-3620]
	b	.L2384
.L2389:
	add	r10, r10, #1
	cmp	r10, #1
	beq	.L2387
	cmp	r3, #256
	beq	.L2387
.L2384:
	mov	r0, #0
	add	sp, sp, #20
	@ sp needed
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L2401:
	.align	2
.L2400:
	.word	.LANCHOR2
	.word	.LANCHOR0
	.word	.LANCHOR0+2472
	.word	-3887
	.word	.LC123
	.word	.LANCHOR0+2448
	.word	.LANCHOR2+1748
	.word	.LC124
	.word	.LC125
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
	ldr	r4, .L2457
	ldr	r3, [r4, #-3620]
	cmp	r3, #0
	bne	.L2403
	mov	r5, r0
	mov	r8, r4
.L2404:
	ldr	r3, .L2457+4
	ldr	r10, .L2457+8
	cmp	r5, r3
	ldrb	r2, [r5, #8]	@ zero_extendqisi2
	bne	.L2405
	sub	r3, r3, #100
	ldr	ip, [r8, #-2712]
	ldrh	r3, [r3]
	mul	lr, ip, r3
	mov	r0, r3, lsr #1
	add	r1, r0, #1
	add	r1, r1, lr, lsr #2
	ldr	lr, [r10, #2308]
	cmp	lr, #0
	uxth	r1, r1
	beq	.L2406
	ldr	lr, [r8, #-3312]
	cmp	lr, #39
	bhi	.L2406
	cmp	lr, #2
	bls	.L2431
	cmp	ip, #0
	movne	r3, #0
	andeq	r3, r3, #1
	cmp	r3, #0
	moveq	r1, r0
	beq	.L2406
	b	.L2431
.L2405:
	cmp	r2, #1
	bne	.L2431
	add	r3, r10, #2336
	ldrh	r3, [r3]
	cmp	r3, #1
	beq	.L2431
	ldrb	r3, [r10, #140]	@ zero_extendqisi2
	cmp	r3, #0
	bne	.L2431
	ldr	r3, .L2457+12
	ldr	r0, [r10, #2308]
	ldrh	r3, [r3]
	cmp	r0, #0
	mov	r1, r3, lsr #3
	beq	.L2406
	ldr	r0, [r8, #-3312]
	cmp	r0, #1
	rsbls	r3, r3, r3, asl #3
	ubfxls	r1, r3, #3, #16
.L2406:
	cmp	r1, #0
	subne	r1, r1, #1
	uxthne	r1, r1
	b	.L2407
.L2431:
	mov	r1, #0
.L2407:
	ldr	r0, .L2457+16
	bl	List_pop_index_node
	ldr	r2, .L2457+12
	ldr	fp, .L2457+20
	ldrh	r3, [r2]
	sub	r3, r3, #1
	strh	r3, [r2]	@ movhi
	ldrh	r3, [fp]
	uxth	r9, r0
	cmp	r3, r9
	bls	.L2404
	ldr	r3, [r8, #-3548]
	mov	r7, r9, asl #1
	ldrh	r6, [r3, r7]
	cmp	r6, #0
	bne	.L2404
	strh	r9, [r5]	@ movhi
	mov	r0, r5
	bl	make_superblock
	ldrb	r3, [r5, #7]	@ zero_extendqisi2
	cmp	r3, #0
	beq	.L2409
	ldr	r1, [r8, #-3616]
	mov	lr, #36
	ldrh	ip, [fp, #-8]
	add	r0, r5, #14
	str	lr, [sp, #8]
	mov	r3, r1
	mla	r2, lr, ip, r1
	mov	ip, r6
	str	r2, [sp, #4]
	b	.L2410
.L2409:
	ldr	r3, [r8, #-3548]
	b	.L2452
.L2410:
	ldr	r2, [sp, #4]
	cmp	r3, r2
	beq	.L2454
	str	ip, [r3, #8]
	movw	lr, #65535
	str	ip, [r3, #12]
	add	r3, r3, #36
	ldrh	r2, [r0, #2]!
	cmp	r2, lr
	movne	r2, r2, asl #10
	ldrne	lr, [sp, #8]
	mlane	lr, lr, r6, r1
	addne	r6, r6, #1
	uxthne	r6, r6
	strne	r2, [lr, #4]
	b	.L2410
.L2454:
	ldr	r3, .L2457+24
	ldr	r2, [r10, #2308]
	rsb	r3, r3, r5
	clz	r3, r3
	cmp	r2, #0
	mov	r3, r3, lsr #5
	moveq	r3, #0
	cmp	r3, #0
	beq	.L2413
	ldr	r3, [r8, #-3612]
	ldrh	r3, [r3, r7]
	cmp	r3, #40
	movhi	r3, #0
	strhib	r3, [r8, #-3520]
.L2413:
	ldrb	r3, [r5, #8]	@ zero_extendqisi2
	ldr	r1, [r4, #-3612]
	cmp	r3, #0
	ldr	r2, .L2457+28
	ldrh	r3, [r1, r7]
	bne	.L2414
	cmp	r3, #0
	str	r2, [sp, #4]
	ldrneh	r0, [r2]
	moveq	r3, #2
	addne	r3, r3, r0
	mov	r0, r9
	uxthne	r3, r3
	strh	r3, [r1, r7]	@ movhi
	ldr	r3, [r4, #-3328]
	mov	r1, #0
	add	r3, r3, #1
	str	r3, [r4, #-3328]
	bl	ftl_set_blk_mode
	ldr	r2, [sp, #4]
	b	.L2416
.L2414:
	add	r3, r3, #1
	strh	r3, [r1, r7]	@ movhi
	ldr	r1, [r4, #-3372]
	mov	r0, r9, lsr #5
	ldr	r3, [r4, #-3324]
	mov	ip, #1
	add	r3, r3, #1
	str	r3, [r4, #-3324]
	ldr	lr, [r1, r0, asl #2]
	and	r3, r9, #31
	orr	r3, lr, ip, asl r3
	str	r3, [r1, r0, asl #2]
.L2416:
	ldr	r3, [r4, #-3612]
	ldr	r1, [r4, #-3316]
	ldrh	r2, [r2]
	ldrh	r3, [r3, r7]
	ldr	r0, [r4, #-3328]
	cmp	r3, r1
	ldrh	r1, [fp]
	strhi	r3, [r8, #-3316]
	ldr	r3, [r4, #-3324]
	mla	r0, r0, r2, r3
	bl	__aeabi_uidiv
	ldr	r2, [r4, #-484]
	ldr	r3, [r2, #16]
	add	r3, r3, #1
	str	r3, [r2, #16]
	ldr	r2, [r4, #-3616]
	mov	r3, #36
	add	r1, r2, #4
	mla	r3, r3, r6, r2
	add	r3, r3, #40
	str	r0, [r4, #-3320]
.L2418:
	add	r1, r1, #36
	cmp	r1, r3
	ldrne	r2, [r1, #-36]
	bicne	r2, r2, #1020
	bicne	r2, r2, #3
	strne	r2, [r1, #-36]
	bne	.L2418
.L2455:
	ldrb	r3, [r10, #140]	@ zero_extendqisi2
	cmp	r3, #0
	beq	.L2420
	ldrb	r3, [r5, #8]	@ zero_extendqisi2
	mov	r2, r6
	ldr	r0, [r4, #-3616]
	cmp	r3, #1
	moveq	r1, #0
	movne	r1, #1
	bl	FlashEraseBlocks
.L2420:
	ldrb	r1, [r5, #8]	@ zero_extendqisi2
	mov	r2, r6
	ldr	r0, [r4, #-3616]
	mov	r10, #0
	bl	FlashEraseBlocks
	add	r2, r5, #16
	mov	fp, r10
	mov	r1, #36
.L2422:
	uxth	r3, r10
	cmp	r3, r6
	bcs	.L2456
	mul	r3, r1, r10
	ldr	ip, [r4, #-3616]
	add	r0, ip, r3
	ldr	r3, [ip, r3]
	cmn	r3, #1
	bne	.L2423
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
.L2423:
	add	r10, r10, #1
	add	r2, r2, #2
	b	.L2422
.L2456:
	cmp	fp, #0
	beq	.L2425
	mov	r0, r9
	bl	update_multiplier_value
	bl	FtlBbmTblFlush
.L2425:
	ldrb	r3, [r5, #7]	@ zero_extendqisi2
	cmp	r3, #0
	bne	.L2426
	ldr	r3, [r4, #-3548]
.L2452:
	mvn	r2, #0
	strh	r2, [r3, r7]	@ movhi
	b	.L2404
.L2426:
	ldr	r2, .L2457+32
	ldrh	r2, [r2]
	strh	r9, [r5]	@ movhi
	smulbb	r3, r2, r3
	mov	r2, #0
	strh	r2, [r5, #2]	@ movhi
	strb	r2, [r5, #6]
	ldr	r2, [r4, #-3336]
	ldr	r1, [r4, #-3548]
	uxth	r3, r3
	strh	r3, [r5, #4]	@ movhi
	str	r2, [r5, #12]
	add	r2, r2, #1
	str	r2, [r4, #-3336]
	ldrh	r2, [r5]
	mov	r2, r2, asl #1
	strh	r3, [r1, r2]	@ movhi
.L2403:
	mov	r0, #0
	add	sp, sp, #20
	@ sp needed
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L2458:
	.align	2
.L2457:
	.word	.LANCHOR2
	.word	.LANCHOR2-3432
	.word	.LANCHOR0
	.word	.LANCHOR2-3532
	.word	.LANCHOR2-3536
	.word	.LANCHOR0+2324
	.word	.LANCHOR2-3528
	.word	.LANCHOR0+2376
	.word	.LANCHOR0+2384
	.fnend
	.size	allocate_data_superblock, .-allocate_data_superblock
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
	ldr	r5, .L2473
	ldrh	r3, [r5, #-6]
	cmp	r3, #0
	beq	.L2461
	sub	r5, r5, #6
	mov	r8, r0
	mov	r7, #0
	mov	r9, r5
.L2460:
	ldr	r3, .L2473+4
	uxth	r2, r7
	ldrh	r3, [r3]
	cmp	r3, r2
	bls	.L2470
	ldr	r3, .L2473+8
	mov	r1, r8
	mov	r10, #0
	add	r3, r3, r2
	ldrb	r0, [r3, #2344]	@ zero_extendqisi2
	bl	V2P_block
	ldr	ip, .L2473+12
	mov	fp, r0
.L2462:
	ldrh	r3, [r5]
	uxth	r4, r10
	cmp	r3, r4
	bls	.L2471
	mov	r3, r4, asl #1
	add	r6, ip, r3
	ldrh	r3, [ip, r3]
	cmp	r3, fp
	bne	.L2463
	mov	r1, fp
	ldr	r0, .L2473+16
	str	ip, [sp, #4]
	bl	printk
	mov	r0, fp
	bl	FtlBbmMapBadBlock
	bl	FtlBbmTblFlush
	ldrh	r2, [r5]
	mov	r3, r6
	ldr	ip, [sp, #4]
.L2464:
	cmp	r4, r2
	ldrcch	r1, [r3, #2]
	addcc	r4, r4, #1
	uxthcc	r4, r4
	strcch	r1, [r3], #2	@ movhi
	bcc	.L2464
.L2472:
	sub	r2, r2, #1
	strh	r2, [r9]	@ movhi
.L2463:
	add	r10, r10, #1
	b	.L2462
.L2471:
	add	r7, r7, #1
	b	.L2460
.L2470:
	bl	FtlGcReFreshBadBlk
.L2461:
	mov	r0, #0
	add	sp, sp, #12
	@ sp needed
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L2474:
	.align	2
.L2473:
	.word	.LANCHOR2-2656
	.word	.LANCHOR0+2316
	.word	.LANCHOR0
	.word	.LANCHOR2-2660
	.word	.LC126
	.fnend
	.size	FtlGcFreeBadSuperBlk, .-FtlGcFreeBadSuperBlk
	.align	2
	.global	update_vpc_list
	.type	update_vpc_list, %function
update_vpc_list:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	ldr	r2, .L2485
	mov	r3, r0, asl #1
	stmfd	sp!, {r4, lr}
	.save {r4, lr}
	mov	r4, r0
	ldr	r1, [r2, #-3548]
	ldrh	r3, [r1, r3]
	cmp	r3, #0
	bne	.L2476
	sub	r1, r2, #3280
	ldrh	r0, [r1, #-8]
	cmp	r0, r4
	mvneq	r3, #0
	streqh	r3, [r1, #-8]	@ movhi
	beq	.L2478
	sub	r1, r2, #3520
	ldrh	r1, [r1, #-8]
	cmp	r1, r4
	beq	.L2484
	sub	r1, r2, #3472
	ldrh	r1, [r1, #-8]
	cmp	r1, r4
	beq	.L2484
	sub	r2, r2, #3424
	ldrh	r2, [r2, #-8]
	cmp	r2, r4
	beq	.L2484
.L2478:
	mov	r1, r4
	ldr	r0, .L2485+4
	bl	List_remove_node
	ldr	r2, .L2485+8
	mov	r0, r4
	ldrh	r3, [r2, #-4]
	sub	r3, r3, #1
	strh	r3, [r2, #-4]	@ movhi
	bl	free_data_superblock
	mov	r0, r4
	bl	FtlGcFreeBadSuperBlk
	mov	r0, #1
	ldmfd	sp!, {r4, pc}
.L2476:
	bl	List_update_data_list
.L2484:
	mov	r0, #0
	ldmfd	sp!, {r4, pc}
.L2486:
	.align	2
.L2485:
	.word	.LANCHOR2
	.word	.LANCHOR2-3552
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
	movw	r3, #65535
	cmp	r0, r3
	stmfd	sp!, {r4, r5, r6, lr}
	.save {r4, r5, r6, lr}
	mov	r4, r0
	beq	.L2488
	ldr	r5, .L2498
	mov	r6, r0, asl #1
	ldr	r3, [r5, #-3548]
	ldrh	r2, [r3, r6]
	cmp	r2, #0
	subne	r2, r2, #1
	strneh	r2, [r3, r6]	@ movhi
	bne	.L2488
	mov	r1, r4
	ldr	r0, .L2498+4
	bl	printk
	ldr	r3, [r5, #-3548]
	mov	r2, #32
	sub	r0, r5, #3536
	mov	r1, r4
	strh	r2, [r3, r6]	@ movhi
	bl	test_node_in_list
	cmp	r0, #0
	beq	.L2490
	mov	r1, r4
	sub	r0, r5, #3536
	bl	List_remove_node
	sub	r2, r5, #3520
	mov	r0, r4
	ldrh	r3, [r2, #-12]
	sub	r3, r3, #1
	strh	r3, [r2, #-12]	@ movhi
	bl	INSERT_DATA_LIST
	ldr	r3, [r5, #-3548]
	ldr	r0, .L2498+8
	mov	r1, r4
	ldrh	r2, [r3, r6]
	bl	printk
.L2490:
	mov	r0, r4
	bl	FtlGcRefreshBlock
	b	.L2493
.L2488:
	ldr	r5, .L2498+12
	movw	r3, #65535
	ldrh	r0, [r5]
	cmp	r0, r3
	streqh	r4, [r5]	@ movhi
	beq	.L2493
	cmp	r0, r4
	beq	.L2493
	bl	update_vpc_list
	strh	r4, [r5]	@ movhi
	adds	r0, r0, #0
	movne	r0, #1
	ldmfd	sp!, {r4, r5, r6, pc}
.L2493:
	mov	r0, #0
	ldmfd	sp!, {r4, r5, r6, pc}
.L2499:
	.align	2
.L2498:
	.word	.LANCHOR2
	.word	.LC127
	.word	.LC128
	.word	.LANCHOR2-384
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
	ldr	r5, .L2514
	ldr	r6, .L2514+4
	add	r3, r0, r3, asl #1
	ldr	r7, .L2514+8
	ldrh	r3, [r3, #16]
.L2504:
	movw	r1, #65535
	cmp	r3, r1
	bne	.L2513
.L2506:
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
	b	.L2504
.L2513:
	ldrb	r2, [r4, #8]	@ zero_extendqisi2
	cmp	r2, #1
	bne	.L2507
	ldrb	r3, [r5, #140]	@ zero_extendqisi2
	cmp	r3, #0
	bne	.L2507
	ldrh	r3, [r4, #2]
	mov	r3, r3, asl #1
	ldrh	r3, [r6, r3]
	cmp	r3, r1
	bne	.L2507
	ldrh	r3, [r4, #4]
	ldrh	r0, [r4]
	sub	r3, r3, #1
	strh	r3, [r4, #4]	@ movhi
	bl	decrement_vpc_count
	ldrh	r2, [r4, #4]
	cmp	r2, #0
	bne	.L2506
	ldrh	r3, [r4, #2]
	add	r3, r3, #1
	b	.L2512
.L2507:
	ldr	r3, .L2514
	ldrb	r1, [r3, #140]	@ zero_extendqisi2
	cmp	r1, #0
	ldmeqfd	sp!, {r3, r4, r5, r6, r7, pc}
	cmp	r2, #1
	ldmnefd	sp!, {r3, r4, r5, r6, r7, pc}
	movw	r2, #2386
	ldrh	r1, [r4, #2]
	ldrh	r2, [r3, r2]
	cmp	r1, r2
	ldmccfd	sp!, {r3, r4, r5, r6, r7, pc}
	ldr	r1, .L2514+12
	add	r3, r3, #2384
	ldrh	r2, [r4]
	ldrh	ip, [r4, #4]
	ldr	r0, [r1, #-3548]
	mov	r2, r2, asl #1
	ldrh	r1, [r0, r2]
	rsb	r1, ip, r1
	strh	r1, [r0, r2]	@ movhi
	ldrh	r3, [r3]
	mov	r2, #0
	strh	r2, [r4, #4]	@ movhi
.L2512:
	strh	r3, [r4, #2]	@ movhi
	strb	r2, [r4, #6]
	ldmfd	sp!, {r3, r4, r5, r6, r7, pc}
.L2515:
	.align	2
.L2514:
	.word	.LANCHOR0
	.word	.LANCHOR2-2624
	.word	.LANCHOR0+2316
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
	ldr	r7, .L2534
	ldr	r8, .L2534+4
	add	r3, r0, r3, asl #1
	ldr	r9, .L2534+8
	ldrh	r2, [r3, #16]
.L2517:
	movw	r1, #65535
	cmp	r2, r1
	ldr	r6, .L2534
	bne	.L2533
.L2518:
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
	b	.L2517
.L2533:
	ldrb	r3, [r4, #8]	@ zero_extendqisi2
	ldrh	r5, [r4, #2]
	cmp	r3, #1
	ldrh	r3, [r4, #4]
	bne	.L2520
	ldrb	r0, [r8, #140]	@ zero_extendqisi2
	cmp	r0, #0
	bne	.L2520
	mov	r0, r5, asl #1
	ldrh	r0, [r9, r0]
	cmp	r0, r1
	bne	.L2520
	sub	r3, r3, #1
	ldrh	r0, [r4]
	strh	r3, [r4, #4]	@ movhi
	bl	decrement_vpc_count
	b	.L2518
.L2520:
	ldr	r7, .L2534+4
	orr	r5, r5, r2, asl #10
	sub	r3, r3, #1
	strh	r3, [r4, #4]	@ movhi
.L2521:
	ldrb	r3, [r4, #6]	@ zero_extendqisi2
	movw	r2, #65535
	ldrh	r0, [r6]
.L2523:
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
	beq	.L2523
	strb	r3, [r4, #6]
	ldrb	r3, [r4, #8]	@ zero_extendqisi2
	cmp	r3, #1
	bne	.L2528
	ldrb	r3, [r7, #140]	@ zero_extendqisi2
	cmp	r3, #0
	bne	.L2525
	ldrh	r3, [r4, #2]
	ldr	r2, .L2534+8
	mov	r3, r3, asl #1
	ldrh	r2, [r2, r3]
	movw	r3, #65535
	cmp	r2, r3
	bne	.L2525
	ldrh	r3, [r4, #4]
	cmp	r3, #0
	beq	.L2525
	sub	r3, r3, #1
	ldrh	r0, [r4]
	strh	r3, [r4, #4]	@ movhi
	bl	decrement_vpc_count
	b	.L2521
.L2525:
	ldr	r3, .L2534+4
	ldrb	r2, [r3, #140]	@ zero_extendqisi2
	cmp	r2, #0
	beq	.L2528
	movw	r2, #2386
	ldrh	r1, [r4, #2]
	ldrh	r2, [r3, r2]
	cmp	r1, r2
	bcc	.L2528
	ldr	r1, .L2534+12
	add	r3, r3, #2384
	ldrh	r2, [r4]
	ldrh	ip, [r4, #4]
	ldr	r0, [r1, #-3548]
	mov	r2, r2, asl #1
	ldrh	r1, [r0, r2]
	rsb	r1, ip, r1
	strh	r1, [r0, r2]	@ movhi
	ldrh	r3, [r3]
	mov	r2, #0
	strh	r2, [r4, #4]	@ movhi
	strb	r2, [r4, #6]
	strh	r3, [r4, #2]	@ movhi
.L2528:
	mov	r0, r5
	ldmfd	sp!, {r3, r4, r5, r6, r7, r8, r9, pc}
.L2535:
	.align	2
.L2534:
	.word	.LANCHOR0+2316
	.word	.LANCHOR0
	.word	.LANCHOR2-2624
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
	ldr	r4, .L2554
	ldr	r3, [r4, #-3620]
	cmp	r3, #0
	bne	.L2538
	ldr	r2, [r4, #-528]
	mov	r1, #255
	ldr	r7, [r4, #-504]
	ldr	r6, .L2554+4
	str	r2, [r4, #1756]
	sub	r2, r4, #3296
	str	r7, [r4, #1760]
	ldrh	r2, [r2, #-8]
	str	r3, [r7, #12]
	ldr	r5, .L2554+8
	strh	r2, [r7, #2]	@ movhi
	ldr	r2, .L2554+12
	strh	r2, [r7]	@ movhi
	ldr	r2, [r4, #-3296]
	stmib	r7, {r2, r3}
	ldr	r3, .L2554+16
	ldrh	r2, [r6, #78]
	str	r3, [r4, #-3604]
	ldr	r3, .L2554+20
	str	r3, [r4, #-3600]
	add	r3, r6, #300
	ldrh	r3, [r3, #6]
	strh	r3, [r6, #8]	@ movhi
	movw	r3, #2338
	ldrh	r3, [r5, r3]
	strb	r3, [r4, #-3594]
	sub	r3, r4, #3520
	ldrh	r3, [r3, #-8]
	strh	r3, [r6, #14]	@ movhi
	ldrb	r3, [r4, #-3522]	@ zero_extendqisi2
	orr	r3, r3, r2, asl #6
	strh	r3, [r6, #16]	@ movhi
	ldrb	r3, [r4, #-3520]	@ zero_extendqisi2
	ldrh	r2, [r6, #126]
	strb	r3, [r4, #-3593]
	sub	r3, r4, #3472
	ldrh	r3, [r3, #-8]
	strh	r3, [r6, #18]	@ movhi
	ldrb	r3, [r4, #-3474]	@ zero_extendqisi2
	orr	r3, r3, r2, asl #6
	strh	r3, [r6, #20]	@ movhi
	ldrb	r3, [r4, #-3472]	@ zero_extendqisi2
	strb	r3, [r4, #-3592]
	sub	r3, r4, #3424
	ldrh	r3, [r3, #-8]
	strh	r3, [r6, #22]	@ movhi
	ldrh	r2, [r6, #174]
	ldrb	r3, [r4, #-3426]	@ zero_extendqisi2
	ldr	r0, [r4, #1756]
	orr	r3, r3, r2, asl #6
	strh	r3, [r6, #24]	@ movhi
	ldrb	r3, [r4, #-3424]	@ zero_extendqisi2
	strb	r3, [r4, #-3591]
	ldr	r3, [r4, #-3328]
	str	r3, [r4, #-3572]
	ldr	r3, [r4, #-3336]
	str	r3, [r4, #-3564]
	ldr	r3, [r4, #-3332]
	str	r3, [r4, #-3568]
	sub	r3, r4, #2656
	ldrh	r2, [r3, #-14]
	ldrh	r3, [r3, #-12]
	strh	r2, [r6, #44]	@ movhi
	strh	r3, [r6, #46]	@ movhi
	movw	r3, #2394
	ldrh	r2, [r5, r3]
	bl	ftl_memset
	mov	r1, r6
	mov	r2, #48
	movw	r6, #2324
	ldr	r0, [r4, #1756]
	bl	ftl_memcpy
	ldrh	r2, [r5, r6]
	ldr	r0, [r4, #1756]
	ldr	r1, [r4, #-3548]
	mov	r2, r2, asl #1
	add	r0, r0, #48
	bl	ftl_memcpy
	ldrh	r2, [r5, r6]
	ldr	r0, [r4, #1756]
	add	r3, r2, #24
	ldr	r1, [r4, #-3372]
	ubfx	r3, r3, #1, #30
	mov	r2, r2, lsr #3
	add	r2, r2, #4
	add	r0, r0, r3, asl #2
	bl	ftl_memcpy
	movw	r3, #2428
	ldrh	r3, [r5, r3]
	cmp	r3, #0
	beq	.L2539
	ldrh	r2, [r5, r6]
	ldr	r0, [r4, #1756]
	ldr	r1, [r4, #-456]
	mov	r3, r2, lsr #3
	add	r3, r3, r2, asl #1
	movw	r2, #2420
	add	r3, r3, #52
	ldrh	r2, [r5, r2]
	ubfx	r3, r3, #2, #14
	mov	r2, r2, asl #2
	add	r0, r0, r3, asl #2
	bl	ftl_memcpy
.L2539:
	mov	r0, #0
	ldr	r8, .L2554
	bl	FtlUpdateVaildLpn
	ldr	r10, .L2554+24
	mov	r6, #0
	movw	r9, #65535
.L2540:
	ldr	r3, [r4, #-528]
	ldrh	r2, [r10]
	ldr	fp, .L2554+28
	str	r3, [r4, #1756]
	ldr	r3, [r4, #-504]
	ldrh	r1, [r10, #2]
	str	r3, [r4, #1760]
	orr	r3, r1, r2, asl #10
	str	r3, [r4, #1752]
	ldrh	r3, [fp]
	sub	r3, r3, #1
	cmp	r1, r3
	blt	.L2541
	mov	r3, #0
	ldrh	r9, [r10, #4]
	strh	r3, [r10, #2]	@ movhi
	strh	r2, [r10, #4]	@ movhi
	bl	FtlFreeSysBlkQueueOut
	ldr	r3, [r8, #-3336]
	add	r2, r3, #1
	str	r2, [r8, #-3336]
	str	r3, [r8, #-3296]
	mov	r2, r0, asl #10
	strh	r0, [r10]	@ movhi
	str	r2, [r8, #1752]
	str	r3, [r7, #4]
	strh	r0, [r7, #2]	@ movhi
.L2541:
	ldrb	r3, [r5]	@ zero_extendqisi2
	cmp	r3, #0
	beq	.L2542
	ldr	r3, .L2554+32
	ldr	r0, [r4, #-528]
	ldrh	r1, [r3]
	bl	js_hash
	str	r0, [r7, #12]
.L2542:
	mov	r1, #1
	ldr	r0, .L2554+36
	mov	r2, r1
	mov	r3, r1
	bl	FlashProgPages
	ldr	r3, .L2554+24
	ldr	r2, .L2554+24
	ldrh	r3, [r3, #2]
	add	r3, r3, #1
	uxth	r3, r3
	strh	r3, [r2, #2]	@ movhi
	ldr	r2, [r4, #1748]
	cmn	r2, #1
	bne	.L2543
	cmp	r3, #1
	add	r6, r6, #1
	ldreqh	r3, [fp]
	uxth	r6, r6
	subeq	r3, r3, #1
	streqh	r3, [r10, #2]	@ movhi
	cmp	r6, #3
	bls	.L2540
	ldr	r0, .L2554+40
	mov	r2, r6
	ldr	r1, [r4, #1752]
	bl	printk
	mov	r3, #1
	str	r3, [r4, #-3620]
	b	.L2538
.L2543:
	cmp	r2, #256
	cmpne	r3, #1
	beq	.L2540
	movw	r3, #65535
	cmp	r9, r3
	beq	.L2538
	mov	r0, r9
	mov	r1, #1
	bl	FtlFreeSysBlkQueueIn
.L2538:
	mov	r0, #0
	ldmfd	sp!, {r3, r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L2555:
	.align	2
.L2554:
	.word	.LANCHOR2
	.word	.LANCHOR2-3604
	.word	.LANCHOR0
	.word	-3932
	.word	1179929683
	.word	1342177376
	.word	.LANCHOR2-3304
	.word	.LANCHOR0+2386
	.word	.LANCHOR0+2394
	.word	.LANCHOR2+1748
	.word	.LC129
	.fnend
	.size	FtlVpcTblFlush, .-FtlVpcTblFlush
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
	ldr	r5, .L2573
	ldr	r3, [r5, #-3620]
	cmp	r3, #0
	bne	.L2556
	ldr	r2, .L2573+4
	ldrb	r6, [r2, #140]	@ zero_extendqisi2
	cmp	r6, #0
	beq	.L2572
	ldrb	r6, [r0, #8]	@ zero_extendqisi2
	cmp	r6, #1
	ldreqh	r10, [r0, #4]
	beq	.L2558
	mov	r6, r3
.L2572:
	mov	r10, #12
.L2558:
	ldr	r7, [r5, #-504]
	mvn	r3, #0
	str	r3, [sp, #20]
	mov	r8, #0
	ldr	r3, [r5, #-528]
	movw	r2, #61589
	str	r7, [sp, #16]
	mov	r4, r0
	ldr	r9, .L2573
	str	r3, [sp, #12]
	mvn	r3, #2
	str	r3, [r7, #8]
	mvn	r3, #1
	str	r3, [r7, #12]
	ldrh	r3, [r0]
	strh	r8, [r7]	@ movhi
	strh	r3, [r7, #2]	@ movhi
	ldr	r3, [r5, #-528]
	str	r2, [r3]
	ldr	r2, .L2573+8
	ldr	r3, [r5, #-528]
	str	r2, [r3, #4]
.L2559:
	subs	r10, r10, #1
	bcc	.L2562
	ldrh	r3, [r4, #4]
	cmp	r3, #0
	bne	.L2560
.L2562:
	ldrh	r3, [r4]
	ldr	r1, [r5, #-3548]
	ldrh	r0, [r4, #4]
	mov	r3, r3, asl #1
	ldrh	r2, [r1, r3]
	rsb	r2, r0, r2
	strh	r2, [r1, r3]	@ movhi
	ldr	r3, .L2573+12
	ldrh	r3, [r3]
	strh	r3, [r4, #2]	@ movhi
	mov	r3, #0
	strb	r3, [r4, #6]
	strh	r3, [r4, #4]	@ movhi
	b	.L2556
.L2560:
	mov	r0, r4
	bl	get_new_active_ppa
	cmn	r0, #1
	str	r0, [sp, #8]
	beq	.L2562
	ldr	r3, [r5, #-3332]
	mov	r1, #1
	mov	r2, r6
	add	r0, sp, #4
	str	r3, [r7, #4]
	add	r3, r3, #1
	cmn	r3, #1
	moveq	r3, r8
	str	r3, [r9, #-3332]
	mov	r3, #0
	bl	FlashProgPages
	ldrh	r0, [r4]
	bl	decrement_vpc_count
	b	.L2559
.L2556:
	add	sp, sp, #40
	@ sp needed
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, pc}
.L2574:
	.align	2
.L2573:
	.word	.LANCHOR2
	.word	.LANCHOR0
	.word	305419896
	.word	.LANCHOR0+2384
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
	ldr	r8, .L2589
	cmp	r2, r3
	blt	.L2576
	uxth	r0, r0
	mov	r0, r0, asl #1
	ldrh	r10, [r5, r0]
	cmp	r10, #0
	beq	.L2576
	ldr	r3, [r4, #32]
	cmp	r3, #0
	bne	.L2576
	mov	r2, #1
	str	r2, [r4, #32]
	strh	r3, [r5, r0]	@ movhi
	ldrh	r3, [r4, #8]
	ldrh	r2, [r4, #2]
	sub	r3, r3, #1
	strh	r3, [r4, #8]	@ movhi
	movw	r3, #2386
	ldrh	r3, [r8, r3]
	cmp	r2, r3
	bcc	.L2577
	mov	r0, r4
	bl	ftl_map_blk_alloc_new_blk
.L2577:
	ldr	r5, .L2589+4
	mov	fp, #0
.L2578:
	ldrh	r3, [r4, #6]
	uxth	r6, fp
	cmp	r3, r6
	bls	.L2588
	ldr	r3, [r7, r6, asl #2]
	add	ip, r7, r6, asl #2
	cmp	r10, r3, lsr #10
	bne	.L2579
	ldr	r3, [r5, #-524]
	mov	r1, #1
	ldr	r9, [r5, #-504]
	mov	r2, r1
	ldr	r0, .L2589+8
	str	r3, [r5, #1756]
	str	r9, [r5, #1760]
	ldr	r3, [r7, r6, asl #2]
	str	ip, [sp, #4]
	str	r3, [r5, #1752]
	bl	FlashReadPages
	ldr	r3, [r5, #1748]
	cmn	r3, #1
	ldr	r3, .L2589+4
	ldr	ip, [sp, #4]
	bne	.L2580
.L2582:
	mov	r2, #0
	ldr	r0, .L2589+12
	str	r2, [ip]
	ldr	r1, [r3, #1752]
	ldrh	r2, [r9, #8]
	str	r3, [sp, #4]
	bl	printk
	mov	r2, #1
	ldr	r3, [sp, #4]
	str	r2, [r3, #-3620]
	b	.L2581
.L2580:
	ldrh	r1, [r9, #8]
	cmp	r1, r6
	bne	.L2582
	ldrh	r0, [r9]
	ldrh	r2, [r4, #4]
	cmp	r0, r2
	bne	.L2582
	mov	r0, r4
	ldr	r2, [r5, #1756]
	bl	FtlMapWritePage
.L2579:
	add	fp, fp, #1
	b	.L2578
.L2588:
	mov	r0, r10
	mov	r1, #1
	bl	FtlFreeSysBlkQueueIn
	mov	r3, #0
	str	r3, [r4, #32]
.L2576:
	movw	r3, #2386
	ldrh	r2, [r4, #2]
	ldrh	r3, [r8, r3]
	cmp	r2, r3
	bcc	.L2581
	mov	r0, r4
	bl	ftl_map_blk_alloc_new_blk
.L2581:
	mov	r0, #0
	add	sp, sp, #12
	@ sp needed
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L2590:
	.align	2
.L2589:
	.word	.LANCHOR0
	.word	.LANCHOR2
	.word	.LANCHOR2+1748
	.word	.LC130
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
	ldr	r5, .L2603
	ldr	r7, [r0, #12]
	ldr	r9, [r0, #24]
	ldr	r6, [r5, #-3620]
	cmp	r6, #0
	bne	.L2592
	ldrh	r3, [r0]
	movw	r2, #65535
	mov	r4, r0
	cmp	r3, r2
	bne	.L2593
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
	b	.L2592
.L2593:
	mov	r3, r3, asl #1
	ldr	r10, .L2603+4
	mov	r1, #255
	ldrh	r8, [r7, r3]
	ldrh	r3, [r0, #2]
	ldr	r7, [r5, #-504]
	orr	r3, r3, r8, asl #10
	str	r3, [r5, #1752]
	ldr	r3, [r5, #-528]
	str	r7, [r5, #1760]
	str	r3, [r5, #1756]
	ldr	r3, [r0, #28]
	str	r3, [r7, #4]
	ldr	r3, .L2603+8
	strh	r3, [r7, #8]	@ movhi
	ldrh	r3, [r0, #4]
	strh	r8, [r7, #2]	@ movhi
	strh	r3, [r7]	@ movhi
	movw	r3, #2386
	ldrh	r2, [r10, r3]
	ldr	r0, [r5, #-528]
	mov	r2, r2, asl #3
	bl	ftl_memset
	mov	r2, r6
.L2594:
	ldrh	r1, [r4, #6]
	uxth	r3, r2
	cmp	r1, r3
	bls	.L2602
	ldr	r1, [r9, r3, asl #2]
	cmp	r8, r1, lsr #10
	bne	.L2595
	add	r6, r6, #1
	ldr	r1, [r5, #-528]
	uxth	r6, r6
	str	r3, [r1, r6, asl #3]
	ldr	r1, [r9, r3, asl #2]
	ldr	r3, [r5, #-528]
	add	r3, r3, r6, asl #3
	str	r1, [r3, #4]
.L2595:
	add	r2, r2, #1
	b	.L2594
.L2602:
	ldrb	r3, [r10]	@ zero_extendqisi2
	cmp	r3, #0
	beq	.L2597
	ldr	r2, .L2603+4
	movw	r3, #2394
	ldr	r0, [r5, #1756]
	ldrh	r1, [r2, r3]
	bl	js_hash
	str	r0, [r7, #12]
.L2597:
	mov	r1, #1
	mov	r3, #0
	ldr	r0, .L2603+12
	mov	r2, r1
	bl	FlashProgPages
	ldrh	r3, [r4, #2]
	mov	r0, r4
	add	r3, r3, #1
	strh	r3, [r4, #2]	@ movhi
	bl	ftl_map_blk_gc
.L2592:
	mov	r0, #0
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, pc}
.L2604:
	.align	2
.L2603:
	.word	.LANCHOR2
	.word	.LANCHOR0
	.word	-1291
	.word	.LANCHOR2+1748
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
	ldr	r10, .L2624
	mov	r8, r1
	ldr	fp, .L2624+4
	mov	r5, #0
	str	r2, [sp]
	mov	r9, r10
.L2606:
	ldr	r3, [r10, #-3352]
	ldr	r6, .L2624
	add	r3, r3, #1
	str	r3, [r10, #-3352]
	ldrh	r3, [fp]
	ldrh	r2, [r4, #2]
	sub	r3, r3, #1
	cmp	r2, r3
	bge	.L2607
	ldrh	r2, [r4]
	movw	r3, #65535
	cmp	r2, r3
	bne	.L2608
.L2607:
	mov	r0, r4
	bl	Ftl_write_map_blk_to_last_page
.L2608:
	ldr	r1, [r9, #-3620]
	cmp	r1, #0
	bne	.L2609
	ldrh	r3, [r4]
	ldr	r2, [r4, #12]
	ldr	r0, [r10, #-504]
	mov	r3, r3, asl #1
	ldrh	r7, [r2, r3]
	mov	r2, #16
	ldrh	r3, [r4, #2]
	str	r0, [r10, #1760]
	orr	r3, r3, r7, asl #10
	str	r3, [r10, #1752]
	ldr	r3, [sp]
	str	r3, [r10, #1756]
	bl	ftl_memset
	ldr	r2, [r4, #28]
	ldr	r3, [r10, #1760]
	str	r2, [r3, #4]
	strh	r8, [r3, #8]	@ movhi
	ldrh	r2, [r4, #4]
	strh	r7, [r3, #2]	@ movhi
	strh	r2, [r3]	@ movhi
	ldr	r2, .L2624+8
	ldrb	r2, [r2]	@ zero_extendqisi2
	cmp	r2, #0
	beq	.L2610
	ldr	r2, .L2624+12
	ldr	r0, [r10, #1756]
	str	r3, [sp, #4]
	ldrh	r1, [r2]
	bl	js_hash
	ldr	r3, [sp, #4]
	str	r0, [r3, #12]
.L2610:
	mov	r1, #1
	ldr	r0, .L2624+16
	mov	r2, r1
	mov	r3, r1
	bl	FlashProgPages
	ldrh	r3, [r4, #2]
	add	r3, r3, #1
	uxth	r3, r3
	strh	r3, [r4, #2]	@ movhi
	ldr	r2, [r9, #1748]
	cmn	r2, #1
	bne	.L2611
	ldr	r0, .L2624+20
	add	r5, r5, #1
	ldr	r1, [r10, #1752]
	bl	printk
	ldrh	r3, [r4, #2]
	uxth	r5, r5
	cmp	r3, #2
	ldrls	r3, .L2624+4
	ldrlsh	r3, [r3]
	subls	r3, r3, #1
	strlsh	r3, [r4, #2]	@ movhi
	cmp	r5, #3
	bls	.L2606
	ldr	r0, .L2624+24
	mov	r2, r5
	ldr	r1, [r6, #1752]
	bl	printk
	mov	r3, #1
	str	r3, [r6, #-3620]
	b	.L2609
.L2611:
	cmp	r2, #0
	strneh	r7, [r4, #40]	@ movhi
	cmp	r2, #256
	cmpne	r3, #1
	beq	.L2615
	ldr	r3, [r4, #36]
	cmp	r3, #0
	beq	.L2616
.L2615:
	mov	r3, #0
	str	r3, [r4, #36]
	b	.L2606
.L2616:
	ldr	r2, [r6, #1752]
	ldr	r3, [r4, #24]
	str	r2, [r3, r8, asl #2]
.L2609:
	mov	r0, #0
	add	sp, sp, #12
	@ sp needed
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L2625:
	.align	2
.L2624:
	.word	.LANCHOR2
	.word	.LANCHOR0+2386
	.word	.LANCHOR0
	.word	.LANCHOR0+2394
	.word	.LANCHOR2+1748
	.word	.LC131
	.word	.LC132
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
	ldr	r5, .L2628
	mul	r4, r4, r0
	sub	r0, r5, #436
	ldr	r3, [r5, #-3384]
	add	r2, r3, r4
	ldrh	r1, [r3, r4]
	ldr	r2, [r2, #8]
	bl	FtlMapWritePage
	ldr	r3, [r5, #-3384]
	mov	r0, #0
	add	r4, r3, r4
	ldr	r3, [r4, #4]
	bic	r3, r3, #-2147483648
	str	r3, [r4, #4]
	ldmfd	sp!, {r3, r4, r5, pc}
.L2629:
	.align	2
.L2628:
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
	str	r3, [sp, #12]
	ldrh	r3, [r0, #8]
	mov	r2, r10, asl #2
	ldr	r9, [r0, #12]
	mov	r0, r8
	str	r3, [sp, #8]
	bl	ftl_memset
	ldr	r1, .L2673
	mov	r2, #1
	str	r2, [r4, #36]
	ldr	r3, [r1, #-528]
	mov	r5, r1
	ldr	r6, [r1, #-504]
	mov	r7, r1
	str	r3, [r1, #1756]
	mvn	r3, #0
	str	r6, [r1, #1760]
	strh	r3, [r4]	@ movhi
	strh	r3, [r4, #2]	@ movhi
	mov	r3, #0
	str	r3, [r4, #32]
	str	r3, [r4, #28]
	str	r3, [sp, #4]
.L2631:
	ldrh	r3, [sp, #4]
	ldr	r2, [sp, #8]
	sxth	fp, r3
	cmp	fp, r2
	bge	.L2649
	ldr	r2, [sp, #8]
	sub	r2, r2, #1
	cmp	fp, r2
	mov	r2, fp, asl #1
	bne	.L2632
	ldrh	r0, [r9, r2]
	mov	r1, #1
	str	r3, [sp, #8]
	add	r3, r9, r2
	str	r3, [sp, #4]
	bl	FtlGetLastWrittenPage
	mov	r7, #0
	ldr	r3, [sp, #8]
	add	r2, r0, #1
	strh	r2, [r4, #2]	@ movhi
	sxth	r0, r0
	add	r9, r0, #1
	strh	r3, [r4]	@ movhi
	ldr	r3, [sp, #12]
	ldr	r3, [r3, fp, asl #2]
	ldr	fp, .L2673
	str	r3, [r4, #28]
.L2633:
	sxth	r3, r7
	cmp	r3, r9
	bge	.L2649
	ldr	r2, [sp, #4]
	mov	r1, #1
	ldr	r0, .L2673+4
	ldrh	r2, [r2]
	orr	r3, r3, r2, asl #10
	mov	r2, r1
	str	r3, [r5, #1752]
	bl	FlashReadPages
	ldr	r3, .L2673+8
	ldrb	r3, [r3]	@ zero_extendqisi2
	cmp	r3, #0
	beq	.L2634
	ldr	r3, [fp, #1760]
	ldr	r3, [r3, #12]
	cmp	r3, #0
	beq	.L2634
	ldr	r2, .L2673+12
	ldr	r0, [fp, #1756]
	str	r3, [sp, #8]
	ldrh	r1, [r2]
	bl	js_hash
	ldr	r3, [sp, #8]
	cmp	r3, r0
	mvnne	r3, #0
	strne	r3, [fp, #1748]
.L2634:
	ldr	r3, [fp, #1748]
	cmn	r3, #1
	beq	.L2635
	ldrh	r3, [r6, #8]
	cmp	r3, r10
	bcs	.L2635
	ldrh	r2, [r4, #4]
	ldrh	r1, [r6]
	cmp	r1, r2
	ldreq	r2, [fp, #1752]
	streq	r2, [r8, r3, asl #2]
.L2635:
	add	r7, r7, #1
	b	.L2633
.L2649:
	mov	r0, r4
	bl	ftl_free_no_use_map_blk
	ldr	r1, .L2673+8
	movw	r3, #2386
	ldrh	r2, [r4, #2]
	ldrh	r3, [r1, r3]
	cmp	r2, r3
	bne	.L2638
	mov	r0, r4
	bl	ftl_map_blk_alloc_new_blk
	b	.L2638
.L2632:
	ldr	r3, [r5, #-528]
	mov	r1, #1
	ldr	fp, .L2673+16
	ldr	r0, .L2673+4
	str	r3, [r5, #1756]
	add	r3, r9, r2
	ldrh	r2, [r9, r2]
	str	r3, [sp, #16]
	ldrh	r3, [fp]
	sub	r3, r3, #1
	orr	r3, r3, r2, asl #10
	mov	r2, r1
	str	r3, [r5, #1752]
	bl	FlashReadPages
	ldr	r3, [r5, #1748]
	cmn	r3, #1
	mov	r3, fp
	beq	.L2651
	ldrh	r1, [r6]
	ldrh	r2, [r4, #4]
	cmp	r1, r2
	bne	.L2651
	ldrh	r1, [r6, #8]
	movw	r2, #64245
	cmp	r1, r2
	bne	.L2651
	mov	r0, #0
	mov	lr, #8
	mov	fp, #4
.L2640:
	uxth	r2, r0
	ldrh	ip, [r3]
	sxth	r1, r2
	sub	ip, ip, #1
	cmp	r1, ip
	bge	.L2643
	ldr	ip, [r5, #-528]
	add	r0, r0, #1
	ldr	r1, [ip, r1, asl #3]
	uxth	r1, r1
	cmp	r1, r10
	smlabbcc	r2, r2, lr, fp
	ldrcc	r2, [ip, r2]
	strcc	r2, [r8, r1, asl #2]
	b	.L2640
.L2651:
	mov	fp, #0
.L2671:
	ldr	r3, .L2673+16
	sxth	r2, fp
	ldrh	r1, [r3]
	cmp	r2, r1
	bge	.L2643
	str	r3, [sp, #20]
	ldr	r3, [sp, #16]
	ldr	r0, .L2673+4
	ldrh	r1, [r3]
	orr	r2, r2, r1, asl #10
	mov	r1, #1
	str	r2, [r7, #1752]
	mov	r2, r1
	bl	FlashReadPages
	ldr	r3, .L2673+8
	ldrb	r2, [r3]	@ zero_extendqisi2
	cmp	r2, #0
	ldr	r3, [sp, #20]
	beq	.L2644
	ldr	r2, [r7, #1760]
	ldr	r2, [r2, #12]
	cmp	r2, #0
	beq	.L2644
	ldrh	r1, [r3, #8]
	ldr	r0, [r7, #1756]
	str	r2, [sp, #20]
	bl	js_hash
	ldr	r2, [sp, #20]
	cmp	r2, r0
	mvnne	r3, #0
	strne	r3, [r7, #1748]
.L2644:
	ldr	r3, .L2673
	ldr	r3, [r3, #1748]
	cmn	r3, #1
	beq	.L2645
	ldrh	r3, [r6, #8]
	cmp	r3, r10
	bcs	.L2645
	ldrh	r2, [r4, #4]
	ldrh	r1, [r6]
	cmp	r1, r2
	ldreq	r2, [r7, #1752]
	streq	r2, [r8, r3, asl #2]
.L2645:
	add	fp, fp, #1
	b	.L2671
.L2643:
	ldr	r3, [sp, #4]
	add	r3, r3, #1
	str	r3, [sp, #4]
	b	.L2631
.L2638:
	mov	r0, r4
	bl	ftl_map_blk_gc
	mov	r0, r4
	bl	ftl_map_blk_gc
	mov	r0, #0
	add	sp, sp, #28
	@ sp needed
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L2674:
	.align	2
.L2673:
	.word	.LANCHOR2
	.word	.LANCHOR2+1748
	.word	.LANCHOR0
	.word	.LANCHOR0+2394
	.word	.LANCHOR0+2386
	.fnend
	.size	FtlMapTblRecovery, .-FtlMapTblRecovery
	.align	2
	.global	FtlLoadVonderInfo
	.type	FtlLoadVonderInfo, %function
FtlLoadVonderInfo:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	ldr	r2, .L2677
	movw	r1, #2404
	stmfd	sp!, {r3, lr}
	.save {r3, lr}
	ldr	r3, .L2677+4
	ldrh	r1, [r2, r1]
	add	r0, r3, #1776
	add	r0, r0, #12
	strh	r1, [r0, #10]	@ movhi
	ldr	r1, .L2677+8
	strh	r1, [r0, #4]	@ movhi
	movw	r1, #2430
	ldrh	r1, [r2, r1]
	strh	r1, [r0, #8]	@ movhi
	movw	r1, #2406
	ldrh	r1, [r2, r1]
	ldr	r2, [r2, #2432]
	strh	r1, [r0, #6]	@ movhi
	str	r2, [r3, #1800]
	ldr	r2, [r3, #-464]
	str	r2, [r3, #1804]
	ldr	r2, [r3, #-468]
	str	r2, [r3, #1808]
	ldr	r2, [r3, #-460]
	str	r2, [r3, #1812]
	bl	FtlMapTblRecovery
	mov	r0, #0
	ldmfd	sp!, {r3, pc}
.L2678:
	.align	2
.L2677:
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
	ldr	r0, .L2681
	bl	FtlMapTblRecovery
	mov	r0, #0
	ldmfd	sp!, {r3, pc}
.L2682:
	.align	2
.L2681:
	.word	.LANCHOR2-436
	.fnend
	.size	FtlLoadMapInfo, .-FtlLoadMapInfo
	.align	2
	.global	FtlVendorPartWrite
	.type	FtlVendorPartWrite, %function
FtlVendorPartWrite:
	.fnstart
	@ args = 0, pretend = 0, frame = 56
	@ frame_needed = 0, uses_anonymous_args = 0
	ldr	r3, .L2694
	stmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.save {r4, r5, r6, r7, r8, r9, r10, fp, lr}
	mov	r10, r2
	movw	r2, #2392
	mov	r4, r1
	ldrh	r5, [r3, r2]
	add	r2, r0, r1
	movw	r1, #2380
	mov	r6, r0
	ldrh	r3, [r3, r1]
	.pad #60
	sub	sp, sp, #60
	cmp	r2, r3
	mvnhi	r0, #0
	bhi	.L2684
	ldr	fp, .L2694+4
	mov	r5, r6, lsr r5
	mov	r3, r5, asl #2
	str	r3, [sp, #4]
	mov	r7, fp
	mov	r3, #0
	str	r3, [sp]
.L2685:
	cmp	r4, #0
	beq	.L2693
	ldr	r2, [sp, #4]
	mov	r0, r6
	ldr	r3, [fp, #-460]
	ldr	ip, [r3, r2]
	ldr	r3, .L2694+8
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
	beq	.L2687
	ldr	r2, [r7, #-520]
	mov	r1, #1
	add	r0, sp, #20
	str	ip, [sp, #24]
	str	r2, [sp, #28]
	mov	r2, #0
	str	r2, [sp, #32]
	mov	r2, r1
	bl	FlashReadPages
	b	.L2688
.L2687:
	ldr	r2, .L2694+12
	ldr	r0, [r7, #-520]
	ldrh	r2, [r2]
	bl	ftl_memset
.L2688:
	mov	ip, r9, asl #9
	ldr	r0, [r7, #-520]
	uxth	r8, r8
	mov	r1, r10
	mov	r2, ip
	str	ip, [sp, #8]
	add	r0, r0, r8, asl #9
	rsb	r4, r9, r4
	bl	ftl_memcpy
	mov	r1, r5
	ldr	r0, .L2694+16
	add	r5, r5, #1
	ldr	r2, [r7, #-520]
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
	b	.L2685
.L2693:
	ldr	r0, [sp]
.L2684:
	add	sp, sp, #60
	@ sp needed
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L2695:
	.align	2
.L2694:
	.word	.LANCHOR0
	.word	.LANCHOR2
	.word	.LANCHOR0+2390
	.word	.LANCHOR0+2394
	.word	.LANCHOR2+1788
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
	ldr	r3, .L2698
	ldr	r2, .L2698+4
	ldr	r1, [r3, #-3236]
	cmp	r1, r2
	bxne	lr
	ldr	r2, .L2698+8
	mov	r0, #0
	mov	r1, #1
	str	r2, [r3, #-3232]
	ldr	r2, [r3, #-3344]
	str	r2, [r3, #-3148]
	ldr	r2, [r3, #-3340]
	str	r2, [r3, #-3144]
	ldr	r2, [r3, #-3348]
	str	r2, [r3, #-3228]
	ldr	r2, [r3, #-3360]
	str	r2, [r3, #-3224]
	ldr	r2, [r3, #-3368]
	str	r2, [r3, #-3220]
	ldr	r2, [r3, #-3352]
	str	r2, [r3, #-3216]
	ldr	r2, [r3, #-3324]
	str	r2, [r3, #-3208]
	ldr	r2, [r3, #-3608]
	str	r2, [r3, #-3204]
	ldr	r2, [r3, #-3364]
	str	r2, [r3, #-3200]
	ldr	r2, [r3, #-3356]
	str	r2, [r3, #-3196]
	ldr	r2, [r3, #-3316]
	str	r2, [r3, #-3192]
	ldr	r2, [r3, #-3312]
	str	r2, [r3, #-3188]
	ldr	r2, [r3, #-3240]
	str	r2, [r3, #-3176]
	ldr	r2, [r3, #-568]
	str	r2, [r3, #-3172]
	ldr	r2, .L2698+12
	b	FtlVendorPartWrite
.L2699:
	.align	2
.L2698:
	.word	.LANCHOR2
	.word	1179929683
	.word	1342177376
	.word	.LANCHOR2-3236
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
	ldr	r3, .L2708
	ldr	r3, [r3, #2308]
	cmp	r3, #0
	ldr	r3, .L2708+4
	moveq	r2, #32
	beq	.L2701
	ldr	r2, [r3, #-3312]
	cmp	r2, #39
	movhi	r2, #32
	movls	r2, #4
.L2701:
	movw	ip, #1832
	ldrh	r1, [r3, ip]
	cmp	r1, #31
	addls	r1, r1, #1
	ldrls	r2, .L2708+4
	strlsh	r1, [r2, ip]	@ movhi
	movls	r2, #1
	cmp	r0, #0
	bne	.L2703
	ldr	r1, [r3, #-484]
	ldr	r0, [r1, #20]
	ldr	r1, [r1, #16]
	add	r2, r2, r0
	cmp	r1, r2
	bcc	.L2704
.L2703:
	ldr	r2, [r3, #-484]
	mov	r0, #64
	ldr	r1, [r2, #16]
	str	r1, [r2, #20]
	ldr	r1, .L2708+8
	str	r1, [r2]
	ldr	r2, [r3, #-484]
	ldr	r3, .L2708+12
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
.L2704:
	mov	r0, #0
	ldmfd	sp!, {r3, pc}
.L2709:
	.align	2
.L2708:
	.word	.LANCHOR0
	.word	.LANCHOR2
	.word	1112818501
	.word	.LANCHOR2-492
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
	movw	r3, #2392
	ldr	r4, .L2722
	mov	r8, r2
	add	r2, r0, r1
	mov	r7, r0
	.pad #60
	sub	sp, sp, #60
	mov	r6, r1
	ldrh	r5, [r4, r3]
	movw	r3, #2380
	ldrh	r3, [r4, r3]
	cmp	r2, r3
	mvnhi	r0, #0
	bhi	.L2711
	ldr	r9, .L2722+4
	mov	r5, r7, lsr r5
	mov	r3, r5, asl #2
	str	r3, [sp, #4]
	mov	r10, r9
	mov	r3, #0
	str	r3, [sp]
.L2712:
	cmp	r6, #0
	beq	.L2721
	ldr	r2, [sp, #4]
	mov	r0, r7
	ldr	r3, [r9, #-460]
	ldr	r3, [r3, r2]
	str	r3, [sp, #12]
	ldr	r3, .L2722+8
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
	beq	.L2714
	ldr	r2, [r10, #-520]
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
	ldr	r2, [r10, #1748]
	mvneq	r3, #0
	cmp	r2, #256
	str	r3, [sp]
	ldr	r3, [sp, #12]
	bne	.L2716
	mov	r1, r5
	mov	r2, r3
	ldr	r0, .L2722+12
	bl	printk
	ldr	r0, .L2722+16
	mov	r1, r5
	ldr	r2, [r9, #-520]
	bl	FtlMapWritePage
.L2716:
	ldrh	r3, [sp, #8]
	mov	r0, r8
	ldr	r1, [r10, #-520]
	mov	r2, fp
	add	r1, r1, r3, asl #9
	bl	ftl_memcpy
	b	.L2717
.L2714:
	mov	r0, r8
	mov	r1, r3
	mov	r2, fp
	bl	ftl_memset
.L2717:
	ldr	r3, [sp, #4]
	add	r5, r5, #1
	rsb	r6, r4, r6
	add	r7, r7, r4
	add	r3, r3, #4
	add	r8, r8, fp
	str	r3, [sp, #4]
	b	.L2712
.L2721:
	ldr	r0, [sp]
.L2711:
	add	sp, sp, #60
	@ sp needed
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L2723:
	.align	2
.L2722:
	.word	.LANCHOR0
	.word	.LANCHOR2
	.word	.LANCHOR0+2390
	.word	.LC133
	.word	.LANCHOR2+1788
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
	ldr	r4, .L2727
	sub	r5, r4, #492
	ldr	r2, [r4, #-484]
	ldrh	r1, [r5]
	bl	FtlVendorPartRead
	ldr	r3, [r4, #-484]
	ldr	r2, [r3]
	ldr	r3, .L2727+4
	cmp	r2, r3
	beq	.L2725
	ldr	r1, .L2727+8
	ldr	r0, .L2727+12
	bl	printk
	ldrh	r2, [r5]
	ldr	r0, [r4, #-484]
	mov	r1, #0
	mov	r2, r2, asl #9
	bl	ftl_memset
.L2725:
	mov	r0, #0
	ldmfd	sp!, {r3, r4, r5, pc}
.L2728:
	.align	2
.L2727:
	.word	.LANCHOR2
	.word	1112818501
	.word	.LC134
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
	ldr	r4, .L2735
	mov	r1, #1
	ldr	r2, .L2735+4
	bl	FtlVendorPartRead
	ldr	r5, .L2735+8
	ldr	r3, [r4, #-3236]
	cmp	r3, r5
	beq	.L2730
	ldr	r0, .L2735+4
	mov	r1, #0
	mov	r2, #512
	bl	ftl_memset
	str	r5, [r4, #-3236]
.L2730:
	ldr	r2, [r4, #-3236]
	ldr	r3, .L2735
	cmp	r2, r5
	bne	.L2731
	ldr	r2, [r3, #-3148]
	str	r2, [r3, #-3344]
	ldr	r2, [r3, #-3144]
	str	r2, [r3, #-3340]
	ldr	r2, [r3, #-3228]
	str	r2, [r3, #-3348]
	ldr	r2, [r3, #-3224]
	str	r2, [r3, #-3360]
	ldr	r2, [r3, #-3220]
	str	r2, [r3, #-3368]
	ldr	r2, [r3, #-3216]
	str	r2, [r3, #-3352]
	ldr	r2, [r3, #-3208]
	str	r2, [r3, #-3324]
	ldr	r2, [r3, #-3204]
	str	r2, [r3, #-3608]
	ldr	r2, [r3, #-3200]
	str	r2, [r3, #-3364]
	ldr	r2, [r3, #-3196]
	str	r2, [r3, #-3356]
	ldr	r2, [r3, #-3192]
	str	r2, [r3, #-3316]
	ldr	r2, [r3, #-3188]
	str	r2, [r3, #-3312]
	ldr	r2, [r3, #-3176]
	str	r2, [r3, #-3240]
.L2731:
	ldr	r1, [r4, #-3168]
	mov	r3, #0
	ldr	r2, .L2735+12
	str	r3, [r4, #-568]
	cmp	r1, r2
	ldr	r5, .L2735+16
	bne	.L2732
	ldrb	r2, [r5, #140]	@ zero_extendqisi2
	cmp	r2, r3
	beq	.L2733
	ldr	r2, .L2735
	str	r3, [r2, #-3168]
	bl	Ftl_save_ext_data
	b	.L2732
.L2733:
	ldr	r0, .L2735+20
	mov	r3, #1
	ldr	r1, .L2735+24
	str	r3, [r5, #2308]
	bl	printk
.L2732:
	movw	r3, #2376
	ldr	r0, [r4, #-3328]
	ldrh	r2, [r5, r3]
	ldr	r3, [r4, #-3324]
	mla	r0, r0, r2, r3
	movw	r3, #2324
	ldrh	r1, [r5, r3]
	bl	__aeabi_uidiv
	str	r0, [r4, #-3320]
	ldmfd	sp!, {r3, r4, r5, pc}
.L2736:
	.align	2
.L2735:
	.word	.LANCHOR2
	.word	.LANCHOR2-3236
	.word	1179929683
	.word	305432421
	.word	.LANCHOR0
	.word	.LC76
	.word	.LC135
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
	ldr	r4, .L2744
	mov	r2, #0
	str	r2, [r0, #36]
	ldr	r2, [r4, #-3620]
	cmp	r2, #0
	ldmnefd	sp!, {r4, r5, r6, pc}
	sub	r5, r5, #1
	mov	r6, r0
	ldr	r2, [r4, #-504]
	ldr	r0, [r4, #-524]
	uxth	r5, r5
	str	r2, [r4, #1760]
	str	r0, [r4, #1756]
	ldr	r3, [r3, r5, asl #2]
	cmp	r3, #0
	str	r3, [r4, #1752]
	beq	.L2741
	mov	r1, #1
	ldr	r0, .L2744+4
	mov	r2, r1
	bl	FlashReadPages
	b	.L2742
.L2741:
	ldr	r2, .L2744+8
	movw	r3, #2394
	mov	r1, #255
	ldrh	r2, [r2, r3]
	bl	ftl_memset
.L2742:
	mov	r0, r6
	mov	r1, r5
	ldr	r2, [r4, #1756]
	ldmfd	sp!, {r4, r5, r6, lr}
	b	FtlMapWritePage
.L2745:
	.align	2
.L2744:
	.word	.LANCHOR2
	.word	.LANCHOR2+1748
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
	ldr	r3, .L2761
	stmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, lr}
	.save {r4, r5, r6, r7, r8, r9, r10, lr}
	mov	r8, r2
	ldrh	r2, [r3, #124]
	.pad #40
	sub	sp, sp, #40
	ldrh	r3, [r3, #126]
	mov	r5, r0
	ldr	r9, .L2761+4
	mov	r7, r1
	smulbb	r3, r2, r3
	ldr	r2, [r9, #1684]
	str	r2, [sp, #12]
	ldr	r2, [r9, #1716]
	uxth	r3, r3
	sub	r6, r3, #1
	sub	r4, r3, #16
	mul	r10, r3, r1
	uxth	r6, r6
	str	r2, [sp, #16]
.L2747:
	cmp	r6, r4
	ble	.L2760
	mov	r1, #1
	add	r3, r6, r10
	add	r0, sp, #4
	mov	r2, r1
	mov	r3, r3, asl #10
	str	r3, [sp, #8]
	bl	FlashReadPages
	ldr	r3, [sp, #4]
	cmn	r3, #1
	beq	.L2748
	ldr	r3, [r9, #1716]
	ldrh	r2, [r3]
	movw	r3, #61664
	cmp	r2, r3
	bne	.L2748
	cmp	r5, #0
	moveq	r0, r5
	beq	.L2749
	cmp	r7, #0
	ldreq	ip, .L2761+4
	moveq	lr, #1
	beq	.L2750
.L2752:
	ldr	r1, [r9, #1684]
	mov	r2, r8
	mov	r0, r5
	bl	ftl_memcpy
	mov	r2, #4
	ldr	r0, .L2761+8
	mov	r1, r5
	mov	r3, r2
	bl	rknand_print_hex
	mov	r0, #0
	b	.L2749
.L2750:
	ldr	r2, [r9, #1704]
	uxth	r3, r7
	add	r7, r7, #1
	cmp	r3, r2
	bcs	.L2752
	ldr	r1, [ip, #1684]
	mov	r0, r3, lsr #5
	and	r4, r3, #31
	ldr	r2, [r1, r0, asl #2]
	orr	r4, r2, lr, asl r4
	str	r4, [r1, r0, asl #2]
	b	.L2750
.L2748:
	sub	r6, r6, #1
	uxth	r6, r6
	b	.L2747
.L2760:
	mvn	r0, #0
.L2749:
	add	sp, sp, #40
	@ sp needed
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, pc}
.L2762:
	.align	2
.L2761:
	.word	.LANCHOR0
	.word	.LANCHOR2
	.word	.LC136
	.fnend
	.size	FlashReadFacBbtData, .-FlashReadFacBbtData
	.align	2
	.global	FlashGetBadBlockList
	.type	FlashGetBadBlockList, %function
FlashGetBadBlockList:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	ldr	r3, .L2774
	stmfd	sp!, {r4, r5, r6, r7, r8, lr}
	.save {r4, r5, r6, r7, r8, lr}
	mov	r5, r0
	ldr	r3, [r3, #1204]
	ldr	r6, .L2774+4
	ldrb	r4, [r3, #13]	@ zero_extendqisi2
	ldrh	r3, [r3, #14]
	ldr	r0, [r6, #1712]
	smulbb	r4, r4, r3
	uxth	r4, r4
	add	r2, r4, #7
	mov	r2, r2, asr #3
	bl	FlashReadFacBbtData
	cmn	r0, #1
	bne	.L2764
.L2768:
	mov	r3, #0
	b	.L2765
.L2764:
	mov	lr, r4, lsr #4
	mov	ip, #0
	sub	r4, r4, #1
	mov	r3, ip
	mov	r7, #1
.L2766:
	uxth	r0, ip
	cmp	r0, r4
	bge	.L2765
	ldr	r8, [r6, #1712]
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
	bcc	.L2766
	b	.L2768
.L2765:
	mov	r3, r3, asl #1
	mvn	r2, #0
	mov	r0, #0
	strh	r2, [r5, r3]	@ movhi
	ldmfd	sp!, {r4, r5, r6, r7, r8, pc}
.L2775:
	.align	2
.L2774:
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
	ldr	r4, .L2800
	ldr	r5, [r4, #-3620]
	cmp	r5, #0
	bne	.L2777
	bl	FtlBbtMemInit
	ldr	r7, .L2800+4
	bl	FtlLoadFactoryBbt
	sub	r9, r7, #18
	sub	r10, r7, #94
.L2778:
	ldr	r3, .L2800+8
	ldr	r6, .L2800+12
	ldrh	r3, [r3]
	cmp	r5, r3
	bcs	.L2797
	ldrh	r3, [r9, #2]!
	movw	r2, #65535
	ldr	r0, [r4, #-528]
	ldr	fp, [r4, #-504]
	cmp	r3, r2
	ldr	r8, .L2800
	str	r0, [r4, #1756]
	str	fp, [r4, #1760]
	beq	.L2779
	ldrh	ip, [r10]
	mov	r1, #1
	mov	r2, r1
	ldr	r0, .L2800+16
	mla	ip, ip, r5, r3
	mov	r3, ip, asl #10
	str	ip, [sp, #4]
	str	r3, [r8, #1752]
	bl	FlashReadPages
	ldrh	r2, [r10]
	ldr	r0, [r7]
	add	r2, r2, #7
	ldr	r1, [r8, #1756]
	mov	r2, r2, asr #3
	bl	ftl_memcpy
	ldr	ip, [sp, #4]
	b	.L2780
.L2779:
	mov	r1, r5
	sub	r8, r8, #440
	bl	FlashGetBadBlockList
	ldr	r0, [r8, #2196]
	ldr	r1, [r7]
	bl	FtlBbt2Bitmap
	ldrh	r6, [r10]
	sub	r6, r6, #1
	uxth	r6, r6
.L2781:
	ldrh	r0, [r10]
	smlabb	r0, r0, r5, r6
	uxth	r0, r0
	bl	FtlBbmIsBadBlock
	cmp	r0, #1
	subeq	r6, r6, #1
	uxtheq	r6, r6
	beq	.L2781
.L2798:
	mov	r1, #0
	mov	r2, #16
	strh	r6, [r9]	@ movhi
	ldr	r0, [r4, #-504]
	bl	ftl_memset
	ldr	r3, .L2800+20
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
	ldr	r0, [r4, #1756]
	mla	ip, ip, r5, r3
	mov	r3, ip, asl #10
	str	r3, [r4, #1752]
	str	ip, [sp, #4]
	bl	ftl_memcpy
	mov	r1, #1
	ldr	r0, .L2800+16
	mov	r2, r1
	bl	FlashEraseBlocks
	mov	r1, #1
	mov	r3, r1
	ldr	r0, .L2800+16
	mov	r2, r1
	bl	FlashProgPages
	ldr	r3, [r4, #1748]
	cmn	r3, #1
	ldr	ip, [sp, #4]
	bne	.L2780
	uxth	r0, ip
	bl	FtlBbmMapBadBlock
	b	.L2781
.L2780:
	uxth	r0, ip
	add	r5, r5, #1
	bl	FtlBbmMapBadBlock
	add	r7, r7, #4
	b	.L2778
.L2797:
	ldr	r7, .L2800+24
	mov	r5, #0
.L2785:
	ldrh	r3, [r7]
	uxth	r0, r5
	add	r5, r5, #1
	cmp	r3, r0
	bls	.L2799
	bl	FtlBbmMapBadBlock
	b	.L2785
.L2799:
	ldr	r5, .L2800+28
	movw	r9, #65535
	ldrh	r7, [r5, #12]
	mov	r8, r5
	sub	r7, r7, #1
	uxth	r7, r7
.L2787:
	ldrh	r3, [r5, #12]
	sub	r3, r3, #48
	cmp	r7, r3
	ble	.L2791
	mov	r0, r7
	bl	FtlBbmIsBadBlock
	cmp	r0, #1
	beq	.L2788
	mov	r0, r7
	bl	FlashTestBlk
	cmp	r0, #0
	beq	.L2789
	mov	r0, r7
	bl	FtlBbmMapBadBlock
	b	.L2788
.L2789:
	ldrh	r3, [r8]
	cmp	r3, r9
	streqh	r7, [r8]	@ movhi
.L2790:
	ldrne	r3, .L2800+28
	strneh	r7, [r3, #4]	@ movhi
	bne	.L2791
.L2788:
	sub	r7, r7, #1
	uxth	r7, r7
	b	.L2787
.L2791:
	ldr	r0, [r4, #-3616]
	mov	r7, #0
	ldrh	r3, [r5]
	mov	r2, #2
	str	r7, [r6, #2456]
	mov	r1, #1
	strh	r7, [r5, #2]	@ movhi
	mov	r3, r3, asl #10
	str	r3, [r0, #4]
	ldrh	r3, [r5, #4]
	mov	r3, r3, asl #10
	str	r3, [r0, #40]
	bl	FlashEraseBlocks
	ldrh	r0, [r5]
	bl	FtlBbmMapBadBlock
	ldrh	r0, [r5, #4]
	bl	FtlBbmMapBadBlock
	bl	FtlBbmTblFlush
	ldr	r3, [r6, #2456]
	ldrh	r2, [r5, #4]
	add	r3, r3, #1
	str	r3, [r6, #2456]
	ldrh	r3, [r5]
	strh	r7, [r5, #2]	@ movhi
	strh	r2, [r5]	@ movhi
	strh	r3, [r5, #4]	@ movhi
	bl	FtlBbmTblFlush
.L2777:
	mov	r0, #0
	add	sp, sp, #12
	@ sp needed
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L2801:
	.align	2
.L2800:
	.word	.LANCHOR2
	.word	.LANCHOR0+2476
	.word	.LANCHOR0+2338
	.word	.LANCHOR0
	.word	.LANCHOR2+1748
	.word	-3872
	.word	.LANCHOR0+2398
	.word	.LANCHOR0+2448
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
	movw	r3, #2392
	ldr	r7, .L2819
	ldrh	r10, [r7, r3]
	ldr	r3, [r7, #2444]
	cmp	r0, r3
	bcs	.L2803
	add	r10, r10, #7
	ldr	fp, .L2819+4
	mov	r5, r0, lsr r10
	movw	r3, #2422
	str	r2, [sp, #4]
	mov	r9, r1
	mov	r6, r7
	str	r0, [sp]
	uxth	r5, r5
	ldrh	r2, [r7, r3]
	ldr	r8, [fp, #-3384]
	mov	r4, #0
	mov	r1, #12
	b	.L2804
.L2803:
	cmp	r2, #0
	mvn	r0, #0
	streq	r0, [r1]
	b	.L2805
.L2809:
	add	r4, r4, #1
	mla	r0, r1, r4, r8
	ldrh	r0, [r0, #-12]
	cmp	r0, r5
	beq	.L2806
.L2804:
	uxth	r7, r4
	cmp	r7, r2
	bcc	.L2809
	bl	select_l2p_ram_region
	mov	r4, #12
	movw	r3, #65535
	mul	r4, r4, r0
	mov	r7, r0
	add	r2, r8, r4
	ldrh	r1, [r8, r4]
	cmp	r1, r3
	bne	.L2818
.L2810:
	ldr	r3, [fp, #-456]
	ldr	r8, .L2819+4
	ldr	fp, [r3, r5, asl #2]
	cmp	fp, #0
	bne	.L2811
	ldr	r3, [r8, #-3384]
	mov	r1, #255
	add	r3, r3, r4
	ldr	r0, [r3, #8]
	movw	r3, #2394
	ldrh	r2, [r6, r3]
	bl	ftl_memset
	ldr	r3, [r8, #-3384]
	strh	r5, [r3, r4]	@ movhi
	ldr	r3, [r8, #-3384]
	add	r4, r3, r4
	str	fp, [r4, #4]
.L2806:
	ldr	r2, [sp]
	mvn	r3, #0
	mov	r6, #12
	bic	r10, r2, r3, asl r10
	ldr	r3, [sp, #4]
	ldr	r2, .L2819+4
	cmp	r3, #0
	uxth	r10, r10
	bne	.L2807
	ldr	r3, [r2, #-3384]
	mla	r6, r6, r7, r3
	ldr	r3, [r6, #8]
	ldr	r3, [r3, r10, asl #2]
	str	r3, [r9]
	b	.L2808
.L2807:
	mul	r6, r6, r7
	ldr	r3, [r2, #-3384]
	ldr	r1, [r9]
	add	r3, r3, r6
	ldr	r3, [r3, #8]
	str	r1, [r3, r10, asl #2]
	ldr	r3, [r2, #-3384]
	add	r6, r3, r6
	ldr	r3, [r6, #4]
	orr	r3, r3, #-2147483648
	str	r3, [r6, #4]
	sub	r3, r2, #3376
	strh	r5, [r3, #-4]	@ movhi
.L2808:
	ldr	r2, [r2, #-3384]
	mov	r3, #12
	mov	r0, #0
	mla	r7, r3, r7, r2
	ldr	r3, [r7, #4]
	cmn	r3, #1
	addne	r3, r3, #1
	strne	r3, [r7, #4]
	b	.L2805
.L2818:
	ldr	r3, [r2, #4]
	cmp	r3, #0
	bge	.L2810
	bl	flush_l2p_region
	b	.L2810
.L2811:
	ldr	r3, [r8, #-3384]
	mov	r1, #1
	ldr	r0, .L2819+8
	mov	r2, r1
	add	r3, r3, r4
	str	fp, [r8, #1752]
	ldr	r3, [r3, #8]
	str	r3, [r8, #1756]
	ldr	r3, [r8, #-504]
	str	r3, [r8, #1760]
	bl	FlashReadPages
	ldr	r3, [r8, #1760]
	ldrh	r3, [r3, #8]
	cmp	r3, r5
	beq	.L2812
	mov	r1, r5
	mov	r2, fp
	ldr	r0, .L2819+12
	bl	printk
	mov	r2, #4
	mov	r3, r2
	ldr	r0, .L2819+16
	ldr	r1, [r8, #1760]
	bl	rknand_print_hex
	movw	r3, #2420
	ldrh	r3, [r6, r3]
	mov	r2, #4
	ldr	r0, .L2819+20
	ldr	r1, [r8, #-456]
	bl	rknand_print_hex
	mov	r3, #1
	str	r3, [r8, #-3620]
	b	.L2813
.L2812:
	ldr	r3, [r8, #1748]
	cmp	r3, #256
	bne	.L2813
	mov	r1, r5
	mov	r2, fp
	ldr	r0, .L2819+24
	bl	printk
	ldr	r3, [r8, #-3384]
	sub	r0, r8, #436
	mov	r1, r5
	add	r3, r3, r4
	ldr	r2, [r3, #8]
	bl	FtlMapWritePage
.L2813:
	ldr	r3, .L2819+4
	mov	r1, #0
	ldr	r3, [r3, #-3384]
	add	r2, r3, r4
	str	r1, [r2, #4]
	strh	r5, [r3, r4]	@ movhi
	b	.L2806
.L2805:
	add	sp, sp, #12
	@ sp needed
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L2820:
	.align	2
.L2819:
	.word	.LANCHOR0
	.word	.LANCHOR2
	.word	.LANCHOR2+1748
	.word	.LC137
	.word	.LC100
	.word	.LC138
	.word	.LC139
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
	ldr	r4, .L2841
	ldr	r3, [r4, #-3620]
	cmp	r3, #0
	bne	.L2821
	ldr	r3, .L2841+4
	ldrh	r2, [r3, #4]
	cmp	r2, #0
	beq	.L2823
	ldrb	r3, [r4, #-3520]	@ zero_extendqisi2
	cmp	r3, #0
	bne	.L2823
	ldr	r8, .L2841+8
	ldrb	r1, [r4, #-3521]	@ zero_extendqisi2
	add	r3, r8, #2384
	ldrh	r3, [r3]
	mul	r3, r3, r1
	cmp	r2, r3
	beq	.L2823
	ldrb	r9, [r4, #-3518]	@ zero_extendqisi2
	movw	r3, #2316
	ldr	r6, [r8, #2444]
	cmp	r9, #0
	ldrh	r7, [r8, r3]
	bne	.L2821
	sub	r6, r6, #1
	mov	r1, sp
	mov	r2, r9
	mov	r0, r6
	bl	log2phys
	ldr	r5, [r4, #-504]
	ldr	r0, [r4, #-528]
	ldr	r3, [sp]
	str	r6, [sp, #20]
	cmn	r3, #1
	str	r0, [sp, #12]
	str	r3, [sp, #8]
	str	r5, [sp, #16]
	str	r9, [r5, #4]
	beq	.L2825
	mov	r1, #1
	mov	r2, r9
	add	r0, sp, #4
	bl	FlashReadPages
	b	.L2826
.L2825:
	movw	r3, #2394
	mov	r1, #255
	ldrh	r2, [r8, r3]
	bl	ftl_memset
.L2826:
	ldr	fp, .L2841+4
	mov	r7, r7, asl #2
	ldr	r3, .L2841+12
	mov	r8, #0
	ldr	r9, .L2841
	mov	r10, fp
	strh	r3, [r5]	@ movhi
.L2827:
	cmp	r8, r7
	beq	.L2828
	ldrh	r3, [fp, #4]
	cmp	r3, #0
	beq	.L2828
	ldr	r3, [sp, #8]
	add	r8, r8, #1
	str	r6, [r5, #8]
	ldr	r0, .L2841+4
	str	r3, [r5, #12]
	ldrh	r3, [fp]
	strh	r3, [r5, #2]	@ movhi
	bl	get_new_active_ppa
	ldr	r3, [r9, #-3332]
	mov	r2, #0
	mov	r1, #1
	str	r0, [sp, #8]
	add	r0, sp, #4
	str	r3, [r5, #4]
	add	r3, r3, #1
	cmn	r3, #1
	moveq	r3, #0
	str	r3, [r9, #-3332]
	mov	r3, r2
	bl	FlashProgPages
	ldrh	r0, [r10]
	bl	decrement_vpc_count
	b	.L2827
.L2828:
	mov	r3, #1
	b	.L2840
.L2823:
	mov	r3, #0
.L2840:
	strb	r3, [r4, #-3518]
.L2821:
	add	sp, sp, #44
	@ sp needed
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L2842:
	.align	2
.L2841:
	.word	.LANCHOR2
	.word	.LANCHOR2-3528
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
	ldr	r5, .L2849
	mov	r7, #12
	ldr	r6, .L2849+4
.L2844:
	ldrh	r3, [r5]
	uxth	r0, r4
	cmp	r3, r0
	bls	.L2848
	ldr	r3, [r6, #-3384]
	mla	r3, r7, r0, r3
	ldr	r3, [r3, #4]
	cmp	r3, #0
	bge	.L2845
	bl	flush_l2p_region
.L2845:
	add	r4, r4, #1
	b	.L2844
.L2848:
	mov	r0, #0
	ldmfd	sp!, {r3, r4, r5, r6, r7, pc}
.L2850:
	.align	2
.L2849:
	.word	.LANCHOR0+2422
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
	ldr	r5, .L2878
	ldrh	r6, [r0]
	ldr	r3, [r5, #-3620]
	cmp	r3, #0
	bne	.L2852
	movw	r3, #65535
	cmp	r6, r3
	mov	r4, r0
	beq	.L2853
	ldr	r2, [r5, #-3548]
	mov	r3, r6, asl #1
	mov	r0, r6
	ldrh	r3, [r2, r3]
	cmp	r3, #0
	beq	.L2854
	bl	INSERT_DATA_LIST
	b	.L2853
.L2854:
	bl	INSERT_FREE_LIST
.L2853:
	mov	r3, #0
	strb	r3, [r4, #8]
	ldr	r3, .L2878+4
	cmp	r4, r3
	beq	.L2855
	ldr	r3, .L2878+8
	add	r2, r3, #2336
	ldrh	r2, [r2]
	cmp	r2, #1
	beq	.L2855
	ldrb	r1, [r3, #140]	@ zero_extendqisi2
	cmp	r1, #0
	beq	.L2856
.L2855:
	mov	r3, #1
	strb	r3, [r4, #8]
	b	.L2857
.L2856:
	ldr	r1, .L2878+12
	cmp	r4, r1
	bne	.L2857
	cmp	r2, #3
	beq	.L2859
	ldr	r2, .L2878
	ldr	r2, [r2, #-3240]
	cmp	r2, #1
	bne	.L2860
.L2859:
	mov	r2, #1
	strb	r2, [r5, #-3520]
.L2860:
	ldr	r3, [r3, #2308]
	cmp	r3, #0
	beq	.L2857
	ldr	r3, [r5, #-3312]
	cmp	r3, #39
	ldrls	r3, .L2878
	movls	r2, #1
	strlsb	r2, [r3, #-3520]
.L2857:
	ldr	r2, .L2878
	movw	r1, #65535
	sub	r3, r2, #384
	ldrh	r0, [r3]
	mov	r5, r3
	cmp	r0, r1
	beq	.L2862
	cmp	r6, r0
	bne	.L2863
	ldr	r2, [r2, #-3548]
	mov	r3, r0, asl #1
	ldrh	r3, [r2, r3]
	cmp	r3, #0
	beq	.L2864
.L2863:
	bl	update_vpc_list
.L2864:
	mvn	r3, #0
	strh	r3, [r5]	@ movhi
.L2862:
	mov	r0, r4
	bl	allocate_data_superblock
	bl	l2p_flush
	mov	r0, #0
	bl	FtlEctTblFlush
	bl	FtlVpcTblFlush
.L2852:
	mov	r0, #0
	ldmfd	sp!, {r4, r5, r6, pc}
.L2879:
	.align	2
.L2878:
	.word	.LANCHOR2
	.word	.LANCHOR2-3480
	.word	.LANCHOR0
	.word	.LANCHOR2-3528
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
	ldr	r1, .L2901
	ldr	r0, .L2901+4
	bl	printk
	ldr	r0, .L2901+8
	mov	r1, #0
	mov	r2, #8192
	bl	memset
	ldr	r6, .L2901+12
	ldr	r5, .L2901+8
.L2881:
	ldr	r3, [r6, #2444]
	cmp	r4, r3
	bcs	.L2899
	mov	r0, r4
	add	r1, sp, #4
	mov	r2, #0
	bl	log2phys
	ldr	r0, [sp, #4]
	cmn	r0, #1
	beq	.L2882
	ubfx	r0, r0, #10, #16
	bl	P2V_block_in_plane
	mov	r0, r0, asl #1
	ldrh	r3, [r5, r0]
	add	r3, r3, #1
	strh	r3, [r5, r0]	@ movhi
.L2882:
	add	r4, r4, #1
	b	.L2881
.L2899:
	ldr	r5, .L2901+16
	mov	r4, #0
	ldr	r7, .L2901+20
	ldr	r8, .L2901+8
.L2884:
	ldrh	r3, [r5]
	uxth	r1, r4
	ldr	r6, .L2901+20
	cmp	r3, r1
	bls	.L2900
	ldr	r2, [r7, #-3548]
	mov	r3, r1, asl #1
	ldrh	r2, [r2, r3]
	ldrh	r3, [r8, r3]
	cmp	r2, r3
	beq	.L2885
	ldr	r0, .L2901+24
	bl	printk
.L2885:
	add	r4, r4, #1
	b	.L2884
.L2900:
	ldr	r3, [r6, #-3536]
	cmp	r3, #0
	beq	.L2880
	sub	r2, r6, #3520
	ldr	r8, .L2901+8
	mov	r5, #0
	mov	r9, #6
	ldrh	r7, [r2, #-12]
	ldr	r2, [r6, #-3556]
	rsb	r3, r2, r3
	ldr	r2, .L2901+28
	mov	r3, r3, asr #1
	mul	r3, r2, r3
	uxth	r4, r3
.L2888:
	uxth	r3, r5
	cmp	r3, r7
	bcs	.L2880
	ldr	r2, [r6, #-3548]
	mov	r3, r4, asl #1
	ldrh	r2, [r2, r3]
	cmp	r2, #0
	beq	.L2889
	ldr	r0, .L2901+32
	mov	r1, r4
	ldrh	r3, [r8, r3]
	bl	printk
.L2889:
	mul	r4, r9, r4
	ldr	r3, [r6, #-3556]
	add	r5, r5, #1
	ldrh	r4, [r3, r4]
	movw	r3, #65535
	cmp	r4, r3
	bne	.L2888
.L2880:
	add	sp, sp, #12
	@ sp needed
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, pc}
.L2902:
	.align	2
.L2901:
	.word	.LANCHOR3+72
	.word	.LC108
	.word	.LANCHOR2+1836
	.word	.LANCHOR0
	.word	.LANCHOR0+2324
	.word	.LANCHOR2
	.word	.LC140
	.word	-1431655765
	.word	.LC141
	.fnend
	.size	FtlCheckVpc, .-FtlCheckVpc
	.align	2
	.global	Ftlscanalldata
	.type	Ftlscanalldata, %function
Ftlscanalldata:
	.fnstart
	@ args = 0, pretend = 0, frame = 8
	@ frame_needed = 0, uses_anonymous_args = 0
	stmfd	sp!, {r4, r5, r6, r7, r8, lr}
	.save {r4, r5, r6, r7, r8, lr}
	mov	r1, #0
	.pad #32
	sub	sp, sp, #32
	ldr	r0, .L2913
	bl	printk
	ldr	r5, .L2913+4
	ldr	r8, .L2913+8
	mov	r4, #0
	mov	r6, r5
.L2904:
	ldr	r3, [r8, #2444]
	cmp	r4, r3
	bcs	.L2912
	mov	r0, r4
	add	r1, sp, #28
	mov	r2, #0
	bl	log2phys
	ubfx	r3, r4, #0, #11
	cmp	r3, #0
	bne	.L2905
	ldr	r0, .L2913+12
	mov	r1, r4
	ldr	r2, [sp, #28]
	bl	printk
.L2905:
	ldr	r3, [sp, #28]
	cmn	r3, #1
	beq	.L2907
	str	r3, [r5, #1752]
	mov	r2, #0
	ldr	r3, [r5, #-528]
	mov	r1, #1
	ldr	r7, [r5, #-504]
	ldr	r0, .L2913+16
	str	r3, [r5, #1756]
	str	r4, [r5, #1764]
	str	r7, [r5, #1760]
	str	r2, [r5, #1748]
	bl	FlashReadPages
	ldr	r3, [r5, #1748]
	cmn	r3, #1
	cmpne	r3, #256
	beq	.L2908
	ldr	r3, [r7, #8]
	cmp	r3, r4
	beq	.L2907
.L2908:
	ldr	r3, [r6, #1760]
	ldr	r2, [r6, #1756]
	ldr	r0, .L2913+20
	ldr	r1, [r3, #4]
	str	r1, [sp]
	ldr	r1, [r3, #8]
	str	r1, [sp, #4]
	ldr	r1, [r3, #12]
	str	r1, [sp, #8]
	ldr	r1, [r2]
	str	r1, [sp, #12]
	mov	r1, r4
	ldr	r2, [r2, #4]
	str	r2, [sp, #16]
	ldr	r2, [r6, #1752]
	ldr	r3, [r3]
	bl	printk
.L2907:
	add	r4, r4, #1
	b	.L2904
.L2912:
	add	sp, sp, #32
	@ sp needed
	ldmfd	sp!, {r4, r5, r6, r7, r8, pc}
.L2914:
	.align	2
.L2913:
	.word	.LC142
	.word	.LANCHOR2
	.word	.LANCHOR0
	.word	.LC143
	.word	.LANCHOR2+1748
	.word	.LC144
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
	ldr	r6, .L2925
	ldr	r4, [r6, #-3548]
	mov	r7, r0, asl #1
	ldrh	r3, [r4, r7]
	cmp	r3, #0
	addne	r3, r3, #1
	strneh	r3, [r4, r7]	@ movhi
	bne	.L2917
	ldr	r4, [r6, #-3536]
	cmp	r4, #0
	beq	.L2917
	ldr	r1, [r6, #-3556]
	sub	r2, r6, #3520
	ldr	lr, .L2925+4
	movw	r9, #65535
	rsb	r4, r1, r4
	ldrh	ip, [r2, #-12]
	mov	r8, r2
	mov	r4, r4, asr #1
	mul	r4, lr, r4
	mov	lr, #6
	uxth	r4, r4
.L2918:
	uxth	r2, r3
	cmp	r2, ip
	bcs	.L2917
	cmp	r4, r0
	bne	.L2919
	mov	r1, r4
	ldr	r0, .L2925+8
	bl	List_remove_node
	ldrh	r3, [r8, #-12]
	mov	r0, r4
	sub	r3, r3, #1
	strh	r3, [r8, #-12]	@ movhi
	bl	INSERT_DATA_LIST
	ldr	r2, [r6, #-3548]
	ldrh	r3, [r2, r7]
	add	r3, r3, #1
	strh	r3, [r2, r7]	@ movhi
	b	.L2917
.L2919:
	mul	r4, lr, r4
	add	r3, r3, #1
	ldrh	r4, [r1, r4]
	cmp	r4, r9
	bne	.L2918
.L2917:
	mov	r0, r5
	add	r1, sp, #4
	mov	r2, #1
	bl	log2phys
	add	sp, sp, #12
	@ sp needed
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, pc}
.L2926:
	.align	2
.L2925:
	.word	.LANCHOR2
	.word	-1431655765
	.word	.LANCHOR2-3536
	.fnend
	.size	FtlReUsePrevPpa, .-FtlReUsePrevPpa
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
	beq	.L3071
	stmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.save {r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.pad #68
	sub	sp, sp, #68
	ldrh	r3, [r0, #2]
	str	r3, [sp, #8]
	ldrb	r3, [r0, #6]	@ zero_extendqisi2
	ldr	r0, [sp, #8]
	str	r3, [sp, #16]
	ldr	r3, .L3086
	ldrh	r3, [r3]
	cmp	r3, r0
	mov	r3, #0
	streqh	r3, [r1, #4]	@ movhi
	streqb	r3, [r1, #6]
	ldrneh	r0, [r1, #16]
	beq	.L3068
.L2931:
	cmp	r0, r2
	add	r3, r3, #1
	uxtheq	r0, r3
	addeq	r0, r1, r0, asl #1
	ldreqh	r0, [r0, #16]
	beq	.L2931
.L3081:
	mov	r9, r1
	ldrb	r1, [r1, #8]	@ zero_extendqisi2
	cmp	r1, #1
	bne	.L2933
	bl	FtlGetLastWrittenPage
	cmn	r0, #1
	mov	r4, r0
	beq	.L2934
	ldr	r3, .L3086+4
	ldrb	r2, [r3, #140]	@ zero_extendqisi2
	cmp	r2, #0
	addeq	r3, r3, r0, asl #1
	ldreqh	r6, [r3, #144]
	beq	.L3005
	b	.L3072
.L2933:
	mov	r1, #0
	bl	FtlGetLastWrittenPage
	cmn	r0, #1
	mov	r4, r0
	beq	.L2934
.L3072:
	mov	r6, r4
.L3005:
	ldr	r3, .L3086+4
	movw	r2, #2316
	movw	r8, #65535
	ldrh	r2, [r3, r2]
	str	r2, [sp, #4]
	ldr	r2, .L3086+8
	ldr	ip, [r2, #-540]
	ldr	lr, [r2, #-2696]
	movw	r2, #2396
	ldrh	r7, [r3, r2]
	add	r3, r9, #14
	mov	r2, #0
	str	r3, [sp, #20]
	mov	r0, r3
	mov	r5, r2
	mov	r10, r2
	b	.L2936
.L2934:
	mov	r3, #0
	strh	r3, [r9, #2]	@ movhi
	strb	r3, [r9, #6]
	b	.L3068
.L2938:
	ldrh	r3, [r0, #2]!
	cmp	r3, r8
	beq	.L2937
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
.L2937:
	add	r2, r2, #1
.L2936:
	ldr	r1, [sp, #4]
	uxth	r3, r2
	cmp	r3, r1
	bcc	.L2938
	ldrb	r3, [r9, #8]	@ zero_extendqisi2
	cmp	r3, #1
	movne	r3, #0
	bne	.L3073
	ldr	r3, .L3086+4
	ldrb	lr, [r3, #140]	@ zero_extendqisi2
	adds	r3, lr, #0
	movne	r3, #1
.L3073:
	ldr	r7, .L3086+8
	mov	r1, r5
	str	r3, [sp, #24]
	mov	fp, #0
	ldr	r2, [sp, #24]
	movw	r10, #65535
	ldr	r0, [r7, #-540]
	bl	FlashReadPages
	ldr	r3, [r7, #-3332]
	str	r7, [sp, #12]
	str	r3, [sp, #28]
.L2940:
	uxth	r8, fp
	cmp	r8, r5
	bcs	.L2947
	mov	r1, #36
	ldr	r0, [r7, #-540]
	mul	r1, r1, fp
	add	ip, r0, r1
	ldr	r1, [r0, r1]
	cmp	r1, #0
	bne	.L2941
	ldr	ip, [ip, #12]
	ldr	r3, [ip, #4]
	cmn	r3, #1
	beq	.L2942
	mov	r0, r3
	ldr	r1, [r7, #-3332]
	str	ip, [sp, #32]
	str	r3, [sp, #4]
	bl	ftl_cmp_data_ver
	ldr	r3, [sp, #4]
	cmp	r0, #0
	ldr	ip, [sp, #32]
	addne	r3, r3, #1
	strne	r3, [r7, #-3332]
.L2942:
	ldr	r1, [ip]
	cmn	r1, #1
	bne	.L2943
.L2947:
	cmp	r8, r5
	ldr	r5, .L3086+8
	bne	.L3069
	add	fp, r4, #1
	uxth	r3, fp
	str	r3, [sp, #4]
	ldr	r3, [r5, #-540]
	ldr	r0, [r3, #4]
	b	.L3074
.L2941:
	ldr	r1, [ip, #4]
	uxth	r10, r6
	ldr	r0, .L3086+12
	bl	printk
	ldrh	r1, [r9]
	ldr	r3, .L3086+16
	strh	r1, [r3]	@ movhi
.L2943:
	add	fp, fp, #1
	b	.L2940
.L3069:
	uxth	r3, r4
	str	r3, [sp, #4]
	ldr	r3, [sp, #12]
	mov	r2, #36
	ldr	r3, [r3, #-540]
	mla	r8, r2, r8, r3
	ldr	r0, [r8, #4]
.L3074:
	ubfx	r0, r0, #10, #16
	bl	P2V_plane
	ldrb	r3, [r9, #8]	@ zero_extendqisi2
	cmp	r3, #1
	str	r0, [sp, #12]
	bne	.L2949
	ldr	r2, .L3086+4
	ldrb	r1, [r2, #140]	@ zero_extendqisi2
	cmp	r1, #0
	ldreq	r1, [sp, #4]
	addeq	r2, r2, r1, asl #1
	ldreqh	r2, [r2, #144]
	streq	r2, [sp, #4]
.L2949:
	ldr	r2, .L3086
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
	beq	.L3079
	clz	r3, r3
	ldr	r2, [sp, #28]
	mov	r3, r3, lsr #5
	sub	fp, r2, #1
	movw	r2, #65535
	cmp	r10, r2
	orrne	r3, r3, #1
	cmp	r3, #0
	beq	.L2953
	ldr	r3, [r5, #-372]
	uxth	r10, r6
	ldr	r7, .L3086+8
	mvn	r8, #0
	cmn	r3, #1
	mov	r6, r8
	ldreq	r3, .L3086+8
	streq	fp, [r3, #-372]
	ldr	r3, [r5, #-372]
	str	r3, [sp, #16]
	ldr	r3, [sp, #8]
	add	r3, r3, #7
	cmp	r10, r3
	subgt	r4, r10, #7
	ldrle	r4, [sp, #8]
	uxthgt	r4, r4
.L2956:
	cmp	r4, r10
	bhi	.L2969
	ldr	r3, .L3086+20
	mov	r0, #36
	ldr	lr, [r7, #-540]
	ldr	r1, [sp, #20]
	ldrh	r3, [r3]
	str	r3, [sp, #28]
	mov	r3, #0
	mov	r5, r3
.L2970:
	ldr	ip, [sp, #28]
	uxth	r2, r3
	cmp	r2, ip
	bcs	.L3082
	ldrh	r2, [r1, #2]!
	movw	ip, #65535
	add	r3, r3, #1
	cmp	r2, ip
	orrne	r2, r4, r2, asl #10
	mlane	ip, r0, r5, lr
	addne	r5, r5, #1
	uxthne	r5, r5
	strne	r2, [ip, #4]
	b	.L2970
.L3082:
	mov	r1, r5
	ldr	r2, [sp, #24]
	ldr	r0, [r7, #-540]
	bl	FlashReadPages
	ldr	r3, .L3086+4
	mov	r2, #36
	movw	r1, #65535
	ldrb	ip, [r3, #140]	@ zero_extendqisi2
	ldr	r3, [r7, #-540]
	mla	r5, r2, r5, r3
	ldr	r2, .L3086+24
	add	r2, r2, r4, asl #1
.L2959:
	cmp	r3, r5
	beq	.L3083
	ldr	r0, [r3]
	cmp	r0, #0
	bne	.L2960
	ldr	r0, [r3, #12]
	ldrh	lr, [r0]
	cmp	lr, r1
	beq	.L2961
	ldr	r0, [r0, #4]
	cmn	r0, #1
	beq	.L2961
	cmn	r8, #1
	ldr	r6, [r7, #-372]
	str	r0, [r7, #-372]
	bne	.L2961
	ldrh	r0, [r2]
	cmp	r0, r1
	bne	.L2962
	cmp	ip, #0
	beq	.L2961
.L2962:
	cmp	r6, fp
	mvneq	r8, #0
	movne	r8, r6
	b	.L2961
.L2960:
	ldrh	r1, [r9]
	movw	r3, #1844
	ldr	r2, .L3086+28
	strh	r1, [r2, r3]	@ movhi
	ldrb	r3, [r9, #8]	@ zero_extendqisi2
	cmp	r3, #0
	bne	.L2953
	ldr	r3, .L3086+8
	mov	r4, r4, asl #1
	sub	r2, r3, #2624
	ldrh	r1, [r2, r4]
	movw	r2, #65535
	cmp	r1, r2
	bne	.L2964
	cmn	r8, #1
	strne	r8, [r3, #-372]
	bne	.L2953
	ldr	r2, [sp, #16]
	cmp	r2, fp
	bne	.L3075
	ldr	r2, [r3, #-372]
	b	.L3080
.L2964:
	cmp	r6, fp
	beq	.L2967
	cmn	r6, #1
	strne	r6, [r3, #-372]
	b	.L2953
.L2967:
	ldr	r2, [r3, #-372]
	cmp	r2, fp
	beq	.L2953
.L3080:
	sub	r2, r2, #1
	b	.L3075
.L2961:
	add	r3, r3, #36
	b	.L2959
.L3083:
	add	r4, r4, #1
	uxth	r4, r4
	b	.L2956
.L2969:
	ldr	r3, .L3086+8
	mvn	r2, #0
.L3075:
	str	r2, [r3, #-372]
.L2953:
	ldr	r2, .L3086+28
	movw	r3, #1846
	ldr	r10, [sp, #8]
	mov	r1, #1
	ldr	r4, .L3086+8
	strh	r1, [r2, r3]	@ movhi
.L2971:
	ldr	r3, .L3086+20
	movw	r1, #65535
	ldr	r8, [r4, #-540]
	mov	r0, #36
	ldr	r6, [sp, #20]
	mov	r5, #0
	ldrh	r7, [r3]
	ldr	r3, .L3086+4
	str	r5, [sp, #16]
	ldrb	lr, [r3, #140]	@ zero_extendqisi2
.L2972:
	uxth	r3, r5
	cmp	r3, r7
	bcs	.L3084
	ldrh	r3, [r6, #2]!
	cmp	r3, r1
	beq	.L2973
	ldr	r2, [sp, #16]
	orr	r3, r10, r3, asl #10
	mla	r2, r0, r2, r8
	str	r3, [r2, #4]
	ldrb	ip, [r9, #8]	@ zero_extendqisi2
	cmp	ip, #1
	bne	.L2974
	cmp	lr, #0
	orrne	r3, r3, #-2147483648
	strne	r3, [r2, #4]
.L2974:
	ldr	r3, [sp, #16]
	add	r3, r3, #1
	uxth	r3, r3
	str	r3, [sp, #16]
.L2973:
	add	r5, r5, #1
	b	.L2972
.L3084:
	ldr	r0, [r4, #-540]
	ldr	r1, [sp, #16]
	ldr	r2, [sp, #24]
	bl	FlashReadPages
	mov	r3, #0
.L3078:
	str	r3, [sp, #28]
	ldr	r2, [sp, #16]
	ldrh	r3, [sp, #28]
	cmp	r3, r2
	bcs	.L3085
	ldr	r3, [sp, #28]
	mov	r5, #36
	ldr	r8, [r4, #-540]
	mul	r5, r5, r3
	add	r7, r8, r5
	ldr	r6, [r7, #4]
	ubfx	r0, r6, #10, #16
	str	r6, [sp, #60]
	bl	P2V_plane
	ldr	r3, [sp, #8]
	cmp	r10, r3
	bcc	.L2977
	ldr	r3, [sp, #32]
	ldr	r2, [sp, #8]
	cmp	r0, r3
	movcs	r3, #0
	movcc	r3, #1
	cmp	r10, r2
	movne	r3, #0
	cmp	r3, #0
	bne	.L2977
	ldr	r3, [sp, #12]
	ldr	r2, [sp, #4]
	cmp	r0, r3
	cmpeq	r10, r2
	beq	.L2978
	ldr	r3, [r8, r5]
	cmn	r3, #1
	beq	.L2979
	ldr	r7, [r7, #12]
	movw	r3, #61589
	ldrh	r2, [r7]
	cmp	r2, r3
	ldrneh	r0, [r9]
	bne	.L3076
	ldr	fp, [r7, #4]
	cmn	fp, #1
	beq	.L2981
	ldr	r1, [r4, #-3332]
	mov	r0, fp
	bl	ftl_cmp_data_ver
	cmp	r0, #0
	addne	r3, fp, #1
	strne	r3, [r4, #-3332]
.L2981:
	ldr	r6, [r7, #8]
	add	r1, sp, #56
	ldr	r3, [r7, #12]
	mov	r2, #0
	mov	r0, r6
	str	r3, [sp, #52]
	bl	log2phys
	ldr	r1, [r4, #-372]
	cmn	r1, #1
	beq	.L2982
	mov	r0, fp
	bl	ftl_cmp_data_ver
	cmp	r0, #0
	beq	.L2982
	ldr	r3, [sp, #52]
	cmn	r3, #1
	beq	.L2983
	ldr	r0, [r4, #-540]
	mov	r2, #0
	mov	r1, #1
	add	r0, r0, r5
	str	r3, [r0, #4]
	ldr	r8, [r0, #12]
	bl	FlashReadPages
	ldr	r2, [r4, #-540]
	ldr	r3, [r8, #4]
	add	ip, r2, r5
	str	r3, [sp, #36]
	ldr	r3, [r2, r5]
	cmn	r3, #1
	bne	.L2984
	b	.L2985
.L2983:
	ldr	r3, [sp, #60]
	ldr	r2, [sp, #56]
	cmp	r2, r3
	bne	.L2977
	mov	r0, r6
	add	r1, sp, #52
	mov	r2, #1
	bl	log2phys
	b	.L2977
.L2984:
	ldr	r7, [r8, #8]
	cmp	r7, r6
	bne	.L2985
	ldr	r0, [r4, #-372]
	ldr	r1, [sp, #36]
	str	r2, [sp, #44]
	str	ip, [sp, #40]
	bl	ftl_cmp_data_ver
	cmp	r0, #0
	ldr	ip, [sp, #40]
	ldr	r2, [sp, #44]
	beq	.L2985
	ldr	r3, [sp, #56]
	ldr	r1, [sp, #60]
	cmp	r3, r1
	beq	.L2990
	ldr	r1, [sp, #52]
	cmp	r3, r1
	beq	.L2985
	cmn	r3, #1
	streq	r3, [r2, r5]
	beq	.L2989
	str	r3, [ip, #4]
	mov	r0, ip
	mov	r1, #1
	mov	r2, #0
	ldr	r8, [ip, #12]
	bl	FlashReadPages
.L2989:
	ldr	r3, [r4, #-540]
	ldr	r3, [r3, r5]
	cmn	r3, #1
	beq	.L2990
	ldr	r5, [r8, #4]
	ldr	r0, [r4, #-372]
	mov	r1, r5
	bl	ftl_cmp_data_ver
	cmp	r0, #0
	beq	.L2990
	ldr	r0, [sp, #36]
	mov	r1, r5
	bl	ftl_cmp_data_ver
	cmp	r0, #0
	beq	.L2985
.L2990:
	mov	r0, r7
	ldr	r1, [sp, #52]
	bl	FtlReUsePrevPpa
.L2985:
	mvn	r3, #0
	str	r3, [sp, #52]
	b	.L2992
.L2982:
	ldr	r3, [sp, #60]
	ldr	r2, [sp, #56]
	cmp	r2, r3
	beq	.L2992
	ldr	r3, [sp, #52]
	cmn	r3, #1
	beq	.L2994
	ldr	r2, .L3086+4
	ubfx	r3, r3, #10, #21
	ldr	r2, [r2, #2332]
	cmp	r3, r2
	bcs	.L2977
.L2994:
	mov	r0, r6
	add	r1, sp, #60
	mov	r2, #1
	bl	log2phys
	ldr	r5, [sp, #56]
	cmn	r5, #1
	beq	.L2992
	ldr	r3, [sp, #52]
	cmp	r5, r3
	beq	.L2992
	ubfx	r0, r5, #10, #16
	bl	P2V_block_in_plane
	ldr	r3, .L3086+32
	ldrh	r2, [r3]
	cmp	r2, r0
	beq	.L2996
	ldrh	r2, [r3, #48]
	cmp	r2, r0
	beq	.L2996
	ldrh	r3, [r3, #96]
	cmp	r3, r0
	bne	.L2992
.L2996:
	ldr	r0, [r4, #-540]
	mov	r1, #1
	mov	r2, #0
	str	r5, [r0, #4]
	ldr	r7, [r0, #12]
	bl	FlashReadPages
	ldr	r3, [r4, #-540]
	ldr	r1, [r7, #4]
	ldr	r3, [r3]
	cmn	r3, #1
	beq	.L2992
	mov	r0, fp
	bl	ftl_cmp_data_ver
	cmp	r0, #0
	bne	.L2992
	mov	r0, r6
	add	r1, sp, #56
	mov	r2, #1
	bl	log2phys
.L2992:
	ldr	r0, [sp, #52]
	cmn	r0, #1
	beq	.L2977
	ubfx	r0, r0, #10, #16
	bl	P2V_block_in_plane
	ldr	r2, [r4, #-3548]
	mov	r3, r0, asl #1
	mov	r1, r0
	ldrh	r3, [r2, r3]
	cmp	r3, #0
	beq	.L2997
.L3076:
	bl	decrement_vpc_count
	b	.L2977
.L2997:
	ldr	r0, .L3086+36
	bl	printk
	b	.L2977
.L2979:
	ldrh	r3, [r9]
	mov	r1, r6
	ldr	r2, .L3086+16
	ldr	r0, .L3086+40
	strh	r3, [r2]	@ movhi
	mov	r2, fp
	bl	printk
	ldr	r3, .L3086+28
	ldr	r3, [r3, #1848]
	cmp	r3, #31
	bhi	.L2998
	ldr	r2, .L3086+28
	ldr	r1, [sp, #60]
	add	r2, r2, r3, asl #2
	add	r3, r3, #1
	str	r1, [r2, #1852]
	ldr	r2, .L3086+28
	str	r3, [r2, #1848]
.L2998:
	ldrh	r0, [r9]
	bl	decrement_vpc_count
	ldr	r3, [r4, #-372]
	cmn	r3, #1
	beq	.L3077
	cmp	r3, fp
	bls	.L2977
.L3077:
	str	fp, [r4, #-372]
.L2977:
	ldr	r3, [sp, #28]
	add	r3, r3, #1
	b	.L3078
.L3085:
	ldrb	r3, [r9, #8]	@ zero_extendqisi2
	add	r10, r10, #1
	cmp	r3, #1
	uxth	r10, r10
	bne	.L3001
	ldr	r3, .L3086+4
	ldrb	r3, [r3, #140]	@ zero_extendqisi2
	cmp	r3, #0
	beq	.L3001
	ldr	r3, .L3086+44
	ldr	r2, [sp, #4]
	ldrh	r3, [r3]
	cmp	r2, r10
	cmpeq	r3, r10
	beq	.L2978
.L3001:
	ldr	r2, .L3086+4
	add	r3, r2, #2384
	ldrh	r3, [r3]
	cmp	r10, r3
	bne	.L2971
	movw	r1, #2316
	movw	r0, #65535
	ldrh	r1, [r2, r1]
	mov	r3, #0
	strh	r10, [r9, #2]	@ movhi
	strh	r3, [r9, #4]	@ movhi
.L3002:
	uxth	r2, r3
	cmp	r2, r1
	bcs	.L3068
	ldr	lr, [sp, #20]
	add	r3, r3, #1
	ldrh	ip, [lr, #2]!
	cmp	ip, r0
	str	lr, [sp, #20]
	beq	.L3002
	strb	r2, [r9, #6]
	b	.L3068
.L2978:
	ldrb	r3, [sp, #12]	@ zero_extendqisi2
	mov	r0, r9
	ldr	r1, [sp, #4]
	strb	r3, [r9, #6]
	ldrh	r3, [sp, #4]
	strh	r3, [r9, #2]	@ movhi
.L3079:
	ldr	r2, [sp, #12]
	bl	ftl_sb_update_avl_pages
.L3068:
	mov	r0, #0
	add	sp, sp, #68
	@ sp needed
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L3071:
	mov	r0, #0
	bx	lr
.L3087:
	.align	2
.L3086:
	.word	.LANCHOR0+2384
	.word	.LANCHOR0
	.word	.LANCHOR2
	.word	.LC145
	.word	.LANCHOR4+1844
	.word	.LANCHOR0+2316
	.word	.LANCHOR2-2624
	.word	.LANCHOR4
	.word	.LANCHOR2-3528
	.word	.LC146
	.word	.LC147
	.word	.LANCHOR0+2386
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
	ldr	r5, .L3105
	ldr	r1, .L3105+4
	ldr	r0, .L3105+8
	bl	printk
	movw	r3, #2326
	ldrh	r2, [r5, r3]
	mov	r1, #0
	ldr	r6, .L3105+12
	mov	r2, r2, asl #1
	ldr	r0, [r6, #-480]
	bl	ftl_memset
.L3089:
	ldr	r3, [r5, #2444]
	cmp	r4, r3
	bcs	.L3103
	mov	r0, r4
	add	r1, sp, #4
	mov	r2, #0
	bl	log2phys
	ldr	r0, [sp, #4]
	cmn	r0, #1
	beq	.L3090
	ubfx	r0, r0, #10, #16
	bl	P2V_block_in_plane
	ldr	r2, [r6, #-480]
	mov	r0, r0, asl #1
	ldrh	r3, [r2, r0]
	add	r3, r3, #1
	strh	r3, [r2, r0]	@ movhi
.L3090:
	add	r4, r4, #1
	b	.L3089
.L3103:
	ldr	r8, .L3105+16
	mov	r7, #0
	ldr	r10, .L3105+12
	ldr	r9, .L3105+20
.L3092:
	ldrh	r3, [r8]
	uxth	r4, r7
	cmp	r3, r4
	bls	.L3104
	ldr	r3, [r6, #-3548]
	mov	r5, r4, asl #1
	movw	r1, #65535
	ldrh	r2, [r3, r5]
	ldr	r3, [r6, #-480]
	ldrh	r3, [r3, r5]
	cmp	r2, r1
	cmpne	r2, r3
	beq	.L3093
	ldrh	r1, [r9]
	cmp	r1, r4
	beq	.L3093
	ldr	r1, .L3105+24
	ldrh	r0, [r1]
	cmp	r0, r4
	beq	.L3093
	ldrh	r1, [r1, #-48]
	cmp	r1, r4
	beq	.L3093
	ldr	r0, .L3105+28
	mov	r1, r4
	bl	printk
	ldr	r3, [r10, #-3548]
	ldrh	r2, [r3, r5]
	cmp	r2, #0
	ldr	r2, [r10, #-480]
	ldrh	r2, [r2, r5]
	strh	r2, [r3, r5]	@ movhi
	beq	.L3093
	mov	r0, r4
	bl	update_vpc_list
.L3093:
	add	r7, r7, #1
	b	.L3092
.L3104:
	bl	l2p_flush
	bl	FtlVpcTblFlush
	add	sp, sp, #8
	@ sp needed
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, pc}
.L3106:
	.align	2
.L3105:
	.word	.LANCHOR0
	.word	.LANCHOR3+84
	.word	.LC108
	.word	.LANCHOR2
	.word	.LANCHOR0+2324
	.word	.LANCHOR2-3528
	.word	.LANCHOR2-3432
	.word	.LC148
	.fnend
	.size	FtlVpcCheckAndModify, .-FtlVpcCheckAndModify
	.align	2
	.global	FtlGcScanTempBlk
	.type	FtlGcScanTempBlk, %function
FtlGcScanTempBlk:
	.fnstart
	@ args = 0, pretend = 0, frame = 64
	@ frame_needed = 0, uses_anonymous_args = 0
	ldr	r2, .L3160
	movw	r3, #3352
	stmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.save {r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.pad #68
	sub	sp, sp, #68
	ldrh	r5, [r2, r3]
	movw	r3, #65535
	mov	r4, r0
	str	r1, [sp, #12]
	cmp	r5, r3
	beq	.L3141
	cmp	r5, #0
	bne	.L3108
	b	.L3109
.L3141:
	mov	r5, #0
.L3108:
	ldr	r3, .L3160+4
	ldr	r2, [sp, #12]
	ldrh	r3, [r3]
	cmp	r2, r3
	bne	.L3110
.L3109:
	bl	FtlGcPageVarInit
.L3110:
	ldr	r6, .L3160+8
	mvn	r3, #0
	str	r3, [sp, #8]
	mov	r3, #0
	str	r3, [sp]
.L3111:
	ldrh	r1, [r4]
	movw	r3, #65535
	mov	r2, #0
	strb	r2, [r4, #8]
	cmp	r1, r3
	beq	.L3112
.L3113:
.L3138:
	ldr	r3, .L3160+12
	mov	r7, #0
	ldr	ip, [r6, #-2696]
	add	r1, r4, #14
	mov	r8, r7
	movw	r9, #65535
	ldrh	r3, [r3]
	mov	r10, #36
	str	r3, [sp, #4]
	ldr	r3, [r6, #-540]
	str	r3, [sp, #16]
	ldr	r3, [r6, #-2700]
	str	r3, [sp, #20]
	ldr	r3, .L3160+16
	ldrh	r0, [r3]
	ldrh	lr, [r3, #2]
.L3114:
	ldr	r2, [sp, #4]
	uxth	r3, r7
	cmp	r3, r2
	bcs	.L3158
	ldrh	r3, [r1, #2]!
	cmp	r3, r9
	beq	.L3115
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
.L3115:
	add	r7, r7, #1
	b	.L3114
.L3158:
	ldr	r0, [r6, #-540]
	mov	r1, r8
	mov	r2, #0
	mov	fp, #0
	bl	FlashReadPages
.L3117:
	uxth	r3, fp
	cmp	r3, r8
	bcs	.L3159
	ldr	r3, .L3160+8
	mov	r9, #36
	mul	r9, r9, fp
	ldr	r7, [r3, #-540]
	add	r10, r7, r9
	ldr	r3, [r10, #4]
	ubfx	r0, r3, #10, #16
	str	r3, [sp, #4]
	bl	P2V_plane
	ldr	r7, [r7, r9]
	ldr	r10, [r10, #12]
	cmp	r7, #0
	ldr	r3, .L3160+8
	ldr	ip, .L3160+20
	mov	r2, r0
	bne	.L3118
	ldrh	r0, [r10]
	movw	r1, #65535
	cmp	r0, r1
	bne	.L3119
.L3122:
	ldrb	r1, [ip, #140]	@ zero_extendqisi2
	cmp	r1, #0
	beq	.L3153
	mov	r1, #1
	str	r1, [r3, #-376]
	b	.L3112
.L3119:
	ldr	r1, .L3160+20
	ldr	r0, [r10, #8]
	ldr	r1, [r1, #2444]
	cmp	r0, r1
	bls	.L3154
	b	.L3122
.L3153:
	ldrh	r3, [r4]
	ldr	r2, .L3160+8
	mov	r3, r3, asl #1
	ldr	r2, [r2, #-3548]
	b	.L3157
.L3154:
	ldr	r3, .L3160+20
	ldrb	r3, [r3]	@ zero_extendqisi2
	cmp	r3, #0
	beq	.L3126
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
	beq	.L3126
	str	r3, [sp, #32]
	mov	r2, r7
	ldr	r3, [r6, #-508]
	add	r0, sp, #28
	mov	r1, #1
	str	r3, [sp, #36]
	ldr	r3, [r6, #-500]
	str	r3, [sp, #40]
	bl	FlashReadPages
	ldr	r3, .L3160+24
	ldr	r2, [r6, #-540]
	ldrh	r3, [r3]
	add	r9, r2, r9
	mov	r3, r3, asl #7
	ldr	r2, [sp, #36]
	b	.L3128
.L3129:
	add	r7, r7, #1
.L3128:
	cmp	r7, r3
	beq	.L3126
	ldr	r1, [r9, #8]
	ldr	r0, [r1, r7, asl #2]
	ldr	r1, [r2, r7, asl #2]
	cmp	r0, r1
	beq	.L3129
	ldrh	r1, [r4]
	ldr	r2, [sp, #32]
	ldr	r0, .L3160+28
	bl	printk
	ldrh	r3, [r4]
	ldr	r2, [r6, #-3548]
	mov	r1, #0
	mov	r3, r3, asl #1
.L3157:
	strh	r1, [r2, r3]	@ movhi
	ldrh	r0, [r4]
	bl	INSERT_FREE_LIST
	ldr	r2, .L3160+32
	mvn	r3, #0
	strh	r3, [r4]	@ movhi
	strh	r3, [r2]	@ movhi
	b	.L3156
.L3126:
	ldr	r0, [r10, #12]
	add	fp, fp, #1
	ldr	r1, [sp, #4]
	ldr	r2, [r10, #8]
	bl	FtlGcUpdatePage
	b	.L3117
.L3118:
	ldr	r2, [sp, #4]
	ldr	r0, .L3160+36
	ldrh	r1, [r4]
	str	ip, [sp, #20]
	str	r3, [sp, #16]
	bl	printk
	ldrh	r5, [r4]
	ldr	ip, [sp, #20]
	ldr	r3, [sp, #16]
	ldr	r2, [ip, #2308]
	cmp	r2, #0
	bne	.L3132
	ldrb	r2, [ip, #140]	@ zero_extendqisi2
	cmp	r2, #0
	beq	.L3133
.L3132:
	ldr	r1, [r3, #-3612]
	mov	r2, r5, asl #1
	ldrh	r2, [r1, r2]
	cmp	r2, #159
	bls	.L3134
.L3133:
	ldr	r2, [r3, #-540]
	ldr	r2, [r2, r9]
	cmn	r2, #1
	bne	.L3135
.L3134:
	ldr	r3, [r3, #-540]
	add	r9, r3, r9
	ldr	r3, [r9, #4]
	str	r3, [sp, #8]
.L3135:
	ldr	r3, .L3160+8
	mov	r5, r5, asl #1
	mov	r2, #0
	ldr	r3, [r3, #-3548]
	strh	r2, [r3, r5]	@ movhi
	ldrh	r0, [r4]
	bl	INSERT_FREE_LIST
	mvn	r3, #0
	strh	r3, [r4]	@ movhi
.L3156:
	bl	FtlGcPageVarInit
	mov	r5, #0
	b	.L3111
.L3159:
	ldr	r3, [sp]
	add	r5, r5, #1
	ldr	r2, [sp, #12]
	add	r3, r3, #1
	uxth	r5, r5
	cmp	r3, r2
	str	r3, [sp]
	ldr	r2, .L3160+4
	bcs	.L3137
.L3139:
	ldrh	r3, [r2]
	cmp	r3, r5
	bhi	.L3138
	mov	r2, #0
	b	.L3112
.L3137:
	ldr	r1, .L3160+40
	movw	r0, #65535
	ldrh	r3, [r1]
	cmp	r3, r0
	beq	.L3139
	ldr	r0, [sp]
	add	r3, r3, r0
	strh	r3, [r1]	@ movhi
	ldrh	r3, [r2]
	cmp	r3, r5
	bls	.L3139
	b	.L3140
.L3112:
	ldr	r1, .L3160
	movw	r3, #3352
	mvn	r0, #0
	strh	r5, [r4, #2]	@ movhi
	strb	r2, [r4, #6]
	strh	r0, [r1, r3]	@ movhi
	mov	r0, r4
	mov	r1, r5
	bl	ftl_sb_update_avl_pages
.L3140:
	ldr	r0, [sp, #8]
	add	sp, sp, #68
	@ sp needed
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L3161:
	.align	2
.L3160:
	.word	.LANCHOR1
	.word	.LANCHOR0+2384
	.word	.LANCHOR2
	.word	.LANCHOR0+2316
	.word	.LANCHOR0+2394
	.word	.LANCHOR0
	.word	.LANCHOR0+2390
	.word	.LC149
	.word	.LANCHOR2-3288
	.word	.LC150
	.word	.LANCHOR1+3352
	.fnend
	.size	FtlGcScanTempBlk, .-FtlGcScanTempBlk
	.align	2
	.global	FtlReadRefresh
	.type	FtlReadRefresh, %function
FtlReadRefresh:
	.fnstart
	@ args = 0, pretend = 0, frame = 40
	@ frame_needed = 0, uses_anonymous_args = 0
	stmfd	sp!, {r4, r5, r6, r7, r8, r9, lr}
	.save {r4, r5, r6, r7, r8, r9, lr}
	.pad #44
	sub	sp, sp, #44
	ldr	r5, .L3179
	ldr	r4, .L3179+4
	ldr	r9, [r5, #-3156]
	mov	r6, r5
	cmp	r9, #0
	beq	.L3163
	ldr	r2, [r5, #-3152]
	ldr	r3, [r4, #2444]
	cmp	r2, r3
	bcs	.L3164
	mov	r5, #2048
	mov	r7, r6
.L3169:
	ldr	r0, [r6, #-3152]
	ldr	r3, [r4, #2444]
	cmp	r0, r3
	bcs	.L3168
	mov	r2, #0
	mov	r1, sp
	bl	log2phys
	ldr	r3, [r7, #-3152]
	add	r3, r3, #1
	str	r3, [r7, #-3152]
	ldr	r2, [sp]
	cmn	r2, #1
	beq	.L3167
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
	bne	.L3168
	ldr	r0, [sp]
	ubfx	r0, r0, #10, #16
	bl	P2V_block_in_plane
	bl	FtlGcRefreshBlock
.L3168:
	mvn	r0, #0
	b	.L3171
.L3167:
	subs	r5, r5, #1
	bne	.L3169
	b	.L3168
.L3164:
	ldr	r3, [r5, #-3368]
	mov	r0, #0
	str	r0, [r5, #-3156]
	str	r0, [r5, #-3152]
	str	r3, [r5, #-3160]
	b	.L3171
.L3163:
	ldr	r1, [r5, #-3316]
	ldr	r8, [r5, #-3368]
	ldr	r3, [r4, #2444]
	movw	r4, #10000
	ldr	r7, [r5, #-3160]
	cmp	r1, r4
	add	r2, r8, #1048576
	movhi	r4, #31
	movls	r4, #63
	cmp	r7, r2
	bhi	.L3173
	mov	r1, r1, lsr #10
	mov	r0, #1000
	mul	r0, r0, r3
	add	r1, r1, #1
	bl	__aeabi_uidiv
	add	r0, r0, r7
	cmp	r0, r8
	bcc	.L3173
	ldr	r3, .L3179+8
	ldrh	r3, [r3, #28]
	ands	r0, r4, r3
	movne	r0, r9
	bne	.L3171
	ldr	r2, [r5, #-3136]
	cmp	r2, r3
	beq	.L3171
.L3173:
	ldr	r3, .L3179+8
	mov	r0, #0
	str	r8, [r6, #-3160]
	str	r0, [r6, #-3152]
	ldrh	r3, [r3, #28]
	str	r3, [r6, #-3136]
	mov	r3, #1
	str	r3, [r6, #-3156]
.L3171:
	add	sp, sp, #44
	@ sp needed
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, pc}
.L3180:
	.align	2
.L3179:
	.word	.LANCHOR2
	.word	.LANCHOR0
	.word	.LANCHOR2-3604
	.fnend
	.size	FtlReadRefresh, .-FtlReadRefresh
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
	ldr	r4, .L3220
	ldr	r7, .L3220+4
	ldr	ip, [r4, #-3620]
	ldrh	r1, [r7]
	cmp	ip, #0
	bne	.L3218
	sub	r6, r4, #3424
	movw	lr, #65535
	ldrh	r5, [r6, #-8]
	cmp	r5, lr
	bne	.L3184
.L3193:
	ldrh	r2, [r6, #-8]
	movw	r3, #65535
	ldr	r5, .L3220
	mov	r8, #0
	cmp	r2, r3
	str	r8, [r4, #-376]
	sub	r9, r5, #3424
	beq	.L3218
	bl	FtlCacheWriteBack
	ldrh	r2, [r7]
	ldrb	r0, [r5, #-3425]	@ zero_extendqisi2
	sub	r7, r5, #2672
	ldrh	r3, [r9, #-8]
	sub	r10, r7, #760
	ldr	r1, [r5, #-3548]
	smulbb	r2, r0, r2
	mov	r3, r3, asl #1
	ldr	fp, .L3220+8
	strh	r2, [r1, r3]	@ movhi
	ldr	r2, [r5, #-3348]
	ldrh	r3, [r7]
	add	r3, r3, r2
	str	r3, [r5, #-3348]
	str	r5, [sp]
	b	.L3194
.L3184:
	cmp	r0, #0
	beq	.L3187
	ldr	r2, .L3220+12
	movw	r3, #3352
	ldrh	r0, [r2, r3]
	cmp	r0, lr
	beq	.L3188
.L3189:
	mov	r1, #2
	b	.L3187
.L3188:
	strh	ip, [r2, r3]	@ movhi
	sub	r3, r4, #3520
	ldrh	r3, [r3, #-12]
	cmp	r3, #17
	bhi	.L3189
.L3187:
	ldr	r0, .L3220+16
	bl	FtlGcScanTempBlk
	cmn	r0, #1
	str	r0, [sp, #12]
	beq	.L3190
	ldr	r3, .L3220
	mov	r5, r5, asl #1
	ldr	r2, [r3, #-3612]
	ldrh	r3, [r2, r5]
	cmp	r3, #4
	bls	.L3191
	sub	r3, r3, #5
	mov	r0, #1
	strh	r3, [r2, r5]	@ movhi
	bl	FtlEctTblFlush
.L3191:
	ldr	r3, [r4, #-376]
	ldr	r2, .L3220
	cmp	r3, #0
	bne	.L3192
	ldr	r0, [sp, #12]
	ldr	r3, [r2, #-3140]
	ubfx	r0, r0, #10, #16
	add	r3, r3, #1
	str	r3, [r2, #-3140]
	bl	FtlBbmMapBadBlock
	bl	FtlBbmTblFlush
.L3192:
	mov	r3, #0
	str	r3, [r4, #-376]
	b	.L3204
.L3190:
	ldr	r2, .L3220+12
	movw	r3, #3352
	ldrh	r2, [r2, r3]
	movw	r3, #65535
	cmp	r2, r3
	bne	.L3204
	b	.L3193
.L3197:
	ldr	r3, [r5, #4]
	cmp	r0, r3
	bne	.L3216
.L3196:
	add	r8, r8, #1
.L3194:
	ldrh	r2, [r7]
	uxth	r3, r8
	ldr	r9, .L3220+8
	cmp	r2, r3
	bls	.L3219
	mov	r9, #12
	ldr	r2, [fp, #2444]
	mul	r9, r9, r3
	ldr	r3, [sp]
	ldr	ip, [r3, #-2676]
	add	r5, ip, r9
	ldr	r0, [r5, #8]
	cmp	r0, r2
	bcs	.L3216
	mov	r2, #0
	add	r1, sp, #12
	str	ip, [sp, #4]
	bl	log2phys
	ldr	ip, [sp, #4]
	ldr	r0, [sp, #12]
	ldr	r2, [ip, r9]
	cmp	r0, r2
	bne	.L3197
	ubfx	r0, r0, #10, #16
	bl	P2V_block_in_plane
	add	r1, r5, #4
	mov	r2, #1
	mov	r9, r0
	ldr	r0, [r5, #8]
	bl	log2phys
	mov	r0, r9
	b	.L3217
.L3216:
	ldrh	r0, [r10]
.L3217:
	bl	decrement_vpc_count
	b	.L3196
.L3219:
	movw	r0, #65535
	bl	decrement_vpc_count
	ldrb	r3, [r9, #140]	@ zero_extendqisi2
	cmp	r3, #0
	beq	.L3199
	ldr	r0, .L3220+20
	ldrh	r1, [r6, #-8]
	bl	printk
.L3199:
	ldrh	r0, [r6, #-8]
	ldr	r2, [r4, #-3548]
	mov	r3, r0, asl #1
	ldrh	r3, [r2, r3]
	cmp	r3, #0
	beq	.L3200
	bl	INSERT_DATA_LIST
	b	.L3201
.L3200:
	bl	INSERT_FREE_LIST
.L3201:
	ldr	r5, .L3220
	mov	r4, #0
	mvn	r8, #0
	strh	r4, [r7]	@ movhi
	strh	r8, [r6, #-8]	@ movhi
	strh	r4, [r7, #-8]	@ movhi
	bl	l2p_flush
	bl	FtlVpcTblFlush
	sub	r3, r5, #3280
	sub	r2, r5, #3520
	strh	r8, [r3, #-8]	@ movhi
	ldr	r3, [r9, #2308]
	ldrh	r2, [r2, #-12]
	cmp	r3, r4
	sub	r3, r5, #3296
	ldrh	r3, [r3, #-12]
	beq	.L3202
	ldr	r1, [r5, #-3312]
	cmp	r1, #39
	bhi	.L3202
	cmp	r2, r3
	subcc	r5, r5, #2720
	movcc	r3, r3, asl #1
	strcch	r3, [r5, #-4]	@ movhi
	b	.L3218
.L3202:
	add	r1, r3, r3, asl #1
	cmp	r2, r1, asr #2
	ble	.L3218
	ldrb	r0, [r9, #140]	@ zero_extendqisi2
	ldr	r2, .L3220+24
	cmp	r0, #0
	moveq	r3, #20
	streqh	r3, [r2, #-4]	@ movhi
	beq	.L3183
	sub	r3, r3, #2
	strh	r3, [r2, #-4]	@ movhi
.L3218:
	mov	r0, #0
	b	.L3183
.L3204:
	mov	r0, #1
.L3183:
	add	sp, sp, #20
	@ sp needed
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L3221:
	.align	2
.L3220:
	.word	.LANCHOR2
	.word	.LANCHOR0+2384
	.word	.LANCHOR0
	.word	.LANCHOR1
	.word	.LANCHOR2-3432
	.word	.LC151
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
	stmfd	sp!, {r4, r5, r6, lr}
	.save {r4, r5, r6, lr}
	ldr	r6, .L3225
	ldr	r5, .L3225+4
	ldr	r4, .L3225+8
	mov	r0, r6
	ldrh	r1, [r5]
	bl	FtlGcScanTempBlk
	ldrh	r2, [r6, #2]
	ldrh	r3, [r5]
	cmp	r2, r3
	ldmccfd	sp!, {r4, r5, r6, pc}
	sub	r0, r4, #436
	bl	FtlMapBlkWriteDumpData
	mov	r0, #0
	bl	FtlGcFreeTempBlock
	mov	r3, #0
	str	r3, [r4, #-376]
	ldmfd	sp!, {r4, r5, r6, pc}
.L3226:
	.align	2
.L3225:
	.word	.LANCHOR2-3432
	.word	.LANCHOR0+2384
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
	ldr	r4, .L3229
	ldr	r3, .L3229+4
	sub	r5, r4, #3520
	sub	r4, r4, #3472
	sub	r5, r5, #8
	sub	r4, r4, #8
	str	r6, [r3, #1848]
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
.L3230:
	.align	2
.L3229:
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
	movw	r3, #1846
	ldr	r4, .L3249
	mov	r2, #0
	ldr	r6, .L3249+4
	ldr	r5, .L3249+8
	strh	r2, [r4, r3]	@ movhi
	movw	r3, #1844
	mvn	r2, #0
	strh	r2, [r4, r3]	@ movhi
	ldr	r3, [r6, #2320]
	uxth	r0, r3
	bl	FtlFreeSysBlkQueueInit
	bl	FtlScanSysBlk
	sub	r3, r5, #3296
	ldrh	r2, [r3, #-8]
	movw	r3, #65535
	cmp	r2, r3
	bne	.L3232
.L3234:
	mvn	r8, #0
	b	.L3233
.L3232:
	bl	FtlLoadSysInfo
	subs	r8, r0, #0
	bne	.L3234
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
	ldr	r2, [r5, #-3384]
	movw	r3, #2422
	mov	r0, #12
	ldrh	r1, [r6, r3]
	mov	r3, r8
.L3235:
	cmp	r3, r1
	bge	.L3240
	mla	ip, r0, r3, r2
	ldr	ip, [ip, #4]
	cmp	ip, #0
	bge	.L3236
.L3240:
	ldr	r4, .L3249+12
	cmp	r3, r1
	add	r9, r4, #84
	ldrh	r2, [r4, #28]
	add	r2, r2, #1
	strh	r2, [r4, #28]	@ movhi
	add	r2, r4, #76
	bge	.L3247
	b	.L3237
.L3236:
	add	r3, r3, #1
	b	.L3235
.L3247:
	movw	r3, #1846
	ldrh	r3, [r7, r3]
	cmp	r3, #0
	beq	.L3241
.L3237:
	ldrh	r3, [r9, #-8]
	ldr	r0, [r5, #-3548]
	ldrh	ip, [r2, #4]
	mov	r3, r3, asl #1
	ldrh	r1, [r0, r3]
	rsb	r1, ip, r1
	ldr	ip, .L3249+16
	strh	r1, [r0, r3]	@ movhi
	ldr	lr, [r5, #-3548]
	ldrh	r3, [ip]
	strh	r3, [r2, #2]	@ movhi
	mov	r3, #0
	strh	r3, [r2, #4]	@ movhi
	ldr	r2, .L3249+20
	strb	r3, [r5, #-3522]
	sub	r1, r2, #8
	ldrh	r2, [r2, #-8]
	ldrh	r7, [r1, #4]
	mov	r2, r2, asl #1
	ldrh	r0, [lr, r2]
	rsb	r0, r7, r0
	strh	r0, [lr, r2]	@ movhi
	ldrh	r2, [ip]
	strh	r3, [r1, #4]	@ movhi
	strb	r3, [r5, #-3474]
	ldrh	r3, [r4, #30]
	strh	r2, [r1, #2]	@ movhi
	add	r3, r3, #1
	strh	r3, [r4, #30]	@ movhi
	bl	l2p_flush
	bl	FtlVpcTblFlush
	bl	FtlVpcTblFlush
.L3241:
	ldrh	r0, [r9, #-8]
	movw	r3, #65535
	ldr	r5, .L3249+24
	cmp	r0, r3
	beq	.L3242
	ldrh	r3, [r5, #4]
	cmp	r3, #0
	bne	.L3242
	ldrh	r3, [r5, #52]
	add	r9, r5, #56
	add	r7, r5, #48
	cmp	r3, #0
	bne	.L3242
	bl	FtlGcRefreshOpenBlock
	ldrh	r0, [r5, #48]
	bl	FtlGcRefreshOpenBlock
	bl	FtlVpcTblFlush
	mov	r0, r5
	bl	allocate_new_data_superblock
	mov	r0, r7
	bl	allocate_new_data_superblock
.L3242:
	ldrb	r3, [r6]	@ zero_extendqisi2
	cmp	r3, #0
	bne	.L3243
	ldrh	r3, [r4, #28]
	tst	r3, #31
	bne	.L3233
.L3243:
	bl	FtlVpcCheckAndModify
.L3233:
	mov	r0, r8
	ldmfd	sp!, {r3, r4, r5, r6, r7, r8, r9, pc}
.L3250:
	.align	2
.L3249:
	.word	.LANCHOR4
	.word	.LANCHOR0
	.word	.LANCHOR2
	.word	.LANCHOR2-3604
	.word	.LANCHOR0+2384
	.word	.LANCHOR2-3472
	.word	.LANCHOR2-3528
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
	ldr	r4, .L3286
	ldr	r6, [r4, #-3620]
	cmp	r6, #0
	bne	.L3253
	ldr	r5, .L3286+4
	movw	r7, #2420
	mov	r1, r6
	ldr	r0, [r4, #-452]
	ldrh	r2, [r5, r7]
	mov	r2, r2, asl #2
	bl	ftl_memset
	ldrh	r2, [r5, r7]
	mov	r1, r6
	ldr	r0, [r4, #-456]
	mov	r2, r2, asl #2
	bl	ftl_memset
	ldr	r3, [r5, #2320]
	str	r6, [r4, #-3336]
	str	r6, [r4, #-3332]
	uxth	r0, r3
	bl	FtlFreeSysBlkQueueInit
	bl	FtlLoadBbt
	cmp	r0, #0
	beq	.L3254
	bl	FtlMakeBbt
.L3254:
	ldr	r0, .L3286+8
	mov	r2, #0
	ldr	ip, .L3286+12
.L3255:
	ldrh	r1, [r0]
	uxth	r3, r2
	add	r2, r2, #1
	cmp	r3, r1, asl #7
	bge	.L3283
	ldr	lr, [r4, #-512]
	mvn	r1, r3
	orr	r1, r3, r1, asl #16
	str	r1, [lr, r3, asl #2]
	ldr	r1, [r4, #-508]
	str	ip, [r1, r3, asl #2]
	b	.L3255
.L3283:
	ldr	r2, .L3286+4
	movw	r3, #2324
	ldr	r10, .L3286+16
	mov	r6, #0
	ldrh	r7, [r2, r3]
.L3257:
	ldrh	r3, [r10]
	ldr	r8, .L3286+4
	cmp	r3, r7
	ldr	r9, .L3286+16
	bls	.L3284
	mov	r0, r7
	mov	r1, #1
	bl	FtlLowFormatEraseBlock
	add	r7, r7, #1
	uxth	r7, r7
	add	r0, r6, r0
	uxth	r6, r0
	b	.L3257
.L3284:
	movw	r3, #2316
	ldrh	r1, [r8, r3]
	sub	r3, r6, #3
	cmp	r3, r1, asl #1
	bge	.L3259
.L3263:
	mov	r6, #0
	ldr	r7, .L3286+20
	mov	r8, r6
	b	.L3260
.L3259:
	mov	r0, r6
	bl	__aeabi_uidiv
	ldr	r3, [r8, #2416]
	add	r0, r0, r3
	uxth	r0, r0
	bl	FtlSysBlkNumInit
	ldr	r3, [r8, #2320]
	uxth	r0, r3
	bl	FtlFreeSysBlkQueueInit
	movw	r3, #2324
	ldrh	r6, [r8, r3]
.L3261:
	ldrh	r3, [r9]
	cmp	r3, r6
	bls	.L3263
	mov	r0, r6
	add	r6, r6, #1
	mov	r1, #1
	bl	FtlLowFormatEraseBlock
	uxth	r6, r6
	b	.L3261
.L3260:
	ldrh	r3, [r7]
	uxth	r0, r6
	ldr	fp, .L3286+4
	add	r6, r6, #1
	cmp	r3, r0
	bls	.L3285
	mov	r1, #0
	bl	FtlLowFormatEraseBlock
	add	r0, r8, r0
	uxth	r8, r0
	b	.L3260
.L3285:
	movw	r3, #2326
	ldr	r2, [fp, #2328]
	ldrh	r3, [fp, r3]
	ldr	r6, .L3286+24
	mov	r0, r2
	str	r2, [sp, #4]
	str	r3, [r4, #-552]
	movw	r3, #2316
	ldrh	r7, [fp, r3]
	mov	r1, r7
	bl	__aeabi_uidiv
	ubfx	r9, r0, #5, #16
	mov	r10, r0
	add	r3, r9, #36
	strh	r3, [r6, #-12]	@ movhi
	mov	r3, #24
	str	r0, [fp, #2444]
	mul	r3, r3, r7
	ldr	r2, [sp, #4]
	cmp	r8, r3
	ble	.L3265
	rsb	r0, r8, r2
	mov	r1, r7
	bl	__aeabi_uidiv
	str	r0, [fp, #2444]
	mov	r0, r0, lsr #5
	add	r0, r0, #24
	strh	r0, [r6, #-12]	@ movhi
.L3265:
	ldr	r3, [r5, #2308]
	cmp	r3, #1
	bne	.L3266
	mov	r0, r8
	mov	r1, r7
	bl	__aeabi_uidiv
	ldrh	fp, [r6, #-12]
	uxtah	r0, fp, r0
	add	fp, fp, r0, asr #2
	strh	fp, [r6, #-12]	@ movhi
.L3266:
	ldrb	r3, [r5, #140]	@ zero_extendqisi2
	cmp	r3, #0
	beq	.L3267
	mov	r0, r8
	mov	r1, r7
	bl	__aeabi_uidiv
	ldrh	fp, [r6, #-12]
	uxtah	r0, fp, r0
	add	fp, fp, r0, asr #2
	strh	fp, [r6, #-12]	@ movhi
.L3267:
	movw	r3, #2378
	ldrh	r3, [r5, r3]
	cmp	r3, #0
	beq	.L3269
	ldrh	r2, [r6, #-12]
	add	r2, r2, r3, lsr #1
	strh	r2, [r6, #-12]	@ movhi
	mul	r2, r7, r3
	cmp	r2, r8
	addgt	r3, r3, #32
	ldrgt	r2, .L3286+4
	addgt	r3, r9, r3
	strgt	r10, [r2, #2444]
	ldrgt	r2, .L3286+24
	strgth	r3, [r2, #-12]	@ movhi
.L3269:
	ldrh	r2, [r6, #-12]
	mvn	r9, #0
	ldr	r3, [r5, #2444]
	rsb	r3, r2, r3
	mul	r7, r7, r3
	ldr	r3, .L3286+28
	ldrh	r3, [r3]
	str	r7, [r4, #1784]
	mul	r7, r3, r7
	movw	r3, #2390
	ldrh	r3, [r5, r3]
	str	r7, [r5, #2444]
	mul	r7, r3, r7
	str	r7, [r5, #2424]
	bl	FtlBbmTblFlush
	movw	r3, #2326
	ldr	r7, .L3286+32
	ldrh	r2, [r5, r3]
	mov	r1, #0
	sub	r8, r7, #248
	ldr	r0, [r4, #-3548]
	mov	r2, r2, asl #1
	mov	fp, r8
	bl	ftl_memset
	mov	r3, #0
	strh	r3, [r7, #-6]	@ movhi
	mov	r1, #255
	strh	r3, [r7, #-248]	@ movhi
	str	r3, [r4, #-3376]
	strb	r3, [r4, #-3282]
	strb	r3, [r4, #-3280]
	strh	r3, [r8, #2]	@ movhi
	strb	r3, [r4, #-3522]
	mov	r3, #1
	strb	r3, [r4, #-3520]
	movw	r3, #2324
	ldrh	r2, [r5, r3]
	ldr	r0, [r4, #-3372]
	strh	r9, [r7, #-8]	@ movhi
	add	r7, r7, #3280
	mov	r2, r2, lsr #3
	bl	ftl_memset
.L3271:
	ldr	r10, .L3286+36
	ldr	r5, .L3286
	mov	r0, r10
	bl	make_superblock
	ldrb	r3, [r4, #-3521]	@ zero_extendqisi2
	cmp	r3, #0
	ldrh	r3, [r8]
	bne	.L3272
	ldr	r2, [r7, #-3548]
	mov	r3, r3, asl #1
	strh	r9, [r2, r3]	@ movhi
	ldrh	r3, [fp]
	add	r3, r3, #1
	strh	r3, [fp]	@ movhi
	b	.L3271
.L3272:
	ldr	r2, [r5, #-3336]
	mov	r3, r3, asl #1
	ldrh	r1, [r10, #4]
	mov	r9, r5
	str	r2, [r5, #-3516]
	add	r2, r2, #1
	str	r2, [r5, #-3336]
	ldr	r2, [r5, #-3548]
	strh	r1, [r2, r3]	@ movhi
	sub	r2, r5, #3472
	sub	r7, r2, #8
	mov	r3, #0
	strb	r3, [r5, #-3474]
	strh	r3, [r7, #2]	@ movhi
	ldrh	r3, [r10]
	mvn	r10, #0
	add	r3, r3, #1
	strh	r3, [r2, #-8]	@ movhi
	mov	r3, #1
	strb	r3, [r5, #-3472]
.L3273:
	ldr	r8, .L3286+40
	ldr	r5, .L3286
	mov	r0, r8
	bl	make_superblock
	ldrb	r3, [r4, #-3473]	@ zero_extendqisi2
	cmp	r3, #0
	ldrh	r3, [r7]
	bne	.L3274
	ldr	r2, [r9, #-3548]
	mov	r3, r3, asl #1
	strh	r10, [r2, r3]	@ movhi
	ldrh	r3, [r7]
	add	r3, r3, #1
	strh	r3, [r7]	@ movhi
	b	.L3273
.L3274:
	ldr	r2, [r5, #-3336]
	mov	r3, r3, asl #1
	ldrh	r1, [r8, #4]
	mvn	r4, #0
	str	r2, [r5, #-3468]
	add	r2, r2, #1
	str	r2, [r5, #-3336]
	ldr	r2, [r5, #-3548]
	strh	r1, [r2, r3]	@ movhi
	sub	r3, r5, #3424
	strh	r4, [r3, #-8]	@ movhi
	bl	FtlFreeSysBlkQueueOut
	ldr	r3, .L3286+44
	mov	r2, #0
	strh	r2, [r3, #2]	@ movhi
	ldr	r2, [r5, #1784]
	strh	r4, [r3, #4]	@ movhi
	strh	r2, [r3, #6]	@ movhi
	ldr	r3, [r5, #-3336]
	str	r3, [r5, #-3296]
	add	r3, r3, #1
	str	r3, [r5, #-3336]
	strh	r0, [r6, #-8]	@ movhi
	bl	FtlVpcTblFlush
	bl	FtlSysBlkInit
	cmp	r0, #0
	ldreq	r3, .L3286+48
	moveq	r2, #1
	streq	r2, [r3, #504]
.L3253:
	mov	r0, #0
	add	sp, sp, #12
	@ sp needed
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L3287:
	.align	2
.L3286:
	.word	.LANCHOR2
	.word	.LANCHOR0
	.word	.LANCHOR0+2390
	.word	168778952
	.word	.LANCHOR0+2326
	.word	.LANCHOR0+2324
	.word	.LANCHOR2-3296
	.word	.LANCHOR0+2384
	.word	.LANCHOR2-3280
	.word	.LANCHOR2-3528
	.word	.LANCHOR2-3480
	.word	.LANCHOR2-3304
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
	ldr	r4, .L3317
	ldrb	r3, [r4, #140]	@ zero_extendqisi2
	cmp	r3, #0
	beq	.L3289
.L3291:
	mov	r0, #0
	b	.L3290
.L3289:
	ldr	r5, .L3317+4
	ldr	r0, [r5, #1680]
	bl	FlashInit
	cmp	r0, #0
	bne	.L3291
	bl	FlashLoadFactorBbt
	cmp	r0, #0
	beq	.L3292
	bl	FlashMakeFactorBbt
.L3292:
	ldr	r0, [r5, #1712]
	bl	FlashReadIdbDataRaw
	cmp	r0, #0
	beq	.L3293
	mov	r1, #0
	mov	r2, #16
	mov	r0, sp
	bl	FlashReadFacBbtData
	mov	r3, #0
	mov	r2, r3
	mov	ip, #1
	ldr	r1, [sp]
.L3294:
	ands	lr, r1, ip, asl r2
	add	r0, r3, #1
	add	r2, r2, #1
	movne	r3, r0
	cmp	r2, #16
	bne	.L3294
	cmp	r3, #6
	bls	.L3313
	mov	r2, #0
	mov	ip, #1
.L3297:
	ands	lr, r1, ip, asl r2
	add	r0, r3, #1
	add	r2, r2, #1
	movne	r3, r0
	cmp	r2, #24
	bne	.L3297
	cmp	r3, #17
	movhi	r3, #36
	strhib	r3, [r4, #1]
	bhi	.L3296
.L3313:
	strb	r2, [r4, #1]
.L3296:
	ldrb	r3, [r4, #1]	@ zero_extendqisi2
	strh	r3, [r4, #138]	@ movhi
.L3293:
	ldr	r1, .L3317+8
	ldr	r0, .L3317+12
	bl	printk
	ldr	r0, .L3317+16
	bl	FtlConstantsInit
	bl	FtlVariablesInit
	ldr	r0, [r4, #2320]
	mov	r4, #1
	uxth	r0, r0
	bl	FtlFreeSysBlkQueueInit
.L3299:
	bl	FtlLoadBbt
	cmp	r0, #0
	beq	.L3300
.L3315:
	bl	FtlLowFormat
	cmp	r4, #3
	addls	r4, r4, #1
	bls	.L3299
.L3316:
	mvn	r0, #0
	b	.L3290
.L3300:
	bl	FtlSysBlkInit
	cmp	r0, #0
	bne	.L3315
	ldr	r3, .L3317+20
	mov	r2, #1
	str	r2, [r3, #504]
.L3290:
	add	sp, sp, #20
	@ sp needed
	ldmfd	sp!, {r4, r5, pc}
.L3318:
	.align	2
.L3317:
	.word	.LANCHOR0
	.word	.LANCHOR2
	.word	.LC77
	.word	.LC76
	.word	.LANCHOR0+112
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
	ldr	r5, .L3336
	ldr	r3, [r5, #-3620]
	cmp	r3, #0
	beq	.L3320
.L3323:
	mov	r0, #0
	ldmfd	sp!, {r4, r5, r6, r7, r8, pc}
.L3320:
	ldr	r3, .L3336+4
	ldrb	r3, [r3, #140]	@ zero_extendqisi2
	cmp	r3, #0
	beq	.L3322
	ldr	r3, [r5, #-2708]
	tst	r3, #1
	beq	.L3322
	ldr	r3, .L3336+8
	ldrh	r3, [r3, #4]
	cmp	r3, #0
	bne	.L3323
.L3322:
	mov	r2, #0
	ldr	r0, [r5, #-536]
	ldr	r1, [r5, #-2708]
	mov	r3, r2
	bl	FlashProgPages
	ldr	r7, .L3336
	mov	r6, #0
	mov	r8, #36
.L3324:
	ldr	r1, [r5, #-2708]
	uxth	r3, r6
	ldr	r4, .L3336
	cmp	r3, r1
	bcs	.L3335
	mul	r3, r8, r3
	ldr	r0, [r7, #-536]
	add	r6, r6, #1
	add	r1, r0, r3
	ldr	r0, [r0, r3]
	ldr	r2, [r1, #12]
	cmn	r0, #1
	bne	.L3325
	sub	r1, r4, #3424
	ldr	ip, [r4, #-3548]
	mov	lr, #0
	ldrh	r2, [r1, #-8]
	mov	r2, r2, asl #1
	strh	lr, [ip, r2]	@ movhi
	ldr	r2, [r4, #-3140]
	strh	r0, [r1, #-8]	@ movhi
	add	r2, r2, #1
	str	r2, [r4, #-3140]
	ldr	r2, [r4, #-536]
	add	r3, r2, r3
	ldr	r0, [r3, #4]
	ubfx	r0, r0, #10, #16
	bl	FtlBbmMapBadBlock
	bl	FtlBbmTblFlush
	bl	FtlGcPageVarInit
	b	.L3334
.L3325:
	ldr	r0, [r2, #12]
	ldr	r1, [r1, #4]
	ldr	r2, [r2, #8]
	bl	FtlGcUpdatePage
	b	.L3324
.L3335:
	ldr	r0, [r4, #-536]
	bl	FtlGcBufFree
	mov	r3, #0
	str	r3, [r4, #-2708]
	ldr	r3, .L3336+8
	ldrh	r3, [r3, #4]
	cmp	r3, #0
	bne	.L3323
	mov	r0, #1
	bl	FtlGcFreeTempBlock
.L3334:
	mov	r0, #1
	ldmfd	sp!, {r4, r5, r6, r7, r8, pc}
.L3337:
	.align	2
.L3336:
	.word	.LANCHOR2
	.word	.LANCHOR0
	.word	.LANCHOR2-3432
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
	ldr	r3, .L3342
	sub	r2, r3, #8
	ldrh	r1, [r3, #-8]
	movw	r3, #65535
	cmp	r1, r3
	beq	.L3339
	ldrh	r3, [r2, #4]
	cmp	r3, #0
	bne	.L3340
.L3339:
	ldr	r4, .L3342+4
	bl	FtlCacheWriteBack
	mov	r0, #0
	mov	r5, #0
	bl	FtlGcFreeTempBlock
	sub	r4, r4, #2672
	strb	r5, [r4, #-752]
	ldr	r0, .L3342+8
	bl	allocate_data_superblock
	strh	r5, [r4, #-8]	@ movhi
	strh	r5, [r4]	@ movhi
	bl	l2p_flush
	mov	r0, r5
	bl	FtlEctTblFlush
	bl	FtlVpcTblFlush
.L3340:
	ldr	r0, .L3342+8
	ldmfd	sp!, {r3, r4, r5, lr}
	b	get_new_active_ppa
.L3343:
	.align	2
.L3342:
	.word	.LANCHOR2-3424
	.word	.LANCHOR2
	.word	.LANCHOR2-3432
	.fnend
	.size	Ftl_get_new_temp_ppa, .-Ftl_get_new_temp_ppa
	.align	2
	.global	ftl_do_gc
	.type	ftl_do_gc, %function
ftl_do_gc:
	.fnstart
	@ args = 0, pretend = 0, frame = 32
	@ frame_needed = 0, uses_anonymous_args = 0
	ldr	ip, .L3508
	stmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.save {r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.pad #44
	sub	sp, sp, #44
	ldr	lr, [ip, #-3620]
	cmp	lr, #0
	movne	r0, #0
	bne	.L3497
	ldr	r9, .L3508+4
	ldr	r3, [r9, #504]
	cmn	r3, #1
	beq	.L3440
	ldr	r2, [ip, #-564]
	cmp	r2, #0
	bne	.L3440
	sub	r3, ip, #3536
	ldrh	r3, [r3, #-4]
	cmp	r3, #47
	movls	r0, r2
	bls	.L3497
	movw	r3, #3352
	mov	r6, ip
	ldrh	r2, [r9, r3]
	movw	r3, #65535
	str	r1, [sp, #8]
	cmp	r2, r3
	str	r0, [sp, #12]
	bne	.L3346
.L3349:
	ldr	r5, .L3508+8
	movw	r2, #65535
	ldrh	r4, [r5, #-12]
	cmp	r4, r2
	bne	.L3347
	b	.L3348
.L3346:
	sub	ip, ip, #3424
	ldrh	r2, [ip, #-8]
	cmp	r2, r3
	beq	.L3349
	mov	r0, #1
	bl	FtlGcFreeTempBlock
	cmp	r0, #0
	beq	.L3349
	mov	r0, #1
	b	.L3497
.L3347:
	ldrh	r3, [r5, #-14]
	cmp	r3, r2
	bne	.L3348
	ldrh	r1, [r5, #-10]
	cmp	r1, r3
	beq	.L3348
	ldrh	r2, [r5, #-8]
	cmp	r2, r3
	strneh	r4, [r5, #-14]	@ movhi
	strneh	r1, [r5, #-12]	@ movhi
	mvnne	r3, #0
	strneh	r2, [r5, #-10]	@ movhi
	strneh	r3, [r5, #-8]	@ movhi
.L3348:
	ldr	r2, [sp, #12]
	ldr	r3, [r6, #-2720]
	ldr	r7, .L3508+12
	cmp	r2, #1
	add	r3, r3, #1
	add	r3, r3, r2, asl #7
	sub	r8, r7, #240
	str	r3, [r6, #-2720]
	bne	.L3360
	ldr	r4, .L3508+16
	ldr	r2, [r4, #2308]
	cmp	r2, #0
	bne	.L3351
	ldrb	r2, [r4, #140]	@ zero_extendqisi2
	cmp	r2, #0
	beq	.L3360
.L3351:
	ldr	r2, [r6, #-3312]
	ldr	r10, .L3508
	cmp	r2, #39
	bhi	.L3360
	ldr	r9, .L3508+20
	movw	r2, #1980
	ldrh	r2, [r9, r2]
	add	r3, r3, r2
	str	r3, [r10, #-2720]
	bl	FtlGcReFreshBadBlk
	ldrh	r3, [r7, #-8]
	movw	r2, #65535
	cmp	r3, r2
	bne	.L3360
	ldrh	r2, [r5, #-14]
	cmp	r2, r3
	bne	.L3360
	ldr	r3, [r10, #-2720]
	cmp	r3, #1024
	bhi	.L3352
	ldrh	r3, [r8, #-12]
	cmp	r3, #63
	bhi	.L3360
.L3352:
	ldr	r1, .L3508
	movw	r2, #1980
	ldrh	lr, [r8, #-12]
	mov	ip, #0
	sub	r3, r1, #2720
	ldr	r0, .L3508+20
	strh	ip, [r9, r2]	@ movhi
	ldrh	r3, [r3, #-2]
	add	r3, r3, #64
	cmp	lr, r3
	bgt	.L3360
	ldr	r3, [r1, #-3312]
	str	ip, [r1, #-2720]
	cmp	r3, ip
	moveq	r3, #6
	beq	.L3499
	cmp	r3, #5
	bhi	.L3354
	mov	r3, #18
.L3499:
	strh	r3, [r0, r2]	@ movhi
.L3354:
	mov	r0, #32
	bl	List_get_gc_head_node
	movw	ip, #65535
	uxth	r2, r0
	cmp	r2, ip
	beq	.L3359
	ldr	r10, .L3508
	sub	r9, r10, #2704
	ldrh	r0, [r9, #-12]
	cmp	r0, #0
	beq	.L3356
	movw	lr, #2386
	movw	fp, #2316
	ldrh	lr, [r4, lr]
	mov	r2, r2, asl #1
	ldrh	r4, [r4, fp]
	ldr	r3, [r10, #-3548]
	mul	lr, r4, lr
	ldrh	r1, [r3, r2]
	add	lr, lr, #1
	cmp	r1, lr
	bgt	.L3359
	add	r1, r0, #1
	str	r2, [sp, #28]
	str	ip, [sp, #24]
	mov	fp, #0
	uxth	r1, r1
	str	r3, [sp, #20]
	strh	r1, [r9, #-12]	@ movhi
	str	r1, [sp, #16]
	str	fp, [r10, #-2712]
	bl	List_get_gc_head_node
	ldr	ip, [sp, #24]
	uxth	r4, r0
	ldr	r1, [sp, #16]
	cmp	r4, ip
	ldr	r3, [sp, #20]
	ldr	r2, [sp, #28]
	beq	.L3359
	mov	ip, r4, asl #1
	ldr	r0, .L3508+24
	str	ip, [sp, #16]
	ldrh	lr, [r3, ip]
	ldrh	r3, [r3, r2]
	mov	r2, r4
	str	r3, [sp]
	mov	r3, lr
	bl	printk
	ldrh	r3, [r9, #-12]
	cmp	r3, #40
	ldr	ip, [sp, #16]
	bls	.L3357
	ldr	r3, [r10, #-3548]
	ldrh	r3, [r3, ip]
	cmp	r3, #32
	strhih	fp, [r9, #-12]	@ movhi
.L3357:
	ldr	r2, .L3508+20
	movw	r3, #1980
	mov	r1, #6
	strh	r1, [r2, r3]	@ movhi
	b	.L3361
.L3356:
	mov	r3, #1
	strh	r3, [r9, #-12]	@ movhi
.L3359:
	bl	GetSwlReplaceBlock
	movw	r3, #65535
	cmp	r0, r3
	mov	r4, r0
	bne	.L3361
	ldr	r2, .L3508+20
	movw	r3, #1980
	mov	r1, #0
	strh	r1, [r2, r3]	@ movhi
.L3360:
	ldrh	r3, [r7, #-8]
	movw	r4, #65535
	ldr	r9, .L3508
	cmp	r3, r4
	bne	.L3361
	sub	r2, r9, #3424
	ldrh	r4, [r2, #-8]
	cmp	r4, r3
	movne	r4, r3
	beq	.L3504
.L3361:
	movw	r1, #65535
	rsb	r3, r1, r4
	clz	r3, r3
	ldr	r2, [sp, #12]
	ldr	r9, .L3508+28
	mov	r3, r3, lsr #5
	cmp	r2, #0
	movne	ip, #0
	andeq	ip, r3, #1
	cmp	ip, #0
	beq	.L3374
	ldrh	r0, [r9, #-12]
	cmp	r0, #24
	movhi	fp, #1
	bhi	.L3375
	ldr	r3, .L3508+32
	cmp	r0, #16
	ldrh	fp, [r3]
	movhi	fp, fp, lsr #5
	bhi	.L3375
	cmp	r0, #12
	movhi	fp, fp, lsr #4
	bhi	.L3375
	cmp	r0, #8
	movhi	fp, fp, lsr #2
.L3375:
	ldr	r1, .L3508
	sub	r2, r1, #2720
	ldrh	r3, [r2, #-4]
	cmp	r3, r0
	bcs	.L3379
	sub	r3, r1, #3424
	movw	r0, #65535
	ldrh	r3, [r3, #-8]
	cmp	r3, r0
	bne	.L3380
	ldrh	r0, [r5, #-14]
	cmp	r0, r3
	bne	.L3380
	ldr	r0, .L3508+20
	movw	r3, #1980
	ldrh	r0, [r0, r3]
	cmp	r0, #0
	bne	.L3381
	ldr	r3, .L3508+16
	ldr	r1, [r1, #-3376]
	ldr	r3, [r3, #2444]
	add	r3, r3, r3, asl #1
	cmp	r1, r3, lsr #2
	movcs	r3, #18
	bcs	.L3382
.L3381:
	ldr	r3, .L3508+36
	ldrh	r3, [r3, #-12]
	add	r3, r3, r3, asl #1
	ubfx	r3, r3, #2, #16
.L3382:
	strh	r3, [r2, #-4]	@ movhi
	mov	r3, #0
	str	r3, [r6, #-2712]
	b	.L3497
.L3380:
	ldr	r3, .L3508+36
	ldrh	r3, [r3, #-12]
	add	r3, r3, r3, asl #1
	mov	r3, r3, asr #2
	strh	r3, [r2, #-4]	@ movhi
.L3379:
	ldr	r3, .L3508+16
	movw	r4, #65535
	ldr	r2, [r3, #2308]
	ldr	r3, [sp, #8]
	cmp	r3, #2
	movhi	r3, #0
	movls	r3, #1
	cmp	r2, #0
	moveq	r3, #0
	cmp	r3, #0
	addne	fp, fp, #1
	uxthne	fp, fp
	b	.L3384
.L3374:
	ldrh	r0, [r9, #88]
	add	r2, r9, #3520
	cmp	r0, r1
	bne	.L3385
	ldrh	r1, [r5, #-14]
	cmp	r1, r0
	movne	r3, #0
	andeq	r3, r3, #1
	cmp	r3, #0
	beq	.L3385
	sub	r3, r2, #3280
	ldrh	r3, [r3, #-8]
	cmp	r3, r0
	beq	.L3386
.L3390:
	movw	r4, #65535
	b	.L3385
.L3386:
	sub	r10, r2, #2720
	ldrh	r1, [r8, #-12]
	str	ip, [r2, #-2712]
	ldrh	r3, [r10, #-4]
	ldr	fp, .L3508+20
	cmp	r1, r3
	bls	.L3387
	movw	r3, #1980
	ldrh	r3, [fp, r3]
	cmp	r3, #0
	bne	.L3388
	ldr	r3, .L3508+16
	ldr	r2, [r2, #-3376]
	ldr	r3, [r3, #2444]
	add	r3, r3, r3, asl #1
	cmp	r2, r3, lsr #2
	movcs	r3, #18
	bcs	.L3389
.L3388:
	ldr	r3, .L3508+36
	ldrh	r3, [r3, #-12]
	add	r3, r3, r3, asl #1
	ubfx	r3, r3, #2, #16
.L3389:
	strh	r3, [r10, #-4]	@ movhi
	bl	FtlReadRefresh
	mov	r0, #0
	bl	List_get_gc_head_node
	ldr	r3, [r6, #-3548]
	uxth	r0, r0
	mov	r0, r0, asl #1
	ldrh	r3, [r3, r0]
	cmp	r3, #4
	movwhi	r3, #1980
	ldrhih	r0, [fp, r3]
	bhi	.L3497
.L3387:
	movw	r4, #1980
	ldrh	r0, [fp, r4]
	cmp	r0, #0
	bne	.L3390
	ldr	r3, .L3508+36
	ldrh	fp, [r3, #-12]
	add	r3, fp, fp, asl #1
	mov	r3, r3, asr #2
	strh	r3, [r10, #-4]	@ movhi
	bl	List_get_gc_head_node
	ldr	r3, [r6, #-3548]
	movw	r2, #2386
	uxth	r0, r0
	mov	r0, r0, asl #1
	ldrh	r1, [r3, r0]
	movw	r0, #2316
	ldr	r3, .L3508+16
	ldrh	r2, [r3, r2]
	ldrh	r3, [r3, r0]
	mul	r3, r3, r2
	add	r3, r3, r3, lsr #31
	cmp	r1, r3, asr #1
	ble	.L3391
	ldrh	r3, [r8, #-12]
	sub	r2, fp, #1
	cmp	r3, r2
	blt	.L3391
	bl	FtlReadRefresh
	ldr	r3, .L3508+20
	ldrh	r0, [r3, r4]
	b	.L3497
.L3391:
	cmp	r1, #0
	bne	.L3390
	movw	r0, #65535
	bl	decrement_vpc_count
	ldrh	r0, [r9, #-12]
	add	r0, r0, #1
	b	.L3497
.L3385:
	ldr	r3, .L3508+16
	ldr	r3, [r3, #2308]
	cmp	r3, #0
	moveq	fp, #1
	movne	fp, #2
.L3384:
	ldrh	r3, [r7, #-8]
	movw	r0, #65535
	ldr	r2, .L3508
	cmp	r3, r0
	sub	r1, r2, #3280
	bne	.L3393
	cmp	r4, r3
	strneh	r4, [r1, #-8]	@ movhi
	bne	.L3395
	ldrh	r3, [r5, #-14]
	sub	r1, r2, #2656
	cmp	r3, r4
	beq	.L3395
	ldr	r2, [r2, #-3548]
	mov	r3, r3, asl #1
	ldrh	r3, [r2, r3]
	cmp	r3, #0
	mvneq	r3, #0
	streqh	r3, [r1, #-14]	@ movhi
	ldrh	r3, [r5, #-14]
	strh	r3, [r7, #-8]	@ movhi
	mvn	r3, #0
	strh	r3, [r5, #-14]	@ movhi
.L3395:
	ldrh	r0, [r7, #-8]
	mov	r3, #0
	strb	r3, [r6, #-3280]
	movw	r3, #65535
	cmp	r0, r3
	beq	.L3393
	bl	IsBlkInGcList
	cmp	r0, #0
	ldrne	r3, .L3508+12
	mvnne	r2, #0
	strneh	r2, [r3, #-8]	@ movhi
	ldr	r3, .L3508+16
	ldrb	r3, [r3, #140]	@ zero_extendqisi2
	cmp	r3, #0
	beq	.L3399
	ldrh	r0, [r7, #-8]
	bl	ftl_get_blk_mode
	strb	r0, [r6, #-3280]
.L3399:
	ldrh	r2, [r7, #-8]
	movw	r3, #65535
	ldr	r6, .L3508
	cmp	r2, r3
	sub	r8, r6, #3280
	sub	r5, r8, #8
	beq	.L3393
	mov	r0, r5
	bl	make_superblock
	ldr	r3, .L3508+20
	movw	r1, #1982
	mov	r2, #0
	strh	r2, [r8, #-6]	@ movhi
	strh	r2, [r3, r1]	@ movhi
	add	r3, r3, #1984
	strb	r2, [r6, #-3282]
	ldrh	r2, [r8, #-8]
	ldr	r1, [r6, #-3548]
	mov	r2, r2, asl #1
	ldrh	r2, [r1, r2]
	strh	r2, [r3]	@ movhi
.L3393:
	ldrh	r3, [r7, #-8]
	ldrh	r2, [r9, #-8]
	cmp	r2, r3
	beq	.L3400
	ldr	r2, .L3508+40
	ldrh	r1, [r2, #-8]
	cmp	r1, r3
	beq	.L3400
	ldrh	r2, [r2, #40]
	cmp	r2, r3
	bne	.L3434
.L3400:
	ldr	r3, .L3508+12
	mvn	r2, #0
	strh	r2, [r3, #-8]	@ movhi
.L3434:
	ldr	r7, .L3508+44
	movw	r3, #65535
	ldr	r6, .L3508
	ldrh	r2, [r7]
	mov	r10, r7
	mov	r8, r6
	cmp	r2, r3
	bne	.L3402
	mov	r3, #0
	str	r3, [r6, #-2712]
.L3403:
	ldr	r9, .L3508+48
	ldrh	r5, [r9]
	mov	r0, r5
	bl	List_get_gc_head_node
	movw	r2, #65535
	uxth	r3, r0
	strh	r3, [r10]	@ movhi
	cmp	r3, r2
	moveq	r3, #0
	moveq	r0, #8
	streqh	r3, [r9]	@ movhi
	beq	.L3497
.L3404:
	mov	r0, r3
	str	r3, [sp, #8]
	bl	IsBlkInGcList
	add	r5, r5, #1
	cmp	r0, #0
	ldr	r3, [sp, #8]
	ldrne	r3, .L3508+48
	strneh	r5, [r3]	@ movhi
	bne	.L3403
	ldr	r2, .L3508+48
	uxth	r5, r5
	ldr	r0, [r8, #-3548]
	mov	r1, r3, asl #1
	strh	r5, [r2]	@ movhi
	ldr	r2, .L3508+32
	ldrh	ip, [r0, r1]
	ldrh	lr, [r2]
	ldrh	r2, [r2, #-68]
	mul	r2, r2, lr
	add	lr, r2, r2, lsr #31
	cmp	ip, lr, asr #1
	bgt	.L3407
	cmp	ip, #8
	cmphi	r5, #48
	bls	.L3408
	ldrh	ip, [r9, #36]
	cmp	ip, #35
	bhi	.L3408
.L3407:
	ldr	lr, .L3508+48
	mov	ip, #0
	strh	ip, [lr]	@ movhi
.L3408:
	ldrh	r1, [r0, r1]
	movw	r0, #65535
	cmp	r1, r2
	cmpge	r4, r0
	bne	.L3409
	ldr	r2, .L3508+48
	ldrh	r0, [r2]
	cmp	r0, #3
	bhi	.L3409
	sub	r3, r2, #564
	mvn	r1, #0
	strh	r1, [r3, #-8]	@ movhi
	mov	r3, #0
	strh	r3, [r2]	@ movhi
	b	.L3502
.L3409:
	cmp	r1, #0
	bne	.L3410
	movw	r0, #65535
	bl	decrement_vpc_count
	ldr	r3, .L3508+48
	ldr	r2, .L3508+48
	ldrh	r3, [r3]
	add	r3, r3, #1
	strh	r3, [r2]	@ movhi
	b	.L3403
.L3410:
	mov	r2, #0
	strb	r2, [r6, #-3280]
	ldr	r2, .L3508+16
	ldrb	r2, [r2, #140]	@ zero_extendqisi2
	cmp	r2, #0
	beq	.L3411
	mov	r0, r3
	bl	ftl_get_blk_mode
	ldr	r3, .L3508
	strb	r0, [r3, #-3280]
.L3411:
	ldr	r0, .L3508+44
	bl	make_superblock
	ldrh	r2, [r7]
	ldr	r1, .L3508+52
	mov	r3, #0
	ldr	r0, [r6, #-3548]
	mov	r2, r2, asl #1
	strh	r3, [r1]	@ movhi
	ldrh	r2, [r0, r2]
	strh	r3, [r7, #2]	@ movhi
	strb	r3, [r6, #-3282]
	strh	r2, [r1, #2]	@ movhi
.L3402:
	ldr	r3, [sp, #12]
	cmp	r3, #1
	bne	.L3412
	bl	FtlReadRefresh
.L3412:
	mov	r3, #1
	str	r3, [r6, #-564]
	ldr	r3, .L3508+16
	add	r2, r3, #2384
	ldrb	r3, [r3, #140]	@ zero_extendqisi2
	ldrh	r2, [r2]
	cmp	r3, #0
	str	r2, [sp, #8]
	beq	.L3413
	ldr	r3, .L3508
	ldrb	r3, [r3, #-3280]	@ zero_extendqisi2
	cmp	r3, #1
	ldreq	r3, .L3508+56
	ldreqh	r3, [r3]
	streq	r3, [sp, #8]
.L3413:
	ldrh	r3, [r7, #2]
	mov	r7, #0
	ldr	r1, [sp, #8]
	add	r2, r3, fp
	ldr	r5, .L3508
	cmp	r2, r1
	movgt	r2, r1
	rsbgt	r3, r3, r2
	uxthgt	fp, r3
	b	.L3415
.L3507:
	ldr	r0, [r5, #-2692]
	mov	r1, r6
	ldrb	r2, [r5, #-3280]	@ zero_extendqisi2
	mov	r9, #0
	bl	FlashReadPages
.L3418:
	uxth	r3, r9
	cmp	r3, r6
	bcs	.L3505
	mov	r3, #36
	ldr	r2, [r5, #-2692]
	mul	r8, r3, r9
	add	r1, r2, r8
	ldr	r2, [r2, r8]
	ldr	r10, [r1, #12]
	cmn	r2, #1
	beq	.L3454
	ldrh	r1, [r10]
	movw	r2, #61589
	cmp	r1, r2
	bne	.L3454
	add	r1, sp, #32
	mov	r2, #0
	ldr	r0, [r10, #8]
	str	r3, [sp, #16]
	bl	log2phys
	ldr	r2, [r5, #-2692]
	add	r2, r2, r8
	ldr	r0, [r2, #4]
	ldr	r1, [sp, #32]
	ldr	r3, [sp, #16]
	bic	r1, r1, #-2147483648
	cmp	r1, r0
	bne	.L3454
	ldr	r1, .L3508+52
	ldr	r0, .L3508+52
	ldr	r2, [r2, #16]
	ldrh	r1, [r1]
	str	r3, [sp, #20]
	add	r1, r1, #1
	strh	r1, [r0]	@ movhi
	ldr	r0, [r5, #-2708]
	ldr	r1, [r5, #-536]
	mla	r1, r3, r0, r1
	str	r2, [r1, #16]
	str	r1, [sp, #16]
	bl	Ftl_get_new_temp_ppa
	ldr	r2, [r5, #-536]
	ldr	r1, [sp, #16]
	ldr	r3, [sp, #20]
	str	r0, [r1, #4]
	ldr	r1, [r5, #-2708]
	mla	r3, r3, r1, r2
	ldr	r2, [r5, #-2692]
	add	r2, r2, r8
	ldr	r1, [r2, #8]
	str	r1, [r3, #8]
	mov	r1, #1
	ldr	r2, [r2, #12]
	str	r2, [r3, #12]
	ldr	r3, [sp, #32]
	str	r3, [r10, #12]
	ldr	r3, .L3508+60
	ldrh	r3, [r3]
	strh	r3, [r10, #2]	@ movhi
	ldr	r3, [r5, #-3332]
	ldr	r0, [r5, #-2692]
	str	r3, [r10, #4]
	add	r0, r0, r8
	ldr	r3, [r5, #-2708]
	add	r3, r3, #1
	str	r3, [r5, #-2708]
	bl	FtlGcBufAlloc
	ldr	r3, .L3508+16
	ldrb	r3, [r3, #140]	@ zero_extendqisi2
	cmp	r3, #0
	beq	.L3506
.L3420:
	bl	Ftl_gc_temp_data_write_back
	cmp	r0, #0
	beq	.L3454
	ldr	r3, .L3508
	mov	r2, #0
	mvn	r1, #0
	sub	r3, r3, #3280
	str	r2, [r3, #2716]
	strh	r1, [r3, #-8]	@ movhi
	strh	r2, [r3, #-6]	@ movhi
.L3502:
	ldr	r2, .L3508+20
	movw	r3, #1980
	ldrh	r0, [r2, r3]
	b	.L3497
.L3505:
	add	r7, r7, #1
.L3415:
	uxth	r3, r7
	ldr	r8, .L3508
	cmp	r3, fp
	ldr	r6, .L3508+44
	bcs	.L3422
	ldr	r3, .L3508+64
	add	r0, r6, #14
	ldrh	r1, [r6, #2]
	movw	lr, #65535
	ldr	r8, [r5, #-2692]
	mov	ip, #36
	ldrh	r9, [r3]
	add	r1, r1, r7
	mov	r3, #0
	mov	r6, r3
.L3423:
	uxth	r2, r3
	cmp	r2, r9
	bcs	.L3507
	ldrh	r2, [r0, #2]!
	add	r3, r3, #1
	cmp	r2, lr
	orrne	r2, r1, r2, asl #10
	mlane	r10, ip, r6, r8
	addne	r6, r6, #1
	uxthne	r6, r6
	strne	r2, [r10, #4]
	b	.L3423
.L3506:
	ldrb	r3, [r5, #-3425]	@ zero_extendqisi2
	ldr	r2, [r5, #-2708]
	cmp	r2, r3
	beq	.L3420
	ldr	r3, .L3508+60
	ldrh	r3, [r3, #4]
	cmp	r3, #0
	beq	.L3420
.L3454:
	add	r9, r9, #1
	b	.L3418
.L3422:
	ldrh	r3, [r6, #2]
	add	fp, fp, r3
	ldr	r3, [sp, #8]
	uxth	fp, fp
	strh	fp, [r6, #2]	@ movhi
	cmp	fp, r3
	bcc	.L3424
	ldr	r3, [r8, #-2708]
	cmp	r3, #0
	beq	.L3425
	bl	Ftl_gc_temp_data_write_back
	cmp	r0, #0
	movne	r3, #0
	strne	r3, [r8, #-564]
	bne	.L3502
.L3425:
	ldr	r3, .L3508+52
	ldrh	r5, [r3]
	cmp	r5, #0
	bne	.L3426
	ldrh	r3, [r6]
	ldr	r2, [r8, #-3548]
	mov	r3, r3, asl #1
	ldrh	r3, [r2, r3]
	cmp	r3, #0
	beq	.L3426
	ldr	r7, .L3508+16
.L3427:
	ldr	r3, [r7, #2444]
	cmp	r5, r3
	bcs	.L3432
	mov	r0, r5
	add	r1, sp, #36
	mov	r2, #0
	bl	log2phys
	ldr	r0, [sp, #36]
	cmn	r0, #1
	beq	.L3428
	ubfx	r0, r0, #10, #16
	bl	P2V_block_in_plane
	ldrh	r3, [r6]
	cmp	r3, r0
	bne	.L3428
.L3432:
	ldr	r3, .L3508+16
	ldr	r3, [r3, #2444]
	cmp	r5, r3
	bcc	.L3426
	ldrh	r3, [r6]
	mov	r1, #0
	ldr	r2, [r8, #-3548]
	mov	r3, r3, asl #1
	strh	r1, [r2, r3]	@ movhi
	ldrh	r0, [r6]
	bl	update_vpc_list
	bl	FtlCacheWriteBack
	bl	l2p_flush
	bl	FtlVpcTblFlush
	b	.L3426
.L3428:
	add	r5, r5, #1
	b	.L3427
.L3426:
	mvn	r3, #0
	strh	r3, [r6]	@ movhi
.L3424:
	ldr	r3, .L3508+68
	ldrh	r3, [r3]
	cmp	r3, #2
	ldrls	r3, .L3508+32
	ldrlsh	fp, [r3]
	bls	.L3434
.L3433:
	ldr	r2, .L3508
	mov	r1, #0
	str	r1, [r2, #-564]
	movw	r2, #1980
	ldr	r1, .L3508+20
	ldrh	r0, [r1, r2]
	cmp	r0, #0
	addeq	r0, r3, #1
	b	.L3497
.L3440:
	mov	r0, lr
	b	.L3497
.L3504:
	ldrh	r10, [r5, #-14]
	cmp	r10, r4
	bne	.L3361
	sub	r3, r9, #3520
	ldr	r2, [r9, #-2720]
	ldrh	r3, [r3, #-12]
	cmp	r3, #24
	movcc	r3, #5120
	movcs	r3, #1024
	cmp	r2, r3
	movls	r4, r10
	bls	.L3361
	ldr	r1, .L3508+20
	movw	r2, #1980
	mov	r3, #0
	str	r3, [r6, #-2720]
	strh	r3, [r1, r2]	@ movhi
	bl	GetSwlReplaceBlock
	cmp	r0, r10
	mov	r4, r0
	sub	r10, r9, #2720
	bne	.L3363
	ldrh	r2, [r8, #-12]
	ldrh	r3, [r10, #-2]
	cmp	r2, r3
	bcs	.L3364
	mov	r0, #64
	bl	List_get_gc_head_node
	uxth	r3, r0
	cmp	r3, r4
	beq	.L3373
	ldr	r2, [r9, #-568]
	ldr	fp, .L3508+16
	cmp	r2, #0
	add	r1, fp, #2336
	bne	.L3366
	ldrh	r0, [r1]
	cmp	r0, #3
	beq	.L3366
	ldr	r0, [r9, #-3240]
	cmp	r0, #0
	bne	.L3366
	ldr	r0, [fp, #2308]
	cmp	r0, #0
	bne	.L3366
	ldrb	r0, [fp, #140]	@ zero_extendqisi2
	cmp	r0, #0
	beq	.L3367
.L3366:
	ldr	r2, [r6, #-3548]
	mov	r3, r3, asl #1
	ldrh	r1, [r1]
	ldrh	r0, [r2, r3]
	movw	r3, #2386
	movw	r2, #2316
	ldrh	r3, [fp, r3]
	ldrh	r2, [fp, r2]
	cmp	r1, #3
	mul	r2, r2, r3
	moveq	r3, r3, lsr #1
	movne	r3, #0
	add	r3, r2, r3
	cmp	r0, r3
	bgt	.L3369
	mov	r0, #0
	bl	List_get_gc_head_node
	ldr	r3, [fp, #2444]
	ldr	r2, [r6, #-3376]
	add	r3, r3, r3, asl #1
	cmp	r2, r3, lsr #2
	movls	r3, #160
	uxth	r4, r0
	bls	.L3500
	b	.L3503
.L3367:
	ldr	r2, [r9, #-3548]
	mov	r3, r3, asl #1
	ldrh	r3, [r2, r3]
	cmp	r3, #7
	bhi	.L3372
	bl	List_get_gc_head_node
	uxth	r4, r0
.L3503:
	mov	r3, #128
.L3500:
	strh	r3, [r10, #-2]	@ movhi
	movw	r3, #65535
	cmp	r4, r3
	beq	.L3373
	b	.L3363
.L3369:
	mov	r3, #128
	b	.L3501
.L3372:
	mov	r3, #64
	b	.L3501
.L3364:
	mov	r3, #80
.L3501:
	strh	r3, [r10, #-2]	@ movhi
	b	.L3373
.L3363:
	ldr	r0, [r6, #-3612]
	mov	r1, r4, asl #1
	ldr	r3, [r6, #-3548]
	ldrh	r2, [r8, #-12]
	ldrh	r3, [r3, r1]
	ldrh	r1, [r0, r1]
	ldr	r0, .L3508+72
	str	r1, [sp]
	ldrh	r1, [r10, #-4]
	str	r1, [sp, #4]
	mov	r1, r4
	bl	printk
.L3373:
	bl	FtlGcReFreshBadBlk
	b	.L3361
.L3497:
	add	sp, sp, #44
	@ sp needed
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L3509:
	.align	2
.L3508:
	.word	.LANCHOR2
	.word	.LANCHOR1
	.word	.LANCHOR2-2656
	.word	.LANCHOR2-3280
	.word	.LANCHOR0
	.word	.LANCHOR4
	.word	.LC152
	.word	.LANCHOR2-3520
	.word	.LANCHOR0+2384
	.word	.LANCHOR2-3296
	.word	.LANCHOR2-3472
	.word	.LANCHOR2-3288
	.word	.LANCHOR2-2716
	.word	.LANCHOR4+1982
	.word	.LANCHOR0+2386
	.word	.LANCHOR2-3432
	.word	.LANCHOR0+2316
	.word	.LANCHOR2-3532
	.word	.LC153
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
	ldr	r3, .L3554
	ldr	r7, .L3554+4
	ldr	r5, [r3, #1988]
	ldr	r3, [r7, #-3620]
	cmp	r3, #0
	bne	.L3512
	ldr	r4, .L3554+8
	ldr	r1, [r4, #2436]
	cmp	r1, #0
	beq	.L3512
	ldrb	r6, [r4, #140]	@ zero_extendqisi2
	mov	r8, #0
	ldr	r0, [r4, #2440]
	mov	r10, #36
	cmp	r6, #0
	ldrb	r3, [r5, #9]	@ zero_extendqisi2
	ldr	r9, .L3554+8
	ldrneb	r6, [r5, #8]	@ zero_extendqisi2
	subne	r6, r6, #1
	clzne	r6, r6
	movne	r6, r6, lsr #5
	mov	r2, r6
	bl	FlashProgPages
.L3515:
	ldr	r3, [r4, #2436]
	cmp	r8, r3
	bcs	.L3533
	mul	fp, r10, r8
	ldr	r3, [r9, #2440]
	add	r2, r3, fp
	ldr	r3, [r3, fp]
	cmn	r3, #1
	beq	.L3536
	ldr	r3, [r2, #4]
	cmp	r6, #0
	ldr	r0, [r2, #16]
	add	r1, sp, #4
	mov	r2, #1
	orrne	r3, r3, #-2147483648
	str	r3, [sp, #4]
	bl	log2phys
	ldr	r3, [r4, #2440]
	add	r3, r3, fp
	ldr	r3, [r3, #12]
	ldr	r0, [r3, #12]
	cmn	r0, #1
	beq	.L3519
	ubfx	r0, r0, #10, #16
	bl	P2V_block_in_plane
	ldr	r2, [r7, #-3548]
	mov	r3, r0, asl #1
	mov	fp, r0
	ldrh	r2, [r2, r3]
	cmp	r2, #0
	bne	.L3520
	ldr	r0, .L3554+12
	mov	r1, fp
	bl	printk
.L3520:
	mov	r0, fp
	bl	decrement_vpc_count
.L3519:
	add	r8, r8, #1
	b	.L3515
.L3552:
	ldr	r6, .L3554+16
	movw	r5, #16386
.L3532:
	ldrh	r3, [r6]
	cmp	r3, #0
	beq	.L3533
	mov	r0, #1
	mov	r1, r0
	bl	ftl_do_gc
	subs	r5, r5, #1
	bne	.L3532
.L3533:
	mov	r3, #0
	str	r3, [r4, #2436]
	b	.L3512
.L3536:
	ldr	fp, .L3554+8
	mov	r9, #0
.L3516:
	ldr	r3, [r4, #2436]
	cmp	r9, r3
	bcs	.L3552
	mov	r8, #36
	ldr	r3, [fp, #2440]
	mul	r8, r8, r9
	mov	r10, #0
	mvn	r2, #0
	str	r2, [r3, r8]
.L3522:
	ldr	r3, [r4, #2440]
	add	r2, r3, r8
	ldr	r3, [r3, r8]
	cmn	r3, #1
	bne	.L3553
	ldr	r0, [r2, #4]
	ubfx	r0, r0, #10, #16
	bl	P2V_block_in_plane
	ldrh	r3, [r5]
	cmp	r3, r0
	bne	.L3523
	ldr	r1, [r7, #-3548]
	mov	r3, r3, asl #1
	ldrh	r0, [r5, #4]
	ldrh	r2, [r1, r3]
	rsb	r2, r0, r2
	strh	r2, [r1, r3]	@ movhi
	ldr	r3, .L3554+20
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
	ldr	r3, [r7, #-3140]
	add	r3, r3, #1
	str	r3, [r7, #-3140]
	ldr	r3, [r4, #2440]
	add	r3, r3, r8
	ldr	r0, [r3, #4]
	ubfx	r0, r0, #10, #16
	bl	FtlGcMarkBadPhyBlk
	mov	r0, r5
	bl	get_new_active_ppa
	ldr	r3, [r4, #2440]
	mov	r2, r6
	mov	r1, #1
	add	r3, r3, r8
	str	r0, [sp, #4]
	str	r0, [r3, #4]
	mov	r0, r3
	ldrb	r3, [r5, #9]	@ zero_extendqisi2
	bl	FlashProgPages
	ldr	r3, [r4, #2440]
	ldr	r3, [r3, r8]
	cmn	r3, #1
	ldreq	r2, .L3554+4
	moveq	r3, #1
	streq	r3, [r2, #-3620]
	ldr	r3, [r7, #-3620]
	cmp	r3, #0
	beq	.L3522
	b	.L3512
.L3553:
	ldr	r3, [r2, #4]
	cmp	r6, #0
	ldr	r0, [r2, #16]
	add	r1, sp, #4
	mov	r2, #1
	orrne	r3, r3, #-2147483648
	str	r3, [sp, #4]
	bl	log2phys
	ldr	r3, [r4, #2440]
	add	r8, r3, r8
	ldr	r3, [r8, #12]
	ldr	r0, [r3, #12]
	cmn	r0, #1
	beq	.L3529
	ubfx	r0, r0, #10, #16
	bl	P2V_block_in_plane
	ldr	r2, [r7, #-3548]
	mov	r3, r0, asl #1
	mov	r8, r0
	ldrh	r2, [r2, r3]
	cmp	r2, #0
	bne	.L3530
	ldr	r0, .L3554+12
	mov	r1, r8
	bl	printk
.L3530:
	mov	r0, r8
	bl	decrement_vpc_count
.L3529:
	add	r9, r9, #1
	b	.L3516
.L3512:
	mov	r0, #0
	add	sp, sp, #12
	@ sp needed
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L3555:
	.align	2
.L3554:
	.word	.LANCHOR4
	.word	.LANCHOR2
	.word	.LANCHOR0
	.word	.LC154
	.word	.LANCHOR2-2662
	.word	.LANCHOR0+2384
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
	ldr	r3, .L3562
	ldr	r3, [r3, #-3620]
	cmp	r3, #0
	bne	.L3557
	ldr	r3, .L3562+4
	ldr	r3, [r3, #504]
	cmn	r3, #1
	beq	.L3557
	bl	FtlCacheWriteBack
	bl	l2p_flush
	mov	r0, #1
	bl	FtlEctTblFlush
	bl	FtlVpcTblFlush
.L3557:
	mov	r0, #0
	ldmfd	sp!, {r3, pc}
.L3563:
	.align	2
.L3562:
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
	ldr	r3, .L3567
	ldr	r3, [r3, #504]
	cmp	r3, #1
	bne	.L3565
	bl	FtlSysFlush
.L3565:
	mov	r0, #0
	ldmfd	sp!, {r3, pc}
.L3568:
	.align	2
.L3567:
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
	ldr	r0, .L3573
	bl	printk
	ldmfd	sp!, {r3, lr}
	b	ftl_deinit
.L3574:
	.align	2
.L3573:
	.word	.LC155
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
	add	r2, r0, r1
	ldr	r4, .L3596
	mov	r6, r0
	mov	r5, r1
	ldr	r3, [r4, #2424]
	cmp	r2, r3
	mvnhi	r0, #0
	bhi	.L3578
	ldr	r3, .L3596+4
	ldr	r0, [r3, #504]
	cmn	r0, #1
	beq	.L3578
	cmp	r1, #31
	bls	.L3587
	ldr	r7, .L3596+8
	ldr	r3, [r7, #-3620]
	cmp	r3, #0
	bne	.L3587
	bl	FtlCacheWriteBack
	movw	r3, #2390
	ldrh	r4, [r4, r3]
	mov	r0, r6
	mov	r1, r4
	bl	__aeabi_uidiv
	smulbb	r3, r0, r4
	mov	r8, r0
	rsb	r6, r3, r6
	uxth	r6, r6
	cmp	r6, #0
	beq	.L3579
	rsb	r4, r6, r4
	add	r8, r0, #1
	cmp	r4, r5
	movcs	r4, r5
	uxth	r4, r4
	rsb	r5, r4, r5
.L3579:
	ldr	r4, .L3596+12
	mvn	r3, #0
	ldr	r9, .L3596+16
	str	r3, [sp, #4]
	mov	r6, r4
.L3580:
	ldrh	r3, [r4]
	cmp	r5, r3
	bcc	.L3595
	mov	r0, r8
	mov	r1, sp
	mov	r2, #0
	bl	log2phys
	ldr	r3, [sp]
	cmn	r3, #1
	beq	.L3581
	ldr	r3, [r9, #1992]
	add	r1, sp, #4
	mov	r2, #1
	mov	r0, r8
	add	r3, r3, #1
	str	r3, [r9, #1992]
	ldr	r3, [r7, #-3364]
	add	r3, r3, #1
	str	r3, [r7, #-3364]
	bl	log2phys
	ldr	r0, [sp]
	ubfx	r0, r0, #10, #16
	bl	P2V_block_in_plane
	bl	decrement_vpc_count
.L3581:
	ldrh	r3, [r6]
	add	r8, r8, #1
	rsb	r5, r3, r5
	b	.L3580
.L3595:
	ldr	r3, .L3596+16
	ldr	r2, [r3, #1992]
	cmp	r2, #32
	bls	.L3587
	mov	r2, #0
	str	r2, [r3, #1992]
	bl	l2p_flush
	bl	FtlVpcTblFlush
.L3587:
	mov	r0, #0
.L3578:
	add	sp, sp, #12
	@ sp needed
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, pc}
.L3597:
	.align	2
.L3596:
	.word	.LANCHOR0
	.word	.LANCHOR1
	.word	.LANCHOR2
	.word	.LANCHOR0+2390
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
	ldr	r1, .L3646
	.pad #84
	sub	sp, sp, #84
	ldr	r4, [r1, #504]
	cmn	r4, #1
	moveq	r0, r4
	beq	.L3600
	cmp	r0, #16
	mov	r8, r3
	str	r2, [sp, #36]
	bne	.L3601
	mov	r1, r2
	add	r0, r5, #256
	mov	r2, r3
	bl	FtlVendorPartRead
	b	.L3600
.L3601:
	ldr	r3, [sp, #36]
	add	r3, r5, r3
	str	r3, [sp, #40]
	ldr	r3, .L3646+4
	ldr	r1, [sp, #40]
	ldr	r2, [r3, #2424]
	cmp	r1, r2
	mvnhi	r0, #0
	bhi	.L3600
	movw	r2, #2390
	mov	r0, r5
	ldrh	r4, [r3, r2]
	mov	r1, r4
	bl	__aeabi_uidiv
	mov	r1, r4
	ldr	r3, [sp, #40]
	mov	r10, r0
	sub	r0, r3, #1
	bl	__aeabi_uidiv
	rsb	r3, r10, #1
	add	r3, r3, r0
	str	r3, [sp, #28]
	ldr	r3, .L3646+8
	ldr	r1, [sp, #36]
	str	r0, [sp, #32]
	mov	r0, r10
	ldr	r2, [r3, #-3340]
	add	r2, r1, r2
	ldr	r1, [sp, #28]
	str	r2, [r3, #-3340]
	ldr	r2, [r3, #-3368]
	add	r2, r1, r2
	ldr	r1, [sp, #32]
	str	r2, [r3, #-3368]
	bl	FtlCacheMetchLpa
	cmp	r0, #0
	beq	.L3602
	bl	FtlCacheWriteBack
.L3602:
	ldr	r4, .L3646+8
	mov	r9, #0
	mov	r6, r10
	mov	r7, r9
	str	r9, [sp, #48]
	str	r9, [sp, #52]
.L3603:
	ldr	r3, [sp, #28]
	cmp	r3, #0
	beq	.L3645
	mov	r0, r6
	add	r1, sp, #76
	mov	r2, #0
	bl	log2phys
	ldr	r3, [sp, #76]
	cmn	r3, #1
	bne	.L3641
	mov	fp, #0
.L3604:
	ldr	r3, .L3646+12
	ldrh	r0, [r3]
	cmp	fp, r0
	bcs	.L3608
	mla	r0, r0, r6, fp
	ldr	r2, [sp, #40]
	cmp	r0, r5
	movcs	r3, #1
	movcc	r3, #0
	cmp	r0, r2
	movcs	r3, #0
	cmp	r3, #0
	beq	.L3606
	rsb	r0, r5, r0
	mov	r1, #0
	mov	r2, #512
	add	r0, r8, r0, asl #9
	bl	ftl_memset
.L3606:
	add	fp, fp, #1
	b	.L3604
.L3641:
	ldr	r2, [r4, #-540]
	mov	fp, #36
	cmp	r6, r10
	mla	fp, fp, r7, r2
	str	r3, [fp, #4]
	bne	.L3609
	ldr	r3, [r4, #-512]
	mov	r0, r5
	str	r3, [fp, #8]
	ldr	r3, .L3646+12
	ldrh	ip, [r3]
	mov	r1, ip
	str	ip, [sp, #44]
	bl	__aeabi_uidivmod
	ldr	ip, [sp, #44]
	ldr	r2, [sp, #36]
	rsb	r3, r1, ip
	str	r1, [sp, #56]
	cmp	r3, r2
	movcs	r3, r2
	cmp	r3, ip
	str	r3, [sp, #48]
	streq	r8, [fp, #8]
	b	.L3610
.L3609:
	ldr	r3, [sp, #32]
	cmp	r6, r3
	bne	.L3611
	ldr	r3, [r4, #-508]
	ldr	r1, [sp, #40]
	str	r3, [fp, #8]
	ldr	r3, .L3646+12
	ldrh	r2, [r3]
	mul	r3, r2, r6
	rsb	r9, r3, r1
	cmp	r9, r2
	bne	.L3610
	b	.L3643
.L3611:
	ldr	r3, .L3646+12
	ldrh	r3, [r3]
	mul	r3, r3, r6
.L3643:
	rsb	r3, r5, r3
	add	r3, r8, r3, asl #9
	str	r3, [fp, #8]
.L3610:
	ldr	r3, .L3646+16
	ldr	r2, [r4, #-500]
	str	r6, [fp, #16]
	ldrh	r3, [r3]
	mul	r3, r3, r7
	add	r7, r7, #1
	bic	r3, r3, #3
	add	r3, r2, r3
	str	r3, [fp, #12]
.L3608:
	ldr	r3, [sp, #28]
	add	r6, r6, #1
	subs	r3, r3, #1
	str	r3, [sp, #28]
	beq	.L3612
	ldr	r3, .L3646+20
	ldrh	r3, [r3]
	cmp	r7, r3, asl #3
	bne	.L3603
.L3612:
	cmp	r7, #0
	beq	.L3603
	ldr	r0, [r4, #-540]
	mov	r1, r7
	mov	r2, #0
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
.L3619:
	ldr	r3, [sp, #44]
	mov	ip, #36
	mul	fp, ip, r3
	ldr	r3, [r4, #-540]
	add	r3, r3, fp
	ldr	r2, [r3, #16]
	cmp	r2, r10
	bne	.L3614
	ldr	r1, [r3, #8]
	ldr	r3, [r4, #-512]
	cmp	r1, r3
	bne	.L3615
	ldr	r3, [sp, #60]
	mov	r0, r8
	ldr	r2, [sp, #64]
	add	r1, r1, r3
	b	.L3644
.L3614:
	ldr	r1, [sp, #32]
	cmp	r2, r1
	bne	.L3615
	ldr	r1, [r3, #8]
	ldr	r3, [r4, #-508]
	cmp	r1, r3
	bne	.L3615
	ldr	r3, .L3646+12
	ldr	r2, [sp, #68]
	ldrh	r0, [r3]
	ldr	r3, [sp, #32]
	mul	r0, r0, r3
	rsb	r0, r5, r0
	add	r0, r8, r0, asl #9
.L3644:
	bl	ftl_memcpy
.L3615:
	ldr	r2, [r4, #-540]
	add	r3, r2, fp
	ldr	r1, [r2, fp]
	cmn	r1, #1
	streq	r1, [sp, #52]
	ldreq	r2, [r4, #-3164]
	addeq	r2, r2, #1
	streq	r2, [r4, #-3164]
	ldr	r2, [r3, #12]
	ldr	r1, [r3, #16]
	ldr	r2, [r2, #8]
	cmp	r1, r2
	beq	.L3617
	ldr	r2, [r4, #-3164]
	ldr	r0, .L3646+24
	add	r2, r2, #1
	str	r2, [r4, #-3164]
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
.L3617:
	ldr	r3, [r4, #-540]
	add	r2, r3, fp
	ldr	r3, [r3, fp]
	cmp	r3, #256
	bne	.L3618
	ldr	r0, [r2, #4]
	ubfx	r0, r0, #10, #16
	bl	P2V_block_in_plane
	bl	FtlGcRefreshBlock
.L3618:
	ldr	r3, [sp, #44]
	add	r3, r3, #1
	str	r3, [sp, #44]
	cmp	r3, r7
	bne	.L3619
	mov	r7, #0
	b	.L3603
.L3645:
	ldr	r3, .L3646+28
	ldrh	r3, [r3, #-6]
	cmp	r3, #0
	beq	.L3621
	ldr	r0, [sp, #28]
	mov	r1, #1
	bl	ftl_do_gc
.L3621:
	ldr	r0, [sp, #52]
.L3600:
	add	sp, sp, #84
	@ sp needed
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L3647:
	.align	2
.L3646:
	.word	.LANCHOR1
	.word	.LANCHOR0
	.word	.LANCHOR2
	.word	.LANCHOR0+2390
	.word	.LANCHOR0+2396
	.word	.LANCHOR0+2316
	.word	.LC144
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
	ldr	r3, .L3654
	ldr	r3, [r3, #504]
	cmp	r3, #1
	mvnne	r4, #0
	bne	.L3651
	mov	r2, r4
	mov	r0, r6
	mov	r1, r5
	bl	ftl_vendor_read
	mov	r4, r0
.L3651:
	bl	rknand_device_unlock
	mov	r0, r4
	ldmfd	sp!, {r4, r5, r6, pc}
.L3655:
	.align	2
.L3654:
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
	ldr	r2, .L3677
	ldr	r7, .L3677+4
	ldr	r5, .L3677+8
	ldr	r6, .L3677+12
	ldr	r1, .L3677+16
	ldr	r0, .L3677+20
	str	r3, [r7, #504]
	mov	r3, #0
	str	r3, [r2, #1996]
	str	r3, [r5, #-3620]
	bl	printk
	add	r0, r6, #112
	bl	FtlConstantsInit
	bl	FtlMemInit
	bl	FtlVariablesInit
	ldr	r3, [r6, #2320]
	uxth	r0, r3
	bl	FtlFreeSysBlkQueueInit
	bl	FtlLoadBbt
	cmp	r0, #0
	ldrne	r0, .L3677+24
	bne	.L3676
	bl	FtlSysBlkInit
	subs	r4, r0, #0
	beq	.L3663
	ldr	r0, .L3677+28
.L3676:
	ldr	r1, .L3677+32
	bl	printk
	b	.L3662
.L3663:
	sub	r5, r5, #3520
	mov	r1, #1
	str	r1, [r7, #504]
	bl	ftl_do_gc
	ldrh	r5, [r5, #-12]
	cmp	r5, #15
	bhi	.L3664
	ldr	r6, .L3677+36
	ldr	r7, .L3677+40
	sub	r8, r6, #244
.L3667:
	ldrh	r3, [r6]
	movw	r2, #65535
	cmp	r3, r2
	bne	.L3665
	ldrh	r2, [r7]
	cmp	r2, r3
	bne	.L3665
	and	r0, r4, #63
	bl	List_get_gc_head_node
	uxth	r0, r0
	bl	FtlGcRefreshBlock
.L3665:
	mov	r0, #1
	mov	r1, r0
	bl	ftl_do_gc
	mov	r0, #0
	mov	r1, #1
	bl	ftl_do_gc
	ldrh	r2, [r8]
	add	r3, r5, #2
	cmp	r2, r3
	bhi	.L3662
	add	r4, r4, #1
	cmp	r4, #4096
	bne	.L3667
	b	.L3662
.L3664:
	ldrb	r3, [r6, #140]	@ zero_extendqisi2
	cmp	r3, #0
	beq	.L3662
	mov	r4, #128
.L3669:
	mov	r0, #1
	mov	r1, r0
	bl	ftl_do_gc
	subs	r4, r4, #1
	bne	.L3669
.L3662:
	mov	r0, #0
	ldmfd	sp!, {r4, r5, r6, r7, r8, pc}
.L3678:
	.align	2
.L3677:
	.word	.LANCHOR4
	.word	.LANCHOR1
	.word	.LANCHOR2
	.word	.LANCHOR0
	.word	.LC77
	.word	.LC76
	.word	.LC156
	.word	.LC157
	.word	.LANCHOR3+108
	.word	.LANCHOR2-3288
	.word	.LANCHOR2-2670
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
	bl	ftl_malloc
	ldr	r6, .L3684
	ldr	r4, .L3684+4
	mov	r5, #0
	ldr	r1, .L3684+8
	str	r5, [r6, #2004]
	str	r0, [r6, #2000]
	mov	r0, r4
	str	r5, [r0, #1680]!
	bl	rknand_get_reg_addr
	ldr	r3, [r4, #1680]
	cmp	r3, r5
	beq	.L3682
	bl	rk_nandc_irq_init
	mov	r1, r5
	mov	r2, r5
	mov	r3, #2048
	ldr	r0, [r6, #2000]
	bl	FlashSramLoadStore
	bl	rknand_flash_cs_init
	ldr	r0, [r4, #1680]
	bl	FlashInit
	subs	r4, r0, #0
	bne	.L3681
	bl	FtlInit
.L3681:
	mov	r1, r4
	ldr	r0, .L3684+12
	bl	printk
	mov	r0, r4
	ldmfd	sp!, {r4, r5, r6, pc}
.L3682:
	mvn	r0, #0
	ldmfd	sp!, {r4, r5, r6, pc}
.L3685:
	.align	2
.L3684:
	.word	.LANCHOR4
	.word	.LANCHOR2
	.word	.LANCHOR4+2004
	.word	.LC158
	.fnend
	.size	rk_ftl_init, .-rk_ftl_init
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
	ldr	r5, .L3702
	ldrb	r3, [r5, #140]	@ zero_extendqisi2
	cmp	r3, #0
	beq	.L3686
	ldr	r8, .L3702+4
	movw	r3, #1844
	ldr	r4, .L3702+8
	movw	r9, #4097
	ldr	r0, .L3702+12
	ldrh	r7, [r8, r3]
	ldr	r3, [r4, #-3548]
	mov	r6, r7, asl #1
	mov	r1, r7
	ldrh	r2, [r3, r6]
	bl	printk
	sub	r3, r4, #3520
	ldrh	r0, [r3, #-8]
	bl	FtlGcRefreshOpenBlock
	sub	r3, r4, #3472
	ldrh	r0, [r3, #-8]
	bl	FtlGcRefreshOpenBlock
	ldr	r0, .L3702+16
	bl	allocate_new_data_superblock
	ldr	r0, .L3702+20
	bl	allocate_new_data_superblock
.L3688:
	subs	r9, r9, #1
	beq	.L3692
	mov	r0, #1
	mov	r1, r0
	bl	ftl_do_gc
	ldr	r3, [r4, #-3548]
	ldrh	r3, [r3, r6]
	cmp	r3, #0
	bne	.L3688
.L3692:
	ldr	r3, [r4, #-3548]
	mov	r1, r7
	ldr	r0, .L3702+12
	ldrh	r2, [r3, r6]
	bl	printk
	ldr	r3, [r4, #-3548]
	ldrh	r10, [r3, r6]
	cmp	r10, #0
	bne	.L3690
	add	r0, sp, #48
	mov	fp, #36
	strh	r7, [r0, #-48]!	@ movhi
	bl	make_superblock
	movw	r3, #2316
	ldrh	lr, [r5, r3]
	mov	r5, r10
	ldr	r3, .L3702+8
	ldr	r9, [r3, #-3616]
	mov	r3, r10
	mov	ip, r3
	movw	r10, #65535
	add	r0, sp, #14
.L3693:
	uxth	r2, r3
	cmp	r2, lr
	bcs	.L3701
	ldrh	r2, [r0, #2]!
	add	r3, r3, #1
	cmp	r2, r10
	movne	r2, r2, asl #10
	mlane	r1, fp, r5, r9
	addne	r5, r5, #1
	uxthne	r5, r5
	stmneib	r1, {r2, ip}
	strne	ip, [r1, #12]
	b	.L3693
.L3701:
	ldr	r3, [r4, #-3548]
	mov	r1, r7
	ldr	r0, .L3702+24
	ldrh	r2, [r3, r6]
	bl	printk
	mov	r1, #0
	mov	r2, r5
	ldr	r0, [r4, #-3616]
	bl	FlashEraseBlocks
	ldr	r0, [r4, #-3616]
	mov	r1, #1
	mov	r2, r5
	bl	FlashEraseBlocks
.L3690:
	movw	r3, #1844
	mvn	r2, #0
	strh	r2, [r8, r3]	@ movhi
.L3686:
	add	sp, sp, #52
	@ sp needed
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L3703:
	.align	2
.L3702:
	.word	.LANCHOR0
	.word	.LANCHOR4
	.word	.LANCHOR2
	.word	.LC159
	.word	.LANCHOR2-3528
	.word	.LANCHOR2-3480
	.word	.LC160
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
	ldr	r10, .L3778
	str	r3, [sp, #4]
	ldr	r3, [r10, #-3620]
	cmp	r3, #0
	bne	.L3746
	mov	r8, r2
	ldr	r2, .L3778+4
	ldr	r2, [r2, #504]
	cmn	r2, #1
	moveq	r0, r3
	beq	.L3706
	cmp	r0, #16
	mov	r7, r1
	bne	.L3707
	add	r0, r1, #256
	ldr	r2, [sp, #4]
	mov	r1, r8
	bl	FtlVendorPartWrite
	b	.L3706
.L3707:
	ldr	fp, .L3778+8
	add	r5, r1, r8
	ldr	r3, [fp, #2424]
	cmp	r5, r3
	mvnhi	r0, #0
	bhi	.L3706
	ldr	r6, .L3778+12
	mov	r3, #2048
	mov	r0, r1
	str	r3, [r6, #2008]
	movw	r3, #2390
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
	ldr	r3, [r10, #-3360]
	add	r3, r2, r3
	ldr	r2, [fp, #2436]
	str	r3, [r10, #-3360]
	ldr	r3, [r10, #-3344]
	add	r3, r8, r3
	str	r3, [r10, #-3344]
	movcs	r3, #1
	movcc	r3, #0
	cmp	r2, #0
	str	r3, [sp, #20]
	beq	.L3709
	mov	r3, #36
	ldr	r9, [fp, #2440]
	mul	r3, r3, r2
	ldr	r2, [sp, #8]
	sub	r3, r3, #36
	add	r9, r9, r3
	ldr	r3, [r9, #16]
	cmp	r2, r3
	bne	.L3710
	ldr	r3, [r10, #-3356]
	mov	r1, r4
	mov	r0, r7
	add	r3, r3, #1
	str	r3, [r10, #-3356]
	ldr	r3, [r6, #2012]
	add	r3, r3, #1
	str	r3, [r6, #2012]
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
	bne	.L3711
	ldr	r3, [r6, #2012]
	cmp	r3, #2
	ble	.L3746
.L3711:
	ldr	r3, [sp, #4]
	rsb	r8, r4, r8
	add	r7, r7, r4
	str	r5, [sp]
	add	r3, r3, r10
	str	r3, [sp, #4]
	ldr	r3, [sp, #8]
	add	r3, r3, #1
	str	r3, [sp, #8]
.L3710:
	mov	r3, #0
	str	r3, [r6, #2012]
.L3709:
	ldr	r0, [sp, #8]
	ldr	r1, [sp, #28]
	bl	FtlCacheMetchLpa
	cmp	r0, #0
	beq	.L3712
	bl	FtlCacheWriteBack
.L3712:
	ldr	r5, .L3778+16
	mov	r3, #0
	ldr	r4, .L3778+8
	str	r3, [sp, #12]
	str	r5, [r6, #1988]
	ldr	r6, [sp, #8]
	mov	r10, r4
	str	r3, [sp, #32]
.L3713:
	ldr	r3, [sp]
	cmp	r3, #0
	beq	.L3777
	ldrh	r2, [r5, #4]
	cmp	r2, #0
	bne	.L3714
	ldr	r3, .L3778+16
	ldr	r9, .L3778+4
	cmp	r5, r3
	bne	.L3715
	add	r0, r3, #48
	ldrh	r5, [r0, #4]
	cmp	r5, #0
	bne	.L3716
	bl	allocate_new_data_superblock
	str	r5, [r9, #3356]
.L3716:
	ldr	r0, .L3778+16
	bl	allocate_new_data_superblock
	ldr	r3, [r9, #3356]
	cmp	r3, #0
	ldrne	r5, .L3778+20
	bne	.L3717
.L3718:
	ldr	r5, .L3778+16
	b	.L3717
.L3715:
	ldrh	r3, [r3, #4]
	str	r2, [r9, #3356]
	cmp	r3, #0
	bne	.L3718
	mov	r0, r5
	bl	allocate_new_data_superblock
.L3717:
	ldrh	r3, [r5, #4]
	cmp	r3, #0
	bne	.L3719
	mov	r0, r5
	bl	allocate_new_data_superblock
.L3719:
	ldr	r3, .L3778+12
	str	r5, [r3, #1988]
.L3714:
	ldr	r2, .L3778
	ldr	r1, [r4, #2436]
	ldrh	r3, [r5, #4]
	ldr	r2, [r2, #-544]
	rsb	r2, r1, r2
	cmp	r3, r2
	movcs	r3, r2
	ldr	r2, [sp]
	cmp	r3, r2
	movcs	r3, r2
	str	r3, [sp, #44]
	mov	r3, #0
.L3775:
	str	r3, [sp, #16]
	ldr	r3, [sp, #16]
	ldr	r2, [sp, #44]
	cmp	r3, r2
	beq	.L3721
	ldrh	r3, [r5, #4]
	cmp	r3, #0
	beq	.L3721
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
	beq	.L3722
	ldr	r3, .L3778+24
	ldrh	r2, [r3]
	add	r3, r8, r7
	mls	r3, r2, r6, r3
	cmp	r3, r2
	bne	.L3721
.L3722:
	add	r1, sp, #56
	mov	r2, #0
	mov	r0, r6
	str	ip, [sp, #52]
	bl	log2phys
	mov	r0, r5
	bl	get_new_active_ppa
	ldr	r2, [r4, #2436]
	ldr	r1, [r4, #2440]
	mov	r3, #36
	ldr	fp, .L3778+28
	mla	r1, r3, r2, r1
	ldrh	r2, [fp]
	str	r6, [r1, #16]
	str	r3, [sp, #48]
	ldr	r3, [r4, #2436]
	str	r0, [r1, #4]
	ldr	r0, .L3778
	mul	lr, r3, r2
	bic	r3, lr, #3
	str	r3, [sp, #36]
	ldr	r3, [r0, #-496]
	ldr	ip, [sp, #36]
	ldrh	lr, [fp, #-2]
	add	r9, r3, ip
	str	r3, [sp, #40]
	ldr	r3, [r4, #2436]
	ldr	r0, [r0, #-516]
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
	beq	.L3723
	ldr	r3, [sp, #24]
	cmp	r3, #0
	beq	.L3724
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
	b	.L3725
.L3724:
	cmp	ip, #0
	beq	.L3725
	ldr	r2, .L3778+24
	add	r3, r8, r7
	ldrh	r2, [r2]
	smulbb	r2, r2, r6
	rsb	r3, r2, r3
	uxth	r3, r3
	str	r3, [sp, #12]
	ldr	r3, [sp, #24]
	str	r3, [sp, #32]
.L3725:
	ldr	r3, .L3778+24
	ldr	r2, [sp, #12]
	ldrh	r3, [r3]
	cmp	r2, r3
	bne	.L3726
	ldr	r3, [sp, #24]
	ldr	r0, [r10, #2440]
	cmp	r3, #0
	moveq	r3, r2
	ldr	r2, [r10, #2436]
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
	bne	.L3729
	ldr	r0, [r3, #8]
	ldr	r3, .L3778+32
	ldrh	r2, [r3]
	b	.L3773
.L3726:
	ldr	r2, [sp, #56]
	mov	r3, #36
	cmn	r2, #1
	beq	.L3730
	ldr	r1, [r4, #2440]
	add	r0, sp, #60
	str	r2, [sp, #64]
	ldr	r2, [r4, #2436]
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
	ldr	r3, .L3778
	ldreq	r2, [r3, #-3164]
	addeq	r2, r2, #1
	streq	r2, [r3, #-3164]
	beq	.L3733
	ldr	r2, [r9, #8]
	cmp	r2, r6
	beq	.L3733
	ldr	r2, [r3, #-3164]
	ldr	r0, .L3778+36
	add	r2, r2, #1
	str	r2, [r3, #-3164]
	mov	r2, r6
	ldr	r1, [r9, #8]
	bl	printk
	b	.L3733
.L3730:
	ldr	r2, [r4, #2436]
	ldr	r1, [r4, #2440]
	mla	r3, r3, r2, r1
	mov	r1, #0
	ldr	r0, [r3, #8]
	ldr	r3, .L3778+32
	ldrh	r2, [r3]
	bl	ftl_memset
.L3733:
	ldr	r3, [sp, #24]
	cmp	r3, #0
	mov	r3, #36
	beq	.L3734
	ldr	r1, [r4, #2440]
	ldr	r2, [r4, #2436]
	mla	r3, r3, r2, r1
	ldr	r1, [sp, #4]
	ldr	r0, [r3, #8]
	ldr	r3, [sp, #32]
	add	r0, r0, r3, asl #9
	b	.L3776
.L3734:
	ldr	r1, [r4, #2436]
	ldr	r2, [r4, #2440]
	mla	r3, r3, r1, r2
	ldr	r2, .L3778+24
	ldrh	r1, [r2]
	ldr	r0, [r3, #8]
	mul	r1, r1, r6
	ldr	r3, [sp, #4]
	rsb	r1, r7, r1
	add	r1, r3, r1, asl #9
.L3776:
	ldr	r3, [sp, #12]
	mov	r2, r3, asl #9
	b	.L3773
.L3723:
	ldr	r2, [sp, #20]
	cmp	r2, #0
	ldr	r2, [r4, #2436]
	beq	.L3735
	ldr	r1, [r4, #2440]
	mla	r3, r3, r2, r1
	ldr	r2, .L3778+24
	ldrh	fp, [r2]
	ldr	r2, [sp, #4]
	mul	fp, fp, r6
	rsb	fp, r7, fp
	add	fp, r2, fp, asl #9
	str	fp, [r3, #8]
	b	.L3729
.L3735:
	ldr	r0, [r4, #2440]
	mla	r3, r3, r2, r0
	ldr	r2, .L3778+24
	ldrh	r1, [r2]
	ldrh	r2, [fp, #-2]
	ldr	r0, [r3, #8]
	mul	r1, r1, r6
	ldr	r3, [sp, #4]
	rsb	r1, r7, r1
	add	r1, r3, r1, asl #9
.L3773:
	bl	ftl_memcpy
.L3729:
	ldr	r3, .L3778+40
	ldr	r2, [sp, #40]
	ldr	r1, [sp, #36]
	strh	r3, [r2, r1]	@ movhi
	ldr	r2, .L3778
	str	r6, [r9, #8]
	add	r6, r6, #1
	ldr	r3, [r2, #-3332]
	str	r3, [r9, #4]
	add	r3, r3, #1
	cmn	r3, #1
	moveq	r3, #0
	str	r3, [r2, #-3332]
	ldr	r3, [sp, #56]
	str	r3, [r9, #12]
	ldrh	r3, [r5]
	strh	r3, [r9, #2]	@ movhi
	ldr	r3, [r4, #2436]
	add	r3, r3, #1
	str	r3, [r4, #2436]
	ldr	r3, [sp, #16]
	add	r3, r3, #1
	b	.L3775
.L3721:
	ldr	r3, [sp]
	ldr	r2, [sp, #16]
	ldr	r1, [sp, #20]
	rsb	r3, r2, r3
	ldr	r2, .L3778
	str	r3, [sp]
	ldr	r3, [r4, #2436]
	ldr	r2, [r2, #-544]
	cmp	r3, r2
	orrcs	r1, r1, #1
	cmp	r1, #0
	bne	.L3739
	ldrh	r3, [r5, #4]
	cmp	r3, #0
	beq	.L3739
.L3741:
	mov	r3, #0
	str	r3, [sp, #20]
	b	.L3713
.L3739:
	bl	FtlCacheWriteBack
	mov	r3, #0
	str	r3, [r10, #2436]
	ldr	r3, [sp]
	cmp	r3, #1
	bhi	.L3713
	b	.L3741
.L3777:
	mov	r0, r3
	ldr	r2, [sp, #8]
	ldr	r3, [sp, #28]
	rsb	r1, r2, r3
	bl	ftl_do_gc
	ldr	r3, .L3778+44
	ldrh	r3, [r3, #-12]
	cmp	r3, #5
	bls	.L3751
	cmp	r3, #31
	bhi	.L3746
	ldr	r3, .L3778+8
	ldrb	r3, [r3]	@ zero_extendqisi2
	cmp	r3, #0
	bne	.L3746
.L3751:
	ldr	r4, [sp]
	ldr	r5, .L3778+48
	ldr	r6, .L3778
	ldr	r7, .L3778+52
.L3764:
	ldrh	r2, [r5]
	movw	r3, #65535
	cmp	r2, r3
	bne	.L3745
	ldrh	r3, [r7]
	cmp	r3, r2
	bne	.L3745
	ldr	r2, .L3778+56
	ldrh	r2, [r2]
	cmp	r2, r3
	bne	.L3745
	and	r0, r4, #7
	bl	List_get_gc_head_node
	uxth	r0, r0
	bl	FtlGcRefreshBlock
.L3745:
	ldr	r3, .L3778+60
	mov	r0, #1
	mov	r1, r0
	mov	r2, #128
	strh	r2, [r3]	@ movhi
	strh	r2, [r3, #-2]	@ movhi
	bl	ftl_do_gc
	mov	r0, #0
	mov	r1, #1
	bl	ftl_do_gc
	ldr	r3, [r6, #-3620]
	cmp	r3, #0
	bne	.L3746
	ldr	r3, .L3778+64
	ldrh	r3, [r3]
	cmp	r3, #2
	bhi	.L3746
	add	r4, r4, #1
	cmp	r4, #256
	bne	.L3764
.L3746:
	mov	r0, #0
.L3706:
	add	sp, sp, #100
	@ sp needed
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L3779:
	.align	2
.L3778:
	.word	.LANCHOR2
	.word	.LANCHOR1
	.word	.LANCHOR0
	.word	.LANCHOR4
	.word	.LANCHOR2-3528
	.word	.LANCHOR2-3480
	.word	.LANCHOR0+2390
	.word	.LANCHOR0+2396
	.word	.LANCHOR0+2394
	.word	.LC161
	.word	-3947
	.word	.LANCHOR2-3520
	.word	.LANCHOR2-3288
	.word	.LANCHOR2-2670
	.word	.LANCHOR2-2668
	.word	.LANCHOR2-2722
	.word	.LANCHOR2-3532
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
	ldr	r3, .L3786
	ldr	r3, [r3, #504]
	cmp	r3, #1
	mvnne	r4, #0
	bne	.L3783
	mov	r2, r4
	mov	r0, r6
	mov	r1, r5
	bl	ftl_vendor_write
	mov	r4, r0
.L3783:
	bl	rknand_device_unlock
	mov	r0, r4
	ldmfd	sp!, {r4, r5, r6, pc}
.L3787:
	.align	2
.L3786:
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
	ldr	r4, .L3800
	.pad #28
	sub	sp, sp, #28
	ldr	r7, .L3800+4
	mov	r6, r0
	mov	r5, #0
	ldr	r3, [r4, #-528]
	mov	r9, r4
	str	r3, [r4, #1756]
	ldr	r3, [r4, #-504]
	str	r3, [r4, #1760]
.L3792:
	ldrh	r2, [r7]
	sxth	r3, r5
	cmp	r3, r2
	bge	.L3799
	mov	r1, #1
	ldr	r0, .L3800+8
	mov	r2, r1
	orr	r3, r3, r8
	str	r3, [r4, #1752]
	bl	FlashReadPages
	ldr	r3, [r4, #1760]
	mov	r1, r6
	ldr	r0, .L3800+12
	ldr	r2, [r3]
	str	r2, [sp]
	ldr	r2, [r3, #4]
	str	r2, [sp, #4]
	ldr	r2, [r3, #8]
	str	r2, [sp, #8]
	ldr	r3, [r3, #12]
	ldr	r2, [r4, #1748]
	str	r3, [sp, #12]
	ldr	r3, [r4, #1756]
	ldr	r3, [r3]
	str	r3, [sp, #16]
	ldr	r3, [r4, #1752]
	bl	printk
	ldr	r3, [r4, #1760]
	ldr	r3, [r3]
	cmn	r3, #1
	beq	.L3793
	ldr	r0, .L3800+16
	mov	r2, #4
	ldr	r1, [r9, #-528]
	mov	r3, #768
	bl	rknand_print_hex
.L3793:
	add	r5, r5, #1
	b	.L3792
.L3799:
	add	sp, sp, #28
	@ sp needed
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, pc}
.L3801:
	.align	2
.L3800:
	.word	.LANCHOR2
	.word	.LANCHOR0+2386
	.word	.LANCHOR2+1748
	.word	.LC162
	.word	.LC163
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
	movw	r3, #2324
	ldr	r6, .L3820
	.pad #52
	sub	sp, sp, #52
	ldr	r10, .L3820+4
	ldrh	r7, [r6, r3]
	add	fp, r10, #18
.L3803:
	ldrh	r3, [r10]
	ldr	r4, .L3820+8
	cmp	r3, r7
	bls	.L3816
	ldr	r1, .L3820+12
	mov	r8, #0
	ldr	r2, [r4, #-540]
	mov	r5, r8
	ldr	r3, [r4, #-2700]
	ldrh	ip, [r1]
	ldr	r9, [r4, #-2696]
	ldrh	r1, [r1, #80]
	str	r1, [sp, #28]
.L3812:
	uxth	r1, r8
	cmp	r1, ip
	bcs	.L3818
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
	bne	.L3804
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
.L3804:
	add	r8, r8, #1
	b	.L3812
.L3818:
	cmp	r5, #0
	beq	.L3807
	ldr	r0, [r4, #-540]
	mov	r1, r5
	mov	r2, #1
	mov	r8, #0
	bl	FlashReadPages
	mov	r9, #36
.L3808:
	uxth	r3, r8
	cmp	r3, r5
	bcs	.L3807
	ldr	r3, [r4, #-540]
	ldr	r0, .L3820+16
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
	b	.L3808
.L3807:
	add	r7, r7, #1
	uxth	r7, r7
	b	.L3803
.L3816:
	ldr	r9, .L3820+20
	mov	r8, #0
.L3811:
	ldr	r7, .L3820+8
	sxth	r5, r8
	sub	r3, r7, #392
	ldrh	r3, [r3]
	cmp	r5, r3
	bge	.L3814
	mov	r5, r5, asl #1
	mov	r7, #0
.L3815:
	ldrh	r2, [r9]
	sxth	r3, r7
	add	r7, r7, #1
	cmp	r3, r2
	bge	.L3819
	ldr	r2, [r4, #-476]
	mov	r1, #1
	ldr	r0, .L3820+24
	ldrh	r2, [r2, r5]
	orr	r3, r3, r2, asl #10
	mov	r2, r1
	str	r3, [r4, #1752]
	bl	FlashReadPages
	ldr	r3, [r4, #1760]
	ldr	r1, [r4, #-476]
	ldr	r2, [r4, #1756]
	ldr	r0, [r3]
	ldrh	r1, [r1, r5]
	str	r0, [sp]
	ldr	r0, [r3, #4]
	str	r0, [sp, #4]
	ldr	r0, [r3, #8]
	str	r0, [sp, #8]
	ldr	r3, [r3, #12]
	ldr	r0, .L3820+28
	str	r3, [sp, #12]
	ldr	r3, [r2]
	str	r3, [sp, #16]
	ldr	r3, [r2, #4]
	str	r3, [sp, #20]
	ldr	r2, [r4, #1748]
	ldr	r3, [r4, #1752]
	bl	printk
	b	.L3815
.L3819:
	add	r8, r8, #1
	b	.L3811
.L3814:
	ldr	r1, [r7, #-476]
	movw	r4, #2420
	ldr	r3, [r6, #2412]
	mov	r2, #2
	ldr	r0, .L3820+32
	bl	rknand_print_hex
	ldr	r1, [r7, #-456]
	ldrh	r3, [r6, r4]
	mov	r2, #4
	ldr	r0, .L3820+36
	bl	rknand_print_hex
	ldr	r0, .L3820+40
	ldr	r1, [r7, #-452]
	mov	r2, #4
	ldrh	r3, [r6, r4]
	add	sp, sp, #52
	@ sp needed
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, fp, lr}
	b	rknand_print_hex
.L3821:
	.align	2
.L3820:
	.word	.LANCHOR0
	.word	.LANCHOR0+2326
	.word	.LANCHOR2
	.word	.LANCHOR0+2316
	.word	.LC164
	.word	.LANCHOR0+2386
	.word	.LANCHOR2+1748
	.word	.LC111
	.word	.LC165
	.word	.LC166
	.word	.LC167
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
	ldr	r3, .L3824
	ldr	r2, [r3, #2256]
	ldr	r3, .L3824+4
	cmp	r2, r3
	bxne	lr
	b	flash_enter_slc_mode
.L3825:
	.align	2
.L3824:
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
	ldr	r3, .L3828
	ldr	r2, [r3, #2256]
	ldr	r3, .L3828+4
	cmp	r2, r3
	bxne	lr
	b	flash_exit_slc_mode
.L3829:
	.align	2
.L3828:
	.word	.LANCHOR0
	.word	1446522928
	.fnend
	.size	flash_boot_exit_slc_mode, .-flash_boot_exit_slc_mode
	.align	2
	.type	IdBlockReadData, %function
IdBlockReadData:
	.fnstart
	@ args = 0, pretend = 0, frame = 16
	@ frame_needed = 0, uses_anonymous_args = 0
	stmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.save {r4, r5, r6, r7, r8, r9, r10, fp, lr}
	mov	r6, r0
	ldr	r4, .L3839
	mov	r8, r1
	.pad #20
	sub	sp, sp, #20
	mov	r1, r6
	mov	fp, r2
	ldr	r0, .L3839+4
	ldr	r3, [r4, #1204]
	mov	r2, r8
	ldr	r5, [r4, #4]
	mov	r7, #0
	ldrb	r9, [r3, #9]	@ zero_extendqisi2
	bl	printk
	mov	r0, r6
	smulbb	r5, r5, r9
	uxth	r5, r5
	mov	r1, r5
	bl	__aeabi_uidivmod
	mul	ip, r9, r1
	mov	r10, r1
	rsb	r3, r1, r6
	str	r3, [sp]
	ubfx	ip, ip, #2, #2
.L3831:
	cmp	r7, r8
	bcs	.L3838
	add	r2, r7, r10
	ldrb	r1, [r4, #140]	@ zero_extendqisi2
	ubfx	r2, r2, #2, #16
	rsb	r5, ip, #4
	cmp	r1, #0
	add	r3, r4, r2, asl #1
	uxth	r5, r5
	ldrh	r3, [r3, #144]
	beq	.L3832
	ldr	r1, .L3839
	ldr	r0, .L3839+8
	ldr	r1, [r1, #2256]
	cmp	r1, r0
	moveq	r3, r2
.L3832:
	ldr	r2, [sp]
	add	r7, r5, r7
	add	r2, ip, r2
	ldrb	ip, [r4, #2304]	@ zero_extendqisi2
	uxth	r7, r7
	mla	r3, r9, r3, r2
	ldr	r2, [r4, #1204]
	str	ip, [sp, #4]
	ldrb	r1, [r2, #9]	@ zero_extendqisi2
	str	r3, [sp, #12]
	ldr	r3, .L3839+12
	str	r1, [sp, #8]
	ldrb	r0, [r3, #1710]	@ zero_extendqisi2
	bl	FlashBchSel
	mov	r0, #0
	bl	flash_boot_enter_slc_mode
	ldr	r3, [sp, #12]
	ldr	r1, [sp, #8]
	mov	r0, r3
	bl	__aeabi_uidiv
	mov	r2, fp
	add	fp, fp, r5, asl #9
	mov	r1, r0
	mov	r0, #0
	mov	r3, r0
	bl	FlashReadPage
	mov	r0, #0
	bl	flash_boot_exit_slc_mode
	ldr	ip, [sp, #4]
	mov	r0, ip
	bl	FlashBchSel
	mov	ip, #0
	b	.L3831
.L3838:
	mov	r1, r6
	mov	r2, r8
	mov	r3, #0
	ldr	r0, .L3839+16
	bl	printk
	mov	r0, #0
	add	sp, sp, #20
	@ sp needed
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L3840:
	.align	2
.L3839:
	.word	.LANCHOR0
	.word	.LC168
	.word	1446522928
	.word	.LANCHOR2
	.word	.LC169
	.fnend
	.size	IdBlockReadData, .-IdBlockReadData
	.align	2
	.global	write_idblock
	.type	write_idblock, %function
write_idblock:
	.fnstart
	@ args = 0, pretend = 0, frame = 120
	@ frame_needed = 0, uses_anonymous_args = 0
	stmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.save {r4, r5, r6, r7, r8, r9, r10, fp, lr}
	mov	r4, r0
	ldr	r7, .L3887
	.pad #132
	sub	sp, sp, #132
	mov	r0, #256000
	mov	r6, r1
	mov	r5, r2
	ldr	r3, [r7, #1204]
	ldr	r8, [r7, #4]
	ldrb	r9, [r3, #9]	@ zero_extendqisi2
	bl	ftl_malloc
	subs	r3, r0, #0
	str	r3, [sp, #12]
	beq	.L3865
	add	r4, r4, #508
	add	r4, r4, #3
	mov	r4, r4, lsr #9
	cmp	r4, #8
	bls	.L3863
	cmp	r4, #500
	bhi	.L3865
	b	.L3843
.L3863:
	mov	r4, #8
.L3843:
	ldr	r2, [r6]
	ldr	r3, .L3887+4
	cmp	r2, r3
	bne	.L3865
	smulbb	r3, r9, r8
	uxth	fp, r3
	sub	r0, fp, #1
	add	r0, r0, r4
	mov	r1, fp
	bl	__aeabi_uidiv
	mov	r3, #0
	movw	r2, #63871
	str	r0, [sp, #32]
	add	r0, r6, #254976
	add	r0, r0, #512
.L3847:
	ldr	r1, [r0, #-4]!
	cmp	r1, #0
	bne	.L3844
	ldr	r1, [r6, r3, asl #2]
	add	r3, r3, #1
	cmp	r3, #4096
	sub	r2, r2, #1
	movhi	r3, #0
	cmp	r2, #4096
	str	r1, [r0, #512]
	bne	.L3847
	b	.L3846
.L3844:
	ldr	r0, .L3887+8
	bl	printk
.L3846:
	mov	r1, r5
	mov	r3, #5
	ldr	r0, .L3887+12
	mov	r2, #4
	bl	rknand_print_hex
	ldrb	r2, [r7, #1]	@ zero_extendqisi2
	ldr	r1, [r6, #512]
	sub	r5, r5, #4
	ldr	r0, .L3887+16
	bl	printk
	ldr	r2, .L3887+20
	ldrh	r3, [r7, #138]
	mov	r1, r4
	ldr	r0, .L3887+24
	ldr	r2, [r2, #1704]
	str	r2, [sp]
	mov	r2, r4
	bl	printk
	ldrb	r3, [r7, #1]	@ zero_extendqisi2
	ldr	r7, .L3887
	ldr	r2, [r6, #512]
	cmp	r2, r3
	strhi	r3, [r6, #512]
	mov	r3, r4, asl #7
	str	r3, [sp, #36]
	mov	r3, #0
	str	r3, [sp, #20]
	str	r3, [sp, #16]
.L3861:
	ldr	r2, [r5, #4]
	ldrb	r3, [r7, #1]	@ zero_extendqisi2
	ldr	r9, .L3887
	cmp	r2, r3
	bcs	.L3849
	ldr	r3, .L3887+20
	ldr	r3, [r3, #1704]
	cmp	r2, r3
	bcc	.L3849
	ldr	r3, [sp, #32]
	ldr	r1, [sp, #32]
	cmp	r3, #1
	movls	r3, #0
	movhi	r3, #1
	str	r3, [sp, #44]
	ldr	r3, [sp, #16]
	cmp	r3, #0
	cmpne	r1, #1
	bls	.L3850
	ldr	r3, [r5]
	add	r3, r3, #1
	cmp	r2, r3
	beq	.L3849
.L3850:
	mov	r1, #0
	mov	r2, #512
	ldr	r0, [sp, #12]
	bl	memset
	ldr	r8, [r5, #4]
	ldr	r3, [r9, #1204]
	mov	r2, r4
	ldr	r9, [r9, #4]
	mul	r8, r8, fp
	ldrb	r3, [r3, #9]	@ zero_extendqisi2
	ldr	r0, .L3887+28
	mov	r1, r8
	str	r3, [sp, #24]
	ldrh	r3, [sp, #24]
	smulbb	r9, r9, r3
	bl	printk
	mov	r0, #0
	bl	flash_boot_enter_slc_mode
	mov	r0, r8
	uxth	r9, r9
	ldr	r1, [sp, #24]
	bl	__aeabi_uidiv
	mov	r1, r0
	mov	r0, #0
	mov	r2, r0
	bl	FlashEraseBlock
	cmp	r4, r9
	movls	r10, #1
	bls	.L3851
	mov	r0, #0
	add	r1, r8, r9
	mov	r2, r0
	mov	r10, #2
	bl	FlashEraseBlock
.L3851:
	mov	r0, #0
	bl	flash_boot_exit_slc_mode
	ldr	r3, [r7, #1204]
	ldrh	r0, [r3, #10]
	ldrb	r1, [r3, #12]	@ zero_extendqisi2
	mov	r0, r0, asl #2
	mul	r0, r10, r0
	bl	__aeabi_idiv
	mov	r1, r9
	mov	r9, #0
	str	r0, [sp, #52]
	mov	r0, r8
	bl	__aeabi_uidivmod
	mov	ip, r1
	rsb	r3, r1, r8
	str	r6, [sp, #28]
	str	r3, [sp, #40]
.L3852:
	ldr	r3, [sp, #52]
	cmp	r9, r3
	bcs	.L3886
	add	r3, r9, ip
	ubfx	r3, r3, #2, #16
	cmp	r3, #0
	beq	.L3853
	add	r2, r3, #1
	add	r1, r7, r2, asl #1
	ldrh	r10, [r1, #144]
	ldrb	r1, [r7, #140]	@ zero_extendqisi2
	cmp	r1, #0
	beq	.L3854
	ldr	r1, [r7, #2256]
	ldr	r0, .L3887+32
	cmp	r1, r0
	moveq	r10, r2
.L3854:
	sub	r10, r10, #-1073741823
	mov	r10, r10, asl #2
	str	r10, [sp, #64]
.L3853:
	movw	r2, #61424
	str	r2, [sp, #68]
	add	r2, r7, r3, asl #1
	ldrh	r10, [r2, #144]
	ldrb	r2, [r7, #140]	@ zero_extendqisi2
	cmp	r2, #0
	beq	.L3855
	ldr	r2, [r7, #2256]
	ldr	r1, .L3887+32
	cmp	r2, r1
	moveq	r10, r3
.L3855:
	ldr	r3, [sp, #24]
	add	r9, r9, #4
	ldr	r2, [sp, #40]
	str	ip, [sp, #60]
	uxth	r9, r9
	mla	r2, r3, r10, r2
	ldr	r3, .L3887
	ldrb	r3, [r3, #2304]	@ zero_extendqisi2
	str	r2, [sp, #56]
	str	r3, [sp, #48]
	ldr	r3, .L3887+20
	ldrb	r0, [r3, #1710]	@ zero_extendqisi2
	bl	FlashBchSel
	mov	r0, #0
	bl	flash_boot_enter_slc_mode
	ldr	r3, .L3887
	ldr	r3, [r3, #1204]
	ldrb	r1, [r3, #9]	@ zero_extendqisi2
	ldr	r2, [sp, #56]
	mov	r0, r2
	bl	__aeabi_uidiv
	add	r3, sp, #64
	mov	r1, r0
	ldr	r2, [sp, #28]
	mov	r0, #0
	bl	FlashProgPage
	mov	r0, #0
	bl	flash_boot_exit_slc_mode
	ldr	r0, [sp, #48]
	bl	FlashBchSel
	ldr	r1, [sp, #24]
	ldr	r0, [sp, #40]
	bl	__aeabi_uidiv
	add	r2, r10, #1
	uxth	r2, r2
	mov	r1, r0
	mov	r0, #0
	bl	FlashPageProgMsbFFData
	ldr	r3, [sp, #28]
	ldr	ip, [sp, #60]
	add	r3, r3, #2048
	str	r3, [sp, #28]
	b	.L3852
.L3886:
	mov	r1, r8
	mov	r3, #0
	mov	r2, r4
	ldr	r0, .L3887+36
	bl	printk
	ldr	r0, [r5, #4]
	mov	r1, r4
	mov	r8, #0
	mul	r0, r0, fp
	ldr	r2, [sp, #12]
	bl	IdBlockReadData
	mov	r3, r6
	ldr	r2, [sp, #12]
.L3860:
	mov	r10, r2
	mov	r9, r3
	ldr	r0, [r10]
	add	r2, r2, #4
	ldr	r1, [r9]
	add	r3, r3, #4
	cmp	r0, r1
	beq	.L3857
	mov	r1, #0
	mov	r2, #512
	ldr	r0, [sp, #12]
	bl	memset
	ldr	r3, [r10]
	ldr	r0, .L3887+40
	str	r3, [sp]
	ldr	r3, [r9]
	bic	r9, r8, #255
	ldr	r1, [sp, #16]
	mov	r9, r9, asl #2
	str	r3, [sp, #4]
	mov	r3, r8
	ldr	r2, [r5, #4]
	bl	printk
	ldr	r0, .L3887+44
	add	r1, r6, r9
	mov	r2, #4
	mov	r3, #256
	bl	rknand_print_hex
	mov	r2, #4
	ldr	r0, .L3887+48
	ldr	r3, [sp, #12]
	add	r1, r3, r9
	mov	r3, #256
	bl	rknand_print_hex
	mov	r0, #0
	bl	flash_boot_enter_slc_mode
	ldr	r1, [r5, #4]
	mov	r0, #0
	mov	r2, r0
	mul	r1, r1, fp
	bl	FlashEraseBlock
	ldr	r3, [sp, #44]
	cmp	r3, #0
	beq	.L3858
	ldr	r1, [r5, #4]
	mov	r0, #0
	mov	r2, r0
	mla	r1, r1, fp, fp
	bl	FlashEraseBlock
.L3858:
	mov	r0, #0
	bl	flash_boot_exit_slc_mode
	ldr	r0, .L3887+52
	ldr	r1, [r5, #4]
	bl	printk
	ldr	r3, [sp, #36]
	cmp	r8, r3
	bcc	.L3849
	b	.L3859
.L3857:
	ldr	r1, [sp, #36]
	add	r8, r8, #1
	cmp	r8, r1
	bne	.L3860
.L3859:
	ldr	r3, [sp, #20]
	add	r3, r3, #1
	str	r3, [sp, #20]
.L3849:
	ldr	r3, [sp, #16]
	add	r5, r5, #4
	add	r3, r3, #1
	str	r3, [sp, #16]
	cmp	r3, #5
	bne	.L3861
	ldr	r0, [sp, #12]
	bl	ftl_free
	ldr	r3, [sp, #20]
	clz	r0, r3
	mov	r0, r0, lsr #5
	rsb	r0, r0, #0
	b	.L3842
.L3865:
	mvn	r0, #0
.L3842:
	add	sp, sp, #132
	@ sp needed
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L3888:
	.align	2
.L3887:
	.word	.LANCHOR0
	.word	-52655045
	.word	.LC170
	.word	.LC171
	.word	.LC172
	.word	.LANCHOR2
	.word	.LC173
	.word	.LC174
	.word	1446522928
	.word	.LC175
	.word	.LC176
	.word	.LC177
	.word	.LC178
	.word	.LC179
	.fnend
	.size	write_idblock, .-write_idblock
	.align	2
	.global	CRC_32
	.type	CRC_32, %function
CRC_32:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	@ link register save eliminated.
	ldr	ip, .L3893
	add	r1, r0, r1
	mov	r2, #0
.L3890:
	cmp	r0, r1
	beq	.L3892
	ldrb	r3, [r0], #1	@ zero_extendqisi2
	eor	r3, r3, r2, lsr #24
	add	r3, ip, r3, asl #2
	ldr	r3, [r3, #3360]
	eor	r2, r3, r2, asl #8
	b	.L3890
.L3892:
	mov	r0, r2
	bx	lr
.L3894:
	.align	2
.L3893:
	.word	.LANCHOR1
	.fnend
	.size	CRC_32, .-CRC_32
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
	ldr	r4, .L3918
	bne	.L3896
	ldr	r2, [r2]
	ldr	r3, .L3918+4
	cmp	r2, r3
	bne	.L3896
	mov	r0, #256000
	mov	r3, #1
	strb	r3, [r4, #2016]
	bl	ftl_malloc
	mov	r1, #0
	mov	r2, #256000
	str	r0, [r4, #2020]
	bl	ftl_memset
	str	r5, [r4, #2024]
.L3896:
	str	r6, [sp]
	mov	r3, r5
	ldr	r0, .L3918+8
	ldr	r1, [r4, #2020]
	ldr	r2, [r8]
	bl	printk
	ldrb	r3, [r4, #2016]	@ zero_extendqisi2
	ldr	r9, .L3918
	cmp	r3, #0
	beq	.L3895
	sub	r0, r5, #64
	ldr	r7, [r9, #2020]
	cmp	r0, #500
	bcs	.L3898
	rsb	r2, r5, #564
	add	r0, r7, r0, asl #9
	cmp	r2, r6
	mov	r1, r8
	movcs	r2, r6
	mov	r2, r2, asl #9
	bl	ftl_memcpy
	b	.L3899
.L3898:
	cmp	r5, #564
	bcs	.L3907
.L3899:
	ldr	r3, [r4, #2024]
	cmp	r3, r5
	beq	.L3905
	ldr	r2, .L3918
	mov	r3, #0
	cmp	r7, r3
	mov	r8, r3
	strb	r3, [r2, #2016]
	beq	.L3906
	mov	r0, r7
	bl	ftl_free
.L3906:
	str	r8, [r4, #2020]
.L3905:
	add	r5, r5, r6
	str	r5, [r4, #2024]
	b	.L3895
.L3907:
	ldr	r3, .L3918+12
	ldr	r0, [r9, #2024]
	ldr	r3, [r3, #1204]
	sub	r0, r0, #64
	cmp	r0, #500
	ldrb	r3, [r3, #9]	@ zero_extendqisi2
	movcs	r0, #500
	cmp	r3, #4
	beq	.L3908
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
	b	.L3901
.L3908:
	mov	r3, #0
.L3900:
	cmp	r0, #256
	add	r1, sp, #8
	mov	r2, r3, asl #1
	movls	r2, r3
	str	r2, [r1, r3, asl #2]
	add	r3, r3, #1
	cmp	r3, #5
	bne	.L3900
.L3901:
	movw	r3, #63872
.L3904:
	ldr	r2, [r7, r3, asl #2]
	cmp	r2, #0
	addne	r3, r3, #128
	movne	r0, r3, asl #2
	bne	.L3903
.L3902:
	sub	r3, r3, #1
	cmp	r3, #4096
	bne	.L3904
	mov	r0, r0, asl #9
.L3903:
	mov	r1, r7
	add	r2, sp, #8
	bl	write_idblock
	ldr	r0, [r4, #2020]
	mov	r7, #0
	strb	r7, [r4, #2016]
	bl	ftl_free
	str	r7, [r4, #2020]
	b	.L3905
.L3895:
	add	sp, sp, #52
	@ sp needed
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, pc}
.L3919:
	.align	2
.L3918:
	.word	.LANCHOR4
	.word	-52655045
	.word	.LC180
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
	beq	.L3921
	mov	r0, r1
	mov	r2, r3
	mov	r1, r6
	bl	write_loader_lba
.L3921:
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
	@ args = 0, pretend = 0, frame = 528
	@ frame_needed = 0, uses_anonymous_args = 0
	ldr	r3, .L4032
	stmfd	sp!, {r4, r5, r6, r7, lr}
	.save {r4, r5, r6, r7, lr}
	cmp	r1, r3
	.pad #532
	sub	sp, sp, #532
	mov	r4, r1
	mov	r5, r2
	beq	.L3928
	bhi	.L3929
	sub	r3, r3, #3456
	sub	r3, r3, #5
	cmp	r1, r3
	beq	.L3930
	bhi	.L3931
	sub	r3, r3, #125
	cmp	r1, r3
	beq	.L3932
	bhi	.L3933
	sub	r3, r3, #237
	cmp	r1, r3
	bne	.L3989
	bl	rknand_dev_flush
	b	.L4020
.L3933:
	ldr	r3, .L4032+4
	cmp	r1, r3
	beq	.L3935
	add	r3, r3, #1
	cmp	r1, r3
	beq	.L3936
	b	.L3989
.L3931:
	ldr	r3, .L4032+8
	cmp	r1, r3
	beq	.L3937
	bhi	.L3938
	sub	r3, r3, #1952
	sub	r3, r3, #9
	cmp	r1, r3
	beq	.L3939
	b	.L3989
.L3938:
	ldr	r3, .L4032+12
	cmp	r1, r3
	beq	.L3937
	add	r3, r3, #10
	cmp	r1, r3
	beq	.L3937
	b	.L3989
.L3929:
	ldr	r3, .L4032+16
	cmp	r1, r3
	beq	.L3940
	bhi	.L3941
	sub	r3, r3, #78
	cmp	r1, r3
	beq	.L3942
	bcc	.L3943
	add	r3, r3, #21
	cmp	r1, r3
	beq	.L3944
	add	r3, r3, #56
	cmp	r1, r3
	beq	.L3945
	b	.L3989
.L3941:
	ldr	r3, .L4032+20
	cmp	r1, r3
	mov	r6, r3
	beq	.L3946
	bhi	.L3947
	sub	r3, r3, #956
	sub	r3, r3, #1
	cmp	r1, r3
	beq	.L3948
	add	r3, r3, #956
	cmp	r1, r3
	beq	.L3949
	b	.L3989
.L3947:
	ldr	r3, .L4032+24
	cmp	r1, r3
	beq	.L3949
	add	r3, r3, #1
	cmp	r1, r3
	beq	.L3946
	b	.L3989
.L3943:
	ldr	r0, .L4032+28
	bl	printk
	mov	r0, #4096
	bl	ftl_malloc
	subs	r4, r0, #0
	bne	.L3950
.L3955:
	mvn	r0, #11
	b	.L3927
.L3950:
	mov	r1, r5
	mov	r2, #512
	bl	rk_copy_from_user
	cmp	r0, #0
	bne	.L4026
	ldr	r0, .L4032+32
	ldmia	r4, {r1, r2}
	bl	printk
	ldr	r3, [r4, #4]
	cmp	r3, #8
	str	r3, [sp, #4]
	bhi	.L4024
	bl	rknand_device_lock
	mov	r2, r4
	ldr	r0, [r4]
	ldr	r1, [sp, #4]
	bl	IdBlockReadData
	bl	rknand_device_unlock
	mov	r0, r5
	mov	r1, r4
	ldr	r2, [sp, #4]
	mov	r2, r2, asl #9
	bl	rk_copy_to_user
	cmp	r0, #0
	beq	.L4019
	ldr	r0, .L4032+36
.L4022:
	bl	printk
.L4024:
	mov	r0, r4
.L4025:
	bl	ftl_free
.L4023:
	mvn	r0, #13
	b	.L3927
.L3942:
	ldr	r0, .L4032+40
	bl	printk
	mov	r0, #4096
	bl	ftl_malloc
	subs	r4, r0, #0
	beq	.L3955
	mov	r1, r5
	mov	r2, #4096
	bl	rk_copy_from_user
	cmp	r0, #0
	beq	.L3956
.L4026:
	ldr	r0, .L4032+44
	b	.L4022
.L3956:
	ldr	r5, .L4032+48
	ldr	r0, .L4032+52
	ldmia	r4, {r1, r2}
	bl	printk
	ldr	r6, [r5, #2028]
	mov	r7, r5
	cmp	r6, #0
	bne	.L3957
	mov	r0, #260096
	bl	ftl_malloc
	cmp	r0, #0
	str	r0, [r5, #2028]
	beq	.L4024
	mov	r1, r6
	mov	r2, #260096
	bl	memset
.L3957:
	ldr	r2, [r4, #4]
	movw	r3, #4088
	cmp	r2, r3
	bhi	.L4024
	ldr	r3, [r4]
	cmp	r3, #251904
	bhi	.L4024
	ldr	r0, [r7, #2028]
	add	r1, r4, #8
	add	r0, r0, r3
	bl	memcpy
.L4019:
	mov	r0, r4
	bl	ftl_free
.L4020:
	mov	r4, #0
	b	.L3954
.L3945:
	ldr	r0, .L4032+56
	bl	printk
	mov	r0, #4096
	bl	ftl_malloc
	subs	r6, r0, #0
	beq	.L3955
	mov	r1, r5
	mov	r2, #28
	bl	rk_copy_from_user
	cmp	r0, #0
	beq	.L3960
	ldr	r0, .L4032+44
	bl	printk
	b	.L3961
.L3960:
	ldmia	r6, {r1, r2}
	ldr	r0, .L4032+60
	bl	printk
	ldr	r1, [r6]
	cmp	r1, #256000
	bhi	.L3961
	ldr	r5, .L4032+48
	ldr	r0, [r5, #2028]
	cmp	r0, #0
	beq	.L3961
	bl	CRC_32
	ldr	r3, [r6, #4]
	cmp	r3, r0
	beq	.L3963
	mov	r0, r6
	bl	ftl_free
	b	.L3990
.L3963:
	bl	rknand_device_lock
	ldr	r1, [r5, #2028]
	add	r2, r6, #8
	ldr	r0, [r6]
	bl	write_idblock
	mov	r4, #0
	bl	rknand_device_unlock
	ldr	r0, [r5, #2028]
	bl	ftl_free
	str	r4, [r5, #2028]
	mov	r0, r6
	b	.L3967
.L3961:
	mov	r0, r6
	b	.L4025
.L3944:
	ldr	r0, .L4032+64
	bl	printk
	mov	r0, #4096
	bl	ftl_malloc
	subs	r6, r0, #0
	beq	.L3955
	bl	ftl_read_flash_info
	mov	r0, r5
	mov	r1, r6
	mov	r2, #11
	b	.L4030
.L3928:
	ldr	r0, .L4032+68
	bl	printk
	bl	nand_blk_add_whole_disk
	mov	r0, #4096
	bl	ftl_malloc
	subs	r6, r0, #0
	beq	.L3955
	mov	r1, #0
	mov	r2, #64
	bl	ftl_memset
	mov	r0, r5
	mov	r1, r6
	mov	r2, #64
	b	.L4030
.L3940:
	ldr	r0, .L4032+72
	bl	printk
	ldr	r3, .L4032+48
	mov	r0, r5
	mov	r2, #4
	ldr	r3, [r3, #2032]
	ldr	r3, [r3, #20]
	add	r1, sp, #528
	str	r3, [r1, #-524]!
.L4014:
	bl	rk_copy_to_user
	cmp	r0, #0
	bne	.L4023
	b	.L4020
.L3948:
	ldr	r0, .L4032+76
	bl	printk
	mov	r0, #4096
	bl	ftl_malloc
	subs	r6, r0, #0
	beq	.L3955
	bl	rknand_device_lock
	mov	r1, #264
	mov	r2, #2
	mov	r3, r6
	mov	r0, #16
	bl	ftl_read
	bl	rknand_device_unlock
	mov	r0, r5
	mov	r1, r6
	mov	r2, #1024
.L4030:
	bl	rk_copy_to_user
	subs	r4, r0, #0
	mov	r0, r6
	bne	.L4025
.L3967:
	bl	ftl_free
	b	.L3954
.L3932:
	ldr	r0, .L4032+80
	bl	printk
	mov	r1, r5
	mov	r2, #520
	add	r0, sp, #8
	bl	rk_copy_from_user
	subs	r4, r0, #0
	beq	.L3968
.L3973:
	ldr	r0, .L4032+44
	bl	printk
	b	.L4023
.L3968:
	ldr	r2, [sp, #8]
	ldr	r3, .L4032+84
	cmp	r2, r3
	beq	.L3969
.L3970:
	mvn	r4, #0
	b	.L3954
.L3969:
	ldr	r3, [sp, #12]
	cmp	r3, #512
	bhi	.L3970
	ldr	r6, .L4032+48
	mov	r2, #512
	add	r0, sp, #8
	ldr	r1, [r6, #2032]
	bl	memcpy
	ldr	r2, [r6, #2036]
	ldr	r3, .L4032+88
	cmp	r2, r3
	beq	.L3971
	add	r0, sp, #72
	mov	r1, r4
	mov	r2, #128
	str	r4, [sp, #16]
	str	r4, [sp, #20]
	bl	memset
.L3971:
	mov	r1, #0
	mov	r2, #256
	add	r0, sp, #264
	str	r1, [sp, #24]
	bl	memset
	b	.L4028
.L3935:
	ldr	r0, .L4032+92
	bl	printk
	mov	r1, r5
	mov	r2, #520
	add	r0, sp, #8
	bl	rk_copy_from_user
	cmp	r0, #0
	bne	.L3973
	ldr	r2, [sp, #8]
	ldr	r3, .L4032+84
	cmp	r2, r3
	bne	.L3970
	ldr	r3, [sp, #12]
	cmp	r3, #512
	bhi	.L3970
	ldr	r2, .L4032+48
	ldr	r3, .L4032+88
	ldr	r1, [r2, #2036]
	cmp	r1, r3
	bne	.L3990
	ldr	r3, [sp, #20]
	sub	r1, r3, #1
	cmp	r1, #127
	mvnhi	r0, #2
	bhi	.L3927
	ldr	r4, [r2, #2032]
	add	r1, sp, #72
	add	r0, r4, #64
	str	r3, [r4, #12]
	ldr	r2, [sp, #20]
	bl	memcpy
	mov	r0, #1
	mov	r1, r4
	b	.L4021
.L3939:
	ldr	r0, .L4032+96
	bl	printk
	mov	r1, r5
	mov	r2, #520
	add	r0, sp, #8
	bl	rk_copy_from_user
	cmp	r0, #0
	bne	.L3973
	ldr	r2, [sp, #8]
	ldr	r3, .L4032+100
	cmp	r2, r3
	bne	.L3970
	ldr	r3, [sp, #12]
	cmp	r3, #512
	bhi	.L3970
	ldr	r5, .L4032+48
	ldr	r3, [r5, #2040]
	cmp	r3, #0
	bne	.L3974
.L3977:
	mov	r0, #0
	b	.L3927
.L3974:
	ldr	r3, [r5, #2044]
	ldr	r2, .L4032+104
	ldr	r1, [r3]
	cmp	r1, r2
	beq	.L3975
	str	r2, [r3]
	mov	r2, #504
	ldr	r3, [r5, #2044]
	str	r2, [r3, #4]
	mov	r2, #0
	str	r2, [r3, #8]
	str	r2, [r3, #12]
.L3975:
	ldr	r1, [r5, #2044]
	mov	r4, #0
	mov	r0, r4
	str	r4, [r1, #16]
	bl	StorageSysDataStore
	ldr	r3, [r5, #2032]
	ldr	r2, .L4032+84
	ldr	r1, [r3]
	cmp	r1, r2
	strne	r2, [r3]
	ldr	r6, [r5, #2032]
	ldrne	r3, .L4032+48
	movne	r2, #504
	add	r0, r6, #64
	ldrne	r3, [r3, #2032]
	stmneib	r3, {r2, r4}
	mov	r4, #0
	mov	r1, r4
	mov	r2, #128
	str	r4, [r6, #12]
	bl	memset
	mov	r0, #1
	mov	r1, r6
	bl	StorageSysDataStore
	str	r4, [r5, #2040]
	str	r4, [r5, #2036]
	b	.L3954
.L3930:
	ldr	r0, .L4032+108
	bl	printk
	mov	r1, r5
	mov	r2, #520
	add	r0, sp, #8
	bl	rk_copy_from_user
	cmp	r0, #0
	bne	.L3973
	ldr	r2, [sp, #8]
	ldr	r3, .L4032+112
	cmp	r2, r3
	bne	.L3970
	ldr	r3, [sp, #12]
	cmp	r3, #512
	bhi	.L3970
	ldr	r5, .L4032+48
	ldr	r3, [r5, #2040]
	cmp	r3, #1
	beq	.L3977
	ldr	r3, [r5, #2044]
	mov	r4, #0
	ldr	r2, .L4032+104
	ldr	r1, [r3]
	cmp	r1, r2
	strne	r2, [r3]
	ldr	r1, [r5, #2044]
	ldrne	r3, [r5, #2044]
	movne	r2, #504
	strne	r0, [r3, #8]
	strne	r2, [r3, #4]
	strne	r0, [r3, #12]
	mov	r3, #1
	mov	r0, #0
	str	r3, [r1, #16]
	bl	StorageSysDataStore
	ldr	r3, [r5, #2032]
	ldr	r2, .L4032+84
	ldr	r1, [r3]
	cmp	r1, r2
	strne	r2, [r3]
	ldr	r6, [r5, #2032]
	ldrne	r3, .L4032+48
	movne	r1, #504
	movne	r2, #0
	add	r0, r6, #64
	ldrne	r3, [r3, #2032]
	stmneib	r3, {r1, r2}
	mov	r1, r4
	mov	r2, #128
	str	r4, [r6, #12]
	bl	memset
	mov	r0, #1
	mov	r1, r6
	bl	StorageSysDataStore
	mov	r3, #1
	str	r3, [r5, #2040]
	b	.L3954
.L3936:
	ldr	r0, .L4032+116
	bl	printk
	mov	r1, r5
	mov	r2, #520
	add	r0, sp, #8
	bl	rk_copy_from_user
	cmp	r0, #0
	bne	.L3973
	ldr	r2, [sp, #8]
	ldr	r3, .L4032+120
	cmp	r2, r3
	bne	.L3970
	ldr	r2, [sp, #12]
	cmp	r2, #512
	addls	r0, sp, #16
	ldrls	r1, .L4032+124
	bls	.L4027
	b	.L3970
.L3937:
	ldr	r0, .L4032+12
	cmp	r4, r0
	mov	r7, r0
	ldreq	r0, .L4032+128
	beq	.L4017
	ldr	r3, .L4032+132
	cmp	r4, r3
	ldreq	r0, .L4032+136
	ldrne	r0, .L4032+140
.L4017:
	bl	printk
	mov	r1, r5
	mov	r2, #520
	add	r0, sp, #8
	bl	rk_copy_from_user
	cmp	r0, #0
	bne	.L3973
	ldr	r2, [sp, #8]
	ldr	r3, .L4032+144
	cmp	r2, r3
	bne	.L4023
	ldr	r3, .L4032+132
	ldr	r6, .L4032+48
	cmp	r4, r3
	bne	.L3983
	ldr	r3, [r6, #2032]
	mov	r0, r5
	add	r1, sp, #8
	mov	r2, #16
	ldr	r3, [r3, #20]
	str	r3, [sp, #12]
	strb	r3, [sp, #16]
	bl	rk_copy_to_user
	cmp	r0, #0
	beq	.L3927
	b	.L4023
.L3983:
	ldr	r3, [r6, #2560]
	cmp	r3, #10
	bhi	.L4023
	ldr	r1, [r6, #2032]
	ldr	r2, [sp, #12]
	ldr	r3, [r1, #24]
	cmp	r3, r2
	cmpne	r3, #0
	movne	r3, #1
	moveq	r3, #0
	beq	.L3984
	ldr	r0, .L4032+148
	mov	r1, r2
	bl	printk
	ldr	r3, [r6, #2560]
	add	r3, r3, #1
	str	r3, [r6, #2560]
	b	.L4023
.L3984:
	cmp	r4, r7
	str	r3, [r6, #2560]
	mov	r0, #1
	moveq	r2, r3
	movne	r3, #1
	moveq	r3, r2
	str	r2, [r1, #24]
	str	r3, [r1, #20]
	bl	StorageSysDataStore
	cmn	r0, #1
	bne	.L4020
	mvn	r4, #1
	b	.L3954
.L3949:
	ldr	r0, .L4032+152
	bl	printk
	mov	r1, r5
	mov	r2, #520
	add	r0, sp, #8
	bl	rk_copy_from_user
	cmp	r0, #0
	bne	.L3973
	ldr	r2, [sp, #8]
	ldr	r3, .L4032+156
	cmp	r2, r3
	bne	.L3970
	ldr	r2, [sp, #12]
	cmp	r2, #504
	bhi	.L3970
	ldr	r3, .L4032+160
	add	r0, sp, #16
	cmp	r4, r3
	ldr	r3, .L4032+48
	ldreq	r1, [r3, #2564]
	ldrne	r1, [r3, #2568]
	add	r1, r1, #8
.L4027:
	bl	memcpy
.L4028:
	mov	r0, r5
	add	r1, sp, #8
	mov	r2, #520
	b	.L4014
.L3946:
	ldr	r0, .L4032+164
	bl	printk
	mov	r1, r5
	mov	r2, #520
	add	r0, sp, #8
	bl	rk_copy_from_user
	cmp	r0, #0
	bne	.L3973
	ldr	r2, [sp, #8]
	ldr	r3, .L4032+156
	cmp	r2, r3
	bne	.L3970
	ldr	r2, [sp, #12]
	cmp	r2, #504
	bhi	.L3970
	cmp	r4, r6
	add	r2, r2, #8
	ldr	r4, .L4032+48
	bne	.L3988
	add	r1, sp, #8
	ldr	r0, [r4, #2564]
	bl	memcpy
	mov	r0, #2
	ldr	r1, [r4, #2564]
	b	.L4021
.L3988:
	add	r1, sp, #8
	ldr	r0, [r4, #2568]
	bl	memcpy
	ldr	r1, [r4, #2568]
	mov	r0, #3
.L4021:
	bl	StorageSysDataStore
	mov	r4, r0
.L3954:
	ldr	r0, .L4032+168
	mov	r1, r4
	bl	printk
	mov	r0, r4
	b	.L3927
.L3989:
	mvn	r0, #21
	b	.L3927
.L3990:
	mvn	r0, #1
.L3927:
	add	sp, sp, #532
	@ sp needed
	ldmfd	sp!, {r4, r5, r6, r7, pc}
.L4033:
	.align	2
.L4032:
	.word	1074033155
	.word	1074029570
	.word	1074031656
	.word	1074031666
	.word	1074033235
	.word	1074034193
	.word	1074034194
	.word	.LC181
	.word	.LC183
	.word	.LC184
	.word	.LC185
	.word	.LC182
	.word	.LANCHOR4
	.word	.LC186
	.word	.LC187
	.word	.LC188
	.word	.LC189
	.word	.LC190
	.word	.LC191
	.word	.LC192
	.word	.LC193
	.word	1263358532
	.word	-1067903959
	.word	.LC194
	.word	.LC195
	.word	1112753220
	.word	1146313043
	.word	.LC196
	.word	1112755781
	.word	.LC197
	.word	1094995539
	.word	.LANCHOR4+2048
	.word	.LC198
	.word	1074031676
	.word	.LC199
	.word	.LC200
	.word	1280262987
	.word	.LC201
	.word	.LC202
	.word	1145980246
	.word	1074034192
	.word	.LC203
	.word	.LC204
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
	ldr	r4, .L4044
	mvn	r3, #0
	mov	r5, #0
	add	r0, r4, #2048
	ldr	r1, [r4, #2000]
	str	r3, [r4, #2024]
	add	r3, r1, #512
	str	r3, [r4, #2032]
	str	r1, [r4, #2044]
	add	r3, r1, #1024
	add	r1, r1, #1536
	str	r3, [r4, #2564]
	strb	r5, [r4, #2016]
	str	r5, [r4, #2020]
	str	r5, [r4, #2028]
	str	r1, [r4, #2568]
	bl	ftl_memcpy
	ldr	r6, [r4, #2044]
	str	r5, [r4, #2036]
	str	r5, [r4, #2560]
	ldr	r7, [r6, #508]
	ldr	r3, [r6, #16]
	cmp	r7, r5
	str	r3, [r4, #2040]
	beq	.L4035
	mov	r0, r6
	mov	r1, #508
	bl	js_hash
	cmp	r7, r0
	beq	.L4035
	str	r5, [r6, #16]
	ldr	r0, .L4044+4
	str	r5, [r4, #2040]
	bl	printk
.L4035:
	ldr	r3, [r4, #2040]
	mov	r0, #2
	ldr	r1, [r4, #2564]
	cmp	r3, #0
	ldrne	r3, .L4044
	ldrne	r2, .L4044+8
	strne	r2, [r3, #2036]
	bl	StorageSysDataLoad
	ldr	r1, [r4, #2568]
	mov	r0, #3
	bl	StorageSysDataLoad
	ldmfd	sp!, {r3, r4, r5, r6, r7, lr}
	b	rknand_sys_storage_init
.L4045:
	.align	2
.L4044:
	.word	.LANCHOR4
	.word	.LC205
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
	ldr	r6, .L4058
	cmp	r0, #0
	str	r0, [r6, #2572]
	beq	.L4053
	ldr	r9, .L4058+4
	mov	r8, #0
	mov	r4, r8
	mov	r7, r8
	mov	r10, r6
.L4051:
	mov	r0, r7, asl #7
	mov	r1, #128
	ldr	r2, [r6, #2572]
	bl	FlashBootVendorRead
	cmp	r0, #0
	bne	.L4049
	ldr	r3, [r10, #2572]
	ldr	r0, .L4058+8
	add	r2, r3, #61440
	ldr	r1, [r3]
	ldr	r2, [r2, #4092]
	ldr	r3, [r3, #4]
	bl	printk
	ldr	r5, [r10, #2572]
	ldr	r3, [r5]
	cmp	r3, r9
	bne	.L4050
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
.L4050:
	cmp	r7, #1
	movne	r7, #1
	bne	.L4051
.L4057:
	cmp	r4, #0
	beq	.L4052
	mov	r0, r8, asl #7
	mov	r1, #128
	mov	r2, r5
	bl	FlashBootVendorRead
	cmp	r0, #0
	bne	.L4049
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, pc}
.L4052:
	mov	r0, r5
	mov	r1, r4
	mov	r2, #65536
	bl	memset
	ldr	r3, .L4058+4
	str	r7, [r5, #4]
	mov	r0, r4
	str	r3, [r5]
	add	r3, r5, #61440
	str	r7, [r3, #4092]
	ldr	r3, .L4058+12
	strh	r4, [r5, #12]	@ movhi
	strh	r3, [r5, #14]	@ movhi
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, pc}
.L4049:
	ldr	r0, [r6, #2572]
	bl	kfree
	mov	r3, #0
	mvn	r0, #0
	str	r3, [r6, #2572]
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, pc}
.L4053:
	mvn	r0, #11
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, pc}
.L4059:
	.align	2
.L4058:
	.word	.LANCHOR4
	.word	1380668996
	.word	.LC206
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
	ldr	r3, .L4067
	ldr	ip, [r3, #2572]
	cmp	ip, #0
	ldrneh	r4, [ip, #10]
	movne	r3, #0
	beq	.L4065
.L4062:
	cmp	r3, r4
	bcs	.L4065
	add	lr, ip, r3, asl #3
	ldrh	r5, [lr, #16]
	cmp	r5, r0
	bne	.L4063
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
.L4063:
	add	r3, r3, #1
	b	.L4062
.L4065:
	mvn	r0, #0
	ldmfd	sp!, {r3, r4, r5, pc}
.L4068:
	.align	2
.L4067:
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
	ldr	r2, .L4092
	.pad #28
	sub	sp, sp, #28
	ldr	r4, [r2, #2572]
	cmp	r4, #0
	beq	.L4084
	mov	r3, r1
	add	r7, r9, #63
	ldrh	r1, [r4, #8]
	bic	r7, r7, #63
	ldrh	r2, [r4, #10]
	mov	ip, r0
	mov	r6, #0
	str	r1, [sp, #4]
.L4071:
	cmp	r6, r2
	bcs	.L4090
	add	r5, r4, r6, asl #3
	ldrh	r1, [r5, #16]
	cmp	r1, ip
	bne	.L4072
	ldrh	r1, [r5, #20]
	add	fp, r4, #1024
	add	r1, r1, #63
	bic	r1, r1, #63
	str	r1, [sp, #8]
	cmp	r9, r1
	bls	.L4073
	ldrh	r1, [r4, #14]
	cmp	r1, r7
	subcs	r2, r2, #1
	strcs	r2, [sp, #12]
	ldrcsh	r8, [r5, #18]
	bcc	.L4084
.L4074:
	ldr	r2, [sp, #12]
	add	r5, r5, #8
	cmp	r6, r2
	bcs	.L4091
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
	b	.L4074
.L4091:
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
	b	.L4089
.L4073:
	ldrh	r0, [r5, #18]
	mov	r1, r3
	mov	r2, r9
	add	r0, fp, r0
	bl	memcpy
	strh	r9, [r5, #20]	@ movhi
	b	.L4089
.L4072:
	add	r6, r6, #1
	b	.L4071
.L4090:
	ldrh	r1, [r4, #14]
	cmp	r1, r7
	bcc	.L4084
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
.L4089:
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
	b	.L4070
.L4084:
	mvn	r0, #0
.L4070:
	add	sp, sp, #28
	@ sp needed
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L4093:
	.align	2
.L4092:
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
	beq	.L4095
	ldr	r3, .L4111
	cmp	r6, r3
	beq	.L4097
	add	r3, r3, #1
	cmp	r6, r3
	beq	.L4098
	b	.L4109
.L4097:
	mov	r1, r5
	mov	r2, #8
	bl	rk_copy_from_user
	cmp	r0, #0
	bne	.L4109
	ldr	r2, [r4]
	ldr	r3, .L4111+4
	cmp	r2, r3
	beq	.L4100
.L4101:
	mvn	r5, #0
	b	.L4096
.L4100:
	ldrh	r0, [r4, #4]
	add	r1, r4, #8
	ldrh	r2, [r4, #6]
	bl	rk_ftl_vendor_read
	cmn	r0, #1
	beq	.L4101
	uxth	r2, r0
	strh	r0, [r4, #6]	@ movhi
	mov	r1, r4
	mov	r0, r5
	add	r2, r2, #8
	bl	rk_copy_to_user
	cmp	r0, #0
	moveq	r5, #0
	mvnne	r5, #13
	b	.L4096
.L4098:
	mov	r1, r5
	mov	r2, #8
	bl	rk_copy_from_user
	cmp	r0, #0
	bne	.L4109
	ldr	r2, [r4]
	ldr	r3, .L4111+4
	cmp	r2, r3
	bne	.L4101
	ldrh	r2, [r4, #6]
	movw	r3, #4087
	cmp	r2, r3
	bhi	.L4101
	mov	r0, r4
	mov	r1, r5
	add	r2, r2, #8
	bl	rk_copy_from_user
	cmp	r0, #0
	bne	.L4109
	ldrh	r0, [r4, #4]
	add	r1, r4, #8
	ldrh	r2, [r4, #6]
	bl	rk_ftl_vendor_write
	mov	r5, r0
	b	.L4096
.L4109:
	mvn	r5, #13
.L4096:
	mov	r0, r4
	bl	kfree
.L4095:
	mov	r0, r5
	ldmfd	sp!, {r4, r5, r6, pc}
.L4112:
	.align	2
.L4111:
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
	.type	__func__.20269, %object
	.size	__func__.20269, 11
__func__.20269:
	.ascii	"FtlMemInit\000"
.LC0:
	.byte	60
	.byte	40
	.byte	24
	.byte	16
	.space	1
	.type	__func__.21044, %object
	.size	__func__.21044, 17
__func__.21044:
	.ascii	"FtlDumpBlockInfo\000"
	.space	3
	.type	__func__.21063, %object
	.size	__func__.21063, 16
__func__.21063:
	.ascii	"FtlScanAllBlock\000"
	.type	__func__.21329, %object
	.size	__func__.21329, 17
__func__.21329:
	.ascii	"ftl_scan_all_ppa\000"
	.space	3
	.type	__func__.21012, %object
	.size	__func__.21012, 12
__func__.21012:
	.ascii	"FtlCheckVpc\000"
	.type	__func__.21311, %object
	.size	__func__.21311, 21
__func__.21311:
	.ascii	"FtlVpcCheckAndModify\000"
	.space	3
	.type	__func__.20342, %object
	.size	__func__.20342, 8
__func__.20342:
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
	.ascii	"FTL version: 5.0.60 20200218\000"
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
	.ascii	"ECC:%d\012\000"
.LC96:
	.ascii	"No.%d FLASH ID:%x %x %x %x %x %x\012\000"
.LC97:
	.ascii	"FlashLoadPhyInfo fail %x!!\012\000"
.LC98:
	.ascii	"Read pageadd=%x  ecc=%x err=%x\012\000"
.LC99:
	.ascii	"data:\000"
.LC100:
	.ascii	"spare:\000"
.LC101:
	.ascii	"ReadRetry pageadd=%x ecc=%x err=%x\012\000"
.LC102:
	.ascii	"FLFB:%d %d\012\000"
.LC103:
	.ascii	"prog error: = %x\012\000"
.LC104:
	.ascii	"prog read error: = %x\012\000"
.LC105:
	.ascii	"prog read s error: = %x %x %x\012\000"
.LC106:
	.ascii	"prog read d error: = %x %x %x\012\000"
.LC107:
	.ascii	"id = %x,%x addr= %x,spare= %x %x %x %x data= %x\012"
	.ascii	"\000"
.LC108:
	.ascii	"...%s enter...\012\000"
.LC109:
	.ascii	"superBlkID = %x vpc=%x\012\000"
.LC110:
	.ascii	"flashmode = %x pagenum = %x %x\012\000"
.LC111:
	.ascii	"id = %x,%x addr= %x,spare= %x %x %x %x data=%x %x\012"
	.ascii	"\000"
.LC112:
	.ascii	"blk = %x vpc=%x mode = %x\012\000"
.LC113:
	.ascii	"mlc id = %x,%x addr= %x,spare= %x %x %x %x data=%x "
	.ascii	"%x\012\000"
.LC114:
	.ascii	"slc id = %x,%x addr= %x,spare= %x %x %x %x data=%x "
	.ascii	"%x\012\000"
.LC115:
	.ascii	"ftl_scan_all_ppa blk %x page %x\012\000"
.LC116:
	.ascii	"addr= %x, status= %d,spare= %x %x %x %x data=%x %x\012"
	.ascii	"\000"
.LC117:
	.ascii	"%s finished\012\000"
.LC118:
	.ascii	"FlashMakeFactorBbt %d\012\000"
.LC119:
	.ascii	"bad block:%d %d\012\000"
.LC120:
	.ascii	"FMFB:%d %d\012\000"
.LC121:
	.ascii	"E:bad block:%d\012\000"
.LC122:
	.ascii	"FMFB:Save %d %d\012\000"
.LC123:
	.ascii	"FtlBbmTblFlush id=%x,page=%x,previd=%x cnt=%d\012\000"
.LC124:
	.ascii	"FtlBbmTblFlush error:%x\012\000"
.LC125:
	.ascii	"FtlBbmTblFlush error = %x error count = %d\012\000"
.LC126:
	.ascii	"FtlGcFreeBadSuperBlk 0x%x\012\000"
.LC127:
	.ascii	"decrement_vpc_count %x = %d\012\000"
.LC128:
	.ascii	"decrement_vpc_count %x = %d in free list\012\000"
.LC129:
	.ascii	"FtlVpcTblFlush error = %x error count = %d\012\000"
.LC130:
	.ascii	"page map lost: %x %x\012\000"
.LC131:
	.ascii	"FtlMapWritePage error = %x\012\000"
.LC132:
	.ascii	"FtlMapWritePage error = %x error count = %d\012\000"
.LC133:
	.ascii	"FtlVendorPartRead refresh = %x phyAddr = %x\012\000"
.LC134:
	.ascii	"no ect\000"
.LC135:
	.ascii	"slc mode\000"
.LC136:
	.ascii	"BBT:\000"
.LC137:
	.ascii	"region_id = %x phyAddr = %x\012\000"
.LC138:
	.ascii	"map_ppn:\000"
.LC139:
	.ascii	"load_l2p_region refresh = %x phyAddr = %x\012\000"
.LC140:
	.ascii	"FtlCheckVpc2 %x = %x  %x\012\000"
.LC141:
	.ascii	"free blk vpc error %x = %x  %x\012\000"
.LC142:
	.ascii	"Ftlscanalldata = %x\012\000"
.LC143:
	.ascii	"scan lpa = %x ppa= %x\012\000"
.LC144:
	.ascii	"lba = %x,addr= %x,spare= %x %x %x %x data=%x %x\012"
	.ascii	"\000"
.LC145:
	.ascii	"RSB refresh addr %x\012\000"
.LC146:
	.ascii	"spuer block %x vpn is 0\012 \000"
.LC147:
	.ascii	"g_recovery_ppa %x ver %x\012 \000"
.LC148:
	.ascii	"FtlCheckVpc %x = %x  %x\012\000"
.LC149:
	.ascii	"FtlGcScanTempBlk Error ID %x %x!!!!!!! \012\000"
.LC150:
	.ascii	"FtlGcScanTempBlkError ID %x %x!!!!!!!\012\000"
.LC151:
	.ascii	"GC des block %x done\012\000"
.LC152:
	.ascii	"%d GC datablk  = %x vpc %x %x\012\000"
.LC153:
	.ascii	"SWL %x, FSB = %x vpc= %x,ec=%x th=%x\012\000"
.LC154:
	.ascii	"Ftlwrite decrement_vpc_count %x = %d\012\000"
.LC155:
	.ascii	"rk_ftl_de_init %x\012\000"
.LC156:
	.ascii	"...%s: no bad block mapping table, format device\012"
	.ascii	"\000"
.LC157:
	.ascii	"...%s FtlSysBlkInit error ,format device!\012\000"
.LC158:
	.ascii	"FtlInit %x\012\000"
.LC159:
	.ascii	"fix power lost blk = %x vpc=%x\012\000"
.LC160:
	.ascii	"erase power lost blk = %x vpc=%x\012\000"
.LC161:
	.ascii	"FtlWrite: lpa error:%x %x\012\000"
.LC162:
	.ascii	"id = %x,%x addr= %x,spare= %x %x %x %x data = %x\012"
	.ascii	"\000"
.LC163:
	.ascii	":\000"
.LC164:
	.ascii	"phyBlk = %x,addr= %x,spare= %x %x %x %x data=%x %x\012"
	.ascii	"\000"
.LC165:
	.ascii	"Mblk:\000"
.LC166:
	.ascii	"L2P:\000"
.LC167:
	.ascii	"L2PC:\000"
.LC168:
	.ascii	"IdBlockReadData %x %x\012\000"
.LC169:
	.ascii	"IdBlockReadData %x %x ret= %x\012\000"
.LC170:
	.ascii	"write_idblock fix data %x %x\012\000"
.LC171:
	.ascii	"idblk:\000"
.LC172:
	.ascii	"idb reverse %x %x\012\000"
.LC173:
	.ascii	"write_idblock totle_sec %x %x %x %x\012\000"
.LC174:
	.ascii	"IDBlockWriteData %x %x\012\000"
.LC175:
	.ascii	"IDBlockWriteData %x %x ret= %x\012\000"
.LC176:
	.ascii	"write and check error:%d idb=%x,offset=%x,r=%x,w=%x"
	.ascii	"\012\000"
.LC177:
	.ascii	"write\000"
.LC178:
	.ascii	"read\000"
.LC179:
	.ascii	"write_idblock error %d\012\000"
.LC180:
	.ascii	"wl_lba %p %x %x %x\012\000"
.LC181:
	.ascii	"READ_SECTOR_IO\012\000"
.LC182:
	.ascii	"rk_copy_from_user error\012\000"
.LC183:
	.ascii	"READ_SECTOR_IO %x %x\012\000"
.LC184:
	.ascii	"rk_copy_to_user error\012\000"
.LC185:
	.ascii	"WRITE_SECTOR_IO\012\000"
.LC186:
	.ascii	"WRITE_SECTOR_IO %x %x\012\000"
.LC187:
	.ascii	"END_WRITE_SECTOR_IO\012\000"
.LC188:
	.ascii	"END_WRITE_SECTOR_IO %x %x\012\000"
.LC189:
	.ascii	"GET_FLASH_INFO_IO\012\000"
.LC190:
	.ascii	"GET_BAD_BLOCK_IO\012\000"
.LC191:
	.ascii	"GET_LOCK_FLAG_IO\012\000"
.LC192:
	.ascii	"GET_PUBLIC_KEY_IO\012\000"
.LC193:
	.ascii	"RKNAND_GET_DRM_KEY\012\000"
.LC194:
	.ascii	"RKNAND_STORE_DRM_KEY\012\000"
.LC195:
	.ascii	"RKNAND_DIASBLE_SECURE_BOOT\012\000"
.LC196:
	.ascii	"RKNAND_ENASBLE_SECURE_BOOT\012\000"
.LC197:
	.ascii	"RKNAND_GET_SN_SECTOR\012\000"
.LC198:
	.ascii	"RKNAND_LOADER_UNLOCK\012\000"
.LC199:
	.ascii	"RKNAND_LOADER_STATUS\012\000"
.LC200:
	.ascii	"RKNAND_LOADER_LOCK\012\000"
.LC201:
	.ascii	"LockKey not match %d\012\000"
.LC202:
	.ascii	"RKNAND_GET_VENDOR_SECTOR\012\000"
.LC203:
	.ascii	"RKNAND_STORE_VENDOR_SECTOR\012\000"
.LC204:
	.ascii	"return ret = %lx\012\000"
.LC205:
	.ascii	"secureBootEn check error\012\000"
.LC206:
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
	.type	gpNandParaInfo, %object
	.size	gpNandParaInfo, 4
gpNandParaInfo:
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
	.type	IDByte, %object
	.size	IDByte, 32
IDByte:
	.space	32
	.type	gDieOp, %object
	.size	gDieOp, 128
gDieOp:
	.space	128
	.type	gFlashInterfaceMode, %object
	.size	gFlashInterfaceMode, 1
gFlashInterfaceMode:
	.space	1
	.type	gNandMaxDie, %object
	.size	gNandMaxDie, 1
gNandMaxDie:
	.space	1
	.space	2
	.type	DieCsIndex, %object
	.size	DieCsIndex, 8
DieCsIndex:
	.space	8
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
	.type	gMultiPageProgEn, %object
	.size	gMultiPageProgEn, 1
gMultiPageProgEn:
	.space	1
	.space	2
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
