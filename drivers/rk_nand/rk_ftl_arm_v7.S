/*
 * Copyright (c) 2016-2018, Fuzhou Rockchip Electronics Co., Ltd
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 * date: 2019-09-29
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
	ldr	r3, .L86
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
.L87:
	.align	2
.L86:
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
	ldr	r0, .L94
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
	bne	.L90
	ldrb	r3, [fp, #140]	@ zero_extendqisi2
	cmp	r3, #0
	ldreq	r3, .L94
	addeq	r4, r3, r4, asl #1
	ldreqh	r4, [r4, #144]
.L90:
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
.L95:
	.align	2
.L94:
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
	ldr	r3, .L97
	ldr	r2, [r3, #688]
	str	r0, [r3, #688]
	mov	r0, r2
	bx	lr
.L98:
	.align	2
.L97:
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
	ldr	r3, .L100
	ldr	r3, [r3, #692]
	ldrb	r0, [r3, #9]	@ zero_extendqisi2
	bx	lr
.L101:
	.align	2
.L100:
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
	ldr	r3, .L104
	add	r0, r3, r0, asl #3
	ldrb	r2, [r0, #16]	@ zero_extendqisi2
	ldr	r0, [r0, #12]
	add	r2, r2, #8
	add	r0, r0, r2, asl #8
	bx	lr
.L105:
	.align	2
.L104:
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
	ldr	r3, .L107
	ldr	r2, [r3, #76]
	mov	r3, r0, asl #8
	orr	r0, r3, r0, asl #16
	orr	r3, r0, #1
	str	r3, [r2, #304]
	bx	lr
.L108:
	.align	2
.L107:
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
	ldr	r3, .L110
	orr	r0, r0, #16640
	ldr	r3, [r3, #76]
	str	r0, [r3, #344]
	bx	lr
.L111:
	.align	2
.L110:
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
	ldr	r3, .L115
	cmp	r0, #0
	ldr	r2, [r3, #76]
	ldr	r3, [r2]
	bfieq	r3, r0, #13, #1
	orrne	r3, r3, #253952
	str	r3, [r2]
	bx	lr
.L116:
	.align	2
.L115:
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
	ldr	r3, .L124
	ands	r1, r0, #6
	ldr	r2, [r3, #76]
	ldr	r3, [r2]
	bfieq	r3, r1, #13, #1
	beq	.L120
	orr	r3, r3, #24576
	movw	r1, #8322
	bfc	r3, #15, #1
	str	r1, [r2, #344]
	orr	r3, r3, #196608
	ldr	r1, .L124+4
	tst	r0, #4
	orrne	r3, r3, #32768
	str	r1, [r2, #304]
	mov	r1, #38
	str	r1, [r2, #308]
	mov	r1, #39
	str	r1, [r2, #308]
.L120:
	str	r3, [r2]
	mov	r0, #0
	bx	lr
.L125:
	.align	2
.L124:
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
	ldr	r2, .L127
	add	r0, r2, r0, asl #3
	mov	r2, #1
	ldr	r1, [r0, #12]
	ldrb	r0, [r0, #16]	@ zero_extendqisi2
	ldr	r3, [r1]
	mov	r2, r2, asl r0
	bfi	r3, r2, #0, #8
	str	r3, [r1]
	bx	lr
.L128:
	.align	2
.L127:
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
	ldr	r3, .L130
	add	r0, r3, r0, asl #3
	ldr	r2, [r0, #12]
	ldr	r3, [r2]
	bfc	r3, #0, #8
	bfc	r3, #17, #1
	str	r3, [r2]
	bx	lr
.L131:
	.align	2
.L130:
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
	ldr	r3, .L134
	add	r0, r0, #3
	umull	r0, r1, r0, r3
	ldr	r3, .L134+4
	ldr	r3, [r3, #8]
	mov	r0, r1, lsr #6
	blx	r3
	mov	r0, #0
	ldmfd	sp!, {r3, pc}
.L135:
	.align	2
.L134:
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
	ldr	r3, .L138
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
.L139:
	.align	2
.L138:
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
	ldr	r9, .L150
	mov	r5, r0
	ldr	r7, .L150+4
	mov	r6, r1
	add	r10, r9, #256
	mov	r4, #0
.L141:
	ldrb	r3, [r7, #697]	@ zero_extendqisi2
	cmp	r4, r3
	bcs	.L149
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
	beq	.L148
	cmp	r3, #35
	addne	r3, r9, r6
	addne	r3, r3, #400
	ldrnesb	r3, [r3]
	bne	.L147
	ldr	r3, .L150+8
	add	r2, r4, r8
	add	r3, r3, r2
.L148:
	ldrsb	r3, [r3, #5]
.L147:
	str	r3, [r5]
	add	r4, r4, #1
	b	.L141
.L149:
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, pc}
.L151:
	.align	2
.L150:
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
	ldr	r3, .L157
	stmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, lr}
	.save {r4, r5, r6, r7, r8, r9, r10, lr}
	add	r1, r3, r1, asl #2
	ldr	r8, .L157+4
	mov	r4, #0
	add	r5, r1, #3
	mov	r6, r0
	mov	r7, r3
	mov	r9, #161
	mov	r10, r4
.L153:
	ldrb	r3, [r8, #697]	@ zero_extendqisi2
	cmp	r4, r3
	bcs	.L156
	str	r9, [r6, #8]
	mov	r0, #300
	str	r10, [r6]
	ldrsb	r3, [r7, r4]
	add	r4, r4, #1
	str	r3, [r6]
	ldrsb	r3, [r5, #1]!
	str	r3, [r6]
	bl	NandcDelayns
	b	.L153
.L156:
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, pc}
.L158:
	.align	2
.L157:
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
	ldr	r5, .L168
	mov	r9, r2
	ldr	r4, .L168+4
	mov	r7, r0
	mov	r8, r1
	ldr	r3, [r5, #692]
	ldrb	r2, [r3, #19]	@ zero_extendqisi2
	mov	r3, r0, asl #3
	cmp	r2, #6
	addeq	r4, r4, r0, asl #6
	addeq	r4, r4, #20
	addeq	r4, r4, r6, asl #2
	beq	.L161
	cmp	r2, #7
	bne	.L162
	mov	r2, #160
	mla	r4, r2, r0, r4
	add	r2, r6, r6, asl #2
	add	r4, r4, #28
	add	r4, r4, r2, asl #1
	b	.L161
.L162:
	cmp	r2, #8
	addne	r2, r6, r3
	addeq	r2, r6, r6, asl #2
	ldreq	r4, .L168+8
	addne	r4, r4, r2, asl #3
	addeq	r4, r4, r2
	addne	r4, r4, #20
.L161:
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
.L164:
	cmp	ip, r9
	beq	.L167
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
	b	.L164
.L167:
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
.L169:
	.align	2
.L168:
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
	ldr	r3, .L178
	ldr	r2, [r3, #692]
	ldrb	r2, [r2, #19]	@ zero_extendqisi2
	sub	r2, r2, #1
	cmp	r2, #7
	bxhi	lr
	stmfd	sp!, {r4, r5, r6, lr}
	.save {r4, r5, r6, lr}
	mov	r4, #0
	ldr	r6, .L178+4
	mov	r5, r3
.L171:
	ldrb	r3, [r6, r4, asl #3]	@ zero_extendqisi2
	uxtb	r0, r4
	cmp	r3, #173
	bne	.L172
	ldrb	r1, [r5, #699]	@ zero_extendqisi2
	mov	r3, #0
	ldr	r2, .L178+8
	bl	HynixSetRRPara
.L172:
	add	r4, r4, #1
	cmp	r4, #4
	bne	.L171
	ldmfd	sp!, {r4, r5, r6, pc}
.L179:
	.align	2
.L178:
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
	ldr	ip, .L191
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
	bne	.L185
	cmp	r2, #0
	add	r3, r4, r3
	ldrneb	r2, [ip, #58]	@ zero_extendqisi2
	ldreqb	r2, [ip, #57]	@ zero_extendqisi2
	str	r2, [r3, #2056]
	ldrb	r0, [ip, #59]	@ zero_extendqisi2
	cmp	r0, #0
	addne	ip, r4, r5, asl #8
	movne	r2, #0
	beq	.L185
.L184:
	cmp	r2, r0
	bcs	.L185
	mov	r3, r2, asl #3
	add	r2, r2, #1
	mov	r3, r1, lsr r3
	uxtb	r3, r3
	str	r3, [ip, #4]
	b	.L184
.L185:
	mov	r0, #80
	bl	NandcDelayns
	ldr	r0, [r4, r5, asl #8]
	uxtb	r0, r0
	ldmfd	sp!, {r3, r4, r5, pc}
.L192:
	.align	2
.L191:
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
	beq	.L197
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
	ldr	r5, .L209
	add	r4, r5, r0, asl #4
	ldr	r3, [r4, #1596]
	ldrb	r7, [r4, #1588]	@ zero_extendqisi2
	cmp	r3, #0
	beq	.L203
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
.L203:
	mov	r0, #0
	ldmfd	sp!, {r3, r4, r5, r6, r7, pc}
.L210:
	.align	2
.L209:
	.word	.LANCHOR0
	.fnend
	.size	FlashWaitCmdDone, .-FlashWaitCmdDone
	.align	2
	.type	flash_read_ecc, %function
flash_read_ecc:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	ldr	r2, .L213
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
.L214:
	.align	2
.L213:
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
	ldr	r3, .L221
	stmfd	sp!, {r0, r1, r2, r4, r5, lr}
	.save {r4, r5, lr}
	.pad #12
	add	r0, r3, r0, asl #3
	ldr	r4, .L221+4
	ldr	r5, [r0, #12]
.L217:
	ldr	r3, [r5]
	str	r3, [sp, #4]
	ldr	r3, [sp, #4]
	tst	r3, #512
	bne	.L218
	mov	r0, #10
	bl	NandcDelayns
	subs	r4, r4, #1
	bne	.L217
	mvn	r0, #0
	b	.L216
.L218:
	mov	r0, #0
.L216:
	add	sp, sp, #12
	@ sp needed
	ldmfd	sp!, {r4, r5, pc}
.L222:
	.align	2
.L221:
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
	ldr	r3, .L229
	stmfd	sp!, {r0, r1, r2, r4, r5, lr}
	.save {r4, r5, lr}
	.pad #12
	add	r0, r3, r0, asl #3
	ldr	r4, .L229+4
	ldr	r5, [r0, #12]
	mov	r0, #150
	bl	NandcDelayns
.L225:
	ldr	r3, [r5]
	str	r3, [sp, #4]
	ldr	r3, [sp, #4]
	tst	r3, #512
	bne	.L226
	mov	r0, #10
	bl	NandcDelayns
	subs	r4, r4, #1
	bne	.L225
	mvn	r0, #0
	b	.L224
.L226:
	mov	r0, #0
.L224:
	add	sp, sp, #12
	@ sp needed
	ldmfd	sp!, {r4, r5, pc}
.L230:
	.align	2
.L229:
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
	ldr	r3, .L233
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
.L234:
	.align	2
.L233:
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
	ldr	r4, .L238
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
.L239:
	.align	2
.L238:
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
	ldr	r4, .L243
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
.L244:
	.align	2
.L243:
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
	ldr	r1, .L270
	stmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.save {r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.pad #20
	sub	sp, sp, #20
	ldrb	fp, [r1, #1716]	@ zero_extendqisi2
	mov	lr, #0
	ldr	r7, .L270+4
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
.L257:
	ldr	r3, [sp, #12]
	ldrb	ip, [lr, r7]	@ zero_extendqisi2
	ldr	r1, [r3, lr]!
	cmp	ip, #69
	cmpne	ip, #152
	ldrb	r3, [r3, #4]	@ zero_extendqisi2
	beq	.L248
	cmp	ip, #44
	cmpne	ip, #173
	bne	.L249
.L248:
	cmp	r0, #1
	bne	.L250
	ldr	fp, [sp, #8]
	cmp	fp, #0
	beq	.L249
	mov	r3, r3, asl #8
	cmp	ip, #173
	add	fp, r1, r3
	str	r4, [fp, #2056]
	streq	r0, [fp, #2052]
	beq	.L269
	cmp	ip, #44
	streq	r0, [fp, #2052]
	strne	r5, [fp, #2052]
	streq	r10, [fp, #2048]
	strne	r0, [fp, #2048]
	b	.L255
.L250:
	ldr	fp, [sp, #4]
	cmp	fp, #0
	beq	.L249
	mov	r3, r3, asl #8
	cmp	ip, #173
	add	fp, r1, r3
	str	r4, [fp, #2056]
	streq	r6, [fp, #2052]
	streq	r9, [fp, #2048]
	beq	.L255
	cmp	ip, #44
	streq	r6, [fp, #2052]
	streq	r8, [fp, #2048]
	beq	.L255
	str	r5, [fp, #2052]
.L269:
	str	r2, [fp, #2048]
.L255:
	add	r3, r1, r3
	str	r2, [r3, #2048]
	str	r2, [r3, #2048]
	str	r2, [r3, #2048]
.L249:
	add	lr, lr, #8
	cmp	lr, #32
	bne	.L257
	mov	r0, #0
	bl	NandcWaitFlashReady
	mov	r0, #0
	add	sp, sp, #20
	@ sp needed
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L271:
	.align	2
.L270:
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
	ldr	ip, .L274
	stmfd	sp!, {r3, r4, r5, lr}
	.save {r3, r4, r5, lr}
	add	ip, ip, r0, asl #3
	ldr	r3, .L274+4
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
.L275:
	.align	2
.L274:
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
	ldr	r2, .L278
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
.L279:
	.align	2
.L278:
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
	ldr	r0, .L288
	ldr	r1, .L288+4
	add	r4, r4, r4, asl #2
	sub	ip, r0, #48
	mov	r2, #0
.L281:
	ldrb	r3, [r1, #697]	@ zero_extendqisi2
	cmp	r2, r3
	bcs	.L287
	ldrb	r3, [r1, #696]	@ zero_extendqisi2
	cmp	r3, #67
	add	r3, r2, r4
	addeq	r3, ip, r3
	addne	r3, r0, r3
	add	r2, r2, #1
	ldrsb	r3, [r3, #5]
	str	r3, [r5]
	b	.L281
.L287:
	mov	r0, #0
	ldmfd	sp!, {r3, r4, r5, lr}
	b	NandcWaitFlashReady
.L289:
	.align	2
.L288:
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
	ldr	r3, .L292
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
.L293:
	.align	2
.L292:
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
	ldr	r7, .L307
	.pad #24
	sub	sp, sp, #24
	ldr	r9, .L307+4
	mov	r6, r0
	mov	r10, r1
	mov	r8, r5
	ldr	r3, [r7]
	str	r3, [sp, #20]
.L295:
	cmp	r8, r10
	beq	.L306
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
	bcs	.L297
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
	bne	.L299
	ldr	r0, .L307+8
	ldr	r1, [sp, #12]
	bl	printk
.L299:
	mov	r0, r4
	bl	NandcFlashDeCs
.L297:
	add	r8, r8, #1
	add	r5, r5, #36
	b	.L295
.L306:
	ldr	r2, [sp, #20]
	mov	r0, #0
	ldr	r3, [r7]
	cmp	r2, r3
	beq	.L301
	bl	__stack_chk_fail
.L301:
	add	sp, sp, #24
	@ sp needed
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, pc}
.L308:
	.align	2
.L307:
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
	ldr	r0, [r4, #1728]
	cmp	r0, #0
	beq	.L317
	mov	r0, #0
	bl	flash_enter_slc_mode
	b	.L318
.L317:
	bl	flash_exit_slc_mode
.L318:
	ldrb	r3, [r4, #1732]	@ zero_extendqisi2
	ldr	r5, .L327
	cmp	r3, #0
	beq	.L319
	ldrb	r3, [r5, #1716]	@ zero_extendqisi2
	tst	r3, #1
	beq	.L319
	mov	r0, #1
	bl	FlashSetInterfaceMode
	mov	r0, #1
	bl	NandcSetMode
	mov	r3, #0
	strb	r3, [r5, #1732]
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
	ldr	r2, .L353+4
	str	r2, [r3, #304]
	bl	ftl_malloc
	str	r5, [r4, #1776]
	str	r5, [r4, #1784]
	str	r0, [r4, #1748]
	str	r0, [r4, #1752]
	add	r0, r0, #32768
	str	r0, [r4, #1756]
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
	str	r0, [r3, #1788]
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
	strb	r0, [r3, #1792]
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
	ldrb	r3, [r4, #1732]	@ zero_extendqisi2
	ldr	r5, .L381
	cmp	r3, #0
	beq	.L375
	mov	r0, #1
	bl	NandcSetMode
	ldrb	r0, [r5, #1716]	@ zero_extendqisi2
	bl	FlashSetInterfaceMode
	ldrb	r0, [r5, #1716]	@ zero_extendqisi2
	bl	NandcSetMode
	ldrb	r0, [r5, #97]	@ zero_extendqisi2
	bl	NandcSetDdrPara
.L375:
	ldr	r3, [r4, #692]
	ldmfd	sp!, {r4, r5, r6, lr}
	ldrb	r0, [r3, #20]	@ zero_extendqisi2
	b	FlashBchSel
.L382:
	.align	2
.L381:
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
	@ args = 0, pretend = 0, frame = 24
	@ frame_needed = 0, uses_anonymous_args = 0
	stmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.save {r4, r5, r6, r7, r8, r9, r10, fp, lr}
	mov	r8, r2
	ldr	r4, .L425
	.pad #36
	sub	sp, sp, #36
	ldr	r3, .L425+4
	ldrb	r5, [r4]	@ zero_extendqisi2
	ldr	r2, [r3]
	cmp	r5, #0
	str	r3, [sp, #12]
	str	r2, [sp, #28]
	moveq	r9, r0
	moveq	r10, r1
	moveq	fp, r4
	beq	.L389
	mov	r1, r8
	bl	FlashEraseSLc2KBlocks
	b	.L390
.L398:
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
	bcc	.L422
.L392:
	add	r5, r5, #1
.L389:
	cmp	r5, r8
	bcc	.L398
	b	.L423
.L422:
	ldrb	r3, [fp, #1793]	@ zero_extendqisi2
	cmp	r3, #0
	add	r3, r4, r0, asl #4
	ldr	r3, [r3, #1596]
	moveq	r7, #0
	cmp	r3, #0
	beq	.L394
	uxtb	r0, r0
	bl	FlashWaitCmdDone
.L394:
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
	bne	.L396
	ldrb	r3, [fp, #140]	@ zero_extendqisi2
	cmp	r3, #0
	beq	.L396
	bl	flash_enter_slc_mode
	b	.L397
.L396:
	bl	flash_exit_slc_mode
.L397:
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
	b	.L392
.L423:
	ldr	r0, [r4, #76]
	mov	r5, #0
	bl	NandcIqrWaitFlashReady
	ldr	r6, .L425
	ldr	r7, .L425+8
.L399:
	ldrb	r2, [r4, #1717]	@ zero_extendqisi2
	ldr	r3, .L425
	cmp	r5, r2
	bcs	.L424
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
.L424:
	ldr	r2, [r3, #1796]
	cmp	r2, #0
	bne	.L402
.L404:
	mov	r0, #0
	b	.L390
.L402:
	ldrb	r3, [r3, #1556]	@ zero_extendqisi2
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
	ldr	r3, [sp, #12]
	ldr	r2, [sp, #28]
	ldr	r3, [r3]
	cmp	r2, r3
	beq	.L406
	bl	__stack_chk_fail
.L406:
	add	sp, sp, #36
	@ sp needed
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L426:
	.align	2
.L425:
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
	ldr	r3, .L430
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
.L431:
	.align	2
.L430:
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
.L433:
	ldr	r3, [r0, #8]
	str	r3, [sp, #4]
	ldr	r3, [sp, #4]
	tst	r3, #1048576
	beq	.L433
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
	ldr	r5, .L455
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
	bls	.L438
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
	beq	.L439
	cmp	r1, #0
	bne	.L440
.L448:
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
	b	.L442
.L440:
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
.L444:
	ldr	lr, [sp, #12]
	cmp	r0, lr
	bcs	.L448
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
	b	.L444
.L442:
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
.L439:
	ldr	r3, [sp, #20]
	str	r3, [r6, #16]
.L438:
	str	r7, [r6, #12]
	str	r4, [r6, #8]
	orr	r4, r4, #4
	str	r4, [r6, #8]
	add	sp, sp, #28
	@ sp needed
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L456:
	.align	2
.L455:
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
.L458:
	cmp	r2, r0
	uxth	r3, r1
	add	r1, r1, #1
	movls	r2, r2, asl #1
	bls	.L458
.L460:
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
	ldr	r3, .L464
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
.L465:
	.align	2
.L464:
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
	ldr	r4, .L494
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
	ldr	r0, .L494+4
	strh	r3, [r4, r1]	@ movhi
	movw	r1, #1814
	strh	lr, [r4, r1]	@ movhi
	mov	r1, #0
.L467:
	strb	r1, [r1, r0]
	add	r1, r1, #1
	cmp	r1, #32
	bne	.L467
	ldrh	r0, [r5, #14]
	ldrh	r1, [r5, #20]
	cmp	r1, r0, lsr #8
	bcs	.L468
	uxtb	r10, r3
	ldr	r9, .L494+4
	mov	r1, r10, asl #1
	uxtb	r1, r1
	str	r1, [sp, #4]
	sub	r1, r2, #1
	mul	r1, r3, r1
	str	r1, [sp]
	mov	r1, #0
.L469:
	cmp	r1, r3
	bcs	.L471
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
.L472:
	cmp	r8, r2
	add	ip, ip, r3
	bcs	.L493
	ldr	r7, [sp, #12]
	add	fp, r0, r10
	add	r8, r8, #1
	strb	r0, [r7, ip]
	ldr	r7, [sp, #8]
	strb	fp, [r7, ip]
	ldr	r7, [sp, #4]
	add	r0, r0, r7
	uxtb	r0, r0
	b	.L472
.L493:
	add	r1, r1, #1
	b	.L469
.L471:
	movw	r1, #1826
	mov	r2, r2, asl #1
	strh	r2, [r4, r1]	@ movhi
	movw	r2, #1814
	mov	lr, lr, lsr #1
	strh	lr, [r4, r2]	@ movhi
.L468:
	cmp	r6, #1
	movw	r2, #1864
	mov	r1, #5
	strh	r1, [r4, r2]	@ movhi
	movw	r1, #1866
	mov	r0, #0
	strh	r0, [r4, r1]	@ movhi
	ldreq	r1, .L494
	ldrb	r9, [r4]	@ zero_extendqisi2
	ldrh	r10, [r5, #16]
	streqh	r6, [r1, r2]	@ movhi
	cmp	r9, #0
	movw	r2, #1868
	mov	r1, #4352
	strh	r1, [r4, r2]	@ movhi
	movne	r0, #384
	ldrne	r1, .L494
	strneh	r0, [r1, r2]	@ movhi
	movw	r1, #1826
	ldrh	r8, [r4, r1]
	movw	r1, #1804
	ldr	r2, .L494
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
	ldr	r5, .L494
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
	ldr	r10, .L494
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
	ldr	r6, .L494
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
	bne	.L492
	cmp	r0, #7
	bhi	.L482
	mov	r3, #8
.L492:
	str	r3, [r6, #1900]
.L482:
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
	ldrcc	r3, .L494
	movcc	r1, #1
	strcch	r1, [r3, r2]	@ movhi
	add	sp, sp, #20
	@ sp needed
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L495:
	.align	2
.L494:
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
	ldr	r2, .L503
	movw	r3, #1918
	ldrh	r3, [r2, r3]
	cmp	r3, #0
	beq	.L502
	movw	r1, #1892
	ldr	r3, [r2, #1920]
	ldrh	r2, [r2, r1]
	add	r2, r3, r2, asl #1
.L498:
	cmp	r3, r2
	beq	.L502
	ldrh	r1, [r3], #2
	cmp	r1, r0
	bne	.L498
	mov	r0, #1
	bx	lr
.L502:
	mov	r0, #0
	bx	lr
.L504:
	.align	2
.L503:
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
	ldr	r2, .L512
	ldr	r3, [r2, #1924]
	cmp	r3, #0
	beq	.L508
	stmfd	sp!, {r4, r5, lr}
	.save {r4, r5, lr}
	mov	r5, #36
	ldr	r4, [r2, #1928]
	mov	r2, #0
.L507:
	mla	ip, r5, r2, r4
	ldr	lr, [ip, #16]
	cmp	lr, r0
	movcc	ip, #0
	movcs	ip, #1
	cmp	lr, r1
	movhi	ip, #0
	cmp	ip, #0
	bne	.L509
	add	r2, r2, #1
	cmp	r2, r3
	bne	.L507
	mov	r0, ip
	ldmfd	sp!, {r4, r5, pc}
.L508:
	mov	r0, r3
	bx	lr
.L509:
	mov	r0, #1
	ldmfd	sp!, {r4, r5, pc}
.L513:
	.align	2
.L512:
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
	ldr	r3, .L515
	ldr	r0, [r3, #1912]
	bx	lr
.L516:
	.align	2
.L515:
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
	ldr	r3, .L518
	ldr	r0, [r3, #1912]
	bx	lr
.L519:
	.align	2
.L518:
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
	ldr	r3, .L521
	ldr	r0, [r3, #1932]
	bx	lr
.L522:
	.align	2
.L521:
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
	ldr	r5, .L525
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
	ldr	r0, .L525+4
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
.L526:
	.align	2
.L525:
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
	ldr	r5, .L529
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
.L530:
	.align	2
.L529:
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
	ldr	r4, .L541
	mov	r5, #0
	ldrh	r6, [r4, r3]
	movw	r3, #1826
	ldrh	r3, [r4, r3]
	mov	r4, r5
	mul	r6, r3, r6
.L533:
	uxth	r0, r5
	cmp	r0, r6
	bge	.L540
	bl	FtlBbmIsBadBlock
	add	r5, r5, #1
	cmp	r0, #0
	addne	r4, r4, #1
	uxthne	r4, r4
	b	.L533
.L540:
	mov	r0, r4
	ldmfd	sp!, {r4, r5, r6, pc}
.L542:
	.align	2
.L541:
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
	ldr	r6, .L545
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
.L546:
	.align	2
.L545:
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
	ldr	r3, .L549
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
.L550:
	.align	2
.L549:
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
	ldr	r4, .L553
	ldrh	r1, [r4, r3]
	bl	__aeabi_uidivmod
	movw	r3, #1828
	uxth	r0, r1
	ldrh	r1, [r4, r3]
	bl	__aeabi_uidiv
	uxth	r0, r0
	ldmfd	sp!, {r4, pc}
.L554:
	.align	2
.L553:
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
	bls	.L556
	rsb	r0, r1, r0
	cmp	r0, #-2147483648
	movhi	r0, #0
	movls	r0, #1
	bx	lr
.L556:
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
	ldr	r3, .L559
	ldrh	r0, [r3, #6]
	clz	r0, r0
	mov	r0, r0, lsr #5
	bx	lr
.L560:
	.align	2
.L559:
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
	ldr	r3, .L562
	ldrh	r0, [r3, #6]
	sub	r0, r0, #1024
	clz	r0, r0
	mov	r0, r0, lsr #5
	bx	lr
.L563:
	.align	2
.L562:
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
	ldr	r4, .L574
	ldr	r5, .L574+4
	ldrh	r3, [r4, #6]
	cmp	r3, #1024
	ldmeqfd	sp!, {r3, r4, r5, r6, r7, pc}
	cmp	r1, #0
	beq	.L566
	ldr	r3, [r5, #4052]
	cmp	r3, #0
	bne	.L566
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
.L566:
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
.L575:
	.align	2
.L574:
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
	ldr	r3, .L589
	ldrh	ip, [r3, #28]
	ldr	r3, .L589+4
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
.L578:
	uxth	lr, r0
	add	r0, r0, #1
	cmp	lr, ip
	bge	.L588
	add	lr, r3, r1, asl #1
	add	r1, r1, #1
	ubfx	r1, r1, #0, #10
	ldrh	r4, [lr, #8]
	add	lr, r3, r2, asl #1
	strh	r4, [lr, #8]	@ movhi
	mov	r4, #1
	add	r2, r2, r4
	ubfx	r2, r2, #0, #10
	b	.L578
.L588:
	cmp	r4, #0
	strneh	r1, [r3, #2]	@ movhi
	strneh	r2, [r3, #4]	@ movhi
	ldmfd	sp!, {r4, pc}
.L590:
	.align	2
.L589:
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
	ldr	r6, .L602
	ldr	r5, .L602+4
	mov	r7, r6
.L592:
	ldrh	r1, [r5, #6]
	cmp	r1, #0
	beq	.L593
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
	bne	.L594
	mov	r0, r4
	bl	P2V_block_in_plane
	mov	r3, r4, asl #10
	mov	r8, r0
	ldr	r0, [r6, #4056]
	str	r3, [r0, #4]
	ldrb	r3, [r6, #140]	@ zero_extendqisi2
	cmp	r3, #0
	beq	.L595
	mov	r1, r9
	mov	r2, #1
	bl	FlashEraseBlocks
.L595:
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
	b	.L594
.L593:
	ldr	r0, .L602+8
	bl	printk
.L596:
	b	.L596
.L594:
	sub	r3, r4, #1
	movw	r2, #65533
	uxth	r3, r3
	cmp	r3, r2
	bls	.L597
	ldr	r3, .L602+4
	mov	r1, r4
	ldr	r0, .L602+12
	ldrh	r2, [r3, #6]
	bl	printk
	b	.L592
.L597:
	mov	r0, r4
	ldmfd	sp!, {r3, r4, r5, r6, r7, r8, r9, pc}
.L603:
	.align	2
.L602:
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
	ldr	r3, .L610
	str	lr, [sp, #-4]!
	.save {lr}
	movw	lr, #65535
	ldr	ip, [r3, #-4068]
	ldr	r2, [r0]
	ldr	r3, .L610+4
	rsb	r0, ip, r2
	mov	r0, r0, asr #1
	mul	r3, r3, r0
	mov	r0, #6
	uxth	r3, r3
.L606:
	cmp	r1, r3
	beq	.L607
	ldrh	r3, [r2]
	cmp	r3, lr
	beq	.L608
	mla	r2, r0, r3, ip
	b	.L606
.L607:
	mov	r0, #1
	ldr	pc, [sp], #4
.L608:
	mov	r0, #0
	ldr	pc, [sp], #4
.L611:
	.align	2
.L610:
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
	ldr	r5, .L629
	ldrh	r3, [r5, r3]
	cmp	r3, r0
	bls	.L614
	ldr	r2, .L629+4
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
	beq	.L628
	ldr	r8, [r2, #-4060]
	mov	r4, r0, asl #1
	ldrh	r2, [r1, #4]
	ldr	r7, [r7, #-4068]
	ldrh	r9, [r8, r4]
	cmp	r2, #0
	rsb	r10, r7, r3
	mulne	ip, r2, r9
	ldr	r2, .L629+8
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
.L623:
	add	r4, r4, #1
	ldr	r5, [sp]
	uxth	r4, r4
	cmp	r4, r5
	movls	r5, #0
	movhi	r5, #1
	cmp	r0, r2
	orreq	r5, r5, #1
	cmp	r5, #0
	bne	.L614
	mov	r10, r2, asl #1
	ldrh	r5, [r3, #4]
	ldrh	fp, [r8, r10]
	cmp	r5, #0
	mvneq	r5, #0
	mulne	r5, r5, fp
	cmp	r5, ip
	bne	.L619
	ldr	r5, [sp, #4]
	ldrh	r10, [r9, r10]
	ldrh	r5, [r5]
	cmp	r10, r5
	bcc	.L621
	b	.L620
.L619:
	bhi	.L620
.L621:
	ldrh	r5, [r3]
	movw	r10, #65535
	cmp	r5, r10
	streqh	r2, [r1, #2]	@ movhi
	streqh	r0, [r3]	@ movhi
	ldreq	r3, .L629+4
	streq	r1, [r3, #-4056]
	beq	.L614
.L622:
	mov	r3, #6
	mov	r2, r5
	mla	r3, r3, r5, r7
	b	.L623
.L620:
	strh	r2, [r6, lr]	@ movhi
	ldrh	r2, [r3, #2]
	strh	r2, [r1, #2]	@ movhi
	ldr	r2, .L629+4
	ldr	ip, [r2, #-4064]
	cmp	r3, ip
	bne	.L624
	strh	r0, [r3, #2]	@ movhi
.L628:
	str	r1, [r2, #-4064]
	b	.L614
.L624:
	ldrh	ip, [r3, #2]
	ldr	r1, [r2, #-4068]
	mov	r2, #6
	mul	r2, r2, ip
	strh	r0, [r1, r2]	@ movhi
	strh	r0, [r3, #2]	@ movhi
.L614:
	mov	r0, #0
	add	sp, sp, #12
	@ sp needed
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L630:
	.align	2
.L629:
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
	ldr	r2, .L633
	ldrh	r3, [r2, #-4]
	add	r3, r3, #1
	strh	r3, [r2, #-4]	@ movhi
	ldmfd	sp!, {r3, pc}
.L634:
	.align	2
.L633:
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
	beq	.L636
	ldr	r2, .L644
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
	beq	.L643
	ldr	r2, .L644+4
	ldr	r8, [r7, #-4068]
	ldr	r9, [r2, #4060]
	mov	r2, r0, asl #1
	rsb	r1, r8, r3
	ldrh	r10, [r9, r2]
	mov	r1, r1, asr #1
	ldr	r2, .L644+8
	mul	r1, r2, r1
	uxth	r2, r1
.L640:
	mov	r1, r2, asl #1
	ldrh	r1, [r9, r1]
	cmp	r1, r10
	bcs	.L638
	ldrh	r1, [r3]
	cmp	r1, r4
	streqh	r2, [ip, #2]	@ movhi
	streqh	r0, [r3]	@ movhi
	beq	.L636
.L639:
	mla	r3, lr, r1, r8
	mov	r2, r1
	b	.L640
.L638:
	ldrh	r1, [r3, #2]
	strh	r1, [ip, #2]	@ movhi
	strh	r2, [r6, r5]	@ movhi
	ldr	r1, [r7, #-4048]
	ldr	r2, .L644
	cmp	r3, r1
	bne	.L641
	strh	r0, [r3, #2]	@ movhi
.L643:
	str	ip, [r2, #-4048]
	b	.L636
.L641:
	ldrh	ip, [r3, #2]
	ldr	r1, [r2, #-4068]
	mov	r2, #6
	mul	r2, r2, ip
	strh	r0, [r1, r2]	@ movhi
	strh	r0, [r3, #2]	@ movhi
.L636:
	mov	r0, #0
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, pc}
.L645:
	.align	2
.L644:
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
	ldr	r2, .L648
	ldrh	r3, [r2, #-12]
	add	r3, r3, #1
	strh	r3, [r2, #-12]	@ movhi
	ldmfd	sp!, {r3, pc}
.L649:
	.align	2
.L648:
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
	ldr	r4, .L656
	movw	r5, #65535
	mul	r1, ip, r1
	ldr	r3, [r0]
	ldr	r2, [r4, #-4068]
	add	lr, r2, r1
	cmp	lr, r3
	ldrh	r3, [r2, r1]
	bne	.L651
	cmp	r3, r5
	mlane	r3, ip, r3, r2
	moveq	r3, #0
	streq	r3, [r0]
	strne	r3, [r0]
	mvnne	r0, #0
	strneh	r0, [r3, #2]	@ movhi
	b	.L653
.L651:
	cmp	r3, r5
	ldrh	r0, [lr, #2]
	bne	.L654
	cmp	r0, r3
	mulne	r0, ip, r0
	mvnne	r3, #0
	strneh	r3, [r2, r0]	@ movhi
	b	.L653
.L654:
	mla	r3, ip, r3, r2
	strh	r0, [r3, #2]	@ movhi
	ldrh	r5, [lr, #2]
	ldrh	r0, [r2, r1]
	ldr	r3, [r4, #-4068]
	mul	ip, ip, r5
	strh	r0, [r3, ip]	@ movhi
.L653:
	mvn	r3, #0
	mov	r0, #0
	strh	r3, [r2, r1]	@ movhi
	strh	r3, [lr, #2]	@ movhi
	ldmfd	sp!, {r4, r5, pc}
.L657:
	.align	2
.L656:
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
	beq	.L664
	ldr	r2, .L667
	movw	ip, #65535
	stmfd	sp!, {r4, lr}
	.save {r4, lr}
	mov	lr, #6
	ldr	r4, [r2, #-4068]
.L660:
	cmp	r1, #0
	bne	.L661
.L663:
	rsb	r4, r4, r3
	ldr	r3, .L667+4
	mov	r4, r4, asr #1
	mul	r4, r3, r4
	uxth	r4, r4
	mov	r1, r4
	bl	List_remove_node
	mov	r0, r4
	ldmfd	sp!, {r4, pc}
.L661:
	ldrh	r2, [r3]
	cmp	r2, ip
	beq	.L663
	sub	r1, r1, #1
	mla	r3, lr, r2, r4
	uxth	r1, r1
	b	.L660
.L664:
	movw	r0, #65535
	bx	lr
.L668:
	.align	2
.L667:
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
	ldr	r2, .L676
	ldr	r3, [r2, #-4064]
	cmp	r3, #0
	ldrne	r1, [r2, #-4068]
	movne	ip, #6
	movwne	r2, #65535
	beq	.L674
.L671:
	cmp	r0, #0
	beq	.L672
	ldrh	r3, [r3]
	cmp	r3, r2
	subne	r0, r0, #1
	mlane	r3, ip, r3, r1
	uxthne	r0, r0
	bne	.L671
.L674:
	movw	r0, #65535
	bx	lr
.L672:
	rsb	r3, r1, r3
	ldr	r0, .L676+4
	mov	r3, r3, asr #1
	mul	r0, r0, r3
	uxth	r0, r0
	bx	lr
.L677:
	.align	2
.L676:
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
	ldr	r3, .L687
	sub	r2, r3, #4032
	ldrh	r2, [r2, #-8]
	cmp	r2, r0
	beq	.L686
	sub	r2, r3, #3984
	ldrh	r2, [r2, #-8]
	cmp	r2, r0
	beq	.L686
	sub	r2, r3, #3936
	ldrh	r2, [r2, #-8]
	cmp	r2, r0
	beq	.L686
	stmfd	sp!, {r4, lr}
	.save {r4, lr}
	mov	lr, #6
	mul	lr, lr, r0
	ldr	r1, [r3, #-4068]
	ldr	r2, [r3, #-4064]
	add	ip, r1, lr
	cmp	ip, r2
	beq	.L679
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
	bne	.L681
	ldrh	r3, [r1, lr]
	cmp	r3, ip
	beq	.L679
.L681:
	mov	r3, #6
	mul	ip, r3, ip
	ldr	r3, .L687+4
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
	bcs	.L679
	mov	r4, r0
	ldr	r0, .L687+8
	mov	r1, r4
	bl	List_remove_node
	ldr	r2, .L687+12
	mov	r0, r4
	ldrh	r3, [r2, #-4]
	sub	r3, r3, #1
	strh	r3, [r2, #-4]	@ movhi
	bl	INSERT_DATA_LIST
.L679:
	mov	r0, #0
	ldmfd	sp!, {r4, pc}
.L686:
	mov	r0, #0
	bx	lr
.L688:
	.align	2
.L687:
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
.L690:
	uxth	r5, r3
	cmp	r5, r1
	bcs	.L693
	mov	r7, r2
	add	r3, r3, #1
	ldrh	r6, [r7]
	add	r2, r2, #2
	cmp	r6, #0
	bne	.L690
	mov	r4, r0
	bl	FtlFreeSysBlkQueueOut
	movw	r2, #65533
	sub	r3, r0, #1
	mov	r1, r0
	strh	r0, [r7]	@ movhi
	uxth	r3, r3
	cmp	r3, r2
	bls	.L691
	ldr	r3, .L697
	ldr	r0, .L697+4
	ldrh	r2, [r3, #6]
	bl	printk
.L692:
	b	.L692
.L691:
	ldr	r3, [r4, #28]
	strh	r6, [r4, #2]	@ movhi
	add	r3, r3, #1
	str	r3, [r4, #28]
	ldrh	r3, [r4, #8]
	strh	r5, [r4]	@ movhi
	add	r3, r3, #1
	strh	r3, [r4, #8]	@ movhi
.L693:
	mov	r0, #0
	ldmfd	sp!, {r3, r4, r5, r6, r7, pc}
.L698:
	.align	2
.L697:
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
	ldr	r2, .L715
	movw	r3, #1910
	stmfd	sp!, {r4, r5, r6, lr}
	.save {r4, r5, r6, lr}
	mov	r1, #0
	ldrh	r2, [r2, r3]
	mov	ip, #12
	ldr	r3, .L715+4
	movw	lr, #65535
	ldr	r3, [r3, #-3896]
.L700:
	uxth	r0, r1
	cmp	r0, r2
	bcs	.L712
	add	r1, r1, #1
	mla	r4, ip, r1, r3
	ldrh	r4, [r4, #-12]
	cmp	r4, lr
	bne	.L700
	ldmfd	sp!, {r4, r5, r6, pc}
.L712:
	mov	r0, r2
	mov	r1, #0
	mov	ip, #-2147483648
	mov	r5, #12
.L703:
	uxth	r4, r1
	cmp	r4, r2
	bcs	.L713
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
	b	.L703
.L713:
	cmp	r0, r2
	ldmccfd	sp!, {r4, r5, r6, pc}
	ldr	r1, .L715+8
	mov	r0, r2
	mvn	ip, #0
	ldrh	r5, [r1, #-4]
	mov	r1, #0
.L706:
	uxth	lr, r1
	cmp	lr, r2
	bcs	.L714
	ldr	r4, [r3, #4]
	cmp	r4, ip
	bcs	.L707
	ldrh	r6, [r3]
	cmp	r6, r5
	movne	ip, r4
	movne	r0, lr
.L707:
	add	r1, r1, #1
	add	r3, r3, #12
	b	.L706
.L714:
	ldmfd	sp!, {r4, r5, r6, pc}
.L716:
	.align	2
.L715:
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
	ldr	r3, .L725
	sub	r1, r3, #3888
	ldrh	r2, [r1, #-2]
	cmp	r2, #4
	cmpls	r0, #0
	addeq	r2, r2, #1
	streqh	r2, [r1, #-2]	@ movhi
	bxeq	lr
	ldr	r0, .L725+4
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
.L719:
	cmp	r2, r0
	beq	.L724
	ldrh	r1, [r2], #2
	cmp	r1, ip
	ldrne	lr, [r3, #-3888]
	addne	r1, r1, lr
	strne	r1, [r3, #-3888]
	b	.L719
.L724:
	ldr	pc, [sp], #4
.L726:
	.align	2
.L725:
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
	ldr	r2, .L731
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
.L732:
	.align	2
.L731:
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
	ldr	r3, .L734
	mov	r2, r0, lsr #5
	and	r0, r0, #31
	ldr	r3, [r3, #-3884]
	ldr	r3, [r3, r2, asl #2]
	mov	r0, r3, lsr r0
	and	r0, r0, #1
	bx	lr
.L735:
	.align	2
.L734:
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
	ldr	ip, .L746
	mov	r3, #0
	strh	r3, [r0, #4]	@ movhi
	movw	r3, #1804
	stmfd	sp!, {r4, r5, lr}
	.save {r4, r5, lr}
	movw	r4, #65535
	ldrh	lr, [ip, r3]
	add	r3, r2, #7
	add	r3, r0, r3, asl #1
.L737:
	cmp	r2, lr
	bcs	.L744
	ldrh	ip, [r3, #2]!
	add	r2, r2, #1
	cmp	ip, r4
	uxth	r2, r2
	ldrneh	ip, [r0, #4]
	addne	ip, ip, #1
	strneh	ip, [r0, #4]	@ movhi
	b	.L737
.L744:
	ldr	r3, .L746+4
	add	ip, r0, #14
	mov	r2, #0
	movw	r5, #65535
	ldrh	r4, [r3]
.L740:
	uxth	r3, r2
	cmp	r3, lr
	bcs	.L745
	ldrh	r3, [ip, #2]!
	add	r2, r2, #1
	cmp	r3, r5
	ldrneh	r3, [r0, #4]
	addne	r3, r4, r3
	subne	r3, r3, #1
	rsbne	r3, r1, r3
	strneh	r3, [r0, #4]	@ movhi
	b	.L740
.L745:
	ldmfd	sp!, {r4, r5, pc}
.L747:
	.align	2
.L746:
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
	ldr	r6, .L762
	mov	r3, #0
	ldr	r10, .L762+4
	add	r7, r0, #16
	mov	r4, r0
	mov	r5, r3
	ldrh	r8, [r6, r2]
	mvn	r9, #0
	strh	r3, [r0, #4]	@ movhi
	strb	r3, [r0, #7]
.L749:
	uxth	r3, r5
	cmp	r3, r8
	bcs	.L761
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
	b	.L749
.L761:
	ldr	r2, .L762
	ldrb	r1, [r4, #7]	@ zero_extendqisi2
	add	r3, r2, #1872
	ldrh	r3, [r3]
	smulbb	r3, r1, r3
	strh	r3, [r4, #4]	@ movhi
	mov	r3, #0
	strb	r3, [r4, #9]
	ldr	r3, [r6, #1796]
	cmp	r3, #0
	beq	.L752
	ldrh	r3, [r4]
	ldr	r2, [r2, #4060]
	mov	r3, r3, asl #1
	ldrh	r3, [r2, r3]
	cmp	r3, #79
	movls	r3, #1
	strlsb	r3, [r4, #9]
.L752:
	ldrb	r3, [r6]	@ zero_extendqisi2
	mov	r0, #0
	cmp	r3, #0
	movne	r3, #1
	strneb	r3, [r4, #9]
	ldmfd	sp!, {r3, r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L763:
	.align	2
.L762:
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
	ldr	r3, .L774
	mov	r5, #0
	mov	r6, r0
	mov	r4, r5
	ldrh	r7, [r3, r2]
	add	r3, r3, #1872
	sub	r9, r3, #40
	ldrh	r8, [r3]
.L765:
	uxth	r3, r5
	cmp	r3, r7
	bcs	.L773
	ldrb	r0, [r9, r5]	@ zero_extendqisi2
	mov	r1, r6
	bl	V2P_block
	add	r5, r5, #1
	bl	FtlBbmIsBadBlock
	cmp	r0, #0
	addeq	r4, r4, r8
	uxtheq	r4, r4
	b	.L765
.L773:
	cmp	r4, #0
	beq	.L768
	mov	r1, r4
	mov	r0, #32768
	bl	__aeabi_idiv
	uxth	r4, r0
.L768:
	ldr	r3, .L774+4
	mov	r2, #6
	mov	r0, #0
	ldr	r3, [r3, #-4068]
	mla	r6, r2, r6, r3
	strh	r4, [r6, #4]	@ movhi
	ldmfd	sp!, {r3, r4, r5, r6, r7, r8, r9, pc}
.L775:
	.align	2
.L774:
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
	ldr	r3, .L779
	ldr	r0, [r3, #-4048]
	cmp	r0, #0
	bxeq	lr
	ldr	r3, [r3, #-4068]
	rsb	r0, r3, r0
	ldr	r3, .L779+4
	mov	r0, r0, asr #1
	mul	r0, r3, r0
	ldr	r3, .L779+8
	ldr	r3, [r3, #4060]
	uxth	r0, r0
	mov	r0, r0, asl #1
	ldrh	r0, [r3, r0]
	bx	lr
.L780:
	.align	2
.L779:
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
	ldr	r1, .L791
	ldr	r3, [r1, #-4048]
	cmp	r3, #0
	beq	.L787
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
	ldr	r2, .L791+4
	mul	r3, r2, r3
	mov	r2, #0
	uxth	r3, r3
.L784:
	uxth	ip, r2
	cmp	ip, r0
	bcs	.L786
	mul	ip, lr, r3
	add	r2, r2, #1
	ldrh	ip, [r1, ip]
	cmp	ip, r4
	bne	.L788
.L786:
	ldr	r2, .L791+8
	mov	r3, r3, asl #1
	ldr	r2, [r2, #4060]
	ldrh	r0, [r2, r3]
	ldmfd	sp!, {r4, pc}
.L788:
	mov	r3, ip
	b	.L784
.L787:
	mov	r0, r3
	bx	lr
.L792:
	.align	2
.L791:
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
	ldr	r7, .L805
	.pad #52
	sub	sp, sp, #52
	ldr	r6, .L805+4
	add	r5, r8, #12
	ldr	r1, .L805+8
	ldr	r3, [r7]
	str	r3, [sp, #44]
	bl	strcpy
	mov	r0, r5
	ldr	r1, .L805+12
	ldr	r2, [r6, #112]
	bl	sprintf
	ldr	r1, .L805+16
	ldr	r2, [r6, #1888]
	add	r5, r5, r0
	mov	r0, r5
	bl	sprintf
	ldr	r3, .L805+20
	ldr	r3, [r3, #504]
	cmp	r3, #1
	add	r4, r5, r0
	bne	.L799
	add	r0, sp, #28
	add	r1, sp, #32
	add	r2, sp, #36
	add	r3, sp, #40
	bl	NandcGetTimeCfg
	mov	r0, r4
	ldr	r1, .L805+24
	ldr	r9, .L805+28
	ldr	r10, .L805+32
	sub	fp, r9, #4032
	ldr	r3, [sp, #36]
	ldr	r2, [sp, #28]
	str	r3, [sp]
	ldr	r3, [sp, #40]
	str	r3, [sp, #4]
	ldr	r3, [sp, #32]
	bl	sprintf
	ldr	r1, .L805+36
	add	r4, r4, r0
	add	r5, r4, #10
	mov	r0, r4
	add	r4, r6, #1936
	bl	strcpy
	mov	r0, r5
	ldr	r1, .L805+40
	ldr	r2, [r6, #1932]
	bl	sprintf
	ldr	r1, .L805+44
	ldr	r2, [r9, #-3888]
	add	r5, r5, r0
	mov	r0, r5
	bl	sprintf
	ldr	r1, .L805+48
	ldr	r2, [r9, #-3880]
	add	r5, r5, r0
	mov	r0, r5
	bl	sprintf
	ldr	r1, .L805+52
	ldr	r2, [r9, #-3876]
	add	r5, r5, r0
	mov	r0, r5
	bl	sprintf
	ldr	r1, .L805+56
	ldr	r2, [r9, #-3872]
	add	r5, r5, r0
	mov	r0, r5
	bl	sprintf
	ldr	r1, .L805+60
	ldr	r2, [r9, #-3868]
	add	r5, r5, r0
	mov	r0, r5
	bl	sprintf
	ldr	r1, .L805+64
	ldr	r2, [r9, #-3864]
	add	r5, r5, r0
	mov	r0, r5
	bl	sprintf
	ldr	r1, .L805+68
	ldr	r2, [r9, #-3860]
	add	r5, r5, r0
	mov	r0, r5
	bl	sprintf
	ldr	r2, [r9, #-3856]
	ldr	r1, .L805+72
	mov	r2, r2, lsr #11
	add	r5, r5, r0
	mov	r0, r5
	bl	sprintf
	ldr	r2, [r9, #-3852]
	ldr	r1, .L805+76
	mov	r2, r2, lsr #11
	add	r5, r5, r0
	mov	r0, r5
	bl	sprintf
	ldr	r1, .L805+80
	ldr	r2, [r9, #-3848]
	add	r5, r5, r0
	mov	r0, r5
	bl	sprintf
	ldr	r1, .L805+84
	ldr	r2, [r9, #-3844]
	add	r5, r5, r0
	mov	r0, r5
	bl	sprintf
	add	r5, r5, r0
	bl	FtlBbtCalcTotleCnt
	ldrh	r2, [r4, #6]
	ldr	r1, .L805+88
	mov	r3, r0
	mov	r0, r5
	bl	sprintf
	ldr	r1, .L805+92
	ldrh	r2, [fp, #-12]
	add	r5, r5, r0
	mov	r0, r5
	bl	sprintf
	ldr	r1, .L805+96
	ldr	r2, [r9, #-3840]
	add	r5, r5, r0
	mov	r0, r5
	bl	sprintf
	ldr	r1, .L805+100
	ldr	r2, [r9, #-3836]
	add	r5, r5, r0
	mov	r0, r5
	bl	sprintf
	ldr	r1, .L805+104
	ldr	r2, [r9, #-3832]
	add	r5, r5, r0
	mov	r0, r5
	bl	sprintf
	ldr	r1, .L805+108
	ldr	r2, [r6, #4064]
	add	r5, r5, r0
	mov	r0, r5
	bl	sprintf
	ldr	r1, .L805+112
	ldr	r2, [r9, #-3828]
	add	r5, r5, r0
	mov	r0, r5
	bl	sprintf
	ldr	r1, .L805+116
	ldr	r2, [r9, #-3824]
	add	r5, r5, r0
	mov	r0, r5
	bl	sprintf
	ldrh	r2, [r10, #30]
	ldr	r1, .L805+120
	add	r5, r5, r0
	mov	r0, r5
	bl	sprintf
	ldrh	r2, [r10, #28]
	ldr	r1, .L805+124
	sub	r10, r9, #3808
	add	r5, r5, r0
	mov	r0, r5
	bl	sprintf
	ldr	r1, .L805+128
	ldr	r2, [r6, #1912]
	add	r5, r5, r0
	mov	r0, r5
	bl	sprintf
	ldr	r1, .L805+132
	ldr	r2, [r6, #1904]
	add	r5, r5, r0
	mov	r0, r5
	bl	sprintf
	ldr	r1, .L805+136
	ldr	r2, [r6, #1800]
	add	r5, r5, r0
	mov	r0, r5
	bl	sprintf
	ldrh	r2, [r4, #66]
	ldr	r1, .L805+140
	add	r5, r5, r0
	mov	r0, r5
	bl	sprintf
	movw	r3, #1812
	ldrh	r2, [r6, r3]
	ldr	r1, .L805+144
	add	r5, r5, r0
	mov	r0, r5
	bl	sprintf
	ldrh	r2, [r10, #-12]
	ldr	r1, .L805+148
	add	r5, r5, r0
	mov	r0, r5
	bl	sprintf
	ldr	r1, .L805+152
	ldr	r2, [r6, #1816]
	add	r5, r5, r0
	mov	r0, r5
	bl	sprintf
	ldrh	r2, [r10, #-8]
	ldr	r1, .L805+156
	add	r5, r5, r0
	mov	r0, r5
	bl	sprintf
	ldrh	r2, [r4]
	ldr	r1, .L805+160
	sub	r4, r9, #3984
	add	r5, r5, r0
	mov	r0, r5
	bl	sprintf
	ldrh	r2, [r10, #-230]
	ldr	r1, .L805+164
	add	r5, r5, r0
	mov	r0, r5
	bl	sprintf
	ldr	r1, .L805+168
	ldrb	r2, [r9, #-4034]	@ zero_extendqisi2
	add	r5, r5, r0
	mov	r0, r5
	bl	sprintf
	ldr	r1, .L805+172
	ldrh	r2, [fp, #-8]
	add	r5, r5, r0
	mov	r0, r5
	bl	sprintf
	ldr	r1, .L805+176
	ldrb	r2, [r9, #-4032]	@ zero_extendqisi2
	add	r5, r5, r0
	mov	r0, r5
	bl	sprintf
	ldrh	r2, [r10, #-228]
	ldr	r1, .L805+180
	add	r5, r5, r0
	mov	r0, r5
	bl	sprintf
	ldrh	r3, [fp, #-8]
	ldr	r2, [r9, #-4060]
	ldr	r1, .L805+184
	mov	r3, r3, asl #1
	ldrh	r2, [r2, r3]
	add	r5, r5, r0
	mov	r0, r5
	bl	sprintf
	ldrh	r2, [r10, #-182]
	ldr	r1, .L805+188
	add	r5, r5, r0
	mov	r0, r5
	bl	sprintf
	ldr	r1, .L805+192
	ldrb	r2, [r9, #-3986]	@ zero_extendqisi2
	add	r5, r5, r0
	mov	r0, r5
	bl	sprintf
	ldrh	r2, [r4, #-8]
	ldr	r1, .L805+196
	add	r5, r5, r0
	mov	r0, r5
	bl	sprintf
	ldr	r1, .L805+200
	ldrb	r2, [r9, #-3984]	@ zero_extendqisi2
	add	r5, r5, r0
	mov	r0, r5
	bl	sprintf
	ldrh	r2, [r10, #-180]
	ldr	r1, .L805+204
	sub	r10, r9, #3936
	add	r5, r5, r0
	mov	r0, r5
	bl	sprintf
	ldrh	r3, [r4, #-8]
	ldr	r2, [r9, #-4060]
	ldr	r1, .L805+208
	mov	r3, r3, asl #1
	ldrh	r2, [r2, r3]
	add	r5, r5, r0
	mov	r0, r5
	bl	sprintf
	ldrh	r2, [r4, #42]
	ldr	r1, .L805+212
	add	r5, r5, r0
	mov	r0, r5
	bl	sprintf
	ldr	r1, .L805+216
	ldrb	r2, [r9, #-3938]	@ zero_extendqisi2
	add	r5, r5, r0
	mov	r0, r5
	bl	sprintf
	ldrh	r2, [r10, #-8]
	ldr	r1, .L805+220
	sub	r10, r9, #3792
	add	r5, r5, r0
	mov	r0, r5
	bl	sprintf
	ldr	r1, .L805+224
	ldrb	r2, [r9, #-3936]	@ zero_extendqisi2
	add	r5, r5, r0
	mov	r0, r5
	bl	sprintf
	ldrh	r2, [r4, #44]
	ldr	r1, .L805+228
	add	r5, r5, r0
	mov	r0, r5
	bl	sprintf
	ldrh	r2, [r4, #186]
	ldr	r1, .L805+232
	add	r5, r5, r0
	mov	r0, r5
	bl	sprintf
	ldr	r1, .L805+236
	ldrb	r2, [r9, #-3794]	@ zero_extendqisi2
	add	r5, r5, r0
	mov	r0, r5
	bl	sprintf
	ldr	r1, .L805+240
	ldrh	r2, [r10, #-8]
	add	r5, r5, r0
	mov	r0, r5
	bl	sprintf
	ldr	r1, .L805+244
	ldrb	r2, [r9, #-3792]	@ zero_extendqisi2
	add	r5, r5, r0
	mov	r0, r5
	bl	sprintf
	ldrh	r2, [r4, #188]
	ldr	r1, .L805+248
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
	ldr	r1, .L805+252
	bl	sprintf
	ldr	r1, .L805+256
	ldr	r2, [r9, #-3676]
	add	r4, r5, r0
	sub	r5, r9, #3232
	mov	r0, r4
	bl	sprintf
	ldr	r1, .L805+260
	ldr	r2, [r9, #-3652]
	add	r4, r4, r0
	mov	r0, r4
	bl	sprintf
	ldr	r1, .L805+264
	ldrh	r2, [r5, #-4]
	add	r4, r4, r0
	mov	r0, r4
	bl	sprintf
	ldr	r1, .L805+268
	ldrh	r2, [r5, #-2]
	add	r4, r4, r0
	mov	r0, r4
	bl	sprintf
	ldr	r1, .L805+272
	ldr	r2, [r9, #-3232]
	add	r4, r4, r0
	mov	r0, r4
	bl	sprintf
	sub	r3, r9, #3216
	ldr	r1, .L805+276
	ldrh	r2, [r3, #-12]
	add	r4, r4, r0
	mov	r0, r4
	bl	sprintf
	add	r4, r4, r0
	bl	GetFreeBlockMinEraseCount
	ldr	r1, .L805+280
	mov	r2, r0
	mov	r0, r4
	bl	sprintf
	add	r4, r4, r0
	ldrh	r0, [fp, #-12]
	bl	GetFreeBlockMaxEraseCount
	ldr	r1, .L805+284
	mov	r2, r0
	mov	r0, r4
	bl	sprintf
	ldrh	r3, [r10, #-8]
	movw	r2, #65535
	cmp	r3, r2
	add	r4, r4, r0
	beq	.L796
	ldr	r2, [r9, #-4060]
	mov	r3, r3, asl #1
	mov	r0, r4
	ldr	r1, .L805+288
	ldrh	r2, [r2, r3]
	bl	sprintf
	add	r4, r4, r0
.L796:
	mov	r0, #0
	mov	r5, #0
	bl	List_get_gc_head_node
	mov	fp, #6
	movw	ip, #65535
	uxth	r3, r0
.L798:
	cmp	r3, ip
	str	ip, [sp, #20]
	beq	.L797
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
	ldr	r1, .L805+292
	str	r2, [sp, #8]
	mov	r2, r5
	bl	sprintf
	add	r5, r5, #1
	ldr	r3, [r9, #-4068]
	cmp	r5, #16
	ldrh	r3, [r3, r10]
	add	r4, r4, r0
	ldr	ip, [sp, #20]
	bne	.L798
.L797:
	ldr	r3, [r9, #-4048]
	mov	r10, #0
	ldr	r5, [r9, #-4068]
	movw	ip, #65535
	rsb	r5, r5, r3
	ldr	r3, .L805+296
	mov	r5, r5, asr #1
	mul	r3, r3, r5
	mov	r5, #6
	uxth	r3, r3
.L800:
	cmp	r3, ip
	str	ip, [sp, #20]
	beq	.L799
	mul	fp, r5, r3
	ldr	r2, [r9, #-4068]
	ldr	r1, [r6, #4060]
	mov	r0, r4
	add	r2, r2, fp
	ldrh	r2, [r2, #4]
	str	r2, [sp]
	mov	r2, r3, asl #1
	ldrh	r2, [r1, r2]
	ldr	r1, .L805+300
	str	r2, [sp, #4]
	mov	r2, r10
	bl	sprintf
	add	r10, r10, #1
	ldr	r3, [r9, #-4068]
	cmp	r10, #4
	ldrh	r3, [r3, fp]
	add	r4, r4, r0
	ldr	ip, [sp, #20]
	bne	.L800
.L799:
	ldr	r2, [sp, #44]
	rsb	r0, r8, r4
	ldr	r3, [r7]
	cmp	r2, r3
	beq	.L801
	bl	__stack_chk_fail
.L801:
	add	sp, sp, #52
	@ sp needed
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L806:
	.align	2
.L805:
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
	ldr	r1, .L809
	ldr	r2, .L809+4
	bl	sprintf
	add	r4, r5, r0
	mov	r0, r4
	bl	FtlPrintInfo2buf
	add	r0, r4, r0
	rsb	r0, r5, r0
	ldmfd	sp!, {r3, r4, r5, pc}
.L810:
	.align	2
.L809:
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
	ldr	r4, .L841
	ldr	r2, [r4, #-3832]
	ldr	r3, [r4, #-3824]
	cmp	r2, r3
	bcs	.L812
	ldr	r2, .L841+4
	movw	r1, #1812
	mov	r3, #0
	str	r3, [r4, #-3840]
	ldr	r0, [r2, #4060]
	mov	r5, r2
	ldrh	r1, [r2, r1]
	sub	r0, r0, #2
.L813:
	cmp	r3, r1
	bcs	.L840
	ldrh	r2, [r0, #2]!
	add	r3, r3, #1
	ldr	ip, [r4, #-3840]
	add	r2, r2, ip
	str	r2, [r4, #-3840]
	b	.L813
.L840:
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
	b	.L815
.L812:
	ldr	r3, [r4, #-3828]
	cmp	r2, r3
	bls	.L815
	ldr	lr, .L841+4
	add	r3, r3, #1
	ldr	ip, .L841+8
	str	r3, [r4, #-3828]
	mov	r3, #0
.L817:
	ldrh	r2, [ip]
	cmp	r3, r2
	bcs	.L815
	ldr	r0, [lr, #4060]
	mov	r1, r3, asl #1
	add	r3, r3, #1
	ldrh	r2, [r0, r1]
	add	r2, r2, #1
	strh	r2, [r0, r1]	@ movhi
	b	.L817
.L815:
	ldr	r6, [r4, #-3824]
	ldr	r8, [r4, #-3832]
	add	r3, r6, #256
	cmp	r3, r8
	bls	.L820
	ldr	r2, .L841
	add	r3, r6, #768
	ldr	r2, [r2, #-3828]
	cmp	r3, r2
	bls	.L820
	ldr	r3, .L841+4
	cmp	r6, #40
	ldr	r2, [r3, #1796]
	movls	r3, #0
	movhi	r3, #1
	cmp	r2, #0
	orreq	r3, r3, #1
	cmp	r3, #0
	beq	.L820
.L822:
	movw	r0, #65535
	b	.L821
.L820:
	ldr	r5, .L841
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
	bne	.L822
	ldr	r3, [r5, #-4064]
	cmp	r3, #0
	beq	.L822
	ldr	r2, .L841+4
	movw	r0, #1812
	ldr	ip, .L841+12
	movw	r7, #65535
	mov	lr, #6
	ldrh	r0, [r2, r0]
	ldr	r10, [r2, #4060]
	str	r0, [sp, #20]
	ldr	r0, [r5, #-4068]
	mov	r5, r7
.L823:
	ldrh	r2, [r3]
	movw	fp, #65535
	cmp	r2, fp
	beq	.L825
	add	r1, r1, #1
	ldr	fp, [sp, #20]
	uxth	r1, r1
	cmp	r1, fp
	bhi	.L822
	ldrh	fp, [r3, #4]
	cmp	fp, #0
	beq	.L824
	rsb	r3, r0, r3
	mov	r3, r3, asr #1
	mul	r3, ip, r3
	uxth	r3, r3
	mov	fp, r3, asl #1
	ldrh	fp, [r10, fp]
	cmp	fp, r6
	bls	.L829
	cmp	fp, r7
	movcc	r7, fp
	movcc	r5, r3
.L824:
	mla	r3, lr, r2, r0
	b	.L823
.L829:
	mov	r5, r3
.L825:
	movw	r3, #65535
	cmp	r5, r3
	beq	.L822
	mov	r3, r5, asl #1
	ldrh	fp, [r10, r3]
	cmp	fp, r6
	bls	.L827
	str	r3, [sp, #20]
	bl	GetFreeBlockMinEraseCount
	ldr	r3, [sp, #20]
	cmp	r0, r6
	strhi	r7, [r4, #-3824]
.L827:
	cmp	fp, r8
	bcs	.L822
	add	r2, fp, #128
	cmp	r9, r2
	ble	.L822
	add	r2, fp, #256
	cmp	r2, r8
	bcc	.L828
	ldr	r2, [r4, #-3828]
	add	fp, fp, #768
	cmp	fp, r2
	bcs	.L822
.L828:
	ldr	r2, [r4, #-4060]
	mov	r1, r5
	ldr	r0, .L841+16
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
.L821:
	add	sp, sp, #28
	@ sp needed
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L842:
	.align	2
.L841:
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
	beq	.L844
	ldr	r2, .L846
	mov	r3, r0, asl #1
	mov	r1, #0
	ldr	r2, [r2, #-4060]
	strh	r1, [r2, r3]	@ movhi
	bl	INSERT_FREE_LIST
.L844:
	mov	r0, #0
	ldmfd	sp!, {r3, pc}
.L847:
	.align	2
.L846:
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
	ldr	ip, .L856
	mov	r3, #0
	ldr	r1, .L856+4
	stmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, lr}
	.save {r4, r5, r6, r7, r8, r9, r10, lr}
	add	r7, ip, #78
	mov	r5, #12
	mov	r6, #1
	mov	r8, #36
	str	r3, [r1, #-3220]
.L849:
	ldrh	r2, [ip]
	add	r4, r3, #1
	uxth	r3, r3
	cmp	r3, r2
	bcs	.L854
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
	ldr	r2, .L856+8
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
	b	.L849
.L854:
	ldr	r0, .L856+4
	mov	ip, #12
	ldr	r4, .L856+12
	mov	lr, #0
.L851:
	ldr	r3, [r1, #-3200]
	cmp	r2, r3
	bcs	.L855
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
	ldr	r3, .L856+8
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
	b	.L851
.L855:
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, pc}
.L857:
	.align	2
.L856:
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
	ldr	r3, .L866
	stmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, lr}
	.save {r4, r5, r6, r7, r8, r9, r10, lr}
	mov	lr, #0
	ldr	r6, [r3, #-3200]
	mov	r5, #36
	ldr	r4, [r3, #-3216]
	mov	r7, lr
	mov	r8, #12
.L859:
	uxth	ip, lr
	cmp	ip, r1
	ldmcsfd	sp!, {r4, r5, r6, r7, r8, r9, r10, pc}
	mla	ip, r5, ip, r0
	mov	r2, #0
.L860:
	uxth	r3, r2
	cmp	r3, r6
	bcs	.L861
	mul	r3, r8, r3
	add	r2, r2, #1
	add	r9, r4, r3
	ldr	r10, [r4, r3]
	ldr	r3, [ip, #8]
	cmp	r10, r3
	bne	.L860
	str	r7, [r9, #8]
.L861:
	add	lr, lr, #1
	b	.L859
.L867:
	.align	2
.L866:
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
	ldr	r3, .L877
	mov	ip, #0
	stmfd	sp!, {r4, r5, r6, r7, r8, r9, lr}
	.save {r4, r5, r6, r7, r8, r9, lr}
	mov	r6, #12
	ldr	r4, [r3, #-3200]
	mov	r7, #1
	ldr	r5, [r3, #-3216]
	mov	r8, #36
.L869:
	uxth	r2, ip
	cmp	r2, r1
	bcs	.L876
	mov	lr, #0
.L870:
	uxth	r3, lr
	cmp	r3, r4
	bcs	.L871
	mla	r3, r6, r3, r5
	add	lr, lr, #1
	ldr	r9, [r3, #8]
	cmp	r9, #0
	bne	.L870
	mla	r2, r8, r2, r0
	ldr	lr, [r3]
	str	r7, [r3, #8]
	str	lr, [r2, #8]
	ldr	r3, [r3, #4]
	str	r3, [r2, #12]
.L871:
	add	ip, ip, #1
	b	.L869
.L876:
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, pc}
.L878:
	.align	2
.L877:
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
	ldr	r2, .L885
	sub	r2, r2, #3184
	ldr	r3, [r2, #-12]
	ldrh	r2, [r2, #-8]
	add	r2, r3, r2, asl #1
.L880:
	cmp	r3, r2
	beq	.L884
	ldrh	r1, [r3], #2
	cmp	r1, r0
	bne	.L880
	mov	r0, #1
	bx	lr
.L884:
	mov	r0, #0
	bx	lr
.L886:
	.align	2
.L885:
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
	ldr	r4, .L895
	ubfx	r0, r0, #10, #16
	mov	r6, r1
	mov	r7, r2
	bl	P2V_block_in_plane
	sub	r3, r4, #3184
	ldr	r2, [r4, #-3196]
	mov	lr, #0
	ldrh	r1, [r3, #-8]
	sub	r8, r2, #2
.L888:
	uxth	ip, lr
	cmp	ip, r1
	bcs	.L892
	ldrh	r9, [r8, #2]!
	add	lr, lr, #1
	cmp	r9, r0
	bne	.L888
.L892:
	cmp	ip, r1
	moveq	ip, ip, asl #1
	streqh	r0, [r2, ip]	@ movhi
	ldreqh	ip, [r3, #-8]
	addeq	ip, ip, #1
	streqh	ip, [r3, #-8]	@ movhi
.L890:
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
.L896:
	.align	2
.L895:
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
	ldr	r4, .L905
	ldrh	r3, [r4, #-14]
	cmp	r3, r0
	beq	.L899
	ldrh	r3, [r4, #-12]
	cmp	r3, r0
	beq	.L899
	ldrh	r3, [r4, #-10]
	cmp	r3, r0
	beq	.L899
	ldrh	r3, [r4, #-8]
	cmp	r3, r0
	beq	.L899
	ldr	r0, .L905+4
	mov	r1, r5
	bl	printk
	ldrh	r2, [r4, #-14]
	movw	r3, #65535
	cmp	r2, r3
	streqh	r5, [r4, #-14]	@ movhi
	beq	.L899
	ldrh	r2, [r4, #-12]
	cmp	r2, r3
	streqh	r5, [r4, #-12]	@ movhi
	beq	.L899
	ldrh	r2, [r4, #-10]
	cmp	r2, r3
	streqh	r5, [r4, #-10]	@ movhi
	beq	.L899
	ldrh	r2, [r4, #-8]
	cmp	r2, r3
	streqh	r5, [r4, #-8]	@ movhi
.L899:
	mov	r0, #0
	ldmfd	sp!, {r3, r4, r5, pc}
.L906:
	.align	2
.L905:
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
	ldr	r4, .L918
	ldrh	r3, [r4, #-14]
	cmp	r3, r0
	beq	.L915
	ldrh	r3, [r4, #-12]
	cmp	r3, r0
	beq	.L915
	ldrh	r3, [r4, #-10]
	cmp	r3, r0
	beq	.L915
	ldrh	r3, [r4, #-8]
	cmp	r3, r0
	beq	.L915
	ldr	r0, .L918+4
	mov	r1, r5
	bl	printk
	ldrh	r2, [r4, #-14]
	movw	r3, #65535
	cmp	r2, r3
	streqh	r5, [r4, #-14]	@ movhi
	beq	.L915
	ldrh	r2, [r4, #-12]
	cmp	r2, r3
	streqh	r5, [r4, #-12]	@ movhi
	beq	.L915
	ldrh	r2, [r4, #-10]
	cmp	r2, r3
	streqh	r5, [r4, #-10]	@ movhi
	beq	.L915
	ldrh	r2, [r4, #-8]
	cmp	r2, r3
	bne	.L916
	strh	r5, [r4, #-8]	@ movhi
.L915:
	mov	r0, #0
	ldmfd	sp!, {r3, r4, r5, pc}
.L916:
	mvn	r0, #0
	ldmfd	sp!, {r3, r4, r5, pc}
.L919:
	.align	2
.L918:
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
	ldr	r6, .L930
	mov	r2, r5
	ldrh	r1, [r6, #-6]
	mov	r4, r0
	ldr	r0, .L930+4
	bl	printk
	mov	r0, r4
	bl	FtlGcRefreshBlock
	ldr	r3, .L930+8
	ldr	r2, [r3, #1796]
	cmp	r2, #0
	beq	.L921
	ldr	r2, [r3, #4060]
	mov	r4, r4, asl #1
	ldrh	r3, [r2, r4]
	cmp	r3, #39
	subhi	r3, r3, #40
	strhih	r3, [r2, r4]	@ movhi
.L921:
	ldrh	r3, [r6, #-6]
	mov	r2, #0
	ldr	r0, .L930+12
.L922:
	uxth	r1, r2
	cmp	r1, r3
	bcs	.L929
	add	r2, r2, #1
	add	r1, r0, r2, asl #1
	ldrh	r1, [r1, #-2]
	cmp	r1, r5
	bne	.L922
	b	.L923
.L929:
	cmp	r3, #15
	addls	r2, r3, #1
	strlsh	r2, [r6, #-6]	@ movhi
	ldrls	r2, .L930+12
	movls	r3, r3, asl #1
	strlsh	r5, [r2, r3]	@ movhi
.L923:
	mov	r0, #0
	ldmfd	sp!, {r4, r5, r6, pc}
.L931:
	.align	2
.L930:
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
	ldr	r4, .L939
	ldrh	r3, [r4, #-6]
	cmp	r3, #0
	beq	.L933
	ldrh	r1, [r4, #-14]
	movw	r2, #65535
	cmp	r1, r2
	bne	.L933
	add	r4, r4, #32
	ldrh	r2, [r4, #-2]
	cmp	r2, r3
	ldr	r2, .L939+4
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
.L933:
	mov	r0, #0
	ldmfd	sp!, {r4, pc}
.L940:
	.align	2
.L939:
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
	bne	.L943
	ldr	r3, .L973
.L944:
	mov	r2, r0, asl #1
	strh	r0, [r2, r3]	@ movhi
	add	r0, r0, #1
	cmp	r0, #256
	bne	.L944
.L948:
	mov	r1, #255
	ldr	r0, .L973+4
	mov	r2, #1024
	uxth	r4, r4
	bl	ftl_memset
	mov	r3, #0
	ldr	r1, .L973
	ldr	r0, .L973+4
	b	.L945
.L943:
	cmp	r0, #1
	bne	.L946
	ldr	ip, .L973
	mov	r3, #0
.L947:
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
	bne	.L947
	b	.L948
.L946:
	cmp	r0, #2
	bne	.L949
	ldr	r1, .L973
	mov	r3, #0
.L950:
	uxth	r2, r3
	mov	r0, r3, asl #1
	cmp	r2, #1
	add	r3, r3, #1
	mov	ip, r2, asl #1
	subhi	r2, ip, #1
	cmp	r3, #256
	strh	r2, [r0, r1]	@ movhi
	bne	.L950
	b	.L948
.L949:
	cmp	r0, #3
	bne	.L951
	ldr	ip, .L973
	mov	r3, #0
.L952:
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
	bne	.L952
	b	.L948
.L951:
	cmp	r0, #4
	mov	r3, #0
	bne	.L953
	ldr	r2, .L973+8
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
.L954:
	tst	r3, #1
	movne	r1, #7
	moveq	r1, #6
	rsb	r1, r1, r3, asl #1
	add	r3, r3, #1
	strh	r1, [r2, #2]!	@ movhi
	uxth	r3, r3
	cmp	r3, #256
	bne	.L954
	b	.L948
.L953:
	cmp	r0, #5
	bne	.L955
	ldr	r2, .L973
.L956:
	mov	r1, r3, asl #1
	strh	r3, [r1, r2]	@ movhi
	add	r3, r3, #1
	cmp	r3, #16
	bne	.L956
	ldr	r2, .L973+12
.L957:
	strh	r3, [r2, #2]!	@ movhi
	add	r3, r3, #2
	uxth	r3, r3
	cmp	r3, #496
	bne	.L957
	b	.L948
.L955:
	cmp	r0, #6
	bne	.L958
	ldr	r1, .L973+16
.L959:
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
	bne	.L959
	b	.L948
.L958:
	cmp	r0, #9
	bne	.L948
	ldr	r2, .L973+8
	movw	r1, #509
	strh	r3, [r2, #144]	@ movhi
	mov	r3, #1
	strh	r3, [r2, #146]	@ movhi
	mov	r3, #2
	strh	r3, [r2, #148]!	@ movhi
	mov	r3, #3
.L960:
	strh	r3, [r2, #2]!	@ movhi
	add	r3, r3, #2
	uxth	r3, r3
	cmp	r3, r1
	bne	.L960
	b	.L948
.L945:
	uxth	r2, r3
	cmp	r2, r4
	bcs	.L972
	mov	r2, r3, asl #1
	add	r3, r3, #1
	ldrh	r2, [r2, r1]
	mov	ip, r2, asl #1
	strh	r2, [r0, ip]	@ movhi
	b	.L945
.L972:
	ldmfd	sp!, {r4, pc}
.L974:
	.align	2
.L973:
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
	ldr	r3, .L990
	ldr	r4, .L990+4
	ldr	r9, .L990+8
	ldrh	r0, [r3, #10]
	strb	r6, [r4, #1717]
	mov	r7, r4
	strb	r6, [r9, #-2112]
	bl	FlashBlockAlignInit
	mov	r1, r6
	mov	r2, #8
	ldr	r0, .L990+12
	bl	ftl_memset
	mov	r1, r6
	mov	r2, #32
	add	r0, r4, #656
	bl	ftl_memset
	ldr	r0, .L990+16
	mov	r1, r6
	mov	r2, #128
	bl	ftl_memset
	ldr	r5, [r4, #692]
	ldr	fp, .L990+20
	add	r8, r5, #1
.L977:
	mov	r0, r8
	add	r1, fp, r6, asl #3
	ldrb	r2, [r5]	@ zero_extendqisi2
	bl	FlashMemCmp8
	ldr	r10, .L990+20
	cmp	r0, #0
	bne	.L976
	ldrb	r3, [r7, #1717]	@ zero_extendqisi2
	add	r2, r7, r3, asl #2
	str	r0, [r2, #656]
	add	r2, r3, #1
	add	r3, r7, r3
	strb	r2, [r7, #1717]
	strb	r6, [r3, #1720]
.L976:
	add	r6, r6, #1
	cmp	r6, #4
	bne	.L977
	ldrb	r3, [r4, #1717]	@ zero_extendqisi2
	ldr	r7, .L990+4
	strb	r3, [r9, #-2112]
	ldrb	r3, [r5, #8]	@ zero_extendqisi2
	cmp	r3, #2
	beq	.L978
.L982:
	ldrb	r3, [r5, #13]	@ zero_extendqisi2
	ldrb	r2, [r4, #1717]	@ zero_extendqisi2
	smulbb	r2, r2, r3
	ldrh	r3, [r5, #14]
	smulbb	r3, r2, r3
	ldr	r2, .L990+24
	strh	r3, [r2, #-14]	@ movhi
	ldmfd	sp!, {r3, r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L978:
	ldr	r9, [r7, #4]
	mov	r6, #0
.L981:
	mov	r0, r8
	add	r1, r10, r6, asl #3
	ldrb	r2, [r5]	@ zero_extendqisi2
	bl	FlashMemCmp8
	cmp	r0, #0
	bne	.L979
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
.L979:
	add	r6, r6, #1
	cmp	r6, #4
	bne	.L981
	b	.L982
.L991:
	.align	2
.L990:
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
	ldr	r2, .L997
	ldr	r0, .L997+4
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
.L993:
	uxtb	r2, r3
	cmp	r2, r1
	bcs	.L996
	ldrb	lr, [r3, r0]	@ zero_extendqisi2
	add	r3, r3, #1
	ldrb	r2, [r4, #10]	@ zero_extendqisi2
	orr	r2, r2, ip, asl lr
	strb	r2, [r4, #10]
	b	.L993
.L996:
	ldmfd	sp!, {r4, pc}
.L998:
	.align	2
.L997:
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
	ldr	r4, .L1104
	mvn	r2, #0
	ldr	r5, .L1104+4
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
	ldr	r6, .L1104+8
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
	ldr	ip, .L1104+12
	mov	r2, r2, asl #2
	mov	r1, r2
	str	r0, [r5, #1964]
.L1001:
	cmp	r3, lr
	bcs	.L1102
	ldr	r0, [r5, #1964]
	add	r3, r3, #1
	add	r0, r0, r1
	add	r1, r1, r2
	str	r0, [ip, #4]!
	b	.L1001
.L1102:
	ldr	r2, .L1104+16
	mov	r1, #0
.L1003:
	cmp	r3, #8
	addne	r0, r2, r3, asl #2
	addne	r3, r3, #1
	strne	r1, [r0, #28]
	bne	.L1003
.L1103:
	ldr	r2, [r4, #-2012]
	ldr	r3, .L1104
	cmp	r2, #0
	bne	.L1005
.L1007:
	ldr	r1, .L1104+20
	ldr	r0, .L1104+24
	bl	printk
	mvn	r0, #0
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, pc}
.L1005:
	ldr	r2, [r3, #-2008]
	cmp	r2, #0
	beq	.L1007
	ldr	r2, [r3, #-1992]
	cmp	r2, #0
	beq	.L1007
	ldr	r2, [r3, #-1984]
	cmp	r2, #0
	beq	.L1007
	ldr	r2, [r3, #-3896]
	cmp	r2, #0
	beq	.L1007
	ldr	r2, [r3, #-1980]
	cmp	r2, #0
	beq	.L1007
	ldr	r2, [r3, #-4068]
	cmp	r2, #0
	beq	.L1007
	ldr	r2, [r5, #1964]
	cmp	r2, #0
	beq	.L1007
	ldr	r3, [r3, #-4060]
	cmp	r3, #0
	beq	.L1007
	ldr	r2, [r4, #-3196]
	ldr	r3, .L1104
	cmp	r2, #0
	beq	.L1007
	ldr	r2, [r3, #-3188]
	cmp	r2, #0
	beq	.L1007
	ldr	r2, [r3, #-2076]
	cmp	r2, #0
	beq	.L1007
	ldr	r2, [r3, #-2068]
	cmp	r2, #0
	beq	.L1007
	ldr	r2, [r5, #4056]
	cmp	r2, #0
	beq	.L1007
	ldr	r2, [r3, #-3204]
	cmp	r2, #0
	beq	.L1007
	ldr	r2, [r3, #-2072]
	cmp	r2, #0
	beq	.L1007
	ldr	r2, [r3, #-2064]
	cmp	r2, #0
	beq	.L1007
	ldr	r2, [r3, #-2060]
	cmp	r2, #0
	beq	.L1007
	ldr	r3, [r3, #-2056]
	cmp	r3, #0
	beq	.L1007
	ldr	r2, [r4, #-3212]
	ldr	r3, .L1104
	cmp	r2, #0
	beq	.L1007
	ldr	r2, [r3, #-2048]
	cmp	r2, #0
	beq	.L1007
	ldr	r2, [r3, #-2044]
	cmp	r2, #0
	beq	.L1007
	ldr	r2, [r3, #-3216]
	cmp	r2, #0
	beq	.L1007
	ldr	r2, [r3, #-2040]
	cmp	r2, #0
	beq	.L1007
	ldr	r2, [r3, #-2036]
	cmp	r2, #0
	beq	.L1007
	ldr	r2, [r3, #-3208]
	cmp	r2, #0
	beq	.L1007
	ldr	r2, .L1104+4
	ldr	r1, [r2, #4060]
	cmp	r1, #0
	beq	.L1007
	ldr	r3, [r3, #-2024]
	cmp	r3, #0
	beq	.L1007
	ldr	r3, [r2, #1920]
	cmp	r3, #0
	beq	.L1007
	ldr	r3, .L1104
	ldr	r2, [r3, #-2004]
	cmp	r2, #0
	beq	.L1007
	ldr	r2, [r3, #-2000]
	cmp	r2, #0
	beq	.L1007
	ldr	r3, [r3, #-1996]
	cmp	r3, #0
	beq	.L1007
	mov	r0, #0
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, pc}
.L1105:
	.align	2
.L1104:
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
	ldr	r3, .L1112
	stmfd	sp!, {r4, r5, r6, lr}
	.save {r4, r5, r6, lr}
	mov	r4, r0
	ldrh	r2, [r3, #-8]
	mov	r0, r1
	mov	r5, r1
	mov	r1, #0
	movw	r6, #65535
	mov	r2, r2, asl #2
	bl	ftl_memset
	add	r3, r4, #1020
	ldr	ip, .L1112+4
	add	r3, r3, #2
	sub	r1, r4, #2
	mov	r4, #1
.L1108:
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
	bne	.L1108
	ldmfd	sp!, {r4, r5, r6, pc}
.L1113:
	.align	2
.L1112:
	.word	.LANCHOR2-1968
	.word	.LANCHOR0+1936
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
	ldr	r0, .L1115
	mvn	r3, #0
	mov	r1, #255
	mov	r2, #16
	add	r0, r0, #12
	strh	r3, [r0, #-12]	@ movhi
	mov	r3, #0
	strh	r3, [r0, #-6]	@ movhi
	b	ftl_memset
.L1116:
	.align	2
.L1115:
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
	ldr	r1, .L1119
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
.L1120:
	.align	2
.L1119:
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
.L1122:
	ldrh	r1, [r4, #6]
	uxth	r3, r2
	cmp	r1, r3
	bls	.L1142
	ldr	r0, [r6, r3, asl #2]
	mov	r1, #0
	ubfx	r0, r0, #10, #16
.L1123:
	ldrh	ip, [r4, #10]
	uxth	r3, r1
	cmp	ip, r3
	bls	.L1143
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
	b	.L1123
.L1143:
	add	r2, r2, #1
	b	.L1122
.L1142:
	ldr	r2, .L1145
	movw	r3, #1874
	mov	r8, #0
	mov	r1, r8
	mov	fp, r8
	ldrh	r2, [r2, r3]
	ldrh	r3, [r4]
	mov	r3, r3, asl #1
	strh	r2, [r5, r3]	@ movhi
	ldrh	r9, [r5]
.L1127:
	ldrh	r3, [r4, #10]
	uxth	r6, r8
	cmp	r3, r6
	bls	.L1144
	mov	r2, r6, asl #1
	ldrh	r3, [r5, r2]
	cmp	r9, r3
	bls	.L1128
	ldrh	r0, [r7, r2]
	add	r10, r7, r2
	cmp	r0, #0
	bne	.L1129
	b	.L1130
.L1128:
	cmp	r3, #0
	bne	.L1130
	ldrh	r0, [r7, r2]
	add	r10, r7, r2
	cmp	r0, #0
	movne	r6, r1
	beq	.L1130
	b	.L1132
.L1129:
	cmp	r3, #0
	movne	r1, r6
	movne	r9, r3
	bne	.L1130
	mov	r9, r3
.L1132:
	mov	r1, #1
	bl	FtlFreeSysBlkQueueIn
	strh	fp, [r10]	@ movhi
	ldrh	r3, [r4, #8]
	mov	r1, r6
	sub	r3, r3, #1
	strh	r3, [r4, #8]	@ movhi
.L1130:
	add	r8, r8, #1
	b	.L1127
.L1144:
	mov	r0, r1
	ldmfd	sp!, {r3, r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L1146:
	.align	2
.L1145:
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
	ldr	r5, .L1152
	mvn	r6, #0
	ldr	r4, .L1152+4
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
	ldr	ip, .L1152+8
	mov	r1, #0
	mov	r3, r4
	sub	r7, ip, #28
	mov	r4, #12
	mov	r5, r1
.L1148:
	ldrh	r2, [ip]
	add	lr, r1, #1
	uxth	r1, r1
	ldr	r0, .L1152
	cmp	r2, r1
	bls	.L1151
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
	b	.L1148
.L1151:
	ldr	r1, .L1152+12
	mvn	ip, #0
	sub	r2, r1, #4
	strh	ip, [r1, #-2]	@ movhi
	strh	ip, [r1, #-4]	@ movhi
	ldr	ip, [r0, #1900]
	strh	ip, [r1, #6]	@ movhi
	ldrh	r1, [r1, #40]
	ldr	ip, .L1152+16
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
.L1153:
	.align	2
.L1152:
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
	ldr	r6, .L1156
	mov	r4, #0
	ldr	r5, .L1156+4
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
.L1157:
	.align	2
.L1156:
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
	ldr	r7, .L1170
	mov	r2, #6
	ldr	r6, .L1170+4
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
.L1159:
	ldr	r3, .L1170+8
	uxth	r7, r4
	sxth	r10, r7
	ldrh	r2, [r3]
	cmp	r10, r2
	bge	.L1166
	sub	r3, r3, #8
	ldr	r2, .L1170+12
	mov	ip, r7
	ldrh	r3, [r3]
	str	r3, [sp]
	ldrh	r3, [r2]
	mov	r2, #0
	mov	r6, r2
	str	r3, [sp, #4]
.L1167:
	ldr	r3, [sp]
	sxth	r1, r2
	cmp	r1, r3
	bge	.L1169
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
	b	.L1167
.L1169:
	cmp	r6, #0
	beq	.L1162
	sxth	r1, r6
	mov	r0, #32768
	bl	__aeabi_idiv
	uxth	r6, r0
	b	.L1163
.L1162:
	sxth	r7, r7
	ldr	r2, [r5, #-4060]
	mvn	r1, #0
	mov	r7, r7, asl #1
	strh	r1, [r2, r7]	@ movhi
.L1163:
	mov	r1, r10, asl #1
	ldr	r2, [r5, #-4068]
	add	r0, r1, r10
	add	r2, r2, r0, asl #1
	strh	r6, [r2, #4]	@ movhi
	ldr	r2, .L1170+16
	ldrh	r0, [r2]
	cmp	r10, r0
	beq	.L1164
	ldrh	r0, [r2, #48]
	cmp	r10, r0
	beq	.L1164
	ldrh	r2, [r2, #96]
	cmp	r10, r2
	beq	.L1164
	ldr	r3, [fp, #-4060]
	uxth	r0, r4
	ldrh	r3, [r3, r1]
	cmp	r3, #0
	bne	.L1165
	add	r8, r8, #1
	uxth	r8, r8
	bl	INSERT_FREE_LIST
	b	.L1164
.L1165:
	add	r9, r9, #1
	uxth	r9, r9
	bl	INSERT_DATA_LIST
.L1164:
	add	r4, r4, #1
	b	.L1159
.L1166:
	ldr	r3, [sp, #12]
	mov	r0, #0
	strh	r9, [r3, #-4]	@ movhi
	ldr	r3, .L1170+20
	strh	r8, [r3, #-12]	@ movhi
	add	sp, sp, #28
	@ sp needed
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L1171:
	.align	2
.L1170:
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
	ldr	r4, .L1174
	mov	r2, #0
	ldr	r6, .L1174+4
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
.L1175:
	.align	2
.L1174:
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
	ldr	r1, .L1179
	bl	ftl_memcpy
	mov	r0, #0
	ldmfd	sp!, {r3, pc}
.L1180:
	.align	2
.L1179:
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
	ldr	r8, .L1194
.L1185:
	mov	r7, r4, asl #5
	ldr	r1, .L1194+4
	add	r0, r7, #1
	ldrb	r2, [r8, r4, asl #5]	@ zero_extendqisi2
	add	r0, r8, r0
	ldr	r6, .L1194+8
	bl	FlashMemCmp8
	add	r9, r6, #508
	subs	r5, r0, #0
	bne	.L1182
	adds	r9, r9, r7
	beq	.L1188
	add	r4, r6, r7
	mov	r3, r5
	ldr	r1, .L1194+12
	ldrb	r2, [r4, #530]	@ zero_extendqisi2
	b	.L1187
.L1182:
	add	r4, r4, #1
	cmp	r4, #81
	bne	.L1185
	b	.L1188
.L1193:
	add	r3, r3, #1
	cmp	r3, #4
	beq	.L1186
.L1187:
	ldrb	r0, [r1, r3, asl #5]	@ zero_extendqisi2
	cmp	r0, r2
	bne	.L1193
.L1186:
	ldr	r4, .L1194+16
	mov	r2, #32
	ldr	r1, .L1194+12
	ldr	r0, .L1194+20
	add	r1, r1, r3, asl #5
	bl	ftl_memcpy
	mov	r0, r4
	mov	r1, r9
	mov	r2, #32
	bl	ftl_memcpy
	ldrh	r0, [r4, #10]
	bl	FlashBlockAlignInit
	b	.L1183
.L1188:
	mvn	r5, #0
.L1183:
	mov	r0, r5
	ldmfd	sp!, {r3, r4, r5, r6, r7, r8, r9, pc}
.L1195:
	.align	2
.L1194:
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
	bne	.L1197
	cmp	r3, #0
	beq	.L1198
	mov	r0, r2
	mov	r1, r3
	mov	r2, #1024
	bl	ftl_memcpy
.L1198:
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
.L1197:
	cmp	r3, #0
	beq	.L1201
	mov	r1, r2
	mov	r0, r3
	mov	r2, #1024
	bl	ftl_memcpy
.L1201:
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
.L1214:
	cmp	r3, r2
	ldrne	ip, [r1, r3, asl #2]
	strne	ip, [r0, r3, asl #2]
	addne	r3, r3, #1
	bne	.L1214
.L1216:
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
	ldr	r3, .L1219
	ldr	r0, [r3]
	b	jiffies_to_msecs
.L1220:
	.align	2
.L1219:
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
	ldr	ip, .L1225
	cmp	r2, #0
	moveq	r2, r3
	ldr	ip, [ip, #144]
	add	ip, ip, #4096
	add	r1, ip, r1
	beq	.L1224
	str	lr, [sp, #-4]!
	.save {lr}
	mov	lr, r0
	mov	r0, r1
	mov	r1, lr
	ldr	lr, [sp], #4
	mov	r2, r3
.L1224:
	b	ftl_memcpy
.L1226:
	.align	2
.L1225:
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
	ldr	r3, .L1231
	ldr	r0, [r3, #1912]
	bx	lr
.L1232:
	.align	2
.L1231:
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
.L1237:
	cmp	r4, r8
	beq	.L1246
	cmp	r5, #0
	bne	.L1238
	ldr	r0, .L1247
	mov	r1, r9
	mov	r2, r6
	mov	r3, r4
	bl	printk
.L1238:
	cmp	r7, #4
	ldreq	r0, .L1247+4
	ldreq	r1, [r6, r4, asl #2]
	beq	.L1245
	cmp	r7, #2
	moveq	r3, r4, asl #1
	ldreq	r0, .L1247+4
	ldreqsh	r1, [r6, r3]
	ldrne	r0, .L1247+4
	ldrneb	r1, [r6, r4]	@ zero_extendqisi2
.L1245:
	add	r5, r5, #1
	bl	printk
	cmp	r5, #15
	bls	.L1242
	ldr	r0, .L1247+8
	mov	r5, #0
	ldr	r1, .L1247+12
	bl	printk
.L1242:
	add	r4, r4, #1
	b	.L1237
.L1246:
	ldr	r0, .L1247+8
	ldr	r1, .L1247+12
	ldmfd	sp!, {r3, r4, r5, r6, r7, r8, r9, lr}
	b	printk
.L1248:
	.align	2
.L1247:
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
	ldr	r7, .L1358
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
	bne	.L1250
	mvn	r3, #88
	strb	r3, [r7, #702]
	ldr	r3, .L1358+4
	mvn	r2, #8
	mov	r5, #7
	strb	r2, [r3, #3245]
	b	.L1311
.L1250:
	cmp	r0, #3
	bne	.L1252
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
	b	.L1352
.L1252:
	cmp	r0, #4
	bne	.L1253
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
.L1352:
	mov	r5, #8
	strb	r3, [r7, #709]
	mov	r6, r5
	b	.L1251
.L1253:
	cmp	r0, #5
	bne	.L1254
	mov	r3, #56
	mov	r5, #8
	strb	r3, [r7, #702]
	mov	r3, #57
	strb	r3, [r7, #703]
	mov	r3, #58
	strb	r3, [r7, #704]
	mov	r3, #59
	strb	r3, [r7, #705]
	b	.L1311
.L1254:
	cmp	r0, #6
	bne	.L1255
	mov	r3, #14
	mov	r5, #12
	strb	r3, [r7, #702]
	mov	r3, #15
	strb	r3, [r7, #703]
	mov	r3, #16
	strb	r3, [r7, #704]
	mov	r3, #17
	strb	r3, [r7, #705]
	b	.L1311
.L1255:
	cmp	r0, #7
	bne	.L1256
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
	b	.L1251
.L1256:
	cmp	r0, #8
	mov	r5, #7
	bne	.L1311
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
	b	.L1251
.L1311:
	mov	r6, #4
.L1251:
	sub	r3, r4, #1
	cmp	r3, #1
	bhi	.L1348
	ldr	r10, .L1358+8
	mov	r9, #0
.L1257:
	ldrb	r1, [r7, #1717]	@ zero_extendqisi2
	uxtb	r2, r9
	ldr	r3, .L1358
	cmp	r1, r2
	bls	.L1264
	add	r2, r3, r2
	ldr	r4, .L1358+12
	ldr	r1, .L1358+16
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
.L1259:
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
	bcc	.L1259
	mov	r2, #0
.L1260:
	add	r0, r10, r2
	mov	r3, #1
.L1261:
	ldrb	r1, [r0, r3, asl #2]	@ zero_extendqisi2
	ldrb	ip, [fp]	@ zero_extendqisi2
	add	r1, r1, ip
	strb	r1, [fp, r3, asl #3]
	add	r3, r3, #1
	cmp	r3, #7
	bne	.L1261
	add	r2, r2, #1
	add	fp, fp, #1
	cmp	r2, #4
	bne	.L1260
	mov	r3, #0
	add	r9, r9, #1
	strb	r3, [r4, #16]
	strb	r3, [r4, #24]
	strb	r3, [r4, #32]
	strb	r3, [r4, #40]
	strb	r3, [r4, #48]
	strb	r3, [r4, #41]
	strb	r3, [r4, #49]
	b	.L1257
.L1348:
	sub	r3, r4, #3
	cmp	r3, #5
	bhi	.L1264
	smulbb	r2, r6, r5
	mov	r3, r2, asl #4
	mov	r2, r2, asr #1
	str	r3, [sp, #36]
	mov	r3, r2, asl #1
	str	r3, [sp, #8]
	mov	r3, #0
.L1356:
	str	r3, [sp, #16]
	ldrb	r3, [sp, #16]	@ zero_extendqisi2
	str	r3, [sp, #12]
	ldr	r3, .L1358
	ldr	r2, [sp, #12]
	ldrb	r3, [r3, #1717]	@ zero_extendqisi2
	cmp	r3, r2
	bhi	.L1309
.L1264:
	ldr	r3, .L1358
	strb	r6, [r3, #699]
	strb	r5, [r3, #700]
	add	sp, sp, #44
	@ sp needed
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L1309:
	ldr	r2, [sp, #12]
	ldr	r3, .L1358
	add	r3, r3, r2
	ldrb	r10, [r3, #1720]	@ zero_extendqisi2
	ldr	r3, .L1358
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
	ldreq	r3, .L1358+12
	moveq	r7, #160
	mlaeq	r7, r7, r10, r3
	addeq	r3, r7, #28
	beq	.L1353
.L1266:
	cmp	r4, #8
	beq	.L1268
	ldr	r3, .L1358+12
	add	r7, r3, r10, asl #6
	add	r3, r7, #20
.L1353:
	str	r3, [sp, #20]
	cmp	r4, #4
	add	r3, r8, fp, asl #8
	mov	r2, #54
	str	r2, [r3, #2056]
	bne	.L1269
	add	r9, r8, r9
	mov	r3, #255
	str	r3, [r9, #2052]
	mov	r3, #64
	str	r3, [r9, #2048]
	mov	r3, #204
	b	.L1354
.L1269:
	sub	r2, r4, #5
	cmp	r2, #1
	bhi	.L1271
	ldr	r2, .L1358
	ldrb	r2, [r2, #702]	@ zero_extendqisi2
	str	r2, [r3, #2052]
	mov	r2, #82
	str	r2, [r3, #2048]
	b	.L1270
.L1271:
	cmp	r4, #7
	bne	.L1270
	add	r9, r8, r9
	mov	r3, #174
	str	r3, [r9, #2052]
	mov	r3, #0
	str	r3, [r9, #2048]
	mov	r3, #176
.L1354:
	str	r3, [r9, #2052]
	mov	r3, #77
	str	r3, [r9, #2048]
.L1270:
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
.L1310:
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
	bne	.L1274
	cmp	r4, #7
	movne	ip, #2
	moveq	ip, #32
.L1274:
	ldr	r3, .L1358+20
	add	r0, r8, fp, asl #8
	ldr	r3, [r3, #148]
	mov	r1, r3
.L1275:
	ldr	r2, [r0, #2048]
	strb	r2, [r1], #1
	rsb	r2, r3, r1
	uxtb	r2, r2
	cmp	r2, ip
	bcc	.L1275
	ldr	r2, [sp, #4]
	cmp	r2, #0
	beq	.L1276
	mov	r2, #0
.L1278:
	ldrb	ip, [r3, r2, asl #2]	@ zero_extendqisi2
	uxtb	r1, r2
	cmp	ip, #50
	beq	.L1277
	add	ip, r3, r2, asl #2
	ldrb	ip, [ip, #1]	@ zero_extendqisi2
	cmp	ip, #5
	beq	.L1277
	add	r2, r2, #1
	cmp	r2, #8
	bne	.L1278
	b	.L1279
.L1277:
	cmp	r1, #6
	bls	.L1280
.L1279:
	ldr	r0, .L1358+24
	mov	r1, #0
	bl	printk
.L1281:
	b	.L1281
.L1276:
	cmp	r4, #7
	bne	.L1282
	ldr	r2, [sp, #4]
.L1284:
	ldrb	ip, [r3, r2, asl #2]	@ zero_extendqisi2
	uxtb	r1, r2
	cmp	ip, #12
	beq	.L1283
	add	ip, r3, r2, asl #2
	ldrb	ip, [ip, #1]	@ zero_extendqisi2
	cmp	ip, #10
	beq	.L1283
	add	r2, r2, #1
	cmp	r2, #8
	bne	.L1284
	b	.L1285
.L1283:
	cmp	r1, #7
	bne	.L1280
.L1285:
	ldr	r0, .L1358+24
	mov	r1, #0
	bl	printk
.L1286:
	b	.L1286
.L1282:
	cmp	r4, #6
	bne	.L1280
	sub	r2, r3, #1
	add	r3, r3, #7
.L1287:
	ldrb	r1, [r2, #1]!	@ zero_extendqisi2
	cmp	r1, #12
	beq	.L1280
	ldrb	r1, [r2, #8]	@ zero_extendqisi2
	cmp	r1, #4
	beq	.L1280
	cmp	r2, r3
	bne	.L1287
	ldr	r0, .L1358+24
	mov	r1, #0
	bl	printk
.L1289:
	b	.L1289
.L1280:
	ldr	r1, .L1358+20
	ldr	ip, [sp, #36]
	ldr	r2, [r1, #148]
	add	ip, r2, ip
	mov	r3, r2
.L1290:
	cmp	r3, ip
	ldrne	lr, [r0, #2048]
	strneb	lr, [r3], #1
	bne	.L1290
.L1357:
	ldr	r3, [r1, #148]
	mov	r0, #8
	ldr	r1, [sp, #8]
	add	r1, r3, r1
	str	r1, [sp, #28]
.L1293:
	ldr	ip, [sp, #8]
	add	lr, r1, ip
.L1292:
	ldrh	ip, [r1]
	mvn	ip, ip
	strh	ip, [r1], #2	@ movhi
	cmp	r1, lr
	bne	.L1292
	ldr	ip, [sp, #8]
	subs	r0, r0, #1
	add	r1, r1, ip
	bne	.L1293
.L1294:
	mov	ip, #0
	mov	r0, ip
.L1297:
	mov	r1, #1
	mov	lr, #0
	mov	r1, r1, asl r0
	mov	r7, #16
	str	r7, [sp, #32]
	mov	r7, lr
.L1295:
	ldrh	r9, [r3, lr]
	and	r9, r9, r1
	cmp	r9, r1
	addeq	r7, r7, #1
	ldr	r9, [sp, #8]
	add	lr, lr, r9
	ldr	r9, [sp, #32]
	subs	r9, r9, #1
	str	r9, [sp, #32]
	bne	.L1295
	cmp	r7, #8
	add	r0, r0, #1
	orrhi	ip, ip, r1
	uxthhi	ip, ip
	cmp	r0, #16
	bne	.L1297
	ldr	r1, [sp, #28]
	strh	ip, [r3], #2	@ movhi
	cmp	r3, r1
	bne	.L1294
	ldr	r3, .L1358+20
	ldr	r1, [r3, #148]
	mov	r3, #0
	sub	r0, r1, #4
	add	ip, r1, #28
.L1299:
	ldr	lr, [r0, #4]!
	cmp	lr, #0
	addeq	r3, r3, #1
	cmp	r0, ip
	bne	.L1299
	cmp	r3, #7
	ble	.L1300
	ldr	r0, .L1358+28
	mov	r2, #1
	mov	r3, #1024
	bl	rknand_print_hex
	ldr	r0, .L1358+24
	mov	r1, #0
	bl	printk
.L1301:
	b	.L1301
.L1300:
	cmp	r4, #6
	moveq	r0, #4
	beq	.L1302
	cmp	r4, #7
	moveq	r0, #10
	beq	.L1302
	ldr	r3, [sp, #4]
	cmp	r3, #0
	moveq	r0, #8
	movne	r0, #5
.L1302:
	sub	r9, r6, #1
	ldr	r1, [sp, #20]
	mov	ip, #0
	uxtb	r9, r9
	add	r9, r9, #1
.L1303:
	mov	r7, r1
	mov	r3, r2
.L1304:
	ldrb	lr, [r3], #1	@ zero_extendqisi2
	strb	lr, [r7], #1
	rsb	lr, r2, r3
	uxtb	lr, lr
	cmp	lr, r6
	bcc	.L1304
	add	ip, ip, #1
	add	r2, r2, r9
	cmp	ip, r5
	add	r1, r1, r0
	blt	.L1303
	add	r7, r8, fp, asl #8
	mov	r3, #255
	mov	r0, r10
	str	r3, [r7, #2056]
	bl	NandcWaitFlashReady
	ldr	r3, [sp, #24]
	cmp	r3, #0
	beq	.L1306
	mov	r3, #54
	str	r3, [r7, #2056]
	ldr	r3, .L1358
	mvn	r1, #0
	ldr	r0, [sp, #12]
	ldrb	r3, [r3, #702]	@ zero_extendqisi2
	str	r3, [r7, #2052]
	mov	r3, #0
	str	r3, [r7, #2048]
	mov	r3, #22
	str	r3, [r7, #2056]
	bl	FlashReadCmd
	b	.L1307
.L1306:
	ldr	r3, [sp, #4]
	cmp	r3, #0
	movne	r3, #190
	moveq	r3, #56
	str	r3, [r7, #2056]
.L1307:
	mov	r0, r10
	bl	NandcWaitFlashReady
	ldr	r3, [sp, #16]
	add	r3, r3, #1
	b	.L1356
.L1268:
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
	ldr	r3, .L1358+32
	str	r3, [sp, #20]
	b	.L1310
.L1359:
	.align	2
.L1358:
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
	bhi	.L1362
	b	HynixGetReadRetryDefault
.L1362:
	cmp	r3, #49
	bne	.L1363
	ldr	r2, .L1374
	ldr	r0, .L1374+4
	ldr	r1, .L1374+8
	strb	r3, [r2, #698]
	mov	r3, #4
	strb	r3, [r2, #699]
	mov	r3, #15
	strb	r3, [r2, #700]
	mov	r2, #64
	b	.L1372
.L1363:
	sub	r2, r3, #65
	cmp	r3, #33
	cmpne	r2, #1
	ldrls	r2, .L1374
	strlsb	r3, [r2, #698]
	movls	r3, #4
	bls	.L1373
.L1364:
	cmp	r3, #34
	cmpne	r3, #67
	bne	.L1365
	ldr	r2, .L1374
	strb	r3, [r2, #698]
	mov	r3, #5
.L1373:
	strb	r3, [r2, #699]
	mov	r3, #7
	ldr	r0, .L1374+4
	strb	r3, [r2, #700]
	mov	r2, #45
	ldr	r1, .L1374+12
	b	.L1372
.L1365:
	cmp	r3, #35
	cmpne	r3, #68
	bxne	lr
	ldr	r2, .L1374
	ldr	r0, .L1374+4
	ldr	r1, .L1374+16
	strb	r3, [r2, #698]
	mov	r3, #5
	strb	r3, [r2, #699]
	mov	r3, #17
	strb	r3, [r2, #700]
	mov	r2, #95
.L1372:
	b	ftl_memcpy
.L1375:
	.align	2
.L1374:
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
	ldr	r5, .L1416
	add	r0, r5, r0, asl #3
	ldr	r3, [r5, #1740]
	ldr	r4, [r0, #12]
	cmp	r3, #3
	bls	.L1407
	ldr	r3, [r4, #16]
	tst	r3, #4
	beq	.L1407
	mov	r0, r4
	bl	wait_for_nandc_xfer_completed
	ldr	r6, [r4, #16]
	ldr	r3, [r4, #8]
	ubfx	r6, r6, #1, #1
	cmp	r6, #0
	movne	r6, #0
	str	r3, [sp]
	beq	.L1389
.L1379:
	ldr	r2, [r4, #28]
	ldr	r3, [sp]
	ubfx	r2, r2, #16, #5
	ubfx	r3, r3, #22, #6
	cmp	r2, r3
	bge	.L1387
	ldr	r3, [r5, #1740]
	cmp	r3, #5
	bhi	.L1380
.L1383:
	add	r6, r6, #1
	bic	r3, r6, #-16777216
	cmp	r3, #0
	bne	.L1379
	ldr	r2, [r4, #28]
	mov	r1, r6
	ldr	r3, [sp]
	ubfx	r2, r2, #16, #5
	ldr	r0, .L1416+4
	ubfx	r3, r3, #22, #6
	bl	printk
	ldr	r0, .L1416+8
	mov	r1, r4
	mov	r2, #4
	mov	r3, #512
	bl	rknand_print_hex
	b	.L1379
.L1380:
	ldr	r3, [r4]
	str	r3, [sp, #4]
	ldr	r3, [sp, #4]
	tst	r3, #8192
	beq	.L1383
	ldr	r3, [sp, #4]
	tst	r3, #131072
	beq	.L1383
.L1387:
	ldr	r3, [r5, #1776]
	ldr	r4, .L1416
	cmp	r3, #0
	beq	.L1388
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
	b	.L1388
.L1391:
	ldr	r3, [r4, #8]
	add	r6, r6, #1
	str	r3, [sp]
	bic	r3, r6, #-16777216
	cmp	r3, #0
	bne	.L1389
	ldr	r2, [sp]
	mov	r1, r6
	ldr	r3, [r4, #28]
	ldr	r0, .L1416+12
	ubfx	r3, r3, #16, #5
	bl	printk
	ldr	r0, .L1416+8
	mov	r1, r4
	mov	r2, #4
	mov	r3, #512
	bl	rknand_print_hex
.L1389:
	ldr	r3, [sp]
	tst	r3, #1048576
	beq	.L1391
	ldr	r3, [r5, #1784]
	cmp	r3, #0
	beq	.L1392
	mov	r0, r4
	bl	NandcSendDumpDataStart
.L1392:
	ldr	r3, [r5, #1776]
	ldr	r6, .L1416
	cmp	r3, #0
	beq	.L1393
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
.L1393:
	ldr	r3, [r5, #1784]
	cmp	r3, #0
	beq	.L1388
	mov	r0, r4
	bl	NandcSendDumpDataDone
.L1388:
	mov	r3, #0
	str	r3, [r5, #1776]
	b	.L1376
.L1407:
	ldr	r3, [r4, #8]
	str	r3, [sp]
	ldr	r3, [sp]
	tst	r3, #1048576
	beq	.L1407
.L1376:
	add	sp, sp, #8
	@ sp needed
	ldmfd	sp!, {r4, r5, r6, pc}
.L1417:
	.align	2
.L1416:
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
	ldr	r3, .L1467
	mov	r8, r2
	.pad #100
	sub	sp, sp, #100
	ldr	r4, .L1467+4
	tst	fp, #63
	mov	r10, r0
	ldr	r2, [r3]
	mov	r7, r1
	ldr	r5, [sp, #136]
	str	r3, [sp, #16]
	str	r2, [sp, #92]
	add	r2, r4, r0, asl #3
	ldr	r9, [r2, #12]
	bne	.L1419
	cmp	r5, #0
	bne	.L1420
	add	r0, sp, #28
	mov	r1, #255
	mov	r2, #64
	bl	ftl_memset
	add	r5, sp, #28
.L1420:
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
	bne	.L1421
	ldr	r3, [r4, #1788]
	mov	r1, r8, lsr #1
	mov	r2, r7
	cmp	r3, #25
	mov	r3, r7
	movcc	r0, #64
	movcs	r0, #128
.L1423:
	cmp	r2, r1
	add	r5, r5, #4
	add	ip, r3, r0
	bcs	.L1465
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
	b	.L1423
.L1465:
	ldr	r1, [r4, #1788]
	mov	r2, #0
	ldr	r0, [r4, #1740]
	mov	r8, r8, lsr #2
	mov	r6, r2
.L1425:
	cmp	r2, r8
	bcs	.L1421
	cmp	r1, #0
	beq	.L1421
	add	r3, r2, #8
	ldr	r3, [r9, r3, asl #2]
	str	r3, [sp, #24]
	ldr	r3, [sp, #24]
	tst	r3, #4
	bne	.L1450
	ldr	r4, [sp, #24]
	ubfx	r4, r4, #15, #1
	cmp	r4, #0
	bne	.L1450
	cmp	r0, #5
	bls	.L1427
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
	b	.L1464
.L1427:
	cmp	r0, #3
	bls	.L1429
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
	bls	.L1430
	ldr	r3, [sp, #24]
	ldr	r4, [sp, #24]
	ubfx	r3, r3, #3, #5
	ubfx	r4, r4, #28, #1
.L1464:
	orr	r4, r3, r4, asl #5
	b	.L1429
.L1430:
	ldr	r5, [sp, #24]
	ldr	r4, [sp, #24]
	ubfx	r5, r5, #16, #5
	ubfx	r4, r4, #30, #1
	orr	r4, r5, r4, asl #5
.L1429:
	cmp	r6, r4
	movcc	r6, r4
	b	.L1426
.L1450:
	mvn	r6, #0
.L1426:
	add	r2, r2, #1
	b	.L1425
.L1421:
	mov	r3, #0
	str	r3, [r9, #16]
	b	.L1432
.L1419:
	cmp	r1, #1
	mov	r4, #0
	bne	.L1462
	mov	r6, r4
.L1433:
	cmp	r4, r8
	bcs	.L1466
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
	b	.L1433
.L1466:
	mov	r6, #0
	b	.L1432
.L1462:
	str	r4, [sp]
	mov	r1, r4
	str	r4, [sp, #4]
	mov	r2, #2
	mov	r3, r4
	mov	r6, r4
	bl	NandcXferStart
	str	fp, [sp, #12]
.L1438:
	cmp	r4, r8
	bcs	.L1432
	mov	r0, r10
	bl	NandcXferComp
	ldr	r3, [r9, #32]
	add	ip, r4, #2
	cmp	ip, r8
	str	r3, [sp, #24]
	bcs	.L1439
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
.L1439:
	ldr	r3, [sp, #24]
	tst	r3, #4
	mvnne	r6, #0
	bne	.L1440
	ldr	r2, [sp, #24]
	ldr	r3, [sp, #24]
	ubfx	r2, r2, #3, #5
	ubfx	r3, r3, #27, #1
	orr	r3, r2, r3, asl #5
	cmp	r6, r3
	movcc	r6, r3
.L1440:
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
	b	.L1438
.L1432:
	ldr	r3, .L1467+4
	clz	r7, r7
	mov	r7, r7, lsr #5
	ldr	r3, [r3, #1740]
	cmp	r3, #5
	movls	r7, #0
	cmp	r7, #0
	beq	.L1444
	ldr	r3, [r9]
	and	r2, r3, #139264
	cmp	r2, #139264
	orreq	r3, r3, #131072
	streq	r3, [r9]
	mvneq	r6, #0
.L1444:
	ldr	r3, [sp, #16]
	mov	r0, r6
	ldr	r2, [sp, #92]
	ldr	r3, [r3]
	cmp	r2, r3
	beq	.L1446
	bl	__stack_chk_fail
.L1446:
	add	sp, sp, #100
	@ sp needed
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L1468:
	.align	2
.L1467:
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
	ldr	r3, .L1472
	subs	r4, r0, #0
	mov	r6, r1
	mov	r7, r2
	ldrb	r5, [r3, #481]	@ zero_extendqisi2
	bne	.L1470
	ldr	r2, .L1472+4
	ldrb	r3, [r2, #1]	@ zero_extendqisi2
	ldr	r2, [r2, #4]
	mul	r2, r2, r3
	cmp	r1, r2
	movcc	r5, #4
.L1470:
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
.L1473:
	.align	2
.L1472:
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
	ldr	r4, .L1501
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
	beq	.L1476
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
	bne	.L1477
.L1486:
	mvn	r6, #0
	b	.L1478
.L1477:
	mov	r2, r6
	ldr	r0, .L1501+4
	ldr	r1, [sp]
	bl	printk
	cmp	r6, #9
	addls	r4, r4, r10, asl #3
	ldrls	r3, [r4, #12]
	ldr	r4, .L1501+8
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
	bcc	.L1478
.L1476:
	mov	r4, #0
	mvn	r8, #0
	mov	ip, r4
	mov	r5, r4
	str	r4, [sp, #4]
.L1484:
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
	bhi	.L1480
	cmp	r0, #2
	bhi	.L1490
	add	r5, r5, #1
	cmp	r5, #9
	bls	.L1490
	rsb	r4, r5, r9
	mov	r6, r0
	mov	r8, #0
	b	.L1482
.L1480:
	ldr	r3, [sp, #4]
	cmp	r3, r5
	bcs	.L1491
	cmp	r5, #7
	rsb	ip, r5, r4
	bhi	.L1492
	str	r5, [sp, #4]
	b	.L1491
.L1490:
	mov	r8, #0
	mov	r4, r9
	mov	r6, r0
	mov	r7, r8
	mov	fp, r8
	b	.L1481
.L1491:
	mov	r5, #0
.L1481:
	add	r9, r9, #2
	cmp	r9, #69
	bls	.L1484
.L1482:
	ldr	r3, [sp, #4]
	cmp	r3, r5
	movcs	r4, ip
	b	.L1483
.L1492:
	mov	r4, ip
.L1483:
	cmp	r4, #0
	beq	.L1485
	ldr	r0, .L1501+12
	mov	r1, r4
	bl	printk
	uxtb	r0, r4
	bl	NandcSetDdrPara
.L1485:
	cmn	r8, #1
	bne	.L1478
	ldr	r0, .L1501+16
	mov	r1, r10
	ldr	r2, [sp]
	bl	printk
	ldr	r3, [sp, #56]
	cmp	r3, #0
	beq	.L1486
	ldr	r3, [sp, #8]
	ubfx	r0, r3, #8, #8
	bl	NandcSetDdrPara
.L1478:
	mov	r0, r6
	add	sp, sp, #20
	@ sp needed
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L1502:
	.align	2
.L1501:
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
	bne	.L1504
	ldr	r9, .L1524
	ldrb	fp, [r9, #8]	@ zero_extendqisi2
	mov	r10, r9
	cmp	fp, #0
	bne	.L1505
.L1507:
	ldrb	r3, [r10, #1732]	@ zero_extendqisi2
	ldr	r9, .L1524
	cmp	r3, #0
	beq	.L1504
	b	.L1523
.L1505:
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
	beq	.L1507
	b	.L1504
.L1523:
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
	beq	.L1508
	ldrb	r3, [r9, #1792]	@ zero_extendqisi2
	cmp	r0, r3, lsr #1
	bls	.L1504
.L1508:
	ubfx	r0, r10, #8, #8
	bl	NandcSetDdrPara
.L1504:
	ldr	r9, .L1524+4
	ldr	ip, [r9, #156]
	adds	r3, ip, #0
	movne	r3, #1
	cmn	r4, #1
	movne	r3, #0
	cmp	r3, #0
	beq	.L1509
	mov	r1, r6
	mov	r2, r8
	mov	r3, r7
	mov	r0, r5
	blx	ip
	mov	r2, r5
	mov	r3, r6
	mov	r4, r0
	ldr	r0, .L1524+8
	mov	r1, r4
	bl	printk
	cmn	r4, #1
	bne	.L1509
	ldr	r3, .L1524
	ldrb	r3, [r3, #140]	@ zero_extendqisi2
	cmp	r3, #0
	beq	.L1509
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
.L1509:
	mov	r0, r4
	add	sp, sp, #12
	@ sp needed
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L1525:
	.align	2
.L1524:
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
	ldr	r5, .L1536
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
	bne	.L1527
	ldrb	r2, [r5, #1716]	@ zero_extendqisi2
	tst	r2, #1
	beq	.L1527
	mov	r0, #1
	bl	FlashSetInterfaceMode
	mov	r0, #1
	bl	NandcSetMode
	strb	r4, [r5, #1732]
	b	.L1528
.L1527:
	mov	r2, #1
	strb	r2, [r3, #1732]
.L1528:
	mov	r0, #0
	add	sp, sp, #8
	@ sp needed
	ldmfd	sp!, {r4, r5, r6, r7, r8, pc}
.L1537:
	.align	2
.L1536:
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
	ldr	r10, .L1555
	mov	r6, #0
	ldr	r7, .L1555+4
	mov	r4, r6
	ldr	r5, .L1555+8
	mov	r9, #4
	ldr	r3, [r10]
	mvn	r8, #0
	str	r6, [r5, #164]
	str	r3, [sp, #12]
	ldr	r3, .L1555+12
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
.L1539:
	add	fp, r4, #1
	mov	r10, #0
.L1541:
	add	r3, sp, #8
	ldrb	r0, [r3, r10]	@ zero_extendqisi2
	bl	FlashBchSel
	mov	r0, #0
	mov	r1, r4
	ldr	r2, [r5, #148]
	mov	r3, r0
	bl	FlashReadRawPage
	cmn	r0, #1
	bne	.L1540
	mov	r0, #0
	mov	r1, fp
	ldr	r2, [r6, #148]
	mov	r3, r0
	bl	FlashReadRawPage
	cmn	r0, #1
	bne	.L1540
	add	r10, r10, #1
	cmp	r10, #4
	beq	.L1542
	b	.L1541
.L1543:
	add	r0, fp, #12
	movw	r1, #2036
	bl	js_hash
	ldr	r3, [fp, #8]
	cmp	r3, r0
	mvnne	r8, #0
	bne	.L1542
	ldr	r8, .L1555+16
	add	r1, fp, #160
	mov	r2, #32
	mov	r0, r8
	bl	ftl_memcpy
	ldr	r1, [r6, #160]
	mov	r2, #32
	ldr	r0, .L1555+20
	add	r1, r1, #192
	bl	ftl_memcpy
	ldr	r1, [r6, #160]
	mov	r2, #852
	ldr	r0, .L1555+24
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
.L1542:
	ldr	r3, [sp]
	subs	r9, r9, #1
	add	r4, r4, r3
	bne	.L1539
	mov	r0, r9
.L1554:
	bl	flash_exit_slc_mode
	mov	r0, r8
	ldr	r3, [sp, #4]
	ldr	r2, [sp, #12]
	ldr	r3, [r3]
	cmp	r2, r3
	beq	.L1549
	bl	__stack_chk_fail
.L1540:
	ldr	fp, [r5, #160]
	ldr	r2, .L1555+28
	ldr	r3, [fp]
	cmp	r3, r2
	bne	.L1542
	cmp	r8, #0
	bne	.L1543
	mov	r0, r4
	ldr	r1, [r7, #4]
	bl	__aeabi_uidiv
	ldr	r3, .L1555+8
	add	r0, r0, #1
	str	r0, [r3, #168]
	mov	r0, r8
	b	.L1554
.L1549:
	add	sp, sp, #20
	@ sp needed
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L1556:
	.align	2
.L1555:
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
	ldr	r4, .L1586
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
	bls	.L1558
	ldrb	r5, [r4, #1732]	@ zero_extendqisi2
	cmp	r5, #0
	streq	r5, [sp, #4]
	beq	.L1559
	mov	r0, #0
	str	r2, [sp, #20]
	bl	NandcSetDdrMode
	mov	r3, #1
	ldr	r2, [sp, #20]
	str	r3, [sp, #4]
.L1559:
	ldr	r3, [sp, #12]
	mov	r1, #92
	add	r3, r6, r3
	str	r1, [r3, #2056]
	mov	r1, #197
	str	r1, [r3, #2056]
.L1558:
	mov	r8, #1
	mvn	r10, #0
	mov	r3, r2, asl #8
	str	r3, [sp, #20]
.L1560:
	ldr	r3, .L1586+4
	ldrb	r3, [r3, #173]	@ zero_extendqisi2
	add	r3, r3, #1
	cmp	r8, r3
	bcs	.L1585
	ldrb	r3, [r4, #696]	@ zero_extendqisi2
	mov	r0, r7
	uxtb	r1, r8
	sub	r3, r3, #67
	cmp	r3, #1
	bhi	.L1561
	bl	SandiskSetRRPara
	b	.L1562
.L1561:
	bl	ToshibaSetRRPara
.L1562:
	ldrb	r3, [r4, #696]	@ zero_extendqisi2
	cmp	r3, #34
	bne	.L1563
	ldr	r3, .L1586+4
	ldrb	r3, [r3, #173]	@ zero_extendqisi2
	sub	r3, r3, #3
	cmp	r8, r3
	ldreq	r3, [sp, #20]
	moveq	r2, #179
	addeq	r3, r6, r3
	streq	r2, [r3, #2056]
.L1563:
	ldr	r3, [sp, #12]
	mov	r2, #38
	add	r3, r6, r3
	str	r2, [r3, #2056]
	mov	r2, #93
	str	r2, [r3, #2056]
	ldr	r3, [sp, #4]
	cmp	r3, #0
	beq	.L1564
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
	b	.L1565
.L1564:
	mov	r0, r9
	ldr	r1, [sp, #16]
	ldr	r2, [sp, #8]
	mov	r3, fp
	bl	FlashReadRawPage
	mov	r5, r0
.L1565:
	cmn	r5, #1
	beq	.L1566
	ldrb	r2, [r4, #1792]	@ zero_extendqisi2
	cmn	r10, #1
	moveq	r10, r5
	add	r2, r2, r2, asl #1
	cmp	r5, r2, asr #2
	bcc	.L1568
	mov	fp, #0
	str	fp, [sp, #8]
.L1566:
	add	r8, r8, #1
	b	.L1560
.L1585:
	mov	r5, r10
.L1568:
	ldrb	r2, [r4, #696]	@ zero_extendqisi2
	mov	r0, r7
	mov	r1, #0
	sub	r2, r2, #67
	cmp	r2, #1
	bhi	.L1570
	bl	SandiskSetRRPara
	b	.L1571
.L1570:
	bl	ToshibaSetRRPara
.L1571:
	ldr	r3, [sp, #12]
	mov	r2, #255
	add	r6, r6, r3
	str	r2, [r6, #2056]
	ldrb	r2, [r4, #1792]	@ zero_extendqisi2
	add	r2, r2, r2, asl #1
	cmp	r5, r2, asr #2
	bcc	.L1572
	cmn	r5, #1
	movne	r5, #256
.L1572:
	mov	r0, r9
	bl	NandcWaitFlashReady
	ldr	r3, [sp, #4]
	cmp	r3, #0
	beq	.L1573
	mov	r0, #4
	bl	NandcSetDdrMode
.L1573:
	mov	r0, r5
	add	sp, sp, #28
	@ sp needed
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L1587:
	.align	2
.L1586:
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
	ldr	r5, .L1602
	mov	r10, r2
	mov	r8, r3
	mov	fp, r1
	bl	NandcWaitFlashReady
	add	r2, r5, r9, asl #3
	ldr	ip, .L1602+4
	mov	r7, #1
	ldrb	r3, [r2, #16]	@ zero_extendqisi2
	mvn	r4, #0
	ldr	r6, [r2, #12]
	add	r3, r3, #8
	add	r6, r6, r3, asl #8
.L1589:
	ldrb	r3, [ip, #173]	@ zero_extendqisi2
	add	r3, r3, #1
	cmp	r7, r3
	bcs	.L1592
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
	beq	.L1590
	ldrb	r3, [r5, #1792]	@ zero_extendqisi2
	cmn	r4, #1
	moveq	r4, r0
	add	r3, r3, r3, asl #1
	cmp	r0, r3, asr #2
	bcc	.L1595
	mov	r8, #0
	mov	r10, r8
.L1590:
	add	r7, r7, #1
	b	.L1589
.L1595:
	mov	r4, r0
.L1592:
	mov	r0, r6
	mov	r1, #0
	bl	SamsungSetRRPara
	ldrb	r3, [r5, #1792]	@ zero_extendqisi2
	add	r3, r3, r3, asl #1
	cmp	r4, r3, asr #2
	bcc	.L1594
	cmn	r4, #1
	movne	r4, #256
.L1594:
	mov	r0, r4
	add	sp, sp, #12
	@ sp needed
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L1603:
	.align	2
.L1602:
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
.L1606:
	stmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.save {r4, r5, r6, r7, r8, r9, r10, fp, lr}
	mov	r8, r3
	ldr	r3, .L1630
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
	ldrne	r2, .L1630+4
	ubfxeq	r5, r5, #2, #8
	smullne	r2, r3, r5, r2
	uxtbne	r5, r3
	ldr	r3, .L1630
	add	r3, r3, r0, asl #3
	str	r3, [sp, #24]
.L1616:
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
.L1607:
	ldr	r3, .L1630+8
	ldrb	r3, [r3, #173]	@ zero_extendqisi2
	cmp	r9, r3
	bcs	.L1610
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
	beq	.L1608
	cmn	r4, #1
	ldr	ip, [sp, #28]
	moveq	r4, r0
	cmp	r0, r5
	bcc	.L1618
	mov	r8, ip
	mov	fp, ip
.L1608:
	ldr	r9, [sp, #8]
	b	.L1607
.L1618:
	mov	r4, r0
	mov	r8, ip
	mov	fp, ip
.L1610:
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
	bcc	.L1612
	cmn	r4, #1
	movne	r4, #256
.L1612:
	cmn	r4, #1
	movne	r7, #0
	moveq	r7, #1
	cmp	r4, #256
	movne	r1, r7
	orreq	r1, r7, #1
	cmp	r1, #0
	beq	.L1613
	str	r4, [sp]
	mov	r1, r9
	ldr	r0, .L1630+12
	mov	r3, r9
	ldr	r2, [sp, #20]
	bl	printk
	cmp	r10, #0
	bne	.L1614
	ldr	r3, .L1630
	ldrb	r3, [r3, #140]	@ zero_extendqisi2
	cmp	r3, #0
	moveq	r7, #0
	andne	r7, r7, #1
	cmp	r7, #0
	beq	.L1623
	mov	r0, r6
	mov	r1, #3
	bl	micron_auto_read_calibration_config
	mov	r10, #1
	b	.L1616
.L1614:
	mov	r0, r6
	mov	r1, #0
	bl	micron_auto_read_calibration_config
	cmn	r4, #1
	movne	r4, #256
	b	.L1623
.L1613:
	cmp	r10, #0
	beq	.L1623
	mov	r0, r6
	mov	r4, #256
	bl	micron_auto_read_calibration_config
.L1623:
	mov	r0, r4
	add	sp, sp, #36
	@ sp needed
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L1631:
	.align	2
.L1630:
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
	ldr	r5, .L1650
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
.L1634:
	cmp	ip, r9
	bcs	.L1638
	add	r4, r4, #1
	mov	r0, r7
	ldrb	r1, [r5, #699]	@ zero_extendqisi2
	uxtb	r4, r4
	ldr	r2, .L1650+4
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
	beq	.L1636
	ldrb	r3, [r5, #1792]	@ zero_extendqisi2
	cmn	r6, #1
	moveq	r6, r0
	add	r3, r3, r3, asl #1
	cmp	r0, r3, asr #2
	bcc	.L1643
	mov	r8, #0
	mov	r10, r8
.L1636:
	add	ip, ip, #1
	b	.L1634
.L1643:
	mov	r6, r0
.L1638:
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
	bcc	.L1642
	cmn	r6, #1
	movne	r6, #256
.L1642:
	mov	r0, r6
	add	sp, sp, #12
	@ sp needed
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L1651:
	.align	2
.L1650:
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
	ldr	r3, .L1656
	subs	r4, r0, #0
	mov	r5, r1
	mov	r7, r2
	ldrb	r6, [r3, #481]	@ zero_extendqisi2
	bne	.L1653
	ldr	r2, .L1656+4
	ldrb	r3, [r2, #1]	@ zero_extendqisi2
	ldr	r1, [r2, #4]
	mul	r1, r1, r3
	cmp	r5, r1
	bcs	.L1653
	ldrb	r3, [r2]	@ zero_extendqisi2
	cmp	r3, #0
	subeq	r6, r6, #2
	movne	r6, #4
.L1653:
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
.L1657:
	.align	2
.L1656:
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
	ldr	r4, .L1668
	ldr	r5, .L1668+4
	ldr	r8, .L1668+8
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
	ldr	r1, .L1668+12
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
	ldr	r1, .L1668+16
	mov	r2, #8
	add	r0, r0, #80
	bl	ftl_memcpy
	ldr	r0, [r4, #160]
	add	r1, r5, #656
	mov	r2, #32
	add	r0, r0, #96
	bl	ftl_memcpy
	ldr	r0, [r4, #160]
	ldr	r1, .L1668+20
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
	ldr	r1, .L1668+24
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
.L1661:
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
	beq	.L1659
	ldr	fp, [r9, #160]
	ldr	r3, [fp]
	cmp	r3, r8
	bne	.L1659
	add	r0, fp, #12
	movw	r1, #2036
	str	r2, [sp, #4]
	bl	js_hash
	ldr	r3, [fp, #8]
	cmp	r3, r0
	ldr	r2, [sp, #4]
	bne	.L1659
	ldr	r3, [r10, #4]
	cmp	r6, #1
	str	r2, [r9, #168]
	mul	r7, r3, r7
	str	r7, [r9, #164]
	beq	.L1662
	mov	r6, #1
.L1659:
	cmp	r2, #4
	mov	r7, r2
	bne	.L1661
	b	.L1660
.L1662:
	mov	r6, #2
.L1660:
	mov	r0, #0
	bl	flash_exit_slc_mode
	clz	r0, r6
	mov	r0, r0, lsr #5
	rsb	r0, r0, #0
	add	sp, sp, #12
	@ sp needed
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L1669:
	.align	2
.L1668:
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
	ldr	r3, .L1691
	stmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.save {r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.pad #20
	sub	sp, sp, #20
	ldr	r2, [r3]
	mov	r9, r0
	ldr	r4, .L1691+4
	str	r3, [sp]
	str	r2, [sp, #12]
	ldr	r2, .L1691+8
	ldrb	r10, [r4, #1792]	@ zero_extendqisi2
	ldr	r0, [r2]	@ unaligned
	ldr	r2, [r4, #1728]
	cmp	r2, #0
	str	r0, [sp, #8]	@ unaligned
	beq	.L1671
	mov	r0, #0
	bl	flash_enter_slc_mode
.L1671:
	mov	r0, r9
	mov	r1, #0
	mov	r2, #2048
	mvn	r8, #0
	bl	ftl_memset
	mov	r5, #2
.L1672:
	ldrb	r3, [r4, #1]	@ zero_extendqisi2
	cmp	r5, r3
	bcs	.L1676
	ldr	fp, .L1691+12
	mov	r7, #0
.L1674:
	add	r3, sp, #8
	ldr	r6, .L1691+12
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
	bne	.L1673
	add	r7, r7, #1
	cmp	r7, #4
	bne	.L1674
	b	.L1675
.L1673:
	ldr	r3, [r6, #148]
	ldr	r2, .L1691+16
	ldr	r3, [r3]
	cmp	r3, r2
	bne	.L1675
	mov	r1, ip
	ldr	r0, .L1691+20
	bl	printk
	mov	r2, #2048
	mov	r0, r9
	ldr	r1, [r6, #148]
	bl	ftl_memcpy
	ldr	r3, [r6, #148]
	ldr	r2, .L1691+4
	ldr	r3, [r3, #512]
	strb	r3, [r2, #1]
	ldr	r3, [r6, #168]
	cmp	r3, r5
	bls	.L1680
	str	r5, [r6, #168]
	bl	FlashSavePhyInfo
	mov	r8, #0
.L1675:
	add	r5, r5, #1
	b	.L1672
.L1680:
	mov	r8, #0
.L1676:
	mov	r0, r10
	bl	FlashBchSel
	ldr	r3, [r4, #1728]
	cmp	r3, #0
	beq	.L1677
	mov	r0, #0
	bl	flash_exit_slc_mode
.L1677:
	ldr	r3, [sp]
	mov	r0, r8
	ldr	r2, [sp, #12]
	ldr	r3, [r3]
	cmp	r2, r3
	beq	.L1679
	bl	__stack_chk_fail
.L1679:
	add	sp, sp, #20
	@ sp needed
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L1692:
	.align	2
.L1691:
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
	ldr	r8, .L1788
	ldr	r4, .L1788+4
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
	ldrb	r3, [r4, #1556]	@ zero_extendqisi2
	sub	r3, r3, #1
	uxtb	r3, r3
	cmp	r3, #253
	bhi	.L1745
	ldr	r3, .L1788+4
	ldrb	r3, [r3, #1557]	@ zero_extendqisi2
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
	ldrb	r3, [r4, #1556]	@ zero_extendqisi2
	cmp	r3, #173
	beq	.L1700
	ldr	r3, .L1788+4
	ldr	r0, [r3, #1736]
	bl	NandcSetDdrMode
.L1700:
	mov	r2, #852
	ldr	r0, .L1788+16
	mov	r1, #0
	ldr	r5, .L1788+20
	bl	ftl_memset
	ldr	r6, [r4, #1744]
	ldr	r3, .L1788+24
	cmp	r6, r5
	ldrb	r0, [r4, #1557]	@ zero_extendqisi2
	str	r3, [r4, #692]
	mov	r3, #0
	strb	r3, [r4, #8]
	sub	ip, r0, #218
	ldreq	r3, .L1788+4
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
	bne	.L1702
	cmp	r0, #220
	bne	.L1703
	ldr	r3, .L1788+4
	ldrb	r3, [r3, #1559]	@ zero_extendqisi2
	cmp	r3, #149
	bne	.L1703
.L1702:
	ldrb	lr, [r4, #1556]	@ zero_extendqisi2
	mov	r3, #16
	strb	r3, [r4, #1]
	mov	r1, #1
	strb	r3, [r8, #174]
	cmp	lr, #152
	ldr	r3, .L1788+28
	strb	r1, [r4]
	ldr	r2, .L1788
	strb	lr, [r3, #3257]
	strb	r0, [r3, #3258]
	bne	.L1705
	ldr	r7, .L1788+4
	movw	lr, #1560
	ldrsb	lr, [r7, lr]
	cmp	lr, #0
	strltb	r1, [r2, #192]
	movge	r1, #24
	strgeb	r1, [r2, #174]
.L1705:
	movw	r2, #2049
	cmp	r6, r5
	cmpne	r6, r2
	moveq	r2, #16
	streqb	r2, [r8, #174]
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
	strb	r2, [r3, #3258]
	b	.L1710
.L1711:
	cmp	r0, #211
	ldreq	r2, .L1788+32
	moveq	r1, #4096
	streqh	r1, [r2, #14]	@ movhi
	moveq	r2, #2
	streqb	r2, [r3, #3269]
.L1710:
	ldr	r1, .L1788+36
	mov	r2, #32
	ldr	r0, .L1788+40
	bl	ftl_memcpy
	ldr	r0, .L1788+24
	mov	r2, #32
	add	r1, r0, #2784
	bl	ftl_memcpy
.L1703:
	ldrb	r3, [r4]	@ zero_extendqisi2
	ldr	r6, .L1788+4
	cmp	r3, #0
	bne	.L1712
	bl	FlashLoadPhyInfoInRam
	cmp	r0, #0
	bne	.L1714
	ldr	r3, [r6, #692]
	ldrh	r3, [r3, #16]
	mov	r3, r3, lsr #8
	tst	r3, #1
	and	r0, r3, #7
	strb	r0, [r6, #1716]
	bne	.L1714
	mov	r3, #1
	strb	r3, [r6, #1732]
	bl	FlashSetInterfaceMode
	ldrb	r0, [r6, #1716]	@ zero_extendqisi2
	bl	NandcSetMode
.L1714:
	ldr	r3, [r4, #692]
	ldr	r6, .L1788+4
	ldrb	r3, [r3, #26]	@ zero_extendqisi2
	strb	r3, [r4, #140]
	bl	FlashLoadPhyInfo
	cmp	r0, #0
	beq	.L1712
	ldr	r3, [r6, #1736]
	cmp	r3, #0
	beq	.L1717
	mov	r0, #1
	bl	FlashSetInterfaceMode
	mov	r0, #1
	b	.L1783
.L1717:
	ldrb	r0, [r6, #1716]	@ zero_extendqisi2
	bl	FlashSetInterfaceMode
	ldrb	r0, [r6, #1716]	@ zero_extendqisi2
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
	ldr	r3, [r4, #692]
	ldr	r0, .L1788+44
	ldrh	r1, [r3, #14]
	bl	printk
	bl	FlashLoadPhyInfoInRam
	cmn	r0, #1
	beq	.L1758
	bl	FlashDieInfoInit
	ldr	r3, [r6, #692]
	ldrb	r0, [r3, #19]	@ zero_extendqisi2
	bl	FlashGetReadRetryDefault
	ldr	r3, .L1788+48
	ldr	r2, [r6, #692]
	ldrh	r3, [r3, #-14]
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
	ldrb	r3, [r4, #1716]	@ zero_extendqisi2
	tst	r3, #6
	beq	.L1721
	bl	FlashSavePhyInfo
	mov	r0, #0
	ldr	r1, [r8, #164]
	bl	FlashDdrParaScan
.L1721:
	bl	FlashSavePhyInfo
.L1712:
	ldr	r2, [r4, #692]
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
	beq	.L1723
	ldrb	r0, [r3, #19]	@ zero_extendqisi2
	ldrb	r3, [r7, #699]	@ zero_extendqisi2
	strb	r0, [r7, #696]
	strb	r3, [r7, #697]
	ldrb	r3, [r7, #700]	@ zero_extendqisi2
	strb	r3, [r6, #173]
	sub	r3, r0, #1
	cmp	r3, #7
	bhi	.L1724
	ldr	r3, .L1788+52
	sub	r2, r0, #8
	clz	r2, r2
	str	r3, [r6, #156]
	sub	r3, r0, #5
	cmp	r0, #8
	cmpne	r3, #1
	mov	r2, r2, lsr #5
	ldr	r6, .L1788+56
	movls	r3, #1
	strls	r3, [r7, #1784]
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
	str	r3, [r6, #156]
	moveq	r3, #15
	bne	.L1787
	b	.L1785
.L1730:
	sub	r3, r0, #65
	cmp	r0, #33
	cmpne	r3, #1
	bhi	.L1732
	ldr	r3, .L1788+64
	str	r3, [r6, #156]
	mov	r3, #4
	strb	r3, [r7, #697]
.L1787:
	mov	r3, #7
.L1785:
	strb	r3, [r6, #173]
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
	str	r3, [r6, #156]
	movne	r3, #7
	moveq	r3, #17
	cmp	r1, #0
	strb	r3, [r6, #173]
	movne	r3, #4
	moveq	r3, #5
	strb	r3, [r4, #697]
	b	.L1723
.L1733:
	cmp	r0, #49
	ldreq	r3, .L1788+68
	streq	r3, [r6, #156]
.L1723:
	ldr	r2, [r4, #1744]
	ldr	r3, .L1788+4
	cmp	r2, r5
	bne	.L1737
	ldrb	r1, [r3, #140]	@ zero_extendqisi2
	cmp	r1, #0
	ldrne	r3, [r3, #692]
	movne	r1, #0
	strneb	r1, [r3, #18]
.L1737:
	ldrb	r1, [r4, #1556]	@ zero_extendqisi2
	ldr	r3, .L1788+4
	cmp	r1, #44
	bne	.L1738
	ldrb	r1, [r3, #1732]	@ zero_extendqisi2
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
	strb	r3, [r4, #1732]
	bl	FlashSetInterfaceMode
	mov	r0, #1
	bl	NandcSetMode
.L1738:
	ldrb	r3, [r4, #1716]	@ zero_extendqisi2
	tst	r3, #6
	beq	.L1740
	ldr	r2, .L1788+4
	ldrb	r2, [r2, #1732]	@ zero_extendqisi2
	cmp	r2, #0
	bne	.L1741
	tst	r3, #1
	bne	.L1740
.L1741:
	mov	r0, #0
	bl	flash_enter_slc_mode
	mov	r0, #0
	ldr	r1, [r8, #164]
	bl	FlashDdrParaScan
	mov	r0, #0
	bl	flash_exit_slc_mode
.L1740:
	ldr	r3, [r4, #692]
	mov	r8, #16
	ldr	r6, .L1788+4
	ldrb	r0, [r3, #20]	@ zero_extendqisi2
	bl	FlashBchSel
	ldr	r0, .L1788+72
	bl	FlashReadIdbDataRaw
	ldr	r0, .L1788+76
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
	ldr	r3, .L1807
	mov	r8, r0
	mov	r9, r1
	ldr	r2, [r3, #692]
	mov	r6, r3
	ldrb	r7, [r2, #19]	@ zero_extendqisi2
	ldrb	r2, [r3, #140]	@ zero_extendqisi2
	cmp	r2, #0
	beq	.L1791
	ldr	r3, [r3, #1728]
	cmp	r3, #0
	ldmnefd	sp!, {r3, r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L1791:
	sub	r3, r7, #5
	cmp	r7, #68
	cmpne	r3, #3
	bls	.L1792
	sub	r3, r7, #19
	tst	r3, #239
	ldmnefd	sp!, {r3, r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L1792:
	ldr	r5, .L1807+4
	movw	fp, #65535
	sub	r10, r5, #3136
.L1794:
	ldr	r3, [r6, #692]
	ldrh	r3, [r3, #10]
	cmp	r3, r4
	bls	.L1806
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
	ldr	r3, .L1807+4
	add	r1, r4, r9
	add	r4, r4, #1
	mov	r0, r8
	ldr	r2, [r3, #176]
	uxth	r4, r4
	mov	r3, r2
	bl	FlashProgPage
	b	.L1794
.L1806:
	ldmfd	sp!, {r3, r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L1808:
	.align	2
.L1807:
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
	ldr	r3, .L1861
	stmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.save {r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.pad #44
	sub	sp, sp, #44
	ldr	r9, .L1861+4
	mov	r4, r0
	str	r2, [sp, #16]
	mov	r8, #0
	ldr	r2, [r3]
	mov	r10, r9
	str	r1, [sp, #12]
	str	r3, [sp, #20]
	str	r2, [sp, #36]
	ldr	r2, .L1861+8
	ldrb	r2, [r2, #481]	@ zero_extendqisi2
	str	r2, [sp, #8]
.L1810:
	ldr	r3, [sp, #12]
	cmp	r8, r3
	beq	.L1860
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
	bcs	.L1812
	add	r3, r9, r3
	mov	r7, #0
	ldrb	r5, [r3, #1720]	@ zero_extendqisi2
	mov	r0, r5
	bl	NandcWaitFlashReady
	mov	r0, r5
	bl	NandcFlashCs
.L1813:
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
	ldr	r3, .L1861+12
	ldrb	r3, [r3, #192]	@ zero_extendqisi2
	cmp	r3, #0
	mov	r6, r0
	beq	.L1814
	mov	r0, r5
	bl	flash_read_ecc
	cmp	r0, #5
	movhi	r6, #256
.L1814:
	cmp	r7, #9
	cmnls	r6, #1
	moveq	r3, #1
	movne	r3, #0
	addeq	r7, r7, #1
	beq	.L1813
.L1815:
	cmp	r7, #0
	mov	r7, r3
	movne	r6, #256
.L1817:
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
	ldr	r3, .L1861+12
	ldrb	r2, [r3, #192]	@ zero_extendqisi2
	cmp	r2, #0
	mov	fp, r0
	beq	.L1820
	mov	r0, r5
	bl	flash_read_ecc
	cmp	r0, #5
	movhi	fp, #256
.L1820:
	cmp	r7, #9
	cmnls	fp, #1
	addeq	r7, r7, #1
	beq	.L1817
.L1821:
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
	bls	.L1823
	cmn	r3, #1
	movne	r3, #256
.L1823:
	cmp	r3, #256
	cmnne	r3, #1
	movne	r3, #0
	str	r3, [r4]
	ldr	r3, [r4, #12]
	cmp	r3, #0
	beq	.L1826
	ldr	r2, [r3, #12]
	cmn	r2, #1
	bne	.L1826
	ldr	r2, [r3, #8]
	cmn	r2, #1
	bne	.L1826
	ldr	r3, [r3]
	cmn	r3, #1
	strne	r2, [r4]
.L1826:
	ldr	r3, [r4]
	cmn	r3, #1
	bne	.L1812
	ldr	r1, [r4, #4]
	ldr	r0, .L1861+16
	ldrb	r2, [r10, #1792]	@ zero_extendqisi2
	bl	printk
	ldr	r1, [r4, #8]
	cmp	r1, #0
	beq	.L1828
	ldr	r0, .L1861+20
	mov	r2, #4
	mov	r3, #8
	bl	rknand_print_hex
.L1828:
	ldr	r1, [r4, #12]
	cmp	r1, #0
	beq	.L1812
	mov	r2, #4
	ldr	r0, .L1861+24
	mov	r3, r2
	bl	rknand_print_hex
.L1812:
	add	r8, r8, #1
	add	r4, r4, #36
	b	.L1810
.L1860:
	ldr	r3, [sp, #20]
	mov	r0, #0
	ldr	r2, [sp, #36]
	ldr	r3, [r3]
	cmp	r2, r3
	beq	.L1831
	bl	__stack_chk_fail
.L1831:
	add	sp, sp, #44
	@ sp needed
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L1862:
	.align	2
.L1861:
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
	ldr	r3, .L1939
	stmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.save {r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.pad #60
	sub	sp, sp, #60
	ldr	r3, [r3]
	ldr	r6, .L1939+4
	str	r1, [sp, #20]
	str	r3, [sp, #52]
	ldr	r3, .L1939+8
	ldrb	r8, [r6]	@ zero_extendqisi2
	str	r2, [sp, #24]
	ldrb	r3, [r3, #481]	@ zero_extendqisi2
	cmp	r8, #0
	str	r3, [sp, #16]
	ldrb	r3, [r6, #8]	@ zero_extendqisi2
	str	r3, [sp, #28]
	beq	.L1901
	bl	FlashReadSlc2KPages
	b	.L1865
.L1901:
	ldr	r10, .L1939+12
	mov	r9, r0
	mov	fp, r8
.L1864:
	ldr	r3, [sp, #20]
	cmp	fp, r3
	bcs	.L1937
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
	bcs	.L1867
	add	r3, r6, r3
	ldrb	r4, [r3, #1720]	@ zero_extendqisi2
	ldrb	r3, [r10, #193]	@ zero_extendqisi2
	cmp	r3, #0
	mov	r0, r4
	moveq	r5, #0
	bl	NandcWaitFlashReady
	ldr	r3, .L1939+4
	ldr	r3, [r3, #692]
	ldrb	r2, [r3, #19]	@ zero_extendqisi2
	sub	r3, r2, #1
	cmp	r3, #7
	bhi	.L1869
	sub	r2, r2, #7
	add	r1, r6, r4
	cmp	r2, #1
	ldr	r2, .L1939+4
	ldrb	r3, [r1, #710]	@ zero_extendqisi2
	add	r2, r2, r4
	ldrlsb	r3, [r1, #718]	@ zero_extendqisi2
	ldrb	r2, [r2, #1552]	@ zero_extendqisi2
	cmp	r2, r3
	beq	.L1869
	mov	r0, r4
	ldrb	r1, [r6, #699]	@ zero_extendqisi2
	ldr	r2, .L1939+16
	bl	HynixSetRRPara
.L1869:
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
	beq	.L1871
	ldr	r3, .L1939+4
	ldrb	r3, [r3, #140]	@ zero_extendqisi2
	cmp	r3, #0
	beq	.L1871
	bl	flash_enter_slc_mode
	b	.L1872
.L1871:
	bl	flash_exit_slc_mode
.L1872:
	ldr	r1, [sp, #48]
	cmn	r1, #1
	cmpeq	r4, #255
	moveq	r3, #0
	movne	r3, #1
	moveq	r5, r3
	beq	.L1874
	cmp	r5, #0
	beq	.L1875
	ldr	r2, [r6, #4]
	mov	r0, r4
	add	r2, r1, r2
	bl	FlashReadDpCmd
	b	.L1876
.L1875:
	mov	r0, r4
	bl	FlashReadCmd
.L1876:
	mov	r0, r4
	bl	NandcWaitFlashReady
	cmp	r5, #0
	beq	.L1874
	mov	r0, r4
	ldr	r1, [sp, #48]
	bl	FlashReadDpDataOutCmd
.L1874:
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
	bne	.L1872
.L1877:
	cmp	r5, #0
	beq	.L1878
	ldr	r3, .L1939+4
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
.L1878:
	mov	r0, r4
	str	ip, [sp, #32]
	bl	NandcFlashDeCs
	ldr	ip, [sp, #32]
	ldrb	r3, [sp, #28]	@ zero_extendqisi2
	cmn	ip, #1
	strb	r3, [r6, #8]
	bne	.L1885
	ldrb	r3, [r6, #1732]	@ zero_extendqisi2
	cmp	r3, #0
	bne	.L1880
.L1884:
	ldr	r5, [r10, #156]
	cmp	r5, #0
	bne	.L1881
	b	.L1938
.L1880:
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
	beq	.L1883
	ldrb	r3, [r6, #1792]	@ zero_extendqisi2
	cmp	r0, r3, lsr #1
	bls	.L1904
.L1883:
	ubfx	r0, r5, #8, #8
	str	ip, [sp, #32]
	bl	NandcSetDdrPara
	ldr	ip, [sp, #32]
	cmn	ip, #1
	beq	.L1884
	b	.L1904
.L1881:
	mov	r0, r4
	ldr	r1, [sp, #48]
	ldr	r2, [r7, #8]
	ldr	r3, [r7, #12]
	blx	r5
	cmn	r0, #1
	mov	ip, r0
	bne	.L1906
	ldr	r3, [r6, #692]
	ldrb	r3, [r3, #19]	@ zero_extendqisi2
	sub	r3, r3, #1
	cmp	r3, #7
	bhi	.L1887
	mov	r0, r4
	ldrb	r1, [r6, #699]	@ zero_extendqisi2
	ldr	r2, .L1939+16
	mov	r3, #0
	bl	HynixSetRRPara
.L1887:
	ldr	r1, [sp, #48]
	mov	r0, r4
	ldr	r2, [r7, #8]
	ldr	r3, [r7, #12]
	bl	FlashReadRawPage
	ldr	r1, [r7, #4]
	ldrb	r2, [r6, #1792]	@ zero_extendqisi2
	mov	ip, r0
	ldr	r0, .L1939+20
	mov	r3, ip
	str	ip, [sp, #32]
	bl	printk
	ldr	ip, [sp, #32]
	cmn	ip, #1
	bne	.L1906
	ldrb	r5, [r6, #140]	@ zero_extendqisi2
	cmp	r5, #0
	beq	.L1886
	ldr	r3, [sp, #12]
	mov	r0, r4
	cmp	r3, #0
	beq	.L1888
	bl	flash_enter_slc_mode
	b	.L1889
.L1888:
	bl	flash_exit_slc_mode
.L1889:
	ldr	ip, [r10, #156]
	mov	r0, r4
	ldr	r1, [sp, #48]
	ldr	r2, [r7, #8]
	ldr	r3, [r7, #12]
	blx	ip
	mov	ip, r0
	b	.L1906
.L1938:
	mov	r0, r4
	ldr	r1, [sp, #48]
	ldr	r2, [r7, #8]
	ldr	r3, [r7, #12]
	bl	FlashReadRawPage
	mov	ip, r0
	b	.L1886
.L1904:
	mov	r5, #0
.L1885:
	ldrb	r3, [r6, #1792]	@ zero_extendqisi2
	add	r3, r3, r3, asl #1
	cmp	ip, r3, asr #2
	bls	.L1886
	ldr	r3, [r10, #156]
	cmp	r3, #0
	moveq	ip, #256
	b	.L1886
.L1906:
	mov	r5, #0
.L1886:
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
	bne	.L1893
	ldr	r1, [r7, #4]
	ldr	r0, .L1939+24
	ldrb	r2, [r6, #1792]	@ zero_extendqisi2
	bl	printk
	ldr	r1, [r7, #12]
	cmp	r1, #0
	beq	.L1893
	mov	r2, #4
	ldr	r0, .L1939+28
	mov	r3, r2
	bl	rknand_print_hex
.L1893:
	cmp	r5, #0
	beq	.L1895
	ldrb	r3, [r6, #1792]	@ zero_extendqisi2
	add	r3, r3, r3, asl #1
	cmp	r8, r3, asr #2
	bls	.L1896
	ldr	r3, [r10, #156]
	cmp	r3, #0
	moveq	r8, #256
.L1896:
	ldr	r3, [sp, #8]
	cmp	r8, #256
	cmnne	r8, #1
	add	r3, r3, #36
	movne	r2, #0
	streq	r8, [r9, r3]
	strne	r2, [r9, r3]
.L1895:
	ldr	r3, [sp, #12]
	add	fp, fp, r5
	cmp	r3, #0
	beq	.L1867
	ldrb	r3, [r6, #140]	@ zero_extendqisi2
	cmp	r3, #0
	beq	.L1867
	mov	r0, r4
	bl	flash_exit_slc_mode
.L1867:
	add	fp, fp, #1
	b	.L1864
.L1937:
	mov	r0, #0
.L1865:
	ldr	r3, .L1939
	ldr	r2, [sp, #52]
	ldr	r3, [r3]
	cmp	r2, r3
	beq	.L1900
	bl	__stack_chk_fail
.L1900:
	add	sp, sp, #60
	@ sp needed
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L1940:
	.align	2
.L1939:
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
	ldr	r7, .L1955
	mov	r1, #0
	ldr	r8, .L1955+4
	mov	r2, #16
	ldr	r4, .L1955+8
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
.L1942:
	ldrb	r3, [r8, #1717]	@ zero_extendqisi2
	uxtb	r7, r5
	cmp	r3, r7
	bls	.L1954
	mul	ip, r6, r7
	sub	r4, r6, #1
	sub	r3, r6, #12
	uxth	r4, r4
.L1943:
	cmp	r4, r3
	ble	.L1945
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
	beq	.L1944
	ldr	r2, [r9, #180]
	ldrh	r1, [r2]
	movw	r2, #61664
	cmp	r1, r2
	bne	.L1944
	mov	r1, r7
	ldr	r0, .L1955+12
	mov	r2, r4
	add	r10, r10, #1
	bl	printk
	ldr	r3, .L1955+8
	uxth	r10, r10
	add	r7, r3, r7, asl #1
	strh	r4, [r7, #196]	@ movhi
	b	.L1945
.L1944:
	sub	r4, r4, #1
	uxth	r4, r4
	b	.L1943
.L1945:
	ldrb	r3, [r8, #1717]	@ zero_extendqisi2
	add	r5, r5, #1
	cmp	r3, r10
	moveq	fp, #0
	b	.L1942
.L1954:
	ldr	r3, [sp, #4]
	mov	r0, fp
	ldr	r2, [sp, #52]
	ldr	r3, [r3]
	cmp	r2, r3
	beq	.L1949
	bl	__stack_chk_fail
.L1949:
	add	sp, sp, #60
	@ sp needed
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L1956:
	.align	2
.L1955:
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
	ldr	r8, .L1989
	str	r3, [sp, #8]
	mov	r6, r1
	ldr	r3, .L1989+4
	mov	r4, r0
	mov	fp, r0
	mov	r7, #0
	ldr	r2, [r3]
	str	r3, [sp, #12]
	str	r2, [sp, #60]
	ldr	r2, .L1989+8
	ldrb	r10, [r2, #481]	@ zero_extendqisi2
.L1958:
	cmp	r7, r6
	beq	.L1988
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
	bcs	.L1960
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
.L1960:
	add	r7, r7, #1
	add	fp, fp, #36
	b	.L1958
.L1988:
	ldr	r3, [sp, #8]
	cmp	r3, #0
	bne	.L1976
.L1972:
	ldr	r3, [sp, #12]
	mov	r0, #0
	ldr	r2, [sp, #60]
	ldr	r3, [r3]
	cmp	r2, r3
	beq	.L1973
	bl	__stack_chk_fail
.L1976:
	ldr	r5, .L1989+12
	mov	r7, #0
	mov	r8, r5
.L1965:
	cmp	r7, r6
	beq	.L1972
	ldr	r3, [r4]
	cmn	r3, #1
	bne	.L1966
	ldr	r1, [r4, #4]
	ldr	r0, .L1989+16
	bl	printk
	b	.L1967
.L1966:
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
	bne	.L1968
	ldr	r0, .L1989+20
	ldr	r1, [r4, #4]
	bl	printk
	str	r10, [r4]
.L1968:
	ldr	r3, [r4, #12]
	cmp	r3, #0
	beq	.L1969
	ldr	r2, [r3]
	ldr	r3, [r8, #188]
	ldr	r3, [r3]
	cmp	r2, r3
	beq	.L1969
	ldr	r0, .L1989+24
	ldr	r1, [r4, #4]
	bl	printk
	mvn	r3, #0
	str	r3, [r4]
.L1969:
	ldr	r3, [r4, #8]
	cmp	r3, #0
	beq	.L1967
	ldr	r2, [r3]
	ldr	r3, [r8, #184]
	ldr	r3, [r3]
	cmp	r2, r3
	beq	.L1967
	ldr	r0, .L1989+28
	ldr	r1, [r4, #4]
	bl	printk
	mvn	r3, #0
	str	r3, [r4]
.L1967:
	add	r7, r7, #1
	add	r4, r4, #36
	b	.L1965
.L1973:
	add	sp, sp, #68
	@ sp needed
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L1990:
	.align	2
.L1989:
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
	ldr	r5, .L2003
	ldr	r6, .L2003+4
	ldr	r3, [r5, #-2064]
	sub	r9, r6, #76
	ldr	r8, [r5, #-2040]
	sub	r10, r6, #120
	mov	fp, r9
	str	r3, [r5, #220]
	str	r8, [r5, #224]
.L1992:
	ldrh	r3, [r10]
	cmp	r7, r3
	bcs	.L2002
	ldrh	r4, [r9]
	mvn	r3, #0
	movw	ip, #61664
	strh	r3, [r6, #2]!	@ movhi
	add	r4, r4, r3
	uxth	r4, r4
.L1993:
	ldrh	r3, [fp]
	sub	r2, r3, #16
	cmp	r4, r2
	ble	.L1995
	mla	r3, r3, r7, r4
	mov	r1, #1
	ldr	r0, .L2003+8
	mov	r2, r1
	str	ip, [sp, #4]
	mov	r3, r3, asl #10
	str	r3, [r5, #216]
	bl	FlashReadPages
	ldr	r3, [r5, #212]
	cmn	r3, #1
	ldr	ip, [sp, #4]
	beq	.L1994
	ldrh	r3, [r8]
	cmp	r3, ip
	streqh	r4, [r6]	@ movhi
	beq	.L1995
.L1994:
	sub	r4, r4, #1
	uxth	r4, r4
	b	.L1993
.L1995:
	add	r7, r7, #1
	b	.L1992
.L2002:
	mov	r0, #0
	add	sp, sp, #12
	@ sp needed
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L2004:
	.align	2
.L2003:
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
	ldr	r6, .L2018
	.pad #108
	sub	sp, sp, #108
	ldreq	r2, .L2018+4
	mov	r9, r1
	mov	r7, r0, asl #10
	mov	r1, #1
	ldr	r3, [r6]
	mov	r0, sp
	mov	r8, #0
	str	r8, [sp, #8]
	str	r3, [sp, #100]
	movweq	r3, #1874
	ldrne	r3, .L2018+8
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
	bne	.L2008
.L2009:
	sxth	r4, r8
	sxth	r3, r5
	cmp	r4, r3
	bgt	.L2008
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
	bne	.L2010
	ldr	r3, [sp, #40]
	cmn	r3, #1
	bne	.L2010
	ldr	r3, [sp]
	cmn	r3, #1
	subne	r5, r4, #1
	uxthne	r5, r5
	bne	.L2009
.L2010:
	add	r3, r4, #1
	uxth	r8, r3
	b	.L2009
.L2008:
	ldr	r2, [sp, #100]
	sxth	r0, r5
	ldr	r3, [r6]
	cmp	r2, r3
	beq	.L2013
	bl	__stack_chk_fail
.L2013:
	add	sp, sp, #108
	@ sp needed
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, pc}
.L2019:
	.align	2
.L2018:
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
	ldr	r4, .L2053
	ldr	r7, .L2053+4
	ldr	r8, .L2053+8
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
.L2021:
	ldrh	r3, [r8]
	sub	r3, r3, #48
	cmp	r5, r3
	ble	.L2024
	mov	r1, #1
	ldr	r0, .L2053+12
	mov	r2, r1
	mov	r3, r5, asl #10
	str	r3, [r4, #216]
	bl	FlashReadPages
	ldr	r3, [r4, #212]
	cmn	r3, #1
	bne	.L2022
	ldr	r3, [r9, #216]
	mov	r1, #1
	ldr	r0, .L2053+12
	mov	r2, r1
	add	r3, r3, #1
	str	r3, [r9, #216]
	bl	FlashReadPages
.L2022:
	ldr	r3, [r4, #212]
	cmn	r3, #1
	beq	.L2023
	ldrh	r2, [r6]
	movw	r3, #61649
	cmp	r2, r3
	bne	.L2023
	ldr	r2, [r6, #4]
	ldr	r3, .L2053+16
	str	r2, [r7, #1944]
	ldrh	r2, [r6, #8]
	strh	r5, [r3]	@ movhi
	strh	r2, [r3, #4]	@ movhi
	b	.L2024
.L2023:
	sub	r5, r5, #1
	uxth	r5, r5
	b	.L2021
.L2024:
	ldr	r7, .L2053+4
	movw	r2, #65535
	add	r5, r7, #1936
	ldrh	r3, [r5]
	cmp	r3, r2
	beq	.L2038
	ldrh	r3, [r5, #4]
	cmp	r3, r2
	beq	.L2028
	mov	r1, #1
	ldr	r0, .L2053+12
	mov	r2, r1
	mov	r3, r3, asl #10
	str	r3, [r4, #216]
	bl	FlashReadPages
	ldr	r3, [r4, #212]
	cmn	r3, #1
	beq	.L2028
	ldrh	r2, [r6]
	movw	r3, #61649
	cmp	r2, r3
	bne	.L2028
	ldr	r3, [r6, #4]
	ldr	r2, [r7, #1944]
	cmp	r3, r2
	strhi	r3, [r7, #1944]
	ldrhih	r2, [r5, #4]
	ldrhih	r3, [r6, #8]
	strhih	r2, [r5]	@ movhi
	strhih	r3, [r5, #4]	@ movhi
.L2028:
	ldrh	r0, [r5]
	mov	r1, #1
	bl	FtlGetLastWrittenPage
	movw	r8, #61649
	uxth	r7, r0
	add	r0, r0, #1
	strh	r0, [r5, #2]	@ movhi
.L2030:
	sxth	r3, r7
	cmp	r3, #0
	blt	.L2035
	ldrh	r2, [r5]
	mov	r1, #1
	ldr	r0, .L2053+12
	orr	r3, r3, r2, asl #10
	str	r3, [r4, #216]
	ldr	r3, [r4, #-2064]
	mov	r2, r1
	str	r3, [r4, #220]
	bl	FlashReadPages
	ldr	r3, [r4, #212]
	cmn	r3, #1
	beq	.L2031
	ldrh	r3, [r6]
	cmp	r3, r8
	bne	.L2031
.L2035:
	ldrh	r2, [r6, #10]
	ldrh	r0, [r6, #12]
	ldr	r3, .L2053+4
	strh	r2, [r5, #6]	@ movhi
	movw	r2, #65535
	cmp	r0, r2
	bne	.L2032
	b	.L2033
.L2031:
	sub	r7, r7, #1
	uxth	r7, r7
	b	.L2030
.L2032:
	ldr	r2, [r3, #1800]
	cmp	r0, r2
	beq	.L2033
	movw	r1, #1814
	ldrh	r3, [r3, r1]
	mov	r3, r3, lsr #2
	cmp	r0, r3
	cmpcc	r2, r3
	bcs	.L2033
	bl	FtlSysBlkNumInit
.L2033:
	ldr	r6, .L2053+20
	mov	r5, #0
	ldr	r8, .L2053+24
	sub	r7, r6, #134
.L2036:
	ldrh	r3, [r7]
	cmp	r5, r3
	bcs	.L2052
	ldrh	r2, [r8]
	ldr	r1, [r4, #220]
	ldr	r0, [r6, #4]!
	mov	r2, r2, asl #2
	mla	r1, r5, r2, r1
	bl	ftl_memcpy
	add	r5, r5, #1
	b	.L2036
.L2052:
	mov	r0, #0
	ldmfd	sp!, {r3, r4, r5, r6, r7, r8, r9, pc}
.L2038:
	mvn	r0, #0
	ldmfd	sp!, {r3, r4, r5, r6, r7, r8, r9, pc}
.L2054:
	.align	2
.L2053:
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
	ldr	r7, .L2140
	mov	r1, r5
	ldr	r4, .L2140+4
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
	ldr	r0, .L2140+8
	mov	r1, #255
	mov	r2, #16
	bl	ftl_memset
	movw	r3, #1812
	ldrh	r3, [r7, r3]
	str	r7, [sp, #20]
	mov	r7, r4
	str	r3, [sp, #8]
.L2056:
	ldr	r3, .L2140+12
	ldr	r2, [sp, #8]
	ldr	r0, .L2140
	ldrh	r3, [r3]
	cmp	r3, r2
	bls	.L2097
	ldr	r1, .L2140+16
	mov	r6, #36
	ldr	r3, .L2140+20
	mov	r5, #0
	ldr	ip, [r4, #-2076]
	sub	r9, r1, #52
	ldr	r2, [r4, #-3212]
	ldrh	r3, [r3]
	ldr	fp, [r4, #-3208]
	ldrh	r10, [r1]
	str	r5, [sp, #4]
.L2098:
	uxth	r1, r5
	cmp	r1, r3
	bcs	.L2135
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
	bne	.L2057
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
.L2057:
	add	r5, r5, #1
	b	.L2098
.L2135:
	ldr	r3, [sp, #4]
	cmp	r3, #0
	beq	.L2060
	mov	r1, r3
	ldr	r0, [r4, #-2076]
	mov	r2, #1
	ldr	r9, .L2140
	bl	FlashReadPages
	mov	r3, #0
	str	r3, [sp, #12]
.L2061:
	ldrh	r3, [sp, #12]
	ldr	r2, [sp, #4]
	cmp	r3, r2
	bcs	.L2060
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
	bne	.L2063
	mov	fp, #16
	movw	r3, #65535
.L2062:
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
	beq	.L2063
.L2064:
	ldr	r2, [r7, #-2076]
	ldr	r2, [r2, r10]
	cmn	r2, #1
	bne	.L2063
	sub	fp, fp, #1
	uxth	fp, fp
	cmp	fp, #0
	bne	.L2062
.L2063:
	ldr	r3, [r7, #-2076]
	ldr	r3, [r3, r10]
	cmn	r3, #1
	beq	.L2066
	ldr	r2, [r7, #-3848]
	ldr	r3, [r6, #4]
	cmn	r2, #1
	beq	.L2067
	cmp	r2, r3
	bhi	.L2068
.L2067:
	cmn	r3, #1
	addne	r2, r3, #1
	strne	r2, [r8, #-3848]
.L2068:
	ldrh	r2, [r6]
	movw	r1, #61604
	cmp	r2, r1
	beq	.L2070
	bhi	.L2071
	movw	r3, #61574
	cmp	r2, r3
	bne	.L2069
	ldr	r2, .L2140+24
	ldr	lr, [r8, #-2000]
	ldrh	ip, [r2]
	ldrh	r1, [r2, #26]
	sub	r0, ip, #1
	rsb	r2, r1, r0
	str	r2, [sp, #16]
	uxth	r3, r0
	b	.L2084
.L2071:
	movw	r3, #61634
	cmp	r2, r3
	beq	.L2073
	movw	r3, #65535
	cmp	r2, r3
	moveq	r0, r5
	beq	.L2134
	b	.L2069
.L2073:
	ldr	r3, [sp, #20]
	ldr	fp, .L2140+28
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
.L2075:
	ldr	r1, [sp, #24]
	sxth	r0, r3
	cmp	r0, r1
	ble	.L2136
	mov	r10, r0, asl #2
	ldr	r1, [r6, #4]
	str	r10, [sp, #16]
	ldr	r10, [lr, r0, asl #2]
	cmp	r1, r10
	bls	.L2076
	ldr	r1, [lr]
	cmp	r1, #0
	bne	.L2077
	cmp	r2, ip
	addne	r2, r2, #1
	strneh	r2, [fp]	@ movhi
.L2077:
	uxth	lr, r3
	mov	r1, #0
.L2078:
	uxth	r2, r1
	cmp	r2, lr
	bcs	.L2137
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
	b	.L2078
.L2137:
	ldr	r2, [r4, #-1984]
	mov	r0, r0, asl #1
	ldr	r1, [r6, #4]
	ldr	ip, [sp, #16]
	str	r1, [r2, ip]
	ldr	r2, [r4, #-2012]
	strh	r5, [r2, r0]	@ movhi
	sxth	r0, r3
	cmp	r0, #0
	bge	.L2080
	b	.L2069
.L2076:
	sub	r3, r3, #1
	uxth	r3, r3
	b	.L2075
.L2136:
	cmp	r0, #0
	bge	.L2111
	b	.L2069
.L2080:
	ldr	r2, .L2140+28
	ldr	r1, [r9, #1900]
	ldrh	r2, [r2]
	rsb	r1, r2, r1
	sub	r1, r1, #1
	sxth	r1, r1
	cmp	r0, r1
	bgt	.L2069
.L2111:
	add	r2, r2, #1
	ldr	r1, [r6, #4]
	strh	r2, [fp]	@ movhi
	sxth	r3, r3
	ldr	r2, [r4, #-1984]
	str	r1, [r2, r3, asl #2]
	mov	r3, r3, asl #1
	ldr	r2, [r4, #-2012]
	b	.L2132
.L2090:
	ldr	r10, [lr, r2, asl #2]
	mov	fp, r2, asl #2
	ldr	r0, [r6, #4]
	cmp	r0, r10
	bhi	.L2138
	sub	r3, r3, #1
	uxth	r3, r3
.L2084:
	ldr	r0, [sp, #16]
	sxth	r2, r3
	cmp	r2, r0
	bgt	.L2090
	b	.L2089
.L2138:
	ldr	r0, [lr]
	cmp	r0, #0
	bne	.L2086
	cmp	r1, ip
	addne	r1, r1, #1
	ldrne	r0, .L2140+32
	strneh	r1, [r0]	@ movhi
.L2086:
	uxth	lr, r3
	mov	r0, #0
.L2087:
	uxth	r1, r0
	cmp	r1, lr
	bcs	.L2139
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
	b	.L2087
.L2139:
	ldr	r1, [r8, #-2000]
	mov	r2, r2, asl #1
	ldr	r0, [r6, #4]
	str	r0, [r1, fp]
	ldr	r1, .L2140
	ldr	r1, [r1, #1920]
	strh	r5, [r1, r2]	@ movhi
.L2089:
	sxth	r3, r3
	cmp	r3, #0
	blt	.L2069
	ldr	r0, .L2140+32
	ldrh	r2, [r0, #-26]
	ldrh	r1, [r0]
	sub	r2, r2, #1
	rsb	r2, r1, r2
	sxth	r2, r2
	cmp	r3, r2
	bgt	.L2069
	add	r1, r1, #1
	ldr	r2, [r8, #-2000]
	strh	r1, [r0]	@ movhi
	ldr	r1, [r6, #4]
	str	r1, [r2, r3, asl #2]
	mov	r3, r3, asl #1
	ldr	r2, [r9, #1920]
.L2132:
	strh	r5, [r2, r3]	@ movhi
	b	.L2069
.L2070:
	ldr	r2, .L2140+8
	movw	r1, #65535
	ldrh	r0, [r2]
	cmp	r0, r1
	streqh	r5, [r2]	@ movhi
	beq	.L2133
	ldrh	r0, [r2, #4]
	cmp	r0, r1
	beq	.L2092
	mov	r1, #1
	bl	FtlFreeSysBlkQueueIn
.L2092:
	ldr	r3, [r6, #4]
	ldr	r2, [r8, #-3808]
	cmp	r2, r3
	ldr	r3, .L2140+8
	strcsh	r5, [r3, #4]	@ movhi
	bcs	.L2069
	ldrh	r2, [r3]
	strh	r5, [r3]	@ movhi
	strh	r2, [r3, #4]	@ movhi
	ldr	r3, [r6, #4]
.L2133:
	str	r3, [r7, #-3808]
	b	.L2069
.L2066:
	ldrb	r1, [r9, #140]	@ zero_extendqisi2
	mov	r0, r5
	cmp	r1, #0
	beq	.L2094
.L2134:
	mov	r1, #0
.L2094:
	bl	FtlFreeSysBlkQueueIn
.L2069:
	ldr	r3, [sp, #12]
	add	r3, r3, #1
	str	r3, [sp, #12]
	b	.L2061
.L2060:
	ldr	r3, [sp, #8]
	add	r5, r3, #1
	uxth	r3, r5
	str	r3, [sp, #8]
	b	.L2056
.L2097:
	ldr	r3, .L2140+4
	ldr	ip, [r3, #-2012]
	ldrh	r2, [ip]
	cmp	r2, #0
	beq	.L2099
.L2102:
	ldr	r0, [r0, #1920]
	ldr	r3, .L2140
	ldrh	r2, [r0]
	cmp	r2, #0
	beq	.L2100
	b	.L2123
.L2099:
	sub	r3, r3, #1920
	ldrh	r3, [r3, #-8]
	cmp	r3, #0
	ldrne	lr, [r0, #1900]
	beq	.L2102
.L2103:
	uxth	r3, r2
	sxth	r1, r3
	cmp	r1, lr
	bcs	.L2102
	mov	r5, r1, asl #1
	add	r2, r2, #1
	ldrh	r5, [ip, r5]
	cmp	r5, #0
	beq	.L2103
	mov	r6, #0
.L2104:
	ldr	ip, [r0, #1900]
	sxth	r2, r3
	cmp	r2, ip
	bcs	.L2102
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
	b	.L2104
.L2100:
	movw	r1, #1918
	ldrh	r1, [r3, r1]
	cmp	r1, #0
	movwne	r1, #1892
	ldrneh	ip, [r3, r1]
	beq	.L2123
.L2107:
	uxth	r3, r2
	sxth	r1, r3
	cmp	r1, ip
	bge	.L2123
	mov	lr, r1, asl #1
	add	r2, r2, #1
	ldrh	lr, [r0, lr]
	cmp	lr, #0
	beq	.L2107
	ldr	r0, .L2140
	mov	r7, #0
	ldr	r6, .L2140+24
.L2108:
	ldrh	ip, [r6]
	sxth	r2, r3
	cmp	r2, ip
	bge	.L2123
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
	b	.L2108
.L2123:
	mov	r0, #0
	add	sp, sp, #36
	@ sp needed
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L2141:
	.align	2
.L2140:
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
	stmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.save {r4, r5, r6, r7, r8, r9, r10, fp, lr}
	mov	r1, #0
	ldr	r4, .L2171
	.pad #36
	sub	sp, sp, #36
	ldr	r6, .L2171+4
	sub	r7, r4, #3808
	ldr	r3, [r4, #-2064]
	ldr	r0, [r4, #-4060]
	str	r3, [r4, #220]
	ldr	r3, [r4, #-2040]
	str	r3, [r4, #224]
	movw	r3, #1812
	ldrh	r2, [r6, r3]
	mov	r2, r2, asl #1
	bl	ftl_memset
	ldrh	r0, [r7, #-8]
	movw	r3, #65535
	cmp	r0, r3
	bne	.L2143
.L2154:
	mvn	r0, #0
	b	.L2144
.L2143:
	mov	r1, #1
	bl	FtlGetLastWrittenPage
	ldr	r3, .L2171+8
	ldrh	r9, [r7, #-8]
	mov	r7, r4
	uxth	r5, r0
	add	r0, r0, #1
	strh	r0, [r3, #2]	@ movhi
.L2145:
	sxth	r3, r5
	cmp	r3, #0
	blt	.L2153
	sxth	fp, r9
	mov	r1, #1
	ldr	r0, .L2171+12
	mov	r2, r1
	orr	r3, r3, fp, asl #10
	str	r3, [r4, #216]
	ldr	r3, [r4, #-2064]
	str	r3, [r4, #220]
	bl	FlashReadPages
	ldrb	r3, [r6]	@ zero_extendqisi2
	cmp	r3, #0
	beq	.L2146
	ldr	r8, [r7, #224]
	ldr	r3, [r8, #12]
	cmp	r3, #0
	beq	.L2146
	ldr	r2, [r7, #220]
	ldr	r10, .L2171+16
	str	r3, [sp, #28]
	mov	r0, r2
	str	r2, [sp, #24]
	ldrh	r1, [r10]
	bl	js_hash
	ldr	r3, [sp, #28]
	cmp	r3, r0
	beq	.L2146
	cmp	r5, #0
	ldr	r2, [sp, #24]
	bne	.L2147
	ldr	ip, .L2171+8
	ldrh	r1, [ip, #4]
	cmp	fp, r1
	beq	.L2147
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
	ldr	r0, .L2171+20
	str	r3, [sp, #16]
	ldr	r2, [r7, #212]
	ldr	r3, [r7, #216]
	bl	printk
	ldrh	r5, [r10, #-8]
	ldr	ip, [sp, #24]
	ldrh	r9, [ip, #4]
	b	.L2149
.L2147:
	mvn	r3, #0
	str	r3, [r4, #212]
.L2146:
	ldr	r3, [r4, #212]
	cmn	r3, #1
	beq	.L2149
	ldr	r3, [r7, #-2064]
	ldr	r2, .L2171+24
	ldr	r3, [r3]
	cmp	r3, r2
	bne	.L2149
	ldr	r3, [r7, #-2040]
	ldrh	r2, [r3]
	movw	r3, #61604
	cmp	r2, r3
	bne	.L2149
.L2153:
	ldr	r1, [r4, #220]
	mov	r2, #48
	movw	r8, #1812
	ldr	r0, .L2171+28
	bl	ftl_memcpy
	ldrh	r2, [r6, r8]
	ldr	r1, [r4, #220]
	ldr	r0, [r4, #-4060]
	add	r1, r1, #48
	mov	r2, r2, asl #1
	bl	ftl_memcpy
	ldrh	r2, [r6, r8]
	ldr	r1, [r4, #220]
	add	r3, r2, #24
	ldr	r0, [r4, #-3884]
	ubfx	r3, r3, #1, #30
	mov	r2, r2, lsr #3
	add	r2, r2, #4
	ldr	r7, .L2171+4
	add	r1, r1, r3, asl #2
	ldr	r5, .L2171
	bl	ftl_memcpy
	movw	r3, #1916
	ldrh	r3, [r6, r3]
	cmp	r3, #0
	beq	.L2151
	ldrh	r2, [r7, r8]
	ldr	r1, [r5, #220]
	ldr	r0, [r5, #-1988]
	mov	r3, r2, lsr #3
	add	r3, r3, r2, asl #1
	movw	r2, #1908
	ldrh	r2, [r7, r2]
	add	r3, r3, #52
	ubfx	r3, r3, #2, #14
	mov	r2, r2, asl #2
	add	r1, r1, r3, asl #2
	bl	ftl_memcpy
	b	.L2151
.L2149:
	sub	r5, r5, #1
	uxth	r5, r5
	b	.L2145
.L2151:
	ldr	r2, [r6, #4068]
	ldr	r3, .L2171+24
	ldr	r5, .L2171+4
	cmp	r2, r3
	bne	.L2154
	ldr	r7, .L2171+28
	movw	r2, #1826
	ldr	r6, .L2171
	ldrb	r1, [r5, #4078]	@ zero_extendqisi2
	ldrh	r2, [r5, r2]
	sub	r8, r6, #3808
	ldrh	r3, [r7, #8]
	cmp	r1, r2
	strh	r3, [r8, #-2]	@ movhi
	bne	.L2154
	add	r2, r5, #1872
	movw	r1, #1878
	ldrh	r1, [r5, r1]
	ldrh	r2, [r2]
	str	r3, [r6, #248]
	mul	r2, r3, r2
	str	r2, [r5, #1932]
	mul	r2, r1, r2
	str	r2, [r5, #1912]
	add	r2, r5, #1936
	ldrh	r0, [r2, #6]
	ldr	r2, [r5, #1816]
	rsb	r0, r0, r2
	rsb	r0, r3, r0
	movw	r3, #1804
	ldrh	r1, [r5, r3]
	add	r5, r5, #4096
	bl	__aeabi_uidiv
	sub	r3, r6, #4032
	sub	r2, r6, #3984
	sub	ip, r6, #3792
	add	lr, r2, #40
	mov	r9, r2
	strh	r0, [r8, #-12]	@ movhi
	ldrh	r0, [r7, #14]
	strh	r0, [r3, #-8]	@ movhi
	ldrh	r3, [r7, #16]
	mov	r1, r3, lsr #6
	strh	r1, [r8, #-230]	@ movhi
	ldrh	r1, [r7, #18]
	and	r3, r3, #63
	strb	r3, [r6, #-4034]
	ldrb	r3, [r5, #-17]	@ zero_extendqisi2
	strh	r1, [r2, #-8]	@ movhi
	ldrh	r1, [r7, #20]
	strb	r3, [r6, #-4032]
	mvn	r3, #0
	strh	r3, [ip, #-8]	@ movhi
	mov	r3, #0
	strh	r3, [r8, #10]	@ movhi
	mov	r8, r1, lsr #6
	and	r1, r1, #63
	strb	r1, [r6, #-3986]
	ldrb	r1, [r5, #-16]	@ zero_extendqisi2
	strh	r8, [r2, #-6]	@ movhi
	sub	r8, r6, #3936
	strb	r3, [r6, #-3794]
	strb	r1, [r6, #-3984]
	ldrh	r1, [r7, #22]
	strb	r3, [r6, #-3792]
	str	r3, [r6, #-3860]
	strh	r1, [r8, #-8]	@ movhi
	ldrh	r1, [r7, #24]
	mov	r7, r1, lsr #6
	and	r1, r1, #63
	strb	r1, [r6, #-3938]
	ldrb	r1, [r5, #-15]	@ zero_extendqisi2
	strh	r7, [r2, #42]	@ movhi
	mov	r7, ip
	strb	r1, [r6, #-3936]
	str	r3, [r6, #-3872]
	ldr	r1, [r5, #4]
	str	r3, [r6, #-3880]
	str	r3, [r6, #-3864]
	str	r3, [r6, #-3836]
	str	r3, [r6, #-3828]
	str	r3, [r6, #-3868]
	ldr	r3, [r5, #12]
	str	r1, [r6, #-3840]
	ldr	r1, [r6, #-3848]
	ldr	r2, [r4, #-3844]
	cmp	r3, r1
	strhi	r3, [r6, #-3848]
	ldr	r3, [r5, #8]
	cmp	r3, r2
	ldrhi	r2, .L2171
	strhi	r3, [r2, #-3844]
	movw	r3, #65535
	cmp	r0, r3
	beq	.L2157
	ldr	r0, .L2171+32
	bl	make_superblock
.L2157:
	ldrh	r2, [r9, #-8]
	movw	r3, #65535
	cmp	r2, r3
	beq	.L2158
	ldr	r0, .L2171+36
	bl	make_superblock
.L2158:
	ldrh	r2, [r8, #-8]
	movw	r3, #65535
	cmp	r2, r3
	beq	.L2159
	ldr	r0, .L2171+40
	bl	make_superblock
.L2159:
	ldrh	r2, [r7, #-8]
	movw	r3, #65535
	cmp	r2, r3
	beq	.L2160
	ldr	r0, .L2171+44
	bl	make_superblock
.L2160:
	mov	r0, #0
.L2144:
	add	sp, sp, #36
	@ sp needed
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L2172:
	.align	2
.L2171:
	.word	.LANCHOR2
	.word	.LANCHOR0
	.word	.LANCHOR2-3816
	.word	.LANCHOR2+212
	.word	.LANCHOR0+1882
	.word	.LC107
	.word	1179929683
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
	ldr	r3, .L2188
	ubfx	r0, r0, #10, #16
	stmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.save {r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.pad #108
	sub	sp, sp, #108
	ldr	r3, [r3]
	mov	r4, r1
	ldr	r8, .L2188+4
	ldr	r7, .L2188+8
	str	r3, [sp, #100]
	bl	P2V_block_in_plane
	add	r5, r8, #1872
	ldr	r1, .L2188+12
	ldrh	r9, [r5]
	mov	r6, r0
	ldr	r0, .L2188+16
	bl	printk
	ldr	r2, [r7, #-4060]
	mov	r3, r6, asl #1
	mov	r1, r6
	ldr	r0, .L2188+20
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
	beq	.L2174
	mov	r0, r6
	bl	ftl_get_blk_mode
	cmp	r0, #1
	mov	r4, r0
	movweq	r3, #1874
	ldreqh	r9, [r8, r3]
.L2174:
	ldr	r0, .L2188+24
	mov	r1, r4
	mov	r2, r9
	ldrh	r3, [r5]
	bl	printk
	ldr	r8, .L2188+8
	mov	r6, #0
.L2175:
	ldr	r3, .L2188+28
	add	r0, sp, #66
	movw	lr, #65535
	mov	r10, #36
	ldrh	r3, [r3]
	mov	r7, r3
	ldr	r3, [r8, #-2076]
	str	r3, [sp, #28]
	ldr	r3, [r8, #-3212]
	str	r3, [sp, #32]
	ldr	r3, .L2188+32
	ldrh	r2, [r3]
	ldrh	ip, [r3, #2]
	str	r2, [sp, #36]
	ldr	r2, [r8, #-3208]
	str	r2, [sp, #40]
	mov	r2, #0
	mov	r5, r2
.L2176:
	uxth	r3, r2
	cmp	r3, r7
	bcs	.L2186
	ldrh	r3, [r0, #2]!
	cmp	r3, lr
	beq	.L2177
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
.L2177:
	add	r2, r2, #1
	b	.L2176
.L2186:
	ldr	r0, [r8, #-2076]
	mov	r1, r5
	mov	r2, r4
	mov	r10, #0
	bl	FlashReadPages
	mov	fp, #36
.L2179:
	uxth	r3, r10
	cmp	r3, r5
	bcs	.L2187
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
	ldr	r0, .L2188+36
	str	r3, [sp, #20]
	ldr	r2, [lr, r2]
	ldr	r3, [ip, #4]
	bl	printk
	b	.L2179
.L2187:
	add	r6, r6, #1
	uxth	r6, r6
	cmp	r6, r9
	bne	.L2175
.L2181:
	ldr	r3, .L2188
	mov	r0, #0
	ldr	r2, [sp, #100]
	ldr	r3, [r3]
	cmp	r2, r3
	beq	.L2182
	bl	__stack_chk_fail
.L2182:
	add	sp, sp, #108
	@ sp needed
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L2189:
	.align	2
.L2188:
	.word	__stack_chk_guard
	.word	.LANCHOR0
	.word	.LANCHOR2
	.word	.LANCHOR3+16
	.word	.LC108
	.word	.LC109
	.word	.LC110
	.word	.LANCHOR0+1804
	.word	.LANCHOR0+1882
	.word	.LC111
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
	ldr	r5, .L2206
	mov	r6, #0
	ldr	r0, .L2206+4
	ldr	r1, .L2206+8
	ldr	r3, [r5]
	str	r3, [sp, #92]
	bl	printk
	str	r5, [sp, #36]
	ldr	r5, .L2206+12
	mov	r7, r5
.L2191:
	ldr	r3, .L2206+16
	uxth	r4, r6
	ldrh	r3, [r3]
	cmp	r3, r4
	bls	.L2202
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
	ldr	r0, .L2206+20
	bl	printk
	mov	r0, r8
	bl	make_superblock
	ldr	r3, .L2206+24
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
.L2192:
	ldr	r1, [sp, #24]
	uxth	r3, r2
	cmp	r3, r1
	bcs	.L2203
	ldrh	r3, [r0, #2]!
	cmp	r3, r9
	beq	.L2193
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
.L2193:
	add	r2, r2, #1
	b	.L2192
.L2203:
	ldr	r0, [r5, #-2076]
	mov	r1, r4
	mov	r2, #0
	mov	r8, #0
	bl	FlashReadPages
	mov	r9, #36
.L2195:
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
	ldr	r0, .L2206+28
	str	r3, [sp, #20]
	ldr	r2, [lr, r2]
	ldr	r3, [ip, #4]
	bl	printk
	b	.L2195
.L2204:
	ldr	r0, [r7, #-2076]
	mov	r1, r4
	mov	r2, #1
	mov	r8, #0
	bl	FlashReadPages
	mov	r9, #36
.L2197:
	uxth	r3, r8
	cmp	r3, r4
	bcs	.L2205
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
	ldr	r0, .L2206+32
	str	r3, [sp, #20]
	ldr	r2, [lr, r2]
	ldr	r3, [ip, #4]
	bl	printk
	b	.L2197
.L2205:
	add	r6, r6, #1
	b	.L2191
.L2202:
	ldr	r3, [sp, #36]
	mov	r0, #0
	ldr	r2, [sp, #92]
	ldr	r3, [r3]
	cmp	r2, r3
	beq	.L2200
	bl	__stack_chk_fail
.L2200:
	add	sp, sp, #100
	@ sp needed
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L2207:
	.align	2
.L2206:
	.word	__stack_chk_guard
	.word	.LC108
	.word	.LANCHOR3+36
	.word	.LANCHOR2
	.word	.LANCHOR0+1814
	.word	.LC112
	.word	.LANCHOR0+1804
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
	ldr	r3, .L2215
	movw	r2, #1870
	stmfd	sp!, {r4, r5, r6, r7, r8, r9, lr}
	.save {r4, r5, r6, r7, r8, r9, lr}
	add	r7, r3, #1872
	ldrh	r1, [r3, r2]
	.pad #28
	sub	sp, sp, #28
	ldr	r0, .L2215+4
	sub	r9, r7, #2
	ldrh	r2, [r7]
	mov	r6, #0
	bl	printk
	ldr	r4, .L2215+8
.L2209:
	ldrh	r3, [r9]
	cmp	r6, r3
	bcs	.L2211
	mov	r8, r6, asl #10
	mov	r5, #0
.L2212:
	ldrh	r3, [r7]
	cmp	r5, r3
	bcs	.L2214
	add	r3, r5, r8
	str	r3, [r4, #216]
	ldr	r3, [r4, #-2064]
	mov	r2, #0
	mov	r1, #1
	ldr	r0, .L2215+12
	str	r2, [r4, #212]
	add	r5, r5, #1
	str	r3, [r4, #220]
	ldr	r3, [r4, #-2040]
	str	r3, [r4, #224]
	bl	FlashReadPages
	ldr	r3, [r4, #224]
	ldr	r2, [r4, #220]
	ldr	r0, .L2215+16
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
	b	.L2212
.L2214:
	add	r6, r6, #1
	b	.L2209
.L2211:
	ldr	r0, .L2215+20
	ldr	r1, .L2215+24
	add	sp, sp, #28
	@ sp needed
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, lr}
	b	printk
.L2216:
	.align	2
.L2215:
	.word	.LANCHOR0
	.word	.LC115
	.word	.LANCHOR2
	.word	.LANCHOR2+212
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
	ldr	ip, .L2275
	stmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.save {r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.pad #76
	sub	sp, sp, #76
	ldr	ip, [ip]
	mov	r4, r0
	ldr	r5, .L2275+4
	mov	r9, r2
	str	r1, [sp, #8]
	str	ip, [sp, #68]
	ldr	ip, [r5, #692]
	ldrb	r7, [r5]	@ zero_extendqisi2
	str	r3, [sp, #20]
	ldrb	ip, [ip, #19]	@ zero_extendqisi2
	cmp	r7, #0
	str	ip, [sp, #16]
	ldr	ip, .L2275+8
	ldrb	ip, [ip, #481]	@ zero_extendqisi2
	str	ip, [sp, #12]
	beq	.L2218
	bl	FlashProgSlc2KPages
	b	.L2219
.L2232:
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
	bcc	.L2272
.L2221:
	add	r7, r7, #1
.L2218:
	ldr	r3, [sp, #8]
	ldr	r6, .L2275+4
	cmp	r7, r3
	bcc	.L2232
	b	.L2273
.L2272:
	ldrb	r1, [r5, #1793]	@ zero_extendqisi2
	add	r3, r5, r3, asl #4
	cmp	r1, #0
	ldr	r3, [r3, #1596]
	moveq	r10, #0
	cmp	r3, #0
	beq	.L2223
	cmp	r2, #1
	bne	.L2224
	ldr	r0, [r5, #76]
	bl	NandcIqrWaitFlashReady
.L2224:
	ldrb	r0, [sp, #28]	@ zero_extendqisi2
	bl	FlashWaitCmdDone
.L2223:
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
	bne	.L2226
	bl	NandcWaitFlashReady
	b	.L2227
.L2226:
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
.L2227:
	ldr	r3, [sp, #16]
	sub	r3, r3, #1
	cmp	r3, #7
	bhi	.L2228
	add	r3, r5, r6
	ldrb	r3, [r3, #1552]	@ zero_extendqisi2
	cmp	r3, #0
	beq	.L2228
	mov	r0, r6
	ldrb	r1, [r5, #699]	@ zero_extendqisi2
	ldr	r2, .L2275+12
	mov	r3, #0
	bl	HynixSetRRPara
.L2228:
	mov	r0, r6
	bl	NandcFlashCs
	cmp	r9, #1
	mov	r0, r6
	bne	.L2229
	ldrb	r3, [r5, #140]	@ zero_extendqisi2
	cmp	r3, #0
	beq	.L2229
	bl	flash_enter_slc_mode
	b	.L2230
.L2229:
	bl	flash_exit_slc_mode
.L2230:
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
	beq	.L2231
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
.L2231:
	mov	r0, r6
	ldr	r1, [sp, #24]
	bl	FlashProgSecondCmd
	mov	r0, r6
	bl	NandcFlashDeCs
	add	r7, r7, r10
	b	.L2221
.L2273:
	ldr	r0, [r6, #76]
	mov	r5, #0
	bl	NandcIqrWaitFlashReady
	ldr	r8, .L2275+16
	mov	r7, r6
.L2233:
	ldrb	r3, [r6, #1717]	@ zero_extendqisi2
	cmp	r5, r3
	bcs	.L2274
	uxtb	r0, r5
	bl	FlashWaitCmdDone
	cmp	r9, #1
	bne	.L2234
	ldrb	r3, [r7, #140]	@ zero_extendqisi2
	cmp	r3, #0
	beq	.L2234
	ldrb	r0, [r8, r5, asl #4]	@ zero_extendqisi2
	bl	flash_exit_slc_mode
.L2234:
	add	r5, r5, #1
	b	.L2233
.L2274:
	ldr	r3, [sp, #20]
	cmp	r3, #0
	bne	.L2236
.L2244:
	mov	r0, #0
	b	.L2219
.L2236:
	ldr	r5, .L2275+20
	mov	r6, #0
	mov	r7, r5
.L2237:
	ldr	r3, [sp, #8]
	cmp	r6, r3
	beq	.L2244
	ldr	r3, [r4]
	cmn	r3, #1
	bne	.L2238
	ldr	r1, [r4, #4]
	ldr	r0, .L2275+24
	bl	printk
	b	.L2239
.L2238:
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
	bne	.L2240
	ldr	r0, .L2275+28
	ldr	r1, [r4, #4]
	bl	printk
	str	r8, [r4]
.L2240:
	ldr	r3, [r4, #12]
	cmp	r3, #0
	beq	.L2241
	ldr	r2, [r3]
	ldr	r3, [r7, #188]
	ldr	r3, [r3]
	cmp	r2, r3
	beq	.L2241
	ldr	r0, .L2275+32
	ldr	r1, [r4, #4]
	bl	printk
	mvn	r3, #0
	str	r3, [r4]
.L2241:
	ldr	r3, [r4, #8]
	cmp	r3, #0
	beq	.L2239
	ldr	r2, [r3]
	ldr	r3, [r7, #184]
	ldr	r3, [r3]
	cmp	r2, r3
	beq	.L2239
	ldr	r0, .L2275+36
	ldr	r1, [r4, #4]
	bl	printk
	mvn	r3, #0
	str	r3, [r4]
.L2239:
	add	r6, r6, #1
	add	r4, r4, #36
	b	.L2237
.L2219:
	ldr	r3, .L2275
	ldr	r2, [sp, #68]
	ldr	r3, [r3]
	cmp	r2, r3
	beq	.L2245
	bl	__stack_chk_fail
.L2245:
	add	sp, sp, #76
	@ sp needed
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L2276:
	.align	2
.L2275:
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
	ldr	r3, .L2282
	stmfd	sp!, {r4, r5, r6, lr}
	.save {r4, r5, r6, lr}
	.pad #104
	sub	sp, sp, #104
	ldr	r2, [r3]
	mov	r4, r0
	ldr	r5, .L2282+4
	mov	r6, r3
	str	r2, [sp, #100]
	ldr	r2, [r5, #168]
	cmp	r0, r2
	movcc	r4, #0
	bcc	.L2278
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
.L2278:
	ldr	r2, [sp, #100]
	mov	r0, r4
	ldr	r3, [r6]
	cmp	r2, r3
	beq	.L2279
	bl	__stack_chk_fail
.L2279:
	add	sp, sp, #104
	@ sp needed
	ldmfd	sp!, {r4, r5, r6, pc}
.L2283:
	.align	2
.L2282:
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
	ldr	r3, .L2340
	mov	r1, #1
	stmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.save {r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.pad #92
	sub	sp, sp, #92
	ldr	r3, [r3]
	ldr	r5, .L2340+4
	ldr	r4, .L2340+8
	str	r3, [sp, #84]
	ldr	r3, [r5, #180]
	ldrh	r8, [r4, #124]
	ldr	r0, .L2340+12
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
	ldr	r4, .L2340+8
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
	ldr	r5, .L2340+4
	mov	r3, r8, lsr #4
	str	r3, [sp, #28]
	mov	r3, #0
	str	r3, [sp, #8]
.L2286:
	ldrb	r7, [sp, #8]	@ zero_extendqisi2
	ldrb	r3, [r4, #1717]	@ zero_extendqisi2
	cmp	r3, r7
	bls	.L2336
	add	r3, r5, r7, asl #1
	ldrh	r6, [r3, #196]
	cmp	r6, #0
	bne	.L2316
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
.L2288:
	ldrh	r3, [sp, #4]
	cmp	r3, r8
	str	r3, [sp, #12]
	bcs	.L2297
	mvn	r3, #0
	strb	r3, [sp, #46]
	strb	r3, [sp, #47]
	ldr	r3, [sp, #24]
	tst	r3, #1
	beq	.L2290
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
	bne	.L2290
	ldr	r1, [r4, #4]
	add	r2, sp, #47
	mov	r0, r10
	add	r1, r3, r1
	bl	FlashReadSpare
	ldrb	r3, [sp, #46]	@ zero_extendqisi2
	ldrb	r2, [sp, #47]	@ zero_extendqisi2
	and	r3, r3, r2
	strb	r3, [sp, #46]
.L2290:
	ldr	r3, [sp, #24]
	tst	r3, #2
	beq	.L2292
	ldr	r3, [r4, #692]
	mov	r0, r10
	add	r2, sp, #47
	ldrh	r1, [r3, #10]
	ldr	r3, [fp, #656]
	sub	r1, r1, #1
	add	r1, r1, r3
	add	r1, r1, r6
	bl	FlashReadSpare
.L2292:
	ldr	r3, [r4, #692]
	ldrb	r3, [r3, #7]	@ zero_extendqisi2
	cmp	r3, #1
	cmpne	r3, #8
	ldrb	r3, [sp, #46]	@ zero_extendqisi2
	bne	.L2293
	cmp	r3, #0
	ldrneb	r0, [sp, #47]	@ zero_extendqisi2
	clzne	r0, r0
	movne	r0, r0, lsr #5
	bne	.L2294
	b	.L2315
.L2293:
	cmp	r3, #255
	bne	.L2315
	ldrb	r0, [sp, #47]	@ zero_extendqisi2
	subs	r0, r0, #255
	movne	r0, #1
	b	.L2294
.L2315:
	mov	r0, #1
.L2294:
	ldr	r3, [sp, #24]
	tst	r3, #4
	beq	.L2295
	ldr	r3, .L2340+8
	mov	r0, r10
	ldr	r2, [sp, #32]
	add	r3, r3, r2
	ldr	r1, [r3, #656]
	add	r1, r6, r1
	bl	SandiskProgTestBadBlock
.L2295:
	cmp	r0, #0
	beq	.L2296
	mov	r1, r7
	ldr	r2, [sp, #4]
	ldr	r0, .L2340+16
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
	bgt	.L2297
.L2296:
	ldr	r3, [sp, #4]
	add	r3, r3, #1
	str	r3, [sp, #4]
	ldr	r3, [sp, #16]
	add	r6, r6, r3
	b	.L2288
.L2297:
	mov	r2, r9
	ldr	r0, .L2340+20
	mov	r1, r7
	bl	printk
	ldrb	r3, [r4, #1717]	@ zero_extendqisi2
	ldr	r2, [sp, #28]
	mul	r3, r3, r2
	cmp	r9, r3
	blt	.L2299
	ldrh	r2, [r4, #132]
	mov	r1, #0
	ldr	r0, [r5, #148]
	mov	r2, r2, asl #9
	bl	ftl_memset
.L2299:
	cmp	r7, #0
	ldreqh	r10, [r5, #168]
	moveq	r6, r7
	moveq	r9, #1
	bne	.L2301
.L2302:
	ldr	r3, .L2340+8
	ldrb	r3, [r3, #1]	@ zero_extendqisi2
	cmp	r3, r10
	bls	.L2337
	mov	r0, r10
	bl	FlashTestBlk
	cmp	r0, #0
	beq	.L2303
	mov	r1, r10
	ldr	r0, .L2340+24
	bl	printk
	ldr	r1, [r5, #148]
	mov	r0, r10, lsr #5
	add	r6, r6, #1
	and	r3, r10, #31
	ldr	r2, [r1, r0, asl #2]
	uxth	r6, r6
	orr	r3, r2, r9, asl r3
	str	r3, [r1, r0, asl #2]
.L2303:
	add	r10, r10, #1
	uxth	r10, r10
	b	.L2302
.L2337:
	sub	fp, r8, #1
	sub	r9, r8, #50
	mov	r10, #1
	uxth	fp, fp
.L2305:
	cmp	fp, r9
	ble	.L2338
	mov	r0, fp
	bl	FlashTestBlk
	cmp	r0, #0
	beq	.L2306
	mov	r1, fp
	ldr	r0, .L2340+24
	bl	printk
	ldr	r1, [r5, #148]
	mov	r0, fp, lsr #5
	and	r3, fp, #31
	ldr	r2, [r1, r0, asl #2]
	orr	r3, r2, r10, asl r3
	str	r3, [r1, r0, asl #2]
.L2306:
	sub	fp, fp, #1
	uxth	fp, fp
	b	.L2305
.L2338:
	ldr	r3, .L2340+8
	ldr	r2, [r5, #168]
	ldrb	r3, [r3, #1]	@ zero_extendqisi2
	rsb	r3, r2, r3
	cmp	r6, r3
	bcc	.L2301
	ldrh	r2, [r4, #132]
	mov	r1, #0
	ldr	r0, [r5, #148]
	mov	r2, r2, asl #9
	bl	ftl_memset
.L2301:
	ldrb	r6, [sp, #8]	@ zero_extendqisi2
	sub	r10, r8, #1
	add	r9, r5, r7, asl #1
	uxth	r10, r10
	mul	r6, r8, r6
.L2309:
	mov	r1, r7
	ldr	r0, .L2340+28
	mov	r2, r10
	bl	printk
	ldr	r1, [r5, #148]
.L2310:
	mov	r2, r10, lsr #5
	and	r3, r10, #31
	ldr	r2, [r1, r2, asl #2]
	mov	r3, r2, lsr r3
	ands	r3, r3, #1
	subne	r10, r10, #1
	uxthne	r10, r10
	bne	.L2310
.L2339:
	ldr	r1, [sp, #20]
	add	r0, sp, #48
	ldr	r2, .L2340+32
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
	bne	.L2309
.L2316:
	ldr	r3, [sp, #8]
	add	r3, r3, #1
	str	r3, [sp, #8]
	b	.L2286
.L2336:
	ldr	r3, .L2340
	ldr	r2, [sp, #84]
	ldr	r3, [r3]
	cmp	r2, r3
	beq	.L2313
	bl	__stack_chk_fail
.L2313:
	add	sp, sp, #92
	@ sp needed
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L2341:
	.align	2
.L2340:
	.word	__stack_chk_guard
	.word	.LANCHOR2
	.word	.LANCHOR0
	.word	.LC118
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
	ldr	r5, .L2392
	ldr	r7, [r5, #4052]
	ldrb	r3, [r5, #1793]	@ zero_extendqisi2
	cmp	r7, #0
	str	r3, [sp, #20]
	movne	r0, #0
	bne	.L2343
	ldrb	r3, [r5, #140]	@ zero_extendqisi2
	mov	r10, r5
	ldr	r6, .L2392+4
	mov	fp, r7
	mov	r5, r7
	mov	r4, r7
	mov	r8, r10
	mov	r9, #36
	str	r1, [sp, #4]
	str	r0, [sp, #8]
	str	r3, [sp, #12]
	str	r0, [r6, #-2088]
.L2344:
	ldr	r3, .L2392+8
	uxth	r2, fp
	ldr	r7, .L2392
	ldrh	r1, [r3]
	cmp	r1, r2
	bls	.L2387
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
	beq	.L2345
	bl	IsBlkInVendorPart
	cmp	r0, #0
	bne	.L2346
.L2345:
	mov	r0, r7
	bl	FtlBbmIsBadBlock
	cmp	r0, #0
	addne	r4, r4, #1
	uxthne	r4, r4
	bne	.L2346
	ldr	r3, .L2392+12
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
.L2346:
	add	fp, fp, #1
	b	.L2344
.L2387:
	cmp	r5, #0
	beq	.L2367
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
.L2350:
	uxth	r3, r7
	cmp	r3, r5
	bcs	.L2388
	mul	r3, r8, r7
	ldr	r2, [r10, #4056]
	add	r0, r2, r3
	ldr	r3, [r2, r3]
	cmn	r3, #1
	bne	.L2351
	ldr	r0, [r0, #4]
	add	r4, r4, #1
	ubfx	r0, r0, #10, #16
	uxth	r4, r4
	bl	FtlBbmMapBadBlock
.L2351:
	add	r7, r7, #1
	b	.L2350
.L2388:
	ldr	r3, [sp, #4]
	cmp	r3, #0
	bne	.L2353
	mov	r3, #6
	uxth	r6, r6
	str	r3, [sp, #16]
	mov	r3, #1
	str	r3, [sp, #12]
	b	.L2354
.L2353:
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
.L2354:
	ldr	r8, .L2392
	mov	r7, #0
.L2363:
	ldr	fp, .L2392+4
	mov	r9, #0
	mov	r5, r9
.L2355:
	ldr	r3, .L2392+8
	ldr	r10, .L2392
	ldrh	r2, [r3]
	uxth	r3, r9
	cmp	r2, r3
	bls	.L2389
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
	beq	.L2356
	bl	IsBlkInVendorPart
	cmp	r0, #0
	bne	.L2357
.L2356:
	mov	r0, r10
	bl	FtlBbmIsBadBlock
	cmp	r0, #0
	bne	.L2357
	ldr	r2, [r8, #4056]
	mov	r3, #36
	add	r10, r7, r10, asl #10
	mla	r2, r3, r5, r2
	ldr	r3, [fp, #-2048]
	str	r3, [r2, #8]
	ldr	r3, .L2392+12
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
.L2357:
	add	r9, r9, #1
	b	.L2355
.L2389:
	cmp	r5, #0
	beq	.L2367
	ldr	r2, .L2392
	mov	r3, #0
	mov	r1, r5
	mov	r9, #0
	mov	fp, #36
	strb	r3, [r2, #1793]
	mov	r3, #1
	ldr	r0, [r2, #4056]
	mov	r2, r6
	bl	FlashProgPages
	ldr	r3, .L2392
	ldrb	r2, [sp, #20]	@ zero_extendqisi2
	strb	r2, [r3, #1793]
.L2360:
	uxth	r3, r9
	cmp	r3, r5
	bcs	.L2390
	mul	r3, fp, r9
	ldr	r2, .L2392
	ldr	r2, [r2, #4056]
	add	r0, r2, r3
	ldr	r3, [r2, r3]
	cmp	r3, #0
	beq	.L2361
	ldr	r0, [r0, #4]
	add	r4, r4, #1
	ubfx	r0, r0, #10, #16
	uxth	r4, r4
	bl	FtlBbmMapBadBlock
.L2361:
	add	r9, r9, #1
	b	.L2360
.L2390:
	ldr	r3, [sp, #16]
	add	r7, r7, r3
	ldr	r3, [sp, #12]
	uxth	r7, r7
	cmp	r7, r3
	bcc	.L2363
	mov	r7, #0
	mov	r8, #36
.L2364:
	uxth	r3, r7
	cmp	r3, r5
	ldr	r3, [sp, #4]
	bcs	.L2391
	cmp	r3, #0
	beq	.L2365
	mul	r3, r8, r7
	ldr	r2, [r10, #4056]
	add	r1, r2, r3
	ldr	r3, [r2, r3]
	cmp	r3, #0
	bne	.L2365
	ldr	r0, [r1, #4]
	mov	r1, #1
	ubfx	r0, r0, #10, #16
	bl	FtlFreeSysBlkQueueIn
.L2365:
	add	r7, r7, #1
	b	.L2364
.L2391:
	adds	r3, r3, #0
	ldr	r2, [sp, #8]
	movne	r3, #1
	cmp	r2, #63
	orrls	r3, r3, #1
	cmp	r3, #0
	beq	.L2367
	ldr	r0, [r10, #4056]
	mov	r1, r6
	mov	r2, r5
	bl	FlashEraseBlocks
.L2367:
	mov	r0, r4
.L2343:
	add	sp, sp, #28
	@ sp needed
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L2393:
	.align	2
.L2392:
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
	ldr	r5, .L2412
	ldr	r6, [r5, #4052]
	cmp	r6, #0
	bne	.L2396
	ldr	r4, .L2412+4
	mov	r1, r6
	ldr	r7, .L2412+8
	ldr	r10, .L2412+12
	ldr	r3, [r4, #-2040]
	ldr	r0, [r4, #-2064]
	str	r3, [r4, #224]
	movw	r3, #1882
	ldrh	r2, [r5, r3]
	add	r5, r5, #1936
	str	r0, [r4, #220]
	bl	ftl_memset
	sub	r9, r5, #110
.L2397:
	ldrh	r3, [r9]
	ldr	r8, .L2412
	cmp	r6, r3
	bge	.L2411
	ldrh	r2, [r10]
	ldr	r3, [r4, #220]
	ldr	r1, [r7, #4]!
	mul	r0, r2, r6
	mov	r2, r2, asl #2
	add	r6, r6, #1
	add	r0, r3, r0, asl #2
	bl	ftl_memcpy
	b	.L2397
.L2411:
	ldr	r6, [r4, #224]
	mov	r1, #255
	mov	r2, #16
	ldr	fp, .L2412+4
	mov	r7, #0
	mov	r0, r6
	mov	r10, r7
	bl	ftl_memset
	ldr	r3, .L2412+16
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
.L2399:
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
	ldr	r0, .L2412+20
	bl	printk
	ldr	r9, .L2412+24
	ldrh	r2, [r5, #2]
	ldrh	r3, [r9, #-62]
	sub	r3, r3, #1
	cmp	r2, r3
	ldr	ip, [sp, #12]
	blt	.L2400
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
.L2400:
	ldr	r9, .L2412+4
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
	bne	.L2401
	add	r7, r7, #1
	ldr	r0, .L2412+28
	ldr	r1, [fp, #216]
	uxth	r7, r7
	bl	printk
	cmp	r7, #3
	bls	.L2399
	mov	r2, r7
	ldr	r0, .L2412+32
	ldr	r1, [r9, #216]
	bl	printk
	ldr	r3, .L2412
	mov	r2, #1
	str	r2, [r3, #4052]
	b	.L2396
.L2401:
	add	r10, r10, #1
	cmp	r10, #1
	beq	.L2399
	cmp	r3, #256
	beq	.L2399
.L2396:
	mov	r0, #0
	add	sp, sp, #20
	@ sp needed
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L2413:
	.align	2
.L2412:
	.word	.LANCHOR0
	.word	.LANCHOR2
	.word	.LANCHOR0+1960
	.word	.LANCHOR2-1976
	.word	-3887
	.word	.LC123
	.word	.LANCHOR0+1936
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
	ldr	r6, .L2469
	ldr	r3, [r6, #4052]
	cmp	r3, #0
	bne	.L2415
	ldr	r10, .L2469+4
	mov	r4, r0
	mov	r7, r10
.L2416:
	ldr	r3, .L2469+8
	ldrb	r2, [r4, #8]	@ zero_extendqisi2
	cmp	r4, r3
	bne	.L2417
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
	beq	.L2418
	ldr	lr, [r10, #-3824]
	cmp	lr, #39
	bhi	.L2418
	cmp	lr, #2
	bls	.L2443
	cmp	ip, #0
	movne	r3, #0
	andeq	r3, r3, #1
	cmp	r3, #0
	moveq	r1, r0
	beq	.L2418
	b	.L2443
.L2417:
	cmp	r2, #1
	bne	.L2443
	ldr	r0, .L2469
	add	r3, r0, #1824
	ldrh	r3, [r3]
	cmp	r3, #1
	beq	.L2443
	ldrb	r3, [r0, #140]	@ zero_extendqisi2
	cmp	r3, #0
	bne	.L2443
	ldr	r3, .L2469+12
	ldr	r0, [r0, #1796]
	ldrh	r3, [r3]
	cmp	r0, #0
	mov	r1, r3, lsr #3
	beq	.L2418
	ldr	r0, [r10, #-3824]
	cmp	r0, #1
	rsbls	r3, r3, r3, asl #3
	ubfxls	r1, r3, #3, #16
.L2418:
	cmp	r1, #0
	subne	r1, r1, #1
	uxthne	r1, r1
	b	.L2419
.L2443:
	mov	r1, #0
.L2419:
	ldr	r0, .L2469+16
	bl	List_pop_index_node
	ldr	r2, .L2469+12
	ldrh	r3, [r2]
	sub	r3, r3, #1
	strh	r3, [r2]	@ movhi
	ldr	r3, .L2469+20
	ldrh	r3, [r3]
	uxth	r9, r0
	cmp	r3, r9
	bls	.L2416
	ldr	r3, [r10, #-4060]
	mov	r8, r9, asl #1
	ldrh	r5, [r3, r8]
	cmp	r5, #0
	bne	.L2416
	strh	r9, [r4]	@ movhi
	mov	r0, r4
	bl	make_superblock
	ldrb	r3, [r4, #7]	@ zero_extendqisi2
	cmp	r3, #0
	beq	.L2421
	ldr	r3, .L2469
	mov	fp, #36
	ldr	r2, .L2469+24
	add	r0, r4, #14
	ldr	r1, [r3, #4056]
	ldrh	ip, [r2]
	mov	r3, r1
	mla	r2, fp, ip, r1
	mov	ip, r5
	str	r2, [sp]
	b	.L2422
.L2421:
	ldr	r3, [r10, #-4060]
	b	.L2464
.L2422:
	ldr	r2, [sp]
	cmp	r3, r2
	beq	.L2466
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
	b	.L2422
.L2466:
	ldr	r3, .L2469+28
	ldr	r2, [r6, #1796]
	rsb	r3, r3, r4
	clz	r3, r3
	cmp	r2, #0
	mov	r3, r3, lsr #5
	moveq	r3, #0
	cmp	r3, #0
	beq	.L2425
	ldr	r3, .L2469
	ldr	r3, [r3, #4060]
	ldrh	r3, [r3, r8]
	cmp	r3, #40
	movhi	r3, #0
	strhib	r3, [r10, #-4032]
.L2425:
	ldrb	r3, [r4, #8]	@ zero_extendqisi2
	ldr	r2, [r6, #4060]
	cmp	r3, #0
	ldr	fp, .L2469+32
	ldrh	r3, [r2, r8]
	bne	.L2426
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
	b	.L2428
.L2426:
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
.L2428:
	ldr	r3, [r6, #4060]
	ldr	r2, [r7, #-3828]
	ldr	r0, [r7, #-3840]
	ldrh	r3, [r3, r8]
	cmp	r3, r2
	ldrh	r2, [fp]
	strhi	r3, [r10, #-3828]
	ldr	r3, [r7, #-3836]
	mla	r0, r0, r2, r3
	ldr	r3, .L2469+20
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
.L2430:
	add	r2, r2, #36
	cmp	r2, r3
	ldrne	r1, [r2, #-36]
	bicne	r1, r1, #1020
	bicne	r1, r1, #3
	strne	r1, [r2, #-36]
	bne	.L2430
.L2467:
	ldrb	r2, [r6, #140]	@ zero_extendqisi2
	ldr	r3, .L2469
	cmp	r2, #0
	beq	.L2432
	ldrb	r2, [r4, #8]	@ zero_extendqisi2
	ldr	r0, [r3, #4056]
	cmp	r2, #1
	mov	r2, r5
	moveq	r1, #0
	movne	r1, #1
	bl	FlashEraseBlocks
.L2432:
	ldrb	r1, [r4, #8]	@ zero_extendqisi2
	mov	r2, r5
	ldr	r0, [r6, #4056]
	mov	fp, #0
	bl	FlashEraseBlocks
	add	r1, r4, #16
	mov	r2, fp
	mov	ip, #36
.L2434:
	uxth	r3, fp
	cmp	r3, r5
	bcs	.L2468
	mul	r3, ip, fp
	ldr	lr, [r6, #4056]
	add	r0, lr, r3
	ldr	r3, [lr, r3]
	cmn	r3, #1
	bne	.L2435
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
.L2435:
	add	fp, fp, #1
	add	r1, r1, #2
	b	.L2434
.L2468:
	cmp	r2, #0
	beq	.L2437
	mov	r0, r9
	bl	update_multiplier_value
	bl	FtlBbmTblFlush
.L2437:
	ldrb	r3, [r4, #7]	@ zero_extendqisi2
	cmp	r3, #0
	bne	.L2438
	ldr	r3, [r7, #-4060]
.L2464:
	mvn	r2, #0
	strh	r2, [r3, r8]	@ movhi
	b	.L2416
.L2438:
	ldr	r2, .L2469+36
	ldr	r1, .L2469+4
	ldrh	r2, [r2]
	strh	r9, [r4]	@ movhi
	smulbb	r3, r2, r3
	mov	r2, #0
	strh	r2, [r4, #2]	@ movhi
	strb	r2, [r4, #6]
	ldr	r2, .L2469+4
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
.L2415:
	mov	r0, #0
	add	sp, sp, #20
	@ sp needed
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L2470:
	.align	2
.L2469:
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
	ldr	r5, .L2485
	ldrh	r3, [r5, #-6]
	cmp	r3, #0
	beq	.L2473
	sub	r5, r5, #6
	mov	r8, r0
	mov	r7, #0
	mov	r9, r5
.L2472:
	ldr	r3, .L2485+4
	uxth	r2, r7
	ldrh	r3, [r3]
	cmp	r3, r2
	bls	.L2482
	ldr	r3, .L2485+8
	mov	r1, r8
	mov	r10, #0
	add	r3, r3, r2
	ldrb	r0, [r3, #1832]	@ zero_extendqisi2
	bl	V2P_block
	ldr	ip, .L2485+12
	mov	fp, r0
.L2474:
	ldrh	r3, [r5]
	uxth	r4, r10
	cmp	r3, r4
	bls	.L2483
	mov	r3, r4, asl #1
	add	r6, ip, r3
	ldrh	r3, [ip, r3]
	cmp	r3, fp
	bne	.L2475
	mov	r1, fp
	ldr	r0, .L2485+16
	str	ip, [sp, #4]
	bl	printk
	mov	r0, fp
	bl	FtlBbmMapBadBlock
	bl	FtlBbmTblFlush
	ldrh	r2, [r5]
	mov	r3, r6
	ldr	ip, [sp, #4]
.L2476:
	cmp	r4, r2
	ldrcch	r1, [r3, #2]
	addcc	r4, r4, #1
	uxthcc	r4, r4
	strcch	r1, [r3], #2	@ movhi
	bcc	.L2476
.L2484:
	sub	r2, r2, #1
	strh	r2, [r9]	@ movhi
.L2475:
	add	r10, r10, #1
	b	.L2474
.L2483:
	add	r7, r7, #1
	b	.L2472
.L2482:
	bl	FtlGcReFreshBadBlk
.L2473:
	mov	r0, #0
	add	sp, sp, #12
	@ sp needed
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L2486:
	.align	2
.L2485:
	.word	.LANCHOR2-3168
	.word	.LANCHOR0+1804
	.word	.LANCHOR0
	.word	.LANCHOR2-3172
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
	ldr	r2, .L2497
	mov	r3, r0, asl #1
	stmfd	sp!, {r4, lr}
	.save {r4, lr}
	mov	r4, r0
	ldr	r1, [r2, #-4060]
	ldrh	r3, [r1, r3]
	cmp	r3, #0
	bne	.L2488
	sub	r1, r2, #3792
	ldrh	r0, [r1, #-8]
	cmp	r0, r4
	mvneq	r3, #0
	streqh	r3, [r1, #-8]	@ movhi
	beq	.L2490
	sub	r1, r2, #4032
	ldrh	r1, [r1, #-8]
	cmp	r1, r4
	beq	.L2496
	sub	r1, r2, #3984
	ldrh	r1, [r1, #-8]
	cmp	r1, r4
	beq	.L2496
	sub	r2, r2, #3936
	ldrh	r2, [r2, #-8]
	cmp	r2, r4
	beq	.L2496
.L2490:
	mov	r1, r4
	ldr	r0, .L2497+4
	bl	List_remove_node
	ldr	r2, .L2497+8
	mov	r0, r4
	ldrh	r3, [r2, #-4]
	sub	r3, r3, #1
	strh	r3, [r2, #-4]	@ movhi
	bl	free_data_superblock
	mov	r0, r4
	bl	FtlGcFreeBadSuperBlk
	mov	r0, #1
	ldmfd	sp!, {r4, pc}
.L2488:
	bl	List_update_data_list
.L2496:
	mov	r0, #0
	ldmfd	sp!, {r4, pc}
.L2498:
	.align	2
.L2497:
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
	beq	.L2500
	ldr	r5, .L2510
	mov	r6, r0, asl #1
	ldr	r3, [r5, #-4060]
	ldrh	r2, [r3, r6]
	cmp	r2, #0
	subne	r2, r2, #1
	strneh	r2, [r3, r6]	@ movhi
	bne	.L2500
	mov	r1, r4
	ldr	r0, .L2510+4
	bl	printk
	ldr	r3, [r5, #-4060]
	mov	r2, #32
	sub	r0, r5, #4048
	mov	r1, r4
	strh	r2, [r3, r6]	@ movhi
	bl	test_node_in_list
	cmp	r0, #0
	beq	.L2502
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
	ldr	r0, .L2510+8
	mov	r1, r4
	ldrh	r2, [r3, r6]
	bl	printk
.L2502:
	mov	r0, r4
	bl	FtlGcRefreshBlock
	b	.L2505
.L2500:
	ldr	r5, .L2510+12
	movw	r3, #65535
	ldrh	r0, [r5]
	cmp	r0, r3
	streqh	r4, [r5]	@ movhi
	beq	.L2505
	cmp	r0, r4
	beq	.L2505
	bl	update_vpc_list
	strh	r4, [r5]	@ movhi
	adds	r0, r0, #0
	movne	r0, #1
	ldmfd	sp!, {r4, r5, r6, pc}
.L2505:
	mov	r0, #0
	ldmfd	sp!, {r4, r5, r6, pc}
.L2511:
	.align	2
.L2510:
	.word	.LANCHOR2
	.word	.LC127
	.word	.LC128
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
	ldr	r5, .L2526
	ldr	r6, .L2526+4
	add	r3, r0, r3, asl #1
	ldr	r7, .L2526+8
	ldrh	r3, [r3, #16]
.L2516:
	movw	r1, #65535
	cmp	r3, r1
	bne	.L2525
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
	ldrh	r3, [r3, #16]
	b	.L2516
.L2525:
	ldrb	r2, [r4, #8]	@ zero_extendqisi2
	cmp	r2, #1
	bne	.L2519
	ldrb	r3, [r5, #140]	@ zero_extendqisi2
	cmp	r3, #0
	bne	.L2519
	ldrh	r3, [r4, #2]
	mov	r3, r3, asl #1
	ldrh	r3, [r6, r3]
	cmp	r3, r1
	bne	.L2519
	ldrh	r3, [r4, #4]
	ldrh	r0, [r4]
	sub	r3, r3, #1
	strh	r3, [r4, #4]	@ movhi
	bl	decrement_vpc_count
	ldrh	r2, [r4, #4]
	cmp	r2, #0
	bne	.L2518
	ldrh	r3, [r4, #2]
	add	r3, r3, #1
	b	.L2524
.L2519:
	ldr	r3, .L2526
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
	ldr	r1, .L2526+12
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
.L2524:
	strh	r3, [r4, #2]	@ movhi
	strb	r2, [r4, #6]
	ldmfd	sp!, {r3, r4, r5, r6, r7, pc}
.L2527:
	.align	2
.L2526:
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
	ldr	r7, .L2546
	ldr	r8, .L2546+4
	add	r3, r0, r3, asl #1
	ldr	r9, .L2546+8
	ldrh	r2, [r3, #16]
.L2529:
	movw	r1, #65535
	cmp	r2, r1
	ldr	r6, .L2546
	bne	.L2545
.L2530:
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
	b	.L2529
.L2545:
	ldrb	r3, [r4, #8]	@ zero_extendqisi2
	ldrh	r5, [r4, #2]
	cmp	r3, #1
	ldrh	r3, [r4, #4]
	bne	.L2532
	ldrb	r0, [r8, #140]	@ zero_extendqisi2
	cmp	r0, #0
	bne	.L2532
	mov	r0, r5, asl #1
	ldrh	r0, [r9, r0]
	cmp	r0, r1
	bne	.L2532
	sub	r3, r3, #1
	ldrh	r0, [r4]
	strh	r3, [r4, #4]	@ movhi
	bl	decrement_vpc_count
	b	.L2530
.L2532:
	ldr	r7, .L2546+4
	orr	r5, r5, r2, asl #10
	sub	r3, r3, #1
	strh	r3, [r4, #4]	@ movhi
.L2533:
	ldrb	r3, [r4, #6]	@ zero_extendqisi2
	movw	r2, #65535
	ldrh	r0, [r6]
.L2535:
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
	beq	.L2535
	strb	r3, [r4, #6]
	ldrb	r3, [r4, #8]	@ zero_extendqisi2
	cmp	r3, #1
	bne	.L2540
	ldrb	r3, [r7, #140]	@ zero_extendqisi2
	cmp	r3, #0
	bne	.L2537
	ldrh	r3, [r4, #2]
	ldr	r2, .L2546+8
	mov	r3, r3, asl #1
	ldrh	r2, [r2, r3]
	movw	r3, #65535
	cmp	r2, r3
	bne	.L2537
	ldrh	r3, [r4, #4]
	cmp	r3, #0
	beq	.L2537
	sub	r3, r3, #1
	ldrh	r0, [r4]
	strh	r3, [r4, #4]	@ movhi
	bl	decrement_vpc_count
	b	.L2533
.L2537:
	ldr	r3, .L2546+4
	ldrb	r2, [r3, #140]	@ zero_extendqisi2
	cmp	r2, #0
	beq	.L2540
	movw	r2, #1874
	ldrh	r1, [r4, #2]
	ldrh	r2, [r3, r2]
	cmp	r1, r2
	bcc	.L2540
	ldr	r1, .L2546+12
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
.L2540:
	mov	r0, r5
	ldmfd	sp!, {r3, r4, r5, r6, r7, r8, r9, pc}
.L2547:
	.align	2
.L2546:
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
	ldr	r5, .L2566
	ldr	r3, [r5, #4052]
	cmp	r3, #0
	bne	.L2550
	ldr	r4, .L2566+4
	mov	r1, #255
	ldr	r8, .L2566+8
	ldr	r2, [r4, #-2064]
	ldr	r6, [r4, #-2040]
	str	r2, [r4, #220]
	sub	r2, r4, #3808
	sub	r7, r2, #8
	str	r6, [r4, #224]
	ldrh	r2, [r2, #-8]
	str	r3, [r6, #12]
	strh	r2, [r6, #2]	@ movhi
	ldr	r2, .L2566+12
	strh	r2, [r6]	@ movhi
	ldr	r2, [r4, #-3808]
	stmib	r6, {r2, r3}
	ldr	r3, .L2566+16
	ldrh	r2, [r7, #-222]
	str	r3, [r5, #4068]
	ldr	r3, .L2566+20
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
	beq	.L2551
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
.L2551:
	mov	r0, #0
	ldr	r9, .L2566+4
	bl	FtlUpdateVaildLpn
	ldr	fp, .L2566+24
	mov	r8, #0
	movw	r10, #65535
.L2552:
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
	ldr	r3, .L2566+28
	blt	.L2553
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
.L2553:
	ldrb	r3, [r5]	@ zero_extendqisi2
	cmp	r3, #0
	beq	.L2554
	ldr	r3, .L2566+32
	ldr	r0, [r4, #-2064]
	ldrh	r1, [r3]
	bl	js_hash
	str	r0, [r6, #12]
.L2554:
	mov	r1, #1
	ldr	r0, .L2566+36
	mov	r2, r1
	mov	r3, r1
	bl	FlashProgPages
	ldrh	r3, [r7, #2]
	ldr	r2, [r4, #212]
	add	r3, r3, #1
	cmn	r2, #1
	uxth	r3, r3
	strh	r3, [r7, #2]	@ movhi
	bne	.L2555
	cmp	r3, #1
	add	r8, r8, #1
	ldreq	r3, .L2566+24
	uxth	r8, r8
	ldreq	r2, .L2566+28
	ldreqh	r3, [r3]
	subeq	r3, r3, #1
	streqh	r3, [r2, #2]	@ movhi
	cmp	r8, #3
	bls	.L2552
	ldr	r0, .L2566+40
	mov	r2, r8
	ldr	r1, [r4, #216]
	bl	printk
	mov	r3, #1
	str	r3, [r5, #4052]
	b	.L2550
.L2555:
	cmp	r2, #256
	cmpne	r3, #1
	beq	.L2552
	movw	r3, #65535
	cmp	r10, r3
	beq	.L2550
	mov	r0, r10
	mov	r1, #1
	bl	FtlFreeSysBlkQueueIn
.L2550:
	mov	r0, #0
	add	sp, sp, #12
	@ sp needed
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L2567:
	.align	2
.L2566:
	.word	.LANCHOR0
	.word	.LANCHOR2
	.word	.LANCHOR0+4068
	.word	-3932
	.word	1179929683
	.word	1342177367
	.word	.LANCHOR0+1874
	.word	.LANCHOR2-3816
	.word	.LANCHOR0+1882
	.word	.LANCHOR2+212
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
	stmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.save {r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.pad #44
	sub	sp, sp, #44
	ldr	r8, .L2586
	ldr	r3, [r8]
	str	r3, [sp, #36]
	ldr	r3, .L2586+4
	ldr	r5, [r3, #4052]
	cmp	r5, #0
	bne	.L2568
	ldrb	r6, [r3, #140]	@ zero_extendqisi2
	mov	r4, r0
	cmp	r6, #0
	beq	.L2585
	ldrb	r6, [r0, #8]	@ zero_extendqisi2
	cmp	r6, #1
	ldreqh	fp, [r0, #4]
	beq	.L2570
	mov	r6, r5
.L2585:
	mov	fp, #12
.L2570:
	mvn	r3, #0
	str	r3, [sp, #16]
	ldr	r3, .L2586+8
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
	ldr	r1, .L2586+12
	ldr	r2, [r3, #-2064]
	str	r1, [r2, #4]
.L2571:
	subs	fp, fp, #1
	bcc	.L2574
	ldrh	r3, [r4, #4]
	cmp	r3, #0
	bne	.L2572
.L2574:
	ldrh	r3, [r4]
	ldr	r1, [r5, #-4060]
	ldrh	r0, [r4, #4]
	mov	r3, r3, asl #1
	ldrh	r2, [r1, r3]
	rsb	r2, r0, r2
	strh	r2, [r1, r3]	@ movhi
	ldr	r3, .L2586+16
	ldrh	r3, [r3]
	strh	r3, [r4, #2]	@ movhi
	mov	r3, #0
	strb	r3, [r4, #6]
	strh	r3, [r4, #4]	@ movhi
	b	.L2568
.L2572:
	mov	r0, r4
	bl	get_new_active_ppa
	cmn	r0, #1
	str	r0, [sp, #4]
	beq	.L2574
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
	b	.L2571
.L2568:
	ldr	r2, [sp, #36]
	ldr	r3, [r8]
	cmp	r2, r3
	beq	.L2578
	bl	__stack_chk_fail
.L2578:
	add	sp, sp, #44
	@ sp needed
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L2587:
	.align	2
.L2586:
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
	ldr	r7, .L2602
	cmp	r2, r3
	blt	.L2589
	uxth	r0, r0
	mov	r0, r0, asl #1
	ldrh	r10, [r5, r0]
	cmp	r10, #0
	beq	.L2589
	ldr	r3, [r4, #32]
	cmp	r3, #0
	bne	.L2589
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
	bcc	.L2590
	mov	r0, r4
	bl	ftl_map_blk_alloc_new_blk
.L2590:
	ldr	r5, .L2602+4
	mov	fp, #0
.L2591:
	ldrh	r3, [r4, #6]
	uxth	r6, fp
	cmp	r3, r6
	bls	.L2601
	ldr	r2, [r8, r6, asl #2]
	add	r3, r8, r6, asl #2
	cmp	r10, r2, lsr #10
	bne	.L2592
	ldr	r2, [r5, #-2060]
	mov	r1, #1
	ldr	r9, [r5, #-2040]
	ldr	r0, .L2602+8
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
	bne	.L2593
.L2595:
	mov	r2, #0
	str	r2, [r3]
	ldr	r3, .L2602+4
	ldr	r0, .L2602+12
	ldrh	r2, [r9, #8]
	ldr	r1, [r3, #216]
	bl	printk
	mov	r3, #1
	str	r3, [r7, #4052]
	b	.L2594
.L2593:
	ldrh	r1, [r9, #8]
	cmp	r1, r6
	bne	.L2595
	ldrh	r0, [r9]
	ldrh	r2, [r4, #4]
	cmp	r0, r2
	bne	.L2595
	mov	r0, r4
	ldr	r2, [r5, #220]
	bl	FtlMapWritePage
.L2592:
	add	fp, fp, #1
	b	.L2591
.L2601:
	mov	r0, r10
	mov	r1, #1
	bl	FtlFreeSysBlkQueueIn
	mov	r3, #0
	str	r3, [r4, #32]
.L2589:
	movw	r3, #1874
	ldrh	r2, [r4, #2]
	ldrh	r3, [r7, r3]
	cmp	r2, r3
	bcc	.L2594
	mov	r0, r4
	bl	ftl_map_blk_alloc_new_blk
.L2594:
	mov	r0, #0
	add	sp, sp, #12
	@ sp needed
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L2603:
	.align	2
.L2602:
	.word	.LANCHOR0
	.word	.LANCHOR2
	.word	.LANCHOR2+212
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
	ldr	r6, .L2616
	ldr	r5, [r0, #12]
	ldr	r7, [r0, #24]
	ldr	r10, [r6, #4052]
	cmp	r10, #0
	bne	.L2605
	ldrh	r3, [r0]
	movw	r2, #65535
	mov	r4, r0
	cmp	r3, r2
	bne	.L2606
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
	b	.L2605
.L2606:
	mov	r3, r3, asl #1
	mov	r1, #255
	ldrh	r9, [r5, r3]
	ldr	r5, .L2616+4
	ldrh	r3, [r0, #2]
	orr	r3, r3, r9, asl #10
	str	r3, [r5, #216]
	ldr	r3, [r5, #-2064]
	ldr	r8, [r5, #-2040]
	str	r3, [r5, #220]
	ldr	r3, [r0, #28]
	str	r8, [r5, #224]
	str	r3, [r8, #4]
	ldr	r3, .L2616+8
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
.L2607:
	ldrh	r0, [r4, #6]
	uxth	r2, r1
	cmp	r0, r2
	bls	.L2615
	ldr	r0, [r7, r2, asl #2]
	cmp	r9, r0, lsr #10
	bne	.L2608
	add	r3, r3, #1
	ldr	r0, [r5, #-2064]
	uxth	r3, r3
	str	r2, [r0, r3, asl #3]
	ldr	r0, [r7, r2, asl #2]
	ldr	r2, [r5, #-2064]
	add	r2, r2, r3, asl #3
	str	r0, [r2, #4]
.L2608:
	add	r1, r1, #1
	b	.L2607
.L2615:
	ldrb	r3, [r6]	@ zero_extendqisi2
	cmp	r3, #0
	beq	.L2610
	ldr	r2, .L2616
	movw	r3, #1882
	ldr	r0, [r5, #220]
	ldrh	r1, [r2, r3]
	bl	js_hash
	str	r0, [r8, #12]
.L2610:
	mov	r1, #1
	mov	r3, #0
	ldr	r0, .L2616+12
	mov	r2, r1
	bl	FlashProgPages
	ldrh	r3, [r4, #2]
	mov	r0, r4
	add	r3, r3, #1
	strh	r3, [r4, #2]	@ movhi
	bl	ftl_map_blk_gc
.L2605:
	mov	r0, #0
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, pc}
.L2617:
	.align	2
.L2616:
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
	ldr	r6, .L2637
	mov	r8, r1
	mov	r4, #0
	str	r2, [sp]
	mov	r5, r6
.L2619:
	ldr	r3, [r6, #-3864]
	ldr	r9, .L2637
	add	r3, r3, #1
	str	r3, [r6, #-3864]
	ldr	r3, .L2637+4
	ldrh	r2, [r10, #2]
	ldrh	r3, [r3]
	sub	r3, r3, #1
	cmp	r2, r3
	bge	.L2620
	ldrh	r2, [r10]
	movw	r3, #65535
	cmp	r2, r3
	bne	.L2621
.L2620:
	mov	r0, r10
	bl	Ftl_write_map_blk_to_last_page
.L2621:
	ldr	r3, .L2637+8
	ldr	r1, [r3, #4052]
	mov	fp, r3
	str	r3, [sp, #4]
	cmp	r1, #0
	bne	.L2622
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
	beq	.L2623
	ldr	r3, .L2637+12
	ldr	r0, [r6, #220]
	str	r2, [sp, #4]
	ldrh	r1, [r3]
	bl	js_hash
	ldr	r2, [sp, #4]
	str	r0, [r2, #12]
.L2623:
	mov	r1, #1
	ldr	r0, .L2637+16
	mov	r2, r1
	mov	r3, r1
	bl	FlashProgPages
	ldrh	r3, [r10, #2]
	add	r3, r3, #1
	uxth	r3, r3
	strh	r3, [r10, #2]	@ movhi
	ldr	r2, [r5, #212]
	cmn	r2, #1
	bne	.L2624
	ldr	r0, .L2637+20
	add	r4, r4, #1
	ldr	r1, [r6, #216]
	bl	printk
	ldrh	r3, [r10, #2]
	uxth	r4, r4
	cmp	r3, #2
	ldrls	r3, .L2637+4
	ldrlsh	r3, [r3]
	subls	r3, r3, #1
	strlsh	r3, [r10, #2]	@ movhi
	cmp	r4, #3
	bls	.L2619
	ldr	r0, .L2637+24
	mov	r2, r4
	ldr	r1, [r9, #216]
	bl	printk
	mov	r3, #1
	str	r3, [fp, #4052]
	b	.L2622
.L2624:
	cmp	r2, #0
	strneh	r7, [r10, #40]	@ movhi
	cmp	r2, #256
	cmpne	r3, #1
	beq	.L2628
	ldr	r3, [r10, #36]
	cmp	r3, #0
	beq	.L2629
.L2628:
	mov	r3, #0
	str	r3, [r10, #36]
	b	.L2619
.L2629:
	ldr	r2, [r9, #216]
	ldr	r3, [r10, #24]
	str	r2, [r3, r8, asl #2]
.L2622:
	mov	r0, #0
	add	sp, sp, #12
	@ sp needed
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L2638:
	.align	2
.L2637:
	.word	.LANCHOR2
	.word	.LANCHOR0+1874
	.word	.LANCHOR0
	.word	.LANCHOR0+1882
	.word	.LANCHOR2+212
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
	ldr	r5, .L2641
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
.L2642:
	.align	2
.L2641:
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
	ldr	r5, .L2686
	str	r3, [sp, #4]
	mov	r2, r10, asl #2
	ldrh	r3, [r0, #8]
	ldr	r9, [r0, #12]
	mov	r0, r8
	ldr	fp, .L2686+4
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
.L2644:
	uxth	r2, r6
	ldr	r1, [sp]
	sxth	r3, r2
	cmp	r3, r1
	bge	.L2662
	ldr	r1, [sp]
	sub	r1, r1, #1
	cmp	r3, r1
	mov	r1, r3, asl #1
	bne	.L2645
	ldrh	r0, [r9, r1]
	mov	r6, #0
	str	r3, [sp, #12]
	add	r3, r9, r1
	mov	r1, #1
	str	r2, [sp, #8]
	str	r3, [sp]
	bl	FtlGetLastWrittenPage
	ldr	fp, .L2686
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
.L2646:
	sxth	r3, r6
	cmp	r3, r9
	bge	.L2662
	ldr	r2, [sp]
	mov	r1, #1
	ldr	r0, .L2686+8
	ldrh	r2, [r2]
	orr	r3, r3, r2, asl #10
	mov	r2, r1
	str	r3, [r5, #216]
	bl	FlashReadPages
	ldr	r3, .L2686+12
	ldrb	r3, [r3]	@ zero_extendqisi2
	cmp	r3, #0
	beq	.L2647
	ldr	r3, [fp, #224]
	ldr	r3, [r3, #12]
	cmp	r3, #0
	beq	.L2647
	ldr	r2, .L2686+16
	ldr	r0, [fp, #220]
	str	r3, [sp, #4]
	ldrh	r1, [r2]
	bl	js_hash
	ldr	r3, [sp, #4]
	cmp	r3, r0
	mvnne	r3, #0
	strne	r3, [fp, #212]
.L2647:
	ldr	r3, [fp, #212]
	cmn	r3, #1
	beq	.L2648
	ldrh	r3, [r7, #8]
	cmp	r3, r10
	bcs	.L2648
	ldrh	r2, [r4, #4]
	ldrh	r1, [r7]
	cmp	r1, r2
	ldreq	r2, [fp, #216]
	streq	r2, [r8, r3, asl #2]
.L2648:
	add	r6, r6, #1
	b	.L2646
.L2662:
	mov	r0, r4
	bl	ftl_free_no_use_map_blk
	ldr	r1, .L2686+12
	movw	r3, #1874
	ldrh	r2, [r4, #2]
	ldrh	r3, [r1, r3]
	cmp	r2, r3
	bne	.L2651
	mov	r0, r4
	bl	ftl_map_blk_alloc_new_blk
	b	.L2651
.L2645:
	ldr	r3, [r5, #-2064]
	ldr	r0, .L2686+8
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
	beq	.L2664
	ldrh	r2, [r7]
	ldrh	r3, [r4, #4]
	cmp	r2, r3
	bne	.L2664
	ldrh	r2, [r7, #8]
	movw	r3, #64245
	cmp	r2, r3
	bne	.L2664
	mov	r1, #0
	mov	ip, #8
	mov	lr, #4
.L2653:
	ldr	r0, .L2686+4
	uxth	r3, r1
	sxth	r2, r3
	ldrh	r0, [r0]
	sub	r0, r0, #1
	cmp	r2, r0
	bge	.L2656
	ldr	r0, [r5, #-2064]
	add	r1, r1, #1
	ldr	r2, [r0, r2, asl #3]
	uxth	r2, r2
	cmp	r2, r10
	smlabbcc	r3, r3, ip, lr
	ldrcc	r3, [r0, r3]
	strcc	r3, [r8, r2, asl #2]
	b	.L2653
.L2664:
	ldr	ip, .L2686
	mov	r3, #0
.L2684:
	ldrh	r1, [fp]
	sxth	r2, r3
	cmp	r2, r1
	bge	.L2656
	str	r3, [sp, #16]
	ldr	r3, [sp, #8]
	ldr	r0, .L2686+8
	str	ip, [sp, #12]
	ldrh	r1, [r3]
	orr	r2, r2, r1, asl #10
	mov	r1, #1
	str	r2, [ip, #216]
	mov	r2, r1
	bl	FlashReadPages
	ldr	r3, .L2686+12
	ldrb	r2, [r3]	@ zero_extendqisi2
	cmp	r2, #0
	ldr	ip, [sp, #12]
	ldr	r3, [sp, #16]
	beq	.L2657
	ldr	r2, [ip, #224]
	ldr	r2, [r2, #12]
	cmp	r2, #0
	beq	.L2657
	ldr	r1, .L2686+16
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
.L2657:
	ldr	r2, .L2686
	ldr	r2, [r2, #212]
	cmn	r2, #1
	beq	.L2658
	ldrh	r2, [r7, #8]
	cmp	r2, r10
	bcs	.L2658
	ldrh	r1, [r4, #4]
	ldrh	r0, [r7]
	cmp	r0, r1
	ldreq	r1, [ip, #216]
	streq	r1, [r8, r2, asl #2]
.L2658:
	add	r3, r3, #1
	b	.L2684
.L2656:
	add	r6, r6, #1
	b	.L2644
.L2651:
	mov	r0, r4
	bl	ftl_map_blk_gc
	mov	r0, r4
	bl	ftl_map_blk_gc
	mov	r0, #0
	add	sp, sp, #28
	@ sp needed
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L2687:
	.align	2
.L2686:
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
	ldr	r3, .L2690
	ldr	r0, .L2690+4
	ldrh	r1, [r3, r2]
	movw	r2, #262
	strh	r1, [r0, r2]	@ movhi
	add	r2, r0, #256
	ldr	r1, .L2690+8
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
.L2691:
	.align	2
.L2690:
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
	ldr	r0, .L2694
	bl	FtlMapTblRecovery
	mov	r0, #0
	ldmfd	sp!, {r3, pc}
.L2695:
	.align	2
.L2694:
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
	ldr	r3, .L2708
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
	ldr	r2, .L2708+4
	ldrh	r5, [r2, r1]
	add	r1, r0, r4
	movw	r0, #1868
	ldrh	r2, [r2, r0]
	cmp	r1, r2
	mvnhi	r0, #0
	bhi	.L2697
	ldr	fp, .L2708+8
	mov	r5, r6, lsr r5
	mov	r3, r5, asl #2
	str	r3, [sp, #8]
	mov	r7, fp
	mov	r3, #0
	str	r3, [sp, #4]
.L2698:
	cmp	r4, #0
	beq	.L2707
	ldr	r2, [sp, #8]
	mov	r0, r6
	ldr	r3, [fp, #-1996]
	ldr	ip, [r3, r2]
	ldr	r3, .L2708+12
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
	beq	.L2700
	ldr	r2, [r7, #-2056]
	mov	r1, #1
	add	r0, sp, #24
	str	ip, [sp, #28]
	str	r2, [sp, #32]
	mov	r2, #0
	str	r2, [sp, #36]
	mov	r2, r1
	bl	FlashReadPages
	b	.L2701
.L2700:
	ldr	r2, .L2708+16
	ldr	r0, [r7, #-2056]
	ldrh	r2, [r2]
	bl	ftl_memset
.L2701:
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
	ldr	r0, .L2708+20
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
	b	.L2698
.L2707:
	ldr	r0, [sp, #4]
.L2697:
	ldr	r3, [sp, #12]
	ldr	r2, [sp, #60]
	ldr	r3, [r3]
	cmp	r2, r3
	beq	.L2704
	bl	__stack_chk_fail
.L2704:
	add	sp, sp, #68
	@ sp needed
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L2709:
	.align	2
.L2708:
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
	ldr	r3, .L2712
	ldr	r2, .L2712+4
	ldr	r1, [r3, #-3748]
	cmp	r1, r2
	bxne	lr
	ldr	r2, .L2712+8
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
	ldr	r2, .L2712+12
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
	ldr	r2, .L2712+16
	b	FtlVendorPartWrite
.L2713:
	.align	2
.L2712:
	.word	.LANCHOR2
	.word	1179929683
	.word	1342177367
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
	ldr	r3, .L2722
	ldr	r2, .L2722+4
	ldr	r3, [r3, #1796]
	cmp	r3, #0
	moveq	r3, #32
	beq	.L2715
	ldr	r3, [r2, #-3824]
	cmp	r3, #39
	movhi	r3, #32
	movls	r3, #4
.L2715:
	ldr	ip, .L2722+8
	ldrh	r1, [ip]
	cmp	r1, #31
	addls	r1, r1, #1
	strlsh	r1, [ip]	@ movhi
	movls	r3, #1
	cmp	r0, #0
	bne	.L2717
	ldr	r1, [r2, #-2020]
	ldr	r0, [r1, #20]
	ldr	r1, [r1, #16]
	add	r3, r3, r0
	cmp	r1, r3
	bcc	.L2718
.L2717:
	ldr	r3, [r2, #-2020]
	mov	r0, #64
	ldr	r1, [r3, #16]
	str	r1, [r3, #20]
	ldr	r1, .L2722+12
	str	r1, [r3]
	ldr	r3, .L2722+16
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
.L2718:
	mov	r0, #0
	ldmfd	sp!, {r3, pc}
.L2723:
	.align	2
.L2722:
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
	ldr	r3, .L2737
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
	ldr	r2, .L2737+4
	ldrh	r4, [r2, r1]
	add	r1, r0, r5
	movw	r0, #1868
	ldrh	r2, [r2, r0]
	cmp	r1, r2
	mvnhi	r0, #0
	bhi	.L2725
	ldr	r10, .L2737+8
	mov	r4, r6, lsr r4
	mov	r3, r4, asl #2
	str	r3, [sp, #4]
	mov	fp, r10
	mov	r3, #0
	str	r3, [sp]
.L2726:
	cmp	r5, #0
	beq	.L2736
	ldr	r3, [r10, #-1996]
	mov	r0, r6
	ldr	r2, [sp, #4]
	ldr	r8, [r3, r2]
	ldr	r3, .L2737+12
	ldrh	r9, [r3]
	mov	r1, r9
	bl	__aeabi_uidivmod
	rsb	r3, r1, r9
	str	r1, [sp, #8]
	uxth	r9, r3
	cmp	r9, r5
	uxthhi	r9, r5
	cmp	r8, #0
	beq	.L2728
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
	bne	.L2730
	mov	r1, r4
	mov	r2, r8
	ldr	r0, .L2737+16
	bl	printk
	ldr	r0, .L2737+20
	mov	r1, r4
	ldr	r2, [r10, #-2056]
	bl	FtlMapWritePage
.L2730:
	ldrh	r8, [sp, #8]
	mov	r0, r7
	ldr	r1, [fp, #-2056]
	mov	r2, r9, asl #9
	add	r1, r1, r8, asl #9
	bl	ftl_memcpy
	b	.L2731
.L2728:
	mov	r0, r7
	mov	r1, r8
	mov	r2, r9, asl #9
	bl	ftl_memset
.L2731:
	ldr	r3, [sp, #4]
	add	r4, r4, #1
	rsb	r5, r9, r5
	add	r6, r6, r9
	add	r3, r3, #4
	add	r7, r7, r9, asl #9
	str	r3, [sp, #4]
	b	.L2726
.L2736:
	ldr	r0, [sp]
.L2725:
	ldr	r3, [sp, #12]
	ldr	r2, [sp, #52]
	ldr	r3, [r3]
	cmp	r2, r3
	beq	.L2733
	bl	__stack_chk_fail
.L2733:
	add	sp, sp, #60
	@ sp needed
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L2738:
	.align	2
.L2737:
	.word	__stack_chk_guard
	.word	.LANCHOR0
	.word	.LANCHOR2
	.word	.LANCHOR0+1878
	.word	.LC133
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
	ldr	r4, .L2742
	sub	r5, r4, #2016
	ldr	r2, [r4, #-2020]
	ldrh	r1, [r5, #-12]
	bl	FtlVendorPartRead
	ldr	r3, [r4, #-2020]
	ldr	r2, [r3]
	ldr	r3, .L2742+4
	cmp	r2, r3
	beq	.L2740
	ldr	r1, .L2742+8
	ldr	r0, .L2742+12
	bl	printk
	ldrh	r2, [r5, #-12]
	ldr	r0, [r4, #-2020]
	mov	r1, #0
	mov	r2, r2, asl #9
	bl	ftl_memset
.L2740:
	mov	r0, #0
	ldmfd	sp!, {r3, r4, r5, pc}
.L2743:
	.align	2
.L2742:
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
	ldr	r4, .L2749
	mov	r1, #1
	ldr	r2, .L2749+4
	bl	FtlVendorPartRead
	ldr	r5, .L2749+8
	ldr	r3, [r4, #-3748]
	cmp	r3, r5
	beq	.L2745
	ldr	r0, .L2749+4
	mov	r1, #0
	mov	r2, #512
	bl	ftl_memset
	str	r5, [r4, #-3748]
.L2745:
	ldr	r2, [r4, #-3748]
	ldr	r3, .L2749
	cmp	r2, r5
	ldr	r5, .L2749+12
	bne	.L2746
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
.L2746:
	mov	r3, #0
	ldr	r2, [r4, #-3680]
	str	r3, [r4, #-2104]
	ldr	r3, .L2749+16
	cmp	r2, r3
	bne	.L2747
	ldr	r0, .L2749+20
	mov	r3, #1
	ldr	r1, .L2749+24
	str	r3, [r5, #1796]
	bl	printk
.L2747:
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
.L2750:
	.align	2
.L2749:
	.word	.LANCHOR2
	.word	.LANCHOR2-3748
	.word	1179929683
	.word	.LANCHOR0
	.word	305432421
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
	ldr	r3, [r0, #36]
	ldrh	r5, [r0, #6]
	cmp	r3, #0
	ldr	r1, [r0, #24]
	ldmeqfd	sp!, {r4, r5, r6, pc}
	ldr	r2, .L2758
	mov	r3, #0
	str	r3, [r0, #36]
	ldr	r3, [r2, #4052]
	cmp	r3, #0
	ldmnefd	sp!, {r4, r5, r6, pc}
	ldr	r3, .L2758+4
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
	beq	.L2755
	mov	r1, #1
	add	r0, r3, #212
	mov	r2, r1
	bl	FlashReadPages
	b	.L2756
.L2755:
	movw	r3, #1882
	mov	r1, #255
	ldrh	r2, [r2, r3]
	bl	ftl_memset
.L2756:
	mov	r0, r6
	mov	r1, r5
	ldr	r2, [r4, #220]
	ldmfd	sp!, {r4, r5, r6, lr}
	b	FtlMapWritePage
.L2759:
	.align	2
.L2758:
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
	ldr	r9, .L2776
	mov	r10, r2
	ldr	r8, .L2776+4
	mov	r5, r0
	mov	r7, r1
	ldr	r3, [r9]
	str	r3, [sp, #36]
	ldr	r3, .L2776+8
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
.L2761:
	cmp	r6, r4
	ble	.L2775
	mov	r1, #1
	add	r3, r6, fp
	mov	r0, sp
	mov	r2, r1
	mov	r3, r3, asl #10
	str	r3, [sp, #4]
	bl	FlashReadPages
	ldr	r3, [sp]
	cmn	r3, #1
	beq	.L2762
	ldr	r3, [r8, #180]
	ldrh	r2, [r3]
	movw	r3, #61664
	cmp	r2, r3
	bne	.L2762
	cmp	r5, #0
	moveq	r0, r5
	beq	.L2763
	cmp	r7, #0
	ldreq	ip, .L2776+4
	moveq	lr, #1
	beq	.L2764
.L2766:
	ldr	r1, [r8, #148]
	mov	r2, r10
	mov	r0, r5
	bl	ftl_memcpy
	mov	r2, #4
	ldr	r0, .L2776+12
	mov	r1, r5
	mov	r3, r2
	bl	rknand_print_hex
	mov	r0, #0
	b	.L2763
.L2764:
	ldr	r3, [r8, #168]
	uxth	r4, r7
	add	r7, r7, #1
	cmp	r4, r3
	bcs	.L2766
	ldr	r2, [ip, #148]
	mov	r1, r4, lsr #5
	and	r0, r4, #31
	ldr	r3, [r2, r1, asl #2]
	orr	r0, r3, lr, asl r0
	str	r0, [r2, r1, asl #2]
	b	.L2764
.L2762:
	sub	r6, r6, #1
	uxth	r6, r6
	b	.L2761
.L2775:
	mvn	r0, #0
.L2763:
	ldr	r2, [sp, #36]
	ldr	r3, [r9]
	cmp	r2, r3
	beq	.L2768
	bl	__stack_chk_fail
.L2768:
	add	sp, sp, #44
	@ sp needed
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L2777:
	.align	2
.L2776:
	.word	__stack_chk_guard
	.word	.LANCHOR2
	.word	.LANCHOR0
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
	ldr	r3, .L2789
	stmfd	sp!, {r4, r5, r6, r7, r8, lr}
	.save {r4, r5, r6, r7, r8, lr}
	mov	r5, r0
	ldr	r3, [r3, #692]
	ldr	r6, .L2789+4
	ldrb	r4, [r3, #13]	@ zero_extendqisi2
	ldrh	r3, [r3, #14]
	ldr	r0, [r6, #176]
	smulbb	r4, r4, r3
	uxth	r4, r4
	add	r2, r4, #7
	mov	r2, r2, asr #3
	bl	FlashReadFacBbtData
	cmn	r0, #1
	bne	.L2779
.L2783:
	mov	r3, #0
	b	.L2780
.L2779:
	mov	lr, r4, lsr #4
	mov	ip, #0
	sub	r4, r4, #1
	mov	r3, ip
	mov	r7, #1
.L2781:
	uxth	r0, ip
	cmp	r0, r4
	bge	.L2780
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
	bcc	.L2781
	b	.L2783
.L2780:
	mov	r3, r3, asl #1
	mvn	r2, #0
	mov	r0, #0
	strh	r2, [r5, r3]	@ movhi
	ldmfd	sp!, {r4, r5, r6, r7, r8, pc}
.L2790:
	.align	2
.L2789:
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
	ldr	r6, .L2815
	ldr	r5, [r6, #4052]
	cmp	r5, #0
	bne	.L2792
	bl	FtlBbtMemInit
	ldr	r8, .L2815+4
	bl	FtlLoadFactoryBbt
	ldr	r7, .L2815+8
	sub	r9, r8, #18
	add	r4, r6, #1936
	mov	r10, r7
.L2793:
	ldr	r3, .L2815+12
	ldrh	r3, [r3]
	cmp	r5, r3
	bcs	.L2812
	ldrh	r2, [r9, #2]!
	movw	r3, #65535
	ldr	r0, [r7, #-2064]
	ldr	fp, [r7, #-2040]
	cmp	r2, r3
	ldr	r3, .L2815+16
	str	r0, [r7, #220]
	str	fp, [r7, #224]
	beq	.L2794
	ldrh	ip, [r3]
	mov	r1, #1
	ldr	r0, .L2815+20
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
	b	.L2795
.L2794:
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
.L2796:
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
	beq	.L2796
.L2813:
	ldrh	r3, [sp, #4]
	mov	r1, #0
	mov	r2, #16
	ldr	r0, [r10, #-2040]
	strh	r3, [r9]	@ movhi
	bl	ftl_memset
	ldr	r3, .L2815+24
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
	ldr	r3, .L2815+28
	str	ip, [sp, #12]
	ldrh	r2, [r3]
	mov	r2, r2, asl #2
	bl	ftl_memcpy
	mov	r1, #1
	mov	r2, r1
	ldr	r0, .L2815+20
	bl	FlashEraseBlocks
	mov	r1, #1
	mov	r3, r1
	ldr	r0, .L2815+20
	mov	r2, r1
	bl	FlashProgPages
	ldr	r3, [r10, #212]
	cmn	r3, #1
	ldr	ip, [sp, #12]
	bne	.L2795
	uxth	r0, ip
	bl	FtlBbmMapBadBlock
	b	.L2796
.L2795:
	uxth	r0, ip
	add	r5, r5, #1
	bl	FtlBbmMapBadBlock
	add	r8, r8, #4
	b	.L2793
.L2812:
	ldr	r7, .L2815+32
	mov	r5, #0
.L2800:
	ldrh	r3, [r7]
	uxth	r0, r5
	add	r5, r5, #1
	cmp	r3, r0
	bls	.L2814
	bl	FtlBbmMapBadBlock
	b	.L2800
.L2814:
	ldrh	r5, [r4, #12]
	movw	r8, #65535
	ldr	r7, .L2815+36
	sub	r5, r5, #1
	uxth	r5, r5
.L2802:
	ldrh	r3, [r4, #12]
	sub	r3, r3, #48
	cmp	r5, r3
	ble	.L2806
	mov	r0, r5
	bl	FtlBbmIsBadBlock
	cmp	r0, #1
	beq	.L2803
	mov	r0, r5
	bl	FlashTestBlk
	cmp	r0, #0
	beq	.L2804
	mov	r0, r5
	bl	FtlBbmMapBadBlock
	b	.L2803
.L2804:
	ldrh	r3, [r7]
	cmp	r3, r8
	streqh	r5, [r7]	@ movhi
.L2805:
	ldrne	r3, .L2815+36
	strneh	r5, [r3, #4]	@ movhi
	bne	.L2806
.L2803:
	sub	r5, r5, #1
	uxth	r5, r5
	b	.L2802
.L2806:
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
.L2792:
	mov	r0, #0
	add	sp, sp, #20
	@ sp needed
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L2816:
	.align	2
.L2815:
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
	ldr	r7, .L2834
	ldrh	r10, [r7, r3]
	ldr	r3, [r7, #1932]
	cmp	r0, r3
	bcs	.L2818
	add	r10, r10, #7
	ldr	fp, .L2834+4
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
	b	.L2819
.L2818:
	cmp	r2, #0
	mvn	r0, #0
	streq	r0, [r1]
	b	.L2820
.L2824:
	add	r4, r4, #1
	mla	r0, r1, r4, r8
	ldrh	r0, [r0, #-12]
	cmp	r0, r5
	beq	.L2821
.L2819:
	uxth	r7, r4
	cmp	r7, r2
	bcc	.L2824
	bl	select_l2p_ram_region
	mov	r4, #12
	movw	r3, #65535
	mul	r4, r4, r0
	mov	r7, r0
	add	r2, r8, r4
	ldrh	r1, [r8, r4]
	cmp	r1, r3
	bne	.L2833
.L2825:
	ldr	r3, [fp, #-1992]
	ldr	r8, .L2834+4
	ldr	fp, [r3, r5, asl #2]
	cmp	fp, #0
	bne	.L2826
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
.L2821:
	ldr	r2, [sp]
	mvn	r3, #0
	mov	r6, #12
	bic	r10, r2, r3, asl r10
	ldr	r3, [sp, #4]
	ldr	r2, .L2834+4
	cmp	r3, #0
	uxth	r10, r10
	bne	.L2822
	ldr	r3, [r2, #-3896]
	mla	r6, r6, r7, r3
	ldr	r3, [r6, #8]
	ldr	r3, [r3, r10, asl #2]
	str	r3, [r9]
	b	.L2823
.L2822:
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
.L2823:
	ldr	r2, [r2, #-3896]
	mov	r3, #12
	mov	r0, #0
	mla	r7, r3, r7, r2
	ldr	r3, [r7, #4]
	cmn	r3, #1
	addne	r3, r3, #1
	strne	r3, [r7, #4]
	b	.L2820
.L2833:
	ldr	r3, [r2, #4]
	cmp	r3, #0
	bge	.L2825
	bl	flush_l2p_region
	b	.L2825
.L2826:
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
	beq	.L2827
	mov	r1, r5
	mov	r2, fp
	ldr	r0, .L2834+8
	bl	printk
	mov	r2, #4
	mov	r3, r2
	ldr	r0, .L2834+12
	ldr	r1, [r8, #224]
	bl	rknand_print_hex
	movw	r3, #1908
	ldrh	r3, [r6, r3]
	mov	r2, #4
	ldr	r0, .L2834+16
	ldr	r1, [r8, #-1992]
	bl	rknand_print_hex
	mov	r3, #1
	str	r3, [r6, #4052]
	b	.L2828
.L2827:
	ldr	r3, [r8, #212]
	cmp	r3, #256
	bne	.L2828
	mov	r1, r5
	mov	r2, fp
	ldr	r0, .L2834+20
	bl	printk
	ldr	r3, [r8, #-3896]
	ldr	r0, .L2834+24
	mov	r1, r5
	add	r3, r3, r4
	ldr	r2, [r3, #8]
	bl	FtlMapWritePage
.L2828:
	ldr	r3, .L2834+4
	mov	r1, #0
	ldr	r3, [r3, #-3896]
	add	r2, r3, r4
	str	r1, [r2, #4]
	strh	r5, [r3, r4]	@ movhi
	b	.L2821
.L2820:
	add	sp, sp, #12
	@ sp needed
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L2835:
	.align	2
.L2834:
	.word	.LANCHOR0
	.word	.LANCHOR2
	.word	.LC137
	.word	.LC100
	.word	.LC138
	.word	.LC139
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
	ldr	r3, .L2856
	stmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.save {r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.pad #60
	sub	sp, sp, #60
	ldr	r2, [r3]
	ldr	r8, .L2856+4
	str	r3, [sp]
	str	r2, [sp, #52]
	ldr	r2, [r8, #4052]
	cmp	r2, #0
	bne	.L2836
	ldr	r7, .L2856+8
	ldr	r4, .L2856+12
	ldrh	r5, [r7, #4]
	cmp	r5, #0
	beq	.L2838
	ldrb	r3, [r4, #-4032]	@ zero_extendqisi2
	cmp	r3, #0
	bne	.L2838
	add	r3, r8, #1872
	ldrb	r2, [r4, #-4033]	@ zero_extendqisi2
	ldrh	r3, [r3]
	mul	r3, r3, r2
	cmp	r5, r3
	beq	.L2838
	ldrb	r10, [r4, #-4030]	@ zero_extendqisi2
	movw	r3, #1804
	ldr	r6, [r8, #1932]
	cmp	r10, #0
	ldrh	r9, [r8, r3]
	bne	.L2836
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
	beq	.L2840
	mov	r1, #1
	mov	r2, r10
	add	r0, sp, #16
	bl	FlashReadPages
	b	.L2841
.L2840:
	movw	r3, #1882
	mov	r1, #255
	ldrh	r2, [r8, r3]
	bl	ftl_memset
.L2841:
	ldr	r3, .L2856+16
	mov	r8, #0
	ldr	r10, .L2856+12
	mov	r9, r9, asl #2
	ldr	ip, .L2856+8
	mov	fp, r8
	strh	r3, [r5]	@ movhi
.L2842:
	cmp	r8, r9
	beq	.L2843
	ldrh	r3, [r7, #4]
	cmp	r3, #0
	beq	.L2843
	ldr	r3, [sp, #20]
	add	r8, r8, #1
	str	r6, [r5, #8]
	ldr	r0, .L2856+8
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
	b	.L2842
.L2843:
	mov	r3, #1
	b	.L2855
.L2838:
	mov	r3, #0
.L2855:
	strb	r3, [r4, #-4030]
.L2836:
	ldr	r3, [sp]
	ldr	r2, [sp, #52]
	ldr	r3, [r3]
	cmp	r2, r3
	beq	.L2847
	bl	__stack_chk_fail
.L2847:
	add	sp, sp, #60
	@ sp needed
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L2857:
	.align	2
.L2856:
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
	ldr	r5, .L2864
	mov	r7, #12
	ldr	r6, .L2864+4
.L2859:
	ldrh	r3, [r5]
	uxth	r0, r4
	cmp	r3, r0
	bls	.L2863
	ldr	r3, [r6, #-3896]
	mla	r3, r7, r0, r3
	ldr	r3, [r3, #4]
	cmp	r3, #0
	bge	.L2860
	bl	flush_l2p_region
.L2860:
	add	r4, r4, #1
	b	.L2859
.L2863:
	mov	r0, #0
	ldmfd	sp!, {r3, r4, r5, r6, r7, pc}
.L2865:
	.align	2
.L2864:
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
	ldr	r1, .L2893
	stmfd	sp!, {r3, r4, r5, r6, r7, lr}
	.save {r3, r4, r5, r6, r7, lr}
	ldr	r3, [r1, #4052]
	ldrh	r5, [r0]
	cmp	r3, #0
	bne	.L2867
	movw	r3, #65535
	cmp	r5, r3
	mov	r7, r1
	mov	r4, r0
	ldr	r6, .L2893+4
	beq	.L2868
	ldr	r2, [r6, #-4060]
	mov	r3, r5, asl #1
	mov	r0, r5
	ldrh	r3, [r2, r3]
	cmp	r3, #0
	beq	.L2869
	bl	INSERT_DATA_LIST
	b	.L2868
.L2869:
	bl	INSERT_FREE_LIST
.L2868:
	mov	r3, #0
	strb	r3, [r4, #8]
	ldr	r3, .L2893+8
	cmp	r4, r3
	beq	.L2870
	ldr	r2, .L2893
	add	r3, r2, #1824
	ldrh	r3, [r3]
	cmp	r3, #1
	beq	.L2870
	ldrb	r2, [r2, #140]	@ zero_extendqisi2
	cmp	r2, #0
	beq	.L2871
.L2870:
	mov	r3, #1
	strb	r3, [r4, #8]
	b	.L2872
.L2871:
	ldr	r2, .L2893+12
	cmp	r4, r2
	bne	.L2872
	cmp	r3, #3
	beq	.L2874
	ldr	r3, .L2893+4
	ldr	r3, [r3, #-3752]
	cmp	r3, #1
	bne	.L2875
.L2874:
	mov	r3, #1
	strb	r3, [r6, #-4032]
.L2875:
	ldr	r3, [r7, #1796]
	cmp	r3, #0
	beq	.L2872
	ldr	r3, [r6, #-3824]
	cmp	r3, #39
	ldrls	r3, .L2893+4
	movls	r2, #1
	strlsb	r2, [r3, #-4032]
.L2872:
	ldr	r2, .L2893+4
	movw	r1, #65535
	sub	r3, r2, #1920
	ldrh	r0, [r3]
	mov	r6, r3
	cmp	r0, r1
	beq	.L2877
	cmp	r5, r0
	bne	.L2878
	ldr	r2, [r2, #-4060]
	mov	r3, r0, asl #1
	ldrh	r3, [r2, r3]
	cmp	r3, #0
	beq	.L2879
.L2878:
	bl	update_vpc_list
.L2879:
	mvn	r3, #0
	strh	r3, [r6]	@ movhi
.L2877:
	mov	r0, r4
	bl	allocate_data_superblock
	bl	l2p_flush
	mov	r0, #0
	bl	FtlEctTblFlush
	bl	FtlVpcTblFlush
.L2867:
	mov	r0, #0
	ldmfd	sp!, {r3, r4, r5, r6, r7, pc}
.L2894:
	.align	2
.L2893:
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
	ldr	r6, .L2917
	ldr	r1, .L2917+4
	ldr	r0, .L2917+8
	ldr	r3, [r6]
	ldr	r7, .L2917+12
	ldr	r5, .L2917+16
	str	r3, [sp, #4]
	bl	printk
	ldr	r0, .L2917+16
	mov	r1, #0
	mov	r2, #8192
	bl	memset
.L2896:
	ldr	r3, [r7, #1932]
	cmp	r4, r3
	bcs	.L2915
	mov	r0, r4
	mov	r1, sp
	mov	r2, #0
	bl	log2phys
	ldr	r0, [sp]
	cmn	r0, #1
	beq	.L2897
	ubfx	r0, r0, #10, #16
	bl	P2V_block_in_plane
	mov	r0, r0, asl #1
	ldrh	r3, [r5, r0]
	add	r3, r3, #1
	strh	r3, [r5, r0]	@ movhi
.L2897:
	add	r4, r4, #1
	b	.L2896
.L2915:
	ldr	r5, .L2917+20
	mov	r4, #0
	ldr	r8, .L2917+24
	add	r9, r5, #300
.L2899:
	ldrh	r3, [r8]
	uxth	r1, r4
	ldr	r7, .L2917+20
	cmp	r3, r1
	bls	.L2916
	ldr	r2, [r5, #-4060]
	mov	r3, r1, asl #1
	ldrh	r2, [r2, r3]
	ldrh	r3, [r9, r3]
	cmp	r2, r3
	beq	.L2900
	ldr	r0, .L2917+28
	bl	printk
.L2900:
	add	r4, r4, #1
	b	.L2899
.L2916:
	ldr	r4, [r7, #-4048]
	cmp	r4, #0
	beq	.L2895
	sub	r3, r7, #4032
	add	r9, r7, #300
	mov	r5, #0
	mov	r10, #6
	ldrh	r8, [r3, #-12]
	ldr	r3, [r7, #-4068]
	rsb	r4, r3, r4
	ldr	r3, .L2917+32
	mov	r4, r4, asr #1
	mul	r4, r3, r4
	uxth	r4, r4
.L2903:
	uxth	r3, r5
	cmp	r3, r8
	bcs	.L2895
	ldr	r2, [r7, #-4060]
	mov	r3, r4, asl #1
	ldrh	r2, [r2, r3]
	cmp	r2, #0
	beq	.L2904
	ldr	r0, .L2917+36
	mov	r1, r4
	ldrh	r3, [r9, r3]
	bl	printk
.L2904:
	mul	r4, r10, r4
	ldr	r3, [r7, #-4068]
	add	r5, r5, #1
	ldrh	r4, [r3, r4]
	movw	r3, #65535
	cmp	r4, r3
	bne	.L2903
.L2895:
	ldr	r2, [sp, #4]
	ldr	r3, [r6]
	cmp	r2, r3
	beq	.L2907
	bl	__stack_chk_fail
.L2907:
	add	sp, sp, #8
	@ sp needed
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, pc}
.L2918:
	.align	2
.L2917:
	.word	__stack_chk_guard
	.word	.LANCHOR3+72
	.word	.LC108
	.word	.LANCHOR0
	.word	.LANCHOR2+300
	.word	.LANCHOR2
	.word	.LANCHOR0+1812
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
	.pad #32
	sub	sp, sp, #32
	ldr	r6, .L2930
	mov	r1, #0
	ldr	r0, .L2930+4
	mov	r4, #0
	ldr	r8, .L2930+8
	ldr	r3, [r6]
	ldr	r5, .L2930+12
	str	r3, [sp, #28]
	bl	printk
.L2920:
	ldr	r3, [r8, #1932]
	cmp	r4, r3
	bcs	.L2929
	mov	r0, r4
	add	r1, sp, #24
	mov	r2, #0
	bl	log2phys
	ubfx	r3, r4, #0, #11
	cmp	r3, #0
	bne	.L2921
	ldr	r0, .L2930+16
	mov	r1, r4
	ldr	r2, [sp, #24]
	bl	printk
.L2921:
	ldr	r3, [sp, #24]
	cmn	r3, #1
	beq	.L2923
	str	r3, [r5, #216]
	mov	r2, #0
	ldr	r3, [r5, #-2064]
	mov	r1, #1
	ldr	r7, [r5, #-2040]
	ldr	r0, .L2930+20
	str	r3, [r5, #220]
	str	r2, [r5, #212]
	str	r4, [r5, #228]
	str	r7, [r5, #224]
	bl	FlashReadPages
	ldr	r3, [r5, #212]
	ldr	r2, .L2930+12
	cmn	r3, #1
	cmpne	r3, #256
	beq	.L2924
	ldr	r3, [r7, #8]
	cmp	r3, r4
	beq	.L2923
.L2924:
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
	ldr	r0, .L2930+24
	str	r1, [sp, #16]
	mov	r1, r4
	ldr	r2, [r2, #216]
	ldr	r3, [r3]
	bl	printk
.L2923:
	add	r4, r4, #1
	b	.L2920
.L2929:
	ldr	r2, [sp, #28]
	ldr	r3, [r6]
	cmp	r2, r3
	beq	.L2927
	bl	__stack_chk_fail
.L2927:
	add	sp, sp, #32
	@ sp needed
	ldmfd	sp!, {r4, r5, r6, r7, r8, pc}
.L2931:
	.align	2
.L2930:
	.word	__stack_chk_guard
	.word	.LC142
	.word	.LANCHOR0
	.word	.LANCHOR2
	.word	.LC143
	.word	.LANCHOR2+212
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
	ldr	r6, .L2942
	ldr	r4, [r6, #-4060]
	mov	r7, r0, asl #1
	ldrh	r3, [r4, r7]
	cmp	r3, #0
	addne	r3, r3, #1
	strneh	r3, [r4, r7]	@ movhi
	bne	.L2934
	ldr	r4, [r6, #-4048]
	cmp	r4, #0
	beq	.L2934
	ldr	r1, [r6, #-4068]
	sub	r2, r6, #4032
	ldr	lr, .L2942+4
	movw	r9, #65535
	rsb	r4, r1, r4
	ldrh	ip, [r2, #-12]
	mov	r8, r2
	mov	r4, r4, asr #1
	mul	r4, lr, r4
	mov	lr, #6
	uxth	r4, r4
.L2935:
	uxth	r2, r3
	cmp	r2, ip
	bcs	.L2934
	cmp	r4, r0
	bne	.L2936
	mov	r1, r4
	ldr	r0, .L2942+8
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
	b	.L2934
.L2936:
	mul	r4, lr, r4
	add	r3, r3, #1
	ldrh	r4, [r1, r4]
	cmp	r4, r9
	bne	.L2935
.L2934:
	mov	r0, r5
	add	r1, sp, #4
	mov	r2, #1
	bl	log2phys
	add	sp, sp, #12
	@ sp needed
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, pc}
.L2943:
	.align	2
.L2942:
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
	ldr	r3, .L3104
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
	beq	.L2946
	ldrh	r3, [r0, #2]
	str	r3, [sp, #8]
	ldrb	r3, [r0, #6]	@ zero_extendqisi2
	ldr	r1, [sp, #8]
	str	r3, [sp, #28]
	ldr	r3, .L3104+4
	ldrh	r3, [r3]
	cmp	r3, r1
	mov	r3, #0
	streqh	r3, [r0, #4]	@ movhi
	ldrneh	r0, [r0, #16]
	beq	.L3096
.L2948:
	cmp	r0, r2
	add	r3, r3, #1
	uxtheq	r1, r3
	addeq	r1, r4, r1, asl #1
	ldreqh	r0, [r1, #16]
	beq	.L2948
.L3099:
	ldrb	r1, [r4, #8]	@ zero_extendqisi2
	cmp	r1, #1
	bne	.L2950
	bl	FtlGetLastWrittenPage
	cmn	r0, #1
	mov	r5, r0
	beq	.L2951
	ldr	r3, .L3104+8
	ldrb	r2, [r3, #140]	@ zero_extendqisi2
	cmp	r2, #0
	addeq	r3, r3, r0, asl #1
	ldreqh	r7, [r3, #144]
	beq	.L3022
	b	.L3089
.L2950:
	mov	r1, #0
	bl	FtlGetLastWrittenPage
	cmn	r0, #1
	mov	r5, r0
	beq	.L2951
.L3089:
	mov	r7, r5
.L3022:
	ldr	r3, .L3104+8
	movw	r2, #1804
	movw	r8, #65535
	mov	r9, #36
	ldrh	r2, [r3, r2]
	str	r2, [sp, #4]
	ldr	r2, .L3104+12
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
	b	.L2953
.L2951:
	mov	r3, #0
	strh	r3, [r4, #2]	@ movhi
.L3096:
	strb	r3, [r4, #6]
	b	.L2946
.L2955:
	ldrh	r3, [r0, #2]!
	cmp	r3, r8
	beq	.L2954
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
.L2954:
	add	r2, r2, #1
.L2953:
	ldr	r1, [sp, #4]
	uxth	r3, r2
	cmp	r3, r1
	bcc	.L2955
	ldrb	r3, [r4, #8]	@ zero_extendqisi2
	cmp	r3, #1
	movne	r3, #0
	bne	.L3090
	ldr	r3, .L3104+8
	ldrb	r1, [r3, #140]	@ zero_extendqisi2
	adds	r3, r1, #0
	movne	r3, #1
.L3090:
	ldr	r8, .L3104+12
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
.L2957:
	uxth	r9, fp
	cmp	r9, r6
	bcs	.L2964
	mov	r1, #36
	ldr	r0, [r8, #-2076]
	mul	r1, r1, fp
	add	ip, r0, r1
	ldr	r1, [r0, r1]
	cmp	r1, #0
	bne	.L2958
	ldr	ip, [ip, #12]
	ldr	r10, [ip, #4]
	cmn	r10, #1
	beq	.L2959
	mov	r0, r10
	ldr	r1, [r8, #-3844]
	str	ip, [sp, #4]
	bl	ftl_cmp_data_ver
	ldr	ip, [sp, #4]
	cmp	r0, #0
	addne	r10, r10, #1
	strne	r10, [r8, #-3844]
.L2959:
	ldr	r1, [ip]
	cmn	r1, #1
	bne	.L2960
.L2964:
	cmp	r9, r6
	ldr	r6, .L3104+12
	bne	.L3086
	add	r5, r5, #1
	uxth	r3, r5
	str	r3, [sp, #4]
	ldr	r3, [r6, #-2076]
	ldr	r0, [r3, #4]
	b	.L3091
.L2958:
	ldr	r1, [ip, #4]
	ldr	r0, .L3104+16
	bl	printk
	uxth	r3, r7
	ldrh	r1, [r4]
	str	r3, [sp, #16]
	ldr	r3, .L3104+20
	strh	r1, [r3]	@ movhi
.L2960:
	add	fp, fp, #1
	b	.L2957
.L3086:
	uxth	r3, r5
	str	r3, [sp, #4]
	ldr	r3, [sp, #12]
	mov	r2, #36
	ldr	r3, [r3, #-2076]
	mla	r9, r2, r9, r3
	ldr	r0, [r9, #4]
.L3091:
	ubfx	r0, r0, #10, #16
	bl	P2V_plane
	ldrb	r3, [r4, #8]	@ zero_extendqisi2
	cmp	r3, #1
	str	r0, [sp, #12]
	bne	.L2966
	ldr	r2, .L3104+8
	ldrb	r1, [r2, #140]	@ zero_extendqisi2
	cmp	r1, #0
	ldreq	r1, [sp, #4]
	addeq	r2, r2, r1, asl #1
	ldreqh	r2, [r2, #144]
	streq	r2, [sp, #4]
.L2966:
	ldr	r2, .L3104+4
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
	beq	.L3097
	clz	r3, r3
	ldr	r2, [sp, #36]
	ldr	r1, [sp, #16]
	mov	r3, r3, lsr #5
	sub	r10, r2, #1
	movw	r2, #65535
	cmp	r1, r2
	orrne	r3, r3, #1
	cmp	r3, #0
	beq	.L2970
	ldr	r3, [r6, #-1908]
	uxth	fp, r7
	ldr	r8, .L3104+12
	mvn	r9, #0
	cmn	r3, #1
	mov	r7, r9
	ldreq	r3, .L3104+12
	streq	r10, [r3, #-1908]
	ldr	r3, [r6, #-1908]
	str	r3, [sp, #16]
	ldr	r3, [sp, #8]
	add	r3, r3, #7
	cmp	fp, r3
	subgt	r5, fp, #7
	ldrle	r5, [sp, #8]
	uxthgt	r5, r5
.L2973:
	cmp	r5, fp
	bhi	.L2986
	ldr	r3, .L3104+24
	mov	r0, #36
	ldr	lr, [r8, #-2076]
	ldr	r1, [sp, #20]
	ldrh	r3, [r3]
	str	r3, [sp, #28]
	mov	r3, #0
	mov	r6, r3
.L2987:
	ldr	ip, [sp, #28]
	uxth	r2, r3
	cmp	r2, ip
	bcs	.L3100
	ldrh	r2, [r1, #2]!
	movw	ip, #65535
	add	r3, r3, #1
	cmp	r2, ip
	orrne	r2, r5, r2, asl #10
	mlane	ip, r0, r6, lr
	addne	r6, r6, #1
	uxthne	r6, r6
	strne	r2, [ip, #4]
	b	.L2987
.L3100:
	mov	r1, r6
	ldr	r2, [sp, #24]
	ldr	r0, [r8, #-2076]
	bl	FlashReadPages
	ldr	r3, .L3104+8
	mov	r2, #36
	movw	r1, #65535
	ldrb	ip, [r3, #140]	@ zero_extendqisi2
	ldr	r3, [r8, #-2076]
	mla	r6, r2, r6, r3
	ldr	r2, .L3104+28
	add	r2, r2, r5, asl #1
.L2976:
	cmp	r3, r6
	beq	.L3101
	ldr	r0, [r3]
	cmp	r0, #0
	bne	.L2977
	ldr	r0, [r3, #12]
	ldrh	lr, [r0]
	cmp	lr, r1
	beq	.L2978
	ldr	r0, [r0, #4]
	cmn	r0, #1
	beq	.L2978
	cmn	r9, #1
	ldr	r7, [r8, #-1908]
	str	r0, [r8, #-1908]
	bne	.L2978
	ldrh	r0, [r2]
	cmp	r0, r1
	bne	.L2979
	cmp	ip, #0
	beq	.L2978
.L2979:
	cmp	r7, r10
	mvneq	r9, #0
	movne	r9, r7
	b	.L2978
.L2977:
	ldr	r3, .L3104+20
	ldrh	r2, [r4]
	strh	r2, [r3]	@ movhi
	ldrb	r3, [r4, #8]	@ zero_extendqisi2
	cmp	r3, #0
	bne	.L2970
	ldr	r3, .L3104+12
	mov	r5, r5, asl #1
	sub	r2, r3, #3136
	ldrh	r1, [r2, r5]
	movw	r2, #65535
	cmp	r1, r2
	bne	.L2981
	cmn	r9, #1
	strne	r9, [r3, #-1908]
	bne	.L2970
	ldr	r2, [sp, #16]
	cmp	r2, r10
	bne	.L3092
	ldr	r2, [r3, #-1908]
	b	.L3098
.L2981:
	cmp	r7, r10
	beq	.L2984
	cmn	r7, #1
	strne	r7, [r3, #-1908]
	b	.L2970
.L2984:
	ldr	r2, [r3, #-1908]
	cmp	r2, r10
	beq	.L2970
.L3098:
	sub	r2, r2, #1
	b	.L3092
.L2978:
	add	r3, r3, #36
	b	.L2976
.L3101:
	add	r5, r5, #1
	uxth	r5, r5
	b	.L2973
.L2986:
	ldr	r3, .L3104+12
	mvn	r2, #0
.L3092:
	str	r2, [r3, #-1908]
.L2970:
	ldr	r2, .L3104+32
	movw	r3, #310
	ldr	fp, [sp, #8]
	mov	r1, #1
	ldr	r5, .L3104+12
	strh	r1, [r2, r3]	@ movhi
.L2988:
	ldr	r3, .L3104+24
	movw	r1, #65535
	ldr	r9, [r5, #-2076]
	mov	r0, #36
	ldr	r7, [sp, #20]
	mov	r6, #0
	ldrh	r8, [r3]
	ldr	r3, .L3104+8
	str	r6, [sp, #16]
	ldrb	r3, [r3, #140]	@ zero_extendqisi2
	str	r3, [sp, #28]
.L2989:
	uxth	r3, r6
	cmp	r3, r8
	bcs	.L3102
	ldrh	r3, [r7, #2]!
	cmp	r3, r1
	beq	.L2990
	ldr	r2, [sp, #16]
	orr	r3, fp, r3, asl #10
	mla	r2, r0, r2, r9
	str	r3, [r2, #4]
	ldrb	ip, [r4, #8]	@ zero_extendqisi2
	cmp	ip, #1
	bne	.L2991
	ldr	ip, [sp, #28]
	cmp	ip, #0
	orrne	r3, r3, #-2147483648
	strne	r3, [r2, #4]
.L2991:
	ldr	r3, [sp, #16]
	add	lr, r3, #1
	uxth	r3, lr
	str	r3, [sp, #16]
.L2990:
	add	r6, r6, #1
	b	.L2989
.L3102:
	ldr	r0, [r5, #-2076]
	ldr	r1, [sp, #16]
	ldr	r2, [sp, #24]
	bl	FlashReadPages
	mov	r3, #0
.L3095:
	str	r3, [sp, #28]
	ldr	r2, [sp, #16]
	ldrh	r3, [sp, #28]
	cmp	r3, r2
	bcs	.L3103
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
	bcc	.L2994
	ldr	r3, [sp, #32]
	ldr	r2, [sp, #8]
	cmp	r0, r3
	movcs	r3, #0
	movcc	r3, #1
	cmp	fp, r2
	movne	r3, #0
	cmp	r3, #0
	bne	.L2994
	ldr	r3, [sp, #12]
	ldr	r2, [sp, #4]
	cmp	r0, r3
	cmpeq	fp, r2
	beq	.L2995
	ldr	r3, [r9, r6]
	cmn	r3, #1
	beq	.L2996
	ldr	r8, [r8, #12]
	movw	r3, #61589
	ldrh	r2, [r8]
	cmp	r2, r3
	ldrneh	r0, [r4]
	bne	.L3093
	ldr	r10, [r8, #4]
	cmn	r10, #1
	beq	.L2998
	ldr	r1, [r5, #-3844]
	mov	r0, r10
	bl	ftl_cmp_data_ver
	cmp	r0, #0
	addne	r3, r10, #1
	strne	r3, [r5, #-3844]
.L2998:
	ldr	r7, [r8, #8]
	add	r1, sp, #52
	ldr	r3, [r8, #12]
	mov	r2, #0
	mov	r0, r7
	str	r3, [sp, #48]
	bl	log2phys
	ldr	r1, [r5, #-1908]
	cmn	r1, #1
	beq	.L2999
	mov	r0, r10
	bl	ftl_cmp_data_ver
	cmp	r0, #0
	beq	.L2999
	ldr	r3, [sp, #48]
	cmn	r3, #1
	beq	.L3000
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
	bne	.L3001
	b	.L3002
.L3000:
	ldr	r3, [sp, #56]
	ldr	r2, [sp, #52]
	cmp	r2, r3
	bne	.L2994
	mov	r0, r7
	add	r1, sp, #48
	mov	r2, #1
	bl	log2phys
	b	.L2994
.L3001:
	ldr	r8, [r9, #8]
	cmp	r8, r7
	bne	.L3002
	ldr	r0, [r5, #-1908]
	ldr	r1, [sp, #36]
	str	r2, [sp, #44]
	str	ip, [sp, #40]
	bl	ftl_cmp_data_ver
	cmp	r0, #0
	ldr	ip, [sp, #40]
	ldr	r2, [sp, #44]
	beq	.L3002
	ldr	r3, [sp, #52]
	ldr	r1, [sp, #56]
	cmp	r3, r1
	beq	.L3007
	ldr	r1, [sp, #48]
	cmp	r3, r1
	beq	.L3002
	cmn	r3, #1
	streq	r3, [r2, r6]
	beq	.L3006
	str	r3, [ip, #4]
	mov	r0, ip
	mov	r1, #1
	mov	r2, #0
	ldr	r9, [ip, #12]
	bl	FlashReadPages
.L3006:
	ldr	r3, [r5, #-2076]
	ldr	r3, [r3, r6]
	cmn	r3, #1
	beq	.L3007
	ldr	r6, [r9, #4]
	ldr	r0, [r5, #-1908]
	mov	r1, r6
	bl	ftl_cmp_data_ver
	cmp	r0, #0
	beq	.L3007
	ldr	r0, [sp, #36]
	mov	r1, r6
	bl	ftl_cmp_data_ver
	cmp	r0, #0
	beq	.L3002
.L3007:
	mov	r0, r8
	ldr	r1, [sp, #48]
	bl	FtlReUsePrevPpa
.L3002:
	mvn	r3, #0
	str	r3, [sp, #48]
	b	.L3009
.L2999:
	ldr	r3, [sp, #56]
	ldr	r2, [sp, #52]
	cmp	r2, r3
	beq	.L3009
	ldr	r3, [sp, #48]
	cmn	r3, #1
	beq	.L3011
	ldr	r2, .L3104+8
	ubfx	r3, r3, #10, #21
	ldr	r2, [r2, #1820]
	cmp	r3, r2
	bcs	.L2994
.L3011:
	mov	r0, r7
	add	r1, sp, #56
	mov	r2, #1
	bl	log2phys
	ldr	r6, [sp, #52]
	cmn	r6, #1
	beq	.L3009
	ldr	r3, [sp, #48]
	cmp	r6, r3
	beq	.L3009
	ubfx	r0, r6, #10, #16
	bl	P2V_block_in_plane
	ldr	r3, .L3104+36
	ldrh	r2, [r3]
	cmp	r2, r0
	beq	.L3013
	ldrh	r2, [r3, #48]
	cmp	r2, r0
	beq	.L3013
	ldrh	r3, [r3, #96]
	cmp	r3, r0
	bne	.L3009
.L3013:
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
	beq	.L3009
	mov	r0, r10
	bl	ftl_cmp_data_ver
	cmp	r0, #0
	bne	.L3009
	mov	r0, r7
	add	r1, sp, #52
	mov	r2, #1
	bl	log2phys
.L3009:
	ldr	r0, [sp, #48]
	cmn	r0, #1
	beq	.L2994
	ubfx	r0, r0, #10, #16
	bl	P2V_block_in_plane
	ldr	r2, [r5, #-4060]
	mov	r3, r0, asl #1
	mov	r1, r0
	ldrh	r3, [r2, r3]
	cmp	r3, #0
	beq	.L3014
.L3093:
	bl	decrement_vpc_count
	b	.L2994
.L3014:
	ldr	r0, .L3104+40
	bl	printk
	b	.L2994
.L2996:
	ldrh	r2, [r4]
	mov	r1, r7
	ldr	r3, .L3104+20
	ldr	r0, .L3104+44
	strh	r2, [r3]	@ movhi
	mov	r2, r10
	bl	printk
	ldr	r3, .L3104+32
	ldr	r3, [r3, #312]
	cmp	r3, #31
	bhi	.L3015
	ldr	r2, .L3104+32
	ldr	r1, [sp, #56]
	add	r2, r2, r3, asl #2
	add	r3, r3, #1
	str	r1, [r2, #316]
	ldr	r2, .L3104+32
	str	r3, [r2, #312]
.L3015:
	ldrh	r0, [r4]
	bl	decrement_vpc_count
	ldr	r3, [r5, #-1908]
	cmn	r3, #1
	beq	.L3094
	cmp	r3, r10
	bls	.L2994
.L3094:
	str	r10, [r5, #-1908]
.L2994:
	ldr	r3, [sp, #28]
	add	r3, r3, #1
	b	.L3095
.L3103:
	ldrb	r3, [r4, #8]	@ zero_extendqisi2
	add	fp, fp, #1
	cmp	r3, #1
	uxth	fp, fp
	bne	.L3018
	ldr	r3, .L3104+8
	ldrb	r3, [r3, #140]	@ zero_extendqisi2
	cmp	r3, #0
	beq	.L3018
	ldr	r3, .L3104+48
	ldr	r2, [sp, #4]
	ldrh	r3, [r3]
	cmp	r2, fp
	cmpeq	r3, fp
	beq	.L2995
.L3018:
	ldr	r2, .L3104+8
	add	r3, r2, #1872
	ldrh	r3, [r3]
	cmp	fp, r3
	bne	.L2988
	movw	r1, #1804
	movw	r0, #65535
	ldrh	r1, [r2, r1]
	mov	r3, #0
	strh	fp, [r4, #2]	@ movhi
	strh	r3, [r4, #4]	@ movhi
.L3019:
	uxth	r2, r3
	cmp	r2, r1
	bcs	.L2946
	ldr	lr, [sp, #20]
	add	r3, r3, #1
	ldrh	ip, [lr, #2]!
	cmp	ip, r0
	str	lr, [sp, #20]
	beq	.L3019
	strb	r2, [r4, #6]
	b	.L2946
.L2995:
	ldrb	r3, [sp, #12]	@ zero_extendqisi2
	mov	r0, r4
	ldr	r1, [sp, #4]
	strb	r3, [r4, #6]
	ldrh	r3, [sp, #4]
	strh	r3, [r4, #2]	@ movhi
.L3097:
	ldr	r2, [sp, #12]
	bl	ftl_sb_update_avl_pages
.L2946:
	ldr	r3, .L3104
	mov	r0, #0
	ldr	r2, [sp, #60]
	ldr	r3, [r3]
	cmp	r2, r3
	beq	.L3023
	bl	__stack_chk_fail
.L3023:
	add	sp, sp, #68
	@ sp needed
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L3105:
	.align	2
.L3104:
	.word	__stack_chk_guard
	.word	.LANCHOR0+1872
	.word	.LANCHOR0
	.word	.LANCHOR2
	.word	.LC145
	.word	.LANCHOR4+308
	.word	.LANCHOR0+1804
	.word	.LANCHOR2-3136
	.word	.LANCHOR4
	.word	.LANCHOR2-4040
	.word	.LC146
	.word	.LC147
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
	ldr	r7, .L3123
	ldr	r5, .L3123+4
	ldr	r1, .L3123+8
	ldr	r3, [r7]
	ldr	r0, .L3123+12
	ldr	r6, .L3123+16
	str	r3, [sp, #4]
	bl	printk
	movw	r3, #1814
	mov	r1, #0
	ldrh	r2, [r5, r3]
	ldr	r0, [r6, #-2016]
	mov	r2, r2, asl #1
	bl	ftl_memset
.L3107:
	ldr	r3, [r5, #1932]
	cmp	r4, r3
	bcs	.L3121
	mov	r0, r4
	mov	r1, sp
	mov	r2, #0
	bl	log2phys
	ldr	r0, [sp]
	cmn	r0, #1
	beq	.L3108
	ubfx	r0, r0, #10, #16
	bl	P2V_block_in_plane
	ldr	r2, [r6, #-2016]
	mov	r0, r0, asl #1
	ldrh	r3, [r2, r0]
	add	r3, r3, #1
	strh	r3, [r2, r0]	@ movhi
.L3108:
	add	r4, r4, #1
	b	.L3107
.L3121:
	ldr	r10, .L3123+20
	mov	r8, #0
	ldr	r9, .L3123+16
.L3110:
	ldrh	r3, [r10]
	uxth	r4, r8
	cmp	r3, r4
	bls	.L3122
	ldr	r3, [r6, #-4060]
	mov	r5, r4, asl #1
	movw	r1, #65535
	ldrh	r2, [r3, r5]
	ldr	r3, [r6, #-2016]
	ldrh	r3, [r3, r5]
	cmp	r2, r3
	cmpne	r2, r1
	beq	.L3112
	ldr	r0, .L3123+24
	mov	r1, r4
	bl	printk
	ldr	r3, .L3123+28
	ldrh	r2, [r3]
	cmp	r2, r4
	beq	.L3112
	ldrh	r2, [r3, #96]
	cmp	r2, r4
	beq	.L3112
	ldrh	r3, [r3, #48]
	cmp	r3, r4
	beq	.L3112
	ldr	r3, [r9, #-4060]
	ldrh	r2, [r3, r5]
	cmp	r2, #0
	ldr	r2, [r9, #-2016]
	ldrh	r2, [r2, r5]
	strh	r2, [r3, r5]	@ movhi
	beq	.L3112
	mov	r0, r4
	bl	update_vpc_list
.L3112:
	add	r8, r8, #1
	b	.L3110
.L3122:
	bl	l2p_flush
	bl	FtlVpcTblFlush
	ldr	r2, [sp, #4]
	ldr	r3, [r7]
	cmp	r2, r3
	beq	.L3116
	bl	__stack_chk_fail
.L3116:
	add	sp, sp, #8
	@ sp needed
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, pc}
.L3124:
	.align	2
.L3123:
	.word	__stack_chk_guard
	.word	.LANCHOR0
	.word	.LANCHOR3+84
	.word	.LC108
	.word	.LANCHOR2
	.word	.LANCHOR0+1812
	.word	.LC148
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
	ldr	r3, .L3173
	stmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.save {r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.pad #76
	sub	sp, sp, #76
	ldr	r3, [r3]
	mov	r4, r0
	ldr	r2, .L3173+4
	str	r1, [sp, #12]
	str	r3, [sp, #68]
	movw	r3, #3288
	ldrh	r5, [r2, r3]
	movw	r3, #65535
	cmp	r5, r3
	beq	.L3157
	cmp	r5, #0
	bne	.L3126
	b	.L3127
.L3157:
	mov	r5, #0
.L3126:
	ldr	r3, .L3173+8
	ldr	r2, [sp, #12]
	ldrh	r3, [r3]
	cmp	r2, r3
	bne	.L3128
.L3127:
	bl	FtlGcPageVarInit
.L3128:
	ldr	r6, .L3173+12
	mvn	r3, #0
	str	r3, [sp, #8]
	mov	r3, #0
	str	r3, [sp]
.L3129:
	ldrh	r2, [r4]
	mov	r3, #0
	strb	r3, [r4, #8]
	movw	r3, #65535
	cmp	r2, r3
	beq	.L3130
.L3131:
.L3153:
	ldr	r3, .L3173+16
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
	ldr	r3, .L3173+20
	ldrh	ip, [r3]
	ldrh	r8, [r3, #2]
.L3132:
	ldr	r1, [sp, #4]
	uxth	r3, r2
	cmp	r3, r1
	bcs	.L3171
	ldrh	r3, [r0, #2]!
	cmp	r3, r9
	beq	.L3133
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
.L3133:
	add	r2, r2, #1
	b	.L3132
.L3171:
	ldr	r0, [r6, #-2076]
	mov	r1, r7
	mov	r2, #0
	ldr	fp, .L3173+24
	bl	FlashReadPages
	mov	r10, #0
.L3135:
	uxth	r3, r10
	cmp	r3, r7
	bcs	.L3172
	mov	r9, #36
	ldr	r2, [r6, #-2076]
	mul	r9, r9, r10
	ldr	ip, .L3173+12
	add	r3, r2, r9
	ldr	r8, [r2, r9]
	ldr	r1, [r3, #4]
	cmp	r8, #0
	ldr	r3, [r3, #12]
	str	r1, [sp, #4]
	bne	.L3136
	ldrh	r1, [r3]
	movw	r2, #65535
	cmp	r1, r2
	bne	.L3137
.L3139:
	ldr	r2, .L3173+12
	ldrh	r3, [r4]
	ldr	r2, [r2, #-4060]
	b	.L3170
.L3137:
	ldr	r0, [r3, #8]
	ldr	r2, [fp, #1932]
	cmp	r0, r2
	bhi	.L3139
	ldrb	r2, [fp]	@ zero_extendqisi2
	cmp	r2, #0
	beq	.L3141
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
	beq	.L3141
	str	r2, [sp, #36]
	mov	r1, #1
	ldr	r2, [r6, #-2044]
	add	r0, sp, #32
	str	r2, [sp, #40]
	ldr	r2, [r6, #-2036]
	str	r2, [sp, #44]
	mov	r2, r8
	bl	FlashReadPages
	ldr	r2, .L3173+28
	ldr	r1, [r6, #-2076]
	ldrh	r2, [r2]
	add	r9, r1, r9
	mov	r2, r2, asl #7
	ldr	r1, [sp, #40]
	ldr	r3, [sp, #16]
	b	.L3143
.L3144:
	add	r8, r8, #1
.L3143:
	cmp	r8, r2
	beq	.L3141
	ldr	r0, [r9, #8]
	ldr	ip, [r0, r8, asl #2]
	ldr	r0, [r1, r8, asl #2]
	cmp	ip, r0
	beq	.L3144
	ldr	r2, [sp, #36]
	ldr	r0, .L3173+32
	ldrh	r1, [r4]
	bl	printk
	ldrh	r3, [r4]
	ldr	r2, [r6, #-4060]
.L3170:
	mov	r3, r3, asl #1
	mov	r1, #0
	strh	r1, [r2, r3]	@ movhi
	ldrh	r0, [r4]
	bl	INSERT_FREE_LIST
	ldr	r2, .L3173+36
	mvn	r3, #0
	strh	r3, [r4]	@ movhi
	strh	r3, [r2]	@ movhi
	b	.L3169
.L3141:
	ldr	r0, [r3, #12]
	add	r10, r10, #1
	ldr	r1, [sp, #4]
	ldr	r2, [r3, #8]
	bl	FtlGcUpdatePage
	b	.L3135
.L3136:
	ldr	r2, [sp, #4]
	ldr	r0, .L3173+40
	ldrh	r1, [r4]
	str	ip, [sp, #16]
	bl	printk
	ldr	r3, .L3173+24
	ldrh	r5, [r4]
	ldr	r2, [r3, #1796]
	cmp	r2, #0
	ldr	ip, [sp, #16]
	bne	.L3147
	ldrb	r2, [r3, #140]	@ zero_extendqisi2
	cmp	r2, #0
	beq	.L3148
.L3147:
	ldr	r2, [r3, #4060]
	mov	r3, r5, asl #1
	ldrh	r3, [r2, r3]
	cmp	r3, #159
	bls	.L3149
.L3148:
	ldr	r3, [ip, #-2076]
	ldr	r3, [r3, r9]
	cmn	r3, #1
	bne	.L3150
.L3149:
	ldr	r3, [ip, #-2076]
	add	r9, r3, r9
	ldr	r3, [r9, #4]
	str	r3, [sp, #8]
.L3150:
	ldr	r3, .L3173+12
	mov	r5, r5, asl #1
	mov	r2, #0
	ldr	r3, [r3, #-4060]
	strh	r2, [r3, r5]	@ movhi
	ldrh	r0, [r4]
	bl	INSERT_FREE_LIST
	mvn	r3, #0
	strh	r3, [r4]	@ movhi
.L3169:
	bl	FtlGcPageVarInit
	mov	r5, #0
	b	.L3129
.L3172:
	ldr	r3, [sp]
	add	r5, r5, #1
	ldr	r2, [sp, #12]
	add	r3, r3, #1
	uxth	r5, r5
	cmp	r3, r2
	str	r3, [sp]
	ldr	r2, .L3173+8
	bcs	.L3152
.L3154:
	ldrh	r3, [r2]
	cmp	r3, r5
	bhi	.L3153
	b	.L3130
.L3152:
	ldr	r1, .L3173+44
	movw	r0, #65535
	ldrh	r3, [r1]
	cmp	r3, r0
	beq	.L3154
	ldr	r0, [sp]
	add	r3, r3, r0
	strh	r3, [r1]	@ movhi
	ldrh	r3, [r2]
	cmp	r3, r5
	bls	.L3154
	b	.L3155
.L3130:
	ldr	r2, .L3173+4
	movw	r3, #3288
	mvn	r1, #0
	strh	r5, [r4, #2]	@ movhi
	mov	r0, r4
	strh	r1, [r2, r3]	@ movhi
	mov	r2, #0
	mov	r1, r5
	strb	r2, [r4, #6]
	bl	ftl_sb_update_avl_pages
.L3155:
	ldr	r3, .L3173
	ldr	r2, [sp, #68]
	ldr	r0, [sp, #8]
	ldr	r3, [r3]
	cmp	r2, r3
	beq	.L3156
	bl	__stack_chk_fail
.L3156:
	add	sp, sp, #76
	@ sp needed
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L3174:
	.align	2
.L3173:
	.word	__stack_chk_guard
	.word	.LANCHOR1
	.word	.LANCHOR0+1872
	.word	.LANCHOR2
	.word	.LANCHOR0+1804
	.word	.LANCHOR0+1882
	.word	.LANCHOR0
	.word	.LANCHOR0+1878
	.word	.LC149
	.word	.LANCHOR2-3800
	.word	.LC150
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
	ldr	r5, .L3193
	ldr	r7, .L3193+4
	ldr	r4, .L3193+8
	ldr	r10, [r5, #-3668]
	mov	r6, r5
	ldr	r3, [r7]
	cmp	r10, #0
	str	r3, [sp, #44]
	beq	.L3176
	ldr	r2, [r5, #-3664]
	ldr	r3, [r4, #1932]
	cmp	r2, r3
	bcs	.L3177
	mov	r5, #2048
	mov	r8, r6
.L3182:
	ldr	r0, [r6, #-3664]
	ldr	r3, [r4, #1932]
	cmp	r0, r3
	bcs	.L3181
	mov	r2, #0
	add	r1, sp, #4
	bl	log2phys
	ldr	r3, [r8, #-3664]
	add	r3, r3, #1
	str	r3, [r8, #-3664]
	ldr	r2, [sp, #4]
	cmn	r2, #1
	beq	.L3180
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
	bne	.L3181
	ldr	r0, [sp, #4]
	ubfx	r0, r0, #10, #16
	bl	P2V_block_in_plane
	bl	FtlGcRefreshBlock
.L3181:
	mvn	r0, #0
	b	.L3184
.L3180:
	subs	r5, r5, #1
	bne	.L3182
	b	.L3181
.L3177:
	ldr	r3, [r5, #-3880]
	mov	r0, #0
	str	r0, [r5, #-3668]
	str	r0, [r5, #-3664]
	str	r3, [r5, #-3672]
	b	.L3184
.L3176:
	ldr	r1, [r5, #-3828]
	ldr	r9, [r5, #-3880]
	ldr	r3, [r4, #1932]
	movw	r4, #10000
	ldr	r8, [r5, #-3672]
	cmp	r1, r4
	add	r2, r9, #1048576
	ldr	fp, .L3193+12
	movhi	r4, #31
	movls	r4, #63
	cmp	r8, r2
	bhi	.L3186
	mov	r1, r1, lsr #10
	mov	r0, #1000
	mul	r0, r0, r3
	add	r1, r1, #1
	bl	__aeabi_uidiv
	add	r0, r0, r8
	cmp	r0, r9
	bcc	.L3186
	ldrh	r3, [fp, #28]
	ands	r0, r4, r3
	movne	r0, r10
	bne	.L3184
	ldr	r2, [r5, #-3648]
	cmp	r2, r3
	beq	.L3184
.L3186:
	ldrh	r3, [fp, #28]
	mov	r0, #0
	str	r9, [r6, #-3672]
	str	r0, [r6, #-3664]
	str	r3, [r6, #-3648]
	mov	r3, #1
	str	r3, [r6, #-3668]
.L3184:
	ldr	r2, [sp, #44]
	ldr	r3, [r7]
	cmp	r2, r3
	beq	.L3187
	bl	__stack_chk_fail
.L3187:
	add	sp, sp, #52
	@ sp needed
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L3194:
	.align	2
.L3193:
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
	ldr	r9, .L3235
	ldr	r2, .L3235+4
	add	r6, r9, #1872
	ldr	ip, [r9, #4052]
	ldr	r3, [r2]
	cmp	ip, #0
	ldrh	r1, [r6]
	str	r2, [sp, #4]
	str	r3, [sp, #20]
	bne	.L3233
	ldr	r5, .L3235+8
	mov	r4, r9
	movw	lr, #65535
	add	r8, r5, #3936
	ldrh	r9, [r5, #-8]
	cmp	r9, lr
	bne	.L3198
.L3207:
	ldrh	r2, [r5, #-8]
	movw	r3, #65535
	ldr	r9, .L3235+12
	mov	r7, #0
	cmp	r2, r3
	str	r7, [r8, #-1912]
	sub	r10, r9, #3936
	beq	.L3233
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
	b	.L3208
.L3198:
	cmp	r0, #0
	beq	.L3201
	ldr	r2, .L3235+16
	movw	r3, #3288
	ldrh	r0, [r2, r3]
	cmp	r0, lr
	beq	.L3202
.L3203:
	mov	r1, #2
	b	.L3201
.L3202:
	strh	ip, [r2, r3]	@ movhi
	sub	r3, r8, #4032
	ldrh	r3, [r3, #-12]
	cmp	r3, #17
	bhi	.L3203
.L3201:
	ldr	r0, .L3235+20
	bl	FtlGcScanTempBlk
	cmn	r0, #1
	str	r0, [sp, #16]
	beq	.L3204
	ldr	r2, [r4, #4060]
	mov	r9, r9, asl #1
	ldrh	r3, [r2, r9]
	cmp	r3, #4
	bls	.L3205
	sub	r3, r3, #5
	mov	r0, #1
	strh	r3, [r2, r9]	@ movhi
	bl	FtlEctTblFlush
.L3205:
	ldr	r3, [r8, #-1912]
	ldr	r2, .L3235+12
	cmp	r3, #0
	bne	.L3206
	ldr	r0, [sp, #16]
	ldr	r3, [r2, #-3652]
	ubfx	r0, r0, #10, #16
	add	r3, r3, #1
	str	r3, [r2, #-3652]
	bl	FtlBbmMapBadBlock
	bl	FtlBbmTblFlush
.L3206:
	mov	r3, #0
	str	r3, [r8, #-1912]
	b	.L3219
.L3204:
	ldr	r2, .L3235+16
	movw	r3, #3288
	ldrh	r2, [r2, r3]
	movw	r3, #65535
	cmp	r2, r3
	bne	.L3219
	b	.L3207
.L3211:
	ldr	r3, [fp, #4]
	cmp	r0, r3
	bne	.L3231
.L3210:
	add	r7, r7, #1
.L3208:
	ldrh	r3, [r6]
	uxth	r9, r7
	cmp	r3, r9
	bls	.L3234
	mov	r3, #12
	ldr	r2, [r4, #1932]
	mul	r9, r3, r9
	ldr	r3, [sp, #8]
	ldr	ip, [r3, #-3188]
	add	fp, ip, r9
	ldr	r0, [fp, #8]
	cmp	r0, r2
	bcs	.L3231
	mov	r2, #0
	add	r1, sp, #16
	str	ip, [sp, #12]
	bl	log2phys
	ldr	ip, [sp, #12]
	ldr	r0, [sp, #16]
	ldr	r2, [ip, r9]
	cmp	r0, r2
	bne	.L3211
	ubfx	r0, r0, #10, #16
	bl	P2V_block_in_plane
	add	r1, fp, #4
	mov	r2, #1
	mov	r9, r0
	ldr	r0, [fp, #8]
	bl	log2phys
	mov	r0, r9
	b	.L3232
.L3231:
	ldrh	r0, [r10]
.L3232:
	bl	decrement_vpc_count
	b	.L3210
.L3234:
	movw	r0, #65535
	bl	decrement_vpc_count
	ldrb	r3, [r4, #140]	@ zero_extendqisi2
	cmp	r3, #0
	beq	.L3213
	ldr	r0, .L3235+24
	ldrh	r1, [r5, #-8]
	bl	printk
.L3213:
	ldrh	r0, [r5, #-8]
	ldr	r2, [r8, #-4060]
	mov	r3, r0, asl #1
	ldrh	r3, [r2, r3]
	cmp	r3, #0
	beq	.L3214
	bl	INSERT_DATA_LIST
	b	.L3215
.L3214:
	bl	INSERT_FREE_LIST
.L3215:
	ldr	r7, .L3235+12
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
	beq	.L3216
	ldr	r1, [r7, #-3824]
	cmp	r1, #39
	bhi	.L3216
	cmp	r2, r3
	subcc	r7, r7, #3232
	movcc	r3, r3, asl #1
	strcch	r3, [r7, #-4]	@ movhi
	b	.L3233
.L3216:
	add	r1, r3, r3, asl #1
	cmp	r2, r1, asr #2
	ble	.L3233
	ldrb	r0, [r4, #140]	@ zero_extendqisi2
	ldr	r2, .L3235+28
	cmp	r0, #0
	moveq	r3, #20
	streqh	r3, [r2, #-4]	@ movhi
	beq	.L3197
	sub	r3, r3, #2
	strh	r3, [r2, #-4]	@ movhi
.L3233:
	mov	r0, #0
	b	.L3197
.L3219:
	mov	r0, #1
.L3197:
	ldr	r3, [sp, #4]
	ldr	r2, [sp, #20]
	ldr	r3, [r3]
	cmp	r2, r3
	beq	.L3218
	bl	__stack_chk_fail
.L3218:
	add	sp, sp, #28
	@ sp needed
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L3236:
	.align	2
.L3235:
	.word	.LANCHOR0
	.word	__stack_chk_guard
	.word	.LANCHOR2-3936
	.word	.LANCHOR2
	.word	.LANCHOR1
	.word	.LANCHOR2-3944
	.word	.LC151
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
	ldr	r5, .L3240
	ldr	r4, .L3240+4
	mov	r0, r5
	ldrh	r1, [r4]
	bl	FtlGcScanTempBlk
	ldrh	r2, [r5, #2]
	ldrh	r3, [r4]
	cmp	r2, r3
	ldmccfd	sp!, {r3, r4, r5, pc}
	ldr	r0, .L3240+8
	bl	FtlMapBlkWriteDumpData
	mov	r0, #0
	bl	FtlGcFreeTempBlock
	ldr	r3, .L3240+12
	mov	r2, #0
	str	r2, [r3, #-1912]
	ldmfd	sp!, {r3, r4, r5, pc}
.L3241:
	.align	2
.L3240:
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
	ldr	r4, .L3244
	ldr	r3, .L3244+4
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
.L3245:
	.align	2
.L3244:
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
	ldr	r7, .L3264
	mov	r2, #0
	ldr	r6, .L3264+4
	ldr	r4, .L3264+8
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
	bne	.L3247
.L3249:
	mvn	r8, #0
	b	.L3248
.L3247:
	bl	FtlLoadSysInfo
	subs	r8, r0, #0
	bne	.L3249
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
.L3250:
	cmp	r3, r1
	bge	.L3255
	mla	ip, r0, r3, r2
	ldr	ip, [ip, #4]
	cmp	ip, #0
	bge	.L3251
.L3255:
	ldr	r5, .L3264+12
	cmp	r3, r1
	ldr	r9, .L3264+16
	ldrh	r2, [r5, #28]
	add	r2, r2, #1
	strh	r2, [r5, #28]	@ movhi
	sub	r2, r9, #8
	bge	.L3262
	b	.L3252
.L3251:
	add	r3, r3, #1
	b	.L3250
.L3262:
	movw	r3, #310
	ldrh	r3, [r7, r3]
	cmp	r3, #0
	beq	.L3256
.L3252:
	ldrh	r3, [r9, #-8]
	ldr	r0, [r4, #-4060]
	ldrh	ip, [r2, #4]
	mov	r3, r3, asl #1
	ldrh	r1, [r0, r3]
	rsb	r1, ip, r1
	ldr	ip, .L3264+20
	strh	r1, [r0, r3]	@ movhi
	ldr	lr, [r4, #-4060]
	ldrh	r3, [ip]
	strh	r3, [r2, #2]	@ movhi
	mov	r3, #0
	strh	r3, [r2, #4]	@ movhi
	ldr	r2, .L3264+24
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
.L3256:
	ldrh	r0, [r9, #-8]
	movw	r3, #65535
	ldr	r4, .L3264+28
	cmp	r0, r3
	beq	.L3257
	ldrh	r3, [r4, #4]
	cmp	r3, #0
	bne	.L3257
	ldrh	r3, [r4, #52]
	add	r9, r4, #56
	add	r7, r4, #48
	cmp	r3, #0
	bne	.L3257
	bl	FtlGcRefreshOpenBlock
	ldrh	r0, [r4, #48]
	bl	FtlGcRefreshOpenBlock
	bl	FtlVpcTblFlush
	mov	r0, r4
	bl	allocate_new_data_superblock
	mov	r0, r7
	bl	allocate_new_data_superblock
.L3257:
	ldrb	r3, [r6]	@ zero_extendqisi2
	cmp	r3, #0
	bne	.L3258
	ldrh	r3, [r5, #28]
	tst	r3, #31
	bne	.L3248
.L3258:
	bl	FtlVpcCheckAndModify
.L3248:
	mov	r0, r8
	ldmfd	sp!, {r3, r4, r5, r6, r7, r8, r9, pc}
.L3265:
	.align	2
.L3264:
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
	ldr	r5, .L3301
	ldr	r6, [r5, #4052]
	cmp	r6, #0
	bne	.L3268
	movw	r7, #1908
	ldr	r4, .L3301+4
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
	beq	.L3269
	bl	FtlMakeBbt
.L3269:
	ldr	r0, .L3301+8
	mov	r2, #0
	ldr	ip, .L3301+12
.L3270:
	ldrh	r1, [r0]
	uxth	r3, r2
	add	r2, r2, #1
	cmp	r3, r1, asl #7
	bge	.L3298
	ldr	lr, [r4, #-2048]
	mvn	r1, r3
	orr	r1, r3, r1, asl #16
	str	r1, [lr, r3, asl #2]
	ldr	r1, [r4, #-2044]
	str	ip, [r1, r3, asl #2]
	b	.L3270
.L3298:
	ldr	r2, .L3301
	movw	r3, #1812
	ldr	r10, .L3301+16
	mov	r6, #0
	ldrh	r7, [r2, r3]
.L3272:
	ldrh	r3, [r10]
	ldr	r8, .L3301
	cmp	r3, r7
	ldr	r9, .L3301+16
	bls	.L3299
	mov	r0, r7
	mov	r1, #1
	bl	FtlLowFormatEraseBlock
	add	r7, r7, #1
	uxth	r7, r7
	add	r0, r6, r0
	uxth	r6, r0
	b	.L3272
.L3299:
	movw	r3, #1804
	ldrh	r1, [r8, r3]
	sub	r3, r6, #3
	cmp	r3, r1, asl #1
	bge	.L3274
.L3278:
	mov	r6, #0
	ldr	r7, .L3301+20
	mov	r8, r6
	b	.L3275
.L3274:
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
.L3276:
	ldrh	r3, [r9]
	cmp	r3, r6
	bls	.L3278
	mov	r0, r6
	add	r6, r6, #1
	mov	r1, #1
	bl	FtlLowFormatEraseBlock
	uxth	r6, r6
	b	.L3276
.L3275:
	ldrh	r3, [r7]
	uxth	r0, r6
	ldr	fp, .L3301
	add	r6, r6, #1
	cmp	r3, r0
	bls	.L3300
	mov	r1, #0
	bl	FtlLowFormatEraseBlock
	add	r0, r8, r0
	uxth	r8, r0
	b	.L3275
.L3300:
	movw	r3, #1814
	ldr	r2, [fp, #1816]
	ldrh	r3, [fp, r3]
	ldr	r6, .L3301+24
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
	ble	.L3280
	rsb	r0, r8, r2
	mov	r1, r7
	bl	__aeabi_uidiv
	str	r0, [fp, #1932]
	mov	r0, r0, lsr #5
	add	r0, r0, #24
	strh	r0, [r6, #-12]	@ movhi
.L3280:
	ldr	r3, [r5, #1796]
	cmp	r3, #1
	bne	.L3281
	mov	r0, r8
	mov	r1, r7
	bl	__aeabi_uidiv
	ldrh	fp, [r6, #-12]
	uxtah	r0, fp, r0
	add	fp, fp, r0, asr #2
	strh	fp, [r6, #-12]	@ movhi
.L3281:
	ldrb	r3, [r5, #140]	@ zero_extendqisi2
	cmp	r3, #0
	beq	.L3282
	mov	r0, r8
	mov	r1, r7
	bl	__aeabi_uidiv
	ldrh	fp, [r6, #-12]
	uxtah	r0, fp, r0
	add	fp, fp, r0, asr #2
	strh	fp, [r6, #-12]	@ movhi
.L3282:
	movw	r3, #1866
	ldrh	r3, [r5, r3]
	cmp	r3, #0
	beq	.L3284
	ldrh	r2, [r6, #-12]
	add	r2, r2, r3, lsr #1
	strh	r2, [r6, #-12]	@ movhi
	mul	r2, r7, r3
	cmp	r2, r8
	addgt	r3, r3, #32
	ldrgt	r2, .L3301
	addgt	r3, r9, r3
	strgt	r10, [r2, #1932]
	ldrgt	r2, .L3301+24
	strgth	r3, [r2, #-12]	@ movhi
.L3284:
	ldrh	r2, [r6, #-12]
	mvn	r9, #0
	ldr	r3, [r5, #1932]
	rsb	r3, r2, r3
	mul	r7, r7, r3
	ldr	r3, .L3301+28
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
	ldr	r7, .L3301+32
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
.L3286:
	ldr	r10, .L3301+36
	ldr	r5, .L3301+4
	mov	r0, r10
	bl	make_superblock
	ldrb	r3, [r4, #-4033]	@ zero_extendqisi2
	cmp	r3, #0
	ldrh	r3, [r8]
	bne	.L3287
	ldr	r2, [r7, #-4060]
	mov	r3, r3, asl #1
	strh	r9, [r2, r3]	@ movhi
	ldrh	r3, [fp]
	add	r3, r3, #1
	strh	r3, [fp]	@ movhi
	b	.L3286
.L3287:
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
.L3288:
	ldr	r8, .L3301+40
	ldr	r5, .L3301+4
	mov	r0, r8
	bl	make_superblock
	ldrb	r3, [r4, #-3985]	@ zero_extendqisi2
	cmp	r3, #0
	ldrh	r3, [r7]
	bne	.L3289
	ldr	r2, [r9, #-4060]
	mov	r3, r3, asl #1
	strh	r10, [r2, r3]	@ movhi
	ldrh	r3, [r7]
	add	r3, r3, #1
	strh	r3, [r7]	@ movhi
	b	.L3288
.L3289:
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
	ldr	r3, .L3301+44
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
	ldreq	r3, .L3301+48
	moveq	r2, #1
	streq	r2, [r3, #504]
.L3268:
	mov	r0, #0
	add	sp, sp, #12
	@ sp needed
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L3302:
	.align	2
.L3301:
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
	ldr	r5, .L3333
	ldr	r0, .L3333+4
	ldr	r3, [r5]
	str	r3, [sp, #20]
	ldrb	r3, [r0, #140]	@ zero_extendqisi2
	cmp	r3, #0
	beq	.L3304
.L3306:
	mov	r0, #0
	b	.L3305
.L3304:
	ldr	r6, .L3333+8
	mov	r4, r0
	ldr	r0, [r6, #144]
	bl	FlashInit
	cmp	r0, #0
	bne	.L3306
	bl	FlashLoadFactorBbt
	cmp	r0, #0
	beq	.L3307
	bl	FlashMakeFactorBbt
.L3307:
	ldr	r0, [r6, #176]
	bl	FlashReadIdbDataRaw
	cmp	r0, #0
	beq	.L3308
	mov	r2, #16
	add	r0, sp, #4
	mov	r1, #0
	bl	FlashReadFacBbtData
	mov	r3, #0
	mov	r0, r3
	mov	ip, #1
	ldr	r2, [sp, #4]
.L3309:
	ands	lr, r2, ip, asl r0
	add	r1, r3, #1
	add	r0, r0, #1
	movne	r3, r1
	cmp	r0, #16
	bne	.L3309
	cmp	r3, #6
	bls	.L3329
	mov	r0, #0
	mov	ip, #1
.L3312:
	ands	lr, r2, ip, asl r0
	add	r1, r3, #1
	add	r0, r0, #1
	movne	r3, r1
	cmp	r0, #24
	bne	.L3312
	cmp	r3, #17
	movhi	r3, #36
	strhib	r3, [r4, #1]
	bhi	.L3311
.L3329:
	strb	r0, [r4, #1]
.L3311:
	ldrb	r3, [r4, #1]	@ zero_extendqisi2
	strh	r3, [r4, #138]	@ movhi
.L3308:
	ldr	r1, .L3333+12
	ldr	r0, .L3333+16
	bl	printk
	ldr	r0, .L3333+20
	bl	FtlConstantsInit
	bl	FtlVariablesInit
	ldr	r0, [r4, #1808]
	mov	r4, #1
	uxth	r0, r0
	bl	FtlFreeSysBlkQueueInit
.L3314:
	bl	FtlLoadBbt
	cmp	r0, #0
	beq	.L3315
.L3331:
	bl	FtlLowFormat
	cmp	r4, #3
	addls	r4, r4, #1
	bls	.L3314
.L3332:
	mvn	r0, #0
	b	.L3305
.L3315:
	bl	FtlSysBlkInit
	cmp	r0, #0
	bne	.L3331
	ldr	r3, .L3333+24
	mov	r2, #1
	str	r2, [r3, #504]
.L3305:
	ldr	r2, [sp, #20]
	ldr	r3, [r5]
	cmp	r2, r3
	beq	.L3319
	bl	__stack_chk_fail
.L3319:
	add	sp, sp, #24
	@ sp needed
	ldmfd	sp!, {r4, r5, r6, pc}
.L3334:
	.align	2
.L3333:
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
	ldr	r3, .L3354
	ldr	r2, [r3, #4052]
	cmp	r2, #0
	bne	.L3352
	stmfd	sp!, {r4, r5, r6, r7, r8, lr}
	.save {r4, r5, r6, r7, r8, lr}
	ldrb	r3, [r3, #140]	@ zero_extendqisi2
	ldr	r5, .L3354+4
	cmp	r3, #0
	beq	.L3338
	ldr	r3, [r5, #-3220]
	tst	r3, #1
	beq	.L3338
	ldr	r3, .L3354+8
	ldrh	r3, [r3, #4]
	cmp	r3, #0
	beq	.L3338
.L3339:
	mov	r0, #0
	ldmfd	sp!, {r4, r5, r6, r7, r8, pc}
.L3338:
	mov	r2, #0
	ldr	r0, [r5, #-2072]
	ldr	r1, [r5, #-3220]
	mov	r3, r2
	bl	FlashProgPages
	ldr	r7, .L3354+4
	mov	r6, #0
	mov	r8, #36
.L3340:
	ldr	r1, [r5, #-3220]
	uxth	r3, r6
	ldr	r4, .L3354+4
	cmp	r3, r1
	bcs	.L3353
	mul	r3, r8, r3
	ldr	r0, [r7, #-2072]
	add	r6, r6, #1
	add	r1, r0, r3
	ldr	r0, [r0, r3]
	ldr	r2, [r1, #12]
	cmn	r0, #1
	bne	.L3341
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
	b	.L3351
.L3341:
	ldr	r0, [r2, #12]
	ldr	r1, [r1, #4]
	ldr	r2, [r2, #8]
	bl	FtlGcUpdatePage
	b	.L3340
.L3353:
	ldr	r0, [r4, #-2072]
	bl	FtlGcBufFree
	mov	r3, #0
	str	r3, [r4, #-3220]
	ldr	r3, .L3354+8
	ldrh	r3, [r3, #4]
	cmp	r3, #0
	bne	.L3339
	mov	r0, #1
	bl	FtlGcFreeTempBlock
.L3351:
	mov	r0, #1
	ldmfd	sp!, {r4, r5, r6, r7, r8, pc}
.L3352:
	mov	r0, #0
	bx	lr
.L3355:
	.align	2
.L3354:
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
	ldr	r3, .L3360
	sub	r2, r3, #8
	ldrh	r1, [r3, #-8]
	movw	r3, #65535
	cmp	r1, r3
	beq	.L3357
	ldrh	r3, [r2, #4]
	cmp	r3, #0
	bne	.L3358
.L3357:
	ldr	r4, .L3360+4
	bl	FtlCacheWriteBack
	mov	r0, #0
	mov	r5, #0
	bl	FtlGcFreeTempBlock
	sub	r4, r4, #3184
	strb	r5, [r4, #-752]
	ldr	r0, .L3360+8
	bl	allocate_data_superblock
	strh	r5, [r4, #-8]	@ movhi
	strh	r5, [r4]	@ movhi
	bl	l2p_flush
	mov	r0, r5
	bl	FtlEctTblFlush
	bl	FtlVpcTblFlush
.L3358:
	ldr	r0, .L3360+8
	ldmfd	sp!, {r3, r4, r5, lr}
	b	get_new_active_ppa
.L3361:
	.align	2
.L3360:
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
	ldr	r5, .L3526
	ldr	r3, .L3526+4
	str	r0, [sp, #12]
	ldr	r0, [r5, #4052]
	ldr	r3, [r3]
	cmp	r0, #0
	str	r1, [sp, #8]
	str	r3, [sp, #44]
	movne	r0, #0
	bne	.L3363
	ldr	r2, .L3526+8
	ldr	r3, [r2, #504]
	cmn	r3, #1
	beq	.L3363
	ldr	r3, .L3526+12
	ldr	r4, [r3, #-2100]
	mov	r7, r3
	cmp	r4, #0
	bne	.L3363
	sub	r1, r3, #4048
	ldrh	r1, [r1, #-4]
	cmp	r1, #47
	movls	r0, r4
	bls	.L3363
	movw	r1, #3288
	ldrh	r1, [r2, r1]
	movw	r2, #65535
	cmp	r1, r2
	bne	.L3364
.L3367:
	ldr	r6, .L3526+16
	movw	r2, #65535
	ldrh	r4, [r6, #-12]
	cmp	r4, r2
	bne	.L3365
	b	.L3366
.L3364:
	sub	r3, r3, #3936
	ldrh	r3, [r3, #-8]
	cmp	r3, r2
	beq	.L3367
	mov	r0, #1
	bl	FtlGcFreeTempBlock
	cmp	r0, #0
	beq	.L3367
	mov	r0, #1
	b	.L3363
.L3365:
	ldrh	r3, [r6, #-14]
	cmp	r3, r2
	bne	.L3366
	ldrh	r1, [r6, #-10]
	cmp	r1, r3
	beq	.L3366
	ldrh	r2, [r6, #-8]
	cmp	r2, r3
	strneh	r4, [r6, #-14]	@ movhi
	strneh	r1, [r6, #-12]	@ movhi
	mvnne	r3, #0
	strneh	r2, [r6, #-10]	@ movhi
	strneh	r3, [r6, #-8]	@ movhi
.L3366:
	ldr	r2, [sp, #12]
	ldr	r3, [r7, #-3232]
	ldr	r8, .L3526+20
	cmp	r2, #1
	add	r3, r3, #1
	add	r3, r3, r2, asl #7
	sub	r10, r8, #240
	str	r3, [r7, #-3232]
	bne	.L3378
	ldr	r2, [r5, #1796]
	cmp	r2, #0
	bne	.L3369
	ldr	r2, .L3526
	ldrb	r2, [r2, #140]	@ zero_extendqisi2
	cmp	r2, #0
	beq	.L3378
.L3369:
	ldr	r2, [r7, #-3824]
	ldr	r9, .L3526+12
	cmp	r2, #39
	bhi	.L3378
	ldr	r4, .L3526+24
	ldrh	r2, [r4]
	add	r3, r3, r2
	str	r3, [r9, #-3232]
	bl	FtlGcReFreshBadBlk
	ldrh	r3, [r8, #-8]
	movw	r2, #65535
	cmp	r3, r2
	bne	.L3378
	ldrh	r2, [r6, #-14]
	cmp	r2, r3
	bne	.L3378
	ldr	r3, [r9, #-3232]
	cmp	r3, #1024
	bhi	.L3370
	ldrh	r3, [r10, #-12]
	cmp	r3, #63
	bhi	.L3378
.L3370:
	ldr	r3, .L3526+12
	mov	r0, #0
	ldrh	ip, [r10, #-12]
	sub	r2, r3, #3232
	ldr	r1, .L3526+24
	strh	r0, [r4]	@ movhi
	ldrh	r2, [r2, #-2]
	add	r2, r2, #64
	cmp	ip, r2
	bgt	.L3378
	str	r0, [r3, #-3232]
	ldr	r3, [r3, #-3824]
	cmp	r3, r0
	moveq	r3, #6
	beq	.L3518
	cmp	r3, #5
	bhi	.L3372
	mov	r3, #18
.L3518:
	strh	r3, [r1]	@ movhi
.L3372:
	mov	r0, #32
	bl	List_get_gc_head_node
	movw	r3, #65535
	uxth	lr, r0
	cmp	lr, r3
	beq	.L3377
	ldr	fp, .L3526+12
	sub	r9, fp, #3216
	ldrh	r0, [r9, #-12]
	cmp	r0, #0
	beq	.L3374
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
	bgt	.L3377
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
	beq	.L3377
	mov	r10, r4, asl #1
	ldr	r0, .L3526+28
	ldrh	r3, [r2, r10]
	ldrh	r2, [r2, ip]
	str	r2, [sp]
	mov	r2, r4
	bl	printk
	ldrh	r3, [r9, #-12]
	cmp	r3, #40
	bls	.L3375
	ldr	r3, [fp, #-4060]
	ldrh	r3, [r3, r10]
	cmp	r3, #32
	movhi	r3, #0
	strhih	r3, [r9, #-12]	@ movhi
.L3375:
	ldr	r3, .L3526+24
	mov	r2, #6
	strh	r2, [r3]	@ movhi
	b	.L3379
.L3374:
	mov	r3, #1
	strh	r3, [r9, #-12]	@ movhi
.L3377:
	bl	GetSwlReplaceBlock
	movw	r3, #65535
	cmp	r0, r3
	mov	r4, r0
	bne	.L3379
	ldr	r3, .L3526+24
	mov	r2, #0
	strh	r2, [r3]	@ movhi
.L3378:
	ldrh	r3, [r8, #-8]
	movw	r4, #65535
	ldr	r9, .L3526+12
	cmp	r3, r4
	bne	.L3379
	sub	r2, r9, #3936
	ldrh	r4, [r2, #-8]
	cmp	r4, r3
	movne	r4, r3
	beq	.L3523
.L3379:
	movw	r1, #65535
	rsb	r3, r1, r4
	clz	r3, r3
	ldr	r2, [sp, #12]
	ldr	r9, .L3526+32
	mov	r3, r3, lsr #5
	cmp	r2, #0
	movne	ip, #0
	andeq	ip, r3, #1
	cmp	ip, #0
	beq	.L3392
	ldrh	r0, [r9, #-12]
	cmp	r0, #24
	movhi	r10, #1
	bhi	.L3393
	ldr	r3, .L3526+36
	cmp	r0, #16
	ldrh	r10, [r3]
	movhi	r10, r10, lsr #5
	bhi	.L3393
	cmp	r0, #12
	movhi	r10, r10, lsr #4
	bhi	.L3393
	cmp	r0, #8
	movhi	r10, r10, lsr #2
.L3393:
	ldr	r1, .L3526+12
	sub	r2, r1, #3232
	ldrh	r3, [r2, #-4]
	cmp	r3, r0
	bcs	.L3397
	sub	r3, r1, #3936
	movw	r0, #65535
	ldrh	r3, [r3, #-8]
	cmp	r3, r0
	bne	.L3398
	sub	r0, r1, #3168
	ldrh	r0, [r0, #-14]
	cmp	r0, r3
	bne	.L3398
	ldr	r3, .L3526+24
	ldrh	r0, [r3]
	cmp	r0, #0
	bne	.L3399
	ldr	r3, [r5, #1932]
	ldr	r1, [r1, #-3888]
	add	r3, r3, r3, asl #1
	cmp	r1, r3, lsr #2
	movcs	r3, #18
	bcs	.L3400
.L3399:
	ldr	r3, .L3526+40
	ldrh	r3, [r3, #-12]
	add	r3, r3, r3, asl #1
	ubfx	r3, r3, #2, #16
.L3400:
	strh	r3, [r2, #-4]	@ movhi
	mov	r3, #0
	str	r3, [r7, #-3224]
	b	.L3363
.L3398:
	ldr	r3, .L3526+40
	ldrh	r3, [r3, #-12]
	add	r3, r3, r3, asl #1
	mov	r3, r3, asr #2
	strh	r3, [r2, #-4]	@ movhi
.L3397:
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
	b	.L3402
.L3392:
	ldrh	r0, [r9, #88]
	add	r2, r9, #4032
	cmp	r0, r1
	bne	.L3403
	ldrh	r1, [r6, #-14]
	cmp	r1, r0
	movne	r3, #0
	andeq	r3, r3, #1
	cmp	r3, #0
	beq	.L3403
	sub	r3, r2, #3792
	ldrh	r3, [r3, #-8]
	cmp	r3, r0
	beq	.L3404
.L3408:
	movw	r4, #65535
	b	.L3403
.L3404:
	sub	r4, r2, #3232
	ldrh	r1, [r9, #-12]
	str	ip, [r2, #-3224]
	ldrh	r3, [r4, #-4]
	ldr	r10, .L3526+24
	cmp	r1, r3
	bls	.L3405
	ldrh	r3, [r10]
	cmp	r3, #0
	bne	.L3406
	ldr	r3, [r5, #1932]
	ldr	r2, [r2, #-3888]
	add	r3, r3, r3, asl #1
	cmp	r2, r3, lsr #2
	movcs	r3, #18
	bcs	.L3407
.L3406:
	ldr	r3, .L3526+40
	ldrh	r3, [r3, #-12]
	add	r3, r3, r3, asl #1
	ubfx	r3, r3, #2, #16
.L3407:
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
	bhi	.L3363
.L3405:
	ldrh	r0, [r10]
	cmp	r0, #0
	bne	.L3408
	ldr	r3, .L3526+40
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
	ble	.L3409
	ldrh	r3, [r9, #-12]
	sub	r2, r10, #1
	cmp	r3, r2
	blt	.L3409
	bl	FtlReadRefresh
	b	.L3521
.L3409:
	cmp	r1, #0
	bne	.L3408
	movw	r0, #65535
	bl	decrement_vpc_count
	ldrh	r0, [r9, #-12]
	add	r0, r0, #1
	b	.L3363
.L3403:
	ldr	r3, [r5, #1796]
	cmp	r3, #0
	moveq	r10, #1
	movne	r10, #2
.L3402:
	ldrh	r3, [r8, #-8]
	movw	r0, #65535
	ldr	r2, .L3526+12
	cmp	r3, r0
	sub	r1, r2, #3792
	bne	.L3411
	cmp	r4, r3
	strneh	r4, [r1, #-8]	@ movhi
	bne	.L3413
	sub	r1, r2, #3168
	ldrh	r3, [r1, #-14]
	cmp	r3, r4
	beq	.L3413
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
.L3413:
	ldrh	r0, [r8, #-8]
	mov	r3, #0
	strb	r3, [r7, #-3792]
	movw	r3, #65535
	cmp	r0, r3
	beq	.L3411
	bl	IsBlkInGcList
	cmp	r0, #0
	ldrne	r3, .L3526+20
	mvnne	r2, #0
	strneh	r2, [r3, #-8]	@ movhi
	ldrb	r3, [r5, #140]	@ zero_extendqisi2
	cmp	r3, #0
	beq	.L3417
	ldrh	r0, [r8, #-8]
	bl	ftl_get_blk_mode
	strb	r0, [r7, #-3792]
.L3417:
	ldrh	r2, [r8, #-8]
	movw	r3, #65535
	ldr	r6, .L3526+12
	cmp	r2, r3
	sub	r7, r6, #3792
	sub	r5, r7, #8
	beq	.L3411
	mov	r0, r5
	bl	make_superblock
	ldr	r3, .L3526+44
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
.L3411:
	ldrh	r3, [r8, #-8]
	ldrh	r2, [r9, #-8]
	cmp	r2, r3
	beq	.L3418
	ldr	r2, .L3526+48
	ldrh	r1, [r2, #-8]
	cmp	r1, r3
	beq	.L3418
	ldrh	r2, [r2, #40]
	cmp	r2, r3
	bne	.L3452
.L3418:
	ldr	r3, .L3526+20
	mvn	r2, #0
	strh	r2, [r3, #-8]	@ movhi
.L3452:
	ldr	r7, .L3526+52
	movw	r3, #65535
	ldr	r6, .L3526+12
	ldrh	r2, [r7]
	mov	fp, r7
	mov	r8, r6
	cmp	r2, r3
	bne	.L3420
	mov	r3, #0
	str	r3, [r6, #-3224]
.L3421:
	ldr	r9, .L3526+56
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
	beq	.L3363
.L3422:
	mov	r0, r3
	str	r3, [sp, #8]
	bl	IsBlkInGcList
	add	r5, r5, #1
	cmp	r0, #0
	ldr	r3, [sp, #8]
	ldrne	r3, .L3526+56
	strneh	r5, [r3]	@ movhi
	bne	.L3421
	ldr	r2, .L3526+56
	uxth	r5, r5
	ldr	r0, [r8, #-4060]
	mov	r1, r3, asl #1
	strh	r5, [r2]	@ movhi
	ldr	r2, .L3526+36
	ldrh	ip, [r0, r1]
	ldrh	lr, [r2]
	ldrh	r2, [r2, #-68]
	mul	r2, r2, lr
	add	lr, r2, r2, lsr #31
	cmp	ip, lr, asr #1
	bgt	.L3425
	cmp	ip, #8
	cmphi	r5, #48
	bls	.L3426
	ldrh	ip, [r9, #36]
	cmp	ip, #35
	bhi	.L3426
.L3425:
	ldr	lr, .L3526+56
	mov	ip, #0
	strh	ip, [lr]	@ movhi
.L3426:
	ldrh	r1, [r0, r1]
	movw	r0, #65535
	cmp	r1, r2
	cmpge	r4, r0
	bne	.L3427
	ldr	r2, .L3526+56
	ldrh	r0, [r2]
	cmp	r0, #3
	bhi	.L3427
	sub	r3, r2, #564
	mvn	r1, #0
	strh	r1, [r3, #-8]	@ movhi
	mov	r3, #0
	strh	r3, [r2]	@ movhi
	b	.L3521
.L3427:
	cmp	r1, #0
	bne	.L3428
	movw	r0, #65535
	bl	decrement_vpc_count
	ldr	r3, .L3526+56
	ldr	r2, .L3526+56
	ldrh	r3, [r3]
	add	r3, r3, #1
	strh	r3, [r2]	@ movhi
	b	.L3421
.L3428:
	mov	r2, #0
	strb	r2, [r6, #-3792]
	ldr	r2, .L3526
	ldrb	r2, [r2, #140]	@ zero_extendqisi2
	cmp	r2, #0
	beq	.L3429
	mov	r0, r3
	bl	ftl_get_blk_mode
	ldr	r3, .L3526+12
	strb	r0, [r3, #-3792]
.L3429:
	ldr	r0, .L3526+52
	bl	make_superblock
	ldrh	r2, [r7]
	ldr	r1, .L3526+60
	mov	r3, #0
	ldr	r0, [r6, #-4060]
	mov	r2, r2, asl #1
	strh	r3, [r1]	@ movhi
	ldrh	r2, [r0, r2]
	strh	r3, [r7, #2]	@ movhi
	strb	r3, [r6, #-3794]
	strh	r2, [r1, #2]	@ movhi
.L3420:
	ldr	r3, [sp, #12]
	cmp	r3, #1
	bne	.L3430
	bl	FtlReadRefresh
.L3430:
	mov	r3, #1
	str	r3, [r6, #-2100]
	ldr	r3, .L3526
	add	r2, r3, #1872
	ldrb	r3, [r3, #140]	@ zero_extendqisi2
	ldrh	r2, [r2]
	cmp	r3, #0
	str	r2, [sp, #8]
	beq	.L3431
	ldr	r3, .L3526+12
	ldrb	r3, [r3, #-3792]	@ zero_extendqisi2
	cmp	r3, #1
	ldreq	r3, .L3526+64
	ldreqh	r3, [r3]
	streq	r3, [sp, #8]
.L3431:
	ldrh	r3, [r7, #2]
	mov	r7, #0
	ldr	r1, [sp, #8]
	add	r2, r3, r10
	ldr	r5, .L3526+12
	cmp	r2, r1
	movgt	r2, r1
	rsbgt	r3, r3, r2
	uxthgt	r10, r3
.L3433:
	uxth	r3, r7
	ldr	r6, .L3526+12
	cmp	r3, r10
	ldr	r8, .L3526+52
	bcs	.L3440
	ldr	r3, .L3526+68
	movw	lr, #65535
	ldr	r8, [r5, #-3204]
	mov	ip, #36
	ldrh	r9, [r3]
	ldr	r3, .L3526+52
	add	r0, r3, #14
	ldrh	r1, [r3, #2]
	mov	r3, #0
	mov	r6, r3
	add	r1, r1, r7
.L3441:
	uxth	r2, r3
	cmp	r2, r9
	bcs	.L3524
	ldrh	r2, [r0, #2]!
	add	r3, r3, #1
	cmp	r2, lr
	orrne	r2, r1, r2, asl #10
	mlane	fp, ip, r6, r8
	addne	r6, r6, #1
	uxthne	r6, r6
	strne	r2, [fp, #4]
	b	.L3441
.L3524:
	ldr	r0, [r5, #-3204]
	mov	r1, r6
	ldrb	r2, [r5, #-3792]	@ zero_extendqisi2
	mov	r9, #0
	bl	FlashReadPages
.L3436:
	uxth	r3, r9
	cmp	r3, r6
	bcs	.L3525
	mov	r3, #36
	ldr	r2, [r5, #-3204]
	mul	r8, r3, r9
	add	r1, r2, r8
	ldr	r2, [r2, r8]
	ldr	fp, [r1, #12]
	cmn	r2, #1
	beq	.L3473
	ldrh	r1, [fp]
	movw	r2, #61589
	cmp	r1, r2
	bne	.L3473
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
	bne	.L3473
	ldr	r1, .L3526+60
	ldr	r0, .L3526+60
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
	ldr	r3, .L3526+72
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
	ldr	r3, .L3526
	ldrb	r3, [r3, #140]	@ zero_extendqisi2
	cmp	r3, #0
	bne	.L3438
	ldrb	r3, [r5, #-3937]	@ zero_extendqisi2
	ldr	r2, [r5, #-3220]
	cmp	r2, r3
	beq	.L3438
	ldr	r3, .L3526+72
	ldrh	r3, [r3, #4]
	cmp	r3, #0
	bne	.L3473
.L3438:
	bl	Ftl_gc_temp_data_write_back
	cmp	r0, #0
	beq	.L3473
	ldr	r3, .L3526+12
	mov	r2, #0
	mvn	r1, #0
	sub	r3, r3, #3792
	str	r2, [r3, #1692]
	strh	r1, [r3, #-8]	@ movhi
	strh	r2, [r3, #-6]	@ movhi
	b	.L3521
.L3473:
	add	r9, r9, #1
	b	.L3436
.L3525:
	add	r7, r7, #1
	b	.L3433
.L3440:
	ldrh	r3, [r8, #2]
	add	r10, r10, r3
	ldr	r3, [sp, #8]
	uxth	r10, r10
	strh	r10, [r8, #2]	@ movhi
	cmp	r10, r3
	bcc	.L3442
	ldr	r3, [r6, #-3220]
	cmp	r3, #0
	beq	.L3443
	bl	Ftl_gc_temp_data_write_back
	cmp	r0, #0
	beq	.L3443
	mov	r3, #0
	str	r3, [r6, #-2100]
.L3521:
	ldr	r3, .L3526+24
	ldrh	r0, [r3]
	b	.L3363
.L3443:
	ldr	r3, .L3526+60
	ldrh	r5, [r3]
	cmp	r5, #0
	bne	.L3444
	ldrh	r3, [r8]
	ldr	r2, [r6, #-4060]
	mov	r3, r3, asl #1
	ldrh	r3, [r2, r3]
	cmp	r3, #0
	beq	.L3444
	ldr	r7, .L3526
.L3445:
	ldr	r3, [r7, #1932]
	cmp	r5, r3
	bcs	.L3450
	mov	r0, r5
	add	r1, sp, #40
	mov	r2, #0
	bl	log2phys
	ldr	r0, [sp, #40]
	cmn	r0, #1
	beq	.L3446
	ubfx	r0, r0, #10, #16
	bl	P2V_block_in_plane
	ldrh	r3, [r8]
	cmp	r3, r0
	bne	.L3446
.L3450:
	ldr	r3, .L3526
	ldr	r3, [r3, #1932]
	cmp	r5, r3
	bcc	.L3444
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
	b	.L3444
.L3446:
	add	r5, r5, #1
	b	.L3445
.L3444:
	mvn	r3, #0
	strh	r3, [r8]	@ movhi
.L3442:
	ldr	r3, .L3526+76
	ldrh	r3, [r3]
	cmp	r3, #2
	ldrls	r3, .L3526+36
	ldrlsh	r10, [r3]
	bls	.L3452
.L3451:
	ldr	r2, .L3526+12
	mov	r1, #0
	str	r1, [r2, #-2100]
	ldr	r2, .L3526+24
	ldrh	r0, [r2]
	cmp	r0, r1
	addeq	r0, r3, #1
.L3363:
	ldr	r3, .L3526+4
	ldr	r2, [sp, #44]
	ldr	r3, [r3]
	cmp	r2, r3
	beq	.L3456
	bl	__stack_chk_fail
.L3523:
	sub	r3, r9, #3168
	ldrh	fp, [r3, #-14]
	cmp	fp, r4
	bne	.L3379
	sub	r3, r9, #4032
	ldr	r1, [r9, #-3232]
	ldrh	r2, [r3, #-12]
	cmp	r2, #24
	movcc	r2, #5120
	movcs	r2, #1024
	cmp	r1, r2
	movls	r4, fp
	bls	.L3379
	ldr	r1, .L3526+24
	mov	r2, #0
	str	r3, [sp, #16]
	str	r2, [r7, #-3232]
	strh	r2, [r1]	@ movhi
	bl	GetSwlReplaceBlock
	cmp	r0, fp
	mov	r4, r0
	sub	fp, r9, #3232
	ldr	r3, [sp, #16]
	bne	.L3381
	ldrh	r2, [r3, #-12]
	ldrh	r3, [fp, #-2]
	cmp	r2, r3
	bcs	.L3382
	mov	r0, #64
	bl	List_get_gc_head_node
	uxth	r3, r0
	cmp	r3, r4
	beq	.L3391
	ldr	r2, [r9, #-2104]
	ldr	r1, .L3526+80
	cmp	r2, #0
	bne	.L3384
	ldrh	r0, [r1]
	cmp	r0, #3
	beq	.L3384
	ldr	r0, [r9, #-3752]
	cmp	r0, #0
	bne	.L3384
	ldr	r0, [r1, #-28]
	cmp	r0, #0
	bne	.L3384
	ldrb	r0, [r1, #-1684]	@ zero_extendqisi2
	cmp	r0, #0
	beq	.L3385
.L3384:
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
	bgt	.L3387
	mov	r0, #0
	bl	List_get_gc_head_node
	ldr	r3, [r5, #1932]
	ldr	r2, [r7, #-3888]
	add	r3, r3, r3, asl #1
	cmp	r2, r3, lsr #2
	movls	r3, #160
	uxth	r4, r0
	bls	.L3519
	b	.L3522
.L3385:
	ldr	r2, [r9, #-4060]
	mov	r3, r3, asl #1
	ldrh	r3, [r2, r3]
	cmp	r3, #7
	bhi	.L3390
	bl	List_get_gc_head_node
	uxth	r4, r0
.L3522:
	mov	r3, #128
.L3519:
	strh	r3, [fp, #-2]	@ movhi
	movw	r3, #65535
	cmp	r4, r3
	beq	.L3391
	b	.L3381
.L3387:
	mov	r3, #128
	b	.L3520
.L3390:
	mov	r3, #64
	b	.L3520
.L3382:
	mov	r3, #80
.L3520:
	strh	r3, [fp, #-2]	@ movhi
	b	.L3391
.L3381:
	ldr	r0, [r5, #4060]
	mov	r1, r4, asl #1
	ldr	r3, [r7, #-4060]
	ldrh	r2, [r10, #-12]
	ldrh	r3, [r3, r1]
	ldrh	r1, [r0, r1]
	ldr	r0, .L3526+84
	str	r1, [sp]
	ldrh	r1, [fp, #-4]
	str	r1, [sp, #4]
	mov	r1, r4
	bl	printk
.L3391:
	bl	FtlGcReFreshBadBlk
	b	.L3379
.L3456:
	add	sp, sp, #52
	@ sp needed
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L3527:
	.align	2
.L3526:
	.word	.LANCHOR0
	.word	__stack_chk_guard
	.word	.LANCHOR1
	.word	.LANCHOR2
	.word	.LANCHOR2-3168
	.word	.LANCHOR2-3792
	.word	.LANCHOR4+444
	.word	.LC152
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
	.word	.LC153
	.fnend
	.size	ftl_do_gc, .-ftl_do_gc
	.align	2
	.global	FtlCacheWriteBack
	.type	FtlCacheWriteBack, %function
FtlCacheWriteBack:
	.fnstart
	@ args = 0, pretend = 0, frame = 16
	@ frame_needed = 0, uses_anonymous_args = 0
	ldr	r3, .L3573
	stmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.save {r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.pad #20
	sub	sp, sp, #20
	ldr	r2, [r3]
	ldr	r4, .L3573+4
	str	r3, [sp, #4]
	str	r2, [sp, #12]
	ldr	r2, .L3573+8
	ldr	r5, [r2, #452]
	ldr	r2, [r4, #4052]
	cmp	r2, #0
	bne	.L3530
	ldr	r1, [r4, #1924]
	cmp	r1, #0
	beq	.L3530
	ldrb	r6, [r4, #140]	@ zero_extendqisi2
	mov	r8, #0
	ldr	r0, [r4, #1928]
	cmp	r6, #0
	ldrb	r3, [r5, #9]	@ zero_extendqisi2
	ldr	r9, .L3573+4
	ldrneb	r6, [r5, #8]	@ zero_extendqisi2
	ldr	r10, .L3573+12
	subne	r6, r6, #1
	clzne	r6, r6
	movne	r6, r6, lsr #5
	mov	r2, r6
	bl	FlashProgPages
.L3533:
	ldr	r3, [r4, #1924]
	cmp	r8, r3
	bcs	.L3551
	mov	r7, #36
	ldr	r3, [r9, #1928]
	mul	r7, r7, r8
	add	r2, r3, r7
	ldr	r3, [r3, r7]
	cmn	r3, #1
	beq	.L3555
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
	beq	.L3537
	ubfx	r0, r0, #10, #16
	bl	P2V_block_in_plane
	ldr	r2, [r10, #-4060]
	mov	r3, r0, asl #1
	mov	r7, r0
	ldrh	r2, [r2, r3]
	cmp	r2, #0
	bne	.L3538
	ldr	r0, .L3573+16
	mov	r1, r7
	bl	printk
.L3538:
	mov	r0, r7
	bl	decrement_vpc_count
.L3537:
	add	r8, r8, #1
	b	.L3533
.L3571:
	ldr	r6, .L3573+20
	movw	r5, #16386
.L3550:
	ldrh	r3, [r6]
	cmp	r3, #0
	beq	.L3551
	mov	r0, #1
	mov	r1, r0
	bl	ftl_do_gc
	subs	r5, r5, #1
	bne	.L3550
.L3551:
	mov	r3, #0
	str	r3, [r4, #1924]
	b	.L3530
.L3555:
	ldr	r10, .L3573+4
	mov	r8, #0
	ldr	r9, .L3573+12
.L3534:
	ldr	r3, [r4, #1924]
	cmp	r8, r3
	bcs	.L3571
	mov	r7, #36
	ldr	r3, [r10, #1928]
	mul	r7, r7, r8
	ldr	fp, .L3573+24
	mvn	r2, #0
	str	r2, [r3, r7]
.L3540:
	ldr	r3, [r4, #1928]
	add	r2, r3, r7
	ldr	r3, [r3, r7]
	cmn	r3, #1
	bne	.L3572
	ldr	r0, [r2, #4]
	ubfx	r0, r0, #10, #16
	bl	P2V_block_in_plane
	ldrh	r3, [r5]
	cmp	r3, r0
	bne	.L3541
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
.L3541:
	ldrh	r3, [r5, #4]
	cmp	r3, #0
	bne	.L3542
	mov	r0, r5
	bl	allocate_new_data_superblock
.L3542:
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
	beq	.L3540
	b	.L3530
.L3572:
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
	beq	.L3547
	ubfx	r0, r0, #10, #16
	bl	P2V_block_in_plane
	ldr	r2, [r9, #-4060]
	mov	r3, r0, asl #1
	mov	r7, r0
	ldrh	r2, [r2, r3]
	cmp	r2, #0
	bne	.L3548
	ldr	r0, .L3573+16
	mov	r1, r7
	bl	printk
.L3548:
	mov	r0, r7
	bl	decrement_vpc_count
.L3547:
	add	r8, r8, #1
	b	.L3534
.L3530:
	ldr	r3, [sp, #4]
	mov	r0, #0
	ldr	r2, [sp, #12]
	ldr	r3, [r3]
	cmp	r2, r3
	beq	.L3553
	bl	__stack_chk_fail
.L3553:
	add	sp, sp, #20
	@ sp needed
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L3574:
	.align	2
.L3573:
	.word	__stack_chk_guard
	.word	.LANCHOR0
	.word	.LANCHOR4
	.word	.LANCHOR2
	.word	.LC154
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
	ldr	r3, .L3581
	ldr	r3, [r3, #4052]
	cmp	r3, #0
	bne	.L3576
	ldr	r3, .L3581+4
	ldr	r3, [r3, #504]
	cmn	r3, #1
	beq	.L3576
	bl	FtlCacheWriteBack
	bl	l2p_flush
	mov	r0, #1
	bl	FtlEctTblFlush
	bl	FtlVpcTblFlush
.L3576:
	mov	r0, #0
	ldmfd	sp!, {r3, pc}
.L3582:
	.align	2
.L3581:
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
	ldr	r3, .L3586
	ldr	r3, [r3, #504]
	cmp	r3, #1
	bne	.L3584
	bl	FtlSysFlush
.L3584:
	mov	r0, #0
	ldmfd	sp!, {r3, pc}
.L3587:
	.align	2
.L3586:
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
	ldr	r0, .L3592
	bl	printk
	ldmfd	sp!, {r3, lr}
	b	ftl_deinit
.L3593:
	.align	2
.L3592:
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
	@ args = 0, pretend = 0, frame = 16
	@ frame_needed = 0, uses_anonymous_args = 0
	stmfd	sp!, {r4, r5, r6, r7, r8, r9, lr}
	.save {r4, r5, r6, r7, r8, r9, lr}
	.pad #20
	sub	sp, sp, #20
	ldr	r6, .L3616
	add	r2, r0, r1
	ldr	r4, .L3616+4
	mov	r7, r0
	mov	r5, r1
	ldr	r3, [r6]
	str	r3, [sp, #12]
	ldr	r3, [r4, #1912]
	cmp	r2, r3
	mvnhi	r0, #0
	bhi	.L3597
	ldr	r3, .L3616+8
	ldr	r0, [r3, #504]
	cmn	r0, #1
	beq	.L3597
	cmp	r1, #31
	bls	.L3607
	ldr	r3, [r4, #4052]
	cmp	r3, #0
	bne	.L3607
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
	beq	.L3598
	rsb	r4, r7, r4
	add	r8, r0, #1
	cmp	r4, r5
	movcs	r4, r5
	uxth	r4, r4
	rsb	r5, r4, r5
.L3598:
	ldr	r4, .L3616+12
	mvn	r3, #0
	ldr	r9, .L3616+16
	str	r3, [sp, #8]
	mov	r7, r4
.L3599:
	ldrh	r3, [r4]
	cmp	r5, r3
	bcc	.L3615
	mov	r0, r8
	add	r1, sp, #4
	mov	r2, #0
	bl	log2phys
	ldr	r3, [sp, #4]
	cmn	r3, #1
	beq	.L3600
	ldr	r2, .L3616+20
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
.L3600:
	ldrh	r3, [r7]
	add	r8, r8, #1
	rsb	r5, r3, r5
	b	.L3599
.L3615:
	ldr	r3, .L3616+16
	ldr	r2, [r3, #456]
	cmp	r2, #32
	bls	.L3607
	mov	r2, #0
	str	r2, [r3, #456]
	bl	l2p_flush
	bl	FtlVpcTblFlush
.L3607:
	mov	r0, #0
.L3597:
	ldr	r2, [sp, #12]
	ldr	r3, [r6]
	cmp	r2, r3
	beq	.L3603
	bl	__stack_chk_fail
.L3603:
	add	sp, sp, #20
	@ sp needed
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, pc}
.L3617:
	.align	2
.L3616:
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
	ldr	r3, .L3667
	.pad #84
	sub	sp, sp, #84
	str	r2, [sp, #36]
	ldr	r3, [r3]
	str	r3, [sp, #76]
	ldr	r3, .L3667+4
	ldr	r3, [r3, #504]
	cmn	r3, #1
	moveq	r0, r3
	beq	.L3620
	cmp	r0, #16
	mov	r7, r1
	bne	.L3621
	add	r0, r1, #256
	mov	r1, r2
	mov	r2, r8
	bl	FtlVendorPartRead
	b	.L3620
.L3621:
	ldr	r3, [sp, #36]
	add	r3, r1, r3
	str	r3, [sp, #40]
	ldr	r3, .L3667+8
	ldr	r1, [sp, #40]
	ldr	r2, [r3, #1912]
	cmp	r1, r2
	mvnhi	r0, #0
	bhi	.L3620
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
	ldr	r3, .L3667+12
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
	beq	.L3622
	bl	FtlCacheWriteBack
.L3622:
	ldr	r4, .L3667+12
	mov	r9, #0
	mov	r5, r10
	mov	r6, r9
	str	r9, [sp, #48]
	str	r9, [sp, #52]
.L3623:
	ldr	r3, [sp, #28]
	cmp	r3, #0
	beq	.L3666
	mov	r0, r5
	add	r1, sp, #72
	mov	r2, #0
	bl	log2phys
	ldr	r3, [sp, #72]
	cmn	r3, #1
	bne	.L3662
	mov	fp, #0
.L3624:
	ldr	r3, .L3667+16
	ldrh	r0, [r3]
	cmp	fp, r0
	bcs	.L3628
	mla	r0, r0, r5, fp
	ldr	r2, [sp, #40]
	cmp	r0, r7
	movcs	r3, #1
	movcc	r3, #0
	cmp	r0, r2
	movcs	r3, #0
	cmp	r3, #0
	beq	.L3626
	rsb	r0, r7, r0
	mov	r1, #0
	mov	r2, #512
	add	r0, r8, r0, asl #9
	bl	ftl_memset
.L3626:
	add	fp, fp, #1
	b	.L3624
.L3662:
	ldr	r2, [r4, #-2076]
	mov	fp, #36
	cmp	r5, r10
	mla	fp, fp, r6, r2
	str	r3, [fp, #4]
	bne	.L3629
	ldr	r3, [r4, #-2048]
	mov	r0, r7
	str	r3, [fp, #8]
	ldr	r3, .L3667+16
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
	b	.L3630
.L3629:
	ldr	r3, [sp, #32]
	cmp	r5, r3
	bne	.L3631
	ldr	r3, [r4, #-2044]
	ldr	r1, [sp, #40]
	str	r3, [fp, #8]
	ldr	r3, .L3667+16
	ldrh	r2, [r3]
	mul	r3, r2, r5
	rsb	r9, r3, r1
	cmp	r9, r2
	bne	.L3630
	b	.L3664
.L3631:
	ldr	r3, .L3667+16
	ldrh	r3, [r3]
	mul	r3, r3, r5
.L3664:
	rsb	r3, r7, r3
	add	r3, r8, r3, asl #9
	str	r3, [fp, #8]
.L3630:
	ldr	r3, .L3667+20
	ldr	r2, [r4, #-2036]
	str	r5, [fp, #16]
	ldrh	r3, [r3]
	mul	r3, r3, r6
	add	r6, r6, #1
	bic	r3, r3, #3
	add	r3, r2, r3
	str	r3, [fp, #12]
.L3628:
	ldr	r3, [sp, #28]
	add	r5, r5, #1
	subs	r3, r3, #1
	str	r3, [sp, #28]
	beq	.L3632
	ldr	r3, .L3667+24
	ldrh	r3, [r3]
	cmp	r6, r3, asl #3
	bne	.L3623
.L3632:
	cmp	r6, #0
	beq	.L3623
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
.L3639:
	ldr	r3, [sp, #44]
	mov	ip, #36
	mul	fp, ip, r3
	ldr	r3, [r4, #-2076]
	add	r3, r3, fp
	ldr	r2, [r3, #16]
	cmp	r2, r10
	bne	.L3634
	ldr	r1, [r3, #8]
	ldr	r3, [r4, #-2048]
	cmp	r1, r3
	bne	.L3635
	ldr	r3, [sp, #60]
	mov	r0, r8
	ldr	r2, [sp, #64]
	add	r1, r1, r3
	b	.L3665
.L3634:
	ldr	r1, [sp, #32]
	cmp	r2, r1
	bne	.L3635
	ldr	r1, [r3, #8]
	ldr	r3, [r4, #-2044]
	cmp	r1, r3
	bne	.L3635
	ldr	r3, .L3667+16
	ldr	r2, [sp, #68]
	ldrh	r0, [r3]
	ldr	r3, [sp, #32]
	mul	r0, r0, r3
	rsb	r0, r7, r0
	add	r0, r8, r0, asl #9
.L3665:
	bl	ftl_memcpy
.L3635:
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
	beq	.L3637
	ldr	r2, [r4, #-3676]
	ldr	r0, .L3667+28
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
.L3637:
	ldr	r3, [r4, #-2076]
	add	r2, r3, fp
	ldr	r3, [r3, fp]
	cmp	r3, #256
	bne	.L3638
	ldr	r0, [r2, #4]
	ubfx	r0, r0, #10, #16
	bl	P2V_block_in_plane
	bl	FtlGcRefreshBlock
.L3638:
	ldr	r3, [sp, #44]
	add	r3, r3, #1
	str	r3, [sp, #44]
	cmp	r3, r6
	bne	.L3639
	mov	r6, #0
	b	.L3623
.L3666:
	ldr	r3, .L3667+32
	ldrh	r3, [r3, #-6]
	cmp	r3, #0
	beq	.L3641
	ldr	r0, [sp, #28]
	mov	r1, #1
	bl	ftl_do_gc
.L3641:
	ldr	r0, [sp, #52]
.L3620:
	ldr	r3, .L3667
	ldr	r2, [sp, #76]
	ldr	r3, [r3]
	cmp	r2, r3
	beq	.L3642
	bl	__stack_chk_fail
.L3642:
	add	sp, sp, #84
	@ sp needed
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L3668:
	.align	2
.L3667:
	.word	__stack_chk_guard
	.word	.LANCHOR1
	.word	.LANCHOR0
	.word	.LANCHOR2
	.word	.LANCHOR0+1878
	.word	.LANCHOR0+1884
	.word	.LANCHOR0+1804
	.word	.LC144
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
	ldr	r3, .L3675
	ldr	r3, [r3, #504]
	cmp	r3, #1
	mvnne	r4, #0
	bne	.L3672
	mov	r2, r4
	mov	r0, r6
	mov	r1, r5
	bl	ftl_vendor_read
	mov	r4, r0
.L3672:
	bl	rknand_device_unlock
	mov	r0, r4
	ldmfd	sp!, {r4, r5, r6, pc}
.L3676:
	.align	2
.L3675:
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
	ldr	r2, .L3698
	ldr	r5, .L3698+4
	ldr	r6, .L3698+8
	ldr	r1, .L3698+12
	ldr	r0, .L3698+16
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
	ldrne	r0, .L3698+20
	bne	.L3697
	bl	FtlSysBlkInit
	subs	r4, r0, #0
	beq	.L3684
	ldr	r0, .L3698+24
.L3697:
	ldr	r1, .L3698+28
	bl	printk
	b	.L3683
.L3684:
	mov	r1, #1
	str	r1, [r6, #504]
	bl	ftl_do_gc
	ldr	r3, .L3698+32
	ldrh	r7, [r3, #-12]
	cmp	r7, #15
	bhi	.L3685
	ldr	r8, .L3698+36
	add	r5, r3, #232
	sub	r6, r3, #12
.L3688:
	ldrh	r3, [r5]
	movw	r2, #65535
	cmp	r3, r2
	bne	.L3686
	ldrh	r2, [r8]
	cmp	r2, r3
	bne	.L3686
	and	r0, r4, #7
	bl	List_get_gc_head_node
	uxth	r0, r0
	bl	FtlGcRefreshBlock
.L3686:
	mov	r0, #1
	mov	r1, r0
	bl	ftl_do_gc
	mov	r0, #0
	mov	r1, #1
	bl	ftl_do_gc
	ldrh	r2, [r6]
	add	r3, r7, #2
	cmp	r2, r3
	bhi	.L3683
	add	r4, r4, #1
	cmp	r4, #1024
	bne	.L3688
	b	.L3683
.L3685:
	ldrb	r3, [r5, #140]	@ zero_extendqisi2
	cmp	r3, #0
	beq	.L3683
	mov	r4, #128
.L3690:
	mov	r0, #1
	mov	r1, r0
	bl	ftl_do_gc
	subs	r4, r4, #1
	bne	.L3690
.L3683:
	mov	r0, #0
	ldmfd	sp!, {r4, r5, r6, r7, r8, pc}
.L3699:
	.align	2
.L3698:
	.word	.LANCHOR4
	.word	.LANCHOR0
	.word	.LANCHOR1
	.word	.LC77
	.word	.LC76
	.word	.LC156
	.word	.LC157
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
	ldr	r6, .L3705
	ldr	r4, .L3705+4
	mov	r5, #0
	mov	r1, r6
	str	r5, [r1, #468]!
	str	r0, [r6, #464]
	mov	r0, r4
	str	r5, [r0, #144]!
	bl	rknand_get_reg_addr
	ldr	r3, [r4, #144]
	cmp	r3, r5
	beq	.L3703
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
	bne	.L3702
	bl	FtlInit
.L3702:
	mov	r1, r4
	ldr	r0, .L3705+8
	bl	printk
	mov	r0, r4
	ldmfd	sp!, {r4, r5, r6, pc}
.L3703:
	mvn	r0, #0
	ldmfd	sp!, {r4, r5, r6, pc}
.L3706:
	.align	2
.L3705:
	.word	.LANCHOR4
	.word	.LANCHOR2
	.word	.LC158
	.fnend
	.size	rk_ftl_init, .-rk_ftl_init
	.align	2
	.global	ftl_fix_nand_power_lost_error
	.type	ftl_fix_nand_power_lost_error, %function
ftl_fix_nand_power_lost_error:
	.fnstart
	@ args = 0, pretend = 0, frame = 64
	@ frame_needed = 0, uses_anonymous_args = 0
	ldr	r3, .L3724
	stmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.save {r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.pad #68
	sub	sp, sp, #68
	ldr	r2, [r3]
	str	r3, [sp]
	str	r2, [sp, #60]
	ldr	r2, .L3724+4
	ldrb	r1, [r2, #140]	@ zero_extendqisi2
	cmp	r1, #0
	beq	.L3707
	ldr	r9, .L3724+8
	mov	r6, r2
	ldr	r4, .L3724+12
	movw	r5, #4097
	ldr	r0, .L3724+16
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
	ldr	r0, .L3724+20
	bl	allocate_new_data_superblock
	ldr	r0, .L3724+24
	bl	allocate_new_data_superblock
	str	r9, [sp, #4]
.L3709:
	subs	r5, r5, #1
	beq	.L3713
	mov	r0, #1
	mov	r1, r0
	bl	ftl_do_gc
	ldr	r3, [r4, #-4060]
	ldrh	r3, [r3, r7]
	cmp	r3, #0
	bne	.L3709
.L3713:
	ldr	r3, [r4, #-4060]
	mov	r1, r8
	ldr	r0, .L3724+16
	ldrh	r2, [r3, r7]
	bl	printk
	ldr	r3, [r4, #-4060]
	ldrh	r5, [r3, r7]
	cmp	r5, #0
	bne	.L3711
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
.L3714:
	uxth	r2, r3
	cmp	r2, lr
	bcs	.L3723
	ldrh	r2, [r0, #2]!
	add	r3, r3, #1
	cmp	r2, r10
	movne	r2, r2, asl #10
	mlane	r1, fp, r5, r9
	addne	r5, r5, #1
	uxthne	r5, r5
	stmneib	r1, {r2, ip}
	strne	ip, [r1, #12]
	b	.L3714
.L3723:
	ldr	r3, [r4, #-4060]
	mov	r1, r8
	ldr	r0, .L3724+28
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
.L3711:
	ldr	r2, [sp, #4]
	mvn	r3, #0
	strh	r3, [r2]	@ movhi
.L3707:
	ldr	r3, [sp]
	ldr	r2, [sp, #60]
	ldr	r3, [r3]
	cmp	r2, r3
	beq	.L3717
	bl	__stack_chk_fail
.L3717:
	add	sp, sp, #68
	@ sp needed
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L3725:
	.align	2
.L3724:
	.word	__stack_chk_guard
	.word	.LANCHOR0
	.word	.LANCHOR4+308
	.word	.LANCHOR2
	.word	.LC159
	.word	.LANCHOR2-4040
	.word	.LANCHOR2-3992
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
	@ args = 0, pretend = 0, frame = 104
	@ frame_needed = 0, uses_anonymous_args = 0
	stmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.save {r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.pad #108
	sub	sp, sp, #108
	ldr	r9, .L3801
	str	r3, [sp, #4]
	ldr	r3, .L3801+4
	ldr	r3, [r3]
	str	r3, [sp, #100]
	ldr	r3, [r9, #4052]
	cmp	r3, #0
	bne	.L3769
	mov	r8, r2
	ldr	r2, .L3801+8
	ldr	r2, [r2, #504]
	cmn	r2, #1
	moveq	r0, r3
	beq	.L3728
	cmp	r0, #16
	mov	r7, r1
	bne	.L3729
	add	r0, r1, #256
	ldr	r2, [sp, #4]
	mov	r1, r8
	bl	FtlVendorPartWrite
	b	.L3728
.L3729:
	ldr	r3, [r9, #1912]
	add	r5, r1, r8
	cmp	r5, r3
	mvnhi	r0, #0
	bhi	.L3728
	ldr	r6, .L3801+12
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
	ldr	r3, .L3801+16
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
	beq	.L3731
	mov	r2, #36
	ldr	r10, [r9, #1928]
	mul	r2, r2, r1
	ldr	r1, [sp, #8]
	sub	r2, r2, #36
	add	r10, r10, r2
	ldr	r2, [r10, #16]
	cmp	r1, r2
	bne	.L3732
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
	bne	.L3733
	ldr	r3, [r6, #476]
	cmp	r3, #2
	ble	.L3769
.L3733:
	ldr	r3, [sp, #4]
	rsb	r8, r4, r8
	add	r7, r7, r4
	str	r5, [sp]
	add	r3, r3, r9
	str	r3, [sp, #4]
	ldr	r3, [sp, #8]
	add	r3, r3, #1
	str	r3, [sp, #8]
.L3732:
	mov	r3, #0
	str	r3, [r6, #476]
.L3731:
	ldr	r0, [sp, #8]
	ldr	r1, [sp, #28]
	bl	FtlCacheMetchLpa
	cmp	r0, #0
	beq	.L3734
	bl	FtlCacheWriteBack
.L3734:
	ldr	r5, .L3801+20
	mov	r3, #0
	ldr	r4, .L3801
	str	r3, [sp, #12]
	str	r5, [r6, #452]
	ldr	r6, [sp, #8]
	mov	r10, r4
	str	r3, [sp, #32]
.L3735:
	ldr	r3, [sp]
	cmp	r3, #0
	beq	.L3800
	ldrh	r2, [r5, #4]
	cmp	r2, #0
	bne	.L3736
	ldr	r3, .L3801+20
	ldr	r9, .L3801+8
	cmp	r5, r3
	bne	.L3737
	add	r0, r3, #48
	ldrh	r5, [r0, #4]
	cmp	r5, #0
	bne	.L3738
	bl	allocate_new_data_superblock
	str	r5, [r9, #3292]
.L3738:
	ldr	r0, .L3801+20
	bl	allocate_new_data_superblock
	ldr	r3, [r9, #3292]
	cmp	r3, #0
	ldrne	r5, .L3801+24
	bne	.L3739
.L3740:
	ldr	r5, .L3801+20
	b	.L3739
.L3737:
	ldrh	r3, [r3, #4]
	str	r2, [r9, #3292]
	cmp	r3, #0
	bne	.L3740
	mov	r0, r5
	bl	allocate_new_data_superblock
.L3739:
	ldrh	r3, [r5, #4]
	cmp	r3, #0
	bne	.L3741
	mov	r0, r5
	bl	allocate_new_data_superblock
.L3741:
	ldr	r3, .L3801+12
	str	r5, [r3, #452]
.L3736:
	ldr	r2, .L3801+16
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
.L3798:
	str	r3, [sp, #16]
	ldr	r3, [sp, #16]
	ldr	r2, [sp, #44]
	cmp	r3, r2
	beq	.L3743
	ldrh	r3, [r5, #4]
	cmp	r3, #0
	beq	.L3743
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
	beq	.L3744
	ldr	r3, .L3801+28
	ldrh	r2, [r3]
	add	r3, r8, r7
	mls	r3, r2, r6, r3
	cmp	r3, r2
	bne	.L3743
.L3744:
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
	ldr	fp, .L3801+32
	mla	r1, r3, r2, r1
	ldrh	r2, [fp]
	str	r6, [r1, #16]
	str	r3, [sp, #48]
	ldr	r3, [r4, #1924]
	str	r0, [r1, #4]
	ldr	r0, .L3801+16
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
	beq	.L3745
	ldr	r3, [sp, #24]
	cmp	r3, #0
	beq	.L3746
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
	b	.L3747
.L3746:
	cmp	ip, #0
	beq	.L3747
	ldr	r2, .L3801+28
	add	r3, r8, r7
	ldrh	r2, [r2]
	smulbb	r2, r2, r6
	rsb	r3, r2, r3
	uxth	r3, r3
	str	r3, [sp, #12]
	ldr	r3, [sp, #24]
	str	r3, [sp, #32]
.L3747:
	ldr	r3, .L3801+28
	ldr	r2, [sp, #12]
	ldrh	r3, [r3]
	cmp	r2, r3
	bne	.L3748
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
	bne	.L3751
	ldr	r0, [r3, #8]
	ldr	r3, .L3801+36
	ldrh	r2, [r3]
	b	.L3796
.L3748:
	ldr	r2, [sp, #60]
	mov	r3, #36
	cmn	r2, #1
	beq	.L3752
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
	ldr	r3, .L3801+16
	ldreq	r2, [r3, #-3676]
	addeq	r2, r2, #1
	streq	r2, [r3, #-3676]
	beq	.L3755
	ldr	r2, [r9, #8]
	cmp	r2, r6
	beq	.L3755
	ldr	r2, [r3, #-3676]
	ldr	r0, .L3801+40
	add	r2, r2, #1
	str	r2, [r3, #-3676]
	mov	r2, r6
	ldr	r1, [r9, #8]
	bl	printk
	b	.L3755
.L3752:
	ldr	r2, [r4, #1924]
	ldr	r1, [r4, #1928]
	mla	r3, r3, r2, r1
	mov	r1, #0
	ldr	r0, [r3, #8]
	ldr	r3, .L3801+36
	ldrh	r2, [r3]
	bl	ftl_memset
.L3755:
	ldr	r3, [sp, #24]
	cmp	r3, #0
	mov	r3, #36
	beq	.L3756
	ldr	r1, [r4, #1928]
	ldr	r2, [r4, #1924]
	mla	r3, r3, r2, r1
	ldr	r1, [sp, #4]
	ldr	r0, [r3, #8]
	ldr	r3, [sp, #32]
	add	r0, r0, r3, asl #9
	b	.L3799
.L3756:
	ldr	r1, [r4, #1924]
	ldr	r2, [r4, #1928]
	mla	r3, r3, r1, r2
	ldr	r2, .L3801+28
	ldrh	r1, [r2]
	ldr	r0, [r3, #8]
	mul	r1, r1, r6
	ldr	r3, [sp, #4]
	rsb	r1, r7, r1
	add	r1, r3, r1, asl #9
.L3799:
	ldr	r3, [sp, #12]
	mov	r2, r3, asl #9
	b	.L3796
.L3745:
	ldr	r2, [sp, #20]
	cmp	r2, #0
	ldr	r2, [r4, #1924]
	beq	.L3757
	ldr	r1, [r4, #1928]
	mla	r3, r3, r2, r1
	ldr	r2, .L3801+28
	ldrh	fp, [r2]
	ldr	r2, [sp, #4]
	mul	fp, fp, r6
	rsb	fp, r7, fp
	add	fp, r2, fp, asl #9
	str	fp, [r3, #8]
	b	.L3751
.L3757:
	ldr	r0, [r4, #1928]
	mla	r3, r3, r2, r0
	ldr	r2, .L3801+28
	ldrh	r1, [r2]
	ldrh	r2, [fp, #-2]
	ldr	r0, [r3, #8]
	mul	r1, r1, r6
	ldr	r3, [sp, #4]
	rsb	r1, r7, r1
	add	r1, r3, r1, asl #9
.L3796:
	bl	ftl_memcpy
.L3751:
	ldr	r3, .L3801+44
	ldr	r2, [sp, #40]
	ldr	r1, [sp, #36]
	strh	r3, [r2, r1]	@ movhi
	ldr	r2, .L3801+16
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
	b	.L3798
.L3743:
	ldr	r3, [sp]
	ldr	r2, [sp, #16]
	ldr	r1, [sp, #20]
	rsb	r3, r2, r3
	ldr	r2, .L3801+16
	str	r3, [sp]
	ldr	r3, [r4, #1924]
	ldr	r2, [r2, #-2080]
	cmp	r3, r2
	orrcs	r1, r1, #1
	cmp	r1, #0
	bne	.L3761
	ldrh	r3, [r5, #4]
	cmp	r3, #0
	beq	.L3761
.L3763:
	mov	r3, #0
	str	r3, [sp, #20]
	b	.L3735
.L3761:
	bl	FtlCacheWriteBack
	mov	r3, #0
	str	r3, [r10, #1924]
	ldr	r3, [sp]
	cmp	r3, #1
	bhi	.L3735
	b	.L3763
.L3800:
	mov	r0, r3
	ldr	r2, [sp, #8]
	ldr	r3, [sp, #28]
	rsb	r1, r2, r3
	bl	ftl_do_gc
	ldr	r3, .L3801+48
	ldrh	r3, [r3, #-12]
	cmp	r3, #5
	bls	.L3774
	cmp	r3, #31
	bhi	.L3769
	ldr	r3, .L3801
	ldrb	r3, [r3]	@ zero_extendqisi2
	cmp	r3, #0
	bne	.L3769
.L3774:
	ldr	r5, .L3801+52
	ldr	r4, [sp]
	ldr	r6, .L3801+56
	add	r7, r5, #620
.L3787:
	ldrh	r2, [r5]
	movw	r3, #65535
	cmp	r2, r3
	bne	.L3767
	ldrh	r3, [r6]
	cmp	r3, r2
	bne	.L3767
	ldrh	r2, [r7]
	cmp	r2, r3
	bne	.L3767
	and	r0, r4, #7
	bl	List_get_gc_head_node
	uxth	r0, r0
	bl	FtlGcRefreshBlock
.L3767:
	ldr	r3, .L3801+60
	mov	r0, #1
	mov	r1, r0
	mov	r2, #128
	strh	r2, [r3]	@ movhi
	strh	r2, [r3, #-2]	@ movhi
	bl	ftl_do_gc
	mov	r0, #0
	mov	r1, #1
	bl	ftl_do_gc
	ldr	r3, .L3801
	ldr	r3, [r3, #4052]
	cmp	r3, #0
	bne	.L3769
	ldr	r3, .L3801+64
	ldrh	r3, [r3]
	cmp	r3, #2
	bhi	.L3769
	add	r4, r4, #1
	cmp	r4, #256
	bne	.L3787
.L3769:
	mov	r0, #0
.L3728:
	ldr	r3, .L3801+4
	ldr	r2, [sp, #100]
	ldr	r3, [r3]
	cmp	r2, r3
	beq	.L3768
	bl	__stack_chk_fail
.L3768:
	add	sp, sp, #108
	@ sp needed
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L3802:
	.align	2
.L3801:
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
	.word	.LC161
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
	ldr	r3, .L3809
	ldr	r3, [r3, #504]
	cmp	r3, #1
	mvnne	r4, #0
	bne	.L3806
	mov	r2, r4
	mov	r0, r6
	mov	r1, r5
	bl	ftl_vendor_write
	mov	r4, r0
.L3806:
	bl	rknand_device_unlock
	mov	r0, r4
	ldmfd	sp!, {r4, r5, r6, pc}
.L3810:
	.align	2
.L3809:
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
	ldr	r4, .L3823
	.pad #28
	sub	sp, sp, #28
	ldr	r7, .L3823+4
	mov	r6, r0
	mov	r5, #0
	ldr	r3, [r4, #-2064]
	mov	r9, r4
	str	r3, [r4, #220]
	ldr	r3, [r4, #-2040]
	str	r3, [r4, #224]
.L3815:
	ldrh	r2, [r7]
	sxth	r3, r5
	cmp	r3, r2
	bge	.L3822
	mov	r1, #1
	ldr	r0, .L3823+8
	mov	r2, r1
	orr	r3, r3, r8
	str	r3, [r4, #216]
	bl	FlashReadPages
	ldr	r3, [r4, #224]
	mov	r1, r6
	ldr	r0, .L3823+12
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
	beq	.L3816
	ldr	r0, .L3823+16
	mov	r2, #4
	ldr	r1, [r9, #-2064]
	mov	r3, #768
	bl	rknand_print_hex
.L3816:
	add	r5, r5, #1
	b	.L3815
.L3822:
	add	sp, sp, #28
	@ sp needed
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, pc}
.L3824:
	.align	2
.L3823:
	.word	.LANCHOR2
	.word	.LANCHOR0+1874
	.word	.LANCHOR2+212
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
	movw	r3, #1812
	ldr	r6, .L3843
	.pad #52
	sub	sp, sp, #52
	ldr	r10, .L3843+4
	ldrh	r7, [r6, r3]
	add	fp, r10, #18
.L3826:
	ldrh	r3, [r10]
	ldr	r4, .L3843+8
	cmp	r3, r7
	bls	.L3839
	ldr	r1, .L3843+12
	mov	r8, #0
	ldr	r2, [r4, #-2076]
	mov	r5, r8
	ldr	r3, [r4, #-3212]
	ldrh	ip, [r1]
	ldr	r9, [r4, #-3208]
	ldrh	r1, [r1, #80]
	str	r1, [sp, #28]
.L3835:
	uxth	r1, r8
	cmp	r1, ip
	bcs	.L3841
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
	bne	.L3827
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
.L3827:
	add	r8, r8, #1
	b	.L3835
.L3841:
	cmp	r5, #0
	beq	.L3830
	ldr	r0, [r4, #-2076]
	mov	r1, r5
	mov	r2, #1
	mov	r8, #0
	bl	FlashReadPages
	mov	r9, #36
.L3831:
	uxth	r3, r8
	cmp	r3, r5
	bcs	.L3830
	ldr	r3, [r4, #-2076]
	ldr	r0, .L3843+16
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
	b	.L3831
.L3830:
	add	r7, r7, #1
	uxth	r7, r7
	b	.L3826
.L3839:
	ldr	r9, .L3843+20
	mov	r8, #0
.L3834:
	ldr	r3, .L3843+24
	sxth	r5, r8
	ldr	r7, .L3843+8
	ldrh	r3, [r3]
	cmp	r5, r3
	bge	.L3837
	mov	r5, r5, asl #1
	mov	r7, #0
.L3838:
	ldrh	r2, [r9]
	sxth	r3, r7
	add	r7, r7, #1
	cmp	r3, r2
	bge	.L3842
	ldr	r2, [r4, #-2012]
	mov	r1, #1
	ldr	r0, .L3843+28
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
	ldr	r0, .L3843+32
	str	r3, [sp, #12]
	ldr	r3, [r2]
	str	r3, [sp, #16]
	ldr	r3, [r2, #4]
	str	r3, [sp, #20]
	ldr	r2, [r4, #212]
	ldr	r3, [r4, #216]
	bl	printk
	b	.L3838
.L3842:
	add	r8, r8, #1
	b	.L3834
.L3837:
	ldr	r1, [r7, #-2012]
	movw	r4, #1908
	ldr	r3, [r6, #1900]
	mov	r2, #2
	ldr	r0, .L3843+36
	bl	rknand_print_hex
	ldr	r1, [r7, #-1992]
	ldrh	r3, [r6, r4]
	mov	r2, #4
	ldr	r0, .L3843+40
	bl	rknand_print_hex
	ldr	r0, .L3843+44
	ldr	r1, [r7, #-1988]
	mov	r2, #4
	ldrh	r3, [r6, r4]
	add	sp, sp, #52
	@ sp needed
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, fp, lr}
	b	rknand_print_hex
.L3844:
	.align	2
.L3843:
	.word	.LANCHOR0
	.word	.LANCHOR0+1814
	.word	.LANCHOR2
	.word	.LANCHOR0+1804
	.word	.LC164
	.word	.LANCHOR0+1874
	.word	.LANCHOR2-1928
	.word	.LANCHOR2+212
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
	ldr	r3, .L3847
	ldr	r2, [r3, #1744]
	ldr	r3, .L3847+4
	cmp	r2, r3
	bxne	lr
	b	flash_enter_slc_mode
.L3848:
	.align	2
.L3847:
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
	ldr	r3, .L3851
	ldr	r2, [r3, #1744]
	ldr	r3, .L3851+4
	cmp	r2, r3
	bxne	lr
	b	flash_exit_slc_mode
.L3852:
	.align	2
.L3851:
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
	ldr	r4, .L3862
	mov	r8, r1
	.pad #20
	sub	sp, sp, #20
	mov	r1, r6
	mov	fp, r2
	ldr	r0, .L3862+4
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
.L3854:
	cmp	r7, r8
	bcs	.L3861
	add	r2, r7, r10
	ldrb	r1, [r4, #140]	@ zero_extendqisi2
	ubfx	r2, r2, #2, #16
	rsb	r5, ip, #4
	cmp	r1, #0
	add	r3, r4, r2, asl #1
	uxth	r5, r5
	ldrh	r3, [r3, #144]
	beq	.L3855
	ldr	r1, .L3862
	ldr	r0, .L3862+8
	ldr	r1, [r1, #1744]
	cmp	r1, r0
	moveq	r3, r2
.L3855:
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
	ldr	r3, .L3862+12
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
	b	.L3854
.L3861:
	mov	r1, r6
	mov	r2, r8
	mov	r3, #0
	ldr	r0, .L3862+16
	bl	printk
	mov	r0, #0
	add	sp, sp, #20
	@ sp needed
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L3863:
	.align	2
.L3862:
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
	@ args = 0, pretend = 0, frame = 128
	@ frame_needed = 0, uses_anonymous_args = 0
	ldr	r3, .L3911
	stmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.save {r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.pad #140
	sub	sp, sp, #140
	ldr	r3, [r3]
	mov	r4, r0
	ldr	r7, .L3911+4
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
	beq	.L3889
	add	r4, r4, #508
	add	r4, r4, #3
	mov	r4, r4, lsr #9
	cmp	r4, #8
	bls	.L3887
	cmp	r4, #500
	bhi	.L3889
	b	.L3866
.L3887:
	mov	r4, #8
.L3866:
	ldr	r2, [r6]
	ldr	r3, .L3911+8
	cmp	r2, r3
	bne	.L3889
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
.L3870:
	ldr	r1, [r0, #-4]!
	cmp	r1, #0
	bne	.L3867
	ldr	r1, [r6, r3, asl #2]
	add	r3, r3, #1
	cmp	r3, #4096
	sub	r2, r2, #1
	movhi	r3, #0
	cmp	r2, #4096
	str	r1, [r0, #512]
	bne	.L3870
	b	.L3869
.L3867:
	ldr	r0, .L3911+12
	bl	printk
.L3869:
	mov	r1, r5
	mov	r3, #5
	ldr	r0, .L3911+16
	mov	r2, #4
	bl	rknand_print_hex
	ldrb	r2, [r7, #1]	@ zero_extendqisi2
	ldr	r1, [r6, #512]
	sub	r5, r5, #4
	ldr	r0, .L3911+20
	bl	printk
	ldr	r2, .L3911+24
	ldrh	r3, [r7, #138]
	mov	r1, r4
	ldr	r0, .L3911+28
	ldr	r2, [r2, #168]
	str	r2, [sp]
	mov	r2, r4
	bl	printk
	ldrb	r3, [r7, #1]	@ zero_extendqisi2
	ldr	r7, .L3911+4
	ldr	r2, [r6, #512]
	cmp	r2, r3
	strhi	r3, [r6, #512]
	mov	r3, r4, asl #7
	str	r3, [sp, #36]
	mov	r3, #0
	str	r3, [sp, #20]
	str	r3, [sp, #16]
.L3884:
	ldr	r2, [r5, #4]
	ldrb	r3, [r7, #1]	@ zero_extendqisi2
	ldr	r9, .L3911+4
	cmp	r2, r3
	bcs	.L3872
	ldr	r3, .L3911+24
	ldr	r3, [r3, #168]
	cmp	r2, r3
	bcc	.L3872
	ldr	r3, [sp, #32]
	ldr	r1, [sp, #32]
	cmp	r3, #1
	movls	r3, #0
	movhi	r3, #1
	str	r3, [sp, #44]
	ldr	r3, [sp, #16]
	cmp	r3, #0
	cmpne	r1, #1
	bls	.L3873
	ldr	r3, [r5]
	add	r3, r3, #1
	cmp	r2, r3
	beq	.L3872
.L3873:
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
	ldr	r0, .L3911+32
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
	bls	.L3874
	mov	r0, #0
	add	r1, r8, r9
	mov	r2, r0
	mov	r10, #2
	bl	FlashEraseBlock
.L3874:
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
.L3875:
	ldr	r3, [sp, #52]
	cmp	r9, r3
	bcs	.L3910
	add	r3, r9, ip
	ubfx	r3, r3, #2, #16
	cmp	r3, #0
	beq	.L3876
	add	r2, r3, #1
	add	r1, r7, r2, asl #1
	ldrh	r10, [r1, #144]
	ldrb	r1, [r7, #140]	@ zero_extendqisi2
	cmp	r1, #0
	beq	.L3877
	ldr	r1, [r7, #1744]
	ldr	r0, .L3911+36
	cmp	r1, r0
	moveq	r10, r2
.L3877:
	sub	r10, r10, #-1073741823
	mov	r10, r10, asl #2
	str	r10, [sp, #68]
.L3876:
	movw	r2, #61424
	str	r2, [sp, #72]
	add	r2, r7, r3, asl #1
	ldrh	r10, [r2, #144]
	ldrb	r2, [r7, #140]	@ zero_extendqisi2
	cmp	r2, #0
	beq	.L3878
	ldr	r2, [r7, #1744]
	ldr	r1, .L3911+36
	cmp	r2, r1
	moveq	r10, r3
.L3878:
	ldr	r3, [sp, #24]
	add	r9, r9, #4
	ldr	r2, [sp, #40]
	str	ip, [sp, #60]
	uxth	r9, r9
	mla	r2, r3, r10, r2
	ldr	r3, .L3911+4
	ldrb	r3, [r3, #1792]	@ zero_extendqisi2
	str	r2, [sp, #56]
	str	r3, [sp, #48]
	ldr	r3, .L3911+24
	ldrb	r0, [r3, #174]	@ zero_extendqisi2
	bl	FlashBchSel
	mov	r0, #0
	bl	flash_boot_enter_slc_mode
	ldr	r3, .L3911+4
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
	b	.L3875
.L3910:
	mov	r1, r8
	mov	r3, #0
	mov	r2, r4
	ldr	r0, .L3911+40
	bl	printk
	ldr	r0, [r5, #4]
	mov	r1, r4
	mov	r8, #0
	mul	r0, r0, fp
	ldr	r2, [sp, #12]
	bl	IdBlockReadData
	mov	r3, r6
	ldr	r2, [sp, #12]
.L3883:
	mov	r10, r2
	mov	r9, r3
	ldr	r0, [r10]
	add	r2, r2, #4
	ldr	r1, [r9]
	add	r3, r3, #4
	cmp	r0, r1
	beq	.L3880
	mov	r1, #0
	mov	r2, #512
	ldr	r0, [sp, #12]
	bl	memset
	ldr	r3, [r10]
	ldr	r0, .L3911+44
	str	r3, [sp]
	ldr	r3, [r9]
	bic	r9, r8, #255
	ldr	r1, [sp, #16]
	mov	r9, r9, asl #2
	str	r3, [sp, #4]
	mov	r3, r8
	ldr	r2, [r5, #4]
	bl	printk
	ldr	r0, .L3911+48
	add	r1, r6, r9
	mov	r2, #4
	mov	r3, #256
	bl	rknand_print_hex
	mov	r2, #4
	ldr	r0, .L3911+52
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
	beq	.L3881
	ldr	r1, [r5, #4]
	mov	r0, #0
	mov	r2, r0
	mla	r1, r1, fp, fp
	bl	FlashEraseBlock
.L3881:
	mov	r0, #0
	bl	flash_boot_exit_slc_mode
	ldr	r0, .L3911+56
	ldr	r1, [r5, #4]
	bl	printk
	ldr	r3, [sp, #36]
	cmp	r8, r3
	bcc	.L3872
	b	.L3882
.L3880:
	ldr	r1, [sp, #36]
	add	r8, r8, #1
	cmp	r8, r1
	bne	.L3883
.L3882:
	ldr	r3, [sp, #20]
	add	r3, r3, #1
	str	r3, [sp, #20]
.L3872:
	ldr	r3, [sp, #16]
	add	r5, r5, #4
	add	r3, r3, #1
	str	r3, [sp, #16]
	cmp	r3, #5
	bne	.L3884
	ldr	r0, [sp, #12]
	bl	ftl_free
	ldr	r3, [sp, #20]
	clz	r0, r3
	mov	r0, r0, lsr #5
	rsb	r0, r0, #0
	b	.L3865
.L3889:
	mvn	r0, #0
.L3865:
	ldr	r3, .L3911
	ldr	r2, [sp, #132]
	ldr	r3, [r3]
	cmp	r2, r3
	beq	.L3885
	bl	__stack_chk_fail
.L3885:
	add	sp, sp, #140
	@ sp needed
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L3912:
	.align	2
.L3911:
	.word	__stack_chk_guard
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
	ldr	ip, .L3917
	add	r1, r0, r1
	mov	r2, #0
.L3914:
	cmp	r0, r1
	beq	.L3916
	ldrb	r3, [r0], #1	@ zero_extendqisi2
	eor	r3, r3, r2, lsr #24
	add	r3, ip, r3, asl #2
	ldr	r3, [r3, #3296]
	eor	r2, r3, r2, asl #8
	b	.L3914
.L3916:
	mov	r0, r2
	bx	lr
.L3918:
	.align	2
.L3917:
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
	ldr	r6, .L3943
	.pad #56
	sub	sp, sp, #56
	mov	r5, r0
	mov	r7, r1
	mov	r9, r2
	ldr	r4, .L3943+4
	ldr	r3, [r6]
	str	r3, [sp, #52]
	bne	.L3920
	ldr	r2, [r2]
	ldr	r3, .L3943+8
	cmp	r2, r3
	bne	.L3920
	mov	r0, #256000
	mov	r3, #1
	strb	r3, [r4, #480]
	bl	ftl_malloc
	mov	r1, #0
	mov	r2, #256000
	str	r0, [r4, #484]
	bl	ftl_memset
	str	r5, [r4, #488]
.L3920:
	str	r7, [sp]
	mov	r3, r5
	ldr	r0, .L3943+12
	ldr	r1, [r4, #484]
	ldr	r2, [r9]
	bl	printk
	ldrb	r3, [r4, #480]	@ zero_extendqisi2
	ldr	r10, .L3943+4
	cmp	r3, #0
	beq	.L3919
	sub	r0, r5, #64
	ldr	r8, [r10, #484]
	cmp	r0, #500
	bcs	.L3922
	rsb	r2, r5, #564
	add	r0, r8, r0, asl #9
	cmp	r2, r7
	mov	r1, r9
	movcs	r2, r7
	mov	r2, r2, asl #9
	bl	ftl_memcpy
	b	.L3923
.L3922:
	cmp	r5, #564
	bcs	.L3931
.L3923:
	ldr	r3, [r4, #488]
	cmp	r3, r5
	beq	.L3929
	ldr	r2, .L3943+4
	mov	r3, #0
	cmp	r8, r3
	mov	r9, r3
	strb	r3, [r2, #480]
	beq	.L3930
	mov	r0, r8
	bl	ftl_free
.L3930:
	str	r9, [r4, #484]
.L3929:
	add	r5, r5, r7
	str	r5, [r4, #488]
	b	.L3919
.L3931:
	ldr	r3, .L3943+16
	ldr	r0, [r10, #488]
	ldr	r3, [r3, #692]
	sub	r0, r0, #64
	cmp	r0, #500
	ldrb	r3, [r3, #9]	@ zero_extendqisi2
	movcs	r0, #500
	cmp	r3, #4
	beq	.L3933
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
	b	.L3925
.L3933:
	mov	r3, #0
.L3924:
	cmp	r0, #256
	add	r1, sp, #12
	mov	r2, r3, asl #1
	movls	r2, r3
	str	r2, [r1, r3, asl #2]
	add	r3, r3, #1
	cmp	r3, #5
	bne	.L3924
.L3925:
	movw	r3, #63872
.L3928:
	ldr	r2, [r8, r3, asl #2]
	cmp	r2, #0
	addne	r3, r3, #128
	movne	r0, r3, asl #2
	bne	.L3927
.L3926:
	sub	r3, r3, #1
	cmp	r3, #4096
	bne	.L3928
	mov	r0, r0, asl #9
.L3927:
	mov	r1, r8
	add	r2, sp, #12
	bl	write_idblock
	ldr	r0, [r4, #484]
	mov	r8, #0
	strb	r8, [r4, #480]
	bl	ftl_free
	str	r8, [r4, #484]
	b	.L3929
.L3919:
	ldr	r2, [sp, #52]
	ldr	r3, [r6]
	cmp	r2, r3
	beq	.L3932
	bl	__stack_chk_fail
.L3932:
	add	sp, sp, #56
	@ sp needed
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, pc}
.L3944:
	.align	2
.L3943:
	.word	__stack_chk_guard
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
	beq	.L3946
	mov	r0, r1
	mov	r2, r3
	mov	r1, r6
	bl	write_loader_lba
.L3946:
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
	ldr	r5, .L4058
	mov	r4, r1
	mov	r6, r2
	ldr	r3, [r5]
	str	r3, [sp, #524]
	ldr	r3, .L4058+4
	cmp	r1, r3
	beq	.L3953
	bhi	.L3954
	sub	r3, r3, #3456
	sub	r3, r3, #5
	cmp	r1, r3
	beq	.L3955
	bhi	.L3956
	sub	r3, r3, #125
	cmp	r1, r3
	beq	.L3957
	bhi	.L3958
	sub	r3, r3, #237
	cmp	r1, r3
	bne	.L4015
	bl	rknand_dev_flush
	b	.L4046
.L3958:
	ldr	r3, .L4058+8
	cmp	r1, r3
	beq	.L3960
	add	r3, r3, #1
	cmp	r1, r3
	beq	.L3961
	b	.L4015
.L3956:
	ldr	r3, .L4058+12
	cmp	r1, r3
	beq	.L3962
	bhi	.L3963
	sub	r3, r3, #1952
	sub	r3, r3, #9
	cmp	r1, r3
	beq	.L3964
	b	.L4015
.L3963:
	ldr	r3, .L4058+16
	cmp	r1, r3
	beq	.L3962
	add	r3, r3, #10
	cmp	r1, r3
	beq	.L3962
	b	.L4015
.L3954:
	ldr	r3, .L4058+20
	cmp	r1, r3
	beq	.L3965
	bhi	.L3966
	sub	r3, r3, #78
	cmp	r1, r3
	beq	.L3967
	bcc	.L3968
	add	r3, r3, #21
	cmp	r1, r3
	beq	.L3969
	add	r3, r3, #56
	cmp	r1, r3
	beq	.L3970
	b	.L4015
.L3966:
	ldr	r3, .L4058+24
	cmp	r1, r3
	mov	r7, r3
	beq	.L3971
	bhi	.L3972
	sub	r3, r3, #956
	sub	r3, r3, #1
	cmp	r1, r3
	beq	.L3973
	add	r3, r3, #956
	cmp	r1, r3
	beq	.L3974
	b	.L4015
.L3972:
	ldr	r3, .L4058+28
	cmp	r1, r3
	beq	.L3974
	add	r3, r3, #1
	cmp	r1, r3
	beq	.L3971
	b	.L4015
.L3968:
	ldr	r0, .L4058+32
	bl	printk
	mov	r0, #4096
	bl	ftl_malloc
	subs	r4, r0, #0
	bne	.L3975
.L3980:
	mvn	r0, #11
	b	.L3952
.L3975:
	mov	r1, r6
	mov	r2, #512
	bl	rk_copy_from_user
	cmp	r0, #0
	bne	.L4052
	ldr	r0, .L4058+36
	ldmia	r4, {r1, r2}
	bl	printk
	ldr	r3, [r4, #4]
	cmp	r3, #8
	str	r3, [sp]
	bhi	.L4050
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
	beq	.L4045
	ldr	r0, .L4058+40
.L4048:
	bl	printk
.L4050:
	mov	r0, r4
.L4051:
	bl	ftl_free
.L4049:
	mvn	r0, #13
	b	.L3952
.L3967:
	ldr	r0, .L4058+44
	bl	printk
	mov	r0, #4096
	bl	ftl_malloc
	subs	r4, r0, #0
	beq	.L3980
	mov	r1, r6
	mov	r2, #4096
	bl	rk_copy_from_user
	cmp	r0, #0
	beq	.L3981
.L4052:
	ldr	r0, .L4058+48
	b	.L4048
.L3981:
	ldr	r6, .L4058+52
	ldr	r0, .L4058+56
	ldmia	r4, {r1, r2}
	bl	printk
	ldr	r7, [r6, #492]
	mov	r8, r6
	cmp	r7, #0
	bne	.L3982
	mov	r0, #260096
	bl	ftl_malloc
	cmp	r0, #0
	str	r0, [r6, #492]
	beq	.L4050
	mov	r1, r7
	mov	r2, #260096
	bl	memset
.L3982:
	ldr	r2, [r4, #4]
	movw	r3, #4088
	cmp	r2, r3
	bhi	.L4050
	ldr	r3, [r4]
	cmp	r3, #251904
	bhi	.L4050
	ldr	r0, [r8, #492]
	add	r1, r4, #8
	add	r0, r0, r3
	bl	memcpy
.L4045:
	mov	r0, r4
	bl	ftl_free
.L4046:
	mov	r4, #0
	b	.L3979
.L3970:
	ldr	r0, .L4058+60
	bl	printk
	mov	r0, #4096
	bl	ftl_malloc
	subs	r7, r0, #0
	beq	.L3980
	mov	r1, r6
	mov	r2, #28
	bl	rk_copy_from_user
	cmp	r0, #0
	beq	.L3985
	ldr	r0, .L4058+48
	bl	printk
	b	.L3986
.L3985:
	ldmia	r7, {r1, r2}
	ldr	r0, .L4058+64
	bl	printk
	ldr	r1, [r7]
	cmp	r1, #256000
	bhi	.L3986
	ldr	r6, .L4058+52
	ldr	r0, [r6, #492]
	cmp	r0, #0
	beq	.L3986
	bl	CRC_32
	ldr	r3, [r7, #4]
	cmp	r3, r0
	beq	.L3988
	mov	r0, r7
	bl	ftl_free
	b	.L4016
.L3988:
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
	b	.L3992
.L3986:
	mov	r0, r7
	b	.L4051
.L3969:
	ldr	r0, .L4058+68
	bl	printk
	mov	r0, #4096
	bl	ftl_malloc
	subs	r7, r0, #0
	beq	.L3980
	bl	ftl_read_flash_info
	mov	r0, r6
	mov	r1, r7
	mov	r2, #11
	b	.L4056
.L3953:
	ldr	r0, .L4058+72
	bl	printk
	bl	nand_blk_add_whole_disk
	mov	r0, #4096
	bl	ftl_malloc
	subs	r7, r0, #0
	beq	.L3980
	mov	r1, #0
	mov	r2, #64
	bl	ftl_memset
	mov	r0, r6
	mov	r1, r7
	mov	r2, #64
	b	.L4056
.L3965:
	ldr	r0, .L4058+76
	bl	printk
	ldr	r3, .L4058+52
	mov	r0, r6
	mov	r2, #4
	ldr	r3, [r3, #496]
	ldr	r3, [r3, #20]
	add	r1, sp, #528
	str	r3, [r1, #-528]!
.L4040:
	bl	rk_copy_to_user
	cmp	r0, #0
	bne	.L4049
	b	.L4046
.L3973:
	ldr	r0, .L4058+80
	bl	printk
	mov	r0, #4096
	bl	ftl_malloc
	subs	r7, r0, #0
	beq	.L3980
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
.L4056:
	bl	rk_copy_to_user
	subs	r4, r0, #0
	mov	r0, r7
	bne	.L4051
.L3992:
	bl	ftl_free
	b	.L3979
.L3957:
	ldr	r0, .L4058+84
	bl	printk
	mov	r1, r6
	mov	r2, #520
	add	r0, sp, #4
	bl	rk_copy_from_user
	subs	r4, r0, #0
	beq	.L3993
.L3998:
	ldr	r0, .L4058+48
	bl	printk
	b	.L4049
.L3993:
	ldr	r2, [sp, #4]
	ldr	r3, .L4058+88
	cmp	r2, r3
	beq	.L3994
.L3995:
	mvn	r4, #0
	b	.L3979
.L3994:
	ldr	r3, [sp, #8]
	cmp	r3, #512
	bhi	.L3995
	ldr	r7, .L4058+52
	mov	r2, #512
	add	r0, sp, #4
	ldr	r1, [r7, #496]
	bl	memcpy
	ldr	r2, [r7, #500]
	ldr	r3, .L4058+92
	cmp	r2, r3
	beq	.L3996
	add	r0, sp, #68
	mov	r1, r4
	mov	r2, #128
	str	r4, [sp, #12]
	str	r4, [sp, #16]
	bl	memset
.L3996:
	mov	r1, #0
	mov	r2, #256
	add	r0, sp, #260
	str	r1, [sp, #20]
	bl	memset
	b	.L4054
.L3960:
	ldr	r0, .L4058+96
	bl	printk
	mov	r1, r6
	mov	r2, #520
	add	r0, sp, #4
	bl	rk_copy_from_user
	cmp	r0, #0
	bne	.L3998
	ldr	r2, [sp, #4]
	ldr	r3, .L4058+88
	cmp	r2, r3
	bne	.L3995
	ldr	r3, [sp, #8]
	cmp	r3, #512
	bhi	.L3995
	ldr	r2, .L4058+52
	ldr	r3, .L4058+92
	ldr	r1, [r2, #500]
	cmp	r1, r3
	bne	.L4016
	ldr	r3, [sp, #16]
	sub	r1, r3, #1
	cmp	r1, #127
	mvnhi	r0, #2
	bhi	.L3952
	ldr	r4, [r2, #496]
	add	r1, sp, #68
	add	r0, r4, #64
	str	r3, [r4, #12]
	ldr	r2, [sp, #16]
	bl	memcpy
	mov	r0, #1
	mov	r1, r4
	b	.L4047
.L3964:
	ldr	r0, .L4058+100
	bl	printk
	mov	r1, r6
	mov	r2, #520
	add	r0, sp, #4
	bl	rk_copy_from_user
	cmp	r0, #0
	bne	.L3998
	ldr	r2, [sp, #4]
	ldr	r3, .L4058+104
	cmp	r2, r3
	bne	.L3995
	ldr	r3, [sp, #8]
	cmp	r3, #512
	bhi	.L3995
	ldr	r6, .L4058+52
	ldr	r3, [r6, #504]
	cmp	r3, #0
	bne	.L3999
.L4002:
	mov	r0, #0
	b	.L3952
.L3999:
	ldr	r3, [r6, #508]
	ldr	r2, .L4058+108
	ldr	r1, [r3]
	cmp	r1, r2
	beq	.L4000
	str	r2, [r3]
	mov	r2, #504
	ldr	r3, [r6, #508]
	str	r2, [r3, #4]
	mov	r2, #0
	str	r2, [r3, #8]
	str	r2, [r3, #12]
.L4000:
	ldr	r1, [r6, #508]
	mov	r4, #0
	mov	r0, r4
	str	r4, [r1, #16]
	bl	StorageSysDataStore
	ldr	r3, [r6, #496]
	ldr	r2, .L4058+88
	ldr	r1, [r3]
	cmp	r1, r2
	strne	r2, [r3]
	ldr	r7, [r6, #496]
	ldrne	r3, .L4058+52
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
	b	.L3979
.L3955:
	ldr	r0, .L4058+112
	bl	printk
	mov	r1, r6
	mov	r2, #520
	add	r0, sp, #4
	bl	rk_copy_from_user
	cmp	r0, #0
	bne	.L3998
	ldr	r2, [sp, #4]
	ldr	r3, .L4058+116
	cmp	r2, r3
	bne	.L3995
	ldr	r3, [sp, #8]
	cmp	r3, #512
	bhi	.L3995
	ldr	r6, .L4058+52
	ldr	r3, [r6, #504]
	cmp	r3, #1
	beq	.L4002
	ldr	r3, [r6, #508]
	mov	r4, #0
	ldr	r2, .L4058+108
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
	ldr	r2, .L4058+88
	ldr	r1, [r3]
	cmp	r1, r2
	strne	r2, [r3]
	ldr	r7, [r6, #496]
	ldrne	r3, .L4058+52
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
	b	.L3979
.L3961:
	ldr	r0, .L4058+120
	bl	printk
	mov	r1, r6
	mov	r2, #520
	add	r0, sp, #4
	bl	rk_copy_from_user
	cmp	r0, #0
	bne	.L3998
	ldr	r2, [sp, #4]
	ldr	r3, .L4058+124
	cmp	r2, r3
	bne	.L3995
	ldr	r2, [sp, #8]
	cmp	r2, #512
	addls	r0, sp, #12
	ldrls	r1, .L4058+128
	bls	.L4053
	b	.L3995
.L3962:
	ldr	r8, .L4058+16
	cmp	r4, r8
	ldreq	r0, .L4058+132
	beq	.L4043
	ldr	r3, .L4058+136
	cmp	r4, r3
	ldreq	r0, .L4058+140
	ldrne	r0, .L4058+144
.L4043:
	bl	printk
	mov	r1, r6
	mov	r2, #520
	add	r0, sp, #4
	bl	rk_copy_from_user
	cmp	r0, #0
	bne	.L3998
	ldr	r2, [sp, #4]
	ldr	r3, .L4058+148
	cmp	r2, r3
	bne	.L4049
	ldr	r3, .L4058+136
	ldr	r7, .L4058+52
	cmp	r4, r3
	bne	.L4008
	ldr	r3, [r7, #496]
	mov	r0, r6
	add	r1, sp, #4
	mov	r2, #16
	ldr	r3, [r3, #20]
	str	r3, [sp, #8]
	strb	r3, [sp, #12]
	bl	rk_copy_to_user
	cmp	r0, #0
	beq	.L3952
	b	.L4049
.L4008:
	ldr	r3, [r7, #1024]
	cmp	r3, #10
	bhi	.L4049
	ldr	r1, [r7, #496]
	ldr	r2, [sp, #8]
	ldr	r3, [r1, #24]
	cmp	r3, r2
	cmpne	r3, #0
	movne	r3, #1
	moveq	r3, #0
	beq	.L4009
	ldr	r0, .L4058+152
	mov	r1, r2
	bl	printk
	ldr	r3, [r7, #1024]
	add	r3, r3, #1
	str	r3, [r7, #1024]
	b	.L4049
.L4009:
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
	bne	.L4046
	mvn	r4, #1
	b	.L3979
.L3974:
	ldr	r0, .L4058+156
	bl	printk
	mov	r1, r6
	mov	r2, #520
	add	r0, sp, #4
	bl	rk_copy_from_user
	cmp	r0, #0
	bne	.L3998
	ldr	r2, [sp, #4]
	ldr	r3, .L4058+160
	cmp	r2, r3
	bne	.L3995
	ldr	r2, [sp, #8]
	cmp	r2, #504
	bhi	.L3995
	ldr	r3, .L4058+164
	add	r0, sp, #12
	cmp	r4, r3
	ldr	r3, .L4058+52
	ldreq	r1, [r3, #1028]
	ldrne	r1, [r3, #1032]
	add	r1, r1, #8
.L4053:
	bl	memcpy
.L4054:
	mov	r0, r6
	add	r1, sp, #4
	mov	r2, #520
	b	.L4040
.L3971:
	ldr	r0, .L4058+168
	bl	printk
	mov	r1, r6
	mov	r2, #520
	add	r0, sp, #4
	bl	rk_copy_from_user
	cmp	r0, #0
	bne	.L3998
	ldr	r2, [sp, #4]
	ldr	r3, .L4058+160
	cmp	r2, r3
	bne	.L3995
	ldr	r2, [sp, #8]
	cmp	r2, #504
	bhi	.L3995
	cmp	r4, r7
	add	r2, r2, #8
	ldr	r4, .L4058+52
	bne	.L4013
	add	r1, sp, #4
	ldr	r0, [r4, #1028]
	bl	memcpy
	mov	r0, #2
	ldr	r1, [r4, #1028]
	b	.L4047
.L4013:
	add	r1, sp, #4
	ldr	r0, [r4, #1032]
	bl	memcpy
	ldr	r1, [r4, #1032]
	mov	r0, #3
.L4047:
	bl	StorageSysDataStore
	mov	r4, r0
.L3979:
	ldr	r0, .L4058+172
	mov	r1, r4
	bl	printk
	mov	r0, r4
	b	.L3952
.L4015:
	mvn	r0, #21
	b	.L3952
.L4016:
	mvn	r0, #1
.L3952:
	ldr	r2, [sp, #524]
	ldr	r3, [r5]
	cmp	r2, r3
	beq	.L4014
	bl	__stack_chk_fail
.L4014:
	add	sp, sp, #528
	@ sp needed
	ldmfd	sp!, {r4, r5, r6, r7, r8, pc}
.L4059:
	.align	2
.L4058:
	.word	__stack_chk_guard
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
	.word	.LANCHOR4+512
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
	ldr	r4, .L4070
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
	beq	.L4061
	mov	r0, r6
	mov	r1, #508
	bl	js_hash
	cmp	r7, r0
	beq	.L4061
	str	r5, [r6, #16]
	ldr	r0, .L4070+4
	str	r5, [r4, #504]
	bl	printk
.L4061:
	ldr	r3, [r4, #504]
	mov	r0, #2
	ldr	r1, [r4, #1028]
	cmp	r3, #0
	ldrne	r3, .L4070
	ldrne	r2, .L4070+8
	strne	r2, [r3, #500]
	bl	StorageSysDataLoad
	ldr	r1, [r4, #1032]
	mov	r0, #3
	bl	StorageSysDataLoad
	ldmfd	sp!, {r3, r4, r5, r6, r7, lr}
	b	rknand_sys_storage_init
.L4071:
	.align	2
.L4070:
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
	ldr	r6, .L4084
	cmp	r0, #0
	str	r0, [r6, #1036]
	beq	.L4079
	ldr	r9, .L4084+4
	mov	r8, #0
	mov	r4, r8
	mov	r7, r8
	mov	r10, r6
.L4077:
	mov	r0, r7, asl #7
	mov	r1, #128
	ldr	r2, [r6, #1036]
	bl	FlashBootVendorRead
	cmp	r0, #0
	bne	.L4075
	ldr	r3, [r10, #1036]
	ldr	r0, .L4084+8
	add	r2, r3, #61440
	ldr	r1, [r3]
	ldr	r2, [r2, #4092]
	ldr	r3, [r3, #4]
	bl	printk
	ldr	r5, [r10, #1036]
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
	ldr	r0, [r6, #1036]
	bl	kfree
	mov	r3, #0
	mvn	r0, #0
	str	r3, [r6, #1036]
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, pc}
.L4079:
	mvn	r0, #11
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, r10, pc}
.L4085:
	.align	2
.L4084:
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
	ldr	r3, .L4093
	ldr	ip, [r3, #1036]
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
	ldr	r4, [r2, #1036]
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
	.type	__func__.20217, %object
	.size	__func__.20217, 11
__func__.20217:
	.ascii	"FtlMemInit\000"
.LC0:
	.byte	60
	.byte	40
	.byte	24
	.byte	16
	.space	1
	.type	__func__.20992, %object
	.size	__func__.20992, 17
__func__.20992:
	.ascii	"FtlDumpBlockInfo\000"
	.space	3
	.type	__func__.21011, %object
	.size	__func__.21011, 16
__func__.21011:
	.ascii	"FtlScanAllBlock\000"
	.type	__func__.21277, %object
	.size	__func__.21277, 17
__func__.21277:
	.ascii	"ftl_scan_all_ppa\000"
	.space	3
	.type	__func__.20960, %object
	.size	__func__.20960, 12
__func__.20960:
	.ascii	"FtlCheckVpc\000"
	.type	__func__.21259, %object
	.size	__func__.21259, 21
__func__.21259:
	.ascii	"FtlVpcCheckAndModify\000"
	.space	3
	.type	__func__.20290, %object
	.size	__func__.20290, 8
__func__.20290:
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
	.ascii	"FTL version: 5.0.57 20190927\000"
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
