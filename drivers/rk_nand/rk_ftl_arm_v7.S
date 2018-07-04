/*
 * Copyright (c) 2016-2018, Fuzhou Rockchip Electronics Co., Ltd
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 * date: 2018-07-03
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
	ldrb	r2, [r3, #2901]	@ zero_extendqisi2
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
	ldrb	r0, [r3, #2913]	@ zero_extendqisi2
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
	.global	remove_from_free_sys_Queue
	.type	remove_from_free_sys_Queue, %function
remove_from_free_sys_Queue:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	ldr	r3, .L461
	movw	r2, #4062
	stmfd	sp!, {r4, r5, r6, lr}
	.save {r4, r5, r6, lr}
	mov	r1, r0
	ldrh	r0, [r3, r2]
	cmp	r0, #0
	ldmeqfd	sp!, {r4, r5, r6, pc}
	movw	r2, #4058
	mov	r5, #0
	ldrh	r2, [r3, r2]
	b	.L457
.L459:
	add	ip, r5, r2
	ldr	r4, .L461
	movw	r6, #4056
	mov	ip, ip, asl #22
	add	ip, r3, ip, lsr #21
	add	ip, ip, #4064
	ldrh	ip, [ip, #0]
	cmp	ip, r1
	bne	.L458
	ldr	r0, .L461+4
	bl	printk
	movw	r2, #4058
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
	movw	r3, #4062
	ldrh	r2, [r4, r3]
	sub	r2, r2, #1
	strh	r2, [r4, r3]	@ movhi
	ldmfd	sp!, {r4, r5, r6, pc}
.L458:
	add	r5, r5, #1
.L457:
	cmp	r5, r0
	bcc	.L459
	mov	r0, #0
	ldmfd	sp!, {r4, r5, r6, pc}
.L462:
	.align	2
.L461:
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
	stmfd	sp!, {r3, r4, r5, r6, r7, r8, sl, lr}
	.save {r3, r4, r5, r6, r7, r8, sl, lr}
	movw	r6, #4062
	ldr	r4, .L470
	movw	r7, #4058
.L464:
	ldrh	r1, [r4, r6]
	cmp	r1, #0
	beq	.L465
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
	bne	.L466
	mov	r0, r5
	bl	P2V_block_in_plane
	mov	r3, r5, asl #10
	mov	r8, r0
	ldr	r0, [r4, #4044]
	str	r3, [r0, #4]
	ldrb	r3, [r4, #928]	@ zero_extendqisi2
	cmp	r3, #0
	beq	.L467
	mov	r1, sl
	mov	r2, #1
	bl	FlashEraseBlocks
.L467:
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
	b	.L466
.L465:
	ldr	r0, .L470+4
	bl	printk
.L468:
	b	.L468
.L466:
	sub	r2, r5, #1
	movw	r3, #65533
	uxth	r2, r2
	cmp	r2, r3
	bls	.L469
	mov	r1, r5
	ldrh	r2, [r4, r6]
	ldr	r0, .L470+8
	bl	printk
	b	.L464
.L469:
	mov	r0, r5
	ldmfd	sp!, {r3, r4, r5, r6, r7, r8, sl, pc}
.L471:
	.align	2
.L470:
	.word	.LANCHOR0
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
	ldr	r2, .L487
	mov	r3, #3856
	stmfd	sp!, {r4, r5, r6, r7, r8, r9, sl, fp, lr}
	.save {r4, r5, r6, r7, r8, r9, sl, fp, lr}
	.pad #20
	sub	sp, sp, #20
	ldrh	r3, [r2, r3]
	cmp	r3, r0
	bls	.L473
	ldr	r1, .L487+4
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
	beq	.L486
.L474:
	ldr	r7, [r1, #-2016]
	mov	r5, r0, asl #1
	ldrh	r1, [r2, #4]
	ldr	fp, [r8, #-2024]
	ldrh	sl, [r7, r5]
	cmp	r1, #0
	ldr	r8, .L487+8
	mulne	r6, r1, sl
	rsb	r1, fp, r3
	mov	sl, #3856
	mov	r1, r1, asr #1
	mul	r1, r8, r1
	ldr	r8, .L487
	ldrh	sl, [r8, sl]
	ldr	r8, [r8, #4048]
	uxth	r1, r1
	add	r5, r8, r5
	str	r5, [sp, #12]
	mov	r5, #0
	str	sl, [sp, #8]
	str	r8, [sp, #4]
.L481:
	add	r5, r5, #1
	ldr	r8, [sp, #8]
	uxth	r5, r5
	cmp	r5, r8
	bhi	.L473
	cmp	r0, r1
	beq	.L473
	mov	r9, r1, asl #1
	ldrh	r8, [r3, #4]
	ldrh	sl, [r7, r9]
	cmp	r8, #0
	mvneq	r8, #0
	mulne	r8, r8, sl
	cmp	r8, r6
	bne	.L477
	ldr	r8, [sp, #4]
	ldrh	sl, [r8, r9]
	ldr	r9, [sp, #12]
	ldrh	r8, [r9, #0]
	cmp	sl, r8
	bcc	.L479
	b	.L478
.L477:
	bhi	.L478
.L479:
	ldrh	r8, [r3, #0]
	movw	sl, #65535
	cmp	r8, sl
	streqh	r1, [r2, #2]	@ movhi
	streqh	r0, [r3, #0]	@ movhi
	ldreq	r3, .L487+4
	streq	r2, [r3, #-2012]
	beq	.L473
.L480:
	mov	r1, #6
	mla	r3, r1, r8, fp
	mov	r1, r8
	b	.L481
.L478:
	strh	r1, [r4, ip]	@ movhi
	ldrh	r1, [r3, #2]
	strh	r1, [r2, #2]	@ movhi
	ldr	r1, .L487+4
	ldr	ip, [r1, #-2020]
	cmp	r3, ip
	bne	.L482
	strh	r0, [r3, #2]	@ movhi
.L486:
	str	r2, [r1, #-2020]
	b	.L473
.L482:
	ldrh	r2, [r3, #2]
	mov	ip, #6
	ldr	r1, [r1, #-2024]
	mul	r2, ip, r2
	strh	r0, [r1, r2]	@ movhi
	strh	r0, [r3, #2]	@ movhi
.L473:
	mov	r0, #0
	add	sp, sp, #20
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, sl, fp, pc}
.L488:
	.align	2
.L487:
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
	ldr	r2, .L490
	ldr	r3, .L490+4
	ldrh	r1, [r2, r3]
	add	r1, r1, #1
	strh	r1, [r2, r3]	@ movhi
	ldmfd	sp!, {r3, pc}
.L491:
	.align	2
.L490:
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
	beq	.L493
	ldr	r1, .L500
	mov	r5, #6
	mul	ip, r5, r0
	mvn	r3, #0
	ldr	r4, [r1, #-2024]
	add	r2, r4, ip
	strh	r3, [r2, #2]	@ movhi
	strh	r3, [r4, ip]	@ movhi
	ldr	r3, [r1, #-2004]
	cmp	r3, #0
	beq	.L499
.L494:
	ldr	sl, [r1, #-2024]
	mov	r8, r0, asl #1
	ldr	r9, .L500+4
	rsb	r1, sl, r3
	ldr	r7, .L500+8
	mov	r1, r1, asr #1
	ldr	r7, [r7, #4048]
	mul	r1, r9, r1
	mov	r9, r5
	ldrh	r8, [r7, r8]
	uxth	r1, r1
.L497:
	mov	r5, r1, asl #1
	ldrh	r5, [r7, r5]
	cmp	r5, r8
	bcs	.L495
	ldrh	r5, [r3, #0]
	cmp	r5, r6
	streqh	r1, [r2, #2]	@ movhi
	streqh	r0, [r3, #0]	@ movhi
	beq	.L493
.L496:
	mla	r3, r9, r5, sl
	mov	r1, r5
	b	.L497
.L495:
	ldrh	r5, [r3, #2]
	strh	r5, [r2, #2]	@ movhi
	strh	r1, [r4, ip]	@ movhi
	ldr	r1, .L500
	ldr	ip, [r1, #-2004]
	cmp	r3, ip
	bne	.L498
	strh	r0, [r3, #2]	@ movhi
.L499:
	str	r2, [r1, #-2004]
	b	.L493
.L498:
	ldrh	r2, [r3, #2]
	mov	ip, #6
	ldr	r1, [r1, #-2024]
	mul	r2, ip, r2
	strh	r0, [r1, r2]	@ movhi
	strh	r0, [r3, #2]	@ movhi
.L493:
	mov	r0, #0
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, sl, pc}
.L501:
	.align	2
.L500:
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
	ldr	r2, .L503
	ldr	r3, .L503+4
	ldrh	r1, [r2, r3]
	add	r1, r1, #1
	strh	r1, [r2, r3]	@ movhi
	ldmfd	sp!, {r3, pc}
.L504:
	.align	2
.L503:
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
	ldr	ip, .L510
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
	bne	.L506
	cmp	r4, r6
	mlane	r2, r2, r4, r3
	moveq	r2, #0
	streq	r2, [r0, #0]
	strne	r2, [r0, #0]
	mvnne	r0, #0
	strneh	r0, [r2, #2]	@ movhi
	b	.L508
.L506:
	cmp	r4, r6
	ldrh	r0, [r5, #2]
	bne	.L509
	cmp	r0, r4
	mulne	r2, r2, r0
	mvnne	r0, #0
	strneh	r0, [r3, r2]	@ movhi
	b	.L508
.L509:
	mla	r4, r2, r4, r3
	strh	r0, [r4, #2]	@ movhi
	ldrh	r6, [r5, #2]
	ldrh	r4, [r3, r1]
	ldr	r0, [ip, #-2024]
	mul	r2, r2, r6
	strh	r4, [r0, r2]	@ movhi
.L508:
	mvn	r2, #0
	mov	r0, #0
	strh	r2, [r3, r1]	@ movhi
	strh	r2, [r5, #2]	@ movhi
	ldmfd	sp!, {r4, r5, r6, pc}
.L511:
	.align	2
.L510:
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
	beq	.L517
	ldr	r2, .L518
	movw	lr, #65535
	mov	ip, #6
	ldr	r4, [r2, #-2024]
	b	.L514
.L516:
	mla	r3, ip, r2, r4
	sub	r1, r1, #1
	uxth	r1, r1
.L514:
	cmp	r1, #0
	beq	.L515
	ldrh	r2, [r3, #0]
	cmp	r2, lr
	bne	.L516
.L515:
	rsb	r4, r4, r3
	ldr	r3, .L518+4
	mov	r4, r4, asr #1
	mul	r4, r3, r4
	uxth	r4, r4
	mov	r1, r4
	bl	List_remove_node
	mov	r0, r4
	ldmfd	sp!, {r4, pc}
.L517:
	movw	r0, #65535
	ldmfd	sp!, {r4, pc}
.L519:
	.align	2
.L518:
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
	ldr	r2, .L526
	ldr	r3, [r2, #-2020]
	cmp	r3, #0
	beq	.L525
	ldr	r2, [r2, #-2024]
	movw	r1, #65535
	mov	ip, #6
	b	.L522
.L524:
	mla	r3, ip, r3, r2
	sub	r0, r0, #1
	uxth	r0, r0
.L522:
	cmp	r0, #0
	beq	.L523
	ldrh	r3, [r3, #0]
	cmp	r3, r1
	bne	.L524
	mov	r0, r1
	bx	lr
.L523:
	rsb	r3, r2, r3
	ldr	r0, .L526+4
	mov	r3, r3, asr #1
	mul	r0, r0, r3
	uxth	r0, r0
	bx	lr
.L525:
	movw	r0, #65535
	bx	lr
.L527:
	.align	2
.L526:
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
	ldr	r3, .L535
	ldr	r2, .L535+4
	ldrh	r2, [r3, r2]
	cmp	r2, r0
	beq	.L529
	ldr	r2, .L535+8
	ldrh	r2, [r3, r2]
	cmp	r2, r0
	beq	.L529
	ldr	r2, .L535+12
	ldrh	r2, [r3, r2]
	cmp	r2, r0
	beq	.L529
	mov	r5, #6
	ldr	r2, [r3, #-2024]
	mul	r5, r5, r0
	ldr	r0, [r3, #-2020]
	add	r1, r2, r5
	cmp	r1, r0
	beq	.L529
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
	bne	.L531
	ldrh	r1, [r2, r5]
	cmp	r1, r3
	beq	.L529
.L531:
	mov	r1, #6
	mla	r3, r1, r3, r2
	ldr	r1, .L535+16
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
	bcs	.L529
	ldr	r0, .L535+20
	mov	r1, r4
	bl	List_remove_node
	ldr	r5, .L535
	ldr	r3, .L535+24
	mov	r0, r4
	ldrh	r2, [r5, r3]
	sub	r2, r2, #1
	strh	r2, [r5, r3]	@ movhi
	bl	INSERT_DATA_LIST
.L529:
	mov	r0, #0
	ldmfd	sp!, {r3, r4, r5, pc}
.L536:
	.align	2
.L535:
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
	b	.L538
.L543:
	mov	r7, r3
	add	r3, r3, #2
	ldrh	r6, [r7, #0]
	cmp	r6, #0
	bne	.L539
	bl	FtlFreeSysBlkQueueOut
	movw	r3, #65533
	sub	r2, r0, #1
	mov	r1, r0
	strh	r0, [r7, #0]	@ movhi
	uxth	r2, r2
	cmp	r2, r3
	bls	.L540
	ldr	r2, .L544
	movw	r3, #4062
	ldr	r0, .L544+4
	ldrh	r2, [r2, r3]
	bl	printk
.L541:
	b	.L541
.L540:
	ldr	r3, [r4, #28]
	strh	r6, [r4, #2]	@ movhi
	add	r3, r3, #1
	str	r3, [r4, #28]
	ldrh	r3, [r4, #8]
	strh	r5, [r4, #0]	@ movhi
	add	r3, r3, #1
	strh	r3, [r4, #8]	@ movhi
	b	.L542
.L539:
	add	r5, r5, #1
	uxth	r5, r5
.L538:
	cmp	r5, r2
	bne	.L543
.L542:
	mov	r0, #0
	ldmfd	sp!, {r3, r4, r5, r6, r7, pc}
.L545:
	.align	2
.L544:
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
	ldr	r2, .L558
	movw	r3, #3954
	stmfd	sp!, {r4, r5, r6, r7, lr}
	.save {r4, r5, r6, r7, lr}
	movw	r1, #65535
	ldrh	r3, [r2, r3]
	ldr	r2, .L558+4
	ldr	ip, [r2, #-1852]
	mov	r2, #0
	mov	r0, r2
	b	.L547
.L549:
	add	r2, r2, #12
	add	r4, ip, r2
	ldrh	r4, [r4, #-12]
	cmp	r4, r1
	ldmeqfd	sp!, {r4, r5, r6, r7, pc}
	add	r0, r0, #1
	uxth	r0, r0
.L547:
	cmp	r0, r3
	bne	.L549
	mov	r1, #0
	mov	r0, r3
	mov	r5, #-2147483648
	mov	r2, r1
	b	.L550
.L552:
	add	r4, ip, r1
	ldr	r4, [r4, #4]
	cmp	r4, #0
	blt	.L551
	cmp	r4, r5
	movcc	r5, r4
	movcc	r0, r2
.L551:
	add	r2, r2, #1
	add	r1, r1, #12
	uxth	r2, r2
.L550:
	cmp	r2, r3
	bne	.L552
	cmp	r0, r3
	ldmccfd	sp!, {r4, r5, r6, r7, pc}
	ldr	r2, .L558+8
	mov	r0, r3
	ldr	r1, .L558+4
	mvn	r4, #0
	ldrh	r6, [r1, r2]
	mov	r2, #0
	mov	r1, r2
	b	.L553
.L555:
	add	r5, ip, r2
	ldr	r5, [r5, #4]
	cmp	r5, r4
	bcs	.L554
	ldrh	r7, [ip, r2]
	cmp	r7, r6
	movne	r4, r5
	movne	r0, r1
.L554:
	add	r1, r1, #1
	add	r2, r2, #12
	uxth	r1, r1
.L553:
	cmp	r1, r3
	bne	.L555
	ldmfd	sp!, {r4, r5, r6, r7, pc}
.L559:
	.align	2
.L558:
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
	ldr	r3, .L566
	ldr	r2, .L566+4
	stmfd	sp!, {r4, r5, lr}
	.save {r4, r5, lr}
	ldrh	r1, [r2, r3]
	cmp	r1, #4
	add	ip, r1, #1
	mov	r1, r3
	strh	ip, [r2, r3]	@ movhi
	bhi	.L561
	cmp	r0, #0
	ldmeqfd	sp!, {r4, r5, pc}
.L561:
	ldr	r0, .L566+8
	mov	r3, #0
	strh	r3, [r2, r1]	@ movhi
	mov	r1, #3856
	str	r3, [r2, #-1844]
	movw	ip, #65535
	ldrh	r4, [r0, r1]
	ldr	r1, [r2, #-2016]
	ldr	r2, .L566+4
	b	.L563
.L565:
	ldrh	r0, [r1], #2
	add	r3, r3, #1
	cmp	r0, ip
	uxth	r3, r3
	ldrne	r5, [r2, #-1844]
	addne	r0, r0, r5
	strne	r0, [r2, #-1844]
.L563:
	cmp	r3, r4
	bne	.L565
	ldmfd	sp!, {r4, r5, pc}
.L567:
	.align	2
.L566:
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
	ldr	r3, .L572
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
.L573:
	.align	2
.L572:
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
	ldr	r3, .L575
	mov	r2, r0, lsr #5
	and	r0, r0, #31
	ldr	r3, [r3, #-1840]
	ldr	r3, [r3, r2, asl #2]
	mov	r0, r3, lsr r0
	and	r0, r0, #1
	bx	lr
.L576:
	.align	2
.L575:
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
	ldr	ip, .L584
	mov	r3, #0
	strh	r3, [r0, #4]	@ movhi
	movw	r3, #3848
	stmfd	sp!, {r4, r5, r6, lr}
	.save {r4, r5, r6, lr}
	ldrh	r3, [ip, r3]
	movw	ip, #65535
	b	.L578
.L580:
	add	r4, r0, r2, asl #1
	add	r2, r2, #1
	ldrh	r4, [r4, #16]
	uxth	r2, r2
	cmp	r4, ip
	ldrneh	r4, [r0, #4]
	addne	r4, r4, #1
	strneh	r4, [r0, #4]	@ movhi
.L578:
	cmp	r2, r3
	bcc	.L580
	ldr	ip, .L584
	movw	r2, #3916
	movw	r4, #65535
	mvn	r1, r1
	ldrh	r5, [ip, r2]
	mov	ip, r0
	mov	r2, #0
	b	.L581
.L583:
	ldrh	r6, [ip, #16]
	add	r2, r2, #1
	add	ip, ip, #2
	cmp	r6, r4
	uxth	r2, r2
	ldrneh	r6, [r0, #4]
	addne	r6, r5, r6
	addne	r6, r6, r1
	strneh	r6, [r0, #4]	@ movhi
.L581:
	cmp	r2, r3
	bne	.L583
	ldmfd	sp!, {r4, r5, r6, pc}
.L585:
	.align	2
.L584:
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
	ldr	r6, .L592
	strh	r5, [r0, #4]	@ movhi
	movw	r8, #3848
	strb	r5, [r4, #7]
	mvn	r7, #0
	b	.L587
.L589:
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
.L587:
	ldrh	r2, [r6, r8]
	ldr	r3, .L592
	cmp	r2, r5
	bhi	.L589
	movw	r1, #3916
	ldrb	r2, [r4, #7]	@ zero_extendqisi2
	ldrh	r1, [r3, r1]
	mul	r2, r1, r2
	strh	r2, [r4, #4]	@ movhi
	mov	r2, #0
	strb	r2, [r4, #9]
	ldr	r2, [r3, #3840]
	cmp	r2, #0
	beq	.L590
	ldrh	r1, [r4, #0]
	ldr	r2, [r3, #4048]
	mov	r3, r1, asl #1
	ldrh	r3, [r2, r3]
	cmp	r3, #59
	movls	r3, #1
	strlsb	r3, [r4, #9]
.L590:
	ldr	r3, .L592
	mov	r0, #0
	ldrb	r3, [r3, #852]	@ zero_extendqisi2
	cmp	r3, #0
	movne	r3, #1
	strneb	r3, [r4, #9]
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, sl, pc}
.L593:
	.align	2
.L592:
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
	ldr	r6, .L599
	movw	sl, #3848
	movw	r8, #3916
	b	.L595
.L597:
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
.L595:
	ldrh	r3, [r6, sl]
	cmp	r3, r5
	bhi	.L597
	cmp	r4, #0
	beq	.L598
	mov	r1, r4
	mov	r0, #32768
	bl	__aeabi_idiv
	uxth	r4, r0
.L598:
	ldr	r3, .L599+4
	mov	r2, #6
	mov	r0, #0
	ldr	r3, [r3, #-2024]
	mla	r7, r2, r7, r3
	strh	r4, [r7, #4]	@ movhi
	ldmfd	sp!, {r3, r4, r5, r6, r7, r8, sl, pc}
.L600:
	.align	2
.L599:
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
	ldr	r3, .L604
	ldr	r0, [r3, #-2004]
	cmp	r0, #0
	bxeq	lr
	ldr	r3, [r3, #-2024]
	ldr	r2, .L604+4
	rsb	r0, r3, r0
	ldr	r3, .L604+8
	mov	r0, r0, asr #1
	ldr	r2, [r2, #4048]
	mul	r3, r3, r0
	uxth	r3, r3
	mov	r3, r3, asl #1
	ldrh	r0, [r2, r3]
	bx	lr
.L605:
	.align	2
.L604:
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
	ldr	r2, .L613
	stmfd	sp!, {r4, r5, lr}
	.save {r4, r5, lr}
	ldr	r3, [r2, #-2004]
	cmp	r3, #0
	beq	.L612
	ldr	r1, .L613+4
	mov	ip, #7
	mov	r5, #6
	movw	r4, #65535
	ldrh	r1, [r2, r1]
	mul	r1, ip, r1
	mov	r1, r1, asr #3
	cmp	r0, r1
	uxthgt	r0, r1
	ldr	r1, [r2, #-2024]
	ldr	r2, .L613+8
	rsb	r3, r1, r3
	mov	r3, r3, asr #1
	mul	r3, r2, r3
	mov	r2, #0
	uxth	r3, r3
	b	.L609
.L611:
	mul	ip, r5, r3
	ldrh	ip, [r1, ip]
	cmp	ip, r4
	beq	.L610
	add	r2, r2, #1
	mov	r3, ip
	uxth	r2, r2
.L609:
	cmp	r2, r0
	bne	.L611
.L610:
	ldr	r2, .L613+12
	mov	r3, r3, asl #1
	ldr	r2, [r2, #4048]
	ldrh	r0, [r2, r3]
	ldmfd	sp!, {r4, r5, pc}
.L612:
	mov	r0, r3
	ldmfd	sp!, {r4, r5, pc}
.L614:
	.align	2
.L613:
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
	ldr	r6, .L625
	add	r4, sl, #12
	.pad #32
	sub	sp, sp, #32
	ldr	r1, .L625+4
	bl	strcpy
	mov	r0, r4
	ldr	r1, .L625+8
	ldr	r2, [r6, #3048]
	bl	sprintf
	ldr	r1, .L625+12
	ldr	r2, [r6, #3932]
	add	r4, r4, r0
	mov	r0, r4
	bl	sprintf
	ldr	r3, .L625+16
	ldr	r3, [r3, #2996]
	cmp	r3, #1
	add	r4, r4, r0
	bne	.L621
.L616:
	add	r0, sp, #16
	add	r1, sp, #20
	add	r2, sp, #24
	add	r3, sp, #28
	bl	NandcGetTimeCfg
	ldr	r3, [sp, #24]
	ldr	r2, [sp, #16]
	mov	r0, r4
	ldr	r1, .L625+20
	str	r3, [sp, #0]
	ldr	r3, [sp, #28]
	ldr	r5, .L625+24
	ldr	r7, .L625+28
	str	r3, [sp, #4]
	ldr	r3, [sp, #20]
	bl	sprintf
	ldr	r1, .L625+32
	ldr	r8, .L625+36
	add	r4, r4, r0
	mov	r0, r4
	add	r4, r4, #10
	bl	strcpy
	ldr	r1, .L625+40
	mov	r0, r4
	ldr	r2, [r6, #3976]
	bl	sprintf
	ldr	r1, .L625+44
	ldr	r2, [r5, #-1844]
	add	r4, r4, r0
	mov	r0, r4
	bl	sprintf
	ldr	r1, .L625+48
	ldr	r2, [r5, #-1836]
	add	r4, r4, r0
	mov	r0, r4
	bl	sprintf
	ldr	r1, .L625+52
	ldr	r2, [r5, #-1832]
	add	r4, r4, r0
	mov	r0, r4
	bl	sprintf
	ldr	r1, .L625+56
	ldr	r2, [r5, #-1828]
	add	r4, r4, r0
	mov	r0, r4
	bl	sprintf
	ldr	r1, .L625+60
	ldr	r2, [r5, #-1824]
	add	r4, r4, r0
	mov	r0, r4
	bl	sprintf
	ldr	r1, .L625+64
	ldr	r2, [r5, #-1820]
	add	r4, r4, r0
	mov	r0, r4
	bl	sprintf
	ldr	r1, .L625+68
	ldr	r2, [r5, #-1816]
	add	r4, r4, r0
	mov	r0, r4
	bl	sprintf
	ldr	r2, [r5, #-1812]
	ldr	r1, .L625+72
	mov	r2, r2, lsr #11
	add	r4, r4, r0
	mov	r0, r4
	bl	sprintf
	ldr	r2, [r5, #-1808]
	ldr	r1, .L625+76
	mov	r2, r2, lsr #11
	add	r4, r4, r0
	mov	r0, r4
	bl	sprintf
	ldr	r1, .L625+80
	ldr	r2, [r5, #-1804]
	add	r4, r4, r0
	mov	r0, r4
	bl	sprintf
	ldr	r1, .L625+84
	ldr	r2, [r5, #-1800]
	add	r4, r4, r0
	mov	r0, r4
	bl	sprintf
	add	r4, r4, r0
	bl	FtlBbtCalcTotleCnt
	movw	r2, #3986
	ldr	r1, .L625+88
	ldrh	r2, [r6, r2]
	mov	r3, r0
	mov	r0, r4
	bl	sprintf
	ldr	r1, .L625+92
	ldrh	r2, [r5, r7]
	add	r4, r4, r0
	mov	r0, r4
	bl	sprintf
	ldr	r1, .L625+96
	ldr	r2, [r5, #-1796]
	add	r4, r4, r0
	mov	r0, r4
	bl	sprintf
	ldr	r1, .L625+100
	ldr	r2, [r5, #-1792]
	add	r4, r4, r0
	mov	r0, r4
	bl	sprintf
	ldr	r1, .L625+104
	ldr	r2, [r5, #-1788]
	add	r4, r4, r0
	mov	r0, r4
	bl	sprintf
	ldr	r1, .L625+108
	ldr	r2, [r6, #4052]
	add	r4, r4, r0
	mov	r0, r4
	bl	sprintf
	ldr	r1, .L625+112
	ldr	r2, [r5, #-1784]
	add	r4, r4, r0
	mov	r0, r4
	bl	sprintf
	ldr	r1, .L625+116
	ldr	r2, [r5, #-1780]
	add	r4, r4, r0
	mov	r0, r4
	bl	sprintf
	ldr	r3, .L625+120
	ldr	r1, .L625+124
	ldrh	r2, [r5, r3]
	add	r4, r4, r0
	mov	r0, r4
	bl	sprintf
	ldr	r3, .L625+128
	ldr	r1, .L625+132
	ldrh	r2, [r5, r3]
	add	r4, r4, r0
	mov	r0, r4
	bl	sprintf
	ldr	r1, .L625+136
	ldr	r2, [r6, #3956]
	add	r4, r4, r0
	mov	r0, r4
	bl	sprintf
	ldr	r1, .L625+140
	ldr	r2, [r6, #3948]
	add	r4, r4, r0
	mov	r0, r4
	bl	sprintf
	ldr	r1, .L625+144
	ldr	r2, [r6, #3844]
	add	r4, r4, r0
	mov	r0, r4
	bl	sprintf
	movw	r3, #4062
	ldrh	r2, [r6, r3]
	ldr	r1, .L625+148
	add	r4, r4, r0
	mov	r0, r4
	bl	sprintf
	mov	r3, #3856
	ldrh	r2, [r6, r3]
	ldr	r1, .L625+152
	add	r4, r4, r0
	mov	r0, r4
	bl	sprintf
	ldr	r3, .L625+156
	ldr	r1, .L625+160
	ldrh	r2, [r5, r3]
	add	r4, r4, r0
	mov	r0, r4
	bl	sprintf
	ldr	r1, .L625+164
	ldr	r2, [r6, #3860]
	add	r4, r4, r0
	mov	r0, r4
	bl	sprintf
	ldr	r3, .L625+168
	ldr	r1, .L625+172
	ldrh	r2, [r5, r3]
	add	r4, r4, r0
	mov	r0, r4
	bl	sprintf
	movw	r3, #3980
	ldrh	r2, [r6, r3]
	ldr	r1, .L625+176
	add	r4, r4, r0
	mov	r0, r4
	bl	sprintf
	ldr	r3, .L625+180
	ldr	r1, .L625+184
	ldrh	r2, [r5, r3]
	add	r4, r4, r0
	mov	r0, r4
	bl	sprintf
	ldr	r1, .L625+188
	ldrb	r2, [r5, #-1990]	@ zero_extendqisi2
	add	r4, r4, r0
	mov	r0, r4
	bl	sprintf
	ldrh	r2, [r5, r8]
	ldr	r1, .L625+192
	add	r4, r4, r0
	mov	r0, r4
	bl	sprintf
	ldr	r1, .L625+196
	ldrb	r2, [r5, #-1988]	@ zero_extendqisi2
	add	r4, r4, r0
	mov	r0, r4
	bl	sprintf
	ldr	r3, .L625+200
	ldr	r1, .L625+204
	ldrh	r2, [r5, r3]
	add	r4, r4, r0
	mov	r0, r4
	bl	sprintf
	ldrh	r3, [r5, r8]
	ldr	r2, [r5, #-2016]
	add	r8, r8, #48
	ldr	r1, .L625+208
	mov	r3, r3, asl #1
	ldrh	r2, [r2, r3]
	add	r4, r4, r0
	mov	r0, r4
	bl	sprintf
	ldr	r3, .L625+212
	ldr	r1, .L625+216
	ldrh	r2, [r5, r3]
	add	r4, r4, r0
	mov	r0, r4
	bl	sprintf
	ldr	r1, .L625+220
	ldrb	r2, [r5, #-1942]	@ zero_extendqisi2
	add	r4, r4, r0
	mov	r0, r4
	bl	sprintf
	ldrh	r2, [r5, r8]
	ldr	r1, .L625+224
	add	r4, r4, r0
	mov	r0, r4
	bl	sprintf
	ldr	r1, .L625+228
	ldrb	r2, [r5, #-1940]	@ zero_extendqisi2
	add	r4, r4, r0
	mov	r0, r4
	bl	sprintf
	ldr	r3, .L625+232
	ldr	r1, .L625+236
	ldrh	r2, [r5, r3]
	add	r4, r4, r0
	mov	r0, r4
	bl	sprintf
	ldrh	r3, [r5, r8]
	ldr	r2, [r5, #-2016]
	add	r8, r8, #188
	ldr	r1, .L625+240
	mov	r3, r3, asl #1
	ldrh	r2, [r2, r3]
	add	r4, r4, r0
	mov	r0, r4
	bl	sprintf
	ldr	r3, .L625+244
	ldr	r1, .L625+248
	ldrh	r2, [r5, r3]
	add	r4, r4, r0
	mov	r0, r4
	bl	sprintf
	ldr	r1, .L625+252
	ldrb	r2, [r5, #-1894]	@ zero_extendqisi2
	add	r4, r4, r0
	mov	r0, r4
	bl	sprintf
	ldr	r3, .L625+256
	ldr	r1, .L625+260
	ldrh	r2, [r5, r3]
	add	r4, r4, r0
	mov	r0, r4
	bl	sprintf
	ldr	r1, .L625+264
	ldrb	r2, [r5, #-1892]	@ zero_extendqisi2
	add	r4, r4, r0
	mov	r0, r4
	bl	sprintf
	ldr	r3, .L625+268
	ldr	r1, .L625+272
	ldrh	r2, [r5, r3]
	add	r4, r4, r0
	mov	r0, r4
	bl	sprintf
	ldr	r3, .L625+276
	ldr	r1, .L625+280
	ldrh	r2, [r5, r3]
	add	r4, r4, r0
	mov	r0, r4
	bl	sprintf
	ldr	r1, .L625+284
	ldrb	r2, [r5, #-1754]	@ zero_extendqisi2
	add	r4, r4, r0
	mov	r0, r4
	bl	sprintf
	ldr	r1, .L625+288
	ldrh	r2, [r5, r8]
	add	r4, r4, r0
	mov	r0, r4
	bl	sprintf
	ldr	r1, .L625+292
	ldrb	r2, [r5, #-1752]	@ zero_extendqisi2
	add	r4, r4, r0
	mov	r0, r4
	bl	sprintf
	ldr	r3, .L625+296
	ldr	r1, .L625+300
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
	ldr	r1, .L625+304
	ldr	r3, [r5, #-1628]
	add	r4, r4, r0
	mov	r0, r4
	bl	sprintf
	ldr	r1, .L625+308
	ldr	r2, [r5, #-1636]
	add	r4, r4, r0
	mov	r0, r4
	bl	sprintf
	ldr	r1, .L625+312
	ldr	r2, [r5, #-1612]
	add	r4, r4, r0
	mov	r0, r4
	bl	sprintf
	ldr	r3, .L625+316
	ldr	r1, .L625+320
	ldrh	r2, [r5, r3]
	add	r4, r4, r0
	mov	r0, r4
	bl	sprintf
	ldr	r3, .L625+324
	ldr	r1, .L625+328
	ldrh	r2, [r5, r3]
	add	r4, r4, r0
	mov	r0, r4
	bl	sprintf
	ldr	r1, .L625+332
	ldr	r2, [r5, #-1192]
	add	r4, r4, r0
	mov	r0, r4
	bl	sprintf
	ldr	r3, .L625+336
	ldr	r1, .L625+340
	ldrh	r2, [r5, r3]
	add	r4, r4, r0
	mov	r0, r4
	bl	sprintf
	add	r4, r4, r0
	bl	GetFreeBlockMinEraseCount
	ldr	r1, .L625+344
	mov	r2, r0
	mov	r0, r4
	bl	sprintf
	add	r4, r4, r0
	ldrh	r0, [r5, r7]
	bl	GetFreeBlockMaxEraseCount
	ldr	r1, .L625+348
	mov	r2, r0
	mov	r0, r4
	bl	sprintf
	ldrh	r3, [r5, r8]
	movw	r2, #65535
	cmp	r3, r2
	add	r4, r4, r0
	beq	.L618
	ldr	r2, [r5, #-2016]
	mov	r3, r3, asl #1
	mov	r0, r4
	ldr	r1, .L625+352
	ldrh	r2, [r2, r3]
	bl	sprintf
	add	r4, r4, r0
.L618:
	mov	r0, #0
	ldr	r6, .L625+24
	bl	List_get_gc_head_node
	ldr	r8, .L625
	mov	r5, #0
	movw	r9, #65535
	uxth	r3, r0
.L620:
	cmp	r3, r9
	beq	.L619
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
	ldr	r1, .L625+356
	str	r2, [sp, #8]
	mov	r2, r5
	bl	sprintf
	add	r5, r5, #1
	ldr	r3, [r6, #-2024]
	cmp	r5, #16
	ldrh	r3, [r3, r7]
	add	r4, r4, r0
	bne	.L620
.L619:
	ldr	r6, .L625+24
	mov	r5, #0
	ldr	r8, .L625
	movw	r9, #65535
	ldr	r2, [r6, #-2004]
	ldr	r3, [r6, #-2024]
	rsb	r3, r3, r2
	ldr	r2, .L625+360
	mov	r3, r3, asr #1
	mul	r3, r2, r3
	uxth	r3, r3
.L622:
	cmp	r3, r9
	beq	.L621
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
	ldr	r1, .L625+364
	str	r2, [sp, #4]
	mov	r2, r5
	bl	sprintf
	add	r5, r5, #1
	ldr	r3, [r6, #-2024]
	cmp	r5, #4
	ldrh	r3, [r3, r7]
	add	r4, r4, r0
	bne	.L622
.L621:
	rsb	r0, sl, r4
	add	sp, sp, #32
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, sl, pc}
.L626:
	.align	2
.L625:
	.word	.LANCHOR0
	.word	.LC8
	.word	.LC9
	.word	.LC10
	.word	.LANCHOR1
	.word	.LC11
	.word	.LANCHOR2
	.word	-2000
	.word	.LC12
	.word	-1996
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
	ldr	r1, .L628
	ldr	r2, .L628+4
	bl	sprintf
	add	r5, r4, r0
	mov	r0, r5
	bl	FtlPrintInfo2buf
	add	r0, r5, r0
	rsb	r0, r4, r0
	ldmfd	sp!, {r3, r4, r5, pc}
.L629:
	.align	2
.L628:
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
	ldr	r3, .L655
	stmfd	sp!, {r4, r5, r6, r7, r8, r9, sl, fp, lr}
	.save {r4, r5, r6, r7, r8, r9, sl, fp, lr}
	.pad #20
	sub	sp, sp, #20
	ldr	r1, [r3, #-1788]
	ldr	r2, [r3, #-1780]
	cmp	r1, r2
	bcs	.L631
	ldr	r0, .L655+4
	mov	r1, #3856
	mov	r2, #0
	str	r2, [r3, #-1796]
	ldrh	r1, [r0, r1]
	ldr	r0, [r0, #4048]
	b	.L632
.L633:
	ldrh	lr, [r0], #2
	add	r2, r2, #1
	ldr	ip, [r3, #-1796]
	add	ip, lr, ip
	str	ip, [r3, #-1796]
.L632:
	cmp	r2, r1
	ldr	r4, .L655
	bcc	.L633
	ldr	r5, [r4, #-1796]
	mov	r0, r5
	bl	__aeabi_uidiv
	ldr	r2, .L655+4
	movw	r3, #3906
	ldrh	r1, [r2, r3]
	str	r0, [r4, #-1788]
	ldr	r0, [r4, #-1792]
	rsb	r0, r0, r5
	bl	__aeabi_uidiv
	str	r0, [r4, #-1796]
	b	.L634
.L631:
	ldr	r2, [r3, #-1784]
	cmp	r1, r2
	bls	.L634
	add	r2, r2, #1
	mov	ip, #3856
	str	r2, [r3, #-1784]
	mov	r3, #0
	ldr	r2, .L655+4
	b	.L635
.L636:
	ldr	r0, [r2, #4048]
	mov	r1, r3, asl #1
	add	r3, r3, #1
	ldrh	r4, [r0, r1]
	add	r4, r4, #1
	strh	r4, [r0, r1]	@ movhi
.L635:
	ldrh	r1, [r2, ip]
	cmp	r3, r1
	bcc	.L636
.L634:
	ldr	r3, .L655
	ldr	r6, [r3, #-1780]
	ldr	r5, [r3, #-1788]
	add	r2, r6, #256
	cmp	r2, r5
	mov	r2, r3
	bls	.L637
	ldr	r3, [r3, #-1784]
	add	r1, r6, #768
	cmp	r1, r3
	bls	.L637
	ldr	r3, .L655+4
	ldr	r3, [r3, #3840]
	cmp	r3, #0
	beq	.L654
	cmp	r6, #30
	bhi	.L654
.L637:
	ldr	r3, .L655+8
	ldrh	r0, [r2, r3]
	add	r0, r0, r0, asl #1
	ubfx	r0, r0, #2, #16
	bl	GetFreeBlockMaxEraseCount
	add	r3, r6, #64
	cmp	r0, r3
	mov	r8, r0
	bcs	.L639
	cmp	r6, #30
	bhi	.L654
.L639:
	ldr	r1, .L655
	ldr	r3, [r1, #-2020]
	cmp	r3, #0
	beq	.L654
	ldr	r2, .L655+4
	mov	r0, #3856
	movw	r7, #65535
	mov	r4, r7
	mov	fp, r7
	ldrh	sl, [r2, r0]
	ldr	r0, [r1, #-2024]
	ldr	r1, [r2, #4048]
	mov	r2, #0
	b	.L640
.L643:
	add	r2, r2, #1
	uxth	r2, r2
	cmp	r2, sl
	bhi	.L654
	ldrh	ip, [r3, #4]
	cmp	ip, #0
	beq	.L641
	rsb	r3, r0, r3
	ldr	ip, .L655+12
	mov	r3, r3, asr #1
	mul	r3, ip, r3
	uxth	r3, r3
	mov	ip, r3, asl #1
	ldrh	ip, [r1, ip]
	cmp	ip, r6
	bls	.L651
	cmp	ip, r7
	movcc	r7, ip
	movcc	r4, r3
.L641:
	mov	ip, #6
	mla	r3, ip, r9, r0
.L640:
	ldrh	r9, [r3, #0]
	cmp	r9, fp
	bne	.L643
	b	.L642
.L651:
	mov	r4, r3
.L642:
	movw	r3, #65535
	cmp	r4, r3
	beq	.L638
	mov	r9, r4, asl #1
	ldrh	sl, [r1, r9]
	cmp	sl, r6
	bls	.L644
	bl	GetFreeBlockMinEraseCount
	cmp	r0, r6
	ldrhi	r3, .L655
	strhi	r7, [r3, #-1780]
.L644:
	cmp	sl, r5
	bcs	.L654
	add	r3, sl, #128
	cmp	r8, r3
	ble	.L654
	add	r3, sl, #256
	ldr	r6, .L655
	cmp	r3, r5
	bcc	.L645
	ldr	r3, [r6, #-1784]
	add	sl, sl, #768
	cmp	sl, r3
	bcs	.L654
.L645:
	ldr	r3, [r6, #-2016]
	mov	r1, r4
	ldr	r0, .L655+16
	mov	r2, r5
	ldrh	r3, [r3, r9]
	str	r3, [sp, #0]
	ldr	r3, .L655+4
	ldr	r3, [r3, #4048]
	ldrh	r3, [r3, r9]
	stmib	sp, {r3, r8}
	ldr	r3, [r6, #-1784]
	bl	printk
	mov	r3, #1
	str	r3, [r6, #-1184]
	b	.L638
.L654:
	movw	r4, #65535
.L638:
	mov	r0, r4
	add	sp, sp, #20
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, sl, fp, pc}
.L656:
	.align	2
.L655:
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
	beq	.L658
	ldr	r2, .L659
	mov	r3, r0, asl #1
	mov	r1, #0
	ldr	r2, [r2, #-2016]
	strh	r1, [r2, r3]	@ movhi
	bl	INSERT_FREE_LIST
.L658:
	mov	r0, #0
	ldmfd	sp!, {r3, pc}
.L660:
	.align	2
.L659:
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
	ldr	r2, .L669
	mov	r3, #0
	stmfd	sp!, {r4, r5, r6, r7, r8, r9, sl, fp, lr}
	.save {r4, r5, r6, r7, r8, r9, sl, fp, lr}
	movw	sl, #3848
	str	r3, [r2, #-1180]
	mov	r8, #12
	ldr	r0, .L669+4
	mov	r7, #1
	movw	r6, #3926
	movw	fp, #3928
	b	.L662
.L663:
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
.L662:
	ldrh	r1, [r0, sl]
	cmp	r3, r1
	bcc	.L663
	b	.L668
.L665:
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
	b	.L667
.L668:
	ldr	r3, .L669
	mov	r8, #12
	ldr	ip, .L669+4
	mov	r7, #0
	movw	r6, #3926
	movw	r5, #3928
.L667:
	ldr	r2, [r3, #-1160]
	cmp	r1, r2
	bcc	.L665
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, sl, fp, pc}
.L670:
	.align	2
.L669:
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
	ldr	r3, .L678
	stmfd	sp!, {r4, r5, r6, r7, r8, r9, sl, fp, lr}
	.save {r4, r5, r6, r7, r8, r9, sl, fp, lr}
	mov	r5, #36
	ldr	r6, [r3, #-1160]
	mov	fp, #12
	ldr	ip, [r3, #-1176]
	mov	r3, #0
	mov	r4, r3
	b	.L672
.L675:
	mul	sl, fp, r2
	add	r8, ip, sl
	ldr	r9, [ip, sl]
	ldr	sl, [r7, #8]
	cmp	r9, sl
	streq	r4, [r8, #8]
	beq	.L674
.L673:
	add	r2, r2, #1
	uxth	r2, r2
.L677:
	cmp	r2, r6
	bcc	.L675
.L674:
	add	r3, r3, #1
	uxth	r3, r3
.L672:
	cmp	r3, r1
	ldmcsfd	sp!, {r4, r5, r6, r7, r8, r9, sl, fp, pc}
	mla	r7, r5, r3, r0
	mov	r2, #0
	b	.L677
.L679:
	.align	2
.L678:
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
	ldr	r3, .L687
	stmfd	sp!, {r4, r5, r6, r7, r8, sl, lr}
	.save {r4, r5, r6, r7, r8, sl, lr}
	mov	r6, #12
	ldr	r8, [r3, #-1160]
	mov	r5, #1
	ldr	r7, [r3, #-1176]
	mov	r4, #36
	mov	r3, #0
	b	.L681
.L684:
	mla	ip, r6, r2, r7
	ldr	sl, [ip, #8]
	cmp	sl, #0
	bne	.L682
	mla	r2, r4, r3, r0
	ldr	sl, [ip, #0]
	str	r5, [ip, #8]
	str	sl, [r2, #8]
	ldr	ip, [ip, #4]
	str	ip, [r2, #12]
	b	.L683
.L682:
	add	r2, r2, #1
	uxth	r2, r2
	b	.L685
.L686:
	mov	r2, #0
.L685:
	cmp	r2, r8
	bcc	.L684
.L683:
	add	r3, r3, #1
	uxth	r3, r3
.L681:
	cmp	r3, r1
	bcc	.L686
	ldmfd	sp!, {r4, r5, r6, r7, r8, sl, pc}
.L688:
	.align	2
.L687:
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
	ldr	r3, .L694
	ldr	r2, .L694+4
	ldrh	r1, [r3, r2]
	ldr	r2, [r3, #-1152]
	mov	r3, #0
	b	.L690
.L692:
	ldrh	ip, [r2], #2
	cmp	ip, r0
	beq	.L693
	add	r3, r3, #1
	uxth	r3, r3
.L690:
	cmp	r3, r1
	bne	.L692
	mov	r0, #0
	bx	lr
.L693:
	mov	r0, #1
	bx	lr
.L695:
	.align	2
.L694:
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
	ldr	r3, .L700
	ldr	r2, .L700+4
	ldrh	ip, [r3, r2]
	ldr	r2, [r3, #-1152]
	mov	r3, #0
	mov	r1, r2
	b	.L697
.L699:
	ldrh	r7, [r1], #2
	cmp	r7, r0
	beq	.L698
	add	r3, r3, #1
	uxth	r3, r3
.L697:
	cmp	r3, ip
	bne	.L699
	mov	r3, r3, asl #1
	strh	r0, [r2, r3]	@ movhi
	ldr	r2, .L700
	ldr	r3, .L700+4
	ldrh	r1, [r2, r3]
	add	r1, r1, #1
	strh	r1, [r2, r3]	@ movhi
.L698:
	ldr	r3, .L700
	mov	r0, #12
	ldr	r2, .L700+8
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
.L701:
	.align	2
.L700:
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
	ldr	r0, .L705
	bl	printk
	ldr	r3, .L705+4
	ldr	ip, .L705+8
	ldrh	r5, [r3, ip]
	cmp	r5, r4
	beq	.L703
	ldr	r2, .L705+12
	ldrh	r0, [r3, r2]
	cmp	r0, r4
	beq	.L703
	movw	r1, #65535
	cmp	r5, r1
	streqh	r4, [r3, ip]	@ movhi
	beq	.L703
	cmp	r0, r1
	streqh	r4, [r3, r2]	@ movhi
.L703:
	mov	r0, #0
	ldmfd	sp!, {r3, r4, r5, pc}
.L706:
	.align	2
.L705:
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
	ldr	r0, .L710
	bl	printk
	ldr	r3, .L710+4
	ldr	ip, .L710+8
	ldrh	r5, [r3, ip]
	cmp	r5, r4
	beq	.L708
	ldr	r2, .L710+12
	ldrh	r0, [r3, r2]
	cmp	r0, r4
	beq	.L708
	movw	r1, #65535
	cmp	r5, r1
	streqh	r4, [r3, ip]	@ movhi
	beq	.L708
	cmp	r0, r1
	streqh	r4, [r3, r2]	@ movhi
.L708:
	mov	r0, #0
	ldmfd	sp!, {r3, r4, r5, pc}
.L711:
	.align	2
.L710:
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
	ldr	r7, .L717
	mov	r2, r4
	ldr	r6, .L717+4
	ldrh	r1, [r7, r6]
	mov	r5, r0
	ldr	r0, .L717+8
	bl	printk
	mov	r0, r5
	bl	FtlGcRefreshBlock
	ldr	r3, .L717+12
	ldr	r2, [r3, #3840]
	cmp	r2, #0
	beq	.L713
	ldr	r3, [r3, #4048]
	mov	r5, r5, asl #1
	ldrh	r2, [r3, r5]
	cmp	r2, #29
	subhi	r2, r2, #30
	strhih	r2, [r3, r5]	@ movhi
.L713:
	ldrh	r1, [r7, r6]
	mov	r3, #0
	ldr	r2, .L717+16
	b	.L714
.L716:
	ldrh	r0, [r2, #2]!
	cmp	r0, r4
	beq	.L715
	add	r3, r3, #1
	uxth	r3, r3
.L714:
	cmp	r3, r1
	bne	.L716
	cmp	r3, #15
	bhi	.L715
	ldr	r2, .L717
	add	r1, r2, r3, asl #1
	add	r3, r3, #1
	sub	r1, r1, #1136
	strh	r4, [r1, #0]	@ movhi
	ldr	r1, .L717+4
	strh	r3, [r2, r1]	@ movhi
.L715:
	mov	r0, #0
	ldmfd	sp!, {r3, r4, r5, r6, r7, pc}
.L718:
	.align	2
.L717:
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
	ldr	r2, .L722
	ldr	r3, .L722+4
	ldrh	r2, [r3, r2]
	cmp	r2, #0
	beq	.L720
	ldr	r1, .L722+8
	ldrh	r0, [r3, r1]
	movw	r1, #65535
	cmp	r0, r1
	bne	.L720
	ldr	r4, .L722+12
	ldr	r5, .L722+4
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
.L720:
	mov	r0, #0
	ldmfd	sp!, {r3, r4, r5, pc}
.L723:
	.align	2
.L722:
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
	beq	.L725
	bl	memset
.L725:
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
	ldr	r4, .L727
	mov	r5, #3920
	ldr	r2, .L727+4
	mov	r1, #255
	ldr	r6, .L727+8
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
.L728:
	.align	2
.L727:
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
	ldr	r7, .L739
	mov	r2, #6
	ldr	r4, .L739+4
	mov	r1, #0
	mov	r6, #0
	ldrh	r3, [r7, r3]
	mov	sl, r6
	ldr	r0, [r4, #-2024]
	mul	r2, r2, r3
	bl	ftl_memset
	ldr	r3, .L739+8
	str	r6, [r4, #-2004]
	str	r6, [r4, #-2020]
	strh	r6, [r4, r3]	@ movhi
	add	r3, r3, #8
	str	r6, [r4, #-2012]
	strh	r6, [r4, r3]	@ movhi
	add	r3, r3, #900
	strh	r6, [r4, r3]	@ movhi
	mov	r4, r6
	b	.L730
.L732:
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
	b	.L737
.L738:
	mov	r5, #0
	uxth	fp, r4
	mov	r9, r5
	movw	r3, #3916
.L737:
	movw	r0, #3848
	sxth	r2, r9
	ldrh	r1, [r7, r0]
	cmp	r2, r1
	blt	.L732
	cmp	r5, #0
	ldr	r9, .L739+4
	beq	.L733
	sxth	r1, r5
	mov	r0, #32768
	bl	__aeabi_idiv
	uxth	r5, r0
	b	.L734
.L733:
	sxth	r3, r4
	ldr	r2, [r9, #-2016]
	mvn	r1, #0
	mov	r3, r3, asl #1
	strh	r1, [r2, r3]	@ movhi
.L734:
	sxth	r1, r4
	ldr	r0, [r9, #-2024]
	ldr	r3, .L739+4
	mov	r2, r1, asl #1
	add	r1, r2, r1
	add	r1, r0, r1, asl #1
	strh	r5, [r1, #4]	@ movhi
	ldr	r1, .L739+12
	ldrh	r1, [r9, r1]
	cmp	r8, r1
	beq	.L735
	ldr	r1, .L739+16
	ldrh	r1, [r3, r1]
	cmp	r8, r1
	beq	.L735
	ldr	r1, .L739+20
	ldrh	r1, [r3, r1]
	cmp	r8, r1
	beq	.L735
	ldr	r3, [r3, #-2016]
	uxth	r0, r4
	ldrh	r3, [r3, r2]
	cmp	r3, #0
	bne	.L736
	add	r6, r6, #1
	uxth	r6, r6
	bl	INSERT_FREE_LIST
	b	.L735
.L736:
	add	sl, sl, #1
	uxth	sl, sl
	bl	INSERT_DATA_LIST
.L735:
	add	r4, r4, #1
	uxth	r4, r4
.L730:
	mov	r3, #3856
	sxth	r8, r4
	ldrh	r3, [r7, r3]
	cmp	r8, r3
	blt	.L738
	ldr	r3, .L739+4
	mov	r0, #0
	ldr	r2, .L739+8
	strh	sl, [r3, r2]	@ movhi
	add	r2, r2, #8
	strh	r6, [r3, r2]	@ movhi
	ldmfd	sp!, {r1, r2, r3, r4, r5, r6, r7, r8, r9, sl, fp, pc}
.L740:
	.align	2
.L739:
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
	ldr	r4, .L744
	movw	r6, #3926
	ldr	r5, .L744+4
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
	b	.L742
.L743:
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
.L742:
	ldrh	r3, [r4, r7]
	ldr	r1, .L744
	cmp	r3, r2
	ldr	r3, .L744+4
	bhi	.L743
	ldr	r0, .L744+8
	mvn	r2, #0
	ldr	ip, [r1, #3944]
	strh	r2, [r3, r0]	@ movhi
	ldr	r0, .L744+12
	strh	r2, [r3, r0]	@ movhi
	add	r0, r0, #10
	strh	ip, [r3, r0]	@ movhi
	sub	r0, r0, #6
	ldr	ip, .L744+16
	strh	ip, [r3, r0]	@ movhi
	add	r0, r0, #40
	ldrh	ip, [r3, r0]
	sub	r0, r0, #36
	strh	ip, [r3, r0]	@ movhi
	mov	r0, #3952
	ldrh	r0, [r1, r0]
	ldr	r1, .L744+20
	strh	r0, [r3, r1]	@ movhi
	ldr	r1, [r3, #-1040]
	str	r1, [r3, #-1076]
	ldr	r1, [r3, #-1036]
	str	r1, [r3, #-1072]
	ldr	r1, [r3, #-1096]
	str	r1, [r3, #-1068]
	ldr	r1, [r3, #-1032]
	str	r1, [r3, #-1064]
	ldr	r1, .L744+24
	strh	r2, [r3, r1]	@ movhi
	ldmfd	sp!, {r3, r4, r5, r6, r7, r8, sl, pc}
.L745:
	.align	2
.L744:
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
	b	.L747
.L751:
	ldr	r0, [r7, r3, asl #2]
	mov	r2, #0
	ubfx	r0, r0, #10, #16
	b	.L748
.L750:
	mov	r1, r2, asl #1
	add	r2, r2, #1
	ldrh	ip, [r6, r1]
	uxth	r2, r2
	cmp	ip, r0
	ldreqh	ip, [r5, r1]
	addeq	ip, ip, #1
	streqh	ip, [r5, r1]	@ movhi
.L748:
	ldrh	r1, [r4, #10]
	cmp	r1, r2
	bhi	.L750
	add	r3, r3, #1
	uxth	r3, r3
.L747:
	ldrh	r2, [r4, #6]
	cmp	r2, r3
	bhi	.L751
	mov	sl, #0
	ldrh	fp, [r5, #0]
	mov	r7, sl
	ldr	r3, .L757
	movw	r2, #3918
	b	.L752
.L756:
	ldrh	r1, [r4, #0]
	cmp	r1, r7
	bne	.L753
	ldrh	r0, [r4, #2]
	ldrh	r1, [r3, r2]
	cmp	r0, r1
	movcc	r0, r7, asl #1
	strcch	r1, [r5, r0]	@ movhi
.L753:
	mov	r9, r7, asl #1
	ldrh	r8, [r5, r9]
	cmp	fp, r8
	movhi	sl, r7
	movhi	fp, r8
	cmp	r8, #0
	bne	.L755
	ldrh	r0, [r6, r9]
	cmp	r0, #0
	beq	.L755
	mov	r1, #1
	stmia	sp, {r2, r3}
	bl	FtlFreeSysBlkQueueIn
	strh	r8, [r6, r9]	@ movhi
	ldmia	sp, {r2, r3}
	ldrh	r1, [r4, #8]
	sub	r1, r1, #1
	strh	r1, [r4, #8]	@ movhi
.L755:
	add	r7, r7, #1
	uxth	r7, r7
.L752:
	ldrh	r1, [r4, #10]
	cmp	r1, r7
	bhi	.L756
	mov	r0, sl
	ldmfd	sp!, {r1, r2, r3, r4, r5, r6, r7, r8, r9, sl, fp, pc}
.L758:
	.align	2
.L757:
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
	ldr	r3, .L760
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
.L761:
	.align	2
.L760:
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
	ldr	r3, .L763
	movw	r2, #3980
	mvn	r1, #0
	ldr	r0, .L763+4
	strh	r1, [r3, r2]	@ movhi
	add	r2, r2, #6
	mov	r1, #0
	strh	r1, [r3, r2]	@ movhi
	mov	r1, #255
	mov	r2, #16
	b	ftl_memset
.L764:
	.align	2
.L763:
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
	ldr	r3, .L769
	ldr	r2, .L769+4
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
.L767:
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
	bne	.L767
	ldmfd	sp!, {r4, r5, r6, pc}
.L770:
	.align	2
.L769:
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
	ldr	r6, .L772
	mov	r4, #0
	ldr	r2, .L772+4
	mov	r1, r4
	ldr	r5, .L772+8
	movw	r7, #3858
	str	r3, [r6, #-1008]
	strh	r3, [r6, r2]	@ movhi
	movw	r3, #3962
	strh	r4, [r5, r3]	@ movhi
	mov	r3, #3936
	ldrh	r2, [r5, r3]
	ldr	r0, [r5, #3964]
	str	r4, [r6, #-1024]
	mov	r2, r2, asl #1
	str	r4, [r6, #-1016]
	str	r4, [r6, #-1012]
	str	r4, [r5, #3840]
	bl	ftl_memset
	ldrh	r2, [r5, r7]
	mov	r1, r4
	ldr	r0, [r5, #4048]
	mov	r2, r2, asl #1
	bl	ftl_memset
	ldrh	r2, [r5, r7]
	mov	r1, r4
	ldr	r0, [r6, #-1004]
	mov	r2, r2, asl #1
	bl	ftl_memset
	mov	r1, r4
	mov	r2, #48
	ldr	r0, .L772+12
	bl	ftl_memset
	mov	r1, r4
	mov	r2, #512
	ldr	r0, .L772+16
	bl	ftl_memset
	bl	FtlGcBufInit
	bl	FtlL2PDataInit
	mov	r0, r4
	ldmfd	sp!, {r3, r4, r5, r6, r7, pc}
.L773:
	.align	2
.L772:
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
	ldr	r4, .L805
	mov	r0, #1024
	ldr	r3, .L805+4
	mov	r7, #12
	ldr	r2, .L805+8
	movw	sl, #3848
	ldr	r5, .L805+12
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
	ldr	r3, .L805+16
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
	str	r6, [r5, #4052]
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
	ldr	r4, .L805
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
	str	r0, [r5, #4044]
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
	ldr	sl, .L805+20
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
	str	r0, [r5, #4048]
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
	ldr	r6, .L805+24
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
	ldr	ip, .L805+12
	mov	r1, r1, asl #2
	mov	r2, r1
	str	r0, [r5, #4008]
	ldrh	r5, [r5, r7]
	ldr	r0, .L805+28
	b	.L776
.L777:
	ldr	r4, [ip, #4008]
	add	r3, r3, #1
	add	r4, r4, r2
	add	r2, r2, r1
	str	r4, [r0, #4]!
.L776:
	cmp	r3, r5
	bcc	.L777
	ldr	r0, .L805+32
	mov	r2, #0
	mov	r1, r2
	add	r0, r0, r3, asl #2
	b	.L778
.L779:
	add	ip, r0, r2
	add	r3, r3, #1
	add	r2, r2, #4
	str	r1, [ip, #28]
.L778:
	cmp	r3, #7
	bls	.L779
	ldr	r3, .L805
	ldr	r2, [r3, #-1040]
	cmp	r2, #0
	beq	.L804
.L780:
	ldr	r2, [r3, #-1096]
	cmp	r2, #0
	beq	.L804
.L782:
	ldr	r2, [r3, #-1032]
	cmp	r2, #0
	beq	.L804
.L783:
	ldr	r2, [r3, #-1036]
	cmp	r2, #0
	beq	.L804
.L784:
	ldr	r2, [r3, #-1852]
	cmp	r2, #0
	beq	.L804
.L785:
	ldr	r2, [r3, #-1092]
	cmp	r2, #0
	beq	.L804
.L786:
	ldr	r2, [r3, #-2024]
	cmp	r2, #0
	beq	.L804
.L787:
	ldr	r2, .L805+12
	ldr	r2, [r2, #4008]
	cmp	r2, #0
	beq	.L804
.L788:
	ldr	r3, [r3, #-2016]
	cmp	r3, #0
	beq	.L804
.L789:
	ldr	r3, .L805
	ldr	r2, [r3, #-1152]
	cmp	r2, #0
	beq	.L804
	ldr	r2, [r3, #-1148]
	cmp	r2, #0
	beq	.L804
.L791:
	ldr	r2, [r3, #-972]
	cmp	r2, #0
	beq	.L804
	ldr	r2, [r3, #-964]
	cmp	r2, #0
	beq	.L804
	ldr	r2, .L805+12
	ldr	r2, [r2, #4044]
	cmp	r2, #0
	beq	.L804
	ldr	r2, [r3, #-1164]
	cmp	r2, #0
	beq	.L804
	ldr	r2, [r3, #-968]
	cmp	r2, #0
	beq	.L804
.L793:
	ldr	r2, [r3, #-960]
	cmp	r2, #0
	beq	.L804
	ldr	r2, [r3, #-956]
	cmp	r2, #0
	beq	.L804
	ldr	r3, [r3, #-952]
	cmp	r3, #0
	beq	.L804
	ldr	r3, .L805
	ldr	r2, [r3, #-1172]
	cmp	r2, #0
	beq	.L804
	ldr	r2, [r3, #-944]
	cmp	r2, #0
	beq	.L804
	ldr	r2, [r3, #-940]
	cmp	r2, #0
	beq	.L804
	ldr	r2, [r3, #-1176]
	cmp	r2, #0
	beq	.L804
.L795:
	ldr	r2, [r3, #-936]
	cmp	r2, #0
	beq	.L804
	ldr	r2, [r3, #-932]
	cmp	r2, #0
	beq	.L804
	ldr	r2, [r3, #-1168]
	cmp	r2, #0
	beq	.L804
.L797:
	ldr	r2, .L805+12
	ldr	r1, [r2, #4048]
	cmp	r1, #0
	beq	.L804
	ldr	r3, [r3, #-1004]
	cmp	r3, #0
	beq	.L804
.L799:
	ldr	r3, [r2, #3964]
	cmp	r3, #0
	beq	.L804
.L800:
	ldr	r3, .L805
	ldr	r2, [r3, #-912]
	cmp	r2, #0
	beq	.L804
.L801:
	ldr	r2, [r3, #-908]
	cmp	r2, #0
	beq	.L804
.L802:
	ldr	r3, [r3, #-904]
	cmp	r3, #0
	bne	.L803
.L804:
	ldr	r0, .L805+36
	ldr	r1, .L805+40
	bl	printk
	mvn	r0, #0
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, sl, pc}
.L803:
	mov	r0, #0
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, sl, pc}
.L806:
	.align	2
.L805:
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
	ldr	r3, .L810
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
	ldr	r1, .L810+4
	b	.L808
.L809:
	ldrb	r5, [r3, r1]	@ zero_extendqisi2
	add	r3, r3, #1
	ldrb	ip, [r4, #10]	@ zero_extendqisi2
	orr	ip, ip, r2, asl r5
	strb	ip, [r4, #10]
.L808:
	uxtb	ip, r3
	cmp	ip, r0
	bcc	.L809
	ldmfd	sp!, {r3, r4, r5, pc}
.L811:
	.align	2
.L810:
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
	ldr	r1, .L824
	mov	r2, #0
	stmfd	sp!, {r3, r4, r5, r6, r7, r8, sl, lr}
	.save {r3, r4, r5, r6, r7, r8, sl, lr}
	ldr	r3, .L824+4
	strb	r2, [r1, #-896]
	ldr	r1, .L824+8
	strb	r2, [r3, #3762]
	movw	r2, #2902
	ldrh	r2, [r1, r2]
	cmp	r2, #256
	str	r2, [r3, #856]
	movhi	r2, #512
	bhi	.L823
	cmp	r2, #128
	bls	.L814
	mov	r2, #256
.L823:
	str	r2, [r3, #856]
.L814:
	mov	r1, #0
	mov	r2, #8
	ldr	r0, .L824+12
	mov	r6, #0
	bl	ftl_memset
	mov	r1, #0
	mov	r2, #32
	ldr	r0, .L824+16
	bl	ftl_memset
	ldr	r0, .L824+20
	mov	r1, #0
	mov	r2, #128
	bl	ftl_memset
	ldr	r7, .L824+24
	ldr	r5, .L824+4
	mov	r8, r7
.L816:
	ldr	r4, [r5, #3624]
	mov	r1, r8
	add	r0, r4, #1
	ldrb	r2, [r4, #0]	@ zero_extendqisi2
	bl	FlashMemCmp8
	cmp	r0, #0
	bne	.L815
	ldrb	r3, [r5, #3762]	@ zero_extendqisi2
	add	r2, r5, r3, asl #2
	str	r0, [r2, #3588]
	add	r2, r5, r3
	add	r3, r3, #1
	strb	r3, [r5, #3762]
	strb	r6, [r2, #3764]
.L815:
	add	r6, r6, #1
	add	r8, r8, #8
	cmp	r6, #4
	bne	.L816
	ldr	r5, .L824+4
	ldr	r3, .L824
	ldrb	r2, [r5, #3762]	@ zero_extendqisi2
	strb	r2, [r3, #-896]
	ldrb	r3, [r4, #8]	@ zero_extendqisi2
	cmp	r3, #2
	bne	.L817
	add	sl, r4, #1
	mov	r6, #0
	mov	r8, r5
.L820:
	mov	r0, sl
	mov	r1, r7
	ldrb	r2, [r4, #0]	@ zero_extendqisi2
	bl	FlashMemCmp8
	cmp	r0, #0
	bne	.L818
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
.L818:
	add	r6, r6, #1
	add	r7, r7, #8
	cmp	r6, #4
	bne	.L820
.L817:
	ldr	r3, .L824+4
	ldrb	r1, [r4, #13]	@ zero_extendqisi2
	ldr	r2, .L824
	ldrb	r3, [r3, #3762]	@ zero_extendqisi2
	mul	r1, r1, r3
	ldrh	r3, [r4, #14]
	mul	r1, r3, r1
	ldr	r3, .L824+28
	strh	r1, [r2, r3]	@ movhi
	ldmfd	sp!, {r3, r4, r5, r6, r7, r8, sl, pc}
.L825:
	.align	2
.L824:
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
	bne	.L827
	ldr	r3, .L866
.L828:
	strh	r0, [r3, #2]!	@ movhi
	add	r0, r0, #1
	uxth	r0, r0
	cmp	r0, #256
	bne	.L828
	b	.L829
.L827:
	cmp	r0, #1
	bne	.L830
	ldr	r0, .L866+4
	mov	r2, #0
	mov	r3, r2
.L833:
	uxth	r1, r3
	cmp	r1, #3
	bls	.L831
	tst	r3, #1
	moveq	r1, #2
	movne	r1, #3
	rsb	r1, r1, r2
	uxth	r1, r1
.L831:
	add	r3, r3, #1
	strh	r1, [r2, r0]	@ movhi
	cmp	r3, #256
	add	r2, r2, #2
	bne	.L833
	b	.L829
.L830:
	cmp	r0, #2
	bne	.L834
	mov	r3, #0
	ldr	r0, .L866
	movw	r2, #65535
	mov	r1, r3
	b	.L865
.L837:
	cmp	r3, #1
	movls	r1, r3
	movhi	r1, r2
.L865:
	add	r3, r3, #1
	add	r2, r2, #2
	strh	r1, [r0, #2]!	@ movhi
	uxth	r3, r3
	uxth	r2, r2
	cmp	r3, #256
	bne	.L837
	b	.L829
.L834:
	cmp	r0, #3
	bne	.L838
	ldr	r0, .L866+4
	mov	r2, #0
	mov	r3, r2
.L841:
	uxth	r1, r3
	cmp	r1, #5
	bls	.L839
	tst	r3, #1
	moveq	r1, #4
	movne	r1, #5
	rsb	r1, r1, r2
	uxth	r1, r1
.L839:
	add	r3, r3, #1
	strh	r1, [r2, r0]	@ movhi
	cmp	r3, #256
	add	r2, r2, #2
	bne	.L841
	b	.L829
.L838:
	cmp	r0, #4
	mov	r2, #0
	bne	.L842
	ldr	r3, .L866+8
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
	ldr	r0, .L866+12
	strh	r1, [r3, r2]	@ movhi
	mov	r2, #3088
	mov	r1, #7
	strh	r1, [r3, r2]	@ movhi
	add	r2, r2, #2
	mov	r1, #8
	strh	r1, [r3, r2]	@ movhi
	mov	r2, #16
	mov	r3, r1
.L844:
	tst	r3, #1
	add	r3, r3, #1
	moveq	r1, #6
	movne	r1, #7
	cmp	r3, #256
	rsb	r1, r1, r2
	add	r2, r2, #2
	strh	r1, [r0, #2]!	@ movhi
	uxth	r2, r2
	bne	.L844
	b	.L829
.L842:
	cmp	r0, #5
	bne	.L845
	ldr	r1, .L866
	mov	r3, r2
.L846:
	strh	r3, [r1, #2]!	@ movhi
	add	r3, r3, #1
	uxth	r3, r3
	cmp	r3, #16
	bne	.L846
	ldr	r2, .L866+16
.L847:
	strh	r3, [r2, #2]!	@ movhi
	add	r3, r3, #2
	uxth	r3, r3
	cmp	r3, #496
	bne	.L847
	b	.L829
.L845:
	cmp	r0, #6
	bne	.L829
	ldr	r0, .L866
	mov	r3, r2
.L850:
	uxth	r1, r3
	cmp	r1, #5
	bls	.L848
	tst	r3, #1
	moveq	r1, #10
	movne	r1, #12
	rsb	r1, r1, r2
	uxth	r1, r1
.L848:
	add	r3, r3, #1
	add	r2, r2, #3
	cmp	r3, #256
	strh	r1, [r0, #2]!	@ movhi
	uxth	r2, r2
	bne	.L850
.L829:
	mov	r2, #1024
	ldr	r0, .L866+20
	mov	r1, #255
	uxth	r4, r4
	bl	ftl_memset
	ldr	r2, .L866
	mov	r3, #0
	ldr	r0, .L866+24
	b	.L851
.L852:
	ldrh	r1, [r2, #2]!
	add	r3, r3, #1
	uxth	r3, r3
	add	ip, r0, r1, asl #1
	sub	ip, ip, #892
	strh	r1, [ip, #0]	@ movhi
.L851:
	cmp	r3, r4
	bcc	.L852
	ldmfd	sp!, {r4, pc}
.L867:
	.align	2
.L866:
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
	b	.L870
.L871:
	ldr	r4, [r1, r3]
	add	ip, ip, #1
	str	r4, [r0, r3]
	add	r3, r3, #4
.L870:
	cmp	ip, r2
	bne	.L871
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
	ldr	r0, .L876
	b	.L874
.L875:
	mov	ip, r0, asl #5
	ldrb	r4, [r3, r2]	@ zero_extendqisi2
	add	ip, ip, r0, lsr #2
	add	r2, r2, #1
	add	ip, ip, r4
	eor	r0, r0, ip
.L874:
	cmp	r2, r1
	bne	.L875
	ldmfd	sp!, {r4, pc}
.L877:
	.align	2
.L876:
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
	ldr	r3, .L879
	ldr	r0, [r3, #0]
	b	jiffies_to_msecs
.L880:
	.align	2
.L879:
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
	ldr	r4, .L884
	mov	ip, r0
	mov	r2, r3
	ldr	r4, [r4, #132]
	add	r4, r4, #4096
	add	r1, r4, r1
	movne	r0, r1
	movne	r1, ip
	ldmfd	sp!, {r4, lr}
	b	memcpy
.L885:
	.align	2
.L884:
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
	ldr	r3, .L890
	ldr	r0, [r3, #3956]
	bx	lr
.L891:
	.align	2
.L890:
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
	b	.L896
.L902:
	cmp	r5, #0
	bne	.L897
	ldr	r0, .L904
	mov	r1, r8
	mov	r2, r6
	mov	r3, r4
	bl	printk
.L897:
	cmp	r7, #4
	ldreq	r0, .L904+4
	ldreq	r1, [r6, r4, asl #2]
	beq	.L903
	cmp	r7, #2
	moveq	r3, r4, asl #1
	ldreq	r0, .L904+4
	ldreqsh	r1, [r6, r3]
	ldrne	r0, .L904+4
	ldrneb	r1, [r6, r4]	@ zero_extendqisi2
.L903:
	add	r5, r5, #1
	bl	printk
	cmp	r5, #15
	bls	.L901
	ldr	r0, .L904+8
	mov	r5, #0
	ldr	r1, .L904+12
	bl	printk
.L901:
	add	r4, r4, #1
.L896:
	cmp	r4, sl
	bne	.L902
	ldr	r0, .L904+8
	ldr	r1, .L904+12
	ldmfd	sp!, {r3, r4, r5, r6, r7, r8, sl, lr}
	b	printk
.L905:
	.align	2
.L904:
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
	ldr	r6, .L929
	add	r0, r6, r0, asl #3
	ldr	r3, [r6, #3788]
	ldr	r4, [r0, #864]
	cmp	r3, #3
	bls	.L923
	ldr	r3, [r4, #16]
	tst	r3, #4
	beq	.L923
	mov	r0, r4
	bl	wait_for_nandc_xfer_completed
	ldr	r5, [r4, #16]
	ldr	r3, [r4, #8]
	ands	r5, r5, #2
	str	r3, [sp, #0]
	movne	r5, #0
	bne	.L927
	b	.L928
.L913:
	ldr	r3, [r6, #3788]
	cmp	r3, #5
	bls	.L910
	ldr	r3, [r4, #0]
	str	r3, [sp, #4]
	ldr	r3, [sp, #4]
	tst	r3, #8192
	beq	.L910
	ldr	r3, [sp, #4]
	tst	r3, #131072
	bne	.L911
.L910:
	add	r5, r5, #1
	bic	r3, r5, #-16777216
	cmp	r3, #0
	bne	.L927
	ldr	r2, [r4, #28]
	mov	r1, r5
	ldr	r3, [sp, #0]
	ubfx	r2, r2, #16, #5
	ldr	r0, .L929+4
	ubfx	r3, r3, #22, #6
	bl	printk
	ldr	r0, .L929+8
	mov	r1, r4
	mov	r2, #4
	mov	r3, #512
	bl	rknand_print_hex
.L927:
	ldr	r2, [r4, #28]
	ldr	r3, [sp, #0]
	ubfx	r2, r2, #16, #5
	ubfx	r3, r3, #22, #6
	cmp	r2, r3
	blt	.L913
.L911:
	ldr	r4, .L929
	ldr	r3, [r4, #3820]
	cmp	r3, #0
	beq	.L914
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
	b	.L914
.L917:
	ldr	r3, [r4, #8]
	add	r5, r5, #1
	str	r3, [sp, #0]
	bic	r3, r5, #-16777216
	cmp	r3, #0
	bne	.L928
	ldr	r2, [sp, #0]
	mov	r1, r5
	ldr	r3, [r4, #28]
	ldr	r0, .L929+12
	ubfx	r3, r3, #16, #5
	bl	printk
	ldr	r0, .L929+8
	mov	r1, r4
	mov	r2, #4
	mov	r3, #512
	bl	rknand_print_hex
.L928:
	ldr	r3, [sp, #0]
	tst	r3, #1048576
	beq	.L917
	ldr	r6, .L929
	ldr	r3, [r6, #3828]
	cmp	r3, #0
	beq	.L918
	mov	r0, r4
	bl	NandcSendDumpDataStart
.L918:
	ldr	r3, [r6, #3820]
	ldr	r5, .L929
	cmp	r3, #0
	beq	.L919
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
.L919:
	ldr	r3, .L929
	ldr	r3, [r3, #3828]
	cmp	r3, #0
	beq	.L914
	mov	r0, r4
	bl	NandcSendDumpDataDone
.L914:
	ldr	r3, .L929
	mov	r2, #0
	str	r2, [r3, #3820]
	b	.L906
.L923:
	ldr	r3, [r4, #8]
	str	r3, [sp, #0]
	ldr	r3, [sp, #0]
	tst	r3, #1048576
	beq	.L923
.L906:
	ldmfd	sp!, {r2, r3, r4, r5, r6, pc}
.L930:
	.align	2
.L929:
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
	ldr	r3, .L969
	.pad #92
	sub	sp, sp, #92
	tst	r4, #63
	add	r3, r3, r0, asl #3
	mov	r7, r0
	mov	fp, r1
	mov	sl, r2
	ldr	r5, [sp, #128]
	ldr	r6, [r3, #864]
	bne	.L932
	cmp	r5, #0
	bne	.L933
	add	r0, sp, #24
	mov	r1, #255
	mov	r2, #64
	add	r5, sp, #24
	bl	ftl_memset
.L933:
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
	bne	.L934
	ldr	r1, .L969
	mov	r0, sl, lsr #1
	mov	r3, fp
	mov	r2, fp
	ldr	ip, [r1, #3832]
	mov	r4, r1
	cmp	ip, #24
	movhi	ip, #128
	movls	ip, #64
	b	.L936
.L937:
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
.L936:
	cmp	r2, r0
	add	r7, r3, ip
	ldr	r1, .L969
	bcc	.L937
	mov	r3, #0
	ldr	r0, [r1, #3832]
	mov	sl, sl, lsr #2
	ldr	r1, [r1, #3788]
	mov	r4, r3
	b	.L938
.L944:
	add	r2, r3, #8
	ldr	r2, [r6, r2, asl #2]
	str	r2, [sp, #20]
	ldr	r2, [sp, #20]
	tst	r2, #4
	bne	.L959
	ldr	r2, [sp, #20]
	ands	r2, r2, #32768
	bne	.L959
	cmp	r1, #5
	bls	.L940
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
	b	.L967
.L940:
	cmp	r1, #3
	bls	.L942
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
.L967:
	orr	r2, ip, r2, asl #5
.L942:
	cmp	r4, r2
	movcc	r4, r2
	b	.L939
.L959:
	mvn	r4, #0
.L939:
	add	r3, r3, #1
.L938:
	cmp	r3, sl
	bcs	.L934
	cmp	r0, #0
	bne	.L944
.L934:
	mov	r3, #0
	str	r3, [r6, #16]
	b	.L945
.L932:
	cmp	r1, #1
	mov	r8, #0
	bne	.L965
	b	.L946
.L949:
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
.L946:
	cmp	r8, sl
	bcc	.L949
	mov	r4, #0
	b	.L945
.L965:
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
	b	.L968
.L954:
	mov	r0, r7
	bl	NandcXferComp
	ldr	r3, [fp, #32]
	add	ip, r5, #2
	cmp	ip, sl
	str	r3, [sp, #20]
	bcs	.L951
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
.L951:
	ldr	r3, [sp, #20]
	tst	r3, #4
	mvnne	r4, #0
	bne	.L952
	ldr	r2, [sp, #20]
	ldr	r3, [sp, #20]
	ubfx	r2, r2, #3, #5
	ubfx	r3, r3, #27, #1
	orr	r3, r2, r3, asl #5
	cmp	r4, r3
	movcc	r4, r3
.L952:
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
.L968:
	cmp	ip, sl
	mov	r5, ip
	bcc	.L954
	mov	r6, fp
	ldr	fp, [sp, #12]
.L945:
	ldr	r3, .L969
	rsbs	fp, fp, #1
	movcc	fp, #0
	ldr	r3, [r3, #3788]
	cmp	r3, #5
	movls	fp, #0
	cmp	fp, #0
	beq	.L955
	ldr	r3, [r6, #0]
	and	r2, r3, #139264
	cmp	r2, #139264
	orreq	r3, r3, #131072
	streq	r3, [r6, #0]
	mvneq	r4, #0
.L955:
	mov	r0, r4
	add	sp, sp, #92
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, sl, fp, pc}
.L970:
	.align	2
.L969:
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
	ldr	r3, .L974
	subs	r4, r0, #0
	mov	r5, r1
	mov	r6, r2
	ldrb	r7, [r3, #2901]	@ zero_extendqisi2
	bne	.L972
	ldr	r3, .L974+4
	ldrb	r2, [r3, #853]	@ zero_extendqisi2
	ldr	r1, [r3, #856]
	mul	r2, r1, r2
	cmp	r5, r2
	bcs	.L972
	ldrb	r3, [r3, #852]	@ zero_extendqisi2
	cmp	r3, #0
	subeq	r7, r7, #2
	movne	r7, #4
.L972:
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
.L975:
	.align	2
.L974:
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
	ldr	r2, .L983
	mov	r7, r1
	mov	r8, r0
	ldrb	r1, [r2, #928]	@ zero_extendqisi2
	ldr	r3, [r2, #3624]
	cmp	r1, #0
	ldrb	r3, [r3, #19]	@ zero_extendqisi2
	beq	.L977
	ldr	r1, [r2, #3776]
	ldr	r2, .L983+4
	cmp	r1, r2
	ldmeqfd	sp!, {r3, r4, r5, r6, r7, r8, sl, pc}
.L977:
	sub	r2, r3, #5
	uxtb	r2, r2
	cmp	r3, #68
	cmpne	r2, #2
	bls	.L980
	cmp	r3, #35
	cmpne	r3, #19
	beq	.L980
	ldmfd	sp!, {r3, r4, r5, r6, r7, r8, sl, pc}
.L981:
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
	b	.L982
.L980:
	ldr	r6, .L983
	movw	sl, #65535
	ldr	r5, .L983+8
.L982:
	ldr	r3, [r6, #3624]
	ldrh	r3, [r3, #10]
	cmp	r3, r4
	bhi	.L981
	ldmfd	sp!, {r3, r4, r5, r6, r7, r8, sl, pc}
.L984:
	.align	2
.L983:
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
	ldr	r3, .L988
	subs	r4, r0, #0
	mov	r6, r1
	mov	r5, r2
	ldrb	r7, [r3, #2901]	@ zero_extendqisi2
	bne	.L986
	ldr	r3, .L988+4
	ldrb	r2, [r3, #853]	@ zero_extendqisi2
	ldr	r3, [r3, #856]
	mul	r3, r3, r2
	cmp	r1, r3
	movcc	r7, #4
.L986:
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
.L989:
	.align	2
.L988:
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
	ldr	r3, .L1004
	mov	sl, r2
	ldr	fp, .L1004
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
	b	.L992
.L997:
	add	r4, r4, #1
	ldrb	r1, [fp, #1]	@ zero_extendqisi2
	mov	r0, r6
	ldr	r2, .L1004+4
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
	beq	.L994
	ldrb	r3, [ip, #3836]	@ zero_extendqisi2
	cmn	r5, #1
	moveq	r5, r0
	add	r3, r3, r3, asl #1
	cmp	r0, r3, lsr #2
	bcc	.L1003
	mov	r7, #0
	mov	sl, r7
.L994:
	add	r8, r8, #1
.L992:
	cmp	r8, r9
	bcc	.L997
	b	.L996
.L1003:
	mov	r5, r0
.L996:
	ldr	r3, .L1004
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
.L1005:
	.align	2
.L1004:
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
	ldr	r3, .L1024
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
	beq	.L1023
	mov	r1, #3
	bl	__aeabi_idiv
	uxtb	r0, r0
.L1023:
.L1008:
	mov	r2, #0
	ldr	r3, .L1024
	mvn	r4, #0
	mov	r6, r2
	add	r3, r3, r9, asl #3
	str	r0, [sp, #16]
	str	r2, [sp, #12]
	str	r3, [sp, #28]
.L1018:
	mov	r0, r9
	mov	fp, #0
	bl	NandcWaitFlashReady
	ldr	r2, [sp, #28]
	ldrb	r7, [r2, #868]	@ zero_extendqisi2
	ldr	r8, [r2, #864]
	add	r7, r7, #8
	add	r5, r8, r7, asl #8
	b	.L1009
.L1013:
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
	beq	.L1010
	cmn	r4, #1
	ldr	r2, [sp, #16]
	moveq	r4, r0
	cmp	r0, r2
	bcc	.L1020
	mov	sl, #0
	str	sl, [sp, #20]
.L1010:
	mov	fp, ip
.L1009:
	ldr	r2, .L1024+4
	ldrb	r3, [r2, #140]	@ zero_extendqisi2
	cmp	fp, r3
	bcc	.L1013
	b	.L1012
.L1020:
	mov	r4, r0
	mov	sl, #0
	str	sl, [sp, #20]
.L1012:
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
	bne	.L1015
	mov	r3, fp
	ldr	r0, .L1024+8
	mov	r1, fp
	ldr	r2, [sp, #24]
	str	r4, [sp, #0]
	bl	printk
	ldr	r3, [sp, #12]
	cmp	r3, #0
	bne	.L1016
	ldr	r2, .L1024
	ldrb	r3, [r2, #928]	@ zero_extendqisi2
	cmp	r3, #0
	moveq	r5, #0
	andne	r5, r5, #1
	cmp	r5, #0
	beq	.L1017
	mov	r0, r9
	mov	r1, #3
	bl	micron_auto_read_calibration_config
	mov	r3, #1
	str	r3, [sp, #12]
	b	.L1018
.L1015:
	ldr	r2, [sp, #12]
	cmp	r2, #0
	beq	.L1017
.L1016:
	mov	r0, r9
	mov	r1, #0
	bl	micron_auto_read_calibration_config
	cmn	r4, #1
	movne	r4, #256
.L1017:
	mov	r0, r4
	add	sp, sp, #36
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, sl, fp, pc}
.L1025:
	.align	2
.L1024:
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
	ldr	ip, .L1035
	mov	r5, #1
	mvn	r4, #0
	ldr	fp, .L1035+4
	add	r3, ip, r8, asl #3
	ldrb	sl, [r3, #868]	@ zero_extendqisi2
	ldr	r2, [r3, #864]
	add	sl, sl, #8
	add	sl, r2, sl, asl #8
	b	.L1027
.L1031:
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
	beq	.L1028
	ldrb	r3, [ip, #3836]	@ zero_extendqisi2
	cmn	r4, #1
	moveq	r4, r0
	add	r3, r3, r3, asl #1
	cmp	r0, r3, lsr #2
	bcc	.L1034
	mov	r6, #0
	mov	r7, r6
.L1028:
	add	r5, r5, #1
.L1027:
	ldrb	r3, [fp, #140]	@ zero_extendqisi2
	add	r3, r3, #1
	cmp	r5, r3
	bcc	.L1031
	b	.L1030
.L1034:
	mov	r4, r0
.L1030:
	mov	r0, sl
	mov	r1, #0
	bl	SamsungSetRRPara
	ldr	r3, .L1035
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
.L1036:
	.align	2
.L1035:
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
	ldr	r3, .L1058
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
	bls	.L1038
	ldrb	sl, [r3, #3780]	@ zero_extendqisi2
	cmp	sl, #0
	beq	.L1039
	mov	r0, #0
	mov	sl, #1
	bl	NandcSetDdrMode
.L1039:
	mov	r3, #92
	str	r3, [r4, #8]
	mov	r3, #197
	str	r3, [r4, #8]
.L1038:
	mov	r6, #1
	mvn	r5, #0
	ldr	fp, .L1058
	b	.L1040
.L1049:
	ldrb	r3, [fp, #3761]	@ zero_extendqisi2
	mov	r0, r4
	uxtb	r1, r6
	sub	r3, r3, #67
	uxtb	r3, r3
	cmp	r3, #1
	bhi	.L1041
	bl	SandiskSetRRPara
	b	.L1042
.L1041:
	bl	ToshibaSetRRPara
.L1042:
	ldr	r2, .L1058
	ldrb	r3, [r2, #3761]	@ zero_extendqisi2
	cmp	r3, #34
	bne	.L1043
	ldr	r2, .L1058+4
	ldrb	r3, [r2, #140]	@ zero_extendqisi2
	sub	r3, r3, #3
	cmp	r6, r3
	moveq	r3, #179
	streq	r3, [r4, #8]
.L1043:
	cmp	sl, #0
	mov	r3, #38
	str	r3, [r4, #8]
	mov	r3, #93
	str	r3, [r4, #8]
	beq	.L1044
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
	b	.L1045
.L1044:
	mov	r3, r7
	mov	r0, r8
	ldr	r1, [sp, #4]
	mov	r2, r9
	bl	FlashReadRawPage
	mov	r3, r0
.L1045:
	cmn	r3, #1
	beq	.L1046
	ldrb	r2, [fp, #3836]	@ zero_extendqisi2
	cmn	r5, #1
	moveq	r5, r3
	add	r2, r2, r2, asl #1
	cmp	r3, r2, lsr #2
	bcc	.L1057
	mov	r7, #0
	mov	r9, r7
.L1046:
	add	r6, r6, #1
.L1040:
	ldr	r2, .L1058+4
	ldrb	r3, [r2, #140]	@ zero_extendqisi2
	add	r3, r3, #1
	cmp	r6, r3
	bcc	.L1049
	b	.L1048
.L1057:
	mov	r5, r3
.L1048:
	ldr	r6, .L1058
	mov	r0, r4
	mov	r1, #0
	ldrb	r3, [r6, #3761]	@ zero_extendqisi2
	sub	r3, r3, #67
	uxtb	r3, r3
	cmp	r3, #1
	bhi	.L1050
	bl	SandiskSetRRPara
	b	.L1051
.L1050:
	bl	ToshibaSetRRPara
.L1051:
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
	beq	.L1053
	mov	r0, #4
	bl	NandcSetDdrMode
.L1053:
	mov	r0, r5
	ldmfd	sp!, {r1, r2, r3, r4, r5, r6, r7, r8, r9, sl, fp, pc}
.L1059:
	.align	2
.L1058:
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
	ldr	r5, .L1067
	ldr	r7, .L1067+4
	ldr	r8, .L1067+8
	ldr	r3, [r5, #148]
	ldrb	r0, [r5, #152]	@ zero_extendqisi2
	str	r3, [r5, #144]
	bl	FlashBchSel
	mov	r1, #0
	mov	r2, #2048
	ldr	r0, [r5, #148]
	bl	ftl_memset
	ldr	r3, [r5, #144]
	ldr	r1, .L1067+12
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
	ldr	r1, .L1067+16
	mov	r2, #8
	bl	memcpy
	add	r0, r4, #96
	ldr	r1, .L1067+20
	mov	r2, #32
	bl	memcpy
	ldr	r0, [r5, #144]
	ldr	r1, .L1067+24
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
.L1063:
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
	beq	.L1061
	ldr	sl, [r5, #144]
	ldr	r3, [sl, #0]
	cmp	r3, r8
	bne	.L1061
	add	r0, sl, #12
	movw	r1, #2036
	bl	JSHash
	ldr	r3, [sl, #8]
	cmp	r3, r0
	bne	.L1061
	add	r3, r4, #1
	str	r3, [r5, #156]
	ldr	r3, [r7, #856]
	add	r6, r6, #1
	cmp	r6, #1
	mul	r3, r3, r4
	str	r3, [r5, #160]
	bhi	.L1062
.L1061:
	add	r4, r4, #1
	cmp	r4, #4
	bne	.L1063
.L1062:
	mov	r0, #0
	bl	flash_exit_slc_mode
	cmp	r6, #0
	mvneq	r0, #0
	movne	r0, #0
	ldmfd	sp!, {r3, r4, r5, r6, r7, r8, sl, pc}
.L1068:
	.align	2
.L1067:
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
	ldr	r1, .L1080
	add	r0, sp, #12
	bl	memcpy
	ldr	r3, .L1080+4
	ldrb	r2, [r3, #3836]	@ zero_extendqisi2
	str	r2, [sp, #4]
	ldr	r2, [r3, #3776]
	ldr	r3, .L1080+8
	cmp	r2, r3
	bne	.L1070
	mov	r0, #0
	bl	flash_enter_slc_mode
.L1070:
	ldr	r7, .L1080+4
	mov	r0, sl
	mov	r1, #0
	mov	r2, #2048
	bl	ftl_memset
	mvn	r8, #0
	mov	r4, #2
	ldr	r9, .L1080+12
	mov	fp, r7
	b	.L1071
.L1078:
	mov	r6, #0
.L1073:
	add	r3, sp, #12
	ldr	r5, .L1080+12
	ldrb	r0, [r3, r6]	@ zero_extendqisi2
	bl	FlashBchSel
	ldr	r1, [r7, #856]
	mov	r0, #0
	ldr	r2, [r9, #148]
	mov	r3, r0
	mul	r1, r1, r4
	bl	FlashReadRawPage
	cmn	r0, #1
	bne	.L1072
	add	r6, r6, #1
	cmp	r6, #4
	bne	.L1073
	b	.L1074
.L1072:
	ldr	r3, [r5, #148]
	ldr	r2, [r3, #0]
	ldr	r3, .L1080+16
	cmp	r2, r3
	bne	.L1074
	add	r3, sp, #16
	ldr	r0, .L1080+20
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
	bls	.L1077
	str	r4, [r5, #156]
	bl	FlashSavePhyInfo
	mov	r8, #0
.L1074:
	add	r4, r4, #1
.L1071:
	ldrb	r3, [r7, #853]	@ zero_extendqisi2
	cmp	r4, r3
	bcc	.L1078
	b	.L1075
.L1077:
	mov	r8, #0
.L1075:
	ldr	r0, [sp, #4]
	bl	FlashBchSel
	ldr	r3, .L1080+4
	ldr	r2, [r3, #3776]
	ldr	r3, .L1080+8
	cmp	r2, r3
	bne	.L1076
	mov	r0, #0
	bl	flash_exit_slc_mode
.L1076:
	mov	r0, r8
	add	sp, sp, #20
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, sl, fp, pc}
.L1081:
	.align	2
.L1080:
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
	ldr	r1, .L1093
	add	r0, sp, #4
	bl	memcpy
	ldr	r5, .L1093+4
	ldr	r2, .L1093+8
	movw	r3, #2902
	mov	r4, #0
	mov	r8, #4
	mov	r0, r4
	str	r4, [r5, #160]
	ldrh	r9, [r2, r3]
	mvn	r6, #0
	ldr	r3, [r5, #148]
	ldr	fp, .L1093+12
	str	r3, [r5, #144]
	bl	flash_enter_slc_mode
	b	.L1083
.L1085:
	add	r3, sp, #4
	ldrb	r0, [r3, r7]	@ zero_extendqisi2
	bl	FlashBchSel
	mov	r0, #0
	mov	r1, r4
	ldr	r2, [r5, #148]
	mov	r3, r0
	bl	FlashReadRawPage
	cmn	r0, #1
	bne	.L1084
	mov	r0, #0
	mov	r1, sl
	ldr	r2, [r5, #148]
	mov	r3, r0
	bl	FlashReadRawPage
	cmn	r0, #1
	bne	.L1084
	add	r7, r7, #1
	cmp	r7, #4
	beq	.L1086
	b	.L1085
.L1084:
	ldr	r7, [r5, #144]
	ldr	r3, [r7, #0]
	cmp	r3, fp
	bne	.L1086
	cmp	r6, #0
	bne	.L1087
	ldr	r2, .L1093+8
	movw	r3, #2902
	mov	r0, r4
	ldrh	r1, [r2, r3]
	bl	__aeabi_uidiv
	ldr	r3, .L1093+4
	add	r0, r0, #1
	str	r0, [r3, #156]
	mov	r0, r6
	b	.L1092
.L1087:
	add	r0, r7, #12
	movw	r1, #2036
	bl	JSHash
	ldr	r3, [r7, #8]
	cmp	r3, r0
	bne	.L1086
	ldr	r6, .L1093+16
	add	r1, r7, #160
	mov	r2, #32
	ldr	r0, .L1093+20
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
	ldr	r2, .L1093+8
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
.L1086:
	subs	r8, r8, #1
	add	r4, r4, r9
	beq	.L1090
.L1083:
	add	sl, r4, #1
	mov	r7, #0
	b	.L1085
.L1090:
	mov	r0, r8
.L1092:
	bl	flash_exit_slc_mode
	mov	r0, r6
	ldmfd	sp!, {r1, r2, r3, r4, r5, r6, r7, r8, r9, sl, fp, pc}
.L1094:
	.align	2
.L1093:
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
	ldr	r4, .L1116
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
	beq	.L1097
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
	beq	.L1098
	ldr	r0, .L1116+4
	mov	r2, r8
	ldr	r1, [sp, #0]
	bl	printk
	cmp	r8, #9
	bhi	.L1099
	add	r4, r4, fp, asl #3
	ldr	r3, [r4, #864]
	ldr	r2, [r3, #3840]
	ldr	r2, [r3, #0]
	orr	r2, r2, #131072
	str	r2, [r3, #0]
.L1099:
	ldr	r3, .L1116+8
	ldr	r2, [r3, #168]
	add	r2, r2, #1
	str	r2, [r3, #168]
	cmp	r2, #2048
	movcs	sl, #0
	strcs	sl, [r3, #168]
	movcs	r9, sl
	bcc	.L1098
.L1097:
	mov	r4, #0
	str	fp, [sp, #4]
	mov	r5, r4
	mvn	r6, #0
	mov	fp, r4
	str	r4, [sp, #8]
.L1104:
	uxtb	r0, r7
	bl	NandcSetDdrPara
	mov	r3, sl
	ldr	r0, [sp, #4]
	mov	r2, r9
	ldr	r1, [sp, #0]
	bl	FlashReadRawPage
	add	r3, r8, #1
	cmp	r0, r3
	bhi	.L1100
	cmp	r0, #2
	bhi	.L1110
	add	r5, r5, #1
	cmp	r5, #9
	bls	.L1110
	mov	ip, fp
	rsb	r4, r5, r7
	ldr	fp, [sp, #4]
	mov	r8, r0
	mov	r6, #0
	b	.L1102
.L1100:
	cmp	fp, r5
	bcs	.L1111
	cmp	r5, #7
	rsb	r3, r5, r4
	str	r3, [sp, #8]
	bhi	.L1115
	mov	fp, r5
	b	.L1111
.L1110:
	mov	r6, #0
	mov	r4, r7
	mov	r8, r0
	mov	sl, r6
	mov	r9, r6
	b	.L1101
.L1111:
	mov	r5, #0
.L1101:
	add	r7, r7, #2
	cmp	r7, #69
	bls	.L1104
	mov	ip, fp
	ldr	fp, [sp, #4]
.L1102:
	cmp	ip, r5
	bcc	.L1105
	b	.L1103
.L1115:
	ldr	fp, [sp, #4]
.L1103:
	ldr	r4, [sp, #8]
.L1105:
	cmp	r4, #0
	beq	.L1106
	ldr	r0, .L1116+12
	mov	r1, r4
	bl	printk
	uxtb	r0, r4
	bl	NandcSetDdrPara
.L1106:
	cmn	r6, #1
	bne	.L1098
	ldr	r0, .L1116+16
	mov	r1, fp
	ldr	r2, [sp, #0]
	bl	printk
	ldr	r3, [sp, #56]
	cmp	r3, #0
	moveq	r8, r6
	beq	.L1098
	ldr	r3, [sp, #12]
	ubfx	r0, r3, #8, #8
	bl	NandcSetDdrPara
.L1098:
	mov	r0, r8
	add	sp, sp, #20
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, sl, fp, pc}
.L1117:
	.align	2
.L1116:
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
	ldr	r5, .L1122
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
	beq	.L1119
	cmn	r8, #1
	bne	.L1120
.L1119:
	ldrb	r3, [r5, #3772]	@ zero_extendqisi2
	tst	r3, #1
	beq	.L1120
	mov	r0, #1
	bl	FlashSetInterfaceMode
	mov	r0, #1
	bl	NandcSetMode
	ldr	r3, .L1122
	mov	r2, #0
	strb	r2, [r3, #3780]
	b	.L1121
.L1120:
	mov	r3, #1
	strb	r3, [r5, #3780]
.L1121:
	mov	r0, #0
	ldmfd	sp!, {r2, r3, r4, r5, r6, r7, r8, pc}
.L1123:
	.align	2
.L1122:
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
	bne	.L1125
	ldr	r7, .L1130
	ldrb	r9, [r7, #860]	@ zero_extendqisi2
	cmp	r9, #0
	beq	.L1126
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
	bne	.L1125
.L1126:
	ldr	r7, .L1130
	ldrb	r3, [r7, #3780]	@ zero_extendqisi2
	cmp	r3, #0
	beq	.L1125
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
	beq	.L1127
	ldrb	r3, [r7, #3836]	@ zero_extendqisi2
	cmp	r0, r3, lsr #1
	bls	.L1125
.L1127:
	ubfx	r0, r9, #8, #8
	bl	NandcSetDdrPara
.L1125:
	ldr	r7, .L1130+4
	ldr	ip, [r7, #172]
	adds	r3, ip, #0
	movne	r3, #1
	cmn	r4, #1
	movne	r3, #0
	cmp	r3, #0
	beq	.L1128
	mov	r1, r6
	mov	r2, r8
	mov	r3, sl
	mov	r0, r5
	blx	ip
	mov	r2, r5
	mov	r3, r6
	mov	r4, r0
	ldr	r0, .L1130+8
	mov	r1, r4
	bl	printk
	cmn	r4, #1
	bne	.L1128
	ldr	r3, .L1130
	ldrb	r3, [r3, #928]	@ zero_extendqisi2
	cmp	r3, #0
	beq	.L1128
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
.L1128:
	mov	r0, r4
	ldmfd	sp!, {r2, r3, r4, r5, r6, r7, r8, r9, sl, pc}
.L1131:
	.align	2
.L1130:
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
	ldr	r3, .L1145
	stmfd	sp!, {r4, r5, r6, r7, r8, r9, sl, fp, lr}
	.save {r4, r5, r6, r7, r8, r9, sl, fp, lr}
	mov	r8, r1
	.pad #28
	sub	sp, sp, #28
	mov	fp, r2
	ldrb	sl, [r3, #2901]	@ zero_extendqisi2
	mov	r4, r0
	mov	r7, #0
	ldr	r6, .L1145+4
	b	.L1133
.L1142:
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
	bcs	.L1135
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
	beq	.L1140
	ldr	r2, [r3, #8]
	cmn	r2, #1
	bne	.L1140
	ldr	r3, [r3, #0]
	cmn	r3, #1
	strne	r2, [r4, #0]
.L1140:
	ldr	r3, [r4, #0]
	cmn	r3, #1
	bne	.L1135
	ldr	r1, [r4, #4]
	ldr	r0, .L1145+8
	ldrb	r2, [r6, #3836]	@ zero_extendqisi2
	bl	printk
	ldr	r1, [r4, #8]
	cmp	r1, #0
	beq	.L1141
	ldr	r0, .L1145+12
	mov	r2, #4
	mov	r3, #8
	bl	rknand_print_hex
.L1141:
	ldr	r1, [r4, #12]
	cmp	r1, #0
	beq	.L1135
	mov	r2, #4
	ldr	r0, .L1145+16
	mov	r3, r2
	bl	rknand_print_hex
.L1135:
	add	r7, r7, #1
	add	r4, r4, #36
.L1133:
	cmp	r7, r8
	bne	.L1142
	mov	r0, #0
	add	sp, sp, #28
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, sl, fp, pc}
.L1146:
	.align	2
.L1145:
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
	ldr	r3, .L1189
	stmfd	sp!, {r4, r5, r6, r7, r8, r9, sl, fp, lr}
	.save {r4, r5, r6, r7, r8, r9, sl, fp, lr}
	.pad #52
	sub	sp, sp, #52
	ldrb	r3, [r3, #2901]	@ zero_extendqisi2
	mov	r9, r0
	str	r1, [sp, #28]
	str	r2, [sp, #32]
	str	r3, [sp, #24]
	ldr	r3, .L1189+4
	ldrb	r8, [r3, #852]	@ zero_extendqisi2
	ldrb	ip, [r3, #860]	@ zero_extendqisi2
	cmp	r8, #0
	str	ip, [sp, #36]
	moveq	sl, r8
	beq	.L1148
	bl	FlashReadSlc2KPages
	b	.L1149
.L1176:
	mov	r3, #36
	ldr	ip, [sp, #28]
	mul	r3, r3, sl
	add	r2, sp, #44
	ldr	r1, [sp, #32]
	ldr	fp, .L1189+4
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
	bcs	.L1151
	add	r3, fp, r3
	ldrb	r4, [r3, #3764]	@ zero_extendqisi2
	ldr	r3, .L1189+8
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
	bhi	.L1153
	add	r1, fp, r4
	cmp	r2, #7
	add	fp, fp, r4
	ldr	r2, .L1189+4
	ldrb	r3, [r1, #12]	@ zero_extendqisi2
	ldreqb	r3, [r1, #20]	@ zero_extendqisi2
	ldrb	r1, [fp, #3756]	@ zero_extendqisi2
	cmp	r1, r3
	beq	.L1153
	ldrb	r1, [r2, #1]	@ zero_extendqisi2
	mov	r0, r4
	add	r2, r2, #4
	bl	HynixSetRRPara
.L1153:
	mov	r0, r4
	mov	r5, r5, lsr #31
	bl	NandcFlashCs
	ldr	ip, [sp, #32]
	mov	r0, r4
	cmp	ip, #1
	orreq	r5, r5, #1
	str	r5, [sp, #20]
	cmp	r5, #0
	beq	.L1155
	ldr	r3, .L1189+4
	ldrb	r3, [r3, #928]	@ zero_extendqisi2
	cmp	r3, #0
	beq	.L1155
	bl	flash_enter_slc_mode
	b	.L1156
.L1155:
	bl	flash_exit_slc_mode
.L1156:
	ldr	fp, .L1189+4
.L1188:
	ldr	r1, [sp, #44]
	cmn	r1, #1
	cmpeq	r4, #255
	moveq	r3, #0
	movne	r3, #1
	moveq	r7, r3
	beq	.L1158
	cmp	r7, #0
	beq	.L1159
	ldr	r2, [fp, #856]
	mov	r0, r4
	add	r2, r1, r2
	bl	FlashReadDpCmd
	b	.L1160
.L1159:
	mov	r0, r4
	bl	FlashReadCmd
.L1160:
	mov	r0, r4
	bl	NandcWaitFlashReady
	cmp	r7, #0
	beq	.L1158
	mov	r0, r4
	ldr	r1, [sp, #44]
	bl	FlashReadDpDataOutCmd
.L1158:
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
	bne	.L1188
.L1161:
	cmp	r7, #0
	mov	r5, r0
	beq	.L1162
	ldr	r3, .L1189+4
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
.L1162:
	mov	r0, r4
	ldr	fp, .L1189+4
	bl	NandcFlashDeCs
	ldr	ip, [sp, #36]
	cmn	r5, #1
	strb	ip, [fp, #860]
	bne	.L1166
	ldrb	r3, [fp, #3780]	@ zero_extendqisi2
	cmp	r3, #0
	beq	.L1164
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
	beq	.L1165
	ldrb	r3, [fp, #3836]	@ zero_extendqisi2
	cmp	r0, r3, lsr #1
	bls	.L1182
.L1165:
	ubfx	r0, r7, #8, #8
	bl	NandcSetDdrPara
	cmn	r5, #1
	bne	.L1182
.L1164:
	ldr	r3, .L1189+8
	mov	r0, r4
	ldr	r1, [sp, #44]
	ldr	r2, [r6, #8]
	ldr	r7, [r3, #172]
	ldr	r3, [r6, #12]
	cmp	r7, #0
	beq	.L1167
	blx	r7
	cmn	r0, #1
	mov	r5, r0
	bne	.L1184
	ldr	r2, .L1189+4
	ldr	r3, [r2, #3624]
	ldrb	r3, [r3, #19]	@ zero_extendqisi2
	sub	r3, r3, #1
	uxtb	r3, r3
	cmp	r3, #6
	bhi	.L1169
	ldrb	r1, [r2, #1]	@ zero_extendqisi2
	mov	r0, r4
	add	r2, r2, #4
	mov	r3, #0
	bl	HynixSetRRPara
.L1169:
	ldr	r1, [sp, #44]
	mov	r0, r4
	ldr	r2, [r6, #8]
	ldr	r3, [r6, #12]
	bl	FlashReadRawPage
	ldr	r7, .L1189+4
	ldr	r1, [r6, #4]
	ldrb	r2, [r7, #3836]	@ zero_extendqisi2
	mov	r5, r0
	ldr	r0, .L1189+12
	mov	r3, r5
	bl	printk
	cmn	r5, #1
	bne	.L1184
	ldrb	r7, [r7, #928]	@ zero_extendqisi2
	cmp	r7, #0
	beq	.L1168
	ldr	ip, [sp, #20]
	mov	r0, r4
	cmp	ip, #0
	beq	.L1170
	bl	flash_enter_slc_mode
	b	.L1171
.L1170:
	bl	flash_exit_slc_mode
.L1171:
	ldr	r3, .L1189+8
	mov	r0, r4
	ldr	r1, [sp, #44]
	ldr	r2, [r6, #8]
	ldr	ip, [r3, #172]
	ldr	r3, [r6, #12]
	blx	ip
	mov	r5, r0
	b	.L1184
.L1167:
	bl	FlashReadRawPage
	mov	r5, r0
	b	.L1168
.L1182:
	mov	r7, #0
.L1166:
	ldr	r3, .L1189+4
	ldrb	r3, [r3, #3836]	@ zero_extendqisi2
	add	r3, r3, r3, asl #1
	cmp	r5, r3, lsr #2
	bls	.L1168
	ldr	r3, .L1189+8
	ldr	r3, [r3, #172]
	cmp	r3, #0
	moveq	r5, #256
	b	.L1168
.L1184:
	mov	r7, #0
.L1168:
	ldr	ip, [sp, #16]
	cmp	r5, #256
	cmnne	r5, #1
	movne	r2, #0
	str	r5, [r9, ip]
	strne	r2, [r9, ip]
	ldr	ip, [sp, #16]
	ldr	r3, [r9, ip]
	cmn	r3, #1
	bne	.L1173
	ldr	r2, .L1189+4
	ldr	r1, [r6, #4]
	ldr	r0, .L1189+16
	ldrb	r2, [r2, #3836]	@ zero_extendqisi2
	bl	printk
	ldr	r1, [r6, #12]
	cmp	r1, #0
	beq	.L1173
	mov	r2, #4
	ldr	r0, .L1189+20
	mov	r3, r2
	bl	rknand_print_hex
.L1173:
	cmp	r7, #0
	beq	.L1174
	ldr	r3, .L1189+4
	ldrb	r3, [r3, #3836]	@ zero_extendqisi2
	add	r3, r3, r3, asl #1
	cmp	r8, r3, lsr #2
	bls	.L1175
	ldr	r3, .L1189+8
	ldr	r3, [r3, #172]
	cmp	r3, #0
	moveq	r8, #256
.L1175:
	add	r3, sl, #1
	mov	r2, #36
	cmp	r8, #256
	cmnne	r8, #1
	mul	r3, r2, r3
	movne	r2, #0
	str	r8, [r9, r3]
	strne	r2, [r9, r3]
.L1174:
	ldr	ip, [sp, #20]
	add	sl, sl, r7
	cmp	ip, #0
	beq	.L1151
	ldr	r3, .L1189+4
	ldrb	r3, [r3, #928]	@ zero_extendqisi2
	cmp	r3, #0
	beq	.L1151
	mov	r0, r4
	bl	flash_exit_slc_mode
.L1151:
	add	sl, sl, #1
.L1148:
	ldr	ip, [sp, #28]
	cmp	sl, ip
	bcc	.L1176
	mov	r0, #0
.L1149:
	add	sp, sp, #52
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, sl, fp, pc}
.L1190:
	.align	2
.L1189:
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
	ldr	r2, .L1216
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
	beq	.L1212
	cmp	r4, #0
	bne	.L1192
	b	.L1193
.L1212:
	mov	r4, #0
.L1192:
	ldr	r2, .L1216+4
	movw	r3, #3916
	ldr	fp, [sp, #20]
	ldrh	r3, [r2, r3]
	cmp	fp, r3
	bne	.L1194
.L1193:
	bl	FtlGcPageVarInit
.L1194:
	mov	r7, #0
	movw	r8, #65535
	mov	sl, r7
	mvn	r3, #0
	str	r3, [sp, #16]
.L1207:
	ldrh	r3, [r5, #0]
	strb	sl, [r5, #8]
	cmp	r3, r8
	beq	.L1213
.L1196:
.L1214:
	ldr	r3, .L1216+4
	movw	r2, #3848
	ldrh	r9, [r3, r2]
	ldr	r2, .L1216+8
	ldr	lr, [r2, #-972]
	ldr	ip, [r2, #-1168]
	movw	r2, #3928
	ldrh	r2, [r3, r2]
	mov	r3, #0
	mov	r6, r3
	str	r2, [sp, #12]
	mov	r2, r5
	b	.L1197
.L1199:
	ldrh	r0, [r2, #16]
	cmp	r0, r8
	beq	.L1198
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
.L1198:
	add	r3, r3, #1
	add	r2, r2, #2
	uxth	r3, r3
.L1197:
	cmp	r3, r9
	bne	.L1199
	ldr	r9, .L1216+8
	mov	r1, r6
	mov	r2, #0
	ldr	r0, [r9, #-972]
	bl	FlashReadPages
	mov	r3, #36
	mul	r3, r3, r6
	mov	r6, #0
	str	r3, [sp, #12]
	b	.L1200
.L1208:
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
	bne	.L1201
	ldr	r3, [r3, #12]
	add	r6, r6, #36
	ldrh	r1, [r3, #0]
	cmp	r1, r8
	ldreq	r3, .L1216+8
	moveq	r1, #1
	streq	r1, [r3, #-1012]
	beq	.L1195
.L1202:
	ldr	r0, [r3, #12]
	mov	r1, fp
	ldr	r2, [r3, #8]
	bl	FtlGcUpdatePage
	b	.L1200
.L1201:
	mov	r2, fp
	ldr	r0, .L1216+12
	ldrh	r1, [r5, #0]
	bl	printk
	ldr	r3, .L1216+4
	ldr	r2, [r3, #3840]
	cmp	r2, #0
	bne	.L1203
	ldrb	r2, [r3, #928]	@ zero_extendqisi2
	cmp	r2, #0
	beq	.L1204
.L1203:
	ldrh	r1, [r5, #0]
	ldr	r2, [r3, #4048]
	mov	r3, r1, asl #1
	ldrh	r3, [r2, r3]
	cmp	r3, #119
	bls	.L1205
.L1204:
	ldr	r3, .L1216+8
	ldr	r3, [r3, #-972]
	ldr	r3, [r3, r6]
	cmn	r3, #1
	bne	.L1206
.L1205:
	ldr	r3, .L1216+8
	ldr	r3, [r3, #-972]
	add	r6, r3, r6
	ldr	r6, [r6, #4]
	str	r6, [sp, #16]
.L1206:
	ldr	r2, .L1216+8
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
	b	.L1207
.L1200:
	ldr	r3, [sp, #12]
	cmp	r6, r3
	bne	.L1208
	ldr	fp, [sp, #20]
	add	r7, r7, #1
	add	r4, r4, #1
	cmp	r7, fp
	uxth	r4, r4
	bcc	.L1209
	ldr	r2, .L1216
	movw	r3, #3000
	ldrh	r1, [r2, r3]
	cmp	r1, r8
	beq	.L1209
	add	r1, r1, r7
	strh	r1, [r2, r3]	@ movhi
	ldr	r2, .L1216+4
	add	r3, r3, #916
	ldrh	r3, [r2, r3]
	cmp	r3, r4
	bhi	.L1210
.L1209:
	ldr	r2, .L1216+4
	movw	r3, #3916
	ldrh	r3, [r2, r3]
	cmp	r3, r4
	bhi	.L1214
.L1213:
	mov	r2, #0
.L1195:
	ldr	r1, .L1216
	movw	r3, #3000
	mvn	r0, #0
	strh	r4, [r5, #2]	@ movhi
	strb	r2, [r5, #6]
	strh	r0, [r1, r3]	@ movhi
	mov	r0, r5
	mov	r1, r4
	bl	ftl_sb_update_avl_pages
.L1210:
	ldr	r0, [sp, #16]
	add	sp, sp, #28
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, sl, fp, pc}
.L1217:
	.align	2
.L1216:
	.word	.LANCHOR1
	.word	.LANCHOR0
	.word	.LANCHOR2
	.word	.LC100
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
	ldr	r0, .L1228
	mov	r5, #0
	ldr	r1, .L1228+4
	bl	printk
	ldr	r7, .L1228+8
	b	.L1219
.L1227:
	add	r4, sp, #80
	mov	r0, r5
	ldr	r6, .L1228+12
	mov	r9, #36
	strh	r5, [r4, #-48]!	@ movhi
	bl	ftl_get_blk_mode
	mov	r2, r5, asl #1
	ldr	ip, [r6, #-2016]
	mov	r1, r5
	ldrh	r2, [ip, r2]
	mov	r3, r0
	ldr	r0, .L1228+16
	bl	printk
	mov	r0, r4
	bl	make_superblock
	movw	r3, #3848
	ldrh	sl, [r7, r3]
	add	r3, r3, #78
	add	r0, r4, #14
	mov	r4, #0
	ldrh	r3, [r7, r3]
	ldr	r8, [r6, #-972]
	ldr	lr, [r6, #-1172]
	str	r3, [sp, #28]
	movw	r3, #3928
	ldrh	ip, [r7, r3]
	mov	r3, r4
	ldr	r6, [r6, #-1168]
	b	.L1220
.L1222:
	ldrh	r1, [r0, #2]!
	movw	r2, #65535
	cmp	r1, r2
	beq	.L1221
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
.L1221:
	add	r3, r3, #1
	uxth	r3, r3
.L1220:
	cmp	r3, sl
	bne	.L1222
	ldr	sl, .L1228+12
	mov	r1, r4
	mov	r2, #0
	mov	r6, #0
	mov	r8, r6
	ldr	r0, [sl, #-972]
	bl	FlashReadPages
	b	.L1223
.L1224:
	ldr	r2, [sl, #-972]
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
	ldr	r0, .L1228+20
	ldr	r2, [r2, r6]
	add	r6, r6, #36
	ldr	r3, [r3, #4]
	bl	printk
.L1223:
	cmp	r8, r4
	ldr	r9, .L1228+12
	bne	.L1224
	ldr	r0, [r9, #-972]
	mov	r1, r4
	mov	r2, #1
	mov	r6, #0
	bl	FlashReadPages
	mov	r8, r6
	mov	sl, r9
	b	.L1225
.L1226:
	ldr	r2, [sl, #-972]
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
	ldr	r0, .L1228+24
	ldr	r2, [r2, r6]
	add	r6, r6, #36
	ldr	r3, [r3, #4]
	bl	printk
.L1225:
	cmp	r8, r4
	bne	.L1226
	add	r5, r5, #1
	uxth	r5, r5
.L1219:
	movw	r3, #3858
	ldrh	r3, [r7, r3]
	cmp	r3, r5
	bhi	.L1227
	mov	r0, #0
	add	sp, sp, #84
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, sl, fp, pc}
.L1229:
	.align	2
.L1228:
	.word	.LC101
	.word	.LANCHOR3+15
	.word	.LANCHOR0
	.word	.LANCHOR2
	.word	.LC102
	.word	.LC103
	.word	.LC104
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
	ldr	r5, .L1241
	ldr	r1, .L1241+4
	movw	r6, #3916
	ldrh	r9, [r5, r6]
	mov	r4, r0
	ldr	r0, .L1241+8
	bl	printk
	ldr	r3, .L1241+12
	mov	r1, r4
	ldr	r0, .L1241+16
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
	beq	.L1231
	mov	r0, r4
	bl	ftl_get_blk_mode
	cmp	r0, #1
	mov	r7, r0
	movweq	r3, #3918
	ldreqh	r9, [r5, r3]
.L1231:
	ldrh	r3, [r5, r6]
	mov	r1, r7
	ldr	r0, .L1241+20
	mov	r2, r9
	bl	printk
	mov	r6, #0
.L1232:
	ldr	r3, .L1241
	movw	r2, #3848
	movw	r1, #3926
	mov	r4, #0
	add	r0, sp, #46
	ldrh	sl, [r3, r2]
	ldr	r2, .L1241+12
	ldrh	lr, [r3, r1]
	ldr	r8, [r2, #-972]
	ldr	r5, [r2, #-1172]
	ldr	ip, [r2, #-1168]
	movw	r2, #3928
	ldrh	r2, [r3, r2]
	mov	r3, r4
	str	r2, [sp, #28]
	b	.L1233
.L1235:
	ldrh	r1, [r0, #2]!
	movw	r2, #65535
	cmp	r1, r2
	beq	.L1234
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
.L1234:
	add	r3, r3, #1
	uxth	r3, r3
.L1233:
	cmp	r3, sl
	bne	.L1235
	ldr	r8, .L1241+12
	mov	r1, r4
	mov	r2, r7
	mov	r5, #0
	mov	sl, r5
	ldr	r0, [r8, #-972]
	bl	FlashReadPages
	b	.L1236
.L1237:
	ldr	r2, [r8, #-972]
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
	ldr	r0, .L1241+24
	ldr	r2, [r2, r5]
	add	r5, r5, #36
	ldr	r3, [r3, #4]
	bl	printk
.L1236:
	cmp	sl, r4
	bne	.L1237
	add	r6, r6, #1
	uxth	r6, r6
	cmp	r6, r9
	bne	.L1232
.L1238:
	mov	r0, #0
	add	sp, sp, #84
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, sl, fp, pc}
.L1242:
	.align	2
.L1241:
	.word	.LANCHOR0
	.word	.LANCHOR3+31
	.word	.LC101
	.word	.LANCHOR2
	.word	.LC105
	.word	.LC106
	.word	.LC107
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
	ldr	r5, .L1301
	.pad #28
	sub	sp, sp, #28
	ldr	r4, .L1301+4
	mov	r1, r6
	ldr	r3, .L1301+8
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
	ldr	r0, .L1301+12
	mov	r1, #255
	mov	r2, #12
	bl	ftl_memset
	mov	r3, #3856
	ldrh	r3, [r5, r3]
	str	r3, [sp, #8]
	b	.L1244
.L1246:
	add	r3, r6, r5
	ldr	r1, [sp, #8]
	ldrb	r0, [r3, #3874]	@ zero_extendqisi2
	bl	V2P_block
	str	r0, [sp, #0]
	bl	FtlBbmIsBadBlock
	ldr	r3, [sp, #0]
	cmp	r0, #0
	bne	.L1245
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
.L1245:
	add	r5, r5, #1
	uxth	r5, r5
	b	.L1279
.L1292:
	mov	r7, #0
	movw	fp, #3848
	mov	r5, r7
	mov	r9, #36
	movw	r8, #3928
.L1279:
	ldrh	r3, [r6, fp]
	cmp	r3, r5
	bhi	.L1246
	cmp	r7, #0
	beq	.L1247
	ldr	r0, [r4, #-972]
	mov	r1, r7
	mov	r2, #1
	mov	fp, r7
	bl	FlashReadPages
	mov	r8, #0
	str	r8, [sp, #12]
.L1278:
	ldr	r3, [r4, #-972]
	add	r2, r3, r8
	ldr	r3, [r3, r8]
	ldr	r6, [r2, #4]
	cmn	r3, #1
	ldr	r5, [r2, #12]
	ubfx	r6, r6, #10, #16
	bne	.L1248
	mov	r7, #16
.L1250:
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
	beq	.L1248
.L1249:
	ldr	r3, [r4, #-972]
	ldr	r3, [r3, r8]
	cmn	r3, #1
	bne	.L1248
	sub	r7, r7, #1
	uxth	r7, r7
	cmp	r7, #0
	bne	.L1250
.L1248:
	ldr	r3, [r4, #-972]
	ldr	r3, [r3, r8]
	cmn	r3, #1
	beq	.L1251
	ldr	r2, [r4, #-1804]
	ldr	r3, [r5, #4]
	cmn	r2, #1
	beq	.L1252
	cmp	r2, r3
	bhi	.L1253
.L1252:
	cmn	r3, #1
	addne	r3, r3, #1
	strne	r3, [sl, #-1804]
.L1253:
	ldrh	r3, [r5, #0]
	movw	r2, #61604
	cmp	r3, r2
	beq	.L1256
	bhi	.L1259
	movw	r2, #61574
	cmp	r3, r2
	bne	.L1254
	b	.L1299
.L1259:
	movw	r2, #61634
	cmp	r3, r2
	beq	.L1257
	movw	r2, #65535
	cmp	r3, r2
	bne	.L1254
	b	.L1300
.L1257:
	ldr	r3, .L1301
	mov	r9, r6
	ldr	r1, .L1301+8
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
	b	.L1260
.L1266:
	mov	r6, r2, asl #2
	ldr	r7, [r5, #4]
	str	r6, [sp, #16]
	ldr	r6, [ip, r2, asl #2]
	cmp	r7, r6
	bls	.L1261
	ldr	ip, [ip, #0]
	mov	r6, r9
	cmp	ip, #0
	bne	.L1262
	cmp	r1, r0
	addne	r1, r1, #1
	ldrne	r0, .L1301+8
	strneh	r1, [r4, r0]	@ movhi
.L1262:
	mov	r0, #0
	sxth	r9, r3
	str	r3, [sp, #4]
	b	.L1263
.L1264:
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
.L1263:
	sxth	r1, r0
	cmp	r1, r9
	bne	.L1264
	ldr	r1, [sl, #-1036]
	mov	r2, r2, asl #1
	ldr	r0, [r5, #4]
	ldr	ip, [sp, #16]
	ldr	r3, [sp, #4]
	str	r0, [r1, ip]
	ldr	r1, [sl, #-1040]
	strh	r6, [r1, r2]	@ movhi
	b	.L1265
.L1261:
	sub	r3, r3, #1
	uxth	r3, r3
.L1260:
	ldr	r6, [sp, #4]
	sxth	r2, r3
	cmp	r2, r6
	bgt	.L1266
	mov	r6, r9
.L1265:
	sxth	r3, r3
	cmp	r3, #0
	blt	.L1254
	ldr	r2, .L1301+8
	ldr	r0, .L1301
	ldrh	r1, [r4, r2]
	ldr	r0, [r0, #3944]
	rsb	r0, r1, r0
	sub	r0, r0, #1
	sxth	r0, r0
	cmp	r3, r0
	bgt	.L1254
	add	r1, r1, #1
	strh	r1, [r4, r2]	@ movhi
	ldr	r2, [r4, #-1036]
	ldr	r1, [r5, #4]
	str	r1, [r2, r3, asl #2]
	mov	r3, r3, asl #1
	ldr	r2, [r4, #-1040]
	b	.L1296
.L1299:
	ldr	r1, .L1301
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
	b	.L1267
.L1273:
	mov	r6, r2, asl #2
	ldr	r7, [r5, #4]
	str	r6, [sp, #16]
	ldr	r6, [ip, r2, asl #2]
	cmp	r7, r6
	bls	.L1268
	ldr	ip, [ip, #0]
	mov	r6, r9
	cmp	ip, #0
	bne	.L1269
	cmp	r1, r0
	addne	r1, r1, #1
	ldrne	ip, .L1301
	movwne	r0, #3962
	strneh	r1, [ip, r0]	@ movhi
.L1269:
	sxth	ip, r3
	mov	r0, #0
	str	ip, [sp, #20]
	ldr	r9, .L1301
	str	r3, [sp, #4]
	b	.L1270
.L1271:
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
.L1270:
	ldr	ip, [sp, #20]
	sxth	r1, r0
	cmp	r1, ip
	bne	.L1271
	ldr	r1, [sl, #-908]
	mov	r2, r2, asl #1
	ldr	r0, [r5, #4]
	ldr	ip, [sp, #16]
	ldr	r3, [sp, #4]
	str	r0, [r1, ip]
	ldr	r1, .L1301
	ldr	r1, [r1, #3964]
	strh	r6, [r1, r2]	@ movhi
	b	.L1272
.L1268:
	sub	r3, r3, #1
	uxth	r3, r3
.L1267:
	ldr	r6, [sp, #4]
	sxth	r2, r3
	cmp	r2, r6
	bgt	.L1273
	mov	r6, r9
.L1272:
	sxth	r3, r3
	cmp	r3, #0
	blt	.L1254
	ldr	r2, .L1301
	mov	ip, #3936
	movw	r1, #3962
	ldrh	ip, [r2, ip]
	ldrh	r0, [r2, r1]
	sub	ip, ip, #1
	rsb	ip, r0, ip
	sxth	ip, ip
	cmp	r3, ip
	bgt	.L1254
	add	r0, r0, #1
	strh	r0, [r2, r1]	@ movhi
	ldr	r0, [r5, #4]
	ldr	r1, [r4, #-908]
	str	r0, [r1, r3, asl #2]
	mov	r3, r3, asl #1
	ldr	r2, [r2, #3964]
.L1296:
	strh	r6, [r2, r3]	@ movhi
	b	.L1254
.L1256:
	ldr	r3, .L1301+16
	movw	ip, #65535
	ldrh	r2, [r4, r3]
	cmp	r2, ip
	streqh	r6, [r4, r3]	@ movhi
	beq	.L1297
	ldr	r3, .L1301+20
	movw	r1, #65535
	ldrh	r0, [r4, r3]
	cmp	r0, r1
	beq	.L1275
	mov	r1, #1
	bl	FtlFreeSysBlkQueueIn
.L1275:
	ldr	r3, [r5, #4]
	ldr	r2, [sl, #-1764]
	cmp	r2, r3
	ldr	r3, .L1301+20
	strcsh	r6, [r4, r3]	@ movhi
	bcs	.L1254
	ldr	r2, .L1301+16
	ldrh	r1, [r4, r2]
	strh	r6, [r4, r2]	@ movhi
	strh	r1, [r4, r3]	@ movhi
.L1297:
	ldr	r3, [r5, #4]
	str	r3, [r4, #-1764]
	b	.L1254
.L1300:
	mov	r0, r6
	b	.L1298
.L1251:
	ldr	r3, .L1301
	mov	r0, r6
	ldrb	r1, [r3, #928]	@ zero_extendqisi2
	cmp	r1, #0
	beq	.L1277
.L1298:
	mov	r1, #0
.L1277:
	bl	FtlFreeSysBlkQueueIn
.L1254:
	ldr	r6, [sp, #12]
	add	r8, r8, #36
	add	r3, r6, #1
	uxth	r3, r3
	str	r3, [sp, #12]
	cmp	r3, fp
	bne	.L1278
.L1247:
	ldr	ip, [sp, #8]
	add	r3, ip, #1
	uxth	r3, r3
	str	r3, [sp, #8]
.L1244:
	ldr	r6, .L1301
	movw	r3, #3858
	ldr	ip, [sp, #8]
	ldrh	r3, [r6, r3]
	cmp	r3, ip
	bhi	.L1292
	ldr	r1, .L1301+4
	ldr	r2, [r1, #-1040]
	ldrh	r3, [r2, #0]
	cmp	r3, #0
	bne	.L1280
	ldr	r0, .L1301+8
	ldrh	r1, [r1, r0]
	cmp	r1, #0
	ldrne	r0, [r6, #3944]
	bne	.L1281
	b	.L1280
.L1285:
	mov	r1, r1, asl #1
	ldrh	r1, [r2, r1]
	cmp	r1, #0
	beq	.L1282
	ldr	r7, .L1301
	sxth	r6, r3
	ldr	r1, .L1301+4
	mov	r5, #0
	b	.L1283
.L1284:
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
.L1283:
	ldr	r0, [r7, #3944]
	sxth	r2, r3
	cmp	r2, r0
	bcc	.L1284
	b	.L1280
.L1282:
	add	r3, r3, #1
	uxth	r3, r3
.L1281:
	sxth	r1, r3
	cmp	r1, r0
	bcc	.L1285
.L1280:
	ldr	r2, .L1301
	ldr	r1, [r2, #3964]
	ldrh	r3, [r1, #0]
	cmp	r3, #0
	bne	.L1286
	movw	r0, #3962
	ldrh	r0, [r2, r0]
	cmp	r0, #0
	movne	r0, #3936
	ldrneh	r2, [r2, r0]
	bne	.L1287
	b	.L1286
.L1291:
	mov	ip, r0, asl #1
	ldrh	ip, [r1, ip]
	cmp	ip, #0
	beq	.L1288
	ldr	r1, .L1301
	mov	r8, #3936
	ldr	r7, .L1301+4
	mov	r6, #0
	b	.L1289
.L1290:
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
.L1289:
	ldrh	ip, [r1, r8]
	sxth	r2, r3
	cmp	r2, ip
	blt	.L1290
	b	.L1286
.L1288:
	add	r3, r3, #1
	uxth	r3, r3
.L1287:
	sxth	r0, r3
	cmp	r0, r2
	blt	.L1291
.L1286:
	mov	r0, #0
	add	sp, sp, #28
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, sl, fp, pc}
.L1302:
	.align	2
.L1301:
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
	ldr	r3, .L1314
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
	bne	.L1307
	b	.L1312
.L1310:
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
	bne	.L1308
	ldr	r3, [sp, #44]
	cmn	r3, #1
	bne	.L1308
	ldr	r3, [sp, #4]
	cmn	r3, #1
	subne	r4, r6, #1
	uxthne	r4, r4
	bne	.L1312
.L1308:
	add	r6, r6, #1
	uxth	r8, r6
.L1312:
	sxth	r6, r8
	sxth	r3, r4
	cmp	r6, r3
	ble	.L1310
.L1307:
	sxth	r0, r4
	add	sp, sp, #108
	ldmfd	sp!, {r4, r5, r6, r7, r8, sl, pc}
.L1315:
	.align	2
.L1314:
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
	ldr	r4, .L1332
	mov	r1, r5
	ldr	r2, .L1332+4
	ldr	r7, .L1332+8
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
	beq	.L1330
	mov	r1, #1
	bl	FtlGetLastWrittenPage
	ldr	r3, .L1332+12
	add	r2, r0, #1
	mov	r6, r0
	strh	r2, [r4, r3]	@ movhi
	b	.L1318
.L1321:
	ldrh	r2, [r4, r7]
	mov	r1, #1
	ldr	r0, .L1332+16
	orr	r3, r3, r2, asl #10
	str	r3, [r4, #184]
	ldr	r3, [r4, #-960]
	mov	r2, r1
	str	r3, [r4, #188]
	bl	FlashReadPages
	ldr	r3, [r4, #180]
	cmn	r3, #1
	beq	.L1319
	ldr	r3, [r4, #-960]
	ldr	r2, [r3, #0]
	ldr	r3, .L1332+20
	cmp	r2, r3
	bne	.L1319
	ldr	r3, [r4, #-936]
	ldrh	r2, [r3, #0]
	movw	r3, #61604
	cmp	r2, r3
	beq	.L1320
.L1319:
	sub	r5, r5, #1
	uxth	r5, r5
.L1318:
	add	r3, r5, r6
	sxth	r3, r3
	cmp	r3, #0
	bge	.L1321
.L1320:
	ldr	r4, .L1332
	mov	r2, #48
	ldr	r5, .L1332+4
	mov	r6, #3856
	ldr	r0, .L1332+24
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
	beq	.L1322
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
.L1322:
	ldr	r4, .L1332
	ldr	r3, .L1332+20
	ldr	r2, [r4, #-2072]
	cmp	r2, r3
	bne	.L1330
	ldr	r3, .L1332+28
	movw	r1, #3870
	ldrb	r0, [r4, #-2062]	@ zero_extendqisi2
	ldrh	r2, [r4, r3]
	ldr	r3, .L1332+32
	strh	r2, [r4, r3]	@ movhi
	ldr	r3, .L1332+4
	ldrh	r1, [r3, r1]
	cmp	r0, r1
	bne	.L1330
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
	ldr	r3, .L1332+36
	ldr	r1, .L1332+40
	strh	r0, [r4, r3]	@ movhi
	ldr	r3, .L1332+44
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
	ldr	r0, .L1332+48
	strb	r3, [r4, #-1988]
	ldr	r3, .L1332+52
	strh	r1, [r4, r3]	@ movhi
	mov	r3, #0
	ldr	r1, .L1332+56
	strh	r3, [r4, r0]	@ movhi
	strb	r3, [r4, #-1754]
	ldrh	r0, [r4, r1]
	add	r1, r1, #106
	strb	r3, [r4, #-1752]
	str	r3, [r4, #-1816]
	strh	r0, [r4, r1]	@ movhi
	sub	r1, r1, #104
	ldr	r0, .L1332+60
	ldrh	r1, [r4, r1]
	mov	ip, r1, lsr #6
	and	r1, r1, #63
	strb	r1, [r4, #-1942]
	ldrb	r1, [r4, #-2060]	@ zero_extendqisi2
	strh	ip, [r4, r0]	@ movhi
	strb	r1, [r4, #-1940]
	ldr	r1, .L1332+64
	ldrh	r0, [r4, r1]
	add	r1, r1, #150
	strh	r0, [r4, r1]	@ movhi
	ldr	r1, .L1332+68
	ldr	r0, .L1332+72
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
	ldr	r4, .L1332
	ldr	r3, [r4, #-2036]
	ldr	r1, [r4, #-1800]
	cmp	r3, r1
	strhi	r3, [r4, #-1800]
	cmp	r2, r5
	beq	.L1325
	ldr	r0, .L1332+76
	bl	make_superblock
.L1325:
	ldr	r3, .L1332+80
	ldrh	r3, [r4, r3]
	cmp	r3, r5
	beq	.L1326
	ldr	r0, .L1332+84
	bl	make_superblock
.L1326:
	ldr	r5, .L1332
	movw	r4, #65535
	ldr	r3, .L1332+88
	ldrh	r3, [r5, r3]
	cmp	r3, r4
	beq	.L1327
	ldr	r0, .L1332+92
	bl	make_superblock
.L1327:
	ldr	r3, .L1332+52
	ldrh	r3, [r5, r3]
	cmp	r3, r4
	beq	.L1331
	ldr	r0, .L1332+96
	bl	make_superblock
	mov	r0, #0
	ldmfd	sp!, {r3, r4, r5, r6, r7, pc}
.L1330:
	mvn	r0, #0
	ldmfd	sp!, {r3, r4, r5, r6, r7, pc}
.L1331:
	mov	r0, #0
	ldmfd	sp!, {r3, r4, r5, r6, r7, pc}
.L1333:
	.align	2
.L1332:
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
	ldr	r6, .L1350
	ldr	r8, .L1350+4
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
	b	.L1335
.L1339:
	mov	r1, #1
	mov	r3, r5, asl #10
	ldr	r0, .L1350+8
	mov	r2, r1
	str	r3, [r6, #184]
	bl	FlashReadPages
	ldr	r3, [r6, #180]
	cmn	r3, #1
	bne	.L1336
	ldr	r3, [r6, #184]
	mov	r1, #1
	ldr	r0, .L1350+8
	mov	r2, r1
	add	r3, r3, #1
	str	r3, [r6, #184]
	bl	FlashReadPages
.L1336:
	ldr	r3, [r7, #180]
	cmn	r3, #1
	beq	.L1337
	ldrh	r2, [r4, #0]
	movw	r3, #61649
	cmp	r2, r3
	bne	.L1337
	ldr	r3, .L1350+4
	movw	r2, #3980
	strh	r5, [r3, r2]	@ movhi
	ldr	r2, [r4, #4]
	str	r2, [r3, #3988]
	mov	r2, #3984
	ldrh	r1, [r4, #8]
	strh	r1, [r3, r2]	@ movhi
	b	.L1338
.L1337:
	sub	r5, r5, #1
	uxth	r5, r5
.L1335:
	movw	r3, #3912
	ldrh	r3, [r8, r3]
	sub	r3, r3, #48
	cmp	r5, r3
	bgt	.L1339
.L1338:
	ldr	r5, .L1350+4
	movw	r8, #3980
	movw	r3, #65535
	ldrh	r2, [r5, r8]
	cmp	r2, r3
	beq	.L1349
	mov	r6, #3984
	ldrh	r2, [r5, r6]
	cmp	r2, r3
	beq	.L1341
	ldr	r7, .L1350
	mov	r1, #1
	mov	r2, r2, asl #10
	add	r0, r7, #180
	str	r2, [r7, #184]
	mov	r2, r1
	bl	FlashReadPages
	ldr	r3, [r7, #180]
	cmn	r3, #1
	beq	.L1341
	ldrh	r2, [r4, #0]
	movw	r3, #61649
	cmp	r2, r3
	bne	.L1341
	ldr	r3, [r4, #4]
	ldr	r2, [r5, #3988]
	cmp	r3, r2
	strhi	r3, [r5, #3988]
	ldrhih	r2, [r5, r6]
	ldrhih	r3, [r4, #8]
	strhih	r2, [r5, r8]	@ movhi
	strhih	r3, [r5, r6]	@ movhi
.L1341:
	ldr	r8, .L1350+4
	movw	r3, #3980
	mov	r1, #1
	mov	r6, #0
	ldr	r5, .L1350
	ldrh	r0, [r8, r3]
	bl	FtlGetLastWrittenPage
	movw	r3, #3982
	add	r2, r0, #1
	mov	r7, r0
	strh	r2, [r8, r3]	@ movhi
	b	.L1342
.L1345:
	movw	r2, #3980
	mov	r1, #1
	ldrh	r2, [r8, r2]
	ldr	r0, .L1350+8
	orr	r3, r3, r2, asl #10
	str	r3, [r5, #184]
	ldr	r3, [r5, #-960]
	mov	r2, r1
	str	r3, [r5, #188]
	bl	FlashReadPages
	ldr	r3, [r5, #180]
	cmn	r3, #1
	beq	.L1343
	ldrh	r2, [r4, #0]
	movw	r3, #61649
	cmp	r2, r3
	beq	.L1344
.L1343:
	sub	r6, r6, #1
	uxth	r6, r6
.L1342:
	add	r3, r6, r7
	sxth	r3, r3
	cmp	r3, #0
	bge	.L1345
.L1344:
	ldr	r3, .L1350+4
	movw	r2, #3986
	ldrh	r1, [r4, #10]
	ldrh	r0, [r4, #12]
	strh	r1, [r3, r2]	@ movhi
	movw	r2, #65535
	cmp	r0, r2
	beq	.L1346
	ldr	r2, [r3, #3844]
	cmp	r0, r2
	beq	.L1346
	movw	r1, #3858
	ldrh	r3, [r3, r1]
	mov	r3, r3, lsr #2
	cmp	r2, r3
	bcs	.L1346
	cmp	r0, r3
	bcs	.L1346
	bl	FtlSysBlkNumInit
.L1346:
	ldr	r6, .L1350+12
	mov	r4, #0
	ldr	r8, .L1350+4
	movw	r7, #3870
	ldr	r5, .L1350
	b	.L1347
.L1348:
	ldr	r3, .L1350+16
	ldr	r1, [r5, #188]
	ldr	r0, [r6, #4]!
	ldrh	r2, [r5, r3]
	mov	r2, r2, asl #2
	mla	r1, r4, r2, r1
	bl	memcpy
	add	r4, r4, #1
.L1347:
	ldrh	r3, [r8, r7]
	cmp	r4, r3
	bcc	.L1348
	mov	r0, #0
	ldmfd	sp!, {r4, r5, r6, r7, r8, pc}
.L1349:
	mvn	r0, #0
	ldmfd	sp!, {r4, r5, r6, r7, r8, pc}
.L1351:
	.align	2
.L1350:
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
	ldr	r3, .L1359
	movw	fp, #3870
	ldr	r7, .L1359+4
	ldr	r6, .L1359+8
	ldr	r2, [r3, #-960]
	ldr	r8, [r3, #-936]
	str	r2, [r3, #188]
	str	r8, [r3, #192]
	b	.L1353
.L1358:
	movw	r9, #3912
	mvn	r3, #0
	ldrh	r4, [r6, r9]
	strh	r3, [r7], #2	@ movhi
	add	r4, r4, r3
	ldr	sl, .L1359
	uxth	r4, r4
	b	.L1354
.L1357:
	mla	r3, r3, r5, r4
	mov	r1, #1
	ldr	r0, .L1359+12
	mov	r2, r1
	mov	r3, r3, asl #10
	str	r3, [sl, #184]
	bl	FlashReadPages
	ldr	r3, [sl, #180]
	cmn	r3, #1
	beq	.L1355
	ldrh	r2, [r8, #0]
	movw	r3, #61664
	cmp	r2, r3
	streqh	r4, [r7, #-2]	@ movhi
	beq	.L1356
.L1355:
	sub	r4, r4, #1
	uxth	r4, r4
.L1354:
	ldrh	r3, [r6, r9]
	sub	r2, r3, #16
	cmp	r4, r2
	bgt	.L1357
.L1356:
	add	r5, r5, #1
.L1353:
	ldrh	r3, [r6, fp]
	cmp	r5, r3
	bcc	.L1358
	mov	r0, #0
	ldmfd	sp!, {r3, r4, r5, r6, r7, r8, r9, sl, fp, pc}
.L1360:
	.align	2
.L1359:
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
	ldr	r3, .L1380
	mov	r6, r0
	mov	r8, #0
	ldr	r7, .L1380+4
	ldrb	fp, [r3, #2901]	@ zero_extendqisi2
	b	.L1362
.L1369:
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
	bcs	.L1364
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
.L1364:
	add	r8, r8, #1
	add	r6, r6, #36
.L1362:
	cmp	r8, sl
	bne	.L1369
	ldr	r3, [sp, #12]
	cmp	r3, #0
	movne	r6, #0
	ldrne	r5, .L1380+8
	bne	.L1370
	b	.L1371
.L1376:
	ldr	r3, [r4, #0]
	cmn	r3, #1
	bne	.L1372
	ldr	r1, [r4, #4]
	ldr	r0, .L1380+12
	bl	printk
	b	.L1373
.L1372:
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
	bne	.L1374
	ldr	r0, .L1380+16
	ldr	r1, [r4, #4]
	bl	printk
	str	r7, [r4, #0]
.L1374:
	ldr	r3, [r4, #12]
	cmp	r3, #0
	beq	.L1375
	ldr	r2, [r3, #0]
	ldr	r3, [r5, #224]
	ldr	r3, [r3, #0]
	cmp	r2, r3
	beq	.L1375
	ldr	r0, .L1380+20
	ldr	r1, [r4, #4]
	bl	printk
	mvn	r3, #0
	str	r3, [r4, #0]
.L1375:
	ldr	r3, [r4, #8]
	cmp	r3, #0
	beq	.L1373
	ldr	r2, [r3, #0]
	ldr	r3, [r5, #220]
	ldr	r3, [r3, #0]
	cmp	r2, r3
	beq	.L1373
	ldr	r0, .L1380+24
	ldr	r1, [r4, #4]
	bl	printk
	mvn	r3, #0
	str	r3, [r4, #0]
.L1373:
	add	r6, r6, #1
	add	r4, r4, #36
.L1370:
	cmp	r6, sl
	bne	.L1376
.L1371:
	mov	r0, #0
	add	sp, sp, #68
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, sl, fp, pc}
.L1381:
	.align	2
.L1380:
	.word	.LANCHOR1
	.word	.LANCHOR0
	.word	.LANCHOR2
	.word	.LC108
	.word	.LC109
	.word	.LC110
	.word	.LC111
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
	ldr	r5, .L1410
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
	ldr	ip, .L1410+4
	ldrb	ip, [ip, #2901]	@ zero_extendqisi2
	str	ip, [sp, #12]
	beq	.L1383
	bl	FlashProgSlc2KPages
	b	.L1384
.L1397:
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
	bcs	.L1386
	ldrb	r1, [r5, #3837]	@ zero_extendqisi2
	add	r2, fp, r2, asl #4
	cmp	r1, #0
	ldr	r2, [r2, #3636]
	moveq	r8, #0
	cmp	r2, #0
	beq	.L1388
	cmp	r3, #1
	bne	.L1389
	ldr	r0, [r5, #3012]
	bl	NandcIqrWaitFlashReady
.L1389:
	ldrb	r0, [sp, #32]	@ zero_extendqisi2
	bl	FlashWaitCmdDone
.L1388:
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
	bne	.L1391
	bl	NandcWaitFlashReady
	b	.L1392
.L1391:
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
.L1392:
	ldr	r2, [sp, #20]
	sub	r3, r2, #1
	cmp	r3, #6
	bhi	.L1393
	add	r3, r5, r4
	ldrb	r3, [r3, #3756]	@ zero_extendqisi2
	cmp	r3, #0
	beq	.L1393
	mov	r0, r4
	ldrb	r1, [r5, #1]	@ zero_extendqisi2
	ldr	r2, .L1410+8
	mov	r3, #0
	bl	HynixSetRRPara
.L1393:
	mov	r0, r4
	bl	NandcFlashCs
	cmp	sl, #1
	mov	r0, r4
	bne	.L1394
	ldrb	r3, [r5, #928]	@ zero_extendqisi2
	cmp	r3, #0
	beq	.L1394
	bl	flash_enter_slc_mode
	b	.L1395
.L1394:
	bl	flash_exit_slc_mode
.L1395:
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
	beq	.L1396
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
.L1396:
	mov	r0, r4
	ldr	r1, [sp, #28]
	bl	FlashProgSecondCmd
	mov	r0, r4
	bl	NandcFlashDeCs
	add	r6, r6, r8
.L1386:
	add	r6, r6, #1
.L1383:
	ldr	r3, [sp, #8]
	cmp	r6, r3
	bcc	.L1397
	ldr	r5, .L1410
	mov	r4, #0
	ldr	r6, .L1410+12
	ldr	r0, [r5, #3012]
	bl	NandcIqrWaitFlashReady
	b	.L1398
.L1400:
	uxtb	r0, r4
	bl	FlashWaitCmdDone
	cmp	sl, #1
	bne	.L1399
	ldrb	r3, [r5, #928]	@ zero_extendqisi2
	cmp	r3, #0
	beq	.L1399
	ldrb	r0, [r6, r4, asl #4]	@ zero_extendqisi2
	bl	flash_exit_slc_mode
.L1399:
	add	r4, r4, #1
.L1398:
	ldrb	r3, [r5, #3762]	@ zero_extendqisi2
	cmp	r4, r3
	bcc	.L1400
	ldr	r2, [sp, #16]
	cmp	r2, #0
	ldreq	r0, [sp, #16]
	beq	.L1384
	mov	r5, #0
	ldr	r4, .L1410+16
	ldr	r6, [sp, #8]
	b	.L1401
.L1406:
	ldr	r3, [r9, #0]
	cmn	r3, #1
	bne	.L1402
	ldr	r1, [r9, #4]
	ldr	r0, .L1410+20
	bl	printk
	b	.L1403
.L1402:
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
	bne	.L1404
	ldr	r0, .L1410+24
	ldr	r1, [r9, #4]
	bl	printk
	str	r7, [r9, #0]
.L1404:
	ldr	r3, [r9, #12]
	cmp	r3, #0
	beq	.L1405
	ldr	r2, [r3, #0]
	ldr	r3, [r4, #224]
	ldr	r3, [r3, #0]
	cmp	r2, r3
	beq	.L1405
	ldr	r0, .L1410+28
	ldr	r1, [r9, #4]
	bl	printk
	mvn	r3, #0
	str	r3, [r9, #0]
.L1405:
	ldr	r3, [r9, #8]
	cmp	r3, #0
	beq	.L1403
	ldr	r2, [r3, #0]
	ldr	r3, [r4, #220]
	ldr	r3, [r3, #0]
	cmp	r2, r3
	beq	.L1403
	ldr	r0, .L1410+32
	ldr	r1, [r9, #4]
	bl	printk
	mvn	r3, #0
	str	r3, [r9, #0]
.L1403:
	add	r5, r5, #1
	add	r9, r9, #36
.L1401:
	cmp	r5, r6
	bne	.L1406
	mov	r0, #0
.L1384:
	add	sp, sp, #76
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, sl, fp, pc}
.L1411:
	.align	2
.L1410:
	.word	.LANCHOR0
	.word	.LANCHOR1
	.word	.LANCHOR0+4
	.word	.LANCHOR0+3628
	.word	.LANCHOR2
	.word	.LC108
	.word	.LC109
	.word	.LC110
	.word	.LC111
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
	ldr	r4, .L1424
	mov	r7, #3856
	ldr	r6, .L1424+4
	ldr	r3, [r4, #-960]
	ldr	r5, [r4, #-936]
	str	r3, [r4, #188]
	ldr	r3, .L1424+8
	str	r5, [r4, #192]
	ldrh	r3, [r4, r3]
	strh	r3, [r5, #2]	@ movhi
	ldr	r3, .L1424+12
	strh	r3, [r5, #0]	@ movhi
	ldr	r3, [r4, #-1764]
	str	r3, [r5, #4]
	mov	r3, #0
	str	r3, [r5, #8]
	str	r3, [r5, #12]
	ldr	r3, .L1424+16
	str	r3, [r4, #-2072]
	ldr	r3, .L1424+20
	str	r3, [r4, #-2068]
	ldr	r3, .L1424+24
	ldrh	r2, [r4, r3]
	ldr	r3, .L1424+28
	strh	r2, [r4, r3]	@ movhi
	movw	r3, #3870
	ldrh	r3, [r6, r3]
	strb	r3, [r4, #-2062]
	ldr	r3, .L1424+32
	ldrh	r2, [r4, r3]
	sub	r3, r3, #62
	strh	r2, [r4, r3]	@ movhi
	add	r3, r3, #64
	ldrh	r2, [r4, r3]
	ldrb	r3, [r4, #-1990]	@ zero_extendqisi2
	orr	r2, r3, r2, asl #6
	ldr	r3, .L1424+36
	strh	r2, [r4, r3]	@ movhi
	ldrb	r3, [r4, #-1988]	@ zero_extendqisi2
	strb	r3, [r4, #-2061]
	ldr	r3, .L1424+40
	ldrh	r2, [r4, r3]
	sub	r3, r3, #106
	strh	r2, [r4, r3]	@ movhi
	add	r3, r3, #108
	ldrh	r2, [r4, r3]
	ldrb	r3, [r4, #-1942]	@ zero_extendqisi2
	orr	r2, r3, r2, asl #6
	ldr	r3, .L1424+44
	strh	r2, [r4, r3]	@ movhi
	ldrb	r3, [r4, #-1940]	@ zero_extendqisi2
	strb	r3, [r4, #-2060]
	ldr	r3, .L1424+48
	ldrh	r2, [r4, r3]
	sub	r3, r3, #150
	strh	r2, [r4, r3]	@ movhi
	add	r3, r3, #152
	ldr	r0, [r4, #188]
	ldrh	r2, [r4, r3]
	ldrb	r3, [r4, #-1894]	@ zero_extendqisi2
	orr	r2, r3, r2, asl #6
	ldr	r3, .L1424+52
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
	ldr	r1, .L1424+56
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
	beq	.L1413
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
.L1413:
	mov	r0, #0
	ldr	r4, .L1424
	bl	FtlUpdateVaildLpn
	ldr	r8, .L1424+8
	mov	r6, #0
	movw	sl, #65535
.L1423:
	ldr	r3, [r4, #-960]
	mov	r1, #1
	ldr	r7, .L1424+60
	ldrh	r2, [r4, r8]
	str	r3, [r4, #188]
	ldr	r3, [r4, #-936]
	ldr	r0, .L1424+64
	str	r3, [r4, #192]
	ldrh	r3, [r4, r7]
	orr	r3, r3, r2, asl #10
	mov	r2, r1
	str	r3, [r4, #184]
	mov	r3, r1
	bl	FlashProgPages
	ldr	r1, .L1424+4
	movw	r3, #3918
	ldrh	r2, [r4, r7]
	ldrh	r3, [r1, r3]
	sub	r3, r3, #1
	cmp	r2, r3
	blt	.L1415
	ldr	r3, .L1424+68
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
	ldr	r0, .L1424+64
	bl	FlashProgPages
.L1415:
	ldr	r2, .L1424+60
	ldr	r1, [r4, #180]
	ldrh	r3, [r4, r2]
	cmn	r1, #1
	add	r3, r3, #1
	uxth	r3, r3
	strh	r3, [r4, r2]	@ movhi
	bne	.L1416
	cmp	r3, #1
	add	r6, r6, #1
	ldreq	r1, .L1424+4
	movweq	r3, #3918
	uxth	r6, r6
	ldreqh	r3, [r1, r3]
	subeq	r3, r3, #1
	streqh	r3, [r4, r2]	@ movhi
	cmp	r6, #3
	bls	.L1423
	ldr	r3, .L1424
	mov	r2, r6
	ldr	r0, .L1424+72
	ldr	r1, [r3, #184]
	bl	printk
	ldr	r3, .L1424+4
	mov	r2, #1
	str	r2, [r3, #4040]
	ldmfd	sp!, {r3, r4, r5, r6, r7, r8, sl, pc}
.L1416:
	cmp	r3, #1
	beq	.L1423
	cmp	r1, #256
	beq	.L1423
	movw	r3, #65535
	cmp	sl, r3
	ldmeqfd	sp!, {r3, r4, r5, r6, r7, r8, sl, pc}
	mov	r0, sl
	mov	r1, #1
	bl	FtlFreeSysBlkQueueIn
	ldmfd	sp!, {r3, r4, r5, r6, r7, r8, sl, pc}
.L1425:
	.align	2
.L1424:
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
	.word	.LC112
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
	ldr	r3, .L1428
	ldr	r3, [r3, #4040]
	cmp	r3, #0
	bne	.L1427
	bl	FtlVpcTblFlush.part.14
.L1427:
	mov	r0, #0
	ldmfd	sp!, {r3, pc}
.L1429:
	.align	2
.L1428:
	.word	.LANCHOR0
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
	ldr	r7, .L1443
	mov	r5, #0
	ldr	sl, .L1443+4
	movw	r9, #3870
	ldr	r8, .L1443+8
	ldr	r3, [r7, #-936]
	ldr	r0, [r7, #-960]
	str	r3, [r7, #192]
	movw	r3, #3926
	str	r0, [r7, #188]
	ldrh	r2, [sl, r3]
	bl	ftl_memset
	b	.L1431
.L1432:
	ldr	r3, .L1443+12
	ldr	r1, [r8, #4]!
	ldrh	r2, [r7, r3]
	ldr	r3, [r7, #188]
	mul	r0, r2, r5
	mov	r2, r2, asl #2
	add	r5, r5, #1
	add	r0, r3, r0, asl #2
	bl	memcpy
.L1431:
	ldrh	r3, [sl, r9]
	ldr	r4, .L1443+4
	cmp	r5, r3
	ldr	r6, .L1443
	blt	.L1432
	ldr	r5, [r6, #192]
	mov	r1, #255
	mov	r2, #16
	mov	r7, #0
	mov	fp, r7
	mov	r0, r5
	bl	ftl_memset
	ldr	r3, .L1443+16
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
	b	.L1441
.L1438:
	mov	fp, #1
.L1441:
	ldr	r3, [r6, #-960]
	movw	r8, #3980
	ldr	r4, .L1443+4
	mov	r1, #0
	movw	sl, #3982
	str	r1, [r6, #180]
	str	r3, [r6, #188]
	mov	r9, #3984
	ldr	r3, [r6, #-936]
	ldrh	r1, [r4, r8]
	ldrh	r2, [r4, sl]
	str	r3, [r6, #192]
	ldrh	r0, [r5, #10]
	orr	r3, r2, r1, asl #10
	str	r3, [r6, #184]
	ldrh	r3, [r4, r9]
	str	r0, [sp, #0]
	ldr	r0, .L1443+20
	bl	printk
	mov	r1, #1
	mov	r2, r1
	mov	r3, r1
	ldr	r0, .L1443+24
	bl	FlashProgPages
	movw	r3, #3918
	ldrh	r3, [r4, r3]
	ldrh	r2, [r4, sl]
	sub	r3, r3, #1
	cmp	r2, r3
	blt	.L1434
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
	str	r3, [r6, #184]
	str	r3, [r0, #4]
	bl	FlashEraseBlocks
	mov	r1, #1
	ldr	r0, .L1443+24
	mov	r2, r1
	mov	r3, r1
	bl	FlashProgPages
.L1434:
	ldr	r4, .L1443+4
	movw	r3, #3982
	ldrh	r2, [r4, r3]
	add	r2, r2, #1
	strh	r2, [r4, r3]	@ movhi
	ldr	r3, [r6, #180]
	cmn	r3, #1
	bne	.L1435
	add	r7, r7, #1
	ldr	r0, .L1443+28
	ldr	r1, [r6, #184]
	uxth	r7, r7
	bl	printk
	cmp	r7, #3
	bls	.L1441
	ldr	r3, .L1443
	mov	r2, r7
	ldr	r0, .L1443+32
	ldr	r1, [r3, #184]
	bl	printk
	mov	r3, #1
	str	r3, [r4, #4040]
	b	.L1442
.L1435:
	cmp	fp, #0
	beq	.L1438
.L1442:
	ldmfd	sp!, {r1, r2, r3, r4, r5, r6, r7, r8, r9, sl, fp, pc}
.L1444:
	.align	2
.L1443:
	.word	.LANCHOR2
	.word	.LANCHOR0
	.word	.LANCHOR0+4004
	.word	-1028
	.word	-3887
	.word	.LC113
	.word	.LANCHOR2+180
	.word	.LC114
	.word	.LC115
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
	ldr	r3, .L1447
	ldr	r3, [r3, #4040]
	cmp	r3, #0
	bne	.L1446
	bl	FtlBbmTblFlush.part.16
.L1446:
	mov	r0, #0
	ldmfd	sp!, {r3, pc}
.L1448:
	.align	2
.L1447:
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
	ldr	r4, .L1459
	ldr	r3, .L1459+4
	ldrh	r3, [r4, r3]
	cmp	r3, #0
	movne	r6, #0
	movne	sl, r4
	bne	.L1450
	b	.L1451
.L1457:
	add	r3, r3, r6
	mov	r1, r9
	mov	r5, #0
	ldr	r7, .L1459+4
	ldrb	r0, [r3, #3874]	@ zero_extendqisi2
	bl	V2P_block
	mov	r8, r0
	b	.L1452
.L1456:
	add	r3, r4, r5, asl #1
	sub	r3, r3, #1136
	ldrh	r3, [r3, #0]
	cmp	r3, r8
	bne	.L1453
	mov	r1, r8
	ldr	r0, .L1459+8
	bl	printk
	mov	r0, r8
	bl	FtlBbmMapBadBlock
	bl	FtlBbmTblFlush
	ldrh	r2, [r4, r7]
	mov	r3, r5
	b	.L1454
.L1455:
	add	r1, r3, #1
	add	r3, r4, r3, asl #1
	sub	r3, r3, #1136
	add	r0, r4, r1, asl #1
	sub	r0, r0, #1136
	ldrh	r0, [r0, #0]
	strh	r0, [r3, #0]	@ movhi
	uxth	r3, r1
.L1454:
	cmp	r3, r2
	bcc	.L1455
	sub	r2, r2, #1
	strh	r2, [sl, r7]	@ movhi
.L1453:
	add	r5, r5, #1
	uxth	r5, r5
.L1452:
	ldrh	r3, [r4, r7]
	cmp	r3, r5
	bhi	.L1456
	add	r6, r6, #1
	uxth	r6, r6
.L1450:
	ldr	r3, .L1459+12
	movw	r2, #3848
	ldrh	r2, [r3, r2]
	cmp	r2, r6
	bhi	.L1457
	bl	FtlGcReFreshBadBlk
.L1451:
	mov	r0, #0
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, sl, pc}
.L1460:
	.align	2
.L1459:
	.word	.LANCHOR2
	.word	-1138
	.word	.LC116
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
	ldr	r3, .L1469
	mov	r4, r0
	ldr	r1, [r3, #-2016]
	ldrh	r2, [r1, r2]
	cmp	r2, #0
	bne	.L1462
	ldr	r1, .L1469+4
	ldrh	r0, [r3, r1]
	cmp	r0, r4
	mvneq	r2, #0
	streqh	r2, [r3, r1]	@ movhi
	beq	.L1464
	ldr	r1, .L1469+8
	ldrh	r1, [r3, r1]
	cmp	r1, r4
	beq	.L1465
	ldr	r1, .L1469+12
	ldrh	r1, [r3, r1]
	cmp	r1, r4
	beq	.L1465
	ldr	r1, .L1469+16
	ldrh	r3, [r3, r1]
	cmp	r3, r4
	beq	.L1465
.L1464:
	mov	r1, r4
	ldr	r0, .L1469+20
	bl	List_remove_node
	ldr	r5, .L1469
	ldr	r3, .L1469+24
	mov	r0, r4
	ldrh	r2, [r5, r3]
	sub	r2, r2, #1
	strh	r2, [r5, r3]	@ movhi
	bl	free_data_superblock
	mov	r0, r4
	bl	FtlGcFreeBadSuperBlk
	mov	r2, #1
	b	.L1465
.L1462:
	bl	List_update_data_list
	mov	r2, #0
.L1465:
	mov	r0, r2
	ldmfd	sp!, {r3, r4, r5, pc}
.L1470:
	.align	2
.L1469:
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
	beq	.L1472
	ldr	r2, .L1477
	mov	r3, r0, asl #1
	ldr	r2, [r2, #-2016]
	ldrh	r4, [r2, r3]
	cmp	r4, #0
	subne	r4, r4, #1
	strneh	r4, [r2, r3]	@ movhi
	bne	.L1472
	ldr	r0, .L1477+4
	mov	r1, r5
	mov	r2, r4
	bl	printk
	mov	r0, r4
	ldmfd	sp!, {r3, r4, r5, r6, r7, pc}
.L1472:
	ldr	r7, .L1477
	movw	r3, #65535
	ldr	r6, .L1477+8
	ldrh	r0, [r7, r6]
	cmp	r0, r3
	streqh	r5, [r7, r6]	@ movhi
	moveq	r0, #0
	ldmeqfd	sp!, {r3, r4, r5, r6, r7, pc}
	cmp	r0, r5
	beq	.L1476
	bl	update_vpc_list
	strh	r5, [r7, r6]	@ movhi
	adds	r0, r0, #0
	movne	r0, #1
	ldmfd	sp!, {r3, r4, r5, r6, r7, pc}
.L1476:
	mov	r0, #0
	ldmfd	sp!, {r3, r4, r5, r6, r7, pc}
.L1478:
	.align	2
.L1477:
	.word	.LANCHOR2
	.word	.LC117
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
	ldr	r5, .L1491
	ldr	r7, .L1491+4
	add	r3, r0, r3, asl #1
	ldrh	r3, [r3, #16]
	b	.L1480
.L1481:
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
.L1480:
	cmp	r3, r6
	beq	.L1481
	ldrb	r2, [r4, #8]	@ zero_extendqisi2
	cmp	r2, #1
	bne	.L1483
	ldrb	r2, [r5, #928]	@ zero_extendqisi2
	cmp	r2, #0
	bne	.L1483
	ldrh	r2, [r4, #2]
	add	r2, r7, r2, asl #1
	sub	r2, r2, #892
	ldrh	r2, [r2, #0]
	cmp	r2, r6
	bne	.L1483
	ldrh	r3, [r4, #4]
	ldrh	r0, [r4, #0]
	sub	r3, r3, #1
	strh	r3, [r4, #4]	@ movhi
	bl	decrement_vpc_count
	b	.L1481
.L1483:
	ldrh	r6, [r4, #2]
	movw	r5, #65535
	mov	r7, r5
	orr	r6, r6, r3, asl #10
	ldrh	r3, [r4, #4]
	sub	r3, r3, #1
	strh	r3, [r4, #4]	@ movhi
.L1488:
	ldr	r2, .L1491
	movw	r3, #3848
	ldrh	r2, [r2, r3]
	ldrb	r3, [r4, #6]	@ zero_extendqisi2
.L1485:
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
	beq	.L1485
	strb	r3, [r4, #6]
	ldrb	r3, [r4, #8]	@ zero_extendqisi2
	cmp	r3, #1
	bne	.L1486
	ldr	r3, .L1491
	ldrb	r2, [r3, #928]	@ zero_extendqisi2
	cmp	r2, #0
	ldrh	r2, [r4, #2]
	bne	.L1489
	ldr	r3, .L1491+4
	add	r2, r3, r2, asl #1
	sub	r2, r2, #892
	ldrh	r3, [r2, #0]
	cmp	r3, r7
	bne	.L1486
	ldrh	r3, [r4, #4]
	cmp	r3, #0
	beq	.L1486
	sub	r3, r3, #1
	ldrh	r0, [r4, #0]
	strh	r3, [r4, #4]	@ movhi
	bl	decrement_vpc_count
	b	.L1488
.L1489:
	movw	r1, #3918
	ldrh	r1, [r3, r1]
	cmp	r2, r1
	bcc	.L1486
	ldr	r1, .L1491+4
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
.L1486:
	mov	r0, r6
	ldmfd	sp!, {r3, r4, r5, r6, r7, pc}
.L1492:
	.align	2
.L1491:
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
	ldr	r5, .L1499
	ldr	r7, .L1499+4
	add	r3, r0, r3, asl #1
	ldrh	r3, [r3, #16]
	b	.L1494
.L1495:
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
.L1494:
	cmp	r3, r6
	beq	.L1495
	ldrb	r2, [r4, #8]	@ zero_extendqisi2
	cmp	r2, #1
	bne	.L1497
	ldrb	r3, [r5, #928]	@ zero_extendqisi2
	cmp	r3, #0
	bne	.L1497
	ldrh	r3, [r4, #2]
	add	r3, r7, r3, asl #1
	sub	r3, r3, #892
	ldrh	r3, [r3, #0]
	cmp	r3, r6
	bne	.L1497
	ldrh	r3, [r4, #4]
	ldrh	r0, [r4, #0]
	sub	r3, r3, #1
	strh	r3, [r4, #4]	@ movhi
	bl	decrement_vpc_count
	ldrh	r3, [r4, #4]
	cmp	r3, #0
	bne	.L1495
	ldrh	r2, [r4, #2]
	strb	r3, [r4, #6]
	add	r2, r2, #1
	strh	r2, [r4, #2]	@ movhi
	ldmfd	sp!, {r3, r4, r5, r6, r7, pc}
.L1497:
	ldr	r3, .L1499
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
	ldr	r1, .L1499+4
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
.L1500:
	.align	2
.L1499:
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
	@ args = 0, pretend = 0, frame = 24
	@ frame_needed = 0, uses_anonymous_args = 0
	stmfd	sp!, {r4, r5, r6, r7, r8, r9, sl, fp, lr}
	.save {r4, r5, r6, r7, r8, r9, sl, fp, lr}
	.pad #28
	sub	sp, sp, #28
	ldr	r5, .L1538
	mov	r4, r0
	mov	r8, r5
.L1535:
	ldr	r3, .L1538+4
	cmp	r4, r3
	bne	.L1505
	ldr	r3, .L1538+8
	ldrh	r2, [r5, r3]
	ldr	r3, [r5, #-1184]
	mov	r1, r2, lsr #1
	mul	ip, r3, r2
	add	r0, r1, #1
	add	r0, r0, ip, lsr #2
	ldr	ip, .L1538+12
	uxth	r0, r0
	ldr	ip, [ip, #3840]
	cmp	ip, #0
	beq	.L1529
	ldr	ip, [r5, #-1780]
	cmp	ip, #29
	bhi	.L1529
	cmp	ip, #2
	bls	.L1534
	tst	r2, #1
	beq	.L1506
	cmp	r3, #0
	moveq	r1, r3
	beq	.L1507
	b	.L1506
.L1505:
	ldrb	r3, [r4, #8]	@ zero_extendqisi2
	cmp	r3, #1
	bne	.L1534
	ldr	r3, .L1538+12
	movw	r2, #3868
	ldrh	r2, [r3, r2]
	cmp	r2, #1
	beq	.L1534
	ldrb	r2, [r3, #928]	@ zero_extendqisi2
	cmp	r2, #0
	bne	.L1534
	sub	r2, r2, #2000
	ldr	r3, [r3, #3840]
	ldrh	r2, [r5, r2]
	cmp	r3, #0
	mov	r1, r2, lsr #3
	beq	.L1506
	ldr	r3, [r5, #-1780]
	cmp	r3, #1
	movls	r1, #7
	mulls	r1, r1, r2
	movls	r1, r1, lsr #3
	b	.L1506
.L1529:
	mov	r1, r0
.L1506:
	cmp	r1, #0
	subne	r1, r1, #1
	uxthne	r1, r1
	b	.L1507
.L1534:
	mov	r1, #0
.L1507:
	ldrb	r2, [r4, #8]	@ zero_extendqisi2
	ldr	r0, .L1538+16
	bl	List_pop_index_node
	ldr	r3, .L1538+8
	ldrh	r2, [r5, r3]
	sub	r2, r2, #1
	strh	r2, [r5, r3]	@ movhi
	uxth	r7, r0
	mov	r0, r4
	strh	r7, [r4, #0]	@ movhi
	bl	make_superblock
	ldrb	r3, [r4, #7]	@ zero_extendqisi2
	cmp	r3, #0
	beq	.L1508
	ldr	r3, .L1538+12
	movw	r2, #3848
	mov	r6, #0
	mov	r9, r4
	mov	r1, r4
	mov	r0, r6
	ldr	ip, [r3, #4044]
	ldrh	sl, [r3, r2]
	mov	r2, r6
	mov	r3, ip
	stmib	sp, {r4, ip}
	b	.L1509
.L1508:
	ldr	r3, [r5, #-2016]
	mov	r7, r7, asl #1
	mvn	r2, #0
	strh	r2, [r3, r7]	@ movhi
	b	.L1535
.L1512:
	str	r0, [r3, #8]
	movw	r4, #65535
	str	r0, [r3, #12]
	ldrh	lr, [r1, #16]
	cmp	lr, r4
	beq	.L1511
	ldr	r4, [sp, #8]
	mov	ip, #36
	mov	lr, lr, asl #10
	mla	fp, ip, r6, r4
	add	r6, r6, #1
	uxth	r6, r6
	str	lr, [fp, #4]
.L1511:
	add	r2, r2, #1
	add	r3, r3, #36
	add	r1, r1, #2
	uxth	r2, r2
.L1509:
	cmp	r2, sl
	bne	.L1512
	ldr	r3, .L1538+12
	ldr	r4, [sp, #4]
	ldr	r2, [r3, #3840]
	cmp	r2, #0
	beq	.L1513
	ldr	r2, .L1538+20
	cmp	r4, r2
	bne	.L1513
	ldr	r2, [r3, #4048]
	mov	r3, r7, asl #1
	ldrh	r3, [r2, r3]
	cmp	r3, #30
	movhi	r3, #0
	strhib	r3, [r5, #-1988]
.L1513:
	ldrb	r3, [r4, #8]	@ zero_extendqisi2
	ldr	r2, .L1538+12
	cmp	r3, #0
	mov	r3, r7, asl #1
	bne	.L1514
	ldr	r1, [r2, #4048]
	ldrh	r0, [r1, r3]
	cmp	r0, #0
	movwne	ip, #3906
	moveq	r2, #2
	ldrneh	r2, [r2, ip]
	streqh	r2, [r1, r3]	@ movhi
	addne	r0, r0, r2
	strneh	r0, [r1, r3]	@ movhi
	ldr	r3, [r5, #-1796]
	mov	r0, r7
	mov	r1, #0
	add	r3, r3, #1
	str	r3, [r5, #-1796]
	b	.L1536
.L1514:
	ldr	r2, [r2, #4048]
	mov	r0, r7
	ldrh	r1, [r2, r3]
	add	r1, r1, #1
	strh	r1, [r2, r3]	@ movhi
	ldr	r3, [r5, #-1792]
	mov	r1, #1
	add	r3, r3, #1
	str	r3, [r5, #-1792]
.L1536:
	ldr	sl, .L1538+12
	bl	ftl_set_blk_mode
	mov	fp, r7, asl #1
	ldr	r2, [r5, #-1784]
	ldr	r0, [r8, #-1796]
	ldr	r3, [sl, #4048]
	ldrh	r3, [r3, fp]
	cmp	r3, r2
	strhi	r3, [r5, #-1784]
	movw	r3, #3906
	ldrh	r2, [sl, r3]
	ldr	r3, [r8, #-1792]
	mla	r0, r0, r2, r3
	mov	r3, #3856
	ldrh	r1, [sl, r3]
	bl	__aeabi_uidiv
	ldr	r3, [r8, #-920]
	ldr	r2, [r3, #16]
	add	r2, r2, #1
	str	r2, [r3, #16]
	ldr	r3, [sl, #4044]
	mov	r2, #0
	str	r0, [r8, #-1788]
	b	.L1519
.L1520:
	add	r2, r2, #1
	ldr	r1, [r3, #-32]
	uxth	r2, r2
	bic	r1, r1, #1020
	bic	r1, r1, #3
	str	r1, [r3, #-32]
.L1519:
	cmp	r2, r6
	add	r3, r3, #36
	bne	.L1520
	ldr	sl, .L1538+12
	ldrb	r3, [sl, #928]	@ zero_extendqisi2
	cmp	r3, #0
	beq	.L1521
	ldrb	r3, [r4, #8]	@ zero_extendqisi2
	mov	r2, r6
	ldr	r0, [sl, #4044]
	cmp	r3, #1
	moveq	r1, #0
	movne	r1, #1
	bl	FlashEraseBlocks
.L1521:
	mov	r2, r6
	ldr	r0, [sl, #4044]
	ldrb	r1, [r4, #8]	@ zero_extendqisi2
	bl	FlashEraseBlocks
	mov	r1, #36
	mul	r1, r1, r6
	mov	r6, #0
	mov	r3, fp
	mov	sl, r6
	mov	fp, r7
	ldr	r2, .L1538+12
	mov	r7, r1
	b	.L1523
.L1525:
	ldr	r1, [r2, #4044]
	add	r0, r1, r6
	ldr	r1, [r1, r6]
	cmn	r1, #1
	bne	.L1524
	ldr	r0, [r0, #4]
	add	sl, sl, #1
	str	r1, [sp, #20]
	ubfx	r0, r0, #10, #16
	str	r2, [sp, #16]
	str	r3, [sp, #12]
	bl	FtlBbmMapBadBlock
	ldr	r1, [sp, #20]
	strh	r1, [r9, #16]	@ movhi
	ldrb	r1, [r4, #7]	@ zero_extendqisi2
	sub	r1, r1, #1
	strb	r1, [r4, #7]
	ldr	r3, [sp, #12]
	ldr	r2, [sp, #16]
.L1524:
	add	r6, r6, #36
	add	r9, r9, #2
.L1523:
	cmp	r6, r7
	bne	.L1525
	cmp	sl, #0
	mov	r7, fp
	mov	fp, r3
	beq	.L1526
	mov	r0, r7
	bl	update_multiplier_value
	bl	FtlBbmTblFlush
.L1526:
	ldrb	r3, [r4, #7]	@ zero_extendqisi2
	cmp	r3, #0
	ldreq	r3, [r5, #-2016]
	mvneq	r2, #0
	streqh	r2, [r3, fp]	@ movhi
	beq	.L1535
.L1527:
	ldr	r1, .L1538+12
	movw	r2, #3916
	ldrh	r2, [r1, r2]
	strh	r7, [r4, #0]	@ movhi
	mul	r2, r2, r3
	mov	r3, #0
	strh	r3, [r4, #2]	@ movhi
	strb	r3, [r4, #6]
	ldr	r3, .L1538
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
.L1539:
	.align	2
.L1538:
	.word	.LANCHOR2
	.word	.LANCHOR2-1900
	.word	-2000
	.word	.LANCHOR0
	.word	.LANCHOR2-2004
	.word	.LANCHOR2-1996
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
	ldr	r3, .L1542
	ldr	r3, [r3, #4040]
	cmp	r3, #0
	bne	.L1541
	bl	allocate_data_superblock.part.21
.L1541:
	mov	r0, #0
	ldmfd	sp!, {r3, pc}
.L1543:
	.align	2
.L1542:
	.word	.LANCHOR0
	.fnend
	.size	allocate_data_superblock, .-allocate_data_superblock
	.align	2
	.type	FtlSuperblockPowerLostFix.part.19, %function
FtlSuperblockPowerLostFix.part.19:
	.fnstart
	@ args = 0, pretend = 0, frame = 40
	@ frame_needed = 0, uses_anonymous_args = 0
	ldr	r3, .L1553
	stmfd	sp!, {r4, r5, r6, r7, r8, lr}
	.save {r4, r5, r6, r7, r8, lr}
	mov	r4, r0
	ldrb	r7, [r3, #928]	@ zero_extendqisi2
	.pad #40
	sub	sp, sp, #40
	cmp	r7, #0
	beq	.L1552
	ldrb	r7, [r0, #8]	@ zero_extendqisi2
	cmp	r7, #1
	ldreqh	r6, [r0, #4]
	beq	.L1545
	mov	r7, #0
.L1552:
	mov	r6, #12
.L1545:
	mvn	r8, #0
	ldr	r5, .L1553+4
	b	.L1546
.L1549:
	ldrh	r3, [r4, #4]
	cmp	r3, #0
	beq	.L1547
	mov	r0, r4
	bl	get_new_active_ppa
	cmn	r0, #1
	str	r0, [sp, #8]
	beq	.L1547
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
.L1546:
	cmp	r6, #0
	bne	.L1549
.L1547:
	ldr	r2, .L1553+4
	ldrh	r3, [r4, #0]
	ldrh	r1, [r4, #4]
	ldr	r2, [r2, #-2016]
	mov	r3, r3, asl #1
	ldrh	r0, [r2, r3]
	rsb	r1, r1, r0
	strh	r1, [r2, r3]	@ movhi
	ldr	r2, .L1553
	movw	r3, #3916
	ldrh	r3, [r2, r3]
	strh	r3, [r4, #2]	@ movhi
	mov	r3, #0
	strb	r3, [r4, #6]
	strh	r3, [r4, #4]	@ movhi
	add	sp, sp, #40
	ldmfd	sp!, {r4, r5, r6, r7, r8, pc}
.L1554:
	.align	2
.L1553:
	.word	.LANCHOR0
	.word	.LANCHOR2
	.fnend
	.size	FtlSuperblockPowerLostFix.part.19, .-FtlSuperblockPowerLostFix.part.19
	.align	2
	.global	FtlSuperblockPowerLostFix
	.type	FtlSuperblockPowerLostFix, %function
FtlSuperblockPowerLostFix:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	@ link register save eliminated.
	ldr	r3, .L1557
	ldr	r3, [r3, #4040]
	cmp	r3, #0
	bxne	lr
	b	FtlSuperblockPowerLostFix.part.19
.L1558:
	.align	2
.L1557:
	.word	.LANCHOR0
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
	ldr	r8, .L1589
	.pad #28
	sub	sp, sp, #28
	str	r0, [sp, #8]
	ldr	r6, [r8, #4040]
	ldrb	r1, [r8, #3837]	@ zero_extendqisi2
	cmp	r6, #0
	str	r1, [sp, #16]
	movne	r4, #0
	bne	.L1560
	ldrb	r2, [r8, #928]	@ zero_extendqisi2
	mov	r5, r6
	ldr	fp, .L1589+4
	mov	r4, r6
	mov	r9, #36
	str	r2, [sp, #4]
	str	r0, [fp, #-984]
	b	.L1561
.L1565:
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
	beq	.L1562
	bl	IsBlkInVendorPart
	cmp	r0, #0
	bne	.L1563
.L1562:
	mov	r0, r7
	bl	FtlBbmIsBadBlock
	cmp	r0, #0
	addne	r4, r4, #1
	uxthne	r4, r4
	bne	.L1563
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
	ldr	r1, [fp, #-932]
	bic	r2, r2, #3
	str	r0, [r3, #8]
	add	r2, r1, r2
	str	r2, [r3, #12]
.L1563:
	add	r6, r6, #1
	uxth	r6, r6
.L1561:
	movw	r2, #3848
	ldr	r7, .L1589
	ldrh	r3, [r8, r2]
	cmp	r3, r6
	bhi	.L1565
	cmp	r5, #0
	beq	.L1560
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
.L1568:
	ldr	r3, [r7, #4044]
	add	r2, r3, r8
	ldr	r3, [r3, r8]
	cmn	r3, #1
	bne	.L1567
	ldr	r0, [r2, #4]
	add	r4, r4, #1
	ubfx	r0, r0, #10, #16
	uxth	r4, r4
	bl	FtlBbmMapBadBlock
.L1567:
	add	r9, r9, #1
	add	r8, r8, #36
	uxth	r9, r9
	cmp	r9, r5
	bne	.L1568
	cmp	sl, #0
	beq	.L1582
	ldr	r3, .L1589
	movw	r2, #3918
	ldrh	r2, [r3, r2]
	ldrb	r3, [r3, #928]	@ zero_extendqisi2
	cmp	r3, #0
	str	r2, [sp, #12]
	moveq	r2, r2, lsr #2
	streq	r2, [sp, #4]
	moveq	r6, #1
	beq	.L1569
	b	.L1588
.L1582:
	mov	r3, #6
	mov	r1, #1
	str	r3, [sp, #4]
	str	r1, [sp, #12]
	b	.L1569
.L1588:
	mov	r6, #1
	str	r6, [sp, #4]
.L1569:
	ldr	r7, .L1589
	mov	r8, #0
.L1577:
	mov	r9, #0
	ldr	fp, .L1589+4
	mov	r5, r9
	b	.L1570
.L1573:
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
	beq	.L1571
	bl	IsBlkInVendorPart
	cmp	r0, #0
	bne	.L1572
.L1571:
	ldr	r0, [sp, #20]
	bl	FtlBbmIsBadBlock
	cmp	r0, #0
	bne	.L1572
	ldr	r3, [r7, #4044]
	mov	r2, #36
	ldr	r1, [sp, #20]
	mla	r3, r2, r5, r3
	add	r2, r8, r1, asl #10
	movw	r1, #3928
	str	r2, [r3, #4]
	ldr	r2, [fp, #-944]
	str	r2, [r3, #8]
	ldrh	r2, [r7, r1]
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
.L1572:
	add	r9, r9, #1
	uxth	r9, r9
.L1570:
	movw	r2, #3848
	ldr	ip, .L1589
	ldrh	r3, [r7, r2]
	cmp	r3, r9
	bhi	.L1573
	cmp	r5, #0
	beq	.L1560
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
.L1576:
	ldr	r3, [r7, #4044]
	add	r2, r3, r9
	ldr	r3, [r3, r9]
	cmp	r3, #0
	beq	.L1575
	ldr	r0, [r2, #4]
	add	r4, r4, #1
	ubfx	r0, r0, #10, #16
	uxth	r4, r4
	bl	FtlBbmMapBadBlock
.L1575:
	add	fp, fp, #1
	add	r9, r9, #36
	uxth	fp, fp
	cmp	fp, r5
	bne	.L1576
	ldr	r2, [sp, #4]
	ldr	r3, [sp, #12]
	add	r8, r8, r2
	uxth	r8, r8
	cmp	r8, r3
	bcc	.L1577
	ldr	r9, .L1589
	mov	r7, #0
	mov	r8, r7
.L1579:
	cmp	sl, #0
	beq	.L1578
	ldr	r3, [r9, #4044]
	add	r2, r3, r7
	ldr	r3, [r3, r7]
	cmp	r3, #0
	bne	.L1578
	ldr	r0, [r2, #4]
	mov	r1, #1
	ubfx	r0, r0, #10, #16
	bl	FtlFreeSysBlkQueueIn
.L1578:
	add	r8, r8, #1
	add	r7, r7, #36
	uxth	r8, r8
	cmp	r8, r5
	bne	.L1579
	ldr	r1, [sp, #8]
	cmp	r1, #63
	movhi	r3, #0
	movls	r3, #1
	cmp	sl, #0
	moveq	sl, r3
	orrne	sl, r3, #1
	cmp	sl, #0
	beq	.L1560
	ldr	r3, .L1589
	mov	r1, r6
	mov	r2, r8
	ldr	r0, [r3, #4044]
	bl	FlashEraseBlocks
.L1560:
	mov	r0, r4
	add	sp, sp, #28
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, sl, fp, pc}
.L1590:
	.align	2
.L1589:
	.word	.LANCHOR0
	.word	.LANCHOR2
	.fnend
	.size	FtlLowFormatEraseBlock, .-FtlLowFormatEraseBlock
	.align	2
	.type	FlashTestBlk.part.25, %function
FlashTestBlk.part.25:
	.fnstart
	@ args = 0, pretend = 0, frame = 104
	@ frame_needed = 0, uses_anonymous_args = 0
.L1592:
	stmfd	sp!, {r4, r5, lr}
	.save {r4, r5, lr}
	.pad #108
	sub	sp, sp, #108
	ldr	r4, .L1594
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
.L1595:
	.align	2
.L1594:
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
	ldr	r3, .L1598
	ldr	r3, [r3, #156]
	cmp	r0, r3
	bcc	.L1597
	b	FlashTestBlk.part.25
.L1597:
	mov	r0, #0
	bx	lr
.L1599:
	.align	2
.L1598:
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
	ldr	r3, .L1628
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
	ldr	r4, .L1628+4
	mul	fp, fp, r2
	ldr	r2, [r3, #3624]
	ldr	r0, [r4, #228]
	ldrb	r2, [r2, #24]	@ zero_extendqisi2
	str	r0, [sp, #28]
	ldr	r0, .L1628+8
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
	ldr	r4, .L1628
	uxth	r3, r3
	str	r2, [sp, #20]
	str	r3, [sp, #24]
	b	.L1602
.L1624:
	ldr	r3, .L1628+4
	add	r2, r3, r5, asl #1
	ldrh	r6, [r2, #232]
	cmp	r6, #0
	bne	.L1603
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
	b	.L1604
.L1612:
	mvn	r3, #0
	strb	r3, [sp, #34]
	strb	r3, [sp, #35]
	ldr	r3, [sp, #12]
	tst	r3, #1
	beq	.L1605
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
	bne	.L1605
	ldr	r1, [r4, #856]
	add	r2, sp, #35
	mov	r0, r9
	add	r1, r3, r1
	bl	FlashReadSpare
	ldrb	r3, [sp, #34]	@ zero_extendqisi2
	ldrb	r2, [sp, #35]	@ zero_extendqisi2
	and	r3, r2, r3
	strb	r3, [sp, #34]
.L1605:
	ldr	r0, [sp, #12]
	tst	r0, #2
	beq	.L1606
	ldr	r3, [r4, #3624]
	mov	r0, r9
	add	r2, sp, #35
	ldrh	r1, [r3, #10]
	ldr	r3, [sl, #3588]
	sub	r1, r1, #1
	add	r1, r1, r3
	add	r1, r1, r6
	bl	FlashReadSpare
.L1606:
	ldr	r3, [r4, #3624]
	ldrb	r3, [r3, #7]	@ zero_extendqisi2
	cmp	r3, #1
	cmpne	r3, #8
	ldrb	r3, [sp, #34]	@ zero_extendqisi2
	bne	.L1607
	cmp	r3, #0
	beq	.L1626
	ldrb	r0, [sp, #35]	@ zero_extendqisi2
	rsbs	r0, r0, #1
	movcc	r0, #0
	b	.L1608
.L1607:
	cmp	r3, #255
	bne	.L1626
	ldrb	r0, [sp, #35]	@ zero_extendqisi2
	subs	r0, r0, #255
	movne	r0, #1
	b	.L1608
.L1626:
	mov	r0, #1
.L1608:
	ldr	r2, [sp, #12]
	tst	r2, #4
	beq	.L1609
	ldr	r1, [sl, #3588]
	mov	r0, r9
	add	r1, r6, r1
	bl	SandiskProgTestBadBlock
.L1609:
	cmp	r0, #0
	beq	.L1610
	mov	r1, r5
	mov	r2, r8
	ldr	r0, .L1628+12
	add	r7, r7, #1
	bl	printk
	ldr	r3, [sp, #16]
	and	r0, r8, #31
	mov	ip, #1
	uxth	r7, r7
	mov	r2, r3, lsr #5
	ldr	r3, .L1628+4
	ldr	r3, [r3, #148]
	ldr	r1, [r3, r2, asl #2]
	orr	r1, r1, ip, asl r0
	ldr	r0, [sp, #20]
	str	r1, [r3, r2, asl #2]
	ldrb	r3, [r4, #3762]	@ zero_extendqisi2
	mul	r3, r3, r0
	cmp	r7, r3
	bgt	.L1611
.L1610:
	ldr	r2, [sp, #8]
	add	r8, r8, #1
	add	r6, r6, r2
.L1604:
	uxth	r3, r8
	str	r3, [sp, #16]
	cmp	r3, fp
	bcc	.L1612
.L1611:
	ldr	r0, .L1628+16
	mov	r1, r5
	mov	r2, r7
	bl	printk
	ldrb	r3, [r4, #3762]	@ zero_extendqisi2
	ldr	r0, [sp, #20]
	mul	r3, r3, r0
	cmp	r7, r3
	blt	.L1613
	movw	r3, #3068
	mov	r1, #0
	ldrh	r2, [r4, r3]
	ldr	r3, .L1628+4
	mov	r2, r2, asl #9
	ldr	r0, [r3, #148]
	bl	ftl_memset
.L1613:
	cmp	r5, #0
	bne	.L1614
	ldr	sl, .L1628+4
	mov	r8, r5
	mov	r7, r5
	ldrh	r9, [sl, #156]
	b	.L1615
.L1617:
	mov	r0, r6
	bl	FlashTestBlk
	cmp	r0, #0
	beq	.L1616
	mov	r1, r6
	ldr	r0, .L1628+20
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
.L1616:
	add	r8, r8, #1
	uxth	r8, r8
.L1615:
	add	r6, r8, r9
	ldrb	r3, [r4, #853]	@ zero_extendqisi2
	uxth	r6, r6
	cmp	r3, r6
	bhi	.L1617
	ldr	r6, [sp, #24]
	sub	r9, fp, #50
	ldr	sl, .L1628+4
	mov	r8, #1
	b	.L1618
.L1620:
	mov	r0, r6
	bl	FlashTestBlk
	cmp	r0, #0
	beq	.L1619
	mov	r1, r6
	ldr	r0, .L1628+20
	bl	printk
	ldr	r3, [sl, #148]
	mov	r2, r6, lsr #5
	and	r0, r6, #31
	ldr	r1, [r3, r2, asl #2]
	orr	r1, r1, r8, asl r0
	str	r1, [r3, r2, asl #2]
.L1619:
	sub	r6, r6, #1
	uxth	r6, r6
.L1618:
	cmp	r6, r9
	bgt	.L1620
	ldr	r3, .L1628+4
	ldrb	r1, [r4, #853]	@ zero_extendqisi2
	ldr	r2, [r3, #156]
	rsb	r2, r2, r1
	cmp	r7, r2
	bcc	.L1614
	movw	r2, #3068
	ldr	r0, [r3, #148]
	ldrh	r2, [r4, r2]
	mov	r1, #0
	mov	r2, r2, asl #9
	bl	ftl_memset
.L1614:
	mul	r8, fp, r5
	ldr	r7, .L1628+4
	ldr	r6, [sp, #24]
	ldr	r9, [sp, #28]
	add	sl, r7, r5, asl #1
.L1621:
	ldr	r0, .L1628+24
	mov	r1, r5
	mov	r2, r6
	bl	printk
	ldr	r3, [r7, #148]
	b	.L1622
.L1623:
	sub	r6, r6, #1
	uxth	r6, r6
.L1622:
	mov	r1, r6, lsr #5
	and	r2, r6, #31
	ldr	r1, [r3, r1, asl #2]
	mov	r2, r1, lsr r2
	ands	r2, r2, #1
	bne	.L1623
	ldr	r3, .L1628+28
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
	bne	.L1621
.L1603:
	add	r5, r5, #1
	uxtb	r5, r5
.L1602:
	ldrb	r3, [r4, #3762]	@ zero_extendqisi2
	cmp	r3, r5
	bhi	.L1624
	add	sp, sp, #76
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, sl, fp, pc}
.L1629:
	.align	2
.L1628:
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
	ldr	r6, [r0, #12]
	ldr	r5, [r0, #24]
	bl	ftl_free_no_use_map_blk
	ldrh	ip, [r4, #10]
	ldrh	r2, [r4, #8]
	sub	r3, ip, #4
	cmp	r2, r3
	bge	.L1631
	ldrh	r2, [r4, #40]
	movw	r3, #65535
	cmp	r2, r3
	beq	.L1632
	ldr	r1, .L1645
	movw	r3, #3918
	ldrh	r2, [r4, #2]
	ldrh	r3, [r1, r3]
	cmp	r2, r3
	bcc	.L1632
.L1631:
	ldrh	r1, [r4, #40]
	movw	r3, #65535
	uxth	r7, r0
	cmp	r1, r3
	beq	.L1633
	ldr	r0, .L1645
	movw	r2, #3918
	ldrh	r3, [r4, #2]
	ldrh	r2, [r0, r2]
	cmp	r2, r3
	movls	r0, r6
	movls	r2, #0
	bhi	.L1633
	b	.L1634
.L1636:
	ldrh	lr, [r0], #2
	cmp	lr, r1
	beq	.L1644
	add	r2, r2, #1
	uxth	r2, r2
.L1634:
	cmp	r2, ip
	bne	.L1636
	b	.L1635
.L1644:
	mov	r7, r2
.L1635:
	mov	r2, r2, asl #1
	ldr	r0, .L1645+4
	ldrh	r2, [r6, r2]
	bl	printk
	mvn	r3, #0
	strh	r3, [r4, #40]	@ movhi
.L1633:
	mov	r7, r7, asl #1
	ldrh	r8, [r6, r7]
	cmp	r8, #0
	beq	.L1632
	ldr	r3, [r4, #32]
	cmp	r3, #0
	bne	.L1632
	mov	r2, #1
	ldr	r1, .L1645
	str	r2, [r4, #32]
	strh	r3, [r6, r7]	@ movhi
	ldrh	r3, [r4, #8]
	ldrh	r2, [r4, #2]
	sub	r3, r3, #1
	strh	r3, [r4, #8]	@ movhi
	movw	r3, #3918
	ldrh	r3, [r1, r3]
	cmp	r2, r3
	bcc	.L1637
	mov	r0, r4
	bl	ftl_map_blk_alloc_new_blk
.L1637:
	mov	r6, #0
	ldr	r7, .L1645+8
	b	.L1638
.L1643:
	ldr	r3, [r5, r6, asl #2]
	add	r9, r5, r6, asl #2
	cmp	r8, r3, lsr #10
	bne	.L1639
	ldr	r3, [r7, #-956]
	mov	r1, #1
	ldr	sl, [r7, #-936]
	mov	r2, r1
	ldr	r0, .L1645+12
	str	r3, [r7, #188]
	str	sl, [r7, #192]
	ldr	r3, [r5, r6, asl #2]
	str	r3, [r7, #184]
	bl	FlashReadPages
	ldr	r3, [r7, #180]
	cmn	r3, #1
	beq	.L1640
	ldrh	r3, [sl, #8]
	cmp	r3, r6
	beq	.L1641
.L1640:
	mov	r3, #0
	str	r3, [r9, #0]
	ldr	r3, .L1645+8
	ldrh	r2, [sl, #8]
	ldr	r0, .L1645+16
	ldr	r1, [r3, #184]
	bl	printk
	ldr	r3, .L1645
	mov	r2, #1
	str	r2, [r3, #4040]
	b	.L1642
.L1641:
	mov	r0, r4
	mov	r1, r6
	ldr	r2, [r7, #188]
	bl	FtlMapWritePage
.L1639:
	add	r6, r6, #1
	uxth	r6, r6
.L1638:
	ldrh	r3, [r4, #6]
	cmp	r3, r6
	bhi	.L1643
	mov	r0, r8
	mov	r1, #1
	bl	FtlFreeSysBlkQueueIn
	mov	r3, #0
	str	r3, [r4, #32]
.L1632:
	ldr	r1, .L1645
	movw	r3, #3918
	ldrh	r2, [r4, #2]
	ldrh	r3, [r1, r3]
	cmp	r2, r3
	bcc	.L1642
	mov	r0, r4
	bl	ftl_map_blk_alloc_new_blk
.L1642:
	mov	r0, #0
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, sl, pc}
.L1646:
	.align	2
.L1645:
	.word	.LANCHOR0
	.word	.LC123
	.word	.LANCHOR2
	.word	.LANCHOR2+180
	.word	.LC124
	.fnend
	.size	ftl_map_blk_gc, .-ftl_map_blk_gc
	.align	2
	.global	Ftl_write_map_blk_to_last_page
	.type	Ftl_write_map_blk_to_last_page, %function
Ftl_write_map_blk_to_last_page:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	ldr	r3, .L1653
	stmfd	sp!, {r4, r5, r6, r7, r8, lr}
	.save {r4, r5, r6, r7, r8, lr}
	mov	r4, r0
	ldr	r6, [r3, #4040]
	ldr	r5, [r0, #12]
	cmp	r6, #0
	ldr	r8, [r0, #24]
	bne	.L1648
	ldrh	r2, [r0, #0]
	movw	r1, #65535
	cmp	r2, r1
	bne	.L1649
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
	b	.L1648
.L1649:
	mov	r2, r2, asl #1
	ldr	r1, [r0, #28]
	ldrh	r7, [r5, r2]
	ldr	r5, .L1653+4
	ldrh	r2, [r0, #2]
	orr	r2, r2, r7, asl #10
	str	r2, [r5, #184]
	ldr	r2, [r5, #-960]
	str	r2, [r5, #188]
	ldr	r2, [r5, #-936]
	str	r2, [r5, #192]
	str	r1, [r2, #4]
	ldr	r1, .L1653+8
	strh	r1, [r2, #8]	@ movhi
	ldrh	r1, [r0, #4]
	strh	r7, [r2, #2]	@ movhi
	strh	r1, [r2, #0]	@ movhi
	movw	r2, #3918
	ldrh	r2, [r3, r2]
	mov	r1, #255
	ldr	r0, [r5, #-960]
	mov	r2, r2, asl #3
	bl	ftl_memset
	mov	r3, r6
	b	.L1650
.L1652:
	ldr	r2, [r8, r3, asl #2]
	cmp	r7, r2, lsr #10
	bne	.L1651
	add	r6, r6, #1
	ldr	r2, [r5, #-960]
	uxth	r6, r6
	str	r3, [r2, r6, asl #3]
	ldr	r2, [r5, #-960]
	ldr	r1, [r8, r3, asl #2]
	add	r2, r2, r6, asl #3
	str	r1, [r2, #4]
.L1651:
	add	r3, r3, #1
	uxth	r3, r3
.L1650:
	ldrh	r2, [r4, #6]
	cmp	r2, r3
	bhi	.L1652
	mov	r1, #1
	mov	r3, #0
	ldr	r0, .L1653+12
	mov	r2, r1
	bl	FlashProgPages
	ldrh	r3, [r4, #2]
	mov	r0, r4
	add	r3, r3, #1
	strh	r3, [r4, #2]	@ movhi
	bl	ftl_map_blk_gc
.L1648:
	mov	r0, #0
	ldmfd	sp!, {r4, r5, r6, r7, r8, pc}
.L1654:
	.align	2
.L1653:
	.word	.LANCHOR0
	.word	.LANCHOR2
	.word	-1291
	.word	.LANCHOR2+180
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
	ldr	r8, .L1667
	mov	sl, r1
	ldr	r5, .L1667+4
	mov	r9, r2
	mov	r6, #0
	mov	fp, r8
.L1666:
	ldr	r3, [r5, #-1820]
	add	r3, r3, #1
	str	r3, [r5, #-1820]
	movw	r3, #3918
	ldrh	r2, [r4, #2]
	ldrh	r3, [r8, r3]
	sub	r3, r3, #1
	cmp	r2, r3
	bge	.L1657
	ldrh	r2, [r4, #0]
	movw	r3, #65535
	cmp	r2, r3
	bne	.L1658
.L1657:
	mov	r0, r4
	bl	Ftl_write_map_blk_to_last_page
.L1658:
	ldr	r1, [fp, #4040]
	cmp	r1, #0
	bne	.L1659
	ldrh	r3, [r4, #0]
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
	ldr	r0, .L1667+8
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
	bne	.L1660
	ldr	r0, .L1667+12
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
	bls	.L1666
	ldr	r3, .L1667+4
	mov	r2, r6
	ldr	r0, .L1667+16
	ldr	r1, [r3, #184]
	bl	printk
	ldr	r3, .L1667
	mov	r2, #1
	str	r2, [r3, #4040]
	b	.L1659
.L1660:
	cmp	r3, #0
	strneh	r7, [r4, #40]	@ movhi
	cmp	r2, #1
	beq	.L1666
	cmp	r3, #256
	beq	.L1666
	ldr	r3, .L1667+4
	ldr	r2, [r3, #184]
	ldr	r3, [r4, #24]
	str	r2, [r3, sl, asl #2]
.L1659:
	mov	r0, #0
	ldmfd	sp!, {r3, r4, r5, r6, r7, r8, r9, sl, fp, pc}
.L1668:
	.align	2
.L1667:
	.word	.LANCHOR0
	.word	.LANCHOR2
	.word	.LANCHOR2+180
	.word	.LC125
	.word	.LC126
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
	ldr	r5, .L1670
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
.L1671:
	.align	2
.L1670:
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
	ldr	r3, .L1676
	ldr	r1, [r3, #4040]
	cmp	r1, #0
	ldmnefd	sp!, {r4, r5, r6, pc}
	ldr	r5, .L1676+4
	sub	r6, r6, #1
	uxth	r6, r6
	ldr	r0, [r5, #-956]
	ldr	r1, [r5, #-936]
	str	r0, [r5, #188]
	str	r1, [r5, #192]
	ldr	r2, [r2, r6, asl #2]
	cmp	r2, #0
	str	r2, [r5, #184]
	beq	.L1674
	mov	r1, #1
	add	r0, r5, #180
	mov	r2, r1
	bl	FlashReadPages
	b	.L1675
.L1674:
	movw	r2, #3926
	mov	r1, #255
	ldrh	r2, [r3, r2]
	bl	ftl_memset
.L1675:
	mov	r0, r4
	mov	r1, r6
	ldr	r2, [r5, #188]
	ldmfd	sp!, {r4, r5, r6, lr}
	b	FtlMapWritePage
.L1677:
	.align	2
.L1676:
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
	ldr	r3, .L1689
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
	bhi	.L1679
	mov	r7, r0, lsr r7
	mov	fp, #0
	ldr	r6, .L1689+4
	mov	r2, r7, asl #2
	str	r2, [sp, #0]
	b	.L1680
.L1686:
	ldr	r3, [r6, #-904]
	mov	r0, r8
	ldr	r2, [sp, #0]
	ldr	sl, [r3, r2]
	movw	r3, #3922
	ldr	r2, .L1689
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
	beq	.L1682
	ldr	r2, .L1689+4
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
	bne	.L1684
	mov	r1, r7
	mov	r2, sl
	ldr	r0, .L1689+8
	bl	printk
	ldr	r0, .L1689+12
	mov	r1, r7
	ldr	r2, [r6, #-952]
	bl	FtlMapWritePage
.L1684:
	ldr	r3, [sp, #4]
	mov	r0, r9
	ldr	r1, [r6, #-952]
	mov	r2, r4, asl #9
	add	r1, r1, r3, asl #9
	bl	memcpy
	b	.L1685
.L1682:
	mov	r0, r9
	mov	r1, sl
	mov	r2, r4, asl #9
	bl	ftl_memset
.L1685:
	ldr	r3, [sp, #0]
	add	r7, r7, #1
	rsb	r5, r4, r5
	add	r8, r8, r4
	add	r9, r9, r4, asl #9
	add	r3, r3, #4
	str	r3, [sp, #0]
.L1680:
	cmp	r5, #0
	bne	.L1686
.L1679:
	mov	r0, fp
	add	sp, sp, #52
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, sl, fp, pc}
.L1690:
	.align	2
.L1689:
	.word	.LANCHOR0
	.word	.LANCHOR2
	.word	.LC127
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
	ldr	r5, .L1695
	mov	r1, #1
	ldr	r2, .L1695+4
	bl	FtlVendorPartRead
	ldr	r4, .L1695+8
	ldr	r3, [r5, #-1708]
	cmp	r3, r4
	beq	.L1692
	ldr	r0, .L1695+4
	mov	r1, #0
	mov	r2, #512
	bl	ftl_memset
	str	r4, [r5, #-1708]
.L1692:
	ldr	r2, [r5, #-1708]
	ldr	r3, .L1695
	cmp	r2, r4
	bne	.L1693
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
	ldr	r2, .L1695+12
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
.L1693:
	ldr	r4, .L1695
	mov	r3, #0
	ldr	r5, .L1695+12
	str	r3, [r4, #-1000]
	ldr	r2, [r4, #-1640]
	ldr	r3, .L1695+16
	cmp	r2, r3
	bne	.L1694
	mov	r3, #1
	ldr	r0, .L1695+20
	str	r3, [r5, #3840]
	ldr	r1, .L1695+24
	bl	printk
.L1694:
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
.L1696:
	.align	2
.L1695:
	.word	.LANCHOR2
	.word	.LANCHOR2-1708
	.word	1179929683
	.word	.LANCHOR0
	.word	305432421
	.word	.LC78
	.word	.LC128
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
	ldr	r4, .L1699
	ldr	r5, .L1699+4
	ldr	r2, [r4, #-920]
	ldrh	r1, [r4, r5]
	bl	FtlVendorPartRead
	ldr	r3, [r4, #-920]
	ldr	r2, [r3, #0]
	ldr	r3, .L1699+8
	cmp	r2, r3
	beq	.L1698
	ldr	r1, .L1699+12
	ldr	r0, .L1699+16
	bl	printk
	ldrh	r2, [r4, r5]
	ldr	r0, [r4, #-920]
	mov	r1, #0
	mov	r2, r2, asl #9
	bl	ftl_memset
.L1698:
	mov	r0, #0
	ldmfd	sp!, {r3, r4, r5, pc}
.L1700:
	.align	2
.L1699:
	.word	.LANCHOR2
	.word	-924
	.word	1112818501
	.word	.LC129
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
	ldr	r3, .L1711
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
	bhi	.L1702
	mov	r8, r0, lsr r8
	mov	r7, #0
	ldr	sl, .L1711+4
	mov	ip, r8, asl #2
	str	ip, [sp, #8]
	b	.L1703
.L1708:
	ldr	r1, .L1711
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
	ldr	r2, .L1711+4
	uxth	r1, r1
	str	r1, [sp, #12]
	rsb	r5, r1, fp
	uxth	r5, r5
	cmp	r5, r4
	uxthhi	r5, r4
	cmp	r3, #0
	beq	.L1705
	cmp	r5, fp
	beq	.L1705
	str	r3, [sp, #24]
	mov	r1, #1
	ldr	r3, [r2, #-952]
	add	r0, sp, #20
	mov	r2, r1
	str	r3, [sp, #28]
	mov	r3, #0
	str	r3, [sp, #32]
	bl	FlashReadPages
	b	.L1706
.L1705:
	ldr	ip, .L1711
	movw	r3, #3926
	ldr	r0, [r2, #-952]
	mov	r1, #0
	ldrh	r2, [ip, r3]
	bl	ftl_memset
.L1706:
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
	ldr	r0, .L1711+8
	add	r8, r8, #1
	ldr	r2, [sl, #-952]
	add	r9, r9, fp
	bl	FtlMapWritePage
	ldr	ip, [sp, #8]
	add	ip, ip, #4
	str	ip, [sp, #8]
	cmn	r0, #1
	moveq	r7, r0
.L1703:
	cmp	r4, #0
	bne	.L1708
.L1702:
	mov	r0, r7
	add	sp, sp, #60
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, sl, fp, pc}
.L1712:
	.align	2
.L1711:
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
	ldr	r3, .L1715
	ldr	r2, .L1715+4
	ldr	r1, [r3, #-1708]
	cmp	r1, r2
	bxne	lr
	ldr	r2, .L1715+8
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
	ldr	r2, .L1715+12
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
	ldr	r2, [r3, #-1000]
	str	r2, [r3, #-1644]
	ldr	r2, .L1715+16
	b	FtlVendorPartWrite
.L1716:
	.align	2
.L1715:
	.word	.LANCHOR2
	.word	1179929683
	.word	1342177352
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
	ldr	r3, .L1724
	ldr	r1, .L1724+4
	ldr	r3, [r3, #3840]
	cmp	r3, #0
	moveq	r2, #32
	beq	.L1718
	ldr	r2, [r1, #-1780]
	cmp	r2, #29
	movls	r2, #4
	movhi	r2, #32
.L1718:
	mov	r3, #292
	ldrh	r1, [r1, r3]
	cmp	r1, #31
	addls	r1, r1, #1
	ldrls	r2, .L1724+4
	strlsh	r1, [r2, r3]	@ movhi
	movls	r2, #1
	cmp	r0, #0
	ldr	r3, .L1724+4
	bne	.L1720
	ldr	r1, [r3, #-920]
	ldr	r0, [r1, #20]
	ldr	r1, [r1, #16]
	add	r2, r2, r0
	cmp	r1, r2
	bcc	.L1721
.L1720:
	ldr	r2, [r3, #-920]
	mov	r0, #64
	ldr	r1, [r2, #16]
	str	r1, [r2, #20]
	ldr	r1, .L1724+8
	str	r1, [r2, #0]
	ldr	r1, .L1724+12
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
.L1721:
	mov	r0, #0
	ldmfd	sp!, {r3, pc}
.L1725:
	.align	2
.L1724:
	.word	.LANCHOR0
	.word	.LANCHOR2
	.word	1112818501
	.word	-924
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
	ldr	r3, .L1747
	str	fp, [r4, #32]
	str	fp, [r4, #28]
	ldr	r2, [r3, #-960]
	ldr	r5, [r3, #-936]
	str	r2, [r3, #188]
	str	r5, [r3, #192]
	mvn	r3, #0
	strh	r3, [r4, #0]	@ movhi
	strh	r3, [r4, #2]	@ movhi
	mov	r3, #1
	str	r3, [r4, #36]
	ldr	r3, [sp, #8]
	sub	r3, r3, #1
	str	r3, [sp, #12]
	ldr	r3, .L1747+4
	b	.L1727
.L1741:
	ldr	r0, [sp, #12]
	cmp	r2, r0
	mov	r2, r2, asl #1
	bne	.L1728
	ldrh	r0, [r9, r2]
	mov	r1, #1
	add	ip, r9, r2
	str	ip, [sp, #4]
	bl	FtlGetLastWrittenPage
	ldr	r1, [sp, #12]
	ldr	r2, [sp, #16]
	mov	r7, sl
	strh	fp, [r4, #0]	@ movhi
	mov	sl, #0
	ldr	ip, [sp, #4]
	ldr	r6, .L1747
	mov	fp, ip
	uxth	r0, r0
	add	r3, r0, #1
	strh	r3, [r4, #2]	@ movhi
	ldr	r3, [r2, r1, asl #2]
	sxth	r9, r0
	add	r9, r9, #1
	str	r3, [r4, #28]
	b	.L1729
.L1732:
	ldrh	r2, [fp, #0]
	mov	r1, #1
	ldr	r0, .L1747+8
	orr	r3, r3, r2, asl #10
	mov	r2, r1
	str	r3, [r6, #184]
	bl	FlashReadPages
	ldr	r3, [r6, #180]
	cmn	r3, #1
	ldreqh	r3, [fp, #0]
	streqh	r3, [r4, #40]	@ movhi
	beq	.L1731
	ldrh	r3, [r5, #8]
	cmp	r3, r8
	bcs	.L1731
	ldrh	r2, [r4, #4]
	ldrh	r1, [r5, #0]
	cmp	r1, r2
	ldreq	r2, [r6, #184]
	streq	r2, [r7, r3, asl #2]
.L1731:
	add	sl, sl, #1
	uxth	sl, sl
.L1729:
	sxth	r3, sl
	cmp	r3, r9
	blt	.L1732
	b	.L1733
.L1728:
	ldr	r6, .L1747
	movw	r7, #3918
	add	r0, r9, r2
	str	r0, [sp, #20]
	add	r0, r6, #180
	ldr	r1, [r6, #-960]
	str	r1, [r6, #188]
	ldrh	r1, [r9, r2]
	ldrh	r2, [r3, r7]
	sub	r2, r2, #1
	orr	r2, r2, r1, asl #10
	mov	r1, #1
	str	r2, [r6, #184]
	mov	r2, r1
	str	r3, [sp, #4]
	bl	FlashReadPages
	ldr	r2, [r6, #180]
	ldr	r3, [sp, #4]
	cmn	r2, #1
	beq	.L1744
	ldrh	r1, [r5, #0]
	ldrh	r2, [r4, #4]
	cmp	r1, r2
	bne	.L1744
	ldrh	r1, [r5, #8]
	movw	r2, #64245
	cmp	r1, r2
	bne	.L1744
	b	.L1745
.L1737:
	ldr	r0, [r6, #-960]
	mov	ip, r1, asl #3
	add	r2, r2, #1
	ldr	r1, [r0, r1, asl #3]
	uxth	r2, r2
	uxth	r1, r1
	cmp	r1, r8
	addcc	r0, r0, ip
	ldrcc	r0, [r0, #4]
	strcc	r0, [sl, r1, asl #2]
	b	.L1735
.L1745:
	mov	r2, #0
.L1735:
	ldrh	r0, [r3, r7]
	sxth	r1, r2
	sub	r0, r0, #1
	cmp	r1, r0
	blt	.L1737
	b	.L1738
.L1740:
	ldr	r0, [sp, #20]
	ldrh	r1, [r0, #0]
	ldr	r0, .L1747+8
	orr	r2, r2, r1, asl #10
	mov	r1, #1
	str	r2, [r7, #184]
	mov	r2, r1
	str	r3, [sp, #4]
	bl	FlashReadPages
	ldr	r2, [r7, #180]
	ldr	r3, [sp, #4]
	cmn	r2, #1
	beq	.L1739
	ldrh	r2, [r5, #8]
	cmp	r2, r8
	bcs	.L1739
	ldrh	r1, [r4, #4]
	ldrh	r0, [r5, #0]
	cmp	r0, r1
	ldreq	r1, [r7, #184]
	streq	r1, [sl, r2, asl #2]
.L1739:
	add	r6, r6, #1
	uxth	r6, r6
	b	.L1746
.L1744:
	ldr	r7, .L1747
	mov	r6, #0
.L1746:
	movw	r1, #3918
	sxth	r2, r6
	ldrh	r1, [r3, r1]
	cmp	r2, r1
	blt	.L1740
.L1738:
	add	fp, fp, #1
	uxth	fp, fp
.L1727:
	ldr	r1, [sp, #8]
	sxth	r2, fp
	cmp	r2, r1
	blt	.L1741
.L1733:
	mov	r0, r4
	bl	ftl_free_no_use_map_blk
	ldr	r1, .L1747+4
	movw	r3, #3918
	ldrh	r2, [r4, #2]
	ldrh	r3, [r1, r3]
	cmp	r2, r3
	bne	.L1742
	mov	r0, r4
	bl	ftl_map_blk_alloc_new_blk
.L1742:
	mov	r0, r4
	bl	ftl_map_blk_gc
	mov	r0, r4
	bl	ftl_map_blk_gc
	mov	r0, #0
	add	sp, sp, #28
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, sl, fp, pc}
.L1748:
	.align	2
.L1747:
	.word	.LANCHOR2
	.word	.LANCHOR0
	.word	.LANCHOR2+180
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
	ldr	r0, .L1750
	bl	FtlMapTblRecovery
	mov	r0, #0
	ldmfd	sp!, {r3, pc}
.L1751:
	.align	2
.L1750:
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
	ldr	r3, .L1753
	ldr	r0, .L1753+4
	ldrh	r1, [r3, r2]
	movw	r2, #258
	strh	r1, [r0, r2]	@ movhi
	ldr	r2, .L1753+8
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
.L1754:
	.align	2
.L1753:
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
	ldr	r9, .L1764
	movw	r3, #3060
	ldr	r4, .L1764+4
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
	b	.L1756
.L1762:
	ldr	r5, [sp, #4]
	sub	r3, r8, #12
	mul	fp, r8, r4
	ldr	sl, .L1764+4
	b	.L1757
.L1760:
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
	beq	.L1758
	ldr	r2, [sl, #228]
	ldrh	r1, [r2, #0]
	movw	r2, #61664
	cmp	r1, r2
	bne	.L1758
	ldr	r0, .L1764+8
	mov	r1, r4
	mov	r2, r5
	add	r6, r6, #1
	bl	printk
	ldr	r3, .L1764+4
	uxth	r6, r6
	add	r3, r3, r4, asl #1
	strh	r5, [r3, #232]	@ movhi
	b	.L1759
.L1758:
	sub	r5, r5, #1
	uxth	r5, r5
.L1757:
	cmp	r5, r3
	bgt	.L1760
.L1759:
	ldrb	r3, [r9, #3762]	@ zero_extendqisi2
	add	r4, r4, #1
	cmp	r3, r6
	uxtb	r4, r4
	moveq	r7, #0
.L1756:
	ldrb	r3, [r9, #3762]	@ zero_extendqisi2
	cmp	r3, r4
	bhi	.L1762
	mov	r0, r7
	add	sp, sp, #52
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, sl, fp, pc}
.L1765:
	.align	2
.L1764:
	.word	.LANCHOR0
	.word	.LANCHOR2
	.word	.LC130
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
	ldr	r5, .L1779
	.pad #36
	sub	sp, sp, #36
	ldrh	r6, [r5, r3]
	b	.L1767
.L1769:
	add	r3, r5, r7
	mov	r1, r6
	ldrb	r0, [r3, #3874]	@ zero_extendqisi2
	bl	V2P_block
	str	r0, [sp, #28]
	bl	FtlBbmIsBadBlock
	ldr	r3, [sp, #28]
	cmp	r0, #0
	bne	.L1768
	ldr	r2, [r8, #-972]
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
.L1768:
	add	r7, r7, #1
	uxth	r7, r7
	b	.L1772
.L1777:
	ldr	r8, .L1779+4
	mov	r4, #0
	mov	r7, r4
	movw	fp, #3848
	mov	r9, #36
	movw	sl, #3928
.L1772:
	ldrh	r3, [r5, fp]
	cmp	r3, r7
	bhi	.L1769
	cmp	r4, #0
	beq	.L1770
	ldr	sl, .L1779+4
	mov	r1, r4
	mov	r2, #1
	mov	r7, #0
	mov	r8, r7
	ldr	r0, [sl, #-972]
	bl	FlashReadPages
.L1771:
	ldr	r1, [sl, #-972]
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
	ldr	r0, .L1779+8
	str	r1, [sp, #16]
	ubfx	r1, r2, #10, #16
	ldr	r3, [r3, #0]
	bl	printk
	cmp	r8, r4
	bne	.L1771
.L1770:
	add	r6, r6, #1
	uxth	r6, r6
.L1767:
	movw	r2, #3858
	ldrh	r3, [r5, r2]
	cmp	r3, r6
	bhi	.L1777
	mov	r5, #0
	ldr	r4, .L1779+4
	ldr	sl, .L1779+12
	b	.L1773
.L1774:
	ldr	r2, [r4, #-1040]
	mov	r1, #1
	ldr	r0, .L1779+16
	add	r6, r6, #1
	ldrh	r2, [r2, r7]
	uxth	r6, r6
	orr	r3, r3, r2, asl #10
	mov	r2, r1
	str	r3, [r4, #184]
	bl	FlashReadPages
	ldr	r3, [r4, #192]
	ldr	r1, [r4, #-1040]
	ldr	r2, [r4, #188]
	ldr	r0, [r3, #0]
	ldrh	r1, [r1, r7]
	str	r0, [sp, #0]
	ldr	r0, [r3, #4]
	str	r0, [sp, #4]
	ldr	r0, [r3, #8]
	str	r0, [sp, #8]
	ldr	r3, [r3, #12]
	ldr	r0, .L1779+20
	str	r3, [sp, #12]
	ldr	r3, [r2, #0]
	str	r3, [sp, #16]
	ldr	r3, [r2, #4]
	str	r3, [sp, #20]
	ldr	r2, [r4, #180]
	ldr	r3, [r4, #184]
	bl	printk
.L1776:
	movw	r2, #3918
	sxth	r3, r6
	ldrh	r2, [r8, r2]
	cmp	r3, r2
	blt	.L1774
	add	r5, r5, #1
	uxth	r5, r5
.L1773:
	ldrh	r3, [r4, sl]
	sxth	r7, r5
	ldr	r6, .L1779+4
	cmp	r7, r3
	movlt	r7, r7, asl #1
	movlt	r6, #0
	ldrlt	r8, .L1779
	blt	.L1776
.L1775:
	ldr	r4, .L1779
	mov	r5, #3952
	ldr	r1, [r6, #-1040]
	mov	r2, #2
	ldr	r0, .L1779+24
	ldr	r3, [r4, #3944]
	bl	rknand_print_hex
	ldr	r1, [r6, #-1032]
	ldrh	r3, [r4, r5]
	mov	r2, #4
	ldr	r0, .L1779+28
	bl	rknand_print_hex
	ldr	r0, .L1779+32
	ldr	r1, [r6, #-900]
	mov	r2, #4
	ldrh	r3, [r4, r5]
	add	sp, sp, #36
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, sl, fp, lr}
	b	rknand_print_hex
.L1780:
	.align	2
.L1779:
	.word	.LANCHOR0
	.word	.LANCHOR2
	.word	.LC131
	.word	-1044
	.word	.LANCHOR2+180
	.word	.LC107
	.word	.LC132
	.word	.LC133
	.word	.LC134
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
	ldr	r4, .L1785
	.pad #24
	sub	sp, sp, #24
	mov	r7, r0, asl #10
	mov	r5, #0
	ldr	r8, .L1785+4
	ldr	r3, [r4, #-960]
	str	r3, [r4, #188]
	ldr	r3, [r4, #-936]
	str	r3, [r4, #192]
	b	.L1782
.L1784:
	mov	r1, #1
	ldr	r0, .L1785+8
	mov	r2, r1
	orr	r3, r3, r7
	str	r3, [r4, #184]
	bl	FlashReadPages
	ldr	r3, [r4, #192]
	mov	r1, r6
	ldr	r0, .L1785+12
	ldr	r2, [r3, #0]
	str	r2, [sp, #0]
	ldr	r2, [r3, #4]
	str	r2, [sp, #4]
	ldr	r2, [r3, #8]
	str	r2, [sp, #8]
	ldr	r3, [r3, #12]
	ldr	r2, [r4, #180]
	str	r3, [sp, #12]
	ldr	r3, [r4, #188]
	ldr	r3, [r3, #0]
	str	r3, [sp, #16]
	ldr	r3, [r4, #184]
	bl	printk
	ldr	r3, [r4, #192]
	ldr	r3, [r3, #0]
	cmn	r3, #1
	beq	.L1783
	ldr	r0, .L1785+16
	mov	r2, #4
	ldr	r1, [r4, #-960]
	mov	r3, #768
	bl	rknand_print_hex
.L1783:
	add	r5, r5, #1
	uxth	r5, r5
.L1782:
	movw	r2, #3918
	sxth	r3, r5
	ldrh	r2, [r8, r2]
	cmp	r3, r2
	blt	.L1784
	add	sp, sp, #24
	ldmfd	sp!, {r4, r5, r6, r7, r8, pc}
.L1786:
	.align	2
.L1785:
	.word	.LANCHOR2
	.word	.LANCHOR0
	.word	.LANCHOR2+180
	.word	.LC135
	.word	.LC136
	.fnend
	.size	FtlDumpSysBlock, .-FtlDumpSysBlock
	.align	2
	.global	FlashReadFacBbtData
	.type	FlashReadFacBbtData, %function
FlashReadFacBbtData:
	.fnstart
	@ args = 0, pretend = 0, frame = 40
	@ frame_needed = 0, uses_anonymous_args = 0
	ldr	r3, .L1797
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
	ldr	r7, .L1797+4
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
	b	.L1788
.L1794:
	mov	r1, #1
	add	r3, r5, sl
	add	r0, sp, #4
	mov	r2, r1
	mov	r3, r3, asl #10
	str	r3, [sp, #8]
	bl	FlashReadPages
	ldr	r3, [sp, #4]
	cmn	r3, #1
	beq	.L1789
	ldr	r3, [r7, #228]
	ldrh	r2, [r3, #0]
	movw	r3, #61664
	cmp	r2, r3
	bne	.L1789
	cmp	r4, #0
	moveq	r0, r4
	beq	.L1790
	cmp	r6, #0
	ldreq	r3, .L1797+4
	moveq	r0, #1
	beq	.L1791
	b	.L1792
.L1793:
	ldr	r2, [r3, #148]
	ubfx	r1, r6, #5, #16
	and	lr, r6, #31
	add	r6, r6, #1
	ldr	ip, [r2, r1, asl #2]
	uxth	r6, r6
	orr	ip, ip, r0, asl lr
	str	ip, [r2, r1, asl #2]
.L1791:
	ldr	r2, [r3, #156]
	cmp	r6, r2
	bcc	.L1793
.L1792:
	ldr	r3, .L1797+4
	mov	r2, r9
	mov	r0, r4
	ldr	r1, [r3, #148]
	bl	memcpy
	mov	r2, #4
	ldr	r0, .L1797+8
	mov	r1, r4
	mov	r3, r2
	bl	rknand_print_hex
	mov	r0, #0
	b	.L1790
.L1789:
	sub	r5, r5, #1
	uxth	r5, r5
.L1788:
	cmp	r5, r8
	bgt	.L1794
	mvn	r0, #0
.L1790:
	add	sp, sp, #40
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, sl, pc}
.L1798:
	.align	2
.L1797:
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
	ldr	r3, .L1806
	stmfd	sp!, {r4, r5, r6, r7, r8, lr}
	.save {r4, r5, r6, r7, r8, lr}
	mov	r5, r0
	ldr	r3, [r3, #3624]
	ldr	r6, .L1806+4
	ldrb	r2, [r3, #13]	@ zero_extendqisi2
	ldrh	r4, [r3, #14]
	ldr	r0, [r6, #136]
	mul	r4, r4, r2
	uxth	r4, r4
	add	r2, r4, #7
	mov	r2, r2, lsr #3
	bl	FlashReadFacBbtData
	cmn	r0, #1
	beq	.L1805
	mov	r2, #0
	mov	r0, r4, lsr #4
	mov	r3, r2
	sub	r4, r4, #1
	mov	r1, #1
	b	.L1801
.L1803:
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
	bcs	.L1805
	add	r2, r2, #1
	uxth	r2, r2
.L1801:
	cmp	r2, r4
	blt	.L1803
	b	.L1800
.L1805:
	mov	r3, #0
.L1800:
	mov	r3, r3, asl #1
	mvn	r2, #0
	mov	r0, #0
	strh	r2, [r5, r3]	@ movhi
	ldmfd	sp!, {r4, r5, r6, r7, r8, pc}
.L1807:
	.align	2
.L1806:
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
	ldr	r6, .L1826
	ldr	r5, [r6, #4040]
	cmp	r5, #0
	bne	.L1809
	ldr	r8, .L1826+4
	bl	FtlBbtMemInit
	ldr	r4, .L1826+8
	bl	FtlLoadFactoryBbt
	add	sl, r8, #12
	b	.L1810
.L1816:
	ldrh	r3, [sl], #2
	movw	r2, #65535
	ldr	r0, [r4, #-960]
	movw	fp, #3912
	ldr	r9, [r4, #-936]
	cmp	r3, r2
	str	r0, [r4, #188]
	str	r9, [r4, #192]
	beq	.L1811
	ldrh	r7, [r6, fp]
	mov	r1, #1
	mov	r2, r1
	ldr	r0, .L1826+12
	mla	r7, r7, r5, r3
	mov	r3, r7, asl #10
	str	r3, [r4, #184]
	bl	FlashReadPages
	ldrh	r2, [r6, fp]
	ldr	r0, [r8, #28]
	add	r2, r2, #7
	ldr	r1, [r4, #188]
	mov	r2, r2, lsr #3
	bl	memcpy
	b	.L1812
.L1811:
	mov	r1, r5
	bl	FlashGetBadBlockList
	ldr	r0, [r4, #188]
	ldr	r1, [r8, #28]
	bl	FtlBbt2Bitmap
	ldrh	fp, [r6, fp]
.L1814:
	sub	fp, fp, #1
	uxth	fp, fp
.L1825:
	movw	r7, #3912
	ldrh	r0, [r6, r7]
	mla	r0, r0, r5, fp
	uxth	r0, r0
	bl	FtlBbmIsBadBlock
	cmp	r0, #1
	beq	.L1814
	mov	r1, #0
	mov	r2, #16
	strh	fp, [sl, #-2]	@ movhi
	ldr	r0, [r4, #-936]
	bl	ftl_memset
	ldr	r3, .L1826+16
	strh	r3, [r9, #0]	@ movhi
	mov	r3, #0
	str	r3, [r9, #4]
	ldrh	r3, [sl, #-2]
	ldrh	r7, [r6, r7]
	strh	r3, [r9, #2]	@ movhi
	ldrh	r3, [sl, #-2]
	ldr	r1, [r8, #28]
	ldr	r0, [r4, #188]
	mla	r7, r7, r5, r3
	mov	r3, r7, asl #10
	str	r3, [r4, #184]
	ldr	r3, .L1826+20
	ldrh	r2, [r4, r3]
	mov	r2, r2, asl #2
	bl	memcpy
	mov	r1, #1
	mov	r2, r1
	ldr	r0, .L1826+12
	bl	FlashEraseBlocks
	mov	r1, #1
	mov	r3, r1
	ldr	r0, .L1826+12
	mov	r2, r1
	bl	FlashProgPages
	ldr	r3, [r4, #180]
	cmn	r3, #1
	bne	.L1812
	uxth	r0, r7
	bl	FtlBbmMapBadBlock
	b	.L1825
.L1812:
	uxth	r0, r7
	add	r5, r5, #1
	bl	FtlBbmMapBadBlock
	add	r8, r8, #4
.L1810:
	movw	r3, #3870
	ldrh	r3, [r6, r3]
	cmp	r5, r3
	bcc	.L1816
	mov	r4, #0
	ldr	r7, .L1826
	movw	r6, #3930
	b	.L1817
.L1818:
	mov	r0, r4
	add	r4, r4, #1
	bl	FtlBbmMapBadBlock
	uxth	r4, r4
.L1817:
	ldrh	r3, [r7, r6]
	ldr	r5, .L1826
	cmp	r3, r4
	bhi	.L1818
	movw	r7, #3992
	movw	r6, #3980
	ldrh	r4, [r5, r7]
	sub	r4, r4, #1
	uxth	r4, r4
	b	.L1819
.L1824:
	mov	r0, r4
	bl	FtlBbmIsBadBlock
	cmp	r0, #1
	beq	.L1820
	mov	r0, r4
	bl	FlashTestBlk
	cmp	r0, #0
	beq	.L1821
	mov	r0, r4
	bl	FtlBbmMapBadBlock
	b	.L1820
.L1821:
	ldrh	r2, [r5, r6]
	movw	r3, #65535
	cmp	r2, r3
	streqh	r4, [r5, r6]	@ movhi
.L1822:
	ldrne	r2, .L1826
	movne	r3, #3984
	strneh	r4, [r2, r3]	@ movhi
	bne	.L1823
.L1820:
	sub	r4, r4, #1
	uxth	r4, r4
.L1819:
	ldrh	r3, [r5, r7]
	sub	r3, r3, #48
	cmp	r4, r3
	bgt	.L1824
.L1823:
	ldr	r4, .L1826
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
.L1809:
	mov	r0, #0
	ldmfd	sp!, {r3, r4, r5, r6, r7, r8, r9, sl, fp, pc}
.L1827:
	.align	2
.L1826:
	.word	.LANCHOR0
	.word	.LANCHOR0+3980
	.word	.LANCHOR2
	.word	.LANCHOR2+180
	.word	-3872
	.word	-1028
	.fnend
	.size	FtlMakeBbt, .-FtlMakeBbt
	.align	2
	.type	ftl_load_l2p_region, %function
ftl_load_l2p_region:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	stmfd	sp!, {r4, r5, r6, r7, r8, lr}
	.save {r4, r5, r6, r7, r8, lr}
	mov	r4, r0
	ldr	r6, .L1833
	mov	r5, #12
	ldr	r3, [r6, #-1032]
	ldr	r7, [r3, r0, asl #2]
	cmp	r7, #0
	bne	.L1829
	mul	r5, r5, r1
	ldr	r3, [r6, #-1852]
	ldr	r2, .L1833+4
	mov	r1, #255
	add	r3, r3, r5
	ldr	r0, [r3, #8]
	movw	r3, #3926
	ldrh	r2, [r2, r3]
	bl	ftl_memset
	ldr	r3, [r6, #-1852]
	strh	r4, [r3, r5]	@ movhi
	ldr	r3, [r6, #-1852]
	add	r5, r3, r5
	str	r7, [r5, #4]
	b	.L1830
.L1829:
	mul	r5, r5, r1
	ldr	r3, [r6, #-1852]
	mov	r1, #1
	str	r7, [r6, #184]
	add	r0, r6, #180
	mov	r2, r1
	add	r3, r3, r5
	ldr	r3, [r3, #8]
	str	r3, [r6, #188]
	ldr	r3, [r6, #-936]
	str	r3, [r6, #192]
	bl	FlashReadPages
	ldr	r3, [r6, #180]
	ldr	r8, [r6, #192]
	cmp	r3, #256
	bne	.L1831
	mov	r1, r4
	mov	r2, r7
	ldr	r0, .L1833+8
	bl	printk
	ldr	r3, .L1833+12
	mov	r2, r7, lsr #10
	sub	r0, r6, #1088
	mov	r1, r4
	strh	r2, [r6, r3]	@ movhi
	ldr	r3, [r6, #-1852]
	add	r3, r3, r5
	ldr	r2, [r3, #8]
	bl	FtlMapWritePage
.L1831:
	ldrh	r3, [r8, #8]
	cmp	r3, r4
	beq	.L1832
	mov	r2, r7
	ldr	r7, .L1833
	mov	r1, r4
	ldr	r0, .L1833+16
	bl	printk
	ldr	r6, .L1833+4
	mov	r2, #4
	ldr	r0, .L1833+20
	mov	r3, r2
	ldr	r1, [r7, #192]
	bl	rknand_print_hex
	mov	r3, #3952
	ldrh	r3, [r6, r3]
	mov	r2, #4
	ldr	r0, .L1833+24
	ldr	r1, [r7, #-1032]
	bl	rknand_print_hex
	mov	r3, #1
	str	r3, [r6, #4040]
.L1832:
	ldr	r3, .L1833
	mov	r1, #0
	ldr	r3, [r3, #-1852]
	add	r2, r3, r5
	str	r1, [r2, #4]
	strh	r4, [r3, r5]	@ movhi
.L1830:
	mov	r0, #0
	ldmfd	sp!, {r4, r5, r6, r7, r8, pc}
.L1834:
	.align	2
.L1833:
	.word	.LANCHOR2
	.word	.LANCHOR0
	.word	.LC138
	.word	-1048
	.word	.LC139
	.word	.LC98
	.word	.LC140
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
	ldr	r3, .L1844
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
	ldr	r3, .L1844+4
	uxth	r7, r7
	ldr	sl, [r3, #-1852]
	mov	r3, #0
	mov	r5, r3
	b	.L1836
.L1842:
	add	r3, r3, #12
	add	r1, sl, r3
	ldrh	r1, [r1, #-12]
	cmp	r1, r6
	bne	.L1837
.L1838:
	cmp	r8, #0
	ldr	r3, .L1844+4
	mov	r2, #12
	bne	.L1839
	ldr	r3, [r3, #-1852]
	mla	r2, r2, r5, r3
	ldr	r3, [r2, #8]
	ldr	r3, [r3, r7, asl #2]
	str	r3, [r4, #0]
	b	.L1840
.L1839:
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
	ldr	r2, .L1844+8
	strh	r6, [r3, r2]	@ movhi
.L1840:
	ldr	r3, .L1844+4
	mov	r2, #12
	mov	r0, #0
	ldr	r3, [r3, #-1852]
	mla	r5, r2, r5, r3
	ldr	r3, [r5, #4]
	cmn	r3, #1
	addne	r3, r3, #1
	strne	r3, [r5, #4]
	ldmfd	sp!, {r3, r4, r5, r6, r7, r8, sl, pc}
.L1837:
	add	r5, r5, #1
	uxth	r5, r5
.L1836:
	cmp	r5, r2
	bne	.L1842
	bl	select_l2p_ram_region
	mov	r3, #12
	mul	r3, r3, r0
	mov	r5, r0
	add	r2, sl, r3
	ldrh	r1, [sl, r3]
	movw	r3, #65535
	cmp	r1, r3
	beq	.L1843
	ldr	r3, [r2, #4]
	cmp	r3, #0
	bge	.L1843
	bl	flush_l2p_region
.L1843:
	mov	r0, r6
	mov	r1, r5
	bl	ftl_load_l2p_region
	b	.L1838
.L1845:
	.align	2
.L1844:
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
	ldr	r4, .L1854
	ldr	r6, .L1854+4
	ldr	r2, [r4, #-1624]
	ldr	r3, [r6, #3976]
	cmp	r2, r3
	bcs	.L1847
	mov	r5, #2048
.L1850:
	ldr	r0, [r4, #-1624]
	ldr	r3, [r6, #3976]
	cmp	r0, r3
	bcs	.L1852
	mov	r1, sp
	mov	r2, #0
	bl	log2phys
	ldr	r0, [sp, #0]
	ldr	r3, [r4, #-1624]
	cmn	r0, #1
	add	r3, r3, #1
	str	r3, [r4, #-1624]
	beq	.L1849
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
	bne	.L1852
	ldr	r0, [sp, #0]
	ubfx	r0, r0, #10, #16
	bl	P2V_block_in_plane
	bl	FtlGcRefreshBlock
	b	.L1852
.L1849:
	subs	r5, r5, #1
	bne	.L1850
	b	.L1848
.L1847:
	ldr	r3, [r4, #-1836]
	mov	r0, #0
	str	r0, [r4, #-1628]
	str	r0, [r4, #-1624]
	str	r3, [r4, #-1632]
	b	.L1848
.L1852:
	mvn	r0, #0
.L1848:
	add	sp, sp, #40
	ldmfd	sp!, {r4, r5, r6, pc}
.L1855:
	.align	2
.L1854:
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
	ldr	r3, .L1863
	ldr	r2, [r3, #-1628]
	cmp	r2, #0
	beq	.L1857
	b	FtlReadRefresh.part.15
.L1857:
	ldr	r2, .L1863+4
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
	bhi	.L1859
	add	r1, r0, r1
	cmp	r1, r2
	bcc	.L1859
	ldr	r3, .L1863
	ldrb	r3, [r3, #-2044]	@ zero_extendqisi2
	cmp	r3, #0
	bne	.L1860
.L1859:
	ldr	r3, .L1863
	mov	r1, #1
	str	r2, [r3, #-1632]
	str	r1, [r3, #-1628]
	mov	r1, #0
	str	r1, [r3, #-1624]
.L1860:
	mov	r0, #0
	bx	lr
.L1864:
	.align	2
.L1863:
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
	ldr	r3, .L1871
	ldr	r1, [r3, #-2016]
	mov	r5, r0, asl #1
	ldrh	r2, [r1, r5]
	cmp	r2, #0
	addne	r2, r2, #1
	strneh	r2, [r1, r5]	@ movhi
	bne	.L1867
	ldr	r4, [r3, #-2004]
	cmp	r4, #0
	beq	.L1867
	ldr	r1, .L1871+4
	mov	ip, #6
	ldrh	lr, [r3, r1]
	ldr	r3, [r3, #-2024]
	ldr	r1, .L1871+8
	rsb	r4, r3, r4
	mov	r4, r4, asr #1
	mul	r4, r1, r4
	movw	r1, #65535
	uxth	r4, r4
	b	.L1868
.L1870:
	cmp	r4, r0
	bne	.L1869
	mov	r1, r4
	ldr	r0, .L1871+12
	bl	List_remove_node
	ldr	r6, .L1871
	ldr	r3, .L1871+4
	mov	r0, r4
	ldrh	r2, [r6, r3]
	sub	r2, r2, #1
	strh	r2, [r6, r3]	@ movhi
	bl	INSERT_DATA_LIST
	ldr	r3, [r6, #-2016]
	ldrh	r2, [r3, r5]
	add	r2, r2, #1
	strh	r2, [r3, r5]	@ movhi
	b	.L1867
.L1869:
	mul	r4, ip, r4
	ldrh	r4, [r3, r4]
	cmp	r4, r1
	beq	.L1867
	add	r2, r2, #1
	uxth	r2, r2
.L1868:
	cmp	r2, lr
	bne	.L1870
.L1867:
	mov	r0, r7
	add	r1, sp, #4
	mov	r2, #1
	bl	log2phys
	ldmfd	sp!, {r1, r2, r3, r4, r5, r6, r7, pc}
.L1872:
	.align	2
.L1871:
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
	ldr	r4, .L1879
	.pad #32
	sub	sp, sp, #32
	ldr	r0, .L1879+4
	mov	r5, #0
	bl	printk
	ldr	r7, .L1879+8
	mov	r6, r4
	b	.L1874
.L1878:
	mov	r0, r5
	add	r1, sp, #28
	mov	r2, #0
	bl	log2phys
	movs	r3, r5, asl #21
	bne	.L1875
	ldr	r0, .L1879+12
	mov	r1, r5
	ldr	r2, [sp, #28]
	bl	printk
.L1875:
	ldr	r3, [sp, #28]
	cmn	r3, #1
	beq	.L1876
	str	r3, [r4, #184]
	mov	r2, #0
	ldr	r3, [r4, #-960]
	mov	r1, #1
	ldr	r8, [r4, #-936]
	ldr	r0, .L1879+16
	str	r3, [r4, #188]
	str	r5, [r4, #196]
	str	r8, [r4, #192]
	str	r2, [r4, #180]
	bl	FlashReadPages
	ldr	r3, [r4, #180]
	cmn	r3, #1
	cmpne	r3, #256
	beq	.L1877
	ldr	r3, [r8, #8]
	cmp	r3, r5
	beq	.L1876
.L1877:
	ldr	r3, [r6, #192]
	ldr	r2, [r6, #188]
	ldr	r0, .L1879+20
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
	ldr	r2, [r6, #184]
	ldr	r3, [r3, #0]
	bl	printk
.L1876:
	add	r5, r5, #1
.L1874:
	ldr	r3, [r7, #3976]
	cmp	r5, r3
	bcc	.L1878
	add	sp, sp, #32
	ldmfd	sp!, {r4, r5, r6, r7, r8, pc}
.L1880:
	.align	2
.L1879:
	.word	.LANCHOR2
	.word	.LC141
	.word	.LANCHOR0
	.word	.LC142
	.word	.LANCHOR2+180
	.word	.LC143
	.fnend
	.size	Ftlscanalldata, .-Ftlscanalldata
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
	beq	.L1882
	ldrh	r3, [r0, #2]
	ldr	r1, .L1971
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
	bne	.L1884
	b	.L1968
.L1885:
	add	r3, r3, #1
	uxth	r3, r3
	add	r1, r4, r3, asl #1
	ldrh	r0, [r1, #16]
.L1884:
	cmp	r0, r2
	beq	.L1885
	ldrb	r1, [r4, #8]	@ zero_extendqisi2
	cmp	r1, #1
	bne	.L1886
	bl	FtlGetLastWrittenPage
	cmn	r0, #1
	mov	r7, r0
	beq	.L1887
	ldr	r3, .L1971
	ldrb	r2, [r3, #928]	@ zero_extendqisi2
	cmp	r2, #0
	bne	.L1961
	add	r3, r3, r0, asl #1
	movw	r2, #3076
	ldrh	r8, [r3, r2]
	b	.L1953
.L1886:
	mov	r1, #0
	bl	FtlGetLastWrittenPage
	cmn	r0, #1
	mov	r7, r0
	beq	.L1887
.L1961:
	mov	r8, r7
.L1953:
	ldr	r3, .L1971
	movw	r2, #3848
	mov	r5, #0
	str	r4, [sp, #32]
	movw	ip, #65535
	ldrh	r9, [r3, r2]
	ldr	r2, .L1971+4
	ldr	sl, [r2, #-972]
	ldr	r6, [r2, #-1168]
	movw	r2, #3928
	ldrh	lr, [r3, r2]
	mov	r2, r4
	mov	r3, r5
	b	.L1889
.L1887:
	mov	r3, #0
	strh	r3, [r4, #2]	@ movhi
	mov	r2, r3	@ movhi
.L1968:
	strb	r2, [r4, #6]
	b	.L1882
.L1891:
	ldrh	r0, [r2, #16]
	cmp	r0, ip
	beq	.L1890
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
.L1890:
	add	r3, r3, #1
	add	r2, r2, #2
	uxth	r3, r3
.L1889:
	cmp	r3, r9
	bne	.L1891
	ldrb	r3, [r4, #8]	@ zero_extendqisi2
	cmp	r3, #1
	movne	r3, #0
	bne	.L1962
	ldr	r3, .L1971
	ldrb	r3, [r3, #928]	@ zero_extendqisi2
	adds	r3, r3, #0
	movne	r3, #1
.L1962:
	ldr	r6, .L1971+4
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
	b	.L1893
.L1898:
	mov	lr, #36
	ldr	r1, [r6, #-972]
	mul	r2, lr, sl
	add	r3, r1, r2
	ldr	r2, [r1, r2]
	cmp	r2, #0
	bne	.L1894
	ldr	r3, [r3, #12]
	ldr	fp, [r3, #4]
	cmn	fp, #1
	beq	.L1895
	ldr	r1, [r6, #-1800]
	mov	r0, fp
	str	r3, [sp, #0]
	bl	ftl_cmp_data_ver
	ldr	r3, [sp, #0]
	cmp	r0, #0
	addne	fp, fp, #1
	strne	fp, [r6, #-1800]
.L1895:
	ldr	r3, [r3, #0]
	cmn	r3, #1
	bne	.L1897
	b	.L1896
.L1894:
	ldr	r1, [r3, #4]
	movw	fp, #294
	ldr	r0, .L1971+8
	bl	printk
	ldrh	r3, [r4, #0]
	uxth	r2, r8
	str	r2, [sp, #4]
	strh	r3, [r6, fp]	@ movhi
.L1897:
	add	sl, sl, #1
.L1893:
	uxth	r9, sl
	cmp	r9, r5
	bcc	.L1898
.L1896:
	ldr	r3, .L1971+4
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
	ldr	r3, .L1971
	cmp	r2, #1
	str	r0, [sp, #16]
	bne	.L1901
	ldrb	r1, [r3, #928]	@ zero_extendqisi2
	cmp	r1, #0
	addeq	r7, r3, r7, asl #1
	movweq	r1, #3076
	ldreqh	r7, [r7, r1]
.L1901:
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
	beq	.L1969
.L1903:
	ldr	fp, [sp, #4]
	movw	r3, #65535
	ldr	ip, [sp, #8]
	cmp	fp, r3
	sub	r6, ip, #1
	bne	.L1904
	cmp	r2, #0
	bne	.L1905
.L1904:
	ldr	r2, .L1971+4
	uxth	r3, r8
	ldr	ip, [sp, #12]
	mvn	fp, #0
	mov	sl, r3
	mov	r9, fp
	ldr	r1, [r2, #-1008]
	ldr	r8, .L1971+4
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
	b	.L1908
.L1910:
	ldrh	r0, [r1, #16]
	movw	lr, #65535
	cmp	r0, lr
	beq	.L1909
	ldr	ip, [sp, #8]
	mov	r3, #36
	orr	r0, r5, r0, asl #10
	mla	lr, r3, r6, ip
	add	r6, r6, #1
	uxth	r6, r6
	str	r0, [lr, #4]
.L1909:
	add	r2, r2, #1
	add	r1, r1, #2
	uxth	r2, r2
.L1921:
	ldr	lr, [sp, #36]
	cmp	r2, lr
	bne	.L1910
	ldr	r3, [sp, #28]
	mov	r1, r6
	ldr	r0, [r8, #-972]
	ldr	r2, [sp, #20]
	str	r3, [sp, #0]
	bl	FlashReadPages
	ldr	r2, .L1971
	add	r0, r8, r5, asl #1
	mov	r1, #0
	str	r0, [sp, #8]
	ldr	r3, [sp, #0]
	ldrb	ip, [r2, #928]	@ zero_extendqisi2
	ldr	r2, [r8, #-972]
	str	ip, [sp, #28]
	b	.L1911
.L1919:
	ldr	r0, [r2, #0]
	cmp	r0, #0
	bne	.L1912
	ldr	r0, [r2, #12]
	movw	ip, #65535
	ldrh	lr, [r0, #0]
	cmp	lr, ip
	beq	.L1913
	ldr	r0, [r0, #4]
	cmn	r0, #1
	beq	.L1913
	cmn	fp, #1
	ldr	r9, [r8, #-1008]
	str	r0, [r8, #-1008]
	bne	.L1913
	ldr	lr, [sp, #8]
	sub	r0, lr, #892
	ldrh	r0, [r0, #0]
	cmp	r0, ip
	bne	.L1914
	ldr	r0, [sp, #28]
	cmp	r0, #0
	beq	.L1913
.L1914:
	cmp	r9, r7
	mvneq	fp, #0
	movne	fp, r9
	b	.L1913
.L1912:
	mov	r6, r7
	ldrh	r1, [r4, #0]
	mov	r7, r3
	ldr	r3, .L1971+4
	movw	r2, #294
	strh	r1, [r3, r2]	@ movhi
	ldrb	r2, [r4, #8]	@ zero_extendqisi2
	cmp	r2, #0
	bne	.L1905
	add	r5, r3, r5, asl #1
	movw	r2, #65535
	sub	r5, r5, #892
	ldrh	r1, [r5, #0]
	cmp	r1, r2
	bne	.L1915
	cmn	fp, #1
	bne	.L1964
.L1916:
	ldr	fp, [sp, #4]
	cmp	fp, r6
	beq	.L1917
.L1964:
	str	fp, [r3, #-1008]
	b	.L1905
.L1917:
	ldr	r2, [r3, #-1008]
	b	.L1970
.L1915:
	cmp	r9, r6
	beq	.L1918
	cmn	r9, #1
	strne	r9, [r3, #-1008]
	b	.L1905
.L1918:
	ldr	r2, [r3, #-1008]
	cmp	r2, r6
	beq	.L1905
.L1970:
	sub	r2, r2, #1
	b	.L1965
.L1913:
	add	r1, r1, #1
	add	r2, r2, #36
	uxth	r1, r1
.L1911:
	cmp	r1, r6
	bne	.L1919
	add	r5, r5, #1
	uxth	r5, r5
.L1908:
	cmp	r5, sl
	bhi	.L1920
	ldr	r1, .L1971
	movw	r2, #3848
	ldr	lr, [r8, #-972]
	mov	r6, #0
	str	r3, [sp, #28]
	ldrh	ip, [r1, r2]
	mov	r1, r4
	str	lr, [sp, #8]
	mov	r2, r6
	str	ip, [sp, #36]
	b	.L1921
.L1920:
	mov	r6, r7
	mov	r7, r3
	ldr	r3, .L1971+4
	mvn	r2, #0
.L1965:
	str	r2, [r3, #-1008]
.L1905:
	ldr	r5, .L1971+4
	mov	r3, #296
	mov	r2, #1
	sub	r0, r5, #1088
	strh	r2, [r5, r3]	@ movhi
	bl	FtlMapBlkWriteDumpData
	ldr	r8, [sp, #12]
	str	r7, [sp, #8]
.L1922:
	ldr	r3, .L1971
	movw	r2, #3848
	mov	r7, #0
	ldr	lr, [r5, #-972]
	mov	fp, #36
	ldrh	sl, [r3, r2]
	mov	r2, r4
	ldrb	ip, [r3, #928]	@ zero_extendqisi2
	mov	r3, r7
	b	.L1923
.L1926:
	ldrh	r0, [r2, #16]
	movw	r1, #65535
	cmp	r0, r1
	beq	.L1924
	mla	r1, fp, r7, lr
	orr	r0, r8, r0, asl #10
	str	r0, [r1, #4]
	ldrb	r9, [r4, #8]	@ zero_extendqisi2
	cmp	r9, #1
	bne	.L1925
	cmp	ip, #0
	orrne	r0, r0, #-2147483648
	strne	r0, [r1, #4]
.L1925:
	add	r7, r7, #1
	uxth	r7, r7
.L1924:
	add	r3, r3, #1
	add	r2, r2, #2
	uxth	r3, r3
.L1923:
	cmp	r3, sl
	bne	.L1926
	mov	r1, r7
	ldr	r0, [r5, #-972]
	ldr	r2, [sp, #20]
	bl	FlashReadPages
	mov	r3, #36
	mul	r3, r3, r7
	mov	r7, #0
	str	r3, [sp, #36]
	b	.L1927
.L1948:
	ldr	r9, [r5, #-972]
	add	r9, r9, r7
	ldr	sl, [r9, #4]
	ubfx	r0, sl, #10, #16
	str	sl, [sp, #52]
	bl	P2V_plane
	ldr	r3, [sp, #12]
	cmp	r8, r3
	bcc	.L1928
	ldr	fp, [sp, #24]
	mov	ip, r3
	cmp	r0, fp
	movcs	r3, #0
	movcc	r3, #1
	cmp	r8, ip
	movne	r3, #0
	cmp	r3, #0
	bne	.L1928
	ldr	r3, [sp, #16]
	ldr	ip, [sp, #8]
	cmp	r0, r3
	cmpeq	r8, ip
	beq	.L1960
	ldr	r3, [r9, #0]
	cmn	r3, #1
	beq	.L1930
	ldr	sl, [r9, #12]
	movw	r3, #61589
	ldrh	r2, [sl, #0]
	cmp	r2, r3
	bne	.L1937
.L1931:
	ldr	r6, [sl, #4]
	cmn	r6, #1
	beq	.L1932
	ldr	r1, [r5, #-1800]
	mov	r0, r6
	bl	ftl_cmp_data_ver
	cmp	r0, #0
	addne	r3, r6, #1
	strne	r3, [r5, #-1800]
.L1932:
	ldr	r9, [sl, #8]
	add	r1, sp, #48
	ldr	r3, [sl, #12]
	mov	r2, #0
	mov	r0, r9
	str	r3, [sp, #44]
	bl	log2phys
	ldr	r1, [r5, #-1008]
	cmn	r1, #1
	beq	.L1933
	mov	r0, r6
	bl	ftl_cmp_data_ver
	cmp	r0, #0
	beq	.L1933
	ldr	r3, [sp, #44]
	cmn	r3, #1
	beq	.L1934
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
	bne	.L1935
	b	.L1936
.L1934:
	ldr	r3, [sp, #52]
	ldr	r2, [sp, #48]
	cmp	r2, r3
	bne	.L1937
	mov	r0, r9
	add	r1, sp, #44
	mov	r2, #1
	bl	log2phys
.L1937:
	ldrh	r0, [r4, #0]
	b	.L1967
.L1935:
	ldr	r3, [sp, #4]
	ldr	sl, [r3, #8]
	cmp	sl, r9
	bne	.L1936
	ldr	r0, [r5, #-1008]
	ldr	r1, [sp, #28]
	bl	ftl_cmp_data_ver
	cmp	r0, #0
	beq	.L1936
	ldr	r3, [sp, #48]
	ldr	r2, [sp, #52]
	cmp	r3, r2
	beq	.L1941
.L1938:
	ldr	r2, [sp, #44]
	cmp	r3, r2
	beq	.L1936
	cmn	r3, #1
	streq	r3, [fp, #0]
	beq	.L1940
	ldr	ip, [fp, #12]
	mov	r0, fp
	str	r3, [fp, #4]
	mov	r1, #1
	mov	r2, #0
	str	ip, [sp, #4]
	bl	FlashReadPages
.L1940:
	ldr	r3, [r5, #-972]
	ldr	r3, [r3, r7]
	cmn	r3, #1
	beq	.L1941
	ldr	r3, [sp, #4]
	ldr	r0, [r5, #-1008]
	ldr	r9, [r3, #4]
	mov	r1, r9
	bl	ftl_cmp_data_ver
	cmp	r0, #0
	beq	.L1941
	ldr	r0, [sp, #28]
	mov	r1, r9
	bl	ftl_cmp_data_ver
	cmp	r0, #0
	beq	.L1936
.L1941:
	mov	r0, sl
	ldr	r1, [sp, #44]
	bl	FtlReUsePrevPpa
.L1936:
	ldrh	r0, [r4, #0]
	mvn	r3, #0
	str	r3, [sp, #44]
	bl	decrement_vpc_count
	b	.L1943
.L1933:
	ldr	r3, [sp, #52]
	ldr	r2, [sp, #48]
	cmp	r2, r3
	beq	.L1943
	mov	r0, r9
	add	r1, sp, #52
	mov	r2, #1
	bl	log2phys
	ldr	fp, [sp, #48]
	cmn	fp, #1
	beq	.L1943
	ldr	r3, [sp, #44]
	cmp	fp, r3
	beq	.L1943
	ubfx	r0, fp, #10, #16
	bl	P2V_block_in_plane
	ldr	r3, .L1971+12
	ldrh	r3, [r5, r3]
	cmp	r3, r0
	beq	.L1944
	ldr	r3, .L1971+16
	ldrh	r3, [r5, r3]
	cmp	r3, r0
	beq	.L1944
	ldr	r3, .L1971+20
	ldrh	r3, [r5, r3]
	cmp	r3, r0
	bne	.L1943
.L1944:
	ldr	ip, .L1971+4
	mov	r1, #1
	mov	r2, #0
	ldr	r0, [ip, #-972]
	str	fp, [r0, #4]
	ldr	sl, [r0, #12]
	bl	FlashReadPages
	ldr	r0, .L1971+4
	ldr	r1, [sl, #4]
	ldr	r3, [r0, #-972]
	ldr	r3, [r3, #0]
	cmn	r3, #1
	beq	.L1943
	mov	r0, r6
	bl	ftl_cmp_data_ver
	cmp	r0, #0
	bne	.L1943
	mov	r0, r9
	add	r1, sp, #48
	mov	r2, #1
	bl	log2phys
.L1943:
	ldr	r0, [sp, #44]
	cmn	r0, #1
	beq	.L1928
	ubfx	r0, r0, #10, #16
	bl	P2V_block_in_plane
	ldr	r2, [r5, #-2016]
	mov	r3, r0, asl #1
	mov	r1, r0
	ldrh	r3, [r2, r3]
	cmp	r3, #0
	beq	.L1945
.L1967:
	bl	decrement_vpc_count
	b	.L1928
.L1945:
	ldr	r0, .L1971+24
	bl	printk
	b	.L1928
.L1930:
	ldrh	r3, [r4, #0]
	movw	r1, #294
	mov	r2, r6
	ldr	r0, .L1971+28
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
	beq	.L1966
.L1947:
	cmp	r3, r6
	bls	.L1928
.L1966:
	str	r6, [r5, #-1008]
.L1928:
	add	r7, r7, #36
.L1927:
	ldr	r3, [sp, #36]
	cmp	r7, r3
	bne	.L1948
	ldrb	r3, [r4, #8]	@ zero_extendqisi2
	add	r8, r8, #1
	cmp	r3, #1
	uxth	r8, r8
	bne	.L1949
	ldr	r3, .L1971
	ldrb	r2, [r3, #928]	@ zero_extendqisi2
	cmp	r2, #0
	beq	.L1949
	movw	r2, #3918
	ldr	ip, [sp, #8]
	ldrh	r3, [r3, r2]
	cmp	ip, r8
	cmpeq	r3, r8
	beq	.L1960
.L1949:
	ldr	r3, .L1971
	movw	r2, #3916
	ldrh	r2, [r3, r2]
	cmp	r8, r2
	bne	.L1922
	mov	r2, #0
	movw	r1, #3848
	strh	r2, [r4, #4]	@ movhi
	ldrh	r1, [r3, r1]
	mov	r3, r2
	strh	r8, [r4, #2]	@ movhi
	movw	r2, #65535
	ldr	r0, [sp, #32]
	b	.L1950
.L1952:
	add	r0, r0, #2
	ldrh	ip, [r0, #14]
	cmp	ip, r2
	strneb	r3, [r4, #6]
	bne	.L1882
.L1951:
	add	r3, r3, #1
	uxth	r3, r3
.L1950:
	cmp	r3, r1
	bne	.L1952
	b	.L1882
.L1960:
.L1929:
	ldr	r7, [sp, #8]
	ldr	r3, [sp, #16]
	strh	r7, [r4, #2]	@ movhi
	strb	r3, [r4, #6]
.L1969:
	mov	r0, r4
	mov	r1, r7
	mov	r2, r3
	bl	ftl_sb_update_avl_pages
.L1882:
	mov	r0, #0
	add	sp, sp, #60
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, sl, fp, pc}
.L1972:
	.align	2
.L1971:
	.word	.LANCHOR0
	.word	.LANCHOR2
	.word	.LC144
	.word	-1996
	.word	-1948
	.word	-1900
	.word	.LC145
	.word	.LC146
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
	ldr	r5, .L1982
	ldr	r3, [r5, #4040]
	cmp	r3, #0
	bne	.L1973
	ldr	r4, .L1982+4
	ldr	r3, .L1982+8
	mov	r2, r4
	ldrh	r3, [r4, r3]
	cmp	r3, #0
	beq	.L1975
	ldrb	r1, [r4, #-1988]	@ zero_extendqisi2
	cmp	r1, #0
	bne	.L1975
	movw	r0, #3916
	ldrb	r1, [r4, #-1989]	@ zero_extendqisi2
	ldrh	r0, [r5, r0]
	mul	r1, r0, r1
	cmp	r3, r1
	beq	.L1975
	ldrb	r8, [r4, #-1986]	@ zero_extendqisi2
	movw	r3, #3848
	ldr	r7, [r5, #3976]
	cmp	r8, #0
	ldrh	r6, [r5, r3]
	bne	.L1973
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
	beq	.L1976
	add	r0, sp, #4
	mov	r1, #1
	mov	r2, r8
	bl	FlashReadPages
	b	.L1977
.L1976:
	movw	r3, #3926
	mov	r1, #255
	ldrh	r2, [r5, r3]
	bl	ftl_memset
.L1977:
	ldr	r3, .L1982+12
	mov	r6, r6, asl #2
	ldr	r5, .L1982+4
	ldr	sl, .L1982+8
	strh	r3, [r4, #0]	@ movhi
	ldr	r8, .L1982+16
	b	.L1978
.L1981:
	ldrh	r3, [r5, sl]
	cmp	r3, #0
	beq	.L1979
	ldr	r3, [sp, #8]
	sub	r6, r6, #1
	str	r7, [r4, #8]
	ldr	r0, .L1982+20
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
.L1978:
	cmp	r6, #0
	bne	.L1981
.L1979:
	ldr	r3, .L1982+4
	mov	r2, #1
	strb	r2, [r3, #-1986]
	b	.L1973
.L1975:
	mov	r3, #0
	strb	r3, [r2, #-1986]
.L1973:
	add	sp, sp, #44
	ldmfd	sp!, {r4, r5, r6, r7, r8, sl, pc}
.L1983:
	.align	2
.L1982:
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
	ldr	r7, .L1988
	movw	r6, #3954
	ldr	r5, .L1988+4
	b	.L1985
.L1987:
	ldr	r3, [r5, #-1852]
	mov	r2, #12
	mla	r3, r2, r4, r3
	ldr	r3, [r3, #4]
	cmp	r3, #0
	bge	.L1986
	mov	r0, r4
	bl	flush_l2p_region
.L1986:
	add	r4, r4, #1
	uxth	r4, r4
.L1985:
	ldrh	r3, [r7, r6]
	cmp	r3, r4
	bhi	.L1987
	mov	r0, #0
	ldmfd	sp!, {r3, r4, r5, r6, r7, pc}
.L1989:
	.align	2
.L1988:
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
	ldr	r3, .L2002
	stmfd	sp!, {r4, r5, r6, lr}
	.save {r4, r5, r6, lr}
	mov	r4, r0
	ldr	r3, [r3, #4040]
	ldrh	r5, [r0, #0]
	cmp	r3, #0
	bne	.L1991
	movw	r3, #65535
	cmp	r5, r3
	beq	.L1992
	ldr	r3, .L2002+4
	mov	r0, r5
	ldr	r2, [r3, #-2016]
	mov	r3, r5, asl #1
	ldrh	r3, [r2, r3]
	cmp	r3, #0
	beq	.L1993
	bl	INSERT_DATA_LIST
	b	.L1992
.L1993:
	bl	INSERT_FREE_LIST
.L1992:
	mov	r3, #0
	strb	r3, [r4, #8]
	ldr	r3, .L2002+8
	cmp	r4, r3
	beq	.L1994
	ldr	r2, .L2002
	movw	r3, #3868
	ldrh	r3, [r2, r3]
	cmp	r3, #1
	beq	.L1994
	ldrb	r2, [r2, #928]	@ zero_extendqisi2
	cmp	r2, #0
	beq	.L1995
.L1994:
	mov	r3, #1
	strb	r3, [r4, #8]
	b	.L1996
.L1995:
	ldr	r2, .L2002+12
	cmp	r4, r2
	bne	.L1996
	cmp	r3, #3
	ldr	r3, .L2002+4
	beq	.L1997
	ldr	r2, [r3, #-1712]
	cmp	r2, #1
	bne	.L1998
.L1997:
	mov	r2, #1
	strb	r2, [r3, #-1988]
.L1998:
	ldr	r2, .L2002
	ldr	r2, [r2, #3840]
	cmp	r2, #0
	beq	.L1996
	ldr	r3, [r3, #-1780]
	cmp	r3, #29
	ldrls	r3, .L2002+4
	movls	r2, #1
	strlsb	r2, [r3, #-1988]
.L1996:
	ldr	r3, .L2002+16
	ldr	r6, .L2002+4
	ldrh	r0, [r6, r3]
	movw	r3, #65535
	cmp	r0, r3
	beq	.L1999
	cmp	r5, r0
	bne	.L2000
	ldr	r2, [r6, #-2016]
	mov	r3, r0, asl #1
	ldrh	r3, [r2, r3]
	cmp	r3, #0
	beq	.L2001
.L2000:
	bl	update_vpc_list
.L2001:
	ldr	r3, .L2002+16
	mvn	r2, #0
	strh	r2, [r6, r3]	@ movhi
.L1999:
	mov	r0, r4
	bl	allocate_data_superblock
	bl	l2p_flush
	mov	r0, #0
	bl	FtlEctTblFlush
	bl	FtlVpcTblFlush
.L1991:
	mov	r0, #0
	ldmfd	sp!, {r4, r5, r6, pc}
.L2003:
	.align	2
.L2002:
	.word	.LANCHOR0
	.word	.LANCHOR2
	.word	.LANCHOR2-1948
	.word	.LANCHOR2-1996
	.word	-1020
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
	ldr	r6, .L2011
	ldr	r1, .L2011+4
	ldr	r0, .L2011+8
	bl	printk
	movw	r3, #3858
	ldrh	r2, [r6, r3]
	mov	r1, #0
	ldr	r5, .L2011+12
	mov	r2, r2, asl #1
	ldr	r0, [r5, #-916]
	bl	ftl_memset
	b	.L2005
.L2007:
	mov	r0, r4
	add	r1, sp, #4
	mov	r2, #0
	bl	log2phys
	ldr	r0, [sp, #4]
	cmn	r0, #1
	beq	.L2006
	ubfx	r0, r0, #10, #16
	bl	P2V_block_in_plane
	ldr	r3, [r5, #-916]
	mov	r0, r0, asl #1
	ldrh	r2, [r3, r0]
	add	r2, r2, #1
	strh	r2, [r3, r0]	@ movhi
.L2006:
	add	r4, r4, #1
.L2005:
	ldr	r3, [r6, #3976]
	cmp	r4, r3
	bcc	.L2007
	mov	r4, #0
	ldr	r7, .L2011
	ldr	r5, .L2011+12
	movw	r8, #65535
	b	.L2008
.L2010:
	ldr	r3, [r5, #-2016]
	mov	r6, r4, asl #1
	ldrh	r2, [r3, r6]
	ldr	r3, [r5, #-916]
	ldrh	r3, [r3, r6]
	cmp	r2, r3
	beq	.L2009
	cmp	r2, r8
	beq	.L2009
	ldr	r0, .L2011+16
	mov	r1, r4
	bl	printk
	ldr	r3, .L2011+20
	ldrh	r3, [r5, r3]
	cmp	r3, r4
	beq	.L2009
	ldr	r3, .L2011+24
	ldrh	r3, [r5, r3]
	cmp	r3, r4
	beq	.L2009
	ldr	r3, .L2011+28
	ldrh	r3, [r5, r3]
	cmp	r3, r4
	beq	.L2009
	ldr	r3, [r5, #-916]
	mov	r0, r4
	ldrh	r2, [r3, r6]
	ldr	r3, [r5, #-2016]
	strh	r2, [r3, r6]	@ movhi
	bl	update_vpc_list
	bl	l2p_flush
	bl	FtlVpcTblFlush
.L2009:
	add	r4, r4, #1
	uxth	r4, r4
.L2008:
	mov	r3, #3856
	ldrh	r3, [r7, r3]
	cmp	r3, r4
	bhi	.L2010
	ldmfd	sp!, {r2, r3, r4, r5, r6, r7, r8, pc}
.L2012:
	.align	2
.L2011:
	.word	.LANCHOR0
	.word	.LANCHOR3+48
	.word	.LC101
	.word	.LANCHOR2
	.word	.LC147
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
	ldr	r3, .L2036
	stmfd	sp!, {r0, r1, r2, r4, r5, r6, r7, r8, sl, lr}
	.save {r0, r1, r2, r4, r5, r6, r7, r8, sl, lr}
	movw	r2, #3916
	ldrh	r1, [r3, r2]
	ldr	r2, [r3, #4040]
	cmp	r2, #0
	bne	.L2034
	ldr	ip, .L2036+4
	ldr	r3, .L2036+8
	ldrh	r4, [r3, ip]
	movw	ip, #65535
	cmp	r4, ip
	beq	.L2015
	cmp	r0, #0
	beq	.L2016
	ldr	lr, .L2036+12
	movw	r0, #3000
	ldrh	r5, [lr, r0]
	cmp	r5, ip
	movne	r1, #2
	bne	.L2016
	strh	r2, [lr, r0]	@ movhi
	sub	r2, r2, #2000
	ldrh	r3, [r3, r2]
	cmp	r3, #17
	movhi	r1, #2
.L2016:
	ldr	r0, .L2036+16
	bl	FtlGcScanTempBlk
	cmn	r0, #1
	str	r0, [sp, #4]
	beq	.L2017
	ldr	r3, .L2036
	mov	r4, r4, asl #1
	ldr	r3, [r3, #4048]
	ldrh	r2, [r3, r4]
	cmp	r2, #4
	bls	.L2018
	sub	r2, r2, #5
	mov	r0, #1
	strh	r2, [r3, r4]	@ movhi
	bl	FtlEctTblFlush
.L2018:
	ldr	r3, .L2036+8
	ldr	r2, [r3, #-1012]
	cmp	r2, #0
	bne	.L2019
	ldr	r2, [r3, #-1612]
	ldr	r0, [sp, #4]
	add	r2, r2, #1
	str	r2, [r3, #-1612]
	ubfx	r0, r0, #10, #16
	bl	FtlBbmMapBadBlock
	bl	FtlBbmTblFlush
.L2019:
	ldr	r3, .L2036+8
	mov	r2, #0
	str	r2, [r3, #-1012]
	b	.L2032
.L2017:
	ldr	r2, .L2036+12
	movw	r3, #3000
	ldrh	r2, [r2, r3]
	movw	r3, #65535
	cmp	r2, r3
	bne	.L2032
.L2015:
	ldr	r6, .L2036+8
	movw	r3, #65535
	ldr	r5, .L2036+4
	mov	r4, #0
	str	r4, [r6, #-1012]
	ldrh	r2, [r6, r5]
	cmp	r2, r3
	moveq	r0, r4
	beq	.L2014
	bl	FtlCacheWriteBack
	ldr	ip, .L2036
	movw	r0, #3916
	ldrb	r1, [r6, #-1893]	@ zero_extendqisi2
	ldrh	r3, [r6, r5]
	ldrh	r0, [ip, r0]
	ldr	r2, [r6, #-2016]
	ldr	sl, .L2036+20
	mov	r3, r3, asl #1
	mul	r1, r0, r1
	strh	r1, [r2, r3]	@ movhi
	ldr	r3, [r6, #-1816]
	ldrh	r2, [r6, sl]
	add	r3, r2, r3
	str	r3, [r6, #-1816]
	b	.L2020
.L2023:
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
	bne	.L2021
	ubfx	r0, r0, #10, #16
	bl	P2V_block_in_plane
	add	r1, r5, #4
	mov	r2, #1
	mov	r7, r0
	ldr	r0, [r5, #8]
	bl	log2phys
	mov	r0, r7
	b	.L2035
.L2021:
	ldr	r3, [r5, #4]
	cmp	r0, r3
	beq	.L2022
	ldr	r3, .L2036+4
	ldrh	r0, [r6, r3]
.L2035:
	bl	decrement_vpc_count
.L2022:
	add	r4, r4, #1
	uxth	r4, r4
.L2020:
	ldrh	r3, [r6, sl]
	ldr	r5, .L2036+8
	cmp	r3, r4
	bhi	.L2023
	movw	r0, #65535
	ldr	r4, .L2036+4
	bl	decrement_vpc_count
	ldr	r3, .L2036
	ldrb	r3, [r3, #928]	@ zero_extendqisi2
	cmp	r3, #0
	beq	.L2024
	ldr	r0, .L2036+24
	ldrh	r1, [r5, r4]
	bl	printk
.L2024:
	ldrh	r0, [r5, r4]
	ldr	r2, [r5, #-2016]
	mov	r3, r0, asl #1
	ldrh	r3, [r2, r3]
	cmp	r3, #0
	beq	.L2025
	bl	INSERT_DATA_LIST
	b	.L2026
.L2025:
	bl	INSERT_FREE_LIST
.L2026:
	ldr	r4, .L2036+8
	mvn	r2, #0
	ldr	r3, .L2036+4
	strh	r2, [r4, r3]	@ movhi
	mov	r3, #0
	ldr	r2, .L2036+20
	strh	r3, [r4, r2]	@ movhi
	sub	r2, r2, #12
	strh	r3, [r4, r2]	@ movhi
	bl	l2p_flush
	bl	FtlVpcTblFlush
	ldr	r3, .L2036
	mov	r1, r4
	ldr	r3, [r3, #3840]
	cmp	r3, #0
	beq	.L2027
	ldr	r3, [r4, #-1780]
	cmp	r3, #29
	bhi	.L2027
	ldr	r3, .L2036+28
	mvn	r1, #0
	ldr	r2, .L2036+32
	ldrh	r3, [r4, r3]
	ldrh	r2, [r4, r2]
	cmp	r2, r3
	movcc	r3, r3, asl #1
	ldrcc	r2, .L2036+36
	strcch	r3, [r4, r2]	@ movhi
	ldr	r3, .L2036+40
	ldr	r2, .L2036+8
	strh	r1, [r2, r3]	@ movhi
	b	.L2034
.L2027:
	ldr	r2, .L2036+28
	ldr	r0, .L2036+32
	ldr	r3, .L2036+8
	ldrh	r2, [r1, r2]
	ldrh	r1, [r1, r0]
	add	r0, r2, r2, asl #1
	cmp	r1, r0, lsr #2
	ble	.L2034
	ldr	r1, .L2036+40
	mvn	r0, #0
	strh	r0, [r3, r1]	@ movhi
	ldr	r1, .L2036
	ldrb	r0, [r1, #928]	@ zero_extendqisi2
	ldr	r1, .L2036+36
	cmp	r0, #0
	subne	r2, r2, #2
	moveq	r2, #20
	strneh	r2, [r3, r1]	@ movhi
	streqh	r2, [r3, r1]	@ movhi
	beq	.L2014
	b	.L2034
.L2032:
	mov	r0, #1
	b	.L2014
.L2034:
	mov	r0, #0
.L2014:
	ldmfd	sp!, {r1, r2, r3, r4, r5, r6, r7, r8, sl, pc}
.L2037:
	.align	2
.L2036:
	.word	.LANCHOR0
	.word	-1900
	.word	.LANCHOR2
	.word	.LANCHOR1
	.word	.LANCHOR2-1900
	.word	-1144
	.word	.LC148
	.word	-1776
	.word	-2000
	.word	-1196
	.word	-1760
	.fnend
	.size	FtlGcFreeTempBlock, .-FtlGcFreeTempBlock
	.align	2
	.type	Ftl_gc_temp_data_write_back.part.22, %function
Ftl_gc_temp_data_write_back.part.22:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	stmfd	sp!, {r3, r4, r5, r6, r7, lr}
	.save {r3, r4, r5, r6, r7, lr}
	mov	r2, #0
	ldr	r4, .L2044
	mov	r3, r2
	mov	r5, #0
	mov	r6, #36
	ldr	r0, [r4, #-968]
	ldr	r1, [r4, #-1180]
	bl	FlashProgPages
	b	.L2039
.L2042:
	mul	r3, r6, r5
	ldr	r1, [r4, #-968]
	add	r2, r1, r3
	ldr	r1, [r1, r3]
	cmn	r1, #1
	bne	.L2040
	ldr	r2, .L2044+4
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
	mov	r0, #1
	ldmfd	sp!, {r3, r4, r5, r6, r7, pc}
.L2040:
	ldr	r3, [r2, #12]
	add	r5, r5, #1
	ldr	r1, [r2, #4]
	uxth	r5, r5
	ldr	r0, [r3, #12]
	ldr	r2, [r3, #8]
	bl	FtlGcUpdatePage
.L2039:
	ldr	r1, [r4, #-1180]
	ldr	r7, .L2044
	cmp	r5, r1
	bcc	.L2042
	ldr	r0, [r7, #-968]
	bl	FtlGcBufFree
	ldr	r3, .L2044+8
	mov	r0, #0
	str	r0, [r7, #-1180]
	ldrh	r3, [r7, r3]
	cmp	r3, r0
	ldmnefd	sp!, {r3, r4, r5, r6, r7, pc}
	mov	r0, #1
	bl	FtlGcFreeTempBlock
	mov	r0, #1
	ldmfd	sp!, {r3, r4, r5, r6, r7, pc}
.L2045:
	.align	2
.L2044:
	.word	.LANCHOR2
	.word	-1900
	.word	-1896
	.fnend
	.size	Ftl_gc_temp_data_write_back.part.22, .-Ftl_gc_temp_data_write_back.part.22
	.align	2
	.global	Ftl_gc_temp_data_write_back
	.type	Ftl_gc_temp_data_write_back, %function
Ftl_gc_temp_data_write_back:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	@ link register save eliminated.
	ldr	r3, .L2051
	ldr	r2, [r3, #4040]
	cmp	r2, #0
	bne	.L2047
	ldrb	r3, [r3, #928]	@ zero_extendqisi2
	cmp	r3, #0
	beq	.L2048
	ldr	r3, .L2051+4
	ldr	r2, [r3, #-1180]
	tst	r2, #1
	beq	.L2048
	ldr	r2, .L2051+8
	ldrh	r3, [r3, r2]
	cmp	r3, #0
	bne	.L2047
.L2048:
	b	Ftl_gc_temp_data_write_back.part.22
.L2047:
	mov	r0, #0
	bx	lr
.L2052:
	.align	2
.L2051:
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
	ldr	r6, .L2055
	ldr	r0, .L2055+4
	ldr	r4, .L2055+8
	ldrh	r1, [r6, r5]
	bl	FtlGcScanTempBlk
	ldr	r3, .L2055+12
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
.L2056:
	.align	2
.L2055:
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
	ldr	r3, .L2058
	ldr	r5, .L2058+4
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
.L2059:
	.align	2
.L2058:
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
	ldr	r6, .L2071
	mov	r2, #0
	ldr	r4, .L2071+4
	mvn	r5, #0
	ldr	r0, [r6, #3852]
	strh	r2, [r4, r3]	@ movhi
	movw	r3, #294
	strh	r5, [r4, r3]	@ movhi
	uxth	r0, r0
	bl	FtlFreeSysBlkQueueInit
	bl	FtlScanSysBlk
	ldr	r3, .L2071+8
	ldrh	r2, [r4, r3]
	movw	r3, #65535
	cmp	r2, r3
	beq	.L2061
	bl	FtlLoadSysInfo
	subs	r7, r0, #0
	bne	.L2061
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
	b	.L2062
.L2064:
	add	r0, r1, r7
	add	r7, r7, #12
	ldr	r0, [r0, #4]
	cmp	r0, #0
	blt	.L2063
	add	r3, r3, #1
.L2062:
	cmp	r3, r2
	blt	.L2064
.L2063:
	ldr	r4, .L2071+4
	cmp	r3, r2
	ldr	r1, .L2071+12
	ldrh	r0, [r4, r1]
	add	r0, r0, #1
	strh	r0, [r4, r1]	@ movhi
	blt	.L2065
	mov	r3, #296
	ldrh	r3, [r4, r3]
	cmp	r3, #0
	beq	.L2066
.L2065:
	ldr	r0, .L2071+16
	bl	FtlSuperblockPowerLostFix
	ldr	r0, .L2071+20
	bl	FtlSuperblockPowerLostFix
	ldr	r3, .L2071+24
	ldr	r1, [r4, #-2016]
	ldr	r2, .L2071+28
	ldrh	r3, [r4, r3]
	ldrh	r0, [r4, r2]
	mov	r3, r3, asl #1
	ldrh	ip, [r1, r3]
	rsb	r0, r0, ip
	strh	r0, [r1, r3]	@ movhi
	ldr	r0, .L2071
	movw	r1, #3916
	ldr	r3, .L2071+32
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
	ldr	r1, .L2071+36
	strh	r3, [r4, r2]	@ movhi
	strb	r3, [r4, #-1942]
	strh	r0, [r4, r1]	@ movhi
	ldr	r0, .L2071+40
	bl	FtlMapBlkWriteDumpData
	ldr	r0, .L2071+44
	bl	FtlMapBlkWriteDumpData
	ldr	r3, .L2071+48
	ldrh	r2, [r4, r3]
	add	r2, r2, #1
	strh	r2, [r4, r3]	@ movhi
	bl	l2p_flush
	bl	FtlVpcTblFlush
	bl	FtlVpcTblFlush
.L2066:
	ldr	r4, .L2071+4
	movw	r3, #65535
	ldr	r5, .L2071+24
	ldrh	r2, [r4, r5]
	cmp	r2, r3
	beq	.L2067
	ldr	r3, .L2071+28
	ldrh	r3, [r4, r3]
	cmp	r3, #0
	bne	.L2067
	ldr	r3, .L2071+52
	ldrh	r3, [r4, r3]
	cmp	r3, #0
	bne	.L2067
	bl	FtlVpcTblFlush
	ldrh	r0, [r4, r5]
	bl	FtlGcRefreshOpenBlock
	ldr	r3, .L2071+56
	ldrh	r0, [r4, r3]
	bl	FtlGcRefreshOpenBlock
	ldr	r0, .L2071+16
	bl	allocate_new_data_superblock
	ldr	r0, .L2071+20
	bl	allocate_new_data_superblock
	add	r0, r4, #248
	bl	FtlMapBlkWriteDumpData
.L2067:
	ldr	r3, .L2071+12
	ldr	r2, .L2071+4
	ldrh	r5, [r2, r3]
	ands	r5, r5, #31
	movne	r5, #0
	bne	.L2061
	bl	FtlVpcCheckAndModify
.L2061:
	mov	r0, r5
	ldmfd	sp!, {r3, r4, r5, r6, r7, pc}
.L2072:
	.align	2
.L2071:
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
	.type	FtlLowFormat.part.26, %function
FtlLowFormat.part.26:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	stmfd	sp!, {r3, r4, r5, r6, r7, r8, r9, sl, fp, lr}
	.save {r3, r4, r5, r6, r7, r8, r9, sl, fp, lr}
	mov	r6, #3952
	ldr	r5, .L2094
	mov	r1, #0
	ldr	r4, .L2094+4
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
	beq	.L2074
	bl	FtlMakeBbt
.L2074:
	mov	r3, #0
	ldr	ip, .L2094
	movw	r0, #3922
	ldr	r2, .L2094+4
	ldr	r1, .L2094+8
	b	.L2075
.L2076:
	ldr	lr, [r2, #-944]
	mvn	r4, r3
	orr	r4, r3, r4, asl #16
	str	r4, [lr, r3, asl #2]
	ldr	lr, [r2, #-940]
	str	r1, [lr, r3, asl #2]
	add	r3, r3, #1
	uxth	r3, r3
.L2075:
	ldrh	lr, [ip, r0]
	ldr	r8, .L2094
	cmp	r3, lr, asl #7
	blt	.L2076
	mov	r3, #3856
	mov	r5, #0
	ldrh	r6, [r8, r3]
	movw	r7, #3858
	b	.L2077
.L2078:
	mov	r0, r6
	mov	r1, #1
	bl	FtlLowFormatEraseBlock
	add	r6, r6, #1
	uxth	r6, r6
	add	r5, r5, r0
	uxth	r5, r5
.L2077:
	ldrh	r3, [r8, r7]
	ldr	r4, .L2094
	cmp	r3, r6
	bhi	.L2078
	movw	r3, #3848
	ldrh	r1, [r4, r3]
	sub	r3, r5, #3
	cmp	r3, r1, asl #1
	blt	.L2079
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
	b	.L2080
.L2081:
	mov	r0, r5
	mov	r1, #1
	bl	FtlLowFormatEraseBlock
	add	r5, r5, #1
	uxth	r5, r5
.L2080:
	ldrh	r3, [r4, r6]
	cmp	r3, r5
	bhi	.L2081
.L2079:
	mov	r4, #0
	ldr	r8, .L2094
	mov	r6, r4
	mov	r7, #3856
	b	.L2082
.L2083:
	mov	r0, r6
	mov	r1, #0
	bl	FtlLowFormatEraseBlock
	add	r6, r6, #1
	uxth	r6, r6
	add	r4, r4, r0
	uxth	r4, r4
.L2082:
	ldrh	r3, [r8, r7]
	ldr	r5, .L2094
	cmp	r3, r6
	bhi	.L2083
	movw	r3, #3858
	ldr	r8, .L2094+4
	ldrh	r3, [r5, r3]
	ldr	fp, [r5, #3860]
	ldr	r9, .L2094+12
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
	ble	.L2084
	rsb	r0, r4, fp
	mov	r1, r6
	bl	__aeabi_uidiv
	str	r0, [r5, #3976]
	mov	r0, r0, lsr #5
	add	r0, r0, #24
	strh	r0, [r8, r9]	@ movhi
.L2084:
	ldr	r3, .L2094
	ldr	r3, [r3, #3840]
	cmp	r3, #1
	bne	.L2085
	mov	r0, r4
	mov	r1, r6
	bl	__aeabi_uidiv
	ldr	r8, .L2094+4
	ldr	r5, .L2094+12
	ldrh	r9, [r8, r5]
	uxtah	r0, r9, r0
	add	r9, r9, r0, lsr #2
	strh	r9, [r8, r5]	@ movhi
.L2085:
	ldr	r3, .L2094
	ldrb	r3, [r3, #928]	@ zero_extendqisi2
	cmp	r3, #0
	beq	.L2086
	mov	r0, r4
	mov	r1, r6
	bl	__aeabi_uidiv
	ldr	r8, .L2094+4
	ldr	r5, .L2094+12
	ldrh	r9, [r8, r5]
	uxtah	r0, r9, r0
	add	r9, r9, r0, lsr #2
	strh	r9, [r8, r5]	@ movhi
.L2086:
	ldr	r0, .L2094
	movw	r3, #3908
	ldrh	r3, [r0, r3]
	cmp	r3, #0
	beq	.L2087
	ldr	r1, .L2094+4
	ldr	r2, .L2094+12
	ldrh	ip, [r1, r2]
	add	ip, ip, r3, lsr #1
	strh	ip, [r1, r2]	@ movhi
	mul	ip, r6, r3
	cmp	ip, r4
	strgt	r7, [r0, #3976]
	addgt	r3, r3, #32
	addgt	sl, sl, r3
	strgth	sl, [r1, r2]	@ movhi
.L2087:
	ldr	r4, .L2094+4
	ldr	r5, .L2094
	ldr	r3, .L2094+12
	ldr	r7, .L2094+16
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
	ldr	r1, .L2094+20
	mov	r3, #0
	ldr	r2, .L2094+24
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
.L2088:
	ldr	r0, .L2094+28
	bl	make_superblock
	ldrb	r3, [r4, #-1989]	@ zero_extendqisi2
	ldr	r5, .L2094+4
	cmp	r3, #0
	ldr	r3, .L2094+16
	bne	.L2089
	ldrh	r3, [r4, r7]
	ldr	r2, [r4, #-2016]
	mov	r3, r3, asl #1
	strh	r6, [r2, r3]	@ movhi
	ldrh	r3, [r4, r7]
	add	r3, r3, #1
	strh	r3, [r4, r7]	@ movhi
	b	.L2088
.L2089:
	ldr	r2, [r5, #-1804]
	mvn	r7, #0
	ldr	r1, [r5, #-2016]
	ldr	r6, .L2094+32
	str	r2, [r5, #-1984]
	add	r2, r2, #1
	str	r2, [r5, #-1804]
	ldr	r2, .L2094+36
	ldrh	r0, [r5, r2]
	ldrh	r2, [r5, r3]
	mov	r2, r2, asl #1
	strh	r0, [r1, r2]	@ movhi
	mov	r2, #0
	ldr	r0, .L2094+40
	ldrh	r3, [r5, r3]
	strb	r2, [r5, #-1942]
	add	r3, r3, #1
	strh	r2, [r5, r0]	@ movhi
	strh	r3, [r5, r6]	@ movhi
	mov	r3, #1
	strb	r3, [r5, #-1940]
.L2090:
	ldr	r0, .L2094+44
	bl	make_superblock
	ldrb	r3, [r5, #-1941]	@ zero_extendqisi2
	ldr	r4, .L2094+4
	cmp	r3, #0
	bne	.L2091
	ldrh	r3, [r5, r6]
	ldr	r2, [r5, #-2016]
	mov	r3, r3, asl #1
	strh	r7, [r2, r3]	@ movhi
	ldrh	r3, [r5, r6]
	add	r3, r3, #1
	strh	r3, [r5, r6]	@ movhi
	b	.L2090
.L2091:
	ldr	r3, [r4, #-1804]
	mvn	r5, #0
	ldr	r2, [r4, #-2016]
	str	r3, [r4, #-1936]
	add	r3, r3, #1
	str	r3, [r4, #-1804]
	ldr	r3, .L2094+48
	ldrh	r1, [r4, r3]
	sub	r3, r3, #4
	ldrh	r3, [r4, r3]
	mov	r3, r3, asl #1
	strh	r1, [r2, r3]	@ movhi
	ldr	r3, .L2094+52
	strh	r5, [r4, r3]	@ movhi
	bl	FtlFreeSysBlkQueueOut
	ldr	r3, .L2094+56
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
	ldr	r3, .L2094+60
	mov	r2, #1
	str	r2, [r3, #2996]
	ldmfd	sp!, {r3, r4, r5, r6, r7, r8, r9, sl, fp, pc}
.L2095:
	.align	2
.L2094:
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
	ldr	r3, .L2098
	ldr	r3, [r3, #4040]
	cmp	r3, #0
	bne	.L2097
	bl	FtlLowFormat.part.26
.L2097:
	mov	r0, #0
	ldmfd	sp!, {r3, pc}
.L2099:
	.align	2
.L2098:
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
	ldr	r3, .L2103
	ldr	r5, .L2103+4
	ldrh	r2, [r5, r3]
	movw	r3, #65535
	cmp	r2, r3
	beq	.L2101
	ldr	r3, .L2103+8
	ldrh	r3, [r5, r3]
	cmp	r3, #0
	bne	.L2102
.L2101:
	bl	FtlCacheWriteBack
	mov	r0, #0
	bl	FtlGcFreeTempBlock
	ldr	r0, .L2103+12
	mov	r4, #0
	strb	r4, [r5, #-1892]
	bl	allocate_data_superblock
	ldr	r3, .L2103+16
	strh	r4, [r5, r3]	@ movhi
	add	r3, r3, #12
	strh	r4, [r5, r3]	@ movhi
	bl	l2p_flush
	mov	r0, r4
	bl	FtlEctTblFlush
	bl	FtlVpcTblFlush
.L2102:
	ldr	r0, .L2103+12
	ldmfd	sp!, {r3, r4, r5, lr}
	b	get_new_active_ppa
.L2104:
	.align	2
.L2103:
	.word	-1900
	.word	.LANCHOR2
	.word	-1896
	.word	.LANCHOR2-1900
	.word	-1156
	.fnend
	.size	Ftl_get_new_temp_ppa, .-Ftl_get_new_temp_ppa
	.align	2
	.type	ftl_do_gc.part.23, %function
ftl_do_gc.part.23:
	.fnstart
	@ args = 0, pretend = 0, frame = 24
	@ frame_needed = 0, uses_anonymous_args = 0
	ldr	r3, .L2202
	movw	ip, #65535
	ldr	r2, .L2202+4
	stmfd	sp!, {r4, r5, r6, r7, r8, r9, sl, fp, lr}
	.save {r4, r5, r6, r7, r8, r9, sl, fp, lr}
	.pad #36
	sub	sp, sp, #36
	mov	sl, r1
	str	r0, [sp, #16]
	ldrh	r0, [r3, r2]
	cmp	r0, ip
	beq	.L2106
	ldr	r1, .L2202+8
	ldrh	r4, [r3, r1]
	cmp	r4, ip
	streqh	r0, [r3, r1]	@ movhi
	mvneq	r1, #0
	streqh	r1, [r3, r2]	@ movhi
.L2106:
	ldr	r1, [sp, #16]
	ldr	r2, [r3, #-1192]
	cmp	r1, #1
	add	r2, r2, #1
	add	r2, r2, r1, asl #7
	str	r2, [r3, #-1192]
	bne	.L2107
	ldr	r3, .L2202+12
	ldr	r1, [r3, #3840]
	cmp	r1, #0
	bne	.L2108
	ldrb	r3, [r3, #928]	@ zero_extendqisi2
	cmp	r3, #0
	beq	.L2107
.L2108:
	ldr	r4, .L2202
	ldr	r3, [r4, #-1780]
	cmp	r3, #29
	bhi	.L2107
	mov	r3, #432
	ldrh	r3, [r4, r3]
	add	r2, r2, r3
	str	r2, [r4, #-1192]
	bl	FtlGcReFreshBadBlk
	ldr	r3, .L2202+16
	movw	r2, #65535
	ldrh	r3, [r4, r3]
	cmp	r3, r2
	bne	.L2107
	ldr	r2, .L2202+8
	ldrh	r2, [r4, r2]
	cmp	r2, r3
	bne	.L2107
	ldr	r3, [r4, #-1192]
	cmp	r3, #1024
	bhi	.L2109
	ldr	r3, .L2202+20
	ldrh	r3, [r4, r3]
	cmp	r3, #63
	bhi	.L2107
.L2109:
	ldr	r3, .L2202
	mov	r2, #432
	ldr	r0, .L2202+20
	mov	r1, #0
	strh	r1, [r3, r2]	@ movhi
	ldrh	ip, [r3, r0]
	ldr	r0, .L2202+24
	ldrh	r0, [r3, r0]
	add	r0, r0, #64
	cmp	ip, r0
	bgt	.L2107
	str	r1, [r3, #-1192]
	ldr	r1, [r3, #-1780]
	cmp	r1, #0
	moveq	r1, #6
	beq	.L2198
	cmp	r1, #5
	bhi	.L2111
	mov	r1, #18
.L2198:
	strh	r1, [r3, r2]	@ movhi
.L2111:
	mov	r0, #32
	movw	r7, #65535
	bl	List_get_gc_head_node
	uxth	r2, r0
	cmp	r2, r7
	beq	.L2112
	ldr	r5, .L2202
	ldr	r6, .L2202+28
	ldrh	r0, [r5, r6]
	cmp	r0, #0
	moveq	r3, #1
	streqh	r3, [r5, r6]	@ movhi
	beq	.L2112
	ldr	r3, .L2202+12
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
	bgt	.L2112
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
	beq	.L2112
	ldrh	r2, [r9, r2]
	mov	r7, r4, asl #1
	ldr	r0, .L2202+32
	mov	r1, fp
	ldrh	r3, [r9, r7]
	str	r2, [sp, #0]
	mov	r2, r4
	bl	printk
	ldrh	r3, [r5, r6]
	cmp	r3, #40
	bls	.L2114
	ldr	r3, [r5, #-2016]
	ldrh	r3, [r3, r7]
	cmp	r3, #32
	strhih	r8, [r5, r6]	@ movhi
.L2114:
	ldr	r2, .L2202
	mov	r3, #432
	mov	r1, #6
	strh	r1, [r2, r3]	@ movhi
	b	.L2116
.L2112:
	bl	GetSwlReplaceBlock
	movw	r3, #65535
	cmp	r0, r3
	mov	r4, r0
	bne	.L2116
	ldr	r2, .L2202
	mov	r3, #432
	mov	r1, #0
	strh	r1, [r2, r3]	@ movhi
.L2107:
	ldr	r5, .L2202
	movw	r4, #65535
	ldr	r3, .L2202+16
	ldrh	r2, [r5, r3]
	cmp	r2, r4
	bne	.L2116
	sub	r3, r3, #140
	ldrh	r3, [r5, r3]
	cmp	r3, r2
	movne	r4, r2
	bne	.L2116
	ldr	r2, .L2202+8
	ldrh	r7, [r5, r2]
	cmp	r7, r3
	movne	r4, r3
	bne	.L2116
	ldr	r8, .L2202+20
	ldr	r2, [r5, #-1192]
	ldrh	r3, [r5, r8]
	cmp	r3, #23
	movhi	r3, #1024
	movls	r3, #5120
	cmp	r2, r3
	movls	r4, r7
	bls	.L2116
	mov	r2, #432
	mov	r3, #0
	str	r3, [r5, #-1192]
	mov	r6, r5
	strh	r3, [r5, r2]	@ movhi
	bl	GetSwlReplaceBlock
	cmp	r0, r7
	mov	r4, r0
	bne	.L2118
	ldr	r3, .L2202+24
	ldrh	r1, [r5, r8]
	ldrh	r2, [r5, r3]
	cmp	r1, r2
	movcs	r2, #80
	strcsh	r2, [r5, r3]	@ movhi
	bcs	.L2128
	mov	r0, #64
	bl	List_get_gc_head_node
	uxth	r3, r0
	cmp	r3, r4
	beq	.L2128
	ldr	r2, [r5, #-1000]
	cmp	r2, #0
	bne	.L2121
	ldr	r2, .L2202+12
	movw	r1, #3868
	ldrh	r1, [r2, r1]
	cmp	r1, #3
	beq	.L2121
	ldr	r1, [r5, #-1712]
	cmp	r1, #0
	bne	.L2121
	ldr	r1, [r2, #3840]
	cmp	r1, #0
	bne	.L2121
	ldrb	r0, [r2, #928]	@ zero_extendqisi2
	cmp	r0, #0
	beq	.L2122
.L2121:
	ldr	r1, .L2202
	mov	r3, r3, asl #1
	movw	ip, #3848
	movw	lr, #3868
	ldr	r2, [r1, #-2016]
	ldrh	r0, [r2, r3]
	movw	r2, #3918
	ldr	r3, .L2202+12
	ldrh	r2, [r3, r2]
	ldrh	ip, [r3, ip]
	ldrh	r3, [r3, lr]
	mul	ip, ip, r2
	cmp	r3, #3
	moveq	r3, r2, lsr #1
	movne	r3, #0
	add	r3, ip, r3
	cmp	r0, r3
	bgt	.L2124
	mov	r0, #0
	bl	List_get_gc_head_node
	ldr	r2, .L2202+12
	ldr	r3, .L2202
	ldr	r2, [r2, #3976]
	ldr	r1, [r3, #-1844]
	add	r2, r2, r2, asl #1
	cmp	r1, r2, lsr #2
	ldr	r2, .L2202+24
	movhi	r1, #128
	movls	r1, #160
	strh	r1, [r3, r2]	@ movhi
	uxth	r4, r0
	b	.L2126
.L2124:
	ldr	r3, .L2202+24
	mov	r2, #128
	strh	r2, [r1, r3]	@ movhi
	b	.L2128
.L2122:
	ldr	r2, [r5, #-2016]
	mov	r3, r3, asl #1
	ldr	r6, .L2202
	ldr	r5, .L2202+24
	ldrh	r3, [r2, r3]
	cmp	r3, #7
	movhi	r3, #64
	strhih	r3, [r6, r5]	@ movhi
	bhi	.L2128
	bl	List_get_gc_head_node
	mov	r3, #128
	strh	r3, [r6, r5]	@ movhi
	uxth	r4, r0
.L2126:
	movw	r3, #65535
	cmp	r4, r3
	beq	.L2128
.L2118:
	ldr	r1, .L2202
	mov	r0, r4, asl #1
	ldr	r3, .L2202+20
	ldr	ip, .L2202+12
	ldrh	r2, [r1, r3]
	ldr	ip, [ip, #4048]
	ldr	r3, [r1, #-2016]
	ldrh	r3, [r3, r0]
	ldrh	r0, [ip, r0]
	str	r0, [sp, #0]
	ldr	r0, .L2202+36
	ldrh	r1, [r1, r0]
	ldr	r0, .L2202+40
	str	r1, [sp, #4]
	mov	r1, r4
	bl	printk
.L2128:
	bl	FtlGcReFreshBadBlk
.L2116:
	movw	r0, #65535
	rsb	ip, r0, r4
	rsbs	r1, ip, #0
	ldr	r3, [sp, #16]
	adc	r1, r1, ip
	ldr	r5, .L2202
	cmp	r3, #0
	movne	r2, #0
	andeq	r2, r1, #1
	cmp	r2, #0
	beq	.L2129
	ldr	r3, .L2202+20
	ldrh	r2, [r5, r3]
	cmp	r2, #24
	movhi	r6, #1
	bhi	.L2130
	ldr	r1, .L2202+12
	movw	r3, #3916
	cmp	r2, #16
	ldrh	r6, [r1, r3]
	movhi	r6, r6, lsr #5
	bhi	.L2130
	cmp	r2, #12
	movhi	r6, r6, lsr #4
	bhi	.L2130
	cmp	r2, #8
	movhi	r6, r6, lsr #2
.L2130:
	ldr	r1, .L2202+36
	ldr	r3, .L2202
	ldrh	r0, [r5, r1]
	cmp	r0, r2
	mov	r0, r1
	bcs	.L2134
	ldr	r2, .L2202+44
	movw	ip, #65535
	ldrh	r2, [r3, r2]
	cmp	r2, ip
	bne	.L2135
	ldr	ip, .L2202+8
	ldrh	ip, [r3, ip]
	cmp	ip, r2
	bne	.L2135
	mov	r2, #432
	ldrh	r0, [r3, r2]
	cmp	r0, #0
	bne	.L2136
	ldr	r2, .L2202+12
	ldr	ip, [r3, #-1844]
	ldr	r2, [r2, #3976]
	add	r2, r2, r2, asl #1
	cmp	ip, r2, lsr #2
	movcs	r2, #18
	bcs	.L2200
.L2136:
	ldr	r3, .L2202
	ldr	r2, .L2202+48
	ldr	r1, .L2202+36
	ldrh	r2, [r3, r2]
	add	r2, r2, r2, asl #1
	mov	r2, r2, asr #2
.L2200:
	strh	r2, [r3, r1]	@ movhi
	mov	r2, #0
	ldr	r3, .L2202
	str	r2, [r3, #-1184]
	b	.L2139
.L2135:
	ldr	r3, .L2202
	ldr	r2, .L2202+48
	ldrh	r2, [r3, r2]
	add	r2, r2, r2, asl #1
	mov	r2, r2, asr #2
	strh	r2, [r3, r0]	@ movhi
.L2134:
	ldr	r3, .L2202+12
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
	b	.L2140
.L2129:
	ldr	r3, .L2202+44
	ldrh	r3, [r5, r3]
	cmp	r3, r0
	bne	.L2141
	ldr	r0, .L2202+8
	ldrh	r0, [r5, r0]
	cmp	r0, r3
	movne	r1, #0
	andeq	r1, r1, #1
	cmp	r1, #0
	beq	.L2141
	ldr	r1, .L2202+16
	ldrh	r4, [r5, r1]
	cmp	r4, r3
	movne	r4, r3
	bne	.L2141
	ldr	r3, .L2202+20
	mov	r6, #432
	str	r2, [r5, #-1184]
	ldrh	r7, [r5, r3]
	add	r3, r3, #804
	ldrh	r2, [r5, r3]
	cmp	r2, r7
	bcs	.L2142
	ldrh	r2, [r5, r6]
	cmp	r2, #0
	bne	.L2143
	ldr	r2, .L2202+12
	ldr	r1, [r5, #-1844]
	ldr	r2, [r2, #3976]
	add	r2, r2, r2, asl #1
	cmp	r1, r2, lsr #2
	movcs	r2, #18
	strcsh	r2, [r5, r3]	@ movhi
	bcs	.L2145
.L2143:
	ldr	r3, .L2202
	ldr	r2, .L2202+48
	ldr	r1, .L2202+36
	ldrh	r2, [r3, r2]
	add	r2, r2, r2, asl #1
	mov	r2, r2, asr #2
	strh	r2, [r3, r1]	@ movhi
.L2145:
	bl	FtlReadRefresh
	mov	r3, #432
	ldr	r2, .L2202
	b	.L2201
.L2142:
	ldrh	r0, [r5, r6]
	cmp	r0, #0
	bne	.L2141
	ldr	r2, .L2202+48
	ldrh	r4, [r5, r2]
	add	r2, r4, r4, asl #1
	mov	r2, r2, asr #2
	strh	r2, [r5, r3]	@ movhi
	bl	List_get_gc_head_node
	ldr	r3, [r5, #-2016]
	ldr	r1, .L2202+12
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
	ble	.L2146
	sub	r4, r4, #1
	cmp	r7, r4
	blt	.L2146
	bl	FtlReadRefresh
	ldrh	r0, [r5, r6]
	b	.L2139
.L2146:
	cmp	r3, #0
	movwne	r4, #65535
	bne	.L2141
	movw	r0, #65535
	bl	decrement_vpc_count
	ldr	r3, .L2202+20
	ldr	r2, .L2202
	ldrh	r0, [r2, r3]
	add	r0, r0, #1
	b	.L2139
.L2141:
	ldr	r3, .L2202+12
	ldr	r6, [r3, #3840]
	cmp	r6, #0
	movne	r6, #2
	moveq	r6, #1
.L2140:
	ldr	r3, .L2202
	movw	r0, #65535
	ldr	r2, .L2202+16
	ldrh	r1, [r3, r2]
	cmp	r1, r0
	bne	.L2147
	cmp	r4, r1
	strneh	r4, [r3, r2]	@ movhi
	bne	.L2149
	ldr	r2, .L2202+8
	ldrh	r1, [r3, r2]
	cmp	r1, r4
	beq	.L2149
	ldr	r0, [r3, #-2016]
	mov	r1, r1, asl #1
	ldrh	r1, [r0, r1]
	cmp	r1, #0
	mvneq	r1, #0
	streqh	r1, [r3, r2]	@ movhi
	ldr	r3, .L2202
	ldr	r1, .L2202+16
	ldrh	r0, [r3, r2]
	strh	r0, [r3, r1]	@ movhi
	mvn	r1, #0
	strh	r1, [r3, r2]	@ movhi
.L2149:
	ldr	r5, .L2202
	mov	r3, #0
	ldr	r7, .L2202+16
	strb	r3, [r5, #-1752]
	movw	r3, #65535
	ldrh	r0, [r5, r7]
	cmp	r0, r3
	beq	.L2147
	bl	IsBlkInGcList
	cmp	r0, #0
	mvnne	r3, #0
	strneh	r3, [r5, r7]	@ movhi
	ldr	r3, .L2202+12
	ldr	r7, .L2202
	ldrb	r3, [r3, #928]	@ zero_extendqisi2
	cmp	r3, #0
	beq	.L2152
	ldr	r3, .L2202+16
	ldrh	r0, [r7, r3]
	bl	ftl_get_blk_mode
	strb	r0, [r7, #-1752]
.L2152:
	ldr	r8, .L2202+16
	movw	r3, #65535
	ldr	r5, .L2202
	ldrh	r2, [r7, r8]
	cmp	r2, r3
	beq	.L2147
	add	r0, r5, r8
	bl	make_superblock
	ldr	r1, .L2202+52
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
.L2147:
	ldr	r3, .L2202
	ldr	r2, .L2202+16
	ldrh	r1, [r3, r2]
	sub	r2, r2, #236
	ldrh	r2, [r3, r2]
	cmp	r2, r1
	mov	r2, r3
	beq	.L2153
	ldr	r0, .L2202+56
	ldrh	r3, [r3, r0]
	cmp	r3, r1
	movne	fp, r4
	bne	.L2197
.L2154:
.L2153:
	ldr	r3, .L2202+16
	mvn	r1, #0
	strh	r1, [r2, r3]	@ movhi
	mov	r3, #432
.L2201:
	ldrh	r0, [r2, r3]
	b	.L2139
.L2197:
	ldr	r4, .L2202
	movw	sl, #65535
	ldr	r3, .L2202+16
	ldrh	r3, [r4, r3]
	cmp	r3, sl
	bne	.L2156
	ldr	r5, .L2202+28
	mov	r3, #0
	str	r3, [r4, #-1184]
.L2196:
	ldrh	r8, [r4, r5]
	mov	r0, r8
	bl	List_get_gc_head_node
	ldr	r3, .L2202+16
	movw	r1, #65535
	uxth	r7, r0
	strh	r7, [r4, r3]	@ movhi
	cmp	r7, r1
	bne	.L2158
	ldr	r2, .L2202
	add	r3, r3, #572
	mov	r1, #0
	mov	r0, #8
	strh	r1, [r2, r3]	@ movhi
	b	.L2139
.L2158:
	mov	r0, r7
	bl	IsBlkInGcList
	cmp	r0, #0
	add	r0, r8, #1
	strneh	r0, [r4, r5]	@ movhi
	bne	.L2196
	ldr	ip, .L2202+12
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
	bgt	.L2161
	cmp	r0, #48
	bls	.L2162
	cmp	r1, #8
	bls	.L2162
	ldr	r1, .L2202+60
	ldrh	r1, [r4, r1]
	cmp	r1, #35
	bhi	.L2162
.L2161:
	mov	r1, #0
	strh	r1, [r4, r5]	@ movhi
.L2162:
	ldrh	r3, [r2, r3]
	movw	r2, #65535
	ldr	sl, .L2202
	cmp	r3, r8
	cmpge	fp, r2
	movne	r8, #0
	moveq	r8, #1
	bne	.L2163
	ldr	r3, .L2202+16
	mvn	r2, #0
	strh	r2, [sl, r3]	@ movhi
	add	r3, r3, #572
	mov	r2, #0
	strh	r2, [sl, r3]	@ movhi
	mov	r3, #432
	ldrh	r0, [sl, r3]
	b	.L2139
.L2163:
	cmp	r3, #0
	bne	.L2164
	movw	r0, #65535
	bl	decrement_vpc_count
	ldrh	r3, [r4, r5]
	add	r3, r3, #1
	strh	r3, [r4, r5]	@ movhi
	b	.L2196
.L2164:
	ldr	r3, .L2202+12
	strb	r8, [sl, #-1752]
	ldrb	r3, [r3, #928]	@ zero_extendqisi2
	cmp	r3, #0
	beq	.L2165
	mov	r0, r7
	bl	ftl_get_blk_mode
	strb	r0, [sl, #-1752]
.L2165:
	ldr	r4, .L2202
	sub	r0, r4, #1760
	bl	make_superblock
	movw	r1, #434
	mov	r3, #0
	strh	r3, [r4, r1]	@ movhi
	ldr	r1, .L2202+16
	ldr	r0, [r4, #-2016]
	ldrh	r1, [r4, r1]
	mov	r1, r1, asl #1
	ldrh	r0, [r0, r1]
	mov	r1, #436
	strb	r3, [r4, #-1754]
	strh	r0, [r4, r1]	@ movhi
	ldr	r1, .L2202+52
	strh	r3, [r4, r1]	@ movhi
.L2156:
	ldr	sl, [sp, #16]
	cmp	sl, #1
	bne	.L2166
	bl	FtlReadRefresh
.L2166:
	ldr	r3, .L2202
	mov	r2, #1
	movw	r1, #3916
	str	r2, [r3, #-996]
	ldr	r2, .L2202+12
	ldrh	r9, [r2, r1]
	ldrb	r1, [r2, #928]	@ zero_extendqisi2
	cmp	r1, #0
	beq	.L2167
	ldrb	r1, [r3, #-1752]	@ zero_extendqisi2
	cmp	r1, #1
	movweq	r1, #3918
	ldreqh	r9, [r2, r1]
.L2167:
	ldr	r2, .L2202+52
	mov	r8, #0
	str	fp, [sp, #20]
	mov	fp, r9
	ldr	r4, .L2202
	ldrh	r3, [r3, r2]
	add	r2, r3, r6
	cmp	r2, r9
	rsbgt	r6, r3, r9
	uxthgt	r6, r6
	b	.L2169
.L2171:
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
.L2177:
	cmp	r3, r5
	bne	.L2171
	ldr	r0, [r4, #-1164]
	mov	r1, r7
	ldrb	r2, [r4, #-1752]	@ zero_extendqisi2
	mov	r5, #0
	bl	FlashReadPages
	mov	r9, r5
	mov	sl, r6
	b	.L2172
.L2175:
	ldr	r3, [r4, #-1164]
	add	r2, r3, r5
	ldr	r3, [r3, r5]
	ldr	r6, [r2, #12]
	cmn	r3, #1
	beq	.L2173
	ldrh	r3, [r6, #0]
	movw	r1, #61589
	cmp	r3, r1
	bne	.L2173
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
	bne	.L2173
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
	ldr	r3, .L2202+44
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
	ldr	r3, .L2202+12
	ldrb	r3, [r3, #928]	@ zero_extendqisi2
	cmp	r3, #0
	bne	.L2174
	ldrb	r3, [r4, #-1893]	@ zero_extendqisi2
	ldr	r2, [r4, #-1180]
	cmp	r2, r3
	beq	.L2174
	ldr	r3, .L2202+64
	ldrh	r3, [r4, r3]
	cmp	r3, #0
	bne	.L2173
.L2174:
	bl	Ftl_gc_temp_data_write_back
	cmp	r0, #0
	ldrne	r3, .L2202
	movne	r2, #0
	strne	r2, [r3, #-996]
	movne	r2, #432
	bne	.L2201
.L2173:
	add	r9, r9, #1
	add	r5, r5, #36
	uxth	r9, r9
.L2172:
	cmp	r9, r7
	bne	.L2175
	add	r8, r8, #1
	mov	r6, sl
.L2169:
	uxth	r3, r8
	ldr	r7, .L2202
	cmp	r3, r6
	ldr	r3, .L2202+52
	bcs	.L2176
	ldr	r1, .L2202+12
	movw	r2, #3848
	ldrh	ip, [r4, r3]
	mov	r7, #0
	ldr	lr, [r4, #-1164]
	mov	r3, r7
	ldrh	r5, [r1, r2]
	add	ip, ip, r8
	ldr	r2, .L2202+68
	mov	r0, #36
	b	.L2177
.L2176:
	ldrh	r2, [r7, r3]
	mov	r9, fp
	ldr	fp, [sp, #20]
	add	r6, r6, r2
	uxth	r6, r6
	strh	r6, [r7, r3]	@ movhi
	cmp	r6, r9
	bcc	.L2178
	ldr	r3, [r7, #-1180]
	cmp	r3, #0
	beq	.L2179
	bl	Ftl_gc_temp_data_write_back
	cmp	r0, #0
	movne	r3, #0
	strne	r3, [r7, #-996]
	movne	r3, #432
	ldrneh	r0, [r7, r3]
	bne	.L2139
.L2179:
	ldr	r3, .L2202
	movw	r2, #434
	ldrh	ip, [r3, r2]
	cmp	ip, #0
	bne	.L2180
	ldr	r2, .L2202+16
	ldr	r0, [r3, #-2016]
	ldrh	r1, [r3, r2]
	mov	r1, r1, asl #1
	ldrh	r4, [r0, r1]
	cmp	r4, #0
	beq	.L2180
	strh	ip, [r0, r1]	@ movhi
	ldrh	r0, [r3, r2]
	bl	update_vpc_list
	bl	FtlCacheWriteBack
	bl	l2p_flush
	bl	FtlVpcTblFlush
.L2180:
	ldr	r3, .L2202+16
	mvn	r1, #0
	ldr	r2, .L2202
	strh	r1, [r2, r3]	@ movhi
.L2178:
	ldr	r3, .L2202
	ldr	r2, .L2202+20
	ldrh	r2, [r3, r2]
	cmp	r2, #2
	ldrls	r2, .L2202+12
	movwls	r3, #3916
	ldrlsh	r6, [r2, r3]
	bls	.L2197
.L2181:
	mov	r1, #0
	str	r1, [r3, #-996]
	mov	r1, #432
	ldrh	r0, [r3, r1]
	cmp	r0, #0
	addeq	r0, r2, #1
.L2139:
	add	sp, sp, #36
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, sl, fp, pc}
.L2203:
	.align	2
.L2202:
	.word	.LANCHOR2
	.word	-1140
	.word	-1142
	.word	.LANCHOR0
	.word	-1760
	.word	-2000
	.word	-1194
	.word	-1188
	.word	.LC149
	.word	-1196
	.word	.LC150
	.word	-1900
	.word	-1776
	.word	-1758
	.word	-1948
	.word	-1156
	.word	-1896
	.word	.LANCHOR2-1746
	.fnend
	.size	ftl_do_gc.part.23, .-ftl_do_gc.part.23
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
	ldr	r3, .L2211
	mov	r5, r1
	ldr	r0, [r3, #4040]
	cmp	r0, #0
	movne	r0, #0
	ldmnefd	sp!, {r3, r4, r5, pc}
	ldr	r3, .L2211+4
	ldr	r2, [r3, #-996]
	cmp	r2, #0
	ldmnefd	sp!, {r3, r4, r5, pc}
	ldr	r1, .L2211+8
	ldrh	r1, [r3, r1]
	cmp	r1, #47
	movls	r0, r2
	ldmlsfd	sp!, {r3, r4, r5, pc}
	ldr	r1, .L2211+12
	movw	r2, #3000
	ldrh	r1, [r1, r2]
	movw	r2, #65535
	cmp	r1, r2
	beq	.L2206
	ldr	r1, .L2211+16
	ldrh	r3, [r3, r1]
	cmp	r3, r2
	beq	.L2206
	mov	r0, #1
	bl	FtlGcFreeTempBlock
	cmp	r0, #0
	bne	.L2210
.L2206:
	mov	r0, r4
	mov	r1, r5
	ldmfd	sp!, {r3, r4, r5, lr}
	b	ftl_do_gc.part.23
.L2210:
	mov	r0, #1
	ldmfd	sp!, {r3, r4, r5, pc}
.L2212:
	.align	2
.L2211:
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
	ldr	r3, .L2236
	stmfd	sp!, {r0, r1, r4, r5, r6, r7, r8, r9, sl, lr}
	.save {r0, r1, r4, r5, r6, r7, r8, r9, sl, lr}
	ldr	r4, [r3, #440]
	ldr	r3, .L2236+4
	ldr	r2, [r3, #4040]
	cmp	r2, #0
	bne	.L2214
	ldr	r1, [r3, #3968]
	cmp	r1, #0
	beq	.L2214
	ldrb	r8, [r3, #928]	@ zero_extendqisi2
	cmp	r8, #0
	beq	.L2215
	ldrb	r8, [r4, #8]	@ zero_extendqisi2
	sub	lr, r8, #1
	rsbs	r8, lr, #0
	adc	r8, r8, lr
.L2215:
	ldr	r5, .L2236+4
	mov	r2, r8
	ldrb	r3, [r4, #9]	@ zero_extendqisi2
	mov	r6, #0
	mov	r7, r6
	ldr	r9, .L2236
	ldr	r0, [r5, #3972]
	bl	FlashProgPages
	b	.L2216
.L2221:
	ldr	r2, [r5, #3972]
	add	r3, r2, r6
	ldr	r2, [r2, r6]
	cmn	r2, #1
	beq	.L2235
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
	beq	.L2219
	ubfx	r0, r0, #10, #16
	bl	P2V_block_in_plane
	ldr	r2, [r9, #-2016]
	mov	r3, r0, asl #1
	mov	sl, r0
	ldrh	r2, [r2, r3]
	cmp	r2, #0
	bne	.L2220
	ldr	r0, .L2236+8
	mov	r1, sl
	bl	printk
.L2220:
	mov	r0, sl
	bl	decrement_vpc_count
.L2219:
	add	r7, r7, #1
	add	r6, r6, #36
.L2216:
	ldr	r3, [r5, #3968]
	cmp	r7, r3
	bcc	.L2221
	b	.L2222
.L2231:
	ldr	r3, [r5, #3972]
	mvn	r2, #0
	ldr	r7, .L2236
	str	r2, [r3, r6]
	b	.L2223
.L2227:
	ldr	r0, [r3, #4]
	ubfx	r0, r0, #10, #16
	bl	P2V_block_in_plane
	ldrh	r3, [r4, #0]
	cmp	r3, r0
	bne	.L2224
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
.L2224:
	ldrh	r3, [r4, #4]
	cmp	r3, #0
	bne	.L2225
	mov	r0, r4
	bl	allocate_new_data_superblock
.L2225:
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
	bne	.L2214
.L2223:
	ldr	r2, [r5, #3972]
	add	r3, r2, r6
	ldr	r2, [r2, r6]
	cmn	r2, #1
	beq	.L2227
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
	beq	.L2229
	ubfx	r0, r0, #10, #16
	bl	P2V_block_in_plane
	ldr	r3, .L2236
	ldr	r2, [r3, #-2016]
	mov	r3, r0, asl #1
	mov	r7, r0
	ldrh	r2, [r2, r3]
	cmp	r2, #0
	bne	.L2230
	ldr	r0, .L2236+8
	mov	r1, r7
	bl	printk
.L2230:
	mov	r0, r7
	bl	decrement_vpc_count
.L2229:
	add	sl, sl, #1
	add	r6, r6, #36
	b	.L2217
.L2235:
	ldr	r5, .L2236+4
	mov	r6, #0
	mov	sl, r6
	mov	r9, r5
.L2217:
	ldr	r3, [r5, #3968]
	cmp	sl, r3
	bcc	.L2231
	movw	r4, #16386
	ldr	r6, .L2236
	ldr	r5, .L2236+12
	b	.L2232
.L2233:
	mov	r0, #1
	mov	r1, r0
	bl	ftl_do_gc
	subs	r4, r4, #1
	beq	.L2222
.L2232:
	ldrh	r3, [r6, r5]
	cmp	r3, #0
	bne	.L2233
.L2222:
	ldr	r3, .L2236+4
	mov	r2, #0
	str	r2, [r3, #3968]
.L2214:
	mov	r0, #0
	ldmfd	sp!, {r2, r3, r4, r5, r6, r7, r8, r9, sl, pc}
.L2237:
	.align	2
.L2236:
	.word	.LANCHOR2
	.word	.LANCHOR0
	.word	.LC151
	.word	-1138
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
	ldr	r3, .L2242
	ldr	r3, [r3, #4040]
	cmp	r3, #0
	bne	.L2241
	bl	FtlCacheWriteBack
	bl	l2p_flush
	mov	r0, #1
	bl	FtlEctTblFlush
	bl	FtlVpcTblFlush
.L2241:
	mov	r0, #0
	ldmfd	sp!, {r3, pc}
.L2243:
	.align	2
.L2242:
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
	ldr	r3, .L2246
	ldr	r3, [r3, #2996]
	cmp	r3, #1
	bne	.L2245
	bl	FtlSysFlush
.L2245:
	mov	r0, #0
	ldmfd	sp!, {r3, pc}
.L2247:
	.align	2
.L2246:
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
	ldr	r0, .L2250
	bl	printk
	ldmfd	sp!, {r3, lr}
	b	ftl_deinit
.L2251:
	.align	2
.L2250:
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
	ldr	r3, .L2262
	stmfd	sp!, {r4, r5, r6, r7, r8, sl, lr}
	.save {r4, r5, r6, r7, r8, sl, lr}
	.pad #52
	sub	sp, sp, #52
	ldrb	r3, [r3, #928]	@ zero_extendqisi2
	cmp	r3, #0
	beq	.L2253
	ldr	r4, .L2262+4
	movw	r3, #294
	ldr	r0, .L2262+8
	movw	r7, #4097
	ldrh	r6, [r4, r3]
	ldr	r3, [r4, #-2016]
	mov	r5, r6, asl #1
	mov	r1, r6
	ldrh	r2, [r3, r5]
	bl	printk
	ldr	r3, .L2262+12
	ldrh	r0, [r4, r3]
	bl	FtlGcRefreshOpenBlock
	ldr	r3, .L2262+16
	ldrh	r0, [r4, r3]
	bl	FtlGcRefreshOpenBlock
	ldr	r0, .L2262+20
	bl	allocate_new_data_superblock
	ldr	r0, .L2262+24
	bl	allocate_new_data_superblock
	b	.L2255
.L2257:
	mov	r0, #1
	mov	r1, r0
	bl	ftl_do_gc
	ldr	r3, [r4, #-2016]
	ldrh	r3, [r3, r5]
	cmp	r3, #0
	beq	.L2256
.L2255:
	subs	r7, r7, #1
	bne	.L2257
.L2256:
	ldr	r4, .L2262+4
	mov	r1, r6
	ldr	r0, .L2262+8
	ldr	r3, [r4, #-2016]
	ldrh	r2, [r3, r5]
	bl	printk
	ldr	r3, [r4, #-2016]
	ldrh	r4, [r3, r5]
	cmp	r4, #0
	bne	.L2258
	add	r7, sp, #48
	mov	r0, sp
	strh	r6, [r7, #-48]!	@ movhi
	add	r7, r7, #14
	bl	make_superblock
	ldr	r3, .L2262
	movw	r2, #3848
	movw	lr, #65535
	mov	ip, #36
	mov	r1, r4
	ldrh	r8, [r3, r2]
	ldr	sl, [r3, #4044]
	mov	r3, r4
	b	.L2259
.L2261:
	ldrh	r0, [r7, #2]!
	cmp	r0, lr
	beq	.L2260
	mla	r2, ip, r4, sl
	add	r4, r4, #1
	mov	r0, r0, asl #10
	uxth	r4, r4
	stmib	r2, {r0, r1}
	str	r1, [r2, #12]
.L2260:
	add	r3, r3, #1
	uxth	r3, r3
.L2259:
	cmp	r3, r8
	bne	.L2261
	ldr	r3, .L2262+4
	mov	r1, r6
	ldr	r0, .L2262+28
	ldr	r3, [r3, #-2016]
	ldrh	r2, [r3, r5]
	ldr	r5, .L2262
	bl	printk
	mov	r1, #0
	mov	r2, r4
	ldr	r0, [r5, #4044]
	bl	FlashEraseBlocks
	ldr	r0, [r5, #4044]
	mov	r1, #1
	mov	r2, r4
	bl	FlashEraseBlocks
.L2258:
	ldr	r2, .L2262+4
	movw	r3, #294
	mvn	r1, #0
	strh	r1, [r2, r3]	@ movhi
.L2253:
	add	sp, sp, #52
	ldmfd	sp!, {r4, r5, r6, r7, r8, sl, pc}
.L2263:
	.align	2
.L2262:
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
.L2265:
	stmfd	sp!, {r4, r5, r6, lr}
	.save {r4, r5, r6, lr}
	mvn	r3, #0
	ldr	r4, .L2275
	ldr	r6, .L2275+4
	ldr	r5, .L2275+8
	ldr	r1, .L2275+12
	str	r3, [r6, #2996]
	mov	r3, #0
	ldr	r0, .L2275+16
	str	r3, [r4, #4040]
	str	r3, [r5, #444]
	bl	printk
	ldr	r0, .L2275+20
	bl	FtlConstantsInit
	bl	FtlMemInit
	bl	FtlVariablesInit
	ldr	r0, [r4, #3852]
	uxth	r0, r0
	bl	FtlFreeSysBlkQueueInit
	bl	FtlLoadBbt
	cmp	r0, #0
	ldrne	r0, .L2275+24
	bne	.L2274
	bl	FtlSysBlkInit
	cmp	r0, #0
	beq	.L2268
	ldr	r0, .L2275+28
.L2274:
	ldr	r1, .L2275+32
	bl	printk
	b	.L2267
.L2268:
	mov	r1, #1
	str	r1, [r6, #2996]
	bl	ftl_do_gc
	ldr	r3, .L2275+36
	ldrh	r3, [r5, r3]
	cmp	r3, #15
	bhi	.L2269
	mov	r4, #1024
.L2270:
	mov	r0, #1
	mov	r1, r0
	bl	ftl_do_gc
	subs	r4, r4, #1
	bne	.L2270
	b	.L2267
.L2269:
	ldrb	r3, [r4, #928]	@ zero_extendqisi2
	cmp	r3, #0
	beq	.L2267
	mov	r4, #128
.L2271:
	mov	r0, #1
	mov	r1, r0
	bl	ftl_do_gc
	subs	r4, r4, #1
	bne	.L2271
.L2267:
	mov	r0, #0
	ldmfd	sp!, {r4, r5, r6, pc}
.L2276:
	.align	2
.L2275:
	.word	.LANCHOR0
	.word	.LANCHOR1
	.word	.LANCHOR2
	.word	.LC79
	.word	.LC78
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
	ldr	r5, .L2287
	mov	r7, r0
	mov	r4, r1
	ldr	r3, [r5, #3956]
	cmp	r2, r3
	mvnhi	r0, #0
	bhi	.L2278
	cmp	r1, #31
	bls	.L2285
	ldr	r3, [r5, #4040]
	cmp	r3, #0
	bne	.L2285
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
	beq	.L2279
	rsb	r6, r7, r6
	add	r5, r0, #1
	cmp	r6, r4
	movcs	r6, r4
	uxth	r6, r6
	rsb	r4, r6, r4
.L2279:
	mvn	r3, #0
	ldr	r7, .L2287
	str	r3, [sp, #4]
	movw	r6, #3922
	b	.L2280
.L2282:
	mov	r0, r5
	mov	r1, sp
	mov	r2, #0
	bl	log2phys
	ldr	r3, [sp, #0]
	cmn	r3, #1
	beq	.L2281
	ldr	r3, .L2287+4
	add	r1, sp, #4
	mov	r0, r5
	ldr	r2, [r3, #448]
	add	r2, r2, #1
	str	r2, [r3, #448]
	ldr	r2, [r3, #-1832]
	add	r2, r2, #1
	str	r2, [r3, #-1832]
	mov	r2, #1
	bl	log2phys
	ldr	r0, [sp, #0]
	ubfx	r0, r0, #10, #16
	bl	P2V_block_in_plane
	bl	decrement_vpc_count
.L2281:
	ldrh	r3, [r7, r6]
	add	r5, r5, #1
	rsb	r4, r3, r4
.L2280:
	ldrh	r3, [r7, r6]
	cmp	r4, r3
	bcs	.L2282
	ldr	r3, .L2287+4
	mov	r4, #0
	ldr	r2, [r3, #448]
	cmp	r2, #32
	bls	.L2286
	str	r4, [r3, #448]
	bl	l2p_flush
	bl	FtlVpcTblFlush
	b	.L2286
.L2285:
	mov	r0, #0
	b	.L2278
.L2286:
	mov	r0, r4
.L2278:
	ldmfd	sp!, {r1, r2, r3, r4, r5, r6, r7, pc}
.L2288:
	.align	2
.L2287:
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
	.type	ftl_write.part.24, %function
ftl_write.part.24:
	.fnstart
	@ args = 0, pretend = 0, frame = 96
	@ frame_needed = 0, uses_anonymous_args = 0
	stmfd	sp!, {r4, r5, r6, r7, r8, r9, sl, fp, lr}
	.save {r4, r5, r6, r7, r8, r9, sl, fp, lr}
	mov	r3, #2048
	ldr	r4, .L2335
	.pad #100
	sub	sp, sp, #100
	ldr	r8, .L2335+4
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
	beq	.L2323
	ldr	r3, [r8, #3972]
	sub	sl, sl, #1
	mov	r2, #36
	ldr	r0, [sp, #16]
	mla	sl, r2, sl, r3
	ldr	r3, [sl, #16]
	cmp	r0, r3
	strne	r5, [sp, #32]
	bne	.L2292
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
	bne	.L2293
	ldr	r2, [r4, #456]
	cmp	r2, #2
	ble	.L2294
.L2293:
	add	r9, r9, r3
	add	r7, r7, r8
	ldr	r3, [sp, #16]
	rsb	r1, r8, r5
	str	fp, [sp, #20]
	add	r3, r3, #1
	str	r1, [sp, #32]
	str	r3, [sp, #16]
.L2292:
	ldr	r3, .L2335
	mov	r2, #0
	str	r2, [r3, #456]
	b	.L2291
.L2323:
	str	r5, [sp, #32]
.L2291:
	ldr	r0, [sp, #16]
	ldr	r1, [sp, #36]
	bl	FtlCacheMetchLpa
	cmp	r0, #0
	beq	.L2295
	bl	FtlCacheWriteBack
.L2295:
	cmp	r5, r6, asl #1
	ldr	r3, .L2335
	ldr	r4, .L2335+4
	mov	fp, #0
	movcc	r6, #0
	movcs	r6, #1
	str	r6, [sp, #24]
	ldr	r6, .L2335+8
	ldr	r5, [sp, #16]
	mov	sl, r4
	str	fp, [sp, #40]
	str	r6, [r3, #440]
	b	.L2332
.L2321:
	ldrh	r1, [r6, #4]
	cmp	r1, #0
	bne	.L2297
	ldr	r2, .L2335+8
	ldr	r3, .L2335+12
	cmp	r6, r2
	bne	.L2298
	ldr	r2, .L2335+16
	ldrh	r8, [r8, r2]
	cmp	r8, #0
	bne	.L2299
	ldr	r0, .L2335+20
	str	r3, [sp, #4]
	bl	allocate_new_data_superblock
	ldr	r3, [sp, #4]
	str	r8, [r3, #3004]
.L2299:
	ldr	r0, .L2335+8
	str	r3, [sp, #4]
	bl	allocate_new_data_superblock
	ldr	r3, [sp, #4]
	ldr	r2, [r3, #3004]
	ldr	r3, .L2335+20
	cmp	r2, #0
	movne	r6, r3
	b	.L2300
.L2298:
	str	r1, [r3, #3004]
	ldr	r3, .L2335+24
	ldrh	r3, [r8, r3]
	cmp	r3, #0
	movne	r6, r2
	bne	.L2300
	mov	r0, r6
	bl	allocate_new_data_superblock
.L2300:
	ldrh	r3, [r6, #4]
	cmp	r3, #0
	bne	.L2301
	mov	r0, r6
	bl	allocate_new_data_superblock
.L2301:
	ldr	r3, .L2335
	str	r6, [r3, #440]
.L2297:
	ldr	r2, .L2335
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
	b	.L2302
.L2318:
	ldrh	r3, [r8, #4]
	cmp	r3, #0
	beq	.L2303
	ldr	r6, [sp, #36]
	rsb	r2, r6, r5
	ldr	r6, [sp, #24]
	rsbs	r3, r2, #0
	adc	r3, r3, r2
	tst	r3, r6
	beq	.L2304
	ldr	r6, [sp, #28]
	cmp	r6, #0
	beq	.L2304
	movw	lr, #3922
	ldr	r6, [sp, #32]
	ldrh	r2, [r4, lr]
	add	r1, r6, r7
	mls	r1, r2, r5, r1
	cmp	r1, r2
	bne	.L2303
.L2304:
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
	ldr	r6, .L2335
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
	ldr	r0, .L2335
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
	beq	.L2305
	ldr	r2, [sp, #12]
	cmp	r2, #0
	beq	.L2306
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
	b	.L2307
.L2306:
	cmp	r3, #0
	beq	.L2307
	ldr	r3, [sp, #32]
	movw	lr, #3922
	add	fp, r3, r7
	ldrh	r3, [r4, lr]
	mls	fp, r3, r5, fp
	ldr	r3, [sp, #12]
	str	r3, [sp, #40]
	uxth	fp, fp
.L2307:
	movw	lr, #3922
	ldrh	r3, [r4, lr]
	cmp	fp, r3
	bne	.L2308
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
	bne	.L2311
	ldr	r0, [r3, #8]
	movw	r3, #3926
	ldrh	r2, [sl, r3]
	b	.L2333
.L2308:
	ldr	r2, [sp, #56]
	mov	r3, #36
	cmn	r2, #1
	beq	.L2312
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
	ldr	r3, .L2335
	ldreq	r2, [r3, #-1636]
	addeq	r2, r2, #1
	streq	r2, [r3, #-1636]
	beq	.L2314
	ldr	r2, [r6, #8]
	cmp	r2, r5
	beq	.L2314
	ldr	r2, [r3, #-1636]
	ldr	r0, .L2335+28
	add	r2, r2, #1
	str	r2, [r3, #-1636]
	mov	r2, r5
	ldr	r1, [r6, #8]
	bl	printk
	b	.L2314
.L2312:
	ldr	r1, [r4, #3968]
	ldr	r2, [r4, #3972]
	mla	r3, r3, r1, r2
	mov	r1, #0
	ldr	r0, [r3, #8]
	movw	r3, #3926
	ldrh	r2, [r4, r3]
	bl	ftl_memset
.L2314:
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
	b	.L2333
.L2305:
	ldr	r3, [sp, #24]
	ldr	r2, [r4, #3968]
	cmp	r3, #0
	ldr	r3, [r4, #3972]
	mla	ip, ip, r2, r3
	beq	.L2316
	movw	lr, #3922
	ldrh	r3, [r4, lr]
	mul	r3, r3, r5
	rsb	r3, r7, r3
	add	r3, r9, r3, asl #9
	str	r3, [ip, #8]
	b	.L2311
.L2316:
	movw	r0, #3922
	movw	r3, #3926
	ldrh	r1, [r4, r0]
	ldrh	r2, [r4, r3]
	ldr	r0, [ip, #8]
	mul	r1, r1, r5
	rsb	r1, r7, r1
	add	r1, r9, r1, asl #9
.L2333:
	bl	memcpy
.L2311:
	ldr	r3, .L2335+32
	ldr	r0, [sp, #48]
	ldr	lr, [sp, #44]
	strh	r3, [r0, lr]	@ movhi
	ldr	r3, .L2335
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
.L2302:
	ldr	r6, [sp, #28]
	ldr	r3, [sp, #52]
	cmp	r6, r3
	bne	.L2318
.L2303:
	ldr	r3, [sp, #20]
	mov	r6, r8
	ldr	lr, [sp, #28]
	ldr	r2, [r4, #3968]
	rsb	r3, lr, r3
	str	r3, [sp, #20]
	ldr	r3, .L2335
	ldr	r0, [sp, #24]
	ldr	r3, [r3, #-976]
	cmp	r2, r3
	orrcs	r0, r0, #1
	uxtb	r3, r0
	cmp	r3, #0
	bne	.L2319
	ldrh	r2, [r8, #4]
	cmp	r2, #0
	bne	.L2329
.L2319:
	bl	FtlCacheWriteBack
	ldr	lr, [sp, #20]
	mov	r3, #0
	str	r3, [sl, #3968]
	cmp	lr, #1
	ldr	r3, [sp, #24]
	movls	r3, #0
.L2329:
	str	r3, [sp, #24]
.L2332:
	ldr	r3, [sp, #20]
	ldr	r8, .L2335
	cmp	r3, #0
	bne	.L2321
	mov	r0, r3
	ldr	r6, [sp, #36]
	ldr	r3, [sp, #16]
	rsb	r1, r3, r6
	bl	ftl_do_gc
	ldr	r3, .L2335+36
	ldrh	r3, [r8, r3]
	cmp	r3, #31
	bhi	.L2294
	ldr	r2, .L2335+40
	mov	r3, #128
	ldr	r5, .L2335+4
	mov	r4, #16
	strh	r3, [r8, r2]	@ movhi
	sub	r2, r2, #2
	strh	r3, [r8, r2]	@ movhi
.L2322:
	mov	r0, #0
	mov	r1, #1
	bl	ftl_do_gc
	ldr	r3, [r5, #4040]
	cmp	r3, #0
	bne	.L2294
	subs	r4, r4, #1
	bne	.L2322
.L2294:
	mov	r0, #0
	add	sp, sp, #100
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, sl, fp, pc}
.L2336:
	.align	2
.L2335:
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
	.size	ftl_write.part.24, .-ftl_write.part.24
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
	ldr	r3, .L2342
	stmfd	sp!, {r4, lr}
	.save {r4, lr}
	ldr	r4, [r3, #4040]
	cmp	r4, #0
	movne	r0, #0
	ldmnefd	sp!, {r4, pc}
	cmp	ip, #16
	bne	.L2339
	add	r0, r0, #256
	ldmfd	sp!, {r4, lr}
	b	FtlVendorPartWrite
.L2339:
	ldr	r3, [r3, #3956]
	add	ip, r1, r0
	cmp	ip, r3
	bhi	.L2341
	ldmfd	sp!, {r4, lr}
	b	ftl_write.part.24
.L2341:
	mvn	r0, #0
	ldmfd	sp!, {r4, pc}
.L2343:
	.align	2
.L2342:
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
	bne	.L2350
	add	r0, r1, #256
	mov	r1, r2
	mov	r2, r3
	bl	FtlVendorPartRead
	str	r0, [sp, #16]
	b	.L2351
.L2350:
	ldr	r3, [sp, #28]
	add	r3, r3, r1
	str	r3, [sp, #20]
	ldr	r3, .L2380
	ldr	r1, [sp, #20]
	ldr	r2, [r3, #3956]
	cmp	r1, r2
	mvnhi	r3, #0
	strhi	r3, [sp, #16]
	bhi	.L2351
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
	ldr	r3, .L2380+4
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
	beq	.L2352
	bl	FtlCacheWriteBack
.L2352:
	mov	r9, #0
	ldr	r6, [sp, #12]
	str	r9, [sp, #32]
	mov	r5, r9
	str	r9, [sp, #16]
	ldr	r7, .L2380
	b	.L2375
.L2369:
	mov	r2, #0
	mov	r0, r6
	add	r1, sp, #60
	bl	log2phys
	ldr	r2, [sp, #60]
	cmn	r2, #1
	bne	.L2376
	b	.L2379
.L2357:
	mla	r0, r0, r6, sl
	cmp	r0, r4
	bcc	.L2356
	ldr	r2, [sp, #20]
	cmp	r0, r2
	bcs	.L2356
	rsb	r0, r4, r0
	mov	r1, #0
	mov	r2, #512
	str	r3, [sp, #4]
	add	r0, r8, r0, asl #9
	bl	ftl_memset
	ldr	r3, [sp, #4]
.L2356:
	add	sl, sl, #1
	b	.L2354
.L2379:
	mov	sl, #0
	movw	r3, #3922
.L2354:
	ldrh	r0, [r7, r3]
	cmp	sl, r0
	bcc	.L2357
	b	.L2358
.L2376:
	ldr	r3, .L2380+4
	mov	sl, #36
	ldr	r1, [r3, #-972]
	mla	sl, sl, r5, r1
	ldr	r1, [sp, #12]
	cmp	r6, r1
	str	r2, [sl, #4]
	movw	r2, #3922
	bne	.L2359
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
	b	.L2360
.L2359:
	cmp	r6, fp
	ldrneh	r3, [r7, r2]
	mulne	r3, r3, r6
	bne	.L2377
	ldr	r3, [r3, #-940]
	ldrh	r2, [r7, r2]
	ldr	r1, [sp, #20]
	str	r3, [sl, #8]
	mul	r3, r2, r6
	rsb	r9, r3, r1
	cmp	r9, r2
	bne	.L2360
.L2377:
	rsb	r3, r4, r3
	add	r3, r8, r3, asl #9
	str	r3, [sl, #8]
.L2360:
	movw	r3, #3928
	ldr	r2, .L2380+4
	ldrh	r3, [r7, r3]
	str	r6, [sl, #16]
	ldr	r2, [r2, #-932]
	mul	r3, r3, r5
	add	r5, r5, #1
	bic	r3, r3, #3
	add	r3, r2, r3
	str	r3, [sl, #12]
.L2358:
	ldr	r3, [sp, #8]
	add	r6, r6, #1
	subs	r3, r3, #1
	str	r3, [sp, #8]
	beq	.L2362
	movw	r3, #3848
	ldrh	r3, [r7, r3]
	cmp	r5, r3, asl #3
	bne	.L2375
.L2362:
	cmp	r5, #0
	beq	.L2375
	ldr	sl, .L2380+4
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
.L2368:
	ldr	r3, [sp, #24]
	mov	r5, #36
	ldr	r1, [sp, #12]
	mul	r5, r5, r3
	ldr	r3, [sl, #-972]
	add	r3, r3, r5
	ldr	r2, [r3, #16]
	cmp	r2, r1
	bne	.L2364
	ldr	r1, [r3, #8]
	ldr	r3, [sl, #-944]
	cmp	r1, r3
	bne	.L2365
	ldr	r3, [sp, #40]
	mov	r0, r8
	ldr	r2, [sp, #44]
	add	r1, r1, r3
	b	.L2378
.L2364:
	cmp	r2, fp
	bne	.L2365
	ldr	r1, [r3, #8]
	ldr	r3, [sl, #-940]
	cmp	r1, r3
	bne	.L2365
	movw	r2, #3922
	ldrh	r0, [r7, r2]
	ldr	r2, [sp, #48]
	mul	r0, r0, fp
	rsb	r0, r4, r0
	add	r0, r8, r0, asl #9
.L2378:
	bl	memcpy
.L2365:
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
	bne	.L2367
	ldr	r0, [r0, #4]
	ubfx	r0, r0, #10, #16
	bl	P2V_block_in_plane
	bl	FtlGcRefreshBlock
.L2367:
	ldr	r3, [sp, #24]
	add	r3, r3, #1
	str	r3, [sp, #24]
	cmp	r3, r6
	bne	.L2368
	ldr	r6, [sp, #52]
	mov	r5, #0
.L2375:
	ldr	r3, [sp, #8]
	cmp	r3, #0
	bne	.L2369
	ldr	r3, .L2380+8
	ldr	r2, .L2380+4
	ldrh	r3, [r2, r3]
	cmp	r3, #0
	beq	.L2351
	ldr	r0, [sp, #8]
	mov	r1, #1
	bl	ftl_do_gc
.L2351:
	ldr	r0, [sp, #16]
	add	sp, sp, #68
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, sl, fp, pc}
.L2381:
	.align	2
.L2380:
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
	.type	rk_ftl_vendor_ops.constprop.28, %function
rk_ftl_vendor_ops.constprop.28:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	@ link register save eliminated.
	cmp	r2, #0
	mov	r3, r0
	mov	r2, r3
	mov	r0, r1
	mov	r1, #128
	beq	.L2388
	b	FlashBootVendorWrite
.L2388:
	b	FlashBootVendorRead
	.fnend
	.size	rk_ftl_vendor_ops.constprop.28, .-rk_ftl_vendor_ops.constprop.28
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
	ldr	r1, .L2400
	ldr	r0, .L2400+4
	bl	printk
	ldr	r0, .L2400+8
	mov	r1, #8192
	ldr	r6, .L2400+12
	bl	__memzero
	ldr	r5, .L2400+16
	b	.L2390
.L2392:
	mov	r0, r4
	add	r1, sp, #4
	mov	r2, #0
	bl	log2phys
	ldr	r0, [sp, #4]
	cmn	r0, #1
	beq	.L2391
	ubfx	r0, r0, #10, #16
	bl	P2V_block_in_plane
	add	r0, r5, r0, asl #1
	add	r0, r0, #460
	ldrh	r3, [r0, #0]
	add	r3, r3, #1
	strh	r3, [r0, #0]	@ movhi
.L2391:
	add	r4, r4, #1
.L2390:
	ldr	r3, [r6, #3976]
	ldr	r7, .L2400+12
	cmp	r4, r3
	bcc	.L2392
	ldr	r5, .L2400+16
	mov	r6, #3856
	ldr	r0, .L2400+20
	mov	r2, #2
	ldrh	r3, [r7, r6]
	mov	r4, #0
	ldr	r1, [r5, #-2016]
	bl	rknand_print_hex
	ldr	r0, .L2400+24
	add	r1, r5, #460
	mov	r2, #2
	ldrh	r3, [r7, r6]
	bl	rknand_print_hex
	b	.L2393
.L2395:
	ldr	r2, [r5, #-2016]
	mov	r3, r4, asl #1
	ldrh	r2, [r2, r3]
	add	r3, r5, r3
	add	r3, r3, #460
	ldrh	r3, [r3, #0]
	cmp	r2, r3
	beq	.L2394
	ldr	r0, .L2400+28
	mov	r1, r4
	bl	printk
.L2394:
	add	r4, r4, #1
	uxth	r4, r4
.L2393:
	ldrh	r3, [r7, r6]
	cmp	r3, r4
	ldr	r3, .L2400+16
	bhi	.L2395
	ldr	r2, [r3, #-2004]
	cmp	r2, #0
	beq	.L2389
	ldr	r4, [r3, #-2024]
	mov	r6, #0
	ldr	r1, .L2400+32
	mov	r5, r3
	rsb	r2, r4, r2
	ldr	r4, .L2400+36
	mov	r8, #6
	movw	sl, #65535
	mov	r2, r2, asr #1
	ldrh	r7, [r3, r1]
	mul	r4, r4, r2
	uxth	r4, r4
	b	.L2397
.L2399:
	ldr	r2, [r5, #-2016]
	mov	r3, r4, asl #1
	ldrh	r2, [r2, r3]
	cmp	r2, #0
	beq	.L2398
	add	r3, r5, r3
	ldr	r0, .L2400+40
	add	r3, r3, #460
	mov	r1, r4
	ldrh	r3, [r3, #0]
	bl	printk
.L2398:
	mul	r4, r8, r4
	ldr	r3, [r5, #-2024]
	ldrh	r4, [r3, r4]
	cmp	r4, sl
	beq	.L2389
	add	r6, r6, #1
	uxth	r6, r6
.L2397:
	cmp	r6, r7
	bne	.L2399
.L2389:
	ldmfd	sp!, {r1, r2, r3, r4, r5, r6, r7, r8, sl, pc}
.L2401:
	.align	2
.L2400:
	.word	.LANCHOR3+77
	.word	.LC101
	.word	.LANCHOR2+460
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
	ldr	r3, .L2487
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
	bne	.L2403
	mvn	r2, #88
	strb	r2, [r3, #4]
	ldr	r3, .L2487+4
	mvn	r2, #8
	strb	r2, [r3, #3025]
	b	.L2459
.L2403:
	cmp	r4, #3
	bne	.L2405
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
	b	.L2482
.L2405:
	cmp	r4, #4
	bne	.L2406
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
.L2482:
	mov	r6, #8
	strb	r2, [r3, #11]
	mov	r5, r6
	b	.L2404
.L2406:
	cmp	r4, #5
	bne	.L2407
	mov	r2, #56
	mov	r6, #8
	strb	r2, [r3, #4]
	mov	r2, #57
	strb	r2, [r3, #5]
	mov	r2, #58
	strb	r2, [r3, #6]
	mov	r2, #59
	strb	r2, [r3, #7]
	b	.L2481
.L2407:
	cmp	r4, #6
	bne	.L2408
	mov	r2, #14
	mov	r6, #12
	strb	r2, [r3, #4]
	mov	r2, #15
	strb	r2, [r3, #5]
	mov	r2, #16
	strb	r2, [r3, #6]
	mov	r2, #17
	strb	r2, [r3, #7]
	b	.L2481
.L2408:
	cmp	r4, #7
	bne	.L2459
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
	b	.L2404
.L2459:
	mov	r6, #7
.L2481:
	mov	r5, #4
.L2404:
	sub	r3, r4, #1
	cmp	r3, #1
	bhi	.L2477
	b	.L2485
.L2415:
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
.L2411:
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
	bcc	.L2411
	mov	r2, #0
	b	.L2412
.L2413:
	ldrb	r7, [r0, r1, asl #2]	@ zero_extendqisi2
	ldrb	r5, [r4, r2]	@ zero_extendqisi2
	add	r5, r7, r5
	strb	r5, [ip, r1, asl #3]
	add	r1, r1, #1
	cmp	r1, #7
	bne	.L2413
	add	r2, r2, #1
	cmp	r2, #4
	beq	.L2414
.L2412:
	mov	r1, #1
	add	ip, r4, r2
	add	r0, r2, r9
	b	.L2413
.L2414:
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
	b	.L2409
.L2485:
	ldr	r9, .L2487+8
	mov	r3, r6
	mov	r8, #0
	mov	r6, r5
.L2409:
	ldr	fp, .L2487
	ldrb	r2, [fp, #3762]	@ zero_extendqisi2
	cmp	r2, r8
	bhi	.L2415
	mov	r5, r6
	mov	r6, r3
	b	.L2416
.L2477:
	sub	r3, r4, #3
	cmp	r3, #4
	bhi	.L2416
	mul	r3, r6, r5
	mov	sl, #0
	mov	r7, sl
	str	r6, [sp, #48]
	mov	r2, r3, asr #2
	mov	r3, r3, asl #4
	str	r2, [sp, #24]
	str	r3, [sp, #52]
	b	.L2417
.L2458:
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
	bne	.L2420
	mov	r3, #255
	str	r3, [r8, #4]
	mov	r3, #64
	str	r3, [r9, fp, asl #8]
	mov	r3, #204
	b	.L2483
.L2420:
	sub	r3, r4, #5
	cmp	r3, #1
	ldrls	r3, .L2487
	ldrlsb	r3, [r3, #4]	@ zero_extendqisi2
	strls	r3, [r8, #4]
	movls	r3, #82
	bls	.L2484
.L2422:
	mov	r3, #174
	str	r3, [r8, #4]
	mov	r3, #176
	str	r7, [r9, fp, asl #8]
.L2483:
	str	r3, [r8, #4]
	mov	r3, #77
.L2484:
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
	bls	.L2426
	cmp	r4, #7
	moveq	r1, #32
	movne	r1, #2
.L2426:
	ldr	r3, .L2487+12
	ldr	r2, [r3, #148]
	mov	r3, #0
.L2427:
	ldr	r0, [r8, #0]
	strb	r0, [r2, r3]
	add	r3, r3, #1
	uxtb	r0, r3
	cmp	r0, r1
	bcc	.L2427
	cmp	r4, #7
	bne	.L2428
	mov	r3, #0
.L2430:
	ldrb	r1, [r2, #0]	@ zero_extendqisi2
	cmp	r1, #12
	beq	.L2429
	ldrb	r1, [r2, #1]	@ zero_extendqisi2
	cmp	r1, #10
	beq	.L2429
	add	r3, r3, #1
	add	r2, r2, #4
	uxtb	r3, r3
	cmp	r3, #8
	bne	.L2430
	b	.L2431
.L2429:
	cmp	r3, #6
	bls	.L2432
.L2431:
	ldr	r0, .L2487+16
	mov	r1, #0
	bl	printk
.L2433:
	b	.L2433
.L2428:
	cmp	r4, #6
	bne	.L2432
	mov	r1, #8
.L2434:
	ldrb	r3, [r2], #1	@ zero_extendqisi2
	cmp	r3, #12
	beq	.L2432
	ldrb	r3, [r2, #7]	@ zero_extendqisi2
	cmp	r3, #4
	beq	.L2432
	sub	r1, r1, #1
	uxtb	r1, r1
	cmp	r1, #0
	bne	.L2434
	b	.L2486
.L2436:
	b	.L2436
.L2432:
	ldr	r3, .L2487+12
	mov	r2, #0
	ldr	ip, [r3, #148]
	str	ip, [sp, #40]
.L2437:
	ldr	r1, [r8, #0]
	ldr	lr, [sp, #40]
	ldr	ip, [sp, #52]
	strb	r1, [lr, r2]
	add	r2, r2, #1
	cmp	r2, ip
	blt	.L2437
	ldr	r2, [sp, #24]
	ldr	r1, [r3, #148]
	mov	r3, #8
	mov	ip, r2, asl #2
	mov	lr, r2, asl #3
	add	r1, r1, ip
	str	ip, [sp, #32]
.L2439:
	mov	r2, #0
	mov	r0, r2
.L2438:
	ldr	ip, [r1, r2]
	add	r0, r0, #1
	mvn	ip, ip
	str	ip, [r1, r2]
	ldr	ip, [sp, #24]
	add	r2, r2, #4
	cmp	r0, ip
	blt	.L2438
	subs	r3, r3, #1
	add	r1, r1, lr
	bne	.L2439
	str	r3, [sp, #16]
	str	sl, [sp, #60]
	b	.L2440
.L2444:
	mov	lr, #1
	mov	r0, #0
	mov	ip, lr, asl r2
	mov	lr, r0
	mov	sl, #16
	str	r3, [sp, #4]
	str	sl, [sp, #20]
.L2442:
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
	bne	.L2442
	cmp	lr, #8
	add	r2, r2, #1
	orrhi	r1, r1, ip
	cmp	r2, #32
	ldr	r3, [sp, #4]
	bne	.L2444
	ldr	ip, [sp, #16]
	ldr	r2, [sp, #24]
	add	ip, ip, #1
	ldr	sl, [sp, #44]
	cmp	ip, r2
	str	ip, [sp, #16]
	str	r1, [sl, r3]
	add	r3, r3, #4
	bge	.L2445
.L2440:
	ldr	sl, .L2487+12
	mov	r1, #0
	mov	r2, r1
	ldr	sl, [sl, #148]
	add	ip, sl, r3
	str	ip, [sp, #56]
	str	sl, [sp, #44]
	b	.L2444
.L2445:
	ldr	r3, .L2487+12
	ldr	sl, [sp, #60]
	ldr	r1, [r3, #148]
	mov	r3, #0
	mov	r2, r3
.L2447:
	ldr	r0, [r1, r3]
	add	r3, r3, #4
	cmp	r0, #0
	addeq	r2, r2, #1
	cmp	r3, #32
	bne	.L2447
	cmp	r2, #7
	ble	.L2448
	ldr	r0, .L2487+20
	mov	r2, #1
	mov	r3, #1024
	bl	rknand_print_hex
	ldr	r0, .L2487+16
	mov	r1, #0
	bl	printk
.L2449:
	b	.L2449
.L2448:
	cmp	r4, #6
	moveq	r0, #4
	streq	r0, [sp, #16]
	beq	.L2450
	cmp	r4, #7
	moveq	lr, #10
	movne	lr, #8
	str	lr, [sp, #16]
.L2450:
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
	b	.L2453
.L2454:
	ldrb	lr, [ip], #1	@ zero_extendqisi2
	strb	lr, [r1, r3]
	add	r3, r3, #1
	uxtb	lr, r3
	cmp	lr, r5
	bcc	.L2454
	ldr	r3, [sp, #20]
	add	r2, r2, #1
	ldr	ip, [sp, #16]
	add	r0, r0, r3
	ldr	r3, [sp, #48]
	add	r1, r1, ip
	cmp	r2, r3
	bge	.L2455
.L2453:
	mov	ip, r0
	mov	r3, #0
	b	.L2454
.L2455:
	mov	r3, #255
	mov	r0, r6
	str	r3, [r8, #8]
	bl	NandcWaitFlashReady
	ldr	ip, [sp, #36]
	cmp	ip, #1
	movhi	r3, #56
	strhi	r3, [r8, #8]
	bhi	.L2457
	mov	r3, #54
	str	r3, [r8, #8]
	ldr	r3, .L2487
	mov	r0, sl
	mvn	r1, #0
	ldrb	r3, [r3, #4]	@ zero_extendqisi2
	str	r3, [r8, #4]
	mov	r3, #22
	str	r7, [r9, fp, asl #8]
	str	r3, [r8, #8]
	bl	FlashReadCmd
.L2457:
	mov	r0, r6
	add	sl, sl, #1
	bl	NandcWaitFlashReady
	uxtb	sl, sl
.L2417:
	ldr	r1, .L2487
	ldrb	r3, [r1, #3762]	@ zero_extendqisi2
	cmp	r3, sl
	bhi	.L2458
	ldr	r6, [sp, #48]
.L2416:
	ldr	r3, .L2487
	strb	r5, [r3, #1]
	strb	r6, [r3, #2]
	add	sp, sp, #68
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, sl, fp, pc}
.L2486:
	ldr	r0, .L2487+16
	bl	printk
	b	.L2436
.L2488:
	.align	2
.L2487:
	.word	.LANCHOR0
	.word	.LANCHOR1
	.word	.LANCHOR1+3008
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
	bhi	.L2491
	ldmfd	sp!, {r3, lr}
	b	HynixGetReadRetryDefault
.L2491:
	cmp	r3, #49
	bne	.L2492
	ldr	r0, .L2493
	mov	r2, #64
	ldr	r1, .L2493+4
	strb	r3, [r0, #0]
	mov	r3, #4
	strb	r3, [r0, #1]
	mov	r3, #15
	strb	r3, [r0, #2]
	add	r0, r0, #4
	bl	memcpy
	ldmfd	sp!, {r3, pc}
.L2492:
	ldmfd	sp!, {r3, lr}
	b	FlashGetReadRetryDefault.part.27
.L2494:
	.align	2
.L2493:
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
	ldr	r5, .L2546
	bl	ftl_malloc
	ldr	r6, .L2546+4
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
	ldr	r5, .L2546+8
	mov	r7, #44
.L2499:
	uxtb	r0, r4
	mov	r1, r5
	bl	FlashReadIDRaw
	cmp	r4, #0
	bne	.L2496
	ldrb	r3, [r6, #2980]	@ zero_extendqisi2
	sub	r3, r3, #1
	uxtb	r3, r3
	cmp	r3, #253
	bhi	.L2537
	ldrb	r3, [r6, #2981]	@ zero_extendqisi2
	cmp	r3, #255
	beq	.L2538
.L2496:
	ldrb	r3, [r5, #0]	@ zero_extendqisi2
	add	r4, r4, #1
	cmp	r3, #181
	streqb	r7, [r5, #0]
	cmp	r4, #4
	add	r5, r5, #8
	bne	.L2499
	ldr	r4, .L2546+4
	ldrb	r3, [r4, #2980]	@ zero_extendqisi2
	cmp	r3, #173
	beq	.L2500
	ldr	r0, [r4, #3784]
	bl	NandcSetDdrMode
.L2500:
	ldr	r5, .L2546+4
	mov	r1, #0
	mov	r2, #852
	mov	r0, r5
	bl	ftl_memset
	ldr	r3, .L2546+12
	str	r3, [r4, #3624]
	mov	r3, #0
	strb	r3, [r4, #860]
	ldrb	r3, [r4, #2981]	@ zero_extendqisi2
	cmp	r3, #161
	cmpne	r3, #241
	beq	.L2501
	cmp	r3, #218
	beq	.L2501
	cmp	r3, #209
	beq	.L2501
	cmp	r3, #220
	bne	.L2502
	ldrb	r2, [r5, #2983]	@ zero_extendqisi2
	cmp	r2, #149
	bne	.L2502
.L2501:
	ldr	r2, .L2546+4
	mov	r1, #1
	mov	r0, #16
	ldr	ip, .L2546+16
	strb	r1, [r2, #852]
	ldr	r1, .L2546
	strb	r0, [r2, #853]
	strb	r3, [ip, #3038]
	strb	r0, [r1, #152]
	ldrb	r0, [r2, #2980]	@ zero_extendqisi2
	cmp	r0, #152
	strb	r0, [ip, #3037]
	bne	.L2503
	ldrb	r0, [r2, #2984]	@ zero_extendqisi2
	sxtb	r0, r0
	cmp	r0, #0
	movge	r0, #24
	strgeb	r0, [r1, #152]
.L2503:
	ldr	r1, [r2, #3776]
	ldr	r2, .L2546+20
	cmp	r1, r2
	ldreq	r2, .L2546
	moveq	r1, #16
	streqb	r1, [r2, #152]
	cmp	r3, #218
	bne	.L2505
	ldr	r3, .L2546+16
	movw	r2, #3050
	mov	r1, #2048
	strh	r1, [r3, r2]	@ movhi
	mvn	r2, #37
	b	.L2543
.L2505:
	cmp	r3, #220
	bne	.L2506
	ldr	r3, .L2546+16
	movw	r2, #3050
	mov	r1, #4096
	strh	r1, [r3, r2]	@ movhi
	mvn	r2, #35
.L2543:
	strb	r2, [r3, #3038]
.L2506:
	ldr	r1, .L2546+24
	mov	r2, #32
	ldr	r0, .L2546+28
	bl	memcpy
	ldr	r0, .L2546+12
	mov	r2, #32
	add	r1, r0, #144
	bl	memcpy
.L2502:
	ldr	r4, .L2546+4
	ldrb	r3, [r4, #852]	@ zero_extendqisi2
	cmp	r3, #0
	bne	.L2507
	bl	FlashLoadPhyInfoInRam
	cmp	r0, #0
	bne	.L2508
	ldr	r3, [r4, #3624]
	ldrh	r0, [r3, #16]
	ubfx	r0, r0, #8, #3
	strb	r0, [r4, #3772]
	tst	r0, #1
	bne	.L2508
	mov	r3, #1
	strb	r3, [r4, #3780]
	bl	FlashSetInterfaceMode
	ldrb	r0, [r4, #3772]	@ zero_extendqisi2
	bl	NandcSetMode
.L2508:
	ldr	r4, .L2546+4
	ldr	r3, [r4, #3624]
	ldrb	r3, [r3, #26]	@ zero_extendqisi2
	strb	r3, [r4, #928]
	bl	FlashLoadPhyInfo
	cmp	r0, #0
	beq	.L2507
	ldr	r3, [r4, #3784]
	cmp	r3, #0
	beq	.L2510
	mov	r0, #1
	bl	FlashSetInterfaceMode
	mov	r0, #1
	b	.L2544
.L2510:
	ldrb	r0, [r4, #3772]	@ zero_extendqisi2
	bl	FlashSetInterfaceMode
	ldrb	r0, [r4, #3772]	@ zero_extendqisi2
.L2544:
	bl	NandcSetMode
	bl	FlashLoadPhyInfo
	cmp	r0, #0
	beq	.L2507
	ldr	r4, .L2546+4
	mov	r0, #1
	bl	FlashSetInterfaceMode
	mov	r0, #1
	bl	NandcSetMode
	ldr	r0, .L2546+32
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
	ldr	r1, .L2546+36
	ldr	r0, .L2546
	ldr	r3, [r4, #3624]
	ldrh	r1, [r0, r1]
	ldrb	r2, [r3, #9]	@ zero_extendqisi2
	add	r1, r1, #4080
	add	r1, r1, #15
	cmp	r2, r1, lsr #12
	blt	.L2513
	ldrh	r1, [r3, #14]
	add	r1, r1, #255
	cmp	r2, r1, lsr #8
	bge	.L2514
.L2513:
	ldrh	r2, [r3, #14]
	bic	r2, r2, #255
	strh	r2, [r3, #14]	@ movhi
.L2514:
	ldr	r3, .L2546+4
	ldrb	r3, [r3, #3772]	@ zero_extendqisi2
	tst	r3, #6
	beq	.L2515
	bl	FlashSavePhyInfo
	ldr	r3, .L2546
	mov	r0, #0
	ldr	r1, [r3, #160]
	bl	FlashDdrParaScan
.L2515:
	bl	FlashSavePhyInfo
.L2507:
	ldr	r4, .L2546+4
	ldr	r5, .L2546
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
	beq	.L2516
	ldrb	r0, [r3, #19]	@ zero_extendqisi2
	ldrb	r3, [r4, #1]	@ zero_extendqisi2
	strb	r0, [r4, #3761]
	strb	r3, [r4, #3760]
	ldrb	r3, [r4, #2]	@ zero_extendqisi2
	strb	r3, [r5, #140]
	sub	r3, r0, #1
	uxtb	r3, r3
	cmp	r3, #6
	bhi	.L2517
	ldr	r3, .L2546+40
	str	r3, [r5, #172]
	sub	r3, r0, #5
	uxtb	r3, r3
	cmp	r3, #1
	movls	r3, #1
	strls	r3, [r4, #3828]
	bls	.L2519
	cmp	r0, #7
	addeq	r4, r4, #28
	beq	.L2520
.L2519:
	ldr	r4, .L2546+44
.L2520:
	mov	r3, #0
	mov	r2, r3
.L2522:
	ldrsb	r1, [r4, r2]
	add	r2, r2, #1
	cmp	r1, #0
	addeq	r3, r3, #1
	cmp	r2, #32
	bne	.L2522
	cmp	r3, #27
	bls	.L2516
	bl	FlashGetReadRetryDefault
	bl	FlashSavePhyInfo
	b	.L2516
.L2517:
	sub	r3, r0, #17
	uxtb	r3, r3
	cmp	r3, #2
	bhi	.L2523
	ldr	r3, .L2546+48
	cmp	r0, #19
	str	r3, [r5, #172]
	mov	r3, #7
	strb	r3, [r5, #140]
	moveq	r3, #15
	streqb	r3, [r5, #140]
	b	.L2516
.L2523:
	cmp	r0, #33
	cmpne	r0, #65
	beq	.L2524
	cmp	r0, #66
	bne	.L2525
.L2524:
	ldr	r3, .L2546
	mov	r1, #4
	ldr	r2, .L2546+52
	str	r2, [r3, #172]
	ldr	r2, .L2546+4
	strb	r1, [r2, #3760]
	mov	r2, #7
	strb	r2, [r3, #140]
	b	.L2516
.L2525:
	cmp	r0, #67
	cmpne	r0, #34
	beq	.L2526
	cmp	r0, #35
	beq	.L2526
	cmp	r0, #68
	bne	.L2527
.L2526:
	ldr	r3, .L2546
	cmp	r0, #35
	cmpne	r0, #68
	ldr	r2, .L2546+52
	sub	r0, r0, #67
	uxtb	r0, r0
	str	r2, [r3, #172]
	mov	r2, #7
	strb	r2, [r3, #140]
	moveq	r2, #17
	streqb	r2, [r3, #140]
	cmp	r0, #1
	ldr	r3, .L2546+4
	movls	r2, #4
	movhi	r2, #5
	strb	r2, [r3, #3760]
	b	.L2516
.L2527:
	cmp	r0, #49
	ldreq	r3, .L2546+56
	streq	r3, [r5, #172]
.L2516:
	ldr	r2, .L2546+4
	ldr	r3, .L2546+20
	ldr	r1, [r2, #3776]
	cmp	r1, r3
	bne	.L2530
	ldrb	r3, [r2, #928]	@ zero_extendqisi2
	cmp	r3, #0
	ldrne	r3, [r2, #3624]
	movne	r1, #0
	strneb	r1, [r3, #18]
.L2530:
	ldrb	r2, [r2, #2980]	@ zero_extendqisi2
	ldr	r3, .L2546+4
	cmp	r2, #44
	bne	.L2531
	ldrb	r2, [r3, #3780]	@ zero_extendqisi2
	cmp	r2, #0
	beq	.L2531
	ldr	r1, [r3, #3776]
	ldr	r2, .L2546+20
	cmp	r1, r2
	bne	.L2532
	ldrb	r3, [r3, #928]	@ zero_extendqisi2
	cmp	r3, #0
	bne	.L2531
.L2532:
	ldr	r3, .L2546+4
	mov	r2, #0
	mov	r0, #1
	strb	r2, [r3, #3780]
	bl	FlashSetInterfaceMode
	mov	r0, #1
	bl	NandcSetMode
.L2531:
	mov	r0, #0
	bl	flash_enter_slc_mode
	ldr	r2, .L2546+4
	ldrb	r3, [r2, #3772]	@ zero_extendqisi2
	tst	r3, #6
	beq	.L2533
	ldrb	r2, [r2, #3780]	@ zero_extendqisi2
	cmp	r2, #0
	bne	.L2534
	tst	r3, #1
	bne	.L2533
.L2534:
	ldr	r3, .L2546
	mov	r0, #0
	ldr	r1, [r3, #160]
	bl	FlashDdrParaScan
.L2533:
	ldr	r4, .L2546+4
	mov	r0, #0
	bl	flash_exit_slc_mode
	mov	r8, #16
	movw	r6, #3062
	ldr	r3, [r4, #3624]
	ldrb	r0, [r3, #20]	@ zero_extendqisi2
	bl	FlashBchSel
	add	r0, r4, #932
	bl	FlashReadIdbDataRaw
	ldr	r0, .L2546+60
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
	bne	.L2536
	mov	r7, r7, lsr #1
	mov	ip, ip, asl #1
	mov	r1, r1, asl #1
	strb	r8, [r4, #853]
	strh	r1, [r4, r2]	@ movhi
	mov	r2, #8
	strh	r7, [r4, r6]	@ movhi
	strh	ip, [r4, r0]	@ movhi
	strh	r2, [r4, r3]	@ movhi
.L2536:
	ldrb	r0, [r5, #20]	@ zero_extendqisi2
	bl	FlashBchSel
	bl	ftl_flash_suspend
	mov	r0, #0
	ldmfd	sp!, {r4, r5, r6, r7, r8, pc}
.L2537:
	mvn	r0, #1
	ldmfd	sp!, {r4, r5, r6, r7, r8, pc}
.L2538:
	mvn	r0, #1
	ldmfd	sp!, {r4, r5, r6, r7, r8, pc}
.L2547:
	.align	2
.L2546:
	.word	.LANCHOR2
	.word	.LANCHOR0
	.word	.LANCHOR0+2980
	.word	.LANCHOR1+2892
	.word	.LANCHOR1
	.word	1446522928
	.word	.LANCHOR1+2796
	.word	.LANCHOR0+896
	.word	.LC164
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
	stmfd	sp!, {r4, r5, r6, lr}
	.save {r4, r5, r6, lr}
	mov	r0, #2048
	bl	ftl_malloc
	ldr	r4, .L2552
	ldr	r6, .L2552+4
	mov	r5, #0
	mov	r1, r6
	str	r5, [r1, #472]!
	str	r0, [r6, #468]
	mov	r0, r4
	str	r5, [r0, #132]!
	bl	rknand_get_reg_addr
	ldr	r3, [r4, #132]
	cmp	r3, r5
	mvneq	r4, #0
	beq	.L2549
	bl	rk_nandc_irq_init
	mov	r1, r5
	mov	r2, r5
	mov	r3, #2048
	ldr	r0, [r6, #468]
	bl	FlashSramLoadStore
	bl	rknand_flash_cs_init
	ldr	r0, [r4, #132]
	bl	FlashInit
	subs	r4, r0, #0
	bne	.L2550
	bl	FtlInit
.L2550:
	ldr	r0, .L2552+8
	mov	r1, r4
	bl	printk
.L2549:
	mov	r0, r4
	ldmfd	sp!, {r4, r5, r6, pc}
.L2553:
	.align	2
.L2552:
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
	ldr	r3, .L2579
	stmfd	sp!, {r4, r5, lr}
	.save {r4, r5, lr}
	.pad #20
	sub	sp, sp, #20
	ldrb	r4, [r3, #928]	@ zero_extendqisi2
	cmp	r4, #0
	movne	r0, #0
	bne	.L2555
	ldr	r5, .L2579+4
	ldr	r0, [r5, #132]
	bl	FlashInit
	cmp	r0, #0
	movne	r0, r4
	bne	.L2555
	bl	FlashLoadFactorBbt
	cmp	r0, #0
	beq	.L2556
	bl	FlashMakeFactorBbt
.L2556:
	ldr	r0, [r5, #136]
	bl	FlashReadIdbDataRaw
	cmp	r0, #0
	beq	.L2557
	mov	r1, #0
	mov	r2, #16
	mov	r0, sp
	bl	FlashReadFacBbtData
	ldr	r1, [sp, #0]
	mov	r3, #0
	mov	r0, #1
	mov	r2, r3
.L2559:
	ands	ip, r1, r0, asl r2
	add	r2, r2, #1
	addne	r3, r3, #1
	cmp	r2, #16
	bne	.L2559
	cmp	r3, #6
	ldrls	r3, .L2579
	bls	.L2575
	mov	r2, #0
	mov	r0, #1
.L2560:
	ands	ip, r1, r0, asl r2
	add	r2, r2, #1
	addne	r3, r3, #1
	cmp	r2, #24
	bne	.L2560
	cmp	r3, #17
	ldr	r3, .L2579
	movhi	r2, #36
.L2575:
	strb	r2, [r3, #853]
	movw	r2, #3074
	ldr	r3, .L2579
	ldrb	r1, [r3, #853]	@ zero_extendqisi2
	strh	r1, [r3, r2]	@ movhi
.L2557:
	ldr	r1, .L2579+8
	mov	r4, #1
	ldr	r0, .L2579+12
	bl	printk
	ldr	r0, .L2579+16
	bl	FtlConstantsInit
	bl	FtlVariablesInit
	ldr	r3, .L2579
	ldr	r0, [r3, #3852]
	uxth	r0, r0
	bl	FtlFreeSysBlkQueueInit
	b	.L2564
.L2566:
	add	r4, r4, #1
.L2564:
	bl	FtlLoadBbt
	cmp	r0, #0
	bne	.L2577
.L2565:
	bl	FtlSysBlkInit
	cmp	r0, #0
	beq	.L2567
.L2577:
	bl	FtlLowFormat
	cmp	r4, #3
	bls	.L2566
	b	.L2578
.L2567:
	ldr	r3, .L2579+20
	mov	r2, #1
	str	r2, [r3, #2996]
	b	.L2555
.L2578:
	mvn	r0, #0
.L2555:
	add	sp, sp, #20
	ldmfd	sp!, {r4, r5, pc}
.L2580:
	.align	2
.L2579:
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
	ldr	r3, .L2583
	ldr	r2, [r3, #3776]
	ldr	r3, .L2583+4
	cmp	r2, r3
	bxne	lr
	b	flash_enter_slc_mode
.L2584:
	.align	2
.L2583:
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
	ldr	r3, .L2587
	ldr	r2, [r3, #3776]
	ldr	r3, .L2587+4
	cmp	r2, r3
	bxne	lr
	b	flash_exit_slc_mode
.L2588:
	.align	2
.L2587:
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
	ldr	r5, .L2593
	mov	fp, r1
	.pad #28
	sub	sp, sp, #28
	mov	r1, r4
	mov	sl, r2
	ldr	r0, .L2593+4
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
	b	.L2590
.L2592:
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
	beq	.L2591
	ldr	r0, [r5, #3776]
	ldr	lr, .L2593+8
	cmp	r0, lr
	moveq	r3, r1
.L2591:
	ldr	r1, [sp, #16]
	add	r2, r2, r1
	mla	r2, r7, r3, r2
	ldr	r3, [r9, #3624]
	ldrb	r1, [r3, #9]	@ zero_extendqisi2
	ldrb	r3, [r9, #3836]	@ zero_extendqisi2
	str	r3, [sp, #20]
	ldr	r3, .L2593+12
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
.L2590:
	cmp	r6, fp
	bcc	.L2592
	mov	r1, r4
	mov	r2, fp
	mov	r3, #0
	ldr	r0, .L2593+16
	bl	printk
	mov	r0, #0
	add	sp, sp, #28
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, sl, fp, pc}
.L2594:
	.align	2
.L2593:
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
	ldr	r3, .L2603
	stmfd	sp!, {r4, r5, r6, r7, r8, r9, sl, fp, lr}
	.save {r4, r5, r6, r7, r8, r9, sl, fp, lr}
	mov	r5, r0
	ldr	r3, [r3, #3624]
	mov	fp, r1
	.pad #92
	sub	sp, sp, #92
	mov	r1, r5
	mov	sl, r2
	ldr	r0, .L2603+4
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
	bls	.L2596
	mov	r0, #0
	add	r1, r7, r5
	mov	r2, r0
	mov	r8, #2
	bl	FlashEraseBlock
.L2596:
	ldr	r4, .L2603
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
	b	.L2597
.L2601:
	add	r3, r8, r7
	ubfx	r3, r3, #2, #16
	cmp	r3, #0
	beq	.L2598
	add	r1, r3, #1
	ldrb	r0, [r4, #928]	@ zero_extendqisi2
	add	r2, r4, r1, asl #1
	cmp	r0, #0
	add	r2, r2, #3072
	add	r2, r2, #4
	ldrh	r2, [r2, #0]
	beq	.L2599
	ldr	r0, [r4, #3776]
	ldr	ip, .L2603+8
	cmp	r0, ip
	moveq	r2, r1
.L2599:
	sub	r2, r2, #-1073741823
	mov	r2, r2, asl #2
	str	r2, [sp, #24]
.L2598:
	movw	r2, #61424
	str	r2, [sp, #28]
	add	r2, r4, r3, asl #1
	add	r2, r2, #3072
	ldrh	r5, [r2, #4]
	ldrb	r2, [r4, #928]	@ zero_extendqisi2
	cmp	r2, #0
	beq	.L2600
	ldr	r2, [r4, #3776]
	ldr	r1, .L2603+8
	cmp	r2, r1
	moveq	r5, r3
.L2600:
	ldr	ip, [sp, #12]
	add	r8, r8, #4
	ldr	r2, .L2603+12
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
.L2597:
	ldr	ip, [sp, #16]
	cmp	r8, ip
	bcc	.L2601
	ldr	r5, [sp, #20]
	mov	r2, fp
	mov	r3, #0
	ldr	r0, .L2603+16
	mov	r1, r5
	bl	printk
	mov	r0, #0
	add	sp, sp, #92
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, sl, fp, pc}
.L2604:
	.align	2
.L2603:
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
	ldr	r3, .L2629
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
	beq	.L2623
	add	r5, r5, #508
	add	r5, r5, #3
	mov	r5, r5, lsr #9
	cmp	r5, #8
	movls	r5, #8
	bls	.L2607
	cmp	r5, #500
	bhi	.L2623
.L2607:
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
.L2612:
	ldr	r1, [r0], #-4
	cmp	r1, #0
	bne	.L2608
	ldr	r1, [r6, r3, asl #2]
	add	r3, r3, #1
	cmp	r3, ip
	sub	r2, r2, #1
	movcs	r3, #0
	cmp	r2, #4096
	str	r1, [r0, #4]
	bne	.L2612
	b	.L2611
.L2608:
	ldr	r0, .L2629+4
	bl	printk
.L2611:
	ldr	r4, .L2629
	mov	r1, r7
	mov	r3, #5
	ldr	r0, .L2629+8
	mov	r2, #4
	mov	r8, r5
	bl	rknand_print_hex
	ldrb	r2, [r4, #853]	@ zero_extendqisi2
	ldr	r1, [r6, #512]
	ldr	r0, .L2629+12
	bl	printk
	ldr	r2, .L2629+16
	movw	r3, #3074
	mov	r1, r5
	ldrh	r3, [r4, r3]
	ldr	r2, [r2, #156]
	ldr	r0, .L2629+20
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
.L2620:
	ldr	r1, .L2629
	ldr	r3, [r4], #4
	ldrb	r2, [r1, #853]	@ zero_extendqisi2
	cmp	r3, r2
	bcs	.L2614
	ldr	r1, .L2629+16
	ldr	r2, [r1, #156]
	cmp	r3, r2
	bcc	.L2614
	ldr	r2, [sp, #20]
	ldr	r1, [sp, #12]
	cmp	r2, #1
	movls	r2, #0
	movhi	r2, #1
	str	r2, [sp, #28]
	ldr	r2, [sp, #20]
	cmp	r1, #0
	cmpne	r2, #1
	bls	.L2615
	ldr	r2, [r4, #-8]
	add	r2, r2, #1
	cmp	r3, r2
	beq	.L2614
.L2615:
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
.L2619:
	mov	fp, r2
	mov	r5, r3
	ldr	r0, [fp, #0]
	add	r2, r2, #4
	ldr	r1, [r5, #0]
	add	r3, r3, #4
	cmp	r0, r1
	beq	.L2616
	mov	r0, r9
	mov	r1, #512
	bl	__memzero
	ldr	r3, [fp, #0]
	ldr	r1, [sp, #12]
	ldr	r0, .L2629+24
	str	r3, [sp, #0]
	ldr	r3, [r5, #0]
	bic	r5, r6, #255
	mov	r5, r5, asl #2
	str	r3, [sp, #4]
	mov	r3, r6
	ldr	r2, [r4, #-4]
	bl	printk
	ldr	r0, .L2629+28
	add	r1, r7, r5
	mov	r2, #4
	mov	r3, #256
	bl	rknand_print_hex
	mov	r3, #256
	add	r1, r9, r5
	mov	r2, #4
	ldr	r0, .L2629+32
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
	beq	.L2617
	ldr	r1, [r4, #-4]
	mov	r0, #0
	mov	r2, r0
	add	r1, r1, #1
	mul	r1, sl, r1
	bl	FlashEraseBlock
.L2617:
	mov	r0, #0
	bl	flash_boot_exit_slc_mode
	ldr	r1, [r4, #-4]
	ldr	r0, .L2629+36
	bl	printk
	ldr	r1, [sp, #24]
	cmp	r6, r1
	bcc	.L2614
	b	.L2618
.L2616:
	ldr	r1, [sp, #24]
	add	r6, r6, #1
	cmp	r6, r1
	bne	.L2619
.L2618:
	ldr	r2, [sp, #16]
	add	r2, r2, #1
	str	r2, [sp, #16]
.L2614:
	ldr	r3, [sp, #12]
	add	r3, r3, #1
	str	r3, [sp, #12]
	cmp	r3, #5
	bne	.L2620
	mov	r0, r9
	bl	ftl_free
	ldr	r1, [sp, #16]
	cmp	r1, #0
	mvneq	r0, #0
	movne	r0, #0
	b	.L2606
.L2623:
	mvn	r0, #0
.L2606:
	add	sp, sp, #36
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, sl, fp, pc}
.L2630:
	.align	2
.L2629:
	.word	.LANCHOR0
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
	ldr	ip, .L2634
	b	.L2632
.L2633:
	ldrb	r4, [r3, r2]	@ zero_extendqisi2
	add	r2, r2, #1
	eor	r4, r4, r0, lsr #24
	add	r4, ip, r4, asl #2
	ldr	r4, [r4, #3068]
	eor	r0, r4, r0, asl #8
.L2632:
	cmp	r2, r1
	bne	.L2633
	ldmfd	sp!, {r4, pc}
.L2635:
	.align	2
.L2634:
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
	ldr	r3, .L2752
	stmfd	sp!, {r4, r5, r6, lr}
	.save {r4, r5, r6, lr}
	cmp	r1, r3
	.pad #528
	sub	sp, sp, #528
	mov	r5, r1
	mov	r4, r2
	beq	.L2645
	bhi	.L2654
	ldr	r3, .L2752+4
	cmp	r1, r3
	beq	.L2642
	bhi	.L2655
	sub	r3, r3, #125
	cmp	r1, r3
	beq	.L2639
	bhi	.L2656
	sub	r3, r3, #237
	cmp	r1, r3
	bne	.L2698
	b	.L2748
.L2656:
	ldr	r3, .L2752+8
	cmp	r1, r3
	beq	.L2640
	add	r3, r3, #1
	cmp	r1, r3
	bne	.L2698
	b	.L2749
.L2655:
	ldr	r3, .L2752+12
	cmp	r1, r3
	beq	.L2644
	bhi	.L2657
	ldr	r3, .L2752+16
	cmp	r1, r3
	bne	.L2698
	b	.L2750
.L2657:
	ldr	r3, .L2752+20
	cmp	r1, r3
	beq	.L2644
	add	r3, r3, #10
	cmp	r1, r3
	bne	.L2698
	b	.L2644
.L2654:
	ldr	r3, .L2752+24
	cmp	r1, r3
	beq	.L2650
	bhi	.L2658
	sub	r3, r3, #78
	cmp	r1, r3
	beq	.L2647
	bcc	.L2646
	add	r3, r3, #21
	cmp	r1, r3
	beq	.L2648
	add	r3, r3, #56
	cmp	r1, r3
	bne	.L2698
	b	.L2751
.L2658:
	ldr	r3, .L2752+28
	cmp	r1, r3
	beq	.L2653
	bhi	.L2659
	sub	r3, r3, #956
	sub	r3, r3, #1
	cmp	r1, r3
	beq	.L2651
	add	r3, r3, #956
	cmp	r1, r3
	bne	.L2698
	b	.L2652
.L2659:
	ldr	r3, .L2752+32
	cmp	r1, r3
	beq	.L2652
	add	r3, r3, #1
	cmp	r1, r3
	bne	.L2698
	b	.L2653
.L2646:
	ldr	r0, .L2752+36
	bl	printk
	mov	r0, #4096
	bl	ftl_malloc
	subs	r5, r0, #0
	beq	.L2705
	mov	r1, r4
	mov	r2, #512
	bl	rk_copy_from_user
	cmp	r0, #0
	bne	.L2747
.L2660:
	ldr	r0, .L2752+40
	ldmia	r5, {r1, r2}
	bl	printk
	ldr	r3, [r5, #4]
	cmp	r3, #8
	str	r3, [sp, #4]
	bhi	.L2669
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
	beq	.L2733
	ldr	r0, .L2752+44
.L2746:
	bl	printk
	b	.L2669
.L2647:
	ldr	r0, .L2752+48
	bl	printk
	mov	r0, #4096
	bl	ftl_malloc
	subs	r5, r0, #0
	beq	.L2705
	mov	r1, r4
	mov	r2, #4096
	bl	rk_copy_from_user
	cmp	r0, #0
	bne	.L2747
.L2664:
	ldr	r4, .L2752+52
	ldr	r0, .L2752+56
	ldmia	r5, {r1, r2}
	bl	printk
	ldr	r3, [r4, #476]
	cmp	r3, #0
	bne	.L2665
	mov	r0, #260096
	bl	ftl_malloc
	cmp	r0, #0
	str	r0, [r4, #476]
	beq	.L2669
.L2666:
	mov	r1, #260096
	bl	__memzero
.L2665:
	ldr	r2, [r5, #4]
	movw	r3, #4088
	cmp	r2, r3
	bhi	.L2669
	ldr	r3, [r5, #0]
	cmp	r3, #251904
	bhi	.L2669
	ldr	r1, .L2752+52
	ldr	r0, [r1, #476]
	add	r1, r5, #8
	add	r0, r0, r3
	bl	memcpy
.L2733:
	mov	r0, r5
	bl	ftl_free
.L2734:
	mov	r4, #0
	b	.L2663
.L2751:
	ldr	r0, .L2752+60
	bl	printk
	mov	r0, #4096
	bl	ftl_malloc
	subs	r5, r0, #0
	beq	.L2705
	mov	r1, r4
	mov	r2, #28
	bl	rk_copy_from_user
	cmp	r0, #0
	beq	.L2668
.L2747:
	ldr	r0, .L2752+64
	b	.L2746
.L2668:
	ldmia	r5, {r1, r2}
	ldr	r0, .L2752+68
	bl	printk
	ldr	r1, [r5, #0]
	cmp	r1, #256000
	bhi	.L2669
	ldr	r4, .L2752+52
	ldr	r0, [r4, #476]
	cmp	r0, #0
	beq	.L2669
.L2670:
	bl	CRC_32
	ldr	r3, [r5, #4]
	cmp	r3, r0
	beq	.L2671
	mov	r0, r5
	bl	ftl_free
	b	.L2711
.L2671:
	bl	rknand_device_lock
	ldr	r1, [r4, #476]
	add	r2, r5, #8
	ldr	r0, [r5, #0]
	bl	write_idblock
	mov	r6, #0
	bl	rknand_device_unlock
	ldr	r0, [r4, #476]
	bl	ftl_free
	str	r6, [r4, #476]
	mov	r0, r5
	mov	r4, r6
	bl	ftl_free
	b	.L2663
.L2669:
	mov	r0, r5
	b	.L2737
.L2648:
	ldr	r0, .L2752+72
	bl	printk
	mov	r0, #4096
	bl	ftl_malloc
	subs	r5, r0, #0
	beq	.L2705
	bl	ftl_read_flash_info
	mov	r0, r4
	mov	r1, r5
	mov	r2, #11
	b	.L2742
.L2645:
	ldr	r0, .L2752+76
	bl	printk
	bl	nand_blk_add_whole_disk
	mov	r0, #4096
	bl	ftl_malloc
	subs	r5, r0, #0
	beq	.L2705
	bl	rknand_device_lock
	mov	r1, #0
	mov	r2, #64
	mov	r0, r5
	bl	FlashReadFacBbtData
	bl	rknand_device_unlock
	ldr	r0, .L2752+80
	mov	r1, r5
	mov	r2, #4
	mov	r3, #8
	bl	rknand_print_hex
	mov	r0, r4
	mov	r1, r5
	mov	r2, #64
	b	.L2742
.L2650:
	ldr	r0, .L2752+84
	bl	printk
	ldr	r3, .L2752+52
	add	r1, sp, #528
	mov	r0, r4
	mov	r2, #4
	ldr	r3, [r3, #480]
	ldr	r3, [r3, #20]
	str	r3, [r1, #-524]!
	b	.L2738
.L2651:
	ldr	r0, .L2752+88
	bl	printk
	mov	r0, #4096
	bl	ftl_malloc
	subs	r5, r0, #0
	beq	.L2705
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
.L2742:
	bl	rk_copy_to_user
	subs	r4, r0, #0
	mov	r0, r5
	beq	.L2674
.L2737:
	bl	ftl_free
	b	.L2728
.L2674:
	bl	ftl_free
	b	.L2663
.L2639:
	ldr	r0, .L2752+92
	bl	printk
	add	r0, sp, #8
	mov	r1, r4
	mov	r2, #520
	bl	rk_copy_from_user
	subs	r5, r0, #0
	bne	.L2739
.L2675:
	ldr	r2, [sp, #8]
	ldr	r3, .L2752+96
	cmp	r2, r3
	bne	.L2730
	ldr	r3, [sp, #12]
	cmp	r3, #512
	bhi	.L2730
	ldr	r6, .L2752+52
	mov	r2, #512
	add	r0, sp, #8
	ldr	r1, [r6, #480]
	bl	memcpy
	ldr	r2, [r6, #484]
	ldr	r3, .L2752+100
	cmp	r2, r3
	beq	.L2676
	add	r0, sp, #72
	mov	r1, #128
	str	r5, [sp, #16]
	str	r5, [sp, #20]
	bl	__memzero
.L2676:
	add	r0, sp, #264
	mov	r1, #256
	mov	r3, #0
	str	r3, [sp, #24]
	bl	__memzero
	b	.L2745
.L2640:
	ldr	r0, .L2752+104
	bl	printk
	add	r0, sp, #8
	mov	r1, r4
	mov	r2, #520
	bl	rk_copy_from_user
	cmp	r0, #0
	bne	.L2739
.L2677:
	ldr	r2, [sp, #8]
	ldr	r3, .L2752+96
	cmp	r2, r3
	bne	.L2730
	ldr	r3, [sp, #12]
	cmp	r3, #512
	bhi	.L2730
	ldr	r4, .L2752+52
	ldr	r3, .L2752+100
	ldr	r2, [r4, #484]
	cmp	r2, r3
	bne	.L2711
	ldr	r3, [sp, #20]
	sub	r2, r3, #1
	cmp	r2, #127
	mvnhi	r4, #2
	bhi	.L2637
	ldr	r0, [r4, #480]
	add	r1, sp, #72
	str	r3, [r0, #12]
	add	r0, r0, #64
	ldr	r2, [sp, #20]
	bl	memcpy
	mov	r0, #1
	ldr	r1, [r4, #480]
	b	.L2735
.L2750:
	ldr	r0, .L2752+108
	bl	printk
	add	r0, sp, #8
	mov	r1, r4
	mov	r2, #520
	bl	rk_copy_from_user
	cmp	r0, #0
	bne	.L2739
.L2678:
	ldr	r2, [sp, #8]
	ldr	r3, .L2752+112
	cmp	r2, r3
	bne	.L2730
	ldr	r3, [sp, #12]
	cmp	r3, #512
	bhi	.L2730
	ldr	r5, .L2752+52
	ldr	r4, [r5, #488]
	cmp	r4, #0
	beq	.L2637
	ldr	r3, [r5, #492]
	ldr	r2, .L2752+116
	ldr	r1, [r3, #0]
	cmp	r1, r2
	beq	.L2679
	str	r2, [r3, #0]
	mov	r2, #504
	ldr	r3, [r5, #492]
	str	r2, [r3, #4]
	mov	r2, #0
	str	r2, [r3, #8]
	str	r2, [r3, #12]
.L2679:
	ldr	r1, [r5, #492]
	mov	r4, #0
	mov	r0, r4
	str	r4, [r1, #16]
	bl	StorageSysDataStore
	ldr	r3, [r5, #480]
	ldr	r2, .L2752+96
	ldr	r5, .L2752+52
	ldr	r1, [r3, #0]
	cmp	r1, r2
	strne	r2, [r3, #0]
	ldr	r0, [r5, #480]
	mov	r1, #128
	ldrne	r3, .L2752+52
	movne	r2, #504
	ldrne	r3, [r3, #480]
	stmneib	r3, {r2, r4}
	mov	r4, #0
	str	r4, [r0, #12]
	add	r0, r0, #64
	bl	__memzero
	mov	r0, #1
	ldr	r1, [r5, #480]
	bl	StorageSysDataStore
	str	r4, [r5, #488]
	str	r4, [r5, #484]
	b	.L2663
.L2642:
	ldr	r0, .L2752+120
	bl	printk
	mov	r1, r4
	add	r0, sp, #8
	mov	r2, #520
	bl	rk_copy_from_user
	subs	r4, r0, #0
	bne	.L2739
.L2681:
	ldr	r2, [sp, #8]
	ldr	r3, .L2752+124
	cmp	r2, r3
	bne	.L2730
	ldr	r3, [sp, #12]
	cmp	r3, #512
	bhi	.L2730
	ldr	r5, .L2752+52
	ldr	r3, [r5, #488]
	cmp	r3, #1
	beq	.L2637
	ldr	r3, [r5, #492]
	mov	r0, #0
	ldr	r2, .L2752+116
	ldr	r1, [r3, #0]
	cmp	r1, r2
	strne	r2, [r3, #0]
	ldr	r1, [r5, #492]
	ldrne	r3, [r5, #492]
	movne	r2, #504
	strne	r4, [r3, #12]
	stmneib	r3, {r2, r4}
	mov	r3, #1
	str	r3, [r1, #16]
	bl	StorageSysDataStore
	ldr	r3, [r5, #480]
	ldr	r2, .L2752+96
	ldr	r1, [r3, #0]
	cmp	r1, r2
	beq	.L2683
	str	r2, [r3, #0]
	mov	r1, #504
	ldr	r3, .L2752+52
	mov	r2, #0
	ldr	r3, [r3, #480]
	stmib	r3, {r1, r2}
.L2683:
	ldr	r5, .L2752+52
	mov	r1, #128
	mov	r4, #0
	ldr	r0, [r5, #480]
	str	r4, [r0, #12]
	add	r0, r0, #64
	bl	__memzero
	mov	r0, #1
	ldr	r1, [r5, #480]
	bl	StorageSysDataStore
	mov	r3, #1
	str	r3, [r5, #488]
	b	.L2663
.L2749:
	ldr	r0, .L2752+128
	bl	printk
	add	r0, sp, #8
	mov	r1, r4
	mov	r2, #520
	bl	rk_copy_from_user
	cmp	r0, #0
	bne	.L2739
.L2684:
	ldr	r2, [sp, #8]
	ldr	r3, .L2752+132
	cmp	r2, r3
	bne	.L2730
	ldr	r2, [sp, #12]
	cmp	r2, #512
	addls	r0, sp, #16
	ldrls	r1, .L2752+136
	bls	.L2744
	b	.L2730
.L2644:
	ldr	r3, .L2752+20
	cmp	r5, r3
	ldreq	r0, .L2752+140
	beq	.L2731
	ldr	r3, .L2752+144
	cmp	r5, r3
	ldreq	r0, .L2752+148
	ldrne	r0, .L2752+152
.L2731:
	bl	printk
	add	r0, sp, #8
	mov	r1, r4
	mov	r2, #520
	bl	rk_copy_from_user
	cmp	r0, #0
	bne	.L2739
.L2688:
	ldr	r2, [sp, #8]
	ldr	r3, .L2752+156
	cmp	r2, r3
	bne	.L2728
	ldr	r3, .L2752+144
	ldr	r6, .L2752+52
	cmp	r5, r3
	bne	.L2689
	ldr	r3, [r6, #480]
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
	b	.L2637
.L2689:
	ldr	r3, [r6, #1008]
	cmp	r3, #10
	bhi	.L2728
	ldr	r3, [r6, #480]
	ldr	r1, [sp, #12]
	ldr	r2, [r3, #24]
	cmp	r2, r1
	beq	.L2690
	cmp	r2, #0
	beq	.L2690
	ldr	r0, .L2752+160
	bl	printk
	ldr	r3, [r6, #1008]
	add	r3, r3, #1
	str	r3, [r6, #1008]
	b	.L2728
.L2690:
	ldr	r0, .L2752+52
	mov	r2, #0
	str	r2, [r0, #1008]
	ldr	r0, .L2752+20
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
	b	.L2663
.L2652:
	ldr	r0, .L2752+164
	bl	printk
	add	r0, sp, #8
	mov	r1, r4
	mov	r2, #520
	bl	rk_copy_from_user
	cmp	r0, #0
	bne	.L2739
.L2693:
	ldr	r2, [sp, #8]
	ldr	r3, .L2752+168
	cmp	r2, r3
	bne	.L2730
	ldr	r2, [sp, #12]
	cmp	r2, #504
	bhi	.L2730
	ldr	r3, .L2752+172
	add	r0, sp, #16
	cmp	r5, r3
	ldr	r3, .L2752+52
	ldreq	r1, [r3, #1012]
	ldrne	r1, [r3, #1016]
	add	r1, r1, #8
.L2744:
	bl	memcpy
.L2745:
	add	r1, sp, #8
	mov	r0, r4
	mov	r2, #520
.L2738:
	bl	rk_copy_to_user
	subs	r4, r0, #0
	bne	.L2728
	b	.L2663
.L2653:
	ldr	r0, .L2752+176
	bl	printk
	add	r0, sp, #8
	mov	r1, r4
	mov	r2, #520
	bl	rk_copy_from_user
	cmp	r0, #0
	beq	.L2696
.L2739:
	ldr	r0, .L2752+64
	bl	printk
	b	.L2728
.L2696:
	ldr	r2, [sp, #8]
	ldr	r3, .L2752+168
	cmp	r2, r3
	bne	.L2730
	ldr	r2, [sp, #12]
	cmp	r2, #504
	bhi	.L2730
	ldr	r3, .L2752+28
	add	r2, r2, #8
	ldr	r4, .L2752+52
	cmp	r5, r3
	bne	.L2697
	add	r1, sp, #8
	ldr	r0, [r4, #1012]
	bl	memcpy
	ldr	r1, [r4, #1012]
	mov	r0, #2
.L2735:
	bl	StorageSysDataStore
	mov	r4, r0
	b	.L2663
.L2697:
	add	r1, sp, #8
	ldr	r0, [r4, #1016]
	bl	memcpy
	mov	r0, #3
	ldr	r1, [r4, #1016]
	b	.L2735
.L2748:
	bl	rknand_dev_flush
	b	.L2734
.L2730:
	mvn	r4, #0
.L2663:
	mov	r1, r4
	ldr	r0, .L2752+180
	bl	printk
	b	.L2637
.L2698:
	mvn	r4, #21
	b	.L2637
.L2705:
	mvn	r4, #11
	b	.L2637
.L2711:
	mvn	r4, #1
	b	.L2637
.L2728:
	mvn	r4, #13
.L2637:
	mov	r0, r4
	add	sp, sp, #528
	ldmfd	sp!, {r4, r5, r6, pc}
.L2753:
	.align	2
.L2752:
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
	.word	.LC191
	.word	1263358532
	.word	-1067903959
	.word	.LC192
	.word	.LC193
	.word	1112753220
	.word	1146313043
	.word	.LC194
	.word	1112755781
	.word	.LC195
	.word	1094995539
	.word	.LANCHOR4+496
	.word	.LC196
	.word	1074031676
	.word	.LC197
	.word	.LC198
	.word	1280262987
	.word	.LC199
	.word	.LC200
	.word	1145980246
	.word	1074034192
	.word	.LC201
	.word	.LC202
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
	ldr	r4, .L2757
	mov	r6, #0
	add	r0, r4, #496
	ldr	r5, [r4, #468]
	str	r6, [r4, #476]
	add	r1, r5, #1536
	add	r3, r5, #512
	str	r5, [r4, #492]
	str	r3, [r4, #480]
	add	r3, r5, #1024
	str	r1, [r4, #1016]
	str	r3, [r4, #1012]
	bl	memcpy
	ldr	r7, [r5, #508]
	ldr	r3, [r5, #16]
	cmp	r7, r6
	str	r6, [r4, #484]
	str	r6, [r4, #1008]
	str	r3, [r4, #488]
	beq	.L2755
	mov	r0, r5
	mov	r1, #508
	bl	JSHash
	cmp	r7, r0
	beq	.L2755
	str	r6, [r5, #16]
	ldr	r0, .L2757+4
	str	r6, [r4, #488]
	bl	printk
.L2755:
	ldr	r3, [r4, #488]
	mov	r0, #2
	ldr	r4, .L2757
	cmp	r3, #0
	ldrne	r2, .L2757+8
	ldrne	r3, .L2757
	ldr	r1, [r4, #1012]
	strne	r2, [r3, #484]
	bl	StorageSysDataLoad
	ldr	r1, [r4, #1016]
	mov	r0, #3
	bl	StorageSysDataLoad
	ldmfd	sp!, {r3, r4, r5, r6, r7, lr}
	b	rknand_sys_storage_init
.L2758:
	.align	2
.L2757:
	.word	.LANCHOR4
	.word	.LC203
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
	ldr	r7, .L2768
	cmp	r0, #0
	str	r0, [r7, #1020]
	beq	.L2766
	ldr	sl, .L2768+4
	mov	r6, #0
	mov	r4, r6
	mov	r5, r6
	movw	r8, #65532
.L2764:
	ldr	r0, [r7, #1020]
	mov	r1, r5, asl #7
	mov	r2, #0
	bl	rk_ftl_vendor_ops.constprop.28
	cmp	r0, #0
	bne	.L2762
	ldr	r3, [r7, #1020]
	ldr	r0, .L2768+8
	ldr	r1, [r3, #0]
	ldr	r2, [r3, r8]
	ldr	r3, [r3, #4]
	bl	printk
	ldr	r0, [r7, #1020]
	ldr	r3, [r0, #0]
	cmp	r3, sl
	bne	.L2763
	ldr	r3, [r0, r8]
	ldr	r2, [r0, #4]
	cmp	r3, r2
	bne	.L2763
	cmp	r4, r3
	movcc	r6, r5
	movcc	r4, r3
.L2763:
	add	r5, r5, #1
	cmp	r5, #2
	bne	.L2764
	cmp	r4, #0
	beq	.L2765
	mov	r1, r6, asl #7
	mov	r2, #0
	bl	rk_ftl_vendor_ops.constprop.28
	cmp	r0, #0
	ldmeqfd	sp!, {r3, r4, r5, r6, r7, r8, sl, pc}
	b	.L2762
.L2765:
	mov	r1, #65536
	bl	__memzero
	ldr	r3, .L2768
	ldr	r1, .L2768+4
	mov	r2, #1
	mov	r0, r4
	ldr	r3, [r3, #1020]
	stmia	r3, {r1, r2}
	movw	r1, #65532
	str	r2, [r3, r1]
	ldr	r2, .L2768+12
	strh	r4, [r3, #12]	@ movhi
	strh	r2, [r3, #14]	@ movhi
	ldmfd	sp!, {r3, r4, r5, r6, r7, r8, sl, pc}
.L2762:
	ldr	r4, .L2768
	ldr	r0, [r4, #1020]
	bl	kfree
	mov	r3, #0
	mvn	r0, #0
	str	r3, [r4, #1020]
	ldmfd	sp!, {r3, r4, r5, r6, r7, r8, sl, pc}
.L2766:
	mvn	r0, #11
	ldmfd	sp!, {r3, r4, r5, r6, r7, r8, sl, pc}
.L2769:
	.align	2
.L2768:
	.word	.LANCHOR4
	.word	1380668996
	.word	.LC204
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
	ldr	r1, .L2777
	stmfd	sp!, {r4, r5, r6, lr}
	.save {r4, r5, r6, lr}
	ldr	ip, [r1, #1020]
	cmp	ip, #0
	ldrneh	r4, [ip, #10]
	movne	r5, ip
	movne	r1, #0
	bne	.L2772
	b	.L2776
.L2774:
	ldrh	r6, [r5, #16]
	add	r5, r5, #8
	cmp	r6, r3
	bne	.L2773
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
.L2773:
	add	r1, r1, #1
.L2772:
	cmp	r1, r4
	bcc	.L2774
	mvn	r0, #0
	ldmfd	sp!, {r4, r5, r6, pc}
.L2776:
	mvn	r0, #0
	ldmfd	sp!, {r4, r5, r6, pc}
.L2778:
	.align	2
.L2777:
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
	ldr	r3, .L2796
	stmfd	sp!, {r4, r5, r6, r7, r8, r9, sl, fp, lr}
	.save {r4, r5, r6, r7, r8, r9, sl, fp, lr}
	mov	sl, r0
	ldr	r4, [r3, #1020]
	.pad #20
	sub	sp, sp, #20
	mov	r9, r1
	mov	r5, r2
	cmp	r4, #0
	beq	.L2792
	add	r8, r2, #63
	ldrh	r2, [r4, #8]
	ldrh	r3, [r4, #10]
	bic	r8, r8, #63
	mov	r7, #0
	str	r2, [sp, #8]
	b	.L2781
.L2788:
	add	r2, r7, #2
	mov	r2, r2, asl #3
	add	r6, r4, r2
	ldrh	r2, [r4, r2]
	cmp	r2, sl
	bne	.L2782
	ldrh	r2, [r6, #4]
	add	r2, r2, #63
	bic	r2, r2, #63
	str	r2, [sp, #12]
	cmp	r5, r2
	bls	.L2783
	ldrh	r2, [r4, #14]
	cmp	r2, r8
	bcc	.L2792
	sub	r3, r3, #1
	mov	fp, r8
	ldrh	r6, [r6, #2]
	mov	r8, r3
	b	.L2784
.L2785:
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
.L2784:
	cmp	r7, r8
	add	r2, r7, #2
	bcc	.L2785
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
	b	.L2795
.L2783:
	ldrh	r0, [r6, #2]
	mov	r1, r9
	mov	r2, r5
	add	r0, r0, #1024
	add	r0, r4, r0
	bl	memcpy
	strh	r5, [r6, #4]	@ movhi
	b	.L2795
.L2782:
	add	r7, r7, #1
.L2781:
	cmp	r7, r3
	bcc	.L2788
	ldrh	r2, [r4, #14]
	cmp	r2, r8
	bcc	.L2792
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
.L2795:
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
	b	.L2780
.L2792:
	mvn	r0, #0
.L2780:
	add	sp, sp, #20
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, sl, fp, pc}
.L2797:
	.align	2
.L2796:
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
	beq	.L2799
	ldr	r3, .L2815
	cmp	r6, r3
	beq	.L2801
	add	r3, r3, #1
	cmp	r6, r3
	bne	.L2812
	b	.L2814
.L2801:
	mov	r1, r5
	mov	r2, #8
	bl	rk_copy_from_user
	cmp	r0, #0
	bne	.L2812
	ldr	r2, [r4, #0]
	ldr	r3, .L2815+4
	cmp	r2, r3
	bne	.L2811
	ldrh	r0, [r4, #4]
	add	r1, r4, #8
	ldrh	r2, [r4, #6]
	bl	rk_ftl_vendor_read
	cmn	r0, #1
	beq	.L2807
	uxth	r2, r0
	mov	r1, r4
	strh	r2, [r4, #6]	@ movhi
	mov	r0, r5
	add	r2, r2, #8
	bl	rk_copy_to_user
	cmp	r0, #0
	moveq	r5, r0
	mvnne	r5, #13
	b	.L2800
.L2814:
	mov	r1, r5
	mov	r2, #8
	bl	rk_copy_from_user
	cmp	r0, #0
	bne	.L2812
	ldr	r2, [r4, #0]
	ldr	r3, .L2815+4
	cmp	r2, r3
	bne	.L2811
	ldrh	r2, [r4, #6]
	movw	r3, #4087
	cmp	r2, r3
	bhi	.L2811
	mov	r0, r4
	mov	r1, r5
	add	r2, r2, #8
	bl	rk_copy_from_user
	cmp	r0, #0
	bne	.L2812
	ldrh	r0, [r4, #4]
	add	r1, r4, #8
	ldrh	r2, [r4, #6]
	bl	rk_ftl_vendor_write
.L2807:
	mov	r5, r0
	b	.L2800
.L2811:
	mvn	r5, #0
	b	.L2800
.L2812:
	mvn	r5, #13
.L2800:
	mov	r0, r4
	bl	kfree
.L2799:
	mov	r0, r5
	ldmfd	sp!, {r4, r5, r6, pc}
.L2816:
	.align	2
.L2815:
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
	.type	__func__.14466, %object
	.size	__func__.14466, 11
__func__.14466:
	.ascii	"FtlMemInit\000"
.LC0:
	.byte	60
	.byte	40
	.byte	24
	.byte	16
	.type	__func__.15268, %object
	.size	__func__.15268, 16
__func__.15268:
	.ascii	"FtlScanAllBlock\000"
	.type	__func__.15249, %object
	.size	__func__.15249, 17
__func__.15249:
	.ascii	"FtlDumpBlockInfo\000"
	.type	__func__.15516, %object
	.size	__func__.15516, 21
__func__.15516:
	.ascii	"FtlVpcCheckAndModify\000"
	.type	__func__.14539, %object
	.size	__func__.14539, 8
__func__.14539:
	.ascii	"FtlInit\000"
	.type	__func__.15213, %object
	.size	__func__.15213, 12
__func__.15213:
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
	.ascii	"Read Err = 0x%x\012\000"
.LC68:
	.ascii	"Prog Err = 0x%x\012\000"
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
	.ascii	"FTL version: 5.0.48 20180703\000"
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
	.ascii	"...%s enter...\012\000"
.LC102:
	.ascii	"blk = %x vpc=%x mode = %x\012\000"
.LC103:
	.ascii	"mlc id = %x,%x addr= %x,spare= %x %x %x %x data=%x "
	.ascii	"%x\012\000"
.LC104:
	.ascii	"slc id = %x,%x addr= %x,spare= %x %x %x %x data=%x "
	.ascii	"%x\012\000"
.LC105:
	.ascii	"superBlkID = %x vpc=%x\012\000"
.LC106:
	.ascii	"flashmode = %x pagenum = %x %x\012\000"
.LC107:
	.ascii	"id = %x,%x addr= %x,spare= %x %x %x %x data=%x %x\012"
	.ascii	"\000"
.LC108:
	.ascii	"prog error: = %x\012\000"
.LC109:
	.ascii	"prog read error: = %x\012\000"
.LC110:
	.ascii	"prog read s error: = %x %x %x\012\000"
.LC111:
	.ascii	"prog read d error: = %x %x %x\012\000"
.LC112:
	.ascii	"FtlVpcTblFlush error = %x error count = %d\012\000"
.LC113:
	.ascii	"FtlBbmTblFlush id=%x,page=%x,previd=%x cnt=%d\012\000"
.LC114:
	.ascii	"FtlBbmTblFlush error:%x\012\000"
.LC115:
	.ascii	"FtlBbmTblFlush error = %x error count = %d\012\000"
.LC116:
	.ascii	"FtlGcFreeBadSuperBlk 0x%x\012\000"
.LC117:
	.ascii	"decrement_vpc_count %x = %d\012\000"
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
	.ascii	"ftl_map_blk_gc blk info: %x %x %x\012\000"
.LC124:
	.ascii	"page map lost: %x %x\012\000"
.LC125:
	.ascii	"FtlMapWritePage error = %x\012\000"
.LC126:
	.ascii	"FtlMapWritePage error = %x error count = %d\012\000"
.LC127:
	.ascii	"FtlVendorPartRead refresh = %x phyAddr = %x\012\000"
.LC128:
	.ascii	"slc mode\000"
.LC129:
	.ascii	"no ect\000"
.LC130:
	.ascii	"FLFB:%d %d\012\000"
.LC131:
	.ascii	"phyBlk = %x,addr= %x,spare= %x %x %x %x data=%x %x\012"
	.ascii	"\000"
.LC132:
	.ascii	"Mblk:\000"
.LC133:
	.ascii	"L2P:\000"
.LC134:
	.ascii	"L2PC:\000"
.LC135:
	.ascii	"id = %x,%x addr= %x,spare= %x %x %x %x data = %x\012"
	.ascii	"\000"
.LC136:
	.ascii	":\000"
.LC137:
	.ascii	"BBT:\000"
.LC138:
	.ascii	"load_l2p_region refresh = %x phyAddr = %x\012\000"
.LC139:
	.ascii	"region_id = %x phyAddr = %x\012\000"
.LC140:
	.ascii	"map_ppn:\000"
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
	.ascii	"GC des block %x done\012\000"
.LC149:
	.ascii	"%d GC datablk  = %x vpc %x %x\012\000"
.LC150:
	.ascii	"SWL %x, FSB = %x vpc= %x,ec=%x th=%x\012\000"
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
	.ascii	"bbt:\000"
.LC189:
	.ascii	"GET_LOCK_FLAG_IO\012\000"
.LC190:
	.ascii	"GET_PUBLIC_KEY_IO\012\000"
.LC191:
	.ascii	"RKNAND_GET_DRM_KEY\012\000"
.LC192:
	.ascii	"RKNAND_STORE_DRM_KEY\012\000"
.LC193:
	.ascii	"RKNAND_DIASBLE_SECURE_BOOT\012\000"
.LC194:
	.ascii	"RKNAND_ENASBLE_SECURE_BOOT\012\000"
.LC195:
	.ascii	"RKNAND_GET_SN_SECTOR\012\000"
.LC196:
	.ascii	"RKNAND_LOADER_UNLOCK\012\000"
.LC197:
	.ascii	"RKNAND_LOADER_STATUS\012\000"
.LC198:
	.ascii	"RKNAND_LOADER_LOCK\012\000"
.LC199:
	.ascii	"LockKey not match %d\012\000"
.LC200:
	.ascii	"RKNAND_GET_VENDOR_SECTOR\012\000"
.LC201:
	.ascii	"RKNAND_STORE_VENDOR_SECTOR\012\000"
.LC202:
	.ascii	"return ret = %lx\012\000"
.LC203:
	.ascii	"secureBootEn check error\012\000"
.LC204:
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
