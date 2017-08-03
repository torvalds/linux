/*
 * Copyright (c) 2016, Fuzhou Rockchip Electronics Co., Ltd
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 * date: 2017-10-16
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
	.type	FlashGetReadRetryDefault.part.19, %function
FlashGetReadRetryDefault.part.19:
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
	.size	FlashGetReadRetryDefault.part.19, .-FlashGetReadRetryDefault.part.19
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
	.global	NandcFlashCsDebug
	.type	NandcFlashCsDebug, %function
NandcFlashCsDebug:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	@ link register save eliminated.
	ldr	r3, .L127
	ldr	r3, [r3, #864]
	ldr	r2, [r3, #0]
	bfi	r2, r0, #0, #8
	str	r2, [r3, #0]
	bx	lr
.L128:
	.align	2
.L127:
	.word	.LANCHOR0
	.fnend
	.size	NandcFlashCsDebug, .-NandcFlashCsDebug
	.align	2
	.global	NandcFlashCs
	.type	NandcFlashCs, %function
NandcFlashCs:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	@ link register save eliminated.
	ldr	r3, .L130
	add	r3, r3, r0, asl #3
	mov	r0, #1
	ldr	r2, [r3, #864]
	ldrb	r1, [r3, #868]	@ zero_extendqisi2
	ldr	r3, [r2, #0]
	mov	r1, r0, asl r1
	bfi	r3, r1, #0, #8
	str	r3, [r2, #0]
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
	add	r3, r3, r0, asl #3
	ldr	r2, [r3, #864]
	ldr	r3, [r2, #0]
	bfc	r3, #0, #8
	bfc	r3, #17, #1
	str	r3, [r2, #0]
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
	@ args = 0, pretend = 0, frame = 8
	@ frame_needed = 0, uses_anonymous_args = 0
	@ link register save eliminated.
	.pad #8
	sub	sp, sp, #8
	mov	r0, r0, lsr #4
	str	r0, [sp, #4]
.L136:
	ldr	r0, [sp, #4]
	cmp	r0, #0
	sub	r3, r0, #1
	str	r3, [sp, #4]
	bne	.L136
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
	ldr	r3, .L147
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
	bne	.L143
	cmp	r2, #0
	ldreqb	r2, [r3, #909]	@ zero_extendqisi2
	ldrneb	r2, [r3, #910]	@ zero_extendqisi2
	str	r2, [r0, #8]
	ldrb	r3, [r3, #911]	@ zero_extendqisi2
	cmp	r3, #0
	movne	r3, #0
	ldrne	r2, .L147
	bne	.L142
	b	.L143
.L144:
	mov	ip, r3, asl #3
	add	r3, r3, #1
	mov	ip, r1, lsr ip
	uxtb	ip, ip
	str	ip, [r0, #4]
.L142:
	ldrb	ip, [r2, #911]	@ zero_extendqisi2
	cmp	r3, ip
	bcc	.L144
.L143:
	mov	r0, #80
	bl	NandcDelayns
	ldr	r0, [r4, r5, asl #8]
	uxtb	r0, r0
	ldmfd	sp!, {r3, r4, r5, pc}
.L148:
	.align	2
.L147:
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
.L153:
	mov	r0, r6
	mov	r1, r5
	mov	r2, r4
	bl	FlashReadStatusEN
	cmp	r0, #255
	beq	.L153
	tst	r0, #64
	beq	.L153
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
	ldr	r3, .L157
	stmfd	sp!, {r4, r5, r6, lr}
	.save {r4, r5, r6, lr}
	add	r4, r3, r0, asl #4
	ldrb	r5, [r3, r0, asl #4]	@ zero_extendqisi2
	mov	r6, r0
	ldr	r3, [r4, #8]
	cmp	r3, #0
	beq	.L155
	mov	r0, r5
	bl	NandcFlashCs
	ldr	r3, .L157+4
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
.L155:
	mov	r0, #0
	ldmfd	sp!, {r4, r5, r6, pc}
.L158:
	.align	2
.L157:
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
	ldr	r9, .L165
	mov	r4, r0
	mov	r5, r1
	mov	r6, r2
	ldr	r3, [r9, #3624]
	ldrb	r3, [r3, #19]	@ zero_extendqisi2
	cmp	r3, #6
	addeq	r9, r9, #20
	addeq	r9, r9, r0, asl #6
	addeq	r9, r9, r8, asl #2
	beq	.L161
	cmp	r3, #7
	addeq	r9, r9, #28
	addne	r3, r8, r0, asl #3
	moveq	r3, #160
	addne	r9, r9, #20
	mlaeq	r9, r3, r0, r9
	moveq	r3, #10
	addne	r9, r9, r3, asl #3
	mlaeq	r9, r3, r8, r9
.L161:
	ldr	r3, .L165
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
	b	.L163
.L164:
	ldrb	r3, [r6, r7]	@ zero_extendqisi2
	mov	r0, #200
	str	r3, [sl, #4]
	bl	NandcDelayns
	ldrsb	r3, [r9, r7]
	add	r7, r7, #1
	str	r3, [sl, #0]
.L163:
	uxtb	r3, r7
	cmp	r3, r5
	bcc	.L164
	mov	r3, #22
	mov	r0, r4
	str	r3, [sl, #8]
	bl	NandcFlashDeCs
	ldr	r3, .L165
	add	r4, r3, r4
	strb	r8, [r4, #3756]
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, sl, pc}
.L166:
	.align	2
.L165:
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
	ldr	r6, .L172
	ldr	r3, [r6, #3624]
	ldrb	r3, [r3, #19]	@ zero_extendqisi2
	sub	r3, r3, #1
	uxtb	r3, r3
	cmp	r3, #6
	ldmhifd	sp!, {r4, r5, r6, pc}
	ldr	r5, .L172+4
	mov	r4, #0
.L170:
	ldrb	r3, [r5, r4, asl #3]	@ zero_extendqisi2
	uxtb	r0, r4
	cmp	r3, #173
	bne	.L169
	ldrb	r1, [r6, #1]	@ zero_extendqisi2
	mov	r3, #0
	ldr	r2, .L172+8
	bl	HynixSetRRPara
.L169:
	add	r4, r4, #1
	cmp	r4, #4
	bne	.L170
	ldmfd	sp!, {r4, r5, r6, pc}
.L173:
	.align	2
.L172:
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
	ldr	r3, .L177
	mov	r5, #0
	ldr	sl, .L177+4
	mov	r8, #161
	add	r1, r3, r1, asl #2
	add	r6, r1, #3
	mov	r7, r3
	b	.L175
.L176:
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
.L175:
	ldrb	r3, [sl, #3760]	@ zero_extendqisi2
	cmp	r5, r3
	bcc	.L176
	ldmfd	sp!, {r3, r4, r5, r6, r7, r8, sl, pc}
.L178:
	.align	2
.L177:
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
	ldr	r8, .L186
	mov	r4, r0
	add	r6, r6, r6, asl #2
	mov	r5, #0
	add	r3, r8, #45
	add	r7, r8, r6
	ldr	sl, .L186+4
	add	r6, r3, r6
	add	r9, r8, r1
	b	.L180
.L184:
	mov	r3, #85
	str	r3, [r4, #8]
	ldrsb	r3, [r5, r8]
	mov	r0, #200
	str	r3, [r4, #4]
	bl	NandcDelayns
	ldrb	r3, [sl, #3761]	@ zero_extendqisi2
	cmp	r3, #34
	ldreqsb	r3, [r7, #0]
	beq	.L185
	cmp	r3, #35
	addne	r3, r9, #2912
	addne	r3, r3, #12
	ldreqsb	r3, [r6, #0]
	ldrnesb	r3, [r3, #0]
.L185:
	add	r5, r5, #1
	add	r7, r7, #1
	add	r6, r6, #1
	str	r3, [r4, #0]
.L180:
	ldrb	r3, [sl, #3760]	@ zero_extendqisi2
	cmp	r5, r3
	bcc	.L184
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, sl, pc}
.L187:
	.align	2
.L186:
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
	ldr	r3, .L189
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
.L190:
	.align	2
.L189:
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
	ldr	r3, .L196
	stmfd	sp!, {r0, r1, r2, r4, r5, lr}
	.save {r0, r1, r2, r4, r5, lr}
	add	r3, r3, r0, asl #3
	ldr	r4, .L196+4
	ldr	r5, [r3, #864]
.L193:
	mov	r0, #100
	bl	NandcDelayns
	ldr	r3, [r5, #0]
	str	r3, [sp, #4]
	ldr	r3, [sp, #4]
	tst	r3, #512
	bne	.L194
	subs	r4, r4, #1
	bne	.L193
	mvn	r0, #0
	b	.L192
.L194:
	mov	r0, #0
.L192:
	ldmfd	sp!, {r1, r2, r3, r4, r5, pc}
.L197:
	.align	2
.L196:
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
	ldr	r5, .L206
	b	.L199
.L205:
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
	bcs	.L201
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
	bne	.L204
	ldr	r0, .L206+4
	ldr	r1, [sp, #8]
	bl	printk
.L204:
	mov	r0, sl
	bl	NandcFlashDeCs
.L201:
	sub	r6, r6, #1
	add	r7, r7, #1
	add	r4, r4, #36
	uxtb	r6, r6
.L199:
	cmp	r7, r8
	bne	.L205
	mov	r0, #0
	add	sp, sp, #20
	ldmfd	sp!, {r4, r5, r6, r7, r8, sl, pc}
.L207:
	.align	2
.L206:
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
	ldr	r1, .L214
	add	r3, r5, r5, asl #2
	mvn	r6, #4
	add	r0, r1, r3
	add	r1, r1, #45
	add	r1, r1, r3
	mov	r2, #0
	ldr	ip, .L214+4
	mul	r5, r6, r5
	b	.L209
.L212:
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
.L209:
	ldrb	r6, [ip, #3760]	@ zero_extendqisi2
	cmp	r2, r6
	bcc	.L212
	mov	r0, #0
	ldmfd	sp!, {r4, r5, r6, lr}
	b	NandcWaitFlashReady
.L215:
	.align	2
.L214:
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
	ldr	r3, .L217
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
.L218:
	.align	2
.L217:
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
	ldr	r3, .L220
	mov	ip, #0
	stmfd	sp!, {r4, r5, r6, lr}
	.save {r4, r5, r6, lr}
	mov	r6, r2
	ldrb	r2, [r3, #2837]	@ zero_extendqisi2
	ldr	r3, .L220+4
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
.L221:
	.align	2
.L220:
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
	ldr	r3, .L224
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
.L225:
	.align	2
.L224:
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
	ldr	r3, .L228
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
	bhi	.L226
	ldrb	r1, [r5, #2]	@ zero_extendqisi2
	ldrb	r3, [r5, #1]	@ zero_extendqisi2
	ldr	r0, .L228+4
	str	r1, [sp, #0]
	ldrb	r1, [r5, #3]	@ zero_extendqisi2
	str	r1, [sp, #4]
	ldrb	r1, [r5, #4]	@ zero_extendqisi2
	str	r1, [sp, #8]
	ldrb	r1, [r5, #5]	@ zero_extendqisi2
	str	r1, [sp, #12]
	add	r1, r4, #1
	bl	printk
.L226:
	ldmfd	sp!, {r0, r1, r2, r3, r4, r5, r6, r7, r8, pc}
.L229:
	.align	2
.L228:
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
	ldr	sl, .L244
	mov	r7, #239
	ldr	r9, .L244+4
	mov	r1, r4
	sub	r8, sl, #864
	mov	r6, #128
	mov	r5, #1
.L240:
	mov	r2, sl
	ldrb	lr, [r4, r9]	@ zero_extendqisi2
	ldr	r3, [r2, r4]!
	cmp	lr, #152
	cmpne	lr, #69
	ldrb	r2, [r2, #4]	@ zero_extendqisi2
	add	r2, r2, #8
	add	ip, r3, r2, asl #8
	beq	.L231
	cmp	lr, #173
	cmpne	lr, #44
	bne	.L232
.L231:
	cmp	r0, #1
	ldrb	fp, [r8, #3772]	@ zero_extendqisi2
	bne	.L233
	tst	fp, #1
	beq	.L232
	cmp	lr, #173
	str	r7, [ip, #8]
	streq	r0, [ip, #4]
	beq	.L243
	cmp	lr, #44
	streq	r0, [ip, #4]
	strne	r6, [ip, #4]
	moveq	ip, #5
	strne	r0, [r3, r2, asl #8]
	bne	.L238
	b	.L242
.L233:
	tst	fp, #4
	beq	.L232
	cmp	lr, #173
	str	r7, [ip, #8]
	streq	r5, [ip, #4]
	moveq	ip, #32
	beq	.L242
	cmp	lr, #44
	strne	r6, [ip, #4]
	bne	.L243
	str	r5, [ip, #4]
	mov	ip, #35
.L242:
	str	ip, [r3, r2, asl #8]
	b	.L238
.L243:
	str	r1, [r3, r2, asl #8]
.L238:
	str	r1, [r3, r2, asl #8]
	str	r1, [r3, r2, asl #8]
	str	r1, [r3, r2, asl #8]
.L232:
	add	r4, r4, #8
	cmp	r4, #32
	bne	.L240
	mov	r0, #0
	bl	NandcWaitFlashReady
	mov	r0, #0
	ldmfd	sp!, {r3, r4, r5, r6, r7, r8, r9, sl, fp, pc}
.L245:
	.align	2
.L244:
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
	ldr	r4, .L248
	bl	NandcWaitFlashReady
	bl	FlashSetReadRetryDefault
	ldrb	r3, [r4, #3773]	@ zero_extendqisi2
	cmp	r3, #0
	beq	.L247
	ldrb	r3, [r4, #3772]	@ zero_extendqisi2
	tst	r3, #1
	beq	.L247
	mov	r0, #1
	bl	FlashSetInterfaceMode
	mov	r0, #1
	bl	NandcSetMode
	mov	r3, #0
	strb	r3, [r4, #3773]
.L247:
	ldr	r3, [r4, #864]
	mov	r0, #0
	str	r0, [r3, #336]
	ldmfd	sp!, {r4, pc}
.L249:
	.align	2
.L248:
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
	ldr	r3, .L254
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
	bne	.L251
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
	b	.L253
.L251:
	uxtb	r2, r5
	str	r2, [r6, #4]
	mov	r2, r5, lsr #8
	str	r2, [r6, #4]
	mov	r2, r5, lsr #16
	str	r2, [r6, #4]
	ldrb	r3, [r3, #905]	@ zero_extendqisi2
	str	r3, [r6, #8]
.L253:
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
.L255:
	.align	2
.L254:
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
	ldr	r3, .L257
	add	r3, r3, r0, asl #3
	ldr	r3, [r3, #864]
	str	r1, [r3, #336]
	bx	lr
.L258:
	.align	2
.L257:
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
	ldr	r1, .L269
	bl	__aeabi_idiv
	ldr	r3, .L269+4
	ldr	r3, [r3, #3012]
	cmp	r0, #250
	movwgt	r2, #8354
	bgt	.L267
	cmp	r0, #220
	bgt	.L268
.L262:
	cmp	r0, #185
	movwgt	r2, #4226
	bgt	.L267
	cmp	r0, #160
	movwgt	r2, #4194
	bgt	.L267
	cmp	r4, #35
	movwls	r2, #4193
	bls	.L267
	cmp	r4, #99
	movwls	r2, #4225
	bls	.L267
.L268:
	movw	r2, #8322
.L267:
	str	r2, [r3, #4]
	ldmfd	sp!, {r4, pc}
.L270:
	.align	2
.L269:
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
	ldr	r4, .L272
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
	ldr	r2, .L272+4
	str	r2, [r3, #304]
	bl	ftl_malloc
	str	r5, [r4, #3816]
	str	r5, [r4, #3824]
	str	r0, [r4, #3788]
	str	r0, [r4, #3792]
	add	r0, r0, #32768
	str	r0, [r4, #3796]
	ldmfd	sp!, {r3, r4, r5, pc}
.L273:
	.align	2
.L272:
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
	bls	.L275
	movw	r3, #4226
	movw	r2, #8322
	cmp	r0, r3
	cmpne	r0, r2
	bne	.L276
.L275:
	ldr	r3, .L277
	ldr	r3, [r3, #3012]
	str	r0, [r3, #4]
.L276:
	ldr	r3, .L277+4
	ldrb	r0, [r3, #2849]	@ zero_extendqisi2
	b	NandcTimeCfg
.L278:
	.align	2
.L277:
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
	ldr	ip, .L280
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
.L281:
	.align	2
.L280:
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
	ldr	r3, .L287
	mov	r1, #1
	ldr	r2, [r3, #3012]
	str	r0, [r3, #3828]
	mov	r3, #0
	str	r1, [r2, #8]
	mov	r1, #16
	cmp	r0, r1
	bfi	r3, r1, #8, #8
	bfc	r3, #18, #1
	beq	.L286
.L283:
	cmp	r0, #24
	orreq	r3, r3, #16
	beq	.L284
	cmp	r0, #40
	orr	r3, r3, #262144
	orr	r3, r3, #16
	bne	.L284
.L286:
	bfc	r3, #4, #1
.L284:
	orr	r3, r3, #1
	str	r3, [r2, #12]
	bx	lr
.L288:
	.align	2
.L287:
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
	ldr	r3, .L290
	strb	r0, [r3, #3832]
	b	NandcBchSel
.L291:
	.align	2
.L290:
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
	ldr	r3, .L297
	ldr	r5, .L297+4
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
.L294:
	ldrb	r3, [r5, r4, asl #3]	@ zero_extendqisi2
	sub	r3, r3, #1
	uxtb	r3, r3
	cmp	r3, #253
	bhi	.L293
	uxtb	r0, r4
	bl	FlashReset
.L293:
	add	r4, r4, #1
	cmp	r4, #4
	bne	.L294
	ldr	r4, .L297
	ldrb	r3, [r4, #3773]	@ zero_extendqisi2
	cmp	r3, #0
	beq	.L295
	mov	r0, #1
	bl	NandcSetMode
	ldrb	r0, [r4, #3772]	@ zero_extendqisi2
	bl	FlashSetInterfaceMode
	ldrb	r0, [r4, #3772]	@ zero_extendqisi2
	bl	NandcSetMode
	ldrb	r0, [r4, #3033]	@ zero_extendqisi2
	bl	NandcSetDdrPara
.L295:
	ldr	r3, .L297
	ldr	r3, [r3, #3624]
	ldrb	r0, [r3, #20]	@ zero_extendqisi2
	bl	FlashBchSel
	mov	r0, #0
	ldmfd	sp!, {r3, r4, r5, pc}
.L298:
	.align	2
.L297:
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
	bne	.L304
	ldmfd	sp!, {r4, lr}
	b	wait_for_nand_flash_ready
.L304:
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
	ldr	r5, .L321
	mov	r6, r0
	str	r1, [sp, #12]
	mov	r7, r2
	ldrb	r4, [r5, #852]	@ zero_extendqisi2
	cmp	r4, #0
	moveq	r9, r5
	beq	.L306
	mov	r1, r2
	bl	FlashEraseSLc2KBlocks
	b	.L307
.L315:
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
	bcs	.L309
	ldrb	r3, [r5, #3833]	@ zero_extendqisi2
	cmp	r3, #0
	add	r3, r9, r0, asl #4
	ldr	r3, [r3, #3636]
	moveq	sl, #0
	cmp	r3, #0
	beq	.L311
	uxtb	r0, r0
	bl	FlashWaitCmdDone
.L311:
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
	bne	.L313
	ldrb	r3, [r5, #928]	@ zero_extendqisi2
	cmp	r3, #0
	beq	.L313
	bl	flash_enter_slc_mode
	b	.L314
.L313:
	bl	flash_exit_slc_mode
.L314:
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
.L309:
	add	r4, r4, #1
.L306:
	cmp	r4, r7
	bcc	.L315
	ldr	r5, .L321
	mov	r4, #0
	ldr	r6, .L321+4
	ldr	r0, [r5, #3012]
	bl	NandcIqrWaitFlashReady
	b	.L316
.L318:
	uxtb	r0, r4
	bl	FlashWaitCmdDone
	ldr	r3, [sp, #12]
	cmp	r3, #1
	bne	.L317
	ldrb	r3, [r5, #928]	@ zero_extendqisi2
	cmp	r3, #0
	beq	.L317
	ldrb	r0, [r6, r4, asl #4]	@ zero_extendqisi2
	bl	flash_exit_slc_mode
.L317:
	add	r4, r4, #1
.L316:
	ldrb	r3, [r5, #3762]	@ zero_extendqisi2
	cmp	r4, r3
	bcc	.L318
	mov	r0, #0
.L307:
	add	sp, sp, #28
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, sl, fp, pc}
.L322:
	.align	2
.L321:
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
	ldr	r3, .L325
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
.L326:
	.align	2
.L325:
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
.L328:
	ldr	r3, [r0, #8]
	str	r3, [sp, #4]
	ldr	r3, [sp, #4]
	tst	r3, #1048576
	beq	.L328
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
	ldr	r2, .L343
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
	bls	.L331
	ldr	r3, [r5, #16]
	cmp	r9, #0
	cmpeq	r1, #0
	str	r3, [sp, #12]
	ldr	r3, [sp, #12]
	bfc	r3, #2, #1
	str	r3, [sp, #12]
	beq	.L332
	cmp	r6, #0
	beq	.L333
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
	b	.L335
.L338:
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
.L335:
	ldr	sl, [sp, #4]
	cmp	r0, sl
	bcc	.L338
.L333:
	mov	r0, r5
	add	r8, r8, #1
	bl	rk_nandc_xfer_irq_flag_init
	mov	r0, r5
	mov	r1, #0
	mov	r8, r8, asr #1
	bl	NandCIrqEnable
	bfi	r4, r8, #22, #6
	ldr	r8, .L343
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
.L332:
	ldr	r3, [sp, #12]
	str	r3, [r5, #16]
.L331:
	str	r7, [r5, #12]
	str	r4, [r5, #8]
	orr	r4, r4, #4
	str	r4, [r5, #8]
	add	sp, sp, #20
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, sl, fp, pc}
.L344:
	.align	2
.L343:
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
	bne	.L346
	cmp	r3, #0
	beq	.L347
	mov	r0, r2
	mov	r1, r3
	mov	r2, #1024
	bl	memcpy
.L347:
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
.L346:
	cmp	r3, #0
	beq	.L349
	mov	r1, r2
	mov	r0, r3
	mov	r2, #1024
	bl	memcpy
.L349:
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
	b	.L351
.L352:
	add	r3, r3, #1
	mov	r2, r2, asl #1
	uxth	r3, r3
.L351:
	cmp	r2, r0
	bls	.L352
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
	ldr	r3, .L357
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
.L358:
	.align	2
.L357:
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
	ldr	r3, .L374
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
	ldr	r2, .L374+4
.L360:
	strb	r3, [r3, r2]
	add	r3, r3, #1
	cmp	r3, #32
	bne	.L360
	ldrh	r2, [r4, #14]
	ldrh	r3, [r4, #20]
	cmp	r3, r2, lsr #8
	bcs	.L361
	ldr	r2, .L374+4
	sub	ip, r1, #1
	uxtb	r7, r0
	mov	r3, #0
	rsb	sl, r0, r2
	str	sl, [sp, #8]
	mla	ip, r0, ip, r2
	mov	r8, r7, asl #1
	uxtb	r8, r8
	str	ip, [sp, #12]
	b	.L362
.L363:
	strb	ip, [r9, r2]
	add	fp, ip, r7
	add	ip, ip, r8
	add	r5, r5, #1
	strb	fp, [sl, r2]
	uxtb	ip, ip
.L365:
	cmp	r5, r1
	add	r2, r2, r0
	bcc	.L363
	add	r3, r3, #1
.L362:
	cmp	r3, r0
	bcs	.L364
	ldr	fp, [sp, #8]
	mov	r2, #0
	uxtb	ip, r3
	mov	r5, r2
	add	r9, fp, r3
	ldr	fp, [sp, #12]
	add	sl, fp, r3
	b	.L365
.L364:
	ldr	r3, .L374
	movw	r2, #3862
	mov	r1, r1, asl #1
	mov	r6, r6, lsr #1
	strh	r1, [r3, r2]	@ movhi
	movw	r2, #3850
	strh	r6, [r3, r2]	@ movhi
.L361:
	ldr	r3, .L374
	movw	r2, #3898
	cmp	lr, #1
	mov	r1, #5
	mov	ip, #0
	ldr	sl, .L374
	ldrb	r8, [r3, #852]	@ zero_extendqisi2
	strh	r1, [r3, r2]	@ movhi
	movw	r1, #3900
	streqh	lr, [r3, r2]	@ movhi
	cmp	r8, #0
	movw	r2, #3902
	strh	ip, [r3, r1]	@ movhi
	mov	r1, #4352
	strh	r1, [r3, r2]	@ movhi
	ldrne	r3, .L374
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
	ldr	r4, .L374
	ldrls	r2, .L374
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
	ldreq	r3, .L374
	uxtah	r6, r6, r0
	str	r6, [r4, #3836]
	ldr	r4, .L374
	addeq	r6, r6, #4
	streq	r6, [r3, #3836]
	ldr	r0, [r4, #3836]
	uxth	r0, r0
	bl	FtlSysBlkNumInit
	ldr	r3, [r4, #3836]
	ldr	r2, [r4, #3852]
	mov	r0, #2048
	str	r3, [r4, #3940]
	movw	r3, #3908
	ldrh	r3, [r4, r3]
	mov	r2, r2, asl #2
	mul	r2, r3, r2
	movw	r3, #3916
	ldrh	r3, [r4, r3]
	add	r3, r3, #9
	mov	r2, r2, lsr r3
	movw	r3, #3944
	add	r2, r2, #2
	strh	r2, [r4, r3]	@ movhi
	movw	r3, #3914
	ldrh	r1, [r4, r3]
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
	addne	r2, r2, #5
	strneh	r1, [r4, r3]	@ movhi
	strne	r2, [r4, #3936]
	add	sp, sp, #20
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, sl, fp, pc}
.L375:
	.align	2
.L374:
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
	ldr	r3, .L383
	mov	r2, #3952
	ldrh	r2, [r3, r2]
	cmp	r2, #0
	beq	.L381
	movw	r2, #3928
	ldrh	r1, [r3, r2]
	ldr	r2, [r3, #3956]
	mov	r3, #0
	b	.L378
.L380:
	ldrh	ip, [r2], #2
	cmp	ip, r0
	beq	.L382
	add	r3, r3, #1
	uxth	r3, r3
.L378:
	cmp	r3, r1
	bne	.L380
	mov	r0, #0
	bx	lr
.L382:
	mov	r0, #1
	bx	lr
.L381:
	mov	r0, r2
	bx	lr
.L384:
	.align	2
.L383:
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
	ldr	r2, .L393
	stmfd	sp!, {r4, lr}
	.save {r4, lr}
	ldr	r3, [r2, #3960]
	cmp	r3, #0
	beq	.L390
	ldr	ip, [r2, #3964]
	mov	r2, #0
.L389:
	ldr	r4, [ip, #16]
	cmp	r4, r0
	bcc	.L387
	cmp	r4, r1
	bls	.L391
.L387:
	add	r2, r2, #1
	add	ip, ip, #36
	cmp	r2, r3
	bne	.L389
	mov	r0, #0
	ldmfd	sp!, {r4, pc}
.L391:
	mov	r0, #1
	ldmfd	sp!, {r4, pc}
.L390:
	mov	r0, r3
	ldmfd	sp!, {r4, pc}
.L394:
	.align	2
.L393:
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
	ldr	r3, .L396
	ldr	r0, [r3, #3948]
	bx	lr
.L397:
	.align	2
.L396:
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
	ldr	r3, .L399
	ldr	r0, [r3, #3948]
	bx	lr
.L400:
	.align	2
.L399:
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
	ldr	r3, .L402
	ldr	r0, [r3, #3968]
	bx	lr
.L403:
	.align	2
.L402:
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
	ldr	r3, .L405
	mov	r2, #3840
	ldrh	r2, [r3, r2]
	ldr	r0, [r3, #3972]
	mul	r0, r0, r2
	bx	lr
.L406:
	.align	2
.L405:
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
	ldr	r3, .L408
	mov	r1, #3840
	movw	r2, #3850
	ldrh	r2, [r3, r2]
	ldrh	r0, [r3, r1]
	mul	r0, r0, r2
	bx	lr
.L409:
	.align	2
.L408:
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
	ldr	r4, .L411
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
	ldr	r0, .L411+4
	bl	printk
	movw	r3, #3982
	ldrh	r2, [r4, r3]
	mov	r0, #0
	add	r2, r2, r6
	strh	r2, [r4, r3]	@ movhi
	ldmfd	sp!, {r2, r3, r4, r5, r6, pc}
.L412:
	.align	2
.L411:
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
	ldr	r5, .L414
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
.L415:
	.align	2
.L414:
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
	ldr	r6, .L421
	mov	r8, #3904
	movw	r7, #3862
	b	.L418
.L420:
	mov	r0, r5
	add	r5, r5, #1
	bl	FtlBbmIsBadBlock
	uxth	r5, r5
	cmp	r0, #0
	addne	r4, r4, #1
	uxthne	r4, r4
.L418:
	ldrh	r3, [r6, r8]
	ldrh	r2, [r6, r7]
	mul	r3, r2, r3
	cmp	r5, r3
	blt	.L420
	mov	r0, r4
	ldmfd	sp!, {r4, r5, r6, r7, r8, pc}
.L422:
	.align	2
.L421:
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
	ldr	r5, .L424
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
.L425:
	.align	2
.L424:
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
	ldr	r3, .L427
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
.L428:
	.align	2
.L427:
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
	ldr	r4, .L430
	ldrh	r1, [r4, r3]
	bl	__aeabi_uidivmod
	movw	r3, #3864
	uxth	r0, r1
	ldrh	r1, [r4, r3]
	bl	__aeabi_uidiv
	uxth	r0, r0
	ldmfd	sp!, {r4, pc}
.L431:
	.align	2
.L430:
	.word	.LANCHOR0
	.fnend
	.size	P2V_block_in_plane, .-P2V_block_in_plane
	.align	2
	.type	FtlFreeSysBlkQueueOut.part.5, %function
FtlFreeSysBlkQueueOut.part.5:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	ldr	r3, .L434
	movw	r2, #4038
	stmfd	sp!, {r4, r5, r6, lr}
	.save {r4, r5, r6, lr}
	movw	r0, #4036
	ldrh	r1, [r3, r2]
	add	ip, r1, #4
	add	r1, r1, #1
	bic	r1, r1, #64512
	add	ip, r3, ip, asl #1
	ldrh	r5, [ip, r0]
	add	r0, r0, #6
	strh	r1, [r3, r2]	@ movhi
	ldrh	ip, [r3, r0]
	sub	ip, ip, #1
	strh	ip, [r3, r0]	@ movhi
	ldrb	r3, [r3, #928]	@ zero_extendqisi2
	cmp	r3, #0
	beq	.L433
	mov	r0, r5
	ldr	r4, .L434+4
	bl	P2V_block_in_plane
	mov	r1, #1
	mov	r3, r5, asl #10
	mov	r2, r1
	mov	r6, r0
	ldr	r0, [r4, #-2092]
	mov	r6, r6, asl #1
	str	r3, [r0, #4]
	bl	FlashEraseBlocks
	ldr	r3, [r4, #-2088]
	ldrh	r2, [r3, r6]
	add	r2, r2, #1
	strh	r2, [r3, r6]	@ movhi
	ldr	r3, [r4, #-2084]
	add	r3, r3, #1
	str	r3, [r4, #-2084]
.L433:
	mov	r0, r5
	ldmfd	sp!, {r4, r5, r6, pc}
.L435:
	.align	2
.L434:
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
	bls	.L437
	rsb	r0, r1, r0
	cmp	r0, #-2147483648
	movhi	r0, #0
	movls	r0, #1
	bx	lr
.L437:
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
	ldr	r2, .L440
	movw	r3, #4042
	ldrh	r0, [r2, r3]
	rsbs	r0, r0, #1
	movcc	r0, #0
	bx	lr
.L441:
	.align	2
.L440:
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
	ldr	r2, .L443
	movw	r3, #4042
	ldrh	r0, [r2, r3]
	sub	r2, r0, #1024
	rsbs	r0, r2, #0
	adc	r0, r0, r2
	bx	lr
.L444:
	.align	2
.L443:
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
	ldr	r2, .L448
	movw	r3, #4042
	stmfd	sp!, {r4, r5, r6, lr}
	.save {r4, r5, r6, lr}
	mov	r5, r0
	ldrh	r3, [r2, r3]
	cmp	r3, #1024
	ldmeqfd	sp!, {r4, r5, r6, pc}
	cmp	r1, #0
	beq	.L447
	bl	P2V_block_in_plane
	ldr	r4, .L448+4
	mov	r1, #1
	mov	r3, r5, asl #10
	mov	r2, r1
	mov	r6, r0
	ldr	r0, [r4, #-2092]
	mov	r6, r6, asl #1
	str	r3, [r0, #4]
	bl	FlashEraseBlocks
	ldr	r3, [r4, #-2088]
	ldrh	r2, [r3, r6]
	add	r2, r2, #1
	strh	r2, [r3, r6]	@ movhi
	ldr	r3, [r4, #-2084]
	add	r3, r3, #1
	str	r3, [r4, #-2084]
.L447:
	ldr	r3, .L448
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
.L449:
	.align	2
.L448:
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
	ldr	r5, .L460
	movw	r8, #4042
	movw	r7, #4038
	ldr	r6, .L460+4
	b	.L451
.L452:
	ldrh	r3, [r5, r7]
	add	r3, r4, r3
	add	r3, r5, r3, asl #1
	add	r3, r3, #4032
	ldrh	r0, [r3, #12]
	bl	P2V_block_in_plane
	ldr	r2, [r6, #-2088]
	ldr	r3, [r6, #-2080]
	mov	r0, r0, asl #1
	ldrh	r2, [r2, r0]
	str	r2, [r3, r4, asl #2]
	add	r4, r4, #1
	uxth	r4, r4
.L451:
	ldrh	r3, [r5, r8]
	cmp	r3, r4
	bhi	.L452
	mov	r3, #0
	ldr	ip, .L460
	movw	r8, #4042
	ldr	r4, .L460+4
	movw	r7, #4038
	b	.L453
.L458:
	add	r6, r3, #1
	ldr	r1, [r4, #-2080]
	mov	r2, r3
	uxth	r6, r6
	mov	r0, r6
	b	.L454
.L456:
	ldr	r9, [r1, r2, asl #2]
	ldr	sl, [r1, r0, asl #2]
	cmp	r9, sl
	movhi	r2, r0
	add	r0, r0, #1
	uxth	r0, r0
.L454:
	cmp	r0, r5
	bcc	.L456
	cmp	r3, r2
	beq	.L457
	ldr	r5, [r1, r3, asl #2]
	ldr	r0, [r1, r2, asl #2]
	str	r5, [r1, r2, asl #2]
	ldr	r1, [r4, #-2080]
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
.L457:
	mov	r3, r6
.L453:
	ldrh	r5, [ip, r8]
	sub	r2, r5, #1
	cmp	r3, r2
	blt	.L458
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, sl, pc}
.L461:
	.align	2
.L460:
	.word	.LANCHOR0
	.word	.LANCHOR2
	.fnend
	.size	FtlFreeSysBLkSort, .-FtlFreeSysBLkSort
	.align	2
	.global	IsInFreeQueue
	.type	IsInFreeQueue, %function
IsInFreeQueue:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	ldr	r2, .L468
	movw	r3, #4042
	stmfd	sp!, {r4, lr}
	.save {r4, lr}
	ldrh	ip, [r2, r3]
	cmp	ip, #1024
	beq	.L466
	movw	r3, #4038
	ldrh	r4, [r2, r3]
	mov	r3, #0
	b	.L464
.L465:
	add	r1, r3, r4
	mov	r1, r1, asl #22
	add	r1, r2, r1, lsr #21
	add	r1, r1, #4032
	add	r1, r1, #12
	ldrh	r1, [r1, #0]
	cmp	r1, r0
	beq	.L467
	add	r3, r3, #1
.L464:
	cmp	r3, ip
	bcc	.L465
.L466:
	mov	r0, #0
	ldmfd	sp!, {r4, pc}
.L467:
	mov	r0, #1
	ldmfd	sp!, {r4, pc}
.L469:
	.align	2
.L468:
	.word	.LANCHOR0
	.fnend
	.size	IsInFreeQueue, .-IsInFreeQueue
	.align	2
	.global	FtlFreeSysBlkQueueOut
	.type	FtlFreeSysBlkQueueOut, %function
FtlFreeSysBlkQueueOut:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	@ link register save eliminated.
	ldr	r2, .L472
	movw	r3, #4042
	ldrh	r3, [r2, r3]
	cmp	r3, #0
	beq	.L471
	b	FtlFreeSysBlkQueueOut.part.5
.L471:
	movw	r0, #65535
	bx	lr
.L473:
	.align	2
.L472:
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
	ldr	r2, .L489
	movw	r3, #3848
	stmfd	sp!, {r4, r5, r6, r7, r8, r9, sl, fp, lr}
	.save {r4, r5, r6, r7, r8, r9, sl, fp, lr}
	.pad #20
	sub	sp, sp, #20
	ldrh	r3, [r2, r3]
	cmp	r3, r0
	bls	.L475
	ldr	r1, .L489+4
	mov	ip, #6
	mul	ip, ip, r0
	mvn	r6, #0
	ldr	r4, [r1, #-2076]
	mov	r5, r1
	add	r2, r4, ip
	strh	r6, [r2, #2]	@ movhi
	strh	r6, [r4, ip]	@ movhi
	ldr	r3, [r1, #-2072]
	cmp	r3, #0
	beq	.L488
.L476:
	ldr	r7, [r1, #-2068]
	mov	r8, r0, asl #1
	ldrh	r1, [r2, #4]
	ldr	r9, .L489
	ldrh	sl, [r7, r8]
	cmp	r1, #0
	ldr	fp, [r5, #-2088]
	mulne	r6, r1, sl
	ldr	r1, [r5, #-2076]
	ldr	sl, .L489+8
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
.L483:
	add	r5, r5, #1
	ldr	r8, [sp, #8]
	uxth	r5, r5
	cmp	r5, r8
	bhi	.L475
	cmp	r0, r1
	beq	.L475
	mov	r9, r1, asl #1
	ldrh	r8, [r3, #4]
	ldrh	sl, [r7, r9]
	cmp	r8, #0
	mvneq	r8, #0
	mulne	r8, r8, sl
	cmp	r8, r6
	bne	.L479
	ldrh	sl, [fp, r9]
	ldr	r9, [sp, #12]
	ldrh	r8, [r9, #0]
	cmp	sl, r8
	bcc	.L481
	b	.L480
.L479:
	bhi	.L480
.L481:
	ldrh	r8, [r3, #0]
	movw	sl, #65535
	cmp	r8, sl
	streqh	r1, [r2, #2]	@ movhi
	streqh	r0, [r3, #0]	@ movhi
	ldreq	r3, .L489+4
	streq	r2, [r3, #-2064]
	beq	.L475
.L482:
	ldr	r9, [sp, #4]
	mov	r1, #6
	mla	r3, r1, r8, r9
	mov	r1, r8
	b	.L483
.L480:
	strh	r1, [r4, ip]	@ movhi
	ldrh	r1, [r3, #2]
	strh	r1, [r2, #2]	@ movhi
	ldr	r1, .L489+4
	ldr	ip, [r1, #-2072]
	cmp	r3, ip
	bne	.L484
	strh	r0, [r3, #2]	@ movhi
.L488:
	str	r2, [r1, #-2072]
	b	.L475
.L484:
	ldrh	r2, [r3, #2]
	mov	ip, #6
	ldr	r1, [r1, #-2076]
	mul	r2, ip, r2
	strh	r0, [r1, r2]	@ movhi
	strh	r0, [r3, #2]	@ movhi
.L475:
	mov	r0, #0
	add	sp, sp, #20
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, sl, fp, pc}
.L490:
	.align	2
.L489:
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
	ldr	r2, .L492
	ldr	r3, .L492+4
	ldrh	r1, [r2, r3]
	add	r1, r1, #1
	strh	r1, [r2, r3]	@ movhi
	ldmfd	sp!, {r3, pc}
.L493:
	.align	2
.L492:
	.word	.LANCHOR2
	.word	-2060
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
	beq	.L495
	ldr	r1, .L502
	mov	r5, #6
	mul	ip, r5, r0
	mvn	r3, #0
	ldr	r4, [r1, #-2076]
	add	r2, r4, ip
	strh	r3, [r2, #2]	@ movhi
	strh	r3, [r4, ip]	@ movhi
	ldr	r3, [r1, #-2056]
	cmp	r3, #0
	beq	.L501
.L496:
	ldr	sl, [r1, #-2076]
	mov	r8, r0, asl #1
	ldr	r7, [r1, #-2088]
	rsb	r1, sl, r3
	ldr	r9, .L502+4
	mov	r1, r1, asr #1
	ldrh	r8, [r7, r8]
	mul	r1, r9, r1
	mov	r9, r5
	uxth	r1, r1
.L499:
	mov	r5, r1, asl #1
	ldrh	r5, [r7, r5]
	cmp	r5, r8
	bcs	.L497
	ldrh	r5, [r3, #0]
	cmp	r5, r6
	streqh	r1, [r2, #2]	@ movhi
	streqh	r0, [r3, #0]	@ movhi
	beq	.L495
.L498:
	mla	r3, r9, r5, sl
	mov	r1, r5
	b	.L499
.L497:
	ldrh	r5, [r3, #2]
	strh	r5, [r2, #2]	@ movhi
	strh	r1, [r4, ip]	@ movhi
	ldr	r1, .L502
	ldr	ip, [r1, #-2056]
	cmp	r3, ip
	bne	.L500
	strh	r0, [r3, #2]	@ movhi
.L501:
	str	r2, [r1, #-2056]
	b	.L495
.L500:
	ldrh	r2, [r3, #2]
	mov	ip, #6
	ldr	r1, [r1, #-2076]
	mul	r2, ip, r2
	strh	r0, [r1, r2]	@ movhi
	strh	r0, [r3, #2]	@ movhi
.L495:
	mov	r0, #0
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, sl, pc}
.L503:
	.align	2
.L502:
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
	ldr	r2, .L505
	ldr	r3, .L505+4
	ldrh	r1, [r2, r3]
	add	r1, r1, #1
	strh	r1, [r2, r3]	@ movhi
	ldmfd	sp!, {r3, pc}
.L506:
	.align	2
.L505:
	.word	.LANCHOR2
	.word	-2052
	.fnend
	.size	INSERT_FREE_LIST, .-INSERT_FREE_LIST
	.align	2
	.global	List_remove_node
	.type	List_remove_node, %function
List_remove_node:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	ldr	ip, .L512
	mov	r2, #6
	mul	r1, r2, r1
	ldr	r3, [ip, #-2076]
	stmfd	sp!, {r4, r5, r6, lr}
	.save {r4, r5, r6, lr}
	add	r5, r3, r1
	ldr	r4, [r0, #0]
	movw	r6, #65535
	cmp	r5, r4
	ldrh	r4, [r3, r1]
	bne	.L508
	cmp	r4, r6
	mlane	r2, r2, r4, r3
	moveq	r2, #0
	streq	r2, [r0, #0]
	strne	r2, [r0, #0]
	mvnne	r0, #0
	strneh	r0, [r2, #2]	@ movhi
	b	.L510
.L508:
	cmp	r4, r6
	ldrh	r0, [r5, #2]
	bne	.L511
	cmp	r0, r4
	mulne	r2, r2, r0
	mvnne	r0, #0
	strneh	r0, [r3, r2]	@ movhi
	b	.L510
.L511:
	mla	r4, r2, r4, r3
	strh	r0, [r4, #2]	@ movhi
	ldrh	r6, [r5, #2]
	ldrh	r4, [r3, r1]
	ldr	r0, [ip, #-2076]
	mul	r2, r2, r6
	strh	r4, [r0, r2]	@ movhi
.L510:
	mvn	r2, #0
	mov	r0, #0
	strh	r2, [r3, r1]	@ movhi
	strh	r2, [r5, #2]	@ movhi
	ldmfd	sp!, {r4, r5, r6, pc}
.L513:
	.align	2
.L512:
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
	beq	.L519
	ldr	r2, .L520
	movw	lr, #65535
	mov	ip, #6
	ldr	r4, [r2, #-2076]
	b	.L516
.L518:
	mla	r3, ip, r2, r4
	sub	r1, r1, #1
	uxth	r1, r1
.L516:
	cmp	r1, #0
	beq	.L517
	ldrh	r2, [r3, #0]
	cmp	r2, lr
	bne	.L518
.L517:
	rsb	r4, r4, r3
	ldr	r3, .L520+4
	mov	r4, r4, asr #1
	mul	r4, r3, r4
	uxth	r4, r4
	mov	r1, r4
	bl	List_remove_node
	mov	r0, r4
	ldmfd	sp!, {r4, pc}
.L519:
	movw	r0, #65535
	ldmfd	sp!, {r4, pc}
.L521:
	.align	2
.L520:
	.word	.LANCHOR2
	.word	-1431655765
	.fnend
	.size	List_pop_index_node, .-List_pop_index_node
	.align	2
	.global	List_pop_head_node
	.type	List_pop_head_node, %function
List_pop_head_node:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	@ link register save eliminated.
	mov	r1, #0
	b	List_pop_index_node
	.fnend
	.size	List_pop_head_node, .-List_pop_head_node
	.align	2
	.global	List_get_gc_head_node
	.type	List_get_gc_head_node, %function
List_get_gc_head_node:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	@ link register save eliminated.
	ldr	r2, .L529
	ldr	r3, [r2, #-2072]
	cmp	r3, #0
	beq	.L528
	ldr	r2, [r2, #-2076]
	movw	r1, #65535
	mov	ip, #6
	b	.L525
.L527:
	mla	r3, ip, r3, r2
	sub	r0, r0, #1
	uxth	r0, r0
.L525:
	cmp	r0, #0
	beq	.L526
	ldrh	r3, [r3, #0]
	cmp	r3, r1
	bne	.L527
	mov	r0, r1
	bx	lr
.L526:
	rsb	r3, r2, r3
	ldr	r0, .L529+4
	mov	r3, r3, asr #1
	mul	r0, r0, r3
	uxth	r0, r0
	bx	lr
.L528:
	movw	r0, #65535
	bx	lr
.L530:
	.align	2
.L529:
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
	ldr	r3, .L538
	ldr	r2, .L538+4
	ldrh	r2, [r3, r2]
	cmp	r2, r0
	beq	.L532
	ldr	r2, .L538+8
	ldrh	r2, [r3, r2]
	cmp	r2, r0
	beq	.L532
	ldr	r2, .L538+12
	ldrh	r2, [r3, r2]
	cmp	r2, r0
	beq	.L532
	mov	r5, #6
	ldr	r2, [r3, #-2076]
	mul	r5, r5, r0
	ldr	r1, [r3, #-2072]
	add	r0, r2, r5
	cmp	r0, r1
	beq	.L532
	ldr	ip, [r3, #-2068]
	mov	r3, r4, asl #1
	ldrh	r1, [r0, #4]
	ldrh	r3, [ip, r3]
	muls	r1, r1, r3
	ldrh	r3, [r0, #2]
	mvneq	r1, #0
	movw	r0, #65535
	cmp	r3, r0
	bne	.L534
	ldrh	r0, [r2, r5]
	cmp	r0, r3
	beq	.L532
.L534:
	mov	r0, #6
	mla	r3, r0, r3, r2
	ldr	r0, .L538+16
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
	bcs	.L532
	ldr	r0, .L538+20
	mov	r1, r4
	bl	List_remove_node
	ldr	r5, .L538
	ldr	r3, .L538+24
	mov	r0, r4
	ldrh	r2, [r5, r3]
	sub	r2, r2, #1
	strh	r2, [r5, r3]	@ movhi
	bl	INSERT_DATA_LIST
.L532:
	mov	r0, #0
	ldmfd	sp!, {r3, r4, r5, pc}
.L539:
	.align	2
.L538:
	.word	.LANCHOR2
	.word	-2048
	.word	-2000
	.word	-1952
	.word	-1431655765
	.word	.LANCHOR2-2072
	.word	-2060
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
	b	.L541
.L544:
	mov	r7, r3
	add	r3, r3, #2
	ldrh	r6, [r7, #0]
	cmp	r6, #0
	bne	.L542
	bl	FtlFreeSysBlkQueueOut
	cmp	r0, #0
	strh	r0, [r7, #0]	@ movhi
	beq	.L543
	ldr	r3, [r4, #28]
	strh	r6, [r4, #2]	@ movhi
	add	r3, r3, #1
	str	r3, [r4, #28]
	ldrh	r3, [r4, #8]
	strh	r5, [r4, #0]	@ movhi
	add	r3, r3, #1
	strh	r3, [r4, #8]	@ movhi
	b	.L543
.L542:
	add	r5, r5, #1
	uxth	r5, r5
.L541:
	cmp	r5, r2
	bne	.L544
.L543:
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
	ldr	r2, .L557
	movw	r3, #3946
	stmfd	sp!, {r4, r5, r6, r7, lr}
	.save {r4, r5, r6, r7, lr}
	movw	r1, #65535
	ldrh	r3, [r2, r3]
	ldr	r2, .L557+4
	ldr	ip, [r2, #-1904]
	mov	r2, #0
	mov	r0, r2
	b	.L546
.L548:
	add	r2, r2, #12
	add	r4, ip, r2
	ldrh	r4, [r4, #-12]
	cmp	r4, r1
	ldmeqfd	sp!, {r4, r5, r6, r7, pc}
	add	r0, r0, #1
	uxth	r0, r0
.L546:
	cmp	r0, r3
	bne	.L548
	mov	r1, #0
	mov	r0, r3
	mov	r5, #-2147483648
	mov	r2, r1
	b	.L549
.L551:
	add	r4, ip, r1
	ldr	r4, [r4, #4]
	cmp	r4, #0
	blt	.L550
	cmp	r4, r5
	movcc	r5, r4
	movcc	r0, r2
.L550:
	add	r2, r2, #1
	add	r1, r1, #12
	uxth	r2, r2
.L549:
	cmp	r2, r3
	bne	.L551
	cmp	r0, r3
	ldmccfd	sp!, {r4, r5, r6, r7, pc}
	ldr	r2, .L557+8
	mov	r0, r3
	ldr	r1, .L557+4
	mvn	r4, #0
	ldrh	r6, [r1, r2]
	mov	r2, #0
	mov	r1, r2
	b	.L552
.L554:
	add	r5, ip, r2
	ldr	r5, [r5, #4]
	cmp	r5, r4
	bcs	.L553
	ldrh	r7, [ip, r2]
	cmp	r7, r6
	movne	r4, r5
	movne	r0, r1
.L553:
	add	r1, r1, #1
	add	r2, r2, #12
	uxth	r1, r1
.L552:
	cmp	r1, r3
	bne	.L554
	ldmfd	sp!, {r4, r5, r6, r7, pc}
.L558:
	.align	2
.L557:
	.word	.LANCHOR0
	.word	.LANCHOR2
	.word	-1900
	.fnend
	.size	select_l2p_ram_region, .-select_l2p_ram_region
	.align	2
	.global	FtlUpdateVaildLpn
	.type	FtlUpdateVaildLpn, %function
FtlUpdateVaildLpn:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	ldr	r3, .L565
	ldr	r2, .L565+4
	stmfd	sp!, {r4, r5, lr}
	.save {r4, r5, lr}
	ldrh	r1, [r2, r3]
	cmp	r1, #4
	add	ip, r1, #1
	mov	r1, r3
	strh	ip, [r2, r3]	@ movhi
	bhi	.L560
	cmp	r0, #0
	ldmeqfd	sp!, {r4, r5, pc}
.L560:
	ldr	r0, .L565+8
	mov	r3, #0
	strh	r3, [r2, r1]	@ movhi
	movw	r1, #3848
	str	r3, [r2, #-1896]
	movw	ip, #65535
	ldrh	r4, [r0, r1]
	ldr	r1, [r2, #-2068]
	ldr	r2, .L565+4
	b	.L562
.L564:
	ldrh	r0, [r1], #2
	add	r3, r3, #1
	cmp	r0, ip
	uxth	r3, r3
	ldrne	r5, [r2, #-1896]
	addne	r0, r0, r5
	strne	r0, [r2, #-1896]
.L562:
	cmp	r3, r4
	bne	.L564
	ldmfd	sp!, {r4, r5, pc}
.L566:
	.align	2
.L565:
	.word	-1898
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
	ldr	r3, .L571
	mov	r2, r0, lsr #5
	cmp	r1, #0
	and	r0, r0, #31
	uxth	r2, r2
	mov	ip, #1
	ldr	r3, [r3, #-1892]
	ldr	r1, [r3, r2, asl #2]
	orrne	r0, r1, ip, asl r0
	biceq	r0, r1, ip, asl r0
	str	r0, [r3, r2, asl #2]
	bx	lr
.L572:
	.align	2
.L571:
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
	ldr	r3, .L574
	mov	r2, r0, lsr #5
	and	r0, r0, #31
	ldr	r3, [r3, #-1892]
	ldr	r3, [r3, r2, asl #2]
	mov	r0, r3, lsr r0
	and	r0, r0, #1
	bx	lr
.L575:
	.align	2
.L574:
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
	ldr	ip, .L583
	mov	r3, #0
	strh	r3, [r0, #4]	@ movhi
	mov	r3, #3840
	stmfd	sp!, {r4, r5, r6, lr}
	.save {r4, r5, r6, lr}
	ldrh	r3, [ip, r3]
	movw	ip, #65535
	b	.L577
.L579:
	add	r4, r0, r2, asl #1
	add	r2, r2, #1
	ldrh	r4, [r4, #16]
	uxth	r2, r2
	cmp	r4, ip
	ldrneh	r4, [r0, #4]
	addne	r4, r4, #1
	strneh	r4, [r0, #4]	@ movhi
.L577:
	cmp	r2, r3
	bcc	.L579
	ldr	ip, .L583
	movw	r2, #3908
	movw	r4, #65535
	mvn	r1, r1
	ldrh	r5, [ip, r2]
	mov	ip, r0
	mov	r2, #0
	b	.L580
.L582:
	ldrh	r6, [ip, #16]
	add	r2, r2, #1
	add	ip, ip, #2
	cmp	r6, r4
	uxth	r2, r2
	ldrneh	r6, [r0, #4]
	addne	r6, r5, r6
	addne	r6, r6, r1
	strneh	r6, [r0, #4]	@ movhi
.L580:
	cmp	r2, r3
	bne	.L582
	ldmfd	sp!, {r4, r5, r6, pc}
.L584:
	.align	2
.L583:
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
	ldr	r6, .L591
	strh	r5, [r0, #4]	@ movhi
	mov	r8, #3840
	strb	r5, [r4, #7]
	mvn	r7, #0
	b	.L586
.L588:
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
.L586:
	ldrh	r3, [r6, r8]
	cmp	r3, r5
	bhi	.L588
	ldr	r1, .L591
	movw	r2, #3908
	ldrb	r3, [r4, #7]	@ zero_extendqisi2
	ldrh	r2, [r1, r2]
	mul	r3, r2, r3
	strh	r3, [r4, #4]	@ movhi
	mov	r3, #0
	strb	r3, [r4, #9]
	ldr	r3, .L591+4
	ldr	r2, [r3, #-1888]
	cmp	r2, #0
	beq	.L589
	ldrh	r1, [r4, #0]
	ldr	r2, [r3, #-2088]
	mov	r3, r1, asl #1
	ldrh	r3, [r2, r3]
	cmp	r3, #59
	movls	r3, #1
	strlsb	r3, [r4, #9]
.L589:
	ldr	r3, .L591
	mov	r0, #0
	ldrb	r3, [r3, #928]	@ zero_extendqisi2
	cmp	r3, #0
	movne	r3, #1
	strneb	r3, [r4, #9]
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, sl, pc}
.L592:
	.align	2
.L591:
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
	ldr	r6, .L598
	mov	sl, #3840
	movw	r8, #3908
	b	.L594
.L596:
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
.L594:
	ldrh	r3, [r6, sl]
	cmp	r3, r5
	bhi	.L596
	cmp	r4, #0
	beq	.L597
	mov	r1, r4
	mov	r0, #32768
	bl	__aeabi_idiv
	uxth	r4, r0
.L597:
	ldr	r3, .L598+4
	mov	r2, #6
	mov	r0, #0
	ldr	r3, [r3, #-2076]
	mla	r7, r2, r7, r3
	strh	r4, [r7, #4]	@ movhi
	ldmfd	sp!, {r3, r4, r5, r6, r7, r8, sl, pc}
.L599:
	.align	2
.L598:
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
	ldr	r3, .L603
	ldr	r0, [r3, #-2056]
	cmp	r0, #0
	bxeq	lr
	ldr	r2, [r3, #-2076]
	rsb	r0, r2, r0
	ldr	r2, .L603+4
	mov	r0, r0, asr #1
	mul	r0, r2, r0
	ldr	r2, [r3, #-2088]
	uxth	r0, r0
	mov	r3, r0, asl #1
	ldrh	r0, [r2, r3]
	bx	lr
.L604:
	.align	2
.L603:
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
	ldr	r2, .L612
	stmfd	sp!, {r4, r5, lr}
	.save {r4, r5, lr}
	ldr	r3, [r2, #-2056]
	cmp	r3, #0
	beq	.L611
	ldr	r1, .L612+4
	mov	ip, #7
	mov	r5, #6
	movw	r4, #65535
	ldrh	r1, [r2, r1]
	mul	r1, ip, r1
	mov	r1, r1, asr #3
	cmp	r0, r1
	uxthgt	r0, r1
	ldr	r1, [r2, #-2076]
	ldr	r2, .L612+8
	rsb	r3, r1, r3
	mov	r3, r3, asr #1
	mul	r3, r2, r3
	mov	r2, #0
	uxth	r3, r3
	b	.L608
.L610:
	mul	ip, r5, r3
	ldrh	ip, [r1, ip]
	cmp	ip, r4
	beq	.L609
	add	r2, r2, #1
	mov	r3, ip
	uxth	r2, r2
.L608:
	cmp	r2, r0
	bne	.L610
.L609:
	ldr	r2, .L612
	mov	r3, r3, asl #1
	ldr	r2, [r2, #-2088]
	ldrh	r0, [r2, r3]
	ldmfd	sp!, {r4, r5, pc}
.L611:
	mov	r0, r3
	ldmfd	sp!, {r4, r5, pc}
.L613:
	.align	2
.L612:
	.word	.LANCHOR2
	.word	-2052
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
	ldr	r6, .L624
	add	r5, r8, #12
	.pad #32
	sub	sp, sp, #32
	ldr	r1, .L624+4
	bl	strcpy
	mov	r0, r5
	ldr	r1, .L624+8
	ldr	r2, [r6, #3048]
	bl	sprintf
	ldr	r1, .L624+12
	ldr	r2, [r6, #3924]
	add	r5, r5, r0
	mov	r0, r5
	bl	sprintf
	ldr	r3, .L624+16
	ldr	r3, [r3, #2932]
	cmp	r3, #1
	add	r5, r5, r0
	bne	.L620
.L615:
	add	r0, sp, #16
	add	r1, sp, #20
	add	r2, sp, #24
	add	r3, sp, #28
	bl	NandcGetTimeCfg
	ldr	r3, [sp, #24]
	ldr	r2, [sp, #16]
	mov	r0, r5
	ldr	r1, .L624+20
	str	r3, [sp, #0]
	ldr	r3, [sp, #28]
	ldr	r4, .L624+24
	ldr	r7, .L624+28
	str	r3, [sp, #4]
	ldr	r3, [sp, #20]
	bl	sprintf
	ldr	r1, .L624+32
	add	r5, r5, r0
	mov	r0, r5
	add	r5, r5, #10
	bl	strcpy
	ldr	r2, [r6, #3968]
	mov	r0, r5
	ldr	r1, .L624+36
	bl	sprintf
	ldr	r1, .L624+40
	ldr	r2, [r4, #-1896]
	add	r5, r5, r0
	mov	r0, r5
	bl	sprintf
	ldr	r1, .L624+44
	ldr	r2, [r4, #-1884]
	add	r5, r5, r0
	mov	r0, r5
	bl	sprintf
	ldr	r1, .L624+48
	ldr	r2, [r4, #-1880]
	add	r5, r5, r0
	mov	r0, r5
	bl	sprintf
	ldr	r1, .L624+52
	ldr	r2, [r4, #-1876]
	add	r5, r5, r0
	mov	r0, r5
	bl	sprintf
	ldr	r1, .L624+56
	ldr	r2, [r4, #-1872]
	add	r5, r5, r0
	mov	r0, r5
	bl	sprintf
	ldr	r1, .L624+60
	ldr	r2, [r4, #-1868]
	add	r5, r5, r0
	mov	r0, r5
	bl	sprintf
	ldr	r1, .L624+64
	ldr	r2, [r4, #-1864]
	add	r5, r5, r0
	mov	r0, r5
	bl	sprintf
	ldr	r2, [r4, #-1860]
	ldr	r1, .L624+68
	mov	r2, r2, lsr #11
	add	r5, r5, r0
	mov	r0, r5
	bl	sprintf
	ldr	r2, [r4, #-1856]
	ldr	r1, .L624+72
	mov	r2, r2, lsr #11
	add	r5, r5, r0
	mov	r0, r5
	bl	sprintf
	ldr	r1, .L624+76
	ldr	r2, [r4, #-1852]
	add	r5, r5, r0
	mov	r0, r5
	bl	sprintf
	ldr	r1, .L624+80
	ldr	r2, [r4, #-1848]
	add	r5, r5, r0
	mov	r0, r5
	bl	sprintf
	add	r5, r5, r0
	bl	FtlBbtCalcTotleCnt
	movw	r2, #3982
	ldr	r1, .L624+84
	ldrh	r2, [r6, r2]
	mov	r3, r0
	mov	r0, r5
	bl	sprintf
	ldr	r1, .L624+88
	ldrh	r2, [r4, r7]
	add	r5, r5, r0
	mov	r0, r5
	bl	sprintf
	ldr	r1, .L624+92
	ldr	r2, [r4, #-1844]
	add	r5, r5, r0
	mov	r0, r5
	bl	sprintf
	ldr	r1, .L624+96
	ldr	r2, [r4, #-1840]
	add	r5, r5, r0
	mov	r0, r5
	bl	sprintf
	ldr	r1, .L624+100
	ldr	r2, [r4, #-1836]
	add	r5, r5, r0
	mov	r0, r5
	bl	sprintf
	ldr	r1, .L624+104
	ldr	r2, [r4, #-2084]
	add	r5, r5, r0
	mov	r0, r5
	bl	sprintf
	ldr	r1, .L624+108
	ldr	r2, [r4, #-1832]
	add	r5, r5, r0
	mov	r0, r5
	bl	sprintf
	ldr	r1, .L624+112
	ldr	r2, [r4, #-1828]
	add	r5, r5, r0
	mov	r0, r5
	bl	sprintf
	ldr	r3, .L624+116
	ldr	r1, .L624+120
	ldrh	r2, [r4, r3]
	add	r5, r5, r0
	mov	r0, r5
	bl	sprintf
	ldr	r3, .L624+124
	ldr	r1, .L624+128
	ldrh	r2, [r4, r3]
	add	r5, r5, r0
	mov	r0, r5
	bl	sprintf
	ldr	r2, [r6, #3948]
	ldr	r1, .L624+132
	add	r5, r5, r0
	mov	r0, r5
	bl	sprintf
	ldr	r2, [r6, #3940]
	ldr	r1, .L624+136
	add	r5, r5, r0
	mov	r0, r5
	bl	sprintf
	ldr	r2, [r6, #3836]
	ldr	r1, .L624+140
	add	r5, r5, r0
	mov	r0, r5
	bl	sprintf
	movw	r3, #4042
	ldrh	r2, [r6, r3]
	ldr	r1, .L624+144
	add	r5, r5, r0
	mov	r0, r5
	bl	sprintf
	movw	r3, #3848
	ldrh	r2, [r6, r3]
	ldr	r1, .L624+148
	add	r5, r5, r0
	mov	r0, r5
	bl	sprintf
	ldr	r3, .L624+152
	ldr	r1, .L624+156
	ldrh	r2, [r4, r3]
	add	r5, r5, r0
	mov	r0, r5
	bl	sprintf
	ldr	r2, [r6, #3852]
	ldr	r1, .L624+160
	add	r5, r5, r0
	mov	r0, r5
	bl	sprintf
	ldr	r3, .L624+164
	ldr	r1, .L624+168
	ldrh	r2, [r4, r3]
	add	r5, r5, r0
	mov	r0, r5
	bl	sprintf
	movw	r3, #3976
	ldrh	r2, [r6, r3]
	ldr	r1, .L624+172
	ldr	r6, .L624+176
	add	r5, r5, r0
	mov	r0, r5
	bl	sprintf
	ldr	r3, .L624+180
	ldr	r1, .L624+184
	ldrh	r2, [r4, r3]
	add	r5, r5, r0
	mov	r0, r5
	bl	sprintf
	ldr	r1, .L624+188
	ldrb	r2, [r4, #-2042]	@ zero_extendqisi2
	add	r5, r5, r0
	mov	r0, r5
	bl	sprintf
	ldrh	r2, [r4, r6]
	ldr	r1, .L624+192
	add	r5, r5, r0
	mov	r0, r5
	bl	sprintf
	ldr	r1, .L624+196
	ldrb	r2, [r4, #-2040]	@ zero_extendqisi2
	add	r5, r5, r0
	mov	r0, r5
	bl	sprintf
	ldr	r3, .L624+200
	ldr	r1, .L624+204
	ldrh	r2, [r4, r3]
	add	r5, r5, r0
	mov	r0, r5
	bl	sprintf
	ldrh	r3, [r4, r6]
	ldr	r2, [r4, #-2068]
	add	r6, r6, #48
	ldr	r1, .L624+208
	mov	r3, r3, asl #1
	ldrh	r2, [r2, r3]
	add	r5, r5, r0
	mov	r0, r5
	bl	sprintf
	ldr	r3, .L624+212
	ldr	r1, .L624+216
	ldrh	r2, [r4, r3]
	add	r5, r5, r0
	mov	r0, r5
	bl	sprintf
	ldr	r1, .L624+220
	ldrb	r2, [r4, #-1994]	@ zero_extendqisi2
	add	r5, r5, r0
	mov	r0, r5
	bl	sprintf
	ldrh	r2, [r4, r6]
	ldr	r1, .L624+224
	add	r5, r5, r0
	mov	r0, r5
	bl	sprintf
	ldr	r1, .L624+228
	ldrb	r2, [r4, #-1992]	@ zero_extendqisi2
	add	r5, r5, r0
	mov	r0, r5
	bl	sprintf
	ldr	r3, .L624+232
	ldr	r1, .L624+236
	ldrh	r2, [r4, r3]
	add	r5, r5, r0
	mov	r0, r5
	bl	sprintf
	ldrh	r3, [r4, r6]
	ldr	r2, [r4, #-2068]
	add	r6, r6, #240
	ldr	r1, .L624+240
	mov	r3, r3, asl #1
	ldrh	r2, [r2, r3]
	add	r5, r5, r0
	mov	r0, r5
	bl	sprintf
	ldr	r3, .L624+244
	ldr	r1, .L624+248
	ldrh	r2, [r4, r3]
	add	r5, r5, r0
	mov	r0, r5
	bl	sprintf
	ldr	r1, .L624+252
	ldrb	r2, [r4, #-1946]	@ zero_extendqisi2
	add	r5, r5, r0
	mov	r0, r5
	bl	sprintf
	ldr	r3, .L624+256
	ldr	r1, .L624+260
	ldrh	r2, [r4, r3]
	add	r5, r5, r0
	mov	r0, r5
	bl	sprintf
	ldr	r1, .L624+264
	ldrb	r2, [r4, #-1944]	@ zero_extendqisi2
	add	r5, r5, r0
	mov	r0, r5
	bl	sprintf
	ldr	r3, .L624+268
	ldr	r1, .L624+272
	ldrh	r2, [r4, r3]
	add	r5, r5, r0
	mov	r0, r5
	bl	sprintf
	ldr	r3, .L624+276
	ldr	r1, .L624+280
	ldrh	r2, [r4, r3]
	add	r5, r5, r0
	mov	r0, r5
	bl	sprintf
	ldr	r1, .L624+284
	ldrb	r2, [r4, #-1754]	@ zero_extendqisi2
	add	r5, r5, r0
	mov	r0, r5
	bl	sprintf
	ldr	r1, .L624+288
	ldrh	r2, [r4, r6]
	add	r5, r5, r0
	mov	r0, r5
	bl	sprintf
	ldr	r1, .L624+292
	ldrb	r2, [r4, #-1752]	@ zero_extendqisi2
	add	r5, r5, r0
	mov	r0, r5
	bl	sprintf
	ldr	r3, .L624+296
	ldr	r1, .L624+300
	ldrh	r2, [r4, r3]
	add	r5, r5, r0
	mov	r0, r5
	bl	sprintf
	ldr	r1, [r4, #-1624]
	ldr	r3, [r4, #-1888]
	ldr	r2, [r4, #-1712]
	str	r1, [sp, #0]
	ldr	r1, [r4, #-1632]
	orr	r2, r3, r2, asl #8
	str	r1, [sp, #4]
	ldr	r1, .L624+304
	ldr	r3, [r4, #-1628]
	add	r5, r5, r0
	mov	r0, r5
	bl	sprintf
	ldr	r1, .L624+308
	ldr	r2, [r4, #-1636]
	add	r5, r5, r0
	mov	r0, r5
	bl	sprintf
	ldr	r1, .L624+312
	ldr	r2, [r4, #-1612]
	add	r5, r5, r0
	mov	r0, r5
	bl	sprintf
	ldr	r3, .L624+316
	ldr	r1, .L624+320
	ldrh	r2, [r4, r3]
	add	r5, r5, r0
	mov	r0, r5
	bl	sprintf
	ldr	r3, .L624+324
	ldr	r1, .L624+328
	ldrh	r2, [r4, r3]
	add	r5, r5, r0
	mov	r0, r5
	bl	sprintf
	ldr	r1, .L624+332
	ldr	r2, [r4, #-1192]
	add	r5, r5, r0
	mov	r0, r5
	bl	sprintf
	ldr	r3, .L624+336
	ldr	r1, .L624+340
	ldrh	r2, [r4, r3]
	add	r5, r5, r0
	mov	r0, r5
	bl	sprintf
	add	r5, r5, r0
	bl	GetFreeBlockMinEraseCount
	ldr	r1, .L624+344
	mov	r2, r0
	mov	r0, r5
	bl	sprintf
	add	r5, r5, r0
	ldrh	r0, [r4, r7]
	bl	GetFreeBlockMaxEraseCount
	ldr	r1, .L624+348
	mov	r2, r0
	mov	r0, r5
	bl	sprintf
	ldrh	r3, [r4, r6]
	movw	r2, #65535
	cmp	r3, r2
	add	r5, r5, r0
	beq	.L617
	ldr	r2, [r4, #-2068]
	mov	r3, r3, asl #1
	mov	r0, r5
	ldr	r1, .L624+352
	ldrh	r2, [r2, r3]
	bl	sprintf
	add	r5, r5, r0
.L617:
	mov	r0, #0
	ldr	r4, .L624+24
	bl	List_get_gc_head_node
	mov	r6, #0
	movw	sl, #65535
	mov	r9, #6
	uxth	r3, r0
.L619:
	cmp	r3, sl
	beq	.L618
	ldr	r1, [r4, #-2068]
	mov	r2, r3, asl #1
	mul	r7, r9, r3
	mov	r0, r5
	ldrh	r1, [r1, r2]
	str	r1, [sp, #0]
	ldr	r1, [r4, #-2076]
	add	r1, r1, r7
	ldrh	r1, [r1, #4]
	str	r1, [sp, #4]
	ldr	r1, [r4, #-2088]
	ldrh	r2, [r1, r2]
	ldr	r1, .L624+356
	str	r2, [sp, #8]
	mov	r2, r6
	bl	sprintf
	add	r6, r6, #1
	ldr	r3, [r4, #-2076]
	cmp	r6, #16
	ldrh	r3, [r3, r7]
	add	r5, r5, r0
	bne	.L619
.L618:
	ldr	r6, .L624+24
	mov	r4, #0
	movw	sl, #65535
	mov	r9, #6
	ldr	r2, [r6, #-2056]
	ldr	r3, [r6, #-2076]
	rsb	r3, r3, r2
	ldr	r2, .L624+360
	mov	r3, r3, asr #1
	mul	r3, r2, r3
	uxth	r3, r3
.L621:
	cmp	r3, sl
	beq	.L620
	mul	r7, r9, r3
	ldr	r2, [r6, #-2076]
	mov	r0, r5
	add	r2, r2, r7
	ldrh	r2, [r2, #4]
	str	r2, [sp, #0]
	mov	r2, r3, asl #1
	ldr	r1, [r6, #-2088]
	ldrh	r2, [r1, r2]
	ldr	r1, .L624+364
	str	r2, [sp, #4]
	mov	r2, r4
	bl	sprintf
	add	r4, r4, #1
	ldr	r3, [r6, #-2076]
	cmp	r4, #4
	ldrh	r3, [r3, r7]
	add	r5, r5, r0
	bne	.L621
.L620:
	rsb	r0, r8, r5
	add	sp, sp, #32
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, sl, pc}
.L625:
	.align	2
.L624:
	.word	.LANCHOR0
	.word	.LC4
	.word	.LC5
	.word	.LC6
	.word	.LANCHOR1
	.word	.LC7
	.word	.LANCHOR2
	.word	-2052
	.word	.LC8
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
	.word	-1794
	.word	.LC29
	.word	-1796
	.word	.LC30
	.word	.LC31
	.word	.LC32
	.word	.LC33
	.word	.LC34
	.word	.LC35
	.word	-1776
	.word	.LC36
	.word	.LC37
	.word	-1772
	.word	.LC38
	.word	.LC39
	.word	-2048
	.word	-2046
	.word	.LC40
	.word	.LC41
	.word	.LC42
	.word	.LC43
	.word	-2044
	.word	.LC44
	.word	.LC45
	.word	-1998
	.word	.LC46
	.word	.LC47
	.word	.LC48
	.word	.LC49
	.word	-1996
	.word	.LC50
	.word	.LC51
	.word	-1950
	.word	.LC52
	.word	.LC53
	.word	-1952
	.word	.LC54
	.word	.LC55
	.word	-1948
	.word	.LC56
	.word	-1758
	.word	.LC57
	.word	.LC58
	.word	.LC59
	.word	.LC60
	.word	-1756
	.word	.LC61
	.word	.LC62
	.word	.LC63
	.word	.LC64
	.word	-1196
	.word	.LC65
	.word	-1194
	.word	.LC66
	.word	.LC67
	.word	-1188
	.word	.LC68
	.word	.LC69
	.word	.LC70
	.word	.LC71
	.word	.LC72
	.word	-1431655765
	.word	.LC73
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
	ldr	r1, .L627
	ldr	r2, .L627+4
	bl	sprintf
	add	r5, r4, r0
	mov	r0, r5
	bl	FtlPrintInfo2buf
	add	r0, r5, r0
	rsb	r0, r4, r0
	ldmfd	sp!, {r3, r4, r5, pc}
.L628:
	.align	2
.L627:
	.word	.LC74
	.word	.LC75
	.fnend
	.size	rknand_proc_ftlread, .-rknand_proc_ftlread
	.align	2
	.global	GetSwlReplaceBlock
	.type	GetSwlReplaceBlock, %function
GetSwlReplaceBlock:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	ldr	r3, .L657
	stmfd	sp!, {r4, r5, r6, r7, r8, r9, sl, fp, lr}
	.save {r4, r5, r6, r7, r8, r9, sl, fp, lr}
	.pad #20
	sub	sp, sp, #20
	ldr	r1, [r3, #-1836]
	ldr	r2, [r3, #-1828]
	cmp	r1, r2
	bcs	.L630
	ldr	r0, .L657+4
	movw	r1, #3848
	mov	r2, #0
	str	r2, [r3, #-1844]
	ldrh	r1, [r0, r1]
	ldr	r0, [r3, #-2088]
	b	.L631
.L632:
	ldrh	lr, [r0], #2
	add	r2, r2, #1
	ldr	ip, [r3, #-1844]
	add	ip, lr, ip
	str	ip, [r3, #-1844]
.L631:
	cmp	r2, r1
	ldr	r4, .L657
	bcc	.L632
	ldr	r5, [r4, #-1844]
	mov	r0, r5
	bl	__aeabi_uidiv
	ldr	r2, .L657+4
	movw	r3, #3898
	ldrh	r1, [r2, r3]
	str	r0, [r4, #-1836]
	ldr	r0, [r4, #-1840]
	rsb	r0, r0, r5
	bl	__aeabi_uidiv
	str	r0, [r4, #-1844]
	b	.L633
.L630:
	ldr	r2, [r3, #-1832]
	cmp	r1, r2
	bls	.L633
	add	r2, r2, #1
	ldr	r4, .L657+4
	str	r2, [r3, #-1832]
	movw	ip, #3848
	mov	r2, #0
	b	.L634
.L635:
	ldr	r0, [r3, #-2088]
	mov	r1, r2, asl #1
	add	r2, r2, #1
	ldrh	r5, [r0, r1]
	add	r5, r5, #1
	strh	r5, [r0, r1]	@ movhi
.L634:
	ldrh	r1, [r4, ip]
	cmp	r2, r1
	bcc	.L635
.L633:
	ldr	r3, .L657
	ldr	r6, [r3, #-1828]
	ldr	r5, [r3, #-1836]
	add	r2, r6, #256
	cmp	r2, r5
	mov	r2, r3
	bls	.L636
	ldr	r1, [r3, #-1832]
	add	r0, r6, #768
	cmp	r0, r1
	bls	.L636
	ldr	r3, [r3, #-1888]
	cmp	r3, #0
	beq	.L655
	cmp	r6, #30
	bhi	.L655
.L636:
	ldr	r3, .L657+8
	ldrh	r0, [r2, r3]
	add	r0, r0, r0, asl #1
	ubfx	r0, r0, #2, #16
	bl	GetFreeBlockMaxEraseCount
	add	r3, r6, #64
	cmp	r0, r3
	mov	r8, r0
	bcs	.L638
	cmp	r6, #30
	bhi	.L655
.L638:
	ldr	r2, .L657
	ldr	r3, [r2, #-2072]
	cmp	r3, #0
	beq	.L655
	ldr	r0, .L657+4
	movw	r1, #3848
	movw	r7, #65535
	mov	r4, r7
	mov	fp, r7
	ldrh	sl, [r0, r1]
	ldr	r0, [r2, #-2076]
	ldr	r1, [r2, #-2088]
	mov	r2, #0
	b	.L639
.L642:
	add	r2, r2, #1
	uxth	r2, r2
	cmp	r2, sl
	bhi	.L655
	ldrh	ip, [r3, #4]
	cmp	ip, #0
	beq	.L640
	rsb	r3, r0, r3
	ldr	ip, .L657+12
	mov	r3, r3, asr #1
	mul	r3, ip, r3
	uxth	r3, r3
	mov	ip, r3, asl #1
	ldrh	ip, [r1, ip]
	cmp	ip, r6
	bls	.L652
	cmp	ip, r7
	movcc	r7, ip
	movcc	r4, r3
.L640:
	mov	ip, #6
	mla	r3, ip, r9, r0
.L639:
	ldrh	r9, [r3, #0]
	cmp	r9, fp
	bne	.L642
	b	.L641
.L652:
	mov	r4, r3
.L641:
	movw	r3, #65535
	cmp	r4, r3
	beq	.L637
	mov	sl, r4, asl #1
	ldr	fp, .L657
	ldrh	r9, [r1, sl]
	cmp	r9, r6
	bls	.L643
	bl	GetFreeBlockMinEraseCount
	cmp	r0, r6
	strhi	r7, [fp, #-1828]
.L643:
	cmp	r9, #29
	ldr	r2, [fp, #-1888]
	movhi	r3, #0
	movls	r3, #1
	cmp	r2, #0
	moveq	r3, #0
	cmp	r3, #0
	beq	.L644
	add	r3, r9, #10
	cmp	r3, r5
	bls	.L645
	ldr	r2, .L657+4
	movw	r3, #3898
	ldrh	r3, [r2, r3]
	cmp	r3, r9
	bls	.L644
.L645:
	ldr	r6, .L657
	ldr	r3, .L657+16
	ldrh	r3, [r6, r3]
	cmp	r3, #64
	bls	.L644
	ldr	r3, [r6, #-2088]
	mov	r1, r4
	ldr	r0, .L657+20
	ldrh	r2, [r3, sl]
	bl	printk
	b	.L656
.L644:
	cmp	r9, r5
	bcs	.L655
	add	r3, r9, #128
	cmp	r8, r3
	ble	.L655
	add	r3, r9, #256
	ldr	r6, .L657
	cmp	r3, r5
	bcc	.L646
	ldr	r3, [r6, #-1832]
	add	r9, r9, #768
	cmp	r9, r3
	bcs	.L655
.L646:
	ldr	r3, [r6, #-2068]
	mov	r1, r4
	ldr	r0, .L657+24
	mov	r2, r5
	ldrh	r3, [r3, sl]
	str	r3, [sp, #0]
	ldr	r3, [r6, #-2088]
	ldrh	r3, [r3, sl]
	stmib	sp, {r3, r8}
	ldr	r3, [r6, #-1832]
	bl	printk
.L656:
	mov	r3, #1
	str	r3, [r6, #-1184]
	b	.L637
.L655:
	movw	r4, #65535
.L637:
	mov	r0, r4
	add	sp, sp, #20
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, sl, fp, pc}
.L658:
	.align	2
.L657:
	.word	.LANCHOR2
	.word	.LANCHOR0
	.word	-2052
	.word	-1431655765
	.word	-2060
	.word	.LC76
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
	beq	.L660
	ldr	r2, .L661
	mov	r3, r0, asl #1
	mov	r1, #0
	ldr	r2, [r2, #-2068]
	strh	r1, [r2, r3]	@ movhi
	bl	INSERT_FREE_LIST
.L660:
	mov	r0, #0
	ldmfd	sp!, {r3, pc}
.L662:
	.align	2
.L661:
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
	ldr	r2, .L671
	mov	r3, #0
	stmfd	sp!, {r4, r5, r6, r7, r8, r9, sl, fp, lr}
	.save {r4, r5, r6, r7, r8, r9, sl, fp, lr}
	mov	sl, #3840
	str	r3, [r2, #-1180]
	mov	r8, #12
	ldr	r0, .L671+4
	mov	r7, #1
	movw	r6, #3918
	mov	fp, #3920
	b	.L664
.L665:
	mul	r1, r8, r3
	ldr	r4, [r2, #-1176]
	add	ip, r4, r1
	str	r7, [ip, #8]
	ldrh	ip, [r0, r6]
	mul	ip, ip, r3
	add	r5, ip, #3
	cmp	ip, #0
	movlt	ip, r5
	ldr	r5, [r2, #-1172]
	bic	ip, ip, #3
	add	ip, r5, ip
	str	ip, [r4, r1]
	ldrh	ip, [r0, fp]
	ldr	r9, [r2, #-1176]
	mul	ip, ip, r3
	add	r4, r9, r1
	add	r5, ip, #3
	cmp	ip, #0
	movlt	ip, r5
	ldr	r5, [r2, #-1168]
	bic	ip, ip, #3
	add	ip, r5, ip
	str	ip, [r4, #4]
	ldr	ip, [r2, #-1164]
	mov	r5, #36
	ldr	r1, [r9, r1]
	mla	ip, r5, r3, ip
	add	r3, r3, #1
	uxth	r3, r3
	str	r1, [ip, #8]
	ldr	r1, [r4, #4]
	str	r1, [ip, #12]
.L664:
	ldrh	r1, [r0, sl]
	cmp	r3, r1
	bcc	.L665
	b	.L670
.L667:
	mul	r2, r8, r1
	ldr	r4, [r3, #-1176]
	add	r0, r4, r2
	str	r7, [r0, #8]
	ldrh	r0, [ip, r6]
	mul	r0, r0, r1
	add	sl, r0, #3
	cmp	r0, #0
	movlt	r0, sl
	ldr	sl, [r3, #-1172]
	bic	r0, r0, #3
	add	r0, sl, r0
	str	r0, [r4, r2]
	ldr	r0, [r3, #-1176]
	add	r0, r0, r2
	ldrh	r2, [ip, r5]
	mul	r2, r2, r1
	add	r1, r1, #1
	uxth	r1, r1
	add	r4, r2, #3
	cmp	r2, #0
	movlt	r2, r4
	ldr	r4, [r3, #-1168]
	bic	r2, r2, #3
	add	r2, r4, r2
	str	r2, [r0, #4]
	b	.L669
.L670:
	ldr	r3, .L671
	mov	r8, #12
	ldr	ip, .L671+4
	mov	r7, #0
	movw	r6, #3918
	mov	r5, #3920
.L669:
	ldr	r2, [r3, #-1160]
	cmp	r1, r2
	bcc	.L667
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, sl, fp, pc}
.L672:
	.align	2
.L671:
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
	ldr	r3, .L680
	stmfd	sp!, {r4, r5, r6, r7, r8, r9, sl, fp, lr}
	.save {r4, r5, r6, r7, r8, r9, sl, fp, lr}
	mov	r5, #36
	ldr	r6, [r3, #-1160]
	mov	fp, #12
	ldr	ip, [r3, #-1176]
	mov	r3, #0
	mov	r4, r3
	b	.L674
.L677:
	mul	sl, fp, r2
	add	r8, ip, sl
	ldr	r9, [ip, sl]
	ldr	sl, [r7, #8]
	cmp	r9, sl
	streq	r4, [r8, #8]
	beq	.L676
.L675:
	add	r2, r2, #1
	uxth	r2, r2
.L679:
	cmp	r2, r6
	bcc	.L677
.L676:
	add	r3, r3, #1
	uxth	r3, r3
.L674:
	cmp	r3, r1
	ldmcsfd	sp!, {r4, r5, r6, r7, r8, r9, sl, fp, pc}
	mla	r7, r5, r3, r0
	mov	r2, #0
	b	.L679
.L681:
	.align	2
.L680:
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
	ldr	r3, .L689
	stmfd	sp!, {r4, r5, r6, r7, r8, sl, lr}
	.save {r4, r5, r6, r7, r8, sl, lr}
	mov	r6, #12
	ldr	r8, [r3, #-1160]
	mov	r5, #1
	ldr	r7, [r3, #-1176]
	mov	r4, #36
	mov	r3, #0
	b	.L683
.L686:
	mla	ip, r6, r2, r7
	ldr	sl, [ip, #8]
	cmp	sl, #0
	bne	.L684
	mla	r2, r4, r3, r0
	ldr	sl, [ip, #0]
	str	r5, [ip, #8]
	str	sl, [r2, #8]
	ldr	ip, [ip, #4]
	str	ip, [r2, #12]
	b	.L685
.L684:
	add	r2, r2, #1
	uxth	r2, r2
	b	.L687
.L688:
	mov	r2, #0
.L687:
	cmp	r2, r8
	bcc	.L686
.L685:
	add	r3, r3, #1
	uxth	r3, r3
.L683:
	cmp	r3, r1
	bcc	.L688
	ldmfd	sp!, {r4, r5, r6, r7, r8, sl, pc}
.L690:
	.align	2
.L689:
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
	ldr	r3, .L696
	ldr	r2, .L696+4
	ldrh	r1, [r3, r2]
	ldr	r2, [r3, #-1152]
	mov	r3, #0
	b	.L692
.L694:
	ldrh	ip, [r2], #2
	cmp	ip, r0
	beq	.L695
	add	r3, r3, #1
	uxth	r3, r3
.L692:
	cmp	r3, r1
	bne	.L694
	mov	r0, #0
	bx	lr
.L695:
	mov	r0, #1
	bx	lr
.L697:
	.align	2
.L696:
	.word	.LANCHOR2
	.word	-1156
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
	ldr	r3, .L702
	ldr	r2, .L702+4
	ldrh	ip, [r3, r2]
	ldr	r2, [r3, #-1152]
	mov	r3, #0
	mov	r1, r2
	b	.L699
.L701:
	ldrh	r7, [r1], #2
	cmp	r7, r0
	beq	.L700
	add	r3, r3, #1
	uxth	r3, r3
.L699:
	cmp	r3, ip
	bne	.L701
	mov	r3, r3, asl #1
	strh	r0, [r2, r3]	@ movhi
	ldr	r2, .L702
	ldr	r3, .L702+4
	ldrh	r1, [r2, r3]
	add	r1, r1, #1
	strh	r1, [r2, r3]	@ movhi
.L700:
	ldr	r3, .L702
	mov	r0, #12
	ldr	r2, .L702+8
	ldrh	r1, [r3, r2]
	mul	r1, r0, r1
	ldr	r0, [r3, #-1148]
	add	ip, r0, r1
	stmib	ip, {r5, r6}
	str	r4, [r0, r1]
	ldrh	r1, [r3, r2]
	add	r1, r1, #1
	strh	r1, [r3, r2]	@ movhi
	ldmfd	sp!, {r3, r4, r5, r6, r7, pc}
.L703:
	.align	2
.L702:
	.word	.LANCHOR2
	.word	-1156
	.word	-1144
	.fnend
	.size	FtlGcUpdatePage, .-FtlGcUpdatePage
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
	.word	-1142
	.word	-1140
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
	ldr	r3, [r5, #-1888]
	cmp	r3, #0
	beq	.L710
	ldr	r3, [r5, #-2088]
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
	add	r1, r2, r3, asl #1
	add	r3, r3, #1
	sub	r1, r1, #1136
	strh	r4, [r1, #0]	@ movhi
	ldr	r1, .L714+4
	strh	r3, [r2, r1]	@ movhi
.L712:
	mov	r0, #0
	ldmfd	sp!, {r3, r4, r5, r6, r7, pc}
.L715:
	.align	2
.L714:
	.word	.LANCHOR2
	.word	-1138
	.word	.LC79
	.word	.LANCHOR2-1138
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
	ldrh	r3, [r5, r4]
	add	r3, r5, r3, asl #1
	sub	r3, r3, #1136
	ldrh	r0, [r3, #0]
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
	.word	-1138
	.word	.LANCHOR2
	.word	-1142
	.word	-1102
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
	ldr	r0, [r4, #-1152]
	strh	r3, [r4, r2]	@ movhi
	add	r2, r2, #12
	strh	r3, [r4, r2]	@ movhi
	ldrh	r2, [r6, r5]
	mov	r2, r2, asl #1
	bl	ftl_memset
	ldrh	r3, [r6, r5]
	mov	r2, #12
	ldr	r0, [r4, #-1148]
	mov	r1, #255
	mul	r2, r2, r3
	bl	ftl_memset
	ldmfd	sp!, {r4, r5, r6, lr}
	b	FtlGcBufInit
.L725:
	.align	2
.L724:
	.word	.LANCHOR2
	.word	-1156
	.word	.LANCHOR0
	.fnend
	.size	FtlGcPageVarInit, .-FtlGcPageVarInit
	.align	2
	.global	SupperBlkListInit
	.type	SupperBlkListInit, %function
SupperBlkListInit:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	stmfd	sp!, {r3, r4, r5, r6, r7, r8, r9, sl, fp, lr}
	.save {r3, r4, r5, r6, r7, r8, r9, sl, fp, lr}
	movw	r3, #3850
	ldr	r7, .L736
	mov	r2, #6
	ldr	r4, .L736+4
	mov	r1, #0
	mov	r6, #0
	ldrh	r3, [r7, r3]
	mov	sl, r6
	ldr	r0, [r4, #-2076]
	mul	r2, r2, r3
	bl	ftl_memset
	ldr	r3, .L736+8
	str	r6, [r4, #-2056]
	str	r6, [r4, #-2072]
	strh	r6, [r4, r3]	@ movhi
	add	r3, r3, #8
	str	r6, [r4, #-2064]
	strh	r6, [r4, r3]	@ movhi
	mov	r4, r6
	b	.L727
.L729:
	add	r3, r7, r8
	mov	r1, r4
	add	r8, r8, #1
	ldrb	r0, [r3, #3866]	@ zero_extendqisi2
	bl	V2P_block
	uxth	r8, r8
	bl	FtlBbmIsBadBlock
	cmp	r0, #0
	ldreqh	r3, [r7, r9]
	addeq	r5, r5, r3
	uxtheq	r5, r5
	b	.L734
.L735:
	mov	r5, #0
	mov	fp, #3840
	mov	r8, r5
	movw	r9, #3908
.L734:
	ldrh	r3, [r7, fp]
	cmp	r3, r8
	bhi	.L729
	cmp	r5, #0
	ldr	r8, .L736+4
	beq	.L730
	mov	r1, r5
	mov	r0, #32768
	bl	__aeabi_idiv
	uxth	r5, r0
	b	.L731
.L730:
	ldr	r2, [r8, #-2068]
	mov	r3, r4, asl #1
	mvn	r1, #0
	strh	r1, [r2, r3]	@ movhi
.L731:
	ldr	r2, [r8, #-2076]
	mov	r1, #6
	ldr	r3, .L736+4
	mla	r2, r1, r4, r2
	strh	r5, [r2, #4]	@ movhi
	ldr	r2, .L736+12
	ldrh	r2, [r8, r2]
	cmp	r2, r4
	beq	.L732
	ldr	r2, .L736+16
	ldrh	r2, [r3, r2]
	cmp	r2, r4
	beq	.L732
	ldr	r2, .L736+20
	ldrh	r2, [r3, r2]
	cmp	r2, r4
	beq	.L732
	ldr	r2, [r3, #-2068]
	mov	r3, r4, asl #1
	ldrh	r3, [r2, r3]
	cmp	r3, #0
	bne	.L733
	add	r6, r6, #1
	mov	r0, r4
	uxth	r6, r6
	bl	INSERT_FREE_LIST
	b	.L732
.L733:
	add	sl, sl, #1
	mov	r0, r4
	uxth	sl, sl
	bl	INSERT_DATA_LIST
.L732:
	add	r4, r4, #1
	uxth	r4, r4
.L727:
	movw	r2, #3848
	ldrh	r3, [r7, r2]
	cmp	r3, r4
	bhi	.L735
	ldr	r3, .L736+4
	mov	r0, #0
	ldr	r2, .L736+8
	strh	sl, [r3, r2]	@ movhi
	add	r2, r2, #8
	strh	r6, [r3, r2]	@ movhi
	ldmfd	sp!, {r3, r4, r5, r6, r7, r8, r9, sl, fp, pc}
.L737:
	.align	2
.L736:
	.word	.LANCHOR0
	.word	.LANCHOR2
	.word	-2060
	.word	-2048
	.word	-2000
	.word	-1952
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
	ldr	r0, [r5, #-1100]
	mov	r2, r2, asl #1
	bl	ftl_memset
	ldrh	r3, [r4, r6]
	ldrh	r2, [r4, r7]
	mov	r1, #255
	ldr	r0, [r5, #-1096]
	mul	r2, r2, r3
	bl	ftl_memset
	mov	r2, #0
	mov	ip, r2
	mvn	r0, #0
	b	.L739
.L740:
	mul	r3, r8, r2
	ldr	r1, [r5, #-1904]
	add	sl, r1, r3
	str	ip, [sl, #4]
	strh	r0, [r1, r3]	@ movhi
	ldr	r1, [r5, #-1904]
	ldr	sl, [r5, #-1096]
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
	strh	r2, [r3, r0]	@ movhi
	sub	r0, r0, #2
	strh	r2, [r3, r0]	@ movhi
	ldr	r0, [r1, #3936]
	ldr	r2, .L741+12
	strh	r0, [r3, r2]	@ movhi
	ldr	r0, .L741+16
	ldr	r2, .L741+20
	strh	r0, [r3, r2]	@ movhi
	add	r2, r2, #36
	ldrh	r0, [r3, r2]
	sub	r2, r2, #32
	strh	r0, [r3, r2]	@ movhi
	movw	r2, #3944
	ldrh	r1, [r1, r2]
	ldr	r2, .L741+24
	strh	r1, [r3, r2]	@ movhi
	ldr	r2, [r3, #-1048]
	str	r2, [r3, #-1080]
	ldr	r2, [r3, #-1044]
	str	r2, [r3, #-1076]
	ldr	r2, [r3, #-1100]
	str	r2, [r3, #-1072]
	ldr	r2, [r3, #-1040]
	str	r2, [r3, #-1068]
	ldmfd	sp!, {r3, r4, r5, r6, r7, r8, sl, pc}
.L742:
	.align	2
.L741:
	.word	.LANCHOR0
	.word	.LANCHOR2
	.word	-1090
	.word	-1082
	.word	-3902
	.word	-1088
	.word	-1086
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
	.word	-1036
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
	str	r3, [r5, #-1016]
	strh	r3, [r5, r2]	@ movhi
	mov	r3, #3952
	strh	r4, [r6, r3]	@ movhi
	movw	r3, #3928
	ldrh	r2, [r6, r3]
	ldr	r0, [r6, #3956]
	str	r4, [r5, #-1032]
	mov	r2, r2, asl #1
	str	r4, [r5, #-1024]
	str	r4, [r5, #-1020]
	str	r4, [r5, #-1888]
	bl	ftl_memset
	ldrh	r2, [r6, r7]
	mov	r1, r4
	ldr	r0, [r5, #-2088]
	mov	r2, r2, asl #1
	bl	ftl_memset
	ldrh	r2, [r6, r7]
	mov	r1, r4
	ldr	r0, [r5, #-1012]
	mov	r2, r2, asl #1
	bl	ftl_memset
	mov	r1, r4
	sub	r0, r5, #1824
	mov	r2, #48
	bl	ftl_memset
	mov	r1, r4
	mov	r2, #512
	ldr	r0, .L769+12
	bl	ftl_memset
	bl	FtlGcBufInit
	bl	FtlL2PDataInit
	mov	r0, r4
	ldmfd	sp!, {r3, r4, r5, r6, r7, pc}
.L770:
	.align	2
.L769:
	.word	.LANCHOR2
	.word	-1028
	.word	.LANCHOR0
	.word	.LANCHOR2-1708
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
	movw	r3, #65535
	ldr	r4, .L802
	mov	r6, #0
	ldr	r2, .L802+4
	mov	r0, #1024
	ldr	r5, .L802+8
	mov	r7, #12
	str	r3, [r4, #-1000]
	mvn	r3, #0
	strh	r3, [r4, r2]	@ movhi
	add	r2, r2, #2
	str	r6, [r4, #-1008]
	mov	sl, #3840
	strh	r3, [r4, r2]	@ movhi
	mov	r2, #32
	ldr	r3, .L802+12
	mov	r8, #36
	str	r6, [r4, #-1712]
	str	r6, [r4, #-1852]
	strh	r2, [r4, r3]	@ movhi
	add	r3, r3, #2
	mov	r2, #128
	str	r6, [r4, #-1848]
	strh	r2, [r4, r3]	@ movhi
	add	r3, r3, #6
	str	r6, [r4, #-1864]
	strh	r6, [r4, r3]	@ movhi
	add	r3, r3, #50
	str	r6, [r4, #-1876]
	strh	r6, [r4, r3]	@ movhi
	add	r3, r3, #36
	str	r6, [r4, #-1880]
	strh	r6, [r4, r3]	@ movhi
	movw	r3, #3914
	ldrh	r1, [r5, r3]
	str	r6, [r4, #-1872]
	str	r6, [r4, #-1868]
	str	r6, [r4, #-1884]
	str	r6, [r4, #-1844]
	str	r6, [r4, #-1840]
	str	r6, [r4, #-2084]
	str	r6, [r4, #-1832]
	str	r6, [r4, #-1828]
	str	r6, [r4, #-1004]
	str	r6, [r4, #-1184]
	str	r6, [r4, #-996]
	str	r6, [r4, #-1192]
	str	r6, [r5, #3972]
	bl	__aeabi_idiv
	mov	r3, #3840
	ldrh	r3, [r5, r3]
	str	r6, [r5, #3960]
	movw	r6, #3912
	mov	r3, r3, asl #2
	cmp	r0, r3
	str	r0, [r4, #-992]
	ldrh	r0, [r5, r6]
	strhi	r3, [r4, #-992]
	ldr	r4, .L802
	mov	r0, r0, asl #1
	bl	ftl_malloc
	str	r0, [r4, #-1152]
	ldrh	r0, [r5, r6]
	mul	r0, r7, r0
	bl	ftl_malloc
	ldrh	r6, [r5, sl]
	mul	r6, r8, r6
	mov	r9, r6, asl #3
	str	r0, [r4, #-1148]
	mov	r0, r9
	bl	ftl_malloc
	str	r0, [r4, #-988]
	mov	r0, r6
	bl	ftl_malloc
	str	r0, [r4, #-984]
	mov	r0, r9
	bl	ftl_malloc
	str	r0, [r4, #-980]
	mov	r0, r6
	bl	ftl_malloc
	str	r0, [r4, #-2092]
	mov	r0, r6
	bl	ftl_malloc
	str	r0, [r4, #-1164]
	ldr	r0, [r4, #-992]
	mul	r0, r8, r0
	bl	ftl_malloc
	movw	r8, #3918
	ldrh	r3, [r5, sl]
	ldrh	r6, [r5, r8]
	mov	r3, r3, asl #1
	add	r3, r3, #1
	str	r3, [r4, #-1160]
	str	r0, [r5, #3964]
	mov	r0, r6
	bl	ftl_malloc
	str	r0, [r4, #-2080]
	mov	r0, r6
	bl	ftl_malloc
	str	r0, [r4, #-976]
	mov	r0, r6
	bl	ftl_malloc
	str	r0, [r4, #-972]
	ldr	r0, [r4, #-1160]
	mul	r0, r0, r6
	bl	ftl_malloc
	str	r0, [r4, #-1172]
	ldr	r0, [r4, #-992]
	mul	r0, r0, r6
	bl	ftl_malloc
	str	r0, [r4, #-968]
	mov	r0, r6
	bl	ftl_malloc
	str	r0, [r4, #-964]
	mov	r0, r6
	bl	ftl_malloc
	mov	r6, #3920
	str	r0, [r4, #-960]
	ldr	r0, [r4, #-1160]
	mul	r0, r7, r0
	bl	ftl_malloc
	ldrh	r3, [r5, r6]
	ldrh	sl, [r5, sl]
	mul	sl, sl, r3
	str	r0, [r4, #-1176]
	mov	r0, sl
	bl	ftl_malloc
	str	r0, [r4, #-956]
	mov	r0, sl, asl #3
	bl	ftl_malloc
	ldrh	r3, [r5, r6]
	ldr	sl, .L802+16
	str	r0, [r4, #-952]
	ldr	r0, [r4, #-1160]
	mul	r0, r0, r3
	bl	ftl_malloc
	ldrh	r3, [r5, r6]
	movw	r6, #3850
	str	r0, [r4, #-1168]
	ldr	r0, [r4, #-992]
	mul	r0, r0, r3
	bl	ftl_malloc
	str	r0, [r4, #-948]
	ldrh	r0, [r5, r6]
	mov	r0, r0, asl #1
	uxth	r0, r0
	strh	r0, [r4, sl]	@ movhi
	bl	ftl_malloc
	str	r0, [r4, #-1012]
	ldrh	r0, [r4, sl]
	add	r0, r0, #544
	add	r0, r0, #3
	mov	r0, r0, lsr #9
	strh	r0, [r4, sl]	@ movhi
	mov	r0, r0, asl #9
	bl	ftl_malloc
	ldrh	sl, [r5, r6]
	mov	sl, sl, asl #1
	str	r0, [r4, #-940]
	add	r0, r0, #32
	str	r0, [r4, #-2088]
	mov	r0, sl
	bl	ftl_malloc
	str	r0, [r4, #-936]
	mov	r0, sl
	bl	ftl_malloc
	ldr	sl, [r5, #3936]
	mov	sl, sl, asl #1
	str	r0, [r4, #-2068]
	mov	r0, sl
	bl	ftl_malloc
	str	r0, [r4, #-1048]
	mov	r0, sl
	bl	ftl_malloc
	movw	sl, #3928
	str	r0, [r4, #-1100]
	ldrh	r0, [r5, r6]
	mov	r0, r0, lsr #3
	add	r0, r0, #4
	bl	ftl_malloc
	str	r0, [r4, #-1892]
	ldrh	r0, [r5, sl]
	mov	r0, r0, asl #1
	bl	ftl_malloc
	str	r0, [r5, #3956]
	ldrh	r0, [r5, sl]
	mov	r0, r0, asl #1
	bl	ftl_malloc
	str	r0, [r4, #-932]
	ldrh	r0, [r5, sl]
	add	sl, sl, #2
	mov	r0, r0, asl #2
	bl	ftl_malloc
	str	r0, [r4, #-928]
	ldrh	r0, [r5, sl]
	mov	r0, r0, asl #2
	bl	ftl_malloc
	ldrh	r2, [r5, sl]
	mov	r1, #0
	add	sl, sl, #16
	mov	r2, r2, asl #2
	str	r0, [r4, #-924]
	bl	ftl_memset
	movw	r3, #3944
	ldrh	r0, [r5, r3]
	mov	r0, r0, asl #2
	bl	ftl_malloc
	str	r0, [r4, #-1040]
	ldr	r0, [r5, #3936]
	mov	r0, r0, asl #2
	bl	ftl_malloc
	str	r0, [r4, #-1044]
	ldrh	r0, [r5, sl]
	mul	r0, r7, r0
	bl	ftl_malloc
	ldrh	r3, [r5, sl]
	movw	r7, #3862
	str	r0, [r4, #-1904]
	ldrh	r0, [r5, r8]
	mul	r0, r0, r3
	bl	ftl_malloc
	ldrh	r3, [r5, r6]
	ldr	r6, .L802+20
	str	r0, [r4, #-1096]
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
	str	r0, [r4, #-2076]
	mov	r0, r3, asl #2
	bl	ftl_malloc
	ldrh	r1, [r4, r6]
	mov	r3, #1
	ldr	ip, .L802+8
	mov	r1, r1, asl #2
	mov	r2, r1
	str	r0, [r5, #4004]
	ldrh	r5, [r5, r7]
	ldr	r0, .L802+24
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
	ldr	r0, .L802+28
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
	ldr	r2, [r3, #-1048]
	cmp	r2, #0
	beq	.L801
.L777:
	ldr	r2, [r3, #-1100]
	cmp	r2, #0
	beq	.L801
.L779:
	ldr	r2, [r3, #-1040]
	cmp	r2, #0
	beq	.L801
.L780:
	ldr	r2, [r3, #-1044]
	cmp	r2, #0
	beq	.L801
.L781:
	ldr	r2, [r3, #-1904]
	cmp	r2, #0
	beq	.L801
.L782:
	ldr	r2, [r3, #-1096]
	cmp	r2, #0
	beq	.L801
.L783:
	ldr	r2, [r3, #-2076]
	cmp	r2, #0
	beq	.L801
.L784:
	ldr	r2, .L802+8
	ldr	r2, [r2, #4004]
	cmp	r2, #0
	beq	.L801
.L785:
	ldr	r3, [r3, #-2068]
	cmp	r3, #0
	beq	.L801
.L786:
	ldr	r3, .L802
	ldr	r2, [r3, #-1152]
	cmp	r2, #0
	beq	.L801
	ldr	r2, [r3, #-1148]
	cmp	r2, #0
	beq	.L801
.L788:
	ldr	r2, [r3, #-988]
	cmp	r2, #0
	beq	.L801
	ldr	r2, [r3, #-980]
	cmp	r2, #0
	beq	.L801
	ldr	r2, [r3, #-2092]
	cmp	r2, #0
	beq	.L801
	ldr	r2, [r3, #-1164]
	cmp	r2, #0
	beq	.L801
	ldr	r2, [r3, #-984]
	cmp	r2, #0
	beq	.L801
.L790:
	ldr	r2, [r3, #-2080]
	cmp	r2, #0
	beq	.L801
	ldr	r2, [r3, #-976]
	cmp	r2, #0
	beq	.L801
	ldr	r3, [r3, #-972]
	cmp	r3, #0
	beq	.L801
	ldr	r3, .L802
	ldr	r2, [r3, #-1172]
	cmp	r2, #0
	beq	.L801
	ldr	r2, [r3, #-964]
	cmp	r2, #0
	beq	.L801
	ldr	r2, [r3, #-960]
	cmp	r2, #0
	beq	.L801
	ldr	r2, [r3, #-1176]
	cmp	r2, #0
	beq	.L801
.L792:
	ldr	r2, [r3, #-956]
	cmp	r2, #0
	beq	.L801
	ldr	r2, [r3, #-952]
	cmp	r2, #0
	beq	.L801
	ldr	r2, [r3, #-1168]
	cmp	r2, #0
	beq	.L801
.L794:
	ldr	r2, [r3, #-2088]
	cmp	r2, #0
	beq	.L801
	ldr	r3, [r3, #-1012]
	cmp	r3, #0
	beq	.L801
.L796:
	ldr	r3, .L802+8
	ldr	r3, [r3, #3956]
	cmp	r3, #0
	beq	.L801
.L797:
	ldr	r3, .L802
	ldr	r2, [r3, #-932]
	cmp	r2, #0
	beq	.L801
.L798:
	ldr	r2, [r3, #-928]
	cmp	r2, #0
	beq	.L801
.L799:
	ldr	r3, [r3, #-924]
	cmp	r3, #0
	bne	.L800
.L801:
	ldr	r0, .L802+32
	ldr	r1, .L802+36
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
	.word	-1142
	.word	.LANCHOR0
	.word	-1196
	.word	-944
	.word	-1036
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
	strb	r2, [r1, #-920]
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
	strb	r2, [r3, #-920]
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
	.word	-918
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
	sub	ip, ip, #916
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
	.word	.LANCHOR2-916
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
	ldr	r4, [r4, #108]
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
	.word	.LC74
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
	sub	r3, r3, #916
	ldrh	r3, [r3, #0]
	cmp	r3, sl
	ldmnefd	sp!, {r3, r4, r5, r6, r7, r8, sl, pc}
	mov	r1, #255
	mov	r2, #32768
	ldr	r0, [r5, #112]
	bl	ftl_memset
	add	r1, r4, r7
	add	r4, r4, #1
	mov	r0, r8
	ldr	r2, [r5, #112]
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
	.pad #28
	sub	sp, sp, #28
	ldr	r4, .L1003
	mov	sl, r3
	str	r2, [sp, #20]
	mov	r9, r0
	str	r1, [sp, #16]
	mov	r8, #0
	ldrb	r3, [r4, #3832]	@ zero_extendqisi2
	mov	fp, r8
	ldrb	r2, [r4, #928]	@ zero_extendqisi2
	add	r4, r4, r9, asl #3
	cmp	r2, #0
	addeq	r3, r3, r3, asl #1
	movne	r3, r3, lsr #2
	ubfxeq	r3, r3, #2, #8
	str	r3, [sp, #12]
	bl	NandcWaitFlashReady
	ldrb	r7, [r4, #868]	@ zero_extendqisi2
	ldr	r6, [r4, #864]
	mvn	r4, #0
	add	r7, r7, #8
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
	ldrb	r3, [r2, #116]	@ zero_extendqisi2
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
	ldrb	r3, [fp, #116]	@ zero_extendqisi2
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
	ldrb	r3, [r2, #116]	@ zero_extendqisi2
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
	ldrb	r3, [r2, #116]	@ zero_extendqisi2
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
	ldr	r3, [r5, #124]
	ldrb	r0, [r5, #128]	@ zero_extendqisi2
	str	r3, [r5, #120]
	bl	FlashBchSel
	mov	r1, #0
	mov	r2, #2048
	ldr	r0, [r5, #124]
	bl	ftl_memset
	ldr	r3, [r5, #120]
	ldr	r1, .L1046+12
	mov	r2, #32
	str	r8, [r3, #0]
	ldr	r4, [r5, #120]
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
	ldr	r0, [r5, #120]
	ldr	r1, .L1046+24
	mov	r2, #32
	add	r0, r0, #160
	bl	memcpy
	ldr	r4, [r5, #120]
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
	ldr	r3, [r5, #112]
	str	r3, [r5, #120]
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
	ldr	r2, [r5, #124]
	mov	r3, r0
	mul	r1, r1, r4
	bl	FlashProgPage
	ldr	r1, [r7, #856]
	mov	r0, #0
	ldr	r2, [r5, #124]
	mov	r3, r0
	mul	r1, r1, r4
	add	r1, r1, #1
	bl	FlashProgPage
	ldr	r1, [r7, #856]
	mov	r0, #0
	ldr	r2, [r5, #112]
	mov	r3, r0
	mul	r1, r1, r4
	bl	FlashReadRawPage
	cmn	r0, #1
	beq	.L1040
	ldr	sl, [r5, #120]
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
	str	r3, [r5, #132]
	ldr	r3, [r7, #856]
	add	r6, r6, #1
	cmp	r6, #1
	mul	r3, r3, r4
	str	r3, [r5, #136]
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
	ldr	r2, [r9, #124]
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
	ldr	r3, [r5, #124]
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
	ldr	r1, [r5, #124]
	mov	r2, #2048
	bl	memcpy
	ldr	r3, [r5, #124]
	ldr	r3, [r3, #512]
	strb	r3, [fp, #853]
	ldr	r3, [r5, #132]
	cmp	r3, r4
	bls	.L1056
	str	r4, [r5, #132]
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
	str	r4, [r5, #136]
	ldrh	r9, [r2, r3]
	mvn	r6, #0
	ldr	r3, [r5, #124]
	ldr	fp, .L1072+12
	str	r3, [r5, #120]
	bl	flash_enter_slc_mode
	b	.L1062
.L1064:
	add	r3, sp, #4
	ldrb	r0, [r3, r7]	@ zero_extendqisi2
	bl	FlashBchSel
	mov	r0, #0
	mov	r1, r4
	ldr	r2, [r5, #124]
	mov	r3, r0
	bl	FlashReadRawPage
	cmn	r0, #1
	bne	.L1063
	mov	r0, #0
	mov	r1, sl
	ldr	r2, [r5, #124]
	mov	r3, r0
	bl	FlashReadRawPage
	cmn	r0, #1
	bne	.L1063
	add	r7, r7, #1
	cmp	r7, #4
	beq	.L1065
	b	.L1064
.L1063:
	ldr	r7, [r5, #120]
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
	str	r0, [r3, #132]
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
	str	r4, [r5, #136]
	strb	r3, [r6, #3773]
	movw	r3, #2838
	ldrh	r1, [r2, r3]
	mov	r6, #0
	bl	__aeabi_uidiv
	add	r0, r0, #1
	str	r0, [r5, #132]
	cmp	r0, #1
	moveq	r3, #2
	streq	r3, [r5, #132]
	ldrh	r3, [r7, #14]
	strb	r3, [r5, #140]
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
	ldr	r2, [r3, #144]
	add	r2, r2, #1
	str	r2, [r3, #144]
	cmp	r2, #2048
	movcs	sl, #0
	strcs	sl, [r3, #144]
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
	ldr	ip, [r7, #148]
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
	ldr	ip, [r7, #148]
	mov	r0, r5
	mov	r1, r6
	mov	r2, r8
	mov	r3, sl
	blx	ip
	mov	r4, r0
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
	ldrb	r3, [r3, #152]	@ zero_extendqisi2
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
	ldr	r7, [r3, #148]
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
	ldr	ip, [r3, #148]
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
	ldr	r3, [r3, #148]
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
	ldr	r3, [r3, #148]
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
	ldr	r2, .L1197
	movw	r3, #2936
	stmfd	sp!, {r4, r5, r6, r7, r8, r9, sl, fp, lr}
	.save {r4, r5, r6, r7, r8, r9, sl, fp, lr}
	.pad #28
	sub	sp, sp, #28
	ldrh	r4, [r2, r3]
	movw	r3, #65535
	mov	r5, r0
	str	r1, [sp, #16]
	cmp	r4, r3
	beq	.L1190
	cmp	r4, #0
	bne	.L1171
	b	.L1172
.L1190:
	mov	r4, #0
.L1171:
	ldr	r2, .L1197+4
	movw	r3, #3908
	ldr	fp, [sp, #16]
	ldrh	r3, [r2, r3]
	cmp	fp, r3
	bne	.L1173
.L1172:
	bl	FtlGcPageVarInit
.L1173:
	ldr	r6, .L1197+8
	mov	r8, #0
	mvn	sl, #0
	mov	r9, r8
.L1185:
	ldrh	r2, [r5, #0]
	movw	r3, #65535
	strb	r9, [r5, #8]
	cmp	r2, r3
	beq	.L1191
.L1175:
	mov	r7, r5
.L1194:
	ldr	r3, .L1197+4
	mov	r2, #3840
	ldr	r0, [r6, #-1168]
	ldr	ip, [r6, #-988]
	ldrh	lr, [r3, r2]
	mov	r2, #3920
	ldrh	r2, [r3, r2]
	mov	r3, #0
	str	r0, [sp, #12]
	mov	r5, r3
	str	r2, [sp, #20]
	mov	r2, r7
	b	.L1176
.L1178:
	ldrh	r0, [r2, #16]
	movw	r1, #65535
	cmp	r0, r1
	beq	.L1177
	mov	fp, #36
	orr	r0, r4, r0, asl #10
	mla	r1, fp, r5, ip
	ldr	fp, [sp, #20]
	stmib	r1, {r0, r9}
	mul	r0, fp, r5
	add	r5, r5, #1
	uxth	r5, r5
	add	fp, r0, #3
	cmp	r0, #0
	movlt	r0, fp
	ldr	fp, [sp, #12]
	bic	r0, r0, #3
	add	r0, fp, r0
	str	r0, [r1, #12]
.L1177:
	add	r3, r3, #1
	add	r2, r2, #2
	uxth	r3, r3
.L1176:
	cmp	r3, lr
	bne	.L1178
	mov	r1, r5
	ldr	r0, [r6, #-988]
	mov	r2, #0
	bl	FlashReadPages
	mov	r3, #36
	mul	r3, r3, r5
	mov	r5, #0
	str	r3, [sp, #12]
	b	.L1179
.L1186:
	ldr	r3, [r6, #-988]
	ldr	ip, .L1197+8
	add	r1, r3, r5
	ldr	fp, [r1, #4]
	stmia	sp, {r3, ip}
	ubfx	r0, fp, #10, #16
	str	r1, [sp, #8]
	bl	P2V_plane
	ldr	r3, [sp, #0]
	ldr	ip, [sp, #4]
	ldr	r1, [sp, #8]
	ldr	r3, [r3, r5]
	cmp	r3, #0
	mov	r2, r0
	mov	r0, ip
	bne	.L1180
	ldr	r3, [r1, #12]
	movw	r0, #65535
	add	r5, r5, #36
	ldrh	r1, [r3, #0]
	cmp	r1, r0
	moveq	r3, #1
	moveq	r5, r7
	streq	r3, [ip, #-1020]
	beq	.L1174
.L1181:
	ldr	r0, [r3, #12]
	mov	r1, fp
	ldr	r2, [r3, #8]
	bl	FtlGcUpdatePage
	b	.L1179
.L1180:
	ldr	r1, .L1197+8
	mov	r5, r7
	ldr	r2, [r1, #-1888]
	cmp	r2, #0
	bne	.L1182
	ldr	r2, .L1197+4
	ldrb	r2, [r2, #928]	@ zero_extendqisi2
	cmp	r2, #0
	beq	.L1183
.L1182:
	ldrh	r2, [r5, #0]
	ldr	r1, [r0, #-2088]
	mov	r2, r2, asl #1
	ldrh	r2, [r1, r2]
	cmp	r2, #119
	movls	sl, fp
	bls	.L1184
.L1183:
	cmn	r3, #1
	moveq	sl, fp
.L1184:
	ldrh	r3, [r5, #0]
	mov	r4, #0
	ldr	r2, [r6, #-2068]
	mov	r3, r3, asl #1
	strh	r9, [r2, r3]	@ movhi
	ldrh	r0, [r5, #0]
	bl	INSERT_FREE_LIST
	mvn	r3, #0
	strh	r3, [r5, #0]	@ movhi
	bl	FtlGcPageVarInit
	b	.L1185
.L1179:
	ldr	r3, [sp, #12]
	cmp	r5, r3
	bne	.L1186
	ldr	fp, [sp, #16]
	add	r8, r8, #1
	add	r4, r4, #1
	cmp	r8, fp
	uxth	r4, r4
	bcc	.L1187
	ldr	r2, .L1197
	movw	r3, #2936
	movw	r0, #65535
	ldrh	r1, [r2, r3]
	cmp	r1, r0
	beq	.L1187
	add	r1, r1, r8
	strh	r1, [r2, r3]	@ movhi
	ldr	r2, .L1197+4
	add	r3, r3, #972
	ldrh	r3, [r2, r3]
	cmp	r3, r4
	bhi	.L1188
.L1187:
	ldr	r2, .L1197+4
	movw	r3, #3908
	ldrh	r3, [r2, r3]
	cmp	r3, r4
	bhi	.L1194
	mov	r5, r7
.L1191:
	mov	r2, #0
.L1174:
	ldr	r1, .L1197
	movw	r3, #2936
	mvn	r0, #0
	strh	r4, [r5, #2]	@ movhi
	strb	r2, [r5, #6]
	strh	r0, [r1, r3]	@ movhi
	mov	r0, r5
	mov	r1, r4
	bl	ftl_sb_update_avl_pages
.L1188:
	mov	r0, sl
	add	sp, sp, #28
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, sl, fp, pc}
.L1198:
	.align	2
.L1197:
	.word	.LANCHOR1
	.word	.LANCHOR0
	.word	.LANCHOR2
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
	ldr	r5, .L1258
	.pad #28
	sub	sp, sp, #28
	ldr	r4, .L1258+4
	mov	r1, r6
	ldr	r3, .L1258+8
	movw	r7, #3928
	ldr	r2, [r5, #3936]
	ldr	r0, [r4, #-1044]
	mov	sl, r4
	strh	r6, [r4, r3]	@ movhi
	mov	r3, #3952
	strh	r6, [r5, r3]	@ movhi
	mov	r2, r2, asl #2
	bl	ftl_memset
	ldr	r2, [r5, #3936]
	mov	r1, r6
	ldr	r0, [r4, #-1048]
	mov	r2, r2, asl #1
	bl	ftl_memset
	ldrh	r2, [r5, r7]
	mov	r1, r6
	ldr	r0, [r4, #-928]
	mov	r2, r2, asl #2
	bl	ftl_memset
	ldrh	r2, [r5, r7]
	mov	r1, r6
	ldr	r0, [r5, #3956]
	mov	r2, r2, asl #1
	bl	ftl_memset
	ldr	r0, .L1258+12
	mov	r1, #255
	mov	r2, #12
	bl	ftl_memset
	movw	r3, #3848
	ldrh	r3, [r5, r3]
	str	r3, [sp, #8]
	b	.L1200
.L1202:
	add	r3, r6, r5
	ldr	r1, [sp, #8]
	ldrb	r0, [r3, #3866]	@ zero_extendqisi2
	bl	V2P_block
	str	r0, [sp, #0]
	bl	FtlBbmIsBadBlock
	ldr	r3, [sp, #0]
	cmp	r0, #0
	bne	.L1201
	ldr	r2, [r4, #-988]
	mov	r3, r3, asl #10
	mla	r2, r9, r7, r2
	str	r3, [r2, #4]
	ldr	r3, [r4, #-1172]
	str	r3, [r2, #8]
	ldrh	r3, [r6, r8]
	mul	r3, r3, r7
	add	r7, r7, #1
	uxth	r7, r7
	add	r1, r3, #3
	cmp	r3, #0
	movlt	r3, r1
	ldr	r1, [r4, #-1168]
	bic	r3, r3, #3
	add	r3, r1, r3
	str	r3, [r2, #12]
.L1201:
	add	r5, r5, #1
	uxth	r5, r5
	b	.L1235
.L1248:
	mov	r7, #0
	mov	fp, #3840
	mov	r5, r7
	mov	r9, #36
	mov	r8, #3920
.L1235:
	ldrh	r3, [r6, fp]
	cmp	r3, r5
	bhi	.L1202
	cmp	r7, #0
	beq	.L1203
	ldr	r0, [r4, #-988]
	mov	r1, r7
	mov	r2, #1
	mov	fp, r7
	bl	FlashReadPages
	mov	r8, #0
	str	r8, [sp, #12]
.L1234:
	ldr	r3, [r4, #-988]
	add	r2, r3, r8
	ldr	r3, [r3, r8]
	ldr	r6, [r2, #4]
	cmn	r3, #1
	ldr	r5, [r2, #12]
	ubfx	r6, r6, #10, #16
	bne	.L1204
	mov	r7, #16
.L1206:
	ldr	r0, [r4, #-988]
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
	ldreq	r3, [sl, #-988]
	mvneq	r2, #0
	streq	r2, [r3, r8]
	beq	.L1204
.L1205:
	ldr	r3, [r4, #-988]
	ldr	r3, [r3, r8]
	cmn	r3, #1
	bne	.L1204
	sub	r7, r7, #1
	uxth	r7, r7
	cmp	r7, #0
	bne	.L1206
.L1204:
	ldr	r3, [r4, #-988]
	ldr	r3, [r3, r8]
	cmn	r3, #1
	beq	.L1207
	ldr	r2, [r4, #-1852]
	ldr	r3, [r5, #4]
	cmn	r2, #1
	beq	.L1208
	cmp	r2, r3
	bhi	.L1209
.L1208:
	cmn	r3, #1
	addne	r3, r3, #1
	strne	r3, [sl, #-1852]
.L1209:
	ldrh	r3, [r5, #0]
	movw	r2, #61604
	cmp	r3, r2
	beq	.L1212
	bhi	.L1215
	movw	r2, #61574
	cmp	r3, r2
	bne	.L1210
	b	.L1256
.L1215:
	movw	r2, #61634
	cmp	r3, r2
	beq	.L1213
	movw	r2, #65535
	cmp	r3, r2
	bne	.L1210
	b	.L1257
.L1213:
	ldr	r3, .L1258
	mov	r9, r6
	ldr	r1, .L1258+8
	ldr	ip, [r4, #-1044]
	ldr	r0, [r3, #3936]
	ldrh	r1, [r4, r1]
	uxth	r2, r0
	sub	r3, r2, #1
	rsb	r2, r1, r2
	sub	r2, r2, #1
	uxth	r3, r3
	sxth	r2, r2
	str	r2, [sp, #4]
	b	.L1216
.L1222:
	mov	r6, r2, asl #2
	ldr	r7, [r5, #4]
	str	r6, [sp, #16]
	ldr	r6, [ip, r2, asl #2]
	cmp	r7, r6
	bls	.L1217
	ldr	ip, [ip, #0]
	mov	r6, r9
	cmp	ip, #0
	bne	.L1218
	cmp	r1, r0
	addne	r1, r1, #1
	ldrne	r0, .L1258+8
	strneh	r1, [r4, r0]	@ movhi
.L1218:
	mov	r0, #0
	sxth	r9, r3
	str	r3, [sp, #4]
	b	.L1219
.L1220:
	ldr	r7, [r4, #-1044]
	add	ip, r1, #1
	add	r0, r0, #1
	ldr	r3, [r7, ip, asl #2]
	mov	ip, ip, asl #1
	uxth	r0, r0
	str	r3, [r7, r1, asl #2]
	mov	r1, r1, asl #1
	ldr	r7, [r4, #-1048]
	ldrh	ip, [r7, ip]
	strh	ip, [r7, r1]	@ movhi
.L1219:
	sxth	r1, r0
	cmp	r1, r9
	bne	.L1220
	ldr	r1, [sl, #-1044]
	mov	r2, r2, asl #1
	ldr	r0, [r5, #4]
	ldr	ip, [sp, #16]
	ldr	r3, [sp, #4]
	str	r0, [r1, ip]
	ldr	r1, [sl, #-1048]
	strh	r6, [r1, r2]	@ movhi
	b	.L1221
.L1217:
	sub	r3, r3, #1
	uxth	r3, r3
.L1216:
	ldr	r6, [sp, #4]
	sxth	r2, r3
	cmp	r2, r6
	bgt	.L1222
	mov	r6, r9
.L1221:
	sxth	r3, r3
	cmp	r3, #0
	blt	.L1210
	ldr	r2, .L1258+8
	ldr	r0, .L1258
	ldrh	r1, [r4, r2]
	ldr	r0, [r0, #3936]
	rsb	r0, r1, r0
	sub	r0, r0, #1
	sxth	r0, r0
	cmp	r3, r0
	bgt	.L1210
	add	r1, r1, #1
	strh	r1, [r4, r2]	@ movhi
	ldr	r2, [r4, #-1044]
	ldr	r1, [r5, #4]
	str	r1, [r2, r3, asl #2]
	mov	r3, r3, asl #1
	ldr	r2, [r4, #-1048]
	b	.L1253
.L1256:
	ldr	r1, .L1258
	movw	r3, #3928
	mov	ip, #3952
	mov	r9, r6
	ldrh	r0, [r1, r3]
	ldrh	r1, [r1, ip]
	sub	r2, r0, #1
	ldr	ip, [r4, #-928]
	uxth	r3, r2
	rsb	r2, r1, r2
	str	r2, [sp, #4]
	b	.L1223
.L1229:
	mov	r6, r2, asl #2
	ldr	r7, [r5, #4]
	str	r6, [sp, #16]
	ldr	r6, [ip, r2, asl #2]
	cmp	r7, r6
	bls	.L1224
	ldr	ip, [ip, #0]
	mov	r6, r9
	cmp	ip, #0
	bne	.L1225
	cmp	r1, r0
	addne	r1, r1, #1
	ldrne	ip, .L1258
	movne	r0, #3952
	strneh	r1, [ip, r0]	@ movhi
.L1225:
	sxth	ip, r3
	mov	r0, #0
	str	ip, [sp, #20]
	ldr	r9, .L1258
	str	r3, [sp, #4]
	b	.L1226
.L1227:
	ldr	r7, [r4, #-928]
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
.L1226:
	ldr	ip, [sp, #20]
	sxth	r1, r0
	cmp	r1, ip
	bne	.L1227
	ldr	r1, [sl, #-928]
	mov	r2, r2, asl #1
	ldr	r0, [r5, #4]
	ldr	ip, [sp, #16]
	ldr	r3, [sp, #4]
	str	r0, [r1, ip]
	ldr	r1, .L1258
	ldr	r1, [r1, #3956]
	strh	r6, [r1, r2]	@ movhi
	b	.L1228
.L1224:
	sub	r3, r3, #1
	uxth	r3, r3
.L1223:
	ldr	r6, [sp, #4]
	sxth	r2, r3
	cmp	r2, r6
	bgt	.L1229
	mov	r6, r9
.L1228:
	sxth	r3, r3
	cmp	r3, #0
	blt	.L1210
	ldr	r2, .L1258
	movw	ip, #3928
	mov	r1, #3952
	ldrh	ip, [r2, ip]
	ldrh	r0, [r2, r1]
	sub	ip, ip, #1
	rsb	ip, r0, ip
	sxth	ip, ip
	cmp	r3, ip
	bgt	.L1210
	add	r0, r0, #1
	strh	r0, [r2, r1]	@ movhi
	ldr	r0, [r5, #4]
	ldr	r1, [r4, #-928]
	str	r0, [r1, r3, asl #2]
	mov	r3, r3, asl #1
	ldr	r2, [r2, #3956]
.L1253:
	strh	r6, [r2, r3]	@ movhi
	b	.L1210
.L1212:
	ldr	r3, .L1258+16
	movw	ip, #65535
	ldrh	r2, [r4, r3]
	cmp	r2, ip
	streqh	r6, [r4, r3]	@ movhi
	beq	.L1254
	ldr	r3, .L1258+20
	movw	r1, #65535
	ldrh	r0, [r4, r3]
	cmp	r0, r1
	beq	.L1231
	mov	r1, #1
	bl	FtlFreeSysBlkQueueIn
.L1231:
	ldr	r3, [r5, #4]
	ldr	r2, [sl, #-1764]
	cmp	r2, r3
	ldr	r3, .L1258+20
	strcsh	r6, [r4, r3]	@ movhi
	bcs	.L1210
	ldr	r2, .L1258+16
	ldrh	r1, [r4, r2]
	strh	r6, [r4, r2]	@ movhi
	strh	r1, [r4, r3]	@ movhi
.L1254:
	ldr	r3, [r5, #4]
	str	r3, [r4, #-1764]
	b	.L1210
.L1257:
	mov	r0, r6
	b	.L1255
.L1207:
	ldr	r3, .L1258
	mov	r0, r6
	ldrb	r3, [r3, #928]	@ zero_extendqisi2
	cmp	r3, #0
	moveq	r1, #1
	beq	.L1252
.L1255:
	mov	r1, #0
.L1252:
	bl	FtlFreeSysBlkQueueIn
.L1210:
	ldr	r6, [sp, #12]
	add	r8, r8, #36
	add	r3, r6, #1
	uxth	r3, r3
	str	r3, [sp, #12]
	cmp	r3, fp
	bne	.L1234
.L1203:
	ldr	ip, [sp, #8]
	add	r3, ip, #1
	uxth	r3, r3
	str	r3, [sp, #8]
.L1200:
	ldr	r6, .L1258
	movw	r3, #3850
	ldr	ip, [sp, #8]
	ldrh	r3, [r6, r3]
	cmp	r3, ip
	bhi	.L1248
	ldr	r1, .L1258+4
	ldr	r2, [r1, #-1048]
	ldrh	r3, [r2, #0]
	cmp	r3, #0
	bne	.L1236
	ldr	r0, .L1258+8
	ldrh	r1, [r1, r0]
	cmp	r1, #0
	ldrne	r0, [r6, #3936]
	bne	.L1237
	b	.L1236
.L1241:
	mov	r1, r1, asl #1
	ldrh	r1, [r2, r1]
	cmp	r1, #0
	beq	.L1238
	ldr	r7, .L1258
	sxth	r6, r3
	ldr	r1, .L1258+4
	mov	r5, #0
	b	.L1239
.L1240:
	ldr	ip, [r1, #-1048]
	mov	r0, r2, asl #1
	rsb	r4, r6, r2
	add	r3, r3, #1
	ldrh	sl, [ip, r0]
	mov	r8, r4, asl #1
	uxth	r3, r3
	strh	sl, [ip, r8]	@ movhi
	ldr	ip, [r1, #-1044]
	ldr	r2, [ip, r2, asl #2]
	str	r2, [ip, r4, asl #2]
	ldr	r2, [r1, #-1048]
	strh	r5, [r2, r0]	@ movhi
.L1239:
	ldr	r0, [r7, #3936]
	sxth	r2, r3
	cmp	r2, r0
	bcc	.L1240
	b	.L1236
.L1238:
	add	r3, r3, #1
	uxth	r3, r3
.L1237:
	sxth	r1, r3
	cmp	r1, r0
	bcc	.L1241
.L1236:
	ldr	r2, .L1258
	ldr	r1, [r2, #3956]
	ldrh	r3, [r1, #0]
	cmp	r3, #0
	bne	.L1242
	mov	r0, #3952
	ldrh	r0, [r2, r0]
	cmp	r0, #0
	movwne	r0, #3928
	ldrneh	r2, [r2, r0]
	bne	.L1243
	b	.L1242
.L1247:
	mov	ip, r0, asl #1
	ldrh	ip, [r1, ip]
	cmp	ip, #0
	beq	.L1244
	ldr	r1, .L1258
	movw	r8, #3928
	ldr	r7, .L1258+4
	mov	r6, #0
	b	.L1245
.L1246:
	ldr	r4, [r1, #3956]
	mov	ip, r2, asl #1
	rsb	r5, r0, r2
	add	r3, r3, #1
	ldrh	r9, [r4, ip]
	mov	sl, r5, asl #1
	uxth	r3, r3
	strh	r9, [r4, sl]	@ movhi
	ldr	r4, [r7, #-928]
	ldr	r2, [r4, r2, asl #2]
	str	r2, [r4, r5, asl #2]
	ldr	r2, [r1, #3956]
	strh	r6, [r2, ip]	@ movhi
.L1245:
	ldrh	ip, [r1, r8]
	sxth	r2, r3
	cmp	r2, ip
	blt	.L1246
	b	.L1242
.L1244:
	add	r3, r3, #1
	uxth	r3, r3
.L1243:
	sxth	r0, r3
	cmp	r0, r2
	blt	.L1247
.L1242:
	mov	r0, #0
	add	sp, sp, #28
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, sl, fp, pc}
.L1259:
	.align	2
.L1258:
	.word	.LANCHOR0
	.word	.LANCHOR2
	.word	-1052
	.word	.LANCHOR2-1772
	.word	-1772
	.word	-1768
	.fnend
	.size	FtlScanSysBlk, .-FtlScanSysBlk
	.align	2
	.global	FtlVendorPartRead
	.type	FtlVendorPartRead, %function
FtlVendorPartRead:
	.fnstart
	@ args = 0, pretend = 0, frame = 48
	@ frame_needed = 0, uses_anonymous_args = 0
	ldr	r3, .L1270
	stmfd	sp!, {r4, r5, r6, r7, r8, r9, sl, fp, lr}
	.save {r4, r5, r6, r7, r8, r9, sl, fp, lr}
	mov	r8, r2
	movw	r2, #3916
	mov	r5, r1
	ldrh	r9, [r3, r2]
	movw	r2, #3902
	ldrh	r2, [r3, r2]
	add	r1, r1, r0
	.pad #52
	sub	sp, sp, #52
	mov	r6, r0
	cmp	r1, r2
	mvnhi	r7, #0
	bhi	.L1261
	mov	r9, r0, lsr r9
	mov	r7, #0
	ldr	sl, .L1270+4
	mov	r9, r9, asl #2
	b	.L1262
.L1267:
	ldr	r3, [sl, #-924]
	mov	r0, r6
	ldr	r1, .L1270
	ldr	fp, [r3, r9]
	movw	r3, #3914
	ldrh	r4, [r1, r3]
	mov	r1, r4
	bl	__aeabi_uidivmod
	uxth	r3, r1
	rsb	r4, r3, r4
	uxth	r4, r4
	cmp	r4, r5
	uxthhi	r4, r5
	cmp	fp, #0
	beq	.L1264
	ldr	r1, .L1270+4
	add	r0, sp, #12
	str	fp, [sp, #16]
	ldr	r2, [r1, #-972]
	mov	r1, #1
	str	r3, [sp, #4]
	str	r2, [sp, #20]
	mov	r2, #0
	str	r2, [sp, #24]
	mov	r2, r1
	bl	FlashReadPages
	ldr	r2, [sp, #12]
	ldr	r3, [sp, #4]
	mov	r0, r8
	cmn	r2, #1
	ldr	r1, [sl, #-972]
	moveq	r7, r2
	add	r1, r1, r3, asl #9
	mov	r2, r4, asl #9
	bl	memcpy
	b	.L1266
.L1264:
	mov	r0, r8
	mov	r1, fp
	mov	r2, r4, asl #9
	bl	ftl_memset
.L1266:
	rsb	r5, r4, r5
	add	r6, r6, r4
	add	r8, r8, r4, asl #9
	add	r9, r9, #4
.L1262:
	cmp	r5, #0
	bne	.L1267
.L1261:
	mov	r0, r7
	add	sp, sp, #52
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, sl, fp, pc}
.L1271:
	.align	2
.L1270:
	.word	.LANCHOR0
	.word	.LANCHOR2
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
	ldr	r5, .L1276
	mov	r1, #1
	ldr	r2, .L1276+4
	bl	FtlVendorPartRead
	ldr	r4, .L1276+8
	ldr	r3, [r5, #-1708]
	cmp	r3, r4
	beq	.L1273
	ldr	r0, .L1276+4
	mov	r1, #0
	mov	r2, #512
	bl	ftl_memset
	str	r4, [r5, #-1708]
.L1273:
	ldr	r2, [r5, #-1708]
	ldr	r3, .L1276
	cmp	r2, r4
	bne	.L1274
	ldr	r2, [r3, #-1620]
	str	r2, [r3, #-1860]
	ldr	r2, [r3, #-1616]
	str	r2, [r3, #-1856]
	ldr	r2, [r3, #-1700]
	str	r2, [r3, #-1864]
	ldr	r2, [r3, #-1696]
	str	r2, [r3, #-1876]
	ldr	r2, [r3, #-1692]
	str	r2, [r3, #-1884]
	ldr	r2, [r3, #-1688]
	str	r2, [r3, #-1868]
	ldr	r2, [r3, #-1680]
	str	r2, [r3, #-1840]
	ldr	r2, [r3, #-1676]
	str	r2, [r3, #-2084]
	ldr	r2, [r3, #-1672]
	str	r2, [r3, #-1880]
	ldr	r2, [r3, #-1668]
	str	r2, [r3, #-1872]
	ldr	r2, [r3, #-1664]
	str	r2, [r3, #-1832]
	ldr	r2, [r3, #-1660]
	str	r2, [r3, #-1828]
	ldr	r2, [r3, #-1648]
	str	r2, [r3, #-1712]
.L1274:
	ldr	r4, .L1276
	mov	r3, #0
	ldr	r2, [r4, #-1640]
	str	r3, [r4, #-1008]
	ldr	r3, .L1276+12
	cmp	r2, r3
	bne	.L1275
	mov	r3, #1
	ldr	r0, .L1276+16
	str	r3, [r4, #-1888]
	ldr	r1, .L1276+20
	bl	printk
.L1275:
	ldr	r3, .L1276+24
	movw	r2, #3898
	ldr	r0, [r4, #-1844]
	ldrh	r1, [r3, r2]
	ldr	r2, [r4, #-1840]
	mla	r0, r0, r1, r2
	movw	r2, #3848
	ldrh	r1, [r3, r2]
	bl	__aeabi_uidiv
	str	r0, [r4, #-1836]
	ldmfd	sp!, {r3, r4, r5, pc}
.L1277:
	.align	2
.L1276:
	.word	.LANCHOR2
	.word	.LANCHOR2-1708
	.word	1179929683
	.word	305432421
	.word	.LC74
	.word	.LC97
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
	ldr	r4, .L1280
	ldr	r5, .L1280+4
	ldr	r2, [r4, #-940]
	ldrh	r1, [r4, r5]
	bl	FtlVendorPartRead
	ldr	r3, [r4, #-940]
	ldr	r2, [r3, #0]
	ldr	r3, .L1280+8
	cmp	r2, r3
	beq	.L1279
	ldr	r1, .L1280+12
	ldr	r0, .L1280+16
	bl	printk
	ldrh	r2, [r4, r5]
	ldr	r0, [r4, #-940]
	mov	r1, #0
	mov	r2, r2, asl #9
	bl	ftl_memset
.L1279:
	mov	r0, #0
	ldmfd	sp!, {r3, r4, r5, pc}
.L1281:
	.align	2
.L1280:
	.word	.LANCHOR2
	.word	-944
	.word	1112818501
	.word	.LC98
	.word	.LC74
	.fnend
	.size	FtlLoadEctTbl, .-FtlLoadEctTbl
	.align	2
	.global	load_l2p_region
	.type	load_l2p_region, %function
load_l2p_region:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	stmfd	sp!, {r3, r4, r5, r6, r7, lr}
	.save {r3, r4, r5, r6, r7, lr}
	mov	r6, r0
	ldr	r4, .L1285
	mov	r5, #12
	ldr	r3, [r4, #-1040]
	ldr	r7, [r3, r0, asl #2]
	cmp	r7, #0
	bne	.L1283
	mul	r5, r5, r1
	ldr	r3, [r4, #-1904]
	ldr	r2, .L1285+4
	mov	r1, #255
	add	r3, r3, r5
	ldr	r0, [r3, #8]
	movw	r3, #3918
	ldrh	r2, [r2, r3]
	bl	ftl_memset
	ldr	r3, [r4, #-1904]
	strh	r6, [r3, r5]	@ movhi
	ldr	r3, [r4, #-1904]
	add	r5, r3, r5
	str	r7, [r5, #4]
	b	.L1284
.L1283:
	mul	r5, r5, r1
	ldr	r3, [r4, #-1904]
	mov	r1, #1
	add	r0, r4, #156
	mov	r2, r1
	str	r7, [r4, #160]
	add	r3, r3, r5
	ldr	r3, [r3, #8]
	str	r3, [r4, #164]
	ldr	r3, [r4, #-956]
	str	r3, [r4, #168]
	bl	FlashReadPages
	ldr	r3, [r4, #-1904]
	mov	r1, #0
	add	r2, r3, r5
	str	r1, [r2, #4]
	strh	r6, [r3, r5]	@ movhi
.L1284:
	mov	r0, #0
	ldmfd	sp!, {r3, r4, r5, r6, r7, pc}
.L1286:
	.align	2
.L1285:
	.word	.LANCHOR2
	.word	.LANCHOR0
	.fnend
	.size	load_l2p_region, .-load_l2p_region
	.align	2
	.global	FtlGetLastWrittenPage
	.type	FtlGetLastWrittenPage, %function
FtlGetLastWrittenPage:
	.fnstart
	@ args = 0, pretend = 0, frame = 104
	@ frame_needed = 0, uses_anonymous_args = 0
	cmp	r1, #1
	ldr	r3, .L1298
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
	bne	.L1291
	b	.L1296
.L1294:
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
	bne	.L1292
	ldr	r3, [sp, #44]
	cmn	r3, #1
	bne	.L1292
	ldr	r3, [sp, #4]
	cmn	r3, #1
	subne	r4, r6, #1
	uxthne	r4, r4
	bne	.L1296
.L1292:
	add	r6, r6, #1
	uxth	r8, r6
.L1296:
	sxth	r6, r8
	sxth	r3, r4
	cmp	r6, r3
	ble	.L1294
.L1291:
	sxth	r0, r4
	add	sp, sp, #108
	ldmfd	sp!, {r4, r5, r6, r7, r8, sl, pc}
.L1299:
	.align	2
.L1298:
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
	ldr	r4, .L1315
	mov	r1, r5
	ldr	r2, .L1315+4
	ldr	r7, .L1315+8
	ldr	r3, [r4, #-956]
	ldr	r0, [r4, #-2068]
	str	r5, [r4, #164]
	str	r3, [r4, #168]
	movw	r3, #3848
	ldrh	r2, [r2, r3]
	mov	r2, r2, asl #1
	bl	ftl_memset
	ldrh	r0, [r4, r7]
	movw	r3, #65535
	cmp	r0, r3
	beq	.L1313
	mov	r1, #1
	bl	FtlGetLastWrittenPage
	ldr	r3, .L1315+12
	add	r2, r0, #1
	mov	r6, r0
	strh	r2, [r4, r3]	@ movhi
	b	.L1302
.L1305:
	ldrh	r2, [r4, r7]
	mov	r1, #1
	ldr	r0, .L1315+16
	orr	r3, r3, r2, asl #10
	str	r3, [r4, #160]
	ldr	r3, [r4, #-2080]
	mov	r2, r1
	str	r3, [r4, #164]
	bl	FlashReadPages
	ldr	r3, [r4, #156]
	cmn	r3, #1
	beq	.L1303
	ldr	r3, [r4, #-2080]
	ldr	r2, [r3, #0]
	ldr	r3, .L1315+20
	cmp	r2, r3
	bne	.L1303
	ldr	r3, [r4, #-956]
	ldrh	r2, [r3, #0]
	movw	r3, #61604
	cmp	r2, r3
	beq	.L1304
.L1303:
	sub	r5, r5, #1
	uxth	r5, r5
.L1302:
	add	r3, r5, r6
	sxth	r3, r3
	cmp	r3, #0
	bge	.L1305
.L1304:
	ldr	r4, .L1315
	mov	r2, #48
	ldr	r5, .L1315+4
	movw	r6, #3848
	sub	r0, r4, #1824
	ldr	r7, [r4, #164]
	mov	r1, r7
	bl	memcpy
	ldrh	r2, [r5, r6]
	add	r1, r7, #48
	ldr	r0, [r4, #-2068]
	mov	r2, r2, asl #1
	bl	memcpy
	ldrh	r1, [r5, r6]
	ldr	r3, [r4, #164]
	ldr	r0, [r4, #-1892]
	mov	r2, r1, lsr #3
	add	r1, r1, #24
	add	r2, r2, #4
	mov	r1, r1, lsr #1
	add	r1, r3, r1, asl #2
	bl	memcpy
	ldr	r2, [r4, #-1824]
	ldr	r3, .L1315+20
	cmp	r2, r3
	bne	.L1313
	ldr	r3, .L1315+24
	ldr	r2, .L1315+28
	ldrb	r1, [r4, #-1814]	@ zero_extendqisi2
	ldrh	r3, [r4, r3]
	strh	r3, [r4, r2]	@ movhi
	movw	r2, #3862
	ldrh	r2, [r5, r2]
	cmp	r1, r2
	bne	.L1313
	movw	r2, #3908
	movw	r1, #3914
	ldrh	r2, [r5, r2]
	ldrh	r1, [r5, r1]
	ldr	r0, [r5, #3852]
	mul	r2, r3, r2
	str	r3, [r4, #192]
	str	r2, [r5, #3968]
	mul	r2, r1, r2
	str	r2, [r5, #3948]
	movw	r2, #3982
	ldrh	r2, [r5, r2]
	rsb	r0, r2, r0
	rsb	r0, r3, r0
	mov	r3, #3840
	ldrh	r1, [r5, r3]
	movw	r5, #65535
	bl	__aeabi_uidiv
	ldr	r3, .L1315+32
	ldr	r1, .L1315+36
	strh	r0, [r4, r3]	@ movhi
	sub	r3, r3, #34
	ldrh	r2, [r4, r3]
	ldr	r3, .L1315+40
	strh	r2, [r4, r3]	@ movhi
	add	r3, r3, #240
	ldrh	r3, [r4, r3]
	mov	r0, r3, lsr #6
	and	r3, r3, #63
	strb	r3, [r4, #-2042]
	ldrb	r3, [r4, #-1813]	@ zero_extendqisi2
	strh	r0, [r4, r1]	@ movhi
	mvn	r1, #0
	ldr	r0, .L1315+44
	strb	r3, [r4, #-2040]
	ldr	r3, .L1315+48
	strh	r1, [r4, r3]	@ movhi
	mov	r3, #0
	ldr	r1, .L1315+52
	strh	r3, [r4, r0]	@ movhi
	strb	r3, [r4, #-1754]
	ldrh	r0, [r4, r1]
	ldr	r1, .L1315+56
	strb	r3, [r4, #-1752]
	str	r3, [r4, #-1864]
	strh	r0, [r4, r1]	@ movhi
	add	r1, r1, #196
	ldr	r0, .L1315+60
	ldrh	r1, [r4, r1]
	mov	ip, r1, lsr #6
	and	r1, r1, #63
	strb	r1, [r4, #-1994]
	ldrb	r1, [r4, #-1812]	@ zero_extendqisi2
	strh	ip, [r4, r0]	@ movhi
	strb	r1, [r4, #-1992]
	ldr	r1, .L1315+64
	ldrh	r0, [r4, r1]
	ldr	r1, .L1315+68
	strh	r0, [r4, r1]	@ movhi
	add	r1, r1, #152
	ldr	r0, .L1315+72
	ldrh	r1, [r4, r1]
	mov	ip, r1, lsr #6
	and	r1, r1, #63
	strb	r1, [r4, #-1946]
	ldrb	r1, [r4, #-1811]	@ zero_extendqisi2
	strh	ip, [r4, r0]	@ movhi
	strb	r1, [r4, #-1944]
	str	r3, [r4, #-1876]
	ldr	r1, [r4, #-1792]
	str	r3, [r4, #-1884]
	str	r3, [r4, #-1868]
	str	r3, [r4, #-1840]
	str	r3, [r4, #-1832]
	str	r3, [r4, #-1872]
	ldr	r3, [r4, #-1784]
	str	r1, [r4, #-1844]
	ldr	r1, [r4, #-1852]
	cmp	r3, r1
	strhi	r3, [r4, #-1852]
	ldr	r4, .L1315
	ldr	r3, [r4, #-1788]
	ldr	r1, [r4, #-1848]
	cmp	r3, r1
	strhi	r3, [r4, #-1848]
	cmp	r2, r5
	beq	.L1308
	ldr	r0, .L1315+76
	bl	make_superblock
.L1308:
	ldr	r3, .L1315+56
	ldrh	r3, [r4, r3]
	cmp	r3, r5
	beq	.L1309
	ldr	r0, .L1315+80
	bl	make_superblock
.L1309:
	ldr	r4, .L1315
	movw	r5, #65535
	ldr	r3, .L1315+68
	ldrh	r3, [r4, r3]
	cmp	r3, r5
	beq	.L1310
	sub	r0, r4, #1952
	bl	make_superblock
.L1310:
	ldr	r3, .L1315+48
	ldrh	r3, [r4, r3]
	cmp	r3, r5
	beq	.L1314
	ldr	r0, .L1315+84
	bl	make_superblock
	mov	r0, #0
	ldmfd	sp!, {r3, r4, r5, r6, r7, pc}
.L1313:
	mvn	r0, #0
	ldmfd	sp!, {r3, r4, r5, r6, r7, pc}
.L1314:
	mov	r0, #0
	ldmfd	sp!, {r3, r4, r5, r6, r7, pc}
.L1316:
	.align	2
.L1315:
	.word	.LANCHOR2
	.word	.LANCHOR0
	.word	-1772
	.word	-1770
	.word	.LANCHOR2+156
	.word	1179929683
	.word	-1816
	.word	-1766
	.word	-1776
	.word	-2046
	.word	-2048
	.word	-1758
	.word	-1760
	.word	-1806
	.word	-2000
	.word	-1998
	.word	-1802
	.word	-1952
	.word	-1950
	.word	.LANCHOR2-2048
	.word	.LANCHOR2-2000
	.word	.LANCHOR2-1760
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
	ldr	r6, .L1333
	ldr	r8, .L1333+4
	ldr	r4, [r6, #-956]
	mov	r7, r6
	str	r3, [r6, #164]
	str	r4, [r6, #168]
	bl	FtlBbtMemInit
	mov	r3, #3904
	ldrh	r5, [r8, r3]
	sub	r5, r5, #1
	uxth	r5, r5
	b	.L1318
.L1322:
	mov	r1, #1
	mov	r3, r5, asl #10
	ldr	r0, .L1333+8
	mov	r2, r1
	str	r3, [r6, #160]
	bl	FlashReadPages
	ldr	r3, [r6, #156]
	cmn	r3, #1
	bne	.L1319
	ldr	r3, [r6, #160]
	mov	r1, #1
	ldr	r0, .L1333+8
	mov	r2, r1
	add	r3, r3, #1
	str	r3, [r6, #160]
	bl	FlashReadPages
.L1319:
	ldr	r3, [r7, #156]
	cmn	r3, #1
	beq	.L1320
	ldrh	r2, [r4, #0]
	movw	r3, #61649
	cmp	r2, r3
	bne	.L1320
	ldr	r3, .L1333+4
	movw	r2, #3976
	strh	r5, [r3, r2]	@ movhi
	ldr	r2, [r4, #4]
	str	r2, [r3, #3984]
	movw	r2, #3980
	ldrh	r1, [r4, #8]
	strh	r1, [r3, r2]	@ movhi
	b	.L1321
.L1320:
	sub	r5, r5, #1
	uxth	r5, r5
.L1318:
	mov	r3, #3904
	ldrh	r3, [r8, r3]
	sub	r3, r3, #48
	cmp	r5, r3
	bgt	.L1322
.L1321:
	ldr	r5, .L1333+4
	movw	r8, #3976
	movw	r3, #65535
	ldrh	r2, [r5, r8]
	cmp	r2, r3
	beq	.L1332
	movw	r6, #3980
	ldrh	r2, [r5, r6]
	cmp	r2, r3
	beq	.L1324
	ldr	r7, .L1333
	mov	r1, #1
	mov	r2, r2, asl #10
	add	r0, r7, #156
	str	r2, [r7, #160]
	mov	r2, r1
	bl	FlashReadPages
	ldr	r3, [r7, #156]
	cmn	r3, #1
	beq	.L1324
	ldrh	r2, [r4, #0]
	movw	r3, #61649
	cmp	r2, r3
	bne	.L1324
	ldr	r3, [r4, #4]
	ldr	r2, [r5, #3984]
	cmp	r3, r2
	strhi	r3, [r5, #3984]
	ldrhih	r2, [r5, r6]
	ldrhih	r3, [r4, #8]
	strhih	r2, [r5, r8]	@ movhi
	strhih	r3, [r5, r6]	@ movhi
.L1324:
	ldr	r8, .L1333+4
	movw	r3, #3976
	mov	r1, #1
	mov	r6, #0
	ldr	r5, .L1333
	ldrh	r0, [r8, r3]
	bl	FtlGetLastWrittenPage
	movw	r3, #3978
	add	r2, r0, #1
	mov	r7, r0
	strh	r2, [r8, r3]	@ movhi
	b	.L1325
.L1328:
	movw	r2, #3976
	mov	r1, #1
	ldrh	r2, [r8, r2]
	ldr	r0, .L1333+8
	orr	r3, r3, r2, asl #10
	str	r3, [r5, #160]
	ldr	r3, [r5, #-2080]
	mov	r2, r1
	str	r3, [r5, #164]
	bl	FlashReadPages
	ldr	r3, [r5, #156]
	cmn	r3, #1
	beq	.L1326
	ldrh	r2, [r4, #0]
	movw	r3, #61649
	cmp	r2, r3
	beq	.L1327
.L1326:
	sub	r6, r6, #1
	uxth	r6, r6
.L1325:
	add	r3, r6, r7
	sxth	r3, r3
	cmp	r3, #0
	bge	.L1328
.L1327:
	ldr	r3, .L1333+4
	movw	r2, #3982
	ldrh	r1, [r4, #10]
	ldrh	r0, [r4, #12]
	strh	r1, [r3, r2]	@ movhi
	movw	r2, #65535
	cmp	r0, r2
	beq	.L1329
	ldr	r2, [r3, #3836]
	cmp	r0, r2
	beq	.L1329
	movw	r1, #3850
	ldrh	r3, [r3, r1]
	mov	r3, r3, lsr #2
	cmp	r2, r3
	bcs	.L1329
	cmp	r0, r3
	bcs	.L1329
	bl	FtlSysBlkNumInit
.L1329:
	ldr	r5, .L1333+12
	mov	r4, #0
	movw	r7, #3862
	ldr	r6, .L1333
	sub	r8, r5, #4000
	b	.L1330
.L1331:
	ldr	r3, .L1333+16
	ldr	r1, [r6, #164]
	ldr	r0, [r5, #4]!
	ldrh	r2, [r6, r3]
	mov	r2, r2, asl #2
	mla	r1, r4, r2, r1
	bl	memcpy
	add	r4, r4, #1
.L1330:
	ldrh	r3, [r8, r7]
	cmp	r4, r3
	bcc	.L1331
	mov	r0, #0
	ldmfd	sp!, {r4, r5, r6, r7, r8, pc}
.L1332:
	mvn	r0, #0
	ldmfd	sp!, {r4, r5, r6, r7, r8, pc}
.L1334:
	.align	2
.L1333:
	.word	.LANCHOR2
	.word	.LANCHOR0
	.word	.LANCHOR2+156
	.word	.LANCHOR0+4000
	.word	-1036
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
	ldr	r3, .L1342
	movw	fp, #3862
	ldr	r7, .L1342+4
	ldr	r6, .L1342+8
	ldr	r2, [r3, #-2080]
	ldr	r8, [r3, #-956]
	str	r2, [r3, #164]
	str	r8, [r3, #168]
	b	.L1336
.L1341:
	mvn	r3, #0
	strh	r3, [r7], #2	@ movhi
	mov	r3, #3904
	ldr	sl, .L1342
	ldrh	r4, [r6, r3]
	movw	r9, #61664
	sub	r4, r4, #1
	uxth	r4, r4
	b	.L1337
.L1340:
	mla	r3, r3, r5, r4
	mov	r1, #1
	ldr	r0, .L1342+12
	mov	r2, r1
	mov	r3, r3, asl #10
	str	r3, [sl, #160]
	bl	FlashReadPages
	ldr	r3, [sl, #156]
	cmn	r3, #1
	beq	.L1338
	ldrh	r3, [r8, #0]
	cmp	r3, r9
	streqh	r4, [r7, #-2]	@ movhi
	beq	.L1339
.L1338:
	sub	r4, r4, #1
	uxth	r4, r4
.L1337:
	mov	r3, #3904
	ldrh	r3, [r6, r3]
	sub	r2, r3, #16
	cmp	r4, r2
	bgt	.L1340
.L1339:
	add	r5, r5, #1
.L1336:
	ldrh	r3, [r6, fp]
	cmp	r5, r3
	bcc	.L1341
	mov	r0, #0
	ldmfd	sp!, {r3, r4, r5, r6, r7, r8, r9, sl, fp, pc}
.L1343:
	.align	2
.L1342:
	.word	.LANCHOR2
	.word	.LANCHOR0+3988
	.word	.LANCHOR0
	.word	.LANCHOR2+156
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
	ldr	r3, .L1363
	mov	r6, r0
	mov	r8, #0
	ldr	r7, .L1363+4
	ldrb	fp, [r3, #2837]	@ zero_extendqisi2
	b	.L1345
.L1352:
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
	bcs	.L1347
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
.L1347:
	add	r8, r8, #1
	add	r6, r6, #36
.L1345:
	cmp	r8, sl
	bne	.L1352
	ldr	r3, [sp, #12]
	cmp	r3, #0
	movne	r6, #0
	ldrne	r5, .L1363+8
	bne	.L1353
	b	.L1354
.L1359:
	ldr	r3, [r4, #0]
	cmn	r3, #1
	bne	.L1355
	ldr	r1, [r4, #4]
	ldr	r0, .L1363+12
	bl	printk
	b	.L1356
.L1355:
	rsb	r3, r6, sl
	mov	r1, r9
	add	r2, sp, #20
	mov	r0, r4
	uxtb	r3, r3
	str	r3, [sp, #0]
	add	r3, sp, #24
	bl	LogAddr2PhyAddr
	ldr	r2, [r5, #196]
	mov	r3, #0
	mov	lr, r4
	add	ip, sp, #28
	str	r3, [r2, #0]
	ldr	r2, [r5, #200]
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
	ldr	r3, [r5, #196]
	str	r3, [sp, #36]
	ldr	r3, [r5, #200]
	str	r3, [sp, #40]
	bl	FlashReadPages
	ldr	r7, [sp, #28]
	cmn	r7, #1
	bne	.L1357
	ldr	r0, .L1363+16
	ldr	r1, [r4, #4]
	bl	printk
	str	r7, [r4, #0]
.L1357:
	ldr	r3, [r4, #12]
	cmp	r3, #0
	beq	.L1358
	ldr	r2, [r3, #0]
	ldr	r3, [r5, #200]
	ldr	r3, [r3, #0]
	cmp	r2, r3
	beq	.L1358
	ldr	r0, .L1363+20
	ldr	r1, [r4, #4]
	bl	printk
	mvn	r3, #0
	str	r3, [r4, #0]
.L1358:
	ldr	r3, [r4, #8]
	cmp	r3, #0
	beq	.L1356
	ldr	r2, [r3, #0]
	ldr	r3, [r5, #196]
	ldr	r3, [r3, #0]
	cmp	r2, r3
	beq	.L1356
	ldr	r0, .L1363+24
	ldr	r1, [r4, #4]
	bl	printk
	mvn	r3, #0
	str	r3, [r4, #0]
.L1356:
	add	r6, r6, #1
	add	r4, r4, #36
.L1353:
	cmp	r6, sl
	bne	.L1359
.L1354:
	mov	r0, #0
	add	sp, sp, #68
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, sl, fp, pc}
.L1364:
	.align	2
.L1363:
	.word	.LANCHOR1
	.word	.LANCHOR0
	.word	.LANCHOR2
	.word	.LC99
	.word	.LC100
	.word	.LC101
	.word	.LC102
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
	ldr	r5, .L1393
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
	ldr	ip, .L1393+4
	ldrb	ip, [ip, #2837]	@ zero_extendqisi2
	str	ip, [sp, #12]
	beq	.L1366
	bl	FlashProgSlc2KPages
	b	.L1367
.L1380:
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
	bcs	.L1369
	ldrb	r1, [r5, #3833]	@ zero_extendqisi2
	add	r2, fp, r2, asl #4
	cmp	r1, #0
	ldr	r2, [r2, #3636]
	moveq	r8, #0
	cmp	r2, #0
	beq	.L1371
	cmp	r3, #1
	bne	.L1372
	ldr	r0, [r5, #3012]
	bl	NandcIqrWaitFlashReady
.L1372:
	ldrb	r0, [sp, #32]	@ zero_extendqisi2
	bl	FlashWaitCmdDone
.L1371:
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
	bne	.L1374
	bl	NandcWaitFlashReady
	b	.L1375
.L1374:
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
.L1375:
	ldr	r2, [sp, #20]
	sub	r3, r2, #1
	cmp	r3, #6
	bhi	.L1376
	add	r3, r5, r4
	ldrb	r3, [r3, #3756]	@ zero_extendqisi2
	cmp	r3, #0
	beq	.L1376
	mov	r0, r4
	ldrb	r1, [r5, #1]	@ zero_extendqisi2
	ldr	r2, .L1393+8
	mov	r3, #0
	bl	HynixSetRRPara
.L1376:
	mov	r0, r4
	bl	NandcFlashCs
	cmp	sl, #1
	mov	r0, r4
	bne	.L1377
	ldrb	r3, [r5, #928]	@ zero_extendqisi2
	cmp	r3, #0
	beq	.L1377
	bl	flash_enter_slc_mode
	b	.L1378
.L1377:
	bl	flash_exit_slc_mode
.L1378:
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
	beq	.L1379
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
.L1379:
	mov	r0, r4
	ldr	r1, [sp, #28]
	bl	FlashProgSecondCmd
	mov	r0, r4
	bl	NandcFlashDeCs
	add	r6, r6, r8
.L1369:
	add	r6, r6, #1
.L1366:
	ldr	r3, [sp, #8]
	cmp	r6, r3
	bcc	.L1380
	ldr	r5, .L1393
	mov	r4, #0
	ldr	r6, .L1393+12
	ldr	r0, [r5, #3012]
	bl	NandcIqrWaitFlashReady
	b	.L1381
.L1383:
	uxtb	r0, r4
	bl	FlashWaitCmdDone
	cmp	sl, #1
	bne	.L1382
	ldrb	r3, [r5, #928]	@ zero_extendqisi2
	cmp	r3, #0
	beq	.L1382
	ldrb	r0, [r6, r4, asl #4]	@ zero_extendqisi2
	bl	flash_exit_slc_mode
.L1382:
	add	r4, r4, #1
.L1381:
	ldrb	r3, [r5, #3762]	@ zero_extendqisi2
	cmp	r4, r3
	bcc	.L1383
	ldr	r2, [sp, #16]
	cmp	r2, #0
	ldreq	r0, [sp, #16]
	beq	.L1367
	mov	r5, #0
	ldr	r4, .L1393+16
	ldr	r6, [sp, #8]
	b	.L1384
.L1389:
	ldr	r3, [r9, #0]
	cmn	r3, #1
	bne	.L1385
	ldr	r1, [r9, #4]
	ldr	r0, .L1393+20
	bl	printk
	b	.L1386
.L1385:
	rsb	r3, r5, r6
	mov	r1, sl
	add	r2, sp, #28
	mov	r0, r9
	uxtb	r3, r3
	str	r3, [sp, #0]
	add	r3, sp, #32
	bl	LogAddr2PhyAddr
	ldr	r2, [r4, #196]
	mov	r3, #0
	mov	lr, r9
	add	ip, sp, #36
	str	r3, [r2, #0]
	ldr	r2, [r4, #200]
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
	ldr	r3, [r4, #196]
	str	r3, [sp, #44]
	ldr	r3, [r4, #200]
	str	r3, [sp, #48]
	bl	FlashReadPages
	ldr	r7, [sp, #36]
	cmn	r7, #1
	bne	.L1387
	ldr	r0, .L1393+24
	ldr	r1, [r9, #4]
	bl	printk
	str	r7, [r9, #0]
.L1387:
	ldr	r3, [r9, #12]
	cmp	r3, #0
	beq	.L1388
	ldr	r2, [r3, #0]
	ldr	r3, [r4, #200]
	ldr	r3, [r3, #0]
	cmp	r2, r3
	beq	.L1388
	ldr	r0, .L1393+28
	ldr	r1, [r9, #4]
	bl	printk
	mvn	r3, #0
	str	r3, [r9, #0]
.L1388:
	ldr	r3, [r9, #8]
	cmp	r3, #0
	beq	.L1386
	ldr	r2, [r3, #0]
	ldr	r3, [r4, #196]
	ldr	r3, [r3, #0]
	cmp	r2, r3
	beq	.L1386
	ldr	r0, .L1393+32
	ldr	r1, [r9, #4]
	bl	printk
	mvn	r3, #0
	str	r3, [r9, #0]
.L1386:
	add	r5, r5, #1
	add	r9, r9, #36
.L1384:
	cmp	r5, r6
	bne	.L1389
	mov	r0, #0
.L1367:
	add	sp, sp, #76
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, sl, fp, pc}
.L1394:
	.align	2
.L1393:
	.word	.LANCHOR0
	.word	.LANCHOR1
	.word	.LANCHOR0+4
	.word	.LANCHOR0+3628
	.word	.LANCHOR2
	.word	.LC99
	.word	.LC100
	.word	.LC101
	.word	.LC102
	.fnend
	.size	FlashProgPages, .-FlashProgPages
	.align	2
	.global	FtlVpcTblFlush
	.type	FtlVpcTblFlush, %function
FtlVpcTblFlush:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	stmfd	sp!, {r3, r4, r5, r6, r7, r8, sl, lr}
	.save {r3, r4, r5, r6, r7, r8, sl, lr}
	mov	r6, #0
	ldr	r4, .L1406
	mov	r1, #255
	ldr	r8, .L1406+4
	movw	sl, #3848
	ldr	r7, .L1406+8
	ldr	r3, [r4, #-2080]
	ldr	r5, [r4, #-956]
	str	r3, [r4, #164]
	ldrh	r3, [r4, r8]
	str	r5, [r4, #168]
	str	r6, [r5, #12]
	strh	r3, [r5, #2]	@ movhi
	ldr	r3, .L1406+12
	strh	r3, [r5, #0]	@ movhi
	ldr	r3, [r4, #-1764]
	str	r6, [r5, #8]
	str	r3, [r5, #4]
	ldr	r3, .L1406+16
	str	r3, [r4, #-1824]
	ldr	r3, .L1406+20
	str	r3, [r4, #-1820]
	ldr	r3, .L1406+24
	ldrh	r2, [r4, r3]
	sub	r3, r3, #50
	strh	r2, [r4, r3]	@ movhi
	movw	r3, #3862
	ldrh	r3, [r7, r3]
	strb	r3, [r4, #-1814]
	ldr	r3, .L1406+28
	ldrh	r2, [r4, r3]
	add	r3, r3, #238
	strh	r2, [r4, r3]	@ movhi
	sub	r3, r3, #236
	ldrh	r2, [r4, r3]
	ldrb	r3, [r4, #-2042]	@ zero_extendqisi2
	orr	r2, r3, r2, asl #6
	ldr	r3, .L1406+32
	strh	r2, [r4, r3]	@ movhi
	ldrb	r3, [r4, #-2040]	@ zero_extendqisi2
	strb	r3, [r4, #-1813]
	ldr	r3, .L1406+36
	ldrh	r2, [r4, r3]
	add	r3, r3, #194
	strh	r2, [r4, r3]	@ movhi
	sub	r3, r3, #192
	ldrh	r2, [r4, r3]
	ldrb	r3, [r4, #-1994]	@ zero_extendqisi2
	orr	r2, r3, r2, asl #6
	ldr	r3, .L1406+40
	strh	r2, [r4, r3]	@ movhi
	ldrb	r3, [r4, #-1992]	@ zero_extendqisi2
	strb	r3, [r4, #-1812]
	ldr	r3, .L1406+44
	ldrh	r2, [r4, r3]
	add	r3, r3, #150
	strh	r2, [r4, r3]	@ movhi
	sub	r3, r3, #148
	ldr	r0, [r4, #164]
	ldrh	r2, [r4, r3]
	ldrb	r3, [r4, #-1946]	@ zero_extendqisi2
	orr	r2, r3, r2, asl #6
	ldr	r3, .L1406+48
	strh	r2, [r4, r3]	@ movhi
	ldrb	r3, [r4, #-1944]	@ zero_extendqisi2
	strb	r3, [r4, #-1811]
	ldr	r3, [r4, #-1844]
	str	r3, [r4, #-1792]
	ldr	r3, [r4, #-1852]
	str	r3, [r4, #-1784]
	ldr	r3, [r4, #-1848]
	str	r3, [r4, #-1788]
	movw	r3, #3918
	ldrh	r2, [r7, r3]
	bl	ftl_memset
	sub	r1, r4, #1824
	mov	r2, #48
	ldr	r0, [r4, #164]
	bl	memcpy
	ldrh	r2, [r7, sl]
	ldr	r0, [r4, #164]
	ldr	r1, [r4, #-2068]
	mov	r2, r2, asl #1
	add	r0, r0, #48
	bl	memcpy
	ldrh	r2, [r7, sl]
	ldr	r3, [r4, #164]
	movw	sl, #65535
	add	r0, r2, #24
	ldr	r1, [r4, #-1892]
	mov	r2, r2, lsr #3
	mov	r0, r0, lsr #1
	add	r2, r2, #4
	add	r0, r3, r0, asl #2
	bl	memcpy
	mov	r0, r6
	bl	FtlUpdateVaildLpn
.L1405:
	ldr	r3, [r4, #-2080]
	mov	r1, #1
	ldr	r7, .L1406+52
	ldrh	r2, [r4, r8]
	str	r3, [r4, #164]
	ldr	r3, [r4, #-956]
	ldr	r0, .L1406+56
	str	r3, [r4, #168]
	ldrh	r3, [r4, r7]
	orr	r3, r3, r2, asl #10
	mov	r2, r1
	str	r3, [r4, #160]
	mov	r3, r1
	bl	FlashProgPages
	ldr	r1, .L1406+8
	movw	r3, #3910
	ldrh	r2, [r4, r7]
	ldrh	r3, [r1, r3]
	sub	r3, r3, #1
	cmp	r2, r3
	blt	.L1397
	ldr	r3, .L1406+60
	ldrh	r2, [r4, r8]
	ldrh	sl, [r4, r3]
	strh	r2, [r4, r3]	@ movhi
	mov	r3, #0
	strh	r3, [r4, r7]	@ movhi
	bl	FtlFreeSysBlkQueueOut
	ldr	r3, [r4, #-1852]
	mov	r1, #1
	add	r2, r3, #1
	str	r2, [r4, #-1852]
	str	r3, [r4, #-1764]
	mov	r2, r0, asl #10
	strh	r0, [r4, r8]	@ movhi
	str	r2, [r4, #160]
	mov	r2, r1
	str	r3, [r5, #4]
	mov	r3, r1
	strh	r0, [r5, #2]	@ movhi
	ldr	r0, .L1406+56
	bl	FlashProgPages
.L1397:
	ldr	r2, .L1406+52
	ldr	r1, [r4, #156]
	ldrh	r3, [r4, r2]
	cmn	r1, #1
	add	r3, r3, #1
	uxth	r3, r3
	strh	r3, [r4, r2]	@ movhi
	bne	.L1398
	cmp	r3, #1
	add	r6, r6, #1
	ldreq	r1, .L1406+8
	movweq	r3, #3910
	uxth	r6, r6
	ldreqh	r3, [r1, r3]
	subeq	r3, r3, #1
	streqh	r3, [r4, r2]	@ movhi
	cmp	r6, #3
	bls	.L1405
	ldr	r3, .L1406
	mov	r2, r6
	ldr	r0, .L1406+64
	ldr	r1, [r3, #160]
	bl	printk
.L1401:
	b	.L1401
.L1398:
	cmp	r3, #1
	beq	.L1405
	movw	r3, #65535
	cmp	sl, r3
	beq	.L1402
	mov	r0, sl
	mov	r1, #1
	bl	FtlFreeSysBlkQueueIn
.L1402:
	mov	r0, #0
	ldmfd	sp!, {r3, r4, r5, r6, r7, r8, sl, pc}
.L1407:
	.align	2
.L1406:
	.word	.LANCHOR2
	.word	-1772
	.word	.LANCHOR0
	.word	-3932
	.word	1179929683
	.word	1342177344
	.word	-1766
	.word	-2048
	.word	-1808
	.word	-2000
	.word	-1804
	.word	-1952
	.word	-1800
	.word	-1770
	.word	.LANCHOR2+156
	.word	-1768
	.word	.LC103
	.fnend
	.size	FtlVpcTblFlush, .-FtlVpcTblFlush
	.align	2
	.global	FtlBbmTblFlush
	.type	FtlBbmTblFlush, %function
FtlBbmTblFlush:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	stmfd	sp!, {r0, r1, r2, r4, r5, r6, r7, r8, r9, sl, fp, lr}
	.save {r0, r1, r2, r4, r5, r6, r7, r8, r9, sl, fp, lr}
	mov	r1, #0
	ldr	r7, .L1420
	mov	r6, #0
	ldr	sl, .L1420+4
	movw	r9, #3862
	ldr	r3, [r7, #-956]
	add	r8, sl, #4000
	ldr	r0, [r7, #-2080]
	str	r3, [r7, #168]
	movw	r3, #3918
	str	r0, [r7, #164]
	ldrh	r2, [sl, r3]
	bl	ftl_memset
	b	.L1409
.L1410:
	ldr	r3, .L1420+8
	ldr	r1, [r8, #4]!
	ldrh	r2, [r7, r3]
	ldr	r3, [r7, #164]
	mul	r0, r2, r6
	mov	r2, r2, asl #2
	add	r6, r6, #1
	add	r0, r3, r0, asl #2
	bl	memcpy
.L1409:
	ldrh	r3, [sl, r9]
	ldr	r4, .L1420+4
	cmp	r6, r3
	ldr	r5, .L1420
	blt	.L1410
	ldr	r6, [r5, #168]
	mov	r1, #255
	mov	r2, #16
	mov	r7, #0
	mov	fp, r7
	mov	r0, r6
	bl	ftl_memset
	ldr	r3, .L1420+12
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
	b	.L1419
.L1416:
	mov	fp, #1
.L1419:
	ldr	r3, [r5, #-2080]
	movw	r8, #3976
	ldr	r4, .L1420+4
	mov	r1, #0
	movw	sl, #3978
	str	r1, [r5, #156]
	str	r3, [r5, #164]
	movw	r9, #3980
	ldr	r3, [r5, #-956]
	ldrh	r1, [r4, r8]
	ldrh	r2, [r4, sl]
	str	r3, [r5, #168]
	ldrh	r0, [r6, #10]
	orr	r3, r2, r1, asl #10
	str	r3, [r5, #160]
	ldrh	r3, [r4, r9]
	str	r0, [sp, #0]
	ldr	r0, .L1420+16
	bl	printk
	mov	r1, #1
	mov	r2, r1
	mov	r3, r1
	ldr	r0, .L1420+20
	bl	FlashProgPages
	movw	r3, #3910
	ldrh	r3, [r4, r3]
	ldrh	r2, [r4, sl]
	sub	r3, r3, #1
	cmp	r2, r3
	blt	.L1412
	ldr	r3, [r4, #3984]
	mov	r1, #0	@ movhi
	ldrh	r2, [r4, r8]
	add	r3, r3, #1
	ldr	r0, [r5, #-2092]
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
	str	r3, [r5, #160]
	str	r3, [r0, #4]
	bl	FlashEraseBlocks
	mov	r1, #1
	ldr	r0, .L1420+20
	mov	r2, r1
	mov	r3, r1
	bl	FlashProgPages
.L1412:
	ldr	r2, .L1420+4
	movw	r3, #3978
	ldrh	r1, [r2, r3]
	add	r1, r1, #1
	strh	r1, [r2, r3]	@ movhi
	ldr	r3, [r5, #156]
	cmn	r3, #1
	bne	.L1413
	add	r7, r7, #1
	ldr	r0, .L1420+24
	ldr	r1, [r5, #160]
	uxth	r7, r7
	bl	printk
	cmp	r7, #3
	bls	.L1419
	ldr	r3, .L1420
	mov	r2, r7
	ldr	r0, .L1420+28
	ldr	r1, [r3, #160]
	bl	printk
.L1415:
	b	.L1415
.L1413:
	cmp	fp, #0
	beq	.L1416
	mov	r0, #0
	ldmfd	sp!, {r1, r2, r3, r4, r5, r6, r7, r8, r9, sl, fp, pc}
.L1421:
	.align	2
.L1420:
	.word	.LANCHOR2
	.word	.LANCHOR0
	.word	-1036
	.word	-3887
	.word	.LC104
	.word	.LANCHOR2+156
	.word	.LC105
	.word	.LC106
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
	ldr	r4, .L1432
	ldr	r3, .L1432+4
	ldrh	r3, [r4, r3]
	cmp	r3, #0
	movne	r6, #0
	movne	sl, r4
	bne	.L1423
	b	.L1424
.L1430:
	add	r3, r3, r6
	mov	r1, r9
	mov	r5, #0
	ldr	r7, .L1432+4
	ldrb	r0, [r3, #3866]	@ zero_extendqisi2
	bl	V2P_block
	mov	r8, r0
	b	.L1425
.L1429:
	add	r3, r4, r5, asl #1
	sub	r3, r3, #1136
	ldrh	r3, [r3, #0]
	cmp	r3, r8
	bne	.L1426
	mov	r1, r8
	ldr	r0, .L1432+8
	bl	printk
	mov	r0, r8
	bl	FtlBbmMapBadBlock
	bl	FtlBbmTblFlush
	ldrh	r2, [r4, r7]
	mov	r3, r5
	b	.L1427
.L1428:
	add	r1, r3, #1
	add	r3, r4, r3, asl #1
	sub	r3, r3, #1136
	add	r0, r4, r1, asl #1
	sub	r0, r0, #1136
	ldrh	r0, [r0, #0]
	strh	r0, [r3, #0]	@ movhi
	uxth	r3, r1
.L1427:
	cmp	r3, r2
	bcc	.L1428
	sub	r2, r2, #1
	strh	r2, [sl, r7]	@ movhi
.L1426:
	add	r5, r5, #1
	uxth	r5, r5
.L1425:
	ldrh	r3, [r4, r7]
	cmp	r3, r5
	bhi	.L1429
	add	r6, r6, #1
	uxth	r6, r6
.L1423:
	ldr	r3, .L1432+12
	mov	r2, #3840
	ldrh	r2, [r3, r2]
	cmp	r2, r6
	bhi	.L1430
	bl	FtlGcReFreshBadBlk
.L1424:
	mov	r0, #0
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, sl, pc}
.L1433:
	.align	2
.L1432:
	.word	.LANCHOR2
	.word	-1138
	.word	.LC107
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
	ldr	r3, .L1442
	mov	r4, r0
	ldr	r1, [r3, #-2068]
	ldrh	r2, [r1, r2]
	cmp	r2, #0
	bne	.L1435
	ldr	r1, .L1442+4
	ldrh	r0, [r3, r1]
	cmp	r0, r4
	mvneq	r2, #0
	streqh	r2, [r3, r1]	@ movhi
	beq	.L1437
	ldr	r1, .L1442+8
	ldrh	r1, [r3, r1]
	cmp	r1, r4
	beq	.L1438
	ldr	r1, .L1442+12
	ldrh	r1, [r3, r1]
	cmp	r1, r4
	beq	.L1438
	ldr	r1, .L1442+16
	ldrh	r3, [r3, r1]
	cmp	r3, r4
	beq	.L1438
.L1437:
	mov	r1, r4
	ldr	r0, .L1442+20
	bl	List_remove_node
	ldr	r5, .L1442
	ldr	r3, .L1442+24
	mov	r0, r4
	ldrh	r2, [r5, r3]
	sub	r2, r2, #1
	strh	r2, [r5, r3]	@ movhi
	bl	free_data_superblock
	mov	r0, r4
	bl	FtlGcFreeBadSuperBlk
	mov	r2, #1
	b	.L1438
.L1435:
	bl	List_update_data_list
	mov	r2, #0
.L1438:
	mov	r0, r2
	ldmfd	sp!, {r3, r4, r5, pc}
.L1443:
	.align	2
.L1442:
	.word	.LANCHOR2
	.word	-1760
	.word	-2048
	.word	-2000
	.word	-1952
	.word	.LANCHOR2-2072
	.word	-2060
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
	beq	.L1445
	ldr	r2, .L1450
	mov	r3, r0, asl #1
	ldr	r2, [r2, #-2068]
	ldrh	r4, [r2, r3]
	cmp	r4, #0
	subne	r4, r4, #1
	strneh	r4, [r2, r3]	@ movhi
	bne	.L1445
	ldr	r0, .L1450+4
	mov	r1, r5
	mov	r2, r4
	bl	printk
	mov	r0, r4
	ldmfd	sp!, {r3, r4, r5, r6, r7, pc}
.L1445:
	ldr	r7, .L1450
	movw	r3, #65535
	ldr	r6, .L1450+8
	ldrh	r0, [r7, r6]
	cmp	r0, r3
	streqh	r5, [r7, r6]	@ movhi
	moveq	r0, #0
	ldmeqfd	sp!, {r3, r4, r5, r6, r7, pc}
	cmp	r0, r5
	beq	.L1449
	bl	update_vpc_list
	strh	r5, [r7, r6]	@ movhi
	adds	r0, r0, #0
	movne	r0, #1
	ldmfd	sp!, {r3, r4, r5, r6, r7, pc}
.L1449:
	mov	r0, #0
	ldmfd	sp!, {r3, r4, r5, r6, r7, pc}
.L1451:
	.align	2
.L1450:
	.word	.LANCHOR2
	.word	.LC108
	.word	-1028
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
	ldr	r5, .L1464
	ldr	r7, .L1464+4
	add	r3, r0, r3, asl #1
	ldrh	r3, [r3, #16]
	b	.L1453
.L1454:
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
.L1453:
	cmp	r3, r6
	beq	.L1454
	ldrb	r2, [r4, #8]	@ zero_extendqisi2
	cmp	r2, #1
	bne	.L1456
	ldrb	r2, [r5, #928]	@ zero_extendqisi2
	cmp	r2, #0
	bne	.L1456
	ldrh	r2, [r4, #2]
	add	r2, r7, r2, asl #1
	sub	r2, r2, #916
	ldrh	r2, [r2, #0]
	cmp	r2, r6
	bne	.L1456
	ldrh	r3, [r4, #4]
	ldrh	r0, [r4, #0]
	sub	r3, r3, #1
	strh	r3, [r4, #4]	@ movhi
	bl	decrement_vpc_count
	b	.L1454
.L1456:
	ldrh	r6, [r4, #2]
	movw	r5, #65535
	mov	r7, r5
	orr	r6, r6, r3, asl #10
	ldrh	r3, [r4, #4]
	sub	r3, r3, #1
	strh	r3, [r4, #4]	@ movhi
.L1461:
	ldr	r2, .L1464
	mov	r3, #3840
	ldrh	r2, [r2, r3]
	ldrb	r3, [r4, #6]	@ zero_extendqisi2
.L1458:
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
	beq	.L1458
	strb	r3, [r4, #6]
	ldrb	r3, [r4, #8]	@ zero_extendqisi2
	cmp	r3, #1
	bne	.L1459
	ldr	r3, .L1464
	ldrb	r2, [r3, #928]	@ zero_extendqisi2
	cmp	r2, #0
	ldrh	r2, [r4, #2]
	bne	.L1462
	ldr	r3, .L1464+4
	add	r2, r3, r2, asl #1
	sub	r2, r2, #916
	ldrh	r3, [r2, #0]
	cmp	r3, r7
	bne	.L1459
	ldrh	r3, [r4, #4]
	cmp	r3, #0
	beq	.L1459
	sub	r3, r3, #1
	ldrh	r0, [r4, #0]
	strh	r3, [r4, #4]	@ movhi
	bl	decrement_vpc_count
	b	.L1461
.L1462:
	movw	r1, #3910
	ldrh	r1, [r3, r1]
	cmp	r2, r1
	bcc	.L1459
	ldr	r1, .L1464+4
	ldrh	r2, [r4, #0]
	ldrh	r0, [r4, #4]
	ldr	r1, [r1, #-2068]
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
.L1459:
	mov	r0, r6
	ldmfd	sp!, {r3, r4, r5, r6, r7, pc}
.L1465:
	.align	2
.L1464:
	.word	.LANCHOR0
	.word	.LANCHOR2
	.fnend
	.size	get_new_active_ppa, .-get_new_active_ppa
	.align	2
	.type	FtlSlcSuperblockCheck.part.16, %function
FtlSlcSuperblockCheck.part.16:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	stmfd	sp!, {r3, r4, r5, r6, r7, lr}
	.save {r3, r4, r5, r6, r7, lr}
	mov	r4, r0
	ldrb	r3, [r0, #6]	@ zero_extendqisi2
	movw	r6, #65535
	ldr	r5, .L1472
	ldr	r7, .L1472+4
	add	r3, r0, r3, asl #1
	ldrh	r3, [r3, #16]
	b	.L1467
.L1468:
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
.L1467:
	cmp	r3, r6
	beq	.L1468
	ldrb	r2, [r4, #8]	@ zero_extendqisi2
	cmp	r2, #1
	bne	.L1470
	ldrb	r3, [r5, #928]	@ zero_extendqisi2
	cmp	r3, #0
	bne	.L1470
	ldrh	r3, [r4, #2]
	add	r3, r7, r3, asl #1
	sub	r3, r3, #916
	ldrh	r3, [r3, #0]
	cmp	r3, r6
	bne	.L1470
	ldrh	r3, [r4, #4]
	ldrh	r0, [r4, #0]
	sub	r3, r3, #1
	strh	r3, [r4, #4]	@ movhi
	bl	decrement_vpc_count
	ldrh	r3, [r4, #4]
	cmp	r3, #0
	bne	.L1468
	ldrh	r2, [r4, #2]
	strb	r3, [r4, #6]
	add	r2, r2, #1
	strh	r2, [r4, #2]	@ movhi
	ldmfd	sp!, {r3, r4, r5, r6, r7, pc}
.L1470:
	ldr	r3, .L1472
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
	ldr	r1, .L1472+4
	ldrh	r2, [r4, #0]
	ldrh	r0, [r4, #4]
	ldr	r1, [r1, #-2068]
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
.L1473:
	.align	2
.L1472:
	.word	.LANCHOR0
	.word	.LANCHOR2
	.fnend
	.size	FtlSlcSuperblockCheck.part.16, .-FtlSlcSuperblockCheck.part.16
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
	b	FtlSlcSuperblockCheck.part.16
	.fnend
	.size	FtlSlcSuperblockCheck, .-FtlSlcSuperblockCheck
	.align	2
	.global	allocate_data_superblock
	.type	allocate_data_superblock, %function
allocate_data_superblock:
	.fnstart
	@ args = 0, pretend = 0, frame = 16
	@ frame_needed = 0, uses_anonymous_args = 0
	stmfd	sp!, {r4, r5, r6, r7, r8, r9, sl, fp, lr}
	.save {r4, r5, r6, r7, r8, r9, sl, fp, lr}
	.pad #20
	sub	sp, sp, #20
	ldr	r4, .L1509
	mov	r5, r0
	ldr	r8, .L1509+4
.L1506:
	ldr	r3, .L1509+8
	cmp	r5, r3
	bne	.L1478
	ldrh	r2, [r4, r8]
	ldr	r3, [r4, #-1184]
	mov	r1, r2, lsr #1
	mul	ip, r3, r2
	add	r0, r1, #1
	add	r0, r0, ip, lsr #2
	ldr	ip, [r4, #-1888]
	cmp	ip, #0
	uxth	r0, r0
	beq	.L1500
	ldr	ip, [r4, #-1828]
	cmp	ip, #29
	bhi	.L1500
	cmp	ip, #2
	bls	.L1505
	tst	r2, #1
	beq	.L1479
	cmp	r3, #0
	moveq	r1, r3
	beq	.L1480
	b	.L1479
.L1478:
	ldrb	r3, [r5, #8]	@ zero_extendqisi2
	cmp	r3, #1
	bne	.L1505
	ldr	r3, .L1509+12
	movw	r2, #3860
	ldrh	r2, [r3, r2]
	cmp	r2, #1
	beq	.L1505
	ldrb	r3, [r3, #928]	@ zero_extendqisi2
	cmp	r3, #0
	bne	.L1505
	ldr	r2, [r4, #-1888]
	ldrh	r3, [r4, r8]
	cmp	r2, #0
	mov	r1, r3, lsr #3
	beq	.L1479
	ldr	r2, [r4, #-1828]
	cmp	r2, #1
	movls	r1, #7
	mulls	r1, r1, r3
	movls	r1, r1, lsr #3
	b	.L1479
.L1500:
	mov	r1, r0
.L1479:
	cmp	r1, #0
	subne	r1, r1, #1
	uxthne	r1, r1
	b	.L1480
.L1505:
	mov	r1, #0
.L1480:
	ldr	r0, .L1509+16
	bl	List_pop_index_node
	ldrh	r3, [r4, r8]
	sub	r3, r3, #1
	strh	r3, [r4, r8]	@ movhi
	uxth	r6, r0
	mov	r0, r5
	strh	r6, [r5, #0]	@ movhi
	bl	make_superblock
	ldrb	r3, [r5, #7]	@ zero_extendqisi2
	cmp	r3, #0
	beq	.L1481
	ldr	r2, .L1509+12
	mov	r3, #3840
	ldr	ip, [r4, #-2092]
	mov	fp, #0
	mov	sl, r5
	mov	r1, r5
	ldrh	r7, [r2, r3]
	mov	r0, fp
	mov	r3, ip
	mov	r2, fp
	stmia	sp, {r5, ip}
	b	.L1482
.L1481:
	ldr	r3, [r4, #-2068]
	mov	r6, r6, asl #1
	mvn	r2, #0
	strh	r2, [r3, r6]	@ movhi
	b	.L1506
.L1485:
	str	r0, [r3, #8]
	movw	r5, #65535
	str	r0, [r3, #12]
	ldrh	lr, [r1, #16]
	cmp	lr, r5
	beq	.L1484
	ldr	r5, [sp, #4]
	mov	ip, #36
	mov	lr, lr, asl #10
	mla	r9, ip, fp, r5
	add	fp, fp, #1
	uxth	fp, fp
	str	lr, [r9, #4]
.L1484:
	add	r2, r2, #1
	add	r3, r3, #36
	add	r1, r1, #2
	uxth	r2, r2
.L1482:
	cmp	r2, r7
	bne	.L1485
	ldr	r3, [r4, #-1888]
	ldr	r5, [sp, #0]
	cmp	r3, #0
	beq	.L1486
	ldr	r3, .L1509+20
	cmp	r5, r3
	bne	.L1486
	ldr	r2, [r4, #-2088]
	mov	r3, r6, asl #1
	ldrh	r3, [r2, r3]
	cmp	r3, #30
	movhi	r3, #0
	strhib	r3, [r4, #-2040]
.L1486:
	ldrb	r3, [r5, #8]	@ zero_extendqisi2
	ldr	r2, [r4, #-2088]
	cmp	r3, #0
	mov	r3, r6, asl #1
	ldrh	r1, [r2, r3]
	bne	.L1487
	cmp	r1, #0
	ldrne	ip, .L1509+12
	movwne	r0, #3898
	moveq	r1, #2
	ldrneh	r0, [ip, r0]
	addne	r1, r1, r0
	strh	r1, [r2, r3]	@ movhi
	ldr	r3, [r4, #-1844]
	mov	r0, r6
	mov	r1, #0
	add	r3, r3, #1
	str	r3, [r4, #-1844]
	b	.L1508
.L1487:
	add	r1, r1, #1
	strh	r1, [r2, r3]	@ movhi
	ldr	r3, [r4, #-1840]
	mov	r0, r6
	mov	r1, #1
	add	r3, r3, #1
	str	r3, [r4, #-1840]
.L1508:
	bl	ftl_set_blk_mode
	ldr	r3, [r4, #-2088]
	mov	r9, r6, asl #1
	ldr	r2, [r4, #-1832]
	ldr	r7, .L1509
	ldrh	r3, [r3, r9]
	cmp	r3, r2
	movw	r2, #3898
	strhi	r3, [r4, #-1832]
	ldr	r3, .L1509+12
	ldr	r0, [r7, #-1844]
	ldrh	r1, [r3, r2]
	ldr	r2, [r7, #-1840]
	mla	r0, r0, r1, r2
	movw	r2, #3848
	ldrh	r1, [r3, r2]
	bl	__aeabi_uidiv
	ldr	r3, [r7, #-940]
	ldr	r2, [r3, #16]
	add	r2, r2, #1
	str	r2, [r3, #16]
	ldr	r3, [r7, #-2092]
	str	r0, [r7, #-1836]
	mov	r7, #0
	b	.L1492
.L1493:
	add	r7, r7, #1
	ldr	r2, [r3, #-32]
	uxth	r7, r7
	bic	r2, r2, #1020
	bic	r2, r2, #3
	str	r2, [r3, #-32]
.L1492:
	cmp	r7, fp
	add	r3, r3, #36
	bne	.L1493
	mov	r2, r7
	ldr	r0, [r4, #-2092]
	ldrb	r1, [r5, #8]	@ zero_extendqisi2
	mov	fp, #0
	bl	FlashEraseBlocks
	mov	r2, #36
	mul	r2, r2, r7
	mov	r3, r6
	mov	ip, r9
	mov	r7, fp
	mov	r6, r5
	mov	r9, r2
	b	.L1494
.L1496:
	ldr	r1, [r4, #-2092]
	add	r0, r1, fp
	ldr	r5, [r1, fp]
	cmn	r5, #1
	bne	.L1495
	ldr	r0, [r0, #4]
	add	r7, r7, #1
	str	r3, [sp, #12]
	ubfx	r0, r0, #10, #16
	str	ip, [sp, #8]
	bl	FtlBbmMapBadBlock
	strh	r5, [sl, #16]	@ movhi
	ldrb	r1, [r6, #7]	@ zero_extendqisi2
	sub	r1, r1, #1
	strb	r1, [r6, #7]
	ldr	ip, [sp, #8]
	ldr	r3, [sp, #12]
.L1495:
	add	fp, fp, #36
	add	sl, sl, #2
.L1494:
	cmp	fp, r9
	bne	.L1496
	cmp	r7, #0
	mov	r5, r6
	mov	r9, ip
	mov	r6, r3
	beq	.L1497
	mov	r0, r3
	bl	update_multiplier_value
	bl	FtlBbmTblFlush
.L1497:
	ldrb	r3, [r5, #7]	@ zero_extendqisi2
	cmp	r3, #0
	ldreq	r3, .L1509
	mvneq	r2, #0
	ldreq	r3, [r3, #-2068]
	streqh	r2, [r3, r9]	@ movhi
	beq	.L1506
.L1498:
	ldr	r1, .L1509+12
	movw	r2, #3908
	mov	r0, #0
	strh	r0, [r5, #2]	@ movhi
	strb	r0, [r5, #6]
	ldrh	r2, [r1, r2]
	strh	r6, [r5, #0]	@ movhi
	mul	r2, r2, r3
	ldr	r3, .L1509
	ldr	r1, [r3, #-1852]
	uxth	r2, r2
	strh	r2, [r5, #4]	@ movhi
	str	r1, [r5, #12]
	add	r1, r1, #1
	str	r1, [r3, #-1852]
	ldrh	ip, [r5, #0]
	ldr	r1, [r3, #-2068]
	mov	r3, ip, asl #1
	strh	r2, [r1, r3]	@ movhi
	add	sp, sp, #20
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, sl, fp, pc}
.L1510:
	.align	2
.L1509:
	.word	.LANCHOR2
	.word	-2052
	.word	.LANCHOR2-1952
	.word	.LANCHOR0
	.word	.LANCHOR2-2056
	.word	.LANCHOR2-2048
	.fnend
	.size	allocate_data_superblock, .-allocate_data_superblock
	.align	2
	.global	FtlSuperblockPowerLostFix
	.type	FtlSuperblockPowerLostFix, %function
FtlSuperblockPowerLostFix:
	.fnstart
	@ args = 0, pretend = 0, frame = 40
	@ frame_needed = 0, uses_anonymous_args = 0
	ldr	r3, .L1518
	stmfd	sp!, {r4, r5, r6, r7, r8, lr}
	.save {r4, r5, r6, r7, r8, lr}
	mov	r4, r0
	ldrb	r7, [r3, #928]	@ zero_extendqisi2
	.pad #40
	sub	sp, sp, #40
	cmp	r7, #0
	beq	.L1512
	ldrb	r7, [r0, #8]	@ zero_extendqisi2
	sub	r1, r7, #1
	rsbs	r7, r1, #0
	adc	r7, r7, r1
.L1512:
	mov	r8, #7
	mvn	r6, #0
	ldr	r5, .L1518+4
	b	.L1513
.L1516:
	ldrh	r3, [r4, #4]
	cmp	r3, #0
	beq	.L1514
	mov	r0, r4
	bl	get_new_active_ppa
	cmn	r0, #1
	str	r0, [sp, #8]
	beq	.L1514
	ldr	r2, [r5, #-956]
	add	r0, sp, #4
	ldr	r3, [r5, #-2080]
	str	r6, [sp, #20]
	str	r2, [sp, #16]
	str	r3, [sp, #12]
	str	r6, [r2, #12]
	str	r6, [r2, #8]
	ldrh	r3, [r4, #0]
	strh	r3, [r2, #2]	@ movhi
	mov	r3, #0
	strh	r3, [r2, #0]	@ movhi
	ldr	r1, [r5, #-1848]
	cmn	r1, #2
	str	r1, [r2, #4]
	addne	r1, r1, #1
	moveq	r1, #0
	mov	r2, r7
	str	r1, [r5, #-1848]
	mov	r1, #1
	bl	FlashProgPages
	ldrh	r0, [r4, #0]
	bl	decrement_vpc_count
.L1513:
	subs	r8, r8, #1
	bne	.L1516
.L1514:
	ldr	r2, .L1518+4
	ldrh	r3, [r4, #0]
	ldrh	r1, [r4, #4]
	ldr	r2, [r2, #-2068]
	mov	r3, r3, asl #1
	ldrh	r0, [r2, r3]
	rsb	r1, r1, r0
	strh	r1, [r2, r3]	@ movhi
	ldr	r2, .L1518
	movw	r3, #3908
	ldrh	r3, [r2, r3]
	strh	r3, [r4, #2]	@ movhi
	mov	r3, #0
	strb	r3, [r4, #6]
	strh	r3, [r4, #4]	@ movhi
	add	sp, sp, #40
	ldmfd	sp!, {r4, r5, r6, r7, r8, pc}
.L1519:
	.align	2
.L1518:
	.word	.LANCHOR0
	.word	.LANCHOR2
	.fnend
	.size	FtlSuperblockPowerLostFix, .-FtlSuperblockPowerLostFix
	.align	2
	.global	FtlLowFormatEraseBlock
	.type	FtlLowFormatEraseBlock, %function
FtlLowFormatEraseBlock:
	.fnstart
	@ args = 0, pretend = 0, frame = 16
	@ frame_needed = 0, uses_anonymous_args = 0
	stmfd	sp!, {r4, r5, r6, r7, r8, r9, sl, fp, lr}
	.save {r4, r5, r6, r7, r8, r9, sl, fp, lr}
	.pad #20
	sub	sp, sp, #20
	ldr	r8, .L1550
	mov	r6, #0
	mov	r9, r0
	mov	sl, r1
	mov	r4, r6
	mov	r5, r6
	ldrb	r2, [r8, #928]	@ zero_extendqisi2
	mov	fp, #36
	str	r0, [r8, #3972]
	ldr	r7, .L1550+4
	str	r2, [sp, #8]
	b	.L1521
.L1525:
	mul	r3, fp, r6
	ldr	r2, [r7, #-2092]
	mov	r0, #0
	mov	r1, r9
	str	r0, [r2, r3]
	add	r3, r8, r6
	ldrb	r0, [r3, #3866]	@ zero_extendqisi2
	bl	V2P_block
	cmp	sl, #0
	str	r0, [sp, #4]
	beq	.L1522
	bl	IsBlkInVendorPart
	cmp	r0, #0
	bne	.L1523
.L1522:
	ldr	r0, [sp, #4]
	bl	FtlBbmIsBadBlock
	cmp	r0, #0
	addne	r5, r5, #1
	uxthne	r5, r5
	bne	.L1523
	ldr	r3, [r7, #-2092]
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
	ldr	r1, [r7, #-952]
	bic	r2, r2, #3
	add	r2, r1, r2
	str	r2, [r3, #12]
.L1523:
	add	r6, r6, #1
	uxth	r6, r6
.L1521:
	mov	r1, #3840
	ldrh	r3, [r8, r1]
	cmp	r3, r6
	bhi	.L1525
	cmp	r4, #0
	beq	.L1527
	ldr	r2, [sp, #8]
	mov	r8, #0
	ldr	r7, .L1550+4
	mov	fp, r8
	adds	r6, r2, #0
	mov	r2, r4
	movne	r6, #1
	ldr	r0, [r7, #-2092]
	mov	r1, r6
	bl	FlashEraseBlocks
.L1529:
	ldr	r3, [r7, #-2092]
	add	r2, r3, r8
	ldr	r3, [r3, r8]
	cmn	r3, #1
	bne	.L1528
	ldr	r0, [r2, #4]
	add	r5, r5, #1
	ubfx	r0, r0, #10, #16
	uxth	r5, r5
	bl	FtlBbmMapBadBlock
.L1528:
	add	fp, fp, #1
	add	r8, r8, #36
	uxth	fp, fp
	cmp	fp, r4
	bne	.L1529
	cmp	sl, #0
	beq	.L1542
	ldr	r3, .L1550
	movw	r2, #3910
	ldrh	r2, [r3, r2]
	ldrb	r3, [r3, #928]	@ zero_extendqisi2
	cmp	r3, #0
	str	r2, [sp, #8]
	moveq	r3, r2, lsr #2
	streq	r3, [sp, #4]
	moveq	r6, #1
	beq	.L1530
	b	.L1549
.L1542:
	mov	r0, #6
	mov	r1, #1
	stmib	sp, {r0, r1}
	b	.L1530
.L1549:
	mov	r6, #1
	str	r6, [sp, #4]
.L1530:
	mov	r8, #0
	mov	r7, r6
.L1538:
	mov	r6, #0
	ldr	fp, .L1550+4
	mov	r4, r6
	b	.L1531
.L1534:
	mov	r0, #36
	ldr	r2, [fp, #-2092]
	mul	r3, r0, r6
	mov	r1, #0
	str	r1, [r2, r3]
	mov	r1, r9
	ldr	r2, .L1550
	add	r3, r2, r6
	ldrb	r0, [r3, #3866]	@ zero_extendqisi2
	bl	V2P_block
	cmp	sl, #0
	str	r0, [sp, #12]
	beq	.L1532
	bl	IsBlkInVendorPart
	cmp	r0, #0
	bne	.L1533
.L1532:
	ldr	r0, [sp, #12]
	bl	FtlBbmIsBadBlock
	cmp	r0, #0
	bne	.L1533
	ldr	r3, [fp, #-2092]
	mov	r0, #36
	ldr	r1, [sp, #12]
	mla	r3, r0, r4, r3
	add	r2, r8, r1, asl #10
	ldr	r1, .L1550
	mov	r0, #3920
	str	r2, [r3, #4]
	ldr	r2, [fp, #-964]
	str	r2, [r3, #8]
	ldrh	r2, [r1, r0]
	mul	r2, r2, r4
	add	r4, r4, #1
	uxth	r4, r4
	add	r1, r2, #3
	cmp	r2, #0
	movlt	r2, r1
	ldr	r1, [fp, #-960]
	bic	r2, r2, #3
	add	r2, r1, r2
	str	r2, [r3, #12]
.L1533:
	add	r6, r6, #1
	uxth	r6, r6
.L1531:
	ldr	r0, .L1550
	mov	r2, #3840
	ldrh	r3, [r0, r2]
	cmp	r3, r6
	bhi	.L1534
	cmp	r4, #0
	beq	.L1527
	ldr	r6, .L1550+4
	mov	r1, r4
	mov	r2, r7
	mov	r3, #1
	mov	fp, #0
	ldr	r0, [r6, #-2092]
	bl	FlashProgPages
	mov	ip, sl
	mov	sl, r7
	mov	r7, r4
	mov	r4, fp
.L1537:
	ldr	r2, [r6, #-2092]
	add	r1, r2, fp
	ldr	r2, [r2, fp]
	cmp	r2, #0
	beq	.L1536
	ldr	r0, [r1, #4]
	add	r5, r5, #1
	str	ip, [sp, #0]
	ubfx	r0, r0, #10, #16
	uxth	r5, r5
	bl	FtlBbmMapBadBlock
	ldr	ip, [sp, #0]
.L1536:
	add	r4, r4, #1
	add	fp, fp, #36
	uxth	r4, r4
	cmp	r4, r7
	bne	.L1537
	ldr	r1, [sp, #4]
	mov	r4, r7
	ldr	r2, [sp, #8]
	mov	r7, sl
	add	r8, r8, r1
	mov	sl, ip
	uxth	r8, r8
	cmp	r8, r2
	bcc	.L1538
	ldr	fp, .L1550+4
	mov	r6, r7
	mov	r7, #0
	mov	r8, r7
.L1540:
	cmp	sl, #0
	beq	.L1539
	ldr	r3, [fp, #-2092]
	add	r2, r3, r7
	ldr	r3, [r3, r7]
	cmp	r3, #0
	bne	.L1539
	ldr	r0, [r2, #4]
	mov	r1, #1
	ubfx	r0, r0, #10, #16
	bl	FtlFreeSysBlkQueueIn
.L1539:
	add	r8, r8, #1
	add	r7, r7, #36
	uxth	r8, r8
	cmp	r8, r4
	bne	.L1540
	cmp	r9, #63
	movhi	r9, #0
	movls	r9, #1
	cmp	sl, #0
	moveq	sl, r9
	orrne	sl, r9, #1
	cmp	sl, #0
	beq	.L1527
	ldr	r3, .L1550+4
	mov	r1, r6
	mov	r2, r8
	ldr	r0, [r3, #-2092]
	bl	FlashEraseBlocks
.L1527:
	mov	r0, r5
	add	sp, sp, #20
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, sl, fp, pc}
.L1551:
	.align	2
.L1550:
	.word	.LANCHOR0
	.word	.LANCHOR2
	.fnend
	.size	FtlLowFormatEraseBlock, .-FtlLowFormatEraseBlock
	.align	2
	.type	FlashTestBlk.part.18, %function
FlashTestBlk.part.18:
	.fnstart
	@ args = 0, pretend = 0, frame = 104
	@ frame_needed = 0, uses_anonymous_args = 0
.L1553:
	stmfd	sp!, {r4, r5, lr}
	.save {r4, r5, lr}
	.pad #108
	sub	sp, sp, #108
	ldr	r4, .L1555
	mov	r5, r0
	mov	r1, #165
	add	r0, sp, #40
	mov	r2, #32
	str	r0, [sp, #16]
	ldr	r3, [r4, #112]
	mov	r5, r5, asl #10
	str	r3, [sp, #12]
	bl	ftl_memset
	ldr	r0, [r4, #112]
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
.L1556:
	.align	2
.L1555:
	.word	.LANCHOR2
	.fnend
	.size	FlashTestBlk.part.18, .-FlashTestBlk.part.18
	.align	2
	.global	FlashTestBlk
	.type	FlashTestBlk, %function
FlashTestBlk:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	@ link register save eliminated.
	ldr	r3, .L1559
	ldr	r3, [r3, #132]
	cmp	r0, r3
	bcc	.L1558
	b	FlashTestBlk.part.18
.L1558:
	mov	r0, #0
	bx	lr
.L1560:
	.align	2
.L1559:
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
	ldr	r3, .L1589
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
	ldr	r4, .L1589+4
	mul	fp, fp, r2
	ldr	r2, [r3, #3624]
	ldr	r0, [r4, #204]
	ldrb	r2, [r2, #24]	@ zero_extendqisi2
	str	r0, [sp, #28]
	ldr	r0, .L1589+8
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
	ldr	r0, [r4, #204]
	mov	r2, #4096
	mov	r1, #0
	bl	ftl_memset
	sub	r3, fp, #1
	mov	r2, fp, lsr #4
	ldr	r4, .L1589
	uxth	r3, r3
	str	r2, [sp, #20]
	str	r3, [sp, #24]
	b	.L1563
.L1585:
	ldr	r3, .L1589+4
	add	r2, r3, r5, asl #1
	ldrh	r6, [r2, #208]
	cmp	r6, #0
	bne	.L1564
	movw	r2, #3068
	ldr	r0, [r3, #124]
	ldrh	r2, [r4, r2]
	mov	r1, r6
	mov	r8, r6
	mov	r7, r6
	add	sl, r4, r5, asl #2
	mov	r2, r2, asl #9
	bl	ftl_memset
	add	r3, r4, r5
	ldrb	r9, [r3, #3764]	@ zero_extendqisi2
	b	.L1565
.L1573:
	mvn	r3, #0
	strb	r3, [sp, #34]
	strb	r3, [sp, #35]
	ldr	r3, [sp, #12]
	tst	r3, #1
	beq	.L1566
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
	bne	.L1566
	ldr	r1, [r4, #856]
	add	r2, sp, #35
	mov	r0, r9
	add	r1, r3, r1
	bl	FlashReadSpare
	ldrb	r3, [sp, #34]	@ zero_extendqisi2
	ldrb	r2, [sp, #35]	@ zero_extendqisi2
	and	r3, r2, r3
	strb	r3, [sp, #34]
.L1566:
	ldr	r0, [sp, #12]
	tst	r0, #2
	beq	.L1567
	ldr	r3, [r4, #3624]
	mov	r0, r9
	add	r2, sp, #35
	ldrh	r1, [r3, #10]
	ldr	r3, [sl, #3588]
	sub	r1, r1, #1
	add	r1, r1, r3
	add	r1, r1, r6
	bl	FlashReadSpare
.L1567:
	ldr	r3, [r4, #3624]
	ldrb	r3, [r3, #7]	@ zero_extendqisi2
	cmp	r3, #1
	cmpne	r3, #8
	ldrb	r3, [sp, #34]	@ zero_extendqisi2
	bne	.L1568
	cmp	r3, #0
	beq	.L1587
	ldrb	r0, [sp, #35]	@ zero_extendqisi2
	rsbs	r0, r0, #1
	movcc	r0, #0
	b	.L1569
.L1568:
	cmp	r3, #255
	bne	.L1587
	ldrb	r0, [sp, #35]	@ zero_extendqisi2
	subs	r0, r0, #255
	movne	r0, #1
	b	.L1569
.L1587:
	mov	r0, #1
.L1569:
	ldr	r2, [sp, #12]
	tst	r2, #4
	beq	.L1570
	ldr	r1, [sl, #3588]
	mov	r0, r9
	add	r1, r6, r1
	bl	SandiskProgTestBadBlock
.L1570:
	cmp	r0, #0
	beq	.L1571
	mov	r1, r5
	mov	r2, r8
	ldr	r0, .L1589+12
	add	r7, r7, #1
	bl	printk
	ldr	r3, [sp, #16]
	and	r0, r8, #31
	mov	ip, #1
	uxth	r7, r7
	mov	r2, r3, lsr #5
	ldr	r3, .L1589+4
	ldr	r3, [r3, #124]
	ldr	r1, [r3, r2, asl #2]
	orr	r1, r1, ip, asl r0
	ldr	r0, [sp, #20]
	str	r1, [r3, r2, asl #2]
	ldrb	r3, [r4, #3762]	@ zero_extendqisi2
	mul	r3, r3, r0
	cmp	r7, r3
	bgt	.L1572
.L1571:
	ldr	r2, [sp, #8]
	add	r8, r8, #1
	add	r6, r6, r2
.L1565:
	uxth	r3, r8
	str	r3, [sp, #16]
	cmp	r3, fp
	bcc	.L1573
.L1572:
	ldr	r0, .L1589+16
	mov	r1, r5
	mov	r2, r7
	bl	printk
	ldrb	r3, [r4, #3762]	@ zero_extendqisi2
	ldr	r0, [sp, #20]
	mul	r3, r3, r0
	cmp	r7, r3
	blt	.L1574
	movw	r3, #3068
	mov	r1, #0
	ldrh	r2, [r4, r3]
	ldr	r3, .L1589+4
	mov	r2, r2, asl #9
	ldr	r0, [r3, #124]
	bl	ftl_memset
.L1574:
	cmp	r5, #0
	bne	.L1575
	ldr	sl, .L1589+4
	mov	r8, r5
	mov	r7, r5
	ldrh	r9, [sl, #132]
	b	.L1576
.L1578:
	mov	r0, r6
	bl	FlashTestBlk
	cmp	r0, #0
	beq	.L1577
	mov	r1, r6
	ldr	r0, .L1589+20
	bl	printk
	ldr	r3, [sl, #124]
	mov	r2, r6, lsr #5
	mov	r0, #1
	add	r7, r7, r0
	and	r6, r6, #31
	ldr	r1, [r3, r2, asl #2]
	uxth	r7, r7
	orr	r6, r1, r0, asl r6
	str	r6, [r3, r2, asl #2]
.L1577:
	add	r8, r8, #1
	uxth	r8, r8
.L1576:
	add	r6, r8, r9
	ldrb	r3, [r4, #853]	@ zero_extendqisi2
	uxth	r6, r6
	cmp	r3, r6
	bhi	.L1578
	ldr	r6, [sp, #24]
	sub	r9, fp, #50
	ldr	sl, .L1589+4
	mov	r8, #1
	b	.L1579
.L1581:
	mov	r0, r6
	bl	FlashTestBlk
	cmp	r0, #0
	beq	.L1580
	mov	r1, r6
	ldr	r0, .L1589+20
	bl	printk
	ldr	r3, [sl, #124]
	mov	r2, r6, lsr #5
	and	r0, r6, #31
	ldr	r1, [r3, r2, asl #2]
	orr	r1, r1, r8, asl r0
	str	r1, [r3, r2, asl #2]
.L1580:
	sub	r6, r6, #1
	uxth	r6, r6
.L1579:
	cmp	r6, r9
	bgt	.L1581
	ldr	r3, .L1589+4
	ldrb	r1, [r4, #853]	@ zero_extendqisi2
	ldr	r2, [r3, #132]
	rsb	r2, r2, r1
	cmp	r7, r2
	bcc	.L1575
	movw	r2, #3068
	ldr	r0, [r3, #124]
	ldrh	r2, [r4, r2]
	mov	r1, #0
	mov	r2, r2, asl #9
	bl	ftl_memset
.L1575:
	mul	r8, fp, r5
	ldr	r7, .L1589+4
	ldr	r6, [sp, #24]
	ldr	r9, [sp, #28]
	add	sl, r7, r5, asl #1
.L1582:
	ldr	r0, .L1589+24
	mov	r1, r5
	mov	r2, r6
	bl	printk
	ldr	r3, [r7, #124]
	b	.L1583
.L1584:
	sub	r6, r6, #1
	uxth	r6, r6
.L1583:
	mov	r1, r6, lsr #5
	and	r2, r6, #31
	ldr	r1, [r3, r1, asl #2]
	mov	r2, r1, lsr r2
	ands	r2, r2, #1
	bne	.L1584
	ldr	r3, .L1589+28
	mov	r1, #1
	strh	r6, [sl, #208]	@ movhi
	add	r0, sp, #36
	strh	r6, [r9, #2]	@ movhi
	strh	r3, [r9, #0]	@ movhi
	ldr	r3, [r7, #124]
	strh	r2, [r9, #8]	@ movhi
	mov	r2, r1
	str	r3, [sp, #44]
	ldr	r3, [r7, #204]
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
	bne	.L1582
.L1564:
	add	r5, r5, #1
	uxtb	r5, r5
.L1563:
	ldrb	r3, [r4, #3762]	@ zero_extendqisi2
	cmp	r3, r5
	bhi	.L1585
	add	sp, sp, #76
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, sl, fp, pc}
.L1590:
	.align	2
.L1589:
	.word	.LANCHOR0
	.word	.LANCHOR2
	.word	.LC109
	.word	.LC110
	.word	.LC111
	.word	.LC112
	.word	.LC113
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
	stmfd	sp!, {r4, r5, r6, r7, r8, r9, sl, lr}
	.save {r4, r5, r6, r7, r8, r9, sl, lr}
	mov	r4, r0
	ldr	r5, [r0, #12]
	ldr	r8, [r0, #24]
	bl	ftl_free_no_use_map_blk
	ldrh	r3, [r4, #10]
	ldrh	r2, [r4, #8]
	sub	r3, r3, #4
	cmp	r2, r3
	blt	.L1592
	uxth	r0, r0
	mov	r0, r0, asl #1
	ldrh	r7, [r5, r0]
	cmp	r7, #0
	beq	.L1592
	ldr	r3, [r4, #32]
	cmp	r3, #0
	bne	.L1592
	mov	r2, #1
	ldr	r1, .L1599
	str	r2, [r4, #32]
	strh	r3, [r5, r0]	@ movhi
	ldrh	r3, [r4, #8]
	ldrh	r2, [r4, #2]
	sub	r3, r3, #1
	strh	r3, [r4, #8]	@ movhi
	movw	r3, #3910
	ldrh	r3, [r1, r3]
	cmp	r2, r3
	bcc	.L1593
	mov	r0, r4
	bl	ftl_map_blk_alloc_new_blk
.L1593:
	mov	r5, #0
	ldr	r6, .L1599+4
	mov	r9, r5
	b	.L1594
.L1597:
	ldr	r3, [r8, r5, asl #2]
	mov	sl, r5, asl #2
	cmp	r7, r3, lsr #10
	bne	.L1595
	ldr	r3, [r6, #-976]
	mov	r1, #1
	ldr	r0, .L1599+8
	mov	r2, r1
	str	r3, [r6, #164]
	ldr	r3, [r6, #-956]
	str	r3, [r6, #168]
	ldr	r3, [r8, r5, asl #2]
	str	r3, [r6, #160]
	bl	FlashReadPages
	ldr	r3, [r6, #156]
	cmn	r3, #1
	streq	r9, [r8, sl]
	beq	.L1595
	mov	r0, r4
	mov	r1, r5
	ldr	r2, [r6, #164]
	bl	FtlMapWritePage
.L1595:
	add	r5, r5, #1
	uxth	r5, r5
.L1594:
	ldrh	r3, [r4, #6]
	cmp	r3, r5
	bhi	.L1597
	mov	r0, r7
	mov	r1, #1
	bl	FtlFreeSysBlkQueueIn
	mov	r3, #0
	str	r3, [r4, #32]
.L1592:
	ldr	r1, .L1599
	movw	r3, #3910
	ldrh	r2, [r4, #2]
	ldrh	r3, [r1, r3]
	cmp	r2, r3
	bcc	.L1598
	mov	r0, r4
	bl	ftl_map_blk_alloc_new_blk
.L1598:
	mov	r0, #0
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, sl, pc}
.L1600:
	.align	2
.L1599:
	.word	.LANCHOR0
	.word	.LANCHOR2
	.word	.LANCHOR2+156
	.fnend
	.size	ftl_map_blk_gc, .-ftl_map_blk_gc
	.align	2
	.global	Ftl_write_map_blk_to_last_page
	.type	Ftl_write_map_blk_to_last_page, %function
Ftl_write_map_blk_to_last_page:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	stmfd	sp!, {r3, r4, r5, r6, r7, lr}
	.save {r3, r4, r5, r6, r7, lr}
	movw	r2, #65535
	ldrh	r3, [r0, #0]
	mov	r4, r0
	ldr	r5, [r0, #12]
	cmp	r3, r2
	ldr	r7, [r0, #24]
	bne	.L1602
	ldrh	r3, [r0, #8]
	add	r3, r3, #1
	strh	r3, [r0, #8]	@ movhi
	bl	FtlFreeSysBlkQueueOut
	mov	r3, #0
	strh	r0, [r5, #0]	@ movhi
	strh	r3, [r4, #2]	@ movhi
	strh	r3, [r4, #0]	@ movhi
	ldr	r3, [r4, #28]
	add	r3, r3, #1
	str	r3, [r4, #28]
	b	.L1603
.L1602:
	mov	r3, r3, asl #1
	ldr	r2, [r0, #28]
	mov	r1, #255
	ldrh	r6, [r5, r3]
	ldr	r5, .L1607
	ldrh	r3, [r0, #2]
	orr	r3, r3, r6, asl #10
	str	r3, [r5, #160]
	ldr	r3, [r5, #-2080]
	str	r3, [r5, #164]
	ldr	r3, [r5, #-956]
	str	r3, [r5, #168]
	str	r2, [r3, #4]
	ldr	r2, .L1607+4
	strh	r2, [r3, #8]	@ movhi
	ldrh	r2, [r0, #4]
	strh	r6, [r3, #2]	@ movhi
	strh	r2, [r3, #0]	@ movhi
	movw	r3, #3910
	ldr	r2, .L1607+8
	ldr	r0, [r5, #-2080]
	ldrh	r2, [r2, r3]
	mov	r2, r2, asl #3
	bl	ftl_memset
	mov	r2, #0
	mov	r3, r2
	b	.L1604
.L1606:
	ldr	r1, [r7, r3, asl #2]
	cmp	r6, r1, lsr #10
	bne	.L1605
	add	r2, r2, #1
	ldr	r1, [r5, #-2080]
	uxth	r2, r2
	str	r3, [r1, r2, asl #3]
	ldr	r1, [r5, #-2080]
	ldr	r0, [r7, r3, asl #2]
	add	r1, r1, r2, asl #3
	str	r0, [r1, #4]
.L1605:
	add	r3, r3, #1
	uxth	r3, r3
.L1604:
	ldrh	r1, [r4, #6]
	cmp	r1, r3
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
.L1603:
	mov	r0, #0
	ldmfd	sp!, {r3, r4, r5, r6, r7, pc}
.L1608:
	.align	2
.L1607:
	.word	.LANCHOR2
	.word	-1291
	.word	.LANCHOR0
	.word	.LANCHOR2+156
	.fnend
	.size	Ftl_write_map_blk_to_last_page, .-Ftl_write_map_blk_to_last_page
	.align	2
	.global	FtlMapWritePage
	.type	FtlMapWritePage, %function
FtlMapWritePage:
	.fnstart
	@ args = 0, pretend = 0, frame = 8
	@ frame_needed = 0, uses_anonymous_args = 0
	stmfd	sp!, {r0, r1, r2, r4, r5, r6, r7, r8, r9, sl, fp, lr}
	.save {r0, r1, r2, r4, r5, r6, r7, r8, r9, sl, fp, lr}
	mov	r4, r0
	ldr	r5, .L1623
	mov	fp, r1
	ldr	r7, .L1623+4
	mov	r6, #0
	movw	r8, #3910
	str	r2, [sp, #4]
.L1621:
	ldr	r3, [r5, #-1868]
	add	r3, r3, #1
	str	r3, [r5, #-1868]
	ldrh	r3, [r7, r8]
	ldrh	r2, [r4, #2]
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
	ldr	r0, [r5, #-956]
	mov	r3, r3, asl #1
	ldr	r9, .L1623
	ldrh	sl, [r2, r3]
	mov	r2, #16
	ldrh	r3, [r4, #2]
	str	r0, [r5, #168]
	orr	r3, r3, sl, asl #10
	str	r3, [r5, #160]
	ldr	r3, [sp, #4]
	str	r3, [r5, #164]
	bl	ftl_memset
	ldr	r3, [r5, #168]
	mov	r1, #1
	ldr	r2, [r4, #28]
	add	r0, r9, #156
	strh	fp, [r3, #8]	@ movhi
	str	r2, [r3, #4]
	ldrh	r2, [r4, #4]
	strh	sl, [r3, #2]	@ movhi
	strh	r2, [r3, #0]	@ movhi
	mov	r2, r1
	mov	r3, r1
	bl	FlashProgPages
	ldrh	r3, [r4, #2]
	add	r3, r3, #1
	uxth	r3, r3
	strh	r3, [r4, #2]	@ movhi
	ldr	r2, [r5, #156]
	cmn	r2, #1
	bne	.L1613
	ldr	r0, .L1623+8
	add	r6, r6, #1
	ldr	r1, [r5, #160]
	bl	printk
	ldrh	r3, [r4, #2]
	uxth	r6, r6
	cmp	r3, #2
	ldrlsh	r3, [r7, r8]
	subls	r3, r3, #1
	strlsh	r3, [r4, #2]	@ movhi
	cmp	r6, #3
	bls	.L1615
	ldr	r3, .L1623
	mov	r2, r6
	ldr	r0, .L1623+12
	ldr	r1, [r3, #160]
	bl	printk
.L1616:
	b	.L1616
.L1615:
	ldr	r3, [r4, #32]
	cmp	r3, #0
	beq	.L1621
.L1622:
	b	.L1622
.L1613:
	cmp	r3, #1
	beq	.L1621
	ldr	r2, [r9, #160]
	mov	r0, #0
	ldr	r3, [r4, #24]
	str	r2, [r3, fp, asl #2]
	ldmfd	sp!, {r1, r2, r3, r4, r5, r6, r7, r8, r9, sl, fp, pc}
.L1624:
	.align	2
.L1623:
	.word	.LANCHOR2
	.word	.LANCHOR0
	.word	.LC114
	.word	.LC115
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
	ldr	r5, .L1626
	mul	r4, r4, r0
	ldr	r0, .L1626+4
	ldr	r2, [r5, #-1904]
	add	r3, r2, r4
	ldrh	r1, [r2, r4]
	ldr	r2, [r3, #8]
	bl	FtlMapWritePage
	ldr	r3, [r5, #-1904]
	mov	r0, #0
	add	r4, r3, r4
	ldr	r3, [r4, #4]
	bic	r3, r3, #-2147483648
	str	r3, [r4, #4]
	ldmfd	sp!, {r3, r4, r5, pc}
.L1627:
	.align	2
.L1626:
	.word	.LANCHOR2
	.word	.LANCHOR2-1092
	.fnend
	.size	flush_l2p_region, .-flush_l2p_region
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
	ldr	r3, .L1637
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
	ldr	r3, .L1637+4
	uxth	r7, r7
	ldr	sl, [r3, #-1904]
	mov	r3, #0
	mov	r5, r3
	b	.L1629
.L1635:
	add	r3, r3, #12
	add	r1, sl, r3
	ldrh	r1, [r1, #-12]
	cmp	r1, r6
	bne	.L1630
.L1631:
	cmp	r8, #0
	ldr	r3, .L1637+4
	mov	r2, #12
	bne	.L1632
	ldr	r3, [r3, #-1904]
	mla	r2, r2, r5, r3
	ldr	r3, [r2, #8]
	ldr	r3, [r3, r7, asl #2]
	str	r3, [r4, #0]
	b	.L1633
.L1632:
	mul	r2, r2, r5
	ldr	r1, [r3, #-1904]
	ldr	r0, [r4, #0]
	add	r1, r1, r2
	ldr	r1, [r1, #8]
	str	r0, [r1, r7, asl #2]
	ldr	r1, [r3, #-1904]
	add	r2, r1, r2
	ldr	r1, [r2, #4]
	orr	r1, r1, #-2147483648
	str	r1, [r2, #4]
	ldr	r2, .L1637+8
	strh	r6, [r3, r2]	@ movhi
.L1633:
	ldr	r3, .L1637+4
	mov	r2, #12
	mov	r0, #0
	ldr	r3, [r3, #-1904]
	mla	r5, r2, r5, r3
	ldr	r3, [r5, #4]
	cmn	r3, #1
	addne	r3, r3, #1
	strne	r3, [r5, #4]
	ldmfd	sp!, {r3, r4, r5, r6, r7, r8, sl, pc}
.L1630:
	add	r5, r5, #1
	uxth	r5, r5
.L1629:
	cmp	r5, r2
	bne	.L1635
	bl	select_l2p_ram_region
	mov	r3, #12
	mul	r3, r3, r0
	mov	r5, r0
	add	r2, sl, r3
	ldrh	r1, [sl, r3]
	movw	r3, #65535
	cmp	r1, r3
	beq	.L1636
	ldr	r3, [r2, #4]
	cmp	r3, #0
	bge	.L1636
	bl	flush_l2p_region
.L1636:
	mov	r0, r6
	mov	r1, r5
	bl	load_l2p_region
	b	.L1631
.L1638:
	.align	2
.L1637:
	.word	.LANCHOR0
	.word	.LANCHOR2
	.word	-1900
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
	ldr	r3, .L1645
	ldr	r1, [r3, #-2068]
	mov	r5, r0, asl #1
	ldrh	r2, [r1, r5]
	cmp	r2, #0
	addne	r2, r2, #1
	strneh	r2, [r1, r5]	@ movhi
	bne	.L1641
	ldr	r4, [r3, #-2056]
	cmp	r4, #0
	beq	.L1641
	ldr	r1, .L1645+4
	mov	ip, #6
	ldrh	lr, [r3, r1]
	ldr	r3, [r3, #-2076]
	ldr	r1, .L1645+8
	rsb	r4, r3, r4
	mov	r4, r4, asr #1
	mul	r4, r1, r4
	movw	r1, #65535
	uxth	r4, r4
	b	.L1642
.L1644:
	cmp	r4, r0
	bne	.L1643
	mov	r1, r4
	ldr	r0, .L1645+12
	bl	List_remove_node
	ldr	r6, .L1645
	ldr	r3, .L1645+4
	mov	r0, r4
	ldrh	r2, [r6, r3]
	sub	r2, r2, #1
	strh	r2, [r6, r3]	@ movhi
	bl	INSERT_DATA_LIST
	ldr	r3, [r6, #-2068]
	ldrh	r2, [r3, r5]
	add	r2, r2, #1
	strh	r2, [r3, r5]	@ movhi
	b	.L1641
.L1643:
	mul	r4, ip, r4
	ldrh	r4, [r3, r4]
	cmp	r4, r1
	beq	.L1641
	add	r2, r2, #1
	uxth	r2, r2
.L1642:
	cmp	r2, lr
	bne	.L1644
.L1641:
	mov	r0, r7
	add	r1, sp, #4
	mov	r2, #1
	bl	log2phys
	ldmfd	sp!, {r1, r2, r3, r4, r5, r6, r7, pc}
.L1646:
	.align	2
.L1645:
	.word	.LANCHOR2
	.word	-2052
	.word	-1431655765
	.word	.LANCHOR2-2056
	.fnend
	.size	FtlReUsePrevPpa, .-FtlReUsePrevPpa
	.align	2
	.global	FtlMapBlkWriteDumpData
	.type	FtlMapBlkWriteDumpData, %function
FtlMapBlkWriteDumpData:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	stmfd	sp!, {r4, r5, r6, lr}
	.save {r4, r5, r6, lr}
	mov	r4, r0
	ldr	r2, [r0, #36]
	ldrh	r6, [r0, #6]
	cmp	r2, #0
	ldr	r3, [r0, #24]
	ldmeqfd	sp!, {r4, r5, r6, pc}
	ldr	r5, .L1651
	sub	r6, r6, #1
	mov	r2, #0
	str	r2, [r0, #36]
	uxth	r6, r6
	ldr	r0, [r5, #-976]
	ldr	r2, [r5, #-956]
	str	r0, [r5, #164]
	str	r2, [r5, #168]
	ldr	r3, [r3, r6, asl #2]
	cmp	r3, #0
	str	r3, [r5, #160]
	beq	.L1649
	mov	r1, #1
	add	r0, r5, #156
	mov	r2, r1
	bl	FlashReadPages
	b	.L1650
.L1649:
	ldr	r2, .L1651+4
	movw	r3, #3918
	mov	r1, #255
	ldrh	r2, [r2, r3]
	bl	ftl_memset
.L1650:
	mov	r0, r4
	mov	r1, r6
	ldr	r2, [r5, #164]
	ldmfd	sp!, {r4, r5, r6, lr}
	b	FtlMapWritePage
.L1652:
	.align	2
.L1651:
	.word	.LANCHOR2
	.word	.LANCHOR0
	.fnend
	.size	FtlMapBlkWriteDumpData, .-FtlMapBlkWriteDumpData
	.align	2
	.global	FtlVendorPartWrite
	.type	FtlVendorPartWrite, %function
FtlVendorPartWrite:
	.fnstart
	@ args = 0, pretend = 0, frame = 56
	@ frame_needed = 0, uses_anonymous_args = 0
	ldr	r3, .L1663
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
	bhi	.L1654
	mov	r8, r0, lsr r8
	mov	r7, #0
	ldr	sl, .L1663+4
	mov	ip, r8, asl #2
	str	ip, [sp, #8]
	b	.L1655
.L1660:
	ldr	r1, .L1663
	movw	r2, #3914
	ldr	ip, [sp, #8]
	mov	r0, r6
	ldr	r3, [sl, #-924]
	ldrh	fp, [r1, r2]
	ldr	r3, [r3, ip]
	mov	r1, fp
	str	r3, [sp, #4]
	bl	__aeabi_uidivmod
	ldr	r3, [sp, #4]
	ldr	r2, .L1663+4
	uxth	r1, r1
	str	r1, [sp, #12]
	rsb	r5, r1, fp
	uxth	r5, r5
	cmp	r5, r4
	uxthhi	r5, r4
	cmp	r3, #0
	beq	.L1657
	cmp	r5, fp
	beq	.L1657
	str	r3, [sp, #24]
	mov	r1, #1
	ldr	r3, [r2, #-972]
	add	r0, sp, #20
	mov	r2, r1
	str	r3, [sp, #28]
	mov	r3, #0
	str	r3, [sp, #32]
	bl	FlashReadPages
	b	.L1658
.L1657:
	ldr	ip, .L1663
	movw	r3, #3918
	ldr	r0, [r2, #-972]
	mov	r1, #0
	ldrh	r2, [ip, r3]
	bl	ftl_memset
.L1658:
	ldr	r3, [sp, #12]
	mov	fp, r5, asl #9
	ldr	r0, [sl, #-972]
	mov	r1, r9
	mov	r2, fp
	rsb	r4, r5, r4
	add	r0, r0, r3, asl #9
	add	r6, r6, r5
	bl	memcpy
	mov	r1, r8
	ldr	r0, .L1663+8
	add	r8, r8, #1
	ldr	r2, [sl, #-972]
	add	r9, r9, fp
	bl	FtlMapWritePage
	ldr	ip, [sp, #8]
	add	ip, ip, #4
	str	ip, [sp, #8]
	cmn	r0, #1
	moveq	r7, r0
.L1655:
	cmp	r4, #0
	bne	.L1660
.L1654:
	mov	r0, r7
	add	sp, sp, #60
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, sl, fp, pc}
.L1664:
	.align	2
.L1663:
	.word	.LANCHOR0
	.word	.LANCHOR2
	.word	.LANCHOR2+224
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
	ldr	r3, .L1667
	ldr	r2, .L1667+4
	ldr	r1, [r3, #-1708]
	cmp	r1, r2
	bxne	lr
	ldr	r2, .L1667+8
	mov	r0, #0
	mov	r1, #1
	str	r2, [r3, #-1704]
	ldr	r2, [r3, #-1860]
	str	r2, [r3, #-1620]
	ldr	r2, [r3, #-1856]
	str	r2, [r3, #-1616]
	ldr	r2, [r3, #-1864]
	str	r2, [r3, #-1700]
	ldr	r2, [r3, #-1876]
	str	r2, [r3, #-1696]
	ldr	r2, [r3, #-1884]
	str	r2, [r3, #-1692]
	ldr	r2, [r3, #-1868]
	str	r2, [r3, #-1688]
	ldr	r2, [r3, #-1840]
	str	r2, [r3, #-1680]
	ldr	r2, [r3, #-2084]
	str	r2, [r3, #-1676]
	ldr	r2, [r3, #-1880]
	str	r2, [r3, #-1672]
	ldr	r2, [r3, #-1872]
	str	r2, [r3, #-1668]
	ldr	r2, [r3, #-1832]
	str	r2, [r3, #-1664]
	ldr	r2, [r3, #-1828]
	str	r2, [r3, #-1660]
	ldr	r2, [r3, #-1712]
	str	r2, [r3, #-1648]
	ldr	r2, [r3, #-1008]
	str	r2, [r3, #-1644]
	ldr	r2, .L1667+12
	b	FtlVendorPartWrite
.L1668:
	.align	2
.L1667:
	.word	.LANCHOR2
	.word	1179929683
	.word	1342177344
	.word	.LANCHOR2-1708
	.fnend
	.size	Ftl_save_ext_data, .-Ftl_save_ext_data
	.align	2
	.global	FtlEctTblFlush
	.type	FtlEctTblFlush, %function
FtlEctTblFlush:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	ldr	r1, .L1676
	stmfd	sp!, {r3, lr}
	.save {r3, lr}
	ldr	r3, [r1, #-1888]
	cmp	r3, #0
	moveq	r2, #32
	beq	.L1670
	ldr	r2, [r1, #-1828]
	cmp	r2, #29
	movls	r2, #4
	movhi	r2, #32
.L1670:
	mov	r3, #264
	ldrh	r1, [r1, r3]
	cmp	r1, #31
	addls	r1, r1, #1
	ldrls	r2, .L1676
	strlsh	r1, [r2, r3]	@ movhi
	movls	r2, #1
	cmp	r0, #0
	ldr	r3, .L1676
	bne	.L1672
	ldr	r1, [r3, #-940]
	ldr	r0, [r1, #20]
	ldr	r1, [r1, #16]
	add	r2, r2, r0
	cmp	r1, r2
	bcc	.L1673
.L1672:
	ldr	r2, [r3, #-940]
	mov	r0, #64
	ldr	r1, [r2, #16]
	str	r1, [r2, #20]
	ldr	r1, .L1676+4
	str	r1, [r2, #0]
	ldr	r1, .L1676+8
	ldr	r2, [r3, #-940]
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
.L1673:
	mov	r0, #0
	ldmfd	sp!, {r3, pc}
.L1677:
	.align	2
.L1676:
	.word	.LANCHOR2
	.word	1112818501
	.word	-944
	.fnend
	.size	FtlEctTblFlush, .-FtlEctTblFlush
	.align	2
	.type	FtlReadRefresh.part.13, %function
FtlReadRefresh.part.13:
	.fnstart
	@ args = 0, pretend = 0, frame = 40
	@ frame_needed = 0, uses_anonymous_args = 0
	stmfd	sp!, {r4, r5, r6, lr}
	.save {r4, r5, r6, lr}
	.pad #40
	sub	sp, sp, #40
	ldr	r4, .L1686
	ldr	r6, .L1686+4
	ldr	r2, [r4, #-1624]
	ldr	r3, [r6, #3968]
	cmp	r2, r3
	bcs	.L1679
	mov	r5, #2048
.L1682:
	ldr	r0, [r4, #-1624]
	ldr	r3, [r6, #3968]
	cmp	r0, r3
	bcs	.L1684
	mov	r1, sp
	mov	r2, #0
	bl	log2phys
	ldr	r0, [sp, #0]
	ldr	r3, [r4, #-1624]
	cmn	r0, #1
	add	r3, r3, #1
	str	r3, [r4, #-1624]
	beq	.L1681
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
	bne	.L1684
	ldr	r0, [sp, #0]
	ubfx	r0, r0, #10, #16
	bl	P2V_block_in_plane
	bl	FtlGcRefreshBlock
	b	.L1684
.L1681:
	subs	r5, r5, #1
	bne	.L1682
	b	.L1680
.L1679:
	ldr	r3, [r4, #-1884]
	mov	r0, #0
	str	r0, [r4, #-1628]
	str	r0, [r4, #-1624]
	str	r3, [r4, #-1632]
	b	.L1680
.L1684:
	mvn	r0, #0
.L1680:
	add	sp, sp, #40
	ldmfd	sp!, {r4, r5, r6, pc}
.L1687:
	.align	2
.L1686:
	.word	.LANCHOR2
	.word	.LANCHOR0
	.fnend
	.size	FtlReadRefresh.part.13, .-FtlReadRefresh.part.13
	.align	2
	.global	FtlReadRefresh
	.type	FtlReadRefresh, %function
FtlReadRefresh:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	@ link register save eliminated.
	ldr	r3, .L1695
	ldr	r2, [r3, #-1628]
	cmp	r2, #0
	beq	.L1689
	b	FtlReadRefresh.part.13
.L1689:
	ldr	r1, .L1695+4
	ldr	r0, [r3, #-1832]
	ldr	r2, [r3, #-1888]
	ldrb	r1, [r1, #928]	@ zero_extendqisi2
	cmp	r1, #0
	addeq	r0, r2, r0, lsr #10
	ldr	r2, [r3, #-1884]
	moveq	r1, #33554432
	movne	r0, #4194304
	moveq	r0, r1, asr r0
	ldr	r1, [r3, #-1632]
	add	r3, r2, #1048576
	cmp	r1, r3
	bhi	.L1691
	add	r1, r0, r1
	cmp	r1, r2
	bcc	.L1691
	ldr	r3, .L1695
	ldrb	r3, [r3, #-1796]	@ zero_extendqisi2
	cmp	r3, #0
	bne	.L1692
.L1691:
	ldr	r3, .L1695
	mov	r1, #1
	str	r2, [r3, #-1632]
	str	r1, [r3, #-1628]
	mov	r1, #0
	str	r1, [r3, #-1624]
.L1692:
	mov	r0, #0
	bx	lr
.L1696:
	.align	2
.L1695:
	.word	.LANCHOR2
	.word	.LANCHOR0
	.fnend
	.size	FtlReadRefresh, .-FtlReadRefresh
	.align	2
	.global	FtlMapTblRecovery
	.type	FtlMapTblRecovery, %function
FtlMapTblRecovery:
	.fnstart
	@ args = 0, pretend = 0, frame = 24
	@ frame_needed = 0, uses_anonymous_args = 0
	stmfd	sp!, {r4, r5, r6, r7, r8, r9, sl, fp, lr}
	.save {r4, r5, r6, r7, r8, r9, sl, fp, lr}
	.pad #28
	sub	sp, sp, #28
	ldr	r3, [r0, #16]
	mov	r4, r0
	ldr	r7, [r0, #24]
	mov	r1, #0
	ldrh	r8, [r0, #6]
	mov	fp, #0
	str	r3, [sp, #16]
	ldrh	r3, [r0, #8]
	mov	sl, r7
	ldr	r9, [r0, #12]
	mov	r2, r8, asl #2
	mov	r0, r7
	str	r3, [sp, #8]
	bl	ftl_memset
	ldr	r3, .L1717
	str	fp, [r4, #32]
	str	fp, [r4, #28]
	ldr	r2, [r3, #-2080]
	ldr	r5, [r3, #-956]
	str	r2, [r3, #164]
	str	r5, [r3, #168]
	mvn	r3, #0
	strh	r3, [r4, #0]	@ movhi
	strh	r3, [r4, #2]	@ movhi
	mov	r3, #1
	str	r3, [r4, #36]
	ldr	r3, [sp, #8]
	sub	r3, r3, #1
	str	r3, [sp, #12]
	ldr	r3, .L1717+4
	b	.L1698
.L1711:
	ldr	r0, [sp, #12]
	cmp	r2, r0
	mov	r2, r2, asl #1
	bne	.L1699
	ldrh	r0, [r9, r2]
	mov	r1, #1
	add	r3, r9, r2
	str	r3, [sp, #8]
	bl	FtlGetLastWrittenPage
	ldr	r1, [sp, #12]
	ldr	r2, [sp, #16]
	mov	r7, sl
	strh	fp, [r4, #0]	@ movhi
	mov	sl, #0
	ldr	r6, .L1717
	uxth	r0, r0
	add	r3, r0, #1
	strh	r3, [r4, #2]	@ movhi
	ldr	r3, [r2, r1, asl #2]
	sxth	r9, r0
	add	r9, r9, #1
	str	r3, [r4, #28]
	b	.L1700
.L1702:
	ldr	r0, [sp, #8]
	mov	r1, #1
	ldrh	r2, [r0, #0]
	ldr	r0, .L1717+8
	orr	r3, r3, r2, asl #10
	mov	r2, r1
	str	r3, [r6, #160]
	bl	FlashReadPages
	ldr	r3, [r6, #156]
	cmn	r3, #1
	beq	.L1701
	ldrh	r3, [r5, #8]
	cmp	r3, r8
	bcs	.L1701
	ldrh	r2, [r4, #4]
	ldrh	r1, [r5, #0]
	cmp	r1, r2
	ldreq	r2, [r6, #160]
	streq	r2, [r7, r3, asl #2]
.L1701:
	add	sl, sl, #1
	uxth	sl, sl
.L1700:
	sxth	r3, sl
	cmp	r3, r9
	blt	.L1702
	b	.L1703
.L1699:
	ldr	r6, .L1717
	movw	r7, #3910
	add	r0, r6, #156
	ldr	r1, [r6, #-2080]
	str	r1, [r6, #164]
	add	r1, r9, r2
	str	r1, [sp, #20]
	ldrh	r1, [r9, r2]
	ldrh	r2, [r3, r7]
	sub	r2, r2, #1
	orr	r2, r2, r1, asl #10
	mov	r1, #1
	str	r2, [r6, #160]
	mov	r2, r1
	str	r3, [sp, #4]
	bl	FlashReadPages
	ldr	r2, [r6, #156]
	ldr	r3, [sp, #4]
	cmn	r2, #1
	beq	.L1714
	ldrh	r1, [r5, #0]
	ldrh	r2, [r4, #4]
	cmp	r1, r2
	bne	.L1714
	ldrh	r1, [r5, #8]
	movw	r2, #64245
	cmp	r1, r2
	bne	.L1714
	b	.L1715
.L1707:
	ldr	r0, [r6, #-2080]
	mov	ip, r1, asl #3
	add	r2, r2, #1
	ldr	r1, [r0, r1, asl #3]
	uxth	r2, r2
	uxth	r1, r1
	cmp	r1, r8
	addcc	r0, r0, ip
	ldrcc	r0, [r0, #4]
	strcc	r0, [sl, r1, asl #2]
	b	.L1705
.L1715:
	mov	r2, #0
.L1705:
	ldrh	r0, [r3, r7]
	sxth	r1, r2
	sub	r0, r0, #1
	cmp	r1, r0
	blt	.L1707
	b	.L1708
.L1710:
	ldr	r0, [sp, #20]
	ldrh	r1, [r0, #0]
	ldr	r0, .L1717+8
	orr	r2, r2, r1, asl #10
	mov	r1, #1
	str	r2, [r7, #160]
	mov	r2, r1
	str	r3, [sp, #4]
	bl	FlashReadPages
	ldr	r2, [r7, #156]
	ldr	r3, [sp, #4]
	cmn	r2, #1
	beq	.L1709
	ldrh	r2, [r5, #8]
	cmp	r2, r8
	bcs	.L1709
	ldrh	r1, [r4, #4]
	ldrh	r0, [r5, #0]
	cmp	r0, r1
	ldreq	r1, [r7, #160]
	streq	r1, [sl, r2, asl #2]
.L1709:
	add	r6, r6, #1
	uxth	r6, r6
	b	.L1716
.L1714:
	ldr	r7, .L1717
	mov	r6, #0
.L1716:
	movw	r1, #3910
	sxth	r2, r6
	ldrh	r1, [r3, r1]
	cmp	r2, r1
	blt	.L1710
.L1708:
	add	fp, fp, #1
	uxth	fp, fp
.L1698:
	ldr	r1, [sp, #8]
	sxth	r2, fp
	cmp	r2, r1
	blt	.L1711
.L1703:
	mov	r0, r4
	bl	ftl_free_no_use_map_blk
	ldr	r1, .L1717+4
	movw	r3, #3910
	ldrh	r2, [r4, #2]
	ldrh	r3, [r1, r3]
	cmp	r2, r3
	bne	.L1712
	mov	r0, r4
	bl	ftl_map_blk_alloc_new_blk
.L1712:
	mov	r0, r4
	bl	ftl_map_blk_gc
	mov	r0, r4
	bl	ftl_map_blk_gc
	mov	r0, #0
	add	sp, sp, #28
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, sl, fp, pc}
.L1718:
	.align	2
.L1717:
	.word	.LANCHOR2
	.word	.LANCHOR0
	.word	.LANCHOR2+156
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
	ldr	r0, .L1720
	bl	FtlMapTblRecovery
	mov	r0, #0
	ldmfd	sp!, {r3, pc}
.L1721:
	.align	2
.L1720:
	.word	.LANCHOR2-1092
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
	ldr	r3, .L1723
	ldr	r0, .L1723+4
	ldrh	r2, [r3, r2]
	strh	r2, [r0, #234]	@ movhi
	ldr	r2, .L1723+8
	strh	r2, [r0, #228]	@ movhi
	mov	r2, #3952
	ldrh	r2, [r3, r2]
	strh	r2, [r0, #232]	@ movhi
	movw	r2, #3930
	ldrh	r2, [r3, r2]
	ldr	r3, [r3, #3956]
	strh	r2, [r0, #230]	@ movhi
	str	r3, [r0, #236]
	ldr	r3, [r0, #-928]
	str	r3, [r0, #240]
	ldr	r3, [r0, #-932]
	str	r3, [r0, #244]
	ldr	r3, [r0, #-924]
	str	r3, [r0, #248]
	add	r0, r0, #224
	bl	FtlMapTblRecovery
	mov	r0, #0
	ldmfd	sp!, {r3, pc}
.L1724:
	.align	2
.L1723:
	.word	.LANCHOR0
	.word	.LANCHOR2
	.word	-3962
	.fnend
	.size	FtlLoadVonderInfo, .-FtlLoadVonderInfo
	.align	2
	.global	FtlRecoverySuperblock
	.type	FtlRecoverySuperblock, %function
FtlRecoverySuperblock:
	.fnstart
	@ args = 0, pretend = 0, frame = 64
	@ frame_needed = 0, uses_anonymous_args = 0
	stmfd	sp!, {r4, r5, r6, r7, r8, r9, sl, fp, lr}
	.save {r4, r5, r6, r7, r8, r9, sl, fp, lr}
	movw	r2, #65535
	ldrh	r3, [r0, #0]
	.pad #68
	sub	sp, sp, #68
	mov	r4, r0
	cmp	r3, r2
	beq	.L1726
	ldrh	r3, [r0, #2]
	ldr	r1, .L1813
	ldrb	fp, [r0, #6]	@ zero_extendqisi2
	str	r3, [sp, #20]
	movw	r3, #3908
	ldrh	r3, [r1, r3]
	ldr	ip, [sp, #20]
	str	fp, [sp, #32]
	cmp	r3, ip
	mov	r3, #0
	streqh	r3, [r0, #4]	@ movhi
	moveq	r2, r3	@ movhi
	ldrneh	r0, [r0, #16]
	bne	.L1728
	b	.L1810
.L1729:
	add	r3, r3, #1
	uxth	r3, r3
	add	r1, r4, r3, asl #1
	ldrh	r0, [r1, #16]
.L1728:
	cmp	r0, r2
	beq	.L1729
	ldrb	r1, [r4, #8]	@ zero_extendqisi2
	cmp	r1, #1
	bne	.L1730
	bl	FtlGetLastWrittenPage
	cmn	r0, #1
	mov	r7, r0
	beq	.L1731
	ldr	r3, .L1813
	ldrb	r2, [r3, #928]	@ zero_extendqisi2
	cmp	r2, #0
	bne	.L1803
	add	r3, r3, r0, asl #1
	movw	r2, #3076
	ldrh	r5, [r3, r2]
	b	.L1795
.L1730:
	mov	r1, #0
	bl	FtlGetLastWrittenPage
	cmn	r0, #1
	mov	r7, r0
	beq	.L1731
.L1803:
	mov	r5, r7
.L1795:
	ldr	r3, .L1813
	mov	r2, #3840
	mov	r6, #0
	str	r4, [sp, #40]
	movw	ip, #65535
	ldrh	r9, [r3, r2]
	ldr	r2, .L1813+4
	ldr	sl, [r2, #-988]
	ldr	r8, [r2, #-1168]
	mov	r2, #3920
	ldrh	lr, [r3, r2]
	mov	r2, r4
	mov	r3, r6
	b	.L1733
.L1731:
	mov	r3, #0
	strh	r3, [r4, #2]	@ movhi
	mov	r2, r3	@ movhi
.L1810:
	strb	r2, [r4, #6]
	b	.L1726
.L1735:
	ldrh	r0, [r2, #16]
	cmp	r0, ip
	beq	.L1734
	mov	fp, #36
	orr	r0, r5, r0, asl #10
	mla	r1, fp, r6, sl
	str	r0, [r1, #4]
	mov	r0, #0
	str	r0, [r1, #8]
	mul	r0, lr, r6
	add	r6, r6, #1
	uxth	r6, r6
	add	fp, r0, #3
	cmp	r0, #0
	movlt	r0, fp
	bic	r0, r0, #3
	add	r0, r8, r0
	str	r0, [r1, #12]
.L1734:
	add	r3, r3, #1
	add	r2, r2, #2
	uxth	r3, r3
.L1733:
	cmp	r3, r9
	bne	.L1735
	ldrb	r3, [r4, #8]	@ zero_extendqisi2
	cmp	r3, #1
	movne	r3, #0
	bne	.L1804
	ldr	r3, .L1813
	ldrb	r3, [r3, #928]	@ zero_extendqisi2
	adds	r3, r3, #0
	movne	r3, #1
.L1804:
	ldr	r8, .L1813+4
	mov	r1, r6
	str	r3, [sp, #28]
	mov	sl, #0
	ldr	r2, [sp, #28]
	ldr	r0, [r8, #-988]
	bl	FlashReadPages
	ldr	fp, [r8, #-1848]
	uxth	r2, r5
	ldr	r9, [r8, #-988]
	movw	ip, #65535
	mov	r3, r8
	str	fp, [sp, #16]
	mov	r8, r2
	str	ip, [sp, #12]
	mov	fp, r7
	b	.L1737
.L1742:
	ldr	r2, [r9, #0]
	cmp	r2, #0
	strne	r8, [sp, #12]
	bne	.L1740
	ldr	r2, [r9, #12]
	ldr	r7, [r2, #4]
	cmn	r7, #1
	beq	.L1739
	ldr	r1, [r3, #-1848]
	mov	r0, r7
	str	r2, [sp, #8]
	str	r3, [sp, #4]
	bl	ftl_cmp_data_ver
	ldr	r3, [sp, #4]
	ldr	r2, [sp, #8]
	cmp	r0, #0
	addne	r7, r7, #1
	strne	r7, [r3, #-1848]
.L1739:
	ldr	r2, [r2, #0]
	cmn	r2, #1
	bne	.L1740
	ldr	r3, .L1813+4
	mov	r2, #36
	uxth	r7, fp
	ldr	r3, [r3, #-988]
	mla	sl, r2, sl, r3
	ldr	r0, [sl, #4]
	b	.L1805
.L1740:
	add	sl, sl, #1
	add	r9, r9, #36
	uxth	sl, sl
.L1737:
	cmp	sl, r6
	bne	.L1742
	ldr	r3, .L1813+4
	add	r7, fp, #1
	uxth	r7, r7
	ldr	r3, [r3, #-988]
	ldr	r0, [r3, #4]
.L1805:
	ubfx	r0, r0, #10, #16
	bl	P2V_plane
	ldrb	r2, [r4, #8]	@ zero_extendqisi2
	ldr	r3, .L1813
	cmp	r2, #1
	str	r0, [sp, #24]
	bne	.L1743
	ldrb	r1, [r3, #928]	@ zero_extendqisi2
	cmp	r1, #0
	addeq	r7, r3, r7, asl #1
	movweq	r1, #3076
	ldreqh	r7, [r7, r1]
.L1743:
	movw	r1, #3908
	ldr	fp, [sp, #24]
	ldrh	r3, [r3, r1]
	ldr	ip, [sp, #32]
	cmp	r3, r7
	streqh	r7, [r4, #2]	@ movhi
	moveq	r3, #0
	streqb	r3, [r4, #6]
	streqh	r3, [r4, #4]	@ movhi
	ldr	r3, [sp, #20]
	cmp	r7, r3
	cmpeq	fp, ip
	moveq	r0, r4
	moveq	r1, r7
	moveq	r2, fp
	beq	.L1811
	ldr	r3, [sp, #16]
	ldr	fp, [sp, #12]
	sub	r6, r3, #1
	movw	r3, #65535
	cmp	fp, r3
	bne	.L1746
	cmp	r2, #0
	bne	.L1747
.L1746:
	ldr	r2, .L1813+4
	uxth	r3, r5
	ldr	ip, [sp, #20]
	mvn	fp, #0
	mov	sl, r3
	mov	r9, fp
	ldr	r1, [r2, #-1016]
	ldr	r8, .L1813+4
	cmn	r1, #1
	streq	r6, [r2, #-1016]
	ldr	r2, [r2, #-1016]
	str	r2, [sp, #12]
	add	r2, ip, #7
	cmp	r3, r2
	subgt	r5, r3, #7
	mov	r3, r7
	ldrle	r5, [sp, #20]
	mov	r7, r6
	uxthgt	r5, r5
	b	.L1750
.L1752:
	ldrh	r0, [r1, #16]
	movw	lr, #65535
	cmp	r0, lr
	beq	.L1751
	ldr	ip, [sp, #16]
	mov	r3, #36
	orr	r0, r5, r0, asl #10
	mla	lr, r3, r6, ip
	add	r6, r6, #1
	uxth	r6, r6
	str	r0, [lr, #4]
.L1751:
	add	r2, r2, #1
	add	r1, r1, #2
	uxth	r2, r2
.L1763:
	ldr	lr, [sp, #44]
	cmp	r2, lr
	bne	.L1752
	ldr	r3, [sp, #36]
	mov	r1, r6
	ldr	r0, [r8, #-988]
	ldr	r2, [sp, #28]
	str	r3, [sp, #4]
	bl	FlashReadPages
	ldr	r2, .L1813
	add	r0, r8, r5, asl #1
	mov	r1, #0
	str	r0, [sp, #16]
	ldr	r3, [sp, #4]
	ldrb	ip, [r2, #928]	@ zero_extendqisi2
	ldr	r2, [r8, #-988]
	str	ip, [sp, #36]
	b	.L1753
.L1761:
	ldr	r0, [r2, #0]
	cmp	r0, #0
	bne	.L1754
	ldr	r0, [r2, #12]
	movw	ip, #65535
	ldrh	lr, [r0, #0]
	cmp	lr, ip
	beq	.L1755
	ldr	r0, [r0, #4]
	cmn	r0, #1
	beq	.L1755
	cmn	fp, #1
	ldr	r9, [r8, #-1016]
	str	r0, [r8, #-1016]
	bne	.L1755
	ldr	lr, [sp, #16]
	sub	r0, lr, #916
	ldrh	r0, [r0, #0]
	cmp	r0, ip
	bne	.L1756
	ldr	r0, [sp, #36]
	cmp	r0, #0
	beq	.L1755
.L1756:
	cmp	r9, r7
	mvneq	fp, #0
	movne	fp, r9
	b	.L1755
.L1754:
	mov	r6, r7
	mov	r7, r3
	ldrb	r3, [r4, #8]	@ zero_extendqisi2
	cmp	r3, #0
	bne	.L1747
	ldr	r3, .L1813+4
	movw	r2, #65535
	add	r5, r3, r5, asl #1
	sub	r5, r5, #916
	ldrh	r1, [r5, #0]
	cmp	r1, r2
	bne	.L1757
	cmn	fp, #1
	bne	.L1806
.L1758:
	ldr	fp, [sp, #12]
	cmp	fp, r6
	beq	.L1759
.L1806:
	str	fp, [r3, #-1016]
	b	.L1747
.L1759:
	ldr	r2, [r3, #-1016]
	b	.L1812
.L1757:
	cmp	r9, r6
	beq	.L1760
	cmn	r9, #1
	strne	r9, [r3, #-1016]
	b	.L1747
.L1760:
	ldr	r2, [r3, #-1016]
	cmp	r2, r6
	beq	.L1747
.L1812:
	sub	r2, r2, #1
	b	.L1807
.L1755:
	add	r1, r1, #1
	add	r2, r2, #36
	uxth	r1, r1
.L1753:
	cmp	r1, r6
	bne	.L1761
	add	r5, r5, #1
	uxth	r5, r5
.L1750:
	cmp	r5, sl
	bhi	.L1762
	ldr	r1, .L1813
	mov	r2, #3840
	ldr	lr, [r8, #-988]
	mov	r6, #0
	str	r3, [sp, #36]
	ldrh	ip, [r1, r2]
	mov	r1, r4
	str	lr, [sp, #16]
	mov	r2, r6
	str	ip, [sp, #44]
	b	.L1763
.L1762:
	mov	r6, r7
	mov	r7, r3
	ldr	r3, .L1813+4
	mvn	r2, #0
.L1807:
	str	r2, [r3, #-1016]
.L1747:
	ldr	r5, .L1813+4
	movw	r3, #266
	mov	r2, #1
	ldr	r0, .L1813+8
	strh	r2, [r5, r3]	@ movhi
	bl	FtlMapBlkWriteDumpData
	ldr	r8, [sp, #20]
	str	r7, [sp, #16]
.L1764:
	ldr	r3, .L1813
	mov	r2, #3840
	mov	r7, #0
	ldr	lr, [r5, #-988]
	mov	fp, #36
	ldrh	sl, [r3, r2]
	mov	r2, r4
	ldrb	ip, [r3, #928]	@ zero_extendqisi2
	mov	r3, r7
	b	.L1765
.L1768:
	ldrh	r0, [r2, #16]
	movw	r1, #65535
	cmp	r0, r1
	beq	.L1766
	mla	r1, fp, r7, lr
	orr	r0, r8, r0, asl #10
	str	r0, [r1, #4]
	ldrb	r9, [r4, #8]	@ zero_extendqisi2
	cmp	r9, #1
	bne	.L1767
	cmp	ip, #0
	orrne	r0, r0, #-2147483648
	strne	r0, [r1, #4]
.L1767:
	add	r7, r7, #1
	uxth	r7, r7
.L1766:
	add	r3, r3, #1
	add	r2, r2, #2
	uxth	r3, r3
.L1765:
	cmp	r3, sl
	bne	.L1768
	mov	r1, r7
	ldr	r0, [r5, #-988]
	ldr	r2, [sp, #28]
	bl	FlashReadPages
	mov	r3, #36
	mul	r3, r3, r7
	mov	r7, #0
	str	r3, [sp, #44]
	b	.L1769
.L1790:
	ldr	r9, [r5, #-988]
	add	r9, r9, r7
	ldr	sl, [r9, #4]
	ubfx	r0, sl, #10, #16
	str	sl, [sp, #60]
	bl	P2V_plane
	ldr	r3, [sp, #20]
	cmp	r8, r3
	bcc	.L1770
	ldr	fp, [sp, #32]
	mov	ip, r3
	cmp	r0, fp
	movcs	r3, #0
	movcc	r3, #1
	cmp	r8, ip
	movne	r3, #0
	cmp	r3, #0
	bne	.L1770
	ldr	r3, [sp, #24]
	ldr	ip, [sp, #16]
	cmp	r0, r3
	cmpeq	r8, ip
	beq	.L1802
	ldr	r3, [r9, #0]
	cmn	r3, #1
	beq	.L1772
	ldr	sl, [r9, #12]
	movw	lr, #61589
	ldrh	r3, [sl, #0]
	cmp	r3, lr
	bne	.L1779
.L1773:
	ldr	r6, [sl, #4]
	cmn	r6, #1
	beq	.L1774
	ldr	r1, [r5, #-1848]
	mov	r0, r6
	bl	ftl_cmp_data_ver
	cmp	r0, #0
	addne	r3, r6, #1
	strne	r3, [r5, #-1848]
.L1774:
	ldr	r9, [sl, #8]
	add	r1, sp, #56
	ldr	r3, [sl, #12]
	mov	r2, #0
	mov	r0, r9
	str	r3, [sp, #52]
	bl	log2phys
	ldr	r1, [r5, #-1016]
	cmn	r1, #1
	beq	.L1775
	mov	r0, r6
	bl	ftl_cmp_data_ver
	cmp	r0, #0
	beq	.L1775
	ldr	r3, [sp, #52]
	cmn	r3, #1
	beq	.L1776
	ldr	r0, [r5, #-988]
	mov	r1, #1
	mov	r2, #0
	add	r0, r0, r7
	ldr	fp, [r0, #12]
	str	r3, [r0, #4]
	str	fp, [sp, #12]
	bl	FlashReadPages
	ldr	r3, [r5, #-988]
	ldr	ip, [fp, #4]
	add	fp, r3, r7
	ldr	r3, [r3, r7]
	str	ip, [sp, #36]
	cmn	r3, #1
	bne	.L1777
	b	.L1778
.L1776:
	ldr	r3, [sp, #60]
	ldr	r2, [sp, #56]
	cmp	r2, r3
	bne	.L1779
	mov	r0, r9
	add	r1, sp, #52
	mov	r2, #1
	bl	log2phys
.L1779:
	ldrh	r0, [r4, #0]
	b	.L1809
.L1777:
	ldr	r3, [sp, #12]
	ldr	sl, [r3, #8]
	cmp	sl, r9
	bne	.L1778
	ldr	r0, [r5, #-1016]
	ldr	r1, [sp, #36]
	bl	ftl_cmp_data_ver
	cmp	r0, #0
	beq	.L1778
	ldr	r3, [sp, #56]
	ldr	r2, [sp, #60]
	cmp	r3, r2
	beq	.L1783
.L1780:
	ldr	r2, [sp, #52]
	cmp	r3, r2
	beq	.L1778
	cmn	r3, #1
	streq	r3, [fp, #0]
	beq	.L1782
	ldr	ip, [fp, #12]
	mov	r0, fp
	str	r3, [fp, #4]
	mov	r1, #1
	mov	r2, #0
	str	ip, [sp, #12]
	bl	FlashReadPages
.L1782:
	ldr	r3, [r5, #-988]
	ldr	r3, [r3, r7]
	cmn	r3, #1
	beq	.L1783
	ldr	r3, [sp, #12]
	ldr	r0, [r5, #-1016]
	ldr	r9, [r3, #4]
	mov	r1, r9
	bl	ftl_cmp_data_ver
	cmp	r0, #0
	beq	.L1783
	ldr	r0, [sp, #36]
	mov	r1, r9
	bl	ftl_cmp_data_ver
	cmp	r0, #0
	beq	.L1778
.L1783:
	mov	r0, sl
	ldr	r1, [sp, #52]
	bl	FtlReUsePrevPpa
.L1778:
	ldrh	r0, [r4, #0]
	mvn	r3, #0
	str	r3, [sp, #52]
	bl	decrement_vpc_count
	b	.L1785
.L1775:
	ldr	r3, [sp, #60]
	ldr	r2, [sp, #56]
	cmp	r2, r3
	beq	.L1785
	mov	r0, r9
	add	r1, sp, #60
	mov	r2, #1
	bl	log2phys
	ldr	fp, [sp, #56]
	cmn	fp, #1
	beq	.L1785
	ldr	r3, [sp, #52]
	cmp	fp, r3
	beq	.L1785
	ubfx	r0, fp, #10, #16
	bl	P2V_block_in_plane
	ldr	r3, .L1813+12
	ldrh	r3, [r5, r3]
	cmp	r3, r0
	beq	.L1786
	ldr	r3, .L1813+16
	ldrh	r3, [r5, r3]
	cmp	r3, r0
	beq	.L1786
	ldr	r3, .L1813+20
	ldrh	r3, [r5, r3]
	cmp	r3, r0
	bne	.L1785
.L1786:
	ldr	ip, .L1813+4
	mov	r1, #1
	mov	r2, #0
	ldr	r0, [ip, #-988]
	str	fp, [r0, #4]
	ldr	sl, [r0, #12]
	bl	FlashReadPages
	ldr	r0, .L1813+4
	ldr	r1, [sl, #4]
	ldr	r3, [r0, #-988]
	ldr	r3, [r3, #0]
	cmn	r3, #1
	beq	.L1785
	mov	r0, r6
	bl	ftl_cmp_data_ver
	cmp	r0, #0
	bne	.L1785
	mov	r0, r9
	add	r1, sp, #56
	mov	r2, #1
	bl	log2phys
.L1785:
	ldr	r0, [sp, #52]
	cmn	r0, #1
	beq	.L1770
	ubfx	r0, r0, #10, #16
	bl	P2V_block_in_plane
	ldr	r2, [r5, #-2068]
	mov	r3, r0, asl #1
	mov	r1, r0
	ldrh	r3, [r2, r3]
	cmp	r3, #0
	beq	.L1787
.L1809:
	bl	decrement_vpc_count
	b	.L1770
.L1787:
	ldr	r0, .L1813+24
	bl	printk
	b	.L1770
.L1772:
	ldr	r3, [r5, #268]
	cmp	r3, #31
	addls	r2, r5, r3, asl #2
	addls	r3, r3, #1
	strls	r3, [r5, #268]
	strls	sl, [r2, #272]
	ldrh	r0, [r4, #0]
	bl	decrement_vpc_count
	ldr	r3, [r5, #-1016]
	cmn	r3, #1
	beq	.L1808
.L1789:
	cmp	r3, r6
	bls	.L1770
.L1808:
	str	r6, [r5, #-1016]
.L1770:
	add	r7, r7, #36
.L1769:
	ldr	r3, [sp, #44]
	cmp	r7, r3
	bne	.L1790
	ldrb	r3, [r4, #8]	@ zero_extendqisi2
	add	r8, r8, #1
	cmp	r3, #1
	uxth	r8, r8
	bne	.L1791
	ldr	r3, .L1813
	ldrb	r2, [r3, #928]	@ zero_extendqisi2
	cmp	r2, #0
	beq	.L1791
	movw	r2, #3910
	ldr	ip, [sp, #16]
	ldrh	r3, [r3, r2]
	cmp	ip, r8
	cmpeq	r3, r8
	beq	.L1802
.L1791:
	ldr	r3, .L1813
	movw	r2, #3908
	ldrh	r2, [r3, r2]
	cmp	r8, r2
	bne	.L1764
	mov	r2, #0
	mov	r1, #3840
	strh	r2, [r4, #4]	@ movhi
	ldrh	r1, [r3, r1]
	mov	r3, r2
	strh	r8, [r4, #2]	@ movhi
	movw	r2, #65535
	ldr	r0, [sp, #40]
	b	.L1792
.L1794:
	add	r0, r0, #2
	ldrh	ip, [r0, #14]
	cmp	ip, r2
	strneb	r3, [r4, #6]
	bne	.L1726
.L1793:
	add	r3, r3, #1
	uxth	r3, r3
.L1792:
	cmp	r3, r1
	bne	.L1794
	b	.L1726
.L1802:
.L1771:
	ldr	r7, [sp, #16]
	mov	r0, r4
	ldr	r3, [sp, #24]
	mov	r1, r7
	strh	r7, [r4, #2]	@ movhi
	mov	r2, r3
	strb	r3, [r4, #6]
.L1811:
	bl	ftl_sb_update_avl_pages
.L1726:
	mov	r0, #0
	add	sp, sp, #68
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, sl, fp, pc}
.L1814:
	.align	2
.L1813:
	.word	.LANCHOR0
	.word	.LANCHOR2
	.word	.LANCHOR2-1092
	.word	-2048
	.word	-2000
	.word	-1952
	.word	.LC116
	.fnend
	.size	FtlRecoverySuperblock, .-FtlRecoverySuperblock
	.align	2
	.global	FtlWriteDumpData
	.type	FtlWriteDumpData, %function
FtlWriteDumpData:
	.fnstart
	@ args = 0, pretend = 0, frame = 40
	@ frame_needed = 0, uses_anonymous_args = 0
	stmfd	sp!, {r4, r5, r6, r7, r8, lr}
	.save {r4, r5, r6, r7, r8, lr}
	.pad #40
	sub	sp, sp, #40
	ldr	r4, .L1824
	ldr	r3, .L1824+4
	mov	r2, r4
	ldrh	r3, [r4, r3]
	cmp	r3, #0
	beq	.L1816
	ldrb	r1, [r4, #-2040]	@ zero_extendqisi2
	cmp	r1, #0
	bne	.L1816
	ldr	r5, .L1824+8
	movw	r0, #3908
	ldrb	r1, [r4, #-2041]	@ zero_extendqisi2
	ldrh	r0, [r5, r0]
	mul	r1, r0, r1
	cmp	r3, r1
	beq	.L1816
	ldrb	r8, [r4, #-2038]	@ zero_extendqisi2
	mov	r3, #3840
	ldr	r7, [r5, #3968]
	cmp	r8, #0
	ldrh	r6, [r5, r3]
	bne	.L1815
	sub	r7, r7, #1
	mov	r1, sp
	mov	r2, r8
	mov	r0, r7
	bl	log2phys
	ldr	r3, [sp, #0]
	ldr	r0, [r4, #-2080]
	ldr	r4, [r4, #-956]
	cmn	r3, #1
	str	r3, [sp, #8]
	str	r7, [sp, #20]
	str	r0, [sp, #12]
	str	r4, [sp, #16]
	str	r8, [r4, #4]
	beq	.L1818
	add	r0, sp, #4
	mov	r1, #1
	mov	r2, r8
	bl	FlashReadPages
	b	.L1819
.L1818:
	movw	r3, #3918
	mov	r1, #255
	ldrh	r2, [r5, r3]
	bl	ftl_memset
.L1819:
	ldr	r3, .L1824+12
	mov	r6, r6, asl #2
	ldr	r5, .L1824
	ldr	r8, .L1824+4
	strh	r3, [r4, #0]	@ movhi
	b	.L1820
.L1823:
	ldrh	r3, [r5, r8]
	cmp	r3, #0
	beq	.L1821
	ldr	r3, [sp, #8]
	sub	r0, r0, #2048
	str	r7, [r4, #8]
	sub	r6, r6, #1
	str	r3, [r4, #12]
	ldr	r3, .L1824+16
	ldrh	r3, [r5, r3]
	strh	r3, [r4, #2]	@ movhi
	bl	get_new_active_ppa
	ldr	r3, [r5, #-1848]
	mov	r2, #0
	mov	r1, #1
	str	r0, [sp, #8]
	add	r0, sp, #4
	str	r3, [r4, #4]
	add	r3, r3, #1
	cmn	r3, #1
	str	r3, [r5, #-1848]
	moveq	r3, #0
	streq	r3, [r5, #-1848]
	mov	r3, r2
	bl	FlashProgPages
	ldr	r3, .L1824+16
	ldrh	r0, [r5, r3]
	bl	decrement_vpc_count
.L1820:
	cmp	r6, #0
	ldr	r0, .L1824
	bne	.L1823
.L1821:
	mov	r3, #1
	strb	r3, [r0, #-2038]
	b	.L1815
.L1816:
	mov	r3, #0
	strb	r3, [r2, #-2038]
.L1815:
	add	sp, sp, #40
	ldmfd	sp!, {r4, r5, r6, r7, r8, pc}
.L1825:
	.align	2
.L1824:
	.word	.LANCHOR2
	.word	-2044
	.word	.LANCHOR0
	.word	-3947
	.word	-2048
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
	ldr	r7, .L1830
	movw	r6, #3946
	ldr	r5, .L1830+4
	b	.L1827
.L1829:
	ldr	r3, [r5, #-1904]
	mov	r2, #12
	mla	r3, r2, r4, r3
	ldr	r3, [r3, #4]
	cmp	r3, #0
	bge	.L1828
	mov	r0, r4
	bl	flush_l2p_region
.L1828:
	add	r4, r4, #1
	uxth	r4, r4
.L1827:
	ldrh	r3, [r7, r6]
	cmp	r3, r4
	bhi	.L1829
	mov	r0, #0
	ldmfd	sp!, {r3, r4, r5, r6, r7, pc}
.L1831:
	.align	2
.L1830:
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
	ldr	r6, .L1839
	ldr	r1, .L1839+4
	ldr	r0, .L1839+8
	bl	printk
	movw	r3, #3850
	ldrh	r2, [r6, r3]
	mov	r1, #0
	ldr	r5, .L1839+12
	mov	r2, r2, asl #1
	ldr	r0, [r5, #-936]
	bl	ftl_memset
	b	.L1833
.L1835:
	mov	r0, r4
	add	r1, sp, #4
	mov	r2, #0
	bl	log2phys
	ldr	r0, [sp, #4]
	cmn	r0, #1
	beq	.L1834
	ubfx	r0, r0, #10, #16
	bl	P2V_block_in_plane
	ldr	r3, [r5, #-936]
	mov	r0, r0, asl #1
	ldrh	r2, [r3, r0]
	add	r2, r2, #1
	strh	r2, [r3, r0]	@ movhi
.L1834:
	add	r4, r4, #1
.L1833:
	ldr	r3, [r6, #3968]
	cmp	r4, r3
	bcc	.L1835
	mov	r4, #0
	ldr	r7, .L1839
	movw	r8, #3848
	ldr	r5, .L1839+12
	b	.L1836
.L1838:
	ldr	r3, [r5, #-2068]
	mov	r6, r4, asl #1
	ldrh	r2, [r3, r6]
	ldr	r3, [r5, #-936]
	ldrh	r3, [r3, r6]
	cmp	r2, r3
	beq	.L1837
	movw	r1, #65535
	cmp	r2, r1
	beq	.L1837
	ldr	r0, .L1839+16
	mov	r1, r4
	bl	printk
	ldr	r3, .L1839+20
	ldrh	r3, [r5, r3]
	cmp	r3, r4
	beq	.L1837
	ldr	r3, .L1839+24
	ldrh	r3, [r5, r3]
	cmp	r3, r4
	beq	.L1837
	ldr	r3, .L1839+28
	ldrh	r3, [r5, r3]
	cmp	r3, r4
	beq	.L1837
	ldr	r3, [r5, #-936]
	mov	r0, r4
	ldrh	r2, [r3, r6]
	ldr	r3, [r5, #-2068]
	strh	r2, [r3, r6]	@ movhi
	bl	update_vpc_list
	bl	l2p_flush
	bl	FtlVpcTblFlush
.L1837:
	add	r4, r4, #1
	uxth	r4, r4
.L1836:
	ldrh	r3, [r7, r8]
	cmp	r3, r4
	bhi	.L1838
	ldmfd	sp!, {r2, r3, r4, r5, r6, r7, r8, pc}
.L1840:
	.align	2
.L1839:
	.word	.LANCHOR0
	.word	.LANCHOR3+15
	.word	.LC117
	.word	.LANCHOR2
	.word	.LC118
	.word	-2048
	.word	-1952
	.word	-2000
	.fnend
	.size	FtlVpcCheckAndModify, .-FtlVpcCheckAndModify
	.align	2
	.global	allocate_new_data_superblock
	.type	allocate_new_data_superblock, %function
allocate_new_data_superblock:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	stmfd	sp!, {r4, r5, r6, lr}
	.save {r4, r5, r6, lr}
	movw	r3, #65535
	ldrh	r5, [r0, #0]
	mov	r4, r0
	cmp	r5, r3
	beq	.L1842
	ldr	r3, .L1852
	mov	r0, r5
	ldr	r2, [r3, #-2068]
	mov	r3, r5, asl #1
	ldrh	r3, [r2, r3]
	cmp	r3, #0
	beq	.L1843
	bl	INSERT_DATA_LIST
	b	.L1842
.L1843:
	bl	INSERT_FREE_LIST
.L1842:
	mov	r3, #0
	strb	r3, [r4, #8]
	ldr	r3, .L1852
	sub	r2, r3, #2000
	cmp	r4, r2
	beq	.L1844
	ldr	r1, .L1852+4
	movw	r2, #3860
	ldrh	r2, [r1, r2]
	cmp	r2, #1
	beq	.L1844
	ldrb	r1, [r1, #928]	@ zero_extendqisi2
	cmp	r1, #0
	beq	.L1845
.L1844:
	mov	r3, #1
	strb	r3, [r4, #8]
	b	.L1846
.L1845:
	sub	r1, r3, #2048
	cmp	r4, r1
	bne	.L1846
	cmp	r2, #3
	beq	.L1847
	ldr	r2, [r3, #-1712]
	cmp	r2, #1
	bne	.L1848
.L1847:
	mov	r2, #1
	strb	r2, [r3, #-2040]
.L1848:
	ldr	r3, [r3, #-1888]
	ldr	r2, .L1852
	cmp	r3, #0
	beq	.L1846
	ldr	r3, [r2, #-1828]
	cmp	r3, #29
	movls	r3, #1
	strlsb	r3, [r2, #-2040]
.L1846:
	ldr	r3, .L1852+8
	ldr	r6, .L1852
	ldrh	r0, [r6, r3]
	movw	r3, #65535
	cmp	r0, r3
	beq	.L1849
	cmp	r5, r0
	bne	.L1850
	ldr	r2, [r6, #-2068]
	mov	r3, r0, asl #1
	ldrh	r3, [r2, r3]
	cmp	r3, #0
	beq	.L1851
.L1850:
	bl	update_vpc_list
.L1851:
	ldr	r3, .L1852+8
	mvn	r2, #0
	strh	r2, [r6, r3]	@ movhi
.L1849:
	mov	r0, r4
	bl	allocate_data_superblock
	bl	l2p_flush
	mov	r0, #0
	bl	FtlEctTblFlush
	bl	FtlVpcTblFlush
	mov	r0, #0
	ldmfd	sp!, {r4, r5, r6, pc}
.L1853:
	.align	2
.L1852:
	.word	.LANCHOR2
	.word	.LANCHOR0
	.word	-1028
	.fnend
	.size	allocate_new_data_superblock, .-allocate_new_data_superblock
	.align	2
	.type	rk_ftl_garbage_collect.part.17, %function
rk_ftl_garbage_collect.part.17:
	.fnstart
	@ args = 0, pretend = 0, frame = 24
	@ frame_needed = 0, uses_anonymous_args = 0
	ldr	r3, .L1952
	movw	ip, #65535
	ldr	r2, .L1952+4
	stmfd	sp!, {r4, r5, r6, r7, r8, r9, sl, fp, lr}
	.save {r4, r5, r6, r7, r8, r9, sl, fp, lr}
	mov	r7, r0
	ldrh	r0, [r3, r2]
	.pad #36
	sub	sp, sp, #36
	cmp	r0, ip
	str	r1, [sp, #20]
	beq	.L1855
	ldr	r1, .L1952+8
	ldrh	r4, [r3, r1]
	cmp	r4, ip
	streqh	r0, [r3, r1]	@ movhi
	mvneq	r1, #0
	streqh	r1, [r3, r2]	@ movhi
.L1855:
	ldr	r2, [r3, #-1192]
	cmp	r7, #1
	add	r2, r2, #1
	add	r2, r2, r7, asl #7
	str	r2, [r3, #-1192]
	bne	.L1856
	ldr	r3, .L1952
	ldr	r3, [r3, #-1888]
	cmp	r3, #0
	bne	.L1857
	ldr	r3, .L1952+12
	ldrb	r3, [r3, #928]	@ zero_extendqisi2
	cmp	r3, #0
	beq	.L1856
.L1857:
	ldr	r4, .L1952
	ldr	r3, [r4, #-1828]
	cmp	r3, #29
	bhi	.L1856
	mov	r3, #400
	ldrh	r3, [r4, r3]
	add	r2, r2, r3
	str	r2, [r4, #-1192]
	bl	FtlGcReFreshBadBlk
	ldr	r3, .L1952+16
	movw	r2, #65535
	ldrh	r3, [r4, r3]
	cmp	r3, r2
	bne	.L1856
	ldr	r2, .L1952+8
	ldrh	r2, [r4, r2]
	cmp	r2, r3
	bne	.L1856
	ldr	r3, [r4, #-1192]
	cmp	r3, #1024
	bhi	.L1858
	ldr	r3, .L1952+20
	ldrh	r3, [r4, r3]
	cmp	r3, #63
	bhi	.L1856
.L1858:
	ldr	r3, .L1952
	mov	r2, #400
	mov	r1, #0
	str	r1, [r3, #-1192]
	strh	r1, [r3, r2]	@ movhi
	ldr	r1, [r3, #-1828]
	cmp	r1, #0
	moveq	r1, #6
	beq	.L1948
	cmp	r1, #5
	bhi	.L1860
	mov	r1, #18
.L1948:
	strh	r1, [r3, r2]	@ movhi
.L1860:
	ldr	r2, .L1952+12
	ldrb	r2, [r2, #928]	@ zero_extendqisi2
	cmp	r2, #0
	beq	.L1861
	ldr	r2, .L1952+20
	ldr	r1, .L1952+24
	ldrh	r2, [r3, r2]
	ldrh	r3, [r3, r1]
	add	r3, r3, #64
	cmp	r2, r3
	bgt	.L1856
.L1861:
	mov	r0, #32
	movw	r8, #65535
	bl	List_get_gc_head_node
	uxth	r2, r0
	cmp	r2, r8
	beq	.L1862
	ldr	r5, .L1952
	ldr	r6, .L1952+28
	ldrh	r0, [r5, r6]
	cmp	r0, #0
	moveq	r3, #1
	streqh	r3, [r5, r6]	@ movhi
	beq	.L1862
	ldr	r3, .L1952+12
	movw	ip, #3910
	mov	lr, #3840
	ldr	r9, [r5, #-2068]
	mov	r2, r2, asl #1
	ldrh	ip, [r3, ip]
	ldrh	r3, [r3, lr]
	ldrh	r1, [r9, r2]
	mul	r3, r3, ip
	add	r3, r3, #1
	cmp	r1, r3
	bgt	.L1862
	add	fp, r0, #1
	mov	sl, #0
	str	sl, [r5, #-1184]
	uxth	fp, fp
	strh	fp, [r5, r6]	@ movhi
	str	r2, [sp, #12]
	bl	List_get_gc_head_node
	ldr	r2, [sp, #12]
	uxth	r4, r0
	cmp	r4, r8
	beq	.L1862
	ldrh	r2, [r9, r2]
	mov	r8, r4, asl #1
	ldr	r0, .L1952+32
	mov	r1, fp
	ldrh	r3, [r9, r8]
	str	r2, [sp, #0]
	mov	r2, r4
	bl	printk
	ldrh	r3, [r5, r6]
	cmp	r3, #40
	bls	.L1864
	ldr	r3, [r5, #-2068]
	ldrh	r3, [r3, r8]
	cmp	r3, #32
	strhih	sl, [r5, r6]	@ movhi
.L1864:
	ldr	r2, .L1952
	mov	r3, #400
	mov	r1, #6
	strh	r1, [r2, r3]	@ movhi
	b	.L1866
.L1862:
	bl	GetSwlReplaceBlock
	movw	r3, #65535
	cmp	r0, r3
	mov	r4, r0
	bne	.L1866
	ldr	r2, .L1952
	mov	r3, #400
	mov	r1, #0
	strh	r1, [r2, r3]	@ movhi
.L1856:
	ldr	r6, .L1952
	movw	r4, #65535
	ldr	r3, .L1952+16
	ldrh	r2, [r6, r3]
	cmp	r2, r4
	bne	.L1866
	ldr	r3, .L1952+36
	ldrh	r3, [r6, r3]
	cmp	r3, r2
	movne	r4, r2
	bne	.L1866
	ldr	r2, .L1952+8
	ldrh	r8, [r6, r2]
	cmp	r8, r3
	movne	r4, r3
	bne	.L1866
	ldr	sl, .L1952+20
	ldr	r2, [r6, #-1192]
	ldrh	r3, [r6, sl]
	cmp	r3, #23
	movhi	r3, #1024
	movls	r3, #5120
	cmp	r2, r3
	movls	r4, r8
	bls	.L1866
	mov	r2, #400
	mov	r3, #0
	str	r3, [r6, #-1192]
	strh	r3, [r6, r2]	@ movhi
	bl	GetSwlReplaceBlock
	cmp	r0, r8
	mov	r4, r0
	bne	.L1868
	ldr	r3, .L1952+24
	ldrh	r1, [r6, sl]
	ldrh	r2, [r6, r3]
	cmp	r1, r2
	movcs	r2, #80
	strcsh	r2, [r6, r3]	@ movhi
	bcs	.L1878
	mov	r0, #64
	bl	List_get_gc_head_node
	uxth	r3, r0
	cmp	r3, r4
	beq	.L1878
	ldr	r2, [r6, #-1008]
	cmp	r2, #0
	bne	.L1871
	ldr	r2, .L1952+12
	movw	r1, #3860
	ldrh	r1, [r2, r1]
	cmp	r1, #3
	beq	.L1871
	ldr	r1, [r6, #-1712]
	cmp	r1, #0
	bne	.L1871
	ldr	r1, [r6, #-1888]
	cmp	r1, #0
	bne	.L1871
	ldrb	r0, [r2, #928]	@ zero_extendqisi2
	cmp	r0, #0
	beq	.L1872
.L1871:
	ldr	r1, .L1952
	mov	r3, r3, asl #1
	mov	ip, #3840
	movw	lr, #3860
	ldr	r2, [r1, #-2068]
	ldrh	r0, [r2, r3]
	movw	r2, #3910
	ldr	r3, .L1952+12
	ldrh	r2, [r3, r2]
	ldrh	ip, [r3, ip]
	ldrh	r3, [r3, lr]
	mul	ip, ip, r2
	cmp	r3, #3
	moveq	r3, r2, lsr #1
	movne	r3, #0
	add	r3, ip, r3
	cmp	r0, r3
	bgt	.L1874
	mov	r0, #0
	bl	List_get_gc_head_node
	ldr	r2, .L1952+12
	ldr	r3, .L1952
	ldr	r2, [r2, #3968]
	ldr	r1, [r3, #-1896]
	add	r2, r2, r2, asl #1
	cmp	r1, r2, lsr #2
	ldr	r2, .L1952+24
	movhi	r1, #128
	movls	r1, #160
	strh	r1, [r3, r2]	@ movhi
	uxth	r4, r0
	b	.L1876
.L1874:
	ldr	r3, .L1952+24
	mov	r2, #128
	strh	r2, [r1, r3]	@ movhi
	b	.L1878
.L1872:
	ldr	r2, [r6, #-2068]
	mov	r3, r3, asl #1
	ldr	r6, .L1952
	ldr	r5, .L1952+24
	ldrh	r3, [r2, r3]
	cmp	r3, #7
	movhi	r3, #64
	strhih	r3, [r6, r5]	@ movhi
	bhi	.L1878
	bl	List_get_gc_head_node
	mov	r3, #128
	strh	r3, [r6, r5]	@ movhi
	uxth	r4, r0
.L1876:
	movw	r3, #65535
	cmp	r4, r3
	beq	.L1878
.L1868:
	ldr	r1, .L1952
	mov	r0, r4, asl #1
	ldr	r3, .L1952+20
	ldr	ip, [r1, #-2088]
	ldrh	r2, [r1, r3]
	ldr	r3, [r1, #-2068]
	ldrh	r3, [r3, r0]
	ldrh	r0, [ip, r0]
	str	r0, [sp, #0]
	ldr	r0, .L1952+40
	ldrh	r1, [r1, r0]
	ldr	r0, .L1952+44
	str	r1, [sp, #4]
	mov	r1, r4
	bl	printk
.L1878:
	bl	FtlGcReFreshBadBlk
.L1866:
	movw	r1, #65535
	rsb	r0, r1, r4
	rsbs	r2, r0, #0
	ldr	r5, .L1952
	adc	r2, r2, r0
	cmp	r7, #0
	movne	r7, #0
	andeq	r7, r2, #1
	cmp	r7, #0
	beq	.L1879
	ldr	r3, .L1952+20
	ldrh	r2, [r5, r3]
	cmp	r2, #24
	movhi	r6, #1
	bhi	.L1880
	ldr	r1, .L1952+12
	movw	r3, #3908
	cmp	r2, #16
	ldrh	r6, [r1, r3]
	movhi	r6, r6, lsr #5
	bhi	.L1880
	cmp	r2, #12
	movhi	r6, r6, lsr #4
	bhi	.L1880
	cmp	r2, #8
	movhi	r6, r6, lsr #2
.L1880:
	ldr	r1, .L1952+40
	ldr	r3, .L1952
	ldrh	r0, [r5, r1]
	cmp	r0, r2
	mov	r0, r1
	bcs	.L1884
	ldr	r2, .L1952+36
	movw	ip, #65535
	ldrh	r2, [r3, r2]
	cmp	r2, ip
	bne	.L1885
	ldr	ip, .L1952+8
	ldrh	ip, [r3, ip]
	cmp	ip, r2
	bne	.L1885
	mov	r2, #400
	ldrh	r0, [r3, r2]
	cmp	r0, #0
	bne	.L1886
	ldr	r2, .L1952+12
	ldr	ip, [r3, #-1896]
	ldr	r2, [r2, #3968]
	add	r2, r2, r2, asl #1
	cmp	ip, r2, lsr #2
	movcs	r2, #18
	bcs	.L1950
.L1886:
	ldr	r3, .L1952
	ldr	r2, .L1952+48
	ldr	r1, .L1952+40
	ldrh	r2, [r3, r2]
	add	r2, r2, r2, asl #1
	mov	r2, r2, asr #2
.L1950:
	strh	r2, [r3, r1]	@ movhi
	mov	r2, #0
	ldr	r3, .L1952
	str	r2, [r3, #-1184]
	b	.L1889
.L1885:
	ldr	r3, .L1952
	ldr	r2, .L1952+48
	ldrh	r2, [r3, r2]
	add	r2, r2, r2, asl #1
	mov	r2, r2, asr #2
	strh	r2, [r3, r0]	@ movhi
.L1884:
	ldr	r3, .L1952
	movw	r4, #65535
	ldr	r8, [sp, #20]
	ldr	r2, [r3, #-1888]
	cmp	r8, #2
	movhi	r3, #0
	movls	r3, #1
	cmp	r2, #0
	moveq	r3, #0
	cmp	r3, #0
	addne	r6, r6, #1
	uxthne	r6, r6
	b	.L1890
.L1879:
	ldr	r3, .L1952+36
	ldrh	r3, [r5, r3]
	cmp	r3, r1
	bne	.L1891
	ldr	r1, .L1952+8
	ldrh	r1, [r5, r1]
	cmp	r1, r3
	movne	r2, #0
	andeq	r2, r2, #1
	cmp	r2, #0
	beq	.L1891
	ldr	r2, .L1952+16
	ldrh	r4, [r5, r2]
	cmp	r4, r3
	movne	r4, r3
	bne	.L1891
	ldr	r3, .L1952+20
	mov	r6, #400
	str	r7, [r5, #-1184]
	ldrh	r7, [r5, r3]
	add	r3, r3, #856
	ldrh	r2, [r5, r3]
	cmp	r2, r7
	bcs	.L1892
	ldrh	r2, [r5, r6]
	cmp	r2, #0
	bne	.L1893
	ldr	r2, .L1952+12
	ldr	r1, [r5, #-1896]
	ldr	r2, [r2, #3968]
	add	r2, r2, r2, asl #1
	cmp	r1, r2, lsr #2
	movcs	r2, #18
	strcsh	r2, [r5, r3]	@ movhi
	bcs	.L1895
.L1893:
	ldr	r3, .L1952
	ldr	r2, .L1952+48
	ldr	r1, .L1952+40
	ldrh	r2, [r3, r2]
	add	r2, r2, r2, asl #1
	mov	r2, r2, asr #2
	strh	r2, [r3, r1]	@ movhi
.L1895:
	bl	FtlReadRefresh
	mov	r3, #400
	ldr	r2, .L1952
	b	.L1951
.L1892:
	ldrh	r0, [r5, r6]
	cmp	r0, #0
	bne	.L1891
	ldr	r2, .L1952+48
	ldrh	r4, [r5, r2]
	add	r2, r4, r4, asl #1
	mov	r2, r2, asr #2
	strh	r2, [r5, r3]	@ movhi
	bl	List_get_gc_head_node
	ldr	r3, [r5, #-2068]
	ldr	r1, .L1952+12
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
	ble	.L1896
	sub	r4, r4, #1
	cmp	r7, r4
	blt	.L1896
	bl	FtlReadRefresh
	ldrh	r0, [r5, r6]
	b	.L1889
.L1896:
	cmp	r3, #0
	movwne	r4, #65535
	bne	.L1891
	movw	r0, #65535
	bl	decrement_vpc_count
	ldr	r3, .L1952+20
	ldr	r2, .L1952
	ldrh	r0, [r2, r3]
	add	r0, r0, #1
	b	.L1889
.L1891:
	ldr	r3, .L1952
	ldr	r6, [r3, #-1888]
	cmp	r6, #0
	movne	r6, #2
	moveq	r6, #1
.L1890:
	ldr	r3, .L1952
	movw	r0, #65535
	ldr	r2, .L1952+16
	ldrh	r1, [r3, r2]
	cmp	r1, r0
	bne	.L1897
	cmp	r4, r1
	strneh	r4, [r3, r2]	@ movhi
	bne	.L1899
	ldr	r2, .L1952+8
	ldrh	r1, [r3, r2]
	cmp	r1, r4
	beq	.L1899
	ldr	r0, [r3, #-2068]
	mov	r1, r1, asl #1
	ldrh	r1, [r0, r1]
	cmp	r1, #0
	mvneq	r1, #0
	streqh	r1, [r3, r2]	@ movhi
	ldr	r3, .L1952
	ldr	r1, .L1952+16
	ldrh	r0, [r3, r2]
	strh	r0, [r3, r1]	@ movhi
	mvn	r1, #0
	strh	r1, [r3, r2]	@ movhi
.L1899:
	ldr	r5, .L1952
	mov	r3, #0
	ldr	r7, .L1952+16
	strb	r3, [r5, #-1752]
	movw	r3, #65535
	ldrh	r0, [r5, r7]
	cmp	r0, r3
	beq	.L1897
	bl	IsBlkInGcList
	cmp	r0, #0
	mvnne	r3, #0
	strneh	r3, [r5, r7]	@ movhi
	ldr	r3, .L1952+12
	ldr	r7, .L1952
	ldrb	r3, [r3, #928]	@ zero_extendqisi2
	cmp	r3, #0
	beq	.L1902
	ldr	r3, .L1952+16
	ldrh	r0, [r7, r3]
	bl	ftl_get_blk_mode
	strb	r0, [r7, #-1752]
.L1902:
	ldr	r8, .L1952+16
	movw	r3, #65535
	ldr	r5, .L1952
	ldrh	r2, [r7, r8]
	cmp	r2, r3
	beq	.L1897
	add	r0, r5, r8
	bl	make_superblock
	ldr	r1, .L1952+52
	movw	r2, #402
	mov	r3, #0
	strh	r3, [r5, r2]	@ movhi
	strh	r3, [r5, r1]	@ movhi
	strb	r3, [r5, #-1754]
	ldrh	r3, [r5, r8]
	ldr	r2, [r5, #-2068]
	mov	r3, r3, asl #1
	ldrh	r2, [r2, r3]
	mov	r3, #404
	strh	r2, [r5, r3]	@ movhi
.L1897:
	ldr	r3, .L1952
	ldr	r2, .L1952+16
	ldrh	r1, [r3, r2]
	sub	r2, r2, #288
	ldrh	r2, [r3, r2]
	cmp	r2, r1
	mov	r2, r3
	beq	.L1903
	ldr	r0, .L1952+56
	ldrh	r3, [r3, r0]
	cmp	r3, r1
	movne	fp, r4
	bne	.L1947
.L1904:
.L1903:
	ldr	r3, .L1952+16
	mvn	r1, #0
	strh	r1, [r2, r3]	@ movhi
	mov	r3, #400
.L1951:
	ldrh	r0, [r2, r3]
	b	.L1889
.L1947:
	ldr	r4, .L1952
	movw	r1, #65535
	ldr	r3, .L1952+16
	ldrh	r3, [r4, r3]
	cmp	r3, r1
	bne	.L1906
	ldr	r5, .L1952+28
	mov	r3, #0
	str	r3, [r4, #-1184]
.L1946:
	ldrh	r8, [r4, r5]
	mov	r0, r8
	bl	List_get_gc_head_node
	ldr	r3, .L1952+16
	movw	r2, #65535
	uxth	r7, r0
	strh	r7, [r4, r3]	@ movhi
	cmp	r7, r2
	bne	.L1908
	ldr	r2, .L1952
	add	r3, r3, #572
	mov	r1, #0
	mov	r0, #8
	strh	r1, [r2, r3]	@ movhi
	b	.L1889
.L1908:
	mov	r0, r7
	add	r8, r8, #1
	bl	IsBlkInGcList
	cmp	r0, #0
	strneh	r8, [r4, r5]	@ movhi
	bne	.L1946
	ldr	ip, .L1952+12
	movw	r3, #3908
	mov	sl, #3840
	ldr	r1, [r4, #-2068]
	mov	r2, r7, asl #1
	uxth	r8, r8
	ldrh	r3, [ip, r3]
	ldrh	ip, [ip, sl]
	strh	r8, [r4, r5]	@ movhi
	ldrh	r0, [r1, r2]
	mul	r3, ip, r3
	add	ip, r3, r3, lsr #31
	cmp	r0, ip, asr #1
	bgt	.L1911
	cmp	r8, #48
	bls	.L1912
	cmp	r0, #8
	bls	.L1912
	ldr	r0, .L1952+60
	ldrh	r0, [r4, r0]
	cmp	r0, #35
	bhi	.L1912
.L1911:
	mov	r0, #0
	strh	r0, [r4, r5]	@ movhi
.L1912:
	ldrh	r2, [r1, r2]
	movw	r8, #65535
	ldr	sl, .L1952
	cmp	r2, r3
	cmpge	fp, r8
	movne	r3, #0
	moveq	r3, #1
	bne	.L1913
	ldr	r3, .L1952+16
	mvn	r2, #0
	strh	r2, [sl, r3]	@ movhi
	add	r3, r3, #572
	mov	r2, #0
	strh	r2, [sl, r3]	@ movhi
	mov	r3, #400
	ldrh	r0, [sl, r3]
	b	.L1889
.L1913:
	cmp	r2, #0
	bne	.L1914
	movw	r0, #65535
	bl	decrement_vpc_count
	ldrh	r3, [r4, r5]
	add	r3, r3, #1
	strh	r3, [r4, r5]	@ movhi
	b	.L1946
.L1914:
	strb	r3, [sl, #-1752]
	ldr	r3, .L1952+12
	ldrb	r3, [r3, #928]	@ zero_extendqisi2
	cmp	r3, #0
	beq	.L1915
	mov	r0, r7
	bl	ftl_get_blk_mode
	strb	r0, [sl, #-1752]
.L1915:
	ldr	r4, .L1952
	sub	r0, r4, #1760
	bl	make_superblock
	movw	r1, #402
	mov	r3, #0
	strh	r3, [r4, r1]	@ movhi
	ldr	r1, .L1952+16
	ldr	r0, [r4, #-2068]
	ldrh	r1, [r4, r1]
	mov	r1, r1, asl #1
	ldrh	r0, [r0, r1]
	mov	r1, #404
	strb	r3, [r4, #-1754]
	strh	r0, [r4, r1]	@ movhi
	ldr	r1, .L1952+52
	strh	r3, [r4, r1]	@ movhi
.L1906:
	bl	FtlReadRefresh
	ldr	r3, .L1952
	mov	r2, #1
	movw	r1, #3908
	str	r2, [r3, #-1004]
	ldr	r2, .L1952+12
	ldrh	r9, [r2, r1]
	ldrb	r1, [r2, #928]	@ zero_extendqisi2
	cmp	r1, #0
	beq	.L1916
	ldrb	r1, [r3, #-1752]	@ zero_extendqisi2
	cmp	r1, #1
	movweq	r1, #3910
	ldreqh	r9, [r2, r1]
.L1916:
	ldr	r2, .L1952+52
	mov	sl, #0
	str	fp, [sp, #20]
	mov	fp, r9
	ldr	r4, .L1952
	ldrh	r3, [r3, r2]
	add	r2, r3, r6
	cmp	r2, r9
	rsbgt	r6, r3, r9
	uxthgt	r6, r6
	b	.L1918
.L1920:
	ldrh	r1, [r2, #2]!
	movw	r8, #65535
	add	r3, r3, #1
	cmp	r1, r8
	uxth	r3, r3
	orrne	r1, ip, r1, asl #10
	mlane	r8, r0, r7, lr
	addne	r7, r7, #1
	uxthne	r7, r7
	strne	r1, [r8, #4]
.L1926:
	cmp	r3, r5
	bne	.L1920
	ldr	r0, [r4, #-1164]
	mov	r1, r7
	ldrb	r2, [r4, #-1752]	@ zero_extendqisi2
	mov	r5, #0
	bl	FlashReadPages
	mov	r9, r5
	mov	r8, r6
	b	.L1921
.L1924:
	ldr	r3, [r4, #-1164]
	add	r2, r3, r5
	ldr	r3, [r3, r5]
	ldr	r6, [r2, #12]
	cmn	r3, #1
	beq	.L1922
	ldrh	r3, [r6, #0]
	movw	r1, #61589
	cmp	r3, r1
	bne	.L1922
	add	r1, sp, #28
	mov	r2, #0
	ldr	r0, [r6, #8]
	bl	log2phys
	ldr	r1, [r4, #-1164]
	ldr	r2, [sp, #28]
	add	r1, r1, r5
	bic	r2, r2, #-2147483648
	ldr	r3, [r1, #4]
	cmp	r2, r3
	bne	.L1922
	movw	r3, #402
	ldr	r0, [r4, #-1180]
	ldrh	r2, [r4, r3]
	ldr	r1, [r1, #16]
	add	r2, r2, #1
	strh	r2, [r4, r3]	@ movhi
	ldr	r2, [r4, #-984]
	mov	r3, #36
	mla	r2, r3, r0, r2
	str	r1, [r2, #16]
	str	r2, [sp, #12]
	str	r3, [sp, #16]
	bl	Ftl_get_new_temp_ppa
	ldr	r2, [sp, #12]
	ldr	r1, [r4, #-1180]
	str	r0, [r2, #4]
	ldr	r3, [sp, #16]
	ldr	r2, [r4, #-984]
	mla	r3, r3, r1, r2
	ldr	r2, [r4, #-1164]
	add	r2, r2, r5
	ldr	r1, [r2, #8]
	str	r1, [r3, #8]
	mov	r1, #1
	ldr	r2, [r2, #12]
	str	r2, [r3, #12]
	ldr	r3, [sp, #28]
	str	r3, [r6, #12]
	ldr	r3, .L1952+36
	ldrh	r3, [r4, r3]
	strh	r3, [r6, #2]	@ movhi
	ldr	r3, [r4, #-1848]
	ldr	r0, [r4, #-1164]
	str	r3, [r6, #4]
	add	r0, r0, r5
	ldr	r3, [r4, #-1180]
	add	r3, r3, #1
	str	r3, [r4, #-1180]
	bl	FtlGcBufAlloc
	ldr	r3, .L1952+12
	ldrb	r3, [r3, #928]	@ zero_extendqisi2
	cmp	r3, #0
	bne	.L1923
	ldrb	r3, [r4, #-1945]	@ zero_extendqisi2
	ldr	r2, [r4, #-1180]
	cmp	r2, r3
	beq	.L1923
	ldr	r3, .L1952+64
	ldrh	r3, [r4, r3]
	cmp	r3, #0
	bne	.L1922
.L1923:
	bl	Ftl_gc_temp_data_write_back
	cmp	r0, #0
	ldrne	r3, .L1952
	movne	r2, #0
	strne	r2, [r3, #-1004]
	movne	r2, #400
	bne	.L1951
.L1922:
	add	r9, r9, #1
	add	r5, r5, #36
	uxth	r9, r9
.L1921:
	cmp	r9, r7
	bne	.L1924
	add	sl, sl, #1
	mov	r6, r8
.L1918:
	uxth	r3, sl
	ldr	r2, .L1952+52
	cmp	r3, r6
	ldr	r3, .L1952
	bcs	.L1925
	ldr	r1, .L1952+12
	mov	r3, #3840
	ldrh	ip, [r4, r2]
	mov	r7, #0
	ldr	lr, [r4, #-1164]
	mov	r0, #36
	ldrh	r5, [r1, r3]
	add	ip, ip, sl
	ldr	r2, .L1952+68
	mov	r3, r7
	b	.L1926
.L1925:
	ldrh	r1, [r3, r2]
	mov	r9, fp
	ldr	fp, [sp, #20]
	add	r6, r6, r1
	uxth	r6, r6
	strh	r6, [r3, r2]	@ movhi
	cmp	r6, r9
	bcs	.L1927
	movw	r2, #402
	mov	r1, #404
	ldrh	r2, [r3, r2]
	ldrh	r3, [r3, r1]
	cmp	r2, r3
	bne	.L1928
.L1927:
	ldr	r5, .L1952
	ldr	r3, [r5, #-1180]
	cmp	r3, #0
	beq	.L1929
	bl	Ftl_gc_temp_data_write_back
	cmp	r0, #0
	movne	r3, #0
	strne	r3, [r5, #-1004]
	movne	r3, #400
	ldrneh	r0, [r5, r3]
	bne	.L1889
.L1929:
	movw	r2, #402
	ldr	r3, .L1952
	ldrh	ip, [r5, r2]
	cmp	ip, #0
	bne	.L1930
	ldr	r2, .L1952+16
	ldr	r0, [r3, #-2068]
	ldrh	r1, [r3, r2]
	mov	r1, r1, asl #1
	ldrh	r4, [r0, r1]
	cmp	r4, #0
	beq	.L1930
	strh	ip, [r0, r1]	@ movhi
	ldrh	r0, [r3, r2]
	bl	update_vpc_list
	bl	FtlCacheWriteBack
	bl	l2p_flush
	bl	FtlVpcTblFlush
.L1930:
	ldr	r3, .L1952+16
	mvn	r1, #0
	ldr	r2, .L1952
	strh	r1, [r2, r3]	@ movhi
.L1928:
	ldr	r3, .L1952
	ldr	r2, .L1952+20
	ldrh	r2, [r3, r2]
	cmp	r2, #2
	ldrls	r2, .L1952+12
	movwls	r3, #3908
	ldrlsh	r6, [r2, r3]
	bls	.L1947
.L1931:
	mov	r1, #0
	str	r1, [r3, #-1004]
	mov	r1, #400
	ldrh	r0, [r3, r1]
	cmp	r0, #0
	addeq	r0, r2, #1
.L1889:
	add	sp, sp, #36
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, sl, fp, pc}
.L1953:
	.align	2
.L1952:
	.word	.LANCHOR2
	.word	-1140
	.word	-1142
	.word	.LANCHOR0
	.word	-1760
	.word	-2052
	.word	-1194
	.word	-1188
	.word	.LC119
	.word	-1952
	.word	-1196
	.word	.LC120
	.word	-1776
	.word	-1758
	.word	-2000
	.word	-1156
	.word	-1948
	.word	.LANCHOR2-1746
	.fnend
	.size	rk_ftl_garbage_collect.part.17, .-rk_ftl_garbage_collect.part.17
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
	ldr	r3, .L1960
	mov	r5, r1
	ldr	r0, [r3, #-1004]
	cmp	r0, #0
	movne	r0, #0
	ldmnefd	sp!, {r3, r4, r5, pc}
	ldr	r2, .L1960+4
	ldrh	r2, [r3, r2]
	cmp	r2, #47
	ldmlsfd	sp!, {r3, r4, r5, pc}
	ldr	r1, .L1960+8
	movw	r2, #2936
	ldrh	r1, [r1, r2]
	movw	r2, #65535
	cmp	r1, r2
	beq	.L1956
	ldr	r1, .L1960+12
	ldrh	r3, [r3, r1]
	cmp	r3, r2
	beq	.L1956
	mov	r0, #1
	bl	FtlGcFreeTempBlock
	cmp	r0, #0
	bne	.L1959
.L1956:
	mov	r0, r4
	mov	r1, r5
	ldmfd	sp!, {r3, r4, r5, lr}
	b	rk_ftl_garbage_collect.part.17
.L1959:
	mov	r0, #1
	ldmfd	sp!, {r3, r4, r5, pc}
.L1961:
	.align	2
.L1960:
	.word	.LANCHOR2
	.word	-2060
	.word	.LANCHOR1
	.word	-1952
	.fnend
	.size	rk_ftl_garbage_collect, .-rk_ftl_garbage_collect
	.align	2
	.global	FtlCacheWriteBack
	.type	FtlCacheWriteBack, %function
FtlCacheWriteBack:
	.fnstart
	@ args = 0, pretend = 0, frame = 8
	@ frame_needed = 0, uses_anonymous_args = 0
	ldr	r3, .L1984
	stmfd	sp!, {r0, r1, r4, r5, r6, r7, r8, r9, sl, lr}
	.save {r0, r1, r4, r5, r6, r7, r8, r9, sl, lr}
	ldr	r4, [r3, #408]
	ldr	r3, .L1984+4
	ldr	r1, [r3, #3960]
	cmp	r1, #0
	beq	.L1963
	ldrb	r8, [r3, #928]	@ zero_extendqisi2
	cmp	r8, #0
	beq	.L1964
	ldrb	r8, [r4, #8]	@ zero_extendqisi2
	sub	ip, r8, #1
	rsbs	r8, ip, #0
	adc	r8, r8, ip
.L1964:
	ldr	r0, [r3, #3964]
	mov	r2, r8
	ldrb	r3, [r4, #9]	@ zero_extendqisi2
	mov	r5, #0
	bl	FlashProgPages
	mov	r7, r5
	ldr	r6, .L1984+4
	ldr	r9, .L1984
	b	.L1965
.L1970:
	ldr	r2, [r6, #3964]
	add	r3, r2, r5
	ldr	r2, [r2, r5]
	cmn	r2, #1
	beq	.L1983
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
	beq	.L1968
	ubfx	r0, r0, #10, #16
	bl	P2V_block_in_plane
	ldr	r2, [r9, #-2068]
	mov	r3, r0, asl #1
	mov	sl, r0
	ldrh	r2, [r2, r3]
	cmp	r2, #0
	bne	.L1969
	ldr	r0, .L1984+8
	mov	r1, sl
	bl	printk
.L1969:
	mov	r0, sl
	bl	decrement_vpc_count
.L1968:
	add	r7, r7, #1
	add	r5, r5, #36
.L1965:
	ldr	r3, [r6, #3960]
	cmp	r7, r3
	bcc	.L1970
	b	.L1971
.L1979:
	ldr	r3, [r6, #3964]
	mvn	r2, #0
	movw	sl, #3908
	str	r2, [r3, r5]
	b	.L1972
.L1975:
	ldr	r0, [r3, #4]
	ubfx	r0, r0, #10, #16
	bl	P2V_block_in_plane
	ldrh	r3, [r4, #0]
	cmp	r3, r0
	bne	.L1973
	ldr	r2, [r7, #-2068]
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
.L1973:
	ldrh	r3, [r4, #4]
	cmp	r3, #0
	bne	.L1974
	mov	r0, r4
	bl	allocate_new_data_superblock
.L1974:
	ldr	r3, [r7, #-1612]
	add	r3, r3, #1
	str	r3, [r7, #-1612]
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
.L1972:
	ldr	r2, [r6, #3964]
	add	r3, r2, r5
	ldr	r2, [r2, r5]
	cmn	r2, #1
	beq	.L1975
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
	beq	.L1977
	ubfx	r0, r0, #10, #16
	bl	P2V_block_in_plane
	ldr	r2, [r7, #-2068]
	mov	r3, r0, asl #1
	mov	sl, r0
	ldrh	r2, [r2, r3]
	cmp	r2, #0
	bne	.L1978
	ldr	r0, .L1984+8
	mov	r1, sl
	bl	printk
.L1978:
	mov	r0, sl
	bl	decrement_vpc_count
.L1977:
	add	r9, r9, #1
	add	r5, r5, #36
	b	.L1966
.L1983:
	ldr	r6, .L1984+4
	mov	r5, #0
	ldr	r7, .L1984
	mov	r9, r5
.L1966:
	ldr	r3, [r6, #3960]
	cmp	r9, r3
	bcc	.L1979
	movw	r4, #16386
	ldr	r6, .L1984
	ldr	r5, .L1984+12
	b	.L1980
.L1981:
	mov	r0, #1
	mov	r1, r0
	bl	rk_ftl_garbage_collect
	subs	r4, r4, #1
	beq	.L1971
.L1980:
	ldrh	r3, [r6, r5]
	cmp	r3, #0
	bne	.L1981
.L1971:
	ldr	r3, .L1984+4
	mov	r2, #0
	str	r2, [r3, #3960]
.L1963:
	mov	r0, #0
	ldmfd	sp!, {r2, r3, r4, r5, r6, r7, r8, r9, sl, pc}
.L1985:
	.align	2
.L1984:
	.word	.LANCHOR2
	.word	.LANCHOR0
	.word	.LC121
	.word	-1138
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
	ldr	r2, .L2008
	ldrh	r1, [r2, r3]
	ldr	r2, .L2008+4
	ldr	r3, .L2008+8
	ldrh	r5, [r3, r2]
	movw	r2, #65535
	cmp	r5, r2
	beq	.L1988
	cmp	r0, #0
	beq	.L1989
	ldr	ip, .L2008+12
	movw	r0, #2936
	ldrh	lr, [ip, r0]
	cmp	lr, r2
	movne	r1, #2
	bne	.L1989
	mov	r2, #0
	strh	r2, [ip, r0]	@ movhi
	ldr	r2, .L2008+16
	ldrh	r3, [r3, r2]
	cmp	r3, #17
	movhi	r1, #2
.L1989:
	ldr	r4, .L2008+8
	sub	r0, r4, #1952
	bl	FtlGcScanTempBlk
	cmn	r0, #1
	str	r0, [sp, #4]
	beq	.L1990
	ldr	r3, [r4, #-2088]
	mov	r5, r5, asl #1
	ldrh	r2, [r3, r5]
	cmp	r2, #4
	bls	.L1991
	sub	r2, r2, #5
	mov	r0, #1
	strh	r2, [r3, r5]	@ movhi
	bl	FtlEctTblFlush
.L1991:
	ldr	r3, .L2008+8
	ldr	r2, [r3, #-1020]
	cmp	r2, #0
	bne	.L1992
	ldr	r2, [r3, #-1612]
	ldr	r0, [sp, #4]
	add	r2, r2, #1
	str	r2, [r3, #-1612]
	ubfx	r0, r0, #10, #16
	bl	FtlBbmMapBadBlock
	bl	FtlBbmTblFlush
.L1992:
	ldr	r3, .L2008+8
	mov	r2, #0
	str	r2, [r3, #-1020]
	b	.L2004
.L1990:
	ldr	r2, .L2008+12
	movw	r3, #2936
	ldrh	r2, [r2, r3]
	movw	r3, #65535
	cmp	r2, r3
	bne	.L2004
.L1988:
	ldr	r6, .L2008+8
	movw	r3, #65535
	ldr	r5, .L2008+4
	mov	r4, #0
	str	r4, [r6, #-1020]
	ldrh	r2, [r6, r5]
	cmp	r2, r3
	moveq	r0, r4
	beq	.L1993
	bl	FtlCacheWriteBack
	ldr	ip, .L2008
	movw	r0, #3908
	ldrb	r1, [r6, #-1945]	@ zero_extendqisi2
	ldrh	r3, [r6, r5]
	ldrh	r0, [ip, r0]
	ldr	r2, [r6, #-2068]
	ldr	sl, .L2008+20
	mov	r3, r3, asl #1
	mul	r1, r0, r1
	strh	r1, [r2, r3]	@ movhi
	ldr	r3, [r6, #-1864]
	ldrh	r2, [r6, sl]
	add	r3, r2, r3
	str	r3, [r6, #-1864]
	b	.L1994
.L1997:
	mov	r7, #12
	ldr	r8, [r6, #-1148]
	mul	r7, r7, r4
	add	r1, sp, #4
	mov	r2, #0
	add	r5, r8, r7
	ldr	r0, [r5, #8]
	bl	log2phys
	ldr	r0, [sp, #4]
	ldr	r3, [r8, r7]
	cmp	r0, r3
	bne	.L1995
	ubfx	r0, r0, #10, #16
	bl	P2V_block_in_plane
	add	r1, r5, #4
	mov	r2, #1
	mov	r7, r0
	ldr	r0, [r5, #8]
	bl	log2phys
	mov	r0, r7
	b	.L2007
.L1995:
	ldr	r3, [r5, #4]
	cmp	r0, r3
	beq	.L1996
	ldr	r3, .L2008+4
	ldrh	r0, [r6, r3]
.L2007:
	bl	decrement_vpc_count
.L1996:
	add	r4, r4, #1
	uxth	r4, r4
.L1994:
	ldrh	r3, [r6, sl]
	ldr	r5, .L2008+8
	cmp	r3, r4
	bhi	.L1997
	movw	r0, #65535
	bl	decrement_vpc_count
	ldr	r3, .L2008+4
	ldr	r1, [r5, #-2068]
	ldrh	r0, [r5, r3]
	mov	r5, r3
	mov	r2, r0, asl #1
	ldrh	r2, [r1, r2]
	cmp	r2, #0
	beq	.L1998
	bl	INSERT_DATA_LIST
	b	.L1999
.L1998:
	bl	INSERT_FREE_LIST
.L1999:
	ldr	r4, .L2008+8
	mvn	r3, #0
	ldr	r2, .L2008+20
	strh	r3, [r4, r5]	@ movhi
	mov	r3, #0
	strh	r3, [r4, r2]	@ movhi
	sub	r2, r2, #12
	strh	r3, [r4, r2]	@ movhi
	bl	l2p_flush
	bl	FtlVpcTblFlush
	ldr	r3, [r4, #-1888]
	mov	r0, r4
	ldr	r1, .L2008+16
	cmp	r3, #0
	beq	.L2000
	ldr	r3, [r4, #-1828]
	cmp	r3, #29
	bhi	.L2000
	ldr	r3, .L2008+24
	ldrh	r2, [r4, r1]
	mvn	r1, #0
	ldrh	r3, [r4, r3]
	cmp	r2, r3
	movcc	r3, r3, asl #1
	ldrcc	r2, .L2008+28
	strcch	r3, [r4, r2]	@ movhi
	ldr	r3, .L2008+32
	ldr	r2, .L2008+8
	strh	r1, [r2, r3]	@ movhi
	b	.L2006
.L2000:
	ldr	r2, .L2008+24
	ldrh	r1, [r0, r1]
	ldr	r3, .L2008+8
	ldrh	r2, [r0, r2]
	add	r0, r2, r2, asl #1
	cmp	r1, r0, lsr #2
	ble	.L2006
	ldr	r1, .L2008+32
	mvn	r0, #0
	strh	r0, [r3, r1]	@ movhi
	ldr	r1, .L2008
	ldrb	r0, [r1, #928]	@ zero_extendqisi2
	ldr	r1, .L2008+28
	cmp	r0, #0
	subne	r2, r2, #2
	moveq	r2, #20
	strneh	r2, [r3, r1]	@ movhi
	streqh	r2, [r3, r1]	@ movhi
	beq	.L1993
	b	.L2006
.L2004:
	mov	r0, #1
	b	.L1993
.L2006:
	mov	r0, #0
.L1993:
	ldmfd	sp!, {r1, r2, r3, r4, r5, r6, r7, r8, sl, pc}
.L2009:
	.align	2
.L2008:
	.word	.LANCHOR0
	.word	-1952
	.word	.LANCHOR2
	.word	.LANCHOR1
	.word	-2052
	.word	-1144
	.word	-1776
	.word	-1196
	.word	-1760
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
	ldr	r3, .L2019
	ldrb	r3, [r3, #928]	@ zero_extendqisi2
	cmp	r3, #0
	ldr	r3, .L2019+4
	beq	.L2011
	ldr	r2, [r3, #-1180]
	tst	r2, #1
	beq	.L2011
	ldr	r2, .L2019+8
	ldrh	r2, [r3, r2]
	cmp	r2, #0
	bne	.L2016
.L2011:
	mov	r2, #0
	ldr	r0, [r3, #-984]
	ldr	r1, [r3, #-1180]
	mov	r3, r2
	bl	FlashProgPages
	mov	r4, #0
	ldr	r5, .L2019+4
	mov	r6, #36
	b	.L2013
.L2015:
	mul	r3, r6, r4
	ldr	r1, [r5, #-984]
	add	r2, r1, r3
	ldr	r1, [r1, r3]
	cmn	r1, #1
	bne	.L2014
	ldr	r2, .L2019+12
	mov	lr, #0
	ldr	ip, [r7, #-2068]
	ldrh	r0, [r7, r2]
	mov	r0, r0, asl #1
	strh	lr, [ip, r0]	@ movhi
	strh	r1, [r7, r2]	@ movhi
	ldr	r2, [r7, #-1612]
	add	r2, r2, #1
	str	r2, [r7, #-1612]
	ldr	r2, [r7, #-984]
	add	r3, r2, r3
	ldr	r0, [r3, #4]
	ubfx	r0, r0, #10, #16
	bl	FtlBbmMapBadBlock
	bl	FtlBbmTblFlush
	bl	FtlGcPageVarInit
	b	.L2018
.L2014:
	ldr	r3, [r2, #12]
	add	r4, r4, #1
	ldr	r1, [r2, #4]
	uxth	r4, r4
	ldr	r0, [r3, #12]
	ldr	r2, [r3, #8]
	bl	FtlGcUpdatePage
.L2013:
	ldr	r1, [r5, #-1180]
	ldr	r7, .L2019+4
	cmp	r4, r1
	bcc	.L2015
	ldr	r0, [r7, #-984]
	bl	FtlGcBufFree
	ldr	r3, .L2019+8
	mov	r0, #0
	str	r0, [r7, #-1180]
	ldrh	r3, [r7, r3]
	cmp	r3, r0
	ldmnefd	sp!, {r3, r4, r5, r6, r7, pc}
	mov	r0, #1
	bl	FtlGcFreeTempBlock
.L2018:
	mov	r0, #1
	ldmfd	sp!, {r3, r4, r5, r6, r7, pc}
.L2016:
	mov	r0, #0
	ldmfd	sp!, {r3, r4, r5, r6, r7, pc}
.L2020:
	.align	2
.L2019:
	.word	.LANCHOR0
	.word	.LANCHOR2
	.word	-1948
	.word	-1952
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
	ldr	r6, .L2023
	ldr	r4, .L2023+4
	ldrh	r1, [r6, r5]
	sub	r0, r4, #1952
	bl	FtlGcScanTempBlk
	ldr	r3, .L2023+8
	ldrh	r2, [r4, r3]
	ldrh	r3, [r6, r5]
	cmp	r2, r3
	ldmccfd	sp!, {r4, r5, r6, pc}
	ldr	r0, .L2023+12
	bl	FtlMapBlkWriteDumpData
	mov	r0, #0
	bl	FtlGcFreeTempBlock
	mov	r3, #0
	str	r3, [r4, #-1020]
	ldmfd	sp!, {r4, r5, r6, pc}
.L2024:
	.align	2
.L2023:
	.word	.LANCHOR0
	.word	.LANCHOR2
	.word	-1950
	.word	.LANCHOR2-1092
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
	ldr	r4, .L2026
	sub	r6, r4, #2048
	str	r5, [r4, #268]
	sub	r4, r4, #2000
	mov	r0, r6
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
.L2027:
	.align	2
.L2026:
	.word	.LANCHOR2
	.fnend
	.size	FtlPowerLostRecovery, .-FtlPowerLostRecovery
	.align	2
	.global	FtlSysBlkInit
	.type	FtlSysBlkInit, %function
FtlSysBlkInit:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	stmfd	sp!, {r4, r5, r6, lr}
	.save {r4, r5, r6, lr}
	movw	r3, #266
	ldr	r5, .L2039
	mov	r2, #0
	ldr	r4, .L2039+4
	ldr	r0, [r5, #3844]
	strh	r2, [r4, r3]	@ movhi
	uxth	r0, r0
	bl	FtlFreeSysBlkQueueInit
	bl	FtlScanSysBlk
	ldr	r3, .L2039+8
	ldrh	r2, [r4, r3]
	movw	r3, #65535
	cmp	r2, r3
	beq	.L2037
	bl	FtlLoadSysInfo
	subs	r6, r0, #0
	bne	.L2037
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
	ldrh	r2, [r5, r3]
	mov	r3, r6
	ldr	r1, [r4, #-1904]
	b	.L2030
.L2032:
	add	r0, r1, r6
	add	r6, r6, #12
	ldr	r0, [r0, #4]
	cmp	r0, #0
	blt	.L2031
	add	r3, r3, #1
.L2030:
	cmp	r3, r2
	blt	.L2032
.L2031:
	ldr	r4, .L2039+4
	cmp	r3, r2
	ldr	r1, .L2039+12
	ldrh	r0, [r4, r1]
	add	r0, r0, #1
	strh	r0, [r4, r1]	@ movhi
	blt	.L2033
	movw	r3, #266
	ldrh	r3, [r4, r3]
	cmp	r3, #0
	beq	.L2034
.L2033:
	ldr	r0, .L2039+16
	bl	FtlSuperblockPowerLostFix
	ldr	r0, .L2039+20
	bl	FtlSuperblockPowerLostFix
	ldr	r3, .L2039+24
	ldr	r1, [r4, #-2068]
	ldr	r2, .L2039+28
	ldrh	r3, [r4, r3]
	ldrh	r0, [r4, r2]
	mov	r3, r3, asl #1
	ldrh	ip, [r1, r3]
	rsb	r0, r0, ip
	strh	r0, [r1, r3]	@ movhi
	ldr	r0, .L2039
	movw	r1, #3908
	ldr	r3, .L2039+32
	ldr	lr, [r4, #-2068]
	ldrh	ip, [r0, r1]
	strh	ip, [r4, r3]	@ movhi
	mov	r3, #0
	strh	r3, [r4, r2]	@ movhi
	add	r2, r2, #44
	strb	r3, [r4, #-2042]
	ldrh	ip, [r4, r2]
	add	r2, r2, #4
	ldrh	r5, [r4, r2]
	mov	ip, ip, asl #1
	ldrh	r6, [lr, ip]
	rsb	r5, r5, r6
	strh	r5, [lr, ip]	@ movhi
	ldrh	r0, [r0, r1]
	ldr	r1, .L2039+36
	strh	r3, [r4, r2]	@ movhi
	strb	r3, [r4, #-1994]
	strh	r0, [r4, r1]	@ movhi
	ldr	r0, .L2039+40
	bl	FtlMapBlkWriteDumpData
	ldr	r0, .L2039+44
	bl	FtlMapBlkWriteDumpData
	ldr	r3, .L2039+48
	ldrh	r2, [r4, r3]
	add	r2, r2, #1
	strh	r2, [r4, r3]	@ movhi
	bl	l2p_flush
	bl	FtlVpcTblFlush
	bl	FtlVpcTblFlush
.L2034:
	ldr	r3, .L2039+24
	ldr	r4, .L2039+4
	ldrh	r0, [r4, r3]
	movw	r3, #65535
	cmp	r0, r3
	beq	.L2035
	ldr	r3, .L2039+28
	ldrh	r3, [r4, r3]
	cmp	r3, #0
	bne	.L2035
	ldr	r3, .L2039+52
	ldrh	r3, [r4, r3]
	cmp	r3, #0
	bne	.L2035
	bl	FtlGcRefreshBlock
	ldr	r3, .L2039+56
	ldrh	r0, [r4, r3]
	bl	FtlGcRefreshBlock
	sub	r0, r4, #2048
	bl	allocate_new_data_superblock
	sub	r0, r4, #2000
	bl	allocate_new_data_superblock
	add	r0, r4, #224
	bl	FtlMapBlkWriteDumpData
.L2035:
	ldr	r3, .L2039+12
	ldr	r2, .L2039+4
	ldrh	r0, [r2, r3]
	ands	r4, r0, #31
	bne	.L2038
	bl	FtlVpcCheckAndModify
	mov	r0, r4
	ldmfd	sp!, {r4, r5, r6, pc}
.L2037:
	mvn	r0, #0
	ldmfd	sp!, {r4, r5, r6, pc}
.L2038:
	mov	r0, #0
	ldmfd	sp!, {r4, r5, r6, pc}
.L2040:
	.align	2
.L2039:
	.word	.LANCHOR0
	.word	.LANCHOR2
	.word	-1772
	.word	-1796
	.word	.LANCHOR2-2048
	.word	.LANCHOR2-2000
	.word	-2048
	.word	-2044
	.word	-2046
	.word	-1998
	.word	.LANCHOR2-1092
	.word	.LANCHOR2+224
	.word	-1794
	.word	-1996
	.word	-2000
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
	ldr	r3, .L2044
	ldr	r5, .L2044+4
	ldrh	r2, [r5, r3]
	movw	r3, #65535
	cmp	r2, r3
	beq	.L2042
	ldr	r3, .L2044+8
	ldrh	r3, [r5, r3]
	cmp	r3, #0
	bne	.L2043
.L2042:
	bl	FtlCacheWriteBack
	mov	r0, #0
	bl	FtlGcFreeTempBlock
	ldr	r0, .L2044+12
	mov	r4, #0
	strb	r4, [r5, #-1944]
	bl	allocate_data_superblock
	ldr	r3, .L2044+16
	strh	r4, [r5, r3]	@ movhi
	add	r3, r3, #12
	strh	r4, [r5, r3]	@ movhi
	bl	l2p_flush
	mov	r0, r4
	bl	FtlEctTblFlush
	bl	FtlVpcTblFlush
.L2043:
	ldr	r0, .L2044+12
	ldmfd	sp!, {r3, r4, r5, lr}
	b	get_new_active_ppa
.L2045:
	.align	2
.L2044:
	.word	-1952
	.word	.LANCHOR2
	.word	-1948
	.word	.LANCHOR2-1952
	.word	-1156
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
	ldr	r5, .L2055
	mov	r7, r0
	mov	r4, r1
	ldr	r3, [r5, #3948]
	cmp	r2, r3
	mvnhi	r0, #0
	bhi	.L2047
	cmp	r1, #31
	movls	r0, #0
	bls	.L2047
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
	beq	.L2048
	rsb	r6, r7, r6
	add	r5, r0, #1
	cmp	r6, r4
	movcs	r6, r4
	uxth	r6, r6
	rsb	r4, r6, r4
.L2048:
	mvn	r3, #0
	ldr	r7, .L2055
	str	r3, [sp, #4]
	movw	r6, #3914
	b	.L2049
.L2051:
	mov	r0, r5
	mov	r1, sp
	mov	r2, #0
	bl	log2phys
	ldr	r3, [sp, #0]
	cmn	r3, #1
	beq	.L2050
	ldr	r3, .L2055+4
	add	r1, sp, #4
	mov	r0, r5
	ldr	r2, [r3, #412]
	add	r2, r2, #1
	str	r2, [r3, #412]
	ldr	r2, [r3, #-1880]
	add	r2, r2, #1
	str	r2, [r3, #-1880]
	mov	r2, #1
	bl	log2phys
	ldr	r0, [sp, #0]
	ubfx	r0, r0, #10, #16
	bl	P2V_block_in_plane
	bl	decrement_vpc_count
.L2050:
	ldrh	r3, [r7, r6]
	add	r5, r5, #1
	rsb	r4, r3, r4
.L2049:
	ldrh	r3, [r7, r6]
	cmp	r4, r3
	bcs	.L2051
	ldr	r3, .L2055+4
	mov	r4, #0
	ldr	r2, [r3, #412]
	cmp	r2, #32
	bls	.L2054
	str	r4, [r3, #412]
	bl	l2p_flush
	bl	FtlVpcTblFlush
.L2054:
	mov	r0, r4
.L2047:
	ldmfd	sp!, {r1, r2, r3, r4, r5, r6, r7, pc}
.L2056:
	.align	2
.L2055:
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
	bl	FtlCacheWriteBack
	bl	l2p_flush
	mov	r0, #1
	bl	FtlEctTblFlush
	bl	FtlVpcTblFlush
	mov	r0, #0
	ldmfd	sp!, {r3, pc}
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
	ldr	r3, .L2060
	ldr	r3, [r3, #2932]
	cmp	r3, #1
	bne	.L2059
	bl	FtlSysFlush
.L2059:
	mov	r0, #0
	ldmfd	sp!, {r3, pc}
.L2061:
	.align	2
.L2060:
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
	.global	FtlInit
	.type	FtlInit, %function
FtlInit:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
.L2064:
	stmfd	sp!, {r4, r5, r6, lr}
	.save {r4, r5, r6, lr}
	mvn	r3, #0
	ldr	r5, .L2068
	mov	r6, r0
	ldr	r4, .L2068+4
	ldr	r1, .L2068+8
	str	r3, [r5, #2932]
	mov	r3, #0
	ldr	r0, .L2068+12
	str	r3, [r4, #416]
	bl	printk
	mov	r0, r6
	bl	FtlConstantsInit
	bl	FtlMemInit
	bl	FtlVariablesInit
	ldr	r3, .L2068+16
	ldr	r0, [r3, #3844]
	uxth	r0, r0
	bl	FtlFreeSysBlkQueueInit
	bl	FtlLoadBbt
	cmp	r0, #0
	bne	.L2065
	bl	FtlSysBlkInit
	cmp	r0, #0
	bne	.L2065
	mov	r3, #1
	str	r3, [r5, #2932]
	ldr	r3, .L2068+20
	ldrh	r3, [r4, r3]
	cmp	r3, #15
	bhi	.L2065
	movw	r4, #8129
.L2066:
	mov	r0, #0
	mov	r1, #1
	bl	rk_ftl_garbage_collect
	subs	r4, r4, #1
	bne	.L2066
.L2065:
	mov	r0, #0
	ldmfd	sp!, {r4, r5, r6, pc}
.L2069:
	.align	2
.L2068:
	.word	.LANCHOR1
	.word	.LANCHOR2
	.word	.LC75
	.word	.LC74
	.word	.LANCHOR0
	.word	-2052
	.fnend
	.size	FtlInit, .-FtlInit
	.align	2
	.global	FtlWrite
	.type	FtlWrite, %function
FtlWrite:
	.fnstart
	@ args = 0, pretend = 0, frame = 96
	@ frame_needed = 0, uses_anonymous_args = 0
	cmp	r0, #16
	stmfd	sp!, {r4, r5, r6, r7, r8, r9, sl, fp, lr}
	.save {r4, r5, r6, r7, r8, r9, sl, fp, lr}
	mov	r4, r1
	.pad #100
	sub	sp, sp, #100
	mov	r5, r2
	mov	sl, r3
	bne	.L2071
	add	r0, r1, #256
	mov	r1, r2
	mov	r2, r3
	bl	FtlVendorPartWrite
	b	.L2072
.L2071:
	ldr	r9, .L2125
	add	r7, r2, r1
	ldr	r3, [r9, #3948]
	cmp	r7, r3
	mvnhi	r0, #0
	bhi	.L2072
	ldr	r6, .L2125+4
	mov	r3, #2048
	mov	r0, r1
	str	r3, [r6, #420]
	movw	r3, #3914
	ldrh	r8, [r9, r3]
	mov	r1, r8
	bl	__aeabi_uidiv
	mov	r1, r8
	str	r0, [sp, #12]
	sub	r0, r7, #1
	bl	__aeabi_uidiv
	ldr	r3, [sp, #12]
	cmp	r5, #8
	sub	r7, r6, #2048
	subls	r7, r6, #2000
	rsb	fp, r3, r0
	ldr	r3, [r6, #-1876]
	add	ip, fp, #1
	str	r0, [sp, #44]
	add	r3, ip, r3
	str	r3, [r6, #-1876]
	ldr	r3, [r6, #-1860]
	str	ip, [sp, #8]
	add	r3, r5, r3
	str	r3, [r6, #-1860]
	ldr	r3, [r9, #3960]
	cmp	r3, #0
	beq	.L2108
	ldr	r2, [r9, #3964]
	sub	r3, r3, #1
	mov	r7, #36
	ldr	ip, [sp, #12]
	mla	r7, r7, r3, r2
	ldr	r3, [r7, #16]
	cmp	ip, r3
	strne	r5, [sp, #28]
	bne	.L2075
	ldr	r3, [r6, #-1872]
	mov	r0, r4
	mov	r1, r8
	add	r3, r3, #1
	str	r3, [r6, #-1872]
	ldr	r3, [r6, #424]
	add	r3, r3, #1
	str	r3, [r6, #424]
	bl	__aeabi_uidivmod
	ldr	r0, [r7, #8]
	rsb	r9, r1, r8
	add	r0, r0, r1, asl #9
	cmp	r9, r5
	movcs	r9, r5
	mov	r1, sl
	mov	r3, r9, asl #9
	str	r3, [sp, #0]
	mov	r2, r3
	bl	memcpy
	cmp	fp, #0
	ldr	r3, [sp, #0]
	bne	.L2076
	ldr	r2, [r6, #424]
	cmp	r2, #2
	movle	r0, fp
	ble	.L2072
.L2076:
	add	sl, sl, r3
	add	r4, r4, r9
	ldr	r3, [sp, #12]
	rsb	ip, r9, r5
	str	fp, [sp, #8]
	add	r3, r3, #1
	str	ip, [sp, #28]
	str	r3, [sp, #12]
.L2075:
	ldr	r3, .L2125+4
	mov	r2, #0
	ldr	r7, [r3, #408]
	str	r2, [r3, #424]
	b	.L2074
.L2108:
	str	r5, [sp, #28]
.L2074:
	ldr	r0, [sp, #12]
	ldr	r1, [sp, #44]
	bl	FtlCacheMetchLpa
	cmp	r0, #0
	beq	.L2077
	bl	FtlCacheWriteBack
.L2077:
	cmp	r5, r8, asl #1
	ldr	r8, .L2125+4
	ldr	r5, .L2125
	mov	fp, #0
	movcc	ip, #0
	movcs	ip, #1
	ldr	r6, [sp, #12]
	str	ip, [sp, #16]
	str	r7, [r8, #408]
	mov	r9, r5
	str	fp, [sp, #40]
	str	sl, [sp, #24]
	b	.L2122
.L2103:
	ldrh	r3, [r7, #4]
	cmp	r3, #0
	bne	.L2079
	ldr	lr, .L2125+8
	cmp	r7, lr
	bne	.L2080
	ldr	r3, .L2125+12
	ldrh	sl, [r8, r3]
	cmp	sl, #0
	bne	.L2081
	sub	r0, r2, #2000
	bl	allocate_new_data_superblock
	ldr	r0, .L2125+16
	str	sl, [r0, #2940]
.L2081:
	ldr	r0, .L2125+8
	bl	allocate_new_data_superblock
	ldr	r1, .L2125+16
	ldr	r3, .L2125+20
	ldr	r2, [r1, #2940]
	cmp	r2, #0
	movne	r7, r3
	b	.L2082
.L2080:
	ldr	ip, .L2125+16
	str	r3, [ip, #2940]
	ldr	r3, .L2125+24
	ldrh	r3, [r8, r3]
	cmp	r3, #0
	subne	r7, r2, #2048
	bne	.L2082
	mov	r0, r7
	bl	allocate_new_data_superblock
.L2082:
	ldrh	r3, [r7, #4]
	cmp	r3, #0
	bne	.L2083
	mov	r0, r7
	bl	allocate_new_data_superblock
.L2083:
	str	r7, [r8, #408]
.L2079:
	ldr	r1, [r8, #-992]
	mov	ip, #0
	ldr	r2, [r5, #3960]
	mov	sl, r7
	ldrh	r3, [r7, #4]
	rsb	r2, r2, r1
	str	ip, [sp, #20]
	cmp	r3, r2
	movcc	r2, r3
	ldr	r3, [sp, #8]
	cmp	r2, r3
	movcc	r3, r2
	str	r3, [sp, #36]
	b	.L2084
.L2100:
	ldrh	r3, [sl, #4]
	cmp	r3, #0
	beq	.L2113
	ldr	ip, [sp, #44]
	rsb	r7, ip, r6
	ldr	ip, [sp, #16]
	rsbs	r3, r7, #0
	adc	r3, r3, r7
	tst	r3, ip
	beq	.L2086
	ldr	ip, [sp, #20]
	cmp	ip, #0
	beq	.L2086
	movw	lr, #3914
	ldr	ip, [sp, #28]
	ldrh	r2, [r5, lr]
	add	r1, ip, r4
	mls	r1, r2, r6, r1
	cmp	r1, r2
	bne	.L2114
.L2086:
	add	r1, sp, #56
	mov	r2, #0
	mov	r0, r6
	str	r3, [sp, #0]
	bl	log2phys
	mov	r0, sl
	bl	get_new_active_ppa
	ldr	lr, [r5, #3960]
	ldr	r1, [r5, #3964]
	mov	ip, #36
	mov	r2, #3920
	ldrh	r2, [r5, r2]
	mla	r1, ip, lr, r1
	str	r6, [r1, #16]
	str	r0, [r1, #4]
	mul	r0, lr, r2
	bic	r0, r0, #3
	str	r0, [sp, #48]
	ldr	r0, [r8, #-948]
	ldr	lr, [sp, #48]
	str	r0, [sp, #52]
	add	r7, r0, lr
	movw	r0, #3918
	ldr	lr, [r5, #3960]
	ldrh	r0, [r5, r0]
	str	r7, [r1, #12]
	mul	lr, lr, r0
	ldr	r0, [r8, #-968]
	str	lr, [sp, #32]
	bic	lr, lr, #3
	add	lr, r0, lr
	mov	r0, r7
	str	lr, [r1, #8]
	mov	r1, #0
	str	ip, [sp, #4]
	bl	ftl_memset
	ldr	r0, [sp, #12]
	ldr	r3, [sp, #0]
	rsb	lr, r0, r6
	ldr	ip, [sp, #4]
	rsbs	r0, lr, #0
	adc	r0, r0, lr
	str	r0, [sp, #32]
	orrs	r0, r0, r3
	beq	.L2087
	ldr	ip, [sp, #32]
	cmp	ip, #0
	beq	.L2088
	movw	lr, #3914
	mov	r0, r4
	ldrh	fp, [r5, lr]
	mov	r1, fp
	bl	__aeabi_uidivmod
	ldr	r3, [sp, #28]
	rsb	fp, r1, fp
	str	r1, [sp, #40]
	cmp	fp, r3
	movcs	fp, r3
	b	.L2089
.L2088:
	cmp	r3, #0
	beq	.L2089
	ldr	ip, [sp, #28]
	movw	lr, #3914
	ldrh	r3, [r5, lr]
	add	fp, ip, r4
	mls	fp, r3, r6, fp
	ldr	r3, [sp, #32]
	str	r3, [sp, #40]
	uxth	fp, fp
.L2089:
	movw	ip, #3914
	ldrh	r3, [r5, ip]
	cmp	fp, r3
	bne	.L2090
	ldr	r3, [sp, #32]
	ldr	r0, [r9, #3960]
	cmp	r3, #0
	ldr	r2, [r9, #3964]
	ldr	r3, [sp, #16]
	muleq	r1, r6, fp
	ldreq	ip, [sp, #24]
	ldrne	r1, [sp, #24]
	rsbeq	r1, r4, r1
	addeq	r1, ip, r1, asl #9
	cmp	r3, #0
	mov	r3, #36
	mla	r3, r3, r0, r2
	strne	r1, [r3, #8]
	bne	.L2093
	ldr	r0, [r3, #8]
	movw	r3, #3918
	ldrh	r2, [r9, r3]
	b	.L2123
.L2090:
	ldr	r2, [sp, #56]
	mov	r3, #36
	cmn	r2, #1
	beq	.L2094
	ldr	r1, [r5, #3960]
	add	r0, sp, #60
	str	r2, [sp, #64]
	ldr	r2, [r5, #3964]
	str	r6, [sp, #76]
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
	ldreq	r3, [r8, #-1636]
	addeq	r3, r3, #1
	streq	r3, [r8, #-1636]
	beq	.L2096
	ldr	r3, [r7, #8]
	cmp	r3, r6
	beq	.L2096
	ldr	r3, [r8, #-1636]
	mov	r2, r6
	ldr	r0, .L2125+28
	add	r3, r3, #1
	str	r3, [r8, #-1636]
	ldr	r1, [r7, #8]
	bl	printk
	b	.L2096
.L2094:
	ldr	r1, [r5, #3960]
	ldr	r2, [r5, #3964]
	mla	r3, r3, r1, r2
	mov	r1, #0
	ldr	r0, [r3, #8]
	movw	r3, #3918
	ldrh	r2, [r5, r3]
	bl	ftl_memset
.L2096:
	ldr	ip, [sp, #32]
	mov	r3, #36
	ldr	r1, [r5, #3960]
	ldr	r2, [r5, #3964]
	cmp	ip, #0
	mla	r3, r3, r1, r2
	ldrne	r0, [r3, #8]
	ldrne	r3, [sp, #40]
	ldrne	r1, [sp, #24]
	addne	r0, r0, r3, asl #9
	bne	.L2124
.L2097:
	movw	ip, #3914
	ldr	r0, [r3, #8]
	ldrh	r1, [r5, ip]
	ldr	r3, [sp, #24]
	mul	r1, r1, r6
	rsb	r1, r4, r1
	add	r1, r3, r1, asl #9
.L2124:
	mov	r2, fp, asl #9
	b	.L2123
.L2087:
	ldr	r3, [sp, #16]
	ldr	r2, [r5, #3960]
	cmp	r3, #0
	ldr	r3, [r5, #3964]
	mla	ip, ip, r2, r3
	beq	.L2098
	movw	lr, #3914
	ldr	r0, [sp, #24]
	ldrh	r3, [r5, lr]
	mul	r3, r3, r6
	rsb	r3, r4, r3
	add	r3, r0, r3, asl #9
	str	r3, [ip, #8]
	b	.L2093
.L2098:
	movw	r2, #3914
	ldr	r0, [ip, #8]
	ldrh	r1, [r5, r2]
	movw	ip, #3918
	ldr	r3, [sp, #24]
	ldrh	r2, [r5, ip]
	mul	r1, r1, r6
	rsb	r1, r4, r1
	add	r1, r3, r1, asl #9
.L2123:
	bl	memcpy
.L2093:
	ldr	r3, .L2125+32
	ldr	lr, [sp, #52]
	ldr	ip, [sp, #48]
	strh	r3, [lr, ip]	@ movhi
	ldr	r3, [r8, #-1848]
	str	r6, [r7, #8]
	add	r6, r6, #1
	str	r3, [r7, #4]
	add	r3, r3, #1
	cmn	r3, #1
	str	r3, [r8, #-1848]
	moveq	r3, #0
	streq	r3, [r8, #-1848]
	ldr	r3, [sp, #56]
	str	r3, [r7, #12]
	ldrh	r3, [sl, #0]
	strh	r3, [r7, #2]	@ movhi
	ldr	r3, [r5, #3960]
	add	r3, r3, #1
	str	r3, [r5, #3960]
	ldr	r3, [sp, #20]
	add	r3, r3, #1
	str	r3, [sp, #20]
.L2084:
	ldr	ip, [sp, #20]
	ldr	r3, [sp, #36]
	cmp	ip, r3
	bne	.L2100
	mov	r7, sl
	b	.L2085
.L2113:
	ldr	ip, [sp, #20]
	mov	r7, sl
	str	ip, [sp, #36]
	b	.L2085
.L2114:
	ldr	r3, [sp, #20]
	mov	r7, sl
	str	r3, [sp, #36]
.L2085:
	ldr	ip, [sp, #8]
	ldr	r3, [sp, #36]
	ldr	r2, [r5, #3960]
	rsb	ip, r3, ip
	ldr	r3, [r8, #-992]
	str	ip, [sp, #8]
	ldr	ip, [sp, #16]
	cmp	r2, r3
	orrcs	ip, ip, #1
	uxtb	r3, ip
	cmp	r3, #0
	bne	.L2101
	ldrh	r2, [r7, #4]
	cmp	r2, #0
	bne	.L2117
.L2101:
	bl	FtlCacheWriteBack
	ldr	ip, [sp, #8]
	mov	r3, #0
	str	r3, [r9, #3960]
	cmp	ip, #3
	ldr	r3, [sp, #16]
	movls	r3, #0
.L2117:
	str	r3, [sp, #16]
.L2122:
	ldr	r3, [sp, #8]
	ldr	r2, .L2125+4
	cmp	r3, #0
	bne	.L2103
	mov	r0, r3
	ldr	ip, [sp, #44]
	ldr	r3, [sp, #12]
	str	r2, [sp, #4]
	rsb	r1, r3, ip
	bl	rk_ftl_garbage_collect
	ldr	r3, .L2125+36
	ldr	r2, [sp, #4]
	ldrh	r3, [r2, r3]
	cmp	r3, #15
	ldrhi	r0, [sp, #8]
	bhi	.L2072
	mov	r5, r2
	movw	r6, #65535
	mov	r4, r2
.L2120:
	ldr	r3, .L2125+40
	ldrh	r3, [r5, r3]
	cmp	r3, r6
	bne	.L2104
	ldr	r3, .L2125+44
	ldrh	r3, [r5, r3]
	cmp	r3, r6
	bne	.L2104
	mov	r0, #0
	bl	List_get_gc_head_node
	uxth	r0, r0
	bl	FtlGcRefreshBlock
.L2104:
	ldr	r2, .L2125+48
	mov	r3, #128
	mov	r0, #1
	mov	r1, r0
	strh	r3, [r4, r2]	@ movhi
	sub	r2, r2, #2
	strh	r3, [r4, r2]	@ movhi
	bl	rk_ftl_garbage_collect
	mov	r0, #0
	mov	r1, #1
	bl	rk_ftl_garbage_collect
	ldr	r3, .L2125+36
	ldrh	r3, [r4, r3]
	cmp	r3, #8
	bls	.L2120
	mov	r0, #0
.L2072:
	add	sp, sp, #100
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, sl, fp, pc}
.L2126:
	.align	2
.L2125:
	.word	.LANCHOR0
	.word	.LANCHOR2
	.word	.LANCHOR2-2048
	.word	-1996
	.word	.LANCHOR1
	.word	.LANCHOR2-2000
	.word	-2044
	.word	.LC122
	.word	-3947
	.word	-2052
	.word	-1760
	.word	-1142
	.word	-1194
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
	bne	.L2129
	add	r0, r1, #256
	mov	r1, r2
	mov	r2, r3
	bl	FtlVendorPartRead
	str	r0, [sp, #16]
	b	.L2130
.L2129:
	ldr	r3, [sp, #28]
	add	r3, r3, r1
	str	r3, [sp, #20]
	ldr	r3, .L2159
	ldr	r1, [sp, #20]
	ldr	r2, [r3, #3948]
	cmp	r1, r2
	mvnhi	r3, #0
	strhi	r3, [sp, #16]
	bhi	.L2130
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
	ldr	r3, .L2159+4
	mov	fp, r0
	ldr	r0, [sp, #12]
	ldr	r2, [r3, #-1856]
	add	r2, r1, r2
	ldr	r1, [sp, #8]
	str	r2, [r3, #-1856]
	ldr	r2, [r3, #-1884]
	add	r2, r1, r2
	mov	r1, fp
	str	r2, [r3, #-1884]
	bl	FtlCacheMetchLpa
	cmp	r0, #0
	beq	.L2131
	bl	FtlCacheWriteBack
.L2131:
	mov	r9, #0
	ldr	r6, [sp, #12]
	str	r9, [sp, #32]
	mov	r5, r9
	str	r9, [sp, #16]
	ldr	r7, .L2159
	b	.L2154
.L2148:
	mov	r2, #0
	mov	r0, r6
	add	r1, sp, #60
	bl	log2phys
	ldr	r2, [sp, #60]
	cmn	r2, #1
	bne	.L2155
	b	.L2158
.L2136:
	mla	r0, r0, r6, sl
	cmp	r0, r4
	bcc	.L2135
	ldr	r2, [sp, #20]
	cmp	r0, r2
	bcs	.L2135
	rsb	r0, r4, r0
	mov	r1, #0
	mov	r2, #512
	str	r3, [sp, #4]
	add	r0, r8, r0, asl #9
	bl	ftl_memset
	ldr	r3, [sp, #4]
.L2135:
	add	sl, sl, #1
	b	.L2133
.L2158:
	mov	sl, #0
	movw	r3, #3914
.L2133:
	ldrh	r0, [r7, r3]
	cmp	sl, r0
	bcc	.L2136
	b	.L2137
.L2155:
	ldr	r3, .L2159+4
	mov	sl, #36
	ldr	r1, [r3, #-988]
	mla	sl, sl, r5, r1
	ldr	r1, [sp, #12]
	cmp	r6, r1
	str	r2, [sl, #4]
	movw	r2, #3914
	bne	.L2138
	ldr	r3, [r3, #-964]
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
	b	.L2139
.L2138:
	cmp	r6, fp
	ldrneh	r3, [r7, r2]
	mulne	r3, r3, r6
	bne	.L2156
	ldr	r3, [r3, #-960]
	ldrh	r2, [r7, r2]
	ldr	r1, [sp, #20]
	str	r3, [sl, #8]
	mul	r3, r2, r6
	rsb	r9, r3, r1
	cmp	r9, r2
	bne	.L2139
.L2156:
	rsb	r3, r4, r3
	add	r3, r8, r3, asl #9
	str	r3, [sl, #8]
.L2139:
	mov	r3, #3920
	ldr	r2, .L2159+4
	ldrh	r3, [r7, r3]
	str	r6, [sl, #16]
	ldr	r2, [r2, #-952]
	mul	r3, r3, r5
	add	r5, r5, #1
	bic	r3, r3, #3
	add	r3, r2, r3
	str	r3, [sl, #12]
.L2137:
	ldr	r3, [sp, #8]
	add	r6, r6, #1
	subs	r3, r3, #1
	str	r3, [sp, #8]
	beq	.L2141
	mov	r3, #3840
	ldrh	r3, [r7, r3]
	cmp	r5, r3, asl #3
	bne	.L2154
.L2141:
	cmp	r5, #0
	beq	.L2154
	ldr	sl, .L2159+4
	mov	r1, r5
	mov	r2, #0
	ldr	r0, [sl, #-988]
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
.L2147:
	ldr	r3, [sp, #24]
	mov	r5, #36
	ldr	r1, [sp, #12]
	mul	r5, r5, r3
	ldr	r3, [sl, #-988]
	add	r3, r3, r5
	ldr	r2, [r3, #16]
	cmp	r2, r1
	bne	.L2143
	ldr	r1, [r3, #8]
	ldr	r3, [sl, #-964]
	cmp	r1, r3
	bne	.L2144
	ldr	r3, [sp, #40]
	mov	r0, r8
	ldr	r2, [sp, #44]
	add	r1, r1, r3
	b	.L2157
.L2143:
	cmp	r2, fp
	bne	.L2144
	ldr	r1, [r3, #8]
	ldr	r3, [sl, #-960]
	cmp	r1, r3
	bne	.L2144
	movw	r2, #3914
	ldrh	r0, [r7, r2]
	ldr	r2, [sp, #48]
	mul	r0, r0, fp
	rsb	r0, r4, r0
	add	r0, r8, r0, asl #9
.L2157:
	bl	memcpy
.L2144:
	ldr	r3, [sl, #-988]
	add	r0, r3, r5
	ldr	r2, [r3, r5]
	cmn	r2, #1
	streq	r2, [sp, #16]
	ldreq	r1, [sl, #-1636]
	addeq	r1, r1, #1
	streq	r1, [sl, #-1636]
	ldr	r3, [r3, r5]
	cmp	r3, #256
	bne	.L2146
	ldr	r0, [r0, #4]
	ubfx	r0, r0, #10, #16
	bl	P2V_block_in_plane
	bl	FtlGcRefreshBlock
.L2146:
	ldr	r3, [sp, #24]
	add	r3, r3, #1
	str	r3, [sp, #24]
	cmp	r3, r6
	bne	.L2147
	ldr	r6, [sp, #52]
	mov	r5, #0
.L2154:
	ldr	r3, [sp, #8]
	cmp	r3, #0
	bne	.L2148
	ldr	r3, .L2159+8
	ldr	r2, .L2159+4
	ldrh	r3, [r2, r3]
	cmp	r3, #0
	beq	.L2130
	ldr	r0, [sp, #8]
	mov	r1, #1
	bl	rk_ftl_garbage_collect
.L2130:
	ldr	r0, [sp, #16]
	add	sp, sp, #68
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, sl, fp, pc}
.L2160:
	.align	2
.L2159:
	.word	.LANCHOR0
	.word	.LANCHOR2
	.word	-1138
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
	.type	rk_ftl_vendor_ops.constprop.20, %function
rk_ftl_vendor_ops.constprop.20:
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
	beq	.L2163
	bl	FtlWrite
	b	.L2165
.L2163:
	bl	FtlRead
.L2165:
	mov	r4, r0
	bl	rknand_device_unlock
	mov	r0, r4
	ldmfd	sp!, {r4, r5, r6, pc}
	.fnend
	.size	rk_ftl_vendor_ops.constprop.20, .-rk_ftl_vendor_ops.constprop.20
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
	ldr	r9, .L2175
	movw	r3, #3060
	ldr	r4, .L2175+4
	.pad #52
	sub	sp, sp, #52
	mov	r1, #0
	mvn	r7, #0
	ldrh	r3, [r9, r3]
	add	r0, r4, #208
	ldrh	r8, [r9, r2]
	mov	r2, #16
	mov	r6, #0
	mul	r8, r8, r3
	bl	ftl_memset
	ldr	r3, [r4, #204]
	str	r6, [sp, #20]
	mov	r4, r6
	str	r3, [sp, #24]
	uxth	r8, r8
	add	r3, r8, r7
	uxth	r3, r3
	str	r3, [sp, #4]
	b	.L2167
.L2173:
	ldr	r5, [sp, #4]
	sub	r3, r8, #12
	mul	fp, r8, r4
	ldr	sl, .L2175+4
	b	.L2168
.L2171:
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
	beq	.L2169
	ldr	r2, [sl, #204]
	ldrh	r1, [r2, #0]
	movw	r2, #61664
	cmp	r1, r2
	bne	.L2169
	ldr	r0, .L2175+8
	mov	r1, r4
	mov	r2, r5
	add	r6, r6, #1
	bl	printk
	ldr	r3, .L2175+4
	uxth	r6, r6
	add	r3, r3, r4, asl #1
	strh	r5, [r3, #208]	@ movhi
	b	.L2170
.L2169:
	sub	r5, r5, #1
	uxth	r5, r5
.L2168:
	cmp	r5, r3
	bgt	.L2171
.L2170:
	ldrb	r3, [r9, #3762]	@ zero_extendqisi2
	add	r4, r4, #1
	cmp	r3, r6
	uxtb	r4, r4
	moveq	r7, #0
.L2167:
	ldrb	r3, [r9, #3762]	@ zero_extendqisi2
	cmp	r3, r4
	bhi	.L2173
	mov	r0, r7
	add	sp, sp, #52
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, sl, fp, pc}
.L2176:
	.align	2
.L2175:
	.word	.LANCHOR0
	.word	.LANCHOR2
	.word	.LC123
	.fnend
	.size	FlashLoadFactorBbt, .-FlashLoadFactorBbt
	.align	2
	.global	FlashReadFacBbtData
	.type	FlashReadFacBbtData, %function
FlashReadFacBbtData:
	.fnstart
	@ args = 0, pretend = 0, frame = 40
	@ frame_needed = 0, uses_anonymous_args = 0
	ldr	r3, .L2187
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
	ldr	r7, .L2187+4
	mov	r4, r0
	mul	r8, r8, r2
	ldr	r3, [r7, #124]
	str	r3, [sp, #12]
	ldr	r3, [r7, #204]
	uxth	r8, r8
	sub	r5, r8, #1
	mul	sl, r8, r6
	str	r3, [sp, #16]
	uxth	r5, r5
	sub	r8, r8, #16
	b	.L2178
.L2184:
	mov	r1, #1
	add	r3, r5, sl
	add	r0, sp, #4
	mov	r2, r1
	mov	r3, r3, asl #10
	str	r3, [sp, #8]
	bl	FlashReadPages
	ldr	r3, [sp, #4]
	cmn	r3, #1
	beq	.L2179
	ldr	r3, [r7, #204]
	ldrh	r2, [r3, #0]
	movw	r3, #61664
	cmp	r2, r3
	bne	.L2179
	cmp	r4, #0
	moveq	r0, r4
	beq	.L2180
	cmp	r6, #0
	ldreq	r3, .L2187+4
	moveq	r0, #1
	beq	.L2181
	b	.L2182
.L2183:
	ldr	r2, [r3, #124]
	ubfx	r1, r6, #5, #16
	and	lr, r6, #31
	add	r6, r6, #1
	ldr	ip, [r2, r1, asl #2]
	uxth	r6, r6
	orr	ip, ip, r0, asl lr
	str	ip, [r2, r1, asl #2]
.L2181:
	ldr	r2, [r3, #132]
	cmp	r6, r2
	bcc	.L2183
.L2182:
	ldr	r3, .L2187+4
	mov	r2, r9
	mov	r0, r4
	ldr	r1, [r3, #124]
	bl	memcpy
	mov	r2, #4
	ldr	r0, .L2187+8
	mov	r1, r4
	mov	r3, r2
	bl	rknand_print_hex
	mov	r0, #0
	b	.L2180
.L2179:
	sub	r5, r5, #1
	uxth	r5, r5
.L2178:
	cmp	r5, r8
	bgt	.L2184
	mvn	r0, #0
.L2180:
	add	sp, sp, #40
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, sl, pc}
.L2188:
	.align	2
.L2187:
	.word	.LANCHOR0
	.word	.LANCHOR2
	.word	.LC124
	.fnend
	.size	FlashReadFacBbtData, .-FlashReadFacBbtData
	.align	2
	.global	FlashGetBadBlockList
	.type	FlashGetBadBlockList, %function
FlashGetBadBlockList:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	ldr	r3, .L2196
	stmfd	sp!, {r4, r5, r6, r7, r8, lr}
	.save {r4, r5, r6, r7, r8, lr}
	mov	r5, r0
	ldr	r3, [r3, #3624]
	ldr	r6, .L2196+4
	ldrb	r2, [r3, #13]	@ zero_extendqisi2
	ldrh	r4, [r3, #14]
	ldr	r0, [r6, #112]
	mul	r4, r4, r2
	uxth	r4, r4
	add	r2, r4, #7
	mov	r2, r2, lsr #3
	bl	FlashReadFacBbtData
	cmn	r0, #1
	beq	.L2195
	mov	r2, #0
	mov	r0, r4, lsr #4
	mov	r3, r2
	sub	r4, r4, #1
	mov	r1, #1
	b	.L2191
.L2193:
	ldr	ip, [r6, #112]
	mov	r8, r2, lsr #5
	and	r7, r2, #31
	ldr	ip, [ip, r8, asl #2]
	ands	ip, ip, r1, asl r7
	movne	ip, r3, asl #1
	addne	r3, r3, #1
	uxthne	r3, r3
	strneh	r2, [r5, ip]	@ movhi
	cmp	r3, r0
	bcs	.L2195
	add	r2, r2, #1
	uxth	r2, r2
.L2191:
	cmp	r2, r4
	blt	.L2193
	b	.L2190
.L2195:
	mov	r3, #0
.L2190:
	mov	r3, r3, asl #1
	mvn	r2, #0
	mov	r0, #0
	strh	r2, [r5, r3]	@ movhi
	ldmfd	sp!, {r4, r5, r6, r7, r8, pc}
.L2197:
	.align	2
.L2196:
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
	bl	FtlBbtMemInit
	mov	r5, #0
	ldr	r7, .L2215
	bl	FtlLoadFactoryBbt
	ldr	r6, .L2215+4
	add	sl, r7, #12
	ldr	r4, .L2215+8
	b	.L2199
.L2205:
	ldrh	r3, [sl], #2
	movw	r2, #65535
	ldr	r0, [r4, #-2080]
	ldr	r9, [r4, #-956]
	cmp	r3, r2
	str	r0, [r4, #164]
	str	r9, [r4, #168]
	beq	.L2200
	mov	r9, #3904
	mov	r1, #1
	ldrh	r8, [r6, r9]
	mov	r2, r1
	ldr	r0, .L2215+12
	mla	r8, r8, r5, r3
	mov	r3, r8, asl #10
	str	r3, [r4, #160]
	bl	FlashReadPages
	ldrh	r2, [r6, r9]
	ldr	r0, [r7, #28]
	add	r2, r2, #7
	ldr	r1, [r4, #164]
	mov	r2, r2, lsr #3
	bl	memcpy
	b	.L2201
.L2200:
	mov	r1, r5
	bl	FlashGetBadBlockList
	ldr	r0, [r4, #164]
	ldr	r1, [r7, #28]
	bl	FtlBbt2Bitmap
	mov	r3, #3904
	ldrh	fp, [r6, r3]
.L2203:
	sub	fp, fp, #1
	uxth	fp, fp
.L2214:
	mov	r8, #3904
	ldrh	r0, [r6, r8]
	mla	r0, r0, r5, fp
	uxth	r0, r0
	bl	FtlBbmIsBadBlock
	cmp	r0, #1
	beq	.L2203
	mov	r1, #0
	mov	r2, #16
	strh	fp, [sl, #-2]	@ movhi
	ldr	r0, [r4, #-956]
	bl	ftl_memset
	ldr	r3, .L2215+16
	strh	r3, [r9, #0]	@ movhi
	mov	r3, #0
	str	r3, [r9, #4]
	ldrh	r3, [sl, #-2]
	ldrh	r8, [r6, r8]
	strh	r3, [r9, #2]	@ movhi
	ldrh	r3, [sl, #-2]
	ldr	r1, [r7, #28]
	ldr	r0, [r4, #164]
	mla	r8, r8, r5, r3
	mov	r3, r8, asl #10
	str	r3, [r4, #160]
	ldr	r3, .L2215+20
	ldrh	r2, [r4, r3]
	mov	r2, r2, asl #2
	bl	memcpy
	mov	r1, #1
	mov	r2, r1
	ldr	r0, .L2215+12
	bl	FlashEraseBlocks
	mov	r1, #1
	mov	r3, r1
	ldr	r0, .L2215+12
	mov	r2, r1
	bl	FlashProgPages
	ldr	r3, [r4, #156]
	cmn	r3, #1
	bne	.L2201
	uxth	r0, r8
	bl	FtlBbmMapBadBlock
	b	.L2214
.L2201:
	uxth	r0, r8
	add	r5, r5, #1
	bl	FtlBbmMapBadBlock
	add	r7, r7, #4
.L2199:
	movw	r3, #3862
	ldrh	r3, [r6, r3]
	cmp	r5, r3
	bcc	.L2205
	mov	r4, #0
	ldr	r7, .L2215+4
	movw	r6, #3922
	b	.L2206
.L2207:
	mov	r0, r4
	add	r4, r4, #1
	bl	FtlBbmMapBadBlock
	uxth	r4, r4
.L2206:
	ldrh	r3, [r7, r6]
	ldr	r5, .L2215+4
	cmp	r3, r4
	bhi	.L2207
	movw	r7, #3988
	movw	r6, #3976
	ldrh	r4, [r5, r7]
	sub	r4, r4, #1
	uxth	r4, r4
	b	.L2208
.L2213:
	mov	r0, r4
	bl	FtlBbmIsBadBlock
	cmp	r0, #1
	beq	.L2209
	mov	r0, r4
	bl	FlashTestBlk
	cmp	r0, #0
	beq	.L2210
	mov	r0, r4
	bl	FtlBbmMapBadBlock
	b	.L2209
.L2210:
	ldrh	r2, [r5, r6]
	movw	r3, #65535
	cmp	r2, r3
	streqh	r4, [r5, r6]	@ movhi
.L2211:
	ldrne	r2, .L2215+4
	movwne	r3, #3980
	strneh	r4, [r2, r3]	@ movhi
	bne	.L2212
.L2209:
	sub	r4, r4, #1
	uxth	r4, r4
.L2208:
	ldrh	r3, [r5, r7]
	sub	r3, r3, #48
	cmp	r4, r3
	bgt	.L2213
.L2212:
	ldr	r4, .L2215+4
	movw	r7, #3976
	ldr	r3, .L2215+8
	movw	r6, #3980
	mov	r5, #0
	movw	r8, #3978
	str	r5, [r4, #3984]
	mov	r1, #1
	ldr	r0, [r3, #-2092]
	mov	r2, #2
	ldrh	r3, [r4, r7]
	strh	r5, [r4, r8]	@ movhi
	mov	r3, r3, asl #10
	str	r3, [r0, #4]
	ldrh	r3, [r4, r6]
	mov	r3, r3, asl #10
	str	r3, [r0, #40]
	bl	FlashEraseBlocks
	ldrh	r0, [r4, r7]
	bl	FtlBbmMapBadBlock
	ldrh	r0, [r4, r6]
	bl	FtlBbmMapBadBlock
	bl	FtlBbmTblFlush
	ldr	r3, [r4, #3984]
	ldrh	r2, [r4, r6]
	add	r3, r3, #1
	str	r3, [r4, #3984]
	ldrh	r3, [r4, r7]
	strh	r5, [r4, r8]	@ movhi
	strh	r2, [r4, r7]	@ movhi
	strh	r3, [r4, r6]	@ movhi
	bl	FtlBbmTblFlush
	mov	r0, r5
	ldmfd	sp!, {r3, r4, r5, r6, r7, r8, r9, sl, fp, pc}
.L2216:
	.align	2
.L2215:
	.word	.LANCHOR0+3976
	.word	.LANCHOR0
	.word	.LANCHOR2
	.word	.LANCHOR2+156
	.word	-3872
	.word	-1036
	.fnend
	.size	FtlMakeBbt, .-FtlMakeBbt
	.align	2
	.global	FtlLowFormat
	.type	FtlLowFormat, %function
FtlLowFormat:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	stmfd	sp!, {r3, r4, r5, r6, r7, r8, r9, sl, fp, lr}
	.save {r3, r4, r5, r6, r7, r8, r9, sl, fp, lr}
	mov	r2, #0
	ldr	r3, .L2238
	str	r2, [r3, #-1852]
	str	r2, [r3, #-1848]
	ldr	r3, .L2238+4
	ldr	r0, [r3, #3844]
	uxth	r0, r0
	bl	FtlFreeSysBlkQueueInit
	bl	FtlLoadBbt
	cmp	r0, #0
	beq	.L2218
	bl	FtlMakeBbt
.L2218:
	mov	r3, #0
	ldr	ip, .L2238+4
	movw	r0, #3914
	ldr	r2, .L2238
	ldr	r1, .L2238+8
	b	.L2219
.L2220:
	ldr	lr, [r2, #-964]
	mvn	r4, r3
	orr	r4, r3, r4, asl #16
	str	r4, [lr, r3, asl #2]
	ldr	lr, [r2, #-960]
	str	r1, [lr, r3, asl #2]
	add	r3, r3, #1
	uxth	r3, r3
.L2219:
	ldrh	lr, [ip, r0]
	ldr	r8, .L2238+4
	cmp	r3, lr, asl #7
	blt	.L2220
	movw	r3, #3848
	mov	r5, #0
	ldrh	r6, [r8, r3]
	movw	r7, #3850
	b	.L2221
.L2222:
	mov	r0, r6
	mov	r1, #1
	bl	FtlLowFormatEraseBlock
	add	r6, r6, #1
	uxth	r6, r6
	add	r5, r5, r0
	uxth	r5, r5
.L2221:
	ldrh	r3, [r8, r7]
	ldr	r4, .L2238+4
	cmp	r3, r6
	bhi	.L2222
	mov	r3, #3840
	ldrh	r1, [r4, r3]
	sub	r3, r5, #3
	cmp	r3, r1, asl #1
	bge	.L2223
.L2227:
	mov	r4, #0
	ldr	r8, .L2238+4
	mov	r6, r4
	movw	r7, #3848
	b	.L2224
.L2223:
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
	b	.L2225
.L2226:
	mov	r0, r5
	mov	r1, #1
	bl	FtlLowFormatEraseBlock
	add	r5, r5, #1
	uxth	r5, r5
.L2225:
	ldrh	r3, [r4, r6]
	cmp	r3, r5
	bhi	.L2226
	b	.L2227
.L2228:
	mov	r0, r6
	mov	r1, #0
	bl	FtlLowFormatEraseBlock
	add	r6, r6, #1
	uxth	r6, r6
	add	r4, r4, r0
	uxth	r4, r4
.L2224:
	ldrh	r3, [r8, r7]
	ldr	r5, .L2238+4
	cmp	r3, r6
	bhi	.L2228
	movw	r3, #3850
	ldr	fp, [r5, #3852]
	ldrh	r3, [r5, r3]
	ldr	r9, .L2238
	mov	r0, fp
	ldr	sl, .L2238+12
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
	ble	.L2229
	rsb	r0, r4, fp
	mov	r1, r6
	bl	__aeabi_uidiv
	str	r0, [r5, #3968]
	mov	r0, r0, lsr #5
	add	r0, r0, #24
	strh	r0, [r9, sl]	@ movhi
.L2229:
	ldr	r5, .L2238
	ldr	r3, [r5, #-1888]
	cmp	r3, #1
	bne	.L2230
	mov	r0, r4
	mov	r1, r6
	bl	__aeabi_uidiv
	ldr	sl, .L2238+12
	ldrh	r9, [r5, sl]
	uxtah	r0, r9, r0
	add	r9, r9, r0, lsr #2
	strh	r9, [r5, sl]	@ movhi
.L2230:
	ldr	r3, .L2238+4
	ldrb	r3, [r3, #928]	@ zero_extendqisi2
	cmp	r3, #0
	beq	.L2231
	mov	r0, r4
	mov	r1, r6
	bl	__aeabi_uidiv
	ldr	sl, .L2238
	ldr	r5, .L2238+12
	ldrh	r9, [sl, r5]
	uxtah	r0, r9, r0
	add	r9, r9, r0, lsr #2
	strh	r9, [sl, r5]	@ movhi
.L2231:
	ldr	r0, .L2238+4
	movw	r3, #3900
	ldrh	r3, [r0, r3]
	cmp	r3, #0
	beq	.L2232
	ldr	r1, .L2238
	ldr	r2, .L2238+12
	ldrh	ip, [r1, r2]
	add	ip, ip, r3, lsr #1
	strh	ip, [r1, r2]	@ movhi
	mul	ip, r6, r3
	cmp	ip, r4
	strgt	r7, [r0, #3968]
	addgt	r3, r3, #32
	addgt	r8, r8, r3
	strgth	r8, [r1, r2]	@ movhi
.L2232:
	ldr	r4, .L2238
	ldr	r5, .L2238+4
	ldr	r3, .L2238+12
	ldr	r2, [r5, #3968]
	ldrh	r3, [r4, r3]
	rsb	r3, r3, r2
	mul	r6, r6, r3
	movw	r3, #3908
	ldrh	r3, [r5, r3]
	str	r6, [r4, #192]
	mul	r6, r3, r6
	movw	r3, #3914
	ldrh	r3, [r5, r3]
	str	r6, [r5, #3968]
	mul	r6, r3, r6
	str	r6, [r5, #3948]
	bl	FtlBbmTblFlush
	movw	r3, #3850
	ldrh	r2, [r5, r3]
	mov	r1, #0
	ldr	r0, [r4, #-2068]
	ldr	r6, .L2238+16
	mov	r2, r2, asl #1
	bl	ftl_memset
	ldr	r2, .L2238+20
	mvn	r1, #0
	mov	r3, #0
	str	r3, [r4, #-1896]
	strh	r1, [r4, r2]	@ movhi
	ldr	r1, .L2238+24
	strb	r3, [r4, #-1754]
	strb	r3, [r4, #-1752]
	strh	r3, [r4, r1]	@ movhi
	sub	r1, r1, #288
	strb	r3, [r4, #-2042]
	strh	r3, [r4, r1]	@ movhi
	mov	r1, #255
	strh	r3, [r4, r6]	@ movhi
	mov	r3, #1
	strb	r3, [r4, #-2040]
	movw	r3, #3848
	ldrh	r2, [r5, r3]
	ldr	r0, [r4, #-1892]
	mov	r2, r2, lsr #3
	bl	ftl_memset
.L2233:
	ldr	r5, .L2238
	sub	r0, r5, #2048
	bl	make_superblock
	ldrb	r3, [r4, #-2041]	@ zero_extendqisi2
	cmp	r3, #0
	bne	.L2234
	ldrh	r3, [r4, r6]
	mvn	r1, #0
	ldr	r2, [r4, #-2068]
	mov	r3, r3, asl #1
	strh	r1, [r2, r3]	@ movhi
	ldrh	r3, [r4, r6]
	add	r3, r3, #1
	strh	r3, [r4, r6]	@ movhi
	b	.L2233
.L2234:
	ldr	r3, [r5, #-1852]
	ldr	r1, [r5, #-2068]
	ldr	r6, .L2238+28
	str	r3, [r5, #-2036]
	add	r3, r3, #1
	str	r3, [r5, #-1852]
	ldr	r3, .L2238+32
	ldrh	r0, [r5, r3]
	ldr	r3, .L2238+16
	ldrh	r2, [r5, r3]
	mov	r2, r2, asl #1
	strh	r0, [r1, r2]	@ movhi
	mov	r2, #0
	ldr	r0, .L2238+36
	ldrh	r3, [r5, r3]
	strb	r2, [r5, #-1994]
	add	r3, r3, #1
	strh	r2, [r5, r0]	@ movhi
	strh	r3, [r5, r6]	@ movhi
	mov	r3, #1
	strb	r3, [r5, #-1992]
.L2235:
	ldr	r4, .L2238
	sub	r0, r4, #2000
	bl	make_superblock
	ldrb	r3, [r5, #-1993]	@ zero_extendqisi2
	cmp	r3, #0
	bne	.L2236
	ldrh	r3, [r5, r6]
	mvn	r1, #0
	ldr	r2, [r5, #-2068]
	mov	r3, r3, asl #1
	strh	r1, [r2, r3]	@ movhi
	ldrh	r3, [r5, r6]
	add	r3, r3, #1
	strh	r3, [r5, r6]	@ movhi
	b	.L2235
.L2236:
	ldr	r3, [r4, #-1852]
	mvn	r5, #0
	ldr	r2, [r4, #-2068]
	str	r3, [r4, #-1988]
	add	r3, r3, #1
	str	r3, [r4, #-1852]
	ldr	r3, .L2238+40
	ldrh	r1, [r4, r3]
	ldr	r3, .L2238+28
	ldrh	r3, [r4, r3]
	mov	r3, r3, asl #1
	strh	r1, [r2, r3]	@ movhi
	ldr	r3, .L2238+44
	strh	r5, [r4, r3]	@ movhi
	bl	FtlFreeSysBlkQueueOut
	ldr	r3, .L2238+48
	mov	r2, #0
	strh	r0, [r4, r3]	@ movhi
	add	r3, r3, #2
	strh	r2, [r4, r3]	@ movhi
	add	r3, r3, #2
	ldr	r2, [r4, #192]
	strh	r5, [r4, r3]	@ movhi
	add	r3, r3, #2
	strh	r2, [r4, r3]	@ movhi
	ldr	r3, [r4, #-1852]
	str	r3, [r4, #-1764]
	add	r3, r3, #1
	str	r3, [r4, #-1852]
	bl	FtlVpcTblFlush
	bl	FtlSysBlkInit
	cmp	r0, #0
	mov	r0, #0
	ldreq	r3, .L2238+52
	moveq	r2, #1
	streq	r2, [r3, #2932]
	ldmfd	sp!, {r3, r4, r5, r6, r7, r8, r9, sl, fp, pc}
.L2239:
	.align	2
.L2238:
	.word	.LANCHOR2
	.word	.LANCHOR0
	.word	168778952
	.word	-1776
	.word	-2048
	.word	-1760
	.word	-1758
	.word	-2000
	.word	-2044
	.word	-1998
	.word	-1996
	.word	-1952
	.word	-1772
	.word	.LANCHOR1
	.fnend
	.size	FtlLowFormat, .-FtlLowFormat
	.align	2
	.global	HynixGetReadRetryDefault
	.type	HynixGetReadRetryDefault, %function
HynixGetReadRetryDefault:
	.fnstart
	@ args = 0, pretend = 0, frame = 64
	@ frame_needed = 0, uses_anonymous_args = 0
	ldr	r3, .L2325
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
	bne	.L2241
	mvn	r2, #88
	strb	r2, [r3, #4]
	ldr	r3, .L2325+4
	mvn	r2, #8
	strb	r2, [r3, #2961]
	b	.L2297
.L2241:
	cmp	r4, #3
	bne	.L2243
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
	b	.L2320
.L2243:
	cmp	r4, #4
	bne	.L2244
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
.L2320:
	mov	r6, #8
	strb	r2, [r3, #11]
	mov	r5, r6
	b	.L2242
.L2244:
	cmp	r4, #5
	bne	.L2245
	mov	r2, #56
	mov	r6, #8
	strb	r2, [r3, #4]
	mov	r2, #57
	strb	r2, [r3, #5]
	mov	r2, #58
	strb	r2, [r3, #6]
	mov	r2, #59
	strb	r2, [r3, #7]
	b	.L2319
.L2245:
	cmp	r4, #6
	bne	.L2246
	mov	r2, #14
	mov	r6, #12
	strb	r2, [r3, #4]
	mov	r2, #15
	strb	r2, [r3, #5]
	mov	r2, #16
	strb	r2, [r3, #6]
	mov	r2, #17
	strb	r2, [r3, #7]
	b	.L2319
.L2246:
	cmp	r4, #7
	bne	.L2297
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
	b	.L2242
.L2297:
	mov	r6, #7
.L2319:
	mov	r5, #4
.L2242:
	sub	r3, r4, #1
	cmp	r3, #1
	bhi	.L2315
	b	.L2323
.L2253:
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
.L2249:
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
	bcc	.L2249
	mov	r2, #0
	b	.L2250
.L2251:
	ldrb	r7, [r0, r1, asl #2]	@ zero_extendqisi2
	ldrb	r5, [r4, r2]	@ zero_extendqisi2
	add	r5, r7, r5
	strb	r5, [ip, r1, asl #3]
	add	r1, r1, #1
	cmp	r1, #7
	bne	.L2251
	add	r2, r2, #1
	cmp	r2, #4
	beq	.L2252
.L2250:
	mov	r1, #1
	add	ip, r4, r2
	add	r0, r2, r9
	b	.L2251
.L2252:
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
	b	.L2247
.L2323:
	ldr	r9, .L2325+8
	mov	r3, r6
	mov	r8, #0
	mov	r6, r5
.L2247:
	ldr	fp, .L2325
	ldrb	r2, [fp, #3762]	@ zero_extendqisi2
	cmp	r2, r8
	bhi	.L2253
	mov	r5, r6
	mov	r6, r3
	b	.L2254
.L2315:
	sub	r3, r4, #3
	cmp	r3, #4
	bhi	.L2254
	mul	r3, r6, r5
	mov	sl, #0
	mov	r7, sl
	str	r6, [sp, #48]
	mov	r2, r3, asr #2
	mov	r3, r3, asl #4
	str	r2, [sp, #24]
	str	r3, [sp, #52]
	b	.L2255
.L2296:
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
	bne	.L2258
	mov	r3, #255
	str	r3, [r8, #4]
	mov	r3, #64
	str	r3, [r9, fp, asl #8]
	mov	r3, #204
	b	.L2321
.L2258:
	sub	r3, r4, #5
	cmp	r3, #1
	ldrls	r3, .L2325
	ldrlsb	r3, [r3, #4]	@ zero_extendqisi2
	strls	r3, [r8, #4]
	movls	r3, #82
	bls	.L2322
.L2260:
	mov	r3, #174
	str	r3, [r8, #4]
	mov	r3, #176
	str	r7, [r9, fp, asl #8]
.L2321:
	str	r3, [r8, #4]
	mov	r3, #77
.L2322:
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
	bls	.L2264
	cmp	r4, #7
	moveq	r1, #32
	movne	r1, #2
.L2264:
	ldr	r3, .L2325+12
	ldr	r2, [r3, #124]
	mov	r3, #0
.L2265:
	ldr	r0, [r8, #0]
	strb	r0, [r2, r3]
	add	r3, r3, #1
	uxtb	r0, r3
	cmp	r0, r1
	bcc	.L2265
	cmp	r4, #7
	bne	.L2266
	mov	r3, #0
.L2268:
	ldrb	r1, [r2, #0]	@ zero_extendqisi2
	cmp	r1, #12
	beq	.L2267
	ldrb	r1, [r2, #1]	@ zero_extendqisi2
	cmp	r1, #10
	beq	.L2267
	add	r3, r3, #1
	add	r2, r2, #4
	uxtb	r3, r3
	cmp	r3, #8
	bne	.L2268
	b	.L2269
.L2267:
	cmp	r3, #6
	bls	.L2270
.L2269:
	ldr	r0, .L2325+16
	mov	r1, #0
	bl	printk
.L2271:
	b	.L2271
.L2266:
	cmp	r4, #6
	bne	.L2270
	mov	r1, #8
.L2272:
	ldrb	r3, [r2], #1	@ zero_extendqisi2
	cmp	r3, #12
	beq	.L2270
	ldrb	r3, [r2, #7]	@ zero_extendqisi2
	cmp	r3, #4
	beq	.L2270
	sub	r1, r1, #1
	uxtb	r1, r1
	cmp	r1, #0
	bne	.L2272
	b	.L2324
.L2274:
	b	.L2274
.L2270:
	ldr	r3, .L2325+12
	mov	r2, #0
	ldr	ip, [r3, #124]
	str	ip, [sp, #40]
.L2275:
	ldr	r1, [r8, #0]
	ldr	lr, [sp, #40]
	ldr	ip, [sp, #52]
	strb	r1, [lr, r2]
	add	r2, r2, #1
	cmp	r2, ip
	blt	.L2275
	ldr	r2, [sp, #24]
	ldr	r1, [r3, #124]
	mov	r3, #8
	mov	ip, r2, asl #2
	mov	lr, r2, asl #3
	add	r1, r1, ip
	str	ip, [sp, #32]
.L2277:
	mov	r2, #0
	mov	r0, r2
.L2276:
	ldr	ip, [r1, r2]
	add	r0, r0, #1
	mvn	ip, ip
	str	ip, [r1, r2]
	ldr	ip, [sp, #24]
	add	r2, r2, #4
	cmp	r0, ip
	blt	.L2276
	subs	r3, r3, #1
	add	r1, r1, lr
	bne	.L2277
	str	r3, [sp, #16]
	str	sl, [sp, #60]
	b	.L2278
.L2282:
	mov	lr, #1
	mov	r0, #0
	mov	ip, lr, asl r2
	mov	lr, r0
	mov	sl, #16
	str	r3, [sp, #4]
	str	sl, [sp, #20]
.L2280:
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
	bne	.L2280
	cmp	lr, #8
	add	r2, r2, #1
	orrhi	r1, r1, ip
	cmp	r2, #32
	ldr	r3, [sp, #4]
	bne	.L2282
	ldr	ip, [sp, #16]
	ldr	r2, [sp, #24]
	add	ip, ip, #1
	ldr	sl, [sp, #44]
	cmp	ip, r2
	str	ip, [sp, #16]
	str	r1, [sl, r3]
	add	r3, r3, #4
	bge	.L2283
.L2278:
	ldr	sl, .L2325+12
	mov	r1, #0
	mov	r2, r1
	ldr	sl, [sl, #124]
	add	ip, sl, r3
	str	ip, [sp, #56]
	str	sl, [sp, #44]
	b	.L2282
.L2283:
	ldr	r3, .L2325+12
	ldr	sl, [sp, #60]
	ldr	r1, [r3, #124]
	mov	r3, #0
	mov	r2, r3
.L2285:
	ldr	r0, [r1, r3]
	add	r3, r3, #4
	cmp	r0, #0
	addeq	r2, r2, #1
	cmp	r3, #32
	bne	.L2285
	cmp	r2, #7
	ble	.L2286
	ldr	r0, .L2325+20
	mov	r2, #1
	mov	r3, #1024
	bl	rknand_print_hex
	ldr	r0, .L2325+16
	mov	r1, #0
	bl	printk
.L2287:
	b	.L2287
.L2286:
	cmp	r4, #6
	moveq	r0, #4
	streq	r0, [sp, #16]
	beq	.L2288
	cmp	r4, #7
	moveq	lr, #10
	movne	lr, #8
	str	lr, [sp, #16]
.L2288:
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
	b	.L2291
.L2292:
	ldrb	lr, [ip], #1	@ zero_extendqisi2
	strb	lr, [r1, r3]
	add	r3, r3, #1
	uxtb	lr, r3
	cmp	lr, r5
	bcc	.L2292
	ldr	r3, [sp, #20]
	add	r2, r2, #1
	ldr	ip, [sp, #16]
	add	r0, r0, r3
	ldr	r3, [sp, #48]
	add	r1, r1, ip
	cmp	r2, r3
	bge	.L2293
.L2291:
	mov	ip, r0
	mov	r3, #0
	b	.L2292
.L2293:
	mov	r3, #255
	mov	r0, r6
	str	r3, [r8, #8]
	bl	NandcWaitFlashReady
	ldr	ip, [sp, #36]
	cmp	ip, #1
	movhi	r3, #56
	strhi	r3, [r8, #8]
	bhi	.L2295
	mov	r3, #54
	str	r3, [r8, #8]
	ldr	r3, .L2325
	mov	r0, sl
	mvn	r1, #0
	ldrb	r3, [r3, #4]	@ zero_extendqisi2
	str	r3, [r8, #4]
	mov	r3, #22
	str	r7, [r9, fp, asl #8]
	str	r3, [r8, #8]
	bl	FlashReadCmd
.L2295:
	mov	r0, r6
	add	sl, sl, #1
	bl	NandcWaitFlashReady
	uxtb	sl, sl
.L2255:
	ldr	r1, .L2325
	ldrb	r3, [r1, #3762]	@ zero_extendqisi2
	cmp	r3, sl
	bhi	.L2296
	ldr	r6, [sp, #48]
.L2254:
	ldr	r3, .L2325
	strb	r5, [r3, #1]
	strb	r6, [r3, #2]
	add	sp, sp, #68
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, sl, fp, pc}
.L2324:
	ldr	r0, .L2325+16
	bl	printk
	b	.L2274
.L2326:
	.align	2
.L2325:
	.word	.LANCHOR0
	.word	.LANCHOR1
	.word	.LANCHOR1+2944
	.word	.LANCHOR2
	.word	.LC125
	.word	.LC126
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
	bhi	.L2329
	ldmfd	sp!, {r3, lr}
	b	HynixGetReadRetryDefault
.L2329:
	cmp	r3, #49
	bne	.L2330
	ldr	r0, .L2331
	mov	r2, #64
	ldr	r1, .L2331+4
	strb	r3, [r0, #0]
	mov	r3, #4
	strb	r3, [r0, #1]
	mov	r3, #15
	strb	r3, [r0, #2]
	add	r0, r0, #4
	bl	memcpy
	ldmfd	sp!, {r3, pc}
.L2330:
	ldmfd	sp!, {r3, lr}
	b	FlashGetReadRetryDefault.part.19
.L2332:
	.align	2
.L2331:
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
	stmfd	sp!, {r3, r4, r5, r6, r7, lr}
	.save {r3, r4, r5, r6, r7, lr}
	mov	r7, r0
	mov	r0, #32768
	ldr	r5, .L2384
	bl	ftl_malloc
	ldr	r6, .L2384+4
	mov	r4, #0
	str	r0, [r5, #124]
	mov	r0, #32768
	bl	ftl_malloc
	str	r0, [r5, #112]
	mov	r0, #4096
	bl	ftl_malloc
	str	r0, [r5, #204]
	mov	r0, #32768
	bl	ftl_malloc
	str	r0, [r5, #196]
	mov	r0, #4096
	bl	ftl_malloc
	mov	r3, #50
	str	r4, [r5, #132]
	strb	r3, [r5, #140]
	strb	r3, [r6, #853]
	mov	r3, #128
	str	r4, [r5, #144]
	str	r3, [r6, #856]
	mov	r3, #60
	strb	r4, [r6, #3773]
	strb	r3, [r5, #128]
	strb	r4, [r6, #852]
	str	r0, [r5, #200]
	mov	r0, r7
	bl	NandcInit
	ldr	r5, .L2384+8
	mov	r7, #44
.L2337:
	uxtb	r0, r4
	mov	r1, r5
	bl	FlashReadIDRaw
	cmp	r4, #0
	bne	.L2334
	ldrb	r3, [r6, #2980]	@ zero_extendqisi2
	sub	r3, r3, #1
	uxtb	r3, r3
	cmp	r3, #253
	bhi	.L2376
	ldrb	r3, [r6, #2981]	@ zero_extendqisi2
	cmp	r3, #255
	beq	.L2377
.L2334:
	ldrb	r3, [r5, #0]	@ zero_extendqisi2
	add	r4, r4, #1
	cmp	r3, #181
	streqb	r7, [r5, #0]
	cmp	r4, #4
	add	r5, r5, #8
	bne	.L2337
	ldr	r4, .L2384+4
	ldrb	r3, [r4, #2980]	@ zero_extendqisi2
	cmp	r3, #173
	beq	.L2338
	ldr	r0, [r4, #3776]
	bl	NandcSetDdrMode
.L2338:
	mov	r2, #852
	ldr	r0, .L2384+4
	mov	r1, #0
	bl	ftl_memset
	ldr	r3, .L2384+12
	str	r3, [r4, #3624]
	mov	r3, #0
	strb	r3, [r4, #860]
	ldrb	r3, [r4, #2981]	@ zero_extendqisi2
	sub	r2, r3, #218
	rsbs	lr, r2, #0
	adc	lr, lr, r2
	cmp	r3, #218
	cmpne	r3, #241
	beq	.L2339
	cmp	r3, #220
	bne	.L2340
.L2339:
	ldr	r2, .L2384+4
	mov	r1, #1
	ldr	r0, .L2384
	ldrb	ip, [r2, #2980]	@ zero_extendqisi2
	strb	r1, [r2, #852]
	mov	r1, #16
	strb	r1, [r2, #853]
	cmp	ip, #152
	strb	r1, [r0, #128]
	ldr	r1, .L2384+16
	strb	ip, [r1, #2973]
	bne	.L2341
	ldrb	r2, [r2, #2984]	@ zero_extendqisi2
	sxtb	r2, r2
	cmp	r2, #0
	movge	r2, #24
	strgeb	r2, [r0, #128]
.L2341:
	cmp	lr, #0
	beq	.L2342
	movw	r3, #2986
	mov	r2, #2048
	strh	r2, [r1, r3]	@ movhi
	mvn	r3, #37
	strb	r3, [r1, #2974]
	b	.L2343
.L2342:
	cmp	r3, #220
	bne	.L2343
	ldr	r3, .L2384+16
	movw	r2, #2986
	mov	r1, #4096
	strh	r1, [r3, r2]	@ movhi
	mvn	r2, #35
	strb	r2, [r3, #2974]
.L2343:
	ldr	r1, .L2384+20
	mov	r2, #32
	ldr	r0, .L2384+24
	bl	memcpy
	ldr	r0, .L2384+12
	mov	r2, #32
	add	r1, r0, #144
	bl	memcpy
.L2340:
	ldr	r4, .L2384+4
	ldrb	r3, [r4, #852]	@ zero_extendqisi2
	cmp	r3, #0
	bne	.L2344
	bl	FlashLoadPhyInfoInRam
	cmp	r0, #0
	bne	.L2345
	ldr	r3, [r4, #3624]
	ldrh	r0, [r3, #16]
	ubfx	r0, r0, #8, #3
	strb	r0, [r4, #3772]
	tst	r0, #1
	bne	.L2345
	mov	r3, #1
	strb	r3, [r4, #3773]
	bl	FlashSetInterfaceMode
	ldrb	r0, [r4, #3772]	@ zero_extendqisi2
	bl	NandcSetMode
.L2345:
	ldr	r4, .L2384+4
	ldr	r3, [r4, #3624]
	ldrb	r3, [r3, #26]	@ zero_extendqisi2
	strb	r3, [r4, #928]
	bl	FlashLoadPhyInfo
	cmp	r0, #0
	beq	.L2344
	ldr	r3, [r4, #3776]
	cmp	r3, #0
	beq	.L2347
	mov	r0, #1
	bl	FlashSetInterfaceMode
	mov	r0, #1
	b	.L2382
.L2347:
	ldrb	r0, [r4, #3772]	@ zero_extendqisi2
	bl	FlashSetInterfaceMode
	ldrb	r0, [r4, #3772]	@ zero_extendqisi2
.L2382:
	bl	NandcSetMode
	bl	FlashLoadPhyInfo
	cmp	r0, #0
	beq	.L2344
	ldr	r4, .L2384+4
	mov	r0, #1
	bl	FlashSetInterfaceMode
	mov	r0, #1
	bl	NandcSetMode
	ldr	r0, .L2384+28
	ldr	r3, [r4, #3624]
	ldrh	r1, [r3, #14]
	bl	printk
	bl	FlashLoadPhyInfoInRam
	cmn	r0, #1
	ldmeqfd	sp!, {r3, r4, r5, r6, r7, pc}
	bl	FlashDieInfoInit
	ldr	r3, [r4, #3624]
	ldrb	r0, [r3, #19]	@ zero_extendqisi2
	bl	FlashGetReadRetryDefault
	ldr	r1, .L2384+32
	ldr	r0, .L2384
	ldr	r3, [r4, #3624]
	ldrh	r1, [r0, r1]
	ldrb	r2, [r3, #9]	@ zero_extendqisi2
	add	r1, r1, #4080
	add	r1, r1, #15
	cmp	r2, r1, lsr #12
	blt	.L2350
	ldrh	r1, [r3, #14]
	add	r1, r1, #255
	cmp	r2, r1, lsr #8
	bge	.L2351
.L2350:
	ldrh	r2, [r3, #14]
	bic	r2, r2, #255
	strh	r2, [r3, #14]	@ movhi
.L2351:
	ldr	r3, .L2384+4
	ldrb	r3, [r3, #3772]	@ zero_extendqisi2
	tst	r3, #6
	beq	.L2352
	bl	FlashSavePhyInfo
	ldr	r3, .L2384
	mov	r0, #0
	ldr	r1, [r3, #136]
	bl	FlashDdrParaScan
.L2352:
	bl	FlashSavePhyInfo
.L2344:
	ldr	r4, .L2384+4
	ldr	r5, .L2384
	ldr	r3, [r4, #3624]
	ldrb	r2, [r3, #26]	@ zero_extendqisi2
	ldrh	r0, [r3, #10]
	ldrb	r6, [r3, #18]	@ zero_extendqisi2
	strb	r2, [r4, #928]
	ldrh	r2, [r3, #16]
	ubfx	r1, r2, #7, #1
	strb	r1, [r4, #860]
	ubfx	r1, r2, #3, #1
	strb	r1, [r5, #152]
	ubfx	r1, r2, #4, #1
	ubfx	r2, r2, #8, #3
	strb	r1, [r4, #3833]
	strb	r2, [r4, #3772]
	mov	r2, #0
	ldrb	r1, [r3, #12]	@ zero_extendqisi2
	str	r2, [r5, #148]
	bl	__aeabi_idiv
	mov	r1, r0
	mov	r0, r6
	bl	BuildFlashLsbPageTable
	bl	FlashDieInfoInit
	ldr	r3, [r4, #3624]
	ldrh	r2, [r3, #16]
	tst	r2, #64
	beq	.L2353
	ldrb	r0, [r3, #19]	@ zero_extendqisi2
	ldrb	r3, [r4, #1]	@ zero_extendqisi2
	strb	r0, [r4, #3761]
	strb	r3, [r4, #3760]
	ldrb	r3, [r4, #2]	@ zero_extendqisi2
	strb	r3, [r5, #116]
	sub	r3, r0, #1
	uxtb	r3, r3
	cmp	r3, #6
	bhi	.L2354
	ldr	r3, .L2384+36
	str	r3, [r5, #148]
	sub	r3, r0, #5
	uxtb	r3, r3
	cmp	r3, #1
	movls	r3, #1
	strls	r3, [r4, #3824]
	bls	.L2356
	cmp	r0, #7
	addeq	r4, r4, #28
	beq	.L2357
.L2356:
	ldr	r4, .L2384+40
.L2357:
	mov	r3, #0
	mov	r2, r3
.L2359:
	ldrsb	r1, [r4, r2]
	add	r2, r2, #1
	cmp	r1, #0
	addeq	r3, r3, #1
	cmp	r2, #32
	bne	.L2359
	cmp	r3, #27
	bls	.L2353
	bl	FlashGetReadRetryDefault
	bl	FlashSavePhyInfo
	b	.L2353
.L2354:
	sub	r3, r0, #17
	uxtb	r3, r3
	cmp	r3, #2
	bhi	.L2360
	ldr	r3, .L2384+44
	cmp	r0, #19
	str	r3, [r5, #148]
	mov	r3, #7
	strb	r3, [r5, #116]
	moveq	r3, #15
	streqb	r3, [r5, #116]
	b	.L2353
.L2360:
	cmp	r0, #33
	cmpne	r0, #65
	beq	.L2361
	cmp	r0, #66
	bne	.L2362
.L2361:
	ldr	r3, .L2384
	mov	r1, #4
	ldr	r2, .L2384+48
	str	r2, [r3, #148]
	ldr	r2, .L2384+4
	strb	r1, [r2, #3760]
	mov	r2, #7
	strb	r2, [r3, #116]
	b	.L2353
.L2362:
	cmp	r0, #67
	cmpne	r0, #34
	beq	.L2363
	cmp	r0, #35
	beq	.L2363
	cmp	r0, #68
	bne	.L2364
.L2363:
	ldr	r3, .L2384
	cmp	r0, #35
	cmpne	r0, #68
	ldr	r2, .L2384+48
	sub	r0, r0, #67
	uxtb	r0, r0
	str	r2, [r3, #148]
	mov	r2, #7
	strb	r2, [r3, #116]
	moveq	r2, #17
	streqb	r2, [r3, #116]
	cmp	r0, #1
	ldr	r3, .L2384+4
	movls	r2, #4
	movhi	r2, #5
	strb	r2, [r3, #3760]
	b	.L2353
.L2364:
	cmp	r0, #49
	ldreq	r3, .L2384+52
	streq	r3, [r5, #148]
.L2353:
	ldr	r2, .L2384+4
	ldr	r3, .L2384+56
	ldr	r1, [r2, #3784]
	cmp	r1, r3
	bne	.L2367
	ldrb	r3, [r2, #928]	@ zero_extendqisi2
	cmp	r3, #0
	ldrne	r3, [r2, #3624]
	movne	r1, #0
	strneb	r1, [r3, #18]
.L2367:
	ldrb	r2, [r2, #2980]	@ zero_extendqisi2
	ldr	r3, .L2384+4
	cmp	r2, #44
	bne	.L2368
	ldrb	r2, [r3, #3773]	@ zero_extendqisi2
	cmp	r2, #0
	beq	.L2368
	ldr	r1, [r3, #3784]
	ldr	r2, .L2384+56
	cmp	r1, r2
	bne	.L2369
	ldrb	r3, [r3, #928]	@ zero_extendqisi2
	cmp	r3, #0
	bne	.L2368
.L2369:
	ldr	r3, .L2384+4
	mov	r2, #0
	mov	r0, #1
	strb	r2, [r3, #3773]
	bl	FlashSetInterfaceMode
	mov	r0, #1
	bl	NandcSetMode
.L2368:
	mov	r0, #0
	bl	flash_enter_slc_mode
	ldr	r2, .L2384+4
	ldrb	r3, [r2, #3772]	@ zero_extendqisi2
	tst	r3, #6
	beq	.L2370
	ldrb	r2, [r2, #3773]	@ zero_extendqisi2
	cmp	r2, #0
	bne	.L2371
	tst	r3, #1
	bne	.L2370
.L2371:
	ldr	r3, .L2384
	mov	r0, #0
	ldr	r1, [r3, #136]
	bl	FlashDdrParaScan
.L2370:
	ldr	r4, .L2384+4
	mov	r0, #0
	bl	flash_exit_slc_mode
	ldr	r3, [r4, #3624]
	ldrb	r0, [r3, #20]	@ zero_extendqisi2
	bl	FlashBchSel
	add	r0, r4, #932
	bl	FlashReadIdbDataRaw
	mov	r3, r4
	cmp	r0, #0
	movne	r3, #16
	strneb	r3, [r4, #853]
	bne	.L2374
	ldr	r2, .L2384
	ldrb	r1, [r3, #853]	@ zero_extendqisi2
	ldrb	r2, [r2, #140]	@ zero_extendqisi2
	cmp	r1, r2
	strhib	r2, [r3, #853]
	ldrb	r3, [r3, #853]	@ zero_extendqisi2
	cmp	r3, #15
	ldrls	r3, .L2384+4
	movls	r2, #16
	strlsb	r2, [r3, #853]
.L2374:
	ldr	r4, .L2384+4
	movw	r6, #3062
	ldr	r0, .L2384+60
	bl	FlashTimingCfg
	mov	r3, #3056
	ldr	r5, [r4, #3624]
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
	bne	.L2375
	mov	lr, #16
	mov	r7, r7, lsr #1
	mov	ip, ip, asl #1
	mov	r1, r1, asl #1
	strb	lr, [r4, #853]
	strh	r1, [r4, r2]	@ movhi
	mov	r2, #8
	strh	r7, [r4, r6]	@ movhi
	strh	ip, [r4, r0]	@ movhi
	strh	r2, [r4, r3]	@ movhi
.L2375:
	ldrb	r0, [r5, #20]	@ zero_extendqisi2
	bl	FlashBchSel
	bl	FlashSuspend
	mov	r0, #0
	ldmfd	sp!, {r3, r4, r5, r6, r7, pc}
.L2376:
	mvn	r0, #1
	ldmfd	sp!, {r3, r4, r5, r6, r7, pc}
.L2377:
	mvn	r0, #1
	ldmfd	sp!, {r3, r4, r5, r6, r7, pc}
.L2385:
	.align	2
.L2384:
	.word	.LANCHOR2
	.word	.LANCHOR0
	.word	.LANCHOR0+2980
	.word	.LANCHOR1+2828
	.word	.LANCHOR1
	.word	.LANCHOR1+2732
	.word	.LANCHOR0+896
	.word	.LC127
	.word	-918
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
	ldr	r4, .L2390
	mov	r5, #0
	add	r1, r4, #432
	str	r5, [r4, #108]
	str	r5, [r4, #432]
	str	r0, [r4, #428]
	add	r0, r4, #108
	bl	rknand_get_reg_addr
	ldr	r3, [r4, #108]
	cmp	r3, r5
	mvneq	r4, #0
	beq	.L2387
	bl	rk_nandc_irq_init
	ldr	r0, [r4, #428]
	mov	r1, r5
	mov	r2, r5
	mov	r3, #2048
	bl	FlashSramLoadStore
	bl	rknand_flash_cs_init
	ldr	r0, [r4, #108]
	bl	FlashInit
	subs	r4, r0, #0
	bne	.L2388
	ldr	r0, .L2390+4
	bl	FtlInit
.L2388:
	ldr	r0, .L2390+8
	mov	r1, r4
	bl	printk
.L2387:
	mov	r0, r4
	ldmfd	sp!, {r3, r4, r5, pc}
.L2391:
	.align	2
.L2390:
	.word	.LANCHOR2
	.word	.LANCHOR0+3048
	.word	.LC128
	.fnend
	.size	rk_ftl_init, .-rk_ftl_init
	.align	2
	.global	FtlReInitForSDUpdata
	.type	FtlReInitForSDUpdata, %function
FtlReInitForSDUpdata:
	.fnstart
	@ args = 0, pretend = 0, frame = 16
	@ frame_needed = 0, uses_anonymous_args = 0
	ldr	r3, .L2417
	stmfd	sp!, {r4, r5, lr}
	.save {r4, r5, lr}
	.pad #20
	sub	sp, sp, #20
	ldrb	r4, [r3, #928]	@ zero_extendqisi2
	cmp	r4, #0
	movne	r0, #0
	bne	.L2393
	ldr	r5, .L2417+4
	ldr	r0, [r5, #108]
	bl	FlashInit
	cmp	r0, #0
	movne	r0, r4
	bne	.L2393
	bl	FlashLoadFactorBbt
	cmp	r0, #0
	beq	.L2394
	bl	FlashMakeFactorBbt
.L2394:
	ldr	r0, [r5, #112]
	bl	FlashReadIdbDataRaw
	cmp	r0, #0
	beq	.L2395
	mov	r1, #0
	mov	r2, #16
	mov	r0, sp
	bl	FlashReadFacBbtData
	ldr	r1, [sp, #0]
	mov	r3, #0
	mov	r0, #1
	mov	r2, r3
.L2397:
	ands	ip, r1, r0, asl r2
	add	r2, r2, #1
	addne	r3, r3, #1
	cmp	r2, #16
	bne	.L2397
	cmp	r3, #6
	ldrls	r3, .L2417
	bls	.L2413
	mov	r2, #0
	mov	r0, #1
.L2398:
	ands	ip, r1, r0, asl r2
	add	r2, r2, #1
	addne	r3, r3, #1
	cmp	r2, #24
	bne	.L2398
	cmp	r3, #17
	ldr	r3, .L2417
	movhi	r2, #36
.L2413:
	strb	r2, [r3, #853]
	movw	r2, #3074
	ldr	r3, .L2417
	ldrb	r1, [r3, #853]	@ zero_extendqisi2
	strh	r1, [r3, r2]	@ movhi
.L2395:
	ldr	r1, .L2417+8
	mov	r4, #1
	ldr	r0, .L2417+12
	bl	printk
	ldr	r0, .L2417+16
	bl	FtlConstantsInit
	bl	FtlVariablesInit
	ldr	r3, .L2417
	ldr	r0, [r3, #3844]
	uxth	r0, r0
	bl	FtlFreeSysBlkQueueInit
	b	.L2402
.L2404:
	add	r4, r4, #1
.L2402:
	bl	FtlLoadBbt
	cmp	r0, #0
	bne	.L2415
.L2403:
	bl	FtlSysBlkInit
	cmp	r0, #0
	beq	.L2405
.L2415:
	bl	FtlLowFormat
	cmp	r4, #3
	bls	.L2404
	b	.L2416
.L2405:
	ldr	r3, .L2417+20
	mov	r2, #1
	str	r2, [r3, #2932]
	b	.L2393
.L2416:
	mvn	r0, #0
.L2393:
	add	sp, sp, #20
	ldmfd	sp!, {r4, r5, pc}
.L2418:
	.align	2
.L2417:
	.word	.LANCHOR0
	.word	.LANCHOR2
	.word	.LC75
	.word	.LC74
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
	ldr	r3, .L2421
	ldr	r2, [r3, #3784]
	ldr	r3, .L2421+4
	cmp	r2, r3
	bxne	lr
	b	flash_enter_slc_mode
.L2422:
	.align	2
.L2421:
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
	ldr	r3, .L2425
	ldr	r2, [r3, #3784]
	ldr	r3, .L2425+4
	cmp	r2, r3
	bxne	lr
	b	flash_exit_slc_mode
.L2426:
	.align	2
.L2425:
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
	ldr	r4, .L2432
	mov	r6, r0
	str	r2, [sp, #20]
	mov	sl, r1
	ldr	r0, .L2432+4
	mov	r1, r6
	ldr	r2, [r4, #3624]
	ldr	r3, [r4, #856]
	ldr	r8, .L2432+8
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
	b	.L2428
.L2430:
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
	beq	.L2429
	ldr	r0, [r4, #3784]
	ldr	ip, .L2432+12
	cmp	r0, ip
	moveq	r3, r1
.L2429:
	ldr	ip, [sp, #12]
	ldrb	r0, [r8, #128]	@ zero_extendqisi2
	add	r2, r2, ip
	ldrb	ip, [r4, #3832]	@ zero_extendqisi2
	ldr	fp, [r8, #112]
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
	ldr	r1, [r8, #112]
	add	r0, ip, r5, asl #9
	bl	memcpy
	ldr	ip, [sp, #16]
	mov	r2, #0
	add	r5, ip, r5
	uxth	r5, r5
.L2428:
	cmp	r5, sl
	bcc	.L2430
	mov	r1, r6
	mov	r2, sl
	mov	r3, #0
	ldr	r0, .L2432+16
	bl	printk
	mov	r0, #0
	add	sp, sp, #28
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, sl, fp, pc}
.L2433:
	.align	2
.L2432:
	.word	.LANCHOR0
	.word	.LC129
	.word	.LANCHOR2
	.word	1446522928
	.word	.LC130
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
	ldr	r4, .L2441
	mov	r5, r0
	str	r2, [sp, #20]
	mov	r8, r1
	ldr	r0, .L2441+4
	mov	r1, r5
	ldr	r2, [r4, #3624]
	mov	r7, r5
	ldr	r3, [r4, #856]
	ldr	fp, .L2441+8
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
	b	.L2435
.L2439:
	add	r3, r6, r9
	ubfx	r3, r3, #2, #16
	cmp	r3, #0
	beq	.L2436
	add	r1, r3, #1
	ldrb	r0, [r4, #928]	@ zero_extendqisi2
	add	r2, r4, r1, asl #1
	cmp	r0, #0
	add	r2, r2, #3072
	add	r2, r2, #4
	ldrh	r2, [r2, #0]
	beq	.L2437
	ldr	r0, [r4, #3784]
	cmp	r0, fp
	uxtheq	r2, r1
.L2437:
	sub	r2, r2, #1
	mov	r2, r2, asl #2
	str	r2, [sp, #24]
	mov	r2, #0
	str	r2, [sp, #28]
.L2436:
	add	r2, r4, r3, asl #1
	add	r2, r2, #3072
	ldrh	r5, [r2, #4]
	ldrb	r2, [r4, #928]	@ zero_extendqisi2
	cmp	r2, #0
	beq	.L2438
	ldr	r2, [r4, #3784]
	cmp	r2, fp
	moveq	r5, r3
.L2438:
	ldr	r2, [sp, #16]
	ldr	r1, [sp, #20]
	ldrb	ip, [r4, #3832]	@ zero_extendqisi2
	mla	r3, sl, r5, r2
	add	r2, r1, r6, asl #9
	ldr	r1, .L2441+12
	add	r6, r6, #4
	uxth	r6, r6
	ldrb	r0, [r1, #128]	@ zero_extendqisi2
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
.L2435:
	cmp	r6, r8
	bcc	.L2439
	mov	r1, r7
	mov	r2, r8
	mov	r3, #0
	ldr	r0, .L2441+16
	bl	printk
	mov	r5, r7
	mov	r0, #0
	add	sp, sp, #92
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, sl, fp, pc}
.L2442:
	.align	2
.L2441:
	.word	.LANCHOR0
	.word	.LC131
	.word	1446522928
	.word	.LANCHOR2
	.word	.LC132
	.fnend
	.size	IDBlockWriteData, .-IDBlockWriteData
	.align	2
	.global	write_idblock
	.type	write_idblock, %function
write_idblock:
	.fnstart
	@ args = 0, pretend = 0, frame = 8
	@ frame_needed = 0, uses_anonymous_args = 0
	ldr	r3, .L2456
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
	beq	.L2444
	add	sl, r6, #508
	add	sl, sl, #3
	mov	sl, sl, lsr #9
	cmp	sl, #255
	bhi	.L2445
	add	r0, r4, sl, asl #9
	mov	r1, r4
	rsb	r2, sl, #256
	bl	memcpy
.L2445:
	ldr	r8, .L2456
	mov	r3, #5
	ldr	r0, .L2456+4
	mov	r1, r7
	mov	r2, #4
	add	sl, sl, #128
	bl	rknand_print_hex
	ldrb	r2, [r8, #853]	@ zero_extendqisi2
	ldr	r1, [r4, #512]
	cmp	sl, #256
	movcs	sl, #256
	ldr	r0, .L2456+8
	mov	fp, #0
	bl	printk
	ldrb	r3, [r8, #853]	@ zero_extendqisi2
	ldr	r2, [r4, #512]
	mov	r1, sl
	ldr	r0, .L2456+12
	mov	r9, fp
	cmp	r2, r3
	mov	r8, r4
	mov	r2, r6
	strhi	r3, [r4, #512]
	mov	r6, r7
	bl	printk
	mov	r1, sl, asl #7
	str	r1, [sp, #12]
.L2451:
	ldr	r1, .L2456
	movw	r2, #3074
	ldr	r3, [r6], #4
	ldrh	r2, [r1, r2]
	cmp	r3, r2
	bcs	.L2447
	ldr	r2, .L2456+16
	ldr	r2, [r2, #132]
	cmp	r3, r2
	bcc	.L2447
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
.L2450:
	ldr	r1, [r5, r3]
	add	r3, r3, #4
	add	r2, r8, r3
	ldr	r2, [r2, #-4]
	cmp	r1, r2
	beq	.L2448
	bic	r4, r7, #255
	stmia	sp, {r1, r2}
	mov	r3, r7
	mov	r1, r9
	ldr	r2, [r6, #-4]
	mov	r4, r4, asl #2
	ldr	r0, .L2456+20
	bl	printk
	ldr	r0, .L2456+24
	add	r1, r8, r4
	mov	r2, #4
	mov	r3, #256
	bl	rknand_print_hex
	mov	r3, #256
	mov	r2, #4
	ldr	r0, .L2456+28
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
	ldr	r0, .L2456+32
	bl	printk
	ldr	r2, [sp, #12]
	cmp	r7, r2
	bcc	.L2447
	b	.L2449
.L2448:
	ldr	r1, [sp, #12]
	add	r7, r7, #1
	cmp	r7, r1
	bne	.L2450
.L2449:
	add	fp, fp, #1
.L2447:
	add	r9, r9, #1
	cmp	r9, #5
	bne	.L2451
	mov	r0, r5
	bl	ftl_free
	cmp	fp, #0
	mvneq	r0, #0
	movne	r0, #0
.L2444:
	add	sp, sp, #20
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, sl, fp, pc}
.L2457:
	.align	2
.L2456:
	.word	.LANCHOR0
	.word	.LC133
	.word	.LC134
	.word	.LC135
	.word	.LANCHOR2
	.word	.LC136
	.word	.LC137
	.word	.LC138
	.word	.LC139
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
	ldr	ip, .L2461
	b	.L2459
.L2460:
	ldrb	r4, [r3, r2]	@ zero_extendqisi2
	add	r2, r2, #1
	eor	r4, r4, r0, lsr #24
	add	r4, ip, r4, asl #2
	ldr	r4, [r4, #3004]
	eor	r0, r4, r0, asl #8
.L2459:
	cmp	r2, r1
	bne	.L2460
	ldmfd	sp!, {r4, pc}
.L2462:
	.align	2
.L2461:
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
	ldr	r3, .L2579
	stmfd	sp!, {r4, r5, r6, lr}
	.save {r4, r5, r6, lr}
	cmp	r1, r3
	.pad #528
	sub	sp, sp, #528
	mov	r5, r1
	mov	r4, r2
	beq	.L2472
	bhi	.L2481
	ldr	r3, .L2579+4
	cmp	r1, r3
	beq	.L2469
	bhi	.L2482
	sub	r3, r3, #125
	cmp	r1, r3
	beq	.L2466
	bhi	.L2483
	sub	r3, r3, #237
	cmp	r1, r3
	bne	.L2524
	b	.L2575
.L2483:
	ldr	r3, .L2579+8
	cmp	r1, r3
	beq	.L2467
	add	r3, r3, #1
	cmp	r1, r3
	bne	.L2524
	b	.L2576
.L2482:
	ldr	r3, .L2579+12
	cmp	r1, r3
	beq	.L2471
	bhi	.L2484
	ldr	r3, .L2579+16
	cmp	r1, r3
	bne	.L2524
	b	.L2577
.L2484:
	ldr	r3, .L2579+20
	cmp	r1, r3
	beq	.L2471
	add	r3, r3, #10
	cmp	r1, r3
	bne	.L2524
	b	.L2471
.L2481:
	ldr	r3, .L2579+24
	cmp	r1, r3
	beq	.L2477
	bhi	.L2485
	sub	r3, r3, #78
	cmp	r1, r3
	beq	.L2474
	bcc	.L2473
	add	r3, r3, #21
	cmp	r1, r3
	beq	.L2475
	add	r3, r3, #56
	cmp	r1, r3
	bne	.L2524
	b	.L2578
.L2485:
	ldr	r3, .L2579+28
	cmp	r1, r3
	beq	.L2480
	bhi	.L2486
	sub	r3, r3, #956
	sub	r3, r3, #1
	cmp	r1, r3
	beq	.L2478
	add	r3, r3, #956
	cmp	r1, r3
	bne	.L2524
	b	.L2479
.L2486:
	ldr	r3, .L2579+32
	cmp	r1, r3
	beq	.L2479
	add	r3, r3, #1
	cmp	r1, r3
	bne	.L2524
	b	.L2480
.L2473:
	ldr	r0, .L2579+36
	bl	printk
	mov	r0, #4096
	bl	ftl_malloc
	subs	r5, r0, #0
	beq	.L2532
	mov	r1, r4
	mov	r2, #512
	bl	rk_copy_from_user
	cmp	r0, #0
	bne	.L2574
.L2487:
	ldr	r0, .L2579+40
	ldmia	r5, {r1, r2}
	bl	printk
	ldr	r3, [r5, #4]
	cmp	r3, #8
	str	r3, [sp, #4]
	bhi	.L2495
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
	beq	.L2560
	ldr	r0, .L2579+44
.L2573:
	bl	printk
	b	.L2495
.L2474:
	ldr	r0, .L2579+48
	bl	printk
	mov	r0, #4096
	bl	ftl_malloc
	subs	r5, r0, #0
	beq	.L2532
	mov	r1, r4
	mov	r2, #4096
	bl	rk_copy_from_user
	cmp	r0, #0
	bne	.L2574
.L2491:
	ldr	r4, .L2579+52
	ldr	r0, .L2579+56
	ldmia	r5, {r1, r2}
	bl	printk
	ldr	r3, [r4, #436]
	cmp	r3, #0
	bne	.L2492
	mov	r0, #260096
	bl	ftl_malloc
	cmp	r0, #0
	str	r0, [r4, #436]
	beq	.L2495
.L2492:
	ldr	r2, [r5, #4]
	movw	r3, #4088
	cmp	r2, r3
	bhi	.L2495
	ldr	r3, [r5, #0]
	cmp	r3, #251904
	bhi	.L2495
	ldr	r1, .L2579+52
	ldr	r0, [r1, #436]
	add	r1, r5, #8
	add	r0, r0, r3
	bl	memcpy
.L2560:
	mov	r0, r5
	bl	ftl_free
.L2561:
	mov	r4, #0
	b	.L2490
.L2578:
	ldr	r0, .L2579+60
	bl	printk
	mov	r0, #4096
	bl	ftl_malloc
	subs	r5, r0, #0
	beq	.L2532
	mov	r1, r4
	mov	r2, #28
	bl	rk_copy_from_user
	cmp	r0, #0
	beq	.L2494
.L2574:
	ldr	r0, .L2579+64
	b	.L2573
.L2494:
	ldmia	r5, {r1, r2}
	ldr	r0, .L2579+68
	bl	printk
	ldr	r1, [r5, #0]
	cmp	r1, #256000
	bhi	.L2495
	ldr	r4, .L2579+52
	ldr	r0, [r4, #436]
	cmp	r0, #0
	beq	.L2495
.L2496:
	bl	CRC_32
	ldr	r3, [r5, #4]
	cmp	r3, r0
	beq	.L2497
	mov	r0, r5
	bl	ftl_free
	b	.L2538
.L2497:
	bl	rknand_device_lock
	ldr	r1, [r4, #436]
	add	r2, r5, #8
	ldr	r0, [r5, #0]
	bl	write_idblock
	mov	r6, #0
	bl	rknand_device_unlock
	ldr	r0, [r4, #436]
	bl	ftl_free
	str	r6, [r4, #436]
	mov	r0, r5
	mov	r4, r6
	bl	ftl_free
	b	.L2490
.L2495:
	mov	r0, r5
	b	.L2564
.L2475:
	ldr	r0, .L2579+72
	bl	printk
	mov	r0, #4096
	bl	ftl_malloc
	subs	r5, r0, #0
	beq	.L2532
	bl	ReadFlashInfo
	mov	r0, r4
	mov	r1, r5
	mov	r2, #11
	b	.L2571
.L2472:
	ldr	r0, .L2579+76
	bl	printk
	bl	rknand_device_lock
	bl	FtlReInitForSDUpdata
	mov	r6, r0
	bl	rknand_device_unlock
	cmp	r6, #0
	bne	.L2555
	bl	nand_blk_add_whole_disk
	mov	r0, #4096
	bl	ftl_malloc
	subs	r5, r0, #0
	beq	.L2532
	bl	rknand_device_lock
	mov	r1, r6
	mov	r2, #64
	mov	r0, r5
	bl	FlashReadFacBbtData
	bl	rknand_device_unlock
	ldr	r0, .L2579+80
	mov	r1, r5
	mov	r2, #4
	mov	r3, #8
	bl	rknand_print_hex
	mov	r0, r4
	mov	r1, r5
	mov	r2, #64
	b	.L2571
.L2477:
	ldr	r0, .L2579+84
	bl	printk
	ldr	r3, .L2579+52
	add	r1, sp, #528
	mov	r0, r4
	mov	r2, #4
	ldr	r3, [r3, #440]
	ldr	r3, [r3, #20]
	str	r3, [r1, #-524]!
	b	.L2568
.L2478:
	ldr	r0, .L2579+88
	bl	printk
	mov	r0, #4096
	bl	ftl_malloc
	subs	r5, r0, #0
	beq	.L2532
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
.L2571:
	bl	rk_copy_to_user
	subs	r4, r0, #0
	mov	r0, r5
	beq	.L2500
.L2564:
	bl	ftl_free
	b	.L2555
.L2500:
	bl	ftl_free
	b	.L2490
.L2466:
	ldr	r0, .L2579+92
	bl	printk
	add	r0, sp, #8
	mov	r1, r4
	mov	r2, #520
	bl	rk_copy_from_user
	subs	r5, r0, #0
	bne	.L2565
.L2501:
	ldr	r2, [sp, #8]
	ldr	r3, .L2579+96
	cmp	r2, r3
	bne	.L2557
	ldr	r3, [sp, #12]
	cmp	r3, #512
	bhi	.L2557
	ldr	r6, .L2579+52
	mov	r2, #512
	add	r0, sp, #8
	ldr	r1, [r6, #440]
	bl	memcpy
	ldr	r2, [r6, #444]
	ldr	r3, .L2579+100
	cmp	r2, r3
	beq	.L2502
	add	r0, sp, #72
	mov	r1, #128
	str	r5, [sp, #16]
	str	r5, [sp, #20]
	bl	__memzero
.L2502:
	add	r0, sp, #264
	mov	r1, #256
	mov	r3, #0
	str	r3, [sp, #24]
	bl	__memzero
	b	.L2567
.L2467:
	ldr	r0, .L2579+104
	bl	printk
	add	r0, sp, #8
	mov	r1, r4
	mov	r2, #520
	bl	rk_copy_from_user
	cmp	r0, #0
	bne	.L2565
.L2503:
	ldr	r2, [sp, #8]
	ldr	r3, .L2579+96
	cmp	r2, r3
	bne	.L2557
	ldr	r3, [sp, #12]
	cmp	r3, #512
	bhi	.L2557
	ldr	r4, .L2579+52
	ldr	r3, .L2579+100
	ldr	r2, [r4, #444]
	cmp	r2, r3
	bne	.L2538
	ldr	r3, [sp, #20]
	sub	r2, r3, #1
	cmp	r2, #127
	mvnhi	r4, #2
	bhi	.L2464
	ldr	r0, [r4, #440]
	add	r1, sp, #72
	str	r3, [r0, #12]
	add	r0, r0, #64
	ldr	r2, [sp, #20]
	bl	memcpy
	mov	r0, #1
	ldr	r1, [r4, #440]
	b	.L2562
.L2577:
	ldr	r0, .L2579+108
	bl	printk
	add	r0, sp, #8
	mov	r1, r4
	mov	r2, #520
	bl	rk_copy_from_user
	cmp	r0, #0
	bne	.L2565
.L2504:
	ldr	r2, [sp, #8]
	ldr	r3, .L2579+112
	cmp	r2, r3
	bne	.L2557
	ldr	r3, [sp, #12]
	cmp	r3, #512
	bhi	.L2557
	ldr	r5, .L2579+52
	ldr	r4, [r5, #448]
	cmp	r4, #0
	beq	.L2464
	ldr	r3, [r5, #452]
	ldr	r2, .L2579+116
	ldr	r1, [r3, #0]
	cmp	r1, r2
	beq	.L2505
	str	r2, [r3, #0]
	mov	r2, #504
	ldr	r3, [r5, #452]
	str	r2, [r3, #4]
	mov	r2, #0
	str	r2, [r3, #8]
	str	r2, [r3, #12]
.L2505:
	ldr	r1, [r5, #452]
	mov	r4, #0
	mov	r0, r4
	str	r4, [r1, #16]
	bl	StorageSysDataStore
	ldr	r3, [r5, #440]
	ldr	r2, .L2579+96
	ldr	r5, .L2579+52
	ldr	r1, [r3, #0]
	cmp	r1, r2
	strne	r2, [r3, #0]
	ldr	r0, [r5, #440]
	mov	r1, #128
	ldrne	r3, .L2579+52
	movne	r2, #504
	ldrne	r3, [r3, #440]
	stmneib	r3, {r2, r4}
	mov	r4, #0
	str	r4, [r0, #12]
	add	r0, r0, #64
	bl	__memzero
	mov	r0, #1
	ldr	r1, [r5, #440]
	bl	StorageSysDataStore
	str	r4, [r5, #448]
	str	r4, [r5, #444]
	b	.L2490
.L2469:
	ldr	r0, .L2579+120
	bl	printk
	mov	r1, r4
	add	r0, sp, #8
	mov	r2, #520
	bl	rk_copy_from_user
	subs	r4, r0, #0
	bne	.L2565
.L2507:
	ldr	r2, [sp, #8]
	ldr	r3, .L2579+124
	cmp	r2, r3
	bne	.L2557
	ldr	r3, [sp, #12]
	cmp	r3, #512
	bhi	.L2557
	ldr	r5, .L2579+52
	ldr	r3, [r5, #448]
	cmp	r3, #1
	beq	.L2464
	ldr	r3, [r5, #452]
	mov	r0, #0
	ldr	r2, .L2579+116
	ldr	r1, [r3, #0]
	cmp	r1, r2
	strne	r2, [r3, #0]
	ldr	r1, [r5, #452]
	ldrne	r3, [r5, #452]
	movne	r2, #504
	strne	r4, [r3, #12]
	stmneib	r3, {r2, r4}
	mov	r3, #1
	str	r3, [r1, #16]
	bl	StorageSysDataStore
	ldr	r3, [r5, #440]
	ldr	r2, .L2579+96
	ldr	r1, [r3, #0]
	cmp	r1, r2
	beq	.L2509
	str	r2, [r3, #0]
	mov	r1, #504
	ldr	r3, .L2579+52
	mov	r2, #0
	ldr	r3, [r3, #440]
	stmib	r3, {r1, r2}
.L2509:
	ldr	r5, .L2579+52
	mov	r1, #128
	mov	r4, #0
	ldr	r0, [r5, #440]
	str	r4, [r0, #12]
	add	r0, r0, #64
	bl	__memzero
	mov	r0, #1
	ldr	r1, [r5, #440]
	bl	StorageSysDataStore
	mov	r3, #1
	str	r3, [r5, #448]
	b	.L2490
.L2576:
	ldr	r0, .L2579+128
	bl	printk
	add	r0, sp, #8
	mov	r1, r4
	mov	r2, #520
	bl	rk_copy_from_user
	cmp	r0, #0
	bne	.L2565
.L2510:
	ldr	r2, [sp, #8]
	ldr	r3, .L2579+132
	cmp	r2, r3
	bne	.L2557
	ldr	r2, [sp, #12]
	cmp	r2, #512
	addls	r0, sp, #16
	ldrls	r1, .L2579+136
	bls	.L2566
	b	.L2557
.L2471:
	ldr	r3, .L2579+20
	cmp	r5, r3
	ldreq	r0, .L2579+140
	beq	.L2558
	ldr	r3, .L2579+144
	cmp	r5, r3
	ldreq	r0, .L2579+148
	ldrne	r0, .L2579+152
.L2558:
	bl	printk
	add	r0, sp, #8
	mov	r1, r4
	mov	r2, #520
	bl	rk_copy_from_user
	cmp	r0, #0
	bne	.L2565
.L2514:
	ldr	r2, [sp, #8]
	ldr	r3, .L2579+156
	cmp	r2, r3
	bne	.L2555
	ldr	r3, .L2579+144
	ldr	r6, .L2579+52
	cmp	r5, r3
	bne	.L2515
	ldr	r3, [r6, #440]
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
	b	.L2464
.L2515:
	ldr	r3, [r6, #968]
	cmp	r3, #10
	bhi	.L2555
	ldr	r3, [r6, #440]
	ldr	r1, [sp, #12]
	ldr	r2, [r3, #24]
	cmp	r2, r1
	beq	.L2516
	cmp	r2, #0
	beq	.L2516
	ldr	r0, .L2579+160
	bl	printk
	ldr	r3, [r6, #968]
	add	r3, r3, #1
	str	r3, [r6, #968]
	b	.L2555
.L2516:
	ldr	r0, .L2579+52
	mov	r2, #0
	str	r2, [r0, #968]
	ldr	r0, .L2579+20
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
	b	.L2490
.L2479:
	ldr	r0, .L2579+164
	bl	printk
	add	r0, sp, #8
	mov	r1, r4
	mov	r2, #520
	bl	rk_copy_from_user
	cmp	r0, #0
	bne	.L2565
.L2519:
	ldr	r2, [sp, #8]
	ldr	r3, .L2579+168
	cmp	r2, r3
	bne	.L2557
	ldr	r2, [sp, #12]
	cmp	r2, #504
	bhi	.L2557
	ldr	r3, .L2579+172
	add	r0, sp, #16
	cmp	r5, r3
	ldr	r3, .L2579+52
	ldreq	r1, [r3, #972]
	ldrne	r1, [r3, #976]
	add	r1, r1, #8
.L2566:
	bl	memcpy
.L2567:
	add	r1, sp, #8
	mov	r0, r4
	mov	r2, #520
.L2568:
	bl	rk_copy_to_user
	subs	r4, r0, #0
	bne	.L2555
	b	.L2490
.L2480:
	ldr	r0, .L2579+176
	bl	printk
	add	r0, sp, #8
	mov	r1, r4
	mov	r2, #520
	bl	rk_copy_from_user
	cmp	r0, #0
	beq	.L2522
.L2565:
	ldr	r0, .L2579+64
	bl	printk
	b	.L2555
.L2522:
	ldr	r2, [sp, #8]
	ldr	r3, .L2579+168
	cmp	r2, r3
	bne	.L2557
	ldr	r2, [sp, #12]
	cmp	r2, #504
	bhi	.L2557
	ldr	r3, .L2579+28
	add	r2, r2, #8
	ldr	r4, .L2579+52
	cmp	r5, r3
	bne	.L2523
	add	r1, sp, #8
	ldr	r0, [r4, #972]
	bl	memcpy
	ldr	r1, [r4, #972]
	mov	r0, #2
.L2562:
	bl	StorageSysDataStore
	mov	r4, r0
	b	.L2490
.L2523:
	add	r1, sp, #8
	ldr	r0, [r4, #976]
	bl	memcpy
	mov	r0, #3
	ldr	r1, [r4, #976]
	b	.L2562
.L2575:
	bl	rknand_dev_flush
	b	.L2561
.L2557:
	mvn	r4, #0
.L2490:
	mov	r1, r4
	ldr	r0, .L2579+180
	bl	printk
	b	.L2464
.L2524:
	mvn	r4, #21
	b	.L2464
.L2532:
	mvn	r4, #11
	b	.L2464
.L2538:
	mvn	r4, #1
	b	.L2464
.L2555:
	mvn	r4, #13
.L2464:
	mov	r0, r4
	add	sp, sp, #528
	ldmfd	sp!, {r4, r5, r6, pc}
.L2580:
	.align	2
.L2579:
	.word	1074033155
	.word	1074029694
	.word	1074029570
	.word	1074031656
	.word	1074029695
	.word	1074031666
	.word	1074033235
	.word	1074034193
	.word	1074034194
	.word	.LC140
	.word	.LC142
	.word	.LC143
	.word	.LC144
	.word	.LANCHOR2
	.word	.LC145
	.word	.LC146
	.word	.LC141
	.word	.LC147
	.word	.LC148
	.word	.LC149
	.word	.LC150
	.word	.LC151
	.word	.LC152
	.word	.LC153
	.word	1263358532
	.word	-1067903959
	.word	.LC154
	.word	.LC155
	.word	1112753220
	.word	1146313043
	.word	.LC156
	.word	1112755781
	.word	.LC157
	.word	1094995539
	.word	.LANCHOR2+456
	.word	.LC158
	.word	1074031676
	.word	.LC159
	.word	.LC160
	.word	1280262987
	.word	.LC161
	.word	.LC162
	.word	1145980246
	.word	1074034192
	.word	.LC163
	.word	.LC164
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
	ldr	r4, .L2584
	mov	r6, #0
	add	r0, r4, #456
	ldr	r5, [r4, #428]
	str	r6, [r4, #436]
	add	r1, r5, #1536
	add	r3, r5, #512
	str	r5, [r4, #452]
	str	r3, [r4, #440]
	add	r3, r5, #1024
	str	r1, [r4, #976]
	str	r3, [r4, #972]
	bl	memcpy
	ldr	r7, [r5, #508]
	ldr	r3, [r5, #16]
	cmp	r7, r6
	str	r6, [r4, #444]
	str	r6, [r4, #968]
	str	r3, [r4, #448]
	beq	.L2582
	mov	r0, r5
	mov	r1, #508
	bl	JSHash
	cmp	r7, r0
	beq	.L2582
	str	r6, [r5, #16]
	ldr	r0, .L2584+4
	str	r6, [r4, #448]
	bl	printk
.L2582:
	ldr	r3, [r4, #448]
	mov	r0, #2
	ldr	r4, .L2584
	cmp	r3, #0
	ldrne	r2, .L2584+8
	ldrne	r3, .L2584
	ldr	r1, [r4, #972]
	strne	r2, [r3, #444]
	bl	StorageSysDataLoad
	ldr	r1, [r4, #976]
	mov	r0, #3
	bl	StorageSysDataLoad
	ldmfd	sp!, {r3, r4, r5, r6, r7, lr}
	b	rknand_sys_storage_init
.L2585:
	.align	2
.L2584:
	.word	.LANCHOR2
	.word	.LC165
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
	ldr	r7, .L2595
	cmp	r0, #0
	str	r0, [r7, #980]
	beq	.L2593
	ldr	sl, .L2595+4
	mov	r6, #0
	mov	r4, r6
	mov	r5, r6
	movw	r8, #65532
.L2591:
	ldr	r0, [r7, #980]
	mov	r1, r5, asl #7
	mov	r2, #0
	bl	rk_ftl_vendor_ops.constprop.20
	cmp	r0, #0
	bne	.L2589
	ldr	r3, [r7, #980]
	ldr	r0, .L2595+8
	ldr	r1, [r3, #0]
	ldr	r2, [r3, r8]
	ldr	r3, [r3, #4]
	bl	printk
	ldr	r0, [r7, #980]
	ldr	r3, [r0, #0]
	cmp	r3, sl
	bne	.L2590
	ldr	r3, [r0, r8]
	ldr	r2, [r0, #4]
	cmp	r3, r2
	bne	.L2590
	cmp	r4, r3
	movcc	r6, r5
	movcc	r4, r3
.L2590:
	add	r5, r5, #1
	cmp	r5, #2
	bne	.L2591
	cmp	r4, #0
	beq	.L2592
	mov	r1, r6, asl #7
	mov	r2, #0
	bl	rk_ftl_vendor_ops.constprop.20
	cmp	r0, #0
	ldmeqfd	sp!, {r3, r4, r5, r6, r7, r8, sl, pc}
	b	.L2589
.L2592:
	mov	r1, #65536
	bl	__memzero
	ldr	r3, .L2595
	ldr	r1, .L2595+4
	mov	r2, #1
	mov	r0, r4
	ldr	r3, [r3, #980]
	stmia	r3, {r1, r2}
	movw	r1, #65532
	str	r2, [r3, r1]
	ldr	r2, .L2595+12
	strh	r4, [r3, #12]	@ movhi
	strh	r2, [r3, #14]	@ movhi
	ldmfd	sp!, {r3, r4, r5, r6, r7, r8, sl, pc}
.L2589:
	ldr	r4, .L2595
	ldr	r0, [r4, #980]
	bl	kfree
	mov	r3, #0
	mvn	r0, #0
	str	r3, [r4, #980]
	ldmfd	sp!, {r3, r4, r5, r6, r7, r8, sl, pc}
.L2593:
	mvn	r0, #11
	ldmfd	sp!, {r3, r4, r5, r6, r7, r8, sl, pc}
.L2596:
	.align	2
.L2595:
	.word	.LANCHOR2
	.word	1380668996
	.word	.LC166
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
	ldr	r1, .L2604
	stmfd	sp!, {r4, r5, r6, lr}
	.save {r4, r5, r6, lr}
	ldr	ip, [r1, #980]
	cmp	ip, #0
	ldrneh	r4, [ip, #10]
	movne	r5, ip
	movne	r1, #0
	bne	.L2599
	b	.L2603
.L2601:
	ldrh	r6, [r5, #16]
	add	r5, r5, #8
	cmp	r6, r3
	bne	.L2600
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
.L2600:
	add	r1, r1, #1
.L2599:
	cmp	r1, r4
	bcc	.L2601
	mvn	r0, #0
	ldmfd	sp!, {r4, r5, r6, pc}
.L2603:
	mvn	r0, #0
	ldmfd	sp!, {r4, r5, r6, pc}
.L2605:
	.align	2
.L2604:
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
	ldr	r3, .L2623
	stmfd	sp!, {r4, r5, r6, r7, r8, r9, sl, fp, lr}
	.save {r4, r5, r6, r7, r8, r9, sl, fp, lr}
	mov	sl, r0
	ldr	r4, [r3, #980]
	.pad #20
	sub	sp, sp, #20
	mov	r9, r1
	mov	r5, r2
	cmp	r4, #0
	beq	.L2619
	add	r8, r2, #63
	ldrh	r2, [r4, #8]
	ldrh	r3, [r4, #10]
	bic	r8, r8, #63
	mov	r7, #0
	str	r2, [sp, #8]
	b	.L2608
.L2615:
	add	r2, r7, #2
	mov	r2, r2, asl #3
	add	r6, r4, r2
	ldrh	r2, [r4, r2]
	cmp	r2, sl
	bne	.L2609
	ldrh	r2, [r6, #4]
	add	r2, r2, #63
	bic	r2, r2, #63
	str	r2, [sp, #12]
	cmp	r5, r2
	bls	.L2610
	ldrh	r2, [r4, #14]
	cmp	r2, r8
	bcc	.L2619
	sub	r3, r3, #1
	mov	fp, r8
	ldrh	r6, [r6, #2]
	mov	r8, r3
	b	.L2611
.L2612:
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
.L2611:
	cmp	r7, r8
	add	r2, r7, #2
	bcc	.L2612
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
	b	.L2622
.L2610:
	ldrh	r0, [r6, #2]
	mov	r1, r9
	mov	r2, r5
	add	r0, r0, #1024
	add	r0, r4, r0
	bl	memcpy
	strh	r5, [r6, #4]	@ movhi
	b	.L2622
.L2609:
	add	r7, r7, #1
.L2608:
	cmp	r7, r3
	bcc	.L2615
	ldrh	r2, [r4, #14]
	cmp	r2, r8
	bcc	.L2619
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
.L2622:
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
	bl	rk_ftl_vendor_ops.constprop.20
	mov	r0, #0
	b	.L2607
.L2619:
	mvn	r0, #0
.L2607:
	add	sp, sp, #20
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, sl, fp, pc}
.L2624:
	.align	2
.L2623:
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
	ldr	r3, .L2642
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
	beq	.L2626
	ldr	r3, .L2642+4
	cmp	r6, r3
	beq	.L2628
	add	r3, r3, #1
	cmp	r6, r3
	bne	.L2639
	b	.L2641
.L2628:
	mov	r1, r5
	mov	r2, #8
	bl	rk_copy_from_user
	cmp	r0, #0
	bne	.L2639
	ldr	r2, [r4, #0]
	ldr	r3, .L2642+8
	cmp	r2, r3
	bne	.L2638
	ldrh	r0, [r4, #4]
	add	r1, r4, #8
	ldrh	r2, [r4, #6]
	bl	rk_ftl_vendor_read
	cmn	r0, #1
	beq	.L2634
	uxth	r2, r0
	mov	r1, r4
	strh	r2, [r4, #6]	@ movhi
	mov	r0, r5
	add	r2, r2, #8
	bl	rk_copy_to_user
	cmp	r0, #0
	moveq	r5, r0
	mvnne	r5, #13
	b	.L2627
.L2641:
	mov	r1, r5
	mov	r2, #8
	bl	rk_copy_from_user
	cmp	r0, #0
	bne	.L2639
	ldr	r2, [r4, #0]
	ldr	r3, .L2642+8
	cmp	r2, r3
	bne	.L2638
	ldrh	r2, [r4, #6]
	movw	r3, #4087
	cmp	r2, r3
	bhi	.L2638
	mov	r0, r4
	mov	r1, r5
	add	r2, r2, #8
	bl	rk_copy_from_user
	cmp	r0, #0
	bne	.L2639
	ldrh	r0, [r4, #4]
	add	r1, r4, #8
	ldrh	r2, [r4, #6]
	bl	rk_ftl_vendor_write
.L2634:
	mov	r5, r0
	b	.L2627
.L2638:
	mvn	r5, #0
	b	.L2627
.L2639:
	mvn	r5, #13
.L2627:
	mov	r0, r4
	bl	kfree
.L2626:
	mov	r0, r5
	ldmfd	sp!, {r4, r5, r6, pc}
.L2643:
	.align	2
.L2642:
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
	.type	__func__.14251, %object
	.size	__func__.14251, 11
__func__.14251:
	.ascii	"FtlMemInit\000"
.LC0:
	.byte	60
	.byte	40
	.byte	24
	.byte	16
	.type	__func__.15149, %object
	.size	__func__.15149, 21
__func__.15149:
	.ascii	"FtlVpcCheckAndModify\000"
	.section	.rodata.str1.1,"aMS",%progbits,1
.LC1:
	.ascii	"FlashEraseBlocks pageAddr error %x\012\000"
.LC2:
	.ascii	"No.%d FLASH ID:%x %x %x %x %x %x\012\000"
.LC3:
	.ascii	"phyBlk = 0x%x die = %d block_in_die = 0x%x 0x%8x\012"
	.ascii	"\000"
.LC4:
	.ascii	"FLASH INFO:\012\000"
.LC5:
	.ascii	"FLASH ID: %x\012\000"
.LC6:
	.ascii	"Device Capacity: %d MB\012\000"
.LC7:
	.ascii	"FMWAIT: %x %x %x %x\012\000"
.LC8:
	.ascii	"FTL INFO:\012\000"
.LC9:
	.ascii	"g_MaxLpn = 0x%x\012\000"
.LC10:
	.ascii	"g_VaildLpn = 0x%x\012\000"
.LC11:
	.ascii	"read_page_count = 0x%x\012\000"
.LC12:
	.ascii	"discard_page_count = 0x%x\012\000"
.LC13:
	.ascii	"write_page_count = 0x%x\012\000"
.LC14:
	.ascii	"cache_write_count = 0x%x\012\000"
.LC15:
	.ascii	"l2p_write_count = 0x%x\012\000"
.LC16:
	.ascii	"gc_page_count = 0x%x\012\000"
.LC17:
	.ascii	"totle_write = %d MB\012\000"
.LC18:
	.ascii	"totle_read = %d MB\012\000"
.LC19:
	.ascii	"GSV = 0x%x\012\000"
.LC20:
	.ascii	"GDV = 0x%x\012\000"
.LC21:
	.ascii	"bad blk num = %d %d\012\000"
.LC22:
	.ascii	"free_superblocks = 0x%x\012\000"
.LC23:
	.ascii	"mlc_EC = 0x%x\012\000"
.LC24:
	.ascii	"slc_EC = 0x%x\012\000"
.LC25:
	.ascii	"avg_EC = 0x%x\012\000"
.LC26:
	.ascii	"sys_EC = 0x%x\012\000"
.LC27:
	.ascii	"max_EC = 0x%x\012\000"
.LC28:
	.ascii	"min_EC = 0x%x\012\000"
.LC29:
	.ascii	"PLT = 0x%x\012\000"
.LC30:
	.ascii	"POT = 0x%x\012\000"
.LC31:
	.ascii	"MaxSector = 0x%x\012\000"
.LC32:
	.ascii	"init_sys_blks_pp = 0x%x\012\000"
.LC33:
	.ascii	"sys_blks_pp = 0x%x\012\000"
.LC34:
	.ascii	"free sysblock = 0x%x\012\000"
.LC35:
	.ascii	"data_blks_pp = 0x%x\012\000"
.LC36:
	.ascii	"data_op_blks_pp = 0x%x\012\000"
.LC37:
	.ascii	"max_data_blks = 0x%x\012\000"
.LC38:
	.ascii	"Sys.id = 0x%x\012\000"
.LC39:
	.ascii	"Bbt.id = 0x%x\012\000"
.LC40:
	.ascii	"ACT.page = 0x%x\012\000"
.LC41:
	.ascii	"ACT.plane = 0x%x\012\000"
.LC42:
	.ascii	"ACT.id = 0x%x\012\000"
.LC43:
	.ascii	"ACT.mode = 0x%x\012\000"
.LC44:
	.ascii	"ACT.a_pages = 0x%x\012\000"
.LC45:
	.ascii	"ACT VPC = 0x%x\012\000"
.LC46:
	.ascii	"BUF.page = 0x%x\012\000"
.LC47:
	.ascii	"BUF.plane = 0x%x\012\000"
.LC48:
	.ascii	"BUF.id = 0x%x\012\000"
.LC49:
	.ascii	"BUF.mode = 0x%x\012\000"
.LC50:
	.ascii	"BUF.a_pages = 0x%x\012\000"
.LC51:
	.ascii	"BUF VPC = 0x%x\012\000"
.LC52:
	.ascii	"TMP.page = 0x%x\012\000"
.LC53:
	.ascii	"TMP.plane = 0x%x\012\000"
.LC54:
	.ascii	"TMP.id = 0x%x\012\000"
.LC55:
	.ascii	"TMP.mode = 0x%x\012\000"
.LC56:
	.ascii	"TMP.a_pages = 0x%x\012\000"
.LC57:
	.ascii	"GC.page = 0x%x\012\000"
.LC58:
	.ascii	"GC.plane = 0x%x\012\000"
.LC59:
	.ascii	"GC.id = 0x%x\012\000"
.LC60:
	.ascii	"GC.mode = 0x%x\012\000"
.LC61:
	.ascii	"GC.a_pages = 0x%x\012\000"
.LC62:
	.ascii	"WR_CHK = 0x%x %x %x %x\012\000"
.LC63:
	.ascii	"Read Err Cnt = 0x%x\012\000"
.LC64:
	.ascii	"Prog Err Cnt = 0x%x\012\000"
.LC65:
	.ascii	"gc_free_blk_th= 0x%x\012\000"
.LC66:
	.ascii	"gc_merge_free_blk_th= 0x%x\012\000"
.LC67:
	.ascii	"gc_skip_write_count= 0x%x\012\000"
.LC68:
	.ascii	"gc_blk_index= 0x%x\012\000"
.LC69:
	.ascii	"free min EC= 0x%x\012\000"
.LC70:
	.ascii	"free max EC= 0x%x\012\000"
.LC71:
	.ascii	"GC__SB VPC = 0x%x\012\000"
.LC72:
	.ascii	"%d. [0x%x]=0x%x 0x%x  0x%x\012\000"
.LC73:
	.ascii	"free %d. [0x%x] 0x%x  0x%x\012\000"
.LC74:
	.ascii	"%s\012\000"
.LC75:
	.ascii	"FTL version: 5.0.44 20171016\000"
.LC76:
	.ascii	"GetSwlReplaceBlock min_ec_id =%x %x\012\000"
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
	.ascii	"slc mode\000"
.LC98:
	.ascii	"no ect\000"
.LC99:
	.ascii	"prog error: = %x\012\000"
.LC100:
	.ascii	"prog read error: = %x\012\000"
.LC101:
	.ascii	"prog read s error: = %x %x %x\012\000"
.LC102:
	.ascii	"prog read d error: = %x %x %x\012\000"
.LC103:
	.ascii	"FtlVpcTblFlush error = %x error count = %d\012\000"
.LC104:
	.ascii	"FtlBbmTblFlush id=%x,page=%x,previd=%x cnt=%d\012\000"
.LC105:
	.ascii	"FtlBbmTblFlush error:%x\012\000"
.LC106:
	.ascii	"FtlBbmTblFlush error = %x error count = %d\012\000"
.LC107:
	.ascii	"FtlGcFreeBadSuperBlk 0x%x\012\000"
.LC108:
	.ascii	"decrement_vpc_count %x = %d\012\000"
.LC109:
	.ascii	"FlashMakeFactorBbt %d\012\000"
.LC110:
	.ascii	"bad block:%d %d\012\000"
.LC111:
	.ascii	"FMFB:%d %d\012\000"
.LC112:
	.ascii	"E:bad block:%d\012\000"
.LC113:
	.ascii	"FMFB:Save %d %d\012\000"
.LC114:
	.ascii	"FtlMapWritePage error = %x \012\000"
.LC115:
	.ascii	"FtlMapWritePage error = %x error count = %d\012\000"
.LC116:
	.ascii	"spuer block %x vpn is 0\012 \000"
.LC117:
	.ascii	"...%s enter...\012\000"
.LC118:
	.ascii	"FtlCheckVpc %x = %x  %x\012\000"
.LC119:
	.ascii	"%d GC datablk  = %x vpc %x %x\012\000"
.LC120:
	.ascii	"SWL %x, FSB = %x vpc= %x,ec=%x th=%x\012\000"
.LC121:
	.ascii	"Ftlwrite decrement_vpc_count %x = %d\012\000"
.LC122:
	.ascii	"FtlWrite: lpa error:%x %x\012\000"
.LC123:
	.ascii	"FLFB:%d %d\012\000"
.LC124:
	.ascii	"BBT:\000"
.LC125:
	.ascii	"otp error! %d\000"
.LC126:
	.ascii	"rr\000"
.LC127:
	.ascii	"FlashLoadPhyInfo fail %x!!\012\000"
.LC128:
	.ascii	"FtlInit %x\012\000"
.LC129:
	.ascii	"IdBlockReadData %x %x\012\000"
.LC130:
	.ascii	"IdBlockReadData %x %x ret= %x\012\000"
.LC131:
	.ascii	"IDBlockWriteData %x %x\012\000"
.LC132:
	.ascii	"IDBlockWriteData %x %x ret= %x\012\000"
.LC133:
	.ascii	"idblk:\000"
.LC134:
	.ascii	"idb reverse %x %x\012\000"
.LC135:
	.ascii	"write_idblock totle_sec %x %x\012\000"
.LC136:
	.ascii	"write and check error:%d idb=%x,offset=%x,r=%x,w=%x"
	.ascii	"\012\000"
.LC137:
	.ascii	"write\000"
.LC138:
	.ascii	"read\000"
.LC139:
	.ascii	"write_idblock error\012\000"
.LC140:
	.ascii	"READ_SECTOR_IO\012\000"
.LC141:
	.ascii	"rk_copy_from_user error\012\000"
.LC142:
	.ascii	"READ_SECTOR_IO %x %x\012\000"
.LC143:
	.ascii	"rk_copy_to_user error\012\000"
.LC144:
	.ascii	"WRITE_SECTOR_IO\012\000"
.LC145:
	.ascii	"WRITE_SECTOR_IO %x %x\012\000"
.LC146:
	.ascii	"END_WRITE_SECTOR_IO\012\000"
.LC147:
	.ascii	"END_WRITE_SECTOR_IO %x %x\012\000"
.LC148:
	.ascii	"GET_FLASH_INFO_IO\012\000"
.LC149:
	.ascii	"GET_BAD_BLOCK_IO\012\000"
.LC150:
	.ascii	"bbt:\000"
.LC151:
	.ascii	"GET_LOCK_FLAG_IO\012\000"
.LC152:
	.ascii	"GET_PUBLIC_KEY_IO\012\000"
.LC153:
	.ascii	"RKNAND_GET_DRM_KEY\012\000"
.LC154:
	.ascii	"RKNAND_STORE_DRM_KEY\012\000"
.LC155:
	.ascii	"RKNAND_DIASBLE_SECURE_BOOT\012\000"
.LC156:
	.ascii	"RKNAND_ENASBLE_SECURE_BOOT\012\000"
.LC157:
	.ascii	"RKNAND_GET_SN_SECTOR\012\000"
.LC158:
	.ascii	"RKNAND_LOADER_UNLOCK\012\000"
.LC159:
	.ascii	"RKNAND_LOADER_STATUS\012\000"
.LC160:
	.ascii	"RKNAND_LOADER_LOCK\012\000"
.LC161:
	.ascii	"LockKey not match %d\012\000"
.LC162:
	.ascii	"RKNAND_GET_VENDOR_SECTOR\012\000"
.LC163:
	.ascii	"RKNAND_STORE_VENDOR_SECTOR\012\000"
.LC164:
	.ascii	"return ret = %lx\012\000"
.LC165:
	.ascii	"secureBootEn check error\012\000"
.LC166:
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
	.type	g_totle_vendor_block, %object
	.size	g_totle_vendor_block, 2
g_totle_vendor_block:
	.space	2
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
	.type	p_map_block_valid_page_count, %object
	.size	p_map_block_valid_page_count, 4
p_map_block_valid_page_count:
	.space	4
	.type	p_l2p_map_buf, %object
	.size	p_l2p_map_buf, 4
p_l2p_map_buf:
	.space	4
	.type	gL2pMapInfo, %object
	.size	gL2pMapInfo, 40
gL2pMapInfo:
	.space	40
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
	.size	gVendorBlkInfo, 40
gVendorBlkInfo:
	.space	40
	.type	g_ect_tbl_power_up_flush, %object
	.size	g_ect_tbl_power_up_flush, 2
g_ect_tbl_power_up_flush:
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
