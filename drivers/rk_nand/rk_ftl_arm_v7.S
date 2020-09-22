/*
 * Copyright (c) 2016-2018, Fuzhou Rockchip Electronics Co., Ltd
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 * date: 2020-09-23
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
	ldr	r3, .L89
	ldr	r2, [r3, #80]
	ldr	r1, [r2]
	str	r1, [r3, #84]
	ldr	r1, [r2, #4]
	str	r1, [r3, #88]
	ldr	r1, [r2, #8]
	str	r1, [r3, #92]
	ldr	r1, [r2, #12]
	str	r1, [r3, #96]
	ldr	r1, [r2, #304]
	str	r1, [r3, #100]
	ldr	r1, [r2, #308]
	str	r1, [r3, #104]
	ldr	r1, [r2, #336]
	ldr	r2, [r2, #344]
	str	r1, [r3, #108]
	str	r2, [r3, #112]
	bx	lr
.L90:
	.align	2
.L89:
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
	ldr	r0, .L97
	mov	r8, r3
	ldr	ip, [r5, #4]
	mov	r9, r1
	mov	r7, r2
	ldrh	r3, [r0, #130]
	bic	ip, ip, #-2147483648
	ldrh	r4, [r0, #128]
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
	bne	.L93
	ldrb	r3, [fp, #144]	@ zero_extendqisi2
	cmp	r3, #0
	ldreq	r3, .L97
	addeq	r4, r3, r4, asl #1
	ldreqh	r4, [r4, #148]
.L93:
	add	ip, fp, r0, asl #2
	ldrb	r3, [sp, #48]	@ zero_extendqisi2
	ldr	r1, [ip, #1172]
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
.L98:
	.align	2
.L97:
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
	ldr	r3, .L100
	ldr	r2, [r3, #1204]
	str	r0, [r3, #1204]
	mov	r0, r2
	bx	lr
.L101:
	.align	2
.L100:
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
	ldr	r3, .L103
	ldr	r3, [r3, #44]
	ldrb	r0, [r3, #9]	@ zero_extendqisi2
	bx	lr
.L104:
	.align	2
.L103:
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
	ldr	r3, .L107
	add	r0, r3, r0, asl #3
	ldrb	r2, [r0, #16]	@ zero_extendqisi2
	ldr	r0, [r0, #12]
	add	r2, r2, #8
	add	r0, r0, r2, asl #8
	bx	lr
.L108:
	.align	2
.L107:
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
	ldr	r3, .L110
	ldr	r2, [r3, #80]
	mov	r3, r0, asl #8
	orr	r0, r3, r0, asl #16
	orr	r3, r0, #1
	str	r3, [r2, #304]
	bx	lr
.L111:
	.align	2
.L110:
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
	ldr	r3, .L113
	orr	r0, r0, #16640
	ldr	r3, [r3, #80]
	str	r0, [r3, #344]
	bx	lr
.L114:
	.align	2
.L113:
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
	ldr	r3, .L118
	cmp	r0, #0
	ldr	r2, [r3, #80]
	ldr	r3, [r2]
	bfieq	r3, r0, #13, #1
	orrne	r3, r3, #253952
	str	r3, [r2]
	bx	lr
.L119:
	.align	2
.L118:
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
	ldr	r3, .L127
	ands	r1, r0, #6
	ldr	r2, [r3, #80]
	ldr	r3, [r2]
	bfieq	r3, r1, #13, #1
	beq	.L123
	orr	r3, r3, #24576
	movw	r1, #8322
	bfc	r3, #15, #1
	str	r1, [r2, #344]
	orr	r3, r3, #196608
	ldr	r1, .L127+4
	tst	r0, #4
	orrne	r3, r3, #32768
	str	r1, [r2, #304]
	mov	r1, #38
	str	r1, [r2, #308]
	mov	r1, #39
	str	r1, [r2, #308]
.L123:
	str	r3, [r2]
	mov	r0, #0
	bx	lr
.L128:
	.align	2
.L127:
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
	ldr	r2, .L130
	add	r0, r2, r0, asl #3
	mov	r2, #1
	ldr	r1, [r0, #12]
	ldrb	r0, [r0, #16]	@ zero_extendqisi2
	ldr	r3, [r1]
	mov	r2, r2, asl r0
	bfi	r3, r2, #0, #8
	str	r3, [r1]
	bx	lr
.L131:
	.align	2
.L130:
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
	ldr	r3, .L133
	add	r0, r3, r0, asl #3
	ldr	r2, [r0, #12]
	ldr	r3, [r2]
	bfc	r3, #0, #8
	bfc	r3, #17, #1
	str	r3, [r2]
	bx	lr
.L134:
	.align	2
.L133:
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
	ldr	r3, .L137
	add	r0, r0, #3
	umull	r0, r1, r0, r3
	ldr	r3, .L137+4
	ldr	r3, [r3, #8]
	mov	r0, r1, lsr #6
	blx	r3
	mov	r0, #0
	ldmfd	sp!, {r3, pc}
.L138:
	.align	2
.L137:
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
	ldr	r3, .L141
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
.L142:
	.align	2
.L141:
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
	ldr	r9, .L153
	mov	r5, r0
	ldr	r7, .L153+4
	mov	r6, r1
	add	r10, r9, #256
	mov	r4, #0
.L144:
	ldrb	r3, [r7, #1209]	@ zero_extendqisi2
	cmp	r4, r3
	bcs	.L152
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
	beq	.L151
	cmp	r3, #35
	addne	r3, r9, r6
	addne	r3, r3, #400
	ldrnesb	r3, [r3]
	bne	.L150
	ldr	r3, .L153+8
	add	r2, r4, r8
	add	r3, r3, r2
.L151:
	ldrsb	r3, [r3, #5]
.L150:
	str	r3, [r5]
	add	r4, r4, #1
	b	.L144
.L152:
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, pc}
.L154:
	.align	2
.L153:
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
	ldr	r3, .L160
	stmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, lr}
	.save {r4, r5, r6, r7, r8, r9, r10, lr}
	add	r1, r3, r1, asl #2
	ldr	r8, .L160+4
	mov	r4, #0
	add	r5, r1, #3
	mov	r6, r0
	mov	r7, r3
	mov	r9, #161
	mov	r10, r4
.L156:
	ldrb	r3, [r8, #1209]	@ zero_extendqisi2
	cmp	r4, r3
	bcs	.L159
	str	r9, [r6, #8]
	mov	r0, #300
	str	r10, [r6]
	ldrsb	r3, [r7, r4]
	add	r4, r4, #1
	str	r3, [r6]
	ldrsb	r3, [r5, #1]!
	str	r3, [r6]
	bl	NandcDelayns
	b	.L156
.L159:
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, pc}
.L161:
	.align	2
.L160:
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
	ldr	r5, .L171
	mov	r9, r2
	ldr	r4, .L171+4
	mov	r7, r0
	mov	r8, r1
	ldr	r3, [r5, #44]
	ldrb	r2, [r3, #19]	@ zero_extendqisi2
	mov	r3, r0, asl #3
	cmp	r2, #6
	addeq	r4, r4, r0, asl #6
	addeq	r4, r4, #20
	addeq	r4, r4, r6, asl #2
	beq	.L164
	cmp	r2, #7
	bne	.L165
	mov	r2, #160
	mla	r4, r2, r0, r4
	add	r2, r6, r6, asl #2
	add	r4, r4, #28
	add	r4, r4, r2, asl #1
	b	.L164
.L165:
	cmp	r2, #8
	addne	r2, r6, r3
	addeq	r2, r6, r6, asl #2
	ldreq	r4, .L171+8
	addne	r4, r4, r2, asl #3
	addeq	r4, r4, r2
	addne	r4, r4, #20
.L164:
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
.L167:
	cmp	ip, r9
	beq	.L170
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
	b	.L167
.L170:
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
.L172:
	.align	2
.L171:
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
	ldr	r3, .L181
	ldr	r2, [r3, #44]
	ldrb	r2, [r2, #19]	@ zero_extendqisi2
	sub	r2, r2, #1
	cmp	r2, #7
	bxhi	lr
	stmfd	sp!, {r4, r5, r6, lr}
	.save {r4, r5, r6, lr}
	mov	r4, #0
	ldr	r6, .L181+4
	mov	r5, r3
.L174:
	ldrb	r3, [r6, r4, asl #3]	@ zero_extendqisi2
	uxtb	r0, r4
	cmp	r3, #173
	bne	.L175
	ldrb	r1, [r5, #1211]	@ zero_extendqisi2
	mov	r3, #0
	ldr	r2, .L181+8
	bl	HynixSetRRPara
.L175:
	add	r4, r4, #1
	cmp	r4, #4
	bne	.L174
	ldmfd	sp!, {r4, r5, r6, pc}
.L182:
	.align	2
.L181:
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
	ldr	ip, .L194
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
	bne	.L188
	cmp	r2, #0
	add	r3, r4, r3
	ldrneb	r2, [ip, #62]	@ zero_extendqisi2
	ldreqb	r2, [ip, #61]	@ zero_extendqisi2
	str	r2, [r3, #2056]
	ldrb	r0, [ip, #63]	@ zero_extendqisi2
	cmp	r0, #0
	addne	ip, r4, r5, asl #8
	movne	r2, #0
	beq	.L188
.L187:
	cmp	r2, r0
	bcs	.L188
	mov	r3, r2, asl #3
	add	r2, r2, #1
	mov	r3, r1, lsr r3
	uxtb	r3, r3
	str	r3, [ip, #4]
	b	.L187
.L188:
	mov	r0, #80
	bl	NandcDelayns
	ldr	r0, [r4, r5, asl #8]
	uxtb	r0, r0
	ldmfd	sp!, {r3, r4, r5, pc}
.L195:
	.align	2
.L194:
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
.L197:
	mov	r0, r4
	mov	r1, r5
	mov	r2, r6
	bl	FlashReadStatusEN
	cmp	r0, #255
	beq	.L197
	tst	r0, #64
	ldmnefd	sp!, {r4, r5, r6, pc}
	mov	r0, #1
	mov	r1, #3
	bl	usleep_range
	b	.L197
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
	ldr	r5, .L211
	add	r4, r5, r0, asl #4
	ldr	r3, [r4, #2108]
	ldrb	r7, [r4, #2100]	@ zero_extendqisi2
	cmp	r3, #0
	beq	.L205
	mov	r6, r0
	mov	r0, r7
	add	r5, r5, r6, asl #2
	bl	NandcFlashCs
	ldr	r1, [r4, #2104]
	mov	r0, r7
	ldr	r2, [r5, #1172]
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
.L205:
	mov	r0, #0
	ldmfd	sp!, {r3, r4, r5, r6, r7, pc}
.L212:
	.align	2
.L211:
	.word	.LANCHOR0
	.fnend
	.size	FlashWaitCmdDone, .-FlashWaitCmdDone
	.align	2
	.type	flash_read_ecc, %function
flash_read_ecc:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	ldr	r2, .L215
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
.L216:
	.align	2
.L215:
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
	ldr	r3, .L223
	stmfd	sp!, {r0, r1, r2, r4, r5, lr}
	.save {r4, r5, lr}
	.pad #12
	add	r0, r3, r0, asl #3
	ldr	r4, .L223+4
	ldr	r5, [r0, #12]
.L219:
	ldr	r3, [r5]
	str	r3, [sp, #4]
	ldr	r3, [sp, #4]
	tst	r3, #512
	bne	.L220
	mov	r0, #10
	bl	NandcDelayns
	subs	r4, r4, #1
	bne	.L219
	mvn	r0, #0
	b	.L218
.L220:
	mov	r0, #0
.L218:
	add	sp, sp, #12
	@ sp needed
	ldmfd	sp!, {r4, r5, pc}
.L224:
	.align	2
.L223:
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
	ldr	r3, .L231
	stmfd	sp!, {r0, r1, r2, r4, r5, lr}
	.save {r4, r5, lr}
	.pad #12
	add	r0, r3, r0, asl #3
	ldr	r4, .L231+4
	ldr	r5, [r0, #12]
	mov	r0, #130
	bl	NandcDelayns
.L227:
	ldr	r3, [r5]
	str	r3, [sp, #4]
	ldr	r3, [sp, #4]
	tst	r3, #512
	bne	.L228
	mov	r0, #1
	mov	r1, #2
	bl	usleep_range
	subs	r4, r4, #1
	bne	.L227
	mvn	r0, #0
	b	.L226
.L228:
	mov	r0, #0
.L226:
	add	sp, sp, #12
	@ sp needed
	ldmfd	sp!, {r4, r5, pc}
.L232:
	.align	2
.L231:
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
	ldr	r3, .L235
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
.L236:
	.align	2
.L235:
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
	ldr	r5, .L244
	ldrb	r3, [r5, #144]	@ zero_extendqisi2
	cmp	r3, #0
	ldmeqfd	sp!, {r4, r5, r6, r7, r8, pc}
	mov	r6, r0
	bl	NandcFlashCs
	add	r4, r5, r6, asl #3
	ldrb	r3, [r4, #2068]	@ zero_extendqisi2
	ldrb	r8, [r4, #16]	@ zero_extendqisi2
	cmp	r3, #44
	ldr	r7, [r4, #12]
	mov	r8, r8, asl #8
	bne	.L239
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
.L239:
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
.L245:
	.align	2
.L244:
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
	ldr	r5, .L253
	ldrb	r3, [r5, #144]	@ zero_extendqisi2
	cmp	r3, #0
	ldmeqfd	sp!, {r4, r5, r6, r7, r8, pc}
	mov	r6, r0
	bl	NandcFlashCs
	add	r4, r5, r6, asl #3
	ldrb	r3, [r4, #2068]	@ zero_extendqisi2
	ldrb	r8, [r4, #16]	@ zero_extendqisi2
	cmp	r3, #44
	ldr	r7, [r4, #12]
	mov	r8, r8, asl #8
	bne	.L248
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
.L248:
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
.L254:
	.align	2
.L253:
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
	ldr	r1, .L280
	stmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.save {r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.pad #20
	sub	sp, sp, #20
	ldrb	fp, [r1, #2229]	@ zero_extendqisi2
	mov	lr, #0
	ldr	r7, .L280+4
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
.L267:
	ldr	r3, [sp, #12]
	ldrb	ip, [lr, r7]	@ zero_extendqisi2
	ldr	r1, [r3, lr]!
	cmp	ip, #69
	cmpne	ip, #152
	ldrb	r3, [r3, #4]	@ zero_extendqisi2
	beq	.L258
	cmp	ip, #44
	cmpne	ip, #173
	bne	.L259
.L258:
	cmp	r0, #1
	bne	.L260
	ldr	fp, [sp, #8]
	cmp	fp, #0
	beq	.L259
	mov	r3, r3, asl #8
	cmp	ip, #173
	add	fp, r1, r3
	str	r4, [fp, #2056]
	streq	r0, [fp, #2052]
	beq	.L279
	cmp	ip, #44
	streq	r0, [fp, #2052]
	strne	r5, [fp, #2052]
	streq	r10, [fp, #2048]
	strne	r0, [fp, #2048]
	b	.L265
.L260:
	ldr	fp, [sp, #4]
	cmp	fp, #0
	beq	.L259
	mov	r3, r3, asl #8
	cmp	ip, #173
	add	fp, r1, r3
	str	r4, [fp, #2056]
	streq	r6, [fp, #2052]
	streq	r9, [fp, #2048]
	beq	.L265
	cmp	ip, #44
	streq	r6, [fp, #2052]
	streq	r8, [fp, #2048]
	beq	.L265
	str	r5, [fp, #2052]
.L279:
	str	r2, [fp, #2048]
.L265:
	add	r3, r1, r3
	str	r2, [r3, #2048]
	str	r2, [r3, #2048]
	str	r2, [r3, #2048]
.L259:
	add	lr, lr, #8
	cmp	lr, #32
	bne	.L267
	mov	r0, #0
	bl	NandcWaitFlashReady
	mov	r0, #0
	add	sp, sp, #20
	@ sp needed
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L281:
	.align	2
.L280:
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
	ldr	ip, .L284
	stmfd	sp!, {r3, r4, r5, lr}
	.save {r3, r4, r5, lr}
	add	ip, ip, r0, asl #3
	ldr	r3, .L284+4
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
.L285:
	.align	2
.L284:
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
	ldr	r2, .L288
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
.L289:
	.align	2
.L288:
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
	ldr	r0, .L298
	ldr	r1, .L298+4
	add	r4, r4, r4, asl #2
	sub	ip, r0, #48
	mov	r2, #0
.L291:
	ldrb	r3, [r1, #1209]	@ zero_extendqisi2
	cmp	r2, r3
	bcs	.L297
	ldrb	r3, [r1, #1208]	@ zero_extendqisi2
	cmp	r3, #67
	add	r3, r2, r4
	addeq	r3, ip, r3
	addne	r3, r0, r3
	add	r2, r2, #1
	ldrsb	r3, [r3, #5]
	str	r3, [r5]
	b	.L291
.L297:
	mov	r0, #0
	ldmfd	sp!, {r3, r4, r5, lr}
	b	NandcWaitFlashReady
.L299:
	.align	2
.L298:
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
	ldr	r3, .L302
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
.L303:
	.align	2
.L302:
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
	ldr	r8, .L316
	.pad #20
	sub	sp, sp, #20
	mov	r6, r0
	mov	r9, r1
	mov	r7, r5
.L305:
	cmp	r7, r9
	beq	.L315
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
	bcs	.L307
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
	bne	.L309
	ldr	r0, .L316+4
	ldr	r1, [sp, #8]
	bl	printk
.L309:
	mov	r0, r4
	bl	NandcFlashDeCs
.L307:
	add	r7, r7, #1
	add	r5, r5, #36
	b	.L305
.L315:
	mov	r0, #0
	add	sp, sp, #20
	@ sp needed
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, pc}
.L317:
	.align	2
.L316:
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
	ldr	r4, .L354
	.pad #28
	sub	sp, sp, #28
	ldrb	r5, [r4]	@ zero_extendqisi2
	cmp	r5, #0
	moveq	r9, r0
	moveq	r10, r1
	moveq	fp, r4
	beq	.L319
	mov	r1, r2
	bl	FlashEraseSLc2KBlocks
	b	.L320
.L328:
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
	bcc	.L351
.L322:
	add	r5, r5, #1
.L319:
	cmp	r5, r8
	bcc	.L328
	b	.L352
.L351:
	ldrb	r3, [fp, #2240]	@ zero_extendqisi2
	cmp	r3, #0
	add	r3, r4, r0, asl #4
	ldr	r3, [r3, #2108]
	moveq	r7, #0
	cmp	r3, #0
	beq	.L324
	uxtb	r0, r0
	bl	FlashWaitCmdDone
.L324:
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
	bne	.L326
	ldrb	r3, [fp, #144]	@ zero_extendqisi2
	cmp	r3, #0
	beq	.L326
	bl	flash_enter_slc_mode
	b	.L327
.L326:
	bl	flash_exit_slc_mode
.L327:
	ldr	r3, [sp, #20]
	mov	r0, r6
	ldr	r1, [sp, #16]
	add	r5, r5, r7
	add	r3, r4, r3, asl #2
	ldr	r2, [r3, #1172]
	adds	r2, r2, #0
	movne	r2, #1
	bl	FlashWaitReadyEN
	mov	r0, r6
	mov	r2, r7
	ldr	r1, [sp, #16]
	bl	FlashEraseCmd
	mov	r0, r6
	bl	NandcFlashDeCs
	b	.L322
.L352:
	ldr	r6, .L354
	mov	r5, #0
	ldr	r7, .L354+4
.L329:
	ldrb	r2, [r4, #2230]	@ zero_extendqisi2
	ldr	r3, .L354
	cmp	r5, r2
	bcs	.L353
	uxtb	r0, r5
	bl	FlashWaitCmdDone
	cmp	r10, #1
	bne	.L330
	ldrb	r3, [r6, #144]	@ zero_extendqisi2
	cmp	r3, #0
	beq	.L330
	ldrb	r0, [r7, r5, asl #4]	@ zero_extendqisi2
	bl	flash_exit_slc_mode
.L330:
	add	r5, r5, #1
	b	.L329
.L353:
	ldr	r2, [r3, #2244]
	cmp	r2, #0
	bne	.L332
.L334:
	mov	r0, #0
	b	.L320
.L332:
	ldrb	r3, [r3, #2068]	@ zero_extendqisi2
	cmp	r3, #69
	bne	.L334
	mov	r3, #0
	mov	r2, #36
	mov	r1, r3
.L333:
	cmp	r3, r8
	beq	.L334
	mul	r0, r2, r3
	add	r3, r3, #1
	str	r1, [r9, r0]
	b	.L333
.L320:
	add	sp, sp, #28
	@ sp needed
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L355:
	.align	2
.L354:
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
	ldr	r0, .L362
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
	bne	.L357
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
	b	.L359
.L357:
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
.L359:
	mov	r0, r8
	mov	r1, r7
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, lr}
	b	FlashSetRandomizer
.L363:
	.align	2
.L362:
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
	ldr	r4, .L375
	bl	NandcWaitFlashReady
	bl	FlashSetReadRetryDefault
	ldr	r0, [r4, #2248]
	cmp	r0, #0
	beq	.L365
	mov	r0, #0
	bl	flash_enter_slc_mode
	b	.L366
.L365:
	bl	flash_exit_slc_mode
.L366:
	ldrb	r3, [r4, #2252]	@ zero_extendqisi2
	ldr	r5, .L375
	cmp	r3, #0
	beq	.L367
	ldrb	r3, [r5, #2229]	@ zero_extendqisi2
	tst	r3, #1
	beq	.L367
	mov	r0, #1
	bl	FlashSetInterfaceMode
	mov	r0, #1
	bl	NandcSetMode
	mov	r3, #0
	strb	r3, [r5, #2252]
.L367:
	ldr	r3, [r4, #12]
	mov	r0, #0
	str	r0, [r3, #336]
	ldmfd	sp!, {r3, r4, r5, pc}
.L376:
	.align	2
.L375:
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
	ldr	r3, .L378
	add	r0, r3, r0, asl #3
	ldr	r3, [r0, #12]
	str	r1, [r3, #336]
	bx	lr
.L379:
	.align	2
.L378:
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
	ldr	r1, .L391
	bl	__aeabi_idiv
	ldr	r3, .L391+4
	ldr	r3, [r3, #80]
	cmp	r0, #250
	movwgt	r2, #8354
	bgt	.L389
	cmp	r0, #220
	bgt	.L390
	cmp	r0, #185
	movwgt	r2, #4226
	bgt	.L389
	cmp	r0, #160
	movwgt	r2, #4194
	bgt	.L389
	cmp	r4, #35
	movwls	r2, #4193
	bls	.L389
	cmp	r4, #99
	movwls	r2, #4225
	bls	.L389
.L390:
	movw	r2, #8322
.L389:
	str	r2, [r3, #4]
	ldmfd	sp!, {r4, pc}
.L392:
	.align	2
.L391:
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
	bls	.L394
	movw	r3, #8322
	cmp	r0, r3
	bne	.L395
.L394:
	ldr	r3, .L396
	ldr	r3, [r3, #80]
	str	r0, [r3, #4]
.L395:
	ldr	r3, .L396+4
	ldrb	r0, [r3, #493]	@ zero_extendqisi2
	b	NandcTimeCfg
.L397:
	.align	2
.L396:
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
	ldr	r3, .L401
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
	str	r0, [r3, #80]
	str	r2, [r3, #40]
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
	ldr	r3, [r4, #80]
	str	r5, [r3, #336]
	bl	NandcTimeCfg
	ldr	r3, [r4, #80]
	movw	r2, #8322
	mov	r0, #36864
	str	r2, [r3, #344]
	ldr	r2, .L401+4
	str	r2, [r3, #304]
	bl	ftl_malloc
	str	r5, [r4, #2296]
	str	r5, [r4, #2304]
	str	r0, [r4, #2268]
	str	r0, [r4, #2272]
	add	r0, r0, #32768
	str	r0, [r4, #2276]
	ldmfd	sp!, {r3, r4, r5, pc}
.L402:
	.align	2
.L401:
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
	ldr	ip, .L405
	str	lr, [sp, #-4]!
	.save {lr}
	ldr	lr, [ip, #80]
	ldr	lr, [lr, #4]
	str	lr, [r0]
	ldr	r0, [ip, #80]
	ldr	r0, [r0]
	str	r0, [r1]
	ldr	r1, [ip, #80]
	ldr	r1, [r1, #304]
	str	r1, [r2]
	ldr	r1, [ip, #80]
	ldr	r2, [r1, #308]
	ldr	r1, [r1, #344]
	uxtb	r2, r2
	orr	r2, r2, r1, asl #16
	str	r2, [r3]
	ldr	pc, [sp], #4
.L406:
	.align	2
.L405:
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
	ldr	r3, .L415
	mov	r1, #1
	ldr	r2, [r3, #80]
	str	r0, [r3, #2308]
	mov	r3, #0
	str	r1, [r2, #8]
	mov	r1, #16
	cmp	r0, r1
	bfi	r3, r1, #8, #8
	bfc	r3, #18, #1
	bne	.L408
.L411:
	bfc	r3, #4, #1
	b	.L409
.L408:
	cmp	r0, #24
	orreq	r3, r3, #16
	beq	.L409
	cmp	r0, #40
	orr	r3, r3, #262144
	orr	r3, r3, #16
	beq	.L411
.L409:
	orr	r3, r3, #1
	str	r3, [r2, #12]
	bx	lr
.L416:
	.align	2
.L415:
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
	ldr	r3, .L418
	strb	r0, [r3, #2312]
	b	NandcBchSel
.L419:
	.align	2
.L418:
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
	ldr	r3, .L429
	stmfd	sp!, {r4, r5, r6, lr}
	.save {r4, r5, r6, lr}
	mov	r5, #0
	ldr	r2, [r3, #80]
	mov	r4, r3
	ldr	r1, [r3, #84]
	ldr	r6, .L429+4
	str	r1, [r2]
	ldr	r1, [r3, #88]
	ldr	r2, [r3, #80]
	str	r1, [r2, #4]
	ldr	r1, [r3, #92]
	str	r1, [r2, #8]
	ldr	r1, [r3, #96]
	str	r1, [r2, #12]
	ldr	r1, [r3, #100]
	str	r1, [r2, #304]
	ldr	r1, [r3, #104]
	str	r1, [r2, #308]
	ldr	r1, [r3, #108]
	str	r1, [r2, #336]
	ldr	r1, [r3, #112]
	str	r1, [r2, #344]
.L422:
	ldrb	r3, [r6, r5, asl #3]	@ zero_extendqisi2
	sub	r3, r3, #1
	uxtb	r3, r3
	cmp	r3, #253
	bhi	.L421
	uxtb	r0, r5
	bl	FlashReset
.L421:
	add	r5, r5, #1
	cmp	r5, #4
	bne	.L422
	ldrb	r3, [r4, #2252]	@ zero_extendqisi2
	ldr	r5, .L429
	cmp	r3, #0
	beq	.L423
	mov	r0, #1
	bl	NandcSetMode
	ldrb	r0, [r5, #2229]	@ zero_extendqisi2
	bl	FlashSetInterfaceMode
	ldrb	r0, [r5, #2229]	@ zero_extendqisi2
	bl	NandcSetMode
	ldrb	r0, [r5, #101]	@ zero_extendqisi2
	bl	NandcSetDdrPara
.L423:
	ldr	r3, [r4, #44]
	ldmfd	sp!, {r4, r5, r6, lr}
	ldrb	r0, [r3, #20]	@ zero_extendqisi2
	b	FlashBchSel
.L430:
	.align	2
.L429:
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
	ldr	r3, .L437
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
.L438:
	.align	2
.L437:
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
.L440:
	ldr	r3, [r0, #8]
	str	r3, [sp, #4]
	ldr	r3, [sp, #4]
	tst	r3, #1048576
	beq	.L440
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
	ldr	r5, .L462
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
	ldr	r3, [r5, #2260]
	cmp	r3, #3
	bls	.L445
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
	beq	.L446
	cmp	r1, #0
	bne	.L447
.L455:
	add	r2, r2, #1
	cmp	r8, #0
	mov	r2, r2, asr #1
	movne	r0, r8
	bfi	r4, r2, #22, #6
	ldreq	r0, [r5, #2272]
	b	.L449
.L447:
	ldr	r3, [r5, #2308]
	mov	r9, r5
	cmp	r3, #25
	movcc	r3, #64
	movcs	r3, #128
	str	r3, [sp, #4]
	mov	r3, r2, lsr #1
	str	r3, [sp, #12]
	mov	r3, #0
	mov	r0, r3
.L451:
	ldr	lr, [sp, #12]
	cmp	r0, lr
	bcs	.L455
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
	b	.L451
.L449:
	ldr	r3, [r5, #2276]
	ubfx	r10, r4, #22, #5
	mov	r9, r1
	str	r0, [r5, #2280]
	mov	r2, r9
	mov	r1, r10, asl #10
	str	r3, [r5, #2284]
	bl	rknand_dma_map_single
	mov	r2, r9
	mov	r1, r10, asl #7
	clz	r9, r9
	mov	r9, r9, lsr #5
	str	r0, [r5, #2288]
	ldr	r0, [r5, #2284]
	bl	rknand_dma_map_single
	mov	r3, #1
	str	r3, [r5, #2296]
	tst	r8, #3
	ldr	r3, [r5, #2288]
	str	r0, [r5, #2292]
	str	r3, [r6, #20]
	ldr	r3, [r5, #2292]
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
.L446:
	ldr	r3, [sp, #20]
	str	r3, [r6, #16]
.L445:
	str	r7, [r6, #12]
	str	r4, [r6, #8]
	orr	r4, r4, #4
	str	r4, [r6, #8]
	add	sp, sp, #28
	@ sp needed
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L463:
	.align	2
.L462:
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
.L465:
	cmp	r2, r0
	uxth	r3, r1
	add	r1, r1, #1
	movls	r2, r2, asl #1
	bls	.L465
.L467:
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
	ldr	r3, .L471
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
.L472:
	.align	2
.L471:
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
	ldr	r4, .L502
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
	ldr	r0, .L502+4
	strh	r3, [r4, r1]	@ movhi
	movw	r1, #2330
	strh	lr, [r4, r1]	@ movhi
	mov	r1, #0
.L474:
	strb	r1, [r1, r0]
	add	r1, r1, #1
	cmp	r1, #32
	bne	.L474
	ldrh	r0, [r5, #14]
	ldrh	r1, [r5, #20]
	cmp	r1, r0, lsr #8
	bcs	.L475
	uxtb	r10, r3
	ldr	r9, .L502+4
	mov	r1, r10, asl #1
	uxtb	r1, r1
	str	r1, [sp, #4]
	sub	r1, r2, #1
	mul	r1, r3, r1
	str	r1, [sp]
	mov	r1, #0
.L476:
	cmp	r1, r3
	bcs	.L478
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
.L479:
	cmp	r8, r2
	add	ip, ip, r3
	bcs	.L501
	ldr	r7, [sp, #12]
	add	fp, r0, r10
	add	r8, r8, #1
	strb	r0, [r7, ip]
	ldr	r7, [sp, #8]
	strb	fp, [r7, ip]
	ldr	r7, [sp, #4]
	add	r0, r0, r7
	uxtb	r0, r0
	b	.L479
.L501:
	add	r1, r1, #1
	b	.L476
.L478:
	movw	r1, #2342
	mov	r2, r2, asl #1
	strh	r2, [r4, r1]	@ movhi
	movw	r2, #2330
	mov	lr, lr, lsr #1
	strh	lr, [r4, r2]	@ movhi
.L475:
	cmp	r6, #1
	movw	r2, #2380
	mov	r1, #5
	strh	r1, [r4, r2]	@ movhi
	movw	r1, #2382
	mov	r0, #0
	strh	r0, [r4, r1]	@ movhi
	ldreq	r1, .L502
	ldrb	r9, [r4]	@ zero_extendqisi2
	ldr	r8, .L502+8
	streqh	r6, [r1, r2]	@ movhi
	movw	r1, #2342
	ldrh	r7, [r4, r1]
	cmp	r9, #0
	mov	r2, #4352
	strh	r2, [r8]	@ movhi
	movne	r2, #384
	strneh	r2, [r8]	@ movhi
	smulbb	r7, r7, r3
	ldr	r2, .L502
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
	ldr	r5, .L502
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
	ldr	r10, .L502
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
	ldr	r6, .L502
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
	ldrb	ip, [r4, #144]	@ zero_extendqisi2
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
	bne	.L500
	cmp	r0, #7
	bhi	.L489
	mov	r3, #8
.L500:
	str	r3, [r6, #2416]
.L489:
	ldr	r2, .L502+12
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
.L503:
	.align	2
.L502:
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
	ldr	r2, .L511
	movw	r3, #2434
	ldrh	r3, [r2, r3]
	cmp	r3, #0
	beq	.L510
	movw	r1, #2408
	ldr	r3, [r2, #2436]
	ldrh	r2, [r2, r1]
	add	r2, r3, r2, asl #1
.L506:
	cmp	r3, r2
	beq	.L510
	ldrh	r1, [r3], #2
	cmp	r1, r0
	bne	.L506
	mov	r0, #1
	bx	lr
.L510:
	mov	r0, #0
	bx	lr
.L512:
	.align	2
.L511:
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
	ldr	r2, .L520
	ldr	r3, [r2, #2440]
	cmp	r3, #0
	beq	.L516
	stmfd	sp!, {r4, r5, lr}
	.save {r4, r5, lr}
	mov	r5, #36
	ldr	r4, [r2, #2444]
	mov	r2, #0
.L515:
	mla	ip, r5, r2, r4
	ldr	lr, [ip, #16]
	cmp	lr, r0
	movcc	ip, #0
	movcs	ip, #1
	cmp	lr, r1
	movhi	ip, #0
	cmp	ip, #0
	bne	.L517
	add	r2, r2, #1
	cmp	r2, r3
	bne	.L515
	mov	r0, ip
	ldmfd	sp!, {r4, r5, pc}
.L516:
	mov	r0, r3
	bx	lr
.L517:
	mov	r0, #1
	ldmfd	sp!, {r4, r5, pc}
.L521:
	.align	2
.L520:
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
	ldr	r3, .L523
	ldr	r0, [r3, #2428]
	bx	lr
.L524:
	.align	2
.L523:
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
	ldr	r3, .L526
	ldr	r0, [r3, #2428]
	bx	lr
.L527:
	.align	2
.L526:
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
	ldr	r3, .L529
	ldr	r0, [r3, #2428]
	bx	lr
.L530:
	.align	2
.L529:
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
	ldr	r3, .L532
	ldr	r0, [r3, #2448]
	bx	lr
.L533:
	.align	2
.L532:
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
	ldr	r5, .L536
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
	ldr	r0, .L536+4
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
.L537:
	.align	2
.L536:
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
	ldr	r5, .L540
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
.L541:
	.align	2
.L540:
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
	ldr	r4, .L552
	mov	r5, #0
	ldrh	r6, [r4, r3]
	movw	r3, #2342
	ldrh	r3, [r4, r3]
	mov	r4, r5
	mul	r6, r3, r6
.L544:
	uxth	r0, r5
	cmp	r0, r6
	bge	.L551
	bl	FtlBbmIsBadBlock
	add	r5, r5, #1
	cmp	r0, #0
	addne	r4, r4, #1
	uxthne	r4, r4
	b	.L544
.L551:
	mov	r0, r4
	ldmfd	sp!, {r4, r5, r6, pc}
.L553:
	.align	2
.L552:
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
	ldr	r6, .L556
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
.L557:
	.align	2
.L556:
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
	ldr	r3, .L560
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
.L561:
	.align	2
.L560:
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
	ldr	r4, .L564
	ldrh	r1, [r4, r3]
	bl	__aeabi_uidivmod
	movw	r3, #2344
	uxth	r0, r1
	ldrh	r1, [r4, r3]
	bl	__aeabi_uidiv
	uxth	r0, r0
	ldmfd	sp!, {r4, pc}
.L565:
	.align	2
.L564:
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
	bls	.L567
	rsb	r0, r1, r0
	cmp	r0, #-2147483648
	movhi	r0, #0
	movls	r0, #1
	bx	lr
.L567:
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
	ldr	r3, .L570
	ldrh	r0, [r3, #6]
	clz	r0, r0
	mov	r0, r0, lsr #5
	bx	lr
.L571:
	.align	2
.L570:
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
	ldr	r3, .L573
	ldrh	r0, [r3, #6]
	sub	r0, r0, #1024
	clz	r0, r0
	mov	r0, r0, lsr #5
	bx	lr
.L574:
	.align	2
.L573:
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
	ldr	r4, .L585
	ldrh	r3, [r4, #6]
	cmp	r3, #1024
	ldmeqfd	sp!, {r3, r4, r5, r6, r7, pc}
	cmp	r1, #0
	beq	.L577
	ldr	r5, .L585+4
	ldr	r3, [r5, #-3616]
	cmp	r3, #0
	bne	.L577
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
.L577:
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
.L586:
	.align	2
.L585:
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
	ldr	r3, .L600
	ldrh	ip, [r3, #28]
	ldr	r3, .L600+4
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
.L589:
	uxth	lr, r0
	add	r0, r0, #1
	cmp	lr, ip
	bge	.L599
	add	lr, r3, r1, asl #1
	add	r1, r1, #1
	ubfx	r1, r1, #0, #10
	ldrh	r4, [lr, #8]
	add	lr, r3, r2, asl #1
	strh	r4, [lr, #8]	@ movhi
	mov	r4, #1
	add	r2, r2, r4
	ubfx	r2, r2, #0, #10
	b	.L589
.L599:
	cmp	r4, #0
	strneh	r1, [r3, #2]	@ movhi
	strneh	r2, [r3, #4]	@ movhi
	ldmfd	sp!, {r4, pc}
.L601:
	.align	2
.L600:
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
	ldr	r7, .L613
	add	r4, r7, #2512
	mov	r8, r4
.L603:
	ldrh	r1, [r4, #6]
	cmp	r1, #0
	beq	.L604
	ldr	r5, .L613+4
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
	bne	.L605
	mov	r0, r6
	bl	P2V_block_in_plane
	mov	r3, r6, asl #10
	mov	r9, r0
	ldr	r0, [r5, #-3612]
	str	r3, [r0, #4]
	ldrb	r3, [r7, #144]	@ zero_extendqisi2
	cmp	r3, #0
	beq	.L606
	mov	r1, r10
	mov	r2, #1
	bl	FlashEraseBlocks
.L606:
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
	b	.L605
.L604:
	ldr	r0, .L613+8
	bl	printk
.L607:
	b	.L607
.L605:
	sub	r3, r6, #1
	movw	r2, #65533
	uxth	r3, r3
	cmp	r3, r2
	bls	.L608
	mov	r1, r6
	ldrh	r2, [r8, #6]
	ldr	r0, .L613+12
	bl	printk
	b	.L603
.L608:
	mov	r0, r6
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, pc}
.L614:
	.align	2
.L613:
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
	ldr	r3, .L621
	str	lr, [sp, #-4]!
	.save {lr}
	movw	lr, #65535
	ldr	ip, [r3, #-3552]
	ldr	r2, [r0]
	ldr	r3, .L621+4
	rsb	r0, ip, r2
	mov	r0, r0, asr #1
	mul	r3, r3, r0
	mov	r0, #6
	uxth	r3, r3
.L617:
	cmp	r1, r3
	beq	.L618
	ldrh	r3, [r2]
	cmp	r3, lr
	beq	.L619
	mla	r2, r0, r3, ip
	b	.L617
.L618:
	mov	r0, #1
	ldr	pc, [sp], #4
.L619:
	mov	r0, #0
	ldr	pc, [sp], #4
.L622:
	.align	2
.L621:
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
	ldr	r5, .L640
	ldrh	r3, [r5, r3]
	cmp	r3, r0
	bls	.L625
	ldr	r2, .L640+4
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
	beq	.L639
	ldr	r8, [r2, #-3544]
	mov	r4, r0, asl #1
	ldrh	r2, [r1, #4]
	ldrh	r7, [r8, r4]
	cmp	r2, #0
	mulne	ip, r2, r7
	ldr	r7, [r10, #-3552]
	ldr	r2, .L640+8
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
.L634:
	add	r4, r4, #1
	ldr	r5, [sp]
	uxth	r4, r4
	cmp	r4, r5
	movls	r5, #0
	movhi	r5, #1
	cmp	r0, r2
	orreq	r5, r5, #1
	cmp	r5, #0
	bne	.L625
	mov	r10, r2, asl #1
	ldrh	r5, [r3, #4]
	ldrh	fp, [r8, r10]
	cmp	r5, #0
	mvneq	r5, #0
	mulne	r5, r5, fp
	cmp	r5, ip
	bne	.L630
	ldr	r5, [sp, #4]
	ldrh	r10, [r9, r10]
	ldrh	r5, [r5]
	cmp	r10, r5
	bcc	.L632
	b	.L631
.L630:
	bhi	.L631
.L632:
	ldrh	r5, [r3]
	movw	r10, #65535
	cmp	r5, r10
	streqh	r2, [r1, #2]	@ movhi
	streqh	r0, [r3]	@ movhi
	ldreq	r3, .L640+4
	streq	r1, [r3, #-3540]
	beq	.L625
.L633:
	mov	r3, #6
	mov	r2, r5
	mla	r3, r3, r5, r7
	b	.L634
.L631:
	strh	r2, [r6, lr]	@ movhi
	ldrh	r2, [r3, #2]
	strh	r2, [r1, #2]	@ movhi
	ldr	r2, .L640+4
	ldr	ip, [r2, #-3548]
	cmp	r3, ip
	bne	.L635
	strh	r0, [r3, #2]	@ movhi
.L639:
	str	r1, [r2, #-3548]
	b	.L625
.L635:
	ldrh	ip, [r3, #2]
	ldr	r1, [r2, #-3552]
	mov	r2, #6
	mul	r2, r2, ip
	strh	r0, [r1, r2]	@ movhi
	strh	r0, [r3, #2]	@ movhi
.L625:
	mov	r0, #0
	add	sp, sp, #12
	@ sp needed
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L641:
	.align	2
.L640:
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
	ldr	r2, .L644
	ldrh	r3, [r2]
	add	r3, r3, #1
	strh	r3, [r2]	@ movhi
	ldmfd	sp!, {r3, pc}
.L645:
	.align	2
.L644:
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
	beq	.L647
	ldr	r2, .L655
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
	beq	.L654
	ldr	r9, [r2, #-3608]
	mov	r2, r0, asl #1
	ldr	r8, [r5, #-3552]
	rsb	r1, r8, r3
	ldrh	r10, [r9, r2]
	ldr	r2, .L655+4
	mov	r1, r1, asr #1
	mul	r1, r2, r1
	uxth	r2, r1
.L651:
	mov	r1, r2, asl #1
	ldrh	r1, [r9, r1]
	cmp	r1, r10
	bcs	.L649
	ldrh	r1, [r3]
	cmp	r1, r4
	streqh	r2, [ip, #2]	@ movhi
	streqh	r0, [r3]	@ movhi
	beq	.L647
.L650:
	mla	r3, lr, r1, r8
	mov	r2, r1
	b	.L651
.L649:
	ldrh	r1, [r3, #2]
	strh	r1, [ip, #2]	@ movhi
	strh	r2, [r7, r6]	@ movhi
	ldr	r1, [r5, #-3532]
	ldr	r2, .L655
	cmp	r3, r1
	bne	.L652
	strh	r0, [r3, #2]	@ movhi
.L654:
	str	ip, [r2, #-3532]
	b	.L647
.L652:
	ldrh	ip, [r3, #2]
	ldr	r1, [r2, #-3552]
	mov	r2, #6
	mul	r2, r2, ip
	strh	r0, [r1, r2]	@ movhi
	strh	r0, [r3, #2]	@ movhi
.L647:
	mov	r0, #0
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, pc}
.L656:
	.align	2
.L655:
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
	ldr	r2, .L659
	ldrh	r3, [r2, #-8]
	add	r3, r3, #1
	strh	r3, [r2, #-8]	@ movhi
	ldmfd	sp!, {r3, pc}
.L660:
	.align	2
.L659:
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
	ldr	r4, .L667
	movw	r5, #65535
	mul	r1, ip, r1
	ldr	r3, [r0]
	ldr	r2, [r4, #-3552]
	add	lr, r2, r1
	cmp	lr, r3
	ldrh	r3, [r2, r1]
	bne	.L662
	cmp	r3, r5
	mlane	r3, ip, r3, r2
	moveq	r3, #0
	streq	r3, [r0]
	strne	r3, [r0]
	mvnne	r0, #0
	strneh	r0, [r3, #2]	@ movhi
	b	.L664
.L662:
	cmp	r3, r5
	ldrh	r0, [lr, #2]
	bne	.L665
	cmp	r0, r3
	mulne	r0, ip, r0
	mvnne	r3, #0
	strneh	r3, [r2, r0]	@ movhi
	b	.L664
.L665:
	mla	r3, ip, r3, r2
	strh	r0, [r3, #2]	@ movhi
	ldrh	r5, [lr, #2]
	ldrh	r0, [r2, r1]
	ldr	r3, [r4, #-3552]
	mul	ip, ip, r5
	strh	r0, [r3, ip]	@ movhi
.L664:
	mvn	r3, #0
	mov	r0, #0
	strh	r3, [r2, r1]	@ movhi
	strh	r3, [lr, #2]	@ movhi
	ldmfd	sp!, {r4, r5, pc}
.L668:
	.align	2
.L667:
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
	beq	.L675
	ldr	r2, .L678
	movw	ip, #65535
	stmfd	sp!, {r4, lr}
	.save {r4, lr}
	mov	lr, #6
	ldr	r4, [r2, #-3552]
.L671:
	cmp	r1, #0
	bne	.L672
.L674:
	rsb	r4, r4, r3
	ldr	r3, .L678+4
	mov	r4, r4, asr #1
	mul	r4, r3, r4
	uxth	r4, r4
	mov	r1, r4
	bl	List_remove_node
	mov	r0, r4
	ldmfd	sp!, {r4, pc}
.L672:
	ldrh	r2, [r3]
	cmp	r2, ip
	beq	.L674
	sub	r1, r1, #1
	mla	r3, lr, r2, r4
	uxth	r1, r1
	b	.L671
.L675:
	movw	r0, #65535
	bx	lr
.L679:
	.align	2
.L678:
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
	ldr	r2, .L687
	ldr	r3, [r2, #-3548]
	cmp	r3, #0
	ldrne	r1, [r2, #-3552]
	movne	ip, #6
	movwne	r2, #65535
	beq	.L685
.L682:
	cmp	r0, #0
	beq	.L683
	ldrh	r3, [r3]
	cmp	r3, r2
	subne	r0, r0, #1
	mlane	r3, ip, r3, r1
	uxthne	r0, r0
	bne	.L682
.L685:
	movw	r0, #65535
	bx	lr
.L683:
	rsb	r3, r1, r3
	ldr	r0, .L687+4
	mov	r3, r3, asr #1
	mul	r0, r0, r3
	uxth	r0, r0
	bx	lr
.L688:
	.align	2
.L687:
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
	ldr	r3, .L697
	sub	r2, r3, #3520
	ldrh	r2, [r2, #-4]
	cmp	r2, r0
	beq	.L690
	sub	r2, r3, #3472
	ldrh	r2, [r2, #-4]
	cmp	r2, r0
	beq	.L690
	sub	r2, r3, #3424
	ldrh	r2, [r2, #-4]
	cmp	r2, r0
	beq	.L690
	mov	lr, #6
	ldr	r1, [r3, #-3552]
	mul	lr, lr, r0
	ldr	r2, [r3, #-3548]
	add	ip, r1, lr
	cmp	ip, r2
	beq	.L690
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
	bne	.L692
	ldrh	r3, [r1, lr]
	cmp	r3, ip
	beq	.L690
.L692:
	mov	r3, #6
	mul	ip, r3, ip
	ldr	r3, .L697+4
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
	bcs	.L690
	ldr	r5, .L697+8
	mov	r4, r0
	mov	r1, r4
	sub	r0, r5, #12
	bl	List_remove_node
	ldrh	r3, [r5]
	mov	r0, r4
	sub	r3, r3, #1
	strh	r3, [r5]	@ movhi
	bl	INSERT_DATA_LIST
.L690:
	mov	r0, #0
	ldmfd	sp!, {r3, r4, r5, pc}
.L698:
	.align	2
.L697:
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
.L700:
	uxth	r5, r3
	cmp	r5, r1
	bcs	.L703
	mov	r7, r2
	add	r3, r3, #1
	ldrh	r6, [r7]
	add	r2, r2, #2
	cmp	r6, #0
	bne	.L700
	mov	r4, r0
	bl	FtlFreeSysBlkQueueOut
	movw	r2, #65533
	sub	r3, r0, #1
	mov	r1, r0
	strh	r0, [r7]	@ movhi
	uxth	r3, r3
	cmp	r3, r2
	bls	.L701
	ldr	r3, .L707
	ldr	r0, .L707+4
	ldrh	r2, [r3, #6]
	bl	printk
.L702:
	b	.L702
.L701:
	ldr	r3, [r4, #28]
	strh	r6, [r4, #2]	@ movhi
	add	r3, r3, #1
	str	r3, [r4, #28]
	ldrh	r3, [r4, #8]
	strh	r5, [r4]	@ movhi
	add	r3, r3, #1
	strh	r3, [r4, #8]	@ movhi
.L703:
	mov	r0, #0
	ldmfd	sp!, {r3, r4, r5, r6, r7, pc}
.L708:
	.align	2
.L707:
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
	ldr	r2, .L725
	movw	r3, #2426
	stmfd	sp!, {r4, r5, r6, lr}
	.save {r4, r5, r6, lr}
	mov	r1, #0
	ldrh	r2, [r2, r3]
	mov	ip, #12
	ldr	r3, .L725+4
	movw	lr, #65535
	ldr	r3, [r3, #-3380]
.L710:
	uxth	r0, r1
	cmp	r0, r2
	bcs	.L722
	add	r1, r1, #1
	mla	r4, ip, r1, r3
	ldrh	r4, [r4, #-12]
	cmp	r4, lr
	bne	.L710
	ldmfd	sp!, {r4, r5, r6, pc}
.L722:
	mov	r0, r2
	mov	r1, #0
	mov	ip, #-2147483648
	mov	r5, #12
.L713:
	uxth	r4, r1
	cmp	r4, r2
	bcs	.L723
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
	b	.L713
.L723:
	cmp	r0, r2
	ldmccfd	sp!, {r4, r5, r6, pc}
	ldr	r1, .L725+8
	mov	r0, r2
	mvn	ip, #0
	ldrh	r5, [r1]
	mov	r1, #0
.L716:
	uxth	lr, r1
	cmp	lr, r2
	bcs	.L724
	ldr	r4, [r3, #4]
	cmp	r4, ip
	bcs	.L717
	ldrh	r6, [r3]
	cmp	r6, r5
	movne	ip, r4
	movne	r0, lr
.L717:
	add	r1, r1, #1
	add	r3, r3, #12
	b	.L716
.L724:
	ldmfd	sp!, {r4, r5, r6, pc}
.L726:
	.align	2
.L725:
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
	ldr	r3, .L735
	sub	r1, r3, #3360
	ldrh	r2, [r1, #-14]
	cmp	r2, #4
	cmpls	r0, #0
	addeq	r2, r2, #1
	streqh	r2, [r1, #-14]	@ movhi
	bxeq	lr
	ldr	r0, .L735+4
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
.L729:
	cmp	r2, r0
	beq	.L734
	ldrh	r1, [r2], #2
	cmp	r1, ip
	ldrne	lr, [r3, #-3372]
	addne	r1, r1, lr
	strne	r1, [r3, #-3372]
	b	.L729
.L734:
	ldr	pc, [sp], #4
.L736:
	.align	2
.L735:
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
	ldr	r2, .L741
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
.L742:
	.align	2
.L741:
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
	ldr	r3, .L744
	mov	r2, r0, lsr #5
	and	r0, r0, #31
	ldr	r3, [r3, #-3368]
	ldr	r3, [r3, r2, asl #2]
	mov	r0, r3, lsr r0
	and	r0, r0, #1
	bx	lr
.L745:
	.align	2
.L744:
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
	ldr	r3, .L756
	stmfd	sp!, {r4, r5, lr}
	.save {r4, r5, lr}
	movw	r4, #65535
	ldrh	lr, [r3]
	add	r3, r2, #7
	add	r3, r0, r3, asl #1
.L747:
	cmp	r2, lr
	bcs	.L754
	ldrh	ip, [r3, #2]!
	add	r2, r2, #1
	cmp	ip, r4
	uxth	r2, r2
	ldrneh	ip, [r0, #4]
	addne	ip, ip, #1
	strneh	ip, [r0, #4]	@ movhi
	b	.L747
.L754:
	ldr	r2, .L756+4
	movw	r3, #2388
	add	ip, r0, #14
	movw	r5, #65535
	ldrh	r4, [r2, r3]
	mov	r2, #0
.L750:
	uxth	r3, r2
	cmp	r3, lr
	bcs	.L755
	ldrh	r3, [ip, #2]!
	add	r2, r2, #1
	cmp	r3, r5
	ldrneh	r3, [r0, #4]
	addne	r3, r4, r3
	subne	r3, r3, #1
	rsbne	r3, r1, r3
	strneh	r3, [r0, #4]	@ movhi
	b	.L750
.L755:
	ldmfd	sp!, {r4, r5, pc}
.L757:
	.align	2
.L756:
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
	ldr	r7, .L772
	mov	r4, r0
	mvn	r9, #0
	mov	r5, #0
	strh	r5, [r0, #4]	@ movhi
	ldrh	r8, [r7], #28
	strb	r5, [r0, #7]
.L759:
	uxth	r3, r5
	cmp	r3, r8
	bcs	.L771
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
	b	.L759
.L771:
	ldr	r3, .L772+4
	movw	r2, #2388
	ldrb	r1, [r4, #7]	@ zero_extendqisi2
	ldrh	r2, [r3, r2]
	smulbb	r2, r1, r2
	strh	r2, [r4, #4]	@ movhi
	mov	r2, #0
	strb	r2, [r4, #9]
	ldr	r2, [r3, #2244]
	cmp	r2, #0
	beq	.L762
	ldr	r1, .L772+8
	ldrh	r2, [r4]
	ldr	r1, [r1, #-3608]
	mov	r2, r2, asl #1
	ldrh	r2, [r1, r2]
	cmp	r2, #79
	movls	r2, #1
	strlsb	r2, [r4, #9]
.L762:
	ldrb	r3, [r3]	@ zero_extendqisi2
	mov	r0, #0
	cmp	r3, #0
	movne	r3, #1
	strneb	r3, [r4, #9]
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, pc}
.L773:
	.align	2
.L772:
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
	ldr	r3, .L784
	mov	r6, r0
	ldr	r9, .L784+4
	mov	r4, r5
	add	r2, r3, #2320
	ldrh	r7, [r2]
	movw	r2, #2388
	ldrh	r8, [r3, r2]
.L775:
	uxth	r3, r5
	cmp	r3, r7
	bcs	.L783
	ldrb	r0, [r9, r5]	@ zero_extendqisi2
	mov	r1, r6
	bl	V2P_block
	add	r5, r5, #1
	bl	FtlBbmIsBadBlock
	cmp	r0, #0
	addeq	r4, r4, r8
	uxtheq	r4, r4
	b	.L775
.L783:
	cmp	r4, #0
	beq	.L778
	mov	r1, r4
	mov	r0, #32768
	bl	__aeabi_idiv
	uxth	r4, r0
.L778:
	ldr	r3, .L784+8
	mov	r2, #6
	mov	r0, #0
	ldr	r3, [r3, #-3552]
	mla	r6, r2, r6, r3
	strh	r4, [r6, #4]	@ movhi
	ldmfd	sp!, {r3, r4, r5, r6, r7, r8, r9, pc}
.L785:
	.align	2
.L784:
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
	ldr	r2, .L789
	ldr	r0, [r2, #-3532]
	cmp	r0, #0
	bxeq	lr
	ldr	r3, [r2, #-3552]
	rsb	r0, r3, r0
	ldr	r3, .L789+4
	mov	r0, r0, asr #1
	mul	r0, r3, r0
	ldr	r3, [r2, #-3608]
	uxth	r0, r0
	mov	r0, r0, asl #1
	ldrh	r0, [r3, r0]
	bx	lr
.L790:
	.align	2
.L789:
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
	ldr	r1, .L801
	ldr	r3, [r1, #-3532]
	cmp	r3, #0
	beq	.L797
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
	ldr	r2, .L801+4
	mul	r3, r2, r3
	mov	r2, #0
	uxth	r3, r3
.L794:
	uxth	lr, r2
	cmp	lr, r0
	bcs	.L796
	mul	lr, r4, r3
	add	r2, r2, #1
	ldrh	lr, [ip, lr]
	cmp	lr, r5
	bne	.L798
.L796:
	ldr	r2, [r1, #-3608]
	mov	r3, r3, asl #1
	ldrh	r0, [r2, r3]
	ldmfd	sp!, {r4, r5, pc}
.L798:
	mov	r3, lr
	b	.L794
.L797:
	mov	r0, r3
	bx	lr
.L802:
	.align	2
.L801:
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
	ldr	r8, .L814
	add	r4, r7, #12
	ldr	r1, .L814+4
	.pad #32
	sub	sp, sp, #32
	bl	strcpy
	mov	r0, r4
	ldr	r1, .L814+8
	ldr	r2, [r8, #116]
	bl	sprintf
	ldr	r1, .L814+12
	ldr	r2, [r8, #2404]
	add	r4, r4, r0
	mov	r0, r4
	bl	sprintf
	ldr	r3, .L814+16
	ldr	r3, [r3, #504]
	cmp	r3, #1
	add	r4, r4, r0
	bne	.L809
	add	r0, sp, #16
	add	r1, sp, #20
	add	r2, sp, #24
	add	r3, sp, #28
	bl	NandcGetTimeCfg
	mov	r0, r4
	ldr	r1, .L814+20
	ldr	r6, .L814+24
	sub	r10, r6, #3520
	ldr	r3, [sp, #24]
	ldr	r2, [sp, #16]
	str	r3, [sp]
	ldr	r3, [sp, #28]
	str	r3, [sp, #4]
	ldr	r3, [sp, #20]
	bl	sprintf
	ldr	r1, .L814+28
	add	r4, r4, r0
	add	r5, r4, #10
	mov	r0, r4
	sub	r4, r6, #3600
	bl	strcpy
	mov	r0, r5
	ldr	r1, .L814+32
	ldr	r2, [r8, #2448]
	bl	sprintf
	ldr	r1, .L814+36
	ldr	r2, [r6, #-3372]
	add	r5, r5, r0
	mov	r0, r5
	bl	sprintf
	ldr	r1, .L814+40
	ldr	r2, [r6, #-3364]
	add	r5, r5, r0
	mov	r0, r5
	bl	sprintf
	ldr	r1, .L814+44
	ldr	r2, [r6, #-3360]
	add	r5, r5, r0
	mov	r0, r5
	bl	sprintf
	ldr	r1, .L814+48
	ldr	r2, [r6, #-3356]
	add	r5, r5, r0
	mov	r0, r5
	bl	sprintf
	ldr	r1, .L814+52
	ldr	r2, [r6, #-3352]
	add	r5, r5, r0
	mov	r0, r5
	bl	sprintf
	ldr	r1, .L814+56
	ldr	r2, [r6, #-3348]
	add	r5, r5, r0
	mov	r0, r5
	bl	sprintf
	ldr	r1, .L814+60
	ldr	r2, [r6, #-3344]
	add	r5, r5, r0
	mov	r0, r5
	bl	sprintf
	ldr	r2, [r6, #-3340]
	ldr	r1, .L814+64
	mov	r2, r2, lsr #11
	add	r5, r5, r0
	mov	r0, r5
	bl	sprintf
	ldr	r2, [r6, #-3336]
	ldr	r1, .L814+68
	mov	r2, r2, lsr #11
	add	r5, r5, r0
	mov	r0, r5
	bl	sprintf
	ldr	r1, .L814+72
	ldr	r2, [r6, #-3332]
	add	r5, r5, r0
	mov	r0, r5
	bl	sprintf
	ldr	r1, .L814+76
	ldr	r2, [r6, #-3328]
	add	r5, r5, r0
	mov	r0, r5
	bl	sprintf
	add	r5, r5, r0
	bl	FtlBbtCalcTotleCnt
	ldr	r2, .L814+80
	ldr	r1, .L814+84
	ldrh	r2, [r2, #6]
	mov	r3, r0
	mov	r0, r5
	bl	sprintf
	ldr	r1, .L814+88
	ldrh	r2, [r10, #-8]
	add	r5, r5, r0
	mov	r0, r5
	bl	sprintf
	ldr	r1, .L814+92
	ldr	r2, [r6, #-3324]
	add	r5, r5, r0
	mov	r0, r5
	bl	sprintf
	ldr	r1, .L814+96
	ldr	r2, [r6, #-3320]
	add	r5, r5, r0
	mov	r0, r5
	bl	sprintf
	ldr	r1, .L814+100
	ldr	r2, [r6, #-3316]
	add	r5, r5, r0
	mov	r0, r5
	bl	sprintf
	ldr	r1, .L814+104
	ldr	r2, [r6, #-3604]
	add	r5, r5, r0
	mov	r0, r5
	bl	sprintf
	ldr	r1, .L814+108
	ldr	r2, [r6, #-3312]
	add	r5, r5, r0
	mov	r0, r5
	bl	sprintf
	ldr	r1, .L814+112
	ldr	r2, [r6, #-3308]
	add	r5, r5, r0
	mov	r0, r5
	bl	sprintf
	ldrh	r2, [r4, #30]
	ldr	r1, .L814+116
	add	r5, r5, r0
	mov	r0, r5
	bl	sprintf
	ldrh	r2, [r4, #28]
	ldr	r1, .L814+120
	sub	r4, r6, #3296
	add	r5, r5, r0
	mov	r0, r5
	bl	sprintf
	ldr	r1, .L814+124
	ldr	r2, [r8, #2428]
	add	r5, r5, r0
	mov	r0, r5
	bl	sprintf
	ldr	r1, .L814+128
	ldr	r2, [r8, #2420]
	add	r5, r5, r0
	mov	r0, r5
	bl	sprintf
	ldr	r1, .L814+132
	ldr	r2, [r8, #2316]
	add	r5, r5, r0
	mov	r0, r5
	bl	sprintf
	add	r3, r8, #2512
	ldr	r1, .L814+136
	ldrh	r2, [r3, #6]
	add	r5, r5, r0
	mov	r0, r5
	bl	sprintf
	movw	r3, #2328
	ldrh	r2, [r8, r3]
	ldr	r1, .L814+140
	add	r5, r5, r0
	mov	r0, r5
	bl	sprintf
	ldrh	r2, [r4, #-8]
	ldr	r1, .L814+144
	add	r5, r5, r0
	mov	r0, r5
	bl	sprintf
	ldr	r1, .L814+148
	ldr	r2, [r8, #2332]
	add	r5, r5, r0
	mov	r0, r5
	bl	sprintf
	ldrh	r2, [r4, #-4]
	ldr	r1, .L814+152
	add	r5, r5, r0
	mov	r0, r5
	bl	sprintf
	movw	r3, #2452
	ldrh	r2, [r8, r3]
	ldr	r1, .L814+156
	add	r5, r5, r0
	mov	r0, r5
	bl	sprintf
	ldrh	r2, [r4, #-226]
	ldr	r1, .L814+160
	add	r5, r5, r0
	mov	r0, r5
	bl	sprintf
	ldr	r1, .L814+164
	ldrb	r2, [r6, #-3518]	@ zero_extendqisi2
	add	r5, r5, r0
	mov	r0, r5
	bl	sprintf
	ldr	r1, .L814+168
	ldrh	r2, [r10, #-4]
	add	r5, r5, r0
	mov	r0, r5
	bl	sprintf
	ldr	r1, .L814+172
	ldrb	r2, [r6, #-3516]	@ zero_extendqisi2
	add	r5, r5, r0
	mov	r0, r5
	bl	sprintf
	ldrh	r2, [r4, #-224]
	ldr	r1, .L814+176
	sub	r4, r6, #3472
	sub	r9, r4, #4
	add	r5, r5, r0
	mov	r0, r5
	bl	sprintf
	ldrh	r3, [r10, #-4]
	ldr	r2, [r6, #-3544]
	ldr	r1, .L814+180
	mov	r3, r3, asl #1
	ldrh	r2, [r2, r3]
	add	r5, r5, r0
	mov	r0, r5
	bl	sprintf
	ldrh	r2, [r4, #-2]
	ldr	r1, .L814+184
	add	r5, r5, r0
	mov	r0, r5
	bl	sprintf
	ldr	r1, .L814+188
	ldrb	r2, [r6, #-3470]	@ zero_extendqisi2
	add	r5, r5, r0
	mov	r0, r5
	bl	sprintf
	ldrh	r2, [r4, #-4]
	ldr	r1, .L814+192
	add	r5, r5, r0
	mov	r0, r5
	bl	sprintf
	ldr	r1, .L814+196
	ldrb	r2, [r6, #-3468]	@ zero_extendqisi2
	add	r5, r5, r0
	mov	r0, r5
	bl	sprintf
	ldrh	r2, [r9, #4]
	ldr	r1, .L814+200
	sub	r9, r6, #3424
	add	r5, r5, r0
	mov	r0, r5
	bl	sprintf
	ldrh	r3, [r4, #-4]
	ldr	r2, [r6, #-3544]
	ldr	r1, .L814+204
	mov	r3, r3, asl #1
	ldrh	r2, [r2, r3]
	add	r5, r5, r0
	mov	r0, r5
	bl	sprintf
	ldrh	r2, [r4, #46]
	ldr	r1, .L814+208
	add	r5, r5, r0
	mov	r0, r5
	bl	sprintf
	ldr	r1, .L814+212
	ldrb	r2, [r6, #-3422]	@ zero_extendqisi2
	add	r5, r5, r0
	mov	r0, r5
	bl	sprintf
	ldrh	r2, [r9, #-4]
	ldr	r1, .L814+216
	sub	r9, r6, #3280
	add	r5, r5, r0
	mov	r0, r5
	bl	sprintf
	ldr	r1, .L814+220
	ldrb	r2, [r6, #-3420]	@ zero_extendqisi2
	add	r5, r5, r0
	mov	r0, r5
	bl	sprintf
	ldrh	r2, [r4, #48]
	ldr	r1, .L814+224
	add	r5, r5, r0
	mov	r0, r5
	bl	sprintf
	ldrh	r2, [r4, #190]
	ldr	r1, .L814+228
	add	r5, r5, r0
	mov	r0, r5
	bl	sprintf
	ldr	r1, .L814+232
	ldrb	r2, [r6, #-3278]	@ zero_extendqisi2
	add	r5, r5, r0
	mov	r0, r5
	bl	sprintf
	ldr	r1, .L814+236
	ldrh	r2, [r9, #-4]
	add	r5, r5, r0
	mov	r0, r5
	bl	sprintf
	ldr	r1, .L814+240
	ldrb	r2, [r6, #-3276]	@ zero_extendqisi2
	add	r5, r5, r0
	mov	r0, r5
	bl	sprintf
	ldrh	r2, [r4, #192]
	ldr	r1, .L814+244
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
	ldr	r1, .L814+248
	bl	sprintf
	ldr	r1, .L814+252
	ldr	r2, [r6, #-3160]
	add	r4, r5, r0
	sub	r5, r6, #2704
	mov	r0, r4
	bl	sprintf
	ldr	r1, .L814+256
	ldr	r2, [r6, #-3136]
	add	r4, r4, r0
	mov	r0, r4
	bl	sprintf
	sub	r3, r6, #2720
	ldr	r1, .L814+260
	ldrh	r2, [r3]
	add	r4, r4, r0
	mov	r0, r4
	bl	sprintf
	ldr	r1, .L814+264
	ldrh	r2, [r5, #-14]
	add	r4, r4, r0
	mov	r0, r4
	bl	sprintf
	ldr	r1, .L814+268
	ldr	r2, [r6, #-2716]
	add	r4, r4, r0
	mov	r0, r4
	bl	sprintf
	ldr	r1, .L814+272
	ldrh	r2, [r5, #-8]
	add	r4, r4, r0
	mov	r0, r4
	bl	sprintf
	add	r4, r4, r0
	bl	GetFreeBlockMinEraseCount
	ldr	r1, .L814+276
	mov	r2, r0
	mov	r0, r4
	bl	sprintf
	add	r4, r4, r0
	ldrh	r0, [r10, #-8]
	bl	GetFreeBlockMaxEraseCount
	ldr	r1, .L814+280
	mov	r2, r0
	mov	r0, r4
	bl	sprintf
	ldrh	r3, [r9, #-4]
	movw	r2, #65535
	cmp	r3, r2
	add	r4, r4, r0
	beq	.L806
	ldr	r2, [r6, #-3544]
	mov	r3, r3, asl #1
	mov	r0, r4
	ldr	r1, .L814+284
	ldrh	r2, [r2, r3]
	bl	sprintf
	add	r4, r4, r0
.L806:
	mov	r0, #0
	mov	r5, #0
	bl	List_get_gc_head_node
	movw	r10, #65535
	mov	r9, #6
	uxth	r3, r0
.L808:
	cmp	r3, r10
	beq	.L807
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
	ldr	r1, .L814+288
	str	r2, [sp, #8]
	mov	r2, r5
	bl	sprintf
	add	r5, r5, #1
	ldr	r3, [r6, #-3552]
	cmp	r5, #16
	ldrh	r3, [r3, r8]
	add	r4, r4, r0
	bne	.L808
.L807:
	ldr	r2, [r6, #-3552]
	mov	r5, #0
	ldr	r3, [r6, #-3532]
	movw	r10, #65535
	mov	r9, #6
	rsb	r3, r2, r3
	ldr	r2, .L814+292
	mov	r3, r3, asr #1
	mul	r3, r2, r3
	uxth	r3, r3
.L810:
	cmp	r3, r10
	beq	.L809
	mul	r8, r9, r3
	ldr	r2, [r6, #-3552]
	ldr	r1, [r6, #-3608]
	mov	r0, r4
	add	r2, r2, r8
	ldrh	r2, [r2, #4]
	str	r2, [sp]
	mov	r2, r3, asl #1
	ldrh	r2, [r1, r2]
	ldr	r1, .L814+296
	str	r2, [sp, #4]
	mov	r2, r5
	bl	sprintf
	add	r5, r5, #1
	ldr	r3, [r6, #-3552]
	cmp	r5, #4
	ldrh	r3, [r3, r8]
	add	r4, r4, r0
	bne	.L810
.L809:
	rsb	r0, r7, r4
	add	sp, sp, #32
	@ sp needed
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, pc}
.L815:
	.align	2
.L814:
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
	ldr	r1, .L818
	ldr	r2, .L818+4
	bl	sprintf
	add	r4, r5, r0
	mov	r0, r4
	bl	FtlPrintInfo2buf
	add	r0, r4, r0
	rsb	r0, r5, r0
	ldmfd	sp!, {r3, r4, r5, pc}
.L819:
	.align	2
.L818:
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
	ldr	r4, .L850
	ldr	r2, [r4, #-3316]
	ldr	r3, [r4, #-3308]
	cmp	r2, r3
	bcs	.L821
	ldr	r2, .L850+4
	movw	r1, #2328
	ldr	r0, [r4, #-3608]
	mov	r3, #0
	str	r3, [r4, #-3324]
	ldrh	r1, [r2, r1]
	sub	r0, r0, #2
	mov	r5, r2
.L822:
	cmp	r3, r1
	bcs	.L849
	ldrh	r2, [r0, #2]!
	add	r3, r3, #1
	ldr	ip, [r4, #-3324]
	add	r2, r2, ip
	str	r2, [r4, #-3324]
	b	.L822
.L849:
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
	b	.L824
.L821:
	ldr	r3, [r4, #-3312]
	cmp	r2, r3
	bls	.L824
	ldr	ip, .L850+8
	add	r3, r3, #1
	str	r3, [r4, #-3312]
	mov	r3, #0
.L826:
	ldrh	r2, [ip]
	cmp	r3, r2
	bcs	.L824
	ldr	r0, [r4, #-3608]
	mov	r1, r3, asl #1
	add	r3, r3, #1
	ldrh	r2, [r0, r1]
	add	r2, r2, #1
	strh	r2, [r0, r1]	@ movhi
	b	.L826
.L824:
	ldr	r6, [r4, #-3308]
	ldr	r8, [r4, #-3316]
	add	r3, r6, #256
	cmp	r3, r8
	bls	.L829
	ldr	r2, .L850
	add	r3, r6, #768
	ldr	r2, [r2, #-3312]
	cmp	r3, r2
	bls	.L829
	ldr	r3, .L850+4
	cmp	r6, #40
	ldr	r2, [r3, #2244]
	movls	r3, #0
	movhi	r3, #1
	cmp	r2, #0
	orreq	r3, r3, #1
	cmp	r3, #0
	beq	.L829
.L831:
	movw	r0, #65535
	b	.L830
.L829:
	ldr	r5, .L850
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
	bne	.L831
	ldr	r3, [r5, #-3548]
	cmp	r3, #0
	beq	.L831
	ldr	r0, .L850+4
	movw	r2, #2328
	ldr	ip, .L850+12
	movw	r7, #65535
	ldr	r10, [r5, #-3608]
	mov	lr, #6
	ldrh	r2, [r0, r2]
	ldr	r0, [r5, #-3552]
	mov	r5, r7
	str	r2, [sp, #20]
.L832:
	ldrh	r2, [r3]
	movw	fp, #65535
	cmp	r2, fp
	beq	.L834
	add	r1, r1, #1
	ldr	fp, [sp, #20]
	uxth	r1, r1
	cmp	r1, fp
	bhi	.L831
	ldrh	fp, [r3, #4]
	cmp	fp, #0
	beq	.L833
	rsb	r3, r0, r3
	mov	r3, r3, asr #1
	mul	r3, ip, r3
	uxth	r3, r3
	mov	fp, r3, asl #1
	ldrh	fp, [r10, fp]
	cmp	fp, r6
	bls	.L838
	cmp	fp, r7
	movcc	r7, fp
	movcc	r5, r3
.L833:
	mla	r3, lr, r2, r0
	b	.L832
.L838:
	mov	r5, r3
.L834:
	movw	r3, #65535
	cmp	r5, r3
	beq	.L831
	mov	r3, r5, asl #1
	ldrh	fp, [r10, r3]
	cmp	fp, r6
	bls	.L836
	str	r3, [sp, #20]
	bl	GetFreeBlockMinEraseCount
	ldr	r3, [sp, #20]
	cmp	r0, r6
	strhi	r7, [r4, #-3308]
.L836:
	cmp	fp, r8
	bcs	.L831
	add	r2, fp, #128
	cmp	r9, r2
	ble	.L831
	add	r2, fp, #256
	cmp	r2, r8
	bcc	.L837
	ldr	r2, [r4, #-3312]
	add	fp, fp, #768
	cmp	fp, r2
	bcs	.L831
.L837:
	ldr	r2, [r4, #-3544]
	mov	r1, r5
	ldr	r0, .L850+16
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
.L830:
	add	sp, sp, #28
	@ sp needed
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L851:
	.align	2
.L850:
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
	beq	.L853
	ldr	r2, .L855
	mov	r3, r0, asl #1
	mov	r1, #0
	ldr	r2, [r2, #-3544]
	strh	r1, [r2, r3]	@ movhi
	bl	INSERT_FREE_LIST
.L853:
	mov	r0, #0
	ldmfd	sp!, {r3, pc}
.L856:
	.align	2
.L855:
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
	ldr	ip, .L865
	mov	r3, #0
	ldr	r1, .L865+4
	stmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, lr}
	.save {r4, r5, r6, r7, r8, r9, r10, lr}
	add	r7, ip, #78
	mov	r5, #12
	mov	r6, #1
	mov	r8, #36
	str	r3, [r1, #-2704]
.L858:
	ldrh	r2, [ip]
	add	r4, r3, #1
	uxth	r3, r3
	cmp	r3, r2
	bcs	.L863
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
	ldr	r2, .L865+8
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
	b	.L858
.L863:
	ldr	r0, .L865+4
	mov	ip, #12
	ldr	r4, .L865+12
	mov	lr, #0
.L860:
	ldr	r3, [r1, #-2684]
	cmp	r2, r3
	bcs	.L864
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
	ldr	r3, .L865+8
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
	b	.L860
.L864:
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, pc}
.L866:
	.align	2
.L865:
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
	ldr	r3, .L875
	stmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, lr}
	.save {r4, r5, r6, r7, r8, r9, r10, lr}
	mov	lr, #0
	ldr	r6, [r3, #-2684]
	mov	r5, #36
	ldr	r4, [r3, #-2700]
	mov	r7, lr
	mov	r8, #12
.L868:
	uxth	ip, lr
	cmp	ip, r1
	ldmcsfd	sp!, {r4, r5, r6, r7, r8, r9, r10, pc}
	mla	ip, r5, ip, r0
	mov	r2, #0
.L869:
	uxth	r3, r2
	cmp	r3, r6
	bcs	.L870
	mul	r3, r8, r3
	add	r2, r2, #1
	add	r9, r4, r3
	ldr	r10, [r4, r3]
	ldr	r3, [ip, #8]
	cmp	r10, r3
	bne	.L869
	str	r7, [r9, #8]
.L870:
	add	lr, lr, #1
	b	.L868
.L876:
	.align	2
.L875:
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
	ldr	r3, .L886
	mov	ip, #0
	stmfd	sp!, {r4, r5, r6, r7, r8, r9, lr}
	.save {r4, r5, r6, r7, r8, r9, lr}
	mov	r6, #12
	ldr	r4, [r3, #-2684]
	mov	r7, #1
	ldr	r5, [r3, #-2700]
	mov	r8, #36
.L878:
	uxth	r2, ip
	cmp	r2, r1
	bcs	.L885
	mov	lr, #0
.L879:
	uxth	r3, lr
	cmp	r3, r4
	bcs	.L880
	mla	r3, r6, r3, r5
	add	lr, lr, #1
	ldr	r9, [r3, #8]
	cmp	r9, #0
	bne	.L879
	mla	r2, r8, r2, r0
	ldr	lr, [r3]
	str	r7, [r3, #8]
	str	lr, [r2, #8]
	ldr	r3, [r3, #4]
	str	r3, [r2, #12]
.L880:
	add	ip, ip, #1
	b	.L878
.L885:
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, pc}
.L887:
	.align	2
.L886:
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
	ldr	r2, .L894
	sub	r2, r2, #2672
	ldr	r3, [r2, #-8]
	ldrh	r2, [r2, #-4]
	add	r2, r3, r2, asl #1
.L889:
	cmp	r3, r2
	beq	.L893
	ldrh	r1, [r3], #2
	cmp	r1, r0
	bne	.L889
	mov	r0, #1
	bx	lr
.L893:
	mov	r0, #0
	bx	lr
.L895:
	.align	2
.L894:
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
	ldr	r2, .L904
	mov	lr, #0
	sub	r3, r2, #2672
	ldr	r8, [r2, #-2680]
	ldrh	r1, [r3, #-4]
	sub	r7, r8, #2
.L897:
	uxth	ip, lr
	cmp	ip, r1
	bcs	.L901
	ldrh	r9, [r7, #2]!
	add	lr, lr, #1
	cmp	r9, r0
	bne	.L897
.L901:
	cmp	ip, r1
	moveq	ip, ip, asl #1
	streqh	r0, [r8, ip]	@ movhi
	ldreqh	ip, [r3, #-4]
	addeq	ip, ip, #1
	streqh	ip, [r3, #-4]	@ movhi
.L899:
	ldr	r3, .L904+4
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
.L905:
	.align	2
.L904:
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
	ldr	r4, .L914
	ldrh	r3, [r4, #-10]
	cmp	r3, r0
	beq	.L908
	ldrh	r3, [r4, #-8]
	cmp	r3, r0
	beq	.L908
	ldrh	r3, [r4, #-6]
	cmp	r3, r0
	beq	.L908
	ldrh	r3, [r4, #-4]
	cmp	r3, r0
	beq	.L908
	ldr	r0, .L914+4
	mov	r1, r5
	bl	printk
	ldrh	r2, [r4, #-10]
	movw	r3, #65535
	cmp	r2, r3
	streqh	r5, [r4, #-10]	@ movhi
	beq	.L908
	ldrh	r2, [r4, #-8]
	cmp	r2, r3
	streqh	r5, [r4, #-8]	@ movhi
	beq	.L908
	ldrh	r2, [r4, #-6]
	cmp	r2, r3
	streqh	r5, [r4, #-6]	@ movhi
	beq	.L908
	ldrh	r2, [r4, #-4]
	cmp	r2, r3
	streqh	r5, [r4, #-4]	@ movhi
.L908:
	mov	r0, #0
	ldmfd	sp!, {r3, r4, r5, pc}
.L915:
	.align	2
.L914:
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
	ldr	r4, .L927
	ldrh	r3, [r4, #-10]
	cmp	r3, r0
	beq	.L924
	ldrh	r3, [r4, #-8]
	cmp	r3, r0
	beq	.L924
	ldrh	r3, [r4, #-6]
	cmp	r3, r0
	beq	.L924
	ldrh	r3, [r4, #-4]
	cmp	r3, r0
	beq	.L924
	ldr	r0, .L927+4
	mov	r1, r5
	bl	printk
	ldrh	r2, [r4, #-10]
	movw	r3, #65535
	cmp	r2, r3
	streqh	r5, [r4, #-10]	@ movhi
	beq	.L924
	ldrh	r2, [r4, #-8]
	cmp	r2, r3
	streqh	r5, [r4, #-8]	@ movhi
	beq	.L924
	ldrh	r2, [r4, #-6]
	cmp	r2, r3
	streqh	r5, [r4, #-6]	@ movhi
	beq	.L924
	ldrh	r2, [r4, #-4]
	cmp	r2, r3
	bne	.L925
	strh	r5, [r4, #-4]	@ movhi
.L924:
	mov	r0, #0
	ldmfd	sp!, {r3, r4, r5, pc}
.L925:
	mvn	r0, #0
	ldmfd	sp!, {r3, r4, r5, pc}
.L928:
	.align	2
.L927:
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
	ldr	r6, .L939
	mov	r2, r5
	sub	r7, r6, #2656
	ldrh	r1, [r7, #-2]
	mov	r4, r0
	ldr	r0, .L939+4
	bl	printk
	mov	r0, r4
	bl	FtlGcRefreshBlock
	ldr	r3, .L939+8
	ldr	r3, [r3, #2244]
	cmp	r3, #0
	mov	r3, r7
	beq	.L930
	ldr	r1, [r6, #-3608]
	mov	r4, r4, asl #1
	ldrh	r2, [r1, r4]
	cmp	r2, #39
	subhi	r2, r2, #40
	strhih	r2, [r1, r4]	@ movhi
.L930:
	ldrh	r2, [r3, #-2]
	mov	r1, #0
.L931:
	uxth	r0, r1
	cmp	r0, r2
	bcs	.L938
	add	r1, r1, #1
	add	r0, r3, r1, asl #1
	ldrh	r0, [r0, #-2]
	cmp	r0, r5
	bne	.L931
	b	.L932
.L938:
	cmp	r2, #15
	addls	r1, r2, #1
	movls	r2, r2, asl #1
	strlsh	r1, [r3, #-2]	@ movhi
	strlsh	r5, [r3, r2]	@ movhi
.L932:
	mov	r0, #0
	ldmfd	sp!, {r3, r4, r5, r6, r7, pc}
.L940:
	.align	2
.L939:
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
	ldr	r3, .L949
	ldrh	r2, [r3, #-2]
	cmp	r2, #0
	beq	.L948
	ldrh	r0, [r3, #-10]
	movw	r1, #65535
	cmp	r0, r1
	bne	.L948
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
.L948:
	mov	r0, #0
	bx	lr
.L950:
	.align	2
.L949:
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
	bne	.L953
	ldr	r3, .L996
.L954:
	mov	r2, r0, asl #1
	strh	r0, [r2, r3]	@ movhi
	add	r0, r0, #1
	cmp	r0, #512
	bne	.L954
.L958:
	mov	r1, #255
	ldr	r0, .L996+4
	mov	r2, #2048
	uxth	r4, r4
	bl	ftl_memset
	mov	r3, #0
	ldr	r1, .L996
	ldr	r0, .L996+4
	b	.L955
.L953:
	cmp	r0, #1
	bne	.L956
	ldr	ip, .L996
	mov	r3, #0
.L957:
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
	bne	.L957
	b	.L958
.L956:
	cmp	r0, #2
	bne	.L959
	ldr	r1, .L996
	mov	r3, #0
.L960:
	uxth	r2, r3
	mov	r0, r3, asl #1
	cmp	r2, #1
	add	r3, r3, #1
	mov	ip, r2, asl #1
	subhi	r2, ip, #1
	cmp	r3, #512
	strh	r2, [r0, r1]	@ movhi
	bne	.L960
	b	.L958
.L959:
	cmp	r0, #3
	bne	.L961
	ldr	ip, .L996
	mov	r3, #0
.L962:
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
	bne	.L962
	b	.L958
.L961:
	cmp	r0, #4
	mov	r3, #0
	bne	.L963
	ldr	r2, .L996+8
	strh	r3, [r2, #148]	@ movhi
	mov	r3, #1
	strh	r0, [r2, #156]	@ movhi
	strh	r3, [r2, #150]	@ movhi
	mov	r3, #2
	strh	r3, [r2, #152]	@ movhi
	mov	r3, #3
	strh	r3, [r2, #154]	@ movhi
	mov	r3, #5
	strh	r3, [r2, #158]	@ movhi
	mov	r3, #7
	strh	r3, [r2, #160]	@ movhi
	mov	r3, #8
	strh	r3, [r2, #162]!	@ movhi
.L964:
	tst	r3, #1
	movne	r1, #7
	moveq	r1, #6
	rsb	r1, r1, r3, asl #1
	add	r3, r3, #1
	strh	r1, [r2, #2]!	@ movhi
	uxth	r3, r3
	cmp	r3, #512
	bne	.L964
	b	.L958
.L963:
	cmp	r0, #5
	bne	.L965
	ldr	r2, .L996
.L966:
	mov	r1, r3, asl #1
	strh	r3, [r1, r2]	@ movhi
	add	r3, r3, #1
	cmp	r3, #16
	bne	.L966
	ldr	r2, .L996+12
.L967:
	strh	r3, [r2, #2]!	@ movhi
	add	r3, r3, #2
	uxth	r3, r3
	cmp	r3, #1008
	bne	.L967
	b	.L958
.L965:
	cmp	r0, #6
	bne	.L968
	ldr	r1, .L996+16
.L969:
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
	bne	.L969
	b	.L958
.L968:
	cmp	r0, #9
	bne	.L970
	ldr	r2, .L996+8
	movw	r1, #1021
	strh	r3, [r2, #148]	@ movhi
	mov	r3, #1
	strh	r3, [r2, #150]	@ movhi
	mov	r3, #2
	strh	r3, [r2, #152]!	@ movhi
	mov	r3, #3
.L971:
	strh	r3, [r2, #2]!	@ movhi
	add	r3, r3, #2
	uxth	r3, r3
	cmp	r3, r1
	bne	.L971
	b	.L958
.L970:
	cmp	r0, #10
	bne	.L972
	ldr	r2, .L996
.L973:
	mov	r1, r3, asl #1
	strh	r3, [r1, r2]	@ movhi
	add	r3, r3, #1
	cmp	r3, #63
	bne	.L973
	ldr	r2, .L996+20
	movw	r1, #961
.L974:
	strh	r3, [r2, #2]!	@ movhi
	add	r3, r3, #2
	uxth	r3, r3
	cmp	r3, r1
	bne	.L974
	b	.L958
.L972:
	cmp	r0, #11
	bne	.L975
	ldr	r2, .L996
	mov	r3, #0
.L976:
	mov	r1, r3, asl #1
	strh	r3, [r1, r2]	@ movhi
	add	r3, r3, #1
	cmp	r3, #8
	bne	.L976
	ldr	r1, .L996+24
.L977:
	tst	r3, #1
	movne	r2, #7
	moveq	r2, #6
	rsb	r2, r2, r3, asl #1
	add	r3, r3, #1
	strh	r2, [r1, #2]!	@ movhi
	uxth	r3, r3
	cmp	r3, #512
	bne	.L977
	b	.L958
.L975:
	cmp	r0, #12
	bne	.L958
	ldr	r3, .L996+8
	mov	r2, #0
	strh	r2, [r3, #148]	@ movhi
	mov	r2, #1
	strh	r2, [r3, #150]	@ movhi
	mov	r2, #2
	strh	r2, [r3, #152]	@ movhi
	mov	r2, #3
	strh	r2, [r3, #154]!	@ movhi
	mov	r2, #4
.L978:
	sub	r1, r2, #1
	add	r1, r1, r2, lsr #1
	add	r2, r2, #1
	strh	r1, [r3, #2]!	@ movhi
	uxth	r2, r2
	cmp	r2, #512
	bne	.L978
	b	.L958
.L955:
	uxth	r2, r3
	cmp	r2, r4
	bcs	.L995
	mov	r2, r3, asl #1
	add	r3, r3, #1
	ldrh	r2, [r2, r1]
	mov	ip, r2, asl #1
	strh	r2, [r0, ip]	@ movhi
	b	.L955
.L995:
	ldmfd	sp!, {r4, pc}
.L997:
	.align	2
.L996:
	.word	.LANCHOR0+148
	.word	.LANCHOR2-2620
	.word	.LANCHOR0
	.word	.LANCHOR0+178
	.word	.LANCHOR0+146
	.word	.LANCHOR0+272
	.word	.LANCHOR0+162
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
	ldr	r3, .L1013
	ldr	r5, .L1013+4
	ldr	r9, .L1013+8
	ldrh	r0, [r3, #10]
	strb	r6, [r5, #2230]
	mov	r7, r5
	strb	r6, [r9, #-572]
	bl	FlashBlockAlignInit
	mov	r1, r6
	mov	r2, #8
	ldr	r0, .L1013+12
	bl	ftl_memset
	mov	r1, r6
	mov	r2, #32
	ldr	r0, .L1013+16
	bl	ftl_memset
	ldr	r0, .L1013+20
	mov	r1, r6
	mov	r2, #128
	bl	ftl_memset
	ldr	r4, [r5, #44]
	ldr	fp, .L1013+24
	add	r8, r4, #1
.L1000:
	mov	r0, r8
	add	r1, fp, r6, asl #3
	ldrb	r2, [r4]	@ zero_extendqisi2
	bl	FlashMemCmp8
	ldr	r10, .L1013+24
	cmp	r0, #0
	bne	.L999
	ldrb	r3, [r7, #2230]	@ zero_extendqisi2
	add	r2, r7, r3, asl #2
	str	r0, [r2, #1172]
	add	r2, r3, #1
	add	r3, r7, r3
	strb	r2, [r7, #2230]
	strb	r6, [r3, #2232]
.L999:
	add	r6, r6, #1
	cmp	r6, #4
	bne	.L1000
	ldrb	r3, [r5, #2230]	@ zero_extendqisi2
	ldr	r7, .L1013+4
	strb	r3, [r9, #-572]
	ldrb	r3, [r4, #8]	@ zero_extendqisi2
	cmp	r3, #2
	beq	.L1001
.L1005:
	ldrb	r3, [r4, #13]	@ zero_extendqisi2
	ldrb	r2, [r5, #2230]	@ zero_extendqisi2
	smulbb	r2, r2, r3
	ldrh	r3, [r4, #14]
	smulbb	r3, r2, r3
	ldr	r2, .L1013+28
	strh	r3, [r2, #-2]	@ movhi
	ldmfd	sp!, {r3, r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L1001:
	ldr	r9, [r7, #4]
	mov	r6, #0
.L1004:
	mov	r0, r8
	add	r1, r10, r6, asl #3
	ldrb	r2, [r4]	@ zero_extendqisi2
	bl	FlashMemCmp8
	cmp	r0, #0
	bne	.L1002
	ldrb	r1, [r4, #13]	@ zero_extendqisi2
	ldrh	r3, [r4, #14]
	ldrb	r2, [r7, #2230]	@ zero_extendqisi2
	mul	r1, r9, r1
	and	r3, r3, #65280
	add	r0, r7, r2, asl #2
	mul	r3, r3, r1
	str	r3, [r0, #1172]
	ldrb	r1, [r4, #23]	@ zero_extendqisi2
	cmp	r1, #0
	movne	r3, r3, asl #1
	strne	r3, [r0, #1172]
	add	r3, r2, #1
	add	r2, r5, r2
	strb	r3, [r5, #2230]
	strb	r6, [r2, #2232]
.L1002:
	add	r6, r6, #1
	cmp	r6, #4
	bne	.L1004
	b	.L1005
.L1014:
	.align	2
.L1013:
	.word	.LANCHOR1+472
	.word	.LANCHOR0
	.word	.LANCHOR2
	.word	.LANCHOR0+2232
	.word	.LANCHOR0+1172
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
	ldr	r2, .L1020
	ldr	r0, .L1020+4
	mov	ip, #1
	ldr	r3, [r2, #44]
	ldrb	r1, [r3, #9]	@ zero_extendqisi2
	ldr	r3, [r2, #4]
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
.L1016:
	uxtb	r2, r3
	cmp	r2, r1
	bcs	.L1019
	ldrb	lr, [r3, r0]	@ zero_extendqisi2
	add	r3, r3, #1
	ldrb	r2, [r4, #10]	@ zero_extendqisi2
	orr	r2, r2, ip, asl lr
	strb	r2, [r4, #10]
	b	.L1016
.L1019:
	ldmfd	sp!, {r4, pc}
.L1021:
	.align	2
.L1020:
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
	ldr	r4, .L1127
	mvn	r2, #0
	ldr	r5, .L1127+4
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
	ldr	r6, .L1127+8
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
	ldr	r7, .L1127+12
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
.L1024:
	cmp	r3, ip
	bcs	.L1125
	ldr	r0, [r5, #2480]
	add	r3, r3, #1
	add	r0, r0, r1
	add	r1, r1, r2
	str	r0, [r6, #4]!
	b	.L1024
.L1125:
	ldr	r2, .L1127+16
	mov	r1, #0
.L1026:
	cmp	r3, #8
	addne	r0, r2, r3, asl #2
	addne	r3, r3, #1
	strne	r1, [r0, #28]
	bne	.L1026
.L1126:
	ldr	r2, [r4, #-472]
	ldr	r3, .L1127
	cmp	r2, #0
	bne	.L1028
.L1030:
	ldr	r1, .L1127+20
	ldr	r0, .L1127+24
	bl	printk
	mvn	r0, #0
	ldmfd	sp!, {r3, r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L1028:
	ldr	r2, [r3, #-468]
	cmp	r2, #0
	beq	.L1030
	ldr	r2, [r3, #-452]
	cmp	r2, #0
	beq	.L1030
	ldr	r2, [r3, #-444]
	cmp	r2, #0
	beq	.L1030
	ldr	r2, [r3, #-3380]
	cmp	r2, #0
	beq	.L1030
	ldr	r2, [r3, #-440]
	cmp	r2, #0
	beq	.L1030
	ldr	r2, [r3, #-3552]
	cmp	r2, #0
	beq	.L1030
	ldr	r2, [r5, #2480]
	cmp	r2, #0
	beq	.L1030
	ldr	r3, [r3, #-3544]
	cmp	r3, #0
	beq	.L1030
	ldr	r2, [r4, #-2680]
	ldr	r3, .L1127
	cmp	r2, #0
	beq	.L1030
	ldr	r2, [r3, #-2672]
	cmp	r2, #0
	beq	.L1030
	ldr	r2, [r3, #-536]
	cmp	r2, #0
	beq	.L1030
	ldr	r2, [r3, #-528]
	cmp	r2, #0
	beq	.L1030
	ldr	r2, [r3, #-3612]
	cmp	r2, #0
	beq	.L1030
	ldr	r2, [r3, #-2688]
	cmp	r2, #0
	beq	.L1030
	ldr	r2, [r3, #-532]
	cmp	r2, #0
	beq	.L1030
	ldr	r2, [r3, #-524]
	cmp	r2, #0
	beq	.L1030
	ldr	r2, [r3, #-520]
	cmp	r2, #0
	beq	.L1030
	ldr	r3, [r3, #-516]
	cmp	r3, #0
	beq	.L1030
	ldr	r2, [r4, #-2696]
	ldr	r3, .L1127
	cmp	r2, #0
	beq	.L1030
	ldr	r2, [r3, #-508]
	cmp	r2, #0
	beq	.L1030
	ldr	r2, [r3, #-504]
	cmp	r2, #0
	beq	.L1030
	ldr	r2, [r3, #-2700]
	cmp	r2, #0
	beq	.L1030
	ldr	r2, [r3, #-500]
	cmp	r2, #0
	beq	.L1030
	ldr	r2, [r3, #-496]
	cmp	r2, #0
	beq	.L1030
	ldr	r2, [r3, #-2692]
	cmp	r2, #0
	beq	.L1030
	ldr	r2, [r3, #-3608]
	cmp	r2, #0
	beq	.L1030
	ldr	r3, [r3, #-484]
	cmp	r3, #0
	beq	.L1030
	ldr	r3, .L1127+4
	ldr	r3, [r3, #2436]
	cmp	r3, #0
	beq	.L1030
	ldr	r3, .L1127
	ldr	r2, [r3, #-464]
	cmp	r2, #0
	beq	.L1030
	ldr	r2, [r3, #-460]
	cmp	r2, #0
	beq	.L1030
	ldr	r3, [r3, #-456]
	cmp	r3, #0
	beq	.L1030
	mov	r0, #0
	ldmfd	sp!, {r3, r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L1128:
	.align	2
.L1127:
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
	ldr	r3, .L1135
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
	ldr	ip, .L1135+4
	add	r3, r3, #2
	sub	r1, r4, #2
	mov	r4, #1
.L1131:
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
	bne	.L1131
	ldmfd	sp!, {r4, r5, r6, pc}
.L1136:
	.align	2
.L1135:
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
	ldr	r0, .L1138
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
.L1139:
	.align	2
.L1138:
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
	ldr	r3, .L1142
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
.L1143:
	.align	2
.L1142:
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
.L1145:
	ldrh	r1, [r4, #6]
	uxth	r3, r2
	cmp	r1, r3
	bls	.L1165
	ldr	r0, [r6, r3, asl #2]
	mov	r1, #0
	ubfx	r0, r0, #10, #16
.L1146:
	ldrh	ip, [r4, #10]
	uxth	r3, r1
	cmp	ip, r3
	bls	.L1166
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
	b	.L1146
.L1166:
	add	r2, r2, #1
	b	.L1145
.L1165:
	ldr	r2, .L1168
	movw	r3, #2390
	mov	r8, #0
	mov	r1, r8
	mov	fp, r8
	ldrh	r2, [r2, r3]
	ldrh	r3, [r4]
	mov	r3, r3, asl #1
	strh	r2, [r5, r3]	@ movhi
	ldrh	r9, [r5]
.L1150:
	ldrh	r3, [r4, #10]
	uxth	r6, r8
	cmp	r3, r6
	bls	.L1167
	mov	r2, r6, asl #1
	ldrh	r3, [r5, r2]
	cmp	r9, r3
	bls	.L1151
	ldrh	r0, [r7, r2]
	add	r10, r7, r2
	cmp	r0, #0
	bne	.L1152
	b	.L1153
.L1151:
	cmp	r3, #0
	bne	.L1153
	ldrh	r0, [r7, r2]
	add	r10, r7, r2
	cmp	r0, #0
	movne	r6, r1
	beq	.L1153
	b	.L1155
.L1152:
	cmp	r3, #0
	movne	r1, r6
	movne	r9, r3
	bne	.L1153
	mov	r9, r3
.L1155:
	mov	r1, #1
	bl	FtlFreeSysBlkQueueIn
	strh	fp, [r10]	@ movhi
	ldrh	r3, [r4, #8]
	mov	r1, r6
	sub	r3, r3, #1
	strh	r3, [r4, #8]	@ movhi
.L1153:
	add	r8, r8, #1
	b	.L1150
.L1167:
	mov	r0, r1
	ldmfd	sp!, {r3, r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L1169:
	.align	2
.L1168:
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
	ldr	r5, .L1175
	mvn	r6, #0
	ldr	r4, .L1175+4
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
	ldr	ip, .L1175+8
	mov	r1, #0
	mov	r3, r4
	sub	r7, ip, #28
	mov	r4, #12
	mov	r5, r1
.L1171:
	ldrh	r2, [ip]
	add	lr, r1, #1
	uxth	r1, r1
	ldr	r0, .L1175
	cmp	r2, r1
	bls	.L1174
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
	b	.L1171
.L1174:
	ldr	r2, .L1175+12
	mvn	r1, #0
	strh	r1, [r2, #2]	@ movhi
	strh	r1, [r2]	@ movhi
	ldr	r1, [r0, #2416]
	strh	r1, [r2, #10]	@ movhi
	ldr	r1, .L1175+16
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
.L1176:
	.align	2
.L1175:
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
	ldr	r5, .L1179
	mov	r4, #0
	ldr	r6, .L1179+4
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
.L1180:
	.align	2
.L1179:
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
	ldr	r10, .L1193
	mov	r2, #6
	ldr	r6, .L1193+4
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
.L1182:
	ldr	r3, .L1193+8
	uxth	r7, r4
	sxth	r10, r7
	ldrh	r2, [r3]
	cmp	r10, r2
	bge	.L1189
	sub	r3, r3, #8
	ldr	r2, .L1193+12
	mov	ip, r7
	ldrh	r3, [r3]
	str	r3, [sp]
	ldrh	r3, [r2]
	mov	r2, #0
	mov	r6, r2
	str	r3, [sp, #4]
.L1190:
	ldr	r3, [sp]
	sxth	r1, r2
	cmp	r1, r3
	bge	.L1192
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
	b	.L1190
.L1192:
	cmp	r6, #0
	beq	.L1185
	sxth	r1, r6
	mov	r0, #32768
	bl	__aeabi_idiv
	uxth	r6, r0
	b	.L1186
.L1185:
	sxth	r7, r7
	ldr	r2, [r5, #-3544]
	mvn	r1, #0
	mov	r7, r7, asl #1
	strh	r1, [r2, r7]	@ movhi
.L1186:
	mov	r1, r10, asl #1
	ldr	r2, [r5, #-3552]
	add	r0, r1, r10
	add	r2, r2, r0, asl #1
	strh	r6, [r2, #4]	@ movhi
	ldr	r2, .L1193+16
	ldrh	r0, [r2]
	cmp	r10, r0
	beq	.L1187
	ldrh	r0, [r2, #48]
	cmp	r10, r0
	beq	.L1187
	ldrh	r2, [r2, #96]
	cmp	r10, r2
	beq	.L1187
	ldr	r3, [fp, #-3544]
	uxth	r0, r4
	ldrh	r3, [r3, r1]
	cmp	r3, #0
	bne	.L1188
	add	r8, r8, #1
	uxth	r8, r8
	bl	INSERT_FREE_LIST
	b	.L1187
.L1188:
	add	r9, r9, #1
	uxth	r9, r9
	bl	INSERT_DATA_LIST
.L1187:
	add	r4, r4, #1
	b	.L1182
.L1189:
	ldr	r3, [sp, #12]
	mov	r0, #0
	strh	r9, [r3]	@ movhi
	ldr	r3, .L1193+20
	strh	r8, [r3, #-8]	@ movhi
	add	sp, sp, #28
	@ sp needed
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L1194:
	.align	2
.L1193:
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
	ldr	r4, .L1197
	movw	r5, #2392
	ldr	r6, .L1197+4
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
.L1198:
	.align	2
.L1197:
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
	ldr	r1, .L1202
	bl	ftl_memcpy
	mov	r0, #0
	ldmfd	sp!, {r3, pc}
.L1203:
	.align	2
.L1202:
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
	ldr	r8, .L1217
.L1208:
	mov	r7, r4, asl #5
	ldr	r1, .L1217+4
	add	r0, r7, #1
	ldrb	r2, [r8, r4, asl #5]	@ zero_extendqisi2
	add	r0, r8, r0
	ldr	r6, .L1217+8
	bl	FlashMemCmp8
	add	r9, r6, #508
	subs	r5, r0, #0
	bne	.L1205
	adds	r9, r9, r7
	beq	.L1211
	add	r4, r6, r7
	mov	r3, r5
	ldr	r1, .L1217+12
	ldrb	r2, [r4, #530]	@ zero_extendqisi2
	b	.L1210
.L1205:
	add	r4, r4, #1
	cmp	r4, #86
	bne	.L1208
	b	.L1211
.L1216:
	add	r3, r3, #1
	cmp	r3, #4
	beq	.L1209
.L1210:
	ldrb	r0, [r1, r3, asl #5]	@ zero_extendqisi2
	cmp	r0, r2
	bne	.L1216
.L1209:
	ldr	r4, .L1217+16
	mov	r2, #32
	ldr	r1, .L1217+12
	ldr	r0, .L1217+20
	add	r1, r1, r3, asl #5
	bl	ftl_memcpy
	mov	r0, r4
	mov	r1, r9
	mov	r2, #32
	bl	ftl_memcpy
	ldrh	r0, [r4, #10]
	bl	FlashBlockAlignInit
	b	.L1206
.L1211:
	mvn	r5, #0
.L1206:
	mov	r0, r5
	ldmfd	sp!, {r3, r4, r5, r6, r7, r8, r9, pc}
.L1218:
	.align	2
.L1217:
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
	bne	.L1220
	cmp	r3, #0
	beq	.L1221
	mov	r0, r2
	mov	r1, r3
	mov	r2, #1024
	bl	ftl_memcpy
.L1221:
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
.L1220:
	cmp	r3, #0
	beq	.L1224
	mov	r1, r2
	mov	r0, r3
	mov	r2, #1024
	bl	ftl_memcpy
.L1224:
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
.L1237:
	cmp	r3, r2
	ldrne	ip, [r1, r3, asl #2]
	strne	ip, [r0, r3, asl #2]
	addne	r3, r3, #1
	bne	.L1237
.L1239:
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
	ldr	r3, .L1242
	ldr	r0, [r3]
	b	jiffies_to_msecs
.L1243:
	.align	2
.L1242:
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
	ldr	ip, .L1248
	cmp	r2, #0
	moveq	r2, r3
	ldr	ip, [ip, #1684]
	add	ip, ip, #4096
	add	r1, ip, r1
	beq	.L1247
	str	lr, [sp, #-4]!
	.save {lr}
	mov	lr, r0
	mov	r0, r1
	mov	r1, lr
	ldr	lr, [sp], #4
	mov	r2, r3
.L1247:
	b	ftl_memcpy
.L1249:
	.align	2
.L1248:
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
	ldr	r3, .L1254
	ldr	r0, [r3, #2428]
	bx	lr
.L1255:
	.align	2
.L1254:
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
.L1260:
	cmp	r4, r8
	beq	.L1269
	cmp	r5, #0
	bne	.L1261
	ldr	r0, .L1270
	mov	r1, r9
	mov	r2, r6
	mov	r3, r4
	bl	printk
.L1261:
	cmp	r7, #4
	ldreq	r0, .L1270+4
	ldreq	r1, [r6, r4, asl #2]
	beq	.L1268
	cmp	r7, #2
	moveq	r3, r4, asl #1
	ldreq	r0, .L1270+4
	ldreqsh	r1, [r6, r3]
	ldrne	r0, .L1270+4
	ldrneb	r1, [r6, r4]	@ zero_extendqisi2
.L1268:
	add	r5, r5, #1
	bl	printk
	cmp	r5, #15
	bls	.L1265
	ldr	r0, .L1270+8
	mov	r5, #0
	ldr	r1, .L1270+12
	bl	printk
.L1265:
	add	r4, r4, #1
	b	.L1260
.L1269:
	ldr	r0, .L1270+8
	ldr	r1, .L1270+12
	ldmfd	sp!, {r3, r4, r5, r6, r7, r8, r9, lr}
	b	printk
.L1271:
	.align	2
.L1270:
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
	ldr	r7, .L1381
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
	bne	.L1273
	mvn	r3, #88
	strb	r3, [r7, #1214]
	ldr	r3, .L1381+4
	mvn	r2, #8
	mov	r5, #7
	strb	r2, [r3, #3405]
	b	.L1334
.L1273:
	cmp	r0, #3
	bne	.L1275
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
	b	.L1375
.L1275:
	cmp	r0, #4
	bne	.L1276
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
.L1375:
	mov	r5, #8
	strb	r3, [r7, #1221]
	mov	r6, r5
	b	.L1274
.L1276:
	cmp	r0, #5
	bne	.L1277
	mov	r3, #56
	mov	r5, #8
	strb	r3, [r7, #1214]
	mov	r3, #57
	strb	r3, [r7, #1215]
	mov	r3, #58
	strb	r3, [r7, #1216]
	mov	r3, #59
	strb	r3, [r7, #1217]
	b	.L1334
.L1277:
	cmp	r0, #6
	bne	.L1278
	mov	r3, #14
	mov	r5, #12
	strb	r3, [r7, #1214]
	mov	r3, #15
	strb	r3, [r7, #1215]
	mov	r3, #16
	strb	r3, [r7, #1216]
	mov	r3, #17
	strb	r3, [r7, #1217]
	b	.L1334
.L1278:
	cmp	r0, #7
	bne	.L1279
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
	b	.L1274
.L1279:
	cmp	r0, #8
	mov	r5, #7
	bne	.L1334
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
	b	.L1274
.L1334:
	mov	r6, #4
.L1274:
	sub	r3, r4, #1
	cmp	r3, #1
	bhi	.L1371
	ldr	r10, .L1381+8
	mov	r9, #0
.L1280:
	ldrb	r1, [r7, #2230]	@ zero_extendqisi2
	uxtb	r2, r9
	ldr	r3, .L1381
	cmp	r1, r2
	bls	.L1287
	add	r2, r3, r2
	ldr	r4, .L1381+12
	ldr	r1, .L1381+16
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
.L1282:
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
	bcc	.L1282
	mov	r2, #0
.L1283:
	add	r0, r10, r2
	mov	r3, #1
.L1284:
	ldrb	r1, [r0, r3, asl #2]	@ zero_extendqisi2
	ldrb	ip, [fp]	@ zero_extendqisi2
	add	r1, r1, ip
	strb	r1, [fp, r3, asl #3]
	add	r3, r3, #1
	cmp	r3, #7
	bne	.L1284
	add	r2, r2, #1
	add	fp, fp, #1
	cmp	r2, #4
	bne	.L1283
	mov	r3, #0
	add	r9, r9, #1
	strb	r3, [r4, #16]
	strb	r3, [r4, #24]
	strb	r3, [r4, #32]
	strb	r3, [r4, #40]
	strb	r3, [r4, #48]
	strb	r3, [r4, #41]
	strb	r3, [r4, #49]
	b	.L1280
.L1371:
	sub	r3, r4, #3
	cmp	r3, #5
	bhi	.L1287
	smulbb	r2, r6, r5
	mov	r3, r2, asl #4
	mov	r2, r2, asr #1
	str	r3, [sp, #36]
	mov	r3, r2, asl #1
	str	r3, [sp, #8]
	mov	r3, #0
.L1379:
	str	r3, [sp, #16]
	ldrb	r3, [sp, #16]	@ zero_extendqisi2
	str	r3, [sp, #12]
	ldr	r3, .L1381
	ldr	r2, [sp, #12]
	ldrb	r3, [r3, #2230]	@ zero_extendqisi2
	cmp	r3, r2
	bhi	.L1332
.L1287:
	ldr	r3, .L1381
	strb	r6, [r3, #1211]
	strb	r5, [r3, #1212]
	add	sp, sp, #44
	@ sp needed
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L1332:
	ldr	r2, [sp, #12]
	ldr	r3, .L1381
	add	r3, r3, r2
	ldrb	r10, [r3, #2232]	@ zero_extendqisi2
	ldr	r3, .L1381
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
	ldreq	r3, .L1381+12
	moveq	r7, #160
	mlaeq	r7, r7, r10, r3
	addeq	r3, r7, #28
	beq	.L1376
.L1289:
	cmp	r4, #8
	beq	.L1291
	ldr	r3, .L1381+12
	add	r7, r3, r10, asl #6
	add	r3, r7, #20
.L1376:
	str	r3, [sp, #20]
	cmp	r4, #4
	add	r3, r8, fp, asl #8
	mov	r2, #54
	str	r2, [r3, #2056]
	bne	.L1292
	add	r9, r8, r9
	mov	r3, #255
	str	r3, [r9, #2052]
	mov	r3, #64
	str	r3, [r9, #2048]
	mov	r3, #204
	b	.L1377
.L1292:
	sub	r2, r4, #5
	cmp	r2, #1
	bhi	.L1294
	ldr	r2, .L1381
	ldrb	r2, [r2, #1214]	@ zero_extendqisi2
	str	r2, [r3, #2052]
	mov	r2, #82
	str	r2, [r3, #2048]
	b	.L1293
.L1294:
	cmp	r4, #7
	bne	.L1293
	add	r9, r8, r9
	mov	r3, #174
	str	r3, [r9, #2052]
	mov	r3, #0
	str	r3, [r9, #2048]
	mov	r3, #176
.L1377:
	str	r3, [r9, #2052]
	mov	r3, #77
	str	r3, [r9, #2048]
.L1293:
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
.L1333:
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
	bne	.L1297
	cmp	r4, #7
	movne	ip, #2
	moveq	ip, #32
.L1297:
	ldr	r3, .L1381+20
	add	r0, r8, fp, asl #8
	ldr	r3, [r3, #1688]
	mov	r1, r3
.L1298:
	ldr	r2, [r0, #2048]
	strb	r2, [r1], #1
	rsb	r2, r3, r1
	uxtb	r2, r2
	cmp	r2, ip
	bcc	.L1298
	ldr	r2, [sp, #4]
	cmp	r2, #0
	beq	.L1299
	mov	r2, #0
.L1301:
	ldrb	ip, [r3, r2, asl #2]	@ zero_extendqisi2
	uxtb	r1, r2
	cmp	ip, #50
	beq	.L1300
	add	ip, r3, r2, asl #2
	ldrb	ip, [ip, #1]	@ zero_extendqisi2
	cmp	ip, #5
	beq	.L1300
	add	r2, r2, #1
	cmp	r2, #8
	bne	.L1301
	b	.L1302
.L1300:
	cmp	r1, #6
	bls	.L1303
.L1302:
	ldr	r0, .L1381+24
	mov	r1, #0
	bl	printk
.L1304:
	b	.L1304
.L1299:
	cmp	r4, #7
	bne	.L1305
	ldr	r2, [sp, #4]
.L1307:
	ldrb	ip, [r3, r2, asl #2]	@ zero_extendqisi2
	uxtb	r1, r2
	cmp	ip, #12
	beq	.L1306
	add	ip, r3, r2, asl #2
	ldrb	ip, [ip, #1]	@ zero_extendqisi2
	cmp	ip, #10
	beq	.L1306
	add	r2, r2, #1
	cmp	r2, #8
	bne	.L1307
	b	.L1308
.L1306:
	cmp	r1, #7
	bne	.L1303
.L1308:
	ldr	r0, .L1381+24
	mov	r1, #0
	bl	printk
.L1309:
	b	.L1309
.L1305:
	cmp	r4, #6
	bne	.L1303
	sub	r2, r3, #1
	add	r3, r3, #7
.L1310:
	ldrb	r1, [r2, #1]!	@ zero_extendqisi2
	cmp	r1, #12
	beq	.L1303
	ldrb	r1, [r2, #8]	@ zero_extendqisi2
	cmp	r1, #4
	beq	.L1303
	cmp	r2, r3
	bne	.L1310
	ldr	r0, .L1381+24
	mov	r1, #0
	bl	printk
.L1312:
	b	.L1312
.L1303:
	ldr	r1, .L1381+20
	ldr	ip, [sp, #36]
	ldr	r2, [r1, #1688]
	add	ip, r2, ip
	mov	r3, r2
.L1313:
	cmp	r3, ip
	ldrne	lr, [r0, #2048]
	strneb	lr, [r3], #1
	bne	.L1313
.L1380:
	ldr	r3, [r1, #1688]
	mov	r0, #8
	ldr	r1, [sp, #8]
	add	r1, r3, r1
	str	r1, [sp, #28]
.L1316:
	ldr	ip, [sp, #8]
	add	lr, r1, ip
.L1315:
	ldrh	ip, [r1]
	mvn	ip, ip
	strh	ip, [r1], #2	@ movhi
	cmp	r1, lr
	bne	.L1315
	ldr	ip, [sp, #8]
	subs	r0, r0, #1
	add	r1, r1, ip
	bne	.L1316
.L1317:
	mov	ip, #0
	mov	r0, ip
.L1320:
	mov	r1, #1
	mov	lr, #0
	mov	r1, r1, asl r0
	mov	r7, #16
	str	r7, [sp, #32]
	mov	r7, lr
.L1318:
	ldrh	r9, [r3, lr]
	and	r9, r9, r1
	cmp	r9, r1
	addeq	r7, r7, #1
	ldr	r9, [sp, #8]
	add	lr, lr, r9
	ldr	r9, [sp, #32]
	subs	r9, r9, #1
	str	r9, [sp, #32]
	bne	.L1318
	cmp	r7, #8
	add	r0, r0, #1
	orrhi	ip, ip, r1
	uxthhi	ip, ip
	cmp	r0, #16
	bne	.L1320
	ldr	r1, [sp, #28]
	strh	ip, [r3], #2	@ movhi
	cmp	r3, r1
	bne	.L1317
	ldr	r3, .L1381+20
	ldr	r1, [r3, #1688]
	mov	r3, #0
	sub	r0, r1, #4
	add	ip, r1, #28
.L1322:
	ldr	lr, [r0, #4]!
	cmp	lr, #0
	addeq	r3, r3, #1
	cmp	r0, ip
	bne	.L1322
	cmp	r3, #7
	ble	.L1323
	ldr	r0, .L1381+28
	mov	r2, #1
	mov	r3, #1024
	bl	rknand_print_hex
	ldr	r0, .L1381+24
	mov	r1, #0
	bl	printk
.L1324:
	b	.L1324
.L1323:
	cmp	r4, #6
	moveq	r0, #4
	beq	.L1325
	cmp	r4, #7
	moveq	r0, #10
	beq	.L1325
	ldr	r3, [sp, #4]
	cmp	r3, #0
	moveq	r0, #8
	movne	r0, #5
.L1325:
	sub	r9, r6, #1
	ldr	r1, [sp, #20]
	mov	ip, #0
	uxtb	r9, r9
	add	r9, r9, #1
.L1326:
	mov	r7, r1
	mov	r3, r2
.L1327:
	ldrb	lr, [r3], #1	@ zero_extendqisi2
	strb	lr, [r7], #1
	rsb	lr, r2, r3
	uxtb	lr, lr
	cmp	lr, r6
	bcc	.L1327
	add	ip, ip, #1
	add	r2, r2, r9
	cmp	ip, r5
	add	r1, r1, r0
	blt	.L1326
	add	r7, r8, fp, asl #8
	mov	r3, #255
	mov	r0, r10
	str	r3, [r7, #2056]
	bl	NandcWaitFlashReady
	ldr	r3, [sp, #24]
	cmp	r3, #0
	beq	.L1329
	mov	r3, #54
	str	r3, [r7, #2056]
	ldr	r3, .L1381
	mvn	r1, #0
	ldr	r0, [sp, #12]
	ldrb	r3, [r3, #1214]	@ zero_extendqisi2
	str	r3, [r7, #2052]
	mov	r3, #0
	str	r3, [r7, #2048]
	mov	r3, #22
	str	r3, [r7, #2056]
	bl	FlashReadCmd
	b	.L1330
.L1329:
	ldr	r3, [sp, #4]
	cmp	r3, #0
	movne	r3, #190
	moveq	r3, #56
	str	r3, [r7, #2056]
.L1330:
	mov	r0, r10
	bl	NandcWaitFlashReady
	ldr	r3, [sp, #16]
	add	r3, r3, #1
	b	.L1379
.L1291:
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
	ldr	r3, .L1381+32
	str	r3, [sp, #20]
	b	.L1333
.L1382:
	.align	2
.L1381:
	.word	.LANCHOR0
	.word	.LANCHOR1
	.word	.LANCHOR1+3388
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
	bhi	.L1385
	b	HynixGetReadRetryDefault
.L1385:
	cmp	r3, #49
	bne	.L1386
	ldr	r2, .L1397
	ldr	r0, .L1397+4
	ldr	r1, .L1397+8
	strb	r3, [r2, #1210]
	mov	r3, #4
	strb	r3, [r2, #1211]
	mov	r3, #15
	strb	r3, [r2, #1212]
	mov	r2, #64
	b	.L1395
.L1386:
	sub	r2, r3, #65
	cmp	r3, #33
	cmpne	r2, #1
	ldrls	r2, .L1397
	strlsb	r3, [r2, #1210]
	movls	r3, #4
	bls	.L1396
.L1387:
	cmp	r3, #34
	cmpne	r3, #67
	bne	.L1388
	ldr	r2, .L1397
	strb	r3, [r2, #1210]
	mov	r3, #5
.L1396:
	strb	r3, [r2, #1211]
	mov	r3, #7
	ldr	r0, .L1397+4
	strb	r3, [r2, #1212]
	mov	r2, #45
	ldr	r1, .L1397+12
	b	.L1395
.L1388:
	cmp	r3, #35
	cmpne	r3, #68
	bxne	lr
	ldr	r2, .L1397
	ldr	r0, .L1397+4
	ldr	r1, .L1397+16
	strb	r3, [r2, #1210]
	mov	r3, #5
	strb	r3, [r2, #1211]
	mov	r3, #17
	strb	r3, [r2, #1212]
	mov	r2, #95
.L1395:
	b	ftl_memcpy
.L1398:
	.align	2
.L1397:
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
	ldr	r5, .L1439
	add	r0, r5, r0, asl #3
	ldr	r3, [r5, #2260]
	ldr	r4, [r0, #12]
	cmp	r3, #3
	bls	.L1430
	ldr	r3, [r4, #16]
	tst	r3, #4
	beq	.L1430
	ldr	r6, [r4, #16]
	ldr	r3, [r4, #8]
	ubfx	r6, r6, #1, #1
	cmp	r6, #0
	str	r3, [sp]
	movne	r6, #0
	beq	.L1412
.L1402:
	ldr	r2, [r4, #28]
	ldr	r3, [sp]
	ubfx	r2, r2, #16, #5
	ubfx	r3, r3, #22, #6
	cmp	r2, r3
	bge	.L1410
	ldr	r3, [r5, #2260]
	cmp	r3, #5
	bhi	.L1403
.L1406:
	add	r6, r6, #1
	bic	r3, r6, #-16777216
	cmp	r3, #0
	bne	.L1405
	ldr	r2, [r4, #28]
	mov	r1, r6
	ldr	r3, [sp]
	ubfx	r2, r2, #16, #5
	ldr	r0, .L1439+4
	ubfx	r3, r3, #22, #6
	bl	printk
	ldr	r0, .L1439+8
	mov	r1, r4
	mov	r2, #4
	mov	r3, #512
	bl	rknand_print_hex
	b	.L1405
.L1403:
	ldr	r3, [r4]
	str	r3, [sp, #4]
	ldr	r3, [sp, #4]
	tst	r3, #8192
	beq	.L1406
	ldr	r3, [sp, #4]
	tst	r3, #131072
	beq	.L1406
.L1410:
	ldr	r3, [r5, #2296]
	ldr	r4, .L1439
	cmp	r3, #0
	beq	.L1411
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
	b	.L1411
.L1405:
	mov	r0, #1
	mov	r1, #5
	bl	usleep_range
	b	.L1402
.L1414:
	ldr	r3, [r4, #8]
	add	r6, r6, #1
	str	r3, [sp]
	bic	r3, r6, #-16777216
	cmp	r3, #0
	bne	.L1413
	ldr	r2, [sp]
	mov	r1, r6
	ldr	r3, [r4, #28]
	ldr	r0, .L1439+12
	ubfx	r3, r3, #16, #5
	bl	printk
	ldr	r0, .L1439+8
	mov	r1, r4
	mov	r2, #4
	mov	r3, #512
	bl	rknand_print_hex
.L1413:
	mov	r0, #1
	mov	r1, #5
	bl	usleep_range
.L1412:
	ldr	r3, [sp]
	tst	r3, #1048576
	beq	.L1414
	ldr	r3, [r5, #2304]
	cmp	r3, #0
	beq	.L1415
	mov	r0, r4
	bl	NandcSendDumpDataStart
.L1415:
	ldr	r3, [r5, #2296]
	ldr	r6, .L1439
	cmp	r3, #0
	beq	.L1416
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
.L1416:
	ldr	r3, [r5, #2304]
	cmp	r3, #0
	beq	.L1411
	mov	r0, r4
	bl	NandcSendDumpDataDone
.L1411:
	mov	r3, #0
	str	r3, [r5, #2296]
	b	.L1399
.L1430:
	ldr	r3, [r4, #8]
	str	r3, [sp]
	ldr	r3, [sp]
	tst	r3, #1048576
	beq	.L1430
.L1399:
	add	sp, sp, #8
	@ sp needed
	ldmfd	sp!, {r4, r5, r6, pc}
.L1440:
	.align	2
.L1439:
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
	ldr	r6, .L1489
	tst	r10, #63
	.pad #92
	sub	sp, sp, #92
	mov	r9, r0
	add	r3, r6, r0, asl #3
	mov	r5, r1
	mov	r7, r2
	ldr	r8, [r3, #12]
	bne	.L1442
	ldr	r3, [sp, #128]
	cmp	r3, #0
	bne	.L1443
	add	r0, sp, #24
	mov	r1, #255
	mov	r2, #64
	bl	ftl_memset
	add	r3, sp, #24
	str	r3, [sp, #128]
.L1443:
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
	bne	.L1444
	ldr	r3, [r6, #2308]
	mov	r0, r7, lsr #1
	mov	r2, r5
	mov	r1, r5
	cmp	r3, #25
	ldr	r3, [sp, #128]
	movcc	lr, #64
	movcs	lr, #128
.L1446:
	cmp	r1, r0
	add	r3, r3, #4
	add	ip, r2, lr
	bcs	.L1487
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
	b	.L1446
.L1487:
	ldr	r0, [r6, #2308]
	mov	r2, #0
	ldr	r1, [r6, #2260]
	mov	r7, r7, lsr #2
	mov	fp, r2
.L1448:
	cmp	r2, r7
	bcs	.L1444
	cmp	r0, #0
	beq	.L1444
	add	r3, r2, #8
	ldr	r3, [r8, r3, asl #2]
	str	r3, [sp, #20]
	ldr	r3, [sp, #20]
	tst	r3, #4
	bne	.L1472
	ldr	r4, [sp, #20]
	ubfx	r4, r4, #15, #1
	cmp	r4, #0
	bne	.L1472
	cmp	r1, #5
	bls	.L1450
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
	b	.L1486
.L1450:
	cmp	r1, #3
	bls	.L1452
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
.L1486:
	orr	r4, r3, r4, asl #5
.L1452:
	cmp	fp, r4
	movcc	fp, r4
	b	.L1449
.L1472:
	mvn	fp, #0
.L1449:
	add	r2, r2, #1
	b	.L1448
.L1444:
	mov	r3, #0
	str	r3, [r8, #16]
	b	.L1455
.L1442:
	cmp	r1, #1
	mov	r4, #0
	bne	.L1484
	mov	fp, r4
.L1456:
	cmp	r4, r7
	bcs	.L1488
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
	b	.L1456
.L1488:
	mov	fp, #0
	b	.L1455
.L1484:
	str	r4, [sp]
	mov	r1, r4
	str	r4, [sp, #4]
	mov	r2, #2
	mov	r3, r4
	mov	fp, r4
	bl	NandcXferStart
	str	r10, [sp, #8]
.L1461:
	cmp	r4, r7
	bcs	.L1455
	mov	r0, r9
	mov	r1, r5
	bl	NandcXferComp
	ldr	r3, [r8, #32]
	add	ip, r4, #2
	cmp	ip, r7
	str	r3, [sp, #20]
	bcs	.L1462
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
.L1462:
	ldr	r3, [sp, #20]
	tst	r3, #4
	mvnne	fp, #0
	bne	.L1463
	ldr	r2, [sp, #20]
	ldr	r3, [sp, #20]
	ubfx	r2, r2, #3, #5
	ubfx	r3, r3, #27, #1
	orr	r3, r2, r3, asl #5
	cmp	fp, r3
	movcc	fp, r3
.L1463:
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
	b	.L1461
.L1455:
	ldr	r3, [r6, #2260]
	clz	r5, r5
	cmp	r3, #5
	mov	r5, r5, lsr #5
	movls	r5, #0
	cmp	r5, #0
	beq	.L1467
	ldr	r3, [r8]
	and	r2, r3, #139264
	cmp	r2, #139264
	orreq	r3, r3, #131072
	streq	r3, [r8]
	mvneq	fp, #0
.L1467:
	mov	r0, fp
	add	sp, sp, #92
	@ sp needed
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L1490:
	.align	2
.L1489:
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
	ldr	r3, .L1494
	subs	r4, r0, #0
	mov	r6, r1
	mov	r7, r2
	ldrb	r5, [r3, #481]	@ zero_extendqisi2
	bne	.L1492
	ldr	r2, .L1494+4
	ldrb	r3, [r2, #1]	@ zero_extendqisi2
	ldr	r2, [r2, #4]
	mul	r2, r2, r3
	cmp	r1, r2
	movcc	r5, #4
.L1492:
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
.L1495:
	.align	2
.L1494:
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
	ldr	r4, .L1523
	.pad #20
	sub	sp, sp, #20
	mov	r10, r0
	mov	fp, r2
	str	r1, [sp]
	ldr	r3, [r4, #80]
	ldr	r3, [r3, #304]
	str	r3, [sp, #8]
	ldr	r3, [r4, #2260]
	cmp	r3, #8
	ldr	r3, [sp, #56]
	movcc	r9, #6
	movcs	r9, #12
	cmp	r3, #0
	moveq	r6, #1024
	beq	.L1498
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
	bne	.L1499
.L1508:
	mvn	r6, #0
	b	.L1500
.L1499:
	mov	r2, r6
	ldr	r0, .L1523+4
	ldr	r1, [sp]
	bl	printk
	cmp	r6, #9
	addls	r4, r4, r10, asl #3
	ldrls	r3, [r4, #12]
	ldr	r4, .L1523+8
	ldrls	r2, [r3, #3840]
	ldrls	r2, [r3]
	orrls	r2, r2, #131072
	strls	r2, [r3]
	ldr	r3, [r4, #1692]
	add	r3, r3, #1
	str	r3, [r4, #1692]
	cmp	r3, #2048
	movcs	r7, #0
	strcs	r7, [r4, #1692]
	movcs	fp, r7
	bcc	.L1500
.L1498:
	mov	r4, #0
	mvn	r8, #0
	mov	ip, r4
	mov	r5, r4
	str	r4, [sp, #4]
.L1506:
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
	bhi	.L1502
	cmp	r0, #2
	bhi	.L1512
	add	r5, r5, #1
	cmp	r5, #9
	bls	.L1512
	rsb	r4, r5, r9
	mov	r6, r0
	mov	r8, #0
	b	.L1504
.L1502:
	ldr	r3, [sp, #4]
	cmp	r3, r5
	bcs	.L1513
	cmp	r5, #7
	rsb	ip, r5, r4
	bhi	.L1514
	str	r5, [sp, #4]
	b	.L1513
.L1512:
	mov	r8, #0
	mov	r4, r9
	mov	r6, r0
	mov	r7, r8
	mov	fp, r8
	b	.L1503
.L1513:
	mov	r5, #0
.L1503:
	add	r9, r9, #2
	cmp	r9, #69
	bls	.L1506
.L1504:
	ldr	r3, [sp, #4]
	cmp	r3, r5
	movcs	r4, ip
	b	.L1505
.L1514:
	mov	r4, ip
.L1505:
	cmp	r4, #0
	beq	.L1507
	ldr	r0, .L1523+12
	mov	r1, r4
	bl	printk
	uxtb	r0, r4
	bl	NandcSetDdrPara
.L1507:
	cmn	r8, #1
	bne	.L1500
	ldr	r0, .L1523+16
	mov	r1, r10
	ldr	r2, [sp]
	bl	printk
	ldr	r3, [sp, #56]
	cmp	r3, #0
	beq	.L1508
	ldr	r3, [sp, #8]
	ubfx	r0, r3, #8, #8
	bl	NandcSetDdrPara
.L1500:
	mov	r0, r6
	add	sp, sp, #20
	@ sp needed
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L1524:
	.align	2
.L1523:
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
	bne	.L1526
	ldr	r9, .L1546
	ldrb	fp, [r9, #8]	@ zero_extendqisi2
	mov	r10, r9
	cmp	fp, #0
	bne	.L1527
.L1529:
	ldrb	r3, [r10, #2252]	@ zero_extendqisi2
	ldr	r9, .L1546
	cmp	r3, #0
	beq	.L1526
	b	.L1545
.L1527:
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
	beq	.L1529
	b	.L1526
.L1545:
	ldr	r3, [r9, #80]
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
	beq	.L1530
	ldrb	r3, [r9, #2312]	@ zero_extendqisi2
	cmp	r0, r3, lsr #1
	bls	.L1526
.L1530:
	ubfx	r0, r10, #8, #8
	bl	NandcSetDdrPara
.L1526:
	ldr	r9, .L1546+4
	ldr	ip, [r9, #1696]
	adds	r3, ip, #0
	movne	r3, #1
	cmn	r4, #1
	movne	r3, #0
	cmp	r3, #0
	beq	.L1531
	mov	r1, r6
	mov	r2, r8
	mov	r3, r7
	mov	r0, r5
	blx	ip
	mov	r2, r5
	mov	r3, r6
	mov	r4, r0
	ldr	r0, .L1546+8
	mov	r1, r4
	bl	printk
	cmn	r4, #1
	bne	.L1531
	ldr	r3, .L1546
	ldrb	r3, [r3, #144]	@ zero_extendqisi2
	cmp	r3, #0
	beq	.L1531
	mov	r0, r5
	bl	flash_enter_slc_mode
	ldr	ip, [r9, #1696]
	mov	r0, r5
	mov	r1, r6
	mov	r2, r8
	mov	r3, r7
	blx	ip
	mov	r4, r0
	mov	r0, r5
	bl	flash_exit_slc_mode
.L1531:
	mov	r0, r4
	add	sp, sp, #12
	@ sp needed
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L1547:
	.align	2
.L1546:
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
	ldr	r5, .L1558
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
	bne	.L1549
	ldrb	r2, [r5, #2229]	@ zero_extendqisi2
	tst	r2, #1
	beq	.L1549
	mov	r0, #1
	bl	FlashSetInterfaceMode
	mov	r0, #1
	bl	NandcSetMode
	strb	r4, [r5, #2252]
	b	.L1550
.L1549:
	mov	r2, #1
	strb	r2, [r3, #2252]
.L1550:
	mov	r0, #0
	add	sp, sp, #8
	@ sp needed
	ldmfd	sp!, {r4, r5, r6, r7, r8, pc}
.L1559:
	.align	2
.L1558:
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
	ldr	r3, .L1576
	mov	r4, #0
	ldr	r7, .L1576+4
	mov	r9, #4
	ldr	r5, .L1576+8
	mvn	r8, #0
	ldr	r0, [r3]	@ unaligned
	ldr	r3, [r7, #4]
	str	r4, [r5, #1704]
	mov	r6, r5
	str	r0, [sp, #12]	@ unaligned
	mov	r0, r4
	str	r3, [sp, #4]
	ldr	r3, [r5, #1688]
	str	r3, [r5, #1700]
	bl	flash_enter_slc_mode
.L1561:
	add	fp, r4, #1
	mov	r10, #0
.L1563:
	add	r3, sp, #12
	ldrb	r0, [r3, r10]	@ zero_extendqisi2
	bl	FlashBchSel
	mov	r0, #0
	mov	r1, r4
	ldr	r2, [r5, #1688]
	mov	r3, r0
	bl	FlashReadRawPage
	cmn	r0, #1
	bne	.L1562
	mov	r0, #0
	mov	r1, fp
	ldr	r2, [r6, #1688]
	mov	r3, r0
	bl	FlashReadRawPage
	cmn	r0, #1
	bne	.L1562
	add	r10, r10, #1
	cmp	r10, #4
	beq	.L1564
	b	.L1563
.L1565:
	add	r0, fp, #12
	movw	r1, #2036
	bl	js_hash
	ldr	r3, [fp, #8]
	cmp	r3, r0
	mvnne	r8, #0
	bne	.L1564
	ldr	r8, .L1576+12
	add	r1, fp, #160
	mov	r2, #32
	mov	r0, r8
	bl	ftl_memcpy
	ldr	r1, [r6, #1700]
	mov	r2, #32
	ldr	r0, .L1576+16
	add	r1, r1, #192
	bl	ftl_memcpy
	ldr	r1, [r6, #1700]
	mov	r2, #852
	ldr	r0, .L1576+20
	add	r1, r1, #224
	bl	ftl_memcpy
	ldrh	r0, [r8, #10]
	bl	FlashBlockAlignInit
	ldr	r8, [r6, #1700]
	str	r4, [r6, #1704]
	mov	r0, r4
	ldr	r1, [r7, #4]
	ldr	r3, [r8, #1076]
	strb	r3, [r7, #2252]
	bl	__aeabi_uidiv
	add	r0, r0, #1
	cmp	r0, #1
	strhi	r0, [r6, #1708]
	movls	r3, #2
	strls	r3, [r6, #1708]
	ldrh	r3, [r8, #14]
	mov	r8, #0
	strb	r3, [r5, #1712]
.L1564:
	ldr	r3, [sp, #4]
	subs	r9, r9, #1
	add	r4, r4, r3
	bne	.L1561
	mov	r0, r9
.L1575:
	bl	flash_exit_slc_mode
	mov	r0, r8
	add	sp, sp, #20
	@ sp needed
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L1562:
	ldr	fp, [r5, #1700]
	ldr	r2, .L1576+24
	ldr	r3, [fp]
	cmp	r3, r2
	bne	.L1564
	cmp	r8, #0
	bne	.L1565
	mov	r0, r4
	ldr	r1, [r7, #4]
	bl	__aeabi_uidiv
	ldr	r3, .L1576+8
	add	r0, r0, #1
	str	r0, [r3, #1708]
	mov	r0, r8
	b	.L1575
.L1577:
	.align	2
.L1576:
	.word	.LANCHOR3+11
	.word	.LANCHOR0
	.word	.LANCHOR2
	.word	.LANCHOR1+472
	.word	.LANCHOR0+48
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
	ldr	r4, .L1607
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
	bls	.L1579
	ldrb	r5, [r4, #2252]	@ zero_extendqisi2
	cmp	r5, #0
	streq	r5, [sp, #4]
	beq	.L1580
	mov	r0, #0
	str	r2, [sp, #20]
	bl	NandcSetDdrMode
	mov	r3, #1
	ldr	r2, [sp, #20]
	str	r3, [sp, #4]
.L1580:
	ldr	r3, [sp, #12]
	mov	r1, #92
	add	r3, r6, r3
	str	r1, [r3, #2056]
	mov	r1, #197
	str	r1, [r3, #2056]
.L1579:
	mov	r8, #1
	mvn	r10, #0
	mov	r3, r2, asl #8
	str	r3, [sp, #20]
.L1581:
	ldr	r3, .L1607+4
	ldrb	r3, [r3, #1713]	@ zero_extendqisi2
	add	r3, r3, #1
	cmp	r8, r3
	bcs	.L1606
	ldrb	r3, [r4, #1208]	@ zero_extendqisi2
	mov	r0, r7
	uxtb	r1, r8
	sub	r3, r3, #67
	cmp	r3, #1
	bhi	.L1582
	bl	SandiskSetRRPara
	b	.L1583
.L1582:
	bl	ToshibaSetRRPara
.L1583:
	ldrb	r3, [r4, #1208]	@ zero_extendqisi2
	cmp	r3, #34
	bne	.L1584
	ldr	r3, .L1607+4
	ldrb	r3, [r3, #1713]	@ zero_extendqisi2
	sub	r3, r3, #3
	cmp	r8, r3
	ldreq	r3, [sp, #20]
	moveq	r2, #179
	addeq	r3, r6, r3
	streq	r2, [r3, #2056]
.L1584:
	ldr	r3, [sp, #12]
	mov	r2, #38
	add	r3, r6, r3
	str	r2, [r3, #2056]
	mov	r2, #93
	str	r2, [r3, #2056]
	ldr	r3, [sp, #4]
	cmp	r3, #0
	beq	.L1585
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
	b	.L1586
.L1585:
	mov	r0, r9
	ldr	r1, [sp, #16]
	ldr	r2, [sp, #8]
	mov	r3, fp
	bl	FlashReadRawPage
	mov	r5, r0
.L1586:
	cmn	r5, #1
	beq	.L1587
	ldrb	r2, [r4, #2312]	@ zero_extendqisi2
	cmn	r10, #1
	moveq	r10, r5
	add	r2, r2, r2, asl #1
	cmp	r5, r2, asr #2
	bcc	.L1589
	mov	fp, #0
	str	fp, [sp, #8]
.L1587:
	add	r8, r8, #1
	b	.L1581
.L1606:
	mov	r5, r10
.L1589:
	ldrb	r2, [r4, #1208]	@ zero_extendqisi2
	mov	r0, r7
	mov	r1, #0
	sub	r2, r2, #67
	cmp	r2, #1
	bhi	.L1591
	bl	SandiskSetRRPara
	b	.L1592
.L1591:
	bl	ToshibaSetRRPara
.L1592:
	ldr	r3, [sp, #12]
	mov	r2, #255
	add	r6, r6, r3
	str	r2, [r6, #2056]
	ldrb	r2, [r4, #2312]	@ zero_extendqisi2
	add	r2, r2, r2, asl #1
	cmp	r5, r2, asr #2
	bcc	.L1593
	cmn	r5, #1
	movne	r5, #256
.L1593:
	mov	r0, r9
	bl	NandcWaitFlashReady
	ldr	r3, [sp, #4]
	cmp	r3, #0
	beq	.L1594
	mov	r0, #4
	bl	NandcSetDdrMode
.L1594:
	mov	r0, r5
	add	sp, sp, #28
	@ sp needed
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L1608:
	.align	2
.L1607:
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
	ldr	r5, .L1623
	mov	r10, r2
	mov	r8, r3
	mov	fp, r1
	bl	NandcWaitFlashReady
	add	r2, r5, r9, asl #3
	ldr	ip, .L1623+4
	mov	r7, #1
	ldrb	r3, [r2, #16]	@ zero_extendqisi2
	mvn	r4, #0
	ldr	r6, [r2, #12]
	add	r3, r3, #8
	add	r6, r6, r3, asl #8
.L1610:
	ldrb	r3, [ip, #1713]	@ zero_extendqisi2
	add	r3, r3, #1
	cmp	r7, r3
	bcs	.L1613
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
	beq	.L1611
	ldrb	r3, [r5, #2312]	@ zero_extendqisi2
	cmn	r4, #1
	moveq	r4, r0
	add	r3, r3, r3, asl #1
	cmp	r0, r3, asr #2
	bcc	.L1616
	mov	r8, #0
	mov	r10, r8
.L1611:
	add	r7, r7, #1
	b	.L1610
.L1616:
	mov	r4, r0
.L1613:
	mov	r0, r6
	mov	r1, #0
	bl	SamsungSetRRPara
	ldrb	r3, [r5, #2312]	@ zero_extendqisi2
	add	r3, r3, r3, asl #1
	cmp	r4, r3, asr #2
	bcc	.L1615
	cmn	r4, #1
	movne	r4, #256
.L1615:
	mov	r0, r4
	add	sp, sp, #12
	@ sp needed
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L1624:
	.align	2
.L1623:
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
.L1627:
	stmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.save {r4, r5, r6, r7, r8, r9, r10, fp, lr}
	mov	r8, r3
	ldr	r3, .L1651
	mov	fp, r2
	.pad #36
	sub	sp, sp, #36
	mov	r6, r0
	mov	r10, #0
	ldrb	r5, [r3, #2312]	@ zero_extendqisi2
	ldrb	r3, [r3, #144]	@ zero_extendqisi2
	str	r1, [sp, #20]
	cmp	r3, #0
	addeq	r5, r5, r5, asl #1
	ldrne	r2, .L1651+4
	ubfxeq	r5, r5, #2, #8
	smullne	r2, r3, r5, r2
	uxtbne	r5, r3
	ldr	r3, .L1651
	add	r3, r3, r0, asl #3
	str	r3, [sp, #24]
.L1637:
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
.L1628:
	ldr	r3, .L1651+8
	ldrb	r3, [r3, #1713]	@ zero_extendqisi2
	cmp	r9, r3
	bcs	.L1631
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
	beq	.L1629
	cmn	r4, #1
	ldr	ip, [sp, #28]
	moveq	r4, r0
	cmp	r0, r5
	bcc	.L1639
	mov	r8, ip
	mov	fp, ip
.L1629:
	ldr	r9, [sp, #8]
	b	.L1628
.L1639:
	mov	r4, r0
	mov	r8, ip
	mov	fp, ip
.L1631:
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
	bcc	.L1633
	cmn	r4, #1
	movne	r4, #256
.L1633:
	cmn	r4, #1
	movne	r7, #0
	moveq	r7, #1
	cmp	r4, #256
	movne	r1, r7
	orreq	r1, r7, #1
	cmp	r1, #0
	beq	.L1634
	str	r4, [sp]
	mov	r1, r9
	ldr	r0, .L1651+12
	mov	r3, r9
	ldr	r2, [sp, #20]
	bl	printk
	cmp	r10, #0
	bne	.L1635
	ldr	r3, .L1651
	ldrb	r3, [r3, #144]	@ zero_extendqisi2
	cmp	r3, #0
	moveq	r7, #0
	andne	r7, r7, #1
	cmp	r7, #0
	beq	.L1644
	mov	r0, r6
	mov	r1, #3
	bl	micron_auto_read_calibration_config
	mov	r10, #1
	b	.L1637
.L1635:
	mov	r0, r6
	mov	r1, #0
	bl	micron_auto_read_calibration_config
	cmn	r4, #1
	movne	r4, #256
	b	.L1644
.L1634:
	cmp	r10, #0
	beq	.L1644
	mov	r0, r6
	mov	r4, #256
	bl	micron_auto_read_calibration_config
.L1644:
	mov	r0, r4
	add	sp, sp, #36
	@ sp needed
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L1652:
	.align	2
.L1651:
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
	ldr	r5, .L1671
	mov	r10, r2
	mov	r7, r0
	mov	fp, r1
	add	r2, r5, r0
	mvn	r6, #0
	ldr	r3, [r5, #44]
	ldrb	r4, [r2, #1222]	@ zero_extendqisi2
	ldrb	r9, [r5, #1212]	@ zero_extendqisi2
	ldrb	r3, [r3, #19]	@ zero_extendqisi2
	sub	r3, r3, #7
	cmp	r3, #1
	ldrlsb	r4, [r2, #1230]	@ zero_extendqisi2
	bl	NandcWaitFlashReady
	mov	ip, #0
.L1655:
	cmp	ip, r9
	bcs	.L1659
	add	r4, r4, #1
	mov	r0, r7
	ldrb	r1, [r5, #1211]	@ zero_extendqisi2
	uxtb	r4, r4
	ldr	r2, .L1671+4
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
	beq	.L1657
	ldrb	r3, [r5, #2312]	@ zero_extendqisi2
	cmn	r6, #1
	moveq	r6, r0
	add	r3, r3, r3, asl #1
	cmp	r0, r3, asr #2
	bcc	.L1664
	mov	r8, #0
	mov	r10, r8
.L1657:
	add	ip, ip, #1
	b	.L1655
.L1664:
	mov	r6, r0
.L1659:
	ldr	r3, [r5, #44]
	add	r7, r5, r7
	ldrb	r3, [r3, #19]	@ zero_extendqisi2
	sub	r3, r3, #7
	cmp	r3, #1
	ldrb	r3, [r5, #2312]	@ zero_extendqisi2
	strlsb	r4, [r7, #1230]
	strhib	r4, [r7, #1222]
	add	r3, r3, r3, asl #1
	cmp	r6, r3, asr #2
	bcc	.L1663
	cmn	r6, #1
	movne	r6, #256
.L1663:
	mov	r0, r6
	add	sp, sp, #12
	@ sp needed
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L1672:
	.align	2
.L1671:
	.word	.LANCHOR0
	.word	.LANCHOR0+1214
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
	ldr	r3, .L1703
	add	r2, r3, r10, asl #3
	ldr	r4, [r2, #12]
	ldrb	r2, [r2, #16]	@ zero_extendqisi2
	str	r2, [sp, #8]
	ldrb	r2, [r3, #2228]	@ zero_extendqisi2
	str	r3, [sp, #16]
	cmp	r2, #0
	bne	.L1674
	ldr	r3, [sp, #8]
	mvn	r5, #0
	mov	r6, #1
	mov	r9, r3, asl #8
	add	r7, r4, r9
.L1678:
	mov	r3, #239
	str	r3, [r7, #2056]
	mov	r3, #141
	str	r3, [r7, #2052]
	ldr	r3, .L1703+4
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
	beq	.L1675
	ldr	r3, [sp, #16]
	cmn	r5, #1
	moveq	r5, r0
	ldrb	r3, [r3, #2312]	@ zero_extendqisi2
	add	r3, r3, r3, asl #1
	cmp	r0, r3, asr #2
	bcc	.L1686
	ldr	ip, [sp, #20]
	mov	r8, ip
	mov	fp, ip
.L1675:
	add	r6, r6, #1
	cmp	r6, #26
	bne	.L1678
	b	.L1677
.L1686:
	mov	r5, r0
.L1677:
	add	r9, r4, r9
	mov	r3, #239
	str	r3, [r9, #2056]
	ldr	r3, [sp, #8]
	add	r4, r4, r3, asl #8
	mov	r3, #141
	b	.L1702
.L1674:
	ldr	r3, [sp, #8]
	mvn	r5, #0
	ldr	r7, .L1703+8
	mov	r6, #1
	mov	ip, r3, asl #8
	add	r9, r4, ip
.L1683:
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
	beq	.L1680
	ldr	r3, [sp, #16]
	cmn	r5, #1
	moveq	r5, r0
	ldrb	r3, [r3, #2312]	@ zero_extendqisi2
	add	r3, r3, r3, asl #1
	cmp	r0, r3, asr #2
	bcc	.L1687
	mov	r8, #0
	mov	fp, r8
.L1680:
	add	r6, r6, #1
	add	r7, r7, #4
	cmp	r6, #26
	bne	.L1683
	b	.L1682
.L1687:
	mov	r5, r0
.L1682:
	add	ip, r4, ip
	mov	r3, #239
	str	r3, [ip, #2056]
	ldr	r3, [sp, #8]
	add	r4, r4, r3, asl #8
	mov	r3, #137
.L1702:
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
	bcc	.L1684
	cmn	r5, #1
	movne	r5, #256
.L1684:
	cmn	r5, #1
	cmpne	r5, #256
	bne	.L1685
	str	r5, [sp]
	mov	r1, r6
	ldr	r0, .L1703+12
	mov	r3, r6
	ldr	r2, [sp, #12]
	bl	printk
.L1685:
	mov	r0, r10
	bl	NandcWaitFlashReady
	mov	r0, r5
	add	sp, sp, #28
	@ sp needed
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L1704:
	.align	2
.L1703:
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
	ldr	r3, .L1709
	subs	r4, r0, #0
	mov	r5, r1
	mov	r7, r2
	ldrb	r6, [r3, #481]	@ zero_extendqisi2
	bne	.L1706
	ldr	r2, .L1709+4
	ldrb	r3, [r2, #1]	@ zero_extendqisi2
	ldr	r1, [r2, #4]
	mul	r1, r1, r3
	cmp	r5, r1
	bcs	.L1706
	ldrb	r3, [r2]	@ zero_extendqisi2
	cmp	r3, #0
	movne	r6, #4
.L1706:
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
.L1710:
	.align	2
.L1709:
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
	ldr	r4, .L1725
	ldr	r5, .L1725+4
	ldr	r8, .L1725+8
	ldr	r3, [r4, #1688]
	mov	fp, r4
	ldrb	r0, [r4, #1714]	@ zero_extendqisi2
	mov	r10, r5
	str	r3, [r4, #1700]
	bl	FlashBchSel
	mov	r1, #0
	mov	r2, #2048
	ldr	r0, [r4, #1688]
	bl	ftl_memset
	ldr	r3, [r4, #1700]
	ldr	r1, .L1725+12
	mov	r2, #32
	str	r8, [r3]
	ldr	r0, [r4, #1700]
	ldrb	r3, [r5, #2230]	@ zero_extendqisi2
	add	r0, r0, #16
	strh	r3, [r0, #-4]	@ movhi
	ldrb	r3, [r5, #1]	@ zero_extendqisi2
	strh	r3, [r0, #-2]	@ movhi
	ldrb	r3, [r5, #2252]	@ zero_extendqisi2
	str	r3, [r0, #1060]
	bl	ftl_memcpy
	ldr	r0, [r4, #1700]
	ldr	r1, .L1725+16
	mov	r2, #8
	add	r0, r0, #80
	bl	ftl_memcpy
	ldr	r0, [r4, #1700]
	ldr	r1, .L1725+20
	mov	r2, #32
	add	r0, r0, #96
	bl	ftl_memcpy
	ldr	r0, [r4, #1700]
	ldr	r1, .L1725+24
	mov	r2, #32
	add	r0, r0, #160
	bl	ftl_memcpy
	ldr	r0, [r4, #1700]
	add	r1, r5, #48
	mov	r2, #32
	add	r0, r0, #192
	bl	ftl_memcpy
	ldr	r0, [r4, #1700]
	mov	r2, #852
	ldr	r1, .L1725+28
	add	r0, r0, #224
	bl	ftl_memcpy
	ldr	r6, [r4, #1700]
	movw	r1, #2036
	add	r0, r6, #12
	bl	js_hash
	movw	r3, #1592
	str	r3, [r6, #4]
	ldr	r3, [r4, #1716]
	str	r3, [r4, #1700]
	str	r0, [r6, #8]
	mov	r0, #0
	bl	flash_enter_slc_mode
	mov	r6, #0
	mov	r7, r6
.L1717:
	ldr	r1, [r5, #4]
	mov	r0, #0
	mov	r2, r0
	mul	r1, r1, r7
	bl	FlashEraseBlock
	ldrb	r9, [r5, #144]	@ zero_extendqisi2
	cmp	r9, #0
	beq	.L1712
	mov	r9, #0
.L1713:
	ldr	r1, [r5, #4]
	mov	r0, #0
	ldr	r2, [r4, #1688]
	mov	r3, r0
	mla	r1, r1, r7, r9
	add	r9, r9, #1
	bl	FlashProgPage
	cmp	r9, #10
	bne	.L1713
	b	.L1714
.L1712:
	ldr	r1, [r10, #4]
	mov	r3, r9
	ldr	r2, [r4, #1688]
	mov	r0, r9
	mul	r1, r1, r7
	bl	FlashProgPage
	ldr	r1, [r10, #4]
	mov	r0, r9
	ldr	r2, [r4, #1688]
	mov	r3, r9
	mul	r1, r1, r7
	add	r1, r1, #1
	bl	FlashProgPage
.L1714:
	ldr	r1, [r5, #4]
	mov	r0, #0
	ldr	r2, [r4, #1716]
	mov	r3, r0
	mul	r1, r1, r7
	bl	FlashReadRawPage
	add	r2, r7, #1
	cmn	r0, #1
	beq	.L1715
	ldr	r9, [fp, #1700]
	ldr	r3, [r9]
	cmp	r3, r8
	bne	.L1715
	add	r0, r9, #12
	movw	r1, #2036
	str	r2, [sp, #4]
	bl	js_hash
	ldr	r3, [r9, #8]
	cmp	r3, r0
	ldr	r2, [sp, #4]
	bne	.L1715
	ldr	r3, [r10, #4]
	cmp	r6, #1
	str	r2, [fp, #1708]
	mul	r7, r3, r7
	str	r7, [fp, #1704]
	beq	.L1718
	mov	r6, #1
.L1715:
	cmp	r2, #4
	mov	r7, r2
	bne	.L1717
	b	.L1716
.L1718:
	mov	r6, #2
.L1716:
	mov	r0, #0
	bl	flash_exit_slc_mode
	clz	r0, r6
	mov	r0, r0, lsr #5
	rsb	r0, r0, #0
	add	sp, sp, #12
	@ sp needed
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L1726:
	.align	2
.L1725:
	.word	.LANCHOR2
	.word	.LANCHOR0
	.word	1312902724
	.word	.LANCHOR0+2068
	.word	.LANCHOR0+2232
	.word	.LANCHOR0+1172
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
	ldr	r3, .L1747
	.pad #20
	sub	sp, sp, #20
	ldr	r4, .L1747+4
	ldr	r0, [r3]	@ unaligned
	ldr	r3, [r4, #2248]
	ldrb	r10, [r4, #2312]	@ zero_extendqisi2
	cmp	r3, #0
	str	r0, [sp, #12]	@ unaligned
	beq	.L1728
	mov	r0, #0
	bl	flash_enter_slc_mode
.L1728:
	mov	r0, r9
	mov	r1, #0
	mov	r2, #2048
	mvn	r8, #0
	bl	ftl_memset
	mov	r5, #2
.L1729:
	ldrb	r3, [r4, #1]	@ zero_extendqisi2
	cmp	r5, r3
	bcs	.L1733
	ldr	fp, .L1747+8
	mov	r7, #0
.L1731:
	add	r3, sp, #12
	ldr	r6, .L1747+8
	ldrb	ip, [r7, r3]	@ zero_extendqisi2
	mov	r0, ip
	str	ip, [sp, #4]
	bl	FlashBchSel
	ldr	r1, [r4, #4]
	mov	r0, #0
	ldr	r2, [fp, #1688]
	mov	r3, r0
	mul	r1, r1, r5
	bl	FlashReadRawPage
	cmn	r0, #1
	ldr	ip, [sp, #4]
	bne	.L1730
	add	r7, r7, #1
	cmp	r7, #4
	bne	.L1731
	b	.L1732
.L1730:
	ldr	r3, [r6, #1688]
	ldr	r2, .L1747+12
	ldr	r3, [r3]
	cmp	r3, r2
	bne	.L1732
	mov	r1, ip
	ldr	r0, .L1747+16
	bl	printk
	mov	r2, #2048
	mov	r0, r9
	ldr	r1, [r6, #1688]
	bl	ftl_memcpy
	ldr	r3, [r6, #1688]
	ldr	r2, .L1747+4
	ldr	r3, [r3, #512]
	strb	r3, [r2, #1]
	ldr	r3, [r6, #1708]
	cmp	r3, r5
	bls	.L1736
	str	r5, [r6, #1708]
	bl	FlashSavePhyInfo
	mov	r8, #0
.L1732:
	add	r5, r5, #1
	b	.L1729
.L1736:
	mov	r8, #0
.L1733:
	mov	r0, r10
	bl	FlashBchSel
	ldr	r3, [r4, #2248]
	cmp	r3, #0
	beq	.L1740
	mov	r0, #0
	bl	flash_exit_slc_mode
.L1740:
	mov	r0, r8
	add	sp, sp, #20
	@ sp needed
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L1748:
	.align	2
.L1747:
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
	ldr	r8, .L1848
	ldr	r4, .L1848+4
	mov	r7, #0
	add	r9, r4, #12
	str	r0, [r8, #1688]
	mov	r0, #32768
	bl	ftl_malloc
	str	r0, [r8, #1716]
	mov	r0, #4096
	bl	ftl_malloc
	str	r0, [r8, #1720]
	mov	r0, #32768
	bl	ftl_malloc
	str	r0, [r8, #1724]
	mov	r0, #4096
	bl	ftl_malloc
	mov	r3, #50
	str	r7, [r8, #1708]
	strb	r3, [r4, #1]
	strb	r3, [r8, #1712]
	mov	r3, #128
	strb	r7, [r4, #2252]
	str	r3, [r4, #4]
	mov	r3, #60
	str	r7, [r8, #1692]
	strb	r7, [r4]
	strb	r7, [r8, #1732]
	strb	r3, [r8, #1714]
	str	r0, [r8, #1728]
	mov	r0, r5
	bl	NandcInit
	ldr	r5, .L1848+8
.L1755:
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
	bne	.L1750
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
.L1750:
	mov	r0, fp
	bl	NandcFlashDeCs
	ldrb	r2, [r5]	@ zero_extendqisi2
	sub	r3, r2, #1
	uxtb	r3, r3
	cmp	r3, #253
	bhi	.L1751
	ldrb	r1, [r5, #2]	@ zero_extendqisi2
	ldrb	r3, [r5, #1]	@ zero_extendqisi2
	ldr	r0, .L1848+12
	str	r1, [sp]
	ldrb	r1, [r5, #3]	@ zero_extendqisi2
	str	r1, [sp, #4]
	ldrb	r1, [r5, #4]	@ zero_extendqisi2
	str	r1, [sp, #8]
	ldrb	r1, [r5, #5]	@ zero_extendqisi2
	str	r1, [sp, #12]
	add	r1, r7, #1
	bl	printk
.L1751:
	cmp	r7, #0
	bne	.L1752
	ldrb	r3, [r4, #2068]	@ zero_extendqisi2
	sub	r3, r3, #1
	uxtb	r3, r3
	cmp	r3, #253
	bhi	.L1802
	ldr	r3, .L1848+4
	ldrb	r3, [r3, #2069]	@ zero_extendqisi2
	cmp	r3, #255
	beq	.L1802
.L1752:
	ldrb	r3, [r5]	@ zero_extendqisi2
	add	r7, r7, #1
	add	r5, r5, #8
	cmp	r3, #181
	moveq	r3, #44
	streqb	r3, [r5, #-8]
	cmp	r7, #4
	bne	.L1755
	ldrb	r3, [r4, #2068]	@ zero_extendqisi2
	cmp	r3, #173
	beq	.L1756
	ldr	r3, .L1848+4
	ldr	r0, [r3, #2256]
	bl	NandcSetDdrMode
.L1756:
	mov	r2, #852
	ldr	r0, .L1848+16
	mov	r1, #0
	ldr	r5, .L1848+20
	bl	ftl_memset
	ldr	r2, [r4, #2264]
	ldr	r0, .L1848+24
	cmp	r2, r5
	add	r3, r0, #472
	str	r3, [r4, #44]
	mov	r3, #0
	strb	r3, [r4, #8]
	bne	.L1757
	ldrb	r3, [r0, #491]	@ zero_extendqisi2
	cmp	r3, #50
	ldrne	r3, .L1848+4
	movne	r1, #1
	strne	r1, [r3, #2248]
.L1757:
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
	bne	.L1758
	cmp	r6, #220
	bne	.L1759
	ldr	r3, .L1848+4
	ldrb	r3, [r3, #2071]	@ zero_extendqisi2
	cmp	r3, #149
	bne	.L1759
.L1758:
	mov	lr, #16
	strb	lr, [r4, #1]
	strb	lr, [r8, #1714]
	mov	r1, #1
	ldrb	lr, [r4, #2068]	@ zero_extendqisi2
	strb	r1, [r4]
	cmp	lr, #152
	ldr	r3, .L1848
	strb	lr, [r0, #3417]
	strb	r6, [r0, #3418]
	bne	.L1761
	ldr	r7, .L1848+4
	movw	lr, #2072
	ldrsb	lr, [r7, lr]
	cmp	lr, #0
	strltb	r1, [r3, #1732]
	movge	r1, #24
	strgeb	r1, [r3, #1714]
.L1761:
	movw	r3, #2049
	cmp	r2, r5
	cmpne	r2, r3
	moveq	r3, #16
	streqb	r3, [r8, #1714]
	cmp	ip, #0
	ldrne	r3, .L1848+28
	movne	r2, #2048
	strneh	r2, [r3, #14]	@ movhi
	mvnne	r3, #37
	bne	.L1842
.L1765:
	cmp	r6, #220
	bne	.L1767
	ldr	r3, .L1848+28
	mov	r2, #4096
	strh	r2, [r3, #14]	@ movhi
	mvn	r3, #35
.L1842:
	strb	r3, [r0, #3418]
	b	.L1766
.L1767:
	cmp	r6, #211
	ldreq	r3, .L1848+28
	moveq	r2, #4096
	streqh	r2, [r3, #14]	@ movhi
	moveq	r3, #2
	streqb	r3, [r0, #3429]
.L1766:
	ldr	r1, .L1848+32
	mov	r2, #32
	ldr	r0, .L1848+36
	bl	ftl_memcpy
	ldr	r0, .L1848+40
	mov	r2, #32
	add	r1, r0, #2944
	bl	ftl_memcpy
.L1759:
	ldrb	r3, [r4]	@ zero_extendqisi2
	ldr	r6, .L1848+4
	cmp	r3, #0
	bne	.L1768
	bl	FlashLoadPhyInfoInRam
	cmp	r0, #0
	bne	.L1770
	ldr	r3, [r6, #44]
	ldrh	r3, [r3, #16]
	mov	r3, r3, lsr #8
	tst	r3, #1
	and	r0, r3, #7
	strb	r0, [r6, #2229]
	bne	.L1770
	mov	r3, #1
	strb	r3, [r6, #2252]
	bl	FlashSetInterfaceMode
	ldrb	r0, [r6, #2229]	@ zero_extendqisi2
	bl	NandcSetMode
.L1770:
	ldr	r3, [r4, #44]
	ldr	r6, .L1848+4
	ldrb	r3, [r3, #26]	@ zero_extendqisi2
	strb	r3, [r4, #144]
	bl	FlashLoadPhyInfo
	cmp	r0, #0
	beq	.L1768
	ldr	r3, [r6, #2256]
	cmp	r3, #0
	beq	.L1773
	mov	r0, #1
	bl	FlashSetInterfaceMode
	mov	r0, #1
	b	.L1843
.L1773:
	ldrb	r0, [r6, #2229]	@ zero_extendqisi2
	bl	FlashSetInterfaceMode
	ldrb	r0, [r6, #2229]	@ zero_extendqisi2
.L1843:
	bl	NandcSetMode
	bl	FlashLoadPhyInfo
	cmp	r0, #0
	beq	.L1768
	mov	r0, #1
	ldr	r6, .L1848+4
	bl	FlashSetInterfaceMode
	mov	r0, #1
	bl	NandcSetMode
	ldr	r3, [r4, #44]
	ldr	r0, .L1848+44
	ldrh	r1, [r3, #14]
	bl	printk
	bl	FlashLoadPhyInfoInRam
	cmn	r0, #1
	beq	.L1816
	bl	FlashDieInfoInit
	ldr	r3, [r6, #44]
	ldrb	r0, [r3, #19]	@ zero_extendqisi2
	bl	FlashGetReadRetryDefault
	ldr	r3, .L1848+48
	ldr	r2, [r6, #44]
	ldrh	r3, [r3, #-2]
	ldrb	r1, [r2, #9]	@ zero_extendqisi2
	add	r3, r3, #4080
	add	r3, r3, #15
	cmp	r1, r3, asr #12
	ldrh	r3, [r2, #14]
	blt	.L1775
	add	r0, r3, #255
	cmp	r1, r0, asr #8
	bge	.L1776
.L1775:
	bic	r3, r3, #255
	strh	r3, [r2, #14]	@ movhi
.L1776:
	ldrb	r3, [r4, #2229]	@ zero_extendqisi2
	tst	r3, #6
	beq	.L1777
	bl	FlashSavePhyInfo
	mov	r0, #0
	bl	flash_enter_slc_mode
	mov	r0, #0
	ldr	r1, [r8, #1704]
	bl	FlashDdrParaScan
	mov	r0, #0
	bl	flash_exit_slc_mode
.L1777:
	bl	FlashSavePhyInfo
.L1768:
	ldr	r2, [r4, #44]
	ldr	r7, .L1848+4
	ldr	r6, .L1848
	ldrb	r3, [r2, #26]	@ zero_extendqisi2
	ldrh	r0, [r2, #10]
	ldrb	r9, [r2, #18]	@ zero_extendqisi2
	strb	r3, [r4, #144]
	ldrh	r3, [r2, #16]
	ubfx	r1, r3, #7, #1
	strb	r1, [r4, #8]
	ubfx	r1, r3, #3, #1
	strb	r1, [r8, #1733]
	ubfx	r1, r3, #4, #1
	ubfx	r3, r3, #8, #3
	strb	r1, [r4, #2240]
	strb	r3, [r4, #2229]
	mov	r3, #0
	ldrb	r1, [r2, #12]	@ zero_extendqisi2
	str	r3, [r8, #1696]
	bl	__aeabi_idiv
	mov	r1, r0
	mov	r0, r9
	bl	BuildFlashLsbPageTable
	bl	FlashDieInfoInit
	ldr	r3, [r4, #44]
	ldrh	r2, [r3, #16]
	tst	r2, #64
	beq	.L1779
	ldrb	r0, [r3, #19]	@ zero_extendqisi2
	ldrb	r3, [r7, #1211]	@ zero_extendqisi2
	strb	r0, [r7, #1208]
	strb	r3, [r7, #1209]
	ldrb	r3, [r7, #1212]	@ zero_extendqisi2
	strb	r3, [r6, #1713]
	sub	r3, r0, #1
	cmp	r3, #7
	bhi	.L1780
	ldr	r3, .L1848+52
	str	r3, [r6, #1696]
	sub	r3, r0, #5
	cmp	r0, #8
	cmpne	r3, #1
	sub	r6, r0, #8
	clz	r6, r6
	movls	r3, #1
	strls	r3, [r7, #2304]
	cmp	r0, #7
	mov	r6, r6, lsr #5
	ldreq	r6, .L1848+56
	beq	.L1783
	ldr	r3, .L1848+56
	cmp	r6, #0
	sub	r2, r3, #8
	movne	r6, r3
	moveq	r6, r2
.L1783:
	sub	r2, r6, #1
	add	r6, r6, #31
	mov	r3, #0
.L1784:
	ldrsb	r1, [r2, #1]!
	cmp	r1, #0
	addeq	r3, r3, #1
	cmp	r2, r6
	bne	.L1784
	cmp	r3, #27
	bls	.L1779
	bl	FlashGetReadRetryDefault
	bl	FlashSavePhyInfo
	b	.L1779
.L1780:
	sub	r3, r0, #17
	cmp	r3, #2
	bhi	.L1786
	ldr	r3, .L1848+60
	cmp	r0, #19
	str	r3, [r6, #1696]
	moveq	r3, #15
	bne	.L1847
	b	.L1845
.L1786:
	sub	r3, r0, #65
	cmp	r0, #33
	cmpne	r3, #1
	bhi	.L1788
	ldr	r3, .L1848+64
	str	r3, [r6, #1696]
	mov	r3, #4
	strb	r3, [r7, #1209]
.L1847:
	mov	r3, #7
.L1845:
	strb	r3, [r6, #1713]
	b	.L1779
.L1788:
	sub	r2, r0, #67
	sub	r3, r0, #34
	cmp	r2, #1
	movhi	r2, #0
	movls	r2, #1
	cmp	r3, #1
	movhi	r3, r2
	orrls	r3, r2, #1
	cmp	r3, #0
	beq	.L1789
	ldr	r3, .L1848+64
	cmp	r0, #68
	cmpne	r0, #35
	str	r3, [r6, #1696]
	movne	r3, #7
	moveq	r3, #17
	cmp	r2, #0
	strb	r3, [r6, #1713]
	movne	r3, #4
	moveq	r3, #5
	strb	r3, [r4, #1209]
	b	.L1779
.L1789:
	cmp	r0, #49
	ldreq	r3, .L1848+68
	streq	r3, [r6, #1696]
	beq	.L1779
	cmp	r0, #50
	streq	r3, [r7, #2248]
	ldreq	r2, .L1848+72
	streq	r2, [r6, #1696]
.L1779:
	ldr	r2, [r4, #2264]
	ldr	r3, .L1848+4
	cmp	r2, r5
	bne	.L1794
	ldrb	r1, [r3, #144]	@ zero_extendqisi2
	cmp	r1, #0
	ldrne	r3, [r3, #44]
	movne	r1, #0
	strneb	r1, [r3, #18]
.L1794:
	ldrb	r1, [r4, #2068]	@ zero_extendqisi2
	ldr	r3, .L1848+4
	cmp	r1, #44
	bne	.L1795
	ldrb	r1, [r3, #2252]	@ zero_extendqisi2
	cmp	r1, #0
	beq	.L1795
	cmp	r2, r5
	bne	.L1796
	ldrb	r3, [r3, #144]	@ zero_extendqisi2
	cmp	r3, #0
	bne	.L1795
.L1796:
	mov	r0, #1
	mov	r3, #0
	strb	r3, [r4, #2252]
	bl	FlashSetInterfaceMode
	mov	r0, #1
	bl	NandcSetMode
.L1795:
	ldrb	r3, [r4, #2229]	@ zero_extendqisi2
	tst	r3, #6
	beq	.L1797
	ldr	r2, .L1848+4
	ldrb	r2, [r2, #2252]	@ zero_extendqisi2
	cmp	r2, #0
	bne	.L1798
	tst	r3, #1
	bne	.L1797
.L1798:
	mov	r0, #0
	bl	flash_enter_slc_mode
	mov	r0, #0
	ldr	r1, [r8, #1704]
	bl	FlashDdrParaScan
	mov	r0, #0
	bl	flash_exit_slc_mode
.L1797:
	ldr	r3, [r4, #44]
	mov	r7, #16
	ldr	r6, .L1848+4
	ldrb	r0, [r3, #20]	@ zero_extendqisi2
	bl	FlashBchSel
	ldr	r0, .L1848+76
	bl	FlashReadIdbDataRaw
	ldr	r0, .L1848+80
	strb	r7, [r4, #1]
	bl	FlashTimingCfg
	ldr	r5, [r4, #44]
	ldrb	r2, [r4, #2069]	@ zero_extendqisi2
	ldrb	r3, [r5, #12]	@ zero_extendqisi2
	ldrh	r8, [r5, #14]
	strh	r3, [r4, #124]	@ movhi
	ldrb	r3, [r5, #7]	@ zero_extendqisi2
	str	r3, [r4, #120]
	mov	r3, r2, asl r7
	orr	r2, r3, r2, asl #8
	ldrb	r3, [r4, #2068]	@ zero_extendqisi2
	orr	r3, r2, r3
	ldrb	r2, [r4, #2071]	@ zero_extendqisi2
	orr	r3, r3, r2, asl #24
	str	r3, [r4, #116]
	ldrb	r3, [r4, #2230]	@ zero_extendqisi2
	strh	r3, [r4, #126]	@ movhi
	ldrb	r3, [r5, #13]	@ zero_extendqisi2
	strh	r8, [r4, #130]	@ movhi
	strh	r3, [r4, #128]	@ movhi
	ldrh	r3, [r5, #10]
	strh	r3, [r4, #132]	@ movhi
	ldrb	r1, [r5, #12]	@ zero_extendqisi2
	ldrh	r0, [r5, #10]
	bl	__aeabi_idiv
	strh	r0, [r4, #134]	@ movhi
	ldrb	r2, [r5, #9]	@ zero_extendqisi2
	strh	r2, [r4, #136]	@ movhi
	ldrb	r1, [r5, #9]	@ zero_extendqisi2
	ldrh	r3, [r5, #10]
	smulbb	r3, r1, r3
	mov	r1, #512
	strh	r1, [r4, #140]	@ movhi
	ldrb	r1, [r4, #1]	@ zero_extendqisi2
	strh	r1, [r4, #142]	@ movhi
	uxth	r3, r3
	ldrb	r1, [r4]	@ zero_extendqisi2
	strh	r3, [r4, #138]	@ movhi
	cmp	r1, #1
	bne	.L1800
	mov	r3, r3, asl #1
	mov	r8, r8, lsr #1
	mov	r2, r2, asl #1
	strh	r3, [r6, #138]	@ movhi
	strb	r7, [r6, #1]
	mov	r3, #8
	strh	r8, [r6, #130]	@ movhi
	strh	r2, [r6, #136]	@ movhi
	strh	r3, [r6, #142]	@ movhi
.L1800:
	ldrb	r0, [r5, #20]	@ zero_extendqisi2
	bl	FlashBchSel
	bl	ftl_flash_suspend
	mov	r0, #0
	b	.L1816
.L1802:
	mvn	r0, #1
.L1816:
	add	sp, sp, #20
	@ sp needed
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L1849:
	.align	2
.L1848:
	.word	.LANCHOR2
	.word	.LANCHOR0
	.word	.LANCHOR0+2068
	.word	.LC97
	.word	.LANCHOR0+1210
	.word	1446522928
	.word	.LANCHOR1
	.word	.LANCHOR1+3416
	.word	.LANCHOR1+3292
	.word	.LANCHOR0+48
	.word	.LANCHOR1+472
	.word	.LC98
	.word	.LANCHOR2-568
	.word	HynixReadRetrial
	.word	.LANCHOR0+1238
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
	ldr	r1, .L1867
	mov	r4, r2
	mov	r6, r0
	ldrb	r2, [r1, #144]	@ zero_extendqisi2
	mov	r5, r1
	ldr	r3, [r1, #44]
	cmp	r2, #0
	ldrb	r3, [r3, #19]	@ zero_extendqisi2
	beq	.L1851
	ldr	r2, [r1, #2248]
	cmp	r2, #0
	ldmnefd	sp!, {r4, r5, r6, r7, r8, r9, r10, pc}
.L1851:
	sub	r2, r3, #5
	cmp	r3, #50
	cmpne	r2, #2
	bls	.L1852
	sub	r2, r3, #19
	tst	r2, #239
	moveq	r2, #1
	movne	r2, #0
	cmp	r3, #68
	movne	r3, r2
	orreq	r3, r2, #1
	cmp	r3, #0
	ldmeqfd	sp!, {r4, r5, r6, r7, r8, r9, r10, pc}
.L1852:
	ldr	r8, .L1867+4
	movw	r10, #65535
	ldr	r9, .L1867+8
.L1854:
	ldr	r3, [r5, #44]
	ldrh	r3, [r3, #10]
	cmp	r3, r4
	bls	.L1866
	mov	r3, r4, asl #1
	ldrh	r3, [r9, r3]
	cmp	r3, r10
	ldmnefd	sp!, {r4, r5, r6, r7, r8, r9, r10, pc}
	mov	r1, #255
	mov	r2, #32768
	ldr	r0, [r8, #1716]
	bl	ftl_memset
	ldr	r2, [r8, #1716]
	add	r1, r4, r7
	add	r4, r4, #1
	mov	r0, r6
	mov	r3, r2
	uxth	r4, r4
	bl	FlashProgPage
	b	.L1854
.L1866:
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, pc}
.L1868:
	.align	2
.L1867:
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
	ldr	r3, .L1920
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
.L1870:
	ldr	r3, [sp, #16]
	cmp	r8, r3
	beq	.L1919
	ldr	r3, [sp, #16]
	add	r2, sp, #28
	mov	r0, r4
	ldr	r1, [sp, #20]
	rsb	r3, r8, r3
	ldr	r9, .L1920+4
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
	bcs	.L1872
	add	r3, r9, r3
	mov	r7, #0
	ldrb	r5, [r3, #2232]	@ zero_extendqisi2
	mov	r0, r5
	bl	NandcWaitFlashReady
	mov	r0, r5
	bl	NandcFlashCs
.L1873:
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
	ldr	r3, .L1920+8
	ldrb	r3, [r3, #1732]	@ zero_extendqisi2
	cmp	r3, #0
	mov	r6, r0
	beq	.L1874
	mov	r0, r5
	bl	flash_read_ecc
	cmp	r0, #5
	movhi	r6, #256
.L1874:
	cmp	r7, #9
	cmnls	r6, #1
	moveq	r3, #1
	movne	r3, #0
	addeq	r7, r7, #1
	beq	.L1873
.L1875:
	cmp	r7, #0
	mov	r7, r3
	movne	r6, #256
.L1877:
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
	ldr	r3, .L1920+8
	ldrb	r2, [r3, #1732]	@ zero_extendqisi2
	cmp	r2, #0
	mov	r10, r0
	beq	.L1880
	mov	r0, r5
	bl	flash_read_ecc
	cmp	r0, #5
	movhi	r10, #256
.L1880:
	cmp	r7, #9
	cmnls	r10, #1
	addeq	r7, r7, #1
	beq	.L1877
.L1881:
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
	bls	.L1883
	cmn	r3, #1
	movne	r3, #256
.L1883:
	cmp	r3, #256
	cmnne	r3, #1
	movne	r3, #0
	str	r3, [r4]
	ldr	r3, [r4, #12]
	cmp	r3, #0
	beq	.L1886
	ldr	r2, [r3, #12]
	cmn	r2, #1
	bne	.L1886
	ldr	r2, [r3, #8]
	cmn	r2, #1
	bne	.L1886
	ldr	r3, [r3]
	cmn	r3, #1
	strne	r2, [r4]
.L1886:
	ldr	r3, [r4]
	cmn	r3, #1
	bne	.L1872
	ldr	r1, [r4, #4]
	ldr	r0, .L1920+12
	ldrb	r2, [r9, #2312]	@ zero_extendqisi2
	bl	printk
	ldr	r1, [r4, #8]
	cmp	r1, #0
	beq	.L1888
	ldr	r0, .L1920+16
	mov	r2, #4
	mov	r3, #8
	bl	rknand_print_hex
.L1888:
	ldr	r1, [r4, #12]
	cmp	r1, #0
	beq	.L1872
	mov	r2, #4
	ldr	r0, .L1920+20
	mov	r3, r2
	bl	rknand_print_hex
.L1872:
	add	r8, r8, #1
	add	r4, r4, #36
	b	.L1870
.L1919:
	mov	r0, #0
	add	sp, sp, #36
	@ sp needed
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L1921:
	.align	2
.L1920:
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
	ldr	r3, .L1997
	stmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.save {r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.pad #52
	sub	sp, sp, #52
	ldr	r6, .L1997+4
	ldrb	r3, [r3, #481]	@ zero_extendqisi2
	str	r1, [sp, #20]
	ldrb	r8, [r6]	@ zero_extendqisi2
	str	r3, [sp, #16]
	ldrb	r3, [r6, #8]	@ zero_extendqisi2
	cmp	r8, #0
	str	r2, [sp, #24]
	str	r3, [sp, #28]
	beq	.L1959
	bl	FlashReadSlc2KPages
	b	.L1992
.L1959:
	ldr	r10, .L1997+8
	mov	r9, r0
	mov	fp, r8
.L1923:
	ldr	r3, [sp, #20]
	cmp	fp, r3
	bcs	.L1995
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
	bcs	.L1926
	add	r3, r6, r3
	ldrb	r4, [r3, #2232]	@ zero_extendqisi2
	ldrb	r3, [r10, #1733]	@ zero_extendqisi2
	cmp	r3, #0
	mov	r0, r4
	moveq	r5, #0
	bl	NandcWaitFlashReady
	ldr	r3, .L1997+4
	ldr	r3, [r3, #44]
	ldrb	r2, [r3, #19]	@ zero_extendqisi2
	sub	r3, r2, #1
	cmp	r3, #7
	bhi	.L1928
	sub	r2, r2, #7
	add	r1, r6, r4
	cmp	r2, #1
	ldr	r2, .L1997+4
	ldrb	r3, [r1, #1222]	@ zero_extendqisi2
	add	r2, r2, r4
	ldrlsb	r3, [r1, #1230]	@ zero_extendqisi2
	ldrb	r2, [r2, #2064]	@ zero_extendqisi2
	cmp	r2, r3
	beq	.L1928
	mov	r0, r4
	ldrb	r1, [r6, #1211]	@ zero_extendqisi2
	ldr	r2, .L1997+12
	bl	HynixSetRRPara
.L1928:
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
	beq	.L1930
	ldr	r3, .L1997+4
	ldrb	r3, [r3, #144]	@ zero_extendqisi2
	cmp	r3, #0
	beq	.L1930
	bl	flash_enter_slc_mode
	b	.L1931
.L1930:
	bl	flash_exit_slc_mode
.L1931:
	ldr	r1, [sp, #44]
	cmn	r1, #1
	cmpeq	r4, #255
	moveq	r3, #0
	movne	r3, #1
	moveq	r5, r3
	beq	.L1933
	cmp	r5, #0
	beq	.L1934
	ldr	r2, [r6, #4]
	mov	r0, r4
	add	r2, r1, r2
	bl	FlashReadDpCmd
	b	.L1935
.L1934:
	mov	r0, r4
	bl	FlashReadCmd
.L1935:
	mov	r0, r4
	bl	NandcWaitFlashReady
	cmp	r5, #0
	beq	.L1933
	mov	r0, r4
	ldr	r1, [sp, #44]
	bl	FlashReadDpDataOutCmd
.L1933:
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
	bne	.L1931
.L1936:
	cmp	r5, #0
	beq	.L1937
	ldr	r3, .L1997+4
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
.L1937:
	mov	r0, r4
	str	ip, [sp, #32]
	bl	NandcFlashDeCs
	ldr	ip, [sp, #32]
	ldrb	r3, [sp, #28]	@ zero_extendqisi2
	cmn	ip, #1
	strb	r3, [r6, #8]
	bne	.L1944
	ldrb	r3, [r6, #2252]	@ zero_extendqisi2
	cmp	r3, #0
	bne	.L1939
.L1943:
	ldr	r5, [r10, #1696]
	cmp	r5, #0
	bne	.L1940
	b	.L1996
.L1939:
	ldr	r3, [r6, #80]
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
	beq	.L1942
	ldrb	r3, [r6, #2312]	@ zero_extendqisi2
	cmp	r0, r3, lsr #1
	bls	.L1962
.L1942:
	ubfx	r0, r5, #8, #8
	str	ip, [sp, #32]
	bl	NandcSetDdrPara
	ldr	ip, [sp, #32]
	cmn	ip, #1
	beq	.L1943
	b	.L1962
.L1940:
	mov	r0, r4
	ldr	r1, [sp, #44]
	ldr	r2, [r7, #8]
	ldr	r3, [r7, #12]
	blx	r5
	cmn	r0, #1
	mov	ip, r0
	bne	.L1964
	ldr	r3, [r6, #44]
	ldrb	r3, [r3, #19]	@ zero_extendqisi2
	sub	r3, r3, #1
	cmp	r3, #7
	bhi	.L1946
	mov	r0, r4
	ldrb	r1, [r6, #1211]	@ zero_extendqisi2
	ldr	r2, .L1997+12
	mov	r3, #0
	bl	HynixSetRRPara
.L1946:
	ldr	r1, [sp, #44]
	mov	r0, r4
	ldr	r2, [r7, #8]
	ldr	r3, [r7, #12]
	bl	FlashReadRawPage
	ldr	r1, [r7, #4]
	ldrb	r2, [r6, #2312]	@ zero_extendqisi2
	mov	ip, r0
	ldr	r0, .L1997+16
	mov	r3, ip
	str	ip, [sp, #32]
	bl	printk
	ldr	ip, [sp, #32]
	cmn	ip, #1
	bne	.L1964
	ldrb	r5, [r6, #144]	@ zero_extendqisi2
	cmp	r5, #0
	beq	.L1945
	ldr	r3, [sp, #12]
	mov	r0, r4
	cmp	r3, #0
	beq	.L1947
	bl	flash_enter_slc_mode
	b	.L1948
.L1947:
	bl	flash_exit_slc_mode
.L1948:
	ldr	ip, [r10, #1696]
	mov	r0, r4
	ldr	r1, [sp, #44]
	ldr	r2, [r7, #8]
	ldr	r3, [r7, #12]
	blx	ip
	mov	ip, r0
	b	.L1964
.L1996:
	mov	r0, r4
	ldr	r1, [sp, #44]
	ldr	r2, [r7, #8]
	ldr	r3, [r7, #12]
	bl	FlashReadRawPage
	mov	ip, r0
	b	.L1945
.L1962:
	mov	r5, #0
.L1944:
	ldrb	r3, [r6, #2312]	@ zero_extendqisi2
	add	r3, r3, r3, asl #1
	cmp	ip, r3, asr #2
	bls	.L1945
	ldr	r3, [r10, #1696]
	cmp	r3, #0
	moveq	ip, #256
	b	.L1945
.L1964:
	mov	r5, #0
.L1945:
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
	bne	.L1952
	ldr	r1, [r7, #4]
	ldr	r0, .L1997+20
	ldrb	r2, [r6, #2312]	@ zero_extendqisi2
	bl	printk
	ldr	r1, [r7, #12]
	cmp	r1, #0
	beq	.L1952
	mov	r2, #4
	ldr	r0, .L1997+24
	mov	r3, r2
	bl	rknand_print_hex
.L1952:
	cmp	r5, #0
	beq	.L1954
	ldrb	r3, [r6, #2312]	@ zero_extendqisi2
	add	r3, r3, r3, asl #1
	cmp	r8, r3, asr #2
	bls	.L1955
	ldr	r3, [r10, #1696]
	cmp	r3, #0
	moveq	r8, #256
.L1955:
	ldr	r3, [sp, #8]
	cmp	r8, #256
	cmnne	r8, #1
	add	r3, r3, #36
	movne	r2, #0
	streq	r8, [r9, r3]
	strne	r2, [r9, r3]
.L1954:
	ldr	r3, [sp, #12]
	add	fp, fp, r5
	cmp	r3, #0
	beq	.L1926
	ldrb	r3, [r6, #144]	@ zero_extendqisi2
	cmp	r3, #0
	beq	.L1926
	mov	r0, r4
	bl	flash_exit_slc_mode
.L1926:
	add	fp, fp, #1
	b	.L1923
.L1995:
	mov	r0, #0
.L1992:
	add	sp, sp, #52
	@ sp needed
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L1998:
	.align	2
.L1997:
	.word	.LANCHOR1
	.word	.LANCHOR0
	.word	.LANCHOR2
	.word	.LANCHOR0+1214
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
	ldr	r4, .L2012
	.pad #60
	sub	sp, sp, #60
	ldr	r0, .L2012+4
	mov	r1, #0
	ldr	r10, .L2012+8
	mov	r5, #0
	ldrh	r3, [r4, #130]
	mvn	fp, #0
	ldrh	r6, [r4, #128]
	mov	r9, r5
	mov	r8, r4
	smulbb	r6, r6, r3
	bl	ftl_memset
	ldr	r3, [r10, #1720]
	uxth	r6, r6
	add	r2, r6, fp
	str	r3, [sp, #32]
	uxth	r3, r2
	str	r5, [sp, #28]
	str	r3, [sp, #4]
.L2000:
	ldrb	r3, [r8, #2230]	@ zero_extendqisi2
	uxtb	r7, r5
	cmp	r3, r7
	bls	.L2011
	mul	ip, r6, r7
	ldr	r4, [sp, #4]
	sub	r3, r6, #12
.L2001:
	cmp	r4, r3
	ble	.L2003
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
	beq	.L2002
	ldr	r2, [r10, #1720]
	ldrh	r1, [r2]
	movw	r2, #61664
	cmp	r1, r2
	bne	.L2002
	mov	r1, r7
	ldr	r0, .L2012+12
	mov	r2, r4
	mov	r7, r7, asl #1
	bl	printk
	ldr	r3, .L2012+4
	add	r9, r9, #1
	strh	r4, [r3, r7]	@ movhi
	uxth	r9, r9
	b	.L2003
.L2002:
	sub	r4, r4, #1
	uxth	r4, r4
	b	.L2001
.L2003:
	ldrb	r3, [r8, #2230]	@ zero_extendqisi2
	add	r5, r5, #1
	cmp	r3, r9
	moveq	fp, #0
	b	.L2000
.L2011:
	mov	r0, fp
	add	sp, sp, #60
	@ sp needed
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L2013:
	.align	2
.L2012:
	.word	.LANCHOR0
	.word	.LANCHOR2+1736
	.word	.LANCHOR2
	.word	.LC103
	.fnend
	.size	FlashLoadFactorBbt, .-FlashLoadFactorBbt
	.align	2
	.global	FlashProgSlc2KPages
	.type	FlashProgSlc2KPages, %function
FlashProgSlc2KPages:
	.fnstart
	@ args = 0, pretend = 0, frame = 56
	@ frame_needed = 0, uses_anonymous_args = 0
	ldr	r3, .L2044
	stmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.save {r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.pad #68
	sub	sp, sp, #68
	ldr	r8, .L2044+4
	mov	r6, r1
	ldrb	r3, [r3, #481]	@ zero_extendqisi2
	mov	r9, r2
	mov	r4, r0
	mov	r10, r0
	mov	r7, #0
	mov	fp, r8
	str	r3, [sp, #12]
.L2015:
	cmp	r7, r6
	beq	.L2042
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
	bcs	.L2017
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
.L2017:
	add	r7, r7, #1
	add	r10, r10, #36
	b	.L2015
.L2042:
	ldr	r5, .L2044+8
	mov	r7, #0
	mov	r8, r5
.L2022:
	cmp	r7, r6
	beq	.L2043
	ldr	r3, [r4]
	cmn	r3, #1
	bne	.L2023
	ldr	r1, [r4, #4]
	ldr	r0, .L2044+12
	bl	printk
	b	.L2024
.L2023:
	rsb	r3, r7, r6
	mov	r1, r9
	add	r2, sp, #20
	mov	r0, r4
	uxtb	r3, r3
	str	r3, [sp]
	add	r3, sp, #24
	bl	LogAddr2PhyAddr
	ldr	r2, [r5, #1724]
	mov	r3, #0
	mov	lr, r4
	str	r3, [r2]
	ldr	r2, [r5, #1728]
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
	ldr	r3, [r5, #1724]
	str	r3, [sp, #36]
	ldr	r3, [r5, #1728]
	str	r3, [sp, #40]
	bl	FlashReadPages
	ldr	r10, [sp, #28]
	cmn	r10, #1
	bne	.L2025
	ldr	r0, .L2044+16
	ldr	r1, [r4, #4]
	bl	printk
	str	r10, [r4]
.L2025:
	ldr	r10, [sp, #28]
	cmp	r10, #256
	bne	.L2026
	ldr	r0, .L2044+20
	ldr	r1, [r4, #4]
	bl	printk
	str	r10, [r4]
.L2026:
	ldr	r3, [r4, #12]
	cmp	r3, #0
	beq	.L2027
	ldr	r2, [r3]
	ldr	r3, [r8, #1728]
	ldr	r3, [r3]
	cmp	r2, r3
	beq	.L2027
	ldr	r0, .L2044+24
	ldr	r1, [r4, #4]
	bl	printk
	mvn	r3, #0
	str	r3, [r4]
.L2027:
	ldr	r3, [r4, #8]
	cmp	r3, #0
	beq	.L2024
	ldr	r2, [r3]
	ldr	r3, [r8, #1724]
	ldr	r3, [r3]
	cmp	r2, r3
	beq	.L2024
	ldr	r0, .L2044+28
	ldr	r1, [r4, #4]
	bl	printk
	mvn	r3, #0
	str	r3, [r4]
.L2024:
	add	r7, r7, #1
	add	r4, r4, #36
	b	.L2022
.L2043:
	mov	r0, #0
	add	sp, sp, #68
	@ sp needed
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L2045:
	.align	2
.L2044:
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
	ldr	r5, .L2058
	ldr	r6, .L2058+4
	ldr	r3, [r5, #-524]
	sub	r9, r6, #76
	ldr	r8, [r5, #-500]
	sub	r10, r6, #120
	mov	fp, r9
	str	r3, [r5, #1760]
	str	r8, [r5, #1764]
.L2047:
	ldrh	r3, [r10]
	cmp	r7, r3
	bcs	.L2057
	ldrh	r4, [r9]
	mvn	r3, #0
	movw	ip, #61664
	strh	r3, [r6, #2]!	@ movhi
	add	r4, r4, r3
	uxth	r4, r4
.L2048:
	ldrh	r3, [fp]
	sub	r2, r3, #16
	cmp	r4, r2
	ble	.L2050
	mla	r3, r3, r7, r4
	mov	r1, #1
	ldr	r0, .L2058+8
	mov	r2, r1
	str	ip, [sp, #4]
	mov	r3, r3, asl #10
	str	r3, [r5, #1756]
	bl	FlashReadPages
	ldr	r3, [r5, #1752]
	cmn	r3, #1
	ldr	ip, [sp, #4]
	beq	.L2049
	ldrh	r3, [r8]
	cmp	r3, ip
	streqh	r4, [r6]	@ movhi
	beq	.L2050
.L2049:
	sub	r4, r4, #1
	uxth	r4, r4
	b	.L2048
.L2050:
	add	r7, r7, #1
	b	.L2047
.L2057:
	mov	r0, #0
	add	sp, sp, #12
	@ sp needed
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L2059:
	.align	2
.L2058:
	.word	.LANCHOR2
	.word	.LANCHOR0+2462
	.word	.LANCHOR2+1752
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
	ldr	r3, .L2073
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
	bne	.L2063
.L2064:
	sxth	r4, r7
	sxth	r3, r5
	cmp	r4, r3
	bgt	.L2063
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
	bne	.L2065
	ldr	r3, [sp, #44]
	cmn	r3, #1
	bne	.L2065
	ldr	r3, [sp, #4]
	cmn	r3, #1
	subne	r4, r4, #1
	uxthne	r5, r4
	bne	.L2064
.L2065:
	add	r3, r4, #1
	uxth	r7, r3
	b	.L2064
.L2063:
	sxth	r0, r5
	add	sp, sp, #104
	@ sp needed
	ldmfd	sp!, {r4, r5, r6, r7, r8, pc}
.L2074:
	.align	2
.L2073:
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
	ldr	r4, .L2108
	ldr	r7, .L2108+4
	ldr	r8, .L2108+8
	ldr	r3, [r4, #-524]
	mov	r9, r4
	ldr	r6, [r4, #-500]
	str	r3, [r4, #1760]
	str	r6, [r4, #1764]
	bl	FtlBbtMemInit
	movw	r3, #2386
	ldrh	r5, [r7, r3]
	sub	r5, r5, #1
	uxth	r5, r5
.L2076:
	ldrh	r3, [r8]
	sub	r3, r3, #48
	cmp	r5, r3
	ble	.L2079
	mov	r1, #1
	ldr	r0, .L2108+12
	mov	r2, r1
	mov	r3, r5, asl #10
	str	r3, [r4, #1756]
	bl	FlashReadPages
	ldr	r3, [r4, #1752]
	cmn	r3, #1
	bne	.L2077
	ldr	r3, [r9, #1756]
	mov	r1, #1
	ldr	r0, .L2108+12
	mov	r2, r1
	add	r3, r3, #1
	str	r3, [r9, #1756]
	bl	FlashReadPages
.L2077:
	ldr	r3, [r4, #1752]
	cmn	r3, #1
	beq	.L2078
	ldrh	r2, [r6]
	movw	r3, #61649
	cmp	r2, r3
	bne	.L2078
	movw	r3, #2452
	strh	r5, [r7, r3]	@ movhi
	ldr	r3, [r6, #4]
	str	r3, [r7, #2460]
	ldr	r3, .L2108+16
	ldrh	r2, [r6, #8]
	strh	r2, [r3, #4]	@ movhi
	b	.L2079
.L2078:
	sub	r5, r5, #1
	uxth	r5, r5
	b	.L2076
.L2079:
	movw	r9, #2452
	movw	r2, #65535
	ldrh	r3, [r7, r9]
	ldr	r8, .L2108+4
	cmp	r3, r2
	ldr	r5, .L2108+16
	beq	.L2093
	ldrh	r3, [r5, #4]
	cmp	r3, r2
	beq	.L2083
	mov	r1, #1
	ldr	r0, .L2108+12
	mov	r2, r1
	mov	r3, r3, asl #10
	str	r3, [r4, #1756]
	bl	FlashReadPages
	ldr	r3, [r4, #1752]
	cmn	r3, #1
	beq	.L2083
	ldrh	r2, [r6]
	movw	r3, #61649
	cmp	r2, r3
	bne	.L2083
	ldr	r3, [r6, #4]
	ldr	r2, [r8, #2460]
	cmp	r3, r2
	strhi	r3, [r8, #2460]
	ldrhih	r2, [r5, #4]
	ldrhih	r3, [r6, #8]
	strhih	r2, [r8, r9]	@ movhi
	strhih	r3, [r5, #4]	@ movhi
.L2083:
	movw	r3, #2452
	mov	r1, #1
	ldrh	r0, [r7, r3]
	movw	r8, #61649
	bl	FtlGetLastWrittenPage
	uxth	r7, r0
	add	r0, r0, #1
	strh	r0, [r5, #2]	@ movhi
.L2085:
	sxth	r3, r7
	cmp	r3, #0
	blt	.L2090
	ldrh	r2, [r5]
	mov	r1, #1
	ldr	r0, .L2108+12
	orr	r3, r3, r2, asl #10
	str	r3, [r4, #1756]
	ldr	r3, [r4, #-524]
	mov	r2, r1
	str	r3, [r4, #1760]
	bl	FlashReadPages
	ldr	r3, [r4, #1752]
	cmn	r3, #1
	beq	.L2086
	ldrh	r3, [r6]
	cmp	r3, r8
	bne	.L2086
.L2090:
	ldrh	r2, [r6, #10]
	ldrh	r0, [r6, #12]
	ldr	r3, .L2108+4
	strh	r2, [r5, #6]	@ movhi
	movw	r2, #65535
	cmp	r0, r2
	bne	.L2087
	b	.L2088
.L2086:
	sub	r7, r7, #1
	uxth	r7, r7
	b	.L2085
.L2087:
	ldr	r2, [r3, #2316]
	cmp	r0, r2
	beq	.L2088
	movw	r1, #2330
	ldrh	r3, [r3, r1]
	mov	r3, r3, lsr #2
	cmp	r0, r3
	cmpcc	r2, r3
	bcs	.L2088
	bl	FtlSysBlkNumInit
.L2088:
	ldr	r6, .L2108+20
	mov	r5, #0
	ldr	r8, .L2108+24
	sub	r7, r6, #134
.L2091:
	ldrh	r3, [r7]
	cmp	r5, r3
	bcs	.L2107
	ldrh	r2, [r8]
	ldr	r1, [r4, #1760]
	ldr	r0, [r6, #4]!
	mov	r2, r2, asl #2
	mla	r1, r5, r2, r1
	bl	ftl_memcpy
	add	r5, r5, #1
	b	.L2091
.L2107:
	mov	r0, #0
	ldmfd	sp!, {r3, r4, r5, r6, r7, r8, r9, pc}
.L2093:
	mvn	r0, #0
	ldmfd	sp!, {r3, r4, r5, r6, r7, r8, r9, pc}
.L2109:
	.align	2
.L2108:
	.word	.LANCHOR2
	.word	.LANCHOR0
	.word	.LANCHOR0+2386
	.word	.LANCHOR2+1752
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
	ldr	r7, .L2196
	mov	r1, r5
	ldr	r4, .L2196+4
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
	ldr	r0, .L2196+8
	mov	r1, #255
	mov	r2, #16
	bl	ftl_memset
	movw	r3, #2328
	ldrh	r3, [r7, r3]
	str	r7, [sp, #16]
	mov	r7, r4
	str	r3, [sp, #4]
.L2111:
	ldr	r3, .L2196+12
	ldr	r2, [sp, #4]
	ldr	r1, .L2196
	ldrh	r3, [r3]
	cmp	r3, r2
	mov	r0, r1
	bls	.L2152
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
.L2153:
	uxth	r1, r5
	cmp	r1, ip
	bcs	.L2191
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
	bne	.L2112
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
.L2112:
	add	r5, r5, #1
	b	.L2153
.L2191:
	cmp	fp, #0
	beq	.L2115
	ldr	r0, [r4, #-536]
	mov	r1, fp
	mov	r2, #1
	bl	FlashReadPages
	mov	r3, #0
.L2189:
	str	r3, [sp, #8]
	ldrh	r3, [sp, #8]
	cmp	r3, fp
	bcs	.L2115
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
	bne	.L2118
	mov	r10, #16
	movw	ip, #65535
.L2117:
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
	beq	.L2118
.L2119:
	ldr	r3, [r7, #-536]
	ldr	r3, [r3, r9]
	cmn	r3, #1
	bne	.L2118
	sub	r10, r10, #1
	uxth	r10, r10
	cmp	r10, #0
	bne	.L2117
.L2118:
	ldr	r3, [r7, #-536]
	ldr	r3, [r3, r9]
	cmn	r3, #1
	beq	.L2121
	ldr	r2, [r7, #-3332]
	ldr	r3, [r6, #4]
	cmn	r2, #1
	beq	.L2122
	cmp	r2, r3
	bhi	.L2123
.L2122:
	cmn	r3, #1
	addne	r2, r3, #1
	strne	r2, [r8, #-3332]
.L2123:
	ldrh	r2, [r6]
	movw	r1, #61604
	cmp	r2, r1
	beq	.L2125
	bhi	.L2126
	movw	r3, #61574
	cmp	r2, r3
	bne	.L2124
	ldr	r2, .L2196+16
	ldr	lr, [r8, #-460]
	ldrh	ip, [r2]
	ldrh	r1, [r2, #26]
	sub	r0, ip, #1
	rsb	r2, r1, r0
	str	r2, [sp, #12]
	uxth	r3, r0
	b	.L2139
.L2126:
	movw	r3, #61634
	cmp	r2, r3
	beq	.L2128
	movw	r3, #65535
	cmp	r2, r3
	moveq	r0, r5
	beq	.L2190
	b	.L2124
.L2128:
	ldr	r3, [sp, #16]
	ldr	ip, .L2196+20
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
.L2130:
	ldr	r1, [sp, #20]
	sxth	r0, r3
	cmp	r0, r1
	ble	.L2192
	mov	r10, r0, asl #2
	ldr	r1, [r6, #4]
	str	r10, [sp, #12]
	ldr	r10, [r9, r0, asl #2]
	cmp	r1, r10
	bls	.L2131
	ldr	r1, [r9]
	cmp	r1, #0
	bne	.L2132
	cmp	r2, lr
	addne	r2, r2, #1
	strneh	r2, [ip]	@ movhi
.L2132:
	uxth	r9, r3
	mov	r1, #0
.L2133:
	uxth	r2, r1
	cmp	r2, r9
	bcs	.L2193
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
	b	.L2133
.L2193:
	ldr	r2, [r4, #-444]
	mov	r0, r0, asl #1
	ldr	r1, [r6, #4]
	ldr	lr, [sp, #12]
	str	r1, [r2, lr]
	ldr	r2, [r4, #-472]
	strh	r5, [r2, r0]	@ movhi
	sxth	r0, r3
	cmp	r0, #0
	bge	.L2135
	b	.L2124
.L2131:
	sub	r3, r3, #1
	uxth	r3, r3
	b	.L2130
.L2192:
	cmp	r0, #0
	bge	.L2166
	b	.L2124
.L2135:
	ldr	r1, .L2196
	ldrh	r2, [ip]
	ldr	r1, [r1, #2416]
	rsb	r1, r2, r1
	sub	r1, r1, #1
	sxth	r1, r1
	cmp	r0, r1
	bgt	.L2124
.L2166:
	add	r2, r2, #1
	ldr	r1, [r6, #4]
	strh	r2, [ip]	@ movhi
	sxth	r3, r3
	ldr	r2, [r4, #-444]
	str	r1, [r2, r3, asl #2]
	mov	r3, r3, asl #1
	ldr	r2, [r4, #-472]
	b	.L2187
.L2145:
	ldr	r9, [lr, r2, asl #2]
	mov	r10, r2, asl #2
	ldr	r0, [r6, #4]
	cmp	r0, r9
	bhi	.L2194
	sub	r3, r3, #1
	uxth	r3, r3
.L2139:
	ldr	r0, [sp, #12]
	sxth	r2, r3
	cmp	r2, r0
	bgt	.L2145
	b	.L2144
.L2194:
	ldr	r0, [lr]
	cmp	r0, #0
	bne	.L2141
	cmp	r1, ip
	addne	r1, r1, #1
	ldrne	r0, .L2196+24
	strneh	r1, [r0]	@ movhi
.L2141:
	uxth	lr, r3
	mov	r0, #0
.L2142:
	uxth	r1, r0
	cmp	r1, lr
	bcs	.L2195
	ldr	ip, [r4, #-460]
	sxth	r1, r1
	add	r0, r0, #1
	add	r9, ip, r1, asl #2
	ldr	r9, [r9, #4]
	str	r9, [ip, r1, asl #2]
	mov	r1, r1, asl #1
	ldr	ip, .L2196
	ldr	ip, [ip, #2436]
	add	r9, ip, r1
	ldrh	r9, [r9, #2]
	strh	r9, [ip, r1]	@ movhi
	b	.L2142
.L2195:
	ldr	r1, [r8, #-460]
	mov	r2, r2, asl #1
	ldr	r0, [r6, #4]
	str	r0, [r1, r10]
	ldr	r1, .L2196
	ldr	r1, [r1, #2436]
	strh	r5, [r1, r2]	@ movhi
.L2144:
	sxth	r3, r3
	cmp	r3, #0
	blt	.L2124
	ldr	r0, .L2196+24
	ldrh	r2, [r0, #-26]
	ldrh	r1, [r0]
	sub	r2, r2, #1
	rsb	r2, r1, r2
	sxth	r2, r2
	cmp	r3, r2
	bgt	.L2124
	add	r1, r1, #1
	ldr	r2, [r8, #-460]
	strh	r1, [r0]	@ movhi
	ldr	r1, [r6, #4]
	str	r1, [r2, r3, asl #2]
	mov	r3, r3, asl #1
	ldr	r2, .L2196
	ldr	r2, [r2, #2436]
.L2187:
	strh	r5, [r2, r3]	@ movhi
	b	.L2124
.L2125:
	ldr	r2, .L2196+8
	movw	r1, #65535
	ldrh	r0, [r2]
	cmp	r0, r1
	streqh	r5, [r2]	@ movhi
	beq	.L2188
	ldrh	r0, [r2, #4]
	cmp	r0, r1
	beq	.L2147
	mov	r1, #1
	bl	FtlFreeSysBlkQueueIn
.L2147:
	ldr	r3, [r6, #4]
	ldr	r2, [r8, #-3292]
	cmp	r2, r3
	ldr	r3, .L2196+8
	strcsh	r5, [r3, #4]	@ movhi
	bcs	.L2124
	ldrh	r2, [r3]
	strh	r5, [r3]	@ movhi
	strh	r2, [r3, #4]	@ movhi
	ldr	r3, [r6, #4]
.L2188:
	str	r3, [r7, #-3292]
	b	.L2124
.L2121:
	ldr	r3, .L2196
	mov	r0, r5
	ldrb	r1, [r3, #144]	@ zero_extendqisi2
	cmp	r1, #0
	beq	.L2149
.L2190:
	mov	r1, #0
.L2149:
	bl	FtlFreeSysBlkQueueIn
.L2124:
	ldr	r3, [sp, #8]
	add	r3, r3, #1
	b	.L2189
.L2115:
	ldr	r3, [sp, #4]
	add	r5, r3, #1
	uxth	r3, r5
	str	r3, [sp, #4]
	b	.L2111
.L2152:
	ldr	r3, .L2196+4
	ldr	ip, [r3, #-472]
	ldrh	r2, [ip]
	cmp	r2, #0
	beq	.L2154
.L2157:
	ldr	r0, [r0, #2436]
	ldr	r3, .L2196
	ldrh	r2, [r0]
	cmp	r2, #0
	beq	.L2155
	b	.L2178
.L2154:
	sub	r3, r3, #388
	ldrh	r3, [r3]
	cmp	r3, #0
	ldrne	lr, [r1, #2416]
	beq	.L2157
.L2158:
	uxth	r3, r2
	sxth	r1, r3
	cmp	r1, lr
	bcs	.L2157
	mov	r5, r1, asl #1
	add	r2, r2, #1
	ldrh	r5, [ip, r5]
	cmp	r5, #0
	beq	.L2158
	mov	r6, #0
.L2159:
	ldr	ip, [r0, #2416]
	sxth	r2, r3
	cmp	r2, ip
	bcs	.L2157
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
	b	.L2159
.L2155:
	movw	r1, #2434
	ldrh	r1, [r3, r1]
	cmp	r1, #0
	movwne	r1, #2408
	ldrneh	ip, [r3, r1]
	beq	.L2178
.L2162:
	uxth	r3, r2
	sxth	r1, r3
	cmp	r1, ip
	bge	.L2178
	mov	lr, r1, asl #1
	add	r2, r2, #1
	ldrh	lr, [r0, lr]
	cmp	lr, #0
	beq	.L2162
	ldr	r0, .L2196
	mov	r7, #0
	ldr	r6, .L2196+16
.L2163:
	ldrh	ip, [r6]
	sxth	r2, r3
	cmp	r2, ip
	bge	.L2178
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
	b	.L2163
.L2178:
	mov	r0, #0
	add	sp, sp, #36
	@ sp needed
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L2197:
	.align	2
.L2196:
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
	ldr	r4, .L2227
	.pad #36
	sub	sp, sp, #36
	ldr	r6, .L2227+4
	sub	r7, r4, #3296
	ldr	r3, [r4, #-524]
	ldr	r0, [r4, #-3544]
	str	r3, [r4, #1760]
	ldr	r3, [r4, #-500]
	str	r3, [r4, #1764]
	movw	r3, #2328
	ldrh	r2, [r6, r3]
	mov	r2, r2, asl #1
	bl	ftl_memset
	ldrh	r0, [r7, #-4]
	movw	r3, #65535
	cmp	r0, r3
	bne	.L2199
.L2210:
	mvn	r0, #0
	b	.L2200
.L2199:
	mov	r1, #1
	bl	FtlGetLastWrittenPage
	ldr	r3, .L2227+8
	ldrh	r9, [r7, #-4]
	mov	r7, r4
	uxth	r5, r0
	add	r0, r0, #1
	strh	r0, [r3, #2]	@ movhi
.L2201:
	sxth	r3, r5
	cmp	r3, #0
	blt	.L2209
	sxth	fp, r9
	mov	r1, #1
	ldr	r0, .L2227+12
	mov	r2, r1
	orr	r3, r3, fp, asl #10
	str	r3, [r4, #1756]
	ldr	r3, [r4, #-524]
	str	r3, [r4, #1760]
	bl	FlashReadPages
	ldrb	r3, [r6]	@ zero_extendqisi2
	cmp	r3, #0
	beq	.L2202
	ldr	r8, [r7, #1764]
	ldr	r3, [r8, #12]
	cmp	r3, #0
	beq	.L2202
	ldr	r2, [r7, #1760]
	ldr	r10, .L2227+16
	str	r3, [sp, #28]
	mov	r0, r2
	str	r2, [sp, #24]
	ldrh	r1, [r10]
	bl	js_hash
	ldr	r3, [sp, #28]
	cmp	r3, r0
	beq	.L2202
	cmp	r5, #0
	ldr	r2, [sp, #24]
	bne	.L2203
	ldr	ip, .L2227+8
	ldrh	r1, [ip, #4]
	cmp	fp, r1
	beq	.L2203
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
	ldr	r0, .L2227+20
	str	r3, [sp, #16]
	ldr	r2, [r7, #1752]
	ldr	r3, [r7, #1756]
	bl	printk
	ldrh	r5, [r10, #-8]
	ldr	ip, [sp, #24]
	ldrh	r9, [ip, #4]
	b	.L2205
.L2203:
	mvn	r3, #0
	str	r3, [r4, #1752]
.L2202:
	ldr	r3, [r4, #1752]
	cmn	r3, #1
	beq	.L2205
	ldr	r3, [r7, #-524]
	ldr	r2, .L2227+24
	ldr	r3, [r3]
	cmp	r3, r2
	bne	.L2205
	ldr	r3, [r7, #-500]
	ldrh	r2, [r3]
	movw	r3, #61604
	cmp	r2, r3
	bne	.L2205
.L2209:
	ldr	r5, .L2227
	mov	r2, #48
	ldr	r1, [r4, #1760]
	movw	r8, #2328
	sub	r0, r5, #3600
	ldr	r7, .L2227+4
	bl	ftl_memcpy
	ldrh	r2, [r6, r8]
	ldr	r1, [r4, #1760]
	ldr	r0, [r4, #-3544]
	add	r1, r1, #48
	mov	r2, r2, asl #1
	bl	ftl_memcpy
	ldrh	r2, [r6, r8]
	ldr	r1, [r4, #1760]
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
	beq	.L2207
	ldrh	r2, [r7, r8]
	ldr	r1, [r5, #1760]
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
	b	.L2207
.L2205:
	sub	r5, r5, #1
	uxth	r5, r5
	b	.L2201
.L2207:
	ldr	r2, [r4, #-3600]
	ldr	r3, .L2227+24
	ldr	r5, .L2227
	cmp	r2, r3
	bne	.L2210
	sub	r7, r5, #3600
	movw	r1, #2342
	ldrb	r0, [r5, #-3590]	@ zero_extendqisi2
	add	r3, r7, #300
	ldrh	r1, [r6, r1]
	sub	r8, r5, #3296
	ldrh	r2, [r7, #8]
	cmp	r0, r1
	strh	r2, [r3, #6]	@ movhi
	ldr	r3, .L2227+4
	bne	.L2210
	movw	r1, #2388
	movw	r0, #2394
	ldrh	r1, [r3, r1]
	ldrh	r0, [r3, r0]
	add	r3, r3, #2320
	str	r2, [r5, #1788]
	mul	r1, r2, r1
	str	r1, [r3, #128]
	mul	r1, r0, r1
	str	r1, [r3, #108]
	ldr	r1, .L2227+28
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
	ldrhi	r2, .L2227
	strhi	r3, [r2, #-3328]
	movw	r3, #65535
	cmp	r0, r3
	beq	.L2213
	ldr	r0, .L2227+32
	bl	make_superblock
.L2213:
	ldrh	r2, [r6, #-4]
	movw	r3, #65535
	cmp	r2, r3
	beq	.L2214
	ldr	r0, .L2227+36
	bl	make_superblock
.L2214:
	ldrh	r2, [r8, #-4]
	movw	r3, #65535
	cmp	r2, r3
	beq	.L2215
	ldr	r0, .L2227+40
	bl	make_superblock
.L2215:
	ldrh	r2, [r7, #-4]
	movw	r3, #65535
	cmp	r2, r3
	beq	.L2216
	ldr	r0, .L2227+44
	bl	make_superblock
.L2216:
	mov	r0, #0
.L2200:
	add	sp, sp, #36
	@ sp needed
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L2228:
	.align	2
.L2227:
	.word	.LANCHOR2
	.word	.LANCHOR0
	.word	.LANCHOR2-3300
	.word	.LANCHOR2+1752
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
	ldr	r5, .L2243
	.pad #100
	sub	sp, sp, #100
	ubfx	r0, r0, #10, #16
	mov	r4, r1
	ldr	r7, .L2243+4
	ldrh	r9, [r5, r3]
	bl	P2V_block_in_plane
	ldr	r1, .L2243+8
	mov	r6, r0
	ldr	r0, .L2243+12
	bl	printk
	ldr	r2, [r7, #-3544]
	mov	r3, r6, asl #1
	mov	r1, r6
	ldr	r0, .L2243+16
	ldrh	r2, [r2, r3]
	bl	printk
	add	r0, sp, #96
	strh	r6, [r0, #-48]!	@ movhi
	bl	make_superblock
	ldrb	r2, [r5, #144]	@ zero_extendqisi2
	clz	r3, r4
	cmp	r2, #0
	mov	r3, r3, lsr #5
	moveq	r3, #0
	cmp	r3, #0
	moveq	r4, r3
	str	r7, [sp, #44]
	beq	.L2230
	mov	r0, r6
	bl	ftl_get_blk_mode
	cmp	r0, #1
	mov	r4, r0
	movweq	r3, #2390
	ldreqh	r9, [r5, r3]
.L2230:
	movw	r3, #2388
	ldr	r0, .L2243+20
	mov	r1, r4
	mov	r2, r9
	ldrh	r3, [r5, r3]
	mov	r6, #0
	bl	printk
	ldr	r8, .L2243+4
.L2231:
	ldr	r3, .L2243+24
	add	r0, sp, #62
	movw	lr, #65535
	mov	r10, #36
	ldrh	r3, [r3]
	mov	r7, r3
	ldr	r3, [r8, #-536]
	str	r3, [sp, #28]
	ldr	r3, [r8, #-2696]
	str	r3, [sp, #32]
	ldr	r3, .L2243+28
	ldrh	r2, [r3]
	ldrh	ip, [r3, #2]
	str	r2, [sp, #36]
	ldr	r2, [r8, #-2692]
	str	r2, [sp, #40]
	mov	r2, #0
	mov	r5, r2
.L2232:
	uxth	r3, r2
	cmp	r3, r7
	bcs	.L2241
	ldrh	r3, [r0, #2]!
	cmp	r3, lr
	beq	.L2233
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
.L2233:
	add	r2, r2, #1
	b	.L2232
.L2241:
	ldr	r0, [r8, #-536]
	mov	r1, r5
	mov	r2, r4
	mov	r10, #0
	bl	FlashReadPages
	mov	fp, #36
.L2235:
	uxth	r3, r10
	cmp	r3, r5
	bcs	.L2242
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
	ldr	r0, .L2243+32
	str	r3, [sp, #20]
	ldr	r2, [lr, r2]
	ldr	r3, [ip, #4]
	bl	printk
	b	.L2235
.L2242:
	add	r6, r6, #1
	uxth	r6, r6
	cmp	r6, r9
	bne	.L2231
.L2237:
	mov	r0, #0
	add	sp, sp, #100
	@ sp needed
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L2244:
	.align	2
.L2243:
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
	ldr	r0, .L2260
	mov	r7, #0
	ldr	r1, .L2260+4
	bl	printk
	ldr	r5, .L2260+8
	mov	r6, r5
.L2246:
	ldr	r3, .L2260+12
	uxth	r4, r7
	ldrh	r3, [r3]
	cmp	r3, r4
	bls	.L2256
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
	ldr	r0, .L2260+16
	bl	printk
	mov	r0, r8
	bl	make_superblock
	ldr	r3, .L2260+20
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
.L2247:
	ldr	r1, [sp, #28]
	uxth	r3, r2
	cmp	r3, r1
	bcs	.L2257
	ldrh	r3, [r0, #2]!
	cmp	r3, r9
	beq	.L2248
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
.L2248:
	add	r2, r2, #1
	b	.L2247
.L2257:
	ldr	r0, [r6, #-536]
	mov	r1, r4
	mov	r2, #0
	mov	r8, #0
	bl	FlashReadPages
	mov	r9, #36
.L2250:
	uxth	r3, r8
	cmp	r3, r4
	bcs	.L2258
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
	ldr	r0, .L2260+24
	str	r3, [sp, #20]
	ldr	r2, [lr, r2]
	ldr	r3, [ip, #4]
	bl	printk
	b	.L2250
.L2258:
	ldr	r0, [r6, #-536]
	mov	r1, r4
	mov	r2, #1
	mov	r8, #0
	bl	FlashReadPages
	mov	r9, #36
.L2252:
	uxth	r3, r8
	cmp	r3, r4
	bcs	.L2259
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
	ldr	r0, .L2260+28
	str	r3, [sp, #20]
	ldr	r2, [lr, r2]
	ldr	r3, [ip, #4]
	bl	printk
	b	.L2252
.L2259:
	add	r7, r7, #1
	b	.L2246
.L2256:
	mov	r0, #0
	add	sp, sp, #92
	@ sp needed
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L2261:
	.align	2
.L2260:
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
	ldr	r6, .L2281
	.pad #24
	sub	sp, sp, #24
	ldr	r5, .L2281+4
	ldrh	r4, [r6, r3]
	sub	r4, r4, #16
.L2263:
	ldr	r7, .L2281+8
	ldrh	r3, [r7]
	cmp	r4, r3
	bge	.L2279
	uxth	r8, r4
	mov	r0, r8
	bl	ftl_get_blk_mode
	ldrb	r3, [r6, #144]	@ zero_extendqisi2
	cmp	r3, #0
	beq	.L2264
	ldrh	r3, [r7, #-58]
	cmp	r4, r3
	bge	.L2265
	ldrh	r3, [r7, #16]
	cmp	r4, r3
	blt	.L2265
.L2264:
	cmp	r0, #1
	bne	.L2266
.L2265:
	ldr	r3, .L2281+12
	mov	r9, #-2147483648
	ldrh	r7, [r3]
	b	.L2267
.L2266:
	ldr	r3, .L2281+16
	mov	r9, #0
	ldrh	r7, [r3]
.L2267:
	mov	r1, r4
	mov	r2, r7
	mov	r3, r9
	ldr	r0, .L2281+20
	bl	printk
	mov	r0, r8
	bl	FtlBbmIsBadBlock
	cmp	r0, #0
	beq	.L2268
	ldr	r0, .L2281+24
	mov	r1, r4
	mov	r2, r7
	mov	r3, r9
	bl	printk
.L2268:
	mov	r10, r4, asl #10
	mov	r8, #0
.L2269:
	cmp	r8, r7
	beq	.L2280
	add	r3, r9, r10
	mov	r2, #0
	add	r3, r3, r8
	str	r3, [r5, #1756]
	ldr	r3, [r5, #-524]
	mov	r1, #1
	ldr	r0, .L2281+28
	add	r8, r8, #1
	str	r2, [r5, #1752]
	str	r3, [r5, #1760]
	ldr	r3, [r5, #-500]
	str	r3, [r5, #1764]
	bl	FlashReadPages
	ldr	r3, [r5, #1764]
	ldr	r2, [r5, #1760]
	ldr	r0, .L2281+32
	ldr	r1, [r3, #4]
	str	r1, [sp]
	ldr	r1, [r3, #8]
	str	r1, [sp, #4]
	ldr	r1, [r3, #12]
	str	r1, [sp, #8]
	ldr	r1, [r2]
	str	r1, [sp, #12]
	ldr	r2, [r2, #4]
	ldr	r1, [r5, #1756]
	str	r2, [sp, #16]
	ldr	r2, [r5, #1752]
	ldr	r3, [r3]
	bl	printk
	b	.L2269
.L2280:
	add	r4, r4, #1
	b	.L2263
.L2279:
	ldr	r0, .L2281+36
	ldr	r1, .L2281+40
	add	sp, sp, #24
	@ sp needed
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, lr}
	b	printk
.L2282:
	.align	2
.L2281:
	.word	.LANCHOR0
	.word	.LANCHOR2
	.word	.LANCHOR0+2386
	.word	.LANCHOR0+2390
	.word	.LANCHOR0+2388
	.word	.LC117
	.word	.LC118
	.word	.LANCHOR2+1752
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
	ldr	r6, .L2339
	mov	r4, r0
	str	r1, [sp, #8]
	mov	r9, r2
	str	r3, [sp, #20]
	ldr	ip, [r6, #44]
	ldrb	r7, [r6]	@ zero_extendqisi2
	ldrb	ip, [ip, #19]	@ zero_extendqisi2
	cmp	r7, #0
	str	ip, [sp, #16]
	ldr	ip, .L2339+4
	ldrb	ip, [ip, #481]	@ zero_extendqisi2
	str	ip, [sp, #12]
	beq	.L2284
	bl	FlashProgSlc2KPages
	b	.L2285
.L2297:
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
	bcc	.L2336
.L2287:
	add	r7, r7, #1
.L2284:
	ldr	r3, [sp, #8]
	ldr	r5, .L2339
	cmp	r7, r3
	bcc	.L2297
	b	.L2337
.L2336:
	ldrb	r3, [r6, #2240]	@ zero_extendqisi2
	cmp	r3, #0
	add	r3, r6, r0, asl #4
	ldr	r3, [r3, #2108]
	moveq	r10, #0
	cmp	r3, #0
	beq	.L2289
	uxtb	r0, r0
	bl	FlashWaitCmdDone
.L2289:
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
	bne	.L2291
	bl	NandcWaitFlashReady
	b	.L2292
.L2291:
	bl	NandcFlashCs
	mov	r0, r5
	ldr	r3, [sp, #32]
	ldr	r1, [sp, #28]
	add	r3, r6, r3, asl #2
	ldr	r2, [r3, #1172]
	adds	r2, r2, #0
	movne	r2, #1
	bl	FlashWaitReadyEN
	mov	r0, r5
	bl	NandcFlashDeCs
.L2292:
	ldr	r3, [sp, #16]
	sub	r3, r3, #1
	cmp	r3, #7
	bhi	.L2293
	add	r3, r6, r5
	ldrb	r3, [r3, #2064]	@ zero_extendqisi2
	cmp	r3, #0
	beq	.L2293
	mov	r0, r5
	ldrb	r1, [r6, #1211]	@ zero_extendqisi2
	ldr	r2, .L2339+8
	mov	r3, #0
	bl	HynixSetRRPara
.L2293:
	mov	r0, r5
	bl	NandcFlashCs
	cmp	r9, #1
	mov	r0, r5
	bne	.L2294
	ldrb	r3, [r6, #144]	@ zero_extendqisi2
	cmp	r3, #0
	beq	.L2294
	bl	flash_enter_slc_mode
	b	.L2295
.L2294:
	bl	flash_exit_slc_mode
.L2295:
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
	beq	.L2296
	mov	r0, r5
	ldr	r1, [sp, #28]
	bl	FlashProgDpFirstCmd
	mov	r0, r5
	add	r8, r8, #36
	add	r8, r4, r8
	ldr	r3, [sp, #32]
	ldr	r1, [sp, #28]
	add	r3, r6, r3, asl #2
	ldr	r2, [r3, #1172]
	adds	r2, r2, #0
	movne	r2, #1
	bl	FlashWaitReadyEN
	ldr	r1, [r6, #4]
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
.L2296:
	mov	r0, r5
	ldr	r1, [sp, #28]
	bl	FlashProgSecondCmd
	mov	r0, r5
	bl	NandcFlashDeCs
	add	r7, r7, r10
	b	.L2287
.L2337:
	ldr	r8, .L2339+12
	mov	r6, #0
	mov	r7, r5
.L2298:
	ldrb	r3, [r5, #2230]	@ zero_extendqisi2
	cmp	r6, r3
	bcs	.L2338
	uxtb	r0, r6
	bl	FlashWaitCmdDone
	cmp	r9, #1
	bne	.L2299
	ldrb	r3, [r7, #144]	@ zero_extendqisi2
	cmp	r3, #0
	beq	.L2299
	ldrb	r0, [r8, r6, asl #4]	@ zero_extendqisi2
	bl	flash_exit_slc_mode
.L2299:
	add	r6, r6, #1
	b	.L2298
.L2338:
	ldr	r3, [sp, #20]
	cmp	r3, #0
	bne	.L2301
.L2309:
	mov	r0, #0
	b	.L2285
.L2301:
	ldr	r5, .L2339+16
	mov	r6, #0
	mov	r7, r5
.L2302:
	ldr	r3, [sp, #8]
	cmp	r6, r3
	beq	.L2309
	ldr	r3, [r4]
	cmn	r3, #1
	bne	.L2303
	ldr	r1, [r4, #4]
	ldr	r0, .L2339+20
	bl	printk
	b	.L2304
.L2303:
	ldr	r3, [sp, #8]
	mov	r1, r9
	add	r2, sp, #28
	mov	r0, r4
	rsb	r3, r6, r3
	uxtb	r3, r3
	str	r3, [sp]
	add	r3, sp, #32
	bl	LogAddr2PhyAddr
	ldr	r2, [r5, #1724]
	mov	r3, #0
	mov	lr, r4
	str	r3, [r2]
	ldr	r2, [r5, #1728]
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
	ldr	r3, [r5, #1724]
	str	r3, [sp, #44]
	ldr	r3, [r5, #1728]
	str	r3, [sp, #48]
	bl	FlashReadPages
	ldr	r8, [sp, #36]
	cmn	r8, #1
	bne	.L2305
	ldr	r0, .L2339+24
	ldr	r1, [r4, #4]
	bl	printk
	str	r8, [r4]
.L2305:
	ldr	r3, [r4, #12]
	cmp	r3, #0
	beq	.L2306
	ldr	r2, [r3]
	ldr	r3, [r7, #1728]
	ldr	r3, [r3]
	cmp	r2, r3
	beq	.L2306
	ldr	r0, .L2339+28
	ldr	r1, [r4, #4]
	bl	printk
	mvn	r3, #0
	str	r3, [r4]
.L2306:
	ldr	r3, [r4, #8]
	cmp	r3, #0
	beq	.L2304
	ldr	r2, [r3]
	ldr	r3, [r7, #1724]
	ldr	r3, [r3]
	cmp	r2, r3
	beq	.L2304
	ldr	r0, .L2339+32
	ldr	r1, [r4, #4]
	bl	printk
	mvn	r3, #0
	str	r3, [r4]
.L2304:
	add	r6, r6, #1
	add	r4, r4, #36
	b	.L2302
.L2285:
	add	sp, sp, #76
	@ sp needed
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L2340:
	.align	2
.L2339:
	.word	.LANCHOR0
	.word	.LANCHOR1
	.word	.LANCHOR0+1214
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
	ldr	r5, .L2345
	ldr	r3, [r5, #1708]
	cmp	r0, r3
	movcc	r4, #0
	bcc	.L2342
	ldr	r3, [r5, #1716]
	mov	r4, r0
	mov	r1, #165
	add	r0, sp, #40
	mov	r2, #32
	str	r0, [sp, #16]
	str	r3, [sp, #12]
	bl	ftl_memset
	mov	r1, #90
	mov	r2, #8
	ldr	r0, [r5, #1716]
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
.L2342:
	mov	r0, r4
	add	sp, sp, #108
	@ sp needed
	ldmfd	sp!, {r4, r5, pc}
.L2346:
	.align	2
.L2345:
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
	ldr	r5, .L2403
	mov	r1, #1
	ldr	r4, .L2403+4
	ldr	r0, .L2403+8
	ldr	r3, [r5, #1720]
	ldrh	r8, [r4, #128]
	str	r3, [sp, #20]
	ldrh	r3, [r4, #130]
	smulbb	r8, r8, r3
	ldr	r3, [r4, #44]
	ldrb	r3, [r3, #24]	@ zero_extendqisi2
	uxth	r8, r8
	str	r3, [sp, #24]
	ldrh	r3, [r4, #4]
	str	r3, [sp, #16]
	ldrb	r3, [r4]	@ zero_extendqisi2
	ldr	r4, .L2403+4
	cmp	r3, #1
	ldreq	r3, [sp, #16]
	moveq	r3, r3, asl #1
	uxtheq	r3, r3
	streq	r3, [sp, #16]
	bl	printk
	ldr	r0, [r5, #1720]
	mov	r1, #0
	mov	r2, #4096
	bl	ftl_memset
	ldr	r5, .L2403
	mov	r3, r8, lsr #4
	str	r3, [sp, #28]
	mov	r3, #0
	str	r3, [sp, #8]
.L2349:
	ldrb	r7, [sp, #8]	@ zero_extendqisi2
	ldrb	r3, [r4, #2230]	@ zero_extendqisi2
	cmp	r3, r7
	bls	.L2399
	ldr	r2, .L2403+12
	mov	r3, r7, asl #1
	ldrh	r6, [r2, r3]
	cmp	r6, #0
	bne	.L2379
	ldrh	r2, [r4, #136]
	mov	r1, r6
	ldr	r0, [r5, #1688]
	mov	r9, r6
	mov	r2, r2, asl #9
	bl	ftl_memset
	add	r3, r4, r7
	ldrb	r10, [r3, #2232]	@ zero_extendqisi2
	mov	r3, r7, asl #2
	add	fp, r4, r3
	str	r6, [sp, #4]
	str	r3, [sp, #32]
.L2351:
	ldrh	r3, [sp, #4]
	cmp	r3, r8
	str	r3, [sp, #12]
	bcs	.L2361
	mvn	r3, #0
	strb	r3, [sp, #42]
	strb	r3, [sp, #43]
	ldr	r3, [sp, #24]
	tst	r3, #1
	beq	.L2353
	ldr	r3, [fp, #1172]
	add	r2, sp, #42
	mov	r0, r10
	add	r3, r6, r3
	str	r3, [sp, #36]
	mov	r1, r3
	bl	FlashReadSpare
	ldrb	r2, [r4]	@ zero_extendqisi2
	cmp	r2, #1
	ldr	r3, [sp, #36]
	bne	.L2353
	ldr	r1, [r4, #4]
	add	r2, sp, #43
	mov	r0, r10
	add	r1, r3, r1
	bl	FlashReadSpare
	ldrb	r3, [sp, #42]	@ zero_extendqisi2
	ldrb	r2, [sp, #43]	@ zero_extendqisi2
	and	r3, r3, r2
	strb	r3, [sp, #42]
.L2353:
	ldr	r3, [sp, #24]
	tst	r3, #2
	beq	.L2355
	ldr	r3, [r4, #44]
	mov	r0, r10
	add	r2, sp, #43
	ldrh	r1, [r3, #10]
	ldr	r3, [fp, #1172]
	sub	r1, r1, #1
	add	r1, r1, r3
	add	r1, r1, r6
	bl	FlashReadSpare
.L2355:
	ldr	r2, [r4, #44]
	ldrb	r3, [r2, #7]	@ zero_extendqisi2
	cmp	r3, #1
	cmpne	r3, #8
	ldrb	r3, [sp, #42]	@ zero_extendqisi2
	beq	.L2356
	ldrb	r2, [r2, #18]	@ zero_extendqisi2
	cmp	r2, #12
	bne	.L2357
.L2356:
	cmp	r3, #0
	ldrneb	r0, [sp, #43]	@ zero_extendqisi2
	clzne	r0, r0
	movne	r0, r0, lsr #5
	bne	.L2358
	b	.L2378
.L2357:
	cmp	r3, #255
	bne	.L2378
	ldrb	r0, [sp, #43]	@ zero_extendqisi2
	subs	r0, r0, #255
	movne	r0, #1
	b	.L2358
.L2378:
	mov	r0, #1
.L2358:
	ldr	r3, [sp, #24]
	tst	r3, #4
	beq	.L2359
	ldr	r3, .L2403+4
	mov	r0, r10
	ldr	r2, [sp, #32]
	add	r3, r3, r2
	ldr	r1, [r3, #1172]
	add	r1, r6, r1
	bl	SandiskProgTestBadBlock
.L2359:
	cmp	r0, #0
	beq	.L2360
	mov	r1, r7
	ldr	r2, [sp, #4]
	ldr	r0, .L2403+16
	add	r9, r9, #1
	bl	printk
	ldr	r1, [r5, #1688]
	mov	ip, #1
	uxth	r9, r9
	ldr	r3, [sp, #12]
	mov	r0, r3, lsr #5
	and	r3, r3, #31
	ldr	r2, [r1, r0, asl #2]
	orr	r3, r2, ip, asl r3
	ldr	r2, [sp, #28]
	str	r3, [r1, r0, asl #2]
	ldrb	r3, [r4, #2230]	@ zero_extendqisi2
	mul	r3, r3, r2
	cmp	r9, r3
	bgt	.L2361
.L2360:
	ldr	r3, [sp, #4]
	add	r3, r3, #1
	str	r3, [sp, #4]
	ldr	r3, [sp, #16]
	add	r6, r6, r3
	b	.L2351
.L2361:
	mov	r2, r9
	ldr	r0, .L2403+20
	mov	r1, r7
	bl	printk
	ldrb	r3, [r4, #2230]	@ zero_extendqisi2
	ldr	r2, [sp, #28]
	mul	r3, r3, r2
	cmp	r9, r3
	blt	.L2363
	ldrh	r2, [r4, #136]
	mov	r1, #0
	ldr	r0, [r5, #1688]
	mov	r2, r2, asl #9
	bl	ftl_memset
.L2363:
	cmp	r7, #0
	bne	.L2365
	ldr	r3, [r5, #1708]
	mov	r6, r7
	mov	r9, #1
	uxth	r10, r3
.L2366:
	ldr	r3, .L2403+4
	ldrb	r3, [r3, #1]	@ zero_extendqisi2
	cmp	r3, r10
	bls	.L2400
	mov	r0, r10
	bl	FlashTestBlk
	cmp	r0, #0
	beq	.L2367
	mov	r1, r10
	ldr	r0, .L2403+24
	bl	printk
	ldr	r1, [r5, #1688]
	mov	r0, r10, lsr #5
	add	r6, r6, #1
	and	r3, r10, #31
	ldr	r2, [r1, r0, asl #2]
	uxth	r6, r6
	orr	r3, r2, r9, asl r3
	str	r3, [r1, r0, asl #2]
.L2367:
	add	r10, r10, #1
	uxth	r10, r10
	b	.L2366
.L2400:
	sub	fp, r8, #1
	sub	r9, r8, #50
	mov	r10, #1
	uxth	fp, fp
.L2369:
	cmp	fp, r9
	ble	.L2401
	mov	r0, fp
	bl	FlashTestBlk
	cmp	r0, #0
	beq	.L2370
	mov	r1, fp
	ldr	r0, .L2403+24
	bl	printk
	ldr	r1, [r5, #1688]
	mov	r0, fp, lsr #5
	and	r3, fp, #31
	ldr	r2, [r1, r0, asl #2]
	orr	r3, r2, r10, asl r3
	str	r3, [r1, r0, asl #2]
.L2370:
	sub	fp, fp, #1
	uxth	fp, fp
	b	.L2369
.L2401:
	ldr	r3, .L2403+4
	ldr	r2, [r5, #1708]
	ldrb	r3, [r3, #1]	@ zero_extendqisi2
	rsb	r3, r2, r3
	cmp	r6, r3
	bcc	.L2365
	ldrh	r2, [r4, #136]
	mov	r1, #0
	ldr	r0, [r5, #1688]
	mov	r2, r2, asl #9
	bl	ftl_memset
.L2365:
	ldrb	r6, [sp, #8]	@ zero_extendqisi2
	sub	r10, r8, #1
	ldr	r9, .L2403+12
	uxth	r10, r10
	mul	r6, r8, r6
	add	r9, r9, r7, asl #1
.L2373:
	mov	r1, r7
	ldr	r0, .L2403+28
	mov	r2, r10
	bl	printk
	ldr	r1, [r5, #1688]
.L2374:
	mov	r2, r10, lsr #5
	and	r3, r10, #31
	ldr	r2, [r1, r2, asl #2]
	mov	r3, r2, lsr r3
	ands	r3, r3, #1
	subne	r10, r10, #1
	uxthne	r10, r10
	bne	.L2374
.L2402:
	ldr	r1, [sp, #20]
	add	r0, sp, #44
	ldr	r2, .L2403+32
	strh	r10, [r9]	@ movhi
	strh	r10, [r1, #2]	@ movhi
	strh	r2, [r1]	@ movhi
	strh	r3, [r1, #8]	@ movhi
	mov	r1, #1
	ldr	r3, [r5, #1688]
	mov	r2, r1
	str	r3, [sp, #52]
	ldr	r3, [r5, #1720]
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
	bne	.L2373
.L2379:
	ldr	r3, [sp, #8]
	add	r3, r3, #1
	str	r3, [sp, #8]
	b	.L2349
.L2399:
	add	sp, sp, #84
	@ sp needed
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L2404:
	.align	2
.L2403:
	.word	.LANCHOR2
	.word	.LANCHOR0
	.word	.LC121
	.word	.LANCHOR2+1736
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
	ldr	r9, .L2455
	ldr	fp, .L2455+4
	ldr	r5, [r9, #-3616]
	ldrb	r3, [fp, #2240]	@ zero_extendqisi2
	cmp	r5, #0
	str	r3, [sp, #16]
	movne	r0, #0
	bne	.L2406
	ldrb	r3, [fp, #144]	@ zero_extendqisi2
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
.L2407:
	ldr	ip, .L2455+4
	add	r1, ip, #2320
	ldrh	r0, [r1]
	uxth	r1, r9
	cmp	r0, r1
	bls	.L2450
	mul	r0, r8, r1
	ldr	ip, [r10, #-3612]
	mov	r3, #0
	str	r3, [ip, r0]
	ldr	r3, .L2455+4
	add	r1, r3, r1
	ldrb	r0, [r1, #2348]	@ zero_extendqisi2
	ldr	r1, [sp, #4]
	bl	V2P_block
	ldr	r3, [sp]
	mov	r7, r0
	cmp	r3, #0
	beq	.L2408
	bl	IsBlkInVendorPart
	cmp	r0, #0
	bne	.L2409
.L2408:
	mov	r0, r7
	bl	FtlBbmIsBadBlock
	cmp	r0, #0
	addne	r4, r4, #1
	uxthne	r4, r4
	bne	.L2409
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
.L2409:
	add	r9, r9, #1
	b	.L2407
.L2450:
	cmp	r5, #0
	beq	.L2430
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
.L2413:
	uxth	r2, r8
	cmp	r2, r5
	bcs	.L2451
	mul	r2, r9, r8
	ldr	r1, [r10, #-3612]
	add	r0, r1, r2
	ldr	r2, [r1, r2]
	cmn	r2, #1
	bne	.L2414
	ldr	r0, [r0, #4]
	add	r4, r4, #1
	ubfx	r0, r0, #10, #16
	uxth	r4, r4
	bl	FtlBbmMapBadBlock
.L2414:
	add	r8, r8, #1
	b	.L2413
.L2451:
	ldr	r3, [sp]
	cmp	r3, #0
	bne	.L2416
	mov	r3, #6
	uxth	r7, r7
	str	r3, [sp, #12]
	mov	r3, #1
	str	r3, [sp, #8]
	b	.L2417
.L2416:
	movw	r3, #2390
	ldrh	r3, [r6, r3]
	str	r3, [sp, #8]
	ldrb	r3, [r6, #144]	@ zero_extendqisi2
	cmp	r3, #0
	ldreq	r3, [sp, #8]
	moveq	r7, #1
	movne	r7, #1
	strne	r7, [sp, #12]
	moveq	r3, r3, lsr #2
	streq	r3, [sp, #12]
.L2417:
	ldr	r10, .L2455
	mov	r5, #0
	mov	r8, r10
.L2426:
	mov	fp, #0
	mov	r6, fp
.L2418:
	ldr	r9, .L2455+4
	add	r3, r9, #2320
	ldrh	r1, [r3]
	uxth	r3, fp
	cmp	r1, r3
	bls	.L2452
	mov	r2, #36
	ldr	r0, [r10, #-3612]
	mul	r1, r2, r3
	mov	r2, #0
	str	r2, [r0, r1]
	ldr	r2, .L2455+4
	ldr	r1, [sp, #4]
	add	r3, r2, r3
	ldrb	r0, [r3, #2348]	@ zero_extendqisi2
	bl	V2P_block
	ldr	r3, [sp]
	mov	r9, r0
	cmp	r3, #0
	beq	.L2419
	bl	IsBlkInVendorPart
	cmp	r0, #0
	bne	.L2420
.L2419:
	mov	r0, r9
	bl	FtlBbmIsBadBlock
	cmp	r0, #0
	bne	.L2420
	ldr	r1, [r8, #-3612]
	mov	r3, #36
	add	r9, r5, r9, asl #10
	mla	r1, r3, r6, r1
	ldr	r3, [r8, #-508]
	str	r3, [r1, #8]
	ldr	r3, .L2455+8
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
.L2420:
	add	fp, fp, #1
	b	.L2418
.L2452:
	cmp	r6, #0
	beq	.L2430
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
	ldr	r9, .L2455
.L2423:
	uxth	r3, fp
	cmp	r3, r6
	bcs	.L2453
	mul	r3, r2, fp
	ldr	r1, [r8, #-3612]
	add	r0, r1, r3
	ldr	r3, [r1, r3]
	cmp	r3, #0
	beq	.L2424
	ldr	r0, [r0, #4]
	add	r4, r4, #1
	str	r2, [sp, #20]
	ubfx	r0, r0, #10, #16
	uxth	r4, r4
	bl	FtlBbmMapBadBlock
	ldr	r2, [sp, #20]
.L2424:
	add	fp, fp, #1
	b	.L2423
.L2453:
	ldr	r3, [sp, #12]
	add	r5, r5, r3
	ldr	r3, [sp, #8]
	uxth	r5, r5
	cmp	r5, r3
	bcc	.L2426
	mov	r5, #0
	mov	r8, #36
.L2427:
	uxth	r3, r5
	cmp	r3, r6
	ldr	r3, [sp]
	bcs	.L2454
	cmp	r3, #0
	beq	.L2428
	mul	r3, r8, r5
	ldr	r2, [r9, #-3612]
	add	r1, r2, r3
	ldr	r3, [r2, r3]
	cmp	r3, #0
	bne	.L2428
	ldr	r0, [r1, #4]
	mov	r1, #1
	ubfx	r0, r0, #10, #16
	bl	FtlFreeSysBlkQueueIn
.L2428:
	add	r5, r5, #1
	b	.L2427
.L2454:
	adds	r1, r3, #0
	ldr	r3, [sp, #4]
	movne	r1, #1
	cmp	r3, #63
	orrls	r1, r1, #1
	cmp	r1, #0
	beq	.L2430
	ldr	r0, [r9, #-3612]
	mov	r1, r7
	mov	r2, r6
	bl	FlashEraseBlocks
.L2430:
	mov	r0, r4
.L2406:
	add	sp, sp, #28
	@ sp needed
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L2456:
	.align	2
.L2455:
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
	ldr	r4, .L2475
	ldr	r5, [r4, #-3616]
	cmp	r5, #0
	bne	.L2459
	ldr	r3, [r4, #-500]
	mov	r1, r5
	ldr	r2, .L2475+4
	sub	r9, r4, #436
	ldr	r0, [r4, #-524]
	str	r3, [r4, #1764]
	movw	r3, #2398
	ldrh	r2, [r2, r3]
	str	r0, [r4, #1760]
	bl	ftl_memset
	ldr	r6, .L2475+8
	sub	r8, r6, #134
.L2460:
	ldrh	r3, [r8]
	ldr	r7, .L2475+4
	cmp	r5, r3
	bge	.L2474
	ldrh	r2, [r9]
	ldr	r3, [r4, #1760]
	ldr	r1, [r6, #4]!
	mul	r0, r2, r5
	mov	r2, r2, asl #2
	add	r5, r5, #1
	add	r0, r3, r0, asl #2
	bl	ftl_memcpy
	b	.L2460
.L2474:
	ldr	r9, [r4, #1764]
	mov	r1, #255
	mov	r2, #16
	ldr	r5, .L2475+12
	ldr	r10, .L2475
	mov	r6, #0
	mov	r0, r9
	bl	ftl_memset
	ldr	r3, .L2475+16
	mov	r8, r5
	strh	r3, [r9]	@ movhi
	ldr	r3, [r7, #2460]
	str	r3, [r9, #4]
	movw	r3, #2452
	ldrh	r3, [r7, r3]
	strh	r3, [r9, #2]	@ movhi
	ldrh	r3, [r5, #4]
	strh	r3, [r9, #8]	@ movhi
	ldrh	r3, [r5, #6]
	strh	r3, [r9, #10]	@ movhi
	ldr	r3, [r7, #2316]
	strh	r3, [r9, #12]	@ movhi
	str	r6, [sp, #12]
.L2462:
	ldr	r3, [r4, #-524]
	mov	fp, #0
	ldrh	r1, [r5]
	ldrh	r2, [r5, #2]
	str	r3, [r4, #1760]
	ldr	r3, [r4, #-500]
	str	fp, [r4, #1752]
	str	r3, [r4, #1764]
	orr	r3, r2, r1, asl #10
	ldrh	r0, [r9, #10]
	str	r3, [r4, #1756]
	ldrh	r3, [r5, #4]
	str	r0, [sp]
	ldr	r0, .L2475+20
	bl	printk
	ldr	r3, .L2475+24
	ldrh	r2, [r5, #2]
	ldrh	r3, [r3]
	sub	r3, r3, #1
	cmp	r2, r3
	blt	.L2463
	ldr	r3, [r7, #2460]
	mov	r1, #1
	ldrh	r2, [r8]
	add	r3, r3, #1
	ldr	r0, [r10, #-3612]
	str	r3, [r7, #2460]
	str	r3, [r9, #4]
	ldrh	r3, [r8, #4]
	strh	r2, [r9, #8]	@ movhi
	strh	r2, [r8, #4]	@ movhi
	mov	r2, r1
	strh	r3, [r8]	@ movhi
	mov	r3, r3, asl #10
	str	r3, [r10, #1756]
	str	r3, [r0, #4]
	strh	fp, [r8, #2]	@ movhi
	bl	FlashEraseBlocks
.L2463:
	mov	r1, #1
	ldr	r0, .L2475+28
	mov	r3, r1
	mov	r2, r1
	bl	FlashProgPages
	ldrh	r3, [r5, #2]
	ldr	fp, .L2475
	add	r3, r3, #1
	strh	r3, [r5, #2]	@ movhi
	ldr	r3, [r4, #1752]
	cmn	r3, #1
	bne	.L2464
	add	r6, r6, #1
	ldr	r0, .L2475+32
	ldr	r1, [r10, #1756]
	uxth	r6, r6
	bl	printk
	cmp	r6, #3
	bls	.L2462
	ldr	r0, .L2475+36
	mov	r2, r6
	ldr	r1, [fp, #1756]
	bl	printk
	mov	r3, #1
	str	r3, [fp, #-3616]
	b	.L2459
.L2464:
	ldr	r2, [sp, #12]
	add	r2, r2, #1
	str	r2, [sp, #12]
	cmp	r2, #1
	beq	.L2462
	cmp	r3, #256
	beq	.L2462
.L2459:
	mov	r0, #0
	add	sp, sp, #20
	@ sp needed
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L2476:
	.align	2
.L2475:
	.word	.LANCHOR2
	.word	.LANCHOR0
	.word	.LANCHOR0+2476
	.word	.LANCHOR0+2452
	.word	-3887
	.word	.LC126
	.word	.LANCHOR0+2390
	.word	.LANCHOR2+1752
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
	ldr	r4, .L2532
	ldr	r3, [r4, #-3616]
	cmp	r3, #0
	bne	.L2478
	mov	r5, r0
	mov	r8, r4
.L2479:
	ldr	r3, .L2532+4
	ldr	r10, .L2532+8
	cmp	r5, r3
	ldrb	r2, [r5, #8]	@ zero_extendqisi2
	bne	.L2480
	ldrh	r3, [r5, #-100]
	ldr	ip, [r8, #-2708]
	mov	r0, r3, lsr #1
	mul	lr, ip, r3
	add	r1, r0, #1
	add	r1, r1, lr, lsr #2
	ldr	lr, [r10, #2244]
	cmp	lr, #0
	uxth	r1, r1
	beq	.L2481
	ldr	lr, [r8, #-3308]
	cmp	lr, #39
	bhi	.L2481
	cmp	lr, #2
	bls	.L2506
	cmp	ip, #0
	movne	r3, #0
	andeq	r3, r3, #1
	cmp	r3, #0
	moveq	r1, r0
	beq	.L2481
	b	.L2506
.L2480:
	cmp	r2, #1
	bne	.L2506
	ldr	r1, .L2532+12
	ldrh	r1, [r1]
	cmp	r1, #1
	beq	.L2506
	ldrb	r1, [r10, #144]	@ zero_extendqisi2
	cmp	r1, #0
	bne	.L2506
	ldr	r0, [r10, #2244]
	ldrh	r3, [r3, #-100]
	cmp	r0, #0
	mov	r1, r3, lsr #3
	beq	.L2481
	ldr	r0, [r8, #-3308]
	cmp	r0, #1
	rsbls	r3, r3, r3, asl #3
	ubfxls	r1, r3, #3, #16
.L2481:
	cmp	r1, #0
	subne	r1, r1, #1
	uxthne	r1, r1
	b	.L2482
.L2506:
	mov	r1, #0
.L2482:
	ldr	r0, .L2532+16
	bl	List_pop_index_node
	ldr	r2, .L2532+20
	ldrh	r3, [r2]
	sub	r3, r3, #1
	strh	r3, [r2]	@ movhi
	ldr	r3, .L2532+24
	ldrh	r3, [r3]
	uxth	r9, r0
	cmp	r3, r9
	bls	.L2479
	ldr	r3, [r8, #-3544]
	mov	r7, r9, asl #1
	ldrh	r6, [r3, r7]
	cmp	r6, #0
	bne	.L2479
	strh	r9, [r5]	@ movhi
	mov	r0, r5
	bl	make_superblock
	ldrb	r3, [r5, #7]	@ zero_extendqisi2
	cmp	r3, #0
	beq	.L2484
	ldr	r2, .L2532+28
	add	ip, r5, #14
	ldr	r0, [r8, #-3612]
	ldrh	lr, [r2]
	mov	r2, #36
	mov	r3, r0
	mla	r1, r2, lr, r0
	mov	lr, r6
	str	r1, [sp]
	b	.L2485
.L2484:
	ldr	r3, [r8, #-3544]
	b	.L2527
.L2485:
	ldr	r1, [sp]
	cmp	r3, r1
	beq	.L2529
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
	b	.L2485
.L2529:
	ldr	r3, .L2532+32
	ldr	r2, [r10, #2244]
	rsb	r3, r3, r5
	clz	r3, r3
	cmp	r2, #0
	mov	r3, r3, lsr #5
	moveq	r3, #0
	cmp	r3, #0
	beq	.L2488
	ldr	r3, [r8, #-3608]
	ldrh	r3, [r3, r7]
	cmp	r3, #40
	movhi	r3, #0
	strhib	r3, [r8, #-3516]
.L2488:
	ldrb	r3, [r5, #8]	@ zero_extendqisi2
	ldr	r2, [r4, #-3608]
	cmp	r3, #0
	ldr	fp, .L2532+36
	ldrh	r3, [r2, r7]
	bne	.L2489
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
	b	.L2491
.L2489:
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
.L2491:
	ldr	r3, [r4, #-3608]
	ldr	r2, [r4, #-3312]
	ldr	r0, [r4, #-3324]
	ldrh	r3, [r3, r7]
	cmp	r3, r2
	ldrh	r2, [fp]
	strhi	r3, [r8, #-3312]
	ldr	r3, [r4, #-3320]
	mla	r0, r0, r2, r3
	ldr	r3, .L2532+24
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
.L2493:
	add	r2, r2, #36
	cmp	r2, r3
	ldrne	r1, [r2, #-36]
	bicne	r1, r1, #1020
	bicne	r1, r1, #3
	strne	r1, [r2, #-36]
	bne	.L2493
.L2530:
	ldrb	r3, [r10, #144]	@ zero_extendqisi2
	cmp	r3, #0
	beq	.L2495
	ldrb	r3, [r5, #8]	@ zero_extendqisi2
	mov	r2, r6
	ldr	r0, [r4, #-3612]
	cmp	r3, #1
	moveq	r1, #0
	movne	r1, #1
	bl	FlashEraseBlocks
.L2495:
	ldrb	r1, [r5, #8]	@ zero_extendqisi2
	mov	r2, r6
	ldr	r0, [r4, #-3612]
	mov	fp, #0
	bl	FlashEraseBlocks
	add	r1, r5, #16
	mov	r2, fp
	mov	ip, #36
.L2497:
	uxth	r3, fp
	cmp	r3, r6
	bcs	.L2531
	mul	r3, ip, fp
	ldr	lr, [r4, #-3612]
	add	r0, lr, r3
	ldr	r3, [lr, r3]
	cmn	r3, #1
	bne	.L2498
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
.L2498:
	add	fp, fp, #1
	add	r1, r1, #2
	b	.L2497
.L2531:
	cmp	r2, #0
	beq	.L2500
	mov	r0, r9
	bl	update_multiplier_value
	bl	FtlBbmTblFlush
.L2500:
	ldrb	r3, [r5, #7]	@ zero_extendqisi2
	cmp	r3, #0
	bne	.L2501
	ldr	r3, [r4, #-3544]
.L2527:
	mvn	r2, #0
	strh	r2, [r3, r7]	@ movhi
	b	.L2479
.L2501:
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
.L2478:
	mov	r0, #0
	add	sp, sp, #20
	@ sp needed
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L2533:
	.align	2
.L2532:
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
	ldr	r3, .L2548
	stmfd	sp!, {r0, r1, r2, r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.save {r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.pad #12
	ldrh	r2, [r3, #-2]
	cmp	r2, #0
	beq	.L2536
	sub	r10, r3, #2
	mov	r7, r0
	mov	r9, #0
	mov	r5, r3
	mov	r8, r10
.L2535:
	ldr	r3, .L2548+4
	add	r2, r3, #2320
	ldrh	r1, [r2]
	uxth	r2, r9
	cmp	r1, r2
	bls	.L2545
	add	r3, r3, r2
	mov	r1, r7
	mov	fp, #0
	ldrb	r0, [r3, #2348]	@ zero_extendqisi2
	bl	V2P_block
	mov	r6, r0
.L2537:
	ldrh	r3, [r10]
	uxth	r4, fp
	cmp	r3, r4
	bls	.L2546
	mov	r3, r4, asl #1
	add	ip, r5, r3
	ldrh	r3, [r5, r3]
	cmp	r3, r6
	bne	.L2538
	mov	r1, r6
	ldr	r0, .L2548+8
	str	ip, [sp, #4]
	bl	printk
	mov	r0, r6
	bl	FtlBbmMapBadBlock
	bl	FtlBbmTblFlush
	ldrh	r2, [r10]
	ldr	ip, [sp, #4]
	mov	r3, ip
.L2539:
	cmp	r4, r2
	ldrcch	r1, [r3, #2]
	addcc	r4, r4, #1
	uxthcc	r4, r4
	strcch	r1, [r3], #2	@ movhi
	bcc	.L2539
.L2547:
	sub	r2, r2, #1
	strh	r2, [r8]	@ movhi
.L2538:
	add	fp, fp, #1
	b	.L2537
.L2546:
	add	r9, r9, #1
	b	.L2535
.L2545:
	bl	FtlGcReFreshBadBlk
.L2536:
	mov	r0, #0
	add	sp, sp, #12
	@ sp needed
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L2549:
	.align	2
.L2548:
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
	ldr	r2, .L2560
	stmfd	sp!, {r3, r4, r5, lr}
	.save {r3, r4, r5, lr}
	mov	r3, r0, asl #1
	ldr	r1, [r2, #-3544]
	mov	r4, r0
	ldrh	r3, [r1, r3]
	cmp	r3, #0
	bne	.L2551
	sub	r1, r2, #3280
	ldrh	r0, [r1, #-4]
	cmp	r0, r4
	mvneq	r3, #0
	streqh	r3, [r1, #-4]	@ movhi
	beq	.L2553
	sub	r1, r2, #3520
	ldrh	r1, [r1, #-4]
	cmp	r1, r4
	beq	.L2559
	sub	r1, r2, #3472
	ldrh	r1, [r1, #-4]
	cmp	r1, r4
	beq	.L2559
	sub	r2, r2, #3424
	ldrh	r2, [r2, #-4]
	cmp	r2, r4
	beq	.L2559
.L2553:
	ldr	r5, .L2560+4
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
.L2551:
	bl	List_update_data_list
.L2559:
	mov	r0, #0
	ldmfd	sp!, {r3, r4, r5, pc}
.L2561:
	.align	2
.L2560:
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
	beq	.L2563
	ldr	r6, .L2573
	mov	r5, r0, asl #1
	ldr	r3, [r6, #-3544]
	ldrh	r2, [r3, r5]
	cmp	r2, #0
	subne	r2, r2, #1
	strneh	r2, [r3, r5]	@ movhi
	bne	.L2563
	mov	r1, r4
	ldr	r0, .L2573+4
	bl	printk
	ldr	r3, [r6, #-3544]
	sub	r7, r6, #3520
	mov	r2, #32
	sub	r0, r7, #12
	mov	r1, r4
	strh	r2, [r3, r5]	@ movhi
	bl	test_node_in_list
	cmp	r0, #0
	beq	.L2565
	mov	r1, r4
	sub	r0, r7, #12
	bl	List_remove_node
	ldrh	r3, [r7, #-8]
	mov	r0, r4
	sub	r3, r3, #1
	strh	r3, [r7, #-8]	@ movhi
	bl	INSERT_DATA_LIST
	ldr	r3, [r6, #-3544]
	ldr	r0, .L2573+8
	mov	r1, r4
	ldrh	r2, [r3, r5]
	bl	printk
.L2565:
	mov	r0, r4
	bl	FtlGcRefreshBlock
	b	.L2568
.L2563:
	ldr	r5, .L2573+12
	movw	r3, #65535
	ldrh	r0, [r5]
	cmp	r0, r3
	streqh	r4, [r5]	@ movhi
	beq	.L2568
	cmp	r0, r4
	beq	.L2568
	bl	update_vpc_list
	strh	r4, [r5]	@ movhi
	adds	r0, r0, #0
	movne	r0, #1
	ldmfd	sp!, {r3, r4, r5, r6, r7, pc}
.L2568:
	mov	r0, #0
	ldmfd	sp!, {r3, r4, r5, r6, r7, pc}
.L2574:
	.align	2
.L2573:
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
	ldr	r5, .L2588
	ldr	r6, .L2588+4
	add	r3, r0, r3, asl #1
	add	r7, r5, #2320
	ldrh	r3, [r3, #16]
.L2579:
	movw	r1, #65535
	cmp	r3, r1
	bne	.L2587
.L2581:
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
	b	.L2579
.L2587:
	ldrb	r2, [r4, #8]	@ zero_extendqisi2
	cmp	r2, #1
	bne	.L2582
	ldrb	r3, [r5, #144]	@ zero_extendqisi2
	cmp	r3, #0
	bne	.L2582
	ldrh	r3, [r4, #2]
	mov	r3, r3, asl #1
	ldrh	r3, [r6, r3]
	cmp	r3, r1
	bne	.L2582
	ldrh	r3, [r4, #4]
	ldrh	r0, [r4]
	sub	r3, r3, #1
	strh	r3, [r4, #4]	@ movhi
	bl	decrement_vpc_count
	ldrh	r2, [r4, #4]
	cmp	r2, #0
	bne	.L2581
	ldrh	r3, [r4, #2]
	strb	r2, [r4, #6]
	add	r3, r3, #1
	strh	r3, [r4, #2]	@ movhi
	ldmfd	sp!, {r3, r4, r5, r6, r7, pc}
.L2582:
	ldr	r1, .L2588
	ldrb	r3, [r1, #144]	@ zero_extendqisi2
	cmp	r3, #0
	ldmeqfd	sp!, {r3, r4, r5, r6, r7, pc}
	cmp	r2, #1
	ldmnefd	sp!, {r3, r4, r5, r6, r7, pc}
	movw	r3, #2390
	ldrh	r2, [r4, #2]
	ldrh	r3, [r1, r3]
	cmp	r2, r3
	ldmccfd	sp!, {r3, r4, r5, r6, r7, pc}
	ldr	r2, .L2588+8
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
.L2589:
	.align	2
.L2588:
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
	ldr	r7, .L2608
	ldr	r9, .L2608+4
	add	r3, r0, r3, asl #1
	sub	r8, r7, #2320
	ldrh	r2, [r3, #16]
.L2591:
	movw	r1, #65535
	cmp	r2, r1
	ldr	r6, .L2608
	bne	.L2607
.L2592:
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
	b	.L2591
.L2607:
	ldrb	r3, [r4, #8]	@ zero_extendqisi2
	ldrh	r5, [r4, #2]
	cmp	r3, #1
	ldrh	r3, [r4, #4]
	bne	.L2594
	ldrb	r0, [r8, #144]	@ zero_extendqisi2
	cmp	r0, #0
	bne	.L2594
	mov	r0, r5, asl #1
	ldrh	r0, [r9, r0]
	cmp	r0, r1
	bne	.L2594
	sub	r3, r3, #1
	ldrh	r0, [r4]
	strh	r3, [r4, #4]	@ movhi
	bl	decrement_vpc_count
	b	.L2592
.L2594:
	ldr	r7, .L2608+8
	orr	r5, r5, r2, asl #10
	sub	r3, r3, #1
	strh	r3, [r4, #4]	@ movhi
.L2595:
	ldrb	r3, [r4, #6]	@ zero_extendqisi2
	movw	r2, #65535
	ldrh	r0, [r6]
.L2597:
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
	beq	.L2597
	strb	r3, [r4, #6]
	ldrb	r3, [r4, #8]	@ zero_extendqisi2
	cmp	r3, #1
	bne	.L2602
	ldrb	r3, [r7, #144]	@ zero_extendqisi2
	cmp	r3, #0
	bne	.L2599
	ldrh	r3, [r4, #2]
	ldr	r2, .L2608+4
	mov	r3, r3, asl #1
	ldrh	r2, [r2, r3]
	movw	r3, #65535
	cmp	r2, r3
	bne	.L2599
	ldrh	r3, [r4, #4]
	cmp	r3, #0
	beq	.L2599
	sub	r3, r3, #1
	ldrh	r0, [r4]
	strh	r3, [r4, #4]	@ movhi
	bl	decrement_vpc_count
	b	.L2595
.L2599:
	ldr	r1, .L2608+8
	ldrb	r3, [r1, #144]	@ zero_extendqisi2
	cmp	r3, #0
	beq	.L2602
	movw	r3, #2390
	ldrh	r2, [r4, #2]
	ldrh	r3, [r1, r3]
	cmp	r2, r3
	bcc	.L2602
	ldr	r2, .L2608+12
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
.L2602:
	mov	r0, r5
	ldmfd	sp!, {r3, r4, r5, r6, r7, r8, r9, pc}
.L2609:
	.align	2
.L2608:
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
	ldr	r4, .L2628
	ldr	r3, [r4, #-3616]
	cmp	r3, #0
	bne	.L2612
	ldr	r2, [r4, #-524]
	sub	r6, r4, #3600
	ldr	r7, [r4, #-500]
	mov	r1, #255
	ldr	r5, .L2628+4
	str	r2, [r4, #1760]
	sub	r2, r4, #3296
	str	r7, [r4, #1764]
	ldrh	r2, [r2, #-4]
	str	r3, [r7, #12]
	strh	r2, [r7, #2]	@ movhi
	ldr	r2, .L2628+8
	strh	r2, [r7]	@ movhi
	ldr	r2, [r4, #-3292]
	stmib	r7, {r2, r3}
	ldr	r3, .L2628+12
	ldrh	r2, [r6, #78]
	str	r3, [r4, #-3600]
	ldr	r3, .L2628+16
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
	ldr	r0, [r4, #1760]
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
	ldr	r0, [r4, #1760]
	bl	ftl_memcpy
	ldrh	r2, [r5, r6]
	ldr	r0, [r4, #1760]
	ldr	r1, [r4, #-3544]
	mov	r2, r2, asl #1
	add	r0, r0, #48
	bl	ftl_memcpy
	ldrh	r2, [r5, r6]
	ldr	r0, [r4, #1760]
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
	beq	.L2613
	ldrh	r2, [r5, r6]
	ldr	r0, [r4, #1760]
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
.L2613:
	mov	r0, #0
	ldr	r8, .L2628
	bl	FtlUpdateVaildLpn
	ldr	r10, .L2628+20
	mov	r6, #0
	movw	r9, #65535
.L2614:
	ldr	r3, [r4, #-524]
	ldrh	r2, [r10]
	ldr	fp, .L2628+24
	str	r3, [r4, #1760]
	ldr	r3, [r4, #-500]
	ldrh	r1, [r10, #2]
	str	r3, [r4, #1764]
	orr	r3, r1, r2, asl #10
	str	r3, [r4, #1756]
	ldrh	r3, [fp]
	sub	r3, r3, #1
	cmp	r1, r3
	blt	.L2615
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
	str	r2, [r8, #1756]
	str	r3, [r7, #4]
	strh	r0, [r7, #2]	@ movhi
.L2615:
	ldrb	r3, [r5]	@ zero_extendqisi2
	cmp	r3, #0
	beq	.L2616
	ldr	r3, .L2628+28
	ldr	r0, [r4, #-524]
	ldrh	r1, [r3]
	bl	js_hash
	str	r0, [r7, #12]
.L2616:
	mov	r1, #1
	ldr	r0, .L2628+32
	mov	r2, r1
	mov	r3, r1
	bl	FlashProgPages
	ldr	r3, .L2628+20
	ldr	r2, .L2628+20
	ldrh	r3, [r3, #2]
	add	r3, r3, #1
	uxth	r3, r3
	strh	r3, [r2, #2]	@ movhi
	ldr	r2, [r4, #1752]
	cmn	r2, #1
	bne	.L2617
	cmp	r3, #1
	add	r6, r6, #1
	ldreqh	r3, [fp]
	uxth	r6, r6
	subeq	r3, r3, #1
	streqh	r3, [r10, #2]	@ movhi
	cmp	r6, #3
	bls	.L2614
	ldr	r0, .L2628+36
	mov	r2, r6
	ldr	r1, [r4, #1756]
	bl	printk
	mov	r3, #1
	str	r3, [r4, #-3616]
	b	.L2612
.L2617:
	cmp	r2, #256
	cmpne	r3, #1
	beq	.L2614
	movw	r3, #65535
	cmp	r9, r3
	beq	.L2612
	mov	r0, r9
	mov	r1, #1
	bl	FtlFreeSysBlkQueueIn
.L2612:
	mov	r0, #0
	ldmfd	sp!, {r3, r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L2629:
	.align	2
.L2628:
	.word	.LANCHOR2
	.word	.LANCHOR0
	.word	-3932
	.word	1179929683
	.word	1342177379
	.word	.LANCHOR2-3300
	.word	.LANCHOR0+2390
	.word	.LANCHOR0+2398
	.word	.LANCHOR2+1752
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
	ldr	r5, .L2647
	ldr	r3, [r5, #-3616]
	cmp	r3, #0
	bne	.L2630
	ldr	r8, .L2647+4
	ldrb	r6, [r8, #144]	@ zero_extendqisi2
	cmp	r6, #0
	beq	.L2646
	ldrb	r6, [r0, #8]	@ zero_extendqisi2
	cmp	r6, #1
	ldreqh	fp, [r0, #4]
	beq	.L2632
	mov	r6, r3
.L2646:
	mov	fp, #12
.L2632:
	ldr	r7, [r5, #-500]
	mvn	r3, #0
	str	r3, [sp, #20]
	mov	r9, #0
	ldr	r3, [r5, #-524]
	movw	r2, #61589
	str	r7, [sp, #16]
	mov	r4, r0
	ldr	r10, .L2647
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
	ldr	r2, .L2647+8
	ldr	r3, [r5, #-524]
	str	r2, [r3, #4]
.L2633:
	subs	fp, fp, #1
	bcc	.L2636
	ldrh	r3, [r4, #4]
	cmp	r3, #0
	bne	.L2634
.L2636:
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
	b	.L2630
.L2634:
	mov	r0, r4
	bl	get_new_active_ppa
	cmn	r0, #1
	str	r0, [sp, #8]
	beq	.L2636
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
	b	.L2633
.L2630:
	add	sp, sp, #44
	@ sp needed
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L2648:
	.align	2
.L2647:
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
	ldr	r8, .L2663
	cmp	r2, r3
	blt	.L2650
	uxth	r0, r0
	mov	r0, r0, asl #1
	ldrh	r10, [r5, r0]
	cmp	r10, #0
	beq	.L2650
	ldr	r3, [r4, #32]
	cmp	r3, #0
	bne	.L2650
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
	bcc	.L2651
	mov	r0, r4
	bl	ftl_map_blk_alloc_new_blk
.L2651:
	ldr	r5, .L2663+4
	mov	fp, #0
.L2652:
	ldrh	r3, [r4, #6]
	uxth	r6, fp
	cmp	r3, r6
	bls	.L2662
	ldr	r3, [r7, r6, asl #2]
	add	ip, r7, r6, asl #2
	cmp	r10, r3, lsr #10
	bne	.L2653
	ldr	r3, [r5, #-520]
	mov	r1, #1
	ldr	r9, [r5, #-500]
	mov	r2, r1
	ldr	r0, .L2663+8
	str	r3, [r5, #1760]
	str	r9, [r5, #1764]
	ldr	r3, [r7, r6, asl #2]
	str	ip, [sp, #4]
	str	r3, [r5, #1756]
	bl	FlashReadPages
	ldr	r3, [r5, #1752]
	cmn	r3, #1
	ldr	r3, .L2663+4
	ldr	ip, [sp, #4]
	bne	.L2654
.L2656:
	mov	r2, #0
	ldr	r0, .L2663+12
	str	r2, [ip]
	ldr	r1, [r3, #1756]
	ldrh	r2, [r9, #8]
	str	r3, [sp, #4]
	bl	printk
	mov	r2, #1
	ldr	r3, [sp, #4]
	str	r2, [r3, #-3616]
	b	.L2655
.L2654:
	ldrh	r1, [r9, #8]
	cmp	r1, r6
	bne	.L2656
	ldrh	r0, [r9]
	ldrh	r2, [r4, #4]
	cmp	r0, r2
	bne	.L2656
	mov	r0, r4
	ldr	r2, [r5, #1760]
	bl	FtlMapWritePage
.L2653:
	add	fp, fp, #1
	b	.L2652
.L2662:
	mov	r0, r10
	mov	r1, #1
	bl	FtlFreeSysBlkQueueIn
	mov	r3, #0
	str	r3, [r4, #32]
.L2650:
	movw	r3, #2390
	ldrh	r2, [r4, #2]
	ldrh	r3, [r8, r3]
	cmp	r2, r3
	bcc	.L2655
	mov	r0, r4
	bl	ftl_map_blk_alloc_new_blk
.L2655:
	mov	r0, #0
	add	sp, sp, #12
	@ sp needed
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L2664:
	.align	2
.L2663:
	.word	.LANCHOR0
	.word	.LANCHOR2
	.word	.LANCHOR2+1752
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
	ldr	r5, .L2677
	ldr	r7, [r0, #12]
	ldr	r9, [r0, #24]
	ldr	r6, [r5, #-3616]
	cmp	r6, #0
	bne	.L2666
	ldrh	r3, [r0]
	movw	r2, #65535
	mov	r4, r0
	cmp	r3, r2
	bne	.L2667
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
	b	.L2666
.L2667:
	mov	r3, r3, asl #1
	ldr	r10, .L2677+4
	mov	r1, #255
	ldrh	r8, [r7, r3]
	ldrh	r3, [r0, #2]
	ldr	r7, [r5, #-500]
	orr	r3, r3, r8, asl #10
	str	r3, [r5, #1756]
	ldr	r3, [r5, #-524]
	str	r7, [r5, #1764]
	str	r3, [r5, #1760]
	ldr	r3, [r0, #28]
	str	r3, [r7, #4]
	ldr	r3, .L2677+8
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
.L2668:
	ldrh	r1, [r4, #6]
	uxth	r3, r2
	cmp	r1, r3
	bls	.L2676
	ldr	r1, [r9, r3, asl #2]
	cmp	r8, r1, lsr #10
	bne	.L2669
	add	r6, r6, #1
	ldr	r1, [r5, #-524]
	uxth	r6, r6
	str	r3, [r1, r6, asl #3]
	ldr	r1, [r9, r3, asl #2]
	ldr	r3, [r5, #-524]
	add	r3, r3, r6, asl #3
	str	r1, [r3, #4]
.L2669:
	add	r2, r2, #1
	b	.L2668
.L2676:
	ldrb	r3, [r10]	@ zero_extendqisi2
	cmp	r3, #0
	beq	.L2671
	ldr	r2, .L2677+4
	movw	r3, #2398
	ldr	r0, [r5, #1760]
	ldrh	r1, [r2, r3]
	bl	js_hash
	str	r0, [r7, #12]
.L2671:
	mov	r1, #1
	mov	r3, #0
	ldr	r0, .L2677+12
	mov	r2, r1
	bl	FlashProgPages
	ldrh	r3, [r4, #2]
	mov	r0, r4
	add	r3, r3, #1
	strh	r3, [r4, #2]	@ movhi
	bl	ftl_map_blk_gc
.L2666:
	mov	r0, #0
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, pc}
.L2678:
	.align	2
.L2677:
	.word	.LANCHOR2
	.word	.LANCHOR0
	.word	-1291
	.word	.LANCHOR2+1752
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
	ldr	r10, .L2698
	mov	r8, r1
	ldr	fp, .L2698+4
	mov	r5, #0
	str	r2, [sp]
	mov	r9, r10
.L2680:
	ldr	r3, [r10, #-3348]
	ldr	r6, .L2698
	add	r3, r3, #1
	str	r3, [r10, #-3348]
	ldrh	r3, [fp]
	ldrh	r2, [r4, #2]
	sub	r3, r3, #1
	cmp	r2, r3
	bge	.L2681
	ldrh	r2, [r4]
	movw	r3, #65535
	cmp	r2, r3
	bne	.L2682
.L2681:
	mov	r0, r4
	bl	Ftl_write_map_blk_to_last_page
.L2682:
	ldr	r1, [r9, #-3616]
	cmp	r1, #0
	bne	.L2683
	ldrh	r3, [r4]
	ldr	r2, [r4, #12]
	ldr	r0, [r10, #-500]
	mov	r3, r3, asl #1
	ldrh	r7, [r2, r3]
	mov	r2, #16
	ldrh	r3, [r4, #2]
	str	r0, [r10, #1764]
	orr	r3, r3, r7, asl #10
	str	r3, [r10, #1756]
	ldr	r3, [sp]
	str	r3, [r10, #1760]
	bl	ftl_memset
	ldr	r2, [r4, #28]
	ldr	r3, [r10, #1764]
	str	r2, [r3, #4]
	strh	r8, [r3, #8]	@ movhi
	ldrh	r2, [r4, #4]
	strh	r7, [r3, #2]	@ movhi
	strh	r2, [r3]	@ movhi
	ldr	r2, .L2698+8
	ldrb	r2, [r2]	@ zero_extendqisi2
	cmp	r2, #0
	beq	.L2684
	ldr	r2, .L2698+12
	ldr	r0, [r10, #1760]
	str	r3, [sp, #4]
	ldrh	r1, [r2]
	bl	js_hash
	ldr	r3, [sp, #4]
	str	r0, [r3, #12]
.L2684:
	mov	r1, #1
	ldr	r0, .L2698+16
	mov	r2, r1
	mov	r3, r1
	bl	FlashProgPages
	ldrh	r3, [r4, #2]
	add	r3, r3, #1
	uxth	r3, r3
	strh	r3, [r4, #2]	@ movhi
	ldr	r2, [r9, #1752]
	cmn	r2, #1
	bne	.L2685
	ldr	r0, .L2698+20
	add	r5, r5, #1
	ldr	r1, [r10, #1756]
	bl	printk
	ldrh	r3, [r4, #2]
	uxth	r5, r5
	cmp	r3, #2
	ldrls	r3, .L2698+4
	ldrlsh	r3, [r3]
	subls	r3, r3, #1
	strlsh	r3, [r4, #2]	@ movhi
	cmp	r5, #3
	bls	.L2680
	ldr	r0, .L2698+24
	mov	r2, r5
	ldr	r1, [r6, #1756]
	bl	printk
	mov	r3, #1
	str	r3, [r6, #-3616]
	b	.L2683
.L2685:
	cmp	r2, #0
	strneh	r7, [r4, #40]	@ movhi
	cmp	r2, #256
	cmpne	r3, #1
	beq	.L2689
	ldr	r3, [r4, #36]
	cmp	r3, #0
	beq	.L2690
.L2689:
	mov	r3, #0
	str	r3, [r4, #36]
	b	.L2680
.L2690:
	ldr	r2, [r6, #1756]
	ldr	r3, [r4, #24]
	str	r2, [r3, r8, asl #2]
.L2683:
	mov	r0, #0
	add	sp, sp, #12
	@ sp needed
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L2699:
	.align	2
.L2698:
	.word	.LANCHOR2
	.word	.LANCHOR0+2390
	.word	.LANCHOR0
	.word	.LANCHOR0+2398
	.word	.LANCHOR2+1752
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
	ldr	r5, .L2702
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
.L2703:
	.align	2
.L2702:
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
	ldr	r1, .L2747
	mov	r2, #1
	str	r2, [r4, #36]
	ldr	r3, [r1, #-524]
	mov	r5, r1
	ldr	r6, [r1, #-500]
	mov	r7, r1
	str	r3, [r1, #1760]
	mvn	r3, #0
	str	r6, [r1, #1764]
	strh	r3, [r4]	@ movhi
	strh	r3, [r4, #2]	@ movhi
	mov	r3, #0
	str	r3, [r4, #32]
	str	r3, [r4, #28]
	str	r3, [sp, #4]
.L2705:
	ldrh	r3, [sp, #4]
	ldr	r2, [sp, #8]
	sxth	fp, r3
	cmp	fp, r2
	bge	.L2723
	ldr	r2, [sp, #8]
	sub	r2, r2, #1
	cmp	fp, r2
	mov	r2, fp, asl #1
	bne	.L2706
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
	ldr	fp, .L2747
	str	r3, [r4, #28]
.L2707:
	sxth	r3, r7
	cmp	r3, r9
	bge	.L2723
	ldr	r2, [sp, #4]
	mov	r1, #1
	ldr	r0, .L2747+4
	ldrh	r2, [r2]
	orr	r3, r3, r2, asl #10
	mov	r2, r1
	str	r3, [r5, #1756]
	bl	FlashReadPages
	ldr	r3, .L2747+8
	ldrb	r3, [r3]	@ zero_extendqisi2
	cmp	r3, #0
	beq	.L2708
	ldr	r3, [fp, #1764]
	ldr	r3, [r3, #12]
	cmp	r3, #0
	beq	.L2708
	ldr	r2, .L2747+12
	ldr	r0, [fp, #1760]
	str	r3, [sp, #8]
	ldrh	r1, [r2]
	bl	js_hash
	ldr	r3, [sp, #8]
	cmp	r3, r0
	mvnne	r3, #0
	strne	r3, [fp, #1752]
.L2708:
	ldr	r3, [fp, #1752]
	cmn	r3, #1
	beq	.L2709
	ldrh	r3, [r6, #8]
	cmp	r3, r10
	bcs	.L2709
	ldrh	r2, [r4, #4]
	ldrh	r1, [r6]
	cmp	r1, r2
	ldreq	r2, [fp, #1756]
	streq	r2, [r8, r3, asl #2]
.L2709:
	add	r7, r7, #1
	b	.L2707
.L2723:
	mov	r0, r4
	bl	ftl_free_no_use_map_blk
	ldr	r1, .L2747+8
	movw	r3, #2390
	ldrh	r2, [r4, #2]
	ldrh	r3, [r1, r3]
	cmp	r2, r3
	bne	.L2712
	mov	r0, r4
	bl	ftl_map_blk_alloc_new_blk
	b	.L2712
.L2706:
	ldr	r3, [r5, #-524]
	mov	r1, #1
	ldr	fp, .L2747+16
	ldr	r0, .L2747+4
	str	r3, [r5, #1760]
	add	r3, r9, r2
	ldrh	r2, [r9, r2]
	str	r3, [sp, #16]
	ldrh	r3, [fp]
	sub	r3, r3, #1
	orr	r3, r3, r2, asl #10
	mov	r2, r1
	str	r3, [r5, #1756]
	bl	FlashReadPages
	ldr	r3, [r5, #1752]
	cmn	r3, #1
	mov	r3, fp
	beq	.L2725
	ldrh	r1, [r6]
	ldrh	r2, [r4, #4]
	cmp	r1, r2
	bne	.L2725
	ldrh	r1, [r6, #8]
	movw	r2, #64245
	cmp	r1, r2
	bne	.L2725
	mov	r0, #0
	mov	lr, #8
	mov	fp, #4
.L2714:
	uxth	r2, r0
	ldrh	ip, [r3]
	sxth	r1, r2
	sub	ip, ip, #1
	cmp	r1, ip
	bge	.L2717
	ldr	ip, [r5, #-524]
	add	r0, r0, #1
	ldr	r1, [ip, r1, asl #3]
	uxth	r1, r1
	cmp	r1, r10
	smlabbcc	r2, r2, lr, fp
	ldrcc	r2, [ip, r2]
	strcc	r2, [r8, r1, asl #2]
	b	.L2714
.L2725:
	mov	fp, #0
.L2745:
	ldr	r3, .L2747+16
	sxth	r2, fp
	ldrh	r1, [r3]
	cmp	r2, r1
	bge	.L2717
	str	r3, [sp, #20]
	ldr	r3, [sp, #16]
	ldr	r0, .L2747+4
	ldrh	r1, [r3]
	orr	r2, r2, r1, asl #10
	mov	r1, #1
	str	r2, [r7, #1756]
	mov	r2, r1
	bl	FlashReadPages
	ldr	r3, .L2747+8
	ldrb	r2, [r3]	@ zero_extendqisi2
	cmp	r2, #0
	ldr	r3, [sp, #20]
	beq	.L2718
	ldr	r2, [r7, #1764]
	ldr	r2, [r2, #12]
	cmp	r2, #0
	beq	.L2718
	ldrh	r1, [r3, #8]
	ldr	r0, [r7, #1760]
	str	r2, [sp, #20]
	bl	js_hash
	ldr	r2, [sp, #20]
	cmp	r2, r0
	mvnne	r3, #0
	strne	r3, [r7, #1752]
.L2718:
	ldr	r3, .L2747
	ldr	r3, [r3, #1752]
	cmn	r3, #1
	beq	.L2719
	ldrh	r3, [r6, #8]
	cmp	r3, r10
	bcs	.L2719
	ldrh	r2, [r4, #4]
	ldrh	r1, [r6]
	cmp	r1, r2
	ldreq	r2, [r7, #1756]
	streq	r2, [r8, r3, asl #2]
.L2719:
	add	fp, fp, #1
	b	.L2745
.L2717:
	ldr	r3, [sp, #4]
	add	r3, r3, #1
	str	r3, [sp, #4]
	b	.L2705
.L2712:
	mov	r0, r4
	bl	ftl_map_blk_gc
	mov	r0, r4
	bl	ftl_map_blk_gc
	mov	r0, #0
	add	sp, sp, #28
	@ sp needed
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L2748:
	.align	2
.L2747:
	.word	.LANCHOR2
	.word	.LANCHOR2+1752
	.word	.LANCHOR0
	.word	.LANCHOR0+2398
	.word	.LANCHOR0+2390
	.fnend
	.size	FtlMapTblRecovery, .-FtlMapTblRecovery
	.align	2
	.global	FtlLoadVonderInfo
	.type	FtlLoadVonderInfo, %function
FtlLoadVonderInfo:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	ldr	r2, .L2751
	movw	r1, #2408
	stmfd	sp!, {r3, lr}
	.save {r3, lr}
	ldr	r3, .L2751+4
	ldrh	r1, [r2, r1]
	add	r0, r3, #1792
	strh	r1, [r0, #10]	@ movhi
	ldr	r1, .L2751+8
	strh	r1, [r0, #4]	@ movhi
	movw	r1, #2434
	ldrh	r1, [r2, r1]
	strh	r1, [r0, #8]	@ movhi
	movw	r1, #2410
	ldrh	r1, [r2, r1]
	ldr	r2, [r2, #2436]
	strh	r1, [r0, #6]	@ movhi
	str	r2, [r3, #1804]
	ldr	r2, [r3, #-460]
	str	r2, [r3, #1808]
	ldr	r2, [r3, #-464]
	str	r2, [r3, #1812]
	ldr	r2, [r3, #-456]
	str	r2, [r3, #1816]
	bl	FtlMapTblRecovery
	mov	r0, #0
	ldmfd	sp!, {r3, pc}
.L2752:
	.align	2
.L2751:
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
	ldr	r0, .L2755
	bl	FtlMapTblRecovery
	mov	r0, #0
	ldmfd	sp!, {r3, pc}
.L2756:
	.align	2
.L2755:
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
	ldr	r3, .L2768
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
	bhi	.L2758
	ldr	fp, .L2768+4
	mov	r5, r6, lsr r5
	mov	r3, r5, asl #2
	str	r3, [sp, #4]
	mov	r7, fp
	mov	r3, #0
	str	r3, [sp]
.L2759:
	cmp	r4, #0
	beq	.L2767
	ldr	r2, [sp, #4]
	mov	r0, r6
	ldr	r3, [fp, #-456]
	ldr	ip, [r3, r2]
	ldr	r3, .L2768+8
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
	beq	.L2761
	ldr	r2, [r7, #-516]
	mov	r1, #1
	add	r0, sp, #20
	str	ip, [sp, #24]
	str	r2, [sp, #28]
	mov	r2, #0
	str	r2, [sp, #32]
	mov	r2, r1
	bl	FlashReadPages
	b	.L2762
.L2761:
	ldr	r2, .L2768+12
	ldr	r0, [r7, #-516]
	ldrh	r2, [r2]
	bl	ftl_memset
.L2762:
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
	ldr	r0, .L2768+16
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
	b	.L2759
.L2767:
	ldr	r0, [sp]
.L2758:
	add	sp, sp, #60
	@ sp needed
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L2769:
	.align	2
.L2768:
	.word	.LANCHOR0
	.word	.LANCHOR2
	.word	.LANCHOR0+2394
	.word	.LANCHOR0+2398
	.word	.LANCHOR2+1792
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
	ldr	r2, .L2772
	ldr	r3, .L2772+4
	ldr	r1, [r2, #-3232]
	cmp	r1, r3
	bxne	lr
	ldr	r3, .L2772+8
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
.L2773:
	.align	2
.L2772:
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
	ldr	r3, .L2782
	ldr	r3, [r3, #2244]
	cmp	r3, #0
	ldr	r3, .L2782+4
	moveq	r2, #32
	beq	.L2775
	ldr	r2, [r3, #-3308]
	cmp	r2, #39
	movhi	r2, #32
	movls	r2, #4
.L2775:
	movw	ip, #1836
	ldrh	r1, [r3, ip]
	cmp	r1, #31
	addls	r1, r1, #1
	ldrls	r2, .L2782+4
	strlsh	r1, [r2, ip]	@ movhi
	movls	r2, #1
	cmp	r0, #0
	bne	.L2777
	ldr	r1, [r3, #-480]
	ldr	r0, [r1, #20]
	ldr	r1, [r1, #16]
	add	r2, r2, r0
	cmp	r1, r2
	bcc	.L2778
.L2777:
	ldr	r2, [r3, #-480]
	mov	r0, #64
	ldr	r1, [r2, #16]
	str	r1, [r2, #20]
	ldr	r1, .L2782+8
	str	r1, [r2]
	ldr	r2, [r3, #-480]
	ldr	r3, .L2782+12
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
.L2778:
	mov	r0, #0
	ldmfd	sp!, {r3, pc}
.L2783:
	.align	2
.L2782:
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
	ldr	r4, .L2796
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
	bhi	.L2785
	ldr	r9, .L2796+4
	mov	r5, r7, lsr r5
	mov	r3, r5, asl #2
	str	r3, [sp, #4]
	mov	r10, r9
	mov	r3, #0
	str	r3, [sp]
.L2786:
	cmp	r6, #0
	beq	.L2795
	ldr	r2, [sp, #4]
	mov	r0, r7
	ldr	r3, [r9, #-456]
	ldr	r3, [r3, r2]
	str	r3, [sp, #12]
	ldr	r3, .L2796+8
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
	beq	.L2788
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
	ldr	r2, [r10, #1752]
	mvneq	r3, #0
	cmp	r2, #256
	str	r3, [sp]
	ldr	r3, [sp, #12]
	bne	.L2790
	mov	r1, r5
	mov	r2, r3
	ldr	r0, .L2796+12
	bl	printk
	ldr	r0, .L2796+16
	mov	r1, r5
	ldr	r2, [r9, #-516]
	bl	FtlMapWritePage
.L2790:
	ldrh	r3, [sp, #8]
	mov	r0, r8
	ldr	r1, [r10, #-516]
	mov	r2, fp
	add	r1, r1, r3, asl #9
	bl	ftl_memcpy
	b	.L2791
.L2788:
	mov	r0, r8
	mov	r1, r3
	mov	r2, fp
	bl	ftl_memset
.L2791:
	ldr	r3, [sp, #4]
	add	r5, r5, #1
	rsb	r6, r4, r6
	add	r7, r7, r4
	add	r3, r3, #4
	add	r8, r8, fp
	str	r3, [sp, #4]
	b	.L2786
.L2795:
	ldr	r0, [sp]
.L2785:
	add	sp, sp, #60
	@ sp needed
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L2797:
	.align	2
.L2796:
	.word	.LANCHOR0
	.word	.LANCHOR2
	.word	.LANCHOR0+2394
	.word	.LC136
	.word	.LANCHOR2+1792
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
	ldr	r4, .L2801
	sub	r5, r4, #488
	ldr	r2, [r4, #-480]
	ldrh	r1, [r5]
	bl	FtlVendorPartRead
	ldr	r3, [r4, #-480]
	ldr	r2, [r3]
	ldr	r3, .L2801+4
	cmp	r2, r3
	beq	.L2799
	ldr	r1, .L2801+8
	ldr	r0, .L2801+12
	bl	printk
	ldrh	r2, [r5]
	ldr	r0, [r4, #-480]
	mov	r1, #0
	mov	r2, r2, asl #9
	bl	ftl_memset
.L2799:
	mov	r0, #0
	ldmfd	sp!, {r3, r4, r5, pc}
.L2802:
	.align	2
.L2801:
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
	ldr	r4, .L2809
	mov	r1, #1
	ldr	r5, .L2809+4
	sub	r2, r4, #3232
	bl	FtlVendorPartRead
	ldr	r3, [r4, #-3232]
	cmp	r3, r5
	beq	.L2804
	sub	r0, r4, #3232
	mov	r1, #0
	mov	r2, #512
	bl	ftl_memset
	str	r5, [r4, #-3232]
.L2804:
	ldr	r2, [r4, #-3232]
	ldr	r3, .L2809
	cmp	r2, r5
	bne	.L2805
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
.L2805:
	ldr	r1, [r4, #-3164]
	mov	r3, #0
	ldr	r2, .L2809+8
	str	r3, [r4, #-564]
	cmp	r1, r2
	ldr	r5, .L2809+12
	bne	.L2806
	ldrb	r2, [r5, #144]	@ zero_extendqisi2
	cmp	r2, r3
	beq	.L2807
	ldr	r2, .L2809
	str	r3, [r2, #-3164]
	bl	Ftl_save_ext_data
	b	.L2806
.L2807:
	ldr	r0, .L2809+16
	mov	r3, #1
	ldr	r1, .L2809+20
	str	r3, [r5, #2244]
	bl	printk
.L2806:
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
.L2810:
	.align	2
.L2809:
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
	ldr	r4, .L2818
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
	str	r2, [r4, #1764]
	str	r0, [r4, #1760]
	ldr	r3, [r3, r5, asl #2]
	cmp	r3, #0
	str	r3, [r4, #1756]
	beq	.L2815
	mov	r1, #1
	ldr	r0, .L2818+4
	mov	r2, r1
	bl	FlashReadPages
	b	.L2816
.L2815:
	ldr	r2, .L2818+8
	movw	r3, #2398
	mov	r1, #255
	ldrh	r2, [r2, r3]
	bl	ftl_memset
.L2816:
	mov	r0, r6
	mov	r1, r5
	ldr	r2, [r4, #1760]
	ldmfd	sp!, {r4, r5, r6, lr}
	b	FtlMapWritePage
.L2819:
	.align	2
.L2818:
	.word	.LANCHOR2
	.word	.LANCHOR2+1752
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
	ldr	r3, .L2835
	stmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, lr}
	.save {r4, r5, r6, r7, r8, r9, r10, lr}
	mov	r8, r2
	ldrh	r2, [r3, #128]
	.pad #40
	sub	sp, sp, #40
	ldrh	r3, [r3, #130]
	mov	r5, r0
	ldr	r9, .L2835+4
	mov	r7, r1
	smulbb	r3, r2, r3
	ldr	r2, [r9, #1688]
	str	r2, [sp, #12]
	ldr	r2, [r9, #1720]
	uxth	r3, r3
	sub	r6, r3, #1
	sub	r4, r3, #16
	mul	r10, r3, r1
	uxth	r6, r6
	str	r2, [sp, #16]
.L2821:
	cmp	r6, r4
	ble	.L2834
	mov	r1, #1
	add	r3, r6, r10
	add	r0, sp, #4
	mov	r2, r1
	mov	r3, r3, asl #10
	str	r3, [sp, #8]
	bl	FlashReadPages
	ldr	r3, [sp, #4]
	cmn	r3, #1
	beq	.L2822
	ldr	r3, [r9, #1720]
	ldrh	r2, [r3]
	movw	r3, #61664
	cmp	r2, r3
	bne	.L2822
	cmp	r5, #0
	moveq	r0, r5
	beq	.L2823
	cmp	r7, #0
	ldreq	ip, .L2835+4
	moveq	lr, #1
	beq	.L2824
.L2826:
	ldr	r1, [r9, #1688]
	mov	r2, r8
	mov	r0, r5
	bl	ftl_memcpy
	mov	r2, #4
	ldr	r0, .L2835+8
	mov	r1, r5
	mov	r3, r2
	bl	rknand_print_hex
	mov	r0, #0
	b	.L2823
.L2824:
	ldr	r2, [r9, #1708]
	uxth	r3, r7
	add	r7, r7, #1
	cmp	r3, r2
	bcs	.L2826
	ldr	r1, [ip, #1688]
	mov	r0, r3, lsr #5
	and	r4, r3, #31
	ldr	r2, [r1, r0, asl #2]
	orr	r4, r2, lr, asl r4
	str	r4, [r1, r0, asl #2]
	b	.L2824
.L2822:
	sub	r6, r6, #1
	uxth	r6, r6
	b	.L2821
.L2834:
	mvn	r0, #0
.L2823:
	add	sp, sp, #40
	@ sp needed
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, pc}
.L2836:
	.align	2
.L2835:
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
	ldr	r3, .L2848
	stmfd	sp!, {r4, r5, r6, r7, r8, lr}
	.save {r4, r5, r6, r7, r8, lr}
	mov	r5, r0
	ldr	r3, [r3, #44]
	ldr	r6, .L2848+4
	ldrb	r4, [r3, #13]	@ zero_extendqisi2
	ldrh	r3, [r3, #14]
	ldr	r0, [r6, #1716]
	smulbb	r4, r4, r3
	uxth	r4, r4
	add	r2, r4, #7
	mov	r2, r2, asr #3
	bl	FlashReadFacBbtData
	cmn	r0, #1
	bne	.L2838
.L2842:
	mov	r3, #0
	b	.L2839
.L2838:
	mov	lr, r4, lsr #4
	mov	ip, #0
	sub	r4, r4, #1
	mov	r3, ip
	mov	r7, #1
.L2840:
	uxth	r0, ip
	cmp	r0, r4
	bge	.L2839
	ldr	r8, [r6, #1716]
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
	bcc	.L2840
	b	.L2842
.L2839:
	mov	r3, r3, asl #1
	mvn	r2, #0
	mov	r0, #0
	strh	r2, [r5, r3]	@ movhi
	ldmfd	sp!, {r4, r5, r6, r7, r8, pc}
.L2849:
	.align	2
.L2848:
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
	ldr	r4, .L2874
	ldr	r5, [r4, #-3616]
	cmp	r5, #0
	bne	.L2851
	bl	FtlBbtMemInit
	ldr	r7, .L2874+4
	bl	FtlLoadFactoryBbt
	sub	r9, r7, #18
	sub	r10, r7, #94
.L2852:
	ldr	r3, .L2874+8
	ldr	r6, .L2874+12
	ldrh	r3, [r3]
	cmp	r5, r3
	bcs	.L2871
	ldrh	r3, [r9, #2]!
	movw	r2, #65535
	ldr	r0, [r4, #-524]
	ldr	fp, [r4, #-500]
	cmp	r3, r2
	ldr	r8, .L2874
	str	r0, [r4, #1760]
	str	fp, [r4, #1764]
	beq	.L2853
	ldrh	ip, [r10]
	mov	r1, #1
	mov	r2, r1
	ldr	r0, .L2874+16
	mla	ip, ip, r5, r3
	mov	r3, ip, asl #10
	str	ip, [sp, #4]
	str	r3, [r8, #1756]
	bl	FlashReadPages
	ldrh	r2, [r10]
	ldr	r0, [r7]
	add	r2, r2, #7
	ldr	r1, [r8, #1760]
	mov	r2, r2, asr #3
	bl	ftl_memcpy
	ldr	ip, [sp, #4]
	b	.L2854
.L2853:
	mov	r1, r5
	sub	r8, r8, #436
	bl	FlashGetBadBlockList
	ldr	r0, [r8, #2196]
	ldr	r1, [r7]
	bl	FtlBbt2Bitmap
	ldrh	r6, [r10]
	sub	r6, r6, #1
	uxth	r6, r6
.L2855:
	ldrh	r0, [r10]
	smlabb	r0, r0, r5, r6
	uxth	r0, r0
	bl	FtlBbmIsBadBlock
	cmp	r0, #1
	subeq	r6, r6, #1
	uxtheq	r6, r6
	beq	.L2855
.L2872:
	mov	r1, #0
	mov	r2, #16
	strh	r6, [r9]	@ movhi
	ldr	r0, [r4, #-500]
	bl	ftl_memset
	ldr	r3, .L2874+20
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
	ldr	r0, [r4, #1760]
	mla	ip, ip, r5, r3
	mov	r3, ip, asl #10
	str	r3, [r4, #1756]
	str	ip, [sp, #4]
	bl	ftl_memcpy
	mov	r1, #1
	ldr	r0, .L2874+16
	mov	r2, r1
	bl	FlashEraseBlocks
	mov	r1, #1
	mov	r3, r1
	ldr	r0, .L2874+16
	mov	r2, r1
	bl	FlashProgPages
	ldr	r3, [r4, #1752]
	cmn	r3, #1
	ldr	ip, [sp, #4]
	bne	.L2854
	uxth	r0, ip
	bl	FtlBbmMapBadBlock
	b	.L2855
.L2854:
	uxth	r0, ip
	add	r5, r5, #1
	bl	FtlBbmMapBadBlock
	add	r7, r7, #4
	b	.L2852
.L2871:
	ldr	r7, .L2874+24
	mov	r5, #0
.L2859:
	ldrh	r3, [r7]
	uxth	r0, r5
	add	r5, r5, #1
	cmp	r3, r0
	bls	.L2873
	bl	FtlBbmMapBadBlock
	b	.L2859
.L2873:
	ldr	r7, .L2874+28
	movw	r9, #65535
	ldrh	r5, [r7, #12]
	mov	r8, r7
	sub	r5, r5, #1
	uxth	r5, r5
.L2861:
	ldrh	r3, [r7, #12]
	sub	r3, r3, #48
	cmp	r5, r3
	ble	.L2865
	mov	r0, r5
	bl	FtlBbmIsBadBlock
	cmp	r0, #1
	beq	.L2862
	mov	r0, r5
	bl	FlashTestBlk
	cmp	r0, #0
	beq	.L2863
	mov	r0, r5
	bl	FtlBbmMapBadBlock
	b	.L2862
.L2863:
	ldrh	r3, [r8]
	cmp	r3, r9
	streqh	r5, [r8]	@ movhi
.L2864:
	ldrne	r3, .L2874+28
	strneh	r5, [r3, #4]	@ movhi
	bne	.L2865
.L2862:
	sub	r5, r5, #1
	uxth	r5, r5
	b	.L2861
.L2865:
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
.L2851:
	mov	r0, #0
	add	sp, sp, #12
	@ sp needed
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L2875:
	.align	2
.L2874:
	.word	.LANCHOR2
	.word	.LANCHOR0+2480
	.word	.LANCHOR0+2342
	.word	.LANCHOR0
	.word	.LANCHOR2+1752
	.word	-3872
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
	ldr	r7, .L2893
	ldrh	r10, [r7, r3]
	ldr	r3, [r7, #2448]
	cmp	r0, r3
	bcs	.L2877
	add	r10, r10, #7
	ldr	fp, .L2893+4
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
	b	.L2878
.L2877:
	cmp	r2, #0
	mvn	r0, #0
	streq	r0, [r1]
	b	.L2879
.L2883:
	add	r4, r4, #1
	mla	r0, r1, r4, r8
	ldrh	r0, [r0, #-12]
	cmp	r0, r5
	beq	.L2880
.L2878:
	uxth	r7, r4
	cmp	r7, r2
	bcc	.L2883
	bl	select_l2p_ram_region
	mov	r4, #12
	movw	r3, #65535
	mul	r4, r4, r0
	mov	r7, r0
	add	r2, r8, r4
	ldrh	r1, [r8, r4]
	cmp	r1, r3
	bne	.L2892
.L2884:
	ldr	r3, [fp, #-452]
	ldr	r8, .L2893+4
	ldr	fp, [r3, r5, asl #2]
	cmp	fp, #0
	bne	.L2885
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
.L2880:
	ldr	r2, [sp]
	mvn	r3, #0
	mov	r6, #12
	bic	r10, r2, r3, asl r10
	ldr	r3, [sp, #4]
	ldr	r2, .L2893+4
	cmp	r3, #0
	uxth	r10, r10
	bne	.L2881
	ldr	r3, [r2, #-3380]
	mla	r6, r6, r7, r3
	ldr	r3, [r6, #8]
	ldr	r3, [r3, r10, asl #2]
	str	r3, [r9]
	b	.L2882
.L2881:
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
.L2882:
	ldr	r2, [r2, #-3380]
	mov	r3, #12
	mov	r0, #0
	mla	r7, r3, r7, r2
	ldr	r3, [r7, #4]
	cmn	r3, #1
	addne	r3, r3, #1
	strne	r3, [r7, #4]
	b	.L2879
.L2892:
	ldr	r3, [r2, #4]
	cmp	r3, #0
	bge	.L2884
	bl	flush_l2p_region
	b	.L2884
.L2885:
	ldr	r3, [r8, #-3380]
	mov	r1, #1
	ldr	r0, .L2893+8
	mov	r2, r1
	add	r3, r3, r4
	str	fp, [r8, #1756]
	ldr	r3, [r3, #8]
	str	r3, [r8, #1760]
	ldr	r3, [r8, #-500]
	str	r3, [r8, #1764]
	bl	FlashReadPages
	ldr	r3, [r8, #1764]
	ldrh	r3, [r3, #8]
	cmp	r3, r5
	beq	.L2886
	mov	r1, r5
	mov	r2, fp
	ldr	r0, .L2893+12
	bl	printk
	mov	r2, #4
	mov	r3, r2
	ldr	r0, .L2893+16
	ldr	r1, [r8, #1764]
	bl	rknand_print_hex
	movw	r3, #2424
	ldrh	r3, [r6, r3]
	mov	r2, #4
	ldr	r0, .L2893+20
	ldr	r1, [r8, #-452]
	bl	rknand_print_hex
	mov	r3, #1
	str	r3, [r8, #-3616]
	b	.L2887
.L2886:
	ldr	r3, [r8, #1752]
	cmp	r3, #256
	bne	.L2887
	mov	r1, r5
	mov	r2, fp
	ldr	r0, .L2893+24
	bl	printk
	ldr	r3, [r8, #-3380]
	sub	r0, r8, #432
	mov	r1, r5
	add	r3, r3, r4
	ldr	r2, [r3, #8]
	bl	FtlMapWritePage
.L2887:
	ldr	r3, .L2893+4
	mov	r1, #0
	ldr	r3, [r3, #-3380]
	add	r2, r3, r4
	str	r1, [r2, #4]
	strh	r5, [r3, r4]	@ movhi
	b	.L2880
.L2879:
	add	sp, sp, #12
	@ sp needed
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L2894:
	.align	2
.L2893:
	.word	.LANCHOR0
	.word	.LANCHOR2
	.word	.LANCHOR2+1752
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
	ldr	r4, .L2915
	ldr	r3, [r4, #-3616]
	cmp	r3, #0
	bne	.L2895
	ldr	r3, .L2915+4
	ldrh	r2, [r3, #4]
	cmp	r2, #0
	beq	.L2897
	ldrb	r3, [r4, #-3516]	@ zero_extendqisi2
	cmp	r3, #0
	bne	.L2897
	ldr	r8, .L2915+8
	movw	r3, #2388
	ldrb	r1, [r4, #-3517]	@ zero_extendqisi2
	ldrh	r3, [r8, r3]
	mul	r3, r3, r1
	cmp	r2, r3
	beq	.L2897
	ldrb	r9, [r4, #-3514]	@ zero_extendqisi2
	add	r3, r8, #2320
	ldr	r6, [r8, #2448]
	cmp	r9, #0
	ldrh	r7, [r3]
	bne	.L2895
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
	beq	.L2899
	mov	r1, #1
	mov	r2, r9
	add	r0, sp, #4
	bl	FlashReadPages
	b	.L2900
.L2899:
	movw	r3, #2398
	mov	r1, #255
	ldrh	r2, [r8, r3]
	bl	ftl_memset
.L2900:
	ldr	fp, .L2915+4
	mov	r7, r7, asl #2
	ldr	r3, .L2915+12
	mov	r8, #0
	ldr	r9, .L2915
	mov	r10, fp
	strh	r3, [r5]	@ movhi
.L2901:
	cmp	r8, r7
	beq	.L2902
	ldrh	r3, [fp, #4]
	cmp	r3, #0
	beq	.L2902
	ldr	r3, [sp, #8]
	add	r8, r8, #1
	str	r6, [r5, #8]
	ldr	r0, .L2915+4
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
	b	.L2901
.L2902:
	mov	r3, #1
	b	.L2914
.L2897:
	mov	r3, #0
.L2914:
	strb	r3, [r4, #-3514]
.L2895:
	add	sp, sp, #44
	@ sp needed
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L2916:
	.align	2
.L2915:
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
	ldr	r5, .L2923
	mov	r7, #12
	ldr	r6, .L2923+4
.L2918:
	ldrh	r3, [r5]
	uxth	r0, r4
	cmp	r3, r0
	bls	.L2922
	ldr	r3, [r6, #-3380]
	mla	r3, r7, r0, r3
	ldr	r3, [r3, #4]
	cmp	r3, #0
	bge	.L2919
	bl	flush_l2p_region
.L2919:
	add	r4, r4, #1
	b	.L2918
.L2922:
	mov	r0, #0
	ldmfd	sp!, {r3, r4, r5, r6, r7, pc}
.L2924:
	.align	2
.L2923:
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
	ldr	r5, .L2952
	ldrh	r6, [r0]
	ldr	r3, [r5, #-3616]
	cmp	r3, #0
	bne	.L2926
	movw	r3, #65535
	cmp	r6, r3
	mov	r4, r0
	beq	.L2927
	ldr	r2, [r5, #-3544]
	mov	r3, r6, asl #1
	mov	r0, r6
	ldrh	r3, [r2, r3]
	cmp	r3, #0
	beq	.L2928
	bl	INSERT_DATA_LIST
	b	.L2927
.L2928:
	bl	INSERT_FREE_LIST
.L2927:
	mov	r3, #0
	strb	r3, [r4, #8]
	ldr	r3, .L2952+4
	cmp	r4, r3
	beq	.L2929
	ldr	r3, .L2952+8
	movw	r2, #2340
	ldrh	r2, [r3, r2]
	cmp	r2, #1
	beq	.L2929
	ldrb	r1, [r3, #144]	@ zero_extendqisi2
	cmp	r1, #0
	beq	.L2930
.L2929:
	mov	r3, #1
	strb	r3, [r4, #8]
	b	.L2931
.L2930:
	ldr	r1, .L2952+12
	cmp	r4, r1
	bne	.L2931
	cmp	r2, #3
	beq	.L2933
	ldr	r2, .L2952
	ldr	r2, [r2, #-3236]
	cmp	r2, #1
	bne	.L2934
.L2933:
	mov	r2, #1
	strb	r2, [r5, #-3516]
.L2934:
	ldr	r3, [r3, #2244]
	cmp	r3, #0
	beq	.L2931
	ldr	r3, [r5, #-3308]
	cmp	r3, #39
	ldrls	r3, .L2952
	movls	r2, #1
	strlsb	r2, [r3, #-3516]
.L2931:
	ldr	r2, .L2952
	movw	r1, #65535
	sub	r3, r2, #380
	ldrh	r0, [r3]
	mov	r5, r3
	cmp	r0, r1
	beq	.L2936
	cmp	r6, r0
	bne	.L2937
	ldr	r2, [r2, #-3544]
	mov	r3, r0, asl #1
	ldrh	r3, [r2, r3]
	cmp	r3, #0
	beq	.L2938
.L2937:
	bl	update_vpc_list
.L2938:
	mvn	r3, #0
	strh	r3, [r5]	@ movhi
.L2936:
	mov	r0, r4
	bl	allocate_data_superblock
	bl	l2p_flush
	mov	r0, #0
	bl	FtlEctTblFlush
	bl	FtlVpcTblFlush
.L2926:
	mov	r0, #0
	ldmfd	sp!, {r4, r5, r6, pc}
.L2953:
	.align	2
.L2952:
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
	ldr	r1, .L2977
	ldr	r0, .L2977+4
	bl	printk
	ldr	r0, .L2977+8
	mov	r1, #0
	mov	r2, #8192
	bl	memset
	ldr	r6, .L2977+12
	ldr	r5, .L2977+8
.L2955:
	ldr	r3, [r6, #2448]
	cmp	r4, r3
	bcs	.L2975
	mov	r0, r4
	add	r1, sp, #4
	mov	r2, #0
	bl	log2phys
	ldr	r0, [sp, #4]
	cmn	r0, #1
	beq	.L2956
	ubfx	r0, r0, #10, #16
	bl	P2V_block_in_plane
	mov	r0, r0, asl #1
	ldrh	r3, [r5, r0]
	add	r3, r3, #1
	strh	r3, [r5, r0]	@ movhi
.L2956:
	add	r4, r4, #1
	b	.L2955
.L2975:
	ldr	r7, .L2977+16
	mov	r4, #0
	ldr	r9, .L2977+20
	mov	r6, r4
	add	r8, r7, #1840
.L2958:
	ldrh	r3, [r9]
	uxth	r1, r4
	ldr	r5, .L2977+16
	cmp	r3, r1
	bls	.L2976
	ldr	r3, [r7, #-3544]
	mov	r5, r1, asl #1
	ldrh	r2, [r3, r5]
	ldrh	r3, [r8, r5]
	cmp	r2, r3
	beq	.L2959
	ldr	r0, .L2977+24
	bl	printk
	ldr	r3, [r7, #-3544]
	movw	r2, #65535
	ldrh	r3, [r3, r5]
	cmp	r3, r2
	beq	.L2959
	ldrh	r2, [r8, r5]
	cmp	r2, r3
	movhi	r6, #1
.L2959:
	add	r4, r4, #1
	b	.L2958
.L2976:
	ldr	r3, [r5, #-3532]
	cmp	r3, #0
	beq	.L2961
	sub	r2, r5, #3520
	add	r9, r5, #1840
	mov	r8, #0
	ldrh	r7, [r2, #-8]
	ldr	r2, [r5, #-3552]
	rsb	r3, r2, r3
	ldr	r2, .L2977+28
	mov	r3, r3, asr #1
	mul	r3, r2, r3
	uxth	r4, r3
.L2962:
	uxth	r3, r8
	cmp	r3, r7
	bcs	.L2961
	ldr	r2, [r5, #-3544]
	mov	r3, r4, asl #1
	ldrh	r2, [r2, r3]
	cmp	r2, #0
	beq	.L2963
	ldr	r0, .L2977+32
	mov	r1, r4
	ldrh	r3, [r9, r3]
	mov	r6, #1
	bl	printk
.L2963:
	mov	r3, #6
	ldr	r2, [r5, #-3552]
	mul	r4, r3, r4
	movw	r3, #65535
	add	r8, r8, #1
	ldrh	r4, [r2, r4]
	cmp	r4, r3
	bne	.L2962
.L2961:
	mov	r1, r6
	ldr	r0, .L2977+36
	bl	printk
	add	sp, sp, #12
	@ sp needed
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, pc}
.L2978:
	.align	2
.L2977:
	.word	.LANCHOR3+204
	.word	.LC110
	.word	.LANCHOR2+1840
	.word	.LANCHOR0
	.word	.LANCHOR2
	.word	.LANCHOR0+2328
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
	stmfd	sp!, {r4, r5, r6, r7, r8, lr}
	.save {r4, r5, r6, r7, r8, lr}
	mov	r1, #0
	.pad #32
	sub	sp, sp, #32
	ldr	r0, .L2989
	bl	printk
	ldr	r5, .L2989+4
	ldr	r8, .L2989+8
	mov	r4, #0
	mov	r6, r5
.L2980:
	ldr	r3, [r8, #2448]
	cmp	r4, r3
	bcs	.L2988
	mov	r0, r4
	add	r1, sp, #28
	mov	r2, #0
	bl	log2phys
	ubfx	r3, r4, #0, #11
	cmp	r3, #0
	bne	.L2981
	ldr	r0, .L2989+12
	mov	r1, r4
	ldr	r2, [sp, #28]
	bl	printk
.L2981:
	ldr	r3, [sp, #28]
	cmn	r3, #1
	beq	.L2983
	str	r3, [r5, #1756]
	mov	r2, #0
	ldr	r3, [r5, #-524]
	mov	r1, #1
	ldr	r7, [r5, #-500]
	ldr	r0, .L2989+16
	str	r3, [r5, #1760]
	str	r4, [r5, #1768]
	str	r7, [r5, #1764]
	str	r2, [r5, #1752]
	bl	FlashReadPages
	ldr	r3, [r5, #1752]
	cmn	r3, #1
	cmpne	r3, #256
	beq	.L2984
	ldr	r3, [r7, #8]
	cmp	r3, r4
	beq	.L2983
.L2984:
	ldr	r3, [r6, #1764]
	ldr	r2, [r6, #1760]
	ldr	r0, .L2989+20
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
	ldr	r2, [r6, #1756]
	ldr	r3, [r3]
	bl	printk
.L2983:
	add	r4, r4, #1
	b	.L2980
.L2988:
	add	sp, sp, #32
	@ sp needed
	ldmfd	sp!, {r4, r5, r6, r7, r8, pc}
.L2990:
	.align	2
.L2989:
	.word	.LC146
	.word	.LANCHOR2
	.word	.LANCHOR0
	.word	.LC147
	.word	.LANCHOR2+1752
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
	ldr	r6, .L3001
	ldr	r4, [r6, #-3544]
	mov	r7, r0, asl #1
	ldrh	r3, [r4, r7]
	cmp	r3, #0
	addne	r3, r3, #1
	strneh	r3, [r4, r7]	@ movhi
	bne	.L2993
	ldr	r4, [r6, #-3532]
	cmp	r4, #0
	beq	.L2993
	ldr	r1, [r6, #-3552]
	sub	r2, r6, #3520
	ldr	lr, .L3001+4
	movw	r9, #65535
	rsb	r4, r1, r4
	ldrh	ip, [r2, #-8]
	mov	r8, r2
	mov	r4, r4, asr #1
	mul	r4, lr, r4
	mov	lr, #6
	uxth	r4, r4
.L2994:
	uxth	r2, r3
	cmp	r2, ip
	bcs	.L2993
	cmp	r4, r0
	bne	.L2995
	mov	r1, r4
	ldr	r0, .L3001+8
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
	b	.L2993
.L2995:
	mul	r4, lr, r4
	add	r3, r3, #1
	ldrh	r4, [r1, r4]
	cmp	r4, r9
	bne	.L2994
.L2993:
	mov	r0, r5
	add	r1, sp, #4
	mov	r2, #1
	bl	log2phys
	add	sp, sp, #12
	@ sp needed
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, pc}
.L3002:
	.align	2
.L3001:
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
	beq	.L3144
	ldrh	r3, [r0, #2]
	ldr	r5, .L3161
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
	beq	.L3144
.L3007:
	cmp	r0, r2
	add	r3, r3, #1
	uxtheq	r1, r3
	addeq	r1, r4, r1, asl #1
	ldreqh	r0, [r1, #16]
	beq	.L3007
.L3156:
	ldrb	r1, [r4, #8]	@ zero_extendqisi2
	mov	r9, r4
	cmp	r1, #1
	bne	.L3009
	bl	FtlGetLastWrittenPage
	cmn	r0, #1
	mov	r4, r0
	beq	.L3010
	ldrb	r3, [r5, #144]	@ zero_extendqisi2
	cmp	r3, #0
	bne	.L3147
	ldr	r3, .L3161
	add	r3, r3, r0, asl #1
	ldrh	r6, [r3, #148]
	b	.L3081
.L3009:
	mov	r1, #0
	bl	FtlGetLastWrittenPage
	cmn	r0, #1
	mov	r4, r0
	beq	.L3010
.L3147:
	mov	r6, r4
.L3081:
	ldr	r3, .L3161+4
	movw	r8, #65535
	ldrh	r2, [r3], #80
	str	r2, [sp, #4]
	ldr	r2, .L3161+8
	ldrh	r7, [r3]
	add	r3, r9, #14
	str	r3, [sp, #20]
	ldr	ip, [r2, #-536]
	mov	r0, r3
	ldr	lr, [r2, #-2692]
	mov	r2, #0
	mov	r5, r2
	mov	r10, r2
	b	.L3012
.L3010:
	mov	r3, #0
	strh	r3, [r9, #2]	@ movhi
	strb	r3, [r9, #6]
	b	.L3144
.L3014:
	ldrh	r3, [r0, #2]!
	cmp	r3, r8
	beq	.L3013
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
.L3013:
	add	r2, r2, #1
.L3012:
	ldr	r1, [sp, #4]
	uxth	r3, r2
	cmp	r3, r1
	bcc	.L3014
	ldrb	r3, [r9, #8]	@ zero_extendqisi2
	cmp	r3, #1
	movne	r3, #0
	bne	.L3148
	ldr	r3, .L3161
	ldrb	lr, [r3, #144]	@ zero_extendqisi2
	adds	r3, lr, #0
	movne	r3, #1
.L3148:
	ldr	r7, .L3161+8
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
.L3016:
	uxth	r8, fp
	cmp	r8, r5
	bcs	.L3023
	mov	r1, #36
	ldr	r0, [r7, #-536]
	mul	r1, r1, fp
	add	ip, r0, r1
	ldr	r1, [r0, r1]
	cmp	r1, #0
	bne	.L3017
	ldr	ip, [ip, #12]
	ldr	r3, [ip, #4]
	cmn	r3, #1
	beq	.L3018
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
.L3018:
	ldr	r1, [ip]
	cmn	r1, #1
	bne	.L3019
.L3023:
	cmp	r8, r5
	ldr	r5, .L3161+8
	bne	.L3145
	add	fp, r4, #1
	uxth	r3, fp
	str	r3, [sp, #4]
	ldr	r3, [r5, #-536]
	ldr	r0, [r3, #4]
	b	.L3149
.L3017:
	ldr	r1, [ip, #4]
	uxth	r10, r6
	ldr	r0, .L3161+12
	bl	printk
	ldrh	r1, [r9]
	ldr	r3, .L3161+16
	strh	r1, [r3]	@ movhi
.L3019:
	add	fp, fp, #1
	b	.L3016
.L3145:
	uxth	r3, r4
	str	r3, [sp, #4]
	ldr	r3, [sp, #12]
	mov	r2, #36
	ldr	r3, [r3, #-536]
	mla	r8, r2, r8, r3
	ldr	r0, [r8, #4]
.L3149:
	ubfx	r0, r0, #10, #16
	bl	P2V_plane
	ldr	r2, .L3161
	str	r0, [sp, #12]
	ldrb	r0, [r9, #8]	@ zero_extendqisi2
	cmp	r0, #1
	bne	.L3025
	ldrb	r3, [r2, #144]	@ zero_extendqisi2
	cmp	r3, #0
	ldreq	r3, [sp, #4]
	addeq	fp, r2, r3, asl #1
	ldreqh	r3, [fp, #148]
	streq	r3, [sp, #4]
.L3025:
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
	beq	.L3154
	ldr	r3, [sp, #28]
	movw	r2, #65535
	sub	fp, r3, #1
	clz	r3, r0
	mov	r3, r3, lsr #5
	cmp	r10, r2
	orrne	r3, r3, #1
	cmp	r3, #0
	beq	.L3029
	ldr	r3, [r5, #-368]
	uxth	r10, r6
	ldr	r7, .L3161+8
	mvn	r8, #0
	cmn	r3, #1
	mov	r6, r8
	ldreq	r3, .L3161+8
	streq	fp, [r3, #-368]
	ldr	r3, [r5, #-368]
	str	r3, [sp, #16]
	ldr	r3, [sp, #8]
	add	r3, r3, #7
	cmp	r10, r3
	subgt	r4, r10, #7
	ldrle	r4, [sp, #8]
	uxthgt	r4, r4
.L3032:
	cmp	r4, r10
	bhi	.L3045
	ldr	r3, .L3161+4
	mov	r0, #36
	ldr	lr, [r7, #-536]
	ldr	r1, [sp, #20]
	ldrh	r3, [r3]
	str	r3, [sp, #28]
	mov	r3, #0
	mov	r5, r3
.L3046:
	ldr	ip, [sp, #28]
	uxth	r2, r3
	cmp	r2, ip
	bcs	.L3157
	ldrh	r2, [r1, #2]!
	movw	ip, #65535
	add	r3, r3, #1
	cmp	r2, ip
	orrne	r2, r4, r2, asl #10
	mlane	ip, r0, r5, lr
	addne	r5, r5, #1
	uxthne	r5, r5
	strne	r2, [ip, #4]
	b	.L3046
.L3157:
	mov	r1, r5
	ldr	r2, [sp, #24]
	ldr	r0, [r7, #-536]
	bl	FlashReadPages
	ldr	r3, .L3161
	mov	r2, #36
	movw	r1, #65535
	ldrb	ip, [r3, #144]	@ zero_extendqisi2
	ldr	r3, [r7, #-536]
	mla	r5, r2, r5, r3
	ldr	r2, .L3161+20
	add	r2, r2, r4, asl #1
.L3035:
	cmp	r3, r5
	beq	.L3158
	ldr	r0, [r3]
	cmp	r0, #0
	bne	.L3036
	ldr	r0, [r3, #12]
	ldrh	lr, [r0]
	cmp	lr, r1
	beq	.L3037
	ldr	r0, [r0, #4]
	cmn	r0, #1
	beq	.L3037
	cmn	r8, #1
	ldr	r6, [r7, #-368]
	str	r0, [r7, #-368]
	bne	.L3037
	ldrh	r0, [r2]
	cmp	r0, r1
	bne	.L3038
	cmp	ip, #0
	beq	.L3037
.L3038:
	cmp	r6, fp
	mvneq	r8, #0
	movne	r8, r6
	b	.L3037
.L3036:
	ldrh	r1, [r9]
	movw	r3, #1848
	ldr	r2, .L3161+24
	strh	r1, [r2, r3]	@ movhi
	ldrb	r3, [r9, #8]	@ zero_extendqisi2
	cmp	r3, #0
	bne	.L3029
	ldr	r2, .L3161+20
	mov	r4, r4, asl #1
	ldr	r3, .L3161+8
	ldrh	r1, [r2, r4]
	movw	r2, #65535
	cmp	r1, r2
	bne	.L3040
	cmn	r8, #1
	strne	r8, [r3, #-368]
	bne	.L3029
	ldr	r2, [sp, #16]
	cmp	r2, fp
	bne	.L3150
	ldr	r2, [r3, #-368]
	b	.L3155
.L3040:
	cmp	r6, fp
	beq	.L3043
	cmn	r6, #1
	strne	r6, [r3, #-368]
	b	.L3029
.L3043:
	ldr	r2, [r3, #-368]
	cmp	r2, fp
	beq	.L3029
.L3155:
	sub	r2, r2, #1
	b	.L3150
.L3037:
	add	r3, r3, #36
	b	.L3035
.L3158:
	add	r4, r4, #1
	uxth	r4, r4
	b	.L3032
.L3045:
	ldr	r3, .L3161+8
	mvn	r2, #0
.L3150:
	str	r2, [r3, #-368]
.L3029:
	ldr	r2, .L3161+24
	movw	r3, #1850
	ldr	r10, [sp, #8]
	mov	r1, #1
	ldr	r4, .L3161+8
	strh	r1, [r2, r3]	@ movhi
.L3047:
	ldr	r3, .L3161+4
	movw	r1, #65535
	ldr	r8, [r4, #-536]
	mov	r0, #36
	ldr	r6, [sp, #20]
	mov	r5, #0
	ldrh	r7, [r3]
	ldrb	lr, [r3, #-2176]	@ zero_extendqisi2
	str	r5, [sp, #16]
.L3048:
	uxth	r3, r5
	cmp	r3, r7
	bcs	.L3159
	ldrh	r3, [r6, #2]!
	cmp	r3, r1
	beq	.L3049
	ldr	r2, [sp, #16]
	orr	r3, r10, r3, asl #10
	mla	r2, r0, r2, r8
	str	r3, [r2, #4]
	ldrb	ip, [r9, #8]	@ zero_extendqisi2
	cmp	ip, #1
	bne	.L3050
	cmp	lr, #0
	orrne	r3, r3, #-2147483648
	strne	r3, [r2, #4]
.L3050:
	ldr	r3, [sp, #16]
	add	r3, r3, #1
	uxth	r3, r3
	str	r3, [sp, #16]
.L3049:
	add	r5, r5, #1
	b	.L3048
.L3159:
	ldr	r0, [r4, #-536]
	ldr	r1, [sp, #16]
	ldr	r2, [sp, #24]
	bl	FlashReadPages
	mov	r3, #0
.L3153:
	str	r3, [sp, #28]
	ldr	r2, [sp, #16]
	ldrh	r3, [sp, #28]
	cmp	r3, r2
	bcs	.L3160
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
	bcc	.L3053
	ldr	r3, [sp, #32]
	ldr	r2, [sp, #8]
	cmp	r0, r3
	movcs	r3, #0
	movcc	r3, #1
	cmp	r10, r2
	movne	r3, #0
	cmp	r3, #0
	bne	.L3053
	ldr	r3, [sp, #12]
	ldr	r2, [sp, #4]
	cmp	r0, r3
	cmpeq	r10, r2
	beq	.L3054
	ldr	r3, [r8, r5]
	cmn	r3, #1
	beq	.L3055
	ldr	r7, [r7, #12]
	movw	r3, #61589
	ldrh	r2, [r7]
	cmp	r2, r3
	ldrneh	r0, [r9]
	bne	.L3151
	ldr	fp, [r7, #4]
	cmn	fp, #1
	beq	.L3057
	ldr	r1, [r4, #-3328]
	mov	r0, fp
	bl	ftl_cmp_data_ver
	cmp	r0, #0
	addne	r3, fp, #1
	strne	r3, [r4, #-3328]
.L3057:
	ldr	r6, [r7, #8]
	add	r1, sp, #56
	ldr	r3, [r7, #12]
	mov	r2, #0
	mov	r0, r6
	str	r3, [sp, #52]
	bl	log2phys
	ldr	r1, [r4, #-368]
	cmn	r1, #1
	beq	.L3058
	mov	r0, fp
	bl	ftl_cmp_data_ver
	cmp	r0, #0
	beq	.L3058
	ldr	r3, [sp, #52]
	cmn	r3, #1
	beq	.L3059
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
	bne	.L3060
	b	.L3061
.L3059:
	ldr	r3, [sp, #60]
	ldr	r2, [sp, #56]
	cmp	r2, r3
	bne	.L3053
	mov	r0, r6
	add	r1, sp, #52
	mov	r2, #1
	bl	log2phys
	b	.L3053
.L3060:
	ldr	r7, [r8, #8]
	cmp	r7, r6
	bne	.L3061
	ldr	r0, [r4, #-368]
	ldr	r1, [sp, #36]
	str	r2, [sp, #44]
	str	ip, [sp, #40]
	bl	ftl_cmp_data_ver
	cmp	r0, #0
	ldr	ip, [sp, #40]
	ldr	r2, [sp, #44]
	beq	.L3061
	ldr	r3, [sp, #56]
	ldr	r1, [sp, #60]
	cmp	r3, r1
	beq	.L3066
	ldr	r1, [sp, #52]
	cmp	r3, r1
	beq	.L3061
	cmn	r3, #1
	streq	r3, [r2, r5]
	beq	.L3065
	str	r3, [ip, #4]
	mov	r0, ip
	mov	r1, #1
	mov	r2, #0
	ldr	r8, [ip, #12]
	bl	FlashReadPages
.L3065:
	ldr	r3, [r4, #-536]
	ldr	r3, [r3, r5]
	cmn	r3, #1
	beq	.L3066
	ldr	r5, [r8, #4]
	ldr	r0, [r4, #-368]
	mov	r1, r5
	bl	ftl_cmp_data_ver
	cmp	r0, #0
	beq	.L3066
	ldr	r0, [sp, #36]
	mov	r1, r5
	bl	ftl_cmp_data_ver
	cmp	r0, #0
	beq	.L3061
.L3066:
	mov	r0, r7
	ldr	r1, [sp, #52]
	bl	FtlReUsePrevPpa
.L3061:
	mvn	r3, #0
	str	r3, [sp, #52]
	b	.L3068
.L3058:
	ldr	r3, [sp, #60]
	ldr	r2, [sp, #56]
	cmp	r2, r3
	beq	.L3068
	ldr	r3, [sp, #52]
	cmn	r3, #1
	beq	.L3070
	ldr	r2, .L3161
	ubfx	r3, r3, #10, #21
	ldr	r2, [r2, #2336]
	cmp	r3, r2
	bcs	.L3053
.L3070:
	mov	r0, r6
	add	r1, sp, #60
	mov	r2, #1
	bl	log2phys
	ldr	r5, [sp, #56]
	cmn	r5, #1
	beq	.L3068
	ldr	r3, [sp, #52]
	cmp	r5, r3
	beq	.L3068
	ubfx	r0, r5, #10, #16
	bl	P2V_block_in_plane
	ldr	r3, .L3161+28
	ldrh	r2, [r3]
	cmp	r2, r0
	beq	.L3072
	ldrh	r2, [r3, #48]
	cmp	r2, r0
	beq	.L3072
	ldrh	r3, [r3, #96]
	cmp	r3, r0
	bne	.L3068
.L3072:
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
	beq	.L3068
	mov	r0, fp
	bl	ftl_cmp_data_ver
	cmp	r0, #0
	bne	.L3068
	mov	r0, r6
	add	r1, sp, #56
	mov	r2, #1
	bl	log2phys
.L3068:
	ldr	r0, [sp, #52]
	cmn	r0, #1
	beq	.L3053
	ubfx	r0, r0, #10, #16
	bl	P2V_block_in_plane
	ldr	r2, [r4, #-3544]
	mov	r3, r0, asl #1
	mov	r1, r0
	ldrh	r3, [r2, r3]
	cmp	r3, #0
	beq	.L3073
.L3151:
	bl	decrement_vpc_count
	b	.L3053
.L3073:
	ldr	r0, .L3161+32
	bl	printk
	b	.L3053
.L3055:
	ldrh	r3, [r9]
	mov	r1, r6
	ldr	r2, .L3161+16
	ldr	r0, .L3161+36
	strh	r3, [r2]	@ movhi
	mov	r2, fp
	bl	printk
	ldr	r3, .L3161+24
	ldr	r3, [r3, #1852]
	cmp	r3, #31
	bhi	.L3074
	ldr	r2, .L3161+24
	ldr	r1, [sp, #60]
	add	r2, r2, r3, asl #2
	add	r3, r3, #1
	str	r1, [r2, #1856]
	ldr	r2, .L3161+24
	str	r3, [r2, #1852]
.L3074:
	ldrh	r0, [r9]
	bl	decrement_vpc_count
	ldr	r3, [r4, #-368]
	cmn	r3, #1
	beq	.L3152
	cmp	r3, fp
	bls	.L3053
.L3152:
	str	fp, [r4, #-368]
.L3053:
	ldr	r3, [sp, #28]
	add	r3, r3, #1
	b	.L3153
.L3160:
	ldrb	r3, [r9, #8]	@ zero_extendqisi2
	add	r10, r10, #1
	cmp	r3, #1
	uxth	r10, r10
	bne	.L3077
	ldr	r3, .L3161
	ldrb	r3, [r3, #144]	@ zero_extendqisi2
	cmp	r3, #0
	beq	.L3077
	ldr	r3, .L3161+40
	ldr	r2, [sp, #4]
	ldrh	r3, [r3]
	cmp	r2, r10
	cmpeq	r3, r10
	beq	.L3054
.L3077:
	ldr	r2, .L3161+44
	ldrh	r3, [r2]
	cmp	r10, r3
	bne	.L3047
	ldrh	r1, [r2, #-68]
	movw	r0, #65535
	mov	r3, #0
	strh	r10, [r9, #2]	@ movhi
	strh	r3, [r9, #4]	@ movhi
.L3078:
	uxth	r2, r3
	cmp	r2, r1
	bcs	.L3144
	ldr	lr, [sp, #20]
	add	r3, r3, #1
	ldrh	ip, [lr, #2]!
	cmp	ip, r0
	str	lr, [sp, #20]
	beq	.L3078
	strb	r2, [r9, #6]
	b	.L3144
.L3054:
	ldrb	r3, [sp, #12]	@ zero_extendqisi2
	mov	r0, r9
	ldr	r1, [sp, #4]
	strb	r3, [r9, #6]
	ldrh	r3, [sp, #4]
	strh	r3, [r9, #2]	@ movhi
.L3154:
	ldr	r2, [sp, #12]
	bl	ftl_sb_update_avl_pages
.L3144:
	mov	r0, #0
	add	sp, sp, #68
	@ sp needed
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L3162:
	.align	2
.L3161:
	.word	.LANCHOR0
	.word	.LANCHOR0+2320
	.word	.LANCHOR2
	.word	.LC149
	.word	.LANCHOR4+1848
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
	ldr	r5, .L3180
	ldr	r1, .L3180+4
	ldr	r0, .L3180+8
	bl	printk
	movw	r3, #2330
	ldrh	r2, [r5, r3]
	mov	r1, #0
	ldr	r6, .L3180+12
	mov	r2, r2, asl #1
	ldr	r0, [r6, #-476]
	bl	ftl_memset
.L3164:
	ldr	r3, [r5, #2448]
	cmp	r4, r3
	bcs	.L3178
	mov	r0, r4
	add	r1, sp, #4
	mov	r2, #0
	bl	log2phys
	ldr	r0, [sp, #4]
	cmn	r0, #1
	beq	.L3165
	ubfx	r0, r0, #10, #16
	bl	P2V_block_in_plane
	ldr	r2, [r6, #-476]
	mov	r0, r0, asl #1
	ldrh	r3, [r2, r0]
	add	r3, r3, #1
	strh	r3, [r2, r0]	@ movhi
.L3165:
	add	r4, r4, #1
	b	.L3164
.L3178:
	ldr	r8, .L3180+16
	mov	r7, #0
	ldr	r10, .L3180+12
	ldr	r9, .L3180+20
.L3167:
	ldrh	r3, [r8]
	uxth	r4, r7
	cmp	r3, r4
	bls	.L3179
	ldr	r3, [r6, #-3544]
	mov	r5, r4, asl #1
	movw	r1, #65535
	ldrh	r2, [r3, r5]
	ldr	r3, [r6, #-476]
	ldrh	r3, [r3, r5]
	cmp	r2, r1
	cmpne	r2, r3
	beq	.L3168
	ldrh	r1, [r9]
	cmp	r1, r4
	beq	.L3168
	ldr	r1, .L3180+24
	ldrh	r0, [r1]
	cmp	r0, r4
	beq	.L3168
	ldrh	r1, [r1, #-48]
	cmp	r1, r4
	beq	.L3168
	ldr	r0, .L3180+28
	mov	r1, r4
	bl	printk
	ldr	r3, [r10, #-3544]
	ldrh	r2, [r3, r5]
	cmp	r2, #0
	ldr	r2, [r10, #-476]
	ldrh	r2, [r2, r5]
	strh	r2, [r3, r5]	@ movhi
	beq	.L3168
	mov	r0, r4
	bl	update_vpc_list
.L3168:
	add	r7, r7, #1
	b	.L3167
.L3179:
	bl	l2p_flush
	bl	FtlVpcTblFlush
	add	sp, sp, #8
	@ sp needed
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, pc}
.L3181:
	.align	2
.L3180:
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
	ldr	r2, .L3235
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
	beq	.L3216
	cmp	r5, #0
	bne	.L3183
	b	.L3184
.L3216:
	mov	r5, #0
.L3183:
	ldr	r2, .L3235+4
	movw	r3, #2388
	ldrh	r3, [r2, r3]
	ldr	r2, [sp, #12]
	cmp	r2, r3
	bne	.L3185
.L3184:
	bl	FtlGcPageVarInit
.L3185:
	ldr	r6, .L3235+8
	mvn	r3, #0
	str	r3, [sp, #8]
	mov	r3, #0
	str	r3, [sp]
.L3186:
	ldrh	r1, [r4]
	movw	r3, #65535
	mov	r2, #0
	strb	r2, [r4, #8]
	cmp	r1, r3
	beq	.L3187
.L3188:
.L3213:
	ldr	r3, .L3235+12
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
	ldr	r3, .L3235+16
	ldrh	r0, [r3]
	ldrh	lr, [r3, #2]
.L3189:
	ldr	r2, [sp, #4]
	uxth	r3, r7
	cmp	r3, r2
	bcs	.L3233
	ldrh	r3, [r1, #2]!
	cmp	r3, r9
	beq	.L3190
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
.L3190:
	add	r7, r7, #1
	b	.L3189
.L3233:
	ldr	r0, [r6, #-536]
	mov	r1, r8
	mov	r2, #0
	mov	fp, #0
	bl	FlashReadPages
.L3192:
	uxth	r3, fp
	cmp	r3, r8
	bcs	.L3234
	ldr	r3, .L3235+8
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
	ldr	ip, .L3235+4
	ldr	r3, .L3235+8
	mov	r2, r0
	bne	.L3193
	ldrh	r0, [r10]
	movw	r1, #65535
	cmp	r0, r1
	bne	.L3194
.L3197:
	ldrb	r1, [ip, #144]	@ zero_extendqisi2
	cmp	r1, #0
	beq	.L3228
	mov	r1, #1
	str	r1, [r3, #-372]
	b	.L3187
.L3194:
	ldr	r1, .L3235+4
	ldr	r0, [r10, #8]
	ldr	r1, [r1, #2448]
	cmp	r0, r1
	bls	.L3229
	b	.L3197
.L3228:
	ldrh	r3, [r4]
	ldr	r2, .L3235+8
	mov	r3, r3, asl #1
	ldr	r2, [r2, #-3544]
	b	.L3232
.L3229:
	ldr	r3, .L3235+4
	ldrb	r3, [r3]	@ zero_extendqisi2
	cmp	r3, #0
	beq	.L3201
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
	beq	.L3201
	str	r3, [sp, #32]
	mov	r2, r7
	ldr	r3, [r6, #-504]
	add	r0, sp, #28
	mov	r1, #1
	str	r3, [sp, #36]
	ldr	r3, [r6, #-496]
	str	r3, [sp, #40]
	bl	FlashReadPages
	ldr	r3, .L3235+20
	ldr	r2, [r6, #-536]
	ldrh	r3, [r3]
	add	r9, r2, r9
	mov	r3, r3, asl #7
	ldr	r2, [sp, #36]
	b	.L3203
.L3204:
	add	r7, r7, #1
.L3203:
	cmp	r7, r3
	beq	.L3201
	ldr	r1, [r9, #8]
	ldr	r0, [r1, r7, asl #2]
	ldr	r1, [r2, r7, asl #2]
	cmp	r0, r1
	beq	.L3204
	ldrh	r1, [r4]
	ldr	r2, [sp, #32]
	ldr	r0, .L3235+24
	bl	printk
	ldrh	r3, [r4]
	ldr	r2, [r6, #-3544]
	mov	r1, #0
	mov	r3, r3, asl #1
.L3232:
	strh	r1, [r2, r3]	@ movhi
	ldrh	r0, [r4]
	bl	INSERT_FREE_LIST
	ldr	r2, .L3235+28
	mvn	r3, #0
	strh	r3, [r4]	@ movhi
	strh	r3, [r2]	@ movhi
	b	.L3231
.L3201:
	ldr	r0, [r10, #12]
	add	fp, fp, #1
	ldr	r1, [sp, #4]
	ldr	r2, [r10, #8]
	bl	FtlGcUpdatePage
	b	.L3192
.L3193:
	ldr	r2, [sp, #4]
	ldr	r0, .L3235+32
	ldrh	r1, [r4]
	str	r3, [sp, #20]
	str	ip, [sp, #16]
	bl	printk
	ldrh	r5, [r4]
	ldr	ip, [sp, #16]
	ldr	r3, [sp, #20]
	ldr	r2, [ip, #2244]
	cmp	r2, #0
	bne	.L3207
	ldrb	r2, [ip, #144]	@ zero_extendqisi2
	cmp	r2, #0
	beq	.L3208
.L3207:
	ldr	r1, [r3, #-3608]
	mov	r2, r5, asl #1
	ldrh	r2, [r1, r2]
	cmp	r2, #159
	bls	.L3209
.L3208:
	ldr	r2, [r3, #-536]
	ldr	r2, [r2, r9]
	cmn	r2, #1
	bne	.L3210
.L3209:
	ldr	r3, [r3, #-536]
	add	r9, r3, r9
	ldr	r3, [r9, #4]
	str	r3, [sp, #8]
.L3210:
	ldr	r3, .L3235+8
	mov	r5, r5, asl #1
	mov	r2, #0
	ldr	r3, [r3, #-3544]
	strh	r2, [r3, r5]	@ movhi
	ldrh	r0, [r4]
	bl	INSERT_FREE_LIST
	mvn	r3, #0
	strh	r3, [r4]	@ movhi
.L3231:
	bl	FtlGcPageVarInit
	mov	r5, #0
	b	.L3186
.L3234:
	ldr	r3, [sp]
	add	r5, r5, #1
	ldr	r2, [sp, #12]
	add	r3, r3, #1
	uxth	r5, r5
	cmp	r3, r2
	str	r3, [sp]
	ldr	r2, .L3235+36
	bcs	.L3212
.L3214:
	ldrh	r3, [r2]
	cmp	r3, r5
	bhi	.L3213
	mov	r2, #0
	b	.L3187
.L3212:
	ldr	r1, .L3235+40
	movw	r0, #65535
	ldrh	r3, [r1]
	cmp	r3, r0
	beq	.L3214
	ldr	r0, [sp]
	add	r3, r3, r0
	strh	r3, [r1]	@ movhi
	ldrh	r3, [r2]
	cmp	r3, r5
	bls	.L3214
	b	.L3215
.L3187:
	ldr	r1, .L3235
	movw	r3, #3448
	mvn	r0, #0
	strh	r5, [r4, #2]	@ movhi
	strb	r2, [r4, #6]
	strh	r0, [r1, r3]	@ movhi
	mov	r0, r4
	mov	r1, r5
	bl	ftl_sb_update_avl_pages
.L3215:
	ldr	r0, [sp, #8]
	add	sp, sp, #68
	@ sp needed
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L3236:
	.align	2
.L3235:
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
	ldr	r5, .L3254
	ldr	r4, .L3254+4
	ldr	r9, [r5, #-3152]
	mov	r6, r5
	cmp	r9, #0
	beq	.L3238
	ldr	r2, [r5, #-3148]
	ldr	r3, [r4, #2448]
	cmp	r2, r3
	bcs	.L3239
	mov	r5, #2048
	mov	r7, r6
.L3244:
	ldr	r0, [r6, #-3148]
	ldr	r3, [r4, #2448]
	cmp	r0, r3
	bcs	.L3243
	mov	r2, #0
	mov	r1, sp
	bl	log2phys
	ldr	r3, [r7, #-3148]
	add	r3, r3, #1
	str	r3, [r7, #-3148]
	ldr	r2, [sp]
	cmn	r2, #1
	beq	.L3242
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
	bne	.L3243
	ldr	r0, [sp]
	ubfx	r0, r0, #10, #16
	bl	P2V_block_in_plane
	bl	FtlGcRefreshBlock
.L3243:
	mvn	r0, #0
	b	.L3246
.L3242:
	subs	r5, r5, #1
	bne	.L3244
	b	.L3243
.L3239:
	ldr	r3, [r5, #-3364]
	mov	r0, #0
	str	r0, [r5, #-3152]
	str	r0, [r5, #-3148]
	str	r3, [r5, #-3156]
	b	.L3246
.L3238:
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
	bhi	.L3248
	mov	r1, r1, lsr #10
	mov	r0, #1000
	mul	r0, r0, r3
	add	r1, r1, #1
	bl	__aeabi_uidiv
	add	r0, r0, r7
	cmp	r0, r8
	bcc	.L3248
	ldrh	r3, [r10, #28]
	ands	r0, r4, r3
	movne	r0, r9
	bne	.L3246
	ldr	r2, [r5, #-3132]
	cmp	r2, r3
	beq	.L3246
.L3248:
	ldrh	r3, [r10, #28]
	mov	r0, #0
	str	r8, [r6, #-3156]
	str	r0, [r6, #-3148]
	str	r3, [r6, #-3132]
	mov	r3, #1
	str	r3, [r6, #-3152]
.L3246:
	add	sp, sp, #40
	@ sp needed
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, pc}
.L3255:
	.align	2
.L3254:
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
	ldr	r4, .L3295
	.pad #28
	sub	sp, sp, #28
	ldr	r6, .L3295+4
	ldr	ip, [r4, #-3616]
	ldrh	r1, [r6, r3]
	cmp	ip, #0
	bne	.L3293
	sub	r7, r4, #3424
	movw	lr, #65535
	ldrh	r5, [r7, #-4]
	cmp	r5, lr
	bne	.L3259
.L3268:
	ldrh	r2, [r7, #-4]
	movw	r3, #65535
	ldr	r5, .L3295
	mov	r8, #0
	cmp	r2, r3
	str	r8, [r4, #-372]
	sub	r9, r5, #3424
	beq	.L3293
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
	b	.L3269
.L3259:
	cmp	r0, #0
	beq	.L3262
	ldr	r2, .L3295+8
	movw	r3, #3448
	ldrh	r0, [r2, r3]
	cmp	r0, lr
	beq	.L3263
.L3264:
	mov	r1, #2
	b	.L3262
.L3263:
	strh	ip, [r2, r3]	@ movhi
	sub	r3, r4, #3520
	ldrh	r3, [r3, #-8]
	cmp	r3, #17
	bhi	.L3264
.L3262:
	ldr	r0, .L3295+12
	bl	FtlGcScanTempBlk
	cmn	r0, #1
	str	r0, [sp, #20]
	beq	.L3265
	ldr	r3, .L3295
	mov	r5, r5, asl #1
	ldr	r2, [r3, #-3608]
	ldrh	r3, [r2, r5]
	cmp	r3, #4
	bls	.L3266
	sub	r3, r3, #5
	mov	r0, #1
	strh	r3, [r2, r5]	@ movhi
	bl	FtlEctTblFlush
.L3266:
	ldr	r3, [r4, #-372]
	ldr	r2, .L3295
	cmp	r3, #0
	bne	.L3267
	ldr	r0, [sp, #20]
	ldr	r3, [r2, #-3136]
	ubfx	r0, r0, #10, #16
	add	r3, r3, #1
	str	r3, [r2, #-3136]
	bl	FtlBbmMapBadBlock
	bl	FtlBbmTblFlush
.L3267:
	mov	r3, #0
	str	r3, [r4, #-372]
	b	.L3279
.L3265:
	ldr	r2, .L3295+8
	movw	r3, #3448
	ldrh	r2, [r2, r3]
	movw	r3, #65535
	cmp	r2, r3
	bne	.L3279
	b	.L3268
.L3272:
	ldr	r3, [r5, #4]
	cmp	r0, r3
	bne	.L3291
.L3271:
	add	r8, r8, #1
.L3269:
	ldrh	r2, [fp]
	uxth	r3, r8
	cmp	r2, r3
	bls	.L3294
	mov	r9, #12
	ldr	r2, [r6, #2448]
	mul	r9, r9, r3
	ldr	r3, [sp, #8]
	ldr	ip, [r3, #-2672]
	add	r5, ip, r9
	ldr	r0, [r5, #8]
	cmp	r0, r2
	bcs	.L3291
	mov	r2, #0
	add	r1, sp, #20
	str	ip, [sp, #12]
	bl	log2phys
	ldr	ip, [sp, #12]
	ldr	r0, [sp, #20]
	ldr	r2, [ip, r9]
	cmp	r0, r2
	bne	.L3272
	ubfx	r0, r0, #10, #16
	bl	P2V_block_in_plane
	add	r1, r5, #4
	mov	r2, #1
	mov	r9, r0
	ldr	r0, [r5, #8]
	bl	log2phys
	mov	r0, r9
	b	.L3292
.L3291:
	ldrh	r0, [r10]
.L3292:
	bl	decrement_vpc_count
	b	.L3271
.L3294:
	movw	r0, #65535
	bl	decrement_vpc_count
	ldrb	r3, [r6, #144]	@ zero_extendqisi2
	cmp	r3, #0
	beq	.L3274
	ldr	r0, .L3295+16
	ldrh	r1, [r7, #-4]
	bl	printk
.L3274:
	ldrh	r0, [r7, #-4]
	ldr	r2, [r4, #-3544]
	mov	r3, r0, asl #1
	ldrh	r3, [r2, r3]
	cmp	r3, #0
	beq	.L3275
	bl	INSERT_DATA_LIST
	b	.L3276
.L3275:
	bl	INSERT_FREE_LIST
.L3276:
	ldr	r3, [sp, #4]
	mvn	r5, #0
	ldr	r4, .L3295
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
	beq	.L3277
	ldr	r1, [r4, #-3308]
	cmp	r1, #39
	bhi	.L3277
	cmp	r2, r3
	subcc	r4, r4, #2720
	movcc	r3, r3, asl #1
	strcch	r3, [r4]	@ movhi
	b	.L3293
.L3277:
	add	r1, r3, r3, asl #1
	cmp	r2, r1, asr #2
	ble	.L3293
	ldrb	r0, [r6, #144]	@ zero_extendqisi2
	ldr	r2, .L3295+20
	cmp	r0, #0
	moveq	r3, #20
	streqh	r3, [r2]	@ movhi
	beq	.L3258
	sub	r3, r3, #2
	strh	r3, [r2]	@ movhi
.L3293:
	mov	r0, #0
	b	.L3258
.L3279:
	mov	r0, #1
.L3258:
	add	sp, sp, #28
	@ sp needed
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L3296:
	.align	2
.L3295:
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
	ldr	r7, .L3300
	ldr	r6, .L3300+4
	ldr	r4, .L3300+8
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
.L3301:
	.align	2
.L3300:
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
	ldr	r4, .L3304
	ldr	r3, .L3304+4
	sub	r5, r4, #3520
	sub	r4, r4, #3472
	sub	r5, r5, #4
	sub	r4, r4, #4
	str	r6, [r3, #1852]
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
.L3305:
	.align	2
.L3304:
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
	movw	r3, #1850
	ldr	r4, .L3324
	mov	r2, #0
	ldr	r6, .L3324+4
	ldr	r5, .L3324+8
	strh	r2, [r4, r3]	@ movhi
	movw	r3, #1848
	mvn	r2, #0
	strh	r2, [r4, r3]	@ movhi
	ldr	r3, [r6, #2324]
	uxth	r0, r3
	bl	FtlFreeSysBlkQueueInit
	bl	FtlScanSysBlk
	sub	r3, r5, #3296
	ldrh	r2, [r3, #-4]
	movw	r3, #65535
	cmp	r2, r3
	bne	.L3307
.L3309:
	mvn	r8, #0
	b	.L3308
.L3307:
	bl	FtlLoadSysInfo
	subs	r8, r0, #0
	bne	.L3309
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
.L3310:
	cmp	r3, r1
	bge	.L3315
	mla	ip, r0, r3, r2
	ldr	ip, [ip, #4]
	cmp	ip, #0
	bge	.L3311
.L3315:
	ldr	r4, .L3324+12
	cmp	r3, r1
	add	r9, r4, #80
	ldrh	r2, [r4, #28]
	add	r2, r2, #1
	strh	r2, [r4, #28]	@ movhi
	bge	.L3322
	b	.L3312
.L3311:
	add	r3, r3, #1
	b	.L3310
.L3322:
	movw	r3, #1850
	ldrh	r3, [r7, r3]
	cmp	r3, #0
	beq	.L3316
.L3312:
	ldrh	r2, [r9, #-4]
	ldr	r3, .L3324+16
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
.L3316:
	ldrh	r0, [r9, #-4]
	movw	r3, #65535
	ldr	r5, .L3324+16
	cmp	r0, r3
	beq	.L3317
	ldrh	r3, [r5, #4]
	cmp	r3, #0
	bne	.L3317
	ldrh	r3, [r5, #52]
	add	r9, r5, #52
	add	r7, r5, #48
	cmp	r3, #0
	bne	.L3317
	bl	FtlGcRefreshOpenBlock
	ldrh	r0, [r5, #48]
	bl	FtlGcRefreshOpenBlock
	bl	FtlVpcTblFlush
	mov	r0, r5
	bl	allocate_new_data_superblock
	mov	r0, r7
	bl	allocate_new_data_superblock
.L3317:
	ldrb	r3, [r6]	@ zero_extendqisi2
	cmp	r3, #0
	bne	.L3318
	ldrh	r3, [r4, #28]
	tst	r3, #31
	bne	.L3308
.L3318:
	bl	FtlVpcCheckAndModify
.L3308:
	mov	r0, r8
	ldmfd	sp!, {r3, r4, r5, r6, r7, r8, r9, pc}
.L3325:
	.align	2
.L3324:
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
	ldr	r4, .L3361
	ldr	r6, [r4, #-3616]
	cmp	r6, #0
	bne	.L3328
	ldr	r5, .L3361+4
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
	beq	.L3329
	bl	FtlMakeBbt
.L3329:
	ldr	r0, .L3361+8
	mov	r2, #0
	ldr	ip, .L3361+12
.L3330:
	ldrh	r1, [r0]
	uxth	r3, r2
	add	r2, r2, #1
	cmp	r3, r1, asl #7
	bge	.L3358
	ldr	lr, [r4, #-508]
	mvn	r1, r3
	orr	r1, r3, r1, asl #16
	str	r1, [lr, r3, asl #2]
	ldr	r1, [r4, #-504]
	str	ip, [r1, r3, asl #2]
	b	.L3330
.L3358:
	ldr	r2, .L3361+4
	movw	r3, #2328
	ldr	r7, .L3361+16
	mov	r8, #0
	ldrh	r6, [r2, r3]
.L3332:
	ldrh	r3, [r7]
	ldr	r10, .L3361+4
	cmp	r3, r6
	ldr	r9, .L3361+16
	bls	.L3359
	mov	r0, r6
	mov	r1, #1
	bl	FtlLowFormatEraseBlock
	add	r6, r6, #1
	uxth	r6, r6
	add	r0, r8, r0
	uxth	r8, r0
	b	.L3332
.L3359:
	add	r7, r10, #2320
	sub	r3, r8, #3
	ldrh	r1, [r7]
	cmp	r3, r1, asl #1
	blt	.L3334
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
.L3335:
	ldrh	r3, [r9]
	cmp	r3, r6
	bls	.L3334
	mov	r0, r6
	mov	r1, #1
	bl	FtlLowFormatEraseBlock
	add	r6, r6, #1
	uxth	r6, r6
	add	r0, r8, r0
	uxth	r8, r0
	b	.L3335
.L3334:
	ldr	r10, .L3361+20
	mov	r9, #0
	mov	r6, r9
.L3337:
	ldrh	r2, [r10]
	uxth	r0, r9
	ldr	r3, .L3361+4
	add	r9, r9, #1
	cmp	r2, r0
	bls	.L3360
	mov	r1, #0
	bl	FtlLowFormatEraseBlock
	add	r0, r6, r0
	uxth	r6, r0
	b	.L3337
.L3360:
	movw	r2, #2330
	ldrh	r9, [r7]
	ldrh	r2, [r3, r2]
	str	r3, [sp, #4]
	mov	r1, r9
	ldr	r7, .L3361+24
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
	ble	.L3339
	rsb	r0, r6, r2
	mov	r1, r9
	str	r3, [sp]
	bl	__aeabi_uidiv
	ldr	r3, [sp]
	str	r0, [r3, #2448]
	mov	r0, r0, lsr #5
	add	r0, r0, #24
	strh	r0, [r7, #-8]	@ movhi
.L3339:
	ldr	r3, [r5, #2244]
	cmp	r3, #1
	bne	.L3340
	ldrh	r3, [r7, #-8]
	mov	r0, r6
	mov	r1, r9
	str	r3, [sp]
	bl	__aeabi_uidiv
	ldr	r3, [sp]
	uxtah	r0, r3, r0
	add	r3, r3, r0, asr #2
	strh	r3, [r7, #-8]	@ movhi
.L3340:
	ldrb	r3, [r5, #144]	@ zero_extendqisi2
	cmp	r3, #0
	beq	.L3341
	ldrh	r3, [r7, #-8]
	mov	r0, r6
	mov	r1, r9
	str	r3, [sp]
	bl	__aeabi_uidiv
	ldr	r3, [sp]
	uxtah	r0, r3, r0
	add	r3, r3, r0, asr #2
	strh	r3, [r7, #-8]	@ movhi
.L3341:
	movw	r3, #2382
	ldrh	r3, [r5, r3]
	cmp	r3, #0
	beq	.L3343
	ldrh	r2, [r7, #-8]
	add	r2, r2, r3, lsr #1
	strh	r2, [r7, #-8]	@ movhi
	mul	r2, r9, r3
	cmp	r2, r6
	addgt	r3, r3, #32
	ldrgt	r2, .L3361+4
	addgt	r3, r10, r3
	strgt	fp, [r2, #2448]
	ldrgt	r2, .L3361+24
	strgth	r3, [r2, #-8]	@ movhi
.L3343:
	ldrh	r2, [r7, #-8]
	ldr	r3, [r5, #2448]
	rsb	r3, r2, r3
	mul	r9, r9, r3
	movw	r3, #2388
	ldrh	r3, [r5, r3]
	str	r9, [r4, #1788]
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
	bls	.L3345
	ldr	r0, .L3361+28
	mov	r2, r2, lsr #5
	bl	printk
.L3345:
	movw	r3, #2330
	ldr	r6, .L3361+32
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
.L3346:
	ldr	r10, .L3361+36
	ldr	r5, .L3361
	mov	r0, r10
	bl	make_superblock
	ldrb	r3, [r4, #-3517]	@ zero_extendqisi2
	cmp	r3, #0
	ldrh	r3, [r8]
	bne	.L3347
	ldr	r2, [r6, #-3544]
	mov	r3, r3, asl #1
	strh	r9, [r2, r3]	@ movhi
	ldrh	r3, [fp]
	add	r3, r3, #1
	strh	r3, [fp]	@ movhi
	b	.L3346
.L3347:
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
.L3348:
	ldr	r8, .L3361+40
	ldr	r5, .L3361
	mov	r0, r8
	bl	make_superblock
	ldrb	r3, [r4, #-3469]	@ zero_extendqisi2
	cmp	r3, #0
	ldrh	r3, [r6]
	bne	.L3349
	ldr	r2, [r9, #-3544]
	mov	r3, r3, asl #1
	strh	r10, [r2, r3]	@ movhi
	ldrh	r3, [r6]
	add	r3, r3, #1
	strh	r3, [r6]	@ movhi
	b	.L3348
.L3349:
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
	ldr	r3, .L3361+44
	mov	r2, #0
	strh	r2, [r3, #2]	@ movhi
	ldr	r2, [r5, #1788]
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
	ldreq	r3, .L3361+48
	moveq	r2, #1
	streq	r2, [r3, #504]
.L3328:
	mov	r0, #0
	add	sp, sp, #12
	@ sp needed
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L3362:
	.align	2
.L3361:
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
	ldr	r4, .L3392
	ldrb	r3, [r4, #144]	@ zero_extendqisi2
	cmp	r3, #0
	beq	.L3364
.L3366:
	mov	r0, #0
	b	.L3365
.L3364:
	ldr	r5, .L3392+4
	ldr	r0, [r5, #1684]
	bl	FlashInit
	cmp	r0, #0
	bne	.L3366
	bl	FlashLoadFactorBbt
	cmp	r0, #0
	beq	.L3367
	bl	FlashMakeFactorBbt
.L3367:
	ldr	r0, [r5, #1716]
	bl	FlashReadIdbDataRaw
	cmp	r0, #0
	beq	.L3368
	mov	r1, #0
	mov	r2, #16
	mov	r0, sp
	bl	FlashReadFacBbtData
	mov	r3, #0
	mov	r2, r3
	mov	ip, #1
	ldr	r1, [sp]
.L3369:
	ands	lr, r1, ip, asl r2
	add	r0, r3, #1
	add	r2, r2, #1
	movne	r3, r0
	cmp	r2, #16
	bne	.L3369
	cmp	r3, #6
	bls	.L3388
	mov	r2, #0
	mov	ip, #1
.L3372:
	ands	lr, r1, ip, asl r2
	add	r0, r3, #1
	add	r2, r2, #1
	movne	r3, r0
	cmp	r2, #24
	bne	.L3372
	cmp	r3, #17
	movhi	r3, #36
	strhib	r3, [r4, #1]
	bhi	.L3371
.L3388:
	strb	r2, [r4, #1]
.L3371:
	ldrb	r3, [r4, #1]	@ zero_extendqisi2
	strh	r3, [r4, #142]	@ movhi
.L3368:
	ldr	r1, .L3392+8
	ldr	r0, .L3392+12
	bl	printk
	ldr	r0, .L3392+16
	bl	FtlConstantsInit
	bl	FtlVariablesInit
	ldr	r0, [r4, #2324]
	mov	r4, #1
	uxth	r0, r0
	bl	FtlFreeSysBlkQueueInit
.L3374:
	bl	FtlLoadBbt
	cmp	r0, #0
	beq	.L3375
.L3390:
	bl	FtlLowFormat
	cmp	r4, #3
	addls	r4, r4, #1
	bls	.L3374
.L3391:
	mvn	r0, #0
	b	.L3365
.L3375:
	bl	FtlSysBlkInit
	cmp	r0, #0
	bne	.L3390
	ldr	r3, .L3392+20
	mov	r2, #1
	str	r2, [r3, #504]
.L3365:
	add	sp, sp, #20
	@ sp needed
	ldmfd	sp!, {r4, r5, pc}
.L3393:
	.align	2
.L3392:
	.word	.LANCHOR0
	.word	.LANCHOR2
	.word	.LC77
	.word	.LC76
	.word	.LANCHOR0+116
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
	ldr	r5, .L3411
	ldr	r3, [r5, #-3616]
	cmp	r3, #0
	beq	.L3395
.L3398:
	mov	r0, #0
	ldmfd	sp!, {r4, r5, r6, r7, r8, pc}
.L3395:
	ldr	r3, .L3411+4
	ldrb	r3, [r3, #144]	@ zero_extendqisi2
	cmp	r3, #0
	beq	.L3397
	ldr	r3, [r5, #-2704]
	tst	r3, #1
	beq	.L3397
	ldr	r3, .L3411+8
	ldrh	r3, [r3, #4]
	cmp	r3, #0
	bne	.L3398
.L3397:
	mov	r2, #0
	ldr	r0, [r5, #-532]
	ldr	r1, [r5, #-2704]
	mov	r3, r2
	bl	FlashProgPages
	ldr	r7, .L3411
	mov	r6, #0
	mov	r8, #36
.L3399:
	ldr	r1, [r5, #-2704]
	uxth	r3, r6
	ldr	r4, .L3411
	cmp	r3, r1
	bcs	.L3410
	mul	r3, r8, r3
	ldr	r0, [r7, #-532]
	add	r6, r6, #1
	add	r1, r0, r3
	ldr	r0, [r0, r3]
	ldr	r2, [r1, #12]
	cmn	r0, #1
	bne	.L3400
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
	b	.L3409
.L3400:
	ldr	r0, [r2, #12]
	ldr	r1, [r1, #4]
	ldr	r2, [r2, #8]
	bl	FtlGcUpdatePage
	b	.L3399
.L3410:
	ldr	r0, [r4, #-532]
	bl	FtlGcBufFree
	mov	r3, #0
	str	r3, [r4, #-2704]
	ldr	r3, .L3411+8
	ldrh	r3, [r3, #4]
	cmp	r3, #0
	bne	.L3398
	mov	r0, #1
	bl	FtlGcFreeTempBlock
.L3409:
	mov	r0, #1
	ldmfd	sp!, {r4, r5, r6, r7, r8, pc}
.L3412:
	.align	2
.L3411:
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
	ldr	r3, .L3417
	sub	r2, r3, #4
	ldrh	r1, [r3, #-4]
	movw	r3, #65535
	cmp	r1, r3
	beq	.L3414
	ldrh	r3, [r2, #4]
	cmp	r3, #0
	bne	.L3415
.L3414:
	ldr	r4, .L3417+4
	bl	FtlCacheWriteBack
	mov	r0, #0
	mov	r5, #0
	bl	FtlGcFreeTempBlock
	ldr	r0, .L3417+8
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
.L3415:
	ldr	r0, .L3417+8
	ldmfd	sp!, {r3, r4, r5, lr}
	b	get_new_active_ppa
.L3418:
	.align	2
.L3417:
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
	ldr	r3, .L3583
	ldr	ip, [r3, #-3616]
	cmp	ip, #0
	bne	.L3513
	stmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.save {r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.pad #44
	sub	sp, sp, #44
	ldr	r4, .L3583+4
	ldr	r8, [r4, #504]
	cmp	r8, #1
	bne	.L3515
	ldr	r2, [r3, #-560]
	cmp	r2, #0
	bne	.L3515
	mov	r9, r0
	sub	r0, r3, #3536
	ldrh	r0, [r0]
	cmp	r0, #47
	movls	r0, r2
	bls	.L3572
	movw	r2, #3448
	str	r1, [sp, #12]
	ldrh	r1, [r4, r2]
	movw	r2, #65535
	mov	r5, r3
	str	r9, [sp, #16]
	cmp	r1, r2
	bne	.L3421
.L3424:
	ldr	r4, .L3583+8
	movw	r2, #65535
	ldrh	r0, [r4, #-8]
	cmp	r0, r2
	bne	.L3422
	b	.L3423
.L3421:
	sub	r3, r3, #3424
	ldrh	r3, [r3, #-4]
	cmp	r3, r2
	beq	.L3424
	mov	r0, r8
	bl	FtlGcFreeTempBlock
	cmp	r0, #0
	beq	.L3424
	mov	r0, r8
	b	.L3572
.L3422:
	ldrh	r3, [r4, #-10]
	cmp	r3, r2
	bne	.L3423
	ldrh	r1, [r4, #-6]
	cmp	r1, r3
	beq	.L3423
	ldrh	r2, [r4, #-4]
	cmp	r2, r3
	strneh	r0, [r4, #-10]	@ movhi
	strneh	r1, [r4, #-8]	@ movhi
	mvnne	r3, #0
	strneh	r2, [r4, #-6]	@ movhi
	strneh	r3, [r4, #-4]	@ movhi
.L3423:
	ldr	r3, [sp, #16]
	ldr	r8, [r5, #-2716]
	ldr	r6, .L3583+12
	cmp	r3, #1
	add	r8, r8, #1
	add	r8, r8, r3, asl #7
	sub	r7, r6, #240
	str	r8, [r5, #-2716]
	bne	.L3435
	ldr	r9, .L3583+16
	ldr	r3, [r9, #2244]
	cmp	r3, #0
	bne	.L3426
	ldrb	r3, [r9, #144]	@ zero_extendqisi2
	cmp	r3, #0
	beq	.L3435
.L3426:
	ldr	r3, [r5, #-3308]
	ldr	fp, .L3583
	cmp	r3, #39
	bhi	.L3435
	ldr	r10, .L3583+20
	ldrh	r3, [r10]
	add	r8, r8, r3
	str	r8, [fp, #-2716]
	bl	FtlGcReFreshBadBlk
	ldrh	r8, [r6, #-4]
	movw	r3, #65535
	cmp	r8, r3
	bne	.L3435
	ldrh	r3, [r4, #-10]
	cmp	r3, r8
	bne	.L3435
	ldr	r3, [fp, #-2716]
	cmp	r3, #1024
	bhi	.L3427
	ldrh	r3, [r7, #-8]
	cmp	r3, #63
	bhi	.L3435
.L3427:
	ldr	r1, .L3583
	mov	r3, #0
	strh	r3, [r10]	@ movhi
	sub	r8, r1, #2704
	ldrh	r0, [r7, #-8]
	ldr	r2, .L3583+20
	ldrh	r10, [r8, #-14]
	add	ip, r10, #64
	cmp	r0, ip
	bgt	.L3435
	str	r3, [r1, #-2716]
	ldr	r3, [r1, #-3308]
	cmp	r3, #0
	moveq	r3, #6
	beq	.L3574
	cmp	r3, #5
	bhi	.L3429
	mov	r3, #18
.L3574:
	strh	r3, [r2]	@ movhi
.L3429:
	mov	r0, #32
	bl	List_get_gc_head_node
	movw	ip, #65535
	uxth	r3, r0
	cmp	r3, ip
	beq	.L3434
	ldrh	r0, [r8, #-8]
	ldr	r2, .L3583
	cmp	r0, #0
	sub	r10, r2, #2704
	beq	.L3431
	mov	r8, r3, asl #1
	movw	r3, #2390
	ldrh	lr, [r9, r3]
	ldr	r3, .L3583+24
	ldr	r2, [r2, #-3544]
	ldrh	r3, [r3]
	ldrh	r1, [r2, r8]
	mul	r3, r3, lr
	add	r3, r3, #1
	cmp	r1, r3
	bgt	.L3434
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
	beq	.L3434
	mov	ip, fp, asl #1
	ldr	r0, .L3583+28
	str	ip, [sp, #20]
	ldrh	r3, [r2, ip]
	ldrh	r2, [r2, r8]
	str	r2, [sp]
	mov	r2, fp
	bl	printk
	ldrh	r3, [r10, #-8]
	cmp	r3, #40
	ldr	ip, [sp, #20]
	bls	.L3432
	ldr	r3, [r10, #-840]
	ldrh	r3, [r3, ip]
	cmp	r3, #32
	strhih	r9, [r10, #-8]	@ movhi
.L3432:
	ldr	r3, .L3583+20
	mov	r2, #6
	strh	r2, [r3]	@ movhi
	b	.L3436
.L3431:
	mov	r3, #1
	strh	r3, [r10, #-8]	@ movhi
.L3434:
	bl	GetSwlReplaceBlock
	movw	r3, #65535
	cmp	r0, r3
	mov	fp, r0
	bne	.L3436
	ldr	r3, .L3583+20
	mov	r2, #0
	strh	r2, [r3]	@ movhi
.L3435:
	ldrh	ip, [r6, #-4]
	movw	r3, #65535
	ldr	r8, .L3583
	cmp	ip, r3
	bne	.L3577
	sub	r3, r8, #3424
	ldrh	r3, [r3, #-4]
	cmp	r3, ip
	movne	fp, ip
	beq	.L3580
.L3436:
	movw	r1, #65535
	rsb	r3, r1, fp
	clz	r3, r3
	ldr	r2, [sp, #16]
	ldr	r8, .L3583+32
	mov	r3, r3, lsr #5
	cmp	r2, #0
	movne	ip, #0
	andeq	ip, r3, #1
	cmp	ip, #0
	beq	.L3449
	ldrh	r0, [r8, #-8]
	cmp	r0, #24
	movhi	r9, #1
	bhi	.L3450
	ldr	r2, .L3583+16
	movw	r3, #2388
	cmp	r0, #16
	ldrh	r9, [r2, r3]
	movhi	r9, r9, lsr #5
	bhi	.L3450
	cmp	r0, #12
	movhi	r9, r9, lsr #4
	bhi	.L3450
	cmp	r0, #8
	movhi	r9, r9, lsr #2
.L3450:
	ldr	r1, .L3583
	sub	r2, r1, #2720
	ldrh	r3, [r2]
	cmp	r3, r0
	bcs	.L3454
	sub	r3, r1, #3424
	movw	r0, #65535
	ldrh	r3, [r3, #-4]
	cmp	r3, r0
	bne	.L3455
	ldrh	r0, [r4, #-10]
	cmp	r0, r3
	bne	.L3455
	ldr	r3, .L3583+20
	ldrh	r0, [r3]
	cmp	r0, #0
	bne	.L3456
	ldr	r3, .L3583+16
	ldr	r1, [r1, #-3372]
	ldr	r3, [r3, #2448]
	add	r3, r3, r3, asl #1
	cmp	r1, r3, lsr #2
	movcs	r3, #18
	bcs	.L3457
.L3456:
	ldr	r3, .L3583+36
	ldrh	r3, [r3, #-8]
	add	r3, r3, r3, asl #1
	ubfx	r3, r3, #2, #16
.L3457:
	strh	r3, [r2]	@ movhi
	mov	r3, #0
	str	r3, [r5, #-2708]
	b	.L3572
.L3455:
	ldr	r3, .L3583+36
	ldrh	r3, [r3, #-8]
	add	r3, r3, r3, asl #1
	mov	r3, r3, asr #2
	strh	r3, [r2]	@ movhi
.L3454:
	ldr	r3, .L3583+16
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
	b	.L3459
.L3449:
	ldrh	r0, [r8, #92]
	add	r2, r8, #3520
	cmp	r0, r1
	bne	.L3460
	ldrh	r1, [r4, #-10]
	cmp	r1, r0
	movne	r3, #0
	andeq	r3, r3, #1
	cmp	r3, #0
	beq	.L3460
	ldrh	r3, [r6, #-4]
	cmp	r3, r0
	beq	.L3461
.L3465:
	movw	fp, #65535
	b	.L3460
.L3461:
	sub	r10, r2, #2720
	ldrh	r1, [r7, #-8]
	str	ip, [r2, #-2708]
	ldrh	r3, [r10]
	ldr	r9, .L3583+20
	cmp	r1, r3
	bls	.L3462
	ldrh	r3, [r9]
	cmp	r3, #0
	bne	.L3463
	ldr	r3, .L3583+16
	ldr	r2, [r2, #-3372]
	ldr	r3, [r3, #2448]
	add	r3, r3, r3, asl #1
	cmp	r2, r3, lsr #2
	movcs	r3, #18
	bcs	.L3464
.L3463:
	ldr	r3, .L3583+36
	ldrh	r3, [r3, #-8]
	add	r3, r3, r3, asl #1
	ubfx	r3, r3, #2, #16
.L3464:
	strh	r3, [r10]	@ movhi
	bl	FtlReadRefresh
	mov	r0, #0
	bl	List_get_gc_head_node
	ldr	r3, [r5, #-3544]
	uxth	r0, r0
	mov	r0, r0, asl #1
	ldrh	r3, [r3, r0]
	cmp	r3, #4
	ldrhih	r0, [r9]
	bhi	.L3572
.L3462:
	ldrh	r0, [r9]
	cmp	r0, #0
	bne	.L3465
	ldr	r3, .L3583+36
	ldrh	r9, [r3, #-8]
	add	r3, r9, r9, asl #1
	mov	r3, r3, asr #2
	strh	r3, [r10]	@ movhi
	bl	List_get_gc_head_node
	ldr	r3, [r5, #-3544]
	movw	r2, #2390
	uxth	r0, r0
	mov	r0, r0, asl #1
	ldrh	r1, [r3, r0]
	ldr	r3, .L3583+16
	ldrh	r2, [r3, r2]
	add	r3, r3, #2320
	ldrh	r3, [r3]
	mul	r3, r3, r2
	add	r3, r3, r3, lsr #31
	cmp	r1, r3, asr #1
	ble	.L3466
	ldrh	r3, [r7, #-8]
	sub	r2, r9, #1
	cmp	r3, r2
	blt	.L3466
	bl	FtlReadRefresh
	b	.L3578
.L3466:
	cmp	r1, #0
	bne	.L3465
	movw	r0, #65535
	bl	decrement_vpc_count
	ldrh	r0, [r8, #-8]
	add	r0, r0, #1
	b	.L3572
.L3460:
	ldr	r3, .L3583+16
	ldr	r3, [r3, #2244]
	cmp	r3, #0
	moveq	r9, #1
	movne	r9, #2
.L3459:
	ldrh	r3, [r6, #-4]
	movw	r0, #65535
	ldr	r2, .L3583
	cmp	r3, r0
	sub	r1, r2, #3280
	bne	.L3468
	cmp	fp, r3
	strneh	fp, [r1, #-4]	@ movhi
	bne	.L3470
	ldrh	r3, [r4, #-10]
	sub	r1, r2, #2656
	cmp	r3, fp
	beq	.L3470
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
.L3470:
	ldrh	r0, [r6, #-4]
	mov	r3, #0
	strb	r3, [r5, #-3276]
	movw	r3, #65535
	cmp	r0, r3
	beq	.L3468
	bl	IsBlkInGcList
	cmp	r0, #0
	ldrne	r3, .L3583+12
	mvnne	r2, #0
	strneh	r2, [r3, #-4]	@ movhi
	ldr	r3, .L3583+16
	ldrb	r3, [r3, #144]	@ zero_extendqisi2
	cmp	r3, #0
	beq	.L3474
	ldrh	r0, [r6, #-4]
	bl	ftl_get_blk_mode
	strb	r0, [r5, #-3276]
.L3474:
	ldrh	r2, [r6, #-4]
	movw	r3, #65535
	ldr	r5, .L3583
	cmp	r2, r3
	sub	r7, r5, #3280
	sub	r4, r7, #4
	beq	.L3468
	mov	r0, r4
	bl	make_superblock
	ldr	r2, .L3583+40
	movw	r1, #1986
	mov	r3, #0
	strh	r3, [r7, #-2]	@ movhi
	strh	r3, [r2, r1]	@ movhi
	strb	r3, [r5, #-3278]
	ldrh	r3, [r7, #-4]
	ldr	r1, [r5, #-3544]
	mov	r3, r3, asl #1
	ldrh	r1, [r1, r3]
	movw	r3, #1988
	strh	r1, [r2, r3]	@ movhi
.L3468:
	ldrh	r3, [r6, #-4]
	ldrh	r2, [r8, #-4]
	cmp	r2, r3
	beq	.L3475
	ldr	r2, .L3583+44
	ldrh	r1, [r2, #-4]
	cmp	r1, r3
	beq	.L3475
	ldrh	r2, [r2, #44]
	cmp	r2, r3
	bne	.L3509
.L3475:
	mvn	r3, #0
	strh	r3, [r6, #-4]	@ movhi
.L3509:
	ldr	r6, .L3583+48
	movw	r3, #65535
	ldr	r5, .L3583
	ldrh	r2, [r6]
	mov	r10, r6
	cmp	r2, r3
	bne	.L3477
	mov	r3, #0
	str	r5, [sp, #12]
	str	r3, [r5, #-2708]
.L3478:
	ldr	r8, .L3583+52
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
	beq	.L3572
.L3479:
	mov	r0, r7
	add	r4, r4, #1
	bl	IsBlkInGcList
	cmp	r0, #0
	ldrne	r3, .L3583+52
	strneh	r4, [r3]	@ movhi
	bne	.L3478
	ldr	r3, .L3583+52
	uxth	r4, r4
	mov	r2, r7, asl #1
	strh	r4, [r3]	@ movhi
	ldr	r3, [sp, #12]
	ldr	r1, [r3, #-3544]
	ldr	r3, .L3583+56
	ldrh	r0, [r1, r2]
	ldrh	ip, [r3]
	ldrh	r3, [r3, #-68]
	mul	r3, r3, ip
	add	ip, r3, r3, lsr #31
	cmp	r0, ip, asr #1
	bgt	.L3482
	cmp	r0, #8
	cmphi	r4, #48
	bls	.L3483
	ldrh	r0, [r8, #36]
	cmp	r0, #35
	bhi	.L3483
.L3482:
	ldr	ip, .L3583+52
	mov	r0, #0
	strh	r0, [ip]	@ movhi
.L3483:
	ldrh	r2, [r1, r2]
	movw	r1, #65535
	cmp	r2, r3
	cmpge	fp, r1
	bne	.L3484
	ldr	r3, .L3583+52
	ldrh	r1, [r3]
	cmp	r1, #3
	bhi	.L3484
	sub	r2, r3, #568
	mvn	r1, #0
	strh	r1, [r2, #-4]	@ movhi
	mov	r2, #0
	strh	r2, [r3]	@ movhi
.L3578:
	ldr	r3, .L3583+20
	ldrh	r0, [r3]
	b	.L3572
.L3484:
	cmp	r2, #0
	bne	.L3485
	movw	r0, #65535
	bl	decrement_vpc_count
	ldr	r3, .L3583+52
	ldr	r2, .L3583+52
	ldrh	r3, [r3]
	add	r3, r3, #1
	strh	r3, [r2]	@ movhi
	b	.L3478
.L3485:
	mov	r3, #0
	strb	r3, [r5, #-3276]
	ldr	r3, .L3583+16
	ldrb	r3, [r3, #144]	@ zero_extendqisi2
	cmp	r3, #0
	beq	.L3486
	mov	r0, r7
	bl	ftl_get_blk_mode
	ldr	r3, .L3583
	strb	r0, [r3, #-3276]
.L3486:
	ldr	r0, .L3583+48
	bl	make_superblock
	ldrh	r2, [r6]
	ldr	r1, .L3583+60
	mov	r3, #0
	ldr	r0, [r5, #-3544]
	mov	r2, r2, asl #1
	strh	r3, [r1]	@ movhi
	ldrh	r2, [r0, r2]
	strh	r3, [r6, #2]	@ movhi
	strb	r3, [r5, #-3278]
	strh	r2, [r1, #2]	@ movhi
.L3477:
	ldr	r3, [sp, #16]
	cmp	r3, #1
	bne	.L3487
	bl	FtlReadRefresh
.L3487:
	mov	r3, #1
	str	r3, [r5, #-560]
	ldr	r3, .L3583+56
	ldrh	r3, [r3]
	str	r3, [sp, #12]
	ldr	r3, .L3583+16
	ldrb	r3, [r3, #144]	@ zero_extendqisi2
	cmp	r3, #0
	beq	.L3488
	ldr	r3, .L3583
	ldrb	r3, [r3, #-3276]	@ zero_extendqisi2
	cmp	r3, #1
	ldreq	r3, .L3583+64
	ldreqh	r3, [r3]
	streq	r3, [sp, #12]
.L3488:
	ldrh	r3, [r6, #2]
	mov	r6, #0
	ldr	r1, [sp, #12]
	add	r2, r3, r9
	ldr	r4, .L3583
	cmp	r2, r1
	movgt	r2, r1
	rsbgt	r3, r3, r2
	uxthgt	r9, r3
.L3490:
	uxth	r3, r6
	ldr	r7, .L3583
	cmp	r3, r9
	ldr	r5, .L3583+48
	bcs	.L3497
	ldr	r3, .L3583+24
	add	r0, r5, #14
	ldrh	r1, [r5, #2]
	movw	lr, #65535
	ldr	r7, [r4, #-2688]
	mov	ip, #36
	ldrh	r8, [r3]
	add	r1, r1, r6
	mov	r3, #0
	mov	r5, r3
.L3498:
	uxth	r2, r3
	cmp	r2, r8
	bcs	.L3581
	ldrh	r2, [r0, #2]!
	add	r3, r3, #1
	cmp	r2, lr
	orrne	r2, r1, r2, asl #10
	mlane	r10, ip, r5, r7
	addne	r5, r5, #1
	uxthne	r5, r5
	strne	r2, [r10, #4]
	b	.L3498
.L3581:
	ldr	r0, [r4, #-2688]
	mov	r1, r5
	ldrb	r2, [r4, #-3276]	@ zero_extendqisi2
	mov	r8, #0
	bl	FlashReadPages
.L3493:
	uxth	r3, r8
	cmp	r3, r5
	bcs	.L3582
	mov	r3, #36
	ldr	r2, [r4, #-2688]
	mul	r7, r3, r8
	add	r1, r2, r7
	ldr	r2, [r2, r7]
	ldr	r10, [r1, #12]
	cmn	r2, #1
	beq	.L3529
	ldrh	r1, [r10]
	movw	r2, #61589
	cmp	r1, r2
	bne	.L3529
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
	bne	.L3529
	ldr	r1, .L3583+60
	ldr	r0, .L3583+60
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
	ldr	r3, .L3583+68
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
	ldr	r3, .L3583+16
	ldrb	r3, [r3, #144]	@ zero_extendqisi2
	cmp	r3, #0
	bne	.L3495
	ldrb	r3, [r4, #-3421]	@ zero_extendqisi2
	ldr	r2, [r4, #-2704]
	cmp	r2, r3
	beq	.L3495
	ldr	r3, .L3583+68
	ldrh	r3, [r3, #4]
	cmp	r3, #0
	bne	.L3529
.L3495:
	bl	Ftl_gc_temp_data_write_back
	cmp	r0, #0
	beq	.L3529
	ldr	r3, .L3583
	mov	r2, #0
	mvn	r1, #0
	sub	r3, r3, #3280
	str	r2, [r3, #2720]
	strh	r1, [r3, #-4]	@ movhi
	strh	r2, [r3, #-2]	@ movhi
	b	.L3578
.L3529:
	add	r8, r8, #1
	b	.L3493
.L3582:
	add	r6, r6, #1
	b	.L3490
.L3497:
	ldrh	r3, [r5, #2]
	add	r9, r9, r3
	ldr	r3, [sp, #12]
	uxth	r9, r9
	strh	r9, [r5, #2]	@ movhi
	cmp	r9, r3
	bcc	.L3499
	ldr	r3, [r7, #-2704]
	cmp	r3, #0
	beq	.L3500
	bl	Ftl_gc_temp_data_write_back
	cmp	r0, #0
	movne	r3, #0
	strne	r3, [r7, #-560]
	bne	.L3578
.L3500:
	ldr	r3, .L3583+60
	ldrh	r4, [r3]
	cmp	r4, #0
	bne	.L3501
	ldrh	r3, [r5]
	ldr	r2, [r7, #-3544]
	mov	r3, r3, asl #1
	ldrh	r3, [r2, r3]
	cmp	r3, #0
	beq	.L3501
	ldr	r6, .L3583+16
.L3502:
	ldr	r3, [r6, #2448]
	cmp	r4, r3
	bcs	.L3507
	mov	r0, r4
	add	r1, sp, #36
	mov	r2, #0
	bl	log2phys
	ldr	r0, [sp, #36]
	cmn	r0, #1
	beq	.L3503
	ubfx	r0, r0, #10, #16
	bl	P2V_block_in_plane
	ldrh	r3, [r5]
	cmp	r3, r0
	bne	.L3503
.L3507:
	ldr	r3, .L3583+16
	ldr	r3, [r3, #2448]
	cmp	r4, r3
	bcc	.L3501
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
	b	.L3501
.L3503:
	add	r4, r4, #1
	b	.L3502
.L3501:
	mvn	r3, #0
	strh	r3, [r5]	@ movhi
.L3499:
	ldr	r3, .L3583+72
	ldrh	r3, [r3]
	cmp	r3, #2
	ldrls	r3, .L3583+56
	ldrlsh	r9, [r3]
	bls	.L3509
.L3508:
	ldr	r2, .L3583
	mov	r1, #0
	str	r1, [r2, #-560]
	ldr	r2, .L3583+20
	ldrh	r0, [r2]
	cmp	r0, r1
	addeq	r0, r3, #1
	b	.L3572
.L3513:
	mov	r0, #0
	bx	lr
.L3515:
	mov	r0, ip
	b	.L3572
.L3580:
	ldrh	r9, [r4, #-10]
	cmp	r9, r3
	bne	.L3577
	sub	r3, r8, #3520
	ldr	r2, [r8, #-2716]
	ldrh	r3, [r3, #-8]
	cmp	r3, #24
	movcc	r3, #5120
	movcs	r3, #1024
	cmp	r2, r3
	movls	fp, r9
	bls	.L3436
	ldr	r2, .L3583+20
	mov	r3, #0
	str	r3, [r5, #-2716]
	strh	r3, [r2]	@ movhi
	bl	GetSwlReplaceBlock
	cmp	r0, r9
	mov	fp, r0
	bne	.L3438
	sub	r3, r8, #2704
	ldrh	r1, [r7, #-8]
	ldrh	r2, [r3, #-14]
	mov	r9, r3
	cmp	r1, r2
	movcs	r2, #80
	strcsh	r2, [r3, #-14]	@ movhi
	bcs	.L3448
	mov	r0, #64
	bl	List_get_gc_head_node
	uxth	r3, r0
	cmp	r3, fp
	beq	.L3448
	ldr	r2, [r8, #-564]
	ldr	r10, .L3583+16
	cmp	r2, #0
	bne	.L3441
	movw	r1, #2340
	ldrh	r1, [r10, r1]
	cmp	r1, #3
	beq	.L3441
	ldr	r1, [r8, #-3236]
	cmp	r1, #0
	bne	.L3441
	ldr	r1, [r10, #2244]
	cmp	r1, #0
	bne	.L3441
	ldrb	r0, [r10, #144]	@ zero_extendqisi2
	cmp	r0, #0
	beq	.L3442
.L3441:
	ldr	r2, [r5, #-3544]
	mov	r3, r3, asl #1
	movw	r0, #2340
	ldrh	r0, [r10, r0]
	ldrh	r1, [r2, r3]
	movw	r3, #2390
	ldr	r2, .L3583+24
	cmp	r0, #3
	ldrh	r3, [r10, r3]
	ldrh	r2, [r2]
	mul	r2, r2, r3
	moveq	r3, r3, lsr #1
	movne	r3, #0
	add	r3, r2, r3
	cmp	r1, r3
	bgt	.L3444
	mov	r0, #0
	bl	List_get_gc_head_node
	ldr	r3, [r10, #2448]
	ldr	r2, [r5, #-3372]
	add	r3, r3, r3, asl #1
	cmp	r2, r3, lsr #2
	movls	r3, #160
	uxth	fp, r0
	bls	.L3575
	b	.L3579
.L3442:
	ldr	r2, [r8, #-3544]
	mov	r3, r3, asl #1
	ldrh	r3, [r2, r3]
	cmp	r3, #7
	bhi	.L3447
	bl	List_get_gc_head_node
	uxth	fp, r0
.L3579:
	mov	r3, #128
.L3575:
	strh	r3, [r9, #-14]	@ movhi
	movw	r3, #65535
	cmp	fp, r3
	beq	.L3448
	b	.L3438
.L3444:
	mov	r3, #128
	b	.L3576
.L3447:
	mov	r3, #64
.L3576:
	strh	r3, [r9, #-14]	@ movhi
	b	.L3448
.L3438:
	ldr	r0, [r5, #-3608]
	mov	r1, fp, asl #1
	ldr	r3, [r5, #-3544]
	ldrh	r2, [r7, #-8]
	ldrh	r3, [r3, r1]
	ldrh	r1, [r0, r1]
	ldr	r0, .L3583+76
	str	r1, [sp]
	ldr	r1, .L3583+80
	ldrh	r1, [r1]
	str	r1, [sp, #4]
	mov	r1, fp
	bl	printk
.L3448:
	bl	FtlGcReFreshBadBlk
	b	.L3436
.L3577:
	mov	fp, r3
	b	.L3436
.L3572:
	add	sp, sp, #44
	@ sp needed
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L3584:
	.align	2
.L3583:
	.word	.LANCHOR2
	.word	.LANCHOR1
	.word	.LANCHOR2-2656
	.word	.LANCHOR2-3280
	.word	.LANCHOR0
	.word	.LANCHOR4+1984
	.word	.LANCHOR0+2320
	.word	.LC157
	.word	.LANCHOR2-3520
	.word	.LANCHOR2-3296
	.word	.LANCHOR4
	.word	.LANCHOR2-3472
	.word	.LANCHOR2-3284
	.word	.LANCHOR2-2712
	.word	.LANCHOR0+2388
	.word	.LANCHOR4+1986
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
	ldr	r3, .L3629
	ldr	r7, .L3629+4
	ldr	r5, [r3, #1992]
	ldr	r3, [r7, #-3616]
	cmp	r3, #0
	bne	.L3587
	ldr	r4, .L3629+8
	ldr	r1, [r4, #2440]
	cmp	r1, #0
	beq	.L3587
	ldrb	r6, [r4, #144]	@ zero_extendqisi2
	mov	r8, #0
	ldr	r0, [r4, #2444]
	mov	r10, #36
	cmp	r6, #0
	ldrb	r3, [r5, #9]	@ zero_extendqisi2
	ldr	r9, .L3629+8
	ldrneb	r6, [r5, #8]	@ zero_extendqisi2
	subne	r6, r6, #1
	clzne	r6, r6
	movne	r6, r6, lsr #5
	mov	r2, r6
	bl	FlashProgPages
.L3590:
	ldr	r3, [r4, #2440]
	cmp	r8, r3
	bcs	.L3608
	mul	fp, r10, r8
	ldr	r3, [r9, #2444]
	add	r2, r3, fp
	ldr	r3, [r3, fp]
	cmn	r3, #1
	beq	.L3611
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
	beq	.L3594
	ubfx	r0, r0, #10, #16
	bl	P2V_block_in_plane
	ldr	r2, [r7, #-3544]
	mov	r3, r0, asl #1
	mov	fp, r0
	ldrh	r2, [r2, r3]
	cmp	r2, #0
	bne	.L3595
	ldr	r0, .L3629+12
	mov	r1, fp
	bl	printk
.L3595:
	mov	r0, fp
	bl	decrement_vpc_count
.L3594:
	add	r8, r8, #1
	b	.L3590
.L3627:
	ldr	r6, .L3629+16
	movw	r5, #16386
.L3607:
	ldrh	r3, [r6]
	cmp	r3, #0
	beq	.L3608
	mov	r0, #1
	mov	r1, r0
	bl	ftl_do_gc
	subs	r5, r5, #1
	bne	.L3607
.L3608:
	mov	r3, #0
	str	r3, [r4, #2440]
	b	.L3587
.L3611:
	ldr	fp, .L3629+8
	mov	r9, #0
.L3591:
	ldr	r3, [r4, #2440]
	cmp	r9, r3
	bcs	.L3627
	mov	r8, #36
	ldr	r3, [fp, #2444]
	mul	r8, r8, r9
	mov	r10, #0
	mvn	r2, #0
	str	r2, [r3, r8]
.L3597:
	ldr	r3, [r4, #2444]
	add	r2, r3, r8
	ldr	r3, [r3, r8]
	cmn	r3, #1
	bne	.L3628
	ldr	r0, [r2, #4]
	ubfx	r0, r0, #10, #16
	bl	P2V_block_in_plane
	ldrh	r3, [r5]
	cmp	r3, r0
	bne	.L3598
	ldr	r1, [r7, #-3544]
	mov	r3, r3, asl #1
	ldrh	r0, [r5, #4]
	ldrh	r2, [r1, r3]
	rsb	r2, r0, r2
	strh	r2, [r1, r3]	@ movhi
	ldr	r3, .L3629+20
	strb	r10, [r5, #6]
	strh	r10, [r5, #4]	@ movhi
	ldrh	r3, [r3]
	strh	r3, [r5, #2]	@ movhi
.L3598:
	ldrh	r3, [r5, #4]
	cmp	r3, #0
	bne	.L3599
	mov	r0, r5
	bl	allocate_new_data_superblock
.L3599:
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
	ldreq	r2, .L3629+4
	moveq	r3, #1
	streq	r3, [r2, #-3616]
	ldr	r3, [r7, #-3616]
	cmp	r3, #0
	beq	.L3597
	b	.L3587
.L3628:
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
	beq	.L3604
	ubfx	r0, r0, #10, #16
	bl	P2V_block_in_plane
	ldr	r2, [r7, #-3544]
	mov	r3, r0, asl #1
	mov	r8, r0
	ldrh	r2, [r2, r3]
	cmp	r2, #0
	bne	.L3605
	ldr	r0, .L3629+12
	mov	r1, r8
	bl	printk
.L3605:
	mov	r0, r8
	bl	decrement_vpc_count
.L3604:
	add	r9, r9, #1
	b	.L3591
.L3587:
	mov	r0, #0
	add	sp, sp, #12
	@ sp needed
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L3630:
	.align	2
.L3629:
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
	ldr	r3, .L3635
	ldr	r3, [r3, #-3616]
	cmp	r3, #0
	bne	.L3634
	ldr	r3, .L3635+4
	stmfd	sp!, {r4, lr}
	.save {r4, lr}
	ldr	r4, [r3, #504]
	cmp	r4, #1
	bne	.L3632
	bl	FtlCacheWriteBack
	bl	l2p_flush
	mov	r0, r4
	bl	FtlEctTblFlush
	bl	FtlVpcTblFlush
.L3632:
	mov	r0, #0
	ldmfd	sp!, {r4, pc}
.L3634:
	mov	r0, #0
	bx	lr
.L3636:
	.align	2
.L3635:
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
	ldr	r3, .L3640
	ldr	r3, [r3, #504]
	cmp	r3, #1
	bne	.L3638
	bl	FtlSysFlush
.L3638:
	mov	r0, #0
	ldmfd	sp!, {r3, pc}
.L3641:
	.align	2
.L3640:
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
	ldr	r0, .L3646
	bl	printk
	ldmfd	sp!, {r3, lr}
	b	ftl_deinit
.L3647:
	.align	2
.L3646:
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
	ldr	r8, .L3669
	mov	r4, r1
	ldr	r3, [r8, #2428]
	cmp	r1, r3
	cmpls	r0, r3
	movcs	r5, #1
	movcc	r5, #0
	bcs	.L3658
	add	r2, r0, r1
	cmp	r2, r3
	bhi	.L3658
	cmp	r1, #31
	bls	.L3656
	ldr	r7, .L3669+4
	ldr	r3, [r7, #-3616]
	cmp	r3, #0
	movne	r0, r5
	bne	.L3651
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
	beq	.L3652
	rsb	r5, r6, r5
	add	r8, r0, #1
	cmp	r5, r4
	movcs	r5, r4
	uxth	r5, r5
	rsb	r4, r5, r4
.L3652:
	ldr	r5, .L3669+8
	mvn	r3, #0
	ldr	r9, .L3669+12
	str	r3, [sp, #4]
	mov	r6, r5
.L3653:
	ldrh	r3, [r5]
	cmp	r4, r3
	bcc	.L3668
	mov	r0, r8
	mov	r1, sp
	mov	r2, #0
	bl	log2phys
	ldr	r3, [sp]
	cmn	r3, #1
	beq	.L3654
	ldr	r3, [r9, #1996]
	add	r1, sp, #4
	mov	r2, #1
	mov	r0, r8
	add	r3, r3, #1
	str	r3, [r9, #1996]
	ldr	r3, [r7, #-3360]
	add	r3, r3, #1
	str	r3, [r7, #-3360]
	bl	log2phys
	ldr	r0, [sp]
	ubfx	r0, r0, #10, #16
	bl	P2V_block_in_plane
	bl	decrement_vpc_count
.L3654:
	ldrh	r3, [r6]
	add	r8, r8, #1
	rsb	r4, r3, r4
	b	.L3653
.L3668:
	ldr	r3, .L3669+12
	ldr	r2, [r3, #1996]
	cmp	r2, #32
	bls	.L3656
	mov	r2, #0
	str	r2, [r3, #1996]
	bl	l2p_flush
	bl	FtlVpcTblFlush
.L3656:
	mov	r0, #0
	b	.L3651
.L3658:
	mvn	r0, #0
.L3651:
	add	sp, sp, #12
	@ sp needed
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, pc}
.L3670:
	.align	2
.L3669:
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
	ldr	r1, .L3720
	.pad #84
	sub	sp, sp, #84
	ldr	r1, [r1, #504]
	cmp	r1, #1
	bne	.L3697
	cmp	r0, #16
	mov	r8, r3
	mov	r9, r2
	bne	.L3674
	mov	r1, r2
	add	r0, r5, #256
	mov	r2, r3
	bl	FtlVendorPartRead
	b	.L3673
.L3674:
	ldr	r2, .L3720+4
	ldr	r3, [r2, #2428]
	cmp	r9, r3
	cmpls	r5, r3
	bcs	.L3697
	add	r1, r5, r9
	str	r1, [sp, #40]
	cmp	r1, r3
	bhi	.L3697
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
	ldr	r3, .L3720+8
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
	beq	.L3675
	bl	FtlCacheWriteBack
.L3675:
	ldr	r6, [sp, #32]
	mov	r10, #0
	ldr	r4, .L3720+8
	mov	r7, r10
	str	r10, [sp, #48]
	str	r10, [sp, #52]
.L3676:
	ldr	r3, [sp, #28]
	cmp	r3, #0
	beq	.L3719
	mov	r0, r6
	add	r1, sp, #76
	mov	r2, #0
	bl	log2phys
	ldr	r3, [sp, #76]
	cmn	r3, #1
	bne	.L3715
	mov	fp, #0
.L3677:
	ldr	r3, .L3720+12
	ldrh	r0, [r3]
	cmp	fp, r0
	bcs	.L3681
	mla	r0, r0, r6, fp
	ldr	r2, [sp, #40]
	cmp	r0, r5
	movcs	r3, #1
	movcc	r3, #0
	cmp	r0, r2
	movcs	r3, #0
	cmp	r3, #0
	beq	.L3679
	rsb	r0, r5, r0
	mov	r1, #0
	mov	r2, #512
	add	r0, r8, r0, asl #9
	bl	ftl_memset
.L3679:
	add	fp, fp, #1
	b	.L3677
.L3715:
	ldr	r2, [r4, #-536]
	mov	fp, #36
	mla	fp, fp, r7, r2
	str	r3, [fp, #4]
	ldr	r3, [sp, #32]
	cmp	r6, r3
	bne	.L3682
	ldr	r3, [r4, #-508]
	mov	r0, r5
	str	r3, [fp, #8]
	ldr	r3, .L3720+12
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
	b	.L3683
.L3682:
	ldr	r3, [sp, #36]
	cmp	r6, r3
	bne	.L3684
	ldr	r3, [r4, #-504]
	ldr	r1, [sp, #40]
	str	r3, [fp, #8]
	ldr	r3, .L3720+12
	ldrh	r2, [r3]
	mul	r3, r2, r6
	rsb	r10, r3, r1
	cmp	r10, r2
	bne	.L3683
	b	.L3717
.L3684:
	ldr	r3, .L3720+12
	ldrh	r3, [r3]
	mul	r3, r3, r6
.L3717:
	rsb	r3, r5, r3
	add	r3, r8, r3, asl #9
	str	r3, [fp, #8]
.L3683:
	ldr	r3, .L3720+16
	ldr	r2, [r4, #-496]
	str	r6, [fp, #16]
	ldrh	r3, [r3]
	mul	r3, r3, r7
	add	r7, r7, #1
	bic	r3, r3, #3
	add	r3, r2, r3
	str	r3, [fp, #12]
.L3681:
	ldr	r3, [sp, #28]
	add	r6, r6, #1
	subs	r3, r3, #1
	str	r3, [sp, #28]
	beq	.L3685
	ldr	r3, .L3720+20
	ldrh	r3, [r3]
	cmp	r7, r3, asl #3
	bne	.L3676
.L3685:
	cmp	r7, #0
	beq	.L3676
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
.L3692:
	ldr	r3, [sp, #44]
	mov	ip, #36
	ldr	r1, [sp, #32]
	mul	fp, ip, r3
	ldr	r3, [r4, #-536]
	add	r3, r3, fp
	ldr	r2, [r3, #16]
	cmp	r2, r1
	bne	.L3687
	ldr	r1, [r3, #8]
	ldr	r3, [r4, #-508]
	cmp	r1, r3
	bne	.L3688
	ldr	r3, [sp, #60]
	mov	r0, r8
	ldr	r2, [sp, #64]
	add	r1, r1, r3
	b	.L3718
.L3687:
	ldr	r1, [sp, #36]
	cmp	r2, r1
	bne	.L3688
	ldr	r1, [r3, #8]
	ldr	r3, [r4, #-504]
	cmp	r1, r3
	bne	.L3688
	ldr	r3, .L3720+12
	ldr	r2, [sp, #68]
	ldrh	r0, [r3]
	ldr	r3, [sp, #36]
	mul	r0, r0, r3
	rsb	r0, r5, r0
	add	r0, r8, r0, asl #9
.L3718:
	bl	ftl_memcpy
.L3688:
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
	beq	.L3690
	ldr	r2, [r4, #-3160]
	ldr	r0, .L3720+24
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
.L3690:
	ldr	r3, [r4, #-536]
	add	r2, r3, fp
	ldr	r3, [r3, fp]
	cmp	r3, #256
	bne	.L3691
	ldr	r0, [r2, #4]
	ubfx	r0, r0, #10, #16
	bl	P2V_block_in_plane
	bl	FtlGcRefreshBlock
.L3691:
	ldr	r3, [sp, #44]
	add	r3, r3, #1
	str	r3, [sp, #44]
	cmp	r3, r7
	bne	.L3692
	mov	r7, #0
	b	.L3676
.L3719:
	ldr	r3, .L3720+28
	ldrh	r3, [r3, #-2]
	cmp	r3, #0
	beq	.L3694
	ldr	r0, [sp, #28]
	mov	r1, #1
	bl	ftl_do_gc
.L3694:
	ldr	r0, [sp, #52]
	b	.L3673
.L3697:
	mvn	r0, #0
.L3673:
	add	sp, sp, #84
	@ sp needed
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L3721:
	.align	2
.L3720:
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
	ldr	r3, .L3728
	ldr	r3, [r3, #504]
	cmp	r3, #1
	mvnne	r4, #0
	bne	.L3725
	mov	r2, r4
	mov	r0, r6
	mov	r1, r5
	bl	ftl_vendor_read
	mov	r4, r0
.L3725:
	bl	rknand_device_unlock
	mov	r0, r4
	ldmfd	sp!, {r4, r5, r6, pc}
.L3729:
	.align	2
.L3728:
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
	ldr	r2, .L3751
	ldr	r7, .L3751+4
	ldr	r5, .L3751+8
	ldr	r6, .L3751+12
	ldr	r1, .L3751+16
	ldr	r0, .L3751+20
	str	r3, [r7, #504]
	mov	r3, #0
	str	r3, [r2, #2000]
	str	r3, [r5, #-3616]
	bl	printk
	add	r0, r6, #116
	bl	FtlConstantsInit
	bl	FtlMemInit
	bl	FtlVariablesInit
	ldr	r3, [r6, #2324]
	uxth	r0, r3
	bl	FtlFreeSysBlkQueueInit
	bl	FtlLoadBbt
	cmp	r0, #0
	ldrne	r0, .L3751+24
	bne	.L3750
	bl	FtlSysBlkInit
	subs	r4, r0, #0
	beq	.L3737
	ldr	r0, .L3751+28
.L3750:
	ldr	r1, .L3751+32
	bl	printk
	b	.L3736
.L3737:
	sub	r5, r5, #3520
	mov	r1, #1
	str	r1, [r7, #504]
	bl	ftl_do_gc
	ldrh	r5, [r5, #-8]
	cmp	r5, #15
	bhi	.L3738
	ldr	r6, .L3751+36
	ldr	r7, .L3751+40
	sub	r8, r6, #244
.L3741:
	ldrh	r3, [r6]
	movw	r2, #65535
	cmp	r3, r2
	bne	.L3739
	ldrh	r2, [r7]
	cmp	r2, r3
	bne	.L3739
	and	r0, r4, #63
	bl	List_get_gc_head_node
	uxth	r0, r0
	bl	FtlGcRefreshBlock
.L3739:
	mov	r0, #1
	mov	r1, r0
	bl	ftl_do_gc
	mov	r0, #0
	mov	r1, #1
	bl	ftl_do_gc
	ldrh	r2, [r8]
	add	r3, r5, #2
	cmp	r2, r3
	bhi	.L3736
	add	r4, r4, #1
	cmp	r4, #4096
	bne	.L3741
	b	.L3736
.L3738:
	ldrb	r3, [r6, #144]	@ zero_extendqisi2
	cmp	r3, #0
	beq	.L3736
	mov	r4, #128
.L3743:
	mov	r0, #1
	mov	r1, r0
	bl	ftl_do_gc
	subs	r4, r4, #1
	bne	.L3743
.L3736:
	mov	r0, #0
	ldmfd	sp!, {r4, r5, r6, r7, r8, pc}
.L3752:
	.align	2
.L3751:
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
	bl	ftl_malloc
	ldr	r6, .L3758
	ldr	r4, .L3758+4
	mov	r5, #0
	ldr	r1, .L3758+8
	str	r5, [r6, #2008]
	str	r5, [r4, #1684]
	str	r0, [r6, #2004]
	ldr	r0, .L3758+12
	bl	rknand_get_reg_addr
	ldr	r3, [r4, #1684]
	cmp	r3, r5
	beq	.L3756
	bl	rk_nandc_irq_init
	mov	r1, r5
	mov	r2, r5
	mov	r3, #2048
	ldr	r0, [r6, #2004]
	bl	FlashSramLoadStore
	bl	rknand_flash_cs_init
	ldr	r0, [r4, #1684]
	bl	FlashInit
	subs	r4, r0, #0
	bne	.L3755
	bl	FtlInit
.L3755:
	mov	r1, r4
	ldr	r0, .L3758+16
	bl	printk
	mov	r0, r4
	ldmfd	sp!, {r4, r5, r6, pc}
.L3756:
	mvn	r0, #0
	ldmfd	sp!, {r4, r5, r6, pc}
.L3759:
	.align	2
.L3758:
	.word	.LANCHOR4
	.word	.LANCHOR2
	.word	.LANCHOR4+2008
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
	ldr	r3, .L3777
	ldrb	r3, [r3, #144]	@ zero_extendqisi2
	cmp	r3, #0
	bxeq	lr
	stmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.save {r4, r5, r6, r7, r8, r9, r10, fp, lr}
	movw	r3, #1848
	ldr	r8, .L3777+4
	.pad #52
	sub	sp, sp, #52
	ldr	r4, .L3777+8
	movw	r5, #4097
	ldr	r0, .L3777+12
	ldrh	r7, [r8, r3]
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
	ldr	r0, .L3777+16
	bl	allocate_new_data_superblock
	ldr	r0, .L3777+20
	bl	allocate_new_data_superblock
.L3762:
	subs	r5, r5, #1
	beq	.L3766
	mov	r0, #1
	mov	r1, r0
	bl	ftl_do_gc
	ldr	r3, [r4, #-3544]
	ldrh	r3, [r3, r6]
	cmp	r3, #0
	bne	.L3762
.L3766:
	ldr	r3, [r4, #-3544]
	mov	r1, r7
	ldr	r0, .L3777+12
	ldrh	r2, [r3, r6]
	bl	printk
	ldr	r3, [r4, #-3544]
	ldrh	r5, [r3, r6]
	cmp	r5, #0
	bne	.L3764
	add	r0, sp, #48
	movw	r10, #65535
	mov	fp, #36
	strh	r7, [r0, #-48]!	@ movhi
	bl	make_superblock
	ldr	r3, .L3777+24
	ldrh	lr, [r3]
	ldr	r3, .L3777+8
	ldr	r9, [r3, #-3612]
	mov	r3, r5
	mov	ip, r3
	add	r0, sp, #14
.L3767:
	uxth	r2, r3
	cmp	r2, lr
	bcs	.L3776
	ldrh	r2, [r0, #2]!
	add	r3, r3, #1
	cmp	r2, r10
	movne	r2, r2, asl #10
	mlane	r1, fp, r5, r9
	addne	r5, r5, #1
	uxthne	r5, r5
	stmneib	r1, {r2, ip}
	strne	ip, [r1, #12]
	b	.L3767
.L3776:
	ldr	r3, [r4, #-3544]
	mov	r1, r7
	ldr	r0, .L3777+28
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
.L3764:
	movw	r3, #1848
	mvn	r2, #0
	strh	r2, [r8, r3]	@ movhi
	add	sp, sp, #52
	@ sp needed
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L3778:
	.align	2
.L3777:
	.word	.LANCHOR0
	.word	.LANCHOR4
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
	ldr	r10, .L3854
	str	r3, [sp, #4]
	ldr	r3, [r10, #-3616]
	cmp	r3, #0
	bne	.L3821
	mov	r8, r2
	ldr	r2, .L3854+4
	ldr	r2, [r2, #504]
	cmp	r2, #1
	movne	r0, r3
	bne	.L3781
	cmp	r0, #16
	mov	r7, r1
	bne	.L3782
	add	r0, r1, #256
	ldr	r2, [sp, #4]
	mov	r1, r8
	bl	FtlVendorPartWrite
	b	.L3781
.L3782:
	ldr	fp, .L3854+8
	ldr	r3, [fp, #2428]
	cmp	r8, r3
	cmpls	r1, r3
	bcs	.L3824
	add	r5, r1, r8
	cmp	r5, r3
	bhi	.L3824
	ldr	r6, .L3854+12
	mov	r3, #2048
	mov	r0, r1
	str	r3, [r6, #2012]
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
	beq	.L3784
	mov	r3, #36
	ldr	r9, [fp, #2444]
	mul	r3, r3, r2
	ldr	r2, [sp, #8]
	sub	r3, r3, #36
	add	r9, r9, r3
	ldr	r3, [r9, #16]
	cmp	r2, r3
	bne	.L3785
	ldr	r3, [r10, #-3352]
	mov	r1, r4
	mov	r0, r7
	add	r3, r3, #1
	str	r3, [r10, #-3352]
	ldr	r3, [r6, #2016]
	add	r3, r3, #1
	str	r3, [r6, #2016]
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
	bne	.L3786
	ldr	r3, [r6, #2016]
	cmp	r3, #2
	ble	.L3821
.L3786:
	ldr	r3, [sp, #4]
	rsb	r8, r4, r8
	add	r7, r7, r4
	str	r5, [sp]
	add	r3, r3, r10
	str	r3, [sp, #4]
	ldr	r3, [sp, #8]
	add	r3, r3, #1
	str	r3, [sp, #8]
.L3785:
	mov	r3, #0
	str	r3, [r6, #2016]
.L3784:
	ldr	r0, [sp, #8]
	ldr	r1, [sp, #28]
	bl	FtlCacheMetchLpa
	cmp	r0, #0
	beq	.L3787
	bl	FtlCacheWriteBack
.L3787:
	ldr	r5, .L3854+16
	mov	r3, #0
	ldr	r4, .L3854+8
	str	r3, [sp, #12]
	str	r5, [r6, #1992]
	ldr	r6, [sp, #8]
	mov	r10, r4
	str	r3, [sp, #32]
.L3788:
	ldr	r3, [sp]
	cmp	r3, #0
	beq	.L3853
	ldrh	r2, [r5, #4]
	cmp	r2, #0
	bne	.L3789
	ldr	r3, .L3854+16
	ldr	r9, .L3854+4
	cmp	r5, r3
	bne	.L3790
	add	r0, r3, #48
	ldrh	r5, [r0, #4]
	cmp	r5, #0
	bne	.L3791
	bl	allocate_new_data_superblock
	str	r5, [r9, #3452]
.L3791:
	ldr	r0, .L3854+16
	bl	allocate_new_data_superblock
	ldr	r3, [r9, #3452]
	cmp	r3, #0
	ldrne	r5, .L3854+20
	bne	.L3792
.L3793:
	ldr	r5, .L3854+16
	b	.L3792
.L3790:
	ldrh	r3, [r3, #4]
	str	r2, [r9, #3452]
	cmp	r3, #0
	bne	.L3793
	mov	r0, r5
	bl	allocate_new_data_superblock
.L3792:
	ldrh	r3, [r5, #4]
	cmp	r3, #0
	bne	.L3794
	mov	r0, r5
	bl	allocate_new_data_superblock
.L3794:
	ldr	r3, .L3854+12
	str	r5, [r3, #1992]
.L3789:
	ldr	r2, .L3854
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
.L3851:
	str	r3, [sp, #16]
	ldr	r3, [sp, #16]
	ldr	r2, [sp, #44]
	cmp	r3, r2
	beq	.L3796
	ldrh	r3, [r5, #4]
	cmp	r3, #0
	beq	.L3796
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
	beq	.L3797
	ldr	r3, .L3854+24
	ldrh	r2, [r3]
	add	r3, r8, r7
	mls	r3, r2, r6, r3
	cmp	r3, r2
	bne	.L3796
.L3797:
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
	ldr	fp, .L3854+28
	mla	r1, r3, r2, r1
	ldrh	r2, [fp]
	str	r6, [r1, #16]
	str	r3, [sp, #48]
	ldr	r3, [r4, #2440]
	str	r0, [r1, #4]
	ldr	r0, .L3854
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
	beq	.L3798
	ldr	r3, [sp, #24]
	cmp	r3, #0
	beq	.L3799
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
	b	.L3800
.L3799:
	cmp	ip, #0
	beq	.L3800
	ldr	r2, .L3854+24
	add	r3, r8, r7
	ldrh	r2, [r2]
	smulbb	r2, r2, r6
	rsb	r3, r2, r3
	uxth	r3, r3
	str	r3, [sp, #12]
	ldr	r3, [sp, #24]
	str	r3, [sp, #32]
.L3800:
	ldr	r3, .L3854+24
	ldr	r2, [sp, #12]
	ldrh	r3, [r3]
	cmp	r2, r3
	bne	.L3801
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
	bne	.L3804
	ldr	r0, [r3, #8]
	ldr	r3, .L3854+32
	ldrh	r2, [r3]
	b	.L3849
.L3801:
	ldr	r2, [sp, #56]
	mov	r3, #36
	cmn	r2, #1
	beq	.L3805
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
	ldr	r3, .L3854
	ldreq	r2, [r3, #-3160]
	addeq	r2, r2, #1
	streq	r2, [r3, #-3160]
	beq	.L3808
	ldr	r2, [r9, #8]
	cmp	r2, r6
	beq	.L3808
	ldr	r2, [r3, #-3160]
	ldr	r0, .L3854+36
	add	r2, r2, #1
	str	r2, [r3, #-3160]
	mov	r2, r6
	ldr	r1, [r9, #8]
	bl	printk
	b	.L3808
.L3805:
	ldr	r2, [r4, #2440]
	ldr	r1, [r4, #2444]
	mla	r3, r3, r2, r1
	mov	r1, #0
	ldr	r0, [r3, #8]
	ldr	r3, .L3854+32
	ldrh	r2, [r3]
	bl	ftl_memset
.L3808:
	ldr	r3, [sp, #24]
	cmp	r3, #0
	mov	r3, #36
	beq	.L3809
	ldr	r1, [r4, #2444]
	ldr	r2, [r4, #2440]
	mla	r3, r3, r2, r1
	ldr	r1, [sp, #4]
	ldr	r0, [r3, #8]
	ldr	r3, [sp, #32]
	add	r0, r0, r3, asl #9
	b	.L3852
.L3809:
	ldr	r1, [r4, #2440]
	ldr	r2, [r4, #2444]
	mla	r3, r3, r1, r2
	ldr	r2, .L3854+24
	ldrh	r1, [r2]
	ldr	r0, [r3, #8]
	mul	r1, r1, r6
	ldr	r3, [sp, #4]
	rsb	r1, r7, r1
	add	r1, r3, r1, asl #9
.L3852:
	ldr	r3, [sp, #12]
	mov	r2, r3, asl #9
	b	.L3849
.L3798:
	ldr	r2, [sp, #20]
	cmp	r2, #0
	ldr	r2, [r4, #2440]
	beq	.L3810
	ldr	r1, [r4, #2444]
	mla	r3, r3, r2, r1
	ldr	r2, .L3854+24
	ldrh	fp, [r2]
	ldr	r2, [sp, #4]
	mul	fp, fp, r6
	rsb	fp, r7, fp
	add	fp, r2, fp, asl #9
	str	fp, [r3, #8]
	b	.L3804
.L3810:
	ldr	r0, [r4, #2444]
	mla	r3, r3, r2, r0
	ldr	r2, .L3854+24
	ldrh	r1, [r2]
	ldrh	r2, [fp, #-2]
	ldr	r0, [r3, #8]
	mul	r1, r1, r6
	ldr	r3, [sp, #4]
	rsb	r1, r7, r1
	add	r1, r3, r1, asl #9
.L3849:
	bl	ftl_memcpy
.L3804:
	ldr	r3, .L3854+40
	ldr	r2, [sp, #40]
	ldr	r1, [sp, #36]
	strh	r3, [r2, r1]	@ movhi
	ldr	r2, .L3854
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
	b	.L3851
.L3796:
	ldr	r3, [sp]
	ldr	r2, [sp, #16]
	ldr	r1, [sp, #20]
	rsb	r3, r2, r3
	ldr	r2, .L3854
	str	r3, [sp]
	ldr	r3, [r4, #2440]
	ldr	r2, [r2, #-540]
	cmp	r3, r2
	orrcs	r1, r1, #1
	cmp	r1, #0
	bne	.L3814
	ldrh	r3, [r5, #4]
	cmp	r3, #0
	beq	.L3814
.L3816:
	mov	r3, #0
	str	r3, [sp, #20]
	b	.L3788
.L3814:
	bl	FtlCacheWriteBack
	mov	r3, #0
	str	r3, [r10, #2440]
	ldr	r3, [sp]
	cmp	r3, #1
	bhi	.L3788
	b	.L3816
.L3853:
	mov	r0, r3
	ldr	r2, [sp, #8]
	ldr	r3, [sp, #28]
	rsb	r1, r2, r3
	bl	ftl_do_gc
	ldr	r3, .L3854+44
	ldrh	r3, [r3, #-8]
	cmp	r3, #5
	bls	.L3827
	cmp	r3, #31
	bhi	.L3821
	ldr	r3, .L3854+8
	ldrb	r3, [r3]	@ zero_extendqisi2
	cmp	r3, #0
	bne	.L3821
.L3827:
	ldr	r4, [sp]
	ldr	r5, .L3854+48
	ldr	r6, .L3854
	ldr	r7, .L3854+52
.L3840:
	ldrh	r2, [r5]
	movw	r3, #65535
	cmp	r2, r3
	bne	.L3820
	ldrh	r3, [r7]
	cmp	r3, r2
	bne	.L3820
	ldr	r2, .L3854+56
	ldrh	r2, [r2]
	cmp	r2, r3
	bne	.L3820
	and	r0, r4, #7
	bl	List_get_gc_head_node
	uxth	r0, r0
	bl	FtlGcRefreshBlock
.L3820:
	ldr	r3, .L3854+60
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
	bne	.L3821
	ldr	r3, .L3854+64
	ldrh	r3, [r3]
	cmp	r3, #2
	bhi	.L3821
	add	r4, r4, #1
	cmp	r4, #256
	bne	.L3840
	b	.L3821
.L3824:
	mvn	r0, #0
	b	.L3781
.L3821:
	mov	r0, #0
.L3781:
	add	sp, sp, #100
	@ sp needed
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L3855:
	.align	2
.L3854:
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
	ldr	r3, .L3862
	ldr	r3, [r3, #504]
	cmp	r3, #1
	mvnne	r4, #0
	bne	.L3859
	mov	r2, r4
	mov	r0, r6
	mov	r1, r5
	bl	ftl_vendor_write
	mov	r4, r0
.L3859:
	bl	rknand_device_unlock
	mov	r0, r4
	ldmfd	sp!, {r4, r5, r6, pc}
.L3863:
	.align	2
.L3862:
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
	ldr	r4, .L3876
	.pad #28
	sub	sp, sp, #28
	ldr	r7, .L3876+4
	mov	r6, r0
	mov	r5, #0
	ldr	r3, [r4, #-524]
	mov	r9, r4
	str	r3, [r4, #1760]
	ldr	r3, [r4, #-500]
	str	r3, [r4, #1764]
.L3868:
	ldrh	r2, [r7]
	sxth	r3, r5
	cmp	r3, r2
	bge	.L3875
	mov	r1, #1
	ldr	r0, .L3876+8
	mov	r2, r1
	orr	r3, r3, r8
	str	r3, [r4, #1756]
	bl	FlashReadPages
	ldr	r3, [r4, #1764]
	mov	r1, r6
	ldr	r0, .L3876+12
	ldr	r2, [r3]
	str	r2, [sp]
	ldr	r2, [r3, #4]
	str	r2, [sp, #4]
	ldr	r2, [r3, #8]
	str	r2, [sp, #8]
	ldr	r3, [r3, #12]
	ldr	r2, [r4, #1752]
	str	r3, [sp, #12]
	ldr	r3, [r4, #1760]
	ldr	r3, [r3]
	str	r3, [sp, #16]
	ldr	r3, [r4, #1756]
	bl	printk
	ldr	r3, [r4, #1764]
	ldr	r3, [r3]
	cmn	r3, #1
	beq	.L3869
	ldr	r0, .L3876+16
	mov	r2, #4
	ldr	r1, [r9, #-524]
	mov	r3, #768
	bl	rknand_print_hex
.L3869:
	add	r5, r5, #1
	b	.L3868
.L3875:
	add	sp, sp, #28
	@ sp needed
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, pc}
.L3877:
	.align	2
.L3876:
	.word	.LANCHOR2
	.word	.LANCHOR0+2390
	.word	.LANCHOR2+1752
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
	ldr	r6, .L3896
	.pad #52
	sub	sp, sp, #52
	ldr	r10, .L3896+4
	ldrh	r7, [r6, r3]
	add	fp, r10, #18
.L3879:
	ldrh	r3, [r10]
	ldr	r4, .L3896+8
	cmp	r3, r7
	bls	.L3892
	ldr	r1, .L3896+12
	mov	r8, #0
	ldr	r2, [r4, #-536]
	mov	r5, r8
	ldr	r3, [r4, #-2696]
	ldrh	ip, [r1]
	ldr	r9, [r4, #-2692]
	ldrh	r1, [r1, #80]
	str	r1, [sp, #28]
.L3888:
	uxth	r1, r8
	cmp	r1, ip
	bcs	.L3894
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
	bne	.L3880
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
.L3880:
	add	r8, r8, #1
	b	.L3888
.L3894:
	cmp	r5, #0
	beq	.L3883
	ldr	r0, [r4, #-536]
	mov	r1, r5
	mov	r2, #1
	mov	r8, #0
	bl	FlashReadPages
	mov	r9, #36
.L3884:
	uxth	r3, r8
	cmp	r3, r5
	bcs	.L3883
	ldr	r3, [r4, #-536]
	ldr	r0, .L3896+16
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
	b	.L3884
.L3883:
	add	r7, r7, #1
	uxth	r7, r7
	b	.L3879
.L3892:
	ldr	r9, .L3896+20
	mov	r8, #0
.L3887:
	ldr	r7, .L3896+8
	sxth	r5, r8
	sub	r3, r7, #388
	ldrh	r3, [r3]
	cmp	r5, r3
	bge	.L3890
	mov	r5, r5, asl #1
	mov	r7, #0
.L3891:
	ldrh	r2, [r9]
	sxth	r3, r7
	add	r7, r7, #1
	cmp	r3, r2
	bge	.L3895
	ldr	r2, [r4, #-472]
	mov	r1, #1
	ldr	r0, .L3896+24
	ldrh	r2, [r2, r5]
	orr	r3, r3, r2, asl #10
	mov	r2, r1
	str	r3, [r4, #1756]
	bl	FlashReadPages
	ldr	r3, [r4, #1764]
	ldr	r1, [r4, #-472]
	ldr	r2, [r4, #1760]
	ldr	r0, [r3]
	ldrh	r1, [r1, r5]
	str	r0, [sp]
	ldr	r0, [r3, #4]
	str	r0, [sp, #4]
	ldr	r0, [r3, #8]
	str	r0, [sp, #8]
	ldr	r3, [r3, #12]
	ldr	r0, .L3896+28
	str	r3, [sp, #12]
	ldr	r3, [r2]
	str	r3, [sp, #16]
	ldr	r3, [r2, #4]
	str	r3, [sp, #20]
	ldr	r2, [r4, #1752]
	ldr	r3, [r4, #1756]
	bl	printk
	b	.L3891
.L3895:
	add	r8, r8, #1
	b	.L3887
.L3890:
	ldr	r1, [r7, #-472]
	movw	r4, #2424
	ldr	r3, [r6, #2416]
	mov	r2, #2
	ldr	r0, .L3896+32
	bl	rknand_print_hex
	ldr	r1, [r7, #-452]
	ldrh	r3, [r6, r4]
	mov	r2, #4
	ldr	r0, .L3896+36
	bl	rknand_print_hex
	ldr	r0, .L3896+40
	ldr	r1, [r7, #-448]
	mov	r2, #4
	ldrh	r3, [r6, r4]
	add	sp, sp, #52
	@ sp needed
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, fp, lr}
	b	rknand_print_hex
.L3897:
	.align	2
.L3896:
	.word	.LANCHOR0
	.word	.LANCHOR0+2330
	.word	.LANCHOR2
	.word	.LANCHOR0+2320
	.word	.LC169
	.word	.LANCHOR0+2390
	.word	.LANCHOR2+1752
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
	ldr	r3, .L3900
	ldr	r2, [r3, #2264]
	ldr	r3, .L3900+4
	cmp	r2, r3
	bxne	lr
	b	flash_enter_slc_mode
.L3901:
	.align	2
.L3900:
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
	ldr	r3, .L3904
	ldr	r2, [r3, #2264]
	ldr	r3, .L3904+4
	cmp	r2, r3
	bxne	lr
	b	flash_exit_slc_mode
.L3905:
	.align	2
.L3904:
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
	ldr	r7, .L3959
	.pad #132
	sub	sp, sp, #132
	mov	r0, #256000
	mov	fp, r1
	mov	r6, r2
	ldr	r3, [r7, #44]
	ldr	r4, [r7, #4]
	ldrb	r5, [r3, #9]	@ zero_extendqisi2
	bl	ftl_malloc
	subs	r3, r0, #0
	str	r3, [sp, #8]
	beq	.L3933
	add	ip, r8, #508
	add	ip, ip, #3
	mov	r10, ip, lsr #9
	cmp	r10, #8
	bls	.L3931
	cmp	r10, #500
	bhi	.L3933
	b	.L3908
.L3931:
	mov	r10, #8
.L3908:
	ldr	r2, [fp]
	ldr	r3, .L3959+4
	cmp	r2, r3
	bne	.L3933
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
.L3912:
	ldr	r1, [r0, #-4]!
	cmp	r1, #0
	bne	.L3909
	ldr	r1, [fp, r3, asl #2]
	add	r3, r3, #1
	cmp	r3, #4096
	sub	r2, r2, #1
	movhi	r3, #0
	cmp	r2, #4096
	str	r1, [r0, #512]
	bne	.L3912
	b	.L3911
.L3909:
	ldr	r0, .L3959+8
	bl	printk
.L3911:
	mov	r3, #5
	ldr	r0, .L3959+12
	mov	r1, r6
	mov	r2, #4
	bl	rknand_print_hex
	ldr	r1, [fp, #512]
	ldrb	r2, [r7, #1]	@ zero_extendqisi2
	sub	r4, r6, #4
	ldr	r0, .L3959+16
	bl	printk
	ldr	r2, .L3959+20
	ldrh	r3, [r7, #142]
	mov	r1, r10
	ldr	r0, .L3959+24
	ldr	r2, [r2, #1708]
	ldr	r5, .L3959
	str	r2, [sp]
	mov	r2, r10
	bl	printk
	ldrb	r3, [r7, #1]	@ zero_extendqisi2
	ldr	r2, [fp, #512]
	cmp	r2, r3
	strhi	r3, [fp, #512]
	mov	r3, r10, asl #7
	str	r3, [sp, #40]
	mov	r3, #0
	str	r3, [sp, #20]
	str	r3, [sp, #16]
.L3929:
	ldr	r2, [r4, #4]
	ldrb	r3, [r5, #1]	@ zero_extendqisi2
	ldr	r7, .L3959
	cmp	r2, r3
	bcs	.L3914
	ldr	r3, .L3959+20
	ldr	r3, [r3, #1708]
	cmp	r2, r3
	bcc	.L3914
	ldr	r3, [sp, #36]
	ldr	r1, [sp, #36]
	cmp	r3, #1
	movls	r3, #0
	movhi	r3, #1
	str	r3, [sp, #44]
	ldr	r3, [sp, #16]
	cmp	r3, #0
	cmpne	r1, #1
	bls	.L3915
	ldr	r3, [r4]
	add	r3, r3, #1
	cmp	r2, r3
	beq	.L3914
.L3915:
	mov	r1, #0
	mov	r2, #512
	ldr	r0, [sp, #8]
	bl	memset
	ldr	r6, [r4, #4]
	mov	r2, r10
	ldr	r0, .L3959+28
	ldr	r3, [sp, #12]
	mul	r6, r6, r3
	ldr	r3, [r7, #44]
	ldr	r7, [r7, #4]
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
	bls	.L3916
	mov	r0, #0
	add	r1, r6, r7
	mov	r2, r0
	mov	r8, #2
	bl	FlashEraseBlock
.L3916:
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
.L3917:
	ldr	r3, [sp, #48]
	ldr	r7, .L3959
	cmp	r8, r3
	bcs	.L3957
	add	r3, r8, ip
	ubfx	r3, r3, #2, #16
	cmp	r3, #0
	beq	.L3918
	add	r2, r3, #1
	add	r1, r5, r2, asl #1
	ldrh	r7, [r1, #148]
	ldrb	r1, [r5, #144]	@ zero_extendqisi2
	cmp	r1, #0
	beq	.L3919
	ldr	r1, [r5, #2264]
	ldr	r0, .L3959+32
	cmp	r1, r0
	moveq	r7, r2
.L3919:
	sub	r7, r7, #-1073741823
	mov	r7, r7, asl #2
	str	r7, [sp, #64]
.L3918:
	movw	r2, #61424
	str	r2, [sp, #68]
	add	r2, r5, r3, asl #1
	ldrh	r7, [r2, #148]
	ldrb	r2, [r5, #144]	@ zero_extendqisi2
	cmp	r2, #0
	beq	.L3920
	ldr	r2, [r5, #2264]
	ldr	r1, .L3959+32
	cmp	r2, r1
	moveq	r7, r3
.L3920:
	ldr	r3, [sp, #28]
	add	r8, r8, #4
	str	ip, [sp, #56]
	uxth	r8, r8
	mla	r2, r9, r7, r3
	ldr	r3, .L3959
	ldrb	r3, [r3, #2312]	@ zero_extendqisi2
	str	r2, [sp, #52]
	str	r3, [sp, #32]
	ldr	r3, .L3959+20
	ldrb	r0, [r3, #1714]	@ zero_extendqisi2
	bl	FlashBchSel
	mov	r0, #0
	bl	flash_boot_enter_slc_mode
	ldr	r3, .L3959
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
	b	.L3917
.L3957:
	mov	r1, r6
	mov	r2, r10
	mov	r3, #0
	ldr	r0, .L3959+36
	bl	printk
	ldr	r6, [r4, #4]
	ldr	r8, [r7, #4]
	mov	r2, r10
	ldr	r0, .L3959+40
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
.L3922:
	cmp	r9, r10
	bcs	.L3958
	ldr	r3, [sp, #28]
	rsb	r8, ip, #4
	ldrb	r1, [r5, #144]	@ zero_extendqisi2
	add	r3, r9, r3
	uxth	r8, r8
	ubfx	r3, r3, #2, #16
	cmp	r1, #0
	add	r2, r5, r3, asl #1
	ldrh	r2, [r2, #148]
	beq	.L3923
	ldr	r1, [r5, #2264]
	ldr	r0, .L3959+32
	cmp	r1, r0
	moveq	r2, r3
.L3923:
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
	ldr	r2, .L3959+20
	str	r3, [sp, #60]
	str	r1, [sp, #56]
	ldrb	r0, [r2, #1714]	@ zero_extendqisi2
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
	b	.L3922
.L3958:
	mov	r1, r6
	mov	r2, r10
	mov	r3, #0
	ldr	r0, .L3959+44
	bl	printk
	mov	r6, #0
	mov	r3, fp
	ldr	r2, [sp, #8]
.L3928:
	mov	r8, r2
	mov	r7, r3
	ldr	r0, [r8]
	add	r2, r2, #4
	ldr	r1, [r7]
	add	r3, r3, #4
	cmp	r0, r1
	beq	.L3925
	mov	r1, #0
	mov	r2, #512
	ldr	r0, [sp, #8]
	bl	memset
	ldr	r3, [r8]
	ldr	r0, .L3959+48
	str	r3, [sp]
	ldr	r3, [r7]
	bic	r7, r6, #255
	ldr	r1, [sp, #16]
	mov	r7, r7, asl #2
	str	r3, [sp, #4]
	mov	r3, r6
	ldr	r2, [r4, #4]
	bl	printk
	ldr	r0, .L3959+52
	add	r1, fp, r7
	mov	r2, #4
	mov	r3, #256
	bl	rknand_print_hex
	mov	r2, #4
	ldr	r0, .L3959+56
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
	beq	.L3926
	ldr	r1, [r4, #4]
	mov	r0, #0
	ldr	r3, [sp, #12]
	mov	r2, r0
	mla	r1, r1, r3, r3
	bl	FlashEraseBlock
.L3926:
	mov	r0, #0
	bl	flash_boot_exit_slc_mode
	ldr	r0, .L3959+60
	ldr	r1, [r4, #4]
	bl	printk
	ldr	r3, [sp, #40]
	cmp	r6, r3
	bcc	.L3914
	b	.L3927
.L3925:
	ldr	r1, [sp, #40]
	add	r6, r6, #1
	cmp	r6, r1
	bne	.L3928
.L3927:
	ldr	r3, [sp, #20]
	add	r3, r3, #1
	str	r3, [sp, #20]
.L3914:
	ldr	r3, [sp, #16]
	add	r4, r4, #4
	add	r3, r3, #1
	str	r3, [sp, #16]
	cmp	r3, #5
	bne	.L3929
	ldr	r0, [sp, #8]
	bl	ftl_free
	ldr	r3, [sp, #20]
	clz	r0, r3
	mov	r0, r0, lsr #5
	rsb	r0, r0, #0
	b	.L3907
.L3933:
	mvn	r0, #0
.L3907:
	add	sp, sp, #132
	@ sp needed
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L3960:
	.align	2
.L3959:
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
	ldr	r4, .L3984
	bne	.L3962
	ldr	r2, [r2]
	ldr	r3, .L3984+4
	cmp	r2, r3
	bne	.L3962
	mov	r0, #256000
	mov	r3, #1
	strb	r3, [r4, #2020]
	bl	ftl_malloc
	mov	r1, #0
	mov	r2, #256000
	str	r0, [r4, #2024]
	bl	ftl_memset
	str	r5, [r4, #2028]
.L3962:
	str	r6, [sp]
	mov	r3, r5
	ldr	r0, .L3984+8
	ldr	r1, [r4, #2024]
	ldr	r2, [r8]
	bl	printk
	ldrb	r3, [r4, #2020]	@ zero_extendqisi2
	ldr	r9, .L3984
	cmp	r3, #0
	beq	.L3961
	sub	r0, r5, #64
	ldr	r7, [r9, #2024]
	cmp	r0, #500
	bcs	.L3964
	rsb	r2, r5, #564
	add	r0, r7, r0, asl #9
	cmp	r2, r6
	mov	r1, r8
	movcs	r2, r6
	mov	r2, r2, asl #9
	bl	ftl_memcpy
	b	.L3965
.L3964:
	cmp	r5, #564
	bcs	.L3973
.L3965:
	ldr	r3, [r4, #2028]
	cmp	r3, r5
	beq	.L3971
	ldr	r2, .L3984
	mov	r3, #0
	cmp	r7, r3
	mov	r8, r3
	strb	r3, [r2, #2020]
	beq	.L3972
	mov	r0, r7
	bl	ftl_free
.L3972:
	str	r8, [r4, #2024]
.L3971:
	add	r5, r5, r6
	str	r5, [r4, #2028]
	b	.L3961
.L3973:
	ldr	r3, .L3984+12
	ldr	r0, [r9, #2028]
	ldr	r3, [r3, #44]
	sub	r0, r0, #64
	cmp	r0, #500
	ldrb	r3, [r3, #9]	@ zero_extendqisi2
	movcs	r0, #500
	cmp	r3, #4
	beq	.L3974
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
	b	.L3967
.L3974:
	mov	r3, #0
.L3966:
	cmp	r0, #256
	add	r1, sp, #8
	mov	r2, r3, asl #1
	movls	r2, r3
	str	r2, [r1, r3, asl #2]
	add	r3, r3, #1
	cmp	r3, #5
	bne	.L3966
.L3967:
	movw	r3, #63872
.L3970:
	ldr	r2, [r7, r3, asl #2]
	cmp	r2, #0
	addne	r3, r3, #128
	movne	r0, r3, asl #2
	bne	.L3969
.L3968:
	sub	r3, r3, #1
	cmp	r3, #4096
	bne	.L3970
	mov	r0, r0, asl #9
.L3969:
	mov	r1, r7
	add	r2, sp, #8
	bl	write_idblock
	ldr	r0, [r4, #2024]
	mov	r7, #0
	strb	r7, [r4, #2020]
	bl	ftl_free
	str	r7, [r4, #2024]
	b	.L3971
.L3961:
	add	sp, sp, #52
	@ sp needed
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, pc}
.L3985:
	.align	2
.L3984:
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
	beq	.L3987
	mov	r0, r1
	mov	r2, r3
	mov	r1, r6
	bl	write_loader_lba
.L3987:
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
	ldr	r3, .L4049
	stmfd	sp!, {r4, r5, r6, r7, lr}
	.save {r4, r5, r6, r7, lr}
	cmp	r1, r3
	.pad #524
	sub	sp, sp, #524
	mov	r4, r1
	mov	r5, r2
	beq	.L3994
	bhi	.L3995
	sub	r3, r3, #2080
	sub	r3, r3, #6
	cmp	r1, r3
	beq	.L3996
	bhi	.L3997
	sub	r3, r3, #238
	cmp	r1, r3
	beq	.L3998
	add	r3, r3, #237
	cmp	r1, r3
	beq	.L3999
	b	.L4029
.L3997:
	ldr	r3, .L4049+4
	cmp	r1, r3
	beq	.L4000
	add	r3, r3, #1
	cmp	r1, r3
	beq	.L4001
	sub	r3, r3, #124
	cmp	r1, r3
	bne	.L4029
	b	.L4047
.L3995:
	ldr	r3, .L4049+8
	cmp	r1, r3
	mov	r6, r3
	beq	.L4003
	bhi	.L4004
	sub	r3, r3, #2512
	sub	r3, r3, #14
	cmp	r1, r3
	beq	.L3994
	add	r3, r3, #10
	cmp	r1, r3
	beq	.L3994
	b	.L4029
.L4004:
	ldr	r3, .L4049+12
	cmp	r1, r3
	beq	.L4003
	bcc	.L4005
	add	r3, r3, #1
	cmp	r1, r3
	beq	.L4005
	b	.L4029
.L3999:
	ldr	r0, .L4049+16
	bl	printk
	mov	r1, r5
	mov	r2, #520
	mov	r0, sp
	bl	rk_copy_from_user
	cmp	r0, #0
	beq	.L4006
.L4012:
	ldr	r0, .L4049+20
	bl	printk
	b	.L4044
.L4006:
	ldr	r2, [sp]
	ldr	r3, .L4049+24
	cmp	r2, r3
	beq	.L4007
.L4009:
	mvn	r4, #0
	b	.L4008
.L4007:
	ldr	r3, [sp, #4]
	cmp	r3, #512
	bhi	.L4009
	ldr	r4, .L4049+28
	mov	r2, #512
	mov	r0, sp
	ldr	r1, [r4, #2032]
	bl	memcpy
	ldr	r2, [r4, #2036]
	ldr	r3, .L4049+32
	cmp	r2, r3
	beq	.L4010
	mov	r1, #0
	add	r0, sp, #64
	mov	r2, #128
	str	r1, [sp, #8]
	str	r1, [sp, #12]
	bl	memset
.L4010:
	mov	r1, #0
	add	r0, sp, #256
	mov	r2, #256
	str	r1, [sp, #16]
	bl	memset
.L4039:
	mov	r0, r5
	mov	r1, sp
	mov	r2, #520
	bl	rk_copy_to_user
	cmp	r0, #0
	bne	.L4044
	b	.L4043
.L3996:
	ldr	r0, .L4049+36
	bl	printk
	mov	r1, r5
	mov	r2, #520
	mov	r0, sp
	bl	rk_copy_from_user
	cmp	r0, #0
	bne	.L4012
	ldr	r2, [sp]
	ldr	r3, .L4049+24
	cmp	r2, r3
	bne	.L4009
	ldr	r3, [sp, #4]
	cmp	r3, #512
	bhi	.L4009
	ldr	r2, .L4049+28
	ldr	r3, .L4049+32
	ldr	r1, [r2, #2036]
	cmp	r1, r3
	mvnne	r0, #1
	bne	.L3993
	ldr	r3, [sp, #12]
	sub	r1, r3, #1
	cmp	r1, #127
	mvnhi	r0, #2
	bhi	.L3993
	ldr	r4, [r2, #2032]
	add	r1, sp, #64
	add	r0, r4, #64
	str	r3, [r4, #12]
	ldr	r2, [sp, #12]
	bl	memcpy
	mov	r0, #1
	mov	r1, r4
	b	.L4042
.L4001:
	ldr	r0, .L4049+40
	bl	printk
	mov	r1, r5
	mov	r2, #520
	mov	r0, sp
	bl	rk_copy_from_user
	cmp	r0, #0
	bne	.L4012
	ldr	r2, [sp]
	ldr	r3, .L4049+44
	cmp	r2, r3
	bne	.L4009
	ldr	r3, [sp, #4]
	cmp	r3, #512
	bhi	.L4009
	ldr	r5, .L4049+28
	ldr	r3, [r5, #2040]
	cmp	r3, #0
	bne	.L4013
.L4016:
	mov	r0, #0
	b	.L3993
.L4013:
	ldr	r3, [r5, #2044]
	ldr	r2, .L4049+48
	ldr	r1, [r3]
	cmp	r1, r2
	beq	.L4014
	str	r2, [r3]
	mov	r2, #504
	ldr	r3, .L4049+28
	ldr	r3, [r3, #2044]
	str	r2, [r3, #4]
	mov	r2, #0
	str	r2, [r3, #8]
	str	r2, [r3, #12]
.L4014:
	ldr	r1, [r5, #2044]
	mov	r4, #0
	mov	r0, r4
	str	r4, [r1, #16]
	bl	StorageSysDataStore
	ldr	r3, [r5, #2032]
	ldr	r2, .L4049+24
	ldr	r1, [r3]
	cmp	r1, r2
	strne	r2, [r3]
	ldr	r6, [r5, #2032]
	ldrne	r3, .L4049+28
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
	b	.L4008
.L4000:
	ldr	r0, .L4049+52
	bl	printk
	mov	r1, r5
	mov	r2, #520
	mov	r0, sp
	bl	rk_copy_from_user
	cmp	r0, #0
	bne	.L4012
	ldr	r2, [sp]
	ldr	r3, .L4049+56
	cmp	r2, r3
	bne	.L4009
	ldr	r3, [sp, #4]
	cmp	r3, #512
	bhi	.L4009
	ldr	r5, .L4049+28
	ldr	r3, [r5, #2040]
	cmp	r3, #1
	beq	.L4016
	ldr	r2, [r5, #2044]
	ldr	r3, .L4049+48
	ldr	r1, [r2]
	cmp	r1, r3
	beq	.L4017
	str	r3, [r2]
	mov	r2, #504
	ldr	r3, [r5, #2044]
	str	r2, [r3, #4]
	mov	r2, #0
	str	r2, [r3, #8]
	str	r2, [r3, #12]
.L4017:
	ldr	r1, [r5, #2044]
	mov	r3, #1
	mov	r0, #0
	mov	r4, #0
	str	r3, [r1, #16]
	bl	StorageSysDataStore
	ldr	r3, [r5, #2032]
	ldr	r2, .L4049+24
	ldr	r1, [r3]
	cmp	r1, r2
	strne	r2, [r3]
	ldr	r6, [r5, #2032]
	ldrne	r3, .L4049+28
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
	b	.L4008
.L4047:
	ldr	r0, .L4049+60
	bl	printk
	mov	r1, r5
	mov	r2, #520
	mov	r0, sp
	bl	rk_copy_from_user
	cmp	r0, #0
	bne	.L4012
	ldr	r2, [sp]
	ldr	r3, .L4049+64
	cmp	r2, r3
	bne	.L4009
	ldr	r2, [sp, #4]
	cmp	r2, #512
	addls	r0, sp, #8
	ldrls	r1, .L4049+68
	bls	.L4045
	b	.L4009
.L3994:
	ldr	r0, .L4049+72
	cmp	r4, r0
	mov	r7, r0
	ldreq	r0, .L4049+76
	beq	.L4041
	ldr	r3, .L4049+80
	cmp	r4, r3
	ldreq	r0, .L4049+84
	ldrne	r0, .L4049+88
.L4041:
	bl	printk
	mov	r1, r5
	mov	r2, #520
	mov	r0, sp
	bl	rk_copy_from_user
	cmp	r0, #0
	bne	.L4012
	ldr	r2, [sp]
	ldr	r3, .L4049+92
	cmp	r2, r3
	bne	.L4044
	ldr	r3, .L4049+80
	ldr	r6, .L4049+28
	cmp	r4, r3
	bne	.L4023
	ldr	r3, [r6, #2032]
	mov	r0, r5
	mov	r1, sp
	mov	r2, #16
	ldr	r3, [r3, #20]
	str	r3, [sp, #4]
	strb	r3, [sp, #8]
	bl	rk_copy_to_user
	cmp	r0, #0
	beq	.L3993
	b	.L4044
.L4023:
	ldr	r3, [r6, #2560]
	cmp	r3, #10
	bhi	.L4044
	ldr	r1, [r6, #2032]
	ldr	r2, [sp, #4]
	ldr	r3, [r1, #24]
	cmp	r3, r2
	cmpne	r3, #0
	movne	r3, #1
	moveq	r3, #0
	beq	.L4024
	ldr	r0, .L4049+96
	mov	r1, r2
	bl	printk
	ldr	r3, [r6, #2560]
	add	r3, r3, #1
	str	r3, [r6, #2560]
.L4044:
	mvn	r0, #13
	b	.L3993
.L4024:
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
	bne	.L4043
	b	.L4048
.L4003:
	ldr	r0, .L4049+100
	bl	printk
	mov	r1, r5
	mov	r2, #520
	mov	r0, sp
	bl	rk_copy_from_user
	cmp	r0, #0
	bne	.L4012
	ldr	r2, [sp]
	ldr	r3, .L4049+104
	cmp	r2, r3
	bne	.L4009
	ldr	r2, [sp, #4]
	cmp	r2, #504
	bhi	.L4009
	ldr	r3, .L4049+28
	cmp	r4, r6
	add	r0, sp, #8
	ldreq	r1, [r3, #2564]
	ldrne	r1, [r3, #2568]
	add	r1, r1, #8
.L4045:
	bl	memcpy
	b	.L4039
.L4005:
	ldr	r0, .L4049+108
	bl	printk
	mov	r1, r5
	mov	r2, #520
	mov	r0, sp
	bl	rk_copy_from_user
	cmp	r0, #0
	bne	.L4012
	ldr	r2, [sp]
	ldr	r3, .L4049+104
	cmp	r2, r3
	bne	.L4009
	ldr	r2, [sp, #4]
	cmp	r2, #504
	bhi	.L4009
	ldr	r3, .L4049+112
	add	r2, r2, #8
	cmp	r4, r3
	ldr	r4, .L4049+28
	bne	.L4028
	mov	r1, sp
	ldr	r0, [r4, #2564]
	bl	memcpy
	mov	r0, #2
	ldr	r1, [r4, #2564]
	b	.L4042
.L4028:
	mov	r1, sp
	ldr	r0, [r4, #2568]
	bl	memcpy
	ldr	r1, [r4, #2568]
	mov	r0, #3
.L4042:
	bl	StorageSysDataStore
	mov	r4, r0
	b	.L4008
.L3998:
	bl	rknand_dev_flush
.L4043:
	mov	r4, #0
	b	.L4008
.L4048:
	mvn	r4, #1
.L4008:
	ldr	r0, .L4049+116
	mov	r1, r4
	bl	printk
	mov	r0, r4
	b	.L3993
.L4029:
	mvn	r0, #21
.L3993:
	add	sp, sp, #524
	@ sp needed
	ldmfd	sp!, {r4, r5, r6, r7, pc}
.L4050:
	.align	2
.L4049:
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
	.word	.LANCHOR4+2048
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
	ldr	r4, .L4061
	mvn	r3, #0
	mov	r5, #0
	add	r0, r4, #2048
	ldr	r1, [r4, #2004]
	str	r3, [r4, #2028]
	add	r3, r1, #512
	str	r3, [r4, #2032]
	str	r1, [r4, #2044]
	add	r3, r1, #1024
	add	r1, r1, #1536
	str	r3, [r4, #2564]
	strb	r5, [r4, #2020]
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
	beq	.L4052
	mov	r0, r6
	mov	r1, #508
	bl	js_hash
	cmp	r7, r0
	beq	.L4052
	str	r5, [r6, #16]
	ldr	r0, .L4061+4
	str	r5, [r4, #2040]
	bl	printk
.L4052:
	ldr	r3, [r4, #2040]
	mov	r0, #2
	ldr	r1, [r4, #2564]
	cmp	r3, #0
	ldrne	r3, .L4061
	ldrne	r2, .L4061+8
	strne	r2, [r3, #2036]
	bl	StorageSysDataLoad
	ldr	r1, [r4, #2568]
	mov	r0, #3
	bl	StorageSysDataLoad
	ldmfd	sp!, {r3, r4, r5, r6, r7, lr}
	b	rknand_sys_storage_init
.L4062:
	.align	2
.L4061:
	.word	.LANCHOR4
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
	ldr	r6, .L4075
	cmp	r0, #0
	str	r0, [r6, #2576]
	beq	.L4070
	ldr	r9, .L4075+4
	mov	r8, #0
	mov	r4, r8
	mov	r7, r8
	mov	r10, r6
.L4068:
	mov	r0, r7, asl #7
	mov	r1, #128
	ldr	r2, [r6, #2576]
	bl	FlashBootVendorRead
	cmp	r0, #0
	bne	.L4066
	ldr	r3, [r10, #2576]
	ldr	r0, .L4075+8
	add	r2, r3, #61440
	ldr	r1, [r3]
	ldr	r2, [r2, #4092]
	ldr	r3, [r3, #4]
	bl	printk
	ldr	r5, [r10, #2576]
	ldr	r3, [r5]
	cmp	r3, r9
	bne	.L4067
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
.L4067:
	cmp	r7, #1
	movne	r7, #1
	bne	.L4068
.L4074:
	cmp	r4, #0
	beq	.L4069
	mov	r0, r8, asl #7
	mov	r1, #128
	mov	r2, r5
	bl	FlashBootVendorRead
	cmp	r0, #0
	bne	.L4066
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, pc}
.L4069:
	mov	r0, r5
	mov	r1, r4
	mov	r2, #65536
	bl	memset
	ldr	r3, .L4075+4
	str	r7, [r5, #4]
	mov	r0, r4
	str	r3, [r5]
	add	r3, r5, #61440
	str	r7, [r3, #4092]
	ldr	r3, .L4075+12
	strh	r4, [r5, #12]	@ movhi
	strh	r3, [r5, #14]	@ movhi
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, pc}
.L4066:
	ldr	r0, [r6, #2576]
	bl	kfree
	mov	r3, #0
	mvn	r0, #0
	str	r3, [r6, #2576]
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, pc}
.L4070:
	mvn	r0, #11
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, pc}
.L4076:
	.align	2
.L4075:
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
	ldr	r3, .L4084
	ldr	ip, [r3, #2576]
	cmp	ip, #0
	ldrneh	r4, [ip, #10]
	movne	r3, #0
	beq	.L4082
.L4079:
	cmp	r3, r4
	bcs	.L4082
	add	lr, ip, r3, asl #3
	ldrh	r5, [lr, #16]
	cmp	r5, r0
	bne	.L4080
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
.L4080:
	add	r3, r3, #1
	b	.L4079
.L4082:
	mvn	r0, #0
	ldmfd	sp!, {r3, r4, r5, pc}
.L4085:
	.align	2
.L4084:
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
	ldr	r2, .L4109
	.pad #28
	sub	sp, sp, #28
	ldr	r4, [r2, #2576]
	cmp	r4, #0
	beq	.L4101
	mov	r3, r1
	add	r7, r9, #63
	ldrh	r1, [r4, #8]
	bic	r7, r7, #63
	ldrh	r2, [r4, #10]
	mov	ip, r0
	mov	r6, #0
	str	r1, [sp, #4]
.L4088:
	cmp	r6, r2
	bcs	.L4107
	add	r5, r4, r6, asl #3
	ldrh	r1, [r5, #16]
	cmp	r1, ip
	bne	.L4089
	ldrh	r1, [r5, #20]
	add	fp, r4, #1024
	add	r1, r1, #63
	bic	r1, r1, #63
	str	r1, [sp, #8]
	cmp	r9, r1
	bls	.L4090
	ldrh	r1, [r4, #14]
	cmp	r1, r7
	subcs	r2, r2, #1
	strcs	r2, [sp, #12]
	ldrcsh	r8, [r5, #18]
	bcc	.L4101
.L4091:
	ldr	r2, [sp, #12]
	add	r5, r5, #8
	cmp	r6, r2
	bcs	.L4108
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
	b	.L4091
.L4108:
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
	b	.L4106
.L4090:
	ldrh	r0, [r5, #18]
	mov	r1, r3
	mov	r2, r9
	add	r0, fp, r0
	bl	memcpy
	strh	r9, [r5, #20]	@ movhi
	b	.L4106
.L4089:
	add	r6, r6, #1
	b	.L4088
.L4107:
	ldrh	r1, [r4, #14]
	cmp	r1, r7
	bcc	.L4101
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
.L4106:
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
	b	.L4087
.L4101:
	mvn	r0, #0
.L4087:
	add	sp, sp, #28
	@ sp needed
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L4110:
	.align	2
.L4109:
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
	beq	.L4112
	ldr	r3, .L4128
	cmp	r6, r3
	beq	.L4114
	add	r3, r3, #1
	cmp	r6, r3
	beq	.L4115
	b	.L4126
.L4114:
	mov	r1, r5
	mov	r2, #8
	bl	rk_copy_from_user
	cmp	r0, #0
	bne	.L4126
	ldr	r2, [r4]
	ldr	r3, .L4128+4
	cmp	r2, r3
	beq	.L4117
.L4118:
	mvn	r5, #0
	b	.L4113
.L4117:
	ldrh	r0, [r4, #4]
	add	r1, r4, #8
	ldrh	r2, [r4, #6]
	bl	rk_ftl_vendor_read
	cmn	r0, #1
	beq	.L4118
	uxth	r2, r0
	strh	r0, [r4, #6]	@ movhi
	mov	r1, r4
	mov	r0, r5
	add	r2, r2, #8
	bl	rk_copy_to_user
	cmp	r0, #0
	moveq	r5, #0
	mvnne	r5, #13
	b	.L4113
.L4115:
	mov	r1, r5
	mov	r2, #8
	bl	rk_copy_from_user
	cmp	r0, #0
	bne	.L4126
	ldr	r2, [r4]
	ldr	r3, .L4128+4
	cmp	r2, r3
	bne	.L4118
	ldrh	r2, [r4, #6]
	movw	r3, #4087
	cmp	r2, r3
	bhi	.L4118
	mov	r0, r4
	mov	r1, r5
	add	r2, r2, #8
	bl	rk_copy_from_user
	cmp	r0, #0
	bne	.L4126
	ldrh	r0, [r4, #4]
	add	r1, r4, #8
	ldrh	r2, [r4, #6]
	bl	rk_ftl_vendor_write
	mov	r5, r0
	b	.L4113
.L4126:
	mvn	r5, #13
.L4113:
	mov	r0, r4
	bl	kfree
.L4112:
	mov	r0, r5
	ldmfd	sp!, {r4, r5, r6, pc}
.L4129:
	.align	2
.L4128:
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
	.type	__func__.20390, %object
	.size	__func__.20390, 11
__func__.20390:
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
	.type	__func__.21169, %object
	.size	__func__.21169, 17
__func__.21169:
	.ascii	"FtlDumpBlockInfo\000"
	.space	3
	.type	__func__.21188, %object
	.size	__func__.21188, 16
__func__.21188:
	.ascii	"FtlScanAllBlock\000"
	.type	__func__.21456, %object
	.size	__func__.21456, 17
__func__.21456:
	.ascii	"ftl_scan_all_ppa\000"
	.space	3
	.type	__func__.21137, %object
	.size	__func__.21137, 12
__func__.21137:
	.ascii	"FtlCheckVpc\000"
	.type	__func__.21436, %object
	.size	__func__.21436, 21
__func__.21436:
	.ascii	"FtlVpcCheckAndModify\000"
	.space	3
	.type	__func__.20463, %object
	.size	__func__.20463, 8
__func__.20463:
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
	.ascii	"FTL version: 5.0.63 20200923\000"
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
