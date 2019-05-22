/*
 * Copyright (c) 2016-2018, Fuzhou Rockchip Electronics Co., Ltd
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 * date: 2019-05-08
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
	@ link register save eliminated.
	ldr	r3, .L71
	add	r0, r3, r0, asl #3
	ldrb	r2, [r0, #16]	@ zero_extendqisi2
	ldr	r3, [r0, #12]
	add	r3, r3, r2, asl #8
	mov	r2, #16
	str	r2, [r3, #2056]
	bx	lr
.L72:
	.align	2
.L71:
	.word	.LANCHOR0
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
	ldr	r2, .L74
	add	r0, r2, r0, asl #3
	ldrb	r2, [r2, #54]	@ zero_extendqisi2
	ldrb	r1, [r0, #16]	@ zero_extendqisi2
	ldr	r3, [r0, #12]
	add	r3, r3, r1, asl #8
	str	r2, [r3, #2056]
	bx	lr
.L75:
	.align	2
.L74:
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
	ldr	r3, .L80
	add	r1, r0, r1
.L77:
	cmp	r0, r1
	beq	.L79
	mov	r2, r3, asl #5
	ldrb	ip, [r0], #1	@ zero_extendqisi2
	add	r2, r2, r3, lsr #2
	add	r2, r2, ip
	eor	r3, r3, r2
	b	.L77
.L79:
	mov	r0, r3
	bx	lr
.L81:
	.align	2
.L80:
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
	ldr	r3, .L85
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
.L86:
	.align	2
.L85:
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
	ldr	r0, .L93
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
	bne	.L89
	ldrb	r3, [fp, #140]	@ zero_extendqisi2
	cmp	r3, #0
	ldreq	r3, .L93
	addeq	r4, r3, r4, asl #1
	ldreqh	r4, [r4, #144]
.L89:
	add	ip, fp, r0, asl #2
	ldrb	r3, [sp, #48]	@ zero_extendqisi2
	ldr	r1, [ip, #656]
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
.L94:
	.align	2
.L93:
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
	ldr	r3, .L96
	ldr	r2, [r3, #688]
	str	r0, [r3, #688]
	mov	r0, r2
	bx	lr
.L97:
	.align	2
.L96:
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
	ldr	r3, .L99
	ldr	r3, [r3, #692]
	ldrb	r0, [r3, #9]	@ zero_extendqisi2
	bx	lr
.L100:
	.align	2
.L99:
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
	ldr	r3, .L103
	add	r0, r3, r0, asl #3
	ldrb	r2, [r0, #16]	@ zero_extendqisi2
	ldr	r0, [r0, #12]
	add	r2, r2, #8
	add	r0, r0, r2, asl #8
	bx	lr
.L104:
	.align	2
.L103:
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
	ldr	r3, .L106
	ldr	r2, [r3, #76]
	mov	r3, r0, asl #8
	orr	r0, r3, r0, asl #16
	orr	r3, r0, #1
	str	r3, [r2, #304]
	bx	lr
.L107:
	.align	2
.L106:
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
	ldr	r3, .L109
	orr	r0, r0, #16640
	ldr	r3, [r3, #76]
	str	r0, [r3, #344]
	bx	lr
.L110:
	.align	2
.L109:
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
	ldr	r3, .L114
	cmp	r0, #0
	ldr	r2, [r3, #76]
	ldr	r3, [r2]
	bfieq	r3, r0, #13, #1
	orrne	r3, r3, #253952
	str	r3, [r2]
	bx	lr
.L115:
	.align	2
.L114:
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
	ldr	r3, .L123
	ands	r1, r0, #6
	ldr	r2, [r3, #76]
	ldr	r3, [r2]
	bfieq	r3, r1, #13, #1
	beq	.L119
	orr	r3, r3, #24576
	movw	r1, #8322
	bfc	r3, #15, #1
	str	r1, [r2, #344]
	orr	r3, r3, #196608
	ldr	r1, .L123+4
	tst	r0, #4
	orrne	r3, r3, #32768
	str	r1, [r2, #304]
	mov	r1, #38
	str	r1, [r2, #308]
	mov	r1, #39
	str	r1, [r2, #308]
.L119:
	str	r3, [r2]
	mov	r0, #0
	bx	lr
.L124:
	.align	2
.L123:
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
	ldr	r2, .L126
	add	r0, r2, r0, asl #3
	mov	r2, #1
	ldr	r1, [r0, #12]
	ldrb	r0, [r0, #16]	@ zero_extendqisi2
	ldr	r3, [r1]
	mov	r2, r2, asl r0
	bfi	r3, r2, #0, #8
	str	r3, [r1]
	bx	lr
.L127:
	.align	2
.L126:
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
	ldr	r3, .L129
	add	r0, r3, r0, asl #3
	ldr	r2, [r0, #12]
	ldr	r3, [r2]
	bfc	r3, #0, #8
	bfc	r3, #17, #1
	str	r3, [r2]
	bx	lr
.L130:
	.align	2
.L129:
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
	ldr	r3, .L133
	add	r0, r0, #3
	umull	r0, r1, r0, r3
	ldr	r3, .L133+4
	ldr	r3, [r3, #8]
	mov	r0, r1, lsr #6
	blx	r3
	mov	r0, #0
	ldmfd	sp!, {r3, pc}
.L134:
	.align	2
.L133:
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
	ldr	r3, .L137
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
.L138:
	.align	2
.L137:
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
	ldr	r9, .L149
	mov	r5, r0
	ldr	r7, .L149+4
	mov	r6, r1
	add	r10, r9, #256
	mov	r4, #0
.L140:
	ldrb	r3, [r7, #697]	@ zero_extendqisi2
	cmp	r4, r3
	bcs	.L148
	mov	r3, #85
	str	r3, [r5, #8]
	ldrsb	r3, [r4, r10]
	mov	r0, #200
	str	r3, [r5, #4]
	bl	NandcDelayns
	ldrb	r3, [r7, #696]	@ zero_extendqisi2
	cmp	r3, #34
	addeq	r3, r4, r8
	addeq	r3, r10, r3
	beq	.L147
	cmp	r3, #35
	addne	r3, r9, r6
	addne	r3, r3, #400
	ldrnesb	r3, [r3]
	bne	.L146
	ldr	r3, .L149+8
	add	r2, r4, r8
	add	r3, r3, r2
.L147:
	ldrsb	r3, [r3, #5]
.L146:
	str	r3, [r5]
	add	r4, r4, #1
	b	.L140
.L148:
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, pc}
.L150:
	.align	2
.L149:
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
	ldr	r3, .L156
	stmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, lr}
	.save {r4, r5, r6, r7, r8, r9, r10, lr}
	add	r1, r3, r1, asl #2
	ldr	r8, .L156+4
	mov	r4, #0
	add	r5, r1, #3
	mov	r6, r0
	mov	r7, r3
	mov	r9, #161
	mov	r10, r4
.L152:
	ldrb	r3, [r8, #697]	@ zero_extendqisi2
	cmp	r4, r3
	bcs	.L155
	str	r9, [r6, #8]
	mov	r0, #300
	str	r10, [r6]
	ldrsb	r3, [r7, r4]
	add	r4, r4, #1
	str	r3, [r6]
	ldrsb	r3, [r5, #1]!
	str	r3, [r6]
	bl	NandcDelayns
	b	.L152
.L155:
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, pc}
.L157:
	.align	2
.L156:
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
	ldr	r5, .L167
	mov	r9, r2
	ldr	r4, .L167+4
	mov	r7, r0
	mov	r8, r1
	ldr	r3, [r5, #692]
	ldrb	r2, [r3, #19]	@ zero_extendqisi2
	mov	r3, r0, asl #3
	cmp	r2, #6
	addeq	r4, r4, r0, asl #6
	addeq	r4, r4, #20
	addeq	r4, r4, r6, asl #2
	beq	.L160
	cmp	r2, #7
	bne	.L161
	mov	r2, #160
	mla	r4, r2, r0, r4
	add	r2, r6, r6, asl #2
	add	r4, r4, #28
	add	r4, r4, r2, asl #1
	b	.L160
.L161:
	cmp	r2, #8
	addne	r2, r6, r3
	addeq	r2, r6, r6, asl #2
	ldreq	r4, .L167+8
	addne	r4, r4, r2, asl #3
	addeq	r4, r4, r2
	addne	r4, r4, #20
.L160:
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
.L163:
	cmp	ip, r9
	beq	.L166
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
	b	.L163
.L166:
	add	r10, r10, r3
	mov	r0, r7
	mov	r3, #22
	add	r5, r5, r7
	str	r3, [r10, #2056]
	bl	NandcFlashDeCs
	strb	r6, [r5, #1552]
	add	sp, sp, #12
	@ sp needed
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L168:
	.align	2
.L167:
	.word	.LANCHOR0
	.word	.LANCHOR0+698
	.word	.LANCHOR0+726
	.fnend
	.size	HynixSetRRPara, .-HynixSetRRPara
	.align	2
	.global	FlashSetReadRetryDefault
	.type	FlashSetReadRetryDefault, %function
FlashSetReadRetryDefault:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	ldr	r3, .L177
	ldr	r2, [r3, #692]
	ldrb	r2, [r2, #19]	@ zero_extendqisi2
	sub	r2, r2, #1
	cmp	r2, #7
	bxhi	lr
	stmfd	sp!, {r4, r5, r6, lr}
	.save {r4, r5, r6, lr}
	mov	r4, #0
	ldr	r6, .L177+4
	mov	r5, r3
.L170:
	ldrb	r3, [r6, r4, asl #3]	@ zero_extendqisi2
	uxtb	r0, r4
	cmp	r3, #173
	bne	.L171
	ldrb	r1, [r5, #699]	@ zero_extendqisi2
	mov	r3, #0
	ldr	r2, .L177+8
	bl	HynixSetRRPara
.L171:
	add	r4, r4, #1
	cmp	r4, #4
	bne	.L170
	ldmfd	sp!, {r4, r5, r6, pc}
.L178:
	.align	2
.L177:
	.word	.LANCHOR0
	.word	.LANCHOR0+1556
	.word	.LANCHOR0+702
	.fnend
	.size	FlashSetReadRetryDefault, .-FlashSetReadRetryDefault
	.align	2
	.global	FlashReadStatusEN
	.type	FlashReadStatusEN, %function
FlashReadStatusEN:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	ldr	ip, .L190
	stmfd	sp!, {r3, r4, r5, lr}
	.save {r3, r4, r5, lr}
	add	r0, ip, r0, asl #3
	ldr	r3, [ip, #692]
	ldrb	r5, [r0, #16]	@ zero_extendqisi2
	ldr	r4, [r0, #12]
	ldrb	r3, [r3, #8]	@ zero_extendqisi2
	cmp	r3, #2
	mov	r3, r5, asl #8
	addne	r3, r4, r3
	add	r5, r5, #8
	movne	r2, #112
	strne	r2, [r3, #2056]
	bne	.L184
	cmp	r2, #0
	add	r3, r4, r3
	ldrneb	r2, [ip, #58]	@ zero_extendqisi2
	ldreqb	r2, [ip, #57]	@ zero_extendqisi2
	str	r2, [r3, #2056]
	ldrb	r0, [ip, #59]	@ zero_extendqisi2
	cmp	r0, #0
	addne	ip, r4, r5, asl #8
	movne	r2, #0
	beq	.L184
.L183:
	cmp	r2, r0
	bcs	.L184
	mov	r3, r2, asl #3
	add	r2, r2, #1
	mov	r3, r1, lsr r3
	uxtb	r3, r3
	str	r3, [ip, #4]
	b	.L183
.L184:
	mov	r0, #80
	bl	NandcDelayns
	ldr	r0, [r4, r5, asl #8]
	uxtb	r0, r0
	ldmfd	sp!, {r3, r4, r5, pc}
.L191:
	.align	2
.L190:
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
.L196:
	mov	r0, r4
	mov	r1, r5
	mov	r2, r6
	bl	FlashReadStatusEN
	cmp	r0, #255
	beq	.L196
	tst	r0, #64
	beq	.L196
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
	ldr	r5, .L208
	add	r4, r5, r0, asl #4
	ldr	r3, [r4, #1596]
	ldrb	r7, [r4, #1588]	@ zero_extendqisi2
	cmp	r3, #0
	beq	.L202
	mov	r6, r0
	mov	r0, r7
	add	r5, r5, r6, asl #2
	bl	NandcFlashCs
	ldr	r1, [r4, #1592]
	mov	r0, r7
	ldr	r2, [r5, #656]
	adds	r2, r2, #0
	movne	r2, #1
	bl	FlashWaitReadyEN
	mov	r5, r0
	mov	r0, r7
	bl	NandcFlashDeCs
	ldr	r2, [r4, #1596]
	sbfx	r3, r5, #0, #1
	str	r3, [r2]
	mov	r2, #0
	ldr	r1, [r4, #1600]
	str	r2, [r4, #1596]
	cmp	r1, r2
	strne	r3, [r1]
	strne	r2, [r4, #1600]
.L202:
	mov	r0, #0
	ldmfd	sp!, {r3, r4, r5, r6, r7, pc}
.L209:
	.align	2
.L208:
	.word	.LANCHOR0
	.fnend
	.size	FlashWaitCmdDone, .-FlashWaitCmdDone
	.align	2
	.type	flash_read_ecc, %function
flash_read_ecc:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	ldr	r2, .L212
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
.L213:
	.align	2
.L212:
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
	ldr	r3, .L220
	stmfd	sp!, {r0, r1, r2, r4, r5, lr}
	.save {r4, r5, lr}
	.pad #12
	add	r0, r3, r0, asl #3
	ldr	r4, .L220+4
	ldr	r5, [r0, #12]
.L216:
	ldr	r3, [r5]
	str	r3, [sp, #4]
	ldr	r3, [sp, #4]
	tst	r3, #512
	bne	.L217
	mov	r0, #10
	bl	NandcDelayns
	subs	r4, r4, #1
	bne	.L216
	mvn	r0, #0
	b	.L215
.L217:
	mov	r0, #0
.L215:
	add	sp, sp, #12
	@ sp needed
	ldmfd	sp!, {r4, r5, pc}
.L221:
	.align	2
.L220:
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
	ldr	r3, .L228
	stmfd	sp!, {r0, r1, r2, r4, r5, lr}
	.save {r4, r5, lr}
	.pad #12
	add	r0, r3, r0, asl #3
	ldr	r4, .L228+4
	ldr	r5, [r0, #12]
	mov	r0, #150
	bl	NandcDelayns
.L224:
	ldr	r3, [r5]
	str	r3, [sp, #4]
	ldr	r3, [sp, #4]
	tst	r3, #512
	bne	.L225
	mov	r0, #10
	bl	NandcDelayns
	subs	r4, r4, #1
	bne	.L224
	mvn	r0, #0
	b	.L223
.L225:
	mov	r0, #0
.L223:
	add	sp, sp, #12
	@ sp needed
	ldmfd	sp!, {r4, r5, pc}
.L229:
	.align	2
.L228:
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
	ldr	r3, .L232
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
.L233:
	.align	2
.L232:
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
	ldr	r4, .L237
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
.L238:
	.align	2
.L237:
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
	ldr	r4, .L242
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
.L243:
	.align	2
.L242:
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
	ldr	r1, .L269
	stmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.save {r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.pad #20
	sub	sp, sp, #20
	ldrb	fp, [r1, #1716]	@ zero_extendqisi2
	mov	lr, #0
	ldr	r7, .L269+4
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
.L256:
	ldr	r3, [sp, #12]
	ldrb	ip, [lr, r7]	@ zero_extendqisi2
	ldr	r1, [r3, lr]!
	cmp	ip, #69
	cmpne	ip, #152
	ldrb	r3, [r3, #4]	@ zero_extendqisi2
	beq	.L247
	cmp	ip, #44
	cmpne	ip, #173
	bne	.L248
.L247:
	cmp	r0, #1
	bne	.L249
	ldr	fp, [sp, #8]
	cmp	fp, #0
	beq	.L248
	mov	r3, r3, asl #8
	cmp	ip, #173
	add	fp, r1, r3
	str	r4, [fp, #2056]
	streq	r0, [fp, #2052]
	beq	.L268
	cmp	ip, #44
	streq	r0, [fp, #2052]
	strne	r5, [fp, #2052]
	streq	r10, [fp, #2048]
	strne	r0, [fp, #2048]
	b	.L254
.L249:
	ldr	fp, [sp, #4]
	cmp	fp, #0
	beq	.L248
	mov	r3, r3, asl #8
	cmp	ip, #173
	add	fp, r1, r3
	str	r4, [fp, #2056]
	streq	r6, [fp, #2052]
	streq	r9, [fp, #2048]
	beq	.L254
	cmp	ip, #44
	streq	r6, [fp, #2052]
	streq	r8, [fp, #2048]
	beq	.L254
	str	r5, [fp, #2052]
.L268:
	str	r2, [fp, #2048]
.L254:
	add	r3, r1, r3
	str	r2, [r3, #2048]
	str	r2, [r3, #2048]
	str	r2, [r3, #2048]
.L248:
	add	lr, lr, #8
	cmp	lr, #32
	bne	.L256
	mov	r0, #0
	bl	NandcWaitFlashReady
	mov	r0, #0
	add	sp, sp, #20
	@ sp needed
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L270:
	.align	2
.L269:
	.word	.LANCHOR0
	.word	.LANCHOR0+1556
	.fnend
	.size	FlashSetInterfaceMode, .-FlashSetInterfaceMode
	.align	2
	.global	FlashReadSpare
	.type	FlashReadSpare, %function
FlashReadSpare:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	ldr	ip, .L273
	stmfd	sp!, {r3, r4, r5, lr}
	.save {r3, r4, r5, lr}
	add	ip, ip, r0, asl #3
	ldr	r3, .L273+4
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
.L274:
	.align	2
.L273:
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
	ldr	r2, .L277
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
.L278:
	.align	2
.L277:
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
	ldr	r0, .L287
	ldr	r1, .L287+4
	add	r4, r4, r4, asl #2
	sub	ip, r0, #48
	mov	r2, #0
.L280:
	ldrb	r3, [r1, #697]	@ zero_extendqisi2
	cmp	r2, r3
	bcs	.L286
	ldrb	r3, [r1, #696]	@ zero_extendqisi2
	cmp	r3, #67
	add	r3, r2, r4
	addeq	r3, ip, r3
	addne	r3, r0, r3
	add	r2, r2, #1
	ldrsb	r3, [r3, #5]
	str	r3, [r5]
	b	.L280
.L286:
	mov	r0, #0
	ldmfd	sp!, {r3, r4, r5, lr}
	b	NandcWaitFlashReady
.L288:
	.align	2
.L287:
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
	ldr	r3, .L291
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
.L292:
	.align	2
.L291:
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
	ldr	r7, .L306
	.pad #24
	sub	sp, sp, #24
	ldr	r9, .L306+4
	mov	r6, r0
	mov	r10, r1
	mov	r8, r5
	ldr	r3, [r7]
	str	r3, [sp, #20]
.L294:
	cmp	r8, r10
	beq	.L305
	rsb	r3, r8, r10
	add	r2, sp, #12
	add	r0, r6, r5
	mov	r1, #0
	uxtb	r3, r3
	str	r3, [sp]
	add	r3, sp, #16
	bl	LogAddr2PhyAddr
	ldrb	r2, [r9, #1717]	@ zero_extendqisi2
	ldr	r3, [sp, #16]
	cmp	r3, r2
	mvncs	r3, #0
	strcs	r3, [r6, r5]
	bcs	.L296
	add	r2, r9, r3
	add	r3, r9, r3, asl #4
	ldrb	r4, [r2, #1720]	@ zero_extendqisi2
	strb	r4, [r3, #1588]
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
	bne	.L298
	ldr	r0, .L306+8
	ldr	r1, [sp, #12]
	bl	printk
.L298:
	mov	r0, r4
	bl	NandcFlashDeCs
.L296:
	add	r8, r8, #1
	add	r5, r5, #36
	b	.L294
.L305:
	ldr	r2, [sp, #20]
	mov	r0, #0
	ldr	r3, [r7]
	cmp	r2, r3
	beq	.L300
	bl	__stack_chk_fail
.L300:
	add	sp, sp, #24
	@ sp needed
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, pc}
.L307:
	.align	2
.L306:
	.word	__stack_chk_guard
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
	ldr	r3, .L313
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
	bne	.L309
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
	b	.L312
.L309:
	str	lr, [r4, #2052]
	str	ip, [r4, #2052]
	str	r1, [r4, #2052]
	str	r3, [r4, #2056]
.L312:
	str	r9, [r4, #2052]
	mov	r3, #48
	str	r8, [r4, #2052]
	mov	r0, r7
	str	r5, [r4, #2052]
	mov	r1, r6
	str	r3, [r4, #2056]
	ldmfd	sp!, {r3, r4, r5, r6, r7, r8, r9, r10, fp, lr}
	b	FlashSetRandomizer
.L314:
	.align	2
.L313:
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
	ldr	r4, .L326
	bl	NandcWaitFlashReady
	bl	FlashSetReadRetryDefault
	ldr	r0, [r4, #1728]
	cmp	r0, #0
	beq	.L316
	mov	r0, #0
	bl	flash_enter_slc_mode
	b	.L317
.L316:
	bl	flash_exit_slc_mode
.L317:
	ldrb	r3, [r4, #1732]	@ zero_extendqisi2
	ldr	r5, .L326
	cmp	r3, #0
	beq	.L318
	ldrb	r3, [r5, #1716]	@ zero_extendqisi2
	tst	r3, #1
	beq	.L318
	mov	r0, #1
	bl	FlashSetInterfaceMode
	mov	r0, #1
	bl	NandcSetMode
	mov	r3, #0
	strb	r3, [r5, #1732]
.L318:
	ldr	r3, [r4, #12]
	mov	r0, #0
	str	r0, [r3, #336]
	ldmfd	sp!, {r3, r4, r5, pc}
.L327:
	.align	2
.L326:
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
	ldr	r3, .L329
	add	r0, r3, r0, asl #3
	ldr	r3, [r0, #12]
	str	r1, [r3, #336]
	bx	lr
.L330:
	.align	2
.L329:
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
	ldr	r1, .L342
	bl	__aeabi_idiv
	ldr	r3, .L342+4
	ldr	r3, [r3, #76]
	cmp	r0, #250
	movwgt	r2, #8354
	bgt	.L340
	cmp	r0, #220
	bgt	.L341
	cmp	r0, #185
	movwgt	r2, #4226
	bgt	.L340
	cmp	r0, #160
	movwgt	r2, #4194
	bgt	.L340
	cmp	r4, #35
	movwls	r2, #4193
	bls	.L340
	cmp	r4, #99
	movwls	r2, #4225
	bls	.L340
.L341:
	movw	r2, #8322
.L340:
	str	r2, [r3, #4]
	ldmfd	sp!, {r4, pc}
.L343:
	.align	2
.L342:
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
	bls	.L345
	movw	r3, #8322
	cmp	r0, r3
	bne	.L346
.L345:
	ldr	r3, .L347
	ldr	r3, [r3, #76]
	str	r0, [r3, #4]
.L346:
	ldr	r3, .L347+4
	ldrb	r0, [r3, #493]	@ zero_extendqisi2
	b	NandcTimeCfg
.L348:
	.align	2
.L347:
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
	ldr	r3, .L352
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
	str	ip, [r3, #1736]
	movw	ip, #2049
	ubfx	r1, r1, #16, #4
	str	r1, [r3, #1740]
	ldr	r1, [r0, #352]
	cmp	r1, ip
	str	r1, [r3, #1744]
	moveq	r3, #8
	streq	r3, [r4, #1740]
	str	r2, [r0]
	mov	r0, #40
	ldr	r3, [r4, #76]
	str	r5, [r3, #336]
	bl	NandcTimeCfg
	ldr	r3, [r4, #76]
	movw	r2, #8322
	mov	r0, #36864
	str	r2, [r3, #344]
	ldr	r2, .L352+4
	str	r2, [r3, #304]
	bl	ftl_malloc
	str	r5, [r4, #1776]
	str	r5, [r4, #1784]
	str	r0, [r4, #1748]
	str	r0, [r4, #1752]
	add	r0, r0, #32768
	str	r0, [r4, #1756]
	ldmfd	sp!, {r3, r4, r5, pc}
.L353:
	.align	2
.L352:
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
	ldr	ip, .L356
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
.L357:
	.align	2
.L356:
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
	ldr	r3, .L366
	mov	r1, #1
	ldr	r2, [r3, #76]
	str	r0, [r3, #1788]
	mov	r3, #0
	str	r1, [r2, #8]
	mov	r1, #16
	cmp	r0, r1
	bfi	r3, r1, #8, #8
	bfc	r3, #18, #1
	bne	.L359
.L362:
	bfc	r3, #4, #1
	b	.L360
.L359:
	cmp	r0, #24
	orreq	r3, r3, #16
	beq	.L360
	cmp	r0, #40
	orr	r3, r3, #262144
	orr	r3, r3, #16
	beq	.L362
.L360:
	orr	r3, r3, #1
	str	r3, [r2, #12]
	bx	lr
.L367:
	.align	2
.L366:
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
	ldr	r3, .L369
	strb	r0, [r3, #1792]
	b	NandcBchSel
.L370:
	.align	2
.L369:
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
	ldr	r3, .L380
	stmfd	sp!, {r4, r5, r6, lr}
	.save {r4, r5, r6, lr}
	mov	r5, #0
	ldr	r2, [r3, #76]
	mov	r4, r3
	ldr	r1, [r3, #80]
	ldr	r6, .L380+4
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
.L373:
	ldrb	r3, [r6, r5, asl #3]	@ zero_extendqisi2
	sub	r3, r3, #1
	uxtb	r3, r3
	cmp	r3, #253
	bhi	.L372
	uxtb	r0, r5
	bl	FlashReset
.L372:
	add	r5, r5, #1
	cmp	r5, #4
	bne	.L373
	ldrb	r3, [r4, #1732]	@ zero_extendqisi2
	ldr	r5, .L380
	cmp	r3, #0
	beq	.L374
	mov	r0, #1
	bl	NandcSetMode
	ldrb	r0, [r5, #1716]	@ zero_extendqisi2
	bl	FlashSetInterfaceMode
	ldrb	r0, [r5, #1716]	@ zero_extendqisi2
	bl	NandcSetMode
	ldrb	r0, [r5, #97]	@ zero_extendqisi2
	bl	NandcSetDdrPara
.L374:
	ldr	r3, [r4, #692]
	ldmfd	sp!, {r4, r5, r6, lr}
	ldrb	r0, [r3, #20]	@ zero_extendqisi2
	b	FlashBchSel
.L381:
	.align	2
.L380:
	.word	.LANCHOR0
	.word	.LANCHOR0+1556
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
	bne	.L385
	mov	r0, r4
	ldmfd	sp!, {r4, lr}
	b	wait_for_nand_flash_ready
.L385:
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
	ldr	r4, .L424
	.pad #36
	sub	sp, sp, #36
	ldr	r3, .L424+4
	ldrb	r5, [r4]	@ zero_extendqisi2
	ldr	r2, [r3]
	cmp	r5, #0
	str	r3, [sp, #12]
	str	r2, [sp, #28]
	moveq	r9, r0
	moveq	r10, r1
	moveq	fp, r4
	beq	.L388
	mov	r1, r8
	bl	FlashEraseSLc2KBlocks
	b	.L389
.L397:
	mov	r3, #36
	mov	r1, #0
	mul	r6, r3, r5
	add	r2, sp, #20
	add	r3, r9, r6
	str	r3, [sp, #8]
	rsb	r3, r5, r8
	ldr	r0, [sp, #8]
	uxtb	r3, r3
	str	r3, [sp]
	add	r3, sp, #24
	bl	LogAddr2PhyAddr
	ldrb	r3, [r4, #1717]	@ zero_extendqisi2
	mov	r7, r0
	ldr	r0, [sp, #24]
	cmp	r0, r3
	mvncs	r3, #0
	strcs	r3, [r9, r6]
	bcc	.L421
.L391:
	add	r5, r5, #1
.L388:
	cmp	r5, r8
	bcc	.L397
	b	.L422
.L421:
	ldrb	r3, [fp, #1793]	@ zero_extendqisi2
	cmp	r3, #0
	add	r3, r4, r0, asl #4
	ldr	r3, [r3, #1596]
	moveq	r7, #0
	cmp	r3, #0
	beq	.L393
	uxtb	r0, r0
	bl	FlashWaitCmdDone
.L393:
	ldr	r2, [sp, #24]
	cmp	r7, #0
	ldr	r0, [sp, #8]
	addne	ip, r6, #36
	addne	ip, r9, ip
	mov	r3, r2, asl #4
	add	r2, r4, r2
	add	r1, r4, r3
	add	r3, r4, r3
	ldrb	r6, [r2, #1720]	@ zero_extendqisi2
	str	r0, [r1, #1596]
	mov	r0, #0
	str	r0, [r1, #1600]
	ldr	r0, [sp, #20]
	strne	ip, [r1, #1600]
	strb	r6, [r3, #1588]
	str	r0, [r1, #1592]
	mov	r0, r6
	bl	NandcFlashCs
	cmp	r10, #1
	mov	r0, r6
	bne	.L395
	ldrb	r3, [fp, #140]	@ zero_extendqisi2
	cmp	r3, #0
	beq	.L395
	bl	flash_enter_slc_mode
	b	.L396
.L395:
	bl	flash_exit_slc_mode
.L396:
	ldr	r3, [sp, #24]
	mov	r0, r6
	ldr	r1, [sp, #20]
	add	r5, r5, r7
	add	r3, r4, r3, asl #2
	ldr	r2, [r3, #656]
	adds	r2, r2, #0
	movne	r2, #1
	bl	FlashWaitReadyEN
	mov	r0, r6
	mov	r2, r7
	ldr	r1, [sp, #20]
	bl	FlashEraseCmd
	mov	r0, r6
	bl	NandcFlashDeCs
	b	.L391
.L422:
	ldr	r0, [r4, #76]
	mov	r5, #0
	bl	NandcIqrWaitFlashReady
	ldr	r6, .L424
	ldr	r7, .L424+8
.L398:
	ldrb	r2, [r4, #1717]	@ zero_extendqisi2
	ldr	r3, .L424
	cmp	r5, r2
	bcs	.L423
	uxtb	r0, r5
	bl	FlashWaitCmdDone
	cmp	r10, #1
	bne	.L399
	ldrb	r3, [r6, #140]	@ zero_extendqisi2
	cmp	r3, #0
	beq	.L399
	ldrb	r0, [r7, r5, asl #4]	@ zero_extendqisi2
	bl	flash_exit_slc_mode
.L399:
	add	r5, r5, #1
	b	.L398
.L423:
	ldr	r2, [r3, #1796]
	cmp	r2, #0
	bne	.L401
.L403:
	mov	r0, #0
	b	.L389
.L401:
	ldrb	r3, [r3, #1556]	@ zero_extendqisi2
	cmp	r3, #69
	bne	.L403
	mov	r3, #0
	mov	r2, #36
	mov	r1, r3
.L402:
	cmp	r3, r8
	beq	.L403
	mul	r0, r2, r3
	add	r3, r3, #1
	str	r1, [r9, r0]
	b	.L402
.L389:
	ldr	r3, [sp, #12]
	ldr	r2, [sp, #28]
	ldr	r3, [r3]
	cmp	r2, r3
	beq	.L405
	bl	__stack_chk_fail
.L405:
	add	sp, sp, #36
	@ sp needed
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L425:
	.align	2
.L424:
	.word	.LANCHOR0
	.word	__stack_chk_guard
	.word	.LANCHOR0+1588
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
	ldr	r3, [r5, #1740]
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
	ldreq	r0, [r5, #1752]
	b	.L441
.L439:
	ldr	r3, [r5, #1788]
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
	ldreq	lr, [r9, #1756]
	orrne	lr, lr, fp, asl #16
	ldrne	fp, [r9, #1756]
	streq	fp, [lr, r10, asl #2]
	strne	lr, [fp, r10, asl #2]
	ldr	lr, [sp, #4]
	add	r3, r3, lr
	b	.L443
.L441:
	ldr	r3, [r5, #1756]
	ubfx	r10, r4, #22, #5
	mov	r2, r9
	str	r0, [r5, #1760]
	mov	r1, r10, asl #10
	str	r3, [r5, #1764]
	bl	rknand_dma_map_single
	mov	r2, r9
	mov	r1, r10, asl #7
	clz	r9, r9
	mov	r9, r9, lsr #5
	str	r0, [r5, #1768]
	ldr	r0, [r5, #1764]
	bl	rknand_dma_map_single
	mov	r3, #1
	str	r3, [r5, #1776]
	tst	r8, #3
	ldr	r3, [r5, #1768]
	str	r0, [r5, #1772]
	str	r3, [r6, #20]
	ldr	r3, [r5, #1772]
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
	movw	r2, #1804
	cmp	r0, #23
	movw	r1, #1814
	ldrh	r2, [r3, r2]
	movls	r0, #24
	ldrh	r1, [r3, r1]
	str	r0, [r3, #1800]
	mul	r2, r2, r0
	rsb	r0, r0, r1
	movw	r1, #1812
	strh	r0, [r3, r1]	@ movhi
	mov	r0, #0
	ldr	r1, [r3, #1820]
	str	r2, [r3, #1808]
	rsb	r2, r2, r1
	str	r2, [r3, #1816]
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
	movw	r1, #1828
	ldr	r4, .L493
	mov	r5, r0
	ldrh	r6, [r0, #8]
	.pad #20
	sub	sp, sp, #20
	add	r3, r4, #1824
	ldrh	r2, [r0, #10]
	ldrh	lr, [r0, #14]
	strh	r6, [r3]	@ movhi
	movw	r3, #1826
	strh	r2, [r4, r3]	@ movhi
	ldrh	r3, [r0, #12]
	ldr	r0, .L493+4
	strh	r3, [r4, r1]	@ movhi
	movw	r1, #1814
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
	movw	r1, #1826
	mov	r2, r2, asl #1
	strh	r2, [r4, r1]	@ movhi
	movw	r2, #1814
	mov	lr, lr, lsr #1
	strh	lr, [r4, r2]	@ movhi
.L467:
	cmp	r6, #1
	movw	r2, #1864
	mov	r1, #5
	strh	r1, [r4, r2]	@ movhi
	movw	r1, #1866
	mov	r0, #0
	strh	r0, [r4, r1]	@ movhi
	ldreq	r1, .L493
	ldrb	r9, [r4]	@ zero_extendqisi2
	ldrh	r10, [r5, #16]
	streqh	r6, [r1, r2]	@ movhi
	cmp	r9, #0
	movw	r2, #1868
	mov	r1, #4352
	strh	r1, [r4, r2]	@ movhi
	movne	r0, #384
	ldrne	r1, .L493
	strneh	r0, [r1, r2]	@ movhi
	movw	r1, #1826
	ldrh	r8, [r4, r1]
	movw	r1, #1804
	ldr	r2, .L493
	smulbb	r8, r8, r3
	add	r7, r2, #1872
	str	r2, [sp, #8]
	strh	r10, [r7]	@ movhi
	uxth	r8, r8
	strh	r8, [r4, r1]	@ movhi
	movw	r1, #1814
	ldrh	r6, [r4, r1]
	movw	r1, #1870
	smulbb	r0, r8, r10
	smulbb	r3, r6, r3
	strh	r3, [r4, r1]	@ movhi
	movw	r3, #1874
	ldrh	r1, [r5, #18]
	strh	r1, [r4, r3]	@ movhi
	movw	r3, #1876
	strh	r0, [r4, r3]	@ movhi
	movw	r0, #1878
	ldrh	r3, [r5, #20]
	str	r1, [sp, #4]
	strh	r3, [r4, r0]	@ movhi
	mov	r0, r3
	str	r3, [sp]
	bl	Ftl_log2
	movw	ip, #1882
	cmp	r6, #1024
	ldr	r3, [sp]
	mov	fp, r0
	movw	r0, #1880
	ldmib	sp, {r1, r2}
	strh	fp, [r4, r0]	@ movhi
	mov	r0, r3, asl #9
	mul	r1, r1, r3
	uxth	r0, r0
	strh	r0, [r4, ip]	@ movhi
	movw	ip, #1884
	mov	r0, r0, lsr #8
	strh	r0, [r4, ip]	@ movhi
	ldrh	ip, [r5, #26]
	movw	r0, #1886
	ldr	r5, .L493
	strh	ip, [r4, r0]	@ movhi
	movwhi	ip, #1866
	mul	r0, r6, r8
	str	r0, [r4, #1820]
	uxtbhi	r0, r6
	strhih	r0, [r2, ip]	@ movhi
	movw	r2, #1866
	ldrh	r2, [r4, r2]
	rsb	r2, r2, r6
	mov	r6, r6, asl #6
	mul	r2, r2, r8
	mul	r2, r3, r2
	mul	r10, r10, r2
	movw	r2, #1868
	ldrh	r0, [r4, r2]
	mov	r0, r0, asl #3
	mov	r10, r10, asr #11
	str	r10, [r4, #1888]
	bl	__aeabi_idiv
	movw	r3, #1892
	mov	r1, r8
	ldr	r10, .L493
	uxth	r0, r0
	cmp	r0, #4
	strhih	r0, [r5, r3]	@ movhi
	movls	r2, #4
	strlsh	r2, [r5, r3]	@ movhi
	cmp	r9, #0
	movwne	r3, #1868
	movne	r2, #640
	strneh	r2, [r4, r3]	@ movhi
	movw	r3, #1868
	ldrh	r3, [r4, r3]
	movw	r2, #1894
	mov	r3, r3, asr fp
	add	fp, fp, #9
	mov	r5, r6, asr fp
	add	r3, r3, #2
	strh	r3, [r4, r2]	@ movhi
	movw	r3, #1896
	strh	r5, [r4, r3]	@ movhi
	uxth	r5, r5
	ldr	r6, .L493
	mul	r3, r8, r5
	add	r5, r5, #8
	str	r3, [r4, #1900]
	movw	r3, #1892
	ldrh	r0, [r4, r3]
	bl	__aeabi_uidiv
	cmp	r8, #1
	uxtah	r0, r5, r0
	strne	r0, [r10, #1800]
	addeq	r5, r0, #4
	streq	r5, [r10, #1800]
	ldr	r3, [r4, #1800]
	uxth	r0, r3
	bl	FtlSysBlkNumInit
	ldr	r3, [r4, #1800]
	ldr	r2, [r4, #1816]
	mov	r0, #2048
	str	r3, [r4, #1904]
	ldrh	r3, [r7]
	mov	r2, r2, asl #2
	mul	r3, r3, r2
	movw	r2, #1880
	ldrh	r5, [r4, r2]
	add	r5, r5, #9
	mov	r5, r3, lsr r5
	movw	r3, #1908
	add	r5, r5, #2
	uxth	r5, r5
	strh	r5, [r4, r3]	@ movhi
	movw	r3, #1878
	ldrh	r7, [r4, r3]
	mov	r1, r7
	bl	__aeabi_idiv
	movw	r1, #1892
	ldrh	r2, [r4, r1]
	movw	r3, #1910
	ldrb	ip, [r4, #140]	@ zero_extendqisi2
	cmp	ip, #0
	strh	r0, [r4, r3]	@ movhi
	mov	r3, #0
	str	r3, [r4, #1912]
	add	r3, r2, #3
	strh	r3, [r4, r1]	@ movhi
	addne	r2, r2, #4
	ldr	r3, [r4, #1900]
	strneh	r2, [r6, r1]	@ movhi
	add	r0, r3, #3
	addne	r3, r3, #5
	str	r0, [r4, #1900]
	bne	.L491
	cmp	r0, #7
	bhi	.L481
	mov	r3, #8
.L491:
	str	r3, [r6, #1900]
.L481:
	movw	r2, #1916
	mov	r3, #0
	strh	r3, [r4, r2]	@ movhi
	movw	r3, #1812
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
	.word	.LANCHOR0+1832
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
	movw	r3, #1918
	ldrh	r3, [r2, r3]
	cmp	r3, #0
	beq	.L501
	movw	r1, #1892
	ldr	r3, [r2, #1920]
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
	ldr	r3, [r2, #1924]
	cmp	r3, #0
	beq	.L507
	stmfd	sp!, {r4, r5, lr}
	.save {r4, r5, lr}
	mov	r5, #36
	ldr	r4, [r2, #1928]
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
	ldr	r0, [r3, #1912]
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
	ldr	r0, [r3, #1912]
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
	ldr	r0, [r3, #1932]
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
	movw	r3, #1870
	ldr	r5, .L524
	mov	r6, r0
	ldrh	r4, [r5, r3]
	mov	r1, r4
	bl	__aeabi_uidiv
	uxth	r2, r0
	smulbb	r3, r2, r4
	add	r1, r5, r2, asl #2
	mov	r4, #1
	add	r5, r5, #1936
	ldr	ip, [r1, #1964]
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
	movw	r3, #1870
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
	ldr	r3, [r5, #1964]
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
	movw	r3, #1870
	ldr	r4, .L540
	mov	r5, #0
	ldrh	r6, [r4, r3]
	movw	r3, #1826
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
	movw	r3, #1828
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
	movw	r3, #1870
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
	movw	r2, #1828
	stmfd	sp!, {r4, r5, r6, lr}
	.save {r4, r5, r6, lr}
	mov	r6, r0
	ldrh	r5, [r3, r2]
	movw	r2, #1870
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
	movw	r3, #1870
	ldr	r4, .L552
	ldrh	r1, [r4, r3]
	bl	__aeabi_uidivmod
	movw	r3, #1828
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
	.word	.LANCHOR0+1996
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
	.word	.LANCHOR0+1996
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
	ldr	r5, .L573+4
	ldrh	r3, [r4, #6]
	cmp	r3, #1024
	ldmeqfd	sp!, {r3, r4, r5, r6, r7, pc}
	cmp	r1, #0
	beq	.L565
	ldr	r3, [r5, #4052]
	cmp	r3, #0
	bne	.L565
	bl	P2V_block_in_plane
	mov	r1, #1
	mov	r3, r7, asl #10
	mov	r2, r1
	mov	r6, r0
	ldr	r0, [r5, #4056]
	str	r3, [r0, #4]
	bl	FlashEraseBlocks
	ldr	r1, [r5, #4060]
	mov	r3, r6, asl #1
	ldrh	r2, [r1, r3]
	add	r2, r2, #1
	strh	r2, [r1, r3]	@ movhi
	ldr	r3, [r5, #4064]
	add	r3, r3, #1
	str	r3, [r5, #4064]
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
	.word	.LANCHOR0+1996
	.word	.LANCHOR0
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
	.word	.LANCHOR0+4068
	.word	.LANCHOR0+1996
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
	ldr	r6, .L601
	ldr	r5, .L601+4
	mov	r7, r6
.L591:
	ldrh	r1, [r5, #6]
	cmp	r1, #0
	beq	.L592
	ldrh	r3, [r5, #2]
	sub	r1, r1, #1
	ldr	r9, [r6, #4052]
	strh	r1, [r5, #6]	@ movhi
	add	r2, r5, r3, asl #1
	cmp	r9, #0
	add	r3, r3, #1
	ubfx	r3, r3, #0, #10
	ldrh	r4, [r2, #8]
	strh	r3, [r5, #2]	@ movhi
	bne	.L593
	mov	r0, r4
	bl	P2V_block_in_plane
	mov	r3, r4, asl #10
	mov	r8, r0
	ldr	r0, [r6, #4056]
	str	r3, [r0, #4]
	ldrb	r3, [r6, #140]	@ zero_extendqisi2
	cmp	r3, #0
	beq	.L594
	mov	r1, r9
	mov	r2, #1
	bl	FlashEraseBlocks
.L594:
	mov	r1, #1
	ldr	r0, [r7, #4056]
	mov	r2, r1
	bl	FlashEraseBlocks
	ldr	r1, [r7, #4060]
	mov	r3, r8, asl #1
	ldrh	r2, [r1, r3]
	add	r2, r2, #1
	strh	r2, [r1, r3]	@ movhi
	ldr	r3, [r7, #4064]
	add	r3, r3, #1
	str	r3, [r7, #4064]
	b	.L593
.L592:
	ldr	r0, .L601+8
	bl	printk
.L595:
	b	.L595
.L593:
	sub	r3, r4, #1
	movw	r2, #65533
	uxth	r3, r3
	cmp	r3, r2
	bls	.L596
	ldr	r3, .L601+4
	mov	r1, r4
	ldr	r0, .L601+12
	ldrh	r2, [r3, #6]
	bl	printk
	b	.L591
.L596:
	mov	r0, r4
	ldmfd	sp!, {r3, r4, r5, r6, r7, r8, r9, pc}
.L602:
	.align	2
.L601:
	.word	.LANCHOR0
	.word	.LANCHOR0+1996
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
	ldr	ip, [r3, #-4068]
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
	movw	r3, #1812
	ldr	r5, .L628
	ldrh	r3, [r5, r3]
	cmp	r3, r0
	bls	.L613
	ldr	r2, .L628+4
	mov	lr, #6
	mul	lr, lr, r0
	mvn	ip, #0
	ldr	r6, [r2, #-4068]
	mov	r7, r2
	add	r1, r6, lr
	strh	ip, [r1, #2]	@ movhi
	strh	ip, [r6, lr]	@ movhi
	ldr	r3, [r2, #-4064]
	cmp	r3, #0
	beq	.L627
	ldr	r8, [r2, #-4060]
	mov	r4, r0, asl #1
	ldrh	r2, [r1, #4]
	ldr	r7, [r7, #-4068]
	ldrh	r9, [r8, r4]
	cmp	r2, #0
	rsb	r10, r7, r3
	mulne	ip, r2, r9
	ldr	r2, .L628+8
	mov	r10, r10, asr #1
	movw	r9, #1812
	ldrh	r9, [r5, r9]
	mul	r10, r2, r10
	str	r9, [sp]
	ldr	r9, [r5, #4060]
	add	r4, r9, r4
	str	r4, [sp, #4]
	uxth	r2, r10
	mov	r4, #0
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
	streq	r1, [r3, #-4056]
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
	ldr	ip, [r2, #-4064]
	cmp	r3, ip
	bne	.L623
	strh	r0, [r3, #2]	@ movhi
.L627:
	str	r1, [r2, #-4064]
	b	.L613
.L623:
	ldrh	ip, [r3, #2]
	ldr	r1, [r2, #-4068]
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
	.word	.LANCHOR2-4048
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
	mul	r5, lr, r0
	mvn	r3, #0
	ldr	r6, [r2, #-4068]
	mov	r7, r2
	add	ip, r6, r5
	strh	r3, [ip, #2]	@ movhi
	strh	r3, [r6, r5]	@ movhi
	ldr	r3, [r2, #-4048]
	cmp	r3, #0
	beq	.L642
	ldr	r2, .L643+4
	ldr	r8, [r7, #-4068]
	ldr	r9, [r2, #4060]
	mov	r2, r0, asl #1
	rsb	r1, r8, r3
	ldrh	r10, [r9, r2]
	mov	r1, r1, asr #1
	ldr	r2, .L643+8
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
	strh	r2, [r6, r5]	@ movhi
	ldr	r1, [r7, #-4048]
	ldr	r2, .L643
	cmp	r3, r1
	bne	.L640
	strh	r0, [r3, #2]	@ movhi
.L642:
	str	ip, [r2, #-4048]
	b	.L635
.L640:
	ldrh	ip, [r3, #2]
	ldr	r1, [r2, #-4068]
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
	.word	.LANCHOR0
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
	.word	.LANCHOR2-4032
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
	ldr	r2, [r4, #-4068]
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
	ldr	r3, [r4, #-4068]
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
	ldr	r4, [r2, #-4068]
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
	ldr	r3, [r2, #-4064]
	cmp	r3, #0
	ldrne	r1, [r2, #-4068]
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
	sub	r2, r3, #4032
	ldrh	r2, [r2, #-8]
	cmp	r2, r0
	beq	.L685
	sub	r2, r3, #3984
	ldrh	r2, [r2, #-8]
	cmp	r2, r0
	beq	.L685
	sub	r2, r3, #3936
	ldrh	r2, [r2, #-8]
	cmp	r2, r0
	beq	.L685
	stmfd	sp!, {r4, lr}
	.save {r4, lr}
	mov	lr, #6
	mul	lr, lr, r0
	ldr	r1, [r3, #-4068]
	ldr	r2, [r3, #-4064]
	add	ip, r1, lr
	cmp	ip, r2
	beq	.L678
	ldr	r4, [r3, #-4060]
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
	.word	.LANCHOR2-4064
	.word	.LANCHOR2-4048
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
	.word	.LANCHOR0+1996
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
	movw	r3, #1910
	stmfd	sp!, {r4, r5, r6, lr}
	.save {r4, r5, r6, lr}
	mov	r1, #0
	ldrh	r2, [r2, r3]
	mov	ip, #12
	ldr	r3, .L714+4
	movw	lr, #65535
	ldr	r3, [r3, #-3896]
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
	.word	.LANCHOR2-3888
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
	sub	r1, r3, #3888
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
	movw	r1, #1812
	ldrh	r0, [r0, r1]
	str	r2, [r3, #-3888]
	ldr	r2, [r3, #-4060]
	add	r0, r2, r0, asl #1
.L718:
	cmp	r2, r0
	beq	.L723
	ldrh	r1, [r2], #2
	cmp	r1, ip
	ldrne	lr, [r3, #-3888]
	addne	r1, r1, lr
	strne	r1, [r3, #-3888]
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
	ldr	r1, [r2, #-3884]
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
	ldr	r3, [r3, #-3884]
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
	movw	r3, #1804
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
	.word	.LANCHOR0+1872
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
	movw	r2, #1804
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
	ldr	r2, .L761
	ldrb	r1, [r4, #7]	@ zero_extendqisi2
	add	r3, r2, #1872
	ldrh	r3, [r3]
	smulbb	r3, r1, r3
	strh	r3, [r4, #4]	@ movhi
	mov	r3, #0
	strb	r3, [r4, #9]
	ldr	r3, [r6, #1796]
	cmp	r3, #0
	beq	.L751
	ldrh	r3, [r4]
	ldr	r2, [r2, #4060]
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
	.word	.LANCHOR0+1832
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
	movw	r2, #1804
	ldr	r3, .L773
	mov	r5, #0
	mov	r6, r0
	mov	r4, r5
	ldrh	r7, [r3, r2]
	add	r3, r3, #1872
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
	ldr	r3, [r3, #-4068]
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
	ldr	r3, .L778
	ldr	r0, [r3, #-4048]
	cmp	r0, #0
	bxeq	lr
	ldr	r3, [r3, #-4068]
	rsb	r0, r3, r0
	ldr	r3, .L778+4
	mov	r0, r0, asr #1
	mul	r0, r3, r0
	ldr	r3, .L778+8
	ldr	r3, [r3, #4060]
	uxth	r0, r0
	mov	r0, r0, asl #1
	ldrh	r0, [r3, r0]
	bx	lr
.L779:
	.align	2
.L778:
	.word	.LANCHOR2
	.word	-1431655765
	.word	.LANCHOR0
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
	ldr	r3, [r1, #-4048]
	cmp	r3, #0
	beq	.L786
	sub	r2, r1, #4032
	stmfd	sp!, {r4, lr}
	.save {r4, lr}
	mov	lr, #6
	ldrh	r2, [r2, #-12]
	movw	r4, #65535
	ldr	r1, [r1, #-4068]
	rsb	r2, r2, r2, asl #3
	rsb	r3, r1, r3
	mov	r2, r2, asr #3
	mov	r3, r3, asr #1
	cmp	r0, r2
	uxthgt	r0, r2
	ldr	r2, .L790+4
	mul	r3, r2, r3
	mov	r2, #0
	uxth	r3, r3
.L783:
	uxth	ip, r2
	cmp	ip, r0
	bcs	.L785
	mul	ip, lr, r3
	add	r2, r2, #1
	ldrh	ip, [r1, ip]
	cmp	ip, r4
	bne	.L787
.L785:
	ldr	r2, .L790+8
	mov	r3, r3, asl #1
	ldr	r2, [r2, #4060]
	ldrh	r0, [r2, r3]
	ldmfd	sp!, {r4, pc}
.L787:
	mov	r3, ip
	b	.L783
.L786:
	mov	r0, r3
	bx	lr
.L791:
	.align	2
.L790:
	.word	.LANCHOR2
	.word	-1431655765
	.word	.LANCHOR0
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
	ldr	r7, .L804
	.pad #52
	sub	sp, sp, #52
	ldr	r6, .L804+4
	add	r5, r8, #12
	ldr	r1, .L804+8
	ldr	r3, [r7]
	str	r3, [sp, #44]
	bl	strcpy
	mov	r0, r5
	ldr	r1, .L804+12
	ldr	r2, [r6, #112]
	bl	sprintf
	ldr	r1, .L804+16
	ldr	r2, [r6, #1888]
	add	r5, r5, r0
	mov	r0, r5
	bl	sprintf
	ldr	r3, .L804+20
	ldr	r3, [r3, #504]
	cmp	r3, #1
	add	r4, r5, r0
	bne	.L798
	add	r0, sp, #28
	add	r1, sp, #32
	add	r2, sp, #36
	add	r3, sp, #40
	bl	NandcGetTimeCfg
	mov	r0, r4
	ldr	r1, .L804+24
	ldr	r9, .L804+28
	ldr	r10, .L804+32
	sub	fp, r9, #4032
	ldr	r3, [sp, #36]
	ldr	r2, [sp, #28]
	str	r3, [sp]
	ldr	r3, [sp, #40]
	str	r3, [sp, #4]
	ldr	r3, [sp, #32]
	bl	sprintf
	ldr	r1, .L804+36
	add	r4, r4, r0
	add	r5, r4, #10
	mov	r0, r4
	add	r4, r6, #1936
	bl	strcpy
	mov	r0, r5
	ldr	r1, .L804+40
	ldr	r2, [r6, #1932]
	bl	sprintf
	ldr	r1, .L804+44
	ldr	r2, [r9, #-3888]
	add	r5, r5, r0
	mov	r0, r5
	bl	sprintf
	ldr	r1, .L804+48
	ldr	r2, [r9, #-3880]
	add	r5, r5, r0
	mov	r0, r5
	bl	sprintf
	ldr	r1, .L804+52
	ldr	r2, [r9, #-3876]
	add	r5, r5, r0
	mov	r0, r5
	bl	sprintf
	ldr	r1, .L804+56
	ldr	r2, [r9, #-3872]
	add	r5, r5, r0
	mov	r0, r5
	bl	sprintf
	ldr	r1, .L804+60
	ldr	r2, [r9, #-3868]
	add	r5, r5, r0
	mov	r0, r5
	bl	sprintf
	ldr	r1, .L804+64
	ldr	r2, [r9, #-3864]
	add	r5, r5, r0
	mov	r0, r5
	bl	sprintf
	ldr	r1, .L804+68
	ldr	r2, [r9, #-3860]
	add	r5, r5, r0
	mov	r0, r5
	bl	sprintf
	ldr	r2, [r9, #-3856]
	ldr	r1, .L804+72
	mov	r2, r2, lsr #11
	add	r5, r5, r0
	mov	r0, r5
	bl	sprintf
	ldr	r2, [r9, #-3852]
	ldr	r1, .L804+76
	mov	r2, r2, lsr #11
	add	r5, r5, r0
	mov	r0, r5
	bl	sprintf
	ldr	r1, .L804+80
	ldr	r2, [r9, #-3848]
	add	r5, r5, r0
	mov	r0, r5
	bl	sprintf
	ldr	r1, .L804+84
	ldr	r2, [r9, #-3844]
	add	r5, r5, r0
	mov	r0, r5
	bl	sprintf
	add	r5, r5, r0
	bl	FtlBbtCalcTotleCnt
	ldrh	r2, [r4, #6]
	ldr	r1, .L804+88
	mov	r3, r0
	mov	r0, r5
	bl	sprintf
	ldr	r1, .L804+92
	ldrh	r2, [fp, #-12]
	add	r5, r5, r0
	mov	r0, r5
	bl	sprintf
	ldr	r1, .L804+96
	ldr	r2, [r9, #-3840]
	add	r5, r5, r0
	mov	r0, r5
	bl	sprintf
	ldr	r1, .L804+100
	ldr	r2, [r9, #-3836]
	add	r5, r5, r0
	mov	r0, r5
	bl	sprintf
	ldr	r1, .L804+104
	ldr	r2, [r9, #-3832]
	add	r5, r5, r0
	mov	r0, r5
	bl	sprintf
	ldr	r1, .L804+108
	ldr	r2, [r6, #4064]
	add	r5, r5, r0
	mov	r0, r5
	bl	sprintf
	ldr	r1, .L804+112
	ldr	r2, [r9, #-3828]
	add	r5, r5, r0
	mov	r0, r5
	bl	sprintf
	ldr	r1, .L804+116
	ldr	r2, [r9, #-3824]
	add	r5, r5, r0
	mov	r0, r5
	bl	sprintf
	ldrh	r2, [r10, #30]
	ldr	r1, .L804+120
	add	r5, r5, r0
	mov	r0, r5
	bl	sprintf
	ldrh	r2, [r10, #28]
	ldr	r1, .L804+124
	sub	r10, r9, #3808
	add	r5, r5, r0
	mov	r0, r5
	bl	sprintf
	ldr	r1, .L804+128
	ldr	r2, [r6, #1912]
	add	r5, r5, r0
	mov	r0, r5
	bl	sprintf
	ldr	r1, .L804+132
	ldr	r2, [r6, #1904]
	add	r5, r5, r0
	mov	r0, r5
	bl	sprintf
	ldr	r1, .L804+136
	ldr	r2, [r6, #1800]
	add	r5, r5, r0
	mov	r0, r5
	bl	sprintf
	ldrh	r2, [r4, #66]
	ldr	r1, .L804+140
	add	r5, r5, r0
	mov	r0, r5
	bl	sprintf
	movw	r3, #1812
	ldrh	r2, [r6, r3]
	ldr	r1, .L804+144
	add	r5, r5, r0
	mov	r0, r5
	bl	sprintf
	ldrh	r2, [r10, #-12]
	ldr	r1, .L804+148
	add	r5, r5, r0
	mov	r0, r5
	bl	sprintf
	ldr	r1, .L804+152
	ldr	r2, [r6, #1816]
	add	r5, r5, r0
	mov	r0, r5
	bl	sprintf
	ldrh	r2, [r10, #-8]
	ldr	r1, .L804+156
	add	r5, r5, r0
	mov	r0, r5
	bl	sprintf
	ldrh	r2, [r4]
	ldr	r1, .L804+160
	sub	r4, r9, #3984
	add	r5, r5, r0
	mov	r0, r5
	bl	sprintf
	ldrh	r2, [r10, #-230]
	ldr	r1, .L804+164
	add	r5, r5, r0
	mov	r0, r5
	bl	sprintf
	ldr	r1, .L804+168
	ldrb	r2, [r9, #-4034]	@ zero_extendqisi2
	add	r5, r5, r0
	mov	r0, r5
	bl	sprintf
	ldr	r1, .L804+172
	ldrh	r2, [fp, #-8]
	add	r5, r5, r0
	mov	r0, r5
	bl	sprintf
	ldr	r1, .L804+176
	ldrb	r2, [r9, #-4032]	@ zero_extendqisi2
	add	r5, r5, r0
	mov	r0, r5
	bl	sprintf
	ldrh	r2, [r10, #-228]
	ldr	r1, .L804+180
	add	r5, r5, r0
	mov	r0, r5
	bl	sprintf
	ldrh	r3, [fp, #-8]
	ldr	r2, [r9, #-4060]
	ldr	r1, .L804+184
	mov	r3, r3, asl #1
	ldrh	r2, [r2, r3]
	add	r5, r5, r0
	mov	r0, r5
	bl	sprintf
	ldrh	r2, [r10, #-182]
	ldr	r1, .L804+188
	add	r5, r5, r0
	mov	r0, r5
	bl	sprintf
	ldr	r1, .L804+192
	ldrb	r2, [r9, #-3986]	@ zero_extendqisi2
	add	r5, r5, r0
	mov	r0, r5
	bl	sprintf
	ldrh	r2, [r4, #-8]
	ldr	r1, .L804+196
	add	r5, r5, r0
	mov	r0, r5
	bl	sprintf
	ldr	r1, .L804+200
	ldrb	r2, [r9, #-3984]	@ zero_extendqisi2
	add	r5, r5, r0
	mov	r0, r5
	bl	sprintf
	ldrh	r2, [r10, #-180]
	ldr	r1, .L804+204
	sub	r10, r9, #3936
	add	r5, r5, r0
	mov	r0, r5
	bl	sprintf
	ldrh	r3, [r4, #-8]
	ldr	r2, [r9, #-4060]
	ldr	r1, .L804+208
	mov	r3, r3, asl #1
	ldrh	r2, [r2, r3]
	add	r5, r5, r0
	mov	r0, r5
	bl	sprintf
	ldrh	r2, [r4, #42]
	ldr	r1, .L804+212
	add	r5, r5, r0
	mov	r0, r5
	bl	sprintf
	ldr	r1, .L804+216
	ldrb	r2, [r9, #-3938]	@ zero_extendqisi2
	add	r5, r5, r0
	mov	r0, r5
	bl	sprintf
	ldrh	r2, [r10, #-8]
	ldr	r1, .L804+220
	sub	r10, r9, #3792
	add	r5, r5, r0
	mov	r0, r5
	bl	sprintf
	ldr	r1, .L804+224
	ldrb	r2, [r9, #-3936]	@ zero_extendqisi2
	add	r5, r5, r0
	mov	r0, r5
	bl	sprintf
	ldrh	r2, [r4, #44]
	ldr	r1, .L804+228
	add	r5, r5, r0
	mov	r0, r5
	bl	sprintf
	ldrh	r2, [r4, #186]
	ldr	r1, .L804+232
	add	r5, r5, r0
	mov	r0, r5
	bl	sprintf
	ldr	r1, .L804+236
	ldrb	r2, [r9, #-3794]	@ zero_extendqisi2
	add	r5, r5, r0
	mov	r0, r5
	bl	sprintf
	ldr	r1, .L804+240
	ldrh	r2, [r10, #-8]
	add	r5, r5, r0
	mov	r0, r5
	bl	sprintf
	ldr	r1, .L804+244
	ldrb	r2, [r9, #-3792]	@ zero_extendqisi2
	add	r5, r5, r0
	mov	r0, r5
	bl	sprintf
	ldrh	r2, [r4, #188]
	ldr	r1, .L804+248
	add	r5, r5, r0
	mov	r0, r5
	bl	sprintf
	ldr	r1, [r9, #-3664]
	ldr	r2, [r9, #-3752]
	ldr	r3, [r6, #1796]
	orr	r2, r3, r2, asl #8
	ldr	r3, [r9, #-3668]
	str	r1, [sp]
	add	r5, r5, r0
	ldr	r1, [r9, #-3672]
	mov	r0, r5
	str	r1, [sp, #4]
	ldr	r1, .L804+252
	bl	sprintf
	ldr	r1, .L804+256
	ldr	r2, [r9, #-3676]
	add	r4, r5, r0
	sub	r5, r9, #3232
	mov	r0, r4
	bl	sprintf
	ldr	r1, .L804+260
	ldr	r2, [r9, #-3652]
	add	r4, r4, r0
	mov	r0, r4
	bl	sprintf
	ldr	r1, .L804+264
	ldrh	r2, [r5, #-4]
	add	r4, r4, r0
	mov	r0, r4
	bl	sprintf
	ldr	r1, .L804+268
	ldrh	r2, [r5, #-2]
	add	r4, r4, r0
	mov	r0, r4
	bl	sprintf
	ldr	r1, .L804+272
	ldr	r2, [r9, #-3232]
	add	r4, r4, r0
	mov	r0, r4
	bl	sprintf
	sub	r3, r9, #3216
	ldr	r1, .L804+276
	ldrh	r2, [r3, #-12]
	add	r4, r4, r0
	mov	r0, r4
	bl	sprintf
	add	r4, r4, r0
	bl	GetFreeBlockMinEraseCount
	ldr	r1, .L804+280
	mov	r2, r0
	mov	r0, r4
	bl	sprintf
	add	r4, r4, r0
	ldrh	r0, [fp, #-12]
	bl	GetFreeBlockMaxEraseCount
	ldr	r1, .L804+284
	mov	r2, r0
	mov	r0, r4
	bl	sprintf
	ldrh	r3, [r10, #-8]
	movw	r2, #65535
	cmp	r3, r2
	add	r4, r4, r0
	beq	.L795
	ldr	r2, [r9, #-4060]
	mov	r3, r3, asl #1
	mov	r0, r4
	ldr	r1, .L804+288
	ldrh	r2, [r2, r3]
	bl	sprintf
	add	r4, r4, r0
.L795:
	mov	r0, #0
	mov	r5, #0
	bl	List_get_gc_head_node
	mov	fp, #6
	movw	ip, #65535
	uxth	r3, r0
.L797:
	cmp	r3, ip
	str	ip, [sp, #20]
	beq	.L796
	ldr	r2, [r9, #-4060]
	mov	r1, r3, asl #1
	mul	r10, fp, r3
	mov	r0, r4
	ldrh	r2, [r2, r1]
	str	r2, [sp]
	ldr	r2, [r9, #-4068]
	add	r2, r2, r10
	ldrh	r2, [r2, #4]
	str	r2, [sp, #4]
	ldr	r2, [r6, #4060]
	ldrh	r2, [r2, r1]
	ldr	r1, .L804+292
	str	r2, [sp, #8]
	mov	r2, r5
	bl	sprintf
	add	r5, r5, #1
	ldr	r3, [r9, #-4068]
	cmp	r5, #16
	ldrh	r3, [r3, r10]
	add	r4, r4, r0
	ldr	ip, [sp, #20]
	bne	.L797
.L796:
	ldr	r3, [r9, #-4048]
	mov	r10, #0
	ldr	r5, [r9, #-4068]
	movw	ip, #65535
	rsb	r5, r5, r3
	ldr	r3, .L804+296
	mov	r5, r5, asr #1
	mul	r3, r3, r5
	mov	r5, #6
	uxth	r3, r3
.L799:
	cmp	r3, ip
	str	ip, [sp, #20]
	beq	.L798
	mul	fp, r5, r3
	ldr	r2, [r9, #-4068]
	ldr	r1, [r6, #4060]
	mov	r0, r4
	add	r2, r2, fp
	ldrh	r2, [r2, #4]
	str	r2, [sp]
	mov	r2, r3, asl #1
	ldrh	r2, [r1, r2]
	ldr	r1, .L804+300
	str	r2, [sp, #4]
	mov	r2, r10
	bl	sprintf
	add	r10, r10, #1
	ldr	r3, [r9, #-4068]
	cmp	r10, #4
	ldrh	r3, [r3, fp]
	add	r4, r4, r0
	ldr	ip, [sp, #20]
	bne	.L799
.L798:
	ldr	r2, [sp, #44]
	rsb	r0, r8, r4
	ldr	r3, [r7]
	cmp	r2, r3
	beq	.L800
	bl	__stack_chk_fail
.L800:
	add	sp, sp, #52
	@ sp needed
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L805:
	.align	2
.L804:
	.word	__stack_chk_guard
	.word	.LANCHOR0
	.word	.LC6
	.word	.LC7
	.word	.LC8
	.word	.LANCHOR1
	.word	.LC9
	.word	.LANCHOR2
	.word	.LANCHOR0+4068
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
	ldr	r1, .L808
	ldr	r2, .L808+4
	bl	sprintf
	add	r4, r5, r0
	mov	r0, r4
	bl	FtlPrintInfo2buf
	add	r0, r4, r0
	rsb	r0, r5, r0
	ldmfd	sp!, {r3, r4, r5, pc}
.L809:
	.align	2
.L808:
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
	ldr	r4, .L840
	ldr	r2, [r4, #-3832]
	ldr	r3, [r4, #-3824]
	cmp	r2, r3
	bcs	.L811
	ldr	r2, .L840+4
	movw	r1, #1812
	mov	r3, #0
	str	r3, [r4, #-3840]
	ldr	r0, [r2, #4060]
	mov	r5, r2
	ldrh	r1, [r2, r1]
	sub	r0, r0, #2
.L812:
	cmp	r3, r1
	bcs	.L839
	ldrh	r2, [r0, #2]!
	add	r3, r3, #1
	ldr	ip, [r4, #-3840]
	add	r2, r2, ip
	str	r2, [r4, #-3840]
	b	.L812
.L839:
	ldr	r6, [r4, #-3840]
	mov	r0, r6
	bl	__aeabi_uidiv
	movw	r3, #1864
	ldrh	r1, [r5, r3]
	str	r0, [r4, #-3832]
	ldr	r0, [r4, #-3836]
	rsb	r0, r0, r6
	bl	__aeabi_uidiv
	str	r0, [r4, #-3840]
	b	.L814
.L811:
	ldr	r3, [r4, #-3828]
	cmp	r2, r3
	bls	.L814
	ldr	lr, .L840+4
	add	r3, r3, #1
	ldr	ip, .L840+8
	str	r3, [r4, #-3828]
	mov	r3, #0
.L816:
	ldrh	r2, [ip]
	cmp	r3, r2
	bcs	.L814
	ldr	r0, [lr, #4060]
	mov	r1, r3, asl #1
	add	r3, r3, #1
	ldrh	r2, [r0, r1]
	add	r2, r2, #1
	strh	r2, [r0, r1]	@ movhi
	b	.L816
.L814:
	ldr	r6, [r4, #-3824]
	ldr	r8, [r4, #-3832]
	add	r3, r6, #256
	cmp	r3, r8
	bls	.L819
	ldr	r2, .L840
	add	r3, r6, #768
	ldr	r2, [r2, #-3828]
	cmp	r3, r2
	bls	.L819
	ldr	r3, .L840+4
	cmp	r6, #40
	ldr	r2, [r3, #1796]
	movls	r3, #0
	movhi	r3, #1
	cmp	r2, #0
	orreq	r3, r3, #1
	cmp	r3, #0
	beq	.L819
.L821:
	movw	r0, #65535
	b	.L820
.L819:
	ldr	r5, .L840
	sub	r3, r5, #4032
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
	bne	.L821
	ldr	r3, [r5, #-4064]
	cmp	r3, #0
	beq	.L821
	ldr	r2, .L840+4
	movw	r0, #1812
	ldr	ip, .L840+12
	movw	r7, #65535
	mov	lr, #6
	ldrh	r0, [r2, r0]
	ldr	r10, [r2, #4060]
	str	r0, [sp, #20]
	ldr	r0, [r5, #-4068]
	mov	r5, r7
.L822:
	ldrh	r2, [r3]
	movw	fp, #65535
	cmp	r2, fp
	beq	.L824
	add	r1, r1, #1
	ldr	fp, [sp, #20]
	uxth	r1, r1
	cmp	r1, fp
	bhi	.L821
	ldrh	fp, [r3, #4]
	cmp	fp, #0
	beq	.L823
	rsb	r3, r0, r3
	mov	r3, r3, asr #1
	mul	r3, ip, r3
	uxth	r3, r3
	mov	fp, r3, asl #1
	ldrh	fp, [r10, fp]
	cmp	fp, r6
	bls	.L828
	cmp	fp, r7
	movcc	r7, fp
	movcc	r5, r3
.L823:
	mla	r3, lr, r2, r0
	b	.L822
.L828:
	mov	r5, r3
.L824:
	movw	r3, #65535
	cmp	r5, r3
	beq	.L821
	mov	r3, r5, asl #1
	ldrh	fp, [r10, r3]
	cmp	fp, r6
	bls	.L826
	str	r3, [sp, #20]
	bl	GetFreeBlockMinEraseCount
	ldr	r3, [sp, #20]
	cmp	r0, r6
	strhi	r7, [r4, #-3824]
.L826:
	cmp	fp, r8
	bcs	.L821
	add	r2, fp, #128
	cmp	r9, r2
	ble	.L821
	add	r2, fp, #256
	cmp	r2, r8
	bcc	.L827
	ldr	r2, [r4, #-3828]
	add	fp, fp, #768
	cmp	fp, r2
	bcs	.L821
.L827:
	ldr	r2, [r4, #-4060]
	mov	r1, r5
	ldr	r0, .L840+16
	ldrh	r2, [r2, r3]
	str	r2, [sp]
	mov	r2, r8
	ldrh	r3, [r10, r3]
	stmib	sp, {r3, r9}
	ldr	r3, [r4, #-3828]
	bl	printk
	mov	r0, r5
	mov	r3, #1
	str	r3, [r4, #-3224]
.L820:
	add	sp, sp, #28
	@ sp needed
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L841:
	.align	2
.L840:
	.word	.LANCHOR2
	.word	.LANCHOR0
	.word	.LANCHOR0+1812
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
	beq	.L843
	ldr	r2, .L845
	mov	r3, r0, asl #1
	mov	r1, #0
	ldr	r2, [r2, #-4060]
	strh	r1, [r2, r3]	@ movhi
	bl	INSERT_FREE_LIST
.L843:
	mov	r0, #0
	ldmfd	sp!, {r3, pc}
.L846:
	.align	2
.L845:
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
	ldr	ip, .L855
	mov	r3, #0
	ldr	r1, .L855+4
	stmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, lr}
	.save {r4, r5, r6, r7, r8, r9, r10, lr}
	add	r7, ip, #78
	mov	r5, #12
	mov	r6, #1
	mov	r8, #36
	str	r3, [r1, #-3220]
.L848:
	ldrh	r2, [ip]
	add	r4, r3, #1
	uxth	r3, r3
	cmp	r3, r2
	bcs	.L853
	mul	r0, r5, r3
	ldr	lr, [r1, #-3216]
	add	r2, lr, r0
	str	r6, [r2, #8]
	ldrh	r2, [r7]
	mul	r2, r2, r3
	add	r9, r2, #3
	cmp	r2, #0
	movlt	r2, r9
	ldr	r9, [r1, #-3212]
	bic	r2, r2, #3
	add	r2, r9, r2
	str	r2, [lr, r0]
	ldr	r2, .L855+8
	ldr	r9, [r1, #-3216]
	ldrh	r2, [r2]
	add	lr, r9, r0
	mul	r2, r2, r3
	add	r10, r2, #3
	cmp	r2, #0
	movlt	r2, r10
	ldr	r10, [r1, #-3208]
	bic	r2, r2, #3
	add	r2, r10, r2
	str	r2, [lr, #4]
	ldr	r2, [r1, #-3204]
	mla	r3, r8, r3, r2
	ldr	r2, [r9, r0]
	str	r2, [r3, #8]
	ldr	r2, [lr, #4]
	str	r2, [r3, #12]
	mov	r3, r4
	b	.L848
.L853:
	ldr	r0, .L855+4
	mov	ip, #12
	ldr	r4, .L855+12
	mov	lr, #0
.L850:
	ldr	r3, [r1, #-3200]
	cmp	r2, r3
	bcs	.L854
	mul	r5, ip, r2
	ldr	r7, [r0, #-3216]
	add	r3, r7, r5
	str	lr, [r3, #8]
	ldrh	r3, [r4]
	mul	r3, r3, r2
	add	r6, r3, #3
	cmp	r3, #0
	movlt	r3, r6
	ldr	r6, [r0, #-3212]
	bic	r3, r3, #3
	add	r3, r6, r3
	str	r3, [r7, r5]
	ldr	r3, .L855+8
	ldr	r6, [r0, #-3216]
	ldrh	r3, [r3]
	add	r5, r6, r5
	mul	r3, r3, r2
	add	r2, r2, #1
	uxth	r2, r2
	add	r6, r3, #3
	cmp	r3, #0
	movlt	r3, r6
	ldr	r6, [r0, #-3208]
	bic	r3, r3, #3
	add	r3, r6, r3
	str	r3, [r5, #4]
	b	.L850
.L854:
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, pc}
.L856:
	.align	2
.L855:
	.word	.LANCHOR0+1804
	.word	.LANCHOR2
	.word	.LANCHOR0+1884
	.word	.LANCHOR0+1882
	.fnend
	.size	FtlGcBufInit, .-FtlGcBufInit
	.align	2
	.global	FtlGcBufFree
	.type	FtlGcBufFree, %function
FtlGcBufFree:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	ldr	r3, .L865
	stmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, lr}
	.save {r4, r5, r6, r7, r8, r9, r10, lr}
	mov	lr, #0
	ldr	r6, [r3, #-3200]
	mov	r5, #36
	ldr	r4, [r3, #-3216]
	mov	r7, lr
	mov	r8, #12
.L858:
	uxth	ip, lr
	cmp	ip, r1
	ldmcsfd	sp!, {r4, r5, r6, r7, r8, r9, r10, pc}
	mla	ip, r5, ip, r0
	mov	r2, #0
.L859:
	uxth	r3, r2
	cmp	r3, r6
	bcs	.L860
	mul	r3, r8, r3
	add	r2, r2, #1
	add	r9, r4, r3
	ldr	r10, [r4, r3]
	ldr	r3, [ip, #8]
	cmp	r10, r3
	bne	.L859
	str	r7, [r9, #8]
.L860:
	add	lr, lr, #1
	b	.L858
.L866:
	.align	2
.L865:
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
	ldr	r3, .L876
	mov	ip, #0
	stmfd	sp!, {r4, r5, r6, r7, r8, r9, lr}
	.save {r4, r5, r6, r7, r8, r9, lr}
	mov	r6, #12
	ldr	r4, [r3, #-3200]
	mov	r7, #1
	ldr	r5, [r3, #-3216]
	mov	r8, #36
.L868:
	uxth	r2, ip
	cmp	r2, r1
	bcs	.L875
	mov	lr, #0
.L869:
	uxth	r3, lr
	cmp	r3, r4
	bcs	.L870
	mla	r3, r6, r3, r5
	add	lr, lr, #1
	ldr	r9, [r3, #8]
	cmp	r9, #0
	bne	.L869
	mla	r2, r8, r2, r0
	ldr	lr, [r3]
	str	r7, [r3, #8]
	str	lr, [r2, #8]
	ldr	r3, [r3, #4]
	str	r3, [r2, #12]
.L870:
	add	ip, ip, #1
	b	.L868
.L875:
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, pc}
.L877:
	.align	2
.L876:
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
	ldr	r2, .L884
	sub	r2, r2, #3184
	ldr	r3, [r2, #-12]
	ldrh	r2, [r2, #-8]
	add	r2, r3, r2, asl #1
.L879:
	cmp	r3, r2
	beq	.L883
	ldrh	r1, [r3], #2
	cmp	r1, r0
	bne	.L879
	mov	r0, #1
	bx	lr
.L883:
	mov	r0, #0
	bx	lr
.L885:
	.align	2
.L884:
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
	ldr	r4, .L894
	ubfx	r0, r0, #10, #16
	mov	r6, r1
	mov	r7, r2
	bl	P2V_block_in_plane
	sub	r3, r4, #3184
	ldr	r2, [r4, #-3196]
	mov	lr, #0
	ldrh	r1, [r3, #-8]
	sub	r8, r2, #2
.L887:
	uxth	ip, lr
	cmp	ip, r1
	bcs	.L891
	ldrh	r9, [r8, #2]!
	add	lr, lr, #1
	cmp	r9, r0
	bne	.L887
.L891:
	cmp	ip, r1
	moveq	ip, ip, asl #1
	streqh	r0, [r2, ip]	@ movhi
	ldreqh	ip, [r3, #-8]
	addeq	ip, ip, #1
	streqh	ip, [r3, #-8]	@ movhi
.L889:
	ldrh	r2, [r3]
	mov	ip, #12
	mul	ip, ip, r2
	ldr	r2, [r4, #-3188]
	add	r1, r2, ip
	stmib	r1, {r6, r7}
	str	r5, [r2, ip]
	ldrh	r2, [r3]
	add	r2, r2, #1
	strh	r2, [r3]	@ movhi
	ldmfd	sp!, {r3, r4, r5, r6, r7, r8, r9, pc}
.L895:
	.align	2
.L894:
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
	ldr	r4, .L904
	ldrh	r3, [r4, #-14]
	cmp	r3, r0
	beq	.L898
	ldrh	r3, [r4, #-12]
	cmp	r3, r0
	beq	.L898
	ldrh	r3, [r4, #-10]
	cmp	r3, r0
	beq	.L898
	ldrh	r3, [r4, #-8]
	cmp	r3, r0
	beq	.L898
	ldr	r0, .L904+4
	mov	r1, r5
	bl	printk
	ldrh	r2, [r4, #-14]
	movw	r3, #65535
	cmp	r2, r3
	streqh	r5, [r4, #-14]	@ movhi
	beq	.L898
	ldrh	r2, [r4, #-12]
	cmp	r2, r3
	streqh	r5, [r4, #-12]	@ movhi
	beq	.L898
	ldrh	r2, [r4, #-10]
	cmp	r2, r3
	streqh	r5, [r4, #-10]	@ movhi
	beq	.L898
	ldrh	r2, [r4, #-8]
	cmp	r2, r3
	streqh	r5, [r4, #-8]	@ movhi
.L898:
	mov	r0, #0
	ldmfd	sp!, {r3, r4, r5, pc}
.L905:
	.align	2
.L904:
	.word	.LANCHOR2-3168
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
	ldr	r4, .L917
	ldrh	r3, [r4, #-14]
	cmp	r3, r0
	beq	.L914
	ldrh	r3, [r4, #-12]
	cmp	r3, r0
	beq	.L914
	ldrh	r3, [r4, #-10]
	cmp	r3, r0
	beq	.L914
	ldrh	r3, [r4, #-8]
	cmp	r3, r0
	beq	.L914
	ldr	r0, .L917+4
	mov	r1, r5
	bl	printk
	ldrh	r2, [r4, #-14]
	movw	r3, #65535
	cmp	r2, r3
	streqh	r5, [r4, #-14]	@ movhi
	beq	.L914
	ldrh	r2, [r4, #-12]
	cmp	r2, r3
	streqh	r5, [r4, #-12]	@ movhi
	beq	.L914
	ldrh	r2, [r4, #-10]
	cmp	r2, r3
	streqh	r5, [r4, #-10]	@ movhi
	beq	.L914
	ldrh	r2, [r4, #-8]
	cmp	r2, r3
	bne	.L915
	strh	r5, [r4, #-8]	@ movhi
.L914:
	mov	r0, #0
	ldmfd	sp!, {r3, r4, r5, pc}
.L915:
	mvn	r0, #0
	ldmfd	sp!, {r3, r4, r5, pc}
.L918:
	.align	2
.L917:
	.word	.LANCHOR2-3168
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
	stmfd	sp!, {r4, r5, r6, lr}
	.save {r4, r5, r6, lr}
	mov	r5, r0
	bl	P2V_block_in_plane
	ldr	r6, .L929
	mov	r2, r5
	ldrh	r1, [r6, #-6]
	mov	r4, r0
	ldr	r0, .L929+4
	bl	printk
	mov	r0, r4
	bl	FtlGcRefreshBlock
	ldr	r3, .L929+8
	ldr	r2, [r3, #1796]
	cmp	r2, #0
	beq	.L920
	ldr	r2, [r3, #4060]
	mov	r4, r4, asl #1
	ldrh	r3, [r2, r4]
	cmp	r3, #39
	subhi	r3, r3, #40
	strhih	r3, [r2, r4]	@ movhi
.L920:
	ldrh	r3, [r6, #-6]
	mov	r2, #0
	ldr	r0, .L929+12
.L921:
	uxth	r1, r2
	cmp	r1, r3
	bcs	.L928
	add	r2, r2, #1
	add	r1, r0, r2, asl #1
	ldrh	r1, [r1, #-2]
	cmp	r1, r5
	bne	.L921
	b	.L922
.L928:
	cmp	r3, #15
	addls	r2, r3, #1
	strlsh	r2, [r6, #-6]	@ movhi
	ldrls	r2, .L929+12
	movls	r3, r3, asl #1
	strlsh	r5, [r2, r3]	@ movhi
.L922:
	mov	r0, #0
	ldmfd	sp!, {r4, r5, r6, pc}
.L930:
	.align	2
.L929:
	.word	.LANCHOR2-3168
	.word	.LC80
	.word	.LANCHOR0
	.word	.LANCHOR2-3172
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
	ldr	r4, .L938
	ldrh	r3, [r4, #-6]
	cmp	r3, #0
	beq	.L932
	ldrh	r1, [r4, #-14]
	movw	r2, #65535
	cmp	r1, r2
	bne	.L932
	add	r4, r4, #32
	ldrh	r2, [r4, #-2]
	cmp	r2, r3
	ldr	r2, .L938+4
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
.L932:
	mov	r0, #0
	ldmfd	sp!, {r4, pc}
.L939:
	.align	2
.L938:
	.word	.LANCHOR2-3168
	.word	.LANCHOR2-3172
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
	bne	.L942
	ldr	r3, .L972
.L943:
	mov	r2, r0, asl #1
	strh	r0, [r2, r3]	@ movhi
	add	r0, r0, #1
	cmp	r0, #256
	bne	.L943
.L947:
	mov	r1, #255
	ldr	r0, .L972+4
	mov	r2, #1024
	uxth	r4, r4
	bl	ftl_memset
	mov	r3, #0
	ldr	r1, .L972
	ldr	r0, .L972+4
	b	.L944
.L942:
	cmp	r0, #1
	bne	.L945
	ldr	ip, .L972
	mov	r3, #0
.L946:
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
	bne	.L946
	b	.L947
.L945:
	cmp	r0, #2
	bne	.L948
	ldr	r1, .L972
	mov	r3, #0
.L949:
	uxth	r2, r3
	mov	r0, r3, asl #1
	cmp	r2, #1
	add	r3, r3, #1
	mov	ip, r2, asl #1
	subhi	r2, ip, #1
	cmp	r3, #256
	strh	r2, [r0, r1]	@ movhi
	bne	.L949
	b	.L947
.L948:
	cmp	r0, #3
	bne	.L950
	ldr	ip, .L972
	mov	r3, #0
.L951:
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
	bne	.L951
	b	.L947
.L950:
	cmp	r0, #4
	mov	r3, #0
	bne	.L952
	ldr	r2, .L972+8
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
.L953:
	tst	r3, #1
	movne	r1, #7
	moveq	r1, #6
	rsb	r1, r1, r3, asl #1
	add	r3, r3, #1
	strh	r1, [r2, #2]!	@ movhi
	uxth	r3, r3
	cmp	r3, #256
	bne	.L953
	b	.L947
.L952:
	cmp	r0, #5
	bne	.L954
	ldr	r2, .L972
.L955:
	mov	r1, r3, asl #1
	strh	r3, [r1, r2]	@ movhi
	add	r3, r3, #1
	cmp	r3, #16
	bne	.L955
	ldr	r2, .L972+12
.L956:
	strh	r3, [r2, #2]!	@ movhi
	add	r3, r3, #2
	uxth	r3, r3
	cmp	r3, #496
	bne	.L956
	b	.L947
.L954:
	cmp	r0, #6
	bne	.L957
	ldr	r1, .L972+16
.L958:
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
	bne	.L958
	b	.L947
.L957:
	cmp	r0, #9
	bne	.L947
	ldr	r2, .L972+8
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
	b	.L947
.L944:
	uxth	r2, r3
	cmp	r2, r4
	bcs	.L971
	mov	r2, r3, asl #1
	add	r3, r3, #1
	ldrh	r2, [r2, r1]
	mov	ip, r2, asl #1
	strh	r2, [r0, ip]	@ movhi
	b	.L944
.L971:
	ldmfd	sp!, {r4, pc}
.L973:
	.align	2
.L972:
	.word	.LANCHOR0+144
	.word	.LANCHOR2-3136
	.word	.LANCHOR0
	.word	.LANCHOR0+174
	.word	.LANCHOR0+142
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
	ldr	r3, .L989
	ldr	r4, .L989+4
	ldr	r9, .L989+8
	ldrh	r0, [r3, #10]
	strb	r6, [r4, #1717]
	mov	r7, r4
	strb	r6, [r9, #-2112]
	bl	FlashBlockAlignInit
	mov	r1, r6
	mov	r2, #8
	ldr	r0, .L989+12
	bl	ftl_memset
	mov	r1, r6
	mov	r2, #32
	add	r0, r4, #656
	bl	ftl_memset
	ldr	r0, .L989+16
	mov	r1, r6
	mov	r2, #128
	bl	ftl_memset
	ldr	r5, [r4, #692]
	ldr	fp, .L989+20
	add	r8, r5, #1
.L976:
	mov	r0, r8
	add	r1, fp, r6, asl #3
	ldrb	r2, [r5]	@ zero_extendqisi2
	bl	FlashMemCmp8
	ldr	r10, .L989+20
	cmp	r0, #0
	bne	.L975
	ldrb	r3, [r7, #1717]	@ zero_extendqisi2
	add	r2, r7, r3, asl #2
	str	r0, [r2, #656]
	add	r2, r3, #1
	add	r3, r7, r3
	strb	r2, [r7, #1717]
	strb	r6, [r3, #1720]
.L975:
	add	r6, r6, #1
	cmp	r6, #4
	bne	.L976
	ldrb	r3, [r4, #1717]	@ zero_extendqisi2
	ldr	r7, .L989+4
	strb	r3, [r9, #-2112]
	ldrb	r3, [r5, #8]	@ zero_extendqisi2
	cmp	r3, #2
	beq	.L977
.L981:
	ldrb	r3, [r5, #13]	@ zero_extendqisi2
	ldrb	r2, [r4, #1717]	@ zero_extendqisi2
	smulbb	r2, r2, r3
	ldrh	r3, [r5, #14]
	smulbb	r3, r2, r3
	ldr	r2, .L989+24
	strh	r3, [r2, #-14]	@ movhi
	ldmfd	sp!, {r3, r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L977:
	ldr	r9, [r7, #4]
	mov	r6, #0
.L980:
	mov	r0, r8
	add	r1, r10, r6, asl #3
	ldrb	r2, [r5]	@ zero_extendqisi2
	bl	FlashMemCmp8
	cmp	r0, #0
	bne	.L978
	ldrb	r1, [r5, #13]	@ zero_extendqisi2
	ldrh	r3, [r5, #14]
	ldrb	r2, [r7, #1717]	@ zero_extendqisi2
	mul	r1, r9, r1
	and	r3, r3, #65280
	add	r0, r7, r2, asl #2
	mul	r3, r3, r1
	str	r3, [r0, #656]
	ldrb	r1, [r5, #23]	@ zero_extendqisi2
	cmp	r1, #0
	movne	r3, r3, asl #1
	strne	r3, [r0, #656]
	add	r3, r2, #1
	add	r2, r4, r2
	strb	r3, [r4, #1717]
	strb	r6, [r2, #1720]
.L978:
	add	r6, r6, #1
	cmp	r6, #4
	bne	.L980
	b	.L981
.L990:
	.align	2
.L989:
	.word	.LANCHOR1+472
	.word	.LANCHOR0
	.word	.LANCHOR2
	.word	.LANCHOR0+1720
	.word	.LANCHOR0+1588
	.word	.LANCHOR0+1556
	.word	.LANCHOR2-2096
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
	ldr	r2, .L996
	ldr	r0, .L996+4
	mov	ip, #1
	ldr	r3, [r2, #692]
	ldrb	r1, [r3, #9]	@ zero_extendqisi2
	ldr	r3, [r2, #4]
	smulbb	r3, r1, r3
	strh	r3, [r4, #4]	@ unaligned
	ldrb	r3, [r2, #1792]	@ zero_extendqisi2
	strb	r3, [r4, #7]
	ldr	r3, [r2, #1912]
	str	r3, [r4]	@ unaligned
	ldr	r3, [r2, #692]
	ldrb	r1, [r3, #9]	@ zero_extendqisi2
	strb	r1, [r4, #6]
	mov	r1, #32
	strb	r1, [r4, #8]
	ldrb	r1, [r2, #1717]	@ zero_extendqisi2
	ldrb	r3, [r3, #7]	@ zero_extendqisi2
	strb	r3, [r4, #9]
	mov	r3, #0
	strb	r3, [r4, #10]
.L992:
	uxtb	r2, r3
	cmp	r2, r1
	bcs	.L995
	ldrb	lr, [r3, r0]	@ zero_extendqisi2
	add	r3, r3, #1
	ldrb	r2, [r4, #10]	@ zero_extendqisi2
	orr	r2, r2, ip, asl lr
	strb	r2, [r4, #10]
	b	.L992
.L995:
	ldmfd	sp!, {r4, pc}
.L997:
	.align	2
.L996:
	.word	.LANCHOR0
	.word	.LANCHOR0+1720
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
	ldr	r4, .L1103
	mvn	r2, #0
	ldr	r5, .L1103+4
	mov	r1, #32
	sub	r3, r4, #2096
	mov	r0, #1024
	str	r6, [r4, #-2104]
	mov	r8, #12
	strh	r6, [r3, #-12]	@ movhi
	movw	r3, #65535
	str	r3, [r4, #-2096]
	sub	r3, r4, #3168
	str	r6, [r4, #-3752]
	movw	r10, #1804
	strh	r2, [r3, #-14]	@ movhi
	mov	r7, #36
	strh	r2, [r3, #-12]	@ movhi
	strh	r2, [r3, #-10]	@ movhi
	strh	r2, [r3, #-8]	@ movhi
	sub	r2, r4, #3232
	strh	r6, [r3, #-6]	@ movhi
	sub	r3, r4, #2080
	strh	r1, [r2, #-4]	@ movhi
	mov	r1, #128
	strh	r6, [r3, #-4]	@ movhi
	sub	r3, r4, #3136
	strh	r1, [r2, #-2]	@ movhi
	sub	r2, r4, #3216
	strh	r6, [r3, #-2]	@ movhi
	movw	r3, #1878
	strh	r6, [r2, #-12]	@ movhi
	str	r6, [r4, #-3848]
	str	r6, [r4, #-3844]
	str	r6, [r4, #-3860]
	str	r6, [r4, #-3872]
	str	r6, [r4, #-3876]
	str	r6, [r4, #-3868]
	str	r6, [r4, #-3864]
	str	r6, [r4, #-3880]
	str	r6, [r4, #-3840]
	str	r6, [r4, #-3836]
	str	r6, [r5, #4064]
	str	r6, [r4, #-3828]
	str	r6, [r4, #-3824]
	str	r6, [r4, #-2100]
	str	r6, [r4, #-3224]
	str	r6, [r4, #-2092]
	str	r6, [r4, #-3232]
	str	r6, [r4, #-2088]
	ldrh	r1, [r5, r3]
	bl	__aeabi_idiv
	movw	r3, #1804
	ldrh	r3, [r5, r3]
	str	r6, [r5, #1924]
	movw	r6, #1876
	mov	r3, r3, asl #2
	cmp	r0, r3
	str	r0, [r4, #-2080]
	ldrh	r0, [r5, r6]
	strhi	r3, [r4, #-2080]
	mov	r0, r0, asl #1
	bl	ftl_malloc
	str	r0, [r4, #-3196]
	ldrh	r0, [r5, r6]
	mul	r0, r8, r0
	bl	ftl_malloc
	ldrh	r6, [r5, r10]
	mul	r6, r7, r6
	mov	r9, r6, asl #3
	str	r0, [r4, #-3188]
	mov	r0, r9
	bl	ftl_malloc
	str	r0, [r4, #-2076]
	mov	r0, r6
	bl	ftl_malloc
	str	r0, [r4, #-2072]
	mov	r0, r9
	bl	ftl_malloc
	movw	r9, #1882
	str	r0, [r4, #-2068]
	mov	r0, r6
	bl	ftl_malloc
	str	r0, [r5, #4056]
	mov	r0, r6
	bl	ftl_malloc
	str	r0, [r4, #-3204]
	ldr	r0, [r4, #-2080]
	mul	r0, r7, r0
	bl	ftl_malloc
	ldrh	r6, [r5, r9]
	ldrh	r3, [r5, r10]
	movw	r7, #1884
	mov	r3, r3, asl #1
	add	r3, r3, #1
	str	r3, [r4, #-3200]
	str	r0, [r5, #1928]
	mov	r0, r6
	bl	ftl_malloc
	str	r0, [r4, #-2064]
	mov	r0, r6
	bl	ftl_malloc
	str	r0, [r4, #-2060]
	mov	r0, r6
	bl	ftl_malloc
	str	r0, [r4, #-2056]
	ldr	r0, [r4, #-3200]
	mul	r0, r0, r6
	bl	ftl_malloc
	str	r0, [r4, #-3212]
	ldr	r0, [r4, #-2080]
	mul	r0, r0, r6
	bl	ftl_malloc
	str	r0, [r4, #-2052]
	mov	r0, r6
	bl	ftl_malloc
	str	r0, [r4, #-2048]
	mov	r0, r6
	bl	ftl_malloc
	str	r0, [r4, #-2044]
	ldr	r0, [r4, #-3200]
	mul	r0, r8, r0
	bl	ftl_malloc
	ldrh	r3, [r5, r10]
	ldrh	r6, [r5, r7]
	mul	r6, r3, r6
	str	r0, [r4, #-3216]
	mov	r0, r6
	bl	ftl_malloc
	str	r0, [r4, #-2040]
	mov	r0, r6, asl #3
	bl	ftl_malloc
	ldrh	r3, [r5, r7]
	ldr	r6, .L1103+8
	add	r6, r6, #48
	str	r0, [r4, #-2036]
	ldr	r0, [r4, #-3200]
	mul	r0, r0, r3
	bl	ftl_malloc
	ldrh	r3, [r5, r7]
	movw	r7, #1814
	str	r0, [r4, #-3208]
	ldr	r0, [r4, #-2080]
	mul	r0, r0, r3
	bl	ftl_malloc
	str	r0, [r4, #-2032]
	ldrh	r0, [r5, r7]
	mov	r0, r0, asl #1
	uxth	r0, r0
	strh	r0, [r6, #-60]	@ movhi
	bl	ftl_malloc
	str	r0, [r4, #-2024]
	ldrh	r0, [r6, #-60]
	add	r0, r0, #544
	add	r0, r0, #3
	mov	r0, r0, lsr #9
	strh	r0, [r6, #-60]	@ movhi
	mov	r0, r0, asl #9
	bl	ftl_malloc
	ldrh	r10, [r5, r7]
	mov	r10, r10, asl #1
	str	r0, [r4, #-2020]
	add	r0, r0, #32
	str	r0, [r5, #4060]
	mov	r0, r10
	bl	ftl_malloc
	str	r0, [r4, #-2016]
	mov	r0, r10
	bl	ftl_malloc
	ldr	r10, [r5, #1900]
	mov	r10, r10, asl #1
	str	r0, [r4, #-4060]
	mov	r0, r10
	bl	ftl_malloc
	str	r0, [r4, #-2012]
	mov	r0, r10
	bl	ftl_malloc
	movw	r10, #1892
	str	r0, [r4, #-2008]
	ldrh	r0, [r5, r7]
	mov	r0, r0, lsr #3
	add	r0, r0, #4
	bl	ftl_malloc
	str	r0, [r4, #-3884]
	ldrh	r0, [r5, r10]
	mov	r0, r0, asl #1
	bl	ftl_malloc
	str	r0, [r5, #1920]
	ldrh	r0, [r5, r10]
	mov	r0, r0, asl #1
	bl	ftl_malloc
	str	r0, [r4, #-2004]
	ldrh	r0, [r5, r10]
	movw	r10, #1894
	mov	r0, r0, asl #2
	bl	ftl_malloc
	str	r0, [r4, #-2000]
	ldrh	r0, [r5, r10]
	mov	r0, r0, asl #2
	bl	ftl_malloc
	ldrh	r2, [r5, r10]
	mov	r1, #0
	mov	r2, r2, asl #2
	str	r0, [r4, #-1996]
	bl	ftl_memset
	movw	r3, #1908
	ldrh	r10, [r5, r3]
	mov	r10, r10, asl #2
	mov	r0, r10
	bl	ftl_malloc
	str	r0, [r4, #-1992]
	mov	r0, r10
	bl	ftl_malloc
	movw	r10, #1910
	str	r0, [r4, #-1988]
	ldr	r0, [r5, #1900]
	mov	r0, r0, asl #2
	bl	ftl_malloc
	str	r0, [r4, #-1984]
	ldrh	r0, [r5, r10]
	mul	r0, r8, r0
	bl	ftl_malloc
	ldrh	r3, [r5, r10]
	str	r0, [r4, #-3896]
	ldrh	r0, [r5, r9]
	mul	r0, r0, r3
	bl	ftl_malloc
	ldrh	r3, [r5, r7]
	movw	r7, #1826
	str	r0, [r4, #-1980]
	mov	r0, #6
	mul	r0, r0, r3
	bl	ftl_malloc
	movw	r3, #1870
	ldrh	r3, [r5, r3]
	add	r3, r3, #31
	mov	r3, r3, asr #5
	strh	r3, [r6, #-8]	@ movhi
	str	r0, [r4, #-4068]
	ldrh	r0, [r5, r7]
	mul	r0, r0, r3
	mov	r0, r0, asl #2
	bl	ftl_malloc
	ldrh	r2, [r6, #-8]
	ldrh	lr, [r5, r7]
	mov	r3, #1
	ldr	ip, .L1103+12
	mov	r2, r2, asl #2
	mov	r1, r2
	str	r0, [r5, #1964]
.L1000:
	cmp	r3, lr
	bcs	.L1101
	ldr	r0, [r5, #1964]
	add	r3, r3, #1
	add	r0, r0, r1
	add	r1, r1, r2
	str	r0, [ip, #4]!
	b	.L1000
.L1101:
	ldr	r2, .L1103+16
	mov	r1, #0
.L1002:
	cmp	r3, #8
	addne	r0, r2, r3, asl #2
	addne	r3, r3, #1
	strne	r1, [r0, #28]
	bne	.L1002
.L1102:
	ldr	r2, [r4, #-2012]
	ldr	r3, .L1103
	cmp	r2, #0
	bne	.L1004
.L1006:
	ldr	r1, .L1103+20
	ldr	r0, .L1103+24
	bl	printk
	mvn	r0, #0
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, pc}
.L1004:
	ldr	r2, [r3, #-2008]
	cmp	r2, #0
	beq	.L1006
	ldr	r2, [r3, #-1992]
	cmp	r2, #0
	beq	.L1006
	ldr	r2, [r3, #-1984]
	cmp	r2, #0
	beq	.L1006
	ldr	r2, [r3, #-3896]
	cmp	r2, #0
	beq	.L1006
	ldr	r2, [r3, #-1980]
	cmp	r2, #0
	beq	.L1006
	ldr	r2, [r3, #-4068]
	cmp	r2, #0
	beq	.L1006
	ldr	r2, [r5, #1964]
	cmp	r2, #0
	beq	.L1006
	ldr	r3, [r3, #-4060]
	cmp	r3, #0
	beq	.L1006
	ldr	r2, [r4, #-3196]
	ldr	r3, .L1103
	cmp	r2, #0
	beq	.L1006
	ldr	r2, [r3, #-3188]
	cmp	r2, #0
	beq	.L1006
	ldr	r2, [r3, #-2076]
	cmp	r2, #0
	beq	.L1006
	ldr	r2, [r3, #-2068]
	cmp	r2, #0
	beq	.L1006
	ldr	r2, [r5, #4056]
	cmp	r2, #0
	beq	.L1006
	ldr	r2, [r3, #-3204]
	cmp	r2, #0
	beq	.L1006
	ldr	r2, [r3, #-2072]
	cmp	r2, #0
	beq	.L1006
	ldr	r2, [r3, #-2064]
	cmp	r2, #0
	beq	.L1006
	ldr	r2, [r3, #-2060]
	cmp	r2, #0
	beq	.L1006
	ldr	r3, [r3, #-2056]
	cmp	r3, #0
	beq	.L1006
	ldr	r2, [r4, #-3212]
	ldr	r3, .L1103
	cmp	r2, #0
	beq	.L1006
	ldr	r2, [r3, #-2048]
	cmp	r2, #0
	beq	.L1006
	ldr	r2, [r3, #-2044]
	cmp	r2, #0
	beq	.L1006
	ldr	r2, [r3, #-3216]
	cmp	r2, #0
	beq	.L1006
	ldr	r2, [r3, #-2040]
	cmp	r2, #0
	beq	.L1006
	ldr	r2, [r3, #-2036]
	cmp	r2, #0
	beq	.L1006
	ldr	r2, [r3, #-3208]
	cmp	r2, #0
	beq	.L1006
	ldr	r2, .L1103+4
	ldr	r1, [r2, #4060]
	cmp	r1, #0
	beq	.L1006
	ldr	r3, [r3, #-2024]
	cmp	r3, #0
	beq	.L1006
	ldr	r3, [r2, #1920]
	cmp	r3, #0
	beq	.L1006
	ldr	r3, .L1103
	ldr	r2, [r3, #-2004]
	cmp	r2, #0
	beq	.L1006
	ldr	r2, [r3, #-2000]
	cmp	r2, #0
	beq	.L1006
	ldr	r3, [r3, #-1996]
	cmp	r3, #0
	beq	.L1006
	mov	r0, #0
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, pc}
.L1104:
	.align	2
.L1103:
	.word	.LANCHOR2
	.word	.LANCHOR0
	.word	.LANCHOR2-2016
	.word	.LANCHOR0+1964
	.word	.LANCHOR0+1936
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
	stmfd	sp!, {r3, r4, r5, lr}
	.save {r3, r4, r5, lr}
	mov	r4, r0
	ldr	r3, .L1111
	mov	r0, r1
	mov	r5, r1
	mov	r1, #0
	ldrh	r2, [r3, #-8]
	mov	r2, r2, asl #2
	bl	ftl_memset
	add	r3, r4, #1020
	add	r3, r3, #2
	sub	r1, r4, #2
	mov	lr, #1
	movw	r4, #65535
.L1107:
	ldrh	r2, [r1, #2]!
	cmp	r2, r4
	ldmeqfd	sp!, {r3, r4, r5, pc}
	mov	ip, r2, lsr #5
	cmp	r1, r3
	and	r2, r2, #31
	ldr	r0, [r5, ip, asl #2]
	orr	r2, r0, lr, asl r2
	str	r2, [r5, ip, asl #2]
	bne	.L1107
	ldmfd	sp!, {r3, r4, r5, pc}
.L1112:
	.align	2
.L1111:
	.word	.LANCHOR2-1968
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
	ldr	r0, .L1114
	mvn	r3, #0
	mov	r1, #255
	mov	r2, #16
	add	r0, r0, #12
	strh	r3, [r0, #-12]	@ movhi
	mov	r3, #0
	strh	r3, [r0, #-6]	@ movhi
	b	ftl_memset
.L1115:
	.align	2
.L1114:
	.word	.LANCHOR0+1936
	.fnend
	.size	FtlBbtMemInit, .-FtlBbtMemInit
	.align	2
	.global	FtlFreeSysBlkQueueInit
	.type	FtlFreeSysBlkQueueInit, %function
FtlFreeSysBlkQueueInit:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	ldr	r1, .L1118
	movw	r2, #1996
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
.L1119:
	.align	2
.L1118:
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
.L1121:
	ldrh	r1, [r4, #6]
	uxth	r3, r2
	cmp	r1, r3
	bls	.L1141
	ldr	r0, [r6, r3, asl #2]
	mov	r1, #0
	ubfx	r0, r0, #10, #16
.L1122:
	ldrh	ip, [r4, #10]
	uxth	r3, r1
	cmp	ip, r3
	bls	.L1142
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
	b	.L1122
.L1142:
	add	r2, r2, #1
	b	.L1121
.L1141:
	ldr	r2, .L1144
	movw	r3, #1874
	mov	r8, #0
	mov	r1, r8
	mov	fp, r8
	ldrh	r2, [r2, r3]
	ldrh	r3, [r4]
	mov	r3, r3, asl #1
	strh	r2, [r5, r3]	@ movhi
	ldrh	r9, [r5]
.L1126:
	ldrh	r3, [r4, #10]
	uxth	r6, r8
	cmp	r3, r6
	bls	.L1143
	mov	r2, r6, asl #1
	ldrh	r3, [r5, r2]
	cmp	r9, r3
	bls	.L1127
	ldrh	r0, [r7, r2]
	add	r10, r7, r2
	cmp	r0, #0
	bne	.L1128
	b	.L1129
.L1127:
	cmp	r3, #0
	bne	.L1129
	ldrh	r0, [r7, r2]
	add	r10, r7, r2
	cmp	r0, #0
	movne	r6, r1
	beq	.L1129
	b	.L1131
.L1128:
	cmp	r3, #0
	movne	r1, r6
	movne	r9, r3
	bne	.L1129
	mov	r9, r3
.L1131:
	mov	r1, #1
	bl	FtlFreeSysBlkQueueIn
	strh	fp, [r10]	@ movhi
	ldrh	r3, [r4, #8]
	mov	r1, r6
	sub	r3, r3, #1
	strh	r3, [r4, #8]	@ movhi
.L1129:
	add	r8, r8, #1
	b	.L1126
.L1143:
	mov	r0, r1
	ldmfd	sp!, {r3, r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L1145:
	.align	2
.L1144:
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
	ldr	r5, .L1151
	mvn	r6, #0
	ldr	r4, .L1151+4
	ldr	r2, [r5, #1900]
	ldr	r0, [r4, #-2008]
	mov	r2, r2, asl #1
	bl	ftl_memset
	movw	r3, #1882
	movw	r2, #1910
	ldrh	r3, [r5, r3]
	ldrh	r2, [r5, r2]
	mov	r1, #255
	ldr	r0, [r4, #-1980]
	mul	r2, r2, r3
	bl	ftl_memset
	ldr	ip, .L1151+8
	mov	r1, #0
	mov	r3, r4
	sub	r7, ip, #28
	mov	r4, #12
	mov	r5, r1
.L1147:
	ldrh	r2, [ip]
	add	lr, r1, #1
	uxth	r1, r1
	ldr	r0, .L1151
	cmp	r2, r1
	bls	.L1150
	mul	r0, r4, r1
	ldr	r2, [r3, #-3896]
	add	r8, r2, r0
	str	r5, [r8, #4]
	strh	r6, [r2, r0]	@ movhi
	ldr	r2, [r3, #-3896]
	add	r0, r2, r0
	ldrh	r2, [r7]
	mul	r2, r1, r2
	ldr	r1, [r3, #-1980]
	bic	r2, r2, #3
	add	r2, r1, r2
	mov	r1, lr
	str	r2, [r0, #8]
	b	.L1147
.L1150:
	ldr	r1, .L1151+12
	mvn	ip, #0
	sub	r2, r1, #4
	strh	ip, [r1, #-2]	@ movhi
	strh	ip, [r1, #-4]	@ movhi
	ldr	ip, [r0, #1900]
	strh	ip, [r1, #6]	@ movhi
	ldrh	r1, [r1, #40]
	ldr	ip, .L1151+16
	strh	r1, [r2, #8]	@ movhi
	movw	r1, #1908
	ldrh	r1, [r0, r1]
	strh	ip, [r2, #4]	@ movhi
	strh	r1, [r2, #6]	@ movhi
	ldr	r2, [r3, #-2012]
	str	r2, [r3, #-1960]
	ldr	r2, [r3, #-1984]
	str	r2, [r3, #-1956]
	ldr	r2, [r3, #-2008]
	str	r2, [r3, #-1952]
	ldr	r2, [r3, #-1992]
	str	r2, [r3, #-1948]
	ldmfd	sp!, {r4, r5, r6, r7, r8, pc}
.L1152:
	.align	2
.L1151:
	.word	.LANCHOR0
	.word	.LANCHOR2
	.word	.LANCHOR0+1910
	.word	.LANCHOR2-1968
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
	ldr	r6, .L1155
	mov	r4, #0
	ldr	r5, .L1155+4
	mov	r1, r4
	sub	r2, r6, #1920
	movw	r7, #1814
	str	r3, [r6, #-1908]
	strh	r3, [r2]	@ movhi
	movw	r3, #1918
	strh	r4, [r5, r3]	@ movhi
	movw	r3, #1892
	ldrh	r2, [r5, r3]
	ldr	r0, [r5, #1920]
	str	r4, [r6, #-1924]
	mov	r2, r2, asl #1
	str	r4, [r6, #-1916]
	str	r4, [r6, #-1912]
	str	r4, [r5, #1796]
	bl	ftl_memset
	ldrh	r2, [r5, r7]
	mov	r1, r4
	ldr	r0, [r5, #4060]
	mov	r2, r2, asl #1
	bl	ftl_memset
	ldrh	r2, [r5, r7]
	mov	r1, r4
	ldr	r0, [r6, #-2024]
	mov	r2, r2, asl #1
	bl	ftl_memset
	add	r0, r5, #4064
	mov	r1, r4
	mov	r2, #48
	add	r0, r0, #4
	bl	ftl_memset
	sub	r0, r6, #3744
	mov	r1, r4
	mov	r2, #512
	sub	r0, r0, #4
	bl	ftl_memset
	bl	FtlGcBufInit
	bl	FtlL2PDataInit
	mov	r0, r4
	ldmfd	sp!, {r3, r4, r5, r6, r7, pc}
.L1156:
	.align	2
.L1155:
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
	movw	r3, #1814
	ldr	r7, .L1169
	mov	r2, #6
	ldr	r6, .L1169+4
	mov	r1, #0
	.pad #28
	sub	sp, sp, #28
	ldrh	r3, [r7, r3]
	ldr	r0, [r6, #-4068]
	mov	r5, r6
	mov	fp, r6
	mul	r2, r2, r3
	bl	ftl_memset
	sub	r2, r6, #4032
	mov	r3, #0
	sub	r1, r6, #4048
	strh	r3, [r2, #-12]	@ movhi
	sub	r2, r6, #2096
	mov	r4, r3
	mov	r8, r3
	mov	r9, r3
	str	r3, [r6, #-4048]
	str	r3, [r6, #-4064]
	str	r3, [r6, #-4056]
	strh	r3, [r1, #-4]	@ movhi
	strh	r3, [r2, #-12]	@ movhi
	str	r7, [sp, #8]
	str	r1, [sp, #12]
.L1158:
	ldr	r3, .L1169+8
	uxth	r7, r4
	sxth	r10, r7
	ldrh	r2, [r3]
	cmp	r10, r2
	bge	.L1165
	sub	r3, r3, #8
	ldr	r2, .L1169+12
	mov	ip, r7
	ldrh	r3, [r3]
	str	r3, [sp]
	ldrh	r3, [r2]
	mov	r2, #0
	mov	r6, r2
	str	r3, [sp, #4]
.L1166:
	ldr	r3, [sp]
	sxth	r1, r2
	cmp	r1, r3
	bge	.L1168
	ldr	r3, [sp, #8]
	str	r2, [sp, #20]
	add	r1, r3, r1
	str	ip, [sp, #16]
	ldrb	r0, [r1, #1832]	@ zero_extendqisi2
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
	b	.L1166
.L1168:
	cmp	r6, #0
	beq	.L1161
	sxth	r1, r6
	mov	r0, #32768
	bl	__aeabi_idiv
	uxth	r6, r0
	b	.L1162
.L1161:
	sxth	r7, r7
	ldr	r2, [r5, #-4060]
	mvn	r1, #0
	mov	r7, r7, asl #1
	strh	r1, [r2, r7]	@ movhi
.L1162:
	mov	r1, r10, asl #1
	ldr	r2, [r5, #-4068]
	add	r0, r1, r10
	add	r2, r2, r0, asl #1
	strh	r6, [r2, #4]	@ movhi
	ldr	r2, .L1169+16
	ldrh	r0, [r2]
	cmp	r10, r0
	beq	.L1163
	ldrh	r0, [r2, #48]
	cmp	r10, r0
	beq	.L1163
	ldrh	r2, [r2, #96]
	cmp	r10, r2
	beq	.L1163
	ldr	r3, [fp, #-4060]
	uxth	r0, r4
	ldrh	r3, [r3, r1]
	cmp	r3, #0
	bne	.L1164
	add	r8, r8, #1
	uxth	r8, r8
	bl	INSERT_FREE_LIST
	b	.L1163
.L1164:
	add	r9, r9, #1
	uxth	r9, r9
	bl	INSERT_DATA_LIST
.L1163:
	add	r4, r4, #1
	b	.L1158
.L1165:
	ldr	r3, [sp, #12]
	mov	r0, #0
	strh	r9, [r3, #-4]	@ movhi
	ldr	r3, .L1169+20
	strh	r8, [r3, #-12]	@ movhi
	add	sp, sp, #28
	@ sp needed
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L1170:
	.align	2
.L1169:
	.word	.LANCHOR0
	.word	.LANCHOR2
	.word	.LANCHOR0+1812
	.word	.LANCHOR0+1872
	.word	.LANCHOR2-4040
	.word	.LANCHOR2-4032
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
	movw	r5, #1876
	ldr	r4, .L1173
	mov	r2, #0
	ldr	r6, .L1173+4
	mov	r1, #255
	sub	r3, r4, #3184
	ldr	r0, [r4, #-3196]
	strh	r2, [r3, #-8]	@ movhi
	strh	r2, [r3]	@ movhi
	ldrh	r2, [r6, r5]
	mov	r2, r2, asl #1
	bl	ftl_memset
	ldrh	r3, [r6, r5]
	mov	r2, #12
	ldr	r0, [r4, #-3188]
	mov	r1, #255
	mul	r2, r2, r3
	bl	ftl_memset
	ldmfd	sp!, {r4, r5, r6, lr}
	b	FtlGcBufInit
.L1174:
	.align	2
.L1173:
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
	ldr	r1, .L1178
	bl	ftl_memcpy
	mov	r0, #0
	ldmfd	sp!, {r3, pc}
.L1179:
	.align	2
.L1178:
	.word	.LANCHOR2-1904
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
	ldr	r8, .L1193
.L1184:
	mov	r7, r4, asl #5
	ldr	r1, .L1193+4
	add	r0, r7, #1
	ldrb	r2, [r8, r4, asl #5]	@ zero_extendqisi2
	add	r0, r8, r0
	ldr	r6, .L1193+8
	bl	FlashMemCmp8
	add	r9, r6, #508
	subs	r5, r0, #0
	bne	.L1181
	adds	r9, r9, r7
	beq	.L1187
	add	r4, r6, r7
	mov	r3, r5
	ldr	r1, .L1193+12
	ldrb	r2, [r4, #530]	@ zero_extendqisi2
	b	.L1186
.L1181:
	add	r4, r4, #1
	cmp	r4, #81
	bne	.L1184
	b	.L1187
.L1192:
	add	r3, r3, #1
	cmp	r3, #4
	beq	.L1185
.L1186:
	ldrb	r0, [r1, r3, asl #5]	@ zero_extendqisi2
	cmp	r0, r2
	bne	.L1192
.L1185:
	ldr	r4, .L1193+16
	mov	r2, #32
	ldr	r1, .L1193+12
	ldr	r0, .L1193+20
	add	r1, r1, r3, asl #5
	bl	ftl_memcpy
	mov	r0, r4
	mov	r1, r9
	mov	r2, #32
	bl	ftl_memcpy
	ldrh	r0, [r4, #10]
	bl	FlashBlockAlignInit
	b	.L1182
.L1187:
	mvn	r5, #0
.L1182:
	mov	r0, r5
	ldmfd	sp!, {r3, r4, r5, r6, r7, r8, r9, pc}
.L1194:
	.align	2
.L1193:
	.word	.LANCHOR1+508
	.word	.LANCHOR0+1556
	.word	.LANCHOR1
	.word	.LANCHOR1+3100
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
	bne	.L1196
	cmp	r3, #0
	beq	.L1197
	mov	r0, r2
	mov	r1, r3
	mov	r2, #1024
	bl	ftl_memcpy
.L1197:
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
.L1196:
	cmp	r3, #0
	beq	.L1200
	mov	r1, r2
	mov	r0, r3
	mov	r2, #1024
	bl	ftl_memcpy
.L1200:
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
.L1213:
	cmp	r3, r2
	ldrne	ip, [r1, r3, asl #2]
	strne	ip, [r0, r3, asl #2]
	addne	r3, r3, #1
	bne	.L1213
.L1215:
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
	ldr	r3, .L1218
	ldr	r0, [r3]
	b	jiffies_to_msecs
.L1219:
	.align	2
.L1218:
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
	ldr	ip, .L1224
	cmp	r2, #0
	moveq	r2, r3
	ldr	ip, [ip, #144]
	add	ip, ip, #4096
	add	r1, ip, r1
	beq	.L1223
	str	lr, [sp, #-4]!
	.save {lr}
	mov	lr, r0
	mov	r0, r1
	mov	r1, lr
	ldr	lr, [sp], #4
	mov	r2, r3
.L1223:
	b	ftl_memcpy
.L1225:
	.align	2
.L1224:
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
	ldr	r3, .L1230
	ldr	r0, [r3, #1912]
	bx	lr
.L1231:
	.align	2
.L1230:
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
.L1236:
	cmp	r4, r8
	beq	.L1245
	cmp	r5, #0
	bne	.L1237
	ldr	r0, .L1246
	mov	r1, r9
	mov	r2, r6
	mov	r3, r4
	bl	printk
.L1237:
	cmp	r7, #4
	ldreq	r0, .L1246+4
	ldreq	r1, [r6, r4, asl #2]
	beq	.L1244
	cmp	r7, #2
	moveq	r3, r4, asl #1
	ldreq	r0, .L1246+4
	ldreqsh	r1, [r6, r3]
	ldrne	r0, .L1246+4
	ldrneb	r1, [r6, r4]	@ zero_extendqisi2
.L1244:
	add	r5, r5, #1
	bl	printk
	cmp	r5, #15
	bls	.L1241
	ldr	r0, .L1246+8
	mov	r5, #0
	ldr	r1, .L1246+12
	bl	printk
.L1241:
	add	r4, r4, #1
	b	.L1236
.L1245:
	ldr	r0, .L1246+8
	ldr	r1, .L1246+12
	ldmfd	sp!, {r3, r4, r5, r6, r7, r8, r9, lr}
	b	printk
.L1247:
	.align	2
.L1246:
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
	ldr	r7, .L1357
	mvn	r3, #83
	mvn	r1, #82
	mvn	r2, #81
	.pad #44
	sub	sp, sp, #44
	mov	r4, r0
	strb	r3, [r7, #702]
	mvn	r3, #80
	strb	r0, [r7, #698]
	strb	r1, [r7, #703]
	strb	r2, [r7, #704]
	strb	r3, [r7, #705]
	bne	.L1249
	mvn	r3, #88
	strb	r3, [r7, #702]
	ldr	r3, .L1357+4
	mvn	r2, #8
	mov	r5, #7
	strb	r2, [r3, #3245]
	b	.L1310
.L1249:
	cmp	r0, #3
	bne	.L1251
	mvn	r3, #79
	strb	r3, [r7, #702]
	mvn	r3, #78
	strb	r3, [r7, #703]
	mvn	r3, #77
	strb	r3, [r7, #704]
	mvn	r3, #76
	strb	r3, [r7, #705]
	mvn	r3, #75
	strb	r3, [r7, #706]
	mvn	r3, #74
	strb	r3, [r7, #707]
	mvn	r3, #73
	strb	r3, [r7, #708]
	mvn	r3, #72
	b	.L1351
.L1251:
	cmp	r0, #4
	bne	.L1252
	mvn	r0, #51
	strb	r1, [r7, #707]
	strb	r0, [r7, #702]
	mvn	r0, #64
	strb	r2, [r7, #708]
	strb	r0, [r7, #703]
	mvn	r0, #85
	strb	r0, [r7, #704]
	mvn	r0, #84
	strb	r0, [r7, #705]
	mvn	r0, #50
	strb	r0, [r7, #706]
.L1351:
	mov	r5, #8
	strb	r3, [r7, #709]
	mov	r6, r5
	b	.L1250
.L1252:
	cmp	r0, #5
	bne	.L1253
	mov	r3, #56
	mov	r5, #8
	strb	r3, [r7, #702]
	mov	r3, #57
	strb	r3, [r7, #703]
	mov	r3, #58
	strb	r3, [r7, #704]
	mov	r3, #59
	strb	r3, [r7, #705]
	b	.L1310
.L1253:
	cmp	r0, #6
	bne	.L1254
	mov	r3, #14
	mov	r5, #12
	strb	r3, [r7, #702]
	mov	r3, #15
	strb	r3, [r7, #703]
	mov	r3, #16
	strb	r3, [r7, #704]
	mov	r3, #17
	strb	r3, [r7, #705]
	b	.L1310
.L1254:
	cmp	r0, #7
	bne	.L1255
	mvn	r3, #79
	mov	r5, #12
	strb	r3, [r7, #702]
	mov	r6, #10
	mvn	r3, #78
	strb	r3, [r7, #703]
	mvn	r3, #77
	strb	r3, [r7, #704]
	mvn	r3, #76
	strb	r3, [r7, #705]
	mvn	r3, #75
	strb	r3, [r7, #706]
	mvn	r3, #74
	strb	r3, [r7, #707]
	mvn	r3, #73
	strb	r3, [r7, #708]
	mvn	r3, #72
	strb	r3, [r7, #709]
	mvn	r3, #43
	strb	r3, [r7, #710]
	mvn	r3, #42
	strb	r3, [r7, #711]
	b	.L1250
.L1255:
	cmp	r0, #8
	mov	r5, #7
	bne	.L1310
	mov	r3, #6
	strb	r5, [r7, #703]
	strb	r3, [r7, #702]
	mov	r5, #50
	mov	r3, #9
	strb	r0, [r7, #704]
	strb	r3, [r7, #705]
	mov	r6, #5
	mov	r3, #10
	strb	r3, [r7, #706]
	b	.L1250
.L1310:
	mov	r6, #4
.L1250:
	sub	r3, r4, #1
	cmp	r3, #1
	bhi	.L1347
	ldr	r10, .L1357+8
	mov	r9, #0
.L1256:
	ldrb	r1, [r7, #1717]	@ zero_extendqisi2
	uxtb	r2, r9
	ldr	r3, .L1357
	cmp	r1, r2
	bls	.L1263
	add	r2, r3, r2
	ldr	r4, .L1357+12
	ldr	r1, .L1357+16
	mov	ip, #55
	ldrb	r2, [r2, #1720]	@ zero_extendqisi2
	add	r3, r3, r2, asl #3
	add	r4, r4, r2, asl #6
	add	r4, r4, #20
	ldrb	r8, [r3, #16]	@ zero_extendqisi2
	ldr	r0, [r3, #12]
	mov	fp, r4
	mov	r2, r4
	add	r8, r0, r8, asl #8
.L1258:
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
	bcc	.L1258
	mov	r2, #0
.L1259:
	add	r0, r10, r2
	mov	r3, #1
.L1260:
	ldrb	r1, [r0, r3, asl #2]	@ zero_extendqisi2
	ldrb	ip, [fp]	@ zero_extendqisi2
	add	r1, r1, ip
	strb	r1, [fp, r3, asl #3]
	add	r3, r3, #1
	cmp	r3, #7
	bne	.L1260
	add	r2, r2, #1
	add	fp, fp, #1
	cmp	r2, #4
	bne	.L1259
	mov	r3, #0
	add	r9, r9, #1
	strb	r3, [r4, #16]
	strb	r3, [r4, #24]
	strb	r3, [r4, #32]
	strb	r3, [r4, #40]
	strb	r3, [r4, #48]
	strb	r3, [r4, #41]
	strb	r3, [r4, #49]
	b	.L1256
.L1347:
	sub	r3, r4, #3
	cmp	r3, #5
	bhi	.L1263
	smulbb	r2, r6, r5
	mov	r3, r2, asl #4
	mov	r2, r2, asr #1
	str	r3, [sp, #36]
	mov	r3, r2, asl #1
	str	r3, [sp, #8]
	mov	r3, #0
.L1355:
	str	r3, [sp, #16]
	ldrb	r3, [sp, #16]	@ zero_extendqisi2
	str	r3, [sp, #12]
	ldr	r3, .L1357
	ldr	r2, [sp, #12]
	ldrb	r3, [r3, #1717]	@ zero_extendqisi2
	cmp	r3, r2
	bhi	.L1308
.L1263:
	ldr	r3, .L1357
	strb	r6, [r3, #699]
	strb	r5, [r3, #700]
	add	sp, sp, #44
	@ sp needed
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L1308:
	ldr	r2, [sp, #12]
	ldr	r3, .L1357
	add	r3, r3, r2
	ldrb	r10, [r3, #1720]	@ zero_extendqisi2
	ldr	r3, .L1357
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
	ldreq	r3, .L1357+12
	moveq	r7, #160
	mlaeq	r7, r7, r10, r3
	addeq	r3, r7, #28
	beq	.L1352
.L1265:
	cmp	r4, #8
	beq	.L1267
	ldr	r3, .L1357+12
	add	r7, r3, r10, asl #6
	add	r3, r7, #20
.L1352:
	str	r3, [sp, #20]
	cmp	r4, #4
	add	r3, r8, fp, asl #8
	mov	r2, #54
	str	r2, [r3, #2056]
	bne	.L1268
	add	r9, r8, r9
	mov	r3, #255
	str	r3, [r9, #2052]
	mov	r3, #64
	str	r3, [r9, #2048]
	mov	r3, #204
	b	.L1353
.L1268:
	sub	r2, r4, #5
	cmp	r2, #1
	bhi	.L1270
	ldr	r2, .L1357
	ldrb	r2, [r2, #702]	@ zero_extendqisi2
	str	r2, [r3, #2052]
	mov	r2, #82
	str	r2, [r3, #2048]
	b	.L1269
.L1270:
	cmp	r4, #7
	bne	.L1269
	add	r9, r8, r9
	mov	r3, #174
	str	r3, [r9, #2052]
	mov	r3, #0
	str	r3, [r9, #2048]
	mov	r3, #176
.L1353:
	str	r3, [r9, #2052]
	mov	r3, #77
	str	r3, [r9, #2048]
.L1269:
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
.L1309:
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
	bne	.L1273
	cmp	r4, #7
	movne	ip, #2
	moveq	ip, #32
.L1273:
	ldr	r3, .L1357+20
	add	r0, r8, fp, asl #8
	ldr	r3, [r3, #148]
	mov	r1, r3
.L1274:
	ldr	r2, [r0, #2048]
	strb	r2, [r1], #1
	rsb	r2, r3, r1
	uxtb	r2, r2
	cmp	r2, ip
	bcc	.L1274
	ldr	r2, [sp, #4]
	cmp	r2, #0
	beq	.L1275
	mov	r2, #0
.L1277:
	ldrb	ip, [r3, r2, asl #2]	@ zero_extendqisi2
	uxtb	r1, r2
	cmp	ip, #50
	beq	.L1276
	add	ip, r3, r2, asl #2
	ldrb	ip, [ip, #1]	@ zero_extendqisi2
	cmp	ip, #5
	beq	.L1276
	add	r2, r2, #1
	cmp	r2, #8
	bne	.L1277
	b	.L1278
.L1276:
	cmp	r1, #6
	bls	.L1279
.L1278:
	ldr	r0, .L1357+24
	mov	r1, #0
	bl	printk
.L1280:
	b	.L1280
.L1275:
	cmp	r4, #7
	bne	.L1281
	ldr	r2, [sp, #4]
.L1283:
	ldrb	ip, [r3, r2, asl #2]	@ zero_extendqisi2
	uxtb	r1, r2
	cmp	ip, #12
	beq	.L1282
	add	ip, r3, r2, asl #2
	ldrb	ip, [ip, #1]	@ zero_extendqisi2
	cmp	ip, #10
	beq	.L1282
	add	r2, r2, #1
	cmp	r2, #8
	bne	.L1283
	b	.L1284
.L1282:
	cmp	r1, #7
	bne	.L1279
.L1284:
	ldr	r0, .L1357+24
	mov	r1, #0
	bl	printk
.L1285:
	b	.L1285
.L1281:
	cmp	r4, #6
	bne	.L1279
	sub	r2, r3, #1
	add	r3, r3, #7
.L1286:
	ldrb	r1, [r2, #1]!	@ zero_extendqisi2
	cmp	r1, #12
	beq	.L1279
	ldrb	r1, [r2, #8]	@ zero_extendqisi2
	cmp	r1, #4
	beq	.L1279
	cmp	r2, r3
	bne	.L1286
	ldr	r0, .L1357+24
	mov	r1, #0
	bl	printk
.L1288:
	b	.L1288
.L1279:
	ldr	r1, .L1357+20
	ldr	ip, [sp, #36]
	ldr	r2, [r1, #148]
	add	ip, r2, ip
	mov	r3, r2
.L1289:
	cmp	r3, ip
	ldrne	lr, [r0, #2048]
	strneb	lr, [r3], #1
	bne	.L1289
.L1356:
	ldr	r3, [r1, #148]
	mov	r0, #8
	ldr	r1, [sp, #8]
	add	r1, r3, r1
	str	r1, [sp, #28]
.L1292:
	ldr	ip, [sp, #8]
	add	lr, r1, ip
.L1291:
	ldrh	ip, [r1]
	mvn	ip, ip
	strh	ip, [r1], #2	@ movhi
	cmp	r1, lr
	bne	.L1291
	ldr	ip, [sp, #8]
	subs	r0, r0, #1
	add	r1, r1, ip
	bne	.L1292
.L1293:
	mov	ip, #0
	mov	r0, ip
.L1296:
	mov	r1, #1
	mov	lr, #0
	mov	r1, r1, asl r0
	mov	r7, #16
	str	r7, [sp, #32]
	mov	r7, lr
.L1294:
	ldrh	r9, [r3, lr]
	and	r9, r9, r1
	cmp	r9, r1
	addeq	r7, r7, #1
	ldr	r9, [sp, #8]
	add	lr, lr, r9
	ldr	r9, [sp, #32]
	subs	r9, r9, #1
	str	r9, [sp, #32]
	bne	.L1294
	cmp	r7, #8
	add	r0, r0, #1
	orrhi	ip, ip, r1
	uxthhi	ip, ip
	cmp	r0, #16
	bne	.L1296
	ldr	r1, [sp, #28]
	strh	ip, [r3], #2	@ movhi
	cmp	r3, r1
	bne	.L1293
	ldr	r3, .L1357+20
	ldr	r1, [r3, #148]
	mov	r3, #0
	sub	r0, r1, #4
	add	ip, r1, #28
.L1298:
	ldr	lr, [r0, #4]!
	cmp	lr, #0
	addeq	r3, r3, #1
	cmp	r0, ip
	bne	.L1298
	cmp	r3, #7
	ble	.L1299
	ldr	r0, .L1357+28
	mov	r2, #1
	mov	r3, #1024
	bl	rknand_print_hex
	ldr	r0, .L1357+24
	mov	r1, #0
	bl	printk
.L1300:
	b	.L1300
.L1299:
	cmp	r4, #6
	moveq	r0, #4
	beq	.L1301
	cmp	r4, #7
	moveq	r0, #10
	beq	.L1301
	ldr	r3, [sp, #4]
	cmp	r3, #0
	moveq	r0, #8
	movne	r0, #5
.L1301:
	sub	r9, r6, #1
	ldr	r1, [sp, #20]
	mov	ip, #0
	uxtb	r9, r9
	add	r9, r9, #1
.L1302:
	mov	r7, r1
	mov	r3, r2
.L1303:
	ldrb	lr, [r3], #1	@ zero_extendqisi2
	strb	lr, [r7], #1
	rsb	lr, r2, r3
	uxtb	lr, lr
	cmp	lr, r6
	bcc	.L1303
	add	ip, ip, #1
	add	r2, r2, r9
	cmp	ip, r5
	add	r1, r1, r0
	blt	.L1302
	add	r7, r8, fp, asl #8
	mov	r3, #255
	mov	r0, r10
	str	r3, [r7, #2056]
	bl	NandcWaitFlashReady
	ldr	r3, [sp, #24]
	cmp	r3, #0
	beq	.L1305
	mov	r3, #54
	str	r3, [r7, #2056]
	ldr	r3, .L1357
	mvn	r1, #0
	ldr	r0, [sp, #12]
	ldrb	r3, [r3, #702]	@ zero_extendqisi2
	str	r3, [r7, #2052]
	mov	r3, #0
	str	r3, [r7, #2048]
	mov	r3, #22
	str	r3, [r7, #2056]
	bl	FlashReadCmd
	b	.L1306
.L1305:
	ldr	r3, [sp, #4]
	cmp	r3, #0
	movne	r3, #190
	moveq	r3, #56
	str	r3, [r7, #2056]
.L1306:
	mov	r0, r10
	bl	NandcWaitFlashReady
	ldr	r3, [sp, #16]
	add	r3, r3, #1
	b	.L1355
.L1267:
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
	ldr	r3, .L1357+32
	str	r3, [sp, #20]
	b	.L1309
.L1358:
	.align	2
.L1357:
	.word	.LANCHOR0
	.word	.LANCHOR1
	.word	.LANCHOR1+3228
	.word	.LANCHOR0+698
	.word	.LANCHOR0+701
	.word	.LANCHOR2
	.word	.LC85
	.word	.LC86
	.word	.LANCHOR0+726
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
	bhi	.L1361
	b	HynixGetReadRetryDefault
.L1361:
	cmp	r3, #49
	bne	.L1362
	ldr	r2, .L1373
	ldr	r0, .L1373+4
	ldr	r1, .L1373+8
	strb	r3, [r2, #698]
	mov	r3, #4
	strb	r3, [r2, #699]
	mov	r3, #15
	strb	r3, [r2, #700]
	mov	r2, #64
	b	.L1371
.L1362:
	sub	r2, r3, #65
	cmp	r3, #33
	cmpne	r2, #1
	ldrls	r2, .L1373
	strlsb	r3, [r2, #698]
	movls	r3, #4
	bls	.L1372
.L1363:
	cmp	r3, #34
	cmpne	r3, #67
	bne	.L1364
	ldr	r2, .L1373
	strb	r3, [r2, #698]
	mov	r3, #5
.L1372:
	strb	r3, [r2, #699]
	mov	r3, #7
	ldr	r0, .L1373+4
	strb	r3, [r2, #700]
	mov	r2, #45
	ldr	r1, .L1373+12
	b	.L1371
.L1364:
	cmp	r3, #35
	cmpne	r3, #68
	bxne	lr
	ldr	r2, .L1373
	ldr	r0, .L1373+4
	ldr	r1, .L1373+16
	strb	r3, [r2, #698]
	mov	r3, #5
	strb	r3, [r2, #699]
	mov	r3, #17
	strb	r3, [r2, #700]
	mov	r2, #95
.L1371:
	b	ftl_memcpy
.L1374:
	.align	2
.L1373:
	.word	.LANCHOR0
	.word	.LANCHOR0+702
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
	ldr	r5, .L1415
	add	r0, r5, r0, asl #3
	ldr	r3, [r5, #1740]
	ldr	r4, [r0, #12]
	cmp	r3, #3
	bls	.L1406
	ldr	r3, [r4, #16]
	tst	r3, #4
	beq	.L1406
	mov	r0, r4
	bl	wait_for_nandc_xfer_completed
	ldr	r6, [r4, #16]
	ldr	r3, [r4, #8]
	ubfx	r6, r6, #1, #1
	cmp	r6, #0
	movne	r6, #0
	str	r3, [sp]
	beq	.L1388
.L1378:
	ldr	r2, [r4, #28]
	ldr	r3, [sp]
	ubfx	r2, r2, #16, #5
	ubfx	r3, r3, #22, #6
	cmp	r2, r3
	bge	.L1386
	ldr	r3, [r5, #1740]
	cmp	r3, #5
	bhi	.L1379
.L1382:
	add	r6, r6, #1
	bic	r3, r6, #-16777216
	cmp	r3, #0
	bne	.L1378
	ldr	r2, [r4, #28]
	mov	r1, r6
	ldr	r3, [sp]
	ubfx	r2, r2, #16, #5
	ldr	r0, .L1415+4
	ubfx	r3, r3, #22, #6
	bl	printk
	ldr	r0, .L1415+8
	mov	r1, r4
	mov	r2, #4
	mov	r3, #512
	bl	rknand_print_hex
	b	.L1378
.L1379:
	ldr	r3, [r4]
	str	r3, [sp, #4]
	ldr	r3, [sp, #4]
	tst	r3, #8192
	beq	.L1382
	ldr	r3, [sp, #4]
	tst	r3, #131072
	beq	.L1382
.L1386:
	ldr	r3, [r5, #1776]
	ldr	r4, .L1415
	cmp	r3, #0
	beq	.L1387
	ldr	r1, [sp]
	mov	r2, #0
	ldr	r0, [r4, #1768]
	ubfx	r1, r1, #22, #5
	mov	r1, r1, asl #10
	bl	rknand_dma_unmap_single
	ldr	r0, [r4, #1772]
	mov	r2, #0
	ldr	r1, [sp]
	ubfx	r1, r1, #22, #5
	mov	r1, r1, asl #7
	bl	rknand_dma_unmap_single
	b	.L1387
.L1390:
	ldr	r3, [r4, #8]
	add	r6, r6, #1
	str	r3, [sp]
	bic	r3, r6, #-16777216
	cmp	r3, #0
	bne	.L1388
	ldr	r2, [sp]
	mov	r1, r6
	ldr	r3, [r4, #28]
	ldr	r0, .L1415+12
	ubfx	r3, r3, #16, #5
	bl	printk
	ldr	r0, .L1415+8
	mov	r1, r4
	mov	r2, #4
	mov	r3, #512
	bl	rknand_print_hex
.L1388:
	ldr	r3, [sp]
	tst	r3, #1048576
	beq	.L1390
	ldr	r3, [r5, #1784]
	cmp	r3, #0
	beq	.L1391
	mov	r0, r4
	bl	NandcSendDumpDataStart
.L1391:
	ldr	r3, [r5, #1776]
	ldr	r6, .L1415
	cmp	r3, #0
	beq	.L1392
	ldr	r1, [sp]
	mov	r2, #1
	ldr	r0, [r6, #1768]
	ubfx	r1, r1, #22, #5
	mov	r1, r1, asl #10
	bl	rknand_dma_unmap_single
	ldr	r0, [r6, #1772]
	mov	r2, #1
	ldr	r1, [sp]
	ubfx	r1, r1, #22, #5
	mov	r1, r1, asl #7
	bl	rknand_dma_unmap_single
.L1392:
	ldr	r3, [r5, #1784]
	cmp	r3, #0
	beq	.L1387
	mov	r0, r4
	bl	NandcSendDumpDataDone
.L1387:
	mov	r3, #0
	str	r3, [r5, #1776]
	b	.L1375
.L1406:
	ldr	r3, [r4, #8]
	str	r3, [sp]
	ldr	r3, [sp]
	tst	r3, #1048576
	beq	.L1406
.L1375:
	add	sp, sp, #8
	@ sp needed
	ldmfd	sp!, {r4, r5, r6, pc}
.L1416:
	.align	2
.L1415:
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
	@ args = 4, pretend = 0, frame = 88
	@ frame_needed = 0, uses_anonymous_args = 0
	stmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.save {r4, r5, r6, r7, r8, r9, r10, fp, lr}
	mov	fp, r3
	ldr	r3, .L1466
	mov	r8, r2
	.pad #100
	sub	sp, sp, #100
	ldr	r4, .L1466+4
	tst	fp, #63
	mov	r10, r0
	ldr	r2, [r3]
	mov	r7, r1
	ldr	r5, [sp, #136]
	str	r3, [sp, #16]
	str	r2, [sp, #92]
	add	r2, r4, r0, asl #3
	ldr	r9, [r2, #12]
	bne	.L1418
	cmp	r5, #0
	bne	.L1419
	add	r0, sp, #28
	mov	r1, #255
	mov	r2, #64
	bl	ftl_memset
	add	r5, sp, #28
.L1419:
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
	bne	.L1420
	ldr	r3, [r4, #1788]
	mov	r1, r8, lsr #1
	mov	r2, r7
	cmp	r3, #25
	mov	r3, r7
	movcc	r0, #64
	movcs	r0, #128
.L1422:
	cmp	r2, r1
	add	r5, r5, #4
	add	ip, r3, r0
	bcs	.L1464
	ldr	lr, [r4, #1756]
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
	b	.L1422
.L1464:
	ldr	r1, [r4, #1788]
	mov	r2, #0
	ldr	r0, [r4, #1740]
	mov	r8, r8, lsr #2
	mov	r6, r2
.L1424:
	cmp	r2, r8
	bcs	.L1420
	cmp	r1, #0
	beq	.L1420
	add	r3, r2, #8
	ldr	r3, [r9, r3, asl #2]
	str	r3, [sp, #24]
	ldr	r3, [sp, #24]
	tst	r3, #4
	bne	.L1449
	ldr	r4, [sp, #24]
	ubfx	r4, r4, #15, #1
	cmp	r4, #0
	bne	.L1449
	cmp	r0, #5
	bls	.L1426
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
	b	.L1463
.L1426:
	cmp	r0, #3
	bls	.L1428
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
	bls	.L1429
	ldr	r3, [sp, #24]
	ldr	r4, [sp, #24]
	ubfx	r3, r3, #3, #5
	ubfx	r4, r4, #28, #1
.L1463:
	orr	r4, r3, r4, asl #5
	b	.L1428
.L1429:
	ldr	r5, [sp, #24]
	ldr	r4, [sp, #24]
	ubfx	r5, r5, #16, #5
	ubfx	r4, r4, #30, #1
	orr	r4, r5, r4, asl #5
.L1428:
	cmp	r6, r4
	movcc	r6, r4
	b	.L1425
.L1449:
	mvn	r6, #0
.L1425:
	add	r2, r2, #1
	b	.L1424
.L1420:
	mov	r3, #0
	str	r3, [r9, #16]
	b	.L1431
.L1418:
	cmp	r1, #1
	mov	r4, #0
	bne	.L1461
	mov	r6, r4
.L1432:
	cmp	r4, r8
	bcs	.L1465
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
	b	.L1432
.L1465:
	mov	r6, #0
	b	.L1431
.L1461:
	str	r4, [sp]
	mov	r1, r4
	str	r4, [sp, #4]
	mov	r2, #2
	mov	r3, r4
	mov	r6, r4
	bl	NandcXferStart
	str	fp, [sp, #12]
.L1437:
	cmp	r4, r8
	bcs	.L1431
	mov	r0, r10
	bl	NandcXferComp
	ldr	r3, [r9, #32]
	add	ip, r4, #2
	cmp	ip, r8
	str	r3, [sp, #24]
	bcs	.L1438
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
.L1438:
	ldr	r3, [sp, #24]
	tst	r3, #4
	mvnne	r6, #0
	bne	.L1439
	ldr	r2, [sp, #24]
	ldr	r3, [sp, #24]
	ubfx	r2, r2, #3, #5
	ubfx	r3, r3, #27, #1
	orr	r3, r2, r3, asl #5
	cmp	r6, r3
	movcc	r6, r3
.L1439:
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
	b	.L1437
.L1431:
	ldr	r3, .L1466+4
	clz	r7, r7
	mov	r7, r7, lsr #5
	ldr	r3, [r3, #1740]
	cmp	r3, #5
	movls	r7, #0
	cmp	r7, #0
	beq	.L1443
	ldr	r3, [r9]
	and	r2, r3, #139264
	cmp	r2, #139264
	orreq	r3, r3, #131072
	streq	r3, [r9]
	mvneq	r6, #0
.L1443:
	ldr	r3, [sp, #16]
	mov	r0, r6
	ldr	r2, [sp, #92]
	ldr	r3, [r3]
	cmp	r2, r3
	beq	.L1445
	bl	__stack_chk_fail
.L1445:
	add	sp, sp, #100
	@ sp needed
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L1467:
	.align	2
.L1466:
	.word	__stack_chk_guard
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
	ldr	r3, .L1471
	subs	r4, r0, #0
	mov	r6, r1
	mov	r7, r2
	ldrb	r5, [r3, #481]	@ zero_extendqisi2
	bne	.L1469
	ldr	r2, .L1471+4
	ldrb	r3, [r2, #1]	@ zero_extendqisi2
	ldr	r2, [r2, #4]
	mul	r2, r2, r3
	cmp	r1, r2
	movcc	r5, #4
.L1469:
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
.L1472:
	.align	2
.L1471:
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
	ldr	r4, .L1500
	.pad #20
	sub	sp, sp, #20
	mov	r10, r0
	mov	fp, r2
	str	r1, [sp]
	ldr	r3, [r4, #76]
	ldr	r3, [r3, #304]
	str	r3, [sp, #8]
	ldr	r3, [r4, #1740]
	cmp	r3, #8
	ldr	r3, [sp, #56]
	movcc	r9, #6
	movcs	r9, #12
	cmp	r3, #0
	moveq	r6, #1024
	beq	.L1475
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
	ldrb	r0, [r4, #1716]	@ zero_extendqisi2
	bl	FlashSetInterfaceMode
	ldrb	r0, [r4, #1716]	@ zero_extendqisi2
	bl	NandcSetMode
	cmn	r6, #1
	bne	.L1476
.L1485:
	mvn	r6, #0
	b	.L1477
.L1476:
	mov	r2, r6
	ldr	r0, .L1500+4
	ldr	r1, [sp]
	bl	printk
	cmp	r6, #9
	addls	r4, r4, r10, asl #3
	ldrls	r3, [r4, #12]
	ldr	r4, .L1500+8
	ldrls	r2, [r3, #3840]
	ldrls	r2, [r3]
	orrls	r2, r2, #131072
	strls	r2, [r3]
	ldr	r3, [r4, #152]
	add	r3, r3, #1
	str	r3, [r4, #152]
	cmp	r3, #2048
	movcs	r7, #0
	strcs	r7, [r4, #152]
	movcs	fp, r7
	bcc	.L1477
.L1475:
	mov	r4, #0
	mvn	r8, #0
	mov	ip, r4
	mov	r5, r4
	str	r4, [sp, #4]
.L1483:
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
	bhi	.L1479
	cmp	r0, #2
	bhi	.L1489
	add	r5, r5, #1
	cmp	r5, #9
	bls	.L1489
	rsb	r4, r5, r9
	mov	r6, r0
	mov	r8, #0
	b	.L1481
.L1479:
	ldr	r3, [sp, #4]
	cmp	r3, r5
	bcs	.L1490
	cmp	r5, #7
	rsb	ip, r5, r4
	bhi	.L1491
	str	r5, [sp, #4]
	b	.L1490
.L1489:
	mov	r8, #0
	mov	r4, r9
	mov	r6, r0
	mov	r7, r8
	mov	fp, r8
	b	.L1480
.L1490:
	mov	r5, #0
.L1480:
	add	r9, r9, #2
	cmp	r9, #69
	bls	.L1483
.L1481:
	ldr	r3, [sp, #4]
	cmp	r3, r5
	movcs	r4, ip
	b	.L1482
.L1491:
	mov	r4, ip
.L1482:
	cmp	r4, #0
	beq	.L1484
	ldr	r0, .L1500+12
	mov	r1, r4
	bl	printk
	uxtb	r0, r4
	bl	NandcSetDdrPara
.L1484:
	cmn	r8, #1
	bne	.L1477
	ldr	r0, .L1500+16
	mov	r1, r10
	ldr	r2, [sp]
	bl	printk
	ldr	r3, [sp, #56]
	cmp	r3, #0
	beq	.L1485
	ldr	r3, [sp, #8]
	ubfx	r0, r3, #8, #8
	bl	NandcSetDdrPara
.L1477:
	mov	r0, r6
	add	sp, sp, #20
	@ sp needed
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L1501:
	.align	2
.L1500:
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
	bne	.L1503
	ldr	r9, .L1523
	ldrb	fp, [r9, #8]	@ zero_extendqisi2
	mov	r10, r9
	cmp	fp, #0
	bne	.L1504
.L1506:
	ldrb	r3, [r10, #1732]	@ zero_extendqisi2
	ldr	r9, .L1523
	cmp	r3, #0
	beq	.L1503
	b	.L1522
.L1504:
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
	beq	.L1506
	b	.L1503
.L1522:
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
	beq	.L1507
	ldrb	r3, [r9, #1792]	@ zero_extendqisi2
	cmp	r0, r3, lsr #1
	bls	.L1503
.L1507:
	ubfx	r0, r10, #8, #8
	bl	NandcSetDdrPara
.L1503:
	ldr	r9, .L1523+4
	ldr	ip, [r9, #156]
	adds	r3, ip, #0
	movne	r3, #1
	cmn	r4, #1
	movne	r3, #0
	cmp	r3, #0
	beq	.L1508
	mov	r1, r6
	mov	r2, r8
	mov	r3, r7
	mov	r0, r5
	blx	ip
	mov	r2, r5
	mov	r3, r6
	mov	r4, r0
	ldr	r0, .L1523+8
	mov	r1, r4
	bl	printk
	cmn	r4, #1
	bne	.L1508
	ldr	r3, .L1523
	ldrb	r3, [r3, #140]	@ zero_extendqisi2
	cmp	r3, #0
	beq	.L1508
	mov	r0, r5
	bl	flash_enter_slc_mode
	ldr	ip, [r9, #156]
	mov	r0, r5
	mov	r1, r6
	mov	r2, r8
	mov	r3, r7
	blx	ip
	mov	r4, r0
	mov	r0, r5
	bl	flash_exit_slc_mode
.L1508:
	mov	r0, r4
	add	sp, sp, #12
	@ sp needed
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L1524:
	.align	2
.L1523:
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
	ldr	r5, .L1535
	mov	r6, r1
	mov	r4, #0
	ldrb	r0, [r5, #1716]	@ zero_extendqisi2
	bl	FlashSetInterfaceMode
	ldrb	r0, [r5, #1716]	@ zero_extendqisi2
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
	bne	.L1526
	ldrb	r2, [r5, #1716]	@ zero_extendqisi2
	tst	r2, #1
	beq	.L1526
	mov	r0, #1
	bl	FlashSetInterfaceMode
	mov	r0, #1
	bl	NandcSetMode
	strb	r4, [r5, #1732]
	b	.L1527
.L1526:
	mov	r2, #1
	strb	r2, [r3, #1732]
.L1527:
	mov	r0, #0
	add	sp, sp, #8
	@ sp needed
	ldmfd	sp!, {r4, r5, r6, r7, r8, pc}
.L1536:
	.align	2
.L1535:
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
	ldr	r10, .L1554
	mov	r6, #0
	ldr	r7, .L1554+4
	mov	r4, r6
	ldr	r5, .L1554+8
	mov	r9, #4
	ldr	r3, [r10]
	mvn	r8, #0
	str	r6, [r5, #164]
	str	r3, [sp, #12]
	ldr	r3, .L1554+12
	ldr	r0, [r3]	@ unaligned
	ldr	r3, [r7, #4]
	str	r0, [sp, #8]	@ unaligned
	mov	r0, r6
	str	r3, [sp]
	mov	r6, r5
	ldr	r3, [r5, #148]
	str	r3, [r5, #160]
	bl	flash_enter_slc_mode
	str	r10, [sp, #4]
.L1538:
	add	fp, r4, #1
	mov	r10, #0
.L1540:
	add	r3, sp, #8
	ldrb	r0, [r3, r10]	@ zero_extendqisi2
	bl	FlashBchSel
	mov	r0, #0
	mov	r1, r4
	ldr	r2, [r5, #148]
	mov	r3, r0
	bl	FlashReadRawPage
	cmn	r0, #1
	bne	.L1539
	mov	r0, #0
	mov	r1, fp
	ldr	r2, [r6, #148]
	mov	r3, r0
	bl	FlashReadRawPage
	cmn	r0, #1
	bne	.L1539
	add	r10, r10, #1
	cmp	r10, #4
	beq	.L1541
	b	.L1540
.L1542:
	add	r0, fp, #12
	movw	r1, #2036
	bl	js_hash
	ldr	r3, [fp, #8]
	cmp	r3, r0
	mvnne	r8, #0
	bne	.L1541
	ldr	r8, .L1554+16
	add	r1, fp, #160
	mov	r2, #32
	mov	r0, r8
	bl	ftl_memcpy
	ldr	r1, [r6, #160]
	mov	r2, #32
	ldr	r0, .L1554+20
	add	r1, r1, #192
	bl	ftl_memcpy
	ldr	r1, [r6, #160]
	mov	r2, #852
	ldr	r0, .L1554+24
	add	r1, r1, #224
	bl	ftl_memcpy
	ldrh	r0, [r8, #10]
	bl	FlashBlockAlignInit
	ldr	r8, [r6, #160]
	str	r4, [r6, #164]
	mov	r0, r4
	ldr	r1, [r7, #4]
	ldr	r3, [r8, #1076]
	strb	r3, [r7, #1732]
	bl	__aeabi_uidiv
	add	r0, r0, #1
	cmp	r0, #1
	strhi	r0, [r6, #168]
	movls	r3, #2
	strls	r3, [r6, #168]
	ldrh	r3, [r8, #14]
	mov	r8, #0
	strb	r3, [r5, #172]
.L1541:
	ldr	r3, [sp]
	subs	r9, r9, #1
	add	r4, r4, r3
	bne	.L1538
	mov	r0, r9
.L1553:
	bl	flash_exit_slc_mode
	mov	r0, r8
	ldr	r3, [sp, #4]
	ldr	r2, [sp, #12]
	ldr	r3, [r3]
	cmp	r2, r3
	beq	.L1548
	bl	__stack_chk_fail
.L1539:
	ldr	fp, [r5, #160]
	ldr	r2, .L1554+28
	ldr	r3, [fp]
	cmp	r3, r2
	bne	.L1541
	cmp	r8, #0
	bne	.L1542
	mov	r0, r4
	ldr	r1, [r7, #4]
	bl	__aeabi_uidiv
	ldr	r3, .L1554+8
	add	r0, r0, #1
	str	r0, [r3, #168]
	mov	r0, r8
	b	.L1553
.L1548:
	add	sp, sp, #20
	@ sp needed
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L1555:
	.align	2
.L1554:
	.word	__stack_chk_guard
	.word	.LANCHOR0
	.word	.LANCHOR2
	.word	.LANCHOR3+11
	.word	.LANCHOR1+472
	.word	.LANCHOR0+44
	.word	.LANCHOR0+698
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
	ldr	r4, .L1585
	add	r3, r4, r9, asl #3
	ldrb	r2, [r3, #16]	@ zero_extendqisi2
	ldr	r6, [r3, #12]
	ldrb	r3, [r4, #696]	@ zero_extendqisi2
	add	r7, r2, #8
	sub	r3, r3, #67
	add	r7, r6, r7, asl #8
	cmp	r3, #1
	mov	r3, r2, asl #8
	str	r3, [sp, #12]
	movls	r3, #0
	strls	r3, [sp, #4]
	bls	.L1557
	ldrb	r5, [r4, #1732]	@ zero_extendqisi2
	cmp	r5, #0
	streq	r5, [sp, #4]
	beq	.L1558
	mov	r0, #0
	str	r2, [sp, #20]
	bl	NandcSetDdrMode
	mov	r3, #1
	ldr	r2, [sp, #20]
	str	r3, [sp, #4]
.L1558:
	ldr	r3, [sp, #12]
	mov	r1, #92
	add	r3, r6, r3
	str	r1, [r3, #2056]
	mov	r1, #197
	str	r1, [r3, #2056]
.L1557:
	mov	r8, #1
	mvn	r10, #0
	mov	r3, r2, asl #8
	str	r3, [sp, #20]
.L1559:
	ldr	r3, .L1585+4
	ldrb	r3, [r3, #173]	@ zero_extendqisi2
	add	r3, r3, #1
	cmp	r8, r3
	bcs	.L1584
	ldrb	r3, [r4, #696]	@ zero_extendqisi2
	mov	r0, r7
	uxtb	r1, r8
	sub	r3, r3, #67
	cmp	r3, #1
	bhi	.L1560
	bl	SandiskSetRRPara
	b	.L1561
.L1560:
	bl	ToshibaSetRRPara
.L1561:
	ldrb	r3, [r4, #696]	@ zero_extendqisi2
	cmp	r3, #34
	bne	.L1562
	ldr	r3, .L1585+4
	ldrb	r3, [r3, #173]	@ zero_extendqisi2
	sub	r3, r3, #3
	cmp	r8, r3
	ldreq	r3, [sp, #20]
	moveq	r2, #179
	addeq	r3, r6, r3
	streq	r2, [r3, #2056]
.L1562:
	ldr	r3, [sp, #12]
	mov	r2, #38
	add	r3, r6, r3
	str	r2, [r3, #2056]
	mov	r2, #93
	str	r2, [r3, #2056]
	ldr	r3, [sp, #4]
	cmp	r3, #0
	beq	.L1563
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
	b	.L1564
.L1563:
	mov	r0, r9
	ldr	r1, [sp, #16]
	ldr	r2, [sp, #8]
	mov	r3, fp
	bl	FlashReadRawPage
	mov	r5, r0
.L1564:
	cmn	r5, #1
	beq	.L1565
	ldrb	r2, [r4, #1792]	@ zero_extendqisi2
	cmn	r10, #1
	moveq	r10, r5
	add	r2, r2, r2, asl #1
	cmp	r5, r2, asr #2
	bcc	.L1567
	mov	fp, #0
	str	fp, [sp, #8]
.L1565:
	add	r8, r8, #1
	b	.L1559
.L1584:
	mov	r5, r10
.L1567:
	ldrb	r2, [r4, #696]	@ zero_extendqisi2
	mov	r0, r7
	mov	r1, #0
	sub	r2, r2, #67
	cmp	r2, #1
	bhi	.L1569
	bl	SandiskSetRRPara
	b	.L1570
.L1569:
	bl	ToshibaSetRRPara
.L1570:
	ldr	r3, [sp, #12]
	mov	r2, #255
	add	r6, r6, r3
	str	r2, [r6, #2056]
	ldrb	r2, [r4, #1792]	@ zero_extendqisi2
	add	r2, r2, r2, asl #1
	cmp	r5, r2, asr #2
	bcc	.L1571
	cmn	r5, #1
	movne	r5, #256
.L1571:
	mov	r0, r9
	bl	NandcWaitFlashReady
	ldr	r3, [sp, #4]
	cmp	r3, #0
	beq	.L1572
	mov	r0, #4
	bl	NandcSetDdrMode
.L1572:
	mov	r0, r5
	add	sp, sp, #28
	@ sp needed
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L1586:
	.align	2
.L1585:
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
	ldr	r5, .L1601
	mov	r10, r2
	mov	r8, r3
	mov	fp, r1
	bl	NandcWaitFlashReady
	add	r2, r5, r9, asl #3
	ldr	ip, .L1601+4
	mov	r7, #1
	ldrb	r3, [r2, #16]	@ zero_extendqisi2
	mvn	r4, #0
	ldr	r6, [r2, #12]
	add	r3, r3, #8
	add	r6, r6, r3, asl #8
.L1588:
	ldrb	r3, [ip, #173]	@ zero_extendqisi2
	add	r3, r3, #1
	cmp	r7, r3
	bcs	.L1591
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
	beq	.L1589
	ldrb	r3, [r5, #1792]	@ zero_extendqisi2
	cmn	r4, #1
	moveq	r4, r0
	add	r3, r3, r3, asl #1
	cmp	r0, r3, asr #2
	bcc	.L1594
	mov	r8, #0
	mov	r10, r8
.L1589:
	add	r7, r7, #1
	b	.L1588
.L1594:
	mov	r4, r0
.L1591:
	mov	r0, r6
	mov	r1, #0
	bl	SamsungSetRRPara
	ldrb	r3, [r5, #1792]	@ zero_extendqisi2
	add	r3, r3, r3, asl #1
	cmp	r4, r3, asr #2
	bcc	.L1593
	cmn	r4, #1
	movne	r4, #256
.L1593:
	mov	r0, r4
	add	sp, sp, #12
	@ sp needed
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L1602:
	.align	2
.L1601:
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
.L1605:
	stmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.save {r4, r5, r6, r7, r8, r9, r10, fp, lr}
	mov	r8, r3
	ldr	r3, .L1629
	mov	fp, r2
	.pad #36
	sub	sp, sp, #36
	mov	r6, r0
	mov	r10, #0
	ldrb	r5, [r3, #1792]	@ zero_extendqisi2
	ldrb	r3, [r3, #140]	@ zero_extendqisi2
	str	r1, [sp, #20]
	cmp	r3, #0
	addeq	r5, r5, r5, asl #1
	ldrne	r2, .L1629+4
	ubfxeq	r5, r5, #2, #8
	smullne	r2, r3, r5, r2
	uxtbne	r5, r3
	ldr	r3, .L1629
	add	r3, r3, r0, asl #3
	str	r3, [sp, #24]
.L1615:
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
.L1606:
	ldr	r3, .L1629+8
	ldrb	r3, [r3, #173]	@ zero_extendqisi2
	cmp	r9, r3
	bcs	.L1609
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
	beq	.L1607
	cmn	r4, #1
	ldr	ip, [sp, #28]
	moveq	r4, r0
	cmp	r0, r5
	bcc	.L1617
	mov	r8, ip
	mov	fp, ip
.L1607:
	ldr	r9, [sp, #8]
	b	.L1606
.L1617:
	mov	r4, r0
	mov	r8, ip
	mov	fp, ip
.L1609:
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
	bcc	.L1611
	cmn	r4, #1
	movne	r4, #256
.L1611:
	cmn	r4, #1
	movne	r7, #0
	moveq	r7, #1
	cmp	r4, #256
	movne	r1, r7
	orreq	r1, r7, #1
	cmp	r1, #0
	beq	.L1612
	str	r4, [sp]
	mov	r1, r9
	ldr	r0, .L1629+12
	mov	r3, r9
	ldr	r2, [sp, #20]
	bl	printk
	cmp	r10, #0
	bne	.L1613
	ldr	r3, .L1629
	ldrb	r3, [r3, #140]	@ zero_extendqisi2
	cmp	r3, #0
	moveq	r7, #0
	andne	r7, r7, #1
	cmp	r7, #0
	beq	.L1622
	mov	r0, r6
	mov	r1, #3
	bl	micron_auto_read_calibration_config
	mov	r10, #1
	b	.L1615
.L1613:
	mov	r0, r6
	mov	r1, #0
	bl	micron_auto_read_calibration_config
	cmn	r4, #1
	movne	r4, #256
	b	.L1622
.L1612:
	cmp	r10, #0
	beq	.L1622
	mov	r0, r6
	mov	r4, #256
	bl	micron_auto_read_calibration_config
.L1622:
	mov	r0, r4
	add	sp, sp, #36
	@ sp needed
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L1630:
	.align	2
.L1629:
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
	ldr	r5, .L1649
	mov	r10, r2
	mov	r7, r0
	mov	fp, r1
	add	r2, r5, r0
	mvn	r6, #0
	ldr	r3, [r5, #692]
	ldrb	r4, [r2, #710]	@ zero_extendqisi2
	ldrb	r9, [r5, #700]	@ zero_extendqisi2
	ldrb	r3, [r3, #19]	@ zero_extendqisi2
	sub	r3, r3, #7
	cmp	r3, #1
	ldrlsb	r4, [r2, #718]	@ zero_extendqisi2
	bl	NandcWaitFlashReady
	mov	ip, #0
.L1633:
	cmp	ip, r9
	bcs	.L1637
	add	r4, r4, #1
	mov	r0, r7
	ldrb	r1, [r5, #699]	@ zero_extendqisi2
	uxtb	r4, r4
	ldr	r2, .L1649+4
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
	beq	.L1635
	ldrb	r3, [r5, #1792]	@ zero_extendqisi2
	cmn	r6, #1
	moveq	r6, r0
	add	r3, r3, r3, asl #1
	cmp	r0, r3, asr #2
	bcc	.L1642
	mov	r8, #0
	mov	r10, r8
.L1635:
	add	ip, ip, #1
	b	.L1633
.L1642:
	mov	r6, r0
.L1637:
	ldr	r3, [r5, #692]
	add	r7, r5, r7
	ldrb	r3, [r3, #19]	@ zero_extendqisi2
	sub	r3, r3, #7
	cmp	r3, #1
	ldrb	r3, [r5, #1792]	@ zero_extendqisi2
	strlsb	r4, [r7, #718]
	strhib	r4, [r7, #710]
	add	r3, r3, r3, asl #1
	cmp	r6, r3, asr #2
	bcc	.L1641
	cmn	r6, #1
	movne	r6, #256
.L1641:
	mov	r0, r6
	add	sp, sp, #12
	@ sp needed
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L1650:
	.align	2
.L1649:
	.word	.LANCHOR0
	.word	.LANCHOR0+702
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
	ldr	r3, .L1655
	subs	r4, r0, #0
	mov	r5, r1
	mov	r7, r2
	ldrb	r6, [r3, #481]	@ zero_extendqisi2
	bne	.L1652
	ldr	r2, .L1655+4
	ldrb	r3, [r2, #1]	@ zero_extendqisi2
	ldr	r1, [r2, #4]
	mul	r1, r1, r3
	cmp	r5, r1
	bcs	.L1652
	ldrb	r3, [r2]	@ zero_extendqisi2
	cmp	r3, #0
	subeq	r6, r6, #2
	movne	r6, #4
.L1652:
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
.L1656:
	.align	2
.L1655:
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
	ldr	r4, .L1667
	ldr	r5, .L1667+4
	ldr	r8, .L1667+8
	ldr	r3, [r4, #148]
	mov	r9, r4
	ldrb	r0, [r4, #174]	@ zero_extendqisi2
	mov	r10, r5
	str	r3, [r4, #160]
	bl	FlashBchSel
	mov	r1, #0
	mov	r2, #2048
	ldr	r0, [r4, #148]
	bl	ftl_memset
	ldr	r3, [r4, #160]
	ldr	r1, .L1667+12
	mov	r2, #32
	str	r8, [r3]
	ldr	r0, [r4, #160]
	ldrb	r3, [r5, #1717]	@ zero_extendqisi2
	add	r0, r0, #16
	strh	r3, [r0, #-4]	@ movhi
	ldrb	r3, [r5, #1]	@ zero_extendqisi2
	strh	r3, [r0, #-2]	@ movhi
	ldrb	r3, [r5, #1732]	@ zero_extendqisi2
	str	r3, [r0, #1060]
	bl	ftl_memcpy
	ldr	r0, [r4, #160]
	ldr	r1, .L1667+16
	mov	r2, #8
	add	r0, r0, #80
	bl	ftl_memcpy
	ldr	r0, [r4, #160]
	add	r1, r5, #656
	mov	r2, #32
	add	r0, r0, #96
	bl	ftl_memcpy
	ldr	r0, [r4, #160]
	ldr	r1, .L1667+20
	mov	r2, #32
	add	r0, r0, #160
	bl	ftl_memcpy
	ldr	r0, [r4, #160]
	add	r1, r5, #44
	mov	r2, #32
	add	r0, r0, #192
	bl	ftl_memcpy
	ldr	r0, [r4, #160]
	mov	r2, #852
	ldr	r1, .L1667+24
	add	r0, r0, #224
	bl	ftl_memcpy
	ldr	r6, [r4, #160]
	movw	r1, #2036
	add	r0, r6, #12
	bl	js_hash
	movw	r3, #1592
	str	r3, [r6, #4]
	ldr	r3, [r4, #176]
	str	r3, [r4, #160]
	str	r0, [r6, #8]
	mov	r0, #0
	bl	flash_enter_slc_mode
	mov	r6, #0
	mov	r7, r6
.L1660:
	ldr	r1, [r5, #4]
	mov	r0, #0
	mov	r2, r0
	mul	r1, r1, r7
	bl	FlashEraseBlock
	ldr	r1, [r5, #4]
	mov	r0, #0
	ldr	r2, [r4, #148]
	mov	r3, r0
	mul	r1, r1, r7
	bl	FlashProgPage
	ldr	r1, [r5, #4]
	mov	r0, #0
	ldr	r2, [r4, #148]
	mov	r3, r0
	mul	r1, r1, r7
	add	r1, r1, #1
	bl	FlashProgPage
	ldr	r1, [r5, #4]
	mov	r0, #0
	ldr	r2, [r4, #176]
	mov	r3, r0
	mul	r1, r1, r7
	bl	FlashReadRawPage
	add	r2, r7, #1
	cmn	r0, #1
	beq	.L1658
	ldr	fp, [r9, #160]
	ldr	r3, [fp]
	cmp	r3, r8
	bne	.L1658
	add	r0, fp, #12
	movw	r1, #2036
	str	r2, [sp, #4]
	bl	js_hash
	ldr	r3, [fp, #8]
	cmp	r3, r0
	ldr	r2, [sp, #4]
	bne	.L1658
	ldr	r3, [r10, #4]
	cmp	r6, #1
	str	r2, [r9, #168]
	mul	r7, r3, r7
	str	r7, [r9, #164]
	beq	.L1661
	mov	r6, #1
.L1658:
	cmp	r2, #4
	mov	r7, r2
	bne	.L1660
	b	.L1659
.L1661:
	mov	r6, #2
.L1659:
	mov	r0, #0
	bl	flash_exit_slc_mode
	clz	r0, r6
	mov	r0, r0, lsr #5
	rsb	r0, r0, #0
	add	sp, sp, #12
	@ sp needed
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L1668:
	.align	2
.L1667:
	.word	.LANCHOR2
	.word	.LANCHOR0
	.word	1312902724
	.word	.LANCHOR0+1556
	.word	.LANCHOR0+1720
	.word	.LANCHOR1+472
	.word	.LANCHOR0+698
	.fnend
	.size	FlashSavePhyInfo, .-FlashSavePhyInfo
	.align	2
	.global	FlashReadIdbDataRaw
	.type	FlashReadIdbDataRaw, %function
FlashReadIdbDataRaw:
	.fnstart
	@ args = 0, pretend = 0, frame = 16
	@ frame_needed = 0, uses_anonymous_args = 0
	ldr	r3, .L1690
	stmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.save {r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.pad #20
	sub	sp, sp, #20
	ldr	r2, [r3]
	mov	r9, r0
	ldr	r4, .L1690+4
	str	r3, [sp]
	str	r2, [sp, #12]
	ldr	r2, .L1690+8
	ldrb	r10, [r4, #1792]	@ zero_extendqisi2
	ldr	r0, [r2]	@ unaligned
	ldr	r2, [r4, #1728]
	cmp	r2, #0
	str	r0, [sp, #8]	@ unaligned
	beq	.L1670
	mov	r0, #0
	bl	flash_enter_slc_mode
.L1670:
	mov	r0, r9
	mov	r1, #0
	mov	r2, #2048
	mvn	r8, #0
	bl	ftl_memset
	mov	r5, #2
.L1671:
	ldrb	r3, [r4, #1]	@ zero_extendqisi2
	cmp	r5, r3
	bcs	.L1675
	ldr	fp, .L1690+12
	mov	r7, #0
.L1673:
	add	r3, sp, #8
	ldr	r6, .L1690+12
	ldrb	ip, [r3, r7]	@ zero_extendqisi2
	mov	r0, ip
	str	ip, [sp, #4]
	bl	FlashBchSel
	ldr	r1, [r4, #4]
	mov	r0, #0
	ldr	r2, [fp, #148]
	mov	r3, r0
	mul	r1, r1, r5
	bl	FlashReadRawPage
	cmn	r0, #1
	ldr	ip, [sp, #4]
	bne	.L1672
	add	r7, r7, #1
	cmp	r7, #4
	bne	.L1673
	b	.L1674
.L1672:
	ldr	r3, [r6, #148]
	ldr	r2, .L1690+16
	ldr	r3, [r3]
	cmp	r3, r2
	bne	.L1674
	mov	r1, ip
	ldr	r0, .L1690+20
	bl	printk
	mov	r2, #2048
	mov	r0, r9
	ldr	r1, [r6, #148]
	bl	ftl_memcpy
	ldr	r3, [r6, #148]
	ldr	r2, .L1690+4
	ldr	r3, [r3, #512]
	strb	r3, [r2, #1]
	ldr	r3, [r6, #168]
	cmp	r3, r5
	bls	.L1679
	str	r5, [r6, #168]
	bl	FlashSavePhyInfo
	mov	r8, #0
.L1674:
	add	r5, r5, #1
	b	.L1671
.L1679:
	mov	r8, #0
.L1675:
	mov	r0, r10
	bl	FlashBchSel
	ldr	r3, [r4, #1728]
	cmp	r3, #0
	beq	.L1676
	mov	r0, #0
	bl	flash_exit_slc_mode
.L1676:
	ldr	r3, [sp]
	mov	r0, r8
	ldr	r2, [sp, #12]
	ldr	r3, [r3]
	cmp	r2, r3
	beq	.L1678
	bl	__stack_chk_fail
.L1678:
	add	sp, sp, #20
	@ sp needed
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L1691:
	.align	2
.L1690:
	.word	__stack_chk_guard
	.word	.LANCHOR0
	.word	.LANCHOR3+11
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
	ldr	r8, .L1787
	ldr	r4, .L1787+4
	mov	r7, #0
	add	r9, r4, #12
	str	r0, [r8, #148]
	mov	r0, #32768
	bl	ftl_malloc
	str	r0, [r8, #176]
	mov	r0, #4096
	bl	ftl_malloc
	str	r0, [r8, #180]
	mov	r0, #32768
	bl	ftl_malloc
	str	r0, [r8, #184]
	mov	r0, #4096
	bl	ftl_malloc
	mov	r3, #50
	str	r7, [r8, #168]
	strb	r3, [r4, #1]
	strb	r3, [r8, #172]
	mov	r3, #128
	strb	r7, [r4, #1732]
	str	r3, [r4, #4]
	mov	r3, #60
	str	r7, [r8, #152]
	strb	r7, [r4]
	strb	r7, [r8, #192]
	strb	r3, [r8, #174]
	str	r0, [r8, #188]
	mov	r0, r5
	bl	NandcInit
	ldr	r5, .L1787+8
.L1698:
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
	bne	.L1693
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
.L1693:
	mov	r0, fp
	bl	NandcFlashDeCs
	ldrb	r2, [r5]	@ zero_extendqisi2
	sub	r3, r2, #1
	uxtb	r3, r3
	cmp	r3, #253
	bhi	.L1694
	ldrb	r1, [r5, #2]	@ zero_extendqisi2
	ldrb	r3, [r5, #1]	@ zero_extendqisi2
	ldr	r0, .L1787+12
	str	r1, [sp]
	ldrb	r1, [r5, #3]	@ zero_extendqisi2
	str	r1, [sp, #4]
	ldrb	r1, [r5, #4]	@ zero_extendqisi2
	str	r1, [sp, #8]
	ldrb	r1, [r5, #5]	@ zero_extendqisi2
	str	r1, [sp, #12]
	add	r1, r7, #1
	bl	printk
.L1694:
	cmp	r7, #0
	bne	.L1695
	ldrb	r3, [r4, #1556]	@ zero_extendqisi2
	sub	r3, r3, #1
	uxtb	r3, r3
	cmp	r3, #253
	bhi	.L1744
	ldr	r3, .L1787+4
	ldrb	r3, [r3, #1557]	@ zero_extendqisi2
	cmp	r3, #255
	beq	.L1744
.L1695:
	ldrb	r3, [r5]	@ zero_extendqisi2
	add	r7, r7, #1
	add	r5, r5, #8
	cmp	r3, #181
	moveq	r3, #44
	streqb	r3, [r5, #-8]
	cmp	r7, #4
	bne	.L1698
	ldrb	r3, [r4, #1556]	@ zero_extendqisi2
	cmp	r3, #173
	beq	.L1699
	ldr	r3, .L1787+4
	ldr	r0, [r3, #1736]
	bl	NandcSetDdrMode
.L1699:
	mov	r2, #852
	ldr	r0, .L1787+16
	mov	r1, #0
	ldr	r5, .L1787+20
	bl	ftl_memset
	ldr	r6, [r4, #1744]
	ldr	r3, .L1787+24
	cmp	r6, r5
	ldrb	r0, [r4, #1557]	@ zero_extendqisi2
	str	r3, [r4, #692]
	mov	r3, #0
	strb	r3, [r4, #8]
	sub	ip, r0, #218
	ldreq	r3, .L1787+4
	clz	ip, ip
	moveq	r2, #1
	mov	ip, ip, lsr #5
	streq	r2, [r3, #1728]
	cmp	r0, #161
	cmpne	r0, #241
	and	r2, r0, #253
	moveq	r3, #1
	movne	r3, #0
	orr	r3, ip, r3
	cmp	r2, #209
	orreq	r3, r3, #1
	cmp	r3, #0
	bne	.L1701
	cmp	r0, #220
	bne	.L1702
	ldr	r3, .L1787+4
	ldrb	r3, [r3, #1559]	@ zero_extendqisi2
	cmp	r3, #149
	bne	.L1702
.L1701:
	ldrb	lr, [r4, #1556]	@ zero_extendqisi2
	mov	r3, #16
	strb	r3, [r4, #1]
	mov	r1, #1
	strb	r3, [r8, #174]
	cmp	lr, #152
	ldr	r3, .L1787+28
	strb	r1, [r4]
	ldr	r2, .L1787
	strb	lr, [r3, #3257]
	strb	r0, [r3, #3258]
	bne	.L1704
	ldr	r7, .L1787+4
	movw	lr, #1560
	ldrsb	lr, [r7, lr]
	cmp	lr, #0
	strltb	r1, [r2, #192]
	movge	r1, #24
	strgeb	r1, [r2, #174]
.L1704:
	movw	r2, #2049
	cmp	r6, r5
	cmpne	r6, r2
	moveq	r2, #16
	streqb	r2, [r8, #174]
	cmp	ip, #0
	ldrne	r2, .L1787+32
	movne	r1, #2048
	strneh	r1, [r2, #14]	@ movhi
	mvnne	r2, #37
	bne	.L1781
.L1708:
	cmp	r0, #220
	bne	.L1710
	ldr	r2, .L1787+32
	mov	r1, #4096
	strh	r1, [r2, #14]	@ movhi
	mvn	r2, #35
.L1781:
	strb	r2, [r3, #3258]
	b	.L1709
.L1710:
	cmp	r0, #211
	ldreq	r2, .L1787+32
	moveq	r1, #4096
	streqh	r1, [r2, #14]	@ movhi
	moveq	r2, #2
	streqb	r2, [r3, #3269]
.L1709:
	ldr	r1, .L1787+36
	mov	r2, #32
	ldr	r0, .L1787+40
	bl	ftl_memcpy
	ldr	r0, .L1787+24
	mov	r2, #32
	add	r1, r0, #2784
	bl	ftl_memcpy
.L1702:
	ldrb	r3, [r4]	@ zero_extendqisi2
	ldr	r6, .L1787+4
	cmp	r3, #0
	bne	.L1711
	bl	FlashLoadPhyInfoInRam
	cmp	r0, #0
	bne	.L1713
	ldr	r3, [r6, #692]
	ldrh	r3, [r3, #16]
	mov	r3, r3, lsr #8
	tst	r3, #1
	and	r0, r3, #7
	strb	r0, [r6, #1716]
	bne	.L1713
	mov	r3, #1
	strb	r3, [r6, #1732]
	bl	FlashSetInterfaceMode
	ldrb	r0, [r6, #1716]	@ zero_extendqisi2
	bl	NandcSetMode
.L1713:
	ldr	r3, [r4, #692]
	ldr	r6, .L1787+4
	ldrb	r3, [r3, #26]	@ zero_extendqisi2
	strb	r3, [r4, #140]
	bl	FlashLoadPhyInfo
	cmp	r0, #0
	beq	.L1711
	ldr	r3, [r6, #1736]
	cmp	r3, #0
	beq	.L1716
	mov	r0, #1
	bl	FlashSetInterfaceMode
	mov	r0, #1
	b	.L1782
.L1716:
	ldrb	r0, [r6, #1716]	@ zero_extendqisi2
	bl	FlashSetInterfaceMode
	ldrb	r0, [r6, #1716]	@ zero_extendqisi2
.L1782:
	bl	NandcSetMode
	bl	FlashLoadPhyInfo
	cmp	r0, #0
	beq	.L1711
	mov	r0, #1
	ldr	r6, .L1787+4
	bl	FlashSetInterfaceMode
	mov	r0, #1
	bl	NandcSetMode
	ldr	r3, [r4, #692]
	ldr	r0, .L1787+44
	ldrh	r1, [r3, #14]
	bl	printk
	bl	FlashLoadPhyInfoInRam
	cmn	r0, #1
	beq	.L1757
	bl	FlashDieInfoInit
	ldr	r3, [r6, #692]
	ldrb	r0, [r3, #19]	@ zero_extendqisi2
	bl	FlashGetReadRetryDefault
	ldr	r3, .L1787+48
	ldr	r2, [r6, #692]
	ldrh	r3, [r3, #-14]
	ldrb	r1, [r2, #9]	@ zero_extendqisi2
	add	r3, r3, #4080
	add	r3, r3, #15
	cmp	r1, r3, asr #12
	ldrh	r3, [r2, #14]
	blt	.L1718
	add	r0, r3, #255
	cmp	r1, r0, asr #8
	bge	.L1719
.L1718:
	bic	r3, r3, #255
	strh	r3, [r2, #14]	@ movhi
.L1719:
	ldrb	r3, [r4, #1716]	@ zero_extendqisi2
	tst	r3, #6
	beq	.L1720
	bl	FlashSavePhyInfo
	mov	r0, #0
	ldr	r1, [r8, #164]
	bl	FlashDdrParaScan
.L1720:
	bl	FlashSavePhyInfo
.L1711:
	ldr	r2, [r4, #692]
	ldr	r7, .L1787+4
	ldr	r6, .L1787
	ldrb	r3, [r2, #26]	@ zero_extendqisi2
	ldrh	r0, [r2, #10]
	ldrb	r9, [r2, #18]	@ zero_extendqisi2
	strb	r3, [r4, #140]
	ldrh	r3, [r2, #16]
	ubfx	r1, r3, #7, #1
	strb	r1, [r4, #8]
	ubfx	r1, r3, #3, #1
	strb	r1, [r8, #193]
	ubfx	r1, r3, #4, #1
	ubfx	r3, r3, #8, #3
	strb	r1, [r4, #1793]
	strb	r3, [r4, #1716]
	mov	r3, #0
	ldrb	r1, [r2, #12]	@ zero_extendqisi2
	str	r3, [r8, #156]
	bl	__aeabi_idiv
	mov	r1, r0
	mov	r0, r9
	bl	BuildFlashLsbPageTable
	bl	FlashDieInfoInit
	ldr	r3, [r4, #692]
	ldrh	r2, [r3, #16]
	tst	r2, #64
	beq	.L1722
	ldrb	r0, [r3, #19]	@ zero_extendqisi2
	ldrb	r3, [r7, #699]	@ zero_extendqisi2
	strb	r0, [r7, #696]
	strb	r3, [r7, #697]
	ldrb	r3, [r7, #700]	@ zero_extendqisi2
	strb	r3, [r6, #173]
	sub	r3, r0, #1
	cmp	r3, #7
	bhi	.L1723
	ldr	r3, .L1787+52
	sub	r2, r0, #8
	clz	r2, r2
	str	r3, [r6, #156]
	sub	r3, r0, #5
	cmp	r0, #8
	cmpne	r3, #1
	mov	r2, r2, lsr #5
	ldr	r6, .L1787+56
	movls	r3, #1
	strls	r3, [r7, #1784]
	cmp	r0, #7
	beq	.L1726
	sub	r3, r6, #8
	cmp	r2, #0
	moveq	r6, r3
.L1726:
	sub	r2, r6, #1
	add	r6, r6, #31
	mov	r3, #0
.L1727:
	ldrsb	r1, [r2, #1]!
	cmp	r1, #0
	addeq	r3, r3, #1
	cmp	r2, r6
	bne	.L1727
	cmp	r3, #27
	bls	.L1722
	bl	FlashGetReadRetryDefault
	bl	FlashSavePhyInfo
	b	.L1722
.L1723:
	sub	r3, r0, #17
	cmp	r3, #2
	bhi	.L1729
	ldr	r3, .L1787+60
	cmp	r0, #19
	str	r3, [r6, #156]
	moveq	r3, #15
	bne	.L1786
	b	.L1784
.L1729:
	sub	r3, r0, #65
	cmp	r0, #33
	cmpne	r3, #1
	bhi	.L1731
	ldr	r3, .L1787+64
	str	r3, [r6, #156]
	mov	r3, #4
	strb	r3, [r7, #697]
.L1786:
	mov	r3, #7
.L1784:
	strb	r3, [r6, #173]
	b	.L1722
.L1731:
	sub	r3, r0, #67
	sub	r2, r0, #34
	cmp	r3, #1
	movhi	r1, #0
	movls	r1, #1
	cmp	r3, #1
	cmphi	r2, #1
	bhi	.L1732
	ldr	r3, .L1787+64
	cmp	r0, #68
	cmpne	r0, #35
	str	r3, [r6, #156]
	movne	r3, #7
	moveq	r3, #17
	cmp	r1, #0
	strb	r3, [r6, #173]
	movne	r3, #4
	moveq	r3, #5
	strb	r3, [r4, #697]
	b	.L1722
.L1732:
	cmp	r0, #49
	ldreq	r3, .L1787+68
	streq	r3, [r6, #156]
.L1722:
	ldr	r2, [r4, #1744]
	ldr	r3, .L1787+4
	cmp	r2, r5
	bne	.L1736
	ldrb	r1, [r3, #140]	@ zero_extendqisi2
	cmp	r1, #0
	ldrne	r3, [r3, #692]
	movne	r1, #0
	strneb	r1, [r3, #18]
.L1736:
	ldrb	r1, [r4, #1556]	@ zero_extendqisi2
	ldr	r3, .L1787+4
	cmp	r1, #44
	bne	.L1737
	ldrb	r1, [r3, #1732]	@ zero_extendqisi2
	cmp	r1, #0
	beq	.L1737
	cmp	r2, r5
	bne	.L1738
	ldrb	r3, [r3, #140]	@ zero_extendqisi2
	cmp	r3, #0
	bne	.L1737
.L1738:
	mov	r0, #1
	mov	r3, #0
	strb	r3, [r4, #1732]
	bl	FlashSetInterfaceMode
	mov	r0, #1
	bl	NandcSetMode
.L1737:
	ldrb	r3, [r4, #1716]	@ zero_extendqisi2
	tst	r3, #6
	beq	.L1739
	ldr	r2, .L1787+4
	ldrb	r2, [r2, #1732]	@ zero_extendqisi2
	cmp	r2, #0
	bne	.L1740
	tst	r3, #1
	bne	.L1739
.L1740:
	mov	r0, #0
	bl	flash_enter_slc_mode
	mov	r0, #0
	ldr	r1, [r8, #164]
	bl	FlashDdrParaScan
	mov	r0, #0
	bl	flash_exit_slc_mode
.L1739:
	ldr	r3, [r4, #692]
	mov	r8, #16
	ldr	r6, .L1787+4
	ldrb	r0, [r3, #20]	@ zero_extendqisi2
	bl	FlashBchSel
	ldr	r0, .L1787+72
	bl	FlashReadIdbDataRaw
	ldr	r0, .L1787+76
	strb	r8, [r4, #1]
	bl	FlashTimingCfg
	ldr	r5, [r4, #692]
	ldrb	r3, [r5, #12]	@ zero_extendqisi2
	ldrh	r7, [r5, #14]
	strh	r3, [r4, #120]	@ movhi
	ldrb	r3, [r5, #7]	@ zero_extendqisi2
	str	r3, [r4, #116]
	ldr	r3, [r4, #1556]
	str	r3, [r4, #112]
	ldrb	r3, [r4, #1717]	@ zero_extendqisi2
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
	bne	.L1742
	mov	r3, r3, asl #1
	mov	r1, r7, lsr #1
	mov	r2, r2, asl #1
	strh	r3, [r6, #134]	@ movhi
	strb	r8, [r6, #1]
	mov	r3, #8
	strh	r1, [r6, #126]	@ movhi
	strh	r2, [r6, #132]	@ movhi
	strh	r3, [r6, #138]	@ movhi
.L1742:
	ldrb	r0, [r5, #20]	@ zero_extendqisi2
	bl	FlashBchSel
	bl	ftl_flash_suspend
	mov	r0, #0
	b	.L1757
.L1744:
	mvn	r0, #1
.L1757:
	add	sp, sp, #20
	@ sp needed
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L1788:
	.align	2
.L1787:
	.word	.LANCHOR2
	.word	.LANCHOR0
	.word	.LANCHOR0+1556
	.word	.LC96
	.word	.LANCHOR0+698
	.word	1446522928
	.word	.LANCHOR1+472
	.word	.LANCHOR1
	.word	.LANCHOR1+3256
	.word	.LANCHOR1+3132
	.word	.LANCHOR0+44
	.word	.LC97
	.word	.LANCHOR2-2096
	.word	HynixReadRetrial
	.word	.LANCHOR0+726
	.word	MicronReadRetrial
	.word	ToshibaReadRetrial
	.word	SamsungReadRetrial
	.word	.LANCHOR2-1904
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
	ldr	r3, .L1806
	mov	r8, r0
	mov	r9, r1
	ldr	r2, [r3, #692]
	mov	r6, r3
	ldrb	r7, [r2, #19]	@ zero_extendqisi2
	ldrb	r2, [r3, #140]	@ zero_extendqisi2
	cmp	r2, #0
	beq	.L1790
	ldr	r3, [r3, #1728]
	cmp	r3, #0
	ldmnefd	sp!, {r3, r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L1790:
	sub	r3, r7, #5
	cmp	r7, #68
	cmpne	r3, #3
	bls	.L1791
	sub	r3, r7, #19
	tst	r3, #239
	ldmnefd	sp!, {r3, r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L1791:
	ldr	r5, .L1806+4
	movw	fp, #65535
	sub	r10, r5, #3136
.L1793:
	ldr	r3, [r6, #692]
	ldrh	r3, [r3, #10]
	cmp	r3, r4
	bls	.L1805
	mov	r3, r4, asl #1
	ldrh	r3, [r10, r3]
	cmp	r3, fp
	ldmnefd	sp!, {r3, r4, r5, r6, r7, r8, r9, r10, fp, pc}
	cmp	r7, #8
	mov	r2, #32768
	ldr	r0, [r5, #176]
	moveq	r1, #0
	movne	r1, #255
	bl	ftl_memset
	ldr	r3, .L1806+4
	add	r1, r4, r9
	add	r4, r4, #1
	mov	r0, r8
	ldr	r2, [r3, #176]
	uxth	r4, r4
	mov	r3, r2
	bl	FlashProgPage
	b	.L1793
.L1805:
	ldmfd	sp!, {r3, r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L1807:
	.align	2
.L1806:
	.word	.LANCHOR0
	.word	.LANCHOR2
	.fnend
	.size	FlashPageProgMsbFFData, .-FlashPageProgMsbFFData
	.align	2
	.global	FlashReadSlc2KPages
	.type	FlashReadSlc2KPages, %function
FlashReadSlc2KPages:
	.fnstart
	@ args = 0, pretend = 0, frame = 32
	@ frame_needed = 0, uses_anonymous_args = 0
	ldr	r3, .L1860
	stmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.save {r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.pad #44
	sub	sp, sp, #44
	ldr	r9, .L1860+4
	mov	r4, r0
	str	r2, [sp, #16]
	mov	r8, #0
	ldr	r2, [r3]
	mov	r10, r9
	str	r1, [sp, #12]
	str	r3, [sp, #20]
	str	r2, [sp, #36]
	ldr	r2, .L1860+8
	ldrb	r2, [r2, #481]	@ zero_extendqisi2
	str	r2, [sp, #8]
.L1809:
	ldr	r3, [sp, #12]
	cmp	r8, r3
	beq	.L1859
	ldr	r3, [sp, #12]
	add	r2, sp, #32
	mov	r0, r4
	ldr	r1, [sp, #16]
	rsb	r3, r8, r3
	uxtb	r3, r3
	str	r3, [sp]
	add	r3, sp, #28
	bl	LogAddr2PhyAddr
	ldrb	r2, [r9, #1717]	@ zero_extendqisi2
	ldr	r3, [sp, #28]
	cmp	r3, r2
	mvncs	r3, #0
	strcs	r3, [r4]
	bcs	.L1811
	add	r3, r9, r3
	mov	r7, #0
	ldrb	r5, [r3, #1720]	@ zero_extendqisi2
	mov	r0, r5
	bl	NandcWaitFlashReady
	mov	r0, r5
	bl	NandcFlashCs
.L1812:
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
	ldr	r3, .L1860+12
	ldrb	r3, [r3, #192]	@ zero_extendqisi2
	cmp	r3, #0
	mov	r6, r0
	beq	.L1813
	mov	r0, r5
	bl	flash_read_ecc
	cmp	r0, #5
	movhi	r6, #256
.L1813:
	cmp	r7, #9
	cmnls	r6, #1
	moveq	r3, #1
	movne	r3, #0
	addeq	r7, r7, #1
	beq	.L1812
.L1814:
	cmp	r7, #0
	mov	r7, r3
	movne	r6, #256
.L1816:
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
	ldr	r3, .L1860+12
	ldrb	r2, [r3, #192]	@ zero_extendqisi2
	cmp	r2, #0
	mov	fp, r0
	beq	.L1819
	mov	r0, r5
	bl	flash_read_ecc
	cmp	r0, #5
	movhi	fp, #256
.L1819:
	cmp	r7, #9
	cmnls	fp, #1
	addeq	r7, r7, #1
	beq	.L1816
.L1820:
	cmp	r7, #0
	mov	r0, r5
	movne	fp, #256
	bl	NandcFlashDeCs
	ldrb	r2, [r10, #1792]	@ zero_extendqisi2
	cmp	fp, r6
	movcs	r3, fp
	movcc	r3, r6
	add	r2, r2, r2, asl #1
	cmp	r3, r2, asr #2
	bls	.L1822
	cmn	r3, #1
	movne	r3, #256
.L1822:
	cmp	r3, #256
	cmnne	r3, #1
	movne	r3, #0
	str	r3, [r4]
	ldr	r3, [r4, #12]
	cmp	r3, #0
	beq	.L1825
	ldr	r2, [r3, #12]
	cmn	r2, #1
	bne	.L1825
	ldr	r2, [r3, #8]
	cmn	r2, #1
	bne	.L1825
	ldr	r3, [r3]
	cmn	r3, #1
	strne	r2, [r4]
.L1825:
	ldr	r3, [r4]
	cmn	r3, #1
	bne	.L1811
	ldr	r1, [r4, #4]
	ldr	r0, .L1860+16
	ldrb	r2, [r10, #1792]	@ zero_extendqisi2
	bl	printk
	ldr	r1, [r4, #8]
	cmp	r1, #0
	beq	.L1827
	ldr	r0, .L1860+20
	mov	r2, #4
	mov	r3, #8
	bl	rknand_print_hex
.L1827:
	ldr	r1, [r4, #12]
	cmp	r1, #0
	beq	.L1811
	mov	r2, #4
	ldr	r0, .L1860+24
	mov	r3, r2
	bl	rknand_print_hex
.L1811:
	add	r8, r8, #1
	add	r4, r4, #36
	b	.L1809
.L1859:
	ldr	r3, [sp, #20]
	mov	r0, #0
	ldr	r2, [sp, #36]
	ldr	r3, [r3]
	cmp	r2, r3
	beq	.L1830
	bl	__stack_chk_fail
.L1830:
	add	sp, sp, #44
	@ sp needed
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L1861:
	.align	2
.L1860:
	.word	__stack_chk_guard
	.word	.LANCHOR0
	.word	.LANCHOR1
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
	@ args = 0, pretend = 0, frame = 48
	@ frame_needed = 0, uses_anonymous_args = 0
	ldr	r3, .L1938
	stmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.save {r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.pad #60
	sub	sp, sp, #60
	ldr	r3, [r3]
	ldr	r6, .L1938+4
	str	r1, [sp, #20]
	str	r3, [sp, #52]
	ldr	r3, .L1938+8
	ldrb	r8, [r6]	@ zero_extendqisi2
	str	r2, [sp, #24]
	ldrb	r3, [r3, #481]	@ zero_extendqisi2
	cmp	r8, #0
	str	r3, [sp, #16]
	ldrb	r3, [r6, #8]	@ zero_extendqisi2
	str	r3, [sp, #28]
	beq	.L1900
	bl	FlashReadSlc2KPages
	b	.L1864
.L1900:
	ldr	r10, .L1938+12
	mov	r9, r0
	mov	fp, r8
.L1863:
	ldr	r3, [sp, #20]
	cmp	fp, r3
	bcs	.L1936
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
	ldrb	r2, [r6, #1717]	@ zero_extendqisi2
	ldr	r3, [sp, #44]
	mov	r5, r0
	cmp	r3, r2
	ldrcs	r2, [sp, #8]
	mvncs	r3, #0
	strcs	r3, [r9, r2]
	bcs	.L1866
	add	r3, r6, r3
	ldrb	r4, [r3, #1720]	@ zero_extendqisi2
	ldrb	r3, [r10, #193]	@ zero_extendqisi2
	cmp	r3, #0
	mov	r0, r4
	moveq	r5, #0
	bl	NandcWaitFlashReady
	ldr	r3, .L1938+4
	ldr	r3, [r3, #692]
	ldrb	r2, [r3, #19]	@ zero_extendqisi2
	sub	r3, r2, #1
	cmp	r3, #7
	bhi	.L1868
	sub	r2, r2, #7
	add	r1, r6, r4
	cmp	r2, #1
	ldr	r2, .L1938+4
	ldrb	r3, [r1, #710]	@ zero_extendqisi2
	add	r2, r2, r4
	ldrlsb	r3, [r1, #718]	@ zero_extendqisi2
	ldrb	r2, [r2, #1552]	@ zero_extendqisi2
	cmp	r2, r3
	beq	.L1868
	mov	r0, r4
	ldrb	r1, [r6, #699]	@ zero_extendqisi2
	ldr	r2, .L1938+16
	bl	HynixSetRRPara
.L1868:
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
	beq	.L1870
	ldr	r3, .L1938+4
	ldrb	r3, [r3, #140]	@ zero_extendqisi2
	cmp	r3, #0
	beq	.L1870
	bl	flash_enter_slc_mode
	b	.L1871
.L1870:
	bl	flash_exit_slc_mode
.L1871:
	ldr	r1, [sp, #48]
	cmn	r1, #1
	cmpeq	r4, #255
	moveq	r3, #0
	movne	r3, #1
	moveq	r5, r3
	beq	.L1873
	cmp	r5, #0
	beq	.L1874
	ldr	r2, [r6, #4]
	mov	r0, r4
	add	r2, r1, r2
	bl	FlashReadDpCmd
	b	.L1875
.L1874:
	mov	r0, r4
	bl	FlashReadCmd
.L1875:
	mov	r0, r4
	bl	NandcWaitFlashReady
	cmp	r5, #0
	beq	.L1873
	mov	r0, r4
	ldr	r1, [sp, #48]
	bl	FlashReadDpDataOutCmd
.L1873:
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
	bne	.L1871
.L1876:
	cmp	r5, #0
	beq	.L1877
	ldr	r3, .L1938+4
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
.L1877:
	mov	r0, r4
	str	ip, [sp, #32]
	bl	NandcFlashDeCs
	ldr	ip, [sp, #32]
	ldrb	r3, [sp, #28]	@ zero_extendqisi2
	cmn	ip, #1
	strb	r3, [r6, #8]
	bne	.L1884
	ldrb	r3, [r6, #1732]	@ zero_extendqisi2
	cmp	r3, #0
	bne	.L1879
.L1883:
	ldr	r5, [r10, #156]
	cmp	r5, #0
	bne	.L1880
	b	.L1937
.L1879:
	ldr	r3, [r6, #76]
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
	beq	.L1882
	ldrb	r3, [r6, #1792]	@ zero_extendqisi2
	cmp	r0, r3, lsr #1
	bls	.L1903
.L1882:
	ubfx	r0, r5, #8, #8
	str	ip, [sp, #32]
	bl	NandcSetDdrPara
	ldr	ip, [sp, #32]
	cmn	ip, #1
	beq	.L1883
	b	.L1903
.L1880:
	mov	r0, r4
	ldr	r1, [sp, #48]
	ldr	r2, [r7, #8]
	ldr	r3, [r7, #12]
	blx	r5
	cmn	r0, #1
	mov	ip, r0
	bne	.L1905
	ldr	r3, [r6, #692]
	ldrb	r3, [r3, #19]	@ zero_extendqisi2
	sub	r3, r3, #1
	cmp	r3, #7
	bhi	.L1886
	mov	r0, r4
	ldrb	r1, [r6, #699]	@ zero_extendqisi2
	ldr	r2, .L1938+16
	mov	r3, #0
	bl	HynixSetRRPara
.L1886:
	ldr	r1, [sp, #48]
	mov	r0, r4
	ldr	r2, [r7, #8]
	ldr	r3, [r7, #12]
	bl	FlashReadRawPage
	ldr	r1, [r7, #4]
	ldrb	r2, [r6, #1792]	@ zero_extendqisi2
	mov	ip, r0
	ldr	r0, .L1938+20
	mov	r3, ip
	str	ip, [sp, #32]
	bl	printk
	ldr	ip, [sp, #32]
	cmn	ip, #1
	bne	.L1905
	ldrb	r5, [r6, #140]	@ zero_extendqisi2
	cmp	r5, #0
	beq	.L1885
	ldr	r3, [sp, #12]
	mov	r0, r4
	cmp	r3, #0
	beq	.L1887
	bl	flash_enter_slc_mode
	b	.L1888
.L1887:
	bl	flash_exit_slc_mode
.L1888:
	ldr	ip, [r10, #156]
	mov	r0, r4
	ldr	r1, [sp, #48]
	ldr	r2, [r7, #8]
	ldr	r3, [r7, #12]
	blx	ip
	mov	ip, r0
	b	.L1905
.L1937:
	mov	r0, r4
	ldr	r1, [sp, #48]
	ldr	r2, [r7, #8]
	ldr	r3, [r7, #12]
	bl	FlashReadRawPage
	mov	ip, r0
	b	.L1885
.L1903:
	mov	r5, #0
.L1884:
	ldrb	r3, [r6, #1792]	@ zero_extendqisi2
	add	r3, r3, r3, asl #1
	cmp	ip, r3, asr #2
	bls	.L1885
	ldr	r3, [r10, #156]
	cmp	r3, #0
	moveq	ip, #256
	b	.L1885
.L1905:
	mov	r5, #0
.L1885:
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
	bne	.L1892
	ldr	r1, [r7, #4]
	ldr	r0, .L1938+24
	ldrb	r2, [r6, #1792]	@ zero_extendqisi2
	bl	printk
	ldr	r1, [r7, #12]
	cmp	r1, #0
	beq	.L1892
	mov	r2, #4
	ldr	r0, .L1938+28
	mov	r3, r2
	bl	rknand_print_hex
.L1892:
	cmp	r5, #0
	beq	.L1894
	ldrb	r3, [r6, #1792]	@ zero_extendqisi2
	add	r3, r3, r3, asl #1
	cmp	r8, r3, asr #2
	bls	.L1895
	ldr	r3, [r10, #156]
	cmp	r3, #0
	moveq	r8, #256
.L1895:
	ldr	r3, [sp, #8]
	cmp	r8, #256
	cmnne	r8, #1
	add	r3, r3, #36
	movne	r2, #0
	streq	r8, [r9, r3]
	strne	r2, [r9, r3]
.L1894:
	ldr	r3, [sp, #12]
	add	fp, fp, r5
	cmp	r3, #0
	beq	.L1866
	ldrb	r3, [r6, #140]	@ zero_extendqisi2
	cmp	r3, #0
	beq	.L1866
	mov	r0, r4
	bl	flash_exit_slc_mode
.L1866:
	add	fp, fp, #1
	b	.L1863
.L1936:
	mov	r0, #0
.L1864:
	ldr	r3, .L1938
	ldr	r2, [sp, #52]
	ldr	r3, [r3]
	cmp	r2, r3
	beq	.L1899
	bl	__stack_chk_fail
.L1899:
	add	sp, sp, #60
	@ sp needed
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L1939:
	.align	2
.L1938:
	.word	__stack_chk_guard
	.word	.LANCHOR0
	.word	.LANCHOR1
	.word	.LANCHOR2
	.word	.LANCHOR0+702
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
	.pad #60
	sub	sp, sp, #60
	ldr	r7, .L1954
	mov	r1, #0
	ldr	r8, .L1954+4
	mov	r2, #16
	ldr	r4, .L1954+8
	mov	r5, #0
	ldr	r3, [r7]
	mov	r10, r5
	ldrh	ip, [r8, #126]
	add	r0, r4, #196
	mvn	fp, #0
	mov	r9, r4
	str	r3, [sp, #52]
	ldrh	r3, [r8, #124]
	smulbb	r3, r3, ip
	uxth	r6, r3
	bl	ftl_memset
	ldr	r3, [r4, #180]
	str	r5, [sp, #24]
	str	r3, [sp, #28]
	str	r7, [sp, #4]
.L1941:
	ldrb	r3, [r8, #1717]	@ zero_extendqisi2
	uxtb	r7, r5
	cmp	r3, r7
	bls	.L1953
	mul	ip, r6, r7
	sub	r4, r6, #1
	sub	r3, r6, #12
	uxth	r4, r4
.L1942:
	cmp	r4, r3
	ble	.L1944
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
	beq	.L1943
	ldr	r2, [r9, #180]
	ldrh	r1, [r2]
	movw	r2, #61664
	cmp	r1, r2
	bne	.L1943
	mov	r1, r7
	ldr	r0, .L1954+12
	mov	r2, r4
	add	r10, r10, #1
	bl	printk
	ldr	r3, .L1954+8
	uxth	r10, r10
	add	r7, r3, r7, asl #1
	strh	r4, [r7, #196]	@ movhi
	b	.L1944
.L1943:
	sub	r4, r4, #1
	uxth	r4, r4
	b	.L1942
.L1944:
	ldrb	r3, [r8, #1717]	@ zero_extendqisi2
	add	r5, r5, #1
	cmp	r3, r10
	moveq	fp, #0
	b	.L1941
.L1953:
	ldr	r3, [sp, #4]
	mov	r0, fp
	ldr	r2, [sp, #52]
	ldr	r3, [r3]
	cmp	r2, r3
	beq	.L1948
	bl	__stack_chk_fail
.L1948:
	add	sp, sp, #60
	@ sp needed
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L1955:
	.align	2
.L1954:
	.word	__stack_chk_guard
	.word	.LANCHOR0
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
	mov	r9, r2
	ldr	r8, .L1988
	str	r3, [sp, #8]
	mov	r6, r1
	ldr	r3, .L1988+4
	mov	r4, r0
	mov	fp, r0
	mov	r7, #0
	ldr	r2, [r3]
	str	r3, [sp, #12]
	str	r2, [sp, #60]
	ldr	r2, .L1988+8
	ldrb	r10, [r2, #481]	@ zero_extendqisi2
.L1957:
	cmp	r7, r6
	beq	.L1987
	rsb	r3, r7, r6
	add	r2, sp, #16
	mov	r0, fp
	mov	r1, r9
	uxtb	r3, r3
	str	r3, [sp]
	add	r3, sp, #20
	bl	LogAddr2PhyAddr
	ldrb	r2, [r8, #1717]	@ zero_extendqisi2
	ldr	r3, [sp, #20]
	cmp	r3, r2
	mvncs	r3, #0
	strcs	r3, [fp]
	bcs	.L1959
	add	r3, r8, r3
	ldrb	r5, [r3, #1720]	@ zero_extendqisi2
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
.L1959:
	add	r7, r7, #1
	add	fp, fp, #36
	b	.L1957
.L1987:
	ldr	r3, [sp, #8]
	cmp	r3, #0
	bne	.L1975
.L1971:
	ldr	r3, [sp, #12]
	mov	r0, #0
	ldr	r2, [sp, #60]
	ldr	r3, [r3]
	cmp	r2, r3
	beq	.L1972
	bl	__stack_chk_fail
.L1975:
	ldr	r5, .L1988+12
	mov	r7, #0
	mov	r8, r5
.L1964:
	cmp	r7, r6
	beq	.L1971
	ldr	r3, [r4]
	cmn	r3, #1
	bne	.L1965
	ldr	r1, [r4, #4]
	ldr	r0, .L1988+16
	bl	printk
	b	.L1966
.L1965:
	rsb	r3, r7, r6
	mov	r1, r9
	add	r2, sp, #16
	mov	r0, r4
	uxtb	r3, r3
	str	r3, [sp]
	add	r3, sp, #20
	bl	LogAddr2PhyAddr
	ldr	r2, [r5, #184]
	mov	r3, #0
	mov	lr, r4
	str	r3, [r2]
	ldr	r2, [r5, #188]
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
	ldr	r3, [r5, #184]
	str	r3, [sp, #32]
	ldr	r3, [r5, #188]
	str	r3, [sp, #36]
	bl	FlashReadPages
	ldr	r10, [sp, #24]
	cmn	r10, #1
	bne	.L1967
	ldr	r0, .L1988+20
	ldr	r1, [r4, #4]
	bl	printk
	str	r10, [r4]
.L1967:
	ldr	r3, [r4, #12]
	cmp	r3, #0
	beq	.L1968
	ldr	r2, [r3]
	ldr	r3, [r8, #188]
	ldr	r3, [r3]
	cmp	r2, r3
	beq	.L1968
	ldr	r0, .L1988+24
	ldr	r1, [r4, #4]
	bl	printk
	mvn	r3, #0
	str	r3, [r4]
.L1968:
	ldr	r3, [r4, #8]
	cmp	r3, #0
	beq	.L1966
	ldr	r2, [r3]
	ldr	r3, [r8, #184]
	ldr	r3, [r3]
	cmp	r2, r3
	beq	.L1966
	ldr	r0, .L1988+28
	ldr	r1, [r4, #4]
	bl	printk
	mvn	r3, #0
	str	r3, [r4]
.L1966:
	add	r7, r7, #1
	add	r4, r4, #36
	b	.L1964
.L1972:
	add	sp, sp, #68
	@ sp needed
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L1989:
	.align	2
.L1988:
	.word	.LANCHOR0
	.word	__stack_chk_guard
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
	ldr	r5, .L2002
	ldr	r6, .L2002+4
	ldr	r3, [r5, #-2064]
	sub	r9, r6, #76
	ldr	r8, [r5, #-2040]
	sub	r10, r6, #120
	mov	fp, r9
	str	r3, [r5, #220]
	str	r8, [r5, #224]
.L1991:
	ldrh	r3, [r10]
	cmp	r7, r3
	bcs	.L2001
	ldrh	r4, [r9]
	mvn	r3, #0
	movw	ip, #61664
	strh	r3, [r6, #2]!	@ movhi
	add	r4, r4, r3
	uxth	r4, r4
.L1992:
	ldrh	r3, [fp]
	sub	r2, r3, #16
	cmp	r4, r2
	ble	.L1994
	mla	r3, r3, r7, r4
	mov	r1, #1
	ldr	r0, .L2002+8
	mov	r2, r1
	str	ip, [sp, #4]
	mov	r3, r3, asl #10
	str	r3, [r5, #216]
	bl	FlashReadPages
	ldr	r3, [r5, #212]
	cmn	r3, #1
	ldr	ip, [sp, #4]
	beq	.L1993
	ldrh	r3, [r8]
	cmp	r3, ip
	streqh	r4, [r6]	@ movhi
	beq	.L1994
.L1993:
	sub	r4, r4, #1
	uxth	r4, r4
	b	.L1992
.L1994:
	add	r7, r7, #1
	b	.L1991
.L2001:
	mov	r0, #0
	add	sp, sp, #12
	@ sp needed
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L2003:
	.align	2
.L2002:
	.word	.LANCHOR2
	.word	.LANCHOR0+1946
	.word	.LANCHOR2+212
	.fnend
	.size	FtlLoadFactoryBbt, .-FtlLoadFactoryBbt
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
	ldr	r6, .L2017
	.pad #108
	sub	sp, sp, #108
	ldreq	r2, .L2017+4
	mov	r9, r1
	mov	r7, r0, asl #10
	mov	r1, #1
	ldr	r3, [r6]
	mov	r0, sp
	mov	r8, #0
	str	r8, [sp, #8]
	str	r3, [sp, #100]
	movweq	r3, #1874
	ldrne	r3, .L2017+8
	ldreqh	r4, [r2, r3]
	mov	r2, r9
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
	bne	.L2007
.L2008:
	sxth	r4, r8
	sxth	r3, r5
	cmp	r4, r3
	bgt	.L2007
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
	bne	.L2009
	ldr	r3, [sp, #40]
	cmn	r3, #1
	bne	.L2009
	ldr	r3, [sp]
	cmn	r3, #1
	subne	r5, r4, #1
	uxthne	r5, r5
	bne	.L2008
.L2009:
	add	r3, r4, #1
	uxth	r8, r3
	b	.L2008
.L2007:
	ldr	r2, [sp, #100]
	sxth	r0, r5
	ldr	r3, [r6]
	cmp	r2, r3
	beq	.L2012
	bl	__stack_chk_fail
.L2012:
	add	sp, sp, #108
	@ sp needed
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, pc}
.L2018:
	.align	2
.L2017:
	.word	__stack_chk_guard
	.word	.LANCHOR0
	.word	.LANCHOR0+1872
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
	ldr	r4, .L2052
	ldr	r7, .L2052+4
	ldr	r8, .L2052+8
	ldr	r3, [r4, #-2064]
	mov	r9, r4
	ldr	r6, [r4, #-2040]
	str	r3, [r4, #220]
	str	r6, [r4, #224]
	bl	FtlBbtMemInit
	movw	r3, #1870
	ldrh	r5, [r7, r3]
	sub	r5, r5, #1
	uxth	r5, r5
.L2020:
	ldrh	r3, [r8]
	sub	r3, r3, #48
	cmp	r5, r3
	ble	.L2023
	mov	r1, #1
	ldr	r0, .L2052+12
	mov	r2, r1
	mov	r3, r5, asl #10
	str	r3, [r4, #216]
	bl	FlashReadPages
	ldr	r3, [r4, #212]
	cmn	r3, #1
	bne	.L2021
	ldr	r3, [r9, #216]
	mov	r1, #1
	ldr	r0, .L2052+12
	mov	r2, r1
	add	r3, r3, #1
	str	r3, [r9, #216]
	bl	FlashReadPages
.L2021:
	ldr	r3, [r4, #212]
	cmn	r3, #1
	beq	.L2022
	ldrh	r2, [r6]
	movw	r3, #61649
	cmp	r2, r3
	bne	.L2022
	ldr	r2, [r6, #4]
	ldr	r3, .L2052+16
	str	r2, [r7, #1944]
	ldrh	r2, [r6, #8]
	strh	r5, [r3]	@ movhi
	strh	r2, [r3, #4]	@ movhi
	b	.L2023
.L2022:
	sub	r5, r5, #1
	uxth	r5, r5
	b	.L2020
.L2023:
	ldr	r7, .L2052+4
	movw	r2, #65535
	add	r5, r7, #1936
	ldrh	r3, [r5]
	cmp	r3, r2
	beq	.L2037
	ldrh	r3, [r5, #4]
	cmp	r3, r2
	beq	.L2027
	mov	r1, #1
	ldr	r0, .L2052+12
	mov	r2, r1
	mov	r3, r3, asl #10
	str	r3, [r4, #216]
	bl	FlashReadPages
	ldr	r3, [r4, #212]
	cmn	r3, #1
	beq	.L2027
	ldrh	r2, [r6]
	movw	r3, #61649
	cmp	r2, r3
	bne	.L2027
	ldr	r3, [r6, #4]
	ldr	r2, [r7, #1944]
	cmp	r3, r2
	strhi	r3, [r7, #1944]
	ldrhih	r2, [r5, #4]
	ldrhih	r3, [r6, #8]
	strhih	r2, [r5]	@ movhi
	strhih	r3, [r5, #4]	@ movhi
.L2027:
	ldrh	r0, [r5]
	mov	r1, #1
	bl	FtlGetLastWrittenPage
	movw	r8, #61649
	uxth	r7, r0
	add	r0, r0, #1
	strh	r0, [r5, #2]	@ movhi
.L2029:
	sxth	r3, r7
	cmp	r3, #0
	blt	.L2034
	ldrh	r2, [r5]
	mov	r1, #1
	ldr	r0, .L2052+12
	orr	r3, r3, r2, asl #10
	str	r3, [r4, #216]
	ldr	r3, [r4, #-2064]
	mov	r2, r1
	str	r3, [r4, #220]
	bl	FlashReadPages
	ldr	r3, [r4, #212]
	cmn	r3, #1
	beq	.L2030
	ldrh	r3, [r6]
	cmp	r3, r8
	bne	.L2030
.L2034:
	ldrh	r2, [r6, #10]
	ldrh	r0, [r6, #12]
	ldr	r3, .L2052+4
	strh	r2, [r5, #6]	@ movhi
	movw	r2, #65535
	cmp	r0, r2
	bne	.L2031
	b	.L2032
.L2030:
	sub	r7, r7, #1
	uxth	r7, r7
	b	.L2029
.L2031:
	ldr	r2, [r3, #1800]
	cmp	r0, r2
	beq	.L2032
	movw	r1, #1814
	ldrh	r3, [r3, r1]
	mov	r3, r3, lsr #2
	cmp	r0, r3
	cmpcc	r2, r3
	bcs	.L2032
	bl	FtlSysBlkNumInit
.L2032:
	ldr	r6, .L2052+20
	mov	r5, #0
	ldr	r8, .L2052+24
	sub	r7, r6, #134
.L2035:
	ldrh	r3, [r7]
	cmp	r5, r3
	bcs	.L2051
	ldrh	r2, [r8]
	ldr	r1, [r4, #220]
	ldr	r0, [r6, #4]!
	mov	r2, r2, asl #2
	mla	r1, r5, r2, r1
	bl	ftl_memcpy
	add	r5, r5, #1
	b	.L2035
.L2051:
	mov	r0, #0
	ldmfd	sp!, {r3, r4, r5, r6, r7, r8, r9, pc}
.L2037:
	mvn	r0, #0
	ldmfd	sp!, {r3, r4, r5, r6, r7, r8, r9, pc}
.L2053:
	.align	2
.L2052:
	.word	.LANCHOR2
	.word	.LANCHOR0
	.word	.LANCHOR0+1870
	.word	.LANCHOR2+212
	.word	.LANCHOR0+1936
	.word	.LANCHOR0+1960
	.word	.LANCHOR2-1976
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
	ldr	r7, .L2139
	mov	r1, r5
	ldr	r4, .L2139+4
	.pad #36
	sub	sp, sp, #36
	movw	r6, #1892
	ldr	r2, [r7, #1900]
	sub	r3, r4, #1920
	ldr	r0, [r4, #-1984]
	mov	r8, r4
	strh	r5, [r3, #-8]	@ movhi
	movw	r3, #1918
	mov	r2, r2, asl #2
	strh	r5, [r7, r3]	@ movhi
	bl	ftl_memset
	ldr	r2, [r7, #1900]
	mov	r1, r5
	ldr	r0, [r4, #-2012]
	mov	r2, r2, asl #1
	bl	ftl_memset
	ldrh	r2, [r7, r6]
	mov	r1, r5
	ldr	r0, [r4, #-2000]
	mov	r2, r2, asl #2
	bl	ftl_memset
	ldrh	r2, [r7, r6]
	ldr	r0, [r7, #1920]
	mov	r1, r5
	mov	r2, r2, asl #1
	bl	ftl_memset
	ldr	r0, .L2139+8
	mov	r1, #255
	mov	r2, #16
	bl	ftl_memset
	movw	r3, #1812
	ldrh	r3, [r7, r3]
	str	r7, [sp, #20]
	mov	r7, r4
	str	r3, [sp, #8]
.L2055:
	ldr	r3, .L2139+12
	ldr	r2, [sp, #8]
	ldr	r0, .L2139
	ldrh	r3, [r3]
	cmp	r3, r2
	bls	.L2096
	ldr	r1, .L2139+16
	mov	r6, #36
	ldr	r3, .L2139+20
	mov	r5, #0
	ldr	ip, [r4, #-2076]
	sub	r9, r1, #52
	ldr	r2, [r4, #-3212]
	ldrh	r3, [r3]
	ldr	fp, [r4, #-3208]
	ldrh	r10, [r1]
	str	r5, [sp, #4]
.L2097:
	uxth	r1, r5
	cmp	r1, r3
	bcs	.L2134
	ldr	r1, [sp, #8]
	ldrb	r0, [r9, r5]	@ zero_extendqisi2
	str	r2, [sp, #28]
	str	ip, [sp, #24]
	str	r3, [sp, #16]
	bl	V2P_block
	str	r0, [sp, #12]
	bl	FtlBbmIsBadBlock
	cmp	r0, #0
	ldr	r1, [sp, #12]
	ldr	r3, [sp, #16]
	ldr	ip, [sp, #24]
	ldr	r2, [sp, #28]
	bne	.L2056
	ldr	r0, [sp, #4]
	mov	r1, r1, asl #10
	mla	r0, r6, r0, ip
	stmib	r0, {r1, r2}
	ldr	r1, [sp, #4]
	mul	r1, r10, r1
	add	lr, r1, #3
	cmp	r1, #0
	movlt	r1, lr
	bic	r1, r1, #3
	add	r1, fp, r1
	str	r1, [r0, #12]
	ldr	r1, [sp, #4]
	add	r0, r1, #1
	uxth	r1, r0
	str	r1, [sp, #4]
.L2056:
	add	r5, r5, #1
	b	.L2097
.L2134:
	ldr	r3, [sp, #4]
	cmp	r3, #0
	beq	.L2059
	mov	r1, r3
	ldr	r0, [r4, #-2076]
	mov	r2, #1
	ldr	r9, .L2139
	bl	FlashReadPages
	mov	r3, #0
	str	r3, [sp, #12]
.L2060:
	ldrh	r3, [sp, #12]
	ldr	r2, [sp, #4]
	cmp	r3, r2
	bcs	.L2059
	ldr	r3, [sp, #12]
	mov	r10, #36
	mul	r10, r10, r3
	ldr	r3, [r4, #-2076]
	add	r2, r3, r10
	ldr	r3, [r3, r10]
	ldr	r5, [r2, #4]
	cmn	r3, #1
	ldr	r6, [r2, #12]
	ubfx	r5, r5, #10, #16
	bne	.L2062
	mov	fp, #16
	movw	r3, #65535
.L2061:
	ldr	r0, [r4, #-2076]
	mov	r1, #1
	str	r3, [sp, #16]
	add	r0, r0, r10
	ldr	r2, [r0, #4]
	add	r2, r2, #1
	str	r2, [r0, #4]
	mov	r2, r1
	bl	FlashReadPages
	ldrh	r2, [r6]
	ldr	r3, [sp, #16]
	cmp	r2, r3
	ldreq	r3, [r8, #-2076]
	mvneq	r2, #0
	streq	r2, [r3, r10]
	beq	.L2062
.L2063:
	ldr	r2, [r7, #-2076]
	ldr	r2, [r2, r10]
	cmn	r2, #1
	bne	.L2062
	sub	fp, fp, #1
	uxth	fp, fp
	cmp	fp, #0
	bne	.L2061
.L2062:
	ldr	r3, [r7, #-2076]
	ldr	r3, [r3, r10]
	cmn	r3, #1
	beq	.L2065
	ldr	r2, [r7, #-3848]
	ldr	r3, [r6, #4]
	cmn	r2, #1
	beq	.L2066
	cmp	r2, r3
	bhi	.L2067
.L2066:
	cmn	r3, #1
	addne	r2, r3, #1
	strne	r2, [r8, #-3848]
.L2067:
	ldrh	r2, [r6]
	movw	r1, #61604
	cmp	r2, r1
	beq	.L2069
	bhi	.L2070
	movw	r3, #61574
	cmp	r2, r3
	bne	.L2068
	ldr	r2, .L2139+24
	ldr	lr, [r8, #-2000]
	ldrh	ip, [r2]
	ldrh	r1, [r2, #26]
	sub	r0, ip, #1
	rsb	r2, r1, r0
	str	r2, [sp, #16]
	uxth	r3, r0
	b	.L2083
.L2070:
	movw	r3, #61634
	cmp	r2, r3
	beq	.L2072
	movw	r3, #65535
	cmp	r2, r3
	moveq	r0, r5
	beq	.L2133
	b	.L2068
.L2072:
	ldr	r3, [sp, #20]
	ldr	fp, .L2139+28
	ldr	lr, [r4, #-1984]
	ldr	ip, [r3, #1900]
	ldrh	r2, [fp]
	uxth	r1, ip
	sub	r3, r1, #1
	rsb	r1, r2, r1
	sub	r1, r1, #1
	uxth	r3, r3
	sxth	r1, r1
	str	r1, [sp, #24]
.L2074:
	ldr	r1, [sp, #24]
	sxth	r0, r3
	cmp	r0, r1
	ble	.L2135
	mov	r10, r0, asl #2
	ldr	r1, [r6, #4]
	str	r10, [sp, #16]
	ldr	r10, [lr, r0, asl #2]
	cmp	r1, r10
	bls	.L2075
	ldr	r1, [lr]
	cmp	r1, #0
	bne	.L2076
	cmp	r2, ip
	addne	r2, r2, #1
	strneh	r2, [fp]	@ movhi
.L2076:
	uxth	lr, r3
	mov	r1, #0
.L2077:
	uxth	r2, r1
	cmp	r2, lr
	bcs	.L2136
	ldr	ip, [r7, #-1984]
	sxth	r2, r2
	add	r1, r1, #1
	add	r10, ip, r2, asl #2
	ldr	r10, [r10, #4]
	str	r10, [ip, r2, asl #2]
	mov	r2, r2, asl #1
	ldr	ip, [r7, #-2012]
	add	r10, ip, r2
	ldrh	r10, [r10, #2]
	strh	r10, [ip, r2]	@ movhi
	b	.L2077
.L2136:
	ldr	r2, [r4, #-1984]
	mov	r0, r0, asl #1
	ldr	r1, [r6, #4]
	ldr	ip, [sp, #16]
	str	r1, [r2, ip]
	ldr	r2, [r4, #-2012]
	strh	r5, [r2, r0]	@ movhi
	sxth	r0, r3
	cmp	r0, #0
	bge	.L2079
	b	.L2068
.L2075:
	sub	r3, r3, #1
	uxth	r3, r3
	b	.L2074
.L2135:
	cmp	r0, #0
	bge	.L2110
	b	.L2068
.L2079:
	ldr	r2, .L2139+28
	ldr	r1, [r9, #1900]
	ldrh	r2, [r2]
	rsb	r1, r2, r1
	sub	r1, r1, #1
	sxth	r1, r1
	cmp	r0, r1
	bgt	.L2068
.L2110:
	add	r2, r2, #1
	ldr	r1, [r6, #4]
	strh	r2, [fp]	@ movhi
	sxth	r3, r3
	ldr	r2, [r4, #-1984]
	str	r1, [r2, r3, asl #2]
	mov	r3, r3, asl #1
	ldr	r2, [r4, #-2012]
	b	.L2131
.L2089:
	ldr	r10, [lr, r2, asl #2]
	mov	fp, r2, asl #2
	ldr	r0, [r6, #4]
	cmp	r0, r10
	bhi	.L2137
	sub	r3, r3, #1
	uxth	r3, r3
.L2083:
	ldr	r0, [sp, #16]
	sxth	r2, r3
	cmp	r2, r0
	bgt	.L2089
	b	.L2088
.L2137:
	ldr	r0, [lr]
	cmp	r0, #0
	bne	.L2085
	cmp	r1, ip
	addne	r1, r1, #1
	ldrne	r0, .L2139+32
	strneh	r1, [r0]	@ movhi
.L2085:
	uxth	lr, r3
	mov	r0, #0
.L2086:
	uxth	r1, r0
	cmp	r1, lr
	bcs	.L2138
	ldr	ip, [r4, #-2000]
	sxth	r1, r1
	add	r0, r0, #1
	add	r10, ip, r1, asl #2
	ldr	r10, [r10, #4]
	str	r10, [ip, r1, asl #2]
	mov	r1, r1, asl #1
	ldr	ip, [r9, #1920]
	add	r10, ip, r1
	ldrh	r10, [r10, #2]
	strh	r10, [ip, r1]	@ movhi
	b	.L2086
.L2138:
	ldr	r1, [r8, #-2000]
	mov	r2, r2, asl #1
	ldr	r0, [r6, #4]
	str	r0, [r1, fp]
	ldr	r1, .L2139
	ldr	r1, [r1, #1920]
	strh	r5, [r1, r2]	@ movhi
.L2088:
	sxth	r3, r3
	cmp	r3, #0
	blt	.L2068
	ldr	r0, .L2139+32
	ldrh	r2, [r0, #-26]
	ldrh	r1, [r0]
	sub	r2, r2, #1
	rsb	r2, r1, r2
	sxth	r2, r2
	cmp	r3, r2
	bgt	.L2068
	add	r1, r1, #1
	ldr	r2, [r8, #-2000]
	strh	r1, [r0]	@ movhi
	ldr	r1, [r6, #4]
	str	r1, [r2, r3, asl #2]
	mov	r3, r3, asl #1
	ldr	r2, [r9, #1920]
.L2131:
	strh	r5, [r2, r3]	@ movhi
	b	.L2068
.L2069:
	ldr	r2, .L2139+8
	movw	r1, #65535
	ldrh	r0, [r2]
	cmp	r0, r1
	streqh	r5, [r2]	@ movhi
	beq	.L2132
	ldrh	r0, [r2, #4]
	cmp	r0, r1
	beq	.L2091
	mov	r1, #1
	bl	FtlFreeSysBlkQueueIn
.L2091:
	ldr	r3, [r6, #4]
	ldr	r2, [r8, #-3808]
	cmp	r2, r3
	ldr	r3, .L2139+8
	strcsh	r5, [r3, #4]	@ movhi
	bcs	.L2068
	ldrh	r2, [r3]
	strh	r5, [r3]	@ movhi
	strh	r2, [r3, #4]	@ movhi
	ldr	r3, [r6, #4]
.L2132:
	str	r3, [r7, #-3808]
	b	.L2068
.L2065:
	ldrb	r1, [r9, #140]	@ zero_extendqisi2
	mov	r0, r5
	cmp	r1, #0
	beq	.L2093
.L2133:
	mov	r1, #0
.L2093:
	bl	FtlFreeSysBlkQueueIn
.L2068:
	ldr	r3, [sp, #12]
	add	r3, r3, #1
	str	r3, [sp, #12]
	b	.L2060
.L2059:
	ldr	r3, [sp, #8]
	add	r5, r3, #1
	uxth	r3, r5
	str	r3, [sp, #8]
	b	.L2055
.L2096:
	ldr	r3, .L2139+4
	ldr	ip, [r3, #-2012]
	ldrh	r2, [ip]
	cmp	r2, #0
	beq	.L2098
.L2101:
	ldr	r0, [r0, #1920]
	ldr	r3, .L2139
	ldrh	r2, [r0]
	cmp	r2, #0
	beq	.L2099
	b	.L2122
.L2098:
	sub	r3, r3, #1920
	ldrh	r3, [r3, #-8]
	cmp	r3, #0
	ldrne	lr, [r0, #1900]
	beq	.L2101
.L2102:
	uxth	r3, r2
	sxth	r1, r3
	cmp	r1, lr
	bcs	.L2101
	mov	r5, r1, asl #1
	add	r2, r2, #1
	ldrh	r5, [ip, r5]
	cmp	r5, #0
	beq	.L2102
	mov	r6, #0
.L2103:
	ldr	ip, [r0, #1900]
	sxth	r2, r3
	cmp	r2, ip
	bcs	.L2101
	ldr	lr, [r4, #-2012]
	mov	ip, r2, asl #1
	rsb	r5, r1, r2
	add	r3, r3, #1
	ldrh	r8, [lr, ip]
	mov	r7, r5, asl #1
	uxth	r3, r3
	strh	r8, [lr, r7]	@ movhi
	ldr	lr, [r4, #-1984]
	ldr	r2, [lr, r2, asl #2]
	str	r2, [lr, r5, asl #2]
	ldr	r2, [r4, #-2012]
	strh	r6, [r2, ip]	@ movhi
	b	.L2103
.L2099:
	movw	r1, #1918
	ldrh	r1, [r3, r1]
	cmp	r1, #0
	movwne	r1, #1892
	ldrneh	ip, [r3, r1]
	beq	.L2122
.L2106:
	uxth	r3, r2
	sxth	r1, r3
	cmp	r1, ip
	bge	.L2122
	mov	lr, r1, asl #1
	add	r2, r2, #1
	ldrh	lr, [r0, lr]
	cmp	lr, #0
	beq	.L2106
	ldr	r0, .L2139
	mov	r7, #0
	ldr	r6, .L2139+24
.L2107:
	ldrh	ip, [r6]
	sxth	r2, r3
	cmp	r2, ip
	bge	.L2122
	ldr	lr, [r0, #1920]
	mov	ip, r2, asl #1
	rsb	r5, r1, r2
	add	r3, r3, #1
	ldrh	r9, [lr, ip]
	mov	r8, r5, asl #1
	uxth	r3, r3
	strh	r9, [lr, r8]	@ movhi
	ldr	lr, [r4, #-2000]
	ldr	r2, [lr, r2, asl #2]
	str	r2, [lr, r5, asl #2]
	ldr	r2, [r0, #1920]
	strh	r7, [r2, ip]	@ movhi
	b	.L2107
.L2122:
	mov	r0, #0
	add	sp, sp, #36
	@ sp needed
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L2140:
	.align	2
.L2139:
	.word	.LANCHOR0
	.word	.LANCHOR2
	.word	.LANCHOR2-3816
	.word	.LANCHOR0+1814
	.word	.LANCHOR0+1884
	.word	.LANCHOR0+1804
	.word	.LANCHOR0+1892
	.word	.LANCHOR2-1928
	.word	.LANCHOR0+1918
	.fnend
	.size	FtlScanSysBlk, .-FtlScanSysBlk
	.align	2
	.global	FtlLoadSysInfo
	.type	FtlLoadSysInfo, %function
FtlLoadSysInfo:
	.fnstart
	@ args = 0, pretend = 0, frame = 8
	@ frame_needed = 0, uses_anonymous_args = 0
	stmfd	sp!, {r0, r1, r2, r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.save {r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.pad #12
	mov	r1, #0
	ldr	r6, .L2170
	ldr	r4, .L2170+4
	sub	r7, r6, #3808
	ldr	r3, [r6, #-2064]
	sub	r8, r7, #8
	ldr	r0, [r6, #-4060]
	str	r3, [r6, #220]
	ldr	r3, [r6, #-2040]
	str	r3, [r6, #224]
	movw	r3, #1812
	ldrh	r2, [r4, r3]
	mov	r2, r2, asl #1
	bl	ftl_memset
	ldrh	r0, [r7, #-8]
	movw	r3, #65535
	cmp	r0, r3
	bne	.L2142
.L2153:
	mvn	r0, #0
	b	.L2143
.L2142:
	mov	r1, #1
	ldr	r10, .L2170+8
	bl	FtlGetLastWrittenPage
	ldrh	r7, [r7, #-8]
	mov	r5, r6
	mov	fp, r5
	uxth	r6, r0
	add	r0, r0, #1
	strh	r0, [r8, #2]	@ movhi
.L2144:
	sxth	r3, r6
	cmp	r3, #0
	blt	.L2152
	sxth	r9, r7
	mov	r1, #1
	ldr	r0, .L2170+12
	mov	r2, r1
	orr	r3, r3, r9, asl #10
	str	r3, [r5, #216]
	ldr	r3, [r5, #-2064]
	str	r3, [r5, #220]
	bl	FlashReadPages
	ldrb	r3, [r4]	@ zero_extendqisi2
	cmp	r3, #0
	beq	.L2145
	ldr	r3, [fp, #224]
	ldr	r2, [r3, #12]
	cmp	r2, #0
	beq	.L2145
	ldr	r3, .L2170+16
	ldr	r0, [fp, #220]
	str	r2, [sp, #4]
	ldrh	r1, [r3]
	str	r3, [sp]
	bl	js_hash
	ldr	r2, [sp, #4]
	cmp	r2, r0
	beq	.L2145
	cmp	r6, #0
	ldr	r3, [sp]
	bne	.L2146
	ldr	r2, .L2170+20
	ldrh	r2, [r2, #4]
	cmp	r9, r2
	uxthne	r7, r2
	ldrneh	r6, [r3, #-8]
	bne	.L2148
.L2146:
	mvn	r3, #0
	str	r3, [r5, #212]
.L2145:
	ldr	r3, [r5, #212]
	cmn	r3, #1
	beq	.L2148
	ldr	r3, [fp, #-2064]
	ldr	r3, [r3]
	cmp	r3, r10
	bne	.L2148
	ldr	r3, [fp, #-2040]
	ldrh	r2, [r3]
	movw	r3, #61604
	cmp	r2, r3
	bne	.L2148
.L2152:
	ldr	r1, [r5, #220]
	mov	r2, #48
	movw	r9, #1812
	ldr	r0, .L2170+24
	bl	ftl_memcpy
	ldrh	r2, [r4, r9]
	ldr	r1, [r5, #220]
	ldr	r0, [r5, #-4060]
	add	r1, r1, #48
	mov	r2, r2, asl #1
	bl	ftl_memcpy
	ldrh	r2, [r4, r9]
	ldr	r1, [r5, #220]
	add	r3, r2, #24
	ldr	r0, [r5, #-3884]
	ubfx	r3, r3, #1, #30
	mov	r2, r2, lsr #3
	add	r2, r2, #4
	ldr	r7, .L2170+4
	add	r1, r1, r3, asl #2
	ldr	r6, .L2170
	bl	ftl_memcpy
	movw	r3, #1916
	ldrh	r3, [r4, r3]
	cmp	r3, #0
	beq	.L2150
	ldrh	r2, [r7, r9]
	ldr	r1, [r6, #220]
	ldr	r0, [r6, #-1988]
	mov	r3, r2, lsr #3
	add	r3, r3, r2, asl #1
	movw	r2, #1908
	ldrh	r2, [r7, r2]
	add	r3, r3, #52
	ubfx	r3, r3, #2, #14
	mov	r2, r2, asl #2
	add	r1, r1, r3, asl #2
	bl	ftl_memcpy
	b	.L2150
.L2148:
	sub	r6, r6, #1
	uxth	r6, r6
	b	.L2144
.L2150:
	ldr	r2, [r4, #4068]
	ldr	r3, .L2170+8
	ldr	r6, .L2170+4
	cmp	r2, r3
	bne	.L2153
	ldr	r7, .L2170+24
	movw	r2, #1826
	ldrb	r1, [r6, #4078]	@ zero_extendqisi2
	ldrh	r2, [r6, r2]
	ldrh	r3, [r7, #8]
	ldr	r4, .L2170
	cmp	r1, r2
	sub	r9, r4, #3808
	strh	r3, [r8, #6]	@ movhi
	bne	.L2153
	add	r2, r6, #1872
	movw	r1, #1878
	ldrh	r1, [r6, r1]
	ldrh	r2, [r2]
	str	r3, [r4, #248]
	mul	r2, r3, r2
	str	r2, [r6, #1932]
	mul	r2, r1, r2
	str	r2, [r6, #1912]
	add	r2, r6, #1936
	ldrh	r0, [r2, #6]
	ldr	r2, [r6, #1816]
	rsb	r0, r0, r2
	rsb	r0, r3, r0
	movw	r3, #1804
	ldrh	r1, [r6, r3]
	add	r6, r6, #4096
	bl	__aeabi_uidiv
	sub	r3, r4, #4032
	sub	r2, r4, #3984
	sub	ip, r4, #3792
	add	lr, r2, #40
	strh	r0, [r9, #-12]	@ movhi
	ldrh	r0, [r7, #14]
	strh	r0, [r3, #-8]	@ movhi
	ldrh	r3, [r7, #16]
	mov	r1, r3, lsr #6
	strh	r1, [r9, #-230]	@ movhi
	ldrh	r1, [r7, #18]
	and	r3, r3, #63
	strb	r3, [r4, #-4034]
	ldrb	r3, [r6, #-17]	@ zero_extendqisi2
	strh	r1, [r2, #-8]	@ movhi
	ldrh	r1, [r7, #20]
	strb	r3, [r4, #-4032]
	mvn	r3, #0
	strh	r3, [ip, #-8]	@ movhi
	mov	r3, #0
	mov	r8, r1, lsr #6
	and	r1, r1, #63
	strb	r1, [r4, #-3986]
	ldrb	r1, [r6, #-16]	@ zero_extendqisi2
	strh	r8, [r2, #-6]	@ movhi
	sub	r8, r4, #3936
	strh	r3, [r9, #10]	@ movhi
	mov	r9, r2
	strb	r1, [r4, #-3984]
	ldrh	r1, [r7, #22]
	strb	r3, [r4, #-3794]
	strb	r3, [r4, #-3792]
	strh	r1, [r8, #-8]	@ movhi
	ldrh	r1, [r7, #24]
	str	r3, [r4, #-3860]
	mov	r7, r1, lsr #6
	and	r1, r1, #63
	strb	r1, [r4, #-3938]
	ldrb	r1, [r6, #-15]	@ zero_extendqisi2
	strh	r7, [r2, #42]	@ movhi
	mov	r7, ip
	strb	r1, [r4, #-3936]
	str	r3, [r4, #-3872]
	ldr	r1, [r6, #4]
	str	r3, [r4, #-3880]
	str	r3, [r4, #-3864]
	str	r3, [r4, #-3836]
	str	r3, [r4, #-3828]
	str	r3, [r4, #-3868]
	ldr	r3, [r6, #12]
	str	r1, [r4, #-3840]
	ldr	r1, [r4, #-3848]
	ldr	r2, [r5, #-3844]
	cmp	r3, r1
	strhi	r3, [r4, #-3848]
	ldr	r3, [r6, #8]
	cmp	r3, r2
	ldrhi	r2, .L2170
	strhi	r3, [r2, #-3844]
	movw	r3, #65535
	cmp	r0, r3
	beq	.L2156
	ldr	r0, .L2170+28
	bl	make_superblock
.L2156:
	ldrh	r2, [r9, #-8]
	movw	r3, #65535
	cmp	r2, r3
	beq	.L2157
	ldr	r0, .L2170+32
	bl	make_superblock
.L2157:
	ldrh	r2, [r8, #-8]
	movw	r3, #65535
	cmp	r2, r3
	beq	.L2158
	ldr	r0, .L2170+36
	bl	make_superblock
.L2158:
	ldrh	r2, [r7, #-8]
	movw	r3, #65535
	cmp	r2, r3
	beq	.L2159
	ldr	r0, .L2170+40
	bl	make_superblock
.L2159:
	mov	r0, #0
.L2143:
	add	sp, sp, #12
	@ sp needed
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L2171:
	.align	2
.L2170:
	.word	.LANCHOR2
	.word	.LANCHOR0
	.word	1179929683
	.word	.LANCHOR2+212
	.word	.LANCHOR0+1882
	.word	.LANCHOR2-3816
	.word	.LANCHOR0+4068
	.word	.LANCHOR2-4040
	.word	.LANCHOR2-3992
	.word	.LANCHOR2-3944
	.word	.LANCHOR2-3800
	.fnend
	.size	FtlLoadSysInfo, .-FtlLoadSysInfo
	.align	2
	.global	FtlDumpBlockInfo
	.type	FtlDumpBlockInfo, %function
FtlDumpBlockInfo:
	.fnstart
	@ args = 0, pretend = 0, frame = 80
	@ frame_needed = 0, uses_anonymous_args = 0
	ldr	r3, .L2187
	ubfx	r0, r0, #10, #16
	stmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.save {r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.pad #108
	sub	sp, sp, #108
	ldr	r3, [r3]
	mov	r4, r1
	ldr	r8, .L2187+4
	ldr	r7, .L2187+8
	str	r3, [sp, #100]
	bl	P2V_block_in_plane
	add	r5, r8, #1872
	ldr	r1, .L2187+12
	ldrh	r9, [r5]
	mov	r6, r0
	ldr	r0, .L2187+16
	bl	printk
	ldr	r2, [r7, #-4060]
	mov	r3, r6, asl #1
	mov	r1, r6
	ldr	r0, .L2187+20
	ldrh	r2, [r2, r3]
	bl	printk
	add	r0, sp, #104
	strh	r6, [r0, #-52]!	@ movhi
	bl	make_superblock
	ldrb	r2, [r8, #140]	@ zero_extendqisi2
	clz	r3, r4
	cmp	r2, #0
	mov	r3, r3, lsr #5
	moveq	r3, #0
	cmp	r3, #0
	moveq	r4, r3
	str	r7, [sp, #44]
	beq	.L2173
	mov	r0, r6
	bl	ftl_get_blk_mode
	cmp	r0, #1
	mov	r4, r0
	movweq	r3, #1874
	ldreqh	r9, [r8, r3]
.L2173:
	ldr	r0, .L2187+24
	mov	r1, r4
	mov	r2, r9
	ldrh	r3, [r5]
	bl	printk
	ldr	r8, .L2187+8
	mov	r6, #0
.L2174:
	ldr	r3, .L2187+28
	add	r0, sp, #66
	movw	lr, #65535
	mov	r10, #36
	ldrh	r3, [r3]
	mov	r7, r3
	ldr	r3, [r8, #-2076]
	str	r3, [sp, #28]
	ldr	r3, [r8, #-3212]
	str	r3, [sp, #32]
	ldr	r3, .L2187+32
	ldrh	r2, [r3]
	ldrh	ip, [r3, #2]
	str	r2, [sp, #36]
	ldr	r2, [r8, #-3208]
	str	r2, [sp, #40]
	mov	r2, #0
	mov	r5, r2
.L2175:
	uxth	r3, r2
	cmp	r3, r7
	bcs	.L2185
	ldrh	r3, [r0, #2]!
	cmp	r3, lr
	beq	.L2176
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
.L2176:
	add	r2, r2, #1
	b	.L2175
.L2185:
	ldr	r0, [r8, #-2076]
	mov	r1, r5
	mov	r2, r4
	mov	r10, #0
	bl	FlashReadPages
	mov	fp, #36
.L2178:
	uxth	r3, r10
	cmp	r3, r5
	bcs	.L2186
	ldr	r3, [sp, #44]
	mul	r2, fp, r10
	ldrh	r1, [sp, #52]
	ldr	lr, [r3, #-2076]
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
	ldr	r0, .L2187+36
	str	r3, [sp, #20]
	ldr	r2, [lr, r2]
	ldr	r3, [ip, #4]
	bl	printk
	b	.L2178
.L2186:
	add	r6, r6, #1
	uxth	r6, r6
	cmp	r6, r9
	bne	.L2174
.L2180:
	ldr	r3, .L2187
	mov	r0, #0
	ldr	r2, [sp, #100]
	ldr	r3, [r3]
	cmp	r2, r3
	beq	.L2181
	bl	__stack_chk_fail
.L2181:
	add	sp, sp, #108
	@ sp needed
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L2188:
	.align	2
.L2187:
	.word	__stack_chk_guard
	.word	.LANCHOR0
	.word	.LANCHOR2
	.word	.LANCHOR3+16
	.word	.LC107
	.word	.LC108
	.word	.LC109
	.word	.LANCHOR0+1804
	.word	.LANCHOR0+1882
	.word	.LC110
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
	ldr	r5, .L2205
	mov	r6, #0
	ldr	r0, .L2205+4
	ldr	r1, .L2205+8
	ldr	r3, [r5]
	str	r3, [sp, #92]
	bl	printk
	str	r5, [sp, #36]
	ldr	r5, .L2205+12
	mov	r7, r5
.L2190:
	ldr	r3, .L2205+16
	uxth	r4, r6
	ldrh	r3, [r3]
	cmp	r3, r4
	bls	.L2201
	add	r8, sp, #96
	mov	r0, r4
	movw	r9, #65535
	mov	r10, #36
	strh	r4, [r8, #-52]!	@ movhi
	bl	ftl_get_blk_mode
	ldr	ip, [r5, #-4060]
	mov	r2, r4, asl #1
	mov	r1, r4
	ldrh	r2, [ip, r2]
	mov	r3, r0
	ldr	r0, .L2205+20
	bl	printk
	mov	r0, r8
	bl	make_superblock
	ldr	r3, .L2205+24
	ldr	lr, [r5, #-3208]
	ldrh	r2, [r3]
	ldrh	ip, [r3, #78]
	ldrh	r8, [r3, #80]
	str	r2, [sp, #24]
	add	r0, sp, #58
	ldr	r2, [r5, #-2076]
	str	r2, [sp, #28]
	ldr	r2, [r5, #-3212]
	str	r2, [sp, #32]
	mov	r2, #0
	mov	r4, r2
.L2191:
	ldr	r1, [sp, #24]
	uxth	r3, r2
	cmp	r3, r1
	bcs	.L2202
	ldrh	r3, [r0, #2]!
	cmp	r3, r9
	beq	.L2192
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
.L2192:
	add	r2, r2, #1
	b	.L2191
.L2202:
	ldr	r0, [r5, #-2076]
	mov	r1, r4
	mov	r2, #0
	mov	r8, #0
	bl	FlashReadPages
	mov	r9, #36
.L2194:
	uxth	r3, r8
	cmp	r3, r4
	bcs	.L2203
	mul	r2, r9, r8
	ldr	lr, [r7, #-2076]
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
	ldr	r0, .L2205+28
	str	r3, [sp, #20]
	ldr	r2, [lr, r2]
	ldr	r3, [ip, #4]
	bl	printk
	b	.L2194
.L2203:
	ldr	r0, [r7, #-2076]
	mov	r1, r4
	mov	r2, #1
	mov	r8, #0
	bl	FlashReadPages
	mov	r9, #36
.L2196:
	uxth	r3, r8
	cmp	r3, r4
	bcs	.L2204
	mul	r2, r9, r8
	ldr	lr, [r7, #-2076]
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
	ldr	r0, .L2205+32
	str	r3, [sp, #20]
	ldr	r2, [lr, r2]
	ldr	r3, [ip, #4]
	bl	printk
	b	.L2196
.L2204:
	add	r6, r6, #1
	b	.L2190
.L2201:
	ldr	r3, [sp, #36]
	mov	r0, #0
	ldr	r2, [sp, #92]
	ldr	r3, [r3]
	cmp	r2, r3
	beq	.L2199
	bl	__stack_chk_fail
.L2199:
	add	sp, sp, #100
	@ sp needed
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L2206:
	.align	2
.L2205:
	.word	__stack_chk_guard
	.word	.LC107
	.word	.LANCHOR3+36
	.word	.LANCHOR2
	.word	.LANCHOR0+1814
	.word	.LC111
	.word	.LANCHOR0+1804
	.word	.LC112
	.word	.LC113
	.fnend
	.size	FtlScanAllBlock, .-FtlScanAllBlock
	.align	2
	.global	ftl_scan_all_ppa
	.type	ftl_scan_all_ppa, %function
ftl_scan_all_ppa:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	ldr	r3, .L2214
	movw	r2, #1870
	stmfd	sp!, {r4, r5, r6, r7, r8, r9, lr}
	.save {r4, r5, r6, r7, r8, r9, lr}
	add	r7, r3, #1872
	ldrh	r1, [r3, r2]
	.pad #28
	sub	sp, sp, #28
	ldr	r0, .L2214+4
	sub	r9, r7, #2
	ldrh	r2, [r7]
	mov	r6, #0
	bl	printk
	ldr	r4, .L2214+8
.L2208:
	ldrh	r3, [r9]
	cmp	r6, r3
	bcs	.L2210
	mov	r8, r6, asl #10
	mov	r5, #0
.L2211:
	ldrh	r3, [r7]
	cmp	r5, r3
	bcs	.L2213
	add	r3, r5, r8
	str	r3, [r4, #216]
	ldr	r3, [r4, #-2064]
	mov	r2, #0
	mov	r1, #1
	ldr	r0, .L2214+12
	str	r2, [r4, #212]
	add	r5, r5, #1
	str	r3, [r4, #220]
	ldr	r3, [r4, #-2040]
	str	r3, [r4, #224]
	bl	FlashReadPages
	ldr	r3, [r4, #224]
	ldr	r2, [r4, #220]
	ldr	r0, .L2214+16
	ldr	r1, [r3, #4]
	str	r1, [sp]
	ldr	r1, [r3, #8]
	str	r1, [sp, #4]
	ldr	r1, [r3, #12]
	str	r1, [sp, #8]
	ldr	r1, [r2]
	str	r1, [sp, #12]
	ldr	r2, [r2, #4]
	ldr	r1, [r4, #216]
	str	r2, [sp, #16]
	ldr	r2, [r4, #212]
	ldr	r3, [r3]
	bl	printk
	b	.L2211
.L2213:
	add	r6, r6, #1
	b	.L2208
.L2210:
	ldr	r0, .L2214+20
	ldr	r1, .L2214+24
	add	sp, sp, #28
	@ sp needed
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, lr}
	b	printk
.L2215:
	.align	2
.L2214:
	.word	.LANCHOR0
	.word	.LC114
	.word	.LANCHOR2
	.word	.LANCHOR2+212
	.word	.LC115
	.word	.LC116
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
	ldr	ip, .L2274
	stmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.save {r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.pad #76
	sub	sp, sp, #76
	ldr	ip, [ip]
	mov	r4, r0
	ldr	r5, .L2274+4
	mov	r9, r2
	str	r1, [sp, #8]
	str	ip, [sp, #68]
	ldr	ip, [r5, #692]
	ldrb	r7, [r5]	@ zero_extendqisi2
	str	r3, [sp, #20]
	ldrb	ip, [ip, #19]	@ zero_extendqisi2
	cmp	r7, #0
	str	ip, [sp, #16]
	ldr	ip, .L2274+8
	ldrb	ip, [ip, #481]	@ zero_extendqisi2
	str	ip, [sp, #12]
	beq	.L2217
	bl	FlashProgSlc2KPages
	b	.L2218
.L2231:
	mov	r3, #36
	add	r2, sp, #24
	mul	r8, r3, r7
	ldr	r3, [sp, #8]
	mov	r1, r9
	rsb	r3, r7, r3
	add	fp, r4, r8
	uxtb	r3, r3
	str	r3, [sp]
	mov	r0, fp
	add	r3, sp, #28
	bl	LogAddr2PhyAddr
	ldrb	r2, [r5, #1717]	@ zero_extendqisi2
	ldr	r3, [sp, #28]
	mov	r10, r0
	cmp	r3, r2
	mvncs	r3, #0
	strcs	r3, [r4, r8]
	bcc	.L2271
.L2220:
	add	r7, r7, #1
.L2217:
	ldr	r3, [sp, #8]
	ldr	r6, .L2274+4
	cmp	r7, r3
	bcc	.L2231
	b	.L2272
.L2271:
	ldrb	r1, [r5, #1793]	@ zero_extendqisi2
	add	r3, r5, r3, asl #4
	cmp	r1, #0
	ldr	r3, [r3, #1596]
	moveq	r10, #0
	cmp	r3, #0
	beq	.L2222
	cmp	r2, #1
	bne	.L2223
	ldr	r0, [r5, #76]
	bl	NandcIqrWaitFlashReady
.L2223:
	ldrb	r0, [sp, #28]	@ zero_extendqisi2
	bl	FlashWaitCmdDone
.L2222:
	ldr	r3, [sp, #28]
	mov	r1, #0
	cmp	r10, #0
	add	r2, r5, r3, asl #4
	str	r1, [r2, #1600]
	ldr	r1, [sp, #24]
	str	fp, [r2, #1596]
	str	r1, [r2, #1592]
	addne	r1, r8, #36
	addne	r1, r4, r1
	strne	r1, [r2, #1600]
	add	r2, r5, r3
	add	r3, r5, r3, asl #4
	ldrb	r6, [r2, #1720]	@ zero_extendqisi2
	strb	r6, [r3, #1588]
	mov	r0, r6
	ldrb	r3, [r5, #1717]	@ zero_extendqisi2
	cmp	r3, #1
	bne	.L2225
	bl	NandcWaitFlashReady
	b	.L2226
.L2225:
	bl	NandcFlashCs
	mov	r0, r6
	ldr	r3, [sp, #28]
	ldr	r1, [sp, #24]
	add	r3, r5, r3, asl #2
	ldr	r2, [r3, #656]
	adds	r2, r2, #0
	movne	r2, #1
	bl	FlashWaitReadyEN
	mov	r0, r6
	bl	NandcFlashDeCs
.L2226:
	ldr	r3, [sp, #16]
	sub	r3, r3, #1
	cmp	r3, #7
	bhi	.L2227
	add	r3, r5, r6
	ldrb	r3, [r3, #1552]	@ zero_extendqisi2
	cmp	r3, #0
	beq	.L2227
	mov	r0, r6
	ldrb	r1, [r5, #699]	@ zero_extendqisi2
	ldr	r2, .L2274+12
	mov	r3, #0
	bl	HynixSetRRPara
.L2227:
	mov	r0, r6
	bl	NandcFlashCs
	cmp	r9, #1
	mov	r0, r6
	bne	.L2228
	ldrb	r3, [r5, #140]	@ zero_extendqisi2
	cmp	r3, #0
	beq	.L2228
	bl	flash_enter_slc_mode
	b	.L2229
.L2228:
	bl	flash_exit_slc_mode
.L2229:
	mov	r0, r6
	ldr	r1, [sp, #24]
	bl	FlashProgFirstCmd
	ldr	r3, [fp, #12]
	mov	r0, r6
	mov	r1, #1
	str	r3, [sp]
	ldr	r2, [sp, #12]
	ldr	r3, [fp, #8]
	bl	NandcXferData
	cmp	r10, #0
	beq	.L2230
	mov	r0, r6
	ldr	r1, [sp, #24]
	bl	FlashProgDpFirstCmd
	mov	r0, r6
	add	r8, r8, #36
	add	r8, r4, r8
	ldr	r3, [sp, #28]
	ldr	r1, [sp, #24]
	add	r3, r5, r3, asl #2
	ldr	r2, [r3, #656]
	adds	r2, r2, #0
	movne	r2, #1
	bl	FlashWaitReadyEN
	ldr	r1, [r5, #4]
	mov	r0, r6
	ldr	r3, [sp, #24]
	add	r1, r1, r3
	bl	FlashProgDpSecondCmd
	ldr	r3, [r8, #12]
	mov	r0, r6
	mov	r1, #1
	str	r3, [sp]
	ldr	r2, [sp, #12]
	ldr	r3, [r8, #8]
	bl	NandcXferData
.L2230:
	mov	r0, r6
	ldr	r1, [sp, #24]
	bl	FlashProgSecondCmd
	mov	r0, r6
	bl	NandcFlashDeCs
	add	r7, r7, r10
	b	.L2220
.L2272:
	ldr	r0, [r6, #76]
	mov	r5, #0
	bl	NandcIqrWaitFlashReady
	ldr	r8, .L2274+16
	mov	r7, r6
.L2232:
	ldrb	r3, [r6, #1717]	@ zero_extendqisi2
	cmp	r5, r3
	bcs	.L2273
	uxtb	r0, r5
	bl	FlashWaitCmdDone
	cmp	r9, #1
	bne	.L2233
	ldrb	r3, [r7, #140]	@ zero_extendqisi2
	cmp	r3, #0
	beq	.L2233
	ldrb	r0, [r8, r5, asl #4]	@ zero_extendqisi2
	bl	flash_exit_slc_mode
.L2233:
	add	r5, r5, #1
	b	.L2232
.L2273:
	ldr	r3, [sp, #20]
	cmp	r3, #0
	bne	.L2235
.L2243:
	mov	r0, #0
	b	.L2218
.L2235:
	ldr	r5, .L2274+20
	mov	r6, #0
	mov	r7, r5
.L2236:
	ldr	r3, [sp, #8]
	cmp	r6, r3
	beq	.L2243
	ldr	r3, [r4]
	cmn	r3, #1
	bne	.L2237
	ldr	r1, [r4, #4]
	ldr	r0, .L2274+24
	bl	printk
	b	.L2238
.L2237:
	ldr	r3, [sp, #8]
	mov	r1, r9
	add	r2, sp, #24
	mov	r0, r4
	rsb	r3, r6, r3
	uxtb	r3, r3
	str	r3, [sp]
	add	r3, sp, #28
	bl	LogAddr2PhyAddr
	ldr	r2, [r5, #184]
	mov	r3, #0
	mov	lr, r4
	str	r3, [r2]
	ldr	r2, [r5, #188]
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
	ldr	r3, [r5, #184]
	str	r3, [sp, #40]
	ldr	r3, [r5, #188]
	str	r3, [sp, #44]
	bl	FlashReadPages
	ldr	r8, [sp, #32]
	cmn	r8, #1
	bne	.L2239
	ldr	r0, .L2274+28
	ldr	r1, [r4, #4]
	bl	printk
	str	r8, [r4]
.L2239:
	ldr	r3, [r4, #12]
	cmp	r3, #0
	beq	.L2240
	ldr	r2, [r3]
	ldr	r3, [r7, #188]
	ldr	r3, [r3]
	cmp	r2, r3
	beq	.L2240
	ldr	r0, .L2274+32
	ldr	r1, [r4, #4]
	bl	printk
	mvn	r3, #0
	str	r3, [r4]
.L2240:
	ldr	r3, [r4, #8]
	cmp	r3, #0
	beq	.L2238
	ldr	r2, [r3]
	ldr	r3, [r7, #184]
	ldr	r3, [r3]
	cmp	r2, r3
	beq	.L2238
	ldr	r0, .L2274+36
	ldr	r1, [r4, #4]
	bl	printk
	mvn	r3, #0
	str	r3, [r4]
.L2238:
	add	r6, r6, #1
	add	r4, r4, #36
	b	.L2236
.L2218:
	ldr	r3, .L2274
	ldr	r2, [sp, #68]
	ldr	r3, [r3]
	cmp	r2, r3
	beq	.L2244
	bl	__stack_chk_fail
.L2244:
	add	sp, sp, #76
	@ sp needed
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L2275:
	.align	2
.L2274:
	.word	__stack_chk_guard
	.word	.LANCHOR0
	.word	.LANCHOR1
	.word	.LANCHOR0+702
	.word	.LANCHOR0+1588
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
	ldr	r3, .L2281
	stmfd	sp!, {r4, r5, r6, lr}
	.save {r4, r5, r6, lr}
	.pad #104
	sub	sp, sp, #104
	ldr	r2, [r3]
	mov	r4, r0
	ldr	r5, .L2281+4
	mov	r6, r3
	str	r2, [sp, #100]
	ldr	r2, [r5, #168]
	cmp	r0, r2
	movcc	r4, #0
	bcc	.L2277
	ldr	r3, [r5, #176]
	add	r0, sp, #36
	mov	r1, #165
	mov	r2, #32
	str	r0, [sp, #12]
	mov	r4, r4, asl #10
	str	r3, [sp, #8]
	bl	ftl_memset
	mov	r1, #90
	mov	r2, #8
	ldr	r0, [r5, #176]
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
.L2277:
	ldr	r2, [sp, #100]
	mov	r0, r4
	ldr	r3, [r6]
	cmp	r2, r3
	beq	.L2278
	bl	__stack_chk_fail
.L2278:
	add	sp, sp, #104
	@ sp needed
	ldmfd	sp!, {r4, r5, r6, pc}
.L2282:
	.align	2
.L2281:
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
	ldr	r3, .L2339
	mov	r1, #1
	stmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.save {r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.pad #92
	sub	sp, sp, #92
	ldr	r3, [r3]
	ldr	r5, .L2339+4
	ldr	r4, .L2339+8
	str	r3, [sp, #84]
	ldr	r3, [r5, #180]
	ldrh	r8, [r4, #124]
	ldr	r0, .L2339+12
	str	r3, [sp, #20]
	ldrh	r3, [r4, #126]
	smulbb	r8, r8, r3
	ldr	r3, [r4, #692]
	ldrb	r3, [r3, #24]	@ zero_extendqisi2
	uxth	r8, r8
	str	r3, [sp, #24]
	ldrh	r3, [r4, #4]
	str	r3, [sp, #16]
	ldrb	r3, [r4]	@ zero_extendqisi2
	ldr	r4, .L2339+8
	cmp	r3, #1
	ldreq	r3, [sp, #16]
	moveq	r3, r3, asl #1
	uxtheq	r3, r3
	streq	r3, [sp, #16]
	bl	printk
	ldr	r0, [r5, #180]
	mov	r1, #0
	mov	r2, #4096
	bl	ftl_memset
	ldr	r5, .L2339+4
	mov	r3, r8, lsr #4
	str	r3, [sp, #28]
	mov	r3, #0
	str	r3, [sp, #8]
.L2285:
	ldrb	r7, [sp, #8]	@ zero_extendqisi2
	ldrb	r3, [r4, #1717]	@ zero_extendqisi2
	cmp	r3, r7
	bls	.L2335
	add	r3, r5, r7, asl #1
	ldrh	r6, [r3, #196]
	cmp	r6, #0
	bne	.L2315
	ldrh	r2, [r4, #132]
	mov	r1, r6
	ldr	r0, [r5, #148]
	mov	r9, r6
	mov	r2, r2, asl #9
	bl	ftl_memset
	add	r3, r4, r7
	ldrb	r10, [r3, #1720]	@ zero_extendqisi2
	mov	r3, r7, asl #2
	add	fp, r4, r3
	str	r6, [sp, #4]
	str	r3, [sp, #32]
.L2287:
	ldrh	r3, [sp, #4]
	cmp	r3, r8
	str	r3, [sp, #12]
	bcs	.L2296
	mvn	r3, #0
	strb	r3, [sp, #46]
	strb	r3, [sp, #47]
	ldr	r3, [sp, #24]
	tst	r3, #1
	beq	.L2289
	ldr	r3, [fp, #656]
	add	r2, sp, #46
	mov	r0, r10
	add	r3, r6, r3
	str	r3, [sp, #36]
	mov	r1, r3
	bl	FlashReadSpare
	ldrb	r2, [r4]	@ zero_extendqisi2
	cmp	r2, #1
	ldr	r3, [sp, #36]
	bne	.L2289
	ldr	r1, [r4, #4]
	add	r2, sp, #47
	mov	r0, r10
	add	r1, r3, r1
	bl	FlashReadSpare
	ldrb	r3, [sp, #46]	@ zero_extendqisi2
	ldrb	r2, [sp, #47]	@ zero_extendqisi2
	and	r3, r3, r2
	strb	r3, [sp, #46]
.L2289:
	ldr	r3, [sp, #24]
	tst	r3, #2
	beq	.L2291
	ldr	r3, [r4, #692]
	mov	r0, r10
	add	r2, sp, #47
	ldrh	r1, [r3, #10]
	ldr	r3, [fp, #656]
	sub	r1, r1, #1
	add	r1, r1, r3
	add	r1, r1, r6
	bl	FlashReadSpare
.L2291:
	ldr	r3, [r4, #692]
	ldrb	r3, [r3, #7]	@ zero_extendqisi2
	cmp	r3, #1
	cmpne	r3, #8
	ldrb	r3, [sp, #46]	@ zero_extendqisi2
	bne	.L2292
	cmp	r3, #0
	ldrneb	r0, [sp, #47]	@ zero_extendqisi2
	clzne	r0, r0
	movne	r0, r0, lsr #5
	bne	.L2293
	b	.L2314
.L2292:
	cmp	r3, #255
	bne	.L2314
	ldrb	r0, [sp, #47]	@ zero_extendqisi2
	subs	r0, r0, #255
	movne	r0, #1
	b	.L2293
.L2314:
	mov	r0, #1
.L2293:
	ldr	r3, [sp, #24]
	tst	r3, #4
	beq	.L2294
	ldr	r3, .L2339+8
	mov	r0, r10
	ldr	r2, [sp, #32]
	add	r3, r3, r2
	ldr	r1, [r3, #656]
	add	r1, r6, r1
	bl	SandiskProgTestBadBlock
.L2294:
	cmp	r0, #0
	beq	.L2295
	mov	r1, r7
	ldr	r2, [sp, #4]
	ldr	r0, .L2339+16
	add	r9, r9, #1
	bl	printk
	ldr	r1, [r5, #148]
	mov	ip, #1
	uxth	r9, r9
	ldr	r3, [sp, #12]
	mov	r0, r3, lsr #5
	and	r3, r3, #31
	ldr	r2, [r1, r0, asl #2]
	orr	r3, r2, ip, asl r3
	ldr	r2, [sp, #28]
	str	r3, [r1, r0, asl #2]
	ldrb	r3, [r4, #1717]	@ zero_extendqisi2
	mul	r3, r3, r2
	cmp	r9, r3
	bgt	.L2296
.L2295:
	ldr	r3, [sp, #4]
	add	r3, r3, #1
	str	r3, [sp, #4]
	ldr	r3, [sp, #16]
	add	r6, r6, r3
	b	.L2287
.L2296:
	mov	r2, r9
	ldr	r0, .L2339+20
	mov	r1, r7
	bl	printk
	ldrb	r3, [r4, #1717]	@ zero_extendqisi2
	ldr	r2, [sp, #28]
	mul	r3, r3, r2
	cmp	r9, r3
	blt	.L2298
	ldrh	r2, [r4, #132]
	mov	r1, #0
	ldr	r0, [r5, #148]
	mov	r2, r2, asl #9
	bl	ftl_memset
.L2298:
	cmp	r7, #0
	ldreqh	r10, [r5, #168]
	moveq	r6, r7
	moveq	r9, #1
	bne	.L2300
.L2301:
	ldr	r3, .L2339+8
	ldrb	r3, [r3, #1]	@ zero_extendqisi2
	cmp	r3, r10
	bls	.L2336
	mov	r0, r10
	bl	FlashTestBlk
	cmp	r0, #0
	beq	.L2302
	mov	r1, r10
	ldr	r0, .L2339+24
	bl	printk
	ldr	r1, [r5, #148]
	mov	r0, r10, lsr #5
	add	r6, r6, #1
	and	r3, r10, #31
	ldr	r2, [r1, r0, asl #2]
	uxth	r6, r6
	orr	r3, r2, r9, asl r3
	str	r3, [r1, r0, asl #2]
.L2302:
	add	r10, r10, #1
	uxth	r10, r10
	b	.L2301
.L2336:
	sub	fp, r8, #1
	sub	r9, r8, #50
	mov	r10, #1
	uxth	fp, fp
.L2304:
	cmp	fp, r9
	ble	.L2337
	mov	r0, fp
	bl	FlashTestBlk
	cmp	r0, #0
	beq	.L2305
	mov	r1, fp
	ldr	r0, .L2339+24
	bl	printk
	ldr	r1, [r5, #148]
	mov	r0, fp, lsr #5
	and	r3, fp, #31
	ldr	r2, [r1, r0, asl #2]
	orr	r3, r2, r10, asl r3
	str	r3, [r1, r0, asl #2]
.L2305:
	sub	fp, fp, #1
	uxth	fp, fp
	b	.L2304
.L2337:
	ldr	r3, .L2339+8
	ldr	r2, [r5, #168]
	ldrb	r3, [r3, #1]	@ zero_extendqisi2
	rsb	r3, r2, r3
	cmp	r6, r3
	bcc	.L2300
	ldrh	r2, [r4, #132]
	mov	r1, #0
	ldr	r0, [r5, #148]
	mov	r2, r2, asl #9
	bl	ftl_memset
.L2300:
	ldrb	r6, [sp, #8]	@ zero_extendqisi2
	sub	r10, r8, #1
	add	r9, r5, r7, asl #1
	uxth	r10, r10
	mul	r6, r8, r6
.L2308:
	mov	r1, r7
	ldr	r0, .L2339+28
	mov	r2, r10
	bl	printk
	ldr	r1, [r5, #148]
.L2309:
	mov	r2, r10, lsr #5
	and	r3, r10, #31
	ldr	r2, [r1, r2, asl #2]
	mov	r3, r2, lsr r3
	ands	r3, r3, #1
	subne	r10, r10, #1
	uxthne	r10, r10
	bne	.L2309
.L2338:
	ldr	r1, [sp, #20]
	add	r0, sp, #48
	ldr	r2, .L2339+32
	strh	r10, [r9, #196]	@ movhi
	strh	r10, [r1, #2]	@ movhi
	strh	r2, [r1]	@ movhi
	strh	r3, [r1, #8]	@ movhi
	mov	r1, #1
	ldr	r3, [r5, #148]
	mov	r2, r1
	str	r3, [sp, #56]
	ldr	r3, [r5, #180]
	str	r3, [sp, #60]
	add	r3, r10, r6
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
	bne	.L2308
.L2315:
	ldr	r3, [sp, #8]
	add	r3, r3, #1
	str	r3, [sp, #8]
	b	.L2285
.L2335:
	ldr	r3, .L2339
	ldr	r2, [sp, #84]
	ldr	r3, [r3]
	cmp	r2, r3
	beq	.L2312
	bl	__stack_chk_fail
.L2312:
	add	sp, sp, #92
	@ sp needed
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L2340:
	.align	2
.L2339:
	.word	__stack_chk_guard
	.word	.LANCHOR2
	.word	.LANCHOR0
	.word	.LC117
	.word	.LC118
	.word	.LC119
	.word	.LC120
	.word	.LC121
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
	ldr	r5, .L2391
	ldr	r7, [r5, #4052]
	ldrb	r3, [r5, #1793]	@ zero_extendqisi2
	cmp	r7, #0
	str	r3, [sp, #20]
	movne	r0, #0
	bne	.L2342
	ldrb	r3, [r5, #140]	@ zero_extendqisi2
	mov	r10, r5
	ldr	r6, .L2391+4
	mov	fp, r7
	mov	r5, r7
	mov	r4, r7
	mov	r8, r10
	mov	r9, #36
	str	r1, [sp, #4]
	str	r0, [sp, #8]
	str	r3, [sp, #12]
	str	r0, [r6, #-2088]
.L2343:
	ldr	r3, .L2391+8
	uxth	r2, fp
	ldr	r7, .L2391
	ldrh	r1, [r3]
	cmp	r1, r2
	bls	.L2386
	mul	r1, r9, r2
	ldr	r0, [r8, #4056]
	add	r2, r8, r2
	mov	r3, #0
	str	r3, [r0, r1]
	ldr	r1, [sp, #8]
	ldrb	r0, [r2, #1832]	@ zero_extendqisi2
	bl	V2P_block
	ldr	r3, [sp, #4]
	mov	r7, r0
	cmp	r3, #0
	beq	.L2344
	bl	IsBlkInVendorPart
	cmp	r0, #0
	bne	.L2345
.L2344:
	mov	r0, r7
	bl	FtlBbmIsBadBlock
	cmp	r0, #0
	addne	r4, r4, #1
	uxthne	r4, r4
	bne	.L2345
	ldr	r3, .L2391+12
	mov	r7, r7, asl #10
	ldr	r1, [r10, #4056]
	ldrh	r2, [r3]
	mla	r1, r9, r5, r1
	mul	r2, r2, r5
	add	r5, r5, #1
	uxth	r5, r5
	cmp	r2, #0
	str	r0, [r1, #8]
	add	r0, r2, #3
	str	r7, [r1, #4]
	movlt	r2, r0
	ldr	r0, [r6, #-2036]
	bic	r2, r2, #3
	add	r2, r0, r2
	str	r2, [r1, #12]
.L2345:
	add	fp, fp, #1
	b	.L2343
.L2386:
	cmp	r5, #0
	beq	.L2366
	ldr	r3, [sp, #12]
	mov	r2, r5
	ldr	r0, [r7, #4056]
	mov	r8, #0
	adds	r6, r3, #0
	strb	r8, [r7, #1793]
	movne	r6, #1
	mov	r1, r6
	bl	FlashEraseBlocks
	ldrb	r3, [sp, #20]	@ zero_extendqisi2
	strb	r3, [r7, #1793]
	mov	r7, r8
	mov	r8, #36
.L2349:
	uxth	r3, r7
	cmp	r3, r5
	bcs	.L2387
	mul	r3, r8, r7
	ldr	r2, [r10, #4056]
	add	r0, r2, r3
	ldr	r3, [r2, r3]
	cmn	r3, #1
	bne	.L2350
	ldr	r0, [r0, #4]
	add	r4, r4, #1
	ubfx	r0, r0, #10, #16
	uxth	r4, r4
	bl	FtlBbmMapBadBlock
.L2350:
	add	r7, r7, #1
	b	.L2349
.L2387:
	ldr	r3, [sp, #4]
	cmp	r3, #0
	bne	.L2352
	mov	r3, #6
	uxth	r6, r6
	str	r3, [sp, #16]
	mov	r3, #1
	str	r3, [sp, #12]
	b	.L2353
.L2352:
	movw	r3, #1874
	ldrh	r3, [r10, r3]
	str	r3, [sp, #12]
	ldrb	r3, [r10, #140]	@ zero_extendqisi2
	cmp	r3, #0
	ldreq	r3, [sp, #12]
	moveq	r6, #1
	movne	r6, #1
	strne	r6, [sp, #16]
	moveq	r3, r3, lsr #2
	streq	r3, [sp, #16]
.L2353:
	ldr	r8, .L2391
	mov	r7, #0
.L2362:
	ldr	fp, .L2391+4
	mov	r9, #0
	mov	r5, r9
.L2354:
	ldr	r3, .L2391+8
	ldr	r10, .L2391
	ldrh	r2, [r3]
	uxth	r3, r9
	cmp	r2, r3
	bls	.L2388
	mov	r2, #36
	ldr	r1, [r8, #4056]
	mul	r2, r2, r3
	add	r3, r8, r3
	mov	r0, #0
	str	r0, [r1, r2]
	ldr	r1, [sp, #8]
	ldrb	r0, [r3, #1832]	@ zero_extendqisi2
	bl	V2P_block
	ldr	r3, [sp, #4]
	mov	r10, r0
	cmp	r3, #0
	beq	.L2355
	bl	IsBlkInVendorPart
	cmp	r0, #0
	bne	.L2356
.L2355:
	mov	r0, r10
	bl	FtlBbmIsBadBlock
	cmp	r0, #0
	bne	.L2356
	ldr	r2, [r8, #4056]
	mov	r3, #36
	add	r10, r7, r10, asl #10
	mla	r2, r3, r5, r2
	ldr	r3, [fp, #-2048]
	str	r3, [r2, #8]
	ldr	r3, .L2391+12
	str	r10, [r2, #4]
	ldrh	r3, [r3]
	mul	r3, r3, r5
	add	r5, r5, #1
	uxth	r5, r5
	add	r1, r3, #3
	cmp	r3, #0
	movlt	r3, r1
	ldr	r1, [fp, #-2044]
	bic	r3, r3, #3
	add	r3, r1, r3
	str	r3, [r2, #12]
.L2356:
	add	r9, r9, #1
	b	.L2354
.L2388:
	cmp	r5, #0
	beq	.L2366
	ldr	r2, .L2391
	mov	r3, #0
	mov	r1, r5
	mov	r9, #0
	mov	fp, #36
	strb	r3, [r2, #1793]
	mov	r3, #1
	ldr	r0, [r2, #4056]
	mov	r2, r6
	bl	FlashProgPages
	ldr	r3, .L2391
	ldrb	r2, [sp, #20]	@ zero_extendqisi2
	strb	r2, [r3, #1793]
.L2359:
	uxth	r3, r9
	cmp	r3, r5
	bcs	.L2389
	mul	r3, fp, r9
	ldr	r2, .L2391
	ldr	r2, [r2, #4056]
	add	r0, r2, r3
	ldr	r3, [r2, r3]
	cmp	r3, #0
	beq	.L2360
	ldr	r0, [r0, #4]
	add	r4, r4, #1
	ubfx	r0, r0, #10, #16
	uxth	r4, r4
	bl	FtlBbmMapBadBlock
.L2360:
	add	r9, r9, #1
	b	.L2359
.L2389:
	ldr	r3, [sp, #16]
	add	r7, r7, r3
	ldr	r3, [sp, #12]
	uxth	r7, r7
	cmp	r7, r3
	bcc	.L2362
	mov	r7, #0
	mov	r8, #36
.L2363:
	uxth	r3, r7
	cmp	r3, r5
	ldr	r3, [sp, #4]
	bcs	.L2390
	cmp	r3, #0
	beq	.L2364
	mul	r3, r8, r7
	ldr	r2, [r10, #4056]
	add	r1, r2, r3
	ldr	r3, [r2, r3]
	cmp	r3, #0
	bne	.L2364
	ldr	r0, [r1, #4]
	mov	r1, #1
	ubfx	r0, r0, #10, #16
	bl	FtlFreeSysBlkQueueIn
.L2364:
	add	r7, r7, #1
	b	.L2363
.L2390:
	adds	r3, r3, #0
	ldr	r2, [sp, #8]
	movne	r3, #1
	cmp	r2, #63
	orrls	r3, r3, #1
	cmp	r3, #0
	beq	.L2366
	ldr	r0, [r10, #4056]
	mov	r1, r6
	mov	r2, r5
	bl	FlashEraseBlocks
.L2366:
	mov	r0, r4
.L2342:
	add	sp, sp, #28
	@ sp needed
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L2392:
	.align	2
.L2391:
	.word	.LANCHOR0
	.word	.LANCHOR2
	.word	.LANCHOR0+1804
	.word	.LANCHOR0+1884
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
	ldr	r5, .L2411
	ldr	r6, [r5, #4052]
	cmp	r6, #0
	bne	.L2395
	ldr	r4, .L2411+4
	mov	r1, r6
	ldr	r7, .L2411+8
	ldr	r10, .L2411+12
	ldr	r3, [r4, #-2040]
	ldr	r0, [r4, #-2064]
	str	r3, [r4, #224]
	movw	r3, #1882
	ldrh	r2, [r5, r3]
	add	r5, r5, #1936
	str	r0, [r4, #220]
	bl	ftl_memset
	sub	r9, r5, #110
.L2396:
	ldrh	r3, [r9]
	ldr	r8, .L2411
	cmp	r6, r3
	bge	.L2410
	ldrh	r2, [r10]
	ldr	r3, [r4, #220]
	ldr	r1, [r7, #4]!
	mul	r0, r2, r6
	mov	r2, r2, asl #2
	add	r6, r6, #1
	add	r0, r3, r0, asl #2
	bl	ftl_memcpy
	b	.L2396
.L2410:
	ldr	r6, [r4, #224]
	mov	r1, #255
	mov	r2, #16
	ldr	fp, .L2411+4
	mov	r7, #0
	mov	r0, r6
	mov	r10, r7
	bl	ftl_memset
	ldr	r3, .L2411+16
	strh	r3, [r6]	@ movhi
	ldr	r3, [r8, #1944]
	str	r3, [r6, #4]
	ldrh	r3, [r5]
	strh	r3, [r6, #2]	@ movhi
	ldrh	r3, [r5, #4]
	strh	r3, [r6, #8]	@ movhi
	ldrh	r3, [r5, #6]
	strh	r3, [r6, #10]	@ movhi
	ldr	r3, [r8, #1800]
	strh	r3, [r6, #12]	@ movhi
.L2398:
	ldr	r3, [r4, #-2064]
	mov	ip, #0
	ldrh	r1, [r5]
	ldrh	r2, [r5, #2]
	str	r3, [r4, #220]
	ldr	r3, [r4, #-2040]
	str	ip, [r4, #212]
	str	ip, [sp, #12]
	str	r3, [r4, #224]
	orr	r3, r2, r1, asl #10
	ldrh	r0, [r6, #10]
	str	r3, [r4, #216]
	ldrh	r3, [r5, #4]
	str	r0, [sp]
	ldr	r0, .L2411+20
	bl	printk
	ldr	r9, .L2411+24
	ldrh	r2, [r5, #2]
	ldrh	r3, [r9, #-62]
	sub	r3, r3, #1
	cmp	r2, r3
	ldr	ip, [sp, #12]
	blt	.L2399
	ldr	r3, [r8, #1944]
	mov	r1, #1
	ldrh	r2, [r9]
	add	r3, r3, #1
	ldr	r0, [r8, #4056]
	str	r3, [r8, #1944]
	str	r3, [r6, #4]
	ldrh	r3, [r9, #4]
	strh	r2, [r6, #8]	@ movhi
	strh	r2, [r9, #4]	@ movhi
	mov	r2, r1
	strh	r3, [r9]	@ movhi
	mov	r3, r3, asl #10
	str	r3, [fp, #216]
	str	r3, [r0, #4]
	strh	ip, [r9, #2]	@ movhi
	bl	FlashEraseBlocks
.L2399:
	ldr	r9, .L2411+4
	mov	r1, #1
	mov	r3, r1
	mov	r2, r1
	add	r0, r9, #212
	bl	FlashProgPages
	ldrh	r3, [r5, #2]
	add	r3, r3, #1
	strh	r3, [r5, #2]	@ movhi
	ldr	r3, [r4, #212]
	cmn	r3, #1
	bne	.L2400
	add	r7, r7, #1
	ldr	r0, .L2411+28
	ldr	r1, [fp, #216]
	uxth	r7, r7
	bl	printk
	cmp	r7, #3
	bls	.L2398
	mov	r2, r7
	ldr	r0, .L2411+32
	ldr	r1, [r9, #216]
	bl	printk
	ldr	r3, .L2411
	mov	r2, #1
	str	r2, [r3, #4052]
	b	.L2395
.L2400:
	add	r10, r10, #1
	cmp	r10, #1
	beq	.L2398
	cmp	r3, #256
	beq	.L2398
.L2395:
	mov	r0, #0
	add	sp, sp, #20
	@ sp needed
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L2412:
	.align	2
.L2411:
	.word	.LANCHOR0
	.word	.LANCHOR2
	.word	.LANCHOR0+1960
	.word	.LANCHOR2-1976
	.word	-3887
	.word	.LC122
	.word	.LANCHOR0+1936
	.word	.LC123
	.word	.LC124
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
	ldr	r6, .L2468
	ldr	r3, [r6, #4052]
	cmp	r3, #0
	bne	.L2414
	ldr	r10, .L2468+4
	mov	r4, r0
	mov	r7, r10
.L2415:
	ldr	r3, .L2468+8
	ldrb	r2, [r4, #8]	@ zero_extendqisi2
	cmp	r4, r3
	bne	.L2416
	sub	r3, r3, #100
	ldr	ip, [r10, #-3224]
	ldrh	r3, [r3]
	mul	lr, ip, r3
	mov	r0, r3, lsr #1
	add	r1, r0, #1
	add	r1, r1, lr, lsr #2
	ldr	lr, [r6, #1796]
	cmp	lr, #0
	uxth	r1, r1
	beq	.L2417
	ldr	lr, [r10, #-3824]
	cmp	lr, #39
	bhi	.L2417
	cmp	lr, #2
	bls	.L2442
	cmp	ip, #0
	movne	r3, #0
	andeq	r3, r3, #1
	cmp	r3, #0
	moveq	r1, r0
	beq	.L2417
	b	.L2442
.L2416:
	cmp	r2, #1
	bne	.L2442
	ldr	r0, .L2468
	add	r3, r0, #1824
	ldrh	r3, [r3]
	cmp	r3, #1
	beq	.L2442
	ldrb	r3, [r0, #140]	@ zero_extendqisi2
	cmp	r3, #0
	bne	.L2442
	ldr	r3, .L2468+12
	ldr	r0, [r0, #1796]
	ldrh	r3, [r3]
	cmp	r0, #0
	mov	r1, r3, lsr #3
	beq	.L2417
	ldr	r0, [r10, #-3824]
	cmp	r0, #1
	rsbls	r3, r3, r3, asl #3
	ubfxls	r1, r3, #3, #16
.L2417:
	cmp	r1, #0
	subne	r1, r1, #1
	uxthne	r1, r1
	b	.L2418
.L2442:
	mov	r1, #0
.L2418:
	ldr	r0, .L2468+16
	bl	List_pop_index_node
	ldr	r2, .L2468+12
	ldrh	r3, [r2]
	sub	r3, r3, #1
	strh	r3, [r2]	@ movhi
	ldr	r3, .L2468+20
	ldrh	r3, [r3]
	uxth	r9, r0
	cmp	r3, r9
	bls	.L2415
	ldr	r3, [r10, #-4060]
	mov	r8, r9, asl #1
	ldrh	r5, [r3, r8]
	cmp	r5, #0
	bne	.L2415
	strh	r9, [r4]	@ movhi
	mov	r0, r4
	bl	make_superblock
	ldrb	r3, [r4, #7]	@ zero_extendqisi2
	cmp	r3, #0
	beq	.L2420
	ldr	r3, .L2468
	mov	fp, #36
	ldr	r2, .L2468+24
	add	r0, r4, #14
	ldr	r1, [r3, #4056]
	ldrh	ip, [r2]
	mov	r3, r1
	mla	r2, fp, ip, r1
	mov	ip, r5
	str	r2, [sp]
	b	.L2421
.L2420:
	ldr	r3, [r10, #-4060]
	b	.L2463
.L2421:
	ldr	r2, [sp]
	cmp	r3, r2
	beq	.L2465
	str	ip, [r3, #8]
	movw	lr, #65535
	str	ip, [r3, #12]
	add	r3, r3, #36
	ldrh	r2, [r0, #2]!
	cmp	r2, lr
	movne	r2, r2, asl #10
	mlane	lr, fp, r5, r1
	addne	r5, r5, #1
	uxthne	r5, r5
	strne	r2, [lr, #4]
	b	.L2421
.L2465:
	ldr	r3, .L2468+28
	ldr	r2, [r6, #1796]
	rsb	r3, r3, r4
	clz	r3, r3
	cmp	r2, #0
	mov	r3, r3, lsr #5
	moveq	r3, #0
	cmp	r3, #0
	beq	.L2424
	ldr	r3, .L2468
	ldr	r3, [r3, #4060]
	ldrh	r3, [r3, r8]
	cmp	r3, #40
	movhi	r3, #0
	strhib	r3, [r10, #-4032]
.L2424:
	ldrb	r3, [r4, #8]	@ zero_extendqisi2
	ldr	r2, [r6, #4060]
	cmp	r3, #0
	ldr	fp, .L2468+32
	ldrh	r3, [r2, r8]
	bne	.L2425
	cmp	r3, #0
	mov	r0, r9
	ldrneh	r1, [fp]
	moveq	r3, #2
	addne	r3, r3, r1
	mov	r1, #0
	uxthne	r3, r3
	strh	r3, [r2, r8]	@ movhi
	ldr	r3, [r7, #-3840]
	add	r3, r3, #1
	str	r3, [r7, #-3840]
	bl	ftl_set_blk_mode
	b	.L2427
.L2425:
	add	r3, r3, #1
	strh	r3, [r2, r8]	@ movhi
	ldr	r2, [r7, #-3884]
	mov	r1, r9, lsr #5
	ldr	r3, [r7, #-3836]
	mov	r0, #1
	add	r3, r3, #1
	str	r3, [r7, #-3836]
	ldr	ip, [r2, r1, asl #2]
	and	r3, r9, #31
	orr	r3, ip, r0, asl r3
	str	r3, [r2, r1, asl #2]
.L2427:
	ldr	r3, [r6, #4060]
	ldr	r2, [r7, #-3828]
	ldr	r0, [r7, #-3840]
	ldrh	r3, [r3, r8]
	cmp	r3, r2
	ldrh	r2, [fp]
	strhi	r3, [r10, #-3828]
	ldr	r3, [r7, #-3836]
	mla	r0, r0, r2, r3
	ldr	r3, .L2468+20
	ldrh	r1, [r3]
	bl	__aeabi_uidiv
	ldr	r2, [r7, #-2020]
	ldr	r1, [r6, #4056]
	ldr	r3, [r2, #16]
	add	r3, r3, #1
	str	r3, [r2, #16]
	mov	r3, #36
	add	r2, r1, #4
	mla	r3, r3, r5, r1
	add	r3, r3, #40
	str	r0, [r7, #-3832]
.L2429:
	add	r2, r2, #36
	cmp	r2, r3
	ldrne	r1, [r2, #-36]
	bicne	r1, r1, #1020
	bicne	r1, r1, #3
	strne	r1, [r2, #-36]
	bne	.L2429
.L2466:
	ldrb	r2, [r6, #140]	@ zero_extendqisi2
	ldr	r3, .L2468
	cmp	r2, #0
	beq	.L2431
	ldrb	r2, [r4, #8]	@ zero_extendqisi2
	ldr	r0, [r3, #4056]
	cmp	r2, #1
	mov	r2, r5
	moveq	r1, #0
	movne	r1, #1
	bl	FlashEraseBlocks
.L2431:
	ldrb	r1, [r4, #8]	@ zero_extendqisi2
	mov	r2, r5
	ldr	r0, [r6, #4056]
	mov	fp, #0
	bl	FlashEraseBlocks
	add	r1, r4, #16
	mov	r2, fp
	mov	ip, #36
.L2433:
	uxth	r3, fp
	cmp	r3, r5
	bcs	.L2467
	mul	r3, ip, fp
	ldr	lr, [r6, #4056]
	add	r0, lr, r3
	ldr	r3, [lr, r3]
	cmn	r3, #1
	bne	.L2434
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
	ldrb	r3, [r4, #7]	@ zero_extendqisi2
	sub	r3, r3, #1
	strb	r3, [r4, #7]
.L2434:
	add	fp, fp, #1
	add	r1, r1, #2
	b	.L2433
.L2467:
	cmp	r2, #0
	beq	.L2436
	mov	r0, r9
	bl	update_multiplier_value
	bl	FtlBbmTblFlush
.L2436:
	ldrb	r3, [r4, #7]	@ zero_extendqisi2
	cmp	r3, #0
	bne	.L2437
	ldr	r3, [r7, #-4060]
.L2463:
	mvn	r2, #0
	strh	r2, [r3, r8]	@ movhi
	b	.L2415
.L2437:
	ldr	r2, .L2468+36
	ldr	r1, .L2468+4
	ldrh	r2, [r2]
	strh	r9, [r4]	@ movhi
	smulbb	r3, r2, r3
	mov	r2, #0
	strh	r2, [r4, #2]	@ movhi
	strb	r2, [r4, #6]
	ldr	r2, .L2468+4
	uxth	r3, r3
	strh	r3, [r4, #4]	@ movhi
	ldr	r2, [r2, #-3848]
	str	r2, [r4, #12]
	add	r2, r2, #1
	str	r2, [r1, #-3848]
	ldrh	r2, [r4]
	ldr	r1, [r1, #-4060]
	mov	r2, r2, asl #1
	strh	r3, [r1, r2]	@ movhi
.L2414:
	mov	r0, #0
	add	sp, sp, #20
	@ sp needed
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L2469:
	.align	2
.L2468:
	.word	.LANCHOR0
	.word	.LANCHOR2
	.word	.LANCHOR2-3944
	.word	.LANCHOR2-4044
	.word	.LANCHOR2-4048
	.word	.LANCHOR0+1812
	.word	.LANCHOR0+1804
	.word	.LANCHOR2-4040
	.word	.LANCHOR0+1864
	.word	.LANCHOR0+1872
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
	ldr	r5, .L2484
	ldrh	r3, [r5, #-6]
	cmp	r3, #0
	beq	.L2472
	sub	r5, r5, #6
	mov	r8, r0
	mov	r7, #0
	mov	r9, r5
.L2471:
	ldr	r3, .L2484+4
	uxth	r2, r7
	ldrh	r3, [r3]
	cmp	r3, r2
	bls	.L2481
	ldr	r3, .L2484+8
	mov	r1, r8
	mov	r10, #0
	add	r3, r3, r2
	ldrb	r0, [r3, #1832]	@ zero_extendqisi2
	bl	V2P_block
	ldr	ip, .L2484+12
	mov	fp, r0
.L2473:
	ldrh	r3, [r5]
	uxth	r4, r10
	cmp	r3, r4
	bls	.L2482
	mov	r3, r4, asl #1
	add	r6, ip, r3
	ldrh	r3, [ip, r3]
	cmp	r3, fp
	bne	.L2474
	mov	r1, fp
	ldr	r0, .L2484+16
	str	ip, [sp, #4]
	bl	printk
	mov	r0, fp
	bl	FtlBbmMapBadBlock
	bl	FtlBbmTblFlush
	ldrh	r2, [r5]
	mov	r3, r6
	ldr	ip, [sp, #4]
.L2475:
	cmp	r4, r2
	ldrcch	r1, [r3, #2]
	addcc	r4, r4, #1
	uxthcc	r4, r4
	strcch	r1, [r3], #2	@ movhi
	bcc	.L2475
.L2483:
	sub	r2, r2, #1
	strh	r2, [r9]	@ movhi
.L2474:
	add	r10, r10, #1
	b	.L2473
.L2482:
	add	r7, r7, #1
	b	.L2471
.L2481:
	bl	FtlGcReFreshBadBlk
.L2472:
	mov	r0, #0
	add	sp, sp, #12
	@ sp needed
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L2485:
	.align	2
.L2484:
	.word	.LANCHOR2-3168
	.word	.LANCHOR0+1804
	.word	.LANCHOR0
	.word	.LANCHOR2-3172
	.word	.LC125
	.fnend
	.size	FtlGcFreeBadSuperBlk, .-FtlGcFreeBadSuperBlk
	.align	2
	.global	update_vpc_list
	.type	update_vpc_list, %function
update_vpc_list:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	ldr	r2, .L2496
	mov	r3, r0, asl #1
	stmfd	sp!, {r4, lr}
	.save {r4, lr}
	mov	r4, r0
	ldr	r1, [r2, #-4060]
	ldrh	r3, [r1, r3]
	cmp	r3, #0
	bne	.L2487
	sub	r1, r2, #3792
	ldrh	r0, [r1, #-8]
	cmp	r0, r4
	mvneq	r3, #0
	streqh	r3, [r1, #-8]	@ movhi
	beq	.L2489
	sub	r1, r2, #4032
	ldrh	r1, [r1, #-8]
	cmp	r1, r4
	beq	.L2495
	sub	r1, r2, #3984
	ldrh	r1, [r1, #-8]
	cmp	r1, r4
	beq	.L2495
	sub	r2, r2, #3936
	ldrh	r2, [r2, #-8]
	cmp	r2, r4
	beq	.L2495
.L2489:
	mov	r1, r4
	ldr	r0, .L2496+4
	bl	List_remove_node
	ldr	r2, .L2496+8
	mov	r0, r4
	ldrh	r3, [r2, #-4]
	sub	r3, r3, #1
	strh	r3, [r2, #-4]	@ movhi
	bl	free_data_superblock
	mov	r0, r4
	bl	FtlGcFreeBadSuperBlk
	mov	r0, #1
	ldmfd	sp!, {r4, pc}
.L2487:
	bl	List_update_data_list
.L2495:
	mov	r0, #0
	ldmfd	sp!, {r4, pc}
.L2497:
	.align	2
.L2496:
	.word	.LANCHOR2
	.word	.LANCHOR2-4064
	.word	.LANCHOR2-4048
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
	beq	.L2499
	ldr	r5, .L2509
	mov	r6, r0, asl #1
	ldr	r3, [r5, #-4060]
	ldrh	r2, [r3, r6]
	cmp	r2, #0
	subne	r2, r2, #1
	strneh	r2, [r3, r6]	@ movhi
	bne	.L2499
	mov	r1, r4
	ldr	r0, .L2509+4
	bl	printk
	ldr	r3, [r5, #-4060]
	mov	r2, #32
	sub	r0, r5, #4048
	mov	r1, r4
	strh	r2, [r3, r6]	@ movhi
	bl	test_node_in_list
	cmp	r0, #0
	beq	.L2501
	mov	r1, r4
	sub	r0, r5, #4048
	bl	List_remove_node
	sub	r2, r5, #4032
	mov	r0, r4
	ldrh	r3, [r2, #-12]
	sub	r3, r3, #1
	strh	r3, [r2, #-12]	@ movhi
	bl	INSERT_DATA_LIST
	ldr	r3, [r5, #-4060]
	ldr	r0, .L2509+8
	mov	r1, r4
	ldrh	r2, [r3, r6]
	bl	printk
.L2501:
	mov	r0, r4
	bl	FtlGcRefreshBlock
	b	.L2504
.L2499:
	ldr	r5, .L2509+12
	movw	r3, #65535
	ldrh	r0, [r5]
	cmp	r0, r3
	streqh	r4, [r5]	@ movhi
	beq	.L2504
	cmp	r0, r4
	beq	.L2504
	bl	update_vpc_list
	strh	r4, [r5]	@ movhi
	adds	r0, r0, #0
	movne	r0, #1
	ldmfd	sp!, {r4, r5, r6, pc}
.L2504:
	mov	r0, #0
	ldmfd	sp!, {r4, r5, r6, pc}
.L2510:
	.align	2
.L2509:
	.word	.LANCHOR2
	.word	.LC126
	.word	.LC127
	.word	.LANCHOR2-1920
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
	ldr	r5, .L2525
	ldr	r6, .L2525+4
	add	r3, r0, r3, asl #1
	ldr	r7, .L2525+8
	ldrh	r3, [r3, #16]
.L2515:
	movw	r1, #65535
	cmp	r3, r1
	bne	.L2524
.L2517:
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
	b	.L2515
.L2524:
	ldrb	r2, [r4, #8]	@ zero_extendqisi2
	cmp	r2, #1
	bne	.L2518
	ldrb	r3, [r5, #140]	@ zero_extendqisi2
	cmp	r3, #0
	bne	.L2518
	ldrh	r3, [r4, #2]
	mov	r3, r3, asl #1
	ldrh	r3, [r6, r3]
	cmp	r3, r1
	bne	.L2518
	ldrh	r3, [r4, #4]
	ldrh	r0, [r4]
	sub	r3, r3, #1
	strh	r3, [r4, #4]	@ movhi
	bl	decrement_vpc_count
	ldrh	r2, [r4, #4]
	cmp	r2, #0
	bne	.L2517
	ldrh	r3, [r4, #2]
	add	r3, r3, #1
	b	.L2523
.L2518:
	ldr	r3, .L2525
	ldrb	r1, [r3, #140]	@ zero_extendqisi2
	cmp	r1, #0
	ldmeqfd	sp!, {r3, r4, r5, r6, r7, pc}
	cmp	r2, #1
	ldmnefd	sp!, {r3, r4, r5, r6, r7, pc}
	movw	r2, #1874
	ldrh	r1, [r4, #2]
	ldrh	r2, [r3, r2]
	cmp	r1, r2
	ldmccfd	sp!, {r3, r4, r5, r6, r7, pc}
	ldr	r1, .L2525+12
	add	r3, r3, #1872
	ldrh	r2, [r4]
	ldrh	ip, [r4, #4]
	ldr	r0, [r1, #-4060]
	mov	r2, r2, asl #1
	ldrh	r1, [r0, r2]
	rsb	r1, ip, r1
	strh	r1, [r0, r2]	@ movhi
	ldrh	r3, [r3]
	mov	r2, #0
	strh	r2, [r4, #4]	@ movhi
.L2523:
	strh	r3, [r4, #2]	@ movhi
	strb	r2, [r4, #6]
	ldmfd	sp!, {r3, r4, r5, r6, r7, pc}
.L2526:
	.align	2
.L2525:
	.word	.LANCHOR0
	.word	.LANCHOR2-3136
	.word	.LANCHOR0+1804
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
	ldr	r7, .L2545
	ldr	r8, .L2545+4
	add	r3, r0, r3, asl #1
	ldr	r9, .L2545+8
	ldrh	r2, [r3, #16]
.L2528:
	movw	r1, #65535
	cmp	r2, r1
	ldr	r6, .L2545
	bne	.L2544
.L2529:
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
	b	.L2528
.L2544:
	ldrb	r3, [r4, #8]	@ zero_extendqisi2
	ldrh	r5, [r4, #2]
	cmp	r3, #1
	ldrh	r3, [r4, #4]
	bne	.L2531
	ldrb	r0, [r8, #140]	@ zero_extendqisi2
	cmp	r0, #0
	bne	.L2531
	mov	r0, r5, asl #1
	ldrh	r0, [r9, r0]
	cmp	r0, r1
	bne	.L2531
	sub	r3, r3, #1
	ldrh	r0, [r4]
	strh	r3, [r4, #4]	@ movhi
	bl	decrement_vpc_count
	b	.L2529
.L2531:
	ldr	r7, .L2545+4
	orr	r5, r5, r2, asl #10
	sub	r3, r3, #1
	strh	r3, [r4, #4]	@ movhi
.L2532:
	ldrb	r3, [r4, #6]	@ zero_extendqisi2
	movw	r2, #65535
	ldrh	r0, [r6]
.L2534:
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
	beq	.L2534
	strb	r3, [r4, #6]
	ldrb	r3, [r4, #8]	@ zero_extendqisi2
	cmp	r3, #1
	bne	.L2539
	ldrb	r3, [r7, #140]	@ zero_extendqisi2
	cmp	r3, #0
	bne	.L2536
	ldrh	r3, [r4, #2]
	ldr	r2, .L2545+8
	mov	r3, r3, asl #1
	ldrh	r2, [r2, r3]
	movw	r3, #65535
	cmp	r2, r3
	bne	.L2536
	ldrh	r3, [r4, #4]
	cmp	r3, #0
	beq	.L2536
	sub	r3, r3, #1
	ldrh	r0, [r4]
	strh	r3, [r4, #4]	@ movhi
	bl	decrement_vpc_count
	b	.L2532
.L2536:
	ldr	r3, .L2545+4
	ldrb	r2, [r3, #140]	@ zero_extendqisi2
	cmp	r2, #0
	beq	.L2539
	movw	r2, #1874
	ldrh	r1, [r4, #2]
	ldrh	r2, [r3, r2]
	cmp	r1, r2
	bcc	.L2539
	ldr	r1, .L2545+12
	add	r3, r3, #1872
	ldrh	r2, [r4]
	ldrh	ip, [r4, #4]
	ldr	r0, [r1, #-4060]
	mov	r2, r2, asl #1
	ldrh	r1, [r0, r2]
	rsb	r1, ip, r1
	strh	r1, [r0, r2]	@ movhi
	ldrh	r3, [r3]
	mov	r2, #0
	strh	r2, [r4, #4]	@ movhi
	strb	r2, [r4, #6]
	strh	r3, [r4, #2]	@ movhi
.L2539:
	mov	r0, r5
	ldmfd	sp!, {r3, r4, r5, r6, r7, r8, r9, pc}
.L2546:
	.align	2
.L2545:
	.word	.LANCHOR0+1804
	.word	.LANCHOR0
	.word	.LANCHOR2-3136
	.word	.LANCHOR2
	.fnend
	.size	get_new_active_ppa, .-get_new_active_ppa
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
	ldr	r5, .L2565
	ldr	r3, [r5, #4052]
	cmp	r3, #0
	bne	.L2549
	ldr	r4, .L2565+4
	mov	r1, #255
	ldr	r8, .L2565+8
	ldr	r2, [r4, #-2064]
	ldr	r6, [r4, #-2040]
	str	r2, [r4, #220]
	sub	r2, r4, #3808
	sub	r7, r2, #8
	str	r6, [r4, #224]
	ldrh	r2, [r2, #-8]
	str	r3, [r6, #12]
	strh	r2, [r6, #2]	@ movhi
	ldr	r2, .L2565+12
	strh	r2, [r6]	@ movhi
	ldr	r2, [r4, #-3808]
	stmib	r6, {r2, r3}
	ldr	r3, .L2565+16
	ldrh	r2, [r7, #-222]
	str	r3, [r5, #4068]
	ldr	r3, .L2565+20
	str	r3, [r5, #4072]
	ldrh	r3, [r7, #6]
	strh	r3, [r8, #8]	@ movhi
	movw	r3, #1826
	ldrh	r3, [r5, r3]
	strb	r3, [r5, #4078]
	sub	r3, r4, #4032
	ldrh	r3, [r3, #-8]
	strh	r3, [r8, #14]	@ movhi
	ldrb	r3, [r4, #-4034]	@ zero_extendqisi2
	orr	r3, r3, r2, asl #6
	strh	r3, [r8, #16]	@ movhi
	ldrb	r3, [r4, #-4032]	@ zero_extendqisi2
	ldrh	r2, [r7, #-174]
	strb	r3, [r5, #4079]
	sub	r3, r4, #3984
	ldrh	r3, [r3, #-8]
	strh	r3, [r8, #18]	@ movhi
	ldrb	r3, [r4, #-3986]	@ zero_extendqisi2
	orr	r3, r3, r2, asl #6
	strh	r3, [r8, #20]	@ movhi
	ldrb	r3, [r4, #-3984]	@ zero_extendqisi2
	strb	r3, [r5, #4080]
	sub	r3, r4, #3936
	ldrh	r3, [r3, #-8]
	strh	r3, [r8, #22]	@ movhi
	ldrh	r2, [r7, #-126]
	ldrb	r3, [r4, #-3938]	@ zero_extendqisi2
	ldr	r0, [r4, #220]
	orr	r3, r3, r2, asl #6
	strh	r3, [r8, #24]	@ movhi
	ldrb	r3, [r4, #-3936]	@ zero_extendqisi2
	ldr	r2, [r4, #-3840]
	strb	r3, [r5, #4081]
	add	r3, r5, #4096
	str	r2, [r3, #4]
	ldr	r2, [r4, #-3848]
	str	r2, [r3, #12]
	ldr	r2, [r4, #-3844]
	str	r2, [r3, #8]
	sub	r3, r4, #3168
	ldrh	r2, [r3, #-14]
	ldrh	r3, [r3, #-12]
	strh	r2, [r8, #44]	@ movhi
	strh	r3, [r8, #46]	@ movhi
	movw	r3, #1882
	ldrh	r2, [r5, r3]
	bl	ftl_memset
	mov	r1, r8
	mov	r2, #48
	movw	r8, #1812
	ldr	r0, [r4, #220]
	bl	ftl_memcpy
	ldrh	r2, [r5, r8]
	ldr	r0, [r4, #220]
	ldr	r1, [r4, #-4060]
	mov	r2, r2, asl #1
	add	r0, r0, #48
	bl	ftl_memcpy
	ldrh	r2, [r5, r8]
	ldr	r0, [r4, #220]
	add	r3, r2, #24
	ldr	r1, [r4, #-3884]
	ubfx	r3, r3, #1, #30
	mov	r2, r2, lsr #3
	add	r2, r2, #4
	add	r0, r0, r3, asl #2
	bl	ftl_memcpy
	movw	r3, #1916
	ldrh	r3, [r5, r3]
	cmp	r3, #0
	beq	.L2550
	ldrh	r2, [r5, r8]
	ldr	r0, [r4, #220]
	ldr	r1, [r4, #-1992]
	mov	r3, r2, lsr #3
	add	r3, r3, r2, asl #1
	movw	r2, #1908
	add	r3, r3, #52
	ldrh	r2, [r5, r2]
	ubfx	r3, r3, #2, #14
	mov	r2, r2, asl #2
	add	r0, r0, r3, asl #2
	bl	ftl_memcpy
.L2550:
	mov	r0, #0
	ldr	r9, .L2565+4
	bl	FtlUpdateVaildLpn
	ldr	fp, .L2565+24
	mov	r8, #0
	movw	r10, #65535
.L2551:
	ldrh	r1, [r7]
	ldrh	r0, [r7, #2]
	ldr	r3, [r4, #-2064]
	orr	r2, r0, r1, asl #10
	str	r2, [r4, #216]
	ldrh	r2, [fp]
	str	r3, [r4, #220]
	sub	r2, r2, #1
	ldr	r3, [r4, #-2040]
	cmp	r0, r2
	str	r3, [r4, #224]
	ldr	r3, .L2565+28
	blt	.L2552
	ldrh	r10, [r3, #4]
	mov	r2, #0
	strh	r1, [r3, #4]	@ movhi
	strh	r2, [r3, #2]	@ movhi
	str	r3, [sp, #4]
	bl	FtlFreeSysBlkQueueOut
	ldr	r3, [sp, #4]
	strh	r0, [r3]	@ movhi
	ldr	r3, [r9, #-3848]
	add	r2, r3, #1
	str	r2, [r9, #-3848]
	str	r3, [r9, #-3808]
	mov	r2, r0, asl #10
	str	r2, [r9, #216]
	str	r3, [r6, #4]
	strh	r0, [r6, #2]	@ movhi
.L2552:
	ldrb	r3, [r5]	@ zero_extendqisi2
	cmp	r3, #0
	beq	.L2553
	ldr	r3, .L2565+32
	ldr	r0, [r4, #-2064]
	ldrh	r1, [r3]
	bl	js_hash
	str	r0, [r6, #12]
.L2553:
	mov	r1, #1
	ldr	r0, .L2565+36
	mov	r2, r1
	mov	r3, r1
	bl	FlashProgPages
	ldrh	r3, [r7, #2]
	ldr	r2, [r4, #212]
	add	r3, r3, #1
	cmn	r2, #1
	uxth	r3, r3
	strh	r3, [r7, #2]	@ movhi
	bne	.L2554
	cmp	r3, #1
	add	r8, r8, #1
	ldreq	r3, .L2565+24
	uxth	r8, r8
	ldreq	r2, .L2565+28
	ldreqh	r3, [r3]
	subeq	r3, r3, #1
	streqh	r3, [r2, #2]	@ movhi
	cmp	r8, #3
	bls	.L2551
	ldr	r0, .L2565+40
	mov	r2, r8
	ldr	r1, [r4, #216]
	bl	printk
	mov	r3, #1
	str	r3, [r5, #4052]
	b	.L2549
.L2554:
	cmp	r2, #256
	cmpne	r3, #1
	beq	.L2551
	movw	r3, #65535
	cmp	r10, r3
	beq	.L2549
	mov	r0, r10
	mov	r1, #1
	bl	FtlFreeSysBlkQueueIn
.L2549:
	mov	r0, #0
	add	sp, sp, #12
	@ sp needed
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L2566:
	.align	2
.L2565:
	.word	.LANCHOR0
	.word	.LANCHOR2
	.word	.LANCHOR0+4068
	.word	-3932
	.word	1179929683
	.word	1342177366
	.word	.LANCHOR0+1874
	.word	.LANCHOR2-3816
	.word	.LANCHOR0+1882
	.word	.LANCHOR2+212
	.word	.LC128
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
	ldr	r8, .L2585
	ldr	r3, [r8]
	str	r3, [sp, #36]
	ldr	r3, .L2585+4
	ldr	r5, [r3, #4052]
	cmp	r5, #0
	bne	.L2567
	ldrb	r6, [r3, #140]	@ zero_extendqisi2
	mov	r4, r0
	cmp	r6, #0
	beq	.L2584
	ldrb	r6, [r0, #8]	@ zero_extendqisi2
	cmp	r6, #1
	ldreqh	fp, [r0, #4]
	beq	.L2569
	mov	r6, r5
.L2584:
	mov	fp, #12
.L2569:
	mvn	r3, #0
	str	r3, [sp, #16]
	ldr	r3, .L2585+8
	mov	r9, #0
	movw	r1, #61589
	ldr	r7, [r3, #-2040]
	mov	r5, r3
	ldr	r2, [r3, #-2064]
	mov	r10, r3
	str	r7, [sp, #12]
	str	r2, [sp, #8]
	mvn	r2, #2
	str	r2, [r7, #8]
	mvn	r2, #1
	str	r2, [r7, #12]
	ldrh	r2, [r4]
	strh	r9, [r7]	@ movhi
	strh	r2, [r7, #2]	@ movhi
	ldr	r2, [r3, #-2064]
	str	r1, [r2]
	ldr	r1, .L2585+12
	ldr	r2, [r3, #-2064]
	str	r1, [r2, #4]
.L2570:
	subs	fp, fp, #1
	bcc	.L2573
	ldrh	r3, [r4, #4]
	cmp	r3, #0
	bne	.L2571
.L2573:
	ldrh	r3, [r4]
	ldr	r1, [r5, #-4060]
	ldrh	r0, [r4, #4]
	mov	r3, r3, asl #1
	ldrh	r2, [r1, r3]
	rsb	r2, r0, r2
	strh	r2, [r1, r3]	@ movhi
	ldr	r3, .L2585+16
	ldrh	r3, [r3]
	strh	r3, [r4, #2]	@ movhi
	mov	r3, #0
	strb	r3, [r4, #6]
	strh	r3, [r4, #4]	@ movhi
	b	.L2567
.L2571:
	mov	r0, r4
	bl	get_new_active_ppa
	cmn	r0, #1
	str	r0, [sp, #4]
	beq	.L2573
	ldr	r3, [r5, #-3844]
	mov	r1, #1
	mov	r2, r6
	mov	r0, sp
	str	r3, [r7, #4]
	add	r3, r3, #1
	cmn	r3, #1
	moveq	r3, r9
	str	r3, [r10, #-3844]
	mov	r3, #0
	bl	FlashProgPages
	ldrh	r0, [r4]
	bl	decrement_vpc_count
	b	.L2570
.L2567:
	ldr	r2, [sp, #36]
	ldr	r3, [r8]
	cmp	r2, r3
	beq	.L2577
	bl	__stack_chk_fail
.L2577:
	add	sp, sp, #44
	@ sp needed
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L2586:
	.align	2
.L2585:
	.word	__stack_chk_guard
	.word	.LANCHOR0
	.word	.LANCHOR2
	.word	305419896
	.word	.LANCHOR0+1872
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
	ldr	r8, [r0, #24]
	bl	ftl_free_no_use_map_blk
	ldrh	r3, [r4, #10]
	ldrh	r2, [r4, #8]
	sub	r3, r3, #4
	ldr	r7, .L2601
	cmp	r2, r3
	blt	.L2588
	uxth	r0, r0
	mov	r0, r0, asl #1
	ldrh	r10, [r5, r0]
	cmp	r10, #0
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
	movw	r3, #1874
	ldrh	r3, [r7, r3]
	cmp	r2, r3
	bcc	.L2589
	mov	r0, r4
	bl	ftl_map_blk_alloc_new_blk
.L2589:
	ldr	r5, .L2601+4
	mov	fp, #0
.L2590:
	ldrh	r3, [r4, #6]
	uxth	r6, fp
	cmp	r3, r6
	bls	.L2600
	ldr	r2, [r8, r6, asl #2]
	add	r3, r8, r6, asl #2
	cmp	r10, r2, lsr #10
	bne	.L2591
	ldr	r2, [r5, #-2060]
	mov	r1, #1
	ldr	r9, [r5, #-2040]
	ldr	r0, .L2601+8
	str	r2, [r5, #220]
	str	r9, [r5, #224]
	ldr	r2, [r8, r6, asl #2]
	str	r3, [sp, #4]
	str	r2, [r5, #216]
	mov	r2, r1
	bl	FlashReadPages
	ldr	r2, [r5, #212]
	cmn	r2, #1
	ldr	r3, [sp, #4]
	bne	.L2592
.L2594:
	mov	r2, #0
	str	r2, [r3]
	ldr	r3, .L2601+4
	ldr	r0, .L2601+12
	ldrh	r2, [r9, #8]
	ldr	r1, [r3, #216]
	bl	printk
	mov	r3, #1
	str	r3, [r7, #4052]
	b	.L2593
.L2592:
	ldrh	r1, [r9, #8]
	cmp	r1, r6
	bne	.L2594
	ldrh	r0, [r9]
	ldrh	r2, [r4, #4]
	cmp	r0, r2
	bne	.L2594
	mov	r0, r4
	ldr	r2, [r5, #220]
	bl	FtlMapWritePage
.L2591:
	add	fp, fp, #1
	b	.L2590
.L2600:
	mov	r0, r10
	mov	r1, #1
	bl	FtlFreeSysBlkQueueIn
	mov	r3, #0
	str	r3, [r4, #32]
.L2588:
	movw	r3, #1874
	ldrh	r2, [r4, #2]
	ldrh	r3, [r7, r3]
	cmp	r2, r3
	bcc	.L2593
	mov	r0, r4
	bl	ftl_map_blk_alloc_new_blk
.L2593:
	mov	r0, #0
	add	sp, sp, #12
	@ sp needed
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L2602:
	.align	2
.L2601:
	.word	.LANCHOR0
	.word	.LANCHOR2
	.word	.LANCHOR2+212
	.word	.LC129
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
	ldr	r6, .L2615
	ldr	r5, [r0, #12]
	ldr	r7, [r0, #24]
	ldr	r10, [r6, #4052]
	cmp	r10, #0
	bne	.L2604
	ldrh	r3, [r0]
	movw	r2, #65535
	mov	r4, r0
	cmp	r3, r2
	bne	.L2605
	ldrh	r3, [r0, #8]
	add	r3, r3, #1
	strh	r3, [r0, #8]	@ movhi
	bl	FtlFreeSysBlkQueueOut
	strh	r0, [r5]	@ movhi
	ldr	r3, [r4, #28]
	strh	r10, [r4, #2]	@ movhi
	add	r3, r3, #1
	strh	r10, [r4]	@ movhi
	str	r3, [r4, #28]
	b	.L2604
.L2605:
	mov	r3, r3, asl #1
	mov	r1, #255
	ldrh	r9, [r5, r3]
	ldr	r5, .L2615+4
	ldrh	r3, [r0, #2]
	orr	r3, r3, r9, asl #10
	str	r3, [r5, #216]
	ldr	r3, [r5, #-2064]
	ldr	r8, [r5, #-2040]
	str	r3, [r5, #220]
	ldr	r3, [r0, #28]
	str	r8, [r5, #224]
	str	r3, [r8, #4]
	ldr	r3, .L2615+8
	strh	r3, [r8, #8]	@ movhi
	ldrh	r3, [r0, #4]
	strh	r9, [r8, #2]	@ movhi
	strh	r3, [r8]	@ movhi
	movw	r3, #1874
	ldrh	r2, [r6, r3]
	ldr	r0, [r5, #-2064]
	mov	r2, r2, asl #3
	bl	ftl_memset
	mov	r1, r10
	mov	r3, r10
.L2606:
	ldrh	r0, [r4, #6]
	uxth	r2, r1
	cmp	r0, r2
	bls	.L2614
	ldr	r0, [r7, r2, asl #2]
	cmp	r9, r0, lsr #10
	bne	.L2607
	add	r3, r3, #1
	ldr	r0, [r5, #-2064]
	uxth	r3, r3
	str	r2, [r0, r3, asl #3]
	ldr	r0, [r7, r2, asl #2]
	ldr	r2, [r5, #-2064]
	add	r2, r2, r3, asl #3
	str	r0, [r2, #4]
.L2607:
	add	r1, r1, #1
	b	.L2606
.L2614:
	ldrb	r3, [r6]	@ zero_extendqisi2
	cmp	r3, #0
	beq	.L2609
	ldr	r2, .L2615
	movw	r3, #1882
	ldr	r0, [r5, #220]
	ldrh	r1, [r2, r3]
	bl	js_hash
	str	r0, [r8, #12]
.L2609:
	mov	r1, #1
	mov	r3, #0
	ldr	r0, .L2615+12
	mov	r2, r1
	bl	FlashProgPages
	ldrh	r3, [r4, #2]
	mov	r0, r4
	add	r3, r3, #1
	strh	r3, [r4, #2]	@ movhi
	bl	ftl_map_blk_gc
.L2604:
	mov	r0, #0
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, pc}
.L2616:
	.align	2
.L2615:
	.word	.LANCHOR0
	.word	.LANCHOR2
	.word	-1291
	.word	.LANCHOR2+212
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
	mov	r10, r0
	ldr	r6, .L2636
	mov	r8, r1
	mov	r4, #0
	str	r2, [sp]
	mov	r5, r6
.L2618:
	ldr	r3, [r6, #-3864]
	ldr	r9, .L2636
	add	r3, r3, #1
	str	r3, [r6, #-3864]
	ldr	r3, .L2636+4
	ldrh	r2, [r10, #2]
	ldrh	r3, [r3]
	sub	r3, r3, #1
	cmp	r2, r3
	bge	.L2619
	ldrh	r2, [r10]
	movw	r3, #65535
	cmp	r2, r3
	bne	.L2620
.L2619:
	mov	r0, r10
	bl	Ftl_write_map_blk_to_last_page
.L2620:
	ldr	r3, .L2636+8
	ldr	r1, [r3, #4052]
	mov	fp, r3
	str	r3, [sp, #4]
	cmp	r1, #0
	bne	.L2621
	ldrh	r2, [r10]
	ldr	r0, [r10, #12]
	ldr	r3, [sp]
	mov	r2, r2, asl #1
	ldrh	r7, [r0, r2]
	ldrh	r2, [r10, #2]
	ldr	r0, [r5, #-2040]
	orr	r2, r2, r7, asl #10
	str	r2, [r5, #216]
	mov	r2, #16
	str	r3, [r5, #220]
	str	r0, [r5, #224]
	bl	ftl_memset
	ldr	r1, [r10, #28]
	ldr	r2, [r5, #224]
	str	r1, [r2, #4]
	strh	r8, [r2, #8]	@ movhi
	ldrh	r1, [r10, #4]
	strh	r7, [r2, #2]	@ movhi
	strh	r1, [r2]	@ movhi
	ldr	r3, [sp, #4]
	ldrb	r3, [r3]	@ zero_extendqisi2
	cmp	r3, #0
	beq	.L2622
	ldr	r3, .L2636+12
	ldr	r0, [r6, #220]
	str	r2, [sp, #4]
	ldrh	r1, [r3]
	bl	js_hash
	ldr	r2, [sp, #4]
	str	r0, [r2, #12]
.L2622:
	mov	r1, #1
	ldr	r0, .L2636+16
	mov	r2, r1
	mov	r3, r1
	bl	FlashProgPages
	ldrh	r3, [r10, #2]
	add	r3, r3, #1
	uxth	r3, r3
	strh	r3, [r10, #2]	@ movhi
	ldr	r2, [r5, #212]
	cmn	r2, #1
	bne	.L2623
	ldr	r0, .L2636+20
	add	r4, r4, #1
	ldr	r1, [r6, #216]
	bl	printk
	ldrh	r3, [r10, #2]
	uxth	r4, r4
	cmp	r3, #2
	ldrls	r3, .L2636+4
	ldrlsh	r3, [r3]
	subls	r3, r3, #1
	strlsh	r3, [r10, #2]	@ movhi
	cmp	r4, #3
	bls	.L2618
	ldr	r0, .L2636+24
	mov	r2, r4
	ldr	r1, [r9, #216]
	bl	printk
	mov	r3, #1
	str	r3, [fp, #4052]
	b	.L2621
.L2623:
	cmp	r2, #0
	strneh	r7, [r10, #40]	@ movhi
	cmp	r2, #256
	cmpne	r3, #1
	beq	.L2627
	ldr	r3, [r10, #36]
	cmp	r3, #0
	beq	.L2628
.L2627:
	mov	r3, #0
	str	r3, [r10, #36]
	b	.L2618
.L2628:
	ldr	r2, [r9, #216]
	ldr	r3, [r10, #24]
	str	r2, [r3, r8, asl #2]
.L2621:
	mov	r0, #0
	add	sp, sp, #12
	@ sp needed
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L2637:
	.align	2
.L2636:
	.word	.LANCHOR2
	.word	.LANCHOR0+1874
	.word	.LANCHOR0
	.word	.LANCHOR0+1882
	.word	.LANCHOR2+212
	.word	.LC130
	.word	.LC131
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
	ldr	r5, .L2640
	mul	r4, r4, r0
	sub	r0, r5, #1968
	ldr	r3, [r5, #-3896]
	sub	r0, r0, #4
	add	r2, r3, r4
	ldrh	r1, [r3, r4]
	ldr	r2, [r2, #8]
	bl	FtlMapWritePage
	ldr	r3, [r5, #-3896]
	mov	r0, #0
	add	r4, r3, r4
	ldr	r3, [r4, #4]
	bic	r3, r3, #-2147483648
	str	r3, [r4, #4]
	ldmfd	sp!, {r3, r4, r5, pc}
.L2641:
	.align	2
.L2640:
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
	ldr	r5, .L2685
	str	r3, [sp, #4]
	mov	r2, r10, asl #2
	ldrh	r3, [r0, #8]
	ldr	r9, [r0, #12]
	mov	r0, r8
	ldr	fp, .L2685+4
	str	r3, [sp]
	bl	ftl_memset
	ldr	r3, [r5, #-2064]
	ldr	r7, [r5, #-2040]
	str	r6, [r4, #32]
	str	r3, [r5, #220]
	mvn	r3, #0
	str	r7, [r5, #224]
	str	r6, [r4, #28]
	strh	r3, [r4]	@ movhi
	strh	r3, [r4, #2]	@ movhi
	mov	r3, #1
	str	r3, [r4, #36]
.L2643:
	uxth	r2, r6
	ldr	r1, [sp]
	sxth	r3, r2
	cmp	r3, r1
	bge	.L2661
	ldr	r1, [sp]
	sub	r1, r1, #1
	cmp	r3, r1
	mov	r1, r3, asl #1
	bne	.L2644
	ldrh	r0, [r9, r1]
	mov	r6, #0
	str	r3, [sp, #12]
	add	r3, r9, r1
	mov	r1, #1
	str	r2, [sp, #8]
	str	r3, [sp]
	bl	FtlGetLastWrittenPage
	ldr	fp, .L2685
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
.L2645:
	sxth	r3, r6
	cmp	r3, r9
	bge	.L2661
	ldr	r2, [sp]
	mov	r1, #1
	ldr	r0, .L2685+8
	ldrh	r2, [r2]
	orr	r3, r3, r2, asl #10
	mov	r2, r1
	str	r3, [r5, #216]
	bl	FlashReadPages
	ldr	r3, .L2685+12
	ldrb	r3, [r3]	@ zero_extendqisi2
	cmp	r3, #0
	beq	.L2646
	ldr	r3, [fp, #224]
	ldr	r3, [r3, #12]
	cmp	r3, #0
	beq	.L2646
	ldr	r2, .L2685+16
	ldr	r0, [fp, #220]
	str	r3, [sp, #4]
	ldrh	r1, [r2]
	bl	js_hash
	ldr	r3, [sp, #4]
	cmp	r3, r0
	mvnne	r3, #0
	strne	r3, [fp, #212]
.L2646:
	ldr	r3, [fp, #212]
	cmn	r3, #1
	beq	.L2647
	ldrh	r3, [r7, #8]
	cmp	r3, r10
	bcs	.L2647
	ldrh	r2, [r4, #4]
	ldrh	r1, [r7]
	cmp	r1, r2
	ldreq	r2, [fp, #216]
	streq	r2, [r8, r3, asl #2]
.L2647:
	add	r6, r6, #1
	b	.L2645
.L2661:
	mov	r0, r4
	bl	ftl_free_no_use_map_blk
	ldr	r1, .L2685+12
	movw	r3, #1874
	ldrh	r2, [r4, #2]
	ldrh	r3, [r1, r3]
	cmp	r2, r3
	bne	.L2650
	mov	r0, r4
	bl	ftl_map_blk_alloc_new_blk
	b	.L2650
.L2644:
	ldr	r3, [r5, #-2064]
	ldr	r0, .L2685+8
	str	r3, [r5, #220]
	add	r3, r9, r1
	ldrh	r2, [r9, r1]
	mov	r1, #1
	str	r3, [sp, #8]
	ldrh	r3, [fp]
	sub	r3, r3, #1
	orr	r3, r3, r2, asl #10
	mov	r2, r1
	str	r3, [r5, #216]
	bl	FlashReadPages
	ldr	r3, [r5, #212]
	cmn	r3, #1
	beq	.L2663
	ldrh	r2, [r7]
	ldrh	r3, [r4, #4]
	cmp	r2, r3
	bne	.L2663
	ldrh	r2, [r7, #8]
	movw	r3, #64245
	cmp	r2, r3
	bne	.L2663
	mov	r1, #0
	mov	ip, #8
	mov	lr, #4
.L2652:
	ldr	r0, .L2685+4
	uxth	r3, r1
	sxth	r2, r3
	ldrh	r0, [r0]
	sub	r0, r0, #1
	cmp	r2, r0
	bge	.L2655
	ldr	r0, [r5, #-2064]
	add	r1, r1, #1
	ldr	r2, [r0, r2, asl #3]
	uxth	r2, r2
	cmp	r2, r10
	smlabbcc	r3, r3, ip, lr
	ldrcc	r3, [r0, r3]
	strcc	r3, [r8, r2, asl #2]
	b	.L2652
.L2663:
	ldr	ip, .L2685
	mov	r3, #0
.L2683:
	ldrh	r1, [fp]
	sxth	r2, r3
	cmp	r2, r1
	bge	.L2655
	str	r3, [sp, #16]
	ldr	r3, [sp, #8]
	ldr	r0, .L2685+8
	str	ip, [sp, #12]
	ldrh	r1, [r3]
	orr	r2, r2, r1, asl #10
	mov	r1, #1
	str	r2, [ip, #216]
	mov	r2, r1
	bl	FlashReadPages
	ldr	r3, .L2685+12
	ldrb	r2, [r3]	@ zero_extendqisi2
	cmp	r2, #0
	ldr	ip, [sp, #12]
	ldr	r3, [sp, #16]
	beq	.L2656
	ldr	r2, [ip, #224]
	ldr	r2, [r2, #12]
	cmp	r2, #0
	beq	.L2656
	ldr	r1, .L2685+16
	ldr	r0, [ip, #220]
	str	r2, [sp, #20]
	ldrh	r1, [r1]
	bl	js_hash
	ldr	r2, [sp, #20]
	ldr	ip, [sp, #12]
	ldr	r3, [sp, #16]
	cmp	r2, r0
	mvnne	r2, #0
	strne	r2, [ip, #212]
.L2656:
	ldr	r2, .L2685
	ldr	r2, [r2, #212]
	cmn	r2, #1
	beq	.L2657
	ldrh	r2, [r7, #8]
	cmp	r2, r10
	bcs	.L2657
	ldrh	r1, [r4, #4]
	ldrh	r0, [r7]
	cmp	r0, r1
	ldreq	r1, [ip, #216]
	streq	r1, [r8, r2, asl #2]
.L2657:
	add	r3, r3, #1
	b	.L2683
.L2655:
	add	r6, r6, #1
	b	.L2643
.L2650:
	mov	r0, r4
	bl	ftl_map_blk_gc
	mov	r0, r4
	bl	ftl_map_blk_gc
	mov	r0, #0
	add	sp, sp, #28
	@ sp needed
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L2686:
	.align	2
.L2685:
	.word	.LANCHOR2
	.word	.LANCHOR0+1874
	.word	.LANCHOR2+212
	.word	.LANCHOR0
	.word	.LANCHOR0+1882
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
	movw	r2, #1892
	ldr	r3, .L2689
	ldr	r0, .L2689+4
	ldrh	r1, [r3, r2]
	movw	r2, #262
	strh	r1, [r0, r2]	@ movhi
	add	r2, r0, #256
	ldr	r1, .L2689+8
	strh	r1, [r2]	@ movhi
	movw	r1, #1918
	ldrh	r1, [r3, r1]
	add	r2, r0, #260
	strh	r1, [r2]	@ movhi
	movw	r2, #1894
	ldrh	r1, [r3, r2]
	movw	r2, #258
	ldr	r3, [r3, #1920]
	strh	r1, [r0, r2]	@ movhi
	add	r0, r0, #252
	str	r3, [r0, #12]
	ldr	r3, [r0, #-2252]
	str	r3, [r0, #16]
	ldr	r3, [r0, #-2256]
	str	r3, [r0, #20]
	ldr	r3, [r0, #-2248]
	str	r3, [r0, #24]
	bl	FtlMapTblRecovery
	mov	r0, #0
	ldmfd	sp!, {r3, pc}
.L2690:
	.align	2
.L2689:
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
	ldr	r0, .L2693
	bl	FtlMapTblRecovery
	mov	r0, #0
	ldmfd	sp!, {r3, pc}
.L2694:
	.align	2
.L2693:
	.word	.LANCHOR2-1972
	.fnend
	.size	FtlLoadMapInfo, .-FtlLoadMapInfo
	.align	2
	.global	FtlVendorPartWrite
	.type	FtlVendorPartWrite, %function
FtlVendorPartWrite:
	.fnstart
	@ args = 0, pretend = 0, frame = 64
	@ frame_needed = 0, uses_anonymous_args = 0
	ldr	r3, .L2707
	stmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.save {r4, r5, r6, r7, r8, r9, r10, fp, lr}
	mov	r10, r2
	ldr	r2, [r3]
	.pad #68
	sub	sp, sp, #68
	mov	r4, r1
	movw	r1, #1880
	mov	r6, r0
	str	r3, [sp, #12]
	str	r2, [sp, #60]
	ldr	r2, .L2707+4
	ldrh	r5, [r2, r1]
	add	r1, r0, r4
	movw	r0, #1868
	ldrh	r2, [r2, r0]
	cmp	r1, r2
	mvnhi	r0, #0
	bhi	.L2696
	ldr	fp, .L2707+8
	mov	r5, r6, lsr r5
	mov	r3, r5, asl #2
	str	r3, [sp, #8]
	mov	r7, fp
	mov	r3, #0
	str	r3, [sp, #4]
.L2697:
	cmp	r4, #0
	beq	.L2706
	ldr	r2, [sp, #8]
	mov	r0, r6
	ldr	r3, [fp, #-1996]
	ldr	ip, [r3, r2]
	ldr	r3, .L2707+12
	str	ip, [sp, #20]
	ldrh	r2, [r3]
	mov	r1, r2
	str	r2, [sp, #16]
	bl	__aeabi_uidivmod
	ldr	r2, [sp, #16]
	mov	r8, r1
	ldr	ip, [sp, #20]
	rsb	r3, r1, r2
	uxth	r9, r3
	cmp	r9, r4
	uxthhi	r9, r4
	cmp	r9, r2
	cmpne	ip, #0
	movne	r1, #1
	moveq	r1, #0
	beq	.L2699
	ldr	r2, [r7, #-2056]
	mov	r1, #1
	add	r0, sp, #24
	str	ip, [sp, #28]
	str	r2, [sp, #32]
	mov	r2, #0
	str	r2, [sp, #36]
	mov	r2, r1
	bl	FlashReadPages
	b	.L2700
.L2699:
	ldr	r2, .L2707+16
	ldr	r0, [r7, #-2056]
	ldrh	r2, [r2]
	bl	ftl_memset
.L2700:
	mov	ip, r9, asl #9
	ldr	r0, [r7, #-2056]
	uxth	r8, r8
	mov	r1, r10
	mov	r2, ip
	str	ip, [sp, #16]
	add	r0, r0, r8, asl #9
	rsb	r4, r9, r4
	bl	ftl_memcpy
	mov	r1, r5
	ldr	r0, .L2707+20
	add	r5, r5, #1
	ldr	r2, [r7, #-2056]
	add	r6, r6, r9
	bl	FtlMapWritePage
	ldr	r3, [sp, #4]
	cmn	r0, #1
	ldr	ip, [sp, #16]
	mvneq	r3, #0
	str	r3, [sp, #4]
	add	r10, r10, ip
	ldr	r3, [sp, #8]
	add	r3, r3, #4
	str	r3, [sp, #8]
	b	.L2697
.L2706:
	ldr	r0, [sp, #4]
.L2696:
	ldr	r3, [sp, #12]
	ldr	r2, [sp, #60]
	ldr	r3, [r3]
	cmp	r2, r3
	beq	.L2703
	bl	__stack_chk_fail
.L2703:
	add	sp, sp, #68
	@ sp needed
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L2708:
	.align	2
.L2707:
	.word	__stack_chk_guard
	.word	.LANCHOR0
	.word	.LANCHOR2
	.word	.LANCHOR0+1878
	.word	.LANCHOR0+1882
	.word	.LANCHOR2+252
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
	ldr	r3, .L2711
	ldr	r2, .L2711+4
	ldr	r1, [r3, #-3748]
	cmp	r1, r2
	bxne	lr
	ldr	r2, .L2711+8
	mov	r0, #0
	mov	r1, #1
	str	r2, [r3, #-3744]
	ldr	r2, [r3, #-3856]
	str	r2, [r3, #-3660]
	ldr	r2, [r3, #-3852]
	str	r2, [r3, #-3656]
	ldr	r2, [r3, #-3860]
	str	r2, [r3, #-3740]
	ldr	r2, [r3, #-3872]
	str	r2, [r3, #-3736]
	ldr	r2, [r3, #-3880]
	str	r2, [r3, #-3732]
	ldr	r2, [r3, #-3864]
	str	r2, [r3, #-3728]
	ldr	r2, [r3, #-3836]
	str	r2, [r3, #-3720]
	ldr	r2, .L2711+12
	ldr	r2, [r2, #4064]
	str	r2, [r3, #-3716]
	ldr	r2, [r3, #-3876]
	str	r2, [r3, #-3712]
	ldr	r2, [r3, #-3868]
	str	r2, [r3, #-3708]
	ldr	r2, [r3, #-3828]
	str	r2, [r3, #-3704]
	ldr	r2, [r3, #-3824]
	str	r2, [r3, #-3700]
	ldr	r2, [r3, #-3752]
	str	r2, [r3, #-3688]
	ldr	r2, [r3, #-2104]
	str	r2, [r3, #-3684]
	ldr	r2, .L2711+16
	b	FtlVendorPartWrite
.L2712:
	.align	2
.L2711:
	.word	.LANCHOR2
	.word	1179929683
	.word	1342177366
	.word	.LANCHOR0
	.word	.LANCHOR2-3748
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
	ldr	r3, .L2721
	ldr	r2, .L2721+4
	ldr	r3, [r3, #1796]
	cmp	r3, #0
	moveq	r3, #32
	beq	.L2714
	ldr	r3, [r2, #-3824]
	cmp	r3, #39
	movhi	r3, #32
	movls	r3, #4
.L2714:
	ldr	ip, .L2721+8
	ldrh	r1, [ip]
	cmp	r1, #31
	addls	r1, r1, #1
	strlsh	r1, [ip]	@ movhi
	movls	r3, #1
	cmp	r0, #0
	bne	.L2716
	ldr	r1, [r2, #-2020]
	ldr	r0, [r1, #20]
	ldr	r1, [r1, #16]
	add	r3, r3, r0
	cmp	r1, r3
	bcc	.L2717
.L2716:
	ldr	r3, [r2, #-2020]
	mov	r0, #64
	ldr	r1, [r3, #16]
	str	r1, [r3, #20]
	ldr	r1, .L2721+12
	str	r1, [r3]
	ldr	r3, .L2721+16
	ldr	r2, [r2, #-2020]
	ldrh	r1, [r3, #-12]
	mov	r3, r1, asl #9
	str	r3, [r2, #12]
	ldr	r3, [r2, #8]
	add	r3, r3, #1
	str	r3, [r2, #8]
	mov	r3, #0
	str	r3, [r2, #4]
	bl	FtlVendorPartWrite
	bl	Ftl_save_ext_data
.L2717:
	mov	r0, #0
	ldmfd	sp!, {r3, pc}
.L2722:
	.align	2
.L2721:
	.word	.LANCHOR0
	.word	.LANCHOR2
	.word	.LANCHOR2+296
	.word	1112818501
	.word	.LANCHOR2-2016
	.fnend
	.size	FtlEctTblFlush, .-FtlEctTblFlush
	.align	2
	.global	FtlVendorPartRead
	.type	FtlVendorPartRead, %function
FtlVendorPartRead:
	.fnstart
	@ args = 0, pretend = 0, frame = 56
	@ frame_needed = 0, uses_anonymous_args = 0
	ldr	r3, .L2736
	stmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.save {r4, r5, r6, r7, r8, r9, r10, fp, lr}
	mov	r7, r2
	ldr	r2, [r3]
	.pad #60
	sub	sp, sp, #60
	mov	r5, r1
	movw	r1, #1880
	mov	r6, r0
	str	r3, [sp, #12]
	str	r2, [sp, #52]
	ldr	r2, .L2736+4
	ldrh	r4, [r2, r1]
	add	r1, r0, r5
	movw	r0, #1868
	ldrh	r2, [r2, r0]
	cmp	r1, r2
	mvnhi	r0, #0
	bhi	.L2724
	ldr	r10, .L2736+8
	mov	r4, r6, lsr r4
	mov	r3, r4, asl #2
	str	r3, [sp, #4]
	mov	fp, r10
	mov	r3, #0
	str	r3, [sp]
.L2725:
	cmp	r5, #0
	beq	.L2735
	ldr	r3, [r10, #-1996]
	mov	r0, r6
	ldr	r2, [sp, #4]
	ldr	r8, [r3, r2]
	ldr	r3, .L2736+12
	ldrh	r9, [r3]
	mov	r1, r9
	bl	__aeabi_uidivmod
	rsb	r3, r1, r9
	str	r1, [sp, #8]
	uxth	r9, r3
	cmp	r9, r5
	uxthhi	r9, r5
	cmp	r8, #0
	beq	.L2727
	ldr	r2, [fp, #-2056]
	mov	r1, #1
	add	r0, sp, #16
	str	r8, [sp, #20]
	str	r2, [sp, #24]
	mov	r2, #0
	str	r2, [sp, #28]
	mov	r2, r1
	bl	FlashReadPages
	ldr	r2, [sp, #16]
	ldr	r3, [sp]
	cmn	r2, #1
	ldr	r2, [fp, #212]
	mvneq	r3, #0
	cmp	r2, #256
	str	r3, [sp]
	bne	.L2729
	mov	r1, r4
	mov	r2, r8
	ldr	r0, .L2736+16
	bl	printk
	ldr	r0, .L2736+20
	mov	r1, r4
	ldr	r2, [r10, #-2056]
	bl	FtlMapWritePage
.L2729:
	ldrh	r8, [sp, #8]
	mov	r0, r7
	ldr	r1, [fp, #-2056]
	mov	r2, r9, asl #9
	add	r1, r1, r8, asl #9
	bl	ftl_memcpy
	b	.L2730
.L2727:
	mov	r0, r7
	mov	r1, r8
	mov	r2, r9, asl #9
	bl	ftl_memset
.L2730:
	ldr	r3, [sp, #4]
	add	r4, r4, #1
	rsb	r5, r9, r5
	add	r6, r6, r9
	add	r3, r3, #4
	add	r7, r7, r9, asl #9
	str	r3, [sp, #4]
	b	.L2725
.L2735:
	ldr	r0, [sp]
.L2724:
	ldr	r3, [sp, #12]
	ldr	r2, [sp, #52]
	ldr	r3, [r3]
	cmp	r2, r3
	beq	.L2732
	bl	__stack_chk_fail
.L2732:
	add	sp, sp, #60
	@ sp needed
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L2737:
	.align	2
.L2736:
	.word	__stack_chk_guard
	.word	.LANCHOR0
	.word	.LANCHOR2
	.word	.LANCHOR0+1878
	.word	.LC132
	.word	.LANCHOR2+252
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
	ldr	r4, .L2741
	sub	r5, r4, #2016
	ldr	r2, [r4, #-2020]
	ldrh	r1, [r5, #-12]
	bl	FtlVendorPartRead
	ldr	r3, [r4, #-2020]
	ldr	r2, [r3]
	ldr	r3, .L2741+4
	cmp	r2, r3
	beq	.L2739
	ldr	r1, .L2741+8
	ldr	r0, .L2741+12
	bl	printk
	ldrh	r2, [r5, #-12]
	ldr	r0, [r4, #-2020]
	mov	r1, #0
	mov	r2, r2, asl #9
	bl	ftl_memset
.L2739:
	mov	r0, #0
	ldmfd	sp!, {r3, r4, r5, pc}
.L2742:
	.align	2
.L2741:
	.word	.LANCHOR2
	.word	1112818501
	.word	.LC133
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
	ldr	r4, .L2748
	mov	r1, #1
	ldr	r2, .L2748+4
	bl	FtlVendorPartRead
	ldr	r5, .L2748+8
	ldr	r3, [r4, #-3748]
	cmp	r3, r5
	beq	.L2744
	ldr	r0, .L2748+4
	mov	r1, #0
	mov	r2, #512
	bl	ftl_memset
	str	r5, [r4, #-3748]
.L2744:
	ldr	r2, [r4, #-3748]
	ldr	r3, .L2748
	cmp	r2, r5
	ldr	r5, .L2748+12
	bne	.L2745
	ldr	r2, [r3, #-3660]
	str	r2, [r3, #-3856]
	ldr	r2, [r3, #-3656]
	str	r2, [r3, #-3852]
	ldr	r2, [r3, #-3740]
	str	r2, [r3, #-3860]
	ldr	r2, [r3, #-3736]
	str	r2, [r3, #-3872]
	ldr	r2, [r3, #-3732]
	str	r2, [r3, #-3880]
	ldr	r2, [r3, #-3728]
	str	r2, [r3, #-3864]
	ldr	r2, [r3, #-3720]
	str	r2, [r3, #-3836]
	ldr	r2, [r3, #-3716]
	str	r2, [r5, #4064]
	ldr	r2, [r3, #-3712]
	str	r2, [r3, #-3876]
	ldr	r2, [r3, #-3708]
	str	r2, [r3, #-3868]
	ldr	r2, [r3, #-3704]
	str	r2, [r3, #-3828]
	ldr	r2, [r3, #-3700]
	str	r2, [r3, #-3824]
	ldr	r2, [r3, #-3688]
	str	r2, [r3, #-3752]
.L2745:
	mov	r3, #0
	ldr	r2, [r4, #-3680]
	str	r3, [r4, #-2104]
	ldr	r3, .L2748+16
	cmp	r2, r3
	bne	.L2746
	ldr	r0, .L2748+20
	mov	r3, #1
	ldr	r1, .L2748+24
	str	r3, [r5, #1796]
	bl	printk
.L2746:
	movw	r3, #1864
	ldr	r0, [r4, #-3840]
	ldrh	r2, [r5, r3]
	ldr	r3, [r4, #-3836]
	mla	r0, r0, r2, r3
	movw	r3, #1812
	ldrh	r1, [r5, r3]
	bl	__aeabi_uidiv
	str	r0, [r4, #-3832]
	ldmfd	sp!, {r3, r4, r5, pc}
.L2749:
	.align	2
.L2748:
	.word	.LANCHOR2
	.word	.LANCHOR2-3748
	.word	1179929683
	.word	.LANCHOR0
	.word	305432421
	.word	.LC76
	.word	.LC134
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
	ldr	r3, [r0, #36]
	ldrh	r5, [r0, #6]
	cmp	r3, #0
	ldr	r1, [r0, #24]
	ldmeqfd	sp!, {r4, r5, r6, pc}
	ldr	r2, .L2757
	mov	r3, #0
	str	r3, [r0, #36]
	ldr	r3, [r2, #4052]
	cmp	r3, #0
	ldmnefd	sp!, {r4, r5, r6, pc}
	ldr	r3, .L2757+4
	sub	r5, r5, #1
	mov	r6, r0
	uxth	r5, r5
	ldr	r0, [r3, #-2060]
	mov	r4, r3
	ldr	ip, [r3, #-2040]
	str	r0, [r3, #220]
	str	ip, [r3, #224]
	ldr	r1, [r1, r5, asl #2]
	cmp	r1, #0
	str	r1, [r3, #216]
	beq	.L2754
	mov	r1, #1
	add	r0, r3, #212
	mov	r2, r1
	bl	FlashReadPages
	b	.L2755
.L2754:
	movw	r3, #1882
	mov	r1, #255
	ldrh	r2, [r2, r3]
	bl	ftl_memset
.L2755:
	mov	r0, r6
	mov	r1, r5
	ldr	r2, [r4, #220]
	ldmfd	sp!, {r4, r5, r6, lr}
	b	FtlMapWritePage
.L2758:
	.align	2
.L2757:
	.word	.LANCHOR0
	.word	.LANCHOR2
	.fnend
	.size	FtlMapBlkWriteDumpData, .-FtlMapBlkWriteDumpData
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
	ldr	r9, .L2775
	mov	r10, r2
	ldr	r8, .L2775+4
	mov	r5, r0
	mov	r7, r1
	ldr	r3, [r9]
	str	r3, [sp, #36]
	ldr	r3, .L2775+8
	ldrh	r2, [r3, #124]
	ldrh	r4, [r3, #126]
	ldr	r3, [r8, #148]
	smulbb	r4, r2, r4
	str	r3, [sp, #8]
	ldr	r3, [r8, #180]
	str	r3, [sp, #12]
	uxth	r0, r4
	sub	r6, r0, #1
	sub	r4, r0, #16
	mul	fp, r0, r1
	uxth	r6, r6
.L2760:
	cmp	r6, r4
	ble	.L2774
	mov	r1, #1
	add	r3, r6, fp
	mov	r0, sp
	mov	r2, r1
	mov	r3, r3, asl #10
	str	r3, [sp, #4]
	bl	FlashReadPages
	ldr	r3, [sp]
	cmn	r3, #1
	beq	.L2761
	ldr	r3, [r8, #180]
	ldrh	r2, [r3]
	movw	r3, #61664
	cmp	r2, r3
	bne	.L2761
	cmp	r5, #0
	moveq	r0, r5
	beq	.L2762
	cmp	r7, #0
	ldreq	ip, .L2775+4
	moveq	lr, #1
	beq	.L2763
.L2765:
	ldr	r1, [r8, #148]
	mov	r2, r10
	mov	r0, r5
	bl	ftl_memcpy
	mov	r2, #4
	ldr	r0, .L2775+12
	mov	r1, r5
	mov	r3, r2
	bl	rknand_print_hex
	mov	r0, #0
	b	.L2762
.L2763:
	ldr	r3, [r8, #168]
	uxth	r4, r7
	add	r7, r7, #1
	cmp	r4, r3
	bcs	.L2765
	ldr	r2, [ip, #148]
	mov	r1, r4, lsr #5
	and	r0, r4, #31
	ldr	r3, [r2, r1, asl #2]
	orr	r0, r3, lr, asl r0
	str	r0, [r2, r1, asl #2]
	b	.L2763
.L2761:
	sub	r6, r6, #1
	uxth	r6, r6
	b	.L2760
.L2774:
	mvn	r0, #0
.L2762:
	ldr	r2, [sp, #36]
	ldr	r3, [r9]
	cmp	r2, r3
	beq	.L2767
	bl	__stack_chk_fail
.L2767:
	add	sp, sp, #44
	@ sp needed
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L2776:
	.align	2
.L2775:
	.word	__stack_chk_guard
	.word	.LANCHOR2
	.word	.LANCHOR0
	.word	.LC135
	.fnend
	.size	FlashReadFacBbtData, .-FlashReadFacBbtData
	.align	2
	.global	FlashGetBadBlockList
	.type	FlashGetBadBlockList, %function
FlashGetBadBlockList:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	ldr	r3, .L2788
	stmfd	sp!, {r4, r5, r6, r7, r8, lr}
	.save {r4, r5, r6, r7, r8, lr}
	mov	r5, r0
	ldr	r3, [r3, #692]
	ldr	r6, .L2788+4
	ldrb	r4, [r3, #13]	@ zero_extendqisi2
	ldrh	r3, [r3, #14]
	ldr	r0, [r6, #176]
	smulbb	r4, r4, r3
	uxth	r4, r4
	add	r2, r4, #7
	mov	r2, r2, asr #3
	bl	FlashReadFacBbtData
	cmn	r0, #1
	bne	.L2778
.L2782:
	mov	r3, #0
	b	.L2779
.L2778:
	mov	lr, r4, lsr #4
	mov	ip, #0
	sub	r4, r4, #1
	mov	r3, ip
	mov	r7, #1
.L2780:
	uxth	r0, ip
	cmp	r0, r4
	bge	.L2779
	ldr	r8, [r6, #176]
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
	bcc	.L2780
	b	.L2782
.L2779:
	mov	r3, r3, asl #1
	mvn	r2, #0
	mov	r0, #0
	strh	r2, [r5, r3]	@ movhi
	ldmfd	sp!, {r4, r5, r6, r7, r8, pc}
.L2789:
	.align	2
.L2788:
	.word	.LANCHOR0
	.word	.LANCHOR2
	.fnend
	.size	FlashGetBadBlockList, .-FlashGetBadBlockList
	.align	2
	.global	FtlMakeBbt
	.type	FtlMakeBbt, %function
FtlMakeBbt:
	.fnstart
	@ args = 0, pretend = 0, frame = 16
	@ frame_needed = 0, uses_anonymous_args = 0
	stmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.save {r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.pad #20
	sub	sp, sp, #20
	ldr	r6, .L2814
	ldr	r5, [r6, #4052]
	cmp	r5, #0
	bne	.L2791
	bl	FtlBbtMemInit
	ldr	r8, .L2814+4
	bl	FtlLoadFactoryBbt
	ldr	r7, .L2814+8
	sub	r9, r8, #18
	add	r4, r6, #1936
	mov	r10, r7
.L2792:
	ldr	r3, .L2814+12
	ldrh	r3, [r3]
	cmp	r5, r3
	bcs	.L2811
	ldrh	r2, [r9, #2]!
	movw	r3, #65535
	ldr	r0, [r7, #-2064]
	ldr	fp, [r7, #-2040]
	cmp	r2, r3
	ldr	r3, .L2814+16
	str	r0, [r7, #220]
	str	fp, [r7, #224]
	beq	.L2793
	ldrh	ip, [r3]
	mov	r1, #1
	ldr	r0, .L2814+20
	str	r3, [sp, #8]
	mla	ip, ip, r5, r2
	mov	r2, ip, asl #10
	str	r2, [r7, #216]
	mov	r2, r1
	str	ip, [sp, #4]
	bl	FlashReadPages
	ldr	r0, [r8]
	ldr	r1, [r7, #220]
	ldr	r3, [sp, #8]
	ldrh	r2, [r3]
	add	r2, r2, #7
	mov	r2, r2, asr #3
	bl	ftl_memcpy
	ldr	ip, [sp, #4]
	b	.L2794
.L2793:
	mov	r1, r5
	str	r3, [sp, #4]
	bl	FlashGetBadBlockList
	ldr	r0, [r7, #220]
	ldr	r1, [r8]
	bl	FtlBbt2Bitmap
	ldr	r3, [sp, #4]
	ldrh	r2, [r3]
	sub	r2, r2, #1
	uxth	r2, r2
	stmib	sp, {r2, r3}
.L2795:
	ldr	r3, [sp, #8]
	ldrh	r0, [r3]
	ldr	r3, [sp, #4]
	smlabb	r0, r0, r5, r3
	uxth	r0, r0
	bl	FtlBbmIsBadBlock
	cmp	r0, #1
	ldreq	r3, [sp, #4]
	subeq	r3, r3, #1
	uxtheq	r3, r3
	streq	r3, [sp, #4]
	beq	.L2795
.L2812:
	ldrh	r3, [sp, #4]
	mov	r1, #0
	mov	r2, #16
	ldr	r0, [r10, #-2040]
	strh	r3, [r9]	@ movhi
	bl	ftl_memset
	ldr	r3, .L2814+24
	strh	r3, [fp]	@ movhi
	mov	r3, #0
	str	r3, [fp, #4]
	ldrh	r3, [r9]
	strh	r3, [fp, #2]	@ movhi
	ldrh	r3, [r9]
	ldr	r1, [r8]
	ldr	r0, [r10, #220]
	ldr	r2, [sp, #8]
	ldrh	ip, [r2]
	mla	ip, ip, r5, r3
	mov	r3, ip, asl #10
	str	r3, [r10, #216]
	ldr	r3, .L2814+28
	str	ip, [sp, #12]
	ldrh	r2, [r3]
	mov	r2, r2, asl #2
	bl	ftl_memcpy
	mov	r1, #1
	mov	r2, r1
	ldr	r0, .L2814+20
	bl	FlashEraseBlocks
	mov	r1, #1
	mov	r3, r1
	ldr	r0, .L2814+20
	mov	r2, r1
	bl	FlashProgPages
	ldr	r3, [r10, #212]
	cmn	r3, #1
	ldr	ip, [sp, #12]
	bne	.L2794
	uxth	r0, ip
	bl	FtlBbmMapBadBlock
	b	.L2795
.L2794:
	uxth	r0, ip
	add	r5, r5, #1
	bl	FtlBbmMapBadBlock
	add	r8, r8, #4
	b	.L2792
.L2811:
	ldr	r7, .L2814+32
	mov	r5, #0
.L2799:
	ldrh	r3, [r7]
	uxth	r0, r5
	add	r5, r5, #1
	cmp	r3, r0
	bls	.L2813
	bl	FtlBbmMapBadBlock
	b	.L2799
.L2813:
	ldrh	r5, [r4, #12]
	movw	r8, #65535
	ldr	r7, .L2814+36
	sub	r5, r5, #1
	uxth	r5, r5
.L2801:
	ldrh	r3, [r4, #12]
	sub	r3, r3, #48
	cmp	r5, r3
	ble	.L2805
	mov	r0, r5
	bl	FtlBbmIsBadBlock
	cmp	r0, #1
	beq	.L2802
	mov	r0, r5
	bl	FlashTestBlk
	cmp	r0, #0
	beq	.L2803
	mov	r0, r5
	bl	FtlBbmMapBadBlock
	b	.L2802
.L2803:
	ldrh	r3, [r7]
	cmp	r3, r8
	streqh	r5, [r7]	@ movhi
.L2804:
	ldrne	r3, .L2814+36
	strneh	r5, [r3, #4]	@ movhi
	bne	.L2805
.L2802:
	sub	r5, r5, #1
	uxth	r5, r5
	b	.L2801
.L2805:
	ldr	r0, [r6, #4056]
	mov	r5, #0
	ldrh	r3, [r4]
	mov	r2, #2
	str	r5, [r6, #1944]
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
	ldr	r3, [r6, #1944]
	ldrh	r2, [r4, #4]
	add	r3, r3, #1
	str	r3, [r6, #1944]
	ldrh	r3, [r4]
	strh	r5, [r4, #2]	@ movhi
	strh	r2, [r4]	@ movhi
	strh	r3, [r4, #4]	@ movhi
	bl	FtlBbmTblFlush
.L2791:
	mov	r0, #0
	add	sp, sp, #20
	@ sp needed
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L2815:
	.align	2
.L2814:
	.word	.LANCHOR0
	.word	.LANCHOR0+1964
	.word	.LANCHOR2
	.word	.LANCHOR0+1826
	.word	.LANCHOR0+1870
	.word	.LANCHOR2+212
	.word	-3872
	.word	.LANCHOR2-1976
	.word	.LANCHOR0+1886
	.word	.LANCHOR0+1936
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
	movw	r3, #1880
	ldr	r7, .L2833
	ldrh	r10, [r7, r3]
	ldr	r3, [r7, #1932]
	cmp	r0, r3
	bcs	.L2817
	add	r10, r10, #7
	ldr	fp, .L2833+4
	mov	r5, r0, lsr r10
	movw	r3, #1910
	str	r2, [sp, #4]
	mov	r9, r1
	mov	r6, r7
	str	r0, [sp]
	uxth	r5, r5
	ldrh	r2, [r7, r3]
	ldr	r8, [fp, #-3896]
	mov	r4, #0
	mov	r1, #12
	b	.L2818
.L2817:
	cmp	r2, #0
	mvn	r0, #0
	streq	r0, [r1]
	b	.L2819
.L2823:
	add	r4, r4, #1
	mla	r0, r1, r4, r8
	ldrh	r0, [r0, #-12]
	cmp	r0, r5
	beq	.L2820
.L2818:
	uxth	r7, r4
	cmp	r7, r2
	bcc	.L2823
	bl	select_l2p_ram_region
	mov	r4, #12
	movw	r3, #65535
	mul	r4, r4, r0
	mov	r7, r0
	add	r2, r8, r4
	ldrh	r1, [r8, r4]
	cmp	r1, r3
	bne	.L2832
.L2824:
	ldr	r3, [fp, #-1992]
	ldr	r8, .L2833+4
	ldr	fp, [r3, r5, asl #2]
	cmp	fp, #0
	bne	.L2825
	ldr	r3, [r8, #-3896]
	mov	r1, #255
	add	r3, r3, r4
	ldr	r0, [r3, #8]
	movw	r3, #1882
	ldrh	r2, [r6, r3]
	bl	ftl_memset
	ldr	r3, [r8, #-3896]
	strh	r5, [r3, r4]	@ movhi
	ldr	r3, [r8, #-3896]
	add	r4, r3, r4
	str	fp, [r4, #4]
.L2820:
	ldr	r2, [sp]
	mvn	r3, #0
	mov	r6, #12
	bic	r10, r2, r3, asl r10
	ldr	r3, [sp, #4]
	ldr	r2, .L2833+4
	cmp	r3, #0
	uxth	r10, r10
	bne	.L2821
	ldr	r3, [r2, #-3896]
	mla	r6, r6, r7, r3
	ldr	r3, [r6, #8]
	ldr	r3, [r3, r10, asl #2]
	str	r3, [r9]
	b	.L2822
.L2821:
	mul	r6, r6, r7
	ldr	r3, [r2, #-3896]
	ldr	r1, [r9]
	add	r3, r3, r6
	ldr	r3, [r3, #8]
	str	r1, [r3, r10, asl #2]
	ldr	r3, [r2, #-3896]
	add	r6, r3, r6
	ldr	r3, [r6, #4]
	orr	r3, r3, #-2147483648
	str	r3, [r6, #4]
	sub	r3, r2, #3888
	strh	r5, [r3, #-4]	@ movhi
.L2822:
	ldr	r2, [r2, #-3896]
	mov	r3, #12
	mov	r0, #0
	mla	r7, r3, r7, r2
	ldr	r3, [r7, #4]
	cmn	r3, #1
	addne	r3, r3, #1
	strne	r3, [r7, #4]
	b	.L2819
.L2832:
	ldr	r3, [r2, #4]
	cmp	r3, #0
	bge	.L2824
	bl	flush_l2p_region
	b	.L2824
.L2825:
	ldr	r3, [r8, #-3896]
	mov	r1, #1
	add	r0, r8, #212
	mov	r2, r1
	add	r3, r3, r4
	str	fp, [r8, #216]
	ldr	r3, [r3, #8]
	str	r3, [r8, #220]
	ldr	r3, [r8, #-2040]
	str	r3, [r8, #224]
	bl	FlashReadPages
	ldr	r3, [r8, #224]
	ldrh	r3, [r3, #8]
	cmp	r3, r5
	beq	.L2826
	mov	r1, r5
	mov	r2, fp
	ldr	r0, .L2833+8
	bl	printk
	mov	r2, #4
	mov	r3, r2
	ldr	r0, .L2833+12
	ldr	r1, [r8, #224]
	bl	rknand_print_hex
	movw	r3, #1908
	ldrh	r3, [r6, r3]
	mov	r2, #4
	ldr	r0, .L2833+16
	ldr	r1, [r8, #-1992]
	bl	rknand_print_hex
	mov	r3, #1
	str	r3, [r6, #4052]
	b	.L2827
.L2826:
	ldr	r3, [r8, #212]
	cmp	r3, #256
	bne	.L2827
	mov	r1, r5
	mov	r2, fp
	ldr	r0, .L2833+20
	bl	printk
	ldr	r3, [r8, #-3896]
	ldr	r0, .L2833+24
	mov	r1, r5
	add	r3, r3, r4
	ldr	r2, [r3, #8]
	bl	FtlMapWritePage
.L2827:
	ldr	r3, .L2833+4
	mov	r1, #0
	ldr	r3, [r3, #-3896]
	add	r2, r3, r4
	str	r1, [r2, #4]
	strh	r5, [r3, r4]	@ movhi
	b	.L2820
.L2819:
	add	sp, sp, #12
	@ sp needed
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L2834:
	.align	2
.L2833:
	.word	.LANCHOR0
	.word	.LANCHOR2
	.word	.LC136
	.word	.LC100
	.word	.LC137
	.word	.LC138
	.word	.LANCHOR2-1972
	.fnend
	.size	log2phys, .-log2phys
	.align	2
	.global	FtlWriteDumpData
	.type	FtlWriteDumpData, %function
FtlWriteDumpData:
	.fnstart
	@ args = 0, pretend = 0, frame = 56
	@ frame_needed = 0, uses_anonymous_args = 0
	ldr	r3, .L2855
	stmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.save {r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.pad #60
	sub	sp, sp, #60
	ldr	r2, [r3]
	ldr	r8, .L2855+4
	str	r3, [sp]
	str	r2, [sp, #52]
	ldr	r2, [r8, #4052]
	cmp	r2, #0
	bne	.L2835
	ldr	r7, .L2855+8
	ldr	r4, .L2855+12
	ldrh	r5, [r7, #4]
	cmp	r5, #0
	beq	.L2837
	ldrb	r3, [r4, #-4032]	@ zero_extendqisi2
	cmp	r3, #0
	bne	.L2837
	add	r3, r8, #1872
	ldrb	r2, [r4, #-4033]	@ zero_extendqisi2
	ldrh	r3, [r3]
	mul	r3, r3, r2
	cmp	r5, r3
	beq	.L2837
	ldrb	r10, [r4, #-4030]	@ zero_extendqisi2
	movw	r3, #1804
	ldr	r6, [r8, #1932]
	cmp	r10, #0
	ldrh	r9, [r8, r3]
	bne	.L2835
	sub	r6, r6, #1
	add	r1, sp, #12
	mov	r2, r10
	mov	r0, r6
	bl	log2phys
	ldr	r5, [r4, #-2040]
	ldr	r0, [r4, #-2064]
	ldr	r3, [sp, #12]
	str	r6, [sp, #32]
	cmn	r3, #1
	str	r0, [sp, #24]
	str	r3, [sp, #20]
	str	r5, [sp, #28]
	str	r10, [r5, #4]
	beq	.L2839
	mov	r1, #1
	mov	r2, r10
	add	r0, sp, #16
	bl	FlashReadPages
	b	.L2840
.L2839:
	movw	r3, #1882
	mov	r1, #255
	ldrh	r2, [r8, r3]
	bl	ftl_memset
.L2840:
	ldr	r3, .L2855+16
	mov	r8, #0
	ldr	r10, .L2855+12
	mov	r9, r9, asl #2
	ldr	ip, .L2855+8
	mov	fp, r8
	strh	r3, [r5]	@ movhi
.L2841:
	cmp	r8, r9
	beq	.L2842
	ldrh	r3, [r7, #4]
	cmp	r3, #0
	beq	.L2842
	ldr	r3, [sp, #20]
	add	r8, r8, #1
	str	r6, [r5, #8]
	ldr	r0, .L2855+8
	str	r3, [r5, #12]
	ldrh	r3, [ip]
	str	ip, [sp, #4]
	strh	r3, [r5, #2]	@ movhi
	bl	get_new_active_ppa
	ldr	r3, [r10, #-3844]
	mov	r2, #0
	mov	r1, #1
	str	r0, [sp, #20]
	add	r0, sp, #16
	str	r3, [r5, #4]
	add	r3, r3, #1
	cmn	r3, #1
	moveq	r3, fp
	str	r3, [r10, #-3844]
	mov	r3, r2
	bl	FlashProgPages
	ldrh	r0, [r7]
	bl	decrement_vpc_count
	ldr	ip, [sp, #4]
	b	.L2841
.L2842:
	mov	r3, #1
	b	.L2854
.L2837:
	mov	r3, #0
.L2854:
	strb	r3, [r4, #-4030]
.L2835:
	ldr	r3, [sp]
	ldr	r2, [sp, #52]
	ldr	r3, [r3]
	cmp	r2, r3
	beq	.L2846
	bl	__stack_chk_fail
.L2846:
	add	sp, sp, #60
	@ sp needed
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L2856:
	.align	2
.L2855:
	.word	__stack_chk_guard
	.word	.LANCHOR0
	.word	.LANCHOR2-4040
	.word	.LANCHOR2
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
	ldr	r5, .L2863
	mov	r7, #12
	ldr	r6, .L2863+4
.L2858:
	ldrh	r3, [r5]
	uxth	r0, r4
	cmp	r3, r0
	bls	.L2862
	ldr	r3, [r6, #-3896]
	mla	r3, r7, r0, r3
	ldr	r3, [r3, #4]
	cmp	r3, #0
	bge	.L2859
	bl	flush_l2p_region
.L2859:
	add	r4, r4, #1
	b	.L2858
.L2862:
	mov	r0, #0
	ldmfd	sp!, {r3, r4, r5, r6, r7, pc}
.L2864:
	.align	2
.L2863:
	.word	.LANCHOR0+1910
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
	ldr	r1, .L2892
	stmfd	sp!, {r3, r4, r5, r6, r7, lr}
	.save {r3, r4, r5, r6, r7, lr}
	ldr	r3, [r1, #4052]
	ldrh	r5, [r0]
	cmp	r3, #0
	bne	.L2866
	movw	r3, #65535
	cmp	r5, r3
	mov	r7, r1
	mov	r4, r0
	ldr	r6, .L2892+4
	beq	.L2867
	ldr	r2, [r6, #-4060]
	mov	r3, r5, asl #1
	mov	r0, r5
	ldrh	r3, [r2, r3]
	cmp	r3, #0
	beq	.L2868
	bl	INSERT_DATA_LIST
	b	.L2867
.L2868:
	bl	INSERT_FREE_LIST
.L2867:
	mov	r3, #0
	strb	r3, [r4, #8]
	ldr	r3, .L2892+8
	cmp	r4, r3
	beq	.L2869
	ldr	r2, .L2892
	add	r3, r2, #1824
	ldrh	r3, [r3]
	cmp	r3, #1
	beq	.L2869
	ldrb	r2, [r2, #140]	@ zero_extendqisi2
	cmp	r2, #0
	beq	.L2870
.L2869:
	mov	r3, #1
	strb	r3, [r4, #8]
	b	.L2871
.L2870:
	ldr	r2, .L2892+12
	cmp	r4, r2
	bne	.L2871
	cmp	r3, #3
	beq	.L2873
	ldr	r3, .L2892+4
	ldr	r3, [r3, #-3752]
	cmp	r3, #1
	bne	.L2874
.L2873:
	mov	r3, #1
	strb	r3, [r6, #-4032]
.L2874:
	ldr	r3, [r7, #1796]
	cmp	r3, #0
	beq	.L2871
	ldr	r3, [r6, #-3824]
	cmp	r3, #39
	ldrls	r3, .L2892+4
	movls	r2, #1
	strlsb	r2, [r3, #-4032]
.L2871:
	ldr	r2, .L2892+4
	movw	r1, #65535
	sub	r3, r2, #1920
	ldrh	r0, [r3]
	mov	r6, r3
	cmp	r0, r1
	beq	.L2876
	cmp	r5, r0
	bne	.L2877
	ldr	r2, [r2, #-4060]
	mov	r3, r0, asl #1
	ldrh	r3, [r2, r3]
	cmp	r3, #0
	beq	.L2878
.L2877:
	bl	update_vpc_list
.L2878:
	mvn	r3, #0
	strh	r3, [r6]	@ movhi
.L2876:
	mov	r0, r4
	bl	allocate_data_superblock
	bl	l2p_flush
	mov	r0, #0
	bl	FtlEctTblFlush
	bl	FtlVpcTblFlush
.L2866:
	mov	r0, #0
	ldmfd	sp!, {r3, r4, r5, r6, r7, pc}
.L2893:
	.align	2
.L2892:
	.word	.LANCHOR0
	.word	.LANCHOR2
	.word	.LANCHOR2-3992
	.word	.LANCHOR2-4040
	.fnend
	.size	allocate_new_data_superblock, .-allocate_new_data_superblock
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
	ldr	r6, .L2916
	ldr	r1, .L2916+4
	ldr	r0, .L2916+8
	ldr	r3, [r6]
	ldr	r7, .L2916+12
	ldr	r5, .L2916+16
	str	r3, [sp, #4]
	bl	printk
	ldr	r0, .L2916+16
	mov	r1, #0
	mov	r2, #8192
	bl	memset
.L2895:
	ldr	r3, [r7, #1932]
	cmp	r4, r3
	bcs	.L2914
	mov	r0, r4
	mov	r1, sp
	mov	r2, #0
	bl	log2phys
	ldr	r0, [sp]
	cmn	r0, #1
	beq	.L2896
	ubfx	r0, r0, #10, #16
	bl	P2V_block_in_plane
	mov	r0, r0, asl #1
	ldrh	r3, [r5, r0]
	add	r3, r3, #1
	strh	r3, [r5, r0]	@ movhi
.L2896:
	add	r4, r4, #1
	b	.L2895
.L2914:
	ldr	r5, .L2916+20
	mov	r4, #0
	ldr	r8, .L2916+24
	add	r9, r5, #300
.L2898:
	ldrh	r3, [r8]
	uxth	r1, r4
	ldr	r7, .L2916+20
	cmp	r3, r1
	bls	.L2915
	ldr	r2, [r5, #-4060]
	mov	r3, r1, asl #1
	ldrh	r2, [r2, r3]
	ldrh	r3, [r9, r3]
	cmp	r2, r3
	beq	.L2899
	ldr	r0, .L2916+28
	bl	printk
.L2899:
	add	r4, r4, #1
	b	.L2898
.L2915:
	ldr	r4, [r7, #-4048]
	cmp	r4, #0
	beq	.L2894
	sub	r3, r7, #4032
	add	r9, r7, #300
	mov	r5, #0
	mov	r10, #6
	ldrh	r8, [r3, #-12]
	ldr	r3, [r7, #-4068]
	rsb	r4, r3, r4
	ldr	r3, .L2916+32
	mov	r4, r4, asr #1
	mul	r4, r3, r4
	uxth	r4, r4
.L2902:
	uxth	r3, r5
	cmp	r3, r8
	bcs	.L2894
	ldr	r2, [r7, #-4060]
	mov	r3, r4, asl #1
	ldrh	r2, [r2, r3]
	cmp	r2, #0
	beq	.L2903
	ldr	r0, .L2916+36
	mov	r1, r4
	ldrh	r3, [r9, r3]
	bl	printk
.L2903:
	mul	r4, r10, r4
	ldr	r3, [r7, #-4068]
	add	r5, r5, #1
	ldrh	r4, [r3, r4]
	movw	r3, #65535
	cmp	r4, r3
	bne	.L2902
.L2894:
	ldr	r2, [sp, #4]
	ldr	r3, [r6]
	cmp	r2, r3
	beq	.L2906
	bl	__stack_chk_fail
.L2906:
	add	sp, sp, #8
	@ sp needed
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, pc}
.L2917:
	.align	2
.L2916:
	.word	__stack_chk_guard
	.word	.LANCHOR3+72
	.word	.LC107
	.word	.LANCHOR0
	.word	.LANCHOR2+300
	.word	.LANCHOR2
	.word	.LANCHOR0+1812
	.word	.LC139
	.word	-1431655765
	.word	.LC140
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
	.pad #32
	sub	sp, sp, #32
	ldr	r6, .L2929
	mov	r1, #0
	ldr	r0, .L2929+4
	mov	r4, #0
	ldr	r8, .L2929+8
	ldr	r3, [r6]
	ldr	r5, .L2929+12
	str	r3, [sp, #28]
	bl	printk
.L2919:
	ldr	r3, [r8, #1932]
	cmp	r4, r3
	bcs	.L2928
	mov	r0, r4
	add	r1, sp, #24
	mov	r2, #0
	bl	log2phys
	ubfx	r3, r4, #0, #11
	cmp	r3, #0
	bne	.L2920
	ldr	r0, .L2929+16
	mov	r1, r4
	ldr	r2, [sp, #24]
	bl	printk
.L2920:
	ldr	r3, [sp, #24]
	cmn	r3, #1
	beq	.L2922
	str	r3, [r5, #216]
	mov	r2, #0
	ldr	r3, [r5, #-2064]
	mov	r1, #1
	ldr	r7, [r5, #-2040]
	ldr	r0, .L2929+20
	str	r3, [r5, #220]
	str	r2, [r5, #212]
	str	r4, [r5, #228]
	str	r7, [r5, #224]
	bl	FlashReadPages
	ldr	r3, [r5, #212]
	ldr	r2, .L2929+12
	cmn	r3, #1
	cmpne	r3, #256
	beq	.L2923
	ldr	r3, [r7, #8]
	cmp	r3, r4
	beq	.L2922
.L2923:
	ldr	r3, [r2, #224]
	ldr	r1, [r2, #220]
	ldr	r0, [r3, #4]
	str	r0, [sp]
	ldr	r0, [r3, #8]
	str	r0, [sp, #4]
	ldr	r0, [r3, #12]
	str	r0, [sp, #8]
	ldr	r0, [r1]
	str	r0, [sp, #12]
	ldr	r1, [r1, #4]
	ldr	r0, .L2929+24
	str	r1, [sp, #16]
	mov	r1, r4
	ldr	r2, [r2, #216]
	ldr	r3, [r3]
	bl	printk
.L2922:
	add	r4, r4, #1
	b	.L2919
.L2928:
	ldr	r2, [sp, #28]
	ldr	r3, [r6]
	cmp	r2, r3
	beq	.L2926
	bl	__stack_chk_fail
.L2926:
	add	sp, sp, #32
	@ sp needed
	ldmfd	sp!, {r4, r5, r6, r7, r8, pc}
.L2930:
	.align	2
.L2929:
	.word	__stack_chk_guard
	.word	.LC141
	.word	.LANCHOR0
	.word	.LANCHOR2
	.word	.LC142
	.word	.LANCHOR2+212
	.word	.LC143
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
	ldr	r6, .L2941
	ldr	r4, [r6, #-4060]
	mov	r7, r0, asl #1
	ldrh	r3, [r4, r7]
	cmp	r3, #0
	addne	r3, r3, #1
	strneh	r3, [r4, r7]	@ movhi
	bne	.L2933
	ldr	r4, [r6, #-4048]
	cmp	r4, #0
	beq	.L2933
	ldr	r1, [r6, #-4068]
	sub	r2, r6, #4032
	ldr	lr, .L2941+4
	movw	r9, #65535
	rsb	r4, r1, r4
	ldrh	ip, [r2, #-12]
	mov	r8, r2
	mov	r4, r4, asr #1
	mul	r4, lr, r4
	mov	lr, #6
	uxth	r4, r4
.L2934:
	uxth	r2, r3
	cmp	r2, ip
	bcs	.L2933
	cmp	r4, r0
	bne	.L2935
	mov	r1, r4
	ldr	r0, .L2941+8
	bl	List_remove_node
	ldrh	r3, [r8, #-12]
	mov	r0, r4
	sub	r3, r3, #1
	strh	r3, [r8, #-12]	@ movhi
	bl	INSERT_DATA_LIST
	ldr	r2, [r6, #-4060]
	ldrh	r3, [r2, r7]
	add	r3, r3, #1
	strh	r3, [r2, r7]	@ movhi
	b	.L2933
.L2935:
	mul	r4, lr, r4
	add	r3, r3, #1
	ldrh	r4, [r1, r4]
	cmp	r4, r9
	bne	.L2934
.L2933:
	mov	r0, r5
	add	r1, sp, #4
	mov	r2, #1
	bl	log2phys
	add	sp, sp, #12
	@ sp needed
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, pc}
.L2942:
	.align	2
.L2941:
	.word	.LANCHOR2
	.word	-1431655765
	.word	.LANCHOR2-4048
	.fnend
	.size	FtlReUsePrevPpa, .-FtlReUsePrevPpa
	.align	2
	.global	FtlRecoverySuperblock
	.type	FtlRecoverySuperblock, %function
FtlRecoverySuperblock:
	.fnstart
	@ args = 0, pretend = 0, frame = 64
	@ frame_needed = 0, uses_anonymous_args = 0
	ldr	r3, .L3099
	movw	r2, #65535
	stmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.save {r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.pad #68
	sub	sp, sp, #68
	ldr	r3, [r3]
	mov	r4, r0
	str	r3, [sp, #60]
	ldrh	r3, [r0]
	cmp	r3, r2
	beq	.L2945
	ldrh	r3, [r0, #2]
	str	r3, [sp, #8]
	ldrb	r3, [r0, #6]	@ zero_extendqisi2
	ldr	r1, [sp, #8]
	str	r3, [sp, #28]
	ldr	r3, .L3099+4
	ldrh	r3, [r3]
	cmp	r3, r1
	mov	r3, #0
	streqh	r3, [r0, #4]	@ movhi
	ldrneh	r0, [r0, #16]
	beq	.L3091
.L2947:
	cmp	r0, r2
	add	r3, r3, #1
	uxtheq	r1, r3
	addeq	r1, r4, r1, asl #1
	ldreqh	r0, [r1, #16]
	beq	.L2947
.L3094:
	ldrb	r1, [r4, #8]	@ zero_extendqisi2
	cmp	r1, #1
	bne	.L2949
	bl	FtlGetLastWrittenPage
	cmn	r0, #1
	mov	r5, r0
	beq	.L2950
	ldr	r3, .L3099+8
	ldrb	r2, [r3, #140]	@ zero_extendqisi2
	cmp	r2, #0
	addeq	r3, r3, r0, asl #1
	ldreqh	r7, [r3, #144]
	beq	.L3020
	b	.L3084
.L2949:
	mov	r1, #0
	bl	FtlGetLastWrittenPage
	cmn	r0, #1
	mov	r5, r0
	beq	.L2950
.L3084:
	mov	r7, r5
.L3020:
	ldr	r3, .L3099+8
	movw	r2, #1804
	movw	r8, #65535
	mov	r9, #36
	ldrh	r2, [r3, r2]
	str	r2, [sp, #4]
	ldr	r2, .L3099+12
	ldr	r1, [r2, #-2076]
	ldr	ip, [r2, #-3208]
	movw	r2, #1884
	ldrh	lr, [r3, r2]
	add	r3, r4, #14
	mov	r2, #0
	str	r1, [sp, #12]
	str	r3, [sp, #20]
	mov	r0, r3
	mov	r6, r2
	mov	r10, r2
	b	.L2952
.L2950:
	mov	r3, #0
	strh	r3, [r4, #2]	@ movhi
.L3091:
	strb	r3, [r4, #6]
	b	.L2945
.L2954:
	ldrh	r3, [r0, #2]!
	cmp	r3, r8
	beq	.L2953
	ldr	r1, [sp, #12]
	orr	r3, r7, r3, asl #10
	mla	r1, r9, r6, r1
	stmib	r1, {r3, r10}
	mul	r3, lr, r6
	add	r6, r6, #1
	uxth	r6, r6
	add	fp, r3, #3
	cmp	r3, #0
	movlt	r3, fp
	bic	r3, r3, #3
	add	r3, ip, r3
	str	r3, [r1, #12]
.L2953:
	add	r2, r2, #1
.L2952:
	ldr	r1, [sp, #4]
	uxth	r3, r2
	cmp	r3, r1
	bcc	.L2954
	ldrb	r3, [r4, #8]	@ zero_extendqisi2
	cmp	r3, #1
	movne	r3, #0
	bne	.L3085
	ldr	r3, .L3099+8
	ldrb	r1, [r3, #140]	@ zero_extendqisi2
	adds	r3, r1, #0
	movne	r3, #1
.L3085:
	ldr	r8, .L3099+12
	mov	r1, r6
	str	r3, [sp, #24]
	mov	fp, #0
	ldr	r2, [sp, #24]
	ldr	r0, [r8, #-2076]
	bl	FlashReadPages
	ldr	r3, [r8, #-3844]
	str	r8, [sp, #12]
	str	r3, [sp, #36]
	movw	r3, #65535
	str	r3, [sp, #16]
.L2956:
	uxth	r9, fp
	cmp	r9, r6
	bcs	.L2963
	mov	r1, #36
	ldr	r0, [r8, #-2076]
	mul	r1, r1, fp
	add	ip, r0, r1
	ldr	r1, [r0, r1]
	cmp	r1, #0
	bne	.L2957
	ldr	ip, [ip, #12]
	ldr	r10, [ip, #4]
	cmn	r10, #1
	beq	.L2958
	mov	r0, r10
	ldr	r1, [r8, #-3844]
	str	ip, [sp, #4]
	bl	ftl_cmp_data_ver
	ldr	ip, [sp, #4]
	cmp	r0, #0
	addne	r10, r10, #1
	strne	r10, [r8, #-3844]
.L2958:
	ldr	r1, [ip]
	cmn	r1, #1
	bne	.L2959
.L2963:
	cmp	r9, r6
	ldr	r6, .L3099+12
	bne	.L3081
	add	r5, r5, #1
	uxth	r3, r5
	str	r3, [sp, #4]
	ldr	r3, [r6, #-2076]
	ldr	r0, [r3, #4]
	b	.L3086
.L2957:
	ldr	r1, [ip, #4]
	ldr	r0, .L3099+16
	bl	printk
	uxth	r3, r7
	ldrh	r1, [r4]
	str	r3, [sp, #16]
	ldr	r3, .L3099+20
	strh	r1, [r3]	@ movhi
.L2959:
	add	fp, fp, #1
	b	.L2956
.L3081:
	uxth	r3, r5
	str	r3, [sp, #4]
	ldr	r3, [sp, #12]
	mov	r2, #36
	ldr	r3, [r3, #-2076]
	mla	r9, r2, r9, r3
	ldr	r0, [r9, #4]
.L3086:
	ubfx	r0, r0, #10, #16
	bl	P2V_plane
	ldrb	r3, [r4, #8]	@ zero_extendqisi2
	cmp	r3, #1
	str	r0, [sp, #12]
	bne	.L2965
	ldr	r2, .L3099+8
	ldrb	r1, [r2, #140]	@ zero_extendqisi2
	cmp	r1, #0
	ldreq	r1, [sp, #4]
	addeq	r2, r2, r1, asl #1
	ldreqh	r2, [r2, #144]
	streq	r2, [sp, #4]
.L2965:
	ldr	r2, .L3099+4
	ldr	r1, [sp, #4]
	ldr	r0, [sp, #12]
	ldrh	r2, [r2]
	cmp	r2, r1
	ldr	r1, [sp, #8]
	ldreqh	r2, [sp, #4]
	streqh	r2, [r4, #2]	@ movhi
	moveq	r2, #0
	streqb	r2, [r4, #6]
	streqh	r2, [r4, #4]	@ movhi
	ldrh	r2, [sp, #28]
	str	r2, [sp, #32]
	ldr	ip, [sp, #32]
	ldr	r2, [sp, #4]
	cmp	r2, r1
	cmpeq	r0, ip
	moveq	r0, r4
	moveq	r1, r2
	beq	.L3092
	clz	r3, r3
	ldr	r2, [sp, #36]
	ldr	r1, [sp, #16]
	mov	r3, r3, lsr #5
	sub	r10, r2, #1
	movw	r2, #65535
	cmp	r1, r2
	orrne	r3, r3, #1
	cmp	r3, #0
	beq	.L2969
	ldr	r3, [r6, #-1908]
	uxth	fp, r7
	ldr	r8, .L3099+12
	mvn	r9, #0
	cmn	r3, #1
	mov	r7, r9
	ldreq	r3, .L3099+12
	streq	r10, [r3, #-1908]
	ldr	r3, [r6, #-1908]
	str	r3, [sp, #16]
	ldr	r3, [sp, #8]
	add	r3, r3, #7
	cmp	fp, r3
	subgt	r5, fp, #7
	ldrle	r5, [sp, #8]
	uxthgt	r5, r5
.L2972:
	cmp	r5, fp
	bhi	.L2985
	ldr	r3, .L3099+24
	mov	r0, #36
	ldr	lr, [r8, #-2076]
	ldr	r1, [sp, #20]
	ldrh	r3, [r3]
	str	r3, [sp, #28]
	mov	r3, #0
	mov	r6, r3
.L2986:
	ldr	ip, [sp, #28]
	uxth	r2, r3
	cmp	r2, ip
	bcs	.L3095
	ldrh	r2, [r1, #2]!
	movw	ip, #65535
	add	r3, r3, #1
	cmp	r2, ip
	orrne	r2, r5, r2, asl #10
	mlane	ip, r0, r6, lr
	addne	r6, r6, #1
	uxthne	r6, r6
	strne	r2, [ip, #4]
	b	.L2986
.L3095:
	mov	r1, r6
	ldr	r2, [sp, #24]
	ldr	r0, [r8, #-2076]
	bl	FlashReadPages
	ldr	r3, .L3099+8
	mov	r2, #36
	movw	r1, #65535
	ldrb	ip, [r3, #140]	@ zero_extendqisi2
	ldr	r3, [r8, #-2076]
	mla	r6, r2, r6, r3
	ldr	r2, .L3099+28
	add	r2, r2, r5, asl #1
.L2975:
	cmp	r3, r6
	beq	.L3096
	ldr	r0, [r3]
	cmp	r0, #0
	bne	.L2976
	ldr	r0, [r3, #12]
	ldrh	lr, [r0]
	cmp	lr, r1
	beq	.L2977
	ldr	r0, [r0, #4]
	cmn	r0, #1
	beq	.L2977
	cmn	r9, #1
	ldr	r7, [r8, #-1908]
	str	r0, [r8, #-1908]
	bne	.L2977
	ldrh	r0, [r2]
	cmp	r0, r1
	bne	.L2978
	cmp	ip, #0
	beq	.L2977
.L2978:
	cmp	r7, r10
	mvneq	r9, #0
	movne	r9, r7
	b	.L2977
.L2976:
	ldr	r3, .L3099+20
	ldrh	r2, [r4]
	strh	r2, [r3]	@ movhi
	ldrb	r3, [r4, #8]	@ zero_extendqisi2
	cmp	r3, #0
	bne	.L2969
	ldr	r3, .L3099+12
	mov	r5, r5, asl #1
	sub	r2, r3, #3136
	ldrh	r1, [r2, r5]
	movw	r2, #65535
	cmp	r1, r2
	bne	.L2980
	cmn	r9, #1
	strne	r9, [r3, #-1908]
	bne	.L2969
	ldr	r2, [sp, #16]
	cmp	r2, r10
	bne	.L3087
	ldr	r2, [r3, #-1908]
	b	.L3093
.L2980:
	cmp	r7, r10
	beq	.L2983
	cmn	r7, #1
	strne	r7, [r3, #-1908]
	b	.L2969
.L2983:
	ldr	r2, [r3, #-1908]
	cmp	r2, r10
	beq	.L2969
.L3093:
	sub	r2, r2, #1
	b	.L3087
.L2977:
	add	r3, r3, #36
	b	.L2975
.L3096:
	add	r5, r5, #1
	uxth	r5, r5
	b	.L2972
.L2985:
	ldr	r3, .L3099+12
	mvn	r2, #0
.L3087:
	str	r2, [r3, #-1908]
.L2969:
	ldr	r2, .L3099+32
	movw	r3, #310
	ldr	fp, [sp, #8]
	mov	r1, #1
	ldr	r5, .L3099+12
	strh	r1, [r2, r3]	@ movhi
.L2987:
	ldr	r3, .L3099+24
	movw	r1, #65535
	ldr	r9, [r5, #-2076]
	mov	r0, #36
	ldr	r7, [sp, #20]
	mov	r6, #0
	ldrh	r8, [r3]
	ldr	r3, .L3099+8
	str	r6, [sp, #16]
	ldrb	r3, [r3, #140]	@ zero_extendqisi2
	str	r3, [sp, #28]
.L2988:
	uxth	r3, r6
	cmp	r3, r8
	bcs	.L3097
	ldrh	r3, [r7, #2]!
	cmp	r3, r1
	beq	.L2989
	ldr	r2, [sp, #16]
	orr	r3, fp, r3, asl #10
	mla	r2, r0, r2, r9
	str	r3, [r2, #4]
	ldrb	ip, [r4, #8]	@ zero_extendqisi2
	cmp	ip, #1
	bne	.L2990
	ldr	ip, [sp, #28]
	cmp	ip, #0
	orrne	r3, r3, #-2147483648
	strne	r3, [r2, #4]
.L2990:
	ldr	r3, [sp, #16]
	add	lr, r3, #1
	uxth	r3, lr
	str	r3, [sp, #16]
.L2989:
	add	r6, r6, #1
	b	.L2988
.L3097:
	ldr	r0, [r5, #-2076]
	ldr	r1, [sp, #16]
	ldr	r2, [sp, #24]
	bl	FlashReadPages
	mov	r3, #0
.L3090:
	str	r3, [sp, #28]
	ldr	r2, [sp, #16]
	ldrh	r3, [sp, #28]
	cmp	r3, r2
	bcs	.L3098
	ldr	r3, [sp, #28]
	mov	r6, #36
	ldr	r9, [r5, #-2076]
	mul	r6, r6, r3
	add	r8, r9, r6
	ldr	r7, [r8, #4]
	ubfx	r0, r7, #10, #16
	str	r7, [sp, #56]
	bl	P2V_plane
	ldr	r3, [sp, #8]
	cmp	fp, r3
	bcc	.L2993
	ldr	r3, [sp, #32]
	ldr	r2, [sp, #8]
	cmp	r0, r3
	movcs	r3, #0
	movcc	r3, #1
	cmp	fp, r2
	movne	r3, #0
	cmp	r3, #0
	bne	.L2993
	ldr	r3, [sp, #12]
	ldr	r2, [sp, #4]
	cmp	r0, r3
	cmpeq	fp, r2
	beq	.L2994
	ldr	r3, [r9, r6]
	cmn	r3, #1
	beq	.L2995
	ldr	r8, [r8, #12]
	movw	r3, #61589
	ldrh	r2, [r8]
	cmp	r2, r3
	ldrneh	r0, [r4]
	bne	.L3088
	ldr	r10, [r8, #4]
	cmn	r10, #1
	beq	.L2997
	ldr	r1, [r5, #-3844]
	mov	r0, r10
	bl	ftl_cmp_data_ver
	cmp	r0, #0
	addne	r3, r10, #1
	strne	r3, [r5, #-3844]
.L2997:
	ldr	r7, [r8, #8]
	add	r1, sp, #52
	ldr	r3, [r8, #12]
	mov	r2, #0
	mov	r0, r7
	str	r3, [sp, #48]
	bl	log2phys
	ldr	r1, [r5, #-1908]
	cmn	r1, #1
	beq	.L2998
	mov	r0, r10
	bl	ftl_cmp_data_ver
	cmp	r0, #0
	beq	.L2998
	ldr	r3, [sp, #48]
	cmn	r3, #1
	beq	.L2999
	ldr	r0, [r5, #-2076]
	mov	r2, #0
	mov	r1, #1
	add	r0, r0, r6
	str	r3, [r0, #4]
	ldr	r9, [r0, #12]
	bl	FlashReadPages
	ldr	r2, [r5, #-2076]
	ldr	r3, [r9, #4]
	add	ip, r2, r6
	str	r3, [sp, #36]
	ldr	r3, [r2, r6]
	cmn	r3, #1
	bne	.L3000
	b	.L3001
.L2999:
	ldr	r3, [sp, #56]
	ldr	r2, [sp, #52]
	cmp	r2, r3
	bne	.L2993
	mov	r0, r7
	add	r1, sp, #48
	mov	r2, #1
	bl	log2phys
	b	.L2993
.L3000:
	ldr	r8, [r9, #8]
	cmp	r8, r7
	bne	.L3001
	ldr	r0, [r5, #-1908]
	ldr	r1, [sp, #36]
	str	r2, [sp, #44]
	str	ip, [sp, #40]
	bl	ftl_cmp_data_ver
	cmp	r0, #0
	ldr	ip, [sp, #40]
	ldr	r2, [sp, #44]
	beq	.L3001
	ldr	r3, [sp, #52]
	ldr	r1, [sp, #56]
	cmp	r3, r1
	beq	.L3006
	ldr	r1, [sp, #48]
	cmp	r3, r1
	beq	.L3001
	cmn	r3, #1
	streq	r3, [r2, r6]
	beq	.L3005
	str	r3, [ip, #4]
	mov	r0, ip
	mov	r1, #1
	mov	r2, #0
	ldr	r9, [ip, #12]
	bl	FlashReadPages
.L3005:
	ldr	r3, [r5, #-2076]
	ldr	r3, [r3, r6]
	cmn	r3, #1
	beq	.L3006
	ldr	r6, [r9, #4]
	ldr	r0, [r5, #-1908]
	mov	r1, r6
	bl	ftl_cmp_data_ver
	cmp	r0, #0
	beq	.L3006
	ldr	r0, [sp, #36]
	mov	r1, r6
	bl	ftl_cmp_data_ver
	cmp	r0, #0
	beq	.L3001
.L3006:
	mov	r0, r8
	ldr	r1, [sp, #48]
	bl	FtlReUsePrevPpa
.L3001:
	mvn	r3, #0
	str	r3, [sp, #48]
	b	.L3008
.L2998:
	ldr	r3, [sp, #56]
	ldr	r2, [sp, #52]
	cmp	r2, r3
	beq	.L3008
	ldr	r3, [sp, #48]
	ldr	r2, .L3099+8
	mov	r1, r3, lsr #10
	adds	r3, r3, #1
	ldr	r2, [r2, #1820]
	movne	r3, #1
	cmp	r1, r2
	movcc	r3, #0
	cmp	r3, #0
	bne	.L2993
	mov	r0, r7
	add	r1, sp, #56
	mov	r2, #1
	bl	log2phys
	ldr	r6, [sp, #52]
	cmn	r6, #1
	beq	.L3008
	ldr	r3, [sp, #48]
	cmp	r6, r3
	beq	.L3008
	ubfx	r0, r6, #10, #16
	bl	P2V_block_in_plane
	ldr	r3, .L3099+36
	ldrh	r2, [r3]
	cmp	r2, r0
	beq	.L3011
	ldrh	r2, [r3, #48]
	cmp	r2, r0
	beq	.L3011
	ldrh	r3, [r3, #96]
	cmp	r3, r0
	bne	.L3008
.L3011:
	ldr	r0, [r5, #-2076]
	mov	r1, #1
	mov	r2, #0
	str	r6, [r0, #4]
	ldr	r8, [r0, #12]
	bl	FlashReadPages
	ldr	r3, [r5, #-2076]
	ldr	r1, [r8, #4]
	ldr	r3, [r3]
	cmn	r3, #1
	beq	.L3008
	mov	r0, r10
	bl	ftl_cmp_data_ver
	cmp	r0, #0
	bne	.L3008
	mov	r0, r7
	add	r1, sp, #52
	mov	r2, #1
	bl	log2phys
.L3008:
	ldr	r0, [sp, #48]
	cmn	r0, #1
	beq	.L2993
	ubfx	r0, r0, #10, #16
	bl	P2V_block_in_plane
	ldr	r2, [r5, #-4060]
	mov	r3, r0, asl #1
	mov	r1, r0
	ldrh	r3, [r2, r3]
	cmp	r3, #0
	beq	.L3012
.L3088:
	bl	decrement_vpc_count
	b	.L2993
.L3012:
	ldr	r0, .L3099+40
	bl	printk
	b	.L2993
.L2995:
	ldrh	r2, [r4]
	mov	r1, r7
	ldr	r3, .L3099+20
	ldr	r0, .L3099+44
	strh	r2, [r3]	@ movhi
	mov	r2, r10
	bl	printk
	ldr	r3, .L3099+32
	ldr	r3, [r3, #312]
	cmp	r3, #31
	bhi	.L3013
	ldr	r2, .L3099+32
	ldr	r1, [sp, #56]
	add	r2, r2, r3, asl #2
	add	r3, r3, #1
	str	r1, [r2, #316]
	ldr	r2, .L3099+32
	str	r3, [r2, #312]
.L3013:
	ldrh	r0, [r4]
	bl	decrement_vpc_count
	ldr	r3, [r5, #-1908]
	cmn	r3, #1
	beq	.L3089
	cmp	r3, r10
	bls	.L2993
.L3089:
	str	r10, [r5, #-1908]
.L2993:
	ldr	r3, [sp, #28]
	add	r3, r3, #1
	b	.L3090
.L3098:
	ldrb	r3, [r4, #8]	@ zero_extendqisi2
	add	fp, fp, #1
	cmp	r3, #1
	uxth	fp, fp
	bne	.L3016
	ldr	r3, .L3099+8
	ldrb	r3, [r3, #140]	@ zero_extendqisi2
	cmp	r3, #0
	beq	.L3016
	ldr	r3, .L3099+48
	ldr	r2, [sp, #4]
	ldrh	r3, [r3]
	cmp	r2, fp
	cmpeq	r3, fp
	beq	.L2994
.L3016:
	ldr	r2, .L3099+8
	add	r3, r2, #1872
	ldrh	r3, [r3]
	cmp	fp, r3
	bne	.L2987
	movw	r1, #1804
	movw	r0, #65535
	ldrh	r1, [r2, r1]
	mov	r3, #0
	strh	fp, [r4, #2]	@ movhi
	strh	r3, [r4, #4]	@ movhi
.L3017:
	uxth	r2, r3
	cmp	r2, r1
	bcs	.L2945
	ldr	lr, [sp, #20]
	add	r3, r3, #1
	ldrh	ip, [lr, #2]!
	cmp	ip, r0
	str	lr, [sp, #20]
	beq	.L3017
	strb	r2, [r4, #6]
	b	.L2945
.L2994:
	ldrb	r3, [sp, #12]	@ zero_extendqisi2
	mov	r0, r4
	ldr	r1, [sp, #4]
	strb	r3, [r4, #6]
	ldrh	r3, [sp, #4]
	strh	r3, [r4, #2]	@ movhi
.L3092:
	ldr	r2, [sp, #12]
	bl	ftl_sb_update_avl_pages
.L2945:
	ldr	r3, .L3099
	mov	r0, #0
	ldr	r2, [sp, #60]
	ldr	r3, [r3]
	cmp	r2, r3
	beq	.L3021
	bl	__stack_chk_fail
.L3021:
	add	sp, sp, #68
	@ sp needed
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L3100:
	.align	2
.L3099:
	.word	__stack_chk_guard
	.word	.LANCHOR0+1872
	.word	.LANCHOR0
	.word	.LANCHOR2
	.word	.LC144
	.word	.LANCHOR4+308
	.word	.LANCHOR0+1804
	.word	.LANCHOR2-3136
	.word	.LANCHOR4
	.word	.LANCHOR2-4040
	.word	.LC145
	.word	.LC146
	.word	.LANCHOR0+1874
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
	ldr	r7, .L3118
	ldr	r5, .L3118+4
	ldr	r1, .L3118+8
	ldr	r3, [r7]
	ldr	r0, .L3118+12
	ldr	r6, .L3118+16
	str	r3, [sp, #4]
	bl	printk
	movw	r3, #1814
	mov	r1, #0
	ldrh	r2, [r5, r3]
	ldr	r0, [r6, #-2016]
	mov	r2, r2, asl #1
	bl	ftl_memset
.L3102:
	ldr	r3, [r5, #1932]
	cmp	r4, r3
	bcs	.L3116
	mov	r0, r4
	mov	r1, sp
	mov	r2, #0
	bl	log2phys
	ldr	r0, [sp]
	cmn	r0, #1
	beq	.L3103
	ubfx	r0, r0, #10, #16
	bl	P2V_block_in_plane
	ldr	r2, [r6, #-2016]
	mov	r0, r0, asl #1
	ldrh	r3, [r2, r0]
	add	r3, r3, #1
	strh	r3, [r2, r0]	@ movhi
.L3103:
	add	r4, r4, #1
	b	.L3102
.L3116:
	ldr	r10, .L3118+20
	mov	r8, #0
	ldr	r9, .L3118+16
.L3105:
	ldrh	r3, [r10]
	uxth	r4, r8
	cmp	r3, r4
	bls	.L3117
	ldr	r3, [r6, #-4060]
	mov	r5, r4, asl #1
	movw	r1, #65535
	ldrh	r2, [r3, r5]
	ldr	r3, [r6, #-2016]
	ldrh	r3, [r3, r5]
	cmp	r2, r3
	cmpne	r2, r1
	beq	.L3107
	ldr	r0, .L3118+24
	mov	r1, r4
	bl	printk
	ldr	r3, .L3118+28
	ldrh	r2, [r3]
	cmp	r2, r4
	beq	.L3107
	ldrh	r2, [r3, #96]
	cmp	r2, r4
	beq	.L3107
	ldrh	r3, [r3, #48]
	cmp	r3, r4
	beq	.L3107
	ldr	r3, [r9, #-4060]
	ldrh	r2, [r3, r5]
	cmp	r2, #0
	ldr	r2, [r9, #-2016]
	ldrh	r2, [r2, r5]
	strh	r2, [r3, r5]	@ movhi
	beq	.L3107
	mov	r0, r4
	bl	update_vpc_list
.L3107:
	add	r8, r8, #1
	b	.L3105
.L3117:
	bl	l2p_flush
	bl	FtlVpcTblFlush
	ldr	r2, [sp, #4]
	ldr	r3, [r7]
	cmp	r2, r3
	beq	.L3111
	bl	__stack_chk_fail
.L3111:
	add	sp, sp, #8
	@ sp needed
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, pc}
.L3119:
	.align	2
.L3118:
	.word	__stack_chk_guard
	.word	.LANCHOR0
	.word	.LANCHOR3+84
	.word	.LC107
	.word	.LANCHOR2
	.word	.LANCHOR0+1812
	.word	.LC147
	.word	.LANCHOR2-4040
	.fnend
	.size	FtlVpcCheckAndModify, .-FtlVpcCheckAndModify
	.align	2
	.global	FtlGcScanTempBlk
	.type	FtlGcScanTempBlk, %function
FtlGcScanTempBlk:
	.fnstart
	@ args = 0, pretend = 0, frame = 72
	@ frame_needed = 0, uses_anonymous_args = 0
	ldr	r3, .L3168
	stmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.save {r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.pad #76
	sub	sp, sp, #76
	ldr	r3, [r3]
	mov	r4, r0
	ldr	r2, .L3168+4
	str	r1, [sp, #12]
	str	r3, [sp, #68]
	movw	r3, #3288
	ldrh	r5, [r2, r3]
	movw	r3, #65535
	cmp	r5, r3
	beq	.L3152
	cmp	r5, #0
	bne	.L3121
	b	.L3122
.L3152:
	mov	r5, #0
.L3121:
	ldr	r3, .L3168+8
	ldr	r2, [sp, #12]
	ldrh	r3, [r3]
	cmp	r2, r3
	bne	.L3123
.L3122:
	bl	FtlGcPageVarInit
.L3123:
	ldr	r6, .L3168+12
	mvn	r3, #0
	str	r3, [sp, #8]
	mov	r3, #0
	str	r3, [sp]
.L3124:
	ldrh	r2, [r4]
	mov	r3, #0
	strb	r3, [r4, #8]
	movw	r3, #65535
	cmp	r2, r3
	beq	.L3125
.L3126:
.L3148:
	ldr	r3, .L3168+16
	mov	r2, #0
	ldr	lr, [r6, #-3208]
	add	r0, r4, #14
	mov	r7, r2
	movw	r9, #65535
	ldrh	r3, [r3]
	mov	r10, #36
	str	r3, [sp, #4]
	ldr	r3, [r6, #-2076]
	str	r3, [sp, #16]
	ldr	r3, [r6, #-3212]
	str	r3, [sp, #20]
	ldr	r3, .L3168+20
	ldrh	ip, [r3]
	ldrh	r8, [r3, #2]
.L3127:
	ldr	r1, [sp, #4]
	uxth	r3, r2
	cmp	r3, r1
	bcs	.L3166
	ldrh	r3, [r0, #2]!
	cmp	r3, r9
	beq	.L3128
	ldr	r1, [sp, #16]
	orr	r3, r5, r3, asl #10
	mla	r1, r10, r7, r1
	str	r3, [r1, #4]
	mul	r3, ip, r7
	add	fp, r3, #3
	cmp	r3, #0
	movlt	r3, fp
	ldr	fp, [sp, #20]
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
.L3128:
	add	r2, r2, #1
	b	.L3127
.L3166:
	ldr	r0, [r6, #-2076]
	mov	r1, r7
	mov	r2, #0
	ldr	fp, .L3168+24
	bl	FlashReadPages
	mov	r10, #0
.L3130:
	uxth	r3, r10
	cmp	r3, r7
	bcs	.L3167
	mov	r9, #36
	ldr	r2, [r6, #-2076]
	mul	r9, r9, r10
	ldr	ip, .L3168+12
	add	r3, r2, r9
	ldr	r8, [r2, r9]
	ldr	r1, [r3, #4]
	cmp	r8, #0
	ldr	r3, [r3, #12]
	str	r1, [sp, #4]
	bne	.L3131
	ldrh	r1, [r3]
	movw	r2, #65535
	cmp	r1, r2
	bne	.L3132
.L3134:
	ldr	r2, .L3168+12
	ldrh	r3, [r4]
	ldr	r2, [r2, #-4060]
	b	.L3165
.L3132:
	ldr	r0, [r3, #8]
	ldr	r2, [fp, #1932]
	cmp	r0, r2
	bhi	.L3134
	ldrb	r2, [fp]	@ zero_extendqisi2
	cmp	r2, #0
	beq	.L3136
	add	r1, sp, #28
	mov	r2, r8
	str	r3, [sp, #16]
	bl	log2phys
	ldr	r3, [sp, #16]
	ldr	r1, [sp, #28]
	ldr	r2, [r3, #12]
	cmn	r1, #1
	rsb	r0, r2, r1
	clz	r0, r0
	mov	r0, r0, lsr #5
	moveq	r0, #0
	cmp	r0, #0
	beq	.L3136
	str	r2, [sp, #36]
	mov	r1, #1
	ldr	r2, [r6, #-2044]
	add	r0, sp, #32
	str	r2, [sp, #40]
	ldr	r2, [r6, #-2036]
	str	r2, [sp, #44]
	mov	r2, r8
	bl	FlashReadPages
	ldr	r2, .L3168+28
	ldr	r1, [r6, #-2076]
	ldrh	r2, [r2]
	add	r9, r1, r9
	mov	r2, r2, asl #7
	ldr	r1, [sp, #40]
	ldr	r3, [sp, #16]
	b	.L3138
.L3139:
	add	r8, r8, #1
.L3138:
	cmp	r8, r2
	beq	.L3136
	ldr	r0, [r9, #8]
	ldr	ip, [r0, r8, asl #2]
	ldr	r0, [r1, r8, asl #2]
	cmp	ip, r0
	beq	.L3139
	ldr	r2, [sp, #36]
	ldr	r0, .L3168+32
	ldrh	r1, [r4]
	bl	printk
	ldrh	r3, [r4]
	ldr	r2, [r6, #-4060]
.L3165:
	mov	r3, r3, asl #1
	mov	r1, #0
	strh	r1, [r2, r3]	@ movhi
	ldrh	r0, [r4]
	bl	INSERT_FREE_LIST
	ldr	r2, .L3168+36
	mvn	r3, #0
	strh	r3, [r4]	@ movhi
	strh	r3, [r2]	@ movhi
	b	.L3164
.L3136:
	ldr	r0, [r3, #12]
	add	r10, r10, #1
	ldr	r1, [sp, #4]
	ldr	r2, [r3, #8]
	bl	FtlGcUpdatePage
	b	.L3130
.L3131:
	ldr	r2, [sp, #4]
	ldr	r0, .L3168+40
	ldrh	r1, [r4]
	str	ip, [sp, #16]
	bl	printk
	ldr	r3, .L3168+24
	ldrh	r5, [r4]
	ldr	r2, [r3, #1796]
	cmp	r2, #0
	ldr	ip, [sp, #16]
	bne	.L3142
	ldrb	r2, [r3, #140]	@ zero_extendqisi2
	cmp	r2, #0
	beq	.L3143
.L3142:
	ldr	r2, [r3, #4060]
	mov	r3, r5, asl #1
	ldrh	r3, [r2, r3]
	cmp	r3, #159
	bls	.L3144
.L3143:
	ldr	r3, [ip, #-2076]
	ldr	r3, [r3, r9]
	cmn	r3, #1
	bne	.L3145
.L3144:
	ldr	r3, [ip, #-2076]
	add	r9, r3, r9
	ldr	r3, [r9, #4]
	str	r3, [sp, #8]
.L3145:
	ldr	r3, .L3168+12
	mov	r5, r5, asl #1
	mov	r2, #0
	ldr	r3, [r3, #-4060]
	strh	r2, [r3, r5]	@ movhi
	ldrh	r0, [r4]
	bl	INSERT_FREE_LIST
	mvn	r3, #0
	strh	r3, [r4]	@ movhi
.L3164:
	bl	FtlGcPageVarInit
	mov	r5, #0
	b	.L3124
.L3167:
	ldr	r3, [sp]
	add	r5, r5, #1
	ldr	r2, [sp, #12]
	add	r3, r3, #1
	uxth	r5, r5
	cmp	r3, r2
	str	r3, [sp]
	ldr	r2, .L3168+8
	bcs	.L3147
.L3149:
	ldrh	r3, [r2]
	cmp	r3, r5
	bhi	.L3148
	b	.L3125
.L3147:
	ldr	r1, .L3168+44
	movw	r0, #65535
	ldrh	r3, [r1]
	cmp	r3, r0
	beq	.L3149
	ldr	r0, [sp]
	add	r3, r3, r0
	strh	r3, [r1]	@ movhi
	ldrh	r3, [r2]
	cmp	r3, r5
	bls	.L3149
	b	.L3150
.L3125:
	ldr	r2, .L3168+4
	movw	r3, #3288
	mvn	r1, #0
	strh	r5, [r4, #2]	@ movhi
	mov	r0, r4
	strh	r1, [r2, r3]	@ movhi
	mov	r2, #0
	mov	r1, r5
	strb	r2, [r4, #6]
	bl	ftl_sb_update_avl_pages
.L3150:
	ldr	r3, .L3168
	ldr	r2, [sp, #68]
	ldr	r0, [sp, #8]
	ldr	r3, [r3]
	cmp	r2, r3
	beq	.L3151
	bl	__stack_chk_fail
.L3151:
	add	sp, sp, #76
	@ sp needed
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L3169:
	.align	2
.L3168:
	.word	__stack_chk_guard
	.word	.LANCHOR1
	.word	.LANCHOR0+1872
	.word	.LANCHOR2
	.word	.LANCHOR0+1804
	.word	.LANCHOR0+1882
	.word	.LANCHOR0
	.word	.LANCHOR0+1878
	.word	.LC148
	.word	.LANCHOR2-3800
	.word	.LC149
	.word	.LANCHOR1+3288
	.fnend
	.size	FtlGcScanTempBlk, .-FtlGcScanTempBlk
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
	ldr	r5, .L3188
	ldr	r7, .L3188+4
	ldr	r4, .L3188+8
	ldr	r10, [r5, #-3668]
	mov	r6, r5
	ldr	r3, [r7]
	cmp	r10, #0
	str	r3, [sp, #44]
	beq	.L3171
	ldr	r2, [r5, #-3664]
	ldr	r3, [r4, #1932]
	cmp	r2, r3
	bcs	.L3172
	mov	r5, #2048
	mov	r8, r6
.L3177:
	ldr	r0, [r6, #-3664]
	ldr	r3, [r4, #1932]
	cmp	r0, r3
	bcs	.L3176
	mov	r2, #0
	add	r1, sp, #4
	bl	log2phys
	ldr	r3, [r8, #-3664]
	add	r3, r3, #1
	str	r3, [r8, #-3664]
	ldr	r2, [sp, #4]
	cmn	r2, #1
	beq	.L3175
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
	bne	.L3176
	ldr	r0, [sp, #4]
	ubfx	r0, r0, #10, #16
	bl	P2V_block_in_plane
	bl	FtlGcRefreshBlock
.L3176:
	mvn	r0, #0
	b	.L3179
.L3175:
	subs	r5, r5, #1
	bne	.L3177
	b	.L3176
.L3172:
	ldr	r3, [r5, #-3880]
	mov	r0, #0
	str	r0, [r5, #-3668]
	str	r0, [r5, #-3664]
	str	r3, [r5, #-3672]
	b	.L3179
.L3171:
	ldr	r1, [r5, #-3828]
	ldr	r9, [r5, #-3880]
	ldr	r3, [r4, #1932]
	movw	r4, #10000
	ldr	r8, [r5, #-3672]
	cmp	r1, r4
	add	r2, r9, #1048576
	ldr	fp, .L3188+12
	movhi	r4, #31
	movls	r4, #63
	cmp	r8, r2
	bhi	.L3181
	mov	r1, r1, lsr #10
	mov	r0, #1000
	mul	r0, r0, r3
	add	r1, r1, #1
	bl	__aeabi_uidiv
	add	r0, r0, r8
	cmp	r0, r9
	bcc	.L3181
	ldrh	r3, [fp, #28]
	ands	r0, r4, r3
	movne	r0, r10
	bne	.L3179
	ldr	r2, [r5, #-3648]
	cmp	r2, r3
	beq	.L3179
.L3181:
	ldrh	r3, [fp, #28]
	mov	r0, #0
	str	r9, [r6, #-3672]
	str	r0, [r6, #-3664]
	str	r3, [r6, #-3648]
	mov	r3, #1
	str	r3, [r6, #-3668]
.L3179:
	ldr	r2, [sp, #44]
	ldr	r3, [r7]
	cmp	r2, r3
	beq	.L3182
	bl	__stack_chk_fail
.L3182:
	add	sp, sp, #52
	@ sp needed
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L3189:
	.align	2
.L3188:
	.word	.LANCHOR2
	.word	__stack_chk_guard
	.word	.LANCHOR0
	.word	.LANCHOR0+4068
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
	.pad #28
	sub	sp, sp, #28
	ldr	r9, .L3230
	ldr	r2, .L3230+4
	add	r6, r9, #1872
	ldr	ip, [r9, #4052]
	ldr	r3, [r2]
	cmp	ip, #0
	ldrh	r1, [r6]
	str	r2, [sp, #4]
	str	r3, [sp, #20]
	bne	.L3228
	ldr	r5, .L3230+8
	mov	r4, r9
	movw	lr, #65535
	add	r8, r5, #3936
	ldrh	r9, [r5, #-8]
	cmp	r9, lr
	bne	.L3193
.L3202:
	ldrh	r2, [r5, #-8]
	movw	r3, #65535
	ldr	r9, .L3230+12
	mov	r7, #0
	cmp	r2, r3
	str	r7, [r8, #-1912]
	sub	r10, r9, #3936
	beq	.L3228
	bl	FtlCacheWriteBack
	ldrh	r2, [r6]
	ldrb	r0, [r9, #-3937]	@ zero_extendqisi2
	sub	r6, r9, #3184
	ldrh	r3, [r10, #-8]
	sub	r10, r6, #760
	ldr	r1, [r9, #-4060]
	smulbb	r2, r0, r2
	mov	r3, r3, asl #1
	strh	r2, [r1, r3]	@ movhi
	ldr	r2, [r9, #-3860]
	ldrh	r3, [r6]
	add	r3, r3, r2
	str	r3, [r9, #-3860]
	str	r9, [sp, #8]
	b	.L3203
.L3193:
	cmp	r0, #0
	beq	.L3196
	ldr	r2, .L3230+16
	movw	r3, #3288
	ldrh	r0, [r2, r3]
	cmp	r0, lr
	beq	.L3197
.L3198:
	mov	r1, #2
	b	.L3196
.L3197:
	strh	ip, [r2, r3]	@ movhi
	sub	r3, r8, #4032
	ldrh	r3, [r3, #-12]
	cmp	r3, #17
	bhi	.L3198
.L3196:
	ldr	r0, .L3230+20
	bl	FtlGcScanTempBlk
	cmn	r0, #1
	str	r0, [sp, #16]
	beq	.L3199
	ldr	r2, [r4, #4060]
	mov	r9, r9, asl #1
	ldrh	r3, [r2, r9]
	cmp	r3, #4
	bls	.L3200
	sub	r3, r3, #5
	mov	r0, #1
	strh	r3, [r2, r9]	@ movhi
	bl	FtlEctTblFlush
.L3200:
	ldr	r3, [r8, #-1912]
	ldr	r2, .L3230+12
	cmp	r3, #0
	bne	.L3201
	ldr	r0, [sp, #16]
	ldr	r3, [r2, #-3652]
	ubfx	r0, r0, #10, #16
	add	r3, r3, #1
	str	r3, [r2, #-3652]
	bl	FtlBbmMapBadBlock
	bl	FtlBbmTblFlush
.L3201:
	mov	r3, #0
	str	r3, [r8, #-1912]
	b	.L3214
.L3199:
	ldr	r2, .L3230+16
	movw	r3, #3288
	ldrh	r2, [r2, r3]
	movw	r3, #65535
	cmp	r2, r3
	bne	.L3214
	b	.L3202
.L3206:
	ldr	r3, [fp, #4]
	cmp	r0, r3
	bne	.L3226
.L3205:
	add	r7, r7, #1
.L3203:
	ldrh	r3, [r6]
	uxth	r9, r7
	cmp	r3, r9
	bls	.L3229
	mov	r3, #12
	ldr	r2, [r4, #1932]
	mul	r9, r3, r9
	ldr	r3, [sp, #8]
	ldr	ip, [r3, #-3188]
	add	fp, ip, r9
	ldr	r0, [fp, #8]
	cmp	r0, r2
	bcs	.L3226
	mov	r2, #0
	add	r1, sp, #16
	str	ip, [sp, #12]
	bl	log2phys
	ldr	ip, [sp, #12]
	ldr	r0, [sp, #16]
	ldr	r2, [ip, r9]
	cmp	r0, r2
	bne	.L3206
	ubfx	r0, r0, #10, #16
	bl	P2V_block_in_plane
	add	r1, fp, #4
	mov	r2, #1
	mov	r9, r0
	ldr	r0, [fp, #8]
	bl	log2phys
	mov	r0, r9
	b	.L3227
.L3226:
	ldrh	r0, [r10]
.L3227:
	bl	decrement_vpc_count
	b	.L3205
.L3229:
	movw	r0, #65535
	bl	decrement_vpc_count
	ldrb	r3, [r4, #140]	@ zero_extendqisi2
	cmp	r3, #0
	beq	.L3208
	ldr	r0, .L3230+24
	ldrh	r1, [r5, #-8]
	bl	printk
.L3208:
	ldrh	r0, [r5, #-8]
	ldr	r2, [r8, #-4060]
	mov	r3, r0, asl #1
	ldrh	r3, [r2, r3]
	cmp	r3, #0
	beq	.L3209
	bl	INSERT_DATA_LIST
	b	.L3210
.L3209:
	bl	INSERT_FREE_LIST
.L3210:
	ldr	r7, .L3230+12
	mvn	r8, #0
	strh	r8, [r5, #-8]	@ movhi
	mov	r5, #0
	strh	r5, [r6]	@ movhi
	strh	r5, [r6, #-8]	@ movhi
	bl	l2p_flush
	bl	FtlVpcTblFlush
	sub	r3, r7, #3792
	sub	r2, r7, #4032
	strh	r8, [r3, #-8]	@ movhi
	ldr	r3, [r4, #1796]
	ldrh	r2, [r2, #-12]
	cmp	r3, r5
	sub	r3, r7, #3808
	ldrh	r3, [r3, #-12]
	beq	.L3211
	ldr	r1, [r7, #-3824]
	cmp	r1, #39
	bhi	.L3211
	cmp	r2, r3
	subcc	r7, r7, #3232
	movcc	r3, r3, asl #1
	strcch	r3, [r7, #-4]	@ movhi
	b	.L3228
.L3211:
	add	r1, r3, r3, asl #1
	cmp	r2, r1, asr #2
	ble	.L3228
	ldrb	r0, [r4, #140]	@ zero_extendqisi2
	ldr	r2, .L3230+28
	cmp	r0, #0
	moveq	r3, #20
	streqh	r3, [r2, #-4]	@ movhi
	beq	.L3192
	sub	r3, r3, #2
	strh	r3, [r2, #-4]	@ movhi
.L3228:
	mov	r0, #0
	b	.L3192
.L3214:
	mov	r0, #1
.L3192:
	ldr	r3, [sp, #4]
	ldr	r2, [sp, #20]
	ldr	r3, [r3]
	cmp	r2, r3
	beq	.L3213
	bl	__stack_chk_fail
.L3213:
	add	sp, sp, #28
	@ sp needed
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L3231:
	.align	2
.L3230:
	.word	.LANCHOR0
	.word	__stack_chk_guard
	.word	.LANCHOR2-3936
	.word	.LANCHOR2
	.word	.LANCHOR1
	.word	.LANCHOR2-3944
	.word	.LC150
	.word	.LANCHOR2-3232
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
	ldr	r5, .L3235
	ldr	r4, .L3235+4
	mov	r0, r5
	ldrh	r1, [r4]
	bl	FtlGcScanTempBlk
	ldrh	r2, [r5, #2]
	ldrh	r3, [r4]
	cmp	r2, r3
	ldmccfd	sp!, {r3, r4, r5, pc}
	ldr	r0, .L3235+8
	bl	FtlMapBlkWriteDumpData
	mov	r0, #0
	bl	FtlGcFreeTempBlock
	ldr	r3, .L3235+12
	mov	r2, #0
	str	r2, [r3, #-1912]
	ldmfd	sp!, {r3, r4, r5, pc}
.L3236:
	.align	2
.L3235:
	.word	.LANCHOR2-3944
	.word	.LANCHOR0+1872
	.word	.LANCHOR2-1972
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
	ldr	r4, .L3239
	ldr	r3, .L3239+4
	sub	r5, r4, #4032
	sub	r4, r4, #3984
	sub	r5, r5, #8
	sub	r4, r4, #8
	str	r6, [r3, #312]
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
.L3240:
	.align	2
.L3239:
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
	movw	r3, #310
	ldr	r7, .L3259
	mov	r2, #0
	ldr	r6, .L3259+4
	ldr	r4, .L3259+8
	strh	r2, [r7, r3]	@ movhi
	add	r3, r7, #308
	mvn	r2, #0
	strh	r2, [r3]	@ movhi
	ldr	r3, [r6, #1808]
	uxth	r0, r3
	bl	FtlFreeSysBlkQueueInit
	bl	FtlScanSysBlk
	sub	r3, r4, #3808
	ldrh	r2, [r3, #-8]
	movw	r3, #65535
	cmp	r2, r3
	bne	.L3242
.L3244:
	mvn	r8, #0
	b	.L3243
.L3242:
	bl	FtlLoadSysInfo
	subs	r8, r0, #0
	bne	.L3244
	bl	FtlLoadMapInfo
	bl	FtlLoadVonderInfo
	bl	Ftl_load_ext_data
	bl	FtlLoadEctTbl
	bl	FtlFreeSysBLkSort
	bl	SupperBlkListInit
	bl	FtlPowerLostRecovery
	mov	r0, #1
	bl	FtlUpdateVaildLpn
	ldr	r2, [r4, #-3896]
	movw	r3, #1910
	mov	r0, #12
	ldrh	r1, [r6, r3]
	mov	r3, r8
.L3245:
	cmp	r3, r1
	bge	.L3250
	mla	ip, r0, r3, r2
	ldr	ip, [ip, #4]
	cmp	ip, #0
	bge	.L3246
.L3250:
	ldr	r5, .L3259+12
	cmp	r3, r1
	ldr	r9, .L3259+16
	ldrh	r2, [r5, #28]
	add	r2, r2, #1
	strh	r2, [r5, #28]	@ movhi
	sub	r2, r9, #8
	bge	.L3257
	b	.L3247
.L3246:
	add	r3, r3, #1
	b	.L3245
.L3257:
	movw	r3, #310
	ldrh	r3, [r7, r3]
	cmp	r3, #0
	beq	.L3251
.L3247:
	ldrh	r3, [r9, #-8]
	ldr	r0, [r4, #-4060]
	ldrh	ip, [r2, #4]
	mov	r3, r3, asl #1
	ldrh	r1, [r0, r3]
	rsb	r1, ip, r1
	ldr	ip, .L3259+20
	strh	r1, [r0, r3]	@ movhi
	ldr	lr, [r4, #-4060]
	ldrh	r3, [ip]
	strh	r3, [r2, #2]	@ movhi
	mov	r3, #0
	strh	r3, [r2, #4]	@ movhi
	ldr	r2, .L3259+24
	strb	r3, [r4, #-4034]
	sub	r1, r2, #8
	ldrh	r2, [r2, #-8]
	ldrh	r7, [r1, #4]
	mov	r2, r2, asl #1
	ldrh	r0, [lr, r2]
	rsb	r0, r7, r0
	strh	r0, [lr, r2]	@ movhi
	ldrh	r2, [ip]
	strh	r3, [r1, #4]	@ movhi
	strb	r3, [r4, #-3986]
	ldrh	r3, [r5, #30]
	strh	r2, [r1, #2]	@ movhi
	add	r3, r3, #1
	strh	r3, [r5, #30]	@ movhi
	bl	l2p_flush
	bl	FtlVpcTblFlush
	bl	FtlVpcTblFlush
.L3251:
	ldrh	r0, [r9, #-8]
	movw	r3, #65535
	ldr	r4, .L3259+28
	cmp	r0, r3
	beq	.L3252
	ldrh	r3, [r4, #4]
	cmp	r3, #0
	bne	.L3252
	ldrh	r3, [r4, #52]
	add	r9, r4, #56
	add	r7, r4, #48
	cmp	r3, #0
	bne	.L3252
	bl	FtlGcRefreshOpenBlock
	ldrh	r0, [r4, #48]
	bl	FtlGcRefreshOpenBlock
	bl	FtlVpcTblFlush
	mov	r0, r4
	bl	allocate_new_data_superblock
	mov	r0, r7
	bl	allocate_new_data_superblock
.L3252:
	ldrb	r3, [r6]	@ zero_extendqisi2
	cmp	r3, #0
	bne	.L3253
	ldrh	r3, [r5, #28]
	tst	r3, #31
	bne	.L3243
.L3253:
	bl	FtlVpcCheckAndModify
.L3243:
	mov	r0, r8
	ldmfd	sp!, {r3, r4, r5, r6, r7, r8, r9, pc}
.L3260:
	.align	2
.L3259:
	.word	.LANCHOR4
	.word	.LANCHOR0
	.word	.LANCHOR2
	.word	.LANCHOR0+4068
	.word	.LANCHOR2-4032
	.word	.LANCHOR0+1872
	.word	.LANCHOR2-3984
	.word	.LANCHOR2-4040
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
	ldr	r5, .L3296
	ldr	r6, [r5, #4052]
	cmp	r6, #0
	bne	.L3263
	movw	r7, #1908
	ldr	r4, .L3296+4
	ldrh	r2, [r5, r7]
	mov	r1, r6
	ldr	r0, [r4, #-1988]
	mov	r2, r2, asl #2
	bl	ftl_memset
	ldrh	r2, [r5, r7]
	mov	r1, r6
	ldr	r0, [r4, #-1992]
	mov	r2, r2, asl #2
	bl	ftl_memset
	ldr	r3, [r5, #1808]
	str	r6, [r4, #-3848]
	str	r6, [r4, #-3844]
	uxth	r0, r3
	bl	FtlFreeSysBlkQueueInit
	bl	FtlLoadBbt
	cmp	r0, #0
	beq	.L3264
	bl	FtlMakeBbt
.L3264:
	ldr	r0, .L3296+8
	mov	r2, #0
	ldr	ip, .L3296+12
.L3265:
	ldrh	r1, [r0]
	uxth	r3, r2
	add	r2, r2, #1
	cmp	r3, r1, asl #7
	bge	.L3293
	ldr	lr, [r4, #-2048]
	mvn	r1, r3
	orr	r1, r3, r1, asl #16
	str	r1, [lr, r3, asl #2]
	ldr	r1, [r4, #-2044]
	str	ip, [r1, r3, asl #2]
	b	.L3265
.L3293:
	ldr	r2, .L3296
	movw	r3, #1812
	ldr	r10, .L3296+16
	mov	r6, #0
	ldrh	r7, [r2, r3]
.L3267:
	ldrh	r3, [r10]
	ldr	r8, .L3296
	cmp	r3, r7
	ldr	r9, .L3296+16
	bls	.L3294
	mov	r0, r7
	mov	r1, #1
	bl	FtlLowFormatEraseBlock
	add	r7, r7, #1
	uxth	r7, r7
	add	r0, r6, r0
	uxth	r6, r0
	b	.L3267
.L3294:
	movw	r3, #1804
	ldrh	r1, [r8, r3]
	sub	r3, r6, #3
	cmp	r3, r1, asl #1
	bge	.L3269
.L3273:
	mov	r6, #0
	ldr	r7, .L3296+20
	mov	r8, r6
	b	.L3270
.L3269:
	mov	r0, r6
	bl	__aeabi_uidiv
	ldr	r3, [r8, #1904]
	add	r0, r0, r3
	uxth	r0, r0
	bl	FtlSysBlkNumInit
	ldr	r3, [r8, #1808]
	uxth	r0, r3
	bl	FtlFreeSysBlkQueueInit
	movw	r3, #1812
	ldrh	r6, [r8, r3]
.L3271:
	ldrh	r3, [r9]
	cmp	r3, r6
	bls	.L3273
	mov	r0, r6
	add	r6, r6, #1
	mov	r1, #1
	bl	FtlLowFormatEraseBlock
	uxth	r6, r6
	b	.L3271
.L3270:
	ldrh	r3, [r7]
	uxth	r0, r6
	ldr	fp, .L3296
	add	r6, r6, #1
	cmp	r3, r0
	bls	.L3295
	mov	r1, #0
	bl	FtlLowFormatEraseBlock
	add	r0, r8, r0
	uxth	r8, r0
	b	.L3270
.L3295:
	movw	r3, #1814
	ldr	r2, [fp, #1816]
	ldrh	r3, [fp, r3]
	ldr	r6, .L3296+24
	mov	r0, r2
	str	r2, [sp, #4]
	str	r3, [r4, #-2088]
	movw	r3, #1804
	ldrh	r7, [fp, r3]
	mov	r1, r7
	bl	__aeabi_uidiv
	ubfx	r9, r0, #5, #16
	mov	r10, r0
	add	r3, r9, #36
	strh	r3, [r6, #-12]	@ movhi
	mov	r3, #24
	str	r0, [fp, #1932]
	mul	r3, r3, r7
	ldr	r2, [sp, #4]
	cmp	r8, r3
	ble	.L3275
	rsb	r0, r8, r2
	mov	r1, r7
	bl	__aeabi_uidiv
	str	r0, [fp, #1932]
	mov	r0, r0, lsr #5
	add	r0, r0, #24
	strh	r0, [r6, #-12]	@ movhi
.L3275:
	ldr	r3, [r5, #1796]
	cmp	r3, #1
	bne	.L3276
	mov	r0, r8
	mov	r1, r7
	bl	__aeabi_uidiv
	ldrh	fp, [r6, #-12]
	uxtah	r0, fp, r0
	add	fp, fp, r0, asr #2
	strh	fp, [r6, #-12]	@ movhi
.L3276:
	ldrb	r3, [r5, #140]	@ zero_extendqisi2
	cmp	r3, #0
	beq	.L3277
	mov	r0, r8
	mov	r1, r7
	bl	__aeabi_uidiv
	ldrh	fp, [r6, #-12]
	uxtah	r0, fp, r0
	add	fp, fp, r0, asr #2
	strh	fp, [r6, #-12]	@ movhi
.L3277:
	movw	r3, #1866
	ldrh	r3, [r5, r3]
	cmp	r3, #0
	beq	.L3279
	ldrh	r2, [r6, #-12]
	add	r2, r2, r3, lsr #1
	strh	r2, [r6, #-12]	@ movhi
	mul	r2, r7, r3
	cmp	r2, r8
	addgt	r3, r3, #32
	ldrgt	r2, .L3296
	addgt	r3, r9, r3
	strgt	r10, [r2, #1932]
	ldrgt	r2, .L3296+24
	strgth	r3, [r2, #-12]	@ movhi
.L3279:
	ldrh	r2, [r6, #-12]
	mvn	r9, #0
	ldr	r3, [r5, #1932]
	rsb	r3, r2, r3
	mul	r7, r7, r3
	ldr	r3, .L3296+28
	ldrh	r3, [r3]
	str	r7, [r4, #248]
	mul	r7, r3, r7
	movw	r3, #1878
	ldrh	r3, [r5, r3]
	str	r7, [r5, #1932]
	mul	r7, r3, r7
	str	r7, [r5, #1912]
	bl	FtlBbmTblFlush
	movw	r3, #1814
	ldr	r7, .L3296+32
	ldrh	r2, [r5, r3]
	mov	r1, #0
	sub	r8, r7, #248
	ldr	r0, [r4, #-4060]
	mov	r2, r2, asl #1
	mov	fp, r8
	bl	ftl_memset
	mov	r3, #0
	strh	r3, [r7, #-6]	@ movhi
	mov	r1, #255
	strh	r3, [r7, #-248]	@ movhi
	str	r3, [r4, #-3888]
	strb	r3, [r4, #-3794]
	strb	r3, [r4, #-3792]
	strh	r3, [r8, #2]	@ movhi
	strb	r3, [r4, #-4034]
	mov	r3, #1
	strb	r3, [r4, #-4032]
	movw	r3, #1812
	ldrh	r2, [r5, r3]
	ldr	r0, [r4, #-3884]
	strh	r9, [r7, #-8]	@ movhi
	add	r7, r7, #3792
	mov	r2, r2, lsr #3
	bl	ftl_memset
.L3281:
	ldr	r10, .L3296+36
	ldr	r5, .L3296+4
	mov	r0, r10
	bl	make_superblock
	ldrb	r3, [r4, #-4033]	@ zero_extendqisi2
	cmp	r3, #0
	ldrh	r3, [r8]
	bne	.L3282
	ldr	r2, [r7, #-4060]
	mov	r3, r3, asl #1
	strh	r9, [r2, r3]	@ movhi
	ldrh	r3, [fp]
	add	r3, r3, #1
	strh	r3, [fp]	@ movhi
	b	.L3281
.L3282:
	ldr	r2, [r5, #-3848]
	mov	r3, r3, asl #1
	ldrh	r1, [r10, #4]
	mov	r9, r5
	str	r2, [r5, #-4028]
	add	r2, r2, #1
	str	r2, [r5, #-3848]
	ldr	r2, [r5, #-4060]
	strh	r1, [r2, r3]	@ movhi
	sub	r2, r5, #3984
	sub	r7, r2, #8
	mov	r3, #0
	strb	r3, [r5, #-3986]
	strh	r3, [r7, #2]	@ movhi
	ldrh	r3, [r10]
	mvn	r10, #0
	add	r3, r3, #1
	strh	r3, [r2, #-8]	@ movhi
	mov	r3, #1
	strb	r3, [r5, #-3984]
.L3283:
	ldr	r8, .L3296+40
	ldr	r5, .L3296+4
	mov	r0, r8
	bl	make_superblock
	ldrb	r3, [r4, #-3985]	@ zero_extendqisi2
	cmp	r3, #0
	ldrh	r3, [r7]
	bne	.L3284
	ldr	r2, [r9, #-4060]
	mov	r3, r3, asl #1
	strh	r10, [r2, r3]	@ movhi
	ldrh	r3, [r7]
	add	r3, r3, #1
	strh	r3, [r7]	@ movhi
	b	.L3283
.L3284:
	ldr	r2, [r5, #-3848]
	mov	r3, r3, asl #1
	ldrh	r1, [r8, #4]
	mvn	r4, #0
	str	r2, [r5, #-3980]
	add	r2, r2, #1
	str	r2, [r5, #-3848]
	ldr	r2, [r5, #-4060]
	strh	r1, [r2, r3]	@ movhi
	sub	r3, r5, #3936
	strh	r4, [r3, #-8]	@ movhi
	bl	FtlFreeSysBlkQueueOut
	ldr	r3, .L3296+44
	mov	r2, #0
	strh	r2, [r3, #2]	@ movhi
	ldr	r2, [r5, #248]
	strh	r4, [r3, #4]	@ movhi
	strh	r2, [r3, #6]	@ movhi
	ldr	r3, [r5, #-3848]
	str	r3, [r5, #-3808]
	add	r3, r3, #1
	str	r3, [r5, #-3848]
	strh	r0, [r6, #-8]	@ movhi
	bl	FtlVpcTblFlush
	bl	FtlSysBlkInit
	cmp	r0, #0
	ldreq	r3, .L3296+48
	moveq	r2, #1
	streq	r2, [r3, #504]
.L3263:
	mov	r0, #0
	add	sp, sp, #12
	@ sp needed
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L3297:
	.align	2
.L3296:
	.word	.LANCHOR0
	.word	.LANCHOR2
	.word	.LANCHOR0+1878
	.word	168778952
	.word	.LANCHOR0+1814
	.word	.LANCHOR0+1812
	.word	.LANCHOR2-3808
	.word	.LANCHOR0+1872
	.word	.LANCHOR2-3792
	.word	.LANCHOR2-4040
	.word	.LANCHOR2-3992
	.word	.LANCHOR2-3816
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
	ldr	r5, .L3328
	ldr	r0, .L3328+4
	ldr	r3, [r5]
	str	r3, [sp, #20]
	ldrb	r3, [r0, #140]	@ zero_extendqisi2
	cmp	r3, #0
	beq	.L3299
.L3301:
	mov	r0, #0
	b	.L3300
.L3299:
	ldr	r6, .L3328+8
	mov	r4, r0
	ldr	r0, [r6, #144]
	bl	FlashInit
	cmp	r0, #0
	bne	.L3301
	bl	FlashLoadFactorBbt
	cmp	r0, #0
	beq	.L3302
	bl	FlashMakeFactorBbt
.L3302:
	ldr	r0, [r6, #176]
	bl	FlashReadIdbDataRaw
	cmp	r0, #0
	beq	.L3303
	mov	r2, #16
	add	r0, sp, #4
	mov	r1, #0
	bl	FlashReadFacBbtData
	mov	r3, #0
	mov	r0, r3
	mov	ip, #1
	ldr	r2, [sp, #4]
.L3304:
	ands	lr, r2, ip, asl r0
	add	r1, r3, #1
	add	r0, r0, #1
	movne	r3, r1
	cmp	r0, #16
	bne	.L3304
	cmp	r3, #6
	bls	.L3324
	mov	r0, #0
	mov	ip, #1
.L3307:
	ands	lr, r2, ip, asl r0
	add	r1, r3, #1
	add	r0, r0, #1
	movne	r3, r1
	cmp	r0, #24
	bne	.L3307
	cmp	r3, #17
	movhi	r3, #36
	strhib	r3, [r4, #1]
	bhi	.L3306
.L3324:
	strb	r0, [r4, #1]
.L3306:
	ldrb	r3, [r4, #1]	@ zero_extendqisi2
	strh	r3, [r4, #138]	@ movhi
.L3303:
	ldr	r1, .L3328+12
	ldr	r0, .L3328+16
	bl	printk
	ldr	r0, .L3328+20
	bl	FtlConstantsInit
	bl	FtlVariablesInit
	ldr	r0, [r4, #1808]
	mov	r4, #1
	uxth	r0, r0
	bl	FtlFreeSysBlkQueueInit
.L3309:
	bl	FtlLoadBbt
	cmp	r0, #0
	beq	.L3310
.L3326:
	bl	FtlLowFormat
	cmp	r4, #3
	addls	r4, r4, #1
	bls	.L3309
.L3327:
	mvn	r0, #0
	b	.L3300
.L3310:
	bl	FtlSysBlkInit
	cmp	r0, #0
	bne	.L3326
	ldr	r3, .L3328+24
	mov	r2, #1
	str	r2, [r3, #504]
.L3300:
	ldr	r2, [sp, #20]
	ldr	r3, [r5]
	cmp	r2, r3
	beq	.L3314
	bl	__stack_chk_fail
.L3314:
	add	sp, sp, #24
	@ sp needed
	ldmfd	sp!, {r4, r5, r6, pc}
.L3329:
	.align	2
.L3328:
	.word	__stack_chk_guard
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
	ldr	r3, .L3349
	ldr	r2, [r3, #4052]
	cmp	r2, #0
	bne	.L3347
	stmfd	sp!, {r4, r5, r6, r7, r8, lr}
	.save {r4, r5, r6, r7, r8, lr}
	ldrb	r3, [r3, #140]	@ zero_extendqisi2
	ldr	r5, .L3349+4
	cmp	r3, #0
	beq	.L3333
	ldr	r3, [r5, #-3220]
	tst	r3, #1
	beq	.L3333
	ldr	r3, .L3349+8
	ldrh	r3, [r3, #4]
	cmp	r3, #0
	beq	.L3333
.L3334:
	mov	r0, #0
	ldmfd	sp!, {r4, r5, r6, r7, r8, pc}
.L3333:
	mov	r2, #0
	ldr	r0, [r5, #-2072]
	ldr	r1, [r5, #-3220]
	mov	r3, r2
	bl	FlashProgPages
	ldr	r7, .L3349+4
	mov	r6, #0
	mov	r8, #36
.L3335:
	ldr	r1, [r5, #-3220]
	uxth	r3, r6
	ldr	r4, .L3349+4
	cmp	r3, r1
	bcs	.L3348
	mul	r3, r8, r3
	ldr	r0, [r7, #-2072]
	add	r6, r6, #1
	add	r1, r0, r3
	ldr	r0, [r0, r3]
	ldr	r2, [r1, #12]
	cmn	r0, #1
	bne	.L3336
	sub	r1, r4, #3936
	ldr	ip, [r4, #-4060]
	mov	lr, #0
	ldrh	r2, [r1, #-8]
	mov	r2, r2, asl #1
	strh	lr, [ip, r2]	@ movhi
	ldr	r2, [r4, #-3652]
	strh	r0, [r1, #-8]	@ movhi
	add	r2, r2, #1
	str	r2, [r4, #-3652]
	ldr	r2, [r4, #-2072]
	add	r3, r2, r3
	ldr	r0, [r3, #4]
	ubfx	r0, r0, #10, #16
	bl	FtlBbmMapBadBlock
	bl	FtlBbmTblFlush
	bl	FtlGcPageVarInit
	b	.L3346
.L3336:
	ldr	r0, [r2, #12]
	ldr	r1, [r1, #4]
	ldr	r2, [r2, #8]
	bl	FtlGcUpdatePage
	b	.L3335
.L3348:
	ldr	r0, [r4, #-2072]
	bl	FtlGcBufFree
	mov	r3, #0
	str	r3, [r4, #-3220]
	ldr	r3, .L3349+8
	ldrh	r3, [r3, #4]
	cmp	r3, #0
	bne	.L3334
	mov	r0, #1
	bl	FtlGcFreeTempBlock
.L3346:
	mov	r0, #1
	ldmfd	sp!, {r4, r5, r6, r7, r8, pc}
.L3347:
	mov	r0, #0
	bx	lr
.L3350:
	.align	2
.L3349:
	.word	.LANCHOR0
	.word	.LANCHOR2
	.word	.LANCHOR2-3944
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
	ldr	r3, .L3355
	sub	r2, r3, #8
	ldrh	r1, [r3, #-8]
	movw	r3, #65535
	cmp	r1, r3
	beq	.L3352
	ldrh	r3, [r2, #4]
	cmp	r3, #0
	bne	.L3353
.L3352:
	ldr	r4, .L3355+4
	bl	FtlCacheWriteBack
	mov	r0, #0
	mov	r5, #0
	bl	FtlGcFreeTempBlock
	sub	r4, r4, #3184
	strb	r5, [r4, #-752]
	ldr	r0, .L3355+8
	bl	allocate_data_superblock
	strh	r5, [r4, #-8]	@ movhi
	strh	r5, [r4]	@ movhi
	bl	l2p_flush
	mov	r0, r5
	bl	FtlEctTblFlush
	bl	FtlVpcTblFlush
.L3353:
	ldr	r0, .L3355+8
	ldmfd	sp!, {r3, r4, r5, lr}
	b	get_new_active_ppa
.L3356:
	.align	2
.L3355:
	.word	.LANCHOR2-3936
	.word	.LANCHOR2
	.word	.LANCHOR2-3944
	.fnend
	.size	Ftl_get_new_temp_ppa, .-Ftl_get_new_temp_ppa
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
	ldr	r5, .L3521
	ldr	r3, .L3521+4
	str	r0, [sp, #12]
	ldr	r0, [r5, #4052]
	ldr	r3, [r3]
	cmp	r0, #0
	str	r1, [sp, #8]
	str	r3, [sp, #44]
	movne	r0, #0
	bne	.L3358
	ldr	r2, .L3521+8
	ldr	r3, [r2, #504]
	cmn	r3, #1
	beq	.L3358
	ldr	r3, .L3521+12
	ldr	r4, [r3, #-2100]
	mov	r7, r3
	cmp	r4, #0
	bne	.L3358
	sub	r1, r3, #4048
	ldrh	r1, [r1, #-4]
	cmp	r1, #47
	movls	r0, r4
	bls	.L3358
	movw	r1, #3288
	ldrh	r1, [r2, r1]
	movw	r2, #65535
	cmp	r1, r2
	bne	.L3359
.L3362:
	ldr	r6, .L3521+16
	movw	r2, #65535
	ldrh	r4, [r6, #-12]
	cmp	r4, r2
	bne	.L3360
	b	.L3361
.L3359:
	sub	r3, r3, #3936
	ldrh	r3, [r3, #-8]
	cmp	r3, r2
	beq	.L3362
	mov	r0, #1
	bl	FtlGcFreeTempBlock
	cmp	r0, #0
	beq	.L3362
	mov	r0, #1
	b	.L3358
.L3360:
	ldrh	r3, [r6, #-14]
	cmp	r3, r2
	bne	.L3361
	ldrh	r1, [r6, #-10]
	cmp	r1, r3
	beq	.L3361
	ldrh	r2, [r6, #-8]
	cmp	r2, r3
	strneh	r4, [r6, #-14]	@ movhi
	strneh	r1, [r6, #-12]	@ movhi
	mvnne	r3, #0
	strneh	r2, [r6, #-10]	@ movhi
	strneh	r3, [r6, #-8]	@ movhi
.L3361:
	ldr	r2, [sp, #12]
	ldr	r3, [r7, #-3232]
	ldr	r8, .L3521+20
	cmp	r2, #1
	add	r3, r3, #1
	add	r3, r3, r2, asl #7
	sub	r10, r8, #240
	str	r3, [r7, #-3232]
	bne	.L3373
	ldr	r2, [r5, #1796]
	cmp	r2, #0
	bne	.L3364
	ldr	r2, .L3521
	ldrb	r2, [r2, #140]	@ zero_extendqisi2
	cmp	r2, #0
	beq	.L3373
.L3364:
	ldr	r2, [r7, #-3824]
	ldr	r9, .L3521+12
	cmp	r2, #39
	bhi	.L3373
	ldr	r4, .L3521+24
	ldrh	r2, [r4]
	add	r3, r3, r2
	str	r3, [r9, #-3232]
	bl	FtlGcReFreshBadBlk
	ldrh	r3, [r8, #-8]
	movw	r2, #65535
	cmp	r3, r2
	bne	.L3373
	ldrh	r2, [r6, #-14]
	cmp	r2, r3
	bne	.L3373
	ldr	r3, [r9, #-3232]
	cmp	r3, #1024
	bhi	.L3365
	ldrh	r3, [r10, #-12]
	cmp	r3, #63
	bhi	.L3373
.L3365:
	ldr	r3, .L3521+12
	mov	r0, #0
	ldrh	ip, [r10, #-12]
	sub	r2, r3, #3232
	ldr	r1, .L3521+24
	strh	r0, [r4]	@ movhi
	ldrh	r2, [r2, #-2]
	add	r2, r2, #64
	cmp	ip, r2
	bgt	.L3373
	str	r0, [r3, #-3232]
	ldr	r3, [r3, #-3824]
	cmp	r3, r0
	moveq	r3, #6
	beq	.L3513
	cmp	r3, #5
	bhi	.L3367
	mov	r3, #18
.L3513:
	strh	r3, [r1]	@ movhi
.L3367:
	mov	r0, #32
	bl	List_get_gc_head_node
	movw	r3, #65535
	uxth	lr, r0
	cmp	lr, r3
	beq	.L3372
	ldr	fp, .L3521+12
	sub	r9, fp, #3216
	ldrh	r0, [r9, #-12]
	cmp	r0, #0
	beq	.L3369
	movw	r1, #1874
	ldr	r2, [fp, #-4060]
	ldrh	r4, [r5, r1]
	movw	r1, #1804
	ldrh	r1, [r5, r1]
	mov	ip, lr, asl #1
	ldrh	lr, [r2, ip]
	mul	r1, r1, r4
	add	r1, r1, #1
	cmp	lr, r1
	bgt	.L3372
	add	r1, r0, #1
	str	ip, [sp, #28]
	str	r3, [sp, #24]
	mov	r3, #0
	uxth	r1, r1
	str	r2, [sp, #20]
	str	r3, [fp, #-3224]
	strh	r1, [r9, #-12]	@ movhi
	str	r1, [sp, #16]
	bl	List_get_gc_head_node
	ldr	r3, [sp, #24]
	uxth	r4, r0
	ldr	r1, [sp, #16]
	cmp	r4, r3
	ldr	r2, [sp, #20]
	ldr	ip, [sp, #28]
	beq	.L3372
	mov	r10, r4, asl #1
	ldr	r0, .L3521+28
	ldrh	r3, [r2, r10]
	ldrh	r2, [r2, ip]
	str	r2, [sp]
	mov	r2, r4
	bl	printk
	ldrh	r3, [r9, #-12]
	cmp	r3, #40
	bls	.L3370
	ldr	r3, [fp, #-4060]
	ldrh	r3, [r3, r10]
	cmp	r3, #32
	movhi	r3, #0
	strhih	r3, [r9, #-12]	@ movhi
.L3370:
	ldr	r3, .L3521+24
	mov	r2, #6
	strh	r2, [r3]	@ movhi
	b	.L3374
.L3369:
	mov	r3, #1
	strh	r3, [r9, #-12]	@ movhi
.L3372:
	bl	GetSwlReplaceBlock
	movw	r3, #65535
	cmp	r0, r3
	mov	r4, r0
	bne	.L3374
	ldr	r3, .L3521+24
	mov	r2, #0
	strh	r2, [r3]	@ movhi
.L3373:
	ldrh	r3, [r8, #-8]
	movw	r4, #65535
	ldr	r9, .L3521+12
	cmp	r3, r4
	bne	.L3374
	sub	r2, r9, #3936
	ldrh	r4, [r2, #-8]
	cmp	r4, r3
	movne	r4, r3
	beq	.L3518
.L3374:
	movw	r1, #65535
	rsb	r3, r1, r4
	clz	r3, r3
	ldr	r2, [sp, #12]
	ldr	r9, .L3521+32
	mov	r3, r3, lsr #5
	cmp	r2, #0
	movne	ip, #0
	andeq	ip, r3, #1
	cmp	ip, #0
	beq	.L3387
	ldrh	r0, [r9, #-12]
	cmp	r0, #24
	movhi	r10, #1
	bhi	.L3388
	ldr	r3, .L3521+36
	cmp	r0, #16
	ldrh	r10, [r3]
	movhi	r10, r10, lsr #5
	bhi	.L3388
	cmp	r0, #12
	movhi	r10, r10, lsr #4
	bhi	.L3388
	cmp	r0, #8
	movhi	r10, r10, lsr #2
.L3388:
	ldr	r1, .L3521+12
	sub	r2, r1, #3232
	ldrh	r3, [r2, #-4]
	cmp	r3, r0
	bcs	.L3392
	sub	r3, r1, #3936
	movw	r0, #65535
	ldrh	r3, [r3, #-8]
	cmp	r3, r0
	bne	.L3393
	sub	r0, r1, #3168
	ldrh	r0, [r0, #-14]
	cmp	r0, r3
	bne	.L3393
	ldr	r3, .L3521+24
	ldrh	r0, [r3]
	cmp	r0, #0
	bne	.L3394
	ldr	r3, [r5, #1932]
	ldr	r1, [r1, #-3888]
	add	r3, r3, r3, asl #1
	cmp	r1, r3, lsr #2
	movcs	r3, #18
	bcs	.L3395
.L3394:
	ldr	r3, .L3521+40
	ldrh	r3, [r3, #-12]
	add	r3, r3, r3, asl #1
	ubfx	r3, r3, #2, #16
.L3395:
	strh	r3, [r2, #-4]	@ movhi
	mov	r3, #0
	str	r3, [r7, #-3224]
	b	.L3358
.L3393:
	ldr	r3, .L3521+40
	ldrh	r3, [r3, #-12]
	add	r3, r3, r3, asl #1
	mov	r3, r3, asr #2
	strh	r3, [r2, #-4]	@ movhi
.L3392:
	ldr	r3, [sp, #8]
	movw	r4, #65535
	ldr	r2, [r5, #1796]
	cmp	r3, #2
	movhi	r3, #0
	movls	r3, #1
	cmp	r2, #0
	moveq	r3, #0
	cmp	r3, #0
	addne	r10, r10, #1
	uxthne	r10, r10
	b	.L3397
.L3387:
	ldrh	r0, [r9, #88]
	add	r2, r9, #4032
	cmp	r0, r1
	bne	.L3398
	ldrh	r1, [r6, #-14]
	cmp	r1, r0
	movne	r3, #0
	andeq	r3, r3, #1
	cmp	r3, #0
	beq	.L3398
	sub	r3, r2, #3792
	ldrh	r3, [r3, #-8]
	cmp	r3, r0
	beq	.L3399
.L3403:
	movw	r4, #65535
	b	.L3398
.L3399:
	sub	r4, r2, #3232
	ldrh	r1, [r9, #-12]
	str	ip, [r2, #-3224]
	ldrh	r3, [r4, #-4]
	ldr	r10, .L3521+24
	cmp	r1, r3
	bls	.L3400
	ldrh	r3, [r10]
	cmp	r3, #0
	bne	.L3401
	ldr	r3, [r5, #1932]
	ldr	r2, [r2, #-3888]
	add	r3, r3, r3, asl #1
	cmp	r2, r3, lsr #2
	movcs	r3, #18
	bcs	.L3402
.L3401:
	ldr	r3, .L3521+40
	ldrh	r3, [r3, #-12]
	add	r3, r3, r3, asl #1
	ubfx	r3, r3, #2, #16
.L3402:
	strh	r3, [r4, #-4]	@ movhi
	bl	FtlReadRefresh
	mov	r0, #0
	bl	List_get_gc_head_node
	ldr	r3, [r7, #-4060]
	uxth	r0, r0
	mov	r0, r0, asl #1
	ldrh	r3, [r3, r0]
	cmp	r3, #4
	ldrhih	r0, [r10]
	bhi	.L3358
.L3400:
	ldrh	r0, [r10]
	cmp	r0, #0
	bne	.L3403
	ldr	r3, .L3521+40
	ldrh	r10, [r3, #-12]
	add	r3, r10, r10, asl #1
	mov	r3, r3, asr #2
	strh	r3, [r4, #-4]	@ movhi
	bl	List_get_gc_head_node
	ldr	r3, [r7, #-4060]
	uxth	r0, r0
	mov	r0, r0, asl #1
	ldrh	r1, [r3, r0]
	movw	r3, #1874
	ldrh	r2, [r5, r3]
	movw	r3, #1804
	ldrh	r3, [r5, r3]
	mul	r3, r3, r2
	add	r3, r3, r3, lsr #31
	cmp	r1, r3, asr #1
	ble	.L3404
	ldrh	r3, [r9, #-12]
	sub	r2, r10, #1
	cmp	r3, r2
	blt	.L3404
	bl	FtlReadRefresh
	b	.L3516
.L3404:
	cmp	r1, #0
	bne	.L3403
	movw	r0, #65535
	bl	decrement_vpc_count
	ldrh	r0, [r9, #-12]
	add	r0, r0, #1
	b	.L3358
.L3398:
	ldr	r3, [r5, #1796]
	cmp	r3, #0
	moveq	r10, #1
	movne	r10, #2
.L3397:
	ldrh	r3, [r8, #-8]
	movw	r0, #65535
	ldr	r2, .L3521+12
	cmp	r3, r0
	sub	r1, r2, #3792
	bne	.L3406
	cmp	r4, r3
	strneh	r4, [r1, #-8]	@ movhi
	bne	.L3408
	sub	r1, r2, #3168
	ldrh	r3, [r1, #-14]
	cmp	r3, r4
	beq	.L3408
	ldr	r2, [r2, #-4060]
	mov	r3, r3, asl #1
	ldrh	r3, [r2, r3]
	cmp	r3, #0
	mvneq	r3, #0
	streqh	r3, [r1, #-14]	@ movhi
	ldrh	r3, [r6, #-14]
	strh	r3, [r8, #-8]	@ movhi
	mvn	r3, #0
	strh	r3, [r6, #-14]	@ movhi
.L3408:
	ldrh	r0, [r8, #-8]
	mov	r3, #0
	strb	r3, [r7, #-3792]
	movw	r3, #65535
	cmp	r0, r3
	beq	.L3406
	bl	IsBlkInGcList
	cmp	r0, #0
	ldrne	r3, .L3521+20
	mvnne	r2, #0
	strneh	r2, [r3, #-8]	@ movhi
	ldrb	r3, [r5, #140]	@ zero_extendqisi2
	cmp	r3, #0
	beq	.L3412
	ldrh	r0, [r8, #-8]
	bl	ftl_get_blk_mode
	strb	r0, [r7, #-3792]
.L3412:
	ldrh	r2, [r8, #-8]
	movw	r3, #65535
	ldr	r6, .L3521+12
	cmp	r2, r3
	sub	r7, r6, #3792
	sub	r5, r7, #8
	beq	.L3406
	mov	r0, r5
	bl	make_superblock
	ldr	r3, .L3521+44
	movw	r1, #446
	mov	r2, #0
	strh	r2, [r7, #-6]	@ movhi
	strh	r2, [r3, r1]	@ movhi
	add	r3, r3, #448
	strb	r2, [r6, #-3794]
	ldrh	r2, [r7, #-8]
	ldr	r1, [r6, #-4060]
	mov	r2, r2, asl #1
	ldrh	r2, [r1, r2]
	strh	r2, [r3]	@ movhi
.L3406:
	ldrh	r3, [r8, #-8]
	ldrh	r2, [r9, #-8]
	cmp	r2, r3
	beq	.L3413
	ldr	r2, .L3521+48
	ldrh	r1, [r2, #-8]
	cmp	r1, r3
	beq	.L3413
	ldrh	r2, [r2, #40]
	cmp	r2, r3
	bne	.L3447
.L3413:
	ldr	r3, .L3521+20
	mvn	r2, #0
	strh	r2, [r3, #-8]	@ movhi
.L3447:
	ldr	r7, .L3521+52
	movw	r3, #65535
	ldr	r6, .L3521+12
	ldrh	r2, [r7]
	mov	fp, r7
	mov	r8, r6
	cmp	r2, r3
	bne	.L3415
	mov	r3, #0
	str	r3, [r6, #-3224]
.L3416:
	ldr	r9, .L3521+56
	ldrh	r5, [r9]
	mov	r0, r5
	bl	List_get_gc_head_node
	movw	r2, #65535
	uxth	r3, r0
	strh	r3, [fp]	@ movhi
	cmp	r3, r2
	moveq	r3, #0
	moveq	r0, #8
	streqh	r3, [r9]	@ movhi
	beq	.L3358
.L3417:
	mov	r0, r3
	str	r3, [sp, #8]
	bl	IsBlkInGcList
	add	r5, r5, #1
	cmp	r0, #0
	ldr	r3, [sp, #8]
	ldrne	r3, .L3521+56
	strneh	r5, [r3]	@ movhi
	bne	.L3416
	ldr	r2, .L3521+56
	uxth	r5, r5
	ldr	r0, [r8, #-4060]
	mov	r1, r3, asl #1
	strh	r5, [r2]	@ movhi
	ldr	r2, .L3521+36
	ldrh	ip, [r0, r1]
	ldrh	lr, [r2]
	ldrh	r2, [r2, #-68]
	mul	r2, r2, lr
	add	lr, r2, r2, lsr #31
	cmp	ip, lr, asr #1
	bgt	.L3420
	cmp	ip, #8
	cmphi	r5, #48
	bls	.L3421
	ldrh	ip, [r9, #36]
	cmp	ip, #35
	bhi	.L3421
.L3420:
	ldr	lr, .L3521+56
	mov	ip, #0
	strh	ip, [lr]	@ movhi
.L3421:
	ldrh	r1, [r0, r1]
	movw	r0, #65535
	cmp	r1, r2
	cmpge	r4, r0
	bne	.L3422
	ldr	r2, .L3521+56
	ldrh	r0, [r2]
	cmp	r0, #3
	bhi	.L3422
	sub	r3, r2, #564
	mvn	r1, #0
	strh	r1, [r3, #-8]	@ movhi
	mov	r3, #0
	strh	r3, [r2]	@ movhi
	b	.L3516
.L3422:
	cmp	r1, #0
	bne	.L3423
	movw	r0, #65535
	bl	decrement_vpc_count
	ldr	r3, .L3521+56
	ldr	r2, .L3521+56
	ldrh	r3, [r3]
	add	r3, r3, #1
	strh	r3, [r2]	@ movhi
	b	.L3416
.L3423:
	mov	r2, #0
	strb	r2, [r6, #-3792]
	ldr	r2, .L3521
	ldrb	r2, [r2, #140]	@ zero_extendqisi2
	cmp	r2, #0
	beq	.L3424
	mov	r0, r3
	bl	ftl_get_blk_mode
	ldr	r3, .L3521+12
	strb	r0, [r3, #-3792]
.L3424:
	ldr	r0, .L3521+52
	bl	make_superblock
	ldrh	r2, [r7]
	ldr	r1, .L3521+60
	mov	r3, #0
	ldr	r0, [r6, #-4060]
	mov	r2, r2, asl #1
	strh	r3, [r1]	@ movhi
	ldrh	r2, [r0, r2]
	strh	r3, [r7, #2]	@ movhi
	strb	r3, [r6, #-3794]
	strh	r2, [r1, #2]	@ movhi
.L3415:
	ldr	r3, [sp, #12]
	cmp	r3, #1
	bne	.L3425
	bl	FtlReadRefresh
.L3425:
	mov	r3, #1
	str	r3, [r6, #-2100]
	ldr	r3, .L3521
	add	r2, r3, #1872
	ldrb	r3, [r3, #140]	@ zero_extendqisi2
	ldrh	r2, [r2]
	cmp	r3, #0
	str	r2, [sp, #8]
	beq	.L3426
	ldr	r3, .L3521+12
	ldrb	r3, [r3, #-3792]	@ zero_extendqisi2
	cmp	r3, #1
	ldreq	r3, .L3521+64
	ldreqh	r3, [r3]
	streq	r3, [sp, #8]
.L3426:
	ldrh	r3, [r7, #2]
	mov	r7, #0
	ldr	r1, [sp, #8]
	add	r2, r3, r10
	ldr	r5, .L3521+12
	cmp	r2, r1
	movgt	r2, r1
	rsbgt	r3, r3, r2
	uxthgt	r10, r3
.L3428:
	uxth	r3, r7
	ldr	r6, .L3521+12
	cmp	r3, r10
	ldr	r8, .L3521+52
	bcs	.L3435
	ldr	r3, .L3521+68
	movw	lr, #65535
	ldr	r8, [r5, #-3204]
	mov	ip, #36
	ldrh	r9, [r3]
	ldr	r3, .L3521+52
	add	r0, r3, #14
	ldrh	r1, [r3, #2]
	mov	r3, #0
	mov	r6, r3
	add	r1, r1, r7
.L3436:
	uxth	r2, r3
	cmp	r2, r9
	bcs	.L3519
	ldrh	r2, [r0, #2]!
	add	r3, r3, #1
	cmp	r2, lr
	orrne	r2, r1, r2, asl #10
	mlane	fp, ip, r6, r8
	addne	r6, r6, #1
	uxthne	r6, r6
	strne	r2, [fp, #4]
	b	.L3436
.L3519:
	ldr	r0, [r5, #-3204]
	mov	r1, r6
	ldrb	r2, [r5, #-3792]	@ zero_extendqisi2
	mov	r9, #0
	bl	FlashReadPages
.L3431:
	uxth	r3, r9
	cmp	r3, r6
	bcs	.L3520
	mov	r3, #36
	ldr	r2, [r5, #-3204]
	mul	r8, r3, r9
	add	r1, r2, r8
	ldr	r2, [r2, r8]
	ldr	fp, [r1, #12]
	cmn	r2, #1
	beq	.L3468
	ldrh	r1, [fp]
	movw	r2, #61589
	cmp	r1, r2
	bne	.L3468
	add	r1, sp, r3
	mov	r2, #0
	ldr	r0, [fp, #8]
	str	r3, [sp, #16]
	bl	log2phys
	ldr	r2, [r5, #-3204]
	add	r2, r2, r8
	ldr	r0, [r2, #4]
	ldr	r1, [sp, #36]
	ldr	r3, [sp, #16]
	bic	r1, r1, #-2147483648
	cmp	r1, r0
	bne	.L3468
	ldr	r1, .L3521+60
	ldr	r0, .L3521+60
	ldr	r2, [r2, #16]
	ldrh	r1, [r1]
	str	r3, [sp, #20]
	add	r1, r1, #1
	strh	r1, [r0]	@ movhi
	ldr	r0, [r5, #-3220]
	ldr	r1, [r5, #-2072]
	mla	r1, r3, r0, r1
	str	r2, [r1, #16]
	str	r1, [sp, #16]
	bl	Ftl_get_new_temp_ppa
	ldr	r2, [r5, #-2072]
	ldr	r1, [sp, #16]
	ldr	r3, [sp, #20]
	str	r0, [r1, #4]
	ldr	r1, [r5, #-3220]
	mla	r3, r3, r1, r2
	ldr	r2, [r5, #-3204]
	add	r2, r2, r8
	ldr	r1, [r2, #8]
	str	r1, [r3, #8]
	mov	r1, #1
	ldr	r2, [r2, #12]
	str	r2, [r3, #12]
	ldr	r3, [sp, #36]
	str	r3, [fp, #12]
	ldr	r3, .L3521+72
	ldrh	r3, [r3]
	strh	r3, [fp, #2]	@ movhi
	ldr	r3, [r5, #-3844]
	ldr	r0, [r5, #-3204]
	str	r3, [fp, #4]
	add	r0, r0, r8
	ldr	r3, [r5, #-3220]
	add	r3, r3, #1
	str	r3, [r5, #-3220]
	bl	FtlGcBufAlloc
	ldr	r3, .L3521
	ldrb	r3, [r3, #140]	@ zero_extendqisi2
	cmp	r3, #0
	bne	.L3433
	ldrb	r3, [r5, #-3937]	@ zero_extendqisi2
	ldr	r2, [r5, #-3220]
	cmp	r2, r3
	beq	.L3433
	ldr	r3, .L3521+72
	ldrh	r3, [r3, #4]
	cmp	r3, #0
	bne	.L3468
.L3433:
	bl	Ftl_gc_temp_data_write_back
	cmp	r0, #0
	beq	.L3468
	ldr	r3, .L3521+12
	mov	r2, #0
	mvn	r1, #0
	sub	r3, r3, #3792
	str	r2, [r3, #1692]
	strh	r1, [r3, #-8]	@ movhi
	strh	r2, [r3, #-6]	@ movhi
	b	.L3516
.L3468:
	add	r9, r9, #1
	b	.L3431
.L3520:
	add	r7, r7, #1
	b	.L3428
.L3435:
	ldrh	r3, [r8, #2]
	add	r10, r10, r3
	ldr	r3, [sp, #8]
	uxth	r10, r10
	strh	r10, [r8, #2]	@ movhi
	cmp	r10, r3
	bcc	.L3437
	ldr	r3, [r6, #-3220]
	cmp	r3, #0
	beq	.L3438
	bl	Ftl_gc_temp_data_write_back
	cmp	r0, #0
	beq	.L3438
	mov	r3, #0
	str	r3, [r6, #-2100]
.L3516:
	ldr	r3, .L3521+24
	ldrh	r0, [r3]
	b	.L3358
.L3438:
	ldr	r3, .L3521+60
	ldrh	r5, [r3]
	cmp	r5, #0
	bne	.L3439
	ldrh	r3, [r8]
	ldr	r2, [r6, #-4060]
	mov	r3, r3, asl #1
	ldrh	r3, [r2, r3]
	cmp	r3, #0
	beq	.L3439
	ldr	r7, .L3521
.L3440:
	ldr	r3, [r7, #1932]
	cmp	r5, r3
	bcs	.L3445
	mov	r0, r5
	add	r1, sp, #40
	mov	r2, #0
	bl	log2phys
	ldr	r0, [sp, #40]
	cmn	r0, #1
	beq	.L3441
	ubfx	r0, r0, #10, #16
	bl	P2V_block_in_plane
	ldrh	r3, [r8]
	cmp	r3, r0
	bne	.L3441
.L3445:
	ldr	r3, .L3521
	ldr	r3, [r3, #1932]
	cmp	r5, r3
	bcc	.L3439
	ldrh	r3, [r8]
	mov	r1, #0
	ldr	r2, [r6, #-4060]
	mov	r3, r3, asl #1
	strh	r1, [r2, r3]	@ movhi
	ldrh	r0, [r8]
	bl	update_vpc_list
	bl	FtlCacheWriteBack
	bl	l2p_flush
	bl	FtlVpcTblFlush
	b	.L3439
.L3441:
	add	r5, r5, #1
	b	.L3440
.L3439:
	mvn	r3, #0
	strh	r3, [r8]	@ movhi
.L3437:
	ldr	r3, .L3521+76
	ldrh	r3, [r3]
	cmp	r3, #2
	ldrls	r3, .L3521+36
	ldrlsh	r10, [r3]
	bls	.L3447
.L3446:
	ldr	r2, .L3521+12
	mov	r1, #0
	str	r1, [r2, #-2100]
	ldr	r2, .L3521+24
	ldrh	r0, [r2]
	cmp	r0, r1
	addeq	r0, r3, #1
.L3358:
	ldr	r3, .L3521+4
	ldr	r2, [sp, #44]
	ldr	r3, [r3]
	cmp	r2, r3
	beq	.L3451
	bl	__stack_chk_fail
.L3518:
	sub	r3, r9, #3168
	ldrh	fp, [r3, #-14]
	cmp	fp, r4
	bne	.L3374
	sub	r3, r9, #4032
	ldr	r1, [r9, #-3232]
	ldrh	r2, [r3, #-12]
	cmp	r2, #24
	movcc	r2, #5120
	movcs	r2, #1024
	cmp	r1, r2
	movls	r4, fp
	bls	.L3374
	ldr	r1, .L3521+24
	mov	r2, #0
	str	r3, [sp, #16]
	str	r2, [r7, #-3232]
	strh	r2, [r1]	@ movhi
	bl	GetSwlReplaceBlock
	cmp	r0, fp
	mov	r4, r0
	sub	fp, r9, #3232
	ldr	r3, [sp, #16]
	bne	.L3376
	ldrh	r2, [r3, #-12]
	ldrh	r3, [fp, #-2]
	cmp	r2, r3
	bcs	.L3377
	mov	r0, #64
	bl	List_get_gc_head_node
	uxth	r3, r0
	cmp	r3, r4
	beq	.L3386
	ldr	r2, [r9, #-2104]
	ldr	r1, .L3521+80
	cmp	r2, #0
	bne	.L3379
	ldrh	r0, [r1]
	cmp	r0, #3
	beq	.L3379
	ldr	r0, [r9, #-3752]
	cmp	r0, #0
	bne	.L3379
	ldr	r0, [r1, #-28]
	cmp	r0, #0
	bne	.L3379
	ldrb	r0, [r1, #-1684]	@ zero_extendqisi2
	cmp	r0, #0
	beq	.L3380
.L3379:
	ldr	r2, [r7, #-4060]
	mov	r3, r3, asl #1
	ldrh	r1, [r1]
	ldrh	r0, [r2, r3]
	movw	r3, #1874
	movw	r2, #1804
	ldrh	r3, [r5, r3]
	ldrh	r2, [r5, r2]
	cmp	r1, #3
	mul	r2, r2, r3
	moveq	r3, r3, lsr #1
	movne	r3, #0
	add	r3, r2, r3
	cmp	r0, r3
	bgt	.L3382
	mov	r0, #0
	bl	List_get_gc_head_node
	ldr	r3, [r5, #1932]
	ldr	r2, [r7, #-3888]
	add	r3, r3, r3, asl #1
	cmp	r2, r3, lsr #2
	movls	r3, #160
	uxth	r4, r0
	bls	.L3514
	b	.L3517
.L3380:
	ldr	r2, [r9, #-4060]
	mov	r3, r3, asl #1
	ldrh	r3, [r2, r3]
	cmp	r3, #7
	bhi	.L3385
	bl	List_get_gc_head_node
	uxth	r4, r0
.L3517:
	mov	r3, #128
.L3514:
	strh	r3, [fp, #-2]	@ movhi
	movw	r3, #65535
	cmp	r4, r3
	beq	.L3386
	b	.L3376
.L3382:
	mov	r3, #128
	b	.L3515
.L3385:
	mov	r3, #64
	b	.L3515
.L3377:
	mov	r3, #80
.L3515:
	strh	r3, [fp, #-2]	@ movhi
	b	.L3386
.L3376:
	ldr	r0, [r5, #4060]
	mov	r1, r4, asl #1
	ldr	r3, [r7, #-4060]
	ldrh	r2, [r10, #-12]
	ldrh	r3, [r3, r1]
	ldrh	r1, [r0, r1]
	ldr	r0, .L3521+84
	str	r1, [sp]
	ldrh	r1, [fp, #-4]
	str	r1, [sp, #4]
	mov	r1, r4
	bl	printk
.L3386:
	bl	FtlGcReFreshBadBlk
	b	.L3374
.L3451:
	add	sp, sp, #52
	@ sp needed
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L3522:
	.align	2
.L3521:
	.word	.LANCHOR0
	.word	__stack_chk_guard
	.word	.LANCHOR1
	.word	.LANCHOR2
	.word	.LANCHOR2-3168
	.word	.LANCHOR2-3792
	.word	.LANCHOR4+444
	.word	.LC151
	.word	.LANCHOR2-4032
	.word	.LANCHOR0+1872
	.word	.LANCHOR2-3808
	.word	.LANCHOR4
	.word	.LANCHOR2-3984
	.word	.LANCHOR2-3800
	.word	.LANCHOR2-3228
	.word	.LANCHOR4+446
	.word	.LANCHOR0+1874
	.word	.LANCHOR0+1804
	.word	.LANCHOR2-3944
	.word	.LANCHOR2-4044
	.word	.LANCHOR0+1824
	.word	.LC152
	.fnend
	.size	ftl_do_gc, .-ftl_do_gc
	.align	2
	.global	FtlCacheWriteBack
	.type	FtlCacheWriteBack, %function
FtlCacheWriteBack:
	.fnstart
	@ args = 0, pretend = 0, frame = 16
	@ frame_needed = 0, uses_anonymous_args = 0
	ldr	r3, .L3568
	stmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.save {r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.pad #20
	sub	sp, sp, #20
	ldr	r2, [r3]
	ldr	r4, .L3568+4
	str	r3, [sp, #4]
	str	r2, [sp, #12]
	ldr	r2, .L3568+8
	ldr	r5, [r2, #452]
	ldr	r2, [r4, #4052]
	cmp	r2, #0
	bne	.L3525
	ldr	r1, [r4, #1924]
	cmp	r1, #0
	beq	.L3525
	ldrb	r6, [r4, #140]	@ zero_extendqisi2
	mov	r8, #0
	ldr	r0, [r4, #1928]
	cmp	r6, #0
	ldrb	r3, [r5, #9]	@ zero_extendqisi2
	ldr	r9, .L3568+4
	ldrneb	r6, [r5, #8]	@ zero_extendqisi2
	ldr	r10, .L3568+12
	subne	r6, r6, #1
	clzne	r6, r6
	movne	r6, r6, lsr #5
	mov	r2, r6
	bl	FlashProgPages
.L3528:
	ldr	r3, [r4, #1924]
	cmp	r8, r3
	bcs	.L3546
	mov	r7, #36
	ldr	r3, [r9, #1928]
	mul	r7, r7, r8
	add	r2, r3, r7
	ldr	r3, [r3, r7]
	cmn	r3, #1
	beq	.L3550
	ldr	r3, [r2, #4]
	cmp	r6, #0
	ldr	r0, [r2, #16]
	add	r1, sp, #8
	mov	r2, #1
	orrne	r3, r3, #-2147483648
	str	r3, [sp, #8]
	bl	log2phys
	ldr	r3, [r4, #1928]
	add	r7, r3, r7
	ldr	r3, [r7, #12]
	ldr	r0, [r3, #12]
	cmn	r0, #1
	beq	.L3532
	ubfx	r0, r0, #10, #16
	bl	P2V_block_in_plane
	ldr	r2, [r10, #-4060]
	mov	r3, r0, asl #1
	mov	r7, r0
	ldrh	r2, [r2, r3]
	cmp	r2, #0
	bne	.L3533
	ldr	r0, .L3568+16
	mov	r1, r7
	bl	printk
.L3533:
	mov	r0, r7
	bl	decrement_vpc_count
.L3532:
	add	r8, r8, #1
	b	.L3528
.L3566:
	ldr	r6, .L3568+20
	movw	r5, #16386
.L3545:
	ldrh	r3, [r6]
	cmp	r3, #0
	beq	.L3546
	mov	r0, #1
	mov	r1, r0
	bl	ftl_do_gc
	subs	r5, r5, #1
	bne	.L3545
.L3546:
	mov	r3, #0
	str	r3, [r4, #1924]
	b	.L3525
.L3550:
	ldr	r10, .L3568+4
	mov	r8, #0
	ldr	r9, .L3568+12
.L3529:
	ldr	r3, [r4, #1924]
	cmp	r8, r3
	bcs	.L3566
	mov	r7, #36
	ldr	r3, [r10, #1928]
	mul	r7, r7, r8
	ldr	fp, .L3568+24
	mvn	r2, #0
	str	r2, [r3, r7]
.L3535:
	ldr	r3, [r4, #1928]
	add	r2, r3, r7
	ldr	r3, [r3, r7]
	cmn	r3, #1
	bne	.L3567
	ldr	r0, [r2, #4]
	ubfx	r0, r0, #10, #16
	bl	P2V_block_in_plane
	ldrh	r3, [r5]
	cmp	r3, r0
	bne	.L3536
	ldr	r1, [r9, #-4060]
	mov	r3, r3, asl #1
	ldrh	r0, [r5, #4]
	ldrh	r2, [r1, r3]
	rsb	r2, r0, r2
	strh	r2, [r1, r3]	@ movhi
	ldrh	r3, [fp]
	strh	r3, [r5, #2]	@ movhi
	mov	r3, #0
	strb	r3, [r5, #6]
	strh	r3, [r5, #4]	@ movhi
.L3536:
	ldrh	r3, [r5, #4]
	cmp	r3, #0
	bne	.L3537
	mov	r0, r5
	bl	allocate_new_data_superblock
.L3537:
	ldr	r3, [r9, #-3652]
	add	r3, r3, #1
	str	r3, [r9, #-3652]
	ldr	r3, [r4, #1928]
	add	r3, r3, r7
	ldr	r0, [r3, #4]
	ubfx	r0, r0, #10, #16
	bl	FtlGcMarkBadPhyBlk
	mov	r0, r5
	bl	get_new_active_ppa
	ldr	r3, [r4, #1928]
	mov	r1, #1
	mov	r2, r6
	add	r3, r3, r7
	str	r0, [sp, #8]
	str	r0, [r3, #4]
	mov	r0, r3
	ldrb	r3, [r5, #9]	@ zero_extendqisi2
	bl	FlashProgPages
	ldr	r3, [r4, #1928]
	ldr	r3, [r3, r7]
	cmn	r3, #1
	moveq	r3, #1
	streq	r3, [r10, #4052]
	ldr	r3, [r4, #4052]
	cmp	r3, #0
	beq	.L3535
	b	.L3525
.L3567:
	ldr	r3, [r2, #4]
	cmp	r6, #0
	ldr	r0, [r2, #16]
	add	r1, sp, #8
	mov	r2, #1
	orrne	r3, r3, #-2147483648
	str	r3, [sp, #8]
	bl	log2phys
	ldr	r3, [r4, #1928]
	add	r7, r3, r7
	ldr	r3, [r7, #12]
	ldr	r0, [r3, #12]
	cmn	r0, #1
	beq	.L3542
	ubfx	r0, r0, #10, #16
	bl	P2V_block_in_plane
	ldr	r2, [r9, #-4060]
	mov	r3, r0, asl #1
	mov	r7, r0
	ldrh	r2, [r2, r3]
	cmp	r2, #0
	bne	.L3543
	ldr	r0, .L3568+16
	mov	r1, r7
	bl	printk
.L3543:
	mov	r0, r7
	bl	decrement_vpc_count
.L3542:
	add	r8, r8, #1
	b	.L3529
.L3525:
	ldr	r3, [sp, #4]
	mov	r0, #0
	ldr	r2, [sp, #12]
	ldr	r3, [r3]
	cmp	r2, r3
	beq	.L3548
	bl	__stack_chk_fail
.L3548:
	add	sp, sp, #20
	@ sp needed
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L3569:
	.align	2
.L3568:
	.word	__stack_chk_guard
	.word	.LANCHOR0
	.word	.LANCHOR4
	.word	.LANCHOR2
	.word	.LC153
	.word	.LANCHOR2-3174
	.word	.LANCHOR0+1872
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
	ldr	r3, .L3576
	ldr	r3, [r3, #4052]
	cmp	r3, #0
	bne	.L3571
	ldr	r3, .L3576+4
	ldr	r3, [r3, #504]
	cmn	r3, #1
	beq	.L3571
	bl	FtlCacheWriteBack
	bl	l2p_flush
	mov	r0, #1
	bl	FtlEctTblFlush
	bl	FtlVpcTblFlush
.L3571:
	mov	r0, #0
	ldmfd	sp!, {r3, pc}
.L3577:
	.align	2
.L3576:
	.word	.LANCHOR0
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
	ldr	r3, .L3581
	ldr	r3, [r3, #504]
	cmp	r3, #1
	bne	.L3579
	bl	FtlSysFlush
.L3579:
	mov	r0, #0
	ldmfd	sp!, {r3, pc}
.L3582:
	.align	2
.L3581:
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
	ldr	r0, .L3587
	bl	printk
	ldmfd	sp!, {r3, lr}
	b	ftl_deinit
.L3588:
	.align	2
.L3587:
	.word	.LC154
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
	@ args = 0, pretend = 0, frame = 16
	@ frame_needed = 0, uses_anonymous_args = 0
	stmfd	sp!, {r4, r5, r6, r7, r8, r9, lr}
	.save {r4, r5, r6, r7, r8, r9, lr}
	.pad #20
	sub	sp, sp, #20
	ldr	r6, .L3611
	add	r2, r0, r1
	ldr	r4, .L3611+4
	mov	r7, r0
	mov	r5, r1
	ldr	r3, [r6]
	str	r3, [sp, #12]
	ldr	r3, [r4, #1912]
	cmp	r2, r3
	mvnhi	r0, #0
	bhi	.L3592
	ldr	r3, .L3611+8
	ldr	r0, [r3, #504]
	cmn	r0, #1
	beq	.L3592
	cmp	r1, #31
	bls	.L3602
	ldr	r3, [r4, #4052]
	cmp	r3, #0
	bne	.L3602
	bl	FtlCacheWriteBack
	movw	r3, #1878
	ldrh	r4, [r4, r3]
	mov	r0, r7
	mov	r1, r4
	bl	__aeabi_uidiv
	smulbb	r3, r0, r4
	mov	r8, r0
	rsb	r7, r3, r7
	uxth	r7, r7
	cmp	r7, #0
	beq	.L3593
	rsb	r4, r7, r4
	add	r8, r0, #1
	cmp	r4, r5
	movcs	r4, r5
	uxth	r4, r4
	rsb	r5, r4, r5
.L3593:
	ldr	r4, .L3611+12
	mvn	r3, #0
	ldr	r9, .L3611+16
	str	r3, [sp, #8]
	mov	r7, r4
.L3594:
	ldrh	r3, [r4]
	cmp	r5, r3
	bcc	.L3610
	mov	r0, r8
	add	r1, sp, #4
	mov	r2, #0
	bl	log2phys
	ldr	r3, [sp, #4]
	cmn	r3, #1
	beq	.L3595
	ldr	r2, .L3611+20
	add	r1, sp, #8
	ldr	r3, [r9, #456]
	mov	r0, r8
	add	r3, r3, #1
	str	r3, [r9, #456]
	ldr	r3, [r2, #-3876]
	add	r3, r3, #1
	str	r3, [r2, #-3876]
	mov	r2, #1
	bl	log2phys
	ldr	r0, [sp, #4]
	ubfx	r0, r0, #10, #16
	bl	P2V_block_in_plane
	bl	decrement_vpc_count
.L3595:
	ldrh	r3, [r7]
	add	r8, r8, #1
	rsb	r5, r3, r5
	b	.L3594
.L3610:
	ldr	r3, .L3611+16
	ldr	r2, [r3, #456]
	cmp	r2, #32
	bls	.L3602
	mov	r2, #0
	str	r2, [r3, #456]
	bl	l2p_flush
	bl	FtlVpcTblFlush
.L3602:
	mov	r0, #0
.L3592:
	ldr	r2, [sp, #12]
	ldr	r3, [r6]
	cmp	r2, r3
	beq	.L3598
	bl	__stack_chk_fail
.L3598:
	add	sp, sp, #20
	@ sp needed
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, pc}
.L3612:
	.align	2
.L3611:
	.word	__stack_chk_guard
	.word	.LANCHOR0
	.word	.LANCHOR1
	.word	.LANCHOR0+1878
	.word	.LANCHOR4
	.word	.LANCHOR2
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
	mov	r8, r3
	ldr	r3, .L3662
	.pad #84
	sub	sp, sp, #84
	str	r2, [sp, #36]
	ldr	r3, [r3]
	str	r3, [sp, #76]
	ldr	r3, .L3662+4
	ldr	r3, [r3, #504]
	cmn	r3, #1
	moveq	r0, r3
	beq	.L3615
	cmp	r0, #16
	mov	r7, r1
	bne	.L3616
	add	r0, r1, #256
	mov	r1, r2
	mov	r2, r8
	bl	FtlVendorPartRead
	b	.L3615
.L3616:
	ldr	r3, [sp, #36]
	add	r3, r1, r3
	str	r3, [sp, #40]
	ldr	r3, .L3662+8
	ldr	r1, [sp, #40]
	ldr	r2, [r3, #1912]
	cmp	r1, r2
	mvnhi	r0, #0
	bhi	.L3615
	movw	r2, #1878
	mov	r0, r7
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
	ldr	r3, .L3662+12
	ldr	r1, [sp, #36]
	str	r0, [sp, #32]
	mov	r0, r10
	ldr	r2, [r3, #-3852]
	add	r2, r1, r2
	ldr	r1, [sp, #28]
	str	r2, [r3, #-3852]
	ldr	r2, [r3, #-3880]
	add	r2, r1, r2
	ldr	r1, [sp, #32]
	str	r2, [r3, #-3880]
	bl	FtlCacheMetchLpa
	cmp	r0, #0
	beq	.L3617
	bl	FtlCacheWriteBack
.L3617:
	ldr	r4, .L3662+12
	mov	r9, #0
	mov	r5, r10
	mov	r6, r9
	str	r9, [sp, #48]
	str	r9, [sp, #52]
.L3618:
	ldr	r3, [sp, #28]
	cmp	r3, #0
	beq	.L3661
	mov	r0, r5
	add	r1, sp, #72
	mov	r2, #0
	bl	log2phys
	ldr	r3, [sp, #72]
	cmn	r3, #1
	bne	.L3657
	mov	fp, #0
.L3619:
	ldr	r3, .L3662+16
	ldrh	r0, [r3]
	cmp	fp, r0
	bcs	.L3623
	mla	r0, r0, r5, fp
	ldr	r2, [sp, #40]
	cmp	r0, r7
	movcs	r3, #1
	movcc	r3, #0
	cmp	r0, r2
	movcs	r3, #0
	cmp	r3, #0
	beq	.L3621
	rsb	r0, r7, r0
	mov	r1, #0
	mov	r2, #512
	add	r0, r8, r0, asl #9
	bl	ftl_memset
.L3621:
	add	fp, fp, #1
	b	.L3619
.L3657:
	ldr	r2, [r4, #-2076]
	mov	fp, #36
	cmp	r5, r10
	mla	fp, fp, r6, r2
	str	r3, [fp, #4]
	bne	.L3624
	ldr	r3, [r4, #-2048]
	mov	r0, r7
	str	r3, [fp, #8]
	ldr	r3, .L3662+16
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
	b	.L3625
.L3624:
	ldr	r3, [sp, #32]
	cmp	r5, r3
	bne	.L3626
	ldr	r3, [r4, #-2044]
	ldr	r1, [sp, #40]
	str	r3, [fp, #8]
	ldr	r3, .L3662+16
	ldrh	r2, [r3]
	mul	r3, r2, r5
	rsb	r9, r3, r1
	cmp	r9, r2
	bne	.L3625
	b	.L3659
.L3626:
	ldr	r3, .L3662+16
	ldrh	r3, [r3]
	mul	r3, r3, r5
.L3659:
	rsb	r3, r7, r3
	add	r3, r8, r3, asl #9
	str	r3, [fp, #8]
.L3625:
	ldr	r3, .L3662+20
	ldr	r2, [r4, #-2036]
	str	r5, [fp, #16]
	ldrh	r3, [r3]
	mul	r3, r3, r6
	add	r6, r6, #1
	bic	r3, r3, #3
	add	r3, r2, r3
	str	r3, [fp, #12]
.L3623:
	ldr	r3, [sp, #28]
	add	r5, r5, #1
	subs	r3, r3, #1
	str	r3, [sp, #28]
	beq	.L3627
	ldr	r3, .L3662+24
	ldrh	r3, [r3]
	cmp	r6, r3, asl #3
	bne	.L3618
.L3627:
	cmp	r6, #0
	beq	.L3618
	ldr	r0, [r4, #-2076]
	mov	r1, r6
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
.L3634:
	ldr	r3, [sp, #44]
	mov	ip, #36
	mul	fp, ip, r3
	ldr	r3, [r4, #-2076]
	add	r3, r3, fp
	ldr	r2, [r3, #16]
	cmp	r2, r10
	bne	.L3629
	ldr	r1, [r3, #8]
	ldr	r3, [r4, #-2048]
	cmp	r1, r3
	bne	.L3630
	ldr	r3, [sp, #60]
	mov	r0, r8
	ldr	r2, [sp, #64]
	add	r1, r1, r3
	b	.L3660
.L3629:
	ldr	r1, [sp, #32]
	cmp	r2, r1
	bne	.L3630
	ldr	r1, [r3, #8]
	ldr	r3, [r4, #-2044]
	cmp	r1, r3
	bne	.L3630
	ldr	r3, .L3662+16
	ldr	r2, [sp, #68]
	ldrh	r0, [r3]
	ldr	r3, [sp, #32]
	mul	r0, r0, r3
	rsb	r0, r7, r0
	add	r0, r8, r0, asl #9
.L3660:
	bl	ftl_memcpy
.L3630:
	ldr	r2, [r4, #-2076]
	add	r3, r2, fp
	ldr	r1, [r2, fp]
	cmn	r1, #1
	streq	r1, [sp, #52]
	ldreq	r2, [r4, #-3676]
	addeq	r2, r2, #1
	streq	r2, [r4, #-3676]
	ldr	r2, [r3, #12]
	ldr	r1, [r3, #16]
	ldr	r2, [r2, #8]
	cmp	r1, r2
	beq	.L3632
	ldr	r2, [r4, #-3676]
	ldr	r0, .L3662+28
	add	r2, r2, #1
	str	r2, [r4, #-3676]
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
.L3632:
	ldr	r3, [r4, #-2076]
	add	r2, r3, fp
	ldr	r3, [r3, fp]
	cmp	r3, #256
	bne	.L3633
	ldr	r0, [r2, #4]
	ubfx	r0, r0, #10, #16
	bl	P2V_block_in_plane
	bl	FtlGcRefreshBlock
.L3633:
	ldr	r3, [sp, #44]
	add	r3, r3, #1
	str	r3, [sp, #44]
	cmp	r3, r6
	bne	.L3634
	mov	r6, #0
	b	.L3618
.L3661:
	ldr	r3, .L3662+32
	ldrh	r3, [r3, #-6]
	cmp	r3, #0
	beq	.L3636
	ldr	r0, [sp, #28]
	mov	r1, #1
	bl	ftl_do_gc
.L3636:
	ldr	r0, [sp, #52]
.L3615:
	ldr	r3, .L3662
	ldr	r2, [sp, #76]
	ldr	r3, [r3]
	cmp	r2, r3
	beq	.L3637
	bl	__stack_chk_fail
.L3637:
	add	sp, sp, #84
	@ sp needed
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L3663:
	.align	2
.L3662:
	.word	__stack_chk_guard
	.word	.LANCHOR1
	.word	.LANCHOR0
	.word	.LANCHOR2
	.word	.LANCHOR0+1878
	.word	.LANCHOR0+1884
	.word	.LANCHOR0+1804
	.word	.LC143
	.word	.LANCHOR2-3168
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
	ldr	r3, .L3670
	ldr	r3, [r3, #504]
	cmp	r3, #1
	mvnne	r4, #0
	bne	.L3667
	mov	r2, r4
	mov	r0, r6
	mov	r1, r5
	bl	ftl_vendor_read
	mov	r4, r0
.L3667:
	bl	rknand_device_unlock
	mov	r0, r4
	ldmfd	sp!, {r4, r5, r6, pc}
.L3671:
	.align	2
.L3670:
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
	ldr	r2, .L3693
	ldr	r5, .L3693+4
	ldr	r6, .L3693+8
	ldr	r1, .L3693+12
	ldr	r0, .L3693+16
	str	r3, [r6, #504]
	mov	r3, #0
	str	r3, [r2, #460]
	str	r3, [r5, #4052]
	bl	printk
	add	r0, r5, #112
	bl	FtlConstantsInit
	bl	FtlMemInit
	bl	FtlVariablesInit
	ldr	r3, [r5, #1808]
	uxth	r0, r3
	bl	FtlFreeSysBlkQueueInit
	bl	FtlLoadBbt
	cmp	r0, #0
	ldrne	r0, .L3693+20
	bne	.L3692
	bl	FtlSysBlkInit
	subs	r4, r0, #0
	beq	.L3679
	ldr	r0, .L3693+24
.L3692:
	ldr	r1, .L3693+28
	bl	printk
	b	.L3678
.L3679:
	mov	r1, #1
	str	r1, [r6, #504]
	bl	ftl_do_gc
	ldr	r3, .L3693+32
	ldrh	r7, [r3, #-12]
	cmp	r7, #15
	bhi	.L3680
	ldr	r8, .L3693+36
	add	r5, r3, #232
	sub	r6, r3, #12
.L3683:
	ldrh	r3, [r5]
	movw	r2, #65535
	cmp	r3, r2
	bne	.L3681
	ldrh	r2, [r8]
	cmp	r2, r3
	bne	.L3681
	and	r0, r4, #7
	bl	List_get_gc_head_node
	uxth	r0, r0
	bl	FtlGcRefreshBlock
.L3681:
	mov	r0, #1
	mov	r1, r0
	bl	ftl_do_gc
	mov	r0, #0
	mov	r1, #1
	bl	ftl_do_gc
	ldrh	r2, [r6]
	add	r3, r7, #2
	cmp	r2, r3
	bhi	.L3678
	add	r4, r4, #1
	cmp	r4, #1024
	bne	.L3683
	b	.L3678
.L3680:
	ldrb	r3, [r5, #140]	@ zero_extendqisi2
	cmp	r3, #0
	beq	.L3678
	mov	r4, #128
.L3685:
	mov	r0, #1
	mov	r1, r0
	bl	ftl_do_gc
	subs	r4, r4, #1
	bne	.L3685
.L3678:
	mov	r0, #0
	ldmfd	sp!, {r4, r5, r6, r7, r8, pc}
.L3694:
	.align	2
.L3693:
	.word	.LANCHOR4
	.word	.LANCHOR0
	.word	.LANCHOR1
	.word	.LC77
	.word	.LC76
	.word	.LC155
	.word	.LC156
	.word	.LANCHOR3+108
	.word	.LANCHOR2-4032
	.word	.LANCHOR2-3182
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
	ldr	r6, .L3700
	ldr	r4, .L3700+4
	mov	r5, #0
	mov	r1, r6
	str	r5, [r1, #468]!
	str	r0, [r6, #464]
	mov	r0, r4
	str	r5, [r0, #144]!
	bl	rknand_get_reg_addr
	ldr	r3, [r4, #144]
	cmp	r3, r5
	beq	.L3698
	bl	rk_nandc_irq_init
	mov	r1, r5
	mov	r2, r5
	mov	r3, #2048
	ldr	r0, [r6, #464]
	bl	FlashSramLoadStore
	bl	rknand_flash_cs_init
	ldr	r0, [r4, #144]
	bl	FlashInit
	subs	r4, r0, #0
	bne	.L3697
	bl	FtlInit
.L3697:
	mov	r1, r4
	ldr	r0, .L3700+8
	bl	printk
	mov	r0, r4
	ldmfd	sp!, {r4, r5, r6, pc}
.L3698:
	mvn	r0, #0
	ldmfd	sp!, {r4, r5, r6, pc}
.L3701:
	.align	2
.L3700:
	.word	.LANCHOR4
	.word	.LANCHOR2
	.word	.LC157
	.fnend
	.size	rk_ftl_init, .-rk_ftl_init
	.align	2
	.global	ftl_fix_nand_power_lost_error
	.type	ftl_fix_nand_power_lost_error, %function
ftl_fix_nand_power_lost_error:
	.fnstart
	@ args = 0, pretend = 0, frame = 64
	@ frame_needed = 0, uses_anonymous_args = 0
	ldr	r3, .L3719
	stmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.save {r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.pad #68
	sub	sp, sp, #68
	ldr	r2, [r3]
	str	r3, [sp]
	str	r2, [sp, #60]
	ldr	r2, .L3719+4
	ldrb	r1, [r2, #140]	@ zero_extendqisi2
	cmp	r1, #0
	beq	.L3702
	ldr	r9, .L3719+8
	mov	r6, r2
	ldr	r4, .L3719+12
	movw	r5, #4097
	ldr	r0, .L3719+16
	ldrh	r8, [r9]
	ldr	r3, [r4, #-4060]
	mov	r7, r8, asl #1
	mov	r1, r8
	ldrh	r2, [r3, r7]
	bl	printk
	sub	r3, r4, #4032
	ldrh	r0, [r3, #-8]
	bl	FtlGcRefreshOpenBlock
	sub	r3, r4, #3984
	ldrh	r0, [r3, #-8]
	bl	FtlGcRefreshOpenBlock
	ldr	r0, .L3719+20
	bl	allocate_new_data_superblock
	ldr	r0, .L3719+24
	bl	allocate_new_data_superblock
	str	r9, [sp, #4]
.L3704:
	subs	r5, r5, #1
	beq	.L3708
	mov	r0, #1
	mov	r1, r0
	bl	ftl_do_gc
	ldr	r3, [r4, #-4060]
	ldrh	r3, [r3, r7]
	cmp	r3, #0
	bne	.L3704
.L3708:
	ldr	r3, [r4, #-4060]
	mov	r1, r8
	ldr	r0, .L3719+16
	ldrh	r2, [r3, r7]
	bl	printk
	ldr	r3, [r4, #-4060]
	ldrh	r5, [r3, r7]
	cmp	r5, #0
	bne	.L3706
	add	r0, sp, #64
	movw	r10, #65535
	mov	fp, #36
	strh	r8, [r0, #-52]!	@ movhi
	bl	make_superblock
	movw	r3, #1804
	ldr	r9, [r6, #4056]
	ldrh	lr, [r6, r3]
	mov	r3, r5
	mov	ip, r3
	add	r0, sp, #26
.L3709:
	uxth	r2, r3
	cmp	r2, lr
	bcs	.L3718
	ldrh	r2, [r0, #2]!
	add	r3, r3, #1
	cmp	r2, r10
	movne	r2, r2, asl #10
	mlane	r1, fp, r5, r9
	addne	r5, r5, #1
	uxthne	r5, r5
	stmneib	r1, {r2, ip}
	strne	ip, [r1, #12]
	b	.L3709
.L3718:
	ldr	r3, [r4, #-4060]
	mov	r1, r8
	ldr	r0, .L3719+28
	ldrh	r2, [r3, r7]
	bl	printk
	mov	r1, #0
	mov	r2, r5
	ldr	r0, [r6, #4056]
	bl	FlashEraseBlocks
	ldr	r0, [r6, #4056]
	mov	r1, #1
	mov	r2, r5
	bl	FlashEraseBlocks
.L3706:
	ldr	r2, [sp, #4]
	mvn	r3, #0
	strh	r3, [r2]	@ movhi
.L3702:
	ldr	r3, [sp]
	ldr	r2, [sp, #60]
	ldr	r3, [r3]
	cmp	r2, r3
	beq	.L3712
	bl	__stack_chk_fail
.L3712:
	add	sp, sp, #68
	@ sp needed
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L3720:
	.align	2
.L3719:
	.word	__stack_chk_guard
	.word	.LANCHOR0
	.word	.LANCHOR4+308
	.word	.LANCHOR2
	.word	.LC158
	.word	.LANCHOR2-4040
	.word	.LANCHOR2-3992
	.word	.LC159
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
	@ args = 0, pretend = 0, frame = 104
	@ frame_needed = 0, uses_anonymous_args = 0
	stmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.save {r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.pad #108
	sub	sp, sp, #108
	ldr	r9, .L3796
	str	r3, [sp, #4]
	ldr	r3, .L3796+4
	ldr	r3, [r3]
	str	r3, [sp, #100]
	ldr	r3, [r9, #4052]
	cmp	r3, #0
	bne	.L3764
	mov	r8, r2
	ldr	r2, .L3796+8
	ldr	r2, [r2, #504]
	cmn	r2, #1
	moveq	r0, r3
	beq	.L3723
	cmp	r0, #16
	mov	r7, r1
	bne	.L3724
	add	r0, r1, #256
	ldr	r2, [sp, #4]
	mov	r1, r8
	bl	FtlVendorPartWrite
	b	.L3723
.L3724:
	ldr	r3, [r9, #1912]
	add	r5, r1, r8
	cmp	r5, r3
	mvnhi	r0, #0
	bhi	.L3723
	ldr	r6, .L3796+12
	mov	r3, #2048
	mov	r0, r1
	str	r3, [r6, #472]
	movw	r3, #1878
	ldrh	r4, [r9, r3]
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
	ldr	r3, .L3796+16
	ldr	r1, [sp]
	ldr	r2, [r3, #-3872]
	add	r2, r1, r2
	ldr	r1, [r9, #1924]
	str	r2, [r3, #-3872]
	ldr	r2, [r3, #-3856]
	add	r2, r8, r2
	str	r2, [r3, #-3856]
	movcs	r2, #1
	movcc	r2, #0
	cmp	r1, #0
	str	r2, [sp, #20]
	beq	.L3726
	mov	r2, #36
	ldr	r10, [r9, #1928]
	mul	r2, r2, r1
	ldr	r1, [sp, #8]
	sub	r2, r2, #36
	add	r10, r10, r2
	ldr	r2, [r10, #16]
	cmp	r1, r2
	bne	.L3727
	ldr	r2, [r3, #-3868]
	mov	r1, r4
	mov	r0, r7
	add	r2, r2, #1
	str	r2, [r3, #-3868]
	ldr	r3, [r6, #476]
	add	r3, r3, #1
	str	r3, [r6, #476]
	bl	__aeabi_uidivmod
	ldr	r0, [r10, #8]
	rsb	r4, r1, r4
	add	r0, r0, r1, asl #9
	cmp	r4, r8
	ldr	r1, [sp, #4]
	movcs	r4, r8
	mov	r9, r4, asl #9
	mov	r2, r9
	bl	ftl_memcpy
	cmp	r5, #0
	bne	.L3728
	ldr	r3, [r6, #476]
	cmp	r3, #2
	ble	.L3764
.L3728:
	ldr	r3, [sp, #4]
	rsb	r8, r4, r8
	add	r7, r7, r4
	str	r5, [sp]
	add	r3, r3, r9
	str	r3, [sp, #4]
	ldr	r3, [sp, #8]
	add	r3, r3, #1
	str	r3, [sp, #8]
.L3727:
	mov	r3, #0
	str	r3, [r6, #476]
.L3726:
	ldr	r0, [sp, #8]
	ldr	r1, [sp, #28]
	bl	FtlCacheMetchLpa
	cmp	r0, #0
	beq	.L3729
	bl	FtlCacheWriteBack
.L3729:
	ldr	r5, .L3796+20
	mov	r3, #0
	ldr	r4, .L3796
	str	r3, [sp, #12]
	str	r5, [r6, #452]
	ldr	r6, [sp, #8]
	mov	r10, r4
	str	r3, [sp, #32]
.L3730:
	ldr	r3, [sp]
	cmp	r3, #0
	beq	.L3795
	ldrh	r2, [r5, #4]
	cmp	r2, #0
	bne	.L3731
	ldr	r3, .L3796+20
	ldr	r9, .L3796+8
	cmp	r5, r3
	bne	.L3732
	add	r0, r3, #48
	ldrh	r5, [r0, #4]
	cmp	r5, #0
	bne	.L3733
	bl	allocate_new_data_superblock
	str	r5, [r9, #3292]
.L3733:
	ldr	r0, .L3796+20
	bl	allocate_new_data_superblock
	ldr	r3, [r9, #3292]
	cmp	r3, #0
	ldrne	r5, .L3796+24
	bne	.L3734
.L3735:
	ldr	r5, .L3796+20
	b	.L3734
.L3732:
	ldrh	r3, [r3, #4]
	str	r2, [r9, #3292]
	cmp	r3, #0
	bne	.L3735
	mov	r0, r5
	bl	allocate_new_data_superblock
.L3734:
	ldrh	r3, [r5, #4]
	cmp	r3, #0
	bne	.L3736
	mov	r0, r5
	bl	allocate_new_data_superblock
.L3736:
	ldr	r3, .L3796+12
	str	r5, [r3, #452]
.L3731:
	ldr	r2, .L3796+16
	ldr	r1, [r4, #1924]
	ldrh	r3, [r5, #4]
	ldr	r2, [r2, #-2080]
	rsb	r2, r1, r2
	cmp	r3, r2
	movcs	r3, r2
	ldr	r2, [sp]
	cmp	r3, r2
	movcs	r3, r2
	str	r3, [sp, #44]
	mov	r3, #0
.L3793:
	str	r3, [sp, #16]
	ldr	r3, [sp, #16]
	ldr	r2, [sp, #44]
	cmp	r3, r2
	beq	.L3738
	ldrh	r3, [r5, #4]
	cmp	r3, #0
	beq	.L3738
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
	beq	.L3739
	ldr	r3, .L3796+28
	ldrh	r2, [r3]
	add	r3, r8, r7
	mls	r3, r2, r6, r3
	cmp	r3, r2
	bne	.L3738
.L3739:
	add	r1, sp, #60
	mov	r2, #0
	mov	r0, r6
	str	ip, [sp, #52]
	bl	log2phys
	mov	r0, r5
	bl	get_new_active_ppa
	ldr	r2, [r4, #1924]
	ldr	r1, [r4, #1928]
	mov	r3, #36
	ldr	fp, .L3796+32
	mla	r1, r3, r2, r1
	ldrh	r2, [fp]
	str	r6, [r1, #16]
	str	r3, [sp, #48]
	ldr	r3, [r4, #1924]
	str	r0, [r1, #4]
	ldr	r0, .L3796+16
	mul	lr, r3, r2
	bic	r3, lr, #3
	str	r3, [sp, #36]
	ldr	r3, [r0, #-2032]
	ldr	ip, [sp, #36]
	ldrh	lr, [fp, #-2]
	add	r9, r3, ip
	str	r3, [sp, #40]
	ldr	r3, [r4, #1924]
	ldr	r0, [r0, #-2052]
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
	beq	.L3740
	ldr	r3, [sp, #24]
	cmp	r3, #0
	beq	.L3741
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
	b	.L3742
.L3741:
	cmp	ip, #0
	beq	.L3742
	ldr	r2, .L3796+28
	add	r3, r8, r7
	ldrh	r2, [r2]
	smulbb	r2, r2, r6
	rsb	r3, r2, r3
	uxth	r3, r3
	str	r3, [sp, #12]
	ldr	r3, [sp, #24]
	str	r3, [sp, #32]
.L3742:
	ldr	r3, .L3796+28
	ldr	r2, [sp, #12]
	ldrh	r3, [r3]
	cmp	r2, r3
	bne	.L3743
	ldr	r3, [sp, #24]
	ldr	r0, [r10, #1928]
	cmp	r3, #0
	moveq	r3, r2
	ldr	r2, [r10, #1924]
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
	bne	.L3746
	ldr	r0, [r3, #8]
	ldr	r3, .L3796+36
	ldrh	r2, [r3]
	b	.L3791
.L3743:
	ldr	r2, [sp, #60]
	mov	r3, #36
	cmn	r2, #1
	beq	.L3747
	ldr	r1, [r4, #1928]
	add	r0, sp, #64
	str	r2, [sp, #68]
	ldr	r2, [r4, #1924]
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
	ldr	r3, .L3796+16
	ldreq	r2, [r3, #-3676]
	addeq	r2, r2, #1
	streq	r2, [r3, #-3676]
	beq	.L3750
	ldr	r2, [r9, #8]
	cmp	r2, r6
	beq	.L3750
	ldr	r2, [r3, #-3676]
	ldr	r0, .L3796+40
	add	r2, r2, #1
	str	r2, [r3, #-3676]
	mov	r2, r6
	ldr	r1, [r9, #8]
	bl	printk
	b	.L3750
.L3747:
	ldr	r2, [r4, #1924]
	ldr	r1, [r4, #1928]
	mla	r3, r3, r2, r1
	mov	r1, #0
	ldr	r0, [r3, #8]
	ldr	r3, .L3796+36
	ldrh	r2, [r3]
	bl	ftl_memset
.L3750:
	ldr	r3, [sp, #24]
	cmp	r3, #0
	mov	r3, #36
	beq	.L3751
	ldr	r1, [r4, #1928]
	ldr	r2, [r4, #1924]
	mla	r3, r3, r2, r1
	ldr	r1, [sp, #4]
	ldr	r0, [r3, #8]
	ldr	r3, [sp, #32]
	add	r0, r0, r3, asl #9
	b	.L3794
.L3751:
	ldr	r1, [r4, #1924]
	ldr	r2, [r4, #1928]
	mla	r3, r3, r1, r2
	ldr	r2, .L3796+28
	ldrh	r1, [r2]
	ldr	r0, [r3, #8]
	mul	r1, r1, r6
	ldr	r3, [sp, #4]
	rsb	r1, r7, r1
	add	r1, r3, r1, asl #9
.L3794:
	ldr	r3, [sp, #12]
	mov	r2, r3, asl #9
	b	.L3791
.L3740:
	ldr	r2, [sp, #20]
	cmp	r2, #0
	ldr	r2, [r4, #1924]
	beq	.L3752
	ldr	r1, [r4, #1928]
	mla	r3, r3, r2, r1
	ldr	r2, .L3796+28
	ldrh	fp, [r2]
	ldr	r2, [sp, #4]
	mul	fp, fp, r6
	rsb	fp, r7, fp
	add	fp, r2, fp, asl #9
	str	fp, [r3, #8]
	b	.L3746
.L3752:
	ldr	r0, [r4, #1928]
	mla	r3, r3, r2, r0
	ldr	r2, .L3796+28
	ldrh	r1, [r2]
	ldrh	r2, [fp, #-2]
	ldr	r0, [r3, #8]
	mul	r1, r1, r6
	ldr	r3, [sp, #4]
	rsb	r1, r7, r1
	add	r1, r3, r1, asl #9
.L3791:
	bl	ftl_memcpy
.L3746:
	ldr	r3, .L3796+44
	ldr	r2, [sp, #40]
	ldr	r1, [sp, #36]
	strh	r3, [r2, r1]	@ movhi
	ldr	r2, .L3796+16
	str	r6, [r9, #8]
	add	r6, r6, #1
	ldr	r3, [r2, #-3844]
	str	r3, [r9, #4]
	add	r3, r3, #1
	cmn	r3, #1
	moveq	r3, #0
	str	r3, [r2, #-3844]
	ldr	r3, [sp, #60]
	str	r3, [r9, #12]
	ldrh	r3, [r5]
	strh	r3, [r9, #2]	@ movhi
	ldr	r3, [r4, #1924]
	add	r3, r3, #1
	str	r3, [r4, #1924]
	ldr	r3, [sp, #16]
	add	r3, r3, #1
	b	.L3793
.L3738:
	ldr	r3, [sp]
	ldr	r2, [sp, #16]
	ldr	r1, [sp, #20]
	rsb	r3, r2, r3
	ldr	r2, .L3796+16
	str	r3, [sp]
	ldr	r3, [r4, #1924]
	ldr	r2, [r2, #-2080]
	cmp	r3, r2
	orrcs	r1, r1, #1
	cmp	r1, #0
	bne	.L3756
	ldrh	r3, [r5, #4]
	cmp	r3, #0
	beq	.L3756
.L3758:
	mov	r3, #0
	str	r3, [sp, #20]
	b	.L3730
.L3756:
	bl	FtlCacheWriteBack
	mov	r3, #0
	str	r3, [r10, #1924]
	ldr	r3, [sp]
	cmp	r3, #1
	bhi	.L3730
	b	.L3758
.L3795:
	mov	r0, r3
	ldr	r2, [sp, #8]
	ldr	r3, [sp, #28]
	rsb	r1, r2, r3
	bl	ftl_do_gc
	ldr	r3, .L3796+48
	ldrh	r3, [r3, #-12]
	cmp	r3, #5
	bls	.L3769
	cmp	r3, #31
	bhi	.L3764
	ldr	r3, .L3796
	ldrb	r3, [r3]	@ zero_extendqisi2
	cmp	r3, #0
	bne	.L3764
.L3769:
	ldr	r5, .L3796+52
	ldr	r4, [sp]
	ldr	r6, .L3796+56
	add	r7, r5, #620
.L3782:
	ldrh	r2, [r5]
	movw	r3, #65535
	cmp	r2, r3
	bne	.L3762
	ldrh	r3, [r6]
	cmp	r3, r2
	bne	.L3762
	ldrh	r2, [r7]
	cmp	r2, r3
	bne	.L3762
	and	r0, r4, #7
	bl	List_get_gc_head_node
	uxth	r0, r0
	bl	FtlGcRefreshBlock
.L3762:
	ldr	r3, .L3796+60
	mov	r0, #1
	mov	r1, r0
	mov	r2, #128
	strh	r2, [r3]	@ movhi
	strh	r2, [r3, #-2]	@ movhi
	bl	ftl_do_gc
	mov	r0, #0
	mov	r1, #1
	bl	ftl_do_gc
	ldr	r3, .L3796
	ldr	r3, [r3, #4052]
	cmp	r3, #0
	bne	.L3764
	ldr	r3, .L3796+64
	ldrh	r3, [r3]
	cmp	r3, #2
	bhi	.L3764
	add	r4, r4, #1
	cmp	r4, #256
	bne	.L3782
.L3764:
	mov	r0, #0
.L3723:
	ldr	r3, .L3796+4
	ldr	r2, [sp, #100]
	ldr	r3, [r3]
	cmp	r2, r3
	beq	.L3763
	bl	__stack_chk_fail
.L3763:
	add	sp, sp, #108
	@ sp needed
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L3797:
	.align	2
.L3796:
	.word	.LANCHOR0
	.word	__stack_chk_guard
	.word	.LANCHOR1
	.word	.LANCHOR4
	.word	.LANCHOR2
	.word	.LANCHOR2-4040
	.word	.LANCHOR2-3992
	.word	.LANCHOR0+1878
	.word	.LANCHOR0+1884
	.word	.LANCHOR0+1882
	.word	.LC160
	.word	-3947
	.word	.LANCHOR2-4032
	.word	.LANCHOR2-3800
	.word	.LANCHOR2-3182
	.word	.LANCHOR2-3234
	.word	.LANCHOR2-4044
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
	ldr	r3, .L3804
	ldr	r3, [r3, #504]
	cmp	r3, #1
	mvnne	r4, #0
	bne	.L3801
	mov	r2, r4
	mov	r0, r6
	mov	r1, r5
	bl	ftl_vendor_write
	mov	r4, r0
.L3801:
	bl	rknand_device_unlock
	mov	r0, r4
	ldmfd	sp!, {r4, r5, r6, pc}
.L3805:
	.align	2
.L3804:
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
	ldr	r4, .L3818
	.pad #28
	sub	sp, sp, #28
	ldr	r7, .L3818+4
	mov	r6, r0
	mov	r5, #0
	ldr	r3, [r4, #-2064]
	mov	r9, r4
	str	r3, [r4, #220]
	ldr	r3, [r4, #-2040]
	str	r3, [r4, #224]
.L3810:
	ldrh	r2, [r7]
	sxth	r3, r5
	cmp	r3, r2
	bge	.L3817
	mov	r1, #1
	ldr	r0, .L3818+8
	mov	r2, r1
	orr	r3, r3, r8
	str	r3, [r4, #216]
	bl	FlashReadPages
	ldr	r3, [r4, #224]
	mov	r1, r6
	ldr	r0, .L3818+12
	ldr	r2, [r3]
	str	r2, [sp]
	ldr	r2, [r3, #4]
	str	r2, [sp, #4]
	ldr	r2, [r3, #8]
	str	r2, [sp, #8]
	ldr	r3, [r3, #12]
	ldr	r2, [r4, #212]
	str	r3, [sp, #12]
	ldr	r3, [r4, #220]
	ldr	r3, [r3]
	str	r3, [sp, #16]
	ldr	r3, [r4, #216]
	bl	printk
	ldr	r3, [r4, #224]
	ldr	r3, [r3]
	cmn	r3, #1
	beq	.L3811
	ldr	r0, .L3818+16
	mov	r2, #4
	ldr	r1, [r9, #-2064]
	mov	r3, #768
	bl	rknand_print_hex
.L3811:
	add	r5, r5, #1
	b	.L3810
.L3817:
	add	sp, sp, #28
	@ sp needed
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, pc}
.L3819:
	.align	2
.L3818:
	.word	.LANCHOR2
	.word	.LANCHOR0+1874
	.word	.LANCHOR2+212
	.word	.LC161
	.word	.LC162
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
	movw	r3, #1812
	ldr	r6, .L3838
	.pad #52
	sub	sp, sp, #52
	ldr	r10, .L3838+4
	ldrh	r7, [r6, r3]
	add	fp, r10, #18
.L3821:
	ldrh	r3, [r10]
	ldr	r4, .L3838+8
	cmp	r3, r7
	bls	.L3834
	ldr	r1, .L3838+12
	mov	r8, #0
	ldr	r2, [r4, #-2076]
	mov	r5, r8
	ldr	r3, [r4, #-3212]
	ldrh	ip, [r1]
	ldr	r9, [r4, #-3208]
	ldrh	r1, [r1, #80]
	str	r1, [sp, #28]
.L3830:
	uxth	r1, r8
	cmp	r1, ip
	bcs	.L3836
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
	bne	.L3822
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
.L3822:
	add	r8, r8, #1
	b	.L3830
.L3836:
	cmp	r5, #0
	beq	.L3825
	ldr	r0, [r4, #-2076]
	mov	r1, r5
	mov	r2, #1
	mov	r8, #0
	bl	FlashReadPages
	mov	r9, #36
.L3826:
	uxth	r3, r8
	cmp	r3, r5
	bcs	.L3825
	ldr	r3, [r4, #-2076]
	ldr	r0, .L3838+16
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
	b	.L3826
.L3825:
	add	r7, r7, #1
	uxth	r7, r7
	b	.L3821
.L3834:
	ldr	r9, .L3838+20
	mov	r8, #0
.L3829:
	ldr	r3, .L3838+24
	sxth	r5, r8
	ldr	r7, .L3838+8
	ldrh	r3, [r3]
	cmp	r5, r3
	bge	.L3832
	mov	r5, r5, asl #1
	mov	r7, #0
.L3833:
	ldrh	r2, [r9]
	sxth	r3, r7
	add	r7, r7, #1
	cmp	r3, r2
	bge	.L3837
	ldr	r2, [r4, #-2012]
	mov	r1, #1
	ldr	r0, .L3838+28
	ldrh	r2, [r2, r5]
	orr	r3, r3, r2, asl #10
	mov	r2, r1
	str	r3, [r4, #216]
	bl	FlashReadPages
	ldr	r3, [r4, #224]
	ldr	r1, [r4, #-2012]
	ldr	r2, [r4, #220]
	ldr	r0, [r3]
	ldrh	r1, [r1, r5]
	str	r0, [sp]
	ldr	r0, [r3, #4]
	str	r0, [sp, #4]
	ldr	r0, [r3, #8]
	str	r0, [sp, #8]
	ldr	r3, [r3, #12]
	ldr	r0, .L3838+32
	str	r3, [sp, #12]
	ldr	r3, [r2]
	str	r3, [sp, #16]
	ldr	r3, [r2, #4]
	str	r3, [sp, #20]
	ldr	r2, [r4, #212]
	ldr	r3, [r4, #216]
	bl	printk
	b	.L3833
.L3837:
	add	r8, r8, #1
	b	.L3829
.L3832:
	ldr	r1, [r7, #-2012]
	movw	r4, #1908
	ldr	r3, [r6, #1900]
	mov	r2, #2
	ldr	r0, .L3838+36
	bl	rknand_print_hex
	ldr	r1, [r7, #-1992]
	ldrh	r3, [r6, r4]
	mov	r2, #4
	ldr	r0, .L3838+40
	bl	rknand_print_hex
	ldr	r0, .L3838+44
	ldr	r1, [r7, #-1988]
	mov	r2, #4
	ldrh	r3, [r6, r4]
	add	sp, sp, #52
	@ sp needed
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, fp, lr}
	b	rknand_print_hex
.L3839:
	.align	2
.L3838:
	.word	.LANCHOR0
	.word	.LANCHOR0+1814
	.word	.LANCHOR2
	.word	.LANCHOR0+1804
	.word	.LC163
	.word	.LANCHOR0+1874
	.word	.LANCHOR2-1928
	.word	.LANCHOR2+212
	.word	.LC110
	.word	.LC164
	.word	.LC165
	.word	.LC166
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
	ldr	r3, .L3842
	ldr	r2, [r3, #1744]
	ldr	r3, .L3842+4
	cmp	r2, r3
	bxne	lr
	b	flash_enter_slc_mode
.L3843:
	.align	2
.L3842:
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
	ldr	r3, .L3846
	ldr	r2, [r3, #1744]
	ldr	r3, .L3846+4
	cmp	r2, r3
	bxne	lr
	b	flash_exit_slc_mode
.L3847:
	.align	2
.L3846:
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
	ldr	r4, .L3857
	mov	r8, r1
	.pad #20
	sub	sp, sp, #20
	mov	r1, r6
	mov	fp, r2
	ldr	r0, .L3857+4
	ldr	r3, [r4, #692]
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
.L3849:
	cmp	r7, r8
	bcs	.L3856
	add	r2, r7, r10
	ldrb	r1, [r4, #140]	@ zero_extendqisi2
	ubfx	r2, r2, #2, #16
	rsb	r5, ip, #4
	cmp	r1, #0
	add	r3, r4, r2, asl #1
	uxth	r5, r5
	ldrh	r3, [r3, #144]
	beq	.L3850
	ldr	r1, .L3857
	ldr	r0, .L3857+8
	ldr	r1, [r1, #1744]
	cmp	r1, r0
	moveq	r3, r2
.L3850:
	ldr	r2, [sp]
	add	r7, r5, r7
	add	r2, ip, r2
	ldrb	ip, [r4, #1792]	@ zero_extendqisi2
	uxth	r7, r7
	mla	r3, r9, r3, r2
	ldr	r2, [r4, #692]
	str	ip, [sp, #4]
	ldrb	r1, [r2, #9]	@ zero_extendqisi2
	str	r3, [sp, #12]
	ldr	r3, .L3857+12
	str	r1, [sp, #8]
	ldrb	r0, [r3, #174]	@ zero_extendqisi2
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
	b	.L3849
.L3856:
	mov	r1, r6
	mov	r2, r8
	mov	r3, #0
	ldr	r0, .L3857+16
	bl	printk
	mov	r0, #0
	add	sp, sp, #20
	@ sp needed
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L3858:
	.align	2
.L3857:
	.word	.LANCHOR0
	.word	.LC167
	.word	1446522928
	.word	.LANCHOR2
	.word	.LC168
	.fnend
	.size	IdBlockReadData, .-IdBlockReadData
	.align	2
	.global	write_idblock
	.type	write_idblock, %function
write_idblock:
	.fnstart
	@ args = 0, pretend = 0, frame = 128
	@ frame_needed = 0, uses_anonymous_args = 0
	ldr	r3, .L3906
	stmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.save {r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.pad #140
	sub	sp, sp, #140
	ldr	r3, [r3]
	mov	r4, r0
	ldr	r7, .L3906+4
	mov	r0, #256000
	mov	r6, r1
	mov	r5, r2
	str	r3, [sp, #132]
	ldr	r3, [r7, #692]
	ldr	r8, [r7, #4]
	ldrb	r9, [r3, #9]	@ zero_extendqisi2
	bl	ftl_malloc
	subs	r3, r0, #0
	str	r3, [sp, #12]
	beq	.L3884
	add	r4, r4, #508
	add	r4, r4, #3
	mov	r4, r4, lsr #9
	cmp	r4, #8
	bls	.L3882
	cmp	r4, #500
	bhi	.L3884
	b	.L3861
.L3882:
	mov	r4, #8
.L3861:
	ldr	r2, [r6]
	ldr	r3, .L3906+8
	cmp	r2, r3
	bne	.L3884
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
.L3865:
	ldr	r1, [r0, #-4]!
	cmp	r1, #0
	bne	.L3862
	ldr	r1, [r6, r3, asl #2]
	add	r3, r3, #1
	cmp	r3, #4096
	sub	r2, r2, #1
	movhi	r3, #0
	cmp	r2, #4096
	str	r1, [r0, #512]
	bne	.L3865
	b	.L3864
.L3862:
	ldr	r0, .L3906+12
	bl	printk
.L3864:
	mov	r1, r5
	mov	r3, #5
	ldr	r0, .L3906+16
	mov	r2, #4
	bl	rknand_print_hex
	ldrb	r2, [r7, #1]	@ zero_extendqisi2
	ldr	r1, [r6, #512]
	sub	r5, r5, #4
	ldr	r0, .L3906+20
	bl	printk
	ldr	r2, .L3906+24
	ldrh	r3, [r7, #138]
	mov	r1, r4
	ldr	r0, .L3906+28
	ldr	r2, [r2, #168]
	str	r2, [sp]
	mov	r2, r4
	bl	printk
	ldrb	r3, [r7, #1]	@ zero_extendqisi2
	ldr	r7, .L3906+4
	ldr	r2, [r6, #512]
	cmp	r2, r3
	strhi	r3, [r6, #512]
	mov	r3, r4, asl #7
	str	r3, [sp, #36]
	mov	r3, #0
	str	r3, [sp, #20]
	str	r3, [sp, #16]
.L3879:
	ldr	r2, [r5, #4]
	ldrb	r3, [r7, #1]	@ zero_extendqisi2
	ldr	r9, .L3906+4
	cmp	r2, r3
	bcs	.L3867
	ldr	r3, .L3906+24
	ldr	r3, [r3, #168]
	cmp	r2, r3
	bcc	.L3867
	ldr	r3, [sp, #32]
	ldr	r1, [sp, #32]
	cmp	r3, #1
	movls	r3, #0
	movhi	r3, #1
	str	r3, [sp, #44]
	ldr	r3, [sp, #16]
	cmp	r3, #0
	cmpne	r1, #1
	bls	.L3868
	ldr	r3, [r5]
	add	r3, r3, #1
	cmp	r2, r3
	beq	.L3867
.L3868:
	mov	r1, #0
	mov	r2, #512
	ldr	r0, [sp, #12]
	bl	memset
	ldr	r8, [r5, #4]
	ldr	r3, [r9, #692]
	mov	r2, r4
	ldr	r9, [r9, #4]
	mul	r8, r8, fp
	ldrb	r3, [r3, #9]	@ zero_extendqisi2
	ldr	r0, .L3906+32
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
	bls	.L3869
	mov	r0, #0
	add	r1, r8, r9
	mov	r2, r0
	mov	r10, #2
	bl	FlashEraseBlock
.L3869:
	mov	r0, #0
	bl	flash_boot_exit_slc_mode
	ldr	r3, [r7, #692]
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
.L3870:
	ldr	r3, [sp, #52]
	cmp	r9, r3
	bcs	.L3905
	add	r3, r9, ip
	ubfx	r3, r3, #2, #16
	cmp	r3, #0
	beq	.L3871
	add	r2, r3, #1
	add	r1, r7, r2, asl #1
	ldrh	r10, [r1, #144]
	ldrb	r1, [r7, #140]	@ zero_extendqisi2
	cmp	r1, #0
	beq	.L3872
	ldr	r1, [r7, #1744]
	ldr	r0, .L3906+36
	cmp	r1, r0
	moveq	r10, r2
.L3872:
	sub	r10, r10, #-1073741823
	mov	r10, r10, asl #2
	str	r10, [sp, #68]
.L3871:
	movw	r2, #61424
	str	r2, [sp, #72]
	add	r2, r7, r3, asl #1
	ldrh	r10, [r2, #144]
	ldrb	r2, [r7, #140]	@ zero_extendqisi2
	cmp	r2, #0
	beq	.L3873
	ldr	r2, [r7, #1744]
	ldr	r1, .L3906+36
	cmp	r2, r1
	moveq	r10, r3
.L3873:
	ldr	r3, [sp, #24]
	add	r9, r9, #4
	ldr	r2, [sp, #40]
	str	ip, [sp, #60]
	uxth	r9, r9
	mla	r2, r3, r10, r2
	ldr	r3, .L3906+4
	ldrb	r3, [r3, #1792]	@ zero_extendqisi2
	str	r2, [sp, #56]
	str	r3, [sp, #48]
	ldr	r3, .L3906+24
	ldrb	r0, [r3, #174]	@ zero_extendqisi2
	bl	FlashBchSel
	mov	r0, #0
	bl	flash_boot_enter_slc_mode
	ldr	r3, .L3906+4
	ldr	r3, [r3, #692]
	ldrb	r1, [r3, #9]	@ zero_extendqisi2
	ldr	r2, [sp, #56]
	mov	r0, r2
	bl	__aeabi_uidiv
	add	r3, sp, #68
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
	b	.L3870
.L3905:
	mov	r1, r8
	mov	r3, #0
	mov	r2, r4
	ldr	r0, .L3906+40
	bl	printk
	ldr	r0, [r5, #4]
	mov	r1, r4
	mov	r8, #0
	mul	r0, r0, fp
	ldr	r2, [sp, #12]
	bl	IdBlockReadData
	mov	r3, r6
	ldr	r2, [sp, #12]
.L3878:
	mov	r10, r2
	mov	r9, r3
	ldr	r0, [r10]
	add	r2, r2, #4
	ldr	r1, [r9]
	add	r3, r3, #4
	cmp	r0, r1
	beq	.L3875
	mov	r1, #0
	mov	r2, #512
	ldr	r0, [sp, #12]
	bl	memset
	ldr	r3, [r10]
	ldr	r0, .L3906+44
	str	r3, [sp]
	ldr	r3, [r9]
	bic	r9, r8, #255
	ldr	r1, [sp, #16]
	mov	r9, r9, asl #2
	str	r3, [sp, #4]
	mov	r3, r8
	ldr	r2, [r5, #4]
	bl	printk
	ldr	r0, .L3906+48
	add	r1, r6, r9
	mov	r2, #4
	mov	r3, #256
	bl	rknand_print_hex
	mov	r2, #4
	ldr	r0, .L3906+52
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
	beq	.L3876
	ldr	r1, [r5, #4]
	mov	r0, #0
	mov	r2, r0
	mla	r1, r1, fp, fp
	bl	FlashEraseBlock
.L3876:
	mov	r0, #0
	bl	flash_boot_exit_slc_mode
	ldr	r0, .L3906+56
	ldr	r1, [r5, #4]
	bl	printk
	ldr	r3, [sp, #36]
	cmp	r8, r3
	bcc	.L3867
	b	.L3877
.L3875:
	ldr	r1, [sp, #36]
	add	r8, r8, #1
	cmp	r8, r1
	bne	.L3878
.L3877:
	ldr	r3, [sp, #20]
	add	r3, r3, #1
	str	r3, [sp, #20]
.L3867:
	ldr	r3, [sp, #16]
	add	r5, r5, #4
	add	r3, r3, #1
	str	r3, [sp, #16]
	cmp	r3, #5
	bne	.L3879
	ldr	r0, [sp, #12]
	bl	ftl_free
	ldr	r3, [sp, #20]
	clz	r0, r3
	mov	r0, r0, lsr #5
	rsb	r0, r0, #0
	b	.L3860
.L3884:
	mvn	r0, #0
.L3860:
	ldr	r3, .L3906
	ldr	r2, [sp, #132]
	ldr	r3, [r3]
	cmp	r2, r3
	beq	.L3880
	bl	__stack_chk_fail
.L3880:
	add	sp, sp, #140
	@ sp needed
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L3907:
	.align	2
.L3906:
	.word	__stack_chk_guard
	.word	.LANCHOR0
	.word	-52655045
	.word	.LC169
	.word	.LC170
	.word	.LC171
	.word	.LANCHOR2
	.word	.LC172
	.word	.LC173
	.word	1446522928
	.word	.LC174
	.word	.LC175
	.word	.LC176
	.word	.LC177
	.word	.LC178
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
	ldr	ip, .L3912
	add	r1, r0, r1
	mov	r2, #0
.L3909:
	cmp	r0, r1
	beq	.L3911
	ldrb	r3, [r0], #1	@ zero_extendqisi2
	eor	r3, r3, r2, lsr #24
	add	r3, ip, r3, asl #2
	ldr	r3, [r3, #3296]
	eor	r2, r3, r2, asl #8
	b	.L3909
.L3911:
	mov	r0, r2
	bx	lr
.L3913:
	.align	2
.L3912:
	.word	.LANCHOR1
	.fnend
	.size	CRC_32, .-CRC_32
	.align	2
	.global	write_loader_lba
	.type	write_loader_lba, %function
write_loader_lba:
	.fnstart
	@ args = 0, pretend = 0, frame = 48
	@ frame_needed = 0, uses_anonymous_args = 0
	stmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, lr}
	.save {r4, r5, r6, r7, r8, r9, r10, lr}
	cmp	r0, #64
	ldr	r6, .L3938
	.pad #56
	sub	sp, sp, #56
	mov	r5, r0
	mov	r7, r1
	mov	r9, r2
	ldr	r4, .L3938+4
	ldr	r3, [r6]
	str	r3, [sp, #52]
	bne	.L3915
	ldr	r2, [r2]
	ldr	r3, .L3938+8
	cmp	r2, r3
	bne	.L3915
	mov	r0, #256000
	mov	r3, #1
	strb	r3, [r4, #480]
	bl	ftl_malloc
	mov	r1, #0
	mov	r2, #256000
	str	r0, [r4, #484]
	bl	ftl_memset
	str	r5, [r4, #488]
.L3915:
	str	r7, [sp]
	mov	r3, r5
	ldr	r0, .L3938+12
	ldr	r1, [r4, #484]
	ldr	r2, [r9]
	bl	printk
	ldrb	r3, [r4, #480]	@ zero_extendqisi2
	ldr	r10, .L3938+4
	cmp	r3, #0
	beq	.L3914
	sub	r0, r5, #64
	ldr	r8, [r10, #484]
	cmp	r0, #500
	bcs	.L3917
	rsb	r2, r5, #564
	add	r0, r8, r0, asl #9
	cmp	r2, r7
	mov	r1, r9
	movcs	r2, r7
	mov	r2, r2, asl #9
	bl	ftl_memcpy
	b	.L3918
.L3917:
	cmp	r5, #564
	bcs	.L3926
.L3918:
	ldr	r3, [r4, #488]
	cmp	r3, r5
	beq	.L3924
	ldr	r2, .L3938+4
	mov	r3, #0
	cmp	r8, r3
	mov	r9, r3
	strb	r3, [r2, #480]
	beq	.L3925
	mov	r0, r8
	bl	ftl_free
.L3925:
	str	r9, [r4, #484]
.L3924:
	add	r5, r5, r7
	str	r5, [r4, #488]
	b	.L3914
.L3926:
	ldr	r3, .L3938+16
	ldr	r0, [r10, #488]
	ldr	r3, [r3, #692]
	sub	r0, r0, #64
	cmp	r0, #500
	ldrb	r3, [r3, #9]	@ zero_extendqisi2
	movcs	r0, #500
	cmp	r3, #4
	beq	.L3928
	mov	r3, #2
	str	r3, [sp, #12]
	mov	r3, #3
	str	r3, [sp, #16]
	mov	r3, #4
	str	r3, [sp, #20]
	mov	r3, #5
	str	r3, [sp, #24]
	mov	r3, #6
	str	r3, [sp, #28]
	b	.L3920
.L3928:
	mov	r3, #0
.L3919:
	cmp	r0, #256
	add	r1, sp, #12
	mov	r2, r3, asl #1
	movls	r2, r3
	str	r2, [r1, r3, asl #2]
	add	r3, r3, #1
	cmp	r3, #5
	bne	.L3919
.L3920:
	movw	r3, #63872
.L3923:
	ldr	r2, [r8, r3, asl #2]
	cmp	r2, #0
	addne	r3, r3, #128
	movne	r0, r3, asl #2
	bne	.L3922
.L3921:
	sub	r3, r3, #1
	cmp	r3, #4096
	bne	.L3923
	mov	r0, r0, asl #9
.L3922:
	mov	r1, r8
	add	r2, sp, #12
	bl	write_idblock
	ldr	r0, [r4, #484]
	mov	r8, #0
	strb	r8, [r4, #480]
	bl	ftl_free
	str	r8, [r4, #484]
	b	.L3924
.L3914:
	ldr	r2, [sp, #52]
	ldr	r3, [r6]
	cmp	r2, r3
	beq	.L3927
	bl	__stack_chk_fail
.L3927:
	add	sp, sp, #56
	@ sp needed
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, pc}
.L3939:
	.align	2
.L3938:
	.word	__stack_chk_guard
	.word	.LANCHOR4
	.word	-52655045
	.word	.LC179
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
	beq	.L3941
	mov	r0, r1
	mov	r2, r3
	mov	r1, r6
	bl	write_loader_lba
.L3941:
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
	stmfd	sp!, {r4, r5, r6, r7, r8, lr}
	.save {r4, r5, r6, r7, r8, lr}
	.pad #528
	sub	sp, sp, #528
	ldr	r5, .L4053
	mov	r4, r1
	mov	r6, r2
	ldr	r3, [r5]
	str	r3, [sp, #524]
	ldr	r3, .L4053+4
	cmp	r1, r3
	beq	.L3948
	bhi	.L3949
	sub	r3, r3, #3456
	sub	r3, r3, #5
	cmp	r1, r3
	beq	.L3950
	bhi	.L3951
	sub	r3, r3, #125
	cmp	r1, r3
	beq	.L3952
	bhi	.L3953
	sub	r3, r3, #237
	cmp	r1, r3
	bne	.L4010
	bl	rknand_dev_flush
	b	.L4041
.L3953:
	ldr	r3, .L4053+8
	cmp	r1, r3
	beq	.L3955
	add	r3, r3, #1
	cmp	r1, r3
	beq	.L3956
	b	.L4010
.L3951:
	ldr	r3, .L4053+12
	cmp	r1, r3
	beq	.L3957
	bhi	.L3958
	sub	r3, r3, #1952
	sub	r3, r3, #9
	cmp	r1, r3
	beq	.L3959
	b	.L4010
.L3958:
	ldr	r3, .L4053+16
	cmp	r1, r3
	beq	.L3957
	add	r3, r3, #10
	cmp	r1, r3
	beq	.L3957
	b	.L4010
.L3949:
	ldr	r3, .L4053+20
	cmp	r1, r3
	beq	.L3960
	bhi	.L3961
	sub	r3, r3, #78
	cmp	r1, r3
	beq	.L3962
	bcc	.L3963
	add	r3, r3, #21
	cmp	r1, r3
	beq	.L3964
	add	r3, r3, #56
	cmp	r1, r3
	beq	.L3965
	b	.L4010
.L3961:
	ldr	r3, .L4053+24
	cmp	r1, r3
	mov	r7, r3
	beq	.L3966
	bhi	.L3967
	sub	r3, r3, #956
	sub	r3, r3, #1
	cmp	r1, r3
	beq	.L3968
	add	r3, r3, #956
	cmp	r1, r3
	beq	.L3969
	b	.L4010
.L3967:
	ldr	r3, .L4053+28
	cmp	r1, r3
	beq	.L3969
	add	r3, r3, #1
	cmp	r1, r3
	beq	.L3966
	b	.L4010
.L3963:
	ldr	r0, .L4053+32
	bl	printk
	mov	r0, #4096
	bl	ftl_malloc
	subs	r4, r0, #0
	bne	.L3970
.L3975:
	mvn	r0, #11
	b	.L3947
.L3970:
	mov	r1, r6
	mov	r2, #512
	bl	rk_copy_from_user
	cmp	r0, #0
	bne	.L4047
	ldr	r0, .L4053+36
	ldmia	r4, {r1, r2}
	bl	printk
	ldr	r3, [r4, #4]
	cmp	r3, #8
	str	r3, [sp]
	bhi	.L4045
	bl	rknand_device_lock
	mov	r2, r4
	ldr	r0, [r4]
	ldr	r1, [sp]
	bl	IdBlockReadData
	bl	rknand_device_unlock
	mov	r0, r6
	mov	r1, r4
	ldr	r2, [sp]
	mov	r2, r2, asl #9
	bl	rk_copy_to_user
	cmp	r0, #0
	beq	.L4040
	ldr	r0, .L4053+40
.L4043:
	bl	printk
.L4045:
	mov	r0, r4
.L4046:
	bl	ftl_free
.L4044:
	mvn	r0, #13
	b	.L3947
.L3962:
	ldr	r0, .L4053+44
	bl	printk
	mov	r0, #4096
	bl	ftl_malloc
	subs	r4, r0, #0
	beq	.L3975
	mov	r1, r6
	mov	r2, #4096
	bl	rk_copy_from_user
	cmp	r0, #0
	beq	.L3976
.L4047:
	ldr	r0, .L4053+48
	b	.L4043
.L3976:
	ldr	r6, .L4053+52
	ldr	r0, .L4053+56
	ldmia	r4, {r1, r2}
	bl	printk
	ldr	r7, [r6, #492]
	mov	r8, r6
	cmp	r7, #0
	bne	.L3977
	mov	r0, #260096
	bl	ftl_malloc
	cmp	r0, #0
	str	r0, [r6, #492]
	beq	.L4045
	mov	r1, r7
	mov	r2, #260096
	bl	memset
.L3977:
	ldr	r2, [r4, #4]
	movw	r3, #4088
	cmp	r2, r3
	bhi	.L4045
	ldr	r3, [r4]
	cmp	r3, #251904
	bhi	.L4045
	ldr	r0, [r8, #492]
	add	r1, r4, #8
	add	r0, r0, r3
	bl	memcpy
.L4040:
	mov	r0, r4
	bl	ftl_free
.L4041:
	mov	r4, #0
	b	.L3974
.L3965:
	ldr	r0, .L4053+60
	bl	printk
	mov	r0, #4096
	bl	ftl_malloc
	subs	r7, r0, #0
	beq	.L3975
	mov	r1, r6
	mov	r2, #28
	bl	rk_copy_from_user
	cmp	r0, #0
	beq	.L3980
	ldr	r0, .L4053+48
	bl	printk
	b	.L3981
.L3980:
	ldmia	r7, {r1, r2}
	ldr	r0, .L4053+64
	bl	printk
	ldr	r1, [r7]
	cmp	r1, #256000
	bhi	.L3981
	ldr	r6, .L4053+52
	ldr	r0, [r6, #492]
	cmp	r0, #0
	beq	.L3981
	bl	CRC_32
	ldr	r3, [r7, #4]
	cmp	r3, r0
	beq	.L3983
	mov	r0, r7
	bl	ftl_free
	b	.L4011
.L3983:
	bl	rknand_device_lock
	ldr	r1, [r6, #492]
	add	r2, r7, #8
	ldr	r0, [r7]
	bl	write_idblock
	mov	r4, #0
	bl	rknand_device_unlock
	ldr	r0, [r6, #492]
	bl	ftl_free
	str	r4, [r6, #492]
	mov	r0, r7
	b	.L3987
.L3981:
	mov	r0, r7
	b	.L4046
.L3964:
	ldr	r0, .L4053+68
	bl	printk
	mov	r0, #4096
	bl	ftl_malloc
	subs	r7, r0, #0
	beq	.L3975
	bl	ftl_read_flash_info
	mov	r0, r6
	mov	r1, r7
	mov	r2, #11
	b	.L4051
.L3948:
	ldr	r0, .L4053+72
	bl	printk
	bl	nand_blk_add_whole_disk
	mov	r0, #4096
	bl	ftl_malloc
	subs	r7, r0, #0
	beq	.L3975
	mov	r1, #0
	mov	r2, #64
	bl	ftl_memset
	mov	r0, r6
	mov	r1, r7
	mov	r2, #64
	b	.L4051
.L3960:
	ldr	r0, .L4053+76
	bl	printk
	ldr	r3, .L4053+52
	mov	r0, r6
	mov	r2, #4
	ldr	r3, [r3, #496]
	ldr	r3, [r3, #20]
	add	r1, sp, #528
	str	r3, [r1, #-528]!
.L4035:
	bl	rk_copy_to_user
	cmp	r0, #0
	bne	.L4044
	b	.L4041
.L3968:
	ldr	r0, .L4053+80
	bl	printk
	mov	r0, #4096
	bl	ftl_malloc
	subs	r7, r0, #0
	beq	.L3975
	bl	rknand_device_lock
	mov	r1, #264
	mov	r2, #2
	mov	r3, r7
	mov	r0, #16
	bl	ftl_read
	bl	rknand_device_unlock
	mov	r0, r6
	mov	r1, r7
	mov	r2, #1024
.L4051:
	bl	rk_copy_to_user
	subs	r4, r0, #0
	mov	r0, r7
	bne	.L4046
.L3987:
	bl	ftl_free
	b	.L3974
.L3952:
	ldr	r0, .L4053+84
	bl	printk
	mov	r1, r6
	mov	r2, #520
	add	r0, sp, #4
	bl	rk_copy_from_user
	subs	r4, r0, #0
	beq	.L3988
.L3993:
	ldr	r0, .L4053+48
	bl	printk
	b	.L4044
.L3988:
	ldr	r2, [sp, #4]
	ldr	r3, .L4053+88
	cmp	r2, r3
	beq	.L3989
.L3990:
	mvn	r4, #0
	b	.L3974
.L3989:
	ldr	r3, [sp, #8]
	cmp	r3, #512
	bhi	.L3990
	ldr	r7, .L4053+52
	mov	r2, #512
	add	r0, sp, #4
	ldr	r1, [r7, #496]
	bl	memcpy
	ldr	r2, [r7, #500]
	ldr	r3, .L4053+92
	cmp	r2, r3
	beq	.L3991
	add	r0, sp, #68
	mov	r1, r4
	mov	r2, #128
	str	r4, [sp, #12]
	str	r4, [sp, #16]
	bl	memset
.L3991:
	mov	r1, #0
	mov	r2, #256
	add	r0, sp, #260
	str	r1, [sp, #20]
	bl	memset
	b	.L4049
.L3955:
	ldr	r0, .L4053+96
	bl	printk
	mov	r1, r6
	mov	r2, #520
	add	r0, sp, #4
	bl	rk_copy_from_user
	cmp	r0, #0
	bne	.L3993
	ldr	r2, [sp, #4]
	ldr	r3, .L4053+88
	cmp	r2, r3
	bne	.L3990
	ldr	r3, [sp, #8]
	cmp	r3, #512
	bhi	.L3990
	ldr	r2, .L4053+52
	ldr	r3, .L4053+92
	ldr	r1, [r2, #500]
	cmp	r1, r3
	bne	.L4011
	ldr	r3, [sp, #16]
	sub	r1, r3, #1
	cmp	r1, #127
	mvnhi	r0, #2
	bhi	.L3947
	ldr	r4, [r2, #496]
	add	r1, sp, #68
	add	r0, r4, #64
	str	r3, [r4, #12]
	ldr	r2, [sp, #16]
	bl	memcpy
	mov	r0, #1
	mov	r1, r4
	b	.L4042
.L3959:
	ldr	r0, .L4053+100
	bl	printk
	mov	r1, r6
	mov	r2, #520
	add	r0, sp, #4
	bl	rk_copy_from_user
	cmp	r0, #0
	bne	.L3993
	ldr	r2, [sp, #4]
	ldr	r3, .L4053+104
	cmp	r2, r3
	bne	.L3990
	ldr	r3, [sp, #8]
	cmp	r3, #512
	bhi	.L3990
	ldr	r6, .L4053+52
	ldr	r3, [r6, #504]
	cmp	r3, #0
	bne	.L3994
.L3997:
	mov	r0, #0
	b	.L3947
.L3994:
	ldr	r3, [r6, #508]
	ldr	r2, .L4053+108
	ldr	r1, [r3]
	cmp	r1, r2
	beq	.L3995
	str	r2, [r3]
	mov	r2, #504
	ldr	r3, [r6, #508]
	str	r2, [r3, #4]
	mov	r2, #0
	str	r2, [r3, #8]
	str	r2, [r3, #12]
.L3995:
	ldr	r1, [r6, #508]
	mov	r4, #0
	mov	r0, r4
	str	r4, [r1, #16]
	bl	StorageSysDataStore
	ldr	r3, [r6, #496]
	ldr	r2, .L4053+88
	ldr	r1, [r3]
	cmp	r1, r2
	strne	r2, [r3]
	ldr	r7, [r6, #496]
	ldrne	r3, .L4053+52
	movne	r2, #504
	add	r0, r7, #64
	ldrne	r3, [r3, #496]
	stmneib	r3, {r2, r4}
	mov	r4, #0
	mov	r1, r4
	mov	r2, #128
	str	r4, [r7, #12]
	bl	memset
	mov	r0, #1
	mov	r1, r7
	bl	StorageSysDataStore
	str	r4, [r6, #504]
	str	r4, [r6, #500]
	b	.L3974
.L3950:
	ldr	r0, .L4053+112
	bl	printk
	mov	r1, r6
	mov	r2, #520
	add	r0, sp, #4
	bl	rk_copy_from_user
	cmp	r0, #0
	bne	.L3993
	ldr	r2, [sp, #4]
	ldr	r3, .L4053+116
	cmp	r2, r3
	bne	.L3990
	ldr	r3, [sp, #8]
	cmp	r3, #512
	bhi	.L3990
	ldr	r6, .L4053+52
	ldr	r3, [r6, #504]
	cmp	r3, #1
	beq	.L3997
	ldr	r3, [r6, #508]
	mov	r4, #0
	ldr	r2, .L4053+108
	ldr	r1, [r3]
	cmp	r1, r2
	strne	r2, [r3]
	ldr	r1, [r6, #508]
	ldrne	r3, [r6, #508]
	movne	r2, #504
	strne	r0, [r3, #8]
	strne	r2, [r3, #4]
	strne	r0, [r3, #12]
	mov	r3, #1
	mov	r0, #0
	str	r3, [r1, #16]
	bl	StorageSysDataStore
	ldr	r3, [r6, #496]
	ldr	r2, .L4053+88
	ldr	r1, [r3]
	cmp	r1, r2
	strne	r2, [r3]
	ldr	r7, [r6, #496]
	ldrne	r3, .L4053+52
	movne	r1, #504
	movne	r2, #0
	add	r0, r7, #64
	ldrne	r3, [r3, #496]
	stmneib	r3, {r1, r2}
	mov	r1, r4
	mov	r2, #128
	str	r4, [r7, #12]
	bl	memset
	mov	r0, #1
	mov	r1, r7
	bl	StorageSysDataStore
	mov	r3, #1
	str	r3, [r6, #504]
	b	.L3974
.L3956:
	ldr	r0, .L4053+120
	bl	printk
	mov	r1, r6
	mov	r2, #520
	add	r0, sp, #4
	bl	rk_copy_from_user
	cmp	r0, #0
	bne	.L3993
	ldr	r2, [sp, #4]
	ldr	r3, .L4053+124
	cmp	r2, r3
	bne	.L3990
	ldr	r2, [sp, #8]
	cmp	r2, #512
	addls	r0, sp, #12
	ldrls	r1, .L4053+128
	bls	.L4048
	b	.L3990
.L3957:
	ldr	r8, .L4053+16
	cmp	r4, r8
	ldreq	r0, .L4053+132
	beq	.L4038
	ldr	r3, .L4053+136
	cmp	r4, r3
	ldreq	r0, .L4053+140
	ldrne	r0, .L4053+144
.L4038:
	bl	printk
	mov	r1, r6
	mov	r2, #520
	add	r0, sp, #4
	bl	rk_copy_from_user
	cmp	r0, #0
	bne	.L3993
	ldr	r2, [sp, #4]
	ldr	r3, .L4053+148
	cmp	r2, r3
	bne	.L4044
	ldr	r3, .L4053+136
	ldr	r7, .L4053+52
	cmp	r4, r3
	bne	.L4003
	ldr	r3, [r7, #496]
	mov	r0, r6
	add	r1, sp, #4
	mov	r2, #16
	ldr	r3, [r3, #20]
	str	r3, [sp, #8]
	strb	r3, [sp, #12]
	bl	rk_copy_to_user
	cmp	r0, #0
	beq	.L3947
	b	.L4044
.L4003:
	ldr	r3, [r7, #1024]
	cmp	r3, #10
	bhi	.L4044
	ldr	r1, [r7, #496]
	ldr	r2, [sp, #8]
	ldr	r3, [r1, #24]
	cmp	r3, r2
	cmpne	r3, #0
	movne	r3, #1
	moveq	r3, #0
	beq	.L4004
	ldr	r0, .L4053+152
	mov	r1, r2
	bl	printk
	ldr	r3, [r7, #1024]
	add	r3, r3, #1
	str	r3, [r7, #1024]
	b	.L4044
.L4004:
	cmp	r4, r8
	str	r3, [r7, #1024]
	mov	r0, #1
	moveq	r2, r3
	movne	r3, #1
	moveq	r3, r2
	str	r2, [r1, #24]
	str	r3, [r1, #20]
	bl	StorageSysDataStore
	cmn	r0, #1
	bne	.L4041
	mvn	r4, #1
	b	.L3974
.L3969:
	ldr	r0, .L4053+156
	bl	printk
	mov	r1, r6
	mov	r2, #520
	add	r0, sp, #4
	bl	rk_copy_from_user
	cmp	r0, #0
	bne	.L3993
	ldr	r2, [sp, #4]
	ldr	r3, .L4053+160
	cmp	r2, r3
	bne	.L3990
	ldr	r2, [sp, #8]
	cmp	r2, #504
	bhi	.L3990
	ldr	r3, .L4053+164
	add	r0, sp, #12
	cmp	r4, r3
	ldr	r3, .L4053+52
	ldreq	r1, [r3, #1028]
	ldrne	r1, [r3, #1032]
	add	r1, r1, #8
.L4048:
	bl	memcpy
.L4049:
	mov	r0, r6
	add	r1, sp, #4
	mov	r2, #520
	b	.L4035
.L3966:
	ldr	r0, .L4053+168
	bl	printk
	mov	r1, r6
	mov	r2, #520
	add	r0, sp, #4
	bl	rk_copy_from_user
	cmp	r0, #0
	bne	.L3993
	ldr	r2, [sp, #4]
	ldr	r3, .L4053+160
	cmp	r2, r3
	bne	.L3990
	ldr	r2, [sp, #8]
	cmp	r2, #504
	bhi	.L3990
	cmp	r4, r7
	add	r2, r2, #8
	ldr	r4, .L4053+52
	bne	.L4008
	add	r1, sp, #4
	ldr	r0, [r4, #1028]
	bl	memcpy
	mov	r0, #2
	ldr	r1, [r4, #1028]
	b	.L4042
.L4008:
	add	r1, sp, #4
	ldr	r0, [r4, #1032]
	bl	memcpy
	ldr	r1, [r4, #1032]
	mov	r0, #3
.L4042:
	bl	StorageSysDataStore
	mov	r4, r0
.L3974:
	ldr	r0, .L4053+172
	mov	r1, r4
	bl	printk
	mov	r0, r4
	b	.L3947
.L4010:
	mvn	r0, #21
	b	.L3947
.L4011:
	mvn	r0, #1
.L3947:
	ldr	r2, [sp, #524]
	ldr	r3, [r5]
	cmp	r2, r3
	beq	.L4009
	bl	__stack_chk_fail
.L4009:
	add	sp, sp, #528
	@ sp needed
	ldmfd	sp!, {r4, r5, r6, r7, r8, pc}
.L4054:
	.align	2
.L4053:
	.word	__stack_chk_guard
	.word	1074033155
	.word	1074029570
	.word	1074031656
	.word	1074031666
	.word	1074033235
	.word	1074034193
	.word	1074034194
	.word	.LC180
	.word	.LC182
	.word	.LC183
	.word	.LC184
	.word	.LC181
	.word	.LANCHOR4
	.word	.LC185
	.word	.LC186
	.word	.LC187
	.word	.LC188
	.word	.LC189
	.word	.LC190
	.word	.LC191
	.word	.LC192
	.word	1263358532
	.word	-1067903959
	.word	.LC193
	.word	.LC194
	.word	1112753220
	.word	1146313043
	.word	.LC195
	.word	1112755781
	.word	.LC196
	.word	1094995539
	.word	.LANCHOR4+512
	.word	.LC197
	.word	1074031676
	.word	.LC198
	.word	.LC199
	.word	1280262987
	.word	.LC200
	.word	.LC201
	.word	1145980246
	.word	1074034192
	.word	.LC202
	.word	.LC203
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
	ldr	r4, .L4065
	mvn	r3, #0
	mov	r5, #0
	add	r0, r4, #512
	ldr	r1, [r4, #464]
	str	r3, [r4, #488]
	add	r3, r1, #512
	str	r3, [r4, #496]
	str	r1, [r4, #508]
	add	r3, r1, #1024
	add	r1, r1, #1536
	str	r3, [r4, #1028]
	strb	r5, [r4, #480]
	str	r5, [r4, #484]
	str	r5, [r4, #492]
	str	r1, [r4, #1032]
	bl	ftl_memcpy
	ldr	r6, [r4, #508]
	str	r5, [r4, #500]
	str	r5, [r4, #1024]
	ldr	r7, [r6, #508]
	ldr	r3, [r6, #16]
	cmp	r7, r5
	str	r3, [r4, #504]
	beq	.L4056
	mov	r0, r6
	mov	r1, #508
	bl	js_hash
	cmp	r7, r0
	beq	.L4056
	str	r5, [r6, #16]
	ldr	r0, .L4065+4
	str	r5, [r4, #504]
	bl	printk
.L4056:
	ldr	r3, [r4, #504]
	mov	r0, #2
	ldr	r1, [r4, #1028]
	cmp	r3, #0
	ldrne	r3, .L4065
	ldrne	r2, .L4065+8
	strne	r2, [r3, #500]
	bl	StorageSysDataLoad
	ldr	r1, [r4, #1032]
	mov	r0, #3
	bl	StorageSysDataLoad
	ldmfd	sp!, {r3, r4, r5, r6, r7, lr}
	b	rknand_sys_storage_init
.L4066:
	.align	2
.L4065:
	.word	.LANCHOR4
	.word	.LC204
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
	ldr	r6, .L4079
	cmp	r0, #0
	str	r0, [r6, #1036]
	beq	.L4074
	ldr	r9, .L4079+4
	mov	r8, #0
	mov	r4, r8
	mov	r7, r8
	mov	r10, r6
.L4072:
	mov	r0, r7, asl #7
	mov	r1, #128
	ldr	r2, [r6, #1036]
	bl	FlashBootVendorRead
	cmp	r0, #0
	bne	.L4070
	ldr	r3, [r10, #1036]
	ldr	r0, .L4079+8
	add	r2, r3, #61440
	ldr	r1, [r3]
	ldr	r2, [r2, #4092]
	ldr	r3, [r3, #4]
	bl	printk
	ldr	r5, [r10, #1036]
	ldr	r3, [r5]
	cmp	r3, r9
	bne	.L4071
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
.L4071:
	cmp	r7, #1
	movne	r7, #1
	bne	.L4072
.L4078:
	cmp	r4, #0
	beq	.L4073
	mov	r0, r8, asl #7
	mov	r1, #128
	mov	r2, r5
	bl	FlashBootVendorRead
	cmp	r0, #0
	bne	.L4070
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, pc}
.L4073:
	mov	r0, r5
	mov	r1, r4
	mov	r2, #65536
	bl	memset
	ldr	r3, .L4079+4
	str	r7, [r5, #4]
	mov	r0, r4
	str	r3, [r5]
	add	r3, r5, #61440
	str	r7, [r3, #4092]
	ldr	r3, .L4079+12
	strh	r4, [r5, #12]	@ movhi
	strh	r3, [r5, #14]	@ movhi
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, pc}
.L4070:
	ldr	r0, [r6, #1036]
	bl	kfree
	mov	r3, #0
	mvn	r0, #0
	str	r3, [r6, #1036]
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, pc}
.L4074:
	mvn	r0, #11
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, pc}
.L4080:
	.align	2
.L4079:
	.word	.LANCHOR4
	.word	1380668996
	.word	.LC205
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
	ldr	r3, .L4088
	ldr	ip, [r3, #1036]
	cmp	ip, #0
	ldrneh	r4, [ip, #10]
	movne	r3, #0
	beq	.L4086
.L4083:
	cmp	r3, r4
	bcs	.L4086
	add	lr, ip, r3, asl #3
	ldrh	r5, [lr, #16]
	cmp	r5, r0
	bne	.L4084
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
.L4084:
	add	r3, r3, #1
	b	.L4083
.L4086:
	mvn	r0, #0
	ldmfd	sp!, {r3, r4, r5, pc}
.L4089:
	.align	2
.L4088:
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
	ldr	r2, .L4113
	.pad #28
	sub	sp, sp, #28
	ldr	r4, [r2, #1036]
	cmp	r4, #0
	beq	.L4105
	mov	r3, r1
	add	r7, r9, #63
	ldrh	r1, [r4, #8]
	bic	r7, r7, #63
	ldrh	r2, [r4, #10]
	mov	ip, r0
	mov	r6, #0
	str	r1, [sp, #4]
.L4092:
	cmp	r6, r2
	bcs	.L4111
	add	r5, r4, r6, asl #3
	ldrh	r1, [r5, #16]
	cmp	r1, ip
	bne	.L4093
	ldrh	r1, [r5, #20]
	add	fp, r4, #1024
	add	r1, r1, #63
	bic	r1, r1, #63
	str	r1, [sp, #8]
	cmp	r9, r1
	bls	.L4094
	ldrh	r1, [r4, #14]
	cmp	r1, r7
	subcs	r2, r2, #1
	strcs	r2, [sp, #12]
	ldrcsh	r8, [r5, #18]
	bcc	.L4105
.L4095:
	ldr	r2, [sp, #12]
	add	r5, r5, #8
	cmp	r6, r2
	bcs	.L4112
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
	b	.L4095
.L4112:
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
	b	.L4110
.L4094:
	ldrh	r0, [r5, #18]
	mov	r1, r3
	mov	r2, r9
	add	r0, fp, r0
	bl	memcpy
	strh	r9, [r5, #20]	@ movhi
	b	.L4110
.L4093:
	add	r6, r6, #1
	b	.L4092
.L4111:
	ldrh	r1, [r4, #14]
	cmp	r1, r7
	bcc	.L4105
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
.L4110:
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
	b	.L4091
.L4105:
	mvn	r0, #0
.L4091:
	add	sp, sp, #28
	@ sp needed
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L4114:
	.align	2
.L4113:
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
	beq	.L4116
	ldr	r3, .L4132
	cmp	r6, r3
	beq	.L4118
	add	r3, r3, #1
	cmp	r6, r3
	beq	.L4119
	b	.L4130
.L4118:
	mov	r1, r5
	mov	r2, #8
	bl	rk_copy_from_user
	cmp	r0, #0
	bne	.L4130
	ldr	r2, [r4]
	ldr	r3, .L4132+4
	cmp	r2, r3
	beq	.L4121
.L4122:
	mvn	r5, #0
	b	.L4117
.L4121:
	ldrh	r0, [r4, #4]
	add	r1, r4, #8
	ldrh	r2, [r4, #6]
	bl	rk_ftl_vendor_read
	cmn	r0, #1
	beq	.L4122
	uxth	r2, r0
	strh	r0, [r4, #6]	@ movhi
	mov	r1, r4
	mov	r0, r5
	add	r2, r2, #8
	bl	rk_copy_to_user
	cmp	r0, #0
	moveq	r5, #0
	mvnne	r5, #13
	b	.L4117
.L4119:
	mov	r1, r5
	mov	r2, #8
	bl	rk_copy_from_user
	cmp	r0, #0
	bne	.L4130
	ldr	r2, [r4]
	ldr	r3, .L4132+4
	cmp	r2, r3
	bne	.L4122
	ldrh	r2, [r4, #6]
	movw	r3, #4087
	cmp	r2, r3
	bhi	.L4122
	mov	r0, r4
	mov	r1, r5
	add	r2, r2, #8
	bl	rk_copy_from_user
	cmp	r0, #0
	bne	.L4130
	ldrh	r0, [r4, #4]
	add	r1, r4, #8
	ldrh	r2, [r4, #6]
	bl	rk_ftl_vendor_write
	mov	r5, r0
	b	.L4117
.L4130:
	mvn	r5, #13
.L4117:
	mov	r0, r4
	bl	kfree
.L4116:
	mov	r0, r5
	ldmfd	sp!, {r4, r5, r6, pc}
.L4133:
	.align	2
.L4132:
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
	.type	__func__.20216, %object
	.size	__func__.20216, 11
__func__.20216:
	.ascii	"FtlMemInit\000"
.LC0:
	.byte	60
	.byte	40
	.byte	24
	.byte	16
	.space	1
	.type	__func__.20991, %object
	.size	__func__.20991, 17
__func__.20991:
	.ascii	"FtlDumpBlockInfo\000"
	.space	3
	.type	__func__.21010, %object
	.size	__func__.21010, 16
__func__.21010:
	.ascii	"FtlScanAllBlock\000"
	.type	__func__.21276, %object
	.size	__func__.21276, 17
__func__.21276:
	.ascii	"ftl_scan_all_ppa\000"
	.space	3
	.type	__func__.20959, %object
	.size	__func__.20959, 12
__func__.20959:
	.ascii	"FtlCheckVpc\000"
	.type	__func__.21258, %object
	.size	__func__.21258, 21
__func__.21258:
	.ascii	"FtlVpcCheckAndModify\000"
	.space	3
	.type	__func__.20289, %object
	.size	__func__.20289, 8
__func__.20289:
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
	.ascii	"FTL version: 5.0.56 20190508\000"
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
	.ascii	"...%s enter...\012\000"
.LC108:
	.ascii	"superBlkID = %x vpc=%x\012\000"
.LC109:
	.ascii	"flashmode = %x pagenum = %x %x\012\000"
.LC110:
	.ascii	"id = %x,%x addr= %x,spare= %x %x %x %x data=%x %x\012"
	.ascii	"\000"
.LC111:
	.ascii	"blk = %x vpc=%x mode = %x\012\000"
.LC112:
	.ascii	"mlc id = %x,%x addr= %x,spare= %x %x %x %x data=%x "
	.ascii	"%x\012\000"
.LC113:
	.ascii	"slc id = %x,%x addr= %x,spare= %x %x %x %x data=%x "
	.ascii	"%x\012\000"
.LC114:
	.ascii	"ftl_scan_all_ppa blk %x page %x\012\000"
.LC115:
	.ascii	"addr= %x, status= %d,spare= %x %x %x %x data=%x %x\012"
	.ascii	"\000"
.LC116:
	.ascii	"%s finished\012\000"
.LC117:
	.ascii	"FlashMakeFactorBbt %d\012\000"
.LC118:
	.ascii	"bad block:%d %d\012\000"
.LC119:
	.ascii	"FMFB:%d %d\012\000"
.LC120:
	.ascii	"E:bad block:%d\012\000"
.LC121:
	.ascii	"FMFB:Save %d %d\012\000"
.LC122:
	.ascii	"FtlBbmTblFlush id=%x,page=%x,previd=%x cnt=%d\012\000"
.LC123:
	.ascii	"FtlBbmTblFlush error:%x\012\000"
.LC124:
	.ascii	"FtlBbmTblFlush error = %x error count = %d\012\000"
.LC125:
	.ascii	"FtlGcFreeBadSuperBlk 0x%x\012\000"
.LC126:
	.ascii	"decrement_vpc_count %x = %d\012\000"
.LC127:
	.ascii	"decrement_vpc_count %x = %d in free list\012\000"
.LC128:
	.ascii	"FtlVpcTblFlush error = %x error count = %d\012\000"
.LC129:
	.ascii	"page map lost: %x %x\012\000"
.LC130:
	.ascii	"FtlMapWritePage error = %x\012\000"
.LC131:
	.ascii	"FtlMapWritePage error = %x error count = %d\012\000"
.LC132:
	.ascii	"FtlVendorPartRead refresh = %x phyAddr = %x\012\000"
.LC133:
	.ascii	"no ect\000"
.LC134:
	.ascii	"slc mode\000"
.LC135:
	.ascii	"BBT:\000"
.LC136:
	.ascii	"region_id = %x phyAddr = %x\012\000"
.LC137:
	.ascii	"map_ppn:\000"
.LC138:
	.ascii	"load_l2p_region refresh = %x phyAddr = %x\012\000"
.LC139:
	.ascii	"FtlCheckVpc2 %x = %x  %x\012\000"
.LC140:
	.ascii	"free blk vpc error %x = %x  %x\012\000"
.LC141:
	.ascii	"Ftlscanalldata = %x\012\000"
.LC142:
	.ascii	"scan lpa = %x ppa= %x\012\000"
.LC143:
	.ascii	"lba = %x,addr= %x,spare= %x %x %x %x data=%x %x\012"
	.ascii	"\000"
.LC144:
	.ascii	"RSB refresh addr %x\012\000"
.LC145:
	.ascii	"spuer block %x vpn is 0\012 \000"
.LC146:
	.ascii	"g_recovery_ppa %x ver %x\012 \000"
.LC147:
	.ascii	"FtlCheckVpc %x = %x  %x\012\000"
.LC148:
	.ascii	"FtlGcScanTempBlk Error ID %x %x!!!!!!! \012\000"
.LC149:
	.ascii	"FtlGcScanTempBlkError ID %x %x!!!!!!!\012\000"
.LC150:
	.ascii	"GC des block %x done\012\000"
.LC151:
	.ascii	"%d GC datablk  = %x vpc %x %x\012\000"
.LC152:
	.ascii	"SWL %x, FSB = %x vpc= %x,ec=%x th=%x\012\000"
.LC153:
	.ascii	"Ftlwrite decrement_vpc_count %x = %d\012\000"
.LC154:
	.ascii	"rk_ftl_de_init %x\012\000"
.LC155:
	.ascii	"...%s: no bad block mapping table, format device\012"
	.ascii	"\000"
.LC156:
	.ascii	"...%s FtlSysBlkInit error ,format device!\012\000"
.LC157:
	.ascii	"FtlInit %x\012\000"
.LC158:
	.ascii	"fix power lost blk = %x vpc=%x\012\000"
.LC159:
	.ascii	"erase power lost blk = %x vpc=%x\012\000"
.LC160:
	.ascii	"FtlWrite: lpa error:%x %x\012\000"
.LC161:
	.ascii	"id = %x,%x addr= %x,spare= %x %x %x %x data = %x\012"
	.ascii	"\000"
.LC162:
	.ascii	":\000"
.LC163:
	.ascii	"phyBlk = %x,addr= %x,spare= %x %x %x %x data=%x %x\012"
	.ascii	"\000"
.LC164:
	.ascii	"Mblk:\000"
.LC165:
	.ascii	"L2P:\000"
.LC166:
	.ascii	"L2PC:\000"
.LC167:
	.ascii	"IdBlockReadData %x %x\012\000"
.LC168:
	.ascii	"IdBlockReadData %x %x ret= %x\012\000"
.LC169:
	.ascii	"write_idblock fix data %x %x\012\000"
.LC170:
	.ascii	"idblk:\000"
.LC171:
	.ascii	"idb reverse %x %x\012\000"
.LC172:
	.ascii	"write_idblock totle_sec %x %x %x %x\012\000"
.LC173:
	.ascii	"IDBlockWriteData %x %x\012\000"
.LC174:
	.ascii	"IDBlockWriteData %x %x ret= %x\012\000"
.LC175:
	.ascii	"write and check error:%d idb=%x,offset=%x,r=%x,w=%x"
	.ascii	"\012\000"
.LC176:
	.ascii	"write\000"
.LC177:
	.ascii	"read\000"
.LC178:
	.ascii	"write_idblock error %d\012\000"
.LC179:
	.ascii	"wl_lba %p %x %x %x\012\000"
.LC180:
	.ascii	"READ_SECTOR_IO\012\000"
.LC181:
	.ascii	"rk_copy_from_user error\012\000"
.LC182:
	.ascii	"READ_SECTOR_IO %x %x\012\000"
.LC183:
	.ascii	"rk_copy_to_user error\012\000"
.LC184:
	.ascii	"WRITE_SECTOR_IO\012\000"
.LC185:
	.ascii	"WRITE_SECTOR_IO %x %x\012\000"
.LC186:
	.ascii	"END_WRITE_SECTOR_IO\012\000"
.LC187:
	.ascii	"END_WRITE_SECTOR_IO %x %x\012\000"
.LC188:
	.ascii	"GET_FLASH_INFO_IO\012\000"
.LC189:
	.ascii	"GET_BAD_BLOCK_IO\012\000"
.LC190:
	.ascii	"GET_LOCK_FLAG_IO\012\000"
.LC191:
	.ascii	"GET_PUBLIC_KEY_IO\012\000"
.LC192:
	.ascii	"RKNAND_GET_DRM_KEY\012\000"
.LC193:
	.ascii	"RKNAND_STORE_DRM_KEY\012\000"
.LC194:
	.ascii	"RKNAND_DIASBLE_SECURE_BOOT\012\000"
.LC195:
	.ascii	"RKNAND_ENASBLE_SECURE_BOOT\012\000"
.LC196:
	.ascii	"RKNAND_GET_SN_SECTOR\012\000"
.LC197:
	.ascii	"RKNAND_LOADER_UNLOCK\012\000"
.LC198:
	.ascii	"RKNAND_LOADER_STATUS\012\000"
.LC199:
	.ascii	"RKNAND_LOADER_LOCK\012\000"
.LC200:
	.ascii	"LockKey not match %d\012\000"
.LC201:
	.ascii	"RKNAND_GET_VENDOR_SECTOR\012\000"
.LC202:
	.ascii	"RKNAND_STORE_VENDOR_SECTOR\012\000"
.LC203:
	.ascii	"return ret = %lx\012\000"
.LC204:
	.ascii	"secureBootEn check error\012\000"
.LC205:
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
	.size	slcPageToMlcPageTbl, 512
slcPageToMlcPageTbl:
	.space	512
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
	.size	mlcPageToSlcPageTbl, 1024
mlcPageToSlcPageTbl:
	.space	1024
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
