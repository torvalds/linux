/*
 * Copyright (c) 2016-2017, Fuzhou Rockchip Electronics Co., Ltd
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 * date: 2017-11-20
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
	.eabi_attribute 18, 4
	.file	"rk_ftl_arm_v7.S"
	.text
	.align	2
	.type	FlashGetReadRetryDefault.part.27, %function
FlashGetReadRetryDefault.part.27:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	cmp	r0, #33
	cmpne	r0, #65
	stmfd	sp!, {r3, lr}
	.save {r3, lr}
	beq	.L2
	cmp	r0, #66
	bne	.L3
.L2:
	ldr	r3, .L8
	mov	r2, #4
	strb	r0, [r3, #0]
	b	.L7
.L3:
	cmp	r0, #34
	cmpne	r0, #67
	bne	.L5
	ldr	r3, .L8
	mov	r2, #5
	strb	r0, [r3, #0]
.L7:
	strb	r2, [r3, #1]
	add	r0, r3, #4
	mov	r2, #7
	ldr	r1, .L8+4
	strb	r2, [r3, #2]
	mov	r2, #45
	b	.L6
.L5:
	cmp	r0, #35
	cmpne	r0, #68
	ldmnefd	sp!, {r3, pc}
	ldr	r3, .L8
	mov	r2, #5
	ldr	r1, .L8+8
	strb	r0, [r3, #0]
	add	r0, r3, #4
	strb	r2, [r3, #1]
	mov	r2, #17
	strb	r2, [r3, #2]
	mov	r2, #95
.L6:
	bl	memcpy
	ldmfd	sp!, {r3, pc}
.L9:
	.align	2
.L8:
	.word	.LANCHOR0
	.word	.LANCHOR1
	.word	.LANCHOR1+45
	.fnend
	.size	FlashGetReadRetryDefault.part.27, .-FlashGetReadRetryDefault.part.27
	.align	2
	.global	FlashMemCmp8
	.type	FlashMemCmp8, %function
FlashMemCmp8:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	ldr	r3, .L20
	stmfd	sp!, {r4, lr}
	.save {r4, lr}
	ldrb	r3, [r3, #852]	@ zero_extendqisi2
	cmp	r3, #0
	beq	.L18
	ldrb	r3, [r1, #1]	@ zero_extendqisi2
	ldrb	ip, [r0, #1]	@ zero_extendqisi2
	cmp	ip, r3
	movne	r3, #0
	bne	.L18
	b	.L19
.L15:
	ldrb	r4, [r0, r3]	@ zero_extendqisi2
	ldrb	ip, [r1, r3]	@ zero_extendqisi2
	add	r3, r3, #1
	cmp	r4, ip
	beq	.L18
	mov	r0, r3
	ldmfd	sp!, {r4, pc}
.L18:
	cmp	r3, r2
	bne	.L15
	mov	r0, #0
	ldmfd	sp!, {r4, pc}
.L19:
	mov	r0, #0
	ldmfd	sp!, {r4, pc}
.L21:
	.align	2
.L20:
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
	ldr	r3, .L23
	ldrb	r2, [r3, #853]	@ zero_extendqisi2
	ldr	r3, [r3, #856]
	mul	r3, r3, r2
	cmp	r1, r3
	movcs	r2, #0
	movcc	r2, #1
	cmp	r0, #0
	movne	r2, #0
	eor	r0, r2, #1
	bx	lr
.L24:
	.align	2
.L23:
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
	and	r2, r1, #127
	ldr	r3, .L27
	stmfd	sp!, {r4, lr}
	.save {r4, lr}
	add	r3, r3, r2, asl #1
	ldrh	r4, [r3, #140]
	ldr	r3, .L27+4
	ldrb	r3, [r3, #860]	@ zero_extendqisi2
	cmp	r3, #0
	beq	.L26
	bl	FlashRsvdBlkChk
	cmp	r0, #0
	orrne	r4, r4, #-1073741824
.L26:
	mov	r0, r4
	ldmfd	sp!, {r4, pc}
.L28:
	.align	2
.L27:
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
	and	r2, r1, #127
	ldr	r3, .L31
	stmfd	sp!, {r4, r5, r6, lr}
	.save {r4, r5, r6, lr}
	add	r3, r3, r2, asl #1
	mov	r6, r0
	ldrh	r4, [r3, #140]
	ldr	r3, .L31+4
	ldrb	r2, [r3, #860]	@ zero_extendqisi2
	mov	r5, r3
	cmp	r2, #0
	beq	.L30
	bl	FlashRsvdBlkChk
	cmp	r0, #0
	orrne	r4, r4, #-1073741824
.L30:
	add	r5, r5, r6, asl #3
	ldr	r3, [r5, #864]
	str	r4, [r3, #336]
	ldmfd	sp!, {r4, r5, r6, pc}
.L32:
	.align	2
.L31:
	.word	.LANCHOR1
	.word	.LANCHOR0
	.fnend
	.size	FlashSetRandomizer, .-FlashSetRandomizer
	.align	2
	.global	FlashReadCmd
	.type	FlashReadCmd, %function
FlashReadCmd:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	ldr	ip, .L34
	mov	r2, r1, lsr #16
	stmfd	sp!, {r4, lr}
	.save {r4, lr}
	add	ip, ip, r0, asl #3
	ldrb	r3, [ip, #868]	@ zero_extendqisi2
	ldr	r4, [ip, #864]
	mov	ip, #0
	add	r3, r3, #8
	add	r3, r4, r3, asl #8
	str	ip, [r3, #8]
	str	ip, [r3, #4]
	str	ip, [r3, #4]
	uxtb	ip, r1
	str	ip, [r3, #4]
	mov	ip, r1, lsr #8
	str	ip, [r3, #4]
	str	r2, [r3, #4]
	mov	r2, #48
	str	r2, [r3, #8]
	ldmfd	sp!, {r4, lr}
	b	FlashSetRandomizer
.L35:
	.align	2
.L34:
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
	ldr	r2, .L40
	stmfd	sp!, {r4, lr}
	.save {r4, lr}
	add	ip, r2, r0, asl #3
	ldrb	r2, [r2, #912]	@ zero_extendqisi2
	ldrb	r3, [ip, #868]	@ zero_extendqisi2
	ldr	r4, [ip, #864]
	cmp	r2, #1
	add	r3, r3, #8
	mov	r2, #0
	add	r3, r4, r3, asl #8
	bne	.L37
	mov	ip, #6
	str	ip, [r3, #8]
	str	r2, [r3, #4]
	str	r2, [r3, #4]
	uxtb	r2, r1
	str	r2, [r3, #4]
	mov	r2, r1, lsr #8
	str	r2, [r3, #4]
	mov	r2, r1, lsr #16
	b	.L39
.L37:
	str	r2, [r3, #8]
	uxtb	ip, r1
	str	r2, [r3, #4]
	str	r2, [r3, #4]
	str	ip, [r3, #4]
	mov	ip, r1, lsr #8
	str	ip, [r3, #4]
	mov	ip, r1, lsr #16
	str	ip, [r3, #4]
	mov	ip, #5
	str	ip, [r3, #8]
	str	r2, [r3, #4]
.L39:
	str	r2, [r3, #4]
	mov	r2, #224
	str	r2, [r3, #8]
	ldmfd	sp!, {r4, lr}
	b	FlashSetRandomizer
.L41:
	.align	2
.L40:
	.word	.LANCHOR0
	.fnend
	.size	FlashReadDpDataOutCmd, .-FlashReadDpDataOutCmd
	.align	2
	.global	flash_enter_slc_mode
	.type	flash_enter_slc_mode, %function
flash_enter_slc_mode:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	@ link register save eliminated.
	ldr	r3, .L44
	ldrb	r2, [r3, #928]	@ zero_extendqisi2
	cmp	r2, #0
	bxeq	lr
	add	r0, r3, r0, asl #3
	ldrb	r3, [r0, #868]	@ zero_extendqisi2
	ldr	r2, [r0, #864]
	add	r3, r3, #8
	add	r3, r2, r3, asl #8
	mov	r2, #218
	str	r2, [r3, #8]
	bx	lr
.L45:
	.align	2
.L44:
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
	@ link register save eliminated.
	ldr	r3, .L48
	ldrb	r2, [r3, #928]	@ zero_extendqisi2
	cmp	r2, #0
	bxeq	lr
	add	r0, r3, r0, asl #3
	ldrb	r3, [r0, #868]	@ zero_extendqisi2
	ldr	r2, [r0, #864]
	add	r3, r3, #8
	add	r3, r2, r3, asl #8
	mov	r2, #223
	str	r2, [r3, #8]
	bx	lr
.L49:
	.align	2
.L48:
	.word	.LANCHOR0
	.fnend
	.size	flash_exit_slc_mode, .-flash_exit_slc_mode
	.align	2
	.global	FlashProgFirstCmd
	.type	FlashProgFirstCmd, %function
FlashProgFirstCmd:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	ldr	ip, .L51
	mov	r2, r1, lsr #16
	stmfd	sp!, {r4, lr}
	.save {r4, lr}
	add	ip, ip, r0, asl #3
	ldrb	r3, [ip, #868]	@ zero_extendqisi2
	ldr	r4, [ip, #864]
	mov	ip, #128
	add	r3, r3, #8
	add	r3, r4, r3, asl #8
	str	ip, [r3, #8]
	mov	ip, #0
	str	ip, [r3, #4]
	str	ip, [r3, #4]
	uxtb	ip, r1
	str	ip, [r3, #4]
	mov	ip, r1, lsr #8
	str	ip, [r3, #4]
	str	r2, [r3, #4]
	ldmfd	sp!, {r4, lr}
	b	FlashSetRandomizer
.L52:
	.align	2
.L51:
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
	@ link register save eliminated.
	ldr	r3, .L55
	cmp	r2, #0
	add	r0, r3, r0, asl #3
	ldr	ip, [r0, #864]
	ldrb	r0, [r0, #868]	@ zero_extendqisi2
	add	r0, r0, #8
	add	r0, ip, r0, asl #8
	beq	.L54
	ldr	r3, [r3, #856]
	mov	r2, #96
	str	r2, [r0, #8]
	uxtb	r2, r1
	str	r2, [r0, #4]
	mov	r2, r1, lsr #8
	str	r2, [r0, #4]
	mov	r2, r1, lsr #16
	add	r1, r1, r3
	str	r2, [r0, #4]
.L54:
	mov	r3, #96
	str	r3, [r0, #8]
	uxtb	r3, r1
	str	r3, [r0, #4]
	mov	r3, r1, lsr #8
	mov	r1, r1, lsr #16
	str	r3, [r0, #4]
	mov	r3, #208
	str	r1, [r0, #4]
	str	r3, [r0, #8]
	bx	lr
.L56:
	.align	2
.L55:
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
	ldr	ip, .L58
	mov	r2, r1, lsr #16
	stmfd	sp!, {r4, r5, lr}
	.save {r4, r5, lr}
	add	r4, ip, r0, asl #3
	ldrb	ip, [ip, #907]	@ zero_extendqisi2
	ldrb	r3, [r4, #868]	@ zero_extendqisi2
	ldr	r5, [r4, #864]
	add	r3, r3, #8
	add	r3, r5, r3, asl #8
	str	ip, [r3, #8]
	mov	ip, #0
	str	ip, [r3, #4]
	str	ip, [r3, #4]
	uxtb	ip, r1
	str	ip, [r3, #4]
	mov	ip, r1, lsr #8
	str	ip, [r3, #4]
	str	r2, [r3, #4]
	ldmfd	sp!, {r4, r5, lr}
	b	FlashSetRandomizer
.L59:
	.align	2
.L58:
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
	ldr	r3, .L61
	add	r3, r3, r0, asl #3
	ldr	r2, [r3, #864]
	ldrb	r3, [r3, #868]	@ zero_extendqisi2
	add	r3, r3, #8
	add	r3, r2, r3, asl #8
	mov	r2, #16
	str	r2, [r3, #8]
	bx	lr
.L62:
	.align	2
.L61:
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
	ldr	r3, .L64
	add	r0, r3, r0, asl #3
	ldrb	r3, [r3, #906]	@ zero_extendqisi2
	ldrb	r2, [r0, #868]	@ zero_extendqisi2
	ldr	r1, [r0, #864]
	add	r2, r2, #8
	add	r2, r1, r2, asl #8
	str	r3, [r2, #8]
	bx	lr
.L65:
	.align	2
.L64:
	.word	.LANCHOR0
	.fnend
	.size	FlashProgDpFirstCmd, .-FlashProgDpFirstCmd
	.align	2
	.global	JSHash
	.type	JSHash, %function
JSHash:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	mov	r3, r0
	mov	r2, #0
	stmfd	sp!, {r4, lr}
	.save {r4, lr}
	ldr	r0, .L69
	b	.L67
.L68:
	mov	ip, r0, asl #5
	ldrb	r4, [r3, r2]	@ zero_extendqisi2
	add	ip, ip, r0, lsr #2
	add	r2, r2, #1
	add	ip, ip, r4
	eor	r0, r0, ip
.L67:
	cmp	r2, r1
	bne	.L68
	ldmfd	sp!, {r4, pc}
.L70:
	.align	2
.L69:
	.word	1204201446
	.fnend
	.size	JSHash, .-JSHash
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
	.global	FlashReadIdbData
	.type	FlashReadIdbData, %function
FlashReadIdbData:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	stmfd	sp!, {r3, lr}
	.save {r3, lr}
	mov	r2, #2048
	ldr	r1, .L74
	bl	memcpy
	mov	r0, #0
	ldmfd	sp!, {r3, pc}
.L75:
	.align	2
.L74:
	.word	.LANCHOR0+932
	.fnend
	.size	FlashReadIdbData, .-FlashReadIdbData
	.align	2
	.global	FlashLoadPhyInfoInRam
	.type	FlashLoadPhyInfoInRam, %function
FlashLoadPhyInfoInRam:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	stmfd	sp!, {r4, r5, r6, lr}
	.save {r4, r5, r6, lr}
	mov	r4, #0
	ldr	r6, .L88
.L80:
	add	r5, r6, r4, asl #5
	ldr	r1, .L88+4
	add	r0, r5, #1
	ldrb	r2, [r6, r4, asl #5]	@ zero_extendqisi2
	bl	FlashMemCmp8
	subs	r1, r0, #0
	bne	.L77
	cmp	r5, #0
	ldrneb	r2, [r5, #22]	@ zero_extendqisi2
	ldrne	r3, .L88+8
	bne	.L82
	b	.L87
.L77:
	add	r4, r4, #1
	cmp	r4, #72
	bne	.L80
	mvn	r0, #0
	ldmfd	sp!, {r4, r5, r6, pc}
.L82:
	ldrb	r0, [r3, r1, asl #5]	@ zero_extendqisi2
	cmp	r0, r2
	beq	.L81
	add	r1, r1, #1
	cmp	r1, #4
	bne	.L82
.L81:
	ldr	r3, .L88+8
	mov	r2, #32
	ldr	r0, .L88+12
	add	r1, r3, r1, asl #5
	bl	memcpy
	ldr	r0, .L88+16
	mov	r1, r5
	mov	r2, #32
	bl	memcpy
	mov	r0, #0
	ldmfd	sp!, {r4, r5, r6, pc}
.L87:
	mvn	r0, #0
	ldmfd	sp!, {r4, r5, r6, pc}
.L89:
	.align	2
.L88:
	.word	.LANCHOR1+396
	.word	.LANCHOR0+2980
	.word	.LANCHOR1+2700
	.word	.LANCHOR0+896
	.word	.LANCHOR1+2828
	.fnend
	.size	FlashLoadPhyInfoInRam, .-FlashLoadPhyInfoInRam
	.align	2
	.global	FlashSuspend
	.type	FlashSuspend, %function
FlashSuspend:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	@ link register save eliminated.
	ldr	r3, .L91
	mov	r0, #0
	ldr	r2, [r3, #3012]
	ldr	r1, [r2, #0]
	str	r1, [r3, #3016]
	ldr	r1, [r2, #4]
	str	r1, [r3, #3020]
	ldr	r1, [r2, #8]
	str	r1, [r3, #3024]
	ldr	r1, [r2, #12]
	str	r1, [r3, #3028]
	ldr	r1, [r2, #304]
	str	r1, [r3, #3032]
	ldr	r1, [r2, #308]
	str	r1, [r3, #3036]
	ldr	r1, [r2, #336]
	ldr	r2, [r2, #344]
	str	r1, [r3, #3040]
	str	r2, [r3, #3044]
	bx	lr
.L92:
	.align	2
.L91:
	.word	.LANCHOR0
	.fnend
	.size	FlashSuspend, .-FlashSuspend
	.global	__aeabi_uidiv
	.align	2
	.global	LogAddr2PhyAddr
	.type	LogAddr2PhyAddr, %function
LogAddr2PhyAddr:
	.fnstart
	@ args = 4, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	stmfd	sp!, {r3, r4, r5, r6, r7, r8, r9, sl, fp, lr}
	.save {r3, r4, r5, r6, r7, r8, r9, sl, fp, lr}
	mov	fp, r3
	ldr	r3, .L98
	mov	r5, r1
	mov	r6, r2
	movw	r1, #3062
	movw	r2, #3060
	ldr	r8, [r0, #4]
	ldrh	r2, [r3, r2]
	mov	r4, r0
	ldrh	sl, [r3, r1]
	bic	r8, r8, #-2147483648
	ldr	r7, [r3, #856]
	ubfx	r9, r8, #10, #16
	ldrb	r3, [r3, #852]	@ zero_extendqisi2
	mov	r8, r8, asl #22
	mul	sl, sl, r2
	uxth	r7, r7
	cmp	r3, #1
	mov	r0, r9
	moveq	r7, r7, asl #1
	mov	r8, r8, lsr #22
	uxtheq	r7, r7
	uxth	sl, sl
	mov	r1, sl
	bl	__aeabi_uidiv
	cmp	r5, #1
	ldr	r3, .L98
	uxth	r0, r0
	mls	sl, sl, r0, r9
	uxth	sl, sl
	bne	.L95
	ldrb	r2, [r3, #928]	@ zero_extendqisi2
	cmp	r2, #0
	addeq	r8, r3, r8, asl #1
	movweq	r2, #3076
	ldreqh	r8, [r8, r2]
.L95:
	add	r3, r3, r0, asl #2
	ldr	r3, [r3, #3588]
	mla	r7, r7, sl, r3
	ldrb	r3, [sp, #40]	@ zero_extendqisi2
	cmp	r3, #1
	add	r8, r7, r8
	str	r8, [r6, #0]
	str	r0, [fp, #0]
	bls	.L97
	ldr	r0, [r4, #4]
	ldr	r3, [r4, #40]
	add	r0, r0, #1024
	rsb	r3, r3, r0
	rsbs	r0, r3, #0
	adc	r0, r0, r3
	ldmfd	sp!, {r3, r4, r5, r6, r7, r8, r9, sl, fp, pc}
.L97:
	mov	r0, #0
	ldmfd	sp!, {r3, r4, r5, r6, r7, r8, r9, sl, fp, pc}
.L99:
	.align	2
.L98:
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
	ldr	r3, .L101
	ldr	r2, [r3, #3620]
	str	r0, [r3, #3620]
	mov	r0, r2
	bx	lr
.L102:
	.align	2
.L101:
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
	ldr	r3, .L104
	ldr	r3, [r3, #3624]
	ldrb	r0, [r3, #9]	@ zero_extendqisi2
	bx	lr
.L105:
	.align	2
.L104:
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
	ldr	r3, .L108
	add	r3, r3, r0, asl #3
	ldrb	r0, [r3, #868]	@ zero_extendqisi2
	ldr	r2, [r3, #864]
	add	r0, r0, #8
	add	r0, r2, r0, asl #8
	bx	lr
.L109:
	.align	2
.L108:
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
	ldr	r3, .L111
	mov	r2, r0, asl #8
	orr	r2, r2, r0, asl #16
	orr	r2, r2, #1
	ldr	r3, [r3, #3012]
	str	r2, [r3, #304]
	bx	lr
.L112:
	.align	2
.L111:
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
	ldr	r3, .L114
	orr	r0, r0, #16640
	ldr	r3, [r3, #3012]
	str	r0, [r3, #344]
	bx	lr
.L115:
	.align	2
.L114:
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
	ldr	r2, [r3, #3012]
	ldr	r3, [r2, #0]
	bfc	r3, #13, #1
	orrne	r3, r3, #253952
	str	r3, [r2, #0]
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
	ldr	r3, .L124
	stmfd	sp!, {r4, lr}
	.save {r4, lr}
	ldr	r3, [r3, #3012]
	ldr	r4, [r3, #0]
	ands	r3, r0, #6
	bfieq	r4, r3, #13, #1
	beq	.L123
	orr	r4, r4, #24576
	tst	r0, #4
	bfc	r4, #15, #1
	mov	r0, #0
	orr	r4, r4, #196608
	orrne	r4, r4, #32768
	bl	rknand_get_clk_rate
	ldr	r3, .L124
	movw	r2, #16641
	ldr	r3, [r3, #3012]
	str	r2, [r3, #344]
	ldr	r2, .L124+4
	str	r2, [r3, #304]
	mov	r2, #38
	str	r2, [r3, #308]
	mov	r2, #39
	str	r2, [r3, #308]
.L123:
	ldr	r3, .L124
	mov	r0, #0
	ldr	r3, [r3, #3012]
	str	r4, [r3, #0]
	ldmfd	sp!, {r4, pc}
.L125:
	.align	2
.L124:
	.word	.LANCHOR0
	.word	1710595
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
	ldr	r3, .L127
	add	r3, r3, r0, asl #3
	mov	r0, #1
	ldr	r2, [r3, #864]
	ldrb	r1, [r3, #868]	@ zero_extendqisi2
	ldr	r3, [r2, #0]
	mov	r1, r0, asl r1
	bfi	r3, r1, #0, #8
	str	r3, [r2, #0]
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
	add	r3, r3, r0, asl #3
	ldr	r2, [r3, #864]
	ldr	r3, [r2, #0]
	bfc	r3, #0, #8
	bfc	r3, #17, #1
	str	r3, [r2, #0]
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
	@ args = 0, pretend = 0, frame = 8
	@ frame_needed = 0, uses_anonymous_args = 0
	@ link register save eliminated.
	.pad #8
	sub	sp, sp, #8
	mov	r0, r0, lsr #4
	str	r0, [sp, #4]
.L133:
	ldr	r0, [sp, #4]
	cmp	r0, #0
	sub	r3, r0, #1
	str	r3, [sp, #4]
	bne	.L133
	add	sp, sp, #8
	bx	lr
	.fnend
	.size	NandcDelayns, .-NandcDelayns
	.align	2
	.global	FlashReadStatusEN
	.type	FlashReadStatusEN, %function
FlashReadStatusEN:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	stmfd	sp!, {r3, r4, r5, lr}
	.save {r3, r4, r5, lr}
	ldr	r3, .L144
	add	r0, r3, r0, asl #3
	ldr	ip, [r3, #3624]
	ldrb	r5, [r0, #868]	@ zero_extendqisi2
	ldr	r4, [r0, #864]
	ldrb	ip, [ip, #8]	@ zero_extendqisi2
	add	r5, r5, #8
	cmp	ip, #2
	add	r0, r4, r5, asl #8
	movne	r3, #112
	strne	r3, [r0, #8]
	bne	.L140
	cmp	r2, #0
	ldreqb	r2, [r3, #909]	@ zero_extendqisi2
	ldrneb	r2, [r3, #910]	@ zero_extendqisi2
	str	r2, [r0, #8]
	ldrb	r3, [r3, #911]	@ zero_extendqisi2
	cmp	r3, #0
	movne	r3, #0
	ldrne	r2, .L144
	bne	.L139
	b	.L140
.L141:
	mov	ip, r3, asl #3
	add	r3, r3, #1
	mov	ip, r1, lsr ip
	uxtb	ip, ip
	str	ip, [r0, #4]
.L139:
	ldrb	ip, [r2, #911]	@ zero_extendqisi2
	cmp	r3, ip
	bcc	.L141
.L140:
	mov	r0, #80
	bl	NandcDelayns
	ldr	r0, [r4, r5, asl #8]
	uxtb	r0, r0
	ldmfd	sp!, {r3, r4, r5, pc}
.L145:
	.align	2
.L144:
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
	mov	r6, r0
	mov	r5, r1
	mov	r4, r2
.L150:
	mov	r0, r6
	mov	r1, r5
	mov	r2, r4
	bl	FlashReadStatusEN
	cmp	r0, #255
	beq	.L150
	tst	r0, #64
	beq	.L150
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
	ldr	r3, .L154
	stmfd	sp!, {r4, r5, r6, lr}
	.save {r4, r5, r6, lr}
	add	r4, r3, r0, asl #4
	ldrb	r5, [r3, r0, asl #4]	@ zero_extendqisi2
	mov	r6, r0
	ldr	r3, [r4, #8]
	cmp	r3, #0
	beq	.L152
	mov	r0, r5
	bl	NandcFlashCs
	ldr	r3, .L154+4
	ldr	r1, [r4, #4]
	mov	r0, r5
	add	r6, r3, r6, asl #2
	ldr	r2, [r6, #3588]
	adds	r2, r2, #0
	movne	r2, #1
	bl	FlashWaitReadyEN
	mov	r6, r0
	mov	r0, r5
	bl	NandcFlashDeCs
	ldr	r3, [r4, #8]
	ands	r6, r6, #1
	mvnne	r6, #0
	str	r6, [r3, #0]
	ldr	r2, [r4, #12]
	mov	r3, #0
	str	r3, [r4, #8]
	cmp	r2, r3
	strne	r6, [r2, #0]
	strne	r3, [r4, #12]
.L152:
	mov	r0, #0
	ldmfd	sp!, {r4, r5, r6, pc}
.L155:
	.align	2
.L154:
	.word	.LANCHOR0+3628
	.word	.LANCHOR0
	.fnend
	.size	FlashWaitCmdDone, .-FlashWaitCmdDone
	.align	2
	.global	HynixSetRRPara
	.type	HynixSetRRPara, %function
HynixSetRRPara:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	stmfd	sp!, {r4, r5, r6, r7, r8, r9, sl, lr}
	.save {r4, r5, r6, r7, r8, r9, sl, lr}
	mov	r8, r3
	ldr	r9, .L162
	mov	r4, r0
	mov	r5, r1
	mov	r6, r2
	ldr	r3, [r9, #3624]
	ldrb	r3, [r3, #19]	@ zero_extendqisi2
	cmp	r3, #6
	addeq	r9, r9, #20
	addeq	r9, r9, r0, asl #6
	addeq	r9, r9, r8, asl #2
	beq	.L158
	cmp	r3, #7
	addeq	r9, r9, #28
	addne	r3, r8, r0, asl #3
	moveq	r3, #160
	addne	r9, r9, #20
	mlaeq	r9, r3, r0, r9
	moveq	r3, #10
	addne	r9, r9, r3, asl #3
	mlaeq	r9, r3, r8, r9
.L158:
	ldr	r3, .L162
	mov	r0, r4
	mov	r7, #0
	add	r3, r3, r4, asl #3
	ldrb	sl, [r3, #868]	@ zero_extendqisi2
	ldr	r2, [r3, #864]
	add	sl, sl, #8
	add	sl, r2, sl, asl #8
	bl	NandcFlashCs
	mov	r3, #54
	str	r3, [sl, #8]
	b	.L160
.L161:
	ldrb	r3, [r6, r7]	@ zero_extendqisi2
	mov	r0, #200
	str	r3, [sl, #4]
	bl	NandcDelayns
	ldrsb	r3, [r9, r7]
	add	r7, r7, #1
	str	r3, [sl, #0]
.L160:
	uxtb	r3, r7
	cmp	r3, r5
	bcc	.L161
	mov	r3, #22
	mov	r0, r4
	str	r3, [sl, #8]
	bl	NandcFlashDeCs
	ldr	r3, .L162
	add	r4, r3, r4
	strb	r8, [r4, #3756]
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, sl, pc}
.L163:
	.align	2
.L162:
	.word	.LANCHOR0
	.fnend
	.size	HynixSetRRPara, .-HynixSetRRPara
	.align	2
	.global	FlashSetReadRetryDefault
	.type	FlashSetReadRetryDefault, %function
FlashSetReadRetryDefault:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	stmfd	sp!, {r4, r5, r6, lr}
	.save {r4, r5, r6, lr}
	ldr	r6, .L169
	ldr	r3, [r6, #3624]
	ldrb	r3, [r3, #19]	@ zero_extendqisi2
	sub	r3, r3, #1
	uxtb	r3, r3
	cmp	r3, #6
	ldmhifd	sp!, {r4, r5, r6, pc}
	ldr	r5, .L169+4
	mov	r4, #0
.L167:
	ldrb	r3, [r5, r4, asl #3]	@ zero_extendqisi2
	uxtb	r0, r4
	cmp	r3, #173
	bne	.L166
	ldrb	r1, [r6, #1]	@ zero_extendqisi2
	mov	r3, #0
	ldr	r2, .L169+8
	bl	HynixSetRRPara
.L166:
	add	r4, r4, #1
	cmp	r4, #4
	bne	.L167
	ldmfd	sp!, {r4, r5, r6, pc}
.L170:
	.align	2
.L169:
	.word	.LANCHOR0
	.word	.LANCHOR0+2980
	.word	.LANCHOR0+4
	.fnend
	.size	FlashSetReadRetryDefault, .-FlashSetReadRetryDefault
	.align	2
	.global	SamsungSetRRPara
	.type	SamsungSetRRPara, %function
SamsungSetRRPara:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	stmfd	sp!, {r3, r4, r5, r6, r7, r8, sl, lr}
	.save {r3, r4, r5, r6, r7, r8, sl, lr}
	mov	r4, r0
	ldr	r3, .L174
	mov	r5, #0
	ldr	sl, .L174+4
	mov	r8, #161
	add	r1, r3, r1, asl #2
	add	r6, r1, #3
	mov	r7, r3
	b	.L172
.L173:
	str	r8, [r4, #8]
	mov	r3, #0
	str	r3, [r4, #0]
	mov	r0, #300
	ldrsb	r3, [r5, r7]
	add	r5, r5, #1
	str	r3, [r4, #0]
	ldrsb	r3, [r6, #1]!
	str	r3, [r4, #0]
	bl	NandcDelayns
.L172:
	ldrb	r3, [sl, #3760]	@ zero_extendqisi2
	cmp	r5, r3
	bcc	.L173
	ldmfd	sp!, {r3, r4, r5, r6, r7, r8, sl, pc}
.L175:
	.align	2
.L174:
	.word	.LANCHOR1+2860
	.word	.LANCHOR0
	.fnend
	.size	SamsungSetRRPara, .-SamsungSetRRPara
	.align	2
	.global	ToshibaSetRRPara
	.type	ToshibaSetRRPara, %function
ToshibaSetRRPara:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	stmfd	sp!, {r4, r5, r6, r7, r8, r9, sl, lr}
	.save {r4, r5, r6, r7, r8, r9, sl, lr}
	add	r6, r1, #1
	ldr	r8, .L183
	mov	r4, r0
	add	r6, r6, r6, asl #2
	mov	r5, #0
	add	r3, r8, #45
	add	r7, r8, r6
	ldr	sl, .L183+4
	add	r6, r3, r6
	add	r9, r8, r1
	b	.L177
.L181:
	mov	r3, #85
	str	r3, [r4, #8]
	ldrsb	r3, [r5, r8]
	mov	r0, #200
	str	r3, [r4, #4]
	bl	NandcDelayns
	ldrb	r3, [sl, #3761]	@ zero_extendqisi2
	cmp	r3, #34
	ldreqsb	r3, [r7, #0]
	beq	.L182
	cmp	r3, #35
	addne	r3, r9, #2912
	addne	r3, r3, #12
	ldreqsb	r3, [r6, #0]
	ldrnesb	r3, [r3, #0]
.L182:
	add	r5, r5, #1
	add	r7, r7, #1
	add	r6, r6, #1
	str	r3, [r4, #0]
.L177:
	ldrb	r3, [sl, #3760]	@ zero_extendqisi2
	cmp	r5, r3
	bcc	.L181
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, sl, pc}
.L184:
	.align	2
.L183:
	.word	.LANCHOR1
	.word	.LANCHOR0
	.fnend
	.size	ToshibaSetRRPara, .-ToshibaSetRRPara
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
	ldr	r3, .L186
	add	r3, r3, r0, asl #3
	mov	r0, #80
	ldrb	r5, [r3, #868]	@ zero_extendqisi2
	ldr	r4, [r3, #864]
	add	r5, r5, #8
	add	r3, r4, r5, asl #8
	str	r2, [r3, #8]
	bl	NandcDelayns
	ldr	r0, [r4, r5, asl #8]
	ldmfd	sp!, {r3, r4, r5, pc}
.L187:
	.align	2
.L186:
	.word	.LANCHOR0
	.fnend
	.size	FlashReadStatus, .-FlashReadStatus
	.align	2
	.global	NandcWaitFlashReady
	.type	NandcWaitFlashReady, %function
NandcWaitFlashReady:
	.fnstart
	@ args = 0, pretend = 0, frame = 8
	@ frame_needed = 0, uses_anonymous_args = 0
	ldr	r3, .L193
	stmfd	sp!, {r0, r1, r2, r4, r5, lr}
	.save {r0, r1, r2, r4, r5, lr}
	add	r3, r3, r0, asl #3
	ldr	r4, .L193+4
	ldr	r5, [r3, #864]
.L190:
	mov	r0, #100
	bl	NandcDelayns
	ldr	r3, [r5, #0]
	str	r3, [sp, #4]
	ldr	r3, [sp, #4]
	tst	r3, #512
	bne	.L191
	subs	r4, r4, #1
	bne	.L190
	mvn	r0, #0
	b	.L189
.L191:
	mov	r0, #0
.L189:
	ldmfd	sp!, {r1, r2, r3, r4, r5, pc}
.L194:
	.align	2
.L193:
	.word	.LANCHOR0
	.word	100000
	.fnend
	.size	NandcWaitFlashReady, .-NandcWaitFlashReady
	.align	2
	.global	FlashEraseSLc2KBlocks
	.type	FlashEraseSLc2KBlocks, %function
FlashEraseSLc2KBlocks:
	.fnstart
	@ args = 0, pretend = 0, frame = 8
	@ frame_needed = 0, uses_anonymous_args = 0
	stmfd	sp!, {r4, r5, r6, r7, r8, sl, lr}
	.save {r4, r5, r6, r7, r8, sl, lr}
	mov	r6, #0
	.pad #20
	sub	sp, sp, #20
	mov	r8, r1
	mov	r4, r0
	mov	r7, r6
	ldr	r5, .L203
	b	.L196
.L202:
	add	r3, r6, r8
	add	r2, sp, #8
	mov	r0, r4
	mov	r1, #0
	uxtb	r3, r3
	str	r3, [sp, #0]
	add	r3, sp, #12
	bl	LogAddr2PhyAddr
	ldr	r3, [sp, #12]
	ldrb	r2, [r5, #3762]	@ zero_extendqisi2
	cmp	r3, r2
	mvncs	r3, #0
	strcs	r3, [r4, #0]
	bcs	.L198
	add	r2, r5, r3
	add	r3, r5, r3, asl #4
	ldrb	sl, [r2, #3764]	@ zero_extendqisi2
	strb	sl, [r3, #3628]
	mov	r0, sl
	bl	NandcWaitFlashReady
	mov	r0, sl
	bl	NandcFlashCs
	mov	r2, #0
	ldr	r1, [sp, #8]
	mov	r0, sl
	bl	FlashEraseCmd
	mov	r0, sl
	bl	NandcWaitFlashReady
	ldr	r1, [sp, #8]
	mov	r0, sl
	bl	FlashReadStatus
	ldr	r3, [sp, #8]
	mov	r2, #0
	ands	r0, r0, #1
	mvnne	r0, #0
	str	r0, [r4, #0]
	ldr	r1, [r5, #856]
	mov	r0, sl
	add	r1, r1, r3
	bl	FlashEraseCmd
	mov	r0, sl
	bl	NandcWaitFlashReady
	mov	r0, sl
	ldr	r1, [sp, #8]
	bl	FlashReadStatus
	tst	r0, #1
	mvnne	r3, #0
	strne	r3, [r4, #0]
	ldr	r3, [r4, #0]
	cmn	r3, #1
	bne	.L201
	ldr	r0, .L203+4
	ldr	r1, [sp, #8]
	bl	printk
.L201:
	mov	r0, sl
	bl	NandcFlashDeCs
.L198:
	sub	r6, r6, #1
	add	r7, r7, #1
	add	r4, r4, #36
	uxtb	r6, r6
.L196:
	cmp	r7, r8
	bne	.L202
	mov	r0, #0
	add	sp, sp, #20
	ldmfd	sp!, {r4, r5, r6, r7, r8, sl, pc}
.L204:
	.align	2
.L203:
	.word	.LANCHOR0
	.word	.LC1
	.fnend
	.size	FlashEraseSLc2KBlocks, .-FlashEraseSLc2KBlocks
	.align	2
	.global	SandiskSetRRPara
	.type	SandiskSetRRPara, %function
SandiskSetRRPara:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	stmfd	sp!, {r4, r5, r6, lr}
	.save {r4, r5, r6, lr}
	mov	r3, #239
	mov	r4, r0
	str	r3, [r0, #8]
	mov	r3, #17
	str	r3, [r0, #4]
	mov	r0, #200
	mov	r5, r1
	bl	NandcDelayns
	ldr	r1, .L211
	add	r3, r5, r5, asl #2
	mvn	r6, #4
	add	r0, r1, r3
	add	r1, r1, #45
	add	r1, r1, r3
	mov	r2, #0
	ldr	ip, .L211+4
	mul	r5, r6, r5
	b	.L206
.L209:
	ldrb	r6, [ip, #3761]	@ zero_extendqisi2
	add	r2, r2, #1
	cmp	r6, #67
	addeq	r6, r0, r5
	addne	r6, r1, r5
	add	r6, r6, r3
	add	r0, r0, #1
	add	r1, r1, #1
	ldrsb	r6, [r6, #5]
	str	r6, [r4, #0]
.L206:
	ldrb	r6, [ip, #3760]	@ zero_extendqisi2
	cmp	r2, r6
	bcc	.L209
	mov	r0, #0
	ldmfd	sp!, {r4, r5, r6, lr}
	b	NandcWaitFlashReady
.L212:
	.align	2
.L211:
	.word	.LANCHOR1
	.word	.LANCHOR0
	.fnend
	.size	SandiskSetRRPara, .-SandiskSetRRPara
	.align	2
	.global	SandiskProgTestBadBlock
	.type	SandiskProgTestBadBlock, %function
SandiskProgTestBadBlock:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	ldr	r3, .L214
	stmfd	sp!, {r4, r5, r6, lr}
	.save {r4, r5, r6, lr}
	add	r3, r3, r0, asl #3
	ldrb	r6, [r3, #868]	@ zero_extendqisi2
	ldr	r5, [r3, #864]
	mov	r3, #162
	add	r6, r6, #8
	add	r4, r5, r6, asl #8
	str	r3, [r4, #8]
	mov	r3, #128
	str	r3, [r4, #8]
	mov	r3, #0
	str	r3, [r4, #4]
	str	r3, [r4, #4]
	uxtb	r3, r1
	str	r3, [r4, #4]
	mov	r3, r1, lsr #8
	mov	r1, r1, lsr #16
	str	r3, [r4, #4]
	str	r1, [r4, #4]
	mov	r3, #16
	str	r3, [r4, #8]
	bl	NandcWaitFlashReady
	mov	r3, #112
	mov	r0, #80
	str	r3, [r4, #8]
	bl	NandcDelayns
	ldr	r0, [r5, r6, asl #8]
	and	r0, r0, #1
	ldmfd	sp!, {r4, r5, r6, pc}
.L215:
	.align	2
.L214:
	.word	.LANCHOR0
	.fnend
	.size	SandiskProgTestBadBlock, .-SandiskProgTestBadBlock
	.align	2
	.global	FlashReadSpare
	.type	FlashReadSpare, %function
FlashReadSpare:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	ldr	r3, .L217
	mov	ip, #0
	stmfd	sp!, {r4, r5, r6, lr}
	.save {r4, r5, r6, lr}
	mov	r6, r2
	ldrb	r2, [r3, #2837]	@ zero_extendqisi2
	ldr	r3, .L217+4
	add	r3, r3, r0, asl #3
	mov	r2, r2, asl #9
	ldrb	r5, [r3, #868]	@ zero_extendqisi2
	ldr	r4, [r3, #864]
	add	r5, r5, #8
	add	r3, r4, r5, asl #8
	str	ip, [r3, #8]
	str	r2, [r3, #4]
	mov	r2, r2, lsr #8
	str	r2, [r3, #4]
	uxtb	r2, r1
	str	r2, [r3, #4]
	mov	r2, r1, lsr #8
	mov	r1, r1, lsr #16
	str	r2, [r3, #4]
	str	r1, [r3, #4]
	mov	r2, #48
	str	r2, [r3, #8]
	bl	NandcWaitFlashReady
	ldr	r3, [r4, r5, asl #8]
	strb	r3, [r6, #0]
	ldmfd	sp!, {r4, r5, r6, pc}
.L218:
	.align	2
.L217:
	.word	.LANCHOR1
	.word	.LANCHOR0
	.fnend
	.size	FlashReadSpare, .-FlashReadSpare
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
	.global	FlashReset
	.type	FlashReset, %function
FlashReset:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	stmfd	sp!, {r3, r4, r5, lr}
	.save {r3, r4, r5, lr}
	mov	r4, r0
	ldr	r3, .L221
	add	r3, r3, r0, asl #3
	ldrb	r5, [r3, #868]	@ zero_extendqisi2
	ldr	r2, [r3, #864]
	add	r5, r5, #8
	add	r5, r2, r5, asl #8
	bl	NandcFlashCs
	mov	r3, #255
	mov	r0, r4
	str	r3, [r5, #8]
	bl	NandcWaitFlashReady
	mov	r0, r4
	ldmfd	sp!, {r3, r4, r5, lr}
	b	NandcFlashDeCs
.L222:
	.align	2
.L221:
	.word	.LANCHOR0
	.fnend
	.size	FlashReset, .-FlashReset
	.align	2
	.type	FlashReadIDRaw, %function
FlashReadIDRaw:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	stmfd	sp!, {r0, r1, r2, r3, r4, r5, r6, r7, r8, lr}
	.save {r0, r1, r2, r3, r4, r5, r6, r7, r8, lr}
	mov	r5, r1
	mov	r4, r0
	bl	FlashReset
	ldr	r3, .L225
	mov	r0, r4
	add	r3, r3, r4, asl #3
	ldrb	r7, [r3, #868]	@ zero_extendqisi2
	ldr	r6, [r3, #864]
	bl	NandcFlashCs
	add	r7, r7, #8
	mov	r3, #144
	mov	r0, #200
	add	r8, r6, r7, asl #8
	str	r3, [r8, #8]
	mov	r3, #0
	str	r3, [r8, #4]
	bl	NandcDelayns
	ldr	r3, [r6, r7, asl #8]
	mov	r0, r4
	strb	r3, [r5, #0]
	ldr	r3, [r6, r7, asl #8]
	strb	r3, [r5, #1]
	ldr	r3, [r6, r7, asl #8]
	strb	r3, [r5, #2]
	ldr	r3, [r6, r7, asl #8]
	strb	r3, [r5, #3]
	ldr	r3, [r6, r7, asl #8]
	strb	r3, [r5, #4]
	ldr	r3, [r6, r7, asl #8]
	strb	r3, [r5, #5]
	bl	NandcFlashDeCs
	ldrb	r2, [r5, #0]	@ zero_extendqisi2
	sub	r3, r2, #1
	uxtb	r3, r3
	cmp	r3, #253
	bhi	.L223
	ldrb	r1, [r5, #2]	@ zero_extendqisi2
	ldrb	r3, [r5, #1]	@ zero_extendqisi2
	ldr	r0, .L225+4
	str	r1, [sp, #0]
	ldrb	r1, [r5, #3]	@ zero_extendqisi2
	str	r1, [sp, #4]
	ldrb	r1, [r5, #4]	@ zero_extendqisi2
	str	r1, [sp, #8]
	ldrb	r1, [r5, #5]	@ zero_extendqisi2
	str	r1, [sp, #12]
	add	r1, r4, #1
	bl	printk
.L223:
	ldmfd	sp!, {r0, r1, r2, r3, r4, r5, r6, r7, r8, pc}
.L226:
	.align	2
.L225:
	.word	.LANCHOR0
	.word	.LC2
	.fnend
	.size	FlashReadIDRaw, .-FlashReadIDRaw
	.align	2
	.global	FlashSetInterfaceMode
	.type	FlashSetInterfaceMode, %function
FlashSetInterfaceMode:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	stmfd	sp!, {r3, r4, r5, r6, r7, r8, r9, sl, fp, lr}
	.save {r3, r4, r5, r6, r7, r8, r9, sl, fp, lr}
	mov	r4, #0
	ldr	sl, .L241
	mov	r7, #239
	ldr	r9, .L241+4
	mov	r1, r4
	sub	r8, sl, #864
	mov	r6, #128
	mov	r5, #1
.L237:
	mov	r2, sl
	ldrb	lr, [r4, r9]	@ zero_extendqisi2
	ldr	r3, [r2, r4]!
	cmp	lr, #152
	cmpne	lr, #69
	ldrb	r2, [r2, #4]	@ zero_extendqisi2
	add	r2, r2, #8
	add	ip, r3, r2, asl #8
	beq	.L228
	cmp	lr, #173
	cmpne	lr, #44
	bne	.L229
.L228:
	cmp	r0, #1
	ldrb	fp, [r8, #3772]	@ zero_extendqisi2
	bne	.L230
	tst	fp, #1
	beq	.L229
	cmp	lr, #173
	str	r7, [ip, #8]
	streq	r0, [ip, #4]
	beq	.L240
	cmp	lr, #44
	streq	r0, [ip, #4]
	strne	r6, [ip, #4]
	moveq	ip, #5
	strne	r0, [r3, r2, asl #8]
	bne	.L235
	b	.L239
.L230:
	tst	fp, #4
	beq	.L229
	cmp	lr, #173
	str	r7, [ip, #8]
	streq	r5, [ip, #4]
	moveq	ip, #32
	beq	.L239
	cmp	lr, #44
	strne	r6, [ip, #4]
	bne	.L240
	str	r5, [ip, #4]
	mov	ip, #35
.L239:
	str	ip, [r3, r2, asl #8]
	b	.L235
.L240:
	str	r1, [r3, r2, asl #8]
.L235:
	str	r1, [r3, r2, asl #8]
	str	r1, [r3, r2, asl #8]
	str	r1, [r3, r2, asl #8]
.L229:
	add	r4, r4, #8
	cmp	r4, #32
	bne	.L237
	mov	r0, #0
	bl	NandcWaitFlashReady
	mov	r0, #0
	ldmfd	sp!, {r3, r4, r5, r6, r7, r8, r9, sl, fp, pc}
.L242:
	.align	2
.L241:
	.word	.LANCHOR0+864
	.word	.LANCHOR0+2980
	.fnend
	.size	FlashSetInterfaceMode, .-FlashSetInterfaceMode
	.align	2
	.global	FlashDeInit
	.type	FlashDeInit, %function
FlashDeInit:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	stmfd	sp!, {r4, lr}
	.save {r4, lr}
	mov	r0, #0
	ldr	r4, .L245
	bl	NandcWaitFlashReady
	bl	FlashSetReadRetryDefault
	ldrb	r3, [r4, #3773]	@ zero_extendqisi2
	cmp	r3, #0
	beq	.L244
	ldrb	r3, [r4, #3772]	@ zero_extendqisi2
	tst	r3, #1
	beq	.L244
	mov	r0, #1
	bl	FlashSetInterfaceMode
	mov	r0, #1
	bl	NandcSetMode
	mov	r3, #0
	strb	r3, [r4, #3773]
.L244:
	ldr	r3, [r4, #864]
	mov	r0, #0
	str	r0, [r3, #336]
	ldmfd	sp!, {r4, pc}
.L246:
	.align	2
.L245:
	.word	.LANCHOR0
	.fnend
	.size	FlashDeInit, .-FlashDeInit
	.align	2
	.global	FlashReadDpCmd
	.type	FlashReadDpCmd, %function
FlashReadDpCmd:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	ldr	r3, .L251
	stmfd	sp!, {r4, r5, r6, r7, r8, lr}
	.save {r4, r5, r6, r7, r8, lr}
	mov	r7, r2
	add	r2, r3, r0, asl #3
	mov	r5, r1
	mov	r4, r0
	ldrb	r6, [r2, #868]	@ zero_extendqisi2
	ldr	r1, [r2, #864]
	ldrb	r2, [r3, #912]	@ zero_extendqisi2
	add	r6, r6, #8
	cmp	r2, #1
	ldrb	r2, [r3, #904]	@ zero_extendqisi2
	add	r6, r1, r6, asl #8
	str	r2, [r6, #8]
	bne	.L248
	mov	r8, #0
	uxtb	r2, r5
	str	r8, [r6, #4]
	str	r8, [r6, #4]
	str	r2, [r6, #4]
	mov	r2, r5, lsr #8
	str	r2, [r6, #4]
	mov	r2, r5, lsr #16
	str	r2, [r6, #4]
	ldrb	r3, [r3, #905]	@ zero_extendqisi2
	str	r3, [r6, #8]
	bl	NandcWaitFlashReady
	str	r8, [r6, #8]
	str	r8, [r6, #4]
	str	r8, [r6, #4]
	b	.L250
.L248:
	uxtb	r2, r5
	str	r2, [r6, #4]
	mov	r2, r5, lsr #8
	str	r2, [r6, #4]
	mov	r2, r5, lsr #16
	str	r2, [r6, #4]
	ldrb	r3, [r3, #905]	@ zero_extendqisi2
	str	r3, [r6, #8]
.L250:
	uxtb	r3, r7
	mov	r0, r4
	str	r3, [r6, #4]
	mov	r1, r5
	mov	r3, r7, lsr #8
	mov	r7, r7, lsr #16
	str	r3, [r6, #4]
	mov	r3, #48
	str	r7, [r6, #4]
	str	r3, [r6, #8]
	ldmfd	sp!, {r4, r5, r6, r7, r8, lr}
	b	FlashSetRandomizer
.L252:
	.align	2
.L251:
	.word	.LANCHOR0
	.fnend
	.size	FlashReadDpCmd, .-FlashReadDpCmd
	.align	2
	.global	NandcRandmzSel
	.type	NandcRandmzSel, %function
NandcRandmzSel:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	@ link register save eliminated.
	ldr	r3, .L254
	add	r3, r3, r0, asl #3
	ldr	r3, [r3, #864]
	str	r1, [r3, #336]
	bx	lr
.L255:
	.align	2
.L254:
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
	ldr	r1, .L266
	bl	__aeabi_idiv
	ldr	r3, .L266+4
	ldr	r3, [r3, #3012]
	cmp	r0, #250
	movwgt	r2, #8354
	bgt	.L264
	cmp	r0, #220
	bgt	.L265
.L259:
	cmp	r0, #185
	movwgt	r2, #4226
	bgt	.L264
	cmp	r0, #160
	movwgt	r2, #4194
	bgt	.L264
	cmp	r4, #35
	movwls	r2, #4193
	bls	.L264
	cmp	r4, #99
	movwls	r2, #4225
	bls	.L264
.L265:
	movw	r2, #8322
.L264:
	str	r2, [r3, #4]
	ldmfd	sp!, {r4, pc}
.L267:
	.align	2
.L266:
	.word	1000000
	.word	.LANCHOR0
	.fnend
	.size	NandcTimeCfg, .-NandcTimeCfg
	.align	2
	.global	NandcInit
	.type	NandcInit, %function
NandcInit:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	stmfd	sp!, {r3, r4, r5, lr}
	.save {r3, r4, r5, lr}
	mov	r5, #0
	ldr	r4, .L269
	mov	r3, #1
	str	r0, [r4, #3012]
	str	r0, [r4, #864]
	str	r3, [r4, #876]
	mov	r3, #2
	str	r0, [r4, #872]
	str	r3, [r4, #884]
	mov	r3, #3
	str	r0, [r4, #880]
	str	r0, [r4, #888]
	str	r5, [r4, #868]
	str	r3, [r4, #892]
	ldr	r3, [r0, #0]
	and	r3, r3, #253952
	ubfx	r2, r3, #13, #1
	str	r2, [r4, #3776]
	ldr	r2, [r0, #352]
	and	r3, r3, #245760
	orr	r3, r3, #256
	ubfx	r2, r2, #16, #4
	str	r2, [r4, #3780]
	ldr	r2, [r0, #352]
	str	r2, [r4, #3784]
	str	r3, [r0, #0]
	mov	r0, #40
	ldr	r3, [r4, #3012]
	str	r5, [r3, #336]
	bl	NandcTimeCfg
	ldr	r3, [r4, #3012]
	movw	r2, #8322
	mov	r0, #36864
	str	r2, [r3, #344]
	ldr	r2, .L269+4
	str	r2, [r3, #304]
	bl	ftl_malloc
	str	r5, [r4, #3816]
	str	r5, [r4, #3824]
	str	r0, [r4, #3788]
	str	r0, [r4, #3792]
	add	r0, r0, #32768
	str	r0, [r4, #3796]
	ldmfd	sp!, {r3, r4, r5, pc}
.L270:
	.align	2
.L269:
	.word	.LANCHOR0
	.word	1710593
	.fnend
	.size	NandcInit, .-NandcInit
	.align	2
	.global	FlashTimingCfg
	.type	FlashTimingCfg, %function
FlashTimingCfg:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	@ link register save eliminated.
	sub	r3, r0, #4160
	movw	r2, #4225
	sub	r3, r3, #33
	cmp	r0, r2
	cmpne	r3, #1
	bls	.L272
	movw	r3, #4226
	movw	r2, #8322
	cmp	r0, r3
	cmpne	r0, r2
	bne	.L273
.L272:
	ldr	r3, .L274
	ldr	r3, [r3, #3012]
	str	r0, [r3, #4]
.L273:
	ldr	r3, .L274+4
	ldrb	r0, [r3, #2849]	@ zero_extendqisi2
	b	NandcTimeCfg
.L275:
	.align	2
.L274:
	.word	.LANCHOR0
	.word	.LANCHOR1
	.fnend
	.size	FlashTimingCfg, .-FlashTimingCfg
	.align	2
	.global	NandcGetTimeCfg
	.type	NandcGetTimeCfg, %function
NandcGetTimeCfg:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	ldr	ip, .L277
	stmfd	sp!, {r4, lr}
	.save {r4, lr}
	ldr	r4, [ip, #3012]
	ldr	r4, [r4, #4]
	str	r4, [r0, #0]
	ldr	r0, [ip, #3012]
	ldr	r0, [r0, #0]
	str	r0, [r1, #0]
	ldr	r1, [ip, #3012]
	ldr	r1, [r1, #304]
	str	r1, [r2, #0]
	ldr	r2, [ip, #3012]
	ldr	r0, [r2, #308]
	ldr	r1, [r2, #344]
	uxtb	r2, r0
	orr	r2, r2, r1, asl #16
	str	r2, [r3, #0]
	ldmfd	sp!, {r4, pc}
.L278:
	.align	2
.L277:
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
	ldr	r3, .L284
	mov	r1, #1
	ldr	r2, [r3, #3012]
	str	r0, [r3, #3828]
	mov	r3, #0
	str	r1, [r2, #8]
	mov	r1, #16
	cmp	r0, r1
	bfi	r3, r1, #8, #8
	bfc	r3, #18, #1
	beq	.L283
.L280:
	cmp	r0, #24
	orreq	r3, r3, #16
	beq	.L281
	cmp	r0, #40
	orr	r3, r3, #262144
	orr	r3, r3, #16
	bne	.L281
.L283:
	bfc	r3, #4, #1
.L281:
	orr	r3, r3, #1
	str	r3, [r2, #12]
	bx	lr
.L285:
	.align	2
.L284:
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
	ldr	r3, .L287
	strb	r0, [r3, #3832]
	b	NandcBchSel
.L288:
	.align	2
.L287:
	.word	.LANCHOR0
	.fnend
	.size	FlashBchSel, .-FlashBchSel
	.align	2
	.global	FlashResume
	.type	FlashResume, %function
FlashResume:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	stmfd	sp!, {r3, r4, r5, lr}
	.save {r3, r4, r5, lr}
	mov	r4, #0
	ldr	r3, .L294
	ldr	r5, .L294+4
	ldr	r2, [r3, #3012]
	ldr	r1, [r3, #3016]
	str	r1, [r2, #0]
	ldr	r1, [r3, #3020]
	ldr	r2, [r3, #3012]
	str	r1, [r2, #4]
	ldr	r1, [r3, #3024]
	str	r1, [r2, #8]
	ldr	r1, [r3, #3028]
	str	r1, [r2, #12]
	ldr	r1, [r3, #3032]
	str	r1, [r2, #304]
	ldr	r1, [r3, #3036]
	str	r1, [r2, #308]
	ldr	r1, [r3, #3040]
	ldr	r3, [r3, #3044]
	str	r1, [r2, #336]
	str	r3, [r2, #344]
.L291:
	ldrb	r3, [r5, r4, asl #3]	@ zero_extendqisi2
	sub	r3, r3, #1
	uxtb	r3, r3
	cmp	r3, #253
	bhi	.L290
	uxtb	r0, r4
	bl	FlashReset
.L290:
	add	r4, r4, #1
	cmp	r4, #4
	bne	.L291
	ldr	r4, .L294
	ldrb	r3, [r4, #3773]	@ zero_extendqisi2
	cmp	r3, #0
	beq	.L292
	mov	r0, #1
	bl	NandcSetMode
	ldrb	r0, [r4, #3772]	@ zero_extendqisi2
	bl	FlashSetInterfaceMode
	ldrb	r0, [r4, #3772]	@ zero_extendqisi2
	bl	NandcSetMode
	ldrb	r0, [r4, #3033]	@ zero_extendqisi2
	bl	NandcSetDdrPara
.L292:
	ldr	r3, .L294
	ldr	r3, [r3, #3624]
	ldrb	r0, [r3, #20]	@ zero_extendqisi2
	bl	FlashBchSel
	mov	r0, #0
	ldmfd	sp!, {r3, r4, r5, pc}
.L295:
	.align	2
.L294:
	.word	.LANCHOR0
	.word	.LANCHOR0+2980
	.fnend
	.size	FlashResume, .-FlashResume
	.align	2
	.global	NandCIrqEnable
	.type	NandCIrqEnable, %function
NandCIrqEnable:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	@ link register save eliminated.
	ldr	r3, [r0, #368]
	mov	r2, #1
	mov	r2, r2, asl r1
	orr	r3, r2, r3
	str	r3, [r0, #368]
	ldr	r3, [r0, #364]
	orr	r2, r2, r3
	str	r2, [r0, #364]
	bx	lr
	.fnend
	.size	NandCIrqEnable, .-NandCIrqEnable
	.align	2
	.global	NandCIrqDisable
	.type	NandCIrqDisable, %function
NandCIrqDisable:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	@ link register save eliminated.
	ldr	r3, [r0, #368]
	mov	r2, #1
	mov	r2, r2, asl r1
	orr	r3, r2, r3
	str	r3, [r0, #368]
	ldr	r3, [r0, #364]
	bic	r2, r3, r2
	str	r2, [r0, #364]
	bx	lr
	.fnend
	.size	NandCIrqDisable, .-NandCIrqDisable
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
	.global	rk_nandc_flash_ready
	.type	rk_nandc_flash_ready, %function
rk_nandc_flash_ready:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	@ link register save eliminated.
	mov	r1, #1
	b	NandCIrqDisable
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
	mov	r0, r4
	mov	r1, #1
	bl	NandCIrqEnable
	ldr	r3, [r4, #0]
	mov	r0, r4
	ubfx	r3, r3, #8, #8
	and	r3, r3, #2
	uxtb	r3, r3
	cmp	r3, #0
	bne	.L301
	ldmfd	sp!, {r4, lr}
	b	wait_for_nand_flash_ready
.L301:
	mov	r1, #1
	ldmfd	sp!, {r4, lr}
	b	NandCIrqDisable
	.fnend
	.size	NandcIqrWaitFlashReady, .-NandcIqrWaitFlashReady
	.align	2
	.global	FlashEraseBlocks
	.type	FlashEraseBlocks, %function
FlashEraseBlocks:
	.fnstart
	@ args = 0, pretend = 0, frame = 16
	@ frame_needed = 0, uses_anonymous_args = 0
	stmfd	sp!, {r4, r5, r6, r7, r8, r9, sl, fp, lr}
	.save {r4, r5, r6, r7, r8, r9, sl, fp, lr}
	.pad #28
	sub	sp, sp, #28
	ldr	r5, .L318
	mov	r6, r0
	str	r1, [sp, #12]
	mov	r7, r2
	ldrb	r4, [r5, #852]	@ zero_extendqisi2
	cmp	r4, #0
	moveq	r9, r5
	beq	.L303
	mov	r1, r2
	bl	FlashEraseSLc2KBlocks
	b	.L304
.L312:
	mov	r8, #36
	rsb	r3, r4, r7
	mul	r8, r8, r4
	uxtb	r3, r3
	mov	r1, #0
	str	r3, [sp, #0]
	add	r2, sp, #16
	add	r3, sp, #20
	add	fp, r6, r8
	mov	r0, fp
	bl	LogAddr2PhyAddr
	ldrb	r3, [r5, #3762]	@ zero_extendqisi2
	mov	sl, r0
	ldr	r0, [sp, #20]
	cmp	r0, r3
	mvncs	r3, #0
	strcs	r3, [r6, r8]
	bcs	.L306
	ldrb	r3, [r5, #3833]	@ zero_extendqisi2
	cmp	r3, #0
	add	r3, r9, r0, asl #4
	ldr	r3, [r3, #3636]
	moveq	sl, #0
	cmp	r3, #0
	beq	.L308
	uxtb	r0, r0
	bl	FlashWaitCmdDone
.L308:
	ldr	r2, [sp, #20]
	mov	r1, #0
	cmp	sl, #0
	add	r3, r5, r2, asl #4
	movne	r0, #36
	str	r1, [r3, #3640]
	ldr	r1, [sp, #16]
	str	fp, [r3, #3636]
	str	r1, [r3, #3632]
	addne	r1, r4, #1
	mlane	r1, r0, r1, r6
	strne	r1, [r3, #3640]
	add	r3, r9, r2
	add	r2, r9, r2, asl #4
	ldrb	r8, [r3, #3764]	@ zero_extendqisi2
	mov	r0, r8
	strb	r8, [r2, #3628]
	bl	NandcFlashCs
	ldr	r3, [sp, #12]
	mov	r0, r8
	cmp	r3, #1
	bne	.L310
	ldrb	r3, [r5, #928]	@ zero_extendqisi2
	cmp	r3, #0
	beq	.L310
	bl	flash_enter_slc_mode
	b	.L311
.L310:
	bl	flash_exit_slc_mode
.L311:
	ldr	r3, [sp, #20]
	mov	r0, r8
	ldr	r1, [sp, #16]
	add	r4, r4, sl
	add	r3, r5, r3, asl #2
	ldr	r2, [r3, #3588]
	adds	r2, r2, #0
	movne	r2, #1
	bl	FlashWaitReadyEN
	mov	r0, r8
	ldr	r1, [sp, #16]
	mov	r2, sl
	bl	FlashEraseCmd
	mov	r0, r8
	bl	NandcFlashDeCs
.L306:
	add	r4, r4, #1
.L303:
	cmp	r4, r7
	bcc	.L312
	ldr	r5, .L318
	mov	r4, #0
	ldr	r6, .L318+4
	ldr	r0, [r5, #3012]
	bl	NandcIqrWaitFlashReady
	b	.L313
.L315:
	uxtb	r0, r4
	bl	FlashWaitCmdDone
	ldr	r3, [sp, #12]
	cmp	r3, #1
	bne	.L314
	ldrb	r3, [r5, #928]	@ zero_extendqisi2
	cmp	r3, #0
	beq	.L314
	ldrb	r0, [r6, r4, asl #4]	@ zero_extendqisi2
	bl	flash_exit_slc_mode
.L314:
	add	r4, r4, #1
.L313:
	ldrb	r3, [r5, #3762]	@ zero_extendqisi2
	cmp	r4, r3
	bcc	.L315
	mov	r0, #0
.L304:
	add	sp, sp, #28
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, sl, fp, pc}
.L319:
	.align	2
.L318:
	.word	.LANCHOR0
	.word	.LANCHOR0+3628
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
	mov	r1, #0
	b	NandCIrqDisable
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
	ldr	r3, .L322
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
	bx	lr
.L323:
	.align	2
.L322:
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
.L325:
	ldr	r3, [r0, #8]
	str	r3, [sp, #4]
	ldr	r3, [sp, #4]
	tst	r3, #1048576
	beq	.L325
	add	sp, sp, #8
	bx	lr
	.fnend
	.size	NandcSendDumpDataDone, .-NandcSendDumpDataDone
	.align	2
	.global	NandcXferStart
	.type	NandcXferStart, %function
NandcXferStart:
	.fnstart
	@ args = 8, pretend = 0, frame = 16
	@ frame_needed = 0, uses_anonymous_args = 0
	stmfd	sp!, {r4, r5, r6, r7, r8, r9, sl, fp, lr}
	.save {r4, r5, r6, r7, r8, r9, sl, fp, lr}
	mov	r8, r2
	ldr	r2, .L340
	mov	ip, #16
	mov	r6, r1
	mov	r4, #0
	add	r0, r2, r0, asl #3
	.pad #20
	sub	sp, sp, #20
	ldr	r5, [r0, #864]
	ldrb	r0, [r0, #868]	@ zero_extendqisi2
	ldr	r9, [sp, #56]
	ldr	r7, [r5, #12]
	ldr	r1, [sp, #60]
	bfi	r7, ip, #8, #8
	bfi	r7, r4, #3, #1
	bfi	r4, r6, #1, #1
	orr	r4, r4, #8
	bfi	r7, r0, #5, #3
	mov	r0, #1
	bfi	r4, r0, #5, #2
	orr	r4, r4, #536870912
	mov	r3, r3, lsr r0
	orr	r4, r4, #1024
	bfi	r4, r3, #4, #1
	ldr	r3, [r2, #3780]
	cmp	r3, #3
	bls	.L328
	ldr	r3, [r5, #16]
	cmp	r9, #0
	cmpeq	r1, #0
	str	r3, [sp, #12]
	ldr	r3, [sp, #12]
	bfc	r3, #2, #1
	str	r3, [sp, #12]
	beq	.L329
	cmp	r6, #0
	beq	.L330
	ldr	r3, [r2, #3828]
	mov	ip, r1
	cmp	r3, #24
	movhi	r3, #128
	movls	r3, #64
	str	r3, [sp, #0]
	mov	r3, r8, lsr r0
	str	r3, [sp, #4]
	mov	r3, #0
	mov	r0, r3
	b	.L332
.L335:
	cmp	r1, #0
	mov	lr, r3, lsr #2
	add	r0, r0, #1
	ldrneh	fp, [ip, #2]
	mvneq	fp, #0
	ldrneh	sl, [ip], #4
	ldreq	sl, [r2, #3796]
	orrne	sl, sl, fp, asl #16
	ldrne	fp, [r2, #3796]
	streq	fp, [sl, lr, asl #2]
	strne	sl, [fp, lr, asl #2]
	ldr	lr, [sp, #0]
	add	r3, r3, lr
.L332:
	ldr	sl, [sp, #4]
	cmp	r0, sl
	bcc	.L335
.L330:
	mov	r0, r5
	add	r8, r8, #1
	bl	rk_nandc_xfer_irq_flag_init
	mov	r0, r5
	mov	r1, #0
	mov	r8, r8, asr #1
	bl	NandCIrqEnable
	bfi	r4, r8, #22, #6
	ldr	r8, .L340
	cmp	r9, #0
	ubfx	sl, r4, #22, #5
	mov	r2, r6
	movne	r0, r9
	ldreq	r0, [r8, #3792]
	mov	r1, sl, asl #10
	ldr	r3, [r8, #3796]
	str	r3, [r8, #3804]
	str	r0, [r8, #3800]
	bl	rknand_dma_map_single
	mov	r2, r6
	mov	r1, sl, asl #7
	str	r0, [r8, #3808]
	ldr	r0, [r8, #3804]
	bl	rknand_dma_map_single
	mov	r3, #1
	str	r3, [r8, #3816]
	mov	r2, #16
	ldr	r3, [r8, #3808]
	tst	r9, #3
	str	r0, [r8, #3812]
	str	r3, [r5, #20]
	ldr	r3, [r8, #3812]
	str	r3, [r5, #24]
	mov	r3, #0
	str	r3, [sp, #12]
	ldr	r3, [sp, #12]
	bfi	r3, r2, #9, #5
	str	r3, [sp, #12]
	ldr	r3, [sp, #12]
	moveq	r2, #2
	orr	r3, r3, #448
	str	r3, [sp, #12]
	ldreq	r3, [sp, #12]
	bfieq	r3, r2, #3, #3
	streq	r3, [sp, #12]
	rsbs	r6, r6, #1
	ldr	r3, [sp, #12]
	movcc	r6, #0
	orr	r3, r3, #4
	str	r3, [sp, #12]
	ldr	r3, [sp, #12]
	bfi	r3, r6, #1, #1
	str	r3, [sp, #12]
	ldr	r3, [sp, #12]
	orr	r3, r3, #1
	str	r3, [sp, #12]
.L329:
	ldr	r3, [sp, #12]
	str	r3, [r5, #16]
.L328:
	str	r7, [r5, #12]
	str	r4, [r5, #8]
	orr	r4, r4, #4
	str	r4, [r5, #8]
	add	sp, sp, #20
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, sl, fp, pc}
.L341:
	.align	2
.L340:
	.word	.LANCHOR0
	.fnend
	.size	NandcXferStart, .-NandcXferStart
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
	mov	r5, r2
	add	r2, r0, #4096
	mov	r4, r0
	ldr	r6, [sp, #16]
	add	r2, r2, r5, asl #9
	bne	.L343
	cmp	r3, #0
	beq	.L344
	mov	r0, r2
	mov	r1, r3
	mov	r2, #1024
	bl	memcpy
.L344:
	cmp	r6, #0
	ldmeqfd	sp!, {r4, r5, r6, pc}
	ldrb	r3, [r6, #2]	@ zero_extendqisi2
	mov	r5, r5, lsr #1
	ldrb	r2, [r6, #1]	@ zero_extendqisi2
	mov	r3, r3, asl #16
	orr	r3, r3, r2, asl #8
	ldrb	r2, [r6, #0]	@ zero_extendqisi2
	orr	r3, r3, r2
	ldrb	r2, [r6, #3]	@ zero_extendqisi2
	orr	r3, r3, r2, asl #24
	mov	r2, #48
	mul	r5, r2, r5
	mov	r5, r5, asr #2
	add	r5, r5, #128
	str	r3, [r4, r5, asl #2]
	ldmfd	sp!, {r4, r5, r6, pc}
.L343:
	cmp	r3, #0
	beq	.L346
	mov	r1, r2
	mov	r0, r3
	mov	r2, #1024
	bl	memcpy
.L346:
	cmp	r6, #0
	ldmeqfd	sp!, {r4, r5, r6, pc}
	mov	r5, r5, lsr #1
	mov	r3, #48
	mul	r5, r3, r5
	mov	r5, r5, asr #2
	add	r5, r5, #128
	ldr	r3, [r4, r5, asl #2]
	mov	r2, r3, lsr #8
	strb	r3, [r6, #0]
	strb	r2, [r6, #1]
	mov	r2, r3, lsr #16
	mov	r3, r3, lsr #24
	strb	r2, [r6, #2]
	strb	r3, [r6, #3]
	ldmfd	sp!, {r4, r5, r6, pc}
	.fnend
	.size	NandcCopy1KB, .-NandcCopy1KB
	.align	2
	.global	Ftl_log2
	.type	Ftl_log2, %function
Ftl_log2:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	@ link register save eliminated.
	mov	r2, #1
	mov	r3, #0
	b	.L348
.L349:
	add	r3, r3, #1
	mov	r2, r2, asl #1
	uxth	r3, r3
.L348:
	cmp	r2, r0
	bls	.L349
	sub	r3, r3, #1
	uxth	r0, r3
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
	ldr	r3, .L354
	mov	r2, #3840
	cmp	r0, #23
	movw	r1, #3850
	ldrh	r2, [r3, r2]
	movls	r0, #24
	ldrh	r1, [r3, r1]
	str	r0, [r3, #3836]
	mul	r2, r2, r0
	rsb	r0, r0, r1
	movw	r1, #3848
	strh	r0, [r3, r1]	@ movhi
	mov	r0, #0
	ldr	r1, [r3, #3856]
	str	r2, [r3, #3844]
	rsb	r2, r2, r1
	str	r2, [r3, #3852]
	bx	lr
.L355:
	.align	2
.L354:
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
	stmfd	sp!, {r4, r5, r6, r7, r8, r9, sl, fp, lr}
	.save {r4, r5, r6, r7, r8, r9, sl, fp, lr}
	movw	r2, #3860
	ldr	r3, .L372
	mov	r4, r0
	ldrh	lr, [r0, #8]
	.pad #20
	sub	sp, sp, #20
	ldrh	r1, [r0, #10]
	ldrh	r0, [r0, #12]
	strh	lr, [r3, r2]	@ movhi
	add	r2, r2, #2
	ldrh	r6, [r4, #14]
	strh	r1, [r3, r2]	@ movhi
	add	r2, r2, #2
	strh	r0, [r3, r2]	@ movhi
	movw	r2, #3850
	strh	r6, [r3, r2]	@ movhi
	mov	r3, #0
	ldr	r2, .L372+4
.L357:
	strb	r3, [r3, r2]
	add	r3, r3, #1
	cmp	r3, #32
	bne	.L357
	ldrh	r2, [r4, #14]
	ldrh	r3, [r4, #20]
	cmp	r3, r2, lsr #8
	bcs	.L358
	ldr	r2, .L372+4
	sub	ip, r1, #1
	uxtb	r7, r0
	mov	r3, #0
	rsb	sl, r0, r2
	str	sl, [sp, #8]
	mla	ip, r0, ip, r2
	mov	r8, r7, asl #1
	uxtb	r8, r8
	str	ip, [sp, #12]
	b	.L359
.L360:
	strb	ip, [r9, r2]
	add	fp, ip, r7
	add	ip, ip, r8
	add	r5, r5, #1
	strb	fp, [sl, r2]
	uxtb	ip, ip
.L362:
	cmp	r5, r1
	add	r2, r2, r0
	bcc	.L360
	add	r3, r3, #1
.L359:
	cmp	r3, r0
	bcs	.L361
	ldr	fp, [sp, #8]
	mov	r2, #0
	uxtb	ip, r3
	mov	r5, r2
	add	r9, fp, r3
	ldr	fp, [sp, #12]
	add	sl, fp, r3
	b	.L362
.L361:
	ldr	r3, .L372
	movw	r2, #3862
	mov	r1, r1, asl #1
	mov	r6, r6, lsr #1
	strh	r1, [r3, r2]	@ movhi
	movw	r2, #3850
	strh	r6, [r3, r2]	@ movhi
.L358:
	ldr	r3, .L372
	movw	r2, #3898
	cmp	lr, #1
	mov	r1, #5
	mov	ip, #0
	ldr	sl, .L372
	ldrb	r8, [r3, #852]	@ zero_extendqisi2
	strh	r1, [r3, r2]	@ movhi
	movw	r1, #3900
	streqh	lr, [r3, r2]	@ movhi
	cmp	r8, #0
	movw	r2, #3902
	strh	ip, [r3, r1]	@ movhi
	mov	r1, #4352
	strh	r1, [r3, r2]	@ movhi
	ldrne	r3, .L372
	movne	r1, #384
	strneh	r1, [r3, r2]	@ movhi
	movw	r3, #3862
	ldrh	r5, [sl, r3]
	mov	r3, #3840
	mul	r5, r5, r0
	uxth	r5, r5
	strh	r5, [sl, r3]	@ movhi
	add	r3, r3, #10
	ldrh	r6, [sl, r3]
	mov	r3, #3904
	mul	r0, r0, r6
	uxth	r0, r0
	strh	r0, [sl, r3]	@ movhi
	bl	Ftl_log2
	movw	r3, #3906
	ldrh	fp, [r4, #18]
	movw	r2, #3908
	ldrh	r9, [r4, #20]
	strh	r0, [sl, r3]	@ movhi
	mov	r0, r9
	ldrh	r3, [r4, #16]
	mul	r1, r3, r5
	strh	r3, [sl, r2]	@ movhi
	add	r2, r2, #2
	strh	fp, [sl, r2]	@ movhi
	add	r2, r2, #2
	strh	r1, [sl, r2]	@ movhi
	add	r2, r2, #2
	strh	r9, [sl, r2]	@ movhi
	str	r3, [sp, #4]
	bl	Ftl_log2
	movw	r2, #3916
	movw	r1, #3918
	cmp	r6, #1024
	strh	r0, [sl, r2]	@ movhi
	mov	r2, r9, asl #9
	mov	r7, r0
	uxth	r2, r2
	strh	r2, [sl, r1]	@ movhi
	mov	r1, #3920
	mov	r2, r2, lsr #8
	strh	r2, [sl, r1]	@ movhi
	ldrh	r1, [r4, #26]
	movw	r2, #3922
	mov	r4, sl
	strh	r1, [sl, r2]	@ movhi
	uxtbhi	r1, r6
	mul	r2, r6, r5
	str	r2, [sl, #3856]
	movw	r2, #3900
	ldr	r3, [sp, #4]
	strhih	r1, [sl, r2]	@ movhi
	ldrh	r2, [sl, r2]
	mul	r1, fp, r9
	rsb	r2, r2, r6
	mov	r6, r6, asl #6
	mul	r2, r2, r5
	mul	r2, r9, r2
	mul	r3, r3, r2
	mov	r3, r3, asr #11
	str	r3, [sl, #3924]
	movw	sl, #3902
	ldrh	r0, [r4, sl]
	mov	r0, r0, asl #3
	bl	__aeabi_idiv
	movw	r3, #3928
	uxth	r0, r0
	strh	r0, [r4, r3]	@ movhi
	cmp	r0, #4
	ldr	r4, .L372
	ldrls	r2, .L372
	movls	r1, #4
	strlsh	r1, [r2, r3]	@ movhi
	cmp	r8, #0
	mov	r1, r5
	movne	r3, #640
	strneh	r3, [r4, sl]	@ movhi
	ldrh	r2, [r4, sl]
	movw	r3, #3930
	mov	r2, r2, asr r7
	add	r7, r7, #9
	mov	r6, r6, asr r7
	add	r2, r2, #2
	strh	r2, [r4, r3]	@ movhi
	add	r3, r3, #2
	uxth	r6, r6
	strh	r6, [r4, r3]	@ movhi
	mul	r3, r5, r6
	add	r6, r6, #8
	str	r3, [r4, #3936]
	movw	r3, #3928
	ldrh	r0, [r4, r3]
	bl	__aeabi_uidiv
	cmp	r5, #1
	ldreq	r3, .L372
	uxtah	r6, r6, r0
	str	r6, [r4, #3836]
	ldr	r4, .L372
	addeq	r6, r6, #4
	streq	r6, [r3, #3836]
	ldr	r0, [r4, #3836]
	uxth	r0, r0
	bl	FtlSysBlkNumInit
	ldr	r3, [r4, #3836]
	ldr	r6, [r4, #3852]
	mov	r0, #2048
	str	r3, [r4, #3940]
	movw	r3, #3908
	ldrh	r3, [r4, r3]
	mov	r6, r6, asl #2
	mul	r6, r3, r6
	movw	r3, #3916
	ldrh	r3, [r4, r3]
	add	r3, r3, #9
	mov	r6, r6, lsr r3
	movw	r3, #3944
	add	r6, r6, #2
	uxth	r6, r6
	strh	r6, [r4, r3]	@ movhi
	movw	r3, #3914
	ldrh	r5, [r4, r3]
	mov	r1, r5
	bl	__aeabi_idiv
	movw	r3, #3946
	strh	r0, [r4, r3]	@ movhi
	mov	r3, #0
	str	r3, [r4, #3948]
	movw	r3, #3928
	ldrh	r1, [r4, r3]
	add	r2, r1, #3
	strh	r2, [r4, r3]	@ movhi
	ldr	r2, [r4, #3936]
	add	r0, r2, #3
	str	r0, [r4, #3936]
	ldrb	r0, [r4, #928]	@ zero_extendqisi2
	cmp	r0, #0
	mov	r0, #0
	addne	r1, r1, #4
	strneh	r1, [r4, r3]	@ movhi
	mov	r3, #3952
	addne	r2, r2, #5
	strne	r2, [r4, #3936]
	mov	r2, #0
	strh	r2, [r4, r3]	@ movhi
	movw	r2, #3848
	ldrh	r2, [r4, r2]
	mov	r1, r2, lsr #3
	add	r2, r1, r2, asl #1
	add	r2, r2, #52
	add	r6, r2, r6, asl #2
	cmp	r6, r5, asl #9
	ldrcc	r2, .L372
	movcc	r1, #1
	strcch	r1, [r2, r3]	@ movhi
	add	sp, sp, #20
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, sl, fp, pc}
.L373:
	.align	2
.L372:
	.word	.LANCHOR0
	.word	.LANCHOR0+3866
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
	ldr	r3, .L381
	movw	r2, #3954
	ldrh	r2, [r3, r2]
	cmp	r2, #0
	beq	.L379
	movw	r2, #3928
	ldrh	r1, [r3, r2]
	ldr	r2, [r3, #3956]
	mov	r3, #0
	b	.L376
.L378:
	ldrh	ip, [r2], #2
	cmp	ip, r0
	beq	.L380
	add	r3, r3, #1
	uxth	r3, r3
.L376:
	cmp	r3, r1
	bne	.L378
	mov	r0, #0
	bx	lr
.L380:
	mov	r0, #1
	bx	lr
.L379:
	mov	r0, r2
	bx	lr
.L382:
	.align	2
.L381:
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
	ldr	r2, .L391
	stmfd	sp!, {r4, lr}
	.save {r4, lr}
	ldr	r3, [r2, #3960]
	cmp	r3, #0
	beq	.L388
	ldr	ip, [r2, #3964]
	mov	r2, #0
.L387:
	ldr	r4, [ip, #16]
	cmp	r4, r0
	bcc	.L385
	cmp	r4, r1
	bls	.L389
.L385:
	add	r2, r2, #1
	add	ip, ip, #36
	cmp	r2, r3
	bne	.L387
	mov	r0, #0
	ldmfd	sp!, {r4, pc}
.L389:
	mov	r0, #1
	ldmfd	sp!, {r4, pc}
.L388:
	mov	r0, r3
	ldmfd	sp!, {r4, pc}
.L392:
	.align	2
.L391:
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
	ldr	r3, .L394
	ldr	r0, [r3, #3948]
	bx	lr
.L395:
	.align	2
.L394:
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
	ldr	r3, .L397
	ldr	r0, [r3, #3948]
	bx	lr
.L398:
	.align	2
.L397:
	.word	.LANCHOR0
	.fnend
	.size	FtlGetCapacity, .-FtlGetCapacity
	.align	2
	.global	FtlGetLpn
	.type	FtlGetLpn, %function
FtlGetLpn:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	@ link register save eliminated.
	ldr	r3, .L400
	ldr	r0, [r3, #3968]
	bx	lr
.L401:
	.align	2
.L400:
	.word	.LANCHOR0
	.fnend
	.size	FtlGetLpn, .-FtlGetLpn
	.align	2
	.global	FtlGetCurEraseBlock
	.type	FtlGetCurEraseBlock, %function
FtlGetCurEraseBlock:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	@ link register save eliminated.
	ldr	r3, .L403
	mov	r2, #3840
	ldrh	r2, [r3, r2]
	ldr	r0, [r3, #3972]
	mul	r0, r0, r2
	bx	lr
.L404:
	.align	2
.L403:
	.word	.LANCHOR0
	.fnend
	.size	FtlGetCurEraseBlock, .-FtlGetCurEraseBlock
	.align	2
	.global	FtlGetAllBlockNum
	.type	FtlGetAllBlockNum, %function
FtlGetAllBlockNum:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	@ link register save eliminated.
	ldr	r3, .L406
	mov	r1, #3840
	movw	r2, #3850
	ldrh	r2, [r3, r2]
	ldrh	r0, [r3, r1]
	mul	r0, r0, r2
	bx	lr
.L407:
	.align	2
.L406:
	.word	.LANCHOR0
	.fnend
	.size	FtlGetAllBlockNum, .-FtlGetAllBlockNum
	.align	2
	.global	FtlBbmMapBadBlock
	.type	FtlBbmMapBadBlock, %function
FtlBbmMapBadBlock:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	stmfd	sp!, {r0, r1, r4, r5, r6, lr}
	.save {r0, r1, r4, r5, r6, lr}
	mov	r3, #3904
	ldr	r4, .L409
	mov	r5, r0
	ldrh	r6, [r4, r3]
	mov	r1, r6
	bl	__aeabi_uidiv
	uxth	r2, r0
	mls	r3, r6, r2, r5
	add	r1, r4, r2, asl #2
	mov	r6, #1
	ldr	r0, [r1, #4004]
	uxth	r3, r3
	and	r1, r3, #31
	mov	ip, r3, lsr #5
	ldr	lr, [r0, ip, asl #2]
	orr	r1, lr, r6, asl r1
	str	r1, [r0, ip, asl #2]
	str	r1, [sp, #0]
	mov	r1, r5
	ldr	r0, .L409+4
	bl	printk
	movw	r3, #3982
	ldrh	r2, [r4, r3]
	mov	r0, #0
	add	r2, r2, r6
	strh	r2, [r4, r3]	@ movhi
	ldmfd	sp!, {r2, r3, r4, r5, r6, pc}
.L410:
	.align	2
.L409:
	.word	.LANCHOR0
	.word	.LC3
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
	mov	r3, #3904
	ldr	r5, .L412
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
	ldr	r3, [r5, #4004]
	ldr	r0, [r3, r2, asl #2]
	mov	r0, r0, lsr r4
	and	r0, r0, #1
	ldmfd	sp!, {r3, r4, r5, r6, r7, pc}
.L413:
	.align	2
.L412:
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
	stmfd	sp!, {r4, r5, r6, r7, r8, lr}
	.save {r4, r5, r6, r7, r8, lr}
	mov	r4, #0
	mov	r5, r4
	ldr	r6, .L419
	mov	r8, #3904
	movw	r7, #3862
	b	.L416
.L418:
	mov	r0, r5
	add	r5, r5, #1
	bl	FtlBbmIsBadBlock
	uxth	r5, r5
	cmp	r0, #0
	addne	r4, r4, #1
	uxthne	r4, r4
.L416:
	ldrh	r3, [r6, r8]
	ldrh	r2, [r6, r7]
	mul	r3, r2, r3
	cmp	r5, r3
	blt	.L418
	mov	r0, r4
	ldmfd	sp!, {r4, r5, r6, r7, r8, pc}
.L420:
	.align	2
.L419:
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
	movw	r3, #3864
	ldr	r5, .L422
	mov	r7, r1
	mov	r6, r0
	ldrh	r4, [r5, r3]
	mov	r1, r4
	bl	__aeabi_uidiv
	mov	r3, #3904
	ldrh	r5, [r5, r3]
	mov	r1, r4
	mul	r7, r4, r7
	mla	r5, r5, r0, r7
	mov	r0, r6
	bl	__aeabi_uidivmod
	add	r1, r5, r1
	uxth	r0, r1
	ldmfd	sp!, {r3, r4, r5, r6, r7, pc}
.L423:
	.align	2
.L422:
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
	ldr	r3, .L425
	movw	r2, #3864
	stmfd	sp!, {r4, r5, r6, lr}
	.save {r4, r5, r6, lr}
	mov	r6, r0
	ldrh	r4, [r3, r2]
	mov	r2, #3904
	ldrh	r1, [r3, r2]
	bl	__aeabi_uidiv
	mov	r1, r4
	mul	r5, r0, r4
	mov	r0, r6
	bl	__aeabi_uidivmod
	add	r1, r5, r1
	uxth	r0, r1
	ldmfd	sp!, {r4, r5, r6, pc}
.L426:
	.align	2
.L425:
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
	mov	r3, #3904
	ldr	r4, .L428
	ldrh	r1, [r4, r3]
	bl	__aeabi_uidivmod
	movw	r3, #3864
	uxth	r0, r1
	ldrh	r1, [r4, r3]
	bl	__aeabi_uidiv
	uxth	r0, r0
	ldmfd	sp!, {r4, pc}
.L429:
	.align	2
.L428:
	.word	.LANCHOR0
	.fnend
	.size	P2V_block_in_plane, .-P2V_block_in_plane
	.align	2
	.type	FtlFreeSysBlkQueueOut.part.5, %function
FtlFreeSysBlkQueueOut.part.5:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	stmfd	sp!, {r4, r5, r6, r7, r8, lr}
	.save {r4, r5, r6, r7, r8, lr}
	movw	r3, #4038
	ldr	r4, .L433
	movw	r1, #4036
	ldr	r8, .L433+4
	ldrh	r2, [r4, r3]
	ldr	r7, [r8, #-2092]
	add	r0, r2, #4
	add	r2, r2, #1
	cmp	r7, #0
	bic	r2, r2, #64512
	add	r0, r4, r0, asl #1
	ldrh	r5, [r0, r1]
	add	r1, r1, #6
	strh	r2, [r4, r3]	@ movhi
	ldrh	r0, [r4, r1]
	sub	r0, r0, #1
	strh	r0, [r4, r1]	@ movhi
	bne	.L431
	mov	r0, r5
	bl	P2V_block_in_plane
	mov	r3, r5, asl #10
	mov	r6, r0
	ldr	r0, [r8, #-2088]
	str	r3, [r0, #4]
	ldrb	r3, [r4, #928]	@ zero_extendqisi2
	cmp	r3, #0
	beq	.L432
	mov	r1, r7
	mov	r2, #1
	bl	FlashEraseBlocks
.L432:
	ldr	r4, .L433+4
	mov	r1, #1
	mov	r2, r1
	mov	r6, r6, asl #1
	ldr	r0, [r4, #-2088]
	bl	FlashEraseBlocks
	ldr	r3, [r4, #-2084]
	ldrh	r2, [r3, r6]
	add	r2, r2, #1
	strh	r2, [r3, r6]	@ movhi
	ldr	r3, [r4, #-2080]
	add	r3, r3, #1
	str	r3, [r4, #-2080]
.L431:
	mov	r0, r5
	ldmfd	sp!, {r4, r5, r6, r7, r8, pc}
.L434:
	.align	2
.L433:
	.word	.LANCHOR0
	.word	.LANCHOR2
	.fnend
	.size	FtlFreeSysBlkQueueOut.part.5, .-FtlFreeSysBlkQueueOut.part.5
	.align	2
	.global	ftl_cmp_data_ver
	.type	ftl_cmp_data_ver, %function
ftl_cmp_data_ver:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	@ link register save eliminated.
	cmp	r0, r1
	bls	.L436
	rsb	r0, r1, r0
	cmp	r0, #-2147483648
	movhi	r0, #0
	movls	r0, #1
	bx	lr
.L436:
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
	ldr	r2, .L439
	movw	r3, #4042
	ldrh	r0, [r2, r3]
	rsbs	r0, r0, #1
	movcc	r0, #0
	bx	lr
.L440:
	.align	2
.L439:
	.word	.LANCHOR0
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
	ldr	r2, .L442
	movw	r3, #4042
	ldrh	r0, [r2, r3]
	sub	r2, r0, #1024
	rsbs	r0, r2, #0
	adc	r0, r0, r2
	bx	lr
.L443:
	.align	2
.L442:
	.word	.LANCHOR0
	.fnend
	.size	FtlFreeSysBlkQueueFull, .-FtlFreeSysBlkQueueFull
	.align	2
	.global	FtlFreeSysBlkQueueIn
	.type	FtlFreeSysBlkQueueIn, %function
FtlFreeSysBlkQueueIn:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	ldr	r2, .L447
	movw	r3, #4042
	stmfd	sp!, {r4, r5, r6, lr}
	.save {r4, r5, r6, lr}
	mov	r5, r0
	ldrh	r3, [r2, r3]
	cmp	r3, #1024
	ldmeqfd	sp!, {r4, r5, r6, pc}
	cmp	r1, #0
	beq	.L446
	ldr	r4, .L447+4
	ldr	r3, [r4, #-2092]
	cmp	r3, #0
	bne	.L446
	bl	P2V_block_in_plane
	mov	r1, #1
	mov	r3, r5, asl #10
	mov	r2, r1
	mov	r6, r0
	ldr	r0, [r4, #-2088]
	mov	r6, r6, asl #1
	str	r3, [r0, #4]
	bl	FlashEraseBlocks
	ldr	r3, [r4, #-2084]
	ldrh	r2, [r3, r6]
	add	r2, r2, #1
	strh	r2, [r3, r6]	@ movhi
	ldr	r3, [r4, #-2080]
	add	r3, r3, #1
	str	r3, [r4, #-2080]
.L446:
	ldr	r3, .L447
	movw	r2, #4042
	movw	r0, #4036
	ldrh	r1, [r3, r2]
	add	r1, r1, #1
	strh	r1, [r3, r2]	@ movhi
	movw	r2, #4040
	ldrh	r1, [r3, r2]
	add	ip, r1, #4
	add	r1, r1, #1
	bic	r1, r1, #64512
	add	ip, r3, ip, asl #1
	strh	r5, [ip, r0]	@ movhi
	strh	r1, [r3, r2]	@ movhi
	ldmfd	sp!, {r4, r5, r6, pc}
.L448:
	.align	2
.L447:
	.word	.LANCHOR0
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
	stmfd	sp!, {r4, r5, r6, r7, r8, r9, sl, lr}
	.save {r4, r5, r6, r7, r8, r9, sl, lr}
	mov	r4, #0
	ldr	r5, .L459
	movw	r8, #4042
	movw	r7, #4038
	ldr	r6, .L459+4
	b	.L450
.L451:
	ldrh	r3, [r5, r7]
	add	r3, r4, r3
	add	r3, r5, r3, asl #1
	add	r3, r3, #4032
	ldrh	r0, [r3, #12]
	bl	P2V_block_in_plane
	ldr	r2, [r6, #-2084]
	ldr	r3, [r6, #-2076]
	mov	r0, r0, asl #1
	ldrh	r2, [r2, r0]
	str	r2, [r3, r4, asl #2]
	add	r4, r4, #1
	uxth	r4, r4
.L450:
	ldrh	r3, [r5, r8]
	cmp	r3, r4
	bhi	.L451
	mov	r3, #0
	ldr	ip, .L459
	movw	r8, #4042
	ldr	r4, .L459+4
	movw	r7, #4038
	b	.L452
.L457:
	add	r6, r3, #1
	ldr	r1, [r4, #-2076]
	mov	r2, r3
	uxth	r6, r6
	mov	r0, r6
	b	.L453
.L455:
	ldr	r9, [r1, r2, asl #2]
	ldr	sl, [r1, r0, asl #2]
	cmp	r9, sl
	movhi	r2, r0
	add	r0, r0, #1
	uxth	r0, r0
.L453:
	cmp	r0, r5
	bcc	.L455
	cmp	r3, r2
	beq	.L456
	ldr	r5, [r1, r3, asl #2]
	ldr	r0, [r1, r2, asl #2]
	str	r5, [r1, r2, asl #2]
	ldr	r1, [r4, #-2076]
	str	r0, [r1, r3, asl #2]
	ldrh	r1, [ip, r7]
	add	r2, r1, r2
	add	r3, r1, r3
	add	r2, ip, r2, asl #1
	add	r3, ip, r3, asl #1
	add	r2, r2, #4032
	add	r3, r3, #4032
	ldrh	r0, [r2, #12]
	ldrh	r1, [r3, #12]
	strh	r1, [r2, #12]	@ movhi
	strh	r0, [r3, #12]	@ movhi
.L456:
	mov	r3, r6
.L452:
	ldrh	r5, [ip, r8]
	sub	r2, r5, #1
	cmp	r3, r2
	blt	.L457
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, sl, pc}
.L460:
	.align	2
.L459:
	.word	.LANCHOR0
	.word	.LANCHOR2
	.fnend
	.size	FtlFreeSysBLkSort, .-FtlFreeSysBLkSort
	.align	2
	.global	remove_from_free_sys_Queue
	.type	remove_from_free_sys_Queue, %function
remove_from_free_sys_Queue:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	ldr	r3, .L467
	movw	r2, #4042
	stmfd	sp!, {r4, r5, r6, lr}
	.save {r4, r5, r6, lr}
	mov	r1, r0
	ldrh	r0, [r3, r2]
	cmp	r0, #0
	ldmeqfd	sp!, {r4, r5, r6, pc}
	movw	r2, #4038
	mov	r5, #0
	ldrh	ip, [r3, r2]
	b	.L463
.L465:
	add	r2, r5, ip
	ldr	r4, .L467
	movw	r6, #4036
	mov	r2, r2, asl #22
	add	r2, r3, r2, lsr #21
	add	r2, r2, #4032
	add	r2, r2, #12
	ldrh	r2, [r2, #0]
	cmp	r2, r1
	bne	.L464
	ldr	r0, .L467+4
	bl	printk
	movw	r2, #4038
	ldrh	r3, [r4, r2]
	mov	r0, #1
	add	r1, r3, #4
	add	r5, r5, r3
	add	r3, r3, #1
	add	r1, r4, r1, asl #1
	mov	r5, r5, asl #22
	bic	r3, r3, #64512
	ldrh	r1, [r1, r6]
	add	r5, r4, r5, lsr #21
	add	r5, r5, #8
	strh	r1, [r5, r6]	@ movhi
	strh	r3, [r4, r2]	@ movhi
	movw	r3, #4042
	ldrh	r2, [r4, r3]
	sub	r2, r2, #1
	strh	r2, [r4, r3]	@ movhi
	ldmfd	sp!, {r4, r5, r6, pc}
.L464:
	add	r5, r5, #1
.L463:
	cmp	r5, r0
	bcc	.L465
	mov	r0, #0
	ldmfd	sp!, {r4, r5, r6, pc}
.L468:
	.align	2
.L467:
	.word	.LANCHOR0
	.word	.LC4
	.fnend
	.size	remove_from_free_sys_Queue, .-remove_from_free_sys_Queue
	.align	2
	.global	FtlFreeSysBlkQueueOut
	.type	FtlFreeSysBlkQueueOut, %function
FtlFreeSysBlkQueueOut:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	@ link register save eliminated.
	ldr	r2, .L471
	movw	r3, #4042
	ldrh	r3, [r2, r3]
	cmp	r3, #0
	beq	.L470
	b	FtlFreeSysBlkQueueOut.part.5
.L470:
	movw	r0, #65535
	bx	lr
.L472:
	.align	2
.L471:
	.word	.LANCHOR0
	.fnend
	.size	FtlFreeSysBlkQueueOut, .-FtlFreeSysBlkQueueOut
	.align	2
	.global	insert_data_list
	.type	insert_data_list, %function
insert_data_list:
	.fnstart
	@ args = 0, pretend = 0, frame = 16
	@ frame_needed = 0, uses_anonymous_args = 0
	ldr	r2, .L488
	movw	r3, #3848
	stmfd	sp!, {r4, r5, r6, r7, r8, r9, sl, fp, lr}
	.save {r4, r5, r6, r7, r8, r9, sl, fp, lr}
	.pad #20
	sub	sp, sp, #20
	ldrh	r3, [r2, r3]
	cmp	r3, r0
	bls	.L474
	ldr	r1, .L488+4
	mov	ip, #6
	mul	ip, ip, r0
	mvn	r6, #0
	ldr	r4, [r1, #-2072]
	mov	r5, r1
	add	r2, r4, ip
	strh	r6, [r2, #2]	@ movhi
	strh	r6, [r4, ip]	@ movhi
	ldr	r3, [r1, #-2068]
	cmp	r3, #0
	beq	.L487
.L475:
	ldr	r7, [r1, #-2064]
	mov	r8, r0, asl #1
	ldrh	r1, [r2, #4]
	ldr	r9, .L488
	ldrh	sl, [r7, r8]
	cmp	r1, #0
	ldr	fp, [r5, #-2084]
	mulne	r6, r1, sl
	ldr	r1, [r5, #-2072]
	ldr	sl, .L488+8
	mov	r5, #0
	add	r8, fp, r8
	str	r8, [sp, #12]
	str	r1, [sp, #4]
	rsb	r1, r1, r3
	mov	r1, r1, asr #1
	mul	r1, sl, r1
	movw	sl, #3848
	ldrh	sl, [r9, sl]
	str	sl, [sp, #8]
	uxth	r1, r1
.L482:
	add	r5, r5, #1
	ldr	r8, [sp, #8]
	uxth	r5, r5
	cmp	r5, r8
	bhi	.L474
	cmp	r0, r1
	beq	.L474
	mov	r9, r1, asl #1
	ldrh	r8, [r3, #4]
	ldrh	sl, [r7, r9]
	cmp	r8, #0
	mvneq	r8, #0
	mulne	r8, r8, sl
	cmp	r8, r6
	bne	.L478
	ldrh	sl, [fp, r9]
	ldr	r9, [sp, #12]
	ldrh	r8, [r9, #0]
	cmp	sl, r8
	bcc	.L480
	b	.L479
.L478:
	bhi	.L479
.L480:
	ldrh	r8, [r3, #0]
	movw	sl, #65535
	cmp	r8, sl
	streqh	r1, [r2, #2]	@ movhi
	streqh	r0, [r3, #0]	@ movhi
	ldreq	r3, .L488+4
	streq	r2, [r3, #-2060]
	beq	.L474
.L481:
	ldr	r9, [sp, #4]
	mov	r1, #6
	mla	r3, r1, r8, r9
	mov	r1, r8
	b	.L482
.L479:
	strh	r1, [r4, ip]	@ movhi
	ldrh	r1, [r3, #2]
	strh	r1, [r2, #2]	@ movhi
	ldr	r1, .L488+4
	ldr	ip, [r1, #-2068]
	cmp	r3, ip
	bne	.L483
	strh	r0, [r3, #2]	@ movhi
.L487:
	str	r2, [r1, #-2068]
	b	.L474
.L483:
	ldrh	r2, [r3, #2]
	mov	ip, #6
	ldr	r1, [r1, #-2072]
	mul	r2, ip, r2
	strh	r0, [r1, r2]	@ movhi
	strh	r0, [r3, #2]	@ movhi
.L474:
	mov	r0, #0
	add	sp, sp, #20
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, sl, fp, pc}
.L489:
	.align	2
.L488:
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
	ldr	r2, .L491
	ldr	r3, .L491+4
	ldrh	r1, [r2, r3]
	add	r1, r1, #1
	strh	r1, [r2, r3]	@ movhi
	ldmfd	sp!, {r3, pc}
.L492:
	.align	2
.L491:
	.word	.LANCHOR2
	.word	-2056
	.fnend
	.size	INSERT_DATA_LIST, .-INSERT_DATA_LIST
	.align	2
	.global	insert_free_list
	.type	insert_free_list, %function
insert_free_list:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	stmfd	sp!, {r4, r5, r6, r7, r8, r9, sl, lr}
	.save {r4, r5, r6, r7, r8, r9, sl, lr}
	movw	r6, #65535
	cmp	r0, r6
	beq	.L494
	ldr	r1, .L501
	mov	r5, #6
	mul	ip, r5, r0
	mvn	r3, #0
	ldr	r4, [r1, #-2072]
	add	r2, r4, ip
	strh	r3, [r2, #2]	@ movhi
	strh	r3, [r4, ip]	@ movhi
	ldr	r3, [r1, #-2052]
	cmp	r3, #0
	beq	.L500
.L495:
	ldr	sl, [r1, #-2072]
	mov	r8, r0, asl #1
	ldr	r7, [r1, #-2084]
	rsb	r1, sl, r3
	ldr	r9, .L501+4
	mov	r1, r1, asr #1
	ldrh	r8, [r7, r8]
	mul	r1, r9, r1
	mov	r9, r5
	uxth	r1, r1
.L498:
	mov	r5, r1, asl #1
	ldrh	r5, [r7, r5]
	cmp	r5, r8
	bcs	.L496
	ldrh	r5, [r3, #0]
	cmp	r5, r6
	streqh	r1, [r2, #2]	@ movhi
	streqh	r0, [r3, #0]	@ movhi
	beq	.L494
.L497:
	mla	r3, r9, r5, sl
	mov	r1, r5
	b	.L498
.L496:
	ldrh	r5, [r3, #2]
	strh	r5, [r2, #2]	@ movhi
	strh	r1, [r4, ip]	@ movhi
	ldr	r1, .L501
	ldr	ip, [r1, #-2052]
	cmp	r3, ip
	bne	.L499
	strh	r0, [r3, #2]	@ movhi
.L500:
	str	r2, [r1, #-2052]
	b	.L494
.L499:
	ldrh	r2, [r3, #2]
	mov	ip, #6
	ldr	r1, [r1, #-2072]
	mul	r2, ip, r2
	strh	r0, [r1, r2]	@ movhi
	strh	r0, [r3, #2]	@ movhi
.L494:
	mov	r0, #0
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, sl, pc}
.L502:
	.align	2
.L501:
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
	ldr	r2, .L504
	ldr	r3, .L504+4
	ldrh	r1, [r2, r3]
	add	r1, r1, #1
	strh	r1, [r2, r3]	@ movhi
	ldmfd	sp!, {r3, pc}
.L505:
	.align	2
.L504:
	.word	.LANCHOR2
	.word	-2048
	.fnend
	.size	INSERT_FREE_LIST, .-INSERT_FREE_LIST
	.align	2
	.global	List_remove_node
	.type	List_remove_node, %function
List_remove_node:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	ldr	ip, .L511
	mov	r2, #6
	mul	r1, r2, r1
	ldr	r3, [ip, #-2072]
	stmfd	sp!, {r4, r5, r6, lr}
	.save {r4, r5, r6, lr}
	add	r5, r3, r1
	ldr	r4, [r0, #0]
	movw	r6, #65535
	cmp	r5, r4
	ldrh	r4, [r3, r1]
	bne	.L507
	cmp	r4, r6
	mlane	r2, r2, r4, r3
	moveq	r2, #0
	streq	r2, [r0, #0]
	strne	r2, [r0, #0]
	mvnne	r0, #0
	strneh	r0, [r2, #2]	@ movhi
	b	.L509
.L507:
	cmp	r4, r6
	ldrh	r0, [r5, #2]
	bne	.L510
	cmp	r0, r4
	mulne	r2, r2, r0
	mvnne	r0, #0
	strneh	r0, [r3, r2]	@ movhi
	b	.L509
.L510:
	mla	r4, r2, r4, r3
	strh	r0, [r4, #2]	@ movhi
	ldrh	r6, [r5, #2]
	ldrh	r4, [r3, r1]
	ldr	r0, [ip, #-2072]
	mul	r2, r2, r6
	strh	r4, [r0, r2]	@ movhi
.L509:
	mvn	r2, #0
	mov	r0, #0
	strh	r2, [r3, r1]	@ movhi
	strh	r2, [r5, #2]	@ movhi
	ldmfd	sp!, {r4, r5, r6, pc}
.L512:
	.align	2
.L511:
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
	stmfd	sp!, {r4, lr}
	.save {r4, lr}
	ldr	r3, [r0, #0]
	cmp	r3, #0
	beq	.L518
	ldr	r2, .L519
	movw	lr, #65535
	mov	ip, #6
	ldr	r4, [r2, #-2072]
	b	.L515
.L517:
	mla	r3, ip, r2, r4
	sub	r1, r1, #1
	uxth	r1, r1
.L515:
	cmp	r1, #0
	beq	.L516
	ldrh	r2, [r3, #0]
	cmp	r2, lr
	bne	.L517
.L516:
	rsb	r4, r4, r3
	ldr	r3, .L519+4
	mov	r4, r4, asr #1
	mul	r4, r3, r4
	uxth	r4, r4
	mov	r1, r4
	bl	List_remove_node
	mov	r0, r4
	ldmfd	sp!, {r4, pc}
.L518:
	movw	r0, #65535
	ldmfd	sp!, {r4, pc}
.L520:
	.align	2
.L519:
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
	ldr	r2, .L527
	ldr	r3, [r2, #-2068]
	cmp	r3, #0
	beq	.L526
	ldr	r2, [r2, #-2072]
	movw	r1, #65535
	mov	ip, #6
	b	.L523
.L525:
	mla	r3, ip, r3, r2
	sub	r0, r0, #1
	uxth	r0, r0
.L523:
	cmp	r0, #0
	beq	.L524
	ldrh	r3, [r3, #0]
	cmp	r3, r1
	bne	.L525
	mov	r0, r1
	bx	lr
.L524:
	rsb	r3, r2, r3
	ldr	r0, .L527+4
	mov	r3, r3, asr #1
	mul	r0, r0, r3
	uxth	r0, r0
	bx	lr
.L526:
	movw	r0, #65535
	bx	lr
.L528:
	.align	2
.L527:
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
	mov	r4, r0
	ldr	r3, .L536
	ldr	r2, .L536+4
	ldrh	r2, [r3, r2]
	cmp	r2, r0
	beq	.L530
	ldr	r2, .L536+8
	ldrh	r2, [r3, r2]
	cmp	r2, r0
	beq	.L530
	ldr	r2, .L536+12
	ldrh	r2, [r3, r2]
	cmp	r2, r0
	beq	.L530
	mov	r5, #6
	ldr	r2, [r3, #-2072]
	mul	r5, r5, r0
	ldr	r1, [r3, #-2068]
	add	r0, r2, r5
	cmp	r0, r1
	beq	.L530
	ldr	ip, [r3, #-2064]
	mov	r3, r4, asl #1
	ldrh	r1, [r0, #4]
	ldrh	r3, [ip, r3]
	muls	r1, r1, r3
	ldrh	r3, [r0, #2]
	mvneq	r1, #0
	movw	r0, #65535
	cmp	r3, r0
	bne	.L532
	ldrh	r0, [r2, r5]
	cmp	r0, r3
	beq	.L530
.L532:
	mov	r0, #6
	mla	r3, r0, r3, r2
	ldr	r0, .L536+16
	rsb	r2, r2, r3
	mov	r2, r2, asr #1
	ldrh	r3, [r3, #4]
	mul	r2, r0, r2
	uxth	r2, r2
	mov	r2, r2, asl #1
	ldrh	r2, [ip, r2]
	muls	r3, r3, r2
	mvneq	r3, #0
	cmp	r1, r3
	bcs	.L530
	ldr	r0, .L536+20
	mov	r1, r4
	bl	List_remove_node
	ldr	r5, .L536
	ldr	r3, .L536+24
	mov	r0, r4
	ldrh	r2, [r5, r3]
	sub	r2, r2, #1
	strh	r2, [r5, r3]	@ movhi
	bl	INSERT_DATA_LIST
.L530:
	mov	r0, #0
	ldmfd	sp!, {r3, r4, r5, pc}
.L537:
	.align	2
.L536:
	.word	.LANCHOR2
	.word	-2044
	.word	-1996
	.word	-1948
	.word	-1431655765
	.word	.LANCHOR2-2068
	.word	-2056
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
	mov	r4, r0
	ldrh	r2, [r0, #10]
	mov	r5, #0
	ldr	r3, [r0, #12]
	b	.L539
.L542:
	mov	r7, r3
	add	r3, r3, #2
	ldrh	r6, [r7, #0]
	cmp	r6, #0
	bne	.L540
	bl	FtlFreeSysBlkQueueOut
	cmp	r0, #0
	strh	r0, [r7, #0]	@ movhi
	beq	.L541
	ldr	r3, [r4, #28]
	strh	r6, [r4, #2]	@ movhi
	add	r3, r3, #1
	str	r3, [r4, #28]
	ldrh	r3, [r4, #8]
	strh	r5, [r4, #0]	@ movhi
	add	r3, r3, #1
	strh	r3, [r4, #8]	@ movhi
	b	.L541
.L540:
	add	r5, r5, #1
	uxth	r5, r5
.L539:
	cmp	r5, r2
	bne	.L542
.L541:
	mov	r0, #0
	ldmfd	sp!, {r3, r4, r5, r6, r7, pc}
	.fnend
	.size	ftl_map_blk_alloc_new_blk, .-ftl_map_blk_alloc_new_blk
	.align	2
	.global	select_l2p_ram_region
	.type	select_l2p_ram_region, %function
select_l2p_ram_region:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	ldr	r2, .L555
	movw	r3, #3946
	stmfd	sp!, {r4, r5, r6, r7, lr}
	.save {r4, r5, r6, r7, lr}
	movw	r1, #65535
	ldrh	r3, [r2, r3]
	ldr	r2, .L555+4
	ldr	ip, [r2, #-1900]
	mov	r2, #0
	mov	r0, r2
	b	.L544
.L546:
	add	r2, r2, #12
	add	r4, ip, r2
	ldrh	r4, [r4, #-12]
	cmp	r4, r1
	ldmeqfd	sp!, {r4, r5, r6, r7, pc}
	add	r0, r0, #1
	uxth	r0, r0
.L544:
	cmp	r0, r3
	bne	.L546
	mov	r1, #0
	mov	r0, r3
	mov	r5, #-2147483648
	mov	r2, r1
	b	.L547
.L549:
	add	r4, ip, r1
	ldr	r4, [r4, #4]
	cmp	r4, #0
	blt	.L548
	cmp	r4, r5
	movcc	r5, r4
	movcc	r0, r2
.L548:
	add	r2, r2, #1
	add	r1, r1, #12
	uxth	r2, r2
.L547:
	cmp	r2, r3
	bne	.L549
	cmp	r0, r3
	ldmccfd	sp!, {r4, r5, r6, r7, pc}
	ldr	r2, .L555+8
	mov	r0, r3
	ldr	r1, .L555+4
	mvn	r4, #0
	ldrh	r6, [r1, r2]
	mov	r2, #0
	mov	r1, r2
	b	.L550
.L552:
	add	r5, ip, r2
	ldr	r5, [r5, #4]
	cmp	r5, r4
	bcs	.L551
	ldrh	r7, [ip, r2]
	cmp	r7, r6
	movne	r4, r5
	movne	r0, r1
.L551:
	add	r1, r1, #1
	add	r2, r2, #12
	uxth	r1, r1
.L550:
	cmp	r1, r3
	bne	.L552
	ldmfd	sp!, {r4, r5, r6, r7, pc}
.L556:
	.align	2
.L555:
	.word	.LANCHOR0
	.word	.LANCHOR2
	.word	-1896
	.fnend
	.size	select_l2p_ram_region, .-select_l2p_ram_region
	.align	2
	.global	FtlUpdateVaildLpn
	.type	FtlUpdateVaildLpn, %function
FtlUpdateVaildLpn:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	ldr	r3, .L563
	ldr	r2, .L563+4
	stmfd	sp!, {r4, r5, lr}
	.save {r4, r5, lr}
	ldrh	r1, [r2, r3]
	cmp	r1, #4
	add	ip, r1, #1
	mov	r1, r3
	strh	ip, [r2, r3]	@ movhi
	bhi	.L558
	cmp	r0, #0
	ldmeqfd	sp!, {r4, r5, pc}
.L558:
	ldr	r0, .L563+8
	mov	r3, #0
	strh	r3, [r2, r1]	@ movhi
	movw	r1, #3848
	str	r3, [r2, #-1892]
	movw	ip, #65535
	ldrh	r4, [r0, r1]
	ldr	r1, [r2, #-2064]
	ldr	r2, .L563+4
	b	.L560
.L562:
	ldrh	r0, [r1], #2
	add	r3, r3, #1
	cmp	r0, ip
	uxth	r3, r3
	ldrne	r5, [r2, #-1892]
	addne	r0, r0, r5
	strne	r0, [r2, #-1892]
.L560:
	cmp	r3, r4
	bne	.L562
	ldmfd	sp!, {r4, r5, pc}
.L564:
	.align	2
.L563:
	.word	-1894
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
	ldr	r3, .L569
	mov	r2, r0, lsr #5
	cmp	r1, #0
	and	r0, r0, #31
	uxth	r2, r2
	mov	ip, #1
	ldr	r3, [r3, #-1888]
	ldr	r1, [r3, r2, asl #2]
	orrne	r0, r1, ip, asl r0
	biceq	r0, r1, ip, asl r0
	str	r0, [r3, r2, asl #2]
	bx	lr
.L570:
	.align	2
.L569:
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
	ldr	r3, .L572
	mov	r2, r0, lsr #5
	and	r0, r0, #31
	ldr	r3, [r3, #-1888]
	ldr	r3, [r3, r2, asl #2]
	mov	r0, r3, lsr r0
	and	r0, r0, #1
	bx	lr
.L573:
	.align	2
.L572:
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
	ldr	ip, .L581
	mov	r3, #0
	strh	r3, [r0, #4]	@ movhi
	mov	r3, #3840
	stmfd	sp!, {r4, r5, r6, lr}
	.save {r4, r5, r6, lr}
	ldrh	r3, [ip, r3]
	movw	ip, #65535
	b	.L575
.L577:
	add	r4, r0, r2, asl #1
	add	r2, r2, #1
	ldrh	r4, [r4, #16]
	uxth	r2, r2
	cmp	r4, ip
	ldrneh	r4, [r0, #4]
	addne	r4, r4, #1
	strneh	r4, [r0, #4]	@ movhi
.L575:
	cmp	r2, r3
	bcc	.L577
	ldr	ip, .L581
	movw	r2, #3908
	movw	r4, #65535
	mvn	r1, r1
	ldrh	r5, [ip, r2]
	mov	ip, r0
	mov	r2, #0
	b	.L578
.L580:
	ldrh	r6, [ip, #16]
	add	r2, r2, #1
	add	ip, ip, #2
	cmp	r6, r4
	uxth	r2, r2
	ldrneh	r6, [r0, #4]
	addne	r6, r5, r6
	addne	r6, r6, r1
	strneh	r6, [r0, #4]	@ movhi
.L578:
	cmp	r2, r3
	bne	.L580
	ldmfd	sp!, {r4, r5, r6, pc}
.L582:
	.align	2
.L581:
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
	stmfd	sp!, {r4, r5, r6, r7, r8, r9, sl, lr}
	.save {r4, r5, r6, r7, r8, r9, sl, lr}
	mov	r4, r0
	mov	r5, #0
	ldr	r6, .L589
	strh	r5, [r0, #4]	@ movhi
	mov	r8, #3840
	strb	r5, [r4, #7]
	mvn	r7, #0
	b	.L584
.L586:
	add	r3, r6, r5
	ldrh	r1, [r4, #0]
	add	sl, r5, #8
	add	r5, r5, #1
	ldrb	r0, [r3, #3866]	@ zero_extendqisi2
	bl	V2P_block
	mov	sl, sl, asl #1
	uxth	r5, r5
	strh	r7, [r4, sl]	@ movhi
	mov	r9, r0
	bl	FtlBbmIsBadBlock
	cmp	r0, #0
	streqh	r9, [r4, sl]	@ movhi
	ldreqb	r3, [r4, #7]	@ zero_extendqisi2
	addeq	r3, r3, #1
	streqb	r3, [r4, #7]
.L584:
	ldrh	r3, [r6, r8]
	cmp	r3, r5
	bhi	.L586
	ldr	r1, .L589
	movw	r2, #3908
	ldrb	r3, [r4, #7]	@ zero_extendqisi2
	ldrh	r2, [r1, r2]
	mul	r3, r2, r3
	strh	r3, [r4, #4]	@ movhi
	mov	r3, #0
	strb	r3, [r4, #9]
	ldr	r3, .L589+4
	ldr	r2, [r3, #-1884]
	cmp	r2, #0
	beq	.L587
	ldrh	r1, [r4, #0]
	ldr	r2, [r3, #-2084]
	mov	r3, r1, asl #1
	ldrh	r3, [r2, r3]
	cmp	r3, #59
	movls	r3, #1
	strlsb	r3, [r4, #9]
.L587:
	ldr	r3, .L589
	mov	r0, #0
	ldrb	r3, [r3, #852]	@ zero_extendqisi2
	cmp	r3, #0
	movne	r3, #1
	strneb	r3, [r4, #9]
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, sl, pc}
.L590:
	.align	2
.L589:
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
	stmfd	sp!, {r3, r4, r5, r6, r7, r8, sl, lr}
	.save {r3, r4, r5, r6, r7, r8, sl, lr}
	mov	r4, #0
	mov	r7, r0
	mov	r5, r4
	ldr	r6, .L596
	mov	sl, #3840
	movw	r8, #3908
	b	.L592
.L594:
	add	r3, r6, r5
	mov	r1, r7
	add	r5, r5, #1
	ldrb	r0, [r3, #3866]	@ zero_extendqisi2
	bl	V2P_block
	uxth	r5, r5
	bl	FtlBbmIsBadBlock
	cmp	r0, #0
	ldreqh	r3, [r6, r8]
	addeq	r4, r4, r3
	uxtheq	r4, r4
.L592:
	ldrh	r3, [r6, sl]
	cmp	r3, r5
	bhi	.L594
	cmp	r4, #0
	beq	.L595
	mov	r1, r4
	mov	r0, #32768
	bl	__aeabi_idiv
	uxth	r4, r0
.L595:
	ldr	r3, .L596+4
	mov	r2, #6
	mov	r0, #0
	ldr	r3, [r3, #-2072]
	mla	r7, r2, r7, r3
	strh	r4, [r7, #4]	@ movhi
	ldmfd	sp!, {r3, r4, r5, r6, r7, r8, sl, pc}
.L597:
	.align	2
.L596:
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
	ldr	r3, .L601
	ldr	r0, [r3, #-2052]
	cmp	r0, #0
	bxeq	lr
	ldr	r2, [r3, #-2072]
	rsb	r0, r2, r0
	ldr	r2, .L601+4
	mov	r0, r0, asr #1
	mul	r0, r2, r0
	ldr	r2, [r3, #-2084]
	uxth	r0, r0
	mov	r3, r0, asl #1
	ldrh	r0, [r2, r3]
	bx	lr
.L602:
	.align	2
.L601:
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
	ldr	r2, .L610
	stmfd	sp!, {r4, r5, lr}
	.save {r4, r5, lr}
	ldr	r3, [r2, #-2052]
	cmp	r3, #0
	beq	.L609
	ldr	r1, .L610+4
	mov	ip, #7
	mov	r5, #6
	movw	r4, #65535
	ldrh	r1, [r2, r1]
	mul	r1, ip, r1
	mov	r1, r1, asr #3
	cmp	r0, r1
	uxthgt	r0, r1
	ldr	r1, [r2, #-2072]
	ldr	r2, .L610+8
	rsb	r3, r1, r3
	mov	r3, r3, asr #1
	mul	r3, r2, r3
	mov	r2, #0
	uxth	r3, r3
	b	.L606
.L608:
	mul	ip, r5, r3
	ldrh	ip, [r1, ip]
	cmp	ip, r4
	beq	.L607
	add	r2, r2, #1
	mov	r3, ip
	uxth	r2, r2
.L606:
	cmp	r2, r0
	bne	.L608
.L607:
	ldr	r2, .L610
	mov	r3, r3, asl #1
	ldr	r2, [r2, #-2084]
	ldrh	r0, [r2, r3]
	ldmfd	sp!, {r4, r5, pc}
.L609:
	mov	r0, r3
	ldmfd	sp!, {r4, r5, pc}
.L611:
	.align	2
.L610:
	.word	.LANCHOR2
	.word	-2048
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
	stmfd	sp!, {r4, r5, r6, r7, r8, r9, sl, lr}
	.save {r4, r5, r6, r7, r8, r9, sl, lr}
	mov	r8, r0
	ldr	r6, .L622
	add	r5, r8, #12
	.pad #32
	sub	sp, sp, #32
	ldr	r1, .L622+4
	bl	strcpy
	mov	r0, r5
	ldr	r1, .L622+8
	ldr	r2, [r6, #3048]
	bl	sprintf
	ldr	r1, .L622+12
	ldr	r2, [r6, #3924]
	add	r5, r5, r0
	mov	r0, r5
	bl	sprintf
	ldr	r3, .L622+16
	ldr	r3, [r3, #2932]
	cmp	r3, #1
	add	r5, r5, r0
	bne	.L618
.L613:
	add	r0, sp, #16
	add	r1, sp, #20
	add	r2, sp, #24
	add	r3, sp, #28
	bl	NandcGetTimeCfg
	ldr	r3, [sp, #24]
	ldr	r2, [sp, #16]
	mov	r0, r5
	ldr	r1, .L622+20
	str	r3, [sp, #0]
	ldr	r3, [sp, #28]
	ldr	r4, .L622+24
	ldr	r7, .L622+28
	str	r3, [sp, #4]
	ldr	r3, [sp, #20]
	bl	sprintf
	ldr	r1, .L622+32
	add	r5, r5, r0
	mov	r0, r5
	add	r5, r5, #10
	bl	strcpy
	ldr	r2, [r6, #3968]
	mov	r0, r5
	ldr	r1, .L622+36
	bl	sprintf
	ldr	r1, .L622+40
	ldr	r2, [r4, #-1892]
	add	r5, r5, r0
	mov	r0, r5
	bl	sprintf
	ldr	r1, .L622+44
	ldr	r2, [r4, #-1880]
	add	r5, r5, r0
	mov	r0, r5
	bl	sprintf
	ldr	r1, .L622+48
	ldr	r2, [r4, #-1876]
	add	r5, r5, r0
	mov	r0, r5
	bl	sprintf
	ldr	r1, .L622+52
	ldr	r2, [r4, #-1872]
	add	r5, r5, r0
	mov	r0, r5
	bl	sprintf
	ldr	r1, .L622+56
	ldr	r2, [r4, #-1868]
	add	r5, r5, r0
	mov	r0, r5
	bl	sprintf
	ldr	r1, .L622+60
	ldr	r2, [r4, #-1864]
	add	r5, r5, r0
	mov	r0, r5
	bl	sprintf
	ldr	r1, .L622+64
	ldr	r2, [r4, #-1860]
	add	r5, r5, r0
	mov	r0, r5
	bl	sprintf
	ldr	r2, [r4, #-1856]
	ldr	r1, .L622+68
	mov	r2, r2, lsr #11
	add	r5, r5, r0
	mov	r0, r5
	bl	sprintf
	ldr	r2, [r4, #-1852]
	ldr	r1, .L622+72
	mov	r2, r2, lsr #11
	add	r5, r5, r0
	mov	r0, r5
	bl	sprintf
	ldr	r1, .L622+76
	ldr	r2, [r4, #-1848]
	add	r5, r5, r0
	mov	r0, r5
	bl	sprintf
	ldr	r1, .L622+80
	ldr	r2, [r4, #-1844]
	add	r5, r5, r0
	mov	r0, r5
	bl	sprintf
	add	r5, r5, r0
	bl	FtlBbtCalcTotleCnt
	movw	r2, #3982
	ldr	r1, .L622+84
	ldrh	r2, [r6, r2]
	mov	r3, r0
	mov	r0, r5
	bl	sprintf
	ldr	r1, .L622+88
	ldrh	r2, [r4, r7]
	add	r5, r5, r0
	mov	r0, r5
	bl	sprintf
	ldr	r1, .L622+92
	ldr	r2, [r4, #-1840]
	add	r5, r5, r0
	mov	r0, r5
	bl	sprintf
	ldr	r1, .L622+96
	ldr	r2, [r4, #-1836]
	add	r5, r5, r0
	mov	r0, r5
	bl	sprintf
	ldr	r1, .L622+100
	ldr	r2, [r4, #-1832]
	add	r5, r5, r0
	mov	r0, r5
	bl	sprintf
	ldr	r1, .L622+104
	ldr	r2, [r4, #-2080]
	add	r5, r5, r0
	mov	r0, r5
	bl	sprintf
	ldr	r1, .L622+108
	ldr	r2, [r4, #-1828]
	add	r5, r5, r0
	mov	r0, r5
	bl	sprintf
	ldr	r1, .L622+112
	ldr	r2, [r4, #-1824]
	add	r5, r5, r0
	mov	r0, r5
	bl	sprintf
	ldr	r3, .L622+116
	ldr	r1, .L622+120
	ldrh	r2, [r4, r3]
	add	r5, r5, r0
	mov	r0, r5
	bl	sprintf
	ldr	r3, .L622+124
	ldr	r1, .L622+128
	ldrh	r2, [r4, r3]
	add	r5, r5, r0
	mov	r0, r5
	bl	sprintf
	ldr	r2, [r6, #3948]
	ldr	r1, .L622+132
	add	r5, r5, r0
	mov	r0, r5
	bl	sprintf
	ldr	r2, [r6, #3940]
	ldr	r1, .L622+136
	add	r5, r5, r0
	mov	r0, r5
	bl	sprintf
	ldr	r2, [r6, #3836]
	ldr	r1, .L622+140
	add	r5, r5, r0
	mov	r0, r5
	bl	sprintf
	movw	r3, #4042
	ldrh	r2, [r6, r3]
	ldr	r1, .L622+144
	add	r5, r5, r0
	mov	r0, r5
	bl	sprintf
	movw	r3, #3848
	ldrh	r2, [r6, r3]
	ldr	r1, .L622+148
	add	r5, r5, r0
	mov	r0, r5
	bl	sprintf
	ldr	r3, .L622+152
	ldr	r1, .L622+156
	ldrh	r2, [r4, r3]
	add	r5, r5, r0
	mov	r0, r5
	bl	sprintf
	ldr	r2, [r6, #3852]
	ldr	r1, .L622+160
	add	r5, r5, r0
	mov	r0, r5
	bl	sprintf
	ldr	r3, .L622+164
	ldr	r1, .L622+168
	ldrh	r2, [r4, r3]
	add	r5, r5, r0
	mov	r0, r5
	bl	sprintf
	movw	r3, #3976
	ldrh	r2, [r6, r3]
	ldr	r1, .L622+172
	ldr	r6, .L622+176
	add	r5, r5, r0
	mov	r0, r5
	bl	sprintf
	ldr	r3, .L622+180
	ldr	r1, .L622+184
	ldrh	r2, [r4, r3]
	add	r5, r5, r0
	mov	r0, r5
	bl	sprintf
	ldr	r1, .L622+188
	ldrb	r2, [r4, #-2038]	@ zero_extendqisi2
	add	r5, r5, r0
	mov	r0, r5
	bl	sprintf
	ldrh	r2, [r4, r6]
	ldr	r1, .L622+192
	add	r5, r5, r0
	mov	r0, r5
	bl	sprintf
	ldr	r1, .L622+196
	ldrb	r2, [r4, #-2036]	@ zero_extendqisi2
	add	r5, r5, r0
	mov	r0, r5
	bl	sprintf
	ldr	r3, .L622+200
	ldr	r1, .L622+204
	ldrh	r2, [r4, r3]
	add	r5, r5, r0
	mov	r0, r5
	bl	sprintf
	ldrh	r3, [r4, r6]
	ldr	r2, [r4, #-2064]
	add	r6, r6, #48
	ldr	r1, .L622+208
	mov	r3, r3, asl #1
	ldrh	r2, [r2, r3]
	add	r5, r5, r0
	mov	r0, r5
	bl	sprintf
	ldr	r3, .L622+212
	ldr	r1, .L622+216
	ldrh	r2, [r4, r3]
	add	r5, r5, r0
	mov	r0, r5
	bl	sprintf
	ldr	r1, .L622+220
	ldrb	r2, [r4, #-1990]	@ zero_extendqisi2
	add	r5, r5, r0
	mov	r0, r5
	bl	sprintf
	ldrh	r2, [r4, r6]
	ldr	r1, .L622+224
	add	r5, r5, r0
	mov	r0, r5
	bl	sprintf
	ldr	r1, .L622+228
	ldrb	r2, [r4, #-1988]	@ zero_extendqisi2
	add	r5, r5, r0
	mov	r0, r5
	bl	sprintf
	ldr	r3, .L622+232
	ldr	r1, .L622+236
	ldrh	r2, [r4, r3]
	add	r5, r5, r0
	mov	r0, r5
	bl	sprintf
	ldrh	r3, [r4, r6]
	ldr	r2, [r4, #-2064]
	add	r6, r6, #240
	ldr	r1, .L622+240
	mov	r3, r3, asl #1
	ldrh	r2, [r2, r3]
	add	r5, r5, r0
	mov	r0, r5
	bl	sprintf
	ldr	r3, .L622+244
	ldr	r1, .L622+248
	ldrh	r2, [r4, r3]
	add	r5, r5, r0
	mov	r0, r5
	bl	sprintf
	ldr	r1, .L622+252
	ldrb	r2, [r4, #-1942]	@ zero_extendqisi2
	add	r5, r5, r0
	mov	r0, r5
	bl	sprintf
	ldr	r3, .L622+256
	ldr	r1, .L622+260
	ldrh	r2, [r4, r3]
	add	r5, r5, r0
	mov	r0, r5
	bl	sprintf
	ldr	r1, .L622+264
	ldrb	r2, [r4, #-1940]	@ zero_extendqisi2
	add	r5, r5, r0
	mov	r0, r5
	bl	sprintf
	ldr	r3, .L622+268
	ldr	r1, .L622+272
	ldrh	r2, [r4, r3]
	add	r5, r5, r0
	mov	r0, r5
	bl	sprintf
	ldr	r3, .L622+276
	ldr	r1, .L622+280
	ldrh	r2, [r4, r3]
	add	r5, r5, r0
	mov	r0, r5
	bl	sprintf
	ldr	r1, .L622+284
	ldrb	r2, [r4, #-1750]	@ zero_extendqisi2
	add	r5, r5, r0
	mov	r0, r5
	bl	sprintf
	ldr	r1, .L622+288
	ldrh	r2, [r4, r6]
	add	r5, r5, r0
	mov	r0, r5
	bl	sprintf
	ldr	r1, .L622+292
	ldrb	r2, [r4, #-1748]	@ zero_extendqisi2
	add	r5, r5, r0
	mov	r0, r5
	bl	sprintf
	ldr	r3, .L622+296
	ldr	r1, .L622+300
	ldrh	r2, [r4, r3]
	add	r5, r5, r0
	mov	r0, r5
	bl	sprintf
	ldr	r1, [r4, #-1620]
	ldr	r3, [r4, #-1884]
	ldr	r2, [r4, #-1708]
	str	r1, [sp, #0]
	ldr	r1, [r4, #-1628]
	orr	r2, r3, r2, asl #8
	str	r1, [sp, #4]
	ldr	r1, .L622+304
	ldr	r3, [r4, #-1624]
	add	r5, r5, r0
	mov	r0, r5
	bl	sprintf
	ldr	r1, .L622+308
	ldr	r2, [r4, #-1632]
	add	r5, r5, r0
	mov	r0, r5
	bl	sprintf
	ldr	r1, .L622+312
	ldr	r2, [r4, #-1608]
	add	r5, r5, r0
	mov	r0, r5
	bl	sprintf
	ldr	r3, .L622+316
	ldr	r1, .L622+320
	ldrh	r2, [r4, r3]
	add	r5, r5, r0
	mov	r0, r5
	bl	sprintf
	ldr	r3, .L622+324
	ldr	r1, .L622+328
	ldrh	r2, [r4, r3]
	add	r5, r5, r0
	mov	r0, r5
	bl	sprintf
	ldr	r1, .L622+332
	ldr	r2, [r4, #-1188]
	add	r5, r5, r0
	mov	r0, r5
	bl	sprintf
	ldr	r3, .L622+336
	ldr	r1, .L622+340
	ldrh	r2, [r4, r3]
	add	r5, r5, r0
	mov	r0, r5
	bl	sprintf
	add	r5, r5, r0
	bl	GetFreeBlockMinEraseCount
	ldr	r1, .L622+344
	mov	r2, r0
	mov	r0, r5
	bl	sprintf
	add	r5, r5, r0
	ldrh	r0, [r4, r7]
	bl	GetFreeBlockMaxEraseCount
	ldr	r1, .L622+348
	mov	r2, r0
	mov	r0, r5
	bl	sprintf
	ldrh	r3, [r4, r6]
	movw	r2, #65535
	cmp	r3, r2
	add	r5, r5, r0
	beq	.L615
	ldr	r2, [r4, #-2064]
	mov	r3, r3, asl #1
	mov	r0, r5
	ldr	r1, .L622+352
	ldrh	r2, [r2, r3]
	bl	sprintf
	add	r5, r5, r0
.L615:
	mov	r0, #0
	ldr	r4, .L622+24
	bl	List_get_gc_head_node
	mov	r6, #0
	movw	sl, #65535
	mov	r9, #6
	uxth	r3, r0
.L617:
	cmp	r3, sl
	beq	.L616
	ldr	r1, [r4, #-2064]
	mov	r2, r3, asl #1
	mul	r7, r9, r3
	mov	r0, r5
	ldrh	r1, [r1, r2]
	str	r1, [sp, #0]
	ldr	r1, [r4, #-2072]
	add	r1, r1, r7
	ldrh	r1, [r1, #4]
	str	r1, [sp, #4]
	ldr	r1, [r4, #-2084]
	ldrh	r2, [r1, r2]
	ldr	r1, .L622+356
	str	r2, [sp, #8]
	mov	r2, r6
	bl	sprintf
	add	r6, r6, #1
	ldr	r3, [r4, #-2072]
	cmp	r6, #16
	ldrh	r3, [r3, r7]
	add	r5, r5, r0
	bne	.L617
.L616:
	ldr	r6, .L622+24
	mov	r4, #0
	movw	sl, #65535
	mov	r9, #6
	ldr	r2, [r6, #-2052]
	ldr	r3, [r6, #-2072]
	rsb	r3, r3, r2
	ldr	r2, .L622+360
	mov	r3, r3, asr #1
	mul	r3, r2, r3
	uxth	r3, r3
.L619:
	cmp	r3, sl
	beq	.L618
	mul	r7, r9, r3
	ldr	r2, [r6, #-2072]
	mov	r0, r5
	add	r2, r2, r7
	ldrh	r2, [r2, #4]
	str	r2, [sp, #0]
	mov	r2, r3, asl #1
	ldr	r1, [r6, #-2084]
	ldrh	r2, [r1, r2]
	ldr	r1, .L622+364
	str	r2, [sp, #4]
	mov	r2, r4
	bl	sprintf
	add	r4, r4, #1
	ldr	r3, [r6, #-2072]
	cmp	r4, #4
	ldrh	r3, [r3, r7]
	add	r5, r5, r0
	bne	.L619
.L618:
	rsb	r0, r8, r5
	add	sp, sp, #32
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, sl, pc}
.L623:
	.align	2
.L622:
	.word	.LANCHOR0
	.word	.LC5
	.word	.LC6
	.word	.LC7
	.word	.LANCHOR1
	.word	.LC8
	.word	.LANCHOR2
	.word	-2048
	.word	.LC9
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
	.word	-1790
	.word	.LC30
	.word	-1792
	.word	.LC31
	.word	.LC32
	.word	.LC33
	.word	.LC34
	.word	.LC35
	.word	.LC36
	.word	-1772
	.word	.LC37
	.word	.LC38
	.word	-1768
	.word	.LC39
	.word	.LC40
	.word	-2044
	.word	-2042
	.word	.LC41
	.word	.LC42
	.word	.LC43
	.word	.LC44
	.word	-2040
	.word	.LC45
	.word	.LC46
	.word	-1994
	.word	.LC47
	.word	.LC48
	.word	.LC49
	.word	.LC50
	.word	-1992
	.word	.LC51
	.word	.LC52
	.word	-1946
	.word	.LC53
	.word	.LC54
	.word	-1948
	.word	.LC55
	.word	.LC56
	.word	-1944
	.word	.LC57
	.word	-1754
	.word	.LC58
	.word	.LC59
	.word	.LC60
	.word	.LC61
	.word	-1752
	.word	.LC62
	.word	.LC63
	.word	.LC64
	.word	.LC65
	.word	-1192
	.word	.LC66
	.word	-1190
	.word	.LC67
	.word	.LC68
	.word	-1184
	.word	.LC69
	.word	.LC70
	.word	.LC71
	.word	.LC72
	.word	.LC73
	.word	-1431655765
	.word	.LC74
	.fnend
	.size	FtlPrintInfo2buf, .-FtlPrintInfo2buf
	.align	2
	.global	rknand_proc_ftlread
	.type	rknand_proc_ftlread, %function
rknand_proc_ftlread:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	stmfd	sp!, {r3, r4, r5, lr}
	.save {r3, r4, r5, lr}
	mov	r4, r0
	ldr	r1, .L625
	ldr	r2, .L625+4
	bl	sprintf
	add	r5, r4, r0
	mov	r0, r5
	bl	FtlPrintInfo2buf
	add	r0, r5, r0
	rsb	r0, r4, r0
	ldmfd	sp!, {r3, r4, r5, pc}
.L626:
	.align	2
.L625:
	.word	.LC75
	.word	.LC76
	.fnend
	.size	rknand_proc_ftlread, .-rknand_proc_ftlread
	.align	2
	.global	GetSwlReplaceBlock
	.type	GetSwlReplaceBlock, %function
GetSwlReplaceBlock:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	ldr	r3, .L652
	stmfd	sp!, {r4, r5, r6, r7, r8, r9, sl, fp, lr}
	.save {r4, r5, r6, r7, r8, r9, sl, fp, lr}
	.pad #20
	sub	sp, sp, #20
	ldr	r1, [r3, #-1832]
	ldr	r2, [r3, #-1824]
	cmp	r1, r2
	bcs	.L628
	ldr	r0, .L652+4
	movw	r1, #3848
	mov	r2, #0
	str	r2, [r3, #-1840]
	ldrh	r1, [r0, r1]
	ldr	r0, [r3, #-2084]
	b	.L629
.L630:
	ldrh	lr, [r0], #2
	add	r2, r2, #1
	ldr	ip, [r3, #-1840]
	add	ip, lr, ip
	str	ip, [r3, #-1840]
.L629:
	cmp	r2, r1
	ldr	r4, .L652
	bcc	.L630
	ldr	r5, [r4, #-1840]
	mov	r0, r5
	bl	__aeabi_uidiv
	ldr	r2, .L652+4
	movw	r3, #3898
	ldrh	r1, [r2, r3]
	str	r0, [r4, #-1832]
	ldr	r0, [r4, #-1836]
	rsb	r0, r0, r5
	bl	__aeabi_uidiv
	str	r0, [r4, #-1840]
	b	.L631
.L628:
	ldr	r2, [r3, #-1828]
	cmp	r1, r2
	bls	.L631
	add	r2, r2, #1
	ldr	r4, .L652+4
	str	r2, [r3, #-1828]
	movw	ip, #3848
	mov	r2, #0
	b	.L632
.L633:
	ldr	r0, [r3, #-2084]
	mov	r1, r2, asl #1
	add	r2, r2, #1
	ldrh	r5, [r0, r1]
	add	r5, r5, #1
	strh	r5, [r0, r1]	@ movhi
.L632:
	ldrh	r1, [r4, ip]
	cmp	r2, r1
	bcc	.L633
.L631:
	ldr	r3, .L652
	ldr	r6, [r3, #-1824]
	ldr	r5, [r3, #-1832]
	add	r2, r6, #256
	cmp	r2, r5
	mov	r2, r3
	bls	.L634
	ldr	r1, [r3, #-1828]
	add	r0, r6, #768
	cmp	r0, r1
	bls	.L634
	ldr	r3, [r3, #-1884]
	cmp	r3, #0
	beq	.L651
	cmp	r6, #30
	bhi	.L651
.L634:
	ldr	r3, .L652+8
	ldrh	r0, [r2, r3]
	add	r0, r0, r0, asl #1
	ubfx	r0, r0, #2, #16
	bl	GetFreeBlockMaxEraseCount
	add	r3, r6, #64
	cmp	r0, r3
	mov	r8, r0
	bcs	.L636
	cmp	r6, #30
	bhi	.L651
.L636:
	ldr	r2, .L652
	ldr	r3, [r2, #-2068]
	cmp	r3, #0
	beq	.L651
	ldr	r0, .L652+4
	movw	r1, #3848
	movw	r7, #65535
	mov	r4, r7
	mov	fp, r7
	ldrh	sl, [r0, r1]
	ldr	r0, [r2, #-2072]
	ldr	r1, [r2, #-2084]
	mov	r2, #0
	b	.L637
.L640:
	add	r2, r2, #1
	uxth	r2, r2
	cmp	r2, sl
	bhi	.L651
	ldrh	ip, [r3, #4]
	cmp	ip, #0
	beq	.L638
	rsb	r3, r0, r3
	ldr	ip, .L652+12
	mov	r3, r3, asr #1
	mul	r3, ip, r3
	uxth	r3, r3
	mov	ip, r3, asl #1
	ldrh	ip, [r1, ip]
	cmp	ip, r6
	bls	.L648
	cmp	ip, r7
	movcc	r7, ip
	movcc	r4, r3
.L638:
	mov	ip, #6
	mla	r3, ip, r9, r0
.L637:
	ldrh	r9, [r3, #0]
	cmp	r9, fp
	bne	.L640
	b	.L639
.L648:
	mov	r4, r3
.L639:
	movw	r3, #65535
	cmp	r4, r3
	beq	.L635
	mov	r9, r4, asl #1
	ldrh	sl, [r1, r9]
	cmp	sl, r6
	bls	.L641
	bl	GetFreeBlockMinEraseCount
	cmp	r0, r6
	ldrhi	r3, .L652
	strhi	r7, [r3, #-1824]
.L641:
	cmp	sl, r5
	bcs	.L651
	add	r3, sl, #128
	cmp	r8, r3
	ble	.L651
	add	r3, sl, #256
	ldr	r6, .L652
	cmp	r3, r5
	bcc	.L642
	ldr	r3, [r6, #-1828]
	add	sl, sl, #768
	cmp	sl, r3
	bcs	.L651
.L642:
	ldr	r3, [r6, #-2064]
	mov	r1, r4
	ldr	r0, .L652+16
	mov	r2, r5
	ldrh	r3, [r3, r9]
	str	r3, [sp, #0]
	ldr	r3, [r6, #-2084]
	ldrh	r3, [r3, r9]
	stmib	sp, {r3, r8}
	ldr	r3, [r6, #-1828]
	bl	printk
	mov	r3, #1
	str	r3, [r6, #-1180]
	b	.L635
.L651:
	movw	r4, #65535
.L635:
	mov	r0, r4
	add	sp, sp, #20
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, sl, fp, pc}
.L653:
	.align	2
.L652:
	.word	.LANCHOR2
	.word	.LANCHOR0
	.word	-2048
	.word	-1431655765
	.word	.LC77
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
	beq	.L655
	ldr	r2, .L656
	mov	r3, r0, asl #1
	mov	r1, #0
	ldr	r2, [r2, #-2064]
	strh	r1, [r2, r3]	@ movhi
	bl	INSERT_FREE_LIST
.L655:
	mov	r0, #0
	ldmfd	sp!, {r3, pc}
.L657:
	.align	2
.L656:
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
	ldr	r2, .L666
	mov	r3, #0
	stmfd	sp!, {r4, r5, r6, r7, r8, r9, sl, fp, lr}
	.save {r4, r5, r6, r7, r8, r9, sl, fp, lr}
	mov	sl, #3840
	str	r3, [r2, #-1176]
	mov	r8, #12
	ldr	r0, .L666+4
	mov	r7, #1
	movw	r6, #3918
	mov	fp, #3920
	b	.L659
.L660:
	mul	r1, r8, r3
	ldr	r4, [r2, #-1172]
	add	ip, r4, r1
	str	r7, [ip, #8]
	ldrh	ip, [r0, r6]
	mul	ip, ip, r3
	add	r5, ip, #3
	cmp	ip, #0
	movlt	ip, r5
	ldr	r5, [r2, #-1168]
	bic	ip, ip, #3
	add	ip, r5, ip
	str	ip, [r4, r1]
	ldrh	ip, [r0, fp]
	ldr	r9, [r2, #-1172]
	mul	ip, ip, r3
	add	r4, r9, r1
	add	r5, ip, #3
	cmp	ip, #0
	movlt	ip, r5
	ldr	r5, [r2, #-1164]
	bic	ip, ip, #3
	add	ip, r5, ip
	str	ip, [r4, #4]
	ldr	ip, [r2, #-1160]
	mov	r5, #36
	ldr	r1, [r9, r1]
	mla	ip, r5, r3, ip
	add	r3, r3, #1
	uxth	r3, r3
	str	r1, [ip, #8]
	ldr	r1, [r4, #4]
	str	r1, [ip, #12]
.L659:
	ldrh	r1, [r0, sl]
	cmp	r3, r1
	bcc	.L660
	b	.L665
.L662:
	mul	r2, r8, r1
	ldr	r4, [r3, #-1172]
	add	r0, r4, r2
	str	r7, [r0, #8]
	ldrh	r0, [ip, r6]
	mul	r0, r0, r1
	add	sl, r0, #3
	cmp	r0, #0
	movlt	r0, sl
	ldr	sl, [r3, #-1168]
	bic	r0, r0, #3
	add	r0, sl, r0
	str	r0, [r4, r2]
	ldr	r0, [r3, #-1172]
	add	r0, r0, r2
	ldrh	r2, [ip, r5]
	mul	r2, r2, r1
	add	r1, r1, #1
	uxth	r1, r1
	add	r4, r2, #3
	cmp	r2, #0
	movlt	r2, r4
	ldr	r4, [r3, #-1164]
	bic	r2, r2, #3
	add	r2, r4, r2
	str	r2, [r0, #4]
	b	.L664
.L665:
	ldr	r3, .L666
	mov	r8, #12
	ldr	ip, .L666+4
	mov	r7, #0
	movw	r6, #3918
	mov	r5, #3920
.L664:
	ldr	r2, [r3, #-1156]
	cmp	r1, r2
	bcc	.L662
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, sl, fp, pc}
.L667:
	.align	2
.L666:
	.word	.LANCHOR2
	.word	.LANCHOR0
	.fnend
	.size	FtlGcBufInit, .-FtlGcBufInit
	.align	2
	.global	FtlGcBufFree
	.type	FtlGcBufFree, %function
FtlGcBufFree:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	ldr	r3, .L675
	stmfd	sp!, {r4, r5, r6, r7, r8, r9, sl, fp, lr}
	.save {r4, r5, r6, r7, r8, r9, sl, fp, lr}
	mov	r5, #36
	ldr	r6, [r3, #-1156]
	mov	fp, #12
	ldr	ip, [r3, #-1172]
	mov	r3, #0
	mov	r4, r3
	b	.L669
.L672:
	mul	sl, fp, r2
	add	r8, ip, sl
	ldr	r9, [ip, sl]
	ldr	sl, [r7, #8]
	cmp	r9, sl
	streq	r4, [r8, #8]
	beq	.L671
.L670:
	add	r2, r2, #1
	uxth	r2, r2
.L674:
	cmp	r2, r6
	bcc	.L672
.L671:
	add	r3, r3, #1
	uxth	r3, r3
.L669:
	cmp	r3, r1
	ldmcsfd	sp!, {r4, r5, r6, r7, r8, r9, sl, fp, pc}
	mla	r7, r5, r3, r0
	mov	r2, #0
	b	.L674
.L676:
	.align	2
.L675:
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
	ldr	r3, .L684
	stmfd	sp!, {r4, r5, r6, r7, r8, sl, lr}
	.save {r4, r5, r6, r7, r8, sl, lr}
	mov	r6, #12
	ldr	r8, [r3, #-1156]
	mov	r5, #1
	ldr	r7, [r3, #-1172]
	mov	r4, #36
	mov	r3, #0
	b	.L678
.L681:
	mla	ip, r6, r2, r7
	ldr	sl, [ip, #8]
	cmp	sl, #0
	bne	.L679
	mla	r2, r4, r3, r0
	ldr	sl, [ip, #0]
	str	r5, [ip, #8]
	str	sl, [r2, #8]
	ldr	ip, [ip, #4]
	str	ip, [r2, #12]
	b	.L680
.L679:
	add	r2, r2, #1
	uxth	r2, r2
	b	.L682
.L683:
	mov	r2, #0
.L682:
	cmp	r2, r8
	bcc	.L681
.L680:
	add	r3, r3, #1
	uxth	r3, r3
.L678:
	cmp	r3, r1
	bcc	.L683
	ldmfd	sp!, {r4, r5, r6, r7, r8, sl, pc}
.L685:
	.align	2
.L684:
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
	ldr	r3, .L691
	ldr	r2, .L691+4
	ldrh	r1, [r3, r2]
	ldr	r2, [r3, #-1148]
	mov	r3, #0
	b	.L687
.L689:
	ldrh	ip, [r2], #2
	cmp	ip, r0
	beq	.L690
	add	r3, r3, #1
	uxth	r3, r3
.L687:
	cmp	r3, r1
	bne	.L689
	mov	r0, #0
	bx	lr
.L690:
	mov	r0, #1
	bx	lr
.L692:
	.align	2
.L691:
	.word	.LANCHOR2
	.word	-1152
	.fnend
	.size	IsBlkInGcList, .-IsBlkInGcList
	.align	2
	.global	FtlGcUpdatePage
	.type	FtlGcUpdatePage, %function
FtlGcUpdatePage:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	stmfd	sp!, {r3, r4, r5, r6, r7, lr}
	.save {r3, r4, r5, r6, r7, lr}
	mov	r4, r0
	ubfx	r0, r0, #10, #16
	mov	r5, r1
	mov	r6, r2
	bl	P2V_block_in_plane
	ldr	r3, .L697
	ldr	r2, .L697+4
	ldrh	ip, [r3, r2]
	ldr	r2, [r3, #-1148]
	mov	r3, #0
	mov	r1, r2
	b	.L694
.L696:
	ldrh	r7, [r1], #2
	cmp	r7, r0
	beq	.L695
	add	r3, r3, #1
	uxth	r3, r3
.L694:
	cmp	r3, ip
	bne	.L696
	mov	r3, r3, asl #1
	strh	r0, [r2, r3]	@ movhi
	ldr	r2, .L697
	ldr	r3, .L697+4
	ldrh	r1, [r2, r3]
	add	r1, r1, #1
	strh	r1, [r2, r3]	@ movhi
.L695:
	ldr	r3, .L697
	mov	r0, #12
	ldr	r2, .L697+8
	ldrh	r1, [r3, r2]
	mul	r1, r0, r1
	ldr	r0, [r3, #-1144]
	add	ip, r0, r1
	stmib	ip, {r5, r6}
	str	r4, [r0, r1]
	ldrh	r1, [r3, r2]
	add	r1, r1, #1
	strh	r1, [r3, r2]	@ movhi
	ldmfd	sp!, {r3, r4, r5, r6, r7, pc}
.L698:
	.align	2
.L697:
	.word	.LANCHOR2
	.word	-1152
	.word	-1140
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
	mov	r4, r0
	mov	r1, r4
	ldr	r0, .L702
	bl	printk
	ldr	r3, .L702+4
	ldr	ip, .L702+8
	ldrh	r5, [r3, ip]
	cmp	r5, r4
	beq	.L700
	ldr	r2, .L702+12
	ldrh	r0, [r3, r2]
	cmp	r0, r4
	beq	.L700
	movw	r1, #65535
	cmp	r5, r1
	streqh	r4, [r3, ip]	@ movhi
	beq	.L700
	cmp	r0, r1
	streqh	r4, [r3, r2]	@ movhi
.L700:
	mov	r0, #0
	ldmfd	sp!, {r3, r4, r5, pc}
.L703:
	.align	2
.L702:
	.word	.LC78
	.word	.LANCHOR2
	.word	-1138
	.word	-1136
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
	mov	r4, r0
	mov	r1, r4
	ldr	r0, .L707
	bl	printk
	ldr	r3, .L707+4
	ldr	ip, .L707+8
	ldrh	r5, [r3, ip]
	cmp	r5, r4
	beq	.L705
	ldr	r2, .L707+12
	ldrh	r0, [r3, r2]
	cmp	r0, r4
	beq	.L705
	movw	r1, #65535
	cmp	r5, r1
	streqh	r4, [r3, ip]	@ movhi
	beq	.L705
	cmp	r0, r1
	streqh	r4, [r3, r2]	@ movhi
.L705:
	mov	r0, #0
	ldmfd	sp!, {r3, r4, r5, pc}
.L708:
	.align	2
.L707:
	.word	.LC78
	.word	.LANCHOR2
	.word	-1138
	.word	-1136
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
	mov	r4, r0
	bl	P2V_block_in_plane
	ldr	r5, .L714
	mov	r2, r4
	ldr	r7, .L714+4
	ldrh	r1, [r5, r7]
	mov	r6, r0
	ldr	r0, .L714+8
	bl	printk
	mov	r0, r6
	bl	FtlGcRefreshBlock
	ldr	r3, [r5, #-1884]
	cmp	r3, #0
	beq	.L710
	ldr	r3, [r5, #-2084]
	mov	r6, r6, asl #1
	ldrh	r2, [r3, r6]
	cmp	r2, #29
	subhi	r2, r2, #30
	strhih	r2, [r3, r6]	@ movhi
.L710:
	ldrh	r1, [r5, r7]
	mov	r3, #0
	ldr	r2, .L714+12
	b	.L711
.L713:
	ldrh	r0, [r2, #2]!
	cmp	r0, r4
	beq	.L712
	add	r3, r3, #1
	uxth	r3, r3
.L711:
	cmp	r3, r1
	bne	.L713
	cmp	r3, #15
	bhi	.L712
	ldr	r2, .L714
	ldr	r1, .L714+16
	add	r0, r2, r3, asl #1
	add	r3, r3, #1
	strh	r4, [r0, r1]	@ movhi
	sub	r1, r1, #2
	strh	r3, [r2, r1]	@ movhi
.L712:
	mov	r0, #0
	ldmfd	sp!, {r3, r4, r5, r6, r7, pc}
.L715:
	.align	2
.L714:
	.word	.LANCHOR2
	.word	-1134
	.word	.LC79
	.word	.LANCHOR2-1134
	.word	-1132
	.fnend
	.size	FtlGcMarkBadPhyBlk, .-FtlGcMarkBadPhyBlk
	.align	2
	.global	FtlGcReFreshBadBlk
	.type	FtlGcReFreshBadBlk, %function
FtlGcReFreshBadBlk:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	stmfd	sp!, {r3, r4, r5, lr}
	.save {r3, r4, r5, lr}
	ldr	r2, .L719
	ldr	r3, .L719+4
	ldrh	r2, [r3, r2]
	cmp	r2, #0
	beq	.L717
	ldr	r1, .L719+8
	ldrh	r0, [r3, r1]
	movw	r1, #65535
	cmp	r0, r1
	bne	.L717
	ldr	r4, .L719+12
	ldr	r5, .L719+4
	ldrh	r1, [r3, r4]
	cmp	r1, r2
	movcs	r2, #0
	strcsh	r2, [r3, r4]	@ movhi
	ldrh	r2, [r5, r4]
	ldr	r3, .L719+16
	add	r2, r5, r2, asl #1
	ldrh	r0, [r2, r3]
	bl	P2V_block_in_plane
	bl	FtlGcRefreshBlock
	ldrh	r3, [r5, r4]
	add	r3, r3, #1
	strh	r3, [r5, r4]	@ movhi
.L717:
	mov	r0, #0
	ldmfd	sp!, {r3, r4, r5, pc}
.L720:
	.align	2
.L719:
	.word	-1134
	.word	.LANCHOR2
	.word	-1138
	.word	-1098
	.word	-1132
	.fnend
	.size	FtlGcReFreshBadBlk, .-FtlGcReFreshBadBlk
	.align	2
	.global	ftl_memset
	.type	ftl_memset, %function
ftl_memset:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	cmp	r2, #0
	stmfd	sp!, {r4, lr}
	.save {r4, lr}
	mov	r4, r0
	beq	.L722
	bl	memset
.L722:
	mov	r0, r4
	ldmfd	sp!, {r4, pc}
	.fnend
	.size	ftl_memset, .-ftl_memset
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
	ldr	r4, .L724
	movw	r5, #3912
	ldr	r2, .L724+4
	mov	r1, #255
	ldr	r6, .L724+8
	ldr	r0, [r4, #-1148]
	strh	r3, [r4, r2]	@ movhi
	add	r2, r2, #12
	strh	r3, [r4, r2]	@ movhi
	ldrh	r2, [r6, r5]
	mov	r2, r2, asl #1
	bl	ftl_memset
	ldrh	r3, [r6, r5]
	mov	r2, #12
	ldr	r0, [r4, #-1144]
	mov	r1, #255
	mul	r2, r2, r3
	bl	ftl_memset
	ldmfd	sp!, {r4, r5, r6, lr}
	b	FtlGcBufInit
.L725:
	.align	2
.L724:
	.word	.LANCHOR2
	.word	-1152
	.word	.LANCHOR0
	.fnend
	.size	FtlGcPageVarInit, .-FtlGcPageVarInit
	.align	2
	.global	SupperBlkListInit
	.type	SupperBlkListInit, %function
SupperBlkListInit:
	.fnstart
	@ args = 0, pretend = 0, frame = 8
	@ frame_needed = 0, uses_anonymous_args = 0
	stmfd	sp!, {r0, r1, r2, r4, r5, r6, r7, r8, r9, sl, fp, lr}
	.save {r0, r1, r2, r4, r5, r6, r7, r8, r9, sl, fp, lr}
	movw	r3, #3850
	ldr	r7, .L736
	mov	r2, #6
	ldr	r4, .L736+4
	mov	r1, #0
	mov	r6, #0
	ldrh	r3, [r7, r3]
	mov	sl, r6
	ldr	r0, [r4, #-2072]
	mul	r2, r2, r3
	bl	ftl_memset
	ldr	r3, .L736+8
	str	r6, [r4, #-2052]
	str	r6, [r4, #-2068]
	strh	r6, [r4, r3]	@ movhi
	add	r3, r3, #8
	str	r6, [r4, #-2060]
	strh	r6, [r4, r3]	@ movhi
	add	r3, r3, #952
	strh	r6, [r4, r3]	@ movhi
	mov	r4, r6
	b	.L727
.L729:
	add	r1, r7, r1
	add	r9, r9, #1
	ldrb	r0, [r1, #3866]	@ zero_extendqisi2
	mov	r1, r3
	stmia	sp, {r2, r3}
	bl	V2P_block
	bl	FtlBbmIsBadBlock
	ldmia	sp, {r2, r3}
	uxth	r9, r9
	cmp	r0, #0
	ldreqh	r1, [r7, r2]
	addeq	r5, r5, r1
	uxtheq	r5, r5
	b	.L734
.L735:
	mov	r5, #0
	uxth	r3, r4
	mov	r9, r5
	mov	fp, #3840
	movw	r2, #3908
.L734:
	ldrh	r0, [r7, fp]
	sxth	r1, r9
	cmp	r1, r0
	blt	.L729
	cmp	r5, #0
	ldr	r9, .L736+4
	beq	.L730
	sxth	r1, r5
	mov	r0, #32768
	bl	__aeabi_idiv
	uxth	r5, r0
	b	.L731
.L730:
	sxth	r3, r4
	ldr	r2, [r9, #-2064]
	mvn	r1, #0
	mov	r3, r3, asl #1
	strh	r1, [r2, r3]	@ movhi
.L731:
	sxth	r1, r4
	ldr	r0, [r9, #-2072]
	ldr	r3, .L736+4
	mov	r2, r1, asl #1
	add	r1, r2, r1
	add	r1, r0, r1, asl #1
	strh	r5, [r1, #4]	@ movhi
	ldr	r1, .L736+12
	ldrh	r1, [r9, r1]
	cmp	r8, r1
	beq	.L732
	ldr	r1, .L736+16
	ldrh	r1, [r3, r1]
	cmp	r8, r1
	beq	.L732
	ldr	r1, .L736+20
	ldrh	r1, [r3, r1]
	cmp	r8, r1
	beq	.L732
	ldr	r3, [r3, #-2064]
	uxth	r0, r4
	ldrh	r3, [r3, r2]
	cmp	r3, #0
	bne	.L733
	add	r6, r6, #1
	uxth	r6, r6
	bl	INSERT_FREE_LIST
	b	.L732
.L733:
	add	sl, sl, #1
	uxth	sl, sl
	bl	INSERT_DATA_LIST
.L732:
	add	r4, r4, #1
	uxth	r4, r4
.L727:
	movw	r2, #3848
	sxth	r8, r4
	ldrh	r3, [r7, r2]
	cmp	r8, r3
	blt	.L735
	ldr	r3, .L736+4
	mov	r0, #0
	ldr	r2, .L736+8
	strh	sl, [r3, r2]	@ movhi
	add	r2, r2, #8
	strh	r6, [r3, r2]	@ movhi
	ldmfd	sp!, {r1, r2, r3, r4, r5, r6, r7, r8, r9, sl, fp, pc}
.L737:
	.align	2
.L736:
	.word	.LANCHOR0
	.word	.LANCHOR2
	.word	-2056
	.word	-2044
	.word	-1996
	.word	-1948
	.fnend
	.size	SupperBlkListInit, .-SupperBlkListInit
	.align	2
	.global	FtlL2PDataInit
	.type	FtlL2PDataInit, %function
FtlL2PDataInit:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	stmfd	sp!, {r3, r4, r5, r6, r7, r8, sl, lr}
	.save {r3, r4, r5, r6, r7, r8, sl, lr}
	mov	r1, #0
	ldr	r4, .L741
	movw	r6, #3918
	ldr	r5, .L741+4
	movw	r7, #3946
	mov	r8, #12
	ldr	r2, [r4, #3936]
	ldr	r0, [r5, #-1092]
	mov	r2, r2, asl #1
	bl	ftl_memset
	ldrh	r3, [r4, r6]
	ldrh	r2, [r4, r7]
	mov	r1, #255
	ldr	r0, [r5, #-1088]
	mul	r2, r2, r3
	bl	ftl_memset
	mov	r2, #0
	mov	ip, r2
	mvn	r0, #0
	b	.L739
.L740:
	mul	r3, r8, r2
	ldr	r1, [r5, #-1900]
	add	sl, r1, r3
	str	ip, [sl, #4]
	strh	r0, [r1, r3]	@ movhi
	ldr	r1, [r5, #-1900]
	ldr	sl, [r5, #-1088]
	add	r3, r1, r3
	ldrh	r1, [r4, r6]
	mul	r1, r2, r1
	add	r2, r2, #1
	uxth	r2, r2
	bic	r1, r1, #3
	add	r1, sl, r1
	str	r1, [r3, #8]
.L739:
	ldrh	r3, [r4, r7]
	ldr	r1, .L741
	cmp	r3, r2
	ldr	r3, .L741+4
	bhi	.L740
	ldr	r0, .L741+8
	mvn	r2, #0
	ldr	ip, [r1, #3936]
	strh	r2, [r3, r0]	@ movhi
	sub	r0, r0, #2
	strh	r2, [r3, r0]	@ movhi
	add	r0, r0, #10
	strh	ip, [r3, r0]	@ movhi
	sub	r0, r0, #6
	ldr	ip, .L741+12
	strh	ip, [r3, r0]	@ movhi
	add	r0, r0, #40
	ldrh	ip, [r3, r0]
	sub	r0, r0, #36
	strh	ip, [r3, r0]	@ movhi
	movw	r0, #3944
	ldrh	r0, [r1, r0]
	ldr	r1, .L741+16
	strh	r0, [r3, r1]	@ movhi
	ldr	r1, [r3, #-1036]
	str	r1, [r3, #-1072]
	ldr	r1, [r3, #-1032]
	str	r1, [r3, #-1068]
	ldr	r1, [r3, #-1092]
	str	r1, [r3, #-1064]
	ldr	r1, [r3, #-1028]
	str	r1, [r3, #-1060]
	ldr	r1, .L741+20
	strh	r2, [r3, r1]	@ movhi
	ldmfd	sp!, {r3, r4, r5, r6, r7, r8, sl, pc}
.L742:
	.align	2
.L741:
	.word	.LANCHOR0
	.word	.LANCHOR2
	.word	-1082
	.word	-3902
	.word	-1078
	.word	-1044
	.fnend
	.size	FtlL2PDataInit, .-FtlL2PDataInit
	.align	2
	.global	ftl_free_no_use_map_blk
	.type	ftl_free_no_use_map_blk, %function
ftl_free_no_use_map_blk:
	.fnstart
	@ args = 0, pretend = 0, frame = 8
	@ frame_needed = 0, uses_anonymous_args = 0
	stmfd	sp!, {r0, r1, r2, r4, r5, r6, r7, r8, r9, sl, fp, lr}
	.save {r0, r1, r2, r4, r5, r6, r7, r8, r9, sl, fp, lr}
	mov	r4, r0
	ldrh	r2, [r0, #10]
	mov	r1, #0
	ldr	r5, [r0, #20]
	ldr	r6, [r0, #12]
	ldr	r7, [r0, #24]
	mov	r2, r2, asl #1
	mov	r0, r5
	bl	ftl_memset
	mov	r3, #0
	b	.L744
.L748:
	ldr	r0, [r7, r3, asl #2]
	mov	r2, #0
	ubfx	r0, r0, #10, #16
	b	.L745
.L747:
	mov	r1, r2, asl #1
	add	r2, r2, #1
	ldrh	ip, [r6, r1]
	uxth	r2, r2
	cmp	ip, r0
	ldreqh	ip, [r5, r1]
	addeq	ip, ip, #1
	streqh	ip, [r5, r1]	@ movhi
.L745:
	ldrh	r1, [r4, #10]
	cmp	r1, r2
	bhi	.L747
	add	r3, r3, #1
	uxth	r3, r3
.L744:
	ldrh	r2, [r4, #6]
	cmp	r2, r3
	bhi	.L748
	mov	sl, #0
	ldrh	fp, [r5, #0]
	mov	r7, sl
	ldr	r3, .L754
	movw	r2, #3910
	b	.L749
.L753:
	ldrh	r1, [r4, #0]
	cmp	r1, r7
	bne	.L750
	ldrh	r0, [r4, #2]
	ldrh	r1, [r3, r2]
	cmp	r0, r1
	movcc	r0, r7, asl #1
	strcch	r1, [r5, r0]	@ movhi
.L750:
	mov	r9, r7, asl #1
	ldrh	r8, [r5, r9]
	cmp	fp, r8
	movhi	sl, r7
	movhi	fp, r8
	cmp	r8, #0
	bne	.L752
	ldrh	r0, [r6, r9]
	cmp	r0, #0
	beq	.L752
	mov	r1, #1
	stmia	sp, {r2, r3}
	bl	FtlFreeSysBlkQueueIn
	strh	r8, [r6, r9]	@ movhi
	ldmia	sp, {r2, r3}
	ldrh	r1, [r4, #8]
	sub	r1, r1, #1
	strh	r1, [r4, #8]	@ movhi
.L752:
	add	r7, r7, #1
	uxth	r7, r7
.L749:
	ldrh	r1, [r4, #10]
	cmp	r1, r7
	bhi	.L753
	mov	r0, sl
	ldmfd	sp!, {r1, r2, r3, r4, r5, r6, r7, r8, r9, sl, fp, pc}
.L755:
	.align	2
.L754:
	.word	.LANCHOR0
	.fnend
	.size	ftl_free_no_use_map_blk, .-ftl_free_no_use_map_blk
	.align	2
	.global	FtlFreeSysBlkQueueInit
	.type	FtlFreeSysBlkQueueInit, %function
FtlFreeSysBlkQueueInit:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	ldr	r3, .L757
	movw	r2, #4038
	stmfd	sp!, {r4, lr}
	.save {r4, lr}
	mov	r4, #0
	strh	r4, [r3, r2]	@ movhi
	add	r2, r2, #2
	mov	r1, r4
	strh	r4, [r3, r2]	@ movhi
	add	r2, r2, #2
	strh	r4, [r3, r2]	@ movhi
	movw	r2, #4036
	strh	r0, [r3, r2]	@ movhi
	mov	r2, #2048
	ldr	r0, .L757+4
	bl	ftl_memset
	mov	r0, r4
	ldmfd	sp!, {r4, pc}
.L758:
	.align	2
.L757:
	.word	.LANCHOR0
	.word	.LANCHOR0+4044
	.fnend
	.size	FtlFreeSysBlkQueueInit, .-FtlFreeSysBlkQueueInit
	.align	2
	.global	FtlBbtMemInit
	.type	FtlBbtMemInit, %function
FtlBbtMemInit:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	@ link register save eliminated.
	ldr	r3, .L760
	movw	r2, #3976
	mvn	r1, #0
	ldr	r0, .L760+4
	strh	r1, [r3, r2]	@ movhi
	add	r2, r2, #6
	mov	r1, #0
	strh	r1, [r3, r2]	@ movhi
	mov	r1, #255
	mov	r2, #16
	b	ftl_memset
.L761:
	.align	2
.L760:
	.word	.LANCHOR0
	.word	.LANCHOR0+3988
	.fnend
	.size	FtlBbtMemInit, .-FtlBbtMemInit
	.align	2
	.global	FtlBbt2Bitmap
	.type	FtlBbt2Bitmap, %function
FtlBbt2Bitmap:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	ldr	r3, .L766
	ldr	r2, .L766+4
	stmfd	sp!, {r4, r5, r6, lr}
	.save {r4, r5, r6, lr}
	mov	r5, r0
	ldrh	r2, [r2, r3]
	mov	r0, r1
	mov	r4, r1
	mov	r1, #0
	mov	r2, r2, asl #2
	bl	ftl_memset
	mov	r3, #0
	movw	ip, #65535
	mov	r0, #1
.L764:
	ldrh	r2, [r5, r3]
	cmp	r2, ip
	ldmeqfd	sp!, {r4, r5, r6, pc}
	mov	r1, r2, lsr #5
	add	r3, r3, #2
	cmp	r3, #1024
	and	r2, r2, #31
	ldr	r6, [r4, r1, asl #2]
	orr	r2, r6, r0, asl r2
	str	r2, [r4, r1, asl #2]
	bne	.L764
	ldmfd	sp!, {r4, r5, r6, pc}
.L767:
	.align	2
.L766:
	.word	-1024
	.word	.LANCHOR2
	.fnend
	.size	FtlBbt2Bitmap, .-FtlBbt2Bitmap
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
	ldr	r5, .L769
	mov	r4, #0
	ldr	r2, .L769+4
	mov	r1, r4
	ldr	r6, .L769+8
	movw	r7, #3850
	str	r3, [r5, #-1004]
	strh	r3, [r5, r2]	@ movhi
	movw	r3, #3954
	strh	r4, [r6, r3]	@ movhi
	movw	r3, #3928
	ldrh	r2, [r6, r3]
	ldr	r0, [r6, #3956]
	str	r4, [r5, #-1020]
	mov	r2, r2, asl #1
	str	r4, [r5, #-1012]
	str	r4, [r5, #-1008]
	str	r4, [r5, #-1884]
	bl	ftl_memset
	ldrh	r2, [r6, r7]
	mov	r1, r4
	ldr	r0, [r5, #-2084]
	mov	r2, r2, asl #1
	bl	ftl_memset
	ldrh	r2, [r6, r7]
	mov	r1, r4
	ldr	r0, [r5, #-1000]
	mov	r2, r2, asl #1
	bl	ftl_memset
	mov	r1, r4
	mov	r2, #48
	ldr	r0, .L769+12
	bl	ftl_memset
	mov	r1, r4
	mov	r2, #512
	ldr	r0, .L769+16
	bl	ftl_memset
	bl	FtlGcBufInit
	bl	FtlL2PDataInit
	mov	r0, r4
	ldmfd	sp!, {r3, r4, r5, r6, r7, pc}
.L770:
	.align	2
.L769:
	.word	.LANCHOR2
	.word	-1016
	.word	.LANCHOR0
	.word	.LANCHOR2-1820
	.word	.LANCHOR2-1704
	.fnend
	.size	FtlVariablesInit, .-FtlVariablesInit
	.align	2
	.global	FtlMemInit
	.type	FtlMemInit, %function
FtlMemInit:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	stmfd	sp!, {r4, r5, r6, r7, r8, r9, sl, lr}
	.save {r4, r5, r6, r7, r8, r9, sl, lr}
	mov	r6, #0
	ldr	r4, .L802
	mov	r0, #1024
	ldr	r3, .L802+4
	mov	r7, #12
	ldr	r2, .L802+8
	mov	sl, #3840
	ldr	r5, .L802+12
	mov	r8, #36
	strh	r6, [r4, r3]	@ movhi
	movw	r3, #65535
	str	r3, [r4, #-988]
	mvn	r3, #0
	strh	r3, [r4, r2]	@ movhi
	add	r2, r2, #2
	str	r6, [r4, #-996]
	strh	r3, [r4, r2]	@ movhi
	mov	r2, #32
	ldr	r3, .L802+16
	str	r6, [r4, #-1708]
	str	r6, [r4, #-1848]
	strh	r2, [r4, r3]	@ movhi
	add	r3, r3, #2
	mov	r2, #128
	str	r6, [r4, #-1844]
	strh	r2, [r4, r3]	@ movhi
	add	r3, r3, #6
	str	r6, [r4, #-1860]
	strh	r6, [r4, r3]	@ movhi
	add	r3, r3, #50
	str	r6, [r4, #-1872]
	strh	r6, [r4, r3]	@ movhi
	add	r3, r3, #154
	str	r6, [r4, #-1876]
	strh	r6, [r4, r3]	@ movhi
	sub	r3, r3, #118
	str	r6, [r4, #-1868]
	strh	r6, [r4, r3]	@ movhi
	movw	r3, #3914
	ldrh	r1, [r5, r3]
	str	r6, [r4, #-1864]
	str	r6, [r4, #-1880]
	str	r6, [r4, #-1840]
	str	r6, [r4, #-1836]
	str	r6, [r4, #-2080]
	str	r6, [r4, #-1828]
	str	r6, [r4, #-1824]
	str	r6, [r4, #-992]
	str	r6, [r4, #-1180]
	str	r6, [r4, #-984]
	str	r6, [r4, #-1188]
	str	r6, [r5, #3972]
	bl	__aeabi_idiv
	mov	r3, #3840
	ldrh	r3, [r5, r3]
	str	r6, [r5, #3960]
	movw	r6, #3912
	mov	r3, r3, asl #2
	cmp	r0, r3
	str	r0, [r4, #-976]
	ldrh	r0, [r5, r6]
	strhi	r3, [r4, #-976]
	ldr	r4, .L802
	mov	r0, r0, asl #1
	bl	ftl_malloc
	str	r0, [r4, #-1148]
	ldrh	r0, [r5, r6]
	mul	r0, r7, r0
	bl	ftl_malloc
	ldrh	r6, [r5, sl]
	mul	r6, r8, r6
	mov	r9, r6, asl #3
	str	r0, [r4, #-1144]
	mov	r0, r9
	bl	ftl_malloc
	str	r0, [r4, #-972]
	mov	r0, r6
	bl	ftl_malloc
	str	r0, [r4, #-968]
	mov	r0, r9
	bl	ftl_malloc
	str	r0, [r4, #-964]
	mov	r0, r6
	bl	ftl_malloc
	str	r0, [r4, #-2088]
	mov	r0, r6
	bl	ftl_malloc
	str	r0, [r4, #-1160]
	ldr	r0, [r4, #-976]
	mul	r0, r8, r0
	bl	ftl_malloc
	movw	r8, #3918
	ldrh	r3, [r5, sl]
	ldrh	r6, [r5, r8]
	mov	r3, r3, asl #1
	add	r3, r3, #1
	str	r3, [r4, #-1156]
	str	r0, [r5, #3964]
	mov	r0, r6
	bl	ftl_malloc
	str	r0, [r4, #-2076]
	mov	r0, r6
	bl	ftl_malloc
	str	r0, [r4, #-960]
	mov	r0, r6
	bl	ftl_malloc
	str	r0, [r4, #-956]
	ldr	r0, [r4, #-1156]
	mul	r0, r0, r6
	bl	ftl_malloc
	str	r0, [r4, #-1168]
	ldr	r0, [r4, #-976]
	mul	r0, r0, r6
	bl	ftl_malloc
	str	r0, [r4, #-952]
	mov	r0, r6
	bl	ftl_malloc
	str	r0, [r4, #-948]
	mov	r0, r6
	bl	ftl_malloc
	mov	r6, #3920
	str	r0, [r4, #-944]
	ldr	r0, [r4, #-1156]
	mul	r0, r7, r0
	bl	ftl_malloc
	ldrh	r3, [r5, r6]
	ldrh	sl, [r5, sl]
	mul	sl, sl, r3
	str	r0, [r4, #-1172]
	mov	r0, sl
	bl	ftl_malloc
	str	r0, [r4, #-940]
	mov	r0, sl, asl #3
	bl	ftl_malloc
	ldrh	r3, [r5, r6]
	ldr	sl, .L802+20
	str	r0, [r4, #-936]
	ldr	r0, [r4, #-1156]
	mul	r0, r0, r3
	bl	ftl_malloc
	ldrh	r3, [r5, r6]
	movw	r6, #3850
	str	r0, [r4, #-1164]
	ldr	r0, [r4, #-976]
	mul	r0, r0, r3
	bl	ftl_malloc
	str	r0, [r4, #-932]
	ldrh	r0, [r5, r6]
	mov	r0, r0, asl #1
	uxth	r0, r0
	strh	r0, [r4, sl]	@ movhi
	bl	ftl_malloc
	str	r0, [r4, #-1000]
	ldrh	r0, [r4, sl]
	add	r0, r0, #544
	add	r0, r0, #3
	mov	r0, r0, lsr #9
	strh	r0, [r4, sl]	@ movhi
	mov	r0, r0, asl #9
	bl	ftl_malloc
	ldrh	sl, [r5, r6]
	mov	sl, sl, asl #1
	str	r0, [r4, #-924]
	add	r0, r0, #32
	str	r0, [r4, #-2084]
	mov	r0, sl
	bl	ftl_malloc
	str	r0, [r4, #-920]
	mov	r0, sl
	bl	ftl_malloc
	ldr	sl, [r5, #3936]
	mov	sl, sl, asl #1
	str	r0, [r4, #-2064]
	mov	r0, sl
	bl	ftl_malloc
	str	r0, [r4, #-1036]
	mov	r0, sl
	bl	ftl_malloc
	movw	sl, #3928
	str	r0, [r4, #-1092]
	ldrh	r0, [r5, r6]
	mov	r0, r0, lsr #3
	add	r0, r0, #4
	bl	ftl_malloc
	str	r0, [r4, #-1888]
	ldrh	r0, [r5, sl]
	mov	r0, r0, asl #1
	bl	ftl_malloc
	str	r0, [r5, #3956]
	ldrh	r0, [r5, sl]
	mov	r0, r0, asl #1
	bl	ftl_malloc
	str	r0, [r4, #-916]
	ldrh	r0, [r5, sl]
	add	sl, sl, #2
	mov	r0, r0, asl #2
	bl	ftl_malloc
	str	r0, [r4, #-912]
	ldrh	r0, [r5, sl]
	mov	r0, r0, asl #2
	bl	ftl_malloc
	ldrh	r2, [r5, sl]
	mov	r1, #0
	mov	r2, r2, asl #2
	str	r0, [r4, #-908]
	bl	ftl_memset
	movw	r3, #3944
	ldrh	sl, [r5, r3]
	mov	sl, sl, asl #2
	mov	r0, sl
	bl	ftl_malloc
	str	r0, [r4, #-1028]
	mov	r0, sl
	bl	ftl_malloc
	movw	sl, #3946
	str	r0, [r4, #-904]
	ldr	r0, [r5, #3936]
	mov	r0, r0, asl #2
	bl	ftl_malloc
	str	r0, [r4, #-1032]
	ldrh	r0, [r5, sl]
	mul	r0, r7, r0
	bl	ftl_malloc
	ldrh	r3, [r5, sl]
	movw	r7, #3862
	str	r0, [r4, #-1900]
	ldrh	r0, [r5, r8]
	mul	r0, r0, r3
	bl	ftl_malloc
	ldrh	r3, [r5, r6]
	ldr	r6, .L802+24
	str	r0, [r4, #-1088]
	mov	r0, #6
	mul	r0, r0, r3
	bl	ftl_malloc
	mov	r3, #3904
	ldrh	r3, [r5, r3]
	ldrh	r2, [r5, r7]
	add	r3, r3, #31
	mov	r3, r3, lsr #5
	strh	r3, [r4, r6]	@ movhi
	mul	r3, r2, r3
	str	r0, [r4, #-2072]
	mov	r0, r3, asl #2
	bl	ftl_malloc
	ldrh	r1, [r4, r6]
	mov	r3, #1
	ldr	ip, .L802+12
	mov	r1, r1, asl #2
	mov	r2, r1
	str	r0, [r5, #4004]
	ldrh	r5, [r5, r7]
	ldr	r0, .L802+28
	b	.L773
.L774:
	ldr	r4, [ip, #4004]
	add	r3, r3, #1
	add	r4, r4, r2
	add	r2, r2, r1
	str	r4, [r0, #4]!
.L773:
	cmp	r3, r5
	bcc	.L774
	ldr	r0, .L802+32
	mov	r2, #0
	mov	r1, r2
	add	r0, r0, r3, asl #2
	b	.L775
.L776:
	add	ip, r0, r2
	add	r3, r3, #1
	add	r2, r2, #4
	str	r1, [ip, #28]
.L775:
	cmp	r3, #7
	bls	.L776
	ldr	r3, .L802
	ldr	r2, [r3, #-1036]
	cmp	r2, #0
	beq	.L801
.L777:
	ldr	r2, [r3, #-1092]
	cmp	r2, #0
	beq	.L801
.L779:
	ldr	r2, [r3, #-1028]
	cmp	r2, #0
	beq	.L801
.L780:
	ldr	r2, [r3, #-1032]
	cmp	r2, #0
	beq	.L801
.L781:
	ldr	r2, [r3, #-1900]
	cmp	r2, #0
	beq	.L801
.L782:
	ldr	r2, [r3, #-1088]
	cmp	r2, #0
	beq	.L801
.L783:
	ldr	r2, [r3, #-2072]
	cmp	r2, #0
	beq	.L801
.L784:
	ldr	r2, .L802+12
	ldr	r2, [r2, #4004]
	cmp	r2, #0
	beq	.L801
.L785:
	ldr	r3, [r3, #-2064]
	cmp	r3, #0
	beq	.L801
.L786:
	ldr	r3, .L802
	ldr	r2, [r3, #-1148]
	cmp	r2, #0
	beq	.L801
	ldr	r2, [r3, #-1144]
	cmp	r2, #0
	beq	.L801
.L788:
	ldr	r2, [r3, #-972]
	cmp	r2, #0
	beq	.L801
	ldr	r2, [r3, #-964]
	cmp	r2, #0
	beq	.L801
	ldr	r2, [r3, #-2088]
	cmp	r2, #0
	beq	.L801
	ldr	r2, [r3, #-1160]
	cmp	r2, #0
	beq	.L801
	ldr	r2, [r3, #-968]
	cmp	r2, #0
	beq	.L801
.L790:
	ldr	r2, [r3, #-2076]
	cmp	r2, #0
	beq	.L801
	ldr	r2, [r3, #-960]
	cmp	r2, #0
	beq	.L801
	ldr	r3, [r3, #-956]
	cmp	r3, #0
	beq	.L801
	ldr	r3, .L802
	ldr	r2, [r3, #-1168]
	cmp	r2, #0
	beq	.L801
	ldr	r2, [r3, #-948]
	cmp	r2, #0
	beq	.L801
	ldr	r2, [r3, #-944]
	cmp	r2, #0
	beq	.L801
	ldr	r2, [r3, #-1172]
	cmp	r2, #0
	beq	.L801
.L792:
	ldr	r2, [r3, #-940]
	cmp	r2, #0
	beq	.L801
	ldr	r2, [r3, #-936]
	cmp	r2, #0
	beq	.L801
	ldr	r2, [r3, #-1164]
	cmp	r2, #0
	beq	.L801
.L794:
	ldr	r2, [r3, #-2084]
	cmp	r2, #0
	beq	.L801
	ldr	r3, [r3, #-1000]
	cmp	r3, #0
	beq	.L801
.L796:
	ldr	r3, .L802+12
	ldr	r3, [r3, #3956]
	cmp	r3, #0
	beq	.L801
.L797:
	ldr	r3, .L802
	ldr	r2, [r3, #-916]
	cmp	r2, #0
	beq	.L801
.L798:
	ldr	r2, [r3, #-912]
	cmp	r2, #0
	beq	.L801
.L799:
	ldr	r3, [r3, #-908]
	cmp	r3, #0
	bne	.L800
.L801:
	ldr	r0, .L802+36
	ldr	r1, .L802+40
	bl	printk
	mvn	r0, #0
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, sl, pc}
.L800:
	mov	r0, #0
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, sl, pc}
.L803:
	.align	2
.L802:
	.word	.LANCHOR2
	.word	-1096
	.word	-1138
	.word	.LANCHOR0
	.word	-1192
	.word	-928
	.word	-1024
	.word	.LANCHOR0+4004
	.word	.LANCHOR0+3976
	.word	.LC80
	.word	.LANCHOR3
	.fnend
	.size	FtlMemInit, .-FtlMemInit
	.align	2
	.global	ReadFlashInfo
	.type	ReadFlashInfo, %function
ReadFlashInfo:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	stmfd	sp!, {r3, r4, r5, lr}
	.save {r3, r4, r5, lr}
	mov	r1, #0
	mov	r2, #11
	mov	r4, r0
	bl	ftl_memset
	ldr	r3, .L807
	ldr	r1, [r3, #3624]
	ldr	r0, [r3, #856]
	ldrb	r2, [r1, #9]	@ zero_extendqisi2
	mul	r2, r0, r2
	mov	r0, #0
	uxth	r2, r2
	strb	r2, [r4, #4]
	mov	ip, r2, lsr #8
	strb	ip, [r4, #5]
	ldrb	ip, [r3, #3832]	@ zero_extendqisi2
	strb	ip, [r4, #7]
	ldrb	ip, [r1, #13]	@ zero_extendqisi2
	mul	r2, r2, ip
	ldrh	ip, [r1, #14]
	ldrb	r1, [r1, #8]	@ zero_extendqisi2
	mul	r2, ip, r2
	mul	r2, r1, r2
	ubfx	r1, r2, #8, #8
	strb	r1, [r4, #1]
	ubfx	r1, r2, #16, #8
	strb	r1, [r4, #2]
	strb	r2, [r4, #0]
	mov	r2, r2, lsr #24
	strb	r2, [r4, #3]
	ldr	r2, [r3, #3624]
	ldrb	ip, [r3, #3762]	@ zero_extendqisi2
	mov	r3, r0
	ldrb	r1, [r2, #9]	@ zero_extendqisi2
	strb	r1, [r4, #6]
	mov	r1, #32
	strb	r1, [r4, #8]
	ldrb	r2, [r2, #7]	@ zero_extendqisi2
	ldr	r1, .L807+4
	strb	r0, [r4, #10]
	strb	r2, [r4, #9]
	mov	r2, #1
	b	.L805
.L806:
	ldrb	r5, [r3, r1]	@ zero_extendqisi2
	add	r3, r3, #1
	ldrb	r0, [r4, #10]	@ zero_extendqisi2
	orr	r0, r0, r2, asl r5
	strb	r0, [r4, #10]
.L805:
	uxtb	r0, r3
	cmp	r0, ip
	bcc	.L806
	ldmfd	sp!, {r3, r4, r5, pc}
.L808:
	.align	2
.L807:
	.word	.LANCHOR0
	.word	.LANCHOR0+3764
	.fnend
	.size	ReadFlashInfo, .-ReadFlashInfo
	.align	2
	.global	FlashDieInfoInit
	.type	FlashDieInfoInit, %function
FlashDieInfoInit:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	ldr	r1, .L821
	mov	r2, #0
	stmfd	sp!, {r3, r4, r5, r6, r7, r8, sl, lr}
	.save {r3, r4, r5, r6, r7, r8, sl, lr}
	ldr	r3, .L821+4
	strb	r2, [r1, #-900]
	ldr	r1, .L821+8
	strb	r2, [r3, #3762]
	movw	r2, #2838
	ldrh	r2, [r1, r2]
	cmp	r2, #256
	str	r2, [r3, #856]
	movhi	r2, #512
	bhi	.L820
	cmp	r2, #128
	bls	.L811
	mov	r2, #256
.L820:
	str	r2, [r3, #856]
.L811:
	mov	r1, #0
	mov	r2, #8
	ldr	r0, .L821+12
	mov	r6, #0
	bl	ftl_memset
	mov	r1, #0
	mov	r2, #32
	ldr	r0, .L821+16
	bl	ftl_memset
	ldr	r0, .L821+20
	mov	r1, #0
	mov	r2, #128
	bl	ftl_memset
	ldr	r7, .L821+24
	ldr	r5, .L821+4
	mov	r8, r7
.L813:
	ldr	r4, [r5, #3624]
	mov	r1, r8
	add	r0, r4, #1
	ldrb	r2, [r4, #0]	@ zero_extendqisi2
	bl	FlashMemCmp8
	cmp	r0, #0
	bne	.L812
	ldrb	r3, [r5, #3762]	@ zero_extendqisi2
	add	r2, r5, r3, asl #2
	str	r0, [r2, #3588]
	add	r2, r5, r3
	add	r3, r3, #1
	strb	r3, [r5, #3762]
	strb	r6, [r2, #3764]
.L812:
	add	r6, r6, #1
	add	r8, r8, #8
	cmp	r6, #4
	bne	.L813
	ldr	r5, .L821+4
	ldr	r3, .L821
	ldrb	r2, [r5, #3762]	@ zero_extendqisi2
	strb	r2, [r3, #-900]
	ldrb	r3, [r4, #8]	@ zero_extendqisi2
	cmp	r3, #2
	bne	.L814
	add	sl, r4, #1
	mov	r6, #0
	mov	r8, r5
.L817:
	mov	r0, sl
	mov	r1, r7
	ldrb	r2, [r4, #0]	@ zero_extendqisi2
	bl	FlashMemCmp8
	cmp	r0, #0
	bne	.L815
	ldrb	r1, [r4, #13]	@ zero_extendqisi2
	ldr	r0, [r5, #856]
	ldrb	r3, [r5, #3762]	@ zero_extendqisi2
	mul	r0, r0, r1
	ldrh	r1, [r4, #14]
	add	r2, r5, r3, asl #2
	and	r1, r1, #65280
	mul	r1, r1, r0
	str	r1, [r2, #3588]
	ldrb	r0, [r4, #23]	@ zero_extendqisi2
	cmp	r0, #0
	movne	r1, r1, asl #1
	strne	r1, [r2, #3588]
	add	r2, r8, r3
	add	r3, r3, #1
	strb	r3, [r8, #3762]
	strb	r6, [r2, #3764]
.L815:
	add	r6, r6, #1
	add	r7, r7, #8
	cmp	r6, #4
	bne	.L817
.L814:
	ldr	r3, .L821+4
	ldrb	r1, [r4, #13]	@ zero_extendqisi2
	ldr	r2, .L821
	ldrb	r3, [r3, #3762]	@ zero_extendqisi2
	mul	r1, r1, r3
	ldrh	r3, [r4, #14]
	mul	r1, r3, r1
	ldr	r3, .L821+28
	strh	r1, [r2, r3]	@ movhi
	ldmfd	sp!, {r3, r4, r5, r6, r7, r8, sl, pc}
.L822:
	.align	2
.L821:
	.word	.LANCHOR2
	.word	.LANCHOR0
	.word	.LANCHOR1
	.word	.LANCHOR0+3764
	.word	.LANCHOR0+3588
	.word	.LANCHOR0+3628
	.word	.LANCHOR0+2980
	.word	-898
	.fnend
	.size	FlashDieInfoInit, .-FlashDieInfoInit
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
	bne	.L824
	ldr	r3, .L863
.L825:
	strh	r0, [r3, #2]!	@ movhi
	add	r0, r0, #1
	uxth	r0, r0
	cmp	r0, #256
	bne	.L825
	b	.L826
.L824:
	cmp	r0, #1
	bne	.L827
	ldr	r0, .L863+4
	mov	r2, #0
	mov	r3, r2
.L830:
	uxth	r1, r3
	cmp	r1, #3
	bls	.L828
	tst	r3, #1
	moveq	r1, #2
	movne	r1, #3
	rsb	r1, r1, r2
	uxth	r1, r1
.L828:
	add	r3, r3, #1
	strh	r1, [r2, r0]	@ movhi
	cmp	r3, #256
	add	r2, r2, #2
	bne	.L830
	b	.L826
.L827:
	cmp	r0, #2
	bne	.L831
	mov	r3, #0
	ldr	r0, .L863
	movw	r2, #65535
	mov	r1, r3
	b	.L862
.L834:
	cmp	r3, #1
	movls	r1, r3
	movhi	r1, r2
.L862:
	add	r3, r3, #1
	add	r2, r2, #2
	strh	r1, [r0, #2]!	@ movhi
	uxth	r3, r3
	uxth	r2, r2
	cmp	r3, #256
	bne	.L834
	b	.L826
.L831:
	cmp	r0, #3
	bne	.L835
	ldr	r0, .L863+4
	mov	r2, #0
	mov	r3, r2
.L838:
	uxth	r1, r3
	cmp	r1, #5
	bls	.L836
	tst	r3, #1
	moveq	r1, #4
	movne	r1, #5
	rsb	r1, r1, r2
	uxth	r1, r1
.L836:
	add	r3, r3, #1
	strh	r1, [r2, r0]	@ movhi
	cmp	r3, #256
	add	r2, r2, #2
	bne	.L838
	b	.L826
.L835:
	cmp	r0, #4
	mov	r2, #0
	bne	.L839
	ldr	r3, .L863+8
	movw	r1, #3076
	strh	r2, [r3, r1]	@ movhi
	movw	r2, #3078
	mov	r1, #1
	strh	r1, [r3, r2]	@ movhi
	mov	r1, #2
	add	r2, r2, r1
	strh	r1, [r3, r2]	@ movhi
	add	r2, r2, #2
	mov	r1, #3
	strh	r1, [r3, r2]	@ movhi
	add	r2, r2, #2
	mov	r1, #5
	strh	r0, [r3, r2]	@ movhi
	add	r2, r2, #2
	ldr	r0, .L863+12
	strh	r1, [r3, r2]	@ movhi
	mov	r2, #3088
	mov	r1, #7
	strh	r1, [r3, r2]	@ movhi
	add	r2, r2, #2
	mov	r1, #8
	strh	r1, [r3, r2]	@ movhi
	mov	r2, #16
	mov	r3, r1
.L841:
	tst	r3, #1
	add	r3, r3, #1
	moveq	r1, #6
	movne	r1, #7
	cmp	r3, #256
	rsb	r1, r1, r2
	add	r2, r2, #2
	strh	r1, [r0, #2]!	@ movhi
	uxth	r2, r2
	bne	.L841
	b	.L826
.L839:
	cmp	r0, #5
	bne	.L842
	ldr	r1, .L863
	mov	r3, r2
.L843:
	strh	r3, [r1, #2]!	@ movhi
	add	r3, r3, #1
	uxth	r3, r3
	cmp	r3, #16
	bne	.L843
	ldr	r2, .L863+16
.L844:
	strh	r3, [r2, #2]!	@ movhi
	add	r3, r3, #2
	uxth	r3, r3
	cmp	r3, #496
	bne	.L844
	b	.L826
.L842:
	cmp	r0, #6
	bne	.L826
	ldr	r0, .L863
	mov	r3, r2
.L847:
	uxth	r1, r3
	cmp	r1, #5
	bls	.L845
	tst	r3, #1
	moveq	r1, #10
	movne	r1, #12
	rsb	r1, r1, r2
	uxth	r1, r1
.L845:
	add	r3, r3, #1
	add	r2, r2, #3
	cmp	r3, #256
	strh	r1, [r0, #2]!	@ movhi
	uxth	r2, r2
	bne	.L847
.L826:
	mov	r2, #1024
	ldr	r0, .L863+20
	mov	r1, #255
	uxth	r4, r4
	bl	ftl_memset
	ldr	r2, .L863
	mov	r3, #0
	ldr	r0, .L863+24
	b	.L848
.L849:
	ldrh	r1, [r2, #2]!
	add	r3, r3, #1
	uxth	r3, r3
	add	ip, r0, r1, asl #1
	sub	ip, ip, #896
	strh	r1, [ip, #0]	@ movhi
.L848:
	cmp	r3, r4
	bcc	.L849
	ldmfd	sp!, {r4, pc}
.L864:
	.align	2
.L863:
	.word	.LANCHOR0+3074
	.word	.LANCHOR0+3076
	.word	.LANCHOR0
	.word	.LANCHOR0+3090
	.word	.LANCHOR0+3106
	.word	.LANCHOR2-896
	.word	.LANCHOR2
	.fnend
	.size	BuildFlashLsbPageTable, .-BuildFlashLsbPageTable
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
	.global	FlashSramLoadStore
	.type	FlashSramLoadStore, %function
FlashSramLoadStore:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	stmfd	sp!, {r4, lr}
	.save {r4, lr}
	cmp	r2, #0
	ldr	r4, .L870
	mov	ip, r0
	mov	r2, r3
	ldr	r4, [r4, #128]
	add	r4, r4, #4096
	add	r1, r4, r1
	movne	r0, r1
	movne	r1, ip
	ldmfd	sp!, {r4, lr}
	b	memcpy
.L871:
	.align	2
.L870:
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
	.global	rk_nand_de_init
	.type	rk_nand_de_init, %function
rk_nand_de_init:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	@ link register save eliminated.
	b	FlashDeInit
	.fnend
	.size	rk_nand_de_init, .-rk_nand_de_init
	.align	2
	.global	rk_nand_suspend
	.type	rk_nand_suspend, %function
rk_nand_suspend:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	@ link register save eliminated.
	b	FlashSuspend
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
	b	FlashResume
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
	ldr	r3, .L877
	ldr	r0, [r3, #3948]
	bx	lr
.L878:
	.align	2
.L877:
	.word	.LANCHOR0
	.fnend
	.size	rk_ftl_get_capacity, .-rk_ftl_get_capacity
	.align	2
	.global	rknand_print_hex
	.type	rknand_print_hex, %function
rknand_print_hex:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	stmfd	sp!, {r3, r4, r5, r6, r7, r8, sl, lr}
	.save {r3, r4, r5, r6, r7, r8, sl, lr}
	mov	r8, #0
	mov	r6, r0
	mov	r4, r1
	mov	r5, r2
	mov	sl, r3
	mov	r7, r8
	b	.L880
.L886:
	cmp	r8, #0
	bne	.L881
	ldr	r0, .L888
	mov	r1, r6
	mov	r2, r7
	bl	printk
.L881:
	cmp	r5, #4
	ldreq	r0, .L888+4
	ldreq	r1, [r4, r7, asl #2]
	beq	.L887
	cmp	r5, #2
	moveq	r3, r7, asl #1
	ldreq	r0, .L888+4
	ldreqsh	r1, [r4, r3]
	ldrne	r0, .L888+4
	ldrneb	r1, [r4, r7]	@ zero_extendqisi2
.L887:
	add	r8, r8, #1
	bl	printk
	cmp	r8, #15
	bls	.L885
	ldr	r0, .L888+8
	mov	r8, #0
	ldr	r1, .L888+12
	bl	printk
.L885:
	add	r7, r7, #1
.L880:
	cmp	r7, sl
	bne	.L886
	ldr	r0, .L888+8
	ldr	r1, .L888+12
	ldmfd	sp!, {r3, r4, r5, r6, r7, r8, sl, lr}
	b	printk
.L889:
	.align	2
.L888:
	.word	.LC81
	.word	.LC82
	.word	.LC75
	.word	.LC83
	.fnend
	.size	rknand_print_hex, .-rknand_print_hex
	.align	2
	.global	NandcXferComp
	.type	NandcXferComp, %function
NandcXferComp:
	.fnstart
	@ args = 0, pretend = 0, frame = 8
	@ frame_needed = 0, uses_anonymous_args = 0
	stmfd	sp!, {r0, r1, r4, r5, r6, lr}
	.save {r0, r1, r4, r5, r6, lr}
	ldr	r6, .L913
	add	r0, r6, r0, asl #3
	ldr	r3, [r6, #3780]
	ldr	r4, [r0, #864]
	cmp	r3, #3
	bls	.L907
	ldr	r3, [r4, #16]
	tst	r3, #4
	beq	.L907
	mov	r0, r4
	bl	wait_for_nandc_xfer_completed
	ldr	r5, [r4, #16]
	ldr	r3, [r4, #8]
	ands	r5, r5, #2
	str	r3, [sp, #0]
	movne	r5, #0
	bne	.L911
	b	.L912
.L897:
	ldr	r3, [r6, #3780]
	cmp	r3, #5
	bls	.L894
	ldr	r3, [r4, #0]
	str	r3, [sp, #4]
	ldr	r3, [sp, #4]
	tst	r3, #8192
	beq	.L894
	ldr	r3, [sp, #4]
	tst	r3, #131072
	bne	.L895
.L894:
	add	r5, r5, #1
	bic	r3, r5, #-16777216
	cmp	r3, #0
	bne	.L911
	ldr	r2, [r4, #28]
	mov	r1, r5
	ldr	r3, [sp, #0]
	ubfx	r2, r2, #16, #5
	ldr	r0, .L913+4
	ubfx	r3, r3, #22, #6
	bl	printk
	ldr	r0, .L913+8
	mov	r1, r4
	mov	r2, #4
	mov	r3, #512
	bl	rknand_print_hex
.L911:
	ldr	r2, [r4, #28]
	ldr	r3, [sp, #0]
	ubfx	r2, r2, #16, #5
	ubfx	r3, r3, #22, #6
	cmp	r2, r3
	blt	.L897
.L895:
	ldr	r4, .L913
	ldr	r3, [r4, #3816]
	cmp	r3, #0
	beq	.L898
	ldr	r1, [sp, #0]
	mov	r2, #0
	ldr	r0, [r4, #3808]
	ubfx	r1, r1, #22, #5
	mov	r1, r1, asl #10
	bl	rknand_dma_unmap_single
	ldr	r1, [sp, #0]
	ldr	r0, [r4, #3812]
	mov	r2, #0
	ubfx	r1, r1, #22, #5
	mov	r1, r1, asl #7
	bl	rknand_dma_unmap_single
	b	.L898
.L901:
	ldr	r3, [r4, #8]
	add	r5, r5, #1
	str	r3, [sp, #0]
	bic	r3, r5, #-16777216
	cmp	r3, #0
	bne	.L912
	ldr	r2, [sp, #0]
	mov	r1, r5
	ldr	r3, [r4, #28]
	ldr	r0, .L913+12
	ubfx	r3, r3, #16, #5
	bl	printk
	ldr	r0, .L913+8
	mov	r1, r4
	mov	r2, #4
	mov	r3, #512
	bl	rknand_print_hex
.L912:
	ldr	r3, [sp, #0]
	tst	r3, #1048576
	beq	.L901
	ldr	r6, .L913
	ldr	r3, [r6, #3824]
	cmp	r3, #0
	beq	.L902
	mov	r0, r4
	bl	NandcSendDumpDataStart
.L902:
	ldr	r3, [r6, #3816]
	ldr	r5, .L913
	cmp	r3, #0
	beq	.L903
	ldr	r1, [sp, #0]
	mov	r2, #1
	ldr	r0, [r5, #3808]
	ubfx	r1, r1, #22, #5
	mov	r1, r1, asl #10
	bl	rknand_dma_unmap_single
	ldr	r1, [sp, #0]
	ldr	r0, [r5, #3812]
	mov	r2, #1
	ubfx	r1, r1, #22, #5
	mov	r1, r1, asl #7
	bl	rknand_dma_unmap_single
.L903:
	ldr	r3, .L913
	ldr	r3, [r3, #3824]
	cmp	r3, #0
	beq	.L898
	mov	r0, r4
	bl	NandcSendDumpDataDone
.L898:
	ldr	r3, .L913
	mov	r2, #0
	str	r2, [r3, #3816]
	b	.L890
.L907:
	ldr	r3, [r4, #8]
	str	r3, [sp, #0]
	ldr	r3, [sp, #0]
	tst	r3, #1048576
	beq	.L907
.L890:
	ldmfd	sp!, {r2, r3, r4, r5, r6, pc}
.L914:
	.align	2
.L913:
	.word	.LANCHOR0
	.word	.LC84
	.word	.LC85
	.word	.LC86
	.fnend
	.size	NandcXferComp, .-NandcXferComp
	.align	2
	.global	NandcXferData
	.type	NandcXferData, %function
NandcXferData:
	.fnstart
	@ args = 4, pretend = 0, frame = 80
	@ frame_needed = 0, uses_anonymous_args = 0
	stmfd	sp!, {r4, r5, r6, r7, r8, r9, sl, fp, lr}
	.save {r4, r5, r6, r7, r8, r9, sl, fp, lr}
	mov	r4, r3
	mov	r9, r3
	ldr	r3, .L953
	.pad #92
	sub	sp, sp, #92
	tst	r4, #63
	add	r3, r3, r0, asl #3
	mov	r7, r0
	mov	fp, r1
	mov	sl, r2
	ldr	r5, [sp, #128]
	ldr	r6, [r3, #864]
	bne	.L916
	cmp	r5, #0
	bne	.L917
	add	r0, sp, #24
	mov	r1, #255
	mov	r2, #64
	add	r5, sp, #24
	bl	ftl_memset
.L917:
	mov	r0, r7
	mov	r1, fp
	mov	r2, sl
	mov	r3, #0
	stmia	sp, {r4, r5}
	bl	NandcXferStart
	mov	r0, r7
	bl	NandcXferComp
	cmp	fp, #0
	movne	r4, #0
	bne	.L918
	ldr	r1, .L953
	mov	r0, sl, lsr #1
	mov	r3, fp
	mov	r2, fp
	ldr	ip, [r1, #3828]
	mov	r4, r1
	cmp	ip, #24
	movhi	ip, #128
	movls	ip, #64
	b	.L920
.L921:
	ldr	r1, [r4, #3796]
	mov	r3, r3, lsr #2
	add	r2, r2, #1
	ldr	r3, [r1, r3, asl #2]
	mov	r1, r3, lsr #8
	strb	r3, [r5, #0]
	strb	r1, [r5, #1]
	mov	r1, r3, lsr #16
	mov	r3, r3, lsr #24
	strb	r1, [r5, #2]
	strb	r3, [r5, #3]
	add	r5, r5, #4
	mov	r3, r7
.L920:
	cmp	r2, r0
	add	r7, r3, ip
	ldr	r1, .L953
	bcc	.L921
	mov	r3, #0
	ldr	r0, [r1, #3828]
	mov	sl, sl, lsr #2
	ldr	r1, [r1, #3780]
	mov	r4, r3
	b	.L922
.L928:
	add	r2, r3, #8
	ldr	r2, [r6, r2, asl #2]
	str	r2, [sp, #20]
	ldr	r2, [sp, #20]
	tst	r2, #4
	bne	.L943
	ldr	r2, [sp, #20]
	ands	r2, r2, #32768
	bne	.L943
	cmp	r1, #5
	bls	.L924
	ldr	r7, [sp, #20]
	ldr	ip, [sp, #20]
	ldr	r5, [sp, #20]
	ubfx	r7, r7, #3, #5
	ldr	r2, [sp, #20]
	ubfx	ip, ip, #27, #1
	ubfx	r5, r5, #16, #5
	ubfx	r2, r2, #29, #1
	orr	ip, r7, ip, asl #5
	orr	r2, r5, r2, asl #5
	cmp	ip, r2
	ldr	ip, [sp, #20]
	ldrhi	r2, [sp, #20]
	ubfxhi	ip, ip, #3, #5
	ldrls	r2, [sp, #20]
	ubfxls	ip, ip, #16, #5
	ubfxhi	r2, r2, #27, #1
	ubfxls	r2, r2, #29, #1
	b	.L951
.L924:
	cmp	r1, #3
	bls	.L926
	ldr	r7, [sp, #20]
	ldr	ip, [sp, #20]
	ldr	r5, [sp, #20]
	ubfx	r7, r7, #3, #5
	ldr	r2, [sp, #20]
	ubfx	ip, ip, #28, #1
	ubfx	r5, r5, #16, #5
	ubfx	r2, r2, #30, #1
	orr	ip, r7, ip, asl #5
	orr	r2, r5, r2, asl #5
	cmp	ip, r2
	ldr	ip, [sp, #20]
	ldrhi	r2, [sp, #20]
	ubfxhi	ip, ip, #3, #5
	ldrls	r2, [sp, #20]
	ubfxls	ip, ip, #16, #5
	ubfxhi	r2, r2, #28, #1
	ubfxls	r2, r2, #30, #1
.L951:
	orr	r2, ip, r2, asl #5
.L926:
	cmp	r4, r2
	movcc	r4, r2
	b	.L923
.L943:
	mvn	r4, #0
.L923:
	add	r3, r3, #1
.L922:
	cmp	r3, sl
	bcs	.L918
	cmp	r0, #0
	bne	.L928
.L918:
	mov	r3, #0
	str	r3, [r6, #16]
	b	.L929
.L916:
	cmp	r1, #1
	mov	r8, #0
	bne	.L949
	b	.L930
.L933:
	cmp	r5, #0
	and	r4, r8, #3
	mov	r0, r6
	mov	r1, #1
	movne	r3, #2
	moveq	r3, #0
	mla	r3, r8, r3, r5
	mov	r2, r4
	add	r8, r8, #2
	str	r3, [sp, #0]
	mov	r3, r9
	bl	NandcCopy1KB
	mov	r3, #0
	mov	r0, r7
	str	r3, [sp, #0]
	str	r3, [sp, #4]
	mov	r1, #1
	mov	r2, #2
	mov	r3, r4
	bl	NandcXferStart
	mov	r0, r7
	bl	NandcXferComp
	add	r9, r9, #1024
.L930:
	cmp	r8, sl
	bcc	.L933
	mov	r4, #0
	b	.L929
.L949:
	mov	r1, r8
	mov	r2, #2
	mov	r3, r8
	str	r8, [sp, #0]
	str	r8, [sp, #4]
	mov	r4, r8
	bl	NandcXferStart
	str	fp, [sp, #12]
	mov	ip, r8
	mov	fp, r6
	mov	r6, r5
	b	.L952
.L938:
	mov	r0, r7
	bl	NandcXferComp
	ldr	r3, [fp, #32]
	add	ip, r5, #2
	cmp	ip, sl
	str	r3, [sp, #20]
	bcs	.L935
	mov	r3, #0
	mov	r0, r7
	str	r3, [sp, #0]
	mov	r1, r3
	str	r3, [sp, #4]
	mov	r2, #2
	and	r3, ip, #3
	str	ip, [sp, #8]
	bl	NandcXferStart
	ldr	ip, [sp, #8]
.L935:
	ldr	r3, [sp, #20]
	tst	r3, #4
	mvnne	r4, #0
	bne	.L936
	ldr	r2, [sp, #20]
	ldr	r3, [sp, #20]
	ubfx	r2, r2, #3, #5
	ubfx	r3, r3, #27, #1
	orr	r3, r2, r3, asl #5
	cmp	r4, r3
	movcc	r4, r3
.L936:
	cmp	r6, #0
	and	r2, r8, #3
	mov	r0, fp
	mov	r1, #0
	movne	r3, #2
	moveq	r3, #0
	mla	r5, r5, r3, r6
	mov	r3, r9
	str	ip, [sp, #8]
	add	r8, r8, #2
	add	r9, r9, #1024
	str	r5, [sp, #0]
	bl	NandcCopy1KB
	ldr	ip, [sp, #8]
.L952:
	cmp	ip, sl
	mov	r5, ip
	bcc	.L938
	mov	r6, fp
	ldr	fp, [sp, #12]
.L929:
	ldr	r3, .L953
	rsbs	fp, fp, #1
	movcc	fp, #0
	ldr	r3, [r3, #3780]
	cmp	r3, #5
	movls	fp, #0
	cmp	fp, #0
	beq	.L939
	ldr	r3, [r6, #0]
	and	r2, r3, #139264
	cmp	r2, #139264
	orreq	r3, r3, #131072
	streq	r3, [r6, #0]
	mvneq	r4, #0
.L939:
	mov	r0, r4
	add	sp, sp, #92
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, sl, fp, pc}
.L954:
	.align	2
.L953:
	.word	.LANCHOR0
	.fnend
	.size	NandcXferData, .-NandcXferData
	.align	2
	.global	FlashProgPage
	.type	FlashProgPage, %function
FlashProgPage:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	stmfd	sp!, {r0, r1, r4, r5, r6, r7, r8, lr}
	.save {r0, r1, r4, r5, r6, r7, r8, lr}
	mov	r8, r3
	ldr	r3, .L958
	subs	r4, r0, #0
	mov	r5, r1
	mov	r6, r2
	ldrb	r7, [r3, #2837]	@ zero_extendqisi2
	bne	.L956
	ldr	r3, .L958+4
	ldrb	r2, [r3, #853]	@ zero_extendqisi2
	ldr	r1, [r3, #856]
	mul	r2, r1, r2
	cmp	r5, r2
	bcs	.L956
	ldrb	r3, [r3, #852]	@ zero_extendqisi2
	cmp	r3, #0
	subeq	r7, r7, #2
	movne	r7, #4
.L956:
	mov	r0, r4
	bl	NandcWaitFlashReady
	mov	r0, r4
	bl	NandcFlashCs
	mov	r0, r4
	mov	r1, r5
	bl	FlashProgFirstCmd
	uxtb	r2, r7
	mov	r3, r6
	mov	r0, r4
	mov	r1, #1
	str	r8, [sp, #0]
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
	ldmfd	sp!, {r2, r3, r4, r5, r6, r7, r8, pc}
.L959:
	.align	2
.L958:
	.word	.LANCHOR1
	.word	.LANCHOR0
	.fnend
	.size	FlashProgPage, .-FlashProgPage
	.align	2
	.global	FlashPageProgMsbFFData
	.type	FlashPageProgMsbFFData, %function
FlashPageProgMsbFFData:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	stmfd	sp!, {r3, r4, r5, r6, r7, r8, sl, lr}
	.save {r3, r4, r5, r6, r7, r8, sl, lr}
	mov	r4, r2
	ldr	r2, .L967
	mov	r7, r1
	mov	r8, r0
	ldrb	r1, [r2, #928]	@ zero_extendqisi2
	ldr	r3, [r2, #3624]
	cmp	r1, #0
	ldrb	r3, [r3, #19]	@ zero_extendqisi2
	beq	.L961
	ldr	r1, [r2, #3784]
	ldr	r2, .L967+4
	cmp	r1, r2
	ldmeqfd	sp!, {r3, r4, r5, r6, r7, r8, sl, pc}
.L961:
	sub	r2, r3, #5
	uxtb	r2, r2
	cmp	r3, #68
	cmpne	r2, #2
	bls	.L964
	cmp	r3, #35
	cmpne	r3, #19
	beq	.L964
	ldmfd	sp!, {r3, r4, r5, r6, r7, r8, sl, pc}
.L965:
	add	r3, r5, r4, asl #1
	sub	r3, r3, #896
	ldrh	r3, [r3, #0]
	cmp	r3, sl
	ldmnefd	sp!, {r3, r4, r5, r6, r7, r8, sl, pc}
	mov	r1, #255
	mov	r2, #32768
	ldr	r0, [r5, #132]
	bl	ftl_memset
	add	r1, r4, r7
	add	r4, r4, #1
	mov	r0, r8
	ldr	r2, [r5, #132]
	mov	r3, #0
	bl	FlashProgPage
	uxth	r4, r4
	b	.L966
.L964:
	ldr	r6, .L967
	movw	sl, #65535
	ldr	r5, .L967+8
.L966:
	ldr	r3, [r6, #3624]
	ldrh	r3, [r3, #10]
	cmp	r3, r4
	bhi	.L965
	ldmfd	sp!, {r3, r4, r5, r6, r7, r8, sl, pc}
.L968:
	.align	2
.L967:
	.word	.LANCHOR0
	.word	1446522928
	.word	.LANCHOR2
	.fnend
	.size	FlashPageProgMsbFFData, .-FlashPageProgMsbFFData
	.align	2
	.global	FlashReadRawPage
	.type	FlashReadRawPage, %function
FlashReadRawPage:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	stmfd	sp!, {r0, r1, r4, r5, r6, r7, r8, lr}
	.save {r0, r1, r4, r5, r6, r7, r8, lr}
	mov	r8, r3
	ldr	r3, .L972
	subs	r4, r0, #0
	mov	r6, r1
	mov	r5, r2
	ldrb	r7, [r3, #2837]	@ zero_extendqisi2
	bne	.L970
	ldr	r3, .L972+4
	ldrb	r2, [r3, #853]	@ zero_extendqisi2
	ldr	r3, [r3, #856]
	mul	r3, r3, r2
	cmp	r1, r3
	movcc	r7, #4
.L970:
	mov	r0, r4
	bl	NandcWaitFlashReady
	mov	r0, r4
	bl	NandcFlashCs
	mov	r1, r6
	mov	r0, r4
	bl	FlashReadCmd
	mov	r0, r4
	bl	NandcWaitFlashReady
	mov	r3, r5
	mov	r1, #0
	mov	r2, r7
	mov	r0, r4
	str	r8, [sp, #0]
	bl	NandcXferData
	mov	r5, r0
	mov	r0, r4
	bl	NandcFlashDeCs
	mov	r0, r5
	ldmfd	sp!, {r2, r3, r4, r5, r6, r7, r8, pc}
.L973:
	.align	2
.L972:
	.word	.LANCHOR1
	.word	.LANCHOR0
	.fnend
	.size	FlashReadRawPage, .-FlashReadRawPage
	.align	2
	.global	HynixReadRetrial
	.type	HynixReadRetrial, %function
HynixReadRetrial:
	.fnstart
	@ args = 0, pretend = 0, frame = 8
	@ frame_needed = 0, uses_anonymous_args = 0
	stmfd	sp!, {r0, r1, r2, r4, r5, r6, r7, r8, r9, sl, fp, lr}
	.save {r0, r1, r2, r4, r5, r6, r7, r8, r9, sl, fp, lr}
	mov	r7, r3
	ldr	r3, .L988
	mov	sl, r2
	ldr	fp, .L988
	mov	r6, r0
	add	r2, r3, r0
	str	r1, [sp, #4]
	ldrb	r9, [r3, #2]	@ zero_extendqisi2
	mov	r8, #0
	ldr	r3, [r3, #3624]
	mvn	r5, #0
	ldrb	r4, [r2, #12]	@ zero_extendqisi2
	ldrb	r3, [r3, #19]	@ zero_extendqisi2
	cmp	r3, #7
	ldreqb	r4, [r2, #20]	@ zero_extendqisi2
	bl	NandcWaitFlashReady
	mov	ip, fp
	b	.L976
.L981:
	add	r4, r4, #1
	ldrb	r1, [fp, #1]	@ zero_extendqisi2
	mov	r0, r6
	ldr	r2, .L988+4
	uxtb	r4, r4
	str	ip, [sp, #0]
	cmp	r4, r9
	movcs	r4, #0
	mov	r3, r4
	bl	HynixSetRRPara
	mov	r0, r6
	ldr	r1, [sp, #4]
	mov	r2, sl
	mov	r3, r7
	bl	FlashReadRawPage
	ldr	ip, [sp, #0]
	cmn	r0, #1
	beq	.L978
	ldrb	r3, [ip, #3832]	@ zero_extendqisi2
	cmn	r5, #1
	moveq	r5, r0
	add	r3, r3, r3, asl #1
	cmp	r0, r3, lsr #2
	bcc	.L987
	mov	r7, #0
	mov	sl, r7
.L978:
	add	r8, r8, #1
.L976:
	cmp	r8, r9
	bcc	.L981
	b	.L980
.L987:
	mov	r5, r0
.L980:
	ldr	r3, .L988
	add	r6, r3, r6
	ldr	r2, [r3, #3624]
	ldrb	r3, [r3, #3832]	@ zero_extendqisi2
	ldrb	r2, [r2, #19]	@ zero_extendqisi2
	add	r3, r3, r3, asl #1
	cmp	r2, #7
	streqb	r4, [r6, #20]
	strneb	r4, [r6, #12]
	adds	r0, r5, #1
	movne	r0, #1
	cmp	r5, r3, lsr #2
	movcc	r0, #0
	andcs	r0, r0, #1
	cmp	r0, #0
	moveq	r0, r5
	movne	r0, #256
	ldmfd	sp!, {r1, r2, r3, r4, r5, r6, r7, r8, r9, sl, fp, pc}
.L989:
	.align	2
.L988:
	.word	.LANCHOR0
	.word	.LANCHOR0+4
	.fnend
	.size	HynixReadRetrial, .-HynixReadRetrial
	.align	2
	.global	MicronReadRetrial
	.type	MicronReadRetrial, %function
MicronReadRetrial:
	.fnstart
	@ args = 0, pretend = 0, frame = 16
	@ frame_needed = 0, uses_anonymous_args = 0
	stmfd	sp!, {r4, r5, r6, r7, r8, r9, sl, fp, lr}
	.save {r4, r5, r6, r7, r8, r9, sl, fp, lr}
	mov	r9, r0
	ldr	r4, .L1003
	mov	sl, r3
	.pad #28
	sub	sp, sp, #28
	ldrb	r0, [r4, #3832]	@ zero_extendqisi2
	ldrb	r3, [r4, #928]	@ zero_extendqisi2
	str	r1, [sp, #16]
	cmp	r3, #0
	str	r2, [sp, #20]
	addeq	r0, r0, r0, asl #1
	ubfxeq	r0, r0, #2, #8
	beq	.L1002
	mov	r1, #3
	bl	__aeabi_idiv
	uxtb	r0, r0
.L1002:
	add	r4, r4, r9, asl #3
	str	r0, [sp, #12]
	mov	r0, r9
	mov	r8, #0
	bl	NandcWaitFlashReady
	ldrb	r7, [r4, #868]	@ zero_extendqisi2
	ldr	r6, [r4, #864]
	mov	fp, r8
	add	r7, r7, #8
	mvn	r4, #0
	add	r5, r6, r7, asl #8
	b	.L993
.L997:
	mov	r3, #239
	mov	r0, #200
	str	r3, [r5, #8]
	mov	r3, #137
	str	r3, [r5, #4]
	bl	NandcDelayns
	add	ip, r8, #1
	mov	r0, r9
	str	ip, [r5, #0]
	ldr	r1, [sp, #16]
	mov	r3, sl
	str	fp, [r5, #0]
	ldr	r2, [sp, #20]
	str	fp, [r5, #0]
	str	fp, [r5, #0]
	str	ip, [sp, #8]
	bl	FlashReadRawPage
	ldr	ip, [sp, #8]
	cmn	r0, #1
	beq	.L994
	cmn	r4, #1
	ldr	r2, [sp, #12]
	moveq	r4, r0
	cmp	r0, r2
	bcc	.L1001
	mov	sl, #0
	str	sl, [sp, #20]
.L994:
	mov	r8, ip
.L993:
	ldr	r2, .L1003+4
	ldrb	r3, [r2, #136]	@ zero_extendqisi2
	cmp	r8, r3
	bcc	.L997
	b	.L996
.L1001:
	mov	r4, r0
.L996:
	mov	r3, #239
	mov	r0, #200
	str	r3, [r5, #8]
	mov	r3, #137
	str	r3, [r5, #4]
	bl	NandcDelayns
	ldr	r2, [sp, #12]
	mov	r3, #0
	str	r3, [r6, r7, asl #8]
	cmp	r4, r2
	str	r3, [r6, r7, asl #8]
	str	r3, [r6, r7, asl #8]
	str	r3, [r6, r7, asl #8]
	movcc	r3, #0
	movcs	r3, #1
	cmn	r4, #1
	moveq	r3, #0
	cmp	r3, #0
	movne	r4, #256
	cmn	r4, #1
	cmpne	r4, #256
	bne	.L999
	ldr	r0, .L1003+8
	mov	r1, r8
	ldr	r2, [sp, #16]
	mov	r3, r8
	str	r4, [sp, #0]
	bl	printk
.L999:
	mov	r0, r4
	add	sp, sp, #28
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, sl, fp, pc}
.L1004:
	.align	2
.L1003:
	.word	.LANCHOR0
	.word	.LANCHOR2
	.word	.LC87
	.fnend
	.size	MicronReadRetrial, .-MicronReadRetrial
	.align	2
	.global	SamsungReadRetrial
	.type	SamsungReadRetrial, %function
SamsungReadRetrial:
	.fnstart
	@ args = 0, pretend = 0, frame = 8
	@ frame_needed = 0, uses_anonymous_args = 0
	stmfd	sp!, {r0, r1, r2, r4, r5, r6, r7, r8, r9, sl, fp, lr}
	.save {r0, r1, r2, r4, r5, r6, r7, r8, r9, sl, fp, lr}
	mov	r8, r0
	mov	r7, r2
	mov	r6, r3
	mov	r9, r1
	bl	NandcWaitFlashReady
	ldr	ip, .L1014
	mov	r5, #1
	mvn	r4, #0
	ldr	fp, .L1014+4
	add	r3, ip, r8, asl #3
	ldrb	sl, [r3, #868]	@ zero_extendqisi2
	ldr	r2, [r3, #864]
	add	sl, sl, #8
	add	sl, r2, sl, asl #8
	b	.L1006
.L1010:
	mov	r0, sl
	uxtb	r1, r5
	str	ip, [sp, #4]
	bl	SamsungSetRRPara
	mov	r0, r8
	mov	r1, r9
	mov	r2, r7
	mov	r3, r6
	bl	FlashReadRawPage
	ldr	ip, [sp, #4]
	cmn	r0, #1
	beq	.L1007
	ldrb	r3, [ip, #3832]	@ zero_extendqisi2
	cmn	r4, #1
	moveq	r4, r0
	add	r3, r3, r3, asl #1
	cmp	r0, r3, lsr #2
	bcc	.L1013
	mov	r6, #0
	mov	r7, r6
.L1007:
	add	r5, r5, #1
.L1006:
	ldrb	r3, [fp, #136]	@ zero_extendqisi2
	add	r3, r3, #1
	cmp	r5, r3
	bcc	.L1010
	b	.L1009
.L1013:
	mov	r4, r0
.L1009:
	mov	r0, sl
	mov	r1, #0
	bl	SamsungSetRRPara
	ldr	r3, .L1014
	adds	r0, r4, #1
	ldrb	r3, [r3, #3832]	@ zero_extendqisi2
	movne	r0, #1
	add	r3, r3, r3, asl #1
	cmp	r4, r3, lsr #2
	movcc	r0, #0
	andcs	r0, r0, #1
	cmp	r0, #0
	moveq	r0, r4
	movne	r0, #256
	ldmfd	sp!, {r1, r2, r3, r4, r5, r6, r7, r8, r9, sl, fp, pc}
.L1015:
	.align	2
.L1014:
	.word	.LANCHOR0
	.word	.LANCHOR2
	.fnend
	.size	SamsungReadRetrial, .-SamsungReadRetrial
	.align	2
	.global	ToshibaReadRetrial
	.type	ToshibaReadRetrial, %function
ToshibaReadRetrial:
	.fnstart
	@ args = 0, pretend = 0, frame = 8
	@ frame_needed = 0, uses_anonymous_args = 0
	stmfd	sp!, {r0, r1, r2, r4, r5, r6, r7, r8, r9, sl, fp, lr}
	.save {r0, r1, r2, r4, r5, r6, r7, r8, r9, sl, fp, lr}
	mov	r8, r0
	str	r1, [sp, #4]
	mov	r9, r2
	mov	r7, r3
	bl	NandcWaitFlashReady
	ldr	r3, .L1037
	add	r2, r3, r8, asl #3
	ldr	r1, [r2, #864]
	ldrb	r4, [r2, #868]	@ zero_extendqisi2
	ldrb	r2, [r3, #3761]	@ zero_extendqisi2
	add	r4, r4, #8
	sub	r2, r2, #67
	add	r4, r1, r4, asl #8
	uxtb	r2, r2
	cmp	r2, #1
	movls	sl, #0
	bls	.L1017
	ldrb	sl, [r3, #3773]	@ zero_extendqisi2
	cmp	sl, #0
	beq	.L1018
	mov	r0, #0
	mov	sl, #1
	bl	NandcSetDdrMode
.L1018:
	mov	r3, #92
	str	r3, [r4, #8]
	mov	r3, #197
	str	r3, [r4, #8]
.L1017:
	mov	r6, #1
	mvn	r5, #0
	ldr	fp, .L1037
	b	.L1019
.L1028:
	ldrb	r3, [fp, #3761]	@ zero_extendqisi2
	mov	r0, r4
	uxtb	r1, r6
	sub	r3, r3, #67
	uxtb	r3, r3
	cmp	r3, #1
	bhi	.L1020
	bl	SandiskSetRRPara
	b	.L1021
.L1020:
	bl	ToshibaSetRRPara
.L1021:
	ldr	r2, .L1037
	ldrb	r3, [r2, #3761]	@ zero_extendqisi2
	cmp	r3, #34
	bne	.L1022
	ldr	r2, .L1037+4
	ldrb	r3, [r2, #136]	@ zero_extendqisi2
	sub	r3, r3, #3
	cmp	r6, r3
	moveq	r3, #179
	streq	r3, [r4, #8]
.L1022:
	cmp	sl, #0
	mov	r3, #38
	str	r3, [r4, #8]
	mov	r3, #93
	str	r3, [r4, #8]
	beq	.L1023
	mov	r0, #4
	bl	NandcSetDdrMode
	ldr	r1, [sp, #4]
	mov	r2, r9
	mov	r3, r7
	mov	r0, r8
	bl	FlashReadRawPage
	mov	r3, r0
	mov	r0, #0
	str	r3, [sp, #0]
	bl	NandcSetDdrMode
	ldr	r3, [sp, #0]
	b	.L1024
.L1023:
	mov	r3, r7
	mov	r0, r8
	ldr	r1, [sp, #4]
	mov	r2, r9
	bl	FlashReadRawPage
	mov	r3, r0
.L1024:
	cmn	r3, #1
	beq	.L1025
	ldrb	r2, [fp, #3832]	@ zero_extendqisi2
	cmn	r5, #1
	moveq	r5, r3
	add	r2, r2, r2, asl #1
	cmp	r3, r2, lsr #2
	bcc	.L1036
	mov	r7, #0
	mov	r9, r7
.L1025:
	add	r6, r6, #1
.L1019:
	ldr	r2, .L1037+4
	ldrb	r3, [r2, #136]	@ zero_extendqisi2
	add	r3, r3, #1
	cmp	r6, r3
	bcc	.L1028
	b	.L1027
.L1036:
	mov	r5, r3
.L1027:
	ldr	r6, .L1037
	mov	r0, r4
	mov	r1, #0
	ldrb	r3, [r6, #3761]	@ zero_extendqisi2
	sub	r3, r3, #67
	uxtb	r3, r3
	cmp	r3, #1
	bhi	.L1029
	bl	SandiskSetRRPara
	b	.L1030
.L1029:
	bl	ToshibaSetRRPara
.L1030:
	mov	r3, #255
	str	r3, [r4, #8]
	ldrb	r3, [r6, #3832]	@ zero_extendqisi2
	adds	r2, r5, #1
	mov	r0, r8
	movne	r2, #1
	add	r3, r3, r3, asl #1
	cmp	r5, r3, lsr #2
	movcc	r3, #0
	andcs	r3, r2, #1
	cmp	r3, #0
	movne	r5, #256
	bl	NandcWaitFlashReady
	cmp	sl, #0
	beq	.L1032
	mov	r0, #4
	bl	NandcSetDdrMode
.L1032:
	mov	r0, r5
	ldmfd	sp!, {r1, r2, r3, r4, r5, r6, r7, r8, r9, sl, fp, pc}
.L1038:
	.align	2
.L1037:
	.word	.LANCHOR0
	.word	.LANCHOR2
	.fnend
	.size	ToshibaReadRetrial, .-ToshibaReadRetrial
	.align	2
	.global	FlashSavePhyInfo
	.type	FlashSavePhyInfo, %function
FlashSavePhyInfo:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	stmfd	sp!, {r3, r4, r5, r6, r7, r8, sl, lr}
	.save {r3, r4, r5, r6, r7, r8, sl, lr}
	mov	r6, #0
	ldr	r5, .L1046
	ldr	r7, .L1046+4
	ldr	r8, .L1046+8
	ldr	r3, [r5, #144]
	ldrb	r0, [r5, #148]	@ zero_extendqisi2
	str	r3, [r5, #140]
	bl	FlashBchSel
	mov	r1, #0
	mov	r2, #2048
	ldr	r0, [r5, #144]
	bl	ftl_memset
	ldr	r3, [r5, #140]
	ldr	r1, .L1046+12
	mov	r2, #32
	str	r8, [r3, #0]
	ldr	r4, [r5, #140]
	ldrb	r3, [r7, #3762]	@ zero_extendqisi2
	add	r0, r4, #16
	strh	r3, [r4, #12]	@ movhi
	ldrb	r3, [r7, #853]	@ zero_extendqisi2
	strh	r3, [r4, #14]	@ movhi
	ldrb	r3, [r7, #3773]	@ zero_extendqisi2
	str	r3, [r4, #1076]
	bl	memcpy
	add	r0, r4, #80
	ldr	r1, .L1046+16
	mov	r2, #8
	bl	memcpy
	add	r0, r4, #96
	ldr	r1, .L1046+20
	mov	r2, #32
	bl	memcpy
	ldr	r0, [r5, #140]
	ldr	r1, .L1046+24
	mov	r2, #32
	add	r0, r0, #160
	bl	memcpy
	ldr	r4, [r5, #140]
	add	r1, r7, #896
	mov	r2, #32
	add	r0, r4, #192
	bl	memcpy
	mov	r2, #852
	add	r0, r4, #224
	mov	r1, r7
	bl	memcpy
	add	r0, r4, #12
	movw	r1, #2036
	bl	JSHash
	movw	r3, #1592
	str	r3, [r4, #4]
	ldr	r3, [r5, #132]
	str	r3, [r5, #140]
	str	r0, [r4, #8]
	mov	r0, #0
	bl	flash_enter_slc_mode
	mov	r4, r6
.L1042:
	ldr	r1, [r7, #856]
	mov	r0, #0
	mov	r2, r0
	mul	r1, r1, r4
	bl	FlashEraseBlock
	ldr	r1, [r7, #856]
	mov	r0, #0
	ldr	r2, [r5, #144]
	mov	r3, r0
	mul	r1, r1, r4
	bl	FlashProgPage
	ldr	r1, [r7, #856]
	mov	r0, #0
	ldr	r2, [r5, #144]
	mov	r3, r0
	mul	r1, r1, r4
	add	r1, r1, #1
	bl	FlashProgPage
	ldr	r1, [r7, #856]
	mov	r0, #0
	ldr	r2, [r5, #132]
	mov	r3, r0
	mul	r1, r1, r4
	bl	FlashReadRawPage
	cmn	r0, #1
	beq	.L1040
	ldr	sl, [r5, #140]
	ldr	r3, [sl, #0]
	cmp	r3, r8
	bne	.L1040
	add	r0, sl, #12
	movw	r1, #2036
	bl	JSHash
	ldr	r3, [sl, #8]
	cmp	r3, r0
	bne	.L1040
	add	r3, r4, #1
	str	r3, [r5, #152]
	ldr	r3, [r7, #856]
	add	r6, r6, #1
	cmp	r6, #1
	mul	r3, r3, r4
	str	r3, [r5, #156]
	bhi	.L1041
.L1040:
	add	r4, r4, #1
	cmp	r4, #4
	bne	.L1042
.L1041:
	mov	r0, #0
	bl	flash_exit_slc_mode
	cmp	r6, #0
	mvneq	r0, #0
	movne	r0, #0
	ldmfd	sp!, {r3, r4, r5, r6, r7, r8, sl, pc}
.L1047:
	.align	2
.L1046:
	.word	.LANCHOR2
	.word	.LANCHOR0
	.word	1312902724
	.word	.LANCHOR0+2980
	.word	.LANCHOR0+3764
	.word	.LANCHOR0+3588
	.word	.LANCHOR1+2828
	.fnend
	.size	FlashSavePhyInfo, .-FlashSavePhyInfo
	.align	2
	.global	FlashReadIdbDataRaw
	.type	FlashReadIdbDataRaw, %function
FlashReadIdbDataRaw:
	.fnstart
	@ args = 0, pretend = 0, frame = 16
	@ frame_needed = 0, uses_anonymous_args = 0
	stmfd	sp!, {r4, r5, r6, r7, r8, r9, sl, fp, lr}
	.save {r4, r5, r6, r7, r8, r9, sl, fp, lr}
	.pad #20
	sub	sp, sp, #20
	mov	r2, #4
	mov	sl, r0
	ldr	r1, .L1059
	add	r0, sp, #12
	bl	memcpy
	ldr	r3, .L1059+4
	ldrb	r2, [r3, #3832]	@ zero_extendqisi2
	str	r2, [sp, #4]
	ldr	r2, [r3, #3784]
	ldr	r3, .L1059+8
	cmp	r2, r3
	bne	.L1049
	mov	r0, #0
	bl	flash_enter_slc_mode
.L1049:
	ldr	r7, .L1059+4
	mov	r0, sl
	mov	r1, #0
	mov	r2, #2048
	bl	ftl_memset
	mvn	r8, #0
	mov	r4, #2
	ldr	r9, .L1059+12
	mov	fp, r7
	b	.L1050
.L1057:
	mov	r6, #0
.L1052:
	add	r3, sp, #12
	ldr	r5, .L1059+12
	ldrb	r0, [r3, r6]	@ zero_extendqisi2
	bl	FlashBchSel
	ldr	r1, [r7, #856]
	mov	r0, #0
	ldr	r2, [r9, #144]
	mov	r3, r0
	mul	r1, r1, r4
	bl	FlashReadRawPage
	cmn	r0, #1
	bne	.L1051
	add	r6, r6, #1
	cmp	r6, #4
	bne	.L1052
	b	.L1053
.L1051:
	ldr	r3, [r5, #144]
	ldr	r2, [r3, #0]
	ldr	r3, .L1059+16
	cmp	r2, r3
	bne	.L1053
	add	r3, sp, #16
	ldr	r0, .L1059+20
	add	r6, r3, r6
	ldrb	r1, [r6, #-4]	@ zero_extendqisi2
	bl	printk
	mov	r0, sl
	ldr	r1, [r5, #144]
	mov	r2, #2048
	bl	memcpy
	ldr	r3, [r5, #144]
	ldr	r3, [r3, #512]
	strb	r3, [fp, #853]
	ldr	r3, [r5, #152]
	cmp	r3, r4
	bls	.L1056
	str	r4, [r5, #152]
	bl	FlashSavePhyInfo
	mov	r8, #0
.L1053:
	add	r4, r4, #1
.L1050:
	ldrb	r3, [r7, #853]	@ zero_extendqisi2
	cmp	r4, r3
	bcc	.L1057
	b	.L1054
.L1056:
	mov	r8, #0
.L1054:
	ldr	r0, [sp, #4]
	bl	FlashBchSel
	ldr	r3, .L1059+4
	ldr	r2, [r3, #3784]
	ldr	r3, .L1059+8
	cmp	r2, r3
	bne	.L1055
	mov	r0, #0
	bl	flash_exit_slc_mode
.L1055:
	mov	r0, r8
	add	sp, sp, #20
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, sl, fp, pc}
.L1060:
	.align	2
.L1059:
	.word	.LANCHOR3+11
	.word	.LANCHOR0
	.word	1446522928
	.word	.LANCHOR2
	.word	-52655045
	.word	.LC88
	.fnend
	.size	FlashReadIdbDataRaw, .-FlashReadIdbDataRaw
	.align	2
	.global	FlashLoadPhyInfo
	.type	FlashLoadPhyInfo, %function
FlashLoadPhyInfo:
	.fnstart
	@ args = 0, pretend = 0, frame = 8
	@ frame_needed = 0, uses_anonymous_args = 0
	stmfd	sp!, {r0, r1, r2, r4, r5, r6, r7, r8, r9, sl, fp, lr}
	.save {r0, r1, r2, r4, r5, r6, r7, r8, r9, sl, fp, lr}
	mov	r2, #4
	ldr	r1, .L1072
	add	r0, sp, #4
	bl	memcpy
	ldr	r5, .L1072+4
	ldr	r2, .L1072+8
	movw	r3, #2838
	mov	r4, #0
	mov	r8, #4
	mov	r0, r4
	str	r4, [r5, #156]
	ldrh	r9, [r2, r3]
	mvn	r6, #0
	ldr	r3, [r5, #144]
	ldr	fp, .L1072+12
	str	r3, [r5, #140]
	bl	flash_enter_slc_mode
	b	.L1062
.L1064:
	add	r3, sp, #4
	ldrb	r0, [r3, r7]	@ zero_extendqisi2
	bl	FlashBchSel
	mov	r0, #0
	mov	r1, r4
	ldr	r2, [r5, #144]
	mov	r3, r0
	bl	FlashReadRawPage
	cmn	r0, #1
	bne	.L1063
	mov	r0, #0
	mov	r1, sl
	ldr	r2, [r5, #144]
	mov	r3, r0
	bl	FlashReadRawPage
	cmn	r0, #1
	bne	.L1063
	add	r7, r7, #1
	cmp	r7, #4
	beq	.L1065
	b	.L1064
.L1063:
	ldr	r7, [r5, #140]
	ldr	r3, [r7, #0]
	cmp	r3, fp
	bne	.L1065
	cmp	r6, #0
	bne	.L1066
	ldr	r2, .L1072+8
	movw	r3, #2838
	mov	r0, r4
	ldrh	r1, [r2, r3]
	bl	__aeabi_uidiv
	ldr	r3, .L1072+4
	add	r0, r0, #1
	str	r0, [r3, #152]
	mov	r0, r6
	b	.L1071
.L1066:
	add	r0, r7, #12
	movw	r1, #2036
	bl	JSHash
	ldr	r3, [r7, #8]
	cmp	r3, r0
	bne	.L1065
	ldr	r6, .L1072+16
	add	r1, r7, #160
	mov	r2, #32
	ldr	r0, .L1072+20
	bl	memcpy
	add	r0, r6, #896
	add	r1, r7, #192
	mov	r2, #32
	bl	memcpy
	mov	r0, r6
	add	r1, r7, #224
	mov	r2, #852
	bl	memcpy
	ldr	r3, [r7, #1076]
	ldr	r2, .L1072+8
	mov	r0, r4
	str	r4, [r5, #156]
	strb	r3, [r6, #3773]
	movw	r3, #2838
	ldrh	r1, [r2, r3]
	mov	r6, #0
	bl	__aeabi_uidiv
	add	r0, r0, #1
	str	r0, [r5, #152]
	cmp	r0, #1
	moveq	r3, #2
	streq	r3, [r5, #152]
	ldrh	r3, [r7, #14]
	strb	r3, [r5, #160]
.L1065:
	subs	r8, r8, #1
	add	r4, r4, r9
	beq	.L1069
.L1062:
	add	sl, r4, #1
	mov	r7, #0
	b	.L1064
.L1069:
	mov	r0, r8
.L1071:
	bl	flash_exit_slc_mode
	mov	r0, r6
	ldmfd	sp!, {r1, r2, r3, r4, r5, r6, r7, r8, r9, sl, fp, pc}
.L1073:
	.align	2
.L1072:
	.word	.LANCHOR3+11
	.word	.LANCHOR2
	.word	.LANCHOR1
	.word	1312902724
	.word	.LANCHOR0
	.word	.LANCHOR1+2828
	.fnend
	.size	FlashLoadPhyInfo, .-FlashLoadPhyInfo
	.align	2
	.global	FlashDdrTunningRead
	.type	FlashDdrTunningRead, %function
FlashDdrTunningRead:
	.fnstart
	@ args = 4, pretend = 0, frame = 16
	@ frame_needed = 0, uses_anonymous_args = 0
	stmfd	sp!, {r4, r5, r6, r7, r8, r9, sl, fp, lr}
	.save {r4, r5, r6, r7, r8, r9, sl, fp, lr}
	mov	sl, r3
	ldr	r4, .L1095
	.pad #20
	sub	sp, sp, #20
	mov	fp, r0
	mov	r9, r2
	str	r1, [sp, #0]
	ldr	r3, [r4, #3012]
	ldr	r7, [r4, #3780]
	ldr	r3, [r3, #304]
	cmp	r7, #7
	movhi	r7, #12
	movls	r7, #6
	str	r3, [sp, #12]
	ldr	r3, [sp, #56]
	cmp	r3, #0
	moveq	r8, #1024
	beq	.L1076
	mov	r0, #1
	bl	FlashSetInterfaceMode
	mov	r0, #1
	bl	NandcSetMode
	mov	r0, fp
	bl	FlashReset
	ldr	r1, [sp, #0]
	mov	r2, r9
	mov	r3, sl
	mov	r0, fp
	bl	FlashReadRawPage
	mov	r8, r0
	ldrb	r0, [r4, #3772]	@ zero_extendqisi2
	bl	FlashSetInterfaceMode
	ldrb	r0, [r4, #3772]	@ zero_extendqisi2
	bl	NandcSetMode
	cmn	r8, #1
	beq	.L1077
	ldr	r0, .L1095+4
	mov	r2, r8
	ldr	r1, [sp, #0]
	bl	printk
	cmp	r8, #9
	bhi	.L1078
	add	r4, r4, fp, asl #3
	ldr	r3, [r4, #864]
	ldr	r2, [r3, #3840]
	ldr	r2, [r3, #0]
	orr	r2, r2, #131072
	str	r2, [r3, #0]
.L1078:
	ldr	r3, .L1095+8
	ldr	r2, [r3, #164]
	add	r2, r2, #1
	str	r2, [r3, #164]
	cmp	r2, #2048
	movcs	sl, #0
	strcs	sl, [r3, #164]
	movcs	r9, sl
	bcc	.L1077
.L1076:
	mov	r4, #0
	str	fp, [sp, #4]
	mov	r5, r4
	mvn	r6, #0
	mov	fp, r4
	str	r4, [sp, #8]
.L1083:
	uxtb	r0, r7
	bl	NandcSetDdrPara
	mov	r3, sl
	ldr	r0, [sp, #4]
	mov	r2, r9
	ldr	r1, [sp, #0]
	bl	FlashReadRawPage
	add	r3, r8, #1
	cmp	r0, r3
	bhi	.L1079
	cmp	r0, #2
	bhi	.L1089
	add	r5, r5, #1
	cmp	r5, #9
	bls	.L1089
	mov	ip, fp
	rsb	r4, r5, r7
	ldr	fp, [sp, #4]
	mov	r8, r0
	mov	r6, #0
	b	.L1081
.L1079:
	cmp	fp, r5
	bcs	.L1090
	cmp	r5, #7
	rsb	r3, r5, r4
	str	r3, [sp, #8]
	bhi	.L1094
	mov	fp, r5
	b	.L1090
.L1089:
	mov	r6, #0
	mov	r4, r7
	mov	r8, r0
	mov	sl, r6
	mov	r9, r6
	b	.L1080
.L1090:
	mov	r5, #0
.L1080:
	add	r7, r7, #2
	cmp	r7, #69
	bls	.L1083
	mov	ip, fp
	ldr	fp, [sp, #4]
.L1081:
	cmp	ip, r5
	bcc	.L1084
	b	.L1082
.L1094:
	ldr	fp, [sp, #4]
.L1082:
	ldr	r4, [sp, #8]
.L1084:
	cmp	r4, #0
	beq	.L1085
	ldr	r0, .L1095+12
	mov	r1, r4
	bl	printk
	uxtb	r0, r4
	bl	NandcSetDdrPara
.L1085:
	cmn	r6, #1
	bne	.L1077
	ldr	r0, .L1095+16
	mov	r1, fp
	ldr	r2, [sp, #0]
	bl	printk
	ldr	r3, [sp, #56]
	cmp	r3, #0
	moveq	r8, r6
	beq	.L1077
	ldr	r3, [sp, #12]
	ubfx	r0, r3, #8, #8
	bl	NandcSetDdrPara
.L1077:
	mov	r0, r8
	add	sp, sp, #20
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, sl, fp, pc}
.L1096:
	.align	2
.L1095:
	.word	.LANCHOR0
	.word	.LC89
	.word	.LANCHOR2
	.word	.LC90
	.word	.LC91
	.fnend
	.size	FlashDdrTunningRead, .-FlashDdrTunningRead
	.align	2
	.global	FlashDdrParaScan
	.type	FlashDdrParaScan, %function
FlashDdrParaScan:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	stmfd	sp!, {r0, r1, r4, r5, r6, r7, r8, lr}
	.save {r0, r1, r4, r5, r6, r7, r8, lr}
	mov	r7, r0
	ldr	r5, .L1101
	mov	r4, #0
	mov	r6, r1
	ldrb	r0, [r5, #3772]	@ zero_extendqisi2
	bl	FlashSetInterfaceMode
	ldrb	r0, [r5, #3772]	@ zero_extendqisi2
	bl	NandcSetMode
	mov	r1, r6
	mov	r2, r4
	mov	r3, r4
	mov	r0, r7
	str	r4, [sp, #0]
	bl	FlashDdrTunningRead
	mov	r1, r6
	mov	r2, r4
	mov	r3, r4
	mov	r8, r0
	mov	r0, r7
	bl	FlashReadRawPage
	cmn	r0, #1
	beq	.L1098
	cmn	r8, #1
	bne	.L1099
.L1098:
	ldrb	r3, [r5, #3772]	@ zero_extendqisi2
	tst	r3, #1
	beq	.L1099
	mov	r0, #1
	bl	FlashSetInterfaceMode
	mov	r0, #1
	bl	NandcSetMode
	ldr	r3, .L1101
	mov	r2, #0
	strb	r2, [r3, #3773]
	b	.L1100
.L1099:
	mov	r3, #1
	strb	r3, [r5, #3773]
.L1100:
	mov	r0, #0
	ldmfd	sp!, {r2, r3, r4, r5, r6, r7, r8, pc}
.L1102:
	.align	2
.L1101:
	.word	.LANCHOR0
	.fnend
	.size	FlashDdrParaScan, .-FlashDdrParaScan
	.align	2
	.global	FlashReadPage
	.type	FlashReadPage, %function
FlashReadPage:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	stmfd	sp!, {r0, r1, r4, r5, r6, r7, r8, r9, sl, lr}
	.save {r0, r1, r4, r5, r6, r7, r8, r9, sl, lr}
	mov	r5, r0
	mov	r6, r1
	mov	r8, r2
	mov	sl, r3
	bl	FlashReadRawPage
	cmn	r0, #1
	mov	r4, r0
	bne	.L1104
	ldr	r7, .L1109
	ldrb	r9, [r7, #860]	@ zero_extendqisi2
	cmp	r9, #0
	beq	.L1105
	mov	r3, #0
	mov	r0, r5
	strb	r3, [r7, #860]
	mov	r1, r6
	mov	r2, r8
	mov	r3, sl
	bl	FlashReadRawPage
	strb	r9, [r7, #860]
	cmn	r0, #1
	movne	r4, r0
	bne	.L1104
.L1105:
	ldr	r7, .L1109
	ldrb	r3, [r7, #3773]	@ zero_extendqisi2
	cmp	r3, #0
	beq	.L1104
	ldr	r3, [r7, #3012]
	mov	r0, r5
	mov	r1, r6
	mov	r2, r8
	ldr	r9, [r3, #304]
	mov	r3, #1
	str	r3, [sp, #0]
	mov	r3, sl
	bl	FlashDdrTunningRead
	cmn	r0, #1
	mov	r4, r0
	beq	.L1106
	ldrb	r3, [r7, #3832]	@ zero_extendqisi2
	cmp	r0, r3, lsr #1
	bls	.L1104
.L1106:
	ubfx	r0, r9, #8, #8
	bl	NandcSetDdrPara
.L1104:
	ldr	r7, .L1109+4
	ldr	ip, [r7, #168]
	adds	r3, ip, #0
	movne	r3, #1
	cmn	r4, #1
	movne	r3, #0
	cmp	r3, #0
	beq	.L1107
	mov	r1, r6
	mov	r2, r8
	mov	r3, sl
	mov	r0, r5
	blx	ip
	mov	r2, r5
	mov	r3, r6
	mov	r4, r0
	ldr	r0, .L1109+8
	mov	r1, r4
	bl	printk
	cmn	r4, #1
	bne	.L1107
	ldr	r3, .L1109
	ldrb	r3, [r3, #928]	@ zero_extendqisi2
	cmp	r3, #0
	beq	.L1107
	mov	r0, r5
	bl	flash_enter_slc_mode
	ldr	ip, [r7, #168]
	mov	r0, r5
	mov	r1, r6
	mov	r2, r8
	mov	r3, sl
	blx	ip
	mov	r4, r0
	mov	r0, r5
	bl	flash_exit_slc_mode
.L1107:
	mov	r0, r4
	ldmfd	sp!, {r2, r3, r4, r5, r6, r7, r8, r9, sl, pc}
.L1110:
	.align	2
.L1109:
	.word	.LANCHOR0
	.word	.LANCHOR2
	.word	.LC92
	.fnend
	.size	FlashReadPage, .-FlashReadPage
	.align	2
	.global	FlashReadSlc2KPages
	.type	FlashReadSlc2KPages, %function
FlashReadSlc2KPages:
	.fnstart
	@ args = 0, pretend = 0, frame = 16
	@ frame_needed = 0, uses_anonymous_args = 0
	ldr	r3, .L1124
	stmfd	sp!, {r4, r5, r6, r7, r8, r9, sl, fp, lr}
	.save {r4, r5, r6, r7, r8, r9, sl, fp, lr}
	mov	r8, r1
	.pad #28
	sub	sp, sp, #28
	mov	fp, r2
	ldrb	sl, [r3, #2837]	@ zero_extendqisi2
	mov	r4, r0
	mov	r7, #0
	ldr	r6, .L1124+4
	b	.L1112
.L1121:
	rsb	r3, r7, r8
	add	r2, sp, #20
	mov	r0, r4
	mov	r1, fp
	uxtb	r3, r3
	str	r3, [sp, #0]
	add	r3, sp, #16
	bl	LogAddr2PhyAddr
	ldr	r3, [sp, #16]
	ldrb	r2, [r6, #3762]	@ zero_extendqisi2
	cmp	r3, r2
	mvncs	r3, #0
	strcs	r3, [r4, #0]
	bcs	.L1114
	add	r3, r6, r3
	ldrb	r5, [r3, #3764]	@ zero_extendqisi2
	mov	r0, r5
	bl	NandcWaitFlashReady
	mov	r0, r5
	bl	NandcFlashCs
	ldr	r1, [sp, #20]
	mov	r0, r5
	bl	FlashReadCmd
	mov	r0, r5
	bl	NandcWaitFlashReady
	ldr	r3, [r4, #12]
	mov	r2, sl
	mov	r1, #0
	mov	r0, r5
	str	r3, [sp, #0]
	ldr	r3, [r4, #8]
	bl	NandcXferData
	ldr	r3, [sp, #20]
	ldr	r1, [r6, #856]
	add	r1, r1, r3
	mov	r9, r0
	mov	r0, r5
	bl	FlashReadCmd
	ldr	r3, [r4, #8]
	ldr	r2, [r4, #12]
	mov	r1, #0
	cmp	r3, #0
	mov	r0, r5
	addne	r3, r3, #2048
	cmp	r2, #0
	addne	r2, r2, #8
	str	r2, [sp, #0]
	mov	r2, sl
	bl	NandcXferData
	mov	r3, r0
	mov	r0, r5
	str	r3, [sp, #12]
	bl	NandcFlashDeCs
	ldr	r3, [sp, #12]
	cmp	r3, r9
	movcs	r9, r3
	ldrb	r3, [r6, #3832]	@ zero_extendqisi2
	adds	r2, r9, #1
	add	r3, r3, r3, asl #1
	movne	r2, #1
	cmp	r9, r3, lsr #2
	movls	r3, #0
	andhi	r3, r2, #1
	cmp	r3, #0
	movne	r9, #256
	cmp	r9, #256
	cmnne	r9, #1
	str	r9, [r4, #0]
	movne	r3, #0
	strne	r3, [r4, #0]
	ldr	r3, [r4, #12]
	cmp	r3, #0
	beq	.L1119
	ldr	r2, [r3, #8]
	cmn	r2, #1
	bne	.L1119
	ldr	r3, [r3, #0]
	cmn	r3, #1
	strne	r2, [r4, #0]
.L1119:
	ldr	r3, [r4, #0]
	cmn	r3, #1
	bne	.L1114
	ldr	r1, [r4, #4]
	ldr	r0, .L1124+8
	ldrb	r2, [r6, #3832]	@ zero_extendqisi2
	bl	printk
	ldr	r1, [r4, #8]
	cmp	r1, #0
	beq	.L1120
	ldr	r0, .L1124+12
	mov	r2, #4
	mov	r3, #8
	bl	rknand_print_hex
.L1120:
	ldr	r1, [r4, #12]
	cmp	r1, #0
	beq	.L1114
	mov	r2, #4
	ldr	r0, .L1124+16
	mov	r3, r2
	bl	rknand_print_hex
.L1114:
	add	r7, r7, #1
	add	r4, r4, #36
.L1112:
	cmp	r7, r8
	bne	.L1121
	mov	r0, #0
	add	sp, sp, #28
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, sl, fp, pc}
.L1125:
	.align	2
.L1124:
	.word	.LANCHOR1
	.word	.LANCHOR0
	.word	.LC93
	.word	.LC94
	.word	.LC95
	.fnend
	.size	FlashReadSlc2KPages, .-FlashReadSlc2KPages
	.align	2
	.global	FlashReadPages
	.type	FlashReadPages, %function
FlashReadPages:
	.fnstart
	@ args = 0, pretend = 0, frame = 40
	@ frame_needed = 0, uses_anonymous_args = 0
	ldr	r3, .L1168
	stmfd	sp!, {r4, r5, r6, r7, r8, r9, sl, fp, lr}
	.save {r4, r5, r6, r7, r8, r9, sl, fp, lr}
	.pad #52
	sub	sp, sp, #52
	ldrb	r3, [r3, #2837]	@ zero_extendqisi2
	mov	r9, r0
	str	r1, [sp, #28]
	str	r2, [sp, #32]
	str	r3, [sp, #24]
	ldr	r3, .L1168+4
	ldrb	r8, [r3, #852]	@ zero_extendqisi2
	ldrb	ip, [r3, #860]	@ zero_extendqisi2
	cmp	r8, #0
	str	ip, [sp, #36]
	moveq	sl, r8
	beq	.L1127
	bl	FlashReadSlc2KPages
	b	.L1128
.L1155:
	mov	r3, #36
	ldr	ip, [sp, #28]
	mul	r3, r3, sl
	add	r2, sp, #44
	ldr	r1, [sp, #32]
	ldr	fp, .L1168+4
	add	r6, r9, r3
	str	r3, [sp, #16]
	rsb	r3, sl, ip
	ldr	r5, [r6, #4]
	mov	r0, r6
	uxtb	r3, r3
	str	r3, [sp, #0]
	add	r3, sp, #40
	bl	LogAddr2PhyAddr
	ldr	r3, [sp, #40]
	ldrb	r2, [fp, #3762]	@ zero_extendqisi2
	cmp	r3, r2
	ldrcs	ip, [sp, #16]
	mvncs	r3, #0
	mov	r7, r0
	strcs	r3, [r9, ip]
	bcs	.L1130
	add	r3, fp, r3
	ldrb	r4, [r3, #3764]	@ zero_extendqisi2
	ldr	r3, .L1168+8
	mov	r0, r4
	ldrb	r3, [r3, #172]	@ zero_extendqisi2
	cmp	r3, #0
	moveq	r7, #0
	bl	NandcWaitFlashReady
	ldr	r3, [fp, #3624]
	ldrb	r2, [r3, #19]	@ zero_extendqisi2
	sub	r3, r2, #1
	uxtb	r3, r3
	cmp	r3, #6
	bhi	.L1132
	add	r1, fp, r4
	cmp	r2, #7
	add	fp, fp, r4
	ldr	r2, .L1168+4
	ldrb	r3, [r1, #12]	@ zero_extendqisi2
	ldreqb	r3, [r1, #20]	@ zero_extendqisi2
	ldrb	r1, [fp, #3756]	@ zero_extendqisi2
	cmp	r1, r3
	beq	.L1132
	ldrb	r1, [r2, #1]	@ zero_extendqisi2
	mov	r0, r4
	add	r2, r2, #4
	bl	HynixSetRRPara
.L1132:
	mov	r0, r4
	mov	r5, r5, lsr #31
	bl	NandcFlashCs
	ldr	ip, [sp, #32]
	mov	r0, r4
	cmp	ip, #1
	orreq	r5, r5, #1
	str	r5, [sp, #20]
	cmp	r5, #0
	beq	.L1134
	ldr	r3, .L1168+4
	ldrb	r3, [r3, #928]	@ zero_extendqisi2
	cmp	r3, #0
	beq	.L1134
	bl	flash_enter_slc_mode
	b	.L1135
.L1134:
	bl	flash_exit_slc_mode
.L1135:
	ldr	fp, .L1168+4
.L1167:
	ldr	r1, [sp, #44]
	cmn	r1, #1
	cmpeq	r4, #255
	moveq	r3, #0
	movne	r3, #1
	moveq	r7, r3
	beq	.L1137
	cmp	r7, #0
	beq	.L1138
	ldr	r2, [fp, #856]
	mov	r0, r4
	add	r2, r1, r2
	bl	FlashReadDpCmd
	b	.L1139
.L1138:
	mov	r0, r4
	bl	FlashReadCmd
.L1139:
	mov	r0, r4
	bl	NandcWaitFlashReady
	cmp	r7, #0
	beq	.L1137
	mov	r0, r4
	ldr	r1, [sp, #44]
	bl	FlashReadDpDataOutCmd
.L1137:
	ldr	r3, [r6, #12]
	mov	r0, r4
	ldr	r2, [sp, #24]
	mov	r1, #0
	str	r3, [sp, #0]
	ldr	r3, [r6, #8]
	bl	NandcXferData
	ldrb	r2, [fp, #860]	@ zero_extendqisi2
	adds	r2, r2, #0
	movne	r2, #1
	cmn	r0, #1
	movne	r2, #0
	cmp	r2, #0
	movne	r2, #0
	movne	r7, #0
	strneb	r2, [fp, #860]
	bne	.L1167
.L1140:
	cmp	r7, #0
	mov	r5, r0
	beq	.L1141
	ldr	r3, .L1168+4
	mov	r0, r4
	ldr	r1, [r3, #856]
	ldr	r3, [sp, #44]
	str	r2, [sp, #12]
	add	r1, r1, r3
	bl	FlashReadDpDataOutCmd
	add	r3, sl, #1
	mov	r1, #36
	ldr	r2, [sp, #12]
	mla	r3, r1, r3, r9
	mov	r0, r4
	ldr	r1, [r3, #12]
	str	r1, [sp, #0]
	mov	r1, r2
	ldr	r3, [r3, #8]
	ldr	r2, [sp, #24]
	bl	NandcXferData
	cmn	r0, #1
	mov	r8, r0
	moveq	r7, #0
.L1141:
	mov	r0, r4
	ldr	fp, .L1168+4
	bl	NandcFlashDeCs
	ldr	ip, [sp, #36]
	cmn	r5, #1
	strb	ip, [fp, #860]
	bne	.L1145
	ldrb	r3, [fp, #3773]	@ zero_extendqisi2
	cmp	r3, #0
	beq	.L1143
	ldr	r3, [fp, #3012]
	mov	r0, r4
	ldr	r1, [sp, #44]
	ldr	r7, [r3, #304]
	mov	r3, #1
	str	r3, [sp, #0]
	ldr	r2, [r6, #8]
	ldr	r3, [r6, #12]
	bl	FlashDdrTunningRead
	cmn	r0, #1
	mov	r5, r0
	beq	.L1144
	ldrb	r3, [fp, #3832]	@ zero_extendqisi2
	cmp	r0, r3, lsr #1
	bls	.L1161
.L1144:
	ubfx	r0, r7, #8, #8
	bl	NandcSetDdrPara
	cmn	r5, #1
	bne	.L1161
.L1143:
	ldr	r3, .L1168+8
	mov	r0, r4
	ldr	r1, [sp, #44]
	ldr	r2, [r6, #8]
	ldr	r7, [r3, #168]
	ldr	r3, [r6, #12]
	cmp	r7, #0
	beq	.L1146
	blx	r7
	cmn	r0, #1
	mov	r5, r0
	bne	.L1163
	ldr	r2, .L1168+4
	ldr	r3, [r2, #3624]
	ldrb	r3, [r3, #19]	@ zero_extendqisi2
	sub	r3, r3, #1
	uxtb	r3, r3
	cmp	r3, #6
	bhi	.L1148
	ldrb	r1, [r2, #1]	@ zero_extendqisi2
	mov	r0, r4
	add	r2, r2, #4
	mov	r3, #0
	bl	HynixSetRRPara
.L1148:
	ldr	r1, [sp, #44]
	mov	r0, r4
	ldr	r2, [r6, #8]
	ldr	r3, [r6, #12]
	bl	FlashReadRawPage
	ldr	r7, .L1168+4
	ldr	r1, [r6, #4]
	ldrb	r2, [r7, #3832]	@ zero_extendqisi2
	mov	r5, r0
	ldr	r0, .L1168+12
	mov	r3, r5
	bl	printk
	cmn	r5, #1
	bne	.L1163
	ldrb	r7, [r7, #928]	@ zero_extendqisi2
	cmp	r7, #0
	beq	.L1147
	ldr	ip, [sp, #20]
	mov	r0, r4
	cmp	ip, #0
	beq	.L1149
	bl	flash_enter_slc_mode
	b	.L1150
.L1149:
	bl	flash_exit_slc_mode
.L1150:
	ldr	r3, .L1168+8
	mov	r0, r4
	ldr	r1, [sp, #44]
	ldr	r2, [r6, #8]
	ldr	ip, [r3, #168]
	ldr	r3, [r6, #12]
	blx	ip
	mov	r5, r0
	b	.L1163
.L1146:
	bl	FlashReadRawPage
	mov	r5, r0
	b	.L1147
.L1161:
	mov	r7, #0
.L1145:
	ldr	r3, .L1168+4
	ldrb	r3, [r3, #3832]	@ zero_extendqisi2
	add	r3, r3, r3, asl #1
	cmp	r5, r3, lsr #2
	bls	.L1147
	ldr	r3, .L1168+8
	ldr	r3, [r3, #168]
	cmp	r3, #0
	moveq	r5, #256
	b	.L1147
.L1163:
	mov	r7, #0
.L1147:
	ldr	ip, [sp, #16]
	cmp	r5, #256
	cmnne	r5, #1
	movne	r2, #0
	str	r5, [r9, ip]
	strne	r2, [r9, ip]
	ldr	ip, [sp, #16]
	ldr	r3, [r9, ip]
	cmn	r3, #1
	bne	.L1152
	ldr	r2, .L1168+4
	ldr	r1, [r6, #4]
	ldr	r0, .L1168+16
	ldrb	r2, [r2, #3832]	@ zero_extendqisi2
	bl	printk
	ldr	r1, [r6, #12]
	cmp	r1, #0
	beq	.L1152
	mov	r2, #4
	ldr	r0, .L1168+20
	mov	r3, r2
	bl	rknand_print_hex
.L1152:
	cmp	r7, #0
	beq	.L1153
	ldr	r3, .L1168+4
	ldrb	r3, [r3, #3832]	@ zero_extendqisi2
	add	r3, r3, r3, asl #1
	cmp	r8, r3, lsr #2
	bls	.L1154
	ldr	r3, .L1168+8
	ldr	r3, [r3, #168]
	cmp	r3, #0
	moveq	r8, #256
.L1154:
	add	r3, sl, #1
	mov	r2, #36
	cmp	r8, #256
	cmnne	r8, #1
	mul	r3, r2, r3
	movne	r2, #0
	str	r8, [r9, r3]
	strne	r2, [r9, r3]
.L1153:
	ldr	ip, [sp, #20]
	add	sl, sl, r7
	cmp	ip, #0
	beq	.L1130
	ldr	r3, .L1168+4
	ldrb	r3, [r3, #928]	@ zero_extendqisi2
	cmp	r3, #0
	beq	.L1130
	mov	r0, r4
	bl	flash_exit_slc_mode
.L1130:
	add	sl, sl, #1
.L1127:
	ldr	ip, [sp, #28]
	cmp	sl, ip
	bcc	.L1155
	mov	r0, #0
.L1128:
	add	sp, sp, #52
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, sl, fp, pc}
.L1169:
	.align	2
.L1168:
	.word	.LANCHOR1
	.word	.LANCHOR0
	.word	.LANCHOR2
	.word	.LC96
	.word	.LC93
	.word	.LC95
	.fnend
	.size	FlashReadPages, .-FlashReadPages
	.align	2
	.global	FtlGcScanTempBlk
	.type	FtlGcScanTempBlk, %function
FtlGcScanTempBlk:
	.fnstart
	@ args = 0, pretend = 0, frame = 24
	@ frame_needed = 0, uses_anonymous_args = 0
	ldr	r2, .L1195
	movw	r3, #2936
	stmfd	sp!, {r4, r5, r6, r7, r8, r9, sl, fp, lr}
	.save {r4, r5, r6, r7, r8, r9, sl, fp, lr}
	.pad #28
	sub	sp, sp, #28
	ldrh	r4, [r2, r3]
	movw	r3, #65535
	mov	r5, r0
	str	r1, [sp, #20]
	cmp	r4, r3
	beq	.L1191
	cmp	r4, #0
	bne	.L1171
	b	.L1172
.L1191:
	mov	r4, #0
.L1171:
	ldr	r2, .L1195+4
	movw	r3, #3908
	ldr	fp, [sp, #20]
	ldrh	r3, [r2, r3]
	cmp	fp, r3
	bne	.L1173
.L1172:
	bl	FtlGcPageVarInit
.L1173:
	mov	r7, #0
	movw	r8, #65535
	mov	sl, r7
	mvn	r3, #0
	str	r3, [sp, #16]
.L1186:
	ldrh	r3, [r5, #0]
	strb	sl, [r5, #8]
	cmp	r3, r8
	beq	.L1192
.L1175:
.L1193:
	ldr	r3, .L1195+4
	mov	r2, #3840
	ldrh	r9, [r3, r2]
	ldr	r2, .L1195+8
	ldr	lr, [r2, #-972]
	ldr	ip, [r2, #-1164]
	mov	r2, #3920
	ldrh	r2, [r3, r2]
	mov	r3, #0
	mov	r6, r3
	str	r2, [sp, #12]
	mov	r2, r5
	b	.L1176
.L1178:
	ldrh	r0, [r2, #16]
	cmp	r0, r8
	beq	.L1177
	mov	fp, #36
	orr	r0, r4, r0, asl #10
	mla	r1, fp, r6, lr
	ldr	fp, [sp, #12]
	stmib	r1, {r0, sl}
	mul	r0, fp, r6
	add	r6, r6, #1
	uxth	r6, r6
	add	fp, r0, #3
	cmp	r0, #0
	movlt	r0, fp
	bic	r0, r0, #3
	add	r0, ip, r0
	str	r0, [r1, #12]
.L1177:
	add	r3, r3, #1
	add	r2, r2, #2
	uxth	r3, r3
.L1176:
	cmp	r3, r9
	bne	.L1178
	ldr	r9, .L1195+8
	mov	r1, r6
	mov	r2, #0
	ldr	r0, [r9, #-972]
	bl	FlashReadPages
	mov	r3, #36
	mul	r3, r3, r6
	mov	r6, #0
	str	r3, [sp, #12]
	b	.L1179
.L1187:
	ldr	r1, [r9, #-972]
	add	r3, r1, r6
	ldr	fp, [r3, #4]
	stmib	sp, {r1, r3}
	ubfx	r0, fp, #10, #16
	bl	P2V_plane
	ldr	r1, [sp, #4]
	ldr	r3, [sp, #8]
	ldr	r1, [r1, r6]
	cmp	r1, #0
	mov	r2, r0
	bne	.L1180
	ldr	r3, [r3, #12]
	add	r6, r6, #36
	ldrh	r1, [r3, #0]
	cmp	r1, r8
	ldreq	r1, .L1195+8
	moveq	r3, #1
	streq	r3, [r1, #-1008]
	beq	.L1174
.L1181:
	ldr	r0, [r3, #12]
	mov	r1, fp
	ldr	r2, [r3, #8]
	bl	FtlGcUpdatePage
	b	.L1179
.L1180:
	mov	r2, fp
	ldr	r0, .L1195+12
	ldrh	r1, [r5, #0]
	bl	printk
	ldr	r2, .L1195+8
	ldr	r3, [r2, #-1884]
	cmp	r3, #0
	bne	.L1182
	ldr	r3, .L1195+4
	ldrb	r3, [r3, #928]	@ zero_extendqisi2
	cmp	r3, #0
	beq	.L1183
.L1182:
	ldr	r2, .L1195+8
	ldrh	r3, [r5, #0]
	ldr	r2, [r2, #-2084]
	mov	r3, r3, asl #1
	ldrh	r3, [r2, r3]
	cmp	r3, #119
	bls	.L1184
.L1183:
	ldr	r3, .L1195+8
	ldr	r3, [r3, #-972]
	ldr	r3, [r3, r6]
	cmn	r3, #1
	bne	.L1185
.L1184:
	ldr	r3, .L1195+8
	ldr	r3, [r3, #-972]
	add	r6, r3, r6
	ldr	r6, [r6, #4]
	str	r6, [sp, #16]
.L1185:
	ldr	r2, .L1195+8
	mov	r4, #0
	ldrh	r3, [r5, #0]
	ldr	r2, [r2, #-2064]
	mov	r3, r3, asl #1
	strh	sl, [r2, r3]	@ movhi
	ldrh	r0, [r5, #0]
	bl	INSERT_FREE_LIST
	mvn	r3, #0
	strh	r3, [r5, #0]	@ movhi
	bl	FtlGcPageVarInit
	b	.L1186
.L1179:
	ldr	r3, [sp, #12]
	cmp	r6, r3
	bne	.L1187
	ldr	fp, [sp, #20]
	add	r7, r7, #1
	add	r4, r4, #1
	cmp	r7, fp
	uxth	r4, r4
	bcc	.L1188
	ldr	r2, .L1195
	movw	r3, #2936
	ldrh	r1, [r2, r3]
	cmp	r1, r8
	beq	.L1188
	add	r1, r1, r7
	strh	r1, [r2, r3]	@ movhi
	ldr	r2, .L1195+4
	add	r3, r3, #972
	ldrh	r3, [r2, r3]
	cmp	r3, r4
	bhi	.L1189
.L1188:
	ldr	r2, .L1195+4
	movw	r3, #3908
	ldrh	r3, [r2, r3]
	cmp	r3, r4
	bhi	.L1193
.L1192:
	mov	r2, #0
.L1174:
	ldr	r1, .L1195
	movw	r3, #2936
	mvn	r0, #0
	strh	r4, [r5, #2]	@ movhi
	strb	r2, [r5, #6]
	strh	r0, [r1, r3]	@ movhi
	mov	r0, r5
	mov	r1, r4
	bl	ftl_sb_update_avl_pages
.L1189:
	ldr	r0, [sp, #16]
	add	sp, sp, #28
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, sl, fp, pc}
.L1196:
	.align	2
.L1195:
	.word	.LANCHOR1
	.word	.LANCHOR0
	.word	.LANCHOR2
	.word	.LC97
	.fnend
	.size	FtlGcScanTempBlk, .-FtlGcScanTempBlk
	.align	2
	.global	FtlScanSysBlk
	.type	FtlScanSysBlk, %function
FtlScanSysBlk:
	.fnstart
	@ args = 0, pretend = 0, frame = 24
	@ frame_needed = 0, uses_anonymous_args = 0
	stmfd	sp!, {r4, r5, r6, r7, r8, r9, sl, fp, lr}
	.save {r4, r5, r6, r7, r8, r9, sl, fp, lr}
	mov	r6, #0
	ldr	r5, .L1255
	.pad #28
	sub	sp, sp, #28
	ldr	r4, .L1255+4
	mov	r1, r6
	ldr	r3, .L1255+8
	movw	r7, #3928
	ldr	r2, [r5, #3936]
	ldr	r0, [r4, #-1032]
	mov	sl, r4
	strh	r6, [r4, r3]	@ movhi
	movw	r3, #3954
	strh	r6, [r5, r3]	@ movhi
	mov	r2, r2, asl #2
	bl	ftl_memset
	ldr	r2, [r5, #3936]
	mov	r1, r6
	ldr	r0, [r4, #-1036]
	mov	r2, r2, asl #1
	bl	ftl_memset
	ldrh	r2, [r5, r7]
	mov	r1, r6
	ldr	r0, [r4, #-912]
	mov	r2, r2, asl #2
	bl	ftl_memset
	ldrh	r2, [r5, r7]
	mov	r1, r6
	ldr	r0, [r5, #3956]
	mov	r2, r2, asl #1
	bl	ftl_memset
	ldr	r0, .L1255+12
	mov	r1, #255
	mov	r2, #12
	bl	ftl_memset
	movw	r3, #3848
	ldrh	r3, [r5, r3]
	str	r3, [sp, #8]
	b	.L1198
.L1200:
	add	r3, r6, r5
	ldr	r1, [sp, #8]
	ldrb	r0, [r3, #3866]	@ zero_extendqisi2
	bl	V2P_block
	str	r0, [sp, #0]
	bl	FtlBbmIsBadBlock
	ldr	r3, [sp, #0]
	cmp	r0, #0
	bne	.L1199
	ldr	r2, [r4, #-972]
	mov	r3, r3, asl #10
	mla	r2, r9, r7, r2
	str	r3, [r2, #4]
	ldr	r3, [r4, #-1168]
	str	r3, [r2, #8]
	ldrh	r3, [r6, r8]
	mul	r3, r3, r7
	add	r7, r7, #1
	uxth	r7, r7
	add	r1, r3, #3
	cmp	r3, #0
	movlt	r3, r1
	ldr	r1, [r4, #-1164]
	bic	r3, r3, #3
	add	r3, r1, r3
	str	r3, [r2, #12]
.L1199:
	add	r5, r5, #1
	uxth	r5, r5
	b	.L1233
.L1246:
	mov	r7, #0
	mov	fp, #3840
	mov	r5, r7
	mov	r9, #36
	mov	r8, #3920
.L1233:
	ldrh	r3, [r6, fp]
	cmp	r3, r5
	bhi	.L1200
	cmp	r7, #0
	beq	.L1201
	ldr	r0, [r4, #-972]
	mov	r1, r7
	mov	r2, #1
	mov	fp, r7
	bl	FlashReadPages
	mov	r8, #0
	str	r8, [sp, #12]
.L1232:
	ldr	r3, [r4, #-972]
	add	r2, r3, r8
	ldr	r3, [r3, r8]
	ldr	r6, [r2, #4]
	cmn	r3, #1
	ldr	r5, [r2, #12]
	ubfx	r6, r6, #10, #16
	bne	.L1202
	mov	r7, #16
.L1204:
	ldr	r0, [r4, #-972]
	mov	r1, #1
	mov	r2, r1
	add	r0, r0, r8
	ldr	r3, [r0, #4]
	add	r3, r3, #1
	str	r3, [r0, #4]
	bl	FlashReadPages
	ldrh	r3, [r5, #0]
	movw	r1, #65535
	cmp	r3, r1
	ldreq	r3, [sl, #-972]
	mvneq	r2, #0
	streq	r2, [r3, r8]
	beq	.L1202
.L1203:
	ldr	r3, [r4, #-972]
	ldr	r3, [r3, r8]
	cmn	r3, #1
	bne	.L1202
	sub	r7, r7, #1
	uxth	r7, r7
	cmp	r7, #0
	bne	.L1204
.L1202:
	ldr	r3, [r4, #-972]
	ldr	r3, [r3, r8]
	cmn	r3, #1
	beq	.L1205
	ldr	r2, [r4, #-1848]
	ldr	r3, [r5, #4]
	cmn	r2, #1
	beq	.L1206
	cmp	r2, r3
	bhi	.L1207
.L1206:
	cmn	r3, #1
	addne	r3, r3, #1
	strne	r3, [sl, #-1848]
.L1207:
	ldrh	r3, [r5, #0]
	movw	r2, #61604
	cmp	r3, r2
	beq	.L1210
	bhi	.L1213
	movw	r2, #61574
	cmp	r3, r2
	bne	.L1208
	b	.L1253
.L1213:
	movw	r2, #61634
	cmp	r3, r2
	beq	.L1211
	movw	r2, #65535
	cmp	r3, r2
	bne	.L1208
	b	.L1254
.L1211:
	ldr	r3, .L1255
	mov	r9, r6
	ldr	r1, .L1255+8
	ldr	ip, [r4, #-1032]
	ldr	r0, [r3, #3936]
	ldrh	r1, [r4, r1]
	uxth	r2, r0
	sub	r3, r2, #1
	rsb	r2, r1, r2
	sub	r2, r2, #1
	uxth	r3, r3
	sxth	r2, r2
	str	r2, [sp, #4]
	b	.L1214
.L1220:
	mov	r6, r2, asl #2
	ldr	r7, [r5, #4]
	str	r6, [sp, #16]
	ldr	r6, [ip, r2, asl #2]
	cmp	r7, r6
	bls	.L1215
	ldr	ip, [ip, #0]
	mov	r6, r9
	cmp	ip, #0
	bne	.L1216
	cmp	r1, r0
	addne	r1, r1, #1
	ldrne	r0, .L1255+8
	strneh	r1, [r4, r0]	@ movhi
.L1216:
	mov	r0, #0
	sxth	r9, r3
	str	r3, [sp, #4]
	b	.L1217
.L1218:
	ldr	r7, [r4, #-1032]
	add	ip, r1, #1
	add	r0, r0, #1
	ldr	r3, [r7, ip, asl #2]
	mov	ip, ip, asl #1
	uxth	r0, r0
	str	r3, [r7, r1, asl #2]
	mov	r1, r1, asl #1
	ldr	r7, [r4, #-1036]
	ldrh	ip, [r7, ip]
	strh	ip, [r7, r1]	@ movhi
.L1217:
	sxth	r1, r0
	cmp	r1, r9
	bne	.L1218
	ldr	r1, [sl, #-1032]
	mov	r2, r2, asl #1
	ldr	r0, [r5, #4]
	ldr	ip, [sp, #16]
	ldr	r3, [sp, #4]
	str	r0, [r1, ip]
	ldr	r1, [sl, #-1036]
	strh	r6, [r1, r2]	@ movhi
	b	.L1219
.L1215:
	sub	r3, r3, #1
	uxth	r3, r3
.L1214:
	ldr	r6, [sp, #4]
	sxth	r2, r3
	cmp	r2, r6
	bgt	.L1220
	mov	r6, r9
.L1219:
	sxth	r3, r3
	cmp	r3, #0
	blt	.L1208
	ldr	r2, .L1255+8
	ldr	r0, .L1255
	ldrh	r1, [r4, r2]
	ldr	r0, [r0, #3936]
	rsb	r0, r1, r0
	sub	r0, r0, #1
	sxth	r0, r0
	cmp	r3, r0
	bgt	.L1208
	add	r1, r1, #1
	strh	r1, [r4, r2]	@ movhi
	ldr	r2, [r4, #-1032]
	ldr	r1, [r5, #4]
	str	r1, [r2, r3, asl #2]
	mov	r3, r3, asl #1
	ldr	r2, [r4, #-1036]
	b	.L1250
.L1253:
	ldr	r1, .L1255
	movw	r3, #3928
	movw	ip, #3954
	mov	r9, r6
	ldrh	r0, [r1, r3]
	ldrh	r1, [r1, ip]
	sub	r2, r0, #1
	ldr	ip, [r4, #-912]
	uxth	r3, r2
	rsb	r2, r1, r2
	str	r2, [sp, #4]
	b	.L1221
.L1227:
	mov	r6, r2, asl #2
	ldr	r7, [r5, #4]
	str	r6, [sp, #16]
	ldr	r6, [ip, r2, asl #2]
	cmp	r7, r6
	bls	.L1222
	ldr	ip, [ip, #0]
	mov	r6, r9
	cmp	ip, #0
	bne	.L1223
	cmp	r1, r0
	addne	r1, r1, #1
	ldrne	ip, .L1255
	movwne	r0, #3954
	strneh	r1, [ip, r0]	@ movhi
.L1223:
	sxth	ip, r3
	mov	r0, #0
	str	ip, [sp, #20]
	ldr	r9, .L1255
	str	r3, [sp, #4]
	b	.L1224
.L1225:
	ldr	r7, [r4, #-912]
	add	ip, r1, #1
	add	r0, r0, #1
	ldr	r3, [r7, ip, asl #2]
	mov	ip, ip, asl #1
	uxth	r0, r0
	str	r3, [r7, r1, asl #2]
	mov	r1, r1, asl #1
	ldr	r7, [r9, #3956]
	ldrh	ip, [r7, ip]
	strh	ip, [r7, r1]	@ movhi
.L1224:
	ldr	ip, [sp, #20]
	sxth	r1, r0
	cmp	r1, ip
	bne	.L1225
	ldr	r1, [sl, #-912]
	mov	r2, r2, asl #1
	ldr	r0, [r5, #4]
	ldr	ip, [sp, #16]
	ldr	r3, [sp, #4]
	str	r0, [r1, ip]
	ldr	r1, .L1255
	ldr	r1, [r1, #3956]
	strh	r6, [r1, r2]	@ movhi
	b	.L1226
.L1222:
	sub	r3, r3, #1
	uxth	r3, r3
.L1221:
	ldr	r6, [sp, #4]
	sxth	r2, r3
	cmp	r2, r6
	bgt	.L1227
	mov	r6, r9
.L1226:
	sxth	r3, r3
	cmp	r3, #0
	blt	.L1208
	ldr	r2, .L1255
	movw	ip, #3928
	movw	r1, #3954
	ldrh	ip, [r2, ip]
	ldrh	r0, [r2, r1]
	sub	ip, ip, #1
	rsb	ip, r0, ip
	sxth	ip, ip
	cmp	r3, ip
	bgt	.L1208
	add	r0, r0, #1
	strh	r0, [r2, r1]	@ movhi
	ldr	r0, [r5, #4]
	ldr	r1, [r4, #-912]
	str	r0, [r1, r3, asl #2]
	mov	r3, r3, asl #1
	ldr	r2, [r2, #3956]
.L1250:
	strh	r6, [r2, r3]	@ movhi
	b	.L1208
.L1210:
	ldr	r3, .L1255+16
	movw	ip, #65535
	ldrh	r2, [r4, r3]
	cmp	r2, ip
	streqh	r6, [r4, r3]	@ movhi
	beq	.L1251
	ldr	r3, .L1255+20
	movw	r1, #65535
	ldrh	r0, [r4, r3]
	cmp	r0, r1
	beq	.L1229
	mov	r1, #1
	bl	FtlFreeSysBlkQueueIn
.L1229:
	ldr	r3, [r5, #4]
	ldr	r2, [sl, #-1760]
	cmp	r2, r3
	ldr	r3, .L1255+20
	strcsh	r6, [r4, r3]	@ movhi
	bcs	.L1208
	ldr	r2, .L1255+16
	ldrh	r1, [r4, r2]
	strh	r6, [r4, r2]	@ movhi
	strh	r1, [r4, r3]	@ movhi
.L1251:
	ldr	r3, [r5, #4]
	str	r3, [r4, #-1760]
	b	.L1208
.L1254:
	mov	r0, r6
	b	.L1252
.L1205:
	ldr	r3, .L1255
	mov	r0, r6
	ldrb	r1, [r3, #928]	@ zero_extendqisi2
	cmp	r1, #0
	beq	.L1231
.L1252:
	mov	r1, #0
.L1231:
	bl	FtlFreeSysBlkQueueIn
.L1208:
	ldr	r6, [sp, #12]
	add	r8, r8, #36
	add	r3, r6, #1
	uxth	r3, r3
	str	r3, [sp, #12]
	cmp	r3, fp
	bne	.L1232
.L1201:
	ldr	ip, [sp, #8]
	add	r3, ip, #1
	uxth	r3, r3
	str	r3, [sp, #8]
.L1198:
	ldr	r6, .L1255
	movw	r3, #3850
	ldr	ip, [sp, #8]
	ldrh	r3, [r6, r3]
	cmp	r3, ip
	bhi	.L1246
	ldr	r1, .L1255+4
	ldr	r2, [r1, #-1036]
	ldrh	r3, [r2, #0]
	cmp	r3, #0
	bne	.L1234
	ldr	r0, .L1255+8
	ldrh	r1, [r1, r0]
	cmp	r1, #0
	ldrne	r0, [r6, #3936]
	bne	.L1235
	b	.L1234
.L1239:
	mov	r1, r1, asl #1
	ldrh	r1, [r2, r1]
	cmp	r1, #0
	beq	.L1236
	ldr	r7, .L1255
	sxth	r6, r3
	ldr	r1, .L1255+4
	mov	r5, #0
	b	.L1237
.L1238:
	ldr	ip, [r1, #-1036]
	mov	r0, r2, asl #1
	rsb	r4, r6, r2
	add	r3, r3, #1
	ldrh	sl, [ip, r0]
	mov	r8, r4, asl #1
	uxth	r3, r3
	strh	sl, [ip, r8]	@ movhi
	ldr	ip, [r1, #-1032]
	ldr	r2, [ip, r2, asl #2]
	str	r2, [ip, r4, asl #2]
	ldr	r2, [r1, #-1036]
	strh	r5, [r2, r0]	@ movhi
.L1237:
	ldr	r0, [r7, #3936]
	sxth	r2, r3
	cmp	r2, r0
	bcc	.L1238
	b	.L1234
.L1236:
	add	r3, r3, #1
	uxth	r3, r3
.L1235:
	sxth	r1, r3
	cmp	r1, r0
	bcc	.L1239
.L1234:
	ldr	r2, .L1255
	ldr	r1, [r2, #3956]
	ldrh	r3, [r1, #0]
	cmp	r3, #0
	bne	.L1240
	movw	r0, #3954
	ldrh	r0, [r2, r0]
	cmp	r0, #0
	movwne	r0, #3928
	ldrneh	r2, [r2, r0]
	bne	.L1241
	b	.L1240
.L1245:
	mov	ip, r0, asl #1
	ldrh	ip, [r1, ip]
	cmp	ip, #0
	beq	.L1242
	ldr	r1, .L1255
	movw	r8, #3928
	ldr	r7, .L1255+4
	mov	r6, #0
	b	.L1243
.L1244:
	ldr	r4, [r1, #3956]
	mov	ip, r2, asl #1
	rsb	r5, r0, r2
	add	r3, r3, #1
	ldrh	r9, [r4, ip]
	mov	sl, r5, asl #1
	uxth	r3, r3
	strh	r9, [r4, sl]	@ movhi
	ldr	r4, [r7, #-912]
	ldr	r2, [r4, r2, asl #2]
	str	r2, [r4, r5, asl #2]
	ldr	r2, [r1, #3956]
	strh	r6, [r2, ip]	@ movhi
.L1243:
	ldrh	ip, [r1, r8]
	sxth	r2, r3
	cmp	r2, ip
	blt	.L1244
	b	.L1240
.L1242:
	add	r3, r3, #1
	uxth	r3, r3
.L1241:
	sxth	r0, r3
	cmp	r0, r2
	blt	.L1245
.L1240:
	mov	r0, #0
	add	sp, sp, #28
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, sl, fp, pc}
.L1256:
	.align	2
.L1255:
	.word	.LANCHOR0
	.word	.LANCHOR2
	.word	-1040
	.word	.LANCHOR2-1768
	.word	-1768
	.word	-1764
	.fnend
	.size	FtlScanSysBlk, .-FtlScanSysBlk
	.align	2
	.global	FtlGetLastWrittenPage
	.type	FtlGetLastWrittenPage, %function
FtlGetLastWrittenPage:
	.fnstart
	@ args = 0, pretend = 0, frame = 104
	@ frame_needed = 0, uses_anonymous_args = 0
	cmp	r1, #1
	ldr	r3, .L1268
	stmfd	sp!, {r4, r5, r6, r7, r8, sl, lr}
	.save {r4, r5, r6, r7, r8, sl, lr}
	movweq	r2, #3910
	movwne	r2, #3908
	.pad #108
	sub	sp, sp, #108
	ldrh	r4, [r3, r2]
	add	r7, sp, #4
	mov	r5, r1
	add	r3, sp, #40
	sub	r4, r4, #1
	str	r3, [sp, #16]
	mov	sl, r0, asl #10
	mov	r1, #1
	uxth	r4, r4
	mov	r0, r7
	mov	r2, r5
	mov	r8, #0
	sxth	r3, r4
	str	r8, [sp, #12]
	orr	r3, r3, sl
	str	r3, [sp, #8]
	bl	FlashReadPages
	ldr	r3, [sp, #40]
	cmn	r3, #1
	bne	.L1261
	b	.L1266
.L1264:
	add	r6, r6, r3
	mov	r0, r7
	mov	r1, #1
	mov	r2, r5
	add	r6, r6, r6, lsr #31
	ubfx	r6, r6, #1, #16
	sxth	r3, r6
	orr	r3, r3, sl
	str	r3, [sp, #8]
	bl	FlashReadPages
	ldr	r3, [sp, #40]
	cmn	r3, #1
	bne	.L1262
	ldr	r3, [sp, #44]
	cmn	r3, #1
	bne	.L1262
	ldr	r3, [sp, #4]
	cmn	r3, #1
	subne	r4, r6, #1
	uxthne	r4, r4
	bne	.L1266
.L1262:
	add	r6, r6, #1
	uxth	r8, r6
.L1266:
	sxth	r6, r8
	sxth	r3, r4
	cmp	r6, r3
	ble	.L1264
.L1261:
	sxth	r0, r4
	add	sp, sp, #108
	ldmfd	sp!, {r4, r5, r6, r7, r8, sl, pc}
.L1269:
	.align	2
.L1268:
	.word	.LANCHOR0
	.fnend
	.size	FtlGetLastWrittenPage, .-FtlGetLastWrittenPage
	.align	2
	.global	FtlLoadSysInfo
	.type	FtlLoadSysInfo, %function
FtlLoadSysInfo:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	stmfd	sp!, {r3, r4, r5, r6, r7, lr}
	.save {r3, r4, r5, r6, r7, lr}
	mov	r5, #0
	ldr	r4, .L1286
	mov	r1, r5
	ldr	r2, .L1286+4
	ldr	r7, .L1286+8
	ldr	r3, [r4, #-940]
	str	r5, [r4, #184]
	ldr	r0, [r4, #-2064]
	str	r3, [r4, #188]
	movw	r3, #3848
	ldrh	r2, [r2, r3]
	mov	r2, r2, asl #1
	bl	ftl_memset
	ldrh	r0, [r4, r7]
	movw	r3, #65535
	cmp	r0, r3
	beq	.L1284
	mov	r1, #1
	bl	FtlGetLastWrittenPage
	ldr	r3, .L1286+12
	add	r2, r0, #1
	mov	r6, r0
	strh	r2, [r4, r3]	@ movhi
	b	.L1272
.L1275:
	ldrh	r2, [r4, r7]
	mov	r1, #1
	ldr	r0, .L1286+16
	orr	r3, r3, r2, asl #10
	str	r3, [r4, #180]
	ldr	r3, [r4, #-2076]
	mov	r2, r1
	str	r3, [r4, #184]
	bl	FlashReadPages
	ldr	r3, [r4, #176]
	cmn	r3, #1
	beq	.L1273
	ldr	r3, [r4, #-2076]
	ldr	r2, [r3, #0]
	ldr	r3, .L1286+20
	cmp	r2, r3
	bne	.L1273
	ldr	r3, [r4, #-940]
	ldrh	r2, [r3, #0]
	movw	r3, #61604
	cmp	r2, r3
	beq	.L1274
.L1273:
	sub	r5, r5, #1
	uxth	r5, r5
.L1272:
	add	r3, r5, r6
	sxth	r3, r3
	cmp	r3, #0
	bge	.L1275
.L1274:
	ldr	r4, .L1286
	mov	r2, #48
	ldr	r5, .L1286+4
	movw	r6, #3848
	ldr	r0, .L1286+24
	ldr	r7, [r4, #184]
	mov	r1, r7
	bl	memcpy
	ldrh	r2, [r5, r6]
	add	r1, r7, #48
	ldr	r0, [r4, #-2064]
	mov	r2, r2, asl #1
	bl	memcpy
	ldrh	r1, [r5, r6]
	ldr	r3, [r4, #184]
	ldr	r0, [r4, #-1888]
	mov	r2, r1, lsr #3
	add	r1, r1, #24
	add	r2, r2, #4
	mov	r1, r1, lsr #1
	add	r1, r3, r1, asl #2
	bl	memcpy
	mov	r3, #3952
	ldrh	r3, [r5, r3]
	cmp	r3, #0
	beq	.L1276
	ldrh	r3, [r5, r6]
	movw	r2, #3944
	ldrh	r2, [r5, r2]
	ldr	r0, [r4, #-904]
	mov	r1, r3, lsr #3
	add	r1, r1, r3, asl #1
	ldr	r3, [r4, #184]
	add	r1, r1, #52
	mov	r2, r2, asl #2
	ubfx	r1, r1, #2, #14
	add	r1, r3, r1, asl #2
	bl	memcpy
.L1276:
	ldr	r4, .L1286
	ldr	r3, .L1286+20
	ldr	r2, [r4, #-1820]
	cmp	r2, r3
	bne	.L1284
	ldr	r3, .L1286+28
	movw	r1, #3862
	ldrb	r0, [r4, #-1810]	@ zero_extendqisi2
	ldrh	r2, [r4, r3]
	add	r3, r3, #50
	strh	r2, [r4, r3]	@ movhi
	ldr	r3, .L1286+4
	ldrh	r1, [r3, r1]
	cmp	r0, r1
	bne	.L1284
	movw	r1, #3908
	movw	r0, #3914
	ldrh	r1, [r3, r1]
	movw	r5, #65535
	ldrh	r0, [r3, r0]
	str	r2, [r4, #212]
	mul	r1, r2, r1
	str	r1, [r3, #3968]
	mul	r1, r0, r1
	ldr	r0, [r3, #3852]
	str	r1, [r3, #3948]
	movw	r1, #3982
	ldrh	r1, [r3, r1]
	rsb	r0, r1, r0
	rsb	r0, r2, r0
	mov	r2, #3840
	ldrh	r1, [r3, r2]
	bl	__aeabi_uidiv
	ldr	r3, .L1286+32
	ldr	r1, .L1286+36
	strh	r0, [r4, r3]	@ movhi
	sub	r3, r3, #34
	ldrh	r2, [r4, r3]
	sub	r3, r3, #238
	strh	r2, [r4, r3]	@ movhi
	add	r3, r3, #240
	ldrh	r3, [r4, r3]
	mov	r0, r3, lsr #6
	and	r3, r3, #63
	strb	r3, [r4, #-2038]
	ldrb	r3, [r4, #-1809]	@ zero_extendqisi2
	strh	r0, [r4, r1]	@ movhi
	mvn	r1, #0
	ldr	r0, .L1286+40
	strb	r3, [r4, #-2036]
	ldr	r3, .L1286+44
	strh	r1, [r4, r3]	@ movhi
	mov	r3, #0
	ldr	r1, .L1286+48
	strh	r3, [r4, r0]	@ movhi
	strb	r3, [r4, #-1750]
	ldrh	r0, [r4, r1]
	sub	r1, r1, #194
	strb	r3, [r4, #-1748]
	str	r3, [r4, #-1860]
	strh	r0, [r4, r1]	@ movhi
	add	r1, r1, #196
	ldr	r0, .L1286+52
	ldrh	r1, [r4, r1]
	mov	ip, r1, lsr #6
	and	r1, r1, #63
	strb	r1, [r4, #-1990]
	ldrb	r1, [r4, #-1808]	@ zero_extendqisi2
	strh	ip, [r4, r0]	@ movhi
	strb	r1, [r4, #-1988]
	ldr	r1, .L1286+56
	ldrh	r0, [r4, r1]
	sub	r1, r1, #150
	strh	r0, [r4, r1]	@ movhi
	add	r1, r1, #152
	ldr	r0, .L1286+60
	ldrh	r1, [r4, r1]
	mov	ip, r1, lsr #6
	and	r1, r1, #63
	strb	r1, [r4, #-1942]
	ldrb	r1, [r4, #-1807]	@ zero_extendqisi2
	strh	ip, [r4, r0]	@ movhi
	strb	r1, [r4, #-1940]
	str	r3, [r4, #-1872]
	ldr	r1, [r4, #-1788]
	str	r3, [r4, #-1880]
	str	r3, [r4, #-1864]
	str	r3, [r4, #-1836]
	str	r3, [r4, #-1828]
	str	r3, [r4, #-1868]
	ldr	r3, [r4, #-1780]
	str	r1, [r4, #-1840]
	ldr	r1, [r4, #-1848]
	cmp	r3, r1
	strhi	r3, [r4, #-1848]
	ldr	r4, .L1286
	ldr	r3, [r4, #-1784]
	ldr	r1, [r4, #-1844]
	cmp	r3, r1
	strhi	r3, [r4, #-1844]
	cmp	r2, r5
	beq	.L1279
	ldr	r0, .L1286+64
	bl	make_superblock
.L1279:
	ldr	r3, .L1286+68
	ldrh	r3, [r4, r3]
	cmp	r3, r5
	beq	.L1280
	ldr	r0, .L1286+72
	bl	make_superblock
.L1280:
	ldr	r5, .L1286
	movw	r4, #65535
	ldr	r3, .L1286+76
	ldrh	r3, [r5, r3]
	cmp	r3, r4
	beq	.L1281
	ldr	r0, .L1286+80
	bl	make_superblock
.L1281:
	ldr	r3, .L1286+44
	ldrh	r3, [r5, r3]
	cmp	r3, r4
	beq	.L1285
	ldr	r0, .L1286+84
	bl	make_superblock
	mov	r0, #0
	ldmfd	sp!, {r3, r4, r5, r6, r7, pc}
.L1284:
	mvn	r0, #0
	ldmfd	sp!, {r3, r4, r5, r6, r7, pc}
.L1285:
	mov	r0, #0
	ldmfd	sp!, {r3, r4, r5, r6, r7, pc}
.L1287:
	.align	2
.L1286:
	.word	.LANCHOR2
	.word	.LANCHOR0
	.word	-1768
	.word	-1766
	.word	.LANCHOR2+176
	.word	1179929683
	.word	.LANCHOR2-1820
	.word	-1812
	.word	-1772
	.word	-2042
	.word	-1754
	.word	-1756
	.word	-1802
	.word	-1994
	.word	-1798
	.word	-1946
	.word	.LANCHOR2-2044
	.word	-1996
	.word	.LANCHOR2-1996
	.word	-1948
	.word	.LANCHOR2-1948
	.word	.LANCHOR2-1756
	.fnend
	.size	FtlLoadSysInfo, .-FtlLoadSysInfo
	.align	2
	.global	FtlLoadBbt
	.type	FtlLoadBbt, %function
FtlLoadBbt:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	stmfd	sp!, {r4, r5, r6, r7, r8, lr}
	.save {r4, r5, r6, r7, r8, lr}
	mov	r3, #0
	ldr	r6, .L1304
	ldr	r8, .L1304+4
	ldr	r4, [r6, #-940]
	mov	r7, r6
	str	r3, [r6, #184]
	str	r4, [r6, #188]
	bl	FtlBbtMemInit
	mov	r3, #3904
	ldrh	r5, [r8, r3]
	sub	r5, r5, #1
	uxth	r5, r5
	b	.L1289
.L1293:
	mov	r1, #1
	mov	r3, r5, asl #10
	ldr	r0, .L1304+8
	mov	r2, r1
	str	r3, [r6, #180]
	bl	FlashReadPages
	ldr	r3, [r6, #176]
	cmn	r3, #1
	bne	.L1290
	ldr	r3, [r6, #180]
	mov	r1, #1
	ldr	r0, .L1304+8
	mov	r2, r1
	add	r3, r3, #1
	str	r3, [r6, #180]
	bl	FlashReadPages
.L1290:
	ldr	r3, [r7, #176]
	cmn	r3, #1
	beq	.L1291
	ldrh	r2, [r4, #0]
	movw	r3, #61649
	cmp	r2, r3
	bne	.L1291
	ldr	r3, .L1304+4
	movw	r2, #3976
	strh	r5, [r3, r2]	@ movhi
	ldr	r2, [r4, #4]
	str	r2, [r3, #3984]
	movw	r2, #3980
	ldrh	r1, [r4, #8]
	strh	r1, [r3, r2]	@ movhi
	b	.L1292
.L1291:
	sub	r5, r5, #1
	uxth	r5, r5
.L1289:
	mov	r3, #3904
	ldrh	r3, [r8, r3]
	sub	r3, r3, #48
	cmp	r5, r3
	bgt	.L1293
.L1292:
	ldr	r5, .L1304+4
	movw	r8, #3976
	movw	r3, #65535
	ldrh	r2, [r5, r8]
	cmp	r2, r3
	beq	.L1303
	movw	r6, #3980
	ldrh	r2, [r5, r6]
	cmp	r2, r3
	beq	.L1295
	ldr	r7, .L1304
	mov	r1, #1
	mov	r2, r2, asl #10
	add	r0, r7, #176
	str	r2, [r7, #180]
	mov	r2, r1
	bl	FlashReadPages
	ldr	r3, [r7, #176]
	cmn	r3, #1
	beq	.L1295
	ldrh	r2, [r4, #0]
	movw	r3, #61649
	cmp	r2, r3
	bne	.L1295
	ldr	r3, [r4, #4]
	ldr	r2, [r5, #3984]
	cmp	r3, r2
	strhi	r3, [r5, #3984]
	ldrhih	r2, [r5, r6]
	ldrhih	r3, [r4, #8]
	strhih	r2, [r5, r8]	@ movhi
	strhih	r3, [r5, r6]	@ movhi
.L1295:
	ldr	r8, .L1304+4
	movw	r3, #3976
	mov	r1, #1
	mov	r6, #0
	ldr	r5, .L1304
	ldrh	r0, [r8, r3]
	bl	FtlGetLastWrittenPage
	movw	r3, #3978
	add	r2, r0, #1
	mov	r7, r0
	strh	r2, [r8, r3]	@ movhi
	b	.L1296
.L1299:
	movw	r2, #3976
	mov	r1, #1
	ldrh	r2, [r8, r2]
	ldr	r0, .L1304+8
	orr	r3, r3, r2, asl #10
	str	r3, [r5, #180]
	ldr	r3, [r5, #-2076]
	mov	r2, r1
	str	r3, [r5, #184]
	bl	FlashReadPages
	ldr	r3, [r5, #176]
	cmn	r3, #1
	beq	.L1297
	ldrh	r2, [r4, #0]
	movw	r3, #61649
	cmp	r2, r3
	beq	.L1298
.L1297:
	sub	r6, r6, #1
	uxth	r6, r6
.L1296:
	add	r3, r6, r7
	sxth	r3, r3
	cmp	r3, #0
	bge	.L1299
.L1298:
	ldr	r3, .L1304+4
	movw	r2, #3982
	ldrh	r1, [r4, #10]
	ldrh	r0, [r4, #12]
	strh	r1, [r3, r2]	@ movhi
	movw	r2, #65535
	cmp	r0, r2
	beq	.L1300
	ldr	r2, [r3, #3836]
	cmp	r0, r2
	beq	.L1300
	movw	r1, #3850
	ldrh	r3, [r3, r1]
	mov	r3, r3, lsr #2
	cmp	r2, r3
	bcs	.L1300
	cmp	r0, r3
	bcs	.L1300
	bl	FtlSysBlkNumInit
.L1300:
	ldr	r5, .L1304+12
	mov	r4, #0
	movw	r7, #3862
	ldr	r6, .L1304
	sub	r8, r5, #4000
	b	.L1301
.L1302:
	ldr	r3, .L1304+16
	ldr	r1, [r6, #184]
	ldr	r0, [r5, #4]!
	ldrh	r2, [r6, r3]
	mov	r2, r2, asl #2
	mla	r1, r4, r2, r1
	bl	memcpy
	add	r4, r4, #1
.L1301:
	ldrh	r3, [r8, r7]
	cmp	r4, r3
	bcc	.L1302
	mov	r0, #0
	ldmfd	sp!, {r4, r5, r6, r7, r8, pc}
.L1303:
	mvn	r0, #0
	ldmfd	sp!, {r4, r5, r6, r7, r8, pc}
.L1305:
	.align	2
.L1304:
	.word	.LANCHOR2
	.word	.LANCHOR0
	.word	.LANCHOR2+176
	.word	.LANCHOR0+4000
	.word	-1024
	.fnend
	.size	FtlLoadBbt, .-FtlLoadBbt
	.align	2
	.global	FtlLoadFactoryBbt
	.type	FtlLoadFactoryBbt, %function
FtlLoadFactoryBbt:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	stmfd	sp!, {r3, r4, r5, r6, r7, r8, r9, sl, fp, lr}
	.save {r3, r4, r5, r6, r7, r8, r9, sl, fp, lr}
	mov	r5, #0
	ldr	r3, .L1313
	movw	fp, #3862
	ldr	r7, .L1313+4
	ldr	r6, .L1313+8
	ldr	r2, [r3, #-2076]
	ldr	r8, [r3, #-940]
	str	r2, [r3, #184]
	str	r8, [r3, #188]
	b	.L1307
.L1312:
	mvn	r3, #0
	strh	r3, [r7], #2	@ movhi
	mov	r3, #3904
	ldr	sl, .L1313
	ldrh	r4, [r6, r3]
	movw	r9, #61664
	sub	r4, r4, #1
	uxth	r4, r4
	b	.L1308
.L1311:
	mla	r3, r3, r5, r4
	mov	r1, #1
	ldr	r0, .L1313+12
	mov	r2, r1
	mov	r3, r3, asl #10
	str	r3, [sl, #180]
	bl	FlashReadPages
	ldr	r3, [sl, #176]
	cmn	r3, #1
	beq	.L1309
	ldrh	r3, [r8, #0]
	cmp	r3, r9
	streqh	r4, [r7, #-2]	@ movhi
	beq	.L1310
.L1309:
	sub	r4, r4, #1
	uxth	r4, r4
.L1308:
	mov	r3, #3904
	ldrh	r3, [r6, r3]
	sub	r2, r3, #16
	cmp	r4, r2
	bgt	.L1311
.L1310:
	add	r5, r5, #1
.L1307:
	ldrh	r3, [r6, fp]
	cmp	r5, r3
	bcc	.L1312
	mov	r0, #0
	ldmfd	sp!, {r3, r4, r5, r6, r7, r8, r9, sl, fp, pc}
.L1314:
	.align	2
.L1313:
	.word	.LANCHOR2
	.word	.LANCHOR0+3988
	.word	.LANCHOR0
	.word	.LANCHOR2+176
	.fnend
	.size	FtlLoadFactoryBbt, .-FtlLoadFactoryBbt
	.align	2
	.global	FlashProgSlc2KPages
	.type	FlashProgSlc2KPages, %function
FlashProgSlc2KPages:
	.fnstart
	@ args = 0, pretend = 0, frame = 56
	@ frame_needed = 0, uses_anonymous_args = 0
	stmfd	sp!, {r4, r5, r6, r7, r8, r9, sl, fp, lr}
	.save {r4, r5, r6, r7, r8, r9, sl, fp, lr}
	.pad #68
	sub	sp, sp, #68
	mov	sl, r1
	mov	r9, r2
	str	r3, [sp, #12]
	mov	r4, r0
	ldr	r3, .L1334
	mov	r6, r0
	mov	r8, #0
	ldr	r7, .L1334+4
	ldrb	fp, [r3, #2837]	@ zero_extendqisi2
	b	.L1316
.L1323:
	rsb	r3, r8, sl
	add	r2, sp, #20
	mov	r0, r6
	mov	r1, r9
	uxtb	r3, r3
	str	r3, [sp, #0]
	add	r3, sp, #24
	bl	LogAddr2PhyAddr
	ldr	r3, [sp, #24]
	ldrb	r2, [r7, #3762]	@ zero_extendqisi2
	cmp	r3, r2
	mvncs	r3, #0
	strcs	r3, [r6, #0]
	bcs	.L1318
	add	r3, r7, r3
	ldrb	r5, [r3, #3764]	@ zero_extendqisi2
	mov	r0, r5
	bl	NandcWaitFlashReady
	mov	r0, r5
	bl	NandcFlashCs
	mov	r0, r5
	ldr	r1, [sp, #20]
	bl	FlashProgFirstCmd
	ldr	r3, [r6, #12]
	mov	r2, fp
	mov	r1, #1
	mov	r0, r5
	str	r3, [sp, #0]
	ldr	r3, [r6, #8]
	bl	NandcXferData
	ldr	r1, [sp, #20]
	mov	r0, r5
	bl	FlashProgSecondCmd
	mov	r0, r5
	bl	NandcWaitFlashReady
	ldr	r1, [sp, #20]
	mov	r0, r5
	bl	FlashReadStatus
	ldr	r3, [sp, #20]
	ands	r0, r0, #1
	mvnne	r0, #0
	str	r0, [r6, #0]
	ldr	r1, [r7, #856]
	mov	r0, r5
	add	r1, r1, r3
	bl	FlashProgFirstCmd
	ldr	r3, [r6, #8]
	ldr	r2, [r6, #12]
	mov	r1, #1
	cmp	r3, #0
	mov	r0, r5
	addne	r3, r3, #2048
	cmp	r2, #0
	addne	r2, r2, #8
	str	r2, [sp, #0]
	mov	r2, fp
	bl	NandcXferData
	ldr	r3, [sp, #20]
	ldr	r1, [r7, #856]
	mov	r0, r5
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
	strne	r3, [r6, #0]
	bl	NandcFlashDeCs
.L1318:
	add	r8, r8, #1
	add	r6, r6, #36
.L1316:
	cmp	r8, sl
	bne	.L1323
	ldr	r3, [sp, #12]
	cmp	r3, #0
	movne	r6, #0
	ldrne	r5, .L1334+8
	bne	.L1324
	b	.L1325
.L1330:
	ldr	r3, [r4, #0]
	cmn	r3, #1
	bne	.L1326
	ldr	r1, [r4, #4]
	ldr	r0, .L1334+12
	bl	printk
	b	.L1327
.L1326:
	rsb	r3, r6, sl
	mov	r1, r9
	add	r2, sp, #20
	mov	r0, r4
	uxtb	r3, r3
	str	r3, [sp, #0]
	add	r3, sp, #24
	bl	LogAddr2PhyAddr
	ldr	r2, [r5, #216]
	mov	r3, #0
	mov	lr, r4
	add	ip, sp, #28
	str	r3, [r2, #0]
	ldr	r2, [r5, #220]
	str	r3, [r2, #0]
	ldmia	lr!, {r0, r1, r2, r3}
	stmia	ip!, {r0, r1, r2, r3}
	ldmia	lr!, {r0, r1, r2, r3}
	stmia	ip!, {r0, r1, r2, r3}
	add	r0, sp, #28
	ldr	r3, [lr, #0]
	mov	r1, #1
	mov	r2, r9
	str	r3, [ip, #0]
	ldr	r3, [r5, #216]
	str	r3, [sp, #36]
	ldr	r3, [r5, #220]
	str	r3, [sp, #40]
	bl	FlashReadPages
	ldr	r7, [sp, #28]
	cmn	r7, #1
	bne	.L1328
	ldr	r0, .L1334+16
	ldr	r1, [r4, #4]
	bl	printk
	str	r7, [r4, #0]
.L1328:
	ldr	r3, [r4, #12]
	cmp	r3, #0
	beq	.L1329
	ldr	r2, [r3, #0]
	ldr	r3, [r5, #220]
	ldr	r3, [r3, #0]
	cmp	r2, r3
	beq	.L1329
	ldr	r0, .L1334+20
	ldr	r1, [r4, #4]
	bl	printk
	mvn	r3, #0
	str	r3, [r4, #0]
.L1329:
	ldr	r3, [r4, #8]
	cmp	r3, #0
	beq	.L1327
	ldr	r2, [r3, #0]
	ldr	r3, [r5, #216]
	ldr	r3, [r3, #0]
	cmp	r2, r3
	beq	.L1327
	ldr	r0, .L1334+24
	ldr	r1, [r4, #4]
	bl	printk
	mvn	r3, #0
	str	r3, [r4, #0]
.L1327:
	add	r6, r6, #1
	add	r4, r4, #36
.L1324:
	cmp	r6, sl
	bne	.L1330
.L1325:
	mov	r0, #0
	add	sp, sp, #68
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, sl, fp, pc}
.L1335:
	.align	2
.L1334:
	.word	.LANCHOR1
	.word	.LANCHOR0
	.word	.LANCHOR2
	.word	.LC98
	.word	.LC99
	.word	.LC100
	.word	.LC101
	.fnend
	.size	FlashProgSlc2KPages, .-FlashProgSlc2KPages
	.align	2
	.global	FlashProgPages
	.type	FlashProgPages, %function
FlashProgPages:
	.fnstart
	@ args = 0, pretend = 0, frame = 64
	@ frame_needed = 0, uses_anonymous_args = 0
	stmfd	sp!, {r4, r5, r6, r7, r8, r9, sl, fp, lr}
	.save {r4, r5, r6, r7, r8, r9, sl, fp, lr}
	.pad #76
	sub	sp, sp, #76
	ldr	r5, .L1364
	mov	r9, r0
	str	r1, [sp, #8]
	mov	sl, r2
	str	r3, [sp, #16]
	ldr	ip, [r5, #3624]
	ldrb	r6, [r5, #852]	@ zero_extendqisi2
	ldrb	ip, [ip, #19]	@ zero_extendqisi2
	cmp	r6, #0
	moveq	fp, r5
	str	ip, [sp, #20]
	ldr	ip, .L1364+4
	ldrb	ip, [ip, #2837]	@ zero_extendqisi2
	str	ip, [sp, #12]
	beq	.L1337
	bl	FlashProgSlc2KPages
	b	.L1338
.L1351:
	mov	r4, #36
	ldr	r2, [sp, #8]
	mul	r4, r4, r6
	rsb	r3, r6, r2
	mov	r1, sl
	add	r2, sp, #28
	uxtb	r3, r3
	str	r3, [sp, #0]
	add	r7, r9, r4
	add	r3, sp, #32
	mov	r0, r7
	bl	LogAddr2PhyAddr
	ldrb	r3, [r5, #3762]	@ zero_extendqisi2
	ldr	r2, [sp, #32]
	cmp	r2, r3
	mvncs	r3, #0
	strcs	r3, [r9, r4]
	mov	r8, r0
	bcs	.L1340
	ldrb	r1, [r5, #3833]	@ zero_extendqisi2
	add	r2, fp, r2, asl #4
	cmp	r1, #0
	ldr	r2, [r2, #3636]
	moveq	r8, #0
	cmp	r2, #0
	beq	.L1342
	cmp	r3, #1
	bne	.L1343
	ldr	r0, [r5, #3012]
	bl	NandcIqrWaitFlashReady
.L1343:
	ldrb	r0, [sp, #32]	@ zero_extendqisi2
	bl	FlashWaitCmdDone
.L1342:
	ldr	r2, [sp, #32]
	mov	r1, #0
	cmp	r8, #0
	add	r3, r5, r2, asl #4
	movne	r0, #36
	str	r1, [r3, #3640]
	ldr	r1, [sp, #28]
	str	r7, [r3, #3636]
	str	r1, [r3, #3632]
	addne	r1, r6, #1
	mlane	r1, r0, r1, r9
	strne	r1, [r3, #3640]
	add	r3, fp, r2
	add	r2, fp, r2, asl #4
	ldrb	r4, [r3, #3764]	@ zero_extendqisi2
	ldrb	r3, [fp, #3762]	@ zero_extendqisi2
	cmp	r3, #1
	strb	r4, [r2, #3628]
	mov	r0, r4
	bne	.L1345
	bl	NandcWaitFlashReady
	b	.L1346
.L1345:
	bl	NandcFlashCs
	ldr	r3, [sp, #32]
	mov	r0, r4
	ldr	r1, [sp, #28]
	add	r3, r5, r3, asl #2
	ldr	r2, [r3, #3588]
	adds	r2, r2, #0
	movne	r2, #1
	bl	FlashWaitReadyEN
	mov	r0, r4
	bl	NandcFlashDeCs
.L1346:
	ldr	r2, [sp, #20]
	sub	r3, r2, #1
	cmp	r3, #6
	bhi	.L1347
	add	r3, r5, r4
	ldrb	r3, [r3, #3756]	@ zero_extendqisi2
	cmp	r3, #0
	beq	.L1347
	mov	r0, r4
	ldrb	r1, [r5, #1]	@ zero_extendqisi2
	ldr	r2, .L1364+8
	mov	r3, #0
	bl	HynixSetRRPara
.L1347:
	mov	r0, r4
	bl	NandcFlashCs
	cmp	sl, #1
	mov	r0, r4
	bne	.L1348
	ldrb	r3, [r5, #928]	@ zero_extendqisi2
	cmp	r3, #0
	beq	.L1348
	bl	flash_enter_slc_mode
	b	.L1349
.L1348:
	bl	flash_exit_slc_mode
.L1349:
	mov	r0, r4
	ldr	r1, [sp, #28]
	bl	FlashProgFirstCmd
	ldr	r3, [r7, #12]
	mov	r0, r4
	mov	r1, #1
	ldr	r2, [sp, #12]
	str	r3, [sp, #0]
	ldr	r3, [r7, #8]
	bl	NandcXferData
	cmp	r8, #0
	beq	.L1350
	mov	r0, r4
	ldr	r1, [sp, #28]
	bl	FlashProgDpFirstCmd
	ldr	r3, [sp, #32]
	ldr	r1, [sp, #28]
	mov	r0, r4
	add	r3, r5, r3, asl #2
	ldr	r2, [r3, #3588]
	adds	r2, r2, #0
	movne	r2, #1
	bl	FlashWaitReadyEN
	ldr	r3, [sp, #28]
	ldr	r1, [r5, #856]
	mov	r0, r4
	add	r1, r1, r3
	bl	FlashProgDpSecondCmd
	add	r3, r6, #1
	mov	r2, #36
	mov	r0, r4
	mla	r3, r2, r3, r9
	mov	r1, #1
	ldr	r2, [r3, #12]
	str	r2, [sp, #0]
	ldr	r2, [sp, #12]
	ldr	r3, [r3, #8]
	bl	NandcXferData
.L1350:
	mov	r0, r4
	ldr	r1, [sp, #28]
	bl	FlashProgSecondCmd
	mov	r0, r4
	bl	NandcFlashDeCs
	add	r6, r6, r8
.L1340:
	add	r6, r6, #1
.L1337:
	ldr	r3, [sp, #8]
	cmp	r6, r3
	bcc	.L1351
	ldr	r5, .L1364
	mov	r4, #0
	ldr	r6, .L1364+12
	ldr	r0, [r5, #3012]
	bl	NandcIqrWaitFlashReady
	b	.L1352
.L1354:
	uxtb	r0, r4
	bl	FlashWaitCmdDone
	cmp	sl, #1
	bne	.L1353
	ldrb	r3, [r5, #928]	@ zero_extendqisi2
	cmp	r3, #0
	beq	.L1353
	ldrb	r0, [r6, r4, asl #4]	@ zero_extendqisi2
	bl	flash_exit_slc_mode
.L1353:
	add	r4, r4, #1
.L1352:
	ldrb	r3, [r5, #3762]	@ zero_extendqisi2
	cmp	r4, r3
	bcc	.L1354
	ldr	r2, [sp, #16]
	cmp	r2, #0
	ldreq	r0, [sp, #16]
	beq	.L1338
	mov	r5, #0
	ldr	r4, .L1364+16
	ldr	r6, [sp, #8]
	b	.L1355
.L1360:
	ldr	r3, [r9, #0]
	cmn	r3, #1
	bne	.L1356
	ldr	r1, [r9, #4]
	ldr	r0, .L1364+20
	bl	printk
	b	.L1357
.L1356:
	rsb	r3, r5, r6
	mov	r1, sl
	add	r2, sp, #28
	mov	r0, r9
	uxtb	r3, r3
	str	r3, [sp, #0]
	add	r3, sp, #32
	bl	LogAddr2PhyAddr
	ldr	r2, [r4, #216]
	mov	r3, #0
	mov	lr, r9
	add	ip, sp, #36
	str	r3, [r2, #0]
	ldr	r2, [r4, #220]
	str	r3, [r2, #0]
	ldmia	lr!, {r0, r1, r2, r3}
	stmia	ip!, {r0, r1, r2, r3}
	ldmia	lr!, {r0, r1, r2, r3}
	stmia	ip!, {r0, r1, r2, r3}
	add	r0, sp, #36
	ldr	r3, [lr, #0]
	mov	r1, #1
	mov	r2, sl
	str	r3, [ip, #0]
	ldr	r3, [r4, #216]
	str	r3, [sp, #44]
	ldr	r3, [r4, #220]
	str	r3, [sp, #48]
	bl	FlashReadPages
	ldr	r7, [sp, #36]
	cmn	r7, #1
	bne	.L1358
	ldr	r0, .L1364+24
	ldr	r1, [r9, #4]
	bl	printk
	str	r7, [r9, #0]
.L1358:
	ldr	r3, [r9, #12]
	cmp	r3, #0
	beq	.L1359
	ldr	r2, [r3, #0]
	ldr	r3, [r4, #220]
	ldr	r3, [r3, #0]
	cmp	r2, r3
	beq	.L1359
	ldr	r0, .L1364+28
	ldr	r1, [r9, #4]
	bl	printk
	mvn	r3, #0
	str	r3, [r9, #0]
.L1359:
	ldr	r3, [r9, #8]
	cmp	r3, #0
	beq	.L1357
	ldr	r2, [r3, #0]
	ldr	r3, [r4, #216]
	ldr	r3, [r3, #0]
	cmp	r2, r3
	beq	.L1357
	ldr	r0, .L1364+32
	ldr	r1, [r9, #4]
	bl	printk
	mvn	r3, #0
	str	r3, [r9, #0]
.L1357:
	add	r5, r5, #1
	add	r9, r9, #36
.L1355:
	cmp	r5, r6
	bne	.L1360
	mov	r0, #0
.L1338:
	add	sp, sp, #76
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, sl, fp, pc}
.L1365:
	.align	2
.L1364:
	.word	.LANCHOR0
	.word	.LANCHOR1
	.word	.LANCHOR0+4
	.word	.LANCHOR0+3628
	.word	.LANCHOR2
	.word	.LC98
	.word	.LC99
	.word	.LC100
	.word	.LC101
	.fnend
	.size	FlashProgPages, .-FlashProgPages
	.align	2
	.type	FtlVpcTblFlush.part.14, %function
FtlVpcTblFlush.part.14:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	stmfd	sp!, {r3, r4, r5, r6, r7, r8, sl, lr}
	.save {r3, r4, r5, r6, r7, r8, sl, lr}
	mov	r1, #255
	ldr	r4, .L1378
	movw	r7, #3848
	ldr	r6, .L1378+4
	ldr	r3, [r4, #-2076]
	ldr	r5, [r4, #-940]
	str	r3, [r4, #184]
	ldr	r3, .L1378+8
	str	r5, [r4, #188]
	ldrh	r3, [r4, r3]
	strh	r3, [r5, #2]	@ movhi
	ldr	r3, .L1378+12
	strh	r3, [r5, #0]	@ movhi
	ldr	r3, [r4, #-1760]
	str	r3, [r5, #4]
	mov	r3, #0
	str	r3, [r5, #8]
	str	r3, [r5, #12]
	ldr	r3, .L1378+16
	str	r3, [r4, #-1820]
	ldr	r3, .L1378+20
	str	r3, [r4, #-1816]
	ldr	r3, .L1378+24
	ldrh	r2, [r4, r3]
	sub	r3, r3, #50
	strh	r2, [r4, r3]	@ movhi
	movw	r3, #3862
	ldrh	r3, [r6, r3]
	strb	r3, [r4, #-1810]
	ldr	r3, .L1378+28
	ldrh	r2, [r4, r3]
	add	r3, r3, #238
	strh	r2, [r4, r3]	@ movhi
	sub	r3, r3, #236
	ldrh	r2, [r4, r3]
	ldrb	r3, [r4, #-2038]	@ zero_extendqisi2
	orr	r2, r3, r2, asl #6
	ldr	r3, .L1378+32
	strh	r2, [r4, r3]	@ movhi
	ldrb	r3, [r4, #-2036]	@ zero_extendqisi2
	strb	r3, [r4, #-1809]
	ldr	r3, .L1378+36
	ldrh	r2, [r4, r3]
	add	r3, r3, #194
	strh	r2, [r4, r3]	@ movhi
	sub	r3, r3, #192
	ldrh	r2, [r4, r3]
	ldrb	r3, [r4, #-1990]	@ zero_extendqisi2
	orr	r2, r3, r2, asl #6
	ldr	r3, .L1378+40
	strh	r2, [r4, r3]	@ movhi
	ldrb	r3, [r4, #-1988]	@ zero_extendqisi2
	strb	r3, [r4, #-1808]
	ldr	r3, .L1378+44
	ldrh	r2, [r4, r3]
	add	r3, r3, #150
	strh	r2, [r4, r3]	@ movhi
	sub	r3, r3, #148
	ldr	r0, [r4, #184]
	ldrh	r2, [r4, r3]
	ldrb	r3, [r4, #-1942]	@ zero_extendqisi2
	orr	r2, r3, r2, asl #6
	ldr	r3, .L1378+48
	strh	r2, [r4, r3]	@ movhi
	ldrb	r3, [r4, #-1940]	@ zero_extendqisi2
	strb	r3, [r4, #-1807]
	ldr	r3, [r4, #-1840]
	str	r3, [r4, #-1788]
	ldr	r3, [r4, #-1848]
	str	r3, [r4, #-1780]
	ldr	r3, [r4, #-1844]
	str	r3, [r4, #-1784]
	movw	r3, #3918
	ldrh	r2, [r6, r3]
	bl	ftl_memset
	ldr	r1, .L1378+52
	mov	r2, #48
	ldr	r0, [r4, #184]
	bl	memcpy
	ldrh	r2, [r6, r7]
	ldr	r0, [r4, #184]
	ldr	r1, [r4, #-2064]
	mov	r2, r2, asl #1
	add	r0, r0, #48
	bl	memcpy
	ldrh	r2, [r6, r7]
	ldr	r3, [r4, #184]
	add	r0, r2, #24
	ldr	r1, [r4, #-1888]
	mov	r2, r2, lsr #3
	mov	r0, r0, lsr #1
	add	r2, r2, #4
	add	r0, r3, r0, asl #2
	bl	memcpy
	mov	r3, #3952
	ldrh	r3, [r6, r3]
	cmp	r3, #0
	beq	.L1367
	ldrh	r3, [r6, r7]
	ldr	r1, [r4, #184]
	mov	r0, r3, lsr #3
	add	r0, r0, r3, asl #1
	movw	r3, #3944
	add	r0, r0, #52
	ldrh	r2, [r6, r3]
	ubfx	r0, r0, #2, #14
	mov	r2, r2, asl #2
	add	r0, r1, r0, asl #2
	ldr	r1, [r4, #-1028]
	bl	memcpy
.L1367:
	mov	r0, #0
	ldr	r4, .L1378
	bl	FtlUpdateVaildLpn
	ldr	r8, .L1378+8
	mov	r6, #0
	movw	sl, #65535
.L1377:
	ldr	r3, [r4, #-2076]
	mov	r1, #1
	ldr	r7, .L1378+56
	ldrh	r2, [r4, r8]
	str	r3, [r4, #184]
	ldr	r3, [r4, #-940]
	ldr	r0, .L1378+60
	str	r3, [r4, #188]
	ldrh	r3, [r4, r7]
	orr	r3, r3, r2, asl #10
	mov	r2, r1
	str	r3, [r4, #180]
	mov	r3, r1
	bl	FlashProgPages
	ldr	r1, .L1378+4
	movw	r3, #3910
	ldrh	r2, [r4, r7]
	ldrh	r3, [r1, r3]
	sub	r3, r3, #1
	cmp	r2, r3
	blt	.L1369
	ldr	r3, .L1378+64
	ldrh	r2, [r4, r8]
	ldrh	sl, [r4, r3]
	strh	r2, [r4, r3]	@ movhi
	mov	r3, #0
	strh	r3, [r4, r7]	@ movhi
	bl	FtlFreeSysBlkQueueOut
	ldr	r3, [r4, #-1848]
	mov	r1, #1
	add	r2, r3, #1
	str	r2, [r4, #-1848]
	str	r3, [r4, #-1760]
	mov	r2, r0, asl #10
	strh	r0, [r4, r8]	@ movhi
	str	r2, [r4, #180]
	mov	r2, r1
	str	r3, [r5, #4]
	mov	r3, r1
	strh	r0, [r5, #2]	@ movhi
	ldr	r0, .L1378+60
	bl	FlashProgPages
.L1369:
	ldr	r2, .L1378+56
	ldr	r1, [r4, #176]
	ldrh	r3, [r4, r2]
	cmn	r1, #1
	add	r3, r3, #1
	uxth	r3, r3
	strh	r3, [r4, r2]	@ movhi
	bne	.L1370
	cmp	r3, #1
	add	r6, r6, #1
	ldreq	r1, .L1378+4
	movweq	r3, #3910
	uxth	r6, r6
	ldreqh	r3, [r1, r3]
	subeq	r3, r3, #1
	streqh	r3, [r4, r2]	@ movhi
	cmp	r6, #3
	bls	.L1377
	ldr	r4, .L1378
	mov	r2, r6
	ldr	r0, .L1378+68
	ldr	r1, [r4, #180]
	bl	printk
	mov	r3, #1
	str	r3, [r4, #-2092]
	ldmfd	sp!, {r3, r4, r5, r6, r7, r8, sl, pc}
.L1370:
	cmp	r3, #1
	beq	.L1377
	cmp	r1, #256
	beq	.L1377
	movw	r3, #65535
	cmp	sl, r3
	ldmeqfd	sp!, {r3, r4, r5, r6, r7, r8, sl, pc}
	mov	r0, sl
	mov	r1, #1
	bl	FtlFreeSysBlkQueueIn
	ldmfd	sp!, {r3, r4, r5, r6, r7, r8, sl, pc}
.L1379:
	.align	2
.L1378:
	.word	.LANCHOR2
	.word	.LANCHOR0
	.word	-1768
	.word	-3932
	.word	1179929683
	.word	1342177351
	.word	-1762
	.word	-2044
	.word	-1804
	.word	-1996
	.word	-1800
	.word	-1948
	.word	-1796
	.word	.LANCHOR2-1820
	.word	-1766
	.word	.LANCHOR2+176
	.word	-1764
	.word	.LC102
	.fnend
	.size	FtlVpcTblFlush.part.14, .-FtlVpcTblFlush.part.14
	.align	2
	.global	FtlVpcTblFlush
	.type	FtlVpcTblFlush, %function
FtlVpcTblFlush:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	stmfd	sp!, {r3, lr}
	.save {r3, lr}
	ldr	r3, .L1382
	ldr	r3, [r3, #-2092]
	cmp	r3, #0
	bne	.L1381
	bl	FtlVpcTblFlush.part.14
.L1381:
	mov	r0, #0
	ldmfd	sp!, {r3, pc}
.L1383:
	.align	2
.L1382:
	.word	.LANCHOR2
	.fnend
	.size	FtlVpcTblFlush, .-FtlVpcTblFlush
	.section	.text.unlikely,"ax",%progbits
	.align	2
	.type	FtlBbmTblFlush.part.17, %function
FtlBbmTblFlush.part.17:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	stmfd	sp!, {r0, r1, r2, r4, r5, r6, r7, r8, r9, sl, fp, lr}
	.save {r0, r1, r2, r4, r5, r6, r7, r8, r9, sl, fp, lr}
	mov	r1, #0
	ldr	r7, .L1397
	mov	r6, #0
	ldr	sl, .L1397+4
	movw	r9, #3862
	ldr	r3, [r7, #-940]
	add	r8, sl, #4000
	ldr	r0, [r7, #-2076]
	str	r3, [r7, #188]
	movw	r3, #3918
	str	r0, [r7, #184]
	ldrh	r2, [sl, r3]
	bl	ftl_memset
	b	.L1385
.L1386:
	ldr	r3, .L1397+8
	ldr	r1, [r8, #4]!
	ldrh	r2, [r7, r3]
	ldr	r3, [r7, #184]
	mul	r0, r2, r6
	mov	r2, r2, asl #2
	add	r6, r6, #1
	add	r0, r3, r0, asl #2
	bl	memcpy
.L1385:
	ldrh	r3, [sl, r9]
	ldr	r4, .L1397+4
	cmp	r6, r3
	ldr	r5, .L1397
	blt	.L1386
	ldr	r6, [r5, #188]
	mov	r1, #255
	mov	r2, #16
	mov	r7, #0
	mov	fp, r7
	mov	r0, r6
	bl	ftl_memset
	ldr	r3, .L1397+12
	strh	r3, [r6, #0]	@ movhi
	ldr	r3, [r4, #3984]
	str	r3, [r6, #4]
	movw	r3, #3976
	ldrh	r3, [r4, r3]
	strh	r3, [r6, #2]	@ movhi
	movw	r3, #3980
	ldrh	r3, [r4, r3]
	strh	r3, [r6, #8]	@ movhi
	movw	r3, #3982
	ldrh	r3, [r4, r3]
	strh	r3, [r6, #10]	@ movhi
	ldr	r3, [r4, #3836]
	strh	r3, [r6, #12]	@ movhi
	b	.L1395
.L1392:
	mov	fp, #1
.L1395:
	ldr	r3, [r5, #-2076]
	movw	r8, #3976
	ldr	r4, .L1397+4
	mov	r1, #0
	movw	sl, #3978
	str	r1, [r5, #176]
	str	r3, [r5, #184]
	movw	r9, #3980
	ldr	r3, [r5, #-940]
	ldrh	r1, [r4, r8]
	ldrh	r2, [r4, sl]
	str	r3, [r5, #188]
	ldrh	r0, [r6, #10]
	orr	r3, r2, r1, asl #10
	str	r3, [r5, #180]
	ldrh	r3, [r4, r9]
	str	r0, [sp, #0]
	ldr	r0, .L1397+16
	bl	printk
	mov	r1, #1
	mov	r2, r1
	mov	r3, r1
	ldr	r0, .L1397+20
	bl	FlashProgPages
	movw	r3, #3910
	ldrh	r3, [r4, r3]
	ldrh	r2, [r4, sl]
	sub	r3, r3, #1
	cmp	r2, r3
	blt	.L1388
	ldr	r3, [r4, #3984]
	mov	r1, #0	@ movhi
	ldrh	r2, [r4, r8]
	add	r3, r3, #1
	ldr	r0, [r5, #-2088]
	str	r3, [r4, #3984]
	str	r3, [r6, #4]
	ldrh	r3, [r4, r9]
	strh	r1, [r4, sl]	@ movhi
	mov	r1, #1
	strh	r2, [r6, #8]	@ movhi
	strh	r2, [r4, r9]	@ movhi
	mov	r2, r1
	strh	r3, [r4, r8]	@ movhi
	mov	r3, r3, asl #10
	str	r3, [r5, #180]
	str	r3, [r0, #4]
	bl	FlashEraseBlocks
	mov	r1, #1
	ldr	r0, .L1397+20
	mov	r2, r1
	mov	r3, r1
	bl	FlashProgPages
.L1388:
	ldr	r2, .L1397+4
	movw	r3, #3978
	ldr	r4, .L1397
	ldrh	r1, [r2, r3]
	add	r1, r1, #1
	strh	r1, [r2, r3]	@ movhi
	ldr	r3, [r5, #176]
	cmn	r3, #1
	bne	.L1389
	add	r7, r7, #1
	ldr	r0, .L1397+24
	ldr	r1, [r5, #180]
	uxth	r7, r7
	bl	printk
	cmp	r7, #3
	bls	.L1395
	ldr	r0, .L1397+28
	mov	r2, r7
	ldr	r1, [r4, #180]
	bl	printk
	mov	r3, #1
	str	r3, [r4, #-2092]
	b	.L1396
.L1389:
	cmp	fp, #0
	beq	.L1392
.L1396:
	ldmfd	sp!, {r1, r2, r3, r4, r5, r6, r7, r8, r9, sl, fp, pc}
.L1398:
	.align	2
.L1397:
	.word	.LANCHOR2
	.word	.LANCHOR0
	.word	-1024
	.word	-3887
	.word	.LC103
	.word	.LANCHOR2+176
	.word	.LC104
	.word	.LC105
	.fnend
	.size	FtlBbmTblFlush.part.17, .-FtlBbmTblFlush.part.17
	.text
	.align	2
	.global	FtlBbmTblFlush
	.type	FtlBbmTblFlush, %function
FtlBbmTblFlush:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	stmfd	sp!, {r3, lr}
	.save {r3, lr}
	ldr	r3, .L1401
	ldr	r3, [r3, #-2092]
	cmp	r3, #0
	bne	.L1400
	bl	FtlBbmTblFlush.part.17
.L1400:
	mov	r0, #0
	ldmfd	sp!, {r3, pc}
.L1402:
	.align	2
.L1401:
	.word	.LANCHOR2
	.fnend
	.size	FtlBbmTblFlush, .-FtlBbmTblFlush
	.align	2
	.global	FtlGcFreeBadSuperBlk
	.type	FtlGcFreeBadSuperBlk, %function
FtlGcFreeBadSuperBlk:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	stmfd	sp!, {r4, r5, r6, r7, r8, r9, sl, lr}
	.save {r4, r5, r6, r7, r8, r9, sl, lr}
	mov	r9, r0
	ldr	r4, .L1413
	ldr	r3, .L1413+4
	ldrh	r3, [r4, r3]
	cmp	r3, #0
	movne	r6, #0
	movne	sl, r4
	bne	.L1404
	b	.L1405
.L1411:
	add	r3, r3, r6
	mov	r1, r9
	mov	r5, #0
	ldr	r7, .L1413+4
	ldrb	r0, [r3, #3866]	@ zero_extendqisi2
	bl	V2P_block
	mov	r8, r0
	b	.L1406
.L1410:
	add	r3, r4, r5, asl #1
	sub	r3, r3, #1120
	sub	r3, r3, #12
	ldrh	r3, [r3, #0]
	cmp	r3, r8
	bne	.L1407
	mov	r1, r8
	ldr	r0, .L1413+8
	bl	printk
	mov	r0, r8
	bl	FtlBbmMapBadBlock
	bl	FtlBbmTblFlush
	ldrh	r1, [r4, r7]
	mov	r3, r5
	b	.L1408
.L1409:
	add	r0, r3, #1
	add	r3, r4, r3, asl #1
	sub	r3, r3, #1120
	add	r2, r4, r0, asl #1
	sub	r2, r2, #1120
	sub	r2, r2, #12
	ldrh	r2, [r2, #0]
	strh	r2, [r3, #-12]	@ movhi
	uxth	r3, r0
.L1408:
	cmp	r3, r1
	bcc	.L1409
	sub	r1, r1, #1
	strh	r1, [sl, r7]	@ movhi
.L1407:
	add	r5, r5, #1
	uxth	r5, r5
.L1406:
	ldrh	r3, [r4, r7]
	cmp	r3, r5
	bhi	.L1410
	add	r6, r6, #1
	uxth	r6, r6
.L1404:
	ldr	r3, .L1413+12
	mov	r2, #3840
	ldrh	r2, [r3, r2]
	cmp	r2, r6
	bhi	.L1411
	bl	FtlGcReFreshBadBlk
.L1405:
	mov	r0, #0
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, sl, pc}
.L1414:
	.align	2
.L1413:
	.word	.LANCHOR2
	.word	-1134
	.word	.LC106
	.word	.LANCHOR0
	.fnend
	.size	FtlGcFreeBadSuperBlk, .-FtlGcFreeBadSuperBlk
	.align	2
	.global	update_vpc_list
	.type	update_vpc_list, %function
update_vpc_list:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	stmfd	sp!, {r3, r4, r5, lr}
	.save {r3, r4, r5, lr}
	mov	r2, r0, asl #1
	ldr	r3, .L1423
	mov	r4, r0
	ldr	r1, [r3, #-2064]
	ldrh	r2, [r1, r2]
	cmp	r2, #0
	bne	.L1416
	ldr	r1, .L1423+4
	ldrh	r0, [r3, r1]
	cmp	r0, r4
	mvneq	r2, #0
	streqh	r2, [r3, r1]	@ movhi
	beq	.L1418
	ldr	r1, .L1423+8
	ldrh	r1, [r3, r1]
	cmp	r1, r4
	beq	.L1419
	ldr	r1, .L1423+12
	ldrh	r1, [r3, r1]
	cmp	r1, r4
	beq	.L1419
	ldr	r1, .L1423+16
	ldrh	r3, [r3, r1]
	cmp	r3, r4
	beq	.L1419
.L1418:
	mov	r1, r4
	ldr	r0, .L1423+20
	bl	List_remove_node
	ldr	r5, .L1423
	ldr	r3, .L1423+24
	mov	r0, r4
	ldrh	r2, [r5, r3]
	sub	r2, r2, #1
	strh	r2, [r5, r3]	@ movhi
	bl	free_data_superblock
	mov	r0, r4
	bl	FtlGcFreeBadSuperBlk
	mov	r2, #1
	b	.L1419
.L1416:
	bl	List_update_data_list
	mov	r2, #0
.L1419:
	mov	r0, r2
	ldmfd	sp!, {r3, r4, r5, pc}
.L1424:
	.align	2
.L1423:
	.word	.LANCHOR2
	.word	-1756
	.word	-2044
	.word	-1996
	.word	-1948
	.word	.LANCHOR2-2068
	.word	-2056
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
	mov	r5, r0
	beq	.L1426
	ldr	r2, .L1431
	mov	r3, r0, asl #1
	ldr	r2, [r2, #-2064]
	ldrh	r4, [r2, r3]
	cmp	r4, #0
	subne	r4, r4, #1
	strneh	r4, [r2, r3]	@ movhi
	bne	.L1426
	ldr	r0, .L1431+4
	mov	r1, r5
	mov	r2, r4
	bl	printk
	mov	r0, r4
	ldmfd	sp!, {r3, r4, r5, r6, r7, pc}
.L1426:
	ldr	r7, .L1431
	movw	r3, #65535
	ldr	r6, .L1431+8
	ldrh	r0, [r7, r6]
	cmp	r0, r3
	streqh	r5, [r7, r6]	@ movhi
	moveq	r0, #0
	ldmeqfd	sp!, {r3, r4, r5, r6, r7, pc}
	cmp	r0, r5
	beq	.L1430
	bl	update_vpc_list
	strh	r5, [r7, r6]	@ movhi
	adds	r0, r0, #0
	movne	r0, #1
	ldmfd	sp!, {r3, r4, r5, r6, r7, pc}
.L1430:
	mov	r0, #0
	ldmfd	sp!, {r3, r4, r5, r6, r7, pc}
.L1432:
	.align	2
.L1431:
	.word	.LANCHOR2
	.word	.LC107
	.word	-1016
	.fnend
	.size	decrement_vpc_count, .-decrement_vpc_count
	.align	2
	.global	get_new_active_ppa
	.type	get_new_active_ppa, %function
get_new_active_ppa:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	stmfd	sp!, {r3, r4, r5, r6, r7, lr}
	.save {r3, r4, r5, r6, r7, lr}
	mov	r3, #0
	strb	r3, [r0, #10]
	mov	r4, r0
	ldrb	r3, [r0, #6]	@ zero_extendqisi2
	movw	r6, #65535
	ldr	r5, .L1445
	ldr	r7, .L1445+4
	add	r3, r0, r3, asl #1
	ldrh	r3, [r3, #16]
	b	.L1434
.L1435:
	ldrb	r3, [r4, #6]	@ zero_extendqisi2
	mov	r2, #3840
	ldrh	r2, [r5, r2]
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
.L1434:
	cmp	r3, r6
	beq	.L1435
	ldrb	r2, [r4, #8]	@ zero_extendqisi2
	cmp	r2, #1
	bne	.L1437
	ldrb	r2, [r5, #928]	@ zero_extendqisi2
	cmp	r2, #0
	bne	.L1437
	ldrh	r2, [r4, #2]
	add	r2, r7, r2, asl #1
	sub	r2, r2, #896
	ldrh	r2, [r2, #0]
	cmp	r2, r6
	bne	.L1437
	ldrh	r3, [r4, #4]
	ldrh	r0, [r4, #0]
	sub	r3, r3, #1
	strh	r3, [r4, #4]	@ movhi
	bl	decrement_vpc_count
	b	.L1435
.L1437:
	ldrh	r6, [r4, #2]
	movw	r5, #65535
	mov	r7, r5
	orr	r6, r6, r3, asl #10
	ldrh	r3, [r4, #4]
	sub	r3, r3, #1
	strh	r3, [r4, #4]	@ movhi
.L1442:
	ldr	r2, .L1445
	mov	r3, #3840
	ldrh	r2, [r2, r3]
	ldrb	r3, [r4, #6]	@ zero_extendqisi2
.L1439:
	add	r3, r3, #1
	uxtb	r3, r3
	cmp	r3, r2
	ldreqh	r3, [r4, #2]
	addeq	r3, r3, #1
	streqh	r3, [r4, #2]	@ movhi
	moveq	r3, #0
	add	r1, r4, r3, asl #1
	ldrh	r1, [r1, #16]
	cmp	r1, r5
	beq	.L1439
	strb	r3, [r4, #6]
	ldrb	r3, [r4, #8]	@ zero_extendqisi2
	cmp	r3, #1
	bne	.L1440
	ldr	r3, .L1445
	ldrb	r2, [r3, #928]	@ zero_extendqisi2
	cmp	r2, #0
	ldrh	r2, [r4, #2]
	bne	.L1443
	ldr	r3, .L1445+4
	add	r2, r3, r2, asl #1
	sub	r2, r2, #896
	ldrh	r3, [r2, #0]
	cmp	r3, r7
	bne	.L1440
	ldrh	r3, [r4, #4]
	cmp	r3, #0
	beq	.L1440
	sub	r3, r3, #1
	ldrh	r0, [r4, #0]
	strh	r3, [r4, #4]	@ movhi
	bl	decrement_vpc_count
	b	.L1442
.L1443:
	movw	r1, #3910
	ldrh	r1, [r3, r1]
	cmp	r2, r1
	bcc	.L1440
	ldr	r1, .L1445+4
	ldrh	r2, [r4, #0]
	ldrh	r0, [r4, #4]
	ldr	r1, [r1, #-2064]
	mov	r2, r2, asl #1
	ldrh	ip, [r1, r2]
	rsb	r0, r0, ip
	strh	r0, [r1, r2]	@ movhi
	mov	r2, #0
	strh	r2, [r4, #4]	@ movhi
	mov	r1, r2	@ movhi
	movw	r2, #3908
	ldrh	r3, [r3, r2]
	strb	r1, [r4, #6]
	strh	r3, [r4, #2]	@ movhi
.L1440:
	mov	r0, r6
	ldmfd	sp!, {r3, r4, r5, r6, r7, pc}
.L1446:
	.align	2
.L1445:
	.word	.LANCHOR0
	.word	.LANCHOR2
	.fnend
	.size	get_new_active_ppa, .-get_new_active_ppa
	.align	2
	.type	FtlSlcSuperblockCheck.part.20, %function
FtlSlcSuperblockCheck.part.20:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	stmfd	sp!, {r3, r4, r5, r6, r7, lr}
	.save {r3, r4, r5, r6, r7, lr}
	mov	r4, r0
	ldrb	r3, [r0, #6]	@ zero_extendqisi2
	movw	r6, #65535
	ldr	r5, .L1453
	ldr	r7, .L1453+4
	add	r3, r0, r3, asl #1
	ldrh	r3, [r3, #16]
	b	.L1448
.L1449:
	ldrb	r3, [r4, #6]	@ zero_extendqisi2
	mov	r2, #3840
	ldrh	r2, [r5, r2]
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
.L1448:
	cmp	r3, r6
	beq	.L1449
	ldrb	r2, [r4, #8]	@ zero_extendqisi2
	cmp	r2, #1
	bne	.L1451
	ldrb	r3, [r5, #928]	@ zero_extendqisi2
	cmp	r3, #0
	bne	.L1451
	ldrh	r3, [r4, #2]
	add	r3, r7, r3, asl #1
	sub	r3, r3, #896
	ldrh	r3, [r3, #0]
	cmp	r3, r6
	bne	.L1451
	ldrh	r3, [r4, #4]
	ldrh	r0, [r4, #0]
	sub	r3, r3, #1
	strh	r3, [r4, #4]	@ movhi
	bl	decrement_vpc_count
	ldrh	r3, [r4, #4]
	cmp	r3, #0
	bne	.L1449
	ldrh	r2, [r4, #2]
	strb	r3, [r4, #6]
	add	r2, r2, #1
	strh	r2, [r4, #2]	@ movhi
	ldmfd	sp!, {r3, r4, r5, r6, r7, pc}
.L1451:
	ldr	r3, .L1453
	ldrb	r1, [r3, #928]	@ zero_extendqisi2
	cmp	r1, #0
	ldmeqfd	sp!, {r3, r4, r5, r6, r7, pc}
	cmp	r2, #1
	ldmnefd	sp!, {r3, r4, r5, r6, r7, pc}
	movw	r2, #3910
	ldrh	r1, [r4, #2]
	ldrh	r2, [r3, r2]
	cmp	r1, r2
	ldmccfd	sp!, {r3, r4, r5, r6, r7, pc}
	ldr	r1, .L1453+4
	ldrh	r2, [r4, #0]
	ldrh	r0, [r4, #4]
	ldr	r1, [r1, #-2064]
	mov	r2, r2, asl #1
	ldrh	ip, [r1, r2]
	rsb	r0, r0, ip
	strh	r0, [r1, r2]	@ movhi
	mov	r2, #0
	strh	r2, [r4, #4]	@ movhi
	mov	r1, r2	@ movhi
	movw	r2, #3908
	ldrh	r3, [r3, r2]
	strb	r1, [r4, #6]
	strh	r3, [r4, #2]	@ movhi
	ldmfd	sp!, {r3, r4, r5, r6, r7, pc}
.L1454:
	.align	2
.L1453:
	.word	.LANCHOR0
	.word	.LANCHOR2
	.fnend
	.size	FtlSlcSuperblockCheck.part.20, .-FtlSlcSuperblockCheck.part.20
	.align	2
	.global	FtlSlcSuperblockCheck
	.type	FtlSlcSuperblockCheck, %function
FtlSlcSuperblockCheck:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	@ link register save eliminated.
	ldrh	r2, [r0, #4]
	cmp	r2, #0
	bxeq	lr
	ldrh	r2, [r0, #0]
	movw	r3, #65535
	cmp	r2, r3
	bxeq	lr
	b	FtlSlcSuperblockCheck.part.20
	.fnend
	.size	FtlSlcSuperblockCheck, .-FtlSlcSuperblockCheck
	.align	2
	.type	allocate_data_superblock.part.21, %function
allocate_data_superblock.part.21:
	.fnstart
	@ args = 0, pretend = 0, frame = 16
	@ frame_needed = 0, uses_anonymous_args = 0
	stmfd	sp!, {r4, r5, r6, r7, r8, r9, sl, fp, lr}
	.save {r4, r5, r6, r7, r8, r9, sl, fp, lr}
	.pad #20
	sub	sp, sp, #20
	ldr	r4, .L1493
	mov	r5, r0
	mov	r8, r4
.L1489:
	ldr	r3, .L1493+4
	cmp	r5, r3
	bne	.L1459
	ldr	r3, .L1493+8
	ldrh	r2, [r4, r3]
	ldr	r3, [r4, #-1180]
	mov	r1, r2, lsr #1
	mul	ip, r3, r2
	add	r0, r1, #1
	add	r0, r0, ip, lsr #2
	ldr	ip, [r4, #-1884]
	cmp	ip, #0
	uxth	r0, r0
	beq	.L1483
	ldr	ip, [r4, #-1824]
	cmp	ip, #29
	bhi	.L1483
	cmp	ip, #2
	bls	.L1488
	tst	r2, #1
	beq	.L1460
	cmp	r3, #0
	moveq	r1, r3
	beq	.L1461
	b	.L1460
.L1459:
	ldrb	r3, [r5, #8]	@ zero_extendqisi2
	cmp	r3, #1
	bne	.L1488
	ldr	r3, .L1493+12
	movw	r2, #3860
	ldrh	r2, [r3, r2]
	cmp	r2, #1
	beq	.L1488
	ldrb	r3, [r3, #928]	@ zero_extendqisi2
	cmp	r3, #0
	bne	.L1488
	sub	r3, r3, #2048
	ldr	r2, [r4, #-1884]
	ldrh	r3, [r4, r3]
	cmp	r2, #0
	mov	r1, r3, lsr #3
	beq	.L1460
	ldr	r2, [r4, #-1824]
	cmp	r2, #1
	movls	r1, #7
	mulls	r1, r1, r3
	movls	r1, r1, lsr #3
	b	.L1460
.L1483:
	mov	r1, r0
.L1460:
	cmp	r1, #0
	subne	r1, r1, #1
	uxthne	r1, r1
	b	.L1461
.L1488:
	mov	r1, #0
.L1461:
	ldrb	r2, [r5, #8]	@ zero_extendqisi2
	ldr	r0, .L1493+16
	bl	List_pop_index_node
	ldr	r3, .L1493+8
	ldrh	r2, [r4, r3]
	sub	r2, r2, #1
	strh	r2, [r4, r3]	@ movhi
	uxth	r7, r0
	mov	r0, r5
	strh	r7, [r5, #0]	@ movhi
	bl	make_superblock
	ldrb	r3, [r5, #7]	@ zero_extendqisi2
	cmp	r3, #0
	beq	.L1462
	ldr	r2, .L1493+12
	mov	r3, #3840
	ldr	ip, [r4, #-2088]
	mov	r6, #0
	mov	sl, r5
	mov	r1, r5
	ldrh	r9, [r2, r3]
	mov	r0, r6
	mov	r3, ip
	mov	r2, r6
	stmia	sp, {r5, ip}
	b	.L1463
.L1462:
	ldr	r3, [r4, #-2064]
	mov	r7, r7, asl #1
	mvn	r2, #0
	strh	r2, [r3, r7]	@ movhi
	b	.L1489
.L1466:
	str	r0, [r3, #8]
	movw	r5, #65535
	str	r0, [r3, #12]
	ldrh	lr, [r1, #16]
	cmp	lr, r5
	beq	.L1465
	ldr	r5, [sp, #4]
	mov	ip, #36
	mov	lr, lr, asl #10
	mla	fp, ip, r6, r5
	add	r6, r6, #1
	uxth	r6, r6
	str	lr, [fp, #4]
.L1465:
	add	r2, r2, #1
	add	r3, r3, #36
	add	r1, r1, #2
	uxth	r2, r2
.L1463:
	cmp	r2, r9
	bne	.L1466
	ldr	r3, [r4, #-1884]
	ldr	r5, [sp, #0]
	cmp	r3, #0
	beq	.L1467
	ldr	r3, .L1493+20
	cmp	r5, r3
	bne	.L1467
	ldr	r2, [r4, #-2084]
	mov	r3, r7, asl #1
	ldrh	r3, [r2, r3]
	cmp	r3, #30
	movhi	r3, #0
	strhib	r3, [r4, #-2036]
.L1467:
	ldrb	r3, [r5, #8]	@ zero_extendqisi2
	ldr	r2, [r4, #-2084]
	cmp	r3, #0
	mov	r3, r7, asl #1
	ldrh	r1, [r2, r3]
	bne	.L1468
	cmp	r1, #0
	ldrne	ip, .L1493+12
	movwne	r0, #3898
	moveq	r1, #2
	ldrneh	r0, [ip, r0]
	addne	r1, r1, r0
	strh	r1, [r2, r3]	@ movhi
	ldr	r3, [r4, #-1840]
	mov	r0, r7
	mov	r1, #0
	add	r3, r3, #1
	str	r3, [r4, #-1840]
	b	.L1491
.L1468:
	add	r1, r1, #1
	strh	r1, [r2, r3]	@ movhi
	ldr	r3, [r4, #-1836]
	mov	r0, r7
	mov	r1, #1
	add	r3, r3, #1
	str	r3, [r4, #-1836]
.L1491:
	bl	ftl_set_blk_mode
	ldr	r3, [r4, #-2084]
	mov	r9, r7, asl #1
	ldr	r2, [r4, #-1828]
	ldr	r0, [r8, #-1840]
	ldrh	r3, [r3, r9]
	cmp	r3, r2
	movw	r2, #3898
	strhi	r3, [r4, #-1828]
	ldr	r3, .L1493+12
	ldrh	r1, [r3, r2]
	ldr	r2, [r8, #-1836]
	mla	r0, r0, r1, r2
	movw	r2, #3848
	ldrh	r1, [r3, r2]
	bl	__aeabi_uidiv
	ldr	r3, [r8, #-924]
	ldr	r2, [r3, #16]
	add	r2, r2, #1
	str	r2, [r3, #16]
	ldr	r3, [r8, #-2088]
	mov	r2, #0
	str	r0, [r8, #-1832]
	b	.L1473
.L1474:
	add	r2, r2, #1
	ldr	r1, [r3, #-32]
	uxth	r2, r2
	bic	r1, r1, #1020
	bic	r1, r1, #3
	str	r1, [r3, #-32]
.L1473:
	cmp	r2, r6
	add	r3, r3, #36
	bne	.L1474
	ldr	r3, .L1493+12
	ldrb	r3, [r3, #928]	@ zero_extendqisi2
	cmp	r3, #0
	beq	.L1475
	ldrb	r3, [r5, #8]	@ zero_extendqisi2
	mov	r2, r6
	ldr	r0, [r4, #-2088]
	cmp	r3, #1
	moveq	r1, #0
	movne	r1, #1
	bl	FlashEraseBlocks
.L1475:
	mov	r2, r6
	ldr	r0, [r4, #-2088]
	ldrb	r1, [r5, #8]	@ zero_extendqisi2
	mov	fp, #0
	bl	FlashEraseBlocks
	mov	r2, #36
	mul	r2, r2, r6
	mov	r3, r7
	mov	ip, r9
	mov	r6, fp
	mov	r7, r5
	mov	r9, r2
	b	.L1477
.L1479:
	ldr	r1, [r4, #-2088]
	add	r0, r1, fp
	ldr	r5, [r1, fp]
	cmn	r5, #1
	bne	.L1478
	ldr	r0, [r0, #4]
	add	r6, r6, #1
	str	r3, [sp, #12]
	ubfx	r0, r0, #10, #16
	str	ip, [sp, #8]
	bl	FtlBbmMapBadBlock
	strh	r5, [sl, #16]	@ movhi
	ldrb	r1, [r7, #7]	@ zero_extendqisi2
	sub	r1, r1, #1
	strb	r1, [r7, #7]
	ldr	ip, [sp, #8]
	ldr	r3, [sp, #12]
.L1478:
	add	fp, fp, #36
	add	sl, sl, #2
.L1477:
	cmp	fp, r9
	bne	.L1479
	cmp	r6, #0
	mov	r5, r7
	mov	r9, ip
	mov	r7, r3
	beq	.L1480
	mov	r0, r3
	bl	update_multiplier_value
	bl	FtlBbmTblFlush
.L1480:
	ldrb	r3, [r5, #7]	@ zero_extendqisi2
	cmp	r3, #0
	ldreq	r3, [r8, #-2064]
	mvneq	r2, #0
	streqh	r2, [r3, r9]	@ movhi
	beq	.L1489
.L1481:
	ldr	r1, .L1493+12
	movw	r2, #3908
	ldrh	r2, [r1, r2]
	strh	r7, [r5, #0]	@ movhi
	mul	r2, r2, r3
	mov	r3, #0
	strh	r3, [r5, #2]	@ movhi
	strb	r3, [r5, #6]
	ldr	r3, .L1493
	uxth	r2, r2
	strh	r2, [r5, #4]	@ movhi
	ldr	r1, [r3, #-1848]
	str	r1, [r5, #12]
	add	r1, r1, #1
	str	r1, [r3, #-1848]
	ldrh	r0, [r5, #0]
	ldr	r1, [r3, #-2064]
	mov	r3, r0, asl #1
	strh	r2, [r1, r3]	@ movhi
	add	sp, sp, #20
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, sl, fp, pc}
.L1494:
	.align	2
.L1493:
	.word	.LANCHOR2
	.word	.LANCHOR2-1948
	.word	-2048
	.word	.LANCHOR0
	.word	.LANCHOR2-2052
	.word	.LANCHOR2-2044
	.fnend
	.size	allocate_data_superblock.part.21, .-allocate_data_superblock.part.21
	.align	2
	.global	allocate_data_superblock
	.type	allocate_data_superblock, %function
allocate_data_superblock:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	stmfd	sp!, {r3, lr}
	.save {r3, lr}
	ldr	r3, .L1497
	ldr	r3, [r3, #-2092]
	cmp	r3, #0
	bne	.L1496
	bl	allocate_data_superblock.part.21
.L1496:
	mov	r0, #0
	ldmfd	sp!, {r3, pc}
.L1498:
	.align	2
.L1497:
	.word	.LANCHOR2
	.fnend
	.size	allocate_data_superblock, .-allocate_data_superblock
	.align	2
	.global	FtlSuperblockPowerLostFix
	.type	FtlSuperblockPowerLostFix, %function
FtlSuperblockPowerLostFix:
	.fnstart
	@ args = 0, pretend = 0, frame = 40
	@ frame_needed = 0, uses_anonymous_args = 0
	ldr	r3, .L1508
	stmfd	sp!, {r4, r5, r6, r7, r8, lr}
	.save {r4, r5, r6, r7, r8, lr}
	mov	r4, r0
	ldrb	r7, [r3, #928]	@ zero_extendqisi2
	.pad #40
	sub	sp, sp, #40
	cmp	r7, #0
	beq	.L1507
	ldrb	r7, [r0, #8]	@ zero_extendqisi2
	cmp	r7, #1
	ldreqh	r6, [r0, #4]
	beq	.L1500
	mov	r7, #0
.L1507:
	mov	r6, #12
.L1500:
	mvn	r8, #0
	ldr	r5, .L1508+4
	b	.L1501
.L1504:
	ldrh	r3, [r4, #4]
	cmp	r3, #0
	beq	.L1502
	mov	r0, r4
	bl	get_new_active_ppa
	cmn	r0, #1
	str	r0, [sp, #8]
	beq	.L1502
	ldr	r2, [r5, #-940]
	add	r0, sp, #4
	ldr	r3, [r5, #-2076]
	sub	r6, r6, #1
	str	r8, [sp, #20]
	str	r2, [sp, #16]
	str	r3, [sp, #12]
	str	r8, [r2, #12]
	str	r8, [r2, #8]
	ldrh	r3, [r4, #0]
	strh	r3, [r2, #2]	@ movhi
	mov	r3, #0
	strh	r3, [r2, #0]	@ movhi
	ldr	r1, [r5, #-1844]
	cmn	r1, #2
	str	r1, [r2, #4]
	addne	r1, r1, #1
	moveq	r1, #0
	mov	r2, r7
	str	r1, [r5, #-1844]
	mov	r1, #1
	bl	FlashProgPages
	ldrh	r0, [r4, #0]
	bl	decrement_vpc_count
.L1501:
	cmp	r6, #0
	bne	.L1504
.L1502:
	ldr	r2, .L1508+4
	ldrh	r3, [r4, #0]
	ldrh	r1, [r4, #4]
	ldr	r2, [r2, #-2064]
	mov	r3, r3, asl #1
	ldrh	r0, [r2, r3]
	rsb	r1, r1, r0
	strh	r1, [r2, r3]	@ movhi
	ldr	r2, .L1508
	movw	r3, #3908
	ldrh	r3, [r2, r3]
	strh	r3, [r4, #2]	@ movhi
	mov	r3, #0
	strb	r3, [r4, #6]
	strh	r3, [r4, #4]	@ movhi
	add	sp, sp, #40
	ldmfd	sp!, {r4, r5, r6, r7, r8, pc}
.L1509:
	.align	2
.L1508:
	.word	.LANCHOR0
	.word	.LANCHOR2
	.fnend
	.size	FtlSuperblockPowerLostFix, .-FtlSuperblockPowerLostFix
	.align	2
	.type	FtlLowFormatEraseBlock.part.24, %function
FtlLowFormatEraseBlock.part.24:
	.fnstart
	@ args = 0, pretend = 0, frame = 16
	@ frame_needed = 0, uses_anonymous_args = 0
	stmfd	sp!, {r4, r5, r6, r7, r8, r9, sl, fp, lr}
	.save {r4, r5, r6, r7, r8, r9, sl, fp, lr}
	.pad #20
	sub	sp, sp, #20
	ldr	r8, .L1540
	mov	r6, #0
	mov	r9, r0
	mov	sl, r1
	mov	r4, r6
	mov	r5, r6
	ldrb	r2, [r8, #928]	@ zero_extendqisi2
	mov	fp, #36
	str	r0, [r8, #3972]
	ldr	r7, .L1540+4
	str	r2, [sp, #8]
	b	.L1511
.L1515:
	mul	r3, fp, r6
	ldr	r2, [r7, #-2088]
	mov	r0, #0
	mov	r1, r9
	str	r0, [r2, r3]
	add	r3, r8, r6
	ldrb	r0, [r3, #3866]	@ zero_extendqisi2
	bl	V2P_block
	cmp	sl, #0
	str	r0, [sp, #4]
	beq	.L1512
	bl	IsBlkInVendorPart
	cmp	r0, #0
	bne	.L1513
.L1512:
	ldr	r0, [sp, #4]
	bl	FtlBbmIsBadBlock
	cmp	r0, #0
	addne	r5, r5, #1
	uxthne	r5, r5
	bne	.L1513
	ldr	r3, [r7, #-2088]
	ldr	r1, [sp, #4]
	mla	r3, fp, r4, r3
	mov	r2, r1, asl #10
	str	r0, [r3, #8]
	mov	r0, #3920
	str	r2, [r3, #4]
	ldrh	r2, [r8, r0]
	mul	r2, r2, r4
	add	r4, r4, #1
	uxth	r4, r4
	add	r1, r2, #3
	cmp	r2, #0
	movlt	r2, r1
	ldr	r1, [r7, #-936]
	bic	r2, r2, #3
	add	r2, r1, r2
	str	r2, [r3, #12]
.L1513:
	add	r6, r6, #1
	uxth	r6, r6
.L1511:
	mov	r1, #3840
	ldrh	r3, [r8, r1]
	cmp	r3, r6
	bhi	.L1515
	cmp	r4, #0
	beq	.L1517
	ldr	r2, [sp, #8]
	mov	r8, #0
	ldr	r7, .L1540+4
	mov	fp, r8
	adds	r6, r2, #0
	mov	r2, r4
	movne	r6, #1
	ldr	r0, [r7, #-2088]
	mov	r1, r6
	bl	FlashEraseBlocks
.L1519:
	ldr	r3, [r7, #-2088]
	add	r2, r3, r8
	ldr	r3, [r3, r8]
	cmn	r3, #1
	bne	.L1518
	ldr	r0, [r2, #4]
	add	r5, r5, #1
	ubfx	r0, r0, #10, #16
	uxth	r5, r5
	bl	FtlBbmMapBadBlock
.L1518:
	add	fp, fp, #1
	add	r8, r8, #36
	uxth	fp, fp
	cmp	fp, r4
	bne	.L1519
	cmp	sl, #0
	beq	.L1532
	ldr	r3, .L1540
	movw	r2, #3910
	ldrh	r2, [r3, r2]
	ldrb	r3, [r3, #928]	@ zero_extendqisi2
	cmp	r3, #0
	str	r2, [sp, #4]
	moveq	r3, r2, lsr #2
	streq	r3, [sp, #8]
	moveq	r6, #1
	beq	.L1520
	b	.L1539
.L1532:
	mov	r0, #1
	mov	r1, #6
	stmib	sp, {r0, r1}
	b	.L1520
.L1539:
	mov	r6, #1
	str	r6, [sp, #8]
.L1520:
	mov	r8, #0
	mov	r7, r6
.L1528:
	mov	r6, #0
	ldr	fp, .L1540+4
	mov	r4, r6
	b	.L1521
.L1524:
	mov	r0, #36
	ldr	r2, [fp, #-2088]
	mul	r3, r0, r6
	mov	r1, #0
	str	r1, [r2, r3]
	mov	r1, r9
	ldr	r2, .L1540
	add	r3, r2, r6
	ldrb	r0, [r3, #3866]	@ zero_extendqisi2
	bl	V2P_block
	cmp	sl, #0
	str	r0, [sp, #12]
	beq	.L1522
	bl	IsBlkInVendorPart
	cmp	r0, #0
	bne	.L1523
.L1522:
	ldr	r0, [sp, #12]
	bl	FtlBbmIsBadBlock
	cmp	r0, #0
	bne	.L1523
	ldr	r3, [fp, #-2088]
	mov	r0, #36
	ldr	r1, [sp, #12]
	mla	r3, r0, r4, r3
	add	r2, r8, r1, asl #10
	ldr	r1, .L1540
	mov	r0, #3920
	str	r2, [r3, #4]
	ldr	r2, [fp, #-948]
	str	r2, [r3, #8]
	ldrh	r2, [r1, r0]
	mul	r2, r2, r4
	add	r4, r4, #1
	uxth	r4, r4
	add	r1, r2, #3
	cmp	r2, #0
	movlt	r2, r1
	ldr	r1, [fp, #-944]
	bic	r2, r2, #3
	add	r2, r1, r2
	str	r2, [r3, #12]
.L1523:
	add	r6, r6, #1
	uxth	r6, r6
.L1521:
	ldr	r0, .L1540
	mov	r2, #3840
	ldrh	r3, [r0, r2]
	cmp	r3, r6
	bhi	.L1524
	cmp	r4, #0
	beq	.L1517
	ldr	r6, .L1540+4
	mov	r1, r4
	mov	r2, r7
	mov	r3, #1
	mov	fp, #0
	ldr	r0, [r6, #-2088]
	bl	FlashProgPages
	mov	ip, sl
	mov	sl, r7
	mov	r7, r4
	mov	r4, fp
.L1527:
	ldr	r2, [r6, #-2088]
	add	r1, r2, fp
	ldr	r2, [r2, fp]
	cmp	r2, #0
	beq	.L1526
	ldr	r0, [r1, #4]
	add	r5, r5, #1
	str	ip, [sp, #0]
	ubfx	r0, r0, #10, #16
	uxth	r5, r5
	bl	FtlBbmMapBadBlock
	ldr	ip, [sp, #0]
.L1526:
	add	r4, r4, #1
	add	fp, fp, #36
	uxth	r4, r4
	cmp	r4, r7
	bne	.L1527
	ldr	r1, [sp, #8]
	mov	r4, r7
	ldr	r2, [sp, #4]
	mov	r7, sl
	add	r8, r8, r1
	mov	sl, ip
	uxth	r8, r8
	cmp	r8, r2
	bcc	.L1528
	ldr	fp, .L1540+4
	mov	r6, r7
	mov	r7, #0
	mov	r8, r7
.L1530:
	cmp	sl, #0
	beq	.L1529
	ldr	r3, [fp, #-2088]
	add	r2, r3, r7
	ldr	r3, [r3, r7]
	cmp	r3, #0
	bne	.L1529
	ldr	r0, [r2, #4]
	mov	r1, #1
	ubfx	r0, r0, #10, #16
	bl	FtlFreeSysBlkQueueIn
.L1529:
	add	r8, r8, #1
	add	r7, r7, #36
	uxth	r8, r8
	cmp	r8, r4
	bne	.L1530
	cmp	r9, #63
	movhi	r9, #0
	movls	r9, #1
	cmp	sl, #0
	moveq	sl, r9
	orrne	sl, r9, #1
	cmp	sl, #0
	beq	.L1517
	ldr	r3, .L1540+4
	mov	r1, r6
	mov	r2, r8
	ldr	r0, [r3, #-2088]
	bl	FlashEraseBlocks
.L1517:
	mov	r0, r5
	add	sp, sp, #20
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, sl, fp, pc}
.L1541:
	.align	2
.L1540:
	.word	.LANCHOR0
	.word	.LANCHOR2
	.fnend
	.size	FtlLowFormatEraseBlock.part.24, .-FtlLowFormatEraseBlock.part.24
	.align	2
	.global	FtlLowFormatEraseBlock
	.type	FtlLowFormatEraseBlock, %function
FtlLowFormatEraseBlock:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	@ link register save eliminated.
	ldr	r3, .L1544
	ldr	r3, [r3, #-2092]
	cmp	r3, #0
	bne	.L1543
	b	FtlLowFormatEraseBlock.part.24
.L1543:
	mov	r0, #0
	bx	lr
.L1545:
	.align	2
.L1544:
	.word	.LANCHOR2
	.fnend
	.size	FtlLowFormatEraseBlock, .-FtlLowFormatEraseBlock
	.align	2
	.type	FlashTestBlk.part.25, %function
FlashTestBlk.part.25:
	.fnstart
	@ args = 0, pretend = 0, frame = 104
	@ frame_needed = 0, uses_anonymous_args = 0
.L1547:
	stmfd	sp!, {r4, r5, lr}
	.save {r4, r5, lr}
	.pad #108
	sub	sp, sp, #108
	ldr	r4, .L1549
	mov	r5, r0
	mov	r1, #165
	add	r0, sp, #40
	mov	r2, #32
	str	r0, [sp, #16]
	ldr	r3, [r4, #132]
	mov	r5, r5, asl #10
	str	r3, [sp, #12]
	bl	ftl_memset
	ldr	r0, [r4, #132]
	mov	r1, #90
	mov	r2, #8
	bl	ftl_memset
	mov	r1, #1
	mov	r2, r1
	add	r0, sp, #4
	str	r5, [sp, #8]
	bl	FlashEraseBlocks
	mov	r1, #1
	add	r0, sp, #4
	mov	r3, r1
	mov	r2, r1
	bl	FlashProgPages
	ldr	r4, [sp, #4]
	add	r0, sp, #4
	mov	r1, #0
	cmp	r4, #0
	mov	r2, #1
	mvnne	r4, #0
	bl	FlashEraseBlocks
	mov	r0, r4
	add	sp, sp, #108
	ldmfd	sp!, {r4, r5, pc}
.L1550:
	.align	2
.L1549:
	.word	.LANCHOR2
	.fnend
	.size	FlashTestBlk.part.25, .-FlashTestBlk.part.25
	.align	2
	.global	FlashTestBlk
	.type	FlashTestBlk, %function
FlashTestBlk:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	@ link register save eliminated.
	ldr	r3, .L1553
	ldr	r3, [r3, #152]
	cmp	r0, r3
	bcc	.L1552
	b	FlashTestBlk.part.25
.L1552:
	mov	r0, #0
	bx	lr
.L1554:
	.align	2
.L1553:
	.word	.LANCHOR2
	.fnend
	.size	FlashTestBlk, .-FlashTestBlk
	.align	2
	.global	FlashMakeFactorBbt
	.type	FlashMakeFactorBbt, %function
FlashMakeFactorBbt:
	.fnstart
	@ args = 0, pretend = 0, frame = 72
	@ frame_needed = 0, uses_anonymous_args = 0
	ldr	r3, .L1583
	movw	r1, #3062
	movw	r2, #3060
	stmfd	sp!, {r4, r5, r6, r7, r8, r9, sl, fp, lr}
	.save {r4, r5, r6, r7, r8, r9, sl, fp, lr}
	.pad #76
	sub	sp, sp, #76
	ldrh	r2, [r3, r2]
	mov	r5, #0
	ldrh	fp, [r3, r1]
	mov	r1, #1
	ldr	r4, .L1583+4
	mul	fp, fp, r2
	ldr	r2, [r3, #3624]
	ldr	r0, [r4, #224]
	ldrb	r2, [r2, #24]	@ zero_extendqisi2
	str	r0, [sp, #28]
	ldr	r0, .L1583+8
	uxth	fp, fp
	str	r2, [sp, #12]
	ldr	r2, [r3, #856]
	ldrb	r3, [r3, #852]	@ zero_extendqisi2
	uxth	r2, r2
	cmp	r3, #1
	str	r2, [sp, #8]
	moveq	r3, r2, asl #1
	uxtheq	r3, r3
	streq	r3, [sp, #8]
	bl	printk
	ldr	r0, [r4, #224]
	mov	r2, #4096
	mov	r1, #0
	bl	ftl_memset
	sub	r3, fp, #1
	mov	r2, fp, lsr #4
	ldr	r4, .L1583
	uxth	r3, r3
	str	r2, [sp, #20]
	str	r3, [sp, #24]
	b	.L1557
.L1579:
	ldr	r3, .L1583+4
	add	r2, r3, r5, asl #1
	ldrh	r6, [r2, #228]
	cmp	r6, #0
	bne	.L1558
	movw	r2, #3068
	ldr	r0, [r3, #144]
	ldrh	r2, [r4, r2]
	mov	r1, r6
	mov	r8, r6
	mov	r7, r6
	add	sl, r4, r5, asl #2
	mov	r2, r2, asl #9
	bl	ftl_memset
	add	r3, r4, r5
	ldrb	r9, [r3, #3764]	@ zero_extendqisi2
	b	.L1559
.L1567:
	mvn	r3, #0
	strb	r3, [sp, #34]
	strb	r3, [sp, #35]
	ldr	r3, [sp, #12]
	tst	r3, #1
	beq	.L1560
	ldr	r3, [sl, #3588]
	add	r2, sp, #34
	mov	r0, r9
	add	r3, r6, r3
	str	r3, [sp, #4]
	mov	r1, r3
	bl	FlashReadSpare
	ldrb	r2, [r4, #852]	@ zero_extendqisi2
	ldr	r3, [sp, #4]
	cmp	r2, #1
	bne	.L1560
	ldr	r1, [r4, #856]
	add	r2, sp, #35
	mov	r0, r9
	add	r1, r3, r1
	bl	FlashReadSpare
	ldrb	r3, [sp, #34]	@ zero_extendqisi2
	ldrb	r2, [sp, #35]	@ zero_extendqisi2
	and	r3, r2, r3
	strb	r3, [sp, #34]
.L1560:
	ldr	r0, [sp, #12]
	tst	r0, #2
	beq	.L1561
	ldr	r3, [r4, #3624]
	mov	r0, r9
	add	r2, sp, #35
	ldrh	r1, [r3, #10]
	ldr	r3, [sl, #3588]
	sub	r1, r1, #1
	add	r1, r1, r3
	add	r1, r1, r6
	bl	FlashReadSpare
.L1561:
	ldr	r3, [r4, #3624]
	ldrb	r3, [r3, #7]	@ zero_extendqisi2
	cmp	r3, #1
	cmpne	r3, #8
	ldrb	r3, [sp, #34]	@ zero_extendqisi2
	bne	.L1562
	cmp	r3, #0
	beq	.L1581
	ldrb	r0, [sp, #35]	@ zero_extendqisi2
	rsbs	r0, r0, #1
	movcc	r0, #0
	b	.L1563
.L1562:
	cmp	r3, #255
	bne	.L1581
	ldrb	r0, [sp, #35]	@ zero_extendqisi2
	subs	r0, r0, #255
	movne	r0, #1
	b	.L1563
.L1581:
	mov	r0, #1
.L1563:
	ldr	r2, [sp, #12]
	tst	r2, #4
	beq	.L1564
	ldr	r1, [sl, #3588]
	mov	r0, r9
	add	r1, r6, r1
	bl	SandiskProgTestBadBlock
.L1564:
	cmp	r0, #0
	beq	.L1565
	mov	r1, r5
	mov	r2, r8
	ldr	r0, .L1583+12
	add	r7, r7, #1
	bl	printk
	ldr	r3, [sp, #16]
	and	r0, r8, #31
	mov	ip, #1
	uxth	r7, r7
	mov	r2, r3, lsr #5
	ldr	r3, .L1583+4
	ldr	r3, [r3, #144]
	ldr	r1, [r3, r2, asl #2]
	orr	r1, r1, ip, asl r0
	ldr	r0, [sp, #20]
	str	r1, [r3, r2, asl #2]
	ldrb	r3, [r4, #3762]	@ zero_extendqisi2
	mul	r3, r3, r0
	cmp	r7, r3
	bgt	.L1566
.L1565:
	ldr	r2, [sp, #8]
	add	r8, r8, #1
	add	r6, r6, r2
.L1559:
	uxth	r3, r8
	str	r3, [sp, #16]
	cmp	r3, fp
	bcc	.L1567
.L1566:
	ldr	r0, .L1583+16
	mov	r1, r5
	mov	r2, r7
	bl	printk
	ldrb	r3, [r4, #3762]	@ zero_extendqisi2
	ldr	r0, [sp, #20]
	mul	r3, r3, r0
	cmp	r7, r3
	blt	.L1568
	movw	r3, #3068
	mov	r1, #0
	ldrh	r2, [r4, r3]
	ldr	r3, .L1583+4
	mov	r2, r2, asl #9
	ldr	r0, [r3, #144]
	bl	ftl_memset
.L1568:
	cmp	r5, #0
	bne	.L1569
	ldr	sl, .L1583+4
	mov	r8, r5
	mov	r7, r5
	ldrh	r9, [sl, #152]
	b	.L1570
.L1572:
	mov	r0, r6
	bl	FlashTestBlk
	cmp	r0, #0
	beq	.L1571
	mov	r1, r6
	ldr	r0, .L1583+20
	bl	printk
	ldr	r3, [sl, #144]
	mov	r2, r6, lsr #5
	mov	r0, #1
	add	r7, r7, r0
	and	r6, r6, #31
	ldr	r1, [r3, r2, asl #2]
	uxth	r7, r7
	orr	r6, r1, r0, asl r6
	str	r6, [r3, r2, asl #2]
.L1571:
	add	r8, r8, #1
	uxth	r8, r8
.L1570:
	add	r6, r8, r9
	ldrb	r3, [r4, #853]	@ zero_extendqisi2
	uxth	r6, r6
	cmp	r3, r6
	bhi	.L1572
	ldr	r6, [sp, #24]
	sub	r9, fp, #50
	ldr	sl, .L1583+4
	mov	r8, #1
	b	.L1573
.L1575:
	mov	r0, r6
	bl	FlashTestBlk
	cmp	r0, #0
	beq	.L1574
	mov	r1, r6
	ldr	r0, .L1583+20
	bl	printk
	ldr	r3, [sl, #144]
	mov	r2, r6, lsr #5
	and	r0, r6, #31
	ldr	r1, [r3, r2, asl #2]
	orr	r1, r1, r8, asl r0
	str	r1, [r3, r2, asl #2]
.L1574:
	sub	r6, r6, #1
	uxth	r6, r6
.L1573:
	cmp	r6, r9
	bgt	.L1575
	ldr	r3, .L1583+4
	ldrb	r1, [r4, #853]	@ zero_extendqisi2
	ldr	r2, [r3, #152]
	rsb	r2, r2, r1
	cmp	r7, r2
	bcc	.L1569
	movw	r2, #3068
	ldr	r0, [r3, #144]
	ldrh	r2, [r4, r2]
	mov	r1, #0
	mov	r2, r2, asl #9
	bl	ftl_memset
.L1569:
	mul	r8, fp, r5
	ldr	r7, .L1583+4
	ldr	r6, [sp, #24]
	ldr	r9, [sp, #28]
	add	sl, r7, r5, asl #1
.L1576:
	ldr	r0, .L1583+24
	mov	r1, r5
	mov	r2, r6
	bl	printk
	ldr	r3, [r7, #144]
	b	.L1577
.L1578:
	sub	r6, r6, #1
	uxth	r6, r6
.L1577:
	mov	r1, r6, lsr #5
	and	r2, r6, #31
	ldr	r1, [r3, r1, asl #2]
	mov	r2, r1, lsr r2
	ands	r2, r2, #1
	bne	.L1578
	ldr	r3, .L1583+28
	mov	r1, #1
	strh	r6, [sl, #228]	@ movhi
	add	r0, sp, #36
	strh	r6, [r9, #2]	@ movhi
	strh	r3, [r9, #0]	@ movhi
	ldr	r3, [r7, #144]
	strh	r2, [r9, #8]	@ movhi
	mov	r2, r1
	str	r3, [sp, #44]
	ldr	r3, [r7, #224]
	str	r3, [sp, #48]
	add	r3, r6, r8
	mov	r3, r3, asl #10
	str	r3, [sp, #40]
	bl	FlashEraseBlocks
	mov	r1, #1
	mov	r3, r1
	add	r0, sp, #36
	mov	r2, r1
	bl	FlashProgPages
	ldr	r3, [sp, #36]
	cmp	r3, #0
	subne	r6, r6, #1
	uxthne	r6, r6
	bne	.L1576
.L1558:
	add	r5, r5, #1
	uxtb	r5, r5
.L1557:
	ldrb	r3, [r4, #3762]	@ zero_extendqisi2
	cmp	r3, r5
	bhi	.L1579
	add	sp, sp, #76
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, sl, fp, pc}
.L1584:
	.align	2
.L1583:
	.word	.LANCHOR0
	.word	.LANCHOR2
	.word	.LC108
	.word	.LC109
	.word	.LC110
	.word	.LC111
	.word	.LC112
	.word	-3872
	.fnend
	.size	FlashMakeFactorBbt, .-FlashMakeFactorBbt
	.align	2
	.global	ftl_map_blk_gc
	.type	ftl_map_blk_gc, %function
ftl_map_blk_gc:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	stmfd	sp!, {r3, r4, r5, r6, r7, r8, r9, sl, fp, lr}
	.save {r3, r4, r5, r6, r7, r8, r9, sl, fp, lr}
	mov	r4, r0
	ldr	r6, [r0, #12]
	ldr	r5, [r0, #24]
	bl	ftl_free_no_use_map_blk
	ldrh	ip, [r4, #10]
	ldrh	r2, [r4, #8]
	sub	r3, ip, #4
	cmp	r2, r3
	bge	.L1586
	ldrh	r2, [r4, #40]
	movw	r3, #65535
	cmp	r2, r3
	beq	.L1587
	ldr	r1, .L1599
	movw	r3, #3910
	ldrh	r2, [r4, #2]
	ldrh	r3, [r1, r3]
	cmp	r2, r3
	bcc	.L1587
.L1586:
	ldrh	r1, [r4, #40]
	movw	r3, #65535
	uxth	r7, r0
	cmp	r1, r3
	beq	.L1588
	ldr	r0, .L1599
	movw	r2, #3910
	ldrh	r3, [r4, #2]
	ldrh	r2, [r0, r2]
	cmp	r2, r3
	movls	r0, r6
	movls	r2, #0
	bhi	.L1588
	b	.L1589
.L1591:
	ldrh	lr, [r0], #2
	cmp	lr, r1
	beq	.L1598
	add	r2, r2, #1
	uxth	r2, r2
.L1589:
	cmp	r2, ip
	bne	.L1591
	b	.L1590
.L1598:
	mov	r7, r2
.L1590:
	mov	r2, r2, asl #1
	ldr	r0, .L1599+4
	ldrh	r2, [r6, r2]
	bl	printk
	mvn	r3, #0
	strh	r3, [r4, #40]	@ movhi
.L1588:
	mov	r7, r7, asl #1
	ldrh	r8, [r6, r7]
	cmp	r8, #0
	beq	.L1587
	ldr	r3, [r4, #32]
	cmp	r3, #0
	bne	.L1587
	mov	r2, #1
	ldr	r1, .L1599
	str	r2, [r4, #32]
	strh	r3, [r6, r7]	@ movhi
	ldrh	r3, [r4, #8]
	ldrh	r2, [r4, #2]
	sub	r3, r3, #1
	strh	r3, [r4, #8]	@ movhi
	movw	r3, #3910
	ldrh	r3, [r1, r3]
	cmp	r2, r3
	bcc	.L1592
	mov	r0, r4
	bl	ftl_map_blk_alloc_new_blk
.L1592:
	mov	r7, #0
	ldr	r6, .L1599+8
	mov	fp, r7
	b	.L1593
.L1596:
	ldr	r3, [r5, r7, asl #2]
	mov	r9, r7, asl #2
	cmp	r8, r3, lsr #10
	bne	.L1594
	ldr	r3, [r6, #-960]
	mov	r1, #1
	ldr	sl, [r6, #-940]
	mov	r2, r1
	ldr	r0, .L1599+12
	str	r3, [r6, #184]
	str	sl, [r6, #188]
	ldr	r3, [r5, r7, asl #2]
	str	r3, [r6, #180]
	bl	FlashReadPages
	ldr	r3, [r6, #176]
	cmn	r3, #1
	bne	.L1595
	str	fp, [r5, r9]
	ldr	r0, .L1599+16
	ldr	r1, [r6, #180]
	ldrh	r2, [sl, #8]
	bl	printk
	mov	r3, #1
	str	r3, [r6, #-2092]
	b	.L1594
.L1595:
	mov	r0, r4
	mov	r1, r7
	ldr	r2, [r6, #184]
	bl	FtlMapWritePage
.L1594:
	add	r7, r7, #1
	uxth	r7, r7
.L1593:
	ldrh	r3, [r4, #6]
	cmp	r3, r7
	bhi	.L1596
	mov	r0, r8
	mov	r1, #1
	bl	FtlFreeSysBlkQueueIn
	mov	r3, #0
	str	r3, [r4, #32]
.L1587:
	ldr	r1, .L1599
	movw	r3, #3910
	ldrh	r2, [r4, #2]
	ldrh	r3, [r1, r3]
	cmp	r2, r3
	bcc	.L1597
	mov	r0, r4
	bl	ftl_map_blk_alloc_new_blk
.L1597:
	mov	r0, #0
	ldmfd	sp!, {r3, r4, r5, r6, r7, r8, r9, sl, fp, pc}
.L1600:
	.align	2
.L1599:
	.word	.LANCHOR0
	.word	.LC113
	.word	.LANCHOR2
	.word	.LANCHOR2+176
	.word	.LC114
	.fnend
	.size	ftl_map_blk_gc, .-ftl_map_blk_gc
	.align	2
	.global	Ftl_write_map_blk_to_last_page
	.type	Ftl_write_map_blk_to_last_page, %function
Ftl_write_map_blk_to_last_page:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	stmfd	sp!, {r4, r5, r6, r7, r8, lr}
	.save {r4, r5, r6, r7, r8, lr}
	mov	r4, r0
	ldr	r5, .L1607
	ldr	r7, [r0, #12]
	ldr	r8, [r0, #24]
	ldr	r6, [r5, #-2092]
	cmp	r6, #0
	bne	.L1602
	ldrh	r3, [r0, #0]
	movw	r2, #65535
	cmp	r3, r2
	bne	.L1603
	ldrh	r3, [r0, #8]
	add	r3, r3, #1
	strh	r3, [r0, #8]	@ movhi
	bl	FtlFreeSysBlkQueueOut
	strh	r0, [r7, #0]	@ movhi
	ldr	r3, [r4, #28]
	strh	r6, [r4, #2]	@ movhi
	add	r3, r3, #1
	strh	r6, [r4, #0]	@ movhi
	str	r3, [r4, #28]
	b	.L1602
.L1603:
	mov	r3, r3, asl #1
	ldr	r2, [r0, #28]
	mov	r1, #255
	ldrh	r7, [r7, r3]
	ldrh	r3, [r0, #2]
	orr	r3, r3, r7, asl #10
	str	r3, [r5, #180]
	ldr	r3, [r5, #-2076]
	str	r3, [r5, #184]
	ldr	r3, [r5, #-940]
	str	r3, [r5, #188]
	str	r2, [r3, #4]
	ldr	r2, .L1607+4
	strh	r2, [r3, #8]	@ movhi
	ldrh	r2, [r0, #4]
	strh	r7, [r3, #2]	@ movhi
	strh	r2, [r3, #0]	@ movhi
	movw	r3, #3910
	ldr	r2, .L1607+8
	ldr	r0, [r5, #-2076]
	ldrh	r2, [r2, r3]
	mov	r2, r2, asl #3
	bl	ftl_memset
	mov	r3, r6
	b	.L1604
.L1606:
	ldr	r2, [r8, r3, asl #2]
	cmp	r7, r2, lsr #10
	bne	.L1605
	add	r6, r6, #1
	ldr	r2, [r5, #-2076]
	uxth	r6, r6
	str	r3, [r2, r6, asl #3]
	ldr	r2, [r5, #-2076]
	ldr	r1, [r8, r3, asl #2]
	add	r2, r2, r6, asl #3
	str	r1, [r2, #4]
.L1605:
	add	r3, r3, #1
	uxth	r3, r3
.L1604:
	ldrh	r2, [r4, #6]
	cmp	r2, r3
	bhi	.L1606
	mov	r1, #1
	mov	r3, #0
	ldr	r0, .L1607+12
	mov	r2, r1
	bl	FlashProgPages
	ldrh	r3, [r4, #2]
	mov	r0, r4
	add	r3, r3, #1
	strh	r3, [r4, #2]	@ movhi
	bl	ftl_map_blk_gc
.L1602:
	mov	r0, #0
	ldmfd	sp!, {r4, r5, r6, r7, r8, pc}
.L1608:
	.align	2
.L1607:
	.word	.LANCHOR2
	.word	-1291
	.word	.LANCHOR0
	.word	.LANCHOR2+176
	.fnend
	.size	Ftl_write_map_blk_to_last_page, .-Ftl_write_map_blk_to_last_page
	.align	2
	.type	FtlMapWritePage.part.15, %function
FtlMapWritePage.part.15:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	stmfd	sp!, {r4, r5, r6, r7, r8, r9, sl, lr}
	.save {r4, r5, r6, r7, r8, r9, sl, lr}
	mov	r4, r0
	ldr	r5, .L1622
	mov	sl, r1
	ldr	r8, .L1622+4
	mov	r9, r2
	mov	r6, #0
.L1620:
	ldr	r3, [r5, #-1864]
	add	r3, r3, #1
	str	r3, [r5, #-1864]
	movw	r3, #3910
	ldrh	r2, [r4, #2]
	ldrh	r3, [r8, r3]
	sub	r3, r3, #1
	cmp	r2, r3
	bge	.L1611
	ldrh	r2, [r4, #0]
	movw	r3, #65535
	cmp	r2, r3
	bne	.L1612
.L1611:
	mov	r0, r4
	bl	Ftl_write_map_blk_to_last_page
.L1612:
	ldrh	r3, [r4, #0]
	mov	r1, #0
	ldr	r2, [r4, #12]
	ldr	r0, [r5, #-940]
	mov	r3, r3, asl #1
	ldrh	r7, [r2, r3]
	mov	r2, #16
	ldrh	r3, [r4, #2]
	str	r9, [r5, #184]
	orr	r3, r3, r7, asl #10
	str	r0, [r5, #188]
	str	r3, [r5, #180]
	bl	ftl_memset
	ldr	r3, [r5, #188]
	mov	r1, #1
	ldr	r2, [r4, #28]
	ldr	r0, .L1622+8
	strh	sl, [r3, #8]	@ movhi
	str	r2, [r3, #4]
	ldrh	r2, [r4, #4]
	strh	r7, [r3, #2]	@ movhi
	strh	r2, [r3, #0]	@ movhi
	mov	r2, r1
	mov	r3, r1
	bl	FlashProgPages
	ldrh	r2, [r4, #2]
	add	r2, r2, #1
	uxth	r2, r2
	strh	r2, [r4, #2]	@ movhi
	ldr	r3, [r5, #176]
	cmn	r3, #1
	bne	.L1613
	ldr	r0, .L1622+12
	add	r6, r6, #1
	ldr	r1, [r5, #180]
	bl	printk
	ldrh	r3, [r4, #2]
	uxth	r6, r6
	cmp	r3, #2
	movwls	r3, #3910
	ldrlsh	r3, [r8, r3]
	subls	r3, r3, #1
	strlsh	r3, [r4, #2]	@ movhi
	cmp	r6, #3
	bls	.L1620
	ldr	r4, .L1622
	mov	r2, r6
	ldr	r0, .L1622+16
	ldr	r1, [r4, #180]
	bl	printk
	mov	r3, #1
	str	r3, [r4, #-2092]
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, sl, pc}
.L1613:
	cmp	r3, #0
	strneh	r7, [r4, #40]	@ movhi
	cmp	r2, #1
	beq	.L1620
	cmp	r3, #256
	beq	.L1620
	ldr	r3, .L1622
	ldr	r2, [r3, #180]
	ldr	r3, [r4, #24]
	str	r2, [r3, sl, asl #2]
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, sl, pc}
.L1623:
	.align	2
.L1622:
	.word	.LANCHOR2
	.word	.LANCHOR0
	.word	.LANCHOR2+176
	.word	.LC115
	.word	.LC116
	.fnend
	.size	FtlMapWritePage.part.15, .-FtlMapWritePage.part.15
	.align	2
	.global	FtlMapWritePage
	.type	FtlMapWritePage, %function
FtlMapWritePage:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	stmfd	sp!, {r3, lr}
	.save {r3, lr}
	ldr	r3, .L1626
	ldr	r3, [r3, #-2092]
	cmp	r3, #0
	bne	.L1625
	bl	FtlMapWritePage.part.15
.L1625:
	mov	r0, #0
	ldmfd	sp!, {r3, pc}
.L1627:
	.align	2
.L1626:
	.word	.LANCHOR2
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
	ldr	r5, .L1629
	mul	r4, r4, r0
	ldr	r0, .L1629+4
	ldr	r2, [r5, #-1900]
	add	r3, r2, r4
	ldrh	r1, [r2, r4]
	ldr	r2, [r3, #8]
	bl	FtlMapWritePage
	ldr	r3, [r5, #-1900]
	mov	r0, #0
	add	r4, r3, r4
	ldr	r3, [r4, #4]
	bic	r3, r3, #-2147483648
	str	r3, [r4, #4]
	ldmfd	sp!, {r3, r4, r5, pc}
.L1630:
	.align	2
.L1629:
	.word	.LANCHOR2
	.word	.LANCHOR2-1084
	.fnend
	.size	flush_l2p_region, .-flush_l2p_region
	.align	2
	.global	FtlMapBlkWriteDumpData
	.type	FtlMapBlkWriteDumpData, %function
FtlMapBlkWriteDumpData:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	stmfd	sp!, {r3, r4, r5, lr}
	.save {r3, r4, r5, lr}
	mov	r4, r0
	ldr	r3, [r0, #36]
	ldrh	r5, [r0, #6]
	cmp	r3, #0
	ldr	r2, [r0, #24]
	ldmeqfd	sp!, {r3, r4, r5, pc}
	mov	r3, #0
	str	r3, [r0, #36]
	ldr	r3, .L1635
	ldr	r1, [r3, #-2092]
	cmp	r1, #0
	ldmnefd	sp!, {r3, r4, r5, pc}
	sub	r5, r5, #1
	ldr	r0, [r3, #-960]
	ldr	r1, [r3, #-940]
	uxth	r5, r5
	str	r0, [r3, #184]
	str	r1, [r3, #188]
	ldr	r2, [r2, r5, asl #2]
	cmp	r2, #0
	str	r2, [r3, #180]
	beq	.L1633
	mov	r1, #1
	add	r0, r3, #176
	mov	r2, r1
	bl	FlashReadPages
	b	.L1634
.L1633:
	ldr	r2, .L1635+4
	movw	r3, #3918
	mov	r1, #255
	ldrh	r2, [r2, r3]
	bl	ftl_memset
.L1634:
	ldr	r3, .L1635
	mov	r0, r4
	mov	r1, r5
	ldr	r2, [r3, #184]
	ldmfd	sp!, {r3, r4, r5, lr}
	b	FtlMapWritePage
.L1636:
	.align	2
.L1635:
	.word	.LANCHOR2
	.word	.LANCHOR0
	.fnend
	.size	FtlMapBlkWriteDumpData, .-FtlMapBlkWriteDumpData
	.align	2
	.global	FtlVendorPartRead
	.type	FtlVendorPartRead, %function
FtlVendorPartRead:
	.fnstart
	@ args = 0, pretend = 0, frame = 48
	@ frame_needed = 0, uses_anonymous_args = 0
	ldr	r3, .L1648
	stmfd	sp!, {r4, r5, r6, r7, r8, r9, sl, fp, lr}
	.save {r4, r5, r6, r7, r8, r9, sl, fp, lr}
	mov	r9, r2
	movw	r2, #3916
	mov	r5, r1
	ldrh	r7, [r3, r2]
	movw	r2, #3902
	ldrh	r2, [r3, r2]
	add	r1, r1, r0
	.pad #52
	sub	sp, sp, #52
	mov	r8, r0
	cmp	r1, r2
	mvnhi	fp, #0
	bhi	.L1638
	mov	r7, r0, lsr r7
	mov	fp, #0
	ldr	r6, .L1648+4
	mov	r2, r7, asl #2
	str	r2, [sp, #0]
	b	.L1639
.L1645:
	ldr	r3, [r6, #-908]
	mov	r0, r8
	ldr	r2, [sp, #0]
	ldr	sl, [r3, r2]
	movw	r3, #3914
	ldr	r2, .L1648
	ldrh	r4, [r2, r3]
	mov	r1, r4
	bl	__aeabi_uidivmod
	uxth	r1, r1
	str	r1, [sp, #4]
	rsb	r4, r1, r4
	uxth	r4, r4
	cmp	r4, r5
	uxthhi	r4, r5
	cmp	sl, #0
	beq	.L1641
	ldr	r2, .L1648+4
	mov	r1, #1
	add	r0, sp, #12
	str	sl, [sp, #16]
	ldr	r3, [r2, #-956]
	mov	r2, r1
	str	r3, [sp, #20]
	mov	r3, #0
	str	r3, [sp, #24]
	bl	FlashReadPages
	ldr	r3, [sp, #12]
	cmn	r3, #1
	moveq	fp, r3
	ldr	r3, [r6, #176]
	cmp	r3, #256
	bne	.L1643
	mov	r1, r7
	mov	r2, sl
	ldr	r0, .L1648+8
	bl	printk
	ldr	r0, .L1648+12
	mov	r1, r7
	ldr	r2, [r6, #-956]
	bl	FtlMapWritePage
.L1643:
	ldr	r3, [sp, #4]
	mov	r0, r9
	ldr	r1, [r6, #-956]
	mov	r2, r4, asl #9
	add	r1, r1, r3, asl #9
	bl	memcpy
	b	.L1644
.L1641:
	mov	r0, r9
	mov	r1, sl
	mov	r2, r4, asl #9
	bl	ftl_memset
.L1644:
	ldr	r3, [sp, #0]
	add	r7, r7, #1
	rsb	r5, r4, r5
	add	r8, r8, r4
	add	r9, r9, r4, asl #9
	add	r3, r3, #4
	str	r3, [sp, #0]
.L1639:
	cmp	r5, #0
	bne	.L1645
.L1638:
	mov	r0, fp
	add	sp, sp, #52
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, sl, fp, pc}
.L1649:
	.align	2
.L1648:
	.word	.LANCHOR0
	.word	.LANCHOR2
	.word	.LC117
	.word	.LANCHOR2+244
	.fnend
	.size	FtlVendorPartRead, .-FtlVendorPartRead
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
	ldr	r5, .L1654
	mov	r1, #1
	ldr	r2, .L1654+4
	bl	FtlVendorPartRead
	ldr	r4, .L1654+8
	ldr	r3, [r5, #-1704]
	cmp	r3, r4
	beq	.L1651
	ldr	r0, .L1654+4
	mov	r1, #0
	mov	r2, #512
	bl	ftl_memset
	str	r4, [r5, #-1704]
.L1651:
	ldr	r2, [r5, #-1704]
	ldr	r3, .L1654
	cmp	r2, r4
	bne	.L1652
	ldr	r2, [r3, #-1616]
	str	r2, [r3, #-1856]
	ldr	r2, [r3, #-1612]
	str	r2, [r3, #-1852]
	ldr	r2, [r3, #-1696]
	str	r2, [r3, #-1860]
	ldr	r2, [r3, #-1692]
	str	r2, [r3, #-1872]
	ldr	r2, [r3, #-1688]
	str	r2, [r3, #-1880]
	ldr	r2, [r3, #-1684]
	str	r2, [r3, #-1864]
	ldr	r2, [r3, #-1676]
	str	r2, [r3, #-1836]
	ldr	r2, [r3, #-1672]
	str	r2, [r3, #-2080]
	ldr	r2, [r3, #-1668]
	str	r2, [r3, #-1876]
	ldr	r2, [r3, #-1664]
	str	r2, [r3, #-1868]
	ldr	r2, [r3, #-1660]
	str	r2, [r3, #-1828]
	ldr	r2, [r3, #-1656]
	str	r2, [r3, #-1824]
	ldr	r2, [r3, #-1644]
	str	r2, [r3, #-1708]
.L1652:
	ldr	r4, .L1654
	mov	r3, #0
	ldr	r2, [r4, #-1636]
	str	r3, [r4, #-996]
	ldr	r3, .L1654+12
	cmp	r2, r3
	bne	.L1653
	mov	r3, #1
	ldr	r0, .L1654+16
	str	r3, [r4, #-1884]
	ldr	r1, .L1654+20
	bl	printk
.L1653:
	ldr	r3, .L1654+24
	movw	r2, #3898
	ldr	r0, [r4, #-1840]
	ldrh	r1, [r3, r2]
	ldr	r2, [r4, #-1836]
	mla	r0, r0, r1, r2
	movw	r2, #3848
	ldrh	r1, [r3, r2]
	bl	__aeabi_uidiv
	str	r0, [r4, #-1832]
	ldmfd	sp!, {r3, r4, r5, pc}
.L1655:
	.align	2
.L1654:
	.word	.LANCHOR2
	.word	.LANCHOR2-1704
	.word	1179929683
	.word	305432421
	.word	.LC75
	.word	.LC118
	.word	.LANCHOR0
	.fnend
	.size	Ftl_load_ext_data, .-Ftl_load_ext_data
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
	ldr	r4, .L1658
	ldr	r5, .L1658+4
	ldr	r2, [r4, #-924]
	ldrh	r1, [r4, r5]
	bl	FtlVendorPartRead
	ldr	r3, [r4, #-924]
	ldr	r2, [r3, #0]
	ldr	r3, .L1658+8
	cmp	r2, r3
	beq	.L1657
	ldr	r1, .L1658+12
	ldr	r0, .L1658+16
	bl	printk
	ldrh	r2, [r4, r5]
	ldr	r0, [r4, #-924]
	mov	r1, #0
	mov	r2, r2, asl #9
	bl	ftl_memset
.L1657:
	mov	r0, #0
	ldmfd	sp!, {r3, r4, r5, pc}
.L1659:
	.align	2
.L1658:
	.word	.LANCHOR2
	.word	-928
	.word	1112818501
	.word	.LC119
	.word	.LC75
	.fnend
	.size	FtlLoadEctTbl, .-FtlLoadEctTbl
	.align	2
	.global	FtlVendorPartWrite
	.type	FtlVendorPartWrite, %function
FtlVendorPartWrite:
	.fnstart
	@ args = 0, pretend = 0, frame = 56
	@ frame_needed = 0, uses_anonymous_args = 0
	ldr	r3, .L1670
	stmfd	sp!, {r4, r5, r6, r7, r8, r9, sl, fp, lr}
	.save {r4, r5, r6, r7, r8, r9, sl, fp, lr}
	mov	r9, r2
	movw	r2, #3916
	mov	r4, r1
	ldrh	r8, [r3, r2]
	movw	r2, #3902
	ldrh	r2, [r3, r2]
	add	r1, r1, r0
	.pad #60
	sub	sp, sp, #60
	mov	r6, r0
	cmp	r1, r2
	mvnhi	r7, #0
	bhi	.L1661
	mov	r8, r0, lsr r8
	mov	r7, #0
	ldr	sl, .L1670+4
	mov	ip, r8, asl #2
	str	ip, [sp, #8]
	b	.L1662
.L1667:
	ldr	r1, .L1670
	movw	r2, #3914
	ldr	ip, [sp, #8]
	mov	r0, r6
	ldr	r3, [sl, #-908]
	ldrh	fp, [r1, r2]
	ldr	r3, [r3, ip]
	mov	r1, fp
	str	r3, [sp, #4]
	bl	__aeabi_uidivmod
	ldr	r3, [sp, #4]
	ldr	r2, .L1670+4
	uxth	r1, r1
	str	r1, [sp, #12]
	rsb	r5, r1, fp
	uxth	r5, r5
	cmp	r5, r4
	uxthhi	r5, r4
	cmp	r3, #0
	beq	.L1664
	cmp	r5, fp
	beq	.L1664
	str	r3, [sp, #24]
	mov	r1, #1
	ldr	r3, [r2, #-956]
	add	r0, sp, #20
	mov	r2, r1
	str	r3, [sp, #28]
	mov	r3, #0
	str	r3, [sp, #32]
	bl	FlashReadPages
	b	.L1665
.L1664:
	ldr	ip, .L1670
	movw	r3, #3918
	ldr	r0, [r2, #-956]
	mov	r1, #0
	ldrh	r2, [ip, r3]
	bl	ftl_memset
.L1665:
	ldr	r3, [sp, #12]
	mov	fp, r5, asl #9
	ldr	r0, [sl, #-956]
	mov	r1, r9
	mov	r2, fp
	rsb	r4, r5, r4
	add	r0, r0, r3, asl #9
	add	r6, r6, r5
	bl	memcpy
	mov	r1, r8
	ldr	r0, .L1670+8
	add	r8, r8, #1
	ldr	r2, [sl, #-956]
	add	r9, r9, fp
	bl	FtlMapWritePage
	ldr	ip, [sp, #8]
	add	ip, ip, #4
	str	ip, [sp, #8]
	cmn	r0, #1
	moveq	r7, r0
.L1662:
	cmp	r4, #0
	bne	.L1667
.L1661:
	mov	r0, r7
	add	sp, sp, #60
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, sl, fp, pc}
.L1671:
	.align	2
.L1670:
	.word	.LANCHOR0
	.word	.LANCHOR2
	.word	.LANCHOR2+244
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
	ldr	r3, .L1674
	ldr	r2, .L1674+4
	ldr	r1, [r3, #-1704]
	cmp	r1, r2
	bxne	lr
	ldr	r2, .L1674+8
	mov	r0, #0
	mov	r1, #1
	str	r2, [r3, #-1700]
	ldr	r2, [r3, #-1856]
	str	r2, [r3, #-1616]
	ldr	r2, [r3, #-1852]
	str	r2, [r3, #-1612]
	ldr	r2, [r3, #-1860]
	str	r2, [r3, #-1696]
	ldr	r2, [r3, #-1872]
	str	r2, [r3, #-1692]
	ldr	r2, [r3, #-1880]
	str	r2, [r3, #-1688]
	ldr	r2, [r3, #-1864]
	str	r2, [r3, #-1684]
	ldr	r2, [r3, #-1836]
	str	r2, [r3, #-1676]
	ldr	r2, [r3, #-2080]
	str	r2, [r3, #-1672]
	ldr	r2, [r3, #-1876]
	str	r2, [r3, #-1668]
	ldr	r2, [r3, #-1868]
	str	r2, [r3, #-1664]
	ldr	r2, [r3, #-1828]
	str	r2, [r3, #-1660]
	ldr	r2, [r3, #-1824]
	str	r2, [r3, #-1656]
	ldr	r2, [r3, #-1708]
	str	r2, [r3, #-1644]
	ldr	r2, [r3, #-996]
	str	r2, [r3, #-1640]
	ldr	r2, .L1674+12
	b	FtlVendorPartWrite
.L1675:
	.align	2
.L1674:
	.word	.LANCHOR2
	.word	1179929683
	.word	1342177351
	.word	.LANCHOR2-1704
	.fnend
	.size	Ftl_save_ext_data, .-Ftl_save_ext_data
	.align	2
	.global	FtlEctTblFlush
	.type	FtlEctTblFlush, %function
FtlEctTblFlush:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	ldr	r1, .L1683
	stmfd	sp!, {r3, lr}
	.save {r3, lr}
	ldr	r3, [r1, #-1884]
	cmp	r3, #0
	moveq	r2, #32
	beq	.L1677
	ldr	r2, [r1, #-1824]
	cmp	r2, #29
	movls	r2, #4
	movhi	r2, #32
.L1677:
	mov	r3, #288
	ldrh	r1, [r1, r3]
	cmp	r1, #31
	addls	r1, r1, #1
	ldrls	r2, .L1683
	strlsh	r1, [r2, r3]	@ movhi
	movls	r2, #1
	cmp	r0, #0
	ldr	r3, .L1683
	bne	.L1679
	ldr	r1, [r3, #-924]
	ldr	r0, [r1, #20]
	ldr	r1, [r1, #16]
	add	r2, r2, r0
	cmp	r1, r2
	bcc	.L1680
.L1679:
	ldr	r2, [r3, #-924]
	mov	r0, #64
	ldr	r1, [r2, #16]
	str	r1, [r2, #20]
	ldr	r1, .L1683+4
	str	r1, [r2, #0]
	ldr	r1, .L1683+8
	ldr	r2, [r3, #-924]
	ldrh	r1, [r3, r1]
	mov	r3, r1, asl #9
	str	r3, [r2, #12]
	ldr	r3, [r2, #8]
	add	r3, r3, #1
	str	r3, [r2, #8]
	mov	r3, #0
	str	r3, [r2, #4]
	bl	FtlVendorPartWrite
	bl	Ftl_save_ext_data
.L1680:
	mov	r0, #0
	ldmfd	sp!, {r3, pc}
.L1684:
	.align	2
.L1683:
	.word	.LANCHOR2
	.word	1112818501
	.word	-928
	.fnend
	.size	FtlEctTblFlush, .-FtlEctTblFlush
	.align	2
	.global	FtlMapTblRecovery
	.type	FtlMapTblRecovery, %function
FtlMapTblRecovery:
	.fnstart
	@ args = 0, pretend = 0, frame = 24
	@ frame_needed = 0, uses_anonymous_args = 0
	stmfd	sp!, {r4, r5, r6, r7, r8, r9, sl, fp, lr}
	.save {r4, r5, r6, r7, r8, r9, sl, fp, lr}
	.pad #36
	sub	sp, sp, #36
	ldr	r3, [r0, #16]
	mov	r4, r0
	ldrh	r8, [r0, #6]
	mov	r1, #0
	ldr	fp, [r0, #24]
	mov	r7, #0
	str	r3, [sp, #24]
	ldrh	r3, [r0, #8]
	mov	r2, r8, asl #2
	ldr	r9, [r0, #12]
	mov	r0, fp
	mov	sl, r8
	str	r3, [sp, #16]
	bl	ftl_memset
	ldr	r3, .L1721
	str	r7, [r4, #32]
	str	r7, [r4, #28]
	ldr	r2, [r3, #-2076]
	ldr	r5, [r3, #-940]
	str	r2, [r3, #184]
	str	r5, [r3, #188]
	mvn	r3, #0
	strh	r3, [r4, #0]	@ movhi
	strh	r3, [r4, #2]	@ movhi
	mov	r3, #1
	str	r3, [r4, #36]
	ldr	r3, [sp, #16]
	sub	r3, r3, #1
	str	r3, [sp, #20]
	ldr	r3, .L1721+4
	b	.L1686
.L1700:
	ldr	r0, [sp, #20]
	cmp	r2, r0
	mov	r2, r2, asl #1
	bne	.L1687
	ldrh	r0, [r9, r2]
	mov	r1, #1
	mov	r8, sl
	add	sl, r9, r2
	bl	FtlGetLastWrittenPage
	ldr	r1, [sp, #20]
	ldr	r2, [sp, #24]
	mov	ip, r9
	strh	r7, [r4, #0]	@ movhi
	mov	r7, #0
	ldr	r6, .L1721
	uxth	r0, r0
	add	r3, r0, #1
	strh	r3, [r4, #2]	@ movhi
	ldr	r3, [r2, r1, asl #2]
	str	r3, [r4, #28]
	sxth	r3, r0
	add	r3, r3, #1
	mov	r9, r3
	b	.L1688
.L1691:
	ldrh	r1, [sl, #0]
	ldr	r0, .L1721+8
	orr	r2, r2, r1, asl #10
	mov	r1, #1
	str	r2, [r6, #180]
	mov	r2, r1
	str	ip, [sp, #12]
	bl	FlashReadPages
	ldr	r2, [r6, #176]
	ldr	ip, [sp, #12]
	cmn	r2, #1
	ldreqh	r2, [sl, #0]
	streqh	r2, [r4, #40]	@ movhi
	beq	.L1690
	ldrh	r2, [r5, #8]
	cmp	r2, r8
	bcs	.L1690
	ldrh	r1, [r4, #4]
	ldrh	r0, [r5, #0]
	cmp	r0, r1
	ldreq	r1, [r6, #180]
	streq	r1, [fp, r2, asl #2]
.L1690:
	add	r7, r7, #1
	uxth	r7, r7
.L1688:
	sxth	r2, r7
	cmp	r2, r9
	blt	.L1691
	mov	r9, ip
	b	.L1692
.L1687:
	ldr	r6, .L1721
	movw	r8, #3910
	add	r0, r9, r2
	str	r0, [sp, #28]
	add	r0, r6, #176
	ldr	r1, [r6, #-2076]
	str	r1, [r6, #184]
	ldrh	r1, [r9, r2]
	ldrh	r2, [r3, r8]
	sub	r2, r2, #1
	orr	r2, r2, r1, asl #10
	mov	r1, #1
	str	r2, [r6, #180]
	mov	r2, r1
	str	r3, [sp, #12]
	bl	FlashReadPages
	ldr	r2, [r6, #176]
	ldr	r3, [sp, #12]
	cmn	r2, #1
	beq	.L1712
	ldrh	r1, [r5, #0]
	ldrh	r2, [r4, #4]
	cmp	r1, r2
	bne	.L1712
	ldrh	r1, [r5, #8]
	movw	r2, #64245
	cmp	r1, r2
	bne	.L1712
	b	.L1713
.L1696:
	ldr	r0, [r6, #-2076]
	mov	ip, r1, asl #3
	add	r2, r2, #1
	ldr	r1, [r0, r1, asl #3]
	uxth	r2, r2
	uxth	r1, r1
	cmp	r1, sl
	addcc	r0, r0, ip
	ldrcc	r0, [r0, #4]
	strcc	r0, [fp, r1, asl #2]
	b	.L1694
.L1713:
	mov	r2, #0
.L1694:
	ldrh	r0, [r3, r8]
	sxth	r1, r2
	sub	r0, r0, #1
	cmp	r1, r0
	blt	.L1696
	b	.L1697
.L1699:
	ldr	r0, [sp, #28]
	ldrh	r1, [r0, #0]
	ldr	r0, .L1721+8
	orr	r2, r2, r1, asl #10
	mov	r1, #1
	str	r2, [r8, #180]
	mov	r2, r1
	str	r3, [sp, #12]
	bl	FlashReadPages
	ldr	r2, [r8, #176]
	ldr	r3, [sp, #12]
	cmn	r2, #1
	beq	.L1698
	ldrh	r2, [r5, #8]
	cmp	r2, sl
	bcs	.L1698
	ldrh	r1, [r4, #4]
	ldrh	r0, [r5, #0]
	cmp	r0, r1
	ldreq	r1, [r8, #180]
	streq	r1, [fp, r2, asl #2]
.L1698:
	add	r6, r6, #1
	uxth	r6, r6
	b	.L1717
.L1712:
	ldr	r8, .L1721
	mov	r6, #0
.L1717:
	movw	r1, #3910
	sxth	r2, r6
	ldrh	r1, [r3, r1]
	cmp	r2, r1
	blt	.L1699
.L1697:
	add	r7, r7, #1
	uxth	r7, r7
.L1686:
	ldr	r1, [sp, #16]
	sxth	r2, r7
	cmp	r2, r1
	blt	.L1700
.L1692:
	ldr	r3, .L1721+12
	cmp	r4, r3
	bne	.L1701
	ldr	fp, .L1721+4
	mov	r3, #3952
	ldrh	r3, [fp, r3]
	cmp	r3, #0
	bne	.L1714
	b	.L1701
.L1708:
	ldr	r3, [r5, #-904]
	mov	r4, r7, asl #2
	ldr	r2, [r3, r7, asl #2]
	sub	r1, r2, #1
	cmn	r1, #3
	bhi	.L1703
	ldr	r1, [r5, #-1028]
	ldr	r1, [r1, r7, asl #2]
	cmp	r2, r1
	beq	.L1703
	ldr	r2, [r5, #-2076]
	mov	r1, #1
	ldr	r6, [r5, #-940]
	ldr	r0, .L1721+8
	str	r2, [r5, #184]
	mov	r2, r1
	str	r6, [r5, #188]
	ldr	r3, [r3, r4]
	str	r3, [r5, #180]
	bl	FlashReadPages
	ldr	r3, [r5, #176]
	cmn	r3, #1
	beq	.L1716
	ldr	r3, .L1721+16
	ldrh	r2, [r6, #0]
	ldrh	r3, [r5, r3]
	cmp	r2, r3
	ldreq	r8, [r6, #4]
	beq	.L1704
.L1716:
	mov	r8, #0
.L1704:
	ldr	r3, [r5, #-1028]
	mov	r1, #1
	mov	r2, r1
	ldr	r0, .L1721+8
	ldr	r3, [r3, r4]
	str	r3, [r5, #180]
	bl	FlashReadPages
	ldr	r1, [r6, #4]
	cmp	r8, r1
	bls	.L1703
	ldr	r2, [r5, #-904]
	ldr	r3, [r5, #-1028]
	ldr	r0, .L1721+20
	str	r1, [sp, #0]
	ldr	r1, [r2, r4]
	ldr	r2, [r3, r4]
	mov	r3, r8
	bl	printk
	ldr	r3, [r5, #-904]
	ldr	r2, [r3, r4]
	ldr	r3, [r5, #-1028]
	str	r2, [r3, r4]
	mov	r2, #0
	ldr	r3, [r5, #-904]
	ldr	r0, [r3, r4]
	ldr	r3, .L1721+24
	ubfx	r0, r0, #10, #16
	ldrh	r3, [r5, r3]
	b	.L1705
.L1706:
	mov	r1, r1, asl #1
	ldrh	r1, [r9, r1]
	cmp	r1, r0
	beq	.L1703
	add	r2, r2, #1
	uxth	r2, r2
.L1705:
	sxth	r1, r2
	cmp	r1, r3
	blt	.L1706
	b	.L1719
.L1720:
	mov	r3, r3, asl #1
	strh	r0, [r9, r3]	@ movhi
	ldr	r3, .L1721+24
	ldrh	r2, [r5, r3]
	add	r2, r2, #1
	strh	r2, [r5, r3]	@ movhi
	bl	remove_from_free_sys_Queue
.L1703:
	add	r7, r7, #1
	b	.L1702
.L1714:
	ldr	r5, .L1721
	mov	r7, #0
	mov	sl, r4
.L1702:
	movw	r3, #3944
	ldrh	r3, [fp, r3]
	cmp	r7, r3
	bcc	.L1708
	mov	r4, sl
.L1701:
	mov	r0, r4
	bl	ftl_free_no_use_map_blk
	ldr	r1, .L1721+4
	movw	r3, #3910
	ldrh	r2, [r4, #2]
	ldrh	r3, [r1, r3]
	cmp	r2, r3
	bne	.L1709
	mov	r0, r4
	bl	ftl_map_blk_alloc_new_blk
.L1709:
	mov	r0, r4
	bl	ftl_map_blk_gc
	mov	r0, r4
	bl	ftl_map_blk_gc
	mov	r0, #0
	add	sp, sp, #36
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, sl, fp, pc}
.L1719:
	ldr	r2, [fp, #3936]
	cmp	r3, r2
	bcs	.L1703
	b	.L1720
.L1722:
	.align	2
.L1721:
	.word	.LANCHOR2
	.word	.LANCHOR0
	.word	.LANCHOR2+176
	.word	.LANCHOR2-1084
	.word	-1080
	.word	.LC120
	.word	-1040
	.fnend
	.size	FtlMapTblRecovery, .-FtlMapTblRecovery
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
	ldr	r0, .L1724
	bl	FtlMapTblRecovery
	mov	r0, #0
	ldmfd	sp!, {r3, pc}
.L1725:
	.align	2
.L1724:
	.word	.LANCHOR2-1084
	.fnend
	.size	FtlLoadMapInfo, .-FtlLoadMapInfo
	.align	2
	.global	FtlLoadVonderInfo
	.type	FtlLoadVonderInfo, %function
FtlLoadVonderInfo:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	stmfd	sp!, {r3, lr}
	.save {r3, lr}
	movw	r2, #3928
	ldr	r3, .L1727
	ldr	r0, .L1727+4
	ldrh	r2, [r3, r2]
	strh	r2, [r0, #254]	@ movhi
	ldr	r2, .L1727+8
	strh	r2, [r0, #248]	@ movhi
	movw	r2, #3954
	ldrh	r2, [r3, r2]
	strh	r2, [r0, #252]	@ movhi
	movw	r2, #3930
	ldrh	r2, [r3, r2]
	ldr	r3, [r3, #3956]
	strh	r2, [r0, #250]	@ movhi
	mvn	r2, #0
	str	r3, [r0, #256]
	ldr	r3, [r0, #-912]
	str	r3, [r0, #260]
	ldr	r3, [r0, #-916]
	str	r3, [r0, #264]
	ldr	r3, [r0, #-908]
	str	r3, [r0, #268]
	mov	r3, #284
	strh	r2, [r0, r3]	@ movhi
	add	r0, r0, #244
	bl	FtlMapTblRecovery
	mov	r0, #0
	ldmfd	sp!, {r3, pc}
.L1728:
	.align	2
.L1727:
	.word	.LANCHOR0
	.word	.LANCHOR2
	.word	-3962
	.fnend
	.size	FtlLoadVonderInfo, .-FtlLoadVonderInfo
	.align	2
	.global	load_l2p_region
	.type	load_l2p_region, %function
load_l2p_region:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	stmfd	sp!, {r3, r4, r5, r6, r7, lr}
	.save {r3, r4, r5, r6, r7, lr}
	mov	r5, r0
	ldr	r4, .L1733
	mov	r7, #12
	ldr	r3, [r4, #-1028]
	ldr	r6, [r3, r0, asl #2]
	cmp	r6, #0
	bne	.L1730
	mul	r7, r7, r1
	ldr	r3, [r4, #-1900]
	ldr	r2, .L1733+4
	mov	r1, #255
	add	r3, r3, r7
	ldr	r0, [r3, #8]
	movw	r3, #3918
	ldrh	r2, [r2, r3]
	bl	ftl_memset
	ldr	r3, [r4, #-1900]
	strh	r5, [r3, r7]	@ movhi
	ldr	r3, [r4, #-1900]
	add	r7, r3, r7
	str	r6, [r7, #4]
	b	.L1731
.L1730:
	mul	r7, r7, r1
	ldr	r3, [r4, #-1900]
	mov	r1, #1
	add	r0, r4, #176
	mov	r2, r1
	str	r6, [r4, #180]
	add	r3, r3, r7
	ldr	r3, [r3, #8]
	str	r3, [r4, #184]
	ldr	r3, [r4, #-940]
	str	r3, [r4, #188]
	bl	FlashReadPages
	ldr	r3, [r4, #176]
	cmp	r3, #256
	bne	.L1732
	mov	r2, r6
	mov	r1, r5
	ldr	r0, .L1733+8
	mov	r6, r6, lsr #10
	bl	printk
	ldr	r3, .L1733+12
	ldr	r0, .L1733+16
	mov	r1, r5
	strh	r6, [r4, r3]	@ movhi
	ldr	r3, [r4, #-1900]
	add	r3, r3, r7
	ldr	r2, [r3, #8]
	bl	FtlMapWritePage
.L1732:
	ldr	r3, .L1733
	mov	r1, #0
	ldr	r3, [r3, #-1900]
	add	r2, r3, r7
	str	r1, [r2, #4]
	strh	r5, [r3, r7]	@ movhi
.L1731:
	mov	r0, #0
	ldmfd	sp!, {r3, r4, r5, r6, r7, pc}
.L1734:
	.align	2
.L1733:
	.word	.LANCHOR2
	.word	.LANCHOR0
	.word	.LC121
	.word	-1044
	.word	.LANCHOR2-1084
	.fnend
	.size	load_l2p_region, .-load_l2p_region
	.align	2
	.global	log2phys
	.type	log2phys, %function
log2phys:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	stmfd	sp!, {r3, r4, r5, r6, r7, r8, sl, lr}
	.save {r3, r4, r5, r6, r7, r8, sl, lr}
	mov	r8, r2
	ldr	r3, .L1744
	movw	r2, #3916
	mov	r4, r1
	ldrh	r7, [r3, r2]
	mvn	r2, #0
	add	r7, r7, #7
	mov	r6, r0, lsr r7
	bic	r7, r0, r2, asl r7
	movw	r2, #3946
	ldrh	r2, [r3, r2]
	uxth	r6, r6
	ldr	r3, .L1744+4
	uxth	r7, r7
	ldr	sl, [r3, #-1900]
	mov	r3, #0
	mov	r5, r3
	b	.L1736
.L1742:
	add	r3, r3, #12
	add	r1, sl, r3
	ldrh	r1, [r1, #-12]
	cmp	r1, r6
	bne	.L1737
.L1738:
	cmp	r8, #0
	ldr	r3, .L1744+4
	mov	r2, #12
	bne	.L1739
	ldr	r3, [r3, #-1900]
	mla	r2, r2, r5, r3
	ldr	r3, [r2, #8]
	ldr	r3, [r3, r7, asl #2]
	str	r3, [r4, #0]
	b	.L1740
.L1739:
	mul	r2, r2, r5
	ldr	r1, [r3, #-1900]
	ldr	r0, [r4, #0]
	add	r1, r1, r2
	ldr	r1, [r1, #8]
	str	r0, [r1, r7, asl #2]
	ldr	r1, [r3, #-1900]
	add	r2, r1, r2
	ldr	r1, [r2, #4]
	orr	r1, r1, #-2147483648
	str	r1, [r2, #4]
	ldr	r2, .L1744+8
	strh	r6, [r3, r2]	@ movhi
.L1740:
	ldr	r3, .L1744+4
	mov	r2, #12
	mov	r0, #0
	ldr	r3, [r3, #-1900]
	mla	r5, r2, r5, r3
	ldr	r3, [r5, #4]
	cmn	r3, #1
	addne	r3, r3, #1
	strne	r3, [r5, #4]
	ldmfd	sp!, {r3, r4, r5, r6, r7, r8, sl, pc}
.L1737:
	add	r5, r5, #1
	uxth	r5, r5
.L1736:
	cmp	r5, r2
	bne	.L1742
	bl	select_l2p_ram_region
	mov	r3, #12
	mul	r3, r3, r0
	mov	r5, r0
	add	r2, sl, r3
	ldrh	r1, [sl, r3]
	movw	r3, #65535
	cmp	r1, r3
	beq	.L1743
	ldr	r3, [r2, #4]
	cmp	r3, #0
	bge	.L1743
	bl	flush_l2p_region
.L1743:
	mov	r0, r6
	mov	r1, r5
	bl	load_l2p_region
	b	.L1738
.L1745:
	.align	2
.L1744:
	.word	.LANCHOR0
	.word	.LANCHOR2
	.word	-1896
	.fnend
	.size	log2phys, .-log2phys
	.align	2
	.global	FtlReUsePrevPpa
	.type	FtlReUsePrevPpa, %function
FtlReUsePrevPpa:
	.fnstart
	@ args = 0, pretend = 0, frame = 8
	@ frame_needed = 0, uses_anonymous_args = 0
	stmfd	sp!, {r0, r1, r2, r4, r5, r6, r7, lr}
	.save {r0, r1, r2, r4, r5, r6, r7, lr}
	mov	r7, r0
	ubfx	r0, r1, #10, #16
	str	r1, [sp, #4]
	bl	P2V_block_in_plane
	ldr	r3, .L1752
	ldr	r1, [r3, #-2064]
	mov	r5, r0, asl #1
	ldrh	r2, [r1, r5]
	cmp	r2, #0
	addne	r2, r2, #1
	strneh	r2, [r1, r5]	@ movhi
	bne	.L1748
	ldr	r4, [r3, #-2052]
	cmp	r4, #0
	beq	.L1748
	ldr	r1, .L1752+4
	mov	ip, #6
	ldrh	lr, [r3, r1]
	ldr	r3, [r3, #-2072]
	ldr	r1, .L1752+8
	rsb	r4, r3, r4
	mov	r4, r4, asr #1
	mul	r4, r1, r4
	movw	r1, #65535
	uxth	r4, r4
	b	.L1749
.L1751:
	cmp	r4, r0
	bne	.L1750
	mov	r1, r4
	ldr	r0, .L1752+12
	bl	List_remove_node
	ldr	r6, .L1752
	ldr	r3, .L1752+4
	mov	r0, r4
	ldrh	r2, [r6, r3]
	sub	r2, r2, #1
	strh	r2, [r6, r3]	@ movhi
	bl	INSERT_DATA_LIST
	ldr	r3, [r6, #-2064]
	ldrh	r2, [r3, r5]
	add	r2, r2, #1
	strh	r2, [r3, r5]	@ movhi
	b	.L1748
.L1750:
	mul	r4, ip, r4
	ldrh	r4, [r3, r4]
	cmp	r4, r1
	beq	.L1748
	add	r2, r2, #1
	uxth	r2, r2
.L1749:
	cmp	r2, lr
	bne	.L1751
.L1748:
	mov	r0, r7
	add	r1, sp, #4
	mov	r2, #1
	bl	log2phys
	ldmfd	sp!, {r1, r2, r3, r4, r5, r6, r7, pc}
.L1753:
	.align	2
.L1752:
	.word	.LANCHOR2
	.word	-2048
	.word	-1431655765
	.word	.LANCHOR2-2052
	.fnend
	.size	FtlReUsePrevPpa, .-FtlReUsePrevPpa
	.align	2
	.type	FtlReadRefresh.part.16, %function
FtlReadRefresh.part.16:
	.fnstart
	@ args = 0, pretend = 0, frame = 40
	@ frame_needed = 0, uses_anonymous_args = 0
	stmfd	sp!, {r4, r5, r6, lr}
	.save {r4, r5, r6, lr}
	.pad #40
	sub	sp, sp, #40
	ldr	r4, .L1762
	ldr	r6, .L1762+4
	ldr	r2, [r4, #-1620]
	ldr	r3, [r6, #3968]
	cmp	r2, r3
	bcs	.L1755
	mov	r5, #2048
.L1758:
	ldr	r0, [r4, #-1620]
	ldr	r3, [r6, #3968]
	cmp	r0, r3
	bcs	.L1760
	mov	r1, sp
	mov	r2, #0
	bl	log2phys
	ldr	r0, [sp, #0]
	ldr	r3, [r4, #-1620]
	cmn	r0, #1
	add	r3, r3, #1
	str	r3, [r4, #-1620]
	beq	.L1757
	str	r0, [sp, #8]
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
	bne	.L1760
	ldr	r0, [sp, #0]
	ubfx	r0, r0, #10, #16
	bl	P2V_block_in_plane
	bl	FtlGcRefreshBlock
	b	.L1760
.L1757:
	subs	r5, r5, #1
	bne	.L1758
	b	.L1756
.L1755:
	ldr	r3, [r4, #-1880]
	mov	r0, #0
	str	r0, [r4, #-1624]
	str	r0, [r4, #-1620]
	str	r3, [r4, #-1628]
	b	.L1756
.L1760:
	mvn	r0, #0
.L1756:
	add	sp, sp, #40
	ldmfd	sp!, {r4, r5, r6, pc}
.L1763:
	.align	2
.L1762:
	.word	.LANCHOR2
	.word	.LANCHOR0
	.fnend
	.size	FtlReadRefresh.part.16, .-FtlReadRefresh.part.16
	.align	2
	.global	FtlReadRefresh
	.type	FtlReadRefresh, %function
FtlReadRefresh:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	@ link register save eliminated.
	ldr	r3, .L1771
	ldr	r2, [r3, #-1624]
	cmp	r2, #0
	beq	.L1765
	b	FtlReadRefresh.part.16
.L1765:
	ldr	r1, .L1771+4
	ldr	r0, [r3, #-1828]
	ldr	r2, [r3, #-1884]
	ldrb	r1, [r1, #928]	@ zero_extendqisi2
	cmp	r1, #0
	addeq	r0, r2, r0, lsr #10
	ldr	r2, [r3, #-1880]
	moveq	r1, #33554432
	movne	r0, #4194304
	moveq	r0, r1, asr r0
	ldr	r1, [r3, #-1628]
	add	r3, r2, #1048576
	cmp	r1, r3
	bhi	.L1767
	add	r1, r0, r1
	cmp	r1, r2
	bcc	.L1767
	ldr	r3, .L1771
	ldrb	r3, [r3, #-1792]	@ zero_extendqisi2
	cmp	r3, #0
	bne	.L1768
.L1767:
	ldr	r3, .L1771
	mov	r1, #1
	str	r2, [r3, #-1628]
	str	r1, [r3, #-1624]
	mov	r1, #0
	str	r1, [r3, #-1620]
.L1768:
	mov	r0, #0
	bx	lr
.L1772:
	.align	2
.L1771:
	.word	.LANCHOR2
	.word	.LANCHOR0
	.fnend
	.size	FtlReadRefresh, .-FtlReadRefresh
	.align	2
	.global	FtlRecoverySuperblock
	.type	FtlRecoverySuperblock, %function
FtlRecoverySuperblock:
	.fnstart
	@ args = 0, pretend = 0, frame = 56
	@ frame_needed = 0, uses_anonymous_args = 0
	stmfd	sp!, {r4, r5, r6, r7, r8, r9, sl, fp, lr}
	.save {r4, r5, r6, r7, r8, r9, sl, fp, lr}
	movw	r2, #65535
	ldrh	r3, [r0, #0]
	.pad #60
	sub	sp, sp, #60
	mov	r4, r0
	cmp	r3, r2
	beq	.L1774
	ldrh	r3, [r0, #2]
	ldr	r1, .L1863
	ldrb	fp, [r0, #6]	@ zero_extendqisi2
	str	r3, [sp, #12]
	movw	r3, #3908
	ldrh	r3, [r1, r3]
	ldr	ip, [sp, #12]
	str	fp, [sp, #24]
	cmp	r3, ip
	mov	r3, #0
	streqh	r3, [r0, #4]	@ movhi
	moveq	r2, r3	@ movhi
	ldrneh	r0, [r0, #16]
	bne	.L1776
	b	.L1860
.L1777:
	add	r3, r3, #1
	uxth	r3, r3
	add	r1, r4, r3, asl #1
	ldrh	r0, [r1, #16]
.L1776:
	cmp	r0, r2
	beq	.L1777
	ldrb	r1, [r4, #8]	@ zero_extendqisi2
	cmp	r1, #1
	bne	.L1778
	bl	FtlGetLastWrittenPage
	cmn	r0, #1
	mov	r7, r0
	beq	.L1779
	ldr	r3, .L1863
	ldrb	r2, [r3, #928]	@ zero_extendqisi2
	cmp	r2, #0
	bne	.L1853
	add	r3, r3, r0, asl #1
	movw	r2, #3076
	ldrh	r8, [r3, r2]
	b	.L1845
.L1778:
	mov	r1, #0
	bl	FtlGetLastWrittenPage
	cmn	r0, #1
	mov	r7, r0
	beq	.L1779
.L1853:
	mov	r8, r7
.L1845:
	ldr	r3, .L1863
	mov	r2, #3840
	mov	r5, #0
	str	r4, [sp, #32]
	movw	ip, #65535
	ldrh	r9, [r3, r2]
	ldr	r2, .L1863+4
	ldr	sl, [r2, #-972]
	ldr	r6, [r2, #-1164]
	mov	r2, #3920
	ldrh	lr, [r3, r2]
	mov	r2, r4
	mov	r3, r5
	b	.L1781
.L1779:
	mov	r3, #0
	strh	r3, [r4, #2]	@ movhi
	mov	r2, r3	@ movhi
.L1860:
	strb	r2, [r4, #6]
	b	.L1774
.L1783:
	ldrh	r0, [r2, #16]
	cmp	r0, ip
	beq	.L1782
	mov	fp, #36
	orr	r0, r8, r0, asl #10
	mla	r1, fp, r5, sl
	str	r0, [r1, #4]
	mov	r0, #0
	str	r0, [r1, #8]
	mul	r0, lr, r5
	add	r5, r5, #1
	uxth	r5, r5
	add	fp, r0, #3
	cmp	r0, #0
	movlt	r0, fp
	bic	r0, r0, #3
	add	r0, r6, r0
	str	r0, [r1, #12]
.L1782:
	add	r3, r3, #1
	add	r2, r2, #2
	uxth	r3, r3
.L1781:
	cmp	r3, r9
	bne	.L1783
	ldrb	r3, [r4, #8]	@ zero_extendqisi2
	cmp	r3, #1
	movne	r3, #0
	bne	.L1854
	ldr	r3, .L1863
	ldrb	r3, [r3, #928]	@ zero_extendqisi2
	adds	r3, r3, #0
	movne	r3, #1
.L1854:
	ldr	r6, .L1863+4
	mov	r1, r5
	str	r3, [sp, #20]
	mov	sl, #0
	ldr	r2, [sp, #20]
	ldr	r0, [r6, #-972]
	bl	FlashReadPages
	ldr	fp, [r6, #-1844]
	movw	ip, #65535
	str	ip, [sp, #4]
	str	fp, [sp, #8]
	b	.L1785
.L1790:
	mov	lr, #36
	ldr	r1, [r6, #-972]
	mul	r2, lr, sl
	add	r3, r1, r2
	ldr	r2, [r1, r2]
	cmp	r2, #0
	bne	.L1786
	ldr	r3, [r3, #12]
	ldr	fp, [r3, #4]
	cmn	fp, #1
	beq	.L1787
	ldr	r1, [r6, #-1844]
	mov	r0, fp
	str	r3, [sp, #0]
	bl	ftl_cmp_data_ver
	ldr	r3, [sp, #0]
	cmp	r0, #0
	addne	fp, fp, #1
	strne	fp, [r6, #-1844]
.L1787:
	ldr	r3, [r3, #0]
	cmn	r3, #1
	bne	.L1789
	b	.L1788
.L1786:
	ldr	r1, [r3, #4]
	movw	fp, #290
	ldr	r0, .L1863+8
	bl	printk
	ldrh	r3, [r4, #0]
	uxth	r2, r8
	str	r2, [sp, #4]
	strh	r3, [r6, fp]	@ movhi
.L1789:
	add	sl, sl, #1
.L1785:
	uxth	r9, sl
	cmp	r9, r5
	bcc	.L1790
.L1788:
	ldr	r3, .L1863+4
	cmp	r9, r5
	addeq	r7, r7, #1
	uxthne	r7, r7
	movne	r2, #36
	ldrne	r3, [r3, #-972]
	uxtheq	r7, r7
	ldreq	r3, [r3, #-972]
	mlane	r9, r2, r9, r3
	ldreq	r0, [r3, #4]
	ldrne	r0, [r9, #4]
	ubfx	r0, r0, #10, #16
	bl	P2V_plane
	ldrb	r2, [r4, #8]	@ zero_extendqisi2
	ldr	r3, .L1863
	cmp	r2, #1
	str	r0, [sp, #16]
	bne	.L1793
	ldrb	r1, [r3, #928]	@ zero_extendqisi2
	cmp	r1, #0
	addeq	r7, r3, r7, asl #1
	movweq	r1, #3076
	ldreqh	r7, [r7, r1]
.L1793:
	movw	r1, #3908
	ldr	ip, [sp, #12]
	ldrh	r3, [r3, r1]
	ldr	fp, [sp, #24]
	cmp	r3, r7
	streqh	r7, [r4, #2]	@ movhi
	moveq	r3, #0
	streqb	r3, [r4, #6]
	streqh	r3, [r4, #4]	@ movhi
	ldr	r3, [sp, #16]
	cmp	r7, ip
	cmpeq	r3, fp
	beq	.L1861
.L1795:
	ldr	fp, [sp, #4]
	movw	r3, #65535
	ldr	ip, [sp, #8]
	cmp	fp, r3
	sub	r6, ip, #1
	bne	.L1796
	cmp	r2, #0
	bne	.L1797
.L1796:
	ldr	r2, .L1863+4
	uxth	r3, r8
	ldr	ip, [sp, #12]
	mvn	fp, #0
	mov	sl, r3
	mov	r9, fp
	ldr	r1, [r2, #-1004]
	ldr	r8, .L1863+4
	cmn	r1, #1
	streq	r6, [r2, #-1004]
	ldr	r2, [r2, #-1004]
	str	r2, [sp, #4]
	add	r2, ip, #7
	cmp	r3, r2
	subgt	r5, r3, #7
	mov	r3, r7
	ldrle	r5, [sp, #12]
	mov	r7, r6
	uxthgt	r5, r5
	b	.L1800
.L1802:
	ldrh	r0, [r1, #16]
	movw	lr, #65535
	cmp	r0, lr
	beq	.L1801
	ldr	ip, [sp, #8]
	mov	r3, #36
	orr	r0, r5, r0, asl #10
	mla	lr, r3, r6, ip
	add	r6, r6, #1
	uxth	r6, r6
	str	r0, [lr, #4]
.L1801:
	add	r2, r2, #1
	add	r1, r1, #2
	uxth	r2, r2
.L1813:
	ldr	lr, [sp, #36]
	cmp	r2, lr
	bne	.L1802
	ldr	r3, [sp, #28]
	mov	r1, r6
	ldr	r0, [r8, #-972]
	ldr	r2, [sp, #20]
	str	r3, [sp, #0]
	bl	FlashReadPages
	ldr	r2, .L1863
	add	r0, r8, r5, asl #1
	mov	r1, #0
	str	r0, [sp, #8]
	ldr	r3, [sp, #0]
	ldrb	ip, [r2, #928]	@ zero_extendqisi2
	ldr	r2, [r8, #-972]
	str	ip, [sp, #28]
	b	.L1803
.L1811:
	ldr	r0, [r2, #0]
	cmp	r0, #0
	bne	.L1804
	ldr	r0, [r2, #12]
	movw	ip, #65535
	ldrh	lr, [r0, #0]
	cmp	lr, ip
	beq	.L1805
	ldr	r0, [r0, #4]
	cmn	r0, #1
	beq	.L1805
	cmn	fp, #1
	ldr	r9, [r8, #-1004]
	str	r0, [r8, #-1004]
	bne	.L1805
	ldr	lr, [sp, #8]
	sub	r0, lr, #896
	ldrh	r0, [r0, #0]
	cmp	r0, ip
	bne	.L1806
	ldr	r0, [sp, #28]
	cmp	r0, #0
	beq	.L1805
.L1806:
	cmp	r9, r7
	mvneq	fp, #0
	movne	fp, r9
	b	.L1805
.L1804:
	mov	r6, r7
	ldrh	r1, [r4, #0]
	mov	r7, r3
	ldr	r3, .L1863+4
	movw	r2, #290
	strh	r1, [r3, r2]	@ movhi
	ldrb	r2, [r4, #8]	@ zero_extendqisi2
	cmp	r2, #0
	bne	.L1797
	add	r5, r3, r5, asl #1
	movw	r2, #65535
	sub	r5, r5, #896
	ldrh	r1, [r5, #0]
	cmp	r1, r2
	bne	.L1807
	cmn	fp, #1
	bne	.L1856
.L1808:
	ldr	fp, [sp, #4]
	cmp	fp, r6
	beq	.L1809
.L1856:
	str	fp, [r3, #-1004]
	b	.L1797
.L1809:
	ldr	r2, [r3, #-1004]
	b	.L1862
.L1807:
	cmp	r9, r6
	beq	.L1810
	cmn	r9, #1
	strne	r9, [r3, #-1004]
	b	.L1797
.L1810:
	ldr	r2, [r3, #-1004]
	cmp	r2, r6
	beq	.L1797
.L1862:
	sub	r2, r2, #1
	b	.L1857
.L1805:
	add	r1, r1, #1
	add	r2, r2, #36
	uxth	r1, r1
.L1803:
	cmp	r1, r6
	bne	.L1811
	add	r5, r5, #1
	uxth	r5, r5
.L1800:
	cmp	r5, sl
	bhi	.L1812
	ldr	r1, .L1863
	mov	r2, #3840
	ldr	lr, [r8, #-972]
	mov	r6, #0
	str	r3, [sp, #28]
	ldrh	ip, [r1, r2]
	mov	r1, r4
	str	lr, [sp, #8]
	mov	r2, r6
	str	ip, [sp, #36]
	b	.L1813
.L1812:
	mov	r6, r7
	mov	r7, r3
	ldr	r3, .L1863+4
	mvn	r2, #0
.L1857:
	str	r2, [r3, #-1004]
.L1797:
	ldr	r5, .L1863+4
	mov	r3, #292
	mov	r2, #1
	ldr	r0, .L1863+12
	strh	r2, [r5, r3]	@ movhi
	bl	FtlMapBlkWriteDumpData
	ldr	r8, [sp, #12]
	str	r7, [sp, #8]
.L1814:
	ldr	r3, .L1863
	mov	r2, #3840
	mov	r7, #0
	ldr	lr, [r5, #-972]
	mov	fp, #36
	ldrh	sl, [r3, r2]
	mov	r2, r4
	ldrb	ip, [r3, #928]	@ zero_extendqisi2
	mov	r3, r7
	b	.L1815
.L1818:
	ldrh	r0, [r2, #16]
	movw	r1, #65535
	cmp	r0, r1
	beq	.L1816
	mla	r1, fp, r7, lr
	orr	r0, r8, r0, asl #10
	str	r0, [r1, #4]
	ldrb	r9, [r4, #8]	@ zero_extendqisi2
	cmp	r9, #1
	bne	.L1817
	cmp	ip, #0
	orrne	r0, r0, #-2147483648
	strne	r0, [r1, #4]
.L1817:
	add	r7, r7, #1
	uxth	r7, r7
.L1816:
	add	r3, r3, #1
	add	r2, r2, #2
	uxth	r3, r3
.L1815:
	cmp	r3, sl
	bne	.L1818
	mov	r1, r7
	ldr	r0, [r5, #-972]
	ldr	r2, [sp, #20]
	bl	FlashReadPages
	mov	r3, #36
	mul	r3, r3, r7
	mov	r7, #0
	str	r3, [sp, #36]
	b	.L1819
.L1840:
	ldr	r9, [r5, #-972]
	add	r9, r9, r7
	ldr	sl, [r9, #4]
	ubfx	r0, sl, #10, #16
	str	sl, [sp, #52]
	bl	P2V_plane
	ldr	r3, [sp, #12]
	cmp	r8, r3
	bcc	.L1820
	ldr	fp, [sp, #24]
	mov	ip, r3
	cmp	r0, fp
	movcs	r3, #0
	movcc	r3, #1
	cmp	r8, ip
	movne	r3, #0
	cmp	r3, #0
	bne	.L1820
	ldr	r3, [sp, #16]
	ldr	ip, [sp, #8]
	cmp	r0, r3
	cmpeq	r8, ip
	beq	.L1852
	ldr	r3, [r9, #0]
	cmn	r3, #1
	beq	.L1822
	ldr	sl, [r9, #12]
	movw	r3, #61589
	ldrh	r2, [sl, #0]
	cmp	r2, r3
	bne	.L1829
.L1823:
	ldr	r6, [sl, #4]
	cmn	r6, #1
	beq	.L1824
	ldr	r1, [r5, #-1844]
	mov	r0, r6
	bl	ftl_cmp_data_ver
	cmp	r0, #0
	addne	r3, r6, #1
	strne	r3, [r5, #-1844]
.L1824:
	ldr	r9, [sl, #8]
	add	r1, sp, #48
	ldr	r3, [sl, #12]
	mov	r2, #0
	mov	r0, r9
	str	r3, [sp, #44]
	bl	log2phys
	ldr	r1, [r5, #-1004]
	cmn	r1, #1
	beq	.L1825
	mov	r0, r6
	bl	ftl_cmp_data_ver
	cmp	r0, #0
	beq	.L1825
	ldr	r3, [sp, #44]
	cmn	r3, #1
	beq	.L1826
	ldr	r0, [r5, #-972]
	mov	r1, #1
	mov	r2, #0
	add	r0, r0, r7
	ldr	fp, [r0, #12]
	str	r3, [r0, #4]
	str	fp, [sp, #4]
	bl	FlashReadPages
	ldr	r3, [r5, #-972]
	ldr	ip, [fp, #4]
	add	fp, r3, r7
	ldr	r3, [r3, r7]
	str	ip, [sp, #28]
	cmn	r3, #1
	bne	.L1827
	b	.L1828
.L1826:
	ldr	r3, [sp, #52]
	ldr	r2, [sp, #48]
	cmp	r2, r3
	bne	.L1829
	mov	r0, r9
	add	r1, sp, #44
	mov	r2, #1
	bl	log2phys
.L1829:
	ldrh	r0, [r4, #0]
	b	.L1859
.L1827:
	ldr	r3, [sp, #4]
	ldr	sl, [r3, #8]
	cmp	sl, r9
	bne	.L1828
	ldr	r0, [r5, #-1004]
	ldr	r1, [sp, #28]
	bl	ftl_cmp_data_ver
	cmp	r0, #0
	beq	.L1828
	ldr	r3, [sp, #48]
	ldr	r2, [sp, #52]
	cmp	r3, r2
	beq	.L1833
.L1830:
	ldr	r2, [sp, #44]
	cmp	r3, r2
	beq	.L1828
	cmn	r3, #1
	streq	r3, [fp, #0]
	beq	.L1832
	ldr	ip, [fp, #12]
	mov	r0, fp
	str	r3, [fp, #4]
	mov	r1, #1
	mov	r2, #0
	str	ip, [sp, #4]
	bl	FlashReadPages
.L1832:
	ldr	r3, [r5, #-972]
	ldr	r3, [r3, r7]
	cmn	r3, #1
	beq	.L1833
	ldr	r3, [sp, #4]
	ldr	r0, [r5, #-1004]
	ldr	r9, [r3, #4]
	mov	r1, r9
	bl	ftl_cmp_data_ver
	cmp	r0, #0
	beq	.L1833
	ldr	r0, [sp, #28]
	mov	r1, r9
	bl	ftl_cmp_data_ver
	cmp	r0, #0
	beq	.L1828
.L1833:
	mov	r0, sl
	ldr	r1, [sp, #44]
	bl	FtlReUsePrevPpa
.L1828:
	ldrh	r0, [r4, #0]
	mvn	r3, #0
	str	r3, [sp, #44]
	bl	decrement_vpc_count
	b	.L1835
.L1825:
	ldr	r3, [sp, #52]
	ldr	r2, [sp, #48]
	cmp	r2, r3
	beq	.L1835
	mov	r0, r9
	add	r1, sp, #52
	mov	r2, #1
	bl	log2phys
	ldr	fp, [sp, #48]
	cmn	fp, #1
	beq	.L1835
	ldr	r3, [sp, #44]
	cmp	fp, r3
	beq	.L1835
	ubfx	r0, fp, #10, #16
	bl	P2V_block_in_plane
	ldr	r3, .L1863+16
	ldrh	r3, [r5, r3]
	cmp	r3, r0
	beq	.L1836
	ldr	r3, .L1863+20
	ldrh	r3, [r5, r3]
	cmp	r3, r0
	beq	.L1836
	ldr	r3, .L1863+24
	ldrh	r3, [r5, r3]
	cmp	r3, r0
	bne	.L1835
.L1836:
	ldr	ip, .L1863+4
	mov	r1, #1
	mov	r2, #0
	ldr	r0, [ip, #-972]
	str	fp, [r0, #4]
	ldr	sl, [r0, #12]
	bl	FlashReadPages
	ldr	r0, .L1863+4
	ldr	r1, [sl, #4]
	ldr	r3, [r0, #-972]
	ldr	r3, [r3, #0]
	cmn	r3, #1
	beq	.L1835
	mov	r0, r6
	bl	ftl_cmp_data_ver
	cmp	r0, #0
	bne	.L1835
	mov	r0, r9
	add	r1, sp, #48
	mov	r2, #1
	bl	log2phys
.L1835:
	ldr	r0, [sp, #44]
	cmn	r0, #1
	beq	.L1820
	ubfx	r0, r0, #10, #16
	bl	P2V_block_in_plane
	ldr	r2, [r5, #-2064]
	mov	r3, r0, asl #1
	mov	r1, r0
	ldrh	r3, [r2, r3]
	cmp	r3, #0
	beq	.L1837
.L1859:
	bl	decrement_vpc_count
	b	.L1820
.L1837:
	ldr	r0, .L1863+28
	bl	printk
	b	.L1820
.L1822:
	ldrh	r3, [r4, #0]
	movw	r1, #290
	mov	r2, r6
	ldr	r0, .L1863+32
	strh	r3, [r5, r1]	@ movhi
	mov	r1, sl
	bl	printk
	ldr	r3, [r5, #296]
	cmp	r3, #31
	addls	r2, r5, r3, asl #2
	addls	r3, r3, #1
	ldrls	r1, [sp, #52]
	strls	r3, [r5, #296]
	strls	r1, [r2, #300]
	ldrh	r0, [r4, #0]
	bl	decrement_vpc_count
	ldr	r3, [r5, #-1004]
	cmn	r3, #1
	beq	.L1858
.L1839:
	cmp	r3, r6
	bls	.L1820
.L1858:
	str	r6, [r5, #-1004]
.L1820:
	add	r7, r7, #36
.L1819:
	ldr	r3, [sp, #36]
	cmp	r7, r3
	bne	.L1840
	ldrb	r3, [r4, #8]	@ zero_extendqisi2
	add	r8, r8, #1
	cmp	r3, #1
	uxth	r8, r8
	bne	.L1841
	ldr	r3, .L1863
	ldrb	r2, [r3, #928]	@ zero_extendqisi2
	cmp	r2, #0
	beq	.L1841
	movw	r2, #3910
	ldr	ip, [sp, #8]
	ldrh	r3, [r3, r2]
	cmp	ip, r8
	cmpeq	r3, r8
	beq	.L1852
.L1841:
	ldr	r3, .L1863
	movw	r2, #3908
	ldrh	r2, [r3, r2]
	cmp	r8, r2
	bne	.L1814
	mov	r2, #0
	mov	r1, #3840
	strh	r2, [r4, #4]	@ movhi
	ldrh	r1, [r3, r1]
	mov	r3, r2
	strh	r8, [r4, #2]	@ movhi
	movw	r2, #65535
	ldr	r0, [sp, #32]
	b	.L1842
.L1844:
	add	r0, r0, #2
	ldrh	ip, [r0, #14]
	cmp	ip, r2
	strneb	r3, [r4, #6]
	bne	.L1774
.L1843:
	add	r3, r3, #1
	uxth	r3, r3
.L1842:
	cmp	r3, r1
	bne	.L1844
	b	.L1774
.L1852:
.L1821:
	ldr	r7, [sp, #8]
	ldr	r3, [sp, #16]
	strh	r7, [r4, #2]	@ movhi
	strb	r3, [r4, #6]
.L1861:
	mov	r0, r4
	mov	r1, r7
	mov	r2, r3
	bl	ftl_sb_update_avl_pages
.L1774:
	mov	r0, #0
	add	sp, sp, #60
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, sl, fp, pc}
.L1864:
	.align	2
.L1863:
	.word	.LANCHOR0
	.word	.LANCHOR2
	.word	.LC122
	.word	.LANCHOR2-1084
	.word	-2044
	.word	-1996
	.word	-1948
	.word	.LC123
	.word	.LC124
	.fnend
	.size	FtlRecoverySuperblock, .-FtlRecoverySuperblock
	.align	2
	.global	FtlWriteDumpData
	.type	FtlWriteDumpData, %function
FtlWriteDumpData:
	.fnstart
	@ args = 0, pretend = 0, frame = 40
	@ frame_needed = 0, uses_anonymous_args = 0
	stmfd	sp!, {r4, r5, r6, r7, r8, sl, lr}
	.save {r4, r5, r6, r7, r8, sl, lr}
	.pad #44
	sub	sp, sp, #44
	ldr	r4, .L1874
	ldr	r3, [r4, #-2092]
	mov	r2, r4
	cmp	r3, #0
	bne	.L1865
	ldr	r3, .L1874+4
	ldrh	r3, [r4, r3]
	cmp	r3, #0
	beq	.L1867
	ldrb	r1, [r4, #-2036]	@ zero_extendqisi2
	cmp	r1, #0
	bne	.L1867
	ldr	r5, .L1874+8
	movw	r0, #3908
	ldrb	r1, [r4, #-2037]	@ zero_extendqisi2
	ldrh	r0, [r5, r0]
	mul	r1, r0, r1
	cmp	r3, r1
	beq	.L1867
	ldrb	r8, [r4, #-2034]	@ zero_extendqisi2
	mov	r3, #3840
	ldr	r7, [r5, #3968]
	cmp	r8, #0
	ldrh	r6, [r5, r3]
	bne	.L1865
	sub	r7, r7, #1
	mov	r1, sp
	mov	r2, r8
	mov	r0, r7
	bl	log2phys
	ldr	r3, [sp, #0]
	ldr	r0, [r4, #-2076]
	ldr	r4, [r4, #-940]
	cmn	r3, #1
	str	r3, [sp, #8]
	str	r7, [sp, #20]
	str	r0, [sp, #12]
	str	r4, [sp, #16]
	str	r8, [r4, #4]
	beq	.L1868
	add	r0, sp, #4
	mov	r1, #1
	mov	r2, r8
	bl	FlashReadPages
	b	.L1869
.L1868:
	movw	r3, #3918
	mov	r1, #255
	ldrh	r2, [r5, r3]
	bl	ftl_memset
.L1869:
	ldr	r3, .L1874+12
	mov	r6, r6, asl #2
	ldr	r5, .L1874
	ldr	sl, .L1874+4
	strh	r3, [r4, #0]	@ movhi
	ldr	r8, .L1874+16
	b	.L1870
.L1873:
	ldrh	r3, [r5, sl]
	cmp	r3, #0
	beq	.L1871
	ldr	r3, [sp, #8]
	sub	r6, r6, #1
	str	r7, [r4, #8]
	ldr	r0, .L1874+20
	str	r3, [r4, #12]
	ldrh	r3, [r5, r8]
	strh	r3, [r4, #2]	@ movhi
	bl	get_new_active_ppa
	ldr	r3, [r5, #-1844]
	mov	r2, #0
	mov	r1, #1
	str	r0, [sp, #8]
	add	r0, sp, #4
	str	r3, [r4, #4]
	add	r3, r3, #1
	cmn	r3, #1
	str	r3, [r5, #-1844]
	moveq	r3, #0
	streq	r3, [r5, #-1844]
	mov	r3, r2
	bl	FlashProgPages
	ldrh	r0, [r5, r8]
	bl	decrement_vpc_count
.L1870:
	cmp	r6, #0
	bne	.L1873
.L1871:
	ldr	r3, .L1874
	mov	r2, #1
	strb	r2, [r3, #-2034]
	b	.L1865
.L1867:
	mov	r3, #0
	strb	r3, [r2, #-2034]
.L1865:
	add	sp, sp, #44
	ldmfd	sp!, {r4, r5, r6, r7, r8, sl, pc}
.L1875:
	.align	2
.L1874:
	.word	.LANCHOR2
	.word	-2040
	.word	.LANCHOR0
	.word	-3947
	.word	-2044
	.word	.LANCHOR2-2044
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
	ldr	r7, .L1880
	movw	r6, #3946
	ldr	r5, .L1880+4
	b	.L1877
.L1879:
	ldr	r3, [r5, #-1900]
	mov	r2, #12
	mla	r3, r2, r4, r3
	ldr	r3, [r3, #4]
	cmp	r3, #0
	bge	.L1878
	mov	r0, r4
	bl	flush_l2p_region
.L1878:
	add	r4, r4, #1
	uxth	r4, r4
.L1877:
	ldrh	r3, [r7, r6]
	cmp	r3, r4
	bhi	.L1879
	mov	r0, #0
	ldmfd	sp!, {r3, r4, r5, r6, r7, pc}
.L1881:
	.align	2
.L1880:
	.word	.LANCHOR0
	.word	.LANCHOR2
	.fnend
	.size	l2p_flush, .-l2p_flush
	.align	2
	.global	FtlVpcCheckAndModify
	.type	FtlVpcCheckAndModify, %function
FtlVpcCheckAndModify:
	.fnstart
	@ args = 0, pretend = 0, frame = 8
	@ frame_needed = 0, uses_anonymous_args = 0
	stmfd	sp!, {r0, r1, r4, r5, r6, r7, r8, lr}
	.save {r0, r1, r4, r5, r6, r7, r8, lr}
	mov	r4, #0
	ldr	r6, .L1889
	ldr	r1, .L1889+4
	ldr	r0, .L1889+8
	bl	printk
	movw	r3, #3850
	ldrh	r2, [r6, r3]
	mov	r1, #0
	ldr	r5, .L1889+12
	mov	r2, r2, asl #1
	ldr	r0, [r5, #-920]
	bl	ftl_memset
	b	.L1883
.L1885:
	mov	r0, r4
	add	r1, sp, #4
	mov	r2, #0
	bl	log2phys
	ldr	r0, [sp, #4]
	cmn	r0, #1
	beq	.L1884
	ubfx	r0, r0, #10, #16
	bl	P2V_block_in_plane
	ldr	r3, [r5, #-920]
	mov	r0, r0, asl #1
	ldrh	r2, [r3, r0]
	add	r2, r2, #1
	strh	r2, [r3, r0]	@ movhi
.L1884:
	add	r4, r4, #1
.L1883:
	ldr	r3, [r6, #3968]
	cmp	r4, r3
	bcc	.L1885
	mov	r4, #0
	ldr	r7, .L1889
	movw	r8, #3848
	ldr	r5, .L1889+12
	b	.L1886
.L1888:
	ldr	r3, [r5, #-2064]
	mov	r6, r4, asl #1
	ldrh	r2, [r3, r6]
	ldr	r3, [r5, #-920]
	ldrh	r3, [r3, r6]
	cmp	r2, r3
	beq	.L1887
	movw	r1, #65535
	cmp	r2, r1
	beq	.L1887
	ldr	r0, .L1889+16
	mov	r1, r4
	bl	printk
	ldr	r3, .L1889+20
	ldrh	r3, [r5, r3]
	cmp	r3, r4
	beq	.L1887
	ldr	r3, .L1889+24
	ldrh	r3, [r5, r3]
	cmp	r3, r4
	beq	.L1887
	ldr	r3, .L1889+28
	ldrh	r3, [r5, r3]
	cmp	r3, r4
	beq	.L1887
	ldr	r3, [r5, #-920]
	mov	r0, r4
	ldrh	r2, [r3, r6]
	ldr	r3, [r5, #-2064]
	strh	r2, [r3, r6]	@ movhi
	bl	update_vpc_list
	bl	l2p_flush
	bl	FtlVpcTblFlush
.L1887:
	add	r4, r4, #1
	uxth	r4, r4
.L1886:
	ldrh	r3, [r7, r8]
	cmp	r3, r4
	bhi	.L1888
	ldmfd	sp!, {r2, r3, r4, r5, r6, r7, r8, pc}
.L1890:
	.align	2
.L1889:
	.word	.LANCHOR0
	.word	.LANCHOR3+15
	.word	.LC125
	.word	.LANCHOR2
	.word	.LC126
	.word	-2044
	.word	-1948
	.word	-1996
	.fnend
	.size	FtlVpcCheckAndModify, .-FtlVpcCheckAndModify
	.align	2
	.global	allocate_new_data_superblock
	.type	allocate_new_data_superblock, %function
allocate_new_data_superblock:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	ldr	r3, .L1903
	stmfd	sp!, {r4, r5, r6, lr}
	.save {r4, r5, r6, lr}
	mov	r4, r0
	ldr	r2, [r3, #-2092]
	ldrh	r5, [r0, #0]
	cmp	r2, #0
	bne	.L1892
	movw	r2, #65535
	cmp	r5, r2
	beq	.L1893
	ldr	r2, [r3, #-2064]
	mov	r3, r5, asl #1
	mov	r0, r5
	ldrh	r3, [r2, r3]
	cmp	r3, #0
	beq	.L1894
	bl	INSERT_DATA_LIST
	b	.L1893
.L1894:
	bl	INSERT_FREE_LIST
.L1893:
	mov	r3, #0
	strb	r3, [r4, #8]
	ldr	r3, .L1903+4
	cmp	r4, r3
	beq	.L1895
	ldr	r2, .L1903+8
	movw	r3, #3860
	ldrh	r3, [r2, r3]
	cmp	r3, #1
	beq	.L1895
	ldrb	r2, [r2, #928]	@ zero_extendqisi2
	cmp	r2, #0
	beq	.L1896
.L1895:
	mov	r3, #1
	strb	r3, [r4, #8]
	b	.L1897
.L1896:
	ldr	r2, .L1903+12
	cmp	r4, r2
	bne	.L1897
	cmp	r3, #3
	ldr	r3, .L1903
	beq	.L1898
	ldr	r2, [r3, #-1708]
	cmp	r2, #1
	bne	.L1899
.L1898:
	mov	r2, #1
	strb	r2, [r3, #-2036]
.L1899:
	ldr	r3, [r3, #-1884]
	ldr	r2, .L1903
	cmp	r3, #0
	beq	.L1897
	ldr	r3, [r2, #-1824]
	cmp	r3, #29
	movls	r3, #1
	strlsb	r3, [r2, #-2036]
.L1897:
	ldr	r3, .L1903+16
	ldr	r6, .L1903
	ldrh	r0, [r6, r3]
	movw	r3, #65535
	cmp	r0, r3
	beq	.L1900
	cmp	r5, r0
	bne	.L1901
	ldr	r2, [r6, #-2064]
	mov	r3, r0, asl #1
	ldrh	r3, [r2, r3]
	cmp	r3, #0
	beq	.L1902
.L1901:
	bl	update_vpc_list
.L1902:
	ldr	r3, .L1903+16
	mvn	r2, #0
	strh	r2, [r6, r3]	@ movhi
.L1900:
	mov	r0, r4
	bl	allocate_data_superblock
	bl	l2p_flush
	mov	r0, #0
	bl	FtlEctTblFlush
	bl	FtlVpcTblFlush
.L1892:
	mov	r0, #0
	ldmfd	sp!, {r4, r5, r6, pc}
.L1904:
	.align	2
.L1903:
	.word	.LANCHOR2
	.word	.LANCHOR2-1996
	.word	.LANCHOR0
	.word	.LANCHOR2-2044
	.word	-1016
	.fnend
	.size	allocate_new_data_superblock, .-allocate_new_data_superblock
	.align	2
	.type	rk_ftl_garbage_collect.part.22, %function
rk_ftl_garbage_collect.part.22:
	.fnstart
	@ args = 0, pretend = 0, frame = 24
	@ frame_needed = 0, uses_anonymous_args = 0
	ldr	r3, .L2002
	movw	ip, #65535
	ldr	r2, .L2002+4
	stmfd	sp!, {r4, r5, r6, r7, r8, r9, sl, fp, lr}
	.save {r4, r5, r6, r7, r8, r9, sl, fp, lr}
	.pad #36
	sub	sp, sp, #36
	mov	sl, r1
	str	r0, [sp, #16]
	ldrh	r0, [r3, r2]
	cmp	r0, ip
	beq	.L1906
	ldr	r1, .L2002+8
	ldrh	r4, [r3, r1]
	cmp	r4, ip
	streqh	r0, [r3, r1]	@ movhi
	mvneq	r1, #0
	streqh	r1, [r3, r2]	@ movhi
.L1906:
	ldr	r1, [sp, #16]
	ldr	r2, [r3, #-1188]
	cmp	r1, #1
	add	r2, r2, #1
	add	r2, r2, r1, asl #7
	str	r2, [r3, #-1188]
	bne	.L1907
	ldr	r3, .L2002
	ldr	r3, [r3, #-1884]
	cmp	r3, #0
	bne	.L1908
	ldr	r3, .L2002+12
	ldrb	r3, [r3, #928]	@ zero_extendqisi2
	cmp	r3, #0
	beq	.L1907
.L1908:
	ldr	r4, .L2002
	ldr	r3, [r4, #-1824]
	cmp	r3, #29
	bhi	.L1907
	mov	r3, #428
	ldrh	r3, [r4, r3]
	add	r2, r2, r3
	str	r2, [r4, #-1188]
	bl	FtlGcReFreshBadBlk
	ldr	r3, .L2002+16
	movw	r2, #65535
	ldrh	r3, [r4, r3]
	cmp	r3, r2
	bne	.L1907
	ldr	r2, .L2002+8
	ldrh	r2, [r4, r2]
	cmp	r2, r3
	bne	.L1907
	ldr	r3, [r4, #-1188]
	cmp	r3, #1024
	bhi	.L1909
	ldr	r3, .L2002+20
	ldrh	r3, [r4, r3]
	cmp	r3, #63
	bhi	.L1907
.L1909:
	ldr	r3, .L2002
	mov	r2, #428
	ldr	r0, .L2002+20
	mov	r1, #0
	strh	r1, [r3, r2]	@ movhi
	ldrh	ip, [r3, r0]
	ldr	r0, .L2002+24
	ldrh	r0, [r3, r0]
	add	r0, r0, #64
	cmp	ip, r0
	bgt	.L1907
	str	r1, [r3, #-1188]
	ldr	r1, [r3, #-1824]
	cmp	r1, #0
	moveq	r1, #6
	beq	.L1998
	cmp	r1, #5
	bhi	.L1911
	mov	r1, #18
.L1998:
	strh	r1, [r3, r2]	@ movhi
.L1911:
	mov	r0, #32
	movw	r7, #65535
	bl	List_get_gc_head_node
	uxth	r2, r0
	cmp	r2, r7
	beq	.L1912
	ldr	r5, .L2002
	ldr	r6, .L2002+28
	ldrh	r0, [r5, r6]
	cmp	r0, #0
	moveq	r3, #1
	streqh	r3, [r5, r6]	@ movhi
	beq	.L1912
	ldr	r3, .L2002+12
	movw	ip, #3910
	mov	lr, #3840
	ldr	r9, [r5, #-2064]
	mov	r2, r2, asl #1
	ldrh	ip, [r3, ip]
	ldrh	r3, [r3, lr]
	ldrh	r1, [r9, r2]
	mul	r3, r3, ip
	add	r3, r3, #1
	cmp	r1, r3
	bgt	.L1912
	add	fp, r0, #1
	mov	r8, #0
	str	r8, [r5, #-1180]
	uxth	fp, fp
	strh	fp, [r5, r6]	@ movhi
	str	r2, [sp, #8]
	bl	List_get_gc_head_node
	ldr	r2, [sp, #8]
	uxth	r4, r0
	cmp	r4, r7
	beq	.L1912
	ldrh	r2, [r9, r2]
	mov	r7, r4, asl #1
	ldr	r0, .L2002+32
	mov	r1, fp
	ldrh	r3, [r9, r7]
	str	r2, [sp, #0]
	mov	r2, r4
	bl	printk
	ldrh	r3, [r5, r6]
	cmp	r3, #40
	bls	.L1914
	ldr	r3, [r5, #-2064]
	ldrh	r3, [r3, r7]
	cmp	r3, #32
	strhih	r8, [r5, r6]	@ movhi
.L1914:
	ldr	r2, .L2002
	mov	r3, #428
	mov	r1, #6
	strh	r1, [r2, r3]	@ movhi
	b	.L1916
.L1912:
	bl	GetSwlReplaceBlock
	movw	r3, #65535
	cmp	r0, r3
	mov	r4, r0
	bne	.L1916
	ldr	r2, .L2002
	mov	r3, #428
	mov	r1, #0
	strh	r1, [r2, r3]	@ movhi
.L1907:
	ldr	r6, .L2002
	movw	r4, #65535
	ldr	r3, .L2002+16
	ldrh	r2, [r6, r3]
	cmp	r2, r4
	bne	.L1916
	sub	r3, r3, #192
	ldrh	r3, [r6, r3]
	cmp	r3, r2
	movne	r4, r2
	bne	.L1916
	ldr	r2, .L2002+8
	ldrh	r7, [r6, r2]
	cmp	r7, r3
	movne	r4, r3
	bne	.L1916
	ldr	r8, .L2002+20
	ldr	r2, [r6, #-1188]
	ldrh	r3, [r6, r8]
	cmp	r3, #23
	movhi	r3, #1024
	movls	r3, #5120
	cmp	r2, r3
	movls	r4, r7
	bls	.L1916
	mov	r2, #428
	mov	r3, #0
	str	r3, [r6, #-1188]
	strh	r3, [r6, r2]	@ movhi
	bl	GetSwlReplaceBlock
	cmp	r0, r7
	mov	r4, r0
	bne	.L1918
	ldr	r3, .L2002+24
	ldrh	r1, [r6, r8]
	ldrh	r2, [r6, r3]
	cmp	r1, r2
	movcs	r2, #80
	strcsh	r2, [r6, r3]	@ movhi
	bcs	.L1928
	mov	r0, #64
	bl	List_get_gc_head_node
	uxth	r3, r0
	cmp	r3, r4
	beq	.L1928
	ldr	r2, [r6, #-996]
	cmp	r2, #0
	bne	.L1921
	ldr	r2, .L2002+12
	movw	r1, #3860
	ldrh	r1, [r2, r1]
	cmp	r1, #3
	beq	.L1921
	ldr	r1, [r6, #-1708]
	cmp	r1, #0
	bne	.L1921
	ldr	r1, [r6, #-1884]
	cmp	r1, #0
	bne	.L1921
	ldrb	r0, [r2, #928]	@ zero_extendqisi2
	cmp	r0, #0
	beq	.L1922
.L1921:
	ldr	r1, .L2002
	mov	r3, r3, asl #1
	mov	ip, #3840
	movw	lr, #3860
	ldr	r2, [r1, #-2064]
	ldrh	r0, [r2, r3]
	movw	r2, #3910
	ldr	r3, .L2002+12
	ldrh	r2, [r3, r2]
	ldrh	ip, [r3, ip]
	ldrh	r3, [r3, lr]
	mul	ip, ip, r2
	cmp	r3, #3
	moveq	r3, r2, lsr #1
	movne	r3, #0
	add	r3, ip, r3
	cmp	r0, r3
	bgt	.L1924
	mov	r0, #0
	bl	List_get_gc_head_node
	ldr	r2, .L2002+12
	ldr	r3, .L2002
	ldr	r2, [r2, #3968]
	ldr	r1, [r3, #-1892]
	add	r2, r2, r2, asl #1
	cmp	r1, r2, lsr #2
	ldr	r2, .L2002+24
	movhi	r1, #128
	movls	r1, #160
	strh	r1, [r3, r2]	@ movhi
	uxth	r4, r0
	b	.L1926
.L1924:
	ldr	r3, .L2002+24
	mov	r2, #128
	strh	r2, [r1, r3]	@ movhi
	b	.L1928
.L1922:
	ldr	r2, [r6, #-2064]
	mov	r3, r3, asl #1
	ldr	r6, .L2002
	ldr	r5, .L2002+24
	ldrh	r3, [r2, r3]
	cmp	r3, #7
	movhi	r3, #64
	strhih	r3, [r6, r5]	@ movhi
	bhi	.L1928
	bl	List_get_gc_head_node
	mov	r3, #128
	strh	r3, [r6, r5]	@ movhi
	uxth	r4, r0
.L1926:
	movw	r3, #65535
	cmp	r4, r3
	beq	.L1928
.L1918:
	ldr	r1, .L2002
	mov	r0, r4, asl #1
	ldr	r3, .L2002+20
	ldr	ip, [r1, #-2084]
	ldrh	r2, [r1, r3]
	ldr	r3, [r1, #-2064]
	ldrh	r3, [r3, r0]
	ldrh	r0, [ip, r0]
	str	r0, [sp, #0]
	ldr	r0, .L2002+36
	ldrh	r1, [r1, r0]
	ldr	r0, .L2002+40
	str	r1, [sp, #4]
	mov	r1, r4
	bl	printk
.L1928:
	bl	FtlGcReFreshBadBlk
.L1916:
	movw	r0, #65535
	rsb	ip, r0, r4
	rsbs	r1, ip, #0
	ldr	r3, [sp, #16]
	adc	r1, r1, ip
	ldr	r5, .L2002
	cmp	r3, #0
	movne	r2, #0
	andeq	r2, r1, #1
	cmp	r2, #0
	beq	.L1929
	ldr	r3, .L2002+20
	ldrh	r2, [r5, r3]
	cmp	r2, #24
	movhi	r6, #1
	bhi	.L1930
	ldr	r1, .L2002+12
	movw	r3, #3908
	cmp	r2, #16
	ldrh	r6, [r1, r3]
	movhi	r6, r6, lsr #5
	bhi	.L1930
	cmp	r2, #12
	movhi	r6, r6, lsr #4
	bhi	.L1930
	cmp	r2, #8
	movhi	r6, r6, lsr #2
.L1930:
	ldr	r1, .L2002+36
	ldr	r3, .L2002
	ldrh	r0, [r5, r1]
	cmp	r0, r2
	mov	r0, r1
	bcs	.L1934
	ldr	r2, .L2002+44
	movw	ip, #65535
	ldrh	r2, [r3, r2]
	cmp	r2, ip
	bne	.L1935
	ldr	ip, .L2002+8
	ldrh	ip, [r3, ip]
	cmp	ip, r2
	bne	.L1935
	mov	r2, #428
	ldrh	r0, [r3, r2]
	cmp	r0, #0
	bne	.L1936
	ldr	r2, .L2002+12
	ldr	ip, [r3, #-1892]
	ldr	r2, [r2, #3968]
	add	r2, r2, r2, asl #1
	cmp	ip, r2, lsr #2
	movcs	r2, #18
	bcs	.L2000
.L1936:
	ldr	r3, .L2002
	ldr	r2, .L2002+48
	ldr	r1, .L2002+36
	ldrh	r2, [r3, r2]
	add	r2, r2, r2, asl #1
	mov	r2, r2, asr #2
.L2000:
	strh	r2, [r3, r1]	@ movhi
	mov	r2, #0
	ldr	r3, .L2002
	str	r2, [r3, #-1180]
	b	.L1939
.L1935:
	ldr	r3, .L2002
	ldr	r2, .L2002+48
	ldrh	r2, [r3, r2]
	add	r2, r2, r2, asl #1
	mov	r2, r2, asr #2
	strh	r2, [r3, r0]	@ movhi
.L1934:
	ldr	r3, .L2002
	cmp	sl, #2
	movw	r4, #65535
	movhi	sl, #0
	movls	sl, #1
	ldr	r3, [r3, #-1884]
	cmp	r3, #0
	moveq	sl, #0
	cmp	sl, #0
	addne	r6, r6, #1
	uxthne	r6, r6
	b	.L1940
.L1929:
	ldr	r3, .L2002+44
	ldrh	r3, [r5, r3]
	cmp	r3, r0
	bne	.L1941
	ldr	r0, .L2002+8
	ldrh	r0, [r5, r0]
	cmp	r0, r3
	movne	r1, #0
	andeq	r1, r1, #1
	cmp	r1, #0
	beq	.L1941
	ldr	r1, .L2002+16
	ldrh	r4, [r5, r1]
	cmp	r4, r3
	movne	r4, r3
	bne	.L1941
	ldr	r3, .L2002+20
	mov	r6, #428
	str	r2, [r5, #-1180]
	ldrh	r7, [r5, r3]
	add	r3, r3, #856
	ldrh	r2, [r5, r3]
	cmp	r2, r7
	bcs	.L1942
	ldrh	r2, [r5, r6]
	cmp	r2, #0
	bne	.L1943
	ldr	r2, .L2002+12
	ldr	r1, [r5, #-1892]
	ldr	r2, [r2, #3968]
	add	r2, r2, r2, asl #1
	cmp	r1, r2, lsr #2
	movcs	r2, #18
	strcsh	r2, [r5, r3]	@ movhi
	bcs	.L1945
.L1943:
	ldr	r3, .L2002
	ldr	r2, .L2002+48
	ldr	r1, .L2002+36
	ldrh	r2, [r3, r2]
	add	r2, r2, r2, asl #1
	mov	r2, r2, asr #2
	strh	r2, [r3, r1]	@ movhi
.L1945:
	bl	FtlReadRefresh
	mov	r3, #428
	ldr	r2, .L2002
	b	.L2001
.L1942:
	ldrh	r0, [r5, r6]
	cmp	r0, #0
	bne	.L1941
	ldr	r2, .L2002+48
	ldrh	r4, [r5, r2]
	add	r2, r4, r4, asl #1
	mov	r2, r2, asr #2
	strh	r2, [r5, r3]	@ movhi
	bl	List_get_gc_head_node
	ldr	r3, [r5, #-2064]
	ldr	r1, .L2002+12
	movw	r2, #3910
	ldrh	r2, [r1, r2]
	uxth	r0, r0
	mov	r0, r0, asl #1
	ldrh	r3, [r3, r0]
	mov	r0, #3840
	ldrh	r1, [r1, r0]
	mul	r2, r1, r2
	add	r2, r2, r2, lsr #31
	cmp	r3, r2, asr #1
	ble	.L1946
	sub	r4, r4, #1
	cmp	r7, r4
	blt	.L1946
	bl	FtlReadRefresh
	ldrh	r0, [r5, r6]
	b	.L1939
.L1946:
	cmp	r3, #0
	movwne	r4, #65535
	bne	.L1941
	movw	r0, #65535
	bl	decrement_vpc_count
	ldr	r3, .L2002+20
	ldr	r2, .L2002
	ldrh	r0, [r2, r3]
	add	r0, r0, #1
	b	.L1939
.L1941:
	ldr	r3, .L2002
	ldr	r6, [r3, #-1884]
	cmp	r6, #0
	movne	r6, #2
	moveq	r6, #1
.L1940:
	ldr	r3, .L2002
	movw	r0, #65535
	ldr	r2, .L2002+16
	ldrh	r1, [r3, r2]
	cmp	r1, r0
	bne	.L1947
	cmp	r4, r1
	strneh	r4, [r3, r2]	@ movhi
	bne	.L1949
	ldr	r2, .L2002+8
	ldrh	r1, [r3, r2]
	cmp	r1, r4
	beq	.L1949
	ldr	r0, [r3, #-2064]
	mov	r1, r1, asl #1
	ldrh	r1, [r0, r1]
	cmp	r1, #0
	mvneq	r1, #0
	streqh	r1, [r3, r2]	@ movhi
	ldr	r3, .L2002
	ldr	r1, .L2002+16
	ldrh	r0, [r3, r2]
	strh	r0, [r3, r1]	@ movhi
	mvn	r1, #0
	strh	r1, [r3, r2]	@ movhi
.L1949:
	ldr	r5, .L2002
	mov	r3, #0
	ldr	r7, .L2002+16
	strb	r3, [r5, #-1748]
	movw	r3, #65535
	ldrh	r0, [r5, r7]
	cmp	r0, r3
	beq	.L1947
	bl	IsBlkInGcList
	cmp	r0, #0
	mvnne	r3, #0
	strneh	r3, [r5, r7]	@ movhi
	ldr	r3, .L2002+12
	ldr	r7, .L2002
	ldrb	r3, [r3, #928]	@ zero_extendqisi2
	cmp	r3, #0
	beq	.L1952
	ldr	r3, .L2002+16
	ldrh	r0, [r7, r3]
	bl	ftl_get_blk_mode
	strb	r0, [r7, #-1748]
.L1952:
	ldr	r8, .L2002+16
	movw	r3, #65535
	ldr	r5, .L2002
	ldrh	r2, [r7, r8]
	cmp	r2, r3
	beq	.L1947
	ldr	r0, .L2002+52
	bl	make_superblock
	ldr	r1, .L2002+56
	movw	r2, #430
	mov	r3, #0
	strh	r3, [r5, r2]	@ movhi
	strh	r3, [r5, r1]	@ movhi
	strb	r3, [r5, #-1750]
	ldrh	r3, [r5, r8]
	ldr	r2, [r5, #-2064]
	mov	r3, r3, asl #1
	ldrh	r2, [r2, r3]
	mov	r3, #432
	strh	r2, [r5, r3]	@ movhi
.L1947:
	ldr	r3, .L2002
	ldr	r0, .L2002+16
	ldr	r2, .L2002+60
	ldrh	r1, [r3, r0]
	ldrh	r2, [r3, r2]
	cmp	r2, r1
	mov	r2, r3
	beq	.L1953
	ldr	ip, .L2002+64
	ldrh	r3, [r3, ip]
	cmp	r3, r1
	strne	r4, [sp, #20]
	bne	.L1997
.L1954:
.L1953:
	mvn	r3, #0
	strh	r3, [r2, r0]	@ movhi
	mov	r3, #428
.L2001:
	ldrh	r0, [r2, r3]
	b	.L1939
.L1997:
	ldr	r4, .L2002
	movw	sl, #65535
	ldr	r7, .L2002+16
	ldrh	r3, [r4, r7]
	cmp	r3, sl
	bne	.L1956
	mov	r3, #0
	str	r3, [r4, #-1180]
.L1996:
	ldr	r8, .L2002+28
	ldrh	sl, [r4, r8]
	mov	r0, sl
	bl	List_get_gc_head_node
	movw	r1, #65535
	uxth	r5, r0
	strh	r5, [r4, r7]	@ movhi
	cmp	r5, r1
	ldreq	r3, .L2002
	moveq	r2, #0
	moveq	r0, #8
	streqh	r2, [r3, r8]	@ movhi
	beq	.L1939
.L1958:
	mov	r0, r5
	bl	IsBlkInGcList
	cmp	r0, #0
	add	r0, sl, #1
	strneh	r0, [r4, r8]	@ movhi
	bne	.L1996
	ldr	ip, .L2002+12
	uxth	r0, r0
	mov	sl, #3840
	strh	r0, [r4, r8]	@ movhi
	movw	r8, #3908
	ldr	r2, [r4, #-2064]
	ldrh	r8, [ip, r8]
	mov	r3, r5, asl #1
	ldrh	ip, [ip, sl]
	ldrh	r1, [r2, r3]
	mul	r8, ip, r8
	add	ip, r8, r8, lsr #31
	cmp	r1, ip, asr #1
	bgt	.L1961
	cmp	r0, #48
	bls	.L1962
	cmp	r1, #8
	bls	.L1962
	ldr	r1, .L2002+68
	ldrh	r1, [r4, r1]
	cmp	r1, #35
	bhi	.L1962
.L1961:
	ldr	r1, .L2002+28
	mov	r0, #0
	strh	r0, [r4, r1]	@ movhi
.L1962:
	ldr	sl, [sp, #20]
	movw	r1, #65535
	ldrh	r3, [r2, r3]
	cmp	r3, r8
	cmpge	sl, r1
	ldr	sl, .L2002
	movne	r8, #0
	moveq	r8, #1
	bne	.L1963
	ldr	r3, .L2002+16
	mvn	r2, #0
	strh	r2, [sl, r3]	@ movhi
	add	r3, r3, #572
	mov	r2, #0
	strh	r2, [sl, r3]	@ movhi
	mov	r3, #428
	ldrh	r0, [sl, r3]
	b	.L1939
.L1963:
	cmp	r3, #0
	bne	.L1964
	movw	r0, #65535
	bl	decrement_vpc_count
	ldr	r3, .L2002+28
	ldrh	r2, [r4, r3]
	add	r2, r2, #1
	strh	r2, [r4, r3]	@ movhi
	b	.L1996
.L1964:
	ldr	r3, .L2002+12
	strb	r8, [sl, #-1748]
	ldrb	r3, [r3, #928]	@ zero_extendqisi2
	cmp	r3, #0
	beq	.L1965
	mov	r0, r5
	bl	ftl_get_blk_mode
	strb	r0, [sl, #-1748]
.L1965:
	ldr	r4, .L2002
	ldr	r0, .L2002+52
	bl	make_superblock
	movw	r1, #430
	mov	r3, #0
	strh	r3, [r4, r1]	@ movhi
	ldr	r1, .L2002+16
	ldr	r0, [r4, #-2064]
	ldrh	r1, [r4, r1]
	mov	r1, r1, asl #1
	ldrh	r0, [r0, r1]
	mov	r1, #432
	strb	r3, [r4, #-1750]
	strh	r0, [r4, r1]	@ movhi
	ldr	r1, .L2002+56
	strh	r3, [r4, r1]	@ movhi
.L1956:
	ldr	sl, [sp, #16]
	cmp	sl, #1
	bne	.L1966
	bl	FtlReadRefresh
.L1966:
	ldr	r3, .L2002
	mov	r2, #1
	movw	r1, #3908
	str	r2, [r3, #-992]
	ldr	r2, .L2002+12
	ldrh	r9, [r2, r1]
	ldrb	r1, [r2, #928]	@ zero_extendqisi2
	cmp	r1, #0
	beq	.L1967
	ldrb	r1, [r3, #-1748]	@ zero_extendqisi2
	cmp	r1, #1
	movweq	r1, #3910
	ldreqh	r9, [r2, r1]
.L1967:
	ldr	r2, .L2002+56
	mov	r8, #0
	ldr	r4, .L2002
	mov	fp, r9
	ldrh	r3, [r3, r2]
	add	r2, r3, r6
	cmp	r2, r9
	rsbgt	r6, r3, r9
	uxthgt	r6, r6
	b	.L1969
.L1971:
	ldrh	r1, [r2, #2]!
	movw	sl, #65535
	add	r3, r3, #1
	cmp	r1, sl
	uxth	r3, r3
	orrne	r1, ip, r1, asl #10
	mlane	sl, r0, r7, lr
	addne	r7, r7, #1
	uxthne	r7, r7
	strne	r1, [sl, #4]
.L1977:
	cmp	r3, r5
	bne	.L1971
	ldr	r0, [r4, #-1160]
	mov	r1, r7
	ldrb	r2, [r4, #-1748]	@ zero_extendqisi2
	mov	r5, #0
	bl	FlashReadPages
	mov	r9, r5
	mov	sl, r6
	b	.L1972
.L1975:
	ldr	r3, [r4, #-1160]
	add	r2, r3, r5
	ldr	r3, [r3, r5]
	ldr	r6, [r2, #12]
	cmn	r3, #1
	beq	.L1973
	ldrh	r3, [r6, #0]
	movw	r1, #61589
	cmp	r3, r1
	bne	.L1973
	add	r1, sp, #28
	mov	r2, #0
	ldr	r0, [r6, #8]
	bl	log2phys
	ldr	r1, [r4, #-1160]
	ldr	r2, [sp, #28]
	add	r1, r1, r5
	bic	r2, r2, #-2147483648
	ldr	r3, [r1, #4]
	cmp	r2, r3
	bne	.L1973
	movw	r3, #430
	ldr	r0, [r4, #-1176]
	ldrh	r2, [r4, r3]
	ldr	r1, [r1, #16]
	add	r2, r2, #1
	strh	r2, [r4, r3]	@ movhi
	ldr	r2, [r4, #-968]
	mov	r3, #36
	mla	r2, r3, r0, r2
	str	r1, [r2, #16]
	str	r2, [sp, #8]
	str	r3, [sp, #12]
	bl	Ftl_get_new_temp_ppa
	ldr	r2, [sp, #8]
	ldr	r1, [r4, #-1176]
	str	r0, [r2, #4]
	ldr	r3, [sp, #12]
	ldr	r2, [r4, #-968]
	mla	r3, r3, r1, r2
	ldr	r2, [r4, #-1160]
	add	r2, r2, r5
	ldr	r1, [r2, #8]
	str	r1, [r3, #8]
	mov	r1, #1
	ldr	r2, [r2, #12]
	str	r2, [r3, #12]
	ldr	r3, [sp, #28]
	str	r3, [r6, #12]
	ldr	r3, .L2002+44
	ldrh	r3, [r4, r3]
	strh	r3, [r6, #2]	@ movhi
	ldr	r3, [r4, #-1844]
	ldr	r0, [r4, #-1160]
	str	r3, [r6, #4]
	add	r0, r0, r5
	ldr	r3, [r4, #-1176]
	add	r3, r3, #1
	str	r3, [r4, #-1176]
	bl	FtlGcBufAlloc
	ldr	r3, .L2002+12
	ldrb	r3, [r3, #928]	@ zero_extendqisi2
	cmp	r3, #0
	bne	.L1974
	ldrb	r3, [r4, #-1941]	@ zero_extendqisi2
	ldr	r2, [r4, #-1176]
	cmp	r2, r3
	beq	.L1974
	ldr	r3, .L2002+72
	ldrh	r3, [r4, r3]
	cmp	r3, #0
	bne	.L1973
.L1974:
	bl	Ftl_gc_temp_data_write_back
	cmp	r0, #0
	ldrne	r3, .L2002
	movne	r2, #0
	strne	r2, [r3, #-992]
	movne	r2, #428
	bne	.L2001
.L1973:
	add	r9, r9, #1
	add	r5, r5, #36
	uxth	r9, r9
.L1972:
	cmp	r9, r7
	bne	.L1975
	add	r8, r8, #1
	mov	r6, sl
.L1969:
	uxth	r3, r8
	ldr	r7, .L2002
	cmp	r3, r6
	ldr	r3, .L2002+56
	bcs	.L1976
	ldr	r1, .L2002+12
	mov	r2, #3840
	ldrh	ip, [r4, r3]
	mov	r7, #0
	ldr	lr, [r4, #-1160]
	mov	r3, r7
	ldrh	r5, [r1, r2]
	add	ip, ip, r8
	ldr	r2, .L2002+76
	mov	r0, #36
	b	.L1977
.L1976:
	ldrh	r2, [r7, r3]
	mov	r9, fp
	add	r6, r6, r2
	uxth	r6, r6
	strh	r6, [r7, r3]	@ movhi
	cmp	r6, fp
	bcc	.L1978
	ldr	r3, [r7, #-1176]
	cmp	r3, #0
	beq	.L1979
	bl	Ftl_gc_temp_data_write_back
	cmp	r0, #0
	movne	r3, #0
	strne	r3, [r7, #-992]
	movne	r3, #428
	ldrneh	r0, [r7, r3]
	bne	.L1939
.L1979:
	ldr	r3, .L2002
	movw	r2, #430
	ldrh	ip, [r3, r2]
	cmp	ip, #0
	bne	.L1980
	ldr	r2, .L2002+16
	ldr	r0, [r3, #-2064]
	ldrh	r1, [r3, r2]
	mov	r1, r1, asl #1
	ldrh	r4, [r0, r1]
	cmp	r4, #0
	beq	.L1980
	strh	ip, [r0, r1]	@ movhi
	ldrh	r0, [r3, r2]
	bl	update_vpc_list
	bl	FtlCacheWriteBack
	bl	l2p_flush
	bl	FtlVpcTblFlush
.L1980:
	ldr	r3, .L2002+16
	mvn	r1, #0
	ldr	r2, .L2002
	strh	r1, [r2, r3]	@ movhi
.L1978:
	ldr	r3, .L2002
	ldr	r2, .L2002+20
	ldrh	r2, [r3, r2]
	cmp	r2, #2
	ldrls	r2, .L2002+12
	movwls	r3, #3908
	ldrlsh	r6, [r2, r3]
	bls	.L1997
.L1981:
	mov	r1, #0
	str	r1, [r3, #-992]
	mov	r1, #428
	ldrh	r0, [r3, r1]
	cmp	r0, #0
	addeq	r0, r2, #1
.L1939:
	add	sp, sp, #36
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, sl, fp, pc}
.L2003:
	.align	2
.L2002:
	.word	.LANCHOR2
	.word	-1136
	.word	-1138
	.word	.LANCHOR0
	.word	-1756
	.word	-2048
	.word	-1190
	.word	-1184
	.word	.LC127
	.word	-1192
	.word	.LC128
	.word	-1948
	.word	-1772
	.word	.LANCHOR2-1756
	.word	-1754
	.word	-2044
	.word	-1996
	.word	-1152
	.word	-1944
	.word	.LANCHOR2-1742
	.fnend
	.size	rk_ftl_garbage_collect.part.22, .-rk_ftl_garbage_collect.part.22
	.align	2
	.global	rk_ftl_garbage_collect
	.type	rk_ftl_garbage_collect, %function
rk_ftl_garbage_collect:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	stmfd	sp!, {r3, r4, r5, lr}
	.save {r3, r4, r5, lr}
	mov	r4, r0
	ldr	r3, .L2011
	mov	r5, r1
	ldr	r0, [r3, #-2092]
	cmp	r0, #0
	movne	r0, #0
	ldmnefd	sp!, {r3, r4, r5, pc}
	ldr	r2, [r3, #-992]
	cmp	r2, #0
	ldmnefd	sp!, {r3, r4, r5, pc}
	ldr	r1, .L2011+4
	ldrh	r1, [r3, r1]
	cmp	r1, #47
	movls	r0, r2
	ldmlsfd	sp!, {r3, r4, r5, pc}
	ldr	r1, .L2011+8
	movw	r2, #2936
	ldrh	r1, [r1, r2]
	movw	r2, #65535
	cmp	r1, r2
	beq	.L2006
	ldr	r1, .L2011+12
	ldrh	r3, [r3, r1]
	cmp	r3, r2
	beq	.L2006
	mov	r0, #1
	bl	FtlGcFreeTempBlock
	cmp	r0, #0
	bne	.L2010
.L2006:
	mov	r0, r4
	mov	r1, r5
	ldmfd	sp!, {r3, r4, r5, lr}
	b	rk_ftl_garbage_collect.part.22
.L2010:
	mov	r0, #1
	ldmfd	sp!, {r3, r4, r5, pc}
.L2012:
	.align	2
.L2011:
	.word	.LANCHOR2
	.word	-2056
	.word	.LANCHOR1
	.word	-1948
	.fnend
	.size	rk_ftl_garbage_collect, .-rk_ftl_garbage_collect
	.align	2
	.global	FtlCacheWriteBack
	.type	FtlCacheWriteBack, %function
FtlCacheWriteBack:
	.fnstart
	@ args = 0, pretend = 0, frame = 8
	@ frame_needed = 0, uses_anonymous_args = 0
	ldr	r3, .L2035
	stmfd	sp!, {r0, r1, r4, r5, r6, r7, r8, r9, sl, lr}
	.save {r0, r1, r4, r5, r6, r7, r8, r9, sl, lr}
	ldr	r4, [r3, #436]
	ldr	r3, [r3, #-2092]
	cmp	r3, #0
	bne	.L2014
	ldr	r3, .L2035+4
	ldr	r1, [r3, #3960]
	cmp	r1, #0
	beq	.L2014
	ldrb	r8, [r3, #928]	@ zero_extendqisi2
	cmp	r8, #0
	beq	.L2015
	ldrb	r8, [r4, #8]	@ zero_extendqisi2
	sub	lr, r8, #1
	rsbs	r8, lr, #0
	adc	r8, r8, lr
.L2015:
	ldr	r0, [r3, #3964]
	mov	r2, r8
	ldrb	r3, [r4, #9]	@ zero_extendqisi2
	mov	r5, #0
	bl	FlashProgPages
	mov	r7, r5
	ldr	r6, .L2035+4
	ldr	r9, .L2035
	b	.L2016
.L2021:
	ldr	r2, [r6, #3964]
	add	r3, r2, r5
	ldr	r2, [r2, r5]
	cmn	r2, #1
	beq	.L2034
	ldr	r2, [r3, #4]
	cmp	r8, #0
	ldr	r0, [r3, #16]
	add	r1, sp, #4
	str	r2, [sp, #4]
	orrne	r2, r2, #-2147483648
	strne	r2, [sp, #4]
	mov	r2, #1
	bl	log2phys
	ldr	r3, [r6, #3964]
	add	r3, r3, r5
	ldr	r3, [r3, #12]
	ldr	r0, [r3, #12]
	cmn	r0, #1
	beq	.L2019
	ubfx	r0, r0, #10, #16
	bl	P2V_block_in_plane
	ldr	r2, [r9, #-2064]
	mov	r3, r0, asl #1
	mov	sl, r0
	ldrh	r2, [r2, r3]
	cmp	r2, #0
	bne	.L2020
	ldr	r0, .L2035+8
	mov	r1, sl
	bl	printk
.L2020:
	mov	r0, sl
	bl	decrement_vpc_count
.L2019:
	add	r7, r7, #1
	add	r5, r5, #36
.L2016:
	ldr	r3, [r6, #3960]
	cmp	r7, r3
	bcc	.L2021
	b	.L2022
.L2030:
	ldr	r3, [r6, #3964]
	mvn	r2, #0
	movw	sl, #3908
	str	r2, [r3, r5]
	b	.L2023
.L2026:
	ldr	r0, [r3, #4]
	ubfx	r0, r0, #10, #16
	bl	P2V_block_in_plane
	ldrh	r3, [r4, #0]
	cmp	r3, r0
	bne	.L2024
	ldr	r2, [r7, #-2064]
	mov	r3, r3, asl #1
	ldrh	r1, [r4, #4]
	ldrh	r0, [r2, r3]
	rsb	r1, r1, r0
	strh	r1, [r2, r3]	@ movhi
	ldrh	r3, [r6, sl]
	strh	r3, [r4, #2]	@ movhi
	mov	r3, #0
	strb	r3, [r4, #6]
	strh	r3, [r4, #4]	@ movhi
.L2024:
	ldrh	r3, [r4, #4]
	cmp	r3, #0
	bne	.L2025
	mov	r0, r4
	bl	allocate_new_data_superblock
.L2025:
	ldr	r3, [r7, #-1608]
	add	r3, r3, #1
	str	r3, [r7, #-1608]
	ldr	r3, [r6, #3964]
	add	r3, r3, r5
	ldr	r0, [r3, #4]
	ubfx	r0, r0, #10, #16
	bl	FtlGcMarkBadPhyBlk
	mov	r0, r4
	bl	get_new_active_ppa
	mov	r1, #1
	mov	r2, r8
	mov	r3, r0
	str	r0, [sp, #4]
	ldr	r0, [r6, #3964]
	add	r0, r0, r5
	str	r3, [r0, #4]
	ldrb	r3, [r4, #9]	@ zero_extendqisi2
	bl	FlashProgPages
	ldr	r3, [r7, #-2092]
	cmp	r3, #0
	bne	.L2014
.L2023:
	ldr	r2, [r6, #3964]
	add	r3, r2, r5
	ldr	r2, [r2, r5]
	cmn	r2, #1
	beq	.L2026
	ldr	r2, [r3, #4]
	cmp	r8, #0
	ldr	r0, [r3, #16]
	add	r1, sp, #4
	str	r2, [sp, #4]
	orrne	r2, r2, #-2147483648
	strne	r2, [sp, #4]
	mov	r2, #1
	bl	log2phys
	ldr	r3, [r6, #3964]
	add	r3, r3, r5
	ldr	r3, [r3, #12]
	ldr	r0, [r3, #12]
	cmn	r0, #1
	beq	.L2028
	ubfx	r0, r0, #10, #16
	bl	P2V_block_in_plane
	ldr	r2, [r7, #-2064]
	mov	r3, r0, asl #1
	mov	sl, r0
	ldrh	r2, [r2, r3]
	cmp	r2, #0
	bne	.L2029
	ldr	r0, .L2035+8
	mov	r1, sl
	bl	printk
.L2029:
	mov	r0, sl
	bl	decrement_vpc_count
.L2028:
	add	r9, r9, #1
	add	r5, r5, #36
	b	.L2017
.L2034:
	ldr	r6, .L2035+4
	mov	r5, #0
	ldr	r7, .L2035
	mov	r9, r5
.L2017:
	ldr	r3, [r6, #3960]
	cmp	r9, r3
	bcc	.L2030
	movw	r4, #16386
	ldr	r6, .L2035
	ldr	r5, .L2035+12
	b	.L2031
.L2032:
	mov	r0, #1
	mov	r1, r0
	bl	rk_ftl_garbage_collect
	subs	r4, r4, #1
	beq	.L2022
.L2031:
	ldrh	r3, [r6, r5]
	cmp	r3, #0
	bne	.L2032
.L2022:
	ldr	r3, .L2035+4
	mov	r2, #0
	str	r2, [r3, #3960]
.L2014:
	mov	r0, #0
	ldmfd	sp!, {r2, r3, r4, r5, r6, r7, r8, r9, sl, pc}
.L2036:
	.align	2
.L2035:
	.word	.LANCHOR2
	.word	.LANCHOR0
	.word	.LC129
	.word	-1134
	.fnend
	.size	FtlCacheWriteBack, .-FtlCacheWriteBack
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
	.global	FtlGcFreeTempBlock
	.type	FtlGcFreeTempBlock, %function
FtlGcFreeTempBlock:
	.fnstart
	@ args = 0, pretend = 0, frame = 8
	@ frame_needed = 0, uses_anonymous_args = 0
	stmfd	sp!, {r0, r1, r2, r4, r5, r6, r7, r8, sl, lr}
	.save {r0, r1, r2, r4, r5, r6, r7, r8, sl, lr}
	movw	r3, #3908
	ldr	r2, .L2061
	ldrh	r1, [r2, r3]
	ldr	r3, .L2061+4
	ldr	r2, [r3, #-2092]
	cmp	r2, #0
	bne	.L2059
	ldr	ip, .L2061+8
	ldrh	r4, [r3, ip]
	movw	ip, #65535
	cmp	r4, ip
	beq	.L2040
	cmp	r0, #0
	beq	.L2041
	ldr	lr, .L2061+12
	movw	r0, #2936
	ldrh	r5, [lr, r0]
	cmp	r5, ip
	movne	r1, #2
	bne	.L2041
	strh	r2, [lr, r0]	@ movhi
	sub	r2, r2, #2048
	ldrh	r3, [r3, r2]
	cmp	r3, #17
	movhi	r1, #2
.L2041:
	ldr	r0, .L2061+16
	bl	FtlGcScanTempBlk
	cmn	r0, #1
	str	r0, [sp, #4]
	beq	.L2042
	ldr	r3, .L2061+4
	mov	r4, r4, asl #1
	ldr	r3, [r3, #-2084]
	ldrh	r2, [r3, r4]
	cmp	r2, #4
	bls	.L2043
	sub	r2, r2, #5
	mov	r0, #1
	strh	r2, [r3, r4]	@ movhi
	bl	FtlEctTblFlush
.L2043:
	ldr	r3, .L2061+4
	ldr	r2, [r3, #-1008]
	cmp	r2, #0
	bne	.L2044
	ldr	r2, [r3, #-1608]
	ldr	r0, [sp, #4]
	add	r2, r2, #1
	str	r2, [r3, #-1608]
	ubfx	r0, r0, #10, #16
	bl	FtlBbmMapBadBlock
	bl	FtlBbmTblFlush
.L2044:
	ldr	r3, .L2061+4
	mov	r2, #0
	str	r2, [r3, #-1008]
	b	.L2057
.L2042:
	ldr	r2, .L2061+12
	movw	r3, #2936
	ldrh	r2, [r2, r3]
	movw	r3, #65535
	cmp	r2, r3
	bne	.L2057
.L2040:
	ldr	r6, .L2061+4
	movw	r3, #65535
	ldr	r5, .L2061+8
	mov	r4, #0
	str	r4, [r6, #-1008]
	ldrh	r2, [r6, r5]
	cmp	r2, r3
	moveq	r0, r4
	beq	.L2039
	bl	FtlCacheWriteBack
	ldr	ip, .L2061
	movw	r0, #3908
	ldrb	r1, [r6, #-1941]	@ zero_extendqisi2
	ldrh	r3, [r6, r5]
	ldrh	r0, [ip, r0]
	ldr	r2, [r6, #-2064]
	ldr	sl, .L2061+20
	mov	r3, r3, asl #1
	mul	r1, r0, r1
	strh	r1, [r2, r3]	@ movhi
	ldr	r3, [r6, #-1860]
	ldrh	r2, [r6, sl]
	add	r3, r2, r3
	str	r3, [r6, #-1860]
	b	.L2045
.L2048:
	mov	r7, #12
	ldr	r8, [r6, #-1144]
	mul	r7, r7, r4
	add	r1, sp, #4
	mov	r2, #0
	add	r5, r8, r7
	ldr	r0, [r5, #8]
	bl	log2phys
	ldr	r0, [sp, #4]
	ldr	r3, [r8, r7]
	cmp	r0, r3
	bne	.L2046
	ubfx	r0, r0, #10, #16
	bl	P2V_block_in_plane
	add	r1, r5, #4
	mov	r2, #1
	mov	r7, r0
	ldr	r0, [r5, #8]
	bl	log2phys
	mov	r0, r7
	b	.L2060
.L2046:
	ldr	r3, [r5, #4]
	cmp	r0, r3
	beq	.L2047
	ldr	r3, .L2061+8
	ldrh	r0, [r6, r3]
.L2060:
	bl	decrement_vpc_count
.L2047:
	add	r4, r4, #1
	uxth	r4, r4
.L2045:
	ldrh	r3, [r6, sl]
	ldr	r5, .L2061+4
	cmp	r3, r4
	bhi	.L2048
	movw	r0, #65535
	ldr	r4, .L2061+8
	bl	decrement_vpc_count
	ldr	r3, .L2061
	ldrb	r3, [r3, #928]	@ zero_extendqisi2
	cmp	r3, #0
	beq	.L2049
	ldr	r0, .L2061+24
	ldrh	r1, [r5, r4]
	bl	printk
.L2049:
	ldrh	r0, [r5, r4]
	ldr	r2, [r5, #-2064]
	mov	r3, r0, asl #1
	ldrh	r3, [r2, r3]
	cmp	r3, #0
	beq	.L2050
	bl	INSERT_DATA_LIST
	b	.L2051
.L2050:
	bl	INSERT_FREE_LIST
.L2051:
	ldr	r4, .L2061+4
	mvn	r2, #0
	ldr	r3, .L2061+8
	strh	r2, [r4, r3]	@ movhi
	mov	r3, #0
	ldr	r2, .L2061+20
	strh	r3, [r4, r2]	@ movhi
	ldr	r2, .L2061+28
	strh	r3, [r4, r2]	@ movhi
	bl	l2p_flush
	bl	FtlVpcTblFlush
	ldr	r3, [r4, #-1884]
	mov	r1, r4
	ldr	r2, .L2061+32
	cmp	r3, #0
	beq	.L2052
	ldr	r3, [r4, #-1824]
	cmp	r3, #29
	bhi	.L2052
	ldrh	r3, [r4, r2]
	sub	r2, r2, #276
	mvn	r1, #0
	ldrh	r2, [r4, r2]
	cmp	r2, r3
	movcc	r3, r3, asl #1
	ldrcc	r2, .L2061+36
	strcch	r3, [r4, r2]	@ movhi
	ldr	r3, .L2061+40
	ldr	r2, .L2061+4
	strh	r1, [r2, r3]	@ movhi
	b	.L2059
.L2052:
	ldrh	r2, [r1, r2]
	ldr	r0, .L2061+44
	ldr	r3, .L2061+4
	ldrh	r1, [r1, r0]
	add	r0, r2, r2, asl #1
	cmp	r1, r0, lsr #2
	ble	.L2059
	ldr	r1, .L2061+40
	mvn	r0, #0
	strh	r0, [r3, r1]	@ movhi
	ldr	r1, .L2061
	ldrb	r0, [r1, #928]	@ zero_extendqisi2
	ldr	r1, .L2061+36
	cmp	r0, #0
	subne	r2, r2, #2
	moveq	r2, #20
	strneh	r2, [r3, r1]	@ movhi
	streqh	r2, [r3, r1]	@ movhi
	beq	.L2039
	b	.L2059
.L2057:
	mov	r0, #1
	b	.L2039
.L2059:
	mov	r0, #0
.L2039:
	ldmfd	sp!, {r1, r2, r3, r4, r5, r6, r7, r8, sl, pc}
.L2062:
	.align	2
.L2061:
	.word	.LANCHOR0
	.word	.LANCHOR2
	.word	-1948
	.word	.LANCHOR1
	.word	.LANCHOR2-1948
	.word	-1140
	.word	.LC130
	.word	-1152
	.word	-1772
	.word	-1192
	.word	-1756
	.word	-2048
	.fnend
	.size	FtlGcFreeTempBlock, .-FtlGcFreeTempBlock
	.align	2
	.global	Ftl_gc_temp_data_write_back
	.type	Ftl_gc_temp_data_write_back, %function
Ftl_gc_temp_data_write_back:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	stmfd	sp!, {r3, r4, r5, r6, r7, lr}
	.save {r3, r4, r5, r6, r7, lr}
	ldr	r3, .L2072
	ldrb	r3, [r3, #928]	@ zero_extendqisi2
	cmp	r3, #0
	ldr	r3, .L2072+4
	beq	.L2064
	ldr	r2, [r3, #-1176]
	tst	r2, #1
	beq	.L2064
	ldr	r2, .L2072+8
	ldrh	r2, [r3, r2]
	cmp	r2, #0
	bne	.L2069
.L2064:
	mov	r2, #0
	ldr	r0, [r3, #-968]
	ldr	r1, [r3, #-1176]
	mov	r3, r2
	bl	FlashProgPages
	mov	r4, #0
	ldr	r5, .L2072+4
	mov	r6, #36
	b	.L2066
.L2068:
	mul	r3, r6, r4
	ldr	r1, [r5, #-968]
	add	r2, r1, r3
	ldr	r1, [r1, r3]
	cmn	r1, #1
	bne	.L2067
	ldr	r2, .L2072+12
	mov	lr, #0
	ldr	ip, [r7, #-2064]
	ldrh	r0, [r7, r2]
	mov	r0, r0, asl #1
	strh	lr, [ip, r0]	@ movhi
	strh	r1, [r7, r2]	@ movhi
	ldr	r2, [r7, #-1608]
	add	r2, r2, #1
	str	r2, [r7, #-1608]
	ldr	r2, [r7, #-968]
	add	r3, r2, r3
	ldr	r0, [r3, #4]
	ubfx	r0, r0, #10, #16
	bl	FtlBbmMapBadBlock
	bl	FtlBbmTblFlush
	bl	FtlGcPageVarInit
	b	.L2071
.L2067:
	ldr	r3, [r2, #12]
	add	r4, r4, #1
	ldr	r1, [r2, #4]
	uxth	r4, r4
	ldr	r0, [r3, #12]
	ldr	r2, [r3, #8]
	bl	FtlGcUpdatePage
.L2066:
	ldr	r1, [r5, #-1176]
	ldr	r7, .L2072+4
	cmp	r4, r1
	bcc	.L2068
	ldr	r0, [r7, #-968]
	bl	FtlGcBufFree
	ldr	r3, .L2072+8
	mov	r0, #0
	str	r0, [r7, #-1176]
	ldrh	r3, [r7, r3]
	cmp	r3, r0
	ldmnefd	sp!, {r3, r4, r5, r6, r7, pc}
	mov	r0, #1
	bl	FtlGcFreeTempBlock
.L2071:
	mov	r0, #1
	ldmfd	sp!, {r3, r4, r5, r6, r7, pc}
.L2069:
	mov	r0, #0
	ldmfd	sp!, {r3, r4, r5, r6, r7, pc}
.L2073:
	.align	2
.L2072:
	.word	.LANCHOR0
	.word	.LANCHOR2
	.word	-1944
	.word	-1948
	.fnend
	.size	Ftl_gc_temp_data_write_back, .-Ftl_gc_temp_data_write_back
	.align	2
	.global	FtlGcPageRecovery
	.type	FtlGcPageRecovery, %function
FtlGcPageRecovery:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	stmfd	sp!, {r4, r5, r6, lr}
	.save {r4, r5, r6, lr}
	movw	r5, #3908
	ldr	r6, .L2076
	ldr	r0, .L2076+4
	ldr	r4, .L2076+8
	ldrh	r1, [r6, r5]
	bl	FtlGcScanTempBlk
	ldr	r3, .L2076+12
	ldrh	r2, [r4, r3]
	ldrh	r3, [r6, r5]
	cmp	r2, r3
	ldmccfd	sp!, {r4, r5, r6, pc}
	ldr	r0, .L2076+16
	bl	FtlMapBlkWriteDumpData
	mov	r0, #0
	bl	FtlGcFreeTempBlock
	mov	r3, #0
	str	r3, [r4, #-1008]
	ldmfd	sp!, {r4, r5, r6, pc}
.L2077:
	.align	2
.L2076:
	.word	.LANCHOR0
	.word	.LANCHOR2-1948
	.word	.LANCHOR2
	.word	-1946
	.word	.LANCHOR2-1084
	.fnend
	.size	FtlGcPageRecovery, .-FtlGcPageRecovery
	.align	2
	.global	FtlPowerLostRecovery
	.type	FtlPowerLostRecovery, %function
FtlPowerLostRecovery:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	stmfd	sp!, {r3, r4, r5, lr}
	.save {r3, r4, r5, lr}
	mov	r4, #0
	ldr	r3, .L2079
	ldr	r5, .L2079+4
	str	r4, [r3, #296]
	mov	r0, r5
	bl	FtlRecoverySuperblock
	mov	r0, r5
	add	r5, r5, #48
	bl	FtlSlcSuperblockCheck
	mov	r0, r5
	bl	FtlRecoverySuperblock
	mov	r0, r5
	bl	FtlSlcSuperblockCheck
	bl	FtlGcPageRecovery
	movw	r0, #65535
	bl	decrement_vpc_count
	mov	r0, r4
	ldmfd	sp!, {r3, r4, r5, pc}
.L2080:
	.align	2
.L2079:
	.word	.LANCHOR2
	.word	.LANCHOR2-2044
	.fnend
	.size	FtlPowerLostRecovery, .-FtlPowerLostRecovery
	.align	2
	.global	FtlSysBlkInit
	.type	FtlSysBlkInit, %function
FtlSysBlkInit:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	stmfd	sp!, {r3, r4, r5, r6, r7, lr}
	.save {r3, r4, r5, r6, r7, lr}
	mov	r3, #292
	ldr	r6, .L2092
	mov	r2, #0
	ldr	r4, .L2092+4
	mvn	r5, #0
	ldr	r0, [r6, #3844]
	strh	r2, [r4, r3]	@ movhi
	movw	r3, #290
	strh	r5, [r4, r3]	@ movhi
	uxth	r0, r0
	bl	FtlFreeSysBlkQueueInit
	bl	FtlScanSysBlk
	ldr	r3, .L2092+8
	ldrh	r2, [r4, r3]
	movw	r3, #65535
	cmp	r2, r3
	beq	.L2082
	bl	FtlLoadSysInfo
	subs	r7, r0, #0
	bne	.L2082
	bl	FtlLoadMapInfo
	bl	FtlLoadVonderInfo
	bl	Ftl_load_ext_data
	bl	FtlLoadEctTbl
	bl	FtlFreeSysBLkSort
	bl	SupperBlkListInit
	bl	FtlPowerLostRecovery
	mov	r0, #1
	bl	FtlUpdateVaildLpn
	movw	r3, #3946
	ldrh	r2, [r6, r3]
	mov	r3, r7
	ldr	r1, [r4, #-1900]
	b	.L2083
.L2085:
	add	r0, r1, r7
	add	r7, r7, #12
	ldr	r0, [r0, #4]
	cmp	r0, #0
	blt	.L2084
	add	r3, r3, #1
.L2083:
	cmp	r3, r2
	blt	.L2085
.L2084:
	ldr	r4, .L2092+4
	cmp	r3, r2
	ldr	r1, .L2092+12
	ldrh	r0, [r4, r1]
	add	r0, r0, #1
	strh	r0, [r4, r1]	@ movhi
	blt	.L2086
	mov	r3, #292
	ldrh	r3, [r4, r3]
	cmp	r3, #0
	beq	.L2087
.L2086:
	ldr	r0, .L2092+16
	bl	FtlSuperblockPowerLostFix
	ldr	r0, .L2092+20
	bl	FtlSuperblockPowerLostFix
	ldr	r3, .L2092+24
	ldr	r1, [r4, #-2064]
	ldr	r2, .L2092+28
	ldrh	r3, [r4, r3]
	ldrh	r0, [r4, r2]
	mov	r3, r3, asl #1
	ldrh	ip, [r1, r3]
	rsb	r0, r0, ip
	strh	r0, [r1, r3]	@ movhi
	ldr	r0, .L2092
	movw	r1, #3908
	ldr	r3, .L2092+32
	ldr	lr, [r4, #-2064]
	ldrh	ip, [r0, r1]
	strh	ip, [r4, r3]	@ movhi
	mov	r3, #0
	strh	r3, [r4, r2]	@ movhi
	add	r2, r2, #44
	strb	r3, [r4, #-2038]
	ldrh	ip, [r4, r2]
	add	r2, r2, #4
	ldrh	r5, [r4, r2]
	mov	ip, ip, asl #1
	ldrh	r6, [lr, ip]
	rsb	r5, r5, r6
	strh	r5, [lr, ip]	@ movhi
	ldrh	r0, [r0, r1]
	ldr	r1, .L2092+36
	strh	r3, [r4, r2]	@ movhi
	strb	r3, [r4, #-1990]
	strh	r0, [r4, r1]	@ movhi
	ldr	r0, .L2092+40
	bl	FtlMapBlkWriteDumpData
	ldr	r0, .L2092+44
	bl	FtlMapBlkWriteDumpData
	ldr	r3, .L2092+48
	ldrh	r2, [r4, r3]
	add	r2, r2, #1
	strh	r2, [r4, r3]	@ movhi
	bl	l2p_flush
	bl	FtlVpcTblFlush
	bl	FtlVpcTblFlush
.L2087:
	ldr	r4, .L2092+4
	movw	r3, #65535
	ldr	r5, .L2092+24
	ldrh	r2, [r4, r5]
	cmp	r2, r3
	beq	.L2088
	ldr	r3, .L2092+28
	ldrh	r3, [r4, r3]
	cmp	r3, #0
	bne	.L2088
	ldr	r3, .L2092+52
	ldrh	r3, [r4, r3]
	cmp	r3, #0
	bne	.L2088
	bl	FtlVpcTblFlush
	ldrh	r0, [r4, r5]
	bl	FtlGcRefreshOpenBlock
	ldr	r3, .L2092+56
	ldrh	r0, [r4, r3]
	bl	FtlGcRefreshOpenBlock
	ldr	r0, .L2092+16
	bl	allocate_new_data_superblock
	ldr	r0, .L2092+20
	bl	allocate_new_data_superblock
	add	r0, r4, #244
	bl	FtlMapBlkWriteDumpData
.L2088:
	ldr	r3, .L2092+12
	ldr	r2, .L2092+4
	ldrh	r5, [r2, r3]
	ands	r5, r5, #31
	movne	r5, #0
	bne	.L2082
	bl	FtlVpcCheckAndModify
.L2082:
	mov	r0, r5
	ldmfd	sp!, {r3, r4, r5, r6, r7, pc}
.L2093:
	.align	2
.L2092:
	.word	.LANCHOR0
	.word	.LANCHOR2
	.word	-1768
	.word	-1792
	.word	.LANCHOR2-2044
	.word	.LANCHOR2-1996
	.word	-2044
	.word	-2040
	.word	-2042
	.word	-1994
	.word	.LANCHOR2-1084
	.word	.LANCHOR2+244
	.word	-1790
	.word	-1992
	.word	-1996
	.fnend
	.size	FtlSysBlkInit, .-FtlSysBlkInit
	.align	2
	.global	Ftl_get_new_temp_ppa
	.type	Ftl_get_new_temp_ppa, %function
Ftl_get_new_temp_ppa:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	stmfd	sp!, {r3, r4, r5, lr}
	.save {r3, r4, r5, lr}
	ldr	r3, .L2097
	ldr	r5, .L2097+4
	ldrh	r2, [r5, r3]
	movw	r3, #65535
	cmp	r2, r3
	beq	.L2095
	ldr	r3, .L2097+8
	ldrh	r3, [r5, r3]
	cmp	r3, #0
	bne	.L2096
.L2095:
	bl	FtlCacheWriteBack
	mov	r0, #0
	bl	FtlGcFreeTempBlock
	ldr	r0, .L2097+12
	mov	r4, #0
	strb	r4, [r5, #-1940]
	bl	allocate_data_superblock
	ldr	r3, .L2097+16
	strh	r4, [r5, r3]	@ movhi
	add	r3, r3, #12
	strh	r4, [r5, r3]	@ movhi
	bl	l2p_flush
	mov	r0, r4
	bl	FtlEctTblFlush
	bl	FtlVpcTblFlush
.L2096:
	ldr	r0, .L2097+12
	ldmfd	sp!, {r3, r4, r5, lr}
	b	get_new_active_ppa
.L2098:
	.align	2
.L2097:
	.word	-1948
	.word	.LANCHOR2
	.word	-1944
	.word	.LANCHOR2-1948
	.word	-1152
	.fnend
	.size	Ftl_get_new_temp_ppa, .-Ftl_get_new_temp_ppa
	.align	2
	.global	FtlDiscard
	.type	FtlDiscard, %function
FtlDiscard:
	.fnstart
	@ args = 0, pretend = 0, frame = 8
	@ frame_needed = 0, uses_anonymous_args = 0
	stmfd	sp!, {r0, r1, r2, r4, r5, r6, r7, lr}
	.save {r0, r1, r2, r4, r5, r6, r7, lr}
	add	r2, r1, r0
	ldr	r5, .L2109
	mov	r7, r0
	mov	r4, r1
	ldr	r3, [r5, #3948]
	cmp	r2, r3
	mvnhi	r0, #0
	bhi	.L2100
	cmp	r1, #31
	bls	.L2107
	ldr	r3, .L2109+4
	ldr	r3, [r3, #-2092]
	cmp	r3, #0
	bne	.L2107
	bl	FtlCacheWriteBack
	movw	r3, #3914
	ldrh	r6, [r5, r3]
	mov	r0, r7
	mov	r1, r6
	bl	__aeabi_uidiv
	mls	r7, r0, r6, r7
	mov	r5, r0
	uxth	r7, r7
	cmp	r7, #0
	beq	.L2101
	rsb	r6, r7, r6
	add	r5, r0, #1
	cmp	r6, r4
	movcs	r6, r4
	uxth	r6, r6
	rsb	r4, r6, r4
.L2101:
	mvn	r3, #0
	ldr	r7, .L2109
	str	r3, [sp, #4]
	movw	r6, #3914
	b	.L2102
.L2104:
	mov	r0, r5
	mov	r1, sp
	mov	r2, #0
	bl	log2phys
	ldr	r3, [sp, #0]
	cmn	r3, #1
	beq	.L2103
	ldr	r3, .L2109+4
	add	r1, sp, #4
	mov	r0, r5
	ldr	r2, [r3, #440]
	add	r2, r2, #1
	str	r2, [r3, #440]
	ldr	r2, [r3, #-1876]
	add	r2, r2, #1
	str	r2, [r3, #-1876]
	mov	r2, #1
	bl	log2phys
	ldr	r0, [sp, #0]
	ubfx	r0, r0, #10, #16
	bl	P2V_block_in_plane
	bl	decrement_vpc_count
.L2103:
	ldrh	r3, [r7, r6]
	add	r5, r5, #1
	rsb	r4, r3, r4
.L2102:
	ldrh	r3, [r7, r6]
	cmp	r4, r3
	bcs	.L2104
	ldr	r3, .L2109+4
	mov	r4, #0
	ldr	r2, [r3, #440]
	cmp	r2, #32
	bls	.L2108
	str	r4, [r3, #440]
	bl	l2p_flush
	bl	FtlVpcTblFlush
	b	.L2108
.L2107:
	mov	r0, #0
	b	.L2100
.L2108:
	mov	r0, r4
.L2100:
	ldmfd	sp!, {r1, r2, r3, r4, r5, r6, r7, pc}
.L2110:
	.align	2
.L2109:
	.word	.LANCHOR0
	.word	.LANCHOR2
	.fnend
	.size	FtlDiscard, .-FtlDiscard
	.align	2
	.global	FtlSysFlush
	.type	FtlSysFlush, %function
FtlSysFlush:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	stmfd	sp!, {r3, lr}
	.save {r3, lr}
	ldr	r3, .L2113
	ldr	r3, [r3, #-2092]
	cmp	r3, #0
	bne	.L2112
	bl	FtlCacheWriteBack
	bl	l2p_flush
	mov	r0, #1
	bl	FtlEctTblFlush
	bl	FtlVpcTblFlush
.L2112:
	mov	r0, #0
	ldmfd	sp!, {r3, pc}
.L2114:
	.align	2
.L2113:
	.word	.LANCHOR2
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
	ldr	r3, .L2117
	ldr	r3, [r3, #2932]
	cmp	r3, #1
	bne	.L2116
	bl	FtlSysFlush
.L2116:
	mov	r0, #0
	ldmfd	sp!, {r3, pc}
.L2118:
	.align	2
.L2117:
	.word	.LANCHOR1
	.fnend
	.size	FtlDeInit, .-FtlDeInit
	.align	2
	.global	rk_ftl_de_init
	.type	rk_ftl_de_init, %function
rk_ftl_de_init:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	stmfd	sp!, {r3, lr}
	.save {r3, lr}
	bl	rk_nand_de_init
	bl	FtlDeInit
	ldmfd	sp!, {r3, lr}
	b	rk_nand_de_init
	.fnend
	.size	rk_ftl_de_init, .-rk_ftl_de_init
	.align	2
	.global	ftl_fix_nand_power_lost_error
	.type	ftl_fix_nand_power_lost_error, %function
ftl_fix_nand_power_lost_error:
	.fnstart
	@ args = 0, pretend = 0, frame = 48
	@ frame_needed = 0, uses_anonymous_args = 0
	ldr	r3, .L2129
	stmfd	sp!, {r4, r5, r6, r7, r8, sl, lr}
	.save {r4, r5, r6, r7, r8, sl, lr}
	.pad #52
	sub	sp, sp, #52
	ldrb	r3, [r3, #928]	@ zero_extendqisi2
	cmp	r3, #0
	beq	.L2120
	ldr	r4, .L2129+4
	movw	r3, #290
	ldr	r0, .L2129+8
	movw	r7, #4097
	ldrh	r6, [r4, r3]
	ldr	r3, [r4, #-2064]
	mov	r5, r6, asl #1
	mov	r1, r6
	ldrh	r2, [r3, r5]
	bl	printk
	ldr	r3, .L2129+12
	ldrh	r0, [r4, r3]
	bl	FtlGcRefreshOpenBlock
	ldr	r3, .L2129+16
	ldrh	r0, [r4, r3]
	bl	FtlGcRefreshOpenBlock
	ldr	r0, .L2129+20
	bl	allocate_new_data_superblock
	ldr	r0, .L2129+24
	bl	allocate_new_data_superblock
	b	.L2122
.L2124:
	mov	r0, #1
	mov	r1, r0
	bl	rk_ftl_garbage_collect
	ldr	r3, [r4, #-2064]
	ldrh	r3, [r3, r5]
	cmp	r3, #0
	beq	.L2123
.L2122:
	subs	r7, r7, #1
	bne	.L2124
.L2123:
	ldr	r7, .L2129+4
	mov	r1, r6
	ldr	r0, .L2129+8
	ldr	r3, [r7, #-2064]
	ldrh	r2, [r3, r5]
	bl	printk
	ldr	r3, [r7, #-2064]
	ldrh	r4, [r3, r5]
	cmp	r4, #0
	bne	.L2125
	add	r8, sp, #48
	mov	r0, sp
	strh	r6, [r8, #-48]!	@ movhi
	add	r8, r8, #14
	bl	make_superblock
	ldr	r2, .L2129
	mov	r3, #3840
	ldr	r7, [r7, #-2088]
	movw	lr, #65535
	mov	ip, #36
	ldrh	sl, [r2, r3]
	mov	r1, r4
	mov	r3, r4
	b	.L2126
.L2128:
	ldrh	r0, [r8, #2]!
	cmp	r0, lr
	beq	.L2127
	mla	r2, ip, r4, r7
	add	r4, r4, #1
	mov	r0, r0, asl #10
	uxth	r4, r4
	stmib	r2, {r0, r1}
	str	r1, [r2, #12]
.L2127:
	add	r3, r3, #1
	uxth	r3, r3
.L2126:
	cmp	r3, sl
	bne	.L2128
	ldr	r7, .L2129+4
	mov	r1, r6
	ldr	r0, .L2129+28
	ldr	r3, [r7, #-2064]
	ldrh	r2, [r3, r5]
	bl	printk
	mov	r1, #0
	mov	r2, r4
	ldr	r0, [r7, #-2088]
	bl	FlashEraseBlocks
	ldr	r0, [r7, #-2088]
	mov	r1, #1
	mov	r2, r4
	bl	FlashEraseBlocks
.L2125:
	ldr	r2, .L2129+4
	movw	r3, #290
	mvn	r1, #0
	strh	r1, [r2, r3]	@ movhi
.L2120:
	add	sp, sp, #52
	ldmfd	sp!, {r4, r5, r6, r7, r8, sl, pc}
.L2130:
	.align	2
.L2129:
	.word	.LANCHOR0
	.word	.LANCHOR2
	.word	.LC131
	.word	-2044
	.word	-1996
	.word	.LANCHOR2-2044
	.word	.LANCHOR2-1996
	.word	.LC132
	.fnend
	.size	ftl_fix_nand_power_lost_error, .-ftl_fix_nand_power_lost_error
	.align	2
	.global	FtlInit
	.type	FtlInit, %function
FtlInit:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
.L2132:
	stmfd	sp!, {r4, r5, r6, lr}
	.save {r4, r5, r6, lr}
	mvn	r3, #0
	ldr	r4, .L2142
	mov	r5, r0
	ldr	r6, .L2142+4
	ldr	r1, .L2142+8
	ldr	r0, .L2142+12
	str	r3, [r6, #2932]
	mov	r3, #0
	str	r3, [r4, #444]
	str	r3, [r4, #-2092]
	bl	printk
	mov	r0, r5
	ldr	r5, .L2142+16
	bl	FtlConstantsInit
	bl	FtlMemInit
	bl	FtlVariablesInit
	ldr	r0, [r5, #3844]
	uxth	r0, r0
	bl	FtlFreeSysBlkQueueInit
	bl	FtlLoadBbt
	cmp	r0, #0
	ldrne	r0, .L2142+20
	bne	.L2141
	bl	FtlSysBlkInit
	cmp	r0, #0
	beq	.L2135
	ldr	r0, .L2142+24
.L2141:
	ldr	r1, .L2142+28
	bl	printk
	b	.L2134
.L2135:
	mov	r1, #1
	str	r1, [r6, #2932]
	bl	rk_ftl_garbage_collect
	ldr	r3, .L2142+32
	ldrh	r3, [r4, r3]
	cmp	r3, #15
	bhi	.L2136
	mov	r4, #1024
.L2137:
	mov	r0, #1
	mov	r1, r0
	bl	rk_ftl_garbage_collect
	subs	r4, r4, #1
	bne	.L2137
	b	.L2134
.L2136:
	ldrb	r3, [r5, #928]	@ zero_extendqisi2
	cmp	r3, #0
	beq	.L2134
	mov	r4, #128
.L2138:
	mov	r0, #1
	mov	r1, r0
	bl	rk_ftl_garbage_collect
	subs	r4, r4, #1
	bne	.L2138
.L2134:
	mov	r0, #0
	ldmfd	sp!, {r4, r5, r6, pc}
.L2143:
	.align	2
.L2142:
	.word	.LANCHOR2
	.word	.LANCHOR1
	.word	.LC76
	.word	.LC75
	.word	.LANCHOR0
	.word	.LC133
	.word	.LC134
	.word	.LANCHOR3+36
	.word	-2048
	.fnend
	.size	FtlInit, .-FtlInit
	.align	2
	.type	FtlWrite.part.23, %function
FtlWrite.part.23:
	.fnstart
	@ args = 0, pretend = 0, frame = 96
	@ frame_needed = 0, uses_anonymous_args = 0
	stmfd	sp!, {r4, r5, r6, r7, r8, r9, sl, fp, lr}
	.save {r4, r5, r6, r7, r8, r9, sl, fp, lr}
	mov	r3, #2048
	ldr	r4, .L2191
	.pad #100
	sub	sp, sp, #100
	ldr	sl, .L2191+4
	mov	r5, r1
	mov	r7, r0
	mov	r9, r2
	str	r3, [r4, #448]
	movw	r3, #3914
	ldrh	r8, [sl, r3]
	mov	r1, r8
	bl	__aeabi_uidiv
	mov	r1, r8
	str	r0, [sp, #16]
	sub	r0, r7, #1
	add	r0, r0, r5
	bl	__aeabi_uidiv
	ldr	r3, [sp, #16]
	cmp	r5, #8
	rsb	fp, r3, r0
	ldr	r3, [r4, #-1872]
	add	r6, fp, #1
	str	r6, [sp, #20]
	add	r3, r6, r3
	str	r3, [r4, #-1872]
	ldr	r3, [r4, #-1856]
	str	r0, [sp, #36]
	add	r3, r5, r3
	str	r3, [r4, #-1856]
	ldr	r3, .L2191+8
	add	r6, r3, #48
	movhi	r6, r3
	ldr	r3, [sl, #3960]
	cmp	r3, #0
	beq	.L2179
	ldr	r2, [sl, #3964]
	sub	r3, r3, #1
	mov	sl, #36
	ldr	r6, [sp, #16]
	mla	sl, sl, r3, r2
	ldr	r3, [sl, #16]
	cmp	r6, r3
	strne	r5, [sp, #32]
	bne	.L2147
	ldr	r3, [r4, #-1868]
	mov	r0, r7
	mov	r1, r8
	add	r3, r3, #1
	str	r3, [r4, #-1868]
	ldr	r3, [r4, #452]
	add	r3, r3, #1
	str	r3, [r4, #452]
	bl	__aeabi_uidivmod
	ldr	r0, [sl, #8]
	rsb	r6, r1, r8
	add	r0, r0, r1, asl #9
	cmp	r6, r5
	movcs	r6, r5
	mov	r1, r9
	mov	r3, r6, asl #9
	str	r3, [sp, #4]
	mov	r2, r3
	bl	memcpy
	cmp	fp, #0
	ldr	r3, [sp, #4]
	bne	.L2148
	ldr	r2, [r4, #452]
	cmp	r2, #2
	ble	.L2149
.L2148:
	add	r9, r9, r3
	add	r7, r7, r6
	ldr	r3, [sp, #16]
	rsb	lr, r6, r5
	str	fp, [sp, #20]
	add	r3, r3, #1
	str	lr, [sp, #32]
	str	r3, [sp, #16]
.L2147:
	ldr	r3, .L2191
	mov	r2, #0
	ldr	r6, [r3, #436]
	str	r2, [r3, #452]
	b	.L2146
.L2179:
	str	r5, [sp, #32]
.L2146:
	ldr	r0, [sp, #16]
	ldr	r1, [sp, #36]
	bl	FtlCacheMetchLpa
	cmp	r0, #0
	beq	.L2150
	bl	FtlCacheWriteBack
.L2150:
	cmp	r5, r8, asl #1
	ldr	r4, .L2191+4
	mov	fp, #0
	ldr	r5, [sp, #16]
	movcc	r3, #0
	movcs	r3, #1
	str	r3, [sp, #24]
	ldr	r3, .L2191
	mov	sl, r4
	str	fp, [sp, #40]
	str	r6, [r3, #436]
	b	.L2188
.L2176:
	ldrh	r1, [r6, #4]
	cmp	r1, #0
	bne	.L2152
	ldr	r2, .L2191+8
	ldr	r3, .L2191+12
	cmp	r6, r2
	bne	.L2153
	ldr	r2, .L2191+16
	ldrh	r8, [r8, r2]
	cmp	r8, #0
	bne	.L2154
	ldr	r0, .L2191+20
	str	r3, [sp, #4]
	bl	allocate_new_data_superblock
	ldr	r3, [sp, #4]
	str	r8, [r3, #2940]
.L2154:
	ldr	r0, .L2191+8
	str	r3, [sp, #4]
	bl	allocate_new_data_superblock
	ldr	r3, [sp, #4]
	ldr	r2, [r3, #2940]
	ldr	r3, .L2191+20
	cmp	r2, #0
	movne	r6, r3
	b	.L2155
.L2153:
	str	r1, [r3, #2940]
	ldr	r3, .L2191+24
	ldrh	r3, [r8, r3]
	cmp	r3, #0
	movne	r6, r2
	bne	.L2155
	mov	r0, r6
	bl	allocate_new_data_superblock
.L2155:
	ldrh	r3, [r6, #4]
	cmp	r3, #0
	bne	.L2156
	mov	r0, r6
	bl	allocate_new_data_superblock
.L2156:
	ldr	r3, .L2191
	str	r6, [r3, #436]
.L2152:
	ldr	r2, .L2191
	mov	r8, r6
	ldrh	r3, [r6, #4]
	ldr	r1, [r2, #-976]
	ldr	r2, [r4, #3960]
	rsb	r2, r2, r1
	cmp	r3, r2
	movcc	r2, r3
	ldr	r3, [sp, #20]
	cmp	r2, r3
	movcc	r3, r2
	str	r3, [sp, #52]
	mov	r3, #0
	str	r3, [sp, #28]
	b	.L2157
.L2173:
	ldrh	r3, [r8, #4]
	cmp	r3, #0
	beq	.L2158
	ldr	r6, [sp, #36]
	rsb	r2, r6, r5
	ldr	r6, [sp, #24]
	rsbs	r3, r2, #0
	adc	r3, r3, r2
	tst	r3, r6
	beq	.L2159
	ldr	r6, [sp, #28]
	cmp	r6, #0
	beq	.L2159
	movw	lr, #3914
	ldr	r6, [sp, #32]
	ldrh	r2, [r4, lr]
	add	r1, r6, r7
	mls	r1, r2, r5, r1
	cmp	r1, r2
	bne	.L2158
.L2159:
	add	r1, sp, #56
	mov	r2, #0
	mov	r0, r5
	str	r3, [sp, #4]
	bl	log2phys
	mov	r0, r8
	bl	get_new_active_ppa
	ldr	lr, [r4, #3960]
	ldr	r1, [r4, #3964]
	mov	r2, #3920
	mov	ip, #36
	ldrh	r2, [r4, r2]
	ldr	r6, .L2191
	mla	r1, ip, lr, r1
	mul	lr, lr, r2
	ldr	r6, [r6, #-932]
	bic	lr, lr, #3
	str	r6, [sp, #48]
	str	lr, [sp, #44]
	add	r6, r6, lr
	str	r5, [r1, #16]
	str	r6, [r1, #12]
	str	r0, [r1, #4]
	movw	r0, #3918
	ldrh	lr, [r4, r0]
	ldr	r0, [r4, #3960]
	mul	lr, r0, lr
	ldr	r0, .L2191
	bic	lr, lr, #3
	str	lr, [sp, #12]
	ldr	lr, [r0, #-952]
	ldr	r0, [sp, #12]
	add	lr, lr, r0
	mov	r0, r6
	str	lr, [r1, #8]
	mov	r1, #0
	str	ip, [sp, #8]
	bl	ftl_memset
	ldr	r1, [sp, #16]
	ldr	ip, [sp, #8]
	rsb	r3, r1, r5
	rsbs	r1, r3, #0
	adc	r1, r1, r3
	ldr	r3, [sp, #4]
	str	r1, [sp, #12]
	orrs	r1, r1, r3
	beq	.L2160
	ldr	r2, [sp, #12]
	cmp	r2, #0
	beq	.L2161
	movw	r3, #3914
	mov	r0, r7
	ldrh	fp, [r4, r3]
	mov	r1, fp
	bl	__aeabi_uidivmod
	ldr	r3, [sp, #32]
	rsb	fp, r1, fp
	str	r1, [sp, #40]
	cmp	fp, r3
	movcs	fp, r3
	b	.L2162
.L2161:
	cmp	r3, #0
	beq	.L2162
	ldr	r3, [sp, #32]
	movw	lr, #3914
	add	fp, r3, r7
	ldrh	r3, [r4, lr]
	mls	fp, r3, r5, fp
	ldr	r3, [sp, #12]
	str	r3, [sp, #40]
	uxth	fp, fp
.L2162:
	movw	lr, #3914
	ldrh	r3, [r4, lr]
	cmp	fp, r3
	bne	.L2163
	ldr	r3, [sp, #12]
	ldr	r0, [sl, #3960]
	cmp	r3, #0
	ldr	r2, [sl, #3964]
	ldr	r3, [sp, #24]
	muleq	r1, r5, fp
	movne	r1, r9
	rsbeq	r1, r7, r1
	addeq	r1, r9, r1, asl #9
	cmp	r3, #0
	mov	r3, #36
	mla	r3, r3, r0, r2
	strne	r1, [r3, #8]
	bne	.L2166
	ldr	r0, [r3, #8]
	movw	r3, #3918
	ldrh	r2, [sl, r3]
	b	.L2189
.L2163:
	ldr	r2, [sp, #56]
	mov	r3, #36
	cmn	r2, #1
	beq	.L2167
	ldr	r1, [r4, #3960]
	add	r0, sp, #60
	str	r2, [sp, #64]
	ldr	r2, [r4, #3964]
	str	r5, [sp, #76]
	mla	r3, r3, r1, r2
	mov	r1, #1
	ldr	r2, [r3, #8]
	ldr	r3, [r3, #12]
	str	r2, [sp, #68]
	mov	r2, #0
	str	r3, [sp, #72]
	bl	FlashReadPages
	ldr	r3, [sp, #60]
	cmn	r3, #1
	ldr	r3, .L2191
	ldreq	r2, [r3, #-1632]
	addeq	r2, r2, #1
	streq	r2, [r3, #-1632]
	beq	.L2169
	ldr	r2, [r6, #8]
	cmp	r2, r5
	beq	.L2169
	ldr	r2, [r3, #-1632]
	ldr	r0, .L2191+28
	add	r2, r2, #1
	str	r2, [r3, #-1632]
	mov	r2, r5
	ldr	r1, [r6, #8]
	bl	printk
	b	.L2169
.L2167:
	ldr	r1, [r4, #3960]
	ldr	r2, [r4, #3964]
	mla	r3, r3, r1, r2
	mov	r1, #0
	ldr	r0, [r3, #8]
	movw	r3, #3918
	ldrh	r2, [r4, r3]
	bl	ftl_memset
.L2169:
	ldr	r3, [sp, #12]
	ldr	r1, [r4, #3960]
	cmp	r3, #0
	ldr	r2, [r4, #3964]
	mov	r3, #36
	movweq	lr, #3914
	mla	r3, r3, r1, r2
	ldreqh	r1, [r4, lr]
	movne	r1, r9
	mov	r2, fp, asl #9
	muleq	r1, r1, r5
	ldrne	r0, [r3, #8]
	ldrne	r3, [sp, #40]
	rsbeq	r1, r7, r1
	ldreq	r0, [r3, #8]
	addne	r0, r0, r3, asl #9
	addeq	r1, r9, r1, asl #9
	b	.L2189
.L2160:
	ldr	r3, [sp, #24]
	ldr	r2, [r4, #3960]
	cmp	r3, #0
	ldr	r3, [r4, #3964]
	mla	ip, ip, r2, r3
	beq	.L2171
	movw	lr, #3914
	ldrh	r3, [r4, lr]
	mul	r3, r3, r5
	rsb	r3, r7, r3
	add	r3, r9, r3, asl #9
	str	r3, [ip, #8]
	b	.L2166
.L2171:
	movw	r0, #3914
	movw	r3, #3918
	ldrh	r1, [r4, r0]
	ldrh	r2, [r4, r3]
	ldr	r0, [ip, #8]
	mul	r1, r1, r5
	rsb	r1, r7, r1
	add	r1, r9, r1, asl #9
.L2189:
	bl	memcpy
.L2166:
	ldr	r3, .L2191+32
	ldr	r0, [sp, #48]
	ldr	lr, [sp, #44]
	strh	r3, [r0, lr]	@ movhi
	ldr	r3, .L2191
	str	r5, [r6, #8]
	add	r5, r5, #1
	ldr	r2, [r3, #-1844]
	str	r2, [r6, #4]
	add	r2, r2, #1
	cmn	r2, #1
	str	r2, [r3, #-1844]
	moveq	r2, #0
	streq	r2, [r3, #-1844]
	ldr	r3, [sp, #56]
	str	r3, [r6, #12]
	ldrh	r3, [r8, #0]
	strh	r3, [r6, #2]	@ movhi
	ldr	r3, [r4, #3960]
	add	r3, r3, #1
	str	r3, [r4, #3960]
	ldr	r3, [sp, #28]
	add	r3, r3, #1
	str	r3, [sp, #28]
.L2157:
	ldr	r6, [sp, #28]
	ldr	r3, [sp, #52]
	cmp	r6, r3
	bne	.L2173
.L2158:
	ldr	r3, [sp, #20]
	mov	r6, r8
	ldr	lr, [sp, #28]
	ldr	r2, [r4, #3960]
	rsb	r3, lr, r3
	str	r3, [sp, #20]
	ldr	r3, .L2191
	ldr	r0, [sp, #24]
	ldr	r3, [r3, #-976]
	cmp	r2, r3
	orrcs	r0, r0, #1
	uxtb	r3, r0
	cmp	r3, #0
	bne	.L2174
	ldrh	r2, [r8, #4]
	cmp	r2, #0
	bne	.L2185
.L2174:
	bl	FtlCacheWriteBack
	ldr	lr, [sp, #20]
	mov	r3, #0
	str	r3, [sl, #3960]
	cmp	lr, #3
	ldr	r3, [sp, #24]
	movls	r3, #0
.L2185:
	str	r3, [sp, #24]
.L2188:
	ldr	r3, [sp, #20]
	ldr	r8, .L2191
	cmp	r3, #0
	bne	.L2176
	mov	r0, r3
	ldr	r6, [sp, #36]
	ldr	r3, [sp, #16]
	rsb	r1, r3, r6
	bl	rk_ftl_garbage_collect
	ldr	r3, .L2191+36
	ldrh	r3, [r8, r3]
	cmp	r3, #31
	bhi	.L2149
	ldr	r2, .L2191+40
	mov	r3, #128
	mov	r4, #16
	strh	r3, [r8, r2]	@ movhi
	sub	r2, r2, #2
	strh	r3, [r8, r2]	@ movhi
.L2177:
	mov	r0, #0
	mov	r1, #1
	bl	rk_ftl_garbage_collect
	ldr	r3, [r8, #-2092]
	cmp	r3, #0
	bne	.L2149
	subs	r4, r4, #1
	bne	.L2177
.L2149:
	mov	r0, #0
	add	sp, sp, #100
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, sl, fp, pc}
.L2192:
	.align	2
.L2191:
	.word	.LANCHOR2
	.word	.LANCHOR0
	.word	.LANCHOR2-2044
	.word	.LANCHOR1
	.word	-1992
	.word	.LANCHOR2-1996
	.word	-2040
	.word	.LC135
	.word	-3947
	.word	-2048
	.word	-1190
	.fnend
	.size	FtlWrite.part.23, .-FtlWrite.part.23
	.align	2
	.global	FtlWrite
	.type	FtlWrite, %function
FtlWrite:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	@ link register save eliminated.
	mov	ip, r0
	mov	r0, r1
	mov	r1, r2
	mov	r2, r3
	ldr	r3, .L2198
	ldr	r3, [r3, #-2092]
	cmp	r3, #0
	movne	r0, #0
	bxne	lr
	cmp	ip, #16
	bne	.L2195
	add	r0, r0, #256
	b	FtlVendorPartWrite
.L2195:
	ldr	r3, .L2198+4
	add	ip, r1, r0
	ldr	r3, [r3, #3948]
	cmp	ip, r3
	bhi	.L2197
	b	FtlWrite.part.23
.L2197:
	mvn	r0, #0
	bx	lr
.L2199:
	.align	2
.L2198:
	.word	.LANCHOR2
	.word	.LANCHOR0
	.fnend
	.size	FtlWrite, .-FtlWrite
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
	mov	r3, r4
	add	r1, r5, #256
	mov	r2, #1
	mov	r0, #16
	bl	FtlWrite
	mov	r4, r0
	bl	rknand_device_unlock
	mov	r0, r4
	ldmfd	sp!, {r3, r4, r5, pc}
	.fnend
	.size	StorageSysDataStore, .-StorageSysDataStore
	.align	2
	.global	FtlRead
	.type	FtlRead, %function
FtlRead:
	.fnstart
	@ args = 0, pretend = 0, frame = 64
	@ frame_needed = 0, uses_anonymous_args = 0
	stmfd	sp!, {r4, r5, r6, r7, r8, r9, sl, fp, lr}
	.save {r4, r5, r6, r7, r8, r9, sl, fp, lr}
	cmp	r0, #16
	.pad #68
	sub	sp, sp, #68
	mov	r4, r1
	mov	r8, r3
	str	r2, [sp, #28]
	bne	.L2202
	add	r0, r1, #256
	mov	r1, r2
	mov	r2, r3
	bl	FtlVendorPartRead
	str	r0, [sp, #16]
	b	.L2203
.L2202:
	ldr	r3, [sp, #28]
	add	r3, r3, r1
	str	r3, [sp, #20]
	ldr	r3, .L2232
	ldr	r1, [sp, #20]
	ldr	r2, [r3, #3948]
	cmp	r1, r2
	mvnhi	r3, #0
	strhi	r3, [sp, #16]
	bhi	.L2203
	movw	r2, #3914
	mov	r0, r4
	ldrh	r5, [r3, r2]
	mov	r1, r5
	bl	__aeabi_uidiv
	ldr	r3, [sp, #20]
	mov	r1, r5
	str	r0, [sp, #12]
	sub	r0, r3, #1
	bl	__aeabi_uidiv
	ldr	r1, [sp, #12]
	rsb	r3, r1, #1
	ldr	r1, [sp, #28]
	add	r3, r3, r0
	str	r3, [sp, #8]
	ldr	r3, .L2232+4
	mov	fp, r0
	ldr	r0, [sp, #12]
	ldr	r2, [r3, #-1852]
	add	r2, r1, r2
	ldr	r1, [sp, #8]
	str	r2, [r3, #-1852]
	ldr	r2, [r3, #-1880]
	add	r2, r1, r2
	mov	r1, fp
	str	r2, [r3, #-1880]
	bl	FtlCacheMetchLpa
	cmp	r0, #0
	beq	.L2204
	bl	FtlCacheWriteBack
.L2204:
	mov	r9, #0
	ldr	r6, [sp, #12]
	str	r9, [sp, #32]
	mov	r5, r9
	str	r9, [sp, #16]
	ldr	r7, .L2232
	b	.L2227
.L2221:
	mov	r2, #0
	mov	r0, r6
	add	r1, sp, #60
	bl	log2phys
	ldr	r2, [sp, #60]
	cmn	r2, #1
	bne	.L2228
	b	.L2231
.L2209:
	mla	r0, r0, r6, sl
	cmp	r0, r4
	bcc	.L2208
	ldr	r2, [sp, #20]
	cmp	r0, r2
	bcs	.L2208
	rsb	r0, r4, r0
	mov	r1, #0
	mov	r2, #512
	str	r3, [sp, #4]
	add	r0, r8, r0, asl #9
	bl	ftl_memset
	ldr	r3, [sp, #4]
.L2208:
	add	sl, sl, #1
	b	.L2206
.L2231:
	mov	sl, #0
	movw	r3, #3914
.L2206:
	ldrh	r0, [r7, r3]
	cmp	sl, r0
	bcc	.L2209
	b	.L2210
.L2228:
	ldr	r3, .L2232+4
	mov	sl, #36
	ldr	r1, [r3, #-972]
	mla	sl, sl, r5, r1
	ldr	r1, [sp, #12]
	cmp	r6, r1
	str	r2, [sl, #4]
	movw	r2, #3914
	bne	.L2211
	ldr	r3, [r3, #-948]
	mov	r0, r4
	str	r3, [sl, #8]
	ldrh	r3, [r7, r2]
	mov	r1, r3
	str	r3, [sp, #4]
	bl	__aeabi_uidivmod
	ldr	r3, [sp, #4]
	str	r1, [sp, #36]
	rsb	r2, r1, r3
	ldr	r1, [sp, #28]
	cmp	r2, r1
	movcc	r1, r2
	str	r1, [sp, #32]
	cmp	r1, r3
	streq	r8, [sl, #8]
	b	.L2212
.L2211:
	cmp	r6, fp
	ldrneh	r3, [r7, r2]
	mulne	r3, r3, r6
	bne	.L2229
	ldr	r3, [r3, #-944]
	ldrh	r2, [r7, r2]
	ldr	r1, [sp, #20]
	str	r3, [sl, #8]
	mul	r3, r2, r6
	rsb	r9, r3, r1
	cmp	r9, r2
	bne	.L2212
.L2229:
	rsb	r3, r4, r3
	add	r3, r8, r3, asl #9
	str	r3, [sl, #8]
.L2212:
	mov	r3, #3920
	ldr	r2, .L2232+4
	ldrh	r3, [r7, r3]
	str	r6, [sl, #16]
	ldr	r2, [r2, #-936]
	mul	r3, r3, r5
	add	r5, r5, #1
	bic	r3, r3, #3
	add	r3, r2, r3
	str	r3, [sl, #12]
.L2210:
	ldr	r3, [sp, #8]
	add	r6, r6, #1
	subs	r3, r3, #1
	str	r3, [sp, #8]
	beq	.L2214
	mov	r3, #3840
	ldrh	r3, [r7, r3]
	cmp	r5, r3, asl #3
	bne	.L2227
.L2214:
	cmp	r5, #0
	beq	.L2227
	ldr	sl, .L2232+4
	mov	r1, r5
	mov	r2, #0
	ldr	r0, [sl, #-972]
	bl	FlashReadPages
	ldr	r3, [sp, #36]
	str	r6, [sp, #52]
	mov	r6, r5
	mov	r3, r3, asl #9
	str	r3, [sp, #40]
	ldr	r3, [sp, #32]
	mov	r3, r3, asl #9
	str	r3, [sp, #44]
	mov	r3, r9, asl #9
	str	r3, [sp, #48]
	mov	r3, #0
	str	r3, [sp, #24]
.L2220:
	ldr	r3, [sp, #24]
	mov	r5, #36
	ldr	r1, [sp, #12]
	mul	r5, r5, r3
	ldr	r3, [sl, #-972]
	add	r3, r3, r5
	ldr	r2, [r3, #16]
	cmp	r2, r1
	bne	.L2216
	ldr	r1, [r3, #8]
	ldr	r3, [sl, #-948]
	cmp	r1, r3
	bne	.L2217
	ldr	r3, [sp, #40]
	mov	r0, r8
	ldr	r2, [sp, #44]
	add	r1, r1, r3
	b	.L2230
.L2216:
	cmp	r2, fp
	bne	.L2217
	ldr	r1, [r3, #8]
	ldr	r3, [sl, #-944]
	cmp	r1, r3
	bne	.L2217
	movw	r2, #3914
	ldrh	r0, [r7, r2]
	ldr	r2, [sp, #48]
	mul	r0, r0, fp
	rsb	r0, r4, r0
	add	r0, r8, r0, asl #9
.L2230:
	bl	memcpy
.L2217:
	ldr	r3, [sl, #-972]
	add	r0, r3, r5
	ldr	r2, [r3, r5]
	cmn	r2, #1
	streq	r2, [sp, #16]
	ldreq	r1, [sl, #-1632]
	addeq	r1, r1, #1
	streq	r1, [sl, #-1632]
	ldr	r3, [r3, r5]
	cmp	r3, #256
	bne	.L2219
	ldr	r0, [r0, #4]
	ubfx	r0, r0, #10, #16
	bl	P2V_block_in_plane
	bl	FtlGcRefreshBlock
.L2219:
	ldr	r3, [sp, #24]
	add	r3, r3, #1
	str	r3, [sp, #24]
	cmp	r3, r6
	bne	.L2220
	ldr	r6, [sp, #52]
	mov	r5, #0
.L2227:
	ldr	r3, [sp, #8]
	cmp	r3, #0
	bne	.L2221
	ldr	r3, .L2232+8
	ldr	r2, .L2232+4
	ldrh	r3, [r2, r3]
	cmp	r3, #0
	beq	.L2203
	ldr	r0, [sp, #8]
	mov	r1, #1
	bl	rk_ftl_garbage_collect
.L2203:
	ldr	r0, [sp, #16]
	add	sp, sp, #68
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, sl, fp, pc}
.L2233:
	.align	2
.L2232:
	.word	.LANCHOR0
	.word	.LANCHOR2
	.word	-1134
	.fnend
	.size	FtlRead, .-FtlRead
	.align	2
	.global	StorageSysDataLoad
	.type	StorageSysDataLoad, %function
StorageSysDataLoad:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	stmfd	sp!, {r3, r4, r5, lr}
	.save {r3, r4, r5, lr}
	mov	r5, r0
	mov	r4, r1
	mov	r0, r1
	mov	r1, #512
	bl	__memzero
	bl	rknand_device_lock
	mov	r3, r4
	add	r1, r5, #256
	mov	r2, #1
	mov	r0, #16
	bl	FtlRead
	mov	r4, r0
	bl	rknand_device_unlock
	mov	r0, r4
	ldmfd	sp!, {r3, r4, r5, pc}
	.fnend
	.size	StorageSysDataLoad, .-StorageSysDataLoad
	.align	2
	.type	rk_ftl_vendor_ops.constprop.28, %function
rk_ftl_vendor_ops.constprop.28:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	stmfd	sp!, {r4, r5, r6, lr}
	.save {r4, r5, r6, lr}
	mov	r4, r0
	mov	r6, r2
	mov	r5, r1
	bl	rknand_device_lock
	cmp	r6, #0
	mov	r0, #16
	mov	r1, r5
	mov	r2, #128
	mov	r3, r4
	beq	.L2236
	bl	FtlWrite
	b	.L2238
.L2236:
	bl	FtlRead
.L2238:
	mov	r4, r0
	bl	rknand_device_unlock
	mov	r0, r4
	ldmfd	sp!, {r4, r5, r6, pc}
	.fnend
	.size	rk_ftl_vendor_ops.constprop.28, .-rk_ftl_vendor_ops.constprop.28
	.align	2
	.global	FlashLoadFactorBbt
	.type	FlashLoadFactorBbt, %function
FlashLoadFactorBbt:
	.fnstart
	@ args = 0, pretend = 0, frame = 48
	@ frame_needed = 0, uses_anonymous_args = 0
	stmfd	sp!, {r4, r5, r6, r7, r8, r9, sl, fp, lr}
	.save {r4, r5, r6, r7, r8, r9, sl, fp, lr}
	movw	r2, #3062
	ldr	r9, .L2248
	movw	r3, #3060
	ldr	r4, .L2248+4
	.pad #52
	sub	sp, sp, #52
	mov	r1, #0
	mvn	r7, #0
	ldrh	r3, [r9, r3]
	add	r0, r4, #228
	ldrh	r8, [r9, r2]
	mov	r2, #16
	mov	r6, #0
	mul	r8, r8, r3
	bl	ftl_memset
	ldr	r3, [r4, #224]
	str	r6, [sp, #20]
	mov	r4, r6
	str	r3, [sp, #24]
	uxth	r8, r8
	add	r3, r8, r7
	uxth	r3, r3
	str	r3, [sp, #4]
	b	.L2240
.L2246:
	ldr	r5, [sp, #4]
	sub	r3, r8, #12
	mul	fp, r8, r4
	ldr	sl, .L2248+4
	b	.L2241
.L2244:
	add	r2, fp, r5
	mov	r1, #1
	add	r0, sp, #12
	str	r3, [sp, #0]
	mov	r2, r2, asl #10
	str	r2, [sp, #16]
	mov	r2, r1
	bl	FlashReadPages
	ldr	r2, [sp, #12]
	ldr	r3, [sp, #0]
	cmn	r2, #1
	beq	.L2242
	ldr	r2, [sl, #224]
	ldrh	r1, [r2, #0]
	movw	r2, #61664
	cmp	r1, r2
	bne	.L2242
	ldr	r0, .L2248+8
	mov	r1, r4
	mov	r2, r5
	add	r6, r6, #1
	bl	printk
	ldr	r3, .L2248+4
	uxth	r6, r6
	add	r3, r3, r4, asl #1
	strh	r5, [r3, #228]	@ movhi
	b	.L2243
.L2242:
	sub	r5, r5, #1
	uxth	r5, r5
.L2241:
	cmp	r5, r3
	bgt	.L2244
.L2243:
	ldrb	r3, [r9, #3762]	@ zero_extendqisi2
	add	r4, r4, #1
	cmp	r3, r6
	uxtb	r4, r4
	moveq	r7, #0
.L2240:
	ldrb	r3, [r9, #3762]	@ zero_extendqisi2
	cmp	r3, r4
	bhi	.L2246
	mov	r0, r7
	add	sp, sp, #52
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, sl, fp, pc}
.L2249:
	.align	2
.L2248:
	.word	.LANCHOR0
	.word	.LANCHOR2
	.word	.LC136
	.fnend
	.size	FlashLoadFactorBbt, .-FlashLoadFactorBbt
	.align	2
	.global	FlashReadFacBbtData
	.type	FlashReadFacBbtData, %function
FlashReadFacBbtData:
	.fnstart
	@ args = 0, pretend = 0, frame = 40
	@ frame_needed = 0, uses_anonymous_args = 0
	ldr	r3, .L2260
	stmfd	sp!, {r4, r5, r6, r7, r8, r9, sl, lr}
	.save {r4, r5, r6, r7, r8, r9, sl, lr}
	mov	r6, r1
	mov	r9, r2
	movw	r1, #3062
	movw	r2, #3060
	ldrh	r8, [r3, r1]
	ldrh	r2, [r3, r2]
	.pad #40
	sub	sp, sp, #40
	ldr	r7, .L2260+4
	mov	r4, r0
	mul	r8, r8, r2
	ldr	r3, [r7, #144]
	str	r3, [sp, #12]
	ldr	r3, [r7, #224]
	uxth	r8, r8
	sub	r5, r8, #1
	mul	sl, r8, r6
	str	r3, [sp, #16]
	uxth	r5, r5
	sub	r8, r8, #16
	b	.L2251
.L2257:
	mov	r1, #1
	add	r3, r5, sl
	add	r0, sp, #4
	mov	r2, r1
	mov	r3, r3, asl #10
	str	r3, [sp, #8]
	bl	FlashReadPages
	ldr	r3, [sp, #4]
	cmn	r3, #1
	beq	.L2252
	ldr	r3, [r7, #224]
	ldrh	r2, [r3, #0]
	movw	r3, #61664
	cmp	r2, r3
	bne	.L2252
	cmp	r4, #0
	moveq	r0, r4
	beq	.L2253
	cmp	r6, #0
	ldreq	r3, .L2260+4
	moveq	r0, #1
	beq	.L2254
	b	.L2255
.L2256:
	ldr	r2, [r3, #144]
	ubfx	r1, r6, #5, #16
	and	lr, r6, #31
	add	r6, r6, #1
	ldr	ip, [r2, r1, asl #2]
	uxth	r6, r6
	orr	ip, ip, r0, asl lr
	str	ip, [r2, r1, asl #2]
.L2254:
	ldr	r2, [r3, #152]
	cmp	r6, r2
	bcc	.L2256
.L2255:
	ldr	r3, .L2260+4
	mov	r2, r9
	mov	r0, r4
	ldr	r1, [r3, #144]
	bl	memcpy
	mov	r2, #4
	ldr	r0, .L2260+8
	mov	r1, r4
	mov	r3, r2
	bl	rknand_print_hex
	mov	r0, #0
	b	.L2253
.L2252:
	sub	r5, r5, #1
	uxth	r5, r5
.L2251:
	cmp	r5, r8
	bgt	.L2257
	mvn	r0, #0
.L2253:
	add	sp, sp, #40
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, sl, pc}
.L2261:
	.align	2
.L2260:
	.word	.LANCHOR0
	.word	.LANCHOR2
	.word	.LC137
	.fnend
	.size	FlashReadFacBbtData, .-FlashReadFacBbtData
	.align	2
	.global	FlashGetBadBlockList
	.type	FlashGetBadBlockList, %function
FlashGetBadBlockList:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	ldr	r3, .L2269
	stmfd	sp!, {r4, r5, r6, r7, r8, lr}
	.save {r4, r5, r6, r7, r8, lr}
	mov	r5, r0
	ldr	r3, [r3, #3624]
	ldr	r6, .L2269+4
	ldrb	r2, [r3, #13]	@ zero_extendqisi2
	ldrh	r4, [r3, #14]
	ldr	r0, [r6, #132]
	mul	r4, r4, r2
	uxth	r4, r4
	add	r2, r4, #7
	mov	r2, r2, lsr #3
	bl	FlashReadFacBbtData
	cmn	r0, #1
	beq	.L2268
	mov	r2, #0
	mov	r0, r4, lsr #4
	mov	r3, r2
	sub	r4, r4, #1
	mov	r1, #1
	b	.L2264
.L2266:
	ldr	ip, [r6, #132]
	mov	r8, r2, lsr #5
	and	r7, r2, #31
	ldr	ip, [ip, r8, asl #2]
	ands	ip, ip, r1, asl r7
	movne	ip, r3, asl #1
	addne	r3, r3, #1
	uxthne	r3, r3
	strneh	r2, [r5, ip]	@ movhi
	cmp	r3, r0
	bcs	.L2268
	add	r2, r2, #1
	uxth	r2, r2
.L2264:
	cmp	r2, r4
	blt	.L2266
	b	.L2263
.L2268:
	mov	r3, #0
.L2263:
	mov	r3, r3, asl #1
	mvn	r2, #0
	mov	r0, #0
	strh	r2, [r5, r3]	@ movhi
	ldmfd	sp!, {r4, r5, r6, r7, r8, pc}
.L2270:
	.align	2
.L2269:
	.word	.LANCHOR0
	.word	.LANCHOR2
	.fnend
	.size	FlashGetBadBlockList, .-FlashGetBadBlockList
	.align	2
	.global	FtlMakeBbt
	.type	FtlMakeBbt, %function
FtlMakeBbt:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	stmfd	sp!, {r3, r4, r5, r6, r7, r8, r9, sl, fp, lr}
	.save {r3, r4, r5, r6, r7, r8, r9, sl, fp, lr}
	ldr	r4, .L2289
	ldr	r5, [r4, #-2092]
	cmp	r5, #0
	bne	.L2272
	ldr	r7, .L2289+4
	bl	FtlBbtMemInit
	ldr	r6, .L2289+8
	bl	FtlLoadFactoryBbt
	add	sl, r7, #12
	b	.L2273
.L2279:
	ldrh	r3, [sl], #2
	movw	r2, #65535
	ldr	r0, [r4, #-2076]
	ldr	r9, [r4, #-940]
	cmp	r3, r2
	str	r0, [r4, #184]
	str	r9, [r4, #188]
	beq	.L2274
	mov	r9, #3904
	mov	r1, #1
	ldrh	r8, [r6, r9]
	mov	r2, r1
	ldr	r0, .L2289+12
	mla	r8, r8, r5, r3
	mov	r3, r8, asl #10
	str	r3, [r4, #180]
	bl	FlashReadPages
	ldrh	r2, [r6, r9]
	ldr	r0, [r7, #28]
	add	r2, r2, #7
	ldr	r1, [r4, #184]
	mov	r2, r2, lsr #3
	bl	memcpy
	b	.L2275
.L2274:
	mov	r1, r5
	bl	FlashGetBadBlockList
	ldr	r0, [r4, #184]
	ldr	r1, [r7, #28]
	bl	FtlBbt2Bitmap
	mov	r3, #3904
	ldrh	fp, [r6, r3]
.L2277:
	sub	fp, fp, #1
	uxth	fp, fp
.L2288:
	mov	r8, #3904
	ldrh	r0, [r6, r8]
	mla	r0, r0, r5, fp
	uxth	r0, r0
	bl	FtlBbmIsBadBlock
	cmp	r0, #1
	beq	.L2277
	mov	r1, #0
	mov	r2, #16
	strh	fp, [sl, #-2]	@ movhi
	ldr	r0, [r4, #-940]
	bl	ftl_memset
	ldr	r3, .L2289+16
	strh	r3, [r9, #0]	@ movhi
	mov	r3, #0
	str	r3, [r9, #4]
	ldrh	r3, [sl, #-2]
	ldrh	r8, [r6, r8]
	strh	r3, [r9, #2]	@ movhi
	ldrh	r3, [sl, #-2]
	ldr	r1, [r7, #28]
	ldr	r0, [r4, #184]
	mla	r8, r8, r5, r3
	mov	r3, r8, asl #10
	str	r3, [r4, #180]
	ldr	r3, .L2289+20
	ldrh	r2, [r4, r3]
	mov	r2, r2, asl #2
	bl	memcpy
	mov	r1, #1
	mov	r2, r1
	ldr	r0, .L2289+12
	bl	FlashEraseBlocks
	mov	r1, #1
	mov	r3, r1
	ldr	r0, .L2289+12
	mov	r2, r1
	bl	FlashProgPages
	ldr	r3, [r4, #176]
	cmn	r3, #1
	bne	.L2275
	uxth	r0, r8
	bl	FtlBbmMapBadBlock
	b	.L2288
.L2275:
	uxth	r0, r8
	add	r5, r5, #1
	bl	FtlBbmMapBadBlock
	add	r7, r7, #4
.L2273:
	movw	r3, #3862
	ldrh	r3, [r6, r3]
	cmp	r5, r3
	bcc	.L2279
	mov	r4, #0
	ldr	r7, .L2289+8
	movw	r6, #3922
	b	.L2280
.L2281:
	mov	r0, r4
	add	r4, r4, #1
	bl	FtlBbmMapBadBlock
	uxth	r4, r4
.L2280:
	ldrh	r3, [r7, r6]
	ldr	r5, .L2289+8
	cmp	r3, r4
	bhi	.L2281
	movw	r7, #3988
	movw	r6, #3976
	ldrh	r4, [r5, r7]
	sub	r4, r4, #1
	uxth	r4, r4
	b	.L2282
.L2287:
	mov	r0, r4
	bl	FtlBbmIsBadBlock
	cmp	r0, #1
	beq	.L2283
	mov	r0, r4
	bl	FlashTestBlk
	cmp	r0, #0
	beq	.L2284
	mov	r0, r4
	bl	FtlBbmMapBadBlock
	b	.L2283
.L2284:
	ldrh	r2, [r5, r6]
	movw	r3, #65535
	cmp	r2, r3
	streqh	r4, [r5, r6]	@ movhi
.L2285:
	ldrne	r2, .L2289+8
	movwne	r3, #3980
	strneh	r4, [r2, r3]	@ movhi
	bne	.L2286
.L2283:
	sub	r4, r4, #1
	uxth	r4, r4
.L2282:
	ldrh	r3, [r5, r7]
	sub	r3, r3, #48
	cmp	r4, r3
	bgt	.L2287
.L2286:
	ldr	r4, .L2289+8
	movw	r6, #3976
	ldr	r3, .L2289
	movw	r5, #3980
	mov	r7, #0
	movw	r8, #3978
	str	r7, [r4, #3984]
	mov	r2, #2
	ldr	r0, [r3, #-2088]
	mov	r1, #1
	ldrh	r3, [r4, r6]
	strh	r7, [r4, r8]	@ movhi
	mov	r3, r3, asl #10
	str	r3, [r0, #4]
	ldrh	r3, [r4, r5]
	mov	r3, r3, asl #10
	str	r3, [r0, #40]
	bl	FlashEraseBlocks
	ldrh	r0, [r4, r6]
	bl	FtlBbmMapBadBlock
	ldrh	r0, [r4, r5]
	bl	FtlBbmMapBadBlock
	bl	FtlBbmTblFlush
	ldr	r3, [r4, #3984]
	ldrh	r2, [r4, r5]
	add	r3, r3, #1
	str	r3, [r4, #3984]
	ldrh	r3, [r4, r6]
	strh	r7, [r4, r8]	@ movhi
	strh	r2, [r4, r6]	@ movhi
	strh	r3, [r4, r5]	@ movhi
	bl	FtlBbmTblFlush
.L2272:
	mov	r0, #0
	ldmfd	sp!, {r3, r4, r5, r6, r7, r8, r9, sl, fp, pc}
.L2290:
	.align	2
.L2289:
	.word	.LANCHOR2
	.word	.LANCHOR0+3976
	.word	.LANCHOR0
	.word	.LANCHOR2+176
	.word	-3872
	.word	-1024
	.fnend
	.size	FtlMakeBbt, .-FtlMakeBbt
	.align	2
	.type	FtlLowFormat.part.26, %function
FtlLowFormat.part.26:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	stmfd	sp!, {r3, r4, r5, r6, r7, r8, r9, sl, fp, lr}
	.save {r3, r4, r5, r6, r7, r8, r9, sl, fp, lr}
	mov	r2, #0
	ldr	r3, .L2312
	str	r2, [r3, #-1848]
	str	r2, [r3, #-1844]
	ldr	r3, .L2312+4
	ldr	r0, [r3, #3844]
	uxth	r0, r0
	bl	FtlFreeSysBlkQueueInit
	bl	FtlLoadBbt
	cmp	r0, #0
	beq	.L2292
	bl	FtlMakeBbt
.L2292:
	mov	r3, #0
	ldr	ip, .L2312+4
	movw	r0, #3914
	ldr	r2, .L2312
	ldr	r1, .L2312+8
	b	.L2293
.L2294:
	ldr	lr, [r2, #-948]
	mvn	r4, r3
	orr	r4, r3, r4, asl #16
	str	r4, [lr, r3, asl #2]
	ldr	lr, [r2, #-944]
	str	r1, [lr, r3, asl #2]
	add	r3, r3, #1
	uxth	r3, r3
.L2293:
	ldrh	lr, [ip, r0]
	ldr	r8, .L2312+4
	cmp	r3, lr, asl #7
	blt	.L2294
	movw	r3, #3848
	mov	r5, #0
	ldrh	r6, [r8, r3]
	movw	r7, #3850
	b	.L2295
.L2296:
	mov	r0, r6
	mov	r1, #1
	bl	FtlLowFormatEraseBlock
	add	r6, r6, #1
	uxth	r6, r6
	add	r5, r5, r0
	uxth	r5, r5
.L2295:
	ldrh	r3, [r8, r7]
	ldr	r4, .L2312+4
	cmp	r3, r6
	bhi	.L2296
	mov	r3, #3840
	ldrh	r1, [r4, r3]
	sub	r3, r5, #3
	cmp	r3, r1, asl #1
	blt	.L2297
	mov	r0, r5
	movw	r6, #3850
	bl	__aeabi_uidiv
	ldr	r3, [r4, #3940]
	add	r0, r0, r3
	uxth	r0, r0
	bl	FtlSysBlkNumInit
	ldr	r0, [r4, #3844]
	uxth	r0, r0
	bl	FtlFreeSysBlkQueueInit
	movw	r3, #3848
	ldrh	r5, [r4, r3]
	b	.L2298
.L2299:
	mov	r0, r5
	mov	r1, #1
	bl	FtlLowFormatEraseBlock
	add	r5, r5, #1
	uxth	r5, r5
.L2298:
	ldrh	r3, [r4, r6]
	cmp	r3, r5
	bhi	.L2299
.L2297:
	mov	r4, #0
	ldr	r8, .L2312+4
	mov	r6, r4
	movw	r7, #3848
	b	.L2300
.L2301:
	mov	r0, r6
	mov	r1, #0
	bl	FtlLowFormatEraseBlock
	add	r6, r6, #1
	uxth	r6, r6
	add	r4, r4, r0
	uxth	r4, r4
.L2300:
	ldrh	r3, [r8, r7]
	ldr	r5, .L2312+4
	cmp	r3, r6
	bhi	.L2301
	movw	r3, #3850
	ldr	fp, [r5, #3852]
	ldrh	r3, [r5, r3]
	ldr	r9, .L2312
	mov	r0, fp
	ldr	sl, .L2312+12
	str	r3, [r5, #3972]
	mov	r3, #3840
	ldrh	r6, [r5, r3]
	mov	r1, r6
	bl	__aeabi_uidiv
	ubfx	r8, r0, #5, #16
	mov	r7, r0
	add	r3, r8, #36
	strh	r3, [r9, sl]	@ movhi
	mov	r3, #24
	str	r0, [r5, #3968]
	mul	r3, r3, r6
	cmp	r4, r3
	ble	.L2302
	rsb	r0, r4, fp
	mov	r1, r6
	bl	__aeabi_uidiv
	str	r0, [r5, #3968]
	mov	r0, r0, lsr #5
	add	r0, r0, #24
	strh	r0, [r9, sl]	@ movhi
.L2302:
	ldr	r5, .L2312
	ldr	r3, [r5, #-1884]
	cmp	r3, #1
	bne	.L2303
	mov	r0, r4
	mov	r1, r6
	bl	__aeabi_uidiv
	ldr	sl, .L2312+12
	ldrh	r9, [r5, sl]
	uxtah	r0, r9, r0
	add	r9, r9, r0, lsr #2
	strh	r9, [r5, sl]	@ movhi
.L2303:
	ldr	r3, .L2312+4
	ldrb	r3, [r3, #928]	@ zero_extendqisi2
	cmp	r3, #0
	beq	.L2304
	mov	r0, r4
	mov	r1, r6
	bl	__aeabi_uidiv
	ldr	sl, .L2312
	ldr	r5, .L2312+12
	ldrh	r9, [sl, r5]
	uxtah	r0, r9, r0
	add	r9, r9, r0, lsr #2
	strh	r9, [sl, r5]	@ movhi
.L2304:
	ldr	r0, .L2312+4
	movw	r3, #3900
	ldrh	r3, [r0, r3]
	cmp	r3, #0
	beq	.L2305
	ldr	r1, .L2312
	ldr	r2, .L2312+12
	ldrh	ip, [r1, r2]
	add	ip, ip, r3, lsr #1
	strh	ip, [r1, r2]	@ movhi
	mul	ip, r6, r3
	cmp	ip, r4
	strgt	r7, [r0, #3968]
	addgt	r3, r3, #32
	addgt	r8, r8, r3
	strgth	r8, [r1, r2]	@ movhi
.L2305:
	ldr	r4, .L2312
	ldr	r5, .L2312+4
	ldr	r3, .L2312+12
	ldr	r7, .L2312+16
	ldr	r2, [r5, #3968]
	ldrh	r3, [r4, r3]
	rsb	r3, r3, r2
	mul	r6, r6, r3
	movw	r3, #3908
	ldrh	r3, [r5, r3]
	str	r6, [r4, #212]
	mul	r6, r3, r6
	movw	r3, #3914
	ldrh	r3, [r5, r3]
	str	r6, [r5, #3968]
	mul	r6, r3, r6
	str	r6, [r5, #3948]
	mvn	r6, #0
	bl	FtlBbmTblFlush
	movw	r3, #3850
	ldrh	r2, [r5, r3]
	mov	r1, #0
	ldr	r0, [r4, #-2064]
	mov	r2, r2, asl #1
	bl	ftl_memset
	ldr	r1, .L2312+20
	mov	r3, #0
	ldr	r2, .L2312+24
	str	r3, [r4, #-1892]
	strh	r3, [r4, r1]	@ movhi
	sub	r1, r1, #288
	strb	r3, [r4, #-1750]
	strh	r3, [r4, r1]	@ movhi
	mov	r1, #255
	strb	r3, [r4, #-1748]
	strb	r3, [r4, #-2038]
	strh	r3, [r4, r7]	@ movhi
	mov	r3, #1
	strb	r3, [r4, #-2036]
	movw	r3, #3848
	strh	r6, [r4, r2]	@ movhi
	ldrh	r2, [r5, r3]
	ldr	r0, [r4, #-1888]
	mov	r2, r2, lsr #3
	bl	ftl_memset
.L2306:
	ldr	r0, .L2312+28
	bl	make_superblock
	ldrb	r3, [r4, #-2037]	@ zero_extendqisi2
	ldr	r5, .L2312
	cmp	r3, #0
	ldr	r3, .L2312+16
	bne	.L2307
	ldrh	r3, [r4, r7]
	ldr	r2, [r4, #-2064]
	mov	r3, r3, asl #1
	strh	r6, [r2, r3]	@ movhi
	ldrh	r3, [r4, r7]
	add	r3, r3, #1
	strh	r3, [r4, r7]	@ movhi
	b	.L2306
.L2307:
	ldr	r2, [r5, #-1848]
	mvn	r7, #0
	ldr	r1, [r5, #-2064]
	ldr	r6, .L2312+32
	str	r2, [r5, #-2032]
	add	r2, r2, #1
	str	r2, [r5, #-1848]
	ldr	r2, .L2312+36
	ldrh	r0, [r5, r2]
	ldrh	r2, [r5, r3]
	mov	r2, r2, asl #1
	strh	r0, [r1, r2]	@ movhi
	mov	r2, #0
	ldr	r0, .L2312+40
	ldrh	r3, [r5, r3]
	strb	r2, [r5, #-1990]
	add	r3, r3, #1
	strh	r2, [r5, r0]	@ movhi
	strh	r3, [r5, r6]	@ movhi
	mov	r3, #1
	strb	r3, [r5, #-1988]
.L2308:
	ldr	r0, .L2312+44
	bl	make_superblock
	ldrb	r3, [r5, #-1989]	@ zero_extendqisi2
	ldr	r4, .L2312
	cmp	r3, #0
	bne	.L2309
	ldrh	r3, [r5, r6]
	ldr	r2, [r5, #-2064]
	mov	r3, r3, asl #1
	strh	r7, [r2, r3]	@ movhi
	ldrh	r3, [r5, r6]
	add	r3, r3, #1
	strh	r3, [r5, r6]	@ movhi
	b	.L2308
.L2309:
	ldr	r3, [r4, #-1848]
	mvn	r5, #0
	ldr	r2, [r4, #-2064]
	str	r3, [r4, #-1984]
	add	r3, r3, #1
	str	r3, [r4, #-1848]
	ldr	r3, .L2312+48
	ldrh	r1, [r4, r3]
	sub	r3, r3, #4
	ldrh	r3, [r4, r3]
	mov	r3, r3, asl #1
	strh	r1, [r2, r3]	@ movhi
	ldr	r3, .L2312+52
	strh	r5, [r4, r3]	@ movhi
	bl	FtlFreeSysBlkQueueOut
	ldr	r3, .L2312+56
	mov	r2, #0
	strh	r0, [r4, r3]	@ movhi
	add	r3, r3, #2
	strh	r2, [r4, r3]	@ movhi
	add	r3, r3, #2
	ldr	r2, [r4, #212]
	strh	r5, [r4, r3]	@ movhi
	add	r3, r3, #2
	strh	r2, [r4, r3]	@ movhi
	ldr	r3, [r4, #-1848]
	str	r3, [r4, #-1760]
	add	r3, r3, #1
	str	r3, [r4, #-1848]
	bl	FtlVpcTblFlush
	bl	FtlSysBlkInit
	cmp	r0, #0
	ldmnefd	sp!, {r3, r4, r5, r6, r7, r8, r9, sl, fp, pc}
	ldr	r3, .L2312+60
	mov	r2, #1
	str	r2, [r3, #2932]
	ldmfd	sp!, {r3, r4, r5, r6, r7, r8, r9, sl, fp, pc}
.L2313:
	.align	2
.L2312:
	.word	.LANCHOR2
	.word	.LANCHOR0
	.word	168778952
	.word	-1772
	.word	-2044
	.word	-1754
	.word	-1756
	.word	.LANCHOR2-2044
	.word	-1996
	.word	-2040
	.word	-1994
	.word	.LANCHOR2-1996
	.word	-1992
	.word	-1948
	.word	-1768
	.word	.LANCHOR1
	.fnend
	.size	FtlLowFormat.part.26, .-FtlLowFormat.part.26
	.align	2
	.global	FtlLowFormat
	.type	FtlLowFormat, %function
FtlLowFormat:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	stmfd	sp!, {r3, lr}
	.save {r3, lr}
	ldr	r3, .L2316
	ldr	r3, [r3, #-2092]
	cmp	r3, #0
	bne	.L2315
	bl	FtlLowFormat.part.26
.L2315:
	mov	r0, #0
	ldmfd	sp!, {r3, pc}
.L2317:
	.align	2
.L2316:
	.word	.LANCHOR2
	.fnend
	.size	FtlLowFormat, .-FtlLowFormat
	.align	2
	.global	HynixGetReadRetryDefault
	.type	HynixGetReadRetryDefault, %function
HynixGetReadRetryDefault:
	.fnstart
	@ args = 0, pretend = 0, frame = 64
	@ frame_needed = 0, uses_anonymous_args = 0
	ldr	r3, .L2403
	mvn	r2, #83
	stmfd	sp!, {r4, r5, r6, r7, r8, r9, sl, fp, lr}
	.save {r4, r5, r6, r7, r8, r9, sl, fp, lr}
	mov	r4, r0
	cmp	r4, #2
	strb	r0, [r3, #0]
	strb	r2, [r3, #4]
	mvn	r0, #82
	mvn	r1, #81
	mvn	r2, #80
	.pad #68
	sub	sp, sp, #68
	strb	r0, [r3, #5]
	strb	r1, [r3, #6]
	strb	r2, [r3, #7]
	bne	.L2319
	mvn	r2, #88
	strb	r2, [r3, #4]
	ldr	r3, .L2403+4
	mvn	r2, #8
	strb	r2, [r3, #2961]
	b	.L2375
.L2319:
	cmp	r4, #3
	bne	.L2321
	mvn	r2, #79
	strb	r2, [r3, #4]
	mvn	r2, #78
	strb	r2, [r3, #5]
	mvn	r2, #77
	strb	r2, [r3, #6]
	mvn	r2, #76
	strb	r2, [r3, #7]
	mvn	r2, #75
	strb	r2, [r3, #8]
	mvn	r2, #74
	strb	r2, [r3, #9]
	mvn	r2, #73
	strb	r2, [r3, #10]
	mvn	r2, #72
	b	.L2398
.L2321:
	cmp	r4, #4
	bne	.L2322
	mvn	ip, #51
	strb	r0, [r3, #9]
	strb	ip, [r3, #4]
	mvn	ip, #64
	strb	r1, [r3, #10]
	strb	ip, [r3, #5]
	mvn	ip, #85
	strb	ip, [r3, #6]
	mvn	ip, #84
	strb	ip, [r3, #7]
	mvn	ip, #50
	strb	ip, [r3, #8]
.L2398:
	mov	r6, #8
	strb	r2, [r3, #11]
	mov	r5, r6
	b	.L2320
.L2322:
	cmp	r4, #5
	bne	.L2323
	mov	r2, #56
	mov	r6, #8
	strb	r2, [r3, #4]
	mov	r2, #57
	strb	r2, [r3, #5]
	mov	r2, #58
	strb	r2, [r3, #6]
	mov	r2, #59
	strb	r2, [r3, #7]
	b	.L2397
.L2323:
	cmp	r4, #6
	bne	.L2324
	mov	r2, #14
	mov	r6, #12
	strb	r2, [r3, #4]
	mov	r2, #15
	strb	r2, [r3, #5]
	mov	r2, #16
	strb	r2, [r3, #6]
	mov	r2, #17
	strb	r2, [r3, #7]
	b	.L2397
.L2324:
	cmp	r4, #7
	bne	.L2375
	mvn	r2, #79
	mov	r6, #12
	strb	r2, [r3, #4]
	mov	r5, #10
	mvn	r2, #78
	strb	r2, [r3, #5]
	mvn	r2, #77
	strb	r2, [r3, #6]
	mvn	r2, #76
	strb	r2, [r3, #7]
	mvn	r2, #75
	strb	r2, [r3, #8]
	mvn	r2, #74
	strb	r2, [r3, #9]
	mvn	r2, #73
	strb	r2, [r3, #10]
	mvn	r2, #72
	strb	r2, [r3, #11]
	mvn	r2, #43
	strb	r2, [r3, #12]
	mvn	r2, #42
	strb	r2, [r3, #13]
	b	.L2320
.L2375:
	mov	r6, #7
.L2397:
	mov	r5, #4
.L2320:
	sub	r3, r4, #1
	cmp	r3, #1
	bhi	.L2393
	b	.L2401
.L2331:
	add	r2, fp, r8
	add	r4, fp, #20
	mov	r7, #0
	mov	r5, #55
	ldrb	r2, [r2, #3764]	@ zero_extendqisi2
	add	r4, r4, r2, asl #6
	add	r2, fp, r2, asl #3
	ldrb	sl, [r2, #868]	@ zero_extendqisi2
	ldr	r1, [r2, #864]
	add	sl, sl, #8
	add	sl, r1, sl, asl #8
.L2327:
	add	r2, fp, r7
	str	r5, [sl, #8]
	mov	r0, #80
	ldrb	r2, [r2, #4]	@ zero_extendqisi2
	str	r2, [sl, #4]
	str	r3, [sp, #12]
	bl	NandcDelayns
	ldr	r2, [sl, #0]
	strb	r2, [r4, r7]
	add	r7, r7, #1
	ldr	r3, [sp, #12]
	uxtb	r2, r7
	cmp	r2, r6
	bcc	.L2327
	mov	r2, #0
	b	.L2328
.L2329:
	ldrb	r7, [r0, r1, asl #2]	@ zero_extendqisi2
	ldrb	r5, [r4, r2]	@ zero_extendqisi2
	add	r5, r7, r5
	strb	r5, [ip, r1, asl #3]
	add	r1, r1, #1
	cmp	r1, #7
	bne	.L2329
	add	r2, r2, #1
	cmp	r2, #4
	beq	.L2330
.L2328:
	mov	r1, #1
	add	ip, r4, r2
	add	r0, r2, r9
	b	.L2329
.L2330:
	add	r8, r8, #1
	mov	r2, #0
	strb	r2, [r4, #16]
	strb	r2, [r4, #24]
	uxtb	r8, r8
	strb	r2, [r4, #32]
	strb	r2, [r4, #40]
	strb	r2, [r4, #48]
	strb	r2, [r4, #41]
	strb	r2, [r4, #49]
	b	.L2325
.L2401:
	ldr	r9, .L2403+8
	mov	r3, r6
	mov	r8, #0
	mov	r6, r5
.L2325:
	ldr	fp, .L2403
	ldrb	r2, [fp, #3762]	@ zero_extendqisi2
	cmp	r2, r8
	bhi	.L2331
	mov	r5, r6
	mov	r6, r3
	b	.L2332
.L2393:
	sub	r3, r4, #3
	cmp	r3, #4
	bhi	.L2332
	mul	r3, r6, r5
	mov	sl, #0
	mov	r7, sl
	str	r6, [sp, #48]
	mov	r2, r3, asr #2
	mov	r3, r3, asl #4
	str	r2, [sp, #24]
	str	r3, [sp, #52]
	b	.L2333
.L2374:
	add	r3, r1, sl
	ldrb	r6, [r3, #3764]	@ zero_extendqisi2
	add	r3, r1, r6, asl #3
	mov	r0, r6
	ldrb	fp, [r3, #868]	@ zero_extendqisi2
	ldr	r9, [r3, #864]
	mov	r3, #255
	add	fp, fp, #8
	add	r8, r9, fp, asl #8
	str	r3, [r8, #8]
	str	r1, [sp, #12]
	bl	NandcWaitFlashReady
	ldr	r1, [sp, #12]
	cmp	r4, #7
	addeq	r1, r1, #28
	moveq	r3, #160
	addne	r1, r1, #20
	addne	r1, r1, r6, asl #6
	strne	r1, [sp, #28]
	mlaeq	r3, r3, r6, r1
	streq	r3, [sp, #28]
	cmp	r4, #4
	mov	r3, #54
	str	r3, [r8, #8]
	bne	.L2336
	mov	r3, #255
	str	r3, [r8, #4]
	mov	r3, #64
	str	r3, [r9, fp, asl #8]
	mov	r3, #204
	b	.L2399
.L2336:
	sub	r3, r4, #5
	cmp	r3, #1
	ldrls	r3, .L2403
	ldrlsb	r3, [r3, #4]	@ zero_extendqisi2
	strls	r3, [r8, #4]
	movls	r3, #82
	bls	.L2400
.L2338:
	mov	r3, #174
	str	r3, [r8, #4]
	mov	r3, #176
	str	r7, [r9, fp, asl #8]
.L2399:
	str	r3, [r8, #4]
	mov	r3, #77
.L2400:
	cmp	r4, #6
	str	r3, [r9, fp, asl #8]
	mov	r0, r6
	mov	r3, #22
	str	r3, [r8, #8]
	mov	r3, #23
	str	r3, [r8, #8]
	mov	r3, #4
	str	r3, [r8, #8]
	mov	r3, #25
	str	r3, [r8, #8]
	moveq	r3, #31
	str	r7, [r8, #8]
	str	r7, [r8, #4]
	str	r7, [r8, #4]
	streq	r3, [r8, #4]
	mov	r3, #2
	strne	r7, [r8, #4]
	str	r3, [r8, #4]
	mov	r3, #48
	str	r7, [r8, #4]
	str	r3, [r8, #8]
	bl	NandcWaitFlashReady
	sub	r3, r4, #5
	cmp	r3, #1
	str	r3, [sp, #36]
	movls	r1, #16
	bls	.L2342
	cmp	r4, #7
	moveq	r1, #32
	movne	r1, #2
.L2342:
	ldr	r3, .L2403+12
	ldr	r2, [r3, #144]
	mov	r3, #0
.L2343:
	ldr	r0, [r8, #0]
	strb	r0, [r2, r3]
	add	r3, r3, #1
	uxtb	r0, r3
	cmp	r0, r1
	bcc	.L2343
	cmp	r4, #7
	bne	.L2344
	mov	r3, #0
.L2346:
	ldrb	r1, [r2, #0]	@ zero_extendqisi2
	cmp	r1, #12
	beq	.L2345
	ldrb	r1, [r2, #1]	@ zero_extendqisi2
	cmp	r1, #10
	beq	.L2345
	add	r3, r3, #1
	add	r2, r2, #4
	uxtb	r3, r3
	cmp	r3, #8
	bne	.L2346
	b	.L2347
.L2345:
	cmp	r3, #6
	bls	.L2348
.L2347:
	ldr	r0, .L2403+16
	mov	r1, #0
	bl	printk
.L2349:
	b	.L2349
.L2344:
	cmp	r4, #6
	bne	.L2348
	mov	r1, #8
.L2350:
	ldrb	r3, [r2], #1	@ zero_extendqisi2
	cmp	r3, #12
	beq	.L2348
	ldrb	r3, [r2, #7]	@ zero_extendqisi2
	cmp	r3, #4
	beq	.L2348
	sub	r1, r1, #1
	uxtb	r1, r1
	cmp	r1, #0
	bne	.L2350
	b	.L2402
.L2352:
	b	.L2352
.L2348:
	ldr	r3, .L2403+12
	mov	r2, #0
	ldr	ip, [r3, #144]
	str	ip, [sp, #40]
.L2353:
	ldr	r1, [r8, #0]
	ldr	lr, [sp, #40]
	ldr	ip, [sp, #52]
	strb	r1, [lr, r2]
	add	r2, r2, #1
	cmp	r2, ip
	blt	.L2353
	ldr	r2, [sp, #24]
	ldr	r1, [r3, #144]
	mov	r3, #8
	mov	ip, r2, asl #2
	mov	lr, r2, asl #3
	add	r1, r1, ip
	str	ip, [sp, #32]
.L2355:
	mov	r2, #0
	mov	r0, r2
.L2354:
	ldr	ip, [r1, r2]
	add	r0, r0, #1
	mvn	ip, ip
	str	ip, [r1, r2]
	ldr	ip, [sp, #24]
	add	r2, r2, #4
	cmp	r0, ip
	blt	.L2354
	subs	r3, r3, #1
	add	r1, r1, lr
	bne	.L2355
	str	r3, [sp, #16]
	str	sl, [sp, #60]
	b	.L2356
.L2360:
	mov	lr, #1
	mov	r0, #0
	mov	ip, lr, asl r2
	mov	lr, r0
	mov	sl, #16
	str	r3, [sp, #4]
	str	sl, [sp, #20]
.L2358:
	ldr	r3, [sp, #56]
	ldr	r3, [r3, r0]
	and	sl, ip, r3
	ldr	r3, [sp, #20]
	cmp	sl, ip
	ldr	sl, [sp, #32]
	addeq	lr, lr, #1
	subs	r3, r3, #1
	add	r0, r0, sl
	str	r3, [sp, #20]
	bne	.L2358
	cmp	lr, #8
	add	r2, r2, #1
	orrhi	r1, r1, ip
	cmp	r2, #32
	ldr	r3, [sp, #4]
	bne	.L2360
	ldr	ip, [sp, #16]
	ldr	r2, [sp, #24]
	add	ip, ip, #1
	ldr	sl, [sp, #44]
	cmp	ip, r2
	str	ip, [sp, #16]
	str	r1, [sl, r3]
	add	r3, r3, #4
	bge	.L2361
.L2356:
	ldr	sl, .L2403+12
	mov	r1, #0
	mov	r2, r1
	ldr	sl, [sl, #144]
	add	ip, sl, r3
	str	ip, [sp, #56]
	str	sl, [sp, #44]
	b	.L2360
.L2361:
	ldr	r3, .L2403+12
	ldr	sl, [sp, #60]
	ldr	r1, [r3, #144]
	mov	r3, #0
	mov	r2, r3
.L2363:
	ldr	r0, [r1, r3]
	add	r3, r3, #4
	cmp	r0, #0
	addeq	r2, r2, #1
	cmp	r3, #32
	bne	.L2363
	cmp	r2, #7
	ble	.L2364
	ldr	r0, .L2403+20
	mov	r2, #1
	mov	r3, #1024
	bl	rknand_print_hex
	ldr	r0, .L2403+16
	mov	r1, #0
	bl	printk
.L2365:
	b	.L2365
.L2364:
	cmp	r4, #6
	moveq	r0, #4
	streq	r0, [sp, #16]
	beq	.L2366
	cmp	r4, #7
	moveq	lr, #10
	movne	lr, #8
	str	lr, [sp, #16]
.L2366:
	cmp	r5, #0
	ldr	r0, [sp, #40]
	subne	r3, r5, #1
	mov	r2, #0
	moveq	r1, #1
	streq	r1, [sp, #20]
	uxtbne	r3, r3
	ldr	r1, [sp, #28]
	addne	r3, r3, #1
	strne	r3, [sp, #20]
	b	.L2369
.L2370:
	ldrb	lr, [ip], #1	@ zero_extendqisi2
	strb	lr, [r1, r3]
	add	r3, r3, #1
	uxtb	lr, r3
	cmp	lr, r5
	bcc	.L2370
	ldr	r3, [sp, #20]
	add	r2, r2, #1
	ldr	ip, [sp, #16]
	add	r0, r0, r3
	ldr	r3, [sp, #48]
	add	r1, r1, ip
	cmp	r2, r3
	bge	.L2371
.L2369:
	mov	ip, r0
	mov	r3, #0
	b	.L2370
.L2371:
	mov	r3, #255
	mov	r0, r6
	str	r3, [r8, #8]
	bl	NandcWaitFlashReady
	ldr	ip, [sp, #36]
	cmp	ip, #1
	movhi	r3, #56
	strhi	r3, [r8, #8]
	bhi	.L2373
	mov	r3, #54
	str	r3, [r8, #8]
	ldr	r3, .L2403
	mov	r0, sl
	mvn	r1, #0
	ldrb	r3, [r3, #4]	@ zero_extendqisi2
	str	r3, [r8, #4]
	mov	r3, #22
	str	r7, [r9, fp, asl #8]
	str	r3, [r8, #8]
	bl	FlashReadCmd
.L2373:
	mov	r0, r6
	add	sl, sl, #1
	bl	NandcWaitFlashReady
	uxtb	sl, sl
.L2333:
	ldr	r1, .L2403
	ldrb	r3, [r1, #3762]	@ zero_extendqisi2
	cmp	r3, sl
	bhi	.L2374
	ldr	r6, [sp, #48]
.L2332:
	ldr	r3, .L2403
	strb	r5, [r3, #1]
	strb	r6, [r3, #2]
	add	sp, sp, #68
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, sl, fp, pc}
.L2402:
	ldr	r0, .L2403+16
	bl	printk
	b	.L2352
.L2404:
	.align	2
.L2403:
	.word	.LANCHOR0
	.word	.LANCHOR1
	.word	.LANCHOR1+2944
	.word	.LANCHOR2
	.word	.LC138
	.word	.LC139
	.fnend
	.size	HynixGetReadRetryDefault, .-HynixGetReadRetryDefault
	.align	2
	.global	FlashGetReadRetryDefault
	.type	FlashGetReadRetryDefault, %function
FlashGetReadRetryDefault:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	stmfd	sp!, {r3, lr}
	.save {r3, lr}
	subs	r3, r0, #0
	ldmeqfd	sp!, {r3, pc}
	sub	r2, r3, #1
	cmp	r2, #6
	bhi	.L2407
	ldmfd	sp!, {r3, lr}
	b	HynixGetReadRetryDefault
.L2407:
	cmp	r3, #49
	bne	.L2408
	ldr	r0, .L2409
	mov	r2, #64
	ldr	r1, .L2409+4
	strb	r3, [r0, #0]
	mov	r3, #4
	strb	r3, [r0, #1]
	mov	r3, #15
	strb	r3, [r0, #2]
	add	r0, r0, #4
	bl	memcpy
	ldmfd	sp!, {r3, pc}
.L2408:
	ldmfd	sp!, {r3, lr}
	b	FlashGetReadRetryDefault.part.27
.L2410:
	.align	2
.L2409:
	.word	.LANCHOR0
	.word	.LANCHOR1+2860
	.fnend
	.size	FlashGetReadRetryDefault, .-FlashGetReadRetryDefault
	.align	2
	.global	FlashInit
	.type	FlashInit, %function
FlashInit:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	stmfd	sp!, {r4, r5, r6, r7, r8, lr}
	.save {r4, r5, r6, r7, r8, lr}
	mov	r7, r0
	mov	r0, #32768
	ldr	r5, .L2459
	bl	ftl_malloc
	ldr	r6, .L2459+4
	mov	r4, #0
	str	r0, [r5, #144]
	mov	r0, #32768
	bl	ftl_malloc
	str	r0, [r5, #132]
	mov	r0, #4096
	bl	ftl_malloc
	str	r0, [r5, #224]
	mov	r0, #32768
	bl	ftl_malloc
	str	r0, [r5, #216]
	mov	r0, #4096
	bl	ftl_malloc
	mov	r3, #50
	str	r4, [r5, #152]
	strb	r3, [r5, #160]
	strb	r3, [r6, #853]
	mov	r3, #128
	str	r4, [r5, #164]
	str	r3, [r6, #856]
	mov	r3, #60
	strb	r4, [r6, #3773]
	strb	r3, [r5, #148]
	strb	r4, [r6, #852]
	str	r0, [r5, #220]
	mov	r0, r7
	bl	NandcInit
	ldr	r5, .L2459+8
	mov	r7, #44
.L2415:
	uxtb	r0, r4
	mov	r1, r5
	bl	FlashReadIDRaw
	cmp	r4, #0
	bne	.L2412
	ldrb	r3, [r6, #2980]	@ zero_extendqisi2
	sub	r3, r3, #1
	uxtb	r3, r3
	cmp	r3, #253
	bhi	.L2451
	ldrb	r3, [r6, #2981]	@ zero_extendqisi2
	cmp	r3, #255
	beq	.L2452
.L2412:
	ldrb	r3, [r5, #0]	@ zero_extendqisi2
	add	r4, r4, #1
	cmp	r3, #181
	streqb	r7, [r5, #0]
	cmp	r4, #4
	add	r5, r5, #8
	bne	.L2415
	ldr	r4, .L2459+4
	ldrb	r3, [r4, #2980]	@ zero_extendqisi2
	cmp	r3, #173
	beq	.L2416
	ldr	r0, [r4, #3776]
	bl	NandcSetDdrMode
.L2416:
	mov	r1, #0
	ldr	r0, .L2459+4
	mov	r2, #852
	bl	ftl_memset
	ldr	r3, .L2459+12
	str	r3, [r4, #3624]
	mov	r3, #0
	strb	r3, [r4, #860]
	ldrb	r3, [r4, #2981]	@ zero_extendqisi2
	sub	r1, r3, #218
	rsbs	lr, r1, #0
	adc	lr, lr, r1
	cmp	r3, #218
	cmpne	r3, #241
	beq	.L2417
	cmp	r3, #220
	bne	.L2418
.L2417:
	ldr	r2, .L2459+4
	mov	r1, #1
	ldr	r0, .L2459
	ldrb	ip, [r2, #2980]	@ zero_extendqisi2
	strb	r1, [r2, #852]
	mov	r1, #16
	strb	r1, [r2, #853]
	cmp	ip, #152
	strb	r1, [r0, #148]
	ldr	r1, .L2459+16
	strb	ip, [r1, #2973]
	bne	.L2419
	ldrb	r2, [r2, #2984]	@ zero_extendqisi2
	sxtb	r2, r2
	cmp	r2, #0
	movge	r2, #24
	strgeb	r2, [r0, #148]
.L2419:
	cmp	lr, #0
	beq	.L2420
	movw	r3, #2986
	mov	r2, #2048
	strh	r2, [r1, r3]	@ movhi
	mvn	r3, #37
	strb	r3, [r1, #2974]
	b	.L2421
.L2420:
	cmp	r3, #220
	bne	.L2421
	ldr	r3, .L2459+16
	movw	r2, #2986
	mov	r1, #4096
	strh	r1, [r3, r2]	@ movhi
	mvn	r2, #35
	strb	r2, [r3, #2974]
.L2421:
	ldr	r1, .L2459+20
	mov	r2, #32
	ldr	r0, .L2459+24
	bl	memcpy
	ldr	r0, .L2459+12
	mov	r2, #32
	add	r1, r0, #144
	bl	memcpy
.L2418:
	ldr	r4, .L2459+4
	ldrb	r3, [r4, #852]	@ zero_extendqisi2
	cmp	r3, #0
	bne	.L2422
	bl	FlashLoadPhyInfoInRam
	cmp	r0, #0
	bne	.L2423
	ldr	r3, [r4, #3624]
	ldrh	r0, [r3, #16]
	ubfx	r0, r0, #8, #3
	strb	r0, [r4, #3772]
	tst	r0, #1
	bne	.L2423
	mov	r3, #1
	strb	r3, [r4, #3773]
	bl	FlashSetInterfaceMode
	ldrb	r0, [r4, #3772]	@ zero_extendqisi2
	bl	NandcSetMode
.L2423:
	ldr	r4, .L2459+4
	ldr	r3, [r4, #3624]
	ldrb	r3, [r3, #26]	@ zero_extendqisi2
	strb	r3, [r4, #928]
	bl	FlashLoadPhyInfo
	cmp	r0, #0
	beq	.L2422
	ldr	r3, [r4, #3776]
	cmp	r3, #0
	beq	.L2425
	mov	r0, #1
	bl	FlashSetInterfaceMode
	mov	r0, #1
	b	.L2457
.L2425:
	ldrb	r0, [r4, #3772]	@ zero_extendqisi2
	bl	FlashSetInterfaceMode
	ldrb	r0, [r4, #3772]	@ zero_extendqisi2
.L2457:
	bl	NandcSetMode
	bl	FlashLoadPhyInfo
	cmp	r0, #0
	beq	.L2422
	ldr	r4, .L2459+4
	mov	r0, #1
	bl	FlashSetInterfaceMode
	mov	r0, #1
	bl	NandcSetMode
	ldr	r0, .L2459+28
	ldr	r3, [r4, #3624]
	ldrh	r1, [r3, #14]
	bl	printk
	bl	FlashLoadPhyInfoInRam
	cmn	r0, #1
	ldmeqfd	sp!, {r4, r5, r6, r7, r8, pc}
	bl	FlashDieInfoInit
	ldr	r3, [r4, #3624]
	ldrb	r0, [r3, #19]	@ zero_extendqisi2
	bl	FlashGetReadRetryDefault
	ldr	r1, .L2459+32
	ldr	r0, .L2459
	ldr	r3, [r4, #3624]
	ldrh	r1, [r0, r1]
	ldrb	r2, [r3, #9]	@ zero_extendqisi2
	add	r1, r1, #4080
	add	r1, r1, #15
	cmp	r2, r1, lsr #12
	blt	.L2428
	ldrh	r1, [r3, #14]
	add	r1, r1, #255
	cmp	r2, r1, lsr #8
	bge	.L2429
.L2428:
	ldrh	r2, [r3, #14]
	bic	r2, r2, #255
	strh	r2, [r3, #14]	@ movhi
.L2429:
	ldr	r3, .L2459+4
	ldrb	r3, [r3, #3772]	@ zero_extendqisi2
	tst	r3, #6
	beq	.L2430
	bl	FlashSavePhyInfo
	ldr	r3, .L2459
	mov	r0, #0
	ldr	r1, [r3, #156]
	bl	FlashDdrParaScan
.L2430:
	bl	FlashSavePhyInfo
.L2422:
	ldr	r4, .L2459+4
	ldr	r5, .L2459
	ldr	r3, [r4, #3624]
	ldrb	r2, [r3, #26]	@ zero_extendqisi2
	ldrh	r0, [r3, #10]
	ldrb	r6, [r3, #18]	@ zero_extendqisi2
	strb	r2, [r4, #928]
	ldrh	r2, [r3, #16]
	ubfx	r1, r2, #7, #1
	strb	r1, [r4, #860]
	ubfx	r1, r2, #3, #1
	strb	r1, [r5, #172]
	ubfx	r1, r2, #4, #1
	ubfx	r2, r2, #8, #3
	strb	r1, [r4, #3833]
	strb	r2, [r4, #3772]
	mov	r2, #0
	ldrb	r1, [r3, #12]	@ zero_extendqisi2
	str	r2, [r5, #168]
	bl	__aeabi_idiv
	mov	r1, r0
	mov	r0, r6
	bl	BuildFlashLsbPageTable
	bl	FlashDieInfoInit
	ldr	r3, [r4, #3624]
	ldrh	r2, [r3, #16]
	tst	r2, #64
	beq	.L2431
	ldrb	r0, [r3, #19]	@ zero_extendqisi2
	ldrb	r3, [r4, #1]	@ zero_extendqisi2
	strb	r0, [r4, #3761]
	strb	r3, [r4, #3760]
	ldrb	r3, [r4, #2]	@ zero_extendqisi2
	strb	r3, [r5, #136]
	sub	r3, r0, #1
	uxtb	r3, r3
	cmp	r3, #6
	bhi	.L2432
	ldr	r3, .L2459+36
	str	r3, [r5, #168]
	sub	r3, r0, #5
	uxtb	r3, r3
	cmp	r3, #1
	movls	r3, #1
	strls	r3, [r4, #3824]
	bls	.L2434
	cmp	r0, #7
	addeq	r4, r4, #28
	beq	.L2435
.L2434:
	ldr	r4, .L2459+40
.L2435:
	mov	r3, #0
	mov	r2, r3
.L2437:
	ldrsb	r1, [r4, r2]
	add	r2, r2, #1
	cmp	r1, #0
	addeq	r3, r3, #1
	cmp	r2, #32
	bne	.L2437
	cmp	r3, #27
	bls	.L2431
	bl	FlashGetReadRetryDefault
	bl	FlashSavePhyInfo
	b	.L2431
.L2432:
	sub	r3, r0, #17
	uxtb	r3, r3
	cmp	r3, #2
	bhi	.L2438
	ldr	r3, .L2459+44
	cmp	r0, #19
	str	r3, [r5, #168]
	mov	r3, #7
	strb	r3, [r5, #136]
	moveq	r3, #15
	streqb	r3, [r5, #136]
	b	.L2431
.L2438:
	cmp	r0, #33
	cmpne	r0, #65
	beq	.L2439
	cmp	r0, #66
	bne	.L2440
.L2439:
	ldr	r3, .L2459
	mov	r1, #4
	ldr	r2, .L2459+48
	str	r2, [r3, #168]
	ldr	r2, .L2459+4
	strb	r1, [r2, #3760]
	mov	r2, #7
	strb	r2, [r3, #136]
	b	.L2431
.L2440:
	cmp	r0, #67
	cmpne	r0, #34
	beq	.L2441
	cmp	r0, #35
	beq	.L2441
	cmp	r0, #68
	bne	.L2442
.L2441:
	ldr	r3, .L2459
	cmp	r0, #35
	cmpne	r0, #68
	ldr	r2, .L2459+48
	sub	r0, r0, #67
	uxtb	r0, r0
	str	r2, [r3, #168]
	mov	r2, #7
	strb	r2, [r3, #136]
	moveq	r2, #17
	streqb	r2, [r3, #136]
	cmp	r0, #1
	ldr	r3, .L2459+4
	movls	r2, #4
	movhi	r2, #5
	strb	r2, [r3, #3760]
	b	.L2431
.L2442:
	cmp	r0, #49
	ldreq	r3, .L2459+52
	streq	r3, [r5, #168]
.L2431:
	ldr	r3, .L2459+4
	ldr	r2, .L2459+56
	ldr	r1, [r3, #3784]
	cmp	r1, r2
	bne	.L2445
	ldrb	r2, [r3, #928]	@ zero_extendqisi2
	cmp	r2, #0
	ldrne	r2, [r3, #3624]
	movne	r1, #0
	strneb	r1, [r2, #18]
.L2445:
	ldrb	r3, [r3, #2980]	@ zero_extendqisi2
	ldr	r2, .L2459+4
	cmp	r3, #44
	bne	.L2446
	ldrb	r3, [r2, #3773]	@ zero_extendqisi2
	cmp	r3, #0
	beq	.L2446
	mov	r3, #0
	mov	r0, #1
	strb	r3, [r2, #3773]
	bl	FlashSetInterfaceMode
	mov	r0, #1
	bl	NandcSetMode
.L2446:
	mov	r0, #0
	bl	flash_enter_slc_mode
	ldr	r2, .L2459+4
	ldrb	r3, [r2, #3772]	@ zero_extendqisi2
	tst	r3, #6
	beq	.L2447
	ldrb	r2, [r2, #3773]	@ zero_extendqisi2
	cmp	r2, #0
	bne	.L2448
	tst	r3, #1
	bne	.L2447
.L2448:
	ldr	r3, .L2459
	mov	r0, #0
	ldr	r1, [r3, #156]
	bl	FlashDdrParaScan
.L2447:
	ldr	r4, .L2459+4
	mov	r0, #0
	bl	flash_exit_slc_mode
	mov	r8, #16
	movw	r6, #3062
	ldr	r3, [r4, #3624]
	ldrb	r0, [r3, #20]	@ zero_extendqisi2
	bl	FlashBchSel
	add	r0, r4, #932
	bl	FlashReadIdbDataRaw
	ldr	r0, .L2459+60
	strb	r8, [r4, #853]
	bl	FlashTimingCfg
	ldr	r5, [r4, #3624]
	mov	r3, #3056
	ldrb	r2, [r5, #12]	@ zero_extendqisi2
	ldrh	r7, [r5, #14]
	strh	r2, [r4, r3]	@ movhi
	ldrb	r3, [r5, #7]	@ zero_extendqisi2
	ldrb	r2, [r4, #3762]	@ zero_extendqisi2
	str	r3, [r4, #3052]
	ldr	r3, [r4, #2980]
	str	r3, [r4, #3048]
	movw	r3, #3058
	strh	r2, [r4, r3]	@ movhi
	add	r3, r3, #2
	ldrb	r2, [r5, #13]	@ zero_extendqisi2
	strh	r7, [r4, r6]	@ movhi
	strh	r2, [r4, r3]	@ movhi
	add	r3, r3, #4
	ldrh	r2, [r5, #10]
	strh	r2, [r4, r3]	@ movhi
	ldrb	r1, [r5, #12]	@ zero_extendqisi2
	ldrh	r0, [r5, #10]
	bl	__aeabi_idiv
	movw	r3, #3066
	mov	lr, #512
	movw	r2, #3070
	strh	r0, [r4, r3]	@ movhi
	movw	r0, #3068
	ldrb	ip, [r5, #9]	@ zero_extendqisi2
	strh	ip, [r4, r0]	@ movhi
	ldrb	r3, [r5, #9]	@ zero_extendqisi2
	ldrh	r1, [r5, #10]
	mul	r1, r1, r3
	mov	r3, #3072
	strh	lr, [r4, r3]	@ movhi
	add	r3, r3, #2
	ldrb	lr, [r4, #853]	@ zero_extendqisi2
	strh	lr, [r4, r3]	@ movhi
	uxth	r1, r1
	ldrb	lr, [r4, #852]	@ zero_extendqisi2
	strh	r1, [r4, r2]	@ movhi
	cmp	lr, #1
	bne	.L2450
	mov	r7, r7, lsr #1
	mov	ip, ip, asl #1
	mov	r1, r1, asl #1
	strb	r8, [r4, #853]
	strh	r1, [r4, r2]	@ movhi
	mov	r2, #8
	strh	r7, [r4, r6]	@ movhi
	strh	ip, [r4, r0]	@ movhi
	strh	r2, [r4, r3]	@ movhi
.L2450:
	ldrb	r0, [r5, #20]	@ zero_extendqisi2
	bl	FlashBchSel
	bl	FlashSuspend
	mov	r0, #0
	ldmfd	sp!, {r4, r5, r6, r7, r8, pc}
.L2451:
	mvn	r0, #1
	ldmfd	sp!, {r4, r5, r6, r7, r8, pc}
.L2452:
	mvn	r0, #1
	ldmfd	sp!, {r4, r5, r6, r7, r8, pc}
.L2460:
	.align	2
.L2459:
	.word	.LANCHOR2
	.word	.LANCHOR0
	.word	.LANCHOR0+2980
	.word	.LANCHOR1+2828
	.word	.LANCHOR1
	.word	.LANCHOR1+2732
	.word	.LANCHOR0+896
	.word	.LC140
	.word	-898
	.word	HynixReadRetrial
	.word	.LANCHOR0+20
	.word	MicronReadRetrial
	.word	ToshibaReadRetrial
	.word	SamsungReadRetrial
	.word	1446522928
	.word	150000
	.fnend
	.size	FlashInit, .-FlashInit
	.align	2
	.global	rk_ftl_init
	.type	rk_ftl_init, %function
rk_ftl_init:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	stmfd	sp!, {r3, r4, r5, lr}
	.save {r3, r4, r5, lr}
	mov	r0, #2048
	bl	ftl_malloc
	ldr	r4, .L2465
	mov	r5, #0
	add	r1, r4, #460
	str	r5, [r4, #128]
	str	r5, [r4, #460]
	str	r0, [r4, #456]
	add	r0, r4, #128
	bl	rknand_get_reg_addr
	ldr	r3, [r4, #128]
	cmp	r3, r5
	mvneq	r4, #0
	beq	.L2462
	bl	rk_nandc_irq_init
	ldr	r0, [r4, #456]
	mov	r1, r5
	mov	r2, r5
	mov	r3, #2048
	bl	FlashSramLoadStore
	bl	rknand_flash_cs_init
	ldr	r0, [r4, #128]
	bl	FlashInit
	subs	r4, r0, #0
	bne	.L2463
	ldr	r0, .L2465+4
	bl	FtlInit
.L2463:
	ldr	r0, .L2465+8
	mov	r1, r4
	bl	printk
.L2462:
	mov	r0, r4
	ldmfd	sp!, {r3, r4, r5, pc}
.L2466:
	.align	2
.L2465:
	.word	.LANCHOR2
	.word	.LANCHOR0+3048
	.word	.LC141
	.fnend
	.size	rk_ftl_init, .-rk_ftl_init
	.align	2
	.global	FtlReInitForSDUpdata
	.type	FtlReInitForSDUpdata, %function
FtlReInitForSDUpdata:
	.fnstart
	@ args = 0, pretend = 0, frame = 16
	@ frame_needed = 0, uses_anonymous_args = 0
	ldr	r3, .L2492
	stmfd	sp!, {r4, r5, lr}
	.save {r4, r5, lr}
	.pad #20
	sub	sp, sp, #20
	ldrb	r4, [r3, #928]	@ zero_extendqisi2
	cmp	r4, #0
	movne	r0, #0
	bne	.L2468
	ldr	r5, .L2492+4
	ldr	r0, [r5, #128]
	bl	FlashInit
	cmp	r0, #0
	movne	r0, r4
	bne	.L2468
	bl	FlashLoadFactorBbt
	cmp	r0, #0
	beq	.L2469
	bl	FlashMakeFactorBbt
.L2469:
	ldr	r0, [r5, #132]
	bl	FlashReadIdbDataRaw
	cmp	r0, #0
	beq	.L2470
	mov	r1, #0
	mov	r2, #16
	mov	r0, sp
	bl	FlashReadFacBbtData
	ldr	r1, [sp, #0]
	mov	r3, #0
	mov	r0, #1
	mov	r2, r3
.L2472:
	ands	ip, r1, r0, asl r2
	add	r2, r2, #1
	addne	r3, r3, #1
	cmp	r2, #16
	bne	.L2472
	cmp	r3, #6
	ldrls	r3, .L2492
	bls	.L2488
	mov	r2, #0
	mov	r0, #1
.L2473:
	ands	ip, r1, r0, asl r2
	add	r2, r2, #1
	addne	r3, r3, #1
	cmp	r2, #24
	bne	.L2473
	cmp	r3, #17
	ldr	r3, .L2492
	movhi	r2, #36
.L2488:
	strb	r2, [r3, #853]
	movw	r2, #3074
	ldr	r3, .L2492
	ldrb	r1, [r3, #853]	@ zero_extendqisi2
	strh	r1, [r3, r2]	@ movhi
.L2470:
	ldr	r1, .L2492+8
	mov	r4, #1
	ldr	r0, .L2492+12
	bl	printk
	ldr	r0, .L2492+16
	bl	FtlConstantsInit
	bl	FtlVariablesInit
	ldr	r3, .L2492
	ldr	r0, [r3, #3844]
	uxth	r0, r0
	bl	FtlFreeSysBlkQueueInit
	b	.L2477
.L2479:
	add	r4, r4, #1
.L2477:
	bl	FtlLoadBbt
	cmp	r0, #0
	bne	.L2490
.L2478:
	bl	FtlSysBlkInit
	cmp	r0, #0
	beq	.L2480
.L2490:
	bl	FtlLowFormat
	cmp	r4, #3
	bls	.L2479
	b	.L2491
.L2480:
	ldr	r3, .L2492+20
	mov	r2, #1
	str	r2, [r3, #2932]
	b	.L2468
.L2491:
	mvn	r0, #0
.L2468:
	add	sp, sp, #20
	ldmfd	sp!, {r4, r5, pc}
.L2493:
	.align	2
.L2492:
	.word	.LANCHOR0
	.word	.LANCHOR2
	.word	.LC76
	.word	.LC75
	.word	.LANCHOR0+3048
	.word	.LANCHOR1
	.fnend
	.size	FtlReInitForSDUpdata, .-FtlReInitForSDUpdata
	.align	2
	.global	flash_boot_enter_slc_mode
	.type	flash_boot_enter_slc_mode, %function
flash_boot_enter_slc_mode:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	@ link register save eliminated.
	ldr	r3, .L2496
	ldr	r2, [r3, #3784]
	ldr	r3, .L2496+4
	cmp	r2, r3
	bxne	lr
	b	flash_enter_slc_mode
.L2497:
	.align	2
.L2496:
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
	ldr	r3, .L2500
	ldr	r2, [r3, #3784]
	ldr	r3, .L2500+4
	cmp	r2, r3
	bxne	lr
	b	flash_exit_slc_mode
.L2501:
	.align	2
.L2500:
	.word	.LANCHOR0
	.word	1446522928
	.fnend
	.size	flash_boot_exit_slc_mode, .-flash_boot_exit_slc_mode
	.align	2
	.global	IdBlockReadData
	.type	IdBlockReadData, %function
IdBlockReadData:
	.fnstart
	@ args = 0, pretend = 0, frame = 24
	@ frame_needed = 0, uses_anonymous_args = 0
	stmfd	sp!, {r4, r5, r6, r7, r8, r9, sl, fp, lr}
	.save {r4, r5, r6, r7, r8, r9, sl, fp, lr}
	.pad #28
	sub	sp, sp, #28
	ldr	r4, .L2507
	mov	r6, r0
	str	r2, [sp, #20]
	mov	sl, r1
	ldr	r0, .L2507+4
	mov	r1, r6
	ldr	r2, [r4, #3624]
	ldr	r3, [r4, #856]
	ldr	r8, .L2507+8
	ldrb	r5, [r2, #9]	@ zero_extendqisi2
	mov	r2, sl
	mul	r5, r5, r3
	bl	printk
	ldr	r3, [r4, #3624]
	mov	r0, r6
	ldrb	r9, [r3, #9]	@ zero_extendqisi2
	uxth	r5, r5
	mov	r1, r5
	mov	r5, #0
	bl	__aeabi_uidivmod
	mul	r2, r9, r1
	rsb	ip, r1, r6
	mov	r7, r1
	str	ip, [sp, #12]
	ubfx	r2, r2, #2, #2
	b	.L2503
.L2505:
	add	r1, r5, r7
	rsb	r3, r2, #4
	ubfx	r1, r1, #2, #16
	ldrb	r0, [r4, #928]	@ zero_extendqisi2
	uxth	r3, r3
	str	r3, [sp, #16]
	add	r3, r4, r1, asl #1
	cmp	r0, #0
	add	r3, r3, #3072
	add	r3, r3, #4
	ldrh	r3, [r3, #0]
	beq	.L2504
	ldr	r0, [r4, #3784]
	ldr	ip, .L2507+12
	cmp	r0, ip
	moveq	r3, r1
.L2504:
	ldr	ip, [sp, #12]
	ldrb	r0, [r8, #148]	@ zero_extendqisi2
	add	r2, r2, ip
	ldrb	ip, [r4, #3832]	@ zero_extendqisi2
	ldr	fp, [r8, #132]
	mla	r2, r9, r3, r2
	ldr	r3, [r4, #3624]
	ldrb	r1, [r3, #9]	@ zero_extendqisi2
	str	ip, [sp, #0]
	stmib	sp, {r1, r2}
	bl	FlashBchSel
	mov	r0, #0
	bl	flash_boot_enter_slc_mode
	ldr	r2, [sp, #8]
	ldr	r1, [sp, #4]
	mov	r0, r2
	bl	__aeabi_uidiv
	mov	r2, fp
	mov	r1, r0
	mov	r0, #0
	mov	r3, r0
	bl	FlashReadPage
	mov	r0, #0
	bl	flash_boot_exit_slc_mode
	ldr	ip, [sp, #0]
	mov	r0, ip
	bl	FlashBchSel
	ldr	ip, [sp, #20]
	mov	r2, #2048
	ldr	r1, [r8, #132]
	add	r0, ip, r5, asl #9
	bl	memcpy
	ldr	ip, [sp, #16]
	mov	r2, #0
	add	r5, ip, r5
	uxth	r5, r5
.L2503:
	cmp	r5, sl
	bcc	.L2505
	mov	r1, r6
	mov	r2, sl
	mov	r3, #0
	ldr	r0, .L2507+16
	bl	printk
	mov	r0, #0
	add	sp, sp, #28
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, sl, fp, pc}
.L2508:
	.align	2
.L2507:
	.word	.LANCHOR0
	.word	.LC142
	.word	.LANCHOR2
	.word	1446522928
	.word	.LC143
	.fnend
	.size	IdBlockReadData, .-IdBlockReadData
	.align	2
	.global	IDBlockWriteData
	.type	IDBlockWriteData, %function
IDBlockWriteData:
	.fnstart
	@ args = 0, pretend = 0, frame = 88
	@ frame_needed = 0, uses_anonymous_args = 0
	stmfd	sp!, {r4, r5, r6, r7, r8, r9, sl, fp, lr}
	.save {r4, r5, r6, r7, r8, r9, sl, fp, lr}
	.pad #92
	sub	sp, sp, #92
	ldr	r4, .L2516
	mov	r5, r0
	str	r2, [sp, #20]
	mov	r8, r1
	ldr	r0, .L2516+4
	mov	r1, r5
	ldr	r2, [r4, #3624]
	mov	r7, r5
	ldr	r3, [r4, #856]
	ldr	fp, .L2516+8
	ldrb	r6, [r2, #9]	@ zero_extendqisi2
	mov	r2, r8
	mul	r6, r6, r3
	bl	printk
	mov	r0, #0
	bl	flash_boot_enter_slc_mode
	movw	r3, #3068
	ldrh	r1, [r4, r3]
	mov	r0, r5
	bl	__aeabi_uidiv
	uxth	r6, r6
	mov	r1, r0
	mov	r0, #0
	mov	r2, r0
	bl	FlashEraseBlock
	mov	r0, #0
	bl	flash_boot_exit_slc_mode
	ldr	r3, [r4, #3624]
	mov	r1, r6
	mov	r0, r5
	mov	r6, #0
	ldrb	sl, [r3, #9]	@ zero_extendqisi2
	bl	__aeabi_uidivmod
	mov	r9, r1
	rsb	r1, r1, r5
	str	r1, [sp, #16]
	b	.L2510
.L2514:
	add	r3, r6, r9
	ubfx	r3, r3, #2, #16
	cmp	r3, #0
	beq	.L2511
	add	r1, r3, #1
	ldrb	r0, [r4, #928]	@ zero_extendqisi2
	add	r2, r4, r1, asl #1
	cmp	r0, #0
	add	r2, r2, #3072
	add	r2, r2, #4
	ldrh	r2, [r2, #0]
	beq	.L2512
	ldr	r0, [r4, #3784]
	cmp	r0, fp
	uxtheq	r2, r1
.L2512:
	sub	r2, r2, #1
	mov	r2, r2, asl #2
	str	r2, [sp, #24]
	mov	r2, #0
	str	r2, [sp, #28]
.L2511:
	add	r2, r4, r3, asl #1
	add	r2, r2, #3072
	ldrh	r5, [r2, #4]
	ldrb	r2, [r4, #928]	@ zero_extendqisi2
	cmp	r2, #0
	beq	.L2513
	ldr	r2, [r4, #3784]
	cmp	r2, fp
	moveq	r5, r3
.L2513:
	ldr	r2, [sp, #16]
	ldr	r1, [sp, #20]
	ldrb	ip, [r4, #3832]	@ zero_extendqisi2
	mla	r3, sl, r5, r2
	add	r2, r1, r6, asl #9
	ldr	r1, .L2516+12
	add	r6, r6, #4
	uxth	r6, r6
	ldrb	r0, [r1, #148]	@ zero_extendqisi2
	str	ip, [sp, #4]
	str	r2, [sp, #8]
	str	r3, [sp, #12]
	bl	FlashBchSel
	mov	r0, #0
	bl	flash_boot_enter_slc_mode
	ldr	r3, [sp, #12]
	ldr	r1, [r4, #3624]
	mov	r0, r3
	ldrb	r1, [r1, #9]	@ zero_extendqisi2
	bl	__aeabi_uidiv
	ldr	r2, [sp, #8]
	add	r3, sp, #24
	mov	r1, r0
	mov	r0, #0
	bl	FlashProgPage
	mov	r0, #0
	bl	flash_boot_exit_slc_mode
	ldr	ip, [sp, #4]
	mov	r0, ip
	bl	FlashBchSel
	ldr	r0, [sp, #16]
	mov	r1, sl
	bl	__aeabi_uidiv
	add	r2, r5, #1
	uxth	r2, r2
	mov	r1, r0
	mov	r0, #0
	bl	FlashPageProgMsbFFData
.L2510:
	cmp	r6, r8
	bcc	.L2514
	mov	r1, r7
	mov	r2, r8
	mov	r3, #0
	ldr	r0, .L2516+16
	bl	printk
	mov	r5, r7
	mov	r0, #0
	add	sp, sp, #92
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, sl, fp, pc}
.L2517:
	.align	2
.L2516:
	.word	.LANCHOR0
	.word	.LC144
	.word	1446522928
	.word	.LANCHOR2
	.word	.LC145
	.fnend
	.size	IDBlockWriteData, .-IDBlockWriteData
	.align	2
	.global	write_idblock
	.type	write_idblock, %function
write_idblock:
	.fnstart
	@ args = 0, pretend = 0, frame = 8
	@ frame_needed = 0, uses_anonymous_args = 0
	ldr	r3, .L2531
	stmfd	sp!, {r4, r5, r6, r7, r8, r9, sl, fp, lr}
	.save {r4, r5, r6, r7, r8, r9, sl, fp, lr}
	mov	r7, r2
	ldr	r2, [r3, #856]
	.pad #20
	sub	sp, sp, #20
	ldr	r3, [r3, #3624]
	mov	r6, r0
	mov	r0, #256000
	mov	r4, r1
	ldrb	r3, [r3, #9]	@ zero_extendqisi2
	mul	r3, r3, r2
	uxth	r3, r3
	str	r3, [sp, #8]
	bl	ftl_malloc
	subs	r5, r0, #0
	mvneq	r0, #0
	beq	.L2519
	add	sl, r6, #508
	add	sl, sl, #3
	mov	sl, sl, lsr #9
	cmp	sl, #255
	bhi	.L2520
	add	r0, r4, sl, asl #9
	mov	r1, r4
	rsb	r2, sl, #256
	bl	memcpy
.L2520:
	ldr	r8, .L2531
	mov	r3, #5
	ldr	r0, .L2531+4
	mov	r1, r7
	mov	r2, #4
	add	sl, sl, #128
	bl	rknand_print_hex
	ldrb	r2, [r8, #853]	@ zero_extendqisi2
	ldr	r1, [r4, #512]
	cmp	sl, #256
	movcs	sl, #256
	ldr	r0, .L2531+8
	mov	fp, #0
	bl	printk
	ldrb	r3, [r8, #853]	@ zero_extendqisi2
	ldr	r2, [r4, #512]
	mov	r1, sl
	ldr	r0, .L2531+12
	mov	r9, fp
	cmp	r2, r3
	mov	r8, r4
	mov	r2, r6
	strhi	r3, [r4, #512]
	mov	r6, r7
	bl	printk
	mov	r1, sl, asl #7
	str	r1, [sp, #12]
.L2526:
	ldr	r1, .L2531
	movw	r2, #3074
	ldr	r3, [r6], #4
	ldrh	r2, [r1, r2]
	cmp	r3, r2
	bcs	.L2522
	ldr	r2, .L2531+16
	ldr	r2, [r2, #152]
	cmp	r3, r2
	bcc	.L2522
	mov	r0, r5
	mov	r1, #512
	bl	__memzero
	ldr	r2, [sp, #8]
	ldr	r0, [r6, #-4]
	mov	r1, sl
	mul	r0, r0, r2
	mov	r2, r8
	bl	IDBlockWriteData
	ldr	r3, [sp, #8]
	ldr	r0, [r6, #-4]
	mov	r1, sl
	mov	r2, r5
	mul	r0, r0, r3
	bl	IdBlockReadData
	mov	r3, #0
	mov	r7, r3
.L2525:
	ldr	r1, [r5, r3]
	add	r3, r3, #4
	add	r2, r8, r3
	ldr	r2, [r2, #-4]
	cmp	r1, r2
	beq	.L2523
	bic	r4, r7, #255
	stmia	sp, {r1, r2}
	mov	r3, r7
	mov	r1, r9
	ldr	r2, [r6, #-4]
	mov	r4, r4, asl #2
	ldr	r0, .L2531+20
	bl	printk
	ldr	r0, .L2531+24
	add	r1, r8, r4
	mov	r2, #4
	mov	r3, #256
	bl	rknand_print_hex
	mov	r3, #256
	mov	r2, #4
	ldr	r0, .L2531+28
	add	r1, r5, r4
	bl	rknand_print_hex
	mov	r0, r5
	mov	r1, #512
	bl	__memzero
	ldr	r1, [sp, #8]
	ldr	r0, [r6, #-4]
	mov	r2, r5
	mul	r0, r0, r1
	mov	r1, #4
	bl	IDBlockWriteData
	ldr	r0, .L2531+32
	bl	printk
	ldr	r2, [sp, #12]
	cmp	r7, r2
	bcc	.L2522
	b	.L2524
.L2523:
	ldr	r1, [sp, #12]
	add	r7, r7, #1
	cmp	r7, r1
	bne	.L2525
.L2524:
	add	fp, fp, #1
.L2522:
	add	r9, r9, #1
	cmp	r9, #5
	bne	.L2526
	mov	r0, r5
	bl	ftl_free
	cmp	fp, #0
	mvneq	r0, #0
	movne	r0, #0
.L2519:
	add	sp, sp, #20
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, sl, fp, pc}
.L2532:
	.align	2
.L2531:
	.word	.LANCHOR0
	.word	.LC146
	.word	.LC147
	.word	.LC148
	.word	.LANCHOR2
	.word	.LC149
	.word	.LC150
	.word	.LC151
	.word	.LC152
	.fnend
	.size	write_idblock, .-write_idblock
	.align	2
	.global	CRC_32
	.type	CRC_32, %function
CRC_32:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	mov	r3, r0
	mov	r0, #0
	stmfd	sp!, {r4, lr}
	.save {r4, lr}
	mov	r2, r0
	ldr	ip, .L2536
	b	.L2534
.L2535:
	ldrb	r4, [r3, r2]	@ zero_extendqisi2
	add	r2, r2, #1
	eor	r4, r4, r0, lsr #24
	add	r4, ip, r4, asl #2
	ldr	r4, [r4, #3004]
	eor	r0, r4, r0, asl #8
.L2534:
	cmp	r2, r1
	bne	.L2535
	ldmfd	sp!, {r4, pc}
.L2537:
	.align	2
.L2536:
	.word	.LANCHOR1
	.fnend
	.size	CRC_32, .-CRC_32
	.align	2
	.global	rknand_sys_storage_ioctl
	.type	rknand_sys_storage_ioctl, %function
rknand_sys_storage_ioctl:
	.fnstart
	@ args = 0, pretend = 0, frame = 528
	@ frame_needed = 0, uses_anonymous_args = 0
	ldr	r3, .L2654
	stmfd	sp!, {r4, r5, r6, lr}
	.save {r4, r5, r6, lr}
	cmp	r1, r3
	.pad #528
	sub	sp, sp, #528
	mov	r5, r1
	mov	r4, r2
	beq	.L2547
	bhi	.L2556
	ldr	r3, .L2654+4
	cmp	r1, r3
	beq	.L2544
	bhi	.L2557
	sub	r3, r3, #125
	cmp	r1, r3
	beq	.L2541
	bhi	.L2558
	sub	r3, r3, #237
	cmp	r1, r3
	bne	.L2599
	b	.L2650
.L2558:
	ldr	r3, .L2654+8
	cmp	r1, r3
	beq	.L2542
	add	r3, r3, #1
	cmp	r1, r3
	bne	.L2599
	b	.L2651
.L2557:
	ldr	r3, .L2654+12
	cmp	r1, r3
	beq	.L2546
	bhi	.L2559
	ldr	r3, .L2654+16
	cmp	r1, r3
	bne	.L2599
	b	.L2652
.L2559:
	ldr	r3, .L2654+20
	cmp	r1, r3
	beq	.L2546
	add	r3, r3, #10
	cmp	r1, r3
	bne	.L2599
	b	.L2546
.L2556:
	ldr	r3, .L2654+24
	cmp	r1, r3
	beq	.L2552
	bhi	.L2560
	sub	r3, r3, #78
	cmp	r1, r3
	beq	.L2549
	bcc	.L2548
	add	r3, r3, #21
	cmp	r1, r3
	beq	.L2550
	add	r3, r3, #56
	cmp	r1, r3
	bne	.L2599
	b	.L2653
.L2560:
	ldr	r3, .L2654+28
	cmp	r1, r3
	beq	.L2555
	bhi	.L2561
	sub	r3, r3, #956
	sub	r3, r3, #1
	cmp	r1, r3
	beq	.L2553
	add	r3, r3, #956
	cmp	r1, r3
	bne	.L2599
	b	.L2554
.L2561:
	ldr	r3, .L2654+32
	cmp	r1, r3
	beq	.L2554
	add	r3, r3, #1
	cmp	r1, r3
	bne	.L2599
	b	.L2555
.L2548:
	ldr	r0, .L2654+36
	bl	printk
	mov	r0, #4096
	bl	ftl_malloc
	subs	r5, r0, #0
	beq	.L2607
	mov	r1, r4
	mov	r2, #512
	bl	rk_copy_from_user
	cmp	r0, #0
	bne	.L2649
.L2562:
	ldr	r0, .L2654+40
	ldmia	r5, {r1, r2}
	bl	printk
	ldr	r3, [r5, #4]
	cmp	r3, #8
	str	r3, [sp, #4]
	bhi	.L2570
	bl	rknand_device_lock
	ldr	r1, [sp, #4]
	mov	r2, r5
	ldr	r0, [r5, #0]
	bl	IdBlockReadData
	bl	rknand_device_unlock
	ldr	r2, [sp, #4]
	mov	r0, r4
	mov	r1, r5
	mov	r2, r2, asl #9
	bl	rk_copy_to_user
	cmp	r0, #0
	beq	.L2635
	ldr	r0, .L2654+44
.L2648:
	bl	printk
	b	.L2570
.L2549:
	ldr	r0, .L2654+48
	bl	printk
	mov	r0, #4096
	bl	ftl_malloc
	subs	r5, r0, #0
	beq	.L2607
	mov	r1, r4
	mov	r2, #4096
	bl	rk_copy_from_user
	cmp	r0, #0
	bne	.L2649
.L2566:
	ldr	r4, .L2654+52
	ldr	r0, .L2654+56
	ldmia	r5, {r1, r2}
	bl	printk
	ldr	r3, [r4, #464]
	cmp	r3, #0
	bne	.L2567
	mov	r0, #260096
	bl	ftl_malloc
	cmp	r0, #0
	str	r0, [r4, #464]
	beq	.L2570
.L2567:
	ldr	r2, [r5, #4]
	movw	r3, #4088
	cmp	r2, r3
	bhi	.L2570
	ldr	r3, [r5, #0]
	cmp	r3, #251904
	bhi	.L2570
	ldr	r1, .L2654+52
	ldr	r0, [r1, #464]
	add	r1, r5, #8
	add	r0, r0, r3
	bl	memcpy
.L2635:
	mov	r0, r5
	bl	ftl_free
.L2636:
	mov	r4, #0
	b	.L2565
.L2653:
	ldr	r0, .L2654+60
	bl	printk
	mov	r0, #4096
	bl	ftl_malloc
	subs	r5, r0, #0
	beq	.L2607
	mov	r1, r4
	mov	r2, #28
	bl	rk_copy_from_user
	cmp	r0, #0
	beq	.L2569
.L2649:
	ldr	r0, .L2654+64
	b	.L2648
.L2569:
	ldmia	r5, {r1, r2}
	ldr	r0, .L2654+68
	bl	printk
	ldr	r1, [r5, #0]
	cmp	r1, #256000
	bhi	.L2570
	ldr	r4, .L2654+52
	ldr	r0, [r4, #464]
	cmp	r0, #0
	beq	.L2570
.L2571:
	bl	CRC_32
	ldr	r3, [r5, #4]
	cmp	r3, r0
	beq	.L2572
	mov	r0, r5
	bl	ftl_free
	b	.L2613
.L2572:
	bl	rknand_device_lock
	ldr	r1, [r4, #464]
	add	r2, r5, #8
	ldr	r0, [r5, #0]
	bl	write_idblock
	mov	r6, #0
	bl	rknand_device_unlock
	ldr	r0, [r4, #464]
	bl	ftl_free
	str	r6, [r4, #464]
	mov	r0, r5
	mov	r4, r6
	bl	ftl_free
	b	.L2565
.L2570:
	mov	r0, r5
	b	.L2639
.L2550:
	ldr	r0, .L2654+72
	bl	printk
	mov	r0, #4096
	bl	ftl_malloc
	subs	r5, r0, #0
	beq	.L2607
	bl	ReadFlashInfo
	mov	r0, r4
	mov	r1, r5
	mov	r2, #11
	b	.L2646
.L2547:
	ldr	r0, .L2654+76
	bl	printk
	bl	rknand_device_lock
	bl	FtlReInitForSDUpdata
	mov	r6, r0
	bl	rknand_device_unlock
	cmp	r6, #0
	bne	.L2630
	bl	nand_blk_add_whole_disk
	mov	r0, #4096
	bl	ftl_malloc
	subs	r5, r0, #0
	beq	.L2607
	bl	rknand_device_lock
	mov	r1, r6
	mov	r2, #64
	mov	r0, r5
	bl	FlashReadFacBbtData
	bl	rknand_device_unlock
	ldr	r0, .L2654+80
	mov	r1, r5
	mov	r2, #4
	mov	r3, #8
	bl	rknand_print_hex
	mov	r0, r4
	mov	r1, r5
	mov	r2, #64
	b	.L2646
.L2552:
	ldr	r0, .L2654+84
	bl	printk
	ldr	r3, .L2654+52
	add	r1, sp, #528
	mov	r0, r4
	mov	r2, #4
	ldr	r3, [r3, #468]
	ldr	r3, [r3, #20]
	str	r3, [r1, #-524]!
	b	.L2643
.L2553:
	ldr	r0, .L2654+88
	bl	printk
	mov	r0, #4096
	bl	ftl_malloc
	subs	r5, r0, #0
	beq	.L2607
	bl	rknand_device_lock
	mov	r1, #264
	mov	r2, #2
	mov	r3, r5
	mov	r0, #16
	bl	FtlRead
	bl	rknand_device_unlock
	mov	r0, r4
	mov	r1, r5
	mov	r2, #1024
.L2646:
	bl	rk_copy_to_user
	subs	r4, r0, #0
	mov	r0, r5
	beq	.L2575
.L2639:
	bl	ftl_free
	b	.L2630
.L2575:
	bl	ftl_free
	b	.L2565
.L2541:
	ldr	r0, .L2654+92
	bl	printk
	add	r0, sp, #8
	mov	r1, r4
	mov	r2, #520
	bl	rk_copy_from_user
	subs	r5, r0, #0
	bne	.L2640
.L2576:
	ldr	r2, [sp, #8]
	ldr	r3, .L2654+96
	cmp	r2, r3
	bne	.L2632
	ldr	r3, [sp, #12]
	cmp	r3, #512
	bhi	.L2632
	ldr	r6, .L2654+52
	mov	r2, #512
	add	r0, sp, #8
	ldr	r1, [r6, #468]
	bl	memcpy
	ldr	r2, [r6, #472]
	ldr	r3, .L2654+100
	cmp	r2, r3
	beq	.L2577
	add	r0, sp, #72
	mov	r1, #128
	str	r5, [sp, #16]
	str	r5, [sp, #20]
	bl	__memzero
.L2577:
	add	r0, sp, #264
	mov	r1, #256
	mov	r3, #0
	str	r3, [sp, #24]
	bl	__memzero
	b	.L2642
.L2542:
	ldr	r0, .L2654+104
	bl	printk
	add	r0, sp, #8
	mov	r1, r4
	mov	r2, #520
	bl	rk_copy_from_user
	cmp	r0, #0
	bne	.L2640
.L2578:
	ldr	r2, [sp, #8]
	ldr	r3, .L2654+96
	cmp	r2, r3
	bne	.L2632
	ldr	r3, [sp, #12]
	cmp	r3, #512
	bhi	.L2632
	ldr	r4, .L2654+52
	ldr	r3, .L2654+100
	ldr	r2, [r4, #472]
	cmp	r2, r3
	bne	.L2613
	ldr	r3, [sp, #20]
	sub	r2, r3, #1
	cmp	r2, #127
	mvnhi	r4, #2
	bhi	.L2539
	ldr	r0, [r4, #468]
	add	r1, sp, #72
	str	r3, [r0, #12]
	add	r0, r0, #64
	ldr	r2, [sp, #20]
	bl	memcpy
	mov	r0, #1
	ldr	r1, [r4, #468]
	b	.L2637
.L2652:
	ldr	r0, .L2654+108
	bl	printk
	add	r0, sp, #8
	mov	r1, r4
	mov	r2, #520
	bl	rk_copy_from_user
	cmp	r0, #0
	bne	.L2640
.L2579:
	ldr	r2, [sp, #8]
	ldr	r3, .L2654+112
	cmp	r2, r3
	bne	.L2632
	ldr	r3, [sp, #12]
	cmp	r3, #512
	bhi	.L2632
	ldr	r5, .L2654+52
	ldr	r4, [r5, #476]
	cmp	r4, #0
	beq	.L2539
	ldr	r3, [r5, #480]
	ldr	r2, .L2654+116
	ldr	r1, [r3, #0]
	cmp	r1, r2
	beq	.L2580
	str	r2, [r3, #0]
	mov	r2, #504
	ldr	r3, [r5, #480]
	str	r2, [r3, #4]
	mov	r2, #0
	str	r2, [r3, #8]
	str	r2, [r3, #12]
.L2580:
	ldr	r1, [r5, #480]
	mov	r4, #0
	mov	r0, r4
	str	r4, [r1, #16]
	bl	StorageSysDataStore
	ldr	r3, [r5, #468]
	ldr	r2, .L2654+96
	ldr	r5, .L2654+52
	ldr	r1, [r3, #0]
	cmp	r1, r2
	strne	r2, [r3, #0]
	ldr	r0, [r5, #468]
	mov	r1, #128
	ldrne	r3, .L2654+52
	movne	r2, #504
	ldrne	r3, [r3, #468]
	stmneib	r3, {r2, r4}
	mov	r4, #0
	str	r4, [r0, #12]
	add	r0, r0, #64
	bl	__memzero
	mov	r0, #1
	ldr	r1, [r5, #468]
	bl	StorageSysDataStore
	str	r4, [r5, #476]
	str	r4, [r5, #472]
	b	.L2565
.L2544:
	ldr	r0, .L2654+120
	bl	printk
	mov	r1, r4
	add	r0, sp, #8
	mov	r2, #520
	bl	rk_copy_from_user
	subs	r4, r0, #0
	bne	.L2640
.L2582:
	ldr	r2, [sp, #8]
	ldr	r3, .L2654+124
	cmp	r2, r3
	bne	.L2632
	ldr	r3, [sp, #12]
	cmp	r3, #512
	bhi	.L2632
	ldr	r5, .L2654+52
	ldr	r3, [r5, #476]
	cmp	r3, #1
	beq	.L2539
	ldr	r3, [r5, #480]
	mov	r0, #0
	ldr	r2, .L2654+116
	ldr	r1, [r3, #0]
	cmp	r1, r2
	strne	r2, [r3, #0]
	ldr	r1, [r5, #480]
	ldrne	r3, [r5, #480]
	movne	r2, #504
	strne	r4, [r3, #12]
	stmneib	r3, {r2, r4}
	mov	r3, #1
	str	r3, [r1, #16]
	bl	StorageSysDataStore
	ldr	r3, [r5, #468]
	ldr	r2, .L2654+96
	ldr	r1, [r3, #0]
	cmp	r1, r2
	beq	.L2584
	str	r2, [r3, #0]
	mov	r0, #504
	ldr	r3, .L2654+52
	mov	r2, #0
	ldr	r3, [r3, #468]
	stmib	r3, {r0, r2}
.L2584:
	ldr	r5, .L2654+52
	mov	r1, #128
	mov	r4, #0
	ldr	r0, [r5, #468]
	str	r4, [r0, #12]
	add	r0, r0, #64
	bl	__memzero
	mov	r0, #1
	ldr	r1, [r5, #468]
	bl	StorageSysDataStore
	mov	r3, #1
	str	r3, [r5, #476]
	b	.L2565
.L2651:
	ldr	r0, .L2654+128
	bl	printk
	add	r0, sp, #8
	mov	r1, r4
	mov	r2, #520
	bl	rk_copy_from_user
	cmp	r0, #0
	bne	.L2640
.L2585:
	ldr	r2, [sp, #8]
	ldr	r3, .L2654+132
	cmp	r2, r3
	bne	.L2632
	ldr	r2, [sp, #12]
	cmp	r2, #512
	addls	r0, sp, #16
	ldrls	r1, .L2654+136
	bls	.L2641
	b	.L2632
.L2546:
	ldr	r3, .L2654+20
	cmp	r5, r3
	ldreq	r0, .L2654+140
	beq	.L2633
	ldr	r3, .L2654+144
	cmp	r5, r3
	ldreq	r0, .L2654+148
	ldrne	r0, .L2654+152
.L2633:
	bl	printk
	add	r0, sp, #8
	mov	r1, r4
	mov	r2, #520
	bl	rk_copy_from_user
	cmp	r0, #0
	bne	.L2640
.L2589:
	ldr	r2, [sp, #8]
	ldr	r3, .L2654+156
	cmp	r2, r3
	bne	.L2630
	ldr	r3, .L2654+144
	ldr	r6, .L2654+52
	cmp	r5, r3
	bne	.L2590
	ldr	r3, [r6, #468]
	mov	r0, r4
	add	r1, sp, #8
	mov	r2, #16
	ldr	r3, [r3, #20]
	str	r3, [sp, #12]
	strb	r3, [sp, #16]
	bl	rk_copy_to_user
	cmp	r0, #0
	moveq	r4, r0
	mvnne	r4, #13
	b	.L2539
.L2590:
	ldr	r3, [r6, #996]
	cmp	r3, #10
	bhi	.L2630
	ldr	r3, [r6, #468]
	ldr	r1, [sp, #12]
	ldr	r2, [r3, #24]
	cmp	r2, r1
	beq	.L2591
	cmp	r2, #0
	beq	.L2591
	ldr	r0, .L2654+160
	bl	printk
	ldr	r3, [r6, #996]
	add	r3, r3, #1
	str	r3, [r6, #996]
	b	.L2630
.L2591:
	ldr	r0, .L2654+52
	mov	r2, #0
	str	r2, [r0, #996]
	ldr	r0, .L2654+20
	cmp	r5, r0
	mov	r0, #1
	strne	r1, [r3, #24]
	mov	r1, r3
	movne	r2, #1
	streq	r2, [r3, #20]
	streq	r2, [r3, #24]
	strne	r2, [r3, #20]
	bl	StorageSysDataStore
	cmn	r0, #1
	mvneq	r4, #1
	movne	r4, #0
	b	.L2565
.L2554:
	ldr	r0, .L2654+164
	bl	printk
	add	r0, sp, #8
	mov	r1, r4
	mov	r2, #520
	bl	rk_copy_from_user
	cmp	r0, #0
	bne	.L2640
.L2594:
	ldr	r2, [sp, #8]
	ldr	r3, .L2654+168
	cmp	r2, r3
	bne	.L2632
	ldr	r2, [sp, #12]
	cmp	r2, #504
	bhi	.L2632
	ldr	r3, .L2654+172
	add	r0, sp, #16
	cmp	r5, r3
	ldr	r3, .L2654+52
	ldreq	r1, [r3, #1000]
	ldrne	r1, [r3, #1004]
	add	r1, r1, #8
.L2641:
	bl	memcpy
.L2642:
	add	r1, sp, #8
	mov	r0, r4
	mov	r2, #520
.L2643:
	bl	rk_copy_to_user
	subs	r4, r0, #0
	bne	.L2630
	b	.L2565
.L2555:
	ldr	r0, .L2654+176
	bl	printk
	add	r0, sp, #8
	mov	r1, r4
	mov	r2, #520
	bl	rk_copy_from_user
	cmp	r0, #0
	beq	.L2597
.L2640:
	ldr	r0, .L2654+64
	bl	printk
	b	.L2630
.L2597:
	ldr	r2, [sp, #8]
	ldr	r3, .L2654+168
	cmp	r2, r3
	bne	.L2632
	ldr	r2, [sp, #12]
	cmp	r2, #504
	bhi	.L2632
	ldr	r3, .L2654+28
	add	r2, r2, #8
	ldr	r4, .L2654+52
	cmp	r5, r3
	bne	.L2598
	add	r1, sp, #8
	ldr	r0, [r4, #1000]
	bl	memcpy
	ldr	r1, [r4, #1000]
	mov	r0, #2
.L2637:
	bl	StorageSysDataStore
	mov	r4, r0
	b	.L2565
.L2598:
	add	r1, sp, #8
	ldr	r0, [r4, #1004]
	bl	memcpy
	mov	r0, #3
	ldr	r1, [r4, #1004]
	b	.L2637
.L2650:
	bl	rknand_dev_flush
	b	.L2636
.L2632:
	mvn	r4, #0
.L2565:
	mov	r1, r4
	ldr	r0, .L2654+180
	bl	printk
	b	.L2539
.L2599:
	mvn	r4, #21
	b	.L2539
.L2607:
	mvn	r4, #11
	b	.L2539
.L2613:
	mvn	r4, #1
	b	.L2539
.L2630:
	mvn	r4, #13
.L2539:
	mov	r0, r4
	add	sp, sp, #528
	ldmfd	sp!, {r4, r5, r6, pc}
.L2655:
	.align	2
.L2654:
	.word	1074033155
	.word	1074029694
	.word	1074029570
	.word	1074031656
	.word	1074029695
	.word	1074031666
	.word	1074033235
	.word	1074034193
	.word	1074034194
	.word	.LC153
	.word	.LC155
	.word	.LC156
	.word	.LC157
	.word	.LANCHOR2
	.word	.LC158
	.word	.LC159
	.word	.LC154
	.word	.LC160
	.word	.LC161
	.word	.LC162
	.word	.LC163
	.word	.LC164
	.word	.LC165
	.word	.LC166
	.word	1263358532
	.word	-1067903959
	.word	.LC167
	.word	.LC168
	.word	1112753220
	.word	1146313043
	.word	.LC169
	.word	1112755781
	.word	.LC170
	.word	1094995539
	.word	.LANCHOR2+484
	.word	.LC171
	.word	1074031676
	.word	.LC172
	.word	.LC173
	.word	1280262987
	.word	.LC174
	.word	.LC175
	.word	1145980246
	.word	1074034192
	.word	.LC176
	.word	.LC177
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
	ldr	r4, .L2659
	mov	r6, #0
	add	r0, r4, #484
	ldr	r5, [r4, #456]
	str	r6, [r4, #464]
	add	r1, r5, #1536
	add	r3, r5, #512
	str	r5, [r4, #480]
	str	r3, [r4, #468]
	add	r3, r5, #1024
	str	r1, [r4, #1004]
	str	r3, [r4, #1000]
	bl	memcpy
	ldr	r7, [r5, #508]
	ldr	r3, [r5, #16]
	cmp	r7, r6
	str	r6, [r4, #472]
	str	r6, [r4, #996]
	str	r3, [r4, #476]
	beq	.L2657
	mov	r0, r5
	mov	r1, #508
	bl	JSHash
	cmp	r7, r0
	beq	.L2657
	str	r6, [r5, #16]
	ldr	r0, .L2659+4
	str	r6, [r4, #476]
	bl	printk
.L2657:
	ldr	r3, [r4, #476]
	mov	r0, #2
	ldr	r4, .L2659
	cmp	r3, #0
	ldrne	r2, .L2659+8
	ldrne	r3, .L2659
	ldr	r1, [r4, #1000]
	strne	r2, [r3, #472]
	bl	StorageSysDataLoad
	ldr	r1, [r4, #1004]
	mov	r0, #3
	bl	StorageSysDataLoad
	ldmfd	sp!, {r3, r4, r5, r6, r7, lr}
	b	rknand_sys_storage_init
.L2660:
	.align	2
.L2659:
	.word	.LANCHOR2
	.word	.LC178
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
	stmfd	sp!, {r3, r4, r5, r6, r7, r8, sl, lr}
	.save {r3, r4, r5, r6, r7, r8, sl, lr}
	mov	r0, #65536
	mov	r1, #209
	mov	r2, #4
	bl	kmalloc_order_trace
	ldr	r7, .L2670
	cmp	r0, #0
	str	r0, [r7, #1008]
	beq	.L2668
	ldr	sl, .L2670+4
	mov	r6, #0
	mov	r4, r6
	mov	r5, r6
	movw	r8, #65532
.L2666:
	ldr	r0, [r7, #1008]
	mov	r1, r5, asl #7
	mov	r2, #0
	bl	rk_ftl_vendor_ops.constprop.28
	cmp	r0, #0
	bne	.L2664
	ldr	r3, [r7, #1008]
	ldr	r0, .L2670+8
	ldr	r1, [r3, #0]
	ldr	r2, [r3, r8]
	ldr	r3, [r3, #4]
	bl	printk
	ldr	r0, [r7, #1008]
	ldr	r3, [r0, #0]
	cmp	r3, sl
	bne	.L2665
	ldr	r3, [r0, r8]
	ldr	r2, [r0, #4]
	cmp	r3, r2
	bne	.L2665
	cmp	r4, r3
	movcc	r6, r5
	movcc	r4, r3
.L2665:
	add	r5, r5, #1
	cmp	r5, #2
	bne	.L2666
	cmp	r4, #0
	beq	.L2667
	mov	r1, r6, asl #7
	mov	r2, #0
	bl	rk_ftl_vendor_ops.constprop.28
	cmp	r0, #0
	ldmeqfd	sp!, {r3, r4, r5, r6, r7, r8, sl, pc}
	b	.L2664
.L2667:
	mov	r1, #65536
	bl	__memzero
	ldr	r3, .L2670
	ldr	r1, .L2670+4
	mov	r2, #1
	mov	r0, r4
	ldr	r3, [r3, #1008]
	stmia	r3, {r1, r2}
	movw	r1, #65532
	str	r2, [r3, r1]
	ldr	r2, .L2670+12
	strh	r4, [r3, #12]	@ movhi
	strh	r2, [r3, #14]	@ movhi
	ldmfd	sp!, {r3, r4, r5, r6, r7, r8, sl, pc}
.L2664:
	ldr	r4, .L2670
	ldr	r0, [r4, #1008]
	bl	kfree
	mov	r3, #0
	mvn	r0, #0
	str	r3, [r4, #1008]
	ldmfd	sp!, {r3, r4, r5, r6, r7, r8, sl, pc}
.L2668:
	mvn	r0, #11
	ldmfd	sp!, {r3, r4, r5, r6, r7, r8, sl, pc}
.L2671:
	.align	2
.L2670:
	.word	.LANCHOR2
	.word	1380668996
	.word	.LC179
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
	mov	r3, r0
	mov	r0, r1
	ldr	r1, .L2679
	stmfd	sp!, {r4, r5, r6, lr}
	.save {r4, r5, r6, lr}
	ldr	ip, [r1, #1008]
	cmp	ip, #0
	ldrneh	r4, [ip, #10]
	movne	r5, ip
	movne	r1, #0
	bne	.L2674
	b	.L2678
.L2676:
	ldrh	r6, [r5, #16]
	add	r5, r5, #8
	cmp	r6, r3
	bne	.L2675
	add	r1, r1, #2
	add	r1, ip, r1, asl #3
	ldrh	r4, [r1, #4]
	ldrh	r1, [r1, #2]
	cmp	r2, r4
	movcc	r4, r2
	add	r1, r1, #1024
	add	r1, ip, r1
	mov	r2, r4
	bl	memcpy
	mov	r0, r4
	ldmfd	sp!, {r4, r5, r6, pc}
.L2675:
	add	r1, r1, #1
.L2674:
	cmp	r1, r4
	bcc	.L2676
	mvn	r0, #0
	ldmfd	sp!, {r4, r5, r6, pc}
.L2678:
	mvn	r0, #0
	ldmfd	sp!, {r4, r5, r6, pc}
.L2680:
	.align	2
.L2679:
	.word	.LANCHOR2
	.fnend
	.size	rk_ftl_vendor_read, .-rk_ftl_vendor_read
	.align	2
	.global	rk_ftl_vendor_write
	.type	rk_ftl_vendor_write, %function
rk_ftl_vendor_write:
	.fnstart
	@ args = 0, pretend = 0, frame = 16
	@ frame_needed = 0, uses_anonymous_args = 0
	ldr	r3, .L2698
	stmfd	sp!, {r4, r5, r6, r7, r8, r9, sl, fp, lr}
	.save {r4, r5, r6, r7, r8, r9, sl, fp, lr}
	mov	sl, r0
	ldr	r4, [r3, #1008]
	.pad #20
	sub	sp, sp, #20
	mov	r9, r1
	mov	r5, r2
	cmp	r4, #0
	beq	.L2694
	add	r8, r2, #63
	ldrh	r2, [r4, #8]
	ldrh	r3, [r4, #10]
	bic	r8, r8, #63
	mov	r7, #0
	str	r2, [sp, #8]
	b	.L2683
.L2690:
	add	r2, r7, #2
	mov	r2, r2, asl #3
	add	r6, r4, r2
	ldrh	r2, [r4, r2]
	cmp	r2, sl
	bne	.L2684
	ldrh	r2, [r6, #4]
	add	r2, r2, #63
	bic	r2, r2, #63
	str	r2, [sp, #12]
	cmp	r5, r2
	bls	.L2685
	ldrh	r2, [r4, #14]
	cmp	r2, r8
	bcc	.L2694
	sub	r3, r3, #1
	mov	fp, r8
	ldrh	r6, [r6, #2]
	mov	r8, r3
	b	.L2686
.L2687:
	add	r7, r7, #1
	mov	r2, r2, asl #3
	add	r0, r7, #2
	add	r3, r4, r2
	mov	r0, r0, asl #3
	add	r1, r4, r0
	ldrh	r0, [r4, r0]
	strh	r0, [r4, r2]	@ movhi
	add	r0, r6, #1024
	ldrh	r2, [r1, #4]
	add	r0, r4, r0
	strh	r6, [r3, #2]	@ movhi
	strh	r2, [r3, #4]	@ movhi
	ldrh	r3, [r1, #4]
	ldrh	r1, [r1, #2]
	add	r3, r3, #63
	bic	r3, r3, #63
	add	r1, r1, #1024
	add	r1, r4, r1
	str	r3, [sp, #4]
	mov	r2, r3
	bl	memcpy
	ldr	r3, [sp, #4]
	add	r6, r6, r3
.L2686:
	cmp	r7, r8
	add	r2, r7, #2
	bcc	.L2687
	mov	r2, r2, asl #3
	uxth	r6, r6
	add	r3, r4, r2
	add	r0, r6, #1024
	strh	sl, [r4, r2]	@ movhi
	add	r0, r4, r0
	strh	r6, [r3, #2]	@ movhi
	mov	r2, r5
	strh	r5, [r3, #4]	@ movhi
	mov	r1, r9
	bl	memcpy
	ldrh	r3, [r4, #14]
	ldr	r2, [sp, #12]
	uxth	r8, fp
	add	r6, r6, r8
	strh	r6, [r4, #12]	@ movhi
	add	r3, r2, r3
	rsb	r8, r8, r3
	strh	r8, [r4, #14]	@ movhi
	b	.L2697
.L2685:
	ldrh	r0, [r6, #2]
	mov	r1, r9
	mov	r2, r5
	add	r0, r0, #1024
	add	r0, r4, r0
	bl	memcpy
	strh	r5, [r6, #4]	@ movhi
	b	.L2697
.L2684:
	add	r7, r7, #1
.L2683:
	cmp	r7, r3
	bcc	.L2690
	ldrh	r2, [r4, #14]
	cmp	r2, r8
	bcc	.L2694
	add	r3, r3, #2
	uxth	r8, r8
	mov	r1, r9
	mov	r3, r3, asl #3
	add	r2, r4, r3
	strh	sl, [r4, r3]	@ movhi
	ldrh	r3, [r4, #12]
	strh	r5, [r2, #4]	@ movhi
	strh	r3, [r2, #2]	@ movhi
	ldrh	r3, [r4, #12]
	add	r3, r8, r3
	strh	r3, [r4, #12]	@ movhi
	ldrh	r3, [r4, #14]
	rsb	r8, r8, r3
	strh	r8, [r4, #14]	@ movhi
	ldrh	r0, [r2, #2]
	mov	r2, r5
	add	r0, r0, #1024
	add	r0, r4, r0
	bl	memcpy
	ldrh	r3, [r4, #10]
	add	r3, r3, #1
	strh	r3, [r4, #10]	@ movhi
.L2697:
	ldr	r3, [r4, #4]
	movw	r2, #65532
	mov	r0, r4
	add	r3, r3, #1
	str	r3, [r4, #4]
	str	r3, [r4, r2]
	mov	r2, #1
	ldrh	r3, [r4, #8]
	add	r3, r3, #1
	uxth	r3, r3
	strh	r3, [r4, #8]	@ movhi
	cmp	r3, #1
	movhi	r3, #0
	strhih	r3, [r4, #8]	@ movhi
	ldr	r3, [sp, #8]
	mov	r1, r3, asl #7
	bl	rk_ftl_vendor_ops.constprop.28
	mov	r0, #0
	b	.L2682
.L2694:
	mvn	r0, #0
.L2682:
	add	sp, sp, #20
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, sl, fp, pc}
.L2699:
	.align	2
.L2698:
	.word	.LANCHOR2
	.fnend
	.size	rk_ftl_vendor_write, .-rk_ftl_vendor_write
	.align	2
	.global	rk_ftl_vendor_storage_ioctl
	.type	rk_ftl_vendor_storage_ioctl, %function
rk_ftl_vendor_storage_ioctl:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	ldr	r3, .L2717
	stmfd	sp!, {r4, r5, r6, lr}
	.save {r4, r5, r6, lr}
	mov	r5, r2
	mov	r6, r1
	ldr	r0, [r3, #48]
	mov	r1, #208
	mov	r2, #4096
	bl	kmem_cache_alloc_trace
	subs	r4, r0, #0
	mvneq	r5, #0
	beq	.L2701
	ldr	r3, .L2717+4
	cmp	r6, r3
	beq	.L2703
	add	r3, r3, #1
	cmp	r6, r3
	bne	.L2714
	b	.L2716
.L2703:
	mov	r1, r5
	mov	r2, #8
	bl	rk_copy_from_user
	cmp	r0, #0
	bne	.L2714
	ldr	r2, [r4, #0]
	ldr	r3, .L2717+8
	cmp	r2, r3
	bne	.L2713
	ldrh	r0, [r4, #4]
	add	r1, r4, #8
	ldrh	r2, [r4, #6]
	bl	rk_ftl_vendor_read
	cmn	r0, #1
	beq	.L2709
	uxth	r2, r0
	mov	r1, r4
	strh	r2, [r4, #6]	@ movhi
	mov	r0, r5
	add	r2, r2, #8
	bl	rk_copy_to_user
	cmp	r0, #0
	moveq	r5, r0
	mvnne	r5, #13
	b	.L2702
.L2716:
	mov	r1, r5
	mov	r2, #8
	bl	rk_copy_from_user
	cmp	r0, #0
	bne	.L2714
	ldr	r2, [r4, #0]
	ldr	r3, .L2717+8
	cmp	r2, r3
	bne	.L2713
	ldrh	r2, [r4, #6]
	movw	r3, #4087
	cmp	r2, r3
	bhi	.L2713
	mov	r0, r4
	mov	r1, r5
	add	r2, r2, #8
	bl	rk_copy_from_user
	cmp	r0, #0
	bne	.L2714
	ldrh	r0, [r4, #4]
	add	r1, r4, #8
	ldrh	r2, [r4, #6]
	bl	rk_ftl_vendor_write
.L2709:
	mov	r5, r0
	b	.L2702
.L2713:
	mvn	r5, #0
	b	.L2702
.L2714:
	mvn	r5, #13
.L2702:
	mov	r0, r4
	bl	kfree
.L2701:
	mov	r0, r5
	ldmfd	sp!, {r4, r5, r6, pc}
.L2718:
	.align	2
.L2717:
	.word	kmalloc_caches
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
	.global	NandFlashParaTbl
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
	.global	c_ftl_nand_blks_per_die_shift
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
	.set	.LANCHOR3,. + 0
	.type	__func__.14256, %object
	.size	__func__.14256, 11
__func__.14256:
	.ascii	"FtlMemInit\000"
.LC0:
	.byte	60
	.byte	40
	.byte	24
	.byte	16
	.type	__func__.15186, %object
	.size	__func__.15186, 21
__func__.15186:
	.ascii	"FtlVpcCheckAndModify\000"
	.type	__func__.14327, %object
	.size	__func__.14327, 8
__func__.14327:
	.ascii	"FtlInit\000"
	.section	.rodata.str1.1,"aMS",%progbits,1
.LC1:
	.ascii	"FlashEraseBlocks pageAddr error %x\012\000"
.LC2:
	.ascii	"No.%d FLASH ID:%x %x %x %x %x %x\012\000"
.LC3:
	.ascii	"phyBlk = 0x%x die = %d block_in_die = 0x%x 0x%8x\012"
	.ascii	"\000"
.LC4:
	.ascii	"remove_from_free_sys_Queue %x\012\000"
.LC5:
	.ascii	"FLASH INFO:\012\000"
.LC6:
	.ascii	"FLASH ID: %x\012\000"
.LC7:
	.ascii	"Device Capacity: %d MB\012\000"
.LC8:
	.ascii	"FMWAIT: %x %x %x %x\012\000"
.LC9:
	.ascii	"FTL INFO:\012\000"
.LC10:
	.ascii	"g_MaxLpn = 0x%x\012\000"
.LC11:
	.ascii	"g_VaildLpn = 0x%x\012\000"
.LC12:
	.ascii	"read_page_count = 0x%x\012\000"
.LC13:
	.ascii	"discard_page_count = 0x%x\012\000"
.LC14:
	.ascii	"write_page_count = 0x%x\012\000"
.LC15:
	.ascii	"cache_write_count = 0x%x\012\000"
.LC16:
	.ascii	"l2p_write_count = 0x%x\012\000"
.LC17:
	.ascii	"gc_page_count = 0x%x\012\000"
.LC18:
	.ascii	"totle_write = %d MB\012\000"
.LC19:
	.ascii	"totle_read = %d MB\012\000"
.LC20:
	.ascii	"GSV = 0x%x\012\000"
.LC21:
	.ascii	"GDV = 0x%x\012\000"
.LC22:
	.ascii	"bad blk num = %d %d\012\000"
.LC23:
	.ascii	"free_superblocks = 0x%x\012\000"
.LC24:
	.ascii	"mlc_EC = 0x%x\012\000"
.LC25:
	.ascii	"slc_EC = 0x%x\012\000"
.LC26:
	.ascii	"avg_EC = 0x%x\012\000"
.LC27:
	.ascii	"sys_EC = 0x%x\012\000"
.LC28:
	.ascii	"max_EC = 0x%x\012\000"
.LC29:
	.ascii	"min_EC = 0x%x\012\000"
.LC30:
	.ascii	"PLT = 0x%x\012\000"
.LC31:
	.ascii	"POT = 0x%x\012\000"
.LC32:
	.ascii	"MaxSector = 0x%x\012\000"
.LC33:
	.ascii	"init_sys_blks_pp = 0x%x\012\000"
.LC34:
	.ascii	"sys_blks_pp = 0x%x\012\000"
.LC35:
	.ascii	"free sysblock = 0x%x\012\000"
.LC36:
	.ascii	"data_blks_pp = 0x%x\012\000"
.LC37:
	.ascii	"data_op_blks_pp = 0x%x\012\000"
.LC38:
	.ascii	"max_data_blks = 0x%x\012\000"
.LC39:
	.ascii	"Sys.id = 0x%x\012\000"
.LC40:
	.ascii	"Bbt.id = 0x%x\012\000"
.LC41:
	.ascii	"ACT.page = 0x%x\012\000"
.LC42:
	.ascii	"ACT.plane = 0x%x\012\000"
.LC43:
	.ascii	"ACT.id = 0x%x\012\000"
.LC44:
	.ascii	"ACT.mode = 0x%x\012\000"
.LC45:
	.ascii	"ACT.a_pages = 0x%x\012\000"
.LC46:
	.ascii	"ACT VPC = 0x%x\012\000"
.LC47:
	.ascii	"BUF.page = 0x%x\012\000"
.LC48:
	.ascii	"BUF.plane = 0x%x\012\000"
.LC49:
	.ascii	"BUF.id = 0x%x\012\000"
.LC50:
	.ascii	"BUF.mode = 0x%x\012\000"
.LC51:
	.ascii	"BUF.a_pages = 0x%x\012\000"
.LC52:
	.ascii	"BUF VPC = 0x%x\012\000"
.LC53:
	.ascii	"TMP.page = 0x%x\012\000"
.LC54:
	.ascii	"TMP.plane = 0x%x\012\000"
.LC55:
	.ascii	"TMP.id = 0x%x\012\000"
.LC56:
	.ascii	"TMP.mode = 0x%x\012\000"
.LC57:
	.ascii	"TMP.a_pages = 0x%x\012\000"
.LC58:
	.ascii	"GC.page = 0x%x\012\000"
.LC59:
	.ascii	"GC.plane = 0x%x\012\000"
.LC60:
	.ascii	"GC.id = 0x%x\012\000"
.LC61:
	.ascii	"GC.mode = 0x%x\012\000"
.LC62:
	.ascii	"GC.a_pages = 0x%x\012\000"
.LC63:
	.ascii	"WR_CHK = 0x%x %x %x %x\012\000"
.LC64:
	.ascii	"Read Err Cnt = 0x%x\012\000"
.LC65:
	.ascii	"Prog Err Cnt = 0x%x\012\000"
.LC66:
	.ascii	"gc_free_blk_th= 0x%x\012\000"
.LC67:
	.ascii	"gc_merge_free_blk_th= 0x%x\012\000"
.LC68:
	.ascii	"gc_skip_write_count= 0x%x\012\000"
.LC69:
	.ascii	"gc_blk_index= 0x%x\012\000"
.LC70:
	.ascii	"free min EC= 0x%x\012\000"
.LC71:
	.ascii	"free max EC= 0x%x\012\000"
.LC72:
	.ascii	"GC__SB VPC = 0x%x\012\000"
.LC73:
	.ascii	"%d. [0x%x]=0x%x 0x%x  0x%x\012\000"
.LC74:
	.ascii	"free %d. [0x%x] 0x%x  0x%x\012\000"
.LC75:
	.ascii	"%s\012\000"
.LC76:
	.ascii	"FTL version: 5.0.47 20171120\000"
.LC77:
	.ascii	"swblk %x ,avg = %x max= %x vpc= %x,ec=%x ,max ec=%x"
	.ascii	"\012\000"
.LC78:
	.ascii	"FtlGcRefreshBlock  0x%x\012\000"
.LC79:
	.ascii	"FtlGcMarkBadPhyBlk %d 0x%x\012\000"
.LC80:
	.ascii	"%s error allocating memory. return -1\012\000"
.LC81:
	.ascii	"%s 0x%x:\000"
.LC82:
	.ascii	"%x \000"
.LC83:
	.ascii	"\000"
.LC84:
	.ascii	"%d statReg->V6.mtrans_cnt=%d flReg.V6.page_num=%d\012"
	.ascii	"\000"
.LC85:
	.ascii	"nandc:\000"
.LC86:
	.ascii	"%d flReg.d32=%x %x\012\000"
.LC87:
	.ascii	"micron RR %d row=%x,count %d,status=%d\012\000"
.LC88:
	.ascii	"ECC:%d\012\000"
.LC89:
	.ascii	"sdr read ok %x ecc=%d\012\000"
.LC90:
	.ascii	"sync para %d\012\000"
.LC91:
	.ascii	"TOG mode Read error %x %x\012\000"
.LC92:
	.ascii	"read retry status %x %x %x\012\000"
.LC93:
	.ascii	"Read pageadd=%x  ecc=%x err=%x\012\000"
.LC94:
	.ascii	"data:\000"
.LC95:
	.ascii	"spare:\000"
.LC96:
	.ascii	"ReadRetry pageadd=%x ecc=%x err=%x\012\000"
.LC97:
	.ascii	"FtlGcScanTempBlkError ID %x %x!!!!!!!\012\000"
.LC98:
	.ascii	"prog error: = %x\012\000"
.LC99:
	.ascii	"prog read error: = %x\012\000"
.LC100:
	.ascii	"prog read s error: = %x %x %x\012\000"
.LC101:
	.ascii	"prog read d error: = %x %x %x\012\000"
.LC102:
	.ascii	"FtlVpcTblFlush error = %x error count = %d\012\000"
.LC103:
	.ascii	"FtlBbmTblFlush id=%x,page=%x,previd=%x cnt=%d\012\000"
.LC104:
	.ascii	"FtlBbmTblFlush error:%x\012\000"
.LC105:
	.ascii	"FtlBbmTblFlush error = %x error count = %d\012\000"
.LC106:
	.ascii	"FtlGcFreeBadSuperBlk 0x%x\012\000"
.LC107:
	.ascii	"decrement_vpc_count %x = %d\012\000"
.LC108:
	.ascii	"FlashMakeFactorBbt %d\012\000"
.LC109:
	.ascii	"bad block:%d %d\012\000"
.LC110:
	.ascii	"FMFB:%d %d\012\000"
.LC111:
	.ascii	"E:bad block:%d\012\000"
.LC112:
	.ascii	"FMFB:Save %d %d\012\000"
.LC113:
	.ascii	"ftl_map_blk_gc blk info: %x %x %x\012\000"
.LC114:
	.ascii	"page map lost: %x %x\012\000"
.LC115:
	.ascii	"FtlMapWritePage error = %x\012\000"
.LC116:
	.ascii	"FtlMapWritePage error = %x error count = %d\012\000"
.LC117:
	.ascii	"FtlVendorPartRead refresh = %x phyAddr = %x\012\000"
.LC118:
	.ascii	"slc mode\000"
.LC119:
	.ascii	"no ect\000"
.LC120:
	.ascii	"page map lost %x %x %x %x\012\000"
.LC121:
	.ascii	"load_l2p_region refresh = %x phyAddr = %x\012\000"
.LC122:
	.ascii	"RSB refresh addr %x\012\000"
.LC123:
	.ascii	"spuer block %x vpn is 0\012 \000"
.LC124:
	.ascii	"g_recovery_ppa %x ver %x\012 \000"
.LC125:
	.ascii	"...%s enter...\012\000"
.LC126:
	.ascii	"FtlCheckVpc %x = %x  %x\012\000"
.LC127:
	.ascii	"%d GC datablk  = %x vpc %x %x\012\000"
.LC128:
	.ascii	"SWL %x, FSB = %x vpc= %x,ec=%x th=%x\012\000"
.LC129:
	.ascii	"Ftlwrite decrement_vpc_count %x = %d\012\000"
.LC130:
	.ascii	"GC des block %x done\012\000"
.LC131:
	.ascii	"fix power lost blk = %x vpc=%x\012\000"
.LC132:
	.ascii	"erase power lost blk = %x vpc=%x\012\000"
.LC133:
	.ascii	"...%s: no bad block mapping table, format device\012"
	.ascii	"\000"
.LC134:
	.ascii	"...%s FtlSysBlkInit error ,format device!\012\000"
.LC135:
	.ascii	"FtlWrite: lpa error:%x %x\012\000"
.LC136:
	.ascii	"FLFB:%d %d\012\000"
.LC137:
	.ascii	"BBT:\000"
.LC138:
	.ascii	"otp error! %d\000"
.LC139:
	.ascii	"rr\000"
.LC140:
	.ascii	"FlashLoadPhyInfo fail %x!!\012\000"
.LC141:
	.ascii	"FtlInit %x\012\000"
.LC142:
	.ascii	"IdBlockReadData %x %x\012\000"
.LC143:
	.ascii	"IdBlockReadData %x %x ret= %x\012\000"
.LC144:
	.ascii	"IDBlockWriteData %x %x\012\000"
.LC145:
	.ascii	"IDBlockWriteData %x %x ret= %x\012\000"
.LC146:
	.ascii	"idblk:\000"
.LC147:
	.ascii	"idb reverse %x %x\012\000"
.LC148:
	.ascii	"write_idblock totle_sec %x %x\012\000"
.LC149:
	.ascii	"write and check error:%d idb=%x,offset=%x,r=%x,w=%x"
	.ascii	"\012\000"
.LC150:
	.ascii	"write\000"
.LC151:
	.ascii	"read\000"
.LC152:
	.ascii	"write_idblock error\012\000"
.LC153:
	.ascii	"READ_SECTOR_IO\012\000"
.LC154:
	.ascii	"rk_copy_from_user error\012\000"
.LC155:
	.ascii	"READ_SECTOR_IO %x %x\012\000"
.LC156:
	.ascii	"rk_copy_to_user error\012\000"
.LC157:
	.ascii	"WRITE_SECTOR_IO\012\000"
.LC158:
	.ascii	"WRITE_SECTOR_IO %x %x\012\000"
.LC159:
	.ascii	"END_WRITE_SECTOR_IO\012\000"
.LC160:
	.ascii	"END_WRITE_SECTOR_IO %x %x\012\000"
.LC161:
	.ascii	"GET_FLASH_INFO_IO\012\000"
.LC162:
	.ascii	"GET_BAD_BLOCK_IO\012\000"
.LC163:
	.ascii	"bbt:\000"
.LC164:
	.ascii	"GET_LOCK_FLAG_IO\012\000"
.LC165:
	.ascii	"GET_PUBLIC_KEY_IO\012\000"
.LC166:
	.ascii	"RKNAND_GET_DRM_KEY\012\000"
.LC167:
	.ascii	"RKNAND_STORE_DRM_KEY\012\000"
.LC168:
	.ascii	"RKNAND_DIASBLE_SECURE_BOOT\012\000"
.LC169:
	.ascii	"RKNAND_ENASBLE_SECURE_BOOT\012\000"
.LC170:
	.ascii	"RKNAND_GET_SN_SECTOR\012\000"
.LC171:
	.ascii	"RKNAND_LOADER_UNLOCK\012\000"
.LC172:
	.ascii	"RKNAND_LOADER_STATUS\012\000"
.LC173:
	.ascii	"RKNAND_LOADER_LOCK\012\000"
.LC174:
	.ascii	"LockKey not match %d\012\000"
.LC175:
	.ascii	"RKNAND_GET_VENDOR_SECTOR\012\000"
.LC176:
	.ascii	"RKNAND_STORE_VENDOR_SECTOR\012\000"
.LC177:
	.ascii	"return ret = %lx\012\000"
.LC178:
	.ascii	"secureBootEn check error\012\000"
.LC179:
	.ascii	"\0013vendor storage %x,%x,%x\012\000"
	.data
	.align	2
	.set	.LANCHOR1,. + 0
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
	.type	NandFlashParaTbl, %object
	.size	NandFlashParaTbl, 2304
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
	.short	1503
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
	.short	1497
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
	.short	1497
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
	.set	.LANCHOR0,. + 0
	.set	.LANCHOR2,. + 8184
	.type	gReadRetryInfo, %object
	.size	gReadRetryInfo, 852
gReadRetryInfo:
	.space	852
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
	.type	gFlashSlcMode, %object
	.size	gFlashSlcMode, 1
gFlashSlcMode:
	.space	1
	.space	3
	.type	gNandIDataBuf, %object
	.size	gNandIDataBuf, 2048
gNandIDataBuf:
	.space	2048
	.type	IDByte, %object
	.size	IDByte, 32
IDByte:
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
	.type	gDieOp, %object
	.size	gDieOp, 128
gDieOp:
	.space	128
	.type	read_retry_cur_offset, %object
	.size	read_retry_cur_offset, 4
read_retry_cur_offset:
	.space	4
	.type	g_maxRegNum, %object
	.size	g_maxRegNum, 1
g_maxRegNum:
	.space	1
	.type	g_retryMode, %object
	.size	g_retryMode, 1
g_retryMode:
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
	.type	gFlashInterfaceMode, %object
	.size	gFlashInterfaceMode, 1
gFlashInterfaceMode:
	.space	1
	.type	gFlashToggleModeEn, %object
	.size	gFlashToggleModeEn, 1
gFlashToggleModeEn:
	.space	1
	.space	2
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
	.type	c_ftl_nand_blks_per_die_shift, %object
	.size	c_ftl_nand_blks_per_die_shift, 2
c_ftl_nand_blks_per_die_shift:
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
	.type	g_cur_erase_blk, %object
	.size	g_cur_erase_blk, 4
g_cur_erase_blk:
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
	.type	p_sys_data_buf, %object
	.size	p_sys_data_buf, 4
p_sys_data_buf:
	.space	4
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
	.type	g_inkDie_check_enable, %object
	.size	g_inkDie_check_enable, 4
g_inkDie_check_enable:
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
	.type	g_sys_save_data, %object
	.size	g_sys_save_data, 48
g_sys_save_data:
	.space	48
	.type	c_ftl_nand_data_op_blks_per_plane, %object
	.size	c_ftl_nand_data_op_blks_per_plane, 2
c_ftl_nand_data_op_blks_per_plane:
	.space	2
	.space	2
	.type	gSysInfo, %object
	.size	gSysInfo, 12
gSysInfo:
	.space	12
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
	.type	g_gc_blk_num, %object
	.size	g_gc_blk_num, 2
g_gc_blk_num:
	.space	2
	.space	2
	.type	p_gc_blk_tbl, %object
	.size	p_gc_blk_tbl, 4
p_gc_blk_tbl:
	.space	4
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
	.type	g_free_slc_blk_num, %object
	.size	g_free_slc_blk_num, 2
g_free_slc_blk_num:
	.space	2
	.space	2
	.type	p_map_block_valid_page_count, %object
	.size	p_map_block_valid_page_count, 4
p_map_block_valid_page_count:
	.space	4
	.type	p_l2p_map_buf, %object
	.size	p_l2p_map_buf, 4
p_l2p_map_buf:
	.space	4
	.type	gL2pMapInfo, %object
	.size	gL2pMapInfo, 44
gL2pMapInfo:
	.space	44
	.type	g_totle_map_block, %object
	.size	g_totle_map_block, 2
g_totle_map_block:
	.space	2
	.space	2
	.type	p_map_block_table, %object
	.size	p_map_block_table, 4
p_map_block_table:
	.space	4
	.type	p_map_block_ver_table, %object
	.size	p_map_block_ver_table, 4
p_map_block_ver_table:
	.space	4
	.type	p_map_region_ppn_table, %object
	.size	p_map_region_ppn_table, 4
p_map_region_ppn_table:
	.space	4
	.type	c_ftl_nand_bbm_buf_size, %object
	.size	c_ftl_nand_bbm_buf_size, 2
c_ftl_nand_bbm_buf_size:
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
	.type	p_swl_mul_table, %object
	.size	p_swl_mul_table, 4
p_swl_mul_table:
	.space	4
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
	.type	gp_ect_tbl_info, %object
	.size	gp_ect_tbl_info, 4
gp_ect_tbl_info:
	.space	4
	.type	p_valid_page_count_check_table, %object
	.size	p_valid_page_count_check_table, 4
p_valid_page_count_check_table:
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
	.type	p_map_region_ppn_check_table, %object
	.size	p_map_region_ppn_check_table, 4
p_map_region_ppn_check_table:
	.space	4
	.type	gNandMaxChip, %object
	.size	gNandMaxChip, 1
gNandMaxChip:
	.space	1
	.space	1
	.type	gTotleBlock, %object
	.size	gTotleBlock, 2
gTotleBlock:
	.space	2
	.type	mlcPageToSlcPageTbl, %object
	.size	mlcPageToSlcPageTbl, 1024
mlcPageToSlcPageTbl:
	.space	1024
	.type	RK29_NANDC_REG_BASE, %object
	.size	RK29_NANDC_REG_BASE, 4
RK29_NANDC_REG_BASE:
	.space	4
	.type	gFlashPageBuffer1, %object
	.size	gFlashPageBuffer1, 4
gFlashPageBuffer1:
	.space	4
	.type	g_maxRetryCount, %object
	.size	g_maxRetryCount, 1
g_maxRetryCount:
	.space	1
	.space	3
	.type	gpFlashSaveInfo, %object
	.size	gpFlashSaveInfo, 4
gpFlashSaveInfo:
	.space	4
	.type	gFlashPageBuffer0, %object
	.size	gFlashPageBuffer0, 4
gFlashPageBuffer0:
	.space	4
	.type	gNandFlashIDBEccBits, %object
	.size	gNandFlashIDBEccBits, 1
gNandFlashIDBEccBits:
	.space	1
	.space	3
	.type	gNandFlashIdbBlockAddr, %object
	.size	gNandFlashIdbBlockAddr, 4
gNandFlashIdbBlockAddr:
	.space	4
	.type	gNandFlashInfoBlockAddr, %object
	.size	gNandFlashInfoBlockAddr, 4
gNandFlashInfoBlockAddr:
	.space	4
	.type	gNandIDBResBlkNumSaveInFlash, %object
	.size	gNandIDBResBlkNumSaveInFlash, 1
gNandIDBResBlkNumSaveInFlash:
	.space	1
	.space	3
	.type	FlashDdrTunningReadCount, %object
	.size	FlashDdrTunningReadCount, 4
FlashDdrTunningReadCount:
	.space	4
	.type	gpReadRetrial, %object
	.size	gpReadRetrial, 4
gpReadRetrial:
	.space	4
	.type	gMultiPageReadEn, %object
	.size	gMultiPageReadEn, 1
gMultiPageReadEn:
	.space	1
	.space	3
	.type	req_sys, %object
	.size	req_sys, 36
req_sys:
	.space	36
	.type	g_MaxLbn, %object
	.size	g_MaxLbn, 4
g_MaxLbn:
	.space	4
	.type	gFlashProgCheckBuffer, %object
	.size	gFlashProgCheckBuffer, 4
gFlashProgCheckBuffer:
	.space	4
	.type	gFlashProgCheckSpareBuffer, %object
	.size	gFlashProgCheckSpareBuffer, 4
gFlashProgCheckSpareBuffer:
	.space	4
	.type	gFlashSpareBuffer, %object
	.size	gFlashSpareBuffer, 4
gFlashSpareBuffer:
	.space	4
	.type	FbbtBlk, %object
	.size	FbbtBlk, 16
FbbtBlk:
	.space	16
	.type	gVendorBlkInfo, %object
	.size	gVendorBlkInfo, 44
gVendorBlkInfo:
	.space	44
	.type	g_ect_tbl_power_up_flush, %object
	.size	g_ect_tbl_power_up_flush, 2
g_ect_tbl_power_up_flush:
	.space	2
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
	.type	gLoaderBootInfo, %object
	.size	gLoaderBootInfo, 4
gLoaderBootInfo:
	.space	4
	.type	RK29_NANDC1_REG_BASE, %object
	.size	RK29_NANDC1_REG_BASE, 4
RK29_NANDC1_REG_BASE:
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
