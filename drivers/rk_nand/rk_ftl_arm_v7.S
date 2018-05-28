/*
 * Copyright (c) 2016-2018, Fuzhou Rockchip Electronics Co., Ltd
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 * date: 2018-05-28
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
	.type	FlashGetReadRetryDefault.part.25, %function
FlashGetReadRetryDefault.part.25:
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
	.size	FlashGetReadRetryDefault.part.25, .-FlashGetReadRetryDefault.part.25
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
	cmp	r4, #74
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
	.word	.LANCHOR1+2764
	.word	.LANCHOR0+896
	.word	.LANCHOR1+2892
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
	.word	.LANCHOR1+2924
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
	addne	r3, r9, #2976
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
	ldrb	r2, [r3, #2901]	@ zero_extendqisi2
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
	.global	ftl_flash_de_init
	.type	ftl_flash_de_init, %function
ftl_flash_de_init:
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
	.size	ftl_flash_de_init, .-ftl_flash_de_init
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
	mov	r2, #0
	ldr	r4, .L270
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
	str	r1, [r4, #3776]
	movw	r1, #2049
	ubfx	r2, r2, #16, #4
	str	r2, [r4, #3780]
	ldr	r2, [r0, #352]
	cmp	r2, r1
	str	r2, [r4, #3784]
	moveq	r2, #8
	streq	r2, [r4, #3780]
	str	r3, [r0, #0]
	mov	r0, #40
	ldr	r3, [r4, #3012]
	str	r5, [r3, #336]
	bl	NandcTimeCfg
	ldr	r3, [r4, #3012]
	movw	r2, #8322
	mov	r0, #36864
	str	r2, [r3, #344]
	ldr	r2, .L270+4
	str	r2, [r3, #304]
	bl	ftl_malloc
	str	r5, [r4, #3816]
	str	r5, [r4, #3824]
	str	r0, [r4, #3788]
	str	r0, [r4, #3792]
	add	r0, r0, #32768
	str	r0, [r4, #3796]
	ldmfd	sp!, {r3, r4, r5, pc}
.L271:
	.align	2
.L270:
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
	bls	.L273
	movw	r3, #4226
	movw	r2, #8322
	cmp	r0, r3
	cmpne	r0, r2
	bne	.L274
.L273:
	ldr	r3, .L275
	ldr	r3, [r3, #3012]
	str	r0, [r3, #4]
.L274:
	ldr	r3, .L275+4
	ldrb	r0, [r3, #2913]	@ zero_extendqisi2
	b	NandcTimeCfg
.L276:
	.align	2
.L275:
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
	ldr	ip, .L278
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
.L279:
	.align	2
.L278:
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
	ldr	r3, .L285
	mov	r1, #1
	ldr	r2, [r3, #3012]
	str	r0, [r3, #3828]
	mov	r3, #0
	str	r1, [r2, #8]
	mov	r1, #16
	cmp	r0, r1
	bfi	r3, r1, #8, #8
	bfc	r3, #18, #1
	beq	.L284
.L281:
	cmp	r0, #24
	orreq	r3, r3, #16
	beq	.L282
	cmp	r0, #40
	orr	r3, r3, #262144
	orr	r3, r3, #16
	bne	.L282
.L284:
	bfc	r3, #4, #1
.L282:
	orr	r3, r3, #1
	str	r3, [r2, #12]
	bx	lr
.L286:
	.align	2
.L285:
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
	ldr	r3, .L288
	strb	r0, [r3, #3832]
	b	NandcBchSel
.L289:
	.align	2
.L288:
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
	ldr	r3, .L295
	stmfd	sp!, {r4, r5, r6, lr}
	.save {r4, r5, r6, lr}
	mov	r4, #0
	ldr	r2, [r3, #3012]
	ldr	r1, [r3, #3016]
	ldr	r5, [r3, #3032]
	ldr	r6, .L295+4
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
.L292:
	ldrb	r3, [r6, r4, asl #3]	@ zero_extendqisi2
	sub	r3, r3, #1
	uxtb	r3, r3
	cmp	r3, #253
	bhi	.L291
	uxtb	r0, r4
	bl	FlashReset
.L291:
	add	r4, r4, #1
	cmp	r4, #4
	bne	.L292
	ldr	r4, .L295
	ldrb	r3, [r4, #3773]	@ zero_extendqisi2
	cmp	r3, #0
	beq	.L293
	mov	r0, #1
	bl	NandcSetMode
	ldrb	r0, [r4, #3772]	@ zero_extendqisi2
	bl	FlashSetInterfaceMode
	ldrb	r0, [r4, #3772]	@ zero_extendqisi2
	bl	NandcSetMode
	ubfx	r0, r5, #8, #8
	bl	NandcSetDdrPara
.L293:
	ldr	r3, .L295
	ldr	r3, [r3, #3624]
	ldrb	r0, [r3, #20]	@ zero_extendqisi2
	ldmfd	sp!, {r4, r5, r6, lr}
	b	FlashBchSel
.L296:
	.align	2
.L295:
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
	bne	.L300
	mov	r0, r4
	ldmfd	sp!, {r4, lr}
	b	wait_for_nand_flash_ready
.L300:
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
	ldr	r5, .L321
	mov	r6, r0
	str	r1, [sp, #12]
	mov	r7, r2
	ldrb	r4, [r5, #852]	@ zero_extendqisi2
	cmp	r4, #0
	moveq	r9, r5
	beq	.L302
	mov	r1, r2
	bl	FlashEraseSLc2KBlocks
	b	.L303
.L311:
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
	bcs	.L305
	ldrb	r3, [r5, #3833]	@ zero_extendqisi2
	cmp	r3, #0
	add	r3, r9, r0, asl #4
	ldr	r3, [r3, #3636]
	moveq	sl, #0
	cmp	r3, #0
	beq	.L307
	uxtb	r0, r0
	bl	FlashWaitCmdDone
.L307:
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
	bne	.L309
	ldrb	r3, [r5, #928]	@ zero_extendqisi2
	cmp	r3, #0
	beq	.L309
	bl	flash_enter_slc_mode
	b	.L310
.L309:
	bl	flash_exit_slc_mode
.L310:
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
.L305:
	add	r4, r4, #1
.L302:
	cmp	r4, r7
	bcc	.L311
	ldr	r5, .L321
	mov	r4, #0
	ldr	r8, .L321+4
	ldr	r0, [r5, #3012]
	bl	NandcIqrWaitFlashReady
	b	.L312
.L314:
	uxtb	r0, r4
	bl	FlashWaitCmdDone
	ldr	r3, [sp, #12]
	cmp	r3, #1
	bne	.L313
	ldrb	r3, [r5, #928]	@ zero_extendqisi2
	cmp	r3, #0
	beq	.L313
	ldrb	r0, [r8, r4, asl #4]	@ zero_extendqisi2
	bl	flash_exit_slc_mode
.L313:
	add	r4, r4, #1
.L312:
	ldrb	r2, [r5, #3762]	@ zero_extendqisi2
	ldr	r3, .L321
	cmp	r4, r2
	bcc	.L314
	ldr	r0, [r3, #3836]
	cmp	r0, #0
	beq	.L303
	ldrb	r3, [r3, #2980]	@ zero_extendqisi2
	cmp	r3, #69
	moveq	r3, #0
	moveq	r2, r3
	bne	.L320
	b	.L315
.L316:
	add	r3, r3, #1
	str	r2, [r6, #-36]
.L315:
	cmp	r3, r7
	add	r6, r6, #36
	bne	.L316
.L320:
	mov	r0, #0
.L303:
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
	mov	r6, r1
	ldr	r1, .L343
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
	ldr	r3, [r1, #3780]
	cmp	r3, #3
	bls	.L331
	ldr	r3, [r5, #16]
	cmp	r9, #0
	cmpeq	ip, #0
	str	r3, [sp, #12]
	ldr	r3, [sp, #12]
	bfc	r3, #2, #1
	str	r3, [sp, #12]
	beq	.L332
	cmp	r6, #0
	beq	.L333
	ldr	r3, [r1, #3828]
	mov	r0, ip
	cmp	r3, #24
	movhi	r3, #128
	movls	r3, #64
	str	r3, [sp, #0]
	mov	r3, r8, lsr r2
	str	r3, [sp, #4]
	mov	r3, #0
	mov	r2, r3
	b	.L335
.L338:
	cmp	ip, #0
	mov	lr, r3, lsr #2
	add	r2, r2, #1
	ldrneh	fp, [r0, #2]
	mvneq	fp, #0
	ldrneh	sl, [r0], #4
	ldreq	sl, [r1, #3796]
	orrne	sl, sl, fp, asl #16
	ldrne	fp, [r1, #3796]
	streq	fp, [sl, lr, asl #2]
	strne	sl, [fp, lr, asl #2]
	ldr	lr, [sp, #0]
	add	r3, r3, lr
.L335:
	ldr	sl, [sp, #4]
	cmp	r2, sl
	bcc	.L338
.L333:
	mov	r0, r5
	add	r8, r8, #1
	bl	rk_nandc_xfer_irq_flag_init
	ldr	r3, [r5, #368]
	mov	r8, r8, asr #1
	cmp	r9, #0
	bfi	r4, r8, #22, #6
	ldr	r8, .L343
	orr	r3, r3, #1
	str	r3, [r5, #368]
	ldr	r3, [r5, #364]
	ubfx	sl, r4, #22, #5
	ldreq	r0, [r8, #3792]
	movne	r0, r9
	orr	r3, r3, #1
	str	r3, [r5, #364]
	ldr	r3, [r8, #3796]
	mov	r2, r6
	mov	r1, sl, asl #10
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
	movw	r2, #3844
	cmp	r0, #23
	movw	r1, #3854
	ldrh	r2, [r3, r2]
	movls	r0, #24
	ldrh	r1, [r3, r1]
	str	r0, [r3, #3840]
	mul	r2, r2, r0
	rsb	r0, r0, r1
	movw	r1, #3852
	strh	r0, [r3, r1]	@ movhi
	mov	r0, #0
	ldr	r1, [r3, #3860]
	str	r2, [r3, #3848]
	rsb	r2, r2, r1
	str	r2, [r3, #3856]
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
	movw	r2, #3864
	ldr	r3, .L376
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
	movw	r2, #3854
	strh	r6, [r3, r2]	@ movhi
	mov	r3, #0
	ldr	r2, .L376+4
.L360:
	strb	r3, [r3, r2]
	add	r3, r3, #1
	cmp	r3, #32
	bne	.L360
	ldrh	r2, [r4, #14]
	ldrh	r3, [r4, #20]
	cmp	r3, r2, lsr #8
	bcs	.L361
	ldr	r2, .L376+4
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
	ldr	r3, .L376
	movw	r2, #3866
	mov	r1, r1, asl #1
	mov	r6, r6, lsr #1
	strh	r1, [r3, r2]	@ movhi
	movw	r2, #3854
	strh	r6, [r3, r2]	@ movhi
.L361:
	ldr	r3, .L376
	movw	r2, #3902
	cmp	lr, #1
	mov	r1, #5
	mov	ip, #0
	ldr	sl, .L376
	ldrb	r8, [r3, #852]	@ zero_extendqisi2
	strh	r1, [r3, r2]	@ movhi
	mov	r1, #3904
	streqh	lr, [r3, r2]	@ movhi
	cmp	r8, #0
	movw	r2, #3906
	strh	ip, [r3, r1]	@ movhi
	mov	r1, #4352
	strh	r1, [r3, r2]	@ movhi
	ldrne	r3, .L376
	movne	r1, #384
	strneh	r1, [r3, r2]	@ movhi
	movw	r3, #3866
	ldrh	r5, [sl, r3]
	movw	r3, #3844
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
	movw	r3, #3910
	ldrh	fp, [r4, #18]
	movw	r2, #3912
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
	mov	r2, #3920
	movw	r1, #3922
	cmp	r6, #1024
	strh	r0, [sl, r2]	@ movhi
	mov	r2, r9, asl #9
	mov	r7, r0
	uxth	r2, r2
	strh	r2, [sl, r1]	@ movhi
	add	r1, r1, #2
	mov	r2, r2, lsr #8
	strh	r2, [sl, r1]	@ movhi
	movw	r2, #3926
	ldrh	r1, [r4, #26]
	mov	r4, sl
	strh	r1, [sl, r2]	@ movhi
	uxtbhi	r1, r6
	mul	r2, r6, r5
	str	r2, [sl, #3860]
	movhi	r2, #3904
	ldr	r3, [sp, #4]
	strhih	r1, [sl, r2]	@ movhi
	mov	r2, #3904
	ldrh	r2, [sl, r2]
	mul	r1, fp, r9
	rsb	r2, r2, r6
	mov	r6, r6, asl #6
	mul	r2, r2, r5
	mul	r2, r9, r2
	mul	r3, r3, r2
	mov	r3, r3, asr #11
	str	r3, [sl, #3928]
	movw	sl, #3906
	ldrh	r0, [r4, sl]
	mov	r0, r0, asl #3
	bl	__aeabi_idiv
	movw	r3, #3932
	uxth	r0, r0
	strh	r0, [r4, r3]	@ movhi
	cmp	r0, #4
	ldr	r4, .L376
	ldrls	r2, .L376
	movls	r1, #4
	strlsh	r1, [r2, r3]	@ movhi
	cmp	r8, #0
	mov	r1, r5
	movne	r3, #640
	strneh	r3, [r4, sl]	@ movhi
	ldrh	r2, [r4, sl]
	movw	r3, #3934
	mov	r2, r2, asr r7
	add	r7, r7, #9
	mov	r6, r6, asr r7
	add	r2, r2, #2
	strh	r2, [r4, r3]	@ movhi
	mov	r3, #3936
	uxth	r6, r6
	strh	r6, [r4, r3]	@ movhi
	mul	r3, r5, r6
	add	r6, r6, #8
	str	r3, [r4, #3940]
	movw	r3, #3932
	ldrh	r0, [r4, r3]
	bl	__aeabi_uidiv
	cmp	r5, #1
	ldreq	r3, .L376
	uxtah	r6, r6, r0
	str	r6, [r4, #3840]
	ldr	r4, .L376
	addeq	r6, r6, #4
	streq	r6, [r3, #3840]
	ldr	r0, [r4, #3840]
	uxth	r0, r0
	bl	FtlSysBlkNumInit
	ldr	r3, [r4, #3840]
	ldr	r6, [r4, #3856]
	mov	r0, #2048
	str	r3, [r4, #3944]
	movw	r3, #3912
	ldrh	r3, [r4, r3]
	mov	r6, r6, asl #2
	mul	r6, r3, r6
	mov	r3, #3920
	ldrh	r3, [r4, r3]
	add	r3, r3, #9
	mov	r6, r6, lsr r3
	movw	r3, #3948
	add	r6, r6, #2
	uxth	r6, r6
	strh	r6, [r4, r3]	@ movhi
	movw	r3, #3918
	ldrh	r5, [r4, r3]
	mov	r1, r5
	bl	__aeabi_idiv
	movw	r3, #3950
	ldrb	ip, [r4, #928]	@ zero_extendqisi2
	ldr	r1, [r4, #3940]
	cmp	ip, #0
	strh	r0, [r4, r3]	@ movhi
	mov	r3, #0
	str	r3, [r4, #3952]
	movw	r3, #3932
	ldrh	r0, [r4, r3]
	add	r2, r0, #3
	strh	r2, [r4, r3]	@ movhi
	add	r2, r1, #3
	str	r2, [r4, #3940]
	addne	r0, r0, #4
	addne	r1, r1, #5
	strneh	r0, [r4, r3]	@ movhi
	strne	r1, [r4, #3940]
	bne	.L373
.L372:
	cmp	r2, #7
	movls	r3, #8
	strls	r3, [r4, #3940]
.L373:
	movw	r3, #3956
	mov	r2, #0
	strh	r2, [r4, r3]	@ movhi
	movw	r2, #3852
	ldrh	r2, [r4, r2]
	mov	r0, #0
	mov	r1, r2, lsr #3
	add	r2, r1, r2, asl #1
	add	r2, r2, #52
	add	r6, r2, r6, asl #2
	cmp	r6, r5, asl #9
	ldrcc	r2, .L376
	movcc	r1, #1
	strcch	r1, [r2, r3]	@ movhi
	add	sp, sp, #20
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, sl, fp, pc}
.L377:
	.align	2
.L376:
	.word	.LANCHOR0
	.word	.LANCHOR0+3870
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
	ldr	r3, .L385
	movw	r2, #3958
	ldrh	r2, [r3, r2]
	cmp	r2, #0
	beq	.L383
	movw	r2, #3932
	ldrh	r1, [r3, r2]
	ldr	r2, [r3, #3960]
	mov	r3, #0
	b	.L380
.L382:
	ldrh	ip, [r2], #2
	cmp	ip, r0
	beq	.L384
	add	r3, r3, #1
	uxth	r3, r3
.L380:
	cmp	r3, r1
	bne	.L382
	mov	r0, #0
	bx	lr
.L384:
	mov	r0, #1
	bx	lr
.L383:
	mov	r0, r2
	bx	lr
.L386:
	.align	2
.L385:
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
	ldr	r2, .L395
	stmfd	sp!, {r4, lr}
	.save {r4, lr}
	ldr	r3, [r2, #3964]
	cmp	r3, #0
	beq	.L392
	ldr	ip, [r2, #3968]
	mov	r2, #0
.L391:
	ldr	r4, [ip, #16]
	cmp	r4, r0
	bcc	.L389
	cmp	r4, r1
	bls	.L393
.L389:
	add	r2, r2, #1
	add	ip, ip, #36
	cmp	r2, r3
	bne	.L391
	mov	r0, #0
	ldmfd	sp!, {r4, pc}
.L393:
	mov	r0, #1
	ldmfd	sp!, {r4, pc}
.L392:
	mov	r0, r3
	ldmfd	sp!, {r4, pc}
.L396:
	.align	2
.L395:
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
	ldr	r3, .L398
	ldr	r0, [r3, #3952]
	bx	lr
.L399:
	.align	2
.L398:
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
	ldr	r3, .L401
	ldr	r0, [r3, #3952]
	bx	lr
.L402:
	.align	2
.L401:
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
	ldr	r3, .L404
	ldr	r0, [r3, #3972]
	bx	lr
.L405:
	.align	2
.L404:
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
	movw	r3, #3908
	ldr	r4, .L407
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
	ldr	r0, .L407+4
	bl	printk
	movw	r3, #3982
	ldrh	r2, [r4, r3]
	mov	r0, #0
	add	r2, r2, r6
	strh	r2, [r4, r3]	@ movhi
	ldmfd	sp!, {r2, r3, r4, r5, r6, pc}
.L408:
	.align	2
.L407:
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
	movw	r3, #3908
	ldr	r5, .L410
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
.L411:
	.align	2
.L410:
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
	ldr	r6, .L417
	movw	r8, #3908
	movw	r7, #3866
	b	.L414
.L416:
	mov	r0, r5
	add	r5, r5, #1
	bl	FtlBbmIsBadBlock
	uxth	r5, r5
	cmp	r0, #0
	addne	r4, r4, #1
	uxthne	r4, r4
.L414:
	ldrh	r3, [r6, r8]
	ldrh	r2, [r6, r7]
	mul	r3, r2, r3
	cmp	r5, r3
	blt	.L416
	mov	r0, r4
	ldmfd	sp!, {r4, r5, r6, r7, r8, pc}
.L418:
	.align	2
.L417:
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
	movw	r3, #3868
	ldr	r5, .L420
	mov	r7, r1
	mov	r6, r0
	ldrh	r4, [r5, r3]
	mov	r1, r4
	bl	__aeabi_uidiv
	movw	r3, #3908
	ldrh	r5, [r5, r3]
	mov	r1, r4
	mul	r7, r4, r7
	mla	r5, r5, r0, r7
	mov	r0, r6
	bl	__aeabi_uidivmod
	add	r1, r5, r1
	uxth	r0, r1
	ldmfd	sp!, {r3, r4, r5, r6, r7, pc}
.L421:
	.align	2
.L420:
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
	ldr	r3, .L423
	movw	r2, #3868
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
.L424:
	.align	2
.L423:
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
	movw	r3, #3908
	ldr	r4, .L426
	ldrh	r1, [r4, r3]
	bl	__aeabi_uidivmod
	movw	r3, #3868
	uxth	r0, r1
	ldrh	r1, [r4, r3]
	bl	__aeabi_uidiv
	uxth	r0, r0
	ldmfd	sp!, {r4, pc}
.L427:
	.align	2
.L426:
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
	bls	.L429
	rsb	r0, r1, r0
	cmp	r0, #-2147483648
	movhi	r0, #0
	movls	r0, #1
	bx	lr
.L429:
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
	ldr	r2, .L432
	movw	r3, #4042
	ldrh	r0, [r2, r3]
	rsbs	r0, r0, #1
	movcc	r0, #0
	bx	lr
.L433:
	.align	2
.L432:
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
	ldr	r2, .L435
	movw	r3, #4042
	ldrh	r0, [r2, r3]
	sub	r2, r0, #1024
	rsbs	r0, r2, #0
	adc	r0, r0, r2
	bx	lr
.L436:
	.align	2
.L435:
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
	ldr	r2, .L440
	movw	r3, #4042
	stmfd	sp!, {r4, r5, r6, lr}
	.save {r4, r5, r6, lr}
	mov	r5, r0
	ldrh	r3, [r2, r3]
	cmp	r3, #1024
	ldmeqfd	sp!, {r4, r5, r6, pc}
	cmp	r1, #0
	beq	.L439
	ldr	r4, .L440+4
	ldr	r3, [r4, #-2092]
	cmp	r3, #0
	bne	.L439
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
.L439:
	ldr	r3, .L440
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
.L441:
	.align	2
.L440:
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
	ldr	r3, .L445
	mov	r0, #0
	ldr	r2, .L445+4
	ldr	r1, .L445+8
	stmfd	sp!, {r4, r5, lr}
	.save {r4, r5, lr}
	ldrh	r4, [r2, r3]
	movw	r3, #4038
	ldrh	r2, [r1, r3]
	add	r3, r3, #2
	and	r4, r4, #31
	ldrh	r3, [r1, r3]
	b	.L443
.L444:
	add	ip, r1, r2, asl #1
	add	r2, r2, #1
	add	ip, ip, #4032
	add	r0, r0, #1
	mov	r2, r2, asl #22
	ldrh	r5, [ip, #12]
	add	ip, r1, r3, asl #1
	add	r3, r3, #1
	add	ip, ip, #4032
	mov	r2, r2, lsr #22
	uxth	r0, r0
	mov	r3, r3, asl #22
	strh	r5, [ip, #12]	@ movhi
	mov	r3, r3, lsr #22
.L443:
	cmp	r0, r4
	ldr	ip, .L445+8
	blt	.L444
	movw	r1, #4038
	strh	r2, [ip, r1]	@ movhi
	movw	r2, #4040
	strh	r3, [ip, r2]	@ movhi
	ldmfd	sp!, {r4, r5, pc}
.L446:
	.align	2
.L445:
	.word	-2048
	.word	.LANCHOR2
	.word	.LANCHOR0
	.fnend
	.size	FtlFreeSysBLkSort, .-FtlFreeSysBLkSort
	.align	2
	.global	remove_from_free_sys_Queue
	.type	remove_from_free_sys_Queue, %function
remove_from_free_sys_Queue:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	ldr	r3, .L453
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
	b	.L449
.L451:
	add	r2, r5, ip
	ldr	r4, .L453
	movw	r6, #4036
	mov	r2, r2, asl #22
	add	r2, r3, r2, lsr #21
	add	r2, r2, #4032
	add	r2, r2, #12
	ldrh	r2, [r2, #0]
	cmp	r2, r1
	bne	.L450
	ldr	r0, .L453+4
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
.L450:
	add	r5, r5, #1
.L449:
	cmp	r5, r0
	bcc	.L451
	mov	r0, #0
	ldmfd	sp!, {r4, r5, r6, pc}
.L454:
	.align	2
.L453:
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
	stmfd	sp!, {r4, r5, r6, r7, r8, r9, sl, lr}
	.save {r4, r5, r6, r7, r8, r9, sl, lr}
	movw	r7, #4042
	ldr	r4, .L462
	movw	r8, #4038
.L456:
	ldrh	r1, [r4, r7]
	cmp	r1, #0
	beq	.L457
	ldr	r9, .L462+4
	sub	r1, r1, #1
	ldrh	r2, [r4, r8]
	ldr	r6, [r9, #-2092]
	add	r3, r4, r2, asl #1
	add	r2, r2, #1
	add	r3, r3, #4032
	cmp	r6, #0
	bic	r2, r2, #64512
	ldrh	r5, [r3, #12]
	strh	r1, [r4, r7]	@ movhi
	strh	r2, [r4, r8]	@ movhi
	bne	.L458
	mov	r0, r5
	bl	P2V_block_in_plane
	mov	r3, r5, asl #10
	mov	sl, r0
	ldr	r0, [r9, #-2088]
	str	r3, [r0, #4]
	ldrb	r3, [r4, #928]	@ zero_extendqisi2
	cmp	r3, #0
	beq	.L459
	mov	r1, r6
	mov	r2, #1
	bl	FlashEraseBlocks
.L459:
	ldr	r6, .L462+4
	mov	r1, #1
	mov	r2, r1
	mov	sl, sl, asl #1
	ldr	r0, [r6, #-2088]
	bl	FlashEraseBlocks
	ldr	r3, [r6, #-2084]
	ldrh	r2, [r3, sl]
	add	r2, r2, #1
	strh	r2, [r3, sl]	@ movhi
	ldr	r3, [r6, #-2080]
	add	r3, r3, #1
	str	r3, [r6, #-2080]
	b	.L458
.L457:
	ldr	r0, .L462+8
	bl	printk
.L460:
	b	.L460
.L458:
	cmp	r5, #0
	bne	.L461
	mov	r1, r5
	ldrh	r2, [r4, r7]
	ldr	r0, .L462+12
	bl	printk
	b	.L456
.L461:
	mov	r0, r5
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, sl, pc}
.L463:
	.align	2
.L462:
	.word	.LANCHOR0
	.word	.LANCHOR2
	.word	.LC5
	.word	.LC6
	.fnend
	.size	FtlFreeSysBlkQueueOut, .-FtlFreeSysBlkQueueOut
	.align	2
	.global	insert_data_list
	.type	insert_data_list, %function
insert_data_list:
	.fnstart
	@ args = 0, pretend = 0, frame = 16
	@ frame_needed = 0, uses_anonymous_args = 0
	ldr	r2, .L479
	movw	r3, #3852
	stmfd	sp!, {r4, r5, r6, r7, r8, r9, sl, fp, lr}
	.save {r4, r5, r6, r7, r8, r9, sl, fp, lr}
	.pad #20
	sub	sp, sp, #20
	ldrh	r3, [r2, r3]
	cmp	r3, r0
	bls	.L465
	ldr	r1, .L479+4
	mov	ip, #6
	mul	ip, ip, r0
	mvn	r6, #0
	ldr	r4, [r1, #-2028]
	mov	r5, r1
	add	r2, r4, ip
	strh	r6, [r2, #2]	@ movhi
	strh	r6, [r4, ip]	@ movhi
	ldr	r3, [r1, #-2024]
	cmp	r3, #0
	beq	.L478
.L466:
	ldr	r7, [r1, #-2020]
	mov	r8, r0, asl #1
	ldrh	r1, [r2, #4]
	ldr	r9, .L479
	ldrh	sl, [r7, r8]
	cmp	r1, #0
	ldr	fp, [r5, #-2084]
	mulne	r6, r1, sl
	ldr	r1, [r5, #-2028]
	ldr	sl, .L479+8
	mov	r5, #0
	add	r8, fp, r8
	str	r8, [sp, #12]
	str	r1, [sp, #4]
	rsb	r1, r1, r3
	mov	r1, r1, asr #1
	mul	r1, sl, r1
	movw	sl, #3852
	ldrh	sl, [r9, sl]
	str	sl, [sp, #8]
	uxth	r1, r1
.L473:
	add	r5, r5, #1
	ldr	r8, [sp, #8]
	uxth	r5, r5
	cmp	r5, r8
	bhi	.L465
	cmp	r0, r1
	beq	.L465
	mov	r9, r1, asl #1
	ldrh	r8, [r3, #4]
	ldrh	sl, [r7, r9]
	cmp	r8, #0
	mvneq	r8, #0
	mulne	r8, r8, sl
	cmp	r8, r6
	bne	.L469
	ldrh	sl, [fp, r9]
	ldr	r9, [sp, #12]
	ldrh	r8, [r9, #0]
	cmp	sl, r8
	bcc	.L471
	b	.L470
.L469:
	bhi	.L470
.L471:
	ldrh	r8, [r3, #0]
	movw	sl, #65535
	cmp	r8, sl
	streqh	r1, [r2, #2]	@ movhi
	streqh	r0, [r3, #0]	@ movhi
	ldreq	r3, .L479+4
	streq	r2, [r3, #-2016]
	beq	.L465
.L472:
	ldr	r9, [sp, #4]
	mov	r1, #6
	mla	r3, r1, r8, r9
	mov	r1, r8
	b	.L473
.L470:
	strh	r1, [r4, ip]	@ movhi
	ldrh	r1, [r3, #2]
	strh	r1, [r2, #2]	@ movhi
	ldr	r1, .L479+4
	ldr	ip, [r1, #-2024]
	cmp	r3, ip
	bne	.L474
	strh	r0, [r3, #2]	@ movhi
.L478:
	str	r2, [r1, #-2024]
	b	.L465
.L474:
	ldrh	r2, [r3, #2]
	mov	ip, #6
	ldr	r1, [r1, #-2028]
	mul	r2, ip, r2
	strh	r0, [r1, r2]	@ movhi
	strh	r0, [r3, #2]	@ movhi
.L465:
	mov	r0, #0
	add	sp, sp, #20
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, sl, fp, pc}
.L480:
	.align	2
.L479:
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
	ldr	r2, .L482
	ldr	r3, .L482+4
	ldrh	r1, [r2, r3]
	add	r1, r1, #1
	strh	r1, [r2, r3]	@ movhi
	ldmfd	sp!, {r3, pc}
.L483:
	.align	2
.L482:
	.word	.LANCHOR2
	.word	-2012
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
	beq	.L485
	ldr	r1, .L492
	mov	r5, #6
	mul	ip, r5, r0
	mvn	r3, #0
	ldr	r4, [r1, #-2028]
	add	r2, r4, ip
	strh	r3, [r2, #2]	@ movhi
	strh	r3, [r4, ip]	@ movhi
	ldr	r3, [r1, #-2008]
	cmp	r3, #0
	beq	.L491
.L486:
	ldr	sl, [r1, #-2028]
	mov	r8, r0, asl #1
	ldr	r7, [r1, #-2084]
	rsb	r1, sl, r3
	ldr	r9, .L492+4
	mov	r1, r1, asr #1
	ldrh	r8, [r7, r8]
	mul	r1, r9, r1
	mov	r9, r5
	uxth	r1, r1
.L489:
	mov	r5, r1, asl #1
	ldrh	r5, [r7, r5]
	cmp	r5, r8
	bcs	.L487
	ldrh	r5, [r3, #0]
	cmp	r5, r6
	streqh	r1, [r2, #2]	@ movhi
	streqh	r0, [r3, #0]	@ movhi
	beq	.L485
.L488:
	mla	r3, r9, r5, sl
	mov	r1, r5
	b	.L489
.L487:
	ldrh	r5, [r3, #2]
	strh	r5, [r2, #2]	@ movhi
	strh	r1, [r4, ip]	@ movhi
	ldr	r1, .L492
	ldr	ip, [r1, #-2008]
	cmp	r3, ip
	bne	.L490
	strh	r0, [r3, #2]	@ movhi
.L491:
	str	r2, [r1, #-2008]
	b	.L485
.L490:
	ldrh	r2, [r3, #2]
	mov	ip, #6
	ldr	r1, [r1, #-2028]
	mul	r2, ip, r2
	strh	r0, [r1, r2]	@ movhi
	strh	r0, [r3, #2]	@ movhi
.L485:
	mov	r0, #0
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, sl, pc}
.L493:
	.align	2
.L492:
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
	ldr	r2, .L495
	ldr	r3, .L495+4
	ldrh	r1, [r2, r3]
	add	r1, r1, #1
	strh	r1, [r2, r3]	@ movhi
	ldmfd	sp!, {r3, pc}
.L496:
	.align	2
.L495:
	.word	.LANCHOR2
	.word	-2004
	.fnend
	.size	INSERT_FREE_LIST, .-INSERT_FREE_LIST
	.align	2
	.global	List_remove_node
	.type	List_remove_node, %function
List_remove_node:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	ldr	ip, .L502
	mov	r2, #6
	mul	r1, r2, r1
	ldr	r3, [ip, #-2028]
	stmfd	sp!, {r4, r5, r6, lr}
	.save {r4, r5, r6, lr}
	add	r5, r3, r1
	ldr	r4, [r0, #0]
	movw	r6, #65535
	cmp	r5, r4
	ldrh	r4, [r3, r1]
	bne	.L498
	cmp	r4, r6
	mlane	r2, r2, r4, r3
	moveq	r2, #0
	streq	r2, [r0, #0]
	strne	r2, [r0, #0]
	mvnne	r0, #0
	strneh	r0, [r2, #2]	@ movhi
	b	.L500
.L498:
	cmp	r4, r6
	ldrh	r0, [r5, #2]
	bne	.L501
	cmp	r0, r4
	mulne	r2, r2, r0
	mvnne	r0, #0
	strneh	r0, [r3, r2]	@ movhi
	b	.L500
.L501:
	mla	r4, r2, r4, r3
	strh	r0, [r4, #2]	@ movhi
	ldrh	r6, [r5, #2]
	ldrh	r4, [r3, r1]
	ldr	r0, [ip, #-2028]
	mul	r2, r2, r6
	strh	r4, [r0, r2]	@ movhi
.L500:
	mvn	r2, #0
	mov	r0, #0
	strh	r2, [r3, r1]	@ movhi
	strh	r2, [r5, #2]	@ movhi
	ldmfd	sp!, {r4, r5, r6, pc}
.L503:
	.align	2
.L502:
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
	beq	.L509
	ldr	r2, .L510
	movw	lr, #65535
	mov	ip, #6
	ldr	r4, [r2, #-2028]
	b	.L506
.L508:
	mla	r3, ip, r2, r4
	sub	r1, r1, #1
	uxth	r1, r1
.L506:
	cmp	r1, #0
	beq	.L507
	ldrh	r2, [r3, #0]
	cmp	r2, lr
	bne	.L508
.L507:
	rsb	r4, r4, r3
	ldr	r3, .L510+4
	mov	r4, r4, asr #1
	mul	r4, r3, r4
	uxth	r4, r4
	mov	r1, r4
	bl	List_remove_node
	mov	r0, r4
	ldmfd	sp!, {r4, pc}
.L509:
	movw	r0, #65535
	ldmfd	sp!, {r4, pc}
.L511:
	.align	2
.L510:
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
	ldr	r2, .L518
	ldr	r3, [r2, #-2024]
	cmp	r3, #0
	beq	.L517
	ldr	r2, [r2, #-2028]
	movw	r1, #65535
	mov	ip, #6
	b	.L514
.L516:
	mla	r3, ip, r3, r2
	sub	r0, r0, #1
	uxth	r0, r0
.L514:
	cmp	r0, #0
	beq	.L515
	ldrh	r3, [r3, #0]
	cmp	r3, r1
	bne	.L516
	mov	r0, r1
	bx	lr
.L515:
	rsb	r3, r2, r3
	ldr	r0, .L518+4
	mov	r3, r3, asr #1
	mul	r0, r0, r3
	uxth	r0, r0
	bx	lr
.L517:
	movw	r0, #65535
	bx	lr
.L519:
	.align	2
.L518:
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
	ldr	r3, .L527
	ldr	r2, .L527+4
	ldrh	r2, [r3, r2]
	cmp	r2, r0
	beq	.L521
	ldr	r2, .L527+8
	ldrh	r2, [r3, r2]
	cmp	r2, r0
	beq	.L521
	ldr	r2, .L527+12
	ldrh	r2, [r3, r2]
	cmp	r2, r0
	beq	.L521
	mov	r5, #6
	ldr	r2, [r3, #-2028]
	mul	r5, r5, r0
	ldr	r0, [r3, #-2024]
	add	r1, r2, r5
	cmp	r1, r0
	beq	.L521
	ldr	ip, [r3, #-2020]
	mov	r3, r4, asl #1
	ldrh	r0, [ip, r3]
	ldrh	r3, [r1, #4]
	cmp	r3, #0
	mulne	r0, r3, r0
	ldrh	r3, [r1, #2]
	mvneq	r0, #0
	movw	r1, #65535
	cmp	r3, r1
	bne	.L523
	ldrh	r1, [r2, r5]
	cmp	r1, r3
	beq	.L521
.L523:
	mov	r1, #6
	mla	r3, r1, r3, r2
	ldr	r1, .L527+16
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
	bcs	.L521
	ldr	r0, .L527+20
	mov	r1, r4
	bl	List_remove_node
	ldr	r5, .L527
	ldr	r3, .L527+24
	mov	r0, r4
	ldrh	r2, [r5, r3]
	sub	r2, r2, #1
	strh	r2, [r5, r3]	@ movhi
	bl	INSERT_DATA_LIST
.L521:
	mov	r0, #0
	ldmfd	sp!, {r3, r4, r5, pc}
.L528:
	.align	2
.L527:
	.word	.LANCHOR2
	.word	-2000
	.word	-1952
	.word	-1904
	.word	-1431655765
	.word	.LANCHOR2-2024
	.word	-2012
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
	b	.L530
.L535:
	mov	r7, r3
	add	r3, r3, #2
	ldrh	r6, [r7, #0]
	cmp	r6, #0
	bne	.L531
	bl	FtlFreeSysBlkQueueOut
	movw	r3, #65533
	sub	r2, r0, #1
	mov	r1, r0
	strh	r0, [r7, #0]	@ movhi
	uxth	r2, r2
	cmp	r2, r3
	bls	.L532
	ldr	r2, .L536
	movw	r3, #4042
	ldr	r0, .L536+4
	ldrh	r2, [r2, r3]
	bl	printk
.L533:
	b	.L533
.L532:
	ldr	r3, [r4, #28]
	strh	r6, [r4, #2]	@ movhi
	add	r3, r3, #1
	str	r3, [r4, #28]
	ldrh	r3, [r4, #8]
	strh	r5, [r4, #0]	@ movhi
	add	r3, r3, #1
	strh	r3, [r4, #8]	@ movhi
	b	.L534
.L531:
	add	r5, r5, #1
	uxth	r5, r5
.L530:
	cmp	r5, r2
	bne	.L535
.L534:
	mov	r0, #0
	ldmfd	sp!, {r3, r4, r5, r6, r7, pc}
.L537:
	.align	2
.L536:
	.word	.LANCHOR0
	.word	.LC7
	.fnend
	.size	ftl_map_blk_alloc_new_blk, .-ftl_map_blk_alloc_new_blk
	.align	2
	.global	select_l2p_ram_region
	.type	select_l2p_ram_region, %function
select_l2p_ram_region:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	ldr	r2, .L550
	movw	r3, #3950
	stmfd	sp!, {r4, r5, r6, r7, lr}
	.save {r4, r5, r6, r7, lr}
	movw	r1, #65535
	ldrh	r3, [r2, r3]
	ldr	r2, .L550+4
	ldr	ip, [r2, #-1856]
	mov	r2, #0
	mov	r0, r2
	b	.L539
.L541:
	add	r2, r2, #12
	add	r4, ip, r2
	ldrh	r4, [r4, #-12]
	cmp	r4, r1
	ldmeqfd	sp!, {r4, r5, r6, r7, pc}
	add	r0, r0, #1
	uxth	r0, r0
.L539:
	cmp	r0, r3
	bne	.L541
	mov	r1, #0
	mov	r0, r3
	mov	r5, #-2147483648
	mov	r2, r1
	b	.L542
.L544:
	add	r4, ip, r1
	ldr	r4, [r4, #4]
	cmp	r4, #0
	blt	.L543
	cmp	r4, r5
	movcc	r5, r4
	movcc	r0, r2
.L543:
	add	r2, r2, #1
	add	r1, r1, #12
	uxth	r2, r2
.L542:
	cmp	r2, r3
	bne	.L544
	cmp	r0, r3
	ldmccfd	sp!, {r4, r5, r6, r7, pc}
	ldr	r2, .L550+8
	mov	r0, r3
	ldr	r1, .L550+4
	mvn	r4, #0
	ldrh	r6, [r1, r2]
	mov	r2, #0
	mov	r1, r2
	b	.L545
.L547:
	add	r5, ip, r2
	ldr	r5, [r5, #4]
	cmp	r5, r4
	bcs	.L546
	ldrh	r7, [ip, r2]
	cmp	r7, r6
	movne	r4, r5
	movne	r0, r1
.L546:
	add	r1, r1, #1
	add	r2, r2, #12
	uxth	r1, r1
.L545:
	cmp	r1, r3
	bne	.L547
	ldmfd	sp!, {r4, r5, r6, r7, pc}
.L551:
	.align	2
.L550:
	.word	.LANCHOR0
	.word	.LANCHOR2
	.word	-1852
	.fnend
	.size	select_l2p_ram_region, .-select_l2p_ram_region
	.align	2
	.global	FtlUpdateVaildLpn
	.type	FtlUpdateVaildLpn, %function
FtlUpdateVaildLpn:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	ldr	r3, .L558
	ldr	r2, .L558+4
	stmfd	sp!, {r4, r5, lr}
	.save {r4, r5, lr}
	ldrh	r1, [r2, r3]
	cmp	r1, #4
	add	ip, r1, #1
	mov	r1, r3
	strh	ip, [r2, r3]	@ movhi
	bhi	.L553
	cmp	r0, #0
	ldmeqfd	sp!, {r4, r5, pc}
.L553:
	ldr	r0, .L558+8
	mov	r3, #0
	strh	r3, [r2, r1]	@ movhi
	movw	r1, #3852
	str	r3, [r2, #-1848]
	movw	ip, #65535
	ldrh	r4, [r0, r1]
	ldr	r1, [r2, #-2020]
	ldr	r2, .L558+4
	b	.L555
.L557:
	ldrh	r0, [r1], #2
	add	r3, r3, #1
	cmp	r0, ip
	uxth	r3, r3
	ldrne	r5, [r2, #-1848]
	addne	r0, r0, r5
	strne	r0, [r2, #-1848]
.L555:
	cmp	r3, r4
	bne	.L557
	ldmfd	sp!, {r4, r5, pc}
.L559:
	.align	2
.L558:
	.word	-1850
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
	ldr	r3, .L564
	mov	r2, r0, lsr #5
	cmp	r1, #0
	and	r0, r0, #31
	uxth	r2, r2
	mov	ip, #1
	ldr	r3, [r3, #-1844]
	ldr	r1, [r3, r2, asl #2]
	orrne	r0, r1, ip, asl r0
	biceq	r0, r1, ip, asl r0
	str	r0, [r3, r2, asl #2]
	bx	lr
.L565:
	.align	2
.L564:
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
	ldr	r3, .L567
	mov	r2, r0, lsr #5
	and	r0, r0, #31
	ldr	r3, [r3, #-1844]
	ldr	r3, [r3, r2, asl #2]
	mov	r0, r3, lsr r0
	and	r0, r0, #1
	bx	lr
.L568:
	.align	2
.L567:
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
	ldr	ip, .L576
	mov	r3, #0
	strh	r3, [r0, #4]	@ movhi
	movw	r3, #3844
	stmfd	sp!, {r4, r5, r6, lr}
	.save {r4, r5, r6, lr}
	ldrh	r3, [ip, r3]
	movw	ip, #65535
	b	.L570
.L572:
	add	r4, r0, r2, asl #1
	add	r2, r2, #1
	ldrh	r4, [r4, #16]
	uxth	r2, r2
	cmp	r4, ip
	ldrneh	r4, [r0, #4]
	addne	r4, r4, #1
	strneh	r4, [r0, #4]	@ movhi
.L570:
	cmp	r2, r3
	bcc	.L572
	ldr	ip, .L576
	movw	r2, #3912
	movw	r4, #65535
	mvn	r1, r1
	ldrh	r5, [ip, r2]
	mov	ip, r0
	mov	r2, #0
	b	.L573
.L575:
	ldrh	r6, [ip, #16]
	add	r2, r2, #1
	add	ip, ip, #2
	cmp	r6, r4
	uxth	r2, r2
	ldrneh	r6, [r0, #4]
	addne	r6, r5, r6
	addne	r6, r6, r1
	strneh	r6, [r0, #4]	@ movhi
.L573:
	cmp	r2, r3
	bne	.L575
	ldmfd	sp!, {r4, r5, r6, pc}
.L577:
	.align	2
.L576:
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
	ldr	r6, .L584
	strh	r5, [r0, #4]	@ movhi
	movw	r8, #3844
	strb	r5, [r4, #7]
	mvn	r7, #0
	b	.L579
.L581:
	add	r3, r6, r5
	ldrh	r1, [r4, #0]
	add	sl, r5, #8
	add	r5, r5, #1
	ldrb	r0, [r3, #3870]	@ zero_extendqisi2
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
.L579:
	ldrh	r2, [r6, r8]
	ldr	r3, .L584
	cmp	r2, r5
	bhi	.L581
	movw	r1, #3912
	ldrb	r2, [r4, #7]	@ zero_extendqisi2
	ldrh	r1, [r3, r1]
	ldr	r3, [r3, #3836]
	mul	r2, r1, r2
	strh	r2, [r4, #4]	@ movhi
	mov	r2, #0
	cmp	r3, r2
	strb	r2, [r4, #9]
	beq	.L582
	ldr	r2, .L584+4
	ldrh	r3, [r4, #0]
	ldr	r2, [r2, #-2084]
	mov	r3, r3, asl #1
	ldrh	r3, [r2, r3]
	cmp	r3, #59
	movls	r3, #1
	strlsb	r3, [r4, #9]
.L582:
	ldr	r3, .L584
	mov	r0, #0
	ldrb	r3, [r3, #852]	@ zero_extendqisi2
	cmp	r3, #0
	movne	r3, #1
	strneb	r3, [r4, #9]
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, sl, pc}
.L585:
	.align	2
.L584:
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
	ldr	r6, .L591
	movw	sl, #3844
	movw	r8, #3912
	b	.L587
.L589:
	add	r3, r6, r5
	mov	r1, r7
	add	r5, r5, #1
	ldrb	r0, [r3, #3870]	@ zero_extendqisi2
	bl	V2P_block
	uxth	r5, r5
	bl	FtlBbmIsBadBlock
	cmp	r0, #0
	ldreqh	r3, [r6, r8]
	addeq	r4, r4, r3
	uxtheq	r4, r4
.L587:
	ldrh	r3, [r6, sl]
	cmp	r3, r5
	bhi	.L589
	cmp	r4, #0
	beq	.L590
	mov	r1, r4
	mov	r0, #32768
	bl	__aeabi_idiv
	uxth	r4, r0
.L590:
	ldr	r3, .L591+4
	mov	r2, #6
	mov	r0, #0
	ldr	r3, [r3, #-2028]
	mla	r7, r2, r7, r3
	strh	r4, [r7, #4]	@ movhi
	ldmfd	sp!, {r3, r4, r5, r6, r7, r8, sl, pc}
.L592:
	.align	2
.L591:
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
	ldr	r3, .L596
	ldr	r0, [r3, #-2008]
	cmp	r0, #0
	bxeq	lr
	ldr	r2, [r3, #-2028]
	rsb	r0, r2, r0
	ldr	r2, .L596+4
	mov	r0, r0, asr #1
	mul	r0, r2, r0
	ldr	r2, [r3, #-2084]
	uxth	r0, r0
	mov	r3, r0, asl #1
	ldrh	r0, [r2, r3]
	bx	lr
.L597:
	.align	2
.L596:
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
	ldr	r2, .L605
	stmfd	sp!, {r4, r5, lr}
	.save {r4, r5, lr}
	ldr	r3, [r2, #-2008]
	cmp	r3, #0
	beq	.L604
	ldr	r1, .L605+4
	mov	ip, #7
	mov	r5, #6
	movw	r4, #65535
	ldrh	r1, [r2, r1]
	mul	r1, ip, r1
	mov	r1, r1, asr #3
	cmp	r0, r1
	uxthgt	r0, r1
	ldr	r1, [r2, #-2028]
	ldr	r2, .L605+8
	rsb	r3, r1, r3
	mov	r3, r3, asr #1
	mul	r3, r2, r3
	mov	r2, #0
	uxth	r3, r3
	b	.L601
.L603:
	mul	ip, r5, r3
	ldrh	ip, [r1, ip]
	cmp	ip, r4
	beq	.L602
	add	r2, r2, #1
	mov	r3, ip
	uxth	r2, r2
.L601:
	cmp	r2, r0
	bne	.L603
.L602:
	ldr	r2, .L605
	mov	r3, r3, asl #1
	ldr	r2, [r2, #-2084]
	ldrh	r0, [r2, r3]
	ldmfd	sp!, {r4, r5, pc}
.L604:
	mov	r0, r3
	ldmfd	sp!, {r4, r5, pc}
.L606:
	.align	2
.L605:
	.word	.LANCHOR2
	.word	-2004
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
	mov	sl, r0
	ldr	r5, .L617
	add	r4, sl, #12
	.pad #32
	sub	sp, sp, #32
	ldr	r1, .L617+4
	bl	strcpy
	mov	r0, r4
	ldr	r1, .L617+8
	ldr	r2, [r5, #3048]
	bl	sprintf
	ldr	r1, .L617+12
	ldr	r2, [r5, #3928]
	add	r4, r4, r0
	mov	r0, r4
	bl	sprintf
	ldr	r3, .L617+16
	ldr	r3, [r3, #2996]
	cmp	r3, #1
	add	r4, r4, r0
	rsbne	r0, sl, r4
	bne	.L609
	add	r0, sp, #16
	add	r1, sp, #20
	add	r2, sp, #24
	add	r3, sp, #28
	bl	NandcGetTimeCfg
	ldr	r3, [sp, #24]
	ldr	r2, [sp, #16]
	mov	r0, r4
	ldr	r1, .L617+20
	str	r3, [sp, #0]
	ldr	r3, [sp, #28]
	ldr	r8, .L617+24
	str	r3, [sp, #4]
	ldr	r3, [sp, #20]
	bl	sprintf
	ldr	r1, .L617+28
	add	r6, r4, r0
	ldr	r4, .L617+32
	mov	r0, r6
	add	r6, r6, #10
	bl	strcpy
	ldr	r2, [r5, #3972]
	mov	r0, r6
	ldr	r1, .L617+36
	bl	sprintf
	ldr	r1, .L617+40
	ldr	r2, [r4, #-1848]
	add	r6, r6, r0
	mov	r0, r6
	bl	sprintf
	ldr	r1, .L617+44
	ldr	r2, [r4, #-1840]
	add	r6, r6, r0
	mov	r0, r6
	bl	sprintf
	ldr	r1, .L617+48
	ldr	r2, [r4, #-1836]
	add	r6, r6, r0
	mov	r0, r6
	bl	sprintf
	ldr	r1, .L617+52
	ldr	r2, [r4, #-1832]
	add	r6, r6, r0
	mov	r0, r6
	bl	sprintf
	ldr	r1, .L617+56
	ldr	r2, [r4, #-1828]
	add	r6, r6, r0
	mov	r0, r6
	bl	sprintf
	ldr	r1, .L617+60
	ldr	r2, [r4, #-1824]
	add	r6, r6, r0
	mov	r0, r6
	bl	sprintf
	ldr	r1, .L617+64
	ldr	r2, [r4, #-1820]
	add	r6, r6, r0
	mov	r0, r6
	bl	sprintf
	ldr	r2, [r4, #-1816]
	ldr	r1, .L617+68
	mov	r2, r2, lsr #11
	add	r6, r6, r0
	mov	r0, r6
	bl	sprintf
	ldr	r2, [r4, #-1812]
	ldr	r1, .L617+72
	mov	r2, r2, lsr #11
	add	r6, r6, r0
	mov	r0, r6
	bl	sprintf
	ldr	r1, .L617+76
	ldr	r2, [r4, #-1808]
	add	r6, r6, r0
	mov	r0, r6
	bl	sprintf
	ldr	r1, .L617+80
	ldr	r2, [r4, #-1804]
	add	r6, r6, r0
	mov	r0, r6
	bl	sprintf
	add	r6, r6, r0
	bl	FtlBbtCalcTotleCnt
	movw	r2, #3982
	ldr	r1, .L617+84
	ldrh	r2, [r5, r2]
	mov	r3, r0
	mov	r0, r6
	bl	sprintf
	ldr	r1, .L617+88
	add	r7, r6, r0
	ldr	r6, .L617+92
	mov	r0, r7
	ldrh	r2, [r4, r6]
	bl	sprintf
	ldr	r1, .L617+96
	ldr	r2, [r4, #-1800]
	add	r7, r7, r0
	mov	r0, r7
	bl	sprintf
	ldr	r1, .L617+100
	ldr	r2, [r4, #-1796]
	add	r7, r7, r0
	mov	r0, r7
	bl	sprintf
	ldr	r1, .L617+104
	ldr	r2, [r4, #-1792]
	add	r7, r7, r0
	mov	r0, r7
	bl	sprintf
	ldr	r1, .L617+108
	ldr	r2, [r4, #-2080]
	add	r7, r7, r0
	mov	r0, r7
	bl	sprintf
	ldr	r1, .L617+112
	ldr	r2, [r4, #-1788]
	add	r7, r7, r0
	mov	r0, r7
	bl	sprintf
	ldr	r1, .L617+116
	ldr	r2, [r4, #-1784]
	add	r7, r7, r0
	mov	r0, r7
	bl	sprintf
	ldr	r3, .L617+120
	ldr	r1, .L617+124
	ldrh	r2, [r4, r3]
	add	r7, r7, r0
	mov	r0, r7
	bl	sprintf
	ldr	r3, .L617+128
	ldr	r1, .L617+132
	ldrh	r2, [r4, r3]
	add	r7, r7, r0
	mov	r0, r7
	bl	sprintf
	ldr	r2, [r5, #3952]
	ldr	r1, .L617+136
	add	r7, r7, r0
	mov	r0, r7
	bl	sprintf
	ldr	r2, [r5, #3944]
	ldr	r1, .L617+140
	add	r7, r7, r0
	mov	r0, r7
	bl	sprintf
	ldr	r2, [r5, #3840]
	ldr	r1, .L617+144
	add	r7, r7, r0
	mov	r0, r7
	bl	sprintf
	movw	r3, #4042
	ldrh	r2, [r5, r3]
	ldr	r1, .L617+148
	add	r7, r7, r0
	mov	r0, r7
	bl	sprintf
	movw	r3, #3852
	ldrh	r2, [r5, r3]
	ldr	r1, .L617+152
	add	r7, r7, r0
	mov	r0, r7
	bl	sprintf
	ldr	r3, .L617+156
	ldr	r1, .L617+160
	ldrh	r2, [r4, r3]
	add	r7, r7, r0
	mov	r0, r7
	bl	sprintf
	ldr	r2, [r5, #3856]
	ldr	r1, .L617+164
	add	r7, r7, r0
	mov	r0, r7
	bl	sprintf
	ldr	r3, .L617+168
	ldr	r1, .L617+172
	ldrh	r2, [r4, r3]
	add	r7, r7, r0
	mov	r0, r7
	bl	sprintf
	movw	r3, #3976
	ldrh	r2, [r5, r3]
	ldr	r1, .L617+176
	add	r7, r7, r0
	mov	r0, r7
	bl	sprintf
	ldr	r3, .L617+180
	ldr	r1, .L617+184
	ldrh	r2, [r4, r3]
	add	r7, r7, r0
	mov	r0, r7
	bl	sprintf
	ldr	r1, .L617+188
	ldrb	r2, [r4, #-1994]	@ zero_extendqisi2
	add	r7, r7, r0
	mov	r0, r7
	bl	sprintf
	ldrh	r2, [r4, r8]
	ldr	r1, .L617+192
	add	r7, r7, r0
	mov	r0, r7
	bl	sprintf
	ldr	r1, .L617+196
	ldrb	r2, [r4, #-1992]	@ zero_extendqisi2
	add	r7, r7, r0
	mov	r0, r7
	bl	sprintf
	ldr	r3, .L617+200
	ldr	r1, .L617+204
	ldrh	r2, [r4, r3]
	add	r7, r7, r0
	mov	r0, r7
	bl	sprintf
	ldrh	r3, [r4, r8]
	ldr	r2, [r4, #-2020]
	add	r8, r8, #48
	ldr	r1, .L617+208
	mov	r3, r3, asl #1
	ldrh	r2, [r2, r3]
	add	r7, r7, r0
	mov	r0, r7
	bl	sprintf
	ldr	r3, .L617+212
	ldr	r1, .L617+216
	ldrh	r2, [r4, r3]
	add	r7, r7, r0
	mov	r0, r7
	bl	sprintf
	ldr	r1, .L617+220
	ldrb	r2, [r4, #-1946]	@ zero_extendqisi2
	add	r7, r7, r0
	mov	r0, r7
	bl	sprintf
	ldrh	r2, [r4, r8]
	ldr	r1, .L617+224
	add	r7, r7, r0
	mov	r0, r7
	bl	sprintf
	ldr	r1, .L617+228
	ldrb	r2, [r4, #-1944]	@ zero_extendqisi2
	add	r7, r7, r0
	mov	r0, r7
	bl	sprintf
	ldr	r3, .L617+232
	ldr	r1, .L617+236
	ldrh	r2, [r4, r3]
	add	r7, r7, r0
	mov	r0, r7
	bl	sprintf
	ldrh	r3, [r4, r8]
	ldr	r2, [r4, #-2020]
	add	r8, r8, #188
	ldr	r1, .L617+240
	mov	r3, r3, asl #1
	ldrh	r2, [r2, r3]
	add	r7, r7, r0
	mov	r0, r7
	bl	sprintf
	ldr	r3, .L617+244
	ldr	r1, .L617+248
	ldrh	r2, [r4, r3]
	add	r7, r7, r0
	mov	r0, r7
	bl	sprintf
	ldr	r1, .L617+252
	ldrb	r2, [r4, #-1898]	@ zero_extendqisi2
	add	r7, r7, r0
	mov	r0, r7
	bl	sprintf
	ldr	r3, .L617+256
	ldr	r1, .L617+260
	ldrh	r2, [r4, r3]
	add	r7, r7, r0
	mov	r0, r7
	bl	sprintf
	ldr	r1, .L617+264
	ldrb	r2, [r4, #-1896]	@ zero_extendqisi2
	add	r7, r7, r0
	mov	r0, r7
	bl	sprintf
	ldr	r3, .L617+268
	ldr	r1, .L617+272
	ldrh	r2, [r4, r3]
	add	r7, r7, r0
	mov	r0, r7
	bl	sprintf
	ldr	r3, .L617+276
	ldr	r1, .L617+280
	ldrh	r2, [r4, r3]
	add	r7, r7, r0
	mov	r0, r7
	bl	sprintf
	ldr	r1, .L617+284
	ldrb	r2, [r4, #-1758]	@ zero_extendqisi2
	add	r7, r7, r0
	mov	r0, r7
	bl	sprintf
	ldr	r1, .L617+288
	ldrh	r2, [r4, r8]
	add	r7, r7, r0
	mov	r0, r7
	bl	sprintf
	ldr	r1, .L617+292
	ldrb	r2, [r4, #-1756]	@ zero_extendqisi2
	add	r7, r7, r0
	mov	r0, r7
	bl	sprintf
	ldr	r3, .L617+296
	ldr	r1, .L617+300
	ldrh	r2, [r4, r3]
	add	r7, r7, r0
	mov	r0, r7
	bl	sprintf
	ldr	r1, [r4, #-1628]
	ldr	r3, [r5, #3836]
	ldr	r2, [r4, #-1716]
	str	r1, [sp, #0]
	ldr	r1, [r4, #-1636]
	orr	r2, r3, r2, asl #8
	str	r1, [sp, #4]
	ldr	r1, .L617+304
	ldr	r3, [r4, #-1632]
	add	r7, r7, r0
	mov	r0, r7
	bl	sprintf
	ldr	r1, .L617+308
	ldr	r2, [r4, #-1640]
	add	r5, r7, r0
	mov	r0, r5
	bl	sprintf
	ldr	r1, .L617+312
	ldr	r2, [r4, #-1616]
	add	r5, r5, r0
	mov	r0, r5
	bl	sprintf
	ldr	r3, .L617+316
	ldr	r1, .L617+320
	ldrh	r2, [r4, r3]
	add	r5, r5, r0
	mov	r0, r5
	bl	sprintf
	ldr	r3, .L617+324
	ldr	r1, .L617+328
	ldrh	r2, [r4, r3]
	add	r5, r5, r0
	mov	r0, r5
	bl	sprintf
	ldr	r1, .L617+332
	ldr	r2, [r4, #-1196]
	add	r5, r5, r0
	mov	r0, r5
	bl	sprintf
	ldr	r3, .L617+336
	ldr	r1, .L617+340
	ldrh	r2, [r4, r3]
	add	r5, r5, r0
	mov	r0, r5
	bl	sprintf
	add	r5, r5, r0
	bl	GetFreeBlockMinEraseCount
	ldr	r1, .L617+344
	mov	r2, r0
	mov	r0, r5
	bl	sprintf
	add	r5, r5, r0
	ldrh	r0, [r4, r6]
	bl	GetFreeBlockMaxEraseCount
	ldr	r1, .L617+348
	mov	r2, r0
	mov	r0, r5
	bl	sprintf
	ldrh	r3, [r4, r8]
	movw	r2, #65535
	cmp	r3, r2
	add	r5, r5, r0
	beq	.L610
	ldr	r2, [r4, #-2020]
	mov	r3, r3, asl #1
	mov	r0, r5
	ldr	r1, .L617+352
	ldrh	r2, [r2, r3]
	bl	sprintf
	add	r5, r5, r0
.L610:
	mov	r0, #0
	ldr	r4, .L617+32
	bl	List_get_gc_head_node
	mov	r6, #0
	movw	r8, #65535
	mov	r9, #6
	uxth	r3, r0
.L612:
	cmp	r3, r8
	beq	.L611
	ldr	r1, [r4, #-2020]
	mov	r2, r3, asl #1
	mul	r7, r9, r3
	mov	r0, r5
	ldrh	r1, [r1, r2]
	str	r1, [sp, #0]
	ldr	r1, [r4, #-2028]
	add	r1, r1, r7
	ldrh	r1, [r1, #4]
	str	r1, [sp, #4]
	ldr	r1, [r4, #-2084]
	ldrh	r2, [r1, r2]
	ldr	r1, .L617+356
	str	r2, [sp, #8]
	mov	r2, r6
	bl	sprintf
	add	r6, r6, #1
	ldr	r3, [r4, #-2028]
	cmp	r6, #16
	ldrh	r3, [r3, r7]
	add	r5, r5, r0
	bne	.L612
.L611:
	ldr	r6, .L617+32
	mov	r4, #0
	movw	r8, #65535
	mov	r9, #6
	ldr	r2, [r6, #-2008]
	ldr	r3, [r6, #-2028]
	rsb	r3, r3, r2
	ldr	r2, .L617+360
	mov	r3, r3, asr #1
	mul	r3, r2, r3
	uxth	r3, r3
.L614:
	cmp	r3, r8
	beq	.L613
	mul	r7, r9, r3
	ldr	r2, [r6, #-2028]
	mov	r0, r5
	add	r2, r2, r7
	ldrh	r2, [r2, #4]
	str	r2, [sp, #0]
	mov	r2, r3, asl #1
	ldr	r1, [r6, #-2084]
	ldrh	r2, [r1, r2]
	ldr	r1, .L617+364
	str	r2, [sp, #4]
	mov	r2, r4
	bl	sprintf
	add	r4, r4, #1
	ldr	r3, [r6, #-2028]
	cmp	r4, #4
	ldrh	r3, [r3, r7]
	add	r5, r5, r0
	bne	.L614
.L613:
	rsb	r0, sl, r5
.L609:
	add	sp, sp, #32
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, sl, pc}
.L618:
	.align	2
.L617:
	.word	.LANCHOR0
	.word	.LC8
	.word	.LC9
	.word	.LC10
	.word	.LANCHOR1
	.word	.LC11
	.word	-2000
	.word	.LC12
	.word	.LANCHOR2
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
	.word	-2004
	.word	.LC27
	.word	.LC28
	.word	.LC29
	.word	.LC30
	.word	.LC31
	.word	.LC32
	.word	-2046
	.word	.LC33
	.word	-2048
	.word	.LC34
	.word	.LC35
	.word	.LC36
	.word	.LC37
	.word	.LC38
	.word	.LC39
	.word	-1780
	.word	.LC40
	.word	.LC41
	.word	-1776
	.word	.LC42
	.word	.LC43
	.word	-1998
	.word	.LC44
	.word	.LC45
	.word	.LC46
	.word	.LC47
	.word	-1996
	.word	.LC48
	.word	.LC49
	.word	-1950
	.word	.LC50
	.word	.LC51
	.word	.LC52
	.word	.LC53
	.word	-1948
	.word	.LC54
	.word	.LC55
	.word	-1902
	.word	.LC56
	.word	.LC57
	.word	-1904
	.word	.LC58
	.word	.LC59
	.word	-1900
	.word	.LC60
	.word	-1762
	.word	.LC61
	.word	.LC62
	.word	.LC63
	.word	.LC64
	.word	-1760
	.word	.LC65
	.word	.LC66
	.word	.LC67
	.word	.LC68
	.word	-1200
	.word	.LC69
	.word	-1198
	.word	.LC70
	.word	.LC71
	.word	-1192
	.word	.LC72
	.word	.LC73
	.word	.LC74
	.word	.LC75
	.word	.LC76
	.word	-1431655765
	.word	.LC77
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
	ldr	r1, .L620
	ldr	r2, .L620+4
	bl	sprintf
	add	r5, r4, r0
	mov	r0, r5
	bl	FtlPrintInfo2buf
	add	r0, r5, r0
	rsb	r0, r4, r0
	ldmfd	sp!, {r3, r4, r5, pc}
.L621:
	.align	2
.L620:
	.word	.LC78
	.word	.LC79
	.fnend
	.size	ftl_proc_ftl_read, .-ftl_proc_ftl_read
	.align	2
	.global	GetSwlReplaceBlock
	.type	GetSwlReplaceBlock, %function
GetSwlReplaceBlock:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	ldr	r3, .L647
	stmfd	sp!, {r4, r5, r6, r7, r8, r9, sl, fp, lr}
	.save {r4, r5, r6, r7, r8, r9, sl, fp, lr}
	.pad #20
	sub	sp, sp, #20
	ldr	r1, [r3, #-1792]
	ldr	r2, [r3, #-1784]
	cmp	r1, r2
	bcs	.L623
	ldr	r0, .L647+4
	movw	r1, #3852
	mov	r2, #0
	str	r2, [r3, #-1800]
	ldrh	r1, [r0, r1]
	ldr	r0, [r3, #-2084]
	b	.L624
.L625:
	ldrh	lr, [r0], #2
	add	r2, r2, #1
	ldr	ip, [r3, #-1800]
	add	ip, lr, ip
	str	ip, [r3, #-1800]
.L624:
	cmp	r2, r1
	ldr	r4, .L647
	bcc	.L625
	ldr	r5, [r4, #-1800]
	mov	r0, r5
	bl	__aeabi_uidiv
	ldr	r2, .L647+4
	movw	r3, #3902
	ldrh	r1, [r2, r3]
	str	r0, [r4, #-1792]
	ldr	r0, [r4, #-1796]
	rsb	r0, r0, r5
	bl	__aeabi_uidiv
	str	r0, [r4, #-1800]
	b	.L626
.L623:
	ldr	r2, [r3, #-1788]
	cmp	r1, r2
	bls	.L626
	add	r2, r2, #1
	ldr	r4, .L647+4
	str	r2, [r3, #-1788]
	movw	ip, #3852
	mov	r2, #0
	b	.L627
.L628:
	ldr	r0, [r3, #-2084]
	mov	r1, r2, asl #1
	add	r2, r2, #1
	ldrh	r5, [r0, r1]
	add	r5, r5, #1
	strh	r5, [r0, r1]	@ movhi
.L627:
	ldrh	r1, [r4, ip]
	cmp	r2, r1
	bcc	.L628
.L626:
	ldr	r3, .L647
	ldr	r6, [r3, #-1784]
	ldr	r5, [r3, #-1792]
	add	r2, r6, #256
	cmp	r2, r5
	mov	r2, r3
	bls	.L629
	ldr	r3, [r3, #-1788]
	add	r1, r6, #768
	cmp	r1, r3
	bls	.L629
	ldr	r3, .L647+4
	ldr	r3, [r3, #3836]
	cmp	r3, #0
	beq	.L646
	cmp	r6, #30
	bhi	.L646
.L629:
	ldr	r3, .L647+8
	ldrh	r0, [r2, r3]
	add	r0, r0, r0, asl #1
	ubfx	r0, r0, #2, #16
	bl	GetFreeBlockMaxEraseCount
	add	r3, r6, #64
	cmp	r0, r3
	mov	r8, r0
	bcs	.L631
	cmp	r6, #30
	bhi	.L646
.L631:
	ldr	r2, .L647
	ldr	r3, [r2, #-2024]
	cmp	r3, #0
	beq	.L646
	ldr	r0, .L647+4
	movw	r1, #3852
	movw	r7, #65535
	mov	r4, r7
	mov	fp, r7
	ldrh	sl, [r0, r1]
	ldr	r0, [r2, #-2028]
	ldr	r1, [r2, #-2084]
	mov	r2, #0
	b	.L632
.L635:
	add	r2, r2, #1
	uxth	r2, r2
	cmp	r2, sl
	bhi	.L646
	ldrh	ip, [r3, #4]
	cmp	ip, #0
	beq	.L633
	rsb	r3, r0, r3
	ldr	ip, .L647+12
	mov	r3, r3, asr #1
	mul	r3, ip, r3
	uxth	r3, r3
	mov	ip, r3, asl #1
	ldrh	ip, [r1, ip]
	cmp	ip, r6
	bls	.L643
	cmp	ip, r7
	movcc	r7, ip
	movcc	r4, r3
.L633:
	mov	ip, #6
	mla	r3, ip, r9, r0
.L632:
	ldrh	r9, [r3, #0]
	cmp	r9, fp
	bne	.L635
	b	.L634
.L643:
	mov	r4, r3
.L634:
	movw	r3, #65535
	cmp	r4, r3
	beq	.L630
	mov	r9, r4, asl #1
	ldrh	sl, [r1, r9]
	cmp	sl, r6
	bls	.L636
	bl	GetFreeBlockMinEraseCount
	cmp	r0, r6
	ldrhi	r3, .L647
	strhi	r7, [r3, #-1784]
.L636:
	cmp	sl, r5
	bcs	.L646
	add	r3, sl, #128
	cmp	r8, r3
	ble	.L646
	add	r3, sl, #256
	ldr	r6, .L647
	cmp	r3, r5
	bcc	.L637
	ldr	r3, [r6, #-1788]
	add	sl, sl, #768
	cmp	sl, r3
	bcs	.L646
.L637:
	ldr	r3, [r6, #-2020]
	mov	r1, r4
	ldr	r0, .L647+16
	mov	r2, r5
	ldrh	r3, [r3, r9]
	str	r3, [sp, #0]
	ldr	r3, [r6, #-2084]
	ldrh	r3, [r3, r9]
	stmib	sp, {r3, r8}
	ldr	r3, [r6, #-1788]
	bl	printk
	mov	r3, #1
	str	r3, [r6, #-1188]
	b	.L630
.L646:
	movw	r4, #65535
.L630:
	mov	r0, r4
	add	sp, sp, #20
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, sl, fp, pc}
.L648:
	.align	2
.L647:
	.word	.LANCHOR2
	.word	.LANCHOR0
	.word	-2004
	.word	-1431655765
	.word	.LC80
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
	beq	.L650
	ldr	r2, .L651
	mov	r3, r0, asl #1
	mov	r1, #0
	ldr	r2, [r2, #-2020]
	strh	r1, [r2, r3]	@ movhi
	bl	INSERT_FREE_LIST
.L650:
	mov	r0, #0
	ldmfd	sp!, {r3, pc}
.L652:
	.align	2
.L651:
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
	ldr	r2, .L661
	mov	r3, #0
	stmfd	sp!, {r4, r5, r6, r7, r8, r9, sl, fp, lr}
	.save {r4, r5, r6, r7, r8, r9, sl, fp, lr}
	movw	sl, #3844
	str	r3, [r2, #-1184]
	mov	r8, #12
	ldr	r0, .L661+4
	mov	r7, #1
	movw	r6, #3922
	movw	fp, #3924
	b	.L654
.L655:
	mul	r1, r8, r3
	ldr	r4, [r2, #-1180]
	add	ip, r4, r1
	str	r7, [ip, #8]
	ldrh	ip, [r0, r6]
	mul	ip, ip, r3
	add	r5, ip, #3
	cmp	ip, #0
	movlt	ip, r5
	ldr	r5, [r2, #-1176]
	bic	ip, ip, #3
	add	ip, r5, ip
	str	ip, [r4, r1]
	ldrh	ip, [r0, fp]
	ldr	r9, [r2, #-1180]
	mul	ip, ip, r3
	add	r4, r9, r1
	add	r5, ip, #3
	cmp	ip, #0
	movlt	ip, r5
	ldr	r5, [r2, #-1172]
	bic	ip, ip, #3
	add	ip, r5, ip
	str	ip, [r4, #4]
	ldr	ip, [r2, #-1168]
	mov	r5, #36
	ldr	r1, [r9, r1]
	mla	ip, r5, r3, ip
	add	r3, r3, #1
	uxth	r3, r3
	str	r1, [ip, #8]
	ldr	r1, [r4, #4]
	str	r1, [ip, #12]
.L654:
	ldrh	r1, [r0, sl]
	cmp	r3, r1
	bcc	.L655
	b	.L660
.L657:
	mul	r2, r8, r1
	ldr	r4, [r3, #-1180]
	add	r0, r4, r2
	str	r7, [r0, #8]
	ldrh	r0, [ip, r6]
	mul	r0, r0, r1
	add	sl, r0, #3
	cmp	r0, #0
	movlt	r0, sl
	ldr	sl, [r3, #-1176]
	bic	r0, r0, #3
	add	r0, sl, r0
	str	r0, [r4, r2]
	ldr	r0, [r3, #-1180]
	add	r0, r0, r2
	ldrh	r2, [ip, r5]
	mul	r2, r2, r1
	add	r1, r1, #1
	uxth	r1, r1
	add	r4, r2, #3
	cmp	r2, #0
	movlt	r2, r4
	ldr	r4, [r3, #-1172]
	bic	r2, r2, #3
	add	r2, r4, r2
	str	r2, [r0, #4]
	b	.L659
.L660:
	ldr	r3, .L661
	mov	r8, #12
	ldr	ip, .L661+4
	mov	r7, #0
	movw	r6, #3922
	movw	r5, #3924
.L659:
	ldr	r2, [r3, #-1164]
	cmp	r1, r2
	bcc	.L657
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, sl, fp, pc}
.L662:
	.align	2
.L661:
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
	ldr	r3, .L670
	stmfd	sp!, {r4, r5, r6, r7, r8, r9, sl, fp, lr}
	.save {r4, r5, r6, r7, r8, r9, sl, fp, lr}
	mov	r5, #36
	ldr	r6, [r3, #-1164]
	mov	fp, #12
	ldr	ip, [r3, #-1180]
	mov	r3, #0
	mov	r4, r3
	b	.L664
.L667:
	mul	sl, fp, r2
	add	r8, ip, sl
	ldr	r9, [ip, sl]
	ldr	sl, [r7, #8]
	cmp	r9, sl
	streq	r4, [r8, #8]
	beq	.L666
.L665:
	add	r2, r2, #1
	uxth	r2, r2
.L669:
	cmp	r2, r6
	bcc	.L667
.L666:
	add	r3, r3, #1
	uxth	r3, r3
.L664:
	cmp	r3, r1
	ldmcsfd	sp!, {r4, r5, r6, r7, r8, r9, sl, fp, pc}
	mla	r7, r5, r3, r0
	mov	r2, #0
	b	.L669
.L671:
	.align	2
.L670:
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
	ldr	r3, .L679
	stmfd	sp!, {r4, r5, r6, r7, r8, sl, lr}
	.save {r4, r5, r6, r7, r8, sl, lr}
	mov	r6, #12
	ldr	r8, [r3, #-1164]
	mov	r5, #1
	ldr	r7, [r3, #-1180]
	mov	r4, #36
	mov	r3, #0
	b	.L673
.L676:
	mla	ip, r6, r2, r7
	ldr	sl, [ip, #8]
	cmp	sl, #0
	bne	.L674
	mla	r2, r4, r3, r0
	ldr	sl, [ip, #0]
	str	r5, [ip, #8]
	str	sl, [r2, #8]
	ldr	ip, [ip, #4]
	str	ip, [r2, #12]
	b	.L675
.L674:
	add	r2, r2, #1
	uxth	r2, r2
	b	.L677
.L678:
	mov	r2, #0
.L677:
	cmp	r2, r8
	bcc	.L676
.L675:
	add	r3, r3, #1
	uxth	r3, r3
.L673:
	cmp	r3, r1
	bcc	.L678
	ldmfd	sp!, {r4, r5, r6, r7, r8, sl, pc}
.L680:
	.align	2
.L679:
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
	ldr	r3, .L686
	ldr	r2, .L686+4
	ldrh	r1, [r3, r2]
	ldr	r2, [r3, #-1156]
	mov	r3, #0
	b	.L682
.L684:
	ldrh	ip, [r2], #2
	cmp	ip, r0
	beq	.L685
	add	r3, r3, #1
	uxth	r3, r3
.L682:
	cmp	r3, r1
	bne	.L684
	mov	r0, #0
	bx	lr
.L685:
	mov	r0, #1
	bx	lr
.L687:
	.align	2
.L686:
	.word	.LANCHOR2
	.word	-1160
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
	ldr	r3, .L692
	ldr	r2, .L692+4
	ldrh	ip, [r3, r2]
	ldr	r2, [r3, #-1156]
	mov	r3, #0
	mov	r1, r2
	b	.L689
.L691:
	ldrh	r7, [r1], #2
	cmp	r7, r0
	beq	.L690
	add	r3, r3, #1
	uxth	r3, r3
.L689:
	cmp	r3, ip
	bne	.L691
	mov	r3, r3, asl #1
	strh	r0, [r2, r3]	@ movhi
	ldr	r2, .L692
	ldr	r3, .L692+4
	ldrh	r1, [r2, r3]
	add	r1, r1, #1
	strh	r1, [r2, r3]	@ movhi
.L690:
	ldr	r3, .L692
	mov	r0, #12
	ldr	r2, .L692+8
	ldrh	r1, [r3, r2]
	mul	r1, r0, r1
	ldr	r0, [r3, #-1152]
	add	ip, r0, r1
	stmib	ip, {r5, r6}
	str	r4, [r0, r1]
	ldrh	r1, [r3, r2]
	add	r1, r1, #1
	strh	r1, [r3, r2]	@ movhi
	ldmfd	sp!, {r3, r4, r5, r6, r7, pc}
.L693:
	.align	2
.L692:
	.word	.LANCHOR2
	.word	-1160
	.word	-1148
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
	ldr	r0, .L697
	bl	printk
	ldr	r3, .L697+4
	ldr	ip, .L697+8
	ldrh	r5, [r3, ip]
	cmp	r5, r4
	beq	.L695
	ldr	r2, .L697+12
	ldrh	r0, [r3, r2]
	cmp	r0, r4
	beq	.L695
	movw	r1, #65535
	cmp	r5, r1
	streqh	r4, [r3, ip]	@ movhi
	beq	.L695
	cmp	r0, r1
	streqh	r4, [r3, r2]	@ movhi
.L695:
	mov	r0, #0
	ldmfd	sp!, {r3, r4, r5, pc}
.L698:
	.align	2
.L697:
	.word	.LC81
	.word	.LANCHOR2
	.word	-1146
	.word	-1144
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
	.word	.LC81
	.word	.LANCHOR2
	.word	-1146
	.word	-1144
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
	ldr	r7, .L709
	mov	r2, r4
	ldr	r6, .L709+4
	ldrh	r1, [r7, r6]
	mov	r5, r0
	ldr	r0, .L709+8
	bl	printk
	mov	r0, r5
	bl	FtlGcRefreshBlock
	ldr	r3, .L709+12
	ldr	r3, [r3, #3836]
	cmp	r3, #0
	beq	.L705
	ldr	r3, [r7, #-2084]
	mov	r5, r5, asl #1
	ldrh	r2, [r3, r5]
	cmp	r2, #29
	subhi	r2, r2, #30
	strhih	r2, [r3, r5]	@ movhi
.L705:
	ldrh	r1, [r7, r6]
	mov	r3, #0
	ldr	r2, .L709+16
	b	.L706
.L708:
	ldrh	r0, [r2, #2]!
	cmp	r0, r4
	beq	.L707
	add	r3, r3, #1
	uxth	r3, r3
.L706:
	cmp	r3, r1
	bne	.L708
	cmp	r3, #15
	bhi	.L707
	ldr	r2, .L709
	ldr	r1, .L709+20
	add	r0, r2, r3, asl #1
	add	r3, r3, #1
	strh	r4, [r0, r1]	@ movhi
	sub	r1, r1, #2
	strh	r3, [r2, r1]	@ movhi
.L707:
	mov	r0, #0
	ldmfd	sp!, {r3, r4, r5, r6, r7, pc}
.L710:
	.align	2
.L709:
	.word	.LANCHOR2
	.word	-1142
	.word	.LC82
	.word	.LANCHOR0
	.word	.LANCHOR2-1142
	.word	-1140
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
	ldr	r2, .L714
	ldr	r3, .L714+4
	ldrh	r2, [r3, r2]
	cmp	r2, #0
	beq	.L712
	ldr	r1, .L714+8
	ldrh	r0, [r3, r1]
	movw	r1, #65535
	cmp	r0, r1
	bne	.L712
	ldr	r4, .L714+12
	ldr	r5, .L714+4
	ldrh	r1, [r3, r4]
	cmp	r1, r2
	movcs	r2, #0
	strcsh	r2, [r3, r4]	@ movhi
	ldrh	r2, [r5, r4]
	ldr	r3, .L714+16
	add	r2, r5, r2, asl #1
	ldrh	r0, [r2, r3]
	bl	P2V_block_in_plane
	bl	FtlGcRefreshBlock
	ldrh	r3, [r5, r4]
	add	r3, r3, #1
	strh	r3, [r5, r4]	@ movhi
.L712:
	mov	r0, #0
	ldmfd	sp!, {r3, r4, r5, pc}
.L715:
	.align	2
.L714:
	.word	-1142
	.word	.LANCHOR2
	.word	-1146
	.word	-1106
	.word	-1140
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
	beq	.L717
	bl	memset
.L717:
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
	ldr	r4, .L719
	movw	r5, #3916
	ldr	r2, .L719+4
	mov	r1, #255
	ldr	r6, .L719+8
	ldr	r0, [r4, #-1156]
	strh	r3, [r4, r2]	@ movhi
	add	r2, r2, #12
	strh	r3, [r4, r2]	@ movhi
	ldrh	r2, [r6, r5]
	mov	r2, r2, asl #1
	bl	ftl_memset
	ldrh	r3, [r6, r5]
	mov	r2, #12
	ldr	r0, [r4, #-1152]
	mov	r1, #255
	mul	r2, r2, r3
	bl	ftl_memset
	ldmfd	sp!, {r4, r5, r6, lr}
	b	FtlGcBufInit
.L720:
	.align	2
.L719:
	.word	.LANCHOR2
	.word	-1160
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
	movw	r3, #3854
	ldr	r7, .L731
	mov	r2, #6
	ldr	r4, .L731+4
	mov	r1, #0
	mov	r6, #0
	ldrh	r3, [r7, r3]
	mov	sl, r6
	ldr	r0, [r4, #-2028]
	mul	r2, r2, r3
	bl	ftl_memset
	ldr	r3, .L731+8
	str	r6, [r4, #-2008]
	str	r6, [r4, #-2024]
	strh	r6, [r4, r3]	@ movhi
	add	r3, r3, #8
	str	r6, [r4, #-2016]
	strh	r6, [r4, r3]	@ movhi
	add	r3, r3, #900
	strh	r6, [r4, r3]	@ movhi
	mov	r4, r6
	b	.L722
.L724:
	add	r1, r7, r1
	add	r9, r9, #1
	ldrb	r0, [r1, #3870]	@ zero_extendqisi2
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
	b	.L729
.L730:
	mov	r5, #0
	uxth	r3, r4
	mov	r9, r5
	movw	fp, #3844
	movw	r2, #3912
.L729:
	ldrh	r0, [r7, fp]
	sxth	r1, r9
	cmp	r1, r0
	blt	.L724
	cmp	r5, #0
	ldr	r9, .L731+4
	beq	.L725
	sxth	r1, r5
	mov	r0, #32768
	bl	__aeabi_idiv
	uxth	r5, r0
	b	.L726
.L725:
	sxth	r3, r4
	ldr	r2, [r9, #-2020]
	mvn	r1, #0
	mov	r3, r3, asl #1
	strh	r1, [r2, r3]	@ movhi
.L726:
	sxth	r1, r4
	ldr	r0, [r9, #-2028]
	ldr	r3, .L731+4
	mov	r2, r1, asl #1
	add	r1, r2, r1
	add	r1, r0, r1, asl #1
	strh	r5, [r1, #4]	@ movhi
	ldr	r1, .L731+12
	ldrh	r1, [r9, r1]
	cmp	r8, r1
	beq	.L727
	ldr	r1, .L731+16
	ldrh	r1, [r3, r1]
	cmp	r8, r1
	beq	.L727
	ldr	r1, .L731+20
	ldrh	r1, [r3, r1]
	cmp	r8, r1
	beq	.L727
	ldr	r3, [r3, #-2020]
	uxth	r0, r4
	ldrh	r3, [r3, r2]
	cmp	r3, #0
	bne	.L728
	add	r6, r6, #1
	uxth	r6, r6
	bl	INSERT_FREE_LIST
	b	.L727
.L728:
	add	sl, sl, #1
	uxth	sl, sl
	bl	INSERT_DATA_LIST
.L727:
	add	r4, r4, #1
	uxth	r4, r4
.L722:
	movw	r2, #3852
	sxth	r8, r4
	ldrh	r3, [r7, r2]
	cmp	r8, r3
	blt	.L730
	ldr	r3, .L731+4
	mov	r0, #0
	ldr	r2, .L731+8
	strh	sl, [r3, r2]	@ movhi
	add	r2, r2, #8
	strh	r6, [r3, r2]	@ movhi
	ldmfd	sp!, {r1, r2, r3, r4, r5, r6, r7, r8, r9, sl, fp, pc}
.L732:
	.align	2
.L731:
	.word	.LANCHOR0
	.word	.LANCHOR2
	.word	-2012
	.word	-2000
	.word	-1952
	.word	-1904
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
	ldr	r4, .L736
	movw	r6, #3922
	ldr	r5, .L736+4
	movw	r7, #3950
	mov	r8, #12
	ldr	r2, [r4, #3940]
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
	b	.L734
.L735:
	mul	r3, r8, r2
	ldr	r1, [r5, #-1856]
	add	sl, r1, r3
	str	ip, [sl, #4]
	strh	r0, [r1, r3]	@ movhi
	ldr	r1, [r5, #-1856]
	ldr	sl, [r5, #-1096]
	add	r3, r1, r3
	ldrh	r1, [r4, r6]
	mul	r1, r2, r1
	add	r2, r2, #1
	uxth	r2, r2
	bic	r1, r1, #3
	add	r1, sl, r1
	str	r1, [r3, #8]
.L734:
	ldrh	r3, [r4, r7]
	ldr	r1, .L736
	cmp	r3, r2
	ldr	r3, .L736+4
	bhi	.L735
	ldr	r0, .L736+8
	mvn	r2, #0
	ldr	ip, [r1, #3940]
	strh	r2, [r3, r0]	@ movhi
	sub	r0, r0, #2
	strh	r2, [r3, r0]	@ movhi
	add	r0, r0, #10
	strh	ip, [r3, r0]	@ movhi
	ldr	ip, .L736+12
	ldr	r0, .L736+16
	strh	ip, [r3, r0]	@ movhi
	add	r0, r0, #40
	ldrh	ip, [r3, r0]
	sub	r0, r0, #36
	strh	ip, [r3, r0]	@ movhi
	movw	r0, #3948
	ldrh	r0, [r1, r0]
	ldr	r1, .L736+20
	strh	r0, [r3, r1]	@ movhi
	ldr	r1, [r3, #-1044]
	str	r1, [r3, #-1080]
	ldr	r1, [r3, #-1040]
	str	r1, [r3, #-1076]
	ldr	r1, [r3, #-1100]
	str	r1, [r3, #-1072]
	ldr	r1, [r3, #-1036]
	str	r1, [r3, #-1068]
	ldr	r1, .L736+24
	strh	r2, [r3, r1]	@ movhi
	ldmfd	sp!, {r3, r4, r5, r6, r7, r8, sl, pc}
.L737:
	.align	2
.L736:
	.word	.LANCHOR0
	.word	.LANCHOR2
	.word	-1090
	.word	-3902
	.word	-1088
	.word	-1086
	.word	-1052
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
	b	.L739
.L743:
	ldr	r0, [r7, r3, asl #2]
	mov	r2, #0
	ubfx	r0, r0, #10, #16
	b	.L740
.L742:
	mov	r1, r2, asl #1
	add	r2, r2, #1
	ldrh	ip, [r6, r1]
	uxth	r2, r2
	cmp	ip, r0
	ldreqh	ip, [r5, r1]
	addeq	ip, ip, #1
	streqh	ip, [r5, r1]	@ movhi
.L740:
	ldrh	r1, [r4, #10]
	cmp	r1, r2
	bhi	.L742
	add	r3, r3, #1
	uxth	r3, r3
.L739:
	ldrh	r2, [r4, #6]
	cmp	r2, r3
	bhi	.L743
	mov	sl, #0
	ldrh	fp, [r5, #0]
	mov	r7, sl
	ldr	r3, .L749
	movw	r2, #3914
	b	.L744
.L748:
	ldrh	r1, [r4, #0]
	cmp	r1, r7
	bne	.L745
	ldrh	r0, [r4, #2]
	ldrh	r1, [r3, r2]
	cmp	r0, r1
	movcc	r0, r7, asl #1
	strcch	r1, [r5, r0]	@ movhi
.L745:
	mov	r9, r7, asl #1
	ldrh	r8, [r5, r9]
	cmp	fp, r8
	movhi	sl, r7
	movhi	fp, r8
	cmp	r8, #0
	bne	.L747
	ldrh	r0, [r6, r9]
	cmp	r0, #0
	beq	.L747
	mov	r1, #1
	stmia	sp, {r2, r3}
	bl	FtlFreeSysBlkQueueIn
	strh	r8, [r6, r9]	@ movhi
	ldmia	sp, {r2, r3}
	ldrh	r1, [r4, #8]
	sub	r1, r1, #1
	strh	r1, [r4, #8]	@ movhi
.L747:
	add	r7, r7, #1
	uxth	r7, r7
.L744:
	ldrh	r1, [r4, #10]
	cmp	r1, r7
	bhi	.L748
	mov	r0, sl
	ldmfd	sp!, {r1, r2, r3, r4, r5, r6, r7, r8, r9, sl, fp, pc}
.L750:
	.align	2
.L749:
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
	ldr	r3, .L752
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
	ldr	r0, .L752+4
	bl	ftl_memset
	mov	r0, r4
	ldmfd	sp!, {r4, pc}
.L753:
	.align	2
.L752:
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
	ldr	r3, .L755
	movw	r2, #3976
	mvn	r1, #0
	ldr	r0, .L755+4
	strh	r1, [r3, r2]	@ movhi
	add	r2, r2, #6
	mov	r1, #0
	strh	r1, [r3, r2]	@ movhi
	mov	r1, #255
	mov	r2, #16
	b	ftl_memset
.L756:
	.align	2
.L755:
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
	ldr	r3, .L761
	ldr	r2, .L761+4
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
.L759:
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
	bne	.L759
	ldmfd	sp!, {r4, r5, r6, pc}
.L762:
	.align	2
.L761:
	.word	-1032
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
	ldr	r5, .L764
	mov	r4, #0
	ldr	r2, .L764+4
	mov	r1, r4
	ldr	r6, .L764+8
	movw	r7, #3854
	str	r3, [r5, #-1012]
	strh	r3, [r5, r2]	@ movhi
	movw	r3, #3958
	strh	r4, [r6, r3]	@ movhi
	movw	r3, #3932
	ldrh	r2, [r6, r3]
	ldr	r0, [r6, #3960]
	str	r4, [r5, #-1028]
	mov	r2, r2, asl #1
	str	r4, [r5, #-1020]
	str	r4, [r5, #-1016]
	str	r4, [r6, #3836]
	bl	ftl_memset
	ldrh	r2, [r6, r7]
	mov	r1, r4
	ldr	r0, [r5, #-2084]
	mov	r2, r2, asl #1
	bl	ftl_memset
	ldrh	r2, [r6, r7]
	mov	r1, r4
	ldr	r0, [r5, #-1008]
	mov	r2, r2, asl #1
	bl	ftl_memset
	mov	r1, r4
	mov	r2, #48
	ldr	r0, .L764+12
	bl	ftl_memset
	mov	r1, r4
	mov	r2, #512
	sub	r0, r5, #1712
	bl	ftl_memset
	bl	FtlGcBufInit
	bl	FtlL2PDataInit
	mov	r0, r4
	ldmfd	sp!, {r3, r4, r5, r6, r7, pc}
.L765:
	.align	2
.L764:
	.word	.LANCHOR2
	.word	-1024
	.word	.LANCHOR0
	.word	.LANCHOR2-2076
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
	ldr	r4, .L797
	mov	r0, #1024
	ldr	r3, .L797+4
	mov	r7, #12
	ldr	r2, .L797+8
	movw	sl, #3844
	ldr	r5, .L797+12
	mov	r8, #36
	strh	r6, [r4, r3]	@ movhi
	movw	r3, #65535
	str	r3, [r4, #-996]
	mvn	r3, #0
	strh	r3, [r4, r2]	@ movhi
	add	r2, r2, #2
	str	r6, [r4, #-1004]
	strh	r3, [r4, r2]	@ movhi
	mov	r2, #32
	ldr	r3, .L797+16
	str	r6, [r4, #-1716]
	str	r6, [r4, #-1808]
	strh	r2, [r4, r3]	@ movhi
	add	r3, r3, #2
	mov	r2, #128
	str	r6, [r4, #-1804]
	strh	r2, [r4, r3]	@ movhi
	add	r3, r3, #6
	str	r6, [r4, #-1820]
	strh	r6, [r4, r3]	@ movhi
	add	r3, r3, #50
	str	r6, [r4, #-1832]
	strh	r6, [r4, r3]	@ movhi
	add	r3, r3, #158
	str	r6, [r4, #-1836]
	strh	r6, [r4, r3]	@ movhi
	sub	r3, r3, #122
	str	r6, [r4, #-1828]
	strh	r6, [r4, r3]	@ movhi
	movw	r3, #3918
	ldrh	r1, [r5, r3]
	str	r6, [r4, #-1824]
	str	r6, [r4, #-1840]
	str	r6, [r4, #-1800]
	str	r6, [r4, #-1796]
	str	r6, [r4, #-2080]
	str	r6, [r4, #-1788]
	str	r6, [r4, #-1784]
	str	r6, [r4, #-1000]
	str	r6, [r4, #-1188]
	str	r6, [r4, #-992]
	str	r6, [r4, #-1196]
	str	r6, [r4, #-988]
	bl	__aeabi_idiv
	movw	r3, #3844
	ldrh	r3, [r5, r3]
	str	r6, [r5, #3964]
	movw	r6, #3916
	mov	r3, r3, asl #2
	cmp	r0, r3
	str	r0, [r4, #-980]
	ldrh	r0, [r5, r6]
	strhi	r3, [r4, #-980]
	ldr	r4, .L797
	mov	r0, r0, asl #1
	bl	ftl_malloc
	str	r0, [r4, #-1156]
	ldrh	r0, [r5, r6]
	mul	r0, r7, r0
	bl	ftl_malloc
	ldrh	r6, [r5, sl]
	mul	r6, r8, r6
	mov	r9, r6, asl #3
	str	r0, [r4, #-1152]
	mov	r0, r9
	bl	ftl_malloc
	str	r0, [r4, #-976]
	mov	r0, r6
	bl	ftl_malloc
	str	r0, [r4, #-972]
	mov	r0, r9
	bl	ftl_malloc
	str	r0, [r4, #-968]
	mov	r0, r6
	bl	ftl_malloc
	str	r0, [r4, #-2088]
	mov	r0, r6
	bl	ftl_malloc
	str	r0, [r4, #-1168]
	ldr	r0, [r4, #-980]
	mul	r0, r8, r0
	bl	ftl_malloc
	movw	r8, #3922
	ldrh	r3, [r5, sl]
	ldrh	r6, [r5, r8]
	mov	r3, r3, asl #1
	add	r3, r3, #1
	str	r3, [r4, #-1164]
	str	r0, [r5, #3968]
	mov	r0, r6
	bl	ftl_malloc
	str	r0, [r4, #-964]
	mov	r0, r6
	bl	ftl_malloc
	str	r0, [r4, #-960]
	mov	r0, r6
	bl	ftl_malloc
	str	r0, [r4, #-956]
	ldr	r0, [r4, #-1164]
	mul	r0, r0, r6
	bl	ftl_malloc
	str	r0, [r4, #-1176]
	ldr	r0, [r4, #-980]
	mul	r0, r0, r6
	bl	ftl_malloc
	str	r0, [r4, #-952]
	mov	r0, r6
	bl	ftl_malloc
	str	r0, [r4, #-948]
	mov	r0, r6
	bl	ftl_malloc
	movw	r6, #3924
	str	r0, [r4, #-944]
	ldr	r0, [r4, #-1164]
	mul	r0, r7, r0
	bl	ftl_malloc
	ldrh	r3, [r5, r6]
	ldrh	sl, [r5, sl]
	mul	sl, sl, r3
	str	r0, [r4, #-1180]
	mov	r0, sl
	bl	ftl_malloc
	str	r0, [r4, #-940]
	mov	r0, sl, asl #3
	bl	ftl_malloc
	ldrh	r3, [r5, r6]
	ldr	sl, .L797+20
	str	r0, [r4, #-936]
	ldr	r0, [r4, #-1164]
	mul	r0, r0, r3
	bl	ftl_malloc
	ldrh	r3, [r5, r6]
	movw	r6, #3854
	str	r0, [r4, #-1172]
	ldr	r0, [r4, #-980]
	mul	r0, r0, r3
	bl	ftl_malloc
	str	r0, [r4, #-932]
	ldrh	r0, [r5, r6]
	mov	r0, r0, asl #1
	uxth	r0, r0
	strh	r0, [r4, sl]	@ movhi
	bl	ftl_malloc
	str	r0, [r4, #-1008]
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
	ldr	sl, [r5, #3940]
	mov	sl, sl, asl #1
	str	r0, [r4, #-2020]
	mov	r0, sl
	bl	ftl_malloc
	str	r0, [r4, #-1044]
	mov	r0, sl
	bl	ftl_malloc
	movw	sl, #3932
	str	r0, [r4, #-1100]
	ldrh	r0, [r5, r6]
	mov	r0, r0, lsr #3
	add	r0, r0, #4
	bl	ftl_malloc
	str	r0, [r4, #-1844]
	ldrh	r0, [r5, sl]
	mov	r0, r0, asl #1
	bl	ftl_malloc
	str	r0, [r5, #3960]
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
	movw	r3, #3948
	ldrh	sl, [r5, r3]
	mov	sl, sl, asl #2
	mov	r0, sl
	bl	ftl_malloc
	str	r0, [r4, #-1036]
	mov	r0, sl
	bl	ftl_malloc
	movw	sl, #3950
	str	r0, [r4, #-904]
	ldr	r0, [r5, #3940]
	mov	r0, r0, asl #2
	bl	ftl_malloc
	str	r0, [r4, #-1040]
	ldrh	r0, [r5, sl]
	mul	r0, r7, r0
	bl	ftl_malloc
	ldrh	r3, [r5, sl]
	movw	r7, #3866
	str	r0, [r4, #-1856]
	ldrh	r0, [r5, r8]
	mul	r0, r0, r3
	bl	ftl_malloc
	ldrh	r3, [r5, r6]
	ldr	r6, .L797+24
	str	r0, [r4, #-1096]
	mov	r0, #6
	mul	r0, r0, r3
	bl	ftl_malloc
	movw	r3, #3908
	ldrh	r3, [r5, r3]
	ldrh	r2, [r5, r7]
	add	r3, r3, #31
	mov	r3, r3, lsr #5
	strh	r3, [r4, r6]	@ movhi
	mul	r3, r2, r3
	str	r0, [r4, #-2028]
	mov	r0, r3, asl #2
	bl	ftl_malloc
	ldrh	r1, [r4, r6]
	mov	r3, #1
	ldr	ip, .L797+12
	mov	r1, r1, asl #2
	mov	r2, r1
	str	r0, [r5, #4004]
	ldrh	r5, [r5, r7]
	ldr	r0, .L797+28
	b	.L768
.L769:
	ldr	r4, [ip, #4004]
	add	r3, r3, #1
	add	r4, r4, r2
	add	r2, r2, r1
	str	r4, [r0, #4]!
.L768:
	cmp	r3, r5
	bcc	.L769
	ldr	r0, .L797+32
	mov	r2, #0
	mov	r1, r2
	add	r0, r0, r3, asl #2
	b	.L770
.L771:
	add	ip, r0, r2
	add	r3, r3, #1
	add	r2, r2, #4
	str	r1, [ip, #28]
.L770:
	cmp	r3, #7
	bls	.L771
	ldr	r3, .L797
	ldr	r2, [r3, #-1044]
	cmp	r2, #0
	beq	.L796
.L772:
	ldr	r2, [r3, #-1100]
	cmp	r2, #0
	beq	.L796
.L774:
	ldr	r2, [r3, #-1036]
	cmp	r2, #0
	beq	.L796
.L775:
	ldr	r2, [r3, #-1040]
	cmp	r2, #0
	beq	.L796
.L776:
	ldr	r2, [r3, #-1856]
	cmp	r2, #0
	beq	.L796
.L777:
	ldr	r2, [r3, #-1096]
	cmp	r2, #0
	beq	.L796
.L778:
	ldr	r2, [r3, #-2028]
	cmp	r2, #0
	beq	.L796
.L779:
	ldr	r2, .L797+12
	ldr	r2, [r2, #4004]
	cmp	r2, #0
	beq	.L796
.L780:
	ldr	r3, [r3, #-2020]
	cmp	r3, #0
	beq	.L796
.L781:
	ldr	r3, .L797
	ldr	r2, [r3, #-1156]
	cmp	r2, #0
	beq	.L796
	ldr	r2, [r3, #-1152]
	cmp	r2, #0
	beq	.L796
.L783:
	ldr	r2, [r3, #-976]
	cmp	r2, #0
	beq	.L796
	ldr	r2, [r3, #-968]
	cmp	r2, #0
	beq	.L796
	ldr	r2, [r3, #-2088]
	cmp	r2, #0
	beq	.L796
	ldr	r2, [r3, #-1168]
	cmp	r2, #0
	beq	.L796
	ldr	r2, [r3, #-972]
	cmp	r2, #0
	beq	.L796
.L785:
	ldr	r2, [r3, #-964]
	cmp	r2, #0
	beq	.L796
	ldr	r2, [r3, #-960]
	cmp	r2, #0
	beq	.L796
	ldr	r3, [r3, #-956]
	cmp	r3, #0
	beq	.L796
	ldr	r3, .L797
	ldr	r2, [r3, #-1176]
	cmp	r2, #0
	beq	.L796
	ldr	r2, [r3, #-948]
	cmp	r2, #0
	beq	.L796
	ldr	r2, [r3, #-944]
	cmp	r2, #0
	beq	.L796
	ldr	r2, [r3, #-1180]
	cmp	r2, #0
	beq	.L796
.L787:
	ldr	r2, [r3, #-940]
	cmp	r2, #0
	beq	.L796
	ldr	r2, [r3, #-936]
	cmp	r2, #0
	beq	.L796
	ldr	r2, [r3, #-1172]
	cmp	r2, #0
	beq	.L796
.L789:
	ldr	r2, [r3, #-2084]
	cmp	r2, #0
	beq	.L796
	ldr	r3, [r3, #-1008]
	cmp	r3, #0
	beq	.L796
.L791:
	ldr	r3, .L797+12
	ldr	r3, [r3, #3960]
	cmp	r3, #0
	beq	.L796
.L792:
	ldr	r3, .L797
	ldr	r2, [r3, #-916]
	cmp	r2, #0
	beq	.L796
.L793:
	ldr	r2, [r3, #-912]
	cmp	r2, #0
	beq	.L796
.L794:
	ldr	r3, [r3, #-908]
	cmp	r3, #0
	bne	.L795
.L796:
	ldr	r0, .L797+36
	ldr	r1, .L797+40
	bl	printk
	mvn	r0, #0
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, sl, pc}
.L795:
	mov	r0, #0
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, sl, pc}
.L798:
	.align	2
.L797:
	.word	.LANCHOR2
	.word	-1104
	.word	-1146
	.word	.LANCHOR0
	.word	-1200
	.word	-928
	.word	-1032
	.word	.LANCHOR0+4004
	.word	.LANCHOR0+3976
	.word	.LC83
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
	ldr	r3, .L802
	ldr	r2, [r3, #3624]
	ldr	r1, [r3, #856]
	ldrb	r2, [r2, #9]	@ zero_extendqisi2
	mul	r2, r1, r2
	mov	r1, #0
	uxth	r2, r2
	strb	r2, [r4, #4]
	mov	r2, r2, lsr #8
	strb	r2, [r4, #5]
	ldrb	r2, [r3, #3832]	@ zero_extendqisi2
	strb	r2, [r4, #7]
	ldr	r2, [r3, #3952]
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
	ldr	r1, .L802+4
	b	.L800
.L801:
	ldrb	r5, [r3, r1]	@ zero_extendqisi2
	add	r3, r3, #1
	ldrb	ip, [r4, #10]	@ zero_extendqisi2
	orr	ip, ip, r2, asl r5
	strb	ip, [r4, #10]
.L800:
	uxtb	ip, r3
	cmp	ip, r0
	bcc	.L801
	ldmfd	sp!, {r3, r4, r5, pc}
.L803:
	.align	2
.L802:
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
	ldr	r1, .L816
	mov	r2, #0
	stmfd	sp!, {r3, r4, r5, r6, r7, r8, sl, lr}
	.save {r3, r4, r5, r6, r7, r8, sl, lr}
	ldr	r3, .L816+4
	strb	r2, [r1, #-900]
	ldr	r1, .L816+8
	strb	r2, [r3, #3762]
	movw	r2, #2902
	ldrh	r2, [r1, r2]
	cmp	r2, #256
	str	r2, [r3, #856]
	movhi	r2, #512
	bhi	.L815
	cmp	r2, #128
	bls	.L806
	mov	r2, #256
.L815:
	str	r2, [r3, #856]
.L806:
	mov	r1, #0
	mov	r2, #8
	ldr	r0, .L816+12
	mov	r6, #0
	bl	ftl_memset
	mov	r1, #0
	mov	r2, #32
	ldr	r0, .L816+16
	bl	ftl_memset
	ldr	r0, .L816+20
	mov	r1, #0
	mov	r2, #128
	bl	ftl_memset
	ldr	r7, .L816+24
	ldr	r5, .L816+4
	mov	r8, r7
.L808:
	ldr	r4, [r5, #3624]
	mov	r1, r8
	add	r0, r4, #1
	ldrb	r2, [r4, #0]	@ zero_extendqisi2
	bl	FlashMemCmp8
	cmp	r0, #0
	bne	.L807
	ldrb	r3, [r5, #3762]	@ zero_extendqisi2
	add	r2, r5, r3, asl #2
	str	r0, [r2, #3588]
	add	r2, r5, r3
	add	r3, r3, #1
	strb	r3, [r5, #3762]
	strb	r6, [r2, #3764]
.L807:
	add	r6, r6, #1
	add	r8, r8, #8
	cmp	r6, #4
	bne	.L808
	ldr	r5, .L816+4
	ldr	r3, .L816
	ldrb	r2, [r5, #3762]	@ zero_extendqisi2
	strb	r2, [r3, #-900]
	ldrb	r3, [r4, #8]	@ zero_extendqisi2
	cmp	r3, #2
	bne	.L809
	add	sl, r4, #1
	mov	r6, #0
	mov	r8, r5
.L812:
	mov	r0, sl
	mov	r1, r7
	ldrb	r2, [r4, #0]	@ zero_extendqisi2
	bl	FlashMemCmp8
	cmp	r0, #0
	bne	.L810
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
.L810:
	add	r6, r6, #1
	add	r7, r7, #8
	cmp	r6, #4
	bne	.L812
.L809:
	ldr	r3, .L816+4
	ldrb	r1, [r4, #13]	@ zero_extendqisi2
	ldr	r2, .L816
	ldrb	r3, [r3, #3762]	@ zero_extendqisi2
	mul	r1, r1, r3
	ldrh	r3, [r4, #14]
	mul	r1, r3, r1
	ldr	r3, .L816+28
	strh	r1, [r2, r3]	@ movhi
	ldmfd	sp!, {r3, r4, r5, r6, r7, r8, sl, pc}
.L817:
	.align	2
.L816:
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
	bne	.L819
	ldr	r3, .L858
.L820:
	strh	r0, [r3, #2]!	@ movhi
	add	r0, r0, #1
	uxth	r0, r0
	cmp	r0, #256
	bne	.L820
	b	.L821
.L819:
	cmp	r0, #1
	bne	.L822
	ldr	r0, .L858+4
	mov	r2, #0
	mov	r3, r2
.L825:
	uxth	r1, r3
	cmp	r1, #3
	bls	.L823
	tst	r3, #1
	moveq	r1, #2
	movne	r1, #3
	rsb	r1, r1, r2
	uxth	r1, r1
.L823:
	add	r3, r3, #1
	strh	r1, [r2, r0]	@ movhi
	cmp	r3, #256
	add	r2, r2, #2
	bne	.L825
	b	.L821
.L822:
	cmp	r0, #2
	bne	.L826
	mov	r3, #0
	ldr	r0, .L858
	movw	r2, #65535
	mov	r1, r3
	b	.L857
.L829:
	cmp	r3, #1
	movls	r1, r3
	movhi	r1, r2
.L857:
	add	r3, r3, #1
	add	r2, r2, #2
	strh	r1, [r0, #2]!	@ movhi
	uxth	r3, r3
	uxth	r2, r2
	cmp	r3, #256
	bne	.L829
	b	.L821
.L826:
	cmp	r0, #3
	bne	.L830
	ldr	r0, .L858+4
	mov	r2, #0
	mov	r3, r2
.L833:
	uxth	r1, r3
	cmp	r1, #5
	bls	.L831
	tst	r3, #1
	moveq	r1, #4
	movne	r1, #5
	rsb	r1, r1, r2
	uxth	r1, r1
.L831:
	add	r3, r3, #1
	strh	r1, [r2, r0]	@ movhi
	cmp	r3, #256
	add	r2, r2, #2
	bne	.L833
	b	.L821
.L830:
	cmp	r0, #4
	mov	r2, #0
	bne	.L834
	ldr	r3, .L858+8
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
	ldr	r0, .L858+12
	strh	r1, [r3, r2]	@ movhi
	mov	r2, #3088
	mov	r1, #7
	strh	r1, [r3, r2]	@ movhi
	add	r2, r2, #2
	mov	r1, #8
	strh	r1, [r3, r2]	@ movhi
	mov	r2, #16
	mov	r3, r1
.L836:
	tst	r3, #1
	add	r3, r3, #1
	moveq	r1, #6
	movne	r1, #7
	cmp	r3, #256
	rsb	r1, r1, r2
	add	r2, r2, #2
	strh	r1, [r0, #2]!	@ movhi
	uxth	r2, r2
	bne	.L836
	b	.L821
.L834:
	cmp	r0, #5
	bne	.L837
	ldr	r1, .L858
	mov	r3, r2
.L838:
	strh	r3, [r1, #2]!	@ movhi
	add	r3, r3, #1
	uxth	r3, r3
	cmp	r3, #16
	bne	.L838
	ldr	r2, .L858+16
.L839:
	strh	r3, [r2, #2]!	@ movhi
	add	r3, r3, #2
	uxth	r3, r3
	cmp	r3, #496
	bne	.L839
	b	.L821
.L837:
	cmp	r0, #6
	bne	.L821
	ldr	r0, .L858
	mov	r3, r2
.L842:
	uxth	r1, r3
	cmp	r1, #5
	bls	.L840
	tst	r3, #1
	moveq	r1, #10
	movne	r1, #12
	rsb	r1, r1, r2
	uxth	r1, r1
.L840:
	add	r3, r3, #1
	add	r2, r2, #3
	cmp	r3, #256
	strh	r1, [r0, #2]!	@ movhi
	uxth	r2, r2
	bne	.L842
.L821:
	mov	r2, #1024
	ldr	r0, .L858+20
	mov	r1, #255
	uxth	r4, r4
	bl	ftl_memset
	ldr	r2, .L858
	mov	r3, #0
	ldr	r0, .L858+24
	b	.L843
.L844:
	ldrh	r1, [r2, #2]!
	add	r3, r3, #1
	uxth	r3, r3
	add	ip, r0, r1, asl #1
	sub	ip, ip, #896
	strh	r1, [ip, #0]	@ movhi
.L843:
	cmp	r3, r4
	bcc	.L844
	ldmfd	sp!, {r4, pc}
.L859:
	.align	2
.L858:
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
	b	.L862
.L863:
	ldr	r4, [r1, r3]
	add	ip, ip, #1
	str	r4, [r0, r3]
	add	r3, r3, #4
.L862:
	cmp	ip, r2
	bne	.L863
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
	ldr	r0, .L868
	b	.L866
.L867:
	mov	ip, r0, asl #5
	ldrb	r4, [r3, r2]	@ zero_extendqisi2
	add	ip, ip, r0, lsr #2
	add	r2, r2, #1
	add	ip, ip, r4
	eor	r0, r0, ip
.L866:
	cmp	r2, r1
	bne	.L867
	ldmfd	sp!, {r4, pc}
.L869:
	.align	2
.L868:
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
	ldr	r3, .L871
	ldr	r0, [r3, #0]
	b	jiffies_to_msecs
.L872:
	.align	2
.L871:
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
	ldr	r4, .L876
	mov	ip, r0
	mov	r2, r3
	ldr	r4, [r4, #128]
	add	r4, r4, #4096
	add	r1, r4, r1
	movne	r0, r1
	movne	r1, ip
	ldmfd	sp!, {r4, lr}
	b	memcpy
.L877:
	.align	2
.L876:
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
	ldr	r3, .L882
	ldr	r0, [r3, #3952]
	bx	lr
.L883:
	.align	2
.L882:
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
	b	.L888
.L894:
	cmp	r5, #0
	bne	.L889
	ldr	r0, .L896
	mov	r1, r8
	mov	r2, r6
	mov	r3, r4
	bl	printk
.L889:
	cmp	r7, #4
	ldreq	r0, .L896+4
	ldreq	r1, [r6, r4, asl #2]
	beq	.L895
	cmp	r7, #2
	moveq	r3, r4, asl #1
	ldreq	r0, .L896+4
	ldreqsh	r1, [r6, r3]
	ldrne	r0, .L896+4
	ldrneb	r1, [r6, r4]	@ zero_extendqisi2
.L895:
	add	r5, r5, #1
	bl	printk
	cmp	r5, #15
	bls	.L893
	ldr	r0, .L896+8
	mov	r5, #0
	ldr	r1, .L896+12
	bl	printk
.L893:
	add	r4, r4, #1
.L888:
	cmp	r4, sl
	bne	.L894
	ldr	r0, .L896+8
	ldr	r1, .L896+12
	ldmfd	sp!, {r3, r4, r5, r6, r7, r8, sl, lr}
	b	printk
.L897:
	.align	2
.L896:
	.word	.LC84
	.word	.LC85
	.word	.LC78
	.word	.LC86
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
	ldr	r6, .L921
	add	r0, r6, r0, asl #3
	ldr	r3, [r6, #3780]
	ldr	r4, [r0, #864]
	cmp	r3, #3
	bls	.L915
	ldr	r3, [r4, #16]
	tst	r3, #4
	beq	.L915
	mov	r0, r4
	bl	wait_for_nandc_xfer_completed
	ldr	r5, [r4, #16]
	ldr	r3, [r4, #8]
	ands	r5, r5, #2
	str	r3, [sp, #0]
	movne	r5, #0
	bne	.L919
	b	.L920
.L905:
	ldr	r3, [r6, #3780]
	cmp	r3, #5
	bls	.L902
	ldr	r3, [r4, #0]
	str	r3, [sp, #4]
	ldr	r3, [sp, #4]
	tst	r3, #8192
	beq	.L902
	ldr	r3, [sp, #4]
	tst	r3, #131072
	bne	.L903
.L902:
	add	r5, r5, #1
	bic	r3, r5, #-16777216
	cmp	r3, #0
	bne	.L919
	ldr	r2, [r4, #28]
	mov	r1, r5
	ldr	r3, [sp, #0]
	ubfx	r2, r2, #16, #5
	ldr	r0, .L921+4
	ubfx	r3, r3, #22, #6
	bl	printk
	ldr	r0, .L921+8
	mov	r1, r4
	mov	r2, #4
	mov	r3, #512
	bl	rknand_print_hex
.L919:
	ldr	r2, [r4, #28]
	ldr	r3, [sp, #0]
	ubfx	r2, r2, #16, #5
	ubfx	r3, r3, #22, #6
	cmp	r2, r3
	blt	.L905
.L903:
	ldr	r4, .L921
	ldr	r3, [r4, #3816]
	cmp	r3, #0
	beq	.L906
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
	b	.L906
.L909:
	ldr	r3, [r4, #8]
	add	r5, r5, #1
	str	r3, [sp, #0]
	bic	r3, r5, #-16777216
	cmp	r3, #0
	bne	.L920
	ldr	r2, [sp, #0]
	mov	r1, r5
	ldr	r3, [r4, #28]
	ldr	r0, .L921+12
	ubfx	r3, r3, #16, #5
	bl	printk
	ldr	r0, .L921+8
	mov	r1, r4
	mov	r2, #4
	mov	r3, #512
	bl	rknand_print_hex
.L920:
	ldr	r3, [sp, #0]
	tst	r3, #1048576
	beq	.L909
	ldr	r6, .L921
	ldr	r3, [r6, #3824]
	cmp	r3, #0
	beq	.L910
	mov	r0, r4
	bl	NandcSendDumpDataStart
.L910:
	ldr	r3, [r6, #3816]
	ldr	r5, .L921
	cmp	r3, #0
	beq	.L911
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
.L911:
	ldr	r3, .L921
	ldr	r3, [r3, #3824]
	cmp	r3, #0
	beq	.L906
	mov	r0, r4
	bl	NandcSendDumpDataDone
.L906:
	ldr	r3, .L921
	mov	r2, #0
	str	r2, [r3, #3816]
	b	.L898
.L915:
	ldr	r3, [r4, #8]
	str	r3, [sp, #0]
	ldr	r3, [sp, #0]
	tst	r3, #1048576
	beq	.L915
.L898:
	ldmfd	sp!, {r2, r3, r4, r5, r6, pc}
.L922:
	.align	2
.L921:
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
	stmfd	sp!, {r4, r5, r6, r7, r8, r9, sl, fp, lr}
	.save {r4, r5, r6, r7, r8, r9, sl, fp, lr}
	mov	r4, r3
	mov	r9, r3
	ldr	r3, .L961
	.pad #92
	sub	sp, sp, #92
	tst	r4, #63
	add	r3, r3, r0, asl #3
	mov	r7, r0
	mov	fp, r1
	mov	sl, r2
	ldr	r5, [sp, #128]
	ldr	r6, [r3, #864]
	bne	.L924
	cmp	r5, #0
	bne	.L925
	add	r0, sp, #24
	mov	r1, #255
	mov	r2, #64
	add	r5, sp, #24
	bl	ftl_memset
.L925:
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
	bne	.L926
	ldr	r1, .L961
	mov	r0, sl, lsr #1
	mov	r3, fp
	mov	r2, fp
	ldr	ip, [r1, #3828]
	mov	r4, r1
	cmp	ip, #24
	movhi	ip, #128
	movls	ip, #64
	b	.L928
.L929:
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
.L928:
	cmp	r2, r0
	add	r7, r3, ip
	ldr	r1, .L961
	bcc	.L929
	mov	r3, #0
	ldr	r0, [r1, #3828]
	mov	sl, sl, lsr #2
	ldr	r1, [r1, #3780]
	mov	r4, r3
	b	.L930
.L936:
	add	r2, r3, #8
	ldr	r2, [r6, r2, asl #2]
	str	r2, [sp, #20]
	ldr	r2, [sp, #20]
	tst	r2, #4
	bne	.L951
	ldr	r2, [sp, #20]
	ands	r2, r2, #32768
	bne	.L951
	cmp	r1, #5
	bls	.L932
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
	b	.L959
.L932:
	cmp	r1, #3
	bls	.L934
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
.L959:
	orr	r2, ip, r2, asl #5
.L934:
	cmp	r4, r2
	movcc	r4, r2
	b	.L931
.L951:
	mvn	r4, #0
.L931:
	add	r3, r3, #1
.L930:
	cmp	r3, sl
	bcs	.L926
	cmp	r0, #0
	bne	.L936
.L926:
	mov	r3, #0
	str	r3, [r6, #16]
	b	.L937
.L924:
	cmp	r1, #1
	mov	r8, #0
	bne	.L957
	b	.L938
.L941:
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
.L938:
	cmp	r8, sl
	bcc	.L941
	mov	r4, #0
	b	.L937
.L957:
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
	b	.L960
.L946:
	mov	r0, r7
	bl	NandcXferComp
	ldr	r3, [fp, #32]
	add	ip, r5, #2
	cmp	ip, sl
	str	r3, [sp, #20]
	bcs	.L943
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
.L943:
	ldr	r3, [sp, #20]
	tst	r3, #4
	mvnne	r4, #0
	bne	.L944
	ldr	r2, [sp, #20]
	ldr	r3, [sp, #20]
	ubfx	r2, r2, #3, #5
	ubfx	r3, r3, #27, #1
	orr	r3, r2, r3, asl #5
	cmp	r4, r3
	movcc	r4, r3
.L944:
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
.L960:
	cmp	ip, sl
	mov	r5, ip
	bcc	.L946
	mov	r6, fp
	ldr	fp, [sp, #12]
.L937:
	ldr	r3, .L961
	rsbs	fp, fp, #1
	movcc	fp, #0
	ldr	r3, [r3, #3780]
	cmp	r3, #5
	movls	fp, #0
	cmp	fp, #0
	beq	.L947
	ldr	r3, [r6, #0]
	and	r2, r3, #139264
	cmp	r2, #139264
	orreq	r3, r3, #131072
	streq	r3, [r6, #0]
	mvneq	r4, #0
.L947:
	mov	r0, r4
	add	sp, sp, #92
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, sl, fp, pc}
.L962:
	.align	2
.L961:
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
	ldr	r3, .L966
	subs	r4, r0, #0
	mov	r5, r1
	mov	r6, r2
	ldrb	r7, [r3, #2901]	@ zero_extendqisi2
	bne	.L964
	ldr	r3, .L966+4
	ldrb	r2, [r3, #853]	@ zero_extendqisi2
	ldr	r1, [r3, #856]
	mul	r2, r1, r2
	cmp	r5, r2
	bcs	.L964
	ldrb	r3, [r3, #852]	@ zero_extendqisi2
	cmp	r3, #0
	subeq	r7, r7, #2
	movne	r7, #4
.L964:
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
.L967:
	.align	2
.L966:
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
	ldr	r2, .L975
	mov	r7, r1
	mov	r8, r0
	ldrb	r1, [r2, #928]	@ zero_extendqisi2
	ldr	r3, [r2, #3624]
	cmp	r1, #0
	ldrb	r3, [r3, #19]	@ zero_extendqisi2
	beq	.L969
	ldr	r1, [r2, #3784]
	ldr	r2, .L975+4
	cmp	r1, r2
	ldmeqfd	sp!, {r3, r4, r5, r6, r7, r8, sl, pc}
.L969:
	sub	r2, r3, #5
	uxtb	r2, r2
	cmp	r3, #68
	cmpne	r2, #2
	bls	.L972
	cmp	r3, #35
	cmpne	r3, #19
	beq	.L972
	ldmfd	sp!, {r3, r4, r5, r6, r7, r8, sl, pc}
.L973:
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
	b	.L974
.L972:
	ldr	r6, .L975
	movw	sl, #65535
	ldr	r5, .L975+8
.L974:
	ldr	r3, [r6, #3624]
	ldrh	r3, [r3, #10]
	cmp	r3, r4
	bhi	.L973
	ldmfd	sp!, {r3, r4, r5, r6, r7, r8, sl, pc}
.L976:
	.align	2
.L975:
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
	ldr	r3, .L980
	subs	r4, r0, #0
	mov	r6, r1
	mov	r5, r2
	ldrb	r7, [r3, #2901]	@ zero_extendqisi2
	bne	.L978
	ldr	r3, .L980+4
	ldrb	r2, [r3, #853]	@ zero_extendqisi2
	ldr	r3, [r3, #856]
	mul	r3, r3, r2
	cmp	r1, r3
	movcc	r7, #4
.L978:
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
.L981:
	.align	2
.L980:
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
	ldr	r3, .L996
	mov	sl, r2
	ldr	fp, .L996
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
	b	.L984
.L989:
	add	r4, r4, #1
	ldrb	r1, [fp, #1]	@ zero_extendqisi2
	mov	r0, r6
	ldr	r2, .L996+4
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
	beq	.L986
	ldrb	r3, [ip, #3832]	@ zero_extendqisi2
	cmn	r5, #1
	moveq	r5, r0
	add	r3, r3, r3, asl #1
	cmp	r0, r3, lsr #2
	bcc	.L995
	mov	r7, #0
	mov	sl, r7
.L986:
	add	r8, r8, #1
.L984:
	cmp	r8, r9
	bcc	.L989
	b	.L988
.L995:
	mov	r5, r0
.L988:
	ldr	r3, .L996
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
.L997:
	.align	2
.L996:
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
	ldr	r4, .L1011
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
	beq	.L1010
	mov	r1, #3
	bl	__aeabi_idiv
	uxtb	r0, r0
.L1010:
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
	b	.L1001
.L1005:
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
	beq	.L1002
	cmn	r4, #1
	ldr	r2, [sp, #12]
	moveq	r4, r0
	cmp	r0, r2
	bcc	.L1009
	mov	sl, #0
	str	sl, [sp, #20]
.L1002:
	mov	r8, ip
.L1001:
	ldr	r2, .L1011+4
	ldrb	r3, [r2, #136]	@ zero_extendqisi2
	cmp	r8, r3
	bcc	.L1005
	b	.L1004
.L1009:
	mov	r4, r0
.L1004:
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
	bne	.L1007
	ldr	r0, .L1011+8
	mov	r1, r8
	ldr	r2, [sp, #16]
	mov	r3, r8
	str	r4, [sp, #0]
	bl	printk
.L1007:
	mov	r0, r4
	add	sp, sp, #28
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, sl, fp, pc}
.L1012:
	.align	2
.L1011:
	.word	.LANCHOR0
	.word	.LANCHOR2
	.word	.LC90
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
	ldr	ip, .L1022
	mov	r5, #1
	mvn	r4, #0
	ldr	fp, .L1022+4
	add	r3, ip, r8, asl #3
	ldrb	sl, [r3, #868]	@ zero_extendqisi2
	ldr	r2, [r3, #864]
	add	sl, sl, #8
	add	sl, r2, sl, asl #8
	b	.L1014
.L1018:
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
	beq	.L1015
	ldrb	r3, [ip, #3832]	@ zero_extendqisi2
	cmn	r4, #1
	moveq	r4, r0
	add	r3, r3, r3, asl #1
	cmp	r0, r3, lsr #2
	bcc	.L1021
	mov	r6, #0
	mov	r7, r6
.L1015:
	add	r5, r5, #1
.L1014:
	ldrb	r3, [fp, #136]	@ zero_extendqisi2
	add	r3, r3, #1
	cmp	r5, r3
	bcc	.L1018
	b	.L1017
.L1021:
	mov	r4, r0
.L1017:
	mov	r0, sl
	mov	r1, #0
	bl	SamsungSetRRPara
	ldr	r3, .L1022
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
.L1023:
	.align	2
.L1022:
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
	ldr	r3, .L1045
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
	bls	.L1025
	ldrb	sl, [r3, #3773]	@ zero_extendqisi2
	cmp	sl, #0
	beq	.L1026
	mov	r0, #0
	mov	sl, #1
	bl	NandcSetDdrMode
.L1026:
	mov	r3, #92
	str	r3, [r4, #8]
	mov	r3, #197
	str	r3, [r4, #8]
.L1025:
	mov	r6, #1
	mvn	r5, #0
	ldr	fp, .L1045
	b	.L1027
.L1036:
	ldrb	r3, [fp, #3761]	@ zero_extendqisi2
	mov	r0, r4
	uxtb	r1, r6
	sub	r3, r3, #67
	uxtb	r3, r3
	cmp	r3, #1
	bhi	.L1028
	bl	SandiskSetRRPara
	b	.L1029
.L1028:
	bl	ToshibaSetRRPara
.L1029:
	ldr	r2, .L1045
	ldrb	r3, [r2, #3761]	@ zero_extendqisi2
	cmp	r3, #34
	bne	.L1030
	ldr	r2, .L1045+4
	ldrb	r3, [r2, #136]	@ zero_extendqisi2
	sub	r3, r3, #3
	cmp	r6, r3
	moveq	r3, #179
	streq	r3, [r4, #8]
.L1030:
	cmp	sl, #0
	mov	r3, #38
	str	r3, [r4, #8]
	mov	r3, #93
	str	r3, [r4, #8]
	beq	.L1031
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
	b	.L1032
.L1031:
	mov	r3, r7
	mov	r0, r8
	ldr	r1, [sp, #4]
	mov	r2, r9
	bl	FlashReadRawPage
	mov	r3, r0
.L1032:
	cmn	r3, #1
	beq	.L1033
	ldrb	r2, [fp, #3832]	@ zero_extendqisi2
	cmn	r5, #1
	moveq	r5, r3
	add	r2, r2, r2, asl #1
	cmp	r3, r2, lsr #2
	bcc	.L1044
	mov	r7, #0
	mov	r9, r7
.L1033:
	add	r6, r6, #1
.L1027:
	ldr	r2, .L1045+4
	ldrb	r3, [r2, #136]	@ zero_extendqisi2
	add	r3, r3, #1
	cmp	r6, r3
	bcc	.L1036
	b	.L1035
.L1044:
	mov	r5, r3
.L1035:
	ldr	r6, .L1045
	mov	r0, r4
	mov	r1, #0
	ldrb	r3, [r6, #3761]	@ zero_extendqisi2
	sub	r3, r3, #67
	uxtb	r3, r3
	cmp	r3, #1
	bhi	.L1037
	bl	SandiskSetRRPara
	b	.L1038
.L1037:
	bl	ToshibaSetRRPara
.L1038:
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
	beq	.L1040
	mov	r0, #4
	bl	NandcSetDdrMode
.L1040:
	mov	r0, r5
	ldmfd	sp!, {r1, r2, r3, r4, r5, r6, r7, r8, r9, sl, fp, pc}
.L1046:
	.align	2
.L1045:
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
	ldr	r5, .L1054
	ldr	r7, .L1054+4
	ldr	r8, .L1054+8
	ldr	r3, [r5, #144]
	ldrb	r0, [r5, #148]	@ zero_extendqisi2
	str	r3, [r5, #140]
	bl	FlashBchSel
	mov	r1, #0
	mov	r2, #2048
	ldr	r0, [r5, #144]
	bl	ftl_memset
	ldr	r3, [r5, #140]
	ldr	r1, .L1054+12
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
	ldr	r1, .L1054+16
	mov	r2, #8
	bl	memcpy
	add	r0, r4, #96
	ldr	r1, .L1054+20
	mov	r2, #32
	bl	memcpy
	ldr	r0, [r5, #140]
	ldr	r1, .L1054+24
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
.L1050:
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
	beq	.L1048
	ldr	sl, [r5, #140]
	ldr	r3, [sl, #0]
	cmp	r3, r8
	bne	.L1048
	add	r0, sl, #12
	movw	r1, #2036
	bl	JSHash
	ldr	r3, [sl, #8]
	cmp	r3, r0
	bne	.L1048
	add	r3, r4, #1
	str	r3, [r5, #152]
	ldr	r3, [r7, #856]
	add	r6, r6, #1
	cmp	r6, #1
	mul	r3, r3, r4
	str	r3, [r5, #156]
	bhi	.L1049
.L1048:
	add	r4, r4, #1
	cmp	r4, #4
	bne	.L1050
.L1049:
	mov	r0, #0
	bl	flash_exit_slc_mode
	cmp	r6, #0
	mvneq	r0, #0
	movne	r0, #0
	ldmfd	sp!, {r3, r4, r5, r6, r7, r8, sl, pc}
.L1055:
	.align	2
.L1054:
	.word	.LANCHOR2
	.word	.LANCHOR0
	.word	1312902724
	.word	.LANCHOR0+2980
	.word	.LANCHOR0+3764
	.word	.LANCHOR0+3588
	.word	.LANCHOR1+2892
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
	ldr	r1, .L1067
	add	r0, sp, #12
	bl	memcpy
	ldr	r3, .L1067+4
	ldrb	r2, [r3, #3832]	@ zero_extendqisi2
	str	r2, [sp, #4]
	ldr	r2, [r3, #3784]
	ldr	r3, .L1067+8
	cmp	r2, r3
	bne	.L1057
	mov	r0, #0
	bl	flash_enter_slc_mode
.L1057:
	ldr	r7, .L1067+4
	mov	r0, sl
	mov	r1, #0
	mov	r2, #2048
	bl	ftl_memset
	mvn	r8, #0
	mov	r4, #2
	ldr	r9, .L1067+12
	mov	fp, r7
	b	.L1058
.L1065:
	mov	r6, #0
.L1060:
	add	r3, sp, #12
	ldr	r5, .L1067+12
	ldrb	r0, [r3, r6]	@ zero_extendqisi2
	bl	FlashBchSel
	ldr	r1, [r7, #856]
	mov	r0, #0
	ldr	r2, [r9, #144]
	mov	r3, r0
	mul	r1, r1, r4
	bl	FlashReadRawPage
	cmn	r0, #1
	bne	.L1059
	add	r6, r6, #1
	cmp	r6, #4
	bne	.L1060
	b	.L1061
.L1059:
	ldr	r3, [r5, #144]
	ldr	r2, [r3, #0]
	ldr	r3, .L1067+16
	cmp	r2, r3
	bne	.L1061
	add	r3, sp, #16
	ldr	r0, .L1067+20
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
	bls	.L1064
	str	r4, [r5, #152]
	bl	FlashSavePhyInfo
	mov	r8, #0
.L1061:
	add	r4, r4, #1
.L1058:
	ldrb	r3, [r7, #853]	@ zero_extendqisi2
	cmp	r4, r3
	bcc	.L1065
	b	.L1062
.L1064:
	mov	r8, #0
.L1062:
	ldr	r0, [sp, #4]
	bl	FlashBchSel
	ldr	r3, .L1067+4
	ldr	r2, [r3, #3784]
	ldr	r3, .L1067+8
	cmp	r2, r3
	bne	.L1063
	mov	r0, #0
	bl	flash_exit_slc_mode
.L1063:
	mov	r0, r8
	add	sp, sp, #20
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, sl, fp, pc}
.L1068:
	.align	2
.L1067:
	.word	.LANCHOR3+11
	.word	.LANCHOR0
	.word	1446522928
	.word	.LANCHOR2
	.word	-52655045
	.word	.LC91
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
	ldr	r1, .L1080
	add	r0, sp, #4
	bl	memcpy
	ldr	r5, .L1080+4
	ldr	r2, .L1080+8
	movw	r3, #2902
	mov	r4, #0
	mov	r8, #4
	mov	r0, r4
	str	r4, [r5, #156]
	ldrh	r9, [r2, r3]
	mvn	r6, #0
	ldr	r3, [r5, #144]
	ldr	fp, .L1080+12
	str	r3, [r5, #140]
	bl	flash_enter_slc_mode
	b	.L1070
.L1072:
	add	r3, sp, #4
	ldrb	r0, [r3, r7]	@ zero_extendqisi2
	bl	FlashBchSel
	mov	r0, #0
	mov	r1, r4
	ldr	r2, [r5, #144]
	mov	r3, r0
	bl	FlashReadRawPage
	cmn	r0, #1
	bne	.L1071
	mov	r0, #0
	mov	r1, sl
	ldr	r2, [r5, #144]
	mov	r3, r0
	bl	FlashReadRawPage
	cmn	r0, #1
	bne	.L1071
	add	r7, r7, #1
	cmp	r7, #4
	beq	.L1073
	b	.L1072
.L1071:
	ldr	r7, [r5, #140]
	ldr	r3, [r7, #0]
	cmp	r3, fp
	bne	.L1073
	cmp	r6, #0
	bne	.L1074
	ldr	r2, .L1080+8
	movw	r3, #2902
	mov	r0, r4
	ldrh	r1, [r2, r3]
	bl	__aeabi_uidiv
	ldr	r3, .L1080+4
	add	r0, r0, #1
	str	r0, [r3, #152]
	mov	r0, r6
	b	.L1079
.L1074:
	add	r0, r7, #12
	movw	r1, #2036
	bl	JSHash
	ldr	r3, [r7, #8]
	cmp	r3, r0
	bne	.L1073
	ldr	r6, .L1080+16
	add	r1, r7, #160
	mov	r2, #32
	ldr	r0, .L1080+20
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
	ldr	r2, .L1080+8
	mov	r0, r4
	str	r4, [r5, #156]
	strb	r3, [r6, #3773]
	movw	r3, #2902
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
.L1073:
	subs	r8, r8, #1
	add	r4, r4, r9
	beq	.L1077
.L1070:
	add	sl, r4, #1
	mov	r7, #0
	b	.L1072
.L1077:
	mov	r0, r8
.L1079:
	bl	flash_exit_slc_mode
	mov	r0, r6
	ldmfd	sp!, {r1, r2, r3, r4, r5, r6, r7, r8, r9, sl, fp, pc}
.L1081:
	.align	2
.L1080:
	.word	.LANCHOR3+11
	.word	.LANCHOR2
	.word	.LANCHOR1
	.word	1312902724
	.word	.LANCHOR0
	.word	.LANCHOR1+2892
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
	ldr	r4, .L1103
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
	beq	.L1084
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
	beq	.L1085
	ldr	r0, .L1103+4
	mov	r2, r8
	ldr	r1, [sp, #0]
	bl	printk
	cmp	r8, #9
	bhi	.L1086
	add	r4, r4, fp, asl #3
	ldr	r3, [r4, #864]
	ldr	r2, [r3, #3840]
	ldr	r2, [r3, #0]
	orr	r2, r2, #131072
	str	r2, [r3, #0]
.L1086:
	ldr	r3, .L1103+8
	ldr	r2, [r3, #164]
	add	r2, r2, #1
	str	r2, [r3, #164]
	cmp	r2, #2048
	movcs	sl, #0
	strcs	sl, [r3, #164]
	movcs	r9, sl
	bcc	.L1085
.L1084:
	mov	r4, #0
	str	fp, [sp, #4]
	mov	r5, r4
	mvn	r6, #0
	mov	fp, r4
	str	r4, [sp, #8]
.L1091:
	uxtb	r0, r7
	bl	NandcSetDdrPara
	mov	r3, sl
	ldr	r0, [sp, #4]
	mov	r2, r9
	ldr	r1, [sp, #0]
	bl	FlashReadRawPage
	add	r3, r8, #1
	cmp	r0, r3
	bhi	.L1087
	cmp	r0, #2
	bhi	.L1097
	add	r5, r5, #1
	cmp	r5, #9
	bls	.L1097
	mov	ip, fp
	rsb	r4, r5, r7
	ldr	fp, [sp, #4]
	mov	r8, r0
	mov	r6, #0
	b	.L1089
.L1087:
	cmp	fp, r5
	bcs	.L1098
	cmp	r5, #7
	rsb	r3, r5, r4
	str	r3, [sp, #8]
	bhi	.L1102
	mov	fp, r5
	b	.L1098
.L1097:
	mov	r6, #0
	mov	r4, r7
	mov	r8, r0
	mov	sl, r6
	mov	r9, r6
	b	.L1088
.L1098:
	mov	r5, #0
.L1088:
	add	r7, r7, #2
	cmp	r7, #69
	bls	.L1091
	mov	ip, fp
	ldr	fp, [sp, #4]
.L1089:
	cmp	ip, r5
	bcc	.L1092
	b	.L1090
.L1102:
	ldr	fp, [sp, #4]
.L1090:
	ldr	r4, [sp, #8]
.L1092:
	cmp	r4, #0
	beq	.L1093
	ldr	r0, .L1103+12
	mov	r1, r4
	bl	printk
	uxtb	r0, r4
	bl	NandcSetDdrPara
.L1093:
	cmn	r6, #1
	bne	.L1085
	ldr	r0, .L1103+16
	mov	r1, fp
	ldr	r2, [sp, #0]
	bl	printk
	ldr	r3, [sp, #56]
	cmp	r3, #0
	moveq	r8, r6
	beq	.L1085
	ldr	r3, [sp, #12]
	ubfx	r0, r3, #8, #8
	bl	NandcSetDdrPara
.L1085:
	mov	r0, r8
	add	sp, sp, #20
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, sl, fp, pc}
.L1104:
	.align	2
.L1103:
	.word	.LANCHOR0
	.word	.LC92
	.word	.LANCHOR2
	.word	.LC93
	.word	.LC94
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
	ldr	r5, .L1109
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
	beq	.L1106
	cmn	r8, #1
	bne	.L1107
.L1106:
	ldrb	r3, [r5, #3772]	@ zero_extendqisi2
	tst	r3, #1
	beq	.L1107
	mov	r0, #1
	bl	FlashSetInterfaceMode
	mov	r0, #1
	bl	NandcSetMode
	ldr	r3, .L1109
	mov	r2, #0
	strb	r2, [r3, #3773]
	b	.L1108
.L1107:
	mov	r3, #1
	strb	r3, [r5, #3773]
.L1108:
	mov	r0, #0
	ldmfd	sp!, {r2, r3, r4, r5, r6, r7, r8, pc}
.L1110:
	.align	2
.L1109:
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
	bne	.L1112
	ldr	r7, .L1117
	ldrb	r9, [r7, #860]	@ zero_extendqisi2
	cmp	r9, #0
	beq	.L1113
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
	bne	.L1112
.L1113:
	ldr	r7, .L1117
	ldrb	r3, [r7, #3773]	@ zero_extendqisi2
	cmp	r3, #0
	beq	.L1112
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
	beq	.L1114
	ldrb	r3, [r7, #3832]	@ zero_extendqisi2
	cmp	r0, r3, lsr #1
	bls	.L1112
.L1114:
	ubfx	r0, r9, #8, #8
	bl	NandcSetDdrPara
.L1112:
	ldr	r7, .L1117+4
	ldr	ip, [r7, #168]
	adds	r3, ip, #0
	movne	r3, #1
	cmn	r4, #1
	movne	r3, #0
	cmp	r3, #0
	beq	.L1115
	mov	r1, r6
	mov	r2, r8
	mov	r3, sl
	mov	r0, r5
	blx	ip
	mov	r2, r5
	mov	r3, r6
	mov	r4, r0
	ldr	r0, .L1117+8
	mov	r1, r4
	bl	printk
	cmn	r4, #1
	bne	.L1115
	ldr	r3, .L1117
	ldrb	r3, [r3, #928]	@ zero_extendqisi2
	cmp	r3, #0
	beq	.L1115
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
.L1115:
	mov	r0, r4
	ldmfd	sp!, {r2, r3, r4, r5, r6, r7, r8, r9, sl, pc}
.L1118:
	.align	2
.L1117:
	.word	.LANCHOR0
	.word	.LANCHOR2
	.word	.LC95
	.fnend
	.size	FlashReadPage, .-FlashReadPage
	.align	2
	.global	FlashReadSlc2KPages
	.type	FlashReadSlc2KPages, %function
FlashReadSlc2KPages:
	.fnstart
	@ args = 0, pretend = 0, frame = 16
	@ frame_needed = 0, uses_anonymous_args = 0
	ldr	r3, .L1132
	stmfd	sp!, {r4, r5, r6, r7, r8, r9, sl, fp, lr}
	.save {r4, r5, r6, r7, r8, r9, sl, fp, lr}
	mov	r8, r1
	.pad #28
	sub	sp, sp, #28
	mov	fp, r2
	ldrb	sl, [r3, #2901]	@ zero_extendqisi2
	mov	r4, r0
	mov	r7, #0
	ldr	r6, .L1132+4
	b	.L1120
.L1129:
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
	bcs	.L1122
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
	beq	.L1127
	ldr	r2, [r3, #8]
	cmn	r2, #1
	bne	.L1127
	ldr	r3, [r3, #0]
	cmn	r3, #1
	strne	r2, [r4, #0]
.L1127:
	ldr	r3, [r4, #0]
	cmn	r3, #1
	bne	.L1122
	ldr	r1, [r4, #4]
	ldr	r0, .L1132+8
	ldrb	r2, [r6, #3832]	@ zero_extendqisi2
	bl	printk
	ldr	r1, [r4, #8]
	cmp	r1, #0
	beq	.L1128
	ldr	r0, .L1132+12
	mov	r2, #4
	mov	r3, #8
	bl	rknand_print_hex
.L1128:
	ldr	r1, [r4, #12]
	cmp	r1, #0
	beq	.L1122
	mov	r2, #4
	ldr	r0, .L1132+16
	mov	r3, r2
	bl	rknand_print_hex
.L1122:
	add	r7, r7, #1
	add	r4, r4, #36
.L1120:
	cmp	r7, r8
	bne	.L1129
	mov	r0, #0
	add	sp, sp, #28
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, sl, fp, pc}
.L1133:
	.align	2
.L1132:
	.word	.LANCHOR1
	.word	.LANCHOR0
	.word	.LC96
	.word	.LC97
	.word	.LC98
	.fnend
	.size	FlashReadSlc2KPages, .-FlashReadSlc2KPages
	.align	2
	.global	FlashReadPages
	.type	FlashReadPages, %function
FlashReadPages:
	.fnstart
	@ args = 0, pretend = 0, frame = 40
	@ frame_needed = 0, uses_anonymous_args = 0
	ldr	r3, .L1176
	stmfd	sp!, {r4, r5, r6, r7, r8, r9, sl, fp, lr}
	.save {r4, r5, r6, r7, r8, r9, sl, fp, lr}
	.pad #52
	sub	sp, sp, #52
	ldrb	r3, [r3, #2901]	@ zero_extendqisi2
	mov	r9, r0
	str	r1, [sp, #28]
	str	r2, [sp, #32]
	str	r3, [sp, #24]
	ldr	r3, .L1176+4
	ldrb	r8, [r3, #852]	@ zero_extendqisi2
	ldrb	ip, [r3, #860]	@ zero_extendqisi2
	cmp	r8, #0
	str	ip, [sp, #36]
	moveq	sl, r8
	beq	.L1135
	bl	FlashReadSlc2KPages
	b	.L1136
.L1163:
	mov	r3, #36
	ldr	ip, [sp, #28]
	mul	r3, r3, sl
	add	r2, sp, #44
	ldr	r1, [sp, #32]
	ldr	fp, .L1176+4
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
	bcs	.L1138
	add	r3, fp, r3
	ldrb	r4, [r3, #3764]	@ zero_extendqisi2
	ldr	r3, .L1176+8
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
	bhi	.L1140
	add	r1, fp, r4
	cmp	r2, #7
	add	fp, fp, r4
	ldr	r2, .L1176+4
	ldrb	r3, [r1, #12]	@ zero_extendqisi2
	ldreqb	r3, [r1, #20]	@ zero_extendqisi2
	ldrb	r1, [fp, #3756]	@ zero_extendqisi2
	cmp	r1, r3
	beq	.L1140
	ldrb	r1, [r2, #1]	@ zero_extendqisi2
	mov	r0, r4
	add	r2, r2, #4
	bl	HynixSetRRPara
.L1140:
	mov	r0, r4
	mov	r5, r5, lsr #31
	bl	NandcFlashCs
	ldr	ip, [sp, #32]
	mov	r0, r4
	cmp	ip, #1
	orreq	r5, r5, #1
	str	r5, [sp, #20]
	cmp	r5, #0
	beq	.L1142
	ldr	r3, .L1176+4
	ldrb	r3, [r3, #928]	@ zero_extendqisi2
	cmp	r3, #0
	beq	.L1142
	bl	flash_enter_slc_mode
	b	.L1143
.L1142:
	bl	flash_exit_slc_mode
.L1143:
	ldr	fp, .L1176+4
.L1175:
	ldr	r1, [sp, #44]
	cmn	r1, #1
	cmpeq	r4, #255
	moveq	r3, #0
	movne	r3, #1
	moveq	r7, r3
	beq	.L1145
	cmp	r7, #0
	beq	.L1146
	ldr	r2, [fp, #856]
	mov	r0, r4
	add	r2, r1, r2
	bl	FlashReadDpCmd
	b	.L1147
.L1146:
	mov	r0, r4
	bl	FlashReadCmd
.L1147:
	mov	r0, r4
	bl	NandcWaitFlashReady
	cmp	r7, #0
	beq	.L1145
	mov	r0, r4
	ldr	r1, [sp, #44]
	bl	FlashReadDpDataOutCmd
.L1145:
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
	bne	.L1175
.L1148:
	cmp	r7, #0
	mov	r5, r0
	beq	.L1149
	ldr	r3, .L1176+4
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
.L1149:
	mov	r0, r4
	ldr	fp, .L1176+4
	bl	NandcFlashDeCs
	ldr	ip, [sp, #36]
	cmn	r5, #1
	strb	ip, [fp, #860]
	bne	.L1153
	ldrb	r3, [fp, #3773]	@ zero_extendqisi2
	cmp	r3, #0
	beq	.L1151
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
	beq	.L1152
	ldrb	r3, [fp, #3832]	@ zero_extendqisi2
	cmp	r0, r3, lsr #1
	bls	.L1169
.L1152:
	ubfx	r0, r7, #8, #8
	bl	NandcSetDdrPara
	cmn	r5, #1
	bne	.L1169
.L1151:
	ldr	r3, .L1176+8
	mov	r0, r4
	ldr	r1, [sp, #44]
	ldr	r2, [r6, #8]
	ldr	r7, [r3, #168]
	ldr	r3, [r6, #12]
	cmp	r7, #0
	beq	.L1154
	blx	r7
	cmn	r0, #1
	mov	r5, r0
	bne	.L1171
	ldr	r2, .L1176+4
	ldr	r3, [r2, #3624]
	ldrb	r3, [r3, #19]	@ zero_extendqisi2
	sub	r3, r3, #1
	uxtb	r3, r3
	cmp	r3, #6
	bhi	.L1156
	ldrb	r1, [r2, #1]	@ zero_extendqisi2
	mov	r0, r4
	add	r2, r2, #4
	mov	r3, #0
	bl	HynixSetRRPara
.L1156:
	ldr	r1, [sp, #44]
	mov	r0, r4
	ldr	r2, [r6, #8]
	ldr	r3, [r6, #12]
	bl	FlashReadRawPage
	ldr	r7, .L1176+4
	ldr	r1, [r6, #4]
	ldrb	r2, [r7, #3832]	@ zero_extendqisi2
	mov	r5, r0
	ldr	r0, .L1176+12
	mov	r3, r5
	bl	printk
	cmn	r5, #1
	bne	.L1171
	ldrb	r7, [r7, #928]	@ zero_extendqisi2
	cmp	r7, #0
	beq	.L1155
	ldr	ip, [sp, #20]
	mov	r0, r4
	cmp	ip, #0
	beq	.L1157
	bl	flash_enter_slc_mode
	b	.L1158
.L1157:
	bl	flash_exit_slc_mode
.L1158:
	ldr	r3, .L1176+8
	mov	r0, r4
	ldr	r1, [sp, #44]
	ldr	r2, [r6, #8]
	ldr	ip, [r3, #168]
	ldr	r3, [r6, #12]
	blx	ip
	mov	r5, r0
	b	.L1171
.L1154:
	bl	FlashReadRawPage
	mov	r5, r0
	b	.L1155
.L1169:
	mov	r7, #0
.L1153:
	ldr	r3, .L1176+4
	ldrb	r3, [r3, #3832]	@ zero_extendqisi2
	add	r3, r3, r3, asl #1
	cmp	r5, r3, lsr #2
	bls	.L1155
	ldr	r3, .L1176+8
	ldr	r3, [r3, #168]
	cmp	r3, #0
	moveq	r5, #256
	b	.L1155
.L1171:
	mov	r7, #0
.L1155:
	ldr	ip, [sp, #16]
	cmp	r5, #256
	cmnne	r5, #1
	movne	r2, #0
	str	r5, [r9, ip]
	strne	r2, [r9, ip]
	ldr	ip, [sp, #16]
	ldr	r3, [r9, ip]
	cmn	r3, #1
	bne	.L1160
	ldr	r2, .L1176+4
	ldr	r1, [r6, #4]
	ldr	r0, .L1176+16
	ldrb	r2, [r2, #3832]	@ zero_extendqisi2
	bl	printk
	ldr	r1, [r6, #12]
	cmp	r1, #0
	beq	.L1160
	mov	r2, #4
	ldr	r0, .L1176+20
	mov	r3, r2
	bl	rknand_print_hex
.L1160:
	cmp	r7, #0
	beq	.L1161
	ldr	r3, .L1176+4
	ldrb	r3, [r3, #3832]	@ zero_extendqisi2
	add	r3, r3, r3, asl #1
	cmp	r8, r3, lsr #2
	bls	.L1162
	ldr	r3, .L1176+8
	ldr	r3, [r3, #168]
	cmp	r3, #0
	moveq	r8, #256
.L1162:
	add	r3, sl, #1
	mov	r2, #36
	cmp	r8, #256
	cmnne	r8, #1
	mul	r3, r2, r3
	movne	r2, #0
	str	r8, [r9, r3]
	strne	r2, [r9, r3]
.L1161:
	ldr	ip, [sp, #20]
	add	sl, sl, r7
	cmp	ip, #0
	beq	.L1138
	ldr	r3, .L1176+4
	ldrb	r3, [r3, #928]	@ zero_extendqisi2
	cmp	r3, #0
	beq	.L1138
	mov	r0, r4
	bl	flash_exit_slc_mode
.L1138:
	add	sl, sl, #1
.L1135:
	ldr	ip, [sp, #28]
	cmp	sl, ip
	bcc	.L1163
	mov	r0, #0
.L1136:
	add	sp, sp, #52
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, sl, fp, pc}
.L1177:
	.align	2
.L1176:
	.word	.LANCHOR1
	.word	.LANCHOR0
	.word	.LANCHOR2
	.word	.LC99
	.word	.LC96
	.word	.LC98
	.fnend
	.size	FlashReadPages, .-FlashReadPages
	.align	2
	.global	FtlGcScanTempBlk
	.type	FtlGcScanTempBlk, %function
FtlGcScanTempBlk:
	.fnstart
	@ args = 0, pretend = 0, frame = 24
	@ frame_needed = 0, uses_anonymous_args = 0
	ldr	r2, .L1203
	movw	r3, #3000
	stmfd	sp!, {r4, r5, r6, r7, r8, r9, sl, fp, lr}
	.save {r4, r5, r6, r7, r8, r9, sl, fp, lr}
	.pad #28
	sub	sp, sp, #28
	ldrh	r4, [r2, r3]
	movw	r3, #65535
	mov	r5, r0
	str	r1, [sp, #20]
	cmp	r4, r3
	beq	.L1199
	cmp	r4, #0
	bne	.L1179
	b	.L1180
.L1199:
	mov	r4, #0
.L1179:
	ldr	r2, .L1203+4
	movw	r3, #3912
	ldr	fp, [sp, #20]
	ldrh	r3, [r2, r3]
	cmp	fp, r3
	bne	.L1181
.L1180:
	bl	FtlGcPageVarInit
.L1181:
	mov	r7, #0
	movw	r8, #65535
	mov	sl, r7
	mvn	r3, #0
	str	r3, [sp, #16]
.L1194:
	ldrh	r3, [r5, #0]
	strb	sl, [r5, #8]
	cmp	r3, r8
	beq	.L1200
.L1183:
.L1201:
	ldr	r3, .L1203+4
	movw	r2, #3844
	ldrh	r9, [r3, r2]
	ldr	r2, .L1203+8
	ldr	lr, [r2, #-976]
	ldr	ip, [r2, #-1172]
	movw	r2, #3924
	ldrh	r2, [r3, r2]
	mov	r3, #0
	mov	r6, r3
	str	r2, [sp, #12]
	mov	r2, r5
	b	.L1184
.L1186:
	ldrh	r0, [r2, #16]
	cmp	r0, r8
	beq	.L1185
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
.L1185:
	add	r3, r3, #1
	add	r2, r2, #2
	uxth	r3, r3
.L1184:
	cmp	r3, r9
	bne	.L1186
	ldr	r9, .L1203+8
	mov	r1, r6
	mov	r2, #0
	ldr	r0, [r9, #-976]
	bl	FlashReadPages
	mov	r3, #36
	mul	r3, r3, r6
	mov	r6, #0
	str	r3, [sp, #12]
	b	.L1187
.L1195:
	ldr	r1, [r9, #-976]
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
	bne	.L1188
	ldr	r3, [r3, #12]
	add	r6, r6, #36
	ldrh	r1, [r3, #0]
	cmp	r1, r8
	ldreq	r3, .L1203+8
	moveq	r1, #1
	streq	r1, [r3, #-1016]
	beq	.L1182
.L1189:
	ldr	r0, [r3, #12]
	mov	r1, fp
	ldr	r2, [r3, #8]
	bl	FtlGcUpdatePage
	b	.L1187
.L1188:
	mov	r2, fp
	ldr	r0, .L1203+12
	ldrh	r1, [r5, #0]
	bl	printk
	ldr	r3, .L1203+4
	ldr	r2, [r3, #3836]
	cmp	r2, #0
	bne	.L1190
	ldrb	r3, [r3, #928]	@ zero_extendqisi2
	cmp	r3, #0
	beq	.L1191
.L1190:
	ldr	r2, .L1203+8
	ldrh	r3, [r5, #0]
	ldr	r2, [r2, #-2084]
	mov	r3, r3, asl #1
	ldrh	r3, [r2, r3]
	cmp	r3, #119
	bls	.L1192
.L1191:
	ldr	r3, .L1203+8
	ldr	r3, [r3, #-976]
	ldr	r3, [r3, r6]
	cmn	r3, #1
	bne	.L1193
.L1192:
	ldr	r3, .L1203+8
	ldr	r3, [r3, #-976]
	add	r6, r3, r6
	ldr	r6, [r6, #4]
	str	r6, [sp, #16]
.L1193:
	ldr	r2, .L1203+8
	mov	r4, #0
	ldrh	r3, [r5, #0]
	ldr	r2, [r2, #-2020]
	mov	r3, r3, asl #1
	strh	sl, [r2, r3]	@ movhi
	ldrh	r0, [r5, #0]
	bl	INSERT_FREE_LIST
	mvn	r3, #0
	strh	r3, [r5, #0]	@ movhi
	bl	FtlGcPageVarInit
	b	.L1194
.L1187:
	ldr	r3, [sp, #12]
	cmp	r6, r3
	bne	.L1195
	ldr	fp, [sp, #20]
	add	r7, r7, #1
	add	r4, r4, #1
	cmp	r7, fp
	uxth	r4, r4
	bcc	.L1196
	ldr	r2, .L1203
	movw	r3, #3000
	ldrh	r1, [r2, r3]
	cmp	r1, r8
	beq	.L1196
	add	r1, r1, r7
	strh	r1, [r2, r3]	@ movhi
	ldr	r2, .L1203+4
	add	r3, r3, #912
	ldrh	r3, [r2, r3]
	cmp	r3, r4
	bhi	.L1197
.L1196:
	ldr	r2, .L1203+4
	movw	r3, #3912
	ldrh	r3, [r2, r3]
	cmp	r3, r4
	bhi	.L1201
.L1200:
	mov	r2, #0
.L1182:
	ldr	r1, .L1203
	movw	r3, #3000
	mvn	r0, #0
	strh	r4, [r5, #2]	@ movhi
	strb	r2, [r5, #6]
	strh	r0, [r1, r3]	@ movhi
	mov	r0, r5
	mov	r1, r4
	bl	ftl_sb_update_avl_pages
.L1197:
	ldr	r0, [sp, #16]
	add	sp, sp, #28
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, sl, fp, pc}
.L1204:
	.align	2
.L1203:
	.word	.LANCHOR1
	.word	.LANCHOR0
	.word	.LANCHOR2
	.word	.LC100
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
	ldr	r5, .L1263
	.pad #28
	sub	sp, sp, #28
	ldr	r4, .L1263+4
	mov	r1, r6
	ldr	r3, .L1263+8
	movw	r7, #3932
	ldr	r2, [r5, #3940]
	ldr	r0, [r4, #-1040]
	mov	sl, r4
	strh	r6, [r4, r3]	@ movhi
	movw	r3, #3958
	strh	r6, [r5, r3]	@ movhi
	mov	r2, r2, asl #2
	bl	ftl_memset
	ldr	r2, [r5, #3940]
	mov	r1, r6
	ldr	r0, [r4, #-1044]
	mov	r2, r2, asl #1
	bl	ftl_memset
	ldrh	r2, [r5, r7]
	mov	r1, r6
	ldr	r0, [r4, #-912]
	mov	r2, r2, asl #2
	bl	ftl_memset
	ldrh	r2, [r5, r7]
	mov	r1, r6
	ldr	r0, [r5, #3960]
	mov	r2, r2, asl #1
	bl	ftl_memset
	sub	r0, r4, #1776
	mov	r1, #255
	mov	r2, #12
	bl	ftl_memset
	movw	r3, #3852
	ldrh	r3, [r5, r3]
	str	r3, [sp, #8]
	b	.L1206
.L1208:
	add	r3, r6, r5
	ldr	r1, [sp, #8]
	ldrb	r0, [r3, #3870]	@ zero_extendqisi2
	bl	V2P_block
	str	r0, [sp, #0]
	bl	FtlBbmIsBadBlock
	ldr	r3, [sp, #0]
	cmp	r0, #0
	bne	.L1207
	ldr	r2, [r4, #-976]
	mov	r3, r3, asl #10
	mla	r2, r9, r7, r2
	str	r3, [r2, #4]
	ldr	r3, [r4, #-1176]
	str	r3, [r2, #8]
	ldrh	r3, [r6, r8]
	mul	r3, r3, r7
	add	r7, r7, #1
	uxth	r7, r7
	add	r1, r3, #3
	cmp	r3, #0
	movlt	r3, r1
	ldr	r1, [r4, #-1172]
	bic	r3, r3, #3
	add	r3, r1, r3
	str	r3, [r2, #12]
.L1207:
	add	r5, r5, #1
	uxth	r5, r5
	b	.L1241
.L1254:
	mov	r7, #0
	movw	fp, #3844
	mov	r5, r7
	mov	r9, #36
	movw	r8, #3924
.L1241:
	ldrh	r3, [r6, fp]
	cmp	r3, r5
	bhi	.L1208
	cmp	r7, #0
	beq	.L1209
	ldr	r0, [r4, #-976]
	mov	r1, r7
	mov	r2, #1
	mov	fp, r7
	bl	FlashReadPages
	mov	r8, #0
	str	r8, [sp, #12]
.L1240:
	ldr	r3, [r4, #-976]
	add	r2, r3, r8
	ldr	r3, [r3, r8]
	ldr	r6, [r2, #4]
	cmn	r3, #1
	ldr	r5, [r2, #12]
	ubfx	r6, r6, #10, #16
	bne	.L1210
	mov	r7, #16
.L1212:
	ldr	r0, [r4, #-976]
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
	ldreq	r3, [sl, #-976]
	mvneq	r2, #0
	streq	r2, [r3, r8]
	beq	.L1210
.L1211:
	ldr	r3, [r4, #-976]
	ldr	r3, [r3, r8]
	cmn	r3, #1
	bne	.L1210
	sub	r7, r7, #1
	uxth	r7, r7
	cmp	r7, #0
	bne	.L1212
.L1210:
	ldr	r3, [r4, #-976]
	ldr	r3, [r3, r8]
	cmn	r3, #1
	beq	.L1213
	ldr	r2, [r4, #-1808]
	ldr	r3, [r5, #4]
	cmn	r2, #1
	beq	.L1214
	cmp	r2, r3
	bhi	.L1215
.L1214:
	cmn	r3, #1
	addne	r3, r3, #1
	strne	r3, [sl, #-1808]
.L1215:
	ldrh	r3, [r5, #0]
	movw	r2, #61604
	cmp	r3, r2
	beq	.L1218
	bhi	.L1221
	movw	r2, #61574
	cmp	r3, r2
	bne	.L1216
	b	.L1261
.L1221:
	movw	r2, #61634
	cmp	r3, r2
	beq	.L1219
	movw	r2, #65535
	cmp	r3, r2
	bne	.L1216
	b	.L1262
.L1219:
	ldr	r3, .L1263
	mov	r9, r6
	ldr	r1, .L1263+8
	ldr	ip, [r4, #-1040]
	ldr	r0, [r3, #3940]
	ldrh	r1, [r4, r1]
	uxth	r2, r0
	sub	r3, r2, #1
	rsb	r2, r1, r2
	sub	r2, r2, #1
	uxth	r3, r3
	sxth	r2, r2
	str	r2, [sp, #4]
	b	.L1222
.L1228:
	mov	r6, r2, asl #2
	ldr	r7, [r5, #4]
	str	r6, [sp, #16]
	ldr	r6, [ip, r2, asl #2]
	cmp	r7, r6
	bls	.L1223
	ldr	ip, [ip, #0]
	mov	r6, r9
	cmp	ip, #0
	bne	.L1224
	cmp	r1, r0
	addne	r1, r1, #1
	ldrne	r0, .L1263+8
	strneh	r1, [r4, r0]	@ movhi
.L1224:
	mov	r0, #0
	sxth	r9, r3
	str	r3, [sp, #4]
	b	.L1225
.L1226:
	ldr	r7, [r4, #-1040]
	add	ip, r1, #1
	add	r0, r0, #1
	ldr	r3, [r7, ip, asl #2]
	mov	ip, ip, asl #1
	uxth	r0, r0
	str	r3, [r7, r1, asl #2]
	mov	r1, r1, asl #1
	ldr	r7, [r4, #-1044]
	ldrh	ip, [r7, ip]
	strh	ip, [r7, r1]	@ movhi
.L1225:
	sxth	r1, r0
	cmp	r1, r9
	bne	.L1226
	ldr	r1, [sl, #-1040]
	mov	r2, r2, asl #1
	ldr	r0, [r5, #4]
	ldr	ip, [sp, #16]
	ldr	r3, [sp, #4]
	str	r0, [r1, ip]
	ldr	r1, [sl, #-1044]
	strh	r6, [r1, r2]	@ movhi
	b	.L1227
.L1223:
	sub	r3, r3, #1
	uxth	r3, r3
.L1222:
	ldr	r6, [sp, #4]
	sxth	r2, r3
	cmp	r2, r6
	bgt	.L1228
	mov	r6, r9
.L1227:
	sxth	r3, r3
	cmp	r3, #0
	blt	.L1216
	ldr	r2, .L1263+8
	ldr	r0, .L1263
	ldrh	r1, [r4, r2]
	ldr	r0, [r0, #3940]
	rsb	r0, r1, r0
	sub	r0, r0, #1
	sxth	r0, r0
	cmp	r3, r0
	bgt	.L1216
	add	r1, r1, #1
	strh	r1, [r4, r2]	@ movhi
	ldr	r2, [r4, #-1040]
	ldr	r1, [r5, #4]
	str	r1, [r2, r3, asl #2]
	mov	r3, r3, asl #1
	ldr	r2, [r4, #-1044]
	b	.L1258
.L1261:
	ldr	r1, .L1263
	movw	r3, #3932
	movw	ip, #3958
	mov	r9, r6
	ldrh	r0, [r1, r3]
	ldrh	r1, [r1, ip]
	sub	r2, r0, #1
	ldr	ip, [r4, #-912]
	uxth	r3, r2
	rsb	r2, r1, r2
	str	r2, [sp, #4]
	b	.L1229
.L1235:
	mov	r6, r2, asl #2
	ldr	r7, [r5, #4]
	str	r6, [sp, #16]
	ldr	r6, [ip, r2, asl #2]
	cmp	r7, r6
	bls	.L1230
	ldr	ip, [ip, #0]
	mov	r6, r9
	cmp	ip, #0
	bne	.L1231
	cmp	r1, r0
	addne	r1, r1, #1
	ldrne	ip, .L1263
	movwne	r0, #3958
	strneh	r1, [ip, r0]	@ movhi
.L1231:
	sxth	ip, r3
	mov	r0, #0
	str	ip, [sp, #20]
	ldr	r9, .L1263
	str	r3, [sp, #4]
	b	.L1232
.L1233:
	ldr	r7, [r4, #-912]
	add	ip, r1, #1
	add	r0, r0, #1
	ldr	r3, [r7, ip, asl #2]
	mov	ip, ip, asl #1
	uxth	r0, r0
	str	r3, [r7, r1, asl #2]
	mov	r1, r1, asl #1
	ldr	r7, [r9, #3960]
	ldrh	ip, [r7, ip]
	strh	ip, [r7, r1]	@ movhi
.L1232:
	ldr	ip, [sp, #20]
	sxth	r1, r0
	cmp	r1, ip
	bne	.L1233
	ldr	r1, [sl, #-912]
	mov	r2, r2, asl #1
	ldr	r0, [r5, #4]
	ldr	ip, [sp, #16]
	ldr	r3, [sp, #4]
	str	r0, [r1, ip]
	ldr	r1, .L1263
	ldr	r1, [r1, #3960]
	strh	r6, [r1, r2]	@ movhi
	b	.L1234
.L1230:
	sub	r3, r3, #1
	uxth	r3, r3
.L1229:
	ldr	r6, [sp, #4]
	sxth	r2, r3
	cmp	r2, r6
	bgt	.L1235
	mov	r6, r9
.L1234:
	sxth	r3, r3
	cmp	r3, #0
	blt	.L1216
	ldr	r2, .L1263
	movw	ip, #3932
	movw	r1, #3958
	ldrh	ip, [r2, ip]
	ldrh	r0, [r2, r1]
	sub	ip, ip, #1
	rsb	ip, r0, ip
	sxth	ip, ip
	cmp	r3, ip
	bgt	.L1216
	add	r0, r0, #1
	strh	r0, [r2, r1]	@ movhi
	ldr	r0, [r5, #4]
	ldr	r1, [r4, #-912]
	str	r0, [r1, r3, asl #2]
	mov	r3, r3, asl #1
	ldr	r2, [r2, #3960]
.L1258:
	strh	r6, [r2, r3]	@ movhi
	b	.L1216
.L1218:
	ldr	r3, .L1263+12
	movw	ip, #65535
	ldrh	r2, [r4, r3]
	cmp	r2, ip
	streqh	r6, [r4, r3]	@ movhi
	beq	.L1259
	ldr	r3, .L1263+16
	movw	r1, #65535
	ldrh	r0, [r4, r3]
	cmp	r0, r1
	beq	.L1237
	mov	r1, #1
	bl	FtlFreeSysBlkQueueIn
.L1237:
	ldr	r3, [r5, #4]
	ldr	r2, [sl, #-1768]
	cmp	r2, r3
	ldr	r3, .L1263+16
	strcsh	r6, [r4, r3]	@ movhi
	bcs	.L1216
	ldr	r2, .L1263+12
	ldrh	r1, [r4, r2]
	strh	r6, [r4, r2]	@ movhi
	strh	r1, [r4, r3]	@ movhi
.L1259:
	ldr	r3, [r5, #4]
	str	r3, [r4, #-1768]
	b	.L1216
.L1262:
	mov	r0, r6
	b	.L1260
.L1213:
	ldr	r3, .L1263
	mov	r0, r6
	ldrb	r1, [r3, #928]	@ zero_extendqisi2
	cmp	r1, #0
	beq	.L1239
.L1260:
	mov	r1, #0
.L1239:
	bl	FtlFreeSysBlkQueueIn
.L1216:
	ldr	r6, [sp, #12]
	add	r8, r8, #36
	add	r3, r6, #1
	uxth	r3, r3
	str	r3, [sp, #12]
	cmp	r3, fp
	bne	.L1240
.L1209:
	ldr	ip, [sp, #8]
	add	r3, ip, #1
	uxth	r3, r3
	str	r3, [sp, #8]
.L1206:
	ldr	r6, .L1263
	movw	r3, #3854
	ldr	ip, [sp, #8]
	ldrh	r3, [r6, r3]
	cmp	r3, ip
	bhi	.L1254
	ldr	r1, .L1263+4
	ldr	r2, [r1, #-1044]
	ldrh	r3, [r2, #0]
	cmp	r3, #0
	bne	.L1242
	ldr	r0, .L1263+8
	ldrh	r1, [r1, r0]
	cmp	r1, #0
	ldrne	r0, [r6, #3940]
	bne	.L1243
	b	.L1242
.L1247:
	mov	r1, r1, asl #1
	ldrh	r1, [r2, r1]
	cmp	r1, #0
	beq	.L1244
	ldr	r7, .L1263
	sxth	r6, r3
	ldr	r1, .L1263+4
	mov	r5, #0
	b	.L1245
.L1246:
	ldr	ip, [r1, #-1044]
	mov	r0, r2, asl #1
	rsb	r4, r6, r2
	add	r3, r3, #1
	ldrh	sl, [ip, r0]
	mov	r8, r4, asl #1
	uxth	r3, r3
	strh	sl, [ip, r8]	@ movhi
	ldr	ip, [r1, #-1040]
	ldr	r2, [ip, r2, asl #2]
	str	r2, [ip, r4, asl #2]
	ldr	r2, [r1, #-1044]
	strh	r5, [r2, r0]	@ movhi
.L1245:
	ldr	r0, [r7, #3940]
	sxth	r2, r3
	cmp	r2, r0
	bcc	.L1246
	b	.L1242
.L1244:
	add	r3, r3, #1
	uxth	r3, r3
.L1243:
	sxth	r1, r3
	cmp	r1, r0
	bcc	.L1247
.L1242:
	ldr	r2, .L1263
	ldr	r1, [r2, #3960]
	ldrh	r3, [r1, #0]
	cmp	r3, #0
	bne	.L1248
	movw	r0, #3958
	ldrh	r0, [r2, r0]
	cmp	r0, #0
	movwne	r0, #3932
	ldrneh	r2, [r2, r0]
	bne	.L1249
	b	.L1248
.L1253:
	mov	ip, r0, asl #1
	ldrh	ip, [r1, ip]
	cmp	ip, #0
	beq	.L1250
	ldr	r1, .L1263
	movw	r8, #3932
	ldr	r7, .L1263+4
	mov	r6, #0
	b	.L1251
.L1252:
	ldr	r4, [r1, #3960]
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
	ldr	r2, [r1, #3960]
	strh	r6, [r2, ip]	@ movhi
.L1251:
	ldrh	ip, [r1, r8]
	sxth	r2, r3
	cmp	r2, ip
	blt	.L1252
	b	.L1248
.L1250:
	add	r3, r3, #1
	uxth	r3, r3
.L1249:
	sxth	r0, r3
	cmp	r0, r2
	blt	.L1253
.L1248:
	mov	r0, #0
	add	sp, sp, #28
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, sl, fp, pc}
.L1264:
	.align	2
.L1263:
	.word	.LANCHOR0
	.word	.LANCHOR2
	.word	-1048
	.word	-1776
	.word	-1772
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
	ldr	r3, .L1276
	stmfd	sp!, {r4, r5, r6, r7, r8, sl, lr}
	.save {r4, r5, r6, r7, r8, sl, lr}
	movweq	r2, #3914
	movwne	r2, #3912
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
	bne	.L1269
	b	.L1274
.L1272:
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
	bne	.L1270
	ldr	r3, [sp, #44]
	cmn	r3, #1
	bne	.L1270
	ldr	r3, [sp, #4]
	cmn	r3, #1
	subne	r4, r6, #1
	uxthne	r4, r4
	bne	.L1274
.L1270:
	add	r6, r6, #1
	uxth	r8, r6
.L1274:
	sxth	r6, r8
	sxth	r3, r4
	cmp	r6, r3
	ble	.L1272
.L1269:
	sxth	r0, r4
	add	sp, sp, #108
	ldmfd	sp!, {r4, r5, r6, r7, r8, sl, pc}
.L1277:
	.align	2
.L1276:
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
	ldr	r4, .L1294
	mov	r1, r5
	ldr	r2, .L1294+4
	ldr	r3, [r4, #-940]
	ldr	r0, [r4, #-2020]
	str	r5, [r4, #184]
	str	r3, [r4, #188]
	movw	r3, #3852
	ldrh	r2, [r2, r3]
	mov	r2, r2, asl #1
	bl	ftl_memset
	ldr	r3, .L1294+8
	ldrh	r0, [r4, r3]
	movw	r3, #65535
	cmp	r0, r3
	beq	.L1292
	mov	r1, #1
	ldr	r7, .L1294+12
	bl	FtlGetLastWrittenPage
	ldr	r3, .L1294+16
	add	r2, r0, #1
	mov	r6, r0
	strh	r2, [r4, r3]	@ movhi
	b	.L1280
.L1283:
	ldr	r2, .L1294+8
	mov	r1, #1
	ldr	r0, .L1294+20
	ldrh	r2, [r4, r2]
	orr	r3, r3, r2, asl #10
	str	r3, [r4, #180]
	ldr	r3, [r4, #-964]
	mov	r2, r1
	str	r3, [r4, #184]
	bl	FlashReadPages
	ldr	r3, [r4, #176]
	cmn	r3, #1
	beq	.L1281
	ldr	r3, [r4, #-964]
	ldr	r3, [r3, #0]
	cmp	r3, r7
	bne	.L1281
	ldr	r3, [r4, #-940]
	ldrh	r2, [r3, #0]
	movw	r3, #61604
	cmp	r2, r3
	beq	.L1282
.L1281:
	sub	r5, r5, #1
	uxth	r5, r5
.L1280:
	add	r3, r5, r6
	sxth	r3, r3
	cmp	r3, #0
	bge	.L1283
.L1282:
	ldr	r4, .L1294
	mov	r2, #48
	ldr	r5, .L1294+4
	movw	r6, #3852
	ldr	r0, .L1294+24
	ldr	r7, [r4, #184]
	mov	r1, r7
	bl	memcpy
	ldrh	r2, [r5, r6]
	add	r1, r7, #48
	ldr	r0, [r4, #-2020]
	mov	r2, r2, asl #1
	bl	memcpy
	ldrh	r1, [r5, r6]
	ldr	r3, [r4, #184]
	ldr	r0, [r4, #-1844]
	mov	r2, r1, lsr #3
	add	r1, r1, #24
	add	r2, r2, #4
	mov	r1, r1, lsr #1
	add	r1, r3, r1, asl #2
	bl	memcpy
	movw	r3, #3956
	ldrh	r3, [r5, r3]
	cmp	r3, #0
	beq	.L1284
	ldrh	r3, [r5, r6]
	movw	r2, #3948
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
.L1284:
	ldr	r4, .L1294
	ldr	r3, .L1294+12
	ldr	r2, [r4, #-2076]
	cmp	r2, r3
	bne	.L1292
	ldr	r3, .L1294+28
	movw	r1, #3866
	ldrb	r0, [r4, #-2066]	@ zero_extendqisi2
	ldrh	r2, [r4, r3]
	ldr	r3, .L1294+32
	strh	r2, [r4, r3]	@ movhi
	ldr	r3, .L1294+4
	ldrh	r1, [r3, r1]
	cmp	r0, r1
	bne	.L1292
	movw	r1, #3912
	movw	r0, #3918
	ldrh	r1, [r3, r1]
	movw	r5, #65535
	ldrh	r0, [r3, r0]
	str	r2, [r4, #212]
	mul	r1, r2, r1
	str	r1, [r3, #3972]
	mul	r1, r0, r1
	ldr	r0, [r3, #3856]
	str	r1, [r3, #3952]
	movw	r1, #3982
	ldrh	r1, [r3, r1]
	rsb	r0, r1, r0
	rsb	r0, r2, r0
	movw	r2, #3844
	ldrh	r1, [r3, r2]
	bl	__aeabi_uidiv
	ldr	r3, .L1294+36
	ldr	r1, .L1294+40
	strh	r0, [r4, r3]	@ movhi
	ldr	r3, .L1294+44
	ldrh	r2, [r4, r3]
	add	r3, r3, #62
	strh	r2, [r4, r3]	@ movhi
	sub	r3, r3, #60
	ldrh	r3, [r4, r3]
	mov	r0, r3, lsr #6
	and	r3, r3, #63
	strb	r3, [r4, #-1994]
	ldrb	r3, [r4, #-2065]	@ zero_extendqisi2
	strh	r0, [r4, r1]	@ movhi
	mvn	r1, #0
	ldr	r0, .L1294+48
	strb	r3, [r4, #-1992]
	ldr	r3, .L1294+52
	strh	r1, [r4, r3]	@ movhi
	mov	r3, #0
	ldr	r1, .L1294+56
	strh	r3, [r4, r0]	@ movhi
	strb	r3, [r4, #-1758]
	ldrh	r0, [r4, r1]
	add	r1, r1, #106
	strb	r3, [r4, #-1756]
	str	r3, [r4, #-1820]
	strh	r0, [r4, r1]	@ movhi
	sub	r1, r1, #104
	ldr	r0, .L1294+60
	ldrh	r1, [r4, r1]
	mov	ip, r1, lsr #6
	and	r1, r1, #63
	strb	r1, [r4, #-1946]
	ldrb	r1, [r4, #-2064]	@ zero_extendqisi2
	strh	ip, [r4, r0]	@ movhi
	strb	r1, [r4, #-1944]
	ldr	r1, .L1294+64
	ldrh	r0, [r4, r1]
	add	r1, r1, #150
	strh	r0, [r4, r1]	@ movhi
	sub	r1, r1, #148
	ldr	r0, .L1294+68
	ldrh	r1, [r4, r1]
	mov	ip, r1, lsr #6
	and	r1, r1, #63
	strb	r1, [r4, #-1898]
	ldrb	r1, [r4, #-2063]	@ zero_extendqisi2
	strh	ip, [r4, r0]	@ movhi
	strb	r1, [r4, #-1896]
	str	r3, [r4, #-1832]
	ldr	r1, [r4, #-2044]
	str	r3, [r4, #-1840]
	str	r3, [r4, #-1824]
	str	r3, [r4, #-1796]
	str	r3, [r4, #-1788]
	str	r3, [r4, #-1828]
	ldr	r3, [r4, #-2036]
	str	r1, [r4, #-1800]
	ldr	r1, [r4, #-1808]
	cmp	r3, r1
	strhi	r3, [r4, #-1808]
	ldr	r4, .L1294
	ldr	r3, [r4, #-2040]
	ldr	r1, [r4, #-1804]
	cmp	r3, r1
	strhi	r3, [r4, #-1804]
	cmp	r2, r5
	beq	.L1287
	ldr	r0, .L1294+72
	bl	make_superblock
.L1287:
	ldr	r3, .L1294+76
	ldrh	r3, [r4, r3]
	cmp	r3, r5
	beq	.L1288
	ldr	r0, .L1294+80
	bl	make_superblock
.L1288:
	ldr	r4, .L1294
	movw	r5, #65535
	ldr	r3, .L1294+84
	ldrh	r3, [r4, r3]
	cmp	r3, r5
	beq	.L1289
	sub	r0, r4, #1904
	bl	make_superblock
.L1289:
	ldr	r3, .L1294+52
	ldrh	r3, [r4, r3]
	cmp	r3, r5
	beq	.L1293
	ldr	r0, .L1294+88
	bl	make_superblock
	mov	r0, #0
	ldmfd	sp!, {r3, r4, r5, r6, r7, pc}
.L1292:
	mvn	r0, #0
	ldmfd	sp!, {r3, r4, r5, r6, r7, pc}
.L1293:
	mov	r0, #0
	ldmfd	sp!, {r3, r4, r5, r6, r7, pc}
.L1295:
	.align	2
.L1294:
	.word	.LANCHOR2
	.word	.LANCHOR0
	.word	-1776
	.word	1179929683
	.word	-1774
	.word	.LANCHOR2+176
	.word	.LANCHOR2-2076
	.word	-2068
	.word	-1770
	.word	-1780
	.word	-1998
	.word	-2062
	.word	-1762
	.word	-1764
	.word	-2058
	.word	-1950
	.word	-2054
	.word	-1902
	.word	.LANCHOR2-2000
	.word	-1952
	.word	.LANCHOR2-1952
	.word	-1904
	.word	.LANCHOR2-1764
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
	ldr	r6, .L1312
	ldr	r8, .L1312+4
	ldr	r3, [r6, #-964]
	mov	r7, r6
	ldr	r4, [r6, #-940]
	str	r3, [r6, #184]
	str	r4, [r6, #188]
	bl	FtlBbtMemInit
	movw	r3, #3908
	ldrh	r5, [r8, r3]
	sub	r5, r5, #1
	uxth	r5, r5
	b	.L1297
.L1301:
	mov	r1, #1
	mov	r3, r5, asl #10
	ldr	r0, .L1312+8
	mov	r2, r1
	str	r3, [r6, #180]
	bl	FlashReadPages
	ldr	r3, [r6, #176]
	cmn	r3, #1
	bne	.L1298
	ldr	r3, [r6, #180]
	mov	r1, #1
	ldr	r0, .L1312+8
	mov	r2, r1
	add	r3, r3, #1
	str	r3, [r6, #180]
	bl	FlashReadPages
.L1298:
	ldr	r3, [r7, #176]
	cmn	r3, #1
	beq	.L1299
	ldrh	r2, [r4, #0]
	movw	r3, #61649
	cmp	r2, r3
	bne	.L1299
	ldr	r3, .L1312+4
	movw	r2, #3976
	strh	r5, [r3, r2]	@ movhi
	ldr	r2, [r4, #4]
	str	r2, [r3, #3984]
	movw	r2, #3980
	ldrh	r1, [r4, #8]
	strh	r1, [r3, r2]	@ movhi
	b	.L1300
.L1299:
	sub	r5, r5, #1
	uxth	r5, r5
.L1297:
	movw	r3, #3908
	ldrh	r3, [r8, r3]
	sub	r3, r3, #48
	cmp	r5, r3
	bgt	.L1301
.L1300:
	ldr	r5, .L1312+4
	movw	r8, #3976
	movw	r3, #65535
	ldrh	r2, [r5, r8]
	cmp	r2, r3
	beq	.L1311
	movw	r6, #3980
	ldrh	r2, [r5, r6]
	cmp	r2, r3
	beq	.L1303
	ldr	r7, .L1312
	mov	r1, #1
	mov	r2, r2, asl #10
	add	r0, r7, #176
	str	r2, [r7, #180]
	mov	r2, r1
	bl	FlashReadPages
	ldr	r3, [r7, #176]
	cmn	r3, #1
	beq	.L1303
	ldrh	r2, [r4, #0]
	movw	r3, #61649
	cmp	r2, r3
	bne	.L1303
	ldr	r3, [r4, #4]
	ldr	r2, [r5, #3984]
	cmp	r3, r2
	strhi	r3, [r5, #3984]
	ldrhih	r2, [r5, r6]
	ldrhih	r3, [r4, #8]
	strhih	r2, [r5, r8]	@ movhi
	strhih	r3, [r5, r6]	@ movhi
.L1303:
	ldr	r8, .L1312+4
	movw	r3, #3976
	mov	r1, #1
	mov	r6, #0
	ldr	r5, .L1312
	ldrh	r0, [r8, r3]
	bl	FtlGetLastWrittenPage
	movw	r3, #3978
	add	r2, r0, #1
	mov	r7, r0
	strh	r2, [r8, r3]	@ movhi
	b	.L1304
.L1307:
	movw	r2, #3976
	mov	r1, #1
	ldrh	r2, [r8, r2]
	ldr	r0, .L1312+8
	orr	r3, r3, r2, asl #10
	str	r3, [r5, #180]
	ldr	r3, [r5, #-964]
	mov	r2, r1
	str	r3, [r5, #184]
	bl	FlashReadPages
	ldr	r3, [r5, #176]
	cmn	r3, #1
	beq	.L1305
	ldrh	r2, [r4, #0]
	movw	r3, #61649
	cmp	r2, r3
	beq	.L1306
.L1305:
	sub	r6, r6, #1
	uxth	r6, r6
.L1304:
	add	r3, r6, r7
	sxth	r3, r3
	cmp	r3, #0
	bge	.L1307
.L1306:
	ldr	r3, .L1312+4
	movw	r2, #3982
	ldrh	r1, [r4, #10]
	ldrh	r0, [r4, #12]
	strh	r1, [r3, r2]	@ movhi
	movw	r2, #65535
	cmp	r0, r2
	beq	.L1308
	ldr	r2, [r3, #3840]
	cmp	r0, r2
	beq	.L1308
	movw	r1, #3854
	ldrh	r3, [r3, r1]
	mov	r3, r3, lsr #2
	cmp	r2, r3
	bcs	.L1308
	cmp	r0, r3
	bcs	.L1308
	bl	FtlSysBlkNumInit
.L1308:
	ldr	r5, .L1312+12
	mov	r4, #0
	movw	r7, #3866
	ldr	r6, .L1312
	sub	r8, r5, #4000
	b	.L1309
.L1310:
	ldr	r3, .L1312+16
	ldr	r1, [r6, #184]
	ldr	r0, [r5, #4]!
	ldrh	r2, [r6, r3]
	mov	r2, r2, asl #2
	mla	r1, r4, r2, r1
	bl	memcpy
	add	r4, r4, #1
.L1309:
	ldrh	r3, [r8, r7]
	cmp	r4, r3
	bcc	.L1310
	mov	r0, #0
	ldmfd	sp!, {r4, r5, r6, r7, r8, pc}
.L1311:
	mvn	r0, #0
	ldmfd	sp!, {r4, r5, r6, r7, r8, pc}
.L1313:
	.align	2
.L1312:
	.word	.LANCHOR2
	.word	.LANCHOR0
	.word	.LANCHOR2+176
	.word	.LANCHOR0+4000
	.word	-1032
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
	ldr	r3, .L1321
	movw	fp, #3866
	ldr	r7, .L1321+4
	ldr	r6, .L1321+8
	ldr	r2, [r3, #-964]
	ldr	r8, [r3, #-940]
	str	r2, [r3, #184]
	str	r8, [r3, #188]
	b	.L1315
.L1320:
	movw	r9, #3908
	mvn	r3, #0
	ldrh	r4, [r6, r9]
	strh	r3, [r7], #2	@ movhi
	add	r4, r4, r3
	ldr	sl, .L1321
	uxth	r4, r4
	b	.L1316
.L1319:
	mla	r3, r3, r5, r4
	mov	r1, #1
	ldr	r0, .L1321+12
	mov	r2, r1
	mov	r3, r3, asl #10
	str	r3, [sl, #180]
	bl	FlashReadPages
	ldr	r3, [sl, #176]
	cmn	r3, #1
	beq	.L1317
	ldrh	r2, [r8, #0]
	movw	r3, #61664
	cmp	r2, r3
	streqh	r4, [r7, #-2]	@ movhi
	beq	.L1318
.L1317:
	sub	r4, r4, #1
	uxth	r4, r4
.L1316:
	ldrh	r3, [r6, r9]
	sub	r2, r3, #16
	cmp	r4, r2
	bgt	.L1319
.L1318:
	add	r5, r5, #1
.L1315:
	ldrh	r3, [r6, fp]
	cmp	r5, r3
	bcc	.L1320
	mov	r0, #0
	ldmfd	sp!, {r3, r4, r5, r6, r7, r8, r9, sl, fp, pc}
.L1322:
	.align	2
.L1321:
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
	ldr	r3, .L1342
	mov	r6, r0
	mov	r8, #0
	ldr	r7, .L1342+4
	ldrb	fp, [r3, #2901]	@ zero_extendqisi2
	b	.L1324
.L1331:
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
	bcs	.L1326
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
.L1326:
	add	r8, r8, #1
	add	r6, r6, #36
.L1324:
	cmp	r8, sl
	bne	.L1331
	ldr	r3, [sp, #12]
	cmp	r3, #0
	movne	r6, #0
	ldrne	r5, .L1342+8
	bne	.L1332
	b	.L1333
.L1338:
	ldr	r3, [r4, #0]
	cmn	r3, #1
	bne	.L1334
	ldr	r1, [r4, #4]
	ldr	r0, .L1342+12
	bl	printk
	b	.L1335
.L1334:
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
	bne	.L1336
	ldr	r0, .L1342+16
	ldr	r1, [r4, #4]
	bl	printk
	str	r7, [r4, #0]
.L1336:
	ldr	r3, [r4, #12]
	cmp	r3, #0
	beq	.L1337
	ldr	r2, [r3, #0]
	ldr	r3, [r5, #220]
	ldr	r3, [r3, #0]
	cmp	r2, r3
	beq	.L1337
	ldr	r0, .L1342+20
	ldr	r1, [r4, #4]
	bl	printk
	mvn	r3, #0
	str	r3, [r4, #0]
.L1337:
	ldr	r3, [r4, #8]
	cmp	r3, #0
	beq	.L1335
	ldr	r2, [r3, #0]
	ldr	r3, [r5, #216]
	ldr	r3, [r3, #0]
	cmp	r2, r3
	beq	.L1335
	ldr	r0, .L1342+24
	ldr	r1, [r4, #4]
	bl	printk
	mvn	r3, #0
	str	r3, [r4, #0]
.L1335:
	add	r6, r6, #1
	add	r4, r4, #36
.L1332:
	cmp	r6, sl
	bne	.L1338
.L1333:
	mov	r0, #0
	add	sp, sp, #68
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, sl, fp, pc}
.L1343:
	.align	2
.L1342:
	.word	.LANCHOR1
	.word	.LANCHOR0
	.word	.LANCHOR2
	.word	.LC101
	.word	.LC102
	.word	.LC103
	.word	.LC104
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
	ldr	r5, .L1372
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
	ldr	ip, .L1372+4
	ldrb	ip, [ip, #2901]	@ zero_extendqisi2
	str	ip, [sp, #12]
	beq	.L1345
	bl	FlashProgSlc2KPages
	b	.L1346
.L1359:
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
	bcs	.L1348
	ldrb	r1, [r5, #3833]	@ zero_extendqisi2
	add	r2, fp, r2, asl #4
	cmp	r1, #0
	ldr	r2, [r2, #3636]
	moveq	r8, #0
	cmp	r2, #0
	beq	.L1350
	cmp	r3, #1
	bne	.L1351
	ldr	r0, [r5, #3012]
	bl	NandcIqrWaitFlashReady
.L1351:
	ldrb	r0, [sp, #32]	@ zero_extendqisi2
	bl	FlashWaitCmdDone
.L1350:
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
	bne	.L1353
	bl	NandcWaitFlashReady
	b	.L1354
.L1353:
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
.L1354:
	ldr	r2, [sp, #20]
	sub	r3, r2, #1
	cmp	r3, #6
	bhi	.L1355
	add	r3, r5, r4
	ldrb	r3, [r3, #3756]	@ zero_extendqisi2
	cmp	r3, #0
	beq	.L1355
	mov	r0, r4
	ldrb	r1, [r5, #1]	@ zero_extendqisi2
	ldr	r2, .L1372+8
	mov	r3, #0
	bl	HynixSetRRPara
.L1355:
	mov	r0, r4
	bl	NandcFlashCs
	cmp	sl, #1
	mov	r0, r4
	bne	.L1356
	ldrb	r3, [r5, #928]	@ zero_extendqisi2
	cmp	r3, #0
	beq	.L1356
	bl	flash_enter_slc_mode
	b	.L1357
.L1356:
	bl	flash_exit_slc_mode
.L1357:
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
	beq	.L1358
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
.L1358:
	mov	r0, r4
	ldr	r1, [sp, #28]
	bl	FlashProgSecondCmd
	mov	r0, r4
	bl	NandcFlashDeCs
	add	r6, r6, r8
.L1348:
	add	r6, r6, #1
.L1345:
	ldr	r3, [sp, #8]
	cmp	r6, r3
	bcc	.L1359
	ldr	r5, .L1372
	mov	r4, #0
	ldr	r6, .L1372+12
	ldr	r0, [r5, #3012]
	bl	NandcIqrWaitFlashReady
	b	.L1360
.L1362:
	uxtb	r0, r4
	bl	FlashWaitCmdDone
	cmp	sl, #1
	bne	.L1361
	ldrb	r3, [r5, #928]	@ zero_extendqisi2
	cmp	r3, #0
	beq	.L1361
	ldrb	r0, [r6, r4, asl #4]	@ zero_extendqisi2
	bl	flash_exit_slc_mode
.L1361:
	add	r4, r4, #1
.L1360:
	ldrb	r3, [r5, #3762]	@ zero_extendqisi2
	cmp	r4, r3
	bcc	.L1362
	ldr	r2, [sp, #16]
	cmp	r2, #0
	ldreq	r0, [sp, #16]
	beq	.L1346
	mov	r5, #0
	ldr	r4, .L1372+16
	ldr	r6, [sp, #8]
	b	.L1363
.L1368:
	ldr	r3, [r9, #0]
	cmn	r3, #1
	bne	.L1364
	ldr	r1, [r9, #4]
	ldr	r0, .L1372+20
	bl	printk
	b	.L1365
.L1364:
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
	bne	.L1366
	ldr	r0, .L1372+24
	ldr	r1, [r9, #4]
	bl	printk
	str	r7, [r9, #0]
.L1366:
	ldr	r3, [r9, #12]
	cmp	r3, #0
	beq	.L1367
	ldr	r2, [r3, #0]
	ldr	r3, [r4, #220]
	ldr	r3, [r3, #0]
	cmp	r2, r3
	beq	.L1367
	ldr	r0, .L1372+28
	ldr	r1, [r9, #4]
	bl	printk
	mvn	r3, #0
	str	r3, [r9, #0]
.L1367:
	ldr	r3, [r9, #8]
	cmp	r3, #0
	beq	.L1365
	ldr	r2, [r3, #0]
	ldr	r3, [r4, #216]
	ldr	r3, [r3, #0]
	cmp	r2, r3
	beq	.L1365
	ldr	r0, .L1372+32
	ldr	r1, [r9, #4]
	bl	printk
	mvn	r3, #0
	str	r3, [r9, #0]
.L1365:
	add	r5, r5, #1
	add	r9, r9, #36
.L1363:
	cmp	r5, r6
	bne	.L1368
	mov	r0, #0
.L1346:
	add	sp, sp, #76
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, sl, fp, pc}
.L1373:
	.align	2
.L1372:
	.word	.LANCHOR0
	.word	.LANCHOR1
	.word	.LANCHOR0+4
	.word	.LANCHOR0+3628
	.word	.LANCHOR2
	.word	.LC101
	.word	.LC102
	.word	.LC103
	.word	.LC104
	.fnend
	.size	FlashProgPages, .-FlashProgPages
	.align	2
	.type	FtlVpcTblFlush.part.13, %function
FtlVpcTblFlush.part.13:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	stmfd	sp!, {r3, r4, r5, r6, r7, r8, sl, lr}
	.save {r3, r4, r5, r6, r7, r8, sl, lr}
	mov	r1, #255
	ldr	r4, .L1386
	movw	r7, #3852
	ldr	r6, .L1386+4
	ldr	r3, [r4, #-964]
	ldr	r5, [r4, #-940]
	str	r3, [r4, #184]
	ldr	r3, .L1386+8
	str	r5, [r4, #188]
	ldrh	r3, [r4, r3]
	strh	r3, [r5, #2]	@ movhi
	ldr	r3, .L1386+12
	strh	r3, [r5, #0]	@ movhi
	ldr	r3, [r4, #-1768]
	str	r3, [r5, #4]
	mov	r3, #0
	str	r3, [r5, #8]
	str	r3, [r5, #12]
	ldr	r3, .L1386+16
	str	r3, [r4, #-2076]
	ldr	r3, .L1386+20
	str	r3, [r4, #-2072]
	ldr	r3, .L1386+24
	ldrh	r2, [r4, r3]
	ldr	r3, .L1386+28
	strh	r2, [r4, r3]	@ movhi
	movw	r3, #3866
	ldrh	r3, [r6, r3]
	strb	r3, [r4, #-2066]
	ldr	r3, .L1386+32
	ldrh	r2, [r4, r3]
	sub	r3, r3, #62
	strh	r2, [r4, r3]	@ movhi
	add	r3, r3, #64
	ldrh	r2, [r4, r3]
	ldrb	r3, [r4, #-1994]	@ zero_extendqisi2
	orr	r2, r3, r2, asl #6
	ldr	r3, .L1386+36
	strh	r2, [r4, r3]	@ movhi
	ldrb	r3, [r4, #-1992]	@ zero_extendqisi2
	strb	r3, [r4, #-2065]
	ldr	r3, .L1386+40
	ldrh	r2, [r4, r3]
	sub	r3, r3, #106
	strh	r2, [r4, r3]	@ movhi
	add	r3, r3, #108
	ldrh	r2, [r4, r3]
	ldrb	r3, [r4, #-1946]	@ zero_extendqisi2
	orr	r2, r3, r2, asl #6
	ldr	r3, .L1386+44
	strh	r2, [r4, r3]	@ movhi
	ldrb	r3, [r4, #-1944]	@ zero_extendqisi2
	strb	r3, [r4, #-2064]
	ldr	r3, .L1386+48
	ldrh	r2, [r4, r3]
	sub	r3, r3, #150
	strh	r2, [r4, r3]	@ movhi
	add	r3, r3, #152
	ldr	r0, [r4, #184]
	ldrh	r2, [r4, r3]
	ldrb	r3, [r4, #-1898]	@ zero_extendqisi2
	orr	r2, r3, r2, asl #6
	ldr	r3, .L1386+52
	strh	r2, [r4, r3]	@ movhi
	ldrb	r3, [r4, #-1896]	@ zero_extendqisi2
	strb	r3, [r4, #-2063]
	ldr	r3, [r4, #-1800]
	str	r3, [r4, #-2044]
	ldr	r3, [r4, #-1808]
	str	r3, [r4, #-2036]
	ldr	r3, [r4, #-1804]
	str	r3, [r4, #-2040]
	movw	r3, #3922
	ldrh	r2, [r6, r3]
	bl	ftl_memset
	ldr	r1, .L1386+56
	mov	r2, #48
	ldr	r0, [r4, #184]
	bl	memcpy
	ldrh	r2, [r6, r7]
	ldr	r0, [r4, #184]
	ldr	r1, [r4, #-2020]
	mov	r2, r2, asl #1
	add	r0, r0, #48
	bl	memcpy
	ldrh	r2, [r6, r7]
	ldr	r3, [r4, #184]
	add	r0, r2, #24
	ldr	r1, [r4, #-1844]
	mov	r2, r2, lsr #3
	mov	r0, r0, lsr #1
	add	r2, r2, #4
	add	r0, r3, r0, asl #2
	bl	memcpy
	movw	r3, #3956
	ldrh	r3, [r6, r3]
	cmp	r3, #0
	beq	.L1375
	ldrh	r3, [r6, r7]
	ldr	r1, [r4, #184]
	mov	r0, r3, lsr #3
	add	r0, r0, r3, asl #1
	movw	r3, #3948
	add	r0, r0, #52
	ldrh	r2, [r6, r3]
	ubfx	r0, r0, #2, #14
	mov	r2, r2, asl #2
	add	r0, r1, r0, asl #2
	ldr	r1, [r4, #-1036]
	bl	memcpy
.L1375:
	mov	r0, #0
	ldr	r4, .L1386
	bl	FtlUpdateVaildLpn
	ldr	r6, .L1386+60
	mov	r7, #0
	movw	sl, #65535
.L1385:
	ldr	r3, [r4, #-964]
	mov	r1, #1
	ldr	r8, .L1386+8
	ldr	r0, .L1386+64
	str	r3, [r4, #184]
	ldr	r3, [r4, #-940]
	ldrh	r2, [r4, r8]
	str	r3, [r4, #188]
	ldrh	r3, [r4, r6]
	orr	r3, r3, r2, asl #10
	mov	r2, r1
	str	r3, [r4, #180]
	mov	r3, r1
	bl	FlashProgPages
	ldr	r1, .L1386+4
	movw	r3, #3914
	ldrh	r2, [r4, r6]
	ldrh	r3, [r1, r3]
	sub	r3, r3, #1
	cmp	r2, r3
	blt	.L1377
	ldr	r3, .L1386+68
	ldrh	r2, [r4, r8]
	ldrh	sl, [r4, r3]
	strh	r2, [r4, r3]	@ movhi
	mov	r3, #0
	strh	r3, [r4, r6]	@ movhi
	bl	FtlFreeSysBlkQueueOut
	ldr	r3, [r4, #-1808]
	mov	r1, #1
	add	r2, r3, #1
	str	r2, [r4, #-1808]
	str	r3, [r4, #-1768]
	mov	r2, r0, asl #10
	strh	r0, [r4, r8]	@ movhi
	str	r2, [r4, #180]
	mov	r2, r1
	str	r3, [r5, #4]
	mov	r3, r1
	strh	r0, [r5, #2]	@ movhi
	ldr	r0, .L1386+64
	bl	FlashProgPages
.L1377:
	ldrh	r3, [r4, r6]
	ldr	r2, [r4, #176]
	add	r3, r3, #1
	cmn	r2, #1
	uxth	r3, r3
	strh	r3, [r4, r6]	@ movhi
	bne	.L1378
	cmp	r3, #1
	add	r7, r7, #1
	ldreq	r2, .L1386+4
	movweq	r3, #3914
	uxth	r7, r7
	ldreqh	r3, [r2, r3]
	subeq	r3, r3, #1
	streqh	r3, [r4, r6]	@ movhi
	cmp	r7, #3
	bls	.L1385
	ldr	r4, .L1386
	mov	r2, r7
	ldr	r0, .L1386+72
	ldr	r1, [r4, #180]
	bl	printk
	mov	r3, #1
	str	r3, [r4, #-2092]
	ldmfd	sp!, {r3, r4, r5, r6, r7, r8, sl, pc}
.L1378:
	cmp	r3, #1
	beq	.L1385
	cmp	r2, #256
	beq	.L1385
	movw	r3, #65535
	cmp	sl, r3
	ldmeqfd	sp!, {r3, r4, r5, r6, r7, r8, sl, pc}
	mov	r0, sl
	mov	r1, #1
	bl	FtlFreeSysBlkQueueIn
	ldmfd	sp!, {r3, r4, r5, r6, r7, r8, sl, pc}
.L1387:
	.align	2
.L1386:
	.word	.LANCHOR2
	.word	.LANCHOR0
	.word	-1776
	.word	-3932
	.word	1179929683
	.word	1342177352
	.word	-1770
	.word	-2068
	.word	-2000
	.word	-2060
	.word	-1952
	.word	-2056
	.word	-1904
	.word	-2052
	.word	.LANCHOR2-2076
	.word	-1774
	.word	.LANCHOR2+176
	.word	-1772
	.word	.LC105
	.fnend
	.size	FtlVpcTblFlush.part.13, .-FtlVpcTblFlush.part.13
	.align	2
	.global	FtlVpcTblFlush
	.type	FtlVpcTblFlush, %function
FtlVpcTblFlush:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	stmfd	sp!, {r3, lr}
	.save {r3, lr}
	ldr	r3, .L1390
	ldr	r3, [r3, #-2092]
	cmp	r3, #0
	bne	.L1389
	bl	FtlVpcTblFlush.part.13
.L1389:
	mov	r0, #0
	ldmfd	sp!, {r3, pc}
.L1391:
	.align	2
.L1390:
	.word	.LANCHOR2
	.fnend
	.size	FtlVpcTblFlush, .-FtlVpcTblFlush
	.section	.text.unlikely,"ax",%progbits
	.align	2
	.type	FtlBbmTblFlush.part.16, %function
FtlBbmTblFlush.part.16:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	stmfd	sp!, {r0, r1, r2, r4, r5, r6, r7, r8, r9, sl, fp, lr}
	.save {r0, r1, r2, r4, r5, r6, r7, r8, r9, sl, fp, lr}
	mov	r1, #0
	ldr	r7, .L1405
	mov	r6, #0
	ldr	sl, .L1405+4
	movw	r9, #3866
	ldr	r3, [r7, #-940]
	add	r8, sl, #4000
	ldr	r0, [r7, #-964]
	str	r3, [r7, #188]
	movw	r3, #3922
	str	r0, [r7, #184]
	ldrh	r2, [sl, r3]
	bl	ftl_memset
	b	.L1393
.L1394:
	ldr	r3, .L1405+8
	ldr	r1, [r8, #4]!
	ldrh	r2, [r7, r3]
	ldr	r3, [r7, #184]
	mul	r0, r2, r6
	mov	r2, r2, asl #2
	add	r6, r6, #1
	add	r0, r3, r0, asl #2
	bl	memcpy
.L1393:
	ldrh	r3, [sl, r9]
	ldr	r4, .L1405+4
	cmp	r6, r3
	ldr	r5, .L1405
	blt	.L1394
	ldr	r6, [r5, #188]
	mov	r1, #255
	mov	r2, #16
	mov	r7, #0
	mov	fp, r7
	mov	r0, r6
	bl	ftl_memset
	ldr	r3, .L1405+12
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
	ldr	r3, [r4, #3840]
	strh	r3, [r6, #12]	@ movhi
	b	.L1403
.L1400:
	mov	fp, #1
.L1403:
	ldr	r3, [r5, #-964]
	movw	r8, #3976
	ldr	r4, .L1405+4
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
	ldr	r0, .L1405+16
	bl	printk
	mov	r1, #1
	mov	r2, r1
	mov	r3, r1
	ldr	r0, .L1405+20
	bl	FlashProgPages
	movw	r3, #3914
	ldrh	r3, [r4, r3]
	ldrh	r2, [r4, sl]
	sub	r3, r3, #1
	cmp	r2, r3
	blt	.L1396
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
	ldr	r0, .L1405+20
	mov	r2, r1
	mov	r3, r1
	bl	FlashProgPages
.L1396:
	ldr	r2, .L1405+4
	movw	r3, #3978
	ldr	r4, .L1405
	ldrh	r1, [r2, r3]
	add	r1, r1, #1
	strh	r1, [r2, r3]	@ movhi
	ldr	r3, [r5, #176]
	cmn	r3, #1
	bne	.L1397
	add	r7, r7, #1
	ldr	r0, .L1405+24
	ldr	r1, [r5, #180]
	uxth	r7, r7
	bl	printk
	cmp	r7, #3
	bls	.L1403
	ldr	r0, .L1405+28
	mov	r2, r7
	ldr	r1, [r4, #180]
	bl	printk
	mov	r3, #1
	str	r3, [r4, #-2092]
	b	.L1404
.L1397:
	cmp	fp, #0
	beq	.L1400
.L1404:
	ldmfd	sp!, {r1, r2, r3, r4, r5, r6, r7, r8, r9, sl, fp, pc}
.L1406:
	.align	2
.L1405:
	.word	.LANCHOR2
	.word	.LANCHOR0
	.word	-1032
	.word	-3887
	.word	.LC106
	.word	.LANCHOR2+176
	.word	.LC107
	.word	.LC108
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
	ldr	r3, .L1409
	ldr	r3, [r3, #-2092]
	cmp	r3, #0
	bne	.L1408
	bl	FtlBbmTblFlush.part.16
.L1408:
	mov	r0, #0
	ldmfd	sp!, {r3, pc}
.L1410:
	.align	2
.L1409:
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
	ldr	r4, .L1421
	ldr	r3, .L1421+4
	ldrh	r3, [r4, r3]
	cmp	r3, #0
	movne	r6, #0
	movne	sl, r4
	bne	.L1412
	b	.L1413
.L1419:
	add	r3, r3, r6
	mov	r1, r9
	mov	r5, #0
	ldr	r7, .L1421+4
	ldrb	r0, [r3, #3870]	@ zero_extendqisi2
	bl	V2P_block
	mov	r8, r0
	b	.L1414
.L1418:
	add	r3, r4, r5, asl #1
	sub	r3, r3, #1136
	sub	r3, r3, #4
	ldrh	r3, [r3, #0]
	cmp	r3, r8
	bne	.L1415
	mov	r1, r8
	ldr	r0, .L1421+8
	bl	printk
	mov	r0, r8
	bl	FtlBbmMapBadBlock
	bl	FtlBbmTblFlush
	ldrh	r1, [r4, r7]
	mov	r3, r5
	b	.L1416
.L1417:
	add	r0, r3, #1
	add	r3, r4, r3, asl #1
	sub	r3, r3, #1136
	add	r2, r4, r0, asl #1
	sub	r2, r2, #1136
	sub	r2, r2, #4
	ldrh	r2, [r2, #0]
	strh	r2, [r3, #-4]	@ movhi
	uxth	r3, r0
.L1416:
	cmp	r3, r1
	bcc	.L1417
	sub	r1, r1, #1
	strh	r1, [sl, r7]	@ movhi
.L1415:
	add	r5, r5, #1
	uxth	r5, r5
.L1414:
	ldrh	r3, [r4, r7]
	cmp	r3, r5
	bhi	.L1418
	add	r6, r6, #1
	uxth	r6, r6
.L1412:
	ldr	r3, .L1421+12
	movw	r2, #3844
	ldrh	r2, [r3, r2]
	cmp	r2, r6
	bhi	.L1419
	bl	FtlGcReFreshBadBlk
.L1413:
	mov	r0, #0
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, sl, pc}
.L1422:
	.align	2
.L1421:
	.word	.LANCHOR2
	.word	-1142
	.word	.LC109
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
	ldr	r3, .L1431
	mov	r4, r0
	ldr	r1, [r3, #-2020]
	ldrh	r2, [r1, r2]
	cmp	r2, #0
	bne	.L1424
	ldr	r1, .L1431+4
	ldrh	r0, [r3, r1]
	cmp	r0, r4
	mvneq	r2, #0
	streqh	r2, [r3, r1]	@ movhi
	beq	.L1426
	ldr	r1, .L1431+8
	ldrh	r1, [r3, r1]
	cmp	r1, r4
	beq	.L1427
	ldr	r1, .L1431+12
	ldrh	r1, [r3, r1]
	cmp	r1, r4
	beq	.L1427
	ldr	r1, .L1431+16
	ldrh	r3, [r3, r1]
	cmp	r3, r4
	beq	.L1427
.L1426:
	mov	r1, r4
	ldr	r0, .L1431+20
	bl	List_remove_node
	ldr	r5, .L1431
	ldr	r3, .L1431+24
	mov	r0, r4
	ldrh	r2, [r5, r3]
	sub	r2, r2, #1
	strh	r2, [r5, r3]	@ movhi
	bl	free_data_superblock
	mov	r0, r4
	bl	FtlGcFreeBadSuperBlk
	mov	r2, #1
	b	.L1427
.L1424:
	bl	List_update_data_list
	mov	r2, #0
.L1427:
	mov	r0, r2
	ldmfd	sp!, {r3, r4, r5, pc}
.L1432:
	.align	2
.L1431:
	.word	.LANCHOR2
	.word	-1764
	.word	-2000
	.word	-1952
	.word	-1904
	.word	.LANCHOR2-2024
	.word	-2012
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
	beq	.L1434
	ldr	r2, .L1439
	mov	r3, r0, asl #1
	ldr	r2, [r2, #-2020]
	ldrh	r4, [r2, r3]
	cmp	r4, #0
	subne	r4, r4, #1
	strneh	r4, [r2, r3]	@ movhi
	bne	.L1434
	ldr	r0, .L1439+4
	mov	r1, r5
	mov	r2, r4
	bl	printk
	mov	r0, r4
	ldmfd	sp!, {r3, r4, r5, r6, r7, pc}
.L1434:
	ldr	r7, .L1439
	movw	r3, #65535
	ldr	r6, .L1439+8
	ldrh	r0, [r7, r6]
	cmp	r0, r3
	streqh	r5, [r7, r6]	@ movhi
	moveq	r0, #0
	ldmeqfd	sp!, {r3, r4, r5, r6, r7, pc}
	cmp	r0, r5
	beq	.L1438
	bl	update_vpc_list
	strh	r5, [r7, r6]	@ movhi
	adds	r0, r0, #0
	movne	r0, #1
	ldmfd	sp!, {r3, r4, r5, r6, r7, pc}
.L1438:
	mov	r0, #0
	ldmfd	sp!, {r3, r4, r5, r6, r7, pc}
.L1440:
	.align	2
.L1439:
	.word	.LANCHOR2
	.word	.LC110
	.word	-1024
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
	ldr	r5, .L1453
	ldr	r7, .L1453+4
	add	r3, r0, r3, asl #1
	ldrh	r3, [r3, #16]
	b	.L1442
.L1443:
	ldrb	r3, [r4, #6]	@ zero_extendqisi2
	movw	r2, #3844
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
.L1442:
	cmp	r3, r6
	beq	.L1443
	ldrb	r2, [r4, #8]	@ zero_extendqisi2
	cmp	r2, #1
	bne	.L1445
	ldrb	r2, [r5, #928]	@ zero_extendqisi2
	cmp	r2, #0
	bne	.L1445
	ldrh	r2, [r4, #2]
	add	r2, r7, r2, asl #1
	sub	r2, r2, #896
	ldrh	r2, [r2, #0]
	cmp	r2, r6
	bne	.L1445
	ldrh	r3, [r4, #4]
	ldrh	r0, [r4, #0]
	sub	r3, r3, #1
	strh	r3, [r4, #4]	@ movhi
	bl	decrement_vpc_count
	b	.L1443
.L1445:
	ldrh	r6, [r4, #2]
	movw	r5, #65535
	mov	r7, r5
	orr	r6, r6, r3, asl #10
	ldrh	r3, [r4, #4]
	sub	r3, r3, #1
	strh	r3, [r4, #4]	@ movhi
.L1450:
	ldr	r2, .L1453
	movw	r3, #3844
	ldrh	r2, [r2, r3]
	ldrb	r3, [r4, #6]	@ zero_extendqisi2
.L1447:
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
	beq	.L1447
	strb	r3, [r4, #6]
	ldrb	r3, [r4, #8]	@ zero_extendqisi2
	cmp	r3, #1
	bne	.L1448
	ldr	r3, .L1453
	ldrb	r2, [r3, #928]	@ zero_extendqisi2
	cmp	r2, #0
	ldrh	r2, [r4, #2]
	bne	.L1451
	ldr	r3, .L1453+4
	add	r2, r3, r2, asl #1
	sub	r2, r2, #896
	ldrh	r3, [r2, #0]
	cmp	r3, r7
	bne	.L1448
	ldrh	r3, [r4, #4]
	cmp	r3, #0
	beq	.L1448
	sub	r3, r3, #1
	ldrh	r0, [r4, #0]
	strh	r3, [r4, #4]	@ movhi
	bl	decrement_vpc_count
	b	.L1450
.L1451:
	movw	r1, #3914
	ldrh	r1, [r3, r1]
	cmp	r2, r1
	bcc	.L1448
	ldr	r1, .L1453+4
	ldrh	r2, [r4, #0]
	ldrh	r0, [r4, #4]
	ldr	r1, [r1, #-2020]
	mov	r2, r2, asl #1
	ldrh	ip, [r1, r2]
	rsb	r0, r0, ip
	strh	r0, [r1, r2]	@ movhi
	mov	r2, #0
	strh	r2, [r4, #4]	@ movhi
	mov	r1, r2	@ movhi
	movw	r2, #3912
	ldrh	r3, [r3, r2]
	strb	r1, [r4, #6]
	strh	r3, [r4, #2]	@ movhi
.L1448:
	mov	r0, r6
	ldmfd	sp!, {r3, r4, r5, r6, r7, pc}
.L1454:
	.align	2
.L1453:
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
	ldr	r5, .L1461
	ldr	r7, .L1461+4
	add	r3, r0, r3, asl #1
	ldrh	r3, [r3, #16]
	b	.L1456
.L1457:
	ldrb	r3, [r4, #6]	@ zero_extendqisi2
	movw	r2, #3844
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
.L1456:
	cmp	r3, r6
	beq	.L1457
	ldrb	r2, [r4, #8]	@ zero_extendqisi2
	cmp	r2, #1
	bne	.L1459
	ldrb	r3, [r5, #928]	@ zero_extendqisi2
	cmp	r3, #0
	bne	.L1459
	ldrh	r3, [r4, #2]
	add	r3, r7, r3, asl #1
	sub	r3, r3, #896
	ldrh	r3, [r3, #0]
	cmp	r3, r6
	bne	.L1459
	ldrh	r3, [r4, #4]
	ldrh	r0, [r4, #0]
	sub	r3, r3, #1
	strh	r3, [r4, #4]	@ movhi
	bl	decrement_vpc_count
	ldrh	r3, [r4, #4]
	cmp	r3, #0
	bne	.L1457
	ldrh	r2, [r4, #2]
	strb	r3, [r4, #6]
	add	r2, r2, #1
	strh	r2, [r4, #2]	@ movhi
	ldmfd	sp!, {r3, r4, r5, r6, r7, pc}
.L1459:
	ldr	r3, .L1461
	ldrb	r1, [r3, #928]	@ zero_extendqisi2
	cmp	r1, #0
	ldmeqfd	sp!, {r3, r4, r5, r6, r7, pc}
	cmp	r2, #1
	ldmnefd	sp!, {r3, r4, r5, r6, r7, pc}
	movw	r2, #3914
	ldrh	r1, [r4, #2]
	ldrh	r2, [r3, r2]
	cmp	r1, r2
	ldmccfd	sp!, {r3, r4, r5, r6, r7, pc}
	ldr	r1, .L1461+4
	ldrh	r2, [r4, #0]
	ldrh	r0, [r4, #4]
	ldr	r1, [r1, #-2020]
	mov	r2, r2, asl #1
	ldrh	ip, [r1, r2]
	rsb	r0, r0, ip
	strh	r0, [r1, r2]	@ movhi
	mov	r2, #0
	strh	r2, [r4, #4]	@ movhi
	mov	r1, r2	@ movhi
	movw	r2, #3912
	ldrh	r3, [r3, r2]
	strb	r1, [r4, #6]
	strh	r3, [r4, #2]	@ movhi
	ldmfd	sp!, {r3, r4, r5, r6, r7, pc}
.L1462:
	.align	2
.L1461:
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
	@ args = 0, pretend = 0, frame = 16
	@ frame_needed = 0, uses_anonymous_args = 0
	stmfd	sp!, {r4, r5, r6, r7, r8, r9, sl, fp, lr}
	.save {r4, r5, r6, r7, r8, r9, sl, fp, lr}
	.pad #20
	sub	sp, sp, #20
	ldr	r4, .L1501
	mov	r5, r0
	ldr	r8, .L1501+4
.L1497:
	ldr	r3, .L1501+8
	cmp	r5, r3
	bne	.L1467
	ldrh	r2, [r4, r8]
	ldr	r3, [r4, #-1188]
	mov	r1, r2, lsr #1
	mul	ip, r3, r2
	add	r0, r1, #1
	add	r0, r0, ip, lsr #2
	ldr	ip, .L1501+12
	uxth	r0, r0
	ldr	ip, [ip, #3836]
	cmp	ip, #0
	beq	.L1491
	ldr	ip, [r4, #-1784]
	cmp	ip, #29
	bhi	.L1491
	cmp	ip, #2
	bls	.L1496
	tst	r2, #1
	beq	.L1468
	cmp	r3, #0
	moveq	r1, r3
	beq	.L1469
	b	.L1468
.L1467:
	ldrb	r3, [r5, #8]	@ zero_extendqisi2
	cmp	r3, #1
	bne	.L1496
	ldr	r3, .L1501+12
	movw	r2, #3864
	ldrh	r2, [r3, r2]
	cmp	r2, #1
	beq	.L1496
	ldrb	r2, [r3, #928]	@ zero_extendqisi2
	cmp	r2, #0
	bne	.L1496
	ldr	r3, [r3, #3836]
	ldrh	r2, [r4, r8]
	cmp	r3, #0
	mov	r1, r2, lsr #3
	beq	.L1468
	ldr	r3, [r4, #-1784]
	cmp	r3, #1
	movls	r1, #7
	mulls	r1, r1, r2
	movls	r1, r1, lsr #3
	b	.L1468
.L1491:
	mov	r1, r0
.L1468:
	cmp	r1, #0
	subne	r1, r1, #1
	uxthne	r1, r1
	b	.L1469
.L1496:
	mov	r1, #0
.L1469:
	ldrb	r2, [r5, #8]	@ zero_extendqisi2
	ldr	r0, .L1501+16
	bl	List_pop_index_node
	ldrh	r3, [r4, r8]
	sub	r3, r3, #1
	strh	r3, [r4, r8]	@ movhi
	uxth	r7, r0
	mov	r0, r5
	strh	r7, [r5, #0]	@ movhi
	bl	make_superblock
	ldrb	r3, [r5, #7]	@ zero_extendqisi2
	cmp	r3, #0
	beq	.L1470
	ldr	r2, .L1501+12
	movw	r3, #3844
	ldr	ip, [r4, #-2088]
	mov	r6, #0
	mov	sl, r5
	mov	r1, r5
	ldrh	r9, [r2, r3]
	mov	r0, r6
	mov	r3, ip
	mov	r2, r6
	stmia	sp, {r5, ip}
	b	.L1471
.L1470:
	ldr	r3, [r4, #-2020]
	mov	r7, r7, asl #1
	mvn	r2, #0
	strh	r2, [r3, r7]	@ movhi
	b	.L1497
.L1474:
	str	r0, [r3, #8]
	movw	r5, #65535
	str	r0, [r3, #12]
	ldrh	lr, [r1, #16]
	cmp	lr, r5
	beq	.L1473
	ldr	r5, [sp, #4]
	mov	ip, #36
	mov	lr, lr, asl #10
	mla	fp, ip, r6, r5
	add	r6, r6, #1
	uxth	r6, r6
	str	lr, [fp, #4]
.L1473:
	add	r2, r2, #1
	add	r3, r3, #36
	add	r1, r1, #2
	uxth	r2, r2
.L1471:
	cmp	r2, r9
	bne	.L1474
	ldr	r3, .L1501+12
	ldr	r5, [sp, #0]
	ldr	r3, [r3, #3836]
	cmp	r3, #0
	beq	.L1475
	ldr	r3, .L1501+20
	cmp	r5, r3
	bne	.L1475
	ldr	r2, [r4, #-2084]
	mov	r3, r7, asl #1
	ldrh	r3, [r2, r3]
	cmp	r3, #30
	movhi	r3, #0
	strhib	r3, [r4, #-1992]
.L1475:
	ldrb	r3, [r5, #8]	@ zero_extendqisi2
	ldr	r2, [r4, #-2084]
	cmp	r3, #0
	mov	r3, r7, asl #1
	ldrh	r1, [r2, r3]
	bne	.L1476
	cmp	r1, #0
	ldrne	ip, .L1501+12
	movwne	r0, #3902
	moveq	r1, #2
	ldrneh	r0, [ip, r0]
	addne	r1, r1, r0
	strh	r1, [r2, r3]	@ movhi
	ldr	r3, [r4, #-1800]
	mov	r0, r7
	mov	r1, #0
	add	r3, r3, #1
	str	r3, [r4, #-1800]
	b	.L1499
.L1476:
	add	r1, r1, #1
	strh	r1, [r2, r3]	@ movhi
	ldr	r3, [r4, #-1796]
	mov	r0, r7
	mov	r1, #1
	add	r3, r3, #1
	str	r3, [r4, #-1796]
.L1499:
	bl	ftl_set_blk_mode
	ldr	r3, [r4, #-2084]
	mov	r9, r7, asl #1
	ldr	r2, [r4, #-1788]
	ldr	fp, .L1501
	ldrh	r3, [r3, r9]
	cmp	r3, r2
	movw	r2, #3902
	strhi	r3, [r4, #-1788]
	ldr	r3, .L1501+12
	ldr	r0, [fp, #-1800]
	ldrh	r1, [r3, r2]
	ldr	r2, [fp, #-1796]
	mla	r0, r0, r1, r2
	movw	r2, #3852
	ldrh	r1, [r3, r2]
	bl	__aeabi_uidiv
	ldr	r3, [fp, #-924]
	ldr	r2, [r3, #16]
	add	r2, r2, #1
	str	r2, [r3, #16]
	ldr	r3, [fp, #-2088]
	mov	r2, #0
	str	r0, [fp, #-1792]
	b	.L1481
.L1482:
	add	r2, r2, #1
	ldr	r1, [r3, #-32]
	uxth	r2, r2
	bic	r1, r1, #1020
	bic	r1, r1, #3
	str	r1, [r3, #-32]
.L1481:
	cmp	r2, r6
	add	r3, r3, #36
	bne	.L1482
	ldr	r3, .L1501+12
	ldrb	r3, [r3, #928]	@ zero_extendqisi2
	cmp	r3, #0
	beq	.L1483
	ldrb	r3, [r5, #8]	@ zero_extendqisi2
	mov	r2, r6
	ldr	r0, [r4, #-2088]
	cmp	r3, #1
	moveq	r1, #0
	movne	r1, #1
	bl	FlashEraseBlocks
.L1483:
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
	b	.L1485
.L1487:
	ldr	r1, [r4, #-2088]
	add	r0, r1, fp
	ldr	r5, [r1, fp]
	cmn	r5, #1
	bne	.L1486
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
.L1486:
	add	fp, fp, #36
	add	sl, sl, #2
.L1485:
	cmp	fp, r9
	bne	.L1487
	cmp	r6, #0
	mov	r5, r7
	mov	r9, ip
	mov	r7, r3
	beq	.L1488
	mov	r0, r3
	bl	update_multiplier_value
	bl	FtlBbmTblFlush
.L1488:
	ldrb	r3, [r5, #7]	@ zero_extendqisi2
	cmp	r3, #0
	ldreq	r3, .L1501
	mvneq	r2, #0
	ldreq	r3, [r3, #-2020]
	streqh	r2, [r3, r9]	@ movhi
	beq	.L1497
.L1489:
	ldr	r1, .L1501+12
	movw	r2, #3912
	ldrh	r2, [r1, r2]
	strh	r7, [r5, #0]	@ movhi
	mul	r2, r2, r3
	mov	r3, #0
	strh	r3, [r5, #2]	@ movhi
	strb	r3, [r5, #6]
	ldr	r3, .L1501
	uxth	r2, r2
	strh	r2, [r5, #4]	@ movhi
	ldr	r1, [r3, #-1808]
	str	r1, [r5, #12]
	add	r1, r1, #1
	str	r1, [r3, #-1808]
	ldrh	r0, [r5, #0]
	ldr	r1, [r3, #-2020]
	mov	r3, r0, asl #1
	strh	r2, [r1, r3]	@ movhi
	add	sp, sp, #20
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, sl, fp, pc}
.L1502:
	.align	2
.L1501:
	.word	.LANCHOR2
	.word	-2004
	.word	.LANCHOR2-1904
	.word	.LANCHOR0
	.word	.LANCHOR2-2008
	.word	.LANCHOR2-2000
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
	ldr	r3, .L1505
	ldr	r3, [r3, #-2092]
	cmp	r3, #0
	bne	.L1504
	bl	allocate_data_superblock.part.20
.L1504:
	mov	r0, #0
	ldmfd	sp!, {r3, pc}
.L1506:
	.align	2
.L1505:
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
	ldr	r3, .L1516
	stmfd	sp!, {r4, r5, r6, r7, r8, lr}
	.save {r4, r5, r6, r7, r8, lr}
	mov	r4, r0
	ldrb	r7, [r3, #928]	@ zero_extendqisi2
	.pad #40
	sub	sp, sp, #40
	cmp	r7, #0
	beq	.L1515
	ldrb	r7, [r0, #8]	@ zero_extendqisi2
	cmp	r7, #1
	ldreqh	r6, [r0, #4]
	beq	.L1508
	mov	r7, #0
.L1515:
	mov	r6, #12
.L1508:
	mvn	r8, #0
	ldr	r5, .L1516+4
	b	.L1509
.L1512:
	ldrh	r3, [r4, #4]
	cmp	r3, #0
	beq	.L1510
	mov	r0, r4
	bl	get_new_active_ppa
	cmn	r0, #1
	str	r0, [sp, #8]
	beq	.L1510
	ldr	r2, [r5, #-940]
	add	r0, sp, #4
	ldr	r3, [r5, #-964]
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
	ldr	r1, [r5, #-1804]
	cmn	r1, #2
	str	r1, [r2, #4]
	addne	r1, r1, #1
	moveq	r1, #0
	mov	r2, r7
	str	r1, [r5, #-1804]
	mov	r1, #1
	bl	FlashProgPages
	ldrh	r0, [r4, #0]
	bl	decrement_vpc_count
.L1509:
	cmp	r6, #0
	bne	.L1512
.L1510:
	ldr	r2, .L1516+4
	ldrh	r3, [r4, #0]
	ldrh	r1, [r4, #4]
	ldr	r2, [r2, #-2020]
	mov	r3, r3, asl #1
	ldrh	r0, [r2, r3]
	rsb	r1, r1, r0
	strh	r1, [r2, r3]	@ movhi
	ldr	r2, .L1516
	movw	r3, #3912
	ldrh	r3, [r2, r3]
	strh	r3, [r4, #2]	@ movhi
	mov	r3, #0
	strb	r3, [r4, #6]
	strh	r3, [r4, #4]	@ movhi
	add	sp, sp, #40
	ldmfd	sp!, {r4, r5, r6, r7, r8, pc}
.L1517:
	.align	2
.L1516:
	.word	.LANCHOR0
	.word	.LANCHOR2
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
	.pad #28
	sub	sp, sp, #28
	ldr	r7, .L1547
	mov	r8, r1
	ldr	r9, .L1547+4
	str	r0, [sp, #12]
	ldr	r6, [r7, #-2092]
	ldrb	r0, [r9, #3833]	@ zero_extendqisi2
	cmp	r6, #0
	str	r0, [sp, #16]
	movne	r4, #0
	bne	.L1519
	ldrb	r2, [r9, #928]	@ zero_extendqisi2
	mov	r5, r6
	ldr	r3, [sp, #12]
	mov	r4, r6
	mov	fp, #36
	str	r2, [sp, #8]
	str	r3, [r7, #-988]
	b	.L1520
.L1524:
	mul	r3, fp, r6
	ldr	r2, [r7, #-2088]
	mov	r0, #0
	ldr	r1, [sp, #12]
	str	r0, [r2, r3]
	add	r3, r9, r6
	ldrb	r0, [r3, #3870]	@ zero_extendqisi2
	bl	V2P_block
	cmp	r8, #0
	mov	sl, r0
	beq	.L1521
	bl	IsBlkInVendorPart
	cmp	r0, #0
	bne	.L1522
.L1521:
	mov	r0, sl
	bl	FtlBbmIsBadBlock
	cmp	r0, #0
	addne	r4, r4, #1
	uxthne	r4, r4
	bne	.L1522
	movw	r1, #3924
	ldr	r3, [r7, #-2088]
	ldrh	r2, [r9, r1]
	mov	sl, sl, asl #10
	mla	r3, fp, r5, r3
	mul	r2, r2, r5
	add	r5, r5, #1
	uxth	r5, r5
	add	r1, r2, #3
	cmp	r2, #0
	str	sl, [r3, #4]
	movlt	r2, r1
	ldr	r1, [r7, #-936]
	bic	r2, r2, #3
	str	r0, [r3, #8]
	add	r2, r1, r2
	str	r2, [r3, #12]
.L1522:
	add	r6, r6, #1
	uxth	r6, r6
.L1520:
	movw	r2, #3844
	ldr	sl, .L1547+4
	ldrh	r3, [r9, r2]
	cmp	r3, r6
	bhi	.L1524
	cmp	r5, #0
	beq	.L1519
	ldr	r3, [sp, #8]
	mov	r7, #0
	ldr	r9, .L1547
	mov	r2, r5
	adds	r6, r3, #0
	strb	r7, [sl, #3833]
	movne	r6, #1
	ldr	r0, [r9, #-2088]
	mov	r1, r6
	bl	FlashEraseBlocks
	ldr	r0, [sp, #16]
	strb	r0, [sl, #3833]
	mov	sl, r7
.L1527:
	ldr	r3, [r9, #-2088]
	add	r2, r3, r7
	ldr	r3, [r3, r7]
	cmn	r3, #1
	bne	.L1526
	ldr	r0, [r2, #4]
	add	r4, r4, #1
	ubfx	r0, r0, #10, #16
	uxth	r4, r4
	bl	FtlBbmMapBadBlock
.L1526:
	add	sl, sl, #1
	add	r7, r7, #36
	uxth	sl, sl
	cmp	sl, r5
	bne	.L1527
	cmp	r8, #0
	moveq	r2, #6
	moveq	sl, #1
	streq	r2, [sp, #8]
	beq	.L1528
	ldr	r3, .L1547+4
	movw	r2, #3914
	ldrh	sl, [r3, r2]
	ldrb	r3, [r3, #928]	@ zero_extendqisi2
	cmp	r3, #0
	moveq	r1, sl, lsr #2
	streq	r1, [sp, #8]
	moveq	r6, #1
	movne	r6, #1
	strne	r6, [sp, #8]
.L1528:
	mov	r7, #0
.L1536:
	mov	r9, #0
	ldr	fp, .L1547
	mov	r5, r9
	b	.L1529
.L1532:
	mov	r0, #36
	ldr	r2, [fp, #-2088]
	mul	r3, r0, r9
	mov	r1, #0
	str	r1, [r2, r3]
	ldr	r2, .L1547+4
	ldr	r1, [sp, #12]
	add	r3, r2, r9
	ldrb	r0, [r3, #3870]	@ zero_extendqisi2
	bl	V2P_block
	cmp	r8, #0
	str	r0, [sp, #20]
	beq	.L1530
	bl	IsBlkInVendorPart
	cmp	r0, #0
	bne	.L1531
.L1530:
	ldr	r0, [sp, #20]
	bl	FtlBbmIsBadBlock
	cmp	r0, #0
	bne	.L1531
	ldr	r3, [fp, #-2088]
	mov	r0, #36
	ldr	r1, [sp, #20]
	mla	r3, r0, r5, r3
	add	r2, r7, r1, asl #10
	ldr	r1, .L1547+4
	add	r0, r0, #3888
	str	r2, [r3, #4]
	ldr	r2, [fp, #-948]
	str	r2, [r3, #8]
	ldrh	r2, [r1, r0]
	mul	r2, r2, r5
	add	r5, r5, #1
	uxth	r5, r5
	add	r1, r2, #3
	cmp	r2, #0
	movlt	r2, r1
	ldr	r1, [fp, #-944]
	bic	r2, r2, #3
	add	r2, r1, r2
	str	r2, [r3, #12]
.L1531:
	add	r9, r9, #1
	uxth	r9, r9
.L1529:
	ldr	ip, .L1547+4
	movw	r2, #3844
	ldrh	r3, [ip, r2]
	cmp	r3, r9
	bhi	.L1532
	cmp	r5, #0
	beq	.L1519
	ldr	r9, .L1547
	mov	r3, #0
	mov	r1, r5
	strb	r3, [ip, #3833]
	mov	r2, r6
	mov	r3, #1
	ldr	r0, [r9, #-2088]
	mov	fp, #0
	str	ip, [sp, #4]
	bl	FlashProgPages
	ldr	ip, [sp, #4]
	mov	r3, fp
	ldr	r0, [sp, #16]
	strb	r0, [ip, #3833]
	mov	ip, r8
	mov	r8, r6
	mov	r6, r5
	mov	r5, fp
.L1535:
	ldr	r2, [r9, #-2088]
	add	r1, r2, fp
	ldr	r2, [r2, fp]
	cmp	r2, #0
	beq	.L1534
	ldr	r0, [r1, #4]
	add	r4, r4, #1
	str	ip, [sp, #4]
	ubfx	r0, r0, #10, #16
	uxth	r4, r4
	bl	FtlBbmMapBadBlock
	ldr	ip, [sp, #4]
.L1534:
	add	r5, r5, #1
	add	fp, fp, #36
	uxth	r5, r5
	cmp	r5, r6
	bne	.L1535
	ldr	r1, [sp, #8]
	mov	r5, r6
	mov	r6, r8
	mov	r8, ip
	add	r7, r7, r1
	uxth	r7, r7
	cmp	r7, sl
	bcc	.L1536
	ldr	r9, .L1547
	mov	r7, #0
	mov	sl, r7
.L1538:
	cmp	r8, #0
	beq	.L1537
	ldr	r3, [r9, #-2088]
	add	r2, r3, r7
	ldr	r3, [r3, r7]
	cmp	r3, #0
	bne	.L1537
	ldr	r0, [r2, #4]
	mov	r1, #1
	ubfx	r0, r0, #10, #16
	bl	FtlFreeSysBlkQueueIn
.L1537:
	add	sl, sl, #1
	add	r7, r7, #36
	uxth	sl, sl
	cmp	sl, r5
	bne	.L1538
	ldr	r2, [sp, #12]
	cmp	r2, #63
	movhi	r3, #0
	movls	r3, #1
	cmp	r8, #0
	moveq	r8, r3
	orrne	r8, r3, #1
	cmp	r8, #0
	beq	.L1519
	ldr	r3, .L1547
	mov	r1, r6
	mov	r2, sl
	ldr	r0, [r3, #-2088]
	bl	FlashEraseBlocks
.L1519:
	mov	r0, r4
	add	sp, sp, #28
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, sl, fp, pc}
.L1548:
	.align	2
.L1547:
	.word	.LANCHOR2
	.word	.LANCHOR0
	.fnend
	.size	FtlLowFormatEraseBlock, .-FtlLowFormatEraseBlock
	.align	2
	.type	FlashTestBlk.part.23, %function
FlashTestBlk.part.23:
	.fnstart
	@ args = 0, pretend = 0, frame = 104
	@ frame_needed = 0, uses_anonymous_args = 0
.L1550:
	stmfd	sp!, {r4, r5, lr}
	.save {r4, r5, lr}
	.pad #108
	sub	sp, sp, #108
	ldr	r4, .L1552
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
.L1553:
	.align	2
.L1552:
	.word	.LANCHOR2
	.fnend
	.size	FlashTestBlk.part.23, .-FlashTestBlk.part.23
	.align	2
	.global	FlashTestBlk
	.type	FlashTestBlk, %function
FlashTestBlk:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	@ link register save eliminated.
	ldr	r3, .L1556
	ldr	r3, [r3, #152]
	cmp	r0, r3
	bcc	.L1555
	b	FlashTestBlk.part.23
.L1555:
	mov	r0, #0
	bx	lr
.L1557:
	.align	2
.L1556:
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
	ldr	r3, .L1586
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
	ldr	r4, .L1586+4
	mul	fp, fp, r2
	ldr	r2, [r3, #3624]
	ldr	r0, [r4, #224]
	ldrb	r2, [r2, #24]	@ zero_extendqisi2
	str	r0, [sp, #28]
	ldr	r0, .L1586+8
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
	ldr	r4, .L1586
	uxth	r3, r3
	str	r2, [sp, #20]
	str	r3, [sp, #24]
	b	.L1560
.L1582:
	ldr	r3, .L1586+4
	add	r2, r3, r5, asl #1
	ldrh	r6, [r2, #228]
	cmp	r6, #0
	bne	.L1561
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
	b	.L1562
.L1570:
	mvn	r3, #0
	strb	r3, [sp, #34]
	strb	r3, [sp, #35]
	ldr	r3, [sp, #12]
	tst	r3, #1
	beq	.L1563
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
	bne	.L1563
	ldr	r1, [r4, #856]
	add	r2, sp, #35
	mov	r0, r9
	add	r1, r3, r1
	bl	FlashReadSpare
	ldrb	r3, [sp, #34]	@ zero_extendqisi2
	ldrb	r2, [sp, #35]	@ zero_extendqisi2
	and	r3, r2, r3
	strb	r3, [sp, #34]
.L1563:
	ldr	r0, [sp, #12]
	tst	r0, #2
	beq	.L1564
	ldr	r3, [r4, #3624]
	mov	r0, r9
	add	r2, sp, #35
	ldrh	r1, [r3, #10]
	ldr	r3, [sl, #3588]
	sub	r1, r1, #1
	add	r1, r1, r3
	add	r1, r1, r6
	bl	FlashReadSpare
.L1564:
	ldr	r3, [r4, #3624]
	ldrb	r3, [r3, #7]	@ zero_extendqisi2
	cmp	r3, #1
	cmpne	r3, #8
	ldrb	r3, [sp, #34]	@ zero_extendqisi2
	bne	.L1565
	cmp	r3, #0
	beq	.L1584
	ldrb	r0, [sp, #35]	@ zero_extendqisi2
	rsbs	r0, r0, #1
	movcc	r0, #0
	b	.L1566
.L1565:
	cmp	r3, #255
	bne	.L1584
	ldrb	r0, [sp, #35]	@ zero_extendqisi2
	subs	r0, r0, #255
	movne	r0, #1
	b	.L1566
.L1584:
	mov	r0, #1
.L1566:
	ldr	r2, [sp, #12]
	tst	r2, #4
	beq	.L1567
	ldr	r1, [sl, #3588]
	mov	r0, r9
	add	r1, r6, r1
	bl	SandiskProgTestBadBlock
.L1567:
	cmp	r0, #0
	beq	.L1568
	mov	r1, r5
	mov	r2, r8
	ldr	r0, .L1586+12
	add	r7, r7, #1
	bl	printk
	ldr	r3, [sp, #16]
	and	r0, r8, #31
	mov	ip, #1
	uxth	r7, r7
	mov	r2, r3, lsr #5
	ldr	r3, .L1586+4
	ldr	r3, [r3, #144]
	ldr	r1, [r3, r2, asl #2]
	orr	r1, r1, ip, asl r0
	ldr	r0, [sp, #20]
	str	r1, [r3, r2, asl #2]
	ldrb	r3, [r4, #3762]	@ zero_extendqisi2
	mul	r3, r3, r0
	cmp	r7, r3
	bgt	.L1569
.L1568:
	ldr	r2, [sp, #8]
	add	r8, r8, #1
	add	r6, r6, r2
.L1562:
	uxth	r3, r8
	str	r3, [sp, #16]
	cmp	r3, fp
	bcc	.L1570
.L1569:
	ldr	r0, .L1586+16
	mov	r1, r5
	mov	r2, r7
	bl	printk
	ldrb	r3, [r4, #3762]	@ zero_extendqisi2
	ldr	r0, [sp, #20]
	mul	r3, r3, r0
	cmp	r7, r3
	blt	.L1571
	movw	r3, #3068
	mov	r1, #0
	ldrh	r2, [r4, r3]
	ldr	r3, .L1586+4
	mov	r2, r2, asl #9
	ldr	r0, [r3, #144]
	bl	ftl_memset
.L1571:
	cmp	r5, #0
	bne	.L1572
	ldr	sl, .L1586+4
	mov	r8, r5
	mov	r7, r5
	ldrh	r9, [sl, #152]
	b	.L1573
.L1575:
	mov	r0, r6
	bl	FlashTestBlk
	cmp	r0, #0
	beq	.L1574
	mov	r1, r6
	ldr	r0, .L1586+20
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
.L1574:
	add	r8, r8, #1
	uxth	r8, r8
.L1573:
	add	r6, r8, r9
	ldrb	r3, [r4, #853]	@ zero_extendqisi2
	uxth	r6, r6
	cmp	r3, r6
	bhi	.L1575
	ldr	r6, [sp, #24]
	sub	r9, fp, #50
	ldr	sl, .L1586+4
	mov	r8, #1
	b	.L1576
.L1578:
	mov	r0, r6
	bl	FlashTestBlk
	cmp	r0, #0
	beq	.L1577
	mov	r1, r6
	ldr	r0, .L1586+20
	bl	printk
	ldr	r3, [sl, #144]
	mov	r2, r6, lsr #5
	and	r0, r6, #31
	ldr	r1, [r3, r2, asl #2]
	orr	r1, r1, r8, asl r0
	str	r1, [r3, r2, asl #2]
.L1577:
	sub	r6, r6, #1
	uxth	r6, r6
.L1576:
	cmp	r6, r9
	bgt	.L1578
	ldr	r3, .L1586+4
	ldrb	r1, [r4, #853]	@ zero_extendqisi2
	ldr	r2, [r3, #152]
	rsb	r2, r2, r1
	cmp	r7, r2
	bcc	.L1572
	movw	r2, #3068
	ldr	r0, [r3, #144]
	ldrh	r2, [r4, r2]
	mov	r1, #0
	mov	r2, r2, asl #9
	bl	ftl_memset
.L1572:
	mul	r8, fp, r5
	ldr	r7, .L1586+4
	ldr	r6, [sp, #24]
	ldr	r9, [sp, #28]
	add	sl, r7, r5, asl #1
.L1579:
	ldr	r0, .L1586+24
	mov	r1, r5
	mov	r2, r6
	bl	printk
	ldr	r3, [r7, #144]
	b	.L1580
.L1581:
	sub	r6, r6, #1
	uxth	r6, r6
.L1580:
	mov	r1, r6, lsr #5
	and	r2, r6, #31
	ldr	r1, [r3, r1, asl #2]
	mov	r2, r1, lsr r2
	ands	r2, r2, #1
	bne	.L1581
	ldr	r3, .L1586+28
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
	bne	.L1579
.L1561:
	add	r5, r5, #1
	uxtb	r5, r5
.L1560:
	ldrb	r3, [r4, #3762]	@ zero_extendqisi2
	cmp	r3, r5
	bhi	.L1582
	add	sp, sp, #76
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, sl, fp, pc}
.L1587:
	.align	2
.L1586:
	.word	.LANCHOR0
	.word	.LANCHOR2
	.word	.LC111
	.word	.LC112
	.word	.LC113
	.word	.LC114
	.word	.LC115
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
	bge	.L1589
	ldrh	r2, [r4, #40]
	movw	r3, #65535
	cmp	r2, r3
	beq	.L1590
	ldr	r1, .L1602
	movw	r3, #3914
	ldrh	r2, [r4, #2]
	ldrh	r3, [r1, r3]
	cmp	r2, r3
	bcc	.L1590
.L1589:
	ldrh	r1, [r4, #40]
	movw	r3, #65535
	uxth	r7, r0
	cmp	r1, r3
	beq	.L1591
	ldr	r0, .L1602
	movw	r2, #3914
	ldrh	r3, [r4, #2]
	ldrh	r2, [r0, r2]
	cmp	r2, r3
	movls	r0, r6
	movls	r2, #0
	bhi	.L1591
	b	.L1592
.L1594:
	ldrh	lr, [r0], #2
	cmp	lr, r1
	beq	.L1601
	add	r2, r2, #1
	uxth	r2, r2
.L1592:
	cmp	r2, ip
	bne	.L1594
	b	.L1593
.L1601:
	mov	r7, r2
.L1593:
	mov	r2, r2, asl #1
	ldr	r0, .L1602+4
	ldrh	r2, [r6, r2]
	bl	printk
	mvn	r3, #0
	strh	r3, [r4, #40]	@ movhi
.L1591:
	mov	r7, r7, asl #1
	ldrh	r8, [r6, r7]
	cmp	r8, #0
	beq	.L1590
	ldr	r3, [r4, #32]
	cmp	r3, #0
	bne	.L1590
	mov	r2, #1
	ldr	r1, .L1602
	str	r2, [r4, #32]
	strh	r3, [r6, r7]	@ movhi
	ldrh	r3, [r4, #8]
	ldrh	r2, [r4, #2]
	sub	r3, r3, #1
	strh	r3, [r4, #8]	@ movhi
	movw	r3, #3914
	ldrh	r3, [r1, r3]
	cmp	r2, r3
	bcc	.L1595
	mov	r0, r4
	bl	ftl_map_blk_alloc_new_blk
.L1595:
	mov	r7, #0
	ldr	r6, .L1602+8
	mov	fp, r7
	b	.L1596
.L1599:
	ldr	r3, [r5, r7, asl #2]
	mov	r9, r7, asl #2
	cmp	r8, r3, lsr #10
	bne	.L1597
	ldr	r3, [r6, #-960]
	mov	r1, #1
	ldr	sl, [r6, #-940]
	mov	r2, r1
	ldr	r0, .L1602+12
	str	r3, [r6, #184]
	str	sl, [r6, #188]
	ldr	r3, [r5, r7, asl #2]
	str	r3, [r6, #180]
	bl	FlashReadPages
	ldr	r3, [r6, #176]
	cmn	r3, #1
	bne	.L1598
	str	fp, [r5, r9]
	ldr	r0, .L1602+16
	ldr	r1, [r6, #180]
	ldrh	r2, [sl, #8]
	bl	printk
	mov	r3, #1
	str	r3, [r6, #-2092]
	b	.L1597
.L1598:
	mov	r0, r4
	mov	r1, r7
	ldr	r2, [r6, #184]
	bl	FtlMapWritePage
.L1597:
	add	r7, r7, #1
	uxth	r7, r7
.L1596:
	ldrh	r3, [r4, #6]
	cmp	r3, r7
	bhi	.L1599
	mov	r0, r8
	mov	r1, #1
	bl	FtlFreeSysBlkQueueIn
	mov	r3, #0
	str	r3, [r4, #32]
.L1590:
	ldr	r1, .L1602
	movw	r3, #3914
	ldrh	r2, [r4, #2]
	ldrh	r3, [r1, r3]
	cmp	r2, r3
	bcc	.L1600
	mov	r0, r4
	bl	ftl_map_blk_alloc_new_blk
.L1600:
	mov	r0, #0
	ldmfd	sp!, {r3, r4, r5, r6, r7, r8, r9, sl, fp, pc}
.L1603:
	.align	2
.L1602:
	.word	.LANCHOR0
	.word	.LC116
	.word	.LANCHOR2
	.word	.LANCHOR2+176
	.word	.LC117
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
	ldr	r5, .L1610
	ldr	r7, [r0, #12]
	ldr	r8, [r0, #24]
	ldr	r6, [r5, #-2092]
	cmp	r6, #0
	bne	.L1605
	ldrh	r3, [r0, #0]
	movw	r2, #65535
	cmp	r3, r2
	bne	.L1606
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
	b	.L1605
.L1606:
	mov	r3, r3, asl #1
	ldr	r2, [r0, #28]
	mov	r1, #255
	ldrh	r7, [r7, r3]
	ldrh	r3, [r0, #2]
	orr	r3, r3, r7, asl #10
	str	r3, [r5, #180]
	ldr	r3, [r5, #-964]
	str	r3, [r5, #184]
	ldr	r3, [r5, #-940]
	str	r3, [r5, #188]
	str	r2, [r3, #4]
	ldr	r2, .L1610+4
	strh	r2, [r3, #8]	@ movhi
	ldrh	r2, [r0, #4]
	strh	r7, [r3, #2]	@ movhi
	strh	r2, [r3, #0]	@ movhi
	movw	r3, #3914
	ldr	r2, .L1610+8
	ldr	r0, [r5, #-964]
	ldrh	r2, [r2, r3]
	mov	r2, r2, asl #3
	bl	ftl_memset
	mov	r3, r6
	b	.L1607
.L1609:
	ldr	r2, [r8, r3, asl #2]
	cmp	r7, r2, lsr #10
	bne	.L1608
	add	r6, r6, #1
	ldr	r2, [r5, #-964]
	uxth	r6, r6
	str	r3, [r2, r6, asl #3]
	ldr	r2, [r5, #-964]
	ldr	r1, [r8, r3, asl #2]
	add	r2, r2, r6, asl #3
	str	r1, [r2, #4]
.L1608:
	add	r3, r3, #1
	uxth	r3, r3
.L1607:
	ldrh	r2, [r4, #6]
	cmp	r2, r3
	bhi	.L1609
	mov	r1, #1
	mov	r3, #0
	ldr	r0, .L1610+12
	mov	r2, r1
	bl	FlashProgPages
	ldrh	r3, [r4, #2]
	mov	r0, r4
	add	r3, r3, #1
	strh	r3, [r4, #2]	@ movhi
	bl	ftl_map_blk_gc
.L1605:
	mov	r0, #0
	ldmfd	sp!, {r4, r5, r6, r7, r8, pc}
.L1611:
	.align	2
.L1610:
	.word	.LANCHOR2
	.word	-1291
	.word	.LANCHOR0
	.word	.LANCHOR2+176
	.fnend
	.size	Ftl_write_map_blk_to_last_page, .-Ftl_write_map_blk_to_last_page
	.align	2
	.type	FtlMapWritePage.part.14, %function
FtlMapWritePage.part.14:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	stmfd	sp!, {r4, r5, r6, r7, r8, r9, sl, lr}
	.save {r4, r5, r6, r7, r8, r9, sl, lr}
	mov	r4, r0
	ldr	r5, .L1625
	mov	sl, r1
	ldr	r8, .L1625+4
	mov	r9, r2
	mov	r6, #0
.L1623:
	ldr	r3, [r5, #-1824]
	add	r3, r3, #1
	str	r3, [r5, #-1824]
	movw	r3, #3914
	ldrh	r2, [r4, #2]
	ldrh	r3, [r8, r3]
	sub	r3, r3, #1
	cmp	r2, r3
	bge	.L1614
	ldrh	r2, [r4, #0]
	movw	r3, #65535
	cmp	r2, r3
	bne	.L1615
.L1614:
	mov	r0, r4
	bl	Ftl_write_map_blk_to_last_page
.L1615:
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
	ldr	r0, .L1625+8
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
	bne	.L1616
	ldr	r0, .L1625+12
	add	r6, r6, #1
	ldr	r1, [r5, #180]
	bl	printk
	ldrh	r3, [r4, #2]
	uxth	r6, r6
	cmp	r3, #2
	movwls	r3, #3914
	ldrlsh	r3, [r8, r3]
	subls	r3, r3, #1
	strlsh	r3, [r4, #2]	@ movhi
	cmp	r6, #3
	bls	.L1623
	ldr	r4, .L1625
	mov	r2, r6
	ldr	r0, .L1625+16
	ldr	r1, [r4, #180]
	bl	printk
	mov	r3, #1
	str	r3, [r4, #-2092]
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, sl, pc}
.L1616:
	cmp	r3, #0
	strneh	r7, [r4, #40]	@ movhi
	cmp	r2, #1
	beq	.L1623
	cmp	r3, #256
	beq	.L1623
	ldr	r3, .L1625
	ldr	r2, [r3, #180]
	ldr	r3, [r4, #24]
	str	r2, [r3, sl, asl #2]
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, sl, pc}
.L1626:
	.align	2
.L1625:
	.word	.LANCHOR2
	.word	.LANCHOR0
	.word	.LANCHOR2+176
	.word	.LC118
	.word	.LC119
	.fnend
	.size	FtlMapWritePage.part.14, .-FtlMapWritePage.part.14
	.align	2
	.global	FtlMapWritePage
	.type	FtlMapWritePage, %function
FtlMapWritePage:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	stmfd	sp!, {r3, lr}
	.save {r3, lr}
	ldr	r3, .L1629
	ldr	r3, [r3, #-2092]
	cmp	r3, #0
	bne	.L1628
	bl	FtlMapWritePage.part.14
.L1628:
	mov	r0, #0
	ldmfd	sp!, {r3, pc}
.L1630:
	.align	2
.L1629:
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
	ldr	r5, .L1632
	mul	r4, r4, r0
	ldr	r0, .L1632+4
	ldr	r2, [r5, #-1856]
	add	r3, r2, r4
	ldrh	r1, [r2, r4]
	ldr	r2, [r3, #8]
	bl	FtlMapWritePage
	ldr	r3, [r5, #-1856]
	mov	r0, #0
	add	r4, r3, r4
	ldr	r3, [r4, #4]
	bic	r3, r3, #-2147483648
	str	r3, [r4, #4]
	ldmfd	sp!, {r3, r4, r5, pc}
.L1633:
	.align	2
.L1632:
	.word	.LANCHOR2
	.word	.LANCHOR2-1092
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
	ldr	r3, .L1638
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
	beq	.L1636
	mov	r1, #1
	add	r0, r3, #176
	mov	r2, r1
	bl	FlashReadPages
	b	.L1637
.L1636:
	ldr	r2, .L1638+4
	movw	r3, #3922
	mov	r1, #255
	ldrh	r2, [r2, r3]
	bl	ftl_memset
.L1637:
	ldr	r3, .L1638
	mov	r0, r4
	mov	r1, r5
	ldr	r2, [r3, #184]
	ldmfd	sp!, {r3, r4, r5, lr}
	b	FtlMapWritePage
.L1639:
	.align	2
.L1638:
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
	ldr	r3, .L1651
	stmfd	sp!, {r4, r5, r6, r7, r8, r9, sl, fp, lr}
	.save {r4, r5, r6, r7, r8, r9, sl, fp, lr}
	mov	r9, r2
	mov	r2, #3920
	mov	r5, r1
	ldrh	r7, [r3, r2]
	movw	r2, #3906
	ldrh	r2, [r3, r2]
	add	r1, r1, r0
	.pad #52
	sub	sp, sp, #52
	mov	r8, r0
	cmp	r1, r2
	mvnhi	fp, #0
	bhi	.L1641
	mov	r7, r0, lsr r7
	mov	fp, #0
	ldr	r6, .L1651+4
	mov	r2, r7, asl #2
	str	r2, [sp, #0]
	b	.L1642
.L1648:
	ldr	r3, [r6, #-908]
	mov	r0, r8
	ldr	r2, [sp, #0]
	ldr	sl, [r3, r2]
	movw	r3, #3918
	ldr	r2, .L1651
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
	beq	.L1644
	ldr	r2, .L1651+4
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
	bne	.L1646
	mov	r1, r7
	mov	r2, sl
	ldr	r0, .L1651+8
	bl	printk
	ldr	r0, .L1651+12
	mov	r1, r7
	ldr	r2, [r6, #-956]
	bl	FtlMapWritePage
.L1646:
	ldr	r3, [sp, #4]
	mov	r0, r9
	ldr	r1, [r6, #-956]
	mov	r2, r4, asl #9
	add	r1, r1, r3, asl #9
	bl	memcpy
	b	.L1647
.L1644:
	mov	r0, r9
	mov	r1, sl
	mov	r2, r4, asl #9
	bl	ftl_memset
.L1647:
	ldr	r3, [sp, #0]
	add	r7, r7, #1
	rsb	r5, r4, r5
	add	r8, r8, r4
	add	r9, r9, r4, asl #9
	add	r3, r3, #4
	str	r3, [sp, #0]
.L1642:
	cmp	r5, #0
	bne	.L1648
.L1641:
	mov	r0, fp
	add	sp, sp, #52
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, sl, fp, pc}
.L1652:
	.align	2
.L1651:
	.word	.LANCHOR0
	.word	.LANCHOR2
	.word	.LC120
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
	ldr	r4, .L1657
	mov	r1, #1
	ldr	r5, .L1657+4
	sub	r2, r4, #1712
	bl	FtlVendorPartRead
	ldr	r3, [r4, #-1712]
	cmp	r3, r5
	beq	.L1654
	sub	r0, r4, #1712
	mov	r1, #0
	mov	r2, #512
	bl	ftl_memset
	str	r5, [r4, #-1712]
.L1654:
	ldr	r2, [r4, #-1712]
	ldr	r3, .L1657
	cmp	r2, r5
	bne	.L1655
	ldr	r2, [r3, #-1624]
	str	r2, [r3, #-1816]
	ldr	r2, [r3, #-1620]
	str	r2, [r3, #-1812]
	ldr	r2, [r3, #-1704]
	str	r2, [r3, #-1820]
	ldr	r2, [r3, #-1700]
	str	r2, [r3, #-1832]
	ldr	r2, [r3, #-1696]
	str	r2, [r3, #-1840]
	ldr	r2, [r3, #-1692]
	str	r2, [r3, #-1824]
	ldr	r2, [r3, #-1684]
	str	r2, [r3, #-1796]
	ldr	r2, [r3, #-1680]
	str	r2, [r3, #-2080]
	ldr	r2, [r3, #-1676]
	str	r2, [r3, #-1836]
	ldr	r2, [r3, #-1672]
	str	r2, [r3, #-1828]
	ldr	r2, [r3, #-1668]
	str	r2, [r3, #-1788]
	ldr	r2, [r3, #-1664]
	str	r2, [r3, #-1784]
	ldr	r2, [r3, #-1652]
	str	r2, [r3, #-1716]
.L1655:
	ldr	r4, .L1657
	mov	r3, #0
	ldr	r5, .L1657+8
	str	r3, [r4, #-1004]
	ldr	r2, [r4, #-1644]
	ldr	r3, .L1657+12
	cmp	r2, r3
	bne	.L1656
	mov	r3, #1
	ldr	r0, .L1657+16
	str	r3, [r5, #3836]
	ldr	r1, .L1657+20
	bl	printk
.L1656:
	movw	r3, #3902
	ldr	r0, [r4, #-1800]
	ldrh	r2, [r5, r3]
	ldr	r3, [r4, #-1796]
	mla	r0, r0, r2, r3
	movw	r3, #3852
	ldrh	r1, [r5, r3]
	bl	__aeabi_uidiv
	str	r0, [r4, #-1792]
	ldmfd	sp!, {r3, r4, r5, pc}
.L1658:
	.align	2
.L1657:
	.word	.LANCHOR2
	.word	1179929683
	.word	.LANCHOR0
	.word	305432421
	.word	.LC78
	.word	.LC121
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
	ldr	r4, .L1661
	ldr	r5, .L1661+4
	ldr	r2, [r4, #-924]
	ldrh	r1, [r4, r5]
	bl	FtlVendorPartRead
	ldr	r3, [r4, #-924]
	ldr	r2, [r3, #0]
	ldr	r3, .L1661+8
	cmp	r2, r3
	beq	.L1660
	ldr	r1, .L1661+12
	ldr	r0, .L1661+16
	bl	printk
	ldrh	r2, [r4, r5]
	ldr	r0, [r4, #-924]
	mov	r1, #0
	mov	r2, r2, asl #9
	bl	ftl_memset
.L1660:
	mov	r0, #0
	ldmfd	sp!, {r3, r4, r5, pc}
.L1662:
	.align	2
.L1661:
	.word	.LANCHOR2
	.word	-928
	.word	1112818501
	.word	.LC122
	.word	.LC78
	.fnend
	.size	FtlLoadEctTbl, .-FtlLoadEctTbl
	.align	2
	.global	FtlVendorPartWrite
	.type	FtlVendorPartWrite, %function
FtlVendorPartWrite:
	.fnstart
	@ args = 0, pretend = 0, frame = 56
	@ frame_needed = 0, uses_anonymous_args = 0
	ldr	r3, .L1673
	stmfd	sp!, {r4, r5, r6, r7, r8, r9, sl, fp, lr}
	.save {r4, r5, r6, r7, r8, r9, sl, fp, lr}
	mov	r9, r2
	mov	r2, #3920
	mov	r4, r1
	ldrh	r8, [r3, r2]
	movw	r2, #3906
	ldrh	r2, [r3, r2]
	add	r1, r1, r0
	.pad #60
	sub	sp, sp, #60
	mov	r6, r0
	cmp	r1, r2
	mvnhi	r7, #0
	bhi	.L1664
	mov	r8, r0, lsr r8
	mov	r7, #0
	ldr	sl, .L1673+4
	mov	ip, r8, asl #2
	str	ip, [sp, #8]
	b	.L1665
.L1670:
	ldr	r1, .L1673
	movw	r2, #3918
	ldr	ip, [sp, #8]
	mov	r0, r6
	ldr	r3, [sl, #-908]
	ldrh	fp, [r1, r2]
	ldr	r3, [r3, ip]
	mov	r1, fp
	str	r3, [sp, #4]
	bl	__aeabi_uidivmod
	ldr	r3, [sp, #4]
	ldr	r2, .L1673+4
	uxth	r1, r1
	str	r1, [sp, #12]
	rsb	r5, r1, fp
	uxth	r5, r5
	cmp	r5, r4
	uxthhi	r5, r4
	cmp	r3, #0
	beq	.L1667
	cmp	r5, fp
	beq	.L1667
	str	r3, [sp, #24]
	mov	r1, #1
	ldr	r3, [r2, #-956]
	add	r0, sp, #20
	mov	r2, r1
	str	r3, [sp, #28]
	mov	r3, #0
	str	r3, [sp, #32]
	bl	FlashReadPages
	b	.L1668
.L1667:
	ldr	ip, .L1673
	movw	r3, #3922
	ldr	r0, [r2, #-956]
	mov	r1, #0
	ldrh	r2, [ip, r3]
	bl	ftl_memset
.L1668:
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
	ldr	r0, .L1673+8
	add	r8, r8, #1
	ldr	r2, [sl, #-956]
	add	r9, r9, fp
	bl	FtlMapWritePage
	ldr	ip, [sp, #8]
	add	ip, ip, #4
	str	ip, [sp, #8]
	cmn	r0, #1
	moveq	r7, r0
.L1665:
	cmp	r4, #0
	bne	.L1670
.L1664:
	mov	r0, r7
	add	sp, sp, #60
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, sl, fp, pc}
.L1674:
	.align	2
.L1673:
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
	ldr	r2, .L1677
	ldr	r3, .L1677+4
	ldr	r1, [r2, #-1712]
	cmp	r1, r3
	bxne	lr
	ldr	r3, .L1677+8
	mov	r0, #0
	mov	r1, #1
	str	r3, [r2, #-1708]
	ldr	r3, [r2, #-1816]
	str	r3, [r2, #-1624]
	ldr	r3, [r2, #-1812]
	str	r3, [r2, #-1620]
	ldr	r3, [r2, #-1820]
	str	r3, [r2, #-1704]
	ldr	r3, [r2, #-1832]
	str	r3, [r2, #-1700]
	ldr	r3, [r2, #-1840]
	str	r3, [r2, #-1696]
	ldr	r3, [r2, #-1824]
	str	r3, [r2, #-1692]
	ldr	r3, [r2, #-1796]
	str	r3, [r2, #-1684]
	ldr	r3, [r2, #-2080]
	str	r3, [r2, #-1680]
	ldr	r3, [r2, #-1836]
	str	r3, [r2, #-1676]
	ldr	r3, [r2, #-1828]
	str	r3, [r2, #-1672]
	ldr	r3, [r2, #-1788]
	str	r3, [r2, #-1668]
	ldr	r3, [r2, #-1784]
	str	r3, [r2, #-1664]
	ldr	r3, [r2, #-1716]
	str	r3, [r2, #-1652]
	ldr	r3, [r2, #-1004]
	str	r3, [r2, #-1648]
	sub	r2, r2, #1712
	b	FtlVendorPartWrite
.L1678:
	.align	2
.L1677:
	.word	.LANCHOR2
	.word	1179929683
	.word	1342177352
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
	ldr	r3, .L1686
	ldr	r1, .L1686+4
	ldr	r3, [r3, #3836]
	cmp	r3, #0
	moveq	r2, #32
	beq	.L1680
	ldr	r2, [r1, #-1784]
	cmp	r2, #29
	movls	r2, #4
	movhi	r2, #32
.L1680:
	mov	r3, #288
	ldrh	r1, [r1, r3]
	cmp	r1, #31
	addls	r1, r1, #1
	ldrls	r2, .L1686+4
	strlsh	r1, [r2, r3]	@ movhi
	movls	r2, #1
	cmp	r0, #0
	ldr	r3, .L1686+4
	bne	.L1682
	ldr	r1, [r3, #-924]
	ldr	r0, [r1, #20]
	ldr	r1, [r1, #16]
	add	r2, r2, r0
	cmp	r1, r2
	bcc	.L1683
.L1682:
	ldr	r2, [r3, #-924]
	mov	r0, #64
	ldr	r1, [r2, #16]
	str	r1, [r2, #20]
	ldr	r1, .L1686+8
	str	r1, [r2, #0]
	ldr	r1, .L1686+12
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
.L1683:
	mov	r0, #0
	ldmfd	sp!, {r3, pc}
.L1687:
	.align	2
.L1686:
	.word	.LANCHOR0
	.word	.LANCHOR2
	.word	1112818501
	.word	-928
	.fnend
	.size	FtlEctTblFlush, .-FtlEctTblFlush
	.align	2
	.type	ftl_load_l2p_region, %function
ftl_load_l2p_region:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	stmfd	sp!, {r3, r4, r5, r6, r7, lr}
	.save {r3, r4, r5, r6, r7, lr}
	mov	r5, r0
	ldr	r4, .L1692
	mov	r7, #12
	ldr	r3, [r4, #-1036]
	ldr	r6, [r3, r0, asl #2]
	cmp	r6, #0
	bne	.L1689
	mul	r7, r7, r1
	ldr	r3, [r4, #-1856]
	ldr	r2, .L1692+4
	mov	r1, #255
	add	r3, r3, r7
	ldr	r0, [r3, #8]
	movw	r3, #3922
	ldrh	r2, [r2, r3]
	bl	ftl_memset
	ldr	r3, [r4, #-1856]
	strh	r5, [r3, r7]	@ movhi
	ldr	r3, [r4, #-1856]
	add	r7, r3, r7
	str	r6, [r7, #4]
	b	.L1690
.L1689:
	mul	r7, r7, r1
	ldr	r3, [r4, #-1856]
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
	bne	.L1691
	mov	r2, r6
	mov	r1, r5
	ldr	r0, .L1692+8
	mov	r6, r6, lsr #10
	bl	printk
	ldr	r3, .L1692+12
	ldr	r0, .L1692+16
	mov	r1, r5
	strh	r6, [r4, r3]	@ movhi
	ldr	r3, [r4, #-1856]
	add	r3, r3, r7
	ldr	r2, [r3, #8]
	bl	FtlMapWritePage
.L1691:
	ldr	r3, .L1692
	mov	r1, #0
	ldr	r3, [r3, #-1856]
	add	r2, r3, r7
	str	r1, [r2, #4]
	strh	r5, [r3, r7]	@ movhi
.L1690:
	mov	r0, #0
	ldmfd	sp!, {r3, r4, r5, r6, r7, pc}
.L1693:
	.align	2
.L1692:
	.word	.LANCHOR2
	.word	.LANCHOR0
	.word	.LC123
	.word	-1052
	.word	.LANCHOR2-1092
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
	ldr	r3, .L1703
	mov	r2, #3920
	mov	r4, r1
	ldrh	r7, [r3, r2]
	mvn	r2, #0
	add	r7, r7, #7
	mov	r6, r0, lsr r7
	bic	r7, r0, r2, asl r7
	movw	r2, #3950
	ldrh	r2, [r3, r2]
	uxth	r6, r6
	ldr	r3, .L1703+4
	uxth	r7, r7
	ldr	sl, [r3, #-1856]
	mov	r3, #0
	mov	r5, r3
	b	.L1695
.L1701:
	add	r3, r3, #12
	add	r1, sl, r3
	ldrh	r1, [r1, #-12]
	cmp	r1, r6
	bne	.L1696
.L1697:
	cmp	r8, #0
	ldr	r3, .L1703+4
	mov	r2, #12
	bne	.L1698
	ldr	r3, [r3, #-1856]
	mla	r2, r2, r5, r3
	ldr	r3, [r2, #8]
	ldr	r3, [r3, r7, asl #2]
	str	r3, [r4, #0]
	b	.L1699
.L1698:
	mul	r2, r2, r5
	ldr	r1, [r3, #-1856]
	ldr	r0, [r4, #0]
	add	r1, r1, r2
	ldr	r1, [r1, #8]
	str	r0, [r1, r7, asl #2]
	ldr	r1, [r3, #-1856]
	add	r2, r1, r2
	ldr	r1, [r2, #4]
	orr	r1, r1, #-2147483648
	str	r1, [r2, #4]
	ldr	r2, .L1703+8
	strh	r6, [r3, r2]	@ movhi
.L1699:
	ldr	r3, .L1703+4
	mov	r2, #12
	mov	r0, #0
	ldr	r3, [r3, #-1856]
	mla	r5, r2, r5, r3
	ldr	r3, [r5, #4]
	cmn	r3, #1
	addne	r3, r3, #1
	strne	r3, [r5, #4]
	ldmfd	sp!, {r3, r4, r5, r6, r7, r8, sl, pc}
.L1696:
	add	r5, r5, #1
	uxth	r5, r5
.L1695:
	cmp	r5, r2
	bne	.L1701
	bl	select_l2p_ram_region
	mov	r3, #12
	mul	r3, r3, r0
	mov	r5, r0
	add	r2, sl, r3
	ldrh	r1, [sl, r3]
	movw	r3, #65535
	cmp	r1, r3
	beq	.L1702
	ldr	r3, [r2, #4]
	cmp	r3, #0
	bge	.L1702
	bl	flush_l2p_region
.L1702:
	mov	r0, r6
	mov	r1, r5
	bl	ftl_load_l2p_region
	b	.L1697
.L1704:
	.align	2
.L1703:
	.word	.LANCHOR0
	.word	.LANCHOR2
	.word	-1852
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
	ldr	r3, .L1711
	ldr	r1, [r3, #-2020]
	mov	r5, r0, asl #1
	ldrh	r2, [r1, r5]
	cmp	r2, #0
	addne	r2, r2, #1
	strneh	r2, [r1, r5]	@ movhi
	bne	.L1707
	ldr	r4, [r3, #-2008]
	cmp	r4, #0
	beq	.L1707
	ldr	r1, .L1711+4
	mov	ip, #6
	ldrh	lr, [r3, r1]
	ldr	r3, [r3, #-2028]
	ldr	r1, .L1711+8
	rsb	r4, r3, r4
	mov	r4, r4, asr #1
	mul	r4, r1, r4
	movw	r1, #65535
	uxth	r4, r4
	b	.L1708
.L1710:
	cmp	r4, r0
	bne	.L1709
	mov	r1, r4
	ldr	r0, .L1711+12
	bl	List_remove_node
	ldr	r6, .L1711
	ldr	r3, .L1711+4
	mov	r0, r4
	ldrh	r2, [r6, r3]
	sub	r2, r2, #1
	strh	r2, [r6, r3]	@ movhi
	bl	INSERT_DATA_LIST
	ldr	r3, [r6, #-2020]
	ldrh	r2, [r3, r5]
	add	r2, r2, #1
	strh	r2, [r3, r5]	@ movhi
	b	.L1707
.L1709:
	mul	r4, ip, r4
	ldrh	r4, [r3, r4]
	cmp	r4, r1
	beq	.L1707
	add	r2, r2, #1
	uxth	r2, r2
.L1708:
	cmp	r2, lr
	bne	.L1710
.L1707:
	mov	r0, r7
	add	r1, sp, #4
	mov	r2, #1
	bl	log2phys
	ldmfd	sp!, {r1, r2, r3, r4, r5, r6, r7, pc}
.L1712:
	.align	2
.L1711:
	.word	.LANCHOR2
	.word	-2004
	.word	-1431655765
	.word	.LANCHOR2-2008
	.fnend
	.size	FtlReUsePrevPpa, .-FtlReUsePrevPpa
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
	ldr	r4, .L1721
	ldr	r6, .L1721+4
	ldr	r2, [r4, #-1628]
	ldr	r3, [r6, #3972]
	cmp	r2, r3
	bcs	.L1714
	mov	r5, #2048
.L1717:
	ldr	r0, [r4, #-1628]
	ldr	r3, [r6, #3972]
	cmp	r0, r3
	bcs	.L1719
	mov	r1, sp
	mov	r2, #0
	bl	log2phys
	ldr	r0, [sp, #0]
	ldr	r3, [r4, #-1628]
	cmn	r0, #1
	add	r3, r3, #1
	str	r3, [r4, #-1628]
	beq	.L1716
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
	bne	.L1719
	ldr	r0, [sp, #0]
	ubfx	r0, r0, #10, #16
	bl	P2V_block_in_plane
	bl	FtlGcRefreshBlock
	b	.L1719
.L1716:
	subs	r5, r5, #1
	bne	.L1717
	b	.L1715
.L1714:
	ldr	r3, [r4, #-1840]
	mov	r0, #0
	str	r0, [r4, #-1632]
	str	r0, [r4, #-1628]
	str	r3, [r4, #-1636]
	b	.L1715
.L1719:
	mvn	r0, #0
.L1715:
	add	sp, sp, #40
	ldmfd	sp!, {r4, r5, r6, pc}
.L1722:
	.align	2
.L1721:
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
	ldr	r3, .L1730
	ldr	r2, [r3, #-1632]
	cmp	r2, #0
	beq	.L1724
	b	FtlReadRefresh.part.15
.L1724:
	ldr	r2, .L1730+4
	ldr	r0, [r3, #-1788]
	ldr	r1, [r2, #3836]
	ldrb	r2, [r2, #928]	@ zero_extendqisi2
	cmp	r2, #0
	ldr	r2, [r3, #-1840]
	addeq	r0, r1, r0, lsr #10
	moveq	r1, #33554432
	movne	r0, #4194304
	moveq	r0, r1, asr r0
	ldr	r1, [r3, #-1636]
	add	r3, r2, #1048576
	cmp	r1, r3
	bhi	.L1726
	add	r1, r0, r1
	cmp	r1, r2
	bcc	.L1726
	ldr	r3, .L1730
	ldrb	r3, [r3, #-2048]	@ zero_extendqisi2
	cmp	r3, #0
	bne	.L1727
.L1726:
	ldr	r3, .L1730
	mov	r1, #1
	str	r2, [r3, #-1636]
	str	r1, [r3, #-1632]
	mov	r1, #0
	str	r1, [r3, #-1628]
.L1727:
	mov	r0, #0
	bx	lr
.L1731:
	.align	2
.L1730:
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
	.pad #36
	sub	sp, sp, #36
	ldr	r3, [r0, #16]
	mov	r4, r0
	ldrh	r8, [r0, #6]
	mov	r1, #0
	ldr	fp, [r0, #24]
	mov	r9, #0
	str	r3, [sp, #24]
	ldrh	r3, [r0, #8]
	mov	r2, r8, asl #2
	ldr	r7, [r0, #12]
	mov	r0, fp
	str	r3, [sp, #16]
	bl	ftl_memset
	ldr	r3, .L1767
	mov	sl, r7
	str	r9, [r4, #32]
	str	r9, [r4, #28]
	ldr	r2, [r3, #-964]
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
	ldr	r3, .L1767+4
	b	.L1733
.L1747:
	ldr	r0, [sp, #20]
	cmp	r2, r0
	mov	r2, r2, asl #1
	bne	.L1734
	ldrh	r0, [sl, r2]
	mov	r1, #1
	add	r3, sl, r2
	str	r3, [sp, #16]
	bl	FtlGetLastWrittenPage
	ldr	r1, [sp, #20]
	ldr	r2, [sp, #24]
	mov	r7, sl
	strh	r9, [r4, #0]	@ movhi
	mov	sl, #0
	ldr	r6, .L1767
	uxth	r0, r0
	add	r3, r0, #1
	strh	r3, [r4, #2]	@ movhi
	ldr	r3, [r2, r1, asl #2]
	sxth	r9, r0
	add	r9, r9, #1
	str	r3, [r4, #28]
	b	.L1735
.L1738:
	ldr	r0, [sp, #16]
	mov	r1, #1
	ldrh	r2, [r0, #0]
	ldr	r0, .L1767+8
	orr	r3, r3, r2, asl #10
	mov	r2, r1
	str	r3, [r6, #180]
	bl	FlashReadPages
	ldr	r3, [r6, #176]
	cmn	r3, #1
	ldreq	r1, [sp, #16]
	ldreqh	r3, [r1, #0]
	streqh	r3, [r4, #40]	@ movhi
	beq	.L1737
	ldrh	r3, [r5, #8]
	cmp	r3, r8
	bcs	.L1737
	ldrh	r2, [r4, #4]
	ldrh	r1, [r5, #0]
	cmp	r1, r2
	ldreq	r2, [r6, #180]
	streq	r2, [fp, r3, asl #2]
.L1737:
	add	sl, sl, #1
	uxth	sl, sl
.L1735:
	sxth	r3, sl
	cmp	r3, r9
	blt	.L1738
	b	.L1739
.L1734:
	ldr	r6, .L1767
	movw	r7, #3914
	add	r0, sl, r2
	str	r0, [sp, #28]
	add	r0, r6, #176
	ldr	r1, [r6, #-964]
	str	r1, [r6, #184]
	ldrh	r1, [sl, r2]
	ldrh	r2, [r3, r7]
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
	beq	.L1760
	ldrh	r1, [r5, #0]
	ldrh	r2, [r4, #4]
	cmp	r1, r2
	bne	.L1760
	ldrh	r1, [r5, #8]
	movw	r2, #64245
	cmp	r1, r2
	bne	.L1760
	b	.L1761
.L1743:
	ldr	r0, [r6, #-964]
	mov	ip, r1, asl #3
	add	r2, r2, #1
	ldr	r1, [r0, r1, asl #3]
	uxth	r2, r2
	uxth	r1, r1
	cmp	r1, r8
	addcc	r0, r0, ip
	ldrcc	r0, [r0, #4]
	strcc	r0, [fp, r1, asl #2]
	b	.L1741
.L1761:
	mov	r2, #0
.L1741:
	ldrh	r0, [r3, r7]
	sxth	r1, r2
	sub	r0, r0, #1
	cmp	r1, r0
	blt	.L1743
	b	.L1744
.L1746:
	ldr	r0, [sp, #28]
	ldrh	r1, [r0, #0]
	ldr	r0, .L1767+8
	orr	r2, r2, r1, asl #10
	mov	r1, #1
	str	r2, [r7, #180]
	mov	r2, r1
	str	r3, [sp, #12]
	bl	FlashReadPages
	ldr	r2, [r7, #176]
	ldr	r3, [sp, #12]
	cmn	r2, #1
	beq	.L1745
	ldrh	r2, [r5, #8]
	cmp	r2, r8
	bcs	.L1745
	ldrh	r1, [r4, #4]
	ldrh	r0, [r5, #0]
	cmp	r0, r1
	ldreq	r1, [r7, #180]
	streq	r1, [fp, r2, asl #2]
.L1745:
	add	r6, r6, #1
	uxth	r6, r6
	b	.L1763
.L1760:
	ldr	r7, .L1767
	mov	r6, #0
.L1763:
	movw	r1, #3914
	sxth	r2, r6
	ldrh	r1, [r3, r1]
	cmp	r2, r1
	blt	.L1746
.L1744:
	add	r9, r9, #1
	uxth	r9, r9
.L1733:
	ldr	r1, [sp, #16]
	sxth	r2, r9
	cmp	r2, r1
	blt	.L1747
	mov	r7, sl
.L1739:
	ldr	r3, .L1767+12
	cmp	r4, r3
	bne	.L1748
	ldr	r9, .L1767+4
	movw	r3, #3956
	ldrh	r3, [r9, r3]
	cmp	r3, #0
	bne	.L1762
	b	.L1748
.L1756:
	ldr	r3, [r5, #-904]
	mov	r4, r8, asl #2
	ldr	r2, [r3, r8, asl #2]
	sub	r1, r2, #1
	cmn	r1, #3
	bhi	.L1750
	ldr	r1, [r5, #-1036]
	ldr	r1, [r1, r8, asl #2]
	cmp	r2, r1
	beq	.L1750
	ldr	r2, [r5, #-964]
	mov	r1, #1
	ldr	r6, [r5, #-940]
	ldr	r0, .L1767+8
	str	r2, [r5, #184]
	mov	r2, r1
	str	r6, [r5, #188]
	ldr	r3, [r3, r4]
	str	r3, [r5, #180]
	bl	FlashReadPages
	ldr	r3, [r5, #176]
	cmn	r3, #1
	beq	.L1750
	ldr	r3, .L1767+16
	ldrh	r2, [r6, #0]
	ldrh	r3, [r5, r3]
	cmp	r2, r3
	bne	.L1750
	ldr	r3, [r5, #-1036]
	ldr	fp, [r6, #4]
	ldr	r2, [r3, r4]
	str	r2, [r5, #180]
	ldr	r3, [r3, r4]
	cmp	r3, #0
	streq	r3, [r6, #4]
	beq	.L1752
	mov	r1, #1
	ldr	r0, .L1767+8
	mov	r2, r1
	bl	FlashReadPages
.L1752:
	ldr	r1, [r6, #4]
	cmp	fp, r1
	bls	.L1750
	ldr	r2, [r5, #-904]
	ldr	r3, [r5, #-1036]
	ldr	r0, .L1767+20
	str	r1, [sp, #0]
	ldr	r1, [r2, r4]
	ldr	r2, [r3, r4]
	mov	r3, fp
	bl	printk
	ldr	r3, [r5, #-904]
	ldr	r2, [r3, r4]
	ldr	r3, [r5, #-1036]
	str	r2, [r3, r4]
	mov	r2, #0
	ldr	r3, [r5, #-904]
	ldr	r0, [r3, r4]
	ldr	r3, .L1767+24
	ubfx	r0, r0, #10, #16
	ldrh	r3, [r5, r3]
	b	.L1753
.L1754:
	mov	r1, r1, asl #1
	ldrh	r1, [r7, r1]
	cmp	r1, r0
	beq	.L1750
	add	r2, r2, #1
	uxth	r2, r2
.L1753:
	sxth	r1, r2
	cmp	r1, r3
	blt	.L1754
	b	.L1765
.L1766:
	mov	r3, r3, asl #1
	strh	r0, [r7, r3]	@ movhi
	ldr	r3, .L1767+24
	ldrh	r2, [r5, r3]
	add	r2, r2, #1
	strh	r2, [r5, r3]	@ movhi
	bl	remove_from_free_sys_Queue
.L1750:
	add	r8, r8, #1
	b	.L1749
.L1762:
	ldr	r5, .L1767
	mov	r8, #0
	mov	sl, r4
.L1749:
	movw	r2, #3948
	ldrh	r3, [r9, r2]
	cmp	r8, r3
	bcc	.L1756
	mov	r4, sl
.L1748:
	mov	r0, r4
	bl	ftl_free_no_use_map_blk
	ldr	r1, .L1767+4
	movw	r3, #3914
	ldrh	r2, [r4, #2]
	ldrh	r3, [r1, r3]
	cmp	r2, r3
	bne	.L1757
	mov	r0, r4
	bl	ftl_map_blk_alloc_new_blk
.L1757:
	mov	r0, r4
	bl	ftl_map_blk_gc
	mov	r0, r4
	bl	ftl_map_blk_gc
	mov	r0, #0
	add	sp, sp, #36
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, sl, fp, pc}
.L1765:
	ldr	r2, [r9, #3940]
	cmp	r3, r2
	bcs	.L1750
	b	.L1766
.L1768:
	.align	2
.L1767:
	.word	.LANCHOR2
	.word	.LANCHOR0
	.word	.LANCHOR2+176
	.word	.LANCHOR2-1092
	.word	-1088
	.word	.LC124
	.word	-1048
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
	ldr	r0, .L1770
	bl	FtlMapTblRecovery
	mov	r0, #0
	ldmfd	sp!, {r3, pc}
.L1771:
	.align	2
.L1770:
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
	movw	r2, #3932
	ldr	r3, .L1773
	ldr	r0, .L1773+4
	ldrh	r2, [r3, r2]
	strh	r2, [r0, #254]	@ movhi
	ldr	r2, .L1773+8
	strh	r2, [r0, #248]	@ movhi
	movw	r2, #3958
	ldrh	r2, [r3, r2]
	strh	r2, [r0, #252]	@ movhi
	movw	r2, #3934
	ldrh	r2, [r3, r2]
	ldr	r3, [r3, #3960]
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
.L1774:
	.align	2
.L1773:
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
	beq	.L1776
	ldrh	r3, [r0, #2]
	ldr	r1, .L1865
	ldrb	fp, [r0, #6]	@ zero_extendqisi2
	str	r3, [sp, #12]
	movw	r3, #3912
	ldrh	r3, [r1, r3]
	ldr	ip, [sp, #12]
	str	fp, [sp, #24]
	cmp	r3, ip
	mov	r3, #0
	streqh	r3, [r0, #4]	@ movhi
	moveq	r2, r3	@ movhi
	ldrneh	r0, [r0, #16]
	bne	.L1778
	b	.L1862
.L1779:
	add	r3, r3, #1
	uxth	r3, r3
	add	r1, r4, r3, asl #1
	ldrh	r0, [r1, #16]
.L1778:
	cmp	r0, r2
	beq	.L1779
	ldrb	r1, [r4, #8]	@ zero_extendqisi2
	cmp	r1, #1
	bne	.L1780
	bl	FtlGetLastWrittenPage
	cmn	r0, #1
	mov	r7, r0
	beq	.L1781
	ldr	r3, .L1865
	ldrb	r2, [r3, #928]	@ zero_extendqisi2
	cmp	r2, #0
	bne	.L1855
	add	r3, r3, r0, asl #1
	movw	r2, #3076
	ldrh	r8, [r3, r2]
	b	.L1847
.L1780:
	mov	r1, #0
	bl	FtlGetLastWrittenPage
	cmn	r0, #1
	mov	r7, r0
	beq	.L1781
.L1855:
	mov	r8, r7
.L1847:
	ldr	r3, .L1865
	movw	r2, #3844
	mov	r5, #0
	str	r4, [sp, #32]
	movw	ip, #65535
	ldrh	r9, [r3, r2]
	ldr	r2, .L1865+4
	ldr	sl, [r2, #-976]
	ldr	r6, [r2, #-1172]
	movw	r2, #3924
	ldrh	lr, [r3, r2]
	mov	r2, r4
	mov	r3, r5
	b	.L1783
.L1781:
	mov	r3, #0
	strh	r3, [r4, #2]	@ movhi
	mov	r2, r3	@ movhi
.L1862:
	strb	r2, [r4, #6]
	b	.L1776
.L1785:
	ldrh	r0, [r2, #16]
	cmp	r0, ip
	beq	.L1784
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
.L1784:
	add	r3, r3, #1
	add	r2, r2, #2
	uxth	r3, r3
.L1783:
	cmp	r3, r9
	bne	.L1785
	ldrb	r3, [r4, #8]	@ zero_extendqisi2
	cmp	r3, #1
	movne	r3, #0
	bne	.L1856
	ldr	r3, .L1865
	ldrb	r3, [r3, #928]	@ zero_extendqisi2
	adds	r3, r3, #0
	movne	r3, #1
.L1856:
	ldr	r6, .L1865+4
	mov	r1, r5
	str	r3, [sp, #20]
	mov	sl, #0
	ldr	r2, [sp, #20]
	ldr	r0, [r6, #-976]
	bl	FlashReadPages
	ldr	fp, [r6, #-1804]
	movw	ip, #65535
	str	ip, [sp, #4]
	str	fp, [sp, #8]
	b	.L1787
.L1792:
	mov	lr, #36
	ldr	r1, [r6, #-976]
	mul	r2, lr, sl
	add	r3, r1, r2
	ldr	r2, [r1, r2]
	cmp	r2, #0
	bne	.L1788
	ldr	r3, [r3, #12]
	ldr	fp, [r3, #4]
	cmn	fp, #1
	beq	.L1789
	ldr	r1, [r6, #-1804]
	mov	r0, fp
	str	r3, [sp, #0]
	bl	ftl_cmp_data_ver
	ldr	r3, [sp, #0]
	cmp	r0, #0
	addne	fp, fp, #1
	strne	fp, [r6, #-1804]
.L1789:
	ldr	r3, [r3, #0]
	cmn	r3, #1
	bne	.L1791
	b	.L1790
.L1788:
	ldr	r1, [r3, #4]
	movw	fp, #290
	ldr	r0, .L1865+8
	bl	printk
	ldrh	r3, [r4, #0]
	uxth	r2, r8
	str	r2, [sp, #4]
	strh	r3, [r6, fp]	@ movhi
.L1791:
	add	sl, sl, #1
.L1787:
	uxth	r9, sl
	cmp	r9, r5
	bcc	.L1792
.L1790:
	ldr	r3, .L1865+4
	cmp	r9, r5
	addeq	r7, r7, #1
	uxthne	r7, r7
	movne	r2, #36
	ldrne	r3, [r3, #-976]
	uxtheq	r7, r7
	ldreq	r3, [r3, #-976]
	mlane	r9, r2, r9, r3
	ldreq	r0, [r3, #4]
	ldrne	r0, [r9, #4]
	ubfx	r0, r0, #10, #16
	bl	P2V_plane
	ldrb	r2, [r4, #8]	@ zero_extendqisi2
	ldr	r3, .L1865
	cmp	r2, #1
	str	r0, [sp, #16]
	bne	.L1795
	ldrb	r1, [r3, #928]	@ zero_extendqisi2
	cmp	r1, #0
	addeq	r7, r3, r7, asl #1
	movweq	r1, #3076
	ldreqh	r7, [r7, r1]
.L1795:
	movw	r1, #3912
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
	beq	.L1863
.L1797:
	ldr	fp, [sp, #4]
	movw	r3, #65535
	ldr	ip, [sp, #8]
	cmp	fp, r3
	sub	r6, ip, #1
	bne	.L1798
	cmp	r2, #0
	bne	.L1799
.L1798:
	ldr	r2, .L1865+4
	uxth	r3, r8
	ldr	ip, [sp, #12]
	mvn	fp, #0
	mov	sl, r3
	mov	r9, fp
	ldr	r1, [r2, #-1012]
	ldr	r8, .L1865+4
	cmn	r1, #1
	streq	r6, [r2, #-1012]
	ldr	r2, [r2, #-1012]
	str	r2, [sp, #4]
	add	r2, ip, #7
	cmp	r3, r2
	subgt	r5, r3, #7
	mov	r3, r7
	ldrle	r5, [sp, #12]
	mov	r7, r6
	uxthgt	r5, r5
	b	.L1802
.L1804:
	ldrh	r0, [r1, #16]
	movw	lr, #65535
	cmp	r0, lr
	beq	.L1803
	ldr	ip, [sp, #8]
	mov	r3, #36
	orr	r0, r5, r0, asl #10
	mla	lr, r3, r6, ip
	add	r6, r6, #1
	uxth	r6, r6
	str	r0, [lr, #4]
.L1803:
	add	r2, r2, #1
	add	r1, r1, #2
	uxth	r2, r2
.L1815:
	ldr	lr, [sp, #36]
	cmp	r2, lr
	bne	.L1804
	ldr	r3, [sp, #28]
	mov	r1, r6
	ldr	r0, [r8, #-976]
	ldr	r2, [sp, #20]
	str	r3, [sp, #0]
	bl	FlashReadPages
	ldr	r2, .L1865
	add	r0, r8, r5, asl #1
	mov	r1, #0
	str	r0, [sp, #8]
	ldr	r3, [sp, #0]
	ldrb	ip, [r2, #928]	@ zero_extendqisi2
	ldr	r2, [r8, #-976]
	str	ip, [sp, #28]
	b	.L1805
.L1813:
	ldr	r0, [r2, #0]
	cmp	r0, #0
	bne	.L1806
	ldr	r0, [r2, #12]
	movw	ip, #65535
	ldrh	lr, [r0, #0]
	cmp	lr, ip
	beq	.L1807
	ldr	r0, [r0, #4]
	cmn	r0, #1
	beq	.L1807
	cmn	fp, #1
	ldr	r9, [r8, #-1012]
	str	r0, [r8, #-1012]
	bne	.L1807
	ldr	lr, [sp, #8]
	sub	r0, lr, #896
	ldrh	r0, [r0, #0]
	cmp	r0, ip
	bne	.L1808
	ldr	r0, [sp, #28]
	cmp	r0, #0
	beq	.L1807
.L1808:
	cmp	r9, r7
	mvneq	fp, #0
	movne	fp, r9
	b	.L1807
.L1806:
	mov	r6, r7
	ldrh	r1, [r4, #0]
	mov	r7, r3
	ldr	r3, .L1865+4
	movw	r2, #290
	strh	r1, [r3, r2]	@ movhi
	ldrb	r2, [r4, #8]	@ zero_extendqisi2
	cmp	r2, #0
	bne	.L1799
	add	r5, r3, r5, asl #1
	movw	r2, #65535
	sub	r5, r5, #896
	ldrh	r1, [r5, #0]
	cmp	r1, r2
	bne	.L1809
	cmn	fp, #1
	bne	.L1858
.L1810:
	ldr	fp, [sp, #4]
	cmp	fp, r6
	beq	.L1811
.L1858:
	str	fp, [r3, #-1012]
	b	.L1799
.L1811:
	ldr	r2, [r3, #-1012]
	b	.L1864
.L1809:
	cmp	r9, r6
	beq	.L1812
	cmn	r9, #1
	strne	r9, [r3, #-1012]
	b	.L1799
.L1812:
	ldr	r2, [r3, #-1012]
	cmp	r2, r6
	beq	.L1799
.L1864:
	sub	r2, r2, #1
	b	.L1859
.L1807:
	add	r1, r1, #1
	add	r2, r2, #36
	uxth	r1, r1
.L1805:
	cmp	r1, r6
	bne	.L1813
	add	r5, r5, #1
	uxth	r5, r5
.L1802:
	cmp	r5, sl
	bhi	.L1814
	ldr	r1, .L1865
	movw	r2, #3844
	ldr	lr, [r8, #-976]
	mov	r6, #0
	str	r3, [sp, #28]
	ldrh	ip, [r1, r2]
	mov	r1, r4
	str	lr, [sp, #8]
	mov	r2, r6
	str	ip, [sp, #36]
	b	.L1815
.L1814:
	mov	r6, r7
	mov	r7, r3
	ldr	r3, .L1865+4
	mvn	r2, #0
.L1859:
	str	r2, [r3, #-1012]
.L1799:
	ldr	r5, .L1865+4
	mov	r3, #292
	mov	r2, #1
	ldr	r0, .L1865+12
	strh	r2, [r5, r3]	@ movhi
	bl	FtlMapBlkWriteDumpData
	ldr	r8, [sp, #12]
	str	r7, [sp, #8]
.L1816:
	ldr	r3, .L1865
	movw	r2, #3844
	mov	r7, #0
	ldr	lr, [r5, #-976]
	mov	fp, #36
	ldrh	sl, [r3, r2]
	mov	r2, r4
	ldrb	ip, [r3, #928]	@ zero_extendqisi2
	mov	r3, r7
	b	.L1817
.L1820:
	ldrh	r0, [r2, #16]
	movw	r1, #65535
	cmp	r0, r1
	beq	.L1818
	mla	r1, fp, r7, lr
	orr	r0, r8, r0, asl #10
	str	r0, [r1, #4]
	ldrb	r9, [r4, #8]	@ zero_extendqisi2
	cmp	r9, #1
	bne	.L1819
	cmp	ip, #0
	orrne	r0, r0, #-2147483648
	strne	r0, [r1, #4]
.L1819:
	add	r7, r7, #1
	uxth	r7, r7
.L1818:
	add	r3, r3, #1
	add	r2, r2, #2
	uxth	r3, r3
.L1817:
	cmp	r3, sl
	bne	.L1820
	mov	r1, r7
	ldr	r0, [r5, #-976]
	ldr	r2, [sp, #20]
	bl	FlashReadPages
	mov	r3, #36
	mul	r3, r3, r7
	mov	r7, #0
	str	r3, [sp, #36]
	b	.L1821
.L1842:
	ldr	r9, [r5, #-976]
	add	r9, r9, r7
	ldr	sl, [r9, #4]
	ubfx	r0, sl, #10, #16
	str	sl, [sp, #52]
	bl	P2V_plane
	ldr	r3, [sp, #12]
	cmp	r8, r3
	bcc	.L1822
	ldr	fp, [sp, #24]
	mov	ip, r3
	cmp	r0, fp
	movcs	r3, #0
	movcc	r3, #1
	cmp	r8, ip
	movne	r3, #0
	cmp	r3, #0
	bne	.L1822
	ldr	r3, [sp, #16]
	ldr	ip, [sp, #8]
	cmp	r0, r3
	cmpeq	r8, ip
	beq	.L1854
	ldr	r3, [r9, #0]
	cmn	r3, #1
	beq	.L1824
	ldr	sl, [r9, #12]
	movw	r3, #61589
	ldrh	r2, [sl, #0]
	cmp	r2, r3
	bne	.L1831
.L1825:
	ldr	r6, [sl, #4]
	cmn	r6, #1
	beq	.L1826
	ldr	r1, [r5, #-1804]
	mov	r0, r6
	bl	ftl_cmp_data_ver
	cmp	r0, #0
	addne	r3, r6, #1
	strne	r3, [r5, #-1804]
.L1826:
	ldr	r9, [sl, #8]
	add	r1, sp, #48
	ldr	r3, [sl, #12]
	mov	r2, #0
	mov	r0, r9
	str	r3, [sp, #44]
	bl	log2phys
	ldr	r1, [r5, #-1012]
	cmn	r1, #1
	beq	.L1827
	mov	r0, r6
	bl	ftl_cmp_data_ver
	cmp	r0, #0
	beq	.L1827
	ldr	r3, [sp, #44]
	cmn	r3, #1
	beq	.L1828
	ldr	r0, [r5, #-976]
	mov	r1, #1
	mov	r2, #0
	add	r0, r0, r7
	ldr	fp, [r0, #12]
	str	r3, [r0, #4]
	str	fp, [sp, #4]
	bl	FlashReadPages
	ldr	r3, [r5, #-976]
	ldr	ip, [fp, #4]
	add	fp, r3, r7
	ldr	r3, [r3, r7]
	str	ip, [sp, #28]
	cmn	r3, #1
	bne	.L1829
	b	.L1830
.L1828:
	ldr	r3, [sp, #52]
	ldr	r2, [sp, #48]
	cmp	r2, r3
	bne	.L1831
	mov	r0, r9
	add	r1, sp, #44
	mov	r2, #1
	bl	log2phys
.L1831:
	ldrh	r0, [r4, #0]
	b	.L1861
.L1829:
	ldr	r3, [sp, #4]
	ldr	sl, [r3, #8]
	cmp	sl, r9
	bne	.L1830
	ldr	r0, [r5, #-1012]
	ldr	r1, [sp, #28]
	bl	ftl_cmp_data_ver
	cmp	r0, #0
	beq	.L1830
	ldr	r3, [sp, #48]
	ldr	r2, [sp, #52]
	cmp	r3, r2
	beq	.L1835
.L1832:
	ldr	r2, [sp, #44]
	cmp	r3, r2
	beq	.L1830
	cmn	r3, #1
	streq	r3, [fp, #0]
	beq	.L1834
	ldr	ip, [fp, #12]
	mov	r0, fp
	str	r3, [fp, #4]
	mov	r1, #1
	mov	r2, #0
	str	ip, [sp, #4]
	bl	FlashReadPages
.L1834:
	ldr	r3, [r5, #-976]
	ldr	r3, [r3, r7]
	cmn	r3, #1
	beq	.L1835
	ldr	r3, [sp, #4]
	ldr	r0, [r5, #-1012]
	ldr	r9, [r3, #4]
	mov	r1, r9
	bl	ftl_cmp_data_ver
	cmp	r0, #0
	beq	.L1835
	ldr	r0, [sp, #28]
	mov	r1, r9
	bl	ftl_cmp_data_ver
	cmp	r0, #0
	beq	.L1830
.L1835:
	mov	r0, sl
	ldr	r1, [sp, #44]
	bl	FtlReUsePrevPpa
.L1830:
	ldrh	r0, [r4, #0]
	mvn	r3, #0
	str	r3, [sp, #44]
	bl	decrement_vpc_count
	b	.L1837
.L1827:
	ldr	r3, [sp, #52]
	ldr	r2, [sp, #48]
	cmp	r2, r3
	beq	.L1837
	mov	r0, r9
	add	r1, sp, #52
	mov	r2, #1
	bl	log2phys
	ldr	fp, [sp, #48]
	cmn	fp, #1
	beq	.L1837
	ldr	r3, [sp, #44]
	cmp	fp, r3
	beq	.L1837
	ubfx	r0, fp, #10, #16
	bl	P2V_block_in_plane
	ldr	r3, .L1865+16
	ldrh	r3, [r5, r3]
	cmp	r3, r0
	beq	.L1838
	ldr	r3, .L1865+20
	ldrh	r3, [r5, r3]
	cmp	r3, r0
	beq	.L1838
	ldr	r3, .L1865+24
	ldrh	r3, [r5, r3]
	cmp	r3, r0
	bne	.L1837
.L1838:
	ldr	ip, .L1865+4
	mov	r1, #1
	mov	r2, #0
	ldr	r0, [ip, #-976]
	str	fp, [r0, #4]
	ldr	sl, [r0, #12]
	bl	FlashReadPages
	ldr	r0, .L1865+4
	ldr	r1, [sl, #4]
	ldr	r3, [r0, #-976]
	ldr	r3, [r3, #0]
	cmn	r3, #1
	beq	.L1837
	mov	r0, r6
	bl	ftl_cmp_data_ver
	cmp	r0, #0
	bne	.L1837
	mov	r0, r9
	add	r1, sp, #48
	mov	r2, #1
	bl	log2phys
.L1837:
	ldr	r0, [sp, #44]
	cmn	r0, #1
	beq	.L1822
	ubfx	r0, r0, #10, #16
	bl	P2V_block_in_plane
	ldr	r2, [r5, #-2020]
	mov	r3, r0, asl #1
	mov	r1, r0
	ldrh	r3, [r2, r3]
	cmp	r3, #0
	beq	.L1839
.L1861:
	bl	decrement_vpc_count
	b	.L1822
.L1839:
	ldr	r0, .L1865+28
	bl	printk
	b	.L1822
.L1824:
	ldrh	r3, [r4, #0]
	movw	r1, #290
	mov	r2, r6
	ldr	r0, .L1865+32
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
	ldr	r3, [r5, #-1012]
	cmn	r3, #1
	beq	.L1860
.L1841:
	cmp	r3, r6
	bls	.L1822
.L1860:
	str	r6, [r5, #-1012]
.L1822:
	add	r7, r7, #36
.L1821:
	ldr	r3, [sp, #36]
	cmp	r7, r3
	bne	.L1842
	ldrb	r3, [r4, #8]	@ zero_extendqisi2
	add	r8, r8, #1
	cmp	r3, #1
	uxth	r8, r8
	bne	.L1843
	ldr	r3, .L1865
	ldrb	r2, [r3, #928]	@ zero_extendqisi2
	cmp	r2, #0
	beq	.L1843
	movw	r2, #3914
	ldr	ip, [sp, #8]
	ldrh	r3, [r3, r2]
	cmp	ip, r8
	cmpeq	r3, r8
	beq	.L1854
.L1843:
	ldr	r3, .L1865
	movw	r2, #3912
	ldrh	r2, [r3, r2]
	cmp	r8, r2
	bne	.L1816
	mov	r2, #0
	movw	r1, #3844
	strh	r2, [r4, #4]	@ movhi
	ldrh	r1, [r3, r1]
	mov	r3, r2
	strh	r8, [r4, #2]	@ movhi
	movw	r2, #65535
	ldr	r0, [sp, #32]
	b	.L1844
.L1846:
	add	r0, r0, #2
	ldrh	ip, [r0, #14]
	cmp	ip, r2
	strneb	r3, [r4, #6]
	bne	.L1776
.L1845:
	add	r3, r3, #1
	uxth	r3, r3
.L1844:
	cmp	r3, r1
	bne	.L1846
	b	.L1776
.L1854:
.L1823:
	ldr	r7, [sp, #8]
	ldr	r3, [sp, #16]
	strh	r7, [r4, #2]	@ movhi
	strb	r3, [r4, #6]
.L1863:
	mov	r0, r4
	mov	r1, r7
	mov	r2, r3
	bl	ftl_sb_update_avl_pages
.L1776:
	mov	r0, #0
	add	sp, sp, #60
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, sl, fp, pc}
.L1866:
	.align	2
.L1865:
	.word	.LANCHOR0
	.word	.LANCHOR2
	.word	.LC125
	.word	.LANCHOR2-1092
	.word	-2000
	.word	-1952
	.word	-1904
	.word	.LC126
	.word	.LC127
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
	ldr	r4, .L1876
	ldr	r3, [r4, #-2092]
	mov	r2, r4
	cmp	r3, #0
	bne	.L1867
	ldr	r3, .L1876+4
	ldrh	r3, [r4, r3]
	cmp	r3, #0
	beq	.L1869
	ldrb	r1, [r4, #-1992]	@ zero_extendqisi2
	cmp	r1, #0
	bne	.L1869
	ldr	r5, .L1876+8
	movw	r0, #3912
	ldrb	r1, [r4, #-1993]	@ zero_extendqisi2
	ldrh	r0, [r5, r0]
	mul	r1, r0, r1
	cmp	r3, r1
	beq	.L1869
	ldrb	r8, [r4, #-1990]	@ zero_extendqisi2
	movw	r3, #3844
	ldr	r7, [r5, #3972]
	cmp	r8, #0
	ldrh	r6, [r5, r3]
	bne	.L1867
	sub	r7, r7, #1
	mov	r1, sp
	mov	r2, r8
	mov	r0, r7
	bl	log2phys
	ldr	r3, [sp, #0]
	ldr	r0, [r4, #-964]
	ldr	r4, [r4, #-940]
	cmn	r3, #1
	str	r3, [sp, #8]
	str	r7, [sp, #20]
	str	r0, [sp, #12]
	str	r4, [sp, #16]
	str	r8, [r4, #4]
	beq	.L1870
	add	r0, sp, #4
	mov	r1, #1
	mov	r2, r8
	bl	FlashReadPages
	b	.L1871
.L1870:
	movw	r3, #3922
	mov	r1, #255
	ldrh	r2, [r5, r3]
	bl	ftl_memset
.L1871:
	ldr	r3, .L1876+12
	mov	r6, r6, asl #2
	ldr	r5, .L1876
	ldr	r8, .L1876+4
	strh	r3, [r4, #0]	@ movhi
	b	.L1872
.L1875:
	ldrh	r3, [r5, r8]
	cmp	r3, #0
	beq	.L1873
	ldr	r3, [sp, #8]
	sub	r0, r0, #2000
	str	r7, [r4, #8]
	sub	r6, r6, #1
	str	r3, [r4, #12]
	ldr	r3, .L1876+16
	ldrh	r3, [r5, r3]
	strh	r3, [r4, #2]	@ movhi
	bl	get_new_active_ppa
	ldr	r3, [r5, #-1804]
	mov	r2, #0
	mov	r1, #1
	str	r0, [sp, #8]
	add	r0, sp, #4
	str	r3, [r4, #4]
	add	r3, r3, #1
	cmn	r3, #1
	str	r3, [r5, #-1804]
	moveq	r3, #0
	streq	r3, [r5, #-1804]
	mov	r3, r2
	bl	FlashProgPages
	ldr	r3, .L1876+16
	ldrh	r0, [r5, r3]
	bl	decrement_vpc_count
.L1872:
	cmp	r6, #0
	ldr	r0, .L1876
	bne	.L1875
.L1873:
	mov	r3, #1
	strb	r3, [r0, #-1990]
	b	.L1867
.L1869:
	mov	r3, #0
	strb	r3, [r2, #-1990]
.L1867:
	add	sp, sp, #40
	ldmfd	sp!, {r4, r5, r6, r7, r8, pc}
.L1877:
	.align	2
.L1876:
	.word	.LANCHOR2
	.word	-1996
	.word	.LANCHOR0
	.word	-3947
	.word	-2000
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
	ldr	r7, .L1882
	movw	r6, #3950
	ldr	r5, .L1882+4
	b	.L1879
.L1881:
	ldr	r3, [r5, #-1856]
	mov	r2, #12
	mla	r3, r2, r4, r3
	ldr	r3, [r3, #4]
	cmp	r3, #0
	bge	.L1880
	mov	r0, r4
	bl	flush_l2p_region
.L1880:
	add	r4, r4, #1
	uxth	r4, r4
.L1879:
	ldrh	r3, [r7, r6]
	cmp	r3, r4
	bhi	.L1881
	mov	r0, #0
	ldmfd	sp!, {r3, r4, r5, r6, r7, pc}
.L1883:
	.align	2
.L1882:
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
	ldr	r6, .L1891
	ldr	r1, .L1891+4
	ldr	r0, .L1891+8
	bl	printk
	movw	r3, #3854
	ldrh	r2, [r6, r3]
	mov	r1, #0
	ldr	r5, .L1891+12
	mov	r2, r2, asl #1
	ldr	r0, [r5, #-920]
	bl	ftl_memset
	b	.L1885
.L1887:
	mov	r0, r4
	add	r1, sp, #4
	mov	r2, #0
	bl	log2phys
	ldr	r0, [sp, #4]
	cmn	r0, #1
	beq	.L1886
	ubfx	r0, r0, #10, #16
	bl	P2V_block_in_plane
	ldr	r3, [r5, #-920]
	mov	r0, r0, asl #1
	ldrh	r2, [r3, r0]
	add	r2, r2, #1
	strh	r2, [r3, r0]	@ movhi
.L1886:
	add	r4, r4, #1
.L1885:
	ldr	r3, [r6, #3972]
	cmp	r4, r3
	bcc	.L1887
	mov	r4, #0
	ldr	r7, .L1891
	movw	r8, #3852
	ldr	r5, .L1891+12
	b	.L1888
.L1890:
	ldr	r3, [r5, #-2020]
	mov	r6, r4, asl #1
	ldrh	r2, [r3, r6]
	ldr	r3, [r5, #-920]
	ldrh	r3, [r3, r6]
	cmp	r2, r3
	beq	.L1889
	movw	r1, #65535
	cmp	r2, r1
	beq	.L1889
	ldr	r0, .L1891+16
	mov	r1, r4
	bl	printk
	ldr	r3, .L1891+20
	ldrh	r3, [r5, r3]
	cmp	r3, r4
	beq	.L1889
	ldr	r3, .L1891+24
	ldrh	r3, [r5, r3]
	cmp	r3, r4
	beq	.L1889
	ldr	r3, .L1891+28
	ldrh	r3, [r5, r3]
	cmp	r3, r4
	beq	.L1889
	ldr	r3, [r5, #-920]
	mov	r0, r4
	ldrh	r2, [r3, r6]
	ldr	r3, [r5, #-2020]
	strh	r2, [r3, r6]	@ movhi
	bl	update_vpc_list
	bl	l2p_flush
	bl	FtlVpcTblFlush
.L1889:
	add	r4, r4, #1
	uxth	r4, r4
.L1888:
	ldrh	r3, [r7, r8]
	cmp	r3, r4
	bhi	.L1890
	ldmfd	sp!, {r2, r3, r4, r5, r6, r7, r8, pc}
.L1892:
	.align	2
.L1891:
	.word	.LANCHOR0
	.word	.LANCHOR3+15
	.word	.LC128
	.word	.LANCHOR2
	.word	.LC129
	.word	-2000
	.word	-1904
	.word	-1952
	.fnend
	.size	FtlVpcCheckAndModify, .-FtlVpcCheckAndModify
	.align	2
	.global	allocate_new_data_superblock
	.type	allocate_new_data_superblock, %function
allocate_new_data_superblock:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	ldr	r3, .L1905
	stmfd	sp!, {r4, r5, r6, lr}
	.save {r4, r5, r6, lr}
	mov	r4, r0
	ldr	r2, [r3, #-2092]
	ldrh	r5, [r0, #0]
	cmp	r2, #0
	bne	.L1894
	movw	r2, #65535
	cmp	r5, r2
	beq	.L1895
	ldr	r2, [r3, #-2020]
	mov	r3, r5, asl #1
	mov	r0, r5
	ldrh	r3, [r2, r3]
	cmp	r3, #0
	beq	.L1896
	bl	INSERT_DATA_LIST
	b	.L1895
.L1896:
	bl	INSERT_FREE_LIST
.L1895:
	mov	r3, #0
	strb	r3, [r4, #8]
	ldr	r3, .L1905
	sub	r2, r3, #1952
	cmp	r4, r2
	beq	.L1897
	ldr	r1, .L1905+4
	movw	r2, #3864
	ldrh	r2, [r1, r2]
	cmp	r2, #1
	beq	.L1897
	ldrb	r1, [r1, #928]	@ zero_extendqisi2
	cmp	r1, #0
	beq	.L1898
.L1897:
	mov	r3, #1
	strb	r3, [r4, #8]
	b	.L1899
.L1898:
	sub	r1, r3, #2000
	cmp	r4, r1
	bne	.L1899
	cmp	r2, #3
	beq	.L1900
	ldr	r2, [r3, #-1716]
	cmp	r2, #1
	bne	.L1901
.L1900:
	mov	r2, #1
	strb	r2, [r3, #-1992]
.L1901:
	ldr	r2, .L1905+4
	ldr	r2, [r2, #3836]
	cmp	r2, #0
	beq	.L1899
	ldr	r3, [r3, #-1784]
	cmp	r3, #29
	ldrls	r3, .L1905
	movls	r2, #1
	strlsb	r2, [r3, #-1992]
.L1899:
	ldr	r3, .L1905+8
	ldr	r6, .L1905
	ldrh	r0, [r6, r3]
	movw	r3, #65535
	cmp	r0, r3
	beq	.L1902
	cmp	r5, r0
	bne	.L1903
	ldr	r2, [r6, #-2020]
	mov	r3, r0, asl #1
	ldrh	r3, [r2, r3]
	cmp	r3, #0
	beq	.L1904
.L1903:
	bl	update_vpc_list
.L1904:
	ldr	r3, .L1905+8
	mvn	r2, #0
	strh	r2, [r6, r3]	@ movhi
.L1902:
	mov	r0, r4
	bl	allocate_data_superblock
	bl	l2p_flush
	mov	r0, #0
	bl	FtlEctTblFlush
	bl	FtlVpcTblFlush
.L1894:
	mov	r0, #0
	ldmfd	sp!, {r4, r5, r6, pc}
.L1906:
	.align	2
.L1905:
	.word	.LANCHOR2
	.word	.LANCHOR0
	.word	-1024
	.fnend
	.size	allocate_new_data_superblock, .-allocate_new_data_superblock
	.align	2
	.type	ftl_do_gc.part.21, %function
ftl_do_gc.part.21:
	.fnstart
	@ args = 0, pretend = 0, frame = 24
	@ frame_needed = 0, uses_anonymous_args = 0
	ldr	r3, .L2004
	movw	ip, #65535
	ldr	r2, .L2004+4
	stmfd	sp!, {r4, r5, r6, r7, r8, r9, sl, fp, lr}
	.save {r4, r5, r6, r7, r8, r9, sl, fp, lr}
	.pad #36
	sub	sp, sp, #36
	mov	sl, r1
	str	r0, [sp, #16]
	ldrh	r0, [r3, r2]
	cmp	r0, ip
	beq	.L1908
	ldr	r1, .L2004+8
	ldrh	r4, [r3, r1]
	cmp	r4, ip
	streqh	r0, [r3, r1]	@ movhi
	mvneq	r1, #0
	streqh	r1, [r3, r2]	@ movhi
.L1908:
	ldr	r1, [sp, #16]
	ldr	r2, [r3, #-1196]
	cmp	r1, #1
	add	r2, r2, #1
	add	r2, r2, r1, asl #7
	str	r2, [r3, #-1196]
	bne	.L1909
	ldr	r3, .L2004+12
	ldr	r1, [r3, #3836]
	cmp	r1, #0
	bne	.L1910
	ldrb	r3, [r3, #928]	@ zero_extendqisi2
	cmp	r3, #0
	beq	.L1909
.L1910:
	ldr	r4, .L2004
	ldr	r3, [r4, #-1784]
	cmp	r3, #29
	bhi	.L1909
	mov	r3, #428
	ldrh	r3, [r4, r3]
	add	r2, r2, r3
	str	r2, [r4, #-1196]
	bl	FtlGcReFreshBadBlk
	ldr	r3, .L2004+16
	movw	r2, #65535
	ldrh	r3, [r4, r3]
	cmp	r3, r2
	bne	.L1909
	ldr	r2, .L2004+8
	ldrh	r2, [r4, r2]
	cmp	r2, r3
	bne	.L1909
	ldr	r3, [r4, #-1196]
	cmp	r3, #1024
	bhi	.L1911
	ldr	r3, .L2004+20
	ldrh	r3, [r4, r3]
	cmp	r3, #63
	bhi	.L1909
.L1911:
	ldr	r3, .L2004
	mov	r2, #428
	ldr	r0, .L2004+20
	mov	r1, #0
	strh	r1, [r3, r2]	@ movhi
	ldrh	ip, [r3, r0]
	ldr	r0, .L2004+24
	ldrh	r0, [r3, r0]
	add	r0, r0, #64
	cmp	ip, r0
	bgt	.L1909
	str	r1, [r3, #-1196]
	ldr	r1, [r3, #-1784]
	cmp	r1, #0
	moveq	r1, #6
	beq	.L2000
	cmp	r1, #5
	bhi	.L1913
	mov	r1, #18
.L2000:
	strh	r1, [r3, r2]	@ movhi
.L1913:
	mov	r0, #32
	movw	r7, #65535
	bl	List_get_gc_head_node
	uxth	r2, r0
	cmp	r2, r7
	beq	.L1914
	ldr	r5, .L2004
	ldr	r6, .L2004+28
	ldrh	r0, [r5, r6]
	cmp	r0, #0
	moveq	r3, #1
	streqh	r3, [r5, r6]	@ movhi
	beq	.L1914
	ldr	r3, .L2004+12
	movw	ip, #3914
	movw	lr, #3844
	ldr	r9, [r5, #-2020]
	mov	r2, r2, asl #1
	ldrh	ip, [r3, ip]
	ldrh	r3, [r3, lr]
	ldrh	r1, [r9, r2]
	mul	r3, r3, ip
	add	r3, r3, #1
	cmp	r1, r3
	bgt	.L1914
	add	fp, r0, #1
	mov	r8, #0
	str	r8, [r5, #-1188]
	uxth	fp, fp
	strh	fp, [r5, r6]	@ movhi
	str	r2, [sp, #8]
	bl	List_get_gc_head_node
	ldr	r2, [sp, #8]
	uxth	r4, r0
	cmp	r4, r7
	beq	.L1914
	ldrh	r2, [r9, r2]
	mov	r7, r4, asl #1
	ldr	r0, .L2004+32
	mov	r1, fp
	ldrh	r3, [r9, r7]
	str	r2, [sp, #0]
	mov	r2, r4
	bl	printk
	ldrh	r3, [r5, r6]
	cmp	r3, #40
	bls	.L1916
	ldr	r3, [r5, #-2020]
	ldrh	r3, [r3, r7]
	cmp	r3, #32
	strhih	r8, [r5, r6]	@ movhi
.L1916:
	ldr	r2, .L2004
	mov	r3, #428
	mov	r1, #6
	strh	r1, [r2, r3]	@ movhi
	b	.L1918
.L1914:
	bl	GetSwlReplaceBlock
	movw	r3, #65535
	cmp	r0, r3
	mov	r4, r0
	bne	.L1918
	ldr	r2, .L2004
	mov	r3, #428
	mov	r1, #0
	strh	r1, [r2, r3]	@ movhi
.L1909:
	ldr	r5, .L2004
	movw	r4, #65535
	ldr	r3, .L2004+16
	ldrh	r2, [r5, r3]
	cmp	r2, r4
	bne	.L1918
	ldr	r3, .L2004+36
	ldrh	r3, [r5, r3]
	cmp	r3, r2
	movne	r4, r2
	bne	.L1918
	ldr	r2, .L2004+8
	ldrh	r7, [r5, r2]
	cmp	r7, r3
	movne	r4, r3
	bne	.L1918
	ldr	r8, .L2004+20
	ldr	r2, [r5, #-1196]
	ldrh	r3, [r5, r8]
	cmp	r3, #23
	movhi	r3, #1024
	movls	r3, #5120
	cmp	r2, r3
	movls	r4, r7
	bls	.L1918
	mov	r2, #428
	mov	r3, #0
	str	r3, [r5, #-1196]
	mov	r6, r5
	strh	r3, [r5, r2]	@ movhi
	bl	GetSwlReplaceBlock
	cmp	r0, r7
	mov	r4, r0
	bne	.L1920
	ldr	r3, .L2004+24
	ldrh	r1, [r5, r8]
	ldrh	r2, [r5, r3]
	cmp	r1, r2
	movcs	r2, #80
	strcsh	r2, [r5, r3]	@ movhi
	bcs	.L1930
	mov	r0, #64
	bl	List_get_gc_head_node
	uxth	r3, r0
	cmp	r3, r4
	beq	.L1930
	ldr	r2, [r5, #-1004]
	cmp	r2, #0
	bne	.L1923
	ldr	r2, .L2004+12
	movw	r1, #3864
	ldrh	r1, [r2, r1]
	cmp	r1, #3
	beq	.L1923
	ldr	r1, [r5, #-1716]
	cmp	r1, #0
	bne	.L1923
	ldr	r1, [r2, #3836]
	cmp	r1, #0
	bne	.L1923
	ldrb	r0, [r2, #928]	@ zero_extendqisi2
	cmp	r0, #0
	beq	.L1924
.L1923:
	ldr	r1, .L2004
	mov	r3, r3, asl #1
	movw	ip, #3844
	movw	lr, #3864
	ldr	r2, [r1, #-2020]
	ldrh	r0, [r2, r3]
	movw	r2, #3914
	ldr	r3, .L2004+12
	ldrh	r2, [r3, r2]
	ldrh	ip, [r3, ip]
	ldrh	r3, [r3, lr]
	mul	ip, ip, r2
	cmp	r3, #3
	moveq	r3, r2, lsr #1
	movne	r3, #0
	add	r3, ip, r3
	cmp	r0, r3
	bgt	.L1926
	mov	r0, #0
	bl	List_get_gc_head_node
	ldr	r2, .L2004+12
	ldr	r3, .L2004
	ldr	r2, [r2, #3972]
	ldr	r1, [r3, #-1848]
	add	r2, r2, r2, asl #1
	cmp	r1, r2, lsr #2
	ldr	r2, .L2004+24
	movhi	r1, #128
	movls	r1, #160
	strh	r1, [r3, r2]	@ movhi
	uxth	r4, r0
	b	.L1928
.L1926:
	ldr	r3, .L2004+24
	mov	r2, #128
	strh	r2, [r1, r3]	@ movhi
	b	.L1930
.L1924:
	ldr	r2, [r5, #-2020]
	mov	r3, r3, asl #1
	ldr	r6, .L2004
	ldr	r5, .L2004+24
	ldrh	r3, [r2, r3]
	cmp	r3, #7
	movhi	r3, #64
	strhih	r3, [r6, r5]	@ movhi
	bhi	.L1930
	bl	List_get_gc_head_node
	mov	r3, #128
	strh	r3, [r6, r5]	@ movhi
	uxth	r4, r0
.L1928:
	movw	r3, #65535
	cmp	r4, r3
	beq	.L1930
.L1920:
	ldr	r1, .L2004
	mov	r0, r4, asl #1
	ldr	r3, .L2004+20
	ldr	ip, [r1, #-2084]
	ldrh	r2, [r1, r3]
	ldr	r3, [r1, #-2020]
	ldrh	r3, [r3, r0]
	ldrh	r0, [ip, r0]
	str	r0, [sp, #0]
	ldr	r0, .L2004+40
	ldrh	r1, [r1, r0]
	ldr	r0, .L2004+44
	str	r1, [sp, #4]
	mov	r1, r4
	bl	printk
.L1930:
	bl	FtlGcReFreshBadBlk
.L1918:
	movw	r0, #65535
	rsb	ip, r0, r4
	rsbs	r1, ip, #0
	ldr	r3, [sp, #16]
	adc	r1, r1, ip
	ldr	r5, .L2004
	cmp	r3, #0
	movne	r2, #0
	andeq	r2, r1, #1
	cmp	r2, #0
	beq	.L1931
	ldr	r3, .L2004+20
	ldrh	r2, [r5, r3]
	cmp	r2, #24
	movhi	r6, #1
	bhi	.L1932
	ldr	r1, .L2004+12
	movw	r3, #3912
	cmp	r2, #16
	ldrh	r6, [r1, r3]
	movhi	r6, r6, lsr #5
	bhi	.L1932
	cmp	r2, #12
	movhi	r6, r6, lsr #4
	bhi	.L1932
	cmp	r2, #8
	movhi	r6, r6, lsr #2
.L1932:
	ldr	r1, .L2004+40
	ldr	r3, .L2004
	ldrh	r0, [r5, r1]
	cmp	r0, r2
	bcs	.L1936
	ldr	r2, .L2004+36
	movw	r0, #65535
	ldrh	r2, [r3, r2]
	cmp	r2, r0
	bne	.L1937
	ldr	r0, .L2004+8
	ldrh	r0, [r3, r0]
	cmp	r0, r2
	bne	.L1937
	mov	r2, #428
	ldrh	r0, [r3, r2]
	cmp	r0, #0
	bne	.L1938
	ldr	r2, .L2004+12
	ldr	ip, [r3, #-1848]
	ldr	r2, [r2, #3972]
	add	r2, r2, r2, asl #1
	cmp	ip, r2, lsr #2
	movcs	r2, #18
	bcs	.L2002
.L1938:
	ldr	r3, .L2004
	ldr	r2, .L2004+48
	ldr	r1, .L2004+40
	ldrh	r2, [r3, r2]
	add	r2, r2, r2, asl #1
	mov	r2, r2, asr #2
.L2002:
	strh	r2, [r3, r1]	@ movhi
	mov	r2, #0
	ldr	r3, .L2004
	str	r2, [r3, #-1188]
	b	.L1941
.L1937:
	ldr	r3, .L2004
	ldr	r2, .L2004+48
	ldr	r1, .L2004+40
	ldrh	r2, [r3, r2]
	add	r2, r2, r2, asl #1
	mov	r2, r2, asr #2
	strh	r2, [r3, r1]	@ movhi
.L1936:
	ldr	r3, .L2004+12
	cmp	sl, #2
	movw	r4, #65535
	movhi	sl, #0
	movls	sl, #1
	ldr	r3, [r3, #3836]
	cmp	r3, #0
	moveq	sl, #0
	cmp	sl, #0
	addne	r6, r6, #1
	uxthne	r6, r6
	b	.L1942
.L1931:
	ldr	r3, .L2004+36
	ldrh	r3, [r5, r3]
	cmp	r3, r0
	bne	.L1943
	ldr	r0, .L2004+8
	ldrh	r0, [r5, r0]
	cmp	r0, r3
	movne	r1, #0
	andeq	r1, r1, #1
	cmp	r1, #0
	beq	.L1943
	ldr	r1, .L2004+16
	ldrh	r4, [r5, r1]
	cmp	r4, r3
	movne	r4, r3
	bne	.L1943
	ldr	r3, .L2004+20
	mov	r6, #428
	str	r2, [r5, #-1188]
	ldrh	r7, [r5, r3]
	add	r3, r3, #804
	ldrh	r2, [r5, r3]
	cmp	r2, r7
	bcs	.L1944
	ldrh	r2, [r5, r6]
	cmp	r2, #0
	bne	.L1945
	ldr	r2, .L2004+12
	ldr	r1, [r5, #-1848]
	ldr	r2, [r2, #3972]
	add	r2, r2, r2, asl #1
	cmp	r1, r2, lsr #2
	movcs	r2, #18
	strcsh	r2, [r5, r3]	@ movhi
	bcs	.L1947
.L1945:
	ldr	r3, .L2004
	ldr	r2, .L2004+48
	ldr	r1, .L2004+40
	ldrh	r2, [r3, r2]
	add	r2, r2, r2, asl #1
	mov	r2, r2, asr #2
	strh	r2, [r3, r1]	@ movhi
.L1947:
	bl	FtlReadRefresh
	mov	r3, #428
	ldr	r2, .L2004
	b	.L2003
.L1944:
	ldrh	r0, [r5, r6]
	cmp	r0, #0
	bne	.L1943
	ldr	r2, .L2004+48
	ldrh	r4, [r5, r2]
	add	r2, r4, r4, asl #1
	mov	r2, r2, asr #2
	strh	r2, [r5, r3]	@ movhi
	bl	List_get_gc_head_node
	ldr	r3, [r5, #-2020]
	ldr	r1, .L2004+12
	movw	r2, #3914
	ldrh	r2, [r1, r2]
	uxth	r0, r0
	mov	r0, r0, asl #1
	ldrh	r3, [r3, r0]
	movw	r0, #3844
	ldrh	r1, [r1, r0]
	mul	r2, r1, r2
	add	r2, r2, r2, lsr #31
	cmp	r3, r2, asr #1
	ble	.L1948
	sub	r4, r4, #1
	cmp	r7, r4
	blt	.L1948
	bl	FtlReadRefresh
	ldrh	r0, [r5, r6]
	b	.L1941
.L1948:
	cmp	r3, #0
	movwne	r4, #65535
	bne	.L1943
	movw	r0, #65535
	bl	decrement_vpc_count
	ldr	r3, .L2004+20
	ldr	r2, .L2004
	ldrh	r0, [r2, r3]
	add	r0, r0, #1
	b	.L1941
.L1943:
	ldr	r3, .L2004+12
	ldr	r6, [r3, #3836]
	cmp	r6, #0
	movne	r6, #2
	moveq	r6, #1
.L1942:
	ldr	r3, .L2004
	movw	r0, #65535
	ldr	r2, .L2004+16
	ldrh	r1, [r3, r2]
	cmp	r1, r0
	bne	.L1949
	cmp	r4, r1
	strneh	r4, [r3, r2]	@ movhi
	bne	.L1951
	ldr	r2, .L2004+8
	ldrh	r1, [r3, r2]
	cmp	r1, r4
	beq	.L1951
	ldr	r0, [r3, #-2020]
	mov	r1, r1, asl #1
	ldrh	r1, [r0, r1]
	cmp	r1, #0
	mvneq	r1, #0
	streqh	r1, [r3, r2]	@ movhi
	ldr	r3, .L2004
	ldr	r1, .L2004+16
	ldrh	r0, [r3, r2]
	strh	r0, [r3, r1]	@ movhi
	mvn	r1, #0
	strh	r1, [r3, r2]	@ movhi
.L1951:
	ldr	r5, .L2004
	mov	r3, #0
	ldr	r7, .L2004+16
	strb	r3, [r5, #-1756]
	movw	r3, #65535
	ldrh	r0, [r5, r7]
	cmp	r0, r3
	beq	.L1949
	bl	IsBlkInGcList
	cmp	r0, #0
	mvnne	r3, #0
	strneh	r3, [r5, r7]	@ movhi
	ldr	r3, .L2004+12
	ldr	r7, .L2004
	ldrb	r3, [r3, #928]	@ zero_extendqisi2
	cmp	r3, #0
	beq	.L1954
	ldr	r3, .L2004+16
	ldrh	r0, [r7, r3]
	bl	ftl_get_blk_mode
	strb	r0, [r7, #-1756]
.L1954:
	ldr	r8, .L2004+16
	movw	r3, #65535
	ldr	r5, .L2004
	ldrh	r2, [r7, r8]
	cmp	r2, r3
	beq	.L1949
	ldr	r0, .L2004+52
	bl	make_superblock
	ldr	r1, .L2004+56
	movw	r2, #430
	mov	r3, #0
	strh	r3, [r5, r2]	@ movhi
	strh	r3, [r5, r1]	@ movhi
	strb	r3, [r5, #-1758]
	ldrh	r3, [r5, r8]
	ldr	r2, [r5, #-2020]
	mov	r3, r3, asl #1
	ldrh	r2, [r2, r3]
	mov	r3, #432
	strh	r2, [r5, r3]	@ movhi
.L1949:
	ldr	r3, .L2004
	ldr	r0, .L2004+16
	ldr	r2, .L2004+60
	ldrh	r1, [r3, r0]
	ldrh	r2, [r3, r2]
	cmp	r2, r1
	mov	r2, r3
	beq	.L1955
	ldr	ip, .L2004+64
	ldrh	r3, [r3, ip]
	cmp	r3, r1
	movne	fp, r4
	bne	.L1999
.L1956:
.L1955:
	mvn	r3, #0
	strh	r3, [r2, r0]	@ movhi
	mov	r3, #428
.L2003:
	ldrh	r0, [r2, r3]
	b	.L1941
.L1999:
	ldr	r4, .L2004
	movw	sl, #65535
	ldr	r9, .L2004+16
	ldrh	r3, [r4, r9]
	cmp	r3, sl
	bne	.L1958
	ldr	r5, .L2004+28
	mov	r3, #0
	str	r3, [r4, #-1188]
.L1998:
	ldrh	r8, [r4, r5]
	mov	r0, r8
	bl	List_get_gc_head_node
	movw	r1, #65535
	uxth	r7, r0
	strh	r7, [r4, r9]	@ movhi
	cmp	r7, r1
	bne	.L1960
	ldr	r3, .L2004+28
	mov	r1, #0
	ldr	r2, .L2004
	mov	r0, #8
	strh	r1, [r2, r3]	@ movhi
	b	.L1941
.L1960:
	mov	r0, r7
	bl	IsBlkInGcList
	cmp	r0, #0
	add	r0, r8, #1
	strneh	r0, [r4, r5]	@ movhi
	bne	.L1998
	ldr	ip, .L2004+12
	movw	r8, #3912
	ldr	r2, [r4, #-2020]
	mov	r3, r7, asl #1
	uxth	r0, r0
	strh	r0, [r4, r5]	@ movhi
	ldrh	sl, [ip, r8]
	movw	r8, #3844
	ldrh	ip, [ip, r8]
	ldrh	r1, [r2, r3]
	mul	sl, ip, sl
	add	ip, sl, sl, lsr #31
	cmp	r1, ip, asr #1
	bgt	.L1963
	cmp	r0, #48
	bls	.L1964
	cmp	r1, #8
	bls	.L1964
	ldr	r1, .L2004+68
	ldrh	r1, [r4, r1]
	cmp	r1, #35
	bhi	.L1964
.L1963:
	mov	r1, #0
	strh	r1, [r4, r5]	@ movhi
.L1964:
	ldrh	r3, [r2, r3]
	movw	r2, #65535
	cmp	r3, sl
	cmpge	fp, r2
	ldr	sl, .L2004
	movne	r8, #0
	moveq	r8, #1
	bne	.L1965
	ldr	r3, .L2004+16
	mvn	r2, #0
	strh	r2, [sl, r3]	@ movhi
	add	r3, r3, #572
	mov	r2, #0
	strh	r2, [sl, r3]	@ movhi
	mov	r3, #428
	ldrh	r0, [sl, r3]
	b	.L1941
.L1965:
	cmp	r3, #0
	bne	.L1966
	movw	r0, #65535
	bl	decrement_vpc_count
	ldrh	r3, [r4, r5]
	add	r3, r3, #1
	strh	r3, [r4, r5]	@ movhi
	b	.L1998
.L1966:
	ldr	r3, .L2004+12
	strb	r8, [sl, #-1756]
	ldrb	r3, [r3, #928]	@ zero_extendqisi2
	cmp	r3, #0
	beq	.L1967
	mov	r0, r7
	bl	ftl_get_blk_mode
	strb	r0, [sl, #-1756]
.L1967:
	ldr	r4, .L2004
	ldr	r0, .L2004+52
	bl	make_superblock
	movw	r1, #430
	mov	r3, #0
	strh	r3, [r4, r1]	@ movhi
	ldr	r1, .L2004+16
	ldr	r0, [r4, #-2020]
	ldrh	r1, [r4, r1]
	mov	r1, r1, asl #1
	ldrh	r0, [r0, r1]
	mov	r1, #432
	strb	r3, [r4, #-1758]
	strh	r0, [r4, r1]	@ movhi
	ldr	r1, .L2004+56
	strh	r3, [r4, r1]	@ movhi
.L1958:
	ldr	sl, [sp, #16]
	cmp	sl, #1
	bne	.L1968
	bl	FtlReadRefresh
.L1968:
	ldr	r3, .L2004
	mov	r2, #1
	movw	r1, #3912
	str	r2, [r3, #-1000]
	ldr	r2, .L2004+12
	ldrh	r9, [r2, r1]
	ldrb	r1, [r2, #928]	@ zero_extendqisi2
	cmp	r1, #0
	beq	.L1969
	ldrb	r1, [r3, #-1756]	@ zero_extendqisi2
	cmp	r1, #1
	movweq	r1, #3914
	ldreqh	r9, [r2, r1]
.L1969:
	ldr	r2, .L2004+56
	mov	r8, #0
	str	fp, [sp, #20]
	mov	fp, r9
	ldr	r4, .L2004
	ldrh	r3, [r3, r2]
	add	r2, r3, r6
	cmp	r2, r9
	rsbgt	r6, r3, r9
	uxthgt	r6, r6
	b	.L1971
.L1973:
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
.L1979:
	cmp	r3, r5
	bne	.L1973
	ldr	r0, [r4, #-1168]
	mov	r1, r7
	ldrb	r2, [r4, #-1756]	@ zero_extendqisi2
	mov	r5, #0
	bl	FlashReadPages
	mov	r9, r5
	mov	sl, r6
	b	.L1974
.L1977:
	ldr	r3, [r4, #-1168]
	add	r2, r3, r5
	ldr	r3, [r3, r5]
	ldr	r6, [r2, #12]
	cmn	r3, #1
	beq	.L1975
	ldrh	r3, [r6, #0]
	movw	r1, #61589
	cmp	r3, r1
	bne	.L1975
	add	r1, sp, #28
	mov	r2, #0
	ldr	r0, [r6, #8]
	bl	log2phys
	ldr	r1, [r4, #-1168]
	ldr	r2, [sp, #28]
	add	r1, r1, r5
	bic	r2, r2, #-2147483648
	ldr	r3, [r1, #4]
	cmp	r2, r3
	bne	.L1975
	movw	r3, #430
	ldr	r0, [r4, #-1184]
	ldrh	r2, [r4, r3]
	ldr	r1, [r1, #16]
	add	r2, r2, #1
	strh	r2, [r4, r3]	@ movhi
	ldr	r2, [r4, #-972]
	mov	r3, #36
	mla	r2, r3, r0, r2
	str	r1, [r2, #16]
	str	r2, [sp, #8]
	str	r3, [sp, #12]
	bl	Ftl_get_new_temp_ppa
	ldr	r2, [sp, #8]
	ldr	r1, [r4, #-1184]
	str	r0, [r2, #4]
	ldr	r3, [sp, #12]
	ldr	r2, [r4, #-972]
	mla	r3, r3, r1, r2
	ldr	r2, [r4, #-1168]
	add	r2, r2, r5
	ldr	r1, [r2, #8]
	str	r1, [r3, #8]
	mov	r1, #1
	ldr	r2, [r2, #12]
	str	r2, [r3, #12]
	ldr	r3, [sp, #28]
	str	r3, [r6, #12]
	ldr	r3, .L2004+36
	ldrh	r3, [r4, r3]
	strh	r3, [r6, #2]	@ movhi
	ldr	r3, [r4, #-1804]
	ldr	r0, [r4, #-1168]
	str	r3, [r6, #4]
	add	r0, r0, r5
	ldr	r3, [r4, #-1184]
	add	r3, r3, #1
	str	r3, [r4, #-1184]
	bl	FtlGcBufAlloc
	ldr	r3, .L2004+12
	ldrb	r3, [r3, #928]	@ zero_extendqisi2
	cmp	r3, #0
	bne	.L1976
	ldrb	r3, [r4, #-1897]	@ zero_extendqisi2
	ldr	r2, [r4, #-1184]
	cmp	r2, r3
	beq	.L1976
	ldr	r3, .L2004+72
	ldrh	r3, [r4, r3]
	cmp	r3, #0
	bne	.L1975
.L1976:
	bl	Ftl_gc_temp_data_write_back
	cmp	r0, #0
	ldrne	r3, .L2004
	movne	r2, #0
	strne	r2, [r3, #-1000]
	movne	r2, #428
	bne	.L2003
.L1975:
	add	r9, r9, #1
	add	r5, r5, #36
	uxth	r9, r9
.L1974:
	cmp	r9, r7
	bne	.L1977
	add	r8, r8, #1
	mov	r6, sl
.L1971:
	uxth	r3, r8
	ldr	r7, .L2004
	cmp	r3, r6
	ldr	r3, .L2004+56
	bcs	.L1978
	ldr	r1, .L2004+12
	movw	r2, #3844
	ldrh	ip, [r4, r3]
	mov	r7, #0
	ldr	lr, [r4, #-1168]
	mov	r3, r7
	ldrh	r5, [r1, r2]
	add	ip, ip, r8
	ldr	r2, .L2004+76
	mov	r0, #36
	b	.L1979
.L1978:
	ldrh	r2, [r7, r3]
	mov	r9, fp
	ldr	fp, [sp, #20]
	add	r6, r6, r2
	uxth	r6, r6
	strh	r6, [r7, r3]	@ movhi
	cmp	r6, r9
	bcc	.L1980
	ldr	r3, [r7, #-1184]
	cmp	r3, #0
	beq	.L1981
	bl	Ftl_gc_temp_data_write_back
	cmp	r0, #0
	movne	r3, #0
	strne	r3, [r7, #-1000]
	movne	r3, #428
	ldrneh	r0, [r7, r3]
	bne	.L1941
.L1981:
	ldr	r3, .L2004
	movw	r2, #430
	ldrh	ip, [r3, r2]
	cmp	ip, #0
	bne	.L1982
	ldr	r2, .L2004+16
	ldr	r0, [r3, #-2020]
	ldrh	r1, [r3, r2]
	mov	r1, r1, asl #1
	ldrh	r4, [r0, r1]
	cmp	r4, #0
	beq	.L1982
	strh	ip, [r0, r1]	@ movhi
	ldrh	r0, [r3, r2]
	bl	update_vpc_list
	bl	FtlCacheWriteBack
	bl	l2p_flush
	bl	FtlVpcTblFlush
.L1982:
	ldr	r3, .L2004+16
	mvn	r1, #0
	ldr	r2, .L2004
	strh	r1, [r2, r3]	@ movhi
.L1980:
	ldr	r3, .L2004
	ldr	r2, .L2004+20
	ldrh	r2, [r3, r2]
	cmp	r2, #2
	ldrls	r2, .L2004+12
	movwls	r3, #3912
	ldrlsh	r6, [r2, r3]
	bls	.L1999
.L1983:
	mov	r1, #0
	str	r1, [r3, #-1000]
	mov	r1, #428
	ldrh	r0, [r3, r1]
	cmp	r0, #0
	addeq	r0, r2, #1
.L1941:
	add	sp, sp, #36
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, sl, fp, pc}
.L2005:
	.align	2
.L2004:
	.word	.LANCHOR2
	.word	-1144
	.word	-1146
	.word	.LANCHOR0
	.word	-1764
	.word	-2004
	.word	-1198
	.word	-1192
	.word	.LC130
	.word	-1904
	.word	-1200
	.word	.LC131
	.word	-1780
	.word	.LANCHOR2-1764
	.word	-1762
	.word	-2000
	.word	-1952
	.word	-1160
	.word	-1900
	.word	.LANCHOR2-1750
	.fnend
	.size	ftl_do_gc.part.21, .-ftl_do_gc.part.21
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
	ldr	r3, .L2013
	mov	r5, r1
	ldr	r0, [r3, #-2092]
	cmp	r0, #0
	movne	r0, #0
	ldmnefd	sp!, {r3, r4, r5, pc}
	ldr	r2, [r3, #-1000]
	cmp	r2, #0
	ldmnefd	sp!, {r3, r4, r5, pc}
	ldr	r1, .L2013+4
	ldrh	r1, [r3, r1]
	cmp	r1, #47
	movls	r0, r2
	ldmlsfd	sp!, {r3, r4, r5, pc}
	ldr	r1, .L2013+8
	movw	r2, #3000
	ldrh	r1, [r1, r2]
	movw	r2, #65535
	cmp	r1, r2
	beq	.L2008
	ldr	r1, .L2013+12
	ldrh	r3, [r3, r1]
	cmp	r3, r2
	beq	.L2008
	mov	r0, #1
	bl	FtlGcFreeTempBlock
	cmp	r0, #0
	bne	.L2012
.L2008:
	mov	r0, r4
	mov	r1, r5
	ldmfd	sp!, {r3, r4, r5, lr}
	b	ftl_do_gc.part.21
.L2012:
	mov	r0, #1
	ldmfd	sp!, {r3, r4, r5, pc}
.L2014:
	.align	2
.L2013:
	.word	.LANCHOR2
	.word	-2012
	.word	.LANCHOR1
	.word	-1904
	.fnend
	.size	ftl_do_gc, .-ftl_do_gc
	.align	2
	.global	FtlCacheWriteBack
	.type	FtlCacheWriteBack, %function
FtlCacheWriteBack:
	.fnstart
	@ args = 0, pretend = 0, frame = 8
	@ frame_needed = 0, uses_anonymous_args = 0
	ldr	r3, .L2037
	stmfd	sp!, {r0, r1, r4, r5, r6, r7, r8, r9, sl, lr}
	.save {r0, r1, r4, r5, r6, r7, r8, r9, sl, lr}
	ldr	r4, [r3, #436]
	ldr	r3, [r3, #-2092]
	cmp	r3, #0
	bne	.L2016
	ldr	r3, .L2037+4
	ldr	r1, [r3, #3964]
	cmp	r1, #0
	beq	.L2016
	ldrb	r8, [r3, #928]	@ zero_extendqisi2
	cmp	r8, #0
	beq	.L2017
	ldrb	r8, [r4, #8]	@ zero_extendqisi2
	sub	lr, r8, #1
	rsbs	r8, lr, #0
	adc	r8, r8, lr
.L2017:
	ldr	r0, [r3, #3968]
	mov	r2, r8
	ldrb	r3, [r4, #9]	@ zero_extendqisi2
	mov	r5, #0
	bl	FlashProgPages
	mov	r7, r5
	ldr	r6, .L2037+4
	ldr	r9, .L2037
	b	.L2018
.L2023:
	ldr	r2, [r6, #3968]
	add	r3, r2, r5
	ldr	r2, [r2, r5]
	cmn	r2, #1
	beq	.L2036
	ldr	r2, [r3, #4]
	cmp	r8, #0
	ldr	r0, [r3, #16]
	add	r1, sp, #4
	str	r2, [sp, #4]
	orrne	r2, r2, #-2147483648
	strne	r2, [sp, #4]
	mov	r2, #1
	bl	log2phys
	ldr	r3, [r6, #3968]
	add	r3, r3, r5
	ldr	r3, [r3, #12]
	ldr	r0, [r3, #12]
	cmn	r0, #1
	beq	.L2021
	ubfx	r0, r0, #10, #16
	bl	P2V_block_in_plane
	ldr	r2, [r9, #-2020]
	mov	r3, r0, asl #1
	mov	sl, r0
	ldrh	r2, [r2, r3]
	cmp	r2, #0
	bne	.L2022
	ldr	r0, .L2037+8
	mov	r1, sl
	bl	printk
.L2022:
	mov	r0, sl
	bl	decrement_vpc_count
.L2021:
	add	r7, r7, #1
	add	r5, r5, #36
.L2018:
	ldr	r3, [r6, #3964]
	cmp	r7, r3
	bcc	.L2023
	b	.L2024
.L2032:
	ldr	r3, [r6, #3968]
	mvn	r2, #0
	movw	sl, #3912
	str	r2, [r3, r5]
	b	.L2025
.L2028:
	ldr	r0, [r3, #4]
	ubfx	r0, r0, #10, #16
	bl	P2V_block_in_plane
	ldrh	r3, [r4, #0]
	cmp	r3, r0
	bne	.L2026
	ldr	r2, [r7, #-2020]
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
.L2026:
	ldrh	r3, [r4, #4]
	cmp	r3, #0
	bne	.L2027
	mov	r0, r4
	bl	allocate_new_data_superblock
.L2027:
	ldr	r3, [r7, #-1616]
	add	r3, r3, #1
	str	r3, [r7, #-1616]
	ldr	r3, [r6, #3968]
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
	ldr	r0, [r6, #3968]
	add	r0, r0, r5
	str	r3, [r0, #4]
	ldrb	r3, [r4, #9]	@ zero_extendqisi2
	bl	FlashProgPages
	ldr	r3, [r7, #-2092]
	cmp	r3, #0
	bne	.L2016
.L2025:
	ldr	r2, [r6, #3968]
	add	r3, r2, r5
	ldr	r2, [r2, r5]
	cmn	r2, #1
	beq	.L2028
	ldr	r2, [r3, #4]
	cmp	r8, #0
	ldr	r0, [r3, #16]
	add	r1, sp, #4
	str	r2, [sp, #4]
	orrne	r2, r2, #-2147483648
	strne	r2, [sp, #4]
	mov	r2, #1
	bl	log2phys
	ldr	r3, [r6, #3968]
	add	r3, r3, r5
	ldr	r3, [r3, #12]
	ldr	r0, [r3, #12]
	cmn	r0, #1
	beq	.L2030
	ubfx	r0, r0, #10, #16
	bl	P2V_block_in_plane
	ldr	r2, [r7, #-2020]
	mov	r3, r0, asl #1
	mov	sl, r0
	ldrh	r2, [r2, r3]
	cmp	r2, #0
	bne	.L2031
	ldr	r0, .L2037+8
	mov	r1, sl
	bl	printk
.L2031:
	mov	r0, sl
	bl	decrement_vpc_count
.L2030:
	add	r9, r9, #1
	add	r5, r5, #36
	b	.L2019
.L2036:
	ldr	r6, .L2037+4
	mov	r5, #0
	ldr	r7, .L2037
	mov	r9, r5
.L2019:
	ldr	r3, [r6, #3964]
	cmp	r9, r3
	bcc	.L2032
	movw	r4, #16386
	ldr	r6, .L2037
	ldr	r5, .L2037+12
	b	.L2033
.L2034:
	mov	r0, #1
	mov	r1, r0
	bl	ftl_do_gc
	subs	r4, r4, #1
	beq	.L2024
.L2033:
	ldrh	r3, [r6, r5]
	cmp	r3, #0
	bne	.L2034
.L2024:
	ldr	r3, .L2037+4
	mov	r2, #0
	str	r2, [r3, #3964]
.L2016:
	mov	r0, #0
	ldmfd	sp!, {r2, r3, r4, r5, r6, r7, r8, r9, sl, pc}
.L2038:
	.align	2
.L2037:
	.word	.LANCHOR2
	.word	.LANCHOR0
	.word	.LC132
	.word	-1142
	.fnend
	.size	FtlCacheWriteBack, .-FtlCacheWriteBack
	.align	2
	.global	FtlGcFreeTempBlock
	.type	FtlGcFreeTempBlock, %function
FtlGcFreeTempBlock:
	.fnstart
	@ args = 0, pretend = 0, frame = 8
	@ frame_needed = 0, uses_anonymous_args = 0
	stmfd	sp!, {r0, r1, r2, r4, r5, r6, r7, r8, sl, lr}
	.save {r0, r1, r2, r4, r5, r6, r7, r8, sl, lr}
	movw	r3, #3912
	ldr	r2, .L2062
	ldrh	r1, [r2, r3]
	ldr	r3, .L2062+4
	ldr	r2, [r3, #-2092]
	cmp	r2, #0
	bne	.L2060
	ldr	ip, .L2062+8
	ldrh	r5, [r3, ip]
	movw	ip, #65535
	cmp	r5, ip
	beq	.L2041
	cmp	r0, #0
	beq	.L2042
	ldr	lr, .L2062+12
	movw	r0, #3000
	ldrh	r4, [lr, r0]
	cmp	r4, ip
	movne	r1, #2
	bne	.L2042
	strh	r2, [lr, r0]	@ movhi
	ldr	r2, .L2062+16
	ldrh	r3, [r3, r2]
	cmp	r3, #17
	movhi	r1, #2
.L2042:
	ldr	r4, .L2062+4
	sub	r0, r4, #1904
	bl	FtlGcScanTempBlk
	cmn	r0, #1
	str	r0, [sp, #4]
	beq	.L2043
	ldr	r3, [r4, #-2084]
	mov	r5, r5, asl #1
	ldrh	r2, [r3, r5]
	cmp	r2, #4
	bls	.L2044
	sub	r2, r2, #5
	mov	r0, #1
	strh	r2, [r3, r5]	@ movhi
	bl	FtlEctTblFlush
.L2044:
	ldr	r3, .L2062+4
	ldr	r2, [r3, #-1016]
	cmp	r2, #0
	bne	.L2045
	ldr	r2, [r3, #-1616]
	ldr	r0, [sp, #4]
	add	r2, r2, #1
	str	r2, [r3, #-1616]
	ubfx	r0, r0, #10, #16
	bl	FtlBbmMapBadBlock
	bl	FtlBbmTblFlush
.L2045:
	ldr	r3, .L2062+4
	mov	r2, #0
	str	r2, [r3, #-1016]
	b	.L2058
.L2043:
	ldr	r2, .L2062+12
	movw	r3, #3000
	ldrh	r2, [r2, r3]
	movw	r3, #65535
	cmp	r2, r3
	bne	.L2058
.L2041:
	ldr	r6, .L2062+4
	movw	r3, #65535
	ldr	r5, .L2062+8
	mov	r4, #0
	str	r4, [r6, #-1016]
	ldrh	r2, [r6, r5]
	cmp	r2, r3
	moveq	r0, r4
	beq	.L2040
	bl	FtlCacheWriteBack
	ldr	ip, .L2062
	movw	r0, #3912
	ldrb	r1, [r6, #-1897]	@ zero_extendqisi2
	ldrh	r3, [r6, r5]
	ldrh	r0, [ip, r0]
	ldr	r2, [r6, #-2020]
	ldr	sl, .L2062+20
	mov	r3, r3, asl #1
	mul	r1, r0, r1
	strh	r1, [r2, r3]	@ movhi
	ldr	r3, [r6, #-1820]
	ldrh	r2, [r6, sl]
	add	r3, r2, r3
	str	r3, [r6, #-1820]
	b	.L2046
.L2049:
	mov	r7, #12
	ldr	r8, [r6, #-1152]
	mul	r7, r7, r4
	add	r1, sp, #4
	mov	r2, #0
	add	r5, r8, r7
	ldr	r0, [r5, #8]
	bl	log2phys
	ldr	r0, [sp, #4]
	ldr	r3, [r8, r7]
	cmp	r0, r3
	bne	.L2047
	ubfx	r0, r0, #10, #16
	bl	P2V_block_in_plane
	add	r1, r5, #4
	mov	r2, #1
	mov	r7, r0
	ldr	r0, [r5, #8]
	bl	log2phys
	mov	r0, r7
	b	.L2061
.L2047:
	ldr	r3, [r5, #4]
	cmp	r0, r3
	beq	.L2048
	ldr	r3, .L2062+8
	ldrh	r0, [r6, r3]
.L2061:
	bl	decrement_vpc_count
.L2048:
	add	r4, r4, #1
	uxth	r4, r4
.L2046:
	ldrh	r3, [r6, sl]
	ldr	r5, .L2062+4
	cmp	r3, r4
	bhi	.L2049
	movw	r0, #65535
	bl	decrement_vpc_count
	ldr	r3, .L2062
	ldrb	r3, [r3, #928]	@ zero_extendqisi2
	cmp	r3, #0
	beq	.L2050
	ldr	r3, .L2062+8
	ldr	r0, .L2062+24
	ldrh	r1, [r5, r3]
	bl	printk
.L2050:
	ldr	r3, .L2062+8
	ldr	r1, [r5, #-2020]
	ldrh	r0, [r5, r3]
	mov	r5, r3
	mov	r2, r0, asl #1
	ldrh	r2, [r1, r2]
	cmp	r2, #0
	beq	.L2051
	bl	INSERT_DATA_LIST
	b	.L2052
.L2051:
	bl	INSERT_FREE_LIST
.L2052:
	ldr	r4, .L2062+4
	mvn	r3, #0
	ldr	r2, .L2062+20
	strh	r3, [r4, r5]	@ movhi
	mov	r3, #0
	strh	r3, [r4, r2]	@ movhi
	sub	r2, r2, #12
	strh	r3, [r4, r2]	@ movhi
	bl	l2p_flush
	bl	FtlVpcTblFlush
	ldr	r3, .L2062
	mov	r0, r4
	ldr	r1, .L2062+16
	ldr	r2, .L2062+28
	ldr	r3, [r3, #3836]
	cmp	r3, #0
	beq	.L2053
	ldr	r3, [r4, #-1784]
	cmp	r3, #29
	bhi	.L2053
	ldrh	r3, [r4, r2]
	ldrh	r2, [r4, r1]
	mvn	r1, #0
	cmp	r2, r3
	movcc	r3, r3, asl #1
	ldrcc	r2, .L2062+32
	strcch	r3, [r4, r2]	@ movhi
	ldr	r3, .L2062+36
	ldr	r2, .L2062+4
	strh	r1, [r2, r3]	@ movhi
	b	.L2060
.L2053:
	ldrh	r2, [r0, r2]
	ldrh	r1, [r0, r1]
	ldr	r3, .L2062+4
	add	r0, r2, r2, asl #1
	cmp	r1, r0, lsr #2
	ble	.L2060
	ldr	r1, .L2062+36
	mvn	r0, #0
	strh	r0, [r3, r1]	@ movhi
	ldr	r1, .L2062
	ldrb	r0, [r1, #928]	@ zero_extendqisi2
	ldr	r1, .L2062+32
	cmp	r0, #0
	subne	r2, r2, #2
	moveq	r2, #20
	strneh	r2, [r3, r1]	@ movhi
	streqh	r2, [r3, r1]	@ movhi
	beq	.L2040
	b	.L2060
.L2058:
	mov	r0, #1
	b	.L2040
.L2060:
	mov	r0, #0
.L2040:
	ldmfd	sp!, {r1, r2, r3, r4, r5, r6, r7, r8, sl, pc}
.L2063:
	.align	2
.L2062:
	.word	.LANCHOR0
	.word	.LANCHOR2
	.word	-1904
	.word	.LANCHOR1
	.word	-2004
	.word	-1148
	.word	.LC133
	.word	-1780
	.word	-1200
	.word	-1764
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
	ldr	r3, .L2073
	ldrb	r3, [r3, #928]	@ zero_extendqisi2
	cmp	r3, #0
	ldr	r3, .L2073+4
	beq	.L2065
	ldr	r2, [r3, #-1184]
	tst	r2, #1
	beq	.L2065
	ldr	r2, .L2073+8
	ldrh	r2, [r3, r2]
	cmp	r2, #0
	bne	.L2070
.L2065:
	mov	r2, #0
	ldr	r0, [r3, #-972]
	ldr	r1, [r3, #-1184]
	mov	r3, r2
	bl	FlashProgPages
	mov	r4, #0
	ldr	r5, .L2073+4
	mov	r6, #36
	b	.L2067
.L2069:
	mul	r3, r6, r4
	ldr	r1, [r5, #-972]
	add	r2, r1, r3
	ldr	r1, [r1, r3]
	cmn	r1, #1
	bne	.L2068
	ldr	r2, .L2073+12
	mov	lr, #0
	ldr	ip, [r7, #-2020]
	ldrh	r0, [r7, r2]
	mov	r0, r0, asl #1
	strh	lr, [ip, r0]	@ movhi
	strh	r1, [r7, r2]	@ movhi
	ldr	r2, [r7, #-1616]
	add	r2, r2, #1
	str	r2, [r7, #-1616]
	ldr	r2, [r7, #-972]
	add	r3, r2, r3
	ldr	r0, [r3, #4]
	ubfx	r0, r0, #10, #16
	bl	FtlBbmMapBadBlock
	bl	FtlBbmTblFlush
	bl	FtlGcPageVarInit
	b	.L2072
.L2068:
	ldr	r3, [r2, #12]
	add	r4, r4, #1
	ldr	r1, [r2, #4]
	uxth	r4, r4
	ldr	r0, [r3, #12]
	ldr	r2, [r3, #8]
	bl	FtlGcUpdatePage
.L2067:
	ldr	r1, [r5, #-1184]
	ldr	r7, .L2073+4
	cmp	r4, r1
	bcc	.L2069
	ldr	r0, [r7, #-972]
	bl	FtlGcBufFree
	ldr	r3, .L2073+8
	mov	r0, #0
	str	r0, [r7, #-1184]
	ldrh	r3, [r7, r3]
	cmp	r3, r0
	ldmnefd	sp!, {r3, r4, r5, r6, r7, pc}
	mov	r0, #1
	bl	FtlGcFreeTempBlock
.L2072:
	mov	r0, #1
	ldmfd	sp!, {r3, r4, r5, r6, r7, pc}
.L2070:
	mov	r0, #0
	ldmfd	sp!, {r3, r4, r5, r6, r7, pc}
.L2074:
	.align	2
.L2073:
	.word	.LANCHOR0
	.word	.LANCHOR2
	.word	-1900
	.word	-1904
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
	movw	r5, #3912
	ldr	r6, .L2077
	ldr	r4, .L2077+4
	ldrh	r1, [r6, r5]
	sub	r0, r4, #1904
	bl	FtlGcScanTempBlk
	ldr	r3, .L2077+8
	ldrh	r2, [r4, r3]
	ldrh	r3, [r6, r5]
	cmp	r2, r3
	ldmccfd	sp!, {r4, r5, r6, pc}
	ldr	r0, .L2077+12
	bl	FtlMapBlkWriteDumpData
	mov	r0, #0
	bl	FtlGcFreeTempBlock
	mov	r3, #0
	str	r3, [r4, #-1016]
	ldmfd	sp!, {r4, r5, r6, pc}
.L2078:
	.align	2
.L2077:
	.word	.LANCHOR0
	.word	.LANCHOR2
	.word	-1902
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
	ldr	r4, .L2080
	sub	r6, r4, #2000
	str	r5, [r4, #296]
	sub	r4, r4, #1952
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
.L2081:
	.align	2
.L2080:
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
	stmfd	sp!, {r3, r4, r5, r6, r7, lr}
	.save {r3, r4, r5, r6, r7, lr}
	mov	r3, #292
	ldr	r6, .L2093
	mov	r2, #0
	ldr	r4, .L2093+4
	mvn	r5, #0
	ldr	r0, [r6, #3848]
	strh	r2, [r4, r3]	@ movhi
	movw	r3, #290
	strh	r5, [r4, r3]	@ movhi
	uxth	r0, r0
	bl	FtlFreeSysBlkQueueInit
	bl	FtlScanSysBlk
	ldr	r3, .L2093+8
	ldrh	r2, [r4, r3]
	movw	r3, #65535
	cmp	r2, r3
	beq	.L2083
	bl	FtlLoadSysInfo
	subs	r7, r0, #0
	bne	.L2083
	bl	FtlLoadMapInfo
	bl	FtlLoadVonderInfo
	bl	Ftl_load_ext_data
	bl	FtlLoadEctTbl
	bl	FtlFreeSysBLkSort
	bl	SupperBlkListInit
	bl	FtlPowerLostRecovery
	mov	r0, #1
	bl	FtlUpdateVaildLpn
	movw	r3, #3950
	ldrh	r2, [r6, r3]
	mov	r3, r7
	ldr	r1, [r4, #-1856]
	b	.L2084
.L2086:
	add	r0, r1, r7
	add	r7, r7, #12
	ldr	r0, [r0, #4]
	cmp	r0, #0
	blt	.L2085
	add	r3, r3, #1
.L2084:
	cmp	r3, r2
	blt	.L2086
.L2085:
	ldr	r4, .L2093+4
	cmp	r3, r2
	ldr	r1, .L2093+12
	ldrh	r0, [r4, r1]
	add	r0, r0, #1
	strh	r0, [r4, r1]	@ movhi
	blt	.L2087
	mov	r3, #292
	ldrh	r3, [r4, r3]
	cmp	r3, #0
	beq	.L2088
.L2087:
	ldr	r0, .L2093+16
	bl	FtlSuperblockPowerLostFix
	ldr	r0, .L2093+20
	bl	FtlSuperblockPowerLostFix
	ldr	r3, .L2093+24
	ldr	r1, [r4, #-2020]
	ldr	r2, .L2093+28
	ldrh	r3, [r4, r3]
	ldrh	r0, [r4, r2]
	mov	r3, r3, asl #1
	ldrh	ip, [r1, r3]
	rsb	r0, r0, ip
	strh	r0, [r1, r3]	@ movhi
	ldr	r0, .L2093
	movw	r1, #3912
	ldr	r3, .L2093+32
	ldr	lr, [r4, #-2020]
	ldrh	ip, [r0, r1]
	strh	ip, [r4, r3]	@ movhi
	mov	r3, #0
	strh	r3, [r4, r2]	@ movhi
	add	r2, r2, #44
	strb	r3, [r4, #-1994]
	ldrh	ip, [r4, r2]
	add	r2, r2, #4
	ldrh	r5, [r4, r2]
	mov	ip, ip, asl #1
	ldrh	r6, [lr, ip]
	rsb	r5, r5, r6
	strh	r5, [lr, ip]	@ movhi
	ldrh	r0, [r0, r1]
	ldr	r1, .L2093+36
	strh	r3, [r4, r2]	@ movhi
	strb	r3, [r4, #-1946]
	strh	r0, [r4, r1]	@ movhi
	ldr	r0, .L2093+40
	bl	FtlMapBlkWriteDumpData
	ldr	r0, .L2093+44
	bl	FtlMapBlkWriteDumpData
	ldr	r3, .L2093+48
	ldrh	r2, [r4, r3]
	add	r2, r2, #1
	strh	r2, [r4, r3]	@ movhi
	bl	l2p_flush
	bl	FtlVpcTblFlush
	bl	FtlVpcTblFlush
.L2088:
	ldr	r4, .L2093+4
	movw	r3, #65535
	ldr	r5, .L2093+24
	ldrh	r2, [r4, r5]
	cmp	r2, r3
	beq	.L2089
	ldr	r3, .L2093+28
	ldrh	r3, [r4, r3]
	cmp	r3, #0
	bne	.L2089
	ldr	r3, .L2093+52
	ldrh	r3, [r4, r3]
	cmp	r3, #0
	bne	.L2089
	bl	FtlVpcTblFlush
	ldrh	r0, [r4, r5]
	bl	FtlGcRefreshOpenBlock
	ldr	r3, .L2093+56
	ldrh	r0, [r4, r3]
	bl	FtlGcRefreshOpenBlock
	add	r0, r4, r5
	bl	allocate_new_data_superblock
	sub	r0, r4, #1952
	bl	allocate_new_data_superblock
	add	r0, r4, #244
	bl	FtlMapBlkWriteDumpData
.L2089:
	ldr	r3, .L2093+12
	ldr	r2, .L2093+4
	ldrh	r5, [r2, r3]
	ands	r5, r5, #31
	movne	r5, #0
	bne	.L2083
	bl	FtlVpcCheckAndModify
.L2083:
	mov	r0, r5
	ldmfd	sp!, {r3, r4, r5, r6, r7, pc}
.L2094:
	.align	2
.L2093:
	.word	.LANCHOR0
	.word	.LANCHOR2
	.word	-1776
	.word	-2048
	.word	.LANCHOR2-2000
	.word	.LANCHOR2-1952
	.word	-2000
	.word	-1996
	.word	-1998
	.word	-1950
	.word	.LANCHOR2-1092
	.word	.LANCHOR2+244
	.word	-2046
	.word	-1948
	.word	-1952
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
	ldr	r3, .L2098
	ldr	r5, .L2098+4
	ldrh	r2, [r5, r3]
	movw	r3, #65535
	cmp	r2, r3
	beq	.L2096
	ldr	r3, .L2098+8
	ldrh	r3, [r5, r3]
	cmp	r3, #0
	bne	.L2097
.L2096:
	bl	FtlCacheWriteBack
	mov	r0, #0
	bl	FtlGcFreeTempBlock
	ldr	r0, .L2098+12
	mov	r4, #0
	strb	r4, [r5, #-1896]
	bl	allocate_data_superblock
	ldr	r3, .L2098+16
	strh	r4, [r5, r3]	@ movhi
	add	r3, r3, #12
	strh	r4, [r5, r3]	@ movhi
	bl	l2p_flush
	mov	r0, r4
	bl	FtlEctTblFlush
	bl	FtlVpcTblFlush
.L2097:
	ldr	r0, .L2098+12
	ldmfd	sp!, {r3, r4, r5, lr}
	b	get_new_active_ppa
.L2099:
	.align	2
.L2098:
	.word	-1904
	.word	.LANCHOR2
	.word	-1900
	.word	.LANCHOR2-1904
	.word	-1160
	.fnend
	.size	Ftl_get_new_temp_ppa, .-Ftl_get_new_temp_ppa
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
	ldr	r5, .L2110
	mov	r7, r0
	mov	r4, r1
	ldr	r3, [r5, #3952]
	cmp	r2, r3
	mvnhi	r0, #0
	bhi	.L2101
	cmp	r1, #31
	bls	.L2108
	ldr	r3, .L2110+4
	ldr	r3, [r3, #-2092]
	cmp	r3, #0
	bne	.L2108
	bl	FtlCacheWriteBack
	movw	r3, #3918
	ldrh	r6, [r5, r3]
	mov	r0, r7
	mov	r1, r6
	bl	__aeabi_uidiv
	mls	r7, r0, r6, r7
	mov	r5, r0
	uxth	r7, r7
	cmp	r7, #0
	beq	.L2102
	rsb	r6, r7, r6
	add	r5, r0, #1
	cmp	r6, r4
	movcs	r6, r4
	uxth	r6, r6
	rsb	r4, r6, r4
.L2102:
	mvn	r3, #0
	ldr	r7, .L2110
	str	r3, [sp, #4]
	movw	r6, #3918
	b	.L2103
.L2105:
	mov	r0, r5
	mov	r1, sp
	mov	r2, #0
	bl	log2phys
	ldr	r3, [sp, #0]
	cmn	r3, #1
	beq	.L2104
	ldr	r3, .L2110+4
	add	r1, sp, #4
	mov	r0, r5
	ldr	r2, [r3, #440]
	add	r2, r2, #1
	str	r2, [r3, #440]
	ldr	r2, [r3, #-1836]
	add	r2, r2, #1
	str	r2, [r3, #-1836]
	mov	r2, #1
	bl	log2phys
	ldr	r0, [sp, #0]
	ubfx	r0, r0, #10, #16
	bl	P2V_block_in_plane
	bl	decrement_vpc_count
.L2104:
	ldrh	r3, [r7, r6]
	add	r5, r5, #1
	rsb	r4, r3, r4
.L2103:
	ldrh	r3, [r7, r6]
	cmp	r4, r3
	bcs	.L2105
	ldr	r3, .L2110+4
	mov	r4, #0
	ldr	r2, [r3, #440]
	cmp	r2, #32
	bls	.L2109
	str	r4, [r3, #440]
	bl	l2p_flush
	bl	FtlVpcTblFlush
	b	.L2109
.L2108:
	mov	r0, #0
	b	.L2101
.L2109:
	mov	r0, r4
.L2101:
	ldmfd	sp!, {r1, r2, r3, r4, r5, r6, r7, pc}
.L2111:
	.align	2
.L2110:
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
	ldr	r3, .L2117
	ldr	r3, [r3, #-2092]
	cmp	r3, #0
	bne	.L2116
	bl	FtlCacheWriteBack
	bl	l2p_flush
	mov	r0, #1
	bl	FtlEctTblFlush
	bl	FtlVpcTblFlush
.L2116:
	mov	r0, #0
	ldmfd	sp!, {r3, pc}
.L2118:
	.align	2
.L2117:
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
	ldr	r3, .L2121
	ldr	r3, [r3, #2996]
	cmp	r3, #1
	bne	.L2120
	bl	FtlSysFlush
.L2120:
	mov	r0, #0
	ldmfd	sp!, {r3, pc}
.L2122:
	.align	2
.L2121:
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
	ldr	r0, .L2125
	bl	printk
	ldmfd	sp!, {r3, lr}
	b	ftl_deinit
.L2126:
	.align	2
.L2125:
	.word	.LC134
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
	ldr	r3, .L2137
	stmfd	sp!, {r4, r5, r6, r7, r8, sl, lr}
	.save {r4, r5, r6, r7, r8, sl, lr}
	.pad #52
	sub	sp, sp, #52
	ldrb	r3, [r3, #928]	@ zero_extendqisi2
	cmp	r3, #0
	beq	.L2128
	ldr	r4, .L2137+4
	movw	r3, #290
	ldr	r0, .L2137+8
	movw	r7, #4097
	ldrh	r6, [r4, r3]
	ldr	r3, [r4, #-2020]
	mov	r5, r6, asl #1
	mov	r1, r6
	ldrh	r2, [r3, r5]
	bl	printk
	ldr	r3, .L2137+12
	ldrh	r0, [r4, r3]
	bl	FtlGcRefreshOpenBlock
	ldr	r3, .L2137+16
	ldrh	r0, [r4, r3]
	bl	FtlGcRefreshOpenBlock
	sub	r0, r4, #2000
	bl	allocate_new_data_superblock
	sub	r0, r4, #1952
	bl	allocate_new_data_superblock
	b	.L2130
.L2132:
	mov	r0, #1
	mov	r1, r0
	bl	ftl_do_gc
	ldr	r3, [r4, #-2020]
	ldrh	r3, [r3, r5]
	cmp	r3, #0
	beq	.L2131
.L2130:
	subs	r7, r7, #1
	bne	.L2132
.L2131:
	ldr	r7, .L2137+4
	mov	r1, r6
	ldr	r0, .L2137+8
	ldr	r3, [r7, #-2020]
	ldrh	r2, [r3, r5]
	bl	printk
	ldr	r3, [r7, #-2020]
	ldrh	r4, [r3, r5]
	cmp	r4, #0
	bne	.L2133
	add	r8, sp, #48
	mov	r0, sp
	strh	r6, [r8, #-48]!	@ movhi
	add	r8, r8, #14
	bl	make_superblock
	ldr	r2, .L2137
	movw	r3, #3844
	ldr	r7, [r7, #-2088]
	movw	lr, #65535
	mov	ip, #36
	ldrh	sl, [r2, r3]
	mov	r1, r4
	mov	r3, r4
	b	.L2134
.L2136:
	ldrh	r0, [r8, #2]!
	cmp	r0, lr
	beq	.L2135
	mla	r2, ip, r4, r7
	add	r4, r4, #1
	mov	r0, r0, asl #10
	uxth	r4, r4
	stmib	r2, {r0, r1}
	str	r1, [r2, #12]
.L2135:
	add	r3, r3, #1
	uxth	r3, r3
.L2134:
	cmp	r3, sl
	bne	.L2136
	ldr	r7, .L2137+4
	mov	r1, r6
	ldr	r0, .L2137+20
	ldr	r3, [r7, #-2020]
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
.L2133:
	ldr	r2, .L2137+4
	movw	r3, #290
	mvn	r1, #0
	strh	r1, [r2, r3]	@ movhi
.L2128:
	add	sp, sp, #52
	ldmfd	sp!, {r4, r5, r6, r7, r8, sl, pc}
.L2138:
	.align	2
.L2137:
	.word	.LANCHOR0
	.word	.LANCHOR2
	.word	.LC135
	.word	-2000
	.word	-1952
	.word	.LC136
	.fnend
	.size	ftl_fix_nand_power_lost_error, .-ftl_fix_nand_power_lost_error
	.align	2
	.global	FtlInit
	.type	FtlInit, %function
FtlInit:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
.L2140:
	stmfd	sp!, {r4, r5, r6, lr}
	.save {r4, r5, r6, lr}
	mvn	r3, #0
	ldr	r4, .L2150
	ldr	r6, .L2150+4
	ldr	r5, .L2150+8
	ldr	r1, .L2150+12
	str	r3, [r6, #2996]
	mov	r3, #0
	ldr	r0, .L2150+16
	str	r3, [r4, #444]
	str	r3, [r4, #-2092]
	bl	printk
	ldr	r0, .L2150+20
	bl	FtlConstantsInit
	bl	FtlMemInit
	bl	FtlVariablesInit
	ldr	r0, [r5, #3848]
	uxth	r0, r0
	bl	FtlFreeSysBlkQueueInit
	bl	FtlLoadBbt
	cmp	r0, #0
	ldrne	r0, .L2150+24
	bne	.L2149
	bl	FtlSysBlkInit
	cmp	r0, #0
	beq	.L2143
	ldr	r0, .L2150+28
.L2149:
	ldr	r1, .L2150+32
	bl	printk
	b	.L2142
.L2143:
	mov	r1, #1
	str	r1, [r6, #2996]
	bl	ftl_do_gc
	ldr	r3, .L2150+36
	ldrh	r3, [r4, r3]
	cmp	r3, #15
	bhi	.L2144
	mov	r4, #1024
.L2145:
	mov	r0, #1
	mov	r1, r0
	bl	ftl_do_gc
	subs	r4, r4, #1
	bne	.L2145
	b	.L2142
.L2144:
	ldrb	r3, [r5, #928]	@ zero_extendqisi2
	cmp	r3, #0
	beq	.L2142
	mov	r4, #128
.L2146:
	mov	r0, #1
	mov	r1, r0
	bl	ftl_do_gc
	subs	r4, r4, #1
	bne	.L2146
.L2142:
	mov	r0, #0
	ldmfd	sp!, {r4, r5, r6, pc}
.L2151:
	.align	2
.L2150:
	.word	.LANCHOR2
	.word	.LANCHOR1
	.word	.LANCHOR0
	.word	.LC79
	.word	.LC78
	.word	.LANCHOR0+3048
	.word	.LC137
	.word	.LC138
	.word	.LANCHOR3+36
	.word	-2004
	.fnend
	.size	FtlInit, .-FtlInit
	.align	2
	.type	ftl_write.part.22, %function
ftl_write.part.22:
	.fnstart
	@ args = 0, pretend = 0, frame = 96
	@ frame_needed = 0, uses_anonymous_args = 0
	stmfd	sp!, {r4, r5, r6, r7, r8, r9, sl, fp, lr}
	.save {r4, r5, r6, r7, r8, r9, sl, fp, lr}
	mov	r3, #2048
	ldr	r4, .L2197
	.pad #100
	sub	sp, sp, #100
	ldr	r7, .L2197+4
	mov	r5, r1
	mov	r8, r0
	str	r2, [sp, #8]
	str	r3, [r4, #448]
	movw	r3, #3918
	ldrh	r6, [r7, r3]
	mov	r1, r6
	bl	__aeabi_uidiv
	mov	r1, r6
	str	r0, [sp, #12]
	sub	r0, r8, #1
	add	r0, r0, r5
	bl	__aeabi_uidiv
	ldr	r3, [sp, #12]
	ldr	sl, [r7, #3964]
	cmp	sl, #0
	rsb	r9, r3, r0
	str	r0, [sp, #36]
	add	r3, r9, #1
	str	r3, [sp, #16]
	ldr	lr, [sp, #16]
	ldr	r3, [r4, #-1832]
	add	r3, lr, r3
	str	r3, [r4, #-1832]
	ldr	r3, [r4, #-1816]
	add	r3, r5, r3
	str	r3, [r4, #-1816]
	beq	.L2185
	ldr	r3, [r7, #3968]
	sub	sl, sl, #1
	mov	r2, #36
	ldr	r0, [sp, #12]
	mla	sl, r2, sl, r3
	ldr	r3, [sl, #16]
	cmp	r0, r3
	strne	r5, [sp, #28]
	bne	.L2154
	ldr	r3, [r4, #-1828]
	mov	r0, r8
	mov	r1, r6
	add	r3, r3, #1
	str	r3, [r4, #-1828]
	ldr	r3, [r4, #452]
	add	r3, r3, #1
	str	r3, [r4, #452]
	bl	__aeabi_uidivmod
	ldr	r0, [sl, #8]
	rsb	r7, r1, r6
	add	r0, r0, r1, asl #9
	cmp	r7, r5
	movcs	r7, r5
	ldr	r1, [sp, #8]
	mov	fp, r7, asl #9
	mov	r2, fp
	bl	memcpy
	cmp	r9, #0
	bne	.L2155
	ldr	r3, [r4, #452]
	cmp	r3, #2
	ble	.L2156
.L2155:
	rsb	r3, r7, r5
	str	r3, [sp, #28]
	ldr	r3, [sp, #8]
	add	r8, r8, r7
	str	r9, [sp, #16]
	add	r3, r3, fp
	str	r3, [sp, #8]
	ldr	r3, [sp, #12]
	add	r3, r3, #1
	str	r3, [sp, #12]
.L2154:
	ldr	r3, .L2197
	mov	r2, #0
	str	r2, [r3, #452]
	b	.L2153
.L2185:
	str	r5, [sp, #28]
.L2153:
	ldr	r0, [sp, #12]
	ldr	r1, [sp, #36]
	bl	FtlCacheMetchLpa
	cmp	r0, #0
	beq	.L2157
	bl	FtlCacheWriteBack
.L2157:
	ldr	r7, .L2197
	cmp	r5, r6, asl #1
	ldr	r4, .L2197+4
	mov	fp, #0
	movcc	r3, #0
	movcs	r3, #1
	sub	r6, r7, #2000
	str	r3, [sp, #20]
	str	r6, [r7, #436]
	mov	sl, r4
	ldr	r5, [sp, #12]
	str	fp, [sp, #40]
	b	.L2194
.L2183:
	ldrh	r3, [r6, #4]
	cmp	r3, #0
	bne	.L2159
	ldr	lr, .L2197+8
	cmp	r6, lr
	bne	.L2160
	ldr	r3, .L2197+12
	ldrh	r3, [r7, r3]
	cmp	r3, #0
	bne	.L2161
	sub	r0, r9, #1952
	str	r3, [sp, #0]
	bl	allocate_new_data_superblock
	ldr	r3, [sp, #0]
	ldr	r0, .L2197+16
	str	r3, [r0, #3004]
.L2161:
	ldr	r0, .L2197+8
	bl	allocate_new_data_superblock
	ldr	r1, .L2197+16
	ldr	r3, .L2197+20
	ldr	r2, [r1, #3004]
	cmp	r2, #0
	movne	r6, r3
	b	.L2162
.L2160:
	ldr	r2, .L2197+16
	str	r3, [r2, #3004]
	ldr	r3, .L2197+24
	ldrh	r3, [r7, r3]
	cmp	r3, #0
	subne	r6, r9, #2000
	bne	.L2162
	mov	r0, r6
	bl	allocate_new_data_superblock
.L2162:
	ldrh	r3, [r6, #4]
	cmp	r3, #0
	bne	.L2163
	mov	r0, r6
	bl	allocate_new_data_superblock
.L2163:
	str	r6, [r7, #436]
.L2159:
	ldr	r1, [r7, #-980]
	mov	r9, r6
	ldr	r2, [r4, #3964]
	ldrh	r3, [r6, #4]
	rsb	r2, r2, r1
	cmp	r3, r2
	movcc	r2, r3
	ldr	r3, [sp, #16]
	cmp	r2, r3
	movcc	r3, r2
	str	r3, [sp, #52]
	mov	r3, #0
	str	r3, [sp, #24]
	b	.L2164
.L2180:
	ldrh	r3, [r9, #4]
	cmp	r3, #0
	beq	.L2165
	ldr	lr, [sp, #36]
	ldr	r0, [sp, #20]
	rsb	r2, lr, r5
	rsbs	r3, r2, #0
	adc	r3, r3, r2
	tst	r3, r0
	beq	.L2166
	ldr	r1, [sp, #24]
	cmp	r1, #0
	beq	.L2166
	movw	lr, #3918
	ldr	r0, [sp, #28]
	ldrh	r2, [r4, lr]
	add	r1, r0, r8
	mls	r1, r2, r5, r1
	cmp	r1, r2
	bne	.L2165
.L2166:
	add	r1, sp, #56
	mov	r2, #0
	mov	r0, r5
	str	r3, [sp, #0]
	bl	log2phys
	mov	r0, r9
	bl	get_new_active_ppa
	ldr	r2, [r4, #3964]
	ldr	r1, [r4, #3968]
	mov	ip, #36
	ldr	lr, [r4, #3964]
	mla	r1, ip, r2, r1
	movw	r2, #3924
	ldrh	r2, [r4, r2]
	str	r5, [r1, #16]
	str	r0, [r1, #4]
	mul	r0, lr, r2
	bic	r0, r0, #3
	str	r0, [sp, #44]
	ldr	r0, [r7, #-932]
	ldr	lr, [sp, #44]
	str	r0, [sp, #48]
	add	r6, r0, lr
	movw	r0, #3922
	ldr	lr, [r4, #3964]
	ldrh	r0, [r4, r0]
	str	r6, [r1, #12]
	mul	lr, lr, r0
	ldr	r0, [r7, #-952]
	str	lr, [sp, #32]
	bic	lr, lr, #3
	add	lr, r0, lr
	mov	r0, r6
	str	lr, [r1, #8]
	mov	r1, #0
	str	ip, [sp, #4]
	bl	ftl_memset
	ldr	r0, [sp, #12]
	ldr	ip, [sp, #4]
	rsb	r3, r0, r5
	rsbs	r0, r3, #0
	adc	r0, r0, r3
	ldr	r3, [sp, #0]
	str	r0, [sp, #32]
	orrs	r0, r0, r3
	beq	.L2167
	ldr	r1, [sp, #32]
	cmp	r1, #0
	beq	.L2168
	movw	r2, #3918
	mov	r0, r8
	ldrh	fp, [r4, r2]
	mov	r1, fp
	bl	__aeabi_uidivmod
	ldr	r3, [sp, #28]
	rsb	fp, r1, fp
	str	r1, [sp, #40]
	cmp	fp, r3
	movcs	fp, r3
	b	.L2169
.L2168:
	cmp	r3, #0
	beq	.L2169
	ldr	r3, [sp, #28]
	movw	lr, #3918
	add	fp, r3, r8
	ldrh	r3, [r4, lr]
	mls	fp, r3, r5, fp
	ldr	r3, [sp, #32]
	str	r3, [sp, #40]
	uxth	fp, fp
.L2169:
	movw	lr, #3918
	ldrh	r3, [r4, lr]
	cmp	fp, r3
	bne	.L2170
	ldr	r3, [sp, #32]
	ldr	r0, [sl, #3964]
	cmp	r3, #0
	ldr	r2, [sl, #3968]
	muleq	r1, r5, fp
	ldreq	r3, [sp, #8]
	ldrne	r1, [sp, #8]
	rsbeq	r1, r8, r1
	addeq	r1, r3, r1, asl #9
	ldr	r3, [sp, #20]
	cmp	r3, #0
	mov	r3, #36
	mla	r3, r3, r0, r2
	strne	r1, [r3, #8]
	bne	.L2173
	ldr	r0, [r3, #8]
	movw	r3, #3922
	ldrh	r2, [sl, r3]
	b	.L2195
.L2170:
	ldr	r2, [sp, #56]
	mov	r3, #36
	cmn	r2, #1
	beq	.L2174
	ldr	r1, [r4, #3964]
	add	r0, sp, #60
	str	r2, [sp, #64]
	ldr	r2, [r4, #3968]
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
	ldreq	r3, [r7, #-1640]
	addeq	r3, r3, #1
	streq	r3, [r7, #-1640]
	beq	.L2176
	ldr	r3, [r6, #8]
	cmp	r3, r5
	beq	.L2176
	ldr	r3, [r7, #-1640]
	mov	r2, r5
	ldr	r0, .L2197+28
	add	r3, r3, #1
	str	r3, [r7, #-1640]
	ldr	r1, [r6, #8]
	bl	printk
	b	.L2176
.L2174:
	ldr	r1, [r4, #3964]
	ldr	r2, [r4, #3968]
	mla	r3, r3, r1, r2
	mov	r1, #0
	ldr	r0, [r3, #8]
	movw	r3, #3922
	ldrh	r2, [r4, r3]
	bl	ftl_memset
.L2176:
	ldr	r3, [sp, #32]
	ldr	r1, [r4, #3964]
	ldr	r2, [r4, #3968]
	cmp	r3, #0
	mov	r3, #36
	mla	r3, r3, r1, r2
	ldrne	r0, [r3, #8]
	ldrne	r3, [sp, #40]
	ldrne	r1, [sp, #8]
	addne	r0, r0, r3, asl #9
	bne	.L2196
.L2177:
	movw	lr, #3918
	ldr	r0, [r3, #8]
	ldrh	r1, [r4, lr]
	ldr	r3, [sp, #8]
	mul	r1, r1, r5
	rsb	r1, r8, r1
	add	r1, r3, r1, asl #9
.L2196:
	mov	r2, fp, asl #9
	b	.L2195
.L2167:
	ldr	r3, [sp, #20]
	ldr	r2, [r4, #3964]
	cmp	r3, #0
	ldr	r3, [r4, #3968]
	mla	ip, ip, r2, r3
	beq	.L2178
	movw	lr, #3918
	ldr	r0, [sp, #8]
	ldrh	r3, [r4, lr]
	mul	r3, r3, r5
	rsb	r3, r8, r3
	add	r3, r0, r3, asl #9
	str	r3, [ip, #8]
	b	.L2173
.L2178:
	movw	r2, #3918
	ldr	r3, [sp, #8]
	ldrh	r1, [r4, r2]
	movw	lr, #3922
	ldr	r0, [ip, #8]
	ldrh	r2, [r4, lr]
	mul	r1, r1, r5
	rsb	r1, r8, r1
	add	r1, r3, r1, asl #9
.L2195:
	bl	memcpy
.L2173:
	ldr	r3, .L2197+32
	ldr	r1, [sp, #48]
	ldr	r0, [sp, #44]
	strh	r3, [r1, r0]	@ movhi
	ldr	r3, [r7, #-1804]
	str	r5, [r6, #8]
	add	r5, r5, #1
	str	r3, [r6, #4]
	add	r3, r3, #1
	cmn	r3, #1
	str	r3, [r7, #-1804]
	moveq	r3, #0
	streq	r3, [r7, #-1804]
	ldr	r3, [sp, #56]
	str	r3, [r6, #12]
	ldrh	r3, [r9, #0]
	strh	r3, [r6, #2]	@ movhi
	ldr	r3, [r4, #3964]
	add	r3, r3, #1
	str	r3, [r4, #3964]
	ldr	r3, [sp, #24]
	add	r3, r3, #1
	str	r3, [sp, #24]
.L2164:
	ldr	r3, [sp, #24]
	ldr	lr, [sp, #52]
	cmp	r3, lr
	bne	.L2180
.L2165:
	ldr	r3, [sp, #16]
	mov	r6, r9
	ldr	lr, [sp, #24]
	ldr	r2, [r4, #3964]
	rsb	r3, lr, r3
	ldr	r0, [sp, #20]
	str	r3, [sp, #16]
	ldr	r3, [r7, #-980]
	cmp	r2, r3
	orrcs	r0, r0, #1
	uxtb	r3, r0
	cmp	r3, #0
	bne	.L2181
	ldrh	r2, [r9, #4]
	cmp	r2, #0
	bne	.L2191
.L2181:
	bl	FtlCacheWriteBack
	ldr	lr, [sp, #16]
	mov	r3, #0
	str	r3, [sl, #3964]
	cmp	lr, #1
	ldr	r3, [sp, #20]
	movls	r3, #0
.L2191:
	str	r3, [sp, #20]
.L2194:
	ldr	r3, [sp, #16]
	ldr	r9, .L2197
	cmp	r3, #0
	bne	.L2183
	mov	r0, r3
	ldr	lr, [sp, #12]
	ldr	r3, [sp, #36]
	rsb	r1, lr, r3
	bl	ftl_do_gc
	ldr	r3, .L2197+36
	ldrh	r3, [r9, r3]
	cmp	r3, #31
	bhi	.L2156
	ldr	r2, .L2197+40
	mov	r3, #128
	mov	r4, #16
	strh	r3, [r9, r2]	@ movhi
	ldr	r2, .L2197+44
	strh	r3, [r9, r2]	@ movhi
.L2184:
	mov	r0, #0
	mov	r1, #1
	bl	ftl_do_gc
	ldr	r3, [r9, #-2092]
	cmp	r3, #0
	bne	.L2156
	subs	r4, r4, #1
	bne	.L2184
.L2156:
	mov	r0, #0
	add	sp, sp, #100
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, sl, fp, pc}
.L2198:
	.align	2
.L2197:
	.word	.LANCHOR2
	.word	.LANCHOR0
	.word	.LANCHOR2-2000
	.word	-1948
	.word	.LANCHOR1
	.word	.LANCHOR2-1952
	.word	-1996
	.word	.LC139
	.word	-3947
	.word	-2004
	.word	-1198
	.word	-1200
	.fnend
	.size	ftl_write.part.22, .-ftl_write.part.22
	.align	2
	.global	ftl_write
	.type	ftl_write, %function
ftl_write:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	@ link register save eliminated.
	mov	ip, r0
	mov	r0, r1
	mov	r1, r2
	mov	r2, r3
	ldr	r3, .L2204
	ldr	r3, [r3, #-2092]
	cmp	r3, #0
	movne	r0, #0
	bxne	lr
	cmp	ip, #16
	bne	.L2201
	add	r0, r0, #256
	b	FtlVendorPartWrite
.L2201:
	ldr	r3, .L2204+4
	add	ip, r1, r0
	ldr	r3, [r3, #3952]
	cmp	ip, r3
	bhi	.L2203
	b	ftl_write.part.22
.L2203:
	mvn	r0, #0
	bx	lr
.L2205:
	.align	2
.L2204:
	.word	.LANCHOR2
	.word	.LANCHOR0
	.fnend
	.size	ftl_write, .-ftl_write
	.align	2
	.global	FtlWrite
	.type	FtlWrite, %function
FtlWrite:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	@ link register save eliminated.
	b	ftl_write
	.fnend
	.size	FtlWrite, .-FtlWrite
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
	bne	.L2212
	add	r0, r1, #256
	mov	r1, r2
	mov	r2, r3
	bl	FtlVendorPartRead
	str	r0, [sp, #16]
	b	.L2213
.L2212:
	ldr	r3, [sp, #28]
	add	r3, r3, r1
	str	r3, [sp, #20]
	ldr	r3, .L2242
	ldr	r1, [sp, #20]
	ldr	r2, [r3, #3952]
	cmp	r1, r2
	mvnhi	r3, #0
	strhi	r3, [sp, #16]
	bhi	.L2213
	movw	r2, #3918
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
	ldr	r3, .L2242+4
	mov	fp, r0
	ldr	r0, [sp, #12]
	ldr	r2, [r3, #-1812]
	add	r2, r1, r2
	ldr	r1, [sp, #8]
	str	r2, [r3, #-1812]
	ldr	r2, [r3, #-1840]
	add	r2, r1, r2
	mov	r1, fp
	str	r2, [r3, #-1840]
	bl	FtlCacheMetchLpa
	cmp	r0, #0
	beq	.L2214
	bl	FtlCacheWriteBack
.L2214:
	mov	r9, #0
	ldr	r6, [sp, #12]
	str	r9, [sp, #32]
	mov	r5, r9
	str	r9, [sp, #16]
	ldr	r7, .L2242
	b	.L2237
.L2231:
	mov	r2, #0
	mov	r0, r6
	add	r1, sp, #60
	bl	log2phys
	ldr	r2, [sp, #60]
	cmn	r2, #1
	bne	.L2238
	b	.L2241
.L2219:
	mla	r0, r0, r6, sl
	cmp	r0, r4
	bcc	.L2218
	ldr	r2, [sp, #20]
	cmp	r0, r2
	bcs	.L2218
	rsb	r0, r4, r0
	mov	r1, #0
	mov	r2, #512
	str	r3, [sp, #4]
	add	r0, r8, r0, asl #9
	bl	ftl_memset
	ldr	r3, [sp, #4]
.L2218:
	add	sl, sl, #1
	b	.L2216
.L2241:
	mov	sl, #0
	movw	r3, #3918
.L2216:
	ldrh	r0, [r7, r3]
	cmp	sl, r0
	bcc	.L2219
	b	.L2220
.L2238:
	ldr	r3, .L2242+4
	mov	sl, #36
	ldr	r1, [r3, #-976]
	mla	sl, sl, r5, r1
	ldr	r1, [sp, #12]
	cmp	r6, r1
	str	r2, [sl, #4]
	movw	r2, #3918
	bne	.L2221
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
	b	.L2222
.L2221:
	cmp	r6, fp
	ldrneh	r3, [r7, r2]
	mulne	r3, r3, r6
	bne	.L2239
	ldr	r3, [r3, #-944]
	ldrh	r2, [r7, r2]
	ldr	r1, [sp, #20]
	str	r3, [sl, #8]
	mul	r3, r2, r6
	rsb	r9, r3, r1
	cmp	r9, r2
	bne	.L2222
.L2239:
	rsb	r3, r4, r3
	add	r3, r8, r3, asl #9
	str	r3, [sl, #8]
.L2222:
	movw	r3, #3924
	ldr	r2, .L2242+4
	ldrh	r3, [r7, r3]
	str	r6, [sl, #16]
	ldr	r2, [r2, #-936]
	mul	r3, r3, r5
	add	r5, r5, #1
	bic	r3, r3, #3
	add	r3, r2, r3
	str	r3, [sl, #12]
.L2220:
	ldr	r3, [sp, #8]
	add	r6, r6, #1
	subs	r3, r3, #1
	str	r3, [sp, #8]
	beq	.L2224
	movw	r3, #3844
	ldrh	r3, [r7, r3]
	cmp	r5, r3, asl #3
	bne	.L2237
.L2224:
	cmp	r5, #0
	beq	.L2237
	ldr	sl, .L2242+4
	mov	r1, r5
	mov	r2, #0
	ldr	r0, [sl, #-976]
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
.L2230:
	ldr	r3, [sp, #24]
	mov	r5, #36
	ldr	r1, [sp, #12]
	mul	r5, r5, r3
	ldr	r3, [sl, #-976]
	add	r3, r3, r5
	ldr	r2, [r3, #16]
	cmp	r2, r1
	bne	.L2226
	ldr	r1, [r3, #8]
	ldr	r3, [sl, #-948]
	cmp	r1, r3
	bne	.L2227
	ldr	r3, [sp, #40]
	mov	r0, r8
	ldr	r2, [sp, #44]
	add	r1, r1, r3
	b	.L2240
.L2226:
	cmp	r2, fp
	bne	.L2227
	ldr	r1, [r3, #8]
	ldr	r3, [sl, #-944]
	cmp	r1, r3
	bne	.L2227
	movw	r2, #3918
	ldrh	r0, [r7, r2]
	ldr	r2, [sp, #48]
	mul	r0, r0, fp
	rsb	r0, r4, r0
	add	r0, r8, r0, asl #9
.L2240:
	bl	memcpy
.L2227:
	ldr	r3, [sl, #-976]
	add	r0, r3, r5
	ldr	r2, [r3, r5]
	cmn	r2, #1
	streq	r2, [sp, #16]
	ldreq	r1, [sl, #-1640]
	addeq	r1, r1, #1
	streq	r1, [sl, #-1640]
	ldr	r3, [r3, r5]
	cmp	r3, #256
	bne	.L2229
	ldr	r0, [r0, #4]
	ubfx	r0, r0, #10, #16
	bl	P2V_block_in_plane
	bl	FtlGcRefreshBlock
.L2229:
	ldr	r3, [sp, #24]
	add	r3, r3, #1
	str	r3, [sp, #24]
	cmp	r3, r6
	bne	.L2230
	ldr	r6, [sp, #52]
	mov	r5, #0
.L2237:
	ldr	r3, [sp, #8]
	cmp	r3, #0
	bne	.L2231
	ldr	r3, .L2242+8
	ldr	r2, .L2242+4
	ldrh	r3, [r2, r3]
	cmp	r3, #0
	beq	.L2213
	ldr	r0, [sp, #8]
	mov	r1, #1
	bl	ftl_do_gc
.L2213:
	ldr	r0, [sp, #16]
	add	sp, sp, #68
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, sl, fp, pc}
.L2243:
	.align	2
.L2242:
	.word	.LANCHOR0
	.word	.LANCHOR2
	.word	-1142
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
	.type	rk_ftl_vendor_ops.constprop.26, %function
rk_ftl_vendor_ops.constprop.26:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	@ link register save eliminated.
	cmp	r2, #0
	mov	r3, r0
	mov	r2, r3
	mov	r0, r1
	mov	r1, #128
	beq	.L2250
	b	FlashBootVendorWrite
.L2250:
	b	FlashBootVendorRead
	.fnend
	.size	rk_ftl_vendor_ops.constprop.26, .-rk_ftl_vendor_ops.constprop.26
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
	ldr	r9, .L2260
	movw	r3, #3060
	ldr	r4, .L2260+4
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
	b	.L2252
.L2258:
	ldr	r5, [sp, #4]
	sub	r3, r8, #12
	mul	fp, r8, r4
	ldr	sl, .L2260+4
	b	.L2253
.L2256:
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
	beq	.L2254
	ldr	r2, [sl, #224]
	ldrh	r1, [r2, #0]
	movw	r2, #61664
	cmp	r1, r2
	bne	.L2254
	ldr	r0, .L2260+8
	mov	r1, r4
	mov	r2, r5
	add	r6, r6, #1
	bl	printk
	ldr	r3, .L2260+4
	uxth	r6, r6
	add	r3, r3, r4, asl #1
	strh	r5, [r3, #228]	@ movhi
	b	.L2255
.L2254:
	sub	r5, r5, #1
	uxth	r5, r5
.L2253:
	cmp	r5, r3
	bgt	.L2256
.L2255:
	ldrb	r3, [r9, #3762]	@ zero_extendqisi2
	add	r4, r4, #1
	cmp	r3, r6
	uxtb	r4, r4
	moveq	r7, #0
.L2252:
	ldrb	r3, [r9, #3762]	@ zero_extendqisi2
	cmp	r3, r4
	bhi	.L2258
	mov	r0, r7
	add	sp, sp, #52
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, sl, fp, pc}
.L2261:
	.align	2
.L2260:
	.word	.LANCHOR0
	.word	.LANCHOR2
	.word	.LC140
	.fnend
	.size	FlashLoadFactorBbt, .-FlashLoadFactorBbt
	.align	2
	.global	FlashReadFacBbtData
	.type	FlashReadFacBbtData, %function
FlashReadFacBbtData:
	.fnstart
	@ args = 0, pretend = 0, frame = 40
	@ frame_needed = 0, uses_anonymous_args = 0
	ldr	r3, .L2272
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
	ldr	r7, .L2272+4
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
	b	.L2263
.L2269:
	mov	r1, #1
	add	r3, r5, sl
	add	r0, sp, #4
	mov	r2, r1
	mov	r3, r3, asl #10
	str	r3, [sp, #8]
	bl	FlashReadPages
	ldr	r3, [sp, #4]
	cmn	r3, #1
	beq	.L2264
	ldr	r3, [r7, #224]
	ldrh	r2, [r3, #0]
	movw	r3, #61664
	cmp	r2, r3
	bne	.L2264
	cmp	r4, #0
	moveq	r0, r4
	beq	.L2265
	cmp	r6, #0
	ldreq	r3, .L2272+4
	moveq	r0, #1
	beq	.L2266
	b	.L2267
.L2268:
	ldr	r2, [r3, #144]
	ubfx	r1, r6, #5, #16
	and	lr, r6, #31
	add	r6, r6, #1
	ldr	ip, [r2, r1, asl #2]
	uxth	r6, r6
	orr	ip, ip, r0, asl lr
	str	ip, [r2, r1, asl #2]
.L2266:
	ldr	r2, [r3, #152]
	cmp	r6, r2
	bcc	.L2268
.L2267:
	ldr	r3, .L2272+4
	mov	r2, r9
	mov	r0, r4
	ldr	r1, [r3, #144]
	bl	memcpy
	mov	r2, #4
	ldr	r0, .L2272+8
	mov	r1, r4
	mov	r3, r2
	bl	rknand_print_hex
	mov	r0, #0
	b	.L2265
.L2264:
	sub	r5, r5, #1
	uxth	r5, r5
.L2263:
	cmp	r5, r8
	bgt	.L2269
	mvn	r0, #0
.L2265:
	add	sp, sp, #40
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, sl, pc}
.L2273:
	.align	2
.L2272:
	.word	.LANCHOR0
	.word	.LANCHOR2
	.word	.LC141
	.fnend
	.size	FlashReadFacBbtData, .-FlashReadFacBbtData
	.align	2
	.global	FlashGetBadBlockList
	.type	FlashGetBadBlockList, %function
FlashGetBadBlockList:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	ldr	r3, .L2281
	stmfd	sp!, {r4, r5, r6, r7, r8, lr}
	.save {r4, r5, r6, r7, r8, lr}
	mov	r5, r0
	ldr	r3, [r3, #3624]
	ldr	r6, .L2281+4
	ldrb	r2, [r3, #13]	@ zero_extendqisi2
	ldrh	r4, [r3, #14]
	ldr	r0, [r6, #132]
	mul	r4, r4, r2
	uxth	r4, r4
	add	r2, r4, #7
	mov	r2, r2, lsr #3
	bl	FlashReadFacBbtData
	cmn	r0, #1
	beq	.L2280
	mov	r2, #0
	mov	r0, r4, lsr #4
	mov	r3, r2
	sub	r4, r4, #1
	mov	r1, #1
	b	.L2276
.L2278:
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
	bcs	.L2280
	add	r2, r2, #1
	uxth	r2, r2
.L2276:
	cmp	r2, r4
	blt	.L2278
	b	.L2275
.L2280:
	mov	r3, #0
.L2275:
	mov	r3, r3, asl #1
	mvn	r2, #0
	mov	r0, #0
	strh	r2, [r5, r3]	@ movhi
	ldmfd	sp!, {r4, r5, r6, r7, r8, pc}
.L2282:
	.align	2
.L2281:
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
	ldr	r4, .L2301
	ldr	r5, [r4, #-2092]
	cmp	r5, #0
	bne	.L2284
	ldr	r7, .L2301+4
	bl	FtlBbtMemInit
	ldr	r6, .L2301+8
	bl	FtlLoadFactoryBbt
	add	sl, r7, #12
	b	.L2285
.L2291:
	ldrh	r3, [sl], #2
	movw	r2, #65535
	ldr	r0, [r4, #-964]
	movw	fp, #3908
	ldr	r9, [r4, #-940]
	cmp	r3, r2
	str	r0, [r4, #184]
	str	r9, [r4, #188]
	beq	.L2286
	ldrh	r8, [r6, fp]
	mov	r1, #1
	mov	r2, r1
	ldr	r0, .L2301+12
	mla	r8, r8, r5, r3
	mov	r3, r8, asl #10
	str	r3, [r4, #180]
	bl	FlashReadPages
	ldrh	r2, [r6, fp]
	ldr	r0, [r7, #28]
	add	r2, r2, #7
	ldr	r1, [r4, #184]
	mov	r2, r2, lsr #3
	bl	memcpy
	b	.L2287
.L2286:
	mov	r1, r5
	bl	FlashGetBadBlockList
	ldr	r0, [r4, #184]
	ldr	r1, [r7, #28]
	bl	FtlBbt2Bitmap
	ldrh	fp, [r6, fp]
.L2289:
	sub	fp, fp, #1
	uxth	fp, fp
.L2300:
	movw	r8, #3908
	ldrh	r0, [r6, r8]
	mla	r0, r0, r5, fp
	uxth	r0, r0
	bl	FtlBbmIsBadBlock
	cmp	r0, #1
	beq	.L2289
	mov	r1, #0
	mov	r2, #16
	strh	fp, [sl, #-2]	@ movhi
	ldr	r0, [r4, #-940]
	bl	ftl_memset
	ldr	r3, .L2301+16
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
	ldr	r3, .L2301+20
	ldrh	r2, [r4, r3]
	mov	r2, r2, asl #2
	bl	memcpy
	mov	r1, #1
	mov	r2, r1
	ldr	r0, .L2301+12
	bl	FlashEraseBlocks
	mov	r1, #1
	mov	r3, r1
	ldr	r0, .L2301+12
	mov	r2, r1
	bl	FlashProgPages
	ldr	r3, [r4, #176]
	cmn	r3, #1
	bne	.L2287
	uxth	r0, r8
	bl	FtlBbmMapBadBlock
	b	.L2300
.L2287:
	uxth	r0, r8
	add	r5, r5, #1
	bl	FtlBbmMapBadBlock
	add	r7, r7, #4
.L2285:
	movw	r3, #3866
	ldrh	r3, [r6, r3]
	cmp	r5, r3
	bcc	.L2291
	mov	r4, #0
	ldr	r7, .L2301+8
	movw	r6, #3926
	b	.L2292
.L2293:
	mov	r0, r4
	add	r4, r4, #1
	bl	FtlBbmMapBadBlock
	uxth	r4, r4
.L2292:
	ldrh	r3, [r7, r6]
	ldr	r5, .L2301+8
	cmp	r3, r4
	bhi	.L2293
	movw	r7, #3988
	movw	r6, #3976
	ldrh	r4, [r5, r7]
	sub	r4, r4, #1
	uxth	r4, r4
	b	.L2294
.L2299:
	mov	r0, r4
	bl	FtlBbmIsBadBlock
	cmp	r0, #1
	beq	.L2295
	mov	r0, r4
	bl	FlashTestBlk
	cmp	r0, #0
	beq	.L2296
	mov	r0, r4
	bl	FtlBbmMapBadBlock
	b	.L2295
.L2296:
	ldrh	r2, [r5, r6]
	movw	r3, #65535
	cmp	r2, r3
	streqh	r4, [r5, r6]	@ movhi
.L2297:
	ldrne	r2, .L2301+8
	movwne	r3, #3980
	strneh	r4, [r2, r3]	@ movhi
	bne	.L2298
.L2295:
	sub	r4, r4, #1
	uxth	r4, r4
.L2294:
	ldrh	r3, [r5, r7]
	sub	r3, r3, #48
	cmp	r4, r3
	bgt	.L2299
.L2298:
	ldr	r4, .L2301+8
	movw	r6, #3976
	ldr	r3, .L2301
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
.L2284:
	mov	r0, #0
	ldmfd	sp!, {r3, r4, r5, r6, r7, r8, r9, sl, fp, pc}
.L2302:
	.align	2
.L2301:
	.word	.LANCHOR2
	.word	.LANCHOR0+3976
	.word	.LANCHOR0
	.word	.LANCHOR2+176
	.word	-3872
	.word	-1032
	.fnend
	.size	FtlMakeBbt, .-FtlMakeBbt
	.align	2
	.type	FtlLowFormat.part.24, %function
FtlLowFormat.part.24:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	stmfd	sp!, {r3, r4, r5, r6, r7, r8, r9, sl, fp, lr}
	.save {r3, r4, r5, r6, r7, r8, r9, sl, fp, lr}
	movw	r6, #3948
	ldr	r5, .L2324
	mov	r1, #0
	ldr	r4, .L2324+4
	ldrh	r2, [r5, r6]
	ldr	r0, [r4, #-904]
	mov	r2, r2, asl #2
	bl	ftl_memset
	ldrh	r2, [r5, r6]
	mov	r1, #0
	ldr	r0, [r4, #-1036]
	mov	r2, r2, asl #2
	bl	ftl_memset
	ldr	r0, [r5, #3848]
	mov	r3, #0
	str	r3, [r4, #-1808]
	str	r3, [r4, #-1804]
	uxth	r0, r0
	bl	FtlFreeSysBlkQueueInit
	bl	FtlLoadBbt
	cmp	r0, #0
	beq	.L2304
	bl	FtlMakeBbt
.L2304:
	mov	r3, #0
	ldr	ip, .L2324
	movw	r0, #3918
	ldr	r2, .L2324+4
	ldr	r1, .L2324+8
	b	.L2305
.L2306:
	ldr	lr, [r2, #-948]
	mvn	r4, r3
	orr	r4, r3, r4, asl #16
	str	r4, [lr, r3, asl #2]
	ldr	lr, [r2, #-944]
	str	r1, [lr, r3, asl #2]
	add	r3, r3, #1
	uxth	r3, r3
.L2305:
	ldrh	lr, [ip, r0]
	ldr	r8, .L2324
	cmp	r3, lr, asl #7
	blt	.L2306
	movw	r3, #3852
	mov	r5, #0
	ldrh	r6, [r8, r3]
	movw	r7, #3854
	b	.L2307
.L2308:
	mov	r0, r6
	mov	r1, #1
	bl	FtlLowFormatEraseBlock
	add	r6, r6, #1
	uxth	r6, r6
	add	r5, r5, r0
	uxth	r5, r5
.L2307:
	ldrh	r3, [r8, r7]
	ldr	r4, .L2324
	cmp	r3, r6
	bhi	.L2308
	movw	r3, #3844
	ldrh	r1, [r4, r3]
	sub	r3, r5, #3
	cmp	r3, r1, asl #1
	blt	.L2309
	mov	r0, r5
	movw	r6, #3854
	bl	__aeabi_uidiv
	ldr	r3, [r4, #3944]
	add	r0, r0, r3
	uxth	r0, r0
	bl	FtlSysBlkNumInit
	ldr	r0, [r4, #3848]
	uxth	r0, r0
	bl	FtlFreeSysBlkQueueInit
	movw	r3, #3852
	ldrh	r5, [r4, r3]
	b	.L2310
.L2311:
	mov	r0, r5
	mov	r1, #1
	bl	FtlLowFormatEraseBlock
	add	r5, r5, #1
	uxth	r5, r5
.L2310:
	ldrh	r3, [r4, r6]
	cmp	r3, r5
	bhi	.L2311
.L2309:
	mov	r4, #0
	ldr	r8, .L2324
	mov	r6, r4
	movw	r7, #3852
	b	.L2312
.L2313:
	mov	r0, r6
	mov	r1, #0
	bl	FtlLowFormatEraseBlock
	add	r6, r6, #1
	uxth	r6, r6
	add	r4, r4, r0
	uxth	r4, r4
.L2312:
	ldrh	r3, [r8, r7]
	ldr	r5, .L2324
	cmp	r3, r6
	bhi	.L2313
	movw	r3, #3854
	ldr	r8, .L2324+4
	ldrh	r3, [r5, r3]
	ldr	fp, [r5, #3856]
	ldr	r9, .L2324+12
	str	r3, [r8, #-988]
	movw	r3, #3844
	ldrh	r6, [r5, r3]
	mov	r0, fp
	mov	r1, r6
	bl	__aeabi_uidiv
	ubfx	sl, r0, #5, #16
	mov	r7, r0
	add	r3, sl, #36
	strh	r3, [r8, r9]	@ movhi
	mov	r3, #24
	str	r0, [r5, #3972]
	mul	r3, r3, r6
	cmp	r4, r3
	ble	.L2314
	rsb	r0, r4, fp
	mov	r1, r6
	bl	__aeabi_uidiv
	str	r0, [r5, #3972]
	mov	r0, r0, lsr #5
	add	r0, r0, #24
	strh	r0, [r8, r9]	@ movhi
.L2314:
	ldr	r3, .L2324
	ldr	r3, [r3, #3836]
	cmp	r3, #1
	bne	.L2315
	mov	r0, r4
	mov	r1, r6
	bl	__aeabi_uidiv
	ldr	r8, .L2324+4
	ldr	r5, .L2324+12
	ldrh	r9, [r8, r5]
	uxtah	r0, r9, r0
	add	r9, r9, r0, lsr #2
	strh	r9, [r8, r5]	@ movhi
.L2315:
	ldr	r3, .L2324
	ldrb	r3, [r3, #928]	@ zero_extendqisi2
	cmp	r3, #0
	beq	.L2316
	mov	r0, r4
	mov	r1, r6
	bl	__aeabi_uidiv
	ldr	r8, .L2324+4
	ldr	r5, .L2324+12
	ldrh	r9, [r8, r5]
	uxtah	r0, r9, r0
	add	r9, r9, r0, lsr #2
	strh	r9, [r8, r5]	@ movhi
.L2316:
	ldr	r0, .L2324
	mov	r3, #3904
	ldrh	r3, [r0, r3]
	cmp	r3, #0
	beq	.L2317
	ldr	r1, .L2324+4
	ldr	r2, .L2324+12
	ldrh	ip, [r1, r2]
	add	ip, ip, r3, lsr #1
	strh	ip, [r1, r2]	@ movhi
	mul	ip, r6, r3
	cmp	ip, r4
	strgt	r7, [r0, #3972]
	addgt	r3, r3, #32
	addgt	sl, sl, r3
	strgth	sl, [r1, r2]	@ movhi
.L2317:
	ldr	r4, .L2324+4
	ldr	r5, .L2324
	ldr	r3, .L2324+12
	ldr	r2, [r5, #3972]
	ldrh	r3, [r4, r3]
	rsb	r3, r3, r2
	mul	r6, r6, r3
	movw	r3, #3912
	ldrh	r3, [r5, r3]
	str	r6, [r4, #212]
	mul	r6, r3, r6
	movw	r3, #3918
	ldrh	r3, [r5, r3]
	str	r6, [r5, #3972]
	mul	r6, r3, r6
	str	r6, [r5, #3952]
	bl	FtlBbmTblFlush
	movw	r3, #3854
	ldrh	r2, [r5, r3]
	mov	r1, #0
	ldr	r0, [r4, #-2020]
	ldr	r6, .L2324+16
	mov	r2, r2, asl #1
	bl	ftl_memset
	ldr	r2, .L2324+20
	mvn	r1, #0
	mov	r3, #0
	str	r3, [r4, #-1848]
	strh	r1, [r4, r2]	@ movhi
	ldr	r1, .L2324+24
	strb	r3, [r4, #-1758]
	strb	r3, [r4, #-1756]
	strh	r3, [r4, r1]	@ movhi
	sub	r1, r1, #236
	strb	r3, [r4, #-1994]
	strh	r3, [r4, r1]	@ movhi
	mov	r1, #255
	strh	r3, [r4, r6]	@ movhi
	mov	r3, #1
	strb	r3, [r4, #-1992]
	movw	r3, #3852
	ldrh	r2, [r5, r3]
	ldr	r0, [r4, #-1844]
	mov	r2, r2, lsr #3
	bl	ftl_memset
.L2318:
	ldr	r5, .L2324+4
	sub	r0, r5, #2000
	bl	make_superblock
	ldrb	r3, [r4, #-1993]	@ zero_extendqisi2
	cmp	r3, #0
	bne	.L2319
	ldrh	r3, [r4, r6]
	mvn	r1, #0
	ldr	r2, [r4, #-2020]
	mov	r3, r3, asl #1
	strh	r1, [r2, r3]	@ movhi
	ldrh	r3, [r4, r6]
	add	r3, r3, #1
	strh	r3, [r4, r6]	@ movhi
	b	.L2318
.L2319:
	ldr	r3, [r5, #-1808]
	ldr	r1, [r5, #-2020]
	ldr	r6, .L2324+28
	str	r3, [r5, #-1988]
	add	r3, r3, #1
	str	r3, [r5, #-1808]
	ldr	r3, .L2324+32
	ldrh	r0, [r5, r3]
	ldr	r3, .L2324+16
	ldrh	r2, [r5, r3]
	mov	r2, r2, asl #1
	strh	r0, [r1, r2]	@ movhi
	mov	r2, #0
	ldr	r0, .L2324+36
	ldrh	r3, [r5, r3]
	strb	r2, [r5, #-1946]
	add	r3, r3, #1
	strh	r2, [r5, r0]	@ movhi
	strh	r3, [r5, r6]	@ movhi
	mov	r3, #1
	strb	r3, [r5, #-1944]
.L2320:
	ldr	r4, .L2324+4
	sub	r0, r4, #1952
	bl	make_superblock
	ldrb	r3, [r5, #-1945]	@ zero_extendqisi2
	cmp	r3, #0
	bne	.L2321
	ldrh	r3, [r5, r6]
	mvn	r1, #0
	ldr	r2, [r5, #-2020]
	mov	r3, r3, asl #1
	strh	r1, [r2, r3]	@ movhi
	ldrh	r3, [r5, r6]
	add	r3, r3, #1
	strh	r3, [r5, r6]	@ movhi
	b	.L2320
.L2321:
	ldr	r3, [r4, #-1808]
	mvn	r5, #0
	ldr	r2, [r4, #-2020]
	str	r3, [r4, #-1940]
	add	r3, r3, #1
	str	r3, [r4, #-1808]
	ldr	r3, .L2324+40
	ldrh	r1, [r4, r3]
	ldr	r3, .L2324+28
	ldrh	r3, [r4, r3]
	mov	r3, r3, asl #1
	strh	r1, [r2, r3]	@ movhi
	ldr	r3, .L2324+44
	strh	r5, [r4, r3]	@ movhi
	bl	FtlFreeSysBlkQueueOut
	ldr	r3, .L2324+48
	mov	r2, #0
	strh	r0, [r4, r3]	@ movhi
	add	r3, r3, #2
	strh	r2, [r4, r3]	@ movhi
	add	r3, r3, #2
	ldr	r2, [r4, #212]
	strh	r5, [r4, r3]	@ movhi
	add	r3, r3, #2
	strh	r2, [r4, r3]	@ movhi
	ldr	r3, [r4, #-1808]
	str	r3, [r4, #-1768]
	add	r3, r3, #1
	str	r3, [r4, #-1808]
	bl	FtlVpcTblFlush
	bl	FtlSysBlkInit
	cmp	r0, #0
	ldmnefd	sp!, {r3, r4, r5, r6, r7, r8, r9, sl, fp, pc}
	ldr	r3, .L2324+52
	mov	r2, #1
	str	r2, [r3, #2996]
	ldmfd	sp!, {r3, r4, r5, r6, r7, r8, r9, sl, fp, pc}
.L2325:
	.align	2
.L2324:
	.word	.LANCHOR0
	.word	.LANCHOR2
	.word	168778952
	.word	-1780
	.word	-2000
	.word	-1764
	.word	-1762
	.word	-1952
	.word	-1996
	.word	-1950
	.word	-1948
	.word	-1904
	.word	-1776
	.word	.LANCHOR1
	.fnend
	.size	FtlLowFormat.part.24, .-FtlLowFormat.part.24
	.align	2
	.global	FtlLowFormat
	.type	FtlLowFormat, %function
FtlLowFormat:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	stmfd	sp!, {r3, lr}
	.save {r3, lr}
	ldr	r3, .L2328
	ldr	r3, [r3, #-2092]
	cmp	r3, #0
	bne	.L2327
	bl	FtlLowFormat.part.24
.L2327:
	mov	r0, #0
	ldmfd	sp!, {r3, pc}
.L2329:
	.align	2
.L2328:
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
	ldr	r3, .L2415
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
	bne	.L2331
	mvn	r2, #88
	strb	r2, [r3, #4]
	ldr	r3, .L2415+4
	mvn	r2, #8
	strb	r2, [r3, #3025]
	b	.L2387
.L2331:
	cmp	r4, #3
	bne	.L2333
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
	b	.L2410
.L2333:
	cmp	r4, #4
	bne	.L2334
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
.L2410:
	mov	r6, #8
	strb	r2, [r3, #11]
	mov	r5, r6
	b	.L2332
.L2334:
	cmp	r4, #5
	bne	.L2335
	mov	r2, #56
	mov	r6, #8
	strb	r2, [r3, #4]
	mov	r2, #57
	strb	r2, [r3, #5]
	mov	r2, #58
	strb	r2, [r3, #6]
	mov	r2, #59
	strb	r2, [r3, #7]
	b	.L2409
.L2335:
	cmp	r4, #6
	bne	.L2336
	mov	r2, #14
	mov	r6, #12
	strb	r2, [r3, #4]
	mov	r2, #15
	strb	r2, [r3, #5]
	mov	r2, #16
	strb	r2, [r3, #6]
	mov	r2, #17
	strb	r2, [r3, #7]
	b	.L2409
.L2336:
	cmp	r4, #7
	bne	.L2387
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
	b	.L2332
.L2387:
	mov	r6, #7
.L2409:
	mov	r5, #4
.L2332:
	sub	r3, r4, #1
	cmp	r3, #1
	bhi	.L2405
	b	.L2413
.L2343:
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
.L2339:
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
	bcc	.L2339
	mov	r2, #0
	b	.L2340
.L2341:
	ldrb	r7, [r0, r1, asl #2]	@ zero_extendqisi2
	ldrb	r5, [r4, r2]	@ zero_extendqisi2
	add	r5, r7, r5
	strb	r5, [ip, r1, asl #3]
	add	r1, r1, #1
	cmp	r1, #7
	bne	.L2341
	add	r2, r2, #1
	cmp	r2, #4
	beq	.L2342
.L2340:
	mov	r1, #1
	add	ip, r4, r2
	add	r0, r2, r9
	b	.L2341
.L2342:
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
	b	.L2337
.L2413:
	ldr	r9, .L2415+8
	mov	r3, r6
	mov	r8, #0
	mov	r6, r5
.L2337:
	ldr	fp, .L2415
	ldrb	r2, [fp, #3762]	@ zero_extendqisi2
	cmp	r2, r8
	bhi	.L2343
	mov	r5, r6
	mov	r6, r3
	b	.L2344
.L2405:
	sub	r3, r4, #3
	cmp	r3, #4
	bhi	.L2344
	mul	r3, r6, r5
	mov	sl, #0
	mov	r7, sl
	str	r6, [sp, #48]
	mov	r2, r3, asr #2
	mov	r3, r3, asl #4
	str	r2, [sp, #24]
	str	r3, [sp, #52]
	b	.L2345
.L2386:
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
	bne	.L2348
	mov	r3, #255
	str	r3, [r8, #4]
	mov	r3, #64
	str	r3, [r9, fp, asl #8]
	mov	r3, #204
	b	.L2411
.L2348:
	sub	r3, r4, #5
	cmp	r3, #1
	ldrls	r3, .L2415
	ldrlsb	r3, [r3, #4]	@ zero_extendqisi2
	strls	r3, [r8, #4]
	movls	r3, #82
	bls	.L2412
.L2350:
	mov	r3, #174
	str	r3, [r8, #4]
	mov	r3, #176
	str	r7, [r9, fp, asl #8]
.L2411:
	str	r3, [r8, #4]
	mov	r3, #77
.L2412:
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
	bls	.L2354
	cmp	r4, #7
	moveq	r1, #32
	movne	r1, #2
.L2354:
	ldr	r3, .L2415+12
	ldr	r2, [r3, #144]
	mov	r3, #0
.L2355:
	ldr	r0, [r8, #0]
	strb	r0, [r2, r3]
	add	r3, r3, #1
	uxtb	r0, r3
	cmp	r0, r1
	bcc	.L2355
	cmp	r4, #7
	bne	.L2356
	mov	r3, #0
.L2358:
	ldrb	r1, [r2, #0]	@ zero_extendqisi2
	cmp	r1, #12
	beq	.L2357
	ldrb	r1, [r2, #1]	@ zero_extendqisi2
	cmp	r1, #10
	beq	.L2357
	add	r3, r3, #1
	add	r2, r2, #4
	uxtb	r3, r3
	cmp	r3, #8
	bne	.L2358
	b	.L2359
.L2357:
	cmp	r3, #6
	bls	.L2360
.L2359:
	ldr	r0, .L2415+16
	mov	r1, #0
	bl	printk
.L2361:
	b	.L2361
.L2356:
	cmp	r4, #6
	bne	.L2360
	mov	r1, #8
.L2362:
	ldrb	r3, [r2], #1	@ zero_extendqisi2
	cmp	r3, #12
	beq	.L2360
	ldrb	r3, [r2, #7]	@ zero_extendqisi2
	cmp	r3, #4
	beq	.L2360
	sub	r1, r1, #1
	uxtb	r1, r1
	cmp	r1, #0
	bne	.L2362
	b	.L2414
.L2364:
	b	.L2364
.L2360:
	ldr	r3, .L2415+12
	mov	r2, #0
	ldr	ip, [r3, #144]
	str	ip, [sp, #40]
.L2365:
	ldr	r1, [r8, #0]
	ldr	lr, [sp, #40]
	ldr	ip, [sp, #52]
	strb	r1, [lr, r2]
	add	r2, r2, #1
	cmp	r2, ip
	blt	.L2365
	ldr	r2, [sp, #24]
	ldr	r1, [r3, #144]
	mov	r3, #8
	mov	ip, r2, asl #2
	mov	lr, r2, asl #3
	add	r1, r1, ip
	str	ip, [sp, #32]
.L2367:
	mov	r2, #0
	mov	r0, r2
.L2366:
	ldr	ip, [r1, r2]
	add	r0, r0, #1
	mvn	ip, ip
	str	ip, [r1, r2]
	ldr	ip, [sp, #24]
	add	r2, r2, #4
	cmp	r0, ip
	blt	.L2366
	subs	r3, r3, #1
	add	r1, r1, lr
	bne	.L2367
	str	r3, [sp, #16]
	str	sl, [sp, #60]
	b	.L2368
.L2372:
	mov	lr, #1
	mov	r0, #0
	mov	ip, lr, asl r2
	mov	lr, r0
	mov	sl, #16
	str	r3, [sp, #4]
	str	sl, [sp, #20]
.L2370:
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
	bne	.L2370
	cmp	lr, #8
	add	r2, r2, #1
	orrhi	r1, r1, ip
	cmp	r2, #32
	ldr	r3, [sp, #4]
	bne	.L2372
	ldr	ip, [sp, #16]
	ldr	r2, [sp, #24]
	add	ip, ip, #1
	ldr	sl, [sp, #44]
	cmp	ip, r2
	str	ip, [sp, #16]
	str	r1, [sl, r3]
	add	r3, r3, #4
	bge	.L2373
.L2368:
	ldr	sl, .L2415+12
	mov	r1, #0
	mov	r2, r1
	ldr	sl, [sl, #144]
	add	ip, sl, r3
	str	ip, [sp, #56]
	str	sl, [sp, #44]
	b	.L2372
.L2373:
	ldr	r3, .L2415+12
	ldr	sl, [sp, #60]
	ldr	r1, [r3, #144]
	mov	r3, #0
	mov	r2, r3
.L2375:
	ldr	r0, [r1, r3]
	add	r3, r3, #4
	cmp	r0, #0
	addeq	r2, r2, #1
	cmp	r3, #32
	bne	.L2375
	cmp	r2, #7
	ble	.L2376
	ldr	r0, .L2415+20
	mov	r2, #1
	mov	r3, #1024
	bl	rknand_print_hex
	ldr	r0, .L2415+16
	mov	r1, #0
	bl	printk
.L2377:
	b	.L2377
.L2376:
	cmp	r4, #6
	moveq	r0, #4
	streq	r0, [sp, #16]
	beq	.L2378
	cmp	r4, #7
	moveq	lr, #10
	movne	lr, #8
	str	lr, [sp, #16]
.L2378:
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
	b	.L2381
.L2382:
	ldrb	lr, [ip], #1	@ zero_extendqisi2
	strb	lr, [r1, r3]
	add	r3, r3, #1
	uxtb	lr, r3
	cmp	lr, r5
	bcc	.L2382
	ldr	r3, [sp, #20]
	add	r2, r2, #1
	ldr	ip, [sp, #16]
	add	r0, r0, r3
	ldr	r3, [sp, #48]
	add	r1, r1, ip
	cmp	r2, r3
	bge	.L2383
.L2381:
	mov	ip, r0
	mov	r3, #0
	b	.L2382
.L2383:
	mov	r3, #255
	mov	r0, r6
	str	r3, [r8, #8]
	bl	NandcWaitFlashReady
	ldr	ip, [sp, #36]
	cmp	ip, #1
	movhi	r3, #56
	strhi	r3, [r8, #8]
	bhi	.L2385
	mov	r3, #54
	str	r3, [r8, #8]
	ldr	r3, .L2415
	mov	r0, sl
	mvn	r1, #0
	ldrb	r3, [r3, #4]	@ zero_extendqisi2
	str	r3, [r8, #4]
	mov	r3, #22
	str	r7, [r9, fp, asl #8]
	str	r3, [r8, #8]
	bl	FlashReadCmd
.L2385:
	mov	r0, r6
	add	sl, sl, #1
	bl	NandcWaitFlashReady
	uxtb	sl, sl
.L2345:
	ldr	r1, .L2415
	ldrb	r3, [r1, #3762]	@ zero_extendqisi2
	cmp	r3, sl
	bhi	.L2386
	ldr	r6, [sp, #48]
.L2344:
	ldr	r3, .L2415
	strb	r5, [r3, #1]
	strb	r6, [r3, #2]
	add	sp, sp, #68
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, sl, fp, pc}
.L2414:
	ldr	r0, .L2415+16
	bl	printk
	b	.L2364
.L2416:
	.align	2
.L2415:
	.word	.LANCHOR0
	.word	.LANCHOR1
	.word	.LANCHOR1+3008
	.word	.LANCHOR2
	.word	.LC142
	.word	.LC143
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
	bhi	.L2419
	ldmfd	sp!, {r3, lr}
	b	HynixGetReadRetryDefault
.L2419:
	cmp	r3, #49
	bne	.L2420
	ldr	r0, .L2421
	mov	r2, #64
	ldr	r1, .L2421+4
	strb	r3, [r0, #0]
	mov	r3, #4
	strb	r3, [r0, #1]
	mov	r3, #15
	strb	r3, [r0, #2]
	add	r0, r0, #4
	bl	memcpy
	ldmfd	sp!, {r3, pc}
.L2420:
	ldmfd	sp!, {r3, lr}
	b	FlashGetReadRetryDefault.part.25
.L2422:
	.align	2
.L2421:
	.word	.LANCHOR0
	.word	.LANCHOR1+2924
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
	ldr	r5, .L2474
	bl	ftl_malloc
	ldr	r6, .L2474+4
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
	ldr	r5, .L2474+8
	mov	r7, #44
.L2427:
	uxtb	r0, r4
	mov	r1, r5
	bl	FlashReadIDRaw
	cmp	r4, #0
	bne	.L2424
	ldrb	r3, [r6, #2980]	@ zero_extendqisi2
	sub	r3, r3, #1
	uxtb	r3, r3
	cmp	r3, #253
	bhi	.L2465
	ldrb	r3, [r6, #2981]	@ zero_extendqisi2
	cmp	r3, #255
	beq	.L2466
.L2424:
	ldrb	r3, [r5, #0]	@ zero_extendqisi2
	add	r4, r4, #1
	cmp	r3, #181
	streqb	r7, [r5, #0]
	cmp	r4, #4
	add	r5, r5, #8
	bne	.L2427
	ldr	r4, .L2474+4
	ldrb	r3, [r4, #2980]	@ zero_extendqisi2
	cmp	r3, #173
	beq	.L2428
	ldr	r0, [r4, #3776]
	bl	NandcSetDdrMode
.L2428:
	ldr	r5, .L2474+4
	mov	r1, #0
	mov	r2, #852
	mov	r0, r5
	bl	ftl_memset
	ldr	r3, .L2474+12
	str	r3, [r4, #3624]
	mov	r3, #0
	strb	r3, [r4, #860]
	ldrb	r3, [r4, #2981]	@ zero_extendqisi2
	cmp	r3, #161
	cmpne	r3, #241
	beq	.L2429
	cmp	r3, #218
	beq	.L2429
	cmp	r3, #209
	beq	.L2429
	cmp	r3, #220
	bne	.L2430
	ldrb	r2, [r5, #2983]	@ zero_extendqisi2
	cmp	r2, #149
	bne	.L2430
.L2429:
	ldr	r2, .L2474+4
	mov	r1, #1
	mov	r0, #16
	ldr	ip, .L2474+16
	strb	r1, [r2, #852]
	ldr	r1, .L2474
	strb	r0, [r2, #853]
	strb	r3, [ip, #3038]
	strb	r0, [r1, #148]
	ldrb	r0, [r2, #2980]	@ zero_extendqisi2
	cmp	r0, #152
	strb	r0, [ip, #3037]
	bne	.L2431
	ldrb	r0, [r2, #2984]	@ zero_extendqisi2
	sxtb	r0, r0
	cmp	r0, #0
	movge	r0, #24
	strgeb	r0, [r1, #148]
.L2431:
	ldr	r1, [r2, #3784]
	ldr	r2, .L2474+20
	cmp	r1, r2
	ldreq	r2, .L2474
	moveq	r1, #16
	streqb	r1, [r2, #148]
	cmp	r3, #218
	bne	.L2433
	ldr	r3, .L2474+16
	movw	r2, #3050
	mov	r1, #2048
	strh	r1, [r3, r2]	@ movhi
	mvn	r2, #37
	b	.L2471
.L2433:
	cmp	r3, #220
	bne	.L2434
	ldr	r3, .L2474+16
	movw	r2, #3050
	mov	r1, #4096
	strh	r1, [r3, r2]	@ movhi
	mvn	r2, #35
.L2471:
	strb	r2, [r3, #3038]
.L2434:
	ldr	r1, .L2474+24
	mov	r2, #32
	ldr	r0, .L2474+28
	bl	memcpy
	ldr	r0, .L2474+12
	mov	r2, #32
	add	r1, r0, #144
	bl	memcpy
.L2430:
	ldr	r4, .L2474+4
	ldrb	r3, [r4, #852]	@ zero_extendqisi2
	cmp	r3, #0
	bne	.L2435
	bl	FlashLoadPhyInfoInRam
	cmp	r0, #0
	bne	.L2436
	ldr	r3, [r4, #3624]
	ldrh	r0, [r3, #16]
	ubfx	r0, r0, #8, #3
	strb	r0, [r4, #3772]
	tst	r0, #1
	bne	.L2436
	mov	r3, #1
	strb	r3, [r4, #3773]
	bl	FlashSetInterfaceMode
	ldrb	r0, [r4, #3772]	@ zero_extendqisi2
	bl	NandcSetMode
.L2436:
	ldr	r4, .L2474+4
	ldr	r3, [r4, #3624]
	ldrb	r3, [r3, #26]	@ zero_extendqisi2
	strb	r3, [r4, #928]
	bl	FlashLoadPhyInfo
	cmp	r0, #0
	beq	.L2435
	ldr	r3, [r4, #3776]
	cmp	r3, #0
	beq	.L2438
	mov	r0, #1
	bl	FlashSetInterfaceMode
	mov	r0, #1
	b	.L2472
.L2438:
	ldrb	r0, [r4, #3772]	@ zero_extendqisi2
	bl	FlashSetInterfaceMode
	ldrb	r0, [r4, #3772]	@ zero_extendqisi2
.L2472:
	bl	NandcSetMode
	bl	FlashLoadPhyInfo
	cmp	r0, #0
	beq	.L2435
	ldr	r4, .L2474+4
	mov	r0, #1
	bl	FlashSetInterfaceMode
	mov	r0, #1
	bl	NandcSetMode
	ldr	r0, .L2474+32
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
	ldr	r1, .L2474+36
	ldr	r0, .L2474
	ldr	r3, [r4, #3624]
	ldrh	r1, [r0, r1]
	ldrb	r2, [r3, #9]	@ zero_extendqisi2
	add	r1, r1, #4080
	add	r1, r1, #15
	cmp	r2, r1, lsr #12
	blt	.L2441
	ldrh	r1, [r3, #14]
	add	r1, r1, #255
	cmp	r2, r1, lsr #8
	bge	.L2442
.L2441:
	ldrh	r2, [r3, #14]
	bic	r2, r2, #255
	strh	r2, [r3, #14]	@ movhi
.L2442:
	ldr	r3, .L2474+4
	ldrb	r3, [r3, #3772]	@ zero_extendqisi2
	tst	r3, #6
	beq	.L2443
	bl	FlashSavePhyInfo
	ldr	r3, .L2474
	mov	r0, #0
	ldr	r1, [r3, #156]
	bl	FlashDdrParaScan
.L2443:
	bl	FlashSavePhyInfo
.L2435:
	ldr	r4, .L2474+4
	ldr	r5, .L2474
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
	beq	.L2444
	ldrb	r0, [r3, #19]	@ zero_extendqisi2
	ldrb	r3, [r4, #1]	@ zero_extendqisi2
	strb	r0, [r4, #3761]
	strb	r3, [r4, #3760]
	ldrb	r3, [r4, #2]	@ zero_extendqisi2
	strb	r3, [r5, #136]
	sub	r3, r0, #1
	uxtb	r3, r3
	cmp	r3, #6
	bhi	.L2445
	ldr	r3, .L2474+40
	str	r3, [r5, #168]
	sub	r3, r0, #5
	uxtb	r3, r3
	cmp	r3, #1
	movls	r3, #1
	strls	r3, [r4, #3824]
	bls	.L2447
	cmp	r0, #7
	addeq	r4, r4, #28
	beq	.L2448
.L2447:
	ldr	r4, .L2474+44
.L2448:
	mov	r3, #0
	mov	r2, r3
.L2450:
	ldrsb	r1, [r4, r2]
	add	r2, r2, #1
	cmp	r1, #0
	addeq	r3, r3, #1
	cmp	r2, #32
	bne	.L2450
	cmp	r3, #27
	bls	.L2444
	bl	FlashGetReadRetryDefault
	bl	FlashSavePhyInfo
	b	.L2444
.L2445:
	sub	r3, r0, #17
	uxtb	r3, r3
	cmp	r3, #2
	bhi	.L2451
	ldr	r3, .L2474+48
	cmp	r0, #19
	str	r3, [r5, #168]
	mov	r3, #7
	strb	r3, [r5, #136]
	moveq	r3, #15
	streqb	r3, [r5, #136]
	b	.L2444
.L2451:
	cmp	r0, #33
	cmpne	r0, #65
	beq	.L2452
	cmp	r0, #66
	bne	.L2453
.L2452:
	ldr	r3, .L2474
	mov	r1, #4
	ldr	r2, .L2474+52
	str	r2, [r3, #168]
	ldr	r2, .L2474+4
	strb	r1, [r2, #3760]
	mov	r2, #7
	strb	r2, [r3, #136]
	b	.L2444
.L2453:
	cmp	r0, #67
	cmpne	r0, #34
	beq	.L2454
	cmp	r0, #35
	beq	.L2454
	cmp	r0, #68
	bne	.L2455
.L2454:
	ldr	r3, .L2474
	cmp	r0, #35
	cmpne	r0, #68
	ldr	r2, .L2474+52
	sub	r0, r0, #67
	uxtb	r0, r0
	str	r2, [r3, #168]
	mov	r2, #7
	strb	r2, [r3, #136]
	moveq	r2, #17
	streqb	r2, [r3, #136]
	cmp	r0, #1
	ldr	r3, .L2474+4
	movls	r2, #4
	movhi	r2, #5
	strb	r2, [r3, #3760]
	b	.L2444
.L2455:
	cmp	r0, #49
	ldreq	r3, .L2474+56
	streq	r3, [r5, #168]
.L2444:
	ldr	r2, .L2474+4
	ldr	r3, .L2474+20
	ldr	r1, [r2, #3784]
	cmp	r1, r3
	bne	.L2458
	ldrb	r3, [r2, #928]	@ zero_extendqisi2
	cmp	r3, #0
	ldrne	r3, [r2, #3624]
	movne	r1, #0
	strneb	r1, [r3, #18]
.L2458:
	ldrb	r2, [r2, #2980]	@ zero_extendqisi2
	ldr	r3, .L2474+4
	cmp	r2, #44
	bne	.L2459
	ldrb	r2, [r3, #3773]	@ zero_extendqisi2
	cmp	r2, #0
	beq	.L2459
	ldr	r1, [r3, #3784]
	ldr	r2, .L2474+20
	cmp	r1, r2
	bne	.L2460
	ldrb	r3, [r3, #928]	@ zero_extendqisi2
	cmp	r3, #0
	bne	.L2459
.L2460:
	ldr	r3, .L2474+4
	mov	r2, #0
	mov	r0, #1
	strb	r2, [r3, #3773]
	bl	FlashSetInterfaceMode
	mov	r0, #1
	bl	NandcSetMode
.L2459:
	mov	r0, #0
	bl	flash_enter_slc_mode
	ldr	r2, .L2474+4
	ldrb	r3, [r2, #3772]	@ zero_extendqisi2
	tst	r3, #6
	beq	.L2461
	ldrb	r2, [r2, #3773]	@ zero_extendqisi2
	cmp	r2, #0
	bne	.L2462
	tst	r3, #1
	bne	.L2461
.L2462:
	ldr	r3, .L2474
	mov	r0, #0
	ldr	r1, [r3, #156]
	bl	FlashDdrParaScan
.L2461:
	ldr	r4, .L2474+4
	mov	r0, #0
	bl	flash_exit_slc_mode
	mov	r8, #16
	movw	r6, #3062
	ldr	r3, [r4, #3624]
	ldrb	r0, [r3, #20]	@ zero_extendqisi2
	bl	FlashBchSel
	add	r0, r4, #932
	bl	FlashReadIdbDataRaw
	ldr	r0, .L2474+60
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
	bne	.L2464
	mov	r7, r7, lsr #1
	mov	ip, ip, asl #1
	mov	r1, r1, asl #1
	strb	r8, [r4, #853]
	strh	r1, [r4, r2]	@ movhi
	mov	r2, #8
	strh	r7, [r4, r6]	@ movhi
	strh	ip, [r4, r0]	@ movhi
	strh	r2, [r4, r3]	@ movhi
.L2464:
	ldrb	r0, [r5, #20]	@ zero_extendqisi2
	bl	FlashBchSel
	bl	ftl_flash_suspend
	mov	r0, #0
	ldmfd	sp!, {r4, r5, r6, r7, r8, pc}
.L2465:
	mvn	r0, #1
	ldmfd	sp!, {r4, r5, r6, r7, r8, pc}
.L2466:
	mvn	r0, #1
	ldmfd	sp!, {r4, r5, r6, r7, r8, pc}
.L2475:
	.align	2
.L2474:
	.word	.LANCHOR2
	.word	.LANCHOR0
	.word	.LANCHOR0+2980
	.word	.LANCHOR1+2892
	.word	.LANCHOR1
	.word	1446522928
	.word	.LANCHOR1+2796
	.word	.LANCHOR0+896
	.word	.LC144
	.word	-898
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
	stmfd	sp!, {r3, r4, r5, lr}
	.save {r3, r4, r5, lr}
	mov	r0, #2048
	bl	ftl_malloc
	ldr	r4, .L2480
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
	beq	.L2477
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
	bne	.L2478
	bl	FtlInit
.L2478:
	ldr	r0, .L2480+4
	mov	r1, r4
	bl	printk
.L2477:
	mov	r0, r4
	ldmfd	sp!, {r3, r4, r5, pc}
.L2481:
	.align	2
.L2480:
	.word	.LANCHOR2
	.word	.LC145
	.fnend
	.size	rk_ftl_init, .-rk_ftl_init
	.align	2
	.global	FtlReInitForSDUpdata
	.type	FtlReInitForSDUpdata, %function
FtlReInitForSDUpdata:
	.fnstart
	@ args = 0, pretend = 0, frame = 16
	@ frame_needed = 0, uses_anonymous_args = 0
	ldr	r3, .L2507
	stmfd	sp!, {r4, r5, lr}
	.save {r4, r5, lr}
	.pad #20
	sub	sp, sp, #20
	ldrb	r4, [r3, #928]	@ zero_extendqisi2
	cmp	r4, #0
	movne	r0, #0
	bne	.L2483
	ldr	r5, .L2507+4
	ldr	r0, [r5, #128]
	bl	FlashInit
	cmp	r0, #0
	movne	r0, r4
	bne	.L2483
	bl	FlashLoadFactorBbt
	cmp	r0, #0
	beq	.L2484
	bl	FlashMakeFactorBbt
.L2484:
	ldr	r0, [r5, #132]
	bl	FlashReadIdbDataRaw
	cmp	r0, #0
	beq	.L2485
	mov	r1, #0
	mov	r2, #16
	mov	r0, sp
	bl	FlashReadFacBbtData
	ldr	r1, [sp, #0]
	mov	r3, #0
	mov	r0, #1
	mov	r2, r3
.L2487:
	ands	ip, r1, r0, asl r2
	add	r2, r2, #1
	addne	r3, r3, #1
	cmp	r2, #16
	bne	.L2487
	cmp	r3, #6
	ldrls	r3, .L2507
	bls	.L2503
	mov	r2, #0
	mov	r0, #1
.L2488:
	ands	ip, r1, r0, asl r2
	add	r2, r2, #1
	addne	r3, r3, #1
	cmp	r2, #24
	bne	.L2488
	cmp	r3, #17
	ldr	r3, .L2507
	movhi	r2, #36
.L2503:
	strb	r2, [r3, #853]
	movw	r2, #3074
	ldr	r3, .L2507
	ldrb	r1, [r3, #853]	@ zero_extendqisi2
	strh	r1, [r3, r2]	@ movhi
.L2485:
	ldr	r1, .L2507+8
	mov	r4, #1
	ldr	r0, .L2507+12
	bl	printk
	ldr	r0, .L2507+16
	bl	FtlConstantsInit
	bl	FtlVariablesInit
	ldr	r3, .L2507
	ldr	r0, [r3, #3848]
	uxth	r0, r0
	bl	FtlFreeSysBlkQueueInit
	b	.L2492
.L2494:
	add	r4, r4, #1
.L2492:
	bl	FtlLoadBbt
	cmp	r0, #0
	bne	.L2505
.L2493:
	bl	FtlSysBlkInit
	cmp	r0, #0
	beq	.L2495
.L2505:
	bl	FtlLowFormat
	cmp	r4, #3
	bls	.L2494
	b	.L2506
.L2495:
	ldr	r3, .L2507+20
	mov	r2, #1
	str	r2, [r3, #2996]
	b	.L2483
.L2506:
	mvn	r0, #0
.L2483:
	add	sp, sp, #20
	ldmfd	sp!, {r4, r5, pc}
.L2508:
	.align	2
.L2507:
	.word	.LANCHOR0
	.word	.LANCHOR2
	.word	.LC79
	.word	.LC78
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
	ldr	r3, .L2511
	ldr	r2, [r3, #3784]
	ldr	r3, .L2511+4
	cmp	r2, r3
	bxne	lr
	b	flash_enter_slc_mode
.L2512:
	.align	2
.L2511:
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
	ldr	r3, .L2515
	ldr	r2, [r3, #3784]
	ldr	r3, .L2515+4
	cmp	r2, r3
	bxne	lr
	b	flash_exit_slc_mode
.L2516:
	.align	2
.L2515:
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
	ldr	r5, .L2521
	mov	fp, r1
	.pad #28
	sub	sp, sp, #28
	mov	r1, r4
	mov	sl, r2
	ldr	r0, .L2521+4
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
	b	.L2518
.L2520:
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
	beq	.L2519
	ldr	r0, [r5, #3784]
	ldr	lr, .L2521+8
	cmp	r0, lr
	moveq	r3, r1
.L2519:
	ldr	r1, [sp, #16]
	add	r2, r2, r1
	mla	r2, r7, r3, r2
	ldr	r3, [r9, #3624]
	ldrb	r1, [r3, #9]	@ zero_extendqisi2
	ldrb	r3, [r9, #3832]	@ zero_extendqisi2
	str	r3, [sp, #20]
	ldr	r3, .L2521+12
	ldrb	r0, [r3, #148]	@ zero_extendqisi2
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
.L2518:
	cmp	r6, fp
	bcc	.L2520
	mov	r1, r4
	mov	r2, fp
	mov	r3, #0
	ldr	r0, .L2521+16
	bl	printk
	mov	r0, #0
	add	sp, sp, #28
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, sl, fp, pc}
.L2522:
	.align	2
.L2521:
	.word	.LANCHOR0
	.word	.LC146
	.word	1446522928
	.word	.LANCHOR2
	.word	.LC147
	.fnend
	.size	IdBlockReadData, .-IdBlockReadData
	.section	.text.unlikely
	.align	2
	.type	IDBlockWriteData, %function
IDBlockWriteData:
	.fnstart
	@ args = 0, pretend = 0, frame = 80
	@ frame_needed = 0, uses_anonymous_args = 0
	ldr	r3, .L2530
	stmfd	sp!, {r4, r5, r6, r7, r8, r9, sl, fp, lr}
	.save {r4, r5, r6, r7, r8, r9, sl, fp, lr}
	mov	r5, r0
	ldr	r3, [r3, #3624]
	mov	sl, r1
	.pad #84
	sub	sp, sp, #84
	mov	r1, r5
	mov	r9, r2
	ldr	r0, .L2530+4
	ldrb	r6, [r3, #9]	@ zero_extendqisi2
	mov	r2, sl
	ldrh	r4, [r3, #10]
	bl	printk
	mov	r0, #0
	bl	flash_boot_enter_slc_mode
	mov	r1, r6
	mov	r0, r5
	bl	__aeabi_uidiv
	mul	r4, r4, r6
	uxth	r4, r4
	mov	r1, r0
	mov	r0, #0
	mov	r2, r0
	bl	FlashEraseBlock
	cmp	sl, r4
	bls	.L2524
	mov	r0, #0
	add	r1, r4, r5
	mov	r2, r0
	bl	FlashEraseBlock
.L2524:
	mov	r0, #0
	mov	r8, #0
	bl	flash_boot_exit_slc_mode
	mov	r1, r4
	mov	r0, r5
	ldr	r4, .L2530
	bl	__aeabi_uidivmod
	mov	fp, r4
	mov	ip, r1
	rsb	r1, r1, r5
	mov	r7, ip
	str	r1, [sp, #8]
	mov	ip, r5
	b	.L2525
.L2529:
	add	r3, r8, r7
	ubfx	r3, r3, #2, #16
	cmp	r3, #0
	beq	.L2526
	add	r1, r3, #1
	ldrb	r0, [r4, #928]	@ zero_extendqisi2
	add	r2, r4, r1, asl #1
	cmp	r0, #0
	add	r2, r2, #3072
	add	r2, r2, #4
	ldrh	r2, [r2, #0]
	beq	.L2527
	ldr	r0, [r4, #3784]
	ldr	lr, .L2530+8
	cmp	r0, lr
	moveq	r2, r1
.L2527:
	sub	r2, r2, #-1073741823
	mov	r2, r2, asl #2
	str	r2, [sp, #16]
.L2526:
	movw	r2, #61424
	str	r2, [sp, #20]
	add	r2, r4, r3, asl #1
	add	r2, r2, #3072
	ldrh	r5, [r2, #4]
	ldrb	r2, [r4, #928]	@ zero_extendqisi2
	cmp	r2, #0
	beq	.L2528
	ldr	r2, [r4, #3784]
	ldr	r1, .L2530+8
	cmp	r2, r1
	moveq	r5, r3
.L2528:
	ldr	r2, [sp, #8]
	add	r8, r8, #4
	ldrb	lr, [fp, #3832]	@ zero_extendqisi2
	uxth	r8, r8
	mla	r3, r6, r5, r2
	ldr	r2, .L2530+12
	str	lr, [sp, #12]
	ldrb	r0, [r2, #148]	@ zero_extendqisi2
	str	ip, [sp, #0]
	str	r3, [sp, #4]
	bl	FlashBchSel
	mov	r0, #0
	bl	flash_boot_enter_slc_mode
	ldr	r2, [fp, #3624]
	ldr	r3, [sp, #4]
	ldrb	r1, [r2, #9]	@ zero_extendqisi2
	mov	r0, r3
	bl	__aeabi_uidiv
	mov	r2, r9
	add	r3, sp, #16
	add	r9, r9, #2048
	mov	r1, r0
	mov	r0, #0
	bl	FlashProgPage
	mov	r0, #0
	bl	flash_boot_exit_slc_mode
	ldr	r0, [sp, #12]
	bl	FlashBchSel
	ldr	r0, [sp, #8]
	mov	r1, r6
	bl	__aeabi_uidiv
	add	r2, r5, #1
	uxth	r2, r2
	mov	r1, r0
	mov	r0, #0
	bl	FlashPageProgMsbFFData
	ldr	ip, [sp, #0]
.L2525:
	cmp	r8, sl
	bcc	.L2529
	mov	r1, ip
	mov	r2, sl
	mov	r3, #0
	ldr	r0, .L2530+16
	mov	r5, ip
	bl	printk
	mov	r0, #0
	add	sp, sp, #84
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, sl, fp, pc}
.L2531:
	.align	2
.L2530:
	.word	.LANCHOR0
	.word	.LC148
	.word	1446522928
	.word	.LANCHOR2
	.word	.LC149
	.fnend
	.size	IDBlockWriteData, .-IDBlockWriteData
	.text
	.align	2
	.global	write_idblock
	.type	write_idblock, %function
write_idblock:
	.fnstart
	@ args = 0, pretend = 0, frame = 16
	@ frame_needed = 0, uses_anonymous_args = 0
	ldr	r3, .L2552
	stmfd	sp!, {r4, r5, r6, r7, r8, r9, sl, fp, lr}
	.save {r4, r5, r6, r7, r8, r9, sl, fp, lr}
	mov	r8, r0
	ldr	r3, [r3, #3624]
	.pad #28
	sub	sp, sp, #28
	mov	r0, #256000
	mov	r5, r1
	mov	r6, r2
	ldrb	r4, [r3, #9]	@ zero_extendqisi2
	ldrh	r9, [r3, #10]
	bl	ftl_malloc
	subs	sl, r0, #0
	mvneq	r0, #0
	beq	.L2533
	mul	r9, r9, r4
	add	r8, r8, #508
	add	r8, r8, #3
	mov	r8, r8, lsr #9
	cmp	r8, #8
	movcc	r8, #8
	uxth	r9, r9
	mov	r0, r8
	mov	r1, r9
	bl	__aeabi_uidiv
	mov	r3, #0
	movw	r2, #63999
	movw	ip, #4097
	str	r0, [sp, #20]
	add	r0, r5, #254976
	add	r0, r0, #1020
.L2538:
	ldr	r1, [r0], #-4
	cmp	r1, #0
	bne	.L2534
	ldr	r1, [r5, r3, asl #2]
	add	r3, r3, #1
	cmp	r3, ip
	sub	r2, r2, #1
	movcs	r3, #0
	cmp	r2, #4096
	str	r1, [r0, #4]
	bne	.L2538
	b	.L2537
.L2534:
	ldr	r0, .L2552+4
	bl	printk
.L2537:
	ldr	r4, .L2552
	mov	r3, #5
	ldr	r0, .L2552+8
	mov	r1, r6
	mov	r2, #4
	mov	r7, r5
	bl	rknand_print_hex
	ldrb	r2, [r4, #853]	@ zero_extendqisi2
	ldr	r1, [r5, #512]
	ldr	r0, .L2552+12
	bl	printk
	ldr	r2, .L2552+16
	movw	r3, #3074
	mov	r1, r8
	ldrh	r3, [r4, r3]
	ldr	r2, [r2, #152]
	ldr	r0, .L2552+20
	str	r2, [sp, #0]
	mov	r2, r8
	bl	printk
	ldrb	r3, [r4, #853]	@ zero_extendqisi2
	ldr	r2, [r5, #512]
	mov	r4, r6
	mov	r1, r8, asl #7
	str	r1, [sp, #16]
	cmp	r2, r3
	strhi	r3, [r5, #512]
	mov	r3, #0
	str	r3, [sp, #8]
	str	r3, [sp, #12]
.L2545:
	ldr	r1, .L2552
	ldr	r3, [r4], #4
	ldrb	r2, [r1, #853]	@ zero_extendqisi2
	cmp	r3, r2
	bcs	.L2540
	ldr	r1, .L2552+16
	ldr	r2, [r1, #152]
	cmp	r3, r2
	bcc	.L2540
	mov	r0, sl
	mov	r1, #512
	bl	__memzero
	ldr	r0, [r4, #-4]
	mov	r1, r8
	mov	r2, r7
	mov	r6, #0
	mul	r0, r0, r9
	bl	IDBlockWriteData
	ldr	r0, [r4, #-4]
	mov	r2, sl
	mov	r1, r8
	mul	r0, r0, r9
	bl	IdBlockReadData
	mov	r2, sl
	mov	r3, r7
.L2544:
	mov	fp, r2
	mov	r5, r3
	ldr	r0, [fp, #0]
	add	r2, r2, #4
	ldr	r1, [r5, #0]
	add	r3, r3, #4
	cmp	r0, r1
	beq	.L2541
	mov	r0, sl
	mov	r1, #512
	bl	__memzero
	ldr	r3, [fp, #0]
	ldr	r1, [sp, #12]
	ldr	r0, .L2552+24
	str	r3, [sp, #0]
	ldr	r3, [r5, #0]
	bic	r5, r6, #255
	mov	r5, r5, asl #2
	str	r3, [sp, #4]
	mov	r3, r6
	ldr	r2, [r4, #-4]
	bl	printk
	ldr	r0, .L2552+28
	add	r1, r7, r5
	mov	r2, #4
	mov	r3, #256
	bl	rknand_print_hex
	mov	r3, #256
	add	r1, sl, r5
	mov	r2, #4
	ldr	r0, .L2552+32
	bl	rknand_print_hex
	mov	r0, #0
	bl	flash_boot_enter_slc_mode
	ldr	r1, [r4, #-4]
	mov	r0, #0
	mov	r2, r0
	mul	r1, r1, r9
	bl	FlashEraseBlock
	ldr	r3, [sp, #20]
	cmp	r3, #1
	bls	.L2542
	ldr	r1, [r4, #-4]
	mov	r0, #0
	mov	r2, r0
	add	r1, r1, #1
	mul	r1, r9, r1
	bl	FlashEraseBlock
.L2542:
	mov	r0, #0
	bl	flash_boot_exit_slc_mode
	ldr	r1, [r4, #-4]
	ldr	r0, .L2552+36
	bl	printk
	ldr	r1, [sp, #16]
	cmp	r6, r1
	bcc	.L2540
	b	.L2543
.L2541:
	ldr	r1, [sp, #16]
	add	r6, r6, #1
	cmp	r6, r1
	bne	.L2544
.L2543:
	ldr	r3, [sp, #8]
	add	r3, r3, #1
	str	r3, [sp, #8]
.L2540:
	ldr	r1, [sp, #12]
	add	r1, r1, #1
	str	r1, [sp, #12]
	cmp	r1, #5
	bne	.L2545
	mov	r0, sl
	bl	ftl_free
	ldr	r3, [sp, #8]
	cmp	r3, #0
	mvneq	r0, #0
	movne	r0, #0
.L2533:
	add	sp, sp, #28
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, sl, fp, pc}
.L2553:
	.align	2
.L2552:
	.word	.LANCHOR0
	.word	.LC150
	.word	.LC151
	.word	.LC152
	.word	.LANCHOR2
	.word	.LC153
	.word	.LC154
	.word	.LC155
	.word	.LC156
	.word	.LC157
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
	ldr	ip, .L2557
	b	.L2555
.L2556:
	ldrb	r4, [r3, r2]	@ zero_extendqisi2
	add	r2, r2, #1
	eor	r4, r4, r0, lsr #24
	add	r4, ip, r4, asl #2
	ldr	r4, [r4, #3068]
	eor	r0, r4, r0, asl #8
.L2555:
	cmp	r2, r1
	bne	.L2556
	ldmfd	sp!, {r4, pc}
.L2558:
	.align	2
.L2557:
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
	ldr	r3, .L2675
	stmfd	sp!, {r4, r5, r6, lr}
	.save {r4, r5, r6, lr}
	cmp	r1, r3
	.pad #528
	sub	sp, sp, #528
	mov	r5, r1
	mov	r4, r2
	beq	.L2568
	bhi	.L2577
	ldr	r3, .L2675+4
	cmp	r1, r3
	beq	.L2565
	bhi	.L2578
	sub	r3, r3, #125
	cmp	r1, r3
	beq	.L2562
	bhi	.L2579
	sub	r3, r3, #237
	cmp	r1, r3
	bne	.L2621
	b	.L2671
.L2579:
	ldr	r3, .L2675+8
	cmp	r1, r3
	beq	.L2563
	add	r3, r3, #1
	cmp	r1, r3
	bne	.L2621
	b	.L2672
.L2578:
	ldr	r3, .L2675+12
	cmp	r1, r3
	beq	.L2567
	bhi	.L2580
	ldr	r3, .L2675+16
	cmp	r1, r3
	bne	.L2621
	b	.L2673
.L2580:
	ldr	r3, .L2675+20
	cmp	r1, r3
	beq	.L2567
	add	r3, r3, #10
	cmp	r1, r3
	bne	.L2621
	b	.L2567
.L2577:
	ldr	r3, .L2675+24
	cmp	r1, r3
	beq	.L2573
	bhi	.L2581
	sub	r3, r3, #78
	cmp	r1, r3
	beq	.L2570
	bcc	.L2569
	add	r3, r3, #21
	cmp	r1, r3
	beq	.L2571
	add	r3, r3, #56
	cmp	r1, r3
	bne	.L2621
	b	.L2674
.L2581:
	ldr	r3, .L2675+28
	cmp	r1, r3
	beq	.L2576
	bhi	.L2582
	sub	r3, r3, #956
	sub	r3, r3, #1
	cmp	r1, r3
	beq	.L2574
	add	r3, r3, #956
	cmp	r1, r3
	bne	.L2621
	b	.L2575
.L2582:
	ldr	r3, .L2675+32
	cmp	r1, r3
	beq	.L2575
	add	r3, r3, #1
	cmp	r1, r3
	bne	.L2621
	b	.L2576
.L2569:
	ldr	r0, .L2675+36
	bl	printk
	mov	r0, #4096
	bl	ftl_malloc
	subs	r5, r0, #0
	beq	.L2628
	mov	r1, r4
	mov	r2, #512
	bl	rk_copy_from_user
	cmp	r0, #0
	bne	.L2670
.L2583:
	ldr	r0, .L2675+40
	ldmia	r5, {r1, r2}
	bl	printk
	ldr	r3, [r5, #4]
	cmp	r3, #8
	str	r3, [sp, #4]
	bhi	.L2592
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
	beq	.L2656
	ldr	r0, .L2675+44
.L2669:
	bl	printk
	b	.L2592
.L2570:
	ldr	r0, .L2675+48
	bl	printk
	mov	r0, #4096
	bl	ftl_malloc
	subs	r5, r0, #0
	beq	.L2628
	mov	r1, r4
	mov	r2, #4096
	bl	rk_copy_from_user
	cmp	r0, #0
	bne	.L2670
.L2587:
	ldr	r4, .L2675+52
	ldr	r0, .L2675+56
	ldmia	r5, {r1, r2}
	bl	printk
	ldr	r3, [r4, #464]
	cmp	r3, #0
	bne	.L2588
	mov	r0, #260096
	bl	ftl_malloc
	cmp	r0, #0
	str	r0, [r4, #464]
	beq	.L2592
.L2589:
	mov	r1, #260096
	bl	__memzero
.L2588:
	ldr	r2, [r5, #4]
	movw	r3, #4088
	cmp	r2, r3
	bhi	.L2592
	ldr	r3, [r5, #0]
	cmp	r3, #251904
	bhi	.L2592
	ldr	r1, .L2675+52
	ldr	r0, [r1, #464]
	add	r1, r5, #8
	add	r0, r0, r3
	bl	memcpy
.L2656:
	mov	r0, r5
	bl	ftl_free
.L2657:
	mov	r4, #0
	b	.L2586
.L2674:
	ldr	r0, .L2675+60
	bl	printk
	mov	r0, #4096
	bl	ftl_malloc
	subs	r5, r0, #0
	beq	.L2628
	mov	r1, r4
	mov	r2, #28
	bl	rk_copy_from_user
	cmp	r0, #0
	beq	.L2591
.L2670:
	ldr	r0, .L2675+64
	b	.L2669
.L2591:
	ldmia	r5, {r1, r2}
	ldr	r0, .L2675+68
	bl	printk
	ldr	r1, [r5, #0]
	cmp	r1, #256000
	bhi	.L2592
	ldr	r4, .L2675+52
	ldr	r0, [r4, #464]
	cmp	r0, #0
	beq	.L2592
.L2593:
	bl	CRC_32
	ldr	r3, [r5, #4]
	cmp	r3, r0
	beq	.L2594
	mov	r0, r5
	bl	ftl_free
	b	.L2634
.L2594:
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
	b	.L2586
.L2592:
	mov	r0, r5
	b	.L2660
.L2571:
	ldr	r0, .L2675+72
	bl	printk
	mov	r0, #4096
	bl	ftl_malloc
	subs	r5, r0, #0
	beq	.L2628
	bl	ftl_read_flash_info
	mov	r0, r4
	mov	r1, r5
	mov	r2, #11
	b	.L2665
.L2568:
	ldr	r0, .L2675+76
	bl	printk
	bl	nand_blk_add_whole_disk
	mov	r0, #4096
	bl	ftl_malloc
	subs	r5, r0, #0
	beq	.L2628
	bl	rknand_device_lock
	mov	r1, #0
	mov	r2, #64
	mov	r0, r5
	bl	FlashReadFacBbtData
	bl	rknand_device_unlock
	ldr	r0, .L2675+80
	mov	r1, r5
	mov	r2, #4
	mov	r3, #8
	bl	rknand_print_hex
	mov	r0, r4
	mov	r1, r5
	mov	r2, #64
	b	.L2665
.L2573:
	ldr	r0, .L2675+84
	bl	printk
	ldr	r3, .L2675+52
	add	r1, sp, #528
	mov	r0, r4
	mov	r2, #4
	ldr	r3, [r3, #468]
	ldr	r3, [r3, #20]
	str	r3, [r1, #-524]!
	b	.L2661
.L2574:
	ldr	r0, .L2675+88
	bl	printk
	mov	r0, #4096
	bl	ftl_malloc
	subs	r5, r0, #0
	beq	.L2628
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
.L2665:
	bl	rk_copy_to_user
	subs	r4, r0, #0
	mov	r0, r5
	beq	.L2597
.L2660:
	bl	ftl_free
	b	.L2651
.L2597:
	bl	ftl_free
	b	.L2586
.L2562:
	ldr	r0, .L2675+92
	bl	printk
	add	r0, sp, #8
	mov	r1, r4
	mov	r2, #520
	bl	rk_copy_from_user
	subs	r5, r0, #0
	bne	.L2662
.L2598:
	ldr	r2, [sp, #8]
	ldr	r3, .L2675+96
	cmp	r2, r3
	bne	.L2653
	ldr	r3, [sp, #12]
	cmp	r3, #512
	bhi	.L2653
	ldr	r6, .L2675+52
	mov	r2, #512
	add	r0, sp, #8
	ldr	r1, [r6, #468]
	bl	memcpy
	ldr	r2, [r6, #472]
	ldr	r3, .L2675+100
	cmp	r2, r3
	beq	.L2599
	add	r0, sp, #72
	mov	r1, #128
	str	r5, [sp, #16]
	str	r5, [sp, #20]
	bl	__memzero
.L2599:
	add	r0, sp, #264
	mov	r1, #256
	mov	r3, #0
	str	r3, [sp, #24]
	bl	__memzero
	b	.L2668
.L2563:
	ldr	r0, .L2675+104
	bl	printk
	add	r0, sp, #8
	mov	r1, r4
	mov	r2, #520
	bl	rk_copy_from_user
	cmp	r0, #0
	bne	.L2662
.L2600:
	ldr	r2, [sp, #8]
	ldr	r3, .L2675+96
	cmp	r2, r3
	bne	.L2653
	ldr	r3, [sp, #12]
	cmp	r3, #512
	bhi	.L2653
	ldr	r4, .L2675+52
	ldr	r3, .L2675+100
	ldr	r2, [r4, #472]
	cmp	r2, r3
	bne	.L2634
	ldr	r3, [sp, #20]
	sub	r2, r3, #1
	cmp	r2, #127
	mvnhi	r4, #2
	bhi	.L2560
	ldr	r0, [r4, #468]
	add	r1, sp, #72
	str	r3, [r0, #12]
	add	r0, r0, #64
	ldr	r2, [sp, #20]
	bl	memcpy
	mov	r0, #1
	ldr	r1, [r4, #468]
	b	.L2658
.L2673:
	ldr	r0, .L2675+108
	bl	printk
	add	r0, sp, #8
	mov	r1, r4
	mov	r2, #520
	bl	rk_copy_from_user
	cmp	r0, #0
	bne	.L2662
.L2601:
	ldr	r2, [sp, #8]
	ldr	r3, .L2675+112
	cmp	r2, r3
	bne	.L2653
	ldr	r3, [sp, #12]
	cmp	r3, #512
	bhi	.L2653
	ldr	r5, .L2675+52
	ldr	r4, [r5, #476]
	cmp	r4, #0
	beq	.L2560
	ldr	r3, [r5, #480]
	ldr	r2, .L2675+116
	ldr	r1, [r3, #0]
	cmp	r1, r2
	beq	.L2602
	str	r2, [r3, #0]
	mov	r2, #504
	ldr	r3, [r5, #480]
	str	r2, [r3, #4]
	mov	r2, #0
	str	r2, [r3, #8]
	str	r2, [r3, #12]
.L2602:
	ldr	r1, [r5, #480]
	mov	r4, #0
	mov	r0, r4
	str	r4, [r1, #16]
	bl	StorageSysDataStore
	ldr	r3, [r5, #468]
	ldr	r2, .L2675+96
	ldr	r5, .L2675+52
	ldr	r1, [r3, #0]
	cmp	r1, r2
	strne	r2, [r3, #0]
	ldr	r0, [r5, #468]
	mov	r1, #128
	ldrne	r3, .L2675+52
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
	b	.L2586
.L2565:
	ldr	r0, .L2675+120
	bl	printk
	mov	r1, r4
	add	r0, sp, #8
	mov	r2, #520
	bl	rk_copy_from_user
	subs	r4, r0, #0
	bne	.L2662
.L2604:
	ldr	r2, [sp, #8]
	ldr	r3, .L2675+124
	cmp	r2, r3
	bne	.L2653
	ldr	r3, [sp, #12]
	cmp	r3, #512
	bhi	.L2653
	ldr	r5, .L2675+52
	ldr	r3, [r5, #476]
	cmp	r3, #1
	beq	.L2560
	ldr	r3, [r5, #480]
	mov	r0, #0
	ldr	r2, .L2675+116
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
	ldr	r2, .L2675+96
	ldr	r1, [r3, #0]
	cmp	r1, r2
	beq	.L2606
	str	r2, [r3, #0]
	mov	r1, #504
	ldr	r3, .L2675+52
	mov	r2, #0
	ldr	r3, [r3, #468]
	stmib	r3, {r1, r2}
.L2606:
	ldr	r5, .L2675+52
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
	b	.L2586
.L2672:
	ldr	r0, .L2675+128
	bl	printk
	add	r0, sp, #8
	mov	r1, r4
	mov	r2, #520
	bl	rk_copy_from_user
	cmp	r0, #0
	bne	.L2662
.L2607:
	ldr	r2, [sp, #8]
	ldr	r3, .L2675+132
	cmp	r2, r3
	bne	.L2653
	ldr	r2, [sp, #12]
	cmp	r2, #512
	addls	r0, sp, #16
	ldrls	r1, .L2675+136
	bls	.L2667
	b	.L2653
.L2567:
	ldr	r3, .L2675+20
	cmp	r5, r3
	ldreq	r0, .L2675+140
	beq	.L2654
	ldr	r3, .L2675+144
	cmp	r5, r3
	ldreq	r0, .L2675+148
	ldrne	r0, .L2675+152
.L2654:
	bl	printk
	add	r0, sp, #8
	mov	r1, r4
	mov	r2, #520
	bl	rk_copy_from_user
	cmp	r0, #0
	bne	.L2662
.L2611:
	ldr	r2, [sp, #8]
	ldr	r3, .L2675+156
	cmp	r2, r3
	bne	.L2651
	ldr	r3, .L2675+144
	ldr	r6, .L2675+52
	cmp	r5, r3
	bne	.L2612
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
	b	.L2560
.L2612:
	ldr	r3, [r6, #996]
	cmp	r3, #10
	bhi	.L2651
	ldr	r3, [r6, #468]
	ldr	r1, [sp, #12]
	ldr	r2, [r3, #24]
	cmp	r2, r1
	beq	.L2613
	cmp	r2, #0
	beq	.L2613
	ldr	r0, .L2675+160
	bl	printk
	ldr	r3, [r6, #996]
	add	r3, r3, #1
	str	r3, [r6, #996]
	b	.L2651
.L2613:
	ldr	r0, .L2675+52
	mov	r2, #0
	str	r2, [r0, #996]
	ldr	r0, .L2675+20
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
	b	.L2586
.L2575:
	ldr	r0, .L2675+164
	bl	printk
	add	r0, sp, #8
	mov	r1, r4
	mov	r2, #520
	bl	rk_copy_from_user
	cmp	r0, #0
	bne	.L2662
.L2616:
	ldr	r2, [sp, #8]
	ldr	r3, .L2675+168
	cmp	r2, r3
	bne	.L2653
	ldr	r2, [sp, #12]
	cmp	r2, #504
	bhi	.L2653
	ldr	r3, .L2675+172
	add	r0, sp, #16
	cmp	r5, r3
	ldr	r3, .L2675+52
	ldreq	r1, [r3, #1000]
	ldrne	r1, [r3, #1004]
	add	r1, r1, #8
.L2667:
	bl	memcpy
.L2668:
	add	r1, sp, #8
	mov	r0, r4
	mov	r2, #520
.L2661:
	bl	rk_copy_to_user
	subs	r4, r0, #0
	bne	.L2651
	b	.L2586
.L2576:
	ldr	r0, .L2675+176
	bl	printk
	add	r0, sp, #8
	mov	r1, r4
	mov	r2, #520
	bl	rk_copy_from_user
	cmp	r0, #0
	beq	.L2619
.L2662:
	ldr	r0, .L2675+64
	bl	printk
	b	.L2651
.L2619:
	ldr	r2, [sp, #8]
	ldr	r3, .L2675+168
	cmp	r2, r3
	bne	.L2653
	ldr	r2, [sp, #12]
	cmp	r2, #504
	bhi	.L2653
	ldr	r3, .L2675+28
	add	r2, r2, #8
	ldr	r4, .L2675+52
	cmp	r5, r3
	bne	.L2620
	add	r1, sp, #8
	ldr	r0, [r4, #1000]
	bl	memcpy
	ldr	r1, [r4, #1000]
	mov	r0, #2
.L2658:
	bl	StorageSysDataStore
	mov	r4, r0
	b	.L2586
.L2620:
	add	r1, sp, #8
	ldr	r0, [r4, #1004]
	bl	memcpy
	mov	r0, #3
	ldr	r1, [r4, #1004]
	b	.L2658
.L2671:
	bl	rknand_dev_flush
	b	.L2657
.L2653:
	mvn	r4, #0
.L2586:
	mov	r1, r4
	ldr	r0, .L2675+180
	bl	printk
	b	.L2560
.L2621:
	mvn	r4, #21
	b	.L2560
.L2628:
	mvn	r4, #11
	b	.L2560
.L2634:
	mvn	r4, #1
	b	.L2560
.L2651:
	mvn	r4, #13
.L2560:
	mov	r0, r4
	add	sp, sp, #528
	ldmfd	sp!, {r4, r5, r6, pc}
.L2676:
	.align	2
.L2675:
	.word	1074033155
	.word	1074029694
	.word	1074029570
	.word	1074031656
	.word	1074029695
	.word	1074031666
	.word	1074033235
	.word	1074034193
	.word	1074034194
	.word	.LC158
	.word	.LC160
	.word	.LC161
	.word	.LC162
	.word	.LANCHOR2
	.word	.LC163
	.word	.LC164
	.word	.LC159
	.word	.LC165
	.word	.LC166
	.word	.LC167
	.word	.LC168
	.word	.LC169
	.word	.LC170
	.word	.LC171
	.word	1263358532
	.word	-1067903959
	.word	.LC172
	.word	.LC173
	.word	1112753220
	.word	1146313043
	.word	.LC174
	.word	1112755781
	.word	.LC175
	.word	1094995539
	.word	.LANCHOR2+484
	.word	.LC176
	.word	1074031676
	.word	.LC177
	.word	.LC178
	.word	1280262987
	.word	.LC179
	.word	.LC180
	.word	1145980246
	.word	1074034192
	.word	.LC181
	.word	.LC182
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
	ldr	r4, .L2680
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
	beq	.L2678
	mov	r0, r5
	mov	r1, #508
	bl	JSHash
	cmp	r7, r0
	beq	.L2678
	str	r6, [r5, #16]
	ldr	r0, .L2680+4
	str	r6, [r4, #476]
	bl	printk
.L2678:
	ldr	r3, [r4, #476]
	mov	r0, #2
	ldr	r4, .L2680
	cmp	r3, #0
	ldrne	r2, .L2680+8
	ldrne	r3, .L2680
	ldr	r1, [r4, #1000]
	strne	r2, [r3, #472]
	bl	StorageSysDataLoad
	ldr	r1, [r4, #1004]
	mov	r0, #3
	bl	StorageSysDataLoad
	ldmfd	sp!, {r3, r4, r5, r6, r7, lr}
	b	rknand_sys_storage_init
.L2681:
	.align	2
.L2680:
	.word	.LANCHOR2
	.word	.LC183
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
	ldr	r7, .L2691
	cmp	r0, #0
	str	r0, [r7, #1008]
	beq	.L2689
	ldr	sl, .L2691+4
	mov	r6, #0
	mov	r4, r6
	mov	r5, r6
	movw	r8, #65532
.L2687:
	ldr	r0, [r7, #1008]
	mov	r1, r5, asl #7
	mov	r2, #0
	bl	rk_ftl_vendor_ops.constprop.26
	cmp	r0, #0
	bne	.L2685
	ldr	r3, [r7, #1008]
	ldr	r0, .L2691+8
	ldr	r1, [r3, #0]
	ldr	r2, [r3, r8]
	ldr	r3, [r3, #4]
	bl	printk
	ldr	r0, [r7, #1008]
	ldr	r3, [r0, #0]
	cmp	r3, sl
	bne	.L2686
	ldr	r3, [r0, r8]
	ldr	r2, [r0, #4]
	cmp	r3, r2
	bne	.L2686
	cmp	r4, r3
	movcc	r6, r5
	movcc	r4, r3
.L2686:
	add	r5, r5, #1
	cmp	r5, #2
	bne	.L2687
	cmp	r4, #0
	beq	.L2688
	mov	r1, r6, asl #7
	mov	r2, #0
	bl	rk_ftl_vendor_ops.constprop.26
	cmp	r0, #0
	ldmeqfd	sp!, {r3, r4, r5, r6, r7, r8, sl, pc}
	b	.L2685
.L2688:
	mov	r1, #65536
	bl	__memzero
	ldr	r3, .L2691
	ldr	r1, .L2691+4
	mov	r2, #1
	mov	r0, r4
	ldr	r3, [r3, #1008]
	stmia	r3, {r1, r2}
	movw	r1, #65532
	str	r2, [r3, r1]
	ldr	r2, .L2691+12
	strh	r4, [r3, #12]	@ movhi
	strh	r2, [r3, #14]	@ movhi
	ldmfd	sp!, {r3, r4, r5, r6, r7, r8, sl, pc}
.L2685:
	ldr	r4, .L2691
	ldr	r0, [r4, #1008]
	bl	kfree
	mov	r3, #0
	mvn	r0, #0
	str	r3, [r4, #1008]
	ldmfd	sp!, {r3, r4, r5, r6, r7, r8, sl, pc}
.L2689:
	mvn	r0, #11
	ldmfd	sp!, {r3, r4, r5, r6, r7, r8, sl, pc}
.L2692:
	.align	2
.L2691:
	.word	.LANCHOR2
	.word	1380668996
	.word	.LC184
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
	ldr	r1, .L2700
	stmfd	sp!, {r4, r5, r6, lr}
	.save {r4, r5, r6, lr}
	ldr	ip, [r1, #1008]
	cmp	ip, #0
	ldrneh	r4, [ip, #10]
	movne	r5, ip
	movne	r1, #0
	bne	.L2695
	b	.L2699
.L2697:
	ldrh	r6, [r5, #16]
	add	r5, r5, #8
	cmp	r6, r3
	bne	.L2696
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
.L2696:
	add	r1, r1, #1
.L2695:
	cmp	r1, r4
	bcc	.L2697
	mvn	r0, #0
	ldmfd	sp!, {r4, r5, r6, pc}
.L2699:
	mvn	r0, #0
	ldmfd	sp!, {r4, r5, r6, pc}
.L2701:
	.align	2
.L2700:
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
	ldr	r3, .L2719
	stmfd	sp!, {r4, r5, r6, r7, r8, r9, sl, fp, lr}
	.save {r4, r5, r6, r7, r8, r9, sl, fp, lr}
	mov	sl, r0
	ldr	r4, [r3, #1008]
	.pad #20
	sub	sp, sp, #20
	mov	r9, r1
	mov	r5, r2
	cmp	r4, #0
	beq	.L2715
	add	r8, r2, #63
	ldrh	r2, [r4, #8]
	ldrh	r3, [r4, #10]
	bic	r8, r8, #63
	mov	r7, #0
	str	r2, [sp, #8]
	b	.L2704
.L2711:
	add	r2, r7, #2
	mov	r2, r2, asl #3
	add	r6, r4, r2
	ldrh	r2, [r4, r2]
	cmp	r2, sl
	bne	.L2705
	ldrh	r2, [r6, #4]
	add	r2, r2, #63
	bic	r2, r2, #63
	str	r2, [sp, #12]
	cmp	r5, r2
	bls	.L2706
	ldrh	r2, [r4, #14]
	cmp	r2, r8
	bcc	.L2715
	sub	r3, r3, #1
	mov	fp, r8
	ldrh	r6, [r6, #2]
	mov	r8, r3
	b	.L2707
.L2708:
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
.L2707:
	cmp	r7, r8
	add	r2, r7, #2
	bcc	.L2708
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
	b	.L2718
.L2706:
	ldrh	r0, [r6, #2]
	mov	r1, r9
	mov	r2, r5
	add	r0, r0, #1024
	add	r0, r4, r0
	bl	memcpy
	strh	r5, [r6, #4]	@ movhi
	b	.L2718
.L2705:
	add	r7, r7, #1
.L2704:
	cmp	r7, r3
	bcc	.L2711
	ldrh	r2, [r4, #14]
	cmp	r2, r8
	bcc	.L2715
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
.L2718:
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
	bl	rk_ftl_vendor_ops.constprop.26
	mov	r0, #0
	b	.L2703
.L2715:
	mvn	r0, #0
.L2703:
	add	sp, sp, #20
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, sl, fp, pc}
.L2720:
	.align	2
.L2719:
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
	stmfd	sp!, {r4, r5, r6, lr}
	.save {r4, r5, r6, lr}
	mov	r0, #4096
	mov	r5, r2
	mov	r6, r1
	bl	ftl_malloc
	subs	r4, r0, #0
	mvneq	r5, #0
	beq	.L2722
	ldr	r3, .L2738
	cmp	r6, r3
	beq	.L2724
	add	r3, r3, #1
	cmp	r6, r3
	bne	.L2735
	b	.L2737
.L2724:
	mov	r1, r5
	mov	r2, #8
	bl	rk_copy_from_user
	cmp	r0, #0
	bne	.L2735
	ldr	r2, [r4, #0]
	ldr	r3, .L2738+4
	cmp	r2, r3
	bne	.L2734
	ldrh	r0, [r4, #4]
	add	r1, r4, #8
	ldrh	r2, [r4, #6]
	bl	rk_ftl_vendor_read
	cmn	r0, #1
	beq	.L2730
	uxth	r2, r0
	mov	r1, r4
	strh	r2, [r4, #6]	@ movhi
	mov	r0, r5
	add	r2, r2, #8
	bl	rk_copy_to_user
	cmp	r0, #0
	moveq	r5, r0
	mvnne	r5, #13
	b	.L2723
.L2737:
	mov	r1, r5
	mov	r2, #8
	bl	rk_copy_from_user
	cmp	r0, #0
	bne	.L2735
	ldr	r2, [r4, #0]
	ldr	r3, .L2738+4
	cmp	r2, r3
	bne	.L2734
	ldrh	r2, [r4, #6]
	movw	r3, #4087
	cmp	r2, r3
	bhi	.L2734
	mov	r0, r4
	mov	r1, r5
	add	r2, r2, #8
	bl	rk_copy_from_user
	cmp	r0, #0
	bne	.L2735
	ldrh	r0, [r4, #4]
	add	r1, r4, #8
	ldrh	r2, [r4, #6]
	bl	rk_ftl_vendor_write
.L2730:
	mov	r5, r0
	b	.L2723
.L2734:
	mvn	r5, #0
	b	.L2723
.L2735:
	mvn	r5, #13
.L2723:
	mov	r0, r4
	bl	kfree
.L2722:
	mov	r0, r5
	ldmfd	sp!, {r4, r5, r6, pc}
.L2739:
	.align	2
.L2738:
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
	.type	__func__.14459, %object
	.size	__func__.14459, 11
__func__.14459:
	.ascii	"FtlMemInit\000"
.LC0:
	.byte	60
	.byte	40
	.byte	24
	.byte	16
	.type	__func__.15408, %object
	.size	__func__.15408, 21
__func__.15408:
	.ascii	"FtlVpcCheckAndModify\000"
	.type	__func__.14532, %object
	.size	__func__.14532, 8
__func__.14532:
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
	.ascii	"FtlFreeSysBlkQueueOut free count = %d\012\000"
.LC6:
	.ascii	"FtlFreeSysBlkQueueOut = %x, free count = %d, error\012"
	.ascii	"\000"
.LC7:
	.ascii	"FtlFreeSysBlkQueueOut = %x, free count = %d\012\000"
.LC8:
	.ascii	"FLASH INFO:\012\000"
.LC9:
	.ascii	"FLASH ID: %x\012\000"
.LC10:
	.ascii	"Device Capacity: %d MB\012\000"
.LC11:
	.ascii	"FMWAIT: %x %x %x %x\012\000"
.LC12:
	.ascii	"FTL INFO:\012\000"
.LC13:
	.ascii	"g_MaxLpn = 0x%x\012\000"
.LC14:
	.ascii	"g_VaildLpn = 0x%x\012\000"
.LC15:
	.ascii	"read_page_count = 0x%x\012\000"
.LC16:
	.ascii	"discard_page_count = 0x%x\012\000"
.LC17:
	.ascii	"write_page_count = 0x%x\012\000"
.LC18:
	.ascii	"cache_write_count = 0x%x\012\000"
.LC19:
	.ascii	"l2p_write_count = 0x%x\012\000"
.LC20:
	.ascii	"gc_page_count = 0x%x\012\000"
.LC21:
	.ascii	"totle_write = %d MB\012\000"
.LC22:
	.ascii	"totle_read = %d MB\012\000"
.LC23:
	.ascii	"GSV = 0x%x\012\000"
.LC24:
	.ascii	"GDV = 0x%x\012\000"
.LC25:
	.ascii	"bad blk num = %d %d\012\000"
.LC26:
	.ascii	"free_superblocks = 0x%x\012\000"
.LC27:
	.ascii	"mlc_EC = 0x%x\012\000"
.LC28:
	.ascii	"slc_EC = 0x%x\012\000"
.LC29:
	.ascii	"avg_EC = 0x%x\012\000"
.LC30:
	.ascii	"sys_EC = 0x%x\012\000"
.LC31:
	.ascii	"max_EC = 0x%x\012\000"
.LC32:
	.ascii	"min_EC = 0x%x\012\000"
.LC33:
	.ascii	"PLT = 0x%x\012\000"
.LC34:
	.ascii	"POT = 0x%x\012\000"
.LC35:
	.ascii	"MaxSector = 0x%x\012\000"
.LC36:
	.ascii	"init_sys_blks_pp = 0x%x\012\000"
.LC37:
	.ascii	"sys_blks_pp = 0x%x\012\000"
.LC38:
	.ascii	"free sysblock = 0x%x\012\000"
.LC39:
	.ascii	"data_blks_pp = 0x%x\012\000"
.LC40:
	.ascii	"data_op_blks_pp = 0x%x\012\000"
.LC41:
	.ascii	"max_data_blks = 0x%x\012\000"
.LC42:
	.ascii	"Sys.id = 0x%x\012\000"
.LC43:
	.ascii	"Bbt.id = 0x%x\012\000"
.LC44:
	.ascii	"ACT.page = 0x%x\012\000"
.LC45:
	.ascii	"ACT.plane = 0x%x\012\000"
.LC46:
	.ascii	"ACT.id = 0x%x\012\000"
.LC47:
	.ascii	"ACT.mode = 0x%x\012\000"
.LC48:
	.ascii	"ACT.a_pages = 0x%x\012\000"
.LC49:
	.ascii	"ACT VPC = 0x%x\012\000"
.LC50:
	.ascii	"BUF.page = 0x%x\012\000"
.LC51:
	.ascii	"BUF.plane = 0x%x\012\000"
.LC52:
	.ascii	"BUF.id = 0x%x\012\000"
.LC53:
	.ascii	"BUF.mode = 0x%x\012\000"
.LC54:
	.ascii	"BUF.a_pages = 0x%x\012\000"
.LC55:
	.ascii	"BUF VPC = 0x%x\012\000"
.LC56:
	.ascii	"TMP.page = 0x%x\012\000"
.LC57:
	.ascii	"TMP.plane = 0x%x\012\000"
.LC58:
	.ascii	"TMP.id = 0x%x\012\000"
.LC59:
	.ascii	"TMP.mode = 0x%x\012\000"
.LC60:
	.ascii	"TMP.a_pages = 0x%x\012\000"
.LC61:
	.ascii	"GC.page = 0x%x\012\000"
.LC62:
	.ascii	"GC.plane = 0x%x\012\000"
.LC63:
	.ascii	"GC.id = 0x%x\012\000"
.LC64:
	.ascii	"GC.mode = 0x%x\012\000"
.LC65:
	.ascii	"GC.a_pages = 0x%x\012\000"
.LC66:
	.ascii	"WR_CHK = 0x%x %x %x %x\012\000"
.LC67:
	.ascii	"Read Err Cnt = 0x%x\012\000"
.LC68:
	.ascii	"Prog Err Cnt = 0x%x\012\000"
.LC69:
	.ascii	"gc_free_blk_th= 0x%x\012\000"
.LC70:
	.ascii	"gc_merge_free_blk_th= 0x%x\012\000"
.LC71:
	.ascii	"gc_skip_write_count= 0x%x\012\000"
.LC72:
	.ascii	"gc_blk_index= 0x%x\012\000"
.LC73:
	.ascii	"free min EC= 0x%x\012\000"
.LC74:
	.ascii	"free max EC= 0x%x\012\000"
.LC75:
	.ascii	"GC__SB VPC = 0x%x\012\000"
.LC76:
	.ascii	"%d. [0x%x]=0x%x 0x%x  0x%x\012\000"
.LC77:
	.ascii	"free %d. [0x%x] 0x%x  0x%x\012\000"
.LC78:
	.ascii	"%s\012\000"
.LC79:
	.ascii	"FTL version: 5.0.48 20180528\000"
.LC80:
	.ascii	"swblk %x ,avg = %x max= %x vpc= %x,ec=%x ,max ec=%x"
	.ascii	"\012\000"
.LC81:
	.ascii	"FtlGcRefreshBlock  0x%x\012\000"
.LC82:
	.ascii	"FtlGcMarkBadPhyBlk %d 0x%x\012\000"
.LC83:
	.ascii	"%s error allocating memory. return -1\012\000"
.LC84:
	.ascii	"%s %p:0x%x:\000"
.LC85:
	.ascii	"%x \000"
.LC86:
	.ascii	"\000"
.LC87:
	.ascii	"%d statReg->V6.mtrans_cnt=%d flReg.V6.page_num=%d\012"
	.ascii	"\000"
.LC88:
	.ascii	"nandc:\000"
.LC89:
	.ascii	"%d flReg.d32=%x %x\012\000"
.LC90:
	.ascii	"micron RR %d row=%x,count %d,status=%d\012\000"
.LC91:
	.ascii	"ECC:%d\012\000"
.LC92:
	.ascii	"sdr read ok %x ecc=%d\012\000"
.LC93:
	.ascii	"sync para %d\012\000"
.LC94:
	.ascii	"TOG mode Read error %x %x\012\000"
.LC95:
	.ascii	"read retry status %x %x %x\012\000"
.LC96:
	.ascii	"Read pageadd=%x  ecc=%x err=%x\012\000"
.LC97:
	.ascii	"data:\000"
.LC98:
	.ascii	"spare:\000"
.LC99:
	.ascii	"ReadRetry pageadd=%x ecc=%x err=%x\012\000"
.LC100:
	.ascii	"FtlGcScanTempBlkError ID %x %x!!!!!!!\012\000"
.LC101:
	.ascii	"prog error: = %x\012\000"
.LC102:
	.ascii	"prog read error: = %x\012\000"
.LC103:
	.ascii	"prog read s error: = %x %x %x\012\000"
.LC104:
	.ascii	"prog read d error: = %x %x %x\012\000"
.LC105:
	.ascii	"FtlVpcTblFlush error = %x error count = %d\012\000"
.LC106:
	.ascii	"FtlBbmTblFlush id=%x,page=%x,previd=%x cnt=%d\012\000"
.LC107:
	.ascii	"FtlBbmTblFlush error:%x\012\000"
.LC108:
	.ascii	"FtlBbmTblFlush error = %x error count = %d\012\000"
.LC109:
	.ascii	"FtlGcFreeBadSuperBlk 0x%x\012\000"
.LC110:
	.ascii	"decrement_vpc_count %x = %d\012\000"
.LC111:
	.ascii	"FlashMakeFactorBbt %d\012\000"
.LC112:
	.ascii	"bad block:%d %d\012\000"
.LC113:
	.ascii	"FMFB:%d %d\012\000"
.LC114:
	.ascii	"E:bad block:%d\012\000"
.LC115:
	.ascii	"FMFB:Save %d %d\012\000"
.LC116:
	.ascii	"ftl_map_blk_gc blk info: %x %x %x\012\000"
.LC117:
	.ascii	"page map lost: %x %x\012\000"
.LC118:
	.ascii	"FtlMapWritePage error = %x\012\000"
.LC119:
	.ascii	"FtlMapWritePage error = %x error count = %d\012\000"
.LC120:
	.ascii	"FtlVendorPartRead refresh = %x phyAddr = %x\012\000"
.LC121:
	.ascii	"slc mode\000"
.LC122:
	.ascii	"no ect\000"
.LC123:
	.ascii	"load_l2p_region refresh = %x phyAddr = %x\012\000"
.LC124:
	.ascii	"page map lost %x %x %x %x\012\000"
.LC125:
	.ascii	"RSB refresh addr %x\012\000"
.LC126:
	.ascii	"spuer block %x vpn is 0\012 \000"
.LC127:
	.ascii	"g_recovery_ppa %x ver %x\012 \000"
.LC128:
	.ascii	"...%s enter...\012\000"
.LC129:
	.ascii	"FtlCheckVpc %x = %x  %x\012\000"
.LC130:
	.ascii	"%d GC datablk  = %x vpc %x %x\012\000"
.LC131:
	.ascii	"SWL %x, FSB = %x vpc= %x,ec=%x th=%x\012\000"
.LC132:
	.ascii	"Ftlwrite decrement_vpc_count %x = %d\012\000"
.LC133:
	.ascii	"GC des block %x done\012\000"
.LC134:
	.ascii	"rk_ftl_de_init %x\012\000"
.LC135:
	.ascii	"fix power lost blk = %x vpc=%x\012\000"
.LC136:
	.ascii	"erase power lost blk = %x vpc=%x\012\000"
.LC137:
	.ascii	"...%s: no bad block mapping table, format device\012"
	.ascii	"\000"
.LC138:
	.ascii	"...%s FtlSysBlkInit error ,format device!\012\000"
.LC139:
	.ascii	"FtlWrite: lpa error:%x %x\012\000"
.LC140:
	.ascii	"FLFB:%d %d\012\000"
.LC141:
	.ascii	"BBT:\000"
.LC142:
	.ascii	"otp error! %d\000"
.LC143:
	.ascii	"rr\000"
.LC144:
	.ascii	"FlashLoadPhyInfo fail %x!!\012\000"
.LC145:
	.ascii	"FtlInit %x\012\000"
.LC146:
	.ascii	"IdBlockReadData %x %x\012\000"
.LC147:
	.ascii	"IdBlockReadData %x %x ret= %x\012\000"
.LC148:
	.ascii	"IDBlockWriteData %x %x\012\000"
.LC149:
	.ascii	"IDBlockWriteData %x %x ret= %x\012\000"
.LC150:
	.ascii	"write_idblock fix data %x %x\012\000"
.LC151:
	.ascii	"idblk:\000"
.LC152:
	.ascii	"idb reverse %x %x\012\000"
.LC153:
	.ascii	"write_idblock totle_sec %x %x %x %x\012\000"
.LC154:
	.ascii	"write and check error:%d idb=%x,offset=%x,r=%x,w=%x"
	.ascii	"\012\000"
.LC155:
	.ascii	"write\000"
.LC156:
	.ascii	"read\000"
.LC157:
	.ascii	"write_idblock error %d\012\000"
.LC158:
	.ascii	"READ_SECTOR_IO\012\000"
.LC159:
	.ascii	"rk_copy_from_user error\012\000"
.LC160:
	.ascii	"READ_SECTOR_IO %x %x\012\000"
.LC161:
	.ascii	"rk_copy_to_user error\012\000"
.LC162:
	.ascii	"WRITE_SECTOR_IO\012\000"
.LC163:
	.ascii	"WRITE_SECTOR_IO %x %x\012\000"
.LC164:
	.ascii	"END_WRITE_SECTOR_IO\012\000"
.LC165:
	.ascii	"END_WRITE_SECTOR_IO %x %x\012\000"
.LC166:
	.ascii	"GET_FLASH_INFO_IO\012\000"
.LC167:
	.ascii	"GET_BAD_BLOCK_IO\012\000"
.LC168:
	.ascii	"bbt:\000"
.LC169:
	.ascii	"GET_LOCK_FLAG_IO\012\000"
.LC170:
	.ascii	"GET_PUBLIC_KEY_IO\012\000"
.LC171:
	.ascii	"RKNAND_GET_DRM_KEY\012\000"
.LC172:
	.ascii	"RKNAND_STORE_DRM_KEY\012\000"
.LC173:
	.ascii	"RKNAND_DIASBLE_SECURE_BOOT\012\000"
.LC174:
	.ascii	"RKNAND_ENASBLE_SECURE_BOOT\012\000"
.LC175:
	.ascii	"RKNAND_GET_SN_SECTOR\012\000"
.LC176:
	.ascii	"RKNAND_LOADER_UNLOCK\012\000"
.LC177:
	.ascii	"RKNAND_LOADER_STATUS\012\000"
.LC178:
	.ascii	"RKNAND_LOADER_LOCK\012\000"
.LC179:
	.ascii	"LockKey not match %d\012\000"
.LC180:
	.ascii	"RKNAND_GET_VENDOR_SECTOR\012\000"
.LC181:
	.ascii	"RKNAND_STORE_VENDOR_SECTOR\012\000"
.LC182:
	.ascii	"return ret = %lx\012\000"
.LC183:
	.ascii	"secureBootEn check error\012\000"
.LC184:
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
	.size	NandFlashParaTbl, 2368
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
