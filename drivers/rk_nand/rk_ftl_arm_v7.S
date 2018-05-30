/*
 * Copyright (c) 2016-2018, Fuzhou Rockchip Electronics Co., Ltd
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 * date: 2018-05-30
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
	stmfd	sp!, {r3, r4, r5, lr}
	.save {r3, r4, r5, lr}
	mov	r0, #0
	ldr	r4, .L246
	bl	NandcWaitFlashReady
	bl	FlashSetReadRetryDefault
	ldr	r3, .L246+4
	ldr	r2, [r4, #3776]
	cmp	r2, r3
	bne	.L244
	mov	r0, #0
	bl	flash_enter_slc_mode
.L244:
	ldrb	r3, [r4, #3780]	@ zero_extendqisi2
	ldr	r5, .L246
	cmp	r3, #0
	beq	.L245
	ldrb	r3, [r5, #3772]	@ zero_extendqisi2
	tst	r3, #1
	beq	.L245
	mov	r0, #1
	bl	FlashSetInterfaceMode
	mov	r0, #1
	bl	NandcSetMode
	mov	r3, #0
	strb	r3, [r5, #3780]
.L245:
	ldr	r3, [r4, #864]
	mov	r0, #0
	str	r0, [r3, #336]
	ldmfd	sp!, {r3, r4, r5, pc}
.L247:
	.align	2
.L246:
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
	ldr	r3, .L252
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
	bne	.L249
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
	b	.L251
.L249:
	uxtb	r2, r5
	str	r2, [r6, #4]
	mov	r2, r5, lsr #8
	str	r2, [r6, #4]
	mov	r2, r5, lsr #16
	str	r2, [r6, #4]
	ldrb	r3, [r3, #905]	@ zero_extendqisi2
	str	r3, [r6, #8]
.L251:
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
.L253:
	.align	2
.L252:
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
	ldr	r3, .L255
	add	r3, r3, r0, asl #3
	ldr	r3, [r3, #864]
	str	r1, [r3, #336]
	bx	lr
.L256:
	.align	2
.L255:
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
	ldr	r1, .L267
	bl	__aeabi_idiv
	ldr	r3, .L267+4
	ldr	r3, [r3, #3012]
	cmp	r0, #250
	movwgt	r2, #8354
	bgt	.L265
	cmp	r0, #220
	bgt	.L266
.L260:
	cmp	r0, #185
	movwgt	r2, #4226
	bgt	.L265
	cmp	r0, #160
	movwgt	r2, #4194
	bgt	.L265
	cmp	r4, #35
	movwls	r2, #4193
	bls	.L265
	cmp	r4, #99
	movwls	r2, #4225
	bls	.L265
.L266:
	movw	r2, #8322
.L265:
	str	r2, [r3, #4]
	ldmfd	sp!, {r4, pc}
.L268:
	.align	2
.L267:
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
	ldr	r4, .L271
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
	ldr	r2, .L271+4
	str	r2, [r3, #304]
	bl	ftl_malloc
	str	r5, [r4, #3820]
	str	r5, [r4, #3828]
	str	r0, [r4, #3792]
	str	r0, [r4, #3796]
	add	r0, r0, #32768
	str	r0, [r4, #3800]
	ldmfd	sp!, {r3, r4, r5, pc}
.L272:
	.align	2
.L271:
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
	bls	.L274
	movw	r3, #4226
	movw	r2, #8322
	cmp	r0, r3
	cmpne	r0, r2
	bne	.L275
.L274:
	ldr	r3, .L276
	ldr	r3, [r3, #3012]
	str	r0, [r3, #4]
.L275:
	ldr	r3, .L276+4
	ldrb	r0, [r3, #2913]	@ zero_extendqisi2
	b	NandcTimeCfg
.L277:
	.align	2
.L276:
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
	ldr	ip, .L279
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
.L280:
	.align	2
.L279:
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
	ldr	r3, .L286
	mov	r1, #1
	ldr	r2, [r3, #3012]
	str	r0, [r3, #3832]
	mov	r3, #0
	str	r1, [r2, #8]
	mov	r1, #16
	cmp	r0, r1
	bfi	r3, r1, #8, #8
	bfc	r3, #18, #1
	beq	.L285
.L282:
	cmp	r0, #24
	orreq	r3, r3, #16
	beq	.L283
	cmp	r0, #40
	orr	r3, r3, #262144
	orr	r3, r3, #16
	bne	.L283
.L285:
	bfc	r3, #4, #1
.L283:
	orr	r3, r3, #1
	str	r3, [r2, #12]
	bx	lr
.L287:
	.align	2
.L286:
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
	ldr	r3, .L289
	strb	r0, [r3, #3836]
	b	NandcBchSel
.L290:
	.align	2
.L289:
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
	ldr	r3, .L296
	stmfd	sp!, {r4, r5, r6, lr}
	.save {r4, r5, r6, lr}
	mov	r4, #0
	ldr	r2, [r3, #3012]
	ldr	r1, [r3, #3016]
	ldr	r5, [r3, #3032]
	ldr	r6, .L296+4
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
.L293:
	ldrb	r3, [r6, r4, asl #3]	@ zero_extendqisi2
	sub	r3, r3, #1
	uxtb	r3, r3
	cmp	r3, #253
	bhi	.L292
	uxtb	r0, r4
	bl	FlashReset
.L292:
	add	r4, r4, #1
	cmp	r4, #4
	bne	.L293
	ldr	r4, .L296
	ldrb	r3, [r4, #3780]	@ zero_extendqisi2
	cmp	r3, #0
	beq	.L294
	mov	r0, #1
	bl	NandcSetMode
	ldrb	r0, [r4, #3772]	@ zero_extendqisi2
	bl	FlashSetInterfaceMode
	ldrb	r0, [r4, #3772]	@ zero_extendqisi2
	bl	NandcSetMode
	ubfx	r0, r5, #8, #8
	bl	NandcSetDdrPara
.L294:
	ldr	r3, .L296
	ldr	r3, [r3, #3624]
	ldrb	r0, [r3, #20]	@ zero_extendqisi2
	ldmfd	sp!, {r4, r5, r6, lr}
	b	FlashBchSel
.L297:
	.align	2
.L296:
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
	bne	.L301
	mov	r0, r4
	ldmfd	sp!, {r4, lr}
	b	wait_for_nand_flash_ready
.L301:
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
	ldr	r5, .L322
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
	ldrb	r3, [r5, #3837]	@ zero_extendqisi2
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
	ldr	r5, .L322
	mov	r4, #0
	ldr	r8, .L322+4
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
	ldrb	r0, [r8, r4, asl #4]	@ zero_extendqisi2
	bl	flash_exit_slc_mode
.L314:
	add	r4, r4, #1
.L313:
	ldrb	r2, [r5, #3762]	@ zero_extendqisi2
	ldr	r3, .L322
	cmp	r4, r2
	bcc	.L315
	ldr	r0, [r3, #3840]
	cmp	r0, #0
	beq	.L304
	ldrb	r3, [r3, #2980]	@ zero_extendqisi2
	cmp	r3, #69
	moveq	r3, #0
	moveq	r2, r3
	bne	.L321
	b	.L316
.L317:
	add	r3, r3, #1
	str	r2, [r6, #-36]
.L316:
	cmp	r3, r7
	add	r6, r6, #36
	bne	.L317
.L321:
	mov	r0, #0
.L304:
	add	sp, sp, #28
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, sl, fp, pc}
.L323:
	.align	2
.L322:
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
	ldr	r3, .L326
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
.L327:
	.align	2
.L326:
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
.L329:
	ldr	r3, [r0, #8]
	str	r3, [sp, #4]
	ldr	r3, [sp, #4]
	tst	r3, #1048576
	beq	.L329
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
	ldr	r1, .L344
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
	bls	.L332
	ldr	r3, [r5, #16]
	cmp	r9, #0
	cmpeq	ip, #0
	str	r3, [sp, #12]
	ldr	r3, [sp, #12]
	bfc	r3, #2, #1
	str	r3, [sp, #12]
	beq	.L333
	cmp	r6, #0
	beq	.L334
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
	b	.L336
.L339:
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
.L336:
	ldr	sl, [sp, #4]
	cmp	r2, sl
	bcc	.L339
.L334:
	mov	r0, r5
	add	r8, r8, #1
	bl	rk_nandc_xfer_irq_flag_init
	ldr	r3, [r5, #368]
	mov	r8, r8, asr #1
	cmp	r9, #0
	bfi	r4, r8, #22, #6
	ldr	r8, .L344
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
.L333:
	ldr	r3, [sp, #12]
	str	r3, [r5, #16]
.L332:
	str	r7, [r5, #12]
	str	r4, [r5, #8]
	orr	r4, r4, #4
	str	r4, [r5, #8]
	add	sp, sp, #20
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, sl, fp, pc}
.L345:
	.align	2
.L344:
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
	bne	.L347
	cmp	r3, #0
	beq	.L348
	mov	r0, r2
	mov	r1, r3
	mov	r2, #1024
	bl	memcpy
.L348:
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
.L347:
	cmp	r3, #0
	beq	.L350
	mov	r1, r2
	mov	r0, r3
	mov	r2, #1024
	bl	memcpy
.L350:
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
	b	.L352
.L353:
	add	r3, r3, #1
	mov	r2, r2, asl #1
	uxth	r3, r3
.L352:
	cmp	r2, r0
	bls	.L353
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
	ldr	r3, .L358
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
.L359:
	.align	2
.L358:
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
	ldr	r3, .L377
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
	ldr	r2, .L377+4
.L361:
	strb	r3, [r3, r2]
	add	r3, r3, #1
	cmp	r3, #32
	bne	.L361
	ldrh	r2, [r4, #14]
	ldrh	r3, [r4, #20]
	cmp	r3, r2, lsr #8
	bcs	.L362
	ldr	r2, .L377+4
	sub	ip, r1, #1
	uxtb	r7, r0
	mov	r3, #0
	rsb	sl, r0, r2
	str	sl, [sp, #8]
	mla	ip, r0, ip, r2
	mov	r8, r7, asl #1
	uxtb	r8, r8
	str	ip, [sp, #12]
	b	.L363
.L364:
	strb	ip, [r9, r2]
	add	fp, ip, r7
	add	ip, ip, r8
	add	r5, r5, #1
	strb	fp, [sl, r2]
	uxtb	ip, ip
.L366:
	cmp	r5, r1
	add	r2, r2, r0
	bcc	.L364
	add	r3, r3, #1
.L363:
	cmp	r3, r0
	bcs	.L365
	ldr	fp, [sp, #8]
	mov	r2, #0
	uxtb	ip, r3
	mov	r5, r2
	add	r9, fp, r3
	ldr	fp, [sp, #12]
	add	sl, fp, r3
	b	.L366
.L365:
	ldr	r3, .L377
	movw	r2, #3870
	mov	r1, r1, asl #1
	mov	r6, r6, lsr #1
	strh	r1, [r3, r2]	@ movhi
	movw	r2, #3858
	strh	r6, [r3, r2]	@ movhi
.L362:
	ldr	r3, .L377
	movw	r2, #3906
	cmp	lr, #1
	mov	r1, #5
	mov	ip, #0
	ldr	sl, .L377
	ldrb	r8, [r3, #852]	@ zero_extendqisi2
	strh	r1, [r3, r2]	@ movhi
	movw	r1, #3908
	streqh	lr, [r3, r2]	@ movhi
	cmp	r8, #0
	movw	r2, #3910
	strh	ip, [r3, r1]	@ movhi
	mov	r1, #4352
	strh	r1, [r3, r2]	@ movhi
	ldrne	r3, .L377
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
	ldr	r4, .L377
	ldrls	r2, .L377
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
	ldreq	r3, .L377
	uxtah	r6, r6, r0
	str	r6, [r4, #3844]
	ldr	r4, .L377
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
	bne	.L374
.L373:
	cmp	r2, #7
	movls	r3, #8
	strls	r3, [r4, #3944]
.L374:
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
	ldrcc	r2, .L377
	movcc	r1, #1
	strcch	r1, [r2, r3]	@ movhi
	add	sp, sp, #20
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, sl, fp, pc}
.L378:
	.align	2
.L377:
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
	ldr	r3, .L386
	movw	r2, #3962
	ldrh	r2, [r3, r2]
	cmp	r2, #0
	beq	.L384
	mov	r2, #3936
	ldrh	r1, [r3, r2]
	ldr	r2, [r3, #3964]
	mov	r3, #0
	b	.L381
.L383:
	ldrh	ip, [r2], #2
	cmp	ip, r0
	beq	.L385
	add	r3, r3, #1
	uxth	r3, r3
.L381:
	cmp	r3, r1
	bne	.L383
	mov	r0, #0
	bx	lr
.L385:
	mov	r0, #1
	bx	lr
.L384:
	mov	r0, r2
	bx	lr
.L387:
	.align	2
.L386:
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
	ldr	r2, .L396
	stmfd	sp!, {r4, lr}
	.save {r4, lr}
	ldr	r3, [r2, #3968]
	cmp	r3, #0
	beq	.L393
	ldr	ip, [r2, #3972]
	mov	r2, #0
.L392:
	ldr	r4, [ip, #16]
	cmp	r4, r0
	bcc	.L390
	cmp	r4, r1
	bls	.L394
.L390:
	add	r2, r2, #1
	add	ip, ip, #36
	cmp	r2, r3
	bne	.L392
	mov	r0, #0
	ldmfd	sp!, {r4, pc}
.L394:
	mov	r0, #1
	ldmfd	sp!, {r4, pc}
.L393:
	mov	r0, r3
	ldmfd	sp!, {r4, pc}
.L397:
	.align	2
.L396:
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
	ldr	r3, .L399
	ldr	r0, [r3, #3956]
	bx	lr
.L400:
	.align	2
.L399:
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
	ldr	r3, .L402
	ldr	r0, [r3, #3956]
	bx	lr
.L403:
	.align	2
.L402:
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
	ldr	r3, .L405
	ldr	r0, [r3, #3976]
	bx	lr
.L406:
	.align	2
.L405:
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
	ldr	r4, .L408
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
	ldr	r0, .L408+4
	bl	printk
	movw	r3, #3986
	ldrh	r2, [r4, r3]
	mov	r0, #0
	add	r2, r2, r6
	strh	r2, [r4, r3]	@ movhi
	ldmfd	sp!, {r2, r3, r4, r5, r6, pc}
.L409:
	.align	2
.L408:
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
	ldr	r5, .L411
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
.L412:
	.align	2
.L411:
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
	ldr	r6, .L418
	movw	r8, #3912
	movw	r7, #3870
	b	.L415
.L417:
	mov	r0, r5
	add	r5, r5, #1
	bl	FtlBbmIsBadBlock
	uxth	r5, r5
	cmp	r0, #0
	addne	r4, r4, #1
	uxthne	r4, r4
.L415:
	ldrh	r3, [r6, r8]
	ldrh	r2, [r6, r7]
	mul	r3, r2, r3
	cmp	r5, r3
	blt	.L417
	mov	r0, r4
	ldmfd	sp!, {r4, r5, r6, r7, r8, pc}
.L419:
	.align	2
.L418:
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
	ldr	r5, .L421
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
.L422:
	.align	2
.L421:
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
	ldr	r3, .L424
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
.L425:
	.align	2
.L424:
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
	ldr	r4, .L427
	ldrh	r1, [r4, r3]
	bl	__aeabi_uidivmod
	mov	r3, #3872
	uxth	r0, r1
	ldrh	r1, [r4, r3]
	bl	__aeabi_uidiv
	uxth	r0, r0
	ldmfd	sp!, {r4, pc}
.L428:
	.align	2
.L427:
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
	bls	.L430
	rsb	r0, r1, r0
	cmp	r0, #-2147483648
	movhi	r0, #0
	movls	r0, #1
	bx	lr
.L430:
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
	ldr	r2, .L433
	movw	r3, #4046
	ldrh	r0, [r2, r3]
	rsbs	r0, r0, #1
	movcc	r0, #0
	bx	lr
.L434:
	.align	2
.L433:
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
	ldr	r2, .L436
	movw	r3, #4046
	ldrh	r0, [r2, r3]
	sub	r2, r0, #1024
	rsbs	r0, r2, #0
	adc	r0, r0, r2
	bx	lr
.L437:
	.align	2
.L436:
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
	ldr	r2, .L441
	movw	r3, #4046
	stmfd	sp!, {r4, r5, r6, lr}
	.save {r4, r5, r6, lr}
	mov	r5, r0
	ldrh	r3, [r2, r3]
	cmp	r3, #1024
	ldmeqfd	sp!, {r4, r5, r6, pc}
	cmp	r1, #0
	beq	.L440
	ldr	r4, .L441+4
	ldr	r3, [r4, #-2088]
	cmp	r3, #0
	bne	.L440
	bl	P2V_block_in_plane
	mov	r1, #1
	mov	r3, r5, asl #10
	mov	r2, r1
	mov	r6, r0
	ldr	r0, [r4, #-2084]
	mov	r6, r6, asl #1
	str	r3, [r0, #4]
	bl	FlashEraseBlocks
	ldr	r3, [r4, #-2080]
	ldrh	r2, [r3, r6]
	add	r2, r2, #1
	strh	r2, [r3, r6]	@ movhi
	ldr	r3, [r4, #-2076]
	add	r3, r3, #1
	str	r3, [r4, #-2076]
.L440:
	ldr	r3, .L441
	movw	r2, #4046
	movw	r0, #4040
	ldrh	r1, [r3, r2]
	add	r1, r1, #1
	strh	r1, [r3, r2]	@ movhi
	movw	r2, #4044
	ldrh	r1, [r3, r2]
	add	ip, r1, #4
	add	r1, r1, #1
	bic	r1, r1, #64512
	add	ip, r3, ip, asl #1
	strh	r5, [ip, r0]	@ movhi
	strh	r1, [r3, r2]	@ movhi
	ldmfd	sp!, {r4, r5, r6, pc}
.L442:
	.align	2
.L441:
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
	ldr	r3, .L446
	mov	r0, #0
	ldr	r2, .L446+4
	ldr	r1, .L446+8
	stmfd	sp!, {r4, r5, lr}
	.save {r4, r5, lr}
	ldrh	r4, [r2, r3]
	movw	r3, #4042
	ldrh	r2, [r1, r3]
	add	r3, r3, #2
	and	r4, r4, #31
	ldrh	r3, [r1, r3]
	b	.L444
.L445:
	add	ip, r1, r2, asl #1
	add	r2, r2, #1
	add	ip, ip, #4048
	add	r0, r0, #1
	mov	r2, r2, asl #22
	ldrh	r5, [ip, #0]
	add	ip, r1, r3, asl #1
	add	r3, r3, #1
	add	ip, ip, #4048
	mov	r2, r2, lsr #22
	uxth	r0, r0
	mov	r3, r3, asl #22
	strh	r5, [ip, #0]	@ movhi
	mov	r3, r3, lsr #22
.L444:
	cmp	r0, r4
	ldr	ip, .L446+8
	blt	.L445
	movw	r1, #4042
	strh	r2, [ip, r1]	@ movhi
	movw	r2, #4044
	strh	r3, [ip, r2]	@ movhi
	ldmfd	sp!, {r4, r5, pc}
.L447:
	.align	2
.L446:
	.word	-2044
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
	ldr	r3, .L454
	movw	r2, #4046
	stmfd	sp!, {r4, r5, r6, lr}
	.save {r4, r5, r6, lr}
	mov	r1, r0
	ldrh	r0, [r3, r2]
	cmp	r0, #0
	ldmeqfd	sp!, {r4, r5, r6, pc}
	movw	r2, #4042
	mov	r5, #0
	ldrh	r2, [r3, r2]
	b	.L450
.L452:
	add	ip, r5, r2
	ldr	r4, .L454
	movw	r6, #4040
	mov	ip, ip, asl #22
	add	ip, r3, ip, lsr #21
	add	ip, ip, #4048
	ldrh	ip, [ip, #0]
	cmp	ip, r1
	bne	.L451
	ldr	r0, .L454+4
	bl	printk
	movw	r2, #4042
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
	movw	r3, #4046
	ldrh	r2, [r4, r3]
	sub	r2, r2, #1
	strh	r2, [r4, r3]	@ movhi
	ldmfd	sp!, {r4, r5, r6, pc}
.L451:
	add	r5, r5, #1
.L450:
	cmp	r5, r0
	bcc	.L452
	mov	r0, #0
	ldmfd	sp!, {r4, r5, r6, pc}
.L455:
	.align	2
.L454:
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
	movw	r7, #4046
	ldr	r4, .L463
	movw	r8, #4042
.L457:
	ldrh	r1, [r4, r7]
	cmp	r1, #0
	beq	.L458
	ldr	r9, .L463+4
	sub	r1, r1, #1
	ldrh	r3, [r4, r8]
	ldr	r6, [r9, #-2088]
	add	r2, r4, r3, asl #1
	add	r3, r3, #1
	add	r2, r2, #4048
	cmp	r6, #0
	bic	r3, r3, #64512
	ldrh	r5, [r2, #0]
	strh	r1, [r4, r7]	@ movhi
	strh	r3, [r4, r8]	@ movhi
	bne	.L459
	mov	r0, r5
	bl	P2V_block_in_plane
	mov	r3, r5, asl #10
	mov	sl, r0
	ldr	r0, [r9, #-2084]
	str	r3, [r0, #4]
	ldrb	r3, [r4, #928]	@ zero_extendqisi2
	cmp	r3, #0
	beq	.L460
	mov	r1, r6
	mov	r2, #1
	bl	FlashEraseBlocks
.L460:
	ldr	r6, .L463+4
	mov	r1, #1
	mov	r2, r1
	mov	sl, sl, asl #1
	ldr	r0, [r6, #-2084]
	bl	FlashEraseBlocks
	ldr	r3, [r6, #-2080]
	ldrh	r2, [r3, sl]
	add	r2, r2, #1
	strh	r2, [r3, sl]	@ movhi
	ldr	r3, [r6, #-2076]
	add	r3, r3, #1
	str	r3, [r6, #-2076]
	b	.L459
.L458:
	ldr	r0, .L463+8
	bl	printk
.L461:
	b	.L461
.L459:
	cmp	r5, #0
	bne	.L462
	mov	r1, r5
	ldrh	r2, [r4, r7]
	ldr	r0, .L463+12
	bl	printk
	b	.L457
.L462:
	mov	r0, r5
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, sl, pc}
.L464:
	.align	2
.L463:
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
	ldr	r2, .L480
	mov	r3, #3856
	stmfd	sp!, {r4, r5, r6, r7, r8, r9, sl, fp, lr}
	.save {r4, r5, r6, r7, r8, r9, sl, fp, lr}
	.pad #20
	sub	sp, sp, #20
	ldrh	r3, [r2, r3]
	cmp	r3, r0
	bls	.L466
	ldr	r1, .L480+4
	mov	ip, #6
	mul	ip, ip, r0
	mvn	r6, #0
	ldr	r4, [r1, #-2024]
	mov	r5, r1
	add	r2, r4, ip
	strh	r6, [r2, #2]	@ movhi
	strh	r6, [r4, ip]	@ movhi
	ldr	r3, [r1, #-2020]
	cmp	r3, #0
	beq	.L479
.L467:
	ldr	r7, [r1, #-2016]
	mov	r8, r0, asl #1
	ldrh	r1, [r2, #4]
	ldr	r9, .L480
	ldrh	sl, [r7, r8]
	cmp	r1, #0
	ldr	fp, [r5, #-2080]
	mulne	r6, r1, sl
	ldr	r1, [r5, #-2024]
	ldr	sl, .L480+8
	mov	r5, #0
	add	r8, fp, r8
	str	r8, [sp, #12]
	str	r1, [sp, #4]
	rsb	r1, r1, r3
	mov	r1, r1, asr #1
	mul	r1, sl, r1
	mov	sl, #3856
	ldrh	sl, [r9, sl]
	str	sl, [sp, #8]
	uxth	r1, r1
.L474:
	add	r5, r5, #1
	ldr	r8, [sp, #8]
	uxth	r5, r5
	cmp	r5, r8
	bhi	.L466
	cmp	r0, r1
	beq	.L466
	mov	r9, r1, asl #1
	ldrh	r8, [r3, #4]
	ldrh	sl, [r7, r9]
	cmp	r8, #0
	mvneq	r8, #0
	mulne	r8, r8, sl
	cmp	r8, r6
	bne	.L470
	ldrh	sl, [fp, r9]
	ldr	r9, [sp, #12]
	ldrh	r8, [r9, #0]
	cmp	sl, r8
	bcc	.L472
	b	.L471
.L470:
	bhi	.L471
.L472:
	ldrh	r8, [r3, #0]
	movw	sl, #65535
	cmp	r8, sl
	streqh	r1, [r2, #2]	@ movhi
	streqh	r0, [r3, #0]	@ movhi
	ldreq	r3, .L480+4
	streq	r2, [r3, #-2012]
	beq	.L466
.L473:
	ldr	r9, [sp, #4]
	mov	r1, #6
	mla	r3, r1, r8, r9
	mov	r1, r8
	b	.L474
.L471:
	strh	r1, [r4, ip]	@ movhi
	ldrh	r1, [r3, #2]
	strh	r1, [r2, #2]	@ movhi
	ldr	r1, .L480+4
	ldr	ip, [r1, #-2020]
	cmp	r3, ip
	bne	.L475
	strh	r0, [r3, #2]	@ movhi
.L479:
	str	r2, [r1, #-2020]
	b	.L466
.L475:
	ldrh	r2, [r3, #2]
	mov	ip, #6
	ldr	r1, [r1, #-2024]
	mul	r2, ip, r2
	strh	r0, [r1, r2]	@ movhi
	strh	r0, [r3, #2]	@ movhi
.L466:
	mov	r0, #0
	add	sp, sp, #20
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, sl, fp, pc}
.L481:
	.align	2
.L480:
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
	ldr	r2, .L483
	ldr	r3, .L483+4
	ldrh	r1, [r2, r3]
	add	r1, r1, #1
	strh	r1, [r2, r3]	@ movhi
	ldmfd	sp!, {r3, pc}
.L484:
	.align	2
.L483:
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
	beq	.L486
	ldr	r1, .L493
	mov	r5, #6
	mul	ip, r5, r0
	mvn	r3, #0
	ldr	r4, [r1, #-2024]
	add	r2, r4, ip
	strh	r3, [r2, #2]	@ movhi
	strh	r3, [r4, ip]	@ movhi
	ldr	r3, [r1, #-2004]
	cmp	r3, #0
	beq	.L492
.L487:
	ldr	sl, [r1, #-2024]
	mov	r8, r0, asl #1
	ldr	r7, [r1, #-2080]
	rsb	r1, sl, r3
	ldr	r9, .L493+4
	mov	r1, r1, asr #1
	ldrh	r8, [r7, r8]
	mul	r1, r9, r1
	mov	r9, r5
	uxth	r1, r1
.L490:
	mov	r5, r1, asl #1
	ldrh	r5, [r7, r5]
	cmp	r5, r8
	bcs	.L488
	ldrh	r5, [r3, #0]
	cmp	r5, r6
	streqh	r1, [r2, #2]	@ movhi
	streqh	r0, [r3, #0]	@ movhi
	beq	.L486
.L489:
	mla	r3, r9, r5, sl
	mov	r1, r5
	b	.L490
.L488:
	ldrh	r5, [r3, #2]
	strh	r5, [r2, #2]	@ movhi
	strh	r1, [r4, ip]	@ movhi
	ldr	r1, .L493
	ldr	ip, [r1, #-2004]
	cmp	r3, ip
	bne	.L491
	strh	r0, [r3, #2]	@ movhi
.L492:
	str	r2, [r1, #-2004]
	b	.L486
.L491:
	ldrh	r2, [r3, #2]
	mov	ip, #6
	ldr	r1, [r1, #-2024]
	mul	r2, ip, r2
	strh	r0, [r1, r2]	@ movhi
	strh	r0, [r3, #2]	@ movhi
.L486:
	mov	r0, #0
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, sl, pc}
.L494:
	.align	2
.L493:
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
	ldr	r2, .L496
	ldr	r3, .L496+4
	ldrh	r1, [r2, r3]
	add	r1, r1, #1
	strh	r1, [r2, r3]	@ movhi
	ldmfd	sp!, {r3, pc}
.L497:
	.align	2
.L496:
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
	ldr	ip, .L503
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
	bne	.L499
	cmp	r4, r6
	mlane	r2, r2, r4, r3
	moveq	r2, #0
	streq	r2, [r0, #0]
	strne	r2, [r0, #0]
	mvnne	r0, #0
	strneh	r0, [r2, #2]	@ movhi
	b	.L501
.L499:
	cmp	r4, r6
	ldrh	r0, [r5, #2]
	bne	.L502
	cmp	r0, r4
	mulne	r2, r2, r0
	mvnne	r0, #0
	strneh	r0, [r3, r2]	@ movhi
	b	.L501
.L502:
	mla	r4, r2, r4, r3
	strh	r0, [r4, #2]	@ movhi
	ldrh	r6, [r5, #2]
	ldrh	r4, [r3, r1]
	ldr	r0, [ip, #-2024]
	mul	r2, r2, r6
	strh	r4, [r0, r2]	@ movhi
.L501:
	mvn	r2, #0
	mov	r0, #0
	strh	r2, [r3, r1]	@ movhi
	strh	r2, [r5, #2]	@ movhi
	ldmfd	sp!, {r4, r5, r6, pc}
.L504:
	.align	2
.L503:
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
	beq	.L510
	ldr	r2, .L511
	movw	lr, #65535
	mov	ip, #6
	ldr	r4, [r2, #-2024]
	b	.L507
.L509:
	mla	r3, ip, r2, r4
	sub	r1, r1, #1
	uxth	r1, r1
.L507:
	cmp	r1, #0
	beq	.L508
	ldrh	r2, [r3, #0]
	cmp	r2, lr
	bne	.L509
.L508:
	rsb	r4, r4, r3
	ldr	r3, .L511+4
	mov	r4, r4, asr #1
	mul	r4, r3, r4
	uxth	r4, r4
	mov	r1, r4
	bl	List_remove_node
	mov	r0, r4
	ldmfd	sp!, {r4, pc}
.L510:
	movw	r0, #65535
	ldmfd	sp!, {r4, pc}
.L512:
	.align	2
.L511:
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
	ldr	r2, .L519
	ldr	r3, [r2, #-2020]
	cmp	r3, #0
	beq	.L518
	ldr	r2, [r2, #-2024]
	movw	r1, #65535
	mov	ip, #6
	b	.L515
.L517:
	mla	r3, ip, r3, r2
	sub	r0, r0, #1
	uxth	r0, r0
.L515:
	cmp	r0, #0
	beq	.L516
	ldrh	r3, [r3, #0]
	cmp	r3, r1
	bne	.L517
	mov	r0, r1
	bx	lr
.L516:
	rsb	r3, r2, r3
	ldr	r0, .L519+4
	mov	r3, r3, asr #1
	mul	r0, r0, r3
	uxth	r0, r0
	bx	lr
.L518:
	movw	r0, #65535
	bx	lr
.L520:
	.align	2
.L519:
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
	ldr	r3, .L528
	ldr	r2, .L528+4
	ldrh	r2, [r3, r2]
	cmp	r2, r0
	beq	.L522
	ldr	r2, .L528+8
	ldrh	r2, [r3, r2]
	cmp	r2, r0
	beq	.L522
	ldr	r2, .L528+12
	ldrh	r2, [r3, r2]
	cmp	r2, r0
	beq	.L522
	mov	r5, #6
	ldr	r2, [r3, #-2024]
	mul	r5, r5, r0
	ldr	r0, [r3, #-2020]
	add	r1, r2, r5
	cmp	r1, r0
	beq	.L522
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
	bne	.L524
	ldrh	r1, [r2, r5]
	cmp	r1, r3
	beq	.L522
.L524:
	mov	r1, #6
	mla	r3, r1, r3, r2
	ldr	r1, .L528+16
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
	bcs	.L522
	ldr	r0, .L528+20
	mov	r1, r4
	bl	List_remove_node
	ldr	r5, .L528
	ldr	r3, .L528+24
	mov	r0, r4
	ldrh	r2, [r5, r3]
	sub	r2, r2, #1
	strh	r2, [r5, r3]	@ movhi
	bl	INSERT_DATA_LIST
.L522:
	mov	r0, #0
	ldmfd	sp!, {r3, r4, r5, pc}
.L529:
	.align	2
.L528:
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
	b	.L531
.L536:
	mov	r7, r3
	add	r3, r3, #2
	ldrh	r6, [r7, #0]
	cmp	r6, #0
	bne	.L532
	bl	FtlFreeSysBlkQueueOut
	movw	r3, #65533
	sub	r2, r0, #1
	mov	r1, r0
	strh	r0, [r7, #0]	@ movhi
	uxth	r2, r2
	cmp	r2, r3
	bls	.L533
	ldr	r2, .L537
	movw	r3, #4046
	ldr	r0, .L537+4
	ldrh	r2, [r2, r3]
	bl	printk
.L534:
	b	.L534
.L533:
	ldr	r3, [r4, #28]
	strh	r6, [r4, #2]	@ movhi
	add	r3, r3, #1
	str	r3, [r4, #28]
	ldrh	r3, [r4, #8]
	strh	r5, [r4, #0]	@ movhi
	add	r3, r3, #1
	strh	r3, [r4, #8]	@ movhi
	b	.L535
.L532:
	add	r5, r5, #1
	uxth	r5, r5
.L531:
	cmp	r5, r2
	bne	.L536
.L535:
	mov	r0, #0
	ldmfd	sp!, {r3, r4, r5, r6, r7, pc}
.L538:
	.align	2
.L537:
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
	ldr	r2, .L551
	movw	r3, #3954
	stmfd	sp!, {r4, r5, r6, r7, lr}
	.save {r4, r5, r6, r7, lr}
	movw	r1, #65535
	ldrh	r3, [r2, r3]
	ldr	r2, .L551+4
	ldr	ip, [r2, #-1852]
	mov	r2, #0
	mov	r0, r2
	b	.L540
.L542:
	add	r2, r2, #12
	add	r4, ip, r2
	ldrh	r4, [r4, #-12]
	cmp	r4, r1
	ldmeqfd	sp!, {r4, r5, r6, r7, pc}
	add	r0, r0, #1
	uxth	r0, r0
.L540:
	cmp	r0, r3
	bne	.L542
	mov	r1, #0
	mov	r0, r3
	mov	r5, #-2147483648
	mov	r2, r1
	b	.L543
.L545:
	add	r4, ip, r1
	ldr	r4, [r4, #4]
	cmp	r4, #0
	blt	.L544
	cmp	r4, r5
	movcc	r5, r4
	movcc	r0, r2
.L544:
	add	r2, r2, #1
	add	r1, r1, #12
	uxth	r2, r2
.L543:
	cmp	r2, r3
	bne	.L545
	cmp	r0, r3
	ldmccfd	sp!, {r4, r5, r6, r7, pc}
	ldr	r2, .L551+8
	mov	r0, r3
	ldr	r1, .L551+4
	mvn	r4, #0
	ldrh	r6, [r1, r2]
	mov	r2, #0
	mov	r1, r2
	b	.L546
.L548:
	add	r5, ip, r2
	ldr	r5, [r5, #4]
	cmp	r5, r4
	bcs	.L547
	ldrh	r7, [ip, r2]
	cmp	r7, r6
	movne	r4, r5
	movne	r0, r1
.L547:
	add	r1, r1, #1
	add	r2, r2, #12
	uxth	r1, r1
.L546:
	cmp	r1, r3
	bne	.L548
	ldmfd	sp!, {r4, r5, r6, r7, pc}
.L552:
	.align	2
.L551:
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
	ldr	r3, .L559
	ldr	r2, .L559+4
	stmfd	sp!, {r4, r5, lr}
	.save {r4, r5, lr}
	ldrh	r1, [r2, r3]
	cmp	r1, #4
	add	ip, r1, #1
	mov	r1, r3
	strh	ip, [r2, r3]	@ movhi
	bhi	.L554
	cmp	r0, #0
	ldmeqfd	sp!, {r4, r5, pc}
.L554:
	ldr	r0, .L559+8
	mov	r3, #0
	strh	r3, [r2, r1]	@ movhi
	mov	r1, #3856
	str	r3, [r2, #-1844]
	movw	ip, #65535
	ldrh	r4, [r0, r1]
	ldr	r1, [r2, #-2016]
	ldr	r2, .L559+4
	b	.L556
.L558:
	ldrh	r0, [r1], #2
	add	r3, r3, #1
	cmp	r0, ip
	uxth	r3, r3
	ldrne	r5, [r2, #-1844]
	addne	r0, r0, r5
	strne	r0, [r2, #-1844]
.L556:
	cmp	r3, r4
	bne	.L558
	ldmfd	sp!, {r4, r5, pc}
.L560:
	.align	2
.L559:
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
	ldr	r3, .L565
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
.L566:
	.align	2
.L565:
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
	ldr	r3, .L568
	mov	r2, r0, lsr #5
	and	r0, r0, #31
	ldr	r3, [r3, #-1840]
	ldr	r3, [r3, r2, asl #2]
	mov	r0, r3, lsr r0
	and	r0, r0, #1
	bx	lr
.L569:
	.align	2
.L568:
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
	ldr	ip, .L577
	mov	r3, #0
	strh	r3, [r0, #4]	@ movhi
	movw	r3, #3848
	stmfd	sp!, {r4, r5, r6, lr}
	.save {r4, r5, r6, lr}
	ldrh	r3, [ip, r3]
	movw	ip, #65535
	b	.L571
.L573:
	add	r4, r0, r2, asl #1
	add	r2, r2, #1
	ldrh	r4, [r4, #16]
	uxth	r2, r2
	cmp	r4, ip
	ldrneh	r4, [r0, #4]
	addne	r4, r4, #1
	strneh	r4, [r0, #4]	@ movhi
.L571:
	cmp	r2, r3
	bcc	.L573
	ldr	ip, .L577
	movw	r2, #3916
	movw	r4, #65535
	mvn	r1, r1
	ldrh	r5, [ip, r2]
	mov	ip, r0
	mov	r2, #0
	b	.L574
.L576:
	ldrh	r6, [ip, #16]
	add	r2, r2, #1
	add	ip, ip, #2
	cmp	r6, r4
	uxth	r2, r2
	ldrneh	r6, [r0, #4]
	addne	r6, r5, r6
	addne	r6, r6, r1
	strneh	r6, [r0, #4]	@ movhi
.L574:
	cmp	r2, r3
	bne	.L576
	ldmfd	sp!, {r4, r5, r6, pc}
.L578:
	.align	2
.L577:
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
	ldr	r6, .L585
	strh	r5, [r0, #4]	@ movhi
	movw	r8, #3848
	strb	r5, [r4, #7]
	mvn	r7, #0
	b	.L580
.L582:
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
.L580:
	ldrh	r2, [r6, r8]
	ldr	r3, .L585
	cmp	r2, r5
	bhi	.L582
	movw	r1, #3916
	ldrb	r2, [r4, #7]	@ zero_extendqisi2
	ldrh	r1, [r3, r1]
	ldr	r3, [r3, #3840]
	mul	r2, r1, r2
	strh	r2, [r4, #4]	@ movhi
	mov	r2, #0
	cmp	r3, r2
	strb	r2, [r4, #9]
	beq	.L583
	ldr	r2, .L585+4
	ldrh	r3, [r4, #0]
	ldr	r2, [r2, #-2080]
	mov	r3, r3, asl #1
	ldrh	r3, [r2, r3]
	cmp	r3, #59
	movls	r3, #1
	strlsb	r3, [r4, #9]
.L583:
	ldr	r3, .L585
	mov	r0, #0
	ldrb	r3, [r3, #852]	@ zero_extendqisi2
	cmp	r3, #0
	movne	r3, #1
	strneb	r3, [r4, #9]
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, sl, pc}
.L586:
	.align	2
.L585:
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
	ldr	r6, .L592
	movw	sl, #3848
	movw	r8, #3916
	b	.L588
.L590:
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
.L588:
	ldrh	r3, [r6, sl]
	cmp	r3, r5
	bhi	.L590
	cmp	r4, #0
	beq	.L591
	mov	r1, r4
	mov	r0, #32768
	bl	__aeabi_idiv
	uxth	r4, r0
.L591:
	ldr	r3, .L592+4
	mov	r2, #6
	mov	r0, #0
	ldr	r3, [r3, #-2024]
	mla	r7, r2, r7, r3
	strh	r4, [r7, #4]	@ movhi
	ldmfd	sp!, {r3, r4, r5, r6, r7, r8, sl, pc}
.L593:
	.align	2
.L592:
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
	ldr	r3, .L597
	ldr	r0, [r3, #-2004]
	cmp	r0, #0
	bxeq	lr
	ldr	r2, [r3, #-2024]
	rsb	r0, r2, r0
	ldr	r2, .L597+4
	mov	r0, r0, asr #1
	mul	r0, r2, r0
	ldr	r2, [r3, #-2080]
	uxth	r0, r0
	mov	r3, r0, asl #1
	ldrh	r0, [r2, r3]
	bx	lr
.L598:
	.align	2
.L597:
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
	ldr	r2, .L606
	stmfd	sp!, {r4, r5, lr}
	.save {r4, r5, lr}
	ldr	r3, [r2, #-2004]
	cmp	r3, #0
	beq	.L605
	ldr	r1, .L606+4
	mov	ip, #7
	mov	r5, #6
	movw	r4, #65535
	ldrh	r1, [r2, r1]
	mul	r1, ip, r1
	mov	r1, r1, asr #3
	cmp	r0, r1
	uxthgt	r0, r1
	ldr	r1, [r2, #-2024]
	ldr	r2, .L606+8
	rsb	r3, r1, r3
	mov	r3, r3, asr #1
	mul	r3, r2, r3
	mov	r2, #0
	uxth	r3, r3
	b	.L602
.L604:
	mul	ip, r5, r3
	ldrh	ip, [r1, ip]
	cmp	ip, r4
	beq	.L603
	add	r2, r2, #1
	mov	r3, ip
	uxth	r2, r2
.L602:
	cmp	r2, r0
	bne	.L604
.L603:
	ldr	r2, .L606
	mov	r3, r3, asl #1
	ldr	r2, [r2, #-2080]
	ldrh	r0, [r2, r3]
	ldmfd	sp!, {r4, r5, pc}
.L605:
	mov	r0, r3
	ldmfd	sp!, {r4, r5, pc}
.L607:
	.align	2
.L606:
	.word	.LANCHOR2
	.word	-2000
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
	ldr	r5, .L618
	add	r4, sl, #12
	.pad #32
	sub	sp, sp, #32
	ldr	r1, .L618+4
	bl	strcpy
	mov	r0, r4
	ldr	r1, .L618+8
	ldr	r2, [r5, #3048]
	bl	sprintf
	ldr	r1, .L618+12
	ldr	r2, [r5, #3932]
	add	r4, r4, r0
	mov	r0, r4
	bl	sprintf
	ldr	r3, .L618+16
	ldr	r3, [r3, #2996]
	cmp	r3, #1
	add	r4, r4, r0
	rsbne	r0, sl, r4
	bne	.L610
	add	r0, sp, #16
	add	r1, sp, #20
	add	r2, sp, #24
	add	r3, sp, #28
	bl	NandcGetTimeCfg
	ldr	r3, [sp, #24]
	ldr	r2, [sp, #16]
	mov	r0, r4
	ldr	r1, .L618+20
	str	r3, [sp, #0]
	ldr	r3, [sp, #28]
	ldr	r8, .L618+24
	str	r3, [sp, #4]
	ldr	r3, [sp, #20]
	bl	sprintf
	ldr	r1, .L618+28
	add	r6, r4, r0
	ldr	r4, .L618+32
	mov	r0, r6
	add	r6, r6, #10
	bl	strcpy
	ldr	r2, [r5, #3976]
	mov	r0, r6
	ldr	r1, .L618+36
	bl	sprintf
	ldr	r1, .L618+40
	ldr	r2, [r4, #-1844]
	add	r6, r6, r0
	mov	r0, r6
	bl	sprintf
	ldr	r1, .L618+44
	ldr	r2, [r4, #-1836]
	add	r6, r6, r0
	mov	r0, r6
	bl	sprintf
	ldr	r1, .L618+48
	ldr	r2, [r4, #-1832]
	add	r6, r6, r0
	mov	r0, r6
	bl	sprintf
	ldr	r1, .L618+52
	ldr	r2, [r4, #-1828]
	add	r6, r6, r0
	mov	r0, r6
	bl	sprintf
	ldr	r1, .L618+56
	ldr	r2, [r4, #-1824]
	add	r6, r6, r0
	mov	r0, r6
	bl	sprintf
	ldr	r1, .L618+60
	ldr	r2, [r4, #-1820]
	add	r6, r6, r0
	mov	r0, r6
	bl	sprintf
	ldr	r1, .L618+64
	ldr	r2, [r4, #-1816]
	add	r6, r6, r0
	mov	r0, r6
	bl	sprintf
	ldr	r2, [r4, #-1812]
	ldr	r1, .L618+68
	mov	r2, r2, lsr #11
	add	r6, r6, r0
	mov	r0, r6
	bl	sprintf
	ldr	r2, [r4, #-1808]
	ldr	r1, .L618+72
	mov	r2, r2, lsr #11
	add	r6, r6, r0
	mov	r0, r6
	bl	sprintf
	ldr	r1, .L618+76
	ldr	r2, [r4, #-1804]
	add	r6, r6, r0
	mov	r0, r6
	bl	sprintf
	ldr	r1, .L618+80
	ldr	r2, [r4, #-1800]
	add	r6, r6, r0
	mov	r0, r6
	bl	sprintf
	add	r6, r6, r0
	bl	FtlBbtCalcTotleCnt
	movw	r2, #3986
	ldr	r1, .L618+84
	ldrh	r2, [r5, r2]
	mov	r3, r0
	mov	r0, r6
	bl	sprintf
	ldr	r1, .L618+88
	add	r7, r6, r0
	ldr	r6, .L618+92
	mov	r0, r7
	ldrh	r2, [r4, r6]
	bl	sprintf
	ldr	r1, .L618+96
	ldr	r2, [r4, #-1796]
	add	r7, r7, r0
	mov	r0, r7
	bl	sprintf
	ldr	r1, .L618+100
	ldr	r2, [r4, #-1792]
	add	r7, r7, r0
	mov	r0, r7
	bl	sprintf
	ldr	r1, .L618+104
	ldr	r2, [r4, #-1788]
	add	r7, r7, r0
	mov	r0, r7
	bl	sprintf
	ldr	r1, .L618+108
	ldr	r2, [r4, #-2076]
	add	r7, r7, r0
	mov	r0, r7
	bl	sprintf
	ldr	r1, .L618+112
	ldr	r2, [r4, #-1784]
	add	r7, r7, r0
	mov	r0, r7
	bl	sprintf
	ldr	r1, .L618+116
	ldr	r2, [r4, #-1780]
	add	r7, r7, r0
	mov	r0, r7
	bl	sprintf
	ldr	r3, .L618+120
	ldr	r1, .L618+124
	ldrh	r2, [r4, r3]
	add	r7, r7, r0
	mov	r0, r7
	bl	sprintf
	ldr	r3, .L618+128
	ldr	r1, .L618+132
	ldrh	r2, [r4, r3]
	add	r7, r7, r0
	mov	r0, r7
	bl	sprintf
	ldr	r2, [r5, #3956]
	ldr	r1, .L618+136
	add	r7, r7, r0
	mov	r0, r7
	bl	sprintf
	ldr	r2, [r5, #3948]
	ldr	r1, .L618+140
	add	r7, r7, r0
	mov	r0, r7
	bl	sprintf
	ldr	r2, [r5, #3844]
	ldr	r1, .L618+144
	add	r7, r7, r0
	mov	r0, r7
	bl	sprintf
	movw	r3, #4046
	ldrh	r2, [r5, r3]
	ldr	r1, .L618+148
	add	r7, r7, r0
	mov	r0, r7
	bl	sprintf
	mov	r3, #3856
	ldrh	r2, [r5, r3]
	ldr	r1, .L618+152
	add	r7, r7, r0
	mov	r0, r7
	bl	sprintf
	ldr	r3, .L618+156
	ldr	r1, .L618+160
	ldrh	r2, [r4, r3]
	add	r7, r7, r0
	mov	r0, r7
	bl	sprintf
	ldr	r2, [r5, #3860]
	ldr	r1, .L618+164
	add	r7, r7, r0
	mov	r0, r7
	bl	sprintf
	ldr	r3, .L618+168
	ldr	r1, .L618+172
	ldrh	r2, [r4, r3]
	add	r7, r7, r0
	mov	r0, r7
	bl	sprintf
	movw	r3, #3980
	ldrh	r2, [r5, r3]
	ldr	r1, .L618+176
	add	r7, r7, r0
	mov	r0, r7
	bl	sprintf
	ldr	r3, .L618+180
	ldr	r1, .L618+184
	ldrh	r2, [r4, r3]
	add	r7, r7, r0
	mov	r0, r7
	bl	sprintf
	ldr	r1, .L618+188
	ldrb	r2, [r4, #-1990]	@ zero_extendqisi2
	add	r7, r7, r0
	mov	r0, r7
	bl	sprintf
	ldrh	r2, [r4, r8]
	ldr	r1, .L618+192
	add	r7, r7, r0
	mov	r0, r7
	bl	sprintf
	ldr	r1, .L618+196
	ldrb	r2, [r4, #-1988]	@ zero_extendqisi2
	add	r7, r7, r0
	mov	r0, r7
	bl	sprintf
	ldr	r3, .L618+200
	ldr	r1, .L618+204
	ldrh	r2, [r4, r3]
	add	r7, r7, r0
	mov	r0, r7
	bl	sprintf
	ldrh	r3, [r4, r8]
	ldr	r2, [r4, #-2016]
	add	r8, r8, #48
	ldr	r1, .L618+208
	mov	r3, r3, asl #1
	ldrh	r2, [r2, r3]
	add	r7, r7, r0
	mov	r0, r7
	bl	sprintf
	ldr	r3, .L618+212
	ldr	r1, .L618+216
	ldrh	r2, [r4, r3]
	add	r7, r7, r0
	mov	r0, r7
	bl	sprintf
	ldr	r1, .L618+220
	ldrb	r2, [r4, #-1942]	@ zero_extendqisi2
	add	r7, r7, r0
	mov	r0, r7
	bl	sprintf
	ldrh	r2, [r4, r8]
	ldr	r1, .L618+224
	add	r7, r7, r0
	mov	r0, r7
	bl	sprintf
	ldr	r1, .L618+228
	ldrb	r2, [r4, #-1940]	@ zero_extendqisi2
	add	r7, r7, r0
	mov	r0, r7
	bl	sprintf
	ldr	r3, .L618+232
	ldr	r1, .L618+236
	ldrh	r2, [r4, r3]
	add	r7, r7, r0
	mov	r0, r7
	bl	sprintf
	ldrh	r3, [r4, r8]
	ldr	r2, [r4, #-2016]
	add	r8, r8, #188
	ldr	r1, .L618+240
	mov	r3, r3, asl #1
	ldrh	r2, [r2, r3]
	add	r7, r7, r0
	mov	r0, r7
	bl	sprintf
	ldr	r3, .L618+244
	ldr	r1, .L618+248
	ldrh	r2, [r4, r3]
	add	r7, r7, r0
	mov	r0, r7
	bl	sprintf
	ldr	r1, .L618+252
	ldrb	r2, [r4, #-1894]	@ zero_extendqisi2
	add	r7, r7, r0
	mov	r0, r7
	bl	sprintf
	ldr	r3, .L618+256
	ldr	r1, .L618+260
	ldrh	r2, [r4, r3]
	add	r7, r7, r0
	mov	r0, r7
	bl	sprintf
	ldr	r1, .L618+264
	ldrb	r2, [r4, #-1892]	@ zero_extendqisi2
	add	r7, r7, r0
	mov	r0, r7
	bl	sprintf
	ldr	r3, .L618+268
	ldr	r1, .L618+272
	ldrh	r2, [r4, r3]
	add	r7, r7, r0
	mov	r0, r7
	bl	sprintf
	ldr	r3, .L618+276
	ldr	r1, .L618+280
	ldrh	r2, [r4, r3]
	add	r7, r7, r0
	mov	r0, r7
	bl	sprintf
	ldr	r1, .L618+284
	ldrb	r2, [r4, #-1754]	@ zero_extendqisi2
	add	r7, r7, r0
	mov	r0, r7
	bl	sprintf
	ldr	r1, .L618+288
	ldrh	r2, [r4, r8]
	add	r7, r7, r0
	mov	r0, r7
	bl	sprintf
	ldr	r1, .L618+292
	ldrb	r2, [r4, #-1752]	@ zero_extendqisi2
	add	r7, r7, r0
	mov	r0, r7
	bl	sprintf
	ldr	r3, .L618+296
	ldr	r1, .L618+300
	ldrh	r2, [r4, r3]
	add	r7, r7, r0
	mov	r0, r7
	bl	sprintf
	ldr	r1, [r4, #-1624]
	ldr	r3, [r5, #3840]
	ldr	r2, [r4, #-1712]
	str	r1, [sp, #0]
	ldr	r1, [r4, #-1632]
	orr	r2, r3, r2, asl #8
	str	r1, [sp, #4]
	ldr	r1, .L618+304
	ldr	r3, [r4, #-1628]
	add	r7, r7, r0
	mov	r0, r7
	bl	sprintf
	ldr	r1, .L618+308
	ldr	r2, [r4, #-1636]
	add	r5, r7, r0
	mov	r0, r5
	bl	sprintf
	ldr	r1, .L618+312
	ldr	r2, [r4, #-1612]
	add	r5, r5, r0
	mov	r0, r5
	bl	sprintf
	ldr	r3, .L618+316
	ldr	r1, .L618+320
	ldrh	r2, [r4, r3]
	add	r5, r5, r0
	mov	r0, r5
	bl	sprintf
	ldr	r3, .L618+324
	ldr	r1, .L618+328
	ldrh	r2, [r4, r3]
	add	r5, r5, r0
	mov	r0, r5
	bl	sprintf
	ldr	r1, .L618+332
	ldr	r2, [r4, #-1192]
	add	r5, r5, r0
	mov	r0, r5
	bl	sprintf
	ldr	r3, .L618+336
	ldr	r1, .L618+340
	ldrh	r2, [r4, r3]
	add	r5, r5, r0
	mov	r0, r5
	bl	sprintf
	add	r5, r5, r0
	bl	GetFreeBlockMinEraseCount
	ldr	r1, .L618+344
	mov	r2, r0
	mov	r0, r5
	bl	sprintf
	add	r5, r5, r0
	ldrh	r0, [r4, r6]
	bl	GetFreeBlockMaxEraseCount
	ldr	r1, .L618+348
	mov	r2, r0
	mov	r0, r5
	bl	sprintf
	ldrh	r3, [r4, r8]
	movw	r2, #65535
	cmp	r3, r2
	add	r5, r5, r0
	beq	.L611
	ldr	r2, [r4, #-2016]
	mov	r3, r3, asl #1
	mov	r0, r5
	ldr	r1, .L618+352
	ldrh	r2, [r2, r3]
	bl	sprintf
	add	r5, r5, r0
.L611:
	mov	r0, #0
	ldr	r4, .L618+32
	bl	List_get_gc_head_node
	mov	r6, #0
	movw	r8, #65535
	mov	r9, #6
	uxth	r3, r0
.L613:
	cmp	r3, r8
	beq	.L612
	ldr	r1, [r4, #-2016]
	mov	r2, r3, asl #1
	mul	r7, r9, r3
	mov	r0, r5
	ldrh	r1, [r1, r2]
	str	r1, [sp, #0]
	ldr	r1, [r4, #-2024]
	add	r1, r1, r7
	ldrh	r1, [r1, #4]
	str	r1, [sp, #4]
	ldr	r1, [r4, #-2080]
	ldrh	r2, [r1, r2]
	ldr	r1, .L618+356
	str	r2, [sp, #8]
	mov	r2, r6
	bl	sprintf
	add	r6, r6, #1
	ldr	r3, [r4, #-2024]
	cmp	r6, #16
	ldrh	r3, [r3, r7]
	add	r5, r5, r0
	bne	.L613
.L612:
	ldr	r6, .L618+32
	mov	r4, #0
	movw	r8, #65535
	mov	r9, #6
	ldr	r2, [r6, #-2004]
	ldr	r3, [r6, #-2024]
	rsb	r3, r3, r2
	ldr	r2, .L618+360
	mov	r3, r3, asr #1
	mul	r3, r2, r3
	uxth	r3, r3
.L615:
	cmp	r3, r8
	beq	.L614
	mul	r7, r9, r3
	ldr	r2, [r6, #-2024]
	mov	r0, r5
	add	r2, r2, r7
	ldrh	r2, [r2, #4]
	str	r2, [sp, #0]
	mov	r2, r3, asl #1
	ldr	r1, [r6, #-2080]
	ldrh	r2, [r1, r2]
	ldr	r1, .L618+364
	str	r2, [sp, #4]
	mov	r2, r4
	bl	sprintf
	add	r4, r4, #1
	ldr	r3, [r6, #-2024]
	cmp	r4, #4
	ldrh	r3, [r3, r7]
	add	r5, r5, r0
	bne	.L615
.L614:
	rsb	r0, sl, r5
.L610:
	add	sp, sp, #32
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, sl, pc}
.L619:
	.align	2
.L618:
	.word	.LANCHOR0
	.word	.LC8
	.word	.LC9
	.word	.LC10
	.word	.LANCHOR1
	.word	.LC11
	.word	-1996
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
	.word	-2000
	.word	.LC27
	.word	.LC28
	.word	.LC29
	.word	.LC30
	.word	.LC31
	.word	.LC32
	.word	-2042
	.word	.LC33
	.word	-2044
	.word	.LC34
	.word	.LC35
	.word	.LC36
	.word	.LC37
	.word	.LC38
	.word	.LC39
	.word	-1776
	.word	.LC40
	.word	.LC41
	.word	-1772
	.word	.LC42
	.word	.LC43
	.word	-1994
	.word	.LC44
	.word	.LC45
	.word	.LC46
	.word	.LC47
	.word	-1992
	.word	.LC48
	.word	.LC49
	.word	-1946
	.word	.LC50
	.word	.LC51
	.word	.LC52
	.word	.LC53
	.word	-1944
	.word	.LC54
	.word	.LC55
	.word	-1898
	.word	.LC56
	.word	.LC57
	.word	-1900
	.word	.LC58
	.word	.LC59
	.word	-1896
	.word	.LC60
	.word	-1758
	.word	.LC61
	.word	.LC62
	.word	.LC63
	.word	.LC64
	.word	-1756
	.word	.LC65
	.word	.LC66
	.word	.LC67
	.word	.LC68
	.word	-1196
	.word	.LC69
	.word	-1194
	.word	.LC70
	.word	.LC71
	.word	-1188
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
	ldr	r1, .L621
	ldr	r2, .L621+4
	bl	sprintf
	add	r5, r4, r0
	mov	r0, r5
	bl	FtlPrintInfo2buf
	add	r0, r5, r0
	rsb	r0, r4, r0
	ldmfd	sp!, {r3, r4, r5, pc}
.L622:
	.align	2
.L621:
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
	ldr	r3, .L648
	stmfd	sp!, {r4, r5, r6, r7, r8, r9, sl, fp, lr}
	.save {r4, r5, r6, r7, r8, r9, sl, fp, lr}
	.pad #20
	sub	sp, sp, #20
	ldr	r1, [r3, #-1788]
	ldr	r2, [r3, #-1780]
	cmp	r1, r2
	bcs	.L624
	ldr	r0, .L648+4
	mov	r1, #3856
	mov	r2, #0
	str	r2, [r3, #-1796]
	ldrh	r1, [r0, r1]
	ldr	r0, [r3, #-2080]
	b	.L625
.L626:
	ldrh	lr, [r0], #2
	add	r2, r2, #1
	ldr	ip, [r3, #-1796]
	add	ip, lr, ip
	str	ip, [r3, #-1796]
.L625:
	cmp	r2, r1
	ldr	r4, .L648
	bcc	.L626
	ldr	r5, [r4, #-1796]
	mov	r0, r5
	bl	__aeabi_uidiv
	ldr	r2, .L648+4
	movw	r3, #3906
	ldrh	r1, [r2, r3]
	str	r0, [r4, #-1788]
	ldr	r0, [r4, #-1792]
	rsb	r0, r0, r5
	bl	__aeabi_uidiv
	str	r0, [r4, #-1796]
	b	.L627
.L624:
	ldr	r2, [r3, #-1784]
	cmp	r1, r2
	bls	.L627
	add	r2, r2, #1
	ldr	r4, .L648+4
	str	r2, [r3, #-1784]
	mov	ip, #3856
	mov	r2, #0
	b	.L628
.L629:
	ldr	r0, [r3, #-2080]
	mov	r1, r2, asl #1
	add	r2, r2, #1
	ldrh	r5, [r0, r1]
	add	r5, r5, #1
	strh	r5, [r0, r1]	@ movhi
.L628:
	ldrh	r1, [r4, ip]
	cmp	r2, r1
	bcc	.L629
.L627:
	ldr	r3, .L648
	ldr	r6, [r3, #-1780]
	ldr	r5, [r3, #-1788]
	add	r2, r6, #256
	cmp	r2, r5
	mov	r2, r3
	bls	.L630
	ldr	r3, [r3, #-1784]
	add	r1, r6, #768
	cmp	r1, r3
	bls	.L630
	ldr	r3, .L648+4
	ldr	r3, [r3, #3840]
	cmp	r3, #0
	beq	.L647
	cmp	r6, #30
	bhi	.L647
.L630:
	ldr	r3, .L648+8
	ldrh	r0, [r2, r3]
	add	r0, r0, r0, asl #1
	ubfx	r0, r0, #2, #16
	bl	GetFreeBlockMaxEraseCount
	add	r3, r6, #64
	cmp	r0, r3
	mov	r8, r0
	bcs	.L632
	cmp	r6, #30
	bhi	.L647
.L632:
	ldr	r2, .L648
	ldr	r3, [r2, #-2020]
	cmp	r3, #0
	beq	.L647
	ldr	r0, .L648+4
	mov	r1, #3856
	movw	r7, #65535
	mov	r4, r7
	mov	fp, r7
	ldrh	sl, [r0, r1]
	ldr	r0, [r2, #-2024]
	ldr	r1, [r2, #-2080]
	mov	r2, #0
	b	.L633
.L636:
	add	r2, r2, #1
	uxth	r2, r2
	cmp	r2, sl
	bhi	.L647
	ldrh	ip, [r3, #4]
	cmp	ip, #0
	beq	.L634
	rsb	r3, r0, r3
	ldr	ip, .L648+12
	mov	r3, r3, asr #1
	mul	r3, ip, r3
	uxth	r3, r3
	mov	ip, r3, asl #1
	ldrh	ip, [r1, ip]
	cmp	ip, r6
	bls	.L644
	cmp	ip, r7
	movcc	r7, ip
	movcc	r4, r3
.L634:
	mov	ip, #6
	mla	r3, ip, r9, r0
.L633:
	ldrh	r9, [r3, #0]
	cmp	r9, fp
	bne	.L636
	b	.L635
.L644:
	mov	r4, r3
.L635:
	movw	r3, #65535
	cmp	r4, r3
	beq	.L631
	mov	r9, r4, asl #1
	ldrh	sl, [r1, r9]
	cmp	sl, r6
	bls	.L637
	bl	GetFreeBlockMinEraseCount
	cmp	r0, r6
	ldrhi	r3, .L648
	strhi	r7, [r3, #-1780]
.L637:
	cmp	sl, r5
	bcs	.L647
	add	r3, sl, #128
	cmp	r8, r3
	ble	.L647
	add	r3, sl, #256
	ldr	r6, .L648
	cmp	r3, r5
	bcc	.L638
	ldr	r3, [r6, #-1784]
	add	sl, sl, #768
	cmp	sl, r3
	bcs	.L647
.L638:
	ldr	r3, [r6, #-2016]
	mov	r1, r4
	ldr	r0, .L648+16
	mov	r2, r5
	ldrh	r3, [r3, r9]
	str	r3, [sp, #0]
	ldr	r3, [r6, #-2080]
	ldrh	r3, [r3, r9]
	stmib	sp, {r3, r8}
	ldr	r3, [r6, #-1784]
	bl	printk
	mov	r3, #1
	str	r3, [r6, #-1184]
	b	.L631
.L647:
	movw	r4, #65535
.L631:
	mov	r0, r4
	add	sp, sp, #20
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, sl, fp, pc}
.L649:
	.align	2
.L648:
	.word	.LANCHOR2
	.word	.LANCHOR0
	.word	-2000
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
	beq	.L651
	ldr	r2, .L652
	mov	r3, r0, asl #1
	mov	r1, #0
	ldr	r2, [r2, #-2016]
	strh	r1, [r2, r3]	@ movhi
	bl	INSERT_FREE_LIST
.L651:
	mov	r0, #0
	ldmfd	sp!, {r3, pc}
.L653:
	.align	2
.L652:
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
	ldr	r2, .L662
	mov	r3, #0
	stmfd	sp!, {r4, r5, r6, r7, r8, r9, sl, fp, lr}
	.save {r4, r5, r6, r7, r8, r9, sl, fp, lr}
	movw	sl, #3848
	str	r3, [r2, #-1180]
	mov	r8, #12
	ldr	r0, .L662+4
	mov	r7, #1
	movw	r6, #3926
	movw	fp, #3928
	b	.L655
.L656:
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
.L655:
	ldrh	r1, [r0, sl]
	cmp	r3, r1
	bcc	.L656
	b	.L661
.L658:
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
	b	.L660
.L661:
	ldr	r3, .L662
	mov	r8, #12
	ldr	ip, .L662+4
	mov	r7, #0
	movw	r6, #3926
	movw	r5, #3928
.L660:
	ldr	r2, [r3, #-1160]
	cmp	r1, r2
	bcc	.L658
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, sl, fp, pc}
.L663:
	.align	2
.L662:
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
	ldr	r3, .L671
	stmfd	sp!, {r4, r5, r6, r7, r8, r9, sl, fp, lr}
	.save {r4, r5, r6, r7, r8, r9, sl, fp, lr}
	mov	r5, #36
	ldr	r6, [r3, #-1160]
	mov	fp, #12
	ldr	ip, [r3, #-1176]
	mov	r3, #0
	mov	r4, r3
	b	.L665
.L668:
	mul	sl, fp, r2
	add	r8, ip, sl
	ldr	r9, [ip, sl]
	ldr	sl, [r7, #8]
	cmp	r9, sl
	streq	r4, [r8, #8]
	beq	.L667
.L666:
	add	r2, r2, #1
	uxth	r2, r2
.L670:
	cmp	r2, r6
	bcc	.L668
.L667:
	add	r3, r3, #1
	uxth	r3, r3
.L665:
	cmp	r3, r1
	ldmcsfd	sp!, {r4, r5, r6, r7, r8, r9, sl, fp, pc}
	mla	r7, r5, r3, r0
	mov	r2, #0
	b	.L670
.L672:
	.align	2
.L671:
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
	ldr	r3, .L680
	stmfd	sp!, {r4, r5, r6, r7, r8, sl, lr}
	.save {r4, r5, r6, r7, r8, sl, lr}
	mov	r6, #12
	ldr	r8, [r3, #-1160]
	mov	r5, #1
	ldr	r7, [r3, #-1176]
	mov	r4, #36
	mov	r3, #0
	b	.L674
.L677:
	mla	ip, r6, r2, r7
	ldr	sl, [ip, #8]
	cmp	sl, #0
	bne	.L675
	mla	r2, r4, r3, r0
	ldr	sl, [ip, #0]
	str	r5, [ip, #8]
	str	sl, [r2, #8]
	ldr	ip, [ip, #4]
	str	ip, [r2, #12]
	b	.L676
.L675:
	add	r2, r2, #1
	uxth	r2, r2
	b	.L678
.L679:
	mov	r2, #0
.L678:
	cmp	r2, r8
	bcc	.L677
.L676:
	add	r3, r3, #1
	uxth	r3, r3
.L674:
	cmp	r3, r1
	bcc	.L679
	ldmfd	sp!, {r4, r5, r6, r7, r8, sl, pc}
.L681:
	.align	2
.L680:
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
	ldr	r3, .L687
	ldr	r2, .L687+4
	ldrh	r1, [r3, r2]
	ldr	r2, [r3, #-1152]
	mov	r3, #0
	b	.L683
.L685:
	ldrh	ip, [r2], #2
	cmp	ip, r0
	beq	.L686
	add	r3, r3, #1
	uxth	r3, r3
.L683:
	cmp	r3, r1
	bne	.L685
	mov	r0, #0
	bx	lr
.L686:
	mov	r0, #1
	bx	lr
.L688:
	.align	2
.L687:
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
	ldr	r3, .L693
	ldr	r2, .L693+4
	ldrh	ip, [r3, r2]
	ldr	r2, [r3, #-1152]
	mov	r3, #0
	mov	r1, r2
	b	.L690
.L692:
	ldrh	r7, [r1], #2
	cmp	r7, r0
	beq	.L691
	add	r3, r3, #1
	uxth	r3, r3
.L690:
	cmp	r3, ip
	bne	.L692
	mov	r3, r3, asl #1
	strh	r0, [r2, r3]	@ movhi
	ldr	r2, .L693
	ldr	r3, .L693+4
	ldrh	r1, [r2, r3]
	add	r1, r1, #1
	strh	r1, [r2, r3]	@ movhi
.L691:
	ldr	r3, .L693
	mov	r0, #12
	ldr	r2, .L693+8
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
.L694:
	.align	2
.L693:
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
	stmfd	sp!, {r3, r4, r5, lr}
	.save {r3, r4, r5, lr}
	mov	r4, r0
	mov	r1, r4
	ldr	r0, .L698
	bl	printk
	ldr	r3, .L698+4
	ldr	ip, .L698+8
	ldrh	r5, [r3, ip]
	cmp	r5, r4
	beq	.L696
	ldr	r2, .L698+12
	ldrh	r0, [r3, r2]
	cmp	r0, r4
	beq	.L696
	movw	r1, #65535
	cmp	r5, r1
	streqh	r4, [r3, ip]	@ movhi
	beq	.L696
	cmp	r0, r1
	streqh	r4, [r3, r2]	@ movhi
.L696:
	mov	r0, #0
	ldmfd	sp!, {r3, r4, r5, pc}
.L699:
	.align	2
.L698:
	.word	.LC81
	.word	.LANCHOR2
	.word	-1142
	.word	-1140
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
	ldr	r0, .L703
	bl	printk
	ldr	r3, .L703+4
	ldr	ip, .L703+8
	ldrh	r5, [r3, ip]
	cmp	r5, r4
	beq	.L701
	ldr	r2, .L703+12
	ldrh	r0, [r3, r2]
	cmp	r0, r4
	beq	.L701
	movw	r1, #65535
	cmp	r5, r1
	streqh	r4, [r3, ip]	@ movhi
	beq	.L701
	cmp	r0, r1
	streqh	r4, [r3, r2]	@ movhi
.L701:
	mov	r0, #0
	ldmfd	sp!, {r3, r4, r5, pc}
.L704:
	.align	2
.L703:
	.word	.LC81
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
	ldr	r7, .L710
	mov	r2, r4
	ldr	r6, .L710+4
	ldrh	r1, [r7, r6]
	mov	r5, r0
	ldr	r0, .L710+8
	bl	printk
	mov	r0, r5
	bl	FtlGcRefreshBlock
	ldr	r3, .L710+12
	ldr	r3, [r3, #3840]
	cmp	r3, #0
	beq	.L706
	ldr	r3, [r7, #-2080]
	mov	r5, r5, asl #1
	ldrh	r2, [r3, r5]
	cmp	r2, #29
	subhi	r2, r2, #30
	strhih	r2, [r3, r5]	@ movhi
.L706:
	ldrh	r1, [r7, r6]
	mov	r3, #0
	ldr	r2, .L710+16
	b	.L707
.L709:
	ldrh	r0, [r2, #2]!
	cmp	r0, r4
	beq	.L708
	add	r3, r3, #1
	uxth	r3, r3
.L707:
	cmp	r3, r1
	bne	.L709
	cmp	r3, #15
	bhi	.L708
	ldr	r2, .L710
	add	r1, r2, r3, asl #1
	add	r3, r3, #1
	sub	r1, r1, #1136
	strh	r4, [r1, #0]	@ movhi
	ldr	r1, .L710+4
	strh	r3, [r2, r1]	@ movhi
.L708:
	mov	r0, #0
	ldmfd	sp!, {r3, r4, r5, r6, r7, pc}
.L711:
	.align	2
.L710:
	.word	.LANCHOR2
	.word	-1138
	.word	.LC82
	.word	.LANCHOR0
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
	ldr	r2, .L715
	ldr	r3, .L715+4
	ldrh	r2, [r3, r2]
	cmp	r2, #0
	beq	.L713
	ldr	r1, .L715+8
	ldrh	r0, [r3, r1]
	movw	r1, #65535
	cmp	r0, r1
	bne	.L713
	ldr	r4, .L715+12
	ldr	r5, .L715+4
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
.L713:
	mov	r0, #0
	ldmfd	sp!, {r3, r4, r5, pc}
.L716:
	.align	2
.L715:
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
	beq	.L718
	bl	memset
.L718:
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
	ldr	r4, .L720
	mov	r5, #3920
	ldr	r2, .L720+4
	mov	r1, #255
	ldr	r6, .L720+8
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
.L721:
	.align	2
.L720:
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
	ldr	r7, .L732
	mov	r2, #6
	ldr	r4, .L732+4
	mov	r1, #0
	mov	r6, #0
	ldrh	r3, [r7, r3]
	mov	sl, r6
	ldr	r0, [r4, #-2024]
	mul	r2, r2, r3
	bl	ftl_memset
	ldr	r3, .L732+8
	str	r6, [r4, #-2004]
	str	r6, [r4, #-2020]
	strh	r6, [r4, r3]	@ movhi
	add	r3, r3, #8
	str	r6, [r4, #-2012]
	strh	r6, [r4, r3]	@ movhi
	add	r3, r3, #900
	strh	r6, [r4, r3]	@ movhi
	mov	r4, r6
	b	.L723
.L725:
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
	b	.L730
.L731:
	mov	r5, #0
	uxth	fp, r4
	mov	r9, r5
	movw	r3, #3916
.L730:
	movw	r0, #3848
	sxth	r2, r9
	ldrh	r1, [r7, r0]
	cmp	r2, r1
	blt	.L725
	cmp	r5, #0
	ldr	r9, .L732+4
	beq	.L726
	sxth	r1, r5
	mov	r0, #32768
	bl	__aeabi_idiv
	uxth	r5, r0
	b	.L727
.L726:
	sxth	r3, r4
	ldr	r2, [r9, #-2016]
	mvn	r1, #0
	mov	r3, r3, asl #1
	strh	r1, [r2, r3]	@ movhi
.L727:
	sxth	r1, r4
	ldr	r0, [r9, #-2024]
	ldr	r3, .L732+4
	mov	r2, r1, asl #1
	add	r1, r2, r1
	add	r1, r0, r1, asl #1
	strh	r5, [r1, #4]	@ movhi
	ldr	r1, .L732+12
	ldrh	r1, [r9, r1]
	cmp	r8, r1
	beq	.L728
	ldr	r1, .L732+16
	ldrh	r1, [r3, r1]
	cmp	r8, r1
	beq	.L728
	ldr	r1, .L732+20
	ldrh	r1, [r3, r1]
	cmp	r8, r1
	beq	.L728
	ldr	r3, [r3, #-2016]
	uxth	r0, r4
	ldrh	r3, [r3, r2]
	cmp	r3, #0
	bne	.L729
	add	r6, r6, #1
	uxth	r6, r6
	bl	INSERT_FREE_LIST
	b	.L728
.L729:
	add	sl, sl, #1
	uxth	sl, sl
	bl	INSERT_DATA_LIST
.L728:
	add	r4, r4, #1
	uxth	r4, r4
.L723:
	mov	r3, #3856
	sxth	r8, r4
	ldrh	r3, [r7, r3]
	cmp	r8, r3
	blt	.L731
	ldr	r3, .L732+4
	mov	r0, #0
	ldr	r2, .L732+8
	strh	sl, [r3, r2]	@ movhi
	add	r2, r2, #8
	strh	r6, [r3, r2]	@ movhi
	ldmfd	sp!, {r1, r2, r3, r4, r5, r6, r7, r8, r9, sl, fp, pc}
.L733:
	.align	2
.L732:
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
	ldr	r4, .L737
	movw	r6, #3926
	ldr	r5, .L737+4
	movw	r7, #3954
	mov	r8, #12
	ldr	r2, [r4, #3944]
	ldr	r0, [r5, #-1096]
	mov	r2, r2, asl #1
	bl	ftl_memset
	ldrh	r3, [r4, r6]
	ldrh	r2, [r4, r7]
	mov	r1, #255
	ldr	r0, [r5, #-1092]
	mul	r2, r2, r3
	bl	ftl_memset
	mov	r2, #0
	mov	ip, r2
	mvn	r0, #0
	b	.L735
.L736:
	mul	r3, r8, r2
	ldr	r1, [r5, #-1852]
	add	sl, r1, r3
	str	ip, [sl, #4]
	strh	r0, [r1, r3]	@ movhi
	ldr	r1, [r5, #-1852]
	ldr	sl, [r5, #-1092]
	add	r3, r1, r3
	ldrh	r1, [r4, r6]
	mul	r1, r2, r1
	add	r2, r2, #1
	uxth	r2, r2
	bic	r1, r1, #3
	add	r1, sl, r1
	str	r1, [r3, #8]
.L735:
	ldrh	r3, [r4, r7]
	ldr	r1, .L737
	cmp	r3, r2
	ldr	r3, .L737+4
	bhi	.L736
	ldr	r0, .L737+8
	mvn	r2, #0
	ldr	ip, [r1, #3944]
	strh	r2, [r3, r0]	@ movhi
	ldr	r0, .L737+12
	strh	r2, [r3, r0]	@ movhi
	add	r0, r0, #10
	strh	ip, [r3, r0]	@ movhi
	sub	r0, r0, #6
	ldr	ip, .L737+16
	strh	ip, [r3, r0]	@ movhi
	add	r0, r0, #40
	ldrh	ip, [r3, r0]
	sub	r0, r0, #36
	strh	ip, [r3, r0]	@ movhi
	mov	r0, #3952
	ldrh	r0, [r1, r0]
	ldr	r1, .L737+20
	strh	r0, [r3, r1]	@ movhi
	ldr	r1, [r3, #-1040]
	str	r1, [r3, #-1076]
	ldr	r1, [r3, #-1036]
	str	r1, [r3, #-1072]
	ldr	r1, [r3, #-1096]
	str	r1, [r3, #-1068]
	ldr	r1, [r3, #-1032]
	str	r1, [r3, #-1064]
	ldr	r1, .L737+24
	strh	r2, [r3, r1]	@ movhi
	ldmfd	sp!, {r3, r4, r5, r6, r7, r8, sl, pc}
.L738:
	.align	2
.L737:
	.word	.LANCHOR0
	.word	.LANCHOR2
	.word	-1086
	.word	-1088
	.word	-3902
	.word	-1082
	.word	-1048
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
	b	.L740
.L744:
	ldr	r0, [r7, r3, asl #2]
	mov	r2, #0
	ubfx	r0, r0, #10, #16
	b	.L741
.L743:
	mov	r1, r2, asl #1
	add	r2, r2, #1
	ldrh	ip, [r6, r1]
	uxth	r2, r2
	cmp	ip, r0
	ldreqh	ip, [r5, r1]
	addeq	ip, ip, #1
	streqh	ip, [r5, r1]	@ movhi
.L741:
	ldrh	r1, [r4, #10]
	cmp	r1, r2
	bhi	.L743
	add	r3, r3, #1
	uxth	r3, r3
.L740:
	ldrh	r2, [r4, #6]
	cmp	r2, r3
	bhi	.L744
	mov	sl, #0
	ldrh	fp, [r5, #0]
	mov	r7, sl
	ldr	r3, .L750
	movw	r2, #3918
	b	.L745
.L749:
	ldrh	r1, [r4, #0]
	cmp	r1, r7
	bne	.L746
	ldrh	r0, [r4, #2]
	ldrh	r1, [r3, r2]
	cmp	r0, r1
	movcc	r0, r7, asl #1
	strcch	r1, [r5, r0]	@ movhi
.L746:
	mov	r9, r7, asl #1
	ldrh	r8, [r5, r9]
	cmp	fp, r8
	movhi	sl, r7
	movhi	fp, r8
	cmp	r8, #0
	bne	.L748
	ldrh	r0, [r6, r9]
	cmp	r0, #0
	beq	.L748
	mov	r1, #1
	stmia	sp, {r2, r3}
	bl	FtlFreeSysBlkQueueIn
	strh	r8, [r6, r9]	@ movhi
	ldmia	sp, {r2, r3}
	ldrh	r1, [r4, #8]
	sub	r1, r1, #1
	strh	r1, [r4, #8]	@ movhi
.L748:
	add	r7, r7, #1
	uxth	r7, r7
.L745:
	ldrh	r1, [r4, #10]
	cmp	r1, r7
	bhi	.L749
	mov	r0, sl
	ldmfd	sp!, {r1, r2, r3, r4, r5, r6, r7, r8, r9, sl, fp, pc}
.L751:
	.align	2
.L750:
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
	ldr	r3, .L753
	movw	r2, #4042
	stmfd	sp!, {r4, lr}
	.save {r4, lr}
	mov	r4, #0
	strh	r4, [r3, r2]	@ movhi
	add	r2, r2, #2
	mov	r1, r4
	strh	r4, [r3, r2]	@ movhi
	add	r2, r2, #2
	strh	r4, [r3, r2]	@ movhi
	movw	r2, #4040
	strh	r0, [r3, r2]	@ movhi
	add	r0, r3, #4048
	mov	r2, #2048
	bl	ftl_memset
	mov	r0, r4
	ldmfd	sp!, {r4, pc}
.L754:
	.align	2
.L753:
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
	ldr	r3, .L756
	movw	r2, #3980
	mvn	r1, #0
	ldr	r0, .L756+4
	strh	r1, [r3, r2]	@ movhi
	add	r2, r2, #6
	mov	r1, #0
	strh	r1, [r3, r2]	@ movhi
	mov	r1, #255
	mov	r2, #16
	b	ftl_memset
.L757:
	.align	2
.L756:
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
	ldr	r3, .L762
	ldr	r2, .L762+4
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
.L760:
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
	bne	.L760
	ldmfd	sp!, {r4, r5, r6, pc}
.L763:
	.align	2
.L762:
	.word	-1028
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
	ldr	r5, .L765
	mov	r4, #0
	ldr	r2, .L765+4
	mov	r1, r4
	ldr	r6, .L765+8
	movw	r7, #3858
	str	r3, [r5, #-1008]
	strh	r3, [r5, r2]	@ movhi
	movw	r3, #3962
	strh	r4, [r6, r3]	@ movhi
	mov	r3, #3936
	ldrh	r2, [r6, r3]
	ldr	r0, [r6, #3964]
	str	r4, [r5, #-1024]
	mov	r2, r2, asl #1
	str	r4, [r5, #-1016]
	str	r4, [r5, #-1012]
	str	r4, [r6, #3840]
	bl	ftl_memset
	ldrh	r2, [r6, r7]
	mov	r1, r4
	ldr	r0, [r5, #-2080]
	mov	r2, r2, asl #1
	bl	ftl_memset
	ldrh	r2, [r6, r7]
	mov	r1, r4
	ldr	r0, [r5, #-1004]
	mov	r2, r2, asl #1
	bl	ftl_memset
	mov	r1, r4
	mov	r2, #48
	ldr	r0, .L765+12
	bl	ftl_memset
	mov	r1, r4
	mov	r2, #512
	ldr	r0, .L765+16
	bl	ftl_memset
	bl	FtlGcBufInit
	bl	FtlL2PDataInit
	mov	r0, r4
	ldmfd	sp!, {r3, r4, r5, r6, r7, pc}
.L766:
	.align	2
.L765:
	.word	.LANCHOR2
	.word	-1020
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
	ldr	r4, .L798
	mov	r0, #1024
	ldr	r3, .L798+4
	mov	r7, #12
	ldr	r2, .L798+8
	movw	sl, #3848
	ldr	r5, .L798+12
	mov	r8, #36
	strh	r6, [r4, r3]	@ movhi
	movw	r3, #65535
	str	r3, [r4, #-992]
	mvn	r3, #0
	strh	r3, [r4, r2]	@ movhi
	add	r2, r2, #2
	str	r6, [r4, #-1000]
	strh	r3, [r4, r2]	@ movhi
	mov	r2, #32
	ldr	r3, .L798+16
	str	r6, [r4, #-1712]
	str	r6, [r4, #-1804]
	strh	r2, [r4, r3]	@ movhi
	add	r3, r3, #2
	mov	r2, #128
	str	r6, [r4, #-1800]
	strh	r2, [r4, r3]	@ movhi
	add	r3, r3, #6
	str	r6, [r4, #-1816]
	strh	r6, [r4, r3]	@ movhi
	add	r3, r3, #50
	str	r6, [r4, #-1828]
	strh	r6, [r4, r3]	@ movhi
	add	r3, r3, #158
	str	r6, [r4, #-1832]
	strh	r6, [r4, r3]	@ movhi
	sub	r3, r3, #122
	str	r6, [r4, #-1824]
	strh	r6, [r4, r3]	@ movhi
	movw	r3, #3922
	ldrh	r1, [r5, r3]
	str	r6, [r4, #-1820]
	str	r6, [r4, #-1836]
	str	r6, [r4, #-1796]
	str	r6, [r4, #-1792]
	str	r6, [r4, #-2076]
	str	r6, [r4, #-1784]
	str	r6, [r4, #-1780]
	str	r6, [r4, #-996]
	str	r6, [r4, #-1184]
	str	r6, [r4, #-988]
	str	r6, [r4, #-1192]
	str	r6, [r4, #-984]
	bl	__aeabi_idiv
	movw	r3, #3848
	ldrh	r3, [r5, r3]
	str	r6, [r5, #3968]
	mov	r6, #3920
	mov	r3, r3, asl #2
	cmp	r0, r3
	str	r0, [r4, #-976]
	ldrh	r0, [r5, r6]
	strhi	r3, [r4, #-976]
	ldr	r4, .L798
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
	str	r0, [r4, #-972]
	mov	r0, r6
	bl	ftl_malloc
	str	r0, [r4, #-968]
	mov	r0, r9
	bl	ftl_malloc
	str	r0, [r4, #-964]
	mov	r0, r6
	bl	ftl_malloc
	str	r0, [r4, #-2084]
	mov	r0, r6
	bl	ftl_malloc
	str	r0, [r4, #-1164]
	ldr	r0, [r4, #-976]
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
	str	r0, [r4, #-960]
	mov	r0, r6
	bl	ftl_malloc
	str	r0, [r4, #-956]
	mov	r0, r6
	bl	ftl_malloc
	str	r0, [r4, #-952]
	ldr	r0, [r4, #-1160]
	mul	r0, r0, r6
	bl	ftl_malloc
	str	r0, [r4, #-1172]
	ldr	r0, [r4, #-976]
	mul	r0, r0, r6
	bl	ftl_malloc
	str	r0, [r4, #-948]
	mov	r0, r6
	bl	ftl_malloc
	str	r0, [r4, #-944]
	mov	r0, r6
	bl	ftl_malloc
	movw	r6, #3928
	str	r0, [r4, #-940]
	ldr	r0, [r4, #-1160]
	mul	r0, r7, r0
	bl	ftl_malloc
	ldrh	r3, [r5, r6]
	ldrh	sl, [r5, sl]
	mul	sl, sl, r3
	str	r0, [r4, #-1176]
	mov	r0, sl
	bl	ftl_malloc
	str	r0, [r4, #-936]
	mov	r0, sl, asl #3
	bl	ftl_malloc
	ldrh	r3, [r5, r6]
	ldr	sl, .L798+20
	str	r0, [r4, #-932]
	ldr	r0, [r4, #-1160]
	mul	r0, r0, r3
	bl	ftl_malloc
	ldrh	r3, [r5, r6]
	movw	r6, #3858
	str	r0, [r4, #-1168]
	ldr	r0, [r4, #-976]
	mul	r0, r0, r3
	bl	ftl_malloc
	str	r0, [r4, #-928]
	ldrh	r0, [r5, r6]
	mov	r0, r0, asl #1
	uxth	r0, r0
	strh	r0, [r4, sl]	@ movhi
	bl	ftl_malloc
	str	r0, [r4, #-1004]
	ldrh	r0, [r4, sl]
	add	r0, r0, #544
	add	r0, r0, #3
	mov	r0, r0, lsr #9
	strh	r0, [r4, sl]	@ movhi
	mov	r0, r0, asl #9
	bl	ftl_malloc
	ldrh	sl, [r5, r6]
	mov	sl, sl, asl #1
	str	r0, [r4, #-920]
	add	r0, r0, #32
	str	r0, [r4, #-2080]
	mov	r0, sl
	bl	ftl_malloc
	str	r0, [r4, #-916]
	mov	r0, sl
	bl	ftl_malloc
	ldr	sl, [r5, #3944]
	mov	sl, sl, asl #1
	str	r0, [r4, #-2016]
	mov	r0, sl
	bl	ftl_malloc
	str	r0, [r4, #-1040]
	mov	r0, sl
	bl	ftl_malloc
	mov	sl, #3936
	str	r0, [r4, #-1096]
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
	str	r0, [r4, #-912]
	ldrh	r0, [r5, sl]
	add	sl, sl, #2
	mov	r0, r0, asl #2
	bl	ftl_malloc
	str	r0, [r4, #-908]
	ldrh	r0, [r5, sl]
	mov	r0, r0, asl #2
	bl	ftl_malloc
	ldrh	r2, [r5, sl]
	mov	r1, #0
	mov	r2, r2, asl #2
	str	r0, [r4, #-904]
	bl	ftl_memset
	mov	r3, #3952
	ldrh	sl, [r5, r3]
	mov	sl, sl, asl #2
	mov	r0, sl
	bl	ftl_malloc
	str	r0, [r4, #-1032]
	mov	r0, sl
	bl	ftl_malloc
	movw	sl, #3954
	str	r0, [r4, #-900]
	ldr	r0, [r5, #3944]
	mov	r0, r0, asl #2
	bl	ftl_malloc
	str	r0, [r4, #-1036]
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
	ldr	r6, .L798+24
	str	r0, [r4, #-1092]
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
	ldr	ip, .L798+12
	mov	r1, r1, asl #2
	mov	r2, r1
	str	r0, [r5, #4008]
	ldrh	r5, [r5, r7]
	ldr	r0, .L798+28
	b	.L769
.L770:
	ldr	r4, [ip, #4008]
	add	r3, r3, #1
	add	r4, r4, r2
	add	r2, r2, r1
	str	r4, [r0, #4]!
.L769:
	cmp	r3, r5
	bcc	.L770
	ldr	r0, .L798+32
	mov	r2, #0
	mov	r1, r2
	add	r0, r0, r3, asl #2
	b	.L771
.L772:
	add	ip, r0, r2
	add	r3, r3, #1
	add	r2, r2, #4
	str	r1, [ip, #28]
.L771:
	cmp	r3, #7
	bls	.L772
	ldr	r3, .L798
	ldr	r2, [r3, #-1040]
	cmp	r2, #0
	beq	.L797
.L773:
	ldr	r2, [r3, #-1096]
	cmp	r2, #0
	beq	.L797
.L775:
	ldr	r2, [r3, #-1032]
	cmp	r2, #0
	beq	.L797
.L776:
	ldr	r2, [r3, #-1036]
	cmp	r2, #0
	beq	.L797
.L777:
	ldr	r2, [r3, #-1852]
	cmp	r2, #0
	beq	.L797
.L778:
	ldr	r2, [r3, #-1092]
	cmp	r2, #0
	beq	.L797
.L779:
	ldr	r2, [r3, #-2024]
	cmp	r2, #0
	beq	.L797
.L780:
	ldr	r2, .L798+12
	ldr	r2, [r2, #4008]
	cmp	r2, #0
	beq	.L797
.L781:
	ldr	r3, [r3, #-2016]
	cmp	r3, #0
	beq	.L797
.L782:
	ldr	r3, .L798
	ldr	r2, [r3, #-1152]
	cmp	r2, #0
	beq	.L797
	ldr	r2, [r3, #-1148]
	cmp	r2, #0
	beq	.L797
.L784:
	ldr	r2, [r3, #-972]
	cmp	r2, #0
	beq	.L797
	ldr	r2, [r3, #-964]
	cmp	r2, #0
	beq	.L797
	ldr	r2, [r3, #-2084]
	cmp	r2, #0
	beq	.L797
	ldr	r2, [r3, #-1164]
	cmp	r2, #0
	beq	.L797
	ldr	r2, [r3, #-968]
	cmp	r2, #0
	beq	.L797
.L786:
	ldr	r2, [r3, #-960]
	cmp	r2, #0
	beq	.L797
	ldr	r2, [r3, #-956]
	cmp	r2, #0
	beq	.L797
	ldr	r3, [r3, #-952]
	cmp	r3, #0
	beq	.L797
	ldr	r3, .L798
	ldr	r2, [r3, #-1172]
	cmp	r2, #0
	beq	.L797
	ldr	r2, [r3, #-944]
	cmp	r2, #0
	beq	.L797
	ldr	r2, [r3, #-940]
	cmp	r2, #0
	beq	.L797
	ldr	r2, [r3, #-1176]
	cmp	r2, #0
	beq	.L797
.L788:
	ldr	r2, [r3, #-936]
	cmp	r2, #0
	beq	.L797
	ldr	r2, [r3, #-932]
	cmp	r2, #0
	beq	.L797
	ldr	r2, [r3, #-1168]
	cmp	r2, #0
	beq	.L797
.L790:
	ldr	r2, [r3, #-2080]
	cmp	r2, #0
	beq	.L797
	ldr	r3, [r3, #-1004]
	cmp	r3, #0
	beq	.L797
.L792:
	ldr	r3, .L798+12
	ldr	r3, [r3, #3964]
	cmp	r3, #0
	beq	.L797
.L793:
	ldr	r3, .L798
	ldr	r2, [r3, #-912]
	cmp	r2, #0
	beq	.L797
.L794:
	ldr	r2, [r3, #-908]
	cmp	r2, #0
	beq	.L797
.L795:
	ldr	r3, [r3, #-904]
	cmp	r3, #0
	bne	.L796
.L797:
	ldr	r0, .L798+36
	ldr	r1, .L798+40
	bl	printk
	mvn	r0, #0
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, sl, pc}
.L796:
	mov	r0, #0
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, sl, pc}
.L799:
	.align	2
.L798:
	.word	.LANCHOR2
	.word	-1100
	.word	-1142
	.word	.LANCHOR0
	.word	-1196
	.word	-924
	.word	-1028
	.word	.LANCHOR0+4008
	.word	.LANCHOR0+3980
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
	ldr	r3, .L803
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
	ldr	r1, .L803+4
	b	.L801
.L802:
	ldrb	r5, [r3, r1]	@ zero_extendqisi2
	add	r3, r3, #1
	ldrb	ip, [r4, #10]	@ zero_extendqisi2
	orr	ip, ip, r2, asl r5
	strb	ip, [r4, #10]
.L801:
	uxtb	ip, r3
	cmp	ip, r0
	bcc	.L802
	ldmfd	sp!, {r3, r4, r5, pc}
.L804:
	.align	2
.L803:
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
	ldr	r1, .L817
	mov	r2, #0
	stmfd	sp!, {r3, r4, r5, r6, r7, r8, sl, lr}
	.save {r3, r4, r5, r6, r7, r8, sl, lr}
	ldr	r3, .L817+4
	strb	r2, [r1, #-896]
	ldr	r1, .L817+8
	strb	r2, [r3, #3762]
	movw	r2, #2902
	ldrh	r2, [r1, r2]
	cmp	r2, #256
	str	r2, [r3, #856]
	movhi	r2, #512
	bhi	.L816
	cmp	r2, #128
	bls	.L807
	mov	r2, #256
.L816:
	str	r2, [r3, #856]
.L807:
	mov	r1, #0
	mov	r2, #8
	ldr	r0, .L817+12
	mov	r6, #0
	bl	ftl_memset
	mov	r1, #0
	mov	r2, #32
	ldr	r0, .L817+16
	bl	ftl_memset
	ldr	r0, .L817+20
	mov	r1, #0
	mov	r2, #128
	bl	ftl_memset
	ldr	r7, .L817+24
	ldr	r5, .L817+4
	mov	r8, r7
.L809:
	ldr	r4, [r5, #3624]
	mov	r1, r8
	add	r0, r4, #1
	ldrb	r2, [r4, #0]	@ zero_extendqisi2
	bl	FlashMemCmp8
	cmp	r0, #0
	bne	.L808
	ldrb	r3, [r5, #3762]	@ zero_extendqisi2
	add	r2, r5, r3, asl #2
	str	r0, [r2, #3588]
	add	r2, r5, r3
	add	r3, r3, #1
	strb	r3, [r5, #3762]
	strb	r6, [r2, #3764]
.L808:
	add	r6, r6, #1
	add	r8, r8, #8
	cmp	r6, #4
	bne	.L809
	ldr	r5, .L817+4
	ldr	r3, .L817
	ldrb	r2, [r5, #3762]	@ zero_extendqisi2
	strb	r2, [r3, #-896]
	ldrb	r3, [r4, #8]	@ zero_extendqisi2
	cmp	r3, #2
	bne	.L810
	add	sl, r4, #1
	mov	r6, #0
	mov	r8, r5
.L813:
	mov	r0, sl
	mov	r1, r7
	ldrb	r2, [r4, #0]	@ zero_extendqisi2
	bl	FlashMemCmp8
	cmp	r0, #0
	bne	.L811
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
.L811:
	add	r6, r6, #1
	add	r7, r7, #8
	cmp	r6, #4
	bne	.L813
.L810:
	ldr	r3, .L817+4
	ldrb	r1, [r4, #13]	@ zero_extendqisi2
	ldr	r2, .L817
	ldrb	r3, [r3, #3762]	@ zero_extendqisi2
	mul	r1, r1, r3
	ldrh	r3, [r4, #14]
	mul	r1, r3, r1
	ldr	r3, .L817+28
	strh	r1, [r2, r3]	@ movhi
	ldmfd	sp!, {r3, r4, r5, r6, r7, r8, sl, pc}
.L818:
	.align	2
.L817:
	.word	.LANCHOR2
	.word	.LANCHOR0
	.word	.LANCHOR1
	.word	.LANCHOR0+3764
	.word	.LANCHOR0+3588
	.word	.LANCHOR0+3628
	.word	.LANCHOR0+2980
	.word	-894
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
	bne	.L820
	ldr	r3, .L859
.L821:
	strh	r0, [r3, #2]!	@ movhi
	add	r0, r0, #1
	uxth	r0, r0
	cmp	r0, #256
	bne	.L821
	b	.L822
.L820:
	cmp	r0, #1
	bne	.L823
	ldr	r0, .L859+4
	mov	r2, #0
	mov	r3, r2
.L826:
	uxth	r1, r3
	cmp	r1, #3
	bls	.L824
	tst	r3, #1
	moveq	r1, #2
	movne	r1, #3
	rsb	r1, r1, r2
	uxth	r1, r1
.L824:
	add	r3, r3, #1
	strh	r1, [r2, r0]	@ movhi
	cmp	r3, #256
	add	r2, r2, #2
	bne	.L826
	b	.L822
.L823:
	cmp	r0, #2
	bne	.L827
	mov	r3, #0
	ldr	r0, .L859
	movw	r2, #65535
	mov	r1, r3
	b	.L858
.L830:
	cmp	r3, #1
	movls	r1, r3
	movhi	r1, r2
.L858:
	add	r3, r3, #1
	add	r2, r2, #2
	strh	r1, [r0, #2]!	@ movhi
	uxth	r3, r3
	uxth	r2, r2
	cmp	r3, #256
	bne	.L830
	b	.L822
.L827:
	cmp	r0, #3
	bne	.L831
	ldr	r0, .L859+4
	mov	r2, #0
	mov	r3, r2
.L834:
	uxth	r1, r3
	cmp	r1, #5
	bls	.L832
	tst	r3, #1
	moveq	r1, #4
	movne	r1, #5
	rsb	r1, r1, r2
	uxth	r1, r1
.L832:
	add	r3, r3, #1
	strh	r1, [r2, r0]	@ movhi
	cmp	r3, #256
	add	r2, r2, #2
	bne	.L834
	b	.L822
.L831:
	cmp	r0, #4
	mov	r2, #0
	bne	.L835
	ldr	r3, .L859+8
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
	ldr	r0, .L859+12
	strh	r1, [r3, r2]	@ movhi
	mov	r2, #3088
	mov	r1, #7
	strh	r1, [r3, r2]	@ movhi
	add	r2, r2, #2
	mov	r1, #8
	strh	r1, [r3, r2]	@ movhi
	mov	r2, #16
	mov	r3, r1
.L837:
	tst	r3, #1
	add	r3, r3, #1
	moveq	r1, #6
	movne	r1, #7
	cmp	r3, #256
	rsb	r1, r1, r2
	add	r2, r2, #2
	strh	r1, [r0, #2]!	@ movhi
	uxth	r2, r2
	bne	.L837
	b	.L822
.L835:
	cmp	r0, #5
	bne	.L838
	ldr	r1, .L859
	mov	r3, r2
.L839:
	strh	r3, [r1, #2]!	@ movhi
	add	r3, r3, #1
	uxth	r3, r3
	cmp	r3, #16
	bne	.L839
	ldr	r2, .L859+16
.L840:
	strh	r3, [r2, #2]!	@ movhi
	add	r3, r3, #2
	uxth	r3, r3
	cmp	r3, #496
	bne	.L840
	b	.L822
.L838:
	cmp	r0, #6
	bne	.L822
	ldr	r0, .L859
	mov	r3, r2
.L843:
	uxth	r1, r3
	cmp	r1, #5
	bls	.L841
	tst	r3, #1
	moveq	r1, #10
	movne	r1, #12
	rsb	r1, r1, r2
	uxth	r1, r1
.L841:
	add	r3, r3, #1
	add	r2, r2, #3
	cmp	r3, #256
	strh	r1, [r0, #2]!	@ movhi
	uxth	r2, r2
	bne	.L843
.L822:
	mov	r2, #1024
	ldr	r0, .L859+20
	mov	r1, #255
	uxth	r4, r4
	bl	ftl_memset
	ldr	r2, .L859
	mov	r3, #0
	ldr	r0, .L859+24
	b	.L844
.L845:
	ldrh	r1, [r2, #2]!
	add	r3, r3, #1
	uxth	r3, r3
	add	ip, r0, r1, asl #1
	sub	ip, ip, #892
	strh	r1, [ip, #0]	@ movhi
.L844:
	cmp	r3, r4
	bcc	.L845
	ldmfd	sp!, {r4, pc}
.L860:
	.align	2
.L859:
	.word	.LANCHOR0+3074
	.word	.LANCHOR0+3076
	.word	.LANCHOR0
	.word	.LANCHOR0+3090
	.word	.LANCHOR0+3106
	.word	.LANCHOR2-892
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
	b	.L863
.L864:
	ldr	r4, [r1, r3]
	add	ip, ip, #1
	str	r4, [r0, r3]
	add	r3, r3, #4
.L863:
	cmp	ip, r2
	bne	.L864
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
	ldr	r0, .L869
	b	.L867
.L868:
	mov	ip, r0, asl #5
	ldrb	r4, [r3, r2]	@ zero_extendqisi2
	add	ip, ip, r0, lsr #2
	add	r2, r2, #1
	add	ip, ip, r4
	eor	r0, r0, ip
.L867:
	cmp	r2, r1
	bne	.L868
	ldmfd	sp!, {r4, pc}
.L870:
	.align	2
.L869:
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
	ldr	r3, .L872
	ldr	r0, [r3, #0]
	b	jiffies_to_msecs
.L873:
	.align	2
.L872:
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
	ldr	r4, .L877
	mov	ip, r0
	mov	r2, r3
	ldr	r4, [r4, #132]
	add	r4, r4, #4096
	add	r1, r4, r1
	movne	r0, r1
	movne	r1, ip
	ldmfd	sp!, {r4, lr}
	b	memcpy
.L878:
	.align	2
.L877:
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
	ldr	r3, .L883
	ldr	r0, [r3, #3956]
	bx	lr
.L884:
	.align	2
.L883:
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
	b	.L889
.L895:
	cmp	r5, #0
	bne	.L890
	ldr	r0, .L897
	mov	r1, r8
	mov	r2, r6
	mov	r3, r4
	bl	printk
.L890:
	cmp	r7, #4
	ldreq	r0, .L897+4
	ldreq	r1, [r6, r4, asl #2]
	beq	.L896
	cmp	r7, #2
	moveq	r3, r4, asl #1
	ldreq	r0, .L897+4
	ldreqsh	r1, [r6, r3]
	ldrne	r0, .L897+4
	ldrneb	r1, [r6, r4]	@ zero_extendqisi2
.L896:
	add	r5, r5, #1
	bl	printk
	cmp	r5, #15
	bls	.L894
	ldr	r0, .L897+8
	mov	r5, #0
	ldr	r1, .L897+12
	bl	printk
.L894:
	add	r4, r4, #1
.L889:
	cmp	r4, sl
	bne	.L895
	ldr	r0, .L897+8
	ldr	r1, .L897+12
	ldmfd	sp!, {r3, r4, r5, r6, r7, r8, sl, lr}
	b	printk
.L898:
	.align	2
.L897:
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
	ldr	r6, .L922
	add	r0, r6, r0, asl #3
	ldr	r3, [r6, #3788]
	ldr	r4, [r0, #864]
	cmp	r3, #3
	bls	.L916
	ldr	r3, [r4, #16]
	tst	r3, #4
	beq	.L916
	mov	r0, r4
	bl	wait_for_nandc_xfer_completed
	ldr	r5, [r4, #16]
	ldr	r3, [r4, #8]
	ands	r5, r5, #2
	str	r3, [sp, #0]
	movne	r5, #0
	bne	.L920
	b	.L921
.L906:
	ldr	r3, [r6, #3788]
	cmp	r3, #5
	bls	.L903
	ldr	r3, [r4, #0]
	str	r3, [sp, #4]
	ldr	r3, [sp, #4]
	tst	r3, #8192
	beq	.L903
	ldr	r3, [sp, #4]
	tst	r3, #131072
	bne	.L904
.L903:
	add	r5, r5, #1
	bic	r3, r5, #-16777216
	cmp	r3, #0
	bne	.L920
	ldr	r2, [r4, #28]
	mov	r1, r5
	ldr	r3, [sp, #0]
	ubfx	r2, r2, #16, #5
	ldr	r0, .L922+4
	ubfx	r3, r3, #22, #6
	bl	printk
	ldr	r0, .L922+8
	mov	r1, r4
	mov	r2, #4
	mov	r3, #512
	bl	rknand_print_hex
.L920:
	ldr	r2, [r4, #28]
	ldr	r3, [sp, #0]
	ubfx	r2, r2, #16, #5
	ubfx	r3, r3, #22, #6
	cmp	r2, r3
	blt	.L906
.L904:
	ldr	r4, .L922
	ldr	r3, [r4, #3820]
	cmp	r3, #0
	beq	.L907
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
	b	.L907
.L910:
	ldr	r3, [r4, #8]
	add	r5, r5, #1
	str	r3, [sp, #0]
	bic	r3, r5, #-16777216
	cmp	r3, #0
	bne	.L921
	ldr	r2, [sp, #0]
	mov	r1, r5
	ldr	r3, [r4, #28]
	ldr	r0, .L922+12
	ubfx	r3, r3, #16, #5
	bl	printk
	ldr	r0, .L922+8
	mov	r1, r4
	mov	r2, #4
	mov	r3, #512
	bl	rknand_print_hex
.L921:
	ldr	r3, [sp, #0]
	tst	r3, #1048576
	beq	.L910
	ldr	r6, .L922
	ldr	r3, [r6, #3828]
	cmp	r3, #0
	beq	.L911
	mov	r0, r4
	bl	NandcSendDumpDataStart
.L911:
	ldr	r3, [r6, #3820]
	ldr	r5, .L922
	cmp	r3, #0
	beq	.L912
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
.L912:
	ldr	r3, .L922
	ldr	r3, [r3, #3828]
	cmp	r3, #0
	beq	.L907
	mov	r0, r4
	bl	NandcSendDumpDataDone
.L907:
	ldr	r3, .L922
	mov	r2, #0
	str	r2, [r3, #3820]
	b	.L899
.L916:
	ldr	r3, [r4, #8]
	str	r3, [sp, #0]
	ldr	r3, [sp, #0]
	tst	r3, #1048576
	beq	.L916
.L899:
	ldmfd	sp!, {r2, r3, r4, r5, r6, pc}
.L923:
	.align	2
.L922:
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
	ldr	r3, .L962
	.pad #92
	sub	sp, sp, #92
	tst	r4, #63
	add	r3, r3, r0, asl #3
	mov	r7, r0
	mov	fp, r1
	mov	sl, r2
	ldr	r5, [sp, #128]
	ldr	r6, [r3, #864]
	bne	.L925
	cmp	r5, #0
	bne	.L926
	add	r0, sp, #24
	mov	r1, #255
	mov	r2, #64
	add	r5, sp, #24
	bl	ftl_memset
.L926:
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
	bne	.L927
	ldr	r1, .L962
	mov	r0, sl, lsr #1
	mov	r3, fp
	mov	r2, fp
	ldr	ip, [r1, #3832]
	mov	r4, r1
	cmp	ip, #24
	movhi	ip, #128
	movls	ip, #64
	b	.L929
.L930:
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
.L929:
	cmp	r2, r0
	add	r7, r3, ip
	ldr	r1, .L962
	bcc	.L930
	mov	r3, #0
	ldr	r0, [r1, #3832]
	mov	sl, sl, lsr #2
	ldr	r1, [r1, #3788]
	mov	r4, r3
	b	.L931
.L937:
	add	r2, r3, #8
	ldr	r2, [r6, r2, asl #2]
	str	r2, [sp, #20]
	ldr	r2, [sp, #20]
	tst	r2, #4
	bne	.L952
	ldr	r2, [sp, #20]
	ands	r2, r2, #32768
	bne	.L952
	cmp	r1, #5
	bls	.L933
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
	b	.L960
.L933:
	cmp	r1, #3
	bls	.L935
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
.L960:
	orr	r2, ip, r2, asl #5
.L935:
	cmp	r4, r2
	movcc	r4, r2
	b	.L932
.L952:
	mvn	r4, #0
.L932:
	add	r3, r3, #1
.L931:
	cmp	r3, sl
	bcs	.L927
	cmp	r0, #0
	bne	.L937
.L927:
	mov	r3, #0
	str	r3, [r6, #16]
	b	.L938
.L925:
	cmp	r1, #1
	mov	r8, #0
	bne	.L958
	b	.L939
.L942:
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
.L939:
	cmp	r8, sl
	bcc	.L942
	mov	r4, #0
	b	.L938
.L958:
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
	b	.L961
.L947:
	mov	r0, r7
	bl	NandcXferComp
	ldr	r3, [fp, #32]
	add	ip, r5, #2
	cmp	ip, sl
	str	r3, [sp, #20]
	bcs	.L944
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
.L944:
	ldr	r3, [sp, #20]
	tst	r3, #4
	mvnne	r4, #0
	bne	.L945
	ldr	r2, [sp, #20]
	ldr	r3, [sp, #20]
	ubfx	r2, r2, #3, #5
	ubfx	r3, r3, #27, #1
	orr	r3, r2, r3, asl #5
	cmp	r4, r3
	movcc	r4, r3
.L945:
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
.L961:
	cmp	ip, sl
	mov	r5, ip
	bcc	.L947
	mov	r6, fp
	ldr	fp, [sp, #12]
.L938:
	ldr	r3, .L962
	rsbs	fp, fp, #1
	movcc	fp, #0
	ldr	r3, [r3, #3788]
	cmp	r3, #5
	movls	fp, #0
	cmp	fp, #0
	beq	.L948
	ldr	r3, [r6, #0]
	and	r2, r3, #139264
	cmp	r2, #139264
	orreq	r3, r3, #131072
	streq	r3, [r6, #0]
	mvneq	r4, #0
.L948:
	mov	r0, r4
	add	sp, sp, #92
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, sl, fp, pc}
.L963:
	.align	2
.L962:
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
	ldr	r3, .L967
	subs	r4, r0, #0
	mov	r5, r1
	mov	r6, r2
	ldrb	r7, [r3, #2901]	@ zero_extendqisi2
	bne	.L965
	ldr	r3, .L967+4
	ldrb	r2, [r3, #853]	@ zero_extendqisi2
	ldr	r1, [r3, #856]
	mul	r2, r1, r2
	cmp	r5, r2
	bcs	.L965
	ldrb	r3, [r3, #852]	@ zero_extendqisi2
	cmp	r3, #0
	subeq	r7, r7, #2
	movne	r7, #4
.L965:
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
.L968:
	.align	2
.L967:
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
	ldr	r2, .L976
	mov	r7, r1
	mov	r8, r0
	ldrb	r1, [r2, #928]	@ zero_extendqisi2
	ldr	r3, [r2, #3624]
	cmp	r1, #0
	ldrb	r3, [r3, #19]	@ zero_extendqisi2
	beq	.L970
	ldr	r1, [r2, #3776]
	ldr	r2, .L976+4
	cmp	r1, r2
	ldmeqfd	sp!, {r3, r4, r5, r6, r7, r8, sl, pc}
.L970:
	sub	r2, r3, #5
	uxtb	r2, r2
	cmp	r3, #68
	cmpne	r2, #2
	bls	.L973
	cmp	r3, #35
	cmpne	r3, #19
	beq	.L973
	ldmfd	sp!, {r3, r4, r5, r6, r7, r8, sl, pc}
.L974:
	add	r3, r5, r4, asl #1
	sub	r3, r3, #892
	ldrh	r3, [r3, #0]
	cmp	r3, sl
	ldmnefd	sp!, {r3, r4, r5, r6, r7, r8, sl, pc}
	mov	r1, #255
	mov	r2, #32768
	ldr	r0, [r5, #136]
	bl	ftl_memset
	add	r1, r4, r7
	add	r4, r4, #1
	mov	r0, r8
	ldr	r2, [r5, #136]
	mov	r3, #0
	bl	FlashProgPage
	uxth	r4, r4
	b	.L975
.L973:
	ldr	r6, .L976
	movw	sl, #65535
	ldr	r5, .L976+8
.L975:
	ldr	r3, [r6, #3624]
	ldrh	r3, [r3, #10]
	cmp	r3, r4
	bhi	.L974
	ldmfd	sp!, {r3, r4, r5, r6, r7, r8, sl, pc}
.L977:
	.align	2
.L976:
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
	ldr	r3, .L981
	subs	r4, r0, #0
	mov	r6, r1
	mov	r5, r2
	ldrb	r7, [r3, #2901]	@ zero_extendqisi2
	bne	.L979
	ldr	r3, .L981+4
	ldrb	r2, [r3, #853]	@ zero_extendqisi2
	ldr	r3, [r3, #856]
	mul	r3, r3, r2
	cmp	r1, r3
	movcc	r7, #4
.L979:
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
.L982:
	.align	2
.L981:
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
	ldr	r3, .L997
	mov	sl, r2
	ldr	fp, .L997
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
	b	.L985
.L990:
	add	r4, r4, #1
	ldrb	r1, [fp, #1]	@ zero_extendqisi2
	mov	r0, r6
	ldr	r2, .L997+4
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
	beq	.L987
	ldrb	r3, [ip, #3836]	@ zero_extendqisi2
	cmn	r5, #1
	moveq	r5, r0
	add	r3, r3, r3, asl #1
	cmp	r0, r3, lsr #2
	bcc	.L996
	mov	r7, #0
	mov	sl, r7
.L987:
	add	r8, r8, #1
.L985:
	cmp	r8, r9
	bcc	.L990
	b	.L989
.L996:
	mov	r5, r0
.L989:
	ldr	r3, .L997
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
.L998:
	.align	2
.L997:
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
	ldr	r4, .L1012
	mov	sl, r3
	.pad #28
	sub	sp, sp, #28
	ldrb	r0, [r4, #3836]	@ zero_extendqisi2
	ldrb	r3, [r4, #928]	@ zero_extendqisi2
	str	r1, [sp, #16]
	cmp	r3, #0
	str	r2, [sp, #20]
	addeq	r0, r0, r0, asl #1
	ubfxeq	r0, r0, #2, #8
	beq	.L1011
	mov	r1, #3
	bl	__aeabi_idiv
	uxtb	r0, r0
.L1011:
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
	b	.L1002
.L1006:
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
	beq	.L1003
	cmn	r4, #1
	ldr	r2, [sp, #12]
	moveq	r4, r0
	cmp	r0, r2
	bcc	.L1010
	mov	sl, #0
	str	sl, [sp, #20]
.L1003:
	mov	r8, ip
.L1002:
	ldr	r2, .L1012+4
	ldrb	r3, [r2, #140]	@ zero_extendqisi2
	cmp	r8, r3
	bcc	.L1006
	b	.L1005
.L1010:
	mov	r4, r0
.L1005:
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
	bne	.L1008
	ldr	r0, .L1012+8
	mov	r1, r8
	ldr	r2, [sp, #16]
	mov	r3, r8
	str	r4, [sp, #0]
	bl	printk
.L1008:
	mov	r0, r4
	add	sp, sp, #28
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, sl, fp, pc}
.L1013:
	.align	2
.L1012:
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
	ldr	ip, .L1023
	mov	r5, #1
	mvn	r4, #0
	ldr	fp, .L1023+4
	add	r3, ip, r8, asl #3
	ldrb	sl, [r3, #868]	@ zero_extendqisi2
	ldr	r2, [r3, #864]
	add	sl, sl, #8
	add	sl, r2, sl, asl #8
	b	.L1015
.L1019:
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
	beq	.L1016
	ldrb	r3, [ip, #3836]	@ zero_extendqisi2
	cmn	r4, #1
	moveq	r4, r0
	add	r3, r3, r3, asl #1
	cmp	r0, r3, lsr #2
	bcc	.L1022
	mov	r6, #0
	mov	r7, r6
.L1016:
	add	r5, r5, #1
.L1015:
	ldrb	r3, [fp, #140]	@ zero_extendqisi2
	add	r3, r3, #1
	cmp	r5, r3
	bcc	.L1019
	b	.L1018
.L1022:
	mov	r4, r0
.L1018:
	mov	r0, sl
	mov	r1, #0
	bl	SamsungSetRRPara
	ldr	r3, .L1023
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
.L1024:
	.align	2
.L1023:
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
	ldr	r3, .L1046
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
	bls	.L1026
	ldrb	sl, [r3, #3780]	@ zero_extendqisi2
	cmp	sl, #0
	beq	.L1027
	mov	r0, #0
	mov	sl, #1
	bl	NandcSetDdrMode
.L1027:
	mov	r3, #92
	str	r3, [r4, #8]
	mov	r3, #197
	str	r3, [r4, #8]
.L1026:
	mov	r6, #1
	mvn	r5, #0
	ldr	fp, .L1046
	b	.L1028
.L1037:
	ldrb	r3, [fp, #3761]	@ zero_extendqisi2
	mov	r0, r4
	uxtb	r1, r6
	sub	r3, r3, #67
	uxtb	r3, r3
	cmp	r3, #1
	bhi	.L1029
	bl	SandiskSetRRPara
	b	.L1030
.L1029:
	bl	ToshibaSetRRPara
.L1030:
	ldr	r2, .L1046
	ldrb	r3, [r2, #3761]	@ zero_extendqisi2
	cmp	r3, #34
	bne	.L1031
	ldr	r2, .L1046+4
	ldrb	r3, [r2, #140]	@ zero_extendqisi2
	sub	r3, r3, #3
	cmp	r6, r3
	moveq	r3, #179
	streq	r3, [r4, #8]
.L1031:
	cmp	sl, #0
	mov	r3, #38
	str	r3, [r4, #8]
	mov	r3, #93
	str	r3, [r4, #8]
	beq	.L1032
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
	b	.L1033
.L1032:
	mov	r3, r7
	mov	r0, r8
	ldr	r1, [sp, #4]
	mov	r2, r9
	bl	FlashReadRawPage
	mov	r3, r0
.L1033:
	cmn	r3, #1
	beq	.L1034
	ldrb	r2, [fp, #3836]	@ zero_extendqisi2
	cmn	r5, #1
	moveq	r5, r3
	add	r2, r2, r2, asl #1
	cmp	r3, r2, lsr #2
	bcc	.L1045
	mov	r7, #0
	mov	r9, r7
.L1034:
	add	r6, r6, #1
.L1028:
	ldr	r2, .L1046+4
	ldrb	r3, [r2, #140]	@ zero_extendqisi2
	add	r3, r3, #1
	cmp	r6, r3
	bcc	.L1037
	b	.L1036
.L1045:
	mov	r5, r3
.L1036:
	ldr	r6, .L1046
	mov	r0, r4
	mov	r1, #0
	ldrb	r3, [r6, #3761]	@ zero_extendqisi2
	sub	r3, r3, #67
	uxtb	r3, r3
	cmp	r3, #1
	bhi	.L1038
	bl	SandiskSetRRPara
	b	.L1039
.L1038:
	bl	ToshibaSetRRPara
.L1039:
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
	beq	.L1041
	mov	r0, #4
	bl	NandcSetDdrMode
.L1041:
	mov	r0, r5
	ldmfd	sp!, {r1, r2, r3, r4, r5, r6, r7, r8, r9, sl, fp, pc}
.L1047:
	.align	2
.L1046:
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
	ldr	r5, .L1055
	ldr	r7, .L1055+4
	ldr	r8, .L1055+8
	ldr	r3, [r5, #148]
	ldrb	r0, [r5, #152]	@ zero_extendqisi2
	str	r3, [r5, #144]
	bl	FlashBchSel
	mov	r1, #0
	mov	r2, #2048
	ldr	r0, [r5, #148]
	bl	ftl_memset
	ldr	r3, [r5, #144]
	ldr	r1, .L1055+12
	mov	r2, #32
	str	r8, [r3, #0]
	ldr	r4, [r5, #144]
	ldrb	r3, [r7, #3762]	@ zero_extendqisi2
	add	r0, r4, #16
	strh	r3, [r4, #12]	@ movhi
	ldrb	r3, [r7, #853]	@ zero_extendqisi2
	strh	r3, [r4, #14]	@ movhi
	ldrb	r3, [r7, #3780]	@ zero_extendqisi2
	str	r3, [r4, #1076]
	bl	memcpy
	add	r0, r4, #80
	ldr	r1, .L1055+16
	mov	r2, #8
	bl	memcpy
	add	r0, r4, #96
	ldr	r1, .L1055+20
	mov	r2, #32
	bl	memcpy
	ldr	r0, [r5, #144]
	ldr	r1, .L1055+24
	mov	r2, #32
	add	r0, r0, #160
	bl	memcpy
	ldr	r4, [r5, #144]
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
	ldr	r3, [r5, #136]
	str	r3, [r5, #144]
	str	r0, [r4, #8]
	mov	r0, #0
	bl	flash_enter_slc_mode
	mov	r4, r6
.L1051:
	ldr	r1, [r7, #856]
	mov	r0, #0
	mov	r2, r0
	mul	r1, r1, r4
	bl	FlashEraseBlock
	ldr	r1, [r7, #856]
	mov	r0, #0
	ldr	r2, [r5, #148]
	mov	r3, r0
	mul	r1, r1, r4
	bl	FlashProgPage
	ldr	r1, [r7, #856]
	mov	r0, #0
	ldr	r2, [r5, #148]
	mov	r3, r0
	mul	r1, r1, r4
	add	r1, r1, #1
	bl	FlashProgPage
	ldr	r1, [r7, #856]
	mov	r0, #0
	ldr	r2, [r5, #136]
	mov	r3, r0
	mul	r1, r1, r4
	bl	FlashReadRawPage
	cmn	r0, #1
	beq	.L1049
	ldr	sl, [r5, #144]
	ldr	r3, [sl, #0]
	cmp	r3, r8
	bne	.L1049
	add	r0, sl, #12
	movw	r1, #2036
	bl	JSHash
	ldr	r3, [sl, #8]
	cmp	r3, r0
	bne	.L1049
	add	r3, r4, #1
	str	r3, [r5, #156]
	ldr	r3, [r7, #856]
	add	r6, r6, #1
	cmp	r6, #1
	mul	r3, r3, r4
	str	r3, [r5, #160]
	bhi	.L1050
.L1049:
	add	r4, r4, #1
	cmp	r4, #4
	bne	.L1051
.L1050:
	mov	r0, #0
	bl	flash_exit_slc_mode
	cmp	r6, #0
	mvneq	r0, #0
	movne	r0, #0
	ldmfd	sp!, {r3, r4, r5, r6, r7, r8, sl, pc}
.L1056:
	.align	2
.L1055:
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
	ldr	r1, .L1068
	add	r0, sp, #12
	bl	memcpy
	ldr	r3, .L1068+4
	ldrb	r2, [r3, #3836]	@ zero_extendqisi2
	str	r2, [sp, #4]
	ldr	r2, [r3, #3776]
	ldr	r3, .L1068+8
	cmp	r2, r3
	bne	.L1058
	mov	r0, #0
	bl	flash_enter_slc_mode
.L1058:
	ldr	r7, .L1068+4
	mov	r0, sl
	mov	r1, #0
	mov	r2, #2048
	bl	ftl_memset
	mvn	r8, #0
	mov	r4, #2
	ldr	r9, .L1068+12
	mov	fp, r7
	b	.L1059
.L1066:
	mov	r6, #0
.L1061:
	add	r3, sp, #12
	ldr	r5, .L1068+12
	ldrb	r0, [r3, r6]	@ zero_extendqisi2
	bl	FlashBchSel
	ldr	r1, [r7, #856]
	mov	r0, #0
	ldr	r2, [r9, #148]
	mov	r3, r0
	mul	r1, r1, r4
	bl	FlashReadRawPage
	cmn	r0, #1
	bne	.L1060
	add	r6, r6, #1
	cmp	r6, #4
	bne	.L1061
	b	.L1062
.L1060:
	ldr	r3, [r5, #148]
	ldr	r2, [r3, #0]
	ldr	r3, .L1068+16
	cmp	r2, r3
	bne	.L1062
	add	r3, sp, #16
	ldr	r0, .L1068+20
	add	r6, r3, r6
	ldrb	r1, [r6, #-4]	@ zero_extendqisi2
	bl	printk
	mov	r0, sl
	ldr	r1, [r5, #148]
	mov	r2, #2048
	bl	memcpy
	ldr	r3, [r5, #148]
	ldr	r3, [r3, #512]
	strb	r3, [fp, #853]
	ldr	r3, [r5, #156]
	cmp	r3, r4
	bls	.L1065
	str	r4, [r5, #156]
	bl	FlashSavePhyInfo
	mov	r8, #0
.L1062:
	add	r4, r4, #1
.L1059:
	ldrb	r3, [r7, #853]	@ zero_extendqisi2
	cmp	r4, r3
	bcc	.L1066
	b	.L1063
.L1065:
	mov	r8, #0
.L1063:
	ldr	r0, [sp, #4]
	bl	FlashBchSel
	ldr	r3, .L1068+4
	ldr	r2, [r3, #3776]
	ldr	r3, .L1068+8
	cmp	r2, r3
	bne	.L1064
	mov	r0, #0
	bl	flash_exit_slc_mode
.L1064:
	mov	r0, r8
	add	sp, sp, #20
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, sl, fp, pc}
.L1069:
	.align	2
.L1068:
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
	ldr	r1, .L1081
	add	r0, sp, #4
	bl	memcpy
	ldr	r5, .L1081+4
	ldr	r2, .L1081+8
	movw	r3, #2902
	mov	r4, #0
	mov	r8, #4
	mov	r0, r4
	str	r4, [r5, #160]
	ldrh	r9, [r2, r3]
	mvn	r6, #0
	ldr	r3, [r5, #148]
	ldr	fp, .L1081+12
	str	r3, [r5, #144]
	bl	flash_enter_slc_mode
	b	.L1071
.L1073:
	add	r3, sp, #4
	ldrb	r0, [r3, r7]	@ zero_extendqisi2
	bl	FlashBchSel
	mov	r0, #0
	mov	r1, r4
	ldr	r2, [r5, #148]
	mov	r3, r0
	bl	FlashReadRawPage
	cmn	r0, #1
	bne	.L1072
	mov	r0, #0
	mov	r1, sl
	ldr	r2, [r5, #148]
	mov	r3, r0
	bl	FlashReadRawPage
	cmn	r0, #1
	bne	.L1072
	add	r7, r7, #1
	cmp	r7, #4
	beq	.L1074
	b	.L1073
.L1072:
	ldr	r7, [r5, #144]
	ldr	r3, [r7, #0]
	cmp	r3, fp
	bne	.L1074
	cmp	r6, #0
	bne	.L1075
	ldr	r2, .L1081+8
	movw	r3, #2902
	mov	r0, r4
	ldrh	r1, [r2, r3]
	bl	__aeabi_uidiv
	ldr	r3, .L1081+4
	add	r0, r0, #1
	str	r0, [r3, #156]
	mov	r0, r6
	b	.L1080
.L1075:
	add	r0, r7, #12
	movw	r1, #2036
	bl	JSHash
	ldr	r3, [r7, #8]
	cmp	r3, r0
	bne	.L1074
	ldr	r6, .L1081+16
	add	r1, r7, #160
	mov	r2, #32
	ldr	r0, .L1081+20
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
	ldr	r2, .L1081+8
	mov	r0, r4
	str	r4, [r5, #160]
	strb	r3, [r6, #3780]
	movw	r3, #2902
	ldrh	r1, [r2, r3]
	mov	r6, #0
	bl	__aeabi_uidiv
	add	r0, r0, #1
	str	r0, [r5, #156]
	cmp	r0, #1
	moveq	r3, #2
	streq	r3, [r5, #156]
	ldrh	r3, [r7, #14]
	strb	r3, [r5, #164]
.L1074:
	subs	r8, r8, #1
	add	r4, r4, r9
	beq	.L1078
.L1071:
	add	sl, r4, #1
	mov	r7, #0
	b	.L1073
.L1078:
	mov	r0, r8
.L1080:
	bl	flash_exit_slc_mode
	mov	r0, r6
	ldmfd	sp!, {r1, r2, r3, r4, r5, r6, r7, r8, r9, sl, fp, pc}
.L1082:
	.align	2
.L1081:
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
	ldr	r4, .L1104
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
	beq	.L1085
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
	beq	.L1086
	ldr	r0, .L1104+4
	mov	r2, r8
	ldr	r1, [sp, #0]
	bl	printk
	cmp	r8, #9
	bhi	.L1087
	add	r4, r4, fp, asl #3
	ldr	r3, [r4, #864]
	ldr	r2, [r3, #3840]
	ldr	r2, [r3, #0]
	orr	r2, r2, #131072
	str	r2, [r3, #0]
.L1087:
	ldr	r3, .L1104+8
	ldr	r2, [r3, #168]
	add	r2, r2, #1
	str	r2, [r3, #168]
	cmp	r2, #2048
	movcs	sl, #0
	strcs	sl, [r3, #168]
	movcs	r9, sl
	bcc	.L1086
.L1085:
	mov	r4, #0
	str	fp, [sp, #4]
	mov	r5, r4
	mvn	r6, #0
	mov	fp, r4
	str	r4, [sp, #8]
.L1092:
	uxtb	r0, r7
	bl	NandcSetDdrPara
	mov	r3, sl
	ldr	r0, [sp, #4]
	mov	r2, r9
	ldr	r1, [sp, #0]
	bl	FlashReadRawPage
	add	r3, r8, #1
	cmp	r0, r3
	bhi	.L1088
	cmp	r0, #2
	bhi	.L1098
	add	r5, r5, #1
	cmp	r5, #9
	bls	.L1098
	mov	ip, fp
	rsb	r4, r5, r7
	ldr	fp, [sp, #4]
	mov	r8, r0
	mov	r6, #0
	b	.L1090
.L1088:
	cmp	fp, r5
	bcs	.L1099
	cmp	r5, #7
	rsb	r3, r5, r4
	str	r3, [sp, #8]
	bhi	.L1103
	mov	fp, r5
	b	.L1099
.L1098:
	mov	r6, #0
	mov	r4, r7
	mov	r8, r0
	mov	sl, r6
	mov	r9, r6
	b	.L1089
.L1099:
	mov	r5, #0
.L1089:
	add	r7, r7, #2
	cmp	r7, #69
	bls	.L1092
	mov	ip, fp
	ldr	fp, [sp, #4]
.L1090:
	cmp	ip, r5
	bcc	.L1093
	b	.L1091
.L1103:
	ldr	fp, [sp, #4]
.L1091:
	ldr	r4, [sp, #8]
.L1093:
	cmp	r4, #0
	beq	.L1094
	ldr	r0, .L1104+12
	mov	r1, r4
	bl	printk
	uxtb	r0, r4
	bl	NandcSetDdrPara
.L1094:
	cmn	r6, #1
	bne	.L1086
	ldr	r0, .L1104+16
	mov	r1, fp
	ldr	r2, [sp, #0]
	bl	printk
	ldr	r3, [sp, #56]
	cmp	r3, #0
	moveq	r8, r6
	beq	.L1086
	ldr	r3, [sp, #12]
	ubfx	r0, r3, #8, #8
	bl	NandcSetDdrPara
.L1086:
	mov	r0, r8
	add	sp, sp, #20
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, sl, fp, pc}
.L1105:
	.align	2
.L1104:
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
	ldr	r5, .L1110
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
	beq	.L1107
	cmn	r8, #1
	bne	.L1108
.L1107:
	ldrb	r3, [r5, #3772]	@ zero_extendqisi2
	tst	r3, #1
	beq	.L1108
	mov	r0, #1
	bl	FlashSetInterfaceMode
	mov	r0, #1
	bl	NandcSetMode
	ldr	r3, .L1110
	mov	r2, #0
	strb	r2, [r3, #3780]
	b	.L1109
.L1108:
	mov	r3, #1
	strb	r3, [r5, #3780]
.L1109:
	mov	r0, #0
	ldmfd	sp!, {r2, r3, r4, r5, r6, r7, r8, pc}
.L1111:
	.align	2
.L1110:
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
	bne	.L1113
	ldr	r7, .L1118
	ldrb	r9, [r7, #860]	@ zero_extendqisi2
	cmp	r9, #0
	beq	.L1114
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
	bne	.L1113
.L1114:
	ldr	r7, .L1118
	ldrb	r3, [r7, #3780]	@ zero_extendqisi2
	cmp	r3, #0
	beq	.L1113
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
	beq	.L1115
	ldrb	r3, [r7, #3836]	@ zero_extendqisi2
	cmp	r0, r3, lsr #1
	bls	.L1113
.L1115:
	ubfx	r0, r9, #8, #8
	bl	NandcSetDdrPara
.L1113:
	ldr	r7, .L1118+4
	ldr	ip, [r7, #172]
	adds	r3, ip, #0
	movne	r3, #1
	cmn	r4, #1
	movne	r3, #0
	cmp	r3, #0
	beq	.L1116
	mov	r1, r6
	mov	r2, r8
	mov	r3, sl
	mov	r0, r5
	blx	ip
	mov	r2, r5
	mov	r3, r6
	mov	r4, r0
	ldr	r0, .L1118+8
	mov	r1, r4
	bl	printk
	cmn	r4, #1
	bne	.L1116
	ldr	r3, .L1118
	ldrb	r3, [r3, #928]	@ zero_extendqisi2
	cmp	r3, #0
	beq	.L1116
	mov	r0, r5
	bl	flash_enter_slc_mode
	ldr	ip, [r7, #172]
	mov	r0, r5
	mov	r1, r6
	mov	r2, r8
	mov	r3, sl
	blx	ip
	mov	r4, r0
	mov	r0, r5
	bl	flash_exit_slc_mode
.L1116:
	mov	r0, r4
	ldmfd	sp!, {r2, r3, r4, r5, r6, r7, r8, r9, sl, pc}
.L1119:
	.align	2
.L1118:
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
	ldr	r3, .L1133
	stmfd	sp!, {r4, r5, r6, r7, r8, r9, sl, fp, lr}
	.save {r4, r5, r6, r7, r8, r9, sl, fp, lr}
	mov	r8, r1
	.pad #28
	sub	sp, sp, #28
	mov	fp, r2
	ldrb	sl, [r3, #2901]	@ zero_extendqisi2
	mov	r4, r0
	mov	r7, #0
	ldr	r6, .L1133+4
	b	.L1121
.L1130:
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
	bcs	.L1123
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
	beq	.L1128
	ldr	r2, [r3, #8]
	cmn	r2, #1
	bne	.L1128
	ldr	r3, [r3, #0]
	cmn	r3, #1
	strne	r2, [r4, #0]
.L1128:
	ldr	r3, [r4, #0]
	cmn	r3, #1
	bne	.L1123
	ldr	r1, [r4, #4]
	ldr	r0, .L1133+8
	ldrb	r2, [r6, #3836]	@ zero_extendqisi2
	bl	printk
	ldr	r1, [r4, #8]
	cmp	r1, #0
	beq	.L1129
	ldr	r0, .L1133+12
	mov	r2, #4
	mov	r3, #8
	bl	rknand_print_hex
.L1129:
	ldr	r1, [r4, #12]
	cmp	r1, #0
	beq	.L1123
	mov	r2, #4
	ldr	r0, .L1133+16
	mov	r3, r2
	bl	rknand_print_hex
.L1123:
	add	r7, r7, #1
	add	r4, r4, #36
.L1121:
	cmp	r7, r8
	bne	.L1130
	mov	r0, #0
	add	sp, sp, #28
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, sl, fp, pc}
.L1134:
	.align	2
.L1133:
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
	ldr	r3, .L1177
	stmfd	sp!, {r4, r5, r6, r7, r8, r9, sl, fp, lr}
	.save {r4, r5, r6, r7, r8, r9, sl, fp, lr}
	.pad #52
	sub	sp, sp, #52
	ldrb	r3, [r3, #2901]	@ zero_extendqisi2
	mov	r9, r0
	str	r1, [sp, #28]
	str	r2, [sp, #32]
	str	r3, [sp, #24]
	ldr	r3, .L1177+4
	ldrb	r8, [r3, #852]	@ zero_extendqisi2
	ldrb	ip, [r3, #860]	@ zero_extendqisi2
	cmp	r8, #0
	str	ip, [sp, #36]
	moveq	sl, r8
	beq	.L1136
	bl	FlashReadSlc2KPages
	b	.L1137
.L1164:
	mov	r3, #36
	ldr	ip, [sp, #28]
	mul	r3, r3, sl
	add	r2, sp, #44
	ldr	r1, [sp, #32]
	ldr	fp, .L1177+4
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
	bcs	.L1139
	add	r3, fp, r3
	ldrb	r4, [r3, #3764]	@ zero_extendqisi2
	ldr	r3, .L1177+8
	mov	r0, r4
	ldrb	r3, [r3, #176]	@ zero_extendqisi2
	cmp	r3, #0
	moveq	r7, #0
	bl	NandcWaitFlashReady
	ldr	r3, [fp, #3624]
	ldrb	r2, [r3, #19]	@ zero_extendqisi2
	sub	r3, r2, #1
	uxtb	r3, r3
	cmp	r3, #6
	bhi	.L1141
	add	r1, fp, r4
	cmp	r2, #7
	add	fp, fp, r4
	ldr	r2, .L1177+4
	ldrb	r3, [r1, #12]	@ zero_extendqisi2
	ldreqb	r3, [r1, #20]	@ zero_extendqisi2
	ldrb	r1, [fp, #3756]	@ zero_extendqisi2
	cmp	r1, r3
	beq	.L1141
	ldrb	r1, [r2, #1]	@ zero_extendqisi2
	mov	r0, r4
	add	r2, r2, #4
	bl	HynixSetRRPara
.L1141:
	mov	r0, r4
	mov	r5, r5, lsr #31
	bl	NandcFlashCs
	ldr	ip, [sp, #32]
	mov	r0, r4
	cmp	ip, #1
	orreq	r5, r5, #1
	str	r5, [sp, #20]
	cmp	r5, #0
	beq	.L1143
	ldr	r3, .L1177+4
	ldrb	r3, [r3, #928]	@ zero_extendqisi2
	cmp	r3, #0
	beq	.L1143
	bl	flash_enter_slc_mode
	b	.L1144
.L1143:
	bl	flash_exit_slc_mode
.L1144:
	ldr	fp, .L1177+4
.L1176:
	ldr	r1, [sp, #44]
	cmn	r1, #1
	cmpeq	r4, #255
	moveq	r3, #0
	movne	r3, #1
	moveq	r7, r3
	beq	.L1146
	cmp	r7, #0
	beq	.L1147
	ldr	r2, [fp, #856]
	mov	r0, r4
	add	r2, r1, r2
	bl	FlashReadDpCmd
	b	.L1148
.L1147:
	mov	r0, r4
	bl	FlashReadCmd
.L1148:
	mov	r0, r4
	bl	NandcWaitFlashReady
	cmp	r7, #0
	beq	.L1146
	mov	r0, r4
	ldr	r1, [sp, #44]
	bl	FlashReadDpDataOutCmd
.L1146:
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
	bne	.L1176
.L1149:
	cmp	r7, #0
	mov	r5, r0
	beq	.L1150
	ldr	r3, .L1177+4
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
.L1150:
	mov	r0, r4
	ldr	fp, .L1177+4
	bl	NandcFlashDeCs
	ldr	ip, [sp, #36]
	cmn	r5, #1
	strb	ip, [fp, #860]
	bne	.L1154
	ldrb	r3, [fp, #3780]	@ zero_extendqisi2
	cmp	r3, #0
	beq	.L1152
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
	beq	.L1153
	ldrb	r3, [fp, #3836]	@ zero_extendqisi2
	cmp	r0, r3, lsr #1
	bls	.L1170
.L1153:
	ubfx	r0, r7, #8, #8
	bl	NandcSetDdrPara
	cmn	r5, #1
	bne	.L1170
.L1152:
	ldr	r3, .L1177+8
	mov	r0, r4
	ldr	r1, [sp, #44]
	ldr	r2, [r6, #8]
	ldr	r7, [r3, #172]
	ldr	r3, [r6, #12]
	cmp	r7, #0
	beq	.L1155
	blx	r7
	cmn	r0, #1
	mov	r5, r0
	bne	.L1172
	ldr	r2, .L1177+4
	ldr	r3, [r2, #3624]
	ldrb	r3, [r3, #19]	@ zero_extendqisi2
	sub	r3, r3, #1
	uxtb	r3, r3
	cmp	r3, #6
	bhi	.L1157
	ldrb	r1, [r2, #1]	@ zero_extendqisi2
	mov	r0, r4
	add	r2, r2, #4
	mov	r3, #0
	bl	HynixSetRRPara
.L1157:
	ldr	r1, [sp, #44]
	mov	r0, r4
	ldr	r2, [r6, #8]
	ldr	r3, [r6, #12]
	bl	FlashReadRawPage
	ldr	r7, .L1177+4
	ldr	r1, [r6, #4]
	ldrb	r2, [r7, #3836]	@ zero_extendqisi2
	mov	r5, r0
	ldr	r0, .L1177+12
	mov	r3, r5
	bl	printk
	cmn	r5, #1
	bne	.L1172
	ldrb	r7, [r7, #928]	@ zero_extendqisi2
	cmp	r7, #0
	beq	.L1156
	ldr	ip, [sp, #20]
	mov	r0, r4
	cmp	ip, #0
	beq	.L1158
	bl	flash_enter_slc_mode
	b	.L1159
.L1158:
	bl	flash_exit_slc_mode
.L1159:
	ldr	r3, .L1177+8
	mov	r0, r4
	ldr	r1, [sp, #44]
	ldr	r2, [r6, #8]
	ldr	ip, [r3, #172]
	ldr	r3, [r6, #12]
	blx	ip
	mov	r5, r0
	b	.L1172
.L1155:
	bl	FlashReadRawPage
	mov	r5, r0
	b	.L1156
.L1170:
	mov	r7, #0
.L1154:
	ldr	r3, .L1177+4
	ldrb	r3, [r3, #3836]	@ zero_extendqisi2
	add	r3, r3, r3, asl #1
	cmp	r5, r3, lsr #2
	bls	.L1156
	ldr	r3, .L1177+8
	ldr	r3, [r3, #172]
	cmp	r3, #0
	moveq	r5, #256
	b	.L1156
.L1172:
	mov	r7, #0
.L1156:
	ldr	ip, [sp, #16]
	cmp	r5, #256
	cmnne	r5, #1
	movne	r2, #0
	str	r5, [r9, ip]
	strne	r2, [r9, ip]
	ldr	ip, [sp, #16]
	ldr	r3, [r9, ip]
	cmn	r3, #1
	bne	.L1161
	ldr	r2, .L1177+4
	ldr	r1, [r6, #4]
	ldr	r0, .L1177+16
	ldrb	r2, [r2, #3836]	@ zero_extendqisi2
	bl	printk
	ldr	r1, [r6, #12]
	cmp	r1, #0
	beq	.L1161
	mov	r2, #4
	ldr	r0, .L1177+20
	mov	r3, r2
	bl	rknand_print_hex
.L1161:
	cmp	r7, #0
	beq	.L1162
	ldr	r3, .L1177+4
	ldrb	r3, [r3, #3836]	@ zero_extendqisi2
	add	r3, r3, r3, asl #1
	cmp	r8, r3, lsr #2
	bls	.L1163
	ldr	r3, .L1177+8
	ldr	r3, [r3, #172]
	cmp	r3, #0
	moveq	r8, #256
.L1163:
	add	r3, sl, #1
	mov	r2, #36
	cmp	r8, #256
	cmnne	r8, #1
	mul	r3, r2, r3
	movne	r2, #0
	str	r8, [r9, r3]
	strne	r2, [r9, r3]
.L1162:
	ldr	ip, [sp, #20]
	add	sl, sl, r7
	cmp	ip, #0
	beq	.L1139
	ldr	r3, .L1177+4
	ldrb	r3, [r3, #928]	@ zero_extendqisi2
	cmp	r3, #0
	beq	.L1139
	mov	r0, r4
	bl	flash_exit_slc_mode
.L1139:
	add	sl, sl, #1
.L1136:
	ldr	ip, [sp, #28]
	cmp	sl, ip
	bcc	.L1164
	mov	r0, #0
.L1137:
	add	sp, sp, #52
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, sl, fp, pc}
.L1178:
	.align	2
.L1177:
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
	ldr	r2, .L1204
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
	beq	.L1200
	cmp	r4, #0
	bne	.L1180
	b	.L1181
.L1200:
	mov	r4, #0
.L1180:
	ldr	r2, .L1204+4
	movw	r3, #3916
	ldr	fp, [sp, #20]
	ldrh	r3, [r2, r3]
	cmp	fp, r3
	bne	.L1182
.L1181:
	bl	FtlGcPageVarInit
.L1182:
	mov	r7, #0
	movw	r8, #65535
	mov	sl, r7
	mvn	r3, #0
	str	r3, [sp, #16]
.L1195:
	ldrh	r3, [r5, #0]
	strb	sl, [r5, #8]
	cmp	r3, r8
	beq	.L1201
.L1184:
.L1202:
	ldr	r3, .L1204+4
	movw	r2, #3848
	ldrh	r9, [r3, r2]
	ldr	r2, .L1204+8
	ldr	lr, [r2, #-972]
	ldr	ip, [r2, #-1168]
	movw	r2, #3928
	ldrh	r2, [r3, r2]
	mov	r3, #0
	mov	r6, r3
	str	r2, [sp, #12]
	mov	r2, r5
	b	.L1185
.L1187:
	ldrh	r0, [r2, #16]
	cmp	r0, r8
	beq	.L1186
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
.L1186:
	add	r3, r3, #1
	add	r2, r2, #2
	uxth	r3, r3
.L1185:
	cmp	r3, r9
	bne	.L1187
	ldr	r9, .L1204+8
	mov	r1, r6
	mov	r2, #0
	ldr	r0, [r9, #-972]
	bl	FlashReadPages
	mov	r3, #36
	mul	r3, r3, r6
	mov	r6, #0
	str	r3, [sp, #12]
	b	.L1188
.L1196:
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
	bne	.L1189
	ldr	r3, [r3, #12]
	add	r6, r6, #36
	ldrh	r1, [r3, #0]
	cmp	r1, r8
	ldreq	r3, .L1204+8
	moveq	r1, #1
	streq	r1, [r3, #-1012]
	beq	.L1183
.L1190:
	ldr	r0, [r3, #12]
	mov	r1, fp
	ldr	r2, [r3, #8]
	bl	FtlGcUpdatePage
	b	.L1188
.L1189:
	mov	r2, fp
	ldr	r0, .L1204+12
	ldrh	r1, [r5, #0]
	bl	printk
	ldr	r3, .L1204+4
	ldr	r2, [r3, #3840]
	cmp	r2, #0
	bne	.L1191
	ldrb	r3, [r3, #928]	@ zero_extendqisi2
	cmp	r3, #0
	beq	.L1192
.L1191:
	ldr	r2, .L1204+8
	ldrh	r3, [r5, #0]
	ldr	r2, [r2, #-2080]
	mov	r3, r3, asl #1
	ldrh	r3, [r2, r3]
	cmp	r3, #119
	bls	.L1193
.L1192:
	ldr	r3, .L1204+8
	ldr	r3, [r3, #-972]
	ldr	r3, [r3, r6]
	cmn	r3, #1
	bne	.L1194
.L1193:
	ldr	r3, .L1204+8
	ldr	r3, [r3, #-972]
	add	r6, r3, r6
	ldr	r6, [r6, #4]
	str	r6, [sp, #16]
.L1194:
	ldr	r2, .L1204+8
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
	b	.L1195
.L1188:
	ldr	r3, [sp, #12]
	cmp	r6, r3
	bne	.L1196
	ldr	fp, [sp, #20]
	add	r7, r7, #1
	add	r4, r4, #1
	cmp	r7, fp
	uxth	r4, r4
	bcc	.L1197
	ldr	r2, .L1204
	movw	r3, #3000
	ldrh	r1, [r2, r3]
	cmp	r1, r8
	beq	.L1197
	add	r1, r1, r7
	strh	r1, [r2, r3]	@ movhi
	ldr	r2, .L1204+4
	add	r3, r3, #916
	ldrh	r3, [r2, r3]
	cmp	r3, r4
	bhi	.L1198
.L1197:
	ldr	r2, .L1204+4
	movw	r3, #3916
	ldrh	r3, [r2, r3]
	cmp	r3, r4
	bhi	.L1202
.L1201:
	mov	r2, #0
.L1183:
	ldr	r1, .L1204
	movw	r3, #3000
	mvn	r0, #0
	strh	r4, [r5, #2]	@ movhi
	strb	r2, [r5, #6]
	strh	r0, [r1, r3]	@ movhi
	mov	r0, r5
	mov	r1, r4
	bl	ftl_sb_update_avl_pages
.L1198:
	ldr	r0, [sp, #16]
	add	sp, sp, #28
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, sl, fp, pc}
.L1205:
	.align	2
.L1204:
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
	ldr	r5, .L1264
	.pad #28
	sub	sp, sp, #28
	ldr	r4, .L1264+4
	mov	r1, r6
	ldr	r3, .L1264+8
	mov	r7, #3936
	ldr	r2, [r5, #3944]
	ldr	r0, [r4, #-1036]
	mov	sl, r4
	strh	r6, [r4, r3]	@ movhi
	movw	r3, #3962
	strh	r6, [r5, r3]	@ movhi
	mov	r2, r2, asl #2
	bl	ftl_memset
	ldr	r2, [r5, #3944]
	mov	r1, r6
	ldr	r0, [r4, #-1040]
	mov	r2, r2, asl #1
	bl	ftl_memset
	ldrh	r2, [r5, r7]
	mov	r1, r6
	ldr	r0, [r4, #-908]
	mov	r2, r2, asl #2
	bl	ftl_memset
	ldrh	r2, [r5, r7]
	mov	r1, r6
	ldr	r0, [r5, #3964]
	mov	r2, r2, asl #1
	bl	ftl_memset
	ldr	r0, .L1264+12
	mov	r1, #255
	mov	r2, #12
	bl	ftl_memset
	mov	r3, #3856
	ldrh	r3, [r5, r3]
	str	r3, [sp, #8]
	b	.L1207
.L1209:
	add	r3, r6, r5
	ldr	r1, [sp, #8]
	ldrb	r0, [r3, #3874]	@ zero_extendqisi2
	bl	V2P_block
	str	r0, [sp, #0]
	bl	FtlBbmIsBadBlock
	ldr	r3, [sp, #0]
	cmp	r0, #0
	bne	.L1208
	ldr	r2, [r4, #-972]
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
.L1208:
	add	r5, r5, #1
	uxth	r5, r5
	b	.L1242
.L1255:
	mov	r7, #0
	movw	fp, #3848
	mov	r5, r7
	mov	r9, #36
	movw	r8, #3928
.L1242:
	ldrh	r3, [r6, fp]
	cmp	r3, r5
	bhi	.L1209
	cmp	r7, #0
	beq	.L1210
	ldr	r0, [r4, #-972]
	mov	r1, r7
	mov	r2, #1
	mov	fp, r7
	bl	FlashReadPages
	mov	r8, #0
	str	r8, [sp, #12]
.L1241:
	ldr	r3, [r4, #-972]
	add	r2, r3, r8
	ldr	r3, [r3, r8]
	ldr	r6, [r2, #4]
	cmn	r3, #1
	ldr	r5, [r2, #12]
	ubfx	r6, r6, #10, #16
	bne	.L1211
	mov	r7, #16
.L1213:
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
	beq	.L1211
.L1212:
	ldr	r3, [r4, #-972]
	ldr	r3, [r3, r8]
	cmn	r3, #1
	bne	.L1211
	sub	r7, r7, #1
	uxth	r7, r7
	cmp	r7, #0
	bne	.L1213
.L1211:
	ldr	r3, [r4, #-972]
	ldr	r3, [r3, r8]
	cmn	r3, #1
	beq	.L1214
	ldr	r2, [r4, #-1804]
	ldr	r3, [r5, #4]
	cmn	r2, #1
	beq	.L1215
	cmp	r2, r3
	bhi	.L1216
.L1215:
	cmn	r3, #1
	addne	r3, r3, #1
	strne	r3, [sl, #-1804]
.L1216:
	ldrh	r3, [r5, #0]
	movw	r2, #61604
	cmp	r3, r2
	beq	.L1219
	bhi	.L1222
	movw	r2, #61574
	cmp	r3, r2
	bne	.L1217
	b	.L1262
.L1222:
	movw	r2, #61634
	cmp	r3, r2
	beq	.L1220
	movw	r2, #65535
	cmp	r3, r2
	bne	.L1217
	b	.L1263
.L1220:
	ldr	r3, .L1264
	mov	r9, r6
	ldr	r1, .L1264+8
	ldr	ip, [r4, #-1036]
	ldr	r0, [r3, #3944]
	ldrh	r1, [r4, r1]
	uxth	r2, r0
	sub	r3, r2, #1
	rsb	r2, r1, r2
	sub	r2, r2, #1
	uxth	r3, r3
	sxth	r2, r2
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
	ldrne	r0, .L1264+8
	strneh	r1, [r4, r0]	@ movhi
.L1225:
	mov	r0, #0
	sxth	r9, r3
	str	r3, [sp, #4]
	b	.L1226
.L1227:
	ldr	r7, [r4, #-1036]
	add	ip, r1, #1
	add	r0, r0, #1
	ldr	r3, [r7, ip, asl #2]
	mov	ip, ip, asl #1
	uxth	r0, r0
	str	r3, [r7, r1, asl #2]
	mov	r1, r1, asl #1
	ldr	r7, [r4, #-1040]
	ldrh	ip, [r7, ip]
	strh	ip, [r7, r1]	@ movhi
.L1226:
	sxth	r1, r0
	cmp	r1, r9
	bne	.L1227
	ldr	r1, [sl, #-1036]
	mov	r2, r2, asl #1
	ldr	r0, [r5, #4]
	ldr	ip, [sp, #16]
	ldr	r3, [sp, #4]
	str	r0, [r1, ip]
	ldr	r1, [sl, #-1040]
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
	blt	.L1217
	ldr	r2, .L1264+8
	ldr	r0, .L1264
	ldrh	r1, [r4, r2]
	ldr	r0, [r0, #3944]
	rsb	r0, r1, r0
	sub	r0, r0, #1
	sxth	r0, r0
	cmp	r3, r0
	bgt	.L1217
	add	r1, r1, #1
	strh	r1, [r4, r2]	@ movhi
	ldr	r2, [r4, #-1036]
	ldr	r1, [r5, #4]
	str	r1, [r2, r3, asl #2]
	mov	r3, r3, asl #1
	ldr	r2, [r4, #-1040]
	b	.L1259
.L1262:
	ldr	r1, .L1264
	mov	r3, #3936
	movw	ip, #3962
	mov	r9, r6
	ldrh	r0, [r1, r3]
	ldrh	r1, [r1, ip]
	sub	r2, r0, #1
	ldr	ip, [r4, #-908]
	uxth	r3, r2
	rsb	r2, r1, r2
	str	r2, [sp, #4]
	b	.L1230
.L1236:
	mov	r6, r2, asl #2
	ldr	r7, [r5, #4]
	str	r6, [sp, #16]
	ldr	r6, [ip, r2, asl #2]
	cmp	r7, r6
	bls	.L1231
	ldr	ip, [ip, #0]
	mov	r6, r9
	cmp	ip, #0
	bne	.L1232
	cmp	r1, r0
	addne	r1, r1, #1
	ldrne	ip, .L1264
	movwne	r0, #3962
	strneh	r1, [ip, r0]	@ movhi
.L1232:
	sxth	ip, r3
	mov	r0, #0
	str	ip, [sp, #20]
	ldr	r9, .L1264
	str	r3, [sp, #4]
	b	.L1233
.L1234:
	ldr	r7, [r4, #-908]
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
.L1233:
	ldr	ip, [sp, #20]
	sxth	r1, r0
	cmp	r1, ip
	bne	.L1234
	ldr	r1, [sl, #-908]
	mov	r2, r2, asl #1
	ldr	r0, [r5, #4]
	ldr	ip, [sp, #16]
	ldr	r3, [sp, #4]
	str	r0, [r1, ip]
	ldr	r1, .L1264
	ldr	r1, [r1, #3964]
	strh	r6, [r1, r2]	@ movhi
	b	.L1235
.L1231:
	sub	r3, r3, #1
	uxth	r3, r3
.L1230:
	ldr	r6, [sp, #4]
	sxth	r2, r3
	cmp	r2, r6
	bgt	.L1236
	mov	r6, r9
.L1235:
	sxth	r3, r3
	cmp	r3, #0
	blt	.L1217
	ldr	r2, .L1264
	mov	ip, #3936
	movw	r1, #3962
	ldrh	ip, [r2, ip]
	ldrh	r0, [r2, r1]
	sub	ip, ip, #1
	rsb	ip, r0, ip
	sxth	ip, ip
	cmp	r3, ip
	bgt	.L1217
	add	r0, r0, #1
	strh	r0, [r2, r1]	@ movhi
	ldr	r0, [r5, #4]
	ldr	r1, [r4, #-908]
	str	r0, [r1, r3, asl #2]
	mov	r3, r3, asl #1
	ldr	r2, [r2, #3964]
.L1259:
	strh	r6, [r2, r3]	@ movhi
	b	.L1217
.L1219:
	ldr	r3, .L1264+16
	movw	ip, #65535
	ldrh	r2, [r4, r3]
	cmp	r2, ip
	streqh	r6, [r4, r3]	@ movhi
	beq	.L1260
	ldr	r3, .L1264+20
	movw	r1, #65535
	ldrh	r0, [r4, r3]
	cmp	r0, r1
	beq	.L1238
	mov	r1, #1
	bl	FtlFreeSysBlkQueueIn
.L1238:
	ldr	r3, [r5, #4]
	ldr	r2, [sl, #-1764]
	cmp	r2, r3
	ldr	r3, .L1264+20
	strcsh	r6, [r4, r3]	@ movhi
	bcs	.L1217
	ldr	r2, .L1264+16
	ldrh	r1, [r4, r2]
	strh	r6, [r4, r2]	@ movhi
	strh	r1, [r4, r3]	@ movhi
.L1260:
	ldr	r3, [r5, #4]
	str	r3, [r4, #-1764]
	b	.L1217
.L1263:
	mov	r0, r6
	b	.L1261
.L1214:
	ldr	r3, .L1264
	mov	r0, r6
	ldrb	r1, [r3, #928]	@ zero_extendqisi2
	cmp	r1, #0
	beq	.L1240
.L1261:
	mov	r1, #0
.L1240:
	bl	FtlFreeSysBlkQueueIn
.L1217:
	ldr	r6, [sp, #12]
	add	r8, r8, #36
	add	r3, r6, #1
	uxth	r3, r3
	str	r3, [sp, #12]
	cmp	r3, fp
	bne	.L1241
.L1210:
	ldr	ip, [sp, #8]
	add	r3, ip, #1
	uxth	r3, r3
	str	r3, [sp, #8]
.L1207:
	ldr	r6, .L1264
	movw	r3, #3858
	ldr	ip, [sp, #8]
	ldrh	r3, [r6, r3]
	cmp	r3, ip
	bhi	.L1255
	ldr	r1, .L1264+4
	ldr	r2, [r1, #-1040]
	ldrh	r3, [r2, #0]
	cmp	r3, #0
	bne	.L1243
	ldr	r0, .L1264+8
	ldrh	r1, [r1, r0]
	cmp	r1, #0
	ldrne	r0, [r6, #3944]
	bne	.L1244
	b	.L1243
.L1248:
	mov	r1, r1, asl #1
	ldrh	r1, [r2, r1]
	cmp	r1, #0
	beq	.L1245
	ldr	r7, .L1264
	sxth	r6, r3
	ldr	r1, .L1264+4
	mov	r5, #0
	b	.L1246
.L1247:
	ldr	ip, [r1, #-1040]
	mov	r0, r2, asl #1
	rsb	r4, r6, r2
	add	r3, r3, #1
	ldrh	sl, [ip, r0]
	mov	r8, r4, asl #1
	uxth	r3, r3
	strh	sl, [ip, r8]	@ movhi
	ldr	ip, [r1, #-1036]
	ldr	r2, [ip, r2, asl #2]
	str	r2, [ip, r4, asl #2]
	ldr	r2, [r1, #-1040]
	strh	r5, [r2, r0]	@ movhi
.L1246:
	ldr	r0, [r7, #3944]
	sxth	r2, r3
	cmp	r2, r0
	bcc	.L1247
	b	.L1243
.L1245:
	add	r3, r3, #1
	uxth	r3, r3
.L1244:
	sxth	r1, r3
	cmp	r1, r0
	bcc	.L1248
.L1243:
	ldr	r2, .L1264
	ldr	r1, [r2, #3964]
	ldrh	r3, [r1, #0]
	cmp	r3, #0
	bne	.L1249
	movw	r0, #3962
	ldrh	r0, [r2, r0]
	cmp	r0, #0
	movne	r0, #3936
	ldrneh	r2, [r2, r0]
	bne	.L1250
	b	.L1249
.L1254:
	mov	ip, r0, asl #1
	ldrh	ip, [r1, ip]
	cmp	ip, #0
	beq	.L1251
	ldr	r1, .L1264
	mov	r8, #3936
	ldr	r7, .L1264+4
	mov	r6, #0
	b	.L1252
.L1253:
	ldr	r4, [r1, #3964]
	mov	ip, r2, asl #1
	rsb	r5, r0, r2
	add	r3, r3, #1
	ldrh	r9, [r4, ip]
	mov	sl, r5, asl #1
	uxth	r3, r3
	strh	r9, [r4, sl]	@ movhi
	ldr	r4, [r7, #-908]
	ldr	r2, [r4, r2, asl #2]
	str	r2, [r4, r5, asl #2]
	ldr	r2, [r1, #3964]
	strh	r6, [r2, ip]	@ movhi
.L1252:
	ldrh	ip, [r1, r8]
	sxth	r2, r3
	cmp	r2, ip
	blt	.L1253
	b	.L1249
.L1251:
	add	r3, r3, #1
	uxth	r3, r3
.L1250:
	sxth	r0, r3
	cmp	r0, r2
	blt	.L1254
.L1249:
	mov	r0, #0
	add	sp, sp, #28
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, sl, fp, pc}
.L1265:
	.align	2
.L1264:
	.word	.LANCHOR0
	.word	.LANCHOR2
	.word	-1044
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
	ldr	r3, .L1277
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
	bne	.L1270
	b	.L1275
.L1273:
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
	bne	.L1271
	ldr	r3, [sp, #44]
	cmn	r3, #1
	bne	.L1271
	ldr	r3, [sp, #4]
	cmn	r3, #1
	subne	r4, r6, #1
	uxthne	r4, r4
	bne	.L1275
.L1271:
	add	r6, r6, #1
	uxth	r8, r6
.L1275:
	sxth	r6, r8
	sxth	r3, r4
	cmp	r6, r3
	ble	.L1273
.L1270:
	sxth	r0, r4
	add	sp, sp, #108
	ldmfd	sp!, {r4, r5, r6, r7, r8, sl, pc}
.L1278:
	.align	2
.L1277:
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
	ldr	r4, .L1295
	mov	r1, r5
	ldr	r2, .L1295+4
	ldr	r7, .L1295+8
	ldr	r3, [r4, #-936]
	str	r5, [r4, #188]
	ldr	r0, [r4, #-2016]
	str	r3, [r4, #192]
	mov	r3, #3856
	ldrh	r2, [r2, r3]
	mov	r2, r2, asl #1
	bl	ftl_memset
	ldrh	r0, [r4, r7]
	movw	r3, #65535
	cmp	r0, r3
	beq	.L1293
	mov	r1, #1
	bl	FtlGetLastWrittenPage
	ldr	r3, .L1295+12
	add	r2, r0, #1
	mov	r6, r0
	strh	r2, [r4, r3]	@ movhi
	b	.L1281
.L1284:
	ldrh	r2, [r4, r7]
	mov	r1, #1
	ldr	r0, .L1295+16
	orr	r3, r3, r2, asl #10
	str	r3, [r4, #184]
	ldr	r3, [r4, #-960]
	mov	r2, r1
	str	r3, [r4, #188]
	bl	FlashReadPages
	ldr	r3, [r4, #180]
	cmn	r3, #1
	beq	.L1282
	ldr	r3, [r4, #-960]
	ldr	r2, [r3, #0]
	ldr	r3, .L1295+20
	cmp	r2, r3
	bne	.L1282
	ldr	r3, [r4, #-936]
	ldrh	r2, [r3, #0]
	movw	r3, #61604
	cmp	r2, r3
	beq	.L1283
.L1282:
	sub	r5, r5, #1
	uxth	r5, r5
.L1281:
	add	r3, r5, r6
	sxth	r3, r3
	cmp	r3, #0
	bge	.L1284
.L1283:
	ldr	r4, .L1295
	mov	r2, #48
	ldr	r5, .L1295+4
	mov	r6, #3856
	ldr	r0, .L1295+24
	ldr	r7, [r4, #188]
	mov	r1, r7
	bl	memcpy
	ldrh	r2, [r5, r6]
	add	r1, r7, #48
	ldr	r0, [r4, #-2016]
	mov	r2, r2, asl #1
	bl	memcpy
	ldrh	r1, [r5, r6]
	ldr	r3, [r4, #188]
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
	beq	.L1285
	ldrh	r3, [r5, r6]
	mov	r2, #3952
	ldrh	r2, [r5, r2]
	ldr	r0, [r4, #-900]
	mov	r1, r3, lsr #3
	add	r1, r1, r3, asl #1
	ldr	r3, [r4, #188]
	add	r1, r1, #52
	mov	r2, r2, asl #2
	ubfx	r1, r1, #2, #14
	add	r1, r3, r1, asl #2
	bl	memcpy
.L1285:
	ldr	r4, .L1295
	ldr	r3, .L1295+20
	ldr	r2, [r4, #-2072]
	cmp	r2, r3
	bne	.L1293
	ldr	r3, .L1295+28
	movw	r1, #3870
	ldrb	r0, [r4, #-2062]	@ zero_extendqisi2
	ldrh	r2, [r4, r3]
	ldr	r3, .L1295+32
	strh	r2, [r4, r3]	@ movhi
	ldr	r3, .L1295+4
	ldrh	r1, [r3, r1]
	cmp	r0, r1
	bne	.L1293
	movw	r1, #3916
	movw	r0, #3922
	ldrh	r1, [r3, r1]
	movw	r5, #65535
	ldrh	r0, [r3, r0]
	str	r2, [r4, #216]
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
	ldr	r3, .L1295+36
	ldr	r1, .L1295+40
	strh	r0, [r4, r3]	@ movhi
	ldr	r3, .L1295+44
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
	ldr	r0, .L1295+48
	strb	r3, [r4, #-1988]
	ldr	r3, .L1295+52
	strh	r1, [r4, r3]	@ movhi
	mov	r3, #0
	ldr	r1, .L1295+56
	strh	r3, [r4, r0]	@ movhi
	strb	r3, [r4, #-1754]
	ldrh	r0, [r4, r1]
	add	r1, r1, #106
	strb	r3, [r4, #-1752]
	str	r3, [r4, #-1816]
	strh	r0, [r4, r1]	@ movhi
	sub	r1, r1, #104
	ldr	r0, .L1295+60
	ldrh	r1, [r4, r1]
	mov	ip, r1, lsr #6
	and	r1, r1, #63
	strb	r1, [r4, #-1942]
	ldrb	r1, [r4, #-2060]	@ zero_extendqisi2
	strh	ip, [r4, r0]	@ movhi
	strb	r1, [r4, #-1940]
	ldr	r1, .L1295+64
	ldrh	r0, [r4, r1]
	add	r1, r1, #150
	strh	r0, [r4, r1]	@ movhi
	ldr	r1, .L1295+68
	ldr	r0, .L1295+72
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
	ldr	r4, .L1295
	ldr	r3, [r4, #-2036]
	ldr	r1, [r4, #-1800]
	cmp	r3, r1
	strhi	r3, [r4, #-1800]
	cmp	r2, r5
	beq	.L1288
	ldr	r0, .L1295+76
	bl	make_superblock
.L1288:
	ldr	r3, .L1295+80
	ldrh	r3, [r4, r3]
	cmp	r3, r5
	beq	.L1289
	ldr	r0, .L1295+84
	bl	make_superblock
.L1289:
	ldr	r5, .L1295
	movw	r4, #65535
	ldr	r3, .L1295+88
	ldrh	r3, [r5, r3]
	cmp	r3, r4
	beq	.L1290
	ldr	r0, .L1295+92
	bl	make_superblock
.L1290:
	ldr	r3, .L1295+52
	ldrh	r3, [r5, r3]
	cmp	r3, r4
	beq	.L1294
	ldr	r0, .L1295+96
	bl	make_superblock
	mov	r0, #0
	ldmfd	sp!, {r3, r4, r5, r6, r7, pc}
.L1293:
	mvn	r0, #0
	ldmfd	sp!, {r3, r4, r5, r6, r7, pc}
.L1294:
	mov	r0, #0
	ldmfd	sp!, {r3, r4, r5, r6, r7, pc}
.L1296:
	.align	2
.L1295:
	.word	.LANCHOR2
	.word	.LANCHOR0
	.word	-1772
	.word	-1770
	.word	.LANCHOR2+180
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
	ldr	r6, .L1313
	ldr	r8, .L1313+4
	ldr	r3, [r6, #-960]
	mov	r7, r6
	ldr	r4, [r6, #-936]
	str	r3, [r6, #188]
	str	r4, [r6, #192]
	bl	FtlBbtMemInit
	movw	r3, #3912
	ldrh	r5, [r8, r3]
	sub	r5, r5, #1
	uxth	r5, r5
	b	.L1298
.L1302:
	mov	r1, #1
	mov	r3, r5, asl #10
	ldr	r0, .L1313+8
	mov	r2, r1
	str	r3, [r6, #184]
	bl	FlashReadPages
	ldr	r3, [r6, #180]
	cmn	r3, #1
	bne	.L1299
	ldr	r3, [r6, #184]
	mov	r1, #1
	ldr	r0, .L1313+8
	mov	r2, r1
	add	r3, r3, #1
	str	r3, [r6, #184]
	bl	FlashReadPages
.L1299:
	ldr	r3, [r7, #180]
	cmn	r3, #1
	beq	.L1300
	ldrh	r2, [r4, #0]
	movw	r3, #61649
	cmp	r2, r3
	bne	.L1300
	ldr	r3, .L1313+4
	movw	r2, #3980
	strh	r5, [r3, r2]	@ movhi
	ldr	r2, [r4, #4]
	str	r2, [r3, #3988]
	mov	r2, #3984
	ldrh	r1, [r4, #8]
	strh	r1, [r3, r2]	@ movhi
	b	.L1301
.L1300:
	sub	r5, r5, #1
	uxth	r5, r5
.L1298:
	movw	r3, #3912
	ldrh	r3, [r8, r3]
	sub	r3, r3, #48
	cmp	r5, r3
	bgt	.L1302
.L1301:
	ldr	r5, .L1313+4
	movw	r8, #3980
	movw	r3, #65535
	ldrh	r2, [r5, r8]
	cmp	r2, r3
	beq	.L1312
	mov	r6, #3984
	ldrh	r2, [r5, r6]
	cmp	r2, r3
	beq	.L1304
	ldr	r7, .L1313
	mov	r1, #1
	mov	r2, r2, asl #10
	add	r0, r7, #180
	str	r2, [r7, #184]
	mov	r2, r1
	bl	FlashReadPages
	ldr	r3, [r7, #180]
	cmn	r3, #1
	beq	.L1304
	ldrh	r2, [r4, #0]
	movw	r3, #61649
	cmp	r2, r3
	bne	.L1304
	ldr	r3, [r4, #4]
	ldr	r2, [r5, #3988]
	cmp	r3, r2
	strhi	r3, [r5, #3988]
	ldrhih	r2, [r5, r6]
	ldrhih	r3, [r4, #8]
	strhih	r2, [r5, r8]	@ movhi
	strhih	r3, [r5, r6]	@ movhi
.L1304:
	ldr	r8, .L1313+4
	movw	r3, #3980
	mov	r1, #1
	mov	r6, #0
	ldr	r5, .L1313
	ldrh	r0, [r8, r3]
	bl	FtlGetLastWrittenPage
	movw	r3, #3982
	add	r2, r0, #1
	mov	r7, r0
	strh	r2, [r8, r3]	@ movhi
	b	.L1305
.L1308:
	movw	r2, #3980
	mov	r1, #1
	ldrh	r2, [r8, r2]
	ldr	r0, .L1313+8
	orr	r3, r3, r2, asl #10
	str	r3, [r5, #184]
	ldr	r3, [r5, #-960]
	mov	r2, r1
	str	r3, [r5, #188]
	bl	FlashReadPages
	ldr	r3, [r5, #180]
	cmn	r3, #1
	beq	.L1306
	ldrh	r2, [r4, #0]
	movw	r3, #61649
	cmp	r2, r3
	beq	.L1307
.L1306:
	sub	r6, r6, #1
	uxth	r6, r6
.L1305:
	add	r3, r6, r7
	sxth	r3, r3
	cmp	r3, #0
	bge	.L1308
.L1307:
	ldr	r3, .L1313+4
	movw	r2, #3986
	ldrh	r1, [r4, #10]
	ldrh	r0, [r4, #12]
	strh	r1, [r3, r2]	@ movhi
	movw	r2, #65535
	cmp	r0, r2
	beq	.L1309
	ldr	r2, [r3, #3844]
	cmp	r0, r2
	beq	.L1309
	movw	r1, #3858
	ldrh	r3, [r3, r1]
	mov	r3, r3, lsr #2
	cmp	r2, r3
	bcs	.L1309
	cmp	r0, r3
	bcs	.L1309
	bl	FtlSysBlkNumInit
.L1309:
	ldr	r6, .L1313+12
	mov	r4, #0
	ldr	r8, .L1313+4
	movw	r7, #3870
	ldr	r5, .L1313
	b	.L1310
.L1311:
	ldr	r3, .L1313+16
	ldr	r1, [r5, #188]
	ldr	r0, [r6, #4]!
	ldrh	r2, [r5, r3]
	mov	r2, r2, asl #2
	mla	r1, r4, r2, r1
	bl	memcpy
	add	r4, r4, #1
.L1310:
	ldrh	r3, [r8, r7]
	cmp	r4, r3
	bcc	.L1311
	mov	r0, #0
	ldmfd	sp!, {r4, r5, r6, r7, r8, pc}
.L1312:
	mvn	r0, #0
	ldmfd	sp!, {r4, r5, r6, r7, r8, pc}
.L1314:
	.align	2
.L1313:
	.word	.LANCHOR2
	.word	.LANCHOR0
	.word	.LANCHOR2+180
	.word	.LANCHOR0+4004
	.word	-1028
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
	ldr	r3, .L1322
	movw	fp, #3870
	ldr	r7, .L1322+4
	ldr	r6, .L1322+8
	ldr	r2, [r3, #-960]
	ldr	r8, [r3, #-936]
	str	r2, [r3, #188]
	str	r8, [r3, #192]
	b	.L1316
.L1321:
	movw	r9, #3912
	mvn	r3, #0
	ldrh	r4, [r6, r9]
	strh	r3, [r7], #2	@ movhi
	add	r4, r4, r3
	ldr	sl, .L1322
	uxth	r4, r4
	b	.L1317
.L1320:
	mla	r3, r3, r5, r4
	mov	r1, #1
	ldr	r0, .L1322+12
	mov	r2, r1
	mov	r3, r3, asl #10
	str	r3, [sl, #184]
	bl	FlashReadPages
	ldr	r3, [sl, #180]
	cmn	r3, #1
	beq	.L1318
	ldrh	r2, [r8, #0]
	movw	r3, #61664
	cmp	r2, r3
	streqh	r4, [r7, #-2]	@ movhi
	beq	.L1319
.L1318:
	sub	r4, r4, #1
	uxth	r4, r4
.L1317:
	ldrh	r3, [r6, r9]
	sub	r2, r3, #16
	cmp	r4, r2
	bgt	.L1320
.L1319:
	add	r5, r5, #1
.L1316:
	ldrh	r3, [r6, fp]
	cmp	r5, r3
	bcc	.L1321
	mov	r0, #0
	ldmfd	sp!, {r3, r4, r5, r6, r7, r8, r9, sl, fp, pc}
.L1323:
	.align	2
.L1322:
	.word	.LANCHOR2
	.word	.LANCHOR0+3992
	.word	.LANCHOR0
	.word	.LANCHOR2+180
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
	ldr	r3, .L1343
	mov	r6, r0
	mov	r8, #0
	ldr	r7, .L1343+4
	ldrb	fp, [r3, #2901]	@ zero_extendqisi2
	b	.L1325
.L1332:
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
	bcs	.L1327
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
.L1327:
	add	r8, r8, #1
	add	r6, r6, #36
.L1325:
	cmp	r8, sl
	bne	.L1332
	ldr	r3, [sp, #12]
	cmp	r3, #0
	movne	r6, #0
	ldrne	r5, .L1343+8
	bne	.L1333
	b	.L1334
.L1339:
	ldr	r3, [r4, #0]
	cmn	r3, #1
	bne	.L1335
	ldr	r1, [r4, #4]
	ldr	r0, .L1343+12
	bl	printk
	b	.L1336
.L1335:
	rsb	r3, r6, sl
	mov	r1, r9
	add	r2, sp, #20
	mov	r0, r4
	uxtb	r3, r3
	str	r3, [sp, #0]
	add	r3, sp, #24
	bl	LogAddr2PhyAddr
	ldr	r2, [r5, #220]
	mov	r3, #0
	mov	lr, r4
	add	ip, sp, #28
	str	r3, [r2, #0]
	ldr	r2, [r5, #224]
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
	ldr	r3, [r5, #220]
	str	r3, [sp, #36]
	ldr	r3, [r5, #224]
	str	r3, [sp, #40]
	bl	FlashReadPages
	ldr	r7, [sp, #28]
	cmn	r7, #1
	bne	.L1337
	ldr	r0, .L1343+16
	ldr	r1, [r4, #4]
	bl	printk
	str	r7, [r4, #0]
.L1337:
	ldr	r3, [r4, #12]
	cmp	r3, #0
	beq	.L1338
	ldr	r2, [r3, #0]
	ldr	r3, [r5, #224]
	ldr	r3, [r3, #0]
	cmp	r2, r3
	beq	.L1338
	ldr	r0, .L1343+20
	ldr	r1, [r4, #4]
	bl	printk
	mvn	r3, #0
	str	r3, [r4, #0]
.L1338:
	ldr	r3, [r4, #8]
	cmp	r3, #0
	beq	.L1336
	ldr	r2, [r3, #0]
	ldr	r3, [r5, #220]
	ldr	r3, [r3, #0]
	cmp	r2, r3
	beq	.L1336
	ldr	r0, .L1343+24
	ldr	r1, [r4, #4]
	bl	printk
	mvn	r3, #0
	str	r3, [r4, #0]
.L1336:
	add	r6, r6, #1
	add	r4, r4, #36
.L1333:
	cmp	r6, sl
	bne	.L1339
.L1334:
	mov	r0, #0
	add	sp, sp, #68
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, sl, fp, pc}
.L1344:
	.align	2
.L1343:
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
	ldr	r5, .L1373
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
	ldr	ip, .L1373+4
	ldrb	ip, [ip, #2901]	@ zero_extendqisi2
	str	ip, [sp, #12]
	beq	.L1346
	bl	FlashProgSlc2KPages
	b	.L1347
.L1360:
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
	bcs	.L1349
	ldrb	r1, [r5, #3837]	@ zero_extendqisi2
	add	r2, fp, r2, asl #4
	cmp	r1, #0
	ldr	r2, [r2, #3636]
	moveq	r8, #0
	cmp	r2, #0
	beq	.L1351
	cmp	r3, #1
	bne	.L1352
	ldr	r0, [r5, #3012]
	bl	NandcIqrWaitFlashReady
.L1352:
	ldrb	r0, [sp, #32]	@ zero_extendqisi2
	bl	FlashWaitCmdDone
.L1351:
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
	bne	.L1354
	bl	NandcWaitFlashReady
	b	.L1355
.L1354:
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
.L1355:
	ldr	r2, [sp, #20]
	sub	r3, r2, #1
	cmp	r3, #6
	bhi	.L1356
	add	r3, r5, r4
	ldrb	r3, [r3, #3756]	@ zero_extendqisi2
	cmp	r3, #0
	beq	.L1356
	mov	r0, r4
	ldrb	r1, [r5, #1]	@ zero_extendqisi2
	ldr	r2, .L1373+8
	mov	r3, #0
	bl	HynixSetRRPara
.L1356:
	mov	r0, r4
	bl	NandcFlashCs
	cmp	sl, #1
	mov	r0, r4
	bne	.L1357
	ldrb	r3, [r5, #928]	@ zero_extendqisi2
	cmp	r3, #0
	beq	.L1357
	bl	flash_enter_slc_mode
	b	.L1358
.L1357:
	bl	flash_exit_slc_mode
.L1358:
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
	beq	.L1359
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
.L1359:
	mov	r0, r4
	ldr	r1, [sp, #28]
	bl	FlashProgSecondCmd
	mov	r0, r4
	bl	NandcFlashDeCs
	add	r6, r6, r8
.L1349:
	add	r6, r6, #1
.L1346:
	ldr	r3, [sp, #8]
	cmp	r6, r3
	bcc	.L1360
	ldr	r5, .L1373
	mov	r4, #0
	ldr	r6, .L1373+12
	ldr	r0, [r5, #3012]
	bl	NandcIqrWaitFlashReady
	b	.L1361
.L1363:
	uxtb	r0, r4
	bl	FlashWaitCmdDone
	cmp	sl, #1
	bne	.L1362
	ldrb	r3, [r5, #928]	@ zero_extendqisi2
	cmp	r3, #0
	beq	.L1362
	ldrb	r0, [r6, r4, asl #4]	@ zero_extendqisi2
	bl	flash_exit_slc_mode
.L1362:
	add	r4, r4, #1
.L1361:
	ldrb	r3, [r5, #3762]	@ zero_extendqisi2
	cmp	r4, r3
	bcc	.L1363
	ldr	r2, [sp, #16]
	cmp	r2, #0
	ldreq	r0, [sp, #16]
	beq	.L1347
	mov	r5, #0
	ldr	r4, .L1373+16
	ldr	r6, [sp, #8]
	b	.L1364
.L1369:
	ldr	r3, [r9, #0]
	cmn	r3, #1
	bne	.L1365
	ldr	r1, [r9, #4]
	ldr	r0, .L1373+20
	bl	printk
	b	.L1366
.L1365:
	rsb	r3, r5, r6
	mov	r1, sl
	add	r2, sp, #28
	mov	r0, r9
	uxtb	r3, r3
	str	r3, [sp, #0]
	add	r3, sp, #32
	bl	LogAddr2PhyAddr
	ldr	r2, [r4, #220]
	mov	r3, #0
	mov	lr, r9
	add	ip, sp, #36
	str	r3, [r2, #0]
	ldr	r2, [r4, #224]
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
	ldr	r3, [r4, #220]
	str	r3, [sp, #44]
	ldr	r3, [r4, #224]
	str	r3, [sp, #48]
	bl	FlashReadPages
	ldr	r7, [sp, #36]
	cmn	r7, #1
	bne	.L1367
	ldr	r0, .L1373+24
	ldr	r1, [r9, #4]
	bl	printk
	str	r7, [r9, #0]
.L1367:
	ldr	r3, [r9, #12]
	cmp	r3, #0
	beq	.L1368
	ldr	r2, [r3, #0]
	ldr	r3, [r4, #224]
	ldr	r3, [r3, #0]
	cmp	r2, r3
	beq	.L1368
	ldr	r0, .L1373+28
	ldr	r1, [r9, #4]
	bl	printk
	mvn	r3, #0
	str	r3, [r9, #0]
.L1368:
	ldr	r3, [r9, #8]
	cmp	r3, #0
	beq	.L1366
	ldr	r2, [r3, #0]
	ldr	r3, [r4, #220]
	ldr	r3, [r3, #0]
	cmp	r2, r3
	beq	.L1366
	ldr	r0, .L1373+32
	ldr	r1, [r9, #4]
	bl	printk
	mvn	r3, #0
	str	r3, [r9, #0]
.L1366:
	add	r5, r5, #1
	add	r9, r9, #36
.L1364:
	cmp	r5, r6
	bne	.L1369
	mov	r0, #0
.L1347:
	add	sp, sp, #76
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, sl, fp, pc}
.L1374:
	.align	2
.L1373:
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
	ldr	r4, .L1387
	mov	r7, #3856
	ldr	r6, .L1387+4
	ldr	r3, [r4, #-960]
	ldr	r5, [r4, #-936]
	str	r3, [r4, #188]
	ldr	r3, .L1387+8
	str	r5, [r4, #192]
	ldrh	r3, [r4, r3]
	strh	r3, [r5, #2]	@ movhi
	ldr	r3, .L1387+12
	strh	r3, [r5, #0]	@ movhi
	ldr	r3, [r4, #-1764]
	str	r3, [r5, #4]
	mov	r3, #0
	str	r3, [r5, #8]
	str	r3, [r5, #12]
	ldr	r3, .L1387+16
	str	r3, [r4, #-2072]
	ldr	r3, .L1387+20
	str	r3, [r4, #-2068]
	ldr	r3, .L1387+24
	ldrh	r2, [r4, r3]
	ldr	r3, .L1387+28
	strh	r2, [r4, r3]	@ movhi
	movw	r3, #3870
	ldrh	r3, [r6, r3]
	strb	r3, [r4, #-2062]
	ldr	r3, .L1387+32
	ldrh	r2, [r4, r3]
	sub	r3, r3, #62
	strh	r2, [r4, r3]	@ movhi
	add	r3, r3, #64
	ldrh	r2, [r4, r3]
	ldrb	r3, [r4, #-1990]	@ zero_extendqisi2
	orr	r2, r3, r2, asl #6
	ldr	r3, .L1387+36
	strh	r2, [r4, r3]	@ movhi
	ldrb	r3, [r4, #-1988]	@ zero_extendqisi2
	strb	r3, [r4, #-2061]
	ldr	r3, .L1387+40
	ldrh	r2, [r4, r3]
	sub	r3, r3, #106
	strh	r2, [r4, r3]	@ movhi
	add	r3, r3, #108
	ldrh	r2, [r4, r3]
	ldrb	r3, [r4, #-1942]	@ zero_extendqisi2
	orr	r2, r3, r2, asl #6
	ldr	r3, .L1387+44
	strh	r2, [r4, r3]	@ movhi
	ldrb	r3, [r4, #-1940]	@ zero_extendqisi2
	strb	r3, [r4, #-2060]
	ldr	r3, .L1387+48
	ldrh	r2, [r4, r3]
	sub	r3, r3, #150
	strh	r2, [r4, r3]	@ movhi
	add	r3, r3, #152
	ldr	r0, [r4, #188]
	ldrh	r2, [r4, r3]
	ldrb	r3, [r4, #-1894]	@ zero_extendqisi2
	orr	r2, r3, r2, asl #6
	ldr	r3, .L1387+52
	strh	r2, [r4, r3]	@ movhi
	ldrb	r3, [r4, #-1892]	@ zero_extendqisi2
	strb	r3, [r4, #-2059]
	ldr	r3, [r4, #-1796]
	str	r3, [r4, #-2040]
	ldr	r3, [r4, #-1804]
	str	r3, [r4, #-2032]
	ldr	r3, [r4, #-1800]
	str	r3, [r4, #-2036]
	movw	r3, #3926
	ldrh	r2, [r6, r3]
	bl	ftl_memset
	ldr	r1, .L1387+56
	mov	r2, #48
	ldr	r0, [r4, #188]
	bl	memcpy
	ldrh	r2, [r6, r7]
	ldr	r0, [r4, #188]
	ldr	r1, [r4, #-2016]
	mov	r2, r2, asl #1
	add	r0, r0, #48
	bl	memcpy
	ldrh	r2, [r6, r7]
	ldr	r3, [r4, #188]
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
	beq	.L1376
	ldrh	r3, [r6, r7]
	ldr	r1, [r4, #188]
	mov	r0, r3, lsr #3
	add	r0, r0, r3, asl #1
	mov	r3, #3952
	add	r0, r0, #52
	ldrh	r2, [r6, r3]
	ubfx	r0, r0, #2, #14
	mov	r2, r2, asl #2
	add	r0, r1, r0, asl #2
	ldr	r1, [r4, #-1032]
	bl	memcpy
.L1376:
	mov	r0, #0
	ldr	r4, .L1387
	bl	FtlUpdateVaildLpn
	ldr	r8, .L1387+8
	mov	r6, #0
	movw	sl, #65535
.L1386:
	ldr	r3, [r4, #-960]
	mov	r1, #1
	ldr	r7, .L1387+60
	ldrh	r2, [r4, r8]
	str	r3, [r4, #188]
	ldr	r3, [r4, #-936]
	ldr	r0, .L1387+64
	str	r3, [r4, #192]
	ldrh	r3, [r4, r7]
	orr	r3, r3, r2, asl #10
	mov	r2, r1
	str	r3, [r4, #184]
	mov	r3, r1
	bl	FlashProgPages
	ldr	r1, .L1387+4
	movw	r3, #3918
	ldrh	r2, [r4, r7]
	ldrh	r3, [r1, r3]
	sub	r3, r3, #1
	cmp	r2, r3
	blt	.L1378
	ldr	r3, .L1387+68
	ldrh	r2, [r4, r8]
	ldrh	sl, [r4, r3]
	strh	r2, [r4, r3]	@ movhi
	mov	r3, #0
	strh	r3, [r4, r7]	@ movhi
	bl	FtlFreeSysBlkQueueOut
	ldr	r3, [r4, #-1804]
	mov	r1, #1
	add	r2, r3, #1
	str	r2, [r4, #-1804]
	str	r3, [r4, #-1764]
	mov	r2, r0, asl #10
	strh	r0, [r4, r8]	@ movhi
	str	r2, [r4, #184]
	mov	r2, r1
	str	r3, [r5, #4]
	mov	r3, r1
	strh	r0, [r5, #2]	@ movhi
	ldr	r0, .L1387+64
	bl	FlashProgPages
.L1378:
	ldr	r2, .L1387+60
	ldr	r1, [r4, #180]
	ldrh	r3, [r4, r2]
	cmn	r1, #1
	add	r3, r3, #1
	uxth	r3, r3
	strh	r3, [r4, r2]	@ movhi
	bne	.L1379
	cmp	r3, #1
	add	r6, r6, #1
	ldreq	r1, .L1387+4
	movweq	r3, #3918
	uxth	r6, r6
	ldreqh	r3, [r1, r3]
	subeq	r3, r3, #1
	streqh	r3, [r4, r2]	@ movhi
	cmp	r6, #3
	bls	.L1386
	ldr	r4, .L1387
	mov	r2, r6
	ldr	r0, .L1387+72
	ldr	r1, [r4, #184]
	bl	printk
	mov	r3, #1
	str	r3, [r4, #-2088]
	ldmfd	sp!, {r3, r4, r5, r6, r7, r8, sl, pc}
.L1379:
	cmp	r3, #1
	beq	.L1386
	cmp	r1, #256
	beq	.L1386
	movw	r3, #65535
	cmp	sl, r3
	ldmeqfd	sp!, {r3, r4, r5, r6, r7, r8, sl, pc}
	mov	r0, sl
	mov	r1, #1
	bl	FtlFreeSysBlkQueueIn
	ldmfd	sp!, {r3, r4, r5, r6, r7, r8, sl, pc}
.L1388:
	.align	2
.L1387:
	.word	.LANCHOR2
	.word	.LANCHOR0
	.word	-1772
	.word	-3932
	.word	1179929683
	.word	1342177352
	.word	-1766
	.word	-2064
	.word	-1996
	.word	-2056
	.word	-1948
	.word	-2052
	.word	-1900
	.word	-2048
	.word	.LANCHOR2-2072
	.word	-1770
	.word	.LANCHOR2+180
	.word	-1768
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
	ldr	r3, .L1391
	ldr	r3, [r3, #-2088]
	cmp	r3, #0
	bne	.L1390
	bl	FtlVpcTblFlush.part.13
.L1390:
	mov	r0, #0
	ldmfd	sp!, {r3, pc}
.L1392:
	.align	2
.L1391:
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
	ldr	r7, .L1406
	mov	r6, #0
	ldr	sl, .L1406+4
	movw	r9, #3870
	ldr	r8, .L1406+8
	ldr	r3, [r7, #-936]
	ldr	r0, [r7, #-960]
	str	r3, [r7, #192]
	movw	r3, #3926
	str	r0, [r7, #188]
	ldrh	r2, [sl, r3]
	bl	ftl_memset
	b	.L1394
.L1395:
	ldr	r3, .L1406+12
	ldr	r1, [r8, #4]!
	ldrh	r2, [r7, r3]
	ldr	r3, [r7, #188]
	mul	r0, r2, r6
	mov	r2, r2, asl #2
	add	r6, r6, #1
	add	r0, r3, r0, asl #2
	bl	memcpy
.L1394:
	ldrh	r3, [sl, r9]
	ldr	r4, .L1406+4
	cmp	r6, r3
	ldr	r5, .L1406
	blt	.L1395
	ldr	r6, [r5, #192]
	mov	r1, #255
	mov	r2, #16
	mov	r7, #0
	mov	fp, r7
	mov	r0, r6
	bl	ftl_memset
	ldr	r3, .L1406+16
	strh	r3, [r6, #0]	@ movhi
	ldr	r3, [r4, #3988]
	str	r3, [r6, #4]
	movw	r3, #3980
	ldrh	r3, [r4, r3]
	strh	r3, [r6, #2]	@ movhi
	mov	r3, #3984
	ldrh	r3, [r4, r3]
	strh	r3, [r6, #8]	@ movhi
	movw	r3, #3986
	ldrh	r3, [r4, r3]
	strh	r3, [r6, #10]	@ movhi
	ldr	r3, [r4, #3844]
	strh	r3, [r6, #12]	@ movhi
	b	.L1404
.L1401:
	mov	fp, #1
.L1404:
	ldr	r3, [r5, #-960]
	movw	r8, #3980
	ldr	r4, .L1406+4
	mov	r1, #0
	movw	sl, #3982
	str	r1, [r5, #180]
	str	r3, [r5, #188]
	mov	r9, #3984
	ldr	r3, [r5, #-936]
	ldrh	r1, [r4, r8]
	ldrh	r2, [r4, sl]
	str	r3, [r5, #192]
	ldrh	r0, [r6, #10]
	orr	r3, r2, r1, asl #10
	str	r3, [r5, #184]
	ldrh	r3, [r4, r9]
	str	r0, [sp, #0]
	ldr	r0, .L1406+20
	bl	printk
	mov	r1, #1
	mov	r2, r1
	mov	r3, r1
	ldr	r0, .L1406+24
	bl	FlashProgPages
	movw	r3, #3918
	ldrh	r3, [r4, r3]
	ldrh	r2, [r4, sl]
	sub	r3, r3, #1
	cmp	r2, r3
	blt	.L1397
	ldr	r3, [r4, #3988]
	mov	r1, #0	@ movhi
	ldrh	r2, [r4, r8]
	add	r3, r3, #1
	ldr	r0, [r5, #-2084]
	str	r3, [r4, #3988]
	str	r3, [r6, #4]
	ldrh	r3, [r4, r9]
	strh	r1, [r4, sl]	@ movhi
	mov	r1, #1
	strh	r2, [r6, #8]	@ movhi
	strh	r2, [r4, r9]	@ movhi
	mov	r2, r1
	strh	r3, [r4, r8]	@ movhi
	mov	r3, r3, asl #10
	str	r3, [r5, #184]
	str	r3, [r0, #4]
	bl	FlashEraseBlocks
	mov	r1, #1
	ldr	r0, .L1406+24
	mov	r2, r1
	mov	r3, r1
	bl	FlashProgPages
.L1397:
	ldr	r2, .L1406+4
	movw	r3, #3982
	ldr	r4, .L1406
	ldrh	r1, [r2, r3]
	add	r1, r1, #1
	strh	r1, [r2, r3]	@ movhi
	ldr	r3, [r5, #180]
	cmn	r3, #1
	bne	.L1398
	add	r7, r7, #1
	ldr	r0, .L1406+28
	ldr	r1, [r5, #184]
	uxth	r7, r7
	bl	printk
	cmp	r7, #3
	bls	.L1404
	ldr	r0, .L1406+32
	mov	r2, r7
	ldr	r1, [r4, #184]
	bl	printk
	mov	r3, #1
	str	r3, [r4, #-2088]
	b	.L1405
.L1398:
	cmp	fp, #0
	beq	.L1401
.L1405:
	ldmfd	sp!, {r1, r2, r3, r4, r5, r6, r7, r8, r9, sl, fp, pc}
.L1407:
	.align	2
.L1406:
	.word	.LANCHOR2
	.word	.LANCHOR0
	.word	.LANCHOR0+4004
	.word	-1028
	.word	-3887
	.word	.LC106
	.word	.LANCHOR2+180
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
	ldr	r3, .L1410
	ldr	r3, [r3, #-2088]
	cmp	r3, #0
	bne	.L1409
	bl	FtlBbmTblFlush.part.16
.L1409:
	mov	r0, #0
	ldmfd	sp!, {r3, pc}
.L1411:
	.align	2
.L1410:
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
	ldr	r4, .L1422
	ldr	r3, .L1422+4
	ldrh	r3, [r4, r3]
	cmp	r3, #0
	movne	r6, #0
	movne	sl, r4
	bne	.L1413
	b	.L1414
.L1420:
	add	r3, r3, r6
	mov	r1, r9
	mov	r5, #0
	ldr	r7, .L1422+4
	ldrb	r0, [r3, #3874]	@ zero_extendqisi2
	bl	V2P_block
	mov	r8, r0
	b	.L1415
.L1419:
	add	r3, r4, r5, asl #1
	sub	r3, r3, #1136
	ldrh	r3, [r3, #0]
	cmp	r3, r8
	bne	.L1416
	mov	r1, r8
	ldr	r0, .L1422+8
	bl	printk
	mov	r0, r8
	bl	FtlBbmMapBadBlock
	bl	FtlBbmTblFlush
	ldrh	r2, [r4, r7]
	mov	r3, r5
	b	.L1417
.L1418:
	add	r1, r3, #1
	add	r3, r4, r3, asl #1
	sub	r3, r3, #1136
	add	r0, r4, r1, asl #1
	sub	r0, r0, #1136
	ldrh	r0, [r0, #0]
	strh	r0, [r3, #0]	@ movhi
	uxth	r3, r1
.L1417:
	cmp	r3, r2
	bcc	.L1418
	sub	r2, r2, #1
	strh	r2, [sl, r7]	@ movhi
.L1416:
	add	r5, r5, #1
	uxth	r5, r5
.L1415:
	ldrh	r3, [r4, r7]
	cmp	r3, r5
	bhi	.L1419
	add	r6, r6, #1
	uxth	r6, r6
.L1413:
	ldr	r3, .L1422+12
	movw	r2, #3848
	ldrh	r2, [r3, r2]
	cmp	r2, r6
	bhi	.L1420
	bl	FtlGcReFreshBadBlk
.L1414:
	mov	r0, #0
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, sl, pc}
.L1423:
	.align	2
.L1422:
	.word	.LANCHOR2
	.word	-1138
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
	ldr	r3, .L1432
	mov	r4, r0
	ldr	r1, [r3, #-2016]
	ldrh	r2, [r1, r2]
	cmp	r2, #0
	bne	.L1425
	ldr	r1, .L1432+4
	ldrh	r0, [r3, r1]
	cmp	r0, r4
	mvneq	r2, #0
	streqh	r2, [r3, r1]	@ movhi
	beq	.L1427
	ldr	r1, .L1432+8
	ldrh	r1, [r3, r1]
	cmp	r1, r4
	beq	.L1428
	ldr	r1, .L1432+12
	ldrh	r1, [r3, r1]
	cmp	r1, r4
	beq	.L1428
	ldr	r1, .L1432+16
	ldrh	r3, [r3, r1]
	cmp	r3, r4
	beq	.L1428
.L1427:
	mov	r1, r4
	ldr	r0, .L1432+20
	bl	List_remove_node
	ldr	r5, .L1432
	ldr	r3, .L1432+24
	mov	r0, r4
	ldrh	r2, [r5, r3]
	sub	r2, r2, #1
	strh	r2, [r5, r3]	@ movhi
	bl	free_data_superblock
	mov	r0, r4
	bl	FtlGcFreeBadSuperBlk
	mov	r2, #1
	b	.L1428
.L1425:
	bl	List_update_data_list
	mov	r2, #0
.L1428:
	mov	r0, r2
	ldmfd	sp!, {r3, r4, r5, pc}
.L1433:
	.align	2
.L1432:
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
	stmfd	sp!, {r3, r4, r5, r6, r7, lr}
	.save {r3, r4, r5, r6, r7, lr}
	movw	r3, #65535
	cmp	r0, r3
	mov	r5, r0
	beq	.L1435
	ldr	r2, .L1440
	mov	r3, r0, asl #1
	ldr	r2, [r2, #-2016]
	ldrh	r4, [r2, r3]
	cmp	r4, #0
	subne	r4, r4, #1
	strneh	r4, [r2, r3]	@ movhi
	bne	.L1435
	ldr	r0, .L1440+4
	mov	r1, r5
	mov	r2, r4
	bl	printk
	mov	r0, r4
	ldmfd	sp!, {r3, r4, r5, r6, r7, pc}
.L1435:
	ldr	r7, .L1440
	movw	r3, #65535
	ldr	r6, .L1440+8
	ldrh	r0, [r7, r6]
	cmp	r0, r3
	streqh	r5, [r7, r6]	@ movhi
	moveq	r0, #0
	ldmeqfd	sp!, {r3, r4, r5, r6, r7, pc}
	cmp	r0, r5
	beq	.L1439
	bl	update_vpc_list
	strh	r5, [r7, r6]	@ movhi
	adds	r0, r0, #0
	movne	r0, #1
	ldmfd	sp!, {r3, r4, r5, r6, r7, pc}
.L1439:
	mov	r0, #0
	ldmfd	sp!, {r3, r4, r5, r6, r7, pc}
.L1441:
	.align	2
.L1440:
	.word	.LANCHOR2
	.word	.LC110
	.word	-1020
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
	ldr	r5, .L1454
	ldr	r7, .L1454+4
	add	r3, r0, r3, asl #1
	ldrh	r3, [r3, #16]
	b	.L1443
.L1444:
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
.L1443:
	cmp	r3, r6
	beq	.L1444
	ldrb	r2, [r4, #8]	@ zero_extendqisi2
	cmp	r2, #1
	bne	.L1446
	ldrb	r2, [r5, #928]	@ zero_extendqisi2
	cmp	r2, #0
	bne	.L1446
	ldrh	r2, [r4, #2]
	add	r2, r7, r2, asl #1
	sub	r2, r2, #892
	ldrh	r2, [r2, #0]
	cmp	r2, r6
	bne	.L1446
	ldrh	r3, [r4, #4]
	ldrh	r0, [r4, #0]
	sub	r3, r3, #1
	strh	r3, [r4, #4]	@ movhi
	bl	decrement_vpc_count
	b	.L1444
.L1446:
	ldrh	r6, [r4, #2]
	movw	r5, #65535
	mov	r7, r5
	orr	r6, r6, r3, asl #10
	ldrh	r3, [r4, #4]
	sub	r3, r3, #1
	strh	r3, [r4, #4]	@ movhi
.L1451:
	ldr	r2, .L1454
	movw	r3, #3848
	ldrh	r2, [r2, r3]
	ldrb	r3, [r4, #6]	@ zero_extendqisi2
.L1448:
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
	beq	.L1448
	strb	r3, [r4, #6]
	ldrb	r3, [r4, #8]	@ zero_extendqisi2
	cmp	r3, #1
	bne	.L1449
	ldr	r3, .L1454
	ldrb	r2, [r3, #928]	@ zero_extendqisi2
	cmp	r2, #0
	ldrh	r2, [r4, #2]
	bne	.L1452
	ldr	r3, .L1454+4
	add	r2, r3, r2, asl #1
	sub	r2, r2, #892
	ldrh	r3, [r2, #0]
	cmp	r3, r7
	bne	.L1449
	ldrh	r3, [r4, #4]
	cmp	r3, #0
	beq	.L1449
	sub	r3, r3, #1
	ldrh	r0, [r4, #0]
	strh	r3, [r4, #4]	@ movhi
	bl	decrement_vpc_count
	b	.L1451
.L1452:
	movw	r1, #3918
	ldrh	r1, [r3, r1]
	cmp	r2, r1
	bcc	.L1449
	ldr	r1, .L1454+4
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
.L1449:
	mov	r0, r6
	ldmfd	sp!, {r3, r4, r5, r6, r7, pc}
.L1455:
	.align	2
.L1454:
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
	ldr	r5, .L1462
	ldr	r7, .L1462+4
	add	r3, r0, r3, asl #1
	ldrh	r3, [r3, #16]
	b	.L1457
.L1458:
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
.L1457:
	cmp	r3, r6
	beq	.L1458
	ldrb	r2, [r4, #8]	@ zero_extendqisi2
	cmp	r2, #1
	bne	.L1460
	ldrb	r3, [r5, #928]	@ zero_extendqisi2
	cmp	r3, #0
	bne	.L1460
	ldrh	r3, [r4, #2]
	add	r3, r7, r3, asl #1
	sub	r3, r3, #892
	ldrh	r3, [r3, #0]
	cmp	r3, r6
	bne	.L1460
	ldrh	r3, [r4, #4]
	ldrh	r0, [r4, #0]
	sub	r3, r3, #1
	strh	r3, [r4, #4]	@ movhi
	bl	decrement_vpc_count
	ldrh	r3, [r4, #4]
	cmp	r3, #0
	bne	.L1458
	ldrh	r2, [r4, #2]
	strb	r3, [r4, #6]
	add	r2, r2, #1
	strh	r2, [r4, #2]	@ movhi
	ldmfd	sp!, {r3, r4, r5, r6, r7, pc}
.L1460:
	ldr	r3, .L1462
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
	ldr	r1, .L1462+4
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
.L1463:
	.align	2
.L1462:
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
	ldr	r4, .L1502
	mov	r5, r0
	mov	r8, r4
.L1498:
	ldr	r3, .L1502+4
	cmp	r5, r3
	bne	.L1468
	ldr	r3, .L1502+8
	ldrh	r2, [r4, r3]
	ldr	r3, [r4, #-1184]
	mov	r1, r2, lsr #1
	mul	ip, r3, r2
	add	r0, r1, #1
	add	r0, r0, ip, lsr #2
	ldr	ip, .L1502+12
	uxth	r0, r0
	ldr	ip, [ip, #3840]
	cmp	ip, #0
	beq	.L1492
	ldr	ip, [r4, #-1780]
	cmp	ip, #29
	bhi	.L1492
	cmp	ip, #2
	bls	.L1497
	tst	r2, #1
	beq	.L1469
	cmp	r3, #0
	moveq	r1, r3
	beq	.L1470
	b	.L1469
.L1468:
	ldrb	r3, [r5, #8]	@ zero_extendqisi2
	cmp	r3, #1
	bne	.L1497
	ldr	r3, .L1502+12
	movw	r2, #3868
	ldrh	r2, [r3, r2]
	cmp	r2, #1
	beq	.L1497
	ldrb	r2, [r3, #928]	@ zero_extendqisi2
	cmp	r2, #0
	bne	.L1497
	sub	r2, r2, #2000
	ldr	r3, [r3, #3840]
	ldrh	r2, [r4, r2]
	cmp	r3, #0
	mov	r1, r2, lsr #3
	beq	.L1469
	ldr	r3, [r4, #-1780]
	cmp	r3, #1
	movls	r1, #7
	mulls	r1, r1, r2
	movls	r1, r1, lsr #3
	b	.L1469
.L1492:
	mov	r1, r0
.L1469:
	cmp	r1, #0
	subne	r1, r1, #1
	uxthne	r1, r1
	b	.L1470
.L1497:
	mov	r1, #0
.L1470:
	ldrb	r2, [r5, #8]	@ zero_extendqisi2
	ldr	r0, .L1502+16
	bl	List_pop_index_node
	ldr	r3, .L1502+8
	ldrh	r2, [r4, r3]
	sub	r2, r2, #1
	strh	r2, [r4, r3]	@ movhi
	uxth	r7, r0
	mov	r0, r5
	strh	r7, [r5, #0]	@ movhi
	bl	make_superblock
	ldrb	r3, [r5, #7]	@ zero_extendqisi2
	cmp	r3, #0
	beq	.L1471
	ldr	r2, .L1502+12
	movw	r3, #3848
	ldr	ip, [r4, #-2084]
	mov	r6, #0
	mov	sl, r5
	mov	r1, r5
	ldrh	r9, [r2, r3]
	mov	r0, r6
	mov	r3, ip
	mov	r2, r6
	stmia	sp, {r5, ip}
	b	.L1472
.L1471:
	ldr	r3, [r4, #-2016]
	mov	r7, r7, asl #1
	mvn	r2, #0
	strh	r2, [r3, r7]	@ movhi
	b	.L1498
.L1475:
	str	r0, [r3, #8]
	movw	r5, #65535
	str	r0, [r3, #12]
	ldrh	lr, [r1, #16]
	cmp	lr, r5
	beq	.L1474
	ldr	r5, [sp, #4]
	mov	ip, #36
	mov	lr, lr, asl #10
	mla	fp, ip, r6, r5
	add	r6, r6, #1
	uxth	r6, r6
	str	lr, [fp, #4]
.L1474:
	add	r2, r2, #1
	add	r3, r3, #36
	add	r1, r1, #2
	uxth	r2, r2
.L1472:
	cmp	r2, r9
	bne	.L1475
	ldr	r3, .L1502+12
	ldr	r5, [sp, #0]
	ldr	r3, [r3, #3840]
	cmp	r3, #0
	beq	.L1476
	ldr	r3, .L1502+20
	cmp	r5, r3
	bne	.L1476
	ldr	r2, [r4, #-2080]
	mov	r3, r7, asl #1
	ldrh	r3, [r2, r3]
	cmp	r3, #30
	movhi	r3, #0
	strhib	r3, [r4, #-1988]
.L1476:
	ldrb	r3, [r5, #8]	@ zero_extendqisi2
	ldr	r2, [r4, #-2080]
	cmp	r3, #0
	mov	r3, r7, asl #1
	ldrh	r1, [r2, r3]
	bne	.L1477
	cmp	r1, #0
	ldrne	ip, .L1502+12
	movwne	r0, #3906
	moveq	r1, #2
	ldrneh	r0, [ip, r0]
	addne	r1, r1, r0
	strh	r1, [r2, r3]	@ movhi
	ldr	r3, [r4, #-1796]
	mov	r0, r7
	mov	r1, #0
	add	r3, r3, #1
	str	r3, [r4, #-1796]
	b	.L1500
.L1477:
	add	r1, r1, #1
	strh	r1, [r2, r3]	@ movhi
	ldr	r3, [r4, #-1792]
	mov	r0, r7
	mov	r1, #1
	add	r3, r3, #1
	str	r3, [r4, #-1792]
.L1500:
	bl	ftl_set_blk_mode
	ldr	r3, [r4, #-2080]
	mov	r9, r7, asl #1
	ldr	r2, [r4, #-1784]
	ldr	r0, [r8, #-1796]
	ldrh	r3, [r3, r9]
	cmp	r3, r2
	movw	r2, #3906
	strhi	r3, [r4, #-1784]
	ldr	r3, .L1502+12
	ldrh	r1, [r3, r2]
	ldr	r2, [r8, #-1792]
	mla	r0, r0, r1, r2
	mov	r2, #3856
	ldrh	r1, [r3, r2]
	bl	__aeabi_uidiv
	ldr	r3, [r8, #-920]
	ldr	r2, [r3, #16]
	add	r2, r2, #1
	str	r2, [r3, #16]
	ldr	r3, [r8, #-2084]
	mov	r2, #0
	str	r0, [r8, #-1788]
	b	.L1482
.L1483:
	add	r2, r2, #1
	ldr	r1, [r3, #-32]
	uxth	r2, r2
	bic	r1, r1, #1020
	bic	r1, r1, #3
	str	r1, [r3, #-32]
.L1482:
	cmp	r2, r6
	add	r3, r3, #36
	bne	.L1483
	ldr	r3, .L1502+12
	ldrb	r3, [r3, #928]	@ zero_extendqisi2
	cmp	r3, #0
	beq	.L1484
	ldrb	r3, [r5, #8]	@ zero_extendqisi2
	mov	r2, r6
	ldr	r0, [r4, #-2084]
	cmp	r3, #1
	moveq	r1, #0
	movne	r1, #1
	bl	FlashEraseBlocks
.L1484:
	mov	r2, r6
	ldr	r0, [r4, #-2084]
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
	b	.L1486
.L1488:
	ldr	r1, [r4, #-2084]
	add	r0, r1, fp
	ldr	r5, [r1, fp]
	cmn	r5, #1
	bne	.L1487
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
.L1487:
	add	fp, fp, #36
	add	sl, sl, #2
.L1486:
	cmp	fp, r9
	bne	.L1488
	cmp	r6, #0
	mov	r5, r7
	mov	r9, ip
	mov	r7, r3
	beq	.L1489
	mov	r0, r3
	bl	update_multiplier_value
	bl	FtlBbmTblFlush
.L1489:
	ldrb	r3, [r5, #7]	@ zero_extendqisi2
	cmp	r3, #0
	ldreq	r3, [r8, #-2016]
	mvneq	r2, #0
	streqh	r2, [r3, r9]	@ movhi
	beq	.L1498
.L1490:
	ldr	r1, .L1502+12
	movw	r2, #3916
	ldrh	r2, [r1, r2]
	strh	r7, [r5, #0]	@ movhi
	mul	r2, r2, r3
	mov	r3, #0
	strh	r3, [r5, #2]	@ movhi
	strb	r3, [r5, #6]
	ldr	r3, .L1502
	uxth	r2, r2
	strh	r2, [r5, #4]	@ movhi
	ldr	r1, [r3, #-1804]
	str	r1, [r5, #12]
	add	r1, r1, #1
	str	r1, [r3, #-1804]
	ldrh	r0, [r5, #0]
	ldr	r1, [r3, #-2016]
	mov	r3, r0, asl #1
	strh	r2, [r1, r3]	@ movhi
	add	sp, sp, #20
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, sl, fp, pc}
.L1503:
	.align	2
.L1502:
	.word	.LANCHOR2
	.word	.LANCHOR2-1900
	.word	-2000
	.word	.LANCHOR0
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
	ldr	r3, .L1506
	ldr	r3, [r3, #-2088]
	cmp	r3, #0
	bne	.L1505
	bl	allocate_data_superblock.part.20
.L1505:
	mov	r0, #0
	ldmfd	sp!, {r3, pc}
.L1507:
	.align	2
.L1506:
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
	ldr	r3, .L1517
	stmfd	sp!, {r4, r5, r6, r7, r8, lr}
	.save {r4, r5, r6, r7, r8, lr}
	mov	r4, r0
	ldrb	r7, [r3, #928]	@ zero_extendqisi2
	.pad #40
	sub	sp, sp, #40
	cmp	r7, #0
	beq	.L1516
	ldrb	r7, [r0, #8]	@ zero_extendqisi2
	cmp	r7, #1
	ldreqh	r6, [r0, #4]
	beq	.L1509
	mov	r7, #0
.L1516:
	mov	r6, #12
.L1509:
	mvn	r8, #0
	ldr	r5, .L1517+4
	b	.L1510
.L1513:
	ldrh	r3, [r4, #4]
	cmp	r3, #0
	beq	.L1511
	mov	r0, r4
	bl	get_new_active_ppa
	cmn	r0, #1
	str	r0, [sp, #8]
	beq	.L1511
	ldr	r2, [r5, #-936]
	add	r0, sp, #4
	ldr	r3, [r5, #-960]
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
	ldr	r1, [r5, #-1800]
	cmn	r1, #2
	str	r1, [r2, #4]
	addne	r1, r1, #1
	moveq	r1, #0
	mov	r2, r7
	str	r1, [r5, #-1800]
	mov	r1, #1
	bl	FlashProgPages
	ldrh	r0, [r4, #0]
	bl	decrement_vpc_count
.L1510:
	cmp	r6, #0
	bne	.L1513
.L1511:
	ldr	r2, .L1517+4
	ldrh	r3, [r4, #0]
	ldrh	r1, [r4, #4]
	ldr	r2, [r2, #-2016]
	mov	r3, r3, asl #1
	ldrh	r0, [r2, r3]
	rsb	r1, r1, r0
	strh	r1, [r2, r3]	@ movhi
	ldr	r2, .L1517
	movw	r3, #3916
	ldrh	r3, [r2, r3]
	strh	r3, [r4, #2]	@ movhi
	mov	r3, #0
	strb	r3, [r4, #6]
	strh	r3, [r4, #4]	@ movhi
	add	sp, sp, #40
	ldmfd	sp!, {r4, r5, r6, r7, r8, pc}
.L1518:
	.align	2
.L1517:
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
	ldr	r7, .L1548
	mov	r8, r1
	ldr	r9, .L1548+4
	str	r0, [sp, #12]
	ldr	r6, [r7, #-2088]
	ldrb	r0, [r9, #3837]	@ zero_extendqisi2
	cmp	r6, #0
	str	r0, [sp, #16]
	movne	r4, #0
	bne	.L1520
	ldrb	r2, [r9, #928]	@ zero_extendqisi2
	mov	r5, r6
	ldr	r3, [sp, #12]
	mov	r4, r6
	mov	fp, #36
	str	r2, [sp, #8]
	str	r3, [r7, #-984]
	b	.L1521
.L1525:
	mul	r3, fp, r6
	ldr	r2, [r7, #-2084]
	mov	r0, #0
	ldr	r1, [sp, #12]
	str	r0, [r2, r3]
	add	r3, r9, r6
	ldrb	r0, [r3, #3874]	@ zero_extendqisi2
	bl	V2P_block
	cmp	r8, #0
	mov	sl, r0
	beq	.L1522
	bl	IsBlkInVendorPart
	cmp	r0, #0
	bne	.L1523
.L1522:
	mov	r0, sl
	bl	FtlBbmIsBadBlock
	cmp	r0, #0
	addne	r4, r4, #1
	uxthne	r4, r4
	bne	.L1523
	movw	r1, #3928
	ldr	r3, [r7, #-2084]
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
	ldr	r1, [r7, #-932]
	bic	r2, r2, #3
	str	r0, [r3, #8]
	add	r2, r1, r2
	str	r2, [r3, #12]
.L1523:
	add	r6, r6, #1
	uxth	r6, r6
.L1521:
	movw	r2, #3848
	ldr	sl, .L1548+4
	ldrh	r3, [r9, r2]
	cmp	r3, r6
	bhi	.L1525
	cmp	r5, #0
	beq	.L1520
	ldr	r3, [sp, #8]
	mov	r7, #0
	ldr	r9, .L1548
	mov	r2, r5
	adds	r6, r3, #0
	strb	r7, [sl, #3837]
	movne	r6, #1
	ldr	r0, [r9, #-2084]
	mov	r1, r6
	bl	FlashEraseBlocks
	ldr	r0, [sp, #16]
	strb	r0, [sl, #3837]
	mov	sl, r7
.L1528:
	ldr	r3, [r9, #-2084]
	add	r2, r3, r7
	ldr	r3, [r3, r7]
	cmn	r3, #1
	bne	.L1527
	ldr	r0, [r2, #4]
	add	r4, r4, #1
	ubfx	r0, r0, #10, #16
	uxth	r4, r4
	bl	FtlBbmMapBadBlock
.L1527:
	add	sl, sl, #1
	add	r7, r7, #36
	uxth	sl, sl
	cmp	sl, r5
	bne	.L1528
	cmp	r8, #0
	moveq	r2, #6
	moveq	sl, #1
	streq	r2, [sp, #8]
	beq	.L1529
	ldr	r3, .L1548+4
	movw	r2, #3918
	ldrh	sl, [r3, r2]
	ldrb	r3, [r3, #928]	@ zero_extendqisi2
	cmp	r3, #0
	moveq	r1, sl, lsr #2
	streq	r1, [sp, #8]
	moveq	r6, #1
	movne	r6, #1
	strne	r6, [sp, #8]
.L1529:
	mov	r7, #0
.L1537:
	mov	r9, #0
	ldr	fp, .L1548
	mov	r5, r9
	b	.L1530
.L1533:
	mov	r0, #36
	ldr	r2, [fp, #-2084]
	mul	r3, r0, r9
	mov	r1, #0
	str	r1, [r2, r3]
	ldr	r2, .L1548+4
	ldr	r1, [sp, #12]
	add	r3, r2, r9
	ldrb	r0, [r3, #3874]	@ zero_extendqisi2
	bl	V2P_block
	cmp	r8, #0
	str	r0, [sp, #20]
	beq	.L1531
	bl	IsBlkInVendorPart
	cmp	r0, #0
	bne	.L1532
.L1531:
	ldr	r0, [sp, #20]
	bl	FtlBbmIsBadBlock
	cmp	r0, #0
	bne	.L1532
	ldr	r3, [fp, #-2084]
	mov	r0, #36
	ldr	r1, [sp, #20]
	mla	r3, r0, r5, r3
	add	r2, r7, r1, asl #10
	ldr	r1, .L1548+4
	movw	r0, #3928
	str	r2, [r3, #4]
	ldr	r2, [fp, #-944]
	str	r2, [r3, #8]
	ldrh	r2, [r1, r0]
	mul	r2, r2, r5
	add	r5, r5, #1
	uxth	r5, r5
	add	r1, r2, #3
	cmp	r2, #0
	movlt	r2, r1
	ldr	r1, [fp, #-940]
	bic	r2, r2, #3
	add	r2, r1, r2
	str	r2, [r3, #12]
.L1532:
	add	r9, r9, #1
	uxth	r9, r9
.L1530:
	ldr	ip, .L1548+4
	movw	r2, #3848
	ldrh	r3, [ip, r2]
	cmp	r3, r9
	bhi	.L1533
	cmp	r5, #0
	beq	.L1520
	ldr	r9, .L1548
	mov	r3, #0
	mov	r1, r5
	strb	r3, [ip, #3837]
	mov	r2, r6
	mov	r3, #1
	ldr	r0, [r9, #-2084]
	mov	fp, #0
	str	ip, [sp, #4]
	bl	FlashProgPages
	ldr	ip, [sp, #4]
	mov	r3, fp
	ldr	r0, [sp, #16]
	strb	r0, [ip, #3837]
	mov	ip, r8
	mov	r8, r6
	mov	r6, r5
	mov	r5, fp
.L1536:
	ldr	r2, [r9, #-2084]
	add	r1, r2, fp
	ldr	r2, [r2, fp]
	cmp	r2, #0
	beq	.L1535
	ldr	r0, [r1, #4]
	add	r4, r4, #1
	str	ip, [sp, #4]
	ubfx	r0, r0, #10, #16
	uxth	r4, r4
	bl	FtlBbmMapBadBlock
	ldr	ip, [sp, #4]
.L1535:
	add	r5, r5, #1
	add	fp, fp, #36
	uxth	r5, r5
	cmp	r5, r6
	bne	.L1536
	ldr	r1, [sp, #8]
	mov	r5, r6
	mov	r6, r8
	mov	r8, ip
	add	r7, r7, r1
	uxth	r7, r7
	cmp	r7, sl
	bcc	.L1537
	ldr	r9, .L1548
	mov	r7, #0
	mov	sl, r7
.L1539:
	cmp	r8, #0
	beq	.L1538
	ldr	r3, [r9, #-2084]
	add	r2, r3, r7
	ldr	r3, [r3, r7]
	cmp	r3, #0
	bne	.L1538
	ldr	r0, [r2, #4]
	mov	r1, #1
	ubfx	r0, r0, #10, #16
	bl	FtlFreeSysBlkQueueIn
.L1538:
	add	sl, sl, #1
	add	r7, r7, #36
	uxth	sl, sl
	cmp	sl, r5
	bne	.L1539
	ldr	r2, [sp, #12]
	cmp	r2, #63
	movhi	r3, #0
	movls	r3, #1
	cmp	r8, #0
	moveq	r8, r3
	orrne	r8, r3, #1
	cmp	r8, #0
	beq	.L1520
	ldr	r3, .L1548
	mov	r1, r6
	mov	r2, sl
	ldr	r0, [r3, #-2084]
	bl	FlashEraseBlocks
.L1520:
	mov	r0, r4
	add	sp, sp, #28
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, sl, fp, pc}
.L1549:
	.align	2
.L1548:
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
.L1551:
	stmfd	sp!, {r4, r5, lr}
	.save {r4, r5, lr}
	.pad #108
	sub	sp, sp, #108
	ldr	r4, .L1553
	mov	r5, r0
	mov	r1, #165
	add	r0, sp, #40
	mov	r2, #32
	str	r0, [sp, #16]
	ldr	r3, [r4, #136]
	mov	r5, r5, asl #10
	str	r3, [sp, #12]
	bl	ftl_memset
	ldr	r0, [r4, #136]
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
.L1554:
	.align	2
.L1553:
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
	ldr	r3, .L1557
	ldr	r3, [r3, #156]
	cmp	r0, r3
	bcc	.L1556
	b	FlashTestBlk.part.23
.L1556:
	mov	r0, #0
	bx	lr
.L1558:
	.align	2
.L1557:
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
	ldr	r3, .L1587
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
	ldr	r4, .L1587+4
	mul	fp, fp, r2
	ldr	r2, [r3, #3624]
	ldr	r0, [r4, #228]
	ldrb	r2, [r2, #24]	@ zero_extendqisi2
	str	r0, [sp, #28]
	ldr	r0, .L1587+8
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
	ldr	r0, [r4, #228]
	mov	r2, #4096
	mov	r1, #0
	bl	ftl_memset
	sub	r3, fp, #1
	mov	r2, fp, lsr #4
	ldr	r4, .L1587
	uxth	r3, r3
	str	r2, [sp, #20]
	str	r3, [sp, #24]
	b	.L1561
.L1583:
	ldr	r3, .L1587+4
	add	r2, r3, r5, asl #1
	ldrh	r6, [r2, #232]
	cmp	r6, #0
	bne	.L1562
	movw	r2, #3068
	ldr	r0, [r3, #148]
	ldrh	r2, [r4, r2]
	mov	r1, r6
	mov	r8, r6
	mov	r7, r6
	add	sl, r4, r5, asl #2
	mov	r2, r2, asl #9
	bl	ftl_memset
	add	r3, r4, r5
	ldrb	r9, [r3, #3764]	@ zero_extendqisi2
	b	.L1563
.L1571:
	mvn	r3, #0
	strb	r3, [sp, #34]
	strb	r3, [sp, #35]
	ldr	r3, [sp, #12]
	tst	r3, #1
	beq	.L1564
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
	bne	.L1564
	ldr	r1, [r4, #856]
	add	r2, sp, #35
	mov	r0, r9
	add	r1, r3, r1
	bl	FlashReadSpare
	ldrb	r3, [sp, #34]	@ zero_extendqisi2
	ldrb	r2, [sp, #35]	@ zero_extendqisi2
	and	r3, r2, r3
	strb	r3, [sp, #34]
.L1564:
	ldr	r0, [sp, #12]
	tst	r0, #2
	beq	.L1565
	ldr	r3, [r4, #3624]
	mov	r0, r9
	add	r2, sp, #35
	ldrh	r1, [r3, #10]
	ldr	r3, [sl, #3588]
	sub	r1, r1, #1
	add	r1, r1, r3
	add	r1, r1, r6
	bl	FlashReadSpare
.L1565:
	ldr	r3, [r4, #3624]
	ldrb	r3, [r3, #7]	@ zero_extendqisi2
	cmp	r3, #1
	cmpne	r3, #8
	ldrb	r3, [sp, #34]	@ zero_extendqisi2
	bne	.L1566
	cmp	r3, #0
	beq	.L1585
	ldrb	r0, [sp, #35]	@ zero_extendqisi2
	rsbs	r0, r0, #1
	movcc	r0, #0
	b	.L1567
.L1566:
	cmp	r3, #255
	bne	.L1585
	ldrb	r0, [sp, #35]	@ zero_extendqisi2
	subs	r0, r0, #255
	movne	r0, #1
	b	.L1567
.L1585:
	mov	r0, #1
.L1567:
	ldr	r2, [sp, #12]
	tst	r2, #4
	beq	.L1568
	ldr	r1, [sl, #3588]
	mov	r0, r9
	add	r1, r6, r1
	bl	SandiskProgTestBadBlock
.L1568:
	cmp	r0, #0
	beq	.L1569
	mov	r1, r5
	mov	r2, r8
	ldr	r0, .L1587+12
	add	r7, r7, #1
	bl	printk
	ldr	r3, [sp, #16]
	and	r0, r8, #31
	mov	ip, #1
	uxth	r7, r7
	mov	r2, r3, lsr #5
	ldr	r3, .L1587+4
	ldr	r3, [r3, #148]
	ldr	r1, [r3, r2, asl #2]
	orr	r1, r1, ip, asl r0
	ldr	r0, [sp, #20]
	str	r1, [r3, r2, asl #2]
	ldrb	r3, [r4, #3762]	@ zero_extendqisi2
	mul	r3, r3, r0
	cmp	r7, r3
	bgt	.L1570
.L1569:
	ldr	r2, [sp, #8]
	add	r8, r8, #1
	add	r6, r6, r2
.L1563:
	uxth	r3, r8
	str	r3, [sp, #16]
	cmp	r3, fp
	bcc	.L1571
.L1570:
	ldr	r0, .L1587+16
	mov	r1, r5
	mov	r2, r7
	bl	printk
	ldrb	r3, [r4, #3762]	@ zero_extendqisi2
	ldr	r0, [sp, #20]
	mul	r3, r3, r0
	cmp	r7, r3
	blt	.L1572
	movw	r3, #3068
	mov	r1, #0
	ldrh	r2, [r4, r3]
	ldr	r3, .L1587+4
	mov	r2, r2, asl #9
	ldr	r0, [r3, #148]
	bl	ftl_memset
.L1572:
	cmp	r5, #0
	bne	.L1573
	ldr	sl, .L1587+4
	mov	r8, r5
	mov	r7, r5
	ldrh	r9, [sl, #156]
	b	.L1574
.L1576:
	mov	r0, r6
	bl	FlashTestBlk
	cmp	r0, #0
	beq	.L1575
	mov	r1, r6
	ldr	r0, .L1587+20
	bl	printk
	ldr	r3, [sl, #148]
	mov	r2, r6, lsr #5
	mov	r0, #1
	add	r7, r7, r0
	and	r6, r6, #31
	ldr	r1, [r3, r2, asl #2]
	uxth	r7, r7
	orr	r6, r1, r0, asl r6
	str	r6, [r3, r2, asl #2]
.L1575:
	add	r8, r8, #1
	uxth	r8, r8
.L1574:
	add	r6, r8, r9
	ldrb	r3, [r4, #853]	@ zero_extendqisi2
	uxth	r6, r6
	cmp	r3, r6
	bhi	.L1576
	ldr	r6, [sp, #24]
	sub	r9, fp, #50
	ldr	sl, .L1587+4
	mov	r8, #1
	b	.L1577
.L1579:
	mov	r0, r6
	bl	FlashTestBlk
	cmp	r0, #0
	beq	.L1578
	mov	r1, r6
	ldr	r0, .L1587+20
	bl	printk
	ldr	r3, [sl, #148]
	mov	r2, r6, lsr #5
	and	r0, r6, #31
	ldr	r1, [r3, r2, asl #2]
	orr	r1, r1, r8, asl r0
	str	r1, [r3, r2, asl #2]
.L1578:
	sub	r6, r6, #1
	uxth	r6, r6
.L1577:
	cmp	r6, r9
	bgt	.L1579
	ldr	r3, .L1587+4
	ldrb	r1, [r4, #853]	@ zero_extendqisi2
	ldr	r2, [r3, #156]
	rsb	r2, r2, r1
	cmp	r7, r2
	bcc	.L1573
	movw	r2, #3068
	ldr	r0, [r3, #148]
	ldrh	r2, [r4, r2]
	mov	r1, #0
	mov	r2, r2, asl #9
	bl	ftl_memset
.L1573:
	mul	r8, fp, r5
	ldr	r7, .L1587+4
	ldr	r6, [sp, #24]
	ldr	r9, [sp, #28]
	add	sl, r7, r5, asl #1
.L1580:
	ldr	r0, .L1587+24
	mov	r1, r5
	mov	r2, r6
	bl	printk
	ldr	r3, [r7, #148]
	b	.L1581
.L1582:
	sub	r6, r6, #1
	uxth	r6, r6
.L1581:
	mov	r1, r6, lsr #5
	and	r2, r6, #31
	ldr	r1, [r3, r1, asl #2]
	mov	r2, r1, lsr r2
	ands	r2, r2, #1
	bne	.L1582
	ldr	r3, .L1587+28
	mov	r1, #1
	strh	r6, [sl, #232]	@ movhi
	add	r0, sp, #36
	strh	r6, [r9, #2]	@ movhi
	strh	r3, [r9, #0]	@ movhi
	ldr	r3, [r7, #148]
	strh	r2, [r9, #8]	@ movhi
	mov	r2, r1
	str	r3, [sp, #44]
	ldr	r3, [r7, #228]
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
	bne	.L1580
.L1562:
	add	r5, r5, #1
	uxtb	r5, r5
.L1561:
	ldrb	r3, [r4, #3762]	@ zero_extendqisi2
	cmp	r3, r5
	bhi	.L1583
	add	sp, sp, #76
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, sl, fp, pc}
.L1588:
	.align	2
.L1587:
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
	bge	.L1590
	ldrh	r2, [r4, #40]
	movw	r3, #65535
	cmp	r2, r3
	beq	.L1591
	ldr	r1, .L1603
	movw	r3, #3918
	ldrh	r2, [r4, #2]
	ldrh	r3, [r1, r3]
	cmp	r2, r3
	bcc	.L1591
.L1590:
	ldrh	r1, [r4, #40]
	movw	r3, #65535
	uxth	r7, r0
	cmp	r1, r3
	beq	.L1592
	ldr	r0, .L1603
	movw	r2, #3918
	ldrh	r3, [r4, #2]
	ldrh	r2, [r0, r2]
	cmp	r2, r3
	movls	r0, r6
	movls	r2, #0
	bhi	.L1592
	b	.L1593
.L1595:
	ldrh	lr, [r0], #2
	cmp	lr, r1
	beq	.L1602
	add	r2, r2, #1
	uxth	r2, r2
.L1593:
	cmp	r2, ip
	bne	.L1595
	b	.L1594
.L1602:
	mov	r7, r2
.L1594:
	mov	r2, r2, asl #1
	ldr	r0, .L1603+4
	ldrh	r2, [r6, r2]
	bl	printk
	mvn	r3, #0
	strh	r3, [r4, #40]	@ movhi
.L1592:
	mov	r7, r7, asl #1
	ldrh	r8, [r6, r7]
	cmp	r8, #0
	beq	.L1591
	ldr	r3, [r4, #32]
	cmp	r3, #0
	bne	.L1591
	mov	r2, #1
	ldr	r1, .L1603
	str	r2, [r4, #32]
	strh	r3, [r6, r7]	@ movhi
	ldrh	r3, [r4, #8]
	ldrh	r2, [r4, #2]
	sub	r3, r3, #1
	strh	r3, [r4, #8]	@ movhi
	movw	r3, #3918
	ldrh	r3, [r1, r3]
	cmp	r2, r3
	bcc	.L1596
	mov	r0, r4
	bl	ftl_map_blk_alloc_new_blk
.L1596:
	mov	r7, #0
	ldr	r6, .L1603+8
	mov	fp, r7
	b	.L1597
.L1600:
	ldr	r3, [r5, r7, asl #2]
	mov	r9, r7, asl #2
	cmp	r8, r3, lsr #10
	bne	.L1598
	ldr	r3, [r6, #-956]
	mov	r1, #1
	ldr	sl, [r6, #-936]
	mov	r2, r1
	ldr	r0, .L1603+12
	str	r3, [r6, #188]
	str	sl, [r6, #192]
	ldr	r3, [r5, r7, asl #2]
	str	r3, [r6, #184]
	bl	FlashReadPages
	ldr	r3, [r6, #180]
	cmn	r3, #1
	bne	.L1599
	str	fp, [r5, r9]
	ldr	r0, .L1603+16
	ldr	r1, [r6, #184]
	ldrh	r2, [sl, #8]
	bl	printk
	mov	r3, #1
	str	r3, [r6, #-2088]
	b	.L1598
.L1599:
	mov	r0, r4
	mov	r1, r7
	ldr	r2, [r6, #188]
	bl	FtlMapWritePage
.L1598:
	add	r7, r7, #1
	uxth	r7, r7
.L1597:
	ldrh	r3, [r4, #6]
	cmp	r3, r7
	bhi	.L1600
	mov	r0, r8
	mov	r1, #1
	bl	FtlFreeSysBlkQueueIn
	mov	r3, #0
	str	r3, [r4, #32]
.L1591:
	ldr	r1, .L1603
	movw	r3, #3918
	ldrh	r2, [r4, #2]
	ldrh	r3, [r1, r3]
	cmp	r2, r3
	bcc	.L1601
	mov	r0, r4
	bl	ftl_map_blk_alloc_new_blk
.L1601:
	mov	r0, #0
	ldmfd	sp!, {r3, r4, r5, r6, r7, r8, r9, sl, fp, pc}
.L1604:
	.align	2
.L1603:
	.word	.LANCHOR0
	.word	.LC116
	.word	.LANCHOR2
	.word	.LANCHOR2+180
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
	ldr	r5, .L1611
	ldr	r7, [r0, #12]
	ldr	r8, [r0, #24]
	ldr	r6, [r5, #-2088]
	cmp	r6, #0
	bne	.L1606
	ldrh	r3, [r0, #0]
	movw	r2, #65535
	cmp	r3, r2
	bne	.L1607
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
	b	.L1606
.L1607:
	mov	r3, r3, asl #1
	ldr	r2, [r0, #28]
	mov	r1, #255
	ldrh	r7, [r7, r3]
	ldrh	r3, [r0, #2]
	orr	r3, r3, r7, asl #10
	str	r3, [r5, #184]
	ldr	r3, [r5, #-960]
	str	r3, [r5, #188]
	ldr	r3, [r5, #-936]
	str	r3, [r5, #192]
	str	r2, [r3, #4]
	ldr	r2, .L1611+4
	strh	r2, [r3, #8]	@ movhi
	ldrh	r2, [r0, #4]
	strh	r7, [r3, #2]	@ movhi
	strh	r2, [r3, #0]	@ movhi
	movw	r3, #3918
	ldr	r2, .L1611+8
	ldr	r0, [r5, #-960]
	ldrh	r2, [r2, r3]
	mov	r2, r2, asl #3
	bl	ftl_memset
	mov	r3, r6
	b	.L1608
.L1610:
	ldr	r2, [r8, r3, asl #2]
	cmp	r7, r2, lsr #10
	bne	.L1609
	add	r6, r6, #1
	ldr	r2, [r5, #-960]
	uxth	r6, r6
	str	r3, [r2, r6, asl #3]
	ldr	r2, [r5, #-960]
	ldr	r1, [r8, r3, asl #2]
	add	r2, r2, r6, asl #3
	str	r1, [r2, #4]
.L1609:
	add	r3, r3, #1
	uxth	r3, r3
.L1608:
	ldrh	r2, [r4, #6]
	cmp	r2, r3
	bhi	.L1610
	mov	r1, #1
	mov	r3, #0
	ldr	r0, .L1611+12
	mov	r2, r1
	bl	FlashProgPages
	ldrh	r3, [r4, #2]
	mov	r0, r4
	add	r3, r3, #1
	strh	r3, [r4, #2]	@ movhi
	bl	ftl_map_blk_gc
.L1606:
	mov	r0, #0
	ldmfd	sp!, {r4, r5, r6, r7, r8, pc}
.L1612:
	.align	2
.L1611:
	.word	.LANCHOR2
	.word	-1291
	.word	.LANCHOR0
	.word	.LANCHOR2+180
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
	ldr	r5, .L1626
	mov	sl, r1
	ldr	r8, .L1626+4
	mov	r9, r2
	mov	r6, #0
.L1624:
	ldr	r3, [r5, #-1820]
	add	r3, r3, #1
	str	r3, [r5, #-1820]
	movw	r3, #3918
	ldrh	r2, [r4, #2]
	ldrh	r3, [r8, r3]
	sub	r3, r3, #1
	cmp	r2, r3
	bge	.L1615
	ldrh	r2, [r4, #0]
	movw	r3, #65535
	cmp	r2, r3
	bne	.L1616
.L1615:
	mov	r0, r4
	bl	Ftl_write_map_blk_to_last_page
.L1616:
	ldrh	r3, [r4, #0]
	mov	r1, #0
	ldr	r2, [r4, #12]
	ldr	r0, [r5, #-936]
	mov	r3, r3, asl #1
	ldrh	r7, [r2, r3]
	mov	r2, #16
	ldrh	r3, [r4, #2]
	str	r9, [r5, #188]
	orr	r3, r3, r7, asl #10
	str	r0, [r5, #192]
	str	r3, [r5, #184]
	bl	ftl_memset
	ldr	r3, [r5, #192]
	mov	r1, #1
	ldr	r2, [r4, #28]
	ldr	r0, .L1626+8
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
	ldr	r3, [r5, #180]
	cmn	r3, #1
	bne	.L1617
	ldr	r0, .L1626+12
	add	r6, r6, #1
	ldr	r1, [r5, #184]
	bl	printk
	ldrh	r3, [r4, #2]
	uxth	r6, r6
	cmp	r3, #2
	movwls	r3, #3918
	ldrlsh	r3, [r8, r3]
	subls	r3, r3, #1
	strlsh	r3, [r4, #2]	@ movhi
	cmp	r6, #3
	bls	.L1624
	ldr	r4, .L1626
	mov	r2, r6
	ldr	r0, .L1626+16
	ldr	r1, [r4, #184]
	bl	printk
	mov	r3, #1
	str	r3, [r4, #-2088]
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, sl, pc}
.L1617:
	cmp	r3, #0
	strneh	r7, [r4, #40]	@ movhi
	cmp	r2, #1
	beq	.L1624
	cmp	r3, #256
	beq	.L1624
	ldr	r3, .L1626
	ldr	r2, [r3, #184]
	ldr	r3, [r4, #24]
	str	r2, [r3, sl, asl #2]
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, sl, pc}
.L1627:
	.align	2
.L1626:
	.word	.LANCHOR2
	.word	.LANCHOR0
	.word	.LANCHOR2+180
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
	ldr	r3, .L1630
	ldr	r3, [r3, #-2088]
	cmp	r3, #0
	bne	.L1629
	bl	FtlMapWritePage.part.14
.L1629:
	mov	r0, #0
	ldmfd	sp!, {r3, pc}
.L1631:
	.align	2
.L1630:
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
	ldr	r5, .L1633
	mul	r4, r4, r0
	sub	r0, r5, #1088
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
.L1634:
	.align	2
.L1633:
	.word	.LANCHOR2
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
	ldr	r3, .L1639
	ldr	r1, [r3, #-2088]
	cmp	r1, #0
	ldmnefd	sp!, {r3, r4, r5, pc}
	sub	r5, r5, #1
	ldr	r0, [r3, #-956]
	ldr	r1, [r3, #-936]
	uxth	r5, r5
	str	r0, [r3, #188]
	str	r1, [r3, #192]
	ldr	r2, [r2, r5, asl #2]
	cmp	r2, #0
	str	r2, [r3, #184]
	beq	.L1637
	mov	r1, #1
	add	r0, r3, #180
	mov	r2, r1
	bl	FlashReadPages
	b	.L1638
.L1637:
	ldr	r2, .L1639+4
	movw	r3, #3926
	mov	r1, #255
	ldrh	r2, [r2, r3]
	bl	ftl_memset
.L1638:
	ldr	r3, .L1639
	mov	r0, r4
	mov	r1, r5
	ldr	r2, [r3, #188]
	ldmfd	sp!, {r3, r4, r5, lr}
	b	FtlMapWritePage
.L1640:
	.align	2
.L1639:
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
	ldr	r3, .L1652
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
	bhi	.L1642
	mov	r7, r0, lsr r7
	mov	fp, #0
	ldr	r6, .L1652+4
	mov	r2, r7, asl #2
	str	r2, [sp, #0]
	b	.L1643
.L1649:
	ldr	r3, [r6, #-904]
	mov	r0, r8
	ldr	r2, [sp, #0]
	ldr	sl, [r3, r2]
	movw	r3, #3922
	ldr	r2, .L1652
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
	beq	.L1645
	ldr	r2, .L1652+4
	mov	r1, #1
	add	r0, sp, #12
	str	sl, [sp, #16]
	ldr	r3, [r2, #-952]
	mov	r2, r1
	str	r3, [sp, #20]
	mov	r3, #0
	str	r3, [sp, #24]
	bl	FlashReadPages
	ldr	r3, [sp, #12]
	cmn	r3, #1
	moveq	fp, r3
	ldr	r3, [r6, #180]
	cmp	r3, #256
	bne	.L1647
	mov	r1, r7
	mov	r2, sl
	ldr	r0, .L1652+8
	bl	printk
	ldr	r0, .L1652+12
	mov	r1, r7
	ldr	r2, [r6, #-952]
	bl	FtlMapWritePage
.L1647:
	ldr	r3, [sp, #4]
	mov	r0, r9
	ldr	r1, [r6, #-952]
	mov	r2, r4, asl #9
	add	r1, r1, r3, asl #9
	bl	memcpy
	b	.L1648
.L1645:
	mov	r0, r9
	mov	r1, sl
	mov	r2, r4, asl #9
	bl	ftl_memset
.L1648:
	ldr	r3, [sp, #0]
	add	r7, r7, #1
	rsb	r5, r4, r5
	add	r8, r8, r4
	add	r9, r9, r4, asl #9
	add	r3, r3, #4
	str	r3, [sp, #0]
.L1643:
	cmp	r5, #0
	bne	.L1649
.L1642:
	mov	r0, fp
	add	sp, sp, #52
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, sl, fp, pc}
.L1653:
	.align	2
.L1652:
	.word	.LANCHOR0
	.word	.LANCHOR2
	.word	.LC120
	.word	.LANCHOR2+248
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
	ldr	r5, .L1658
	mov	r1, #1
	ldr	r2, .L1658+4
	bl	FtlVendorPartRead
	ldr	r4, .L1658+8
	ldr	r3, [r5, #-1708]
	cmp	r3, r4
	beq	.L1655
	ldr	r0, .L1658+4
	mov	r1, #0
	mov	r2, #512
	bl	ftl_memset
	str	r4, [r5, #-1708]
.L1655:
	ldr	r2, [r5, #-1708]
	ldr	r3, .L1658
	cmp	r2, r4
	bne	.L1656
	ldr	r2, [r3, #-1620]
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
	ldr	r2, [r3, #-1676]
	str	r2, [r3, #-2076]
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
.L1656:
	ldr	r4, .L1658
	mov	r3, #0
	ldr	r5, .L1658+12
	str	r3, [r4, #-1000]
	ldr	r2, [r4, #-1640]
	ldr	r3, .L1658+16
	cmp	r2, r3
	bne	.L1657
	mov	r3, #1
	ldr	r0, .L1658+20
	str	r3, [r5, #3840]
	ldr	r1, .L1658+24
	bl	printk
.L1657:
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
.L1659:
	.align	2
.L1658:
	.word	.LANCHOR2
	.word	.LANCHOR2-1708
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
	ldr	r4, .L1662
	ldr	r5, .L1662+4
	ldr	r2, [r4, #-920]
	ldrh	r1, [r4, r5]
	bl	FtlVendorPartRead
	ldr	r3, [r4, #-920]
	ldr	r2, [r3, #0]
	ldr	r3, .L1662+8
	cmp	r2, r3
	beq	.L1661
	ldr	r1, .L1662+12
	ldr	r0, .L1662+16
	bl	printk
	ldrh	r2, [r4, r5]
	ldr	r0, [r4, #-920]
	mov	r1, #0
	mov	r2, r2, asl #9
	bl	ftl_memset
.L1661:
	mov	r0, #0
	ldmfd	sp!, {r3, r4, r5, pc}
.L1663:
	.align	2
.L1662:
	.word	.LANCHOR2
	.word	-924
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
	ldr	r3, .L1674
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
	bhi	.L1665
	mov	r8, r0, lsr r8
	mov	r7, #0
	ldr	sl, .L1674+4
	mov	ip, r8, asl #2
	str	ip, [sp, #8]
	b	.L1666
.L1671:
	ldr	r1, .L1674
	movw	r2, #3922
	ldr	ip, [sp, #8]
	mov	r0, r6
	ldr	r3, [sl, #-904]
	ldrh	fp, [r1, r2]
	ldr	r3, [r3, ip]
	mov	r1, fp
	str	r3, [sp, #4]
	bl	__aeabi_uidivmod
	ldr	r3, [sp, #4]
	ldr	r2, .L1674+4
	uxth	r1, r1
	str	r1, [sp, #12]
	rsb	r5, r1, fp
	uxth	r5, r5
	cmp	r5, r4
	uxthhi	r5, r4
	cmp	r3, #0
	beq	.L1668
	cmp	r5, fp
	beq	.L1668
	str	r3, [sp, #24]
	mov	r1, #1
	ldr	r3, [r2, #-952]
	add	r0, sp, #20
	mov	r2, r1
	str	r3, [sp, #28]
	mov	r3, #0
	str	r3, [sp, #32]
	bl	FlashReadPages
	b	.L1669
.L1668:
	ldr	ip, .L1674
	movw	r3, #3926
	ldr	r0, [r2, #-952]
	mov	r1, #0
	ldrh	r2, [ip, r3]
	bl	ftl_memset
.L1669:
	ldr	r3, [sp, #12]
	mov	fp, r5, asl #9
	ldr	r0, [sl, #-952]
	mov	r1, r9
	mov	r2, fp
	rsb	r4, r5, r4
	add	r0, r0, r3, asl #9
	add	r6, r6, r5
	bl	memcpy
	mov	r1, r8
	ldr	r0, .L1674+8
	add	r8, r8, #1
	ldr	r2, [sl, #-952]
	add	r9, r9, fp
	bl	FtlMapWritePage
	ldr	ip, [sp, #8]
	add	ip, ip, #4
	str	ip, [sp, #8]
	cmn	r0, #1
	moveq	r7, r0
.L1666:
	cmp	r4, #0
	bne	.L1671
.L1665:
	mov	r0, r7
	add	sp, sp, #60
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, sl, fp, pc}
.L1675:
	.align	2
.L1674:
	.word	.LANCHOR0
	.word	.LANCHOR2
	.word	.LANCHOR2+248
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
	ldr	r3, .L1678
	ldr	r2, .L1678+4
	ldr	r1, [r3, #-1708]
	cmp	r1, r2
	bxne	lr
	ldr	r2, .L1678+8
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
	ldr	r2, [r3, #-2076]
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
	ldr	r2, [r3, #-1000]
	str	r2, [r3, #-1644]
	ldr	r2, .L1678+12
	b	FtlVendorPartWrite
.L1679:
	.align	2
.L1678:
	.word	.LANCHOR2
	.word	1179929683
	.word	1342177352
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
	ldr	r3, .L1687
	ldr	r1, .L1687+4
	ldr	r3, [r3, #3840]
	cmp	r3, #0
	moveq	r2, #32
	beq	.L1681
	ldr	r2, [r1, #-1780]
	cmp	r2, #29
	movls	r2, #4
	movhi	r2, #32
.L1681:
	mov	r3, #292
	ldrh	r1, [r1, r3]
	cmp	r1, #31
	addls	r1, r1, #1
	ldrls	r2, .L1687+4
	strlsh	r1, [r2, r3]	@ movhi
	movls	r2, #1
	cmp	r0, #0
	ldr	r3, .L1687+4
	bne	.L1683
	ldr	r1, [r3, #-920]
	ldr	r0, [r1, #20]
	ldr	r1, [r1, #16]
	add	r2, r2, r0
	cmp	r1, r2
	bcc	.L1684
.L1683:
	ldr	r2, [r3, #-920]
	mov	r0, #64
	ldr	r1, [r2, #16]
	str	r1, [r2, #20]
	ldr	r1, .L1687+8
	str	r1, [r2, #0]
	ldr	r1, .L1687+12
	ldr	r2, [r3, #-920]
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
.L1684:
	mov	r0, #0
	ldmfd	sp!, {r3, pc}
.L1688:
	.align	2
.L1687:
	.word	.LANCHOR0
	.word	.LANCHOR2
	.word	1112818501
	.word	-924
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
	ldr	r4, .L1693
	mov	r7, #12
	ldr	r3, [r4, #-1032]
	ldr	r6, [r3, r0, asl #2]
	cmp	r6, #0
	bne	.L1690
	mul	r7, r7, r1
	ldr	r3, [r4, #-1852]
	ldr	r2, .L1693+4
	mov	r1, #255
	add	r3, r3, r7
	ldr	r0, [r3, #8]
	movw	r3, #3926
	ldrh	r2, [r2, r3]
	bl	ftl_memset
	ldr	r3, [r4, #-1852]
	strh	r5, [r3, r7]	@ movhi
	ldr	r3, [r4, #-1852]
	add	r7, r3, r7
	str	r6, [r7, #4]
	b	.L1691
.L1690:
	mul	r7, r7, r1
	ldr	r3, [r4, #-1852]
	mov	r1, #1
	add	r0, r4, #180
	mov	r2, r1
	str	r6, [r4, #184]
	add	r3, r3, r7
	ldr	r3, [r3, #8]
	str	r3, [r4, #188]
	ldr	r3, [r4, #-936]
	str	r3, [r4, #192]
	bl	FlashReadPages
	ldr	r3, [r4, #180]
	cmp	r3, #256
	bne	.L1692
	mov	r2, r6
	mov	r1, r5
	ldr	r0, .L1693+8
	mov	r6, r6, lsr #10
	bl	printk
	ldr	r3, .L1693+12
	sub	r0, r4, #1088
	mov	r1, r5
	strh	r6, [r4, r3]	@ movhi
	ldr	r3, [r4, #-1852]
	add	r3, r3, r7
	ldr	r2, [r3, #8]
	bl	FtlMapWritePage
.L1692:
	ldr	r3, .L1693
	mov	r1, #0
	ldr	r3, [r3, #-1852]
	add	r2, r3, r7
	str	r1, [r2, #4]
	strh	r5, [r3, r7]	@ movhi
.L1691:
	mov	r0, #0
	ldmfd	sp!, {r3, r4, r5, r6, r7, pc}
.L1694:
	.align	2
.L1693:
	.word	.LANCHOR2
	.word	.LANCHOR0
	.word	.LC123
	.word	-1048
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
	ldr	r3, .L1704
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
	ldr	r3, .L1704+4
	uxth	r7, r7
	ldr	sl, [r3, #-1852]
	mov	r3, #0
	mov	r5, r3
	b	.L1696
.L1702:
	add	r3, r3, #12
	add	r1, sl, r3
	ldrh	r1, [r1, #-12]
	cmp	r1, r6
	bne	.L1697
.L1698:
	cmp	r8, #0
	ldr	r3, .L1704+4
	mov	r2, #12
	bne	.L1699
	ldr	r3, [r3, #-1852]
	mla	r2, r2, r5, r3
	ldr	r3, [r2, #8]
	ldr	r3, [r3, r7, asl #2]
	str	r3, [r4, #0]
	b	.L1700
.L1699:
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
	ldr	r2, .L1704+8
	strh	r6, [r3, r2]	@ movhi
.L1700:
	ldr	r3, .L1704+4
	mov	r2, #12
	mov	r0, #0
	ldr	r3, [r3, #-1852]
	mla	r5, r2, r5, r3
	ldr	r3, [r5, #4]
	cmn	r3, #1
	addne	r3, r3, #1
	strne	r3, [r5, #4]
	ldmfd	sp!, {r3, r4, r5, r6, r7, r8, sl, pc}
.L1697:
	add	r5, r5, #1
	uxth	r5, r5
.L1696:
	cmp	r5, r2
	bne	.L1702
	bl	select_l2p_ram_region
	mov	r3, #12
	mul	r3, r3, r0
	mov	r5, r0
	add	r2, sl, r3
	ldrh	r1, [sl, r3]
	movw	r3, #65535
	cmp	r1, r3
	beq	.L1703
	ldr	r3, [r2, #4]
	cmp	r3, #0
	bge	.L1703
	bl	flush_l2p_region
.L1703:
	mov	r0, r6
	mov	r1, r5
	bl	ftl_load_l2p_region
	b	.L1698
.L1705:
	.align	2
.L1704:
	.word	.LANCHOR0
	.word	.LANCHOR2
	.word	-1848
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
	ldr	r3, .L1712
	ldr	r1, [r3, #-2016]
	mov	r5, r0, asl #1
	ldrh	r2, [r1, r5]
	cmp	r2, #0
	addne	r2, r2, #1
	strneh	r2, [r1, r5]	@ movhi
	bne	.L1708
	ldr	r4, [r3, #-2004]
	cmp	r4, #0
	beq	.L1708
	ldr	r1, .L1712+4
	mov	ip, #6
	ldrh	lr, [r3, r1]
	ldr	r3, [r3, #-2024]
	ldr	r1, .L1712+8
	rsb	r4, r3, r4
	mov	r4, r4, asr #1
	mul	r4, r1, r4
	movw	r1, #65535
	uxth	r4, r4
	b	.L1709
.L1711:
	cmp	r4, r0
	bne	.L1710
	mov	r1, r4
	ldr	r0, .L1712+12
	bl	List_remove_node
	ldr	r6, .L1712
	ldr	r3, .L1712+4
	mov	r0, r4
	ldrh	r2, [r6, r3]
	sub	r2, r2, #1
	strh	r2, [r6, r3]	@ movhi
	bl	INSERT_DATA_LIST
	ldr	r3, [r6, #-2016]
	ldrh	r2, [r3, r5]
	add	r2, r2, #1
	strh	r2, [r3, r5]	@ movhi
	b	.L1708
.L1710:
	mul	r4, ip, r4
	ldrh	r4, [r3, r4]
	cmp	r4, r1
	beq	.L1708
	add	r2, r2, #1
	uxth	r2, r2
.L1709:
	cmp	r2, lr
	bne	.L1711
.L1708:
	mov	r0, r7
	add	r1, sp, #4
	mov	r2, #1
	bl	log2phys
	ldmfd	sp!, {r1, r2, r3, r4, r5, r6, r7, pc}
.L1713:
	.align	2
.L1712:
	.word	.LANCHOR2
	.word	-2000
	.word	-1431655765
	.word	.LANCHOR2-2004
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
	ldr	r4, .L1722
	ldr	r6, .L1722+4
	ldr	r2, [r4, #-1624]
	ldr	r3, [r6, #3976]
	cmp	r2, r3
	bcs	.L1715
	mov	r5, #2048
.L1718:
	ldr	r0, [r4, #-1624]
	ldr	r3, [r6, #3976]
	cmp	r0, r3
	bcs	.L1720
	mov	r1, sp
	mov	r2, #0
	bl	log2phys
	ldr	r0, [sp, #0]
	ldr	r3, [r4, #-1624]
	cmn	r0, #1
	add	r3, r3, #1
	str	r3, [r4, #-1624]
	beq	.L1717
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
	bne	.L1720
	ldr	r0, [sp, #0]
	ubfx	r0, r0, #10, #16
	bl	P2V_block_in_plane
	bl	FtlGcRefreshBlock
	b	.L1720
.L1717:
	subs	r5, r5, #1
	bne	.L1718
	b	.L1716
.L1715:
	ldr	r3, [r4, #-1836]
	mov	r0, #0
	str	r0, [r4, #-1628]
	str	r0, [r4, #-1624]
	str	r3, [r4, #-1632]
	b	.L1716
.L1720:
	mvn	r0, #0
.L1716:
	add	sp, sp, #40
	ldmfd	sp!, {r4, r5, r6, pc}
.L1723:
	.align	2
.L1722:
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
	ldr	r3, .L1731
	ldr	r2, [r3, #-1628]
	cmp	r2, #0
	beq	.L1725
	b	FtlReadRefresh.part.15
.L1725:
	ldr	r2, .L1731+4
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
	bhi	.L1727
	add	r1, r0, r1
	cmp	r1, r2
	bcc	.L1727
	ldr	r3, .L1731
	ldrb	r3, [r3, #-2044]	@ zero_extendqisi2
	cmp	r3, #0
	bne	.L1728
.L1727:
	ldr	r3, .L1731
	mov	r1, #1
	str	r2, [r3, #-1632]
	str	r1, [r3, #-1628]
	mov	r1, #0
	str	r1, [r3, #-1624]
.L1728:
	mov	r0, #0
	bx	lr
.L1732:
	.align	2
.L1731:
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
	ldr	r3, .L1768
	mov	sl, r7
	str	r9, [r4, #32]
	str	r9, [r4, #28]
	ldr	r2, [r3, #-960]
	ldr	r5, [r3, #-936]
	str	r2, [r3, #188]
	str	r5, [r3, #192]
	mvn	r3, #0
	strh	r3, [r4, #0]	@ movhi
	strh	r3, [r4, #2]	@ movhi
	mov	r3, #1
	str	r3, [r4, #36]
	ldr	r3, [sp, #16]
	sub	r3, r3, #1
	str	r3, [sp, #20]
	ldr	r3, .L1768+4
	b	.L1734
.L1748:
	ldr	r0, [sp, #20]
	cmp	r2, r0
	mov	r2, r2, asl #1
	bne	.L1735
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
	ldr	r6, .L1768
	uxth	r0, r0
	add	r3, r0, #1
	strh	r3, [r4, #2]	@ movhi
	ldr	r3, [r2, r1, asl #2]
	sxth	r9, r0
	add	r9, r9, #1
	str	r3, [r4, #28]
	b	.L1736
.L1739:
	ldr	r0, [sp, #16]
	mov	r1, #1
	ldrh	r2, [r0, #0]
	ldr	r0, .L1768+8
	orr	r3, r3, r2, asl #10
	mov	r2, r1
	str	r3, [r6, #184]
	bl	FlashReadPages
	ldr	r3, [r6, #180]
	cmn	r3, #1
	ldreq	r1, [sp, #16]
	ldreqh	r3, [r1, #0]
	streqh	r3, [r4, #40]	@ movhi
	beq	.L1738
	ldrh	r3, [r5, #8]
	cmp	r3, r8
	bcs	.L1738
	ldrh	r2, [r4, #4]
	ldrh	r1, [r5, #0]
	cmp	r1, r2
	ldreq	r2, [r6, #184]
	streq	r2, [fp, r3, asl #2]
.L1738:
	add	sl, sl, #1
	uxth	sl, sl
.L1736:
	sxth	r3, sl
	cmp	r3, r9
	blt	.L1739
	b	.L1740
.L1735:
	ldr	r6, .L1768
	movw	r7, #3918
	add	r0, sl, r2
	str	r0, [sp, #28]
	add	r0, r6, #180
	ldr	r1, [r6, #-960]
	str	r1, [r6, #188]
	ldrh	r1, [sl, r2]
	ldrh	r2, [r3, r7]
	sub	r2, r2, #1
	orr	r2, r2, r1, asl #10
	mov	r1, #1
	str	r2, [r6, #184]
	mov	r2, r1
	str	r3, [sp, #12]
	bl	FlashReadPages
	ldr	r2, [r6, #180]
	ldr	r3, [sp, #12]
	cmn	r2, #1
	beq	.L1761
	ldrh	r1, [r5, #0]
	ldrh	r2, [r4, #4]
	cmp	r1, r2
	bne	.L1761
	ldrh	r1, [r5, #8]
	movw	r2, #64245
	cmp	r1, r2
	bne	.L1761
	b	.L1762
.L1744:
	ldr	r0, [r6, #-960]
	mov	ip, r1, asl #3
	add	r2, r2, #1
	ldr	r1, [r0, r1, asl #3]
	uxth	r2, r2
	uxth	r1, r1
	cmp	r1, r8
	addcc	r0, r0, ip
	ldrcc	r0, [r0, #4]
	strcc	r0, [fp, r1, asl #2]
	b	.L1742
.L1762:
	mov	r2, #0
.L1742:
	ldrh	r0, [r3, r7]
	sxth	r1, r2
	sub	r0, r0, #1
	cmp	r1, r0
	blt	.L1744
	b	.L1745
.L1747:
	ldr	r0, [sp, #28]
	ldrh	r1, [r0, #0]
	ldr	r0, .L1768+8
	orr	r2, r2, r1, asl #10
	mov	r1, #1
	str	r2, [r7, #184]
	mov	r2, r1
	str	r3, [sp, #12]
	bl	FlashReadPages
	ldr	r2, [r7, #180]
	ldr	r3, [sp, #12]
	cmn	r2, #1
	beq	.L1746
	ldrh	r2, [r5, #8]
	cmp	r2, r8
	bcs	.L1746
	ldrh	r1, [r4, #4]
	ldrh	r0, [r5, #0]
	cmp	r0, r1
	ldreq	r1, [r7, #184]
	streq	r1, [fp, r2, asl #2]
.L1746:
	add	r6, r6, #1
	uxth	r6, r6
	b	.L1764
.L1761:
	ldr	r7, .L1768
	mov	r6, #0
.L1764:
	movw	r1, #3918
	sxth	r2, r6
	ldrh	r1, [r3, r1]
	cmp	r2, r1
	blt	.L1747
.L1745:
	add	r9, r9, #1
	uxth	r9, r9
.L1734:
	ldr	r1, [sp, #16]
	sxth	r2, r9
	cmp	r2, r1
	blt	.L1748
	mov	r7, sl
.L1740:
	ldr	r3, .L1768+12
	cmp	r4, r3
	bne	.L1749
	ldr	r9, .L1768+4
	movw	r3, #3960
	ldrh	r3, [r9, r3]
	cmp	r3, #0
	addne	r5, r4, #1088
	movne	r8, #0
	movne	sl, r4
	bne	.L1750
	b	.L1749
.L1757:
	ldr	r3, [r5, #-900]
	mov	r4, r8, asl #2
	ldr	r2, [r3, r8, asl #2]
	sub	r1, r2, #1
	cmn	r1, #3
	bhi	.L1751
	ldr	r1, [r5, #-1032]
	ldr	r1, [r1, r8, asl #2]
	cmp	r2, r1
	beq	.L1751
	ldr	r2, [r5, #-960]
	mov	r1, #1
	ldr	r6, [r5, #-936]
	ldr	r0, .L1768+8
	str	r2, [r5, #188]
	mov	r2, r1
	str	r6, [r5, #192]
	ldr	r3, [r3, r4]
	str	r3, [r5, #184]
	bl	FlashReadPages
	ldr	r3, [r5, #180]
	cmn	r3, #1
	beq	.L1751
	ldr	r0, .L1768+16
	ldrh	r2, [r6, #0]
	ldrh	r3, [r5, r0]
	cmp	r2, r3
	bne	.L1751
	ldr	r3, [r5, #-1032]
	ldr	fp, [r6, #4]
	ldr	r2, [r3, r4]
	str	r2, [r5, #184]
	ldr	r3, [r3, r4]
	cmp	r3, #0
	streq	r3, [r6, #4]
	beq	.L1753
	mov	r1, #1
	ldr	r0, .L1768+8
	mov	r2, r1
	bl	FlashReadPages
.L1753:
	ldr	r1, [r6, #4]
	cmp	fp, r1
	bls	.L1751
	ldr	r2, [r5, #-900]
	ldr	r3, [r5, #-1032]
	ldr	r0, .L1768+20
	str	r1, [sp, #0]
	ldr	r1, [r2, r4]
	ldr	r2, [r3, r4]
	mov	r3, fp
	bl	printk
	ldr	r3, [r5, #-900]
	ldr	r2, [r3, r4]
	ldr	r3, [r5, #-1032]
	str	r2, [r3, r4]
	mov	r2, #0
	ldr	r3, [r5, #-900]
	ldr	r0, [r3, r4]
	ldr	r3, .L1768+24
	ubfx	r0, r0, #10, #16
	ldrh	r3, [r5, r3]
	b	.L1754
.L1755:
	mov	r1, r1, asl #1
	ldrh	r1, [r7, r1]
	cmp	r1, r0
	beq	.L1751
	add	r2, r2, #1
	uxth	r2, r2
.L1754:
	sxth	r1, r2
	cmp	r1, r3
	blt	.L1755
	b	.L1766
.L1767:
	mov	r3, r3, asl #1
	strh	r0, [r7, r3]	@ movhi
	ldr	r3, .L1768+24
	ldrh	r2, [r5, r3]
	add	r2, r2, #1
	strh	r2, [r5, r3]	@ movhi
	bl	remove_from_free_sys_Queue
.L1751:
	add	r8, r8, #1
.L1750:
	mov	r3, #3952
	ldrh	r3, [r9, r3]
	cmp	r8, r3
	bcc	.L1757
	mov	r4, sl
.L1749:
	mov	r0, r4
	bl	ftl_free_no_use_map_blk
	ldr	r1, .L1768+4
	movw	r3, #3918
	ldrh	r2, [r4, #2]
	ldrh	r3, [r1, r3]
	cmp	r2, r3
	bne	.L1758
	mov	r0, r4
	bl	ftl_map_blk_alloc_new_blk
.L1758:
	mov	r0, r4
	bl	ftl_map_blk_gc
	mov	r0, r4
	bl	ftl_map_blk_gc
	mov	r0, #0
	add	sp, sp, #36
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, sl, fp, pc}
.L1766:
	ldr	r2, [r9, #3944]
	cmp	r3, r2
	bcs	.L1751
	b	.L1767
.L1769:
	.align	2
.L1768:
	.word	.LANCHOR2
	.word	.LANCHOR0
	.word	.LANCHOR2+180
	.word	.LANCHOR2-1088
	.word	-1084
	.word	.LC124
	.word	-1044
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
	ldr	r0, .L1771
	bl	FtlMapTblRecovery
	mov	r0, #0
	ldmfd	sp!, {r3, pc}
.L1772:
	.align	2
.L1771:
	.word	.LANCHOR2-1088
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
	ldr	r3, .L1774
	ldr	r0, .L1774+4
	ldrh	r1, [r3, r2]
	movw	r2, #258
	strh	r1, [r0, r2]	@ movhi
	ldr	r2, .L1774+8
	strh	r2, [r0, #252]	@ movhi
	movw	r2, #3962
	ldrh	r1, [r3, r2]
	mov	r2, #256
	strh	r1, [r0, r2]	@ movhi
	movw	r2, #3938
	ldrh	r2, [r3, r2]
	ldr	r3, [r3, #3964]
	strh	r2, [r0, #254]	@ movhi
	mvn	r2, #0
	str	r3, [r0, #260]
	ldr	r3, [r0, #-908]
	str	r3, [r0, #264]
	ldr	r3, [r0, #-912]
	str	r3, [r0, #268]
	ldr	r3, [r0, #-904]
	str	r3, [r0, #272]
	mov	r3, #288
	strh	r2, [r0, r3]	@ movhi
	add	r0, r0, #248
	bl	FtlMapTblRecovery
	mov	r0, #0
	ldmfd	sp!, {r3, pc}
.L1775:
	.align	2
.L1774:
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
	beq	.L1777
	ldrh	r3, [r0, #2]
	ldr	r1, .L1866
	ldrb	fp, [r0, #6]	@ zero_extendqisi2
	str	r3, [sp, #12]
	movw	r3, #3916
	ldrh	r3, [r1, r3]
	ldr	ip, [sp, #12]
	str	fp, [sp, #24]
	cmp	r3, ip
	mov	r3, #0
	streqh	r3, [r0, #4]	@ movhi
	moveq	r2, r3	@ movhi
	ldrneh	r0, [r0, #16]
	bne	.L1779
	b	.L1863
.L1780:
	add	r3, r3, #1
	uxth	r3, r3
	add	r1, r4, r3, asl #1
	ldrh	r0, [r1, #16]
.L1779:
	cmp	r0, r2
	beq	.L1780
	ldrb	r1, [r4, #8]	@ zero_extendqisi2
	cmp	r1, #1
	bne	.L1781
	bl	FtlGetLastWrittenPage
	cmn	r0, #1
	mov	r7, r0
	beq	.L1782
	ldr	r3, .L1866
	ldrb	r2, [r3, #928]	@ zero_extendqisi2
	cmp	r2, #0
	bne	.L1856
	add	r3, r3, r0, asl #1
	movw	r2, #3076
	ldrh	r8, [r3, r2]
	b	.L1848
.L1781:
	mov	r1, #0
	bl	FtlGetLastWrittenPage
	cmn	r0, #1
	mov	r7, r0
	beq	.L1782
.L1856:
	mov	r8, r7
.L1848:
	ldr	r3, .L1866
	movw	r2, #3848
	mov	r5, #0
	str	r4, [sp, #32]
	movw	ip, #65535
	ldrh	r9, [r3, r2]
	ldr	r2, .L1866+4
	ldr	sl, [r2, #-972]
	ldr	r6, [r2, #-1168]
	movw	r2, #3928
	ldrh	lr, [r3, r2]
	mov	r2, r4
	mov	r3, r5
	b	.L1784
.L1782:
	mov	r3, #0
	strh	r3, [r4, #2]	@ movhi
	mov	r2, r3	@ movhi
.L1863:
	strb	r2, [r4, #6]
	b	.L1777
.L1786:
	ldrh	r0, [r2, #16]
	cmp	r0, ip
	beq	.L1785
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
.L1785:
	add	r3, r3, #1
	add	r2, r2, #2
	uxth	r3, r3
.L1784:
	cmp	r3, r9
	bne	.L1786
	ldrb	r3, [r4, #8]	@ zero_extendqisi2
	cmp	r3, #1
	movne	r3, #0
	bne	.L1857
	ldr	r3, .L1866
	ldrb	r3, [r3, #928]	@ zero_extendqisi2
	adds	r3, r3, #0
	movne	r3, #1
.L1857:
	ldr	r6, .L1866+4
	mov	r1, r5
	str	r3, [sp, #20]
	mov	sl, #0
	ldr	r2, [sp, #20]
	ldr	r0, [r6, #-972]
	bl	FlashReadPages
	ldr	fp, [r6, #-1800]
	movw	ip, #65535
	str	ip, [sp, #4]
	str	fp, [sp, #8]
	b	.L1788
.L1793:
	mov	lr, #36
	ldr	r1, [r6, #-972]
	mul	r2, lr, sl
	add	r3, r1, r2
	ldr	r2, [r1, r2]
	cmp	r2, #0
	bne	.L1789
	ldr	r3, [r3, #12]
	ldr	fp, [r3, #4]
	cmn	fp, #1
	beq	.L1790
	ldr	r1, [r6, #-1800]
	mov	r0, fp
	str	r3, [sp, #0]
	bl	ftl_cmp_data_ver
	ldr	r3, [sp, #0]
	cmp	r0, #0
	addne	fp, fp, #1
	strne	fp, [r6, #-1800]
.L1790:
	ldr	r3, [r3, #0]
	cmn	r3, #1
	bne	.L1792
	b	.L1791
.L1789:
	ldr	r1, [r3, #4]
	movw	fp, #294
	ldr	r0, .L1866+8
	bl	printk
	ldrh	r3, [r4, #0]
	uxth	r2, r8
	str	r2, [sp, #4]
	strh	r3, [r6, fp]	@ movhi
.L1792:
	add	sl, sl, #1
.L1788:
	uxth	r9, sl
	cmp	r9, r5
	bcc	.L1793
.L1791:
	ldr	r3, .L1866+4
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
	ldr	r3, .L1866
	cmp	r2, #1
	str	r0, [sp, #16]
	bne	.L1796
	ldrb	r1, [r3, #928]	@ zero_extendqisi2
	cmp	r1, #0
	addeq	r7, r3, r7, asl #1
	movweq	r1, #3076
	ldreqh	r7, [r7, r1]
.L1796:
	movw	r1, #3916
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
	beq	.L1864
.L1798:
	ldr	fp, [sp, #4]
	movw	r3, #65535
	ldr	ip, [sp, #8]
	cmp	fp, r3
	sub	r6, ip, #1
	bne	.L1799
	cmp	r2, #0
	bne	.L1800
.L1799:
	ldr	r2, .L1866+4
	uxth	r3, r8
	ldr	ip, [sp, #12]
	mvn	fp, #0
	mov	sl, r3
	mov	r9, fp
	ldr	r1, [r2, #-1008]
	ldr	r8, .L1866+4
	cmn	r1, #1
	streq	r6, [r2, #-1008]
	ldr	r2, [r2, #-1008]
	str	r2, [sp, #4]
	add	r2, ip, #7
	cmp	r3, r2
	subgt	r5, r3, #7
	mov	r3, r7
	ldrle	r5, [sp, #12]
	mov	r7, r6
	uxthgt	r5, r5
	b	.L1803
.L1805:
	ldrh	r0, [r1, #16]
	movw	lr, #65535
	cmp	r0, lr
	beq	.L1804
	ldr	ip, [sp, #8]
	mov	r3, #36
	orr	r0, r5, r0, asl #10
	mla	lr, r3, r6, ip
	add	r6, r6, #1
	uxth	r6, r6
	str	r0, [lr, #4]
.L1804:
	add	r2, r2, #1
	add	r1, r1, #2
	uxth	r2, r2
.L1816:
	ldr	lr, [sp, #36]
	cmp	r2, lr
	bne	.L1805
	ldr	r3, [sp, #28]
	mov	r1, r6
	ldr	r0, [r8, #-972]
	ldr	r2, [sp, #20]
	str	r3, [sp, #0]
	bl	FlashReadPages
	ldr	r2, .L1866
	add	r0, r8, r5, asl #1
	mov	r1, #0
	str	r0, [sp, #8]
	ldr	r3, [sp, #0]
	ldrb	ip, [r2, #928]	@ zero_extendqisi2
	ldr	r2, [r8, #-972]
	str	ip, [sp, #28]
	b	.L1806
.L1814:
	ldr	r0, [r2, #0]
	cmp	r0, #0
	bne	.L1807
	ldr	r0, [r2, #12]
	movw	ip, #65535
	ldrh	lr, [r0, #0]
	cmp	lr, ip
	beq	.L1808
	ldr	r0, [r0, #4]
	cmn	r0, #1
	beq	.L1808
	cmn	fp, #1
	ldr	r9, [r8, #-1008]
	str	r0, [r8, #-1008]
	bne	.L1808
	ldr	lr, [sp, #8]
	sub	r0, lr, #892
	ldrh	r0, [r0, #0]
	cmp	r0, ip
	bne	.L1809
	ldr	r0, [sp, #28]
	cmp	r0, #0
	beq	.L1808
.L1809:
	cmp	r9, r7
	mvneq	fp, #0
	movne	fp, r9
	b	.L1808
.L1807:
	mov	r6, r7
	ldrh	r1, [r4, #0]
	mov	r7, r3
	ldr	r3, .L1866+4
	movw	r2, #294
	strh	r1, [r3, r2]	@ movhi
	ldrb	r2, [r4, #8]	@ zero_extendqisi2
	cmp	r2, #0
	bne	.L1800
	add	r5, r3, r5, asl #1
	movw	r2, #65535
	sub	r5, r5, #892
	ldrh	r1, [r5, #0]
	cmp	r1, r2
	bne	.L1810
	cmn	fp, #1
	bne	.L1859
.L1811:
	ldr	fp, [sp, #4]
	cmp	fp, r6
	beq	.L1812
.L1859:
	str	fp, [r3, #-1008]
	b	.L1800
.L1812:
	ldr	r2, [r3, #-1008]
	b	.L1865
.L1810:
	cmp	r9, r6
	beq	.L1813
	cmn	r9, #1
	strne	r9, [r3, #-1008]
	b	.L1800
.L1813:
	ldr	r2, [r3, #-1008]
	cmp	r2, r6
	beq	.L1800
.L1865:
	sub	r2, r2, #1
	b	.L1860
.L1808:
	add	r1, r1, #1
	add	r2, r2, #36
	uxth	r1, r1
.L1806:
	cmp	r1, r6
	bne	.L1814
	add	r5, r5, #1
	uxth	r5, r5
.L1803:
	cmp	r5, sl
	bhi	.L1815
	ldr	r1, .L1866
	movw	r2, #3848
	ldr	lr, [r8, #-972]
	mov	r6, #0
	str	r3, [sp, #28]
	ldrh	ip, [r1, r2]
	mov	r1, r4
	str	lr, [sp, #8]
	mov	r2, r6
	str	ip, [sp, #36]
	b	.L1816
.L1815:
	mov	r6, r7
	mov	r7, r3
	ldr	r3, .L1866+4
	mvn	r2, #0
.L1860:
	str	r2, [r3, #-1008]
.L1800:
	ldr	r5, .L1866+4
	mov	r3, #296
	mov	r2, #1
	sub	r0, r5, #1088
	strh	r2, [r5, r3]	@ movhi
	bl	FtlMapBlkWriteDumpData
	ldr	r8, [sp, #12]
	str	r7, [sp, #8]
.L1817:
	ldr	r3, .L1866
	movw	r2, #3848
	mov	r7, #0
	ldr	lr, [r5, #-972]
	mov	fp, #36
	ldrh	sl, [r3, r2]
	mov	r2, r4
	ldrb	ip, [r3, #928]	@ zero_extendqisi2
	mov	r3, r7
	b	.L1818
.L1821:
	ldrh	r0, [r2, #16]
	movw	r1, #65535
	cmp	r0, r1
	beq	.L1819
	mla	r1, fp, r7, lr
	orr	r0, r8, r0, asl #10
	str	r0, [r1, #4]
	ldrb	r9, [r4, #8]	@ zero_extendqisi2
	cmp	r9, #1
	bne	.L1820
	cmp	ip, #0
	orrne	r0, r0, #-2147483648
	strne	r0, [r1, #4]
.L1820:
	add	r7, r7, #1
	uxth	r7, r7
.L1819:
	add	r3, r3, #1
	add	r2, r2, #2
	uxth	r3, r3
.L1818:
	cmp	r3, sl
	bne	.L1821
	mov	r1, r7
	ldr	r0, [r5, #-972]
	ldr	r2, [sp, #20]
	bl	FlashReadPages
	mov	r3, #36
	mul	r3, r3, r7
	mov	r7, #0
	str	r3, [sp, #36]
	b	.L1822
.L1843:
	ldr	r9, [r5, #-972]
	add	r9, r9, r7
	ldr	sl, [r9, #4]
	ubfx	r0, sl, #10, #16
	str	sl, [sp, #52]
	bl	P2V_plane
	ldr	r3, [sp, #12]
	cmp	r8, r3
	bcc	.L1823
	ldr	fp, [sp, #24]
	mov	ip, r3
	cmp	r0, fp
	movcs	r3, #0
	movcc	r3, #1
	cmp	r8, ip
	movne	r3, #0
	cmp	r3, #0
	bne	.L1823
	ldr	r3, [sp, #16]
	ldr	ip, [sp, #8]
	cmp	r0, r3
	cmpeq	r8, ip
	beq	.L1855
	ldr	r3, [r9, #0]
	cmn	r3, #1
	beq	.L1825
	ldr	sl, [r9, #12]
	movw	r3, #61589
	ldrh	r2, [sl, #0]
	cmp	r2, r3
	bne	.L1832
.L1826:
	ldr	r6, [sl, #4]
	cmn	r6, #1
	beq	.L1827
	ldr	r1, [r5, #-1800]
	mov	r0, r6
	bl	ftl_cmp_data_ver
	cmp	r0, #0
	addne	r3, r6, #1
	strne	r3, [r5, #-1800]
.L1827:
	ldr	r9, [sl, #8]
	add	r1, sp, #48
	ldr	r3, [sl, #12]
	mov	r2, #0
	mov	r0, r9
	str	r3, [sp, #44]
	bl	log2phys
	ldr	r1, [r5, #-1008]
	cmn	r1, #1
	beq	.L1828
	mov	r0, r6
	bl	ftl_cmp_data_ver
	cmp	r0, #0
	beq	.L1828
	ldr	r3, [sp, #44]
	cmn	r3, #1
	beq	.L1829
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
	bne	.L1830
	b	.L1831
.L1829:
	ldr	r3, [sp, #52]
	ldr	r2, [sp, #48]
	cmp	r2, r3
	bne	.L1832
	mov	r0, r9
	add	r1, sp, #44
	mov	r2, #1
	bl	log2phys
.L1832:
	ldrh	r0, [r4, #0]
	b	.L1862
.L1830:
	ldr	r3, [sp, #4]
	ldr	sl, [r3, #8]
	cmp	sl, r9
	bne	.L1831
	ldr	r0, [r5, #-1008]
	ldr	r1, [sp, #28]
	bl	ftl_cmp_data_ver
	cmp	r0, #0
	beq	.L1831
	ldr	r3, [sp, #48]
	ldr	r2, [sp, #52]
	cmp	r3, r2
	beq	.L1836
.L1833:
	ldr	r2, [sp, #44]
	cmp	r3, r2
	beq	.L1831
	cmn	r3, #1
	streq	r3, [fp, #0]
	beq	.L1835
	ldr	ip, [fp, #12]
	mov	r0, fp
	str	r3, [fp, #4]
	mov	r1, #1
	mov	r2, #0
	str	ip, [sp, #4]
	bl	FlashReadPages
.L1835:
	ldr	r3, [r5, #-972]
	ldr	r3, [r3, r7]
	cmn	r3, #1
	beq	.L1836
	ldr	r3, [sp, #4]
	ldr	r0, [r5, #-1008]
	ldr	r9, [r3, #4]
	mov	r1, r9
	bl	ftl_cmp_data_ver
	cmp	r0, #0
	beq	.L1836
	ldr	r0, [sp, #28]
	mov	r1, r9
	bl	ftl_cmp_data_ver
	cmp	r0, #0
	beq	.L1831
.L1836:
	mov	r0, sl
	ldr	r1, [sp, #44]
	bl	FtlReUsePrevPpa
.L1831:
	ldrh	r0, [r4, #0]
	mvn	r3, #0
	str	r3, [sp, #44]
	bl	decrement_vpc_count
	b	.L1838
.L1828:
	ldr	r3, [sp, #52]
	ldr	r2, [sp, #48]
	cmp	r2, r3
	beq	.L1838
	mov	r0, r9
	add	r1, sp, #52
	mov	r2, #1
	bl	log2phys
	ldr	fp, [sp, #48]
	cmn	fp, #1
	beq	.L1838
	ldr	r3, [sp, #44]
	cmp	fp, r3
	beq	.L1838
	ubfx	r0, fp, #10, #16
	bl	P2V_block_in_plane
	ldr	r3, .L1866+12
	ldrh	r3, [r5, r3]
	cmp	r3, r0
	beq	.L1839
	ldr	r3, .L1866+16
	ldrh	r3, [r5, r3]
	cmp	r3, r0
	beq	.L1839
	ldr	r3, .L1866+20
	ldrh	r3, [r5, r3]
	cmp	r3, r0
	bne	.L1838
.L1839:
	ldr	ip, .L1866+4
	mov	r1, #1
	mov	r2, #0
	ldr	r0, [ip, #-972]
	str	fp, [r0, #4]
	ldr	sl, [r0, #12]
	bl	FlashReadPages
	ldr	r0, .L1866+4
	ldr	r1, [sl, #4]
	ldr	r3, [r0, #-972]
	ldr	r3, [r3, #0]
	cmn	r3, #1
	beq	.L1838
	mov	r0, r6
	bl	ftl_cmp_data_ver
	cmp	r0, #0
	bne	.L1838
	mov	r0, r9
	add	r1, sp, #48
	mov	r2, #1
	bl	log2phys
.L1838:
	ldr	r0, [sp, #44]
	cmn	r0, #1
	beq	.L1823
	ubfx	r0, r0, #10, #16
	bl	P2V_block_in_plane
	ldr	r2, [r5, #-2016]
	mov	r3, r0, asl #1
	mov	r1, r0
	ldrh	r3, [r2, r3]
	cmp	r3, #0
	beq	.L1840
.L1862:
	bl	decrement_vpc_count
	b	.L1823
.L1840:
	ldr	r0, .L1866+24
	bl	printk
	b	.L1823
.L1825:
	ldrh	r3, [r4, #0]
	movw	r1, #294
	mov	r2, r6
	ldr	r0, .L1866+28
	strh	r3, [r5, r1]	@ movhi
	mov	r1, sl
	bl	printk
	ldr	r3, [r5, #300]
	cmp	r3, #31
	addls	r2, r5, r3, asl #2
	addls	r3, r3, #1
	ldrls	r1, [sp, #52]
	strls	r3, [r5, #300]
	strls	r1, [r2, #304]
	ldrh	r0, [r4, #0]
	bl	decrement_vpc_count
	ldr	r3, [r5, #-1008]
	cmn	r3, #1
	beq	.L1861
.L1842:
	cmp	r3, r6
	bls	.L1823
.L1861:
	str	r6, [r5, #-1008]
.L1823:
	add	r7, r7, #36
.L1822:
	ldr	r3, [sp, #36]
	cmp	r7, r3
	bne	.L1843
	ldrb	r3, [r4, #8]	@ zero_extendqisi2
	add	r8, r8, #1
	cmp	r3, #1
	uxth	r8, r8
	bne	.L1844
	ldr	r3, .L1866
	ldrb	r2, [r3, #928]	@ zero_extendqisi2
	cmp	r2, #0
	beq	.L1844
	movw	r2, #3918
	ldr	ip, [sp, #8]
	ldrh	r3, [r3, r2]
	cmp	ip, r8
	cmpeq	r3, r8
	beq	.L1855
.L1844:
	ldr	r3, .L1866
	movw	r2, #3916
	ldrh	r2, [r3, r2]
	cmp	r8, r2
	bne	.L1817
	mov	r2, #0
	movw	r1, #3848
	strh	r2, [r4, #4]	@ movhi
	ldrh	r1, [r3, r1]
	mov	r3, r2
	strh	r8, [r4, #2]	@ movhi
	movw	r2, #65535
	ldr	r0, [sp, #32]
	b	.L1845
.L1847:
	add	r0, r0, #2
	ldrh	ip, [r0, #14]
	cmp	ip, r2
	strneb	r3, [r4, #6]
	bne	.L1777
.L1846:
	add	r3, r3, #1
	uxth	r3, r3
.L1845:
	cmp	r3, r1
	bne	.L1847
	b	.L1777
.L1855:
.L1824:
	ldr	r7, [sp, #8]
	ldr	r3, [sp, #16]
	strh	r7, [r4, #2]	@ movhi
	strb	r3, [r4, #6]
.L1864:
	mov	r0, r4
	mov	r1, r7
	mov	r2, r3
	bl	ftl_sb_update_avl_pages
.L1777:
	mov	r0, #0
	add	sp, sp, #60
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, sl, fp, pc}
.L1867:
	.align	2
.L1866:
	.word	.LANCHOR0
	.word	.LANCHOR2
	.word	.LC125
	.word	-1996
	.word	-1948
	.word	-1900
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
	stmfd	sp!, {r4, r5, r6, r7, r8, sl, lr}
	.save {r4, r5, r6, r7, r8, sl, lr}
	.pad #44
	sub	sp, sp, #44
	ldr	r4, .L1877
	ldr	r3, [r4, #-2088]
	mov	r2, r4
	cmp	r3, #0
	bne	.L1868
	ldr	r3, .L1877+4
	ldrh	r3, [r4, r3]
	cmp	r3, #0
	beq	.L1870
	ldrb	r1, [r4, #-1988]	@ zero_extendqisi2
	cmp	r1, #0
	bne	.L1870
	ldr	r5, .L1877+8
	movw	r0, #3916
	ldrb	r1, [r4, #-1989]	@ zero_extendqisi2
	ldrh	r0, [r5, r0]
	mul	r1, r0, r1
	cmp	r3, r1
	beq	.L1870
	ldrb	r8, [r4, #-1986]	@ zero_extendqisi2
	movw	r3, #3848
	ldr	r7, [r5, #3976]
	cmp	r8, #0
	ldrh	r6, [r5, r3]
	bne	.L1868
	sub	r7, r7, #1
	mov	r1, sp
	mov	r2, r8
	mov	r0, r7
	bl	log2phys
	ldr	r3, [sp, #0]
	ldr	r0, [r4, #-960]
	ldr	r4, [r4, #-936]
	cmn	r3, #1
	str	r3, [sp, #8]
	str	r7, [sp, #20]
	str	r0, [sp, #12]
	str	r4, [sp, #16]
	str	r8, [r4, #4]
	beq	.L1871
	add	r0, sp, #4
	mov	r1, #1
	mov	r2, r8
	bl	FlashReadPages
	b	.L1872
.L1871:
	movw	r3, #3926
	mov	r1, #255
	ldrh	r2, [r5, r3]
	bl	ftl_memset
.L1872:
	ldr	r3, .L1877+12
	mov	r6, r6, asl #2
	ldr	r5, .L1877
	ldr	sl, .L1877+4
	strh	r3, [r4, #0]	@ movhi
	ldr	r8, .L1877+16
	b	.L1873
.L1876:
	ldrh	r3, [r5, sl]
	cmp	r3, #0
	beq	.L1874
	ldr	r3, [sp, #8]
	sub	r6, r6, #1
	str	r7, [r4, #8]
	ldr	r0, .L1877+20
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
.L1873:
	cmp	r6, #0
	bne	.L1876
.L1874:
	ldr	r3, .L1877
	mov	r2, #1
	strb	r2, [r3, #-1986]
	b	.L1868
.L1870:
	mov	r3, #0
	strb	r3, [r2, #-1986]
.L1868:
	add	sp, sp, #44
	ldmfd	sp!, {r4, r5, r6, r7, r8, sl, pc}
.L1878:
	.align	2
.L1877:
	.word	.LANCHOR2
	.word	-1992
	.word	.LANCHOR0
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
	ldr	r7, .L1883
	movw	r6, #3954
	ldr	r5, .L1883+4
	b	.L1880
.L1882:
	ldr	r3, [r5, #-1852]
	mov	r2, #12
	mla	r3, r2, r4, r3
	ldr	r3, [r3, #4]
	cmp	r3, #0
	bge	.L1881
	mov	r0, r4
	bl	flush_l2p_region
.L1881:
	add	r4, r4, #1
	uxth	r4, r4
.L1880:
	ldrh	r3, [r7, r6]
	cmp	r3, r4
	bhi	.L1882
	mov	r0, #0
	ldmfd	sp!, {r3, r4, r5, r6, r7, pc}
.L1884:
	.align	2
.L1883:
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
	ldr	r6, .L1892
	ldr	r1, .L1892+4
	ldr	r0, .L1892+8
	bl	printk
	movw	r3, #3858
	ldrh	r2, [r6, r3]
	mov	r1, #0
	ldr	r5, .L1892+12
	mov	r2, r2, asl #1
	ldr	r0, [r5, #-916]
	bl	ftl_memset
	b	.L1886
.L1888:
	mov	r0, r4
	add	r1, sp, #4
	mov	r2, #0
	bl	log2phys
	ldr	r0, [sp, #4]
	cmn	r0, #1
	beq	.L1887
	ubfx	r0, r0, #10, #16
	bl	P2V_block_in_plane
	ldr	r3, [r5, #-916]
	mov	r0, r0, asl #1
	ldrh	r2, [r3, r0]
	add	r2, r2, #1
	strh	r2, [r3, r0]	@ movhi
.L1887:
	add	r4, r4, #1
.L1886:
	ldr	r3, [r6, #3976]
	cmp	r4, r3
	bcc	.L1888
	mov	r4, #0
	ldr	r7, .L1892
	ldr	r5, .L1892+12
	movw	r8, #65535
	b	.L1889
.L1891:
	ldr	r3, [r5, #-2016]
	mov	r6, r4, asl #1
	ldrh	r2, [r3, r6]
	ldr	r3, [r5, #-916]
	ldrh	r3, [r3, r6]
	cmp	r2, r3
	beq	.L1890
	cmp	r2, r8
	beq	.L1890
	ldr	r0, .L1892+16
	mov	r1, r4
	bl	printk
	ldr	r3, .L1892+20
	ldrh	r3, [r5, r3]
	cmp	r3, r4
	beq	.L1890
	ldr	r3, .L1892+24
	ldrh	r3, [r5, r3]
	cmp	r3, r4
	beq	.L1890
	ldr	r3, .L1892+28
	ldrh	r3, [r5, r3]
	cmp	r3, r4
	beq	.L1890
	ldr	r3, [r5, #-916]
	mov	r0, r4
	ldrh	r2, [r3, r6]
	ldr	r3, [r5, #-2016]
	strh	r2, [r3, r6]	@ movhi
	bl	update_vpc_list
	bl	l2p_flush
	bl	FtlVpcTblFlush
.L1890:
	add	r4, r4, #1
	uxth	r4, r4
.L1889:
	mov	r3, #3856
	ldrh	r3, [r7, r3]
	cmp	r3, r4
	bhi	.L1891
	ldmfd	sp!, {r2, r3, r4, r5, r6, r7, r8, pc}
.L1893:
	.align	2
.L1892:
	.word	.LANCHOR0
	.word	.LANCHOR3+15
	.word	.LC128
	.word	.LANCHOR2
	.word	.LC129
	.word	-1996
	.word	-1900
	.word	-1948
	.fnend
	.size	FtlVpcCheckAndModify, .-FtlVpcCheckAndModify
	.align	2
	.global	allocate_new_data_superblock
	.type	allocate_new_data_superblock, %function
allocate_new_data_superblock:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	ldr	r3, .L1906
	stmfd	sp!, {r4, r5, r6, lr}
	.save {r4, r5, r6, lr}
	mov	r4, r0
	ldr	r2, [r3, #-2088]
	ldrh	r5, [r0, #0]
	cmp	r2, #0
	bne	.L1895
	movw	r2, #65535
	cmp	r5, r2
	beq	.L1896
	ldr	r2, [r3, #-2016]
	mov	r3, r5, asl #1
	mov	r0, r5
	ldrh	r3, [r2, r3]
	cmp	r3, #0
	beq	.L1897
	bl	INSERT_DATA_LIST
	b	.L1896
.L1897:
	bl	INSERT_FREE_LIST
.L1896:
	mov	r3, #0
	strb	r3, [r4, #8]
	ldr	r3, .L1906+4
	cmp	r4, r3
	beq	.L1898
	ldr	r2, .L1906+8
	movw	r3, #3868
	ldrh	r3, [r2, r3]
	cmp	r3, #1
	beq	.L1898
	ldrb	r2, [r2, #928]	@ zero_extendqisi2
	cmp	r2, #0
	beq	.L1899
.L1898:
	mov	r3, #1
	strb	r3, [r4, #8]
	b	.L1900
.L1899:
	ldr	r2, .L1906+12
	cmp	r4, r2
	bne	.L1900
	cmp	r3, #3
	ldr	r3, .L1906
	beq	.L1901
	ldr	r2, [r3, #-1712]
	cmp	r2, #1
	bne	.L1902
.L1901:
	mov	r2, #1
	strb	r2, [r3, #-1988]
.L1902:
	ldr	r2, .L1906+8
	ldr	r2, [r2, #3840]
	cmp	r2, #0
	beq	.L1900
	ldr	r3, [r3, #-1780]
	cmp	r3, #29
	ldrls	r3, .L1906
	movls	r2, #1
	strlsb	r2, [r3, #-1988]
.L1900:
	ldr	r3, .L1906+16
	ldr	r6, .L1906
	ldrh	r0, [r6, r3]
	movw	r3, #65535
	cmp	r0, r3
	beq	.L1903
	cmp	r5, r0
	bne	.L1904
	ldr	r2, [r6, #-2016]
	mov	r3, r0, asl #1
	ldrh	r3, [r2, r3]
	cmp	r3, #0
	beq	.L1905
.L1904:
	bl	update_vpc_list
.L1905:
	ldr	r3, .L1906+16
	mvn	r2, #0
	strh	r2, [r6, r3]	@ movhi
.L1903:
	mov	r0, r4
	bl	allocate_data_superblock
	bl	l2p_flush
	mov	r0, #0
	bl	FtlEctTblFlush
	bl	FtlVpcTblFlush
.L1895:
	mov	r0, #0
	ldmfd	sp!, {r4, r5, r6, pc}
.L1907:
	.align	2
.L1906:
	.word	.LANCHOR2
	.word	.LANCHOR2-1948
	.word	.LANCHOR0
	.word	.LANCHOR2-1996
	.word	-1020
	.fnend
	.size	allocate_new_data_superblock, .-allocate_new_data_superblock
	.align	2
	.type	ftl_do_gc.part.21, %function
ftl_do_gc.part.21:
	.fnstart
	@ args = 0, pretend = 0, frame = 24
	@ frame_needed = 0, uses_anonymous_args = 0
	ldr	r3, .L2005
	movw	ip, #65535
	ldr	r2, .L2005+4
	stmfd	sp!, {r4, r5, r6, r7, r8, r9, sl, fp, lr}
	.save {r4, r5, r6, r7, r8, r9, sl, fp, lr}
	.pad #36
	sub	sp, sp, #36
	mov	sl, r1
	str	r0, [sp, #16]
	ldrh	r0, [r3, r2]
	cmp	r0, ip
	beq	.L1909
	ldr	r1, .L2005+8
	ldrh	r4, [r3, r1]
	cmp	r4, ip
	streqh	r0, [r3, r1]	@ movhi
	mvneq	r1, #0
	streqh	r1, [r3, r2]	@ movhi
.L1909:
	ldr	r1, [sp, #16]
	ldr	r2, [r3, #-1192]
	cmp	r1, #1
	add	r2, r2, #1
	add	r2, r2, r1, asl #7
	str	r2, [r3, #-1192]
	bne	.L1910
	ldr	r3, .L2005+12
	ldr	r1, [r3, #3840]
	cmp	r1, #0
	bne	.L1911
	ldrb	r3, [r3, #928]	@ zero_extendqisi2
	cmp	r3, #0
	beq	.L1910
.L1911:
	ldr	r4, .L2005
	ldr	r3, [r4, #-1780]
	cmp	r3, #29
	bhi	.L1910
	mov	r3, #432
	ldrh	r3, [r4, r3]
	add	r2, r2, r3
	str	r2, [r4, #-1192]
	bl	FtlGcReFreshBadBlk
	ldr	r3, .L2005+16
	movw	r2, #65535
	ldrh	r3, [r4, r3]
	cmp	r3, r2
	bne	.L1910
	ldr	r2, .L2005+8
	ldrh	r2, [r4, r2]
	cmp	r2, r3
	bne	.L1910
	ldr	r3, [r4, #-1192]
	cmp	r3, #1024
	bhi	.L1912
	ldr	r3, .L2005+20
	ldrh	r3, [r4, r3]
	cmp	r3, #63
	bhi	.L1910
.L1912:
	ldr	r3, .L2005
	mov	r2, #432
	ldr	r0, .L2005+20
	mov	r1, #0
	strh	r1, [r3, r2]	@ movhi
	ldrh	ip, [r3, r0]
	ldr	r0, .L2005+24
	ldrh	r0, [r3, r0]
	add	r0, r0, #64
	cmp	ip, r0
	bgt	.L1910
	str	r1, [r3, #-1192]
	ldr	r1, [r3, #-1780]
	cmp	r1, #0
	moveq	r1, #6
	beq	.L2001
	cmp	r1, #5
	bhi	.L1914
	mov	r1, #18
.L2001:
	strh	r1, [r3, r2]	@ movhi
.L1914:
	mov	r0, #32
	movw	r7, #65535
	bl	List_get_gc_head_node
	uxth	r2, r0
	cmp	r2, r7
	beq	.L1915
	ldr	r5, .L2005
	ldr	r6, .L2005+28
	ldrh	r0, [r5, r6]
	cmp	r0, #0
	moveq	r3, #1
	streqh	r3, [r5, r6]	@ movhi
	beq	.L1915
	ldr	r3, .L2005+12
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
	bgt	.L1915
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
	beq	.L1915
	ldrh	r2, [r9, r2]
	mov	r7, r4, asl #1
	ldr	r0, .L2005+32
	mov	r1, fp
	ldrh	r3, [r9, r7]
	str	r2, [sp, #0]
	mov	r2, r4
	bl	printk
	ldrh	r3, [r5, r6]
	cmp	r3, #40
	bls	.L1917
	ldr	r3, [r5, #-2016]
	ldrh	r3, [r3, r7]
	cmp	r3, #32
	strhih	r8, [r5, r6]	@ movhi
.L1917:
	ldr	r2, .L2005
	mov	r3, #432
	mov	r1, #6
	strh	r1, [r2, r3]	@ movhi
	b	.L1919
.L1915:
	bl	GetSwlReplaceBlock
	movw	r3, #65535
	cmp	r0, r3
	mov	r4, r0
	bne	.L1919
	ldr	r2, .L2005
	mov	r3, #432
	mov	r1, #0
	strh	r1, [r2, r3]	@ movhi
.L1910:
	ldr	r5, .L2005
	movw	r4, #65535
	ldr	r3, .L2005+16
	ldrh	r2, [r5, r3]
	cmp	r2, r4
	bne	.L1919
	sub	r3, r3, #140
	ldrh	r3, [r5, r3]
	cmp	r3, r2
	movne	r4, r2
	bne	.L1919
	ldr	r2, .L2005+8
	ldrh	r7, [r5, r2]
	cmp	r7, r3
	movne	r4, r3
	bne	.L1919
	ldr	r8, .L2005+20
	ldr	r2, [r5, #-1192]
	ldrh	r3, [r5, r8]
	cmp	r3, #23
	movhi	r3, #1024
	movls	r3, #5120
	cmp	r2, r3
	movls	r4, r7
	bls	.L1919
	mov	r2, #432
	mov	r3, #0
	str	r3, [r5, #-1192]
	mov	r6, r5
	strh	r3, [r5, r2]	@ movhi
	bl	GetSwlReplaceBlock
	cmp	r0, r7
	mov	r4, r0
	bne	.L1921
	ldr	r3, .L2005+24
	ldrh	r1, [r5, r8]
	ldrh	r2, [r5, r3]
	cmp	r1, r2
	movcs	r2, #80
	strcsh	r2, [r5, r3]	@ movhi
	bcs	.L1931
	mov	r0, #64
	bl	List_get_gc_head_node
	uxth	r3, r0
	cmp	r3, r4
	beq	.L1931
	ldr	r2, [r5, #-1000]
	cmp	r2, #0
	bne	.L1924
	ldr	r2, .L2005+12
	movw	r1, #3868
	ldrh	r1, [r2, r1]
	cmp	r1, #3
	beq	.L1924
	ldr	r1, [r5, #-1712]
	cmp	r1, #0
	bne	.L1924
	ldr	r1, [r2, #3840]
	cmp	r1, #0
	bne	.L1924
	ldrb	r0, [r2, #928]	@ zero_extendqisi2
	cmp	r0, #0
	beq	.L1925
.L1924:
	ldr	r1, .L2005
	mov	r3, r3, asl #1
	movw	ip, #3848
	movw	lr, #3868
	ldr	r2, [r1, #-2016]
	ldrh	r0, [r2, r3]
	movw	r2, #3918
	ldr	r3, .L2005+12
	ldrh	r2, [r3, r2]
	ldrh	ip, [r3, ip]
	ldrh	r3, [r3, lr]
	mul	ip, ip, r2
	cmp	r3, #3
	moveq	r3, r2, lsr #1
	movne	r3, #0
	add	r3, ip, r3
	cmp	r0, r3
	bgt	.L1927
	mov	r0, #0
	bl	List_get_gc_head_node
	ldr	r2, .L2005+12
	ldr	r3, .L2005
	ldr	r2, [r2, #3976]
	ldr	r1, [r3, #-1844]
	add	r2, r2, r2, asl #1
	cmp	r1, r2, lsr #2
	ldr	r2, .L2005+24
	movhi	r1, #128
	movls	r1, #160
	strh	r1, [r3, r2]	@ movhi
	uxth	r4, r0
	b	.L1929
.L1927:
	ldr	r3, .L2005+24
	mov	r2, #128
	strh	r2, [r1, r3]	@ movhi
	b	.L1931
.L1925:
	ldr	r2, [r5, #-2016]
	mov	r3, r3, asl #1
	ldr	r6, .L2005
	ldr	r5, .L2005+24
	ldrh	r3, [r2, r3]
	cmp	r3, #7
	movhi	r3, #64
	strhih	r3, [r6, r5]	@ movhi
	bhi	.L1931
	bl	List_get_gc_head_node
	mov	r3, #128
	strh	r3, [r6, r5]	@ movhi
	uxth	r4, r0
.L1929:
	movw	r3, #65535
	cmp	r4, r3
	beq	.L1931
.L1921:
	ldr	r1, .L2005
	mov	r0, r4, asl #1
	ldr	r3, .L2005+20
	ldr	ip, [r1, #-2080]
	ldrh	r2, [r1, r3]
	ldr	r3, [r1, #-2016]
	ldrh	r3, [r3, r0]
	ldrh	r0, [ip, r0]
	str	r0, [sp, #0]
	ldr	r0, .L2005+36
	ldrh	r1, [r1, r0]
	ldr	r0, .L2005+40
	str	r1, [sp, #4]
	mov	r1, r4
	bl	printk
.L1931:
	bl	FtlGcReFreshBadBlk
.L1919:
	movw	r0, #65535
	rsb	ip, r0, r4
	rsbs	r1, ip, #0
	ldr	r3, [sp, #16]
	adc	r1, r1, ip
	ldr	r5, .L2005
	cmp	r3, #0
	movne	r2, #0
	andeq	r2, r1, #1
	cmp	r2, #0
	beq	.L1932
	ldr	r3, .L2005+20
	ldrh	r2, [r5, r3]
	cmp	r2, #24
	movhi	r6, #1
	bhi	.L1933
	ldr	r1, .L2005+12
	movw	r3, #3916
	cmp	r2, #16
	ldrh	r6, [r1, r3]
	movhi	r6, r6, lsr #5
	bhi	.L1933
	cmp	r2, #12
	movhi	r6, r6, lsr #4
	bhi	.L1933
	cmp	r2, #8
	movhi	r6, r6, lsr #2
.L1933:
	ldr	r1, .L2005+36
	ldr	r3, .L2005
	ldrh	r0, [r5, r1]
	cmp	r0, r2
	mov	r0, r1
	bcs	.L1937
	ldr	r2, .L2005+44
	movw	ip, #65535
	ldrh	r2, [r3, r2]
	cmp	r2, ip
	bne	.L1938
	ldr	ip, .L2005+8
	ldrh	ip, [r3, ip]
	cmp	ip, r2
	bne	.L1938
	mov	r2, #432
	ldrh	r0, [r3, r2]
	cmp	r0, #0
	bne	.L1939
	ldr	r2, .L2005+12
	ldr	ip, [r3, #-1844]
	ldr	r2, [r2, #3976]
	add	r2, r2, r2, asl #1
	cmp	ip, r2, lsr #2
	movcs	r2, #18
	bcs	.L2003
.L1939:
	ldr	r3, .L2005
	ldr	r2, .L2005+48
	ldr	r1, .L2005+36
	ldrh	r2, [r3, r2]
	add	r2, r2, r2, asl #1
	mov	r2, r2, asr #2
.L2003:
	strh	r2, [r3, r1]	@ movhi
	mov	r2, #0
	ldr	r3, .L2005
	str	r2, [r3, #-1184]
	b	.L1942
.L1938:
	ldr	r3, .L2005
	ldr	r2, .L2005+48
	ldrh	r2, [r3, r2]
	add	r2, r2, r2, asl #1
	mov	r2, r2, asr #2
	strh	r2, [r3, r0]	@ movhi
.L1937:
	ldr	r3, .L2005+12
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
	b	.L1943
.L1932:
	ldr	r3, .L2005+44
	ldrh	r3, [r5, r3]
	cmp	r3, r0
	bne	.L1944
	ldr	r0, .L2005+8
	ldrh	r0, [r5, r0]
	cmp	r0, r3
	movne	r1, #0
	andeq	r1, r1, #1
	cmp	r1, #0
	beq	.L1944
	ldr	r1, .L2005+16
	ldrh	r4, [r5, r1]
	cmp	r4, r3
	movne	r4, r3
	bne	.L1944
	ldr	r3, .L2005+20
	mov	r6, #432
	str	r2, [r5, #-1184]
	ldrh	r7, [r5, r3]
	add	r3, r3, #804
	ldrh	r2, [r5, r3]
	cmp	r2, r7
	bcs	.L1945
	ldrh	r2, [r5, r6]
	cmp	r2, #0
	bne	.L1946
	ldr	r2, .L2005+12
	ldr	r1, [r5, #-1844]
	ldr	r2, [r2, #3976]
	add	r2, r2, r2, asl #1
	cmp	r1, r2, lsr #2
	movcs	r2, #18
	strcsh	r2, [r5, r3]	@ movhi
	bcs	.L1948
.L1946:
	ldr	r3, .L2005
	ldr	r2, .L2005+48
	ldr	r1, .L2005+36
	ldrh	r2, [r3, r2]
	add	r2, r2, r2, asl #1
	mov	r2, r2, asr #2
	strh	r2, [r3, r1]	@ movhi
.L1948:
	bl	FtlReadRefresh
	mov	r3, #432
	ldr	r2, .L2005
	b	.L2004
.L1945:
	ldrh	r0, [r5, r6]
	cmp	r0, #0
	bne	.L1944
	ldr	r2, .L2005+48
	ldrh	r4, [r5, r2]
	add	r2, r4, r4, asl #1
	mov	r2, r2, asr #2
	strh	r2, [r5, r3]	@ movhi
	bl	List_get_gc_head_node
	ldr	r3, [r5, #-2016]
	ldr	r1, .L2005+12
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
	ble	.L1949
	sub	r4, r4, #1
	cmp	r7, r4
	blt	.L1949
	bl	FtlReadRefresh
	ldrh	r0, [r5, r6]
	b	.L1942
.L1949:
	cmp	r3, #0
	movwne	r4, #65535
	bne	.L1944
	movw	r0, #65535
	bl	decrement_vpc_count
	ldr	r3, .L2005+20
	ldr	r2, .L2005
	ldrh	r0, [r2, r3]
	add	r0, r0, #1
	b	.L1942
.L1944:
	ldr	r3, .L2005+12
	ldr	r6, [r3, #3840]
	cmp	r6, #0
	movne	r6, #2
	moveq	r6, #1
.L1943:
	ldr	r3, .L2005
	movw	r0, #65535
	ldr	r2, .L2005+16
	ldrh	r1, [r3, r2]
	cmp	r1, r0
	bne	.L1950
	cmp	r4, r1
	strneh	r4, [r3, r2]	@ movhi
	bne	.L1952
	ldr	r2, .L2005+8
	ldrh	r1, [r3, r2]
	cmp	r1, r4
	beq	.L1952
	ldr	r0, [r3, #-2016]
	mov	r1, r1, asl #1
	ldrh	r1, [r0, r1]
	cmp	r1, #0
	mvneq	r1, #0
	streqh	r1, [r3, r2]	@ movhi
	ldr	r3, .L2005
	ldr	r1, .L2005+16
	ldrh	r0, [r3, r2]
	strh	r0, [r3, r1]	@ movhi
	mvn	r1, #0
	strh	r1, [r3, r2]	@ movhi
.L1952:
	ldr	r5, .L2005
	mov	r3, #0
	ldr	r7, .L2005+16
	strb	r3, [r5, #-1752]
	movw	r3, #65535
	ldrh	r0, [r5, r7]
	cmp	r0, r3
	beq	.L1950
	bl	IsBlkInGcList
	cmp	r0, #0
	mvnne	r3, #0
	strneh	r3, [r5, r7]	@ movhi
	ldr	r3, .L2005+12
	ldr	r7, .L2005
	ldrb	r3, [r3, #928]	@ zero_extendqisi2
	cmp	r3, #0
	beq	.L1955
	ldr	r3, .L2005+16
	ldrh	r0, [r7, r3]
	bl	ftl_get_blk_mode
	strb	r0, [r7, #-1752]
.L1955:
	ldr	r8, .L2005+16
	movw	r3, #65535
	ldr	r5, .L2005
	ldrh	r2, [r7, r8]
	cmp	r2, r3
	beq	.L1950
	add	r0, r5, r8
	bl	make_superblock
	ldr	r1, .L2005+52
	movw	r2, #434
	mov	r3, #0
	strh	r3, [r5, r2]	@ movhi
	strh	r3, [r5, r1]	@ movhi
	strb	r3, [r5, #-1754]
	ldrh	r3, [r5, r8]
	ldr	r2, [r5, #-2016]
	mov	r3, r3, asl #1
	ldrh	r2, [r2, r3]
	mov	r3, #436
	strh	r2, [r5, r3]	@ movhi
.L1950:
	ldr	r3, .L2005
	ldr	r2, .L2005+16
	ldrh	r1, [r3, r2]
	sub	r2, r2, #236
	ldrh	r2, [r3, r2]
	cmp	r2, r1
	mov	r2, r3
	beq	.L1956
	ldr	r0, .L2005+56
	ldrh	r3, [r3, r0]
	cmp	r3, r1
	movne	fp, r4
	bne	.L2000
.L1957:
.L1956:
	ldr	r3, .L2005+16
	mvn	r1, #0
	strh	r1, [r2, r3]	@ movhi
	mov	r3, #432
.L2004:
	ldrh	r0, [r2, r3]
	b	.L1942
.L2000:
	ldr	r4, .L2005
	movw	sl, #65535
	ldr	r3, .L2005+16
	ldrh	r3, [r4, r3]
	cmp	r3, sl
	bne	.L1959
	ldr	r5, .L2005+28
	mov	r3, #0
	str	r3, [r4, #-1184]
.L1999:
	ldrh	r8, [r4, r5]
	mov	r0, r8
	bl	List_get_gc_head_node
	ldr	r3, .L2005+16
	movw	r1, #65535
	uxth	r7, r0
	strh	r7, [r4, r3]	@ movhi
	cmp	r7, r1
	bne	.L1961
	ldr	r2, .L2005
	add	r3, r3, #572
	mov	r1, #0
	mov	r0, #8
	strh	r1, [r2, r3]	@ movhi
	b	.L1942
.L1961:
	mov	r0, r7
	bl	IsBlkInGcList
	cmp	r0, #0
	add	r0, r8, #1
	strneh	r0, [r4, r5]	@ movhi
	bne	.L1999
	ldr	ip, .L2005+12
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
	bgt	.L1964
	cmp	r0, #48
	bls	.L1965
	cmp	r1, #8
	bls	.L1965
	ldr	r1, .L2005+60
	ldrh	r1, [r4, r1]
	cmp	r1, #35
	bhi	.L1965
.L1964:
	mov	r1, #0
	strh	r1, [r4, r5]	@ movhi
.L1965:
	ldrh	r3, [r2, r3]
	movw	r2, #65535
	ldr	sl, .L2005
	cmp	r3, r8
	cmpge	fp, r2
	movne	r8, #0
	moveq	r8, #1
	bne	.L1966
	ldr	r3, .L2005+16
	mvn	r2, #0
	strh	r2, [sl, r3]	@ movhi
	add	r3, r3, #572
	mov	r2, #0
	strh	r2, [sl, r3]	@ movhi
	mov	r3, #432
	ldrh	r0, [sl, r3]
	b	.L1942
.L1966:
	cmp	r3, #0
	bne	.L1967
	movw	r0, #65535
	bl	decrement_vpc_count
	ldrh	r3, [r4, r5]
	add	r3, r3, #1
	strh	r3, [r4, r5]	@ movhi
	b	.L1999
.L1967:
	ldr	r3, .L2005+12
	strb	r8, [sl, #-1752]
	ldrb	r3, [r3, #928]	@ zero_extendqisi2
	cmp	r3, #0
	beq	.L1968
	mov	r0, r7
	bl	ftl_get_blk_mode
	strb	r0, [sl, #-1752]
.L1968:
	ldr	r4, .L2005
	sub	r0, r4, #1760
	bl	make_superblock
	movw	r1, #434
	mov	r3, #0
	strh	r3, [r4, r1]	@ movhi
	ldr	r1, .L2005+16
	ldr	r0, [r4, #-2016]
	ldrh	r1, [r4, r1]
	mov	r1, r1, asl #1
	ldrh	r0, [r0, r1]
	mov	r1, #436
	strb	r3, [r4, #-1754]
	strh	r0, [r4, r1]	@ movhi
	ldr	r1, .L2005+52
	strh	r3, [r4, r1]	@ movhi
.L1959:
	ldr	sl, [sp, #16]
	cmp	sl, #1
	bne	.L1969
	bl	FtlReadRefresh
.L1969:
	ldr	r3, .L2005
	mov	r2, #1
	movw	r1, #3916
	str	r2, [r3, #-996]
	ldr	r2, .L2005+12
	ldrh	r9, [r2, r1]
	ldrb	r1, [r2, #928]	@ zero_extendqisi2
	cmp	r1, #0
	beq	.L1970
	ldrb	r1, [r3, #-1752]	@ zero_extendqisi2
	cmp	r1, #1
	movweq	r1, #3918
	ldreqh	r9, [r2, r1]
.L1970:
	ldr	r2, .L2005+52
	mov	r8, #0
	str	fp, [sp, #20]
	mov	fp, r9
	ldr	r4, .L2005
	ldrh	r3, [r3, r2]
	add	r2, r3, r6
	cmp	r2, r9
	rsbgt	r6, r3, r9
	uxthgt	r6, r6
	b	.L1972
.L1974:
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
.L1980:
	cmp	r3, r5
	bne	.L1974
	ldr	r0, [r4, #-1164]
	mov	r1, r7
	ldrb	r2, [r4, #-1752]	@ zero_extendqisi2
	mov	r5, #0
	bl	FlashReadPages
	mov	r9, r5
	mov	sl, r6
	b	.L1975
.L1978:
	ldr	r3, [r4, #-1164]
	add	r2, r3, r5
	ldr	r3, [r3, r5]
	ldr	r6, [r2, #12]
	cmn	r3, #1
	beq	.L1976
	ldrh	r3, [r6, #0]
	movw	r1, #61589
	cmp	r3, r1
	bne	.L1976
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
	bne	.L1976
	movw	r3, #434
	ldr	r0, [r4, #-1180]
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
	ldr	r1, [r4, #-1180]
	str	r0, [r2, #4]
	ldr	r3, [sp, #12]
	ldr	r2, [r4, #-968]
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
	ldr	r3, .L2005+44
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
	ldr	r3, .L2005+12
	ldrb	r3, [r3, #928]	@ zero_extendqisi2
	cmp	r3, #0
	bne	.L1977
	ldrb	r3, [r4, #-1893]	@ zero_extendqisi2
	ldr	r2, [r4, #-1180]
	cmp	r2, r3
	beq	.L1977
	ldr	r3, .L2005+64
	ldrh	r3, [r4, r3]
	cmp	r3, #0
	bne	.L1976
.L1977:
	bl	Ftl_gc_temp_data_write_back
	cmp	r0, #0
	ldrne	r3, .L2005
	movne	r2, #0
	strne	r2, [r3, #-996]
	movne	r2, #432
	bne	.L2004
.L1976:
	add	r9, r9, #1
	add	r5, r5, #36
	uxth	r9, r9
.L1975:
	cmp	r9, r7
	bne	.L1978
	add	r8, r8, #1
	mov	r6, sl
.L1972:
	uxth	r3, r8
	ldr	r7, .L2005
	cmp	r3, r6
	ldr	r3, .L2005+52
	bcs	.L1979
	ldr	r1, .L2005+12
	movw	r2, #3848
	ldrh	ip, [r4, r3]
	mov	r7, #0
	ldr	lr, [r4, #-1164]
	mov	r3, r7
	ldrh	r5, [r1, r2]
	add	ip, ip, r8
	ldr	r2, .L2005+68
	mov	r0, #36
	b	.L1980
.L1979:
	ldrh	r2, [r7, r3]
	mov	r9, fp
	ldr	fp, [sp, #20]
	add	r6, r6, r2
	uxth	r6, r6
	strh	r6, [r7, r3]	@ movhi
	cmp	r6, r9
	bcc	.L1981
	ldr	r3, [r7, #-1180]
	cmp	r3, #0
	beq	.L1982
	bl	Ftl_gc_temp_data_write_back
	cmp	r0, #0
	movne	r3, #0
	strne	r3, [r7, #-996]
	movne	r3, #432
	ldrneh	r0, [r7, r3]
	bne	.L1942
.L1982:
	ldr	r3, .L2005
	movw	r2, #434
	ldrh	ip, [r3, r2]
	cmp	ip, #0
	bne	.L1983
	ldr	r2, .L2005+16
	ldr	r0, [r3, #-2016]
	ldrh	r1, [r3, r2]
	mov	r1, r1, asl #1
	ldrh	r4, [r0, r1]
	cmp	r4, #0
	beq	.L1983
	strh	ip, [r0, r1]	@ movhi
	ldrh	r0, [r3, r2]
	bl	update_vpc_list
	bl	FtlCacheWriteBack
	bl	l2p_flush
	bl	FtlVpcTblFlush
.L1983:
	ldr	r3, .L2005+16
	mvn	r1, #0
	ldr	r2, .L2005
	strh	r1, [r2, r3]	@ movhi
.L1981:
	ldr	r3, .L2005
	ldr	r2, .L2005+20
	ldrh	r2, [r3, r2]
	cmp	r2, #2
	ldrls	r2, .L2005+12
	movwls	r3, #3916
	ldrlsh	r6, [r2, r3]
	bls	.L2000
.L1984:
	mov	r1, #0
	str	r1, [r3, #-996]
	mov	r1, #432
	ldrh	r0, [r3, r1]
	cmp	r0, #0
	addeq	r0, r2, #1
.L1942:
	add	sp, sp, #36
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, sl, fp, pc}
.L2006:
	.align	2
.L2005:
	.word	.LANCHOR2
	.word	-1140
	.word	-1142
	.word	.LANCHOR0
	.word	-1760
	.word	-2000
	.word	-1194
	.word	-1188
	.word	.LC130
	.word	-1196
	.word	.LC131
	.word	-1900
	.word	-1776
	.word	-1758
	.word	-1948
	.word	-1156
	.word	-1896
	.word	.LANCHOR2-1746
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
	ldr	r3, .L2014
	mov	r5, r1
	ldr	r0, [r3, #-2088]
	cmp	r0, #0
	movne	r0, #0
	ldmnefd	sp!, {r3, r4, r5, pc}
	ldr	r2, [r3, #-996]
	cmp	r2, #0
	ldmnefd	sp!, {r3, r4, r5, pc}
	ldr	r1, .L2014+4
	ldrh	r1, [r3, r1]
	cmp	r1, #47
	movls	r0, r2
	ldmlsfd	sp!, {r3, r4, r5, pc}
	ldr	r1, .L2014+8
	movw	r2, #3000
	ldrh	r1, [r1, r2]
	movw	r2, #65535
	cmp	r1, r2
	beq	.L2009
	ldr	r1, .L2014+12
	ldrh	r3, [r3, r1]
	cmp	r3, r2
	beq	.L2009
	mov	r0, #1
	bl	FtlGcFreeTempBlock
	cmp	r0, #0
	bne	.L2013
.L2009:
	mov	r0, r4
	mov	r1, r5
	ldmfd	sp!, {r3, r4, r5, lr}
	b	ftl_do_gc.part.21
.L2013:
	mov	r0, #1
	ldmfd	sp!, {r3, r4, r5, pc}
.L2015:
	.align	2
.L2014:
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
	ldr	r3, .L2038
	stmfd	sp!, {r0, r1, r4, r5, r6, r7, r8, r9, sl, lr}
	.save {r0, r1, r4, r5, r6, r7, r8, r9, sl, lr}
	ldr	r4, [r3, #440]
	ldr	r3, [r3, #-2088]
	cmp	r3, #0
	bne	.L2017
	ldr	r3, .L2038+4
	ldr	r1, [r3, #3968]
	cmp	r1, #0
	beq	.L2017
	ldrb	r8, [r3, #928]	@ zero_extendqisi2
	cmp	r8, #0
	beq	.L2018
	ldrb	r8, [r4, #8]	@ zero_extendqisi2
	sub	lr, r8, #1
	rsbs	r8, lr, #0
	adc	r8, r8, lr
.L2018:
	ldr	r0, [r3, #3972]
	mov	r2, r8
	ldrb	r3, [r4, #9]	@ zero_extendqisi2
	mov	r5, #0
	bl	FlashProgPages
	mov	r7, r5
	ldr	r6, .L2038+4
	ldr	r9, .L2038
	b	.L2019
.L2024:
	ldr	r2, [r6, #3972]
	add	r3, r2, r5
	ldr	r2, [r2, r5]
	cmn	r2, #1
	beq	.L2037
	ldr	r2, [r3, #4]
	cmp	r8, #0
	ldr	r0, [r3, #16]
	add	r1, sp, #4
	str	r2, [sp, #4]
	orrne	r2, r2, #-2147483648
	strne	r2, [sp, #4]
	mov	r2, #1
	bl	log2phys
	ldr	r3, [r6, #3972]
	add	r3, r3, r5
	ldr	r3, [r3, #12]
	ldr	r0, [r3, #12]
	cmn	r0, #1
	beq	.L2022
	ubfx	r0, r0, #10, #16
	bl	P2V_block_in_plane
	ldr	r2, [r9, #-2016]
	mov	r3, r0, asl #1
	mov	sl, r0
	ldrh	r2, [r2, r3]
	cmp	r2, #0
	bne	.L2023
	ldr	r0, .L2038+8
	mov	r1, sl
	bl	printk
.L2023:
	mov	r0, sl
	bl	decrement_vpc_count
.L2022:
	add	r7, r7, #1
	add	r5, r5, #36
.L2019:
	ldr	r3, [r6, #3968]
	cmp	r7, r3
	bcc	.L2024
	b	.L2025
.L2033:
	ldr	r3, [r6, #3972]
	mvn	r2, #0
	movw	sl, #3916
	str	r2, [r3, r5]
	b	.L2026
.L2029:
	ldr	r0, [r3, #4]
	ubfx	r0, r0, #10, #16
	bl	P2V_block_in_plane
	ldrh	r3, [r4, #0]
	cmp	r3, r0
	bne	.L2027
	ldr	r2, [r7, #-2016]
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
.L2027:
	ldrh	r3, [r4, #4]
	cmp	r3, #0
	bne	.L2028
	mov	r0, r4
	bl	allocate_new_data_superblock
.L2028:
	ldr	r3, [r7, #-1612]
	add	r3, r3, #1
	str	r3, [r7, #-1612]
	ldr	r3, [r6, #3972]
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
	ldr	r0, [r6, #3972]
	add	r0, r0, r5
	str	r3, [r0, #4]
	ldrb	r3, [r4, #9]	@ zero_extendqisi2
	bl	FlashProgPages
	ldr	r3, [r7, #-2088]
	cmp	r3, #0
	bne	.L2017
.L2026:
	ldr	r2, [r6, #3972]
	add	r3, r2, r5
	ldr	r2, [r2, r5]
	cmn	r2, #1
	beq	.L2029
	ldr	r2, [r3, #4]
	cmp	r8, #0
	ldr	r0, [r3, #16]
	add	r1, sp, #4
	str	r2, [sp, #4]
	orrne	r2, r2, #-2147483648
	strne	r2, [sp, #4]
	mov	r2, #1
	bl	log2phys
	ldr	r3, [r6, #3972]
	add	r3, r3, r5
	ldr	r3, [r3, #12]
	ldr	r0, [r3, #12]
	cmn	r0, #1
	beq	.L2031
	ubfx	r0, r0, #10, #16
	bl	P2V_block_in_plane
	ldr	r2, [r7, #-2016]
	mov	r3, r0, asl #1
	mov	sl, r0
	ldrh	r2, [r2, r3]
	cmp	r2, #0
	bne	.L2032
	ldr	r0, .L2038+8
	mov	r1, sl
	bl	printk
.L2032:
	mov	r0, sl
	bl	decrement_vpc_count
.L2031:
	add	r9, r9, #1
	add	r5, r5, #36
	b	.L2020
.L2037:
	ldr	r6, .L2038+4
	mov	r5, #0
	ldr	r7, .L2038
	mov	r9, r5
.L2020:
	ldr	r3, [r6, #3968]
	cmp	r9, r3
	bcc	.L2033
	movw	r4, #16386
	ldr	r6, .L2038
	ldr	r5, .L2038+12
	b	.L2034
.L2035:
	mov	r0, #1
	mov	r1, r0
	bl	ftl_do_gc
	subs	r4, r4, #1
	beq	.L2025
.L2034:
	ldrh	r3, [r6, r5]
	cmp	r3, #0
	bne	.L2035
.L2025:
	ldr	r3, .L2038+4
	mov	r2, #0
	str	r2, [r3, #3968]
.L2017:
	mov	r0, #0
	ldmfd	sp!, {r2, r3, r4, r5, r6, r7, r8, r9, sl, pc}
.L2039:
	.align	2
.L2038:
	.word	.LANCHOR2
	.word	.LANCHOR0
	.word	.LC132
	.word	-1138
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
	movw	r3, #3916
	ldr	r2, .L2063
	ldrh	r1, [r2, r3]
	ldr	r3, .L2063+4
	ldr	r2, [r3, #-2088]
	cmp	r2, #0
	bne	.L2061
	ldr	ip, .L2063+8
	ldrh	r4, [r3, ip]
	movw	ip, #65535
	cmp	r4, ip
	beq	.L2042
	cmp	r0, #0
	beq	.L2043
	ldr	lr, .L2063+12
	movw	r0, #3000
	ldrh	r5, [lr, r0]
	cmp	r5, ip
	movne	r1, #2
	bne	.L2043
	strh	r2, [lr, r0]	@ movhi
	sub	r2, r2, #2000
	ldrh	r3, [r3, r2]
	cmp	r3, #17
	movhi	r1, #2
.L2043:
	ldr	r0, .L2063+16
	bl	FtlGcScanTempBlk
	cmn	r0, #1
	str	r0, [sp, #4]
	beq	.L2044
	ldr	r3, .L2063+4
	mov	r4, r4, asl #1
	ldr	r3, [r3, #-2080]
	ldrh	r2, [r3, r4]
	cmp	r2, #4
	bls	.L2045
	sub	r2, r2, #5
	mov	r0, #1
	strh	r2, [r3, r4]	@ movhi
	bl	FtlEctTblFlush
.L2045:
	ldr	r3, .L2063+4
	ldr	r2, [r3, #-1012]
	cmp	r2, #0
	bne	.L2046
	ldr	r2, [r3, #-1612]
	ldr	r0, [sp, #4]
	add	r2, r2, #1
	str	r2, [r3, #-1612]
	ubfx	r0, r0, #10, #16
	bl	FtlBbmMapBadBlock
	bl	FtlBbmTblFlush
.L2046:
	ldr	r3, .L2063+4
	mov	r2, #0
	str	r2, [r3, #-1012]
	b	.L2059
.L2044:
	ldr	r2, .L2063+12
	movw	r3, #3000
	ldrh	r2, [r2, r3]
	movw	r3, #65535
	cmp	r2, r3
	bne	.L2059
.L2042:
	ldr	r6, .L2063+4
	movw	r3, #65535
	ldr	r5, .L2063+8
	mov	r4, #0
	str	r4, [r6, #-1012]
	ldrh	r2, [r6, r5]
	cmp	r2, r3
	moveq	r0, r4
	beq	.L2041
	bl	FtlCacheWriteBack
	ldr	ip, .L2063
	movw	r0, #3916
	ldrb	r1, [r6, #-1893]	@ zero_extendqisi2
	ldrh	r3, [r6, r5]
	ldrh	r0, [ip, r0]
	ldr	r2, [r6, #-2016]
	ldr	sl, .L2063+20
	mov	r3, r3, asl #1
	mul	r1, r0, r1
	strh	r1, [r2, r3]	@ movhi
	ldr	r3, [r6, #-1816]
	ldrh	r2, [r6, sl]
	add	r3, r2, r3
	str	r3, [r6, #-1816]
	b	.L2047
.L2050:
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
	bne	.L2048
	ubfx	r0, r0, #10, #16
	bl	P2V_block_in_plane
	add	r1, r5, #4
	mov	r2, #1
	mov	r7, r0
	ldr	r0, [r5, #8]
	bl	log2phys
	mov	r0, r7
	b	.L2062
.L2048:
	ldr	r3, [r5, #4]
	cmp	r0, r3
	beq	.L2049
	ldr	r3, .L2063+8
	ldrh	r0, [r6, r3]
.L2062:
	bl	decrement_vpc_count
.L2049:
	add	r4, r4, #1
	uxth	r4, r4
.L2047:
	ldrh	r3, [r6, sl]
	ldr	r5, .L2063+4
	cmp	r3, r4
	bhi	.L2050
	movw	r0, #65535
	ldr	r4, .L2063+8
	bl	decrement_vpc_count
	ldr	r3, .L2063
	ldrb	r3, [r3, #928]	@ zero_extendqisi2
	cmp	r3, #0
	beq	.L2051
	ldr	r0, .L2063+24
	ldrh	r1, [r5, r4]
	bl	printk
.L2051:
	ldrh	r0, [r5, r4]
	ldr	r2, [r5, #-2016]
	mov	r3, r0, asl #1
	ldrh	r3, [r2, r3]
	cmp	r3, #0
	beq	.L2052
	bl	INSERT_DATA_LIST
	b	.L2053
.L2052:
	bl	INSERT_FREE_LIST
.L2053:
	ldr	r4, .L2063+4
	mvn	r2, #0
	ldr	r3, .L2063+8
	strh	r2, [r4, r3]	@ movhi
	mov	r3, #0
	ldr	r2, .L2063+20
	strh	r3, [r4, r2]	@ movhi
	sub	r2, r2, #12
	strh	r3, [r4, r2]	@ movhi
	bl	l2p_flush
	bl	FtlVpcTblFlush
	ldr	r3, .L2063
	mov	r1, r4
	ldr	r3, [r3, #3840]
	cmp	r3, #0
	beq	.L2054
	ldr	r3, [r4, #-1780]
	cmp	r3, #29
	bhi	.L2054
	ldr	r3, .L2063+28
	mvn	r1, #0
	ldr	r2, .L2063+32
	ldrh	r3, [r4, r3]
	ldrh	r2, [r4, r2]
	cmp	r2, r3
	movcc	r3, r3, asl #1
	ldrcc	r2, .L2063+36
	strcch	r3, [r4, r2]	@ movhi
	ldr	r3, .L2063+40
	ldr	r2, .L2063+4
	strh	r1, [r2, r3]	@ movhi
	b	.L2061
.L2054:
	ldr	r2, .L2063+28
	ldr	r0, .L2063+32
	ldr	r3, .L2063+4
	ldrh	r2, [r1, r2]
	ldrh	r1, [r1, r0]
	add	r0, r2, r2, asl #1
	cmp	r1, r0, lsr #2
	ble	.L2061
	ldr	r1, .L2063+40
	mvn	r0, #0
	strh	r0, [r3, r1]	@ movhi
	ldr	r1, .L2063
	ldrb	r0, [r1, #928]	@ zero_extendqisi2
	ldr	r1, .L2063+36
	cmp	r0, #0
	subne	r2, r2, #2
	moveq	r2, #20
	strneh	r2, [r3, r1]	@ movhi
	streqh	r2, [r3, r1]	@ movhi
	beq	.L2041
	b	.L2061
.L2059:
	mov	r0, #1
	b	.L2041
.L2061:
	mov	r0, #0
.L2041:
	ldmfd	sp!, {r1, r2, r3, r4, r5, r6, r7, r8, sl, pc}
.L2064:
	.align	2
.L2063:
	.word	.LANCHOR0
	.word	.LANCHOR2
	.word	-1900
	.word	.LANCHOR1
	.word	.LANCHOR2-1900
	.word	-1144
	.word	.LC133
	.word	-1776
	.word	-2000
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
	ldr	r3, .L2074
	ldrb	r3, [r3, #928]	@ zero_extendqisi2
	cmp	r3, #0
	ldr	r3, .L2074+4
	beq	.L2066
	ldr	r2, [r3, #-1180]
	tst	r2, #1
	beq	.L2066
	ldr	r2, .L2074+8
	ldrh	r2, [r3, r2]
	cmp	r2, #0
	bne	.L2071
.L2066:
	mov	r2, #0
	ldr	r0, [r3, #-968]
	ldr	r1, [r3, #-1180]
	mov	r3, r2
	bl	FlashProgPages
	mov	r4, #0
	ldr	r5, .L2074+4
	mov	r6, #36
	b	.L2068
.L2070:
	mul	r3, r6, r4
	ldr	r1, [r5, #-968]
	add	r2, r1, r3
	ldr	r1, [r1, r3]
	cmn	r1, #1
	bne	.L2069
	ldr	r2, .L2074+12
	mov	lr, #0
	ldr	ip, [r7, #-2016]
	ldrh	r0, [r7, r2]
	mov	r0, r0, asl #1
	strh	lr, [ip, r0]	@ movhi
	strh	r1, [r7, r2]	@ movhi
	ldr	r2, [r7, #-1612]
	add	r2, r2, #1
	str	r2, [r7, #-1612]
	ldr	r2, [r7, #-968]
	add	r3, r2, r3
	ldr	r0, [r3, #4]
	ubfx	r0, r0, #10, #16
	bl	FtlBbmMapBadBlock
	bl	FtlBbmTblFlush
	bl	FtlGcPageVarInit
	b	.L2073
.L2069:
	ldr	r3, [r2, #12]
	add	r4, r4, #1
	ldr	r1, [r2, #4]
	uxth	r4, r4
	ldr	r0, [r3, #12]
	ldr	r2, [r3, #8]
	bl	FtlGcUpdatePage
.L2068:
	ldr	r1, [r5, #-1180]
	ldr	r7, .L2074+4
	cmp	r4, r1
	bcc	.L2070
	ldr	r0, [r7, #-968]
	bl	FtlGcBufFree
	ldr	r3, .L2074+8
	mov	r0, #0
	str	r0, [r7, #-1180]
	ldrh	r3, [r7, r3]
	cmp	r3, r0
	ldmnefd	sp!, {r3, r4, r5, r6, r7, pc}
	mov	r0, #1
	bl	FtlGcFreeTempBlock
.L2073:
	mov	r0, #1
	ldmfd	sp!, {r3, r4, r5, r6, r7, pc}
.L2071:
	mov	r0, #0
	ldmfd	sp!, {r3, r4, r5, r6, r7, pc}
.L2075:
	.align	2
.L2074:
	.word	.LANCHOR0
	.word	.LANCHOR2
	.word	-1896
	.word	-1900
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
	ldr	r6, .L2078
	ldr	r0, .L2078+4
	ldr	r4, .L2078+8
	ldrh	r1, [r6, r5]
	bl	FtlGcScanTempBlk
	ldr	r3, .L2078+12
	ldrh	r2, [r4, r3]
	ldrh	r3, [r6, r5]
	cmp	r2, r3
	ldmccfd	sp!, {r4, r5, r6, pc}
	sub	r0, r4, #1088
	bl	FtlMapBlkWriteDumpData
	mov	r0, #0
	bl	FtlGcFreeTempBlock
	mov	r3, #0
	str	r3, [r4, #-1012]
	ldmfd	sp!, {r4, r5, r6, pc}
.L2079:
	.align	2
.L2078:
	.word	.LANCHOR0
	.word	.LANCHOR2-1900
	.word	.LANCHOR2
	.word	-1898
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
	ldr	r3, .L2081
	ldr	r5, .L2081+4
	str	r4, [r3, #300]
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
.L2082:
	.align	2
.L2081:
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
	mov	r3, #296
	ldr	r6, .L2094
	mov	r2, #0
	ldr	r4, .L2094+4
	mvn	r5, #0
	ldr	r0, [r6, #3852]
	strh	r2, [r4, r3]	@ movhi
	movw	r3, #294
	strh	r5, [r4, r3]	@ movhi
	uxth	r0, r0
	bl	FtlFreeSysBlkQueueInit
	bl	FtlScanSysBlk
	ldr	r3, .L2094+8
	ldrh	r2, [r4, r3]
	movw	r3, #65535
	cmp	r2, r3
	beq	.L2084
	bl	FtlLoadSysInfo
	subs	r7, r0, #0
	bne	.L2084
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
	ldrh	r2, [r6, r3]
	mov	r3, r7
	ldr	r1, [r4, #-1852]
	b	.L2085
.L2087:
	add	r0, r1, r7
	add	r7, r7, #12
	ldr	r0, [r0, #4]
	cmp	r0, #0
	blt	.L2086
	add	r3, r3, #1
.L2085:
	cmp	r3, r2
	blt	.L2087
.L2086:
	ldr	r4, .L2094+4
	cmp	r3, r2
	ldr	r1, .L2094+12
	ldrh	r0, [r4, r1]
	add	r0, r0, #1
	strh	r0, [r4, r1]	@ movhi
	blt	.L2088
	mov	r3, #296
	ldrh	r3, [r4, r3]
	cmp	r3, #0
	beq	.L2089
.L2088:
	ldr	r0, .L2094+16
	bl	FtlSuperblockPowerLostFix
	ldr	r0, .L2094+20
	bl	FtlSuperblockPowerLostFix
	ldr	r3, .L2094+24
	ldr	r1, [r4, #-2016]
	ldr	r2, .L2094+28
	ldrh	r3, [r4, r3]
	ldrh	r0, [r4, r2]
	mov	r3, r3, asl #1
	ldrh	ip, [r1, r3]
	rsb	r0, r0, ip
	strh	r0, [r1, r3]	@ movhi
	ldr	r0, .L2094
	movw	r1, #3916
	ldr	r3, .L2094+32
	ldr	lr, [r4, #-2016]
	ldrh	ip, [r0, r1]
	strh	ip, [r4, r3]	@ movhi
	mov	r3, #0
	strh	r3, [r4, r2]	@ movhi
	add	r2, r2, #44
	strb	r3, [r4, #-1990]
	ldrh	ip, [r4, r2]
	add	r2, r2, #4
	ldrh	r5, [r4, r2]
	mov	ip, ip, asl #1
	ldrh	r6, [lr, ip]
	rsb	r5, r5, r6
	strh	r5, [lr, ip]	@ movhi
	ldrh	r0, [r0, r1]
	ldr	r1, .L2094+36
	strh	r3, [r4, r2]	@ movhi
	strb	r3, [r4, #-1942]
	strh	r0, [r4, r1]	@ movhi
	ldr	r0, .L2094+40
	bl	FtlMapBlkWriteDumpData
	ldr	r0, .L2094+44
	bl	FtlMapBlkWriteDumpData
	ldr	r3, .L2094+48
	ldrh	r2, [r4, r3]
	add	r2, r2, #1
	strh	r2, [r4, r3]	@ movhi
	bl	l2p_flush
	bl	FtlVpcTblFlush
	bl	FtlVpcTblFlush
.L2089:
	ldr	r4, .L2094+4
	movw	r3, #65535
	ldr	r5, .L2094+24
	ldrh	r2, [r4, r5]
	cmp	r2, r3
	beq	.L2090
	ldr	r3, .L2094+28
	ldrh	r3, [r4, r3]
	cmp	r3, #0
	bne	.L2090
	ldr	r3, .L2094+52
	ldrh	r3, [r4, r3]
	cmp	r3, #0
	bne	.L2090
	bl	FtlVpcTblFlush
	ldrh	r0, [r4, r5]
	bl	FtlGcRefreshOpenBlock
	ldr	r3, .L2094+56
	ldrh	r0, [r4, r3]
	bl	FtlGcRefreshOpenBlock
	ldr	r0, .L2094+16
	bl	allocate_new_data_superblock
	ldr	r0, .L2094+20
	bl	allocate_new_data_superblock
	add	r0, r4, #248
	bl	FtlMapBlkWriteDumpData
.L2090:
	ldr	r3, .L2094+12
	ldr	r2, .L2094+4
	ldrh	r5, [r2, r3]
	ands	r5, r5, #31
	movne	r5, #0
	bne	.L2084
	bl	FtlVpcCheckAndModify
.L2084:
	mov	r0, r5
	ldmfd	sp!, {r3, r4, r5, r6, r7, pc}
.L2095:
	.align	2
.L2094:
	.word	.LANCHOR0
	.word	.LANCHOR2
	.word	-1772
	.word	-2044
	.word	.LANCHOR2-1996
	.word	.LANCHOR2-1948
	.word	-1996
	.word	-1992
	.word	-1994
	.word	-1946
	.word	.LANCHOR2-1088
	.word	.LANCHOR2+248
	.word	-2042
	.word	-1944
	.word	-1948
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
	ldr	r3, .L2099
	ldr	r5, .L2099+4
	ldrh	r2, [r5, r3]
	movw	r3, #65535
	cmp	r2, r3
	beq	.L2097
	ldr	r3, .L2099+8
	ldrh	r3, [r5, r3]
	cmp	r3, #0
	bne	.L2098
.L2097:
	bl	FtlCacheWriteBack
	mov	r0, #0
	bl	FtlGcFreeTempBlock
	ldr	r0, .L2099+12
	mov	r4, #0
	strb	r4, [r5, #-1892]
	bl	allocate_data_superblock
	ldr	r3, .L2099+16
	strh	r4, [r5, r3]	@ movhi
	add	r3, r3, #12
	strh	r4, [r5, r3]	@ movhi
	bl	l2p_flush
	mov	r0, r4
	bl	FtlEctTblFlush
	bl	FtlVpcTblFlush
.L2098:
	ldr	r0, .L2099+12
	ldmfd	sp!, {r3, r4, r5, lr}
	b	get_new_active_ppa
.L2100:
	.align	2
.L2099:
	.word	-1900
	.word	.LANCHOR2
	.word	-1896
	.word	.LANCHOR2-1900
	.word	-1156
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
	ldr	r5, .L2111
	mov	r7, r0
	mov	r4, r1
	ldr	r3, [r5, #3956]
	cmp	r2, r3
	mvnhi	r0, #0
	bhi	.L2102
	cmp	r1, #31
	bls	.L2109
	ldr	r3, .L2111+4
	ldr	r3, [r3, #-2088]
	cmp	r3, #0
	bne	.L2109
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
	beq	.L2103
	rsb	r6, r7, r6
	add	r5, r0, #1
	cmp	r6, r4
	movcs	r6, r4
	uxth	r6, r6
	rsb	r4, r6, r4
.L2103:
	mvn	r3, #0
	ldr	r7, .L2111
	str	r3, [sp, #4]
	movw	r6, #3922
	b	.L2104
.L2106:
	mov	r0, r5
	mov	r1, sp
	mov	r2, #0
	bl	log2phys
	ldr	r3, [sp, #0]
	cmn	r3, #1
	beq	.L2105
	ldr	r3, .L2111+4
	add	r1, sp, #4
	mov	r0, r5
	ldr	r2, [r3, #444]
	add	r2, r2, #1
	str	r2, [r3, #444]
	ldr	r2, [r3, #-1832]
	add	r2, r2, #1
	str	r2, [r3, #-1832]
	mov	r2, #1
	bl	log2phys
	ldr	r0, [sp, #0]
	ubfx	r0, r0, #10, #16
	bl	P2V_block_in_plane
	bl	decrement_vpc_count
.L2105:
	ldrh	r3, [r7, r6]
	add	r5, r5, #1
	rsb	r4, r3, r4
.L2104:
	ldrh	r3, [r7, r6]
	cmp	r4, r3
	bcs	.L2106
	ldr	r3, .L2111+4
	mov	r4, #0
	ldr	r2, [r3, #444]
	cmp	r2, #32
	bls	.L2110
	str	r4, [r3, #444]
	bl	l2p_flush
	bl	FtlVpcTblFlush
	b	.L2110
.L2109:
	mov	r0, #0
	b	.L2102
.L2110:
	mov	r0, r4
.L2102:
	ldmfd	sp!, {r1, r2, r3, r4, r5, r6, r7, pc}
.L2112:
	.align	2
.L2111:
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
	ldr	r3, .L2118
	ldr	r3, [r3, #-2088]
	cmp	r3, #0
	bne	.L2117
	bl	FtlCacheWriteBack
	bl	l2p_flush
	mov	r0, #1
	bl	FtlEctTblFlush
	bl	FtlVpcTblFlush
.L2117:
	mov	r0, #0
	ldmfd	sp!, {r3, pc}
.L2119:
	.align	2
.L2118:
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
	ldr	r3, .L2122
	ldr	r3, [r3, #2996]
	cmp	r3, #1
	bne	.L2121
	bl	FtlSysFlush
.L2121:
	mov	r0, #0
	ldmfd	sp!, {r3, pc}
.L2123:
	.align	2
.L2122:
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
	ldr	r0, .L2126
	bl	printk
	ldmfd	sp!, {r3, lr}
	b	ftl_deinit
.L2127:
	.align	2
.L2126:
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
	ldr	r3, .L2138
	stmfd	sp!, {r4, r5, r6, r7, r8, sl, lr}
	.save {r4, r5, r6, r7, r8, sl, lr}
	.pad #52
	sub	sp, sp, #52
	ldrb	r3, [r3, #928]	@ zero_extendqisi2
	cmp	r3, #0
	beq	.L2129
	ldr	r4, .L2138+4
	movw	r3, #294
	ldr	r0, .L2138+8
	movw	r7, #4097
	ldrh	r6, [r4, r3]
	ldr	r3, [r4, #-2016]
	mov	r5, r6, asl #1
	mov	r1, r6
	ldrh	r2, [r3, r5]
	bl	printk
	ldr	r3, .L2138+12
	ldrh	r0, [r4, r3]
	bl	FtlGcRefreshOpenBlock
	ldr	r3, .L2138+16
	ldrh	r0, [r4, r3]
	bl	FtlGcRefreshOpenBlock
	ldr	r0, .L2138+20
	bl	allocate_new_data_superblock
	ldr	r0, .L2138+24
	bl	allocate_new_data_superblock
	b	.L2131
.L2133:
	mov	r0, #1
	mov	r1, r0
	bl	ftl_do_gc
	ldr	r3, [r4, #-2016]
	ldrh	r3, [r3, r5]
	cmp	r3, #0
	beq	.L2132
.L2131:
	subs	r7, r7, #1
	bne	.L2133
.L2132:
	ldr	r7, .L2138+4
	mov	r1, r6
	ldr	r0, .L2138+8
	ldr	r3, [r7, #-2016]
	ldrh	r2, [r3, r5]
	bl	printk
	ldr	r3, [r7, #-2016]
	ldrh	r4, [r3, r5]
	cmp	r4, #0
	bne	.L2134
	add	r8, sp, #48
	mov	r0, sp
	strh	r6, [r8, #-48]!	@ movhi
	add	r8, r8, #14
	bl	make_superblock
	ldr	r2, .L2138
	movw	r3, #3848
	ldr	r7, [r7, #-2084]
	movw	lr, #65535
	mov	ip, #36
	ldrh	sl, [r2, r3]
	mov	r1, r4
	mov	r3, r4
	b	.L2135
.L2137:
	ldrh	r0, [r8, #2]!
	cmp	r0, lr
	beq	.L2136
	mla	r2, ip, r4, r7
	add	r4, r4, #1
	mov	r0, r0, asl #10
	uxth	r4, r4
	stmib	r2, {r0, r1}
	str	r1, [r2, #12]
.L2136:
	add	r3, r3, #1
	uxth	r3, r3
.L2135:
	cmp	r3, sl
	bne	.L2137
	ldr	r7, .L2138+4
	mov	r1, r6
	ldr	r0, .L2138+28
	ldr	r3, [r7, #-2016]
	ldrh	r2, [r3, r5]
	bl	printk
	mov	r1, #0
	mov	r2, r4
	ldr	r0, [r7, #-2084]
	bl	FlashEraseBlocks
	ldr	r0, [r7, #-2084]
	mov	r1, #1
	mov	r2, r4
	bl	FlashEraseBlocks
.L2134:
	ldr	r2, .L2138+4
	movw	r3, #294
	mvn	r1, #0
	strh	r1, [r2, r3]	@ movhi
.L2129:
	add	sp, sp, #52
	ldmfd	sp!, {r4, r5, r6, r7, r8, sl, pc}
.L2139:
	.align	2
.L2138:
	.word	.LANCHOR0
	.word	.LANCHOR2
	.word	.LC135
	.word	-1996
	.word	-1948
	.word	.LANCHOR2-1996
	.word	.LANCHOR2-1948
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
.L2141:
	stmfd	sp!, {r4, r5, r6, lr}
	.save {r4, r5, r6, lr}
	mvn	r3, #0
	ldr	r4, .L2151
	ldr	r6, .L2151+4
	ldr	r5, .L2151+8
	ldr	r1, .L2151+12
	str	r3, [r6, #2996]
	mov	r3, #0
	ldr	r0, .L2151+16
	str	r3, [r4, #448]
	str	r3, [r4, #-2088]
	bl	printk
	ldr	r0, .L2151+20
	bl	FtlConstantsInit
	bl	FtlMemInit
	bl	FtlVariablesInit
	ldr	r0, [r5, #3852]
	uxth	r0, r0
	bl	FtlFreeSysBlkQueueInit
	bl	FtlLoadBbt
	cmp	r0, #0
	ldrne	r0, .L2151+24
	bne	.L2150
	bl	FtlSysBlkInit
	cmp	r0, #0
	beq	.L2144
	ldr	r0, .L2151+28
.L2150:
	ldr	r1, .L2151+32
	bl	printk
	b	.L2143
.L2144:
	mov	r1, #1
	str	r1, [r6, #2996]
	bl	ftl_do_gc
	ldr	r3, .L2151+36
	ldrh	r3, [r4, r3]
	cmp	r3, #15
	bhi	.L2145
	mov	r4, #1024
.L2146:
	mov	r0, #1
	mov	r1, r0
	bl	ftl_do_gc
	subs	r4, r4, #1
	bne	.L2146
	b	.L2143
.L2145:
	ldrb	r3, [r5, #928]	@ zero_extendqisi2
	cmp	r3, #0
	beq	.L2143
	mov	r4, #128
.L2147:
	mov	r0, #1
	mov	r1, r0
	bl	ftl_do_gc
	subs	r4, r4, #1
	bne	.L2147
.L2143:
	mov	r0, #0
	ldmfd	sp!, {r4, r5, r6, pc}
.L2152:
	.align	2
.L2151:
	.word	.LANCHOR2
	.word	.LANCHOR1
	.word	.LANCHOR0
	.word	.LC79
	.word	.LC78
	.word	.LANCHOR0+3048
	.word	.LC137
	.word	.LC138
	.word	.LANCHOR3+36
	.word	-2000
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
	ldr	r4, .L2198
	.pad #100
	sub	sp, sp, #100
	ldr	r8, .L2198+4
	mov	r5, r1
	mov	r7, r0
	mov	r9, r2
	str	r3, [r4, #452]
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
	beq	.L2186
	ldr	r3, [r8, #3972]
	sub	sl, sl, #1
	mov	r2, #36
	ldr	r0, [sp, #16]
	mla	sl, r2, sl, r3
	ldr	r3, [sl, #16]
	cmp	r0, r3
	strne	r5, [sp, #32]
	bne	.L2155
	ldr	r3, [r4, #-1824]
	mov	r0, r7
	mov	r1, r6
	add	r3, r3, #1
	str	r3, [r4, #-1824]
	ldr	r3, [r4, #456]
	add	r3, r3, #1
	str	r3, [r4, #456]
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
	bne	.L2156
	ldr	r2, [r4, #456]
	cmp	r2, #2
	ble	.L2157
.L2156:
	add	r9, r9, r3
	add	r7, r7, r8
	ldr	r3, [sp, #16]
	rsb	r1, r8, r5
	str	fp, [sp, #20]
	add	r3, r3, #1
	str	r1, [sp, #32]
	str	r3, [sp, #16]
.L2155:
	ldr	r3, .L2198
	mov	r2, #0
	str	r2, [r3, #456]
	b	.L2154
.L2186:
	str	r5, [sp, #32]
.L2154:
	ldr	r0, [sp, #16]
	ldr	r1, [sp, #36]
	bl	FtlCacheMetchLpa
	cmp	r0, #0
	beq	.L2158
	bl	FtlCacheWriteBack
.L2158:
	cmp	r5, r6, asl #1
	ldr	r3, .L2198
	ldr	r4, .L2198+4
	mov	fp, #0
	movcc	r6, #0
	movcs	r6, #1
	str	r6, [sp, #24]
	ldr	r6, .L2198+8
	ldr	r5, [sp, #16]
	mov	sl, r4
	str	fp, [sp, #40]
	str	r6, [r3, #440]
	b	.L2195
.L2184:
	ldrh	r1, [r6, #4]
	cmp	r1, #0
	bne	.L2160
	ldr	r2, .L2198+8
	ldr	r3, .L2198+12
	cmp	r6, r2
	bne	.L2161
	ldr	r2, .L2198+16
	ldrh	r8, [r8, r2]
	cmp	r8, #0
	bne	.L2162
	ldr	r0, .L2198+20
	str	r3, [sp, #4]
	bl	allocate_new_data_superblock
	ldr	r3, [sp, #4]
	str	r8, [r3, #3004]
.L2162:
	ldr	r0, .L2198+8
	str	r3, [sp, #4]
	bl	allocate_new_data_superblock
	ldr	r3, [sp, #4]
	ldr	r2, [r3, #3004]
	ldr	r3, .L2198+20
	cmp	r2, #0
	movne	r6, r3
	b	.L2163
.L2161:
	str	r1, [r3, #3004]
	ldr	r3, .L2198+24
	ldrh	r3, [r8, r3]
	cmp	r3, #0
	movne	r6, r2
	bne	.L2163
	mov	r0, r6
	bl	allocate_new_data_superblock
.L2163:
	ldrh	r3, [r6, #4]
	cmp	r3, #0
	bne	.L2164
	mov	r0, r6
	bl	allocate_new_data_superblock
.L2164:
	ldr	r3, .L2198
	str	r6, [r3, #440]
.L2160:
	ldr	r2, .L2198
	mov	r8, r6
	ldrh	r3, [r6, #4]
	ldr	r1, [r2, #-976]
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
	b	.L2165
.L2181:
	ldrh	r3, [r8, #4]
	cmp	r3, #0
	beq	.L2166
	ldr	r6, [sp, #36]
	rsb	r2, r6, r5
	ldr	r6, [sp, #24]
	rsbs	r3, r2, #0
	adc	r3, r3, r2
	tst	r3, r6
	beq	.L2167
	ldr	r6, [sp, #28]
	cmp	r6, #0
	beq	.L2167
	movw	lr, #3922
	ldr	r6, [sp, #32]
	ldrh	r2, [r4, lr]
	add	r1, r6, r7
	mls	r1, r2, r5, r1
	cmp	r1, r2
	bne	.L2166
.L2167:
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
	ldr	r6, .L2198
	mla	r1, ip, lr, r1
	mul	lr, lr, r2
	ldr	r6, [r6, #-928]
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
	ldr	r0, .L2198
	bic	lr, lr, #3
	str	lr, [sp, #12]
	ldr	lr, [r0, #-948]
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
	beq	.L2168
	ldr	r2, [sp, #12]
	cmp	r2, #0
	beq	.L2169
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
	b	.L2170
.L2169:
	cmp	r3, #0
	beq	.L2170
	ldr	r3, [sp, #32]
	movw	lr, #3922
	add	fp, r3, r7
	ldrh	r3, [r4, lr]
	mls	fp, r3, r5, fp
	ldr	r3, [sp, #12]
	str	r3, [sp, #40]
	uxth	fp, fp
.L2170:
	movw	lr, #3922
	ldrh	r3, [r4, lr]
	cmp	fp, r3
	bne	.L2171
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
	bne	.L2174
	ldr	r0, [r3, #8]
	movw	r3, #3926
	ldrh	r2, [sl, r3]
	b	.L2196
.L2171:
	ldr	r2, [sp, #56]
	mov	r3, #36
	cmn	r2, #1
	beq	.L2175
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
	ldr	r3, .L2198
	ldreq	r2, [r3, #-1636]
	addeq	r2, r2, #1
	streq	r2, [r3, #-1636]
	beq	.L2177
	ldr	r2, [r6, #8]
	cmp	r2, r5
	beq	.L2177
	ldr	r2, [r3, #-1636]
	ldr	r0, .L2198+28
	add	r2, r2, #1
	str	r2, [r3, #-1636]
	mov	r2, r5
	ldr	r1, [r6, #8]
	bl	printk
	b	.L2177
.L2175:
	ldr	r1, [r4, #3968]
	ldr	r2, [r4, #3972]
	mla	r3, r3, r1, r2
	mov	r1, #0
	ldr	r0, [r3, #8]
	movw	r3, #3926
	ldrh	r2, [r4, r3]
	bl	ftl_memset
.L2177:
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
	b	.L2196
.L2168:
	ldr	r3, [sp, #24]
	ldr	r2, [r4, #3968]
	cmp	r3, #0
	ldr	r3, [r4, #3972]
	mla	ip, ip, r2, r3
	beq	.L2179
	movw	lr, #3922
	ldrh	r3, [r4, lr]
	mul	r3, r3, r5
	rsb	r3, r7, r3
	add	r3, r9, r3, asl #9
	str	r3, [ip, #8]
	b	.L2174
.L2179:
	movw	r0, #3922
	movw	r3, #3926
	ldrh	r1, [r4, r0]
	ldrh	r2, [r4, r3]
	ldr	r0, [ip, #8]
	mul	r1, r1, r5
	rsb	r1, r7, r1
	add	r1, r9, r1, asl #9
.L2196:
	bl	memcpy
.L2174:
	ldr	r3, .L2198+32
	ldr	r0, [sp, #48]
	ldr	lr, [sp, #44]
	strh	r3, [r0, lr]	@ movhi
	ldr	r3, .L2198
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
.L2165:
	ldr	r6, [sp, #28]
	ldr	r3, [sp, #52]
	cmp	r6, r3
	bne	.L2181
.L2166:
	ldr	r3, [sp, #20]
	mov	r6, r8
	ldr	lr, [sp, #28]
	ldr	r2, [r4, #3968]
	rsb	r3, lr, r3
	str	r3, [sp, #20]
	ldr	r3, .L2198
	ldr	r0, [sp, #24]
	ldr	r3, [r3, #-976]
	cmp	r2, r3
	orrcs	r0, r0, #1
	uxtb	r3, r0
	cmp	r3, #0
	bne	.L2182
	ldrh	r2, [r8, #4]
	cmp	r2, #0
	bne	.L2192
.L2182:
	bl	FtlCacheWriteBack
	ldr	lr, [sp, #20]
	mov	r3, #0
	str	r3, [sl, #3968]
	cmp	lr, #1
	ldr	r3, [sp, #24]
	movls	r3, #0
.L2192:
	str	r3, [sp, #24]
.L2195:
	ldr	r3, [sp, #20]
	ldr	r8, .L2198
	cmp	r3, #0
	bne	.L2184
	mov	r0, r3
	ldr	r6, [sp, #36]
	ldr	r3, [sp, #16]
	rsb	r1, r3, r6
	bl	ftl_do_gc
	ldr	r3, .L2198+36
	ldrh	r3, [r8, r3]
	cmp	r3, #31
	bhi	.L2157
	ldr	r2, .L2198+40
	mov	r3, #128
	mov	r4, #16
	strh	r3, [r8, r2]	@ movhi
	sub	r2, r2, #2
	strh	r3, [r8, r2]	@ movhi
.L2185:
	mov	r0, #0
	mov	r1, #1
	bl	ftl_do_gc
	ldr	r3, [r8, #-2088]
	cmp	r3, #0
	bne	.L2157
	subs	r4, r4, #1
	bne	.L2185
.L2157:
	mov	r0, #0
	add	sp, sp, #100
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, sl, fp, pc}
.L2199:
	.align	2
.L2198:
	.word	.LANCHOR2
	.word	.LANCHOR0
	.word	.LANCHOR2-1996
	.word	.LANCHOR1
	.word	-1944
	.word	.LANCHOR2-1948
	.word	-1992
	.word	.LC139
	.word	-3947
	.word	-2000
	.word	-1194
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
	ldr	r3, .L2205
	ldr	r3, [r3, #-2088]
	cmp	r3, #0
	movne	r0, #0
	bxne	lr
	cmp	ip, #16
	bne	.L2202
	add	r0, r0, #256
	b	FtlVendorPartWrite
.L2202:
	ldr	r3, .L2205+4
	add	ip, r1, r0
	ldr	r3, [r3, #3956]
	cmp	ip, r3
	bhi	.L2204
	b	ftl_write.part.22
.L2204:
	mvn	r0, #0
	bx	lr
.L2206:
	.align	2
.L2205:
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
	bne	.L2213
	add	r0, r1, #256
	mov	r1, r2
	mov	r2, r3
	bl	FtlVendorPartRead
	str	r0, [sp, #16]
	b	.L2214
.L2213:
	ldr	r3, [sp, #28]
	add	r3, r3, r1
	str	r3, [sp, #20]
	ldr	r3, .L2243
	ldr	r1, [sp, #20]
	ldr	r2, [r3, #3956]
	cmp	r1, r2
	mvnhi	r3, #0
	strhi	r3, [sp, #16]
	bhi	.L2214
	movw	r2, #3922
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
	ldr	r3, .L2243+4
	mov	fp, r0
	ldr	r0, [sp, #12]
	ldr	r2, [r3, #-1808]
	add	r2, r1, r2
	ldr	r1, [sp, #8]
	str	r2, [r3, #-1808]
	ldr	r2, [r3, #-1836]
	add	r2, r1, r2
	mov	r1, fp
	str	r2, [r3, #-1836]
	bl	FtlCacheMetchLpa
	cmp	r0, #0
	beq	.L2215
	bl	FtlCacheWriteBack
.L2215:
	mov	r9, #0
	ldr	r6, [sp, #12]
	str	r9, [sp, #32]
	mov	r5, r9
	str	r9, [sp, #16]
	ldr	r7, .L2243
	b	.L2238
.L2232:
	mov	r2, #0
	mov	r0, r6
	add	r1, sp, #60
	bl	log2phys
	ldr	r2, [sp, #60]
	cmn	r2, #1
	bne	.L2239
	b	.L2242
.L2220:
	mla	r0, r0, r6, sl
	cmp	r0, r4
	bcc	.L2219
	ldr	r2, [sp, #20]
	cmp	r0, r2
	bcs	.L2219
	rsb	r0, r4, r0
	mov	r1, #0
	mov	r2, #512
	str	r3, [sp, #4]
	add	r0, r8, r0, asl #9
	bl	ftl_memset
	ldr	r3, [sp, #4]
.L2219:
	add	sl, sl, #1
	b	.L2217
.L2242:
	mov	sl, #0
	movw	r3, #3922
.L2217:
	ldrh	r0, [r7, r3]
	cmp	sl, r0
	bcc	.L2220
	b	.L2221
.L2239:
	ldr	r3, .L2243+4
	mov	sl, #36
	ldr	r1, [r3, #-972]
	mla	sl, sl, r5, r1
	ldr	r1, [sp, #12]
	cmp	r6, r1
	str	r2, [sl, #4]
	movw	r2, #3922
	bne	.L2222
	ldr	r3, [r3, #-944]
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
	b	.L2223
.L2222:
	cmp	r6, fp
	ldrneh	r3, [r7, r2]
	mulne	r3, r3, r6
	bne	.L2240
	ldr	r3, [r3, #-940]
	ldrh	r2, [r7, r2]
	ldr	r1, [sp, #20]
	str	r3, [sl, #8]
	mul	r3, r2, r6
	rsb	r9, r3, r1
	cmp	r9, r2
	bne	.L2223
.L2240:
	rsb	r3, r4, r3
	add	r3, r8, r3, asl #9
	str	r3, [sl, #8]
.L2223:
	movw	r3, #3928
	ldr	r2, .L2243+4
	ldrh	r3, [r7, r3]
	str	r6, [sl, #16]
	ldr	r2, [r2, #-932]
	mul	r3, r3, r5
	add	r5, r5, #1
	bic	r3, r3, #3
	add	r3, r2, r3
	str	r3, [sl, #12]
.L2221:
	ldr	r3, [sp, #8]
	add	r6, r6, #1
	subs	r3, r3, #1
	str	r3, [sp, #8]
	beq	.L2225
	movw	r3, #3848
	ldrh	r3, [r7, r3]
	cmp	r5, r3, asl #3
	bne	.L2238
.L2225:
	cmp	r5, #0
	beq	.L2238
	ldr	sl, .L2243+4
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
.L2231:
	ldr	r3, [sp, #24]
	mov	r5, #36
	ldr	r1, [sp, #12]
	mul	r5, r5, r3
	ldr	r3, [sl, #-972]
	add	r3, r3, r5
	ldr	r2, [r3, #16]
	cmp	r2, r1
	bne	.L2227
	ldr	r1, [r3, #8]
	ldr	r3, [sl, #-944]
	cmp	r1, r3
	bne	.L2228
	ldr	r3, [sp, #40]
	mov	r0, r8
	ldr	r2, [sp, #44]
	add	r1, r1, r3
	b	.L2241
.L2227:
	cmp	r2, fp
	bne	.L2228
	ldr	r1, [r3, #8]
	ldr	r3, [sl, #-940]
	cmp	r1, r3
	bne	.L2228
	movw	r2, #3922
	ldrh	r0, [r7, r2]
	ldr	r2, [sp, #48]
	mul	r0, r0, fp
	rsb	r0, r4, r0
	add	r0, r8, r0, asl #9
.L2241:
	bl	memcpy
.L2228:
	ldr	r3, [sl, #-972]
	add	r0, r3, r5
	ldr	r2, [r3, r5]
	cmn	r2, #1
	streq	r2, [sp, #16]
	ldreq	r1, [sl, #-1636]
	addeq	r1, r1, #1
	streq	r1, [sl, #-1636]
	ldr	r3, [r3, r5]
	cmp	r3, #256
	bne	.L2230
	ldr	r0, [r0, #4]
	ubfx	r0, r0, #10, #16
	bl	P2V_block_in_plane
	bl	FtlGcRefreshBlock
.L2230:
	ldr	r3, [sp, #24]
	add	r3, r3, #1
	str	r3, [sp, #24]
	cmp	r3, r6
	bne	.L2231
	ldr	r6, [sp, #52]
	mov	r5, #0
.L2238:
	ldr	r3, [sp, #8]
	cmp	r3, #0
	bne	.L2232
	ldr	r3, .L2243+8
	ldr	r2, .L2243+4
	ldrh	r3, [r2, r3]
	cmp	r3, #0
	beq	.L2214
	ldr	r0, [sp, #8]
	mov	r1, #1
	bl	ftl_do_gc
.L2214:
	ldr	r0, [sp, #16]
	add	sp, sp, #68
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, sl, fp, pc}
.L2244:
	.align	2
.L2243:
	.word	.LANCHOR0
	.word	.LANCHOR2
	.word	-1138
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
	beq	.L2251
	b	FlashBootVendorWrite
.L2251:
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
	ldr	r9, .L2261
	movw	r3, #3060
	ldr	r4, .L2261+4
	.pad #52
	sub	sp, sp, #52
	mov	r1, #0
	mvn	r7, #0
	ldrh	r3, [r9, r3]
	add	r0, r4, #232
	ldrh	r8, [r9, r2]
	mov	r2, #16
	mov	r6, #0
	mul	r8, r8, r3
	bl	ftl_memset
	ldr	r3, [r4, #228]
	str	r6, [sp, #20]
	mov	r4, r6
	str	r3, [sp, #24]
	uxth	r8, r8
	add	r3, r8, r7
	uxth	r3, r3
	str	r3, [sp, #4]
	b	.L2253
.L2259:
	ldr	r5, [sp, #4]
	sub	r3, r8, #12
	mul	fp, r8, r4
	ldr	sl, .L2261+4
	b	.L2254
.L2257:
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
	beq	.L2255
	ldr	r2, [sl, #228]
	ldrh	r1, [r2, #0]
	movw	r2, #61664
	cmp	r1, r2
	bne	.L2255
	ldr	r0, .L2261+8
	mov	r1, r4
	mov	r2, r5
	add	r6, r6, #1
	bl	printk
	ldr	r3, .L2261+4
	uxth	r6, r6
	add	r3, r3, r4, asl #1
	strh	r5, [r3, #232]	@ movhi
	b	.L2256
.L2255:
	sub	r5, r5, #1
	uxth	r5, r5
.L2254:
	cmp	r5, r3
	bgt	.L2257
.L2256:
	ldrb	r3, [r9, #3762]	@ zero_extendqisi2
	add	r4, r4, #1
	cmp	r3, r6
	uxtb	r4, r4
	moveq	r7, #0
.L2253:
	ldrb	r3, [r9, #3762]	@ zero_extendqisi2
	cmp	r3, r4
	bhi	.L2259
	mov	r0, r7
	add	sp, sp, #52
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, sl, fp, pc}
.L2262:
	.align	2
.L2261:
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
	ldr	r3, .L2273
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
	ldr	r7, .L2273+4
	mov	r4, r0
	mul	r8, r8, r2
	ldr	r3, [r7, #148]
	str	r3, [sp, #12]
	ldr	r3, [r7, #228]
	uxth	r8, r8
	sub	r5, r8, #1
	mul	sl, r8, r6
	str	r3, [sp, #16]
	uxth	r5, r5
	sub	r8, r8, #16
	b	.L2264
.L2270:
	mov	r1, #1
	add	r3, r5, sl
	add	r0, sp, #4
	mov	r2, r1
	mov	r3, r3, asl #10
	str	r3, [sp, #8]
	bl	FlashReadPages
	ldr	r3, [sp, #4]
	cmn	r3, #1
	beq	.L2265
	ldr	r3, [r7, #228]
	ldrh	r2, [r3, #0]
	movw	r3, #61664
	cmp	r2, r3
	bne	.L2265
	cmp	r4, #0
	moveq	r0, r4
	beq	.L2266
	cmp	r6, #0
	ldreq	r3, .L2273+4
	moveq	r0, #1
	beq	.L2267
	b	.L2268
.L2269:
	ldr	r2, [r3, #148]
	ubfx	r1, r6, #5, #16
	and	lr, r6, #31
	add	r6, r6, #1
	ldr	ip, [r2, r1, asl #2]
	uxth	r6, r6
	orr	ip, ip, r0, asl lr
	str	ip, [r2, r1, asl #2]
.L2267:
	ldr	r2, [r3, #156]
	cmp	r6, r2
	bcc	.L2269
.L2268:
	ldr	r3, .L2273+4
	mov	r2, r9
	mov	r0, r4
	ldr	r1, [r3, #148]
	bl	memcpy
	mov	r2, #4
	ldr	r0, .L2273+8
	mov	r1, r4
	mov	r3, r2
	bl	rknand_print_hex
	mov	r0, #0
	b	.L2266
.L2265:
	sub	r5, r5, #1
	uxth	r5, r5
.L2264:
	cmp	r5, r8
	bgt	.L2270
	mvn	r0, #0
.L2266:
	add	sp, sp, #40
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, sl, pc}
.L2274:
	.align	2
.L2273:
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
	ldr	r3, .L2282
	stmfd	sp!, {r4, r5, r6, r7, r8, lr}
	.save {r4, r5, r6, r7, r8, lr}
	mov	r5, r0
	ldr	r3, [r3, #3624]
	ldr	r6, .L2282+4
	ldrb	r2, [r3, #13]	@ zero_extendqisi2
	ldrh	r4, [r3, #14]
	ldr	r0, [r6, #136]
	mul	r4, r4, r2
	uxth	r4, r4
	add	r2, r4, #7
	mov	r2, r2, lsr #3
	bl	FlashReadFacBbtData
	cmn	r0, #1
	beq	.L2281
	mov	r2, #0
	mov	r0, r4, lsr #4
	mov	r3, r2
	sub	r4, r4, #1
	mov	r1, #1
	b	.L2277
.L2279:
	ldr	ip, [r6, #136]
	mov	r8, r2, lsr #5
	and	r7, r2, #31
	ldr	ip, [ip, r8, asl #2]
	ands	ip, ip, r1, asl r7
	movne	ip, r3, asl #1
	addne	r3, r3, #1
	uxthne	r3, r3
	strneh	r2, [r5, ip]	@ movhi
	cmp	r3, r0
	bcs	.L2281
	add	r2, r2, #1
	uxth	r2, r2
.L2277:
	cmp	r2, r4
	blt	.L2279
	b	.L2276
.L2281:
	mov	r3, #0
.L2276:
	mov	r3, r3, asl #1
	mvn	r2, #0
	mov	r0, #0
	strh	r2, [r5, r3]	@ movhi
	ldmfd	sp!, {r4, r5, r6, r7, r8, pc}
.L2283:
	.align	2
.L2282:
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
	ldr	r4, .L2302
	ldr	r5, [r4, #-2088]
	cmp	r5, #0
	bne	.L2285
	ldr	r7, .L2302+4
	bl	FtlBbtMemInit
	ldr	r6, .L2302+8
	bl	FtlLoadFactoryBbt
	add	sl, r7, #12
	b	.L2286
.L2292:
	ldrh	r3, [sl], #2
	movw	r2, #65535
	ldr	r0, [r4, #-960]
	movw	fp, #3912
	ldr	r9, [r4, #-936]
	cmp	r3, r2
	str	r0, [r4, #188]
	str	r9, [r4, #192]
	beq	.L2287
	ldrh	r8, [r6, fp]
	mov	r1, #1
	mov	r2, r1
	ldr	r0, .L2302+12
	mla	r8, r8, r5, r3
	mov	r3, r8, asl #10
	str	r3, [r4, #184]
	bl	FlashReadPages
	ldrh	r2, [r6, fp]
	ldr	r0, [r7, #28]
	add	r2, r2, #7
	ldr	r1, [r4, #188]
	mov	r2, r2, lsr #3
	bl	memcpy
	b	.L2288
.L2287:
	mov	r1, r5
	bl	FlashGetBadBlockList
	ldr	r0, [r4, #188]
	ldr	r1, [r7, #28]
	bl	FtlBbt2Bitmap
	ldrh	fp, [r6, fp]
.L2290:
	sub	fp, fp, #1
	uxth	fp, fp
.L2301:
	movw	r8, #3912
	ldrh	r0, [r6, r8]
	mla	r0, r0, r5, fp
	uxth	r0, r0
	bl	FtlBbmIsBadBlock
	cmp	r0, #1
	beq	.L2290
	mov	r1, #0
	mov	r2, #16
	strh	fp, [sl, #-2]	@ movhi
	ldr	r0, [r4, #-936]
	bl	ftl_memset
	ldr	r3, .L2302+16
	strh	r3, [r9, #0]	@ movhi
	mov	r3, #0
	str	r3, [r9, #4]
	ldrh	r3, [sl, #-2]
	ldrh	r8, [r6, r8]
	strh	r3, [r9, #2]	@ movhi
	ldrh	r3, [sl, #-2]
	ldr	r1, [r7, #28]
	ldr	r0, [r4, #188]
	mla	r8, r8, r5, r3
	mov	r3, r8, asl #10
	str	r3, [r4, #184]
	ldr	r3, .L2302+20
	ldrh	r2, [r4, r3]
	mov	r2, r2, asl #2
	bl	memcpy
	mov	r1, #1
	mov	r2, r1
	ldr	r0, .L2302+12
	bl	FlashEraseBlocks
	mov	r1, #1
	mov	r3, r1
	ldr	r0, .L2302+12
	mov	r2, r1
	bl	FlashProgPages
	ldr	r3, [r4, #180]
	cmn	r3, #1
	bne	.L2288
	uxth	r0, r8
	bl	FtlBbmMapBadBlock
	b	.L2301
.L2288:
	uxth	r0, r8
	add	r5, r5, #1
	bl	FtlBbmMapBadBlock
	add	r7, r7, #4
.L2286:
	movw	r3, #3870
	ldrh	r3, [r6, r3]
	cmp	r5, r3
	bcc	.L2292
	mov	r4, #0
	ldr	r7, .L2302+8
	movw	r6, #3930
	b	.L2293
.L2294:
	mov	r0, r4
	add	r4, r4, #1
	bl	FtlBbmMapBadBlock
	uxth	r4, r4
.L2293:
	ldrh	r3, [r7, r6]
	ldr	r5, .L2302+8
	cmp	r3, r4
	bhi	.L2294
	movw	r7, #3992
	movw	r6, #3980
	ldrh	r4, [r5, r7]
	sub	r4, r4, #1
	uxth	r4, r4
	b	.L2295
.L2300:
	mov	r0, r4
	bl	FtlBbmIsBadBlock
	cmp	r0, #1
	beq	.L2296
	mov	r0, r4
	bl	FlashTestBlk
	cmp	r0, #0
	beq	.L2297
	mov	r0, r4
	bl	FtlBbmMapBadBlock
	b	.L2296
.L2297:
	ldrh	r2, [r5, r6]
	movw	r3, #65535
	cmp	r2, r3
	streqh	r4, [r5, r6]	@ movhi
.L2298:
	ldrne	r2, .L2302+8
	movne	r3, #3984
	strneh	r4, [r2, r3]	@ movhi
	bne	.L2299
.L2296:
	sub	r4, r4, #1
	uxth	r4, r4
.L2295:
	ldrh	r3, [r5, r7]
	sub	r3, r3, #48
	cmp	r4, r3
	bgt	.L2300
.L2299:
	ldr	r4, .L2302+8
	movw	r6, #3980
	ldr	r3, .L2302
	mov	r5, #3984
	mov	r7, #0
	movw	r8, #3982
	str	r7, [r4, #3988]
	mov	r2, #2
	ldr	r0, [r3, #-2084]
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
	ldr	r3, [r4, #3988]
	ldrh	r2, [r4, r5]
	add	r3, r3, #1
	str	r3, [r4, #3988]
	ldrh	r3, [r4, r6]
	strh	r7, [r4, r8]	@ movhi
	strh	r2, [r4, r6]	@ movhi
	strh	r3, [r4, r5]	@ movhi
	bl	FtlBbmTblFlush
.L2285:
	mov	r0, #0
	ldmfd	sp!, {r3, r4, r5, r6, r7, r8, r9, sl, fp, pc}
.L2303:
	.align	2
.L2302:
	.word	.LANCHOR2
	.word	.LANCHOR0+3980
	.word	.LANCHOR0
	.word	.LANCHOR2+180
	.word	-3872
	.word	-1028
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
	mov	r6, #3952
	ldr	r5, .L2325
	mov	r1, #0
	ldr	r4, .L2325+4
	ldrh	r2, [r5, r6]
	ldr	r0, [r4, #-900]
	mov	r2, r2, asl #2
	bl	ftl_memset
	ldrh	r2, [r5, r6]
	mov	r1, #0
	ldr	r0, [r4, #-1032]
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
	beq	.L2305
	bl	FtlMakeBbt
.L2305:
	mov	r3, #0
	ldr	ip, .L2325
	movw	r0, #3922
	ldr	r2, .L2325+4
	ldr	r1, .L2325+8
	b	.L2306
.L2307:
	ldr	lr, [r2, #-944]
	mvn	r4, r3
	orr	r4, r3, r4, asl #16
	str	r4, [lr, r3, asl #2]
	ldr	lr, [r2, #-940]
	str	r1, [lr, r3, asl #2]
	add	r3, r3, #1
	uxth	r3, r3
.L2306:
	ldrh	lr, [ip, r0]
	ldr	r8, .L2325
	cmp	r3, lr, asl #7
	blt	.L2307
	mov	r3, #3856
	mov	r5, #0
	ldrh	r6, [r8, r3]
	movw	r7, #3858
	b	.L2308
.L2309:
	mov	r0, r6
	mov	r1, #1
	bl	FtlLowFormatEraseBlock
	add	r6, r6, #1
	uxth	r6, r6
	add	r5, r5, r0
	uxth	r5, r5
.L2308:
	ldrh	r3, [r8, r7]
	ldr	r4, .L2325
	cmp	r3, r6
	bhi	.L2309
	movw	r3, #3848
	ldrh	r1, [r4, r3]
	sub	r3, r5, #3
	cmp	r3, r1, asl #1
	blt	.L2310
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
	b	.L2311
.L2312:
	mov	r0, r5
	mov	r1, #1
	bl	FtlLowFormatEraseBlock
	add	r5, r5, #1
	uxth	r5, r5
.L2311:
	ldrh	r3, [r4, r6]
	cmp	r3, r5
	bhi	.L2312
.L2310:
	mov	r4, #0
	ldr	r8, .L2325
	mov	r6, r4
	mov	r7, #3856
	b	.L2313
.L2314:
	mov	r0, r6
	mov	r1, #0
	bl	FtlLowFormatEraseBlock
	add	r6, r6, #1
	uxth	r6, r6
	add	r4, r4, r0
	uxth	r4, r4
.L2313:
	ldrh	r3, [r8, r7]
	ldr	r5, .L2325
	cmp	r3, r6
	bhi	.L2314
	movw	r3, #3858
	ldr	r8, .L2325+4
	ldrh	r3, [r5, r3]
	ldr	fp, [r5, #3860]
	ldr	r9, .L2325+12
	str	r3, [r8, #-984]
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
	ble	.L2315
	rsb	r0, r4, fp
	mov	r1, r6
	bl	__aeabi_uidiv
	str	r0, [r5, #3976]
	mov	r0, r0, lsr #5
	add	r0, r0, #24
	strh	r0, [r8, r9]	@ movhi
.L2315:
	ldr	r3, .L2325
	ldr	r3, [r3, #3840]
	cmp	r3, #1
	bne	.L2316
	mov	r0, r4
	mov	r1, r6
	bl	__aeabi_uidiv
	ldr	r8, .L2325+4
	ldr	r5, .L2325+12
	ldrh	r9, [r8, r5]
	uxtah	r0, r9, r0
	add	r9, r9, r0, lsr #2
	strh	r9, [r8, r5]	@ movhi
.L2316:
	ldr	r3, .L2325
	ldrb	r3, [r3, #928]	@ zero_extendqisi2
	cmp	r3, #0
	beq	.L2317
	mov	r0, r4
	mov	r1, r6
	bl	__aeabi_uidiv
	ldr	r8, .L2325+4
	ldr	r5, .L2325+12
	ldrh	r9, [r8, r5]
	uxtah	r0, r9, r0
	add	r9, r9, r0, lsr #2
	strh	r9, [r8, r5]	@ movhi
.L2317:
	ldr	r0, .L2325
	movw	r3, #3908
	ldrh	r3, [r0, r3]
	cmp	r3, #0
	beq	.L2318
	ldr	r1, .L2325+4
	ldr	r2, .L2325+12
	ldrh	ip, [r1, r2]
	add	ip, ip, r3, lsr #1
	strh	ip, [r1, r2]	@ movhi
	mul	ip, r6, r3
	cmp	ip, r4
	strgt	r7, [r0, #3976]
	addgt	r3, r3, #32
	addgt	sl, sl, r3
	strgth	sl, [r1, r2]	@ movhi
.L2318:
	ldr	r4, .L2325+4
	ldr	r5, .L2325
	ldr	r3, .L2325+12
	ldr	r7, .L2325+16
	ldr	r2, [r5, #3976]
	ldrh	r3, [r4, r3]
	rsb	r3, r3, r2
	mul	r6, r6, r3
	movw	r3, #3916
	ldrh	r3, [r5, r3]
	str	r6, [r4, #216]
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
	ldr	r1, .L2325+20
	mov	r3, #0
	ldr	r2, .L2325+24
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
.L2319:
	ldr	r0, .L2325+28
	bl	make_superblock
	ldrb	r3, [r4, #-1989]	@ zero_extendqisi2
	ldr	r5, .L2325+4
	cmp	r3, #0
	ldr	r3, .L2325+16
	bne	.L2320
	ldrh	r3, [r4, r7]
	ldr	r2, [r4, #-2016]
	mov	r3, r3, asl #1
	strh	r6, [r2, r3]	@ movhi
	ldrh	r3, [r4, r7]
	add	r3, r3, #1
	strh	r3, [r4, r7]	@ movhi
	b	.L2319
.L2320:
	ldr	r2, [r5, #-1804]
	mvn	r7, #0
	ldr	r1, [r5, #-2016]
	ldr	r6, .L2325+32
	str	r2, [r5, #-1984]
	add	r2, r2, #1
	str	r2, [r5, #-1804]
	ldr	r2, .L2325+36
	ldrh	r0, [r5, r2]
	ldrh	r2, [r5, r3]
	mov	r2, r2, asl #1
	strh	r0, [r1, r2]	@ movhi
	mov	r2, #0
	ldr	r0, .L2325+40
	ldrh	r3, [r5, r3]
	strb	r2, [r5, #-1942]
	add	r3, r3, #1
	strh	r2, [r5, r0]	@ movhi
	strh	r3, [r5, r6]	@ movhi
	mov	r3, #1
	strb	r3, [r5, #-1940]
.L2321:
	ldr	r0, .L2325+44
	bl	make_superblock
	ldrb	r3, [r5, #-1941]	@ zero_extendqisi2
	ldr	r4, .L2325+4
	cmp	r3, #0
	bne	.L2322
	ldrh	r3, [r5, r6]
	ldr	r2, [r5, #-2016]
	mov	r3, r3, asl #1
	strh	r7, [r2, r3]	@ movhi
	ldrh	r3, [r5, r6]
	add	r3, r3, #1
	strh	r3, [r5, r6]	@ movhi
	b	.L2321
.L2322:
	ldr	r3, [r4, #-1804]
	mvn	r5, #0
	ldr	r2, [r4, #-2016]
	str	r3, [r4, #-1936]
	add	r3, r3, #1
	str	r3, [r4, #-1804]
	ldr	r3, .L2325+48
	ldrh	r1, [r4, r3]
	sub	r3, r3, #4
	ldrh	r3, [r4, r3]
	mov	r3, r3, asl #1
	strh	r1, [r2, r3]	@ movhi
	ldr	r3, .L2325+52
	strh	r5, [r4, r3]	@ movhi
	bl	FtlFreeSysBlkQueueOut
	ldr	r3, .L2325+56
	mov	r2, #0
	strh	r0, [r4, r3]	@ movhi
	add	r3, r3, #2
	strh	r2, [r4, r3]	@ movhi
	add	r3, r3, #2
	ldr	r2, [r4, #216]
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
	ldr	r3, .L2325+60
	mov	r2, #1
	str	r2, [r3, #2996]
	ldmfd	sp!, {r3, r4, r5, r6, r7, r8, r9, sl, fp, pc}
.L2326:
	.align	2
.L2325:
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
	ldr	r3, .L2329
	ldr	r3, [r3, #-2088]
	cmp	r3, #0
	bne	.L2328
	bl	FtlLowFormat.part.24
.L2328:
	mov	r0, #0
	ldmfd	sp!, {r3, pc}
.L2330:
	.align	2
.L2329:
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
	ldr	r3, .L2416
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
	bne	.L2332
	mvn	r2, #88
	strb	r2, [r3, #4]
	ldr	r3, .L2416+4
	mvn	r2, #8
	strb	r2, [r3, #3025]
	b	.L2388
.L2332:
	cmp	r4, #3
	bne	.L2334
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
	b	.L2411
.L2334:
	cmp	r4, #4
	bne	.L2335
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
.L2411:
	mov	r6, #8
	strb	r2, [r3, #11]
	mov	r5, r6
	b	.L2333
.L2335:
	cmp	r4, #5
	bne	.L2336
	mov	r2, #56
	mov	r6, #8
	strb	r2, [r3, #4]
	mov	r2, #57
	strb	r2, [r3, #5]
	mov	r2, #58
	strb	r2, [r3, #6]
	mov	r2, #59
	strb	r2, [r3, #7]
	b	.L2410
.L2336:
	cmp	r4, #6
	bne	.L2337
	mov	r2, #14
	mov	r6, #12
	strb	r2, [r3, #4]
	mov	r2, #15
	strb	r2, [r3, #5]
	mov	r2, #16
	strb	r2, [r3, #6]
	mov	r2, #17
	strb	r2, [r3, #7]
	b	.L2410
.L2337:
	cmp	r4, #7
	bne	.L2388
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
	b	.L2333
.L2388:
	mov	r6, #7
.L2410:
	mov	r5, #4
.L2333:
	sub	r3, r4, #1
	cmp	r3, #1
	bhi	.L2406
	b	.L2414
.L2344:
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
.L2340:
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
	bcc	.L2340
	mov	r2, #0
	b	.L2341
.L2342:
	ldrb	r7, [r0, r1, asl #2]	@ zero_extendqisi2
	ldrb	r5, [r4, r2]	@ zero_extendqisi2
	add	r5, r7, r5
	strb	r5, [ip, r1, asl #3]
	add	r1, r1, #1
	cmp	r1, #7
	bne	.L2342
	add	r2, r2, #1
	cmp	r2, #4
	beq	.L2343
.L2341:
	mov	r1, #1
	add	ip, r4, r2
	add	r0, r2, r9
	b	.L2342
.L2343:
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
	b	.L2338
.L2414:
	ldr	r9, .L2416+8
	mov	r3, r6
	mov	r8, #0
	mov	r6, r5
.L2338:
	ldr	fp, .L2416
	ldrb	r2, [fp, #3762]	@ zero_extendqisi2
	cmp	r2, r8
	bhi	.L2344
	mov	r5, r6
	mov	r6, r3
	b	.L2345
.L2406:
	sub	r3, r4, #3
	cmp	r3, #4
	bhi	.L2345
	mul	r3, r6, r5
	mov	sl, #0
	mov	r7, sl
	str	r6, [sp, #48]
	mov	r2, r3, asr #2
	mov	r3, r3, asl #4
	str	r2, [sp, #24]
	str	r3, [sp, #52]
	b	.L2346
.L2387:
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
	bne	.L2349
	mov	r3, #255
	str	r3, [r8, #4]
	mov	r3, #64
	str	r3, [r9, fp, asl #8]
	mov	r3, #204
	b	.L2412
.L2349:
	sub	r3, r4, #5
	cmp	r3, #1
	ldrls	r3, .L2416
	ldrlsb	r3, [r3, #4]	@ zero_extendqisi2
	strls	r3, [r8, #4]
	movls	r3, #82
	bls	.L2413
.L2351:
	mov	r3, #174
	str	r3, [r8, #4]
	mov	r3, #176
	str	r7, [r9, fp, asl #8]
.L2412:
	str	r3, [r8, #4]
	mov	r3, #77
.L2413:
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
	bls	.L2355
	cmp	r4, #7
	moveq	r1, #32
	movne	r1, #2
.L2355:
	ldr	r3, .L2416+12
	ldr	r2, [r3, #148]
	mov	r3, #0
.L2356:
	ldr	r0, [r8, #0]
	strb	r0, [r2, r3]
	add	r3, r3, #1
	uxtb	r0, r3
	cmp	r0, r1
	bcc	.L2356
	cmp	r4, #7
	bne	.L2357
	mov	r3, #0
.L2359:
	ldrb	r1, [r2, #0]	@ zero_extendqisi2
	cmp	r1, #12
	beq	.L2358
	ldrb	r1, [r2, #1]	@ zero_extendqisi2
	cmp	r1, #10
	beq	.L2358
	add	r3, r3, #1
	add	r2, r2, #4
	uxtb	r3, r3
	cmp	r3, #8
	bne	.L2359
	b	.L2360
.L2358:
	cmp	r3, #6
	bls	.L2361
.L2360:
	ldr	r0, .L2416+16
	mov	r1, #0
	bl	printk
.L2362:
	b	.L2362
.L2357:
	cmp	r4, #6
	bne	.L2361
	mov	r1, #8
.L2363:
	ldrb	r3, [r2], #1	@ zero_extendqisi2
	cmp	r3, #12
	beq	.L2361
	ldrb	r3, [r2, #7]	@ zero_extendqisi2
	cmp	r3, #4
	beq	.L2361
	sub	r1, r1, #1
	uxtb	r1, r1
	cmp	r1, #0
	bne	.L2363
	b	.L2415
.L2365:
	b	.L2365
.L2361:
	ldr	r3, .L2416+12
	mov	r2, #0
	ldr	ip, [r3, #148]
	str	ip, [sp, #40]
.L2366:
	ldr	r1, [r8, #0]
	ldr	lr, [sp, #40]
	ldr	ip, [sp, #52]
	strb	r1, [lr, r2]
	add	r2, r2, #1
	cmp	r2, ip
	blt	.L2366
	ldr	r2, [sp, #24]
	ldr	r1, [r3, #148]
	mov	r3, #8
	mov	ip, r2, asl #2
	mov	lr, r2, asl #3
	add	r1, r1, ip
	str	ip, [sp, #32]
.L2368:
	mov	r2, #0
	mov	r0, r2
.L2367:
	ldr	ip, [r1, r2]
	add	r0, r0, #1
	mvn	ip, ip
	str	ip, [r1, r2]
	ldr	ip, [sp, #24]
	add	r2, r2, #4
	cmp	r0, ip
	blt	.L2367
	subs	r3, r3, #1
	add	r1, r1, lr
	bne	.L2368
	str	r3, [sp, #16]
	str	sl, [sp, #60]
	b	.L2369
.L2373:
	mov	lr, #1
	mov	r0, #0
	mov	ip, lr, asl r2
	mov	lr, r0
	mov	sl, #16
	str	r3, [sp, #4]
	str	sl, [sp, #20]
.L2371:
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
	bne	.L2371
	cmp	lr, #8
	add	r2, r2, #1
	orrhi	r1, r1, ip
	cmp	r2, #32
	ldr	r3, [sp, #4]
	bne	.L2373
	ldr	ip, [sp, #16]
	ldr	r2, [sp, #24]
	add	ip, ip, #1
	ldr	sl, [sp, #44]
	cmp	ip, r2
	str	ip, [sp, #16]
	str	r1, [sl, r3]
	add	r3, r3, #4
	bge	.L2374
.L2369:
	ldr	sl, .L2416+12
	mov	r1, #0
	mov	r2, r1
	ldr	sl, [sl, #148]
	add	ip, sl, r3
	str	ip, [sp, #56]
	str	sl, [sp, #44]
	b	.L2373
.L2374:
	ldr	r3, .L2416+12
	ldr	sl, [sp, #60]
	ldr	r1, [r3, #148]
	mov	r3, #0
	mov	r2, r3
.L2376:
	ldr	r0, [r1, r3]
	add	r3, r3, #4
	cmp	r0, #0
	addeq	r2, r2, #1
	cmp	r3, #32
	bne	.L2376
	cmp	r2, #7
	ble	.L2377
	ldr	r0, .L2416+20
	mov	r2, #1
	mov	r3, #1024
	bl	rknand_print_hex
	ldr	r0, .L2416+16
	mov	r1, #0
	bl	printk
.L2378:
	b	.L2378
.L2377:
	cmp	r4, #6
	moveq	r0, #4
	streq	r0, [sp, #16]
	beq	.L2379
	cmp	r4, #7
	moveq	lr, #10
	movne	lr, #8
	str	lr, [sp, #16]
.L2379:
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
	b	.L2382
.L2383:
	ldrb	lr, [ip], #1	@ zero_extendqisi2
	strb	lr, [r1, r3]
	add	r3, r3, #1
	uxtb	lr, r3
	cmp	lr, r5
	bcc	.L2383
	ldr	r3, [sp, #20]
	add	r2, r2, #1
	ldr	ip, [sp, #16]
	add	r0, r0, r3
	ldr	r3, [sp, #48]
	add	r1, r1, ip
	cmp	r2, r3
	bge	.L2384
.L2382:
	mov	ip, r0
	mov	r3, #0
	b	.L2383
.L2384:
	mov	r3, #255
	mov	r0, r6
	str	r3, [r8, #8]
	bl	NandcWaitFlashReady
	ldr	ip, [sp, #36]
	cmp	ip, #1
	movhi	r3, #56
	strhi	r3, [r8, #8]
	bhi	.L2386
	mov	r3, #54
	str	r3, [r8, #8]
	ldr	r3, .L2416
	mov	r0, sl
	mvn	r1, #0
	ldrb	r3, [r3, #4]	@ zero_extendqisi2
	str	r3, [r8, #4]
	mov	r3, #22
	str	r7, [r9, fp, asl #8]
	str	r3, [r8, #8]
	bl	FlashReadCmd
.L2386:
	mov	r0, r6
	add	sl, sl, #1
	bl	NandcWaitFlashReady
	uxtb	sl, sl
.L2346:
	ldr	r1, .L2416
	ldrb	r3, [r1, #3762]	@ zero_extendqisi2
	cmp	r3, sl
	bhi	.L2387
	ldr	r6, [sp, #48]
.L2345:
	ldr	r3, .L2416
	strb	r5, [r3, #1]
	strb	r6, [r3, #2]
	add	sp, sp, #68
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, sl, fp, pc}
.L2415:
	ldr	r0, .L2416+16
	bl	printk
	b	.L2365
.L2417:
	.align	2
.L2416:
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
	bhi	.L2420
	ldmfd	sp!, {r3, lr}
	b	HynixGetReadRetryDefault
.L2420:
	cmp	r3, #49
	bne	.L2421
	ldr	r0, .L2422
	mov	r2, #64
	ldr	r1, .L2422+4
	strb	r3, [r0, #0]
	mov	r3, #4
	strb	r3, [r0, #1]
	mov	r3, #15
	strb	r3, [r0, #2]
	add	r0, r0, #4
	bl	memcpy
	ldmfd	sp!, {r3, pc}
.L2421:
	ldmfd	sp!, {r3, lr}
	b	FlashGetReadRetryDefault.part.25
.L2423:
	.align	2
.L2422:
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
	ldr	r5, .L2475
	bl	ftl_malloc
	ldr	r6, .L2475+4
	mov	r4, #0
	str	r0, [r5, #148]
	mov	r0, #32768
	bl	ftl_malloc
	str	r0, [r5, #136]
	mov	r0, #4096
	bl	ftl_malloc
	str	r0, [r5, #228]
	mov	r0, #32768
	bl	ftl_malloc
	str	r0, [r5, #220]
	mov	r0, #4096
	bl	ftl_malloc
	mov	r3, #50
	str	r4, [r5, #156]
	strb	r3, [r5, #164]
	strb	r3, [r6, #853]
	mov	r3, #128
	str	r4, [r5, #168]
	str	r3, [r6, #856]
	mov	r3, #60
	strb	r4, [r6, #3780]
	strb	r3, [r5, #152]
	strb	r4, [r6, #852]
	str	r0, [r5, #224]
	mov	r0, r7
	bl	NandcInit
	ldr	r5, .L2475+8
	mov	r7, #44
.L2428:
	uxtb	r0, r4
	mov	r1, r5
	bl	FlashReadIDRaw
	cmp	r4, #0
	bne	.L2425
	ldrb	r3, [r6, #2980]	@ zero_extendqisi2
	sub	r3, r3, #1
	uxtb	r3, r3
	cmp	r3, #253
	bhi	.L2466
	ldrb	r3, [r6, #2981]	@ zero_extendqisi2
	cmp	r3, #255
	beq	.L2467
.L2425:
	ldrb	r3, [r5, #0]	@ zero_extendqisi2
	add	r4, r4, #1
	cmp	r3, #181
	streqb	r7, [r5, #0]
	cmp	r4, #4
	add	r5, r5, #8
	bne	.L2428
	ldr	r4, .L2475+4
	ldrb	r3, [r4, #2980]	@ zero_extendqisi2
	cmp	r3, #173
	beq	.L2429
	ldr	r0, [r4, #3784]
	bl	NandcSetDdrMode
.L2429:
	ldr	r5, .L2475+4
	mov	r1, #0
	mov	r2, #852
	mov	r0, r5
	bl	ftl_memset
	ldr	r3, .L2475+12
	str	r3, [r4, #3624]
	mov	r3, #0
	strb	r3, [r4, #860]
	ldrb	r3, [r4, #2981]	@ zero_extendqisi2
	cmp	r3, #161
	cmpne	r3, #241
	beq	.L2430
	cmp	r3, #218
	beq	.L2430
	cmp	r3, #209
	beq	.L2430
	cmp	r3, #220
	bne	.L2431
	ldrb	r2, [r5, #2983]	@ zero_extendqisi2
	cmp	r2, #149
	bne	.L2431
.L2430:
	ldr	r2, .L2475+4
	mov	r1, #1
	mov	r0, #16
	ldr	ip, .L2475+16
	strb	r1, [r2, #852]
	ldr	r1, .L2475
	strb	r0, [r2, #853]
	strb	r3, [ip, #3038]
	strb	r0, [r1, #152]
	ldrb	r0, [r2, #2980]	@ zero_extendqisi2
	cmp	r0, #152
	strb	r0, [ip, #3037]
	bne	.L2432
	ldrb	r0, [r2, #2984]	@ zero_extendqisi2
	sxtb	r0, r0
	cmp	r0, #0
	movge	r0, #24
	strgeb	r0, [r1, #152]
.L2432:
	ldr	r1, [r2, #3776]
	ldr	r2, .L2475+20
	cmp	r1, r2
	ldreq	r2, .L2475
	moveq	r1, #16
	streqb	r1, [r2, #152]
	cmp	r3, #218
	bne	.L2434
	ldr	r3, .L2475+16
	movw	r2, #3050
	mov	r1, #2048
	strh	r1, [r3, r2]	@ movhi
	mvn	r2, #37
	b	.L2472
.L2434:
	cmp	r3, #220
	bne	.L2435
	ldr	r3, .L2475+16
	movw	r2, #3050
	mov	r1, #4096
	strh	r1, [r3, r2]	@ movhi
	mvn	r2, #35
.L2472:
	strb	r2, [r3, #3038]
.L2435:
	ldr	r1, .L2475+24
	mov	r2, #32
	ldr	r0, .L2475+28
	bl	memcpy
	ldr	r0, .L2475+12
	mov	r2, #32
	add	r1, r0, #144
	bl	memcpy
.L2431:
	ldr	r4, .L2475+4
	ldrb	r3, [r4, #852]	@ zero_extendqisi2
	cmp	r3, #0
	bne	.L2436
	bl	FlashLoadPhyInfoInRam
	cmp	r0, #0
	bne	.L2437
	ldr	r3, [r4, #3624]
	ldrh	r0, [r3, #16]
	ubfx	r0, r0, #8, #3
	strb	r0, [r4, #3772]
	tst	r0, #1
	bne	.L2437
	mov	r3, #1
	strb	r3, [r4, #3780]
	bl	FlashSetInterfaceMode
	ldrb	r0, [r4, #3772]	@ zero_extendqisi2
	bl	NandcSetMode
.L2437:
	ldr	r4, .L2475+4
	ldr	r3, [r4, #3624]
	ldrb	r3, [r3, #26]	@ zero_extendqisi2
	strb	r3, [r4, #928]
	bl	FlashLoadPhyInfo
	cmp	r0, #0
	beq	.L2436
	ldr	r3, [r4, #3784]
	cmp	r3, #0
	beq	.L2439
	mov	r0, #1
	bl	FlashSetInterfaceMode
	mov	r0, #1
	b	.L2473
.L2439:
	ldrb	r0, [r4, #3772]	@ zero_extendqisi2
	bl	FlashSetInterfaceMode
	ldrb	r0, [r4, #3772]	@ zero_extendqisi2
.L2473:
	bl	NandcSetMode
	bl	FlashLoadPhyInfo
	cmp	r0, #0
	beq	.L2436
	ldr	r4, .L2475+4
	mov	r0, #1
	bl	FlashSetInterfaceMode
	mov	r0, #1
	bl	NandcSetMode
	ldr	r0, .L2475+32
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
	ldr	r1, .L2475+36
	ldr	r0, .L2475
	ldr	r3, [r4, #3624]
	ldrh	r1, [r0, r1]
	ldrb	r2, [r3, #9]	@ zero_extendqisi2
	add	r1, r1, #4080
	add	r1, r1, #15
	cmp	r2, r1, lsr #12
	blt	.L2442
	ldrh	r1, [r3, #14]
	add	r1, r1, #255
	cmp	r2, r1, lsr #8
	bge	.L2443
.L2442:
	ldrh	r2, [r3, #14]
	bic	r2, r2, #255
	strh	r2, [r3, #14]	@ movhi
.L2443:
	ldr	r3, .L2475+4
	ldrb	r3, [r3, #3772]	@ zero_extendqisi2
	tst	r3, #6
	beq	.L2444
	bl	FlashSavePhyInfo
	ldr	r3, .L2475
	mov	r0, #0
	ldr	r1, [r3, #160]
	bl	FlashDdrParaScan
.L2444:
	bl	FlashSavePhyInfo
.L2436:
	ldr	r4, .L2475+4
	ldr	r5, .L2475
	ldr	r3, [r4, #3624]
	ldrb	r2, [r3, #26]	@ zero_extendqisi2
	ldrh	r0, [r3, #10]
	ldrb	r6, [r3, #18]	@ zero_extendqisi2
	strb	r2, [r4, #928]
	ldrh	r2, [r3, #16]
	ubfx	r1, r2, #7, #1
	strb	r1, [r4, #860]
	ubfx	r1, r2, #3, #1
	strb	r1, [r5, #176]
	ubfx	r1, r2, #4, #1
	ubfx	r2, r2, #8, #3
	strb	r1, [r4, #3837]
	strb	r2, [r4, #3772]
	mov	r2, #0
	ldrb	r1, [r3, #12]	@ zero_extendqisi2
	str	r2, [r5, #172]
	bl	__aeabi_idiv
	mov	r1, r0
	mov	r0, r6
	bl	BuildFlashLsbPageTable
	bl	FlashDieInfoInit
	ldr	r3, [r4, #3624]
	ldrh	r2, [r3, #16]
	tst	r2, #64
	beq	.L2445
	ldrb	r0, [r3, #19]	@ zero_extendqisi2
	ldrb	r3, [r4, #1]	@ zero_extendqisi2
	strb	r0, [r4, #3761]
	strb	r3, [r4, #3760]
	ldrb	r3, [r4, #2]	@ zero_extendqisi2
	strb	r3, [r5, #140]
	sub	r3, r0, #1
	uxtb	r3, r3
	cmp	r3, #6
	bhi	.L2446
	ldr	r3, .L2475+40
	str	r3, [r5, #172]
	sub	r3, r0, #5
	uxtb	r3, r3
	cmp	r3, #1
	movls	r3, #1
	strls	r3, [r4, #3828]
	bls	.L2448
	cmp	r0, #7
	addeq	r4, r4, #28
	beq	.L2449
.L2448:
	ldr	r4, .L2475+44
.L2449:
	mov	r3, #0
	mov	r2, r3
.L2451:
	ldrsb	r1, [r4, r2]
	add	r2, r2, #1
	cmp	r1, #0
	addeq	r3, r3, #1
	cmp	r2, #32
	bne	.L2451
	cmp	r3, #27
	bls	.L2445
	bl	FlashGetReadRetryDefault
	bl	FlashSavePhyInfo
	b	.L2445
.L2446:
	sub	r3, r0, #17
	uxtb	r3, r3
	cmp	r3, #2
	bhi	.L2452
	ldr	r3, .L2475+48
	cmp	r0, #19
	str	r3, [r5, #172]
	mov	r3, #7
	strb	r3, [r5, #140]
	moveq	r3, #15
	streqb	r3, [r5, #140]
	b	.L2445
.L2452:
	cmp	r0, #33
	cmpne	r0, #65
	beq	.L2453
	cmp	r0, #66
	bne	.L2454
.L2453:
	ldr	r3, .L2475
	mov	r1, #4
	ldr	r2, .L2475+52
	str	r2, [r3, #172]
	ldr	r2, .L2475+4
	strb	r1, [r2, #3760]
	mov	r2, #7
	strb	r2, [r3, #140]
	b	.L2445
.L2454:
	cmp	r0, #67
	cmpne	r0, #34
	beq	.L2455
	cmp	r0, #35
	beq	.L2455
	cmp	r0, #68
	bne	.L2456
.L2455:
	ldr	r3, .L2475
	cmp	r0, #35
	cmpne	r0, #68
	ldr	r2, .L2475+52
	sub	r0, r0, #67
	uxtb	r0, r0
	str	r2, [r3, #172]
	mov	r2, #7
	strb	r2, [r3, #140]
	moveq	r2, #17
	streqb	r2, [r3, #140]
	cmp	r0, #1
	ldr	r3, .L2475+4
	movls	r2, #4
	movhi	r2, #5
	strb	r2, [r3, #3760]
	b	.L2445
.L2456:
	cmp	r0, #49
	ldreq	r3, .L2475+56
	streq	r3, [r5, #172]
.L2445:
	ldr	r2, .L2475+4
	ldr	r3, .L2475+20
	ldr	r1, [r2, #3776]
	cmp	r1, r3
	bne	.L2459
	ldrb	r3, [r2, #928]	@ zero_extendqisi2
	cmp	r3, #0
	ldrne	r3, [r2, #3624]
	movne	r1, #0
	strneb	r1, [r3, #18]
.L2459:
	ldrb	r2, [r2, #2980]	@ zero_extendqisi2
	ldr	r3, .L2475+4
	cmp	r2, #44
	bne	.L2460
	ldrb	r2, [r3, #3780]	@ zero_extendqisi2
	cmp	r2, #0
	beq	.L2460
	ldr	r1, [r3, #3776]
	ldr	r2, .L2475+20
	cmp	r1, r2
	bne	.L2461
	ldrb	r3, [r3, #928]	@ zero_extendqisi2
	cmp	r3, #0
	bne	.L2460
.L2461:
	ldr	r3, .L2475+4
	mov	r2, #0
	mov	r0, #1
	strb	r2, [r3, #3780]
	bl	FlashSetInterfaceMode
	mov	r0, #1
	bl	NandcSetMode
.L2460:
	mov	r0, #0
	bl	flash_enter_slc_mode
	ldr	r2, .L2475+4
	ldrb	r3, [r2, #3772]	@ zero_extendqisi2
	tst	r3, #6
	beq	.L2462
	ldrb	r2, [r2, #3780]	@ zero_extendqisi2
	cmp	r2, #0
	bne	.L2463
	tst	r3, #1
	bne	.L2462
.L2463:
	ldr	r3, .L2475
	mov	r0, #0
	ldr	r1, [r3, #160]
	bl	FlashDdrParaScan
.L2462:
	ldr	r4, .L2475+4
	mov	r0, #0
	bl	flash_exit_slc_mode
	mov	r8, #16
	movw	r6, #3062
	ldr	r3, [r4, #3624]
	ldrb	r0, [r3, #20]	@ zero_extendqisi2
	bl	FlashBchSel
	add	r0, r4, #932
	bl	FlashReadIdbDataRaw
	ldr	r0, .L2475+60
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
	bne	.L2465
	mov	r7, r7, lsr #1
	mov	ip, ip, asl #1
	mov	r1, r1, asl #1
	strb	r8, [r4, #853]
	strh	r1, [r4, r2]	@ movhi
	mov	r2, #8
	strh	r7, [r4, r6]	@ movhi
	strh	ip, [r4, r0]	@ movhi
	strh	r2, [r4, r3]	@ movhi
.L2465:
	ldrb	r0, [r5, #20]	@ zero_extendqisi2
	bl	FlashBchSel
	bl	ftl_flash_suspend
	mov	r0, #0
	ldmfd	sp!, {r4, r5, r6, r7, r8, pc}
.L2466:
	mvn	r0, #1
	ldmfd	sp!, {r4, r5, r6, r7, r8, pc}
.L2467:
	mvn	r0, #1
	ldmfd	sp!, {r4, r5, r6, r7, r8, pc}
.L2476:
	.align	2
.L2475:
	.word	.LANCHOR2
	.word	.LANCHOR0
	.word	.LANCHOR0+2980
	.word	.LANCHOR1+2892
	.word	.LANCHOR1
	.word	1446522928
	.word	.LANCHOR1+2796
	.word	.LANCHOR0+896
	.word	.LC144
	.word	-894
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
	ldr	r4, .L2481
	mov	r5, #0
	add	r1, r4, #464
	str	r5, [r4, #132]
	str	r5, [r4, #464]
	str	r0, [r4, #460]
	add	r0, r4, #132
	bl	rknand_get_reg_addr
	ldr	r3, [r4, #132]
	cmp	r3, r5
	mvneq	r4, #0
	beq	.L2478
	bl	rk_nandc_irq_init
	ldr	r0, [r4, #460]
	mov	r1, r5
	mov	r2, r5
	mov	r3, #2048
	bl	FlashSramLoadStore
	bl	rknand_flash_cs_init
	ldr	r0, [r4, #132]
	bl	FlashInit
	subs	r4, r0, #0
	bne	.L2479
	bl	FtlInit
.L2479:
	ldr	r0, .L2481+4
	mov	r1, r4
	bl	printk
.L2478:
	mov	r0, r4
	ldmfd	sp!, {r3, r4, r5, pc}
.L2482:
	.align	2
.L2481:
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
	ldr	r3, .L2508
	stmfd	sp!, {r4, r5, lr}
	.save {r4, r5, lr}
	.pad #20
	sub	sp, sp, #20
	ldrb	r4, [r3, #928]	@ zero_extendqisi2
	cmp	r4, #0
	movne	r0, #0
	bne	.L2484
	ldr	r5, .L2508+4
	ldr	r0, [r5, #132]
	bl	FlashInit
	cmp	r0, #0
	movne	r0, r4
	bne	.L2484
	bl	FlashLoadFactorBbt
	cmp	r0, #0
	beq	.L2485
	bl	FlashMakeFactorBbt
.L2485:
	ldr	r0, [r5, #136]
	bl	FlashReadIdbDataRaw
	cmp	r0, #0
	beq	.L2486
	mov	r1, #0
	mov	r2, #16
	mov	r0, sp
	bl	FlashReadFacBbtData
	ldr	r1, [sp, #0]
	mov	r3, #0
	mov	r0, #1
	mov	r2, r3
.L2488:
	ands	ip, r1, r0, asl r2
	add	r2, r2, #1
	addne	r3, r3, #1
	cmp	r2, #16
	bne	.L2488
	cmp	r3, #6
	ldrls	r3, .L2508
	bls	.L2504
	mov	r2, #0
	mov	r0, #1
.L2489:
	ands	ip, r1, r0, asl r2
	add	r2, r2, #1
	addne	r3, r3, #1
	cmp	r2, #24
	bne	.L2489
	cmp	r3, #17
	ldr	r3, .L2508
	movhi	r2, #36
.L2504:
	strb	r2, [r3, #853]
	movw	r2, #3074
	ldr	r3, .L2508
	ldrb	r1, [r3, #853]	@ zero_extendqisi2
	strh	r1, [r3, r2]	@ movhi
.L2486:
	ldr	r1, .L2508+8
	mov	r4, #1
	ldr	r0, .L2508+12
	bl	printk
	ldr	r0, .L2508+16
	bl	FtlConstantsInit
	bl	FtlVariablesInit
	ldr	r3, .L2508
	ldr	r0, [r3, #3852]
	uxth	r0, r0
	bl	FtlFreeSysBlkQueueInit
	b	.L2493
.L2495:
	add	r4, r4, #1
.L2493:
	bl	FtlLoadBbt
	cmp	r0, #0
	bne	.L2506
.L2494:
	bl	FtlSysBlkInit
	cmp	r0, #0
	beq	.L2496
.L2506:
	bl	FtlLowFormat
	cmp	r4, #3
	bls	.L2495
	b	.L2507
.L2496:
	ldr	r3, .L2508+20
	mov	r2, #1
	str	r2, [r3, #2996]
	b	.L2484
.L2507:
	mvn	r0, #0
.L2484:
	add	sp, sp, #20
	ldmfd	sp!, {r4, r5, pc}
.L2509:
	.align	2
.L2508:
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
	ldr	r3, .L2512
	ldr	r2, [r3, #3776]
	ldr	r3, .L2512+4
	cmp	r2, r3
	bxne	lr
	b	flash_enter_slc_mode
.L2513:
	.align	2
.L2512:
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
	ldr	r3, .L2516
	ldr	r2, [r3, #3776]
	ldr	r3, .L2516+4
	cmp	r2, r3
	bxne	lr
	b	flash_exit_slc_mode
.L2517:
	.align	2
.L2516:
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
	ldr	r5, .L2522
	mov	fp, r1
	.pad #28
	sub	sp, sp, #28
	mov	r1, r4
	mov	sl, r2
	ldr	r0, .L2522+4
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
	b	.L2519
.L2521:
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
	beq	.L2520
	ldr	r0, [r5, #3776]
	ldr	lr, .L2522+8
	cmp	r0, lr
	moveq	r3, r1
.L2520:
	ldr	r1, [sp, #16]
	add	r2, r2, r1
	mla	r2, r7, r3, r2
	ldr	r3, [r9, #3624]
	ldrb	r1, [r3, #9]	@ zero_extendqisi2
	ldrb	r3, [r9, #3836]	@ zero_extendqisi2
	str	r3, [sp, #20]
	ldr	r3, .L2522+12
	ldrb	r0, [r3, #152]	@ zero_extendqisi2
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
.L2519:
	cmp	r6, fp
	bcc	.L2521
	mov	r1, r4
	mov	r2, fp
	mov	r3, #0
	ldr	r0, .L2522+16
	bl	printk
	mov	r0, #0
	add	sp, sp, #28
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, sl, fp, pc}
.L2523:
	.align	2
.L2522:
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
	@ args = 0, pretend = 0, frame = 88
	@ frame_needed = 0, uses_anonymous_args = 0
	ldr	r3, .L2532
	stmfd	sp!, {r4, r5, r6, r7, r8, r9, sl, fp, lr}
	.save {r4, r5, r6, r7, r8, r9, sl, fp, lr}
	mov	r5, r0
	ldr	r3, [r3, #3624]
	mov	fp, r1
	.pad #92
	sub	sp, sp, #92
	mov	r1, r5
	mov	sl, r2
	ldr	r0, .L2532+4
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
	bls	.L2525
	mov	r0, #0
	add	r1, r7, r5
	mov	r2, r0
	mov	r8, #2
	bl	FlashEraseBlock
.L2525:
	ldr	r4, .L2532
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
	b	.L2526
.L2530:
	add	r3, r8, r7
	ubfx	r3, r3, #2, #16
	cmp	r3, #0
	beq	.L2527
	add	r1, r3, #1
	ldrb	r0, [r4, #928]	@ zero_extendqisi2
	add	r2, r4, r1, asl #1
	cmp	r0, #0
	add	r2, r2, #3072
	add	r2, r2, #4
	ldrh	r2, [r2, #0]
	beq	.L2528
	ldr	r0, [r4, #3776]
	ldr	ip, .L2532+8
	cmp	r0, ip
	moveq	r2, r1
.L2528:
	sub	r2, r2, #-1073741823
	mov	r2, r2, asl #2
	str	r2, [sp, #24]
.L2527:
	movw	r2, #61424
	str	r2, [sp, #28]
	add	r2, r4, r3, asl #1
	add	r2, r2, #3072
	ldrh	r5, [r2, #4]
	ldrb	r2, [r4, #928]	@ zero_extendqisi2
	cmp	r2, #0
	beq	.L2529
	ldr	r2, [r4, #3776]
	ldr	r1, .L2532+8
	cmp	r2, r1
	moveq	r5, r3
.L2529:
	ldr	ip, [sp, #12]
	add	r8, r8, #4
	ldr	r2, .L2532+12
	uxth	r8, r8
	mla	r3, r6, r5, ip
	ldrb	ip, [r9, #3836]	@ zero_extendqisi2
	ldrb	r0, [r2, #152]	@ zero_extendqisi2
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
.L2526:
	ldr	ip, [sp, #16]
	cmp	r8, ip
	bcc	.L2530
	ldr	r5, [sp, #20]
	mov	r2, fp
	mov	r3, #0
	ldr	r0, .L2532+16
	mov	r1, r5
	bl	printk
	mov	r0, #0
	add	sp, sp, #92
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, sl, fp, pc}
.L2533:
	.align	2
.L2532:
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
	@ args = 0, pretend = 0, frame = 24
	@ frame_needed = 0, uses_anonymous_args = 0
	ldr	r3, .L2558
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
	ldrh	r8, [r3, #10]
	bl	ftl_malloc
	subs	sl, r0, #0
	beq	.L2552
	add	r5, r5, #508
	add	r5, r5, #3
	mov	r5, r5, lsr #9
	cmp	r5, #8
	movls	r5, #8
	bls	.L2536
	cmp	r5, #500
	bhi	.L2552
.L2536:
	mul	r4, r8, r4
	mov	r0, r5
	uxth	r4, r4
	str	r4, [sp, #12]
	mov	r1, r4
	bl	__aeabi_uidiv
	mov	r3, #0
	movw	r2, #63999
	movw	ip, #4097
	str	r0, [sp, #20]
	add	r0, r6, #254976
	add	r0, r0, #1020
.L2541:
	ldr	r1, [r0], #-4
	cmp	r1, #0
	bne	.L2537
	ldr	r1, [r6, r3, asl #2]
	add	r3, r3, #1
	cmp	r3, ip
	sub	r2, r2, #1
	movcs	r3, #0
	cmp	r2, #4096
	str	r1, [r0, #4]
	bne	.L2541
	b	.L2540
.L2537:
	ldr	r0, .L2558+4
	bl	printk
.L2540:
	ldr	r4, .L2558
	mov	r1, r7
	mov	r3, #5
	ldr	r0, .L2558+8
	mov	r2, #4
	mov	r8, r5
	bl	rknand_print_hex
	ldrb	r2, [r4, #853]	@ zero_extendqisi2
	ldr	r1, [r6, #512]
	ldr	r0, .L2558+12
	bl	printk
	ldr	r2, .L2558+16
	movw	r3, #3074
	mov	r1, r5
	ldrh	r3, [r4, r3]
	ldr	r2, [r2, #156]
	ldr	r0, .L2558+20
	str	r2, [sp, #0]
	mov	r2, r5
	bl	printk
	ldrb	r3, [r4, #853]	@ zero_extendqisi2
	ldr	r2, [r6, #512]
	mov	r4, r7
	mov	r7, r6
	mov	r1, r5, asl #7
	cmp	r2, r3
	mov	r2, #0
	mov	r9, r2
	strhi	r3, [r6, #512]
	str	r1, [sp, #24]
	str	r2, [sp, #16]
.L2549:
	ldr	r1, .L2558
	ldr	r3, [r4], #4
	ldrb	r2, [r1, #853]	@ zero_extendqisi2
	cmp	r3, r2
	bcs	.L2543
	ldr	r1, .L2558+16
	ldr	r2, [r1, #156]
	cmp	r3, r2
	bcc	.L2543
	ldr	r2, [sp, #20]
	ldr	r1, [sp, #20]
	cmp	r2, #1
	movls	r2, #0
	movhi	r2, #1
	cmp	r9, #0
	cmpne	r1, #1
	str	r2, [sp, #28]
	bls	.L2544
	ldr	r2, [r4, #-8]
	add	r2, r2, #1
	cmp	r3, r2
	beq	.L2543
.L2544:
	mov	r0, sl
	mov	r1, #512
	bl	__memzero
	ldr	r2, [sp, #12]
	ldr	r0, [r4, #-4]
	mov	r1, r8
	mov	r6, #0
	mul	r0, r0, r2
	mov	r2, r7
	bl	IDBlockWriteData
	ldr	r3, [sp, #12]
	ldr	r0, [r4, #-4]
	mov	r2, sl
	mov	r1, r8
	mul	r0, r0, r3
	bl	IdBlockReadData
	mov	r2, sl
	mov	r3, r7
.L2548:
	mov	fp, r2
	mov	r5, r3
	ldr	r0, [fp, #0]
	add	r2, r2, #4
	ldr	r1, [r5, #0]
	add	r3, r3, #4
	cmp	r0, r1
	beq	.L2545
	mov	r0, sl
	mov	r1, #512
	bl	__memzero
	ldr	r3, [fp, #0]
	mov	r1, r9
	ldr	r0, .L2558+24
	str	r3, [sp, #0]
	ldr	r3, [r5, #0]
	bic	r5, r6, #255
	mov	r5, r5, asl #2
	str	r3, [sp, #4]
	mov	r3, r6
	ldr	r2, [r4, #-4]
	bl	printk
	ldr	r0, .L2558+28
	add	r1, r7, r5
	mov	r2, #4
	mov	r3, #256
	bl	rknand_print_hex
	mov	r3, #256
	add	r1, sl, r5
	mov	r2, #4
	ldr	r0, .L2558+32
	bl	rknand_print_hex
	mov	r0, #0
	bl	flash_boot_enter_slc_mode
	ldr	r2, [sp, #12]
	ldr	r1, [r4, #-4]
	mov	r0, #0
	mul	r1, r1, r2
	mov	r2, r0
	bl	FlashEraseBlock
	ldr	r3, [sp, #28]
	cmp	r3, #0
	beq	.L2546
	ldr	r1, [r4, #-4]
	mov	r0, #0
	ldr	r2, [sp, #12]
	add	r1, r1, #1
	mul	r1, r2, r1
	mov	r2, r0
	bl	FlashEraseBlock
.L2546:
	mov	r0, #0
	bl	flash_boot_exit_slc_mode
	ldr	r0, .L2558+36
	ldr	r1, [r4, #-4]
	bl	printk
	ldr	r3, [sp, #24]
	cmp	r6, r3
	bcc	.L2543
	b	.L2547
.L2545:
	ldr	r1, [sp, #24]
	add	r6, r6, #1
	cmp	r6, r1
	bne	.L2548
.L2547:
	ldr	r2, [sp, #16]
	add	r2, r2, #1
	str	r2, [sp, #16]
.L2543:
	add	r9, r9, #1
	cmp	r9, #5
	bne	.L2549
	mov	r0, sl
	bl	ftl_free
	ldr	r3, [sp, #16]
	cmp	r3, #0
	mvneq	r0, #0
	movne	r0, #0
	b	.L2535
.L2552:
	mvn	r0, #0
.L2535:
	add	sp, sp, #36
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, sl, fp, pc}
.L2559:
	.align	2
.L2558:
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
	ldr	ip, .L2563
	b	.L2561
.L2562:
	ldrb	r4, [r3, r2]	@ zero_extendqisi2
	add	r2, r2, #1
	eor	r4, r4, r0, lsr #24
	add	r4, ip, r4, asl #2
	ldr	r4, [r4, #3068]
	eor	r0, r4, r0, asl #8
.L2561:
	cmp	r2, r1
	bne	.L2562
	ldmfd	sp!, {r4, pc}
.L2564:
	.align	2
.L2563:
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
	ldr	r3, .L2681
	stmfd	sp!, {r4, r5, r6, lr}
	.save {r4, r5, r6, lr}
	cmp	r1, r3
	.pad #528
	sub	sp, sp, #528
	mov	r5, r1
	mov	r4, r2
	beq	.L2574
	bhi	.L2583
	ldr	r3, .L2681+4
	cmp	r1, r3
	beq	.L2571
	bhi	.L2584
	sub	r3, r3, #125
	cmp	r1, r3
	beq	.L2568
	bhi	.L2585
	sub	r3, r3, #237
	cmp	r1, r3
	bne	.L2627
	b	.L2677
.L2585:
	ldr	r3, .L2681+8
	cmp	r1, r3
	beq	.L2569
	add	r3, r3, #1
	cmp	r1, r3
	bne	.L2627
	b	.L2678
.L2584:
	ldr	r3, .L2681+12
	cmp	r1, r3
	beq	.L2573
	bhi	.L2586
	ldr	r3, .L2681+16
	cmp	r1, r3
	bne	.L2627
	b	.L2679
.L2586:
	ldr	r3, .L2681+20
	cmp	r1, r3
	beq	.L2573
	add	r3, r3, #10
	cmp	r1, r3
	bne	.L2627
	b	.L2573
.L2583:
	ldr	r3, .L2681+24
	cmp	r1, r3
	beq	.L2579
	bhi	.L2587
	sub	r3, r3, #78
	cmp	r1, r3
	beq	.L2576
	bcc	.L2575
	add	r3, r3, #21
	cmp	r1, r3
	beq	.L2577
	add	r3, r3, #56
	cmp	r1, r3
	bne	.L2627
	b	.L2680
.L2587:
	ldr	r3, .L2681+28
	cmp	r1, r3
	beq	.L2582
	bhi	.L2588
	sub	r3, r3, #956
	sub	r3, r3, #1
	cmp	r1, r3
	beq	.L2580
	add	r3, r3, #956
	cmp	r1, r3
	bne	.L2627
	b	.L2581
.L2588:
	ldr	r3, .L2681+32
	cmp	r1, r3
	beq	.L2581
	add	r3, r3, #1
	cmp	r1, r3
	bne	.L2627
	b	.L2582
.L2575:
	ldr	r0, .L2681+36
	bl	printk
	mov	r0, #4096
	bl	ftl_malloc
	subs	r5, r0, #0
	beq	.L2634
	mov	r1, r4
	mov	r2, #512
	bl	rk_copy_from_user
	cmp	r0, #0
	bne	.L2676
.L2589:
	ldr	r0, .L2681+40
	ldmia	r5, {r1, r2}
	bl	printk
	ldr	r3, [r5, #4]
	cmp	r3, #8
	str	r3, [sp, #4]
	bhi	.L2598
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
	beq	.L2662
	ldr	r0, .L2681+44
.L2675:
	bl	printk
	b	.L2598
.L2576:
	ldr	r0, .L2681+48
	bl	printk
	mov	r0, #4096
	bl	ftl_malloc
	subs	r5, r0, #0
	beq	.L2634
	mov	r1, r4
	mov	r2, #4096
	bl	rk_copy_from_user
	cmp	r0, #0
	bne	.L2676
.L2593:
	ldr	r4, .L2681+52
	ldr	r0, .L2681+56
	ldmia	r5, {r1, r2}
	bl	printk
	ldr	r3, [r4, #468]
	cmp	r3, #0
	bne	.L2594
	mov	r0, #260096
	bl	ftl_malloc
	cmp	r0, #0
	str	r0, [r4, #468]
	beq	.L2598
.L2595:
	mov	r1, #260096
	bl	__memzero
.L2594:
	ldr	r2, [r5, #4]
	movw	r3, #4088
	cmp	r2, r3
	bhi	.L2598
	ldr	r3, [r5, #0]
	cmp	r3, #251904
	bhi	.L2598
	ldr	r1, .L2681+52
	ldr	r0, [r1, #468]
	add	r1, r5, #8
	add	r0, r0, r3
	bl	memcpy
.L2662:
	mov	r0, r5
	bl	ftl_free
.L2663:
	mov	r4, #0
	b	.L2592
.L2680:
	ldr	r0, .L2681+60
	bl	printk
	mov	r0, #4096
	bl	ftl_malloc
	subs	r5, r0, #0
	beq	.L2634
	mov	r1, r4
	mov	r2, #28
	bl	rk_copy_from_user
	cmp	r0, #0
	beq	.L2597
.L2676:
	ldr	r0, .L2681+64
	b	.L2675
.L2597:
	ldmia	r5, {r1, r2}
	ldr	r0, .L2681+68
	bl	printk
	ldr	r1, [r5, #0]
	cmp	r1, #256000
	bhi	.L2598
	ldr	r4, .L2681+52
	ldr	r0, [r4, #468]
	cmp	r0, #0
	beq	.L2598
.L2599:
	bl	CRC_32
	ldr	r3, [r5, #4]
	cmp	r3, r0
	beq	.L2600
	mov	r0, r5
	bl	ftl_free
	b	.L2640
.L2600:
	bl	rknand_device_lock
	ldr	r1, [r4, #468]
	add	r2, r5, #8
	ldr	r0, [r5, #0]
	bl	write_idblock
	mov	r6, #0
	bl	rknand_device_unlock
	ldr	r0, [r4, #468]
	bl	ftl_free
	str	r6, [r4, #468]
	mov	r0, r5
	mov	r4, r6
	bl	ftl_free
	b	.L2592
.L2598:
	mov	r0, r5
	b	.L2666
.L2577:
	ldr	r0, .L2681+72
	bl	printk
	mov	r0, #4096
	bl	ftl_malloc
	subs	r5, r0, #0
	beq	.L2634
	bl	ftl_read_flash_info
	mov	r0, r4
	mov	r1, r5
	mov	r2, #11
	b	.L2671
.L2574:
	ldr	r0, .L2681+76
	bl	printk
	bl	nand_blk_add_whole_disk
	mov	r0, #4096
	bl	ftl_malloc
	subs	r5, r0, #0
	beq	.L2634
	bl	rknand_device_lock
	mov	r1, #0
	mov	r2, #64
	mov	r0, r5
	bl	FlashReadFacBbtData
	bl	rknand_device_unlock
	ldr	r0, .L2681+80
	mov	r1, r5
	mov	r2, #4
	mov	r3, #8
	bl	rknand_print_hex
	mov	r0, r4
	mov	r1, r5
	mov	r2, #64
	b	.L2671
.L2579:
	ldr	r0, .L2681+84
	bl	printk
	ldr	r3, .L2681+52
	add	r1, sp, #528
	mov	r0, r4
	mov	r2, #4
	ldr	r3, [r3, #472]
	ldr	r3, [r3, #20]
	str	r3, [r1, #-524]!
	b	.L2667
.L2580:
	ldr	r0, .L2681+88
	bl	printk
	mov	r0, #4096
	bl	ftl_malloc
	subs	r5, r0, #0
	beq	.L2634
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
.L2671:
	bl	rk_copy_to_user
	subs	r4, r0, #0
	mov	r0, r5
	beq	.L2603
.L2666:
	bl	ftl_free
	b	.L2657
.L2603:
	bl	ftl_free
	b	.L2592
.L2568:
	ldr	r0, .L2681+92
	bl	printk
	add	r0, sp, #8
	mov	r1, r4
	mov	r2, #520
	bl	rk_copy_from_user
	subs	r5, r0, #0
	bne	.L2668
.L2604:
	ldr	r2, [sp, #8]
	ldr	r3, .L2681+96
	cmp	r2, r3
	bne	.L2659
	ldr	r3, [sp, #12]
	cmp	r3, #512
	bhi	.L2659
	ldr	r6, .L2681+52
	mov	r2, #512
	add	r0, sp, #8
	ldr	r1, [r6, #472]
	bl	memcpy
	ldr	r2, [r6, #476]
	ldr	r3, .L2681+100
	cmp	r2, r3
	beq	.L2605
	add	r0, sp, #72
	mov	r1, #128
	str	r5, [sp, #16]
	str	r5, [sp, #20]
	bl	__memzero
.L2605:
	add	r0, sp, #264
	mov	r1, #256
	mov	r3, #0
	str	r3, [sp, #24]
	bl	__memzero
	b	.L2674
.L2569:
	ldr	r0, .L2681+104
	bl	printk
	add	r0, sp, #8
	mov	r1, r4
	mov	r2, #520
	bl	rk_copy_from_user
	cmp	r0, #0
	bne	.L2668
.L2606:
	ldr	r2, [sp, #8]
	ldr	r3, .L2681+96
	cmp	r2, r3
	bne	.L2659
	ldr	r3, [sp, #12]
	cmp	r3, #512
	bhi	.L2659
	ldr	r4, .L2681+52
	ldr	r3, .L2681+100
	ldr	r2, [r4, #476]
	cmp	r2, r3
	bne	.L2640
	ldr	r3, [sp, #20]
	sub	r2, r3, #1
	cmp	r2, #127
	mvnhi	r4, #2
	bhi	.L2566
	ldr	r0, [r4, #472]
	add	r1, sp, #72
	str	r3, [r0, #12]
	add	r0, r0, #64
	ldr	r2, [sp, #20]
	bl	memcpy
	mov	r0, #1
	ldr	r1, [r4, #472]
	b	.L2664
.L2679:
	ldr	r0, .L2681+108
	bl	printk
	add	r0, sp, #8
	mov	r1, r4
	mov	r2, #520
	bl	rk_copy_from_user
	cmp	r0, #0
	bne	.L2668
.L2607:
	ldr	r2, [sp, #8]
	ldr	r3, .L2681+112
	cmp	r2, r3
	bne	.L2659
	ldr	r3, [sp, #12]
	cmp	r3, #512
	bhi	.L2659
	ldr	r5, .L2681+52
	ldr	r4, [r5, #480]
	cmp	r4, #0
	beq	.L2566
	ldr	r3, [r5, #484]
	ldr	r2, .L2681+116
	ldr	r1, [r3, #0]
	cmp	r1, r2
	beq	.L2608
	str	r2, [r3, #0]
	mov	r2, #504
	ldr	r3, [r5, #484]
	str	r2, [r3, #4]
	mov	r2, #0
	str	r2, [r3, #8]
	str	r2, [r3, #12]
.L2608:
	ldr	r1, [r5, #484]
	mov	r4, #0
	mov	r0, r4
	str	r4, [r1, #16]
	bl	StorageSysDataStore
	ldr	r3, [r5, #472]
	ldr	r2, .L2681+96
	ldr	r5, .L2681+52
	ldr	r1, [r3, #0]
	cmp	r1, r2
	strne	r2, [r3, #0]
	ldr	r0, [r5, #472]
	mov	r1, #128
	ldrne	r3, .L2681+52
	movne	r2, #504
	ldrne	r3, [r3, #472]
	stmneib	r3, {r2, r4}
	mov	r4, #0
	str	r4, [r0, #12]
	add	r0, r0, #64
	bl	__memzero
	mov	r0, #1
	ldr	r1, [r5, #472]
	bl	StorageSysDataStore
	str	r4, [r5, #480]
	str	r4, [r5, #476]
	b	.L2592
.L2571:
	ldr	r0, .L2681+120
	bl	printk
	mov	r1, r4
	add	r0, sp, #8
	mov	r2, #520
	bl	rk_copy_from_user
	subs	r4, r0, #0
	bne	.L2668
.L2610:
	ldr	r2, [sp, #8]
	ldr	r3, .L2681+124
	cmp	r2, r3
	bne	.L2659
	ldr	r3, [sp, #12]
	cmp	r3, #512
	bhi	.L2659
	ldr	r5, .L2681+52
	ldr	r3, [r5, #480]
	cmp	r3, #1
	beq	.L2566
	ldr	r3, [r5, #484]
	mov	r0, #0
	ldr	r2, .L2681+116
	ldr	r1, [r3, #0]
	cmp	r1, r2
	strne	r2, [r3, #0]
	ldr	r1, [r5, #484]
	ldrne	r3, [r5, #484]
	movne	r2, #504
	strne	r4, [r3, #12]
	stmneib	r3, {r2, r4}
	mov	r3, #1
	str	r3, [r1, #16]
	bl	StorageSysDataStore
	ldr	r3, [r5, #472]
	ldr	r2, .L2681+96
	ldr	r1, [r3, #0]
	cmp	r1, r2
	beq	.L2612
	str	r2, [r3, #0]
	mov	r1, #504
	ldr	r3, .L2681+52
	mov	r2, #0
	ldr	r3, [r3, #472]
	stmib	r3, {r1, r2}
.L2612:
	ldr	r5, .L2681+52
	mov	r1, #128
	mov	r4, #0
	ldr	r0, [r5, #472]
	str	r4, [r0, #12]
	add	r0, r0, #64
	bl	__memzero
	mov	r0, #1
	ldr	r1, [r5, #472]
	bl	StorageSysDataStore
	mov	r3, #1
	str	r3, [r5, #480]
	b	.L2592
.L2678:
	ldr	r0, .L2681+128
	bl	printk
	add	r0, sp, #8
	mov	r1, r4
	mov	r2, #520
	bl	rk_copy_from_user
	cmp	r0, #0
	bne	.L2668
.L2613:
	ldr	r2, [sp, #8]
	ldr	r3, .L2681+132
	cmp	r2, r3
	bne	.L2659
	ldr	r2, [sp, #12]
	cmp	r2, #512
	addls	r0, sp, #16
	ldrls	r1, .L2681+136
	bls	.L2673
	b	.L2659
.L2573:
	ldr	r3, .L2681+20
	cmp	r5, r3
	ldreq	r0, .L2681+140
	beq	.L2660
	ldr	r3, .L2681+144
	cmp	r5, r3
	ldreq	r0, .L2681+148
	ldrne	r0, .L2681+152
.L2660:
	bl	printk
	add	r0, sp, #8
	mov	r1, r4
	mov	r2, #520
	bl	rk_copy_from_user
	cmp	r0, #0
	bne	.L2668
.L2617:
	ldr	r2, [sp, #8]
	ldr	r3, .L2681+156
	cmp	r2, r3
	bne	.L2657
	ldr	r3, .L2681+144
	ldr	r6, .L2681+52
	cmp	r5, r3
	bne	.L2618
	ldr	r3, [r6, #472]
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
	b	.L2566
.L2618:
	ldr	r3, [r6, #1000]
	cmp	r3, #10
	bhi	.L2657
	ldr	r3, [r6, #472]
	ldr	r1, [sp, #12]
	ldr	r2, [r3, #24]
	cmp	r2, r1
	beq	.L2619
	cmp	r2, #0
	beq	.L2619
	ldr	r0, .L2681+160
	bl	printk
	ldr	r3, [r6, #1000]
	add	r3, r3, #1
	str	r3, [r6, #1000]
	b	.L2657
.L2619:
	ldr	r0, .L2681+52
	mov	r2, #0
	str	r2, [r0, #1000]
	ldr	r0, .L2681+20
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
	b	.L2592
.L2581:
	ldr	r0, .L2681+164
	bl	printk
	add	r0, sp, #8
	mov	r1, r4
	mov	r2, #520
	bl	rk_copy_from_user
	cmp	r0, #0
	bne	.L2668
.L2622:
	ldr	r2, [sp, #8]
	ldr	r3, .L2681+168
	cmp	r2, r3
	bne	.L2659
	ldr	r2, [sp, #12]
	cmp	r2, #504
	bhi	.L2659
	ldr	r3, .L2681+172
	add	r0, sp, #16
	cmp	r5, r3
	ldr	r3, .L2681+52
	ldreq	r1, [r3, #1004]
	ldrne	r1, [r3, #1008]
	add	r1, r1, #8
.L2673:
	bl	memcpy
.L2674:
	add	r1, sp, #8
	mov	r0, r4
	mov	r2, #520
.L2667:
	bl	rk_copy_to_user
	subs	r4, r0, #0
	bne	.L2657
	b	.L2592
.L2582:
	ldr	r0, .L2681+176
	bl	printk
	add	r0, sp, #8
	mov	r1, r4
	mov	r2, #520
	bl	rk_copy_from_user
	cmp	r0, #0
	beq	.L2625
.L2668:
	ldr	r0, .L2681+64
	bl	printk
	b	.L2657
.L2625:
	ldr	r2, [sp, #8]
	ldr	r3, .L2681+168
	cmp	r2, r3
	bne	.L2659
	ldr	r2, [sp, #12]
	cmp	r2, #504
	bhi	.L2659
	ldr	r3, .L2681+28
	add	r2, r2, #8
	ldr	r4, .L2681+52
	cmp	r5, r3
	bne	.L2626
	add	r1, sp, #8
	ldr	r0, [r4, #1004]
	bl	memcpy
	ldr	r1, [r4, #1004]
	mov	r0, #2
.L2664:
	bl	StorageSysDataStore
	mov	r4, r0
	b	.L2592
.L2626:
	add	r1, sp, #8
	ldr	r0, [r4, #1008]
	bl	memcpy
	mov	r0, #3
	ldr	r1, [r4, #1008]
	b	.L2664
.L2677:
	bl	rknand_dev_flush
	b	.L2663
.L2659:
	mvn	r4, #0
.L2592:
	mov	r1, r4
	ldr	r0, .L2681+180
	bl	printk
	b	.L2566
.L2627:
	mvn	r4, #21
	b	.L2566
.L2634:
	mvn	r4, #11
	b	.L2566
.L2640:
	mvn	r4, #1
	b	.L2566
.L2657:
	mvn	r4, #13
.L2566:
	mov	r0, r4
	add	sp, sp, #528
	ldmfd	sp!, {r4, r5, r6, pc}
.L2682:
	.align	2
.L2681:
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
	.word	.LANCHOR2+488
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
	ldr	r4, .L2686
	mov	r6, #0
	add	r0, r4, #488
	ldr	r5, [r4, #460]
	str	r6, [r4, #468]
	add	r1, r5, #1536
	add	r3, r5, #512
	str	r5, [r4, #484]
	str	r3, [r4, #472]
	add	r3, r5, #1024
	str	r1, [r4, #1008]
	str	r3, [r4, #1004]
	bl	memcpy
	ldr	r7, [r5, #508]
	ldr	r3, [r5, #16]
	cmp	r7, r6
	str	r6, [r4, #476]
	str	r6, [r4, #1000]
	str	r3, [r4, #480]
	beq	.L2684
	mov	r0, r5
	mov	r1, #508
	bl	JSHash
	cmp	r7, r0
	beq	.L2684
	str	r6, [r5, #16]
	ldr	r0, .L2686+4
	str	r6, [r4, #480]
	bl	printk
.L2684:
	ldr	r3, [r4, #480]
	mov	r0, #2
	ldr	r4, .L2686
	cmp	r3, #0
	ldrne	r2, .L2686+8
	ldrne	r3, .L2686
	ldr	r1, [r4, #1004]
	strne	r2, [r3, #476]
	bl	StorageSysDataLoad
	ldr	r1, [r4, #1008]
	mov	r0, #3
	bl	StorageSysDataLoad
	ldmfd	sp!, {r3, r4, r5, r6, r7, lr}
	b	rknand_sys_storage_init
.L2687:
	.align	2
.L2686:
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
	ldr	r7, .L2697
	cmp	r0, #0
	str	r0, [r7, #1012]
	beq	.L2695
	ldr	sl, .L2697+4
	mov	r6, #0
	mov	r4, r6
	mov	r5, r6
	movw	r8, #65532
.L2693:
	ldr	r0, [r7, #1012]
	mov	r1, r5, asl #7
	mov	r2, #0
	bl	rk_ftl_vendor_ops.constprop.26
	cmp	r0, #0
	bne	.L2691
	ldr	r3, [r7, #1012]
	ldr	r0, .L2697+8
	ldr	r1, [r3, #0]
	ldr	r2, [r3, r8]
	ldr	r3, [r3, #4]
	bl	printk
	ldr	r0, [r7, #1012]
	ldr	r3, [r0, #0]
	cmp	r3, sl
	bne	.L2692
	ldr	r3, [r0, r8]
	ldr	r2, [r0, #4]
	cmp	r3, r2
	bne	.L2692
	cmp	r4, r3
	movcc	r6, r5
	movcc	r4, r3
.L2692:
	add	r5, r5, #1
	cmp	r5, #2
	bne	.L2693
	cmp	r4, #0
	beq	.L2694
	mov	r1, r6, asl #7
	mov	r2, #0
	bl	rk_ftl_vendor_ops.constprop.26
	cmp	r0, #0
	ldmeqfd	sp!, {r3, r4, r5, r6, r7, r8, sl, pc}
	b	.L2691
.L2694:
	mov	r1, #65536
	bl	__memzero
	ldr	r3, .L2697
	ldr	r1, .L2697+4
	mov	r2, #1
	mov	r0, r4
	ldr	r3, [r3, #1012]
	stmia	r3, {r1, r2}
	movw	r1, #65532
	str	r2, [r3, r1]
	ldr	r2, .L2697+12
	strh	r4, [r3, #12]	@ movhi
	strh	r2, [r3, #14]	@ movhi
	ldmfd	sp!, {r3, r4, r5, r6, r7, r8, sl, pc}
.L2691:
	ldr	r4, .L2697
	ldr	r0, [r4, #1012]
	bl	kfree
	mov	r3, #0
	mvn	r0, #0
	str	r3, [r4, #1012]
	ldmfd	sp!, {r3, r4, r5, r6, r7, r8, sl, pc}
.L2695:
	mvn	r0, #11
	ldmfd	sp!, {r3, r4, r5, r6, r7, r8, sl, pc}
.L2698:
	.align	2
.L2697:
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
	ldr	r1, .L2706
	stmfd	sp!, {r4, r5, r6, lr}
	.save {r4, r5, r6, lr}
	ldr	ip, [r1, #1012]
	cmp	ip, #0
	ldrneh	r4, [ip, #10]
	movne	r5, ip
	movne	r1, #0
	bne	.L2701
	b	.L2705
.L2703:
	ldrh	r6, [r5, #16]
	add	r5, r5, #8
	cmp	r6, r3
	bne	.L2702
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
.L2702:
	add	r1, r1, #1
.L2701:
	cmp	r1, r4
	bcc	.L2703
	mvn	r0, #0
	ldmfd	sp!, {r4, r5, r6, pc}
.L2705:
	mvn	r0, #0
	ldmfd	sp!, {r4, r5, r6, pc}
.L2707:
	.align	2
.L2706:
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
	ldr	r3, .L2725
	stmfd	sp!, {r4, r5, r6, r7, r8, r9, sl, fp, lr}
	.save {r4, r5, r6, r7, r8, r9, sl, fp, lr}
	mov	sl, r0
	ldr	r4, [r3, #1012]
	.pad #20
	sub	sp, sp, #20
	mov	r9, r1
	mov	r5, r2
	cmp	r4, #0
	beq	.L2721
	add	r8, r2, #63
	ldrh	r2, [r4, #8]
	ldrh	r3, [r4, #10]
	bic	r8, r8, #63
	mov	r7, #0
	str	r2, [sp, #8]
	b	.L2710
.L2717:
	add	r2, r7, #2
	mov	r2, r2, asl #3
	add	r6, r4, r2
	ldrh	r2, [r4, r2]
	cmp	r2, sl
	bne	.L2711
	ldrh	r2, [r6, #4]
	add	r2, r2, #63
	bic	r2, r2, #63
	str	r2, [sp, #12]
	cmp	r5, r2
	bls	.L2712
	ldrh	r2, [r4, #14]
	cmp	r2, r8
	bcc	.L2721
	sub	r3, r3, #1
	mov	fp, r8
	ldrh	r6, [r6, #2]
	mov	r8, r3
	b	.L2713
.L2714:
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
.L2713:
	cmp	r7, r8
	add	r2, r7, #2
	bcc	.L2714
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
	b	.L2724
.L2712:
	ldrh	r0, [r6, #2]
	mov	r1, r9
	mov	r2, r5
	add	r0, r0, #1024
	add	r0, r4, r0
	bl	memcpy
	strh	r5, [r6, #4]	@ movhi
	b	.L2724
.L2711:
	add	r7, r7, #1
.L2710:
	cmp	r7, r3
	bcc	.L2717
	ldrh	r2, [r4, #14]
	cmp	r2, r8
	bcc	.L2721
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
.L2724:
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
	b	.L2709
.L2721:
	mvn	r0, #0
.L2709:
	add	sp, sp, #20
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, sl, fp, pc}
.L2726:
	.align	2
.L2725:
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
	beq	.L2728
	ldr	r3, .L2744
	cmp	r6, r3
	beq	.L2730
	add	r3, r3, #1
	cmp	r6, r3
	bne	.L2741
	b	.L2743
.L2730:
	mov	r1, r5
	mov	r2, #8
	bl	rk_copy_from_user
	cmp	r0, #0
	bne	.L2741
	ldr	r2, [r4, #0]
	ldr	r3, .L2744+4
	cmp	r2, r3
	bne	.L2740
	ldrh	r0, [r4, #4]
	add	r1, r4, #8
	ldrh	r2, [r4, #6]
	bl	rk_ftl_vendor_read
	cmn	r0, #1
	beq	.L2736
	uxth	r2, r0
	mov	r1, r4
	strh	r2, [r4, #6]	@ movhi
	mov	r0, r5
	add	r2, r2, #8
	bl	rk_copy_to_user
	cmp	r0, #0
	moveq	r5, r0
	mvnne	r5, #13
	b	.L2729
.L2743:
	mov	r1, r5
	mov	r2, #8
	bl	rk_copy_from_user
	cmp	r0, #0
	bne	.L2741
	ldr	r2, [r4, #0]
	ldr	r3, .L2744+4
	cmp	r2, r3
	bne	.L2740
	ldrh	r2, [r4, #6]
	movw	r3, #4087
	cmp	r2, r3
	bhi	.L2740
	mov	r0, r4
	mov	r1, r5
	add	r2, r2, #8
	bl	rk_copy_from_user
	cmp	r0, #0
	bne	.L2741
	ldrh	r0, [r4, #4]
	add	r1, r4, #8
	ldrh	r2, [r4, #6]
	bl	rk_ftl_vendor_write
.L2736:
	mov	r5, r0
	b	.L2729
.L2740:
	mvn	r5, #0
	b	.L2729
.L2741:
	mvn	r5, #13
.L2729:
	mov	r0, r4
	bl	kfree
.L2728:
	mov	r0, r5
	ldmfd	sp!, {r4, r5, r6, pc}
.L2745:
	.align	2
.L2744:
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
