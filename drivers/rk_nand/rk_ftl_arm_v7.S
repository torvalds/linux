/*
 * Copyright (c) 2016-2018, Fuzhou Rockchip Electronics Co., Ltd
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 * date: 2018-09-05
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
	.type	FlashGetReadRetryDefault.part.26, %function
FlashGetReadRetryDefault.part.26:
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
	.size	FlashGetReadRetryDefault.part.26, .-FlashGetReadRetryDefault.part.26
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
	cmp	r4, #77
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
	.word	.LANCHOR1+2860
	.word	.LANCHOR0+896
	.word	.LANCHOR1+2988
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
	ldr	r3, .L91
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
	.size	ftl_flash_suspend, .-ftl_flash_suspend
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
	@ link register save eliminated.
	ldr	r3, .L124
	ands	r1, r0, #6
	ldr	r2, [r3, #3012]
	ldr	r3, [r2, #0]
	bfieq	r3, r1, #13, #1
	beq	.L123
	orr	r3, r3, #24576
	movw	r1, #16641
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
.L123:
	str	r3, [r2, #0]
	mov	r0, #0
	bx	lr
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
	.word	.LANCHOR1+3020
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
	addne	r3, r9, #3072
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
	.global	micron_auto_read_calibration_config
	.type	micron_auto_read_calibration_config, %function
micron_auto_read_calibration_config:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	stmfd	sp!, {r4, r5, r6, lr}
	.save {r4, r5, r6, lr}
	mov	r6, r1
	mov	r4, r0
	bl	NandcWaitFlashReady
	ldr	r3, .L206
	mov	r2, #239
	mov	r0, #200
	add	r4, r3, r4, asl #3
	ldr	r5, [r4, #864]
	ldrb	r4, [r4, #868]	@ zero_extendqisi2
	add	r4, r4, #8
	add	r3, r5, r4, asl #8
	str	r2, [r3, #8]
	mov	r2, #150
	str	r2, [r3, #4]
	bl	NandcDelayns
	str	r6, [r5, r4, asl #8]
	mov	r3, #0
	str	r3, [r5, r4, asl #8]
	str	r3, [r5, r4, asl #8]
	str	r3, [r5, r4, asl #8]
	ldmfd	sp!, {r4, r5, r6, pc}
.L207:
	.align	2
.L206:
	.word	.LANCHOR0
	.fnend
	.size	micron_auto_read_calibration_config, .-micron_auto_read_calibration_config
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
	ldrb	r2, [r3, #2997]	@ zero_extendqisi2
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
	.global	ftl_flash_de_init
	.type	ftl_flash_de_init, %function
ftl_flash_de_init:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	stmfd	sp!, {r3, r4, r5, lr}
	.save {r3, r4, r5, lr}
	mov	r0, #0
	ldr	r4, .L249
	bl	NandcWaitFlashReady
	bl	FlashSetReadRetryDefault
	ldr	r3, .L249+4
	ldr	r2, [r4, #3776]
	cmp	r2, r3
	bne	.L247
	mov	r0, #0
	bl	flash_enter_slc_mode
.L247:
	ldrb	r3, [r4, #3780]	@ zero_extendqisi2
	ldr	r5, .L249
	cmp	r3, #0
	beq	.L248
	ldrb	r3, [r5, #3772]	@ zero_extendqisi2
	tst	r3, #1
	beq	.L248
	mov	r0, #1
	bl	FlashSetInterfaceMode
	mov	r0, #1
	bl	NandcSetMode
	mov	r3, #0
	strb	r3, [r5, #3780]
.L248:
	ldr	r3, [r4, #864]
	mov	r0, #0
	str	r0, [r3, #336]
	ldmfd	sp!, {r3, r4, r5, pc}
.L250:
	.align	2
.L249:
	.word	.LANCHOR0
	.word	1446522928
	.fnend
	.size	ftl_flash_de_init, .-ftl_flash_de_init
	.align	2
	.global	FlashReadDpCmd
	.type	FlashReadDpCmd, %function
FlashReadDpCmd:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	ldr	r3, .L255
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
	bne	.L252
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
	b	.L254
.L252:
	uxtb	r2, r5
	str	r2, [r6, #4]
	mov	r2, r5, lsr #8
	str	r2, [r6, #4]
	mov	r2, r5, lsr #16
	str	r2, [r6, #4]
	ldrb	r3, [r3, #905]	@ zero_extendqisi2
	str	r3, [r6, #8]
.L254:
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
.L256:
	.align	2
.L255:
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
	ldr	r3, .L258
	add	r3, r3, r0, asl #3
	ldr	r3, [r3, #864]
	str	r1, [r3, #336]
	bx	lr
.L259:
	.align	2
.L258:
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
	ldr	r1, .L270
	bl	__aeabi_idiv
	ldr	r3, .L270+4
	ldr	r3, [r3, #3012]
	cmp	r0, #250
	movwgt	r2, #8354
	bgt	.L268
	cmp	r0, #220
	bgt	.L269
.L263:
	cmp	r0, #185
	movwgt	r2, #4226
	bgt	.L268
	cmp	r0, #160
	movwgt	r2, #4194
	bgt	.L268
	cmp	r4, #35
	movwls	r2, #4193
	bls	.L268
	cmp	r4, #99
	movwls	r2, #4225
	bls	.L268
.L269:
	movw	r2, #8322
.L268:
	str	r2, [r3, #4]
	ldmfd	sp!, {r4, pc}
.L271:
	.align	2
.L270:
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
	mov	r2, #0
	ldr	r4, .L274
	mov	r3, #1
	mov	r5, #0
	str	r0, [r4, #3012]
	str	r2, [r4, #868]
	str	r0, [r4, #864]
	str	r3, [r4, #876]
	mov	r3, #2
	str	r0, [r4, #872]
	str	r3, [r4, #884]
	mov	r3, #3
	str	r0, [r4, #880]
	str	r0, [r4, #888]
	str	r3, [r4, #892]
	ldr	r3, [r0, #0]
	and	r3, r3, #253952
	ubfx	r1, r3, #13, #1
	bfi	r3, r2, #13, #1
	ldr	r2, [r0, #352]
	orr	r3, r3, #256
	str	r1, [r4, #3784]
	movw	r1, #2049
	ubfx	r2, r2, #16, #4
	str	r2, [r4, #3788]
	ldr	r2, [r0, #352]
	cmp	r2, r1
	str	r2, [r4, #3776]
	moveq	r2, #8
	streq	r2, [r4, #3788]
	str	r3, [r0, #0]
	mov	r0, #40
	ldr	r3, [r4, #3012]
	str	r5, [r3, #336]
	bl	NandcTimeCfg
	ldr	r3, [r4, #3012]
	movw	r2, #8322
	mov	r0, #36864
	str	r2, [r3, #344]
	ldr	r2, .L274+4
	str	r2, [r3, #304]
	bl	ftl_malloc
	str	r5, [r4, #3820]
	str	r5, [r4, #3828]
	str	r0, [r4, #3792]
	str	r0, [r4, #3796]
	add	r0, r0, #32768
	str	r0, [r4, #3800]
	ldmfd	sp!, {r3, r4, r5, pc}
.L275:
	.align	2
.L274:
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
	bls	.L277
	movw	r3, #4226
	movw	r2, #8322
	cmp	r0, r3
	cmpne	r0, r2
	bne	.L278
.L277:
	ldr	r3, .L279
	ldr	r3, [r3, #3012]
	str	r0, [r3, #4]
.L278:
	ldr	r3, .L279+4
	ldrb	r0, [r3, #3009]	@ zero_extendqisi2
	b	NandcTimeCfg
.L280:
	.align	2
.L279:
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
	ldr	ip, .L282
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
.L283:
	.align	2
.L282:
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
	ldr	r3, .L289
	mov	r1, #1
	ldr	r2, [r3, #3012]
	str	r0, [r3, #3832]
	mov	r3, #0
	str	r1, [r2, #8]
	mov	r1, #16
	cmp	r0, r1
	bfi	r3, r1, #8, #8
	bfc	r3, #18, #1
	beq	.L288
.L285:
	cmp	r0, #24
	orreq	r3, r3, #16
	beq	.L286
	cmp	r0, #40
	orr	r3, r3, #262144
	orr	r3, r3, #16
	bne	.L286
.L288:
	bfc	r3, #4, #1
.L286:
	orr	r3, r3, #1
	str	r3, [r2, #12]
	bx	lr
.L290:
	.align	2
.L289:
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
	ldr	r3, .L292
	strb	r0, [r3, #3836]
	b	NandcBchSel
.L293:
	.align	2
.L292:
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
	ldr	r3, .L299
	stmfd	sp!, {r4, r5, r6, lr}
	.save {r4, r5, r6, lr}
	mov	r4, #0
	ldr	r2, [r3, #3012]
	ldr	r1, [r3, #3016]
	ldr	r5, [r3, #3032]
	ldr	r6, .L299+4
	str	r1, [r2, #0]
	ldr	r1, [r3, #3020]
	ldr	r2, [r3, #3012]
	str	r1, [r2, #4]
	ldr	r1, [r3, #3024]
	str	r1, [r2, #8]
	ldr	r1, [r3, #3028]
	str	r1, [r2, #12]
	ldr	r1, [r3, #3036]
	str	r5, [r2, #304]
	str	r1, [r2, #308]
	ldr	r1, [r3, #3040]
	ldr	r3, [r3, #3044]
	str	r1, [r2, #336]
	str	r3, [r2, #344]
.L296:
	ldrb	r3, [r6, r4, asl #3]	@ zero_extendqisi2
	sub	r3, r3, #1
	uxtb	r3, r3
	cmp	r3, #253
	bhi	.L295
	uxtb	r0, r4
	bl	FlashReset
.L295:
	add	r4, r4, #1
	cmp	r4, #4
	bne	.L296
	ldr	r4, .L299
	ldrb	r3, [r4, #3780]	@ zero_extendqisi2
	cmp	r3, #0
	beq	.L297
	mov	r0, #1
	bl	NandcSetMode
	ldrb	r0, [r4, #3772]	@ zero_extendqisi2
	bl	FlashSetInterfaceMode
	ldrb	r0, [r4, #3772]	@ zero_extendqisi2
	bl	NandcSetMode
	ubfx	r0, r5, #8, #8
	bl	NandcSetDdrPara
.L297:
	ldr	r3, .L299
	ldr	r3, [r3, #3624]
	ldrb	r0, [r3, #20]	@ zero_extendqisi2
	ldmfd	sp!, {r4, r5, r6, lr}
	b	FlashBchSel
.L300:
	.align	2
.L299:
	.word	.LANCHOR0
	.word	.LANCHOR0+2980
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
	ldr	r3, [r4, #0]
	ubfx	r3, r3, #8, #8
	and	r3, r3, #2
	uxtb	r3, r3
	cmp	r3, #0
	bne	.L304
	mov	r0, r4
	ldmfd	sp!, {r4, lr}
	b	wait_for_nand_flash_ready
.L304:
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
	stmfd	sp!, {r4, r5, r6, r7, r8, r9, sl, fp, lr}
	.save {r4, r5, r6, r7, r8, r9, sl, fp, lr}
	.pad #28
	sub	sp, sp, #28
	ldr	r5, .L325
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
	ldrb	r3, [r5, #3837]	@ zero_extendqisi2
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
	ldr	r5, .L325
	mov	r4, #0
	ldr	r8, .L325+4
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
	ldrb	r0, [r8, r4, asl #4]	@ zero_extendqisi2
	bl	flash_exit_slc_mode
.L317:
	add	r4, r4, #1
.L316:
	ldrb	r2, [r5, #3762]	@ zero_extendqisi2
	ldr	r3, .L325
	cmp	r4, r2
	bcc	.L318
	ldr	r0, [r3, #3840]
	cmp	r0, #0
	beq	.L307
	ldrb	r3, [r3, #2980]	@ zero_extendqisi2
	cmp	r3, #69
	moveq	r3, #0
	moveq	r2, r3
	bne	.L324
	b	.L319
.L320:
	add	r3, r3, #1
	str	r2, [r6, #-36]
.L319:
	cmp	r3, r7
	add	r6, r6, #36
	bne	.L320
.L324:
	mov	r0, #0
.L307:
	add	sp, sp, #28
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, sl, fp, pc}
.L326:
	.align	2
.L325:
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
	ldr	r3, .L329
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
.L330:
	.align	2
.L329:
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
.L332:
	ldr	r3, [r0, #8]
	str	r3, [sp, #4]
	ldr	r3, [sp, #4]
	tst	r3, #1048576
	beq	.L332
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
	mov	r6, r1
	ldr	r1, .L347
	mov	r8, r2
	mov	r4, #0
	.pad #20
	sub	sp, sp, #20
	add	r0, r1, r0, asl #3
	ldr	r9, [sp, #56]
	ldr	r5, [r0, #864]
	ldrb	r2, [r0, #868]	@ zero_extendqisi2
	mov	r0, #16
	ldr	ip, [sp, #60]
	ldr	r7, [r5, #12]
	bfi	r7, r0, #8, #8
	bfi	r7, r4, #3, #1
	bfi	r4, r6, #1, #1
	orr	r4, r4, #8
	bfi	r7, r2, #5, #3
	mov	r2, #1
	bfi	r4, r2, #5, #2
	orr	r4, r4, #536870912
	mov	r3, r3, lsr r2
	orr	r4, r4, #1024
	bfi	r4, r3, #4, #1
	ldr	r3, [r1, #3788]
	cmp	r3, #3
	bls	.L335
	ldr	r3, [r5, #16]
	cmp	r9, #0
	cmpeq	ip, #0
	str	r3, [sp, #12]
	ldr	r3, [sp, #12]
	bfc	r3, #2, #1
	str	r3, [sp, #12]
	beq	.L336
	cmp	r6, #0
	beq	.L337
	ldr	r3, [r1, #3832]
	mov	r0, ip
	cmp	r3, #24
	movhi	r3, #128
	movls	r3, #64
	str	r3, [sp, #0]
	mov	r3, r8, lsr r2
	str	r3, [sp, #4]
	mov	r3, #0
	mov	r2, r3
	b	.L339
.L342:
	cmp	ip, #0
	mov	lr, r3, lsr #2
	add	r2, r2, #1
	ldrneh	fp, [r0, #2]
	mvneq	fp, #0
	ldrneh	sl, [r0], #4
	ldreq	sl, [r1, #3800]
	orrne	sl, sl, fp, asl #16
	ldrne	fp, [r1, #3800]
	streq	fp, [sl, lr, asl #2]
	strne	sl, [fp, lr, asl #2]
	ldr	lr, [sp, #0]
	add	r3, r3, lr
.L339:
	ldr	sl, [sp, #4]
	cmp	r2, sl
	bcc	.L342
.L337:
	mov	r0, r5
	add	r8, r8, #1
	bl	rk_nandc_xfer_irq_flag_init
	ldr	r3, [r5, #368]
	mov	r8, r8, asr #1
	cmp	r9, #0
	bfi	r4, r8, #22, #6
	ldr	r8, .L347
	orr	r3, r3, #1
	str	r3, [r5, #368]
	ldr	r3, [r5, #364]
	ubfx	sl, r4, #22, #5
	ldreq	r0, [r8, #3796]
	movne	r0, r9
	orr	r3, r3, #1
	str	r3, [r5, #364]
	ldr	r3, [r8, #3800]
	mov	r2, r6
	mov	r1, sl, asl #10
	str	r3, [r8, #3808]
	str	r0, [r8, #3804]
	bl	rknand_dma_map_single
	mov	r2, r6
	mov	r1, sl, asl #7
	str	r0, [r8, #3812]
	ldr	r0, [r8, #3808]
	bl	rknand_dma_map_single
	mov	r3, #1
	str	r3, [r8, #3820]
	mov	r2, #16
	ldr	r3, [r8, #3812]
	tst	r9, #3
	str	r0, [r8, #3816]
	str	r3, [r5, #20]
	ldr	r3, [r8, #3816]
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
.L336:
	ldr	r3, [sp, #12]
	str	r3, [r5, #16]
.L335:
	str	r7, [r5, #12]
	str	r4, [r5, #8]
	orr	r4, r4, #4
	str	r4, [r5, #8]
	add	sp, sp, #20
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, sl, fp, pc}
.L348:
	.align	2
.L347:
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
	bne	.L350
	cmp	r3, #0
	beq	.L351
	mov	r0, r2
	mov	r1, r3
	mov	r2, #1024
	bl	memcpy
.L351:
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
.L350:
	cmp	r3, #0
	beq	.L353
	mov	r1, r2
	mov	r0, r3
	mov	r2, #1024
	bl	memcpy
.L353:
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
	b	.L355
.L356:
	add	r3, r3, #1
	mov	r2, r2, asl #1
	uxth	r3, r3
.L355:
	cmp	r2, r0
	bls	.L356
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
	ldr	r3, .L361
	movw	r2, #3848
	cmp	r0, #23
	movw	r1, #3858
	ldrh	r2, [r3, r2]
	movls	r0, #24
	ldrh	r1, [r3, r1]
	str	r0, [r3, #3844]
	mul	r2, r2, r0
	rsb	r0, r0, r1
	mov	r1, #3856
	strh	r0, [r3, r1]	@ movhi
	mov	r0, #0
	ldr	r1, [r3, #3864]
	str	r2, [r3, #3852]
	rsb	r2, r2, r1
	str	r2, [r3, #3860]
	bx	lr
.L362:
	.align	2
.L361:
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
	movw	r2, #3868
	ldr	r3, .L380
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
	mov	r2, #3872
	strh	r0, [r3, r2]	@ movhi
	movw	r2, #3858
	strh	r6, [r3, r2]	@ movhi
	mov	r3, #0
	ldr	r2, .L380+4
.L364:
	strb	r3, [r3, r2]
	add	r3, r3, #1
	cmp	r3, #32
	bne	.L364
	ldrh	r2, [r4, #14]
	ldrh	r3, [r4, #20]
	cmp	r3, r2, lsr #8
	bcs	.L365
	ldr	r2, .L380+4
	sub	ip, r1, #1
	uxtb	r7, r0
	mov	r3, #0
	rsb	sl, r0, r2
	str	sl, [sp, #8]
	mla	ip, r0, ip, r2
	mov	r8, r7, asl #1
	uxtb	r8, r8
	str	ip, [sp, #12]
	b	.L366
.L367:
	strb	ip, [r9, r2]
	add	fp, ip, r7
	add	ip, ip, r8
	add	r5, r5, #1
	strb	fp, [sl, r2]
	uxtb	ip, ip
.L369:
	cmp	r5, r1
	add	r2, r2, r0
	bcc	.L367
	add	r3, r3, #1
.L366:
	cmp	r3, r0
	bcs	.L368
	ldr	fp, [sp, #8]
	mov	r2, #0
	uxtb	ip, r3
	mov	r5, r2
	add	r9, fp, r3
	ldr	fp, [sp, #12]
	add	sl, fp, r3
	b	.L369
.L368:
	ldr	r3, .L380
	movw	r2, #3870
	mov	r1, r1, asl #1
	mov	r6, r6, lsr #1
	strh	r1, [r3, r2]	@ movhi
	movw	r2, #3858
	strh	r6, [r3, r2]	@ movhi
.L365:
	ldr	r3, .L380
	movw	r2, #3906
	cmp	lr, #1
	mov	r1, #5
	mov	ip, #0
	ldr	sl, .L380
	ldrb	r8, [r3, #852]	@ zero_extendqisi2
	strh	r1, [r3, r2]	@ movhi
	movw	r1, #3908
	streqh	lr, [r3, r2]	@ movhi
	cmp	r8, #0
	movw	r2, #3910
	strh	ip, [r3, r1]	@ movhi
	mov	r1, #4352
	strh	r1, [r3, r2]	@ movhi
	ldrne	r3, .L380
	movne	r1, #384
	strneh	r1, [r3, r2]	@ movhi
	movw	r3, #3870
	ldrh	r5, [sl, r3]
	movw	r3, #3848
	mul	r5, r5, r0
	uxth	r5, r5
	strh	r5, [sl, r3]	@ movhi
	add	r3, r3, #10
	ldrh	r6, [sl, r3]
	add	r3, r3, #54
	mul	r0, r0, r6
	uxth	r0, r0
	strh	r0, [sl, r3]	@ movhi
	bl	Ftl_log2
	movw	r3, #3914
	ldrh	fp, [r4, #18]
	movw	r2, #3916
	ldrh	r9, [r4, #20]
	strh	r0, [sl, r3]	@ movhi
	mov	r0, r9
	ldrh	r3, [r4, #16]
	mul	r1, r3, r5
	strh	r3, [sl, r2]	@ movhi
	add	r2, r2, #2
	strh	fp, [sl, r2]	@ movhi
	mov	r2, #3920
	strh	r1, [sl, r2]	@ movhi
	add	r2, r2, #2
	strh	r9, [sl, r2]	@ movhi
	str	r3, [sp, #4]
	bl	Ftl_log2
	movw	r2, #3924
	movw	r1, #3926
	cmp	r6, #1024
	strh	r0, [sl, r2]	@ movhi
	mov	r2, r9, asl #9
	mov	r7, r0
	uxth	r2, r2
	strh	r2, [sl, r1]	@ movhi
	add	r1, r1, #2
	mov	r2, r2, lsr #8
	strh	r2, [sl, r1]	@ movhi
	movw	r2, #3930
	ldrh	r1, [r4, #26]
	mov	r4, sl
	strh	r1, [sl, r2]	@ movhi
	uxtbhi	r1, r6
	mul	r2, r6, r5
	str	r2, [sl, #3864]
	movw	r2, #3908
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
	str	r3, [sl, #3932]
	movw	sl, #3910
	ldrh	r0, [r4, sl]
	mov	r0, r0, asl #3
	bl	__aeabi_idiv
	mov	r3, #3936
	uxth	r0, r0
	strh	r0, [r4, r3]	@ movhi
	cmp	r0, #4
	ldr	r4, .L380
	ldrls	r2, .L380
	movls	r1, #4
	strlsh	r1, [r2, r3]	@ movhi
	cmp	r8, #0
	mov	r1, r5
	movne	r3, #640
	strneh	r3, [r4, sl]	@ movhi
	ldrh	r2, [r4, sl]
	movw	r3, #3938
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
	str	r3, [r4, #3944]
	mov	r3, #3936
	ldrh	r0, [r4, r3]
	bl	__aeabi_uidiv
	cmp	r5, #1
	ldreq	r3, .L380
	uxtah	r6, r6, r0
	str	r6, [r4, #3844]
	ldr	r4, .L380
	addeq	r6, r6, #4
	streq	r6, [r3, #3844]
	ldr	r0, [r4, #3844]
	uxth	r0, r0
	bl	FtlSysBlkNumInit
	ldr	r3, [r4, #3844]
	ldr	r6, [r4, #3860]
	mov	r0, #2048
	str	r3, [r4, #3948]
	movw	r3, #3916
	ldrh	r3, [r4, r3]
	mov	r6, r6, asl #2
	mul	r6, r3, r6
	movw	r3, #3924
	ldrh	r3, [r4, r3]
	add	r3, r3, #9
	mov	r6, r6, lsr r3
	mov	r3, #3952
	add	r6, r6, #2
	uxth	r6, r6
	strh	r6, [r4, r3]	@ movhi
	movw	r3, #3922
	ldrh	r5, [r4, r3]
	mov	r1, r5
	bl	__aeabi_idiv
	movw	r3, #3954
	ldrb	ip, [r4, #928]	@ zero_extendqisi2
	ldr	r1, [r4, #3944]
	cmp	ip, #0
	strh	r0, [r4, r3]	@ movhi
	mov	r3, #0
	str	r3, [r4, #3956]
	mov	r3, #3936
	ldrh	r0, [r4, r3]
	add	r2, r0, #3
	strh	r2, [r4, r3]	@ movhi
	add	r2, r1, #3
	str	r2, [r4, #3944]
	addne	r0, r0, #4
	addne	r1, r1, #5
	strneh	r0, [r4, r3]	@ movhi
	strne	r1, [r4, #3944]
	bne	.L377
.L376:
	cmp	r2, #7
	movls	r3, #8
	strls	r3, [r4, #3944]
.L377:
	movw	r3, #3960
	mov	r2, #0
	strh	r2, [r4, r3]	@ movhi
	mov	r2, #3856
	ldrh	r2, [r4, r2]
	mov	r0, #0
	mov	r1, r2, lsr #3
	add	r2, r1, r2, asl #1
	add	r2, r2, #52
	add	r6, r2, r6, asl #2
	cmp	r6, r5, asl #9
	ldrcc	r2, .L380
	movcc	r1, #1
	strcch	r1, [r2, r3]	@ movhi
	add	sp, sp, #20
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, sl, fp, pc}
.L381:
	.align	2
.L380:
	.word	.LANCHOR0
	.word	.LANCHOR0+3874
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
	ldr	r3, .L389
	movw	r2, #3962
	ldrh	r2, [r3, r2]
	cmp	r2, #0
	beq	.L387
	mov	r2, #3936
	ldrh	r1, [r3, r2]
	ldr	r2, [r3, #3964]
	mov	r3, #0
	b	.L384
.L386:
	ldrh	ip, [r2], #2
	cmp	ip, r0
	beq	.L388
	add	r3, r3, #1
	uxth	r3, r3
.L384:
	cmp	r3, r1
	bne	.L386
	mov	r0, #0
	bx	lr
.L388:
	mov	r0, #1
	bx	lr
.L387:
	mov	r0, r2
	bx	lr
.L390:
	.align	2
.L389:
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
	ldr	r2, .L399
	stmfd	sp!, {r4, lr}
	.save {r4, lr}
	ldr	r3, [r2, #3968]
	cmp	r3, #0
	beq	.L396
	ldr	ip, [r2, #3972]
	mov	r2, #0
.L395:
	ldr	r4, [ip, #16]
	cmp	r4, r0
	bcc	.L393
	cmp	r4, r1
	bls	.L397
.L393:
	add	r2, r2, #1
	add	ip, ip, #36
	cmp	r2, r3
	bne	.L395
	mov	r0, #0
	ldmfd	sp!, {r4, pc}
.L397:
	mov	r0, #1
	ldmfd	sp!, {r4, pc}
.L396:
	mov	r0, r3
	ldmfd	sp!, {r4, pc}
.L400:
	.align	2
.L399:
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
	ldr	r3, .L402
	ldr	r0, [r3, #3956]
	bx	lr
.L403:
	.align	2
.L402:
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
	ldr	r3, .L405
	ldr	r0, [r3, #3956]
	bx	lr
.L406:
	.align	2
.L405:
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
	ldr	r3, .L408
	ldr	r0, [r3, #3976]
	bx	lr
.L409:
	.align	2
.L408:
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
	.save {r0, r1, r4, r5, r6, lr}
	movw	r3, #3912
	ldr	r4, .L411
	mov	r5, r0
	ldrh	r6, [r4, r3]
	mov	r1, r6
	bl	__aeabi_uidiv
	uxth	r2, r0
	mls	r3, r6, r2, r5
	add	r1, r4, r2, asl #2
	mov	r6, #1
	ldr	r0, [r1, #4008]
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
	movw	r3, #3986
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
	movw	r3, #3912
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
	ldr	r3, [r5, #4008]
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
	movw	r8, #3912
	movw	r7, #3870
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
	mov	r3, #3872
	ldr	r5, .L424
	mov	r7, r1
	mov	r6, r0
	ldrh	r4, [r5, r3]
	mov	r1, r4
	bl	__aeabi_uidiv
	movw	r3, #3912
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
	mov	r2, #3872
	stmfd	sp!, {r4, r5, r6, lr}
	.save {r4, r5, r6, lr}
	mov	r6, r0
	ldrh	r4, [r3, r2]
	add	r2, r2, #40
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
	movw	r3, #3912
	ldr	r4, .L430
	ldrh	r1, [r4, r3]
	bl	__aeabi_uidivmod
	mov	r3, #3872
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
	.type	FtlFreeSysBlkQueueIn.part.5, %function
FtlFreeSysBlkQueueIn.part.5:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	cmp	r1, #0
	stmfd	sp!, {r4, r5, r6, lr}
	.save {r4, r5, r6, lr}
	mov	r5, r0
	beq	.L433
	ldr	r4, .L434
	ldr	r3, [r4, #4040]
	cmp	r3, #0
	bne	.L433
	bl	P2V_block_in_plane
	mov	r1, #1
	mov	r3, r5, asl #10
	mov	r2, r1
	mov	r6, r0
	ldr	r0, [r4, #4044]
	mov	r6, r6, asl #1
	str	r3, [r0, #4]
	bl	FlashEraseBlocks
	ldr	r3, [r4, #4048]
	ldrh	r2, [r3, r6]
	add	r2, r2, #1
	strh	r2, [r3, r6]	@ movhi
	ldr	r3, [r4, #4052]
	add	r3, r3, #1
	str	r3, [r4, #4052]
.L433:
	ldr	r3, .L434
	movw	r2, #4062
	movw	r0, #4056
	ldrh	r1, [r3, r2]
	add	r1, r1, #1
	strh	r1, [r3, r2]	@ movhi
	movw	r2, #4060
	ldrh	r1, [r3, r2]
	add	ip, r1, #4
	add	r1, r1, #1
	bic	r1, r1, #64512
	add	ip, r3, ip, asl #1
	strh	r5, [ip, r0]	@ movhi
	strh	r1, [r3, r2]	@ movhi
	ldmfd	sp!, {r4, r5, r6, pc}
.L435:
	.align	2
.L434:
	.word	.LANCHOR0
	.fnend
	.size	FtlFreeSysBlkQueueIn.part.5, .-FtlFreeSysBlkQueueIn.part.5
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
	movw	r3, #4062
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
	movw	r3, #4062
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
	@ link register save eliminated.
	sub	r2, r0, #1
	movw	r3, #65533
	uxth	r2, r2
	cmp	r2, r3
	bxhi	lr
	ldr	r2, .L447
	movw	r3, #4062
	ldrh	r3, [r2, r3]
	cmp	r3, #1024
	bxeq	lr
	b	FtlFreeSysBlkQueueIn.part.5
.L448:
	.align	2
.L447:
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
	ldr	r3, .L453
	ldr	r2, .L453+4
	ldr	r1, .L453+8
	stmfd	sp!, {r4, r5, lr}
	.save {r4, r5, lr}
	ldrh	r4, [r2, r3]
	movw	r3, #4062
	ldrh	r3, [r1, r3]
	and	r4, r4, #31
	cmp	r3, #0
	ldmeqfd	sp!, {r4, r5, pc}
	movw	r3, #4058
	mov	r0, #0
	ldrh	r2, [r1, r3]
	add	r3, r3, #2
	ldrh	r3, [r1, r3]
	b	.L451
.L452:
	add	ip, r1, r2, asl #1
	add	r2, r2, #1
	add	ip, ip, #4064
	add	r0, r0, #1
	mov	r2, r2, asl #22
	ldrh	r5, [ip, #0]
	add	ip, r1, r3, asl #1
	add	r3, r3, #1
	add	ip, ip, #4064
	mov	r2, r2, lsr #22
	uxth	r0, r0
	mov	r3, r3, asl #22
	strh	r5, [ip, #0]	@ movhi
	mov	r3, r3, lsr #22
.L451:
	cmp	r0, r4
	ldr	ip, .L453+8
	blt	.L452
	movw	r1, #4058
	strh	r2, [ip, r1]	@ movhi
	movw	r2, #4060
	strh	r3, [ip, r2]	@ movhi
	ldmfd	sp!, {r4, r5, pc}
.L454:
	.align	2
.L453:
	.word	-2044
	.word	.LANCHOR2
	.word	.LANCHOR0
	.fnend
	.size	FtlFreeSysBLkSort, .-FtlFreeSysBLkSort
	.align	2
	.global	FtlFreeSysBlkQueueOut
	.type	FtlFreeSysBlkQueueOut, %function
FtlFreeSysBlkQueueOut:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	stmfd	sp!, {r3, r4, r5, r6, r7, r8, sl, lr}
	.save {r3, r4, r5, r6, r7, r8, sl, lr}
	movw	r6, #4062
	ldr	r4, .L462
	movw	r7, #4058
.L456:
	ldrh	r1, [r4, r6]
	cmp	r1, #0
	beq	.L457
	ldrh	r3, [r4, r7]
	sub	r1, r1, #1
	ldr	sl, [r4, #4040]
	add	r2, r4, r3, asl #1
	cmp	sl, #0
	add	r2, r2, #4064
	add	r3, r3, #1
	bic	r3, r3, #64512
	ldrh	r5, [r2, #0]
	strh	r1, [r4, r6]	@ movhi
	strh	r3, [r4, r7]	@ movhi
	bne	.L458
	mov	r0, r5
	bl	P2V_block_in_plane
	mov	r3, r5, asl #10
	mov	r8, r0
	ldr	r0, [r4, #4044]
	str	r3, [r0, #4]
	ldrb	r3, [r4, #928]	@ zero_extendqisi2
	cmp	r3, #0
	beq	.L459
	mov	r1, sl
	mov	r2, #1
	bl	FlashEraseBlocks
.L459:
	mov	r1, #1
	ldr	r0, [r4, #4044]
	mov	r2, r1
	mov	r8, r8, asl #1
	bl	FlashEraseBlocks
	ldr	r3, [r4, #4048]
	ldrh	r2, [r3, r8]
	add	r2, r2, #1
	strh	r2, [r3, r8]	@ movhi
	ldr	r3, [r4, #4052]
	add	r3, r3, #1
	str	r3, [r4, #4052]
	b	.L458
.L457:
	ldr	r0, .L462+4
	bl	printk
.L460:
	b	.L460
.L458:
	sub	r2, r5, #1
	movw	r3, #65533
	uxth	r2, r2
	cmp	r2, r3
	bls	.L461
	mov	r1, r5
	ldrh	r2, [r4, r6]
	ldr	r0, .L462+8
	bl	printk
	b	.L456
.L461:
	mov	r0, r5
	ldmfd	sp!, {r3, r4, r5, r6, r7, r8, sl, pc}
.L463:
	.align	2
.L462:
	.word	.LANCHOR0
	.word	.LC4
	.word	.LC5
	.fnend
	.size	FtlFreeSysBlkQueueOut, .-FtlFreeSysBlkQueueOut
	.align	2
	.global	test_node_in_list
	.type	test_node_in_list, %function
test_node_in_list:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	ldr	r3, .L469
	stmfd	sp!, {r4, lr}
	.save {r4, lr}
	movw	r4, #65535
	ldr	r2, [r0, #0]
	ldr	r0, [r3, #-2024]
	ldr	ip, .L469+4
	rsb	r3, r0, r2
	mov	r3, r3, asr #1
	mul	r3, ip, r3
	mov	ip, #6
	uxth	r3, r3
.L466:
	cmp	r1, r3
	beq	.L467
	ldrh	r3, [r2, #0]
	cmp	r3, r4
	beq	.L468
	mla	r2, ip, r3, r0
	b	.L466
.L467:
	mov	r0, #1
	ldmfd	sp!, {r4, pc}
.L468:
	mov	r0, #0
	ldmfd	sp!, {r4, pc}
.L470:
	.align	2
.L469:
	.word	.LANCHOR2
	.word	-1431655765
	.fnend
	.size	test_node_in_list, .-test_node_in_list
	.align	2
	.global	insert_data_list
	.type	insert_data_list, %function
insert_data_list:
	.fnstart
	@ args = 0, pretend = 0, frame = 16
	@ frame_needed = 0, uses_anonymous_args = 0
	ldr	r2, .L486
	mov	r3, #3856
	stmfd	sp!, {r4, r5, r6, r7, r8, r9, sl, fp, lr}
	.save {r4, r5, r6, r7, r8, r9, sl, fp, lr}
	.pad #20
	sub	sp, sp, #20
	ldrh	r3, [r2, r3]
	cmp	r3, r0
	bls	.L472
	ldr	r1, .L486+4
	mov	ip, #6
	mul	ip, ip, r0
	mvn	r6, #0
	ldr	r4, [r1, #-2024]
	mov	r8, r1
	add	r2, r4, ip
	strh	r6, [r2, #2]	@ movhi
	strh	r6, [r4, ip]	@ movhi
	ldr	r3, [r1, #-2020]
	cmp	r3, #0
	beq	.L485
.L473:
	ldr	r7, [r1, #-2016]
	mov	r5, r0, asl #1
	ldrh	r1, [r2, #4]
	ldr	fp, [r8, #-2024]
	ldrh	sl, [r7, r5]
	cmp	r1, #0
	ldr	r8, .L486+8
	mulne	r6, r1, sl
	rsb	r1, fp, r3
	mov	sl, #3856
	mov	r1, r1, asr #1
	mul	r1, r8, r1
	ldr	r8, .L486
	ldrh	sl, [r8, sl]
	ldr	r8, [r8, #4048]
	uxth	r1, r1
	add	r5, r8, r5
	str	r5, [sp, #12]
	mov	r5, #0
	str	sl, [sp, #8]
	str	r8, [sp, #4]
.L480:
	add	r5, r5, #1
	ldr	r8, [sp, #8]
	uxth	r5, r5
	cmp	r5, r8
	bhi	.L472
	cmp	r0, r1
	beq	.L472
	mov	r9, r1, asl #1
	ldrh	r8, [r3, #4]
	ldrh	sl, [r7, r9]
	cmp	r8, #0
	mvneq	r8, #0
	mulne	r8, r8, sl
	cmp	r8, r6
	bne	.L476
	ldr	r8, [sp, #4]
	ldrh	sl, [r8, r9]
	ldr	r9, [sp, #12]
	ldrh	r8, [r9, #0]
	cmp	sl, r8
	bcc	.L478
	b	.L477
.L476:
	bhi	.L477
.L478:
	ldrh	r8, [r3, #0]
	movw	sl, #65535
	cmp	r8, sl
	streqh	r1, [r2, #2]	@ movhi
	streqh	r0, [r3, #0]	@ movhi
	ldreq	r3, .L486+4
	streq	r2, [r3, #-2012]
	beq	.L472
.L479:
	mov	r1, #6
	mla	r3, r1, r8, fp
	mov	r1, r8
	b	.L480
.L477:
	strh	r1, [r4, ip]	@ movhi
	ldrh	r1, [r3, #2]
	strh	r1, [r2, #2]	@ movhi
	ldr	r1, .L486+4
	ldr	ip, [r1, #-2020]
	cmp	r3, ip
	bne	.L481
	strh	r0, [r3, #2]	@ movhi
.L485:
	str	r2, [r1, #-2020]
	b	.L472
.L481:
	ldrh	r2, [r3, #2]
	mov	ip, #6
	ldr	r1, [r1, #-2024]
	mul	r2, ip, r2
	strh	r0, [r1, r2]	@ movhi
	strh	r0, [r3, #2]	@ movhi
.L472:
	mov	r0, #0
	add	sp, sp, #20
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, sl, fp, pc}
.L487:
	.align	2
.L486:
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
	ldr	r2, .L489
	ldr	r3, .L489+4
	ldrh	r1, [r2, r3]
	add	r1, r1, #1
	strh	r1, [r2, r3]	@ movhi
	ldmfd	sp!, {r3, pc}
.L490:
	.align	2
.L489:
	.word	.LANCHOR2
	.word	-2008
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
	beq	.L492
	ldr	r1, .L499
	mov	r5, #6
	mul	ip, r5, r0
	mvn	r3, #0
	ldr	r4, [r1, #-2024]
	add	r2, r4, ip
	strh	r3, [r2, #2]	@ movhi
	strh	r3, [r4, ip]	@ movhi
	ldr	r3, [r1, #-2004]
	cmp	r3, #0
	beq	.L498
.L493:
	ldr	sl, [r1, #-2024]
	mov	r8, r0, asl #1
	ldr	r9, .L499+4
	rsb	r1, sl, r3
	ldr	r7, .L499+8
	mov	r1, r1, asr #1
	ldr	r7, [r7, #4048]
	mul	r1, r9, r1
	mov	r9, r5
	ldrh	r8, [r7, r8]
	uxth	r1, r1
.L496:
	mov	r5, r1, asl #1
	ldrh	r5, [r7, r5]
	cmp	r5, r8
	bcs	.L494
	ldrh	r5, [r3, #0]
	cmp	r5, r6
	streqh	r1, [r2, #2]	@ movhi
	streqh	r0, [r3, #0]	@ movhi
	beq	.L492
.L495:
	mla	r3, r9, r5, sl
	mov	r1, r5
	b	.L496
.L494:
	ldrh	r5, [r3, #2]
	strh	r5, [r2, #2]	@ movhi
	strh	r1, [r4, ip]	@ movhi
	ldr	r1, .L499
	ldr	ip, [r1, #-2004]
	cmp	r3, ip
	bne	.L497
	strh	r0, [r3, #2]	@ movhi
.L498:
	str	r2, [r1, #-2004]
	b	.L492
.L497:
	ldrh	r2, [r3, #2]
	mov	ip, #6
	ldr	r1, [r1, #-2024]
	mul	r2, ip, r2
	strh	r0, [r1, r2]	@ movhi
	strh	r0, [r3, #2]	@ movhi
.L492:
	mov	r0, #0
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, sl, pc}
.L500:
	.align	2
.L499:
	.word	.LANCHOR2
	.word	-1431655765
	.word	.LANCHOR0
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
	ldr	r2, .L502
	ldr	r3, .L502+4
	ldrh	r1, [r2, r3]
	add	r1, r1, #1
	strh	r1, [r2, r3]	@ movhi
	ldmfd	sp!, {r3, pc}
.L503:
	.align	2
.L502:
	.word	.LANCHOR2
	.word	-2000
	.fnend
	.size	INSERT_FREE_LIST, .-INSERT_FREE_LIST
	.align	2
	.global	List_remove_node
	.type	List_remove_node, %function
List_remove_node:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	ldr	ip, .L509
	mov	r2, #6
	mul	r1, r2, r1
	ldr	r3, [ip, #-2024]
	stmfd	sp!, {r4, r5, r6, lr}
	.save {r4, r5, r6, lr}
	add	r5, r3, r1
	ldr	r4, [r0, #0]
	movw	r6, #65535
	cmp	r5, r4
	ldrh	r4, [r3, r1]
	bne	.L505
	cmp	r4, r6
	mlane	r2, r2, r4, r3
	moveq	r2, #0
	streq	r2, [r0, #0]
	strne	r2, [r0, #0]
	mvnne	r0, #0
	strneh	r0, [r2, #2]	@ movhi
	b	.L507
.L505:
	cmp	r4, r6
	ldrh	r0, [r5, #2]
	bne	.L508
	cmp	r0, r4
	mulne	r2, r2, r0
	mvnne	r0, #0
	strneh	r0, [r3, r2]	@ movhi
	b	.L507
.L508:
	mla	r4, r2, r4, r3
	strh	r0, [r4, #2]	@ movhi
	ldrh	r6, [r5, #2]
	ldrh	r4, [r3, r1]
	ldr	r0, [ip, #-2024]
	mul	r2, r2, r6
	strh	r4, [r0, r2]	@ movhi
.L507:
	mvn	r2, #0
	mov	r0, #0
	strh	r2, [r3, r1]	@ movhi
	strh	r2, [r5, #2]	@ movhi
	ldmfd	sp!, {r4, r5, r6, pc}
.L510:
	.align	2
.L509:
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
	beq	.L516
	ldr	r2, .L517
	movw	lr, #65535
	mov	ip, #6
	ldr	r4, [r2, #-2024]
	b	.L513
.L515:
	mla	r3, ip, r2, r4
	sub	r1, r1, #1
	uxth	r1, r1
.L513:
	cmp	r1, #0
	beq	.L514
	ldrh	r2, [r3, #0]
	cmp	r2, lr
	bne	.L515
.L514:
	rsb	r4, r4, r3
	ldr	r3, .L517+4
	mov	r4, r4, asr #1
	mul	r4, r3, r4
	uxth	r4, r4
	mov	r1, r4
	bl	List_remove_node
	mov	r0, r4
	ldmfd	sp!, {r4, pc}
.L516:
	movw	r0, #65535
	ldmfd	sp!, {r4, pc}
.L518:
	.align	2
.L517:
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
	ldr	r2, .L525
	ldr	r3, [r2, #-2020]
	cmp	r3, #0
	beq	.L524
	ldr	r2, [r2, #-2024]
	movw	r1, #65535
	mov	ip, #6
	b	.L521
.L523:
	mla	r3, ip, r3, r2
	sub	r0, r0, #1
	uxth	r0, r0
.L521:
	cmp	r0, #0
	beq	.L522
	ldrh	r3, [r3, #0]
	cmp	r3, r1
	bne	.L523
	mov	r0, r1
	bx	lr
.L522:
	rsb	r3, r2, r3
	ldr	r0, .L525+4
	mov	r3, r3, asr #1
	mul	r0, r0, r3
	uxth	r0, r0
	bx	lr
.L524:
	movw	r0, #65535
	bx	lr
.L526:
	.align	2
.L525:
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
	ldr	r3, .L534
	ldr	r2, .L534+4
	ldrh	r2, [r3, r2]
	cmp	r2, r0
	beq	.L528
	ldr	r2, .L534+8
	ldrh	r2, [r3, r2]
	cmp	r2, r0
	beq	.L528
	ldr	r2, .L534+12
	ldrh	r2, [r3, r2]
	cmp	r2, r0
	beq	.L528
	mov	r5, #6
	ldr	r2, [r3, #-2024]
	mul	r5, r5, r0
	ldr	r0, [r3, #-2020]
	add	r1, r2, r5
	cmp	r1, r0
	beq	.L528
	ldr	ip, [r3, #-2016]
	mov	r3, r4, asl #1
	ldrh	r0, [ip, r3]
	ldrh	r3, [r1, #4]
	cmp	r3, #0
	mulne	r0, r3, r0
	ldrh	r3, [r1, #2]
	mvneq	r0, #0
	movw	r1, #65535
	cmp	r3, r1
	bne	.L530
	ldrh	r1, [r2, r5]
	cmp	r1, r3
	beq	.L528
.L530:
	mov	r1, #6
	mla	r3, r1, r3, r2
	ldr	r1, .L534+16
	rsb	r2, r2, r3
	mov	r2, r2, asr #1
	ldrh	r3, [r3, #4]
	mul	r2, r1, r2
	cmp	r3, #0
	mvneq	r3, #0
	uxth	r2, r2
	mov	r2, r2, asl #1
	ldrh	r2, [ip, r2]
	mulne	r3, r3, r2
	cmp	r0, r3
	bcs	.L528
	ldr	r0, .L534+20
	mov	r1, r4
	bl	List_remove_node
	ldr	r5, .L534
	ldr	r3, .L534+24
	mov	r0, r4
	ldrh	r2, [r5, r3]
	sub	r2, r2, #1
	strh	r2, [r5, r3]	@ movhi
	bl	INSERT_DATA_LIST
.L528:
	mov	r0, #0
	ldmfd	sp!, {r3, r4, r5, pc}
.L535:
	.align	2
.L534:
	.word	.LANCHOR2
	.word	-1996
	.word	-1948
	.word	-1900
	.word	-1431655765
	.word	.LANCHOR2-2020
	.word	-2008
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
	b	.L537
.L542:
	mov	r7, r3
	add	r3, r3, #2
	ldrh	r6, [r7, #0]
	cmp	r6, #0
	bne	.L538
	bl	FtlFreeSysBlkQueueOut
	movw	r3, #65533
	sub	r2, r0, #1
	mov	r1, r0
	strh	r0, [r7, #0]	@ movhi
	uxth	r2, r2
	cmp	r2, r3
	bls	.L539
	ldr	r2, .L543
	movw	r3, #4062
	ldr	r0, .L543+4
	ldrh	r2, [r2, r3]
	bl	printk
.L540:
	b	.L540
.L539:
	ldr	r3, [r4, #28]
	strh	r6, [r4, #2]	@ movhi
	add	r3, r3, #1
	str	r3, [r4, #28]
	ldrh	r3, [r4, #8]
	strh	r5, [r4, #0]	@ movhi
	add	r3, r3, #1
	strh	r3, [r4, #8]	@ movhi
	b	.L541
.L538:
	add	r5, r5, #1
	uxth	r5, r5
.L537:
	cmp	r5, r2
	bne	.L542
.L541:
	mov	r0, #0
	ldmfd	sp!, {r3, r4, r5, r6, r7, pc}
.L544:
	.align	2
.L543:
	.word	.LANCHOR0
	.word	.LC6
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
	movw	r3, #3954
	stmfd	sp!, {r4, r5, r6, r7, lr}
	.save {r4, r5, r6, r7, lr}
	movw	r1, #65535
	ldrh	r3, [r2, r3]
	ldr	r2, .L557+4
	ldr	ip, [r2, #-1852]
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
	.word	-1848
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
	mov	r1, #3856
	str	r3, [r2, #-1844]
	movw	ip, #65535
	ldrh	r4, [r0, r1]
	ldr	r1, [r2, #-2016]
	ldr	r2, .L565+4
	b	.L562
.L564:
	ldrh	r0, [r1], #2
	add	r3, r3, #1
	cmp	r0, ip
	uxth	r3, r3
	ldrne	r5, [r2, #-1844]
	addne	r0, r0, r5
	strne	r0, [r2, #-1844]
.L562:
	cmp	r3, r4
	bne	.L564
	ldmfd	sp!, {r4, r5, pc}
.L566:
	.align	2
.L565:
	.word	-1846
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
	ldr	r3, [r3, #-1840]
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
	ldr	r3, [r3, #-1840]
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
	movw	r3, #3848
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
	movw	r2, #3916
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
	movw	r8, #3848
	strb	r5, [r4, #7]
	mvn	r7, #0
	b	.L586
.L588:
	add	r3, r6, r5
	ldrh	r1, [r4, #0]
	add	sl, r5, #8
	add	r5, r5, #1
	ldrb	r0, [r3, #3874]	@ zero_extendqisi2
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
	ldrh	r2, [r6, r8]
	ldr	r3, .L591
	cmp	r2, r5
	bhi	.L588
	movw	r1, #3916
	ldrb	r2, [r4, #7]	@ zero_extendqisi2
	ldrh	r1, [r3, r1]
	mul	r2, r1, r2
	strh	r2, [r4, #4]	@ movhi
	mov	r2, #0
	strb	r2, [r4, #9]
	ldr	r2, [r3, #3840]
	cmp	r2, #0
	beq	.L589
	ldrh	r1, [r4, #0]
	ldr	r2, [r3, #4048]
	mov	r3, r1, asl #1
	ldrh	r3, [r2, r3]
	cmp	r3, #79
	movls	r3, #1
	strlsb	r3, [r4, #9]
.L589:
	ldr	r3, .L591
	mov	r0, #0
	ldrb	r3, [r3, #852]	@ zero_extendqisi2
	cmp	r3, #0
	movne	r3, #1
	strneb	r3, [r4, #9]
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, sl, pc}
.L592:
	.align	2
.L591:
	.word	.LANCHOR0
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
	movw	sl, #3848
	movw	r8, #3916
	b	.L594
.L596:
	add	r3, r6, r5
	mov	r1, r7
	add	r5, r5, #1
	ldrb	r0, [r3, #3874]	@ zero_extendqisi2
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
	ldr	r3, [r3, #-2024]
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
	ldr	r0, [r3, #-2004]
	cmp	r0, #0
	bxeq	lr
	ldr	r3, [r3, #-2024]
	ldr	r2, .L603+4
	rsb	r0, r3, r0
	ldr	r3, .L603+8
	mov	r0, r0, asr #1
	ldr	r2, [r2, #4048]
	mul	r3, r3, r0
	uxth	r3, r3
	mov	r3, r3, asl #1
	ldrh	r0, [r2, r3]
	bx	lr
.L604:
	.align	2
.L603:
	.word	.LANCHOR2
	.word	.LANCHOR0
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
	ldr	r3, [r2, #-2004]
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
	ldr	r1, [r2, #-2024]
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
	ldr	r2, .L612+12
	mov	r3, r3, asl #1
	ldr	r2, [r2, #4048]
	ldrh	r0, [r2, r3]
	ldmfd	sp!, {r4, r5, pc}
.L611:
	mov	r0, r3
	ldmfd	sp!, {r4, r5, pc}
.L613:
	.align	2
.L612:
	.word	.LANCHOR2
	.word	-2000
	.word	-1431655765
	.word	.LANCHOR0
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
	mov	sl, r0
	ldr	r6, .L624
	add	r4, sl, #12
	.pad #32
	sub	sp, sp, #32
	ldr	r1, .L624+4
	bl	strcpy
	mov	r0, r4
	ldr	r1, .L624+8
	ldr	r2, [r6, #3048]
	bl	sprintf
	ldr	r1, .L624+12
	ldr	r2, [r6, #3932]
	add	r4, r4, r0
	mov	r0, r4
	bl	sprintf
	ldr	r3, .L624+16
	ldr	r3, [r3, #3092]
	cmp	r3, #1
	add	r4, r4, r0
	bne	.L620
.L615:
	add	r0, sp, #16
	add	r1, sp, #20
	add	r2, sp, #24
	add	r3, sp, #28
	bl	NandcGetTimeCfg
	ldr	r3, [sp, #24]
	ldr	r2, [sp, #16]
	mov	r0, r4
	ldr	r1, .L624+20
	str	r3, [sp, #0]
	ldr	r3, [sp, #28]
	ldr	r5, .L624+24
	ldr	r7, .L624+28
	str	r3, [sp, #4]
	ldr	r3, [sp, #20]
	bl	sprintf
	ldr	r1, .L624+32
	ldr	r8, .L624+36
	add	r4, r4, r0
	mov	r0, r4
	add	r4, r4, #10
	bl	strcpy
	ldr	r1, .L624+40
	mov	r0, r4
	ldr	r2, [r6, #3976]
	bl	sprintf
	ldr	r1, .L624+44
	ldr	r2, [r5, #-1844]
	add	r4, r4, r0
	mov	r0, r4
	bl	sprintf
	ldr	r1, .L624+48
	ldr	r2, [r5, #-1836]
	add	r4, r4, r0
	mov	r0, r4
	bl	sprintf
	ldr	r1, .L624+52
	ldr	r2, [r5, #-1832]
	add	r4, r4, r0
	mov	r0, r4
	bl	sprintf
	ldr	r1, .L624+56
	ldr	r2, [r5, #-1828]
	add	r4, r4, r0
	mov	r0, r4
	bl	sprintf
	ldr	r1, .L624+60
	ldr	r2, [r5, #-1824]
	add	r4, r4, r0
	mov	r0, r4
	bl	sprintf
	ldr	r1, .L624+64
	ldr	r2, [r5, #-1820]
	add	r4, r4, r0
	mov	r0, r4
	bl	sprintf
	ldr	r1, .L624+68
	ldr	r2, [r5, #-1816]
	add	r4, r4, r0
	mov	r0, r4
	bl	sprintf
	ldr	r2, [r5, #-1812]
	ldr	r1, .L624+72
	mov	r2, r2, lsr #11
	add	r4, r4, r0
	mov	r0, r4
	bl	sprintf
	ldr	r2, [r5, #-1808]
	ldr	r1, .L624+76
	mov	r2, r2, lsr #11
	add	r4, r4, r0
	mov	r0, r4
	bl	sprintf
	ldr	r1, .L624+80
	ldr	r2, [r5, #-1804]
	add	r4, r4, r0
	mov	r0, r4
	bl	sprintf
	ldr	r1, .L624+84
	ldr	r2, [r5, #-1800]
	add	r4, r4, r0
	mov	r0, r4
	bl	sprintf
	add	r4, r4, r0
	bl	FtlBbtCalcTotleCnt
	movw	r2, #3986
	ldr	r1, .L624+88
	ldrh	r2, [r6, r2]
	mov	r3, r0
	mov	r0, r4
	bl	sprintf
	ldr	r1, .L624+92
	ldrh	r2, [r5, r7]
	add	r4, r4, r0
	mov	r0, r4
	bl	sprintf
	ldr	r1, .L624+96
	ldr	r2, [r5, #-1796]
	add	r4, r4, r0
	mov	r0, r4
	bl	sprintf
	ldr	r1, .L624+100
	ldr	r2, [r5, #-1792]
	add	r4, r4, r0
	mov	r0, r4
	bl	sprintf
	ldr	r1, .L624+104
	ldr	r2, [r5, #-1788]
	add	r4, r4, r0
	mov	r0, r4
	bl	sprintf
	ldr	r1, .L624+108
	ldr	r2, [r6, #4052]
	add	r4, r4, r0
	mov	r0, r4
	bl	sprintf
	ldr	r1, .L624+112
	ldr	r2, [r5, #-1784]
	add	r4, r4, r0
	mov	r0, r4
	bl	sprintf
	ldr	r1, .L624+116
	ldr	r2, [r5, #-1780]
	add	r4, r4, r0
	mov	r0, r4
	bl	sprintf
	ldr	r3, .L624+120
	ldr	r1, .L624+124
	ldrh	r2, [r5, r3]
	add	r4, r4, r0
	mov	r0, r4
	bl	sprintf
	ldr	r3, .L624+128
	ldr	r1, .L624+132
	ldrh	r2, [r5, r3]
	add	r4, r4, r0
	mov	r0, r4
	bl	sprintf
	ldr	r1, .L624+136
	ldr	r2, [r6, #3956]
	add	r4, r4, r0
	mov	r0, r4
	bl	sprintf
	ldr	r1, .L624+140
	ldr	r2, [r6, #3948]
	add	r4, r4, r0
	mov	r0, r4
	bl	sprintf
	ldr	r1, .L624+144
	ldr	r2, [r6, #3844]
	add	r4, r4, r0
	mov	r0, r4
	bl	sprintf
	movw	r3, #4062
	ldrh	r2, [r6, r3]
	ldr	r1, .L624+148
	add	r4, r4, r0
	mov	r0, r4
	bl	sprintf
	mov	r3, #3856
	ldrh	r2, [r6, r3]
	ldr	r1, .L624+152
	add	r4, r4, r0
	mov	r0, r4
	bl	sprintf
	ldr	r3, .L624+156
	ldr	r1, .L624+160
	ldrh	r2, [r5, r3]
	add	r4, r4, r0
	mov	r0, r4
	bl	sprintf
	ldr	r1, .L624+164
	ldr	r2, [r6, #3860]
	add	r4, r4, r0
	mov	r0, r4
	bl	sprintf
	ldr	r3, .L624+168
	ldr	r1, .L624+172
	ldrh	r2, [r5, r3]
	add	r4, r4, r0
	mov	r0, r4
	bl	sprintf
	movw	r3, #3980
	ldrh	r2, [r6, r3]
	ldr	r1, .L624+176
	add	r4, r4, r0
	mov	r0, r4
	bl	sprintf
	ldr	r3, .L624+180
	ldr	r1, .L624+184
	ldrh	r2, [r5, r3]
	add	r4, r4, r0
	mov	r0, r4
	bl	sprintf
	ldr	r1, .L624+188
	ldrb	r2, [r5, #-1990]	@ zero_extendqisi2
	add	r4, r4, r0
	mov	r0, r4
	bl	sprintf
	ldrh	r2, [r5, r8]
	ldr	r1, .L624+192
	add	r4, r4, r0
	mov	r0, r4
	bl	sprintf
	ldr	r1, .L624+196
	ldrb	r2, [r5, #-1988]	@ zero_extendqisi2
	add	r4, r4, r0
	mov	r0, r4
	bl	sprintf
	ldr	r3, .L624+200
	ldr	r1, .L624+204
	ldrh	r2, [r5, r3]
	add	r4, r4, r0
	mov	r0, r4
	bl	sprintf
	ldrh	r3, [r5, r8]
	ldr	r2, [r5, #-2016]
	add	r8, r8, #48
	ldr	r1, .L624+208
	mov	r3, r3, asl #1
	ldrh	r2, [r2, r3]
	add	r4, r4, r0
	mov	r0, r4
	bl	sprintf
	ldr	r3, .L624+212
	ldr	r1, .L624+216
	ldrh	r2, [r5, r3]
	add	r4, r4, r0
	mov	r0, r4
	bl	sprintf
	ldr	r1, .L624+220
	ldrb	r2, [r5, #-1942]	@ zero_extendqisi2
	add	r4, r4, r0
	mov	r0, r4
	bl	sprintf
	ldrh	r2, [r5, r8]
	ldr	r1, .L624+224
	add	r4, r4, r0
	mov	r0, r4
	bl	sprintf
	ldr	r1, .L624+228
	ldrb	r2, [r5, #-1940]	@ zero_extendqisi2
	add	r4, r4, r0
	mov	r0, r4
	bl	sprintf
	ldr	r3, .L624+232
	ldr	r1, .L624+236
	ldrh	r2, [r5, r3]
	add	r4, r4, r0
	mov	r0, r4
	bl	sprintf
	ldrh	r3, [r5, r8]
	ldr	r2, [r5, #-2016]
	add	r8, r8, #188
	ldr	r1, .L624+240
	mov	r3, r3, asl #1
	ldrh	r2, [r2, r3]
	add	r4, r4, r0
	mov	r0, r4
	bl	sprintf
	ldr	r3, .L624+244
	ldr	r1, .L624+248
	ldrh	r2, [r5, r3]
	add	r4, r4, r0
	mov	r0, r4
	bl	sprintf
	ldr	r1, .L624+252
	ldrb	r2, [r5, #-1894]	@ zero_extendqisi2
	add	r4, r4, r0
	mov	r0, r4
	bl	sprintf
	ldr	r3, .L624+256
	ldr	r1, .L624+260
	ldrh	r2, [r5, r3]
	add	r4, r4, r0
	mov	r0, r4
	bl	sprintf
	ldr	r1, .L624+264
	ldrb	r2, [r5, #-1892]	@ zero_extendqisi2
	add	r4, r4, r0
	mov	r0, r4
	bl	sprintf
	ldr	r3, .L624+268
	ldr	r1, .L624+272
	ldrh	r2, [r5, r3]
	add	r4, r4, r0
	mov	r0, r4
	bl	sprintf
	ldr	r3, .L624+276
	ldr	r1, .L624+280
	ldrh	r2, [r5, r3]
	add	r4, r4, r0
	mov	r0, r4
	bl	sprintf
	ldr	r1, .L624+284
	ldrb	r2, [r5, #-1754]	@ zero_extendqisi2
	add	r4, r4, r0
	mov	r0, r4
	bl	sprintf
	ldr	r1, .L624+288
	ldrh	r2, [r5, r8]
	add	r4, r4, r0
	mov	r0, r4
	bl	sprintf
	ldr	r1, .L624+292
	ldrb	r2, [r5, #-1752]	@ zero_extendqisi2
	add	r4, r4, r0
	mov	r0, r4
	bl	sprintf
	ldr	r3, .L624+296
	ldr	r1, .L624+300
	ldrh	r2, [r5, r3]
	add	r4, r4, r0
	mov	r0, r4
	bl	sprintf
	ldr	r1, [r5, #-1624]
	ldr	r3, [r6, #3840]
	ldr	r2, [r5, #-1712]
	str	r1, [sp, #0]
	ldr	r1, [r5, #-1632]
	orr	r2, r3, r2, asl #8
	str	r1, [sp, #4]
	ldr	r1, .L624+304
	ldr	r3, [r5, #-1628]
	add	r4, r4, r0
	mov	r0, r4
	bl	sprintf
	ldr	r1, .L624+308
	ldr	r2, [r5, #-1636]
	add	r4, r4, r0
	mov	r0, r4
	bl	sprintf
	ldr	r1, .L624+312
	ldr	r2, [r5, #-1612]
	add	r4, r4, r0
	mov	r0, r4
	bl	sprintf
	ldr	r3, .L624+316
	ldr	r1, .L624+320
	ldrh	r2, [r5, r3]
	add	r4, r4, r0
	mov	r0, r4
	bl	sprintf
	ldr	r3, .L624+324
	ldr	r1, .L624+328
	ldrh	r2, [r5, r3]
	add	r4, r4, r0
	mov	r0, r4
	bl	sprintf
	ldr	r1, .L624+332
	ldr	r2, [r5, #-1192]
	add	r4, r4, r0
	mov	r0, r4
	bl	sprintf
	ldr	r3, .L624+336
	ldr	r1, .L624+340
	ldrh	r2, [r5, r3]
	add	r4, r4, r0
	mov	r0, r4
	bl	sprintf
	add	r4, r4, r0
	bl	GetFreeBlockMinEraseCount
	ldr	r1, .L624+344
	mov	r2, r0
	mov	r0, r4
	bl	sprintf
	add	r4, r4, r0
	ldrh	r0, [r5, r7]
	bl	GetFreeBlockMaxEraseCount
	ldr	r1, .L624+348
	mov	r2, r0
	mov	r0, r4
	bl	sprintf
	ldrh	r3, [r5, r8]
	movw	r2, #65535
	cmp	r3, r2
	add	r4, r4, r0
	beq	.L617
	ldr	r2, [r5, #-2016]
	mov	r3, r3, asl #1
	mov	r0, r4
	ldr	r1, .L624+352
	ldrh	r2, [r2, r3]
	bl	sprintf
	add	r4, r4, r0
.L617:
	mov	r0, #0
	ldr	r6, .L624+24
	bl	List_get_gc_head_node
	ldr	r8, .L624
	mov	r5, #0
	movw	r9, #65535
	uxth	r3, r0
.L619:
	cmp	r3, r9
	beq	.L618
	ldr	r1, [r6, #-2016]
	mov	r2, r3, asl #1
	mov	r7, #6
	mov	r0, r4
	mul	r7, r7, r3
	ldrh	r1, [r1, r2]
	str	r1, [sp, #0]
	ldr	r1, [r6, #-2024]
	add	r1, r1, r7
	ldrh	r1, [r1, #4]
	str	r1, [sp, #4]
	ldr	r1, [r8, #4048]
	ldrh	r2, [r1, r2]
	ldr	r1, .L624+356
	str	r2, [sp, #8]
	mov	r2, r5
	bl	sprintf
	add	r5, r5, #1
	ldr	r3, [r6, #-2024]
	cmp	r5, #16
	ldrh	r3, [r3, r7]
	add	r4, r4, r0
	bne	.L619
.L618:
	ldr	r6, .L624+24
	mov	r5, #0
	ldr	r8, .L624
	movw	r9, #65535
	ldr	r2, [r6, #-2004]
	ldr	r3, [r6, #-2024]
	rsb	r3, r3, r2
	ldr	r2, .L624+360
	mov	r3, r3, asr #1
	mul	r3, r2, r3
	uxth	r3, r3
.L621:
	cmp	r3, r9
	beq	.L620
	mov	r7, #6
	ldr	r2, [r6, #-2024]
	mul	r7, r7, r3
	mov	r0, r4
	add	r2, r2, r7
	ldrh	r2, [r2, #4]
	str	r2, [sp, #0]
	mov	r2, r3, asl #1
	ldr	r1, [r8, #4048]
	ldrh	r2, [r1, r2]
	ldr	r1, .L624+364
	str	r2, [sp, #4]
	mov	r2, r5
	bl	sprintf
	add	r5, r5, #1
	ldr	r3, [r6, #-2024]
	cmp	r5, #4
	ldrh	r3, [r3, r7]
	add	r4, r4, r0
	bne	.L621
.L620:
	rsb	r0, sl, r4
	add	sp, sp, #32
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, sl, pc}
.L625:
	.align	2
.L624:
	.word	.LANCHOR0
	.word	.LC7
	.word	.LC8
	.word	.LC9
	.word	.LANCHOR1
	.word	.LC10
	.word	.LANCHOR2
	.word	-2000
	.word	.LC11
	.word	-1996
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
	.word	-2042
	.word	.LC32
	.word	-2044
	.word	.LC33
	.word	.LC34
	.word	.LC35
	.word	.LC36
	.word	.LC37
	.word	.LC38
	.word	-1776
	.word	.LC39
	.word	.LC40
	.word	-1772
	.word	.LC41
	.word	.LC42
	.word	-1994
	.word	.LC43
	.word	.LC44
	.word	.LC45
	.word	.LC46
	.word	-1992
	.word	.LC47
	.word	.LC48
	.word	-1946
	.word	.LC49
	.word	.LC50
	.word	.LC51
	.word	.LC52
	.word	-1944
	.word	.LC53
	.word	.LC54
	.word	-1898
	.word	.LC55
	.word	.LC56
	.word	-1900
	.word	.LC57
	.word	.LC58
	.word	-1896
	.word	.LC59
	.word	-1758
	.word	.LC60
	.word	.LC61
	.word	.LC62
	.word	.LC63
	.word	-1756
	.word	.LC64
	.word	.LC65
	.word	.LC66
	.word	.LC67
	.word	-1196
	.word	.LC68
	.word	-1194
	.word	.LC69
	.word	.LC70
	.word	-1188
	.word	.LC71
	.word	.LC72
	.word	.LC73
	.word	.LC74
	.word	.LC75
	.word	-1431655765
	.word	.LC76
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
	.word	.LC77
	.word	.LC78
	.fnend
	.size	ftl_proc_ftl_read, .-ftl_proc_ftl_read
	.align	2
	.global	GetSwlReplaceBlock
	.type	GetSwlReplaceBlock, %function
GetSwlReplaceBlock:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	ldr	r3, .L654
	stmfd	sp!, {r4, r5, r6, r7, r8, r9, sl, fp, lr}
	.save {r4, r5, r6, r7, r8, r9, sl, fp, lr}
	.pad #20
	sub	sp, sp, #20
	ldr	r1, [r3, #-1788]
	ldr	r2, [r3, #-1780]
	cmp	r1, r2
	bcs	.L630
	ldr	r0, .L654+4
	mov	r1, #3856
	mov	r2, #0
	str	r2, [r3, #-1796]
	ldrh	r1, [r0, r1]
	ldr	r0, [r0, #4048]
	b	.L631
.L632:
	ldrh	lr, [r0], #2
	add	r2, r2, #1
	ldr	ip, [r3, #-1796]
	add	ip, lr, ip
	str	ip, [r3, #-1796]
.L631:
	cmp	r2, r1
	ldr	r4, .L654
	bcc	.L632
	ldr	r5, [r4, #-1796]
	mov	r0, r5
	bl	__aeabi_uidiv
	ldr	r2, .L654+4
	movw	r3, #3906
	ldrh	r1, [r2, r3]
	str	r0, [r4, #-1788]
	ldr	r0, [r4, #-1792]
	rsb	r0, r0, r5
	bl	__aeabi_uidiv
	str	r0, [r4, #-1796]
	b	.L633
.L630:
	ldr	r2, [r3, #-1784]
	cmp	r1, r2
	bls	.L633
	add	r2, r2, #1
	mov	ip, #3856
	str	r2, [r3, #-1784]
	mov	r3, #0
	ldr	r2, .L654+4
	b	.L634
.L635:
	ldr	r0, [r2, #4048]
	mov	r1, r3, asl #1
	add	r3, r3, #1
	ldrh	r4, [r0, r1]
	add	r4, r4, #1
	strh	r4, [r0, r1]	@ movhi
.L634:
	ldrh	r1, [r2, ip]
	cmp	r3, r1
	bcc	.L635
.L633:
	ldr	r3, .L654
	ldr	r6, [r3, #-1780]
	ldr	r5, [r3, #-1788]
	add	r2, r6, #256
	cmp	r2, r5
	mov	r2, r3
	bls	.L636
	ldr	r3, [r3, #-1784]
	add	r1, r6, #768
	cmp	r1, r3
	bls	.L636
	ldr	r3, .L654+4
	ldr	r3, [r3, #3840]
	cmp	r3, #0
	beq	.L653
	cmp	r6, #40
	bhi	.L653
.L636:
	ldr	r3, .L654+8
	ldrh	r0, [r2, r3]
	add	r0, r0, r0, asl #1
	ubfx	r0, r0, #2, #16
	bl	GetFreeBlockMaxEraseCount
	add	r3, r6, #64
	cmp	r0, r3
	mov	r8, r0
	bcs	.L638
	cmp	r6, #40
	bhi	.L653
.L638:
	ldr	r1, .L654
	ldr	r3, [r1, #-2020]
	cmp	r3, #0
	beq	.L653
	ldr	r2, .L654+4
	mov	r0, #3856
	movw	r7, #65535
	mov	r4, r7
	mov	fp, r7
	ldrh	sl, [r2, r0]
	ldr	r0, [r1, #-2024]
	ldr	r1, [r2, #4048]
	mov	r2, #0
	b	.L639
.L642:
	add	r2, r2, #1
	uxth	r2, r2
	cmp	r2, sl
	bhi	.L653
	ldrh	ip, [r3, #4]
	cmp	ip, #0
	beq	.L640
	rsb	r3, r0, r3
	ldr	ip, .L654+12
	mov	r3, r3, asr #1
	mul	r3, ip, r3
	uxth	r3, r3
	mov	ip, r3, asl #1
	ldrh	ip, [r1, ip]
	cmp	ip, r6
	bls	.L650
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
.L650:
	mov	r4, r3
.L641:
	movw	r3, #65535
	cmp	r4, r3
	beq	.L637
	mov	r9, r4, asl #1
	ldrh	sl, [r1, r9]
	cmp	sl, r6
	bls	.L643
	bl	GetFreeBlockMinEraseCount
	cmp	r0, r6
	ldrhi	r3, .L654
	strhi	r7, [r3, #-1780]
.L643:
	cmp	sl, r5
	bcs	.L653
	add	r3, sl, #128
	cmp	r8, r3
	ble	.L653
	add	r3, sl, #256
	ldr	r6, .L654
	cmp	r3, r5
	bcc	.L644
	ldr	r3, [r6, #-1784]
	add	sl, sl, #768
	cmp	sl, r3
	bcs	.L653
.L644:
	ldr	r3, [r6, #-2016]
	mov	r1, r4
	ldr	r0, .L654+16
	mov	r2, r5
	ldrh	r3, [r3, r9]
	str	r3, [sp, #0]
	ldr	r3, .L654+4
	ldr	r3, [r3, #4048]
	ldrh	r3, [r3, r9]
	stmib	sp, {r3, r8}
	ldr	r3, [r6, #-1784]
	bl	printk
	mov	r3, #1
	str	r3, [r6, #-1184]
	b	.L637
.L653:
	movw	r4, #65535
.L637:
	mov	r0, r4
	add	sp, sp, #20
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, sl, fp, pc}
.L655:
	.align	2
.L654:
	.word	.LANCHOR2
	.word	.LANCHOR0
	.word	-2000
	.word	-1431655765
	.word	.LC79
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
	beq	.L657
	ldr	r2, .L658
	mov	r3, r0, asl #1
	mov	r1, #0
	ldr	r2, [r2, #-2016]
	strh	r1, [r2, r3]	@ movhi
	bl	INSERT_FREE_LIST
.L657:
	mov	r0, #0
	ldmfd	sp!, {r3, pc}
.L659:
	.align	2
.L658:
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
	ldr	r2, .L668
	mov	r3, #0
	stmfd	sp!, {r4, r5, r6, r7, r8, r9, sl, fp, lr}
	.save {r4, r5, r6, r7, r8, r9, sl, fp, lr}
	movw	sl, #3848
	str	r3, [r2, #-1180]
	mov	r8, #12
	ldr	r0, .L668+4
	mov	r7, #1
	movw	r6, #3926
	movw	fp, #3928
	b	.L661
.L662:
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
.L661:
	ldrh	r1, [r0, sl]
	cmp	r3, r1
	bcc	.L662
	b	.L667
.L664:
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
	b	.L666
.L667:
	ldr	r3, .L668
	mov	r8, #12
	ldr	ip, .L668+4
	mov	r7, #0
	movw	r6, #3926
	movw	r5, #3928
.L666:
	ldr	r2, [r3, #-1160]
	cmp	r1, r2
	bcc	.L664
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, sl, fp, pc}
.L669:
	.align	2
.L668:
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
	ldr	r3, .L677
	stmfd	sp!, {r4, r5, r6, r7, r8, r9, sl, fp, lr}
	.save {r4, r5, r6, r7, r8, r9, sl, fp, lr}
	mov	r5, #36
	ldr	r6, [r3, #-1160]
	mov	fp, #12
	ldr	ip, [r3, #-1176]
	mov	r3, #0
	mov	r4, r3
	b	.L671
.L674:
	mul	sl, fp, r2
	add	r8, ip, sl
	ldr	r9, [ip, sl]
	ldr	sl, [r7, #8]
	cmp	r9, sl
	streq	r4, [r8, #8]
	beq	.L673
.L672:
	add	r2, r2, #1
	uxth	r2, r2
.L676:
	cmp	r2, r6
	bcc	.L674
.L673:
	add	r3, r3, #1
	uxth	r3, r3
.L671:
	cmp	r3, r1
	ldmcsfd	sp!, {r4, r5, r6, r7, r8, r9, sl, fp, pc}
	mla	r7, r5, r3, r0
	mov	r2, #0
	b	.L676
.L678:
	.align	2
.L677:
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
	ldr	r3, .L686
	stmfd	sp!, {r4, r5, r6, r7, r8, sl, lr}
	.save {r4, r5, r6, r7, r8, sl, lr}
	mov	r6, #12
	ldr	r8, [r3, #-1160]
	mov	r5, #1
	ldr	r7, [r3, #-1176]
	mov	r4, #36
	mov	r3, #0
	b	.L680
.L683:
	mla	ip, r6, r2, r7
	ldr	sl, [ip, #8]
	cmp	sl, #0
	bne	.L681
	mla	r2, r4, r3, r0
	ldr	sl, [ip, #0]
	str	r5, [ip, #8]
	str	sl, [r2, #8]
	ldr	ip, [ip, #4]
	str	ip, [r2, #12]
	b	.L682
.L681:
	add	r2, r2, #1
	uxth	r2, r2
	b	.L684
.L685:
	mov	r2, #0
.L684:
	cmp	r2, r8
	bcc	.L683
.L682:
	add	r3, r3, #1
	uxth	r3, r3
.L680:
	cmp	r3, r1
	bcc	.L685
	ldmfd	sp!, {r4, r5, r6, r7, r8, sl, pc}
.L687:
	.align	2
.L686:
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
	ldr	r3, .L693
	ldr	r2, .L693+4
	ldrh	r1, [r3, r2]
	ldr	r2, [r3, #-1152]
	mov	r3, #0
	b	.L689
.L691:
	ldrh	ip, [r2], #2
	cmp	ip, r0
	beq	.L692
	add	r3, r3, #1
	uxth	r3, r3
.L689:
	cmp	r3, r1
	bne	.L691
	mov	r0, #0
	bx	lr
.L692:
	mov	r0, #1
	bx	lr
.L694:
	.align	2
.L693:
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
	ldr	r3, .L699
	ldr	r2, .L699+4
	ldrh	ip, [r3, r2]
	ldr	r2, [r3, #-1152]
	mov	r3, #0
	mov	r1, r2
	b	.L696
.L698:
	ldrh	r7, [r1], #2
	cmp	r7, r0
	beq	.L697
	add	r3, r3, #1
	uxth	r3, r3
.L696:
	cmp	r3, ip
	bne	.L698
	mov	r3, r3, asl #1
	strh	r0, [r2, r3]	@ movhi
	ldr	r2, .L699
	ldr	r3, .L699+4
	ldrh	r1, [r2, r3]
	add	r1, r1, #1
	strh	r1, [r2, r3]	@ movhi
.L697:
	ldr	r3, .L699
	mov	r0, #12
	ldr	r2, .L699+8
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
.L700:
	.align	2
.L699:
	.word	.LANCHOR2
	.word	-1156
	.word	-1144
	.fnend
	.size	FtlGcUpdatePage, .-FtlGcUpdatePage
	.align	2
	.global	FtlGcRefreshOpenBlock
	.type	FtlGcRefreshOpenBlock, %function
FtlGcRefreshOpenBlock:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	stmfd	sp!, {r3, r4, r5, r6, r7, r8, sl, lr}
	.save {r3, r4, r5, r6, r7, r8, sl, lr}
	mov	r4, r0
	mov	r1, r4
	ldr	r0, .L706
	bl	printk
	ldr	r8, .L706+4
	ldr	r3, .L706+8
	ldrh	sl, [r3, r8]
	cmp	sl, r4
	beq	.L702
	ldr	r6, .L706+12
	ldrh	r7, [r3, r6]
	cmp	r7, r4
	beq	.L702
	ldr	ip, .L706+16
	ldrh	r5, [r3, ip]
	cmp	r5, r4
	beq	.L702
	ldr	r1, .L706+20
	ldrh	r0, [r3, r1]
	cmp	r0, r4
	beq	.L702
	movw	r2, #65535
	cmp	sl, r2
	streqh	r4, [r3, r8]	@ movhi
	beq	.L702
	cmp	r7, r2
	streqh	r4, [r3, r6]	@ movhi
	beq	.L702
	cmp	r5, r2
	streqh	r4, [r3, ip]	@ movhi
	beq	.L702
	cmp	r0, r2
	streqh	r4, [r3, r1]	@ movhi
.L702:
	mov	r0, #0
	ldmfd	sp!, {r3, r4, r5, r6, r7, r8, sl, pc}
.L707:
	.align	2
.L706:
	.word	.LC80
	.word	-1142
	.word	.LANCHOR2
	.word	-1140
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
	stmfd	sp!, {r3, r4, r5, r6, r7, r8, sl, lr}
	.save {r3, r4, r5, r6, r7, r8, sl, lr}
	mov	r4, r0
	mov	r1, r4
	ldr	r0, .L719
	bl	printk
	ldr	r8, .L719+4
	ldr	r3, .L719+8
	ldrh	sl, [r3, r8]
	cmp	sl, r4
	beq	.L716
	ldr	r6, .L719+12
	ldrh	r7, [r3, r6]
	cmp	r7, r4
	beq	.L716
	ldr	ip, .L719+16
	ldrh	r5, [r3, ip]
	cmp	r5, r4
	beq	.L716
	ldr	r1, .L719+20
	ldrh	r0, [r3, r1]
	cmp	r0, r4
	beq	.L717
	movw	r2, #65535
	cmp	sl, r2
	streqh	r4, [r3, r8]	@ movhi
	beq	.L716
	cmp	r7, r2
	streqh	r4, [r3, r6]	@ movhi
	beq	.L716
	cmp	r5, r2
	streqh	r4, [r3, ip]	@ movhi
	beq	.L716
	cmp	r0, r2
	streqh	r4, [r3, r1]	@ movhi
	mvnne	r0, #0
	ldmnefd	sp!, {r3, r4, r5, r6, r7, r8, sl, pc}
.L716:
	mov	r0, #0
	ldmfd	sp!, {r3, r4, r5, r6, r7, r8, sl, pc}
.L717:
	mov	r0, #0
	ldmfd	sp!, {r3, r4, r5, r6, r7, r8, sl, pc}
.L720:
	.align	2
.L719:
	.word	.LC80
	.word	-1142
	.word	.LANCHOR2
	.word	-1140
	.word	-1138
	.word	-1136
	.fnend
	.size	FtlGcRefreshBlock, .-FtlGcRefreshBlock
	.section	.text.unlikely,"ax",%progbits
	.align	2
	.type	decrement_vpc_count.part.18, %function
decrement_vpc_count.part.18:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	stmfd	sp!, {r4, r5, r6, lr}
	.save {r4, r5, r6, lr}
	mov	r6, r0, asl #1
	ldr	r5, .L723
	mov	r4, r0
	mov	r1, r4
	ldr	r0, .L723+4
	ldr	r3, [r5, #-2016]
	ldrh	r2, [r3, r6]
	bl	printk
	ldr	r3, [r5, #-2016]
	mov	r2, #32
	ldr	r0, .L723+8
	mov	r1, r4
	strh	r2, [r3, r6]	@ movhi
	bl	test_node_in_list
	cmp	r0, #0
	beq	.L722
	mov	r1, r4
	ldr	r0, .L723+8
	bl	List_remove_node
	ldr	r3, .L723+12
	mov	r0, r4
	ldrh	r2, [r5, r3]
	sub	r2, r2, #1
	strh	r2, [r5, r3]	@ movhi
	bl	INSERT_DATA_LIST
	ldr	r3, [r5, #-2016]
	ldr	r0, .L723+16
	mov	r1, r4
	ldrh	r2, [r3, r6]
	bl	printk
.L722:
	mov	r0, r4
	bl	FtlGcRefreshBlock
	mov	r0, #0
	ldmfd	sp!, {r4, r5, r6, pc}
.L724:
	.align	2
.L723:
	.word	.LANCHOR2
	.word	.LC81
	.word	.LANCHOR2-2004
	.word	-2000
	.word	.LC82
	.fnend
	.size	decrement_vpc_count.part.18, .-decrement_vpc_count.part.18
	.text
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
	ldr	r7, .L730
	mov	r2, r4
	ldr	r6, .L730+4
	ldrh	r1, [r7, r6]
	mov	r5, r0
	ldr	r0, .L730+8
	bl	printk
	mov	r0, r5
	bl	FtlGcRefreshBlock
	ldr	r3, .L730+12
	ldr	r2, [r3, #3840]
	cmp	r2, #0
	beq	.L726
	ldr	r3, [r3, #4048]
	mov	r5, r5, asl #1
	ldrh	r2, [r3, r5]
	cmp	r2, #39
	subhi	r2, r2, #40
	strhih	r2, [r3, r5]	@ movhi
.L726:
	ldrh	r1, [r7, r6]
	mov	r3, #0
	ldr	r2, .L730+16
	b	.L727
.L729:
	ldrh	r0, [r2, #2]!
	cmp	r0, r4
	beq	.L728
	add	r3, r3, #1
	uxth	r3, r3
.L727:
	cmp	r3, r1
	bne	.L729
	cmp	r3, #15
	bhi	.L728
	ldr	r2, .L730
	ldr	r1, .L730+20
	add	r0, r2, r3, asl #1
	add	r3, r3, #1
	strh	r4, [r0, r1]	@ movhi
	sub	r1, r1, #2
	strh	r3, [r2, r1]	@ movhi
.L728:
	mov	r0, #0
	ldmfd	sp!, {r3, r4, r5, r6, r7, pc}
.L731:
	.align	2
.L730:
	.word	.LANCHOR2
	.word	-1134
	.word	.LC83
	.word	.LANCHOR0
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
	ldr	r2, .L735
	ldr	r3, .L735+4
	ldrh	r2, [r3, r2]
	cmp	r2, #0
	beq	.L733
	ldr	r1, .L735+8
	ldrh	r0, [r3, r1]
	movw	r1, #65535
	cmp	r0, r1
	bne	.L733
	ldr	r4, .L735+12
	ldr	r5, .L735+4
	ldrh	r1, [r3, r4]
	cmp	r1, r2
	movcs	r2, #0
	strcsh	r2, [r3, r4]	@ movhi
	ldrh	r2, [r5, r4]
	ldr	r3, .L735+16
	add	r2, r5, r2, asl #1
	ldrh	r0, [r2, r3]
	bl	P2V_block_in_plane
	bl	FtlGcRefreshBlock
	ldrh	r3, [r5, r4]
	add	r3, r3, #1
	strh	r3, [r5, r4]	@ movhi
.L733:
	mov	r0, #0
	ldmfd	sp!, {r3, r4, r5, pc}
.L736:
	.align	2
.L735:
	.word	-1134
	.word	.LANCHOR2
	.word	-1142
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
	beq	.L738
	bl	memset
.L738:
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
	ldr	r4, .L740
	mov	r5, #3920
	ldr	r2, .L740+4
	mov	r1, #255
	ldr	r6, .L740+8
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
.L741:
	.align	2
.L740:
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
	@ args = 0, pretend = 0, frame = 8
	@ frame_needed = 0, uses_anonymous_args = 0
	stmfd	sp!, {r0, r1, r2, r4, r5, r6, r7, r8, r9, sl, fp, lr}
	.save {r0, r1, r2, r4, r5, r6, r7, r8, r9, sl, fp, lr}
	movw	r3, #3858
	ldr	r7, .L752
	mov	r2, #6
	ldr	r4, .L752+4
	mov	r1, #0
	mov	r6, #0
	ldrh	r3, [r7, r3]
	mov	sl, r6
	ldr	r0, [r4, #-2024]
	mul	r2, r2, r3
	bl	ftl_memset
	ldr	r3, .L752+8
	str	r6, [r4, #-2004]
	str	r6, [r4, #-2020]
	strh	r6, [r4, r3]	@ movhi
	add	r3, r3, #8
	str	r6, [r4, #-2012]
	strh	r6, [r4, r3]	@ movhi
	add	r3, r3, #904
	strh	r6, [r4, r3]	@ movhi
	mov	r4, r6
	b	.L743
.L745:
	add	r2, r7, r2
	mov	r1, fp
	add	r9, r9, #1
	ldrb	r0, [r2, #3874]	@ zero_extendqisi2
	str	r3, [sp, #4]
	bl	V2P_block
	bl	FtlBbmIsBadBlock
	ldr	r3, [sp, #4]
	uxth	r9, r9
	cmp	r0, #0
	ldreqh	r2, [r7, r3]
	addeq	r5, r5, r2
	uxtheq	r5, r5
	b	.L750
.L751:
	mov	r5, #0
	uxth	fp, r4
	mov	r9, r5
	movw	r3, #3916
.L750:
	movw	r0, #3848
	sxth	r2, r9
	ldrh	r1, [r7, r0]
	cmp	r2, r1
	blt	.L745
	cmp	r5, #0
	ldr	r9, .L752+4
	beq	.L746
	sxth	r1, r5
	mov	r0, #32768
	bl	__aeabi_idiv
	uxth	r5, r0
	b	.L747
.L746:
	sxth	r3, r4
	ldr	r2, [r9, #-2016]
	mvn	r1, #0
	mov	r3, r3, asl #1
	strh	r1, [r2, r3]	@ movhi
.L747:
	sxth	r1, r4
	ldr	r0, [r9, #-2024]
	ldr	r3, .L752+4
	mov	r2, r1, asl #1
	add	r1, r2, r1
	add	r1, r0, r1, asl #1
	strh	r5, [r1, #4]	@ movhi
	ldr	r1, .L752+12
	ldrh	r1, [r9, r1]
	cmp	r8, r1
	beq	.L748
	ldr	r1, .L752+16
	ldrh	r1, [r3, r1]
	cmp	r8, r1
	beq	.L748
	ldr	r1, .L752+20
	ldrh	r1, [r3, r1]
	cmp	r8, r1
	beq	.L748
	ldr	r3, [r3, #-2016]
	uxth	r0, r4
	ldrh	r3, [r3, r2]
	cmp	r3, #0
	bne	.L749
	add	r6, r6, #1
	uxth	r6, r6
	bl	INSERT_FREE_LIST
	b	.L748
.L749:
	add	sl, sl, #1
	uxth	sl, sl
	bl	INSERT_DATA_LIST
.L748:
	add	r4, r4, #1
	uxth	r4, r4
.L743:
	mov	r3, #3856
	sxth	r8, r4
	ldrh	r3, [r7, r3]
	cmp	r8, r3
	blt	.L751
	ldr	r3, .L752+4
	mov	r0, #0
	ldr	r2, .L752+8
	strh	sl, [r3, r2]	@ movhi
	add	r2, r2, #8
	strh	r6, [r3, r2]	@ movhi
	ldmfd	sp!, {r1, r2, r3, r4, r5, r6, r7, r8, r9, sl, fp, pc}
.L753:
	.align	2
.L752:
	.word	.LANCHOR0
	.word	.LANCHOR2
	.word	-2008
	.word	-1996
	.word	-1948
	.word	-1900
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
	ldr	r4, .L757
	movw	r6, #3926
	ldr	r5, .L757+4
	movw	r7, #3954
	mov	r8, #12
	ldr	r2, [r4, #3944]
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
	b	.L755
.L756:
	mul	r3, r8, r2
	ldr	r1, [r5, #-1852]
	add	sl, r1, r3
	str	ip, [sl, #4]
	strh	r0, [r1, r3]	@ movhi
	ldr	r1, [r5, #-1852]
	ldr	sl, [r5, #-1088]
	add	r3, r1, r3
	ldrh	r1, [r4, r6]
	mul	r1, r2, r1
	add	r2, r2, #1
	uxth	r2, r2
	bic	r1, r1, #3
	add	r1, sl, r1
	str	r1, [r3, #8]
.L755:
	ldrh	r3, [r4, r7]
	ldr	r1, .L757
	cmp	r3, r2
	ldr	r3, .L757+4
	bhi	.L756
	ldr	r0, .L757+8
	mvn	r2, #0
	strh	r2, [r3, r0]	@ movhi
	sub	r0, r0, #2
	strh	r2, [r3, r0]	@ movhi
	ldr	r0, [r1, #3944]
	ldr	r2, .L757+12
	strh	r0, [r3, r2]	@ movhi
	sub	r2, r2, #6
	ldr	r0, .L757+16
	strh	r0, [r3, r2]	@ movhi
	add	r2, r2, #40
	ldrh	r0, [r3, r2]
	sub	r2, r2, #36
	strh	r0, [r3, r2]	@ movhi
	mov	r2, #3952
	ldrh	r1, [r1, r2]
	ldr	r2, .L757+20
	strh	r1, [r3, r2]	@ movhi
	ldr	r2, [r3, #-1036]
	str	r2, [r3, #-1072]
	ldr	r2, [r3, #-1032]
	str	r2, [r3, #-1068]
	ldr	r2, [r3, #-1092]
	str	r2, [r3, #-1064]
	ldr	r2, [r3, #-1028]
	str	r2, [r3, #-1060]
	ldmfd	sp!, {r3, r4, r5, r6, r7, r8, sl, pc}
.L758:
	.align	2
.L757:
	.word	.LANCHOR0
	.word	.LANCHOR2
	.word	-1082
	.word	-1074
	.word	-3902
	.word	-1078
	.fnend
	.size	FtlL2PDataInit, .-FtlL2PDataInit
	.align	2
	.global	ftl_free_no_use_map_blk
	.type	ftl_free_no_use_map_blk, %function
ftl_free_no_use_map_blk:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	stmfd	sp!, {r3, r4, r5, r6, r7, r8, r9, sl, fp, lr}
	.save {r3, r4, r5, r6, r7, r8, r9, sl, fp, lr}
	mov	r1, #0
	ldrh	r2, [r0, #10]
	mov	r4, r0
	ldr	r5, [r0, #20]
	ldr	r6, [r0, #12]
	ldr	r7, [r0, #24]
	mov	r2, r2, asl #1
	mov	r0, r5
	bl	ftl_memset
	mov	r3, #0
	b	.L760
.L764:
	ldr	r0, [r7, r3, asl #2]
	mov	r2, #0
	ubfx	r0, r0, #10, #16
	b	.L761
.L763:
	mov	r1, r2, asl #1
	ldrh	ip, [r6, r1]
	cmp	ip, r0
	bne	.L762
	cmp	r0, #0
	ldrneh	ip, [r5, r1]
	addne	ip, ip, #1
	strneh	ip, [r5, r1]	@ movhi
.L762:
	add	r2, r2, #1
	uxth	r2, r2
.L761:
	ldrh	r1, [r4, #10]
	cmp	r1, r2
	bhi	.L763
	add	r3, r3, #1
	uxth	r3, r3
.L760:
	ldrh	r2, [r4, #6]
	cmp	r2, r3
	bhi	.L764
	ldr	r2, .L770
	movw	r3, #3918
	mov	r8, #0
	mov	r7, r8
	ldrh	r2, [r2, r3]
	ldrh	r3, [r4, #0]
	mov	r3, r3, asl #1
	strh	r2, [r5, r3]	@ movhi
	ldrh	fp, [r5, #0]
	b	.L765
.L768:
	mov	sl, r7, asl #1
	ldrh	r9, [r5, sl]
	cmp	fp, r9
	bls	.L766
	ldrh	r3, [r6, sl]
	cmp	r3, #0
	movne	r8, r7
	movne	fp, r9
.L766:
	cmp	r9, #0
	bne	.L767
	ldrh	r0, [r6, sl]
	cmp	r0, #0
	beq	.L767
	mov	r1, #1
	bl	FtlFreeSysBlkQueueIn
	strh	r9, [r6, sl]	@ movhi
	ldrh	r3, [r4, #8]
	sub	r3, r3, #1
	strh	r3, [r4, #8]	@ movhi
.L767:
	add	r7, r7, #1
	uxth	r7, r7
.L765:
	ldrh	r3, [r4, #10]
	cmp	r3, r7
	bhi	.L768
	mov	r0, r8
	ldmfd	sp!, {r3, r4, r5, r6, r7, r8, r9, sl, fp, pc}
.L771:
	.align	2
.L770:
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
	ldr	r3, .L773
	movw	r2, #4058
	stmfd	sp!, {r4, lr}
	.save {r4, lr}
	mov	r4, #0
	strh	r4, [r3, r2]	@ movhi
	add	r2, r2, #2
	mov	r1, r4
	strh	r4, [r3, r2]	@ movhi
	add	r2, r2, #2
	strh	r4, [r3, r2]	@ movhi
	movw	r2, #4056
	strh	r0, [r3, r2]	@ movhi
	add	r0, r3, #4064
	mov	r2, #2048
	bl	ftl_memset
	mov	r0, r4
	ldmfd	sp!, {r4, pc}
.L774:
	.align	2
.L773:
	.word	.LANCHOR0
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
	ldr	r3, .L776
	movw	r2, #3980
	mvn	r1, #0
	ldr	r0, .L776+4
	strh	r1, [r3, r2]	@ movhi
	add	r2, r2, #6
	mov	r1, #0
	strh	r1, [r3, r2]	@ movhi
	mov	r1, #255
	mov	r2, #16
	b	ftl_memset
.L777:
	.align	2
.L776:
	.word	.LANCHOR0
	.word	.LANCHOR0+3992
	.fnend
	.size	FtlBbtMemInit, .-FtlBbtMemInit
	.align	2
	.global	FtlBbt2Bitmap
	.type	FtlBbt2Bitmap, %function
FtlBbt2Bitmap:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	ldr	r3, .L782
	ldr	r2, .L782+4
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
.L780:
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
	bne	.L780
	ldmfd	sp!, {r4, r5, r6, pc}
.L783:
	.align	2
.L782:
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
	ldr	r6, .L785
	mov	r4, #0
	ldr	r2, .L785+4
	mov	r1, r4
	ldr	r5, .L785+8
	movw	r7, #3858
	str	r3, [r6, #-1004]
	strh	r3, [r6, r2]	@ movhi
	movw	r3, #3962
	strh	r4, [r5, r3]	@ movhi
	mov	r3, #3936
	ldrh	r2, [r5, r3]
	ldr	r0, [r5, #3964]
	str	r4, [r6, #-1020]
	mov	r2, r2, asl #1
	str	r4, [r6, #-1012]
	str	r4, [r6, #-1008]
	str	r4, [r5, #3840]
	bl	ftl_memset
	ldrh	r2, [r5, r7]
	mov	r1, r4
	ldr	r0, [r5, #4048]
	mov	r2, r2, asl #1
	bl	ftl_memset
	ldrh	r2, [r5, r7]
	mov	r1, r4
	ldr	r0, [r6, #-1000]
	mov	r2, r2, asl #1
	bl	ftl_memset
	mov	r1, r4
	mov	r2, #48
	ldr	r0, .L785+12
	bl	ftl_memset
	mov	r1, r4
	mov	r2, #512
	ldr	r0, .L785+16
	bl	ftl_memset
	bl	FtlGcBufInit
	bl	FtlL2PDataInit
	mov	r0, r4
	ldmfd	sp!, {r3, r4, r5, r6, r7, pc}
.L786:
	.align	2
.L785:
	.word	.LANCHOR2
	.word	-1016
	.word	.LANCHOR0
	.word	.LANCHOR2-2072
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
	mov	r6, #0
	ldr	r4, .L818
	mov	r0, #1024
	ldr	r3, .L818+4
	mov	r7, #12
	ldr	r2, .L818+8
	movw	sl, #3848
	ldr	r5, .L818+12
	mov	r8, #36
	strh	r6, [r4, r3]	@ movhi
	movw	r3, #65535
	str	r3, [r4, #-988]
	mvn	r3, #0
	strh	r3, [r4, r2]	@ movhi
	add	r2, r2, #2
	str	r6, [r4, #-996]
	strh	r3, [r4, r2]	@ movhi
	add	r2, r2, #2
	str	r6, [r4, #-1712]
	strh	r3, [r4, r2]	@ movhi
	add	r2, r2, #2
	str	r6, [r4, #-1804]
	strh	r3, [r4, r2]	@ movhi
	mov	r2, #32
	ldr	r3, .L818+16
	str	r6, [r4, #-1800]
	str	r6, [r4, #-1816]
	strh	r2, [r4, r3]	@ movhi
	add	r3, r3, #2
	mov	r2, #128
	str	r6, [r4, #-1828]
	strh	r2, [r4, r3]	@ movhi
	add	r3, r3, #6
	str	r6, [r4, #-1832]
	strh	r6, [r4, r3]	@ movhi
	add	r3, r3, #54
	str	r6, [r4, #-1824]
	strh	r6, [r4, r3]	@ movhi
	add	r3, r3, #158
	str	r6, [r4, #-1820]
	strh	r6, [r4, r3]	@ movhi
	sub	r3, r3, #122
	str	r6, [r4, #-1836]
	strh	r6, [r4, r3]	@ movhi
	movw	r3, #3922
	ldrh	r1, [r5, r3]
	str	r6, [r4, #-1796]
	str	r6, [r4, #-1792]
	str	r6, [r5, #4052]
	str	r6, [r4, #-1784]
	str	r6, [r4, #-1780]
	str	r6, [r4, #-992]
	str	r6, [r4, #-1184]
	str	r6, [r4, #-984]
	str	r6, [r4, #-1192]
	str	r6, [r4, #-980]
	bl	__aeabi_idiv
	movw	r3, #3848
	ldrh	r3, [r5, r3]
	str	r6, [r5, #3968]
	mov	r6, #3920
	mov	r3, r3, asl #2
	cmp	r0, r3
	str	r0, [r4, #-972]
	ldrh	r0, [r5, r6]
	strhi	r3, [r4, #-972]
	ldr	r4, .L818
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
	str	r0, [r4, #-968]
	mov	r0, r6
	bl	ftl_malloc
	str	r0, [r4, #-964]
	mov	r0, r9
	bl	ftl_malloc
	str	r0, [r4, #-960]
	mov	r0, r6
	bl	ftl_malloc
	str	r0, [r5, #4044]
	mov	r0, r6
	bl	ftl_malloc
	str	r0, [r4, #-1164]
	ldr	r0, [r4, #-972]
	mul	r0, r8, r0
	bl	ftl_malloc
	movw	r8, #3926
	ldrh	r3, [r5, sl]
	ldrh	r6, [r5, r8]
	mov	r3, r3, asl #1
	add	r3, r3, #1
	str	r3, [r4, #-1160]
	str	r0, [r5, #3972]
	mov	r0, r6
	bl	ftl_malloc
	str	r0, [r4, #-956]
	mov	r0, r6
	bl	ftl_malloc
	str	r0, [r4, #-952]
	mov	r0, r6
	bl	ftl_malloc
	str	r0, [r4, #-948]
	ldr	r0, [r4, #-1160]
	mul	r0, r0, r6
	bl	ftl_malloc
	str	r0, [r4, #-1172]
	ldr	r0, [r4, #-972]
	mul	r0, r0, r6
	bl	ftl_malloc
	str	r0, [r4, #-944]
	mov	r0, r6
	bl	ftl_malloc
	str	r0, [r4, #-940]
	mov	r0, r6
	bl	ftl_malloc
	movw	r6, #3928
	str	r0, [r4, #-936]
	ldr	r0, [r4, #-1160]
	mul	r0, r7, r0
	bl	ftl_malloc
	ldrh	r3, [r5, r6]
	ldrh	sl, [r5, sl]
	mul	sl, sl, r3
	str	r0, [r4, #-1176]
	mov	r0, sl
	bl	ftl_malloc
	str	r0, [r4, #-932]
	mov	r0, sl, asl #3
	bl	ftl_malloc
	ldrh	r3, [r5, r6]
	ldr	sl, .L818+20
	str	r0, [r4, #-928]
	ldr	r0, [r4, #-1160]
	mul	r0, r0, r3
	bl	ftl_malloc
	ldrh	r3, [r5, r6]
	movw	r6, #3858
	str	r0, [r4, #-1168]
	ldr	r0, [r4, #-972]
	mul	r0, r0, r3
	bl	ftl_malloc
	str	r0, [r4, #-924]
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
	str	r0, [r4, #-916]
	add	r0, r0, #32
	str	r0, [r5, #4048]
	mov	r0, sl
	bl	ftl_malloc
	str	r0, [r4, #-912]
	mov	r0, sl
	bl	ftl_malloc
	ldr	sl, [r5, #3944]
	mov	sl, sl, asl #1
	str	r0, [r4, #-2016]
	mov	r0, sl
	bl	ftl_malloc
	str	r0, [r4, #-1036]
	mov	r0, sl
	bl	ftl_malloc
	mov	sl, #3936
	str	r0, [r4, #-1092]
	ldrh	r0, [r5, r6]
	mov	r0, r0, lsr #3
	add	r0, r0, #4
	bl	ftl_malloc
	str	r0, [r4, #-1840]
	ldrh	r0, [r5, sl]
	mov	r0, r0, asl #1
	bl	ftl_malloc
	str	r0, [r5, #3964]
	ldrh	r0, [r5, sl]
	mov	r0, r0, asl #1
	bl	ftl_malloc
	str	r0, [r4, #-908]
	ldrh	r0, [r5, sl]
	add	sl, sl, #2
	mov	r0, r0, asl #2
	bl	ftl_malloc
	str	r0, [r4, #-904]
	ldrh	r0, [r5, sl]
	mov	r0, r0, asl #2
	bl	ftl_malloc
	ldrh	r2, [r5, sl]
	mov	r1, #0
	mov	r2, r2, asl #2
	str	r0, [r4, #-900]
	bl	ftl_memset
	mov	r3, #3952
	ldrh	sl, [r5, r3]
	mov	sl, sl, asl #2
	mov	r0, sl
	bl	ftl_malloc
	str	r0, [r4, #-1028]
	mov	r0, sl
	bl	ftl_malloc
	movw	sl, #3954
	str	r0, [r4, #-896]
	ldr	r0, [r5, #3944]
	mov	r0, r0, asl #2
	bl	ftl_malloc
	str	r0, [r4, #-1032]
	ldrh	r0, [r5, sl]
	mul	r0, r7, r0
	bl	ftl_malloc
	ldrh	r3, [r5, sl]
	movw	r7, #3870
	str	r0, [r4, #-1852]
	ldrh	r0, [r5, r8]
	mul	r0, r0, r3
	bl	ftl_malloc
	ldrh	r3, [r5, r6]
	ldr	r6, .L818+24
	str	r0, [r4, #-1088]
	mov	r0, #6
	mul	r0, r0, r3
	bl	ftl_malloc
	movw	r3, #3912
	ldrh	r3, [r5, r3]
	ldrh	r2, [r5, r7]
	add	r3, r3, #31
	mov	r3, r3, lsr #5
	strh	r3, [r4, r6]	@ movhi
	mul	r3, r2, r3
	str	r0, [r4, #-2024]
	mov	r0, r3, asl #2
	bl	ftl_malloc
	ldrh	r1, [r4, r6]
	mov	r3, #1
	ldr	ip, .L818+12
	mov	r1, r1, asl #2
	mov	r2, r1
	str	r0, [r5, #4008]
	ldrh	r5, [r5, r7]
	ldr	r0, .L818+28
	b	.L789
.L790:
	ldr	r4, [ip, #4008]
	add	r3, r3, #1
	add	r4, r4, r2
	add	r2, r2, r1
	str	r4, [r0, #4]!
.L789:
	cmp	r3, r5
	bcc	.L790
	ldr	r0, .L818+32
	mov	r2, #0
	mov	r1, r2
	add	r0, r0, r3, asl #2
	b	.L791
.L792:
	add	ip, r0, r2
	add	r3, r3, #1
	add	r2, r2, #4
	str	r1, [ip, #28]
.L791:
	cmp	r3, #7
	bls	.L792
	ldr	r3, .L818
	ldr	r2, [r3, #-1036]
	cmp	r2, #0
	beq	.L817
.L793:
	ldr	r2, [r3, #-1092]
	cmp	r2, #0
	beq	.L817
.L795:
	ldr	r2, [r3, #-1028]
	cmp	r2, #0
	beq	.L817
.L796:
	ldr	r2, [r3, #-1032]
	cmp	r2, #0
	beq	.L817
.L797:
	ldr	r2, [r3, #-1852]
	cmp	r2, #0
	beq	.L817
.L798:
	ldr	r2, [r3, #-1088]
	cmp	r2, #0
	beq	.L817
.L799:
	ldr	r2, [r3, #-2024]
	cmp	r2, #0
	beq	.L817
.L800:
	ldr	r2, .L818+12
	ldr	r2, [r2, #4008]
	cmp	r2, #0
	beq	.L817
.L801:
	ldr	r3, [r3, #-2016]
	cmp	r3, #0
	beq	.L817
.L802:
	ldr	r3, .L818
	ldr	r2, [r3, #-1152]
	cmp	r2, #0
	beq	.L817
	ldr	r2, [r3, #-1148]
	cmp	r2, #0
	beq	.L817
.L804:
	ldr	r2, [r3, #-968]
	cmp	r2, #0
	beq	.L817
	ldr	r2, [r3, #-960]
	cmp	r2, #0
	beq	.L817
	ldr	r2, .L818+12
	ldr	r2, [r2, #4044]
	cmp	r2, #0
	beq	.L817
	ldr	r2, [r3, #-1164]
	cmp	r2, #0
	beq	.L817
	ldr	r2, [r3, #-964]
	cmp	r2, #0
	beq	.L817
.L806:
	ldr	r2, [r3, #-956]
	cmp	r2, #0
	beq	.L817
	ldr	r2, [r3, #-952]
	cmp	r2, #0
	beq	.L817
	ldr	r3, [r3, #-948]
	cmp	r3, #0
	beq	.L817
	ldr	r3, .L818
	ldr	r2, [r3, #-1172]
	cmp	r2, #0
	beq	.L817
	ldr	r2, [r3, #-940]
	cmp	r2, #0
	beq	.L817
	ldr	r2, [r3, #-936]
	cmp	r2, #0
	beq	.L817
	ldr	r2, [r3, #-1176]
	cmp	r2, #0
	beq	.L817
.L808:
	ldr	r2, [r3, #-932]
	cmp	r2, #0
	beq	.L817
	ldr	r2, [r3, #-928]
	cmp	r2, #0
	beq	.L817
	ldr	r2, [r3, #-1168]
	cmp	r2, #0
	beq	.L817
.L810:
	ldr	r2, .L818+12
	ldr	r1, [r2, #4048]
	cmp	r1, #0
	beq	.L817
	ldr	r3, [r3, #-1000]
	cmp	r3, #0
	beq	.L817
.L812:
	ldr	r3, [r2, #3964]
	cmp	r3, #0
	beq	.L817
.L813:
	ldr	r3, .L818
	ldr	r2, [r3, #-908]
	cmp	r2, #0
	beq	.L817
.L814:
	ldr	r2, [r3, #-904]
	cmp	r2, #0
	beq	.L817
.L815:
	ldr	r3, [r3, #-900]
	cmp	r3, #0
	bne	.L816
.L817:
	ldr	r0, .L818+36
	ldr	r1, .L818+40
	bl	printk
	mvn	r0, #0
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, sl, pc}
.L816:
	mov	r0, #0
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, sl, pc}
.L819:
	.align	2
.L818:
	.word	.LANCHOR2
	.word	-1096
	.word	-1142
	.word	.LANCHOR0
	.word	-1196
	.word	-920
	.word	-1024
	.word	.LANCHOR0+4008
	.word	.LANCHOR0+3980
	.word	.LC84
	.word	.LANCHOR3
	.fnend
	.size	FtlMemInit, .-FtlMemInit
	.align	2
	.global	ftl_read_flash_info
	.type	ftl_read_flash_info, %function
ftl_read_flash_info:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	stmfd	sp!, {r3, r4, r5, lr}
	.save {r3, r4, r5, lr}
	mov	r1, #0
	mov	r2, #11
	mov	r4, r0
	bl	ftl_memset
	ldr	r3, .L823
	ldr	r2, [r3, #3624]
	ldr	r1, [r3, #856]
	ldrb	r2, [r2, #9]	@ zero_extendqisi2
	mul	r2, r1, r2
	mov	r1, #0
	uxth	r2, r2
	strb	r2, [r4, #4]
	mov	r2, r2, lsr #8
	strb	r2, [r4, #5]
	ldrb	r2, [r3, #3836]	@ zero_extendqisi2
	strb	r2, [r4, #7]
	ldr	r2, [r3, #3956]
	ubfx	r0, r2, #8, #8
	strb	r0, [r4, #1]
	strb	r2, [r4, #0]
	ubfx	r0, r2, #16, #8
	mov	r2, r2, lsr #24
	strb	r0, [r4, #2]
	strb	r2, [r4, #3]
	ldr	r2, [r3, #3624]
	ldrb	r0, [r2, #9]	@ zero_extendqisi2
	strb	r0, [r4, #6]
	mov	r0, #32
	strb	r0, [r4, #8]
	ldrb	r2, [r2, #7]	@ zero_extendqisi2
	ldrb	r0, [r3, #3762]	@ zero_extendqisi2
	mov	r3, r1
	strb	r1, [r4, #10]
	strb	r2, [r4, #9]
	mov	r2, #1
	ldr	r1, .L823+4
	b	.L821
.L822:
	ldrb	r5, [r3, r1]	@ zero_extendqisi2
	add	r3, r3, #1
	ldrb	ip, [r4, #10]	@ zero_extendqisi2
	orr	ip, ip, r2, asl r5
	strb	ip, [r4, #10]
.L821:
	uxtb	ip, r3
	cmp	ip, r0
	bcc	.L822
	ldmfd	sp!, {r3, r4, r5, pc}
.L824:
	.align	2
.L823:
	.word	.LANCHOR0
	.word	.LANCHOR0+3764
	.fnend
	.size	ftl_read_flash_info, .-ftl_read_flash_info
	.align	2
	.global	FlashDieInfoInit
	.type	FlashDieInfoInit, %function
FlashDieInfoInit:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	ldr	r1, .L837
	mov	r2, #0
	stmfd	sp!, {r3, r4, r5, r6, r7, r8, sl, lr}
	.save {r3, r4, r5, r6, r7, r8, sl, lr}
	ldr	r3, .L837+4
	strb	r2, [r1, #-892]
	ldr	r1, .L837+8
	strb	r2, [r3, #3762]
	movw	r2, #2998
	ldrh	r2, [r1, r2]
	cmp	r2, #256
	str	r2, [r3, #856]
	movhi	r2, #512
	bhi	.L836
	cmp	r2, #128
	bls	.L827
	mov	r2, #256
.L836:
	str	r2, [r3, #856]
.L827:
	mov	r1, #0
	mov	r2, #8
	ldr	r0, .L837+12
	mov	r6, #0
	bl	ftl_memset
	mov	r1, #0
	mov	r2, #32
	ldr	r0, .L837+16
	bl	ftl_memset
	ldr	r0, .L837+20
	mov	r1, #0
	mov	r2, #128
	bl	ftl_memset
	ldr	r7, .L837+24
	ldr	r5, .L837+4
	mov	r8, r7
.L829:
	ldr	r4, [r5, #3624]
	mov	r1, r8
	add	r0, r4, #1
	ldrb	r2, [r4, #0]	@ zero_extendqisi2
	bl	FlashMemCmp8
	cmp	r0, #0
	bne	.L828
	ldrb	r3, [r5, #3762]	@ zero_extendqisi2
	add	r2, r5, r3, asl #2
	str	r0, [r2, #3588]
	add	r2, r5, r3
	add	r3, r3, #1
	strb	r3, [r5, #3762]
	strb	r6, [r2, #3764]
.L828:
	add	r6, r6, #1
	add	r8, r8, #8
	cmp	r6, #4
	bne	.L829
	ldr	r5, .L837+4
	ldr	r3, .L837
	ldrb	r2, [r5, #3762]	@ zero_extendqisi2
	strb	r2, [r3, #-892]
	ldrb	r3, [r4, #8]	@ zero_extendqisi2
	cmp	r3, #2
	bne	.L830
	add	sl, r4, #1
	mov	r6, #0
	mov	r8, r5
.L833:
	mov	r0, sl
	mov	r1, r7
	ldrb	r2, [r4, #0]	@ zero_extendqisi2
	bl	FlashMemCmp8
	cmp	r0, #0
	bne	.L831
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
.L831:
	add	r6, r6, #1
	add	r7, r7, #8
	cmp	r6, #4
	bne	.L833
.L830:
	ldr	r3, .L837+4
	ldrb	r1, [r4, #13]	@ zero_extendqisi2
	ldr	r2, .L837
	ldrb	r3, [r3, #3762]	@ zero_extendqisi2
	mul	r1, r1, r3
	ldrh	r3, [r4, #14]
	mul	r1, r3, r1
	ldr	r3, .L837+28
	strh	r1, [r2, r3]	@ movhi
	ldmfd	sp!, {r3, r4, r5, r6, r7, r8, sl, pc}
.L838:
	.align	2
.L837:
	.word	.LANCHOR2
	.word	.LANCHOR0
	.word	.LANCHOR1
	.word	.LANCHOR0+3764
	.word	.LANCHOR0+3588
	.word	.LANCHOR0+3628
	.word	.LANCHOR0+2980
	.word	-890
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
	bne	.L840
	ldr	r3, .L879
.L841:
	strh	r0, [r3, #2]!	@ movhi
	add	r0, r0, #1
	uxth	r0, r0
	cmp	r0, #256
	bne	.L841
	b	.L842
.L840:
	cmp	r0, #1
	bne	.L843
	ldr	r0, .L879+4
	mov	r2, #0
	mov	r3, r2
.L846:
	uxth	r1, r3
	cmp	r1, #3
	bls	.L844
	tst	r3, #1
	moveq	r1, #2
	movne	r1, #3
	rsb	r1, r1, r2
	uxth	r1, r1
.L844:
	add	r3, r3, #1
	strh	r1, [r2, r0]	@ movhi
	cmp	r3, #256
	add	r2, r2, #2
	bne	.L846
	b	.L842
.L843:
	cmp	r0, #2
	bne	.L847
	mov	r3, #0
	ldr	r0, .L879
	movw	r2, #65535
	mov	r1, r3
	b	.L878
.L850:
	cmp	r3, #1
	movls	r1, r3
	movhi	r1, r2
.L878:
	add	r3, r3, #1
	add	r2, r2, #2
	strh	r1, [r0, #2]!	@ movhi
	uxth	r3, r3
	uxth	r2, r2
	cmp	r3, #256
	bne	.L850
	b	.L842
.L847:
	cmp	r0, #3
	bne	.L851
	ldr	r0, .L879+4
	mov	r2, #0
	mov	r3, r2
.L854:
	uxth	r1, r3
	cmp	r1, #5
	bls	.L852
	tst	r3, #1
	moveq	r1, #4
	movne	r1, #5
	rsb	r1, r1, r2
	uxth	r1, r1
.L852:
	add	r3, r3, #1
	strh	r1, [r2, r0]	@ movhi
	cmp	r3, #256
	add	r2, r2, #2
	bne	.L854
	b	.L842
.L851:
	cmp	r0, #4
	mov	r2, #0
	bne	.L855
	ldr	r3, .L879+8
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
	ldr	r0, .L879+12
	strh	r1, [r3, r2]	@ movhi
	mov	r2, #3088
	mov	r1, #7
	strh	r1, [r3, r2]	@ movhi
	add	r2, r2, #2
	mov	r1, #8
	strh	r1, [r3, r2]	@ movhi
	mov	r2, #16
	mov	r3, r1
.L857:
	tst	r3, #1
	add	r3, r3, #1
	moveq	r1, #6
	movne	r1, #7
	cmp	r3, #256
	rsb	r1, r1, r2
	add	r2, r2, #2
	strh	r1, [r0, #2]!	@ movhi
	uxth	r2, r2
	bne	.L857
	b	.L842
.L855:
	cmp	r0, #5
	bne	.L858
	ldr	r1, .L879
	mov	r3, r2
.L859:
	strh	r3, [r1, #2]!	@ movhi
	add	r3, r3, #1
	uxth	r3, r3
	cmp	r3, #16
	bne	.L859
	ldr	r2, .L879+16
.L860:
	strh	r3, [r2, #2]!	@ movhi
	add	r3, r3, #2
	uxth	r3, r3
	cmp	r3, #496
	bne	.L860
	b	.L842
.L858:
	cmp	r0, #6
	bne	.L842
	ldr	r0, .L879
	mov	r3, r2
.L863:
	uxth	r1, r3
	cmp	r1, #5
	bls	.L861
	tst	r3, #1
	moveq	r1, #10
	movne	r1, #12
	rsb	r1, r1, r2
	uxth	r1, r1
.L861:
	add	r3, r3, #1
	add	r2, r2, #3
	cmp	r3, #256
	strh	r1, [r0, #2]!	@ movhi
	uxth	r2, r2
	bne	.L863
.L842:
	mov	r2, #1024
	ldr	r0, .L879+20
	mov	r1, #255
	uxth	r4, r4
	bl	ftl_memset
	ldr	r2, .L879
	mov	r3, #0
	ldr	r0, .L879+24
	b	.L864
.L865:
	ldrh	r1, [r2, #2]!
	add	r3, r3, #1
	uxth	r3, r3
	add	ip, r0, r1, asl #1
	sub	ip, ip, #888
	strh	r1, [ip, #0]	@ movhi
.L864:
	cmp	r3, r4
	bcc	.L865
	ldmfd	sp!, {r4, pc}
.L880:
	.align	2
.L879:
	.word	.LANCHOR0+3074
	.word	.LANCHOR0+3076
	.word	.LANCHOR0
	.word	.LANCHOR0+3090
	.word	.LANCHOR0+3106
	.word	.LANCHOR2-888
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
	.global	ftl_memcpy32
	.type	ftl_memcpy32, %function
ftl_memcpy32:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	mov	r3, #0
	stmfd	sp!, {r4, lr}
	.save {r4, lr}
	mov	ip, r3
	b	.L883
.L884:
	ldr	r4, [r1, r3]
	add	ip, ip, #1
	str	r4, [r0, r3]
	add	r3, r3, #4
.L883:
	cmp	ip, r2
	bne	.L884
	ldmfd	sp!, {r4, pc}
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
	.global	js_hash
	.type	js_hash, %function
js_hash:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	mov	r3, r0
	mov	r2, #0
	stmfd	sp!, {r4, lr}
	.save {r4, lr}
	ldr	r0, .L889
	b	.L887
.L888:
	mov	ip, r0, asl #5
	ldrb	r4, [r3, r2]	@ zero_extendqisi2
	add	ip, ip, r0, lsr #2
	add	r2, r2, #1
	add	ip, ip, r4
	eor	r0, r0, ip
.L887:
	cmp	r2, r1
	bne	.L888
	ldmfd	sp!, {r4, pc}
.L890:
	.align	2
.L889:
	.word	1204201446
	.fnend
	.size	js_hash, .-js_hash
	.align	2
	.global	timer_get_time
	.type	timer_get_time, %function
timer_get_time:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	@ link register save eliminated.
	ldr	r3, .L892
	ldr	r0, [r3, #0]
	b	jiffies_to_msecs
.L893:
	.align	2
.L892:
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
	stmfd	sp!, {r4, lr}
	.save {r4, lr}
	cmp	r2, #0
	ldr	r4, .L897
	mov	ip, r0
	mov	r2, r3
	ldr	r4, [r4, #136]
	add	r4, r4, #4096
	add	r1, r4, r1
	movne	r0, r1
	movne	r1, ip
	ldmfd	sp!, {r4, lr}
	b	memcpy
.L898:
	.align	2
.L897:
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
	ldr	r3, .L903
	ldr	r0, [r3, #3956]
	bx	lr
.L904:
	.align	2
.L903:
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
	stmfd	sp!, {r3, r4, r5, r6, r7, r8, sl, lr}
	.save {r3, r4, r5, r6, r7, r8, sl, lr}
	mov	r5, #0
	mov	r8, r0
	mov	r6, r1
	mov	r7, r2
	mov	sl, r3
	mov	r4, r5
	b	.L909
.L915:
	cmp	r5, #0
	bne	.L910
	ldr	r0, .L917
	mov	r1, r8
	mov	r2, r6
	mov	r3, r4
	bl	printk
.L910:
	cmp	r7, #4
	ldreq	r0, .L917+4
	ldreq	r1, [r6, r4, asl #2]
	beq	.L916
	cmp	r7, #2
	moveq	r3, r4, asl #1
	ldreq	r0, .L917+4
	ldreqsh	r1, [r6, r3]
	ldrne	r0, .L917+4
	ldrneb	r1, [r6, r4]	@ zero_extendqisi2
.L916:
	add	r5, r5, #1
	bl	printk
	cmp	r5, #15
	bls	.L914
	ldr	r0, .L917+8
	mov	r5, #0
	ldr	r1, .L917+12
	bl	printk
.L914:
	add	r4, r4, #1
.L909:
	cmp	r4, sl
	bne	.L915
	ldr	r0, .L917+8
	ldr	r1, .L917+12
	ldmfd	sp!, {r3, r4, r5, r6, r7, r8, sl, lr}
	b	printk
.L918:
	.align	2
.L917:
	.word	.LC85
	.word	.LC86
	.word	.LC77
	.word	.LC87
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
	ldr	r6, .L942
	add	r0, r6, r0, asl #3
	ldr	r3, [r6, #3788]
	ldr	r4, [r0, #864]
	cmp	r3, #3
	bls	.L936
	ldr	r3, [r4, #16]
	tst	r3, #4
	beq	.L936
	mov	r0, r4
	bl	wait_for_nandc_xfer_completed
	ldr	r5, [r4, #16]
	ldr	r3, [r4, #8]
	ands	r5, r5, #2
	str	r3, [sp, #0]
	movne	r5, #0
	bne	.L940
	b	.L941
.L926:
	ldr	r3, [r6, #3788]
	cmp	r3, #5
	bls	.L923
	ldr	r3, [r4, #0]
	str	r3, [sp, #4]
	ldr	r3, [sp, #4]
	tst	r3, #8192
	beq	.L923
	ldr	r3, [sp, #4]
	tst	r3, #131072
	bne	.L924
.L923:
	add	r5, r5, #1
	bic	r3, r5, #-16777216
	cmp	r3, #0
	bne	.L940
	ldr	r2, [r4, #28]
	mov	r1, r5
	ldr	r3, [sp, #0]
	ubfx	r2, r2, #16, #5
	ldr	r0, .L942+4
	ubfx	r3, r3, #22, #6
	bl	printk
	ldr	r0, .L942+8
	mov	r1, r4
	mov	r2, #4
	mov	r3, #512
	bl	rknand_print_hex
.L940:
	ldr	r2, [r4, #28]
	ldr	r3, [sp, #0]
	ubfx	r2, r2, #16, #5
	ubfx	r3, r3, #22, #6
	cmp	r2, r3
	blt	.L926
.L924:
	ldr	r4, .L942
	ldr	r3, [r4, #3820]
	cmp	r3, #0
	beq	.L927
	ldr	r1, [sp, #0]
	mov	r2, #0
	ldr	r0, [r4, #3812]
	ubfx	r1, r1, #22, #5
	mov	r1, r1, asl #10
	bl	rknand_dma_unmap_single
	ldr	r1, [sp, #0]
	ldr	r0, [r4, #3816]
	mov	r2, #0
	ubfx	r1, r1, #22, #5
	mov	r1, r1, asl #7
	bl	rknand_dma_unmap_single
	b	.L927
.L930:
	ldr	r3, [r4, #8]
	add	r5, r5, #1
	str	r3, [sp, #0]
	bic	r3, r5, #-16777216
	cmp	r3, #0
	bne	.L941
	ldr	r2, [sp, #0]
	mov	r1, r5
	ldr	r3, [r4, #28]
	ldr	r0, .L942+12
	ubfx	r3, r3, #16, #5
	bl	printk
	ldr	r0, .L942+8
	mov	r1, r4
	mov	r2, #4
	mov	r3, #512
	bl	rknand_print_hex
.L941:
	ldr	r3, [sp, #0]
	tst	r3, #1048576
	beq	.L930
	ldr	r6, .L942
	ldr	r3, [r6, #3828]
	cmp	r3, #0
	beq	.L931
	mov	r0, r4
	bl	NandcSendDumpDataStart
.L931:
	ldr	r3, [r6, #3820]
	ldr	r5, .L942
	cmp	r3, #0
	beq	.L932
	ldr	r1, [sp, #0]
	mov	r2, #1
	ldr	r0, [r5, #3812]
	ubfx	r1, r1, #22, #5
	mov	r1, r1, asl #10
	bl	rknand_dma_unmap_single
	ldr	r1, [sp, #0]
	ldr	r0, [r5, #3816]
	mov	r2, #1
	ubfx	r1, r1, #22, #5
	mov	r1, r1, asl #7
	bl	rknand_dma_unmap_single
.L932:
	ldr	r3, .L942
	ldr	r3, [r3, #3828]
	cmp	r3, #0
	beq	.L927
	mov	r0, r4
	bl	NandcSendDumpDataDone
.L927:
	ldr	r3, .L942
	mov	r2, #0
	str	r2, [r3, #3820]
	b	.L919
.L936:
	ldr	r3, [r4, #8]
	str	r3, [sp, #0]
	ldr	r3, [sp, #0]
	tst	r3, #1048576
	beq	.L936
.L919:
	ldmfd	sp!, {r2, r3, r4, r5, r6, pc}
.L943:
	.align	2
.L942:
	.word	.LANCHOR0
	.word	.LC88
	.word	.LC89
	.word	.LC90
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
	ldr	r3, .L982
	.pad #92
	sub	sp, sp, #92
	tst	r4, #63
	add	r3, r3, r0, asl #3
	mov	r7, r0
	mov	fp, r1
	mov	sl, r2
	ldr	r5, [sp, #128]
	ldr	r6, [r3, #864]
	bne	.L945
	cmp	r5, #0
	bne	.L946
	add	r0, sp, #24
	mov	r1, #255
	mov	r2, #64
	add	r5, sp, #24
	bl	ftl_memset
.L946:
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
	bne	.L947
	ldr	r1, .L982
	mov	r0, sl, lsr #1
	mov	r3, fp
	mov	r2, fp
	ldr	ip, [r1, #3832]
	mov	r4, r1
	cmp	ip, #24
	movhi	ip, #128
	movls	ip, #64
	b	.L949
.L950:
	ldr	r1, [r4, #3800]
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
.L949:
	cmp	r2, r0
	add	r7, r3, ip
	ldr	r1, .L982
	bcc	.L950
	mov	r3, #0
	ldr	r0, [r1, #3832]
	mov	sl, sl, lsr #2
	ldr	r1, [r1, #3788]
	mov	r4, r3
	b	.L951
.L957:
	add	r2, r3, #8
	ldr	r2, [r6, r2, asl #2]
	str	r2, [sp, #20]
	ldr	r2, [sp, #20]
	tst	r2, #4
	bne	.L972
	ldr	r2, [sp, #20]
	ands	r2, r2, #32768
	bne	.L972
	cmp	r1, #5
	bls	.L953
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
	b	.L980
.L953:
	cmp	r1, #3
	bls	.L955
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
.L980:
	orr	r2, ip, r2, asl #5
.L955:
	cmp	r4, r2
	movcc	r4, r2
	b	.L952
.L972:
	mvn	r4, #0
.L952:
	add	r3, r3, #1
.L951:
	cmp	r3, sl
	bcs	.L947
	cmp	r0, #0
	bne	.L957
.L947:
	mov	r3, #0
	str	r3, [r6, #16]
	b	.L958
.L945:
	cmp	r1, #1
	mov	r8, #0
	bne	.L978
	b	.L959
.L962:
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
.L959:
	cmp	r8, sl
	bcc	.L962
	mov	r4, #0
	b	.L958
.L978:
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
	b	.L981
.L967:
	mov	r0, r7
	bl	NandcXferComp
	ldr	r3, [fp, #32]
	add	ip, r5, #2
	cmp	ip, sl
	str	r3, [sp, #20]
	bcs	.L964
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
.L964:
	ldr	r3, [sp, #20]
	tst	r3, #4
	mvnne	r4, #0
	bne	.L965
	ldr	r2, [sp, #20]
	ldr	r3, [sp, #20]
	ubfx	r2, r2, #3, #5
	ubfx	r3, r3, #27, #1
	orr	r3, r2, r3, asl #5
	cmp	r4, r3
	movcc	r4, r3
.L965:
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
.L981:
	cmp	ip, sl
	mov	r5, ip
	bcc	.L967
	mov	r6, fp
	ldr	fp, [sp, #12]
.L958:
	ldr	r3, .L982
	rsbs	fp, fp, #1
	movcc	fp, #0
	ldr	r3, [r3, #3788]
	cmp	r3, #5
	movls	fp, #0
	cmp	fp, #0
	beq	.L968
	ldr	r3, [r6, #0]
	and	r2, r3, #139264
	cmp	r2, #139264
	orreq	r3, r3, #131072
	streq	r3, [r6, #0]
	mvneq	r4, #0
.L968:
	mov	r0, r4
	add	sp, sp, #92
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, sl, fp, pc}
.L983:
	.align	2
.L982:
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
	ldr	r3, .L987
	subs	r4, r0, #0
	mov	r5, r1
	mov	r6, r2
	ldrb	r7, [r3, #2997]	@ zero_extendqisi2
	bne	.L985
	ldr	r3, .L987+4
	ldrb	r2, [r3, #853]	@ zero_extendqisi2
	ldr	r1, [r3, #856]
	mul	r2, r1, r2
	cmp	r5, r2
	bcs	.L985
	ldrb	r3, [r3, #852]	@ zero_extendqisi2
	cmp	r3, #0
	subeq	r7, r7, #2
	movne	r7, #4
.L985:
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
.L988:
	.align	2
.L987:
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
	ldr	r2, .L996
	mov	r7, r1
	mov	r8, r0
	ldrb	r1, [r2, #928]	@ zero_extendqisi2
	ldr	r3, [r2, #3624]
	cmp	r1, #0
	ldrb	r3, [r3, #19]	@ zero_extendqisi2
	beq	.L990
	ldr	r1, [r2, #3776]
	ldr	r2, .L996+4
	cmp	r1, r2
	ldmeqfd	sp!, {r3, r4, r5, r6, r7, r8, sl, pc}
.L990:
	sub	r2, r3, #5
	uxtb	r2, r2
	cmp	r3, #68
	cmpne	r2, #2
	bls	.L993
	cmp	r3, #35
	cmpne	r3, #19
	beq	.L993
	ldmfd	sp!, {r3, r4, r5, r6, r7, r8, sl, pc}
.L994:
	add	r3, r5, r4, asl #1
	sub	r3, r3, #888
	ldrh	r3, [r3, #0]
	cmp	r3, sl
	ldmnefd	sp!, {r3, r4, r5, r6, r7, r8, sl, pc}
	mov	r1, #255
	mov	r2, #32768
	ldr	r0, [r5, #140]
	bl	ftl_memset
	add	r1, r4, r7
	add	r4, r4, #1
	mov	r0, r8
	ldr	r2, [r5, #140]
	mov	r3, #0
	bl	FlashProgPage
	uxth	r4, r4
	b	.L995
.L993:
	ldr	r6, .L996
	movw	sl, #65535
	ldr	r5, .L996+8
.L995:
	ldr	r3, [r6, #3624]
	ldrh	r3, [r3, #10]
	cmp	r3, r4
	bhi	.L994
	ldmfd	sp!, {r3, r4, r5, r6, r7, r8, sl, pc}
.L997:
	.align	2
.L996:
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
	ldr	r3, .L1001
	subs	r4, r0, #0
	mov	r6, r1
	mov	r5, r2
	ldrb	r7, [r3, #2997]	@ zero_extendqisi2
	bne	.L999
	ldr	r3, .L1001+4
	ldrb	r2, [r3, #853]	@ zero_extendqisi2
	ldr	r3, [r3, #856]
	mul	r3, r3, r2
	cmp	r1, r3
	movcc	r7, #4
.L999:
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
.L1002:
	.align	2
.L1001:
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
	ldr	r3, .L1017
	mov	sl, r2
	ldr	fp, .L1017
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
	b	.L1005
.L1010:
	add	r4, r4, #1
	ldrb	r1, [fp, #1]	@ zero_extendqisi2
	mov	r0, r6
	ldr	r2, .L1017+4
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
	beq	.L1007
	ldrb	r3, [ip, #3836]	@ zero_extendqisi2
	cmn	r5, #1
	moveq	r5, r0
	add	r3, r3, r3, asl #1
	cmp	r0, r3, lsr #2
	bcc	.L1016
	mov	r7, #0
	mov	sl, r7
.L1007:
	add	r8, r8, #1
.L1005:
	cmp	r8, r9
	bcc	.L1010
	b	.L1009
.L1016:
	mov	r5, r0
.L1009:
	ldr	r3, .L1017
	add	r6, r3, r6
	ldr	r2, [r3, #3624]
	ldrb	r3, [r3, #3836]	@ zero_extendqisi2
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
.L1018:
	.align	2
.L1017:
	.word	.LANCHOR0
	.word	.LANCHOR0+4
	.fnend
	.size	HynixReadRetrial, .-HynixReadRetrial
	.align	2
	.global	MicronReadRetrial
	.type	MicronReadRetrial, %function
MicronReadRetrial:
	.fnstart
	@ args = 0, pretend = 0, frame = 24
	@ frame_needed = 0, uses_anonymous_args = 0
	stmfd	sp!, {r4, r5, r6, r7, r8, r9, sl, fp, lr}
	.save {r4, r5, r6, r7, r8, r9, sl, fp, lr}
	mov	sl, r3
	ldr	r3, .L1037
	mov	r9, r0
	.pad #36
	sub	sp, sp, #36
	ldrb	r0, [r3, #3836]	@ zero_extendqisi2
	ldrb	r3, [r3, #928]	@ zero_extendqisi2
	str	r1, [sp, #24]
	cmp	r3, #0
	str	r2, [sp, #20]
	addeq	r0, r0, r0, asl #1
	ubfxeq	r0, r0, #2, #8
	beq	.L1036
	mov	r1, #3
	bl	__aeabi_idiv
	uxtb	r0, r0
.L1036:
.L1021:
	mov	r2, #0
	ldr	r3, .L1037
	mvn	r4, #0
	mov	r6, r2
	add	r3, r3, r9, asl #3
	str	r0, [sp, #16]
	str	r2, [sp, #12]
	str	r3, [sp, #28]
.L1031:
	mov	r0, r9
	mov	fp, #0
	bl	NandcWaitFlashReady
	ldr	r2, [sp, #28]
	ldrb	r7, [r2, #868]	@ zero_extendqisi2
	ldr	r8, [r2, #864]
	add	r7, r7, #8
	add	r5, r8, r7, asl #8
	b	.L1022
.L1026:
	mov	r3, #239
	mov	r0, #200
	str	r3, [r5, #8]
	mov	r3, #137
	str	r3, [r5, #4]
	bl	NandcDelayns
	add	ip, fp, #1
	mov	r0, r9
	str	ip, [r5, #0]
	ldr	r1, [sp, #24]
	mov	r3, sl
	str	r6, [r5, #0]
	ldr	r2, [sp, #20]
	str	r6, [r5, #0]
	str	r6, [r5, #0]
	str	ip, [sp, #8]
	bl	FlashReadRawPage
	ldr	ip, [sp, #8]
	cmn	r0, #1
	beq	.L1023
	cmn	r4, #1
	ldr	r2, [sp, #16]
	moveq	r4, r0
	cmp	r0, r2
	bcc	.L1033
	mov	sl, #0
	str	sl, [sp, #20]
.L1023:
	mov	fp, ip
.L1022:
	ldr	r2, .L1037+4
	ldrb	r3, [r2, #144]	@ zero_extendqisi2
	cmp	fp, r3
	bcc	.L1026
	b	.L1025
.L1033:
	mov	r4, r0
	mov	sl, #0
	str	sl, [sp, #20]
.L1025:
	mov	r3, #239
	mov	r0, #200
	str	r3, [r5, #8]
	mov	r3, #137
	str	r3, [r5, #4]
	bl	NandcDelayns
	ldr	r2, [sp, #16]
	str	r6, [r8, r7, asl #8]
	cmp	r4, r2
	str	r6, [r8, r7, asl #8]
	str	r6, [r8, r7, asl #8]
	movcc	r3, #0
	movcs	r3, #1
	cmn	r4, #1
	str	r6, [r8, r7, asl #8]
	moveq	r3, #0
	cmp	r3, #0
	movne	r4, #256
	cmn	r4, #1
	movne	r5, #0
	moveq	r5, #1
	cmn	r4, #1
	cmpne	r4, #256
	bne	.L1028
	mov	r3, fp
	ldr	r0, .L1037+8
	mov	r1, fp
	ldr	r2, [sp, #24]
	str	r4, [sp, #0]
	bl	printk
	ldr	r3, [sp, #12]
	cmp	r3, #0
	bne	.L1029
	ldr	r2, .L1037
	ldrb	r3, [r2, #928]	@ zero_extendqisi2
	cmp	r3, #0
	moveq	r5, #0
	andne	r5, r5, #1
	cmp	r5, #0
	beq	.L1030
	mov	r0, r9
	mov	r1, #3
	bl	micron_auto_read_calibration_config
	mov	r3, #1
	str	r3, [sp, #12]
	b	.L1031
.L1028:
	ldr	r2, [sp, #12]
	cmp	r2, #0
	beq	.L1030
.L1029:
	mov	r0, r9
	mov	r1, #0
	bl	micron_auto_read_calibration_config
	cmn	r4, #1
	movne	r4, #256
.L1030:
	mov	r0, r4
	add	sp, sp, #36
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, sl, fp, pc}
.L1038:
	.align	2
.L1037:
	.word	.LANCHOR0
	.word	.LANCHOR2
	.word	.LC91
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
	ldr	ip, .L1048
	mov	r5, #1
	mvn	r4, #0
	ldr	fp, .L1048+4
	add	r3, ip, r8, asl #3
	ldrb	sl, [r3, #868]	@ zero_extendqisi2
	ldr	r2, [r3, #864]
	add	sl, sl, #8
	add	sl, r2, sl, asl #8
	b	.L1040
.L1044:
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
	beq	.L1041
	ldrb	r3, [ip, #3836]	@ zero_extendqisi2
	cmn	r4, #1
	moveq	r4, r0
	add	r3, r3, r3, asl #1
	cmp	r0, r3, lsr #2
	bcc	.L1047
	mov	r6, #0
	mov	r7, r6
.L1041:
	add	r5, r5, #1
.L1040:
	ldrb	r3, [fp, #144]	@ zero_extendqisi2
	add	r3, r3, #1
	cmp	r5, r3
	bcc	.L1044
	b	.L1043
.L1047:
	mov	r4, r0
.L1043:
	mov	r0, sl
	mov	r1, #0
	bl	SamsungSetRRPara
	ldr	r3, .L1048
	adds	r0, r4, #1
	ldrb	r3, [r3, #3836]	@ zero_extendqisi2
	movne	r0, #1
	add	r3, r3, r3, asl #1
	cmp	r4, r3, lsr #2
	movcc	r0, #0
	andcs	r0, r0, #1
	cmp	r0, #0
	moveq	r0, r4
	movne	r0, #256
	ldmfd	sp!, {r1, r2, r3, r4, r5, r6, r7, r8, r9, sl, fp, pc}
.L1049:
	.align	2
.L1048:
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
	ldr	r3, .L1071
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
	bls	.L1051
	ldrb	sl, [r3, #3780]	@ zero_extendqisi2
	cmp	sl, #0
	beq	.L1052
	mov	r0, #0
	mov	sl, #1
	bl	NandcSetDdrMode
.L1052:
	mov	r3, #92
	str	r3, [r4, #8]
	mov	r3, #197
	str	r3, [r4, #8]
.L1051:
	mov	r6, #1
	mvn	r5, #0
	ldr	fp, .L1071
	b	.L1053
.L1062:
	ldrb	r3, [fp, #3761]	@ zero_extendqisi2
	mov	r0, r4
	uxtb	r1, r6
	sub	r3, r3, #67
	uxtb	r3, r3
	cmp	r3, #1
	bhi	.L1054
	bl	SandiskSetRRPara
	b	.L1055
.L1054:
	bl	ToshibaSetRRPara
.L1055:
	ldr	r2, .L1071
	ldrb	r3, [r2, #3761]	@ zero_extendqisi2
	cmp	r3, #34
	bne	.L1056
	ldr	r2, .L1071+4
	ldrb	r3, [r2, #144]	@ zero_extendqisi2
	sub	r3, r3, #3
	cmp	r6, r3
	moveq	r3, #179
	streq	r3, [r4, #8]
.L1056:
	cmp	sl, #0
	mov	r3, #38
	str	r3, [r4, #8]
	mov	r3, #93
	str	r3, [r4, #8]
	beq	.L1057
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
	b	.L1058
.L1057:
	mov	r3, r7
	mov	r0, r8
	ldr	r1, [sp, #4]
	mov	r2, r9
	bl	FlashReadRawPage
	mov	r3, r0
.L1058:
	cmn	r3, #1
	beq	.L1059
	ldrb	r2, [fp, #3836]	@ zero_extendqisi2
	cmn	r5, #1
	moveq	r5, r3
	add	r2, r2, r2, asl #1
	cmp	r3, r2, lsr #2
	bcc	.L1070
	mov	r7, #0
	mov	r9, r7
.L1059:
	add	r6, r6, #1
.L1053:
	ldr	r2, .L1071+4
	ldrb	r3, [r2, #144]	@ zero_extendqisi2
	add	r3, r3, #1
	cmp	r6, r3
	bcc	.L1062
	b	.L1061
.L1070:
	mov	r5, r3
.L1061:
	ldr	r6, .L1071
	mov	r0, r4
	mov	r1, #0
	ldrb	r3, [r6, #3761]	@ zero_extendqisi2
	sub	r3, r3, #67
	uxtb	r3, r3
	cmp	r3, #1
	bhi	.L1063
	bl	SandiskSetRRPara
	b	.L1064
.L1063:
	bl	ToshibaSetRRPara
.L1064:
	mov	r3, #255
	str	r3, [r4, #8]
	ldrb	r3, [r6, #3836]	@ zero_extendqisi2
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
	beq	.L1066
	mov	r0, #4
	bl	NandcSetDdrMode
.L1066:
	mov	r0, r5
	ldmfd	sp!, {r1, r2, r3, r4, r5, r6, r7, r8, r9, sl, fp, pc}
.L1072:
	.align	2
.L1071:
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
	ldr	r5, .L1080
	ldr	r7, .L1080+4
	ldr	r8, .L1080+8
	ldr	r3, [r5, #152]
	ldrb	r0, [r5, #156]	@ zero_extendqisi2
	str	r3, [r5, #148]
	bl	FlashBchSel
	mov	r1, #0
	mov	r2, #2048
	ldr	r0, [r5, #152]
	bl	ftl_memset
	ldr	r3, [r5, #148]
	ldr	r1, .L1080+12
	mov	r2, #32
	str	r8, [r3, #0]
	ldr	r4, [r5, #148]
	ldrb	r3, [r7, #3762]	@ zero_extendqisi2
	add	r0, r4, #16
	strh	r3, [r4, #12]	@ movhi
	ldrb	r3, [r7, #853]	@ zero_extendqisi2
	strh	r3, [r4, #14]	@ movhi
	ldrb	r3, [r7, #3780]	@ zero_extendqisi2
	str	r3, [r4, #1076]
	bl	memcpy
	add	r0, r4, #80
	ldr	r1, .L1080+16
	mov	r2, #8
	bl	memcpy
	add	r0, r4, #96
	ldr	r1, .L1080+20
	mov	r2, #32
	bl	memcpy
	ldr	r0, [r5, #148]
	ldr	r1, .L1080+24
	mov	r2, #32
	add	r0, r0, #160
	bl	memcpy
	ldr	r4, [r5, #148]
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
	ldr	r3, [r5, #140]
	str	r3, [r5, #148]
	str	r0, [r4, #8]
	mov	r0, #0
	bl	flash_enter_slc_mode
	mov	r4, r6
.L1076:
	ldr	r1, [r7, #856]
	mov	r0, #0
	mov	r2, r0
	mul	r1, r1, r4
	bl	FlashEraseBlock
	ldr	r1, [r7, #856]
	mov	r0, #0
	ldr	r2, [r5, #152]
	mov	r3, r0
	mul	r1, r1, r4
	bl	FlashProgPage
	ldr	r1, [r7, #856]
	mov	r0, #0
	ldr	r2, [r5, #152]
	mov	r3, r0
	mul	r1, r1, r4
	add	r1, r1, #1
	bl	FlashProgPage
	ldr	r1, [r7, #856]
	mov	r0, #0
	ldr	r2, [r5, #140]
	mov	r3, r0
	mul	r1, r1, r4
	bl	FlashReadRawPage
	cmn	r0, #1
	beq	.L1074
	ldr	sl, [r5, #148]
	ldr	r3, [sl, #0]
	cmp	r3, r8
	bne	.L1074
	add	r0, sl, #12
	movw	r1, #2036
	bl	JSHash
	ldr	r3, [sl, #8]
	cmp	r3, r0
	bne	.L1074
	add	r3, r4, #1
	str	r3, [r5, #160]
	ldr	r3, [r7, #856]
	add	r6, r6, #1
	cmp	r6, #1
	mul	r3, r3, r4
	str	r3, [r5, #164]
	bhi	.L1075
.L1074:
	add	r4, r4, #1
	cmp	r4, #4
	bne	.L1076
.L1075:
	mov	r0, #0
	bl	flash_exit_slc_mode
	cmp	r6, #0
	mvneq	r0, #0
	movne	r0, #0
	ldmfd	sp!, {r3, r4, r5, r6, r7, r8, sl, pc}
.L1081:
	.align	2
.L1080:
	.word	.LANCHOR2
	.word	.LANCHOR0
	.word	1312902724
	.word	.LANCHOR0+2980
	.word	.LANCHOR0+3764
	.word	.LANCHOR0+3588
	.word	.LANCHOR1+2988
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
	ldr	r1, .L1093
	add	r0, sp, #12
	bl	memcpy
	ldr	r3, .L1093+4
	ldrb	r2, [r3, #3836]	@ zero_extendqisi2
	str	r2, [sp, #4]
	ldr	r2, [r3, #3776]
	ldr	r3, .L1093+8
	cmp	r2, r3
	bne	.L1083
	mov	r0, #0
	bl	flash_enter_slc_mode
.L1083:
	ldr	r7, .L1093+4
	mov	r0, sl
	mov	r1, #0
	mov	r2, #2048
	bl	ftl_memset
	mvn	r8, #0
	mov	r4, #2
	ldr	r9, .L1093+12
	mov	fp, r7
	b	.L1084
.L1091:
	mov	r6, #0
.L1086:
	add	r3, sp, #12
	ldr	r5, .L1093+12
	ldrb	r0, [r3, r6]	@ zero_extendqisi2
	bl	FlashBchSel
	ldr	r1, [r7, #856]
	mov	r0, #0
	ldr	r2, [r9, #152]
	mov	r3, r0
	mul	r1, r1, r4
	bl	FlashReadRawPage
	cmn	r0, #1
	bne	.L1085
	add	r6, r6, #1
	cmp	r6, #4
	bne	.L1086
	b	.L1087
.L1085:
	ldr	r3, [r5, #152]
	ldr	r2, [r3, #0]
	ldr	r3, .L1093+16
	cmp	r2, r3
	bne	.L1087
	add	r3, sp, #16
	ldr	r0, .L1093+20
	add	r6, r3, r6
	ldrb	r1, [r6, #-4]	@ zero_extendqisi2
	bl	printk
	mov	r0, sl
	ldr	r1, [r5, #152]
	mov	r2, #2048
	bl	memcpy
	ldr	r3, [r5, #152]
	ldr	r3, [r3, #512]
	strb	r3, [fp, #853]
	ldr	r3, [r5, #160]
	cmp	r3, r4
	bls	.L1090
	str	r4, [r5, #160]
	bl	FlashSavePhyInfo
	mov	r8, #0
.L1087:
	add	r4, r4, #1
.L1084:
	ldrb	r3, [r7, #853]	@ zero_extendqisi2
	cmp	r4, r3
	bcc	.L1091
	b	.L1088
.L1090:
	mov	r8, #0
.L1088:
	ldr	r0, [sp, #4]
	bl	FlashBchSel
	ldr	r3, .L1093+4
	ldr	r2, [r3, #3776]
	ldr	r3, .L1093+8
	cmp	r2, r3
	bne	.L1089
	mov	r0, #0
	bl	flash_exit_slc_mode
.L1089:
	mov	r0, r8
	add	sp, sp, #20
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, sl, fp, pc}
.L1094:
	.align	2
.L1093:
	.word	.LANCHOR3+11
	.word	.LANCHOR0
	.word	1446522928
	.word	.LANCHOR2
	.word	-52655045
	.word	.LC92
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
	ldr	r1, .L1106
	add	r0, sp, #4
	bl	memcpy
	ldr	r5, .L1106+4
	ldr	r2, .L1106+8
	movw	r3, #2998
	mov	r4, #0
	mov	r8, #4
	mov	r0, r4
	str	r4, [r5, #164]
	ldrh	r9, [r2, r3]
	mvn	r6, #0
	ldr	r3, [r5, #152]
	ldr	fp, .L1106+12
	str	r3, [r5, #148]
	bl	flash_enter_slc_mode
	b	.L1096
.L1098:
	add	r3, sp, #4
	ldrb	r0, [r3, r7]	@ zero_extendqisi2
	bl	FlashBchSel
	mov	r0, #0
	mov	r1, r4
	ldr	r2, [r5, #152]
	mov	r3, r0
	bl	FlashReadRawPage
	cmn	r0, #1
	bne	.L1097
	mov	r0, #0
	mov	r1, sl
	ldr	r2, [r5, #152]
	mov	r3, r0
	bl	FlashReadRawPage
	cmn	r0, #1
	bne	.L1097
	add	r7, r7, #1
	cmp	r7, #4
	beq	.L1099
	b	.L1098
.L1097:
	ldr	r7, [r5, #148]
	ldr	r3, [r7, #0]
	cmp	r3, fp
	bne	.L1099
	cmp	r6, #0
	bne	.L1100
	ldr	r2, .L1106+8
	movw	r3, #2998
	mov	r0, r4
	ldrh	r1, [r2, r3]
	bl	__aeabi_uidiv
	ldr	r3, .L1106+4
	add	r0, r0, #1
	str	r0, [r3, #160]
	mov	r0, r6
	b	.L1105
.L1100:
	add	r0, r7, #12
	movw	r1, #2036
	bl	JSHash
	ldr	r3, [r7, #8]
	cmp	r3, r0
	bne	.L1099
	ldr	r6, .L1106+16
	add	r1, r7, #160
	mov	r2, #32
	ldr	r0, .L1106+20
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
	ldr	r2, .L1106+8
	mov	r0, r4
	str	r4, [r5, #164]
	strb	r3, [r6, #3780]
	movw	r3, #2998
	ldrh	r1, [r2, r3]
	mov	r6, #0
	bl	__aeabi_uidiv
	add	r0, r0, #1
	str	r0, [r5, #160]
	cmp	r0, #1
	moveq	r3, #2
	streq	r3, [r5, #160]
	ldrh	r3, [r7, #14]
	strb	r3, [r5, #168]
.L1099:
	subs	r8, r8, #1
	add	r4, r4, r9
	beq	.L1103
.L1096:
	add	sl, r4, #1
	mov	r7, #0
	b	.L1098
.L1103:
	mov	r0, r8
.L1105:
	bl	flash_exit_slc_mode
	mov	r0, r6
	ldmfd	sp!, {r1, r2, r3, r4, r5, r6, r7, r8, r9, sl, fp, pc}
.L1107:
	.align	2
.L1106:
	.word	.LANCHOR3+11
	.word	.LANCHOR2
	.word	.LANCHOR1
	.word	1312902724
	.word	.LANCHOR0
	.word	.LANCHOR1+2988
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
	ldr	r4, .L1129
	.pad #20
	sub	sp, sp, #20
	mov	fp, r0
	mov	r9, r2
	str	r1, [sp, #0]
	ldr	r3, [r4, #3012]
	ldr	r7, [r4, #3788]
	ldr	r3, [r3, #304]
	cmp	r7, #7
	movhi	r7, #12
	movls	r7, #6
	str	r3, [sp, #12]
	ldr	r3, [sp, #56]
	cmp	r3, #0
	moveq	r8, #1024
	beq	.L1110
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
	beq	.L1111
	ldr	r0, .L1129+4
	mov	r2, r8
	ldr	r1, [sp, #0]
	bl	printk
	cmp	r8, #9
	bhi	.L1112
	add	r4, r4, fp, asl #3
	ldr	r3, [r4, #864]
	ldr	r2, [r3, #3840]
	ldr	r2, [r3, #0]
	orr	r2, r2, #131072
	str	r2, [r3, #0]
.L1112:
	ldr	r3, .L1129+8
	ldr	r2, [r3, #172]
	add	r2, r2, #1
	str	r2, [r3, #172]
	cmp	r2, #2048
	movcs	sl, #0
	strcs	sl, [r3, #172]
	movcs	r9, sl
	bcc	.L1111
.L1110:
	mov	r4, #0
	str	fp, [sp, #4]
	mov	r5, r4
	mvn	r6, #0
	mov	fp, r4
	str	r4, [sp, #8]
.L1117:
	uxtb	r0, r7
	bl	NandcSetDdrPara
	mov	r3, sl
	ldr	r0, [sp, #4]
	mov	r2, r9
	ldr	r1, [sp, #0]
	bl	FlashReadRawPage
	add	r3, r8, #1
	cmp	r0, r3
	bhi	.L1113
	cmp	r0, #2
	bhi	.L1123
	add	r5, r5, #1
	cmp	r5, #9
	bls	.L1123
	mov	ip, fp
	rsb	r4, r5, r7
	ldr	fp, [sp, #4]
	mov	r8, r0
	mov	r6, #0
	b	.L1115
.L1113:
	cmp	fp, r5
	bcs	.L1124
	cmp	r5, #7
	rsb	r3, r5, r4
	str	r3, [sp, #8]
	bhi	.L1128
	mov	fp, r5
	b	.L1124
.L1123:
	mov	r6, #0
	mov	r4, r7
	mov	r8, r0
	mov	sl, r6
	mov	r9, r6
	b	.L1114
.L1124:
	mov	r5, #0
.L1114:
	add	r7, r7, #2
	cmp	r7, #69
	bls	.L1117
	mov	ip, fp
	ldr	fp, [sp, #4]
.L1115:
	cmp	ip, r5
	bcc	.L1118
	b	.L1116
.L1128:
	ldr	fp, [sp, #4]
.L1116:
	ldr	r4, [sp, #8]
.L1118:
	cmp	r4, #0
	beq	.L1119
	ldr	r0, .L1129+12
	mov	r1, r4
	bl	printk
	uxtb	r0, r4
	bl	NandcSetDdrPara
.L1119:
	cmn	r6, #1
	bne	.L1111
	ldr	r0, .L1129+16
	mov	r1, fp
	ldr	r2, [sp, #0]
	bl	printk
	ldr	r3, [sp, #56]
	cmp	r3, #0
	moveq	r8, r6
	beq	.L1111
	ldr	r3, [sp, #12]
	ubfx	r0, r3, #8, #8
	bl	NandcSetDdrPara
.L1111:
	mov	r0, r8
	add	sp, sp, #20
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, sl, fp, pc}
.L1130:
	.align	2
.L1129:
	.word	.LANCHOR0
	.word	.LC93
	.word	.LANCHOR2
	.word	.LC94
	.word	.LC95
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
	ldr	r5, .L1135
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
	beq	.L1132
	cmn	r8, #1
	bne	.L1133
.L1132:
	ldrb	r3, [r5, #3772]	@ zero_extendqisi2
	tst	r3, #1
	beq	.L1133
	mov	r0, #1
	bl	FlashSetInterfaceMode
	mov	r0, #1
	bl	NandcSetMode
	ldr	r3, .L1135
	mov	r2, #0
	strb	r2, [r3, #3780]
	b	.L1134
.L1133:
	mov	r3, #1
	strb	r3, [r5, #3780]
.L1134:
	mov	r0, #0
	ldmfd	sp!, {r2, r3, r4, r5, r6, r7, r8, pc}
.L1136:
	.align	2
.L1135:
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
	bne	.L1138
	ldr	r7, .L1143
	ldrb	r9, [r7, #860]	@ zero_extendqisi2
	cmp	r9, #0
	beq	.L1139
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
	bne	.L1138
.L1139:
	ldr	r7, .L1143
	ldrb	r3, [r7, #3780]	@ zero_extendqisi2
	cmp	r3, #0
	beq	.L1138
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
	beq	.L1140
	ldrb	r3, [r7, #3836]	@ zero_extendqisi2
	cmp	r0, r3, lsr #1
	bls	.L1138
.L1140:
	ubfx	r0, r9, #8, #8
	bl	NandcSetDdrPara
.L1138:
	ldr	r7, .L1143+4
	ldr	ip, [r7, #176]
	adds	r3, ip, #0
	movne	r3, #1
	cmn	r4, #1
	movne	r3, #0
	cmp	r3, #0
	beq	.L1141
	mov	r1, r6
	mov	r2, r8
	mov	r3, sl
	mov	r0, r5
	blx	ip
	mov	r2, r5
	mov	r3, r6
	mov	r4, r0
	ldr	r0, .L1143+8
	mov	r1, r4
	bl	printk
	cmn	r4, #1
	bne	.L1141
	ldr	r3, .L1143
	ldrb	r3, [r3, #928]	@ zero_extendqisi2
	cmp	r3, #0
	beq	.L1141
	mov	r0, r5
	bl	flash_enter_slc_mode
	ldr	ip, [r7, #176]
	mov	r0, r5
	mov	r1, r6
	mov	r2, r8
	mov	r3, sl
	blx	ip
	mov	r4, r0
	mov	r0, r5
	bl	flash_exit_slc_mode
.L1141:
	mov	r0, r4
	ldmfd	sp!, {r2, r3, r4, r5, r6, r7, r8, r9, sl, pc}
.L1144:
	.align	2
.L1143:
	.word	.LANCHOR0
	.word	.LANCHOR2
	.word	.LC96
	.fnend
	.size	FlashReadPage, .-FlashReadPage
	.align	2
	.global	FlashReadSlc2KPages
	.type	FlashReadSlc2KPages, %function
FlashReadSlc2KPages:
	.fnstart
	@ args = 0, pretend = 0, frame = 16
	@ frame_needed = 0, uses_anonymous_args = 0
	ldr	r3, .L1158
	stmfd	sp!, {r4, r5, r6, r7, r8, r9, sl, fp, lr}
	.save {r4, r5, r6, r7, r8, r9, sl, fp, lr}
	mov	r8, r1
	.pad #28
	sub	sp, sp, #28
	mov	fp, r2
	ldrb	sl, [r3, #2997]	@ zero_extendqisi2
	mov	r4, r0
	mov	r7, #0
	ldr	r6, .L1158+4
	b	.L1146
.L1155:
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
	bcs	.L1148
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
	ldrb	r3, [r6, #3836]	@ zero_extendqisi2
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
	beq	.L1153
	ldr	r2, [r3, #12]
	cmn	r2, #1
	bne	.L1153
	ldr	r2, [r3, #8]
	cmn	r2, #1
	bne	.L1153
	ldr	r3, [r3, #0]
	cmn	r3, #1
	strne	r2, [r4, #0]
.L1153:
	ldr	r3, [r4, #0]
	cmn	r3, #1
	bne	.L1148
	ldr	r1, [r4, #4]
	ldr	r0, .L1158+8
	ldrb	r2, [r6, #3836]	@ zero_extendqisi2
	bl	printk
	ldr	r1, [r4, #8]
	cmp	r1, #0
	beq	.L1154
	ldr	r0, .L1158+12
	mov	r2, #4
	mov	r3, #8
	bl	rknand_print_hex
.L1154:
	ldr	r1, [r4, #12]
	cmp	r1, #0
	beq	.L1148
	mov	r2, #4
	ldr	r0, .L1158+16
	mov	r3, r2
	bl	rknand_print_hex
.L1148:
	add	r7, r7, #1
	add	r4, r4, #36
.L1146:
	cmp	r7, r8
	bne	.L1155
	mov	r0, #0
	add	sp, sp, #28
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, sl, fp, pc}
.L1159:
	.align	2
.L1158:
	.word	.LANCHOR1
	.word	.LANCHOR0
	.word	.LC97
	.word	.LC98
	.word	.LC99
	.fnend
	.size	FlashReadSlc2KPages, .-FlashReadSlc2KPages
	.align	2
	.global	FlashReadPages
	.type	FlashReadPages, %function
FlashReadPages:
	.fnstart
	@ args = 0, pretend = 0, frame = 40
	@ frame_needed = 0, uses_anonymous_args = 0
	ldr	r3, .L1202
	stmfd	sp!, {r4, r5, r6, r7, r8, r9, sl, fp, lr}
	.save {r4, r5, r6, r7, r8, r9, sl, fp, lr}
	.pad #52
	sub	sp, sp, #52
	ldrb	r3, [r3, #2997]	@ zero_extendqisi2
	mov	r9, r0
	str	r1, [sp, #28]
	str	r2, [sp, #32]
	str	r3, [sp, #24]
	ldr	r3, .L1202+4
	ldrb	r8, [r3, #852]	@ zero_extendqisi2
	ldrb	ip, [r3, #860]	@ zero_extendqisi2
	cmp	r8, #0
	str	ip, [sp, #36]
	moveq	sl, r8
	beq	.L1161
	bl	FlashReadSlc2KPages
	b	.L1162
.L1189:
	mov	r3, #36
	ldr	ip, [sp, #28]
	mul	r3, r3, sl
	add	r2, sp, #44
	ldr	r1, [sp, #32]
	ldr	fp, .L1202+4
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
	bcs	.L1164
	add	r3, fp, r3
	ldrb	r4, [r3, #3764]	@ zero_extendqisi2
	ldr	r3, .L1202+8
	mov	r0, r4
	ldrb	r3, [r3, #180]	@ zero_extendqisi2
	cmp	r3, #0
	moveq	r7, #0
	bl	NandcWaitFlashReady
	ldr	r3, [fp, #3624]
	ldrb	r2, [r3, #19]	@ zero_extendqisi2
	sub	r3, r2, #1
	uxtb	r3, r3
	cmp	r3, #6
	bhi	.L1166
	add	r1, fp, r4
	cmp	r2, #7
	add	fp, fp, r4
	ldr	r2, .L1202+4
	ldrb	r3, [r1, #12]	@ zero_extendqisi2
	ldreqb	r3, [r1, #20]	@ zero_extendqisi2
	ldrb	r1, [fp, #3756]	@ zero_extendqisi2
	cmp	r1, r3
	beq	.L1166
	ldrb	r1, [r2, #1]	@ zero_extendqisi2
	mov	r0, r4
	add	r2, r2, #4
	bl	HynixSetRRPara
.L1166:
	mov	r0, r4
	mov	r5, r5, lsr #31
	bl	NandcFlashCs
	ldr	ip, [sp, #32]
	mov	r0, r4
	cmp	ip, #1
	orreq	r5, r5, #1
	str	r5, [sp, #20]
	cmp	r5, #0
	beq	.L1168
	ldr	r3, .L1202+4
	ldrb	r3, [r3, #928]	@ zero_extendqisi2
	cmp	r3, #0
	beq	.L1168
	bl	flash_enter_slc_mode
	b	.L1169
.L1168:
	bl	flash_exit_slc_mode
.L1169:
	ldr	fp, .L1202+4
.L1201:
	ldr	r1, [sp, #44]
	cmn	r1, #1
	cmpeq	r4, #255
	moveq	r3, #0
	movne	r3, #1
	moveq	r7, r3
	beq	.L1171
	cmp	r7, #0
	beq	.L1172
	ldr	r2, [fp, #856]
	mov	r0, r4
	add	r2, r1, r2
	bl	FlashReadDpCmd
	b	.L1173
.L1172:
	mov	r0, r4
	bl	FlashReadCmd
.L1173:
	mov	r0, r4
	bl	NandcWaitFlashReady
	cmp	r7, #0
	beq	.L1171
	mov	r0, r4
	ldr	r1, [sp, #44]
	bl	FlashReadDpDataOutCmd
.L1171:
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
	bne	.L1201
.L1174:
	cmp	r7, #0
	mov	r5, r0
	beq	.L1175
	ldr	r3, .L1202+4
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
.L1175:
	mov	r0, r4
	ldr	fp, .L1202+4
	bl	NandcFlashDeCs
	ldr	ip, [sp, #36]
	cmn	r5, #1
	strb	ip, [fp, #860]
	bne	.L1179
	ldrb	r3, [fp, #3780]	@ zero_extendqisi2
	cmp	r3, #0
	beq	.L1177
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
	beq	.L1178
	ldrb	r3, [fp, #3836]	@ zero_extendqisi2
	cmp	r0, r3, lsr #1
	bls	.L1195
.L1178:
	ubfx	r0, r7, #8, #8
	bl	NandcSetDdrPara
	cmn	r5, #1
	bne	.L1195
.L1177:
	ldr	r3, .L1202+8
	mov	r0, r4
	ldr	r1, [sp, #44]
	ldr	r2, [r6, #8]
	ldr	r7, [r3, #176]
	ldr	r3, [r6, #12]
	cmp	r7, #0
	beq	.L1180
	blx	r7
	cmn	r0, #1
	mov	r5, r0
	bne	.L1197
	ldr	r2, .L1202+4
	ldr	r3, [r2, #3624]
	ldrb	r3, [r3, #19]	@ zero_extendqisi2
	sub	r3, r3, #1
	uxtb	r3, r3
	cmp	r3, #6
	bhi	.L1182
	ldrb	r1, [r2, #1]	@ zero_extendqisi2
	mov	r0, r4
	add	r2, r2, #4
	mov	r3, #0
	bl	HynixSetRRPara
.L1182:
	ldr	r1, [sp, #44]
	mov	r0, r4
	ldr	r2, [r6, #8]
	ldr	r3, [r6, #12]
	bl	FlashReadRawPage
	ldr	r7, .L1202+4
	ldr	r1, [r6, #4]
	ldrb	r2, [r7, #3836]	@ zero_extendqisi2
	mov	r5, r0
	ldr	r0, .L1202+12
	mov	r3, r5
	bl	printk
	cmn	r5, #1
	bne	.L1197
	ldrb	r7, [r7, #928]	@ zero_extendqisi2
	cmp	r7, #0
	beq	.L1181
	ldr	ip, [sp, #20]
	mov	r0, r4
	cmp	ip, #0
	beq	.L1183
	bl	flash_enter_slc_mode
	b	.L1184
.L1183:
	bl	flash_exit_slc_mode
.L1184:
	ldr	r3, .L1202+8
	mov	r0, r4
	ldr	r1, [sp, #44]
	ldr	r2, [r6, #8]
	ldr	ip, [r3, #176]
	ldr	r3, [r6, #12]
	blx	ip
	mov	r5, r0
	b	.L1197
.L1180:
	bl	FlashReadRawPage
	mov	r5, r0
	b	.L1181
.L1195:
	mov	r7, #0
.L1179:
	ldr	r3, .L1202+4
	ldrb	r3, [r3, #3836]	@ zero_extendqisi2
	add	r3, r3, r3, asl #1
	cmp	r5, r3, lsr #2
	bls	.L1181
	ldr	r3, .L1202+8
	ldr	r3, [r3, #176]
	cmp	r3, #0
	moveq	r5, #256
	b	.L1181
.L1197:
	mov	r7, #0
.L1181:
	ldr	ip, [sp, #16]
	cmp	r5, #256
	cmnne	r5, #1
	movne	r2, #0
	str	r5, [r9, ip]
	strne	r2, [r9, ip]
	ldr	ip, [sp, #16]
	ldr	r3, [r9, ip]
	cmn	r3, #1
	bne	.L1186
	ldr	r2, .L1202+4
	ldr	r1, [r6, #4]
	ldr	r0, .L1202+16
	ldrb	r2, [r2, #3836]	@ zero_extendqisi2
	bl	printk
	ldr	r1, [r6, #12]
	cmp	r1, #0
	beq	.L1186
	mov	r2, #4
	ldr	r0, .L1202+20
	mov	r3, r2
	bl	rknand_print_hex
.L1186:
	cmp	r7, #0
	beq	.L1187
	ldr	r3, .L1202+4
	ldrb	r3, [r3, #3836]	@ zero_extendqisi2
	add	r3, r3, r3, asl #1
	cmp	r8, r3, lsr #2
	bls	.L1188
	ldr	r3, .L1202+8
	ldr	r3, [r3, #176]
	cmp	r3, #0
	moveq	r8, #256
.L1188:
	add	r3, sl, #1
	mov	r2, #36
	cmp	r8, #256
	cmnne	r8, #1
	mul	r3, r2, r3
	movne	r2, #0
	str	r8, [r9, r3]
	strne	r2, [r9, r3]
.L1187:
	ldr	ip, [sp, #20]
	add	sl, sl, r7
	cmp	ip, #0
	beq	.L1164
	ldr	r3, .L1202+4
	ldrb	r3, [r3, #928]	@ zero_extendqisi2
	cmp	r3, #0
	beq	.L1164
	mov	r0, r4
	bl	flash_exit_slc_mode
.L1164:
	add	sl, sl, #1
.L1161:
	ldr	ip, [sp, #28]
	cmp	sl, ip
	bcc	.L1189
	mov	r0, #0
.L1162:
	add	sp, sp, #52
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, sl, fp, pc}
.L1203:
	.align	2
.L1202:
	.word	.LANCHOR1
	.word	.LANCHOR0
	.word	.LANCHOR2
	.word	.LC100
	.word	.LC97
	.word	.LC99
	.fnend
	.size	FlashReadPages, .-FlashReadPages
	.align	2
	.global	FtlGcScanTempBlk
	.type	FtlGcScanTempBlk, %function
FtlGcScanTempBlk:
	.fnstart
	@ args = 0, pretend = 0, frame = 24
	@ frame_needed = 0, uses_anonymous_args = 0
	ldr	r2, .L1229
	movw	r3, #3096
	stmfd	sp!, {r4, r5, r6, r7, r8, r9, sl, fp, lr}
	.save {r4, r5, r6, r7, r8, r9, sl, fp, lr}
	.pad #28
	sub	sp, sp, #28
	ldrh	r4, [r2, r3]
	movw	r3, #65535
	mov	r5, r0
	str	r1, [sp, #20]
	cmp	r4, r3
	beq	.L1225
	cmp	r4, #0
	bne	.L1205
	b	.L1206
.L1225:
	mov	r4, #0
.L1205:
	ldr	r2, .L1229+4
	movw	r3, #3916
	ldr	fp, [sp, #20]
	ldrh	r3, [r2, r3]
	cmp	fp, r3
	bne	.L1207
.L1206:
	bl	FtlGcPageVarInit
.L1207:
	mov	r7, #0
	movw	r8, #65535
	mov	sl, r7
	mvn	r3, #0
	str	r3, [sp, #16]
.L1220:
	ldrh	r3, [r5, #0]
	strb	sl, [r5, #8]
	cmp	r3, r8
	beq	.L1226
.L1209:
.L1227:
	ldr	r3, .L1229+4
	movw	r2, #3848
	ldrh	r9, [r3, r2]
	ldr	r2, .L1229+8
	ldr	lr, [r2, #-968]
	ldr	ip, [r2, #-1168]
	movw	r2, #3928
	ldrh	r2, [r3, r2]
	mov	r3, #0
	mov	r6, r3
	str	r2, [sp, #12]
	mov	r2, r5
	b	.L1210
.L1212:
	ldrh	r0, [r2, #16]
	cmp	r0, r8
	beq	.L1211
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
.L1211:
	add	r3, r3, #1
	add	r2, r2, #2
	uxth	r3, r3
.L1210:
	cmp	r3, r9
	bne	.L1212
	ldr	r9, .L1229+8
	mov	r1, r6
	mov	r2, #0
	ldr	r0, [r9, #-968]
	bl	FlashReadPages
	mov	r3, #36
	mul	r3, r3, r6
	mov	r6, #0
	str	r3, [sp, #12]
	b	.L1213
.L1221:
	ldr	r1, [r9, #-968]
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
	bne	.L1214
	ldr	r3, [r3, #12]
	add	r6, r6, #36
	ldrh	r1, [r3, #0]
	cmp	r1, r8
	ldreq	r3, .L1229+8
	moveq	r1, #1
	streq	r1, [r3, #-1008]
	beq	.L1208
.L1215:
	ldr	r0, [r3, #12]
	mov	r1, fp
	ldr	r2, [r3, #8]
	bl	FtlGcUpdatePage
	b	.L1213
.L1214:
	mov	r2, fp
	ldr	r0, .L1229+12
	ldrh	r1, [r5, #0]
	bl	printk
	ldr	r3, .L1229+4
	ldr	r2, [r3, #3840]
	cmp	r2, #0
	bne	.L1216
	ldrb	r2, [r3, #928]	@ zero_extendqisi2
	cmp	r2, #0
	beq	.L1217
.L1216:
	ldrh	r1, [r5, #0]
	ldr	r2, [r3, #4048]
	mov	r3, r1, asl #1
	ldrh	r3, [r2, r3]
	cmp	r3, #159
	bls	.L1218
.L1217:
	ldr	r3, .L1229+8
	ldr	r3, [r3, #-968]
	ldr	r3, [r3, r6]
	cmn	r3, #1
	bne	.L1219
.L1218:
	ldr	r3, .L1229+8
	ldr	r3, [r3, #-968]
	add	r6, r3, r6
	ldr	r6, [r6, #4]
	str	r6, [sp, #16]
.L1219:
	ldr	r2, .L1229+8
	mov	r4, #0
	ldrh	r3, [r5, #0]
	ldr	r2, [r2, #-2016]
	mov	r3, r3, asl #1
	strh	sl, [r2, r3]	@ movhi
	ldrh	r0, [r5, #0]
	bl	INSERT_FREE_LIST
	mvn	r3, #0
	strh	r3, [r5, #0]	@ movhi
	bl	FtlGcPageVarInit
	b	.L1220
.L1213:
	ldr	r3, [sp, #12]
	cmp	r6, r3
	bne	.L1221
	ldr	fp, [sp, #20]
	add	r7, r7, #1
	add	r4, r4, #1
	cmp	r7, fp
	uxth	r4, r4
	bcc	.L1222
	ldr	r2, .L1229
	movw	r3, #3096
	ldrh	r1, [r2, r3]
	cmp	r1, r8
	beq	.L1222
	add	r1, r1, r7
	strh	r1, [r2, r3]	@ movhi
	ldr	r2, .L1229+4
	add	r3, r3, #820
	ldrh	r3, [r2, r3]
	cmp	r3, r4
	bhi	.L1223
.L1222:
	ldr	r2, .L1229+4
	movw	r3, #3916
	ldrh	r3, [r2, r3]
	cmp	r3, r4
	bhi	.L1227
.L1226:
	mov	r2, #0
.L1208:
	ldr	r1, .L1229
	movw	r3, #3096
	mvn	r0, #0
	strh	r4, [r5, #2]	@ movhi
	strb	r2, [r5, #6]
	strh	r0, [r1, r3]	@ movhi
	mov	r0, r5
	mov	r1, r4
	bl	ftl_sb_update_avl_pages
.L1223:
	ldr	r0, [sp, #16]
	add	sp, sp, #28
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, sl, fp, pc}
.L1230:
	.align	2
.L1229:
	.word	.LANCHOR1
	.word	.LANCHOR0
	.word	.LANCHOR2
	.word	.LC101
	.fnend
	.size	FtlGcScanTempBlk, .-FtlGcScanTempBlk
	.align	2
	.global	FtlScanAllBlock
	.type	FtlScanAllBlock, %function
FtlScanAllBlock:
	.fnstart
	@ args = 0, pretend = 0, frame = 56
	@ frame_needed = 0, uses_anonymous_args = 0
	stmfd	sp!, {r4, r5, r6, r7, r8, r9, sl, fp, lr}
	.save {r4, r5, r6, r7, r8, r9, sl, fp, lr}
	.pad #84
	sub	sp, sp, #84
	ldr	r0, .L1241
	mov	r5, #0
	ldr	r1, .L1241+4
	bl	printk
	ldr	r7, .L1241+8
	b	.L1232
.L1240:
	add	r4, sp, #80
	mov	r0, r5
	ldr	r6, .L1241+12
	mov	r9, #36
	strh	r5, [r4, #-48]!	@ movhi
	bl	ftl_get_blk_mode
	mov	r2, r5, asl #1
	ldr	ip, [r6, #-2016]
	mov	r1, r5
	ldrh	r2, [ip, r2]
	mov	r3, r0
	ldr	r0, .L1241+16
	bl	printk
	mov	r0, r4
	bl	make_superblock
	movw	r3, #3848
	ldrh	sl, [r7, r3]
	add	r3, r3, #78
	add	r0, r4, #14
	mov	r4, #0
	ldrh	r3, [r7, r3]
	ldr	r8, [r6, #-968]
	ldr	lr, [r6, #-1172]
	str	r3, [sp, #28]
	movw	r3, #3928
	ldrh	ip, [r7, r3]
	mov	r3, r4
	ldr	r6, [r6, #-1168]
	b	.L1233
.L1235:
	ldrh	r1, [r0, #2]!
	movw	r2, #65535
	cmp	r1, r2
	beq	.L1234
	mla	r2, r9, r4, r8
	ldr	fp, [sp, #28]
	mov	r1, r1, asl #10
	str	r1, [r2, #4]
	mul	r1, fp, r4
	add	fp, r1, #3
	cmp	r1, #0
	movlt	r1, fp
	bic	r1, r1, #3
	add	r1, lr, r1
	str	r1, [r2, #8]
	mul	r1, ip, r4
	add	r4, r4, #1
	uxth	r4, r4
	add	fp, r1, #3
	cmp	r1, #0
	movlt	r1, fp
	bic	r1, r1, #3
	add	r1, r6, r1
	str	r1, [r2, #12]
.L1234:
	add	r3, r3, #1
	uxth	r3, r3
.L1233:
	cmp	r3, sl
	bne	.L1235
	ldr	sl, .L1241+12
	mov	r1, r4
	mov	r2, #0
	mov	r6, #0
	mov	r8, r6
	ldr	r0, [sl, #-968]
	bl	FlashReadPages
	b	.L1236
.L1237:
	ldr	r2, [sl, #-968]
	add	r8, r8, #1
	ldrh	r1, [sp, #32]
	add	r3, r2, r6
	uxth	r8, r8
	ldr	r0, [r3, #12]
	ldr	ip, [r3, #8]
	ldr	lr, [r0, #0]
	str	lr, [sp, #0]
	ldr	lr, [r0, #4]
	str	lr, [sp, #4]
	ldr	lr, [r0, #8]
	str	lr, [sp, #8]
	ldr	r0, [r0, #12]
	str	r0, [sp, #12]
	ldr	r0, [ip, #0]
	str	r0, [sp, #16]
	ldr	r0, [ip, #4]
	str	r0, [sp, #20]
	ldr	r0, .L1241+20
	ldr	r2, [r2, r6]
	add	r6, r6, #36
	ldr	r3, [r3, #4]
	bl	printk
.L1236:
	cmp	r8, r4
	ldr	r9, .L1241+12
	bne	.L1237
	ldr	r0, [r9, #-968]
	mov	r1, r4
	mov	r2, #1
	mov	r6, #0
	bl	FlashReadPages
	mov	r8, r6
	mov	sl, r9
	b	.L1238
.L1239:
	ldr	r2, [sl, #-968]
	add	r8, r8, #1
	ldrh	r1, [sp, #32]
	add	r3, r2, r6
	uxth	r8, r8
	ldr	r0, [r3, #12]
	ldr	ip, [r3, #8]
	ldr	lr, [r0, #0]
	str	lr, [sp, #0]
	ldr	lr, [r0, #4]
	str	lr, [sp, #4]
	ldr	lr, [r0, #8]
	str	lr, [sp, #8]
	ldr	r0, [r0, #12]
	str	r0, [sp, #12]
	ldr	r0, [ip, #0]
	str	r0, [sp, #16]
	ldr	r0, [ip, #4]
	str	r0, [sp, #20]
	ldr	r0, .L1241+24
	ldr	r2, [r2, r6]
	add	r6, r6, #36
	ldr	r3, [r3, #4]
	bl	printk
.L1238:
	cmp	r8, r4
	bne	.L1239
	add	r5, r5, #1
	uxth	r5, r5
.L1232:
	movw	r3, #3858
	ldrh	r3, [r7, r3]
	cmp	r3, r5
	bhi	.L1240
	mov	r0, #0
	add	sp, sp, #84
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, sl, fp, pc}
.L1242:
	.align	2
.L1241:
	.word	.LC102
	.word	.LANCHOR3+15
	.word	.LANCHOR0
	.word	.LANCHOR2
	.word	.LC103
	.word	.LC104
	.word	.LC105
	.fnend
	.size	FtlScanAllBlock, .-FtlScanAllBlock
	.align	2
	.global	FtlDumpBlockInfo
	.type	FtlDumpBlockInfo, %function
FtlDumpBlockInfo:
	.fnstart
	@ args = 0, pretend = 0, frame = 56
	@ frame_needed = 0, uses_anonymous_args = 0
	stmfd	sp!, {r4, r5, r6, r7, r8, r9, sl, fp, lr}
	.save {r4, r5, r6, r7, r8, r9, sl, fp, lr}
	ubfx	r0, r0, #10, #16
	.pad #84
	sub	sp, sp, #84
	mov	r7, r1
	bl	P2V_block_in_plane
	ldr	r5, .L1254
	ldr	r1, .L1254+4
	movw	r6, #3916
	ldrh	r9, [r5, r6]
	mov	r4, r0
	ldr	r0, .L1254+8
	bl	printk
	ldr	r3, .L1254+12
	mov	r1, r4
	ldr	r0, .L1254+16
	ldr	r2, [r3, #-2016]
	mov	r3, r4, asl #1
	ldrh	r2, [r2, r3]
	bl	printk
	add	r0, sp, #80
	strh	r4, [r0, #-48]!	@ movhi
	bl	make_superblock
	rsbs	r7, r7, #1
	ldrb	r3, [r5, #928]	@ zero_extendqisi2
	movcc	r7, #0
	cmp	r3, #0
	moveq	r7, #0
	cmp	r7, #0
	beq	.L1244
	mov	r0, r4
	bl	ftl_get_blk_mode
	cmp	r0, #1
	mov	r7, r0
	movweq	r3, #3918
	ldreqh	r9, [r5, r3]
.L1244:
	ldrh	r3, [r5, r6]
	mov	r1, r7
	ldr	r0, .L1254+20
	mov	r2, r9
	bl	printk
	mov	r6, #0
.L1245:
	ldr	r3, .L1254
	movw	r2, #3848
	movw	r1, #3926
	mov	r4, #0
	add	r0, sp, #46
	ldrh	sl, [r3, r2]
	ldr	r2, .L1254+12
	ldrh	lr, [r3, r1]
	ldr	r8, [r2, #-968]
	ldr	r5, [r2, #-1172]
	ldr	ip, [r2, #-1168]
	movw	r2, #3928
	ldrh	r2, [r3, r2]
	mov	r3, r4
	str	r2, [sp, #28]
	b	.L1246
.L1248:
	ldrh	r1, [r0, #2]!
	movw	r2, #65535
	cmp	r1, r2
	beq	.L1247
	mov	fp, #36
	orr	r1, r6, r1, asl #10
	mla	r2, fp, r4, r8
	str	r1, [r2, #4]
	mul	r1, lr, r4
	add	fp, r1, #3
	cmp	r1, #0
	movlt	r1, fp
	ldr	fp, [sp, #28]
	bic	r1, r1, #3
	add	r1, r5, r1
	str	r1, [r2, #8]
	mul	r1, fp, r4
	add	r4, r4, #1
	uxth	r4, r4
	add	fp, r1, #3
	cmp	r1, #0
	movlt	r1, fp
	bic	r1, r1, #3
	add	r1, ip, r1
	str	r1, [r2, #12]
.L1247:
	add	r3, r3, #1
	uxth	r3, r3
.L1246:
	cmp	r3, sl
	bne	.L1248
	ldr	r8, .L1254+12
	mov	r1, r4
	mov	r2, r7
	mov	r5, #0
	mov	sl, r5
	ldr	r0, [r8, #-968]
	bl	FlashReadPages
	b	.L1249
.L1250:
	ldr	r2, [r8, #-968]
	add	sl, sl, #1
	ldrh	r1, [sp, #32]
	add	r3, r2, r5
	uxth	sl, sl
	ldr	r0, [r3, #12]
	ldr	ip, [r3, #8]
	ldr	lr, [r0, #0]
	str	lr, [sp, #0]
	ldr	lr, [r0, #4]
	str	lr, [sp, #4]
	ldr	lr, [r0, #8]
	str	lr, [sp, #8]
	ldr	r0, [r0, #12]
	str	r0, [sp, #12]
	ldr	r0, [ip, #0]
	str	r0, [sp, #16]
	ldr	r0, [ip, #4]
	str	r0, [sp, #20]
	ldr	r0, .L1254+24
	ldr	r2, [r2, r5]
	add	r5, r5, #36
	ldr	r3, [r3, #4]
	bl	printk
.L1249:
	cmp	sl, r4
	bne	.L1250
	add	r6, r6, #1
	uxth	r6, r6
	cmp	r6, r9
	bne	.L1245
.L1251:
	mov	r0, #0
	add	sp, sp, #84
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, sl, fp, pc}
.L1255:
	.align	2
.L1254:
	.word	.LANCHOR0
	.word	.LANCHOR3+31
	.word	.LC102
	.word	.LANCHOR2
	.word	.LC106
	.word	.LC107
	.word	.LC108
	.fnend
	.size	FtlDumpBlockInfo, .-FtlDumpBlockInfo
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
	ldr	r5, .L1314
	.pad #28
	sub	sp, sp, #28
	ldr	r4, .L1314+4
	mov	r1, r6
	ldr	r3, .L1314+8
	mov	r7, #3936
	ldr	r2, [r5, #3944]
	ldr	r0, [r4, #-1032]
	mov	sl, r4
	strh	r6, [r4, r3]	@ movhi
	movw	r3, #3962
	strh	r6, [r5, r3]	@ movhi
	mov	r2, r2, asl #2
	bl	ftl_memset
	ldr	r2, [r5, #3944]
	mov	r1, r6
	ldr	r0, [r4, #-1036]
	mov	r2, r2, asl #1
	bl	ftl_memset
	ldrh	r2, [r5, r7]
	mov	r1, r6
	ldr	r0, [r4, #-904]
	mov	r2, r2, asl #2
	bl	ftl_memset
	ldrh	r2, [r5, r7]
	mov	r1, r6
	ldr	r0, [r5, #3964]
	mov	r2, r2, asl #1
	bl	ftl_memset
	ldr	r0, .L1314+12
	mov	r1, #255
	mov	r2, #12
	bl	ftl_memset
	mov	r3, #3856
	ldrh	r3, [r5, r3]
	str	r3, [sp, #8]
	b	.L1257
.L1259:
	add	r3, r6, r5
	ldr	r1, [sp, #8]
	ldrb	r0, [r3, #3874]	@ zero_extendqisi2
	bl	V2P_block
	str	r0, [sp, #0]
	bl	FtlBbmIsBadBlock
	ldr	r3, [sp, #0]
	cmp	r0, #0
	bne	.L1258
	ldr	r2, [r4, #-968]
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
.L1258:
	add	r5, r5, #1
	uxth	r5, r5
	b	.L1292
.L1305:
	mov	r7, #0
	movw	fp, #3848
	mov	r5, r7
	mov	r9, #36
	movw	r8, #3928
.L1292:
	ldrh	r3, [r6, fp]
	cmp	r3, r5
	bhi	.L1259
	cmp	r7, #0
	beq	.L1260
	ldr	r0, [r4, #-968]
	mov	r1, r7
	mov	r2, #1
	mov	fp, r7
	bl	FlashReadPages
	mov	r8, #0
	str	r8, [sp, #12]
.L1291:
	ldr	r3, [r4, #-968]
	add	r2, r3, r8
	ldr	r3, [r3, r8]
	ldr	r6, [r2, #4]
	cmn	r3, #1
	ldr	r5, [r2, #12]
	ubfx	r6, r6, #10, #16
	bne	.L1261
	mov	r7, #16
.L1263:
	ldr	r0, [r4, #-968]
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
	ldreq	r3, [sl, #-968]
	mvneq	r2, #0
	streq	r2, [r3, r8]
	beq	.L1261
.L1262:
	ldr	r3, [r4, #-968]
	ldr	r3, [r3, r8]
	cmn	r3, #1
	bne	.L1261
	sub	r7, r7, #1
	uxth	r7, r7
	cmp	r7, #0
	bne	.L1263
.L1261:
	ldr	r3, [r4, #-968]
	ldr	r3, [r3, r8]
	cmn	r3, #1
	beq	.L1264
	ldr	r2, [r4, #-1804]
	ldr	r3, [r5, #4]
	cmn	r2, #1
	beq	.L1265
	cmp	r2, r3
	bhi	.L1266
.L1265:
	cmn	r3, #1
	addne	r3, r3, #1
	strne	r3, [sl, #-1804]
.L1266:
	ldrh	r3, [r5, #0]
	movw	r2, #61604
	cmp	r3, r2
	beq	.L1269
	bhi	.L1272
	movw	r2, #61574
	cmp	r3, r2
	bne	.L1267
	b	.L1312
.L1272:
	movw	r2, #61634
	cmp	r3, r2
	beq	.L1270
	movw	r2, #65535
	cmp	r3, r2
	bne	.L1267
	b	.L1313
.L1270:
	ldr	r3, .L1314
	mov	r9, r6
	ldr	r1, .L1314+8
	ldr	ip, [r4, #-1032]
	ldr	r0, [r3, #3944]
	ldrh	r1, [r4, r1]
	uxth	r2, r0
	sub	r3, r2, #1
	rsb	r2, r1, r2
	sub	r2, r2, #1
	uxth	r3, r3
	sxth	r2, r2
	str	r2, [sp, #4]
	b	.L1273
.L1279:
	mov	r6, r2, asl #2
	ldr	r7, [r5, #4]
	str	r6, [sp, #16]
	ldr	r6, [ip, r2, asl #2]
	cmp	r7, r6
	bls	.L1274
	ldr	ip, [ip, #0]
	mov	r6, r9
	cmp	ip, #0
	bne	.L1275
	cmp	r1, r0
	addne	r1, r1, #1
	ldrne	r0, .L1314+8
	strneh	r1, [r4, r0]	@ movhi
.L1275:
	mov	r0, #0
	sxth	r9, r3
	str	r3, [sp, #4]
	b	.L1276
.L1277:
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
.L1276:
	sxth	r1, r0
	cmp	r1, r9
	bne	.L1277
	ldr	r1, [sl, #-1032]
	mov	r2, r2, asl #1
	ldr	r0, [r5, #4]
	ldr	ip, [sp, #16]
	ldr	r3, [sp, #4]
	str	r0, [r1, ip]
	ldr	r1, [sl, #-1036]
	strh	r6, [r1, r2]	@ movhi
	b	.L1278
.L1274:
	sub	r3, r3, #1
	uxth	r3, r3
.L1273:
	ldr	r6, [sp, #4]
	sxth	r2, r3
	cmp	r2, r6
	bgt	.L1279
	mov	r6, r9
.L1278:
	sxth	r3, r3
	cmp	r3, #0
	blt	.L1267
	ldr	r2, .L1314+8
	ldr	r0, .L1314
	ldrh	r1, [r4, r2]
	ldr	r0, [r0, #3944]
	rsb	r0, r1, r0
	sub	r0, r0, #1
	sxth	r0, r0
	cmp	r3, r0
	bgt	.L1267
	add	r1, r1, #1
	strh	r1, [r4, r2]	@ movhi
	ldr	r2, [r4, #-1032]
	ldr	r1, [r5, #4]
	str	r1, [r2, r3, asl #2]
	mov	r3, r3, asl #1
	ldr	r2, [r4, #-1036]
	b	.L1309
.L1312:
	ldr	r1, .L1314
	mov	r3, #3936
	movw	ip, #3962
	mov	r9, r6
	ldrh	r0, [r1, r3]
	ldrh	r1, [r1, ip]
	sub	r2, r0, #1
	ldr	ip, [r4, #-904]
	uxth	r3, r2
	rsb	r2, r1, r2
	str	r2, [sp, #4]
	b	.L1280
.L1286:
	mov	r6, r2, asl #2
	ldr	r7, [r5, #4]
	str	r6, [sp, #16]
	ldr	r6, [ip, r2, asl #2]
	cmp	r7, r6
	bls	.L1281
	ldr	ip, [ip, #0]
	mov	r6, r9
	cmp	ip, #0
	bne	.L1282
	cmp	r1, r0
	addne	r1, r1, #1
	ldrne	ip, .L1314
	movwne	r0, #3962
	strneh	r1, [ip, r0]	@ movhi
.L1282:
	sxth	ip, r3
	mov	r0, #0
	str	ip, [sp, #20]
	ldr	r9, .L1314
	str	r3, [sp, #4]
	b	.L1283
.L1284:
	ldr	r7, [r4, #-904]
	add	ip, r1, #1
	add	r0, r0, #1
	ldr	r3, [r7, ip, asl #2]
	mov	ip, ip, asl #1
	uxth	r0, r0
	str	r3, [r7, r1, asl #2]
	mov	r1, r1, asl #1
	ldr	r7, [r9, #3964]
	ldrh	ip, [r7, ip]
	strh	ip, [r7, r1]	@ movhi
.L1283:
	ldr	ip, [sp, #20]
	sxth	r1, r0
	cmp	r1, ip
	bne	.L1284
	ldr	r1, [sl, #-904]
	mov	r2, r2, asl #1
	ldr	r0, [r5, #4]
	ldr	ip, [sp, #16]
	ldr	r3, [sp, #4]
	str	r0, [r1, ip]
	ldr	r1, .L1314
	ldr	r1, [r1, #3964]
	strh	r6, [r1, r2]	@ movhi
	b	.L1285
.L1281:
	sub	r3, r3, #1
	uxth	r3, r3
.L1280:
	ldr	r6, [sp, #4]
	sxth	r2, r3
	cmp	r2, r6
	bgt	.L1286
	mov	r6, r9
.L1285:
	sxth	r3, r3
	cmp	r3, #0
	blt	.L1267
	ldr	r2, .L1314
	mov	ip, #3936
	movw	r1, #3962
	ldrh	ip, [r2, ip]
	ldrh	r0, [r2, r1]
	sub	ip, ip, #1
	rsb	ip, r0, ip
	sxth	ip, ip
	cmp	r3, ip
	bgt	.L1267
	add	r0, r0, #1
	strh	r0, [r2, r1]	@ movhi
	ldr	r0, [r5, #4]
	ldr	r1, [r4, #-904]
	str	r0, [r1, r3, asl #2]
	mov	r3, r3, asl #1
	ldr	r2, [r2, #3964]
.L1309:
	strh	r6, [r2, r3]	@ movhi
	b	.L1267
.L1269:
	ldr	r3, .L1314+16
	movw	ip, #65535
	ldrh	r2, [r4, r3]
	cmp	r2, ip
	streqh	r6, [r4, r3]	@ movhi
	beq	.L1310
	ldr	r3, .L1314+20
	movw	r1, #65535
	ldrh	r0, [r4, r3]
	cmp	r0, r1
	beq	.L1288
	mov	r1, #1
	bl	FtlFreeSysBlkQueueIn
.L1288:
	ldr	r3, [r5, #4]
	ldr	r2, [sl, #-1764]
	cmp	r2, r3
	ldr	r3, .L1314+20
	strcsh	r6, [r4, r3]	@ movhi
	bcs	.L1267
	ldr	r2, .L1314+16
	ldrh	r1, [r4, r2]
	strh	r6, [r4, r2]	@ movhi
	strh	r1, [r4, r3]	@ movhi
.L1310:
	ldr	r3, [r5, #4]
	str	r3, [r4, #-1764]
	b	.L1267
.L1313:
	mov	r0, r6
	b	.L1311
.L1264:
	ldr	r3, .L1314
	mov	r0, r6
	ldrb	r1, [r3, #928]	@ zero_extendqisi2
	cmp	r1, #0
	beq	.L1290
.L1311:
	mov	r1, #0
.L1290:
	bl	FtlFreeSysBlkQueueIn
.L1267:
	ldr	r6, [sp, #12]
	add	r8, r8, #36
	add	r3, r6, #1
	uxth	r3, r3
	str	r3, [sp, #12]
	cmp	r3, fp
	bne	.L1291
.L1260:
	ldr	ip, [sp, #8]
	add	r3, ip, #1
	uxth	r3, r3
	str	r3, [sp, #8]
.L1257:
	ldr	r6, .L1314
	movw	r3, #3858
	ldr	ip, [sp, #8]
	ldrh	r3, [r6, r3]
	cmp	r3, ip
	bhi	.L1305
	ldr	r1, .L1314+4
	ldr	r2, [r1, #-1036]
	ldrh	r3, [r2, #0]
	cmp	r3, #0
	bne	.L1293
	ldr	r0, .L1314+8
	ldrh	r1, [r1, r0]
	cmp	r1, #0
	ldrne	r0, [r6, #3944]
	bne	.L1294
	b	.L1293
.L1298:
	mov	r1, r1, asl #1
	ldrh	r1, [r2, r1]
	cmp	r1, #0
	beq	.L1295
	ldr	r7, .L1314
	sxth	r6, r3
	ldr	r1, .L1314+4
	mov	r5, #0
	b	.L1296
.L1297:
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
.L1296:
	ldr	r0, [r7, #3944]
	sxth	r2, r3
	cmp	r2, r0
	bcc	.L1297
	b	.L1293
.L1295:
	add	r3, r3, #1
	uxth	r3, r3
.L1294:
	sxth	r1, r3
	cmp	r1, r0
	bcc	.L1298
.L1293:
	ldr	r2, .L1314
	ldr	r1, [r2, #3964]
	ldrh	r3, [r1, #0]
	cmp	r3, #0
	bne	.L1299
	movw	r0, #3962
	ldrh	r0, [r2, r0]
	cmp	r0, #0
	movne	r0, #3936
	ldrneh	r2, [r2, r0]
	bne	.L1300
	b	.L1299
.L1304:
	mov	ip, r0, asl #1
	ldrh	ip, [r1, ip]
	cmp	ip, #0
	beq	.L1301
	ldr	r1, .L1314
	mov	r8, #3936
	ldr	r7, .L1314+4
	mov	r6, #0
	b	.L1302
.L1303:
	ldr	r4, [r1, #3964]
	mov	ip, r2, asl #1
	rsb	r5, r0, r2
	add	r3, r3, #1
	ldrh	r9, [r4, ip]
	mov	sl, r5, asl #1
	uxth	r3, r3
	strh	r9, [r4, sl]	@ movhi
	ldr	r4, [r7, #-904]
	ldr	r2, [r4, r2, asl #2]
	str	r2, [r4, r5, asl #2]
	ldr	r2, [r1, #3964]
	strh	r6, [r2, ip]	@ movhi
.L1302:
	ldrh	ip, [r1, r8]
	sxth	r2, r3
	cmp	r2, ip
	blt	.L1303
	b	.L1299
.L1301:
	add	r3, r3, #1
	uxth	r3, r3
.L1300:
	sxth	r0, r3
	cmp	r0, r2
	blt	.L1304
.L1299:
	mov	r0, #0
	add	sp, sp, #28
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, sl, fp, pc}
.L1315:
	.align	2
.L1314:
	.word	.LANCHOR0
	.word	.LANCHOR2
	.word	-1040
	.word	.LANCHOR2-1772
	.word	-1772
	.word	-1768
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
	ldr	r3, .L1327
	stmfd	sp!, {r4, r5, r6, r7, r8, sl, lr}
	.save {r4, r5, r6, r7, r8, sl, lr}
	movweq	r2, #3918
	movwne	r2, #3916
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
	bne	.L1320
	b	.L1325
.L1323:
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
	bne	.L1321
	ldr	r3, [sp, #44]
	cmn	r3, #1
	bne	.L1321
	ldr	r3, [sp, #4]
	cmn	r3, #1
	subne	r4, r6, #1
	uxthne	r4, r4
	bne	.L1325
.L1321:
	add	r6, r6, #1
	uxth	r8, r6
.L1325:
	sxth	r6, r8
	sxth	r3, r4
	cmp	r6, r3
	ble	.L1323
.L1320:
	sxth	r0, r4
	add	sp, sp, #108
	ldmfd	sp!, {r4, r5, r6, r7, r8, sl, pc}
.L1328:
	.align	2
.L1327:
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
	mov	r1, #0
	ldr	r4, .L1345
	ldr	r2, .L1345+4
	ldr	r7, .L1345+8
	ldr	r3, [r4, #-956]
	ldr	r0, [r4, #-2016]
	str	r3, [r4, #192]
	ldr	r3, [r4, #-932]
	str	r3, [r4, #196]
	mov	r3, #3856
	ldrh	r2, [r2, r3]
	mov	r2, r2, asl #1
	bl	ftl_memset
	ldrh	r0, [r4, r7]
	movw	r3, #65535
	cmp	r0, r3
	beq	.L1343
	mov	r1, #1
	mov	r5, #0
	bl	FtlGetLastWrittenPage
	ldr	r3, .L1345+12
	add	r2, r0, #1
	mov	r6, r0
	strh	r2, [r4, r3]	@ movhi
	b	.L1331
.L1334:
	ldrh	r2, [r4, r7]
	mov	r1, #1
	ldr	r0, .L1345+16
	orr	r3, r3, r2, asl #10
	str	r3, [r4, #188]
	ldr	r3, [r4, #-956]
	mov	r2, r1
	str	r3, [r4, #192]
	bl	FlashReadPages
	ldr	r3, [r4, #184]
	cmn	r3, #1
	beq	.L1332
	ldr	r3, [r4, #-956]
	ldr	r2, [r3, #0]
	ldr	r3, .L1345+20
	cmp	r2, r3
	bne	.L1332
	ldr	r3, [r4, #-932]
	ldrh	r2, [r3, #0]
	movw	r3, #61604
	cmp	r2, r3
	beq	.L1333
.L1332:
	sub	r5, r5, #1
	uxth	r5, r5
.L1331:
	add	r3, r5, r6
	sxth	r3, r3
	cmp	r3, #0
	bge	.L1334
.L1333:
	ldr	r4, .L1345
	mov	r2, #48
	ldr	r5, .L1345+4
	mov	r6, #3856
	ldr	r0, .L1345+24
	ldr	r7, [r4, #192]
	mov	r1, r7
	bl	memcpy
	ldrh	r2, [r5, r6]
	add	r1, r7, #48
	ldr	r0, [r4, #-2016]
	mov	r2, r2, asl #1
	bl	memcpy
	ldrh	r1, [r5, r6]
	ldr	r3, [r4, #192]
	ldr	r0, [r4, #-1840]
	mov	r2, r1, lsr #3
	add	r1, r1, #24
	add	r2, r2, #4
	mov	r1, r1, lsr #1
	add	r1, r3, r1, asl #2
	bl	memcpy
	movw	r3, #3960
	ldrh	r3, [r5, r3]
	cmp	r3, #0
	beq	.L1335
	ldrh	r3, [r5, r6]
	mov	r2, #3952
	ldrh	r2, [r5, r2]
	ldr	r0, [r4, #-896]
	mov	r1, r3, lsr #3
	add	r1, r1, r3, asl #1
	ldr	r3, [r4, #192]
	add	r1, r1, #52
	mov	r2, r2, asl #2
	ubfx	r1, r1, #2, #14
	add	r1, r3, r1, asl #2
	bl	memcpy
.L1335:
	ldr	r4, .L1345
	ldr	r3, .L1345+20
	ldr	r2, [r4, #-2072]
	cmp	r2, r3
	bne	.L1343
	ldr	r3, .L1345+28
	movw	r1, #3870
	ldrb	r0, [r4, #-2062]	@ zero_extendqisi2
	ldrh	r2, [r4, r3]
	ldr	r3, .L1345+32
	strh	r2, [r4, r3]	@ movhi
	ldr	r3, .L1345+4
	ldrh	r1, [r3, r1]
	cmp	r0, r1
	bne	.L1343
	movw	r1, #3916
	movw	r0, #3922
	ldrh	r1, [r3, r1]
	movw	r5, #65535
	ldrh	r0, [r3, r0]
	str	r2, [r4, #220]
	mul	r1, r2, r1
	str	r1, [r3, #3976]
	mul	r1, r0, r1
	ldr	r0, [r3, #3860]
	str	r1, [r3, #3956]
	movw	r1, #3986
	ldrh	r1, [r3, r1]
	rsb	r0, r1, r0
	rsb	r0, r2, r0
	movw	r2, #3848
	ldrh	r1, [r3, r2]
	bl	__aeabi_uidiv
	ldr	r3, .L1345+36
	ldr	r1, .L1345+40
	strh	r0, [r4, r3]	@ movhi
	ldr	r3, .L1345+44
	ldrh	r2, [r4, r3]
	add	r3, r3, #62
	strh	r2, [r4, r3]	@ movhi
	sub	r3, r3, #60
	ldrh	r3, [r4, r3]
	mov	r0, r3, lsr #6
	and	r3, r3, #63
	strb	r3, [r4, #-1990]
	ldrb	r3, [r4, #-2061]	@ zero_extendqisi2
	strh	r0, [r4, r1]	@ movhi
	mvn	r1, #0
	ldr	r0, .L1345+48
	strb	r3, [r4, #-1988]
	ldr	r3, .L1345+52
	strh	r1, [r4, r3]	@ movhi
	mov	r3, #0
	ldr	r1, .L1345+56
	strh	r3, [r4, r0]	@ movhi
	strb	r3, [r4, #-1754]
	ldrh	r0, [r4, r1]
	add	r1, r1, #106
	strb	r3, [r4, #-1752]
	str	r3, [r4, #-1816]
	strh	r0, [r4, r1]	@ movhi
	sub	r1, r1, #104
	ldr	r0, .L1345+60
	ldrh	r1, [r4, r1]
	mov	ip, r1, lsr #6
	and	r1, r1, #63
	strb	r1, [r4, #-1942]
	ldrb	r1, [r4, #-2060]	@ zero_extendqisi2
	strh	ip, [r4, r0]	@ movhi
	strb	r1, [r4, #-1940]
	ldr	r1, .L1345+64
	ldrh	r0, [r4, r1]
	add	r1, r1, #150
	strh	r0, [r4, r1]	@ movhi
	ldr	r1, .L1345+68
	ldr	r0, .L1345+72
	ldrh	r1, [r4, r1]
	mov	ip, r1, lsr #6
	and	r1, r1, #63
	strb	r1, [r4, #-1894]
	ldrb	r1, [r4, #-2059]	@ zero_extendqisi2
	strh	ip, [r4, r0]	@ movhi
	strb	r1, [r4, #-1892]
	str	r3, [r4, #-1828]
	ldr	r1, [r4, #-2040]
	str	r3, [r4, #-1836]
	str	r3, [r4, #-1820]
	str	r3, [r4, #-1792]
	str	r3, [r4, #-1784]
	str	r3, [r4, #-1824]
	ldr	r3, [r4, #-2032]
	str	r1, [r4, #-1796]
	ldr	r1, [r4, #-1804]
	cmp	r3, r1
	strhi	r3, [r4, #-1804]
	ldr	r4, .L1345
	ldr	r3, [r4, #-2036]
	ldr	r1, [r4, #-1800]
	cmp	r3, r1
	strhi	r3, [r4, #-1800]
	cmp	r2, r5
	beq	.L1338
	ldr	r0, .L1345+76
	bl	make_superblock
.L1338:
	ldr	r3, .L1345+80
	ldrh	r3, [r4, r3]
	cmp	r3, r5
	beq	.L1339
	ldr	r0, .L1345+84
	bl	make_superblock
.L1339:
	ldr	r5, .L1345
	movw	r4, #65535
	ldr	r3, .L1345+88
	ldrh	r3, [r5, r3]
	cmp	r3, r4
	beq	.L1340
	ldr	r0, .L1345+92
	bl	make_superblock
.L1340:
	ldr	r3, .L1345+52
	ldrh	r3, [r5, r3]
	cmp	r3, r4
	beq	.L1344
	ldr	r0, .L1345+96
	bl	make_superblock
	mov	r0, #0
	ldmfd	sp!, {r3, r4, r5, r6, r7, pc}
.L1343:
	mvn	r0, #0
	ldmfd	sp!, {r3, r4, r5, r6, r7, pc}
.L1344:
	mov	r0, #0
	ldmfd	sp!, {r3, r4, r5, r6, r7, pc}
.L1346:
	.align	2
.L1345:
	.word	.LANCHOR2
	.word	.LANCHOR0
	.word	-1772
	.word	-1770
	.word	.LANCHOR2+184
	.word	1179929683
	.word	.LANCHOR2-2072
	.word	-2064
	.word	-1766
	.word	-1776
	.word	-1994
	.word	-2058
	.word	-1758
	.word	-1760
	.word	-2054
	.word	-1946
	.word	-2050
	.word	-2048
	.word	-1898
	.word	.LANCHOR2-1996
	.word	-1948
	.word	.LANCHOR2-1948
	.word	-1900
	.word	.LANCHOR2-1900
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
	ldr	r6, .L1363
	ldr	r8, .L1363+4
	ldr	r3, [r6, #-956]
	mov	r7, r6
	ldr	r4, [r6, #-932]
	str	r3, [r6, #192]
	str	r4, [r6, #196]
	bl	FtlBbtMemInit
	movw	r3, #3912
	ldrh	r5, [r8, r3]
	sub	r5, r5, #1
	uxth	r5, r5
	b	.L1348
.L1352:
	mov	r1, #1
	mov	r3, r5, asl #10
	ldr	r0, .L1363+8
	mov	r2, r1
	str	r3, [r6, #188]
	bl	FlashReadPages
	ldr	r3, [r6, #184]
	cmn	r3, #1
	bne	.L1349
	ldr	r3, [r6, #188]
	mov	r1, #1
	ldr	r0, .L1363+8
	mov	r2, r1
	add	r3, r3, #1
	str	r3, [r6, #188]
	bl	FlashReadPages
.L1349:
	ldr	r3, [r7, #184]
	cmn	r3, #1
	beq	.L1350
	ldrh	r2, [r4, #0]
	movw	r3, #61649
	cmp	r2, r3
	bne	.L1350
	ldr	r3, .L1363+4
	movw	r2, #3980
	strh	r5, [r3, r2]	@ movhi
	ldr	r2, [r4, #4]
	str	r2, [r3, #3988]
	mov	r2, #3984
	ldrh	r1, [r4, #8]
	strh	r1, [r3, r2]	@ movhi
	b	.L1351
.L1350:
	sub	r5, r5, #1
	uxth	r5, r5
.L1348:
	movw	r3, #3912
	ldrh	r3, [r8, r3]
	sub	r3, r3, #48
	cmp	r5, r3
	bgt	.L1352
.L1351:
	ldr	r5, .L1363+4
	movw	r8, #3980
	movw	r3, #65535
	ldrh	r2, [r5, r8]
	cmp	r2, r3
	beq	.L1362
	mov	r6, #3984
	ldrh	r2, [r5, r6]
	cmp	r2, r3
	beq	.L1354
	ldr	r7, .L1363
	mov	r1, #1
	mov	r2, r2, asl #10
	add	r0, r7, #184
	str	r2, [r7, #188]
	mov	r2, r1
	bl	FlashReadPages
	ldr	r3, [r7, #184]
	cmn	r3, #1
	beq	.L1354
	ldrh	r2, [r4, #0]
	movw	r3, #61649
	cmp	r2, r3
	bne	.L1354
	ldr	r3, [r4, #4]
	ldr	r2, [r5, #3988]
	cmp	r3, r2
	strhi	r3, [r5, #3988]
	ldrhih	r2, [r5, r6]
	ldrhih	r3, [r4, #8]
	strhih	r2, [r5, r8]	@ movhi
	strhih	r3, [r5, r6]	@ movhi
.L1354:
	ldr	r8, .L1363+4
	movw	r3, #3980
	mov	r1, #1
	mov	r6, #0
	ldr	r5, .L1363
	ldrh	r0, [r8, r3]
	bl	FtlGetLastWrittenPage
	movw	r3, #3982
	add	r2, r0, #1
	mov	r7, r0
	strh	r2, [r8, r3]	@ movhi
	b	.L1355
.L1358:
	movw	r2, #3980
	mov	r1, #1
	ldrh	r2, [r8, r2]
	ldr	r0, .L1363+8
	orr	r3, r3, r2, asl #10
	str	r3, [r5, #188]
	ldr	r3, [r5, #-956]
	mov	r2, r1
	str	r3, [r5, #192]
	bl	FlashReadPages
	ldr	r3, [r5, #184]
	cmn	r3, #1
	beq	.L1356
	ldrh	r2, [r4, #0]
	movw	r3, #61649
	cmp	r2, r3
	beq	.L1357
.L1356:
	sub	r6, r6, #1
	uxth	r6, r6
.L1355:
	add	r3, r6, r7
	sxth	r3, r3
	cmp	r3, #0
	bge	.L1358
.L1357:
	ldr	r3, .L1363+4
	movw	r2, #3986
	ldrh	r1, [r4, #10]
	ldrh	r0, [r4, #12]
	strh	r1, [r3, r2]	@ movhi
	movw	r2, #65535
	cmp	r0, r2
	beq	.L1359
	ldr	r2, [r3, #3844]
	cmp	r0, r2
	beq	.L1359
	movw	r1, #3858
	ldrh	r3, [r3, r1]
	mov	r3, r3, lsr #2
	cmp	r2, r3
	bcs	.L1359
	cmp	r0, r3
	bcs	.L1359
	bl	FtlSysBlkNumInit
.L1359:
	ldr	r6, .L1363+12
	mov	r4, #0
	ldr	r8, .L1363+4
	movw	r7, #3870
	ldr	r5, .L1363
	b	.L1360
.L1361:
	ldr	r3, .L1363+16
	ldr	r1, [r5, #192]
	ldr	r0, [r6, #4]!
	ldrh	r2, [r5, r3]
	mov	r2, r2, asl #2
	mla	r1, r4, r2, r1
	bl	memcpy
	add	r4, r4, #1
.L1360:
	ldrh	r3, [r8, r7]
	cmp	r4, r3
	bcc	.L1361
	mov	r0, #0
	ldmfd	sp!, {r4, r5, r6, r7, r8, pc}
.L1362:
	mvn	r0, #0
	ldmfd	sp!, {r4, r5, r6, r7, r8, pc}
.L1364:
	.align	2
.L1363:
	.word	.LANCHOR2
	.word	.LANCHOR0
	.word	.LANCHOR2+184
	.word	.LANCHOR0+4004
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
	ldr	r3, .L1372
	movw	fp, #3870
	ldr	r7, .L1372+4
	ldr	r6, .L1372+8
	ldr	r2, [r3, #-956]
	ldr	r8, [r3, #-932]
	str	r2, [r3, #192]
	str	r8, [r3, #196]
	b	.L1366
.L1371:
	movw	r9, #3912
	mvn	r3, #0
	ldrh	r4, [r6, r9]
	strh	r3, [r7], #2	@ movhi
	add	r4, r4, r3
	ldr	sl, .L1372
	uxth	r4, r4
	b	.L1367
.L1370:
	mla	r3, r3, r5, r4
	mov	r1, #1
	ldr	r0, .L1372+12
	mov	r2, r1
	mov	r3, r3, asl #10
	str	r3, [sl, #188]
	bl	FlashReadPages
	ldr	r3, [sl, #184]
	cmn	r3, #1
	beq	.L1368
	ldrh	r2, [r8, #0]
	movw	r3, #61664
	cmp	r2, r3
	streqh	r4, [r7, #-2]	@ movhi
	beq	.L1369
.L1368:
	sub	r4, r4, #1
	uxth	r4, r4
.L1367:
	ldrh	r3, [r6, r9]
	sub	r2, r3, #16
	cmp	r4, r2
	bgt	.L1370
.L1369:
	add	r5, r5, #1
.L1366:
	ldrh	r3, [r6, fp]
	cmp	r5, r3
	bcc	.L1371
	mov	r0, #0
	ldmfd	sp!, {r3, r4, r5, r6, r7, r8, r9, sl, fp, pc}
.L1373:
	.align	2
.L1372:
	.word	.LANCHOR2
	.word	.LANCHOR0+3992
	.word	.LANCHOR0
	.word	.LANCHOR2+184
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
	ldr	r3, .L1393
	mov	r6, r0
	mov	r8, #0
	ldr	r7, .L1393+4
	ldrb	fp, [r3, #2997]	@ zero_extendqisi2
	b	.L1375
.L1382:
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
	bcs	.L1377
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
.L1377:
	add	r8, r8, #1
	add	r6, r6, #36
.L1375:
	cmp	r8, sl
	bne	.L1382
	ldr	r3, [sp, #12]
	cmp	r3, #0
	movne	r6, #0
	ldrne	r5, .L1393+8
	bne	.L1383
	b	.L1384
.L1389:
	ldr	r3, [r4, #0]
	cmn	r3, #1
	bne	.L1385
	ldr	r1, [r4, #4]
	ldr	r0, .L1393+12
	bl	printk
	b	.L1386
.L1385:
	rsb	r3, r6, sl
	mov	r1, r9
	add	r2, sp, #20
	mov	r0, r4
	uxtb	r3, r3
	str	r3, [sp, #0]
	add	r3, sp, #24
	bl	LogAddr2PhyAddr
	ldr	r2, [r5, #224]
	mov	r3, #0
	mov	lr, r4
	add	ip, sp, #28
	str	r3, [r2, #0]
	ldr	r2, [r5, #228]
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
	ldr	r3, [r5, #224]
	str	r3, [sp, #36]
	ldr	r3, [r5, #228]
	str	r3, [sp, #40]
	bl	FlashReadPages
	ldr	r7, [sp, #28]
	cmn	r7, #1
	bne	.L1387
	ldr	r0, .L1393+16
	ldr	r1, [r4, #4]
	bl	printk
	str	r7, [r4, #0]
.L1387:
	ldr	r3, [r4, #12]
	cmp	r3, #0
	beq	.L1388
	ldr	r2, [r3, #0]
	ldr	r3, [r5, #228]
	ldr	r3, [r3, #0]
	cmp	r2, r3
	beq	.L1388
	ldr	r0, .L1393+20
	ldr	r1, [r4, #4]
	bl	printk
	mvn	r3, #0
	str	r3, [r4, #0]
.L1388:
	ldr	r3, [r4, #8]
	cmp	r3, #0
	beq	.L1386
	ldr	r2, [r3, #0]
	ldr	r3, [r5, #224]
	ldr	r3, [r3, #0]
	cmp	r2, r3
	beq	.L1386
	ldr	r0, .L1393+24
	ldr	r1, [r4, #4]
	bl	printk
	mvn	r3, #0
	str	r3, [r4, #0]
.L1386:
	add	r6, r6, #1
	add	r4, r4, #36
.L1383:
	cmp	r6, sl
	bne	.L1389
.L1384:
	mov	r0, #0
	add	sp, sp, #68
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, sl, fp, pc}
.L1394:
	.align	2
.L1393:
	.word	.LANCHOR1
	.word	.LANCHOR0
	.word	.LANCHOR2
	.word	.LC109
	.word	.LC110
	.word	.LC111
	.word	.LC112
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
	ldr	r5, .L1423
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
	ldr	ip, .L1423+4
	ldrb	ip, [ip, #2997]	@ zero_extendqisi2
	str	ip, [sp, #12]
	beq	.L1396
	bl	FlashProgSlc2KPages
	b	.L1397
.L1410:
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
	bcs	.L1399
	ldrb	r1, [r5, #3837]	@ zero_extendqisi2
	add	r2, fp, r2, asl #4
	cmp	r1, #0
	ldr	r2, [r2, #3636]
	moveq	r8, #0
	cmp	r2, #0
	beq	.L1401
	cmp	r3, #1
	bne	.L1402
	ldr	r0, [r5, #3012]
	bl	NandcIqrWaitFlashReady
.L1402:
	ldrb	r0, [sp, #32]	@ zero_extendqisi2
	bl	FlashWaitCmdDone
.L1401:
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
	bne	.L1404
	bl	NandcWaitFlashReady
	b	.L1405
.L1404:
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
.L1405:
	ldr	r2, [sp, #20]
	sub	r3, r2, #1
	cmp	r3, #6
	bhi	.L1406
	add	r3, r5, r4
	ldrb	r3, [r3, #3756]	@ zero_extendqisi2
	cmp	r3, #0
	beq	.L1406
	mov	r0, r4
	ldrb	r1, [r5, #1]	@ zero_extendqisi2
	ldr	r2, .L1423+8
	mov	r3, #0
	bl	HynixSetRRPara
.L1406:
	mov	r0, r4
	bl	NandcFlashCs
	cmp	sl, #1
	mov	r0, r4
	bne	.L1407
	ldrb	r3, [r5, #928]	@ zero_extendqisi2
	cmp	r3, #0
	beq	.L1407
	bl	flash_enter_slc_mode
	b	.L1408
.L1407:
	bl	flash_exit_slc_mode
.L1408:
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
	beq	.L1409
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
.L1409:
	mov	r0, r4
	ldr	r1, [sp, #28]
	bl	FlashProgSecondCmd
	mov	r0, r4
	bl	NandcFlashDeCs
	add	r6, r6, r8
.L1399:
	add	r6, r6, #1
.L1396:
	ldr	r3, [sp, #8]
	cmp	r6, r3
	bcc	.L1410
	ldr	r5, .L1423
	mov	r4, #0
	ldr	r6, .L1423+12
	ldr	r0, [r5, #3012]
	bl	NandcIqrWaitFlashReady
	b	.L1411
.L1413:
	uxtb	r0, r4
	bl	FlashWaitCmdDone
	cmp	sl, #1
	bne	.L1412
	ldrb	r3, [r5, #928]	@ zero_extendqisi2
	cmp	r3, #0
	beq	.L1412
	ldrb	r0, [r6, r4, asl #4]	@ zero_extendqisi2
	bl	flash_exit_slc_mode
.L1412:
	add	r4, r4, #1
.L1411:
	ldrb	r3, [r5, #3762]	@ zero_extendqisi2
	cmp	r4, r3
	bcc	.L1413
	ldr	r2, [sp, #16]
	cmp	r2, #0
	ldreq	r0, [sp, #16]
	beq	.L1397
	mov	r5, #0
	ldr	r4, .L1423+16
	ldr	r6, [sp, #8]
	b	.L1414
.L1419:
	ldr	r3, [r9, #0]
	cmn	r3, #1
	bne	.L1415
	ldr	r1, [r9, #4]
	ldr	r0, .L1423+20
	bl	printk
	b	.L1416
.L1415:
	rsb	r3, r5, r6
	mov	r1, sl
	add	r2, sp, #28
	mov	r0, r9
	uxtb	r3, r3
	str	r3, [sp, #0]
	add	r3, sp, #32
	bl	LogAddr2PhyAddr
	ldr	r2, [r4, #224]
	mov	r3, #0
	mov	lr, r9
	add	ip, sp, #36
	str	r3, [r2, #0]
	ldr	r2, [r4, #228]
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
	ldr	r3, [r4, #224]
	str	r3, [sp, #44]
	ldr	r3, [r4, #228]
	str	r3, [sp, #48]
	bl	FlashReadPages
	ldr	r7, [sp, #36]
	cmn	r7, #1
	bne	.L1417
	ldr	r0, .L1423+24
	ldr	r1, [r9, #4]
	bl	printk
	str	r7, [r9, #0]
.L1417:
	ldr	r3, [r9, #12]
	cmp	r3, #0
	beq	.L1418
	ldr	r2, [r3, #0]
	ldr	r3, [r4, #228]
	ldr	r3, [r3, #0]
	cmp	r2, r3
	beq	.L1418
	ldr	r0, .L1423+28
	ldr	r1, [r9, #4]
	bl	printk
	mvn	r3, #0
	str	r3, [r9, #0]
.L1418:
	ldr	r3, [r9, #8]
	cmp	r3, #0
	beq	.L1416
	ldr	r2, [r3, #0]
	ldr	r3, [r4, #224]
	ldr	r3, [r3, #0]
	cmp	r2, r3
	beq	.L1416
	ldr	r0, .L1423+32
	ldr	r1, [r9, #4]
	bl	printk
	mvn	r3, #0
	str	r3, [r9, #0]
.L1416:
	add	r5, r5, #1
	add	r9, r9, #36
.L1414:
	cmp	r5, r6
	bne	.L1419
	mov	r0, #0
.L1397:
	add	sp, sp, #76
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, sl, fp, pc}
.L1424:
	.align	2
.L1423:
	.word	.LANCHOR0
	.word	.LANCHOR1
	.word	.LANCHOR0+4
	.word	.LANCHOR0+3628
	.word	.LANCHOR2
	.word	.LC109
	.word	.LC110
	.word	.LC111
	.word	.LC112
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
	ldr	r4, .L1437
	mov	r7, #3856
	ldr	r6, .L1437+4
	ldr	r3, [r4, #-956]
	ldr	r5, [r4, #-932]
	str	r3, [r4, #192]
	ldr	r3, .L1437+8
	str	r5, [r4, #196]
	ldrh	r3, [r4, r3]
	strh	r3, [r5, #2]	@ movhi
	ldr	r3, .L1437+12
	strh	r3, [r5, #0]	@ movhi
	ldr	r3, [r4, #-1764]
	str	r3, [r5, #4]
	mov	r3, #0
	str	r3, [r5, #8]
	str	r3, [r5, #12]
	ldr	r3, .L1437+16
	str	r3, [r4, #-2072]
	ldr	r3, .L1437+20
	str	r3, [r4, #-2068]
	ldr	r3, .L1437+24
	ldrh	r2, [r4, r3]
	ldr	r3, .L1437+28
	strh	r2, [r4, r3]	@ movhi
	movw	r3, #3870
	ldrh	r3, [r6, r3]
	strb	r3, [r4, #-2062]
	ldr	r3, .L1437+32
	ldrh	r2, [r4, r3]
	sub	r3, r3, #62
	strh	r2, [r4, r3]	@ movhi
	add	r3, r3, #64
	ldrh	r2, [r4, r3]
	ldrb	r3, [r4, #-1990]	@ zero_extendqisi2
	orr	r2, r3, r2, asl #6
	ldr	r3, .L1437+36
	strh	r2, [r4, r3]	@ movhi
	ldrb	r3, [r4, #-1988]	@ zero_extendqisi2
	strb	r3, [r4, #-2061]
	ldr	r3, .L1437+40
	ldrh	r2, [r4, r3]
	sub	r3, r3, #106
	strh	r2, [r4, r3]	@ movhi
	add	r3, r3, #108
	ldrh	r2, [r4, r3]
	ldrb	r3, [r4, #-1942]	@ zero_extendqisi2
	orr	r2, r3, r2, asl #6
	ldr	r3, .L1437+44
	strh	r2, [r4, r3]	@ movhi
	ldrb	r3, [r4, #-1940]	@ zero_extendqisi2
	strb	r3, [r4, #-2060]
	ldr	r3, .L1437+48
	ldrh	r2, [r4, r3]
	sub	r3, r3, #150
	strh	r2, [r4, r3]	@ movhi
	add	r3, r3, #152
	ldr	r0, [r4, #192]
	ldrh	r2, [r4, r3]
	ldrb	r3, [r4, #-1894]	@ zero_extendqisi2
	orr	r2, r3, r2, asl #6
	ldr	r3, .L1437+52
	strh	r2, [r4, r3]	@ movhi
	ldrb	r3, [r4, #-1892]	@ zero_extendqisi2
	strb	r3, [r4, #-2059]
	ldr	r3, [r4, #-1796]
	str	r3, [r4, #-2040]
	ldr	r3, [r4, #-1804]
	str	r3, [r4, #-2032]
	ldr	r3, [r4, #-1800]
	str	r3, [r4, #-2036]
	ldr	r3, .L1437+56
	ldrh	r2, [r4, r3]
	ldr	r3, .L1437+60
	strh	r2, [r4, r3]	@ movhi
	add	r3, r3, #888
	ldrh	r2, [r4, r3]
	ldr	r3, .L1437+64
	strh	r2, [r4, r3]	@ movhi
	add	r3, r3, #5952
	ldrh	r2, [r6, r3]
	bl	ftl_memset
	ldr	r1, .L1437+68
	mov	r2, #48
	ldr	r0, [r4, #192]
	bl	memcpy
	ldrh	r2, [r6, r7]
	ldr	r0, [r4, #192]
	ldr	r1, [r4, #-2016]
	mov	r2, r2, asl #1
	add	r0, r0, #48
	bl	memcpy
	ldrh	r2, [r6, r7]
	ldr	r3, [r4, #192]
	add	r0, r2, #24
	ldr	r1, [r4, #-1840]
	mov	r2, r2, lsr #3
	mov	r0, r0, lsr #1
	add	r2, r2, #4
	add	r0, r3, r0, asl #2
	bl	memcpy
	movw	r3, #3960
	ldrh	r3, [r6, r3]
	cmp	r3, #0
	beq	.L1426
	ldrh	r3, [r6, r7]
	ldr	r1, [r4, #192]
	mov	r0, r3, lsr #3
	add	r0, r0, r3, asl #1
	mov	r3, #3952
	add	r0, r0, #52
	ldrh	r2, [r6, r3]
	ubfx	r0, r0, #2, #14
	mov	r2, r2, asl #2
	add	r0, r1, r0, asl #2
	ldr	r1, [r4, #-1028]
	bl	memcpy
.L1426:
	mov	r0, #0
	ldr	r4, .L1437
	bl	FtlUpdateVaildLpn
	ldr	sl, .L1437+8
	ldr	r6, .L1437+72
	mov	r7, #0
	movw	r8, #65535
.L1436:
	ldr	r3, [r4, #-956]
	ldrh	r2, [r4, sl]
	ldr	r0, .L1437+4
	str	r3, [r4, #192]
	ldr	r3, [r4, #-932]
	str	r3, [r4, #196]
	ldrh	r3, [r4, r6]
	orr	r1, r3, r2, asl #10
	str	r1, [r4, #188]
	movw	r1, #3918
	ldrh	r1, [r0, r1]
	sub	r1, r1, #1
	cmp	r3, r1
	blt	.L1428
	ldr	r3, .L1437+76
	ldrh	r8, [r4, r3]
	strh	r2, [r4, r3]	@ movhi
	mov	r3, #0
	strh	r3, [r4, r6]	@ movhi
	bl	FtlFreeSysBlkQueueOut
	ldr	r3, [r4, #-1804]
	add	r2, r3, #1
	str	r2, [r4, #-1804]
	str	r3, [r4, #-1764]
	mov	r2, r0, asl #10
	strh	r0, [r4, sl]	@ movhi
	str	r2, [r4, #188]
	str	r3, [r5, #4]
	strh	r0, [r5, #2]	@ movhi
.L1428:
	mov	r1, #1
	ldr	r0, .L1437+80
	mov	r2, r1
	mov	r3, r1
	bl	FlashProgPages
	ldrh	r3, [r4, r6]
	ldr	r2, [r4, #184]
	add	r3, r3, #1
	cmn	r2, #1
	uxth	r3, r3
	strh	r3, [r4, r6]	@ movhi
	bne	.L1429
	cmp	r3, #1
	add	r7, r7, #1
	ldreq	r2, .L1437+4
	movweq	r3, #3918
	uxth	r7, r7
	ldreqh	r3, [r2, r3]
	subeq	r3, r3, #1
	streqh	r3, [r4, r6]	@ movhi
	cmp	r7, #3
	bls	.L1436
	ldr	r3, .L1437
	mov	r2, r7
	ldr	r0, .L1437+84
	ldr	r1, [r3, #188]
	bl	printk
	ldr	r3, .L1437+4
	mov	r2, #1
	str	r2, [r3, #4040]
	ldmfd	sp!, {r3, r4, r5, r6, r7, r8, sl, pc}
.L1429:
	cmp	r3, #1
	beq	.L1436
	cmp	r2, #256
	beq	.L1436
	movw	r3, #65535
	cmp	r8, r3
	ldmeqfd	sp!, {r3, r4, r5, r6, r7, r8, sl, pc}
	mov	r0, r8
	mov	r1, #1
	bl	FtlFreeSysBlkQueueIn
	ldmfd	sp!, {r3, r4, r5, r6, r7, r8, sl, pc}
.L1438:
	.align	2
.L1437:
	.word	.LANCHOR2
	.word	.LANCHOR0
	.word	-1772
	.word	-3932
	.word	1179929683
	.word	1342177360
	.word	-1766
	.word	-2064
	.word	-1996
	.word	-2056
	.word	-1948
	.word	-2052
	.word	-1900
	.word	-2048
	.word	-1142
	.word	-2028
	.word	-2026
	.word	.LANCHOR2-2072
	.word	-1770
	.word	-1768
	.word	.LANCHOR2+184
	.word	.LC113
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
	ldr	r3, .L1441
	ldr	r3, [r3, #4040]
	cmp	r3, #0
	bne	.L1440
	bl	FtlVpcTblFlush.part.14
.L1440:
	mov	r0, #0
	ldmfd	sp!, {r3, pc}
.L1442:
	.align	2
.L1441:
	.word	.LANCHOR0
	.fnend
	.size	FtlVpcTblFlush, .-FtlVpcTblFlush
	.section	.text.unlikely
	.align	2
	.type	FtlBbmTblFlush.part.16, %function
FtlBbmTblFlush.part.16:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	stmfd	sp!, {r0, r1, r2, r4, r5, r6, r7, r8, r9, sl, fp, lr}
	.save {r0, r1, r2, r4, r5, r6, r7, r8, r9, sl, fp, lr}
	mov	r1, #0
	ldr	r7, .L1456
	mov	r5, #0
	ldr	sl, .L1456+4
	movw	r9, #3870
	ldr	r8, .L1456+8
	ldr	r3, [r7, #-932]
	ldr	r0, [r7, #-956]
	str	r3, [r7, #196]
	movw	r3, #3926
	str	r0, [r7, #192]
	ldrh	r2, [sl, r3]
	bl	ftl_memset
	b	.L1444
.L1445:
	ldr	r3, .L1456+12
	ldr	r1, [r8, #4]!
	ldrh	r2, [r7, r3]
	ldr	r3, [r7, #192]
	mul	r0, r2, r5
	mov	r2, r2, asl #2
	add	r5, r5, #1
	add	r0, r3, r0, asl #2
	bl	memcpy
.L1444:
	ldrh	r3, [sl, r9]
	ldr	r4, .L1456+4
	cmp	r5, r3
	ldr	r6, .L1456
	blt	.L1445
	ldr	r5, [r6, #196]
	mov	r1, #255
	mov	r2, #16
	mov	r7, #0
	mov	fp, r7
	mov	r0, r5
	bl	ftl_memset
	ldr	r3, .L1456+16
	strh	r3, [r5, #0]	@ movhi
	ldr	r3, [r4, #3988]
	str	r3, [r5, #4]
	movw	r3, #3980
	ldrh	r3, [r4, r3]
	strh	r3, [r5, #2]	@ movhi
	mov	r3, #3984
	ldrh	r3, [r4, r3]
	strh	r3, [r5, #8]	@ movhi
	movw	r3, #3986
	ldrh	r3, [r4, r3]
	strh	r3, [r5, #10]	@ movhi
	ldr	r3, [r4, #3844]
	strh	r3, [r5, #12]	@ movhi
	b	.L1454
.L1451:
	mov	fp, #1
.L1454:
	ldr	r3, [r6, #-956]
	movw	r8, #3980
	ldr	r4, .L1456+4
	mov	r1, #0
	movw	sl, #3982
	str	r1, [r6, #184]
	str	r3, [r6, #192]
	mov	r9, #3984
	ldr	r3, [r6, #-932]
	ldrh	r1, [r4, r8]
	ldrh	r2, [r4, sl]
	str	r3, [r6, #196]
	ldrh	r0, [r5, #10]
	orr	r3, r2, r1, asl #10
	str	r3, [r6, #188]
	ldrh	r3, [r4, r9]
	str	r0, [sp, #0]
	ldr	r0, .L1456+20
	bl	printk
	movw	r3, #3918
	ldrh	r3, [r4, r3]
	ldrh	r2, [r4, sl]
	sub	r3, r3, #1
	cmp	r2, r3
	blt	.L1447
	ldr	r3, [r4, #3988]
	mov	r1, #0	@ movhi
	ldrh	r2, [r4, r8]
	add	r3, r3, #1
	ldr	r0, [r4, #4044]
	str	r3, [r4, #3988]
	str	r3, [r5, #4]
	ldrh	r3, [r4, r9]
	strh	r1, [r4, sl]	@ movhi
	mov	r1, #1
	strh	r2, [r5, #8]	@ movhi
	strh	r2, [r4, r9]	@ movhi
	mov	r2, r1
	strh	r3, [r4, r8]	@ movhi
	mov	r3, r3, asl #10
	str	r3, [r6, #188]
	str	r3, [r0, #4]
	bl	FlashEraseBlocks
.L1447:
	ldr	r4, .L1456
	mov	r1, #1
	ldr	r8, .L1456+4
	mov	r2, r1
	mov	r3, r1
	add	r0, r4, #184
	bl	FlashProgPages
	movw	r3, #3982
	ldrh	r2, [r8, r3]
	add	r2, r2, #1
	strh	r2, [r8, r3]	@ movhi
	ldr	r3, [r6, #184]
	cmn	r3, #1
	bne	.L1448
	add	r7, r7, #1
	ldr	r0, .L1456+24
	ldr	r1, [r6, #188]
	uxth	r7, r7
	bl	printk
	cmp	r7, #3
	bls	.L1454
	ldr	r0, .L1456+28
	mov	r2, r7
	ldr	r1, [r4, #188]
	bl	printk
	mov	r3, #1
	str	r3, [r8, #4040]
	b	.L1455
.L1448:
	cmp	fp, #0
	beq	.L1451
.L1455:
	ldmfd	sp!, {r1, r2, r3, r4, r5, r6, r7, r8, r9, sl, fp, pc}
.L1457:
	.align	2
.L1456:
	.word	.LANCHOR2
	.word	.LANCHOR0
	.word	.LANCHOR0+4004
	.word	-1024
	.word	-3887
	.word	.LC114
	.word	.LC115
	.word	.LC116
	.fnend
	.size	FtlBbmTblFlush.part.16, .-FtlBbmTblFlush.part.16
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
	ldr	r3, .L1460
	ldr	r3, [r3, #4040]
	cmp	r3, #0
	bne	.L1459
	bl	FtlBbmTblFlush.part.16
.L1459:
	mov	r0, #0
	ldmfd	sp!, {r3, pc}
.L1461:
	.align	2
.L1460:
	.word	.LANCHOR0
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
	ldr	r4, .L1472
	ldr	r3, .L1472+4
	ldrh	r3, [r4, r3]
	cmp	r3, #0
	movne	r6, #0
	movne	sl, r4
	bne	.L1463
	b	.L1464
.L1470:
	add	r3, r3, r6
	mov	r1, r9
	mov	r5, #0
	ldr	r7, .L1472+4
	ldrb	r0, [r3, #3874]	@ zero_extendqisi2
	bl	V2P_block
	mov	r8, r0
	b	.L1465
.L1469:
	add	r3, r4, r5, asl #1
	sub	r3, r3, #1120
	sub	r3, r3, #12
	ldrh	r3, [r3, #0]
	cmp	r3, r8
	bne	.L1466
	mov	r1, r8
	ldr	r0, .L1472+8
	bl	printk
	mov	r0, r8
	bl	FtlBbmMapBadBlock
	bl	FtlBbmTblFlush
	ldrh	r1, [r4, r7]
	mov	r3, r5
	b	.L1467
.L1468:
	add	r0, r3, #1
	add	r3, r4, r3, asl #1
	sub	r3, r3, #1120
	add	r2, r4, r0, asl #1
	sub	r2, r2, #1120
	sub	r2, r2, #12
	ldrh	r2, [r2, #0]
	strh	r2, [r3, #-12]	@ movhi
	uxth	r3, r0
.L1467:
	cmp	r3, r1
	bcc	.L1468
	sub	r1, r1, #1
	strh	r1, [sl, r7]	@ movhi
.L1466:
	add	r5, r5, #1
	uxth	r5, r5
.L1465:
	ldrh	r3, [r4, r7]
	cmp	r3, r5
	bhi	.L1469
	add	r6, r6, #1
	uxth	r6, r6
.L1463:
	ldr	r3, .L1472+12
	movw	r2, #3848
	ldrh	r2, [r3, r2]
	cmp	r2, r6
	bhi	.L1470
	bl	FtlGcReFreshBadBlk
.L1464:
	mov	r0, #0
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, sl, pc}
.L1473:
	.align	2
.L1472:
	.word	.LANCHOR2
	.word	-1134
	.word	.LC117
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
	ldr	r3, .L1482
	mov	r4, r0
	ldr	r1, [r3, #-2016]
	ldrh	r2, [r1, r2]
	cmp	r2, #0
	bne	.L1475
	ldr	r1, .L1482+4
	ldrh	r0, [r3, r1]
	cmp	r0, r4
	mvneq	r2, #0
	streqh	r2, [r3, r1]	@ movhi
	beq	.L1477
	ldr	r1, .L1482+8
	ldrh	r1, [r3, r1]
	cmp	r1, r4
	beq	.L1478
	ldr	r1, .L1482+12
	ldrh	r1, [r3, r1]
	cmp	r1, r4
	beq	.L1478
	ldr	r1, .L1482+16
	ldrh	r3, [r3, r1]
	cmp	r3, r4
	beq	.L1478
.L1477:
	mov	r1, r4
	ldr	r0, .L1482+20
	bl	List_remove_node
	ldr	r5, .L1482
	ldr	r3, .L1482+24
	mov	r0, r4
	ldrh	r2, [r5, r3]
	sub	r2, r2, #1
	strh	r2, [r5, r3]	@ movhi
	bl	free_data_superblock
	mov	r0, r4
	bl	FtlGcFreeBadSuperBlk
	mov	r2, #1
	b	.L1478
.L1475:
	bl	List_update_data_list
	mov	r2, #0
.L1478:
	mov	r0, r2
	ldmfd	sp!, {r3, r4, r5, pc}
.L1483:
	.align	2
.L1482:
	.word	.LANCHOR2
	.word	-1760
	.word	-1996
	.word	-1948
	.word	-1900
	.word	.LANCHOR2-2020
	.word	-2008
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
	beq	.L1485
	ldr	r2, .L1490
	mov	r3, r0, asl #1
	ldr	r2, [r2, #-2016]
	ldrh	r1, [r2, r3]
	cmp	r1, #0
	subne	r1, r1, #1
	strneh	r1, [r2, r3]	@ movhi
	bne	.L1485
	ldmfd	sp!, {r4, r5, r6, lr}
	b	decrement_vpc_count.part.18
.L1485:
	ldr	r6, .L1490
	movw	r3, #65535
	ldr	r5, .L1490+4
	ldrh	r0, [r6, r5]
	cmp	r0, r3
	streqh	r4, [r6, r5]	@ movhi
	moveq	r0, #0
	ldmeqfd	sp!, {r4, r5, r6, pc}
	cmp	r0, r4
	beq	.L1489
	bl	update_vpc_list
	strh	r4, [r6, r5]	@ movhi
	adds	r0, r0, #0
	movne	r0, #1
	ldmfd	sp!, {r4, r5, r6, pc}
.L1489:
	mov	r0, #0
	ldmfd	sp!, {r4, r5, r6, pc}
.L1491:
	.align	2
.L1490:
	.word	.LANCHOR2
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
	ldr	r5, .L1504
	ldr	r7, .L1504+4
	add	r3, r0, r3, asl #1
	ldrh	r3, [r3, #16]
	b	.L1493
.L1494:
	ldrb	r3, [r4, #6]	@ zero_extendqisi2
	movw	r2, #3848
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
.L1493:
	cmp	r3, r6
	beq	.L1494
	ldrb	r2, [r4, #8]	@ zero_extendqisi2
	cmp	r2, #1
	bne	.L1496
	ldrb	r2, [r5, #928]	@ zero_extendqisi2
	cmp	r2, #0
	bne	.L1496
	ldrh	r2, [r4, #2]
	add	r2, r7, r2, asl #1
	sub	r2, r2, #888
	ldrh	r2, [r2, #0]
	cmp	r2, r6
	bne	.L1496
	ldrh	r3, [r4, #4]
	ldrh	r0, [r4, #0]
	sub	r3, r3, #1
	strh	r3, [r4, #4]	@ movhi
	bl	decrement_vpc_count
	b	.L1494
.L1496:
	ldrh	r6, [r4, #2]
	movw	r5, #65535
	mov	r7, r5
	orr	r6, r6, r3, asl #10
	ldrh	r3, [r4, #4]
	sub	r3, r3, #1
	strh	r3, [r4, #4]	@ movhi
.L1501:
	ldr	r2, .L1504
	movw	r3, #3848
	ldrh	r2, [r2, r3]
	ldrb	r3, [r4, #6]	@ zero_extendqisi2
.L1498:
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
	beq	.L1498
	strb	r3, [r4, #6]
	ldrb	r3, [r4, #8]	@ zero_extendqisi2
	cmp	r3, #1
	bne	.L1499
	ldr	r3, .L1504
	ldrb	r2, [r3, #928]	@ zero_extendqisi2
	cmp	r2, #0
	ldrh	r2, [r4, #2]
	bne	.L1502
	ldr	r3, .L1504+4
	add	r2, r3, r2, asl #1
	sub	r2, r2, #888
	ldrh	r3, [r2, #0]
	cmp	r3, r7
	bne	.L1499
	ldrh	r3, [r4, #4]
	cmp	r3, #0
	beq	.L1499
	sub	r3, r3, #1
	ldrh	r0, [r4, #0]
	strh	r3, [r4, #4]	@ movhi
	bl	decrement_vpc_count
	b	.L1501
.L1502:
	movw	r1, #3918
	ldrh	r1, [r3, r1]
	cmp	r2, r1
	bcc	.L1499
	ldr	r1, .L1504+4
	ldrh	r2, [r4, #0]
	ldrh	r0, [r4, #4]
	ldr	r1, [r1, #-2016]
	mov	r2, r2, asl #1
	ldrh	ip, [r1, r2]
	rsb	r0, r0, ip
	strh	r0, [r1, r2]	@ movhi
	mov	r2, #0
	strh	r2, [r4, #4]	@ movhi
	mov	r1, r2	@ movhi
	movw	r2, #3916
	ldrh	r3, [r3, r2]
	strb	r1, [r4, #6]
	strh	r3, [r4, #2]	@ movhi
.L1499:
	mov	r0, r6
	ldmfd	sp!, {r3, r4, r5, r6, r7, pc}
.L1505:
	.align	2
.L1504:
	.word	.LANCHOR0
	.word	.LANCHOR2
	.fnend
	.size	get_new_active_ppa, .-get_new_active_ppa
	.align	2
	.type	FtlSlcSuperblockCheck.part.19, %function
FtlSlcSuperblockCheck.part.19:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	stmfd	sp!, {r3, r4, r5, r6, r7, lr}
	.save {r3, r4, r5, r6, r7, lr}
	mov	r4, r0
	ldrb	r3, [r0, #6]	@ zero_extendqisi2
	movw	r6, #65535
	ldr	r5, .L1512
	ldr	r7, .L1512+4
	add	r3, r0, r3, asl #1
	ldrh	r3, [r3, #16]
	b	.L1507
.L1508:
	ldrb	r3, [r4, #6]	@ zero_extendqisi2
	movw	r2, #3848
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
.L1507:
	cmp	r3, r6
	beq	.L1508
	ldrb	r2, [r4, #8]	@ zero_extendqisi2
	cmp	r2, #1
	bne	.L1510
	ldrb	r3, [r5, #928]	@ zero_extendqisi2
	cmp	r3, #0
	bne	.L1510
	ldrh	r3, [r4, #2]
	add	r3, r7, r3, asl #1
	sub	r3, r3, #888
	ldrh	r3, [r3, #0]
	cmp	r3, r6
	bne	.L1510
	ldrh	r3, [r4, #4]
	ldrh	r0, [r4, #0]
	sub	r3, r3, #1
	strh	r3, [r4, #4]	@ movhi
	bl	decrement_vpc_count
	ldrh	r3, [r4, #4]
	cmp	r3, #0
	bne	.L1508
	ldrh	r2, [r4, #2]
	strb	r3, [r4, #6]
	add	r2, r2, #1
	strh	r2, [r4, #2]	@ movhi
	ldmfd	sp!, {r3, r4, r5, r6, r7, pc}
.L1510:
	ldr	r3, .L1512
	ldrb	r1, [r3, #928]	@ zero_extendqisi2
	cmp	r1, #0
	ldmeqfd	sp!, {r3, r4, r5, r6, r7, pc}
	cmp	r2, #1
	ldmnefd	sp!, {r3, r4, r5, r6, r7, pc}
	movw	r2, #3918
	ldrh	r1, [r4, #2]
	ldrh	r2, [r3, r2]
	cmp	r1, r2
	ldmccfd	sp!, {r3, r4, r5, r6, r7, pc}
	ldr	r1, .L1512+4
	ldrh	r2, [r4, #0]
	ldrh	r0, [r4, #4]
	ldr	r1, [r1, #-2016]
	mov	r2, r2, asl #1
	ldrh	ip, [r1, r2]
	rsb	r0, r0, ip
	strh	r0, [r1, r2]	@ movhi
	mov	r2, #0
	strh	r2, [r4, #4]	@ movhi
	mov	r1, r2	@ movhi
	movw	r2, #3916
	ldrh	r3, [r3, r2]
	strb	r1, [r4, #6]
	strh	r3, [r4, #2]	@ movhi
	ldmfd	sp!, {r3, r4, r5, r6, r7, pc}
.L1513:
	.align	2
.L1512:
	.word	.LANCHOR0
	.word	.LANCHOR2
	.fnend
	.size	FtlSlcSuperblockCheck.part.19, .-FtlSlcSuperblockCheck.part.19
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
	b	FtlSlcSuperblockCheck.part.19
	.fnend
	.size	FtlSlcSuperblockCheck, .-FtlSlcSuperblockCheck
	.align	2
	.type	allocate_data_superblock.part.20, %function
allocate_data_superblock.part.20:
	.fnstart
	@ args = 0, pretend = 0, frame = 24
	@ frame_needed = 0, uses_anonymous_args = 0
	stmfd	sp!, {r4, r5, r6, r7, r8, r9, sl, fp, lr}
	.save {r4, r5, r6, r7, r8, r9, sl, fp, lr}
	.pad #28
	sub	sp, sp, #28
	ldr	r6, .L1555
	mov	r4, r0
	ldr	r5, .L1555+4
	mov	sl, r6
.L1550:
	ldr	r3, .L1555+8
	cmp	r4, r3
	bne	.L1518
	ldr	r3, .L1555+12
	ldrh	r2, [r5, r3]
	ldr	r3, [r5, #-1184]
	mov	r1, r2, lsr #1
	mul	ip, r3, r2
	add	r0, r1, #1
	add	r0, r0, ip, lsr #2
	ldr	ip, [r6, #3840]
	cmp	ip, #0
	uxth	r0, r0
	beq	.L1542
	ldr	ip, [r5, #-1780]
	cmp	ip, #39
	bhi	.L1542
	cmp	ip, #2
	bls	.L1547
	tst	r2, #1
	beq	.L1519
	cmp	r3, #0
	moveq	r1, r3
	beq	.L1520
	b	.L1519
.L1518:
	ldrb	r3, [r4, #8]	@ zero_extendqisi2
	cmp	r3, #1
	bne	.L1547
	movw	r3, #3868
	ldrh	r3, [r6, r3]
	cmp	r3, #1
	beq	.L1547
	ldrb	r3, [r6, #928]	@ zero_extendqisi2
	cmp	r3, #0
	bne	.L1547
	sub	r3, r3, #2000
	ldr	r2, [r6, #3840]
	ldrh	r3, [r5, r3]
	cmp	r2, #0
	mov	r1, r3, lsr #3
	beq	.L1519
	ldr	r2, [r5, #-1780]
	cmp	r2, #1
	movls	r1, #7
	mulls	r1, r1, r3
	movls	r1, r1, lsr #3
	b	.L1519
.L1542:
	mov	r1, r0
.L1519:
	cmp	r1, #0
	subne	r1, r1, #1
	uxthne	r1, r1
	b	.L1520
.L1547:
	mov	r1, #0
.L1520:
	ldrb	r2, [r4, #8]	@ zero_extendqisi2
	ldr	r0, .L1555+16
	bl	List_pop_index_node
	ldr	r3, .L1555+12
	ldrh	r2, [r5, r3]
	sub	r2, r2, #1
	strh	r2, [r5, r3]	@ movhi
	mov	r3, #3856
	ldrh	r3, [r6, r3]
	uxth	r9, r0
	cmp	r3, r9
	bls	.L1550
	ldr	r3, [r5, #-2016]
	mov	r7, r9, asl #1
	ldrh	r8, [r3, r7]
	cmp	r8, #0
	bne	.L1550
	strh	r9, [r4, #0]	@ movhi
	mov	r0, r4
	bl	make_superblock
	ldrb	r3, [r4, #7]	@ zero_extendqisi2
	cmp	r3, #0
	beq	.L1551
	movw	r3, #3848
	ldr	ip, [r6, #4044]
	ldrh	r3, [r6, r3]
	mov	r1, r4
	str	r4, [sp, #16]
	mov	r2, r8
	mov	r0, r8
	stmia	sp, {r4, ip}
	str	r3, [sp, #20]
	mov	r3, ip
	b	.L1523
.L1525:
	str	r0, [r3, #8]
	movw	r4, #65535
	str	r0, [r3, #12]
	ldrh	lr, [r1, #16]
	cmp	lr, r4
	beq	.L1524
	ldr	r4, [sp, #4]
	mov	ip, #36
	mov	lr, lr, asl #10
	mla	fp, ip, r8, r4
	add	r8, r8, #1
	uxth	r8, r8
	str	lr, [fp, #4]
.L1524:
	add	r2, r2, #1
	add	r3, r3, #36
	add	r1, r1, #2
	uxth	r2, r2
.L1523:
	ldr	fp, [sp, #20]
	cmp	r2, fp
	bne	.L1525
	ldr	r3, [r6, #3840]
	ldr	r4, [sp, #0]
	cmp	r3, #0
	beq	.L1526
	ldr	r3, .L1555+20
	cmp	r4, r3
	bne	.L1526
	ldr	r3, [r6, #4048]
	ldrh	r3, [r3, r7]
	cmp	r3, #40
	movhi	r3, #0
	strhib	r3, [r5, #-1988]
.L1526:
	ldrb	r3, [r4, #8]	@ zero_extendqisi2
	cmp	r3, #0
	ldr	r3, [sl, #4048]
	ldrh	r2, [r3, r7]
	bne	.L1527
	cmp	r2, #0
	mov	r0, r9
	movwne	r1, #3906
	moveq	r2, #2
	ldrneh	r1, [r6, r1]
	addne	r2, r2, r1
	strh	r2, [r3, r7]	@ movhi
	ldr	r3, [r5, #-1796]
	mov	r1, #0
	add	r3, r3, #1
	str	r3, [r5, #-1796]
	b	.L1553
.L1527:
	add	r2, r2, #1
	strh	r2, [r3, r7]	@ movhi
	ldr	r3, [r5, #-1792]
	mov	r0, r9
	mov	r1, #1
	add	r3, r3, #1
	str	r3, [r5, #-1792]
.L1553:
	bl	ftl_set_blk_mode
	ldr	r3, [r6, #4048]
	ldr	fp, .L1555+4
	ldr	r2, [r5, #-1784]
	ldrh	r3, [r3, r7]
	ldr	r0, [fp, #-1796]
	cmp	r3, r2
	strhi	r3, [r5, #-1784]
	movw	r3, #3906
	ldrh	r2, [sl, r3]
	ldr	r3, [fp, #-1792]
	mla	r0, r0, r2, r3
	mov	r3, #3856
	ldrh	r1, [sl, r3]
	bl	__aeabi_uidiv
	ldr	r3, [fp, #-916]
	ldr	r2, [r3, #16]
	add	r2, r2, #1
	str	r2, [r3, #16]
	ldr	r3, [sl, #4044]
	mov	r2, #0
	str	r0, [fp, #-1788]
	b	.L1532
.L1533:
	add	r2, r2, #1
	ldr	r1, [r3, #-32]
	uxth	r2, r2
	bic	r1, r1, #1020
	bic	r1, r1, #3
	str	r1, [r3, #-32]
.L1532:
	cmp	r2, r8
	add	r3, r3, #36
	bne	.L1533
	ldrb	r3, [r6, #928]	@ zero_extendqisi2
	cmp	r3, #0
	beq	.L1534
	ldrb	r3, [r4, #8]	@ zero_extendqisi2
	mov	r2, r8
	ldr	r0, [r6, #4044]
	cmp	r3, #1
	moveq	r1, #0
	movne	r1, #1
	bl	FlashEraseBlocks
.L1534:
	mov	r2, r8
	ldr	r0, [sl, #4044]
	ldrb	r1, [r4, #8]	@ zero_extendqisi2
	bl	FlashEraseBlocks
	mov	r2, #36
	mul	r2, r2, r8
	mov	r8, #0
	mov	r3, r9
	mov	fp, r8
	mov	r9, r2
	mov	r2, r7
	mov	r7, r4
	b	.L1536
.L1538:
	ldr	r0, [r6, #4044]
	add	r1, r0, r8
	ldr	r4, [r0, r8]
	cmn	r4, #1
	bne	.L1537
	ldr	r0, [r1, #4]
	add	fp, fp, #1
	str	r2, [sp, #8]
	ubfx	r0, r0, #10, #16
	str	r3, [sp, #12]
	bl	FtlBbmMapBadBlock
	ldr	ip, [sp, #16]
	strh	r4, [ip, #16]	@ movhi
	ldrb	r1, [r7, #7]	@ zero_extendqisi2
	sub	r1, r1, #1
	strb	r1, [r7, #7]
	ldr	r3, [sp, #12]
	ldr	r2, [sp, #8]
.L1537:
	ldr	r4, [sp, #16]
	add	r8, r8, #36
	add	r4, r4, #2
	str	r4, [sp, #16]
.L1536:
	cmp	r8, r9
	bne	.L1538
	cmp	fp, #0
	mov	r4, r7
	mov	r9, r3
	mov	r7, r2
	beq	.L1539
	mov	r0, r3
	bl	update_multiplier_value
	bl	FtlBbmTblFlush
.L1539:
	ldrb	r3, [r4, #7]	@ zero_extendqisi2
	cmp	r3, #0
	bne	.L1540
.L1551:
	ldr	r3, [r5, #-2016]
	mvn	r2, #0
	strh	r2, [r3, r7]	@ movhi
	b	.L1550
.L1540:
	ldr	r1, .L1555
	movw	r2, #3916
	ldrh	r2, [r1, r2]
	strh	r9, [r4, #0]	@ movhi
	mul	r2, r2, r3
	mov	r3, #0
	strh	r3, [r4, #2]	@ movhi
	strb	r3, [r4, #6]
	ldr	r3, .L1555+4
	uxth	r2, r2
	strh	r2, [r4, #4]	@ movhi
	ldr	r1, [r3, #-1804]
	str	r1, [r4, #12]
	add	r1, r1, #1
	str	r1, [r3, #-1804]
	ldrh	r0, [r4, #0]
	ldr	r1, [r3, #-2016]
	mov	r3, r0, asl #1
	strh	r2, [r1, r3]	@ movhi
	add	sp, sp, #28
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, sl, fp, pc}
.L1556:
	.align	2
.L1555:
	.word	.LANCHOR0
	.word	.LANCHOR2
	.word	.LANCHOR2-1900
	.word	-2000
	.word	.LANCHOR2-2004
	.word	.LANCHOR2-1996
	.fnend
	.size	allocate_data_superblock.part.20, .-allocate_data_superblock.part.20
	.align	2
	.global	allocate_data_superblock
	.type	allocate_data_superblock, %function
allocate_data_superblock:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	stmfd	sp!, {r3, lr}
	.save {r3, lr}
	ldr	r3, .L1559
	ldr	r3, [r3, #4040]
	cmp	r3, #0
	bne	.L1558
	bl	allocate_data_superblock.part.20
.L1558:
	mov	r0, #0
	ldmfd	sp!, {r3, pc}
.L1560:
	.align	2
.L1559:
	.word	.LANCHOR0
	.fnend
	.size	allocate_data_superblock, .-allocate_data_superblock
	.align	2
	.global	FtlSuperblockPowerLostFix
	.type	FtlSuperblockPowerLostFix, %function
FtlSuperblockPowerLostFix:
	.fnstart
	@ args = 0, pretend = 0, frame = 40
	@ frame_needed = 0, uses_anonymous_args = 0
	ldr	r2, .L1571
	stmfd	sp!, {r4, r5, r6, r7, r8, sl, lr}
	.save {r4, r5, r6, r7, r8, sl, lr}
	mov	r4, r0
	ldr	r3, [r2, #4040]
	.pad #44
	sub	sp, sp, #44
	cmp	r3, #0
	bne	.L1561
	ldrb	r7, [r2, #928]	@ zero_extendqisi2
	cmp	r7, #0
	beq	.L1570
	ldrb	r7, [r0, #8]	@ zero_extendqisi2
	cmp	r7, #1
	ldreqh	r8, [r0, #4]
	beq	.L1563
	mov	r7, r3
.L1570:
	mov	r8, #12
.L1563:
	ldr	r6, .L1571+4
	mvn	r3, #0
	str	r3, [sp, #20]
	mov	sl, #0
	movw	r2, #61589
	ldr	r5, [r6, #-932]
	ldr	r3, [r6, #-956]
	str	r5, [sp, #16]
	str	r3, [sp, #12]
	mvn	r3, #2
	str	r3, [r5, #8]
	mvn	r3, #1
	str	r3, [r5, #12]
	ldrh	r3, [r4, #0]
	strh	sl, [r5, #0]	@ movhi
	strh	r3, [r5, #2]	@ movhi
	ldr	r3, [r6, #-956]
	str	r2, [r3, #0]
	ldr	r2, .L1571+8
	ldr	r3, [r6, #-956]
	str	r2, [r3, #4]
	b	.L1564
.L1567:
	ldrh	r3, [r4, #4]
	cmp	r3, #0
	beq	.L1565
	mov	r0, r4
	bl	get_new_active_ppa
	cmn	r0, #1
	str	r0, [sp, #8]
	beq	.L1565
	ldr	r3, [r6, #-1800]
	mov	r1, #1
	mov	r2, r7
	add	r0, sp, #4
	sub	r8, r8, #1
	str	r3, [r5, #4]
	add	r3, r3, #1
	cmn	r3, #1
	moveq	r3, sl
	str	r3, [r6, #-1800]
	mov	r3, #0
	bl	FlashProgPages
	ldrh	r0, [r4, #0]
	bl	decrement_vpc_count
.L1564:
	cmp	r8, #0
	bne	.L1567
.L1565:
	ldr	r2, .L1571+4
	ldrh	r3, [r4, #0]
	ldrh	r1, [r4, #4]
	ldr	r2, [r2, #-2016]
	mov	r3, r3, asl #1
	ldrh	r0, [r2, r3]
	rsb	r1, r1, r0
	strh	r1, [r2, r3]	@ movhi
	ldr	r2, .L1571
	movw	r3, #3916
	ldrh	r3, [r2, r3]
	strh	r3, [r4, #2]	@ movhi
	mov	r3, #0
	strb	r3, [r4, #6]
	strh	r3, [r4, #4]	@ movhi
.L1561:
	add	sp, sp, #44
	ldmfd	sp!, {r4, r5, r6, r7, r8, sl, pc}
.L1572:
	.align	2
.L1571:
	.word	.LANCHOR0
	.word	.LANCHOR2
	.word	305419896
	.fnend
	.size	FtlSuperblockPowerLostFix, .-FtlSuperblockPowerLostFix
	.align	2
	.global	FtlLowFormatEraseBlock
	.type	FtlLowFormatEraseBlock, %function
FtlLowFormatEraseBlock:
	.fnstart
	@ args = 0, pretend = 0, frame = 24
	@ frame_needed = 0, uses_anonymous_args = 0
	stmfd	sp!, {r4, r5, r6, r7, r8, r9, sl, fp, lr}
	.save {r4, r5, r6, r7, r8, r9, sl, fp, lr}
	mov	sl, r1
	ldr	r8, .L1603
	.pad #28
	sub	sp, sp, #28
	str	r0, [sp, #8]
	ldr	r6, [r8, #4040]
	ldrb	r1, [r8, #3837]	@ zero_extendqisi2
	cmp	r6, #0
	str	r1, [sp, #16]
	movne	r4, #0
	bne	.L1574
	ldrb	r2, [r8, #928]	@ zero_extendqisi2
	mov	r5, r6
	ldr	fp, .L1603+4
	mov	r4, r6
	mov	r9, #36
	str	r2, [sp, #4]
	str	r0, [fp, #-980]
	b	.L1575
.L1579:
	mul	r3, r9, r6
	ldr	r2, [r8, #4044]
	mov	r1, #0
	str	r1, [r2, r3]
	add	r3, r8, r6
	ldr	r1, [sp, #8]
	ldrb	r0, [r3, #3874]	@ zero_extendqisi2
	bl	V2P_block
	cmp	sl, #0
	mov	r7, r0
	beq	.L1576
	bl	IsBlkInVendorPart
	cmp	r0, #0
	bne	.L1577
.L1576:
	mov	r0, r7
	bl	FtlBbmIsBadBlock
	cmp	r0, #0
	addne	r4, r4, #1
	uxthne	r4, r4
	bne	.L1577
	movw	r1, #3928
	ldr	r3, [r8, #4044]
	ldrh	r2, [r8, r1]
	mov	r7, r7, asl #10
	mla	r3, r9, r5, r3
	mul	r2, r2, r5
	add	r5, r5, #1
	uxth	r5, r5
	add	r1, r2, #3
	cmp	r2, #0
	str	r7, [r3, #4]
	movlt	r2, r1
	ldr	r1, [fp, #-928]
	bic	r2, r2, #3
	str	r0, [r3, #8]
	add	r2, r1, r2
	str	r2, [r3, #12]
.L1577:
	add	r6, r6, #1
	uxth	r6, r6
.L1575:
	movw	r2, #3848
	ldr	r7, .L1603
	ldrh	r3, [r8, r2]
	cmp	r3, r6
	bhi	.L1579
	cmp	r5, #0
	beq	.L1574
	ldr	r3, [sp, #4]
	mov	r8, #0
	ldr	r0, [r7, #4044]
	mov	r2, r5
	adds	r6, r3, #0
	strb	r8, [r7, #3837]
	mov	r9, r8
	movne	r6, #1
	mov	r1, r6
	bl	FlashEraseBlocks
	ldr	r1, [sp, #16]
	strb	r1, [r7, #3837]
.L1582:
	ldr	r3, [r7, #4044]
	add	r2, r3, r8
	ldr	r3, [r3, r8]
	cmn	r3, #1
	bne	.L1581
	ldr	r0, [r2, #4]
	add	r4, r4, #1
	ubfx	r0, r0, #10, #16
	uxth	r4, r4
	bl	FtlBbmMapBadBlock
.L1581:
	add	r9, r9, #1
	add	r8, r8, #36
	uxth	r9, r9
	cmp	r9, r5
	bne	.L1582
	cmp	sl, #0
	beq	.L1596
	ldr	r3, .L1603
	movw	r2, #3918
	ldrh	r2, [r3, r2]
	ldrb	r3, [r3, #928]	@ zero_extendqisi2
	cmp	r3, #0
	str	r2, [sp, #12]
	moveq	r2, r2, lsr #2
	streq	r2, [sp, #4]
	moveq	r6, #1
	beq	.L1583
	b	.L1602
.L1596:
	mov	r3, #6
	mov	r1, #1
	str	r3, [sp, #4]
	str	r1, [sp, #12]
	b	.L1583
.L1602:
	mov	r6, #1
	str	r6, [sp, #4]
.L1583:
	ldr	r7, .L1603
	mov	r8, #0
.L1591:
	mov	r9, #0
	ldr	fp, .L1603+4
	mov	r5, r9
	b	.L1584
.L1587:
	mov	r1, #36
	ldr	r2, [r7, #4044]
	mul	r3, r1, r9
	mov	r1, #0
	str	r1, [r2, r3]
	add	r3, r7, r9
	ldr	r1, [sp, #8]
	ldrb	r0, [r3, #3874]	@ zero_extendqisi2
	bl	V2P_block
	cmp	sl, #0
	str	r0, [sp, #20]
	beq	.L1585
	bl	IsBlkInVendorPart
	cmp	r0, #0
	bne	.L1586
.L1585:
	ldr	r0, [sp, #20]
	bl	FtlBbmIsBadBlock
	cmp	r0, #0
	bne	.L1586
	ldr	r3, [r7, #4044]
	mov	r2, #36
	ldr	r1, [sp, #20]
	mla	r3, r2, r5, r3
	add	r2, r8, r1, asl #10
	movw	r1, #3928
	str	r2, [r3, #4]
	ldr	r2, [fp, #-940]
	str	r2, [r3, #8]
	ldrh	r2, [r7, r1]
	mul	r2, r2, r5
	add	r5, r5, #1
	uxth	r5, r5
	add	r1, r2, #3
	cmp	r2, #0
	movlt	r2, r1
	ldr	r1, [fp, #-936]
	bic	r2, r2, #3
	add	r2, r1, r2
	str	r2, [r3, #12]
.L1586:
	add	r9, r9, #1
	uxth	r9, r9
.L1584:
	movw	r2, #3848
	ldr	ip, .L1603
	ldrh	r3, [r7, r2]
	cmp	r3, r9
	bhi	.L1587
	cmp	r5, #0
	beq	.L1574
	ldr	r0, [ip, #4044]
	mov	r3, #0
	mov	r1, r5
	strb	r3, [ip, #3837]
	mov	r2, r6
	mov	r3, #1
	str	ip, [sp, #0]
	bl	FlashProgPages
	ldr	ip, [sp, #0]
	mov	r9, #0
	ldr	r1, [sp, #16]
	mov	fp, r9
	strb	r1, [ip, #3837]
.L1590:
	ldr	r3, [r7, #4044]
	add	r2, r3, r9
	ldr	r3, [r3, r9]
	cmp	r3, #0
	beq	.L1589
	ldr	r0, [r2, #4]
	add	r4, r4, #1
	ubfx	r0, r0, #10, #16
	uxth	r4, r4
	bl	FtlBbmMapBadBlock
.L1589:
	add	fp, fp, #1
	add	r9, r9, #36
	uxth	fp, fp
	cmp	fp, r5
	bne	.L1590
	ldr	r2, [sp, #4]
	ldr	r3, [sp, #12]
	add	r8, r8, r2
	uxth	r8, r8
	cmp	r8, r3
	bcc	.L1591
	ldr	r9, .L1603
	mov	r7, #0
	mov	r8, r7
.L1593:
	cmp	sl, #0
	beq	.L1592
	ldr	r3, [r9, #4044]
	add	r2, r3, r7
	ldr	r3, [r3, r7]
	cmp	r3, #0
	bne	.L1592
	ldr	r0, [r2, #4]
	mov	r1, #1
	ubfx	r0, r0, #10, #16
	bl	FtlFreeSysBlkQueueIn
.L1592:
	add	r8, r8, #1
	add	r7, r7, #36
	uxth	r8, r8
	cmp	r8, r5
	bne	.L1593
	ldr	r1, [sp, #8]
	cmp	r1, #63
	movhi	r3, #0
	movls	r3, #1
	cmp	sl, #0
	moveq	sl, r3
	orrne	sl, r3, #1
	cmp	sl, #0
	beq	.L1574
	ldr	r3, .L1603
	mov	r1, r6
	mov	r2, r8
	ldr	r0, [r3, #4044]
	bl	FlashEraseBlocks
.L1574:
	mov	r0, r4
	add	sp, sp, #28
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, sl, fp, pc}
.L1604:
	.align	2
.L1603:
	.word	.LANCHOR0
	.word	.LANCHOR2
	.fnend
	.size	FtlLowFormatEraseBlock, .-FtlLowFormatEraseBlock
	.align	2
	.type	FlashTestBlk.part.24, %function
FlashTestBlk.part.24:
	.fnstart
	@ args = 0, pretend = 0, frame = 104
	@ frame_needed = 0, uses_anonymous_args = 0
.L1606:
	stmfd	sp!, {r4, r5, lr}
	.save {r4, r5, lr}
	.pad #108
	sub	sp, sp, #108
	ldr	r4, .L1608
	mov	r5, r0
	mov	r1, #165
	add	r0, sp, #40
	mov	r2, #32
	str	r0, [sp, #16]
	ldr	r3, [r4, #140]
	mov	r5, r5, asl #10
	str	r3, [sp, #12]
	bl	ftl_memset
	ldr	r0, [r4, #140]
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
.L1609:
	.align	2
.L1608:
	.word	.LANCHOR2
	.fnend
	.size	FlashTestBlk.part.24, .-FlashTestBlk.part.24
	.align	2
	.global	FlashTestBlk
	.type	FlashTestBlk, %function
FlashTestBlk:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	@ link register save eliminated.
	ldr	r3, .L1612
	ldr	r3, [r3, #160]
	cmp	r0, r3
	bcc	.L1611
	b	FlashTestBlk.part.24
.L1611:
	mov	r0, #0
	bx	lr
.L1613:
	.align	2
.L1612:
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
	ldr	r3, .L1642
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
	ldr	r4, .L1642+4
	mul	fp, fp, r2
	ldr	r2, [r3, #3624]
	ldr	r0, [r4, #232]
	ldrb	r2, [r2, #24]	@ zero_extendqisi2
	str	r0, [sp, #28]
	ldr	r0, .L1642+8
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
	ldr	r0, [r4, #232]
	mov	r2, #4096
	mov	r1, #0
	bl	ftl_memset
	sub	r3, fp, #1
	mov	r2, fp, lsr #4
	ldr	r4, .L1642
	uxth	r3, r3
	str	r2, [sp, #20]
	str	r3, [sp, #24]
	b	.L1616
.L1638:
	ldr	r3, .L1642+4
	add	r2, r3, r5, asl #1
	ldrh	r6, [r2, #236]
	cmp	r6, #0
	bne	.L1617
	movw	r2, #3068
	ldr	r0, [r3, #152]
	ldrh	r2, [r4, r2]
	mov	r1, r6
	mov	r8, r6
	mov	r7, r6
	add	sl, r4, r5, asl #2
	mov	r2, r2, asl #9
	bl	ftl_memset
	add	r3, r4, r5
	ldrb	r9, [r3, #3764]	@ zero_extendqisi2
	b	.L1618
.L1626:
	mvn	r3, #0
	strb	r3, [sp, #34]
	strb	r3, [sp, #35]
	ldr	r3, [sp, #12]
	tst	r3, #1
	beq	.L1619
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
	bne	.L1619
	ldr	r1, [r4, #856]
	add	r2, sp, #35
	mov	r0, r9
	add	r1, r3, r1
	bl	FlashReadSpare
	ldrb	r3, [sp, #34]	@ zero_extendqisi2
	ldrb	r2, [sp, #35]	@ zero_extendqisi2
	and	r3, r2, r3
	strb	r3, [sp, #34]
.L1619:
	ldr	r0, [sp, #12]
	tst	r0, #2
	beq	.L1620
	ldr	r3, [r4, #3624]
	mov	r0, r9
	add	r2, sp, #35
	ldrh	r1, [r3, #10]
	ldr	r3, [sl, #3588]
	sub	r1, r1, #1
	add	r1, r1, r3
	add	r1, r1, r6
	bl	FlashReadSpare
.L1620:
	ldr	r3, [r4, #3624]
	ldrb	r3, [r3, #7]	@ zero_extendqisi2
	cmp	r3, #1
	cmpne	r3, #8
	ldrb	r3, [sp, #34]	@ zero_extendqisi2
	bne	.L1621
	cmp	r3, #0
	beq	.L1640
	ldrb	r0, [sp, #35]	@ zero_extendqisi2
	rsbs	r0, r0, #1
	movcc	r0, #0
	b	.L1622
.L1621:
	cmp	r3, #255
	bne	.L1640
	ldrb	r0, [sp, #35]	@ zero_extendqisi2
	subs	r0, r0, #255
	movne	r0, #1
	b	.L1622
.L1640:
	mov	r0, #1
.L1622:
	ldr	r2, [sp, #12]
	tst	r2, #4
	beq	.L1623
	ldr	r1, [sl, #3588]
	mov	r0, r9
	add	r1, r6, r1
	bl	SandiskProgTestBadBlock
.L1623:
	cmp	r0, #0
	beq	.L1624
	mov	r1, r5
	mov	r2, r8
	ldr	r0, .L1642+12
	add	r7, r7, #1
	bl	printk
	ldr	r3, [sp, #16]
	and	r0, r8, #31
	mov	ip, #1
	uxth	r7, r7
	mov	r2, r3, lsr #5
	ldr	r3, .L1642+4
	ldr	r3, [r3, #152]
	ldr	r1, [r3, r2, asl #2]
	orr	r1, r1, ip, asl r0
	ldr	r0, [sp, #20]
	str	r1, [r3, r2, asl #2]
	ldrb	r3, [r4, #3762]	@ zero_extendqisi2
	mul	r3, r3, r0
	cmp	r7, r3
	bgt	.L1625
.L1624:
	ldr	r2, [sp, #8]
	add	r8, r8, #1
	add	r6, r6, r2
.L1618:
	uxth	r3, r8
	str	r3, [sp, #16]
	cmp	r3, fp
	bcc	.L1626
.L1625:
	ldr	r0, .L1642+16
	mov	r1, r5
	mov	r2, r7
	bl	printk
	ldrb	r3, [r4, #3762]	@ zero_extendqisi2
	ldr	r0, [sp, #20]
	mul	r3, r3, r0
	cmp	r7, r3
	blt	.L1627
	movw	r3, #3068
	mov	r1, #0
	ldrh	r2, [r4, r3]
	ldr	r3, .L1642+4
	mov	r2, r2, asl #9
	ldr	r0, [r3, #152]
	bl	ftl_memset
.L1627:
	cmp	r5, #0
	bne	.L1628
	ldr	sl, .L1642+4
	mov	r8, r5
	mov	r7, r5
	ldrh	r9, [sl, #160]
	b	.L1629
.L1631:
	mov	r0, r6
	bl	FlashTestBlk
	cmp	r0, #0
	beq	.L1630
	mov	r1, r6
	ldr	r0, .L1642+20
	bl	printk
	ldr	r3, [sl, #152]
	mov	r2, r6, lsr #5
	mov	r0, #1
	add	r7, r7, r0
	and	r6, r6, #31
	ldr	r1, [r3, r2, asl #2]
	uxth	r7, r7
	orr	r6, r1, r0, asl r6
	str	r6, [r3, r2, asl #2]
.L1630:
	add	r8, r8, #1
	uxth	r8, r8
.L1629:
	add	r6, r8, r9
	ldrb	r3, [r4, #853]	@ zero_extendqisi2
	uxth	r6, r6
	cmp	r3, r6
	bhi	.L1631
	ldr	r6, [sp, #24]
	sub	r9, fp, #50
	ldr	sl, .L1642+4
	mov	r8, #1
	b	.L1632
.L1634:
	mov	r0, r6
	bl	FlashTestBlk
	cmp	r0, #0
	beq	.L1633
	mov	r1, r6
	ldr	r0, .L1642+20
	bl	printk
	ldr	r3, [sl, #152]
	mov	r2, r6, lsr #5
	and	r0, r6, #31
	ldr	r1, [r3, r2, asl #2]
	orr	r1, r1, r8, asl r0
	str	r1, [r3, r2, asl #2]
.L1633:
	sub	r6, r6, #1
	uxth	r6, r6
.L1632:
	cmp	r6, r9
	bgt	.L1634
	ldr	r3, .L1642+4
	ldrb	r1, [r4, #853]	@ zero_extendqisi2
	ldr	r2, [r3, #160]
	rsb	r2, r2, r1
	cmp	r7, r2
	bcc	.L1628
	movw	r2, #3068
	ldr	r0, [r3, #152]
	ldrh	r2, [r4, r2]
	mov	r1, #0
	mov	r2, r2, asl #9
	bl	ftl_memset
.L1628:
	mul	r8, fp, r5
	ldr	r7, .L1642+4
	ldr	r6, [sp, #24]
	ldr	r9, [sp, #28]
	add	sl, r7, r5, asl #1
.L1635:
	ldr	r0, .L1642+24
	mov	r1, r5
	mov	r2, r6
	bl	printk
	ldr	r3, [r7, #152]
	b	.L1636
.L1637:
	sub	r6, r6, #1
	uxth	r6, r6
.L1636:
	mov	r1, r6, lsr #5
	and	r2, r6, #31
	ldr	r1, [r3, r1, asl #2]
	mov	r2, r1, lsr r2
	ands	r2, r2, #1
	bne	.L1637
	ldr	r3, .L1642+28
	mov	r1, #1
	strh	r6, [sl, #236]	@ movhi
	add	r0, sp, #36
	strh	r6, [r9, #2]	@ movhi
	strh	r3, [r9, #0]	@ movhi
	ldr	r3, [r7, #152]
	strh	r2, [r9, #8]	@ movhi
	mov	r2, r1
	str	r3, [sp, #44]
	ldr	r3, [r7, #232]
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
	bne	.L1635
.L1617:
	add	r5, r5, #1
	uxtb	r5, r5
.L1616:
	ldrb	r3, [r4, #3762]	@ zero_extendqisi2
	cmp	r3, r5
	bhi	.L1638
	add	sp, sp, #76
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, sl, fp, pc}
.L1643:
	.align	2
.L1642:
	.word	.LANCHOR0
	.word	.LANCHOR2
	.word	.LC118
	.word	.LC119
	.word	.LC120
	.word	.LC121
	.word	.LC122
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
	ldr	sl, [r0, #24]
	bl	ftl_free_no_use_map_blk
	ldrh	r3, [r4, #10]
	ldrh	r2, [r4, #8]
	sub	r3, r3, #4
	cmp	r2, r3
	blt	.L1645
	uxth	r0, r0
	mov	r0, r0, asl #1
	ldrh	r8, [r5, r0]
	cmp	r8, #0
	beq	.L1645
	ldr	r3, [r4, #32]
	cmp	r3, #0
	bne	.L1645
	mov	r2, #1
	ldr	r1, .L1653
	str	r2, [r4, #32]
	strh	r3, [r5, r0]	@ movhi
	ldrh	r3, [r4, #8]
	ldrh	r2, [r4, #2]
	sub	r3, r3, #1
	strh	r3, [r4, #8]	@ movhi
	movw	r3, #3918
	ldrh	r3, [r1, r3]
	cmp	r2, r3
	bcc	.L1646
	mov	r0, r4
	bl	ftl_map_blk_alloc_new_blk
.L1646:
	mov	r5, #0
	ldr	r6, .L1653+4
	b	.L1647
.L1652:
	ldr	r3, [sl, r5, asl #2]
	add	r9, sl, r5, asl #2
	cmp	r8, r3, lsr #10
	bne	.L1648
	ldr	r3, [r6, #-952]
	mov	r1, #1
	ldr	r7, [r6, #-932]
	mov	r2, r1
	ldr	r0, .L1653+8
	str	r3, [r6, #192]
	str	r7, [r6, #196]
	ldr	r3, [sl, r5, asl #2]
	str	r3, [r6, #188]
	bl	FlashReadPages
	ldr	r3, [r6, #184]
	cmn	r3, #1
	beq	.L1649
	ldrh	r3, [r7, #8]
	cmp	r3, r5
	bne	.L1649
	ldrh	r2, [r7, #0]
	ldrh	r3, [r4, #4]
	cmp	r2, r3
	beq	.L1650
.L1649:
	mov	r3, #0
	str	r3, [r9, #0]
	ldr	r3, .L1653+4
	ldrh	r2, [r7, #8]
	ldr	r0, .L1653+12
	ldr	r1, [r3, #188]
	bl	printk
	ldr	r3, .L1653
	mov	r2, #1
	str	r2, [r3, #4040]
	b	.L1651
.L1650:
	mov	r0, r4
	mov	r1, r5
	ldr	r2, [r6, #192]
	bl	FtlMapWritePage
.L1648:
	add	r5, r5, #1
	uxth	r5, r5
.L1647:
	ldrh	r3, [r4, #6]
	cmp	r3, r5
	bhi	.L1652
	mov	r0, r8
	mov	r1, #1
	bl	FtlFreeSysBlkQueueIn
	mov	r3, #0
	str	r3, [r4, #32]
.L1645:
	ldr	r1, .L1653
	movw	r3, #3918
	ldrh	r2, [r4, #2]
	ldrh	r3, [r1, r3]
	cmp	r2, r3
	bcc	.L1651
	mov	r0, r4
	bl	ftl_map_blk_alloc_new_blk
.L1651:
	mov	r0, #0
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, sl, pc}
.L1654:
	.align	2
.L1653:
	.word	.LANCHOR0
	.word	.LANCHOR2
	.word	.LANCHOR2+184
	.word	.LC123
	.fnend
	.size	ftl_map_blk_gc, .-ftl_map_blk_gc
	.align	2
	.global	Ftl_write_map_blk_to_last_page
	.type	Ftl_write_map_blk_to_last_page, %function
Ftl_write_map_blk_to_last_page:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	ldr	r3, .L1661
	stmfd	sp!, {r4, r5, r6, r7, r8, lr}
	.save {r4, r5, r6, r7, r8, lr}
	mov	r4, r0
	ldr	r6, [r3, #4040]
	ldr	r5, [r0, #12]
	cmp	r6, #0
	ldr	r8, [r0, #24]
	bne	.L1656
	ldrh	r2, [r0, #0]
	movw	r1, #65535
	cmp	r2, r1
	bne	.L1657
	ldrh	r3, [r0, #8]
	add	r3, r3, #1
	strh	r3, [r0, #8]	@ movhi
	bl	FtlFreeSysBlkQueueOut
	strh	r0, [r5, #0]	@ movhi
	ldr	r3, [r4, #28]
	strh	r6, [r4, #2]	@ movhi
	add	r3, r3, #1
	strh	r6, [r4, #0]	@ movhi
	str	r3, [r4, #28]
	b	.L1656
.L1657:
	mov	r2, r2, asl #1
	ldr	r1, [r0, #28]
	ldrh	r7, [r5, r2]
	ldr	r5, .L1661+4
	ldrh	r2, [r0, #2]
	orr	r2, r2, r7, asl #10
	str	r2, [r5, #188]
	ldr	r2, [r5, #-956]
	str	r2, [r5, #192]
	ldr	r2, [r5, #-932]
	str	r2, [r5, #196]
	str	r1, [r2, #4]
	ldr	r1, .L1661+8
	strh	r1, [r2, #8]	@ movhi
	ldrh	r1, [r0, #4]
	strh	r7, [r2, #2]	@ movhi
	strh	r1, [r2, #0]	@ movhi
	movw	r2, #3918
	ldrh	r2, [r3, r2]
	mov	r1, #255
	ldr	r0, [r5, #-956]
	mov	r2, r2, asl #3
	bl	ftl_memset
	mov	r3, r6
	b	.L1658
.L1660:
	ldr	r2, [r8, r3, asl #2]
	cmp	r7, r2, lsr #10
	bne	.L1659
	add	r6, r6, #1
	ldr	r2, [r5, #-956]
	uxth	r6, r6
	str	r3, [r2, r6, asl #3]
	ldr	r2, [r5, #-956]
	ldr	r1, [r8, r3, asl #2]
	add	r2, r2, r6, asl #3
	str	r1, [r2, #4]
.L1659:
	add	r3, r3, #1
	uxth	r3, r3
.L1658:
	ldrh	r2, [r4, #6]
	cmp	r2, r3
	bhi	.L1660
	mov	r1, #1
	mov	r3, #0
	ldr	r0, .L1661+12
	mov	r2, r1
	bl	FlashProgPages
	ldrh	r3, [r4, #2]
	mov	r0, r4
	add	r3, r3, #1
	strh	r3, [r4, #2]	@ movhi
	bl	ftl_map_blk_gc
.L1656:
	mov	r0, #0
	ldmfd	sp!, {r4, r5, r6, r7, r8, pc}
.L1662:
	.align	2
.L1661:
	.word	.LANCHOR0
	.word	.LANCHOR2
	.word	-1291
	.word	.LANCHOR2+184
	.fnend
	.size	Ftl_write_map_blk_to_last_page, .-Ftl_write_map_blk_to_last_page
	.align	2
	.global	FtlMapWritePage
	.type	FtlMapWritePage, %function
FtlMapWritePage:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	stmfd	sp!, {r3, r4, r5, r6, r7, r8, r9, sl, fp, lr}
	.save {r3, r4, r5, r6, r7, r8, r9, sl, fp, lr}
	mov	r4, r0
	ldr	r8, .L1677
	mov	sl, r1
	ldr	r5, .L1677+4
	mov	r9, r2
	mov	r6, #0
	mov	fp, r8
.L1676:
	ldr	r3, [r5, #-1820]
	add	r3, r3, #1
	str	r3, [r5, #-1820]
	movw	r3, #3918
	ldrh	r2, [r4, #2]
	ldrh	r3, [r8, r3]
	sub	r3, r3, #1
	cmp	r2, r3
	bge	.L1665
	ldrh	r2, [r4, #0]
	movw	r3, #65535
	cmp	r2, r3
	bne	.L1666
.L1665:
	mov	r0, r4
	bl	Ftl_write_map_blk_to_last_page
.L1666:
	ldr	r1, [fp, #4040]
	cmp	r1, #0
	bne	.L1667
	ldrh	r3, [r4, #0]
	ldr	r2, [r4, #12]
	ldr	r0, [r5, #-932]
	mov	r3, r3, asl #1
	ldrh	r7, [r2, r3]
	mov	r2, #16
	ldrh	r3, [r4, #2]
	str	r9, [r5, #192]
	orr	r3, r3, r7, asl #10
	str	r0, [r5, #196]
	str	r3, [r5, #188]
	bl	ftl_memset
	ldr	r3, [r5, #196]
	mov	r1, #1
	ldr	r2, [r4, #28]
	ldr	r0, .L1677+8
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
	ldr	r3, [r5, #184]
	cmn	r3, #1
	bne	.L1668
	ldr	r0, .L1677+12
	add	r6, r6, #1
	ldr	r1, [r5, #188]
	bl	printk
	ldrh	r3, [r4, #2]
	uxth	r6, r6
	cmp	r3, #2
	movwls	r3, #3918
	ldrlsh	r3, [r8, r3]
	subls	r3, r3, #1
	strlsh	r3, [r4, #2]	@ movhi
	cmp	r6, #3
	bls	.L1676
	ldr	r3, .L1677+4
	mov	r2, r6
	ldr	r0, .L1677+16
	ldr	r1, [r3, #188]
	bl	printk
	ldr	r3, .L1677
	mov	r2, #1
	str	r2, [r3, #4040]
	b	.L1667
.L1668:
	cmp	r3, #0
	strneh	r7, [r4, #40]	@ movhi
	cmp	r2, #1
	beq	.L1672
	cmp	r3, #256
	beq	.L1672
	ldr	r3, [r4, #36]
	cmp	r3, #0
	beq	.L1673
.L1672:
	mov	r3, #0
	str	r3, [r4, #36]
	b	.L1676
.L1673:
	ldr	r3, .L1677+4
	ldr	r2, [r3, #188]
	ldr	r3, [r4, #24]
	str	r2, [r3, sl, asl #2]
.L1667:
	mov	r0, #0
	ldmfd	sp!, {r3, r4, r5, r6, r7, r8, r9, sl, fp, pc}
.L1678:
	.align	2
.L1677:
	.word	.LANCHOR0
	.word	.LANCHOR2
	.word	.LANCHOR2+184
	.word	.LC124
	.word	.LC125
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
	ldr	r5, .L1680
	mul	r4, r4, r0
	ldr	r0, .L1680+4
	ldr	r2, [r5, #-1852]
	add	r3, r2, r4
	ldrh	r1, [r2, r4]
	ldr	r2, [r3, #8]
	bl	FtlMapWritePage
	ldr	r3, [r5, #-1852]
	mov	r0, #0
	add	r4, r3, r4
	ldr	r3, [r4, #4]
	bic	r3, r3, #-2147483648
	str	r3, [r4, #4]
	ldmfd	sp!, {r3, r4, r5, pc}
.L1681:
	.align	2
.L1680:
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
	stmfd	sp!, {r4, r5, r6, lr}
	.save {r4, r5, r6, lr}
	mov	r4, r0
	ldr	r3, [r0, #36]
	ldrh	r6, [r0, #6]
	cmp	r3, #0
	ldr	r2, [r0, #24]
	ldmeqfd	sp!, {r4, r5, r6, pc}
	mov	r3, #0
	str	r3, [r0, #36]
	ldr	r3, .L1686
	ldr	r1, [r3, #4040]
	cmp	r1, #0
	ldmnefd	sp!, {r4, r5, r6, pc}
	ldr	r5, .L1686+4
	sub	r6, r6, #1
	uxth	r6, r6
	ldr	r0, [r5, #-952]
	ldr	r1, [r5, #-932]
	str	r0, [r5, #192]
	str	r1, [r5, #196]
	ldr	r2, [r2, r6, asl #2]
	cmp	r2, #0
	str	r2, [r5, #188]
	beq	.L1684
	mov	r1, #1
	add	r0, r5, #184
	mov	r2, r1
	bl	FlashReadPages
	b	.L1685
.L1684:
	movw	r2, #3926
	mov	r1, #255
	ldrh	r2, [r3, r2]
	bl	ftl_memset
.L1685:
	mov	r0, r4
	mov	r1, r6
	ldr	r2, [r5, #192]
	ldmfd	sp!, {r4, r5, r6, lr}
	b	FtlMapWritePage
.L1687:
	.align	2
.L1686:
	.word	.LANCHOR0
	.word	.LANCHOR2
	.fnend
	.size	FtlMapBlkWriteDumpData, .-FtlMapBlkWriteDumpData
	.align	2
	.global	FtlVendorPartRead
	.type	FtlVendorPartRead, %function
FtlVendorPartRead:
	.fnstart
	@ args = 0, pretend = 0, frame = 48
	@ frame_needed = 0, uses_anonymous_args = 0
	ldr	r3, .L1699
	stmfd	sp!, {r4, r5, r6, r7, r8, r9, sl, fp, lr}
	.save {r4, r5, r6, r7, r8, r9, sl, fp, lr}
	mov	r9, r2
	movw	r2, #3924
	mov	r5, r1
	ldrh	r7, [r3, r2]
	movw	r2, #3910
	ldrh	r2, [r3, r2]
	add	r1, r1, r0
	.pad #52
	sub	sp, sp, #52
	mov	r8, r0
	cmp	r1, r2
	mvnhi	fp, #0
	bhi	.L1689
	mov	r7, r0, lsr r7
	mov	fp, #0
	ldr	r6, .L1699+4
	mov	r2, r7, asl #2
	str	r2, [sp, #0]
	b	.L1690
.L1696:
	ldr	r3, [r6, #-900]
	mov	r0, r8
	ldr	r2, [sp, #0]
	ldr	sl, [r3, r2]
	movw	r3, #3922
	ldr	r2, .L1699
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
	beq	.L1692
	ldr	r2, .L1699+4
	mov	r1, #1
	add	r0, sp, #12
	str	sl, [sp, #16]
	ldr	r3, [r2, #-948]
	mov	r2, r1
	str	r3, [sp, #20]
	mov	r3, #0
	str	r3, [sp, #24]
	bl	FlashReadPages
	ldr	r3, [sp, #12]
	cmn	r3, #1
	moveq	fp, r3
	ldr	r3, [r6, #184]
	cmp	r3, #256
	bne	.L1694
	mov	r1, r7
	mov	r2, sl
	ldr	r0, .L1699+8
	bl	printk
	ldr	r0, .L1699+12
	mov	r1, r7
	ldr	r2, [r6, #-948]
	bl	FtlMapWritePage
.L1694:
	ldr	r3, [sp, #4]
	mov	r0, r9
	ldr	r1, [r6, #-948]
	mov	r2, r4, asl #9
	add	r1, r1, r3, asl #9
	bl	memcpy
	b	.L1695
.L1692:
	mov	r0, r9
	mov	r1, sl
	mov	r2, r4, asl #9
	bl	ftl_memset
.L1695:
	ldr	r3, [sp, #0]
	add	r7, r7, #1
	rsb	r5, r4, r5
	add	r8, r8, r4
	add	r9, r9, r4, asl #9
	add	r3, r3, #4
	str	r3, [sp, #0]
.L1690:
	cmp	r5, #0
	bne	.L1696
.L1689:
	mov	r0, fp
	add	sp, sp, #52
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, sl, fp, pc}
.L1700:
	.align	2
.L1699:
	.word	.LANCHOR0
	.word	.LANCHOR2
	.word	.LC126
	.word	.LANCHOR2+252
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
	ldr	r5, .L1705
	mov	r1, #1
	ldr	r2, .L1705+4
	bl	FtlVendorPartRead
	ldr	r4, .L1705+8
	ldr	r3, [r5, #-1708]
	cmp	r3, r4
	beq	.L1702
	ldr	r0, .L1705+4
	mov	r1, #0
	mov	r2, #512
	bl	ftl_memset
	str	r4, [r5, #-1708]
.L1702:
	ldr	r2, [r5, #-1708]
	ldr	r3, .L1705
	cmp	r2, r4
	bne	.L1703
	ldr	r2, [r3, #-1620]
	ldr	r1, [r3, #-1676]
	str	r2, [r3, #-1812]
	ldr	r2, [r3, #-1616]
	str	r2, [r3, #-1808]
	ldr	r2, [r3, #-1700]
	str	r2, [r3, #-1816]
	ldr	r2, [r3, #-1696]
	str	r2, [r3, #-1828]
	ldr	r2, [r3, #-1692]
	str	r2, [r3, #-1836]
	ldr	r2, [r3, #-1688]
	str	r2, [r3, #-1820]
	ldr	r2, [r3, #-1680]
	str	r2, [r3, #-1792]
	ldr	r2, .L1705+12
	str	r1, [r2, #4052]
	ldr	r2, [r3, #-1672]
	str	r2, [r3, #-1832]
	ldr	r2, [r3, #-1668]
	str	r2, [r3, #-1824]
	ldr	r2, [r3, #-1664]
	str	r2, [r3, #-1784]
	ldr	r2, [r3, #-1660]
	str	r2, [r3, #-1780]
	ldr	r2, [r3, #-1648]
	str	r2, [r3, #-1712]
.L1703:
	ldr	r4, .L1705
	mov	r3, #0
	ldr	r5, .L1705+12
	str	r3, [r4, #-996]
	ldr	r2, [r4, #-1640]
	ldr	r3, .L1705+16
	cmp	r2, r3
	bne	.L1704
	mov	r3, #1
	ldr	r0, .L1705+20
	str	r3, [r5, #3840]
	ldr	r1, .L1705+24
	bl	printk
.L1704:
	movw	r3, #3906
	ldr	r0, [r4, #-1796]
	ldrh	r2, [r5, r3]
	ldr	r3, [r4, #-1792]
	mla	r0, r0, r2, r3
	mov	r3, #3856
	ldrh	r1, [r5, r3]
	bl	__aeabi_uidiv
	str	r0, [r4, #-1788]
	ldmfd	sp!, {r3, r4, r5, pc}
.L1706:
	.align	2
.L1705:
	.word	.LANCHOR2
	.word	.LANCHOR2-1708
	.word	1179929683
	.word	.LANCHOR0
	.word	305432421
	.word	.LC77
	.word	.LC127
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
	ldr	r4, .L1709
	ldr	r5, .L1709+4
	ldr	r2, [r4, #-916]
	ldrh	r1, [r4, r5]
	bl	FtlVendorPartRead
	ldr	r3, [r4, #-916]
	ldr	r2, [r3, #0]
	ldr	r3, .L1709+8
	cmp	r2, r3
	beq	.L1708
	ldr	r1, .L1709+12
	ldr	r0, .L1709+16
	bl	printk
	ldrh	r2, [r4, r5]
	ldr	r0, [r4, #-916]
	mov	r1, #0
	mov	r2, r2, asl #9
	bl	ftl_memset
.L1708:
	mov	r0, #0
	ldmfd	sp!, {r3, r4, r5, pc}
.L1710:
	.align	2
.L1709:
	.word	.LANCHOR2
	.word	-920
	.word	1112818501
	.word	.LC128
	.word	.LC77
	.fnend
	.size	FtlLoadEctTbl, .-FtlLoadEctTbl
	.align	2
	.global	FtlVendorPartWrite
	.type	FtlVendorPartWrite, %function
FtlVendorPartWrite:
	.fnstart
	@ args = 0, pretend = 0, frame = 56
	@ frame_needed = 0, uses_anonymous_args = 0
	ldr	r3, .L1721
	stmfd	sp!, {r4, r5, r6, r7, r8, r9, sl, fp, lr}
	.save {r4, r5, r6, r7, r8, r9, sl, fp, lr}
	mov	r9, r2
	movw	r2, #3924
	mov	r4, r1
	ldrh	r8, [r3, r2]
	movw	r2, #3910
	ldrh	r2, [r3, r2]
	add	r1, r1, r0
	.pad #60
	sub	sp, sp, #60
	mov	r6, r0
	cmp	r1, r2
	mvnhi	r7, #0
	bhi	.L1712
	mov	r8, r0, lsr r8
	mov	r7, #0
	ldr	sl, .L1721+4
	mov	ip, r8, asl #2
	str	ip, [sp, #8]
	b	.L1713
.L1718:
	ldr	r1, .L1721
	movw	r2, #3922
	ldr	ip, [sp, #8]
	mov	r0, r6
	ldr	r3, [sl, #-900]
	ldrh	fp, [r1, r2]
	ldr	r3, [r3, ip]
	mov	r1, fp
	str	r3, [sp, #4]
	bl	__aeabi_uidivmod
	ldr	r3, [sp, #4]
	ldr	r2, .L1721+4
	uxth	r1, r1
	str	r1, [sp, #12]
	rsb	r5, r1, fp
	uxth	r5, r5
	cmp	r5, r4
	uxthhi	r5, r4
	cmp	r3, #0
	beq	.L1715
	cmp	r5, fp
	beq	.L1715
	str	r3, [sp, #24]
	mov	r1, #1
	ldr	r3, [r2, #-948]
	add	r0, sp, #20
	mov	r2, r1
	str	r3, [sp, #28]
	mov	r3, #0
	str	r3, [sp, #32]
	bl	FlashReadPages
	b	.L1716
.L1715:
	ldr	ip, .L1721
	movw	r3, #3926
	ldr	r0, [r2, #-948]
	mov	r1, #0
	ldrh	r2, [ip, r3]
	bl	ftl_memset
.L1716:
	ldr	r3, [sp, #12]
	mov	fp, r5, asl #9
	ldr	r0, [sl, #-948]
	mov	r1, r9
	mov	r2, fp
	rsb	r4, r5, r4
	add	r0, r0, r3, asl #9
	add	r6, r6, r5
	bl	memcpy
	mov	r1, r8
	ldr	r0, .L1721+8
	add	r8, r8, #1
	ldr	r2, [sl, #-948]
	add	r9, r9, fp
	bl	FtlMapWritePage
	ldr	ip, [sp, #8]
	add	ip, ip, #4
	str	ip, [sp, #8]
	cmn	r0, #1
	moveq	r7, r0
.L1713:
	cmp	r4, #0
	bne	.L1718
.L1712:
	mov	r0, r7
	add	sp, sp, #60
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, sl, fp, pc}
.L1722:
	.align	2
.L1721:
	.word	.LANCHOR0
	.word	.LANCHOR2
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
	ldr	r3, .L1725
	ldr	r2, .L1725+4
	ldr	r1, [r3, #-1708]
	cmp	r1, r2
	bxne	lr
	ldr	r2, .L1725+8
	mov	r0, #0
	mov	r1, #1
	str	r2, [r3, #-1704]
	ldr	r2, [r3, #-1812]
	str	r2, [r3, #-1620]
	ldr	r2, [r3, #-1808]
	str	r2, [r3, #-1616]
	ldr	r2, [r3, #-1816]
	str	r2, [r3, #-1700]
	ldr	r2, [r3, #-1828]
	str	r2, [r3, #-1696]
	ldr	r2, [r3, #-1836]
	str	r2, [r3, #-1692]
	ldr	r2, [r3, #-1820]
	str	r2, [r3, #-1688]
	ldr	r2, [r3, #-1792]
	str	r2, [r3, #-1680]
	ldr	r2, .L1725+12
	ldr	r2, [r2, #4052]
	str	r2, [r3, #-1676]
	ldr	r2, [r3, #-1832]
	str	r2, [r3, #-1672]
	ldr	r2, [r3, #-1824]
	str	r2, [r3, #-1668]
	ldr	r2, [r3, #-1784]
	str	r2, [r3, #-1664]
	ldr	r2, [r3, #-1780]
	str	r2, [r3, #-1660]
	ldr	r2, [r3, #-1712]
	str	r2, [r3, #-1648]
	ldr	r2, [r3, #-996]
	str	r2, [r3, #-1644]
	ldr	r2, .L1725+16
	b	FtlVendorPartWrite
.L1726:
	.align	2
.L1725:
	.word	.LANCHOR2
	.word	1179929683
	.word	1342177360
	.word	.LANCHOR0
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
	stmfd	sp!, {r3, lr}
	.save {r3, lr}
	ldr	r3, .L1734
	ldr	r1, .L1734+4
	ldr	r3, [r3, #3840]
	cmp	r3, #0
	moveq	r2, #32
	beq	.L1728
	ldr	r2, [r1, #-1780]
	cmp	r2, #39
	movls	r2, #4
	movhi	r2, #32
.L1728:
	mov	r3, #296
	ldrh	r1, [r1, r3]
	cmp	r1, #31
	addls	r1, r1, #1
	ldrls	r2, .L1734+4
	strlsh	r1, [r2, r3]	@ movhi
	movls	r2, #1
	cmp	r0, #0
	ldr	r3, .L1734+4
	bne	.L1730
	ldr	r1, [r3, #-916]
	ldr	r0, [r1, #20]
	ldr	r1, [r1, #16]
	add	r2, r2, r0
	cmp	r1, r2
	bcc	.L1731
.L1730:
	ldr	r2, [r3, #-916]
	mov	r0, #64
	ldr	r1, [r2, #16]
	str	r1, [r2, #20]
	ldr	r1, .L1734+8
	str	r1, [r2, #0]
	ldr	r1, .L1734+12
	ldr	r2, [r3, #-916]
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
.L1731:
	mov	r0, #0
	ldmfd	sp!, {r3, pc}
.L1735:
	.align	2
.L1734:
	.word	.LANCHOR0
	.word	.LANCHOR2
	.word	1112818501
	.word	-920
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
	ldr	r3, .L1756
	str	fp, [r4, #32]
	str	fp, [r4, #28]
	ldr	r2, [r3, #-956]
	ldr	r5, [r3, #-932]
	str	r2, [r3, #192]
	str	r5, [r3, #196]
	mvn	r3, #0
	strh	r3, [r4, #0]	@ movhi
	strh	r3, [r4, #2]	@ movhi
	mov	r3, #1
	str	r3, [r4, #36]
	ldr	r3, [sp, #8]
	sub	r3, r3, #1
	str	r3, [sp, #12]
	ldr	r3, .L1756+4
	b	.L1737
.L1750:
	ldr	r0, [sp, #12]
	cmp	r2, r0
	mov	r2, r2, asl #1
	bne	.L1738
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
	ldr	r6, .L1756
	uxth	r0, r0
	add	r3, r0, #1
	strh	r3, [r4, #2]	@ movhi
	ldr	r3, [r2, r1, asl #2]
	sxth	r9, r0
	add	r9, r9, #1
	str	r3, [r4, #28]
	b	.L1739
.L1741:
	ldr	r0, [sp, #8]
	mov	r1, #1
	ldrh	r2, [r0, #0]
	ldr	r0, .L1756+8
	orr	r3, r3, r2, asl #10
	mov	r2, r1
	str	r3, [r6, #188]
	bl	FlashReadPages
	ldr	r3, [r6, #184]
	cmn	r3, #1
	beq	.L1740
	ldrh	r3, [r5, #8]
	cmp	r3, r8
	bcs	.L1740
	ldrh	r2, [r4, #4]
	ldrh	r1, [r5, #0]
	cmp	r1, r2
	ldreq	r2, [r6, #188]
	streq	r2, [r7, r3, asl #2]
.L1740:
	add	sl, sl, #1
	uxth	sl, sl
.L1739:
	sxth	r3, sl
	cmp	r3, r9
	blt	.L1741
	b	.L1742
.L1738:
	ldr	r6, .L1756
	movw	r7, #3918
	add	r0, r6, #184
	ldr	r1, [r6, #-956]
	str	r1, [r6, #192]
	add	r1, r9, r2
	str	r1, [sp, #20]
	ldrh	r1, [r9, r2]
	ldrh	r2, [r3, r7]
	sub	r2, r2, #1
	orr	r2, r2, r1, asl #10
	mov	r1, #1
	str	r2, [r6, #188]
	mov	r2, r1
	str	r3, [sp, #4]
	bl	FlashReadPages
	ldr	r2, [r6, #184]
	ldr	r3, [sp, #4]
	cmn	r2, #1
	beq	.L1753
	ldrh	r1, [r5, #0]
	ldrh	r2, [r4, #4]
	cmp	r1, r2
	bne	.L1753
	ldrh	r1, [r5, #8]
	movw	r2, #64245
	cmp	r1, r2
	bne	.L1753
	b	.L1754
.L1746:
	ldr	r0, [r6, #-956]
	mov	ip, r1, asl #3
	add	r2, r2, #1
	ldr	r1, [r0, r1, asl #3]
	uxth	r2, r2
	uxth	r1, r1
	cmp	r1, r8
	addcc	r0, r0, ip
	ldrcc	r0, [r0, #4]
	strcc	r0, [sl, r1, asl #2]
	b	.L1744
.L1754:
	mov	r2, #0
.L1744:
	ldrh	r0, [r3, r7]
	sxth	r1, r2
	sub	r0, r0, #1
	cmp	r1, r0
	blt	.L1746
	b	.L1747
.L1749:
	ldr	r0, [sp, #20]
	ldrh	r1, [r0, #0]
	ldr	r0, .L1756+8
	orr	r2, r2, r1, asl #10
	mov	r1, #1
	str	r2, [r7, #188]
	mov	r2, r1
	str	r3, [sp, #4]
	bl	FlashReadPages
	ldr	r2, [r7, #184]
	ldr	r3, [sp, #4]
	cmn	r2, #1
	beq	.L1748
	ldrh	r2, [r5, #8]
	cmp	r2, r8
	bcs	.L1748
	ldrh	r1, [r4, #4]
	ldrh	r0, [r5, #0]
	cmp	r0, r1
	ldreq	r1, [r7, #188]
	streq	r1, [sl, r2, asl #2]
.L1748:
	add	r6, r6, #1
	uxth	r6, r6
	b	.L1755
.L1753:
	ldr	r7, .L1756
	mov	r6, #0
.L1755:
	movw	r1, #3918
	sxth	r2, r6
	ldrh	r1, [r3, r1]
	cmp	r2, r1
	blt	.L1749
.L1747:
	add	fp, fp, #1
	uxth	fp, fp
.L1737:
	ldr	r1, [sp, #8]
	sxth	r2, fp
	cmp	r2, r1
	blt	.L1750
.L1742:
	mov	r0, r4
	bl	ftl_free_no_use_map_blk
	ldr	r1, .L1756+4
	movw	r3, #3918
	ldrh	r2, [r4, #2]
	ldrh	r3, [r1, r3]
	cmp	r2, r3
	bne	.L1751
	mov	r0, r4
	bl	ftl_map_blk_alloc_new_blk
.L1751:
	mov	r0, r4
	bl	ftl_map_blk_gc
	mov	r0, r4
	bl	ftl_map_blk_gc
	mov	r0, #0
	add	sp, sp, #28
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, sl, fp, pc}
.L1757:
	.align	2
.L1756:
	.word	.LANCHOR2
	.word	.LANCHOR0
	.word	.LANCHOR2+184
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
	ldr	r0, .L1759
	bl	FtlMapTblRecovery
	mov	r0, #0
	ldmfd	sp!, {r3, pc}
.L1760:
	.align	2
.L1759:
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
	mov	r2, #3936
	ldr	r3, .L1762
	ldr	r0, .L1762+4
	ldrh	r1, [r3, r2]
	movw	r2, #262
	strh	r1, [r0, r2]	@ movhi
	mov	r2, #256
	ldr	r1, .L1762+8
	strh	r1, [r0, r2]	@ movhi
	movw	r2, #3962
	ldrh	r1, [r3, r2]
	mov	r2, #260
	strh	r1, [r0, r2]	@ movhi
	movw	r2, #3938
	ldrh	r1, [r3, r2]
	sub	r2, r2, #3680
	ldr	r3, [r3, #3964]
	strh	r1, [r0, r2]	@ movhi
	str	r3, [r0, #264]
	ldr	r3, [r0, #-904]
	str	r3, [r0, #268]
	ldr	r3, [r0, #-908]
	str	r3, [r0, #272]
	ldr	r3, [r0, #-900]
	str	r3, [r0, #276]
	add	r0, r0, #252
	bl	FtlMapTblRecovery
	mov	r0, #0
	ldmfd	sp!, {r3, pc}
.L1763:
	.align	2
.L1762:
	.word	.LANCHOR0
	.word	.LANCHOR2
	.word	-3962
	.fnend
	.size	FtlLoadVonderInfo, .-FtlLoadVonderInfo
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
	ldr	r9, .L1773
	movw	r3, #3060
	ldr	r4, .L1773+4
	.pad #52
	sub	sp, sp, #52
	mov	r1, #0
	mvn	r7, #0
	ldrh	r3, [r9, r3]
	add	r0, r4, #236
	ldrh	r8, [r9, r2]
	mov	r2, #16
	mov	r6, #0
	mul	r8, r8, r3
	bl	ftl_memset
	ldr	r3, [r4, #232]
	str	r6, [sp, #20]
	mov	r4, r6
	str	r3, [sp, #24]
	uxth	r8, r8
	add	r3, r8, r7
	uxth	r3, r3
	str	r3, [sp, #4]
	b	.L1765
.L1771:
	ldr	r5, [sp, #4]
	sub	r3, r8, #12
	mul	fp, r8, r4
	ldr	sl, .L1773+4
	b	.L1766
.L1769:
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
	beq	.L1767
	ldr	r2, [sl, #232]
	ldrh	r1, [r2, #0]
	movw	r2, #61664
	cmp	r1, r2
	bne	.L1767
	ldr	r0, .L1773+8
	mov	r1, r4
	mov	r2, r5
	add	r6, r6, #1
	bl	printk
	ldr	r3, .L1773+4
	uxth	r6, r6
	add	r3, r3, r4, asl #1
	strh	r5, [r3, #236]	@ movhi
	b	.L1768
.L1767:
	sub	r5, r5, #1
	uxth	r5, r5
.L1766:
	cmp	r5, r3
	bgt	.L1769
.L1768:
	ldrb	r3, [r9, #3762]	@ zero_extendqisi2
	add	r4, r4, #1
	cmp	r3, r6
	uxtb	r4, r4
	moveq	r7, #0
.L1765:
	ldrb	r3, [r9, #3762]	@ zero_extendqisi2
	cmp	r3, r4
	bhi	.L1771
	mov	r0, r7
	add	sp, sp, #52
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, sl, fp, pc}
.L1774:
	.align	2
.L1773:
	.word	.LANCHOR0
	.word	.LANCHOR2
	.word	.LC129
	.fnend
	.size	FlashLoadFactorBbt, .-FlashLoadFactorBbt
	.align	2
	.global	dump_map_info
	.type	dump_map_info, %function
dump_map_info:
	.fnstart
	@ args = 0, pretend = 0, frame = 8
	@ frame_needed = 0, uses_anonymous_args = 0
	stmfd	sp!, {r4, r5, r6, r7, r8, r9, sl, fp, lr}
	.save {r4, r5, r6, r7, r8, r9, sl, fp, lr}
	mov	r3, #3856
	ldr	r5, .L1788
	.pad #36
	sub	sp, sp, #36
	ldrh	r6, [r5, r3]
	b	.L1776
.L1778:
	add	r3, r5, r7
	mov	r1, r6
	ldrb	r0, [r3, #3874]	@ zero_extendqisi2
	bl	V2P_block
	str	r0, [sp, #28]
	bl	FtlBbmIsBadBlock
	ldr	r3, [sp, #28]
	cmp	r0, #0
	bne	.L1777
	ldr	r2, [r8, #-968]
	mov	r3, r3, asl #10
	mla	r2, r9, r4, r2
	str	r3, [r2, #4]
	ldr	r3, [r8, #-1172]
	str	r3, [r2, #8]
	ldrh	r3, [r5, sl]
	mul	r3, r3, r4
	add	r4, r4, #1
	uxth	r4, r4
	add	r1, r3, #3
	cmp	r3, #0
	movlt	r3, r1
	ldr	r1, [r8, #-1168]
	bic	r3, r3, #3
	add	r3, r1, r3
	str	r3, [r2, #12]
.L1777:
	add	r7, r7, #1
	uxth	r7, r7
	b	.L1781
.L1786:
	ldr	r8, .L1788+4
	mov	r4, #0
	mov	r7, r4
	movw	fp, #3848
	mov	r9, #36
	movw	sl, #3928
.L1781:
	ldrh	r3, [r5, fp]
	cmp	r3, r7
	bhi	.L1778
	cmp	r4, #0
	beq	.L1779
	ldr	sl, .L1788+4
	mov	r1, r4
	mov	r2, #1
	mov	r7, #0
	mov	r8, r7
	ldr	r0, [sl, #-968]
	bl	FlashReadPages
.L1780:
	ldr	r1, [sl, #-968]
	add	r8, r8, #1
	add	r1, r1, r7
	uxth	r8, r8
	add	r7, r7, #36
	ldr	r3, [r1, #12]
	ldr	r2, [r1, #4]
	ldr	r1, [r1, #8]
	ldr	r0, [r3, #4]
	str	r0, [sp, #0]
	ldr	r0, [r3, #8]
	str	r0, [sp, #4]
	ldr	r0, [r3, #12]
	str	r0, [sp, #8]
	ldr	r0, [r1, #0]
	str	r0, [sp, #12]
	ldr	r1, [r1, #4]
	ldr	r0, .L1788+8
	str	r1, [sp, #16]
	ubfx	r1, r2, #10, #16
	ldr	r3, [r3, #0]
	bl	printk
	cmp	r8, r4
	bne	.L1780
.L1779:
	add	r6, r6, #1
	uxth	r6, r6
.L1776:
	movw	r2, #3858
	ldrh	r3, [r5, r2]
	cmp	r3, r6
	bhi	.L1786
	mov	r5, #0
	ldr	r4, .L1788+4
	ldr	r8, .L1788
	b	.L1782
.L1783:
	ldr	r2, [r4, #-1036]
	mov	r1, #1
	ldr	r0, .L1788+12
	add	r6, r6, #1
	ldrh	r2, [r2, r7]
	uxth	r6, r6
	orr	r3, r3, r2, asl #10
	mov	r2, r1
	str	r3, [r4, #188]
	bl	FlashReadPages
	ldr	r3, [r4, #196]
	ldr	r1, [r4, #-1036]
	ldr	r2, [r4, #192]
	ldr	r0, [r3, #0]
	ldrh	r1, [r1, r7]
	str	r0, [sp, #0]
	ldr	r0, [r3, #4]
	str	r0, [sp, #4]
	ldr	r0, [r3, #8]
	str	r0, [sp, #8]
	ldr	r3, [r3, #12]
	ldr	r0, .L1788+16
	str	r3, [sp, #12]
	ldr	r3, [r2, #0]
	str	r3, [sp, #16]
	ldr	r3, [r2, #4]
	str	r3, [sp, #20]
	ldr	r2, [r4, #184]
	ldr	r3, [r4, #188]
	bl	printk
.L1785:
	movw	r2, #3918
	sxth	r3, r6
	ldrh	r2, [r8, r2]
	cmp	r3, r2
	blt	.L1783
	add	r5, r5, #1
	uxth	r5, r5
.L1782:
	ldr	r3, .L1788+20
	sxth	r7, r5
	ldr	r6, .L1788+4
	ldrh	r3, [r4, r3]
	cmp	r7, r3
	movlt	r7, r7, asl #1
	movlt	r6, #0
	blt	.L1785
.L1784:
	ldr	r4, .L1788
	mov	r5, #3952
	ldr	r1, [r6, #-1036]
	mov	r2, #2
	ldr	r0, .L1788+24
	ldr	r3, [r4, #3944]
	bl	rknand_print_hex
	ldr	r1, [r6, #-1028]
	ldrh	r3, [r4, r5]
	mov	r2, #4
	ldr	r0, .L1788+28
	bl	rknand_print_hex
	ldr	r0, .L1788+32
	ldr	r1, [r6, #-896]
	mov	r2, #4
	ldrh	r3, [r4, r5]
	add	sp, sp, #36
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, sl, fp, lr}
	b	rknand_print_hex
.L1789:
	.align	2
.L1788:
	.word	.LANCHOR0
	.word	.LANCHOR2
	.word	.LC130
	.word	.LANCHOR2+184
	.word	.LC108
	.word	-1040
	.word	.LC131
	.word	.LC132
	.word	.LC133
	.fnend
	.size	dump_map_info, .-dump_map_info
	.align	2
	.global	FtlDumpSysBlock
	.type	FtlDumpSysBlock, %function
FtlDumpSysBlock:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	stmfd	sp!, {r4, r5, r6, r7, r8, lr}
	.save {r4, r5, r6, r7, r8, lr}
	mov	r6, r0
	ldr	r4, .L1794
	.pad #24
	sub	sp, sp, #24
	mov	r7, r0, asl #10
	mov	r5, #0
	ldr	r8, .L1794+4
	ldr	r3, [r4, #-956]
	str	r3, [r4, #192]
	ldr	r3, [r4, #-932]
	str	r3, [r4, #196]
	b	.L1791
.L1793:
	mov	r1, #1
	ldr	r0, .L1794+8
	mov	r2, r1
	orr	r3, r3, r7
	str	r3, [r4, #188]
	bl	FlashReadPages
	ldr	r3, [r4, #196]
	mov	r1, r6
	ldr	r0, .L1794+12
	ldr	r2, [r3, #0]
	str	r2, [sp, #0]
	ldr	r2, [r3, #4]
	str	r2, [sp, #4]
	ldr	r2, [r3, #8]
	str	r2, [sp, #8]
	ldr	r3, [r3, #12]
	ldr	r2, [r4, #184]
	str	r3, [sp, #12]
	ldr	r3, [r4, #192]
	ldr	r3, [r3, #0]
	str	r3, [sp, #16]
	ldr	r3, [r4, #188]
	bl	printk
	ldr	r3, [r4, #196]
	ldr	r3, [r3, #0]
	cmn	r3, #1
	beq	.L1792
	ldr	r0, .L1794+16
	mov	r2, #4
	ldr	r1, [r4, #-956]
	mov	r3, #768
	bl	rknand_print_hex
.L1792:
	add	r5, r5, #1
	uxth	r5, r5
.L1791:
	movw	r2, #3918
	sxth	r3, r5
	ldrh	r2, [r8, r2]
	cmp	r3, r2
	blt	.L1793
	add	sp, sp, #24
	ldmfd	sp!, {r4, r5, r6, r7, r8, pc}
.L1795:
	.align	2
.L1794:
	.word	.LANCHOR2
	.word	.LANCHOR0
	.word	.LANCHOR2+184
	.word	.LC134
	.word	.LC135
	.fnend
	.size	FtlDumpSysBlock, .-FtlDumpSysBlock
	.align	2
	.global	FlashReadFacBbtData
	.type	FlashReadFacBbtData, %function
FlashReadFacBbtData:
	.fnstart
	@ args = 0, pretend = 0, frame = 40
	@ frame_needed = 0, uses_anonymous_args = 0
	ldr	r3, .L1806
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
	ldr	r7, .L1806+4
	mov	r4, r0
	mul	r8, r8, r2
	ldr	r3, [r7, #152]
	str	r3, [sp, #12]
	ldr	r3, [r7, #232]
	uxth	r8, r8
	sub	r5, r8, #1
	mul	sl, r8, r6
	str	r3, [sp, #16]
	uxth	r5, r5
	sub	r8, r8, #16
	b	.L1797
.L1803:
	mov	r1, #1
	add	r3, r5, sl
	add	r0, sp, #4
	mov	r2, r1
	mov	r3, r3, asl #10
	str	r3, [sp, #8]
	bl	FlashReadPages
	ldr	r3, [sp, #4]
	cmn	r3, #1
	beq	.L1798
	ldr	r3, [r7, #232]
	ldrh	r2, [r3, #0]
	movw	r3, #61664
	cmp	r2, r3
	bne	.L1798
	cmp	r4, #0
	moveq	r0, r4
	beq	.L1799
	cmp	r6, #0
	ldreq	r3, .L1806+4
	moveq	r0, #1
	beq	.L1800
	b	.L1801
.L1802:
	ldr	r2, [r3, #152]
	ubfx	r1, r6, #5, #16
	and	lr, r6, #31
	add	r6, r6, #1
	ldr	ip, [r2, r1, asl #2]
	uxth	r6, r6
	orr	ip, ip, r0, asl lr
	str	ip, [r2, r1, asl #2]
.L1800:
	ldr	r2, [r3, #160]
	cmp	r6, r2
	bcc	.L1802
.L1801:
	ldr	r3, .L1806+4
	mov	r2, r9
	mov	r0, r4
	ldr	r1, [r3, #152]
	bl	memcpy
	mov	r2, #4
	ldr	r0, .L1806+8
	mov	r1, r4
	mov	r3, r2
	bl	rknand_print_hex
	mov	r0, #0
	b	.L1799
.L1798:
	sub	r5, r5, #1
	uxth	r5, r5
.L1797:
	cmp	r5, r8
	bgt	.L1803
	mvn	r0, #0
.L1799:
	add	sp, sp, #40
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, sl, pc}
.L1807:
	.align	2
.L1806:
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
	ldr	r3, .L1815
	stmfd	sp!, {r4, r5, r6, r7, r8, lr}
	.save {r4, r5, r6, r7, r8, lr}
	mov	r5, r0
	ldr	r3, [r3, #3624]
	ldr	r6, .L1815+4
	ldrb	r2, [r3, #13]	@ zero_extendqisi2
	ldrh	r4, [r3, #14]
	ldr	r0, [r6, #140]
	mul	r4, r4, r2
	uxth	r4, r4
	add	r2, r4, #7
	mov	r2, r2, lsr #3
	bl	FlashReadFacBbtData
	cmn	r0, #1
	beq	.L1814
	mov	r2, #0
	mov	r0, r4, lsr #4
	mov	r3, r2
	sub	r4, r4, #1
	mov	r1, #1
	b	.L1810
.L1812:
	ldr	ip, [r6, #140]
	mov	r8, r2, lsr #5
	and	r7, r2, #31
	ldr	ip, [ip, r8, asl #2]
	ands	ip, ip, r1, asl r7
	movne	ip, r3, asl #1
	addne	r3, r3, #1
	uxthne	r3, r3
	strneh	r2, [r5, ip]	@ movhi
	cmp	r3, r0
	bcs	.L1814
	add	r2, r2, #1
	uxth	r2, r2
.L1810:
	cmp	r2, r4
	blt	.L1812
	b	.L1809
.L1814:
	mov	r3, #0
.L1809:
	mov	r3, r3, asl #1
	mvn	r2, #0
	mov	r0, #0
	strh	r2, [r5, r3]	@ movhi
	ldmfd	sp!, {r4, r5, r6, r7, r8, pc}
.L1816:
	.align	2
.L1815:
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
	ldr	r6, .L1835
	ldr	r5, [r6, #4040]
	cmp	r5, #0
	bne	.L1818
	ldr	r8, .L1835+4
	bl	FtlBbtMemInit
	ldr	r4, .L1835+8
	bl	FtlLoadFactoryBbt
	add	sl, r8, #12
	b	.L1819
.L1825:
	ldrh	r3, [sl], #2
	movw	r2, #65535
	ldr	r0, [r4, #-956]
	movw	fp, #3912
	ldr	r9, [r4, #-932]
	cmp	r3, r2
	str	r0, [r4, #192]
	str	r9, [r4, #196]
	beq	.L1820
	ldrh	r7, [r6, fp]
	mov	r1, #1
	mov	r2, r1
	ldr	r0, .L1835+12
	mla	r7, r7, r5, r3
	mov	r3, r7, asl #10
	str	r3, [r4, #188]
	bl	FlashReadPages
	ldrh	r2, [r6, fp]
	ldr	r0, [r8, #28]
	add	r2, r2, #7
	ldr	r1, [r4, #192]
	mov	r2, r2, lsr #3
	bl	memcpy
	b	.L1821
.L1820:
	mov	r1, r5
	bl	FlashGetBadBlockList
	ldr	r0, [r4, #192]
	ldr	r1, [r8, #28]
	bl	FtlBbt2Bitmap
	ldrh	fp, [r6, fp]
.L1823:
	sub	fp, fp, #1
	uxth	fp, fp
.L1834:
	movw	r7, #3912
	ldrh	r0, [r6, r7]
	mla	r0, r0, r5, fp
	uxth	r0, r0
	bl	FtlBbmIsBadBlock
	cmp	r0, #1
	beq	.L1823
	mov	r1, #0
	mov	r2, #16
	strh	fp, [sl, #-2]	@ movhi
	ldr	r0, [r4, #-932]
	bl	ftl_memset
	ldr	r3, .L1835+16
	strh	r3, [r9, #0]	@ movhi
	mov	r3, #0
	str	r3, [r9, #4]
	ldrh	r3, [sl, #-2]
	ldrh	r7, [r6, r7]
	strh	r3, [r9, #2]	@ movhi
	ldrh	r3, [sl, #-2]
	ldr	r1, [r8, #28]
	ldr	r0, [r4, #192]
	mla	r7, r7, r5, r3
	mov	r3, r7, asl #10
	str	r3, [r4, #188]
	ldr	r3, .L1835+20
	ldrh	r2, [r4, r3]
	mov	r2, r2, asl #2
	bl	memcpy
	mov	r1, #1
	mov	r2, r1
	ldr	r0, .L1835+12
	bl	FlashEraseBlocks
	mov	r1, #1
	mov	r3, r1
	ldr	r0, .L1835+12
	mov	r2, r1
	bl	FlashProgPages
	ldr	r3, [r4, #184]
	cmn	r3, #1
	bne	.L1821
	uxth	r0, r7
	bl	FtlBbmMapBadBlock
	b	.L1834
.L1821:
	uxth	r0, r7
	add	r5, r5, #1
	bl	FtlBbmMapBadBlock
	add	r8, r8, #4
.L1819:
	movw	r3, #3870
	ldrh	r3, [r6, r3]
	cmp	r5, r3
	bcc	.L1825
	mov	r4, #0
	ldr	r7, .L1835
	movw	r6, #3930
	b	.L1826
.L1827:
	mov	r0, r4
	add	r4, r4, #1
	bl	FtlBbmMapBadBlock
	uxth	r4, r4
.L1826:
	ldrh	r3, [r7, r6]
	ldr	r5, .L1835
	cmp	r3, r4
	bhi	.L1827
	movw	r7, #3992
	movw	r6, #3980
	ldrh	r4, [r5, r7]
	sub	r4, r4, #1
	uxth	r4, r4
	b	.L1828
.L1833:
	mov	r0, r4
	bl	FtlBbmIsBadBlock
	cmp	r0, #1
	beq	.L1829
	mov	r0, r4
	bl	FlashTestBlk
	cmp	r0, #0
	beq	.L1830
	mov	r0, r4
	bl	FtlBbmMapBadBlock
	b	.L1829
.L1830:
	ldrh	r2, [r5, r6]
	movw	r3, #65535
	cmp	r2, r3
	streqh	r4, [r5, r6]	@ movhi
.L1831:
	ldrne	r2, .L1835
	movne	r3, #3984
	strneh	r4, [r2, r3]	@ movhi
	bne	.L1832
.L1829:
	sub	r4, r4, #1
	uxth	r4, r4
.L1828:
	ldrh	r3, [r5, r7]
	sub	r3, r3, #48
	cmp	r4, r3
	bgt	.L1833
.L1832:
	ldr	r4, .L1835
	movw	r6, #3980
	mov	r5, #3984
	mov	r7, #0
	movw	r8, #3982
	mov	r2, #2
	ldr	r0, [r4, #4044]
	mov	r1, #1
	ldrh	r3, [r4, r6]
	str	r7, [r4, #3988]
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
	ldr	r3, [r4, #3988]
	ldrh	r2, [r4, r5]
	add	r3, r3, #1
	str	r3, [r4, #3988]
	ldrh	r3, [r4, r6]
	strh	r7, [r4, r8]	@ movhi
	strh	r2, [r4, r6]	@ movhi
	strh	r3, [r4, r5]	@ movhi
	bl	FtlBbmTblFlush
.L1818:
	mov	r0, #0
	ldmfd	sp!, {r3, r4, r5, r6, r7, r8, r9, sl, fp, pc}
.L1836:
	.align	2
.L1835:
	.word	.LANCHOR0
	.word	.LANCHOR0+3980
	.word	.LANCHOR2
	.word	.LANCHOR2+184
	.word	-3872
	.word	-1024
	.fnend
	.size	FtlMakeBbt, .-FtlMakeBbt
	.align	2
	.type	ftl_load_l2p_region, %function
ftl_load_l2p_region:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	stmfd	sp!, {r3, r4, r5, r6, r7, lr}
	.save {r3, r4, r5, r6, r7, lr}
	mov	r5, r0
	ldr	r4, .L1842
	mov	r6, #12
	ldr	r3, [r4, #-1028]
	ldr	r7, [r3, r0, asl #2]
	cmp	r7, #0
	bne	.L1838
	mul	r6, r6, r1
	ldr	r3, [r4, #-1852]
	ldr	r2, .L1842+4
	mov	r1, #255
	add	r3, r3, r6
	ldr	r0, [r3, #8]
	movw	r3, #3926
	ldrh	r2, [r2, r3]
	bl	ftl_memset
	ldr	r3, [r4, #-1852]
	strh	r5, [r3, r6]	@ movhi
	ldr	r3, [r4, #-1852]
	add	r6, r3, r6
	str	r7, [r6, #4]
	b	.L1839
.L1838:
	mul	r6, r6, r1
	ldr	r3, [r4, #-1852]
	mov	r1, #1
	add	r0, r4, #184
	mov	r2, r1
	str	r7, [r4, #188]
	add	r3, r3, r6
	ldr	r3, [r3, #8]
	str	r3, [r4, #192]
	ldr	r3, [r4, #-932]
	str	r3, [r4, #196]
	bl	FlashReadPages
	ldr	r3, [r4, #196]
	ldrh	r3, [r3, #8]
	cmp	r3, r5
	beq	.L1840
	mov	r2, r7
	mov	r1, r5
	ldr	r0, .L1842+8
	bl	printk
	ldr	r7, .L1842+4
	mov	r2, #4
	ldr	r0, .L1842+12
	mov	r3, r2
	ldr	r1, [r4, #196]
	bl	rknand_print_hex
	mov	r3, #3952
	ldrh	r3, [r7, r3]
	mov	r2, #4
	ldr	r0, .L1842+16
	ldr	r1, [r4, #-1028]
	bl	rknand_print_hex
	mov	r3, #1
	str	r3, [r7, #4040]
	b	.L1841
.L1840:
	ldr	r3, [r4, #184]
	cmp	r3, #256
	bne	.L1841
	mov	r1, r5
	mov	r2, r7
	ldr	r0, .L1842+20
	bl	printk
	ldr	r3, [r4, #-1852]
	ldr	r0, .L1842+24
	mov	r1, r5
	add	r3, r3, r6
	ldr	r2, [r3, #8]
	bl	FtlMapWritePage
.L1841:
	ldr	r3, .L1842
	mov	r1, #0
	ldr	r3, [r3, #-1852]
	add	r2, r3, r6
	str	r1, [r2, #4]
	strh	r5, [r3, r6]	@ movhi
.L1839:
	mov	r0, #0
	ldmfd	sp!, {r3, r4, r5, r6, r7, pc}
.L1843:
	.align	2
.L1842:
	.word	.LANCHOR2
	.word	.LANCHOR0
	.word	.LC137
	.word	.LC99
	.word	.LC138
	.word	.LC139
	.word	.LANCHOR2-1084
	.fnend
	.size	ftl_load_l2p_region, .-ftl_load_l2p_region
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
	ldr	r3, .L1853
	movw	r2, #3924
	mov	r4, r1
	ldrh	r7, [r3, r2]
	mvn	r2, #0
	add	r7, r7, #7
	mov	r6, r0, lsr r7
	bic	r7, r0, r2, asl r7
	movw	r2, #3954
	ldrh	r2, [r3, r2]
	uxth	r6, r6
	ldr	r3, .L1853+4
	uxth	r7, r7
	ldr	sl, [r3, #-1852]
	mov	r3, #0
	mov	r5, r3
	b	.L1845
.L1851:
	add	r3, r3, #12
	add	r1, sl, r3
	ldrh	r1, [r1, #-12]
	cmp	r1, r6
	bne	.L1846
.L1847:
	cmp	r8, #0
	ldr	r3, .L1853+4
	mov	r2, #12
	bne	.L1848
	ldr	r3, [r3, #-1852]
	mla	r2, r2, r5, r3
	ldr	r3, [r2, #8]
	ldr	r3, [r3, r7, asl #2]
	str	r3, [r4, #0]
	b	.L1849
.L1848:
	mul	r2, r2, r5
	ldr	r1, [r3, #-1852]
	ldr	r0, [r4, #0]
	add	r1, r1, r2
	ldr	r1, [r1, #8]
	str	r0, [r1, r7, asl #2]
	ldr	r1, [r3, #-1852]
	add	r2, r1, r2
	ldr	r1, [r2, #4]
	orr	r1, r1, #-2147483648
	str	r1, [r2, #4]
	ldr	r2, .L1853+8
	strh	r6, [r3, r2]	@ movhi
.L1849:
	ldr	r3, .L1853+4
	mov	r2, #12
	mov	r0, #0
	ldr	r3, [r3, #-1852]
	mla	r5, r2, r5, r3
	ldr	r3, [r5, #4]
	cmn	r3, #1
	addne	r3, r3, #1
	strne	r3, [r5, #4]
	ldmfd	sp!, {r3, r4, r5, r6, r7, r8, sl, pc}
.L1846:
	add	r5, r5, #1
	uxth	r5, r5
.L1845:
	cmp	r5, r2
	bne	.L1851
	bl	select_l2p_ram_region
	mov	r3, #12
	mul	r3, r3, r0
	mov	r5, r0
	add	r2, sl, r3
	ldrh	r1, [sl, r3]
	movw	r3, #65535
	cmp	r1, r3
	beq	.L1852
	ldr	r3, [r2, #4]
	cmp	r3, #0
	bge	.L1852
	bl	flush_l2p_region
.L1852:
	mov	r0, r6
	mov	r1, r5
	bl	ftl_load_l2p_region
	b	.L1847
.L1854:
	.align	2
.L1853:
	.word	.LANCHOR0
	.word	.LANCHOR2
	.word	-1848
	.fnend
	.size	log2phys, .-log2phys
	.align	2
	.type	FtlReadRefresh.part.15, %function
FtlReadRefresh.part.15:
	.fnstart
	@ args = 0, pretend = 0, frame = 40
	@ frame_needed = 0, uses_anonymous_args = 0
	stmfd	sp!, {r4, r5, r6, lr}
	.save {r4, r5, r6, lr}
	.pad #40
	sub	sp, sp, #40
	ldr	r4, .L1863
	ldr	r6, .L1863+4
	ldr	r2, [r4, #-1624]
	ldr	r3, [r6, #3976]
	cmp	r2, r3
	bcs	.L1856
	mov	r5, #2048
.L1859:
	ldr	r0, [r4, #-1624]
	ldr	r3, [r6, #3976]
	cmp	r0, r3
	bcs	.L1861
	mov	r1, sp
	mov	r2, #0
	bl	log2phys
	ldr	r0, [sp, #0]
	ldr	r3, [r4, #-1624]
	cmn	r0, #1
	add	r3, r3, #1
	str	r3, [r4, #-1624]
	beq	.L1858
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
	bne	.L1861
	ldr	r0, [sp, #0]
	ubfx	r0, r0, #10, #16
	bl	P2V_block_in_plane
	bl	FtlGcRefreshBlock
	b	.L1861
.L1858:
	subs	r5, r5, #1
	bne	.L1859
	b	.L1857
.L1856:
	ldr	r3, [r4, #-1836]
	mov	r0, #0
	str	r0, [r4, #-1628]
	str	r0, [r4, #-1624]
	str	r3, [r4, #-1632]
	b	.L1857
.L1861:
	mvn	r0, #0
.L1857:
	add	sp, sp, #40
	ldmfd	sp!, {r4, r5, r6, pc}
.L1864:
	.align	2
.L1863:
	.word	.LANCHOR2
	.word	.LANCHOR0
	.fnend
	.size	FtlReadRefresh.part.15, .-FtlReadRefresh.part.15
	.align	2
	.global	FtlReadRefresh
	.type	FtlReadRefresh, %function
FtlReadRefresh:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	@ link register save eliminated.
	ldr	r3, .L1872
	ldr	r2, [r3, #-1628]
	cmp	r2, #0
	beq	.L1866
	b	FtlReadRefresh.part.15
.L1866:
	ldr	r2, .L1872+4
	ldr	r0, [r3, #-1784]
	ldr	r1, [r2, #3840]
	ldrb	r2, [r2, #928]	@ zero_extendqisi2
	cmp	r2, #0
	ldr	r2, [r3, #-1836]
	addeq	r0, r1, r0, lsr #10
	moveq	r1, #33554432
	movne	r0, #4194304
	moveq	r0, r1, asr r0
	ldr	r1, [r3, #-1632]
	add	r3, r2, #1048576
	cmp	r1, r3
	bhi	.L1868
	add	r1, r0, r1
	cmp	r1, r2
	bcc	.L1868
	ldr	r3, .L1872
	ldrb	r3, [r3, #-2044]	@ zero_extendqisi2
	cmp	r3, #0
	bne	.L1869
.L1868:
	ldr	r3, .L1872
	mov	r1, #1
	str	r2, [r3, #-1632]
	str	r1, [r3, #-1628]
	mov	r1, #0
	str	r1, [r3, #-1624]
.L1869:
	mov	r0, #0
	bx	lr
.L1873:
	.align	2
.L1872:
	.word	.LANCHOR2
	.word	.LANCHOR0
	.fnend
	.size	FtlReadRefresh, .-FtlReadRefresh
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
	ldr	r3, .L1880
	ldr	r1, [r3, #-2016]
	mov	r5, r0, asl #1
	ldrh	r2, [r1, r5]
	cmp	r2, #0
	addne	r2, r2, #1
	strneh	r2, [r1, r5]	@ movhi
	bne	.L1876
	ldr	r4, [r3, #-2004]
	cmp	r4, #0
	beq	.L1876
	ldr	r1, .L1880+4
	mov	ip, #6
	ldrh	lr, [r3, r1]
	ldr	r3, [r3, #-2024]
	ldr	r1, .L1880+8
	rsb	r4, r3, r4
	mov	r4, r4, asr #1
	mul	r4, r1, r4
	movw	r1, #65535
	uxth	r4, r4
	b	.L1877
.L1879:
	cmp	r4, r0
	bne	.L1878
	mov	r1, r4
	ldr	r0, .L1880+12
	bl	List_remove_node
	ldr	r6, .L1880
	ldr	r3, .L1880+4
	mov	r0, r4
	ldrh	r2, [r6, r3]
	sub	r2, r2, #1
	strh	r2, [r6, r3]	@ movhi
	bl	INSERT_DATA_LIST
	ldr	r3, [r6, #-2016]
	ldrh	r2, [r3, r5]
	add	r2, r2, #1
	strh	r2, [r3, r5]	@ movhi
	b	.L1876
.L1878:
	mul	r4, ip, r4
	ldrh	r4, [r3, r4]
	cmp	r4, r1
	beq	.L1876
	add	r2, r2, #1
	uxth	r2, r2
.L1877:
	cmp	r2, lr
	bne	.L1879
.L1876:
	mov	r0, r7
	add	r1, sp, #4
	mov	r2, #1
	bl	log2phys
	ldmfd	sp!, {r1, r2, r3, r4, r5, r6, r7, pc}
.L1881:
	.align	2
.L1880:
	.word	.LANCHOR2
	.word	-2000
	.word	-1431655765
	.word	.LANCHOR2-2004
	.fnend
	.size	FtlReUsePrevPpa, .-FtlReUsePrevPpa
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
	ldr	r4, .L1888
	.pad #32
	sub	sp, sp, #32
	ldr	r0, .L1888+4
	mov	r5, #0
	bl	printk
	ldr	r7, .L1888+8
	mov	r6, r4
	b	.L1883
.L1887:
	mov	r0, r5
	add	r1, sp, #28
	mov	r2, #0
	bl	log2phys
	movs	r3, r5, asl #21
	bne	.L1884
	ldr	r0, .L1888+12
	mov	r1, r5
	ldr	r2, [sp, #28]
	bl	printk
.L1884:
	ldr	r3, [sp, #28]
	cmn	r3, #1
	beq	.L1885
	str	r3, [r4, #188]
	mov	r2, #0
	ldr	r3, [r4, #-956]
	mov	r1, #1
	ldr	r8, [r4, #-932]
	ldr	r0, .L1888+16
	str	r3, [r4, #192]
	str	r5, [r4, #200]
	str	r8, [r4, #196]
	str	r2, [r4, #184]
	bl	FlashReadPages
	ldr	r3, [r4, #184]
	cmn	r3, #1
	cmpne	r3, #256
	beq	.L1886
	ldr	r3, [r8, #8]
	cmp	r3, r5
	beq	.L1885
.L1886:
	ldr	r3, [r6, #196]
	ldr	r2, [r6, #192]
	ldr	r0, .L1888+20
	ldr	r1, [r3, #4]
	str	r1, [sp, #0]
	ldr	r1, [r3, #8]
	str	r1, [sp, #4]
	ldr	r1, [r3, #12]
	str	r1, [sp, #8]
	ldr	r1, [r2, #0]
	str	r1, [sp, #12]
	mov	r1, r5
	ldr	r2, [r2, #4]
	str	r2, [sp, #16]
	ldr	r2, [r6, #188]
	ldr	r3, [r3, #0]
	bl	printk
.L1885:
	add	r5, r5, #1
.L1883:
	ldr	r3, [r7, #3976]
	cmp	r5, r3
	bcc	.L1887
	add	sp, sp, #32
	ldmfd	sp!, {r4, r5, r6, r7, r8, pc}
.L1889:
	.align	2
.L1888:
	.word	.LANCHOR2
	.word	.LC140
	.word	.LANCHOR0
	.word	.LC141
	.word	.LANCHOR2+184
	.word	.LC142
	.fnend
	.size	Ftlscanalldata, .-Ftlscanalldata
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
	beq	.L1891
	ldrh	r3, [r0, #2]
	ldr	r1, .L1979
	ldrb	r6, [r0, #6]	@ zero_extendqisi2
	str	r3, [sp, #20]
	movw	r3, #3916
	ldrh	r3, [r1, r3]
	ldr	fp, [sp, #20]
	str	r6, [sp, #36]
	cmp	r3, fp
	mov	r3, #0
	streqh	r3, [r0, #4]	@ movhi
	moveq	r2, r3	@ movhi
	ldrneh	r0, [r0, #16]
	bne	.L1893
	b	.L1976
.L1894:
	add	r3, r3, #1
	uxth	r3, r3
	add	r1, r4, r3, asl #1
	ldrh	r0, [r1, #16]
.L1893:
	cmp	r0, r2
	beq	.L1894
	ldrb	r1, [r4, #8]	@ zero_extendqisi2
	cmp	r1, #1
	bne	.L1895
	bl	FtlGetLastWrittenPage
	cmn	r0, #1
	mov	r7, r0
	beq	.L1896
	ldr	r3, .L1979
	ldrb	r2, [r3, #928]	@ zero_extendqisi2
	cmp	r2, #0
	bne	.L1969
	add	r3, r3, r0, asl #1
	movw	r2, #3076
	ldrh	r8, [r3, r2]
	b	.L1961
.L1895:
	mov	r1, #0
	bl	FtlGetLastWrittenPage
	cmn	r0, #1
	mov	r7, r0
	beq	.L1896
.L1969:
	mov	r8, r7
.L1961:
	ldr	r3, .L1979
	movw	r2, #3848
	mov	r5, #0
	str	r4, [sp, #44]
	movw	ip, #65535
	ldrh	r9, [r3, r2]
	ldr	r2, .L1979+4
	ldr	sl, [r2, #-968]
	ldr	r6, [r2, #-1168]
	movw	r2, #3928
	ldrh	lr, [r3, r2]
	mov	r2, r4
	mov	r3, r5
	b	.L1898
.L1896:
	mov	r3, #0
	strh	r3, [r4, #2]	@ movhi
	mov	r2, r3	@ movhi
.L1976:
	strb	r2, [r4, #6]
	b	.L1891
.L1900:
	ldrh	r0, [r2, #16]
	cmp	r0, ip
	beq	.L1899
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
.L1899:
	add	r3, r3, #1
	add	r2, r2, #2
	uxth	r3, r3
.L1898:
	cmp	r3, r9
	bne	.L1900
	ldrb	r3, [r4, #8]	@ zero_extendqisi2
	cmp	r3, #1
	movne	r3, #0
	bne	.L1970
	ldr	r3, .L1979
	ldrb	r3, [r3, #928]	@ zero_extendqisi2
	adds	r3, r3, #0
	movne	r3, #1
.L1970:
	ldr	r6, .L1979+4
	mov	r1, r5
	str	r3, [sp, #32]
	mov	sl, #0
	ldr	r2, [sp, #32]
	ldr	r0, [r6, #-968]
	bl	FlashReadPages
	ldr	fp, [r6, #-1800]
	movw	r2, #65535
	str	r2, [sp, #12]
	str	fp, [sp, #16]
	b	.L1902
.L1907:
	mov	r3, #36
	ldr	r1, [r6, #-968]
	mul	r2, r3, sl
	add	r3, r1, r2
	ldr	r2, [r1, r2]
	cmp	r2, #0
	bne	.L1903
	ldr	r3, [r3, #12]
	ldr	fp, [r3, #4]
	cmn	fp, #1
	beq	.L1904
	ldr	r1, [r6, #-1800]
	mov	r0, fp
	str	r3, [sp, #4]
	bl	ftl_cmp_data_ver
	ldr	r3, [sp, #4]
	cmp	r0, #0
	addne	fp, fp, #1
	strne	fp, [r6, #-1800]
.L1904:
	ldr	r3, [r3, #0]
	cmn	r3, #1
	bne	.L1906
	b	.L1905
.L1903:
	ldr	r1, [r3, #4]
	uxth	fp, r8
	ldr	r0, .L1979+8
	bl	printk
	ldrh	r3, [r4, #0]
	movw	lr, #298
	str	fp, [sp, #12]
	strh	r3, [r6, lr]	@ movhi
.L1906:
	add	sl, sl, #1
.L1902:
	uxth	r9, sl
	cmp	r9, r5
	bcc	.L1907
.L1905:
	ldr	r3, .L1979+4
	cmp	r9, r5
	addeq	r7, r7, #1
	uxthne	r7, r7
	movne	r2, #36
	ldrne	r3, [r3, #-968]
	uxtheq	r7, r7
	ldreq	r3, [r3, #-968]
	mlane	r9, r2, r9, r3
	ldreq	r0, [r3, #4]
	ldrne	r0, [r9, #4]
	ubfx	r0, r0, #10, #16
	bl	P2V_plane
	ldrb	r2, [r4, #8]	@ zero_extendqisi2
	ldr	r3, .L1979
	cmp	r2, #1
	str	r0, [sp, #24]
	bne	.L1910
	ldrb	r1, [r3, #928]	@ zero_extendqisi2
	cmp	r1, #0
	addeq	r7, r3, r7, asl #1
	movweq	r1, #3076
	ldreqh	r7, [r7, r1]
.L1910:
	movw	r1, #3916
	ldr	r6, [sp, #24]
	ldrh	r3, [r3, r1]
	ldr	fp, [sp, #36]
	cmp	r3, r7
	streqh	r7, [r4, #2]	@ movhi
	moveq	r3, #0
	streqb	r3, [r4, #6]
	streqh	r3, [r4, #4]	@ movhi
	ldr	r3, [sp, #20]
	cmp	r7, r3
	cmpeq	r6, fp
	moveq	r0, r4
	moveq	r1, r7
	moveq	r2, r6
	beq	.L1977
	ldr	r3, [sp, #16]
	ldr	fp, [sp, #12]
	sub	r6, r3, #1
	movw	r3, #65535
	cmp	fp, r3
	bne	.L1913
	cmp	r2, #0
	bne	.L1914
.L1913:
	ldr	r2, .L1979+4
	uxth	r3, r8
	ldr	fp, [sp, #20]
	movw	ip, #65535
	mov	sl, r3
	ldr	r8, .L1979+4
	ldr	r1, [r2, #-1004]
	cmn	r1, #1
	streq	r6, [r2, #-1004]
	ldr	r2, [r2, #-1004]
	str	r2, [sp, #12]
	add	r2, fp, #7
	cmp	r3, r2
	mvn	fp, #0
	subgt	r5, r3, #7
	mov	r9, fp
	mov	r3, r7
	ldrle	r5, [sp, #20]
	mov	r7, r6
	uxthgt	r5, r5
	mov	r6, r4
	b	.L1917
.L1919:
	ldrh	r0, [r1, #16]
	cmp	r0, ip
	beq	.L1918
	ldr	r6, [sp, #28]
	mov	r3, #36
	orr	r0, r5, r0, asl #10
	mla	lr, r3, r4, r6
	add	r4, r4, #1
	uxth	r4, r4
	str	r0, [lr, #4]
.L1918:
	add	r2, r2, #1
	add	r1, r1, #2
	uxth	r2, r2
.L1930:
	ldr	lr, [sp, #16]
	cmp	r2, lr
	bne	.L1919
	ldr	r3, [sp, #40]
	mov	r1, r4
	ldr	r0, [r8, #-968]
	ldr	r2, [sp, #32]
	stmib	sp, {r3, ip}
	ldr	r6, [sp, #0]
	bl	FlashReadPages
	ldr	r2, .L1979
	add	r0, r8, r5, asl #1
	mov	r1, #0
	str	r0, [sp, #28]
	ldmib	sp, {r3, ip}
	ldrb	r2, [r2, #928]	@ zero_extendqisi2
	str	r2, [sp, #16]
	ldr	r2, [r8, #-968]
	b	.L1920
.L1928:
	ldr	r0, [r2, #0]
	cmp	r0, #0
	bne	.L1921
	ldr	r0, [r2, #12]
	ldrh	lr, [r0, #0]
	cmp	lr, ip
	beq	.L1922
	ldr	r0, [r0, #4]
	cmn	r0, #1
	beq	.L1922
	cmn	fp, #1
	ldr	r9, [r8, #-1004]
	str	r0, [r8, #-1004]
	bne	.L1922
	ldr	lr, [sp, #28]
	sub	r0, lr, #888
	ldrh	r0, [r0, #0]
	cmp	r0, ip
	bne	.L1923
	ldr	r0, [sp, #16]
	cmp	r0, #0
	beq	.L1922
.L1923:
	cmp	r9, r7
	mvneq	fp, #0
	movne	fp, r9
	b	.L1922
.L1921:
	mov	r4, r6
	mov	r6, r7
	ldrh	r1, [r4, #0]
	mov	r7, r3
	ldr	r3, .L1979+4
	movw	r2, #298
	strh	r1, [r3, r2]	@ movhi
	ldrb	r2, [r4, #8]	@ zero_extendqisi2
	cmp	r2, #0
	bne	.L1914
	add	r5, r3, r5, asl #1
	movw	r2, #65535
	sub	r5, r5, #888
	ldrh	r1, [r5, #0]
	cmp	r1, r2
	bne	.L1924
	cmn	fp, #1
	bne	.L1972
.L1925:
	ldr	fp, [sp, #12]
	cmp	fp, r6
	beq	.L1926
.L1972:
	str	fp, [r3, #-1004]
	b	.L1914
.L1926:
	ldr	r2, [r3, #-1004]
	b	.L1978
.L1924:
	cmp	r9, r6
	beq	.L1927
	cmn	r9, #1
	strne	r9, [r3, #-1004]
	b	.L1914
.L1927:
	ldr	r2, [r3, #-1004]
	cmp	r2, r6
	beq	.L1914
.L1978:
	sub	r2, r2, #1
	b	.L1973
.L1922:
	add	r1, r1, #1
	add	r2, r2, #36
	uxth	r1, r1
.L1920:
	cmp	r1, r4
	bne	.L1928
	add	r5, r5, #1
	uxth	r5, r5
.L1917:
	cmp	r5, sl
	bhi	.L1929
	ldr	r1, .L1979
	movw	r2, #3848
	ldr	lr, [r8, #-968]
	mov	r4, #0
	str	r3, [sp, #40]
	ldrh	r2, [r1, r2]
	mov	r1, r6
	str	lr, [sp, #28]
	str	r6, [sp, #0]
	str	r2, [sp, #16]
	mov	r2, r4
	b	.L1930
.L1929:
	mov	r4, r6
	mov	r6, r7
	mov	r7, r3
	ldr	r3, .L1979+4
	mvn	r2, #0
.L1973:
	str	r2, [r3, #-1004]
.L1914:
	ldr	r5, .L1979+4
	mov	r3, #300
	ldr	r8, [sp, #20]
	mov	r2, #1
	str	r7, [sp, #16]
	strh	r2, [r5, r3]	@ movhi
.L1931:
	ldr	r3, .L1979
	movw	r2, #3848
	mov	r7, #0
	ldr	lr, [r5, #-968]
	mov	fp, #36
	ldrh	sl, [r3, r2]
	mov	r2, r4
	ldrb	ip, [r3, #928]	@ zero_extendqisi2
	mov	r3, r7
	b	.L1932
.L1935:
	ldrh	r0, [r2, #16]
	movw	r1, #65535
	cmp	r0, r1
	beq	.L1933
	mla	r1, fp, r7, lr
	orr	r0, r8, r0, asl #10
	str	r0, [r1, #4]
	ldrb	r9, [r4, #8]	@ zero_extendqisi2
	cmp	r9, #1
	bne	.L1934
	cmp	ip, #0
	orrne	r0, r0, #-2147483648
	strne	r0, [r1, #4]
.L1934:
	add	r7, r7, #1
	uxth	r7, r7
.L1933:
	add	r3, r3, #1
	add	r2, r2, #2
	uxth	r3, r3
.L1932:
	cmp	r3, sl
	bne	.L1935
	mov	r1, r7
	ldr	r0, [r5, #-968]
	ldr	r2, [sp, #32]
	bl	FlashReadPages
	mov	r3, #36
	mul	r3, r3, r7
	mov	r7, #0
	str	r3, [sp, #40]
	b	.L1936
.L1956:
	ldr	r9, [r5, #-968]
	add	r9, r9, r7
	ldr	sl, [r9, #4]
	ubfx	r0, sl, #10, #16
	str	sl, [sp, #60]
	bl	P2V_plane
	ldr	r3, [sp, #20]
	cmp	r8, r3
	bcc	.L1937
	ldr	fp, [sp, #36]
	mov	r1, r3
	cmp	r0, fp
	movcs	r3, #0
	movcc	r3, #1
	cmp	r8, r1
	movne	r3, #0
	cmp	r3, #0
	bne	.L1937
	ldr	r3, [sp, #24]
	ldr	fp, [sp, #16]
	cmp	r0, r3
	cmpeq	r8, fp
	beq	.L1968
	ldr	r3, [r9, #0]
	cmn	r3, #1
	beq	.L1939
	ldr	sl, [r9, #12]
	movw	r3, #61589
	ldrh	r2, [sl, #0]
	cmp	r2, r3
	ldrneh	r0, [r4, #0]
	bne	.L1974
	ldr	r6, [sl, #4]
	cmn	r6, #1
	beq	.L1941
	ldr	r1, [r5, #-1800]
	mov	r0, r6
	bl	ftl_cmp_data_ver
	cmp	r0, #0
	addne	r3, r6, #1
	strne	r3, [r5, #-1800]
.L1941:
	ldr	r9, [sl, #8]
	add	r1, sp, #56
	ldr	r3, [sl, #12]
	mov	r2, #0
	mov	r0, r9
	str	r3, [sp, #52]
	bl	log2phys
	ldr	r1, [r5, #-1004]
	cmn	r1, #1
	beq	.L1942
	mov	r0, r6
	bl	ftl_cmp_data_ver
	cmp	r0, #0
	beq	.L1942
	ldr	r3, [sp, #52]
	cmn	r3, #1
	beq	.L1943
	ldr	r0, [r5, #-968]
	mov	r1, #1
	mov	r2, #0
	add	r0, r0, r7
	ldr	fp, [r0, #12]
	str	r3, [r0, #4]
	str	fp, [sp, #12]
	bl	FlashReadPages
	ldr	r3, [fp, #4]
	str	r3, [sp, #28]
	ldr	r3, [r5, #-968]
	add	fp, r3, r7
	ldr	r3, [r3, r7]
	cmn	r3, #1
	bne	.L1944
	b	.L1945
.L1943:
	ldr	r3, [sp, #60]
	ldr	r2, [sp, #56]
	cmp	r2, r3
	bne	.L1937
	mov	r0, r9
	add	r1, sp, #52
	mov	r2, #1
	bl	log2phys
	b	.L1937
.L1944:
	ldr	r3, [sp, #12]
	ldr	sl, [r3, #8]
	cmp	sl, r9
	bne	.L1945
	ldr	r0, [r5, #-1004]
	ldr	r1, [sp, #28]
	bl	ftl_cmp_data_ver
	cmp	r0, #0
	beq	.L1945
	ldr	r3, [sp, #56]
	ldr	r2, [sp, #60]
	cmp	r3, r2
	beq	.L1949
.L1946:
	ldr	r2, [sp, #52]
	cmp	r3, r2
	beq	.L1945
	cmn	r3, #1
	streq	r3, [fp, #0]
	beq	.L1948
	ldr	lr, [fp, #12]
	mov	r0, fp
	str	r3, [fp, #4]
	mov	r1, #1
	mov	r2, #0
	str	lr, [sp, #12]
	bl	FlashReadPages
.L1948:
	ldr	r3, [r5, #-968]
	ldr	r3, [r3, r7]
	cmn	r3, #1
	beq	.L1949
	ldr	r3, [sp, #12]
	ldr	r0, [r5, #-1004]
	ldr	r9, [r3, #4]
	mov	r1, r9
	bl	ftl_cmp_data_ver
	cmp	r0, #0
	beq	.L1949
	ldr	r0, [sp, #28]
	mov	r1, r9
	bl	ftl_cmp_data_ver
	cmp	r0, #0
	beq	.L1945
.L1949:
	mov	r0, sl
	ldr	r1, [sp, #52]
	bl	FtlReUsePrevPpa
.L1945:
	mvn	r3, #0
	str	r3, [sp, #52]
	b	.L1951
.L1942:
	ldr	r3, [sp, #60]
	ldr	r2, [sp, #56]
	cmp	r2, r3
	beq	.L1951
	mov	r0, r9
	add	r1, sp, #60
	mov	r2, #1
	bl	log2phys
	ldr	fp, [sp, #56]
	cmn	fp, #1
	beq	.L1951
	ldr	r3, [sp, #52]
	cmp	fp, r3
	beq	.L1951
	ubfx	r0, fp, #10, #16
	bl	P2V_block_in_plane
	ldr	r3, .L1979+12
	ldrh	r3, [r5, r3]
	cmp	r3, r0
	beq	.L1952
	ldr	r3, .L1979+16
	ldrh	r3, [r5, r3]
	cmp	r3, r0
	beq	.L1952
	ldr	r3, .L1979+20
	ldrh	r3, [r5, r3]
	cmp	r3, r0
	bne	.L1951
.L1952:
	ldr	lr, .L1979+4
	mov	r1, #1
	mov	r2, #0
	ldr	r0, [lr, #-968]
	str	fp, [r0, #4]
	ldr	sl, [r0, #12]
	bl	FlashReadPages
	ldr	r0, .L1979+4
	ldr	r1, [sl, #4]
	ldr	r3, [r0, #-968]
	ldr	r3, [r3, #0]
	cmn	r3, #1
	beq	.L1951
	mov	r0, r6
	bl	ftl_cmp_data_ver
	cmp	r0, #0
	bne	.L1951
	mov	r0, r9
	add	r1, sp, #56
	mov	r2, #1
	bl	log2phys
.L1951:
	ldr	r0, [sp, #52]
	cmn	r0, #1
	beq	.L1937
	ubfx	r0, r0, #10, #16
	bl	P2V_block_in_plane
	ldr	r2, [r5, #-2016]
	mov	r3, r0, asl #1
	mov	r1, r0
	ldrh	r3, [r2, r3]
	cmp	r3, #0
	beq	.L1953
.L1974:
	bl	decrement_vpc_count
	b	.L1937
.L1953:
	ldr	r0, .L1979+24
	bl	printk
	b	.L1937
.L1939:
	ldrh	r3, [r4, #0]
	movw	r1, #298
	mov	r2, r6
	ldr	r0, .L1979+28
	strh	r3, [r5, r1]	@ movhi
	mov	r1, sl
	bl	printk
	ldr	r3, [r5, #304]
	cmp	r3, #31
	addls	r2, r5, r3, asl #2
	addls	r3, r3, #1
	ldrls	r1, [sp, #60]
	strls	r3, [r5, #304]
	strls	r1, [r2, #308]
	ldrh	r0, [r4, #0]
	bl	decrement_vpc_count
	ldr	r3, [r5, #-1004]
	cmn	r3, #1
	beq	.L1975
.L1955:
	cmp	r3, r6
	bls	.L1937
.L1975:
	str	r6, [r5, #-1004]
.L1937:
	add	r7, r7, #36
.L1936:
	ldr	r3, [sp, #40]
	cmp	r7, r3
	bne	.L1956
	ldrb	r3, [r4, #8]	@ zero_extendqisi2
	add	r8, r8, #1
	cmp	r3, #1
	uxth	r8, r8
	bne	.L1957
	ldr	r3, .L1979
	ldrb	r2, [r3, #928]	@ zero_extendqisi2
	cmp	r2, #0
	beq	.L1957
	movw	r2, #3918
	ldr	fp, [sp, #16]
	ldrh	r3, [r3, r2]
	cmp	fp, r8
	cmpeq	r3, r8
	beq	.L1968
.L1957:
	ldr	r3, .L1979
	movw	r2, #3916
	ldrh	r2, [r3, r2]
	cmp	r8, r2
	bne	.L1931
	mov	r2, #0
	movw	r1, #3848
	strh	r2, [r4, #4]	@ movhi
	ldrh	r1, [r3, r1]
	mov	r3, r2
	strh	r8, [r4, #2]	@ movhi
	movw	r2, #65535
	ldr	r0, [sp, #44]
	b	.L1958
.L1960:
	add	r0, r0, #2
	ldrh	ip, [r0, #14]
	cmp	ip, r2
	strneb	r3, [r4, #6]
	bne	.L1891
.L1959:
	add	r3, r3, #1
	uxth	r3, r3
.L1958:
	cmp	r3, r1
	bne	.L1960
	b	.L1891
.L1968:
.L1938:
	ldr	r7, [sp, #16]
	mov	r0, r4
	ldr	r3, [sp, #24]
	mov	r1, r7
	strh	r7, [r4, #2]	@ movhi
	mov	r2, r3
	strb	r3, [r4, #6]
.L1977:
	bl	ftl_sb_update_avl_pages
.L1891:
	mov	r0, #0
	add	sp, sp, #68
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, sl, fp, pc}
.L1980:
	.align	2
.L1979:
	.word	.LANCHOR0
	.word	.LANCHOR2
	.word	.LC143
	.word	-1996
	.word	-1948
	.word	-1900
	.word	.LC144
	.word	.LC145
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
	ldr	r5, .L1990
	ldr	r3, [r5, #4040]
	cmp	r3, #0
	bne	.L1981
	ldr	r4, .L1990+4
	ldr	r3, .L1990+8
	mov	r2, r4
	ldrh	r3, [r4, r3]
	cmp	r3, #0
	beq	.L1983
	ldrb	r1, [r4, #-1988]	@ zero_extendqisi2
	cmp	r1, #0
	bne	.L1983
	movw	r0, #3916
	ldrb	r1, [r4, #-1989]	@ zero_extendqisi2
	ldrh	r0, [r5, r0]
	mul	r1, r0, r1
	cmp	r3, r1
	beq	.L1983
	ldrb	r8, [r4, #-1986]	@ zero_extendqisi2
	movw	r3, #3848
	ldr	r7, [r5, #3976]
	cmp	r8, #0
	ldrh	r6, [r5, r3]
	bne	.L1981
	sub	r7, r7, #1
	mov	r1, sp
	mov	r2, r8
	mov	r0, r7
	bl	log2phys
	ldr	r3, [sp, #0]
	ldr	r0, [r4, #-956]
	ldr	r4, [r4, #-932]
	cmn	r3, #1
	str	r3, [sp, #8]
	str	r7, [sp, #20]
	str	r0, [sp, #12]
	str	r4, [sp, #16]
	str	r8, [r4, #4]
	beq	.L1984
	add	r0, sp, #4
	mov	r1, #1
	mov	r2, r8
	bl	FlashReadPages
	b	.L1985
.L1984:
	movw	r3, #3926
	mov	r1, #255
	ldrh	r2, [r5, r3]
	bl	ftl_memset
.L1985:
	ldr	r3, .L1990+12
	mov	r6, r6, asl #2
	ldr	r5, .L1990+4
	ldr	sl, .L1990+8
	strh	r3, [r4, #0]	@ movhi
	ldr	r8, .L1990+16
	b	.L1986
.L1989:
	ldrh	r3, [r5, sl]
	cmp	r3, #0
	beq	.L1987
	ldr	r3, [sp, #8]
	sub	r6, r6, #1
	str	r7, [r4, #8]
	ldr	r0, .L1990+20
	str	r3, [r4, #12]
	ldrh	r3, [r5, r8]
	strh	r3, [r4, #2]	@ movhi
	bl	get_new_active_ppa
	ldr	r3, [r5, #-1800]
	mov	r2, #0
	mov	r1, #1
	str	r0, [sp, #8]
	add	r0, sp, #4
	str	r3, [r4, #4]
	add	r3, r3, #1
	cmn	r3, #1
	str	r3, [r5, #-1800]
	moveq	r3, #0
	streq	r3, [r5, #-1800]
	mov	r3, r2
	bl	FlashProgPages
	ldrh	r0, [r5, r8]
	bl	decrement_vpc_count
.L1986:
	cmp	r6, #0
	bne	.L1989
.L1987:
	ldr	r3, .L1990+4
	mov	r2, #1
	strb	r2, [r3, #-1986]
	b	.L1981
.L1983:
	mov	r3, #0
	strb	r3, [r2, #-1986]
.L1981:
	add	sp, sp, #44
	ldmfd	sp!, {r4, r5, r6, r7, r8, sl, pc}
.L1991:
	.align	2
.L1990:
	.word	.LANCHOR0
	.word	.LANCHOR2
	.word	-1992
	.word	-3947
	.word	-1996
	.word	.LANCHOR2-1996
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
	ldr	r7, .L1996
	movw	r6, #3954
	ldr	r5, .L1996+4
	b	.L1993
.L1995:
	ldr	r3, [r5, #-1852]
	mov	r2, #12
	mla	r3, r2, r4, r3
	ldr	r3, [r3, #4]
	cmp	r3, #0
	bge	.L1994
	mov	r0, r4
	bl	flush_l2p_region
.L1994:
	add	r4, r4, #1
	uxth	r4, r4
.L1993:
	ldrh	r3, [r7, r6]
	cmp	r3, r4
	bhi	.L1995
	mov	r0, #0
	ldmfd	sp!, {r3, r4, r5, r6, r7, pc}
.L1997:
	.align	2
.L1996:
	.word	.LANCHOR0
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
	ldr	r3, .L2010
	stmfd	sp!, {r4, r5, r6, lr}
	.save {r4, r5, r6, lr}
	mov	r4, r0
	ldr	r3, [r3, #4040]
	ldrh	r5, [r0, #0]
	cmp	r3, #0
	bne	.L1999
	movw	r3, #65535
	cmp	r5, r3
	beq	.L2000
	ldr	r3, .L2010+4
	mov	r0, r5
	ldr	r2, [r3, #-2016]
	mov	r3, r5, asl #1
	ldrh	r3, [r2, r3]
	cmp	r3, #0
	beq	.L2001
	bl	INSERT_DATA_LIST
	b	.L2000
.L2001:
	bl	INSERT_FREE_LIST
.L2000:
	mov	r3, #0
	strb	r3, [r4, #8]
	ldr	r3, .L2010+8
	cmp	r4, r3
	beq	.L2002
	ldr	r2, .L2010
	movw	r3, #3868
	ldrh	r3, [r2, r3]
	cmp	r3, #1
	beq	.L2002
	ldrb	r2, [r2, #928]	@ zero_extendqisi2
	cmp	r2, #0
	beq	.L2003
.L2002:
	mov	r3, #1
	strb	r3, [r4, #8]
	b	.L2004
.L2003:
	ldr	r2, .L2010+12
	cmp	r4, r2
	bne	.L2004
	cmp	r3, #3
	ldr	r3, .L2010+4
	beq	.L2005
	ldr	r2, [r3, #-1712]
	cmp	r2, #1
	bne	.L2006
.L2005:
	mov	r2, #1
	strb	r2, [r3, #-1988]
.L2006:
	ldr	r2, .L2010
	ldr	r2, [r2, #3840]
	cmp	r2, #0
	beq	.L2004
	ldr	r3, [r3, #-1780]
	cmp	r3, #39
	ldrls	r3, .L2010+4
	movls	r2, #1
	strlsb	r2, [r3, #-1988]
.L2004:
	ldr	r3, .L2010+16
	ldr	r6, .L2010+4
	ldrh	r0, [r6, r3]
	movw	r3, #65535
	cmp	r0, r3
	beq	.L2007
	cmp	r5, r0
	bne	.L2008
	ldr	r2, [r6, #-2016]
	mov	r3, r0, asl #1
	ldrh	r3, [r2, r3]
	cmp	r3, #0
	beq	.L2009
.L2008:
	bl	update_vpc_list
.L2009:
	ldr	r3, .L2010+16
	mvn	r2, #0
	strh	r2, [r6, r3]	@ movhi
.L2007:
	mov	r0, r4
	bl	allocate_data_superblock
	bl	l2p_flush
	mov	r0, #0
	bl	FtlEctTblFlush
	bl	FtlVpcTblFlush
.L1999:
	mov	r0, #0
	ldmfd	sp!, {r4, r5, r6, pc}
.L2011:
	.align	2
.L2010:
	.word	.LANCHOR0
	.word	.LANCHOR2
	.word	.LANCHOR2-1948
	.word	.LANCHOR2-1996
	.word	-1016
	.fnend
	.size	allocate_new_data_superblock, .-allocate_new_data_superblock
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
	ldr	r6, .L2020
	ldr	r1, .L2020+4
	ldr	r0, .L2020+8
	bl	printk
	movw	r3, #3858
	ldrh	r2, [r6, r3]
	mov	r1, #0
	ldr	r5, .L2020+12
	mov	r2, r2, asl #1
	ldr	r0, [r5, #-912]
	bl	ftl_memset
	b	.L2013
.L2015:
	mov	r0, r4
	add	r1, sp, #4
	mov	r2, #0
	bl	log2phys
	ldr	r0, [sp, #4]
	cmn	r0, #1
	beq	.L2014
	ubfx	r0, r0, #10, #16
	bl	P2V_block_in_plane
	ldr	r3, [r5, #-912]
	mov	r0, r0, asl #1
	ldrh	r2, [r3, r0]
	add	r2, r2, #1
	strh	r2, [r3, r0]	@ movhi
.L2014:
	add	r4, r4, #1
.L2013:
	ldr	r3, [r6, #3976]
	cmp	r4, r3
	bcc	.L2015
	mov	r4, #0
	ldr	r7, .L2020
	ldr	r5, .L2020+12
	movw	r8, #65535
	b	.L2016
.L2019:
	ldr	r3, [r5, #-2016]
	mov	r6, r4, asl #1
	ldrh	r2, [r3, r6]
	ldr	r3, [r5, #-912]
	ldrh	r3, [r3, r6]
	cmp	r2, r3
	beq	.L2017
	cmp	r2, r8
	beq	.L2017
	ldr	r0, .L2020+16
	mov	r1, r4
	bl	printk
	ldr	r3, .L2020+20
	ldrh	r3, [r5, r3]
	cmp	r3, r4
	beq	.L2017
	ldr	r3, .L2020+24
	ldrh	r3, [r5, r3]
	cmp	r3, r4
	beq	.L2017
	ldr	r3, .L2020+28
	ldrh	r3, [r5, r3]
	cmp	r3, r4
	beq	.L2017
	ldr	r3, [r5, #-2016]
	ldrh	r2, [r3, r6]
	cmp	r2, #0
	ldr	r2, [r5, #-912]
	ldrh	r2, [r2, r6]
	strh	r2, [r3, r6]	@ movhi
	beq	.L2017
.L2018:
	mov	r0, r4
	bl	update_vpc_list
.L2017:
	add	r4, r4, #1
	uxth	r4, r4
.L2016:
	mov	r3, #3856
	ldrh	r3, [r7, r3]
	cmp	r3, r4
	bhi	.L2019
	bl	l2p_flush
	bl	FtlVpcTblFlush
	ldmfd	sp!, {r2, r3, r4, r5, r6, r7, r8, pc}
.L2021:
	.align	2
.L2020:
	.word	.LANCHOR0
	.word	.LANCHOR3+48
	.word	.LC102
	.word	.LANCHOR2
	.word	.LC146
	.word	-1996
	.word	-1900
	.word	-1948
	.fnend
	.size	FtlVpcCheckAndModify, .-FtlVpcCheckAndModify
	.align	2
	.global	FtlGcFreeTempBlock
	.type	FtlGcFreeTempBlock, %function
FtlGcFreeTempBlock:
	.fnstart
	@ args = 0, pretend = 0, frame = 8
	@ frame_needed = 0, uses_anonymous_args = 0
	ldr	r3, .L2045
	stmfd	sp!, {r0, r1, r2, r4, r5, r6, r7, r8, sl, lr}
	.save {r0, r1, r2, r4, r5, r6, r7, r8, sl, lr}
	movw	r2, #3916
	ldrh	r1, [r3, r2]
	ldr	r2, [r3, #4040]
	cmp	r2, #0
	bne	.L2043
	ldr	ip, .L2045+4
	ldr	r3, .L2045+8
	ldrh	r4, [r3, ip]
	movw	ip, #65535
	cmp	r4, ip
	beq	.L2024
	cmp	r0, #0
	beq	.L2025
	ldr	lr, .L2045+12
	movw	r0, #3096
	ldrh	r5, [lr, r0]
	cmp	r5, ip
	movne	r1, #2
	bne	.L2025
	strh	r2, [lr, r0]	@ movhi
	sub	r2, r2, #2000
	ldrh	r3, [r3, r2]
	cmp	r3, #17
	movhi	r1, #2
.L2025:
	ldr	r0, .L2045+16
	bl	FtlGcScanTempBlk
	cmn	r0, #1
	str	r0, [sp, #4]
	beq	.L2026
	ldr	r3, .L2045
	mov	r4, r4, asl #1
	ldr	r3, [r3, #4048]
	ldrh	r2, [r3, r4]
	cmp	r2, #4
	bls	.L2027
	sub	r2, r2, #5
	mov	r0, #1
	strh	r2, [r3, r4]	@ movhi
	bl	FtlEctTblFlush
.L2027:
	ldr	r3, .L2045+8
	ldr	r2, [r3, #-1008]
	cmp	r2, #0
	bne	.L2028
	ldr	r2, [r3, #-1612]
	ldr	r0, [sp, #4]
	add	r2, r2, #1
	str	r2, [r3, #-1612]
	ubfx	r0, r0, #10, #16
	bl	FtlBbmMapBadBlock
	bl	FtlBbmTblFlush
.L2028:
	ldr	r3, .L2045+8
	mov	r2, #0
	str	r2, [r3, #-1008]
	b	.L2040
.L2026:
	ldr	r2, .L2045+12
	movw	r3, #3096
	ldrh	r2, [r2, r3]
	movw	r3, #65535
	cmp	r2, r3
	bne	.L2040
.L2024:
	ldr	r6, .L2045+8
	movw	r3, #65535
	ldr	r5, .L2045+4
	mov	r4, #0
	str	r4, [r6, #-1008]
	ldrh	r2, [r6, r5]
	cmp	r2, r3
	beq	.L2023
	bl	FtlCacheWriteBack
	ldr	ip, .L2045
	movw	r0, #3916
	ldrb	r1, [r6, #-1893]	@ zero_extendqisi2
	ldrh	r3, [r6, r5]
	ldrh	r0, [ip, r0]
	ldr	r2, [r6, #-2016]
	ldr	sl, .L2045+20
	mov	r3, r3, asl #1
	mul	r1, r0, r1
	strh	r1, [r2, r3]	@ movhi
	ldr	r3, [r6, #-1816]
	ldrh	r2, [r6, sl]
	add	r3, r2, r3
	str	r3, [r6, #-1816]
	b	.L2029
.L2032:
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
	bne	.L2030
	ubfx	r0, r0, #10, #16
	bl	P2V_block_in_plane
	add	r1, r5, #4
	mov	r2, #1
	mov	r7, r0
	ldr	r0, [r5, #8]
	bl	log2phys
	mov	r0, r7
	b	.L2044
.L2030:
	ldr	r3, [r5, #4]
	cmp	r0, r3
	beq	.L2031
	ldr	r3, .L2045+4
	ldrh	r0, [r6, r3]
.L2044:
	bl	decrement_vpc_count
.L2031:
	add	r4, r4, #1
	uxth	r4, r4
.L2029:
	ldrh	r3, [r6, sl]
	ldr	r5, .L2045+8
	cmp	r3, r4
	bhi	.L2032
	movw	r0, #65535
	ldr	r4, .L2045+4
	bl	decrement_vpc_count
	ldr	r3, .L2045
	ldrb	r3, [r3, #928]	@ zero_extendqisi2
	cmp	r3, #0
	beq	.L2033
	ldr	r0, .L2045+24
	ldrh	r1, [r5, r4]
	bl	printk
.L2033:
	ldrh	r0, [r5, r4]
	ldr	r2, [r5, #-2016]
	mov	r3, r0, asl #1
	ldrh	r3, [r2, r3]
	cmp	r3, #0
	beq	.L2034
	bl	INSERT_DATA_LIST
	b	.L2035
.L2034:
	bl	INSERT_FREE_LIST
.L2035:
	ldr	r5, .L2045+8
	mvn	r6, #0
	ldr	r3, .L2045+4
	mov	r4, #0
	strh	r6, [r5, r3]	@ movhi
	add	r3, r3, #756
	strh	r4, [r5, r3]	@ movhi
	sub	r3, r3, #12
	strh	r4, [r5, r3]	@ movhi
	bl	l2p_flush
	bl	FtlVpcTblFlush
	ldr	r3, .L2045+28
	mov	r2, r5
	strh	r6, [r5, r3]	@ movhi
	ldr	r3, .L2045
	ldr	r3, [r3, #3840]
	cmp	r3, r4
	beq	.L2036
	ldr	r3, [r5, #-1780]
	cmp	r3, #39
	bhi	.L2036
	ldr	r3, .L2045+32
	ldr	r2, .L2045+36
	ldrh	r3, [r5, r3]
	ldrh	r2, [r5, r2]
	cmp	r2, r3
	ldrcc	r2, .L2045+40
	movcc	r3, r3, asl #1
	strcch	r3, [r5, r2]	@ movhi
	b	.L2023
.L2036:
	ldr	r3, .L2045+32
	ldr	r0, .L2045+36
	ldr	r1, .L2045+8
	ldrh	r3, [r2, r3]
	ldrh	r2, [r2, r0]
	add	r0, r3, r3, asl #1
	cmp	r2, r0, lsr #2
	ble	.L2043
	ldr	r2, .L2045
	ldrb	r4, [r2, #928]	@ zero_extendqisi2
	ldr	r2, .L2045+40
	cmp	r4, #0
	subne	r3, r3, #2
	moveq	r3, #20
	strneh	r3, [r1, r2]	@ movhi
	streqh	r3, [r1, r2]	@ movhi
	beq	.L2023
	b	.L2043
.L2040:
	mov	r4, #1
	b	.L2023
.L2043:
	mov	r4, #0
.L2023:
	mov	r0, r4
	ldmfd	sp!, {r1, r2, r3, r4, r5, r6, r7, r8, sl, pc}
.L2046:
	.align	2
.L2045:
	.word	.LANCHOR0
	.word	-1900
	.word	.LANCHOR2
	.word	.LANCHOR1
	.word	.LANCHOR2-1900
	.word	-1144
	.word	.LC147
	.word	-1760
	.word	-1776
	.word	-2000
	.word	-1196
	.fnend
	.size	FtlGcFreeTempBlock, .-FtlGcFreeTempBlock
	.align	2
	.type	Ftl_gc_temp_data_write_back.part.21, %function
Ftl_gc_temp_data_write_back.part.21:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	stmfd	sp!, {r3, r4, r5, r6, r7, lr}
	.save {r3, r4, r5, r6, r7, lr}
	mov	r2, #0
	ldr	r4, .L2053
	mov	r3, r2
	mov	r5, #0
	mov	r6, #36
	ldr	r0, [r4, #-964]
	ldr	r1, [r4, #-1180]
	bl	FlashProgPages
	b	.L2048
.L2051:
	mul	r3, r6, r5
	ldr	r1, [r4, #-964]
	add	r2, r1, r3
	ldr	r1, [r1, r3]
	cmn	r1, #1
	bne	.L2049
	ldr	r2, .L2053+4
	mov	lr, #0
	ldr	ip, [r7, #-2016]
	ldrh	r0, [r7, r2]
	mov	r0, r0, asl #1
	strh	lr, [ip, r0]	@ movhi
	strh	r1, [r7, r2]	@ movhi
	ldr	r2, [r7, #-1612]
	add	r2, r2, #1
	str	r2, [r7, #-1612]
	ldr	r2, [r7, #-964]
	add	r3, r2, r3
	ldr	r0, [r3, #4]
	ubfx	r0, r0, #10, #16
	bl	FtlBbmMapBadBlock
	bl	FtlBbmTblFlush
	bl	FtlGcPageVarInit
	mov	r0, #1
	ldmfd	sp!, {r3, r4, r5, r6, r7, pc}
.L2049:
	ldr	r3, [r2, #12]
	add	r5, r5, #1
	ldr	r1, [r2, #4]
	uxth	r5, r5
	ldr	r0, [r3, #12]
	ldr	r2, [r3, #8]
	bl	FtlGcUpdatePage
.L2048:
	ldr	r1, [r4, #-1180]
	ldr	r7, .L2053
	cmp	r5, r1
	bcc	.L2051
	ldr	r0, [r7, #-964]
	bl	FtlGcBufFree
	ldr	r3, .L2053+8
	mov	r0, #0
	str	r0, [r7, #-1180]
	ldrh	r3, [r7, r3]
	cmp	r3, r0
	ldmnefd	sp!, {r3, r4, r5, r6, r7, pc}
	mov	r0, #1
	bl	FtlGcFreeTempBlock
	mov	r0, #1
	ldmfd	sp!, {r3, r4, r5, r6, r7, pc}
.L2054:
	.align	2
.L2053:
	.word	.LANCHOR2
	.word	-1900
	.word	-1896
	.fnend
	.size	Ftl_gc_temp_data_write_back.part.21, .-Ftl_gc_temp_data_write_back.part.21
	.align	2
	.global	Ftl_gc_temp_data_write_back
	.type	Ftl_gc_temp_data_write_back, %function
Ftl_gc_temp_data_write_back:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	@ link register save eliminated.
	ldr	r3, .L2060
	ldr	r2, [r3, #4040]
	cmp	r2, #0
	bne	.L2056
	ldrb	r3, [r3, #928]	@ zero_extendqisi2
	cmp	r3, #0
	beq	.L2057
	ldr	r3, .L2060+4
	ldr	r2, [r3, #-1180]
	tst	r2, #1
	beq	.L2057
	ldr	r2, .L2060+8
	ldrh	r3, [r3, r2]
	cmp	r3, #0
	bne	.L2056
.L2057:
	b	Ftl_gc_temp_data_write_back.part.21
.L2056:
	mov	r0, #0
	bx	lr
.L2061:
	.align	2
.L2060:
	.word	.LANCHOR0
	.word	.LANCHOR2
	.word	-1896
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
	movw	r5, #3916
	ldr	r6, .L2064
	ldr	r0, .L2064+4
	ldr	r4, .L2064+8
	ldrh	r1, [r6, r5]
	bl	FtlGcScanTempBlk
	ldr	r3, .L2064+12
	ldrh	r2, [r4, r3]
	ldrh	r3, [r6, r5]
	cmp	r2, r3
	ldmccfd	sp!, {r4, r5, r6, pc}
	ldr	r0, .L2064+16
	bl	FtlMapBlkWriteDumpData
	mov	r0, #0
	bl	FtlGcFreeTempBlock
	mov	r3, #0
	str	r3, [r4, #-1008]
	ldmfd	sp!, {r4, r5, r6, pc}
.L2065:
	.align	2
.L2064:
	.word	.LANCHOR0
	.word	.LANCHOR2-1900
	.word	.LANCHOR2
	.word	-1898
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
	ldr	r3, .L2067
	ldr	r5, .L2067+4
	str	r4, [r3, #304]
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
.L2068:
	.align	2
.L2067:
	.word	.LANCHOR2
	.word	.LANCHOR2-1996
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
	mov	r3, #300
	ldr	r6, .L2082
	mov	r2, #0
	ldr	r4, .L2082+4
	mvn	r5, #0
	ldr	r0, [r6, #3852]
	strh	r2, [r4, r3]	@ movhi
	movw	r3, #298
	strh	r5, [r4, r3]	@ movhi
	uxth	r0, r0
	bl	FtlFreeSysBlkQueueInit
	bl	FtlScanSysBlk
	ldr	r3, .L2082+8
	ldrh	r2, [r4, r3]
	movw	r3, #65535
	cmp	r2, r3
	beq	.L2070
	bl	FtlLoadSysInfo
	subs	r7, r0, #0
	bne	.L2070
	bl	FtlLoadMapInfo
	bl	FtlLoadVonderInfo
	bl	Ftl_load_ext_data
	bl	FtlLoadEctTbl
	bl	FtlFreeSysBLkSort
	bl	SupperBlkListInit
	bl	FtlPowerLostRecovery
	mov	r0, #1
	bl	FtlUpdateVaildLpn
	movw	r3, #3954
	ldrh	r1, [r6, r3]
	mov	r2, r7
	ldr	r3, [r4, #-1852]
	b	.L2071
.L2073:
	add	r0, r3, r7
	add	r7, r7, #12
	ldr	r0, [r0, #4]
	cmp	r0, #0
	blt	.L2072
	add	r2, r2, #1
.L2071:
	cmp	r2, r1
	blt	.L2073
.L2072:
	ldr	r3, .L2082+4
	cmp	r2, r1
	ldr	r0, .L2082+12
	ldrh	ip, [r3, r0]
	add	ip, ip, #1
	strh	ip, [r3, r0]	@ movhi
	blt	.L2074
	mov	r2, #300
	ldrh	r2, [r3, r2]
	cmp	r2, #0
	beq	.L2075
.L2074:
	ldr	r2, .L2082+16
	ldr	r0, [r3, #-2016]
	ldr	r1, .L2082+20
	ldrh	r2, [r3, r2]
	ldrh	ip, [r3, r1]
	mov	r2, r2, asl #1
	ldrh	lr, [r0, r2]
	rsb	ip, ip, lr
	strh	ip, [r0, r2]	@ movhi
	ldr	ip, .L2082
	movw	r0, #3916
	ldr	r2, .L2082+24
	ldr	r4, [r3, #-2016]
	ldrh	lr, [ip, r0]
	strh	lr, [r3, r2]	@ movhi
	mov	r2, #0
	strh	r2, [r3, r1]	@ movhi
	add	r1, r1, #44
	strb	r2, [r3, #-1990]
	ldrh	lr, [r3, r1]
	add	r1, r1, #4
	ldrh	r5, [r3, r1]
	mov	lr, lr, asl #1
	ldrh	r6, [r4, lr]
	rsb	r5, r5, r6
	strh	r5, [r4, lr]	@ movhi
	strh	r2, [r3, r1]	@ movhi
	strb	r2, [r3, #-1942]
	ldr	r2, .L2082+28
	ldrh	ip, [ip, r0]
	ldr	r0, .L2082+32
	ldrh	r1, [r3, r2]
	strh	ip, [r3, r0]	@ movhi
	add	r1, r1, #1
	strh	r1, [r3, r2]	@ movhi
	bl	l2p_flush
	bl	FtlVpcTblFlush
	bl	FtlVpcTblFlush
.L2075:
	ldr	r3, .L2082+4
	ldr	r2, .L2082+12
	mov	r5, r3
	ldrh	r2, [r3, r2]
	tst	r2, #31
	bne	.L2076
	bl	FtlVpcCheckAndModify
.L2076:
	ldr	r2, .L2082+16
	movw	r3, #65535
	ldr	r4, .L2082+4
	ldrh	r0, [r5, r2]
	cmp	r0, r3
	beq	.L2080
	ldr	r3, .L2082+20
	ldrh	r3, [r4, r3]
	cmp	r3, #0
	bne	.L2080
	add	r2, r2, #52
	ldrh	r5, [r4, r2]
	cmp	r5, #0
	movne	r5, r3
	bne	.L2070
	bl	FtlGcRefreshOpenBlock
	ldr	r3, .L2082+36
	ldrh	r0, [r4, r3]
	bl	FtlGcRefreshOpenBlock
	bl	FtlVpcTblFlush
	ldr	r0, .L2082+40
	bl	allocate_new_data_superblock
	ldr	r0, .L2082+44
	bl	allocate_new_data_superblock
	b	.L2070
.L2080:
	mov	r5, #0
.L2070:
	mov	r0, r5
	ldmfd	sp!, {r3, r4, r5, r6, r7, pc}
.L2083:
	.align	2
.L2082:
	.word	.LANCHOR0
	.word	.LANCHOR2
	.word	-1772
	.word	-2044
	.word	-1996
	.word	-1992
	.word	-1994
	.word	-2042
	.word	-1946
	.word	-1948
	.word	.LANCHOR2-1996
	.word	.LANCHOR2-1948
	.fnend
	.size	FtlSysBlkInit, .-FtlSysBlkInit
	.align	2
	.type	FtlLowFormat.part.25, %function
FtlLowFormat.part.25:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	stmfd	sp!, {r3, r4, r5, r6, r7, r8, r9, sl, fp, lr}
	.save {r3, r4, r5, r6, r7, r8, r9, sl, fp, lr}
	mov	r6, #3952
	ldr	r5, .L2105
	mov	r1, #0
	ldr	r4, .L2105+4
	ldrh	r2, [r5, r6]
	ldr	r0, [r4, #-896]
	mov	r2, r2, asl #2
	bl	ftl_memset
	ldrh	r2, [r5, r6]
	mov	r1, #0
	ldr	r0, [r4, #-1028]
	mov	r2, r2, asl #2
	bl	ftl_memset
	ldr	r0, [r5, #3852]
	mov	r3, #0
	str	r3, [r4, #-1804]
	str	r3, [r4, #-1800]
	uxth	r0, r0
	bl	FtlFreeSysBlkQueueInit
	bl	FtlLoadBbt
	cmp	r0, #0
	beq	.L2085
	bl	FtlMakeBbt
.L2085:
	mov	r3, #0
	ldr	ip, .L2105
	movw	r0, #3922
	ldr	r2, .L2105+4
	ldr	r1, .L2105+8
	b	.L2086
.L2087:
	ldr	lr, [r2, #-940]
	mvn	r4, r3
	orr	r4, r3, r4, asl #16
	str	r4, [lr, r3, asl #2]
	ldr	lr, [r2, #-936]
	str	r1, [lr, r3, asl #2]
	add	r3, r3, #1
	uxth	r3, r3
.L2086:
	ldrh	lr, [ip, r0]
	ldr	r8, .L2105
	cmp	r3, lr, asl #7
	blt	.L2087
	mov	r3, #3856
	mov	r5, #0
	ldrh	r6, [r8, r3]
	movw	r7, #3858
	b	.L2088
.L2089:
	mov	r0, r6
	mov	r1, #1
	bl	FtlLowFormatEraseBlock
	add	r6, r6, #1
	uxth	r6, r6
	add	r5, r5, r0
	uxth	r5, r5
.L2088:
	ldrh	r3, [r8, r7]
	ldr	r4, .L2105
	cmp	r3, r6
	bhi	.L2089
	movw	r3, #3848
	ldrh	r1, [r4, r3]
	sub	r3, r5, #3
	cmp	r3, r1, asl #1
	blt	.L2090
	mov	r0, r5
	movw	r6, #3858
	bl	__aeabi_uidiv
	ldr	r3, [r4, #3948]
	add	r0, r0, r3
	uxth	r0, r0
	bl	FtlSysBlkNumInit
	ldr	r0, [r4, #3852]
	uxth	r0, r0
	bl	FtlFreeSysBlkQueueInit
	mov	r3, #3856
	ldrh	r5, [r4, r3]
	b	.L2091
.L2092:
	mov	r0, r5
	mov	r1, #1
	bl	FtlLowFormatEraseBlock
	add	r5, r5, #1
	uxth	r5, r5
.L2091:
	ldrh	r3, [r4, r6]
	cmp	r3, r5
	bhi	.L2092
.L2090:
	mov	r4, #0
	ldr	r8, .L2105
	mov	r6, r4
	mov	r7, #3856
	b	.L2093
.L2094:
	mov	r0, r6
	mov	r1, #0
	bl	FtlLowFormatEraseBlock
	add	r6, r6, #1
	uxth	r6, r6
	add	r4, r4, r0
	uxth	r4, r4
.L2093:
	ldrh	r3, [r8, r7]
	ldr	r5, .L2105
	cmp	r3, r6
	bhi	.L2094
	movw	r3, #3858
	ldr	r8, .L2105+4
	ldrh	r3, [r5, r3]
	ldr	fp, [r5, #3860]
	ldr	r9, .L2105+12
	str	r3, [r8, #-980]
	movw	r3, #3848
	ldrh	r6, [r5, r3]
	mov	r0, fp
	mov	r1, r6
	bl	__aeabi_uidiv
	ubfx	sl, r0, #5, #16
	mov	r7, r0
	add	r3, sl, #36
	strh	r3, [r8, r9]	@ movhi
	mov	r3, #24
	str	r0, [r5, #3976]
	mul	r3, r3, r6
	cmp	r4, r3
	ble	.L2095
	rsb	r0, r4, fp
	mov	r1, r6
	bl	__aeabi_uidiv
	str	r0, [r5, #3976]
	mov	r0, r0, lsr #5
	add	r0, r0, #24
	strh	r0, [r8, r9]	@ movhi
.L2095:
	ldr	r3, .L2105
	ldr	r3, [r3, #3840]
	cmp	r3, #1
	bne	.L2096
	mov	r0, r4
	mov	r1, r6
	bl	__aeabi_uidiv
	ldr	r8, .L2105+4
	ldr	r5, .L2105+12
	ldrh	r9, [r8, r5]
	uxtah	r0, r9, r0
	add	r9, r9, r0, lsr #2
	strh	r9, [r8, r5]	@ movhi
.L2096:
	ldr	r3, .L2105
	ldrb	r3, [r3, #928]	@ zero_extendqisi2
	cmp	r3, #0
	beq	.L2097
	mov	r0, r4
	mov	r1, r6
	bl	__aeabi_uidiv
	ldr	r8, .L2105+4
	ldr	r5, .L2105+12
	ldrh	r9, [r8, r5]
	uxtah	r0, r9, r0
	add	r9, r9, r0, lsr #2
	strh	r9, [r8, r5]	@ movhi
.L2097:
	ldr	r0, .L2105
	movw	r3, #3908
	ldrh	r3, [r0, r3]
	cmp	r3, #0
	beq	.L2098
	ldr	r1, .L2105+4
	ldr	r2, .L2105+12
	ldrh	ip, [r1, r2]
	add	ip, ip, r3, lsr #1
	strh	ip, [r1, r2]	@ movhi
	mul	ip, r6, r3
	cmp	ip, r4
	strgt	r7, [r0, #3976]
	addgt	r3, r3, #32
	addgt	sl, sl, r3
	strgth	sl, [r1, r2]	@ movhi
.L2098:
	ldr	r4, .L2105+4
	ldr	r5, .L2105
	ldr	r3, .L2105+12
	ldr	r7, .L2105+16
	ldr	r2, [r5, #3976]
	ldrh	r3, [r4, r3]
	rsb	r3, r3, r2
	mul	r6, r6, r3
	movw	r3, #3916
	ldrh	r3, [r5, r3]
	str	r6, [r4, #220]
	mul	r6, r3, r6
	movw	r3, #3922
	ldrh	r3, [r5, r3]
	str	r6, [r5, #3976]
	mul	r6, r3, r6
	str	r6, [r5, #3956]
	mvn	r6, #0
	bl	FtlBbmTblFlush
	movw	r3, #3858
	ldrh	r2, [r5, r3]
	mov	r1, #0
	ldr	r0, [r4, #-2016]
	mov	r2, r2, asl #1
	bl	ftl_memset
	ldr	r1, .L2105+20
	mov	r3, #0
	ldr	r2, .L2105+24
	str	r3, [r4, #-1844]
	strh	r3, [r4, r1]	@ movhi
	sub	r1, r1, #236
	strb	r3, [r4, #-1754]
	strh	r3, [r4, r1]	@ movhi
	mov	r1, #255
	strb	r3, [r4, #-1752]
	strb	r3, [r4, #-1990]
	strh	r3, [r4, r7]	@ movhi
	mov	r3, #1
	strb	r3, [r4, #-1988]
	mov	r3, #3856
	strh	r6, [r4, r2]	@ movhi
	ldrh	r2, [r5, r3]
	ldr	r0, [r4, #-1840]
	mov	r2, r2, lsr #3
	bl	ftl_memset
.L2099:
	ldr	r0, .L2105+28
	bl	make_superblock
	ldrb	r3, [r4, #-1989]	@ zero_extendqisi2
	ldr	r5, .L2105+4
	cmp	r3, #0
	ldr	r3, .L2105+16
	bne	.L2100
	ldrh	r3, [r4, r7]
	ldr	r2, [r4, #-2016]
	mov	r3, r3, asl #1
	strh	r6, [r2, r3]	@ movhi
	ldrh	r3, [r4, r7]
	add	r3, r3, #1
	strh	r3, [r4, r7]	@ movhi
	b	.L2099
.L2100:
	ldr	r2, [r5, #-1804]
	mvn	r7, #0
	ldr	r1, [r5, #-2016]
	ldr	r6, .L2105+32
	str	r2, [r5, #-1984]
	add	r2, r2, #1
	str	r2, [r5, #-1804]
	ldr	r2, .L2105+36
	ldrh	r0, [r5, r2]
	ldrh	r2, [r5, r3]
	mov	r2, r2, asl #1
	strh	r0, [r1, r2]	@ movhi
	mov	r2, #0
	ldr	r0, .L2105+40
	ldrh	r3, [r5, r3]
	strb	r2, [r5, #-1942]
	add	r3, r3, #1
	strh	r2, [r5, r0]	@ movhi
	strh	r3, [r5, r6]	@ movhi
	mov	r3, #1
	strb	r3, [r5, #-1940]
.L2101:
	ldr	r0, .L2105+44
	bl	make_superblock
	ldrb	r3, [r5, #-1941]	@ zero_extendqisi2
	ldr	r4, .L2105+4
	cmp	r3, #0
	bne	.L2102
	ldrh	r3, [r5, r6]
	ldr	r2, [r5, #-2016]
	mov	r3, r3, asl #1
	strh	r7, [r2, r3]	@ movhi
	ldrh	r3, [r5, r6]
	add	r3, r3, #1
	strh	r3, [r5, r6]	@ movhi
	b	.L2101
.L2102:
	ldr	r3, [r4, #-1804]
	mvn	r5, #0
	ldr	r2, [r4, #-2016]
	str	r3, [r4, #-1936]
	add	r3, r3, #1
	str	r3, [r4, #-1804]
	ldr	r3, .L2105+48
	ldrh	r1, [r4, r3]
	sub	r3, r3, #4
	ldrh	r3, [r4, r3]
	mov	r3, r3, asl #1
	strh	r1, [r2, r3]	@ movhi
	ldr	r3, .L2105+52
	strh	r5, [r4, r3]	@ movhi
	bl	FtlFreeSysBlkQueueOut
	ldr	r3, .L2105+56
	mov	r2, #0
	strh	r0, [r4, r3]	@ movhi
	add	r3, r3, #2
	strh	r2, [r4, r3]	@ movhi
	add	r3, r3, #2
	ldr	r2, [r4, #220]
	strh	r5, [r4, r3]	@ movhi
	add	r3, r3, #2
	strh	r2, [r4, r3]	@ movhi
	ldr	r3, [r4, #-1804]
	str	r3, [r4, #-1764]
	add	r3, r3, #1
	str	r3, [r4, #-1804]
	bl	FtlVpcTblFlush
	bl	FtlSysBlkInit
	cmp	r0, #0
	ldmnefd	sp!, {r3, r4, r5, r6, r7, r8, r9, sl, fp, pc}
	ldr	r3, .L2105+60
	mov	r2, #1
	str	r2, [r3, #3092]
	ldmfd	sp!, {r3, r4, r5, r6, r7, r8, r9, sl, fp, pc}
.L2106:
	.align	2
.L2105:
	.word	.LANCHOR0
	.word	.LANCHOR2
	.word	168778952
	.word	-1776
	.word	-1996
	.word	-1758
	.word	-1760
	.word	.LANCHOR2-1996
	.word	-1948
	.word	-1992
	.word	-1946
	.word	.LANCHOR2-1948
	.word	-1944
	.word	-1900
	.word	-1772
	.word	.LANCHOR1
	.fnend
	.size	FtlLowFormat.part.25, .-FtlLowFormat.part.25
	.align	2
	.global	FtlLowFormat
	.type	FtlLowFormat, %function
FtlLowFormat:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	stmfd	sp!, {r3, lr}
	.save {r3, lr}
	ldr	r3, .L2109
	ldr	r3, [r3, #4040]
	cmp	r3, #0
	bne	.L2108
	bl	FtlLowFormat.part.25
.L2108:
	mov	r0, #0
	ldmfd	sp!, {r3, pc}
.L2110:
	.align	2
.L2109:
	.word	.LANCHOR0
	.fnend
	.size	FtlLowFormat, .-FtlLowFormat
	.align	2
	.global	Ftl_get_new_temp_ppa
	.type	Ftl_get_new_temp_ppa, %function
Ftl_get_new_temp_ppa:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	stmfd	sp!, {r3, r4, r5, lr}
	.save {r3, r4, r5, lr}
	ldr	r3, .L2114
	ldr	r5, .L2114+4
	ldrh	r2, [r5, r3]
	movw	r3, #65535
	cmp	r2, r3
	beq	.L2112
	ldr	r3, .L2114+8
	ldrh	r3, [r5, r3]
	cmp	r3, #0
	bne	.L2113
.L2112:
	bl	FtlCacheWriteBack
	mov	r0, #0
	bl	FtlGcFreeTempBlock
	ldr	r0, .L2114+12
	mov	r4, #0
	strb	r4, [r5, #-1892]
	bl	allocate_data_superblock
	ldr	r3, .L2114+16
	strh	r4, [r5, r3]	@ movhi
	add	r3, r3, #12
	strh	r4, [r5, r3]	@ movhi
	bl	l2p_flush
	mov	r0, r4
	bl	FtlEctTblFlush
	bl	FtlVpcTblFlush
.L2113:
	ldr	r0, .L2114+12
	ldmfd	sp!, {r3, r4, r5, lr}
	b	get_new_active_ppa
.L2115:
	.align	2
.L2114:
	.word	-1900
	.word	.LANCHOR2
	.word	-1896
	.word	.LANCHOR2-1900
	.word	-1156
	.fnend
	.size	Ftl_get_new_temp_ppa, .-Ftl_get_new_temp_ppa
	.align	2
	.type	ftl_do_gc.part.22, %function
ftl_do_gc.part.22:
	.fnstart
	@ args = 0, pretend = 0, frame = 24
	@ frame_needed = 0, uses_anonymous_args = 0
	stmfd	sp!, {r4, r5, r6, r7, r8, r9, sl, fp, lr}
	.save {r4, r5, r6, r7, r8, r9, sl, fp, lr}
	mov	sl, r1
	ldr	r3, .L2213
	.pad #36
	sub	sp, sp, #36
	ldr	r4, .L2213+4
	movw	r1, #65535
	str	r0, [sp, #16]
	ldrh	r7, [r3, r4]
	cmp	r7, r1
	beq	.L2117
	ldr	r6, .L2213+8
	ldrh	r2, [r3, r6]
	cmp	r2, r1
	bne	.L2117
	ldr	r0, .L2213+12
	ldrh	r5, [r3, r0]
	cmp	r5, r2
	beq	.L2117
	ldr	r1, .L2213+16
	ldrh	ip, [r3, r1]
	cmp	ip, r2
	strneh	r7, [r3, r6]	@ movhi
	strneh	r5, [r3, r4]	@ movhi
	mvnne	r2, #0
	strneh	ip, [r3, r0]	@ movhi
	strneh	r2, [r3, r1]	@ movhi
.L2117:
	ldr	r1, [sp, #16]
	ldr	r2, [r3, #-1192]
	cmp	r1, #1
	add	r2, r2, #1
	add	r2, r2, r1, asl #7
	str	r2, [r3, #-1192]
	bne	.L2118
	ldr	r3, .L2213+20
	ldr	r1, [r3, #3840]
	cmp	r1, #0
	bne	.L2119
	ldrb	r3, [r3, #928]	@ zero_extendqisi2
	cmp	r3, #0
	beq	.L2118
.L2119:
	ldr	r4, .L2213
	ldr	r3, [r4, #-1780]
	cmp	r3, #39
	bhi	.L2118
	mov	r3, #436
	ldrh	r3, [r4, r3]
	add	r2, r2, r3
	str	r2, [r4, #-1192]
	bl	FtlGcReFreshBadBlk
	ldr	r3, .L2213+24
	movw	r2, #65535
	ldrh	r3, [r4, r3]
	cmp	r3, r2
	bne	.L2118
	ldr	r2, .L2213+8
	ldrh	r2, [r4, r2]
	cmp	r2, r3
	bne	.L2118
	ldr	r3, [r4, #-1192]
	cmp	r3, #1024
	bhi	.L2120
	ldr	r3, .L2213+28
	ldrh	r3, [r4, r3]
	cmp	r3, #63
	bhi	.L2118
.L2120:
	ldr	r3, .L2213
	mov	r2, #436
	ldr	r0, .L2213+28
	mov	r1, #0
	strh	r1, [r3, r2]	@ movhi
	ldrh	ip, [r3, r0]
	ldr	r0, .L2213+32
	ldrh	r0, [r3, r0]
	add	r0, r0, #64
	cmp	ip, r0
	bgt	.L2118
	str	r1, [r3, #-1192]
	ldr	r1, [r3, #-1780]
	cmp	r1, #0
	moveq	r1, #6
	beq	.L2209
	cmp	r1, #5
	bhi	.L2122
	mov	r1, #18
.L2209:
	strh	r1, [r3, r2]	@ movhi
.L2122:
	mov	r0, #32
	movw	r7, #65535
	bl	List_get_gc_head_node
	uxth	r2, r0
	cmp	r2, r7
	beq	.L2123
	ldr	r5, .L2213
	ldr	r6, .L2213+36
	ldrh	r0, [r5, r6]
	cmp	r0, #0
	moveq	r3, #1
	streqh	r3, [r5, r6]	@ movhi
	beq	.L2123
	ldr	r3, .L2213+20
	movw	ip, #3918
	movw	lr, #3848
	ldr	r9, [r5, #-2016]
	mov	r2, r2, asl #1
	ldrh	ip, [r3, ip]
	ldrh	r3, [r3, lr]
	ldrh	r1, [r9, r2]
	mul	r3, r3, ip
	add	r3, r3, #1
	cmp	r1, r3
	bgt	.L2123
	add	fp, r0, #1
	mov	r8, #0
	str	r8, [r5, #-1184]
	uxth	fp, fp
	strh	fp, [r5, r6]	@ movhi
	str	r2, [sp, #8]
	bl	List_get_gc_head_node
	ldr	r2, [sp, #8]
	uxth	r4, r0
	cmp	r4, r7
	beq	.L2123
	ldrh	r2, [r9, r2]
	mov	r7, r4, asl #1
	ldr	r0, .L2213+40
	mov	r1, fp
	ldrh	r3, [r9, r7]
	str	r2, [sp, #0]
	mov	r2, r4
	bl	printk
	ldrh	r3, [r5, r6]
	cmp	r3, #40
	bls	.L2125
	ldr	r3, [r5, #-2016]
	ldrh	r3, [r3, r7]
	cmp	r3, #32
	strhih	r8, [r5, r6]	@ movhi
.L2125:
	ldr	r2, .L2213
	mov	r3, #436
	mov	r1, #6
	strh	r1, [r2, r3]	@ movhi
	b	.L2127
.L2123:
	bl	GetSwlReplaceBlock
	movw	r3, #65535
	cmp	r0, r3
	mov	r4, r0
	bne	.L2127
	ldr	r2, .L2213
	mov	r3, #436
	mov	r1, #0
	strh	r1, [r2, r3]	@ movhi
.L2118:
	ldr	r5, .L2213
	movw	r4, #65535
	ldr	r3, .L2213+24
	ldrh	r2, [r5, r3]
	cmp	r2, r4
	bne	.L2127
	sub	r3, r3, #140
	ldrh	r3, [r5, r3]
	cmp	r3, r2
	movne	r4, r2
	bne	.L2127
	ldr	r2, .L2213+8
	ldrh	r7, [r5, r2]
	cmp	r7, r3
	movne	r4, r3
	bne	.L2127
	ldr	r8, .L2213+28
	ldr	r2, [r5, #-1192]
	ldrh	r3, [r5, r8]
	cmp	r3, #23
	movhi	r3, #1024
	movls	r3, #5120
	cmp	r2, r3
	movls	r4, r7
	bls	.L2127
	mov	r2, #436
	mov	r3, #0
	str	r3, [r5, #-1192]
	mov	r6, r5
	strh	r3, [r5, r2]	@ movhi
	bl	GetSwlReplaceBlock
	cmp	r0, r7
	mov	r4, r0
	bne	.L2129
	ldr	r3, .L2213+32
	ldrh	r1, [r5, r8]
	ldrh	r2, [r5, r3]
	cmp	r1, r2
	movcs	r2, #80
	strcsh	r2, [r5, r3]	@ movhi
	bcs	.L2139
	mov	r0, #64
	bl	List_get_gc_head_node
	uxth	r3, r0
	cmp	r3, r4
	beq	.L2139
	ldr	r2, [r5, #-996]
	cmp	r2, #0
	bne	.L2132
	ldr	r2, .L2213+20
	movw	r1, #3868
	ldrh	r1, [r2, r1]
	cmp	r1, #3
	beq	.L2132
	ldr	r1, [r5, #-1712]
	cmp	r1, #0
	bne	.L2132
	ldr	r1, [r2, #3840]
	cmp	r1, #0
	bne	.L2132
	ldrb	r0, [r2, #928]	@ zero_extendqisi2
	cmp	r0, #0
	beq	.L2133
.L2132:
	ldr	r1, .L2213
	mov	r3, r3, asl #1
	movw	ip, #3848
	movw	lr, #3868
	ldr	r2, [r1, #-2016]
	ldrh	r0, [r2, r3]
	movw	r2, #3918
	ldr	r3, .L2213+20
	ldrh	r2, [r3, r2]
	ldrh	ip, [r3, ip]
	ldrh	r3, [r3, lr]
	mul	ip, ip, r2
	cmp	r3, #3
	moveq	r3, r2, lsr #1
	movne	r3, #0
	add	r3, ip, r3
	cmp	r0, r3
	bgt	.L2135
	mov	r0, #0
	bl	List_get_gc_head_node
	ldr	r2, .L2213+20
	ldr	r3, .L2213
	ldr	r2, [r2, #3976]
	ldr	r1, [r3, #-1844]
	add	r2, r2, r2, asl #1
	cmp	r1, r2, lsr #2
	ldr	r2, .L2213+32
	movhi	r1, #128
	movls	r1, #160
	strh	r1, [r3, r2]	@ movhi
	uxth	r4, r0
	b	.L2137
.L2135:
	ldr	r3, .L2213+32
	mov	r2, #128
	strh	r2, [r1, r3]	@ movhi
	b	.L2139
.L2133:
	ldr	r2, [r5, #-2016]
	mov	r3, r3, asl #1
	ldr	r6, .L2213
	ldr	r5, .L2213+32
	ldrh	r3, [r2, r3]
	cmp	r3, #7
	movhi	r3, #64
	strhih	r3, [r6, r5]	@ movhi
	bhi	.L2139
	bl	List_get_gc_head_node
	mov	r3, #128
	strh	r3, [r6, r5]	@ movhi
	uxth	r4, r0
.L2137:
	movw	r3, #65535
	cmp	r4, r3
	beq	.L2139
.L2129:
	ldr	r1, .L2213
	mov	r0, r4, asl #1
	ldr	r3, .L2213+28
	ldr	ip, .L2213+20
	ldrh	r2, [r1, r3]
	ldr	ip, [ip, #4048]
	ldr	r3, [r1, #-2016]
	ldrh	r3, [r3, r0]
	ldrh	r0, [ip, r0]
	str	r0, [sp, #0]
	ldr	r0, .L2213+44
	ldrh	r1, [r1, r0]
	ldr	r0, .L2213+48
	str	r1, [sp, #4]
	mov	r1, r4
	bl	printk
.L2139:
	bl	FtlGcReFreshBadBlk
.L2127:
	movw	ip, #65535
	rsb	r1, ip, r4
	rsbs	r0, r1, #0
	ldr	r2, [sp, #16]
	adc	r0, r0, r1
	ldr	r3, .L2213
	cmp	r2, #0
	movne	r1, #0
	andeq	r1, r0, #1
	cmp	r1, #0
	beq	.L2140
	ldr	r2, .L2213+28
	ldrh	r1, [r3, r2]
	cmp	r1, #24
	movhi	r6, #1
	bhi	.L2141
	ldr	r0, .L2213+20
	movw	r2, #3916
	cmp	r1, #16
	ldrh	r6, [r0, r2]
	movhi	r6, r6, lsr #5
	bhi	.L2141
	cmp	r1, #12
	movhi	r6, r6, lsr #4
	bhi	.L2141
	cmp	r1, #8
	movhi	r6, r6, lsr #2
.L2141:
	ldr	ip, .L2213+44
	ldr	r2, .L2213
	ldrh	r3, [r3, ip]
	cmp	r3, r1
	mov	r1, ip
	bcs	.L2145
	ldr	r3, .L2213+52
	movw	r0, #65535
	ldrh	r3, [r2, r3]
	cmp	r3, r0
	bne	.L2146
	ldr	r0, .L2213+8
	ldrh	r0, [r2, r0]
	cmp	r0, r3
	bne	.L2146
	mov	r3, #436
	ldrh	r0, [r2, r3]
	cmp	r0, #0
	bne	.L2147
	ldr	r3, .L2213+20
	ldr	r1, [r2, #-1844]
	ldr	r3, [r3, #3976]
	add	r3, r3, r3, asl #1
	cmp	r1, r3, lsr #2
	movcs	r3, #18
	strcsh	r3, [r2, ip]	@ movhi
	bcs	.L2149
.L2147:
	ldr	r3, .L2213
	ldr	r2, .L2213+56
	ldr	r1, .L2213+44
	ldrh	r2, [r3, r2]
	add	r2, r2, r2, asl #1
	mov	r2, r2, asr #2
	strh	r2, [r3, r1]	@ movhi
.L2149:
	ldr	r3, .L2213
	mov	r2, #0
	str	r2, [r3, #-1184]
	b	.L2150
.L2146:
	ldr	r3, .L2213
	ldr	r2, .L2213+56
	ldrh	r2, [r3, r2]
	add	r2, r2, r2, asl #1
	mov	r2, r2, asr #2
	strh	r2, [r3, r1]	@ movhi
.L2145:
	ldr	r3, .L2213+20
	cmp	sl, #2
	movw	r4, #65535
	movhi	sl, #0
	movls	sl, #1
	ldr	r3, [r3, #3840]
	cmp	r3, #0
	moveq	sl, #0
	cmp	sl, #0
	addne	r6, r6, #1
	uxthne	r6, r6
	b	.L2151
.L2140:
	ldr	r2, .L2213+52
	ldrh	r2, [r3, r2]
	cmp	r2, ip
	bne	.L2152
	ldr	ip, .L2213+8
	ldrh	ip, [r3, ip]
	cmp	ip, r2
	movne	r0, #0
	andeq	r0, r0, #1
	cmp	r0, #0
	beq	.L2152
	ldr	r0, .L2213+24
	ldrh	r0, [r3, r0]
	cmp	r0, r2
	movne	r4, r2
	bne	.L2152
	str	r1, [r3, #-1184]
	ldr	r2, .L2213+44
	ldr	r1, .L2213+28
	ldrh	r0, [r3, r1]
	ldrh	r1, [r3, r2]
	cmp	r0, r1
	bls	.L2153
	mov	r1, #436
	ldrh	r1, [r3, r1]
	cmp	r1, #0
	bne	.L2154
	ldr	r1, .L2213+20
	ldr	r0, [r3, #-1844]
	ldr	r1, [r1, #3976]
	add	r1, r1, r1, asl #1
	cmp	r0, r1, lsr #2
	movcs	r1, #18
	strcsh	r1, [r3, r2]	@ movhi
	bcs	.L2156
.L2154:
	ldr	r3, .L2213
	ldr	r2, .L2213+56
	ldr	r1, .L2213+44
	ldrh	r2, [r3, r2]
	add	r2, r2, r2, asl #1
	mov	r2, r2, asr #2
	strh	r2, [r3, r1]	@ movhi
.L2156:
	bl	FtlReadRefresh
	mov	r0, #0
	bl	List_get_gc_head_node
	ldr	r3, .L2213
	ldr	r2, [r3, #-2016]
	uxth	r0, r0
	mov	r0, r0, asl #1
	ldrh	r2, [r2, r0]
	cmp	r2, #4
	bhi	.L2212
.L2153:
	ldr	r4, .L2213
	mov	r6, #436
	ldrh	r0, [r4, r6]
	cmp	r0, #0
	bne	.L2204
	ldr	r3, .L2213+56
	ldrh	r5, [r4, r3]
	add	r3, r3, #580
	add	r2, r5, r5, asl #1
	mov	r2, r2, asr #2
	strh	r2, [r4, r3]	@ movhi
	bl	List_get_gc_head_node
	ldr	r3, [r4, #-2016]
	ldr	r1, .L2213+20
	movw	r2, #3918
	ldrh	r2, [r1, r2]
	uxth	r0, r0
	mov	r0, r0, asl #1
	ldrh	r3, [r3, r0]
	movw	r0, #3848
	ldrh	r1, [r1, r0]
	mul	r2, r1, r2
	add	r2, r2, r2, lsr #31
	cmp	r3, r2, asr #1
	ble	.L2157
	ldr	r2, .L2213+28
	sub	r5, r5, #1
	ldrh	r2, [r4, r2]
	cmp	r2, r5
	blt	.L2157
	bl	FtlReadRefresh
	ldrh	r0, [r4, r6]
	b	.L2150
.L2157:
	cmp	r3, #0
	bne	.L2204
	movw	r0, #65535
	bl	decrement_vpc_count
	ldr	r3, .L2213+28
	ldr	r2, .L2213
	ldrh	r0, [r2, r3]
	add	r0, r0, #1
	b	.L2150
.L2204:
	movw	r4, #65535
.L2152:
	ldr	r3, .L2213+20
	ldr	r6, [r3, #3840]
	cmp	r6, #0
	movne	r6, #2
	moveq	r6, #1
.L2151:
	ldr	r3, .L2213
	movw	r0, #65535
	ldr	r2, .L2213+24
	ldrh	r1, [r3, r2]
	cmp	r1, r0
	bne	.L2158
	cmp	r4, r1
	strneh	r4, [r3, r2]	@ movhi
	bne	.L2160
	ldr	r2, .L2213+8
	ldrh	r1, [r3, r2]
	cmp	r1, r4
	beq	.L2160
	ldr	r0, [r3, #-2016]
	mov	r1, r1, asl #1
	ldrh	r1, [r0, r1]
	cmp	r1, #0
	mvneq	r1, #0
	streqh	r1, [r3, r2]	@ movhi
	ldr	r3, .L2213
	ldr	r1, .L2213+24
	ldrh	r0, [r3, r2]
	strh	r0, [r3, r1]	@ movhi
	mvn	r1, #0
	strh	r1, [r3, r2]	@ movhi
.L2160:
	ldr	r5, .L2213
	mov	r3, #0
	ldr	r7, .L2213+24
	strb	r3, [r5, #-1752]
	movw	r3, #65535
	ldrh	r0, [r5, r7]
	cmp	r0, r3
	beq	.L2158
	bl	IsBlkInGcList
	cmp	r0, #0
	mvnne	r3, #0
	strneh	r3, [r5, r7]	@ movhi
	ldr	r3, .L2213+20
	ldr	r7, .L2213
	ldrb	r3, [r3, #928]	@ zero_extendqisi2
	cmp	r3, #0
	beq	.L2163
	ldr	r3, .L2213+24
	ldrh	r0, [r7, r3]
	bl	ftl_get_blk_mode
	strb	r0, [r7, #-1752]
.L2163:
	ldr	r8, .L2213+24
	movw	r3, #65535
	ldr	r5, .L2213
	ldrh	r2, [r7, r8]
	cmp	r2, r3
	beq	.L2158
	add	r0, r5, r8
	bl	make_superblock
	ldr	r1, .L2213+60
	movw	r2, #438
	mov	r3, #0
	strh	r3, [r5, r2]	@ movhi
	strh	r3, [r5, r1]	@ movhi
	strb	r3, [r5, #-1754]
	ldrh	r3, [r5, r8]
	ldr	r2, [r5, #-2016]
	mov	r3, r3, asl #1
	ldrh	r2, [r2, r3]
	mov	r3, #440
	strh	r2, [r5, r3]	@ movhi
.L2158:
	ldr	r3, .L2213
	ldr	r2, .L2213+24
	ldrh	r1, [r3, r2]
	sub	r2, r2, #236
	ldrh	r2, [r3, r2]
	cmp	r2, r1
	mov	r2, r3
	beq	.L2164
	ldr	r0, .L2213+64
	ldrh	r3, [r3, r0]
	cmp	r3, r1
	strne	r4, [sp, #20]
	bne	.L2208
.L2165:
.L2164:
	ldr	r3, .L2213+24
	mvn	r1, #0
	strh	r1, [r2, r3]	@ movhi
	mov	r3, #436
.L2211:
	ldrh	r0, [r2, r3]
	b	.L2150
.L2208:
	ldr	r4, .L2213
	movw	sl, #65535
	ldr	r3, .L2213+24
	ldrh	r3, [r4, r3]
	cmp	r3, sl
	bne	.L2167
	ldr	r5, .L2213+36
	mov	r3, #0
	str	r3, [r4, #-1184]
.L2207:
	ldrh	r8, [r4, r5]
	mov	r0, r8
	bl	List_get_gc_head_node
	ldr	r3, .L2213+24
	movw	r1, #65535
	uxth	r7, r0
	strh	r7, [r4, r3]	@ movhi
	cmp	r7, r1
	bne	.L2169
	ldr	r2, .L2213
	add	r3, r3, #572
	mov	r1, #0
	mov	r0, #8
	strh	r1, [r2, r3]	@ movhi
	b	.L2150
.L2169:
	mov	r0, r7
	bl	IsBlkInGcList
	cmp	r0, #0
	add	r0, r8, #1
	strneh	r0, [r4, r5]	@ movhi
	bne	.L2207
	ldr	ip, .L2213+20
	movw	r8, #3916
	movw	sl, #3848
	ldr	r2, [r4, #-2016]
	mov	r3, r7, asl #1
	uxth	r0, r0
	ldrh	r8, [ip, r8]
	ldrh	ip, [ip, sl]
	strh	r0, [r4, r5]	@ movhi
	ldrh	r1, [r2, r3]
	mul	r8, ip, r8
	add	ip, r8, r8, lsr #31
	cmp	r1, ip, asr #1
	bgt	.L2172
	cmp	r0, #48
	bls	.L2173
	cmp	r1, #8
	bls	.L2173
	ldr	r1, .L2213+68
	ldrh	r1, [r4, r1]
	cmp	r1, #35
	bhi	.L2173
.L2172:
	mov	r1, #0
	strh	r1, [r4, r5]	@ movhi
.L2173:
	ldrh	r3, [r2, r3]
	movw	r1, #65535
	ldr	sl, [sp, #20]
	ldr	r9, .L2213
	cmp	r3, r8
	cmpge	sl, r1
	movne	r8, #0
	moveq	r8, #1
	bne	.L2174
	ldr	r3, .L2213+24
	mvn	r2, #0
	strh	r2, [r9, r3]	@ movhi
	add	r3, r3, #572
	mov	r2, #0
	strh	r2, [r9, r3]	@ movhi
	mov	r3, #436
	ldrh	r0, [r9, r3]
	b	.L2150
.L2174:
	cmp	r3, #0
	bne	.L2175
	movw	r0, #65535
	bl	decrement_vpc_count
	ldrh	r3, [r4, r5]
	add	r3, r3, #1
	strh	r3, [r4, r5]	@ movhi
	b	.L2207
.L2175:
	ldr	r3, .L2213+20
	strb	r8, [r9, #-1752]
	ldrb	r3, [r3, #928]	@ zero_extendqisi2
	cmp	r3, #0
	beq	.L2176
	mov	r0, r7
	bl	ftl_get_blk_mode
	strb	r0, [r9, #-1752]
.L2176:
	ldr	r4, .L2213
	sub	r0, r4, #1760
	bl	make_superblock
	movw	r1, #438
	mov	r3, #0
	strh	r3, [r4, r1]	@ movhi
	ldr	r1, .L2213+24
	ldr	r0, [r4, #-2016]
	ldrh	r1, [r4, r1]
	mov	r1, r1, asl #1
	ldrh	r0, [r0, r1]
	mov	r1, #440
	strb	r3, [r4, #-1754]
	strh	r0, [r4, r1]	@ movhi
	ldr	r1, .L2213+60
	strh	r3, [r4, r1]	@ movhi
.L2167:
	ldr	sl, [sp, #16]
	cmp	sl, #1
	bne	.L2177
	bl	FtlReadRefresh
.L2177:
	ldr	r3, .L2213
	mov	r2, #1
	movw	r1, #3916
	str	r2, [r3, #-992]
	ldr	r2, .L2213+20
	ldrh	r9, [r2, r1]
	ldrb	r1, [r2, #928]	@ zero_extendqisi2
	cmp	r1, #0
	beq	.L2178
	ldrb	r1, [r3, #-1752]	@ zero_extendqisi2
	cmp	r1, #1
	movweq	r1, #3918
	ldreqh	r9, [r2, r1]
.L2178:
	ldr	r2, .L2213+60
	mov	r8, #0
	ldr	r4, .L2213
	mov	fp, r9
	ldrh	r3, [r3, r2]
	add	r2, r3, r6
	cmp	r2, r9
	rsbgt	r6, r3, r9
	uxthgt	r6, r6
	b	.L2180
.L2182:
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
.L2188:
	cmp	r3, r5
	bne	.L2182
	ldr	r0, [r4, #-1164]
	mov	r1, r7
	ldrb	r2, [r4, #-1752]	@ zero_extendqisi2
	mov	r5, #0
	bl	FlashReadPages
	mov	r9, r5
	mov	sl, r6
	b	.L2183
.L2186:
	ldr	r3, [r4, #-1164]
	add	r2, r3, r5
	ldr	r3, [r3, r5]
	ldr	r6, [r2, #12]
	cmn	r3, #1
	beq	.L2184
	ldrh	r3, [r6, #0]
	movw	r1, #61589
	cmp	r3, r1
	bne	.L2184
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
	bne	.L2184
	movw	r3, #438
	ldr	r0, [r4, #-1180]
	ldrh	r2, [r4, r3]
	ldr	r1, [r1, #16]
	add	r2, r2, #1
	strh	r2, [r4, r3]	@ movhi
	ldr	r2, [r4, #-964]
	mov	r3, #36
	mla	r2, r3, r0, r2
	str	r1, [r2, #16]
	str	r2, [sp, #8]
	str	r3, [sp, #12]
	bl	Ftl_get_new_temp_ppa
	ldr	r2, [sp, #8]
	ldr	r1, [r4, #-1180]
	str	r0, [r2, #4]
	ldr	r3, [sp, #12]
	ldr	r2, [r4, #-964]
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
	ldr	r3, .L2213+52
	ldrh	r3, [r4, r3]
	strh	r3, [r6, #2]	@ movhi
	ldr	r3, [r4, #-1800]
	ldr	r0, [r4, #-1164]
	str	r3, [r6, #4]
	add	r0, r0, r5
	ldr	r3, [r4, #-1180]
	add	r3, r3, #1
	str	r3, [r4, #-1180]
	bl	FtlGcBufAlloc
	ldr	r3, .L2213+20
	ldrb	r3, [r3, #928]	@ zero_extendqisi2
	cmp	r3, #0
	bne	.L2185
	ldrb	r3, [r4, #-1893]	@ zero_extendqisi2
	ldr	r2, [r4, #-1180]
	cmp	r2, r3
	beq	.L2185
	ldr	r3, .L2213+72
	ldrh	r3, [r4, r3]
	cmp	r3, #0
	bne	.L2184
.L2185:
	bl	Ftl_gc_temp_data_write_back
	cmp	r0, #0
	beq	.L2184
	ldr	r3, .L2213
	mov	r2, #0
	str	r2, [r3, #-992]
.L2212:
	mov	r2, #436
	b	.L2211
.L2184:
	add	r9, r9, #1
	add	r5, r5, #36
	uxth	r9, r9
.L2183:
	cmp	r9, r7
	bne	.L2186
	add	r8, r8, #1
	mov	r6, sl
.L2180:
	uxth	r3, r8
	ldr	r7, .L2213
	cmp	r3, r6
	ldr	r3, .L2213+60
	bcs	.L2187
	ldr	r1, .L2213+20
	movw	r2, #3848
	ldrh	ip, [r4, r3]
	mov	r7, #0
	ldr	lr, [r4, #-1164]
	mov	r3, r7
	ldrh	r5, [r1, r2]
	add	ip, ip, r8
	ldr	r2, .L2213+76
	mov	r0, #36
	b	.L2188
.L2187:
	ldrh	r2, [r7, r3]
	mov	r9, fp
	add	r6, r6, r2
	uxth	r6, r6
	strh	r6, [r7, r3]	@ movhi
	cmp	r6, fp
	bcc	.L2189
	ldr	r3, [r7, #-1180]
	cmp	r3, #0
	beq	.L2190
	bl	Ftl_gc_temp_data_write_back
	cmp	r0, #0
	movne	r3, #0
	strne	r3, [r7, #-992]
	movne	r3, #436
	ldrneh	r0, [r7, r3]
	bne	.L2150
.L2190:
	ldr	r4, .L2213
	movw	r3, #438
	ldrh	r6, [r4, r3]
	cmp	r6, #0
	bne	.L2191
	ldr	r5, .L2213+24
	ldr	r2, [r4, #-2016]
	ldrh	r1, [r4, r5]
	mov	r3, r1, asl #1
	ldrh	r3, [r2, r3]
	cmp	r3, #0
	beq	.L2191
	ldr	r2, .L2213+60
	ldr	r0, .L2213+80
	ldrh	r2, [r4, r2]
	str	r3, [sp, #0]
	mov	r3, #440
	ldrh	r3, [r4, r3]
	str	r3, [sp, #4]
	mov	r3, r6
	bl	printk
	ldrh	r3, [r4, r5]
	ldr	r2, [r4, #-2016]
	mov	r3, r3, asl #1
	strh	r6, [r2, r3]	@ movhi
	ldrh	r0, [r4, r5]
	bl	update_vpc_list
	bl	FtlCacheWriteBack
	bl	l2p_flush
	bl	FtlVpcTblFlush
.L2191:
	ldr	r3, .L2213+24
	mvn	r1, #0
	ldr	r2, .L2213
	strh	r1, [r2, r3]	@ movhi
.L2189:
	ldr	r3, .L2213
	ldr	r2, .L2213+28
	ldrh	r2, [r3, r2]
	cmp	r2, #2
	ldrls	r2, .L2213+20
	movwls	r3, #3916
	ldrlsh	r6, [r2, r3]
	bls	.L2208
.L2192:
	mov	r1, #0
	str	r1, [r3, #-992]
	mov	r1, #436
	ldrh	r0, [r3, r1]
	cmp	r0, #0
	addeq	r0, r2, #1
.L2150:
	add	sp, sp, #36
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, sl, fp, pc}
.L2214:
	.align	2
.L2213:
	.word	.LANCHOR2
	.word	-1140
	.word	-1142
	.word	-1138
	.word	-1136
	.word	.LANCHOR0
	.word	-1760
	.word	-2000
	.word	-1194
	.word	-1188
	.word	.LC148
	.word	-1196
	.word	.LC149
	.word	-1900
	.word	-1776
	.word	-1758
	.word	-1948
	.word	-1156
	.word	-1896
	.word	.LANCHOR2-1746
	.word	.LC150
	.fnend
	.size	ftl_do_gc.part.22, .-ftl_do_gc.part.22
	.align	2
	.global	ftl_do_gc
	.type	ftl_do_gc, %function
ftl_do_gc:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	stmfd	sp!, {r3, r4, r5, lr}
	.save {r3, r4, r5, lr}
	mov	r4, r0
	ldr	r3, .L2222
	mov	r5, r1
	ldr	r0, [r3, #4040]
	cmp	r0, #0
	movne	r0, #0
	ldmnefd	sp!, {r3, r4, r5, pc}
	ldr	r3, .L2222+4
	ldr	r2, [r3, #-992]
	cmp	r2, #0
	ldmnefd	sp!, {r3, r4, r5, pc}
	ldr	r1, .L2222+8
	ldrh	r1, [r3, r1]
	cmp	r1, #47
	movls	r0, r2
	ldmlsfd	sp!, {r3, r4, r5, pc}
	ldr	r1, .L2222+12
	movw	r2, #3096
	ldrh	r1, [r1, r2]
	movw	r2, #65535
	cmp	r1, r2
	beq	.L2217
	ldr	r1, .L2222+16
	ldrh	r3, [r3, r1]
	cmp	r3, r2
	beq	.L2217
	mov	r0, #1
	bl	FtlGcFreeTempBlock
	cmp	r0, #0
	bne	.L2221
.L2217:
	mov	r0, r4
	mov	r1, r5
	ldmfd	sp!, {r3, r4, r5, lr}
	b	ftl_do_gc.part.22
.L2221:
	mov	r0, #1
	ldmfd	sp!, {r3, r4, r5, pc}
.L2223:
	.align	2
.L2222:
	.word	.LANCHOR0
	.word	.LANCHOR2
	.word	-2008
	.word	.LANCHOR1
	.word	-1900
	.fnend
	.size	ftl_do_gc, .-ftl_do_gc
	.align	2
	.global	FtlCacheWriteBack
	.type	FtlCacheWriteBack, %function
FtlCacheWriteBack:
	.fnstart
	@ args = 0, pretend = 0, frame = 8
	@ frame_needed = 0, uses_anonymous_args = 0
	ldr	r3, .L2247
	stmfd	sp!, {r0, r1, r4, r5, r6, r7, r8, r9, sl, lr}
	.save {r0, r1, r4, r5, r6, r7, r8, r9, sl, lr}
	ldr	r4, [r3, #444]
	ldr	r3, .L2247+4
	ldr	r2, [r3, #4040]
	cmp	r2, #0
	bne	.L2225
	ldr	r1, [r3, #3968]
	cmp	r1, #0
	beq	.L2225
	ldrb	r8, [r3, #928]	@ zero_extendqisi2
	cmp	r8, #0
	beq	.L2226
	ldrb	r8, [r4, #8]	@ zero_extendqisi2
	sub	r0, r8, #1
	rsbs	r8, r0, #0
	adc	r8, r8, r0
.L2226:
	ldr	r5, .L2247+4
	mov	r2, r8
	ldrb	r3, [r4, #9]	@ zero_extendqisi2
	mov	r6, #0
	mov	r7, r6
	ldr	r9, .L2247
	ldr	r0, [r5, #3972]
	bl	FlashProgPages
	b	.L2227
.L2232:
	ldr	r2, [r5, #3972]
	add	r3, r2, r6
	ldr	r2, [r2, r6]
	cmn	r2, #1
	beq	.L2246
	ldr	r2, [r3, #4]
	cmp	r8, #0
	ldr	r0, [r3, #16]
	add	r1, sp, #4
	str	r2, [sp, #4]
	orrne	r2, r2, #-2147483648
	strne	r2, [sp, #4]
	mov	r2, #1
	bl	log2phys
	ldr	r3, [r5, #3972]
	add	r3, r3, r6
	ldr	r3, [r3, #12]
	ldr	r0, [r3, #12]
	cmn	r0, #1
	beq	.L2230
	ubfx	r0, r0, #10, #16
	bl	P2V_block_in_plane
	ldr	r2, [r9, #-2016]
	mov	r3, r0, asl #1
	mov	sl, r0
	ldrh	r2, [r2, r3]
	cmp	r2, #0
	bne	.L2231
	ldr	r0, .L2247+8
	mov	r1, sl
	bl	printk
.L2231:
	mov	r0, sl
	bl	decrement_vpc_count
.L2230:
	add	r7, r7, #1
	add	r6, r6, #36
.L2227:
	ldr	r3, [r5, #3968]
	cmp	r7, r3
	bcc	.L2232
	b	.L2233
.L2242:
	ldr	r3, [r5, #3972]
	mvn	r2, #0
	ldr	r7, .L2247
	str	r2, [r3, r6]
	b	.L2234
.L2238:
	ldr	r0, [r3, #4]
	ubfx	r0, r0, #10, #16
	bl	P2V_block_in_plane
	ldrh	r3, [r4, #0]
	cmp	r3, r0
	bne	.L2235
	ldr	r2, [r7, #-2016]
	mov	r3, r3, asl #1
	ldrh	r1, [r4, #4]
	ldrh	r0, [r2, r3]
	rsb	r1, r1, r0
	strh	r1, [r2, r3]	@ movhi
	movw	r3, #3916
	ldrh	r3, [r5, r3]
	strh	r3, [r4, #2]	@ movhi
	mov	r3, #0
	strb	r3, [r4, #6]
	strh	r3, [r4, #4]	@ movhi
.L2235:
	ldrh	r3, [r4, #4]
	cmp	r3, #0
	bne	.L2236
	mov	r0, r4
	bl	allocate_new_data_superblock
.L2236:
	ldr	r3, [r7, #-1612]
	add	r3, r3, #1
	str	r3, [r7, #-1612]
	ldr	r3, [r5, #3972]
	add	r3, r3, r6
	ldr	r0, [r3, #4]
	ubfx	r0, r0, #10, #16
	bl	FtlGcMarkBadPhyBlk
	mov	r0, r4
	bl	get_new_active_ppa
	mov	r1, #1
	mov	r2, r8
	mov	r3, r0
	str	r0, [sp, #4]
	ldr	r0, [r5, #3972]
	add	r0, r0, r6
	str	r3, [r0, #4]
	ldrb	r3, [r4, #9]	@ zero_extendqisi2
	bl	FlashProgPages
	ldr	r3, [r5, #3972]
	ldr	r3, [r3, r6]
	cmn	r3, #1
	moveq	r3, #1
	streq	r3, [r5, #4040]
	ldr	r3, [r9, #4040]
	cmp	r3, #0
	bne	.L2225
.L2234:
	ldr	r2, [r5, #3972]
	add	r3, r2, r6
	ldr	r2, [r2, r6]
	cmn	r2, #1
	beq	.L2238
	ldr	r2, [r3, #4]
	cmp	r8, #0
	ldr	r0, [r3, #16]
	add	r1, sp, #4
	str	r2, [sp, #4]
	orrne	r2, r2, #-2147483648
	strne	r2, [sp, #4]
	mov	r2, #1
	bl	log2phys
	ldr	r3, [r5, #3972]
	add	r3, r3, r6
	ldr	r3, [r3, #12]
	ldr	r0, [r3, #12]
	cmn	r0, #1
	beq	.L2240
	ubfx	r0, r0, #10, #16
	bl	P2V_block_in_plane
	ldr	r3, .L2247
	ldr	r2, [r3, #-2016]
	mov	r3, r0, asl #1
	mov	r7, r0
	ldrh	r2, [r2, r3]
	cmp	r2, #0
	bne	.L2241
	ldr	r0, .L2247+8
	mov	r1, r7
	bl	printk
.L2241:
	mov	r0, r7
	bl	decrement_vpc_count
.L2240:
	add	sl, sl, #1
	add	r6, r6, #36
	b	.L2228
.L2246:
	ldr	r5, .L2247+4
	mov	r6, #0
	mov	sl, r6
	mov	r9, r5
.L2228:
	ldr	r3, [r5, #3968]
	cmp	sl, r3
	bcc	.L2242
	movw	r4, #16386
	ldr	r6, .L2247
	ldr	r5, .L2247+12
	b	.L2243
.L2244:
	mov	r0, #1
	mov	r1, r0
	bl	ftl_do_gc
	subs	r4, r4, #1
	beq	.L2233
.L2243:
	ldrh	r3, [r6, r5]
	cmp	r3, #0
	bne	.L2244
.L2233:
	ldr	r3, .L2247+4
	mov	r2, #0
	str	r2, [r3, #3968]
.L2225:
	mov	r0, #0
	ldmfd	sp!, {r2, r3, r4, r5, r6, r7, r8, r9, sl, pc}
.L2248:
	.align	2
.L2247:
	.word	.LANCHOR2
	.word	.LANCHOR0
	.word	.LC151
	.word	-1134
	.fnend
	.size	FtlCacheWriteBack, .-FtlCacheWriteBack
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
	.global	FtlSysFlush
	.type	FtlSysFlush, %function
FtlSysFlush:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	stmfd	sp!, {r3, lr}
	.save {r3, lr}
	ldr	r3, .L2253
	ldr	r3, [r3, #4040]
	cmp	r3, #0
	bne	.L2252
	bl	FtlCacheWriteBack
	bl	l2p_flush
	mov	r0, #1
	bl	FtlEctTblFlush
	bl	FtlVpcTblFlush
.L2252:
	mov	r0, #0
	ldmfd	sp!, {r3, pc}
.L2254:
	.align	2
.L2253:
	.word	.LANCHOR0
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
	ldr	r3, .L2257
	ldr	r3, [r3, #3092]
	cmp	r3, #1
	bne	.L2256
	bl	FtlSysFlush
.L2256:
	mov	r0, #0
	ldmfd	sp!, {r3, pc}
.L2258:
	.align	2
.L2257:
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
	ldr	r0, .L2261
	bl	printk
	ldmfd	sp!, {r3, lr}
	b	ftl_deinit
.L2262:
	.align	2
.L2261:
	.word	.LC152
	.fnend
	.size	rk_ftl_de_init, .-rk_ftl_de_init
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
	.global	ftl_fix_nand_power_lost_error
	.type	ftl_fix_nand_power_lost_error, %function
ftl_fix_nand_power_lost_error:
	.fnstart
	@ args = 0, pretend = 0, frame = 48
	@ frame_needed = 0, uses_anonymous_args = 0
	ldr	r3, .L2273
	stmfd	sp!, {r4, r5, r6, r7, r8, sl, lr}
	.save {r4, r5, r6, r7, r8, sl, lr}
	.pad #52
	sub	sp, sp, #52
	ldrb	r3, [r3, #928]	@ zero_extendqisi2
	cmp	r3, #0
	beq	.L2264
	ldr	r4, .L2273+4
	movw	r3, #298
	ldr	r0, .L2273+8
	movw	r7, #4097
	ldrh	r6, [r4, r3]
	ldr	r3, [r4, #-2016]
	mov	r5, r6, asl #1
	mov	r1, r6
	ldrh	r2, [r3, r5]
	bl	printk
	ldr	r3, .L2273+12
	ldrh	r0, [r4, r3]
	bl	FtlGcRefreshOpenBlock
	ldr	r3, .L2273+16
	ldrh	r0, [r4, r3]
	bl	FtlGcRefreshOpenBlock
	ldr	r0, .L2273+20
	bl	allocate_new_data_superblock
	ldr	r0, .L2273+24
	bl	allocate_new_data_superblock
	b	.L2266
.L2268:
	mov	r0, #1
	mov	r1, r0
	bl	ftl_do_gc
	ldr	r3, [r4, #-2016]
	ldrh	r3, [r3, r5]
	cmp	r3, #0
	beq	.L2267
.L2266:
	subs	r7, r7, #1
	bne	.L2268
.L2267:
	ldr	r4, .L2273+4
	mov	r1, r6
	ldr	r0, .L2273+8
	ldr	r3, [r4, #-2016]
	ldrh	r2, [r3, r5]
	bl	printk
	ldr	r3, [r4, #-2016]
	ldrh	r4, [r3, r5]
	cmp	r4, #0
	bne	.L2269
	add	r7, sp, #48
	mov	r0, sp
	strh	r6, [r7, #-48]!	@ movhi
	add	r7, r7, #14
	bl	make_superblock
	ldr	r3, .L2273
	movw	r2, #3848
	movw	lr, #65535
	mov	ip, #36
	mov	r1, r4
	ldrh	r8, [r3, r2]
	ldr	sl, [r3, #4044]
	mov	r3, r4
	b	.L2270
.L2272:
	ldrh	r0, [r7, #2]!
	cmp	r0, lr
	beq	.L2271
	mla	r2, ip, r4, sl
	add	r4, r4, #1
	mov	r0, r0, asl #10
	uxth	r4, r4
	stmib	r2, {r0, r1}
	str	r1, [r2, #12]
.L2271:
	add	r3, r3, #1
	uxth	r3, r3
.L2270:
	cmp	r3, r8
	bne	.L2272
	ldr	r3, .L2273+4
	mov	r1, r6
	ldr	r0, .L2273+28
	ldr	r3, [r3, #-2016]
	ldrh	r2, [r3, r5]
	ldr	r5, .L2273
	bl	printk
	mov	r1, #0
	mov	r2, r4
	ldr	r0, [r5, #4044]
	bl	FlashEraseBlocks
	ldr	r0, [r5, #4044]
	mov	r1, #1
	mov	r2, r4
	bl	FlashEraseBlocks
.L2269:
	ldr	r2, .L2273+4
	movw	r3, #298
	mvn	r1, #0
	strh	r1, [r2, r3]	@ movhi
.L2264:
	add	sp, sp, #52
	ldmfd	sp!, {r4, r5, r6, r7, r8, sl, pc}
.L2274:
	.align	2
.L2273:
	.word	.LANCHOR0
	.word	.LANCHOR2
	.word	.LC153
	.word	-1996
	.word	-1948
	.word	.LANCHOR2-1996
	.word	.LANCHOR2-1948
	.word	.LC154
	.fnend
	.size	ftl_fix_nand_power_lost_error, .-ftl_fix_nand_power_lost_error
	.align	2
	.global	FtlInit
	.type	FtlInit, %function
FtlInit:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
.L2276:
	stmfd	sp!, {r4, r5, r6, lr}
	.save {r4, r5, r6, lr}
	mvn	r3, #0
	ldr	r4, .L2286
	ldr	r6, .L2286+4
	ldr	r5, .L2286+8
	ldr	r1, .L2286+12
	str	r3, [r6, #3092]
	mov	r3, #0
	ldr	r0, .L2286+16
	str	r3, [r4, #4040]
	str	r3, [r5, #448]
	bl	printk
	ldr	r0, .L2286+20
	bl	FtlConstantsInit
	bl	FtlMemInit
	bl	FtlVariablesInit
	ldr	r0, [r4, #3852]
	uxth	r0, r0
	bl	FtlFreeSysBlkQueueInit
	bl	FtlLoadBbt
	cmp	r0, #0
	ldrne	r0, .L2286+24
	bne	.L2285
	bl	FtlSysBlkInit
	cmp	r0, #0
	beq	.L2279
	ldr	r0, .L2286+28
.L2285:
	ldr	r1, .L2286+32
	bl	printk
	b	.L2278
.L2279:
	mov	r1, #1
	str	r1, [r6, #3092]
	bl	ftl_do_gc
	ldr	r3, .L2286+36
	ldrh	r3, [r5, r3]
	cmp	r3, #15
	bhi	.L2280
	mov	r4, #1024
.L2281:
	mov	r0, #1
	mov	r1, r0
	bl	ftl_do_gc
	subs	r4, r4, #1
	bne	.L2281
	b	.L2278
.L2280:
	ldrb	r3, [r4, #928]	@ zero_extendqisi2
	cmp	r3, #0
	beq	.L2278
	mov	r4, #128
.L2282:
	mov	r0, #1
	mov	r1, r0
	bl	ftl_do_gc
	subs	r4, r4, #1
	bne	.L2282
.L2278:
	mov	r0, #0
	ldmfd	sp!, {r4, r5, r6, pc}
.L2287:
	.align	2
.L2286:
	.word	.LANCHOR0
	.word	.LANCHOR1
	.word	.LANCHOR2
	.word	.LC78
	.word	.LC77
	.word	.LANCHOR0+3048
	.word	.LC155
	.word	.LC156
	.word	.LANCHOR3+69
	.word	-2000
	.fnend
	.size	FtlInit, .-FtlInit
	.align	2
	.global	ftl_discard
	.type	ftl_discard, %function
ftl_discard:
	.fnstart
	@ args = 0, pretend = 0, frame = 8
	@ frame_needed = 0, uses_anonymous_args = 0
	stmfd	sp!, {r0, r1, r2, r4, r5, r6, r7, lr}
	.save {r0, r1, r2, r4, r5, r6, r7, lr}
	add	r2, r1, r0
	ldr	r5, .L2298
	mov	r7, r0
	mov	r4, r1
	ldr	r3, [r5, #3956]
	cmp	r2, r3
	mvnhi	r0, #0
	bhi	.L2289
	cmp	r1, #31
	bls	.L2296
	ldr	r3, [r5, #4040]
	cmp	r3, #0
	bne	.L2296
	bl	FtlCacheWriteBack
	movw	r3, #3922
	ldrh	r6, [r5, r3]
	mov	r0, r7
	mov	r1, r6
	bl	__aeabi_uidiv
	mls	r7, r0, r6, r7
	mov	r5, r0
	uxth	r7, r7
	cmp	r7, #0
	beq	.L2290
	rsb	r6, r7, r6
	add	r5, r0, #1
	cmp	r6, r4
	movcs	r6, r4
	uxth	r6, r6
	rsb	r4, r6, r4
.L2290:
	mvn	r3, #0
	ldr	r7, .L2298
	str	r3, [sp, #4]
	movw	r6, #3922
	b	.L2291
.L2293:
	mov	r0, r5
	mov	r1, sp
	mov	r2, #0
	bl	log2phys
	ldr	r3, [sp, #0]
	cmn	r3, #1
	beq	.L2292
	ldr	r3, .L2298+4
	add	r1, sp, #4
	mov	r0, r5
	ldr	r2, [r3, #452]
	add	r2, r2, #1
	str	r2, [r3, #452]
	ldr	r2, [r3, #-1832]
	add	r2, r2, #1
	str	r2, [r3, #-1832]
	mov	r2, #1
	bl	log2phys
	ldr	r0, [sp, #0]
	ubfx	r0, r0, #10, #16
	bl	P2V_block_in_plane
	bl	decrement_vpc_count
.L2292:
	ldrh	r3, [r7, r6]
	add	r5, r5, #1
	rsb	r4, r3, r4
.L2291:
	ldrh	r3, [r7, r6]
	cmp	r4, r3
	bcs	.L2293
	ldr	r3, .L2298+4
	mov	r4, #0
	ldr	r2, [r3, #452]
	cmp	r2, #32
	bls	.L2297
	str	r4, [r3, #452]
	bl	l2p_flush
	bl	FtlVpcTblFlush
	b	.L2297
.L2296:
	mov	r0, #0
	b	.L2289
.L2297:
	mov	r0, r4
.L2289:
	ldmfd	sp!, {r1, r2, r3, r4, r5, r6, r7, pc}
.L2299:
	.align	2
.L2298:
	.word	.LANCHOR0
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
	.type	ftl_write.part.23, %function
ftl_write.part.23:
	.fnstart
	@ args = 0, pretend = 0, frame = 96
	@ frame_needed = 0, uses_anonymous_args = 0
	stmfd	sp!, {r4, r5, r6, r7, r8, r9, sl, fp, lr}
	.save {r4, r5, r6, r7, r8, r9, sl, fp, lr}
	mov	r3, #2048
	ldr	r4, .L2346
	.pad #100
	sub	sp, sp, #100
	ldr	r8, .L2346+4
	mov	r5, r1
	mov	r7, r0
	mov	r9, r2
	str	r3, [r4, #456]
	movw	r3, #3922
	ldrh	r6, [r8, r3]
	mov	r1, r6
	bl	__aeabi_uidiv
	mov	r1, r6
	str	r0, [sp, #16]
	sub	r0, r7, #1
	add	r0, r0, r5
	bl	__aeabi_uidiv
	ldr	r3, [sp, #16]
	ldr	sl, [r8, #3968]
	cmp	sl, #0
	rsb	fp, r3, r0
	str	r0, [sp, #36]
	add	r3, fp, #1
	str	r3, [sp, #20]
	ldr	lr, [sp, #20]
	ldr	r3, [r4, #-1828]
	add	r3, lr, r3
	str	r3, [r4, #-1828]
	ldr	r3, [r4, #-1812]
	add	r3, r5, r3
	str	r3, [r4, #-1812]
	beq	.L2334
	ldr	r3, [r8, #3972]
	sub	sl, sl, #1
	mov	r2, #36
	ldr	r0, [sp, #16]
	mla	sl, r2, sl, r3
	ldr	r3, [sl, #16]
	cmp	r0, r3
	strne	r5, [sp, #32]
	bne	.L2303
	ldr	r3, [r4, #-1824]
	mov	r0, r7
	mov	r1, r6
	add	r3, r3, #1
	str	r3, [r4, #-1824]
	ldr	r3, [r4, #460]
	add	r3, r3, #1
	str	r3, [r4, #460]
	bl	__aeabi_uidivmod
	ldr	r0, [sl, #8]
	rsb	r8, r1, r6
	add	r0, r0, r1, asl #9
	cmp	r8, r5
	movcs	r8, r5
	mov	r1, r9
	mov	r3, r8, asl #9
	str	r3, [sp, #4]
	mov	r2, r3
	bl	memcpy
	cmp	fp, #0
	ldr	r3, [sp, #4]
	bne	.L2304
	ldr	r2, [r4, #460]
	cmp	r2, #2
	ble	.L2305
.L2304:
	add	r9, r9, r3
	add	r7, r7, r8
	ldr	r3, [sp, #16]
	rsb	r1, r8, r5
	str	fp, [sp, #20]
	add	r3, r3, #1
	str	r1, [sp, #32]
	str	r3, [sp, #16]
.L2303:
	ldr	r3, .L2346
	mov	r2, #0
	str	r2, [r3, #460]
	b	.L2302
.L2334:
	str	r5, [sp, #32]
.L2302:
	ldr	r0, [sp, #16]
	ldr	r1, [sp, #36]
	bl	FtlCacheMetchLpa
	cmp	r0, #0
	beq	.L2306
	bl	FtlCacheWriteBack
.L2306:
	cmp	r5, r6, asl #1
	ldr	r3, .L2346
	ldr	r4, .L2346+4
	mov	fp, #0
	movcc	r6, #0
	movcs	r6, #1
	str	r6, [sp, #24]
	ldr	r6, .L2346+8
	ldr	r5, [sp, #16]
	mov	sl, r4
	str	fp, [sp, #40]
	str	r6, [r3, #444]
	b	.L2343
.L2332:
	ldrh	r1, [r6, #4]
	cmp	r1, #0
	bne	.L2308
	ldr	r2, .L2346+8
	ldr	r3, .L2346+12
	cmp	r6, r2
	bne	.L2309
	ldr	r2, .L2346+16
	ldrh	r8, [r8, r2]
	cmp	r8, #0
	bne	.L2310
	ldr	r0, .L2346+20
	str	r3, [sp, #4]
	bl	allocate_new_data_superblock
	ldr	r3, [sp, #4]
	str	r8, [r3, #3100]
.L2310:
	ldr	r0, .L2346+8
	str	r3, [sp, #4]
	bl	allocate_new_data_superblock
	ldr	r3, [sp, #4]
	ldr	r2, [r3, #3100]
	ldr	r3, .L2346+20
	cmp	r2, #0
	movne	r6, r3
	b	.L2311
.L2309:
	str	r1, [r3, #3100]
	ldr	r3, .L2346+24
	ldrh	r3, [r8, r3]
	cmp	r3, #0
	movne	r6, r2
	bne	.L2311
	mov	r0, r6
	bl	allocate_new_data_superblock
.L2311:
	ldrh	r3, [r6, #4]
	cmp	r3, #0
	bne	.L2312
	mov	r0, r6
	bl	allocate_new_data_superblock
.L2312:
	ldr	r3, .L2346
	str	r6, [r3, #444]
.L2308:
	ldr	r2, .L2346
	mov	r8, r6
	ldrh	r3, [r6, #4]
	ldr	r1, [r2, #-972]
	ldr	r2, [r4, #3968]
	rsb	r2, r2, r1
	cmp	r3, r2
	movcc	r2, r3
	ldr	r3, [sp, #20]
	cmp	r2, r3
	movcc	r3, r2
	str	r3, [sp, #52]
	mov	r3, #0
	str	r3, [sp, #28]
	b	.L2313
.L2329:
	ldrh	r3, [r8, #4]
	cmp	r3, #0
	beq	.L2314
	ldr	r6, [sp, #36]
	rsb	lr, r6, r5
	ldr	r6, [sp, #24]
	rsbs	r3, lr, #0
	adc	r3, r3, lr
	tst	r3, r6
	beq	.L2315
	ldr	r6, [sp, #28]
	cmp	r6, #0
	beq	.L2315
	movw	lr, #3922
	ldr	r6, [sp, #32]
	ldrh	r2, [r4, lr]
	add	r1, r6, r7
	mls	r1, r2, r5, r1
	cmp	r1, r2
	bne	.L2314
.L2315:
	add	r1, sp, #56
	mov	r2, #0
	mov	r0, r5
	str	r3, [sp, #4]
	bl	log2phys
	mov	r0, r8
	bl	get_new_active_ppa
	ldr	lr, [r4, #3968]
	ldr	r1, [r4, #3972]
	movw	r2, #3928
	mov	ip, #36
	ldrh	r2, [r4, r2]
	ldr	r6, .L2346
	mla	r1, ip, lr, r1
	mul	lr, lr, r2
	ldr	r6, [r6, #-924]
	bic	lr, lr, #3
	str	r6, [sp, #48]
	str	lr, [sp, #44]
	add	r6, r6, lr
	str	r5, [r1, #16]
	str	r6, [r1, #12]
	str	r0, [r1, #4]
	movw	r0, #3926
	ldrh	lr, [r4, r0]
	ldr	r0, [r4, #3968]
	mul	lr, r0, lr
	ldr	r0, .L2346
	bic	lr, lr, #3
	str	lr, [sp, #12]
	ldr	lr, [r0, #-944]
	ldr	r0, [sp, #12]
	add	lr, lr, r0
	mov	r0, r6
	str	lr, [r1, #8]
	mov	r1, #0
	str	ip, [sp, #8]
	bl	ftl_memset
	ldr	r1, [sp, #16]
	ldr	r3, [sp, #4]
	rsb	ip, r1, r5
	rsbs	r1, ip, #0
	adc	r1, r1, ip
	str	r1, [sp, #12]
	orrs	r1, r1, r3
	ldr	ip, [sp, #8]
	beq	.L2316
	ldr	r2, [sp, #12]
	cmp	r2, #0
	beq	.L2317
	movw	r3, #3922
	mov	r0, r7
	ldrh	fp, [r4, r3]
	mov	r1, fp
	bl	__aeabi_uidivmod
	ldr	r3, [sp, #32]
	rsb	fp, r1, fp
	str	r1, [sp, #40]
	cmp	fp, r3
	movcs	fp, r3
	b	.L2318
.L2317:
	cmp	r3, #0
	beq	.L2318
	ldr	r3, [sp, #32]
	movw	lr, #3922
	add	fp, r3, r7
	ldrh	r3, [r4, lr]
	mls	fp, r3, r5, fp
	ldr	r3, [sp, #12]
	str	r3, [sp, #40]
	uxth	fp, fp
.L2318:
	movw	lr, #3922
	ldrh	r3, [r4, lr]
	cmp	fp, r3
	bne	.L2319
	ldr	r3, [sp, #12]
	ldr	r0, [sl, #3968]
	cmp	r3, #0
	ldr	r2, [sl, #3972]
	ldr	r3, [sp, #24]
	muleq	r1, r5, fp
	movne	r1, r9
	rsbeq	r1, r7, r1
	addeq	r1, r9, r1, asl #9
	cmp	r3, #0
	mov	r3, #36
	mla	r3, r3, r0, r2
	strne	r1, [r3, #8]
	bne	.L2322
	ldr	r0, [r3, #8]
	movw	r3, #3926
	ldrh	r2, [sl, r3]
	b	.L2344
.L2319:
	ldr	r2, [sp, #56]
	mov	r3, #36
	cmn	r2, #1
	beq	.L2323
	ldr	r1, [r4, #3968]
	add	r0, sp, #60
	str	r2, [sp, #64]
	ldr	r2, [r4, #3972]
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
	ldr	r3, .L2346
	ldreq	r2, [r3, #-1636]
	addeq	r2, r2, #1
	streq	r2, [r3, #-1636]
	beq	.L2325
	ldr	r2, [r6, #8]
	cmp	r2, r5
	beq	.L2325
	ldr	r2, [r3, #-1636]
	ldr	r0, .L2346+28
	add	r2, r2, #1
	str	r2, [r3, #-1636]
	mov	r2, r5
	ldr	r1, [r6, #8]
	bl	printk
	b	.L2325
.L2323:
	ldr	r1, [r4, #3968]
	ldr	r2, [r4, #3972]
	mla	r3, r3, r1, r2
	mov	r1, #0
	ldr	r0, [r3, #8]
	movw	r3, #3926
	ldrh	r2, [r4, r3]
	bl	ftl_memset
.L2325:
	ldr	r3, [sp, #12]
	ldr	r1, [r4, #3968]
	cmp	r3, #0
	ldr	r2, [r4, #3972]
	mov	r3, #36
	movweq	lr, #3922
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
	b	.L2344
.L2316:
	ldr	r3, [sp, #24]
	ldr	r2, [r4, #3968]
	cmp	r3, #0
	ldr	r3, [r4, #3972]
	mla	ip, ip, r2, r3
	beq	.L2327
	movw	lr, #3922
	ldrh	r3, [r4, lr]
	mul	r3, r3, r5
	rsb	r3, r7, r3
	add	r3, r9, r3, asl #9
	str	r3, [ip, #8]
	b	.L2322
.L2327:
	movw	r0, #3922
	movw	r3, #3926
	ldrh	r1, [r4, r0]
	ldrh	r2, [r4, r3]
	ldr	r0, [ip, #8]
	mul	r1, r1, r5
	rsb	r1, r7, r1
	add	r1, r9, r1, asl #9
.L2344:
	bl	memcpy
.L2322:
	ldr	r3, .L2346+32
	ldr	r0, [sp, #48]
	ldr	lr, [sp, #44]
	strh	r3, [r0, lr]	@ movhi
	ldr	r3, .L2346
	str	r5, [r6, #8]
	add	r5, r5, #1
	ldr	r2, [r3, #-1800]
	str	r2, [r6, #4]
	add	r2, r2, #1
	cmn	r2, #1
	str	r2, [r3, #-1800]
	moveq	r2, #0
	streq	r2, [r3, #-1800]
	ldr	r3, [sp, #56]
	str	r3, [r6, #12]
	ldrh	r3, [r8, #0]
	strh	r3, [r6, #2]	@ movhi
	ldr	r3, [r4, #3968]
	add	r3, r3, #1
	str	r3, [r4, #3968]
	ldr	r3, [sp, #28]
	add	r3, r3, #1
	str	r3, [sp, #28]
.L2313:
	ldr	r6, [sp, #28]
	ldr	r3, [sp, #52]
	cmp	r6, r3
	bne	.L2329
.L2314:
	ldr	r3, [sp, #20]
	mov	r6, r8
	ldr	lr, [sp, #28]
	ldr	r2, [r4, #3968]
	rsb	r3, lr, r3
	str	r3, [sp, #20]
	ldr	r3, .L2346
	ldr	r0, [sp, #24]
	ldr	r3, [r3, #-972]
	cmp	r2, r3
	orrcs	r0, r0, #1
	uxtb	r3, r0
	cmp	r3, #0
	bne	.L2330
	ldrh	r2, [r8, #4]
	cmp	r2, #0
	bne	.L2340
.L2330:
	bl	FtlCacheWriteBack
	ldr	lr, [sp, #20]
	mov	r3, #0
	str	r3, [sl, #3968]
	cmp	lr, #1
	ldr	r3, [sp, #24]
	movls	r3, #0
.L2340:
	str	r3, [sp, #24]
.L2343:
	ldr	r3, [sp, #20]
	ldr	r8, .L2346
	cmp	r3, #0
	bne	.L2332
	mov	r0, r3
	ldr	r6, [sp, #36]
	ldr	r3, [sp, #16]
	rsb	r1, r3, r6
	bl	ftl_do_gc
	ldr	r3, .L2346+36
	ldrh	r3, [r8, r3]
	cmp	r3, #31
	bhi	.L2305
	ldr	r2, .L2346+40
	mov	r3, #128
	ldr	r5, .L2346+4
	mov	r4, #16
	strh	r3, [r8, r2]	@ movhi
	sub	r2, r2, #2
	strh	r3, [r8, r2]	@ movhi
.L2333:
	mov	r0, #0
	mov	r1, #1
	bl	ftl_do_gc
	ldr	r3, [r5, #4040]
	cmp	r3, #0
	bne	.L2305
	subs	r4, r4, #1
	bne	.L2333
.L2305:
	mov	r0, #0
	add	sp, sp, #100
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, sl, fp, pc}
.L2347:
	.align	2
.L2346:
	.word	.LANCHOR2
	.word	.LANCHOR0
	.word	.LANCHOR2-1996
	.word	.LANCHOR1
	.word	-1944
	.word	.LANCHOR2-1948
	.word	-1992
	.word	.LC157
	.word	-3947
	.word	-2000
	.word	-1194
	.fnend
	.size	ftl_write.part.23, .-ftl_write.part.23
	.align	2
	.global	ftl_write
	.type	ftl_write, %function
ftl_write:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	mov	ip, r0
	mov	r0, r1
	mov	r1, r2
	mov	r2, r3
	ldr	r3, .L2353
	stmfd	sp!, {r4, lr}
	.save {r4, lr}
	ldr	r4, [r3, #4040]
	cmp	r4, #0
	movne	r0, #0
	ldmnefd	sp!, {r4, pc}
	cmp	ip, #16
	bne	.L2350
	add	r0, r0, #256
	ldmfd	sp!, {r4, lr}
	b	FtlVendorPartWrite
.L2350:
	ldr	r3, [r3, #3956]
	add	ip, r1, r0
	cmp	ip, r3
	bhi	.L2352
	ldmfd	sp!, {r4, lr}
	b	ftl_write.part.23
.L2352:
	mvn	r0, #0
	ldmfd	sp!, {r4, pc}
.L2354:
	.align	2
.L2353:
	.word	.LANCHOR0
	.fnend
	.size	ftl_write, .-ftl_write
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
	.global	ftl_vendor_write
	.type	ftl_vendor_write, %function
ftl_vendor_write:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	mov	ip, r1
	mov	r3, r2
	stmfd	sp!, {r4, lr}
	.save {r4, lr}
	mov	r4, r0
	mov	r1, r4
	mov	r0, #16
	mov	r2, ip
	ldmfd	sp!, {r4, lr}
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
	mov	r6, r0
	mov	r5, r1
	mov	r4, r2
	bl	rknand_device_lock
	mov	r2, r4
	mov	r1, r5
	mov	r0, r6
	bl	ftl_vendor_write
	mov	r4, r0
	bl	rknand_device_unlock
	mov	r0, r4
	ldmfd	sp!, {r4, r5, r6, pc}
	.fnend
	.size	FlashBootVendorWrite, .-FlashBootVendorWrite
	.align	2
	.global	ftl_read
	.type	ftl_read, %function
ftl_read:
	.fnstart
	@ args = 0, pretend = 0, frame = 56
	@ frame_needed = 0, uses_anonymous_args = 0
	stmfd	sp!, {r4, r5, r6, r7, r8, r9, sl, fp, lr}
	.save {r4, r5, r6, r7, r8, r9, sl, fp, lr}
	cmp	r0, #16
	.pad #84
	sub	sp, sp, #84
	mov	r6, r1
	mov	r8, r3
	str	r2, [sp, #48]
	bne	.L2360
	add	r0, r1, #256
	mov	r1, r2
	mov	r2, r3
	bl	FtlVendorPartRead
	str	r0, [sp, #36]
	b	.L2361
.L2360:
	ldr	r3, .L2391
	ldr	r1, [sp, #48]
	ldr	r2, [r3, #3956]
	add	r1, r1, r6
	str	r1, [sp, #40]
	cmp	r1, r2
	mvnhi	r3, #0
	strhi	r3, [sp, #36]
	bhi	.L2361
	movw	r2, #3922
	mov	r0, r6
	ldrh	r4, [r3, r2]
	mov	r1, r4
	bl	__aeabi_uidiv
	ldr	r2, [sp, #40]
	mov	r1, r4
	str	r0, [sp, #28]
	sub	r0, r2, #1
	bl	__aeabi_uidiv
	ldr	r1, [sp, #28]
	rsb	r3, r1, #1
	ldr	r1, [sp, #48]
	add	r3, r3, r0
	str	r3, [sp, #24]
	ldr	r3, .L2391+4
	str	r0, [sp, #32]
	ldr	r0, [sp, #28]
	ldr	r2, [r3, #-1808]
	add	r2, r1, r2
	ldr	r1, [sp, #24]
	str	r2, [r3, #-1808]
	ldr	r2, [r3, #-1836]
	add	r2, r1, r2
	ldr	r1, [sp, #32]
	str	r2, [r3, #-1836]
	bl	FtlCacheMetchLpa
	cmp	r0, #0
	beq	.L2362
	bl	FtlCacheWriteBack
.L2362:
	mov	r9, #0
	ldr	r5, [sp, #28]
	str	r9, [sp, #52]
	mov	r4, r9
	str	r9, [sp, #36]
	mov	r7, r6
	ldr	sl, .L2391
	b	.L2386
.L2380:
	mov	r2, #0
	mov	r0, r5
	add	r1, sp, #76
	bl	log2phys
	ldr	r2, [sp, #76]
	cmn	r2, #1
	bne	.L2387
	b	.L2390
.L2367:
	mla	r0, r0, r5, r6
	cmp	r0, r7
	bcc	.L2366
	ldr	r2, [sp, #40]
	cmp	r0, r2
	bcs	.L2366
	rsb	r0, r7, r0
	mov	r1, #0
	mov	r2, #512
	add	r0, r8, r0, asl #9
	bl	ftl_memset
.L2366:
	add	r6, r6, #1
	b	.L2364
.L2390:
	mov	r6, #0
	movw	fp, #3922
.L2364:
	ldrh	r0, [sl, fp]
	cmp	r6, r0
	bcc	.L2367
	b	.L2368
.L2387:
	ldr	r3, .L2391+4
	mov	r6, #36
	ldr	r1, [r3, #-968]
	mla	r6, r6, r4, r1
	ldr	r1, [sp, #28]
	cmp	r5, r1
	str	r2, [r6, #4]
	movw	r2, #3922
	bne	.L2369
	ldrh	fp, [sl, r2]
	mov	r0, r7
	ldr	r3, [r3, #-940]
	mov	r1, fp
	str	r3, [r6, #8]
	bl	__aeabi_uidivmod
	ldr	r2, [sp, #48]
	rsb	r3, r1, fp
	str	r1, [sp, #56]
	cmp	r3, r2
	movcc	r2, r3
	str	r2, [sp, #52]
	cmp	r2, fp
	streq	r8, [r6, #8]
	b	.L2370
.L2369:
	ldr	r1, [sp, #32]
	cmp	r5, r1
	ldrneh	r3, [sl, r2]
	mulne	r3, r3, r5
	bne	.L2388
	ldr	r3, [r3, #-936]
	ldrh	r2, [sl, r2]
	ldr	r1, [sp, #40]
	str	r3, [r6, #8]
	mul	r3, r2, r5
	rsb	r9, r3, r1
	cmp	r9, r2
	bne	.L2370
.L2388:
	rsb	r3, r7, r3
	add	r3, r8, r3, asl #9
	str	r3, [r6, #8]
.L2370:
	movw	r3, #3928
	ldr	r2, .L2391+4
	ldrh	r3, [sl, r3]
	str	r5, [r6, #16]
	ldr	r2, [r2, #-928]
	mul	r3, r3, r4
	add	r4, r4, #1
	bic	r3, r3, #3
	add	r3, r2, r3
	str	r3, [r6, #12]
.L2368:
	ldr	r2, [sp, #24]
	add	r5, r5, #1
	subs	r2, r2, #1
	str	r2, [sp, #24]
	beq	.L2372
	movw	r3, #3848
	ldrh	r3, [sl, r3]
	cmp	r4, r3, asl #3
	bne	.L2386
.L2372:
	cmp	r4, #0
	beq	.L2386
	ldr	r6, .L2391+4
	mov	r1, r4
	mov	r2, #0
	mov	fp, r5
	mov	r5, r4
	ldr	r0, [r6, #-968]
	bl	FlashReadPages
	ldr	r3, [sp, #56]
	ldr	r1, [sp, #52]
	mov	r2, r9, asl #9
	str	r2, [sp, #68]
	mov	r3, r3, asl #9
	str	r3, [sp, #60]
	mov	r1, r1, asl #9
	mov	r3, #0
	str	r1, [sp, #64]
	str	r3, [sp, #44]
.L2379:
	ldr	r1, [sp, #44]
	mov	r4, #36
	ldr	r3, [r6, #-968]
	mul	r4, r4, r1
	ldr	r1, [sp, #28]
	add	r3, r3, r4
	ldr	r2, [r3, #16]
	cmp	r2, r1
	bne	.L2374
	ldr	r1, [r3, #8]
	ldr	r3, [r6, #-940]
	cmp	r1, r3
	bne	.L2375
	ldr	r2, [sp, #60]
	mov	r0, r8
	add	r1, r1, r2
	ldr	r2, [sp, #64]
	b	.L2389
.L2374:
	ldr	r1, [sp, #32]
	cmp	r2, r1
	bne	.L2375
	ldr	r1, [r3, #8]
	ldr	r3, [r6, #-936]
	cmp	r1, r3
	bne	.L2375
	movw	r2, #3922
	ldr	r3, [sp, #32]
	ldrh	r0, [sl, r2]
	ldr	r2, [sp, #68]
	mul	r0, r0, r3
	rsb	r0, r7, r0
	add	r0, r8, r0, asl #9
.L2389:
	bl	memcpy
.L2375:
	ldr	r2, [r6, #-968]
	add	r3, r2, r4
	ldr	r2, [r2, r4]
	cmn	r2, #1
	streq	r2, [sp, #36]
	ldreq	r1, [r6, #-1636]
	addeq	r1, r1, #1
	streq	r1, [r6, #-1636]
	ldr	r2, [r3, #12]
	ldr	r1, [r3, #16]
	ldr	r2, [r2, #8]
	cmp	r1, r2
	beq	.L2377
	ldr	r2, [r6, #-1636]
	ldr	r0, .L2391+8
	add	r2, r2, #1
	str	r2, [r6, #-1636]
	ldr	ip, [r3, #12]
	ldr	r2, [r3, #8]
	ldr	r1, [ip, #4]
	str	r1, [sp, #0]
	ldr	r1, [ip, #8]
	str	r1, [sp, #4]
	ldr	r1, [ip, #12]
	str	r1, [sp, #8]
	ldr	r1, [r2, #0]
	str	r1, [sp, #12]
	ldr	r2, [r2, #4]
	str	r2, [sp, #16]
	ldr	r1, [r3, #16]
	ldr	r2, [r3, #4]
	ldr	r3, [ip, #0]
	bl	printk
.L2377:
	ldr	r3, [r6, #-968]
	add	r2, r3, r4
	ldr	r3, [r3, r4]
	cmp	r3, #256
	bne	.L2378
	ldr	r0, [r2, #4]
	ubfx	r0, r0, #10, #16
	bl	P2V_block_in_plane
	bl	FtlGcRefreshBlock
.L2378:
	ldr	r1, [sp, #44]
	add	r1, r1, #1
	str	r1, [sp, #44]
	cmp	r1, r5
	bne	.L2379
	mov	r5, fp
	mov	r4, #0
.L2386:
	ldr	r2, [sp, #24]
	cmp	r2, #0
	bne	.L2380
	ldr	r3, .L2391+12
	ldr	r2, .L2391+4
	ldrh	r3, [r2, r3]
	cmp	r3, #0
	beq	.L2361
	ldr	r0, [sp, #24]
	mov	r1, #1
	bl	ftl_do_gc
.L2361:
	ldr	r0, [sp, #36]
	add	sp, sp, #84
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, sl, fp, pc}
.L2392:
	.align	2
.L2391:
	.word	.LANCHOR0
	.word	.LANCHOR2
	.word	.LC142
	.word	-1134
	.fnend
	.size	ftl_read, .-ftl_read
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
	.global	ftl_vendor_read
	.type	ftl_vendor_read, %function
ftl_vendor_read:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	mov	ip, r1
	mov	r3, r2
	stmfd	sp!, {r4, lr}
	.save {r4, lr}
	mov	r4, r0
	mov	r1, r4
	mov	r0, #16
	mov	r2, ip
	ldmfd	sp!, {r4, lr}
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
	mov	r6, r0
	mov	r5, r1
	mov	r4, r2
	bl	rknand_device_lock
	mov	r2, r4
	mov	r1, r5
	mov	r0, r6
	bl	ftl_vendor_read
	mov	r4, r0
	bl	rknand_device_unlock
	mov	r0, r4
	ldmfd	sp!, {r4, r5, r6, pc}
	.fnend
	.size	FlashBootVendorRead, .-FlashBootVendorRead
	.align	2
	.type	rk_ftl_vendor_ops.constprop.27, %function
rk_ftl_vendor_ops.constprop.27:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	@ link register save eliminated.
	cmp	r2, #0
	mov	r3, r0
	mov	r2, r3
	mov	r0, r1
	mov	r1, #128
	beq	.L2399
	b	FlashBootVendorWrite
.L2399:
	b	FlashBootVendorRead
	.fnend
	.size	rk_ftl_vendor_ops.constprop.27, .-rk_ftl_vendor_ops.constprop.27
	.align	2
	.global	FtlCheckVpc
	.type	FtlCheckVpc, %function
FtlCheckVpc:
	.fnstart
	@ args = 0, pretend = 0, frame = 8
	@ frame_needed = 0, uses_anonymous_args = 0
	stmfd	sp!, {r0, r1, r2, r4, r5, r6, r7, r8, sl, lr}
	.save {r0, r1, r2, r4, r5, r6, r7, r8, sl, lr}
	mov	r4, #0
	ldr	r1, .L2411
	ldr	r0, .L2411+4
	bl	printk
	ldr	r0, .L2411+8
	mov	r1, #8192
	ldr	r6, .L2411+12
	bl	__memzero
	ldr	r5, .L2411+16
	b	.L2401
.L2403:
	mov	r0, r4
	add	r1, sp, #4
	mov	r2, #0
	bl	log2phys
	ldr	r0, [sp, #4]
	cmn	r0, #1
	beq	.L2402
	ubfx	r0, r0, #10, #16
	bl	P2V_block_in_plane
	add	r0, r5, r0, asl #1
	add	r0, r0, #464
	ldrh	r3, [r0, #0]
	add	r3, r3, #1
	strh	r3, [r0, #0]	@ movhi
.L2402:
	add	r4, r4, #1
.L2401:
	ldr	r3, [r6, #3976]
	ldr	r7, .L2411+12
	cmp	r4, r3
	bcc	.L2403
	ldr	r5, .L2411+16
	mov	r6, #3856
	ldr	r0, .L2411+20
	mov	r2, #2
	ldrh	r3, [r7, r6]
	mov	r4, #0
	ldr	r1, [r5, #-2016]
	bl	rknand_print_hex
	ldr	r0, .L2411+24
	add	r1, r5, #464
	mov	r2, #2
	ldrh	r3, [r7, r6]
	bl	rknand_print_hex
	b	.L2404
.L2406:
	ldr	r2, [r5, #-2016]
	mov	r3, r4, asl #1
	ldrh	r2, [r2, r3]
	add	r3, r5, r3
	add	r3, r3, #464
	ldrh	r3, [r3, #0]
	cmp	r2, r3
	beq	.L2405
	ldr	r0, .L2411+28
	mov	r1, r4
	bl	printk
.L2405:
	add	r4, r4, #1
	uxth	r4, r4
.L2404:
	ldrh	r3, [r7, r6]
	cmp	r3, r4
	ldr	r3, .L2411+16
	bhi	.L2406
	ldr	r2, [r3, #-2004]
	cmp	r2, #0
	beq	.L2400
	ldr	r4, [r3, #-2024]
	mov	r6, #0
	ldr	r1, .L2411+32
	mov	r5, r3
	rsb	r2, r4, r2
	ldr	r4, .L2411+36
	mov	r8, #6
	movw	sl, #65535
	mov	r2, r2, asr #1
	ldrh	r7, [r3, r1]
	mul	r4, r4, r2
	uxth	r4, r4
	b	.L2408
.L2410:
	ldr	r2, [r5, #-2016]
	mov	r3, r4, asl #1
	ldrh	r2, [r2, r3]
	cmp	r2, #0
	beq	.L2409
	add	r3, r5, r3
	ldr	r0, .L2411+40
	add	r3, r3, #464
	mov	r1, r4
	ldrh	r3, [r3, #0]
	bl	printk
.L2409:
	mul	r4, r8, r4
	ldr	r3, [r5, #-2024]
	ldrh	r4, [r3, r4]
	cmp	r4, sl
	beq	.L2400
	add	r6, r6, #1
	uxth	r6, r6
.L2408:
	cmp	r6, r7
	bne	.L2410
.L2400:
	ldmfd	sp!, {r1, r2, r3, r4, r5, r6, r7, r8, sl, pc}
.L2412:
	.align	2
.L2411:
	.word	.LANCHOR3+77
	.word	.LC102
	.word	.LANCHOR2+464
	.word	.LANCHOR0
	.word	.LANCHOR2
	.word	.LC158
	.word	.LC159
	.word	.LC160
	.word	-2000
	.word	-1431655765
	.word	.LC161
	.fnend
	.size	FtlCheckVpc, .-FtlCheckVpc
	.align	2
	.global	HynixGetReadRetryDefault
	.type	HynixGetReadRetryDefault, %function
HynixGetReadRetryDefault:
	.fnstart
	@ args = 0, pretend = 0, frame = 64
	@ frame_needed = 0, uses_anonymous_args = 0
	ldr	r3, .L2498
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
	bne	.L2414
	mvn	r2, #88
	strb	r2, [r3, #4]
	ldr	r3, .L2498+4
	mvn	r2, #8
	strb	r2, [r3, #3121]
	b	.L2470
.L2414:
	cmp	r4, #3
	bne	.L2416
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
	b	.L2493
.L2416:
	cmp	r4, #4
	bne	.L2417
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
.L2493:
	mov	r6, #8
	strb	r2, [r3, #11]
	mov	r5, r6
	b	.L2415
.L2417:
	cmp	r4, #5
	bne	.L2418
	mov	r2, #56
	mov	r6, #8
	strb	r2, [r3, #4]
	mov	r2, #57
	strb	r2, [r3, #5]
	mov	r2, #58
	strb	r2, [r3, #6]
	mov	r2, #59
	strb	r2, [r3, #7]
	b	.L2492
.L2418:
	cmp	r4, #6
	bne	.L2419
	mov	r2, #14
	mov	r6, #12
	strb	r2, [r3, #4]
	mov	r2, #15
	strb	r2, [r3, #5]
	mov	r2, #16
	strb	r2, [r3, #6]
	mov	r2, #17
	strb	r2, [r3, #7]
	b	.L2492
.L2419:
	cmp	r4, #7
	bne	.L2470
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
	b	.L2415
.L2470:
	mov	r6, #7
.L2492:
	mov	r5, #4
.L2415:
	sub	r3, r4, #1
	cmp	r3, #1
	bhi	.L2488
	b	.L2496
.L2426:
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
.L2422:
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
	bcc	.L2422
	mov	r2, #0
	b	.L2423
.L2424:
	ldrb	r7, [r0, r1, asl #2]	@ zero_extendqisi2
	ldrb	r5, [r4, r2]	@ zero_extendqisi2
	add	r5, r7, r5
	strb	r5, [ip, r1, asl #3]
	add	r1, r1, #1
	cmp	r1, #7
	bne	.L2424
	add	r2, r2, #1
	cmp	r2, #4
	beq	.L2425
.L2423:
	mov	r1, #1
	add	ip, r4, r2
	add	r0, r2, r9
	b	.L2424
.L2425:
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
	b	.L2420
.L2496:
	ldr	r9, .L2498+8
	mov	r3, r6
	mov	r8, #0
	mov	r6, r5
.L2420:
	ldr	fp, .L2498
	ldrb	r2, [fp, #3762]	@ zero_extendqisi2
	cmp	r2, r8
	bhi	.L2426
	mov	r5, r6
	mov	r6, r3
	b	.L2427
.L2488:
	sub	r3, r4, #3
	cmp	r3, #4
	bhi	.L2427
	mul	r3, r6, r5
	mov	sl, #0
	mov	r7, sl
	str	r6, [sp, #48]
	mov	r2, r3, asr #2
	mov	r3, r3, asl #4
	str	r2, [sp, #24]
	str	r3, [sp, #52]
	b	.L2428
.L2469:
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
	bne	.L2431
	mov	r3, #255
	str	r3, [r8, #4]
	mov	r3, #64
	str	r3, [r9, fp, asl #8]
	mov	r3, #204
	b	.L2494
.L2431:
	sub	r3, r4, #5
	cmp	r3, #1
	ldrls	r3, .L2498
	ldrlsb	r3, [r3, #4]	@ zero_extendqisi2
	strls	r3, [r8, #4]
	movls	r3, #82
	bls	.L2495
.L2433:
	mov	r3, #174
	str	r3, [r8, #4]
	mov	r3, #176
	str	r7, [r9, fp, asl #8]
.L2494:
	str	r3, [r8, #4]
	mov	r3, #77
.L2495:
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
	bls	.L2437
	cmp	r4, #7
	moveq	r1, #32
	movne	r1, #2
.L2437:
	ldr	r3, .L2498+12
	ldr	r2, [r3, #152]
	mov	r3, #0
.L2438:
	ldr	r0, [r8, #0]
	strb	r0, [r2, r3]
	add	r3, r3, #1
	uxtb	r0, r3
	cmp	r0, r1
	bcc	.L2438
	cmp	r4, #7
	bne	.L2439
	mov	r3, #0
.L2441:
	ldrb	r1, [r2, #0]	@ zero_extendqisi2
	cmp	r1, #12
	beq	.L2440
	ldrb	r1, [r2, #1]	@ zero_extendqisi2
	cmp	r1, #10
	beq	.L2440
	add	r3, r3, #1
	add	r2, r2, #4
	uxtb	r3, r3
	cmp	r3, #8
	bne	.L2441
	b	.L2442
.L2440:
	cmp	r3, #6
	bls	.L2443
.L2442:
	ldr	r0, .L2498+16
	mov	r1, #0
	bl	printk
.L2444:
	b	.L2444
.L2439:
	cmp	r4, #6
	bne	.L2443
	mov	r1, #8
.L2445:
	ldrb	r3, [r2], #1	@ zero_extendqisi2
	cmp	r3, #12
	beq	.L2443
	ldrb	r3, [r2, #7]	@ zero_extendqisi2
	cmp	r3, #4
	beq	.L2443
	sub	r1, r1, #1
	uxtb	r1, r1
	cmp	r1, #0
	bne	.L2445
	b	.L2497
.L2447:
	b	.L2447
.L2443:
	ldr	r3, .L2498+12
	mov	r2, #0
	ldr	ip, [r3, #152]
	str	ip, [sp, #40]
.L2448:
	ldr	r1, [r8, #0]
	ldr	lr, [sp, #40]
	ldr	ip, [sp, #52]
	strb	r1, [lr, r2]
	add	r2, r2, #1
	cmp	r2, ip
	blt	.L2448
	ldr	r2, [sp, #24]
	ldr	r1, [r3, #152]
	mov	r3, #8
	mov	ip, r2, asl #2
	mov	lr, r2, asl #3
	add	r1, r1, ip
	str	ip, [sp, #32]
.L2450:
	mov	r2, #0
	mov	r0, r2
.L2449:
	ldr	ip, [r1, r2]
	add	r0, r0, #1
	mvn	ip, ip
	str	ip, [r1, r2]
	ldr	ip, [sp, #24]
	add	r2, r2, #4
	cmp	r0, ip
	blt	.L2449
	subs	r3, r3, #1
	add	r1, r1, lr
	bne	.L2450
	str	r3, [sp, #16]
	str	sl, [sp, #60]
	b	.L2451
.L2455:
	mov	lr, #1
	mov	r0, #0
	mov	ip, lr, asl r2
	mov	lr, r0
	mov	sl, #16
	str	r3, [sp, #4]
	str	sl, [sp, #20]
.L2453:
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
	bne	.L2453
	cmp	lr, #8
	add	r2, r2, #1
	orrhi	r1, r1, ip
	cmp	r2, #32
	ldr	r3, [sp, #4]
	bne	.L2455
	ldr	ip, [sp, #16]
	ldr	r2, [sp, #24]
	add	ip, ip, #1
	ldr	sl, [sp, #44]
	cmp	ip, r2
	str	ip, [sp, #16]
	str	r1, [sl, r3]
	add	r3, r3, #4
	bge	.L2456
.L2451:
	ldr	sl, .L2498+12
	mov	r1, #0
	mov	r2, r1
	ldr	sl, [sl, #152]
	add	ip, sl, r3
	str	ip, [sp, #56]
	str	sl, [sp, #44]
	b	.L2455
.L2456:
	ldr	r3, .L2498+12
	ldr	sl, [sp, #60]
	ldr	r1, [r3, #152]
	mov	r3, #0
	mov	r2, r3
.L2458:
	ldr	r0, [r1, r3]
	add	r3, r3, #4
	cmp	r0, #0
	addeq	r2, r2, #1
	cmp	r3, #32
	bne	.L2458
	cmp	r2, #7
	ble	.L2459
	ldr	r0, .L2498+20
	mov	r2, #1
	mov	r3, #1024
	bl	rknand_print_hex
	ldr	r0, .L2498+16
	mov	r1, #0
	bl	printk
.L2460:
	b	.L2460
.L2459:
	cmp	r4, #6
	moveq	r0, #4
	streq	r0, [sp, #16]
	beq	.L2461
	cmp	r4, #7
	moveq	lr, #10
	movne	lr, #8
	str	lr, [sp, #16]
.L2461:
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
	b	.L2464
.L2465:
	ldrb	lr, [ip], #1	@ zero_extendqisi2
	strb	lr, [r1, r3]
	add	r3, r3, #1
	uxtb	lr, r3
	cmp	lr, r5
	bcc	.L2465
	ldr	r3, [sp, #20]
	add	r2, r2, #1
	ldr	ip, [sp, #16]
	add	r0, r0, r3
	ldr	r3, [sp, #48]
	add	r1, r1, ip
	cmp	r2, r3
	bge	.L2466
.L2464:
	mov	ip, r0
	mov	r3, #0
	b	.L2465
.L2466:
	mov	r3, #255
	mov	r0, r6
	str	r3, [r8, #8]
	bl	NandcWaitFlashReady
	ldr	ip, [sp, #36]
	cmp	ip, #1
	movhi	r3, #56
	strhi	r3, [r8, #8]
	bhi	.L2468
	mov	r3, #54
	str	r3, [r8, #8]
	ldr	r3, .L2498
	mov	r0, sl
	mvn	r1, #0
	ldrb	r3, [r3, #4]	@ zero_extendqisi2
	str	r3, [r8, #4]
	mov	r3, #22
	str	r7, [r9, fp, asl #8]
	str	r3, [r8, #8]
	bl	FlashReadCmd
.L2468:
	mov	r0, r6
	add	sl, sl, #1
	bl	NandcWaitFlashReady
	uxtb	sl, sl
.L2428:
	ldr	r1, .L2498
	ldrb	r3, [r1, #3762]	@ zero_extendqisi2
	cmp	r3, sl
	bhi	.L2469
	ldr	r6, [sp, #48]
.L2427:
	ldr	r3, .L2498
	strb	r5, [r3, #1]
	strb	r6, [r3, #2]
	add	sp, sp, #68
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, sl, fp, pc}
.L2497:
	ldr	r0, .L2498+16
	bl	printk
	b	.L2447
.L2499:
	.align	2
.L2498:
	.word	.LANCHOR0
	.word	.LANCHOR1
	.word	.LANCHOR1+3104
	.word	.LANCHOR2
	.word	.LC162
	.word	.LC163
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
	bhi	.L2502
	ldmfd	sp!, {r3, lr}
	b	HynixGetReadRetryDefault
.L2502:
	cmp	r3, #49
	bne	.L2503
	ldr	r0, .L2504
	mov	r2, #64
	ldr	r1, .L2504+4
	strb	r3, [r0, #0]
	mov	r3, #4
	strb	r3, [r0, #1]
	mov	r3, #15
	strb	r3, [r0, #2]
	add	r0, r0, #4
	bl	memcpy
	ldmfd	sp!, {r3, pc}
.L2503:
	ldmfd	sp!, {r3, lr}
	b	FlashGetReadRetryDefault.part.26
.L2505:
	.align	2
.L2504:
	.word	.LANCHOR0
	.word	.LANCHOR1+3020
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
	ldr	r5, .L2557
	bl	ftl_malloc
	ldr	r6, .L2557+4
	mov	r4, #0
	str	r0, [r5, #152]
	mov	r0, #32768
	bl	ftl_malloc
	str	r0, [r5, #140]
	mov	r0, #4096
	bl	ftl_malloc
	str	r0, [r5, #232]
	mov	r0, #32768
	bl	ftl_malloc
	str	r0, [r5, #224]
	mov	r0, #4096
	bl	ftl_malloc
	mov	r3, #50
	str	r4, [r5, #160]
	strb	r3, [r5, #168]
	strb	r3, [r6, #853]
	mov	r3, #128
	str	r4, [r5, #172]
	str	r3, [r6, #856]
	mov	r3, #60
	strb	r4, [r6, #3780]
	strb	r3, [r5, #156]
	strb	r4, [r6, #852]
	str	r0, [r5, #228]
	mov	r0, r7
	bl	NandcInit
	ldr	r5, .L2557+8
	mov	r7, #44
.L2510:
	uxtb	r0, r4
	mov	r1, r5
	bl	FlashReadIDRaw
	cmp	r4, #0
	bne	.L2507
	ldrb	r3, [r6, #2980]	@ zero_extendqisi2
	sub	r3, r3, #1
	uxtb	r3, r3
	cmp	r3, #253
	bhi	.L2548
	ldrb	r3, [r6, #2981]	@ zero_extendqisi2
	cmp	r3, #255
	beq	.L2549
.L2507:
	ldrb	r3, [r5, #0]	@ zero_extendqisi2
	add	r4, r4, #1
	cmp	r3, #181
	streqb	r7, [r5, #0]
	cmp	r4, #4
	add	r5, r5, #8
	bne	.L2510
	ldr	r4, .L2557+4
	ldrb	r3, [r4, #2980]	@ zero_extendqisi2
	cmp	r3, #173
	beq	.L2511
	ldr	r0, [r4, #3784]
	bl	NandcSetDdrMode
.L2511:
	ldr	r5, .L2557+4
	mov	r1, #0
	mov	r2, #852
	mov	r0, r5
	bl	ftl_memset
	ldr	r3, .L2557+12
	str	r3, [r4, #3624]
	mov	r3, #0
	strb	r3, [r4, #860]
	ldrb	r3, [r4, #2981]	@ zero_extendqisi2
	cmp	r3, #161
	cmpne	r3, #241
	beq	.L2512
	cmp	r3, #218
	beq	.L2512
	cmp	r3, #209
	beq	.L2512
	cmp	r3, #220
	bne	.L2513
	ldrb	r2, [r5, #2983]	@ zero_extendqisi2
	cmp	r2, #149
	bne	.L2513
.L2512:
	ldr	r2, .L2557+4
	mov	r1, #1
	mov	r0, #16
	ldr	ip, .L2557+16
	strb	r1, [r2, #852]
	ldr	r1, .L2557
	strb	r0, [r2, #853]
	strb	r3, [ip, #3134]
	strb	r0, [r1, #156]
	ldrb	r0, [r2, #2980]	@ zero_extendqisi2
	cmp	r0, #152
	strb	r0, [ip, #3133]
	bne	.L2514
	ldrb	r0, [r2, #2984]	@ zero_extendqisi2
	sxtb	r0, r0
	cmp	r0, #0
	movge	r0, #24
	strgeb	r0, [r1, #156]
.L2514:
	ldr	r2, [r2, #3776]
	movw	r0, #2049
	ldr	r1, .L2557+20
	cmp	r2, r1
	cmpne	r2, r0
	ldreq	r2, .L2557
	moveq	r1, #16
	streqb	r1, [r2, #156]
	cmp	r3, #218
	bne	.L2516
	ldr	r3, .L2557+16
	movw	r2, #3146
	mov	r1, #2048
	strh	r1, [r3, r2]	@ movhi
	mvn	r2, #37
	b	.L2554
.L2516:
	cmp	r3, #220
	bne	.L2517
	ldr	r3, .L2557+16
	movw	r2, #3146
	mov	r1, #4096
	strh	r1, [r3, r2]	@ movhi
	mvn	r2, #35
.L2554:
	strb	r2, [r3, #3134]
.L2517:
	ldr	r1, .L2557+24
	mov	r2, #32
	ldr	r0, .L2557+28
	bl	memcpy
	ldr	r0, .L2557+12
	mov	r2, #32
	add	r1, r0, #144
	bl	memcpy
.L2513:
	ldr	r4, .L2557+4
	ldrb	r3, [r4, #852]	@ zero_extendqisi2
	cmp	r3, #0
	bne	.L2518
	bl	FlashLoadPhyInfoInRam
	cmp	r0, #0
	bne	.L2519
	ldr	r3, [r4, #3624]
	ldrh	r0, [r3, #16]
	ubfx	r0, r0, #8, #3
	strb	r0, [r4, #3772]
	tst	r0, #1
	bne	.L2519
	mov	r3, #1
	strb	r3, [r4, #3780]
	bl	FlashSetInterfaceMode
	ldrb	r0, [r4, #3772]	@ zero_extendqisi2
	bl	NandcSetMode
.L2519:
	ldr	r4, .L2557+4
	ldr	r3, [r4, #3624]
	ldrb	r3, [r3, #26]	@ zero_extendqisi2
	strb	r3, [r4, #928]
	bl	FlashLoadPhyInfo
	cmp	r0, #0
	beq	.L2518
	ldr	r3, [r4, #3784]
	cmp	r3, #0
	beq	.L2521
	mov	r0, #1
	bl	FlashSetInterfaceMode
	mov	r0, #1
	b	.L2555
.L2521:
	ldrb	r0, [r4, #3772]	@ zero_extendqisi2
	bl	FlashSetInterfaceMode
	ldrb	r0, [r4, #3772]	@ zero_extendqisi2
.L2555:
	bl	NandcSetMode
	bl	FlashLoadPhyInfo
	cmp	r0, #0
	beq	.L2518
	ldr	r4, .L2557+4
	mov	r0, #1
	bl	FlashSetInterfaceMode
	mov	r0, #1
	bl	NandcSetMode
	ldr	r0, .L2557+32
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
	ldr	r1, .L2557+36
	ldr	r0, .L2557
	ldr	r3, [r4, #3624]
	ldrh	r1, [r0, r1]
	ldrb	r2, [r3, #9]	@ zero_extendqisi2
	add	r1, r1, #4080
	add	r1, r1, #15
	cmp	r2, r1, lsr #12
	blt	.L2524
	ldrh	r1, [r3, #14]
	add	r1, r1, #255
	cmp	r2, r1, lsr #8
	bge	.L2525
.L2524:
	ldrh	r2, [r3, #14]
	bic	r2, r2, #255
	strh	r2, [r3, #14]	@ movhi
.L2525:
	ldr	r3, .L2557+4
	ldrb	r3, [r3, #3772]	@ zero_extendqisi2
	tst	r3, #6
	beq	.L2526
	bl	FlashSavePhyInfo
	ldr	r3, .L2557
	mov	r0, #0
	ldr	r1, [r3, #164]
	bl	FlashDdrParaScan
.L2526:
	bl	FlashSavePhyInfo
.L2518:
	ldr	r4, .L2557+4
	ldr	r5, .L2557
	ldr	r3, [r4, #3624]
	ldrb	r2, [r3, #26]	@ zero_extendqisi2
	ldrh	r0, [r3, #10]
	ldrb	r6, [r3, #18]	@ zero_extendqisi2
	strb	r2, [r4, #928]
	ldrh	r2, [r3, #16]
	ubfx	r1, r2, #7, #1
	strb	r1, [r4, #860]
	ubfx	r1, r2, #3, #1
	strb	r1, [r5, #180]
	ubfx	r1, r2, #4, #1
	ubfx	r2, r2, #8, #3
	strb	r1, [r4, #3837]
	strb	r2, [r4, #3772]
	mov	r2, #0
	ldrb	r1, [r3, #12]	@ zero_extendqisi2
	str	r2, [r5, #176]
	bl	__aeabi_idiv
	mov	r1, r0
	mov	r0, r6
	bl	BuildFlashLsbPageTable
	bl	FlashDieInfoInit
	ldr	r3, [r4, #3624]
	ldrh	r2, [r3, #16]
	tst	r2, #64
	beq	.L2527
	ldrb	r0, [r3, #19]	@ zero_extendqisi2
	ldrb	r3, [r4, #1]	@ zero_extendqisi2
	strb	r0, [r4, #3761]
	strb	r3, [r4, #3760]
	ldrb	r3, [r4, #2]	@ zero_extendqisi2
	strb	r3, [r5, #144]
	sub	r3, r0, #1
	uxtb	r3, r3
	cmp	r3, #6
	bhi	.L2528
	ldr	r3, .L2557+40
	str	r3, [r5, #176]
	sub	r3, r0, #5
	uxtb	r3, r3
	cmp	r3, #1
	movls	r3, #1
	strls	r3, [r4, #3828]
	bls	.L2530
	cmp	r0, #7
	addeq	r4, r4, #28
	beq	.L2531
.L2530:
	ldr	r4, .L2557+44
.L2531:
	mov	r3, #0
	mov	r2, r3
.L2533:
	ldrsb	r1, [r4, r2]
	add	r2, r2, #1
	cmp	r1, #0
	addeq	r3, r3, #1
	cmp	r2, #32
	bne	.L2533
	cmp	r3, #27
	bls	.L2527
	bl	FlashGetReadRetryDefault
	bl	FlashSavePhyInfo
	b	.L2527
.L2528:
	sub	r3, r0, #17
	uxtb	r3, r3
	cmp	r3, #2
	bhi	.L2534
	ldr	r3, .L2557+48
	cmp	r0, #19
	str	r3, [r5, #176]
	mov	r3, #7
	strb	r3, [r5, #144]
	moveq	r3, #15
	streqb	r3, [r5, #144]
	b	.L2527
.L2534:
	cmp	r0, #33
	cmpne	r0, #65
	beq	.L2535
	cmp	r0, #66
	bne	.L2536
.L2535:
	ldr	r3, .L2557
	mov	r1, #4
	ldr	r2, .L2557+52
	str	r2, [r3, #176]
	ldr	r2, .L2557+4
	strb	r1, [r2, #3760]
	mov	r2, #7
	strb	r2, [r3, #144]
	b	.L2527
.L2536:
	cmp	r0, #67
	cmpne	r0, #34
	beq	.L2537
	cmp	r0, #35
	beq	.L2537
	cmp	r0, #68
	bne	.L2538
.L2537:
	ldr	r3, .L2557
	cmp	r0, #35
	cmpne	r0, #68
	ldr	r2, .L2557+52
	sub	r0, r0, #67
	uxtb	r0, r0
	str	r2, [r3, #176]
	mov	r2, #7
	strb	r2, [r3, #144]
	moveq	r2, #17
	streqb	r2, [r3, #144]
	cmp	r0, #1
	ldr	r3, .L2557+4
	movls	r2, #4
	movhi	r2, #5
	strb	r2, [r3, #3760]
	b	.L2527
.L2538:
	cmp	r0, #49
	ldreq	r3, .L2557+56
	streq	r3, [r5, #176]
.L2527:
	ldr	r2, .L2557+4
	ldr	r3, .L2557+20
	ldr	r1, [r2, #3776]
	cmp	r1, r3
	bne	.L2541
	ldrb	r3, [r2, #928]	@ zero_extendqisi2
	cmp	r3, #0
	ldrne	r3, [r2, #3624]
	movne	r1, #0
	strneb	r1, [r3, #18]
.L2541:
	ldrb	r2, [r2, #2980]	@ zero_extendqisi2
	ldr	r3, .L2557+4
	cmp	r2, #44
	bne	.L2542
	ldrb	r2, [r3, #3780]	@ zero_extendqisi2
	cmp	r2, #0
	beq	.L2542
	ldr	r1, [r3, #3776]
	ldr	r2, .L2557+20
	cmp	r1, r2
	bne	.L2543
	ldrb	r3, [r3, #928]	@ zero_extendqisi2
	cmp	r3, #0
	bne	.L2542
.L2543:
	ldr	r3, .L2557+4
	mov	r2, #0
	mov	r0, #1
	strb	r2, [r3, #3780]
	bl	FlashSetInterfaceMode
	mov	r0, #1
	bl	NandcSetMode
.L2542:
	mov	r0, #0
	bl	flash_enter_slc_mode
	ldr	r2, .L2557+4
	ldrb	r3, [r2, #3772]	@ zero_extendqisi2
	tst	r3, #6
	beq	.L2544
	ldrb	r2, [r2, #3780]	@ zero_extendqisi2
	cmp	r2, #0
	bne	.L2545
	tst	r3, #1
	bne	.L2544
.L2545:
	ldr	r3, .L2557
	mov	r0, #0
	ldr	r1, [r3, #164]
	bl	FlashDdrParaScan
.L2544:
	ldr	r4, .L2557+4
	mov	r0, #0
	bl	flash_exit_slc_mode
	mov	r8, #16
	movw	r6, #3062
	ldr	r3, [r4, #3624]
	ldrb	r0, [r3, #20]	@ zero_extendqisi2
	bl	FlashBchSel
	add	r0, r4, #932
	bl	FlashReadIdbDataRaw
	ldr	r0, .L2557+60
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
	bne	.L2547
	mov	r7, r7, lsr #1
	mov	ip, ip, asl #1
	mov	r1, r1, asl #1
	strb	r8, [r4, #853]
	strh	r1, [r4, r2]	@ movhi
	mov	r2, #8
	strh	r7, [r4, r6]	@ movhi
	strh	ip, [r4, r0]	@ movhi
	strh	r2, [r4, r3]	@ movhi
.L2547:
	ldrb	r0, [r5, #20]	@ zero_extendqisi2
	bl	FlashBchSel
	bl	ftl_flash_suspend
	mov	r0, #0
	ldmfd	sp!, {r4, r5, r6, r7, r8, pc}
.L2548:
	mvn	r0, #1
	ldmfd	sp!, {r4, r5, r6, r7, r8, pc}
.L2549:
	mvn	r0, #1
	ldmfd	sp!, {r4, r5, r6, r7, r8, pc}
.L2558:
	.align	2
.L2557:
	.word	.LANCHOR2
	.word	.LANCHOR0
	.word	.LANCHOR0+2980
	.word	.LANCHOR1+2988
	.word	.LANCHOR1
	.word	1446522928
	.word	.LANCHOR1+2892
	.word	.LANCHOR0+896
	.word	.LC164
	.word	-890
	.word	HynixReadRetrial
	.word	.LANCHOR0+20
	.word	MicronReadRetrial
	.word	ToshibaReadRetrial
	.word	SamsungReadRetrial
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
	stmfd	sp!, {r4, r5, r6, lr}
	.save {r4, r5, r6, lr}
	mov	r0, #2048
	bl	ftl_malloc
	ldr	r4, .L2563
	ldr	r6, .L2563+4
	mov	r5, #0
	mov	r1, r6
	str	r5, [r1, #476]!
	str	r0, [r6, #472]
	mov	r0, r4
	str	r5, [r0, #136]!
	bl	rknand_get_reg_addr
	ldr	r3, [r4, #136]
	cmp	r3, r5
	mvneq	r4, #0
	beq	.L2560
	bl	rk_nandc_irq_init
	mov	r1, r5
	mov	r2, r5
	mov	r3, #2048
	ldr	r0, [r6, #472]
	bl	FlashSramLoadStore
	bl	rknand_flash_cs_init
	ldr	r0, [r4, #136]
	bl	FlashInit
	subs	r4, r0, #0
	bne	.L2561
	bl	FtlInit
.L2561:
	ldr	r0, .L2563+8
	mov	r1, r4
	bl	printk
.L2560:
	mov	r0, r4
	ldmfd	sp!, {r4, r5, r6, pc}
.L2564:
	.align	2
.L2563:
	.word	.LANCHOR2
	.word	.LANCHOR4
	.word	.LC165
	.fnend
	.size	rk_ftl_init, .-rk_ftl_init
	.align	2
	.global	FtlReInitForSDUpdata
	.type	FtlReInitForSDUpdata, %function
FtlReInitForSDUpdata:
	.fnstart
	@ args = 0, pretend = 0, frame = 16
	@ frame_needed = 0, uses_anonymous_args = 0
	ldr	r3, .L2590
	stmfd	sp!, {r4, r5, lr}
	.save {r4, r5, lr}
	.pad #20
	sub	sp, sp, #20
	ldrb	r4, [r3, #928]	@ zero_extendqisi2
	cmp	r4, #0
	movne	r0, #0
	bne	.L2566
	ldr	r5, .L2590+4
	ldr	r0, [r5, #136]
	bl	FlashInit
	cmp	r0, #0
	movne	r0, r4
	bne	.L2566
	bl	FlashLoadFactorBbt
	cmp	r0, #0
	beq	.L2567
	bl	FlashMakeFactorBbt
.L2567:
	ldr	r0, [r5, #140]
	bl	FlashReadIdbDataRaw
	cmp	r0, #0
	beq	.L2568
	mov	r1, #0
	mov	r2, #16
	mov	r0, sp
	bl	FlashReadFacBbtData
	ldr	r1, [sp, #0]
	mov	r3, #0
	mov	r0, #1
	mov	r2, r3
.L2570:
	ands	ip, r1, r0, asl r2
	add	r2, r2, #1
	addne	r3, r3, #1
	cmp	r2, #16
	bne	.L2570
	cmp	r3, #6
	ldrls	r3, .L2590
	bls	.L2586
	mov	r2, #0
	mov	r0, #1
.L2571:
	ands	ip, r1, r0, asl r2
	add	r2, r2, #1
	addne	r3, r3, #1
	cmp	r2, #24
	bne	.L2571
	cmp	r3, #17
	ldr	r3, .L2590
	movhi	r2, #36
.L2586:
	strb	r2, [r3, #853]
	movw	r2, #3074
	ldr	r3, .L2590
	ldrb	r1, [r3, #853]	@ zero_extendqisi2
	strh	r1, [r3, r2]	@ movhi
.L2568:
	ldr	r1, .L2590+8
	mov	r4, #1
	ldr	r0, .L2590+12
	bl	printk
	ldr	r0, .L2590+16
	bl	FtlConstantsInit
	bl	FtlVariablesInit
	ldr	r3, .L2590
	ldr	r0, [r3, #3852]
	uxth	r0, r0
	bl	FtlFreeSysBlkQueueInit
	b	.L2575
.L2577:
	add	r4, r4, #1
.L2575:
	bl	FtlLoadBbt
	cmp	r0, #0
	bne	.L2588
.L2576:
	bl	FtlSysBlkInit
	cmp	r0, #0
	beq	.L2578
.L2588:
	bl	FtlLowFormat
	cmp	r4, #3
	bls	.L2577
	b	.L2589
.L2578:
	ldr	r3, .L2590+20
	mov	r2, #1
	str	r2, [r3, #3092]
	b	.L2566
.L2589:
	mvn	r0, #0
.L2566:
	add	sp, sp, #20
	ldmfd	sp!, {r4, r5, pc}
.L2591:
	.align	2
.L2590:
	.word	.LANCHOR0
	.word	.LANCHOR2
	.word	.LC78
	.word	.LC77
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
	ldr	r3, .L2594
	ldr	r2, [r3, #3776]
	ldr	r3, .L2594+4
	cmp	r2, r3
	bxne	lr
	b	flash_enter_slc_mode
.L2595:
	.align	2
.L2594:
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
	ldr	r3, .L2598
	ldr	r2, [r3, #3776]
	ldr	r3, .L2598+4
	cmp	r2, r3
	bxne	lr
	b	flash_exit_slc_mode
.L2599:
	.align	2
.L2598:
	.word	.LANCHOR0
	.word	1446522928
	.fnend
	.size	flash_boot_exit_slc_mode, .-flash_boot_exit_slc_mode
	.align	2
	.type	IdBlockReadData, %function
IdBlockReadData:
	.fnstart
	@ args = 0, pretend = 0, frame = 24
	@ frame_needed = 0, uses_anonymous_args = 0
	stmfd	sp!, {r4, r5, r6, r7, r8, r9, sl, fp, lr}
	.save {r4, r5, r6, r7, r8, r9, sl, fp, lr}
	mov	r4, r0
	ldr	r5, .L2604
	mov	fp, r1
	.pad #28
	sub	sp, sp, #28
	mov	r1, r4
	mov	sl, r2
	ldr	r0, .L2604+4
	ldr	r3, [r5, #3624]
	mov	r2, fp
	mov	r9, r5
	ldrb	r7, [r3, #9]	@ zero_extendqisi2
	ldrh	r6, [r3, #10]
	bl	printk
	mov	r0, r4
	mul	r6, r6, r7
	uxth	r6, r6
	mov	r1, r6
	mov	r6, #0
	bl	__aeabi_uidivmod
	mov	r8, r1
	rsb	r1, r1, r4
	mul	r2, r7, r8
	str	r1, [sp, #16]
	ubfx	r2, r2, #2, #2
	b	.L2601
.L2603:
	add	r1, r6, r8
	ldrb	r0, [r5, #928]	@ zero_extendqisi2
	ubfx	r1, r1, #2, #16
	rsb	ip, r2, #4
	cmp	r0, #0
	add	r3, r5, r1, asl #1
	uxth	ip, ip
	add	r3, r3, #3072
	add	r3, r3, #4
	ldrh	r3, [r3, #0]
	beq	.L2602
	ldr	r0, [r5, #3776]
	ldr	lr, .L2604+8
	cmp	r0, lr
	moveq	r3, r1
.L2602:
	ldr	r1, [sp, #16]
	add	r2, r2, r1
	mla	r2, r7, r3, r2
	ldr	r3, [r9, #3624]
	ldrb	r1, [r3, #9]	@ zero_extendqisi2
	ldrb	r3, [r9, #3836]	@ zero_extendqisi2
	str	r3, [sp, #20]
	ldr	r3, .L2604+12
	ldrb	r0, [r3, #156]	@ zero_extendqisi2
	str	ip, [sp, #4]
	str	r1, [sp, #8]
	str	r2, [sp, #12]
	bl	FlashBchSel
	mov	r0, #0
	bl	flash_boot_enter_slc_mode
	ldr	r2, [sp, #12]
	ldr	r1, [sp, #8]
	mov	r0, r2
	bl	__aeabi_uidiv
	mov	r2, sl
	mov	r1, r0
	mov	r0, #0
	mov	r3, r0
	bl	FlashReadPage
	mov	r0, #0
	bl	flash_boot_exit_slc_mode
	ldr	r0, [sp, #20]
	bl	FlashBchSel
	ldr	ip, [sp, #4]
	mov	r2, #0
	add	r6, ip, r6
	add	sl, sl, ip, asl #9
	uxth	r6, r6
.L2601:
	cmp	r6, fp
	bcc	.L2603
	mov	r1, r4
	mov	r2, fp
	mov	r3, #0
	ldr	r0, .L2604+16
	bl	printk
	mov	r0, #0
	add	sp, sp, #28
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, sl, fp, pc}
.L2605:
	.align	2
.L2604:
	.word	.LANCHOR0
	.word	.LC166
	.word	1446522928
	.word	.LANCHOR2
	.word	.LC167
	.fnend
	.size	IdBlockReadData, .-IdBlockReadData
	.section	.text.unlikely
	.align	2
	.type	IDBlockWriteData, %function
IDBlockWriteData:
	.fnstart
	@ args = 0, pretend = 0, frame = 88
	@ frame_needed = 0, uses_anonymous_args = 0
	ldr	r3, .L2614
	stmfd	sp!, {r4, r5, r6, r7, r8, r9, sl, fp, lr}
	.save {r4, r5, r6, r7, r8, r9, sl, fp, lr}
	mov	r5, r0
	ldr	r3, [r3, #3624]
	mov	fp, r1
	.pad #92
	sub	sp, sp, #92
	mov	r1, r5
	mov	sl, r2
	ldr	r0, .L2614+4
	ldrb	r6, [r3, #9]	@ zero_extendqisi2
	mov	r2, fp
	ldrh	r7, [r3, #10]
	bl	printk
	mov	r0, #0
	bl	flash_boot_enter_slc_mode
	mov	r1, r6
	mov	r0, r5
	bl	__aeabi_uidiv
	mul	r7, r7, r6
	uxth	r7, r7
	mov	r1, r0
	mov	r0, #0
	mov	r2, r0
	bl	FlashEraseBlock
	cmp	fp, r7
	movls	r8, #1
	bls	.L2607
	mov	r0, #0
	add	r1, r7, r5
	mov	r2, r0
	mov	r8, #2
	bl	FlashEraseBlock
.L2607:
	ldr	r4, .L2614
	mov	r0, #0
	bl	flash_boot_exit_slc_mode
	ldr	r3, [r4, #3624]
	mov	r9, r4
	ldrh	r0, [r3, #10]
	ldrb	r1, [r3, #12]	@ zero_extendqisi2
	mov	r0, r0, asl #2
	mul	r0, r8, r0
	bl	__aeabi_idiv
	mov	r1, r7
	mov	r8, #0
	str	r0, [sp, #16]
	mov	r0, r5
	bl	__aeabi_uidivmod
	str	r5, [sp, #20]
	mov	ip, r1
	rsb	r1, r1, r5
	mov	r7, ip
	str	r1, [sp, #12]
	b	.L2608
.L2612:
	add	r3, r8, r7
	ubfx	r3, r3, #2, #16
	cmp	r3, #0
	beq	.L2609
	add	r1, r3, #1
	ldrb	r0, [r4, #928]	@ zero_extendqisi2
	add	r2, r4, r1, asl #1
	cmp	r0, #0
	add	r2, r2, #3072
	add	r2, r2, #4
	ldrh	r2, [r2, #0]
	beq	.L2610
	ldr	r0, [r4, #3776]
	ldr	ip, .L2614+8
	cmp	r0, ip
	moveq	r2, r1
.L2610:
	sub	r2, r2, #-1073741823
	mov	r2, r2, asl #2
	str	r2, [sp, #24]
.L2609:
	movw	r2, #61424
	str	r2, [sp, #28]
	add	r2, r4, r3, asl #1
	add	r2, r2, #3072
	ldrh	r5, [r2, #4]
	ldrb	r2, [r4, #928]	@ zero_extendqisi2
	cmp	r2, #0
	beq	.L2611
	ldr	r2, [r4, #3776]
	ldr	r1, .L2614+8
	cmp	r2, r1
	moveq	r5, r3
.L2611:
	ldr	ip, [sp, #12]
	add	r8, r8, #4
	ldr	r2, .L2614+12
	uxth	r8, r8
	mla	r3, r6, r5, ip
	ldrb	ip, [r9, #3836]	@ zero_extendqisi2
	ldrb	r0, [r2, #156]	@ zero_extendqisi2
	str	ip, [sp, #4]
	str	r3, [sp, #8]
	bl	FlashBchSel
	mov	r0, #0
	bl	flash_boot_enter_slc_mode
	ldr	r2, [r9, #3624]
	ldr	r3, [sp, #8]
	ldrb	r1, [r2, #9]	@ zero_extendqisi2
	mov	r0, r3
	bl	__aeabi_uidiv
	mov	r2, sl
	add	r3, sp, #24
	add	sl, sl, #2048
	mov	r1, r0
	mov	r0, #0
	bl	FlashProgPage
	mov	r0, #0
	bl	flash_boot_exit_slc_mode
	ldr	ip, [sp, #4]
	mov	r0, ip
	bl	FlashBchSel
	ldr	r0, [sp, #12]
	mov	r1, r6
	bl	__aeabi_uidiv
	add	r2, r5, #1
	uxth	r2, r2
	mov	r1, r0
	mov	r0, #0
	bl	FlashPageProgMsbFFData
.L2608:
	ldr	ip, [sp, #16]
	cmp	r8, ip
	bcc	.L2612
	ldr	r5, [sp, #20]
	mov	r2, fp
	mov	r3, #0
	ldr	r0, .L2614+16
	mov	r1, r5
	bl	printk
	mov	r0, #0
	add	sp, sp, #92
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, sl, fp, pc}
.L2615:
	.align	2
.L2614:
	.word	.LANCHOR0
	.word	.LC168
	.word	1446522928
	.word	.LANCHOR2
	.word	.LC169
	.fnend
	.size	IDBlockWriteData, .-IDBlockWriteData
	.text
	.align	2
	.global	write_idblock
	.type	write_idblock, %function
write_idblock:
	.fnstart
	@ args = 0, pretend = 0, frame = 24
	@ frame_needed = 0, uses_anonymous_args = 0
	ldr	r3, .L2641
	stmfd	sp!, {r4, r5, r6, r7, r8, r9, sl, fp, lr}
	.save {r4, r5, r6, r7, r8, r9, sl, fp, lr}
	mov	r5, r0
	ldr	r3, [r3, #3624]
	.pad #36
	sub	sp, sp, #36
	mov	r0, #256000
	mov	r6, r1
	mov	r7, r2
	ldrb	r4, [r3, #9]	@ zero_extendqisi2
	ldrh	sl, [r3, #10]
	bl	ftl_malloc
	subs	r9, r0, #0
	beq	.L2635
	add	r5, r5, #508
	add	r5, r5, #3
	mov	r5, r5, lsr #9
	cmp	r5, #8
	movls	r5, #8
	bls	.L2618
	cmp	r5, #500
	bhi	.L2635
.L2618:
	ldr	r2, [r6, #0]
	ldr	r3, .L2641+4
	cmp	r2, r3
	bne	.L2635
	mul	sl, sl, r4
	uxth	sl, sl
	sub	r0, sl, #1
	add	r0, r0, r5
	mov	r1, sl
	bl	__aeabi_uidiv
	mov	r3, #0
	movw	r2, #63999
	movw	ip, #4097
	str	r0, [sp, #20]
	add	r0, r6, #254976
	add	r0, r0, #1020
.L2623:
	ldr	r1, [r0], #-4
	cmp	r1, #0
	bne	.L2619
	ldr	r1, [r6, r3, asl #2]
	add	r3, r3, #1
	cmp	r3, ip
	sub	r2, r2, #1
	movcs	r3, #0
	cmp	r2, #4096
	str	r1, [r0, #4]
	bne	.L2623
	b	.L2622
.L2619:
	ldr	r0, .L2641+8
	bl	printk
.L2622:
	ldr	r4, .L2641
	mov	r1, r7
	mov	r3, #5
	ldr	r0, .L2641+12
	mov	r2, #4
	mov	r8, r5
	bl	rknand_print_hex
	ldrb	r2, [r4, #853]	@ zero_extendqisi2
	ldr	r1, [r6, #512]
	ldr	r0, .L2641+16
	bl	printk
	ldr	r2, .L2641+20
	movw	r3, #3074
	mov	r1, r5
	ldrh	r3, [r4, r3]
	ldr	r2, [r2, #160]
	ldr	r0, .L2641+24
	str	r2, [sp, #0]
	mov	r2, r5
	bl	printk
	ldrb	r3, [r4, #853]	@ zero_extendqisi2
	ldr	r2, [r6, #512]
	mov	r4, r7
	mov	r7, r6
	mov	r1, r5, asl #7
	cmp	r2, r3
	str	r1, [sp, #24]
	mov	r2, #0
	strhi	r3, [r6, #512]
	str	r2, [sp, #16]
	str	r2, [sp, #12]
.L2631:
	ldr	r1, .L2641
	ldr	r3, [r4], #4
	ldrb	r2, [r1, #853]	@ zero_extendqisi2
	cmp	r3, r2
	bcs	.L2625
	ldr	r1, .L2641+20
	ldr	r2, [r1, #160]
	cmp	r3, r2
	bcc	.L2625
	ldr	r2, [sp, #20]
	ldr	r1, [sp, #12]
	cmp	r2, #1
	movls	r2, #0
	movhi	r2, #1
	str	r2, [sp, #28]
	ldr	r2, [sp, #20]
	cmp	r1, #0
	cmpne	r2, #1
	bls	.L2626
	ldr	r2, [r4, #-8]
	add	r2, r2, #1
	cmp	r3, r2
	beq	.L2625
.L2626:
	mov	r0, r9
	mov	r1, #512
	bl	__memzero
	ldr	r0, [r4, #-4]
	mov	r1, r8
	mov	r2, r7
	mov	r6, #0
	mul	r0, r0, sl
	bl	IDBlockWriteData
	ldr	r0, [r4, #-4]
	mov	r2, r9
	mov	r1, r8
	mul	r0, r0, sl
	bl	IdBlockReadData
	mov	r2, r9
	mov	r3, r7
.L2630:
	mov	fp, r2
	mov	r5, r3
	ldr	r0, [fp, #0]
	add	r2, r2, #4
	ldr	r1, [r5, #0]
	add	r3, r3, #4
	cmp	r0, r1
	beq	.L2627
	mov	r0, r9
	mov	r1, #512
	bl	__memzero
	ldr	r3, [fp, #0]
	ldr	r1, [sp, #12]
	ldr	r0, .L2641+28
	str	r3, [sp, #0]
	ldr	r3, [r5, #0]
	bic	r5, r6, #255
	mov	r5, r5, asl #2
	str	r3, [sp, #4]
	mov	r3, r6
	ldr	r2, [r4, #-4]
	bl	printk
	ldr	r0, .L2641+32
	add	r1, r7, r5
	mov	r2, #4
	mov	r3, #256
	bl	rknand_print_hex
	mov	r3, #256
	add	r1, r9, r5
	mov	r2, #4
	ldr	r0, .L2641+36
	bl	rknand_print_hex
	mov	r0, #0
	bl	flash_boot_enter_slc_mode
	ldr	r1, [r4, #-4]
	mov	r0, #0
	mov	r2, r0
	mul	r1, r1, sl
	bl	FlashEraseBlock
	ldr	r3, [sp, #28]
	cmp	r3, #0
	beq	.L2628
	ldr	r1, [r4, #-4]
	mov	r0, #0
	mov	r2, r0
	add	r1, r1, #1
	mul	r1, sl, r1
	bl	FlashEraseBlock
.L2628:
	mov	r0, #0
	bl	flash_boot_exit_slc_mode
	ldr	r1, [r4, #-4]
	ldr	r0, .L2641+40
	bl	printk
	ldr	r1, [sp, #24]
	cmp	r6, r1
	bcc	.L2625
	b	.L2629
.L2627:
	ldr	r1, [sp, #24]
	add	r6, r6, #1
	cmp	r6, r1
	bne	.L2630
.L2629:
	ldr	r2, [sp, #16]
	add	r2, r2, #1
	str	r2, [sp, #16]
.L2625:
	ldr	r3, [sp, #12]
	add	r3, r3, #1
	str	r3, [sp, #12]
	cmp	r3, #5
	bne	.L2631
	mov	r0, r9
	bl	ftl_free
	ldr	r1, [sp, #16]
	cmp	r1, #0
	mvneq	r0, #0
	movne	r0, #0
	b	.L2617
.L2635:
	mvn	r0, #0
.L2617:
	add	sp, sp, #36
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, sl, fp, pc}
.L2642:
	.align	2
.L2641:
	.word	.LANCHOR0
	.word	-52655045
	.word	.LC170
	.word	.LC171
	.word	.LC172
	.word	.LANCHOR2
	.word	.LC173
	.word	.LC174
	.word	.LC175
	.word	.LC176
	.word	.LC177
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
	ldr	ip, .L2646
	b	.L2644
.L2645:
	ldrb	r4, [r3, r2]	@ zero_extendqisi2
	add	r2, r2, #1
	eor	r4, r4, r0, lsr #24
	add	r4, ip, r4, asl #2
	ldr	r4, [r4, #3164]
	eor	r0, r4, r0, asl #8
.L2644:
	cmp	r2, r1
	bne	.L2645
	ldmfd	sp!, {r4, pc}
.L2647:
	.align	2
.L2646:
	.word	.LANCHOR1
	.fnend
	.size	CRC_32, .-CRC_32
	.align	2
	.global	write_loader_lba
	.type	write_loader_lba, %function
write_loader_lba:
	.fnstart
	@ args = 0, pretend = 0, frame = 32
	@ frame_needed = 0, uses_anonymous_args = 0
	cmp	r0, #64
	stmfd	sp!, {r4, r5, r6, r7, r8, lr}
	.save {r4, r5, r6, r7, r8, lr}
	mov	r4, r0
	.pad #32
	sub	sp, sp, #32
	mov	r6, r1
	mov	r7, r2
	bne	.L2649
	ldr	r2, [r2, #0]
	ldr	r3, .L2665
	cmp	r2, r3
	bne	.L2649
	ldr	r5, .L2665+4
	mov	r3, #1
	mov	r0, #256000
	strb	r3, [r5, #480]
	bl	ftl_malloc
	mov	r1, #0
	mov	r2, #256000
	str	r0, [r5, #484]
	bl	ftl_memset
	str	r4, [r5, #488]
.L2649:
	ldr	r3, .L2665+4
	ldrb	r2, [r3, #480]	@ zero_extendqisi2
	cmp	r2, #0
	beq	.L2648
	sub	r0, r4, #64
	ldr	r5, [r3, #484]
	cmp	r0, #500
	bcs	.L2651
	rsb	r2, r4, #564
	add	r0, r5, r0, asl #9
	cmp	r6, r2
	movcc	r2, r6
	mov	r1, r7
	mov	r2, r2, asl #9
	bl	memcpy
	b	.L2652
.L2651:
	cmp	r4, #564
	bcc	.L2652
	ldr	r0, [r3, #488]
	ldr	r3, .L2665+8
	sub	r0, r0, #64
	cmp	r0, #500
	movcs	r0, #500
	ldr	r3, [r3, #3624]
	ldrb	r3, [r3, #9]	@ zero_extendqisi2
	cmp	r3, #4
	beq	.L2662
	mov	r3, #4
	mov	r1, #2
	mov	r2, #3
	mov	r7, #5
	stmia	sp, {r1, r2, r3, r7}
	mov	r3, #6
	str	r3, [sp, #16]
	b	.L2654
.L2662:
	mov	r1, #0
	mov	r2, r1
.L2653:
	cmp	r0, #256
	strls	r2, [sp, r1]
	movhi	r3, r2, asl #1
	add	r2, r2, #1
	strhi	r3, [sp, r1]
	cmp	r2, #5
	add	r1, r1, #4
	bne	.L2653
.L2654:
	ldr	r3, .L2665+4
	ldr	r2, [r3, #492]
	movw	r3, #63999
.L2659:
	ldrb	r1, [r2, r3]	@ zero_extendqisi2
	cmp	r1, #0
	addne	r3, r3, #1
	movne	r0, r3, asl #2
	bne	.L2658
.L2657:
	sub	r3, r3, #1
	cmp	r3, #4096
	bne	.L2659
	mov	r0, r0, asl #9
.L2658:
	mov	r1, r5
	ldr	r5, .L2665+4
	mov	r2, sp
	mov	r7, #0
	bl	write_idblock
	strb	r7, [r5, #480]
	ldr	r0, [r5, #484]
	bl	ftl_free
	str	r7, [r5, #484]
	b	.L2660
.L2652:
	ldr	r7, .L2665+4
	ldr	r3, [r7, #488]
	cmp	r3, r4
	beq	.L2660
	mov	r8, #0
	cmp	r5, r8
	strb	r8, [r7, #480]
	beq	.L2661
	mov	r0, r5
	bl	ftl_free
.L2661:
	str	r8, [r7, #484]
.L2660:
	ldr	r3, .L2665+4
	add	r4, r6, r4
	str	r4, [r3, #488]
.L2648:
	add	sp, sp, #32
	ldmfd	sp!, {r4, r5, r6, r7, r8, pc}
.L2666:
	.align	2
.L2665:
	.word	-52655045
	.word	.LANCHOR4
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
	mov	r6, r3
	sub	r3, r1, #64
	mov	r4, r1
	cmp	r3, #1984
	mov	r5, r2
	mov	r7, r0
	movcs	r3, #0
	movcc	r3, #1
	cmp	r0, #0
	movne	r3, #0
	cmp	r3, #0
	beq	.L2668
	mov	r0, r1
	mov	r1, r2
	mov	r2, r6
	bl	write_loader_lba
.L2668:
	mov	r0, r7
	mov	r1, r4
	mov	r2, r5
	mov	r3, r6
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
	ldr	r3, .L2785
	stmfd	sp!, {r4, r5, r6, lr}
	.save {r4, r5, r6, lr}
	cmp	r1, r3
	.pad #528
	sub	sp, sp, #528
	mov	r5, r1
	mov	r4, r2
	beq	.L2678
	bhi	.L2687
	ldr	r3, .L2785+4
	cmp	r1, r3
	beq	.L2675
	bhi	.L2688
	sub	r3, r3, #125
	cmp	r1, r3
	beq	.L2672
	bhi	.L2689
	sub	r3, r3, #237
	cmp	r1, r3
	bne	.L2731
	b	.L2781
.L2689:
	ldr	r3, .L2785+8
	cmp	r1, r3
	beq	.L2673
	add	r3, r3, #1
	cmp	r1, r3
	bne	.L2731
	b	.L2782
.L2688:
	ldr	r3, .L2785+12
	cmp	r1, r3
	beq	.L2677
	bhi	.L2690
	ldr	r3, .L2785+16
	cmp	r1, r3
	bne	.L2731
	b	.L2783
.L2690:
	ldr	r3, .L2785+20
	cmp	r1, r3
	beq	.L2677
	add	r3, r3, #10
	cmp	r1, r3
	bne	.L2731
	b	.L2677
.L2687:
	ldr	r3, .L2785+24
	cmp	r1, r3
	beq	.L2683
	bhi	.L2691
	sub	r3, r3, #78
	cmp	r1, r3
	beq	.L2680
	bcc	.L2679
	add	r3, r3, #21
	cmp	r1, r3
	beq	.L2681
	add	r3, r3, #56
	cmp	r1, r3
	bne	.L2731
	b	.L2784
.L2691:
	ldr	r3, .L2785+28
	cmp	r1, r3
	beq	.L2686
	bhi	.L2692
	sub	r3, r3, #956
	sub	r3, r3, #1
	cmp	r1, r3
	beq	.L2684
	add	r3, r3, #956
	cmp	r1, r3
	bne	.L2731
	b	.L2685
.L2692:
	ldr	r3, .L2785+32
	cmp	r1, r3
	beq	.L2685
	add	r3, r3, #1
	cmp	r1, r3
	bne	.L2731
	b	.L2686
.L2679:
	ldr	r0, .L2785+36
	bl	printk
	mov	r0, #4096
	bl	ftl_malloc
	subs	r5, r0, #0
	beq	.L2738
	mov	r1, r4
	mov	r2, #512
	bl	rk_copy_from_user
	cmp	r0, #0
	bne	.L2780
.L2693:
	ldr	r0, .L2785+40
	ldmia	r5, {r1, r2}
	bl	printk
	ldr	r3, [r5, #4]
	cmp	r3, #8
	str	r3, [sp, #4]
	bhi	.L2702
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
	beq	.L2766
	ldr	r0, .L2785+44
.L2779:
	bl	printk
	b	.L2702
.L2680:
	ldr	r0, .L2785+48
	bl	printk
	mov	r0, #4096
	bl	ftl_malloc
	subs	r5, r0, #0
	beq	.L2738
	mov	r1, r4
	mov	r2, #4096
	bl	rk_copy_from_user
	cmp	r0, #0
	bne	.L2780
.L2697:
	ldr	r4, .L2785+52
	ldr	r0, .L2785+56
	ldmia	r5, {r1, r2}
	bl	printk
	ldr	r3, [r4, #492]
	cmp	r3, #0
	bne	.L2698
	mov	r0, #260096
	bl	ftl_malloc
	cmp	r0, #0
	str	r0, [r4, #492]
	beq	.L2702
.L2699:
	mov	r1, #260096
	bl	__memzero
.L2698:
	ldr	r2, [r5, #4]
	movw	r3, #4088
	cmp	r2, r3
	bhi	.L2702
	ldr	r3, [r5, #0]
	cmp	r3, #251904
	bhi	.L2702
	ldr	r1, .L2785+52
	ldr	r0, [r1, #492]
	add	r1, r5, #8
	add	r0, r0, r3
	bl	memcpy
.L2766:
	mov	r0, r5
	bl	ftl_free
.L2767:
	mov	r4, #0
	b	.L2696
.L2784:
	ldr	r0, .L2785+60
	bl	printk
	mov	r0, #4096
	bl	ftl_malloc
	subs	r5, r0, #0
	beq	.L2738
	mov	r1, r4
	mov	r2, #28
	bl	rk_copy_from_user
	cmp	r0, #0
	beq	.L2701
.L2780:
	ldr	r0, .L2785+64
	b	.L2779
.L2701:
	ldmia	r5, {r1, r2}
	ldr	r0, .L2785+68
	bl	printk
	ldr	r1, [r5, #0]
	cmp	r1, #256000
	bhi	.L2702
	ldr	r4, .L2785+52
	ldr	r0, [r4, #492]
	cmp	r0, #0
	beq	.L2702
.L2703:
	bl	CRC_32
	ldr	r3, [r5, #4]
	cmp	r3, r0
	beq	.L2704
	mov	r0, r5
	bl	ftl_free
	b	.L2744
.L2704:
	bl	rknand_device_lock
	ldr	r1, [r4, #492]
	add	r2, r5, #8
	ldr	r0, [r5, #0]
	bl	write_idblock
	mov	r6, #0
	bl	rknand_device_unlock
	ldr	r0, [r4, #492]
	bl	ftl_free
	str	r6, [r4, #492]
	mov	r0, r5
	mov	r4, r6
	bl	ftl_free
	b	.L2696
.L2702:
	mov	r0, r5
	b	.L2770
.L2681:
	ldr	r0, .L2785+72
	bl	printk
	mov	r0, #4096
	bl	ftl_malloc
	subs	r5, r0, #0
	beq	.L2738
	bl	ftl_read_flash_info
	mov	r0, r4
	mov	r1, r5
	mov	r2, #11
	b	.L2775
.L2678:
	ldr	r0, .L2785+76
	bl	printk
	bl	nand_blk_add_whole_disk
	mov	r0, #4096
	bl	ftl_malloc
	subs	r5, r0, #0
	beq	.L2738
	mov	r1, #0
	mov	r2, #64
	bl	ftl_memset
	mov	r0, r4
	mov	r1, r5
	mov	r2, #64
	b	.L2775
.L2683:
	ldr	r0, .L2785+80
	bl	printk
	ldr	r3, .L2785+52
	add	r1, sp, #528
	mov	r0, r4
	mov	r2, #4
	ldr	r3, [r3, #496]
	ldr	r3, [r3, #20]
	str	r3, [r1, #-524]!
	b	.L2771
.L2684:
	ldr	r0, .L2785+84
	bl	printk
	mov	r0, #4096
	bl	ftl_malloc
	subs	r5, r0, #0
	beq	.L2738
	bl	rknand_device_lock
	mov	r1, #264
	mov	r2, #2
	mov	r3, r5
	mov	r0, #16
	bl	ftl_read
	bl	rknand_device_unlock
	mov	r0, r4
	mov	r1, r5
	mov	r2, #1024
.L2775:
	bl	rk_copy_to_user
	subs	r4, r0, #0
	mov	r0, r5
	beq	.L2707
.L2770:
	bl	ftl_free
	b	.L2761
.L2707:
	bl	ftl_free
	b	.L2696
.L2672:
	ldr	r0, .L2785+88
	bl	printk
	add	r0, sp, #8
	mov	r1, r4
	mov	r2, #520
	bl	rk_copy_from_user
	subs	r5, r0, #0
	bne	.L2772
.L2708:
	ldr	r2, [sp, #8]
	ldr	r3, .L2785+92
	cmp	r2, r3
	bne	.L2763
	ldr	r3, [sp, #12]
	cmp	r3, #512
	bhi	.L2763
	ldr	r6, .L2785+52
	mov	r2, #512
	add	r0, sp, #8
	ldr	r1, [r6, #496]
	bl	memcpy
	ldr	r2, [r6, #500]
	ldr	r3, .L2785+96
	cmp	r2, r3
	beq	.L2709
	add	r0, sp, #72
	mov	r1, #128
	str	r5, [sp, #16]
	str	r5, [sp, #20]
	bl	__memzero
.L2709:
	add	r0, sp, #264
	mov	r1, #256
	mov	r3, #0
	str	r3, [sp, #24]
	bl	__memzero
	b	.L2778
.L2673:
	ldr	r0, .L2785+100
	bl	printk
	add	r0, sp, #8
	mov	r1, r4
	mov	r2, #520
	bl	rk_copy_from_user
	cmp	r0, #0
	bne	.L2772
.L2710:
	ldr	r2, [sp, #8]
	ldr	r3, .L2785+92
	cmp	r2, r3
	bne	.L2763
	ldr	r3, [sp, #12]
	cmp	r3, #512
	bhi	.L2763
	ldr	r4, .L2785+52
	ldr	r3, .L2785+96
	ldr	r2, [r4, #500]
	cmp	r2, r3
	bne	.L2744
	ldr	r3, [sp, #20]
	sub	r2, r3, #1
	cmp	r2, #127
	mvnhi	r4, #2
	bhi	.L2670
	ldr	r0, [r4, #496]
	add	r1, sp, #72
	str	r3, [r0, #12]
	add	r0, r0, #64
	ldr	r2, [sp, #20]
	bl	memcpy
	mov	r0, #1
	ldr	r1, [r4, #496]
	b	.L2768
.L2783:
	ldr	r0, .L2785+104
	bl	printk
	add	r0, sp, #8
	mov	r1, r4
	mov	r2, #520
	bl	rk_copy_from_user
	cmp	r0, #0
	bne	.L2772
.L2711:
	ldr	r2, [sp, #8]
	ldr	r3, .L2785+108
	cmp	r2, r3
	bne	.L2763
	ldr	r3, [sp, #12]
	cmp	r3, #512
	bhi	.L2763
	ldr	r5, .L2785+52
	ldr	r4, [r5, #504]
	cmp	r4, #0
	beq	.L2670
	ldr	r3, [r5, #508]
	ldr	r2, .L2785+112
	ldr	r1, [r3, #0]
	cmp	r1, r2
	beq	.L2712
	str	r2, [r3, #0]
	mov	r2, #504
	ldr	r3, [r5, #508]
	str	r2, [r3, #4]
	mov	r2, #0
	str	r2, [r3, #8]
	str	r2, [r3, #12]
.L2712:
	ldr	r1, [r5, #508]
	mov	r4, #0
	mov	r0, r4
	str	r4, [r1, #16]
	bl	StorageSysDataStore
	ldr	r3, [r5, #496]
	ldr	r2, .L2785+92
	ldr	r5, .L2785+52
	ldr	r1, [r3, #0]
	cmp	r1, r2
	strne	r2, [r3, #0]
	ldr	r0, [r5, #496]
	mov	r1, #128
	ldrne	r3, .L2785+52
	movne	r2, #504
	ldrne	r3, [r3, #496]
	stmneib	r3, {r2, r4}
	mov	r4, #0
	str	r4, [r0, #12]
	add	r0, r0, #64
	bl	__memzero
	mov	r0, #1
	ldr	r1, [r5, #496]
	bl	StorageSysDataStore
	str	r4, [r5, #504]
	str	r4, [r5, #500]
	b	.L2696
.L2675:
	ldr	r0, .L2785+116
	bl	printk
	mov	r1, r4
	add	r0, sp, #8
	mov	r2, #520
	bl	rk_copy_from_user
	subs	r4, r0, #0
	bne	.L2772
.L2714:
	ldr	r2, [sp, #8]
	ldr	r3, .L2785+120
	cmp	r2, r3
	bne	.L2763
	ldr	r3, [sp, #12]
	cmp	r3, #512
	bhi	.L2763
	ldr	r5, .L2785+52
	ldr	r3, [r5, #504]
	cmp	r3, #1
	beq	.L2670
	ldr	r3, [r5, #508]
	mov	r0, #0
	ldr	r2, .L2785+112
	ldr	r1, [r3, #0]
	cmp	r1, r2
	strne	r2, [r3, #0]
	ldr	r1, [r5, #508]
	ldrne	r3, [r5, #508]
	movne	r2, #504
	strne	r4, [r3, #12]
	stmneib	r3, {r2, r4}
	mov	r3, #1
	str	r3, [r1, #16]
	bl	StorageSysDataStore
	ldr	r3, [r5, #496]
	ldr	r2, .L2785+92
	ldr	r1, [r3, #0]
	cmp	r1, r2
	beq	.L2716
	str	r2, [r3, #0]
	mov	r0, #504
	ldr	r3, .L2785+52
	mov	r2, #0
	ldr	r3, [r3, #496]
	stmib	r3, {r0, r2}
.L2716:
	ldr	r5, .L2785+52
	mov	r1, #128
	mov	r4, #0
	ldr	r0, [r5, #496]
	str	r4, [r0, #12]
	add	r0, r0, #64
	bl	__memzero
	mov	r0, #1
	ldr	r1, [r5, #496]
	bl	StorageSysDataStore
	mov	r3, #1
	str	r3, [r5, #504]
	b	.L2696
.L2782:
	ldr	r0, .L2785+124
	bl	printk
	add	r0, sp, #8
	mov	r1, r4
	mov	r2, #520
	bl	rk_copy_from_user
	cmp	r0, #0
	bne	.L2772
.L2717:
	ldr	r2, [sp, #8]
	ldr	r3, .L2785+128
	cmp	r2, r3
	bne	.L2763
	ldr	r2, [sp, #12]
	cmp	r2, #512
	addls	r0, sp, #16
	ldrls	r1, .L2785+132
	bls	.L2777
	b	.L2763
.L2677:
	ldr	r3, .L2785+20
	cmp	r5, r3
	ldreq	r0, .L2785+136
	beq	.L2764
	ldr	r3, .L2785+140
	cmp	r5, r3
	ldreq	r0, .L2785+144
	ldrne	r0, .L2785+148
.L2764:
	bl	printk
	add	r0, sp, #8
	mov	r1, r4
	mov	r2, #520
	bl	rk_copy_from_user
	cmp	r0, #0
	bne	.L2772
.L2721:
	ldr	r2, [sp, #8]
	ldr	r3, .L2785+152
	cmp	r2, r3
	bne	.L2761
	ldr	r3, .L2785+140
	ldr	r6, .L2785+52
	cmp	r5, r3
	bne	.L2722
	ldr	r3, [r6, #496]
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
	b	.L2670
.L2722:
	ldr	r3, [r6, #1024]
	cmp	r3, #10
	bhi	.L2761
	ldr	r3, [r6, #496]
	ldr	r1, [sp, #12]
	ldr	r2, [r3, #24]
	cmp	r2, r1
	beq	.L2723
	cmp	r2, #0
	beq	.L2723
	ldr	r0, .L2785+156
	bl	printk
	ldr	r3, [r6, #1024]
	add	r3, r3, #1
	str	r3, [r6, #1024]
	b	.L2761
.L2723:
	ldr	r0, .L2785+52
	mov	r2, #0
	str	r2, [r0, #1024]
	ldr	r0, .L2785+20
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
	b	.L2696
.L2685:
	ldr	r0, .L2785+160
	bl	printk
	add	r0, sp, #8
	mov	r1, r4
	mov	r2, #520
	bl	rk_copy_from_user
	cmp	r0, #0
	bne	.L2772
.L2726:
	ldr	r2, [sp, #8]
	ldr	r3, .L2785+164
	cmp	r2, r3
	bne	.L2763
	ldr	r2, [sp, #12]
	cmp	r2, #504
	bhi	.L2763
	ldr	r3, .L2785+168
	add	r0, sp, #16
	cmp	r5, r3
	ldr	r3, .L2785+52
	ldreq	r1, [r3, #1028]
	ldrne	r1, [r3, #1032]
	add	r1, r1, #8
.L2777:
	bl	memcpy
.L2778:
	add	r1, sp, #8
	mov	r0, r4
	mov	r2, #520
.L2771:
	bl	rk_copy_to_user
	subs	r4, r0, #0
	bne	.L2761
	b	.L2696
.L2686:
	ldr	r0, .L2785+172
	bl	printk
	add	r0, sp, #8
	mov	r1, r4
	mov	r2, #520
	bl	rk_copy_from_user
	cmp	r0, #0
	beq	.L2729
.L2772:
	ldr	r0, .L2785+64
	bl	printk
	b	.L2761
.L2729:
	ldr	r2, [sp, #8]
	ldr	r3, .L2785+164
	cmp	r2, r3
	bne	.L2763
	ldr	r2, [sp, #12]
	cmp	r2, #504
	bhi	.L2763
	ldr	r3, .L2785+28
	add	r2, r2, #8
	ldr	r4, .L2785+52
	cmp	r5, r3
	bne	.L2730
	add	r1, sp, #8
	ldr	r0, [r4, #1028]
	bl	memcpy
	ldr	r1, [r4, #1028]
	mov	r0, #2
.L2768:
	bl	StorageSysDataStore
	mov	r4, r0
	b	.L2696
.L2730:
	add	r1, sp, #8
	ldr	r0, [r4, #1032]
	bl	memcpy
	mov	r0, #3
	ldr	r1, [r4, #1032]
	b	.L2768
.L2781:
	bl	rknand_dev_flush
	b	.L2767
.L2763:
	mvn	r4, #0
.L2696:
	mov	r1, r4
	ldr	r0, .L2785+176
	bl	printk
	b	.L2670
.L2731:
	mvn	r4, #21
	b	.L2670
.L2738:
	mvn	r4, #11
	b	.L2670
.L2744:
	mvn	r4, #1
	b	.L2670
.L2761:
	mvn	r4, #13
.L2670:
	mov	r0, r4
	add	sp, sp, #528
	ldmfd	sp!, {r4, r5, r6, pc}
.L2786:
	.align	2
.L2785:
	.word	1074033155
	.word	1074029694
	.word	1074029570
	.word	1074031656
	.word	1074029695
	.word	1074031666
	.word	1074033235
	.word	1074034193
	.word	1074034194
	.word	.LC178
	.word	.LC180
	.word	.LC181
	.word	.LC182
	.word	.LANCHOR4
	.word	.LC183
	.word	.LC184
	.word	.LC179
	.word	.LC185
	.word	.LC186
	.word	.LC187
	.word	.LC188
	.word	.LC189
	.word	.LC190
	.word	1263358532
	.word	-1067903959
	.word	.LC191
	.word	.LC192
	.word	1112753220
	.word	1146313043
	.word	.LC193
	.word	1112755781
	.word	.LC194
	.word	1094995539
	.word	.LANCHOR4+512
	.word	.LC195
	.word	1074031676
	.word	.LC196
	.word	.LC197
	.word	1280262987
	.word	.LC198
	.word	.LC199
	.word	1145980246
	.word	1074034192
	.word	.LC200
	.word	.LC201
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
	ldr	r4, .L2790
	mov	r6, #0
	mvn	r3, #0
	add	r0, r4, #512
	ldr	r5, [r4, #472]
	str	r3, [r4, #488]
	add	r1, r5, #1536
	add	r3, r5, #512
	strb	r6, [r4, #480]
	str	r3, [r4, #496]
	add	r3, r5, #1024
	str	r6, [r4, #484]
	str	r3, [r4, #1028]
	str	r6, [r4, #492]
	str	r5, [r4, #508]
	str	r1, [r4, #1032]
	bl	memcpy
	ldr	r7, [r5, #508]
	ldr	r3, [r5, #16]
	cmp	r7, r6
	str	r6, [r4, #500]
	str	r6, [r4, #1024]
	str	r3, [r4, #504]
	beq	.L2788
	mov	r0, r5
	mov	r1, #508
	bl	JSHash
	cmp	r7, r0
	beq	.L2788
	str	r6, [r5, #16]
	ldr	r0, .L2790+4
	str	r6, [r4, #504]
	bl	printk
.L2788:
	ldr	r3, [r4, #504]
	mov	r0, #2
	ldr	r4, .L2790
	cmp	r3, #0
	ldrne	r2, .L2790+8
	ldrne	r3, .L2790
	ldr	r1, [r4, #1028]
	strne	r2, [r3, #500]
	bl	StorageSysDataLoad
	ldr	r1, [r4, #1032]
	mov	r0, #3
	bl	StorageSysDataLoad
	ldmfd	sp!, {r3, r4, r5, r6, r7, lr}
	b	rknand_sys_storage_init
.L2791:
	.align	2
.L2790:
	.word	.LANCHOR4
	.word	.LC202
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
	bl	ftl_malloc
	ldr	r7, .L2801
	cmp	r0, #0
	str	r0, [r7, #1036]
	beq	.L2799
	ldr	sl, .L2801+4
	mov	r6, #0
	mov	r4, r6
	mov	r5, r6
	movw	r8, #65532
.L2797:
	ldr	r0, [r7, #1036]
	mov	r1, r5, asl #7
	mov	r2, #0
	bl	rk_ftl_vendor_ops.constprop.27
	cmp	r0, #0
	bne	.L2795
	ldr	r3, [r7, #1036]
	ldr	r0, .L2801+8
	ldr	r1, [r3, #0]
	ldr	r2, [r3, r8]
	ldr	r3, [r3, #4]
	bl	printk
	ldr	r0, [r7, #1036]
	ldr	r3, [r0, #0]
	cmp	r3, sl
	bne	.L2796
	ldr	r3, [r0, r8]
	ldr	r2, [r0, #4]
	cmp	r3, r2
	bne	.L2796
	cmp	r4, r3
	movcc	r6, r5
	movcc	r4, r3
.L2796:
	add	r5, r5, #1
	cmp	r5, #2
	bne	.L2797
	cmp	r4, #0
	beq	.L2798
	mov	r1, r6, asl #7
	mov	r2, #0
	bl	rk_ftl_vendor_ops.constprop.27
	cmp	r0, #0
	ldmeqfd	sp!, {r3, r4, r5, r6, r7, r8, sl, pc}
	b	.L2795
.L2798:
	mov	r1, #65536
	bl	__memzero
	ldr	r3, .L2801
	ldr	r1, .L2801+4
	mov	r2, #1
	mov	r0, r4
	ldr	r3, [r3, #1036]
	stmia	r3, {r1, r2}
	movw	r1, #65532
	str	r2, [r3, r1]
	ldr	r2, .L2801+12
	strh	r4, [r3, #12]	@ movhi
	strh	r2, [r3, #14]	@ movhi
	ldmfd	sp!, {r3, r4, r5, r6, r7, r8, sl, pc}
.L2795:
	ldr	r4, .L2801
	ldr	r0, [r4, #1036]
	bl	kfree
	mov	r3, #0
	mvn	r0, #0
	str	r3, [r4, #1036]
	ldmfd	sp!, {r3, r4, r5, r6, r7, r8, sl, pc}
.L2799:
	mvn	r0, #11
	ldmfd	sp!, {r3, r4, r5, r6, r7, r8, sl, pc}
.L2802:
	.align	2
.L2801:
	.word	.LANCHOR4
	.word	1380668996
	.word	.LC203
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
	ldr	r1, .L2810
	stmfd	sp!, {r4, r5, r6, lr}
	.save {r4, r5, r6, lr}
	ldr	ip, [r1, #1036]
	cmp	ip, #0
	ldrneh	r4, [ip, #10]
	movne	r5, ip
	movne	r1, #0
	bne	.L2805
	b	.L2809
.L2807:
	ldrh	r6, [r5, #16]
	add	r5, r5, #8
	cmp	r6, r3
	bne	.L2806
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
.L2806:
	add	r1, r1, #1
.L2805:
	cmp	r1, r4
	bcc	.L2807
	mvn	r0, #0
	ldmfd	sp!, {r4, r5, r6, pc}
.L2809:
	mvn	r0, #0
	ldmfd	sp!, {r4, r5, r6, pc}
.L2811:
	.align	2
.L2810:
	.word	.LANCHOR4
	.fnend
	.size	rk_ftl_vendor_read, .-rk_ftl_vendor_read
	.align	2
	.global	rk_ftl_vendor_write
	.type	rk_ftl_vendor_write, %function
rk_ftl_vendor_write:
	.fnstart
	@ args = 0, pretend = 0, frame = 16
	@ frame_needed = 0, uses_anonymous_args = 0
	ldr	r3, .L2829
	stmfd	sp!, {r4, r5, r6, r7, r8, r9, sl, fp, lr}
	.save {r4, r5, r6, r7, r8, r9, sl, fp, lr}
	mov	sl, r0
	ldr	r4, [r3, #1036]
	.pad #20
	sub	sp, sp, #20
	mov	r9, r1
	mov	r5, r2
	cmp	r4, #0
	beq	.L2825
	add	r8, r2, #63
	ldrh	r2, [r4, #8]
	ldrh	r3, [r4, #10]
	bic	r8, r8, #63
	mov	r7, #0
	str	r2, [sp, #8]
	b	.L2814
.L2821:
	add	r2, r7, #2
	mov	r2, r2, asl #3
	add	r6, r4, r2
	ldrh	r2, [r4, r2]
	cmp	r2, sl
	bne	.L2815
	ldrh	r2, [r6, #4]
	add	r2, r2, #63
	bic	r2, r2, #63
	str	r2, [sp, #12]
	cmp	r5, r2
	bls	.L2816
	ldrh	r2, [r4, #14]
	cmp	r2, r8
	bcc	.L2825
	sub	r3, r3, #1
	mov	fp, r8
	ldrh	r6, [r6, #2]
	mov	r8, r3
	b	.L2817
.L2818:
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
.L2817:
	cmp	r7, r8
	add	r2, r7, #2
	bcc	.L2818
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
	b	.L2828
.L2816:
	ldrh	r0, [r6, #2]
	mov	r1, r9
	mov	r2, r5
	add	r0, r0, #1024
	add	r0, r4, r0
	bl	memcpy
	strh	r5, [r6, #4]	@ movhi
	b	.L2828
.L2815:
	add	r7, r7, #1
.L2814:
	cmp	r7, r3
	bcc	.L2821
	ldrh	r2, [r4, #14]
	cmp	r2, r8
	bcc	.L2825
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
.L2828:
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
	bl	rk_ftl_vendor_ops.constprop.27
	mov	r0, #0
	b	.L2813
.L2825:
	mvn	r0, #0
.L2813:
	add	sp, sp, #20
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, sl, fp, pc}
.L2830:
	.align	2
.L2829:
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
	beq	.L2832
	ldr	r3, .L2848
	cmp	r6, r3
	beq	.L2834
	add	r3, r3, #1
	cmp	r6, r3
	bne	.L2845
	b	.L2847
.L2834:
	mov	r1, r5
	mov	r2, #8
	bl	rk_copy_from_user
	cmp	r0, #0
	bne	.L2845
	ldr	r2, [r4, #0]
	ldr	r3, .L2848+4
	cmp	r2, r3
	bne	.L2844
	ldrh	r0, [r4, #4]
	add	r1, r4, #8
	ldrh	r2, [r4, #6]
	bl	rk_ftl_vendor_read
	cmn	r0, #1
	beq	.L2840
	uxth	r2, r0
	mov	r1, r4
	strh	r2, [r4, #6]	@ movhi
	mov	r0, r5
	add	r2, r2, #8
	bl	rk_copy_to_user
	cmp	r0, #0
	moveq	r5, r0
	mvnne	r5, #13
	b	.L2833
.L2847:
	mov	r1, r5
	mov	r2, #8
	bl	rk_copy_from_user
	cmp	r0, #0
	bne	.L2845
	ldr	r2, [r4, #0]
	ldr	r3, .L2848+4
	cmp	r2, r3
	bne	.L2844
	ldrh	r2, [r4, #6]
	movw	r3, #4087
	cmp	r2, r3
	bhi	.L2844
	mov	r0, r4
	mov	r1, r5
	add	r2, r2, #8
	bl	rk_copy_from_user
	cmp	r0, #0
	bne	.L2845
	ldrh	r0, [r4, #4]
	add	r1, r4, #8
	ldrh	r2, [r4, #6]
	bl	rk_ftl_vendor_write
.L2840:
	mov	r5, r0
	b	.L2833
.L2844:
	mvn	r5, #0
	b	.L2833
.L2845:
	mvn	r5, #13
.L2833:
	mov	r0, r4
	bl	kfree
.L2832:
	mov	r0, r5
	ldmfd	sp!, {r4, r5, r6, pc}
.L2849:
	.align	2
.L2848:
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
	.type	__func__.14269, %object
	.size	__func__.14269, 11
__func__.14269:
	.ascii	"FtlMemInit\000"
.LC0:
	.byte	60
	.byte	40
	.byte	24
	.byte	16
	.type	__func__.15064, %object
	.size	__func__.15064, 16
__func__.15064:
	.ascii	"FtlScanAllBlock\000"
	.type	__func__.15045, %object
	.size	__func__.15045, 17
__func__.15045:
	.ascii	"FtlDumpBlockInfo\000"
	.type	__func__.15312, %object
	.size	__func__.15312, 21
__func__.15312:
	.ascii	"FtlVpcCheckAndModify\000"
	.type	__func__.14342, %object
	.size	__func__.14342, 8
__func__.14342:
	.ascii	"FtlInit\000"
	.type	__func__.15010, %object
	.size	__func__.15010, 12
__func__.15010:
	.ascii	"FtlCheckVpc\000"
	.section	.rodata.str1.1,"aMS",%progbits,1
.LC1:
	.ascii	"FlashEraseBlocks pageAddr error %x\012\000"
.LC2:
	.ascii	"No.%d FLASH ID:%x %x %x %x %x %x\012\000"
.LC3:
	.ascii	"phyBlk = 0x%x die = %d block_in_die = 0x%x 0x%8x\012"
	.ascii	"\000"
.LC4:
	.ascii	"FtlFreeSysBlkQueueOut free count = %d\012\000"
.LC5:
	.ascii	"FtlFreeSysBlkQueueOut = %x, free count = %d, error\012"
	.ascii	"\000"
.LC6:
	.ascii	"FtlFreeSysBlkQueueOut = %x, free count = %d\012\000"
.LC7:
	.ascii	"FLASH INFO:\012\000"
.LC8:
	.ascii	"FLASH ID: %x\012\000"
.LC9:
	.ascii	"Device Capacity: %d MB\012\000"
.LC10:
	.ascii	"FMWAIT: %x %x %x %x\012\000"
.LC11:
	.ascii	"FTL INFO:\012\000"
.LC12:
	.ascii	"g_MaxLpn = 0x%x\012\000"
.LC13:
	.ascii	"g_VaildLpn = 0x%x\012\000"
.LC14:
	.ascii	"read_page_count = 0x%x\012\000"
.LC15:
	.ascii	"discard_page_count = 0x%x\012\000"
.LC16:
	.ascii	"write_page_count = 0x%x\012\000"
.LC17:
	.ascii	"cache_write_count = 0x%x\012\000"
.LC18:
	.ascii	"l2p_write_count = 0x%x\012\000"
.LC19:
	.ascii	"gc_page_count = 0x%x\012\000"
.LC20:
	.ascii	"totle_write = %d MB\012\000"
.LC21:
	.ascii	"totle_read = %d MB\012\000"
.LC22:
	.ascii	"GSV = 0x%x\012\000"
.LC23:
	.ascii	"GDV = 0x%x\012\000"
.LC24:
	.ascii	"bad blk num = %d %d\012\000"
.LC25:
	.ascii	"free_superblocks = 0x%x\012\000"
.LC26:
	.ascii	"mlc_EC = 0x%x\012\000"
.LC27:
	.ascii	"slc_EC = 0x%x\012\000"
.LC28:
	.ascii	"avg_EC = 0x%x\012\000"
.LC29:
	.ascii	"sys_EC = 0x%x\012\000"
.LC30:
	.ascii	"max_EC = 0x%x\012\000"
.LC31:
	.ascii	"min_EC = 0x%x\012\000"
.LC32:
	.ascii	"PLT = 0x%x\012\000"
.LC33:
	.ascii	"POT = 0x%x\012\000"
.LC34:
	.ascii	"MaxSector = 0x%x\012\000"
.LC35:
	.ascii	"init_sys_blks_pp = 0x%x\012\000"
.LC36:
	.ascii	"sys_blks_pp = 0x%x\012\000"
.LC37:
	.ascii	"free sysblock = 0x%x\012\000"
.LC38:
	.ascii	"data_blks_pp = 0x%x\012\000"
.LC39:
	.ascii	"data_op_blks_pp = 0x%x\012\000"
.LC40:
	.ascii	"max_data_blks = 0x%x\012\000"
.LC41:
	.ascii	"Sys.id = 0x%x\012\000"
.LC42:
	.ascii	"Bbt.id = 0x%x\012\000"
.LC43:
	.ascii	"ACT.page = 0x%x\012\000"
.LC44:
	.ascii	"ACT.plane = 0x%x\012\000"
.LC45:
	.ascii	"ACT.id = 0x%x\012\000"
.LC46:
	.ascii	"ACT.mode = 0x%x\012\000"
.LC47:
	.ascii	"ACT.a_pages = 0x%x\012\000"
.LC48:
	.ascii	"ACT VPC = 0x%x\012\000"
.LC49:
	.ascii	"BUF.page = 0x%x\012\000"
.LC50:
	.ascii	"BUF.plane = 0x%x\012\000"
.LC51:
	.ascii	"BUF.id = 0x%x\012\000"
.LC52:
	.ascii	"BUF.mode = 0x%x\012\000"
.LC53:
	.ascii	"BUF.a_pages = 0x%x\012\000"
.LC54:
	.ascii	"BUF VPC = 0x%x\012\000"
.LC55:
	.ascii	"TMP.page = 0x%x\012\000"
.LC56:
	.ascii	"TMP.plane = 0x%x\012\000"
.LC57:
	.ascii	"TMP.id = 0x%x\012\000"
.LC58:
	.ascii	"TMP.mode = 0x%x\012\000"
.LC59:
	.ascii	"TMP.a_pages = 0x%x\012\000"
.LC60:
	.ascii	"GC.page = 0x%x\012\000"
.LC61:
	.ascii	"GC.plane = 0x%x\012\000"
.LC62:
	.ascii	"GC.id = 0x%x\012\000"
.LC63:
	.ascii	"GC.mode = 0x%x\012\000"
.LC64:
	.ascii	"GC.a_pages = 0x%x\012\000"
.LC65:
	.ascii	"WR_CHK = 0x%x %x %x %x\012\000"
.LC66:
	.ascii	"Read Err = 0x%x\012\000"
.LC67:
	.ascii	"Prog Err = 0x%x\012\000"
.LC68:
	.ascii	"gc_free_blk_th= 0x%x\012\000"
.LC69:
	.ascii	"gc_merge_free_blk_th= 0x%x\012\000"
.LC70:
	.ascii	"gc_skip_write_count= 0x%x\012\000"
.LC71:
	.ascii	"gc_blk_index= 0x%x\012\000"
.LC72:
	.ascii	"free min EC= 0x%x\012\000"
.LC73:
	.ascii	"free max EC= 0x%x\012\000"
.LC74:
	.ascii	"GC__SB VPC = 0x%x\012\000"
.LC75:
	.ascii	"%d. [0x%x]=0x%x 0x%x  0x%x\012\000"
.LC76:
	.ascii	"free %d. [0x%x] 0x%x  0x%x\012\000"
.LC77:
	.ascii	"%s\012\000"
.LC78:
	.ascii	"FTL version: 5.0.50 20180905\000"
.LC79:
	.ascii	"swblk %x ,avg = %x max= %x vpc= %x,ec=%x ,max ec=%x"
	.ascii	"\012\000"
.LC80:
	.ascii	"FtlGcRefreshBlock  0x%x\012\000"
.LC81:
	.ascii	"decrement_vpc_count %x = %d\012\000"
.LC82:
	.ascii	"decrement_vpc_count %x = %d in free list\012\000"
.LC83:
	.ascii	"FtlGcMarkBadPhyBlk %d 0x%x\012\000"
.LC84:
	.ascii	"%s error allocating memory. return -1\012\000"
.LC85:
	.ascii	"%s %p:0x%x:\000"
.LC86:
	.ascii	"%x \000"
.LC87:
	.ascii	"\000"
.LC88:
	.ascii	"%d statReg->V6.mtrans_cnt=%d flReg.V6.page_num=%d\012"
	.ascii	"\000"
.LC89:
	.ascii	"nandc:\000"
.LC90:
	.ascii	"%d flReg.d32=%x %x\012\000"
.LC91:
	.ascii	"micron RR %d row=%x,count %d,status=%d\012\000"
.LC92:
	.ascii	"ECC:%d\012\000"
.LC93:
	.ascii	"sdr read ok %x ecc=%d\012\000"
.LC94:
	.ascii	"sync para %d\012\000"
.LC95:
	.ascii	"TOG mode Read error %x %x\012\000"
.LC96:
	.ascii	"read retry status %x %x %x\012\000"
.LC97:
	.ascii	"Read pageadd=%x  ecc=%x err=%x\012\000"
.LC98:
	.ascii	"data:\000"
.LC99:
	.ascii	"spare:\000"
.LC100:
	.ascii	"ReadRetry pageadd=%x ecc=%x err=%x\012\000"
.LC101:
	.ascii	"FtlGcScanTempBlkError ID %x %x!!!!!!!\012\000"
.LC102:
	.ascii	"...%s enter...\012\000"
.LC103:
	.ascii	"blk = %x vpc=%x mode = %x\012\000"
.LC104:
	.ascii	"mlc id = %x,%x addr= %x,spare= %x %x %x %x data=%x "
	.ascii	"%x\012\000"
.LC105:
	.ascii	"slc id = %x,%x addr= %x,spare= %x %x %x %x data=%x "
	.ascii	"%x\012\000"
.LC106:
	.ascii	"superBlkID = %x vpc=%x\012\000"
.LC107:
	.ascii	"flashmode = %x pagenum = %x %x\012\000"
.LC108:
	.ascii	"id = %x,%x addr= %x,spare= %x %x %x %x data=%x %x\012"
	.ascii	"\000"
.LC109:
	.ascii	"prog error: = %x\012\000"
.LC110:
	.ascii	"prog read error: = %x\012\000"
.LC111:
	.ascii	"prog read s error: = %x %x %x\012\000"
.LC112:
	.ascii	"prog read d error: = %x %x %x\012\000"
.LC113:
	.ascii	"FtlVpcTblFlush error = %x error count = %d\012\000"
.LC114:
	.ascii	"FtlBbmTblFlush id=%x,page=%x,previd=%x cnt=%d\012\000"
.LC115:
	.ascii	"FtlBbmTblFlush error:%x\012\000"
.LC116:
	.ascii	"FtlBbmTblFlush error = %x error count = %d\012\000"
.LC117:
	.ascii	"FtlGcFreeBadSuperBlk 0x%x\012\000"
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
	.ascii	"page map lost: %x %x\012\000"
.LC124:
	.ascii	"FtlMapWritePage error = %x\012\000"
.LC125:
	.ascii	"FtlMapWritePage error = %x error count = %d\012\000"
.LC126:
	.ascii	"FtlVendorPartRead refresh = %x phyAddr = %x\012\000"
.LC127:
	.ascii	"slc mode\000"
.LC128:
	.ascii	"no ect\000"
.LC129:
	.ascii	"FLFB:%d %d\012\000"
.LC130:
	.ascii	"phyBlk = %x,addr= %x,spare= %x %x %x %x data=%x %x\012"
	.ascii	"\000"
.LC131:
	.ascii	"Mblk:\000"
.LC132:
	.ascii	"L2P:\000"
.LC133:
	.ascii	"L2PC:\000"
.LC134:
	.ascii	"id = %x,%x addr= %x,spare= %x %x %x %x data = %x\012"
	.ascii	"\000"
.LC135:
	.ascii	":\000"
.LC136:
	.ascii	"BBT:\000"
.LC137:
	.ascii	"region_id = %x phyAddr = %x\012\000"
.LC138:
	.ascii	"map_ppn:\000"
.LC139:
	.ascii	"load_l2p_region refresh = %x phyAddr = %x\012\000"
.LC140:
	.ascii	"Ftlscanalldata = %x\012\000"
.LC141:
	.ascii	"scan lpa = %x ppa= %x\012\000"
.LC142:
	.ascii	"lba = %x,addr= %x,spare= %x %x %x %x data=%x %x\012"
	.ascii	"\000"
.LC143:
	.ascii	"RSB refresh addr %x\012\000"
.LC144:
	.ascii	"spuer block %x vpn is 0\012 \000"
.LC145:
	.ascii	"g_recovery_ppa %x ver %x\012 \000"
.LC146:
	.ascii	"FtlCheckVpc %x = %x  %x\012\000"
.LC147:
	.ascii	"GC des block %x done\012\000"
.LC148:
	.ascii	"%d GC datablk  = %x vpc %x %x\012\000"
.LC149:
	.ascii	"SWL %x, FSB = %x vpc= %x,ec=%x th=%x\012\000"
.LC150:
	.ascii	"g_gc_superblock_free %x %x %x %x %x\012\000"
.LC151:
	.ascii	"Ftlwrite decrement_vpc_count %x = %d\012\000"
.LC152:
	.ascii	"rk_ftl_de_init %x\012\000"
.LC153:
	.ascii	"fix power lost blk = %x vpc=%x\012\000"
.LC154:
	.ascii	"erase power lost blk = %x vpc=%x\012\000"
.LC155:
	.ascii	"...%s: no bad block mapping table, format device\012"
	.ascii	"\000"
.LC156:
	.ascii	"...%s FtlSysBlkInit error ,format device!\012\000"
.LC157:
	.ascii	"FtlWrite: lpa error:%x %x\012\000"
.LC158:
	.ascii	"vpc1\000"
.LC159:
	.ascii	"vpc2\000"
.LC160:
	.ascii	"FtlCheckVpc2 %x = %x  %x\012\000"
.LC161:
	.ascii	"free blk vpc error %x = %x  %x\012\000"
.LC162:
	.ascii	"otp error! %d\000"
.LC163:
	.ascii	"rr\000"
.LC164:
	.ascii	"FlashLoadPhyInfo fail %x!!\012\000"
.LC165:
	.ascii	"FtlInit %x\012\000"
.LC166:
	.ascii	"IdBlockReadData %x %x\012\000"
.LC167:
	.ascii	"IdBlockReadData %x %x ret= %x\012\000"
.LC168:
	.ascii	"IDBlockWriteData %x %x\012\000"
.LC169:
	.ascii	"IDBlockWriteData %x %x ret= %x\012\000"
.LC170:
	.ascii	"write_idblock fix data %x %x\012\000"
.LC171:
	.ascii	"idblk:\000"
.LC172:
	.ascii	"idb reverse %x %x\012\000"
.LC173:
	.ascii	"write_idblock totle_sec %x %x %x %x\012\000"
.LC174:
	.ascii	"write and check error:%d idb=%x,offset=%x,r=%x,w=%x"
	.ascii	"\012\000"
.LC175:
	.ascii	"write\000"
.LC176:
	.ascii	"read\000"
.LC177:
	.ascii	"write_idblock error %d\012\000"
.LC178:
	.ascii	"READ_SECTOR_IO\012\000"
.LC179:
	.ascii	"rk_copy_from_user error\012\000"
.LC180:
	.ascii	"READ_SECTOR_IO %x %x\012\000"
.LC181:
	.ascii	"rk_copy_to_user error\012\000"
.LC182:
	.ascii	"WRITE_SECTOR_IO\012\000"
.LC183:
	.ascii	"WRITE_SECTOR_IO %x %x\012\000"
.LC184:
	.ascii	"END_WRITE_SECTOR_IO\012\000"
.LC185:
	.ascii	"END_WRITE_SECTOR_IO %x %x\012\000"
.LC186:
	.ascii	"GET_FLASH_INFO_IO\012\000"
.LC187:
	.ascii	"GET_BAD_BLOCK_IO\012\000"
.LC188:
	.ascii	"GET_LOCK_FLAG_IO\012\000"
.LC189:
	.ascii	"GET_PUBLIC_KEY_IO\012\000"
.LC190:
	.ascii	"RKNAND_GET_DRM_KEY\012\000"
.LC191:
	.ascii	"RKNAND_STORE_DRM_KEY\012\000"
.LC192:
	.ascii	"RKNAND_DIASBLE_SECURE_BOOT\012\000"
.LC193:
	.ascii	"RKNAND_ENASBLE_SECURE_BOOT\012\000"
.LC194:
	.ascii	"RKNAND_GET_SN_SECTOR\012\000"
.LC195:
	.ascii	"RKNAND_LOADER_UNLOCK\012\000"
.LC196:
	.ascii	"RKNAND_LOADER_STATUS\012\000"
.LC197:
	.ascii	"RKNAND_LOADER_LOCK\012\000"
.LC198:
	.ascii	"LockKey not match %d\012\000"
.LC199:
	.ascii	"RKNAND_GET_VENDOR_SECTOR\012\000"
.LC200:
	.ascii	"RKNAND_STORE_VENDOR_SECTOR\012\000"
.LC201:
	.ascii	"return ret = %lx\012\000"
.LC202:
	.ascii	"secureBootEn check error\012\000"
.LC203:
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
	.size	NandFlashParaTbl, 2464
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
	.set	.LANCHOR4,. + 16368
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
	.space	3
	.type	g_nandc_version_data, %object
	.size	g_nandc_version_data, 4
g_nandc_version_data:
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
	.type	gBbtInfo, %object
	.size	gBbtInfo, 60
gBbtInfo:
	.space	60
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
	.type	gSysFreeQueue, %object
	.size	gSysFreeQueue, 2056
gSysFreeQueue:
	.space	2056
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
	.type	g_LowFormat, %object
	.size	g_LowFormat, 4
g_LowFormat:
	.space	4
	.type	gc_discard_updated, %object
	.size	gc_discard_updated, 4
gc_discard_updated:
	.space	4
	.type	g_ftl_nand_free_count, %object
	.size	g_ftl_nand_free_count, 4
g_ftl_nand_free_count:
	.space	4
	.type	last_cache_match_count, %object
	.size	last_cache_match_count, 4
last_cache_match_count:
	.space	4
	.type	check_valid_page_count_table, %object
	.size	check_valid_page_count_table, 8192
check_valid_page_count_table:
	.space	8192
	.type	gLoaderBootInfo, %object
	.size	gLoaderBootInfo, 4
gLoaderBootInfo:
	.space	4
	.type	RK29_NANDC1_REG_BASE, %object
	.size	RK29_NANDC1_REG_BASE, 4
RK29_NANDC1_REG_BASE:
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
