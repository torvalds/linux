/*
 * Copyright (c) 2016, Fuzhou Rockchip Electronics Co., Ltd
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 * date: 2017-05-26
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
	.type	rknand_sys_storage_open, %function
rknand_sys_storage_open:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	@ link register save eliminated.
	mov	r0, #0
	bx	lr
	.fnend
	.size	rknand_sys_storage_open, .-rknand_sys_storage_open
	.align	2
	.type	rknand_sys_storage_release, %function
rknand_sys_storage_release:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	@ link register save eliminated.
	mov	r0, #0
	bx	lr
	.fnend
	.size	rknand_sys_storage_release, .-rknand_sys_storage_release
	.align	2
	.global	FlashMemCmp8
	.type	FlashMemCmp8, %function
FlashMemCmp8:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	ldr	r3, .L13
	stmfd	sp!, {r4, lr}
	.save {r4, lr}
	ldrb	r3, [r3, #0]	@ zero_extendqisi2
	cmp	r3, #0
	beq	.L11
	ldrb	r3, [r1, #1]	@ zero_extendqisi2
	ldrb	ip, [r0, #1]	@ zero_extendqisi2
	cmp	ip, r3
	movne	r3, #0
	bne	.L11
	b	.L12
.L8:
	ldrb	r4, [r0, r3]	@ zero_extendqisi2
	ldrb	ip, [r1, r3]	@ zero_extendqisi2
	add	r3, r3, #1
	cmp	r4, ip
	beq	.L11
	mov	r0, r3
	ldmfd	sp!, {r4, pc}
.L11:
	cmp	r3, r2
	bne	.L8
	mov	r0, #0
	ldmfd	sp!, {r4, pc}
.L12:
	mov	r0, #0
	ldmfd	sp!, {r4, pc}
.L14:
	.align	2
.L13:
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
	ldr	r3, .L16
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
.L17:
	.align	2
.L16:
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
	ldr	r2, .L20
	stmfd	sp!, {r4, lr}
	.save {r4, lr}
	mov	r3, r3, asl #1
	ldrh	r4, [r2, r3]
	ldr	r3, .L20+4
	ldrb	r3, [r3, #8]	@ zero_extendqisi2
	cmp	r3, #0
	beq	.L19
	bl	FlashRsvdBlkChk
	cmp	r0, #0
	orrne	r4, r4, #-1073741824
.L19:
	mov	r0, r4
	ldmfd	sp!, {r4, pc}
.L21:
	.align	2
.L20:
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
	stmfd	sp!, {r3, r4, r5, r6, r7, lr}
	.save {r3, r4, r5, r6, r7, lr}
	mov	r6, r0
	ldr	r3, .L28
	mov	r5, r1
	ldr	r2, [r3, #12]
	mov	r7, r3
	cmp	r2, #5
	bls	.L23
	and	r5, r1, #127
	ldr	r3, .L28+4
	mov	r5, r5, asl #1
	ldrh	r4, [r3, r5]
	ldrb	r3, [r7, #8]	@ zero_extendqisi2
	cmp	r3, #0
	beq	.L24
	bl	FlashRsvdBlkChk
	cmp	r0, #0
	orrne	r4, r4, #-1073741824
.L24:
	add	r6, r7, r6, asl #3
	b	.L27
.L23:
	cmp	r2, #4
	ldmnefd	sp!, {r3, r4, r5, r6, r7, pc}
	and	r2, r1, #127
	ldr	ip, .L28+4
	ldrb	r3, [r3, #8]	@ zero_extendqisi2
	mov	r2, r2, asl #1
	cmp	r3, #0
	ldrh	r4, [ip, r2]
	mov	r4, r4, asl #8
	beq	.L26
	bl	FlashRsvdBlkChk
	cmp	r0, #0
	movne	r5, r5, asl #1
	uxtbne	r5, r5
	orrne	r5, r5, #1
	orrne	r4, r4, r5
.L26:
	ldr	r3, .L28
	add	r6, r3, r6, asl #3
.L27:
	ldr	r3, [r6, #16]
	str	r4, [r3, #336]
	ldmfd	sp!, {r3, r4, r5, r6, r7, pc}
.L29:
	.align	2
.L28:
	.word	.LANCHOR0
	.word	.LANCHOR1
	.fnend
	.size	FlashSetRandomizer, .-FlashSetRandomizer
	.align	2
	.global	FlashReadCmd
	.type	FlashReadCmd, %function
FlashReadCmd:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	ldr	ip, .L31
	mov	r2, r1, lsr #16
	stmfd	sp!, {r4, lr}
	.save {r4, lr}
	add	ip, ip, r0, asl #3
	ldrb	r3, [ip, #20]	@ zero_extendqisi2
	ldr	r4, [ip, #16]
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
.L32:
	.align	2
.L31:
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
	ldr	r2, .L37
	stmfd	sp!, {r4, lr}
	.save {r4, lr}
	add	ip, r2, r0, asl #3
	ldrb	r2, [r2, #64]	@ zero_extendqisi2
	ldrb	r3, [ip, #20]	@ zero_extendqisi2
	ldr	r4, [ip, #16]
	cmp	r2, #1
	add	r3, r3, #8
	mov	r2, #0
	add	r3, r4, r3, asl #8
	bne	.L34
	mov	ip, #6
	str	ip, [r3, #8]
	str	r2, [r3, #4]
	str	r2, [r3, #4]
	uxtb	r2, r1
	str	r2, [r3, #4]
	mov	r2, r1, lsr #8
	str	r2, [r3, #4]
	mov	r2, r1, lsr #16
	b	.L36
.L34:
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
.L36:
	str	r2, [r3, #4]
	mov	r2, #224
	str	r2, [r3, #8]
	ldmfd	sp!, {r4, lr}
	b	FlashSetRandomizer
.L38:
	.align	2
.L37:
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
	ldr	r3, .L41
	ldrb	r2, [r3, #80]	@ zero_extendqisi2
	cmp	r2, #0
	bxeq	lr
	add	r0, r3, r0, asl #3
	ldrb	r3, [r0, #20]	@ zero_extendqisi2
	ldr	r2, [r0, #16]
	add	r3, r3, #8
	add	r3, r2, r3, asl #8
	mov	r2, #218
	str	r2, [r3, #8]
	bx	lr
.L42:
	.align	2
.L41:
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
	ldr	r3, .L45
	ldrb	r2, [r3, #80]	@ zero_extendqisi2
	cmp	r2, #0
	bxeq	lr
	add	r0, r3, r0, asl #3
	ldrb	r3, [r0, #20]	@ zero_extendqisi2
	ldr	r2, [r0, #16]
	add	r3, r3, #8
	add	r3, r2, r3, asl #8
	mov	r2, #223
	str	r2, [r3, #8]
	bx	lr
.L46:
	.align	2
.L45:
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
	ldr	ip, .L48
	mov	r2, r1, lsr #16
	stmfd	sp!, {r4, lr}
	.save {r4, lr}
	add	ip, ip, r0, asl #3
	ldrb	r3, [ip, #20]	@ zero_extendqisi2
	ldr	r4, [ip, #16]
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
.L49:
	.align	2
.L48:
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
	ldr	r3, .L52
	cmp	r2, #0
	add	r0, r3, r0, asl #3
	ldr	ip, [r0, #16]
	ldrb	r0, [r0, #20]	@ zero_extendqisi2
	add	r0, r0, #8
	add	r0, ip, r0, asl #8
	beq	.L51
	ldr	r3, [r3, #4]
	mov	r2, #96
	str	r2, [r0, #8]
	uxtb	r2, r1
	str	r2, [r0, #4]
	mov	r2, r1, lsr #8
	str	r2, [r0, #4]
	mov	r2, r1, lsr #16
	add	r1, r1, r3
	str	r2, [r0, #4]
.L51:
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
.L53:
	.align	2
.L52:
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
	ldr	ip, .L55
	mov	r2, r1, lsr #16
	stmfd	sp!, {r4, r5, lr}
	.save {r4, r5, lr}
	add	r4, ip, r0, asl #3
	ldrb	ip, [ip, #59]	@ zero_extendqisi2
	ldrb	r3, [r4, #20]	@ zero_extendqisi2
	ldr	r5, [r4, #16]
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
.L56:
	.align	2
.L55:
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
	ldr	r3, .L58
	add	r3, r3, r0, asl #3
	ldr	r2, [r3, #16]
	ldrb	r3, [r3, #20]	@ zero_extendqisi2
	add	r3, r3, #8
	add	r3, r2, r3, asl #8
	mov	r2, #16
	str	r2, [r3, #8]
	bx	lr
.L59:
	.align	2
.L58:
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
	ldr	r3, .L61
	add	r0, r3, r0, asl #3
	ldrb	r3, [r3, #58]	@ zero_extendqisi2
	ldrb	r2, [r0, #20]	@ zero_extendqisi2
	ldr	r1, [r0, #16]
	add	r2, r2, #8
	add	r2, r1, r2, asl #8
	str	r3, [r2, #8]
	bx	lr
.L62:
	.align	2
.L61:
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
	ldr	r0, .L66
	b	.L64
.L65:
	mov	ip, r0, asl #5
	ldrb	r4, [r3, r2]	@ zero_extendqisi2
	add	ip, ip, r0, lsr #2
	add	r2, r2, #1
	add	ip, ip, r4
	eor	r0, r0, ip
.L64:
	cmp	r2, r1
	bne	.L65
	ldmfd	sp!, {r4, pc}
.L67:
	.align	2
.L66:
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
	ldr	r1, .L71
	bl	memcpy
	mov	r0, #0
	ldmfd	sp!, {r3, pc}
.L72:
	.align	2
.L71:
	.word	.LANCHOR0+84
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
	ldr	r6, .L85
.L77:
	add	r5, r6, r4, asl #5
	ldr	r1, .L85+4
	add	r0, r5, #1
	ldrb	r2, [r6, r4, asl #5]	@ zero_extendqisi2
	bl	FlashMemCmp8
	subs	r1, r0, #0
	bne	.L74
	cmp	r5, #0
	ldrneb	r2, [r5, #22]	@ zero_extendqisi2
	ldrne	r3, .L85+8
	bne	.L79
	b	.L84
.L74:
	add	r4, r4, #1
	cmp	r4, #72
	bne	.L77
	mvn	r0, #0
	ldmfd	sp!, {r4, r5, r6, pc}
.L79:
	ldrb	r0, [r3, r1, asl #5]	@ zero_extendqisi2
	cmp	r0, r2
	beq	.L78
	add	r1, r1, #1
	cmp	r1, #4
	bne	.L79
.L78:
	ldr	r3, .L85+8
	mov	r2, #32
	ldr	r0, .L85+12
	add	r1, r3, r1, asl #5
	bl	memcpy
	ldr	r0, .L85+16
	mov	r1, r5
	mov	r2, #32
	bl	memcpy
	mov	r0, #0
	ldmfd	sp!, {r4, r5, r6, pc}
.L84:
	mvn	r0, #0
	ldmfd	sp!, {r4, r5, r6, pc}
.L86:
	.align	2
.L85:
	.word	.LANCHOR1+256
	.word	.LANCHOR0+2132
	.word	.LANCHOR1+2560
	.word	.LANCHOR0+48
	.word	.LANCHOR1+2688
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
	ldr	r3, .L88
	mov	r0, #0
	ldr	r2, [r3, #2164]
	ldr	r1, [r2, #0]
	str	r1, [r3, #2168]
	ldr	r1, [r2, #4]
	str	r1, [r3, #2172]
	ldr	r1, [r2, #8]
	str	r1, [r3, #2176]
	ldr	r1, [r2, #12]
	str	r1, [r3, #2180]
	ldr	r1, [r2, #304]
	str	r1, [r3, #2184]
	ldr	r1, [r2, #308]
	str	r1, [r3, #2188]
	ldr	r1, [r2, #336]
	ldr	r2, [r2, #344]
	str	r1, [r3, #2192]
	str	r2, [r3, #2196]
	bx	lr
.L89:
	.align	2
.L88:
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
	ldr	r3, .L95
	mov	r5, r1
	mov	r6, r2
	movw	r1, #2214
	movw	r2, #2212
	ldr	r8, [r0, #4]
	ldrh	r2, [r3, r2]
	mov	r4, r0
	ldrh	sl, [r3, r1]
	bic	r8, r8, #-2147483648
	ldrh	r7, [r3, #4]
	ubfx	r9, r8, #10, #16
	ldrb	r3, [r3, #0]	@ zero_extendqisi2
	mov	r8, r8, asl #22
	mul	sl, sl, r2
	cmp	r3, #1
	moveq	r7, r7, asl #1
	mov	r0, r9
	mov	r8, r8, lsr #22
	uxtheq	r7, r7
	uxth	sl, sl
	mov	r1, sl
	bl	__aeabi_uidiv
	cmp	r5, #1
	ldr	r3, .L95
	uxth	r0, r0
	mls	sl, sl, r0, r9
	uxth	sl, sl
	bne	.L92
	ldrb	r2, [r3, #80]	@ zero_extendqisi2
	cmp	r2, #0
	addeq	r8, r3, r8, asl #1
	movweq	r2, #2228
	ldreqh	r8, [r8, r2]
.L92:
	add	r3, r3, r0, asl #2
	ldr	r3, [r3, #2740]
	mla	r7, r7, sl, r3
	ldrb	r3, [sp, #40]	@ zero_extendqisi2
	cmp	r3, #1
	add	r8, r7, r8
	str	r8, [r6, #0]
	str	r0, [fp, #0]
	bls	.L94
	ldr	r0, [r4, #4]
	ldr	r3, [r4, #40]
	add	r0, r0, #1024
	rsb	r3, r3, r0
	rsbs	r0, r3, #0
	adc	r0, r0, r3
	ldmfd	sp!, {r3, r4, r5, r6, r7, r8, r9, sl, fp, pc}
.L94:
	mov	r0, #0
	ldmfd	sp!, {r3, r4, r5, r6, r7, r8, r9, sl, fp, pc}
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
	ldr	r2, [r3, #2772]
	str	r0, [r3, #2772]
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
	ldr	r3, [r3, #2776]
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
	add	r3, r3, r0, asl #3
	ldrb	r0, [r3, #20]	@ zero_extendqisi2
	ldr	r2, [r3, #16]
	add	r0, r0, #8
	add	r0, r2, r0, asl #8
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
	mov	r2, r0, asl #8
	orr	r2, r2, r0, asl #16
	orr	r2, r2, #1
	ldr	r3, [r3, #2164]
	str	r2, [r3, #304]
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
	ldr	r3, [r3, #2164]
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
	ldr	r3, .L115
	cmp	r0, #0
	ldr	r2, [r3, #2164]
	ldr	r3, [r2, #0]
	bfc	r3, #13, #1
	orrne	r3, r3, #253952
	str	r3, [r2, #0]
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
	ldr	r3, .L121
	stmfd	sp!, {r4, lr}
	.save {r4, lr}
	ldr	r3, [r3, #2164]
	ldr	r4, [r3, #0]
	ands	r3, r0, #6
	bfieq	r4, r3, #13, #1
	beq	.L120
	orr	r4, r4, #24576
	tst	r0, #4
	bfc	r4, #15, #1
	mov	r0, #0
	orr	r4, r4, #196608
	orrne	r4, r4, #32768
	bl	rknand_get_clk_rate
	ldr	r3, .L121
	movw	r2, #16641
	ldr	r3, [r3, #2164]
	str	r2, [r3, #344]
	ldr	r2, .L121+4
	str	r2, [r3, #304]
	mov	r2, #38
	str	r2, [r3, #308]
	mov	r2, #39
	str	r2, [r3, #308]
.L120:
	ldr	r3, .L121
	mov	r0, #0
	ldr	r3, [r3, #2164]
	str	r4, [r3, #0]
	ldmfd	sp!, {r4, pc}
.L122:
	.align	2
.L121:
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
	ldr	r3, .L124
	ldr	r3, [r3, #16]
	ldr	r2, [r3, #0]
	bfi	r2, r0, #0, #8
	str	r2, [r3, #0]
	bx	lr
.L125:
	.align	2
.L124:
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
	ldr	r3, .L127
	add	r3, r3, r0, asl #3
	mov	r0, #1
	ldr	r2, [r3, #16]
	ldrb	r1, [r3, #20]	@ zero_extendqisi2
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
	ldr	r2, [r3, #16]
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
	ldr	ip, [r3, #2776]
	ldrb	r5, [r0, #20]	@ zero_extendqisi2
	ldr	r4, [r0, #16]
	ldrb	ip, [ip, #8]	@ zero_extendqisi2
	add	r5, r5, #8
	cmp	ip, #2
	add	r0, r4, r5, asl #8
	movne	r3, #112
	strne	r3, [r0, #8]
	bne	.L140
	cmp	r2, #0
	ldreqb	r2, [r3, #61]	@ zero_extendqisi2
	ldrneb	r2, [r3, #62]	@ zero_extendqisi2
	str	r2, [r0, #8]
	ldrb	r3, [r3, #63]	@ zero_extendqisi2
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
	ldrb	ip, [r2, #63]	@ zero_extendqisi2
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
	ldr	r2, [r6, #2740]
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
	.word	.LANCHOR0+2780
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
	mov	sl, r3
	ldr	r3, .L161
	mov	r6, r2
	mov	r4, r0
	mov	r5, r1
	mov	r7, #0
	ldr	r2, [r3, #2776]
	ldrb	r2, [r2, #19]	@ zero_extendqisi2
	cmp	r2, #6
	addeq	r9, r3, #2928
	addne	r2, r3, #2928
	add	r3, r3, r0, asl #3
	addne	r9, sl, r0, asl #3
	addeq	r9, r9, r0, asl #6
	ldrb	r8, [r3, #20]	@ zero_extendqisi2
	addne	r9, r2, r9, asl #3
	ldr	r2, [r3, #16]
	addeq	r9, r9, sl, asl #2
	add	r8, r8, #8
	add	r8, r2, r8, asl #8
	bl	NandcFlashCs
	mov	r3, #54
	str	r3, [r8, #8]
	b	.L159
.L160:
	ldrb	r3, [r6, r7]	@ zero_extendqisi2
	mov	r0, #200
	str	r3, [r8, #4]
	bl	NandcDelayns
	ldrb	r3, [r9, r7]	@ zero_extendqisi2
	add	r7, r7, #1
	str	r3, [r8, #0]
.L159:
	uxtb	r3, r7
	cmp	r3, r5
	bcc	.L160
	mov	r3, #22
	mov	r0, r4
	str	r3, [r8, #8]
	bl	NandcFlashDeCs
	ldr	r3, .L161
	add	r4, r3, r4
	strb	sl, [r4, #3760]
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, sl, pc}
.L162:
	.align	2
.L161:
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
	ldr	r6, .L168
	ldr	r3, [r6, #2776]
	ldrb	r3, [r3, #19]	@ zero_extendqisi2
	sub	r3, r3, #1
	uxtb	r3, r3
	cmp	r3, #5
	ldmhifd	sp!, {r4, r5, r6, pc}
	ldr	r5, .L168+4
	mov	r4, #0
.L166:
	ldrb	r3, [r5, r4, asl #3]	@ zero_extendqisi2
	uxtb	r0, r4
	cmp	r3, #173
	bne	.L165
	ldrb	r1, [r6, #2909]	@ zero_extendqisi2
	mov	r3, #0
	ldr	r2, .L168+8
	bl	HynixSetRRPara
.L165:
	add	r4, r4, #1
	cmp	r4, #4
	bne	.L166
	ldmfd	sp!, {r4, r5, r6, pc}
.L169:
	.align	2
.L168:
	.word	.LANCHOR0
	.word	.LANCHOR0+2132
	.word	.LANCHOR0+2912
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
	ldr	r3, .L173
	mov	r5, #0
	ldr	sl, .L173+4
	mov	r8, #161
	add	r1, r3, r1, asl #2
	add	r6, r1, #3
	mov	r7, r3
	b	.L171
.L172:
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
.L171:
	ldrb	r3, [sl, #3764]	@ zero_extendqisi2
	cmp	r5, r3
	bcc	.L172
	ldmfd	sp!, {r3, r4, r5, r6, r7, r8, sl, pc}
.L174:
	.align	2
.L173:
	.word	.LANCHOR1+2720
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
	stmfd	sp!, {r3, r4, r5, r6, r7, r8, sl, lr}
	.save {r3, r4, r5, r6, r7, r8, sl, lr}
	add	r6, r1, #1
	ldr	r3, .L182
	mov	r4, r0
	add	r6, r6, r6, asl #2
	mov	r5, #0
	add	r2, r3, #45
	sub	sl, r3, #2784
	add	r7, r3, r6
	ldr	r8, .L182+4
	add	r6, r2, r6
	add	sl, sl, r1
	b	.L176
.L180:
	mov	r3, #85
	str	r3, [r4, #8]
	ldr	r3, .L182
	mov	r0, #200
	ldrsb	r3, [r5, r3]
	str	r3, [r4, #4]
	bl	NandcDelayns
	ldrb	r3, [r8, #3765]	@ zero_extendqisi2
	cmp	r3, #34
	ldreqsb	r3, [r7, #0]
	beq	.L181
	cmp	r3, #35
	addne	r3, sl, #2912
	addne	r3, r3, #12
	ldreqsb	r3, [r6, #0]
	ldrnesb	r3, [r3, #0]
.L181:
	add	r5, r5, #1
	add	r7, r7, #1
	add	r6, r6, #1
	str	r3, [r4, #0]
.L176:
	ldrb	r3, [r8, #3764]	@ zero_extendqisi2
	cmp	r5, r3
	bcc	.L180
	ldmfd	sp!, {r3, r4, r5, r6, r7, r8, sl, pc}
.L183:
	.align	2
.L182:
	.word	.LANCHOR1+2784
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
	ldr	r3, .L185
	add	r3, r3, r0, asl #3
	mov	r0, #80
	ldrb	r5, [r3, #20]	@ zero_extendqisi2
	ldr	r4, [r3, #16]
	add	r5, r5, #8
	add	r3, r4, r5, asl #8
	str	r2, [r3, #8]
	bl	NandcDelayns
	ldr	r0, [r4, r5, asl #8]
	ldmfd	sp!, {r3, r4, r5, pc}
.L186:
	.align	2
.L185:
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
	ldr	r3, .L192
	stmfd	sp!, {r0, r1, r2, r4, r5, lr}
	.save {r0, r1, r2, r4, r5, lr}
	add	r3, r3, r0, asl #3
	ldr	r4, .L192+4
	ldr	r5, [r3, #16]
.L189:
	mov	r0, #100
	bl	NandcDelayns
	ldr	r3, [r5, #0]
	str	r3, [sp, #4]
	ldr	r3, [sp, #4]
	tst	r3, #512
	bne	.L190
	subs	r4, r4, #1
	bne	.L189
	mvn	r0, #0
	b	.L188
.L190:
	mov	r0, #0
.L188:
	ldmfd	sp!, {r1, r2, r3, r4, r5, pc}
.L193:
	.align	2
.L192:
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
	ldr	r5, .L202
	b	.L195
.L201:
	add	r3, r6, r8
	add	r2, sp, #8
	mov	r0, r4
	mov	r1, #0
	uxtb	r3, r3
	str	r3, [sp, #0]
	add	r3, sp, #12
	bl	LogAddr2PhyAddr
	ldr	r3, [sp, #12]
	ldrb	r2, [r5, #3766]	@ zero_extendqisi2
	cmp	r3, r2
	mvncs	r3, #0
	strcs	r3, [r4, #0]
	bcs	.L197
	add	r2, r5, r3
	add	r3, r5, r3, asl #4
	ldrb	sl, [r2, #3768]	@ zero_extendqisi2
	strb	sl, [r3, #2780]
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
	ldr	r1, [r5, #4]
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
	bne	.L200
	ldr	r0, .L202+4
	ldr	r1, [sp, #8]
	bl	printk
.L200:
	mov	r0, sl
	bl	NandcFlashDeCs
.L197:
	sub	r6, r6, #1
	add	r7, r7, #1
	add	r4, r4, #36
	uxtb	r6, r6
.L195:
	cmp	r7, r8
	bne	.L201
	mov	r0, #0
	add	sp, sp, #20
	ldmfd	sp!, {r4, r5, r6, r7, r8, sl, pc}
.L203:
	.align	2
.L202:
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
	ldr	r1, .L210
	add	r3, r5, r5, asl #2
	mvn	r6, #4
	add	r0, r1, r3
	add	r1, r1, #45
	add	r1, r1, r3
	mov	r2, #0
	ldr	ip, .L210+4
	mul	r5, r6, r5
	b	.L205
.L208:
	ldrb	r6, [ip, #3765]	@ zero_extendqisi2
	add	r2, r2, #1
	cmp	r6, #67
	addeq	r6, r0, r5
	addne	r6, r1, r5
	add	r6, r6, r3
	add	r0, r0, #1
	add	r1, r1, #1
	ldrsb	r6, [r6, #5]
	str	r6, [r4, #0]
.L205:
	ldrb	r6, [ip, #3764]	@ zero_extendqisi2
	cmp	r2, r6
	bcc	.L208
	mov	r0, #0
	ldmfd	sp!, {r4, r5, r6, lr}
	b	NandcWaitFlashReady
.L211:
	.align	2
.L210:
	.word	.LANCHOR1+2784
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
	ldr	r3, .L213
	stmfd	sp!, {r4, r5, r6, lr}
	.save {r4, r5, r6, lr}
	add	r3, r3, r0, asl #3
	ldrb	r6, [r3, #20]	@ zero_extendqisi2
	ldr	r5, [r3, #16]
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
.L214:
	.align	2
.L213:
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
	ldr	r3, .L216
	mov	ip, #0
	stmfd	sp!, {r4, r5, r6, lr}
	.save {r4, r5, r6, lr}
	mov	r6, r2
	ldrb	r2, [r3, #2697]	@ zero_extendqisi2
	ldr	r3, .L216+4
	add	r3, r3, r0, asl #3
	mov	r2, r2, asl #9
	ldrb	r5, [r3, #20]	@ zero_extendqisi2
	ldr	r4, [r3, #16]
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
.L217:
	.align	2
.L216:
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
	ldr	r3, .L220
	add	r3, r3, r0, asl #3
	ldrb	r5, [r3, #20]	@ zero_extendqisi2
	ldr	r2, [r3, #16]
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
.L221:
	.align	2
.L220:
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
	ldr	r3, .L224
	mov	r0, r4
	add	r3, r3, r4, asl #3
	ldrb	r7, [r3, #20]	@ zero_extendqisi2
	ldr	r6, [r3, #16]
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
	ldr	r3, [r6, r7, asl #8]
	strb	r3, [r5, #6]
	ldr	r3, [r6, r7, asl #8]
	strb	r3, [r5, #7]
	bl	NandcFlashDeCs
	ldrb	r2, [r5, #0]	@ zero_extendqisi2
	sub	r3, r2, #1
	uxtb	r3, r3
	cmp	r3, #253
	bhi	.L222
	ldrb	r1, [r5, #2]	@ zero_extendqisi2
	ldrb	r3, [r5, #1]	@ zero_extendqisi2
	ldr	r0, .L224+4
	str	r1, [sp, #0]
	ldrb	r1, [r5, #3]	@ zero_extendqisi2
	str	r1, [sp, #4]
	ldrb	r1, [r5, #4]	@ zero_extendqisi2
	str	r1, [sp, #8]
	ldrb	r1, [r5, #5]	@ zero_extendqisi2
	str	r1, [sp, #12]
	add	r1, r4, #1
	bl	printk
.L222:
	ldmfd	sp!, {r0, r1, r2, r3, r4, r5, r6, r7, r8, pc}
.L225:
	.align	2
.L224:
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
	ldr	r9, .L240
	mov	r7, #239
	ldr	sl, .L240+4
	mov	r1, r4
	sub	r8, r9, #16
	mov	r6, #128
	mov	r5, #1
.L236:
	mov	r2, r9
	ldrb	lr, [r4, sl]	@ zero_extendqisi2
	ldr	r3, [r2, r4]!
	cmp	lr, #152
	cmpne	lr, #69
	ldrb	r2, [r2, #4]	@ zero_extendqisi2
	add	r2, r2, #8
	add	ip, r3, r2, asl #8
	beq	.L227
	cmp	lr, #173
	cmpne	lr, #44
	bne	.L228
.L227:
	cmp	r0, #1
	ldrb	fp, [r8, #3776]	@ zero_extendqisi2
	bne	.L229
	tst	fp, #1
	beq	.L228
	cmp	lr, #173
	str	r7, [ip, #8]
	streq	r0, [ip, #4]
	beq	.L239
	cmp	lr, #44
	streq	r0, [ip, #4]
	strne	r6, [ip, #4]
	moveq	ip, #5
	strne	r0, [r3, r2, asl #8]
	bne	.L234
	b	.L238
.L229:
	tst	fp, #4
	beq	.L228
	cmp	lr, #173
	str	r7, [ip, #8]
	streq	r5, [ip, #4]
	moveq	ip, #32
	beq	.L238
	cmp	lr, #44
	strne	r6, [ip, #4]
	bne	.L239
	str	r5, [ip, #4]
	mov	ip, #35
.L238:
	str	ip, [r3, r2, asl #8]
	b	.L234
.L239:
	str	r1, [r3, r2, asl #8]
.L234:
	str	r1, [r3, r2, asl #8]
	str	r1, [r3, r2, asl #8]
	str	r1, [r3, r2, asl #8]
.L228:
	add	r4, r4, #8
	cmp	r4, #32
	bne	.L236
	mov	r0, #0
	bl	NandcWaitFlashReady
	mov	r0, #0
	ldmfd	sp!, {r3, r4, r5, r6, r7, r8, r9, sl, fp, pc}
.L241:
	.align	2
.L240:
	.word	.LANCHOR0+16
	.word	.LANCHOR0+2132
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
	ldr	r4, .L244
	bl	NandcWaitFlashReady
	bl	FlashSetReadRetryDefault
	ldrb	r3, [r4, #3777]	@ zero_extendqisi2
	cmp	r3, #0
	beq	.L243
	ldrb	r3, [r4, #3776]	@ zero_extendqisi2
	tst	r3, #1
	beq	.L243
	mov	r0, #1
	bl	FlashSetInterfaceMode
	mov	r0, #1
	bl	NandcSetMode
	mov	r3, #0
	strb	r3, [r4, #3777]
.L243:
	ldr	r3, [r4, #16]
	mov	r0, #0
	str	r0, [r3, #336]
	ldmfd	sp!, {r4, pc}
.L245:
	.align	2
.L244:
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
	ldr	r3, .L250
	stmfd	sp!, {r4, r5, r6, r7, r8, lr}
	.save {r4, r5, r6, r7, r8, lr}
	mov	r7, r2
	add	r2, r3, r0, asl #3
	mov	r5, r1
	mov	r4, r0
	ldrb	r6, [r2, #20]	@ zero_extendqisi2
	ldr	r1, [r2, #16]
	ldrb	r2, [r3, #64]	@ zero_extendqisi2
	add	r6, r6, #8
	cmp	r2, #1
	ldrb	r2, [r3, #56]	@ zero_extendqisi2
	add	r6, r1, r6, asl #8
	str	r2, [r6, #8]
	bne	.L247
	mov	r8, #0
	uxtb	r2, r5
	str	r8, [r6, #4]
	str	r8, [r6, #4]
	str	r2, [r6, #4]
	mov	r2, r5, lsr #8
	str	r2, [r6, #4]
	mov	r2, r5, lsr #16
	str	r2, [r6, #4]
	ldrb	r3, [r3, #57]	@ zero_extendqisi2
	str	r3, [r6, #8]
	bl	NandcWaitFlashReady
	str	r8, [r6, #8]
	str	r8, [r6, #4]
	str	r8, [r6, #4]
	b	.L249
.L247:
	uxtb	r2, r5
	str	r2, [r6, #4]
	mov	r2, r5, lsr #8
	str	r2, [r6, #4]
	mov	r2, r5, lsr #16
	str	r2, [r6, #4]
	ldrb	r3, [r3, #57]	@ zero_extendqisi2
	str	r3, [r6, #8]
.L249:
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
.L251:
	.align	2
.L250:
	.word	.LANCHOR0
	.fnend
	.size	FlashReadDpCmd, .-FlashReadDpCmd
	.align	2
	.global	HynixGetReadRetryDefault
	.type	HynixGetReadRetryDefault, %function
HynixGetReadRetryDefault:
	.fnstart
	@ args = 0, pretend = 0, frame = 64
	@ frame_needed = 0, uses_anonymous_args = 0
	ldr	r3, .L311
	mvn	r2, #83
	stmfd	sp!, {r4, r5, r6, r7, r8, r9, sl, fp, lr}
	.save {r4, r5, r6, r7, r8, r9, sl, fp, lr}
	mov	r4, r0
	cmp	r4, #2
	strb	r0, [r3, #2908]
	strb	r2, [r3, #2912]
	mvn	r0, #82
	mvn	r1, #81
	mvn	r2, #80
	.pad #68
	sub	sp, sp, #68
	strb	r0, [r3, #2913]
	strb	r1, [r3, #2914]
	strb	r2, [r3, #2915]
	bne	.L253
	mvn	r2, #88
	strb	r2, [r3, #2912]
	ldr	r3, .L311+4
	mvn	r2, #8
	strb	r2, [r3, #2949]
	b	.L292
.L253:
	cmp	r4, #3
	bne	.L255
	mvn	r2, #79
	strb	r2, [r3, #2912]
	mvn	r2, #78
	strb	r2, [r3, #2913]
	mvn	r2, #77
	strb	r2, [r3, #2914]
	mvn	r2, #76
	strb	r2, [r3, #2915]
	mvn	r2, #75
	strb	r2, [r3, #2916]
	mvn	r2, #74
	strb	r2, [r3, #2917]
	mvn	r2, #73
	strb	r2, [r3, #2918]
	mvn	r2, #72
	b	.L307
.L255:
	cmp	r4, #4
	bne	.L256
	mvn	ip, #51
	strb	r0, [r3, #2917]
	strb	ip, [r3, #2912]
	mvn	ip, #64
	strb	r1, [r3, #2918]
	strb	ip, [r3, #2913]
	mvn	ip, #85
	strb	ip, [r3, #2914]
	mvn	ip, #84
	strb	ip, [r3, #2915]
	mvn	ip, #50
	strb	ip, [r3, #2916]
.L307:
	mov	r6, #8
	strb	r2, [r3, #2919]
	mov	r5, r6
	b	.L254
.L256:
	cmp	r4, #5
	bne	.L257
	mov	r2, #56
	mov	r6, #8
	strb	r2, [r3, #2912]
	mov	r2, #57
	strb	r2, [r3, #2913]
	mov	r2, #58
	strb	r2, [r3, #2914]
	mov	r2, #59
	strb	r2, [r3, #2915]
	b	.L306
.L257:
	cmp	r4, #6
	bne	.L292
	mov	r2, #14
	mov	r6, #12
	strb	r2, [r3, #2912]
	mov	r2, #15
	strb	r2, [r3, #2913]
	mov	r2, #16
	strb	r2, [r3, #2914]
	mov	r2, #17
	strb	r2, [r3, #2915]
	b	.L306
.L292:
	mov	r6, #7
.L306:
	mov	r5, #4
.L254:
	sub	r3, r4, #1
	cmp	r3, #1
	bhi	.L305
	b	.L310
.L264:
	add	r1, r2, r8
	add	r4, r2, #2928
	ldr	fp, .L311+8
	mov	r7, #0
	ldrb	r1, [r1, #3768]	@ zero_extendqisi2
	mov	r5, #55
	add	r2, r2, r1, asl #3
	add	r4, r4, r1, asl #6
	ldrb	sl, [r2, #20]	@ zero_extendqisi2
	ldr	r1, [r2, #16]
	add	sl, sl, #8
	add	sl, r1, sl, asl #8
.L260:
	add	r2, fp, r7
	str	r5, [sl, #8]
	mov	r0, #80
	ldrb	r2, [r2, #4]	@ zero_extendqisi2
	str	r2, [sl, #4]
	str	r3, [sp, #8]
	bl	NandcDelayns
	ldr	r2, [sl, #0]
	strb	r2, [r4, r7]
	add	r7, r7, #1
	ldr	r3, [sp, #8]
	uxtb	r2, r7
	cmp	r2, r6
	bcc	.L260
	mov	r2, #0
	b	.L261
.L262:
	ldrb	r7, [r0, r1, asl #2]	@ zero_extendqisi2
	ldrb	r5, [r4, r2]	@ zero_extendqisi2
	add	r5, r7, r5
	strb	r5, [ip, r1, asl #3]
	add	r1, r1, #1
	cmp	r1, #7
	bne	.L262
	add	r2, r2, #1
	cmp	r2, #4
	beq	.L263
.L261:
	mov	r1, #1
	add	ip, r4, r2
	add	r0, r2, r9
	b	.L262
.L263:
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
	b	.L258
.L310:
	ldr	r9, .L311+12
	mov	r3, r6
	mov	r8, #0
	mov	r6, r5
.L258:
	ldr	r2, .L311
	ldrb	r1, [r2, #3766]	@ zero_extendqisi2
	cmp	r1, r8
	bhi	.L264
	mov	r5, r6
	mov	r6, r3
	b	.L265
.L305:
	sub	r3, r4, #3
	cmp	r3, #3
	bhi	.L265
	mul	r3, r6, r5
	ldr	r8, .L311
	str	r6, [sp, #44]
	mov	r2, r3, asr #2
	mov	r3, r3, asl #4
	str	r3, [sp, #52]
	mov	r3, #0
	str	r2, [sp, #24]
	mov	r7, r3
	str	r3, [sp, #12]
	b	.L266
.L291:
	ldr	ip, [sp, #12]
	ldr	r1, .L311+16
	add	r3, r8, ip
	ldrb	r6, [r3, #3768]	@ zero_extendqisi2
	add	r3, r8, r6, asl #3
	mov	r0, r6
	add	r1, r1, r6, asl #6
	str	r1, [sp, #48]
	ldrb	fp, [r3, #20]	@ zero_extendqisi2
	ldr	r9, [r3, #16]
	mov	r3, #255
	add	fp, fp, #8
	add	sl, r9, fp, asl #8
	str	r3, [sl, #8]
	str	r3, [sp, #8]
	bl	NandcWaitFlashReady
	cmp	r4, #4
	mov	r2, #54
	str	r2, [sl, #8]
	ldr	r3, [sp, #8]
	streq	r3, [sl, #4]
	moveq	r3, #64
	streq	r3, [r9, fp, asl #8]
	moveq	r3, #204
	beq	.L308
.L267:
	sub	r3, r4, #5
	cmp	r3, #1
	ldrls	r2, .L311+8
	ldrlsb	r3, [r2, #4]	@ zero_extendqisi2
	strls	r3, [sl, #4]
	movls	r3, #82
	bls	.L309
.L269:
	mov	r3, #174
	str	r3, [sl, #4]
	mov	r3, #176
	str	r7, [r9, fp, asl #8]
.L308:
	str	r3, [sl, #4]
	mov	r3, #77
.L309:
	cmp	r4, #6
	str	r3, [r9, fp, asl #8]
	mov	r0, r6
	mov	r3, #22
	str	r3, [sl, #8]
	mov	r3, #23
	str	r3, [sl, #8]
	mov	r3, #4
	str	r3, [sl, #8]
	mov	r3, #25
	str	r3, [sl, #8]
	moveq	r3, #31
	str	r7, [sl, #8]
	str	r7, [sl, #4]
	str	r7, [sl, #4]
	streq	r3, [sl, #4]
	mov	r3, #2
	strne	r7, [sl, #4]
	str	r3, [sl, #4]
	mov	r3, #48
	str	r7, [sl, #4]
	str	r3, [sl, #8]
	bl	NandcWaitFlashReady
	sub	r3, r4, #5
	cmp	r3, #1
	str	r3, [sp, #28]
	mov	r3, #0
	movls	r2, #16
	movhi	r2, #2
.L273:
	add	r3, r3, #1
	ldr	r1, [sl, #0]
	uxtb	r3, r3
	cmp	r3, r2
	bcc	.L273
	ldr	ip, [r8, #3780]
	mov	r3, #0
	str	ip, [sp, #32]
.L274:
	ldr	r2, [sl, #0]
	ldr	lr, [sp, #32]
	strb	r2, [lr, r3]
	add	r3, r3, #1
	ldr	r2, [sp, #52]
	cmp	r3, r2
	blt	.L274
	ldr	r3, [sp, #24]
	mov	ip, r3, asl #2
	mov	lr, r3, asl #3
	ldr	r3, .L311
	str	ip, [sp, #40]
	ldr	r1, [r3, #3780]
	mov	r3, #8
	add	r1, r1, ip
.L276:
	mov	r2, #0
	mov	r0, r2
.L275:
	ldr	ip, [r1, r2]
	add	r0, r0, #1
	mvn	ip, ip
	str	ip, [r1, r2]
	ldr	ip, [sp, #24]
	add	r2, r2, #4
	cmp	r0, ip
	blt	.L275
	subs	r3, r3, #1
	add	r1, r1, lr
	bne	.L276
	str	r3, [sp, #20]
	str	r5, [sp, #60]
	b	.L277
.L281:
	mov	lr, #1
	mov	r0, #0
	mov	ip, lr, asl r2
	mov	lr, r0
	mov	r5, #16
	str	r3, [sp, #0]
	str	r5, [sp, #16]
.L279:
	ldr	r3, [sp, #56]
	ldr	r3, [r3, r0]
	and	r5, ip, r3
	ldr	r3, [sp, #16]
	cmp	r5, ip
	ldr	r5, [sp, #40]
	addeq	lr, lr, #1
	subs	r3, r3, #1
	add	r0, r0, r5
	str	r3, [sp, #16]
	bne	.L279
	cmp	lr, #8
	add	r2, r2, #1
	orrhi	r1, r1, ip
	cmp	r2, #32
	ldr	r3, [sp, #0]
	bne	.L281
	ldr	ip, [sp, #20]
	ldr	r2, [sp, #24]
	add	ip, ip, #1
	ldr	r5, [sp, #36]
	cmp	ip, r2
	str	ip, [sp, #20]
	str	r1, [r5, r3]
	add	r3, r3, #4
	bge	.L282
.L277:
	ldr	r5, [r8, #3780]
	mov	r1, #0
	mov	r2, r1
	add	ip, r5, r3
	str	ip, [sp, #56]
	str	r5, [sp, #36]
	b	.L281
.L282:
	cmp	r4, #6
	ldr	r5, [sp, #60]
	ldr	r1, [sp, #48]
	mov	r2, #0
	moveq	lr, #4
	movne	lr, #8
	cmp	r5, #0
	str	lr, [sp, #20]
	subne	r3, r5, #1
	moveq	r0, #1
	streq	r0, [sp, #16]
	uxtbne	r3, r3
	ldr	r0, [sp, #32]
	addne	r3, r3, #1
	strne	r3, [sp, #16]
	b	.L286
.L287:
	ldrb	lr, [ip], #1	@ zero_extendqisi2
	strb	lr, [r1, r3]
	add	r3, r3, #1
	uxtb	lr, r3
	cmp	lr, r5
	bcc	.L287
	ldr	r3, [sp, #16]
	add	r2, r2, #1
	ldr	ip, [sp, #20]
	add	r0, r0, r3
	ldr	r3, [sp, #44]
	add	r1, r1, ip
	cmp	r2, r3
	bge	.L288
.L286:
	mov	ip, r0
	mov	r3, #0
	b	.L287
.L288:
	mov	r3, #255
	mov	r0, r6
	str	r3, [sl, #8]
	bl	NandcWaitFlashReady
	ldr	ip, [sp, #28]
	cmp	ip, #1
	movhi	r3, #56
	strhi	r3, [sl, #8]
	bhi	.L290
	ldr	lr, .L311+8
	mov	r3, #54
	str	r3, [sl, #8]
	mvn	r1, #0
	ldr	r0, [sp, #12]
	ldrb	r3, [lr, #4]	@ zero_extendqisi2
	str	r3, [sl, #4]
	mov	r3, #22
	str	r7, [r9, fp, asl #8]
	str	r3, [sl, #8]
	bl	FlashReadCmd
.L290:
	mov	r0, r6
	bl	NandcWaitFlashReady
	ldr	r2, [sp, #12]
	add	r3, r2, #1
	uxtb	r3, r3
	str	r3, [sp, #12]
.L266:
	ldrb	r3, [r8, #3766]	@ zero_extendqisi2
	ldr	ip, [sp, #12]
	cmp	r3, ip
	bhi	.L291
	ldr	r6, [sp, #44]
.L265:
	ldr	r3, .L311
	strb	r5, [r3, #2909]
	strb	r6, [r3, #2910]
	add	sp, sp, #68
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, sl, fp, pc}
.L312:
	.align	2
.L311:
	.word	.LANCHOR0
	.word	.LANCHOR1
	.word	.LANCHOR0+2908
	.word	.LANCHOR1+2932
	.word	.LANCHOR0+2928
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
	cmp	r2, #5
	bhi	.L315
	ldmfd	sp!, {r3, lr}
	b	HynixGetReadRetryDefault
.L315:
	cmp	r3, #49
	bne	.L316
	ldr	r0, .L322
	mov	r2, #64
	ldr	r1, .L322+4
	strb	r3, [r0, #2908]
	mov	r3, #4
	strb	r3, [r0, #2909]
	mov	r3, #15
	strb	r3, [r0, #2910]
	add	r0, r0, #2912
	b	.L320
.L316:
	cmp	r3, #33
	cmpne	r3, #65
	beq	.L317
	cmp	r3, #66
	bne	.L318
.L317:
	ldr	r0, .L322
	strb	r3, [r0, #2908]
	mov	r3, #4
	b	.L321
.L318:
	cmp	r3, #34
	cmpne	r3, #67
	bne	.L319
	ldr	r0, .L322
	strb	r3, [r0, #2908]
	mov	r3, #5
.L321:
	strb	r3, [r0, #2909]
	mov	r2, #45
	mov	r3, #7
	ldr	r1, .L322+8
	strb	r3, [r0, #2910]
	add	r0, r0, #2912
	b	.L320
.L319:
	cmp	r3, #35
	cmpne	r3, #68
	ldmnefd	sp!, {r3, pc}
	ldr	r0, .L322
	mov	r2, #95
	ldr	r1, .L322+12
	strb	r3, [r0, #2908]
	mov	r3, #5
	strb	r3, [r0, #2909]
	mov	r3, #17
	strb	r3, [r0, #2910]
	add	r0, r0, #2912
.L320:
	bl	memcpy
	ldmfd	sp!, {r3, pc}
.L323:
	.align	2
.L322:
	.word	.LANCHOR0
	.word	.LANCHOR1+2720
	.word	.LANCHOR1+2784
	.word	.LANCHOR1+2829
	.fnend
	.size	FlashGetReadRetryDefault, .-FlashGetReadRetryDefault
	.align	2
	.global	NandcRandmzSel
	.type	NandcRandmzSel, %function
NandcRandmzSel:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	@ link register save eliminated.
	ldr	r3, .L325
	add	r3, r3, r0, asl #3
	ldr	r3, [r3, #16]
	str	r1, [r3, #336]
	bx	lr
.L326:
	.align	2
.L325:
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
	ldr	r1, .L337
	bl	__aeabi_idiv
	ldr	r3, .L337+4
	ldr	r3, [r3, #2164]
	cmp	r0, #250
	movwgt	r2, #8354
	bgt	.L335
	cmp	r0, #220
	bgt	.L336
.L330:
	cmp	r0, #185
	movwgt	r2, #4226
	bgt	.L335
	cmp	r0, #160
	movwgt	r2, #4194
	bgt	.L335
	cmp	r4, #35
	movwls	r2, #4193
	bls	.L335
	cmp	r4, #99
	movwls	r2, #4225
	bls	.L335
.L336:
	movw	r2, #8322
.L335:
	str	r2, [r3, #4]
	ldmfd	sp!, {r4, pc}
.L338:
	.align	2
.L337:
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
	ldr	r4, .L340
	mov	r3, #1
	str	r0, [r4, #2164]
	str	r0, [r4, #16]
	str	r3, [r4, #28]
	mov	r3, #2
	str	r0, [r4, #24]
	str	r3, [r4, #36]
	mov	r3, #3
	str	r0, [r4, #32]
	str	r0, [r4, #40]
	str	r5, [r4, #20]
	str	r3, [r4, #44]
	ldr	r3, [r0, #0]
	and	r3, r3, #253952
	ubfx	r2, r3, #13, #1
	str	r2, [r4, #3784]
	ldr	r2, [r0, #352]
	and	r3, r3, #245760
	orr	r3, r3, #256
	ubfx	r2, r2, #16, #4
	str	r2, [r4, #12]
	ldr	r2, [r0, #352]
	str	r2, [r4, #3788]
	str	r3, [r0, #0]
	mov	r0, #40
	ldr	r3, [r4, #2164]
	str	r5, [r3, #336]
	bl	NandcTimeCfg
	ldr	r3, [r4, #2164]
	movw	r2, #8322
	mov	r0, #36864
	str	r2, [r3, #344]
	ldr	r2, .L340+4
	str	r2, [r3, #304]
	bl	ftl_malloc
	str	r5, [r4, #3820]
	str	r5, [r4, #3828]
	str	r0, [r4, #3792]
	str	r0, [r4, #3796]
	add	r0, r0, #32768
	str	r0, [r4, #3800]
	ldmfd	sp!, {r3, r4, r5, pc}
.L341:
	.align	2
.L340:
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
	bls	.L343
	movw	r3, #4226
	movw	r2, #8322
	cmp	r0, r3
	cmpne	r0, r2
	bne	.L344
.L343:
	ldr	r3, .L345
	ldr	r3, [r3, #2164]
	str	r0, [r3, #4]
.L344:
	ldr	r3, .L345+4
	ldrb	r0, [r3, #2709]	@ zero_extendqisi2
	b	NandcTimeCfg
.L346:
	.align	2
.L345:
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
	ldr	ip, .L348
	stmfd	sp!, {r4, lr}
	.save {r4, lr}
	ldr	r4, [ip, #2164]
	ldr	r4, [r4, #4]
	str	r4, [r0, #0]
	ldr	r0, [ip, #2164]
	ldr	r0, [r0, #0]
	str	r0, [r1, #0]
	ldr	r1, [ip, #2164]
	ldr	r1, [r1, #304]
	str	r1, [r2, #0]
	ldr	r2, [ip, #2164]
	ldr	r0, [r2, #308]
	ldr	r1, [r2, #344]
	uxtb	r2, r0
	orr	r2, r2, r1, asl #16
	str	r2, [r3, #0]
	ldmfd	sp!, {r4, pc}
.L349:
	.align	2
.L348:
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
	ldr	r3, .L355
	mov	r1, #1
	ldr	r2, [r3, #2164]
	str	r0, [r3, #3832]
	mov	r3, #0
	str	r1, [r2, #8]
	mov	r1, #16
	cmp	r0, r1
	bfi	r3, r1, #8, #8
	bfc	r3, #18, #1
	beq	.L354
.L351:
	cmp	r0, #24
	orreq	r3, r3, #16
	beq	.L352
	cmp	r0, #40
	orr	r3, r3, #262144
	orr	r3, r3, #16
	bne	.L352
.L354:
	bfc	r3, #4, #1
.L352:
	orr	r3, r3, #1
	str	r3, [r2, #12]
	bx	lr
.L356:
	.align	2
.L355:
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
	ldr	r3, .L358
	strb	r0, [r3, #3836]
	b	NandcBchSel
.L359:
	.align	2
.L358:
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
	ldr	r3, .L365
	ldr	r5, .L365+4
	ldr	r2, [r3, #2164]
	ldr	r1, [r3, #2168]
	str	r1, [r2, #0]
	ldr	r1, [r3, #2172]
	ldr	r2, [r3, #2164]
	str	r1, [r2, #4]
	ldr	r1, [r3, #2176]
	str	r1, [r2, #8]
	ldr	r1, [r3, #2180]
	str	r1, [r2, #12]
	ldr	r1, [r3, #2184]
	str	r1, [r2, #304]
	ldr	r1, [r3, #2188]
	str	r1, [r2, #308]
	ldr	r1, [r3, #2192]
	ldr	r3, [r3, #2196]
	str	r1, [r2, #336]
	str	r3, [r2, #344]
.L362:
	ldrb	r3, [r5, r4, asl #3]	@ zero_extendqisi2
	sub	r3, r3, #1
	uxtb	r3, r3
	cmp	r3, #253
	bhi	.L361
	uxtb	r0, r4
	bl	FlashReset
.L361:
	add	r4, r4, #1
	cmp	r4, #4
	bne	.L362
	ldr	r4, .L365
	ldrb	r3, [r4, #3777]	@ zero_extendqisi2
	cmp	r3, #0
	beq	.L363
	mov	r0, #1
	bl	NandcSetMode
	ldrb	r0, [r4, #3776]	@ zero_extendqisi2
	bl	FlashSetInterfaceMode
	ldrb	r0, [r4, #3776]	@ zero_extendqisi2
	bl	NandcSetMode
	ldrb	r0, [r4, #2185]	@ zero_extendqisi2
	bl	NandcSetDdrPara
.L363:
	ldr	r3, .L365
	ldr	r3, [r3, #2776]
	ldrb	r0, [r3, #20]	@ zero_extendqisi2
	bl	FlashBchSel
	mov	r0, #0
	ldmfd	sp!, {r3, r4, r5, pc}
.L366:
	.align	2
.L365:
	.word	.LANCHOR0
	.word	.LANCHOR0+2132
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
	bne	.L372
	ldmfd	sp!, {r4, lr}
	b	wait_for_nand_flash_ready
.L372:
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
	ldr	r5, .L388
	mov	r6, r0
	str	r1, [sp, #12]
	mov	r7, r2
	ldrb	r4, [r5, #0]	@ zero_extendqisi2
	cmp	r4, #0
	moveq	r9, r5
	beq	.L374
	mov	r1, r2
	bl	FlashEraseSLc2KBlocks
	b	.L375
.L382:
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
	ldrb	r3, [r5, #3766]	@ zero_extendqisi2
	mov	sl, r0
	ldr	r0, [sp, #20]
	cmp	r0, r3
	mvncs	r3, #0
	strcs	r3, [r6, r8]
	bcs	.L377
	ldrb	r3, [r5, #3837]	@ zero_extendqisi2
	cmp	r3, #0
	add	r3, r9, r0, asl #4
	ldr	r3, [r3, #2788]
	moveq	sl, #0
	cmp	r3, #0
	beq	.L379
	uxtb	r0, r0
	bl	FlashWaitCmdDone
.L379:
	ldr	r2, [sp, #20]
	mov	r1, #0
	cmp	sl, #0
	add	r3, r5, r2, asl #4
	movne	r0, #36
	str	r1, [r3, #2792]
	ldr	r1, [sp, #16]
	str	fp, [r3, #2788]
	str	r1, [r3, #2784]
	addne	r1, r4, #1
	mlane	r1, r0, r1, r6
	strne	r1, [r3, #2792]
	add	r3, r9, r2
	add	r2, r9, r2, asl #4
	ldrb	r8, [r3, #3768]	@ zero_extendqisi2
	strb	r8, [r2, #2780]
	mov	r0, r8
	bl	NandcFlashCs
	ldr	r3, [sp, #12]
	cmp	r3, #1
	bne	.L381
	ldrb	r3, [r5, #80]	@ zero_extendqisi2
	cmp	r3, #0
	beq	.L381
	mov	r0, r8
	bl	flash_enter_slc_mode
.L381:
	ldr	r3, [sp, #20]
	mov	r0, r8
	ldr	r1, [sp, #16]
	add	r4, r4, sl
	add	r3, r5, r3, asl #2
	ldr	r2, [r3, #2740]
	adds	r2, r2, #0
	movne	r2, #1
	bl	FlashWaitReadyEN
	mov	r0, r8
	ldr	r1, [sp, #16]
	mov	r2, sl
	bl	FlashEraseCmd
	mov	r0, r8
	bl	NandcFlashDeCs
.L377:
	add	r4, r4, #1
.L374:
	cmp	r4, r7
	bcc	.L382
	ldr	r5, .L388
	mov	r4, #0
	ldr	r6, .L388+4
	ldr	r0, [r5, #2164]
	bl	NandcIqrWaitFlashReady
	b	.L383
.L385:
	uxtb	r0, r4
	bl	FlashWaitCmdDone
	ldr	r3, [sp, #12]
	cmp	r3, #1
	bne	.L384
	ldrb	r3, [r5, #80]	@ zero_extendqisi2
	cmp	r3, #0
	beq	.L384
	ldrb	r0, [r6, r4, asl #4]	@ zero_extendqisi2
	bl	flash_exit_slc_mode
.L384:
	add	r4, r4, #1
.L383:
	ldrb	r3, [r5, #3766]	@ zero_extendqisi2
	cmp	r4, r3
	bcc	.L385
	mov	r0, #0
.L375:
	add	sp, sp, #28
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, sl, fp, pc}
.L389:
	.align	2
.L388:
	.word	.LANCHOR0
	.word	.LANCHOR0+2780
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
	ldr	r3, .L392
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
.L393:
	.align	2
.L392:
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
.L395:
	ldr	r3, [r0, #8]
	str	r3, [sp, #4]
	ldr	r3, [sp, #4]
	tst	r3, #1048576
	beq	.L395
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
	ldr	r2, .L410
	mov	ip, #16
	mov	r6, r1
	mov	r4, #0
	add	r0, r2, r0, asl #3
	.pad #20
	sub	sp, sp, #20
	ldr	r5, [r0, #16]
	ldrb	r0, [r0, #20]	@ zero_extendqisi2
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
	ldr	r3, [r2, #12]
	cmp	r3, #3
	bls	.L398
	ldr	r3, [r5, #16]
	cmp	r9, #0
	cmpeq	r1, #0
	str	r3, [sp, #12]
	ldr	r3, [sp, #12]
	bfc	r3, #2, #1
	str	r3, [sp, #12]
	beq	.L399
	cmp	r6, #0
	beq	.L400
	ldr	r3, [r2, #3832]
	mov	ip, r1
	cmp	r3, #24
	movhi	r3, #128
	movls	r3, #64
	str	r3, [sp, #0]
	mov	r3, r8, lsr r0
	str	r3, [sp, #4]
	mov	r3, #0
	mov	r0, r3
	b	.L402
.L405:
	cmp	r1, #0
	mov	lr, r3, lsr #2
	add	r0, r0, #1
	ldrneh	fp, [ip, #2]
	mvneq	fp, #0
	ldrneh	sl, [ip], #4
	ldreq	sl, [r2, #3800]
	orrne	sl, sl, fp, asl #16
	ldrne	fp, [r2, #3800]
	streq	fp, [sl, lr, asl #2]
	strne	sl, [fp, lr, asl #2]
	ldr	lr, [sp, #0]
	add	r3, r3, lr
.L402:
	ldr	sl, [sp, #4]
	cmp	r0, sl
	bcc	.L405
.L400:
	mov	r0, r5
	add	r8, r8, #1
	bl	rk_nandc_xfer_irq_flag_init
	mov	r0, r5
	mov	r1, #0
	mov	r8, r8, asr #1
	bl	NandCIrqEnable
	bfi	r4, r8, #22, #6
	ldr	r8, .L410
	cmp	r9, #0
	ubfx	sl, r4, #22, #5
	mov	r2, r6
	movne	r0, r9
	ldreq	r0, [r8, #3796]
	mov	r1, sl, asl #10
	ldr	r3, [r8, #3800]
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
.L399:
	ldr	r3, [sp, #12]
	str	r3, [r5, #16]
.L398:
	str	r7, [r5, #12]
	str	r4, [r5, #8]
	orr	r4, r4, #4
	str	r4, [r5, #8]
	add	sp, sp, #20
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, sl, fp, pc}
.L411:
	.align	2
.L410:
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
	bne	.L413
	cmp	r3, #0
	beq	.L414
	mov	r0, r2
	mov	r1, r3
	mov	r2, #1024
	bl	memcpy
.L414:
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
.L413:
	cmp	r3, #0
	beq	.L416
	mov	r1, r2
	mov	r0, r3
	mov	r2, #1024
	bl	memcpy
.L416:
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
	b	.L418
.L419:
	add	r3, r3, #1
	mov	r2, r2, asl #1
	uxth	r3, r3
.L418:
	cmp	r2, r0
	bls	.L419
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
	ldr	r3, .L424
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
.L425:
	.align	2
.L424:
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
	ldr	r3, .L440
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
	ldr	r2, .L440+4
.L427:
	strb	r3, [r3, r2]
	add	r3, r3, #1
	cmp	r3, #32
	bne	.L427
	ldrh	r2, [r4, #14]
	ldrh	r3, [r4, #20]
	cmp	r3, r2, lsr #8
	bcs	.L428
	ldr	r2, .L440+4
	sub	ip, r1, #1
	uxtb	r7, r0
	mov	r3, #0
	rsb	sl, r0, r2
	str	sl, [sp, #8]
	mla	ip, r0, ip, r2
	mov	r8, r7, asl #1
	uxtb	r8, r8
	str	ip, [sp, #12]
	b	.L429
.L430:
	strb	ip, [r9, r2]
	add	fp, ip, r7
	add	ip, ip, r8
	add	r5, r5, #1
	strb	fp, [sl, r2]
	uxtb	ip, ip
.L432:
	cmp	r5, r1
	add	r2, r2, r0
	bcc	.L430
	add	r3, r3, #1
.L429:
	cmp	r3, r0
	bcs	.L431
	ldr	fp, [sp, #8]
	mov	r2, #0
	uxtb	ip, r3
	mov	r5, r2
	add	r9, fp, r3
	ldr	fp, [sp, #12]
	add	sl, fp, r3
	b	.L432
.L431:
	ldr	r3, .L440
	movw	r2, #3866
	mov	r1, r1, asl #1
	mov	r6, r6, lsr #1
	strh	r1, [r3, r2]	@ movhi
	movw	r2, #3854
	strh	r6, [r3, r2]	@ movhi
.L428:
	ldr	r3, .L440
	movw	r2, #3902
	cmp	lr, #1
	mov	r1, #5
	mov	ip, #0
	ldr	sl, .L440
	ldrb	r8, [r3, #0]	@ zero_extendqisi2
	strh	r1, [r3, r2]	@ movhi
	mov	r1, #3904
	streqh	lr, [r3, r2]	@ movhi
	cmp	r8, #0
	movw	r2, #3906
	strh	ip, [r3, r1]	@ movhi
	mov	r1, #4352
	strh	r1, [r3, r2]	@ movhi
	ldrne	r3, .L440
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
	ldr	r4, .L440
	ldrls	r2, .L440
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
	ldreq	r3, .L440
	uxtah	r6, r6, r0
	str	r6, [r4, #3840]
	ldr	r4, .L440
	addeq	r6, r6, #4
	streq	r6, [r3, #3840]
	ldr	r0, [r4, #3840]
	uxth	r0, r0
	bl	FtlSysBlkNumInit
	ldr	r3, [r4, #3840]
	ldr	r2, [r4, #3856]
	mov	r0, #2048
	str	r3, [r4, #3944]
	movw	r3, #3912
	ldrh	r3, [r4, r3]
	mov	r2, r2, asl #2
	mul	r2, r3, r2
	mov	r3, #3920
	ldrh	r3, [r4, r3]
	add	r3, r3, #9
	mov	r2, r2, lsr r3
	movw	r3, #3948
	add	r2, r2, #2
	strh	r2, [r4, r3]	@ movhi
	movw	r3, #3918
	ldrh	r1, [r4, r3]
	bl	__aeabi_idiv
	movw	r3, #3950
	strh	r0, [r4, r3]	@ movhi
	mov	r0, #0
	str	r0, [r4, #3952]
	add	sp, sp, #20
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, sl, fp, pc}
.L441:
	.align	2
.L440:
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
	ldr	r3, .L449
	movw	r2, #3956
	ldrh	r2, [r3, r2]
	cmp	r2, #0
	beq	.L447
	movw	r2, #3932
	ldrh	r1, [r3, r2]
	ldr	r2, [r3, #3960]
	mov	r3, #0
	b	.L444
.L446:
	ldrh	ip, [r2], #2
	cmp	ip, r0
	beq	.L448
	add	r3, r3, #1
	uxth	r3, r3
.L444:
	cmp	r3, r1
	bne	.L446
	mov	r0, #0
	bx	lr
.L448:
	mov	r0, #1
	bx	lr
.L447:
	mov	r0, r2
	bx	lr
.L450:
	.align	2
.L449:
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
	ldr	r2, .L459
	stmfd	sp!, {r4, lr}
	.save {r4, lr}
	ldr	r3, [r2, #3964]
	cmp	r3, #0
	beq	.L456
	ldr	ip, [r2, #3968]
	mov	r2, #0
.L455:
	ldr	r4, [ip, #16]
	cmp	r4, r0
	bcc	.L453
	cmp	r4, r1
	bls	.L457
.L453:
	add	r2, r2, #1
	add	ip, ip, #36
	cmp	r2, r3
	bne	.L455
	mov	r0, #0
	ldmfd	sp!, {r4, pc}
.L457:
	mov	r0, #1
	ldmfd	sp!, {r4, pc}
.L456:
	mov	r0, r3
	ldmfd	sp!, {r4, pc}
.L460:
	.align	2
.L459:
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
	ldr	r3, .L462
	ldr	r0, [r3, #3952]
	bx	lr
.L463:
	.align	2
.L462:
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
	ldr	r3, .L465
	ldr	r0, [r3, #3952]
	bx	lr
.L466:
	.align	2
.L465:
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
	ldr	r3, .L468
	ldr	r0, [r3, #3972]
	bx	lr
.L469:
	.align	2
.L468:
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
	ldr	r3, .L471
	movw	r2, #3844
	ldrh	r2, [r3, r2]
	ldr	r0, [r3, #3976]
	mul	r0, r0, r2
	bx	lr
.L472:
	.align	2
.L471:
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
	ldr	r3, .L474
	movw	r1, #3844
	movw	r2, #3854
	ldrh	r2, [r3, r2]
	ldrh	r0, [r3, r1]
	mul	r0, r0, r2
	bx	lr
.L475:
	.align	2
.L474:
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
	movw	r3, #3908
	ldr	r4, .L477
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
	ldr	r0, .L477+4
	bl	printk
	movw	r3, #3986
	ldrh	r2, [r4, r3]
	mov	r0, #0
	add	r2, r2, r6
	strh	r2, [r4, r3]	@ movhi
	ldmfd	sp!, {r2, r3, r4, r5, r6, pc}
.L478:
	.align	2
.L477:
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
	ldr	r5, .L480
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
.L481:
	.align	2
.L480:
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
	ldr	r6, .L487
	movw	r8, #3908
	movw	r7, #3866
	b	.L484
.L486:
	mov	r0, r5
	add	r5, r5, #1
	bl	FtlBbmIsBadBlock
	uxth	r5, r5
	cmp	r0, #0
	addne	r4, r4, #1
	uxthne	r4, r4
.L484:
	ldrh	r3, [r6, r8]
	ldrh	r2, [r6, r7]
	mul	r3, r2, r3
	cmp	r5, r3
	blt	.L486
	mov	r0, r4
	ldmfd	sp!, {r4, r5, r6, r7, r8, pc}
.L488:
	.align	2
.L487:
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
	ldr	r5, .L490
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
.L491:
	.align	2
.L490:
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
	ldr	r3, .L493
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
.L494:
	.align	2
.L493:
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
	ldr	r4, .L496
	ldrh	r1, [r4, r3]
	bl	__aeabi_uidivmod
	movw	r3, #3868
	uxth	r0, r1
	ldrh	r1, [r4, r3]
	bl	__aeabi_uidiv
	uxth	r0, r0
	ldmfd	sp!, {r4, pc}
.L497:
	.align	2
.L496:
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
	bls	.L499
	rsb	r0, r1, r0
	cmp	r0, #-2147483648
	movhi	r0, #0
	movls	r0, #1
	bx	lr
.L499:
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
	ldr	r2, .L502
	movw	r3, #4046
	ldrh	r0, [r2, r3]
	rsbs	r0, r0, #1
	movcc	r0, #0
	bx	lr
.L503:
	.align	2
.L502:
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
	ldr	r2, .L505
	movw	r3, #4046
	ldrh	r0, [r2, r3]
	sub	r2, r0, #1024
	rsbs	r0, r2, #0
	adc	r0, r0, r2
	bx	lr
.L506:
	.align	2
.L505:
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
	ldr	r2, .L510
	movw	r3, #4046
	stmfd	sp!, {r4, r5, r6, lr}
	.save {r4, r5, r6, lr}
	mov	r5, r0
	ldrh	r3, [r2, r3]
	cmp	r3, #1024
	ldmeqfd	sp!, {r4, r5, r6, pc}
	cmp	r1, #0
	beq	.L509
	bl	P2V_block_in_plane
	ldr	r4, .L510+4
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
.L509:
	ldr	r3, .L510
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
.L511:
	.align	2
.L510:
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
	ldr	r5, .L522
	movw	r8, #4046
	movw	r7, #4042
	ldr	r6, .L522+4
	b	.L513
.L514:
	ldrh	r3, [r5, r7]
	add	r3, r4, r3
	add	r3, r5, r3, asl #1
	add	r3, r3, #4048
	ldrh	r0, [r3, #0]
	bl	P2V_block_in_plane
	ldr	r2, [r6, #-2084]
	ldr	r3, [r6, #-2076]
	mov	r0, r0, asl #1
	ldrh	r2, [r2, r0]
	str	r2, [r3, r4, asl #2]
	add	r4, r4, #1
	uxth	r4, r4
.L513:
	ldrh	r3, [r5, r8]
	cmp	r3, r4
	bhi	.L514
	mov	r3, #0
	ldr	ip, .L522
	movw	r8, #4046
	ldr	r4, .L522+4
	movw	r7, #4042
	b	.L515
.L520:
	add	r6, r3, #1
	ldr	r1, [r4, #-2076]
	mov	r2, r3
	uxth	r6, r6
	mov	r0, r6
	b	.L516
.L518:
	ldr	r9, [r1, r2, asl #2]
	ldr	sl, [r1, r0, asl #2]
	cmp	r9, sl
	movhi	r2, r0
	add	r0, r0, #1
	uxth	r0, r0
.L516:
	cmp	r0, r5
	bcc	.L518
	cmp	r3, r2
	beq	.L519
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
	add	r2, r2, #4048
	add	r3, r3, #4048
	ldrh	r0, [r2, #0]
	ldrh	r1, [r3, #0]
	strh	r1, [r2, #0]	@ movhi
	strh	r0, [r3, #0]	@ movhi
.L519:
	mov	r3, r6
.L515:
	ldrh	r5, [ip, r8]
	sub	r2, r5, #1
	cmp	r3, r2
	blt	.L520
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, sl, pc}
.L523:
	.align	2
.L522:
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
	ldr	r2, .L530
	movw	r3, #4046
	stmfd	sp!, {r4, lr}
	.save {r4, lr}
	ldrh	r1, [r2, r3]
	cmp	r1, #1024
	beq	.L528
	movw	r3, #4042
	ldrh	ip, [r2, r3]
	mov	r3, #0
	b	.L526
.L527:
	add	r4, r3, ip
	mov	r4, r4, asl #22
	add	r4, r2, r4, lsr #21
	add	r4, r4, #4048
	ldrh	r4, [r4, #0]
	cmp	r4, r0
	beq	.L529
	add	r3, r3, #1
.L526:
	cmp	r3, r1
	bcc	.L527
.L528:
	mov	r0, #0
	ldmfd	sp!, {r4, pc}
.L529:
	mov	r0, #1
	ldmfd	sp!, {r4, pc}
.L531:
	.align	2
.L530:
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
	ldr	r3, .L535
	stmfd	sp!, {r4, r5, lr}
	.save {r4, r5, lr}
	movw	r4, #4046
	ldrh	r2, [r3, r4]
	cmp	r2, #0
	beq	.L534
	movw	r1, #4042
	movw	r0, #4040
	ldrh	ip, [r3, r1]
	sub	r2, r2, #1
	add	r5, ip, #4
	add	ip, ip, #1
	bic	ip, ip, #64512
	add	r5, r3, r5, asl #1
	ldrh	r0, [r5, r0]
	strh	r2, [r3, r4]	@ movhi
	strh	ip, [r3, r1]	@ movhi
	ldmfd	sp!, {r4, r5, pc}
.L534:
	mov	r0, r2
	ldmfd	sp!, {r4, r5, pc}
.L536:
	.align	2
.L535:
	.word	.LANCHOR0
	.fnend
	.size	FtlFreeSysBlkQueueOut, .-FtlFreeSysBlkQueueOut
	.align	2
	.global	insert_data_list
	.type	insert_data_list, %function
insert_data_list:
	.fnstart
	@ args = 0, pretend = 0, frame = 8
	@ frame_needed = 0, uses_anonymous_args = 0
	movw	r3, #65535
	cmp	r0, r3
	stmfd	sp!, {r0, r1, r2, r4, r5, r6, r7, r8, r9, sl, fp, lr}
	.save {r0, r1, r2, r4, r5, r6, r7, r8, r9, sl, fp, lr}
	beq	.L538
	ldr	r1, .L552
	mov	ip, #6
	mul	ip, ip, r0
	mvn	r6, #0
	ldr	r4, [r1, #-2072]
	mov	r8, r1
	add	r2, r4, ip
	strh	r6, [r2, #2]	@ movhi
	strh	r6, [r4, ip]	@ movhi
	ldr	r3, [r1, #-2068]
	cmp	r3, #0
	beq	.L551
.L539:
	ldr	r7, [r1, #-2064]
	mov	sl, r0, asl #1
	ldrh	r1, [r2, #4]
	ldr	r9, .L552+4
	ldrh	r5, [r7, sl]
	cmp	r1, #0
	ldr	fp, [r8, #-2084]
	mulne	r6, r1, r5
	ldr	r5, [r8, #-2072]
	add	sl, fp, sl
	str	sl, [sp, #4]
	rsb	r1, r5, r3
	mov	r1, r1, asr #1
	mul	r1, r9, r1
	uxth	r1, r1
.L546:
	mov	r9, r1, asl #1
	ldrh	r8, [r3, #4]
	ldrh	sl, [r7, r9]
	cmp	r8, #0
	mvneq	r8, #0
	mulne	r8, r8, sl
	cmp	r8, r6
	bne	.L542
	ldrh	sl, [fp, r9]
	ldr	r9, [sp, #4]
	ldrh	r8, [r9, #0]
	cmp	sl, r8
	bcc	.L544
	b	.L543
.L542:
	bhi	.L543
.L544:
	ldrh	r8, [r3, #0]
	movw	sl, #65535
	cmp	r8, sl
	streqh	r1, [r2, #2]	@ movhi
	streqh	r0, [r3, #0]	@ movhi
	ldreq	r3, .L552
	streq	r2, [r3, #-2060]
	beq	.L538
.L545:
	mov	r1, #6
	mla	r3, r1, r8, r5
	mov	r1, r8
	b	.L546
.L543:
	strh	r1, [r4, ip]	@ movhi
	ldrh	r1, [r3, #2]
	strh	r1, [r2, #2]	@ movhi
	ldr	r1, .L552
	ldr	ip, [r1, #-2068]
	cmp	r3, ip
	bne	.L547
	strh	r0, [r3, #2]	@ movhi
.L551:
	str	r2, [r1, #-2068]
	b	.L538
.L547:
	ldrh	r2, [r3, #2]
	mov	ip, #6
	ldr	r1, [r1, #-2072]
	mul	r2, ip, r2
	strh	r0, [r1, r2]	@ movhi
	strh	r0, [r3, #2]	@ movhi
.L538:
	mov	r0, #0
	ldmfd	sp!, {r1, r2, r3, r4, r5, r6, r7, r8, r9, sl, fp, pc}
.L553:
	.align	2
.L552:
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
	ldr	r2, .L555
	ldr	r3, .L555+4
	ldrh	r1, [r2, r3]
	add	r1, r1, #1
	strh	r1, [r2, r3]	@ movhi
	ldmfd	sp!, {r3, pc}
.L556:
	.align	2
.L555:
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
	beq	.L558
	ldr	r1, .L565
	mov	r5, #6
	mul	ip, r5, r0
	mvn	r3, #0
	ldr	r4, [r1, #-2072]
	add	r2, r4, ip
	strh	r3, [r2, #2]	@ movhi
	strh	r3, [r4, ip]	@ movhi
	ldr	r3, [r1, #-2052]
	cmp	r3, #0
	beq	.L564
.L559:
	ldr	sl, [r1, #-2072]
	mov	r8, r0, asl #1
	ldr	r7, [r1, #-2084]
	rsb	r1, sl, r3
	ldr	r9, .L565+4
	mov	r1, r1, asr #1
	ldrh	r8, [r7, r8]
	mul	r1, r9, r1
	mov	r9, r5
	uxth	r1, r1
.L562:
	mov	r5, r1, asl #1
	ldrh	r5, [r7, r5]
	cmp	r5, r8
	bcs	.L560
	ldrh	r5, [r3, #0]
	cmp	r5, r6
	streqh	r1, [r2, #2]	@ movhi
	streqh	r0, [r3, #0]	@ movhi
	beq	.L558
.L561:
	mla	r3, r9, r5, sl
	mov	r1, r5
	b	.L562
.L560:
	ldrh	r5, [r3, #2]
	strh	r5, [r2, #2]	@ movhi
	strh	r1, [r4, ip]	@ movhi
	ldr	r1, .L565
	ldr	ip, [r1, #-2052]
	cmp	r3, ip
	bne	.L563
	strh	r0, [r3, #2]	@ movhi
.L564:
	str	r2, [r1, #-2052]
	b	.L558
.L563:
	ldrh	r2, [r3, #2]
	mov	ip, #6
	ldr	r1, [r1, #-2072]
	mul	r2, ip, r2
	strh	r0, [r1, r2]	@ movhi
	strh	r0, [r3, #2]	@ movhi
.L558:
	mov	r0, #0
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, sl, pc}
.L566:
	.align	2
.L565:
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
	ldr	r2, .L568
	ldr	r3, .L568+4
	ldrh	r1, [r2, r3]
	add	r1, r1, #1
	strh	r1, [r2, r3]	@ movhi
	ldmfd	sp!, {r3, pc}
.L569:
	.align	2
.L568:
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
	ldr	ip, .L575
	mov	r2, #6
	mul	r1, r2, r1
	ldr	r3, [ip, #-2072]
	stmfd	sp!, {r4, r5, r6, lr}
	.save {r4, r5, r6, lr}
	add	r4, r3, r1
	ldr	r5, [r0, #0]
	movw	r6, #65535
	cmp	r4, r5
	ldrh	r5, [r3, r1]
	bne	.L571
	cmp	r5, r6
	mlane	r2, r2, r5, r3
	moveq	r2, #0
	streq	r2, [r0, #0]
	strne	r2, [r0, #0]
	mvnne	r0, #0
	strneh	r0, [r2, #2]	@ movhi
	b	.L573
.L571:
	cmp	r5, r6
	ldrh	r0, [r4, #2]
	mlane	r5, r2, r5, r3
	muleq	r2, r2, r0
	mvneq	r0, #0
	strneh	r0, [r5, #2]	@ movhi
	ldrneh	r6, [r4, #2]
	ldrneh	r5, [r3, r1]
	ldrne	r0, [ip, #-2072]
	mulne	r2, r2, r6
	streqh	r0, [r3, r2]	@ movhi
	strneh	r5, [r0, r2]	@ movhi
.L573:
	mvn	r2, #0
	mov	r0, #0
	strh	r2, [r3, r1]	@ movhi
	strh	r2, [r4, #2]	@ movhi
	ldmfd	sp!, {r4, r5, r6, pc}
.L576:
	.align	2
.L575:
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
	beq	.L582
	ldr	r2, .L583
	movw	lr, #65535
	mov	ip, #6
	ldr	r4, [r2, #-2072]
	b	.L579
.L581:
	mla	r3, ip, r2, r4
	sub	r1, r1, #1
	uxth	r1, r1
.L579:
	cmp	r1, #0
	beq	.L580
	ldrh	r2, [r3, #0]
	cmp	r2, lr
	bne	.L581
.L580:
	rsb	r4, r4, r3
	ldr	r3, .L583+4
	mov	r4, r4, asr #1
	mul	r4, r3, r4
	uxth	r4, r4
	mov	r1, r4
	bl	List_remove_node
	mov	r0, r4
	ldmfd	sp!, {r4, pc}
.L582:
	movw	r0, #65535
	ldmfd	sp!, {r4, pc}
.L584:
	.align	2
.L583:
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
	ldr	r2, .L592
	ldr	r3, [r2, #-2068]
	cmp	r3, #0
	beq	.L591
	ldr	r2, [r2, #-2072]
	movw	r1, #65535
	mov	ip, #6
	b	.L588
.L590:
	mla	r3, ip, r3, r2
	sub	r0, r0, #1
	uxth	r0, r0
.L588:
	cmp	r0, #0
	beq	.L589
	ldrh	r3, [r3, #0]
	cmp	r3, r1
	bne	.L590
	mov	r0, r1
	bx	lr
.L589:
	rsb	r3, r2, r3
	ldr	r0, .L592+4
	mov	r3, r3, asr #1
	mul	r0, r0, r3
	uxth	r0, r0
	bx	lr
.L591:
	movw	r0, #65535
	bx	lr
.L593:
	.align	2
.L592:
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
	stmfd	sp!, {r4, r5, r6, lr}
	.save {r4, r5, r6, lr}
	mov	r5, r0
	ldr	r4, .L600
	ldr	r3, .L600+4
	ldrh	r3, [r4, r3]
	cmp	r3, r0
	beq	.L595
	ldr	r3, .L600+8
	ldrh	r3, [r4, r3]
	cmp	r3, r0
	beq	.L595
	ldr	r3, .L600+12
	ldrh	r3, [r4, r3]
	cmp	r3, r0
	beq	.L595
	ldr	r3, [r4, #-2072]
	mov	ip, #6
	ldr	r2, [r4, #-2068]
	mla	r1, ip, r0, r3
	cmp	r1, r2
	beq	.L595
	ldrh	r6, [r1, #4]
	mov	r2, r5, asl #1
	ldrh	r1, [r1, #2]
	ldr	r0, [r4, #-2064]
	mla	r1, ip, r1, r3
	ldr	ip, .L600+16
	ldrh	r2, [r0, r2]
	muls	r2, r6, r2
	mvneq	r2, #0
	rsb	r3, r3, r1
	mov	r3, r3, asr #1
	ldrh	r1, [r1, #4]
	mul	r3, ip, r3
	uxth	r3, r3
	mov	r3, r3, asl #1
	ldrh	r3, [r0, r3]
	muls	r3, r1, r3
	mvneq	r3, #0
	cmp	r2, r3
	bcs	.L595
	ldr	r0, .L600+20
	mov	r1, r5
	bl	List_remove_node
	ldr	r3, .L600+24
	mov	r0, r5
	ldrh	r2, [r4, r3]
	sub	r2, r2, #1
	strh	r2, [r4, r3]	@ movhi
	bl	INSERT_DATA_LIST
.L595:
	mov	r0, #0
	ldmfd	sp!, {r4, r5, r6, pc}
.L601:
	.align	2
.L600:
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
	b	.L603
.L606:
	mov	r7, r3
	add	r3, r3, #2
	ldrh	r6, [r7, #0]
	cmp	r6, #0
	bne	.L604
	bl	FtlFreeSysBlkQueueOut
	cmp	r0, #0
	strh	r0, [r7, #0]	@ movhi
	beq	.L605
	ldr	r3, [r4, #28]
	strh	r6, [r4, #2]	@ movhi
	add	r3, r3, #1
	str	r3, [r4, #28]
	ldrh	r3, [r4, #8]
	strh	r5, [r4, #0]	@ movhi
	add	r3, r3, #1
	strh	r3, [r4, #8]	@ movhi
	b	.L605
.L604:
	add	r5, r5, #1
	uxth	r5, r5
.L603:
	cmp	r5, r2
	bne	.L606
.L605:
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
	ldr	r2, .L619
	movw	r3, #3950
	stmfd	sp!, {r4, r5, r6, r7, lr}
	.save {r4, r5, r6, r7, lr}
	movw	r1, #65535
	ldrh	r3, [r2, r3]
	ldr	r2, .L619+4
	ldr	ip, [r2, #-1900]
	mov	r2, #0
	mov	r0, r2
	b	.L608
.L610:
	add	r2, r2, #12
	add	r4, ip, r2
	ldrh	r4, [r4, #-12]
	cmp	r4, r1
	ldmeqfd	sp!, {r4, r5, r6, r7, pc}
	add	r0, r0, #1
	uxth	r0, r0
.L608:
	cmp	r0, r3
	bne	.L610
	mov	r1, #0
	mov	r0, r3
	mov	r5, #-2147483648
	mov	r2, r1
	b	.L611
.L613:
	add	r4, ip, r1
	ldr	r4, [r4, #4]
	cmp	r4, #0
	blt	.L612
	cmp	r4, r5
	movcc	r5, r4
	movcc	r0, r2
.L612:
	add	r2, r2, #1
	add	r1, r1, #12
	uxth	r2, r2
.L611:
	cmp	r2, r3
	bne	.L613
	cmp	r0, r3
	ldmccfd	sp!, {r4, r5, r6, r7, pc}
	ldr	r2, .L619+8
	mov	r0, r3
	ldr	r1, .L619+4
	mvn	r4, #0
	ldrh	r6, [r1, r2]
	mov	r2, #0
	mov	r1, r2
	b	.L614
.L616:
	add	r5, ip, r2
	ldr	r5, [r5, #4]
	cmp	r5, r4
	bcs	.L615
	ldrh	r7, [ip, r2]
	cmp	r7, r6
	movne	r4, r5
	movne	r0, r1
.L615:
	add	r1, r1, #1
	add	r2, r2, #12
	uxth	r1, r1
.L614:
	cmp	r1, r3
	bne	.L616
	ldmfd	sp!, {r4, r5, r6, r7, pc}
.L620:
	.align	2
.L619:
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
	ldr	r3, .L627
	ldr	r2, .L627+4
	stmfd	sp!, {r4, r5, lr}
	.save {r4, r5, lr}
	ldrh	r1, [r2, r3]
	cmp	r1, #4
	add	ip, r1, #1
	mov	r1, r3
	strh	ip, [r2, r3]	@ movhi
	bhi	.L622
	cmp	r0, #0
	ldmeqfd	sp!, {r4, r5, pc}
.L622:
	ldr	r0, .L627+8
	mov	r3, #0
	strh	r3, [r2, r1]	@ movhi
	movw	r1, #3852
	str	r3, [r2, #-1892]
	movw	ip, #65535
	ldrh	r4, [r0, r1]
	ldr	r1, [r2, #-2064]
	ldr	r2, .L627+4
	b	.L624
.L626:
	ldrh	r0, [r1], #2
	add	r3, r3, #1
	cmp	r0, ip
	uxth	r3, r3
	ldrne	r5, [r2, #-1892]
	addne	r0, r0, r5
	strne	r0, [r2, #-1892]
.L624:
	cmp	r3, r4
	bne	.L626
	ldmfd	sp!, {r4, r5, pc}
.L628:
	.align	2
.L627:
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
	ldr	r3, .L633
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
.L634:
	.align	2
.L633:
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
	ldr	r3, .L636
	mov	r2, r0, lsr #5
	and	r0, r0, #31
	ldr	r3, [r3, #-1888]
	ldr	r3, [r3, r2, asl #2]
	mov	r0, r3, lsr r0
	and	r0, r0, #1
	bx	lr
.L637:
	.align	2
.L636:
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
	ldr	ip, .L645
	mov	r3, #0
	strh	r3, [r0, #4]	@ movhi
	movw	r3, #3844
	stmfd	sp!, {r4, r5, r6, lr}
	.save {r4, r5, r6, lr}
	ldrh	r3, [ip, r3]
	movw	ip, #65535
	b	.L639
.L641:
	add	r4, r0, r2, asl #1
	add	r2, r2, #1
	ldrh	r4, [r4, #16]
	uxth	r2, r2
	cmp	r4, ip
	ldrneh	r4, [r0, #4]
	addne	r4, r4, #1
	strneh	r4, [r0, #4]	@ movhi
.L639:
	cmp	r2, r3
	bcc	.L641
	ldr	ip, .L645
	movw	r2, #3912
	movw	r4, #65535
	mvn	r1, r1
	ldrh	r5, [ip, r2]
	mov	ip, r0
	mov	r2, #0
	b	.L642
.L644:
	ldrh	r6, [ip, #16]
	add	r2, r2, #1
	add	ip, ip, #2
	cmp	r6, r4
	uxth	r2, r2
	ldrneh	r6, [r0, #4]
	addne	r6, r5, r6
	addne	r6, r6, r1
	strneh	r6, [r0, #4]	@ movhi
.L642:
	cmp	r2, r3
	bne	.L644
	ldmfd	sp!, {r4, r5, r6, pc}
.L646:
	.align	2
.L645:
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
	ldr	r6, .L652
	strh	r5, [r0, #4]	@ movhi
	movw	r8, #3844
	strb	r5, [r4, #7]
	mvn	r7, #0
	b	.L648
.L650:
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
.L648:
	ldrh	r3, [r6, r8]
	cmp	r3, r5
	bhi	.L650
	ldr	r1, .L652
	movw	r2, #3912
	ldrb	r3, [r4, #7]	@ zero_extendqisi2
	ldrh	r2, [r1, r2]
	mul	r3, r2, r3
	strh	r3, [r4, #4]	@ movhi
	mov	r3, #0
	strb	r3, [r4, #9]
	ldr	r3, .L652+4
	ldr	r2, [r3, #-1884]
	cmp	r2, #0
	beq	.L651
	ldrh	r1, [r4, #0]
	ldr	r2, [r3, #-2084]
	mov	r3, r1, asl #1
	ldrh	r3, [r2, r3]
	cmp	r3, #59
	movls	r3, #1
	strlsb	r3, [r4, #9]
.L651:
	mov	r0, #0
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, sl, pc}
.L653:
	.align	2
.L652:
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
	ldr	r6, .L659
	movw	sl, #3844
	movw	r8, #3912
	b	.L655
.L657:
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
.L655:
	ldrh	r3, [r6, sl]
	cmp	r3, r5
	bhi	.L657
	cmp	r4, #0
	beq	.L658
	mov	r1, r4
	mov	r0, #32768
	bl	__aeabi_idiv
	uxth	r4, r0
.L658:
	ldr	r3, .L659+4
	mov	r2, #6
	mov	r0, #0
	ldr	r3, [r3, #-2072]
	mla	r7, r2, r7, r3
	strh	r4, [r7, #4]	@ movhi
	ldmfd	sp!, {r3, r4, r5, r6, r7, r8, sl, pc}
.L660:
	.align	2
.L659:
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
	ldr	r3, .L664
	ldr	r0, [r3, #-2052]
	cmp	r0, #0
	bxeq	lr
	ldr	r2, [r3, #-2072]
	rsb	r0, r2, r0
	ldr	r2, .L664+4
	mov	r0, r0, asr #1
	mul	r0, r2, r0
	ldr	r2, [r3, #-2084]
	uxth	r0, r0
	mov	r3, r0, asl #1
	ldrh	r0, [r2, r3]
	bx	lr
.L665:
	.align	2
.L664:
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
	ldr	r2, .L673
	stmfd	sp!, {r4, r5, lr}
	.save {r4, r5, lr}
	ldr	r3, [r2, #-2052]
	cmp	r3, #0
	beq	.L672
	ldr	r1, .L673+4
	mov	ip, #7
	mov	r5, #6
	movw	r4, #65535
	ldrh	r1, [r2, r1]
	mul	r1, ip, r1
	mov	r1, r1, asr #3
	cmp	r0, r1
	uxthgt	r0, r1
	ldr	r1, [r2, #-2072]
	ldr	r2, .L673+8
	rsb	r3, r1, r3
	mov	r3, r3, asr #1
	mul	r3, r2, r3
	mov	r2, #0
	uxth	r3, r3
	b	.L669
.L671:
	mul	ip, r5, r3
	ldrh	ip, [r1, ip]
	cmp	ip, r4
	beq	.L670
	add	r2, r2, #1
	mov	r3, ip
	uxth	r2, r2
.L669:
	cmp	r2, r0
	bne	.L671
.L670:
	ldr	r2, .L673
	mov	r3, r3, asl #1
	ldr	r2, [r2, #-2084]
	ldrh	r0, [r2, r3]
	ldmfd	sp!, {r4, r5, pc}
.L672:
	mov	r0, r3
	ldmfd	sp!, {r4, r5, pc}
.L674:
	.align	2
.L673:
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
	ldr	r6, .L685
	add	r5, r8, #12
	.pad #32
	sub	sp, sp, #32
	ldr	r1, .L685+4
	bl	strcpy
	mov	r0, r5
	ldr	r1, .L685+8
	ldr	r2, [r6, #2200]
	bl	sprintf
	ldr	r1, .L685+12
	ldr	r2, [r6, #3928]
	add	r5, r5, r0
	mov	r0, r5
	bl	sprintf
	ldr	r3, .L685+16
	ldr	r3, [r3, #2960]
	cmp	r3, #1
	add	r5, r5, r0
	bne	.L681
.L676:
	add	r0, sp, #16
	add	r1, sp, #20
	add	r2, sp, #24
	add	r3, sp, #28
	bl	NandcGetTimeCfg
	ldr	r3, [sp, #24]
	ldr	r2, [sp, #16]
	mov	r0, r5
	ldr	r1, .L685+20
	str	r3, [sp, #0]
	ldr	r3, [sp, #28]
	ldr	r4, .L685+24
	ldr	r7, .L685+28
	str	r3, [sp, #4]
	ldr	r3, [sp, #20]
	bl	sprintf
	ldr	r1, .L685+32
	add	r5, r5, r0
	mov	r0, r5
	add	r5, r5, #10
	bl	strcpy
	ldr	r2, [r6, #3972]
	mov	r0, r5
	ldr	r1, .L685+36
	bl	sprintf
	ldr	r1, .L685+40
	ldr	r2, [r4, #-1892]
	add	r5, r5, r0
	mov	r0, r5
	bl	sprintf
	ldr	r1, .L685+44
	ldr	r2, [r4, #-1880]
	add	r5, r5, r0
	mov	r0, r5
	bl	sprintf
	ldr	r1, .L685+48
	ldr	r2, [r4, #-1876]
	add	r5, r5, r0
	mov	r0, r5
	bl	sprintf
	ldr	r1, .L685+52
	ldr	r2, [r4, #-1872]
	add	r5, r5, r0
	mov	r0, r5
	bl	sprintf
	ldr	r1, .L685+56
	ldr	r2, [r4, #-1868]
	add	r5, r5, r0
	mov	r0, r5
	bl	sprintf
	ldr	r1, .L685+60
	ldr	r2, [r4, #-1864]
	add	r5, r5, r0
	mov	r0, r5
	bl	sprintf
	ldr	r1, .L685+64
	ldr	r2, [r4, #-1860]
	add	r5, r5, r0
	mov	r0, r5
	bl	sprintf
	ldr	r2, [r4, #-1856]
	ldr	r1, .L685+68
	mov	r2, r2, lsr #11
	add	r5, r5, r0
	mov	r0, r5
	bl	sprintf
	ldr	r2, [r4, #-1852]
	ldr	r1, .L685+72
	mov	r2, r2, lsr #11
	add	r5, r5, r0
	mov	r0, r5
	bl	sprintf
	ldr	r1, .L685+76
	ldr	r2, [r4, #-1848]
	add	r5, r5, r0
	mov	r0, r5
	bl	sprintf
	ldr	r1, .L685+80
	ldr	r2, [r4, #-1844]
	add	r5, r5, r0
	mov	r0, r5
	bl	sprintf
	add	r5, r5, r0
	bl	FtlBbtCalcTotleCnt
	movw	r2, #3986
	ldr	r1, .L685+84
	ldrh	r2, [r6, r2]
	mov	r3, r0
	mov	r0, r5
	bl	sprintf
	ldr	r1, .L685+88
	ldrh	r2, [r4, r7]
	add	r5, r5, r0
	mov	r0, r5
	bl	sprintf
	ldr	r1, .L685+92
	ldr	r2, [r4, #-1840]
	add	r5, r5, r0
	mov	r0, r5
	bl	sprintf
	ldr	r1, .L685+96
	ldr	r2, [r4, #-1836]
	add	r5, r5, r0
	mov	r0, r5
	bl	sprintf
	ldr	r1, .L685+100
	ldr	r2, [r4, #-1832]
	add	r5, r5, r0
	mov	r0, r5
	bl	sprintf
	ldr	r1, .L685+104
	ldr	r2, [r4, #-2080]
	add	r5, r5, r0
	mov	r0, r5
	bl	sprintf
	ldr	r1, .L685+108
	ldr	r2, [r4, #-1828]
	add	r5, r5, r0
	mov	r0, r5
	bl	sprintf
	ldr	r1, .L685+112
	ldr	r2, [r4, #-1824]
	add	r5, r5, r0
	mov	r0, r5
	bl	sprintf
	ldr	r3, .L685+116
	ldr	r1, .L685+120
	ldrh	r2, [r4, r3]
	add	r5, r5, r0
	mov	r0, r5
	bl	sprintf
	ldr	r3, .L685+124
	ldr	r1, .L685+128
	ldrh	r2, [r4, r3]
	add	r5, r5, r0
	mov	r0, r5
	bl	sprintf
	ldr	r2, [r6, #3952]
	ldr	r1, .L685+132
	add	r5, r5, r0
	mov	r0, r5
	bl	sprintf
	ldr	r2, [r6, #3944]
	ldr	r1, .L685+136
	add	r5, r5, r0
	mov	r0, r5
	bl	sprintf
	ldr	r2, [r6, #3840]
	ldr	r1, .L685+140
	add	r5, r5, r0
	mov	r0, r5
	bl	sprintf
	movw	r3, #4046
	ldrh	r2, [r6, r3]
	ldr	r1, .L685+144
	add	r5, r5, r0
	mov	r0, r5
	bl	sprintf
	movw	r3, #3852
	ldrh	r2, [r6, r3]
	ldr	r1, .L685+148
	add	r5, r5, r0
	mov	r0, r5
	bl	sprintf
	ldr	r3, .L685+152
	ldr	r1, .L685+156
	ldrh	r2, [r4, r3]
	add	r5, r5, r0
	mov	r0, r5
	bl	sprintf
	ldr	r2, [r6, #3856]
	ldr	r1, .L685+160
	add	r5, r5, r0
	mov	r0, r5
	bl	sprintf
	ldr	r3, .L685+164
	ldr	r1, .L685+168
	ldrh	r2, [r4, r3]
	add	r5, r5, r0
	mov	r0, r5
	bl	sprintf
	movw	r3, #3980
	ldrh	r2, [r6, r3]
	ldr	r1, .L685+172
	ldr	r6, .L685+176
	add	r5, r5, r0
	mov	r0, r5
	bl	sprintf
	ldr	r3, .L685+180
	ldr	r1, .L685+184
	ldrh	r2, [r4, r3]
	add	r5, r5, r0
	mov	r0, r5
	bl	sprintf
	ldr	r1, .L685+188
	ldrb	r2, [r4, #-2038]	@ zero_extendqisi2
	add	r5, r5, r0
	mov	r0, r5
	bl	sprintf
	ldrh	r2, [r4, r6]
	ldr	r1, .L685+192
	add	r5, r5, r0
	mov	r0, r5
	bl	sprintf
	ldr	r1, .L685+196
	ldrb	r2, [r4, #-2036]	@ zero_extendqisi2
	add	r5, r5, r0
	mov	r0, r5
	bl	sprintf
	ldr	r3, .L685+200
	ldr	r1, .L685+204
	ldrh	r2, [r4, r3]
	add	r5, r5, r0
	mov	r0, r5
	bl	sprintf
	ldrh	r3, [r4, r6]
	ldr	r2, [r4, #-2064]
	add	r6, r6, #48
	ldr	r1, .L685+208
	mov	r3, r3, asl #1
	ldrh	r2, [r2, r3]
	add	r5, r5, r0
	mov	r0, r5
	bl	sprintf
	ldr	r3, .L685+212
	ldr	r1, .L685+216
	ldrh	r2, [r4, r3]
	add	r5, r5, r0
	mov	r0, r5
	bl	sprintf
	ldr	r1, .L685+220
	ldrb	r2, [r4, #-1990]	@ zero_extendqisi2
	add	r5, r5, r0
	mov	r0, r5
	bl	sprintf
	ldrh	r2, [r4, r6]
	ldr	r1, .L685+224
	add	r5, r5, r0
	mov	r0, r5
	bl	sprintf
	ldr	r1, .L685+228
	ldrb	r2, [r4, #-1988]	@ zero_extendqisi2
	add	r5, r5, r0
	mov	r0, r5
	bl	sprintf
	ldr	r3, .L685+232
	ldr	r1, .L685+236
	ldrh	r2, [r4, r3]
	add	r5, r5, r0
	mov	r0, r5
	bl	sprintf
	ldrh	r3, [r4, r6]
	ldr	r2, [r4, #-2064]
	add	r6, r6, #240
	ldr	r1, .L685+240
	mov	r3, r3, asl #1
	ldrh	r2, [r2, r3]
	add	r5, r5, r0
	mov	r0, r5
	bl	sprintf
	ldr	r3, .L685+244
	ldr	r1, .L685+248
	ldrh	r2, [r4, r3]
	add	r5, r5, r0
	mov	r0, r5
	bl	sprintf
	ldr	r1, .L685+252
	ldrb	r2, [r4, #-1942]	@ zero_extendqisi2
	add	r5, r5, r0
	mov	r0, r5
	bl	sprintf
	ldr	r3, .L685+256
	ldr	r1, .L685+260
	ldrh	r2, [r4, r3]
	add	r5, r5, r0
	mov	r0, r5
	bl	sprintf
	ldr	r1, .L685+264
	ldrb	r2, [r4, #-1940]	@ zero_extendqisi2
	add	r5, r5, r0
	mov	r0, r5
	bl	sprintf
	ldr	r3, .L685+268
	ldr	r1, .L685+272
	ldrh	r2, [r4, r3]
	add	r5, r5, r0
	mov	r0, r5
	bl	sprintf
	ldr	r3, .L685+276
	ldr	r1, .L685+280
	ldrh	r2, [r4, r3]
	add	r5, r5, r0
	mov	r0, r5
	bl	sprintf
	ldr	r1, .L685+284
	ldrb	r2, [r4, #-1750]	@ zero_extendqisi2
	add	r5, r5, r0
	mov	r0, r5
	bl	sprintf
	ldr	r1, .L685+288
	ldrh	r2, [r4, r6]
	add	r5, r5, r0
	mov	r0, r5
	bl	sprintf
	ldr	r1, .L685+292
	ldrb	r2, [r4, #-1748]	@ zero_extendqisi2
	add	r5, r5, r0
	mov	r0, r5
	bl	sprintf
	ldr	r3, .L685+296
	ldr	r1, .L685+300
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
	ldr	r1, .L685+304
	ldr	r3, [r4, #-1624]
	add	r5, r5, r0
	mov	r0, r5
	bl	sprintf
	ldr	r1, .L685+308
	ldr	r2, [r4, #-1632]
	add	r5, r5, r0
	mov	r0, r5
	bl	sprintf
	ldr	r1, .L685+312
	ldr	r2, [r4, #-1608]
	add	r5, r5, r0
	mov	r0, r5
	bl	sprintf
	ldr	r3, .L685+316
	ldr	r1, .L685+320
	ldrh	r2, [r4, r3]
	add	r5, r5, r0
	mov	r0, r5
	bl	sprintf
	ldr	r3, .L685+324
	ldr	r1, .L685+328
	ldrh	r2, [r4, r3]
	add	r5, r5, r0
	mov	r0, r5
	bl	sprintf
	ldr	r1, .L685+332
	ldr	r2, [r4, #-1188]
	add	r5, r5, r0
	mov	r0, r5
	bl	sprintf
	ldr	r3, .L685+336
	ldr	r1, .L685+340
	ldrh	r2, [r4, r3]
	add	r5, r5, r0
	mov	r0, r5
	bl	sprintf
	add	r5, r5, r0
	bl	GetFreeBlockMinEraseCount
	ldr	r1, .L685+344
	mov	r2, r0
	mov	r0, r5
	bl	sprintf
	add	r5, r5, r0
	ldrh	r0, [r4, r7]
	bl	GetFreeBlockMaxEraseCount
	ldr	r1, .L685+348
	mov	r2, r0
	mov	r0, r5
	bl	sprintf
	ldrh	r3, [r4, r6]
	movw	r2, #65535
	cmp	r3, r2
	add	r5, r5, r0
	beq	.L678
	ldr	r2, [r4, #-2064]
	mov	r3, r3, asl #1
	mov	r0, r5
	ldr	r1, .L685+352
	ldrh	r2, [r2, r3]
	bl	sprintf
	add	r5, r5, r0
.L678:
	mov	r0, #0
	ldr	r4, .L685+24
	bl	List_get_gc_head_node
	mov	r6, #0
	movw	sl, #65535
	mov	r9, #6
	uxth	r3, r0
.L680:
	cmp	r3, sl
	beq	.L679
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
	ldr	r1, .L685+356
	str	r2, [sp, #8]
	mov	r2, r6
	bl	sprintf
	add	r6, r6, #1
	ldr	r3, [r4, #-2072]
	cmp	r6, #16
	ldrh	r3, [r3, r7]
	add	r5, r5, r0
	bne	.L680
.L679:
	ldr	r6, .L685+24
	mov	r4, #0
	movw	sl, #65535
	mov	r9, #6
	ldr	r2, [r6, #-2052]
	ldr	r3, [r6, #-2072]
	rsb	r3, r3, r2
	ldr	r2, .L685+360
	mov	r3, r3, asr #1
	mul	r3, r2, r3
	uxth	r3, r3
.L682:
	cmp	r3, sl
	beq	.L681
	mul	r7, r9, r3
	ldr	r2, [r6, #-2072]
	mov	r0, r5
	add	r2, r2, r7
	ldrh	r2, [r2, #4]
	str	r2, [sp, #0]
	mov	r2, r3, asl #1
	ldr	r1, [r6, #-2084]
	ldrh	r2, [r1, r2]
	ldr	r1, .L685+364
	str	r2, [sp, #4]
	mov	r2, r4
	bl	sprintf
	add	r4, r4, #1
	ldr	r3, [r6, #-2072]
	cmp	r4, #4
	ldrh	r3, [r3, r7]
	add	r5, r5, r0
	bne	.L682
.L681:
	rsb	r0, r8, r5
	add	sp, sp, #32
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, sl, pc}
.L686:
	.align	2
.L685:
	.word	.LANCHOR0
	.word	.LC4
	.word	.LC5
	.word	.LC6
	.word	.LANCHOR1
	.word	.LC7
	.word	.LANCHOR2
	.word	-2048
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
	.word	-1790
	.word	.LC29
	.word	-1792
	.word	.LC30
	.word	.LC31
	.word	.LC32
	.word	.LC33
	.word	.LC34
	.word	.LC35
	.word	-1772
	.word	.LC36
	.word	.LC37
	.word	-1768
	.word	.LC38
	.word	.LC39
	.word	-2044
	.word	-2042
	.word	.LC40
	.word	.LC41
	.word	.LC42
	.word	.LC43
	.word	-2040
	.word	.LC44
	.word	.LC45
	.word	-1994
	.word	.LC46
	.word	.LC47
	.word	.LC48
	.word	.LC49
	.word	-1992
	.word	.LC50
	.word	.LC51
	.word	-1946
	.word	.LC52
	.word	.LC53
	.word	-1948
	.word	.LC54
	.word	.LC55
	.word	-1944
	.word	.LC56
	.word	-1754
	.word	.LC57
	.word	.LC58
	.word	.LC59
	.word	.LC60
	.word	-1752
	.word	.LC61
	.word	.LC62
	.word	.LC63
	.word	.LC64
	.word	-1192
	.word	.LC65
	.word	-1190
	.word	.LC66
	.word	.LC67
	.word	-1184
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
	ldr	r1, .L688
	ldr	r2, .L688+4
	bl	sprintf
	add	r5, r4, r0
	mov	r0, r5
	bl	FtlPrintInfo2buf
	add	r0, r5, r0
	rsb	r0, r4, r0
	ldmfd	sp!, {r3, r4, r5, pc}
.L689:
	.align	2
.L688:
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
	ldr	r3, .L717
	stmfd	sp!, {r4, r5, r6, r7, r8, r9, sl, fp, lr}
	.save {r4, r5, r6, r7, r8, r9, sl, fp, lr}
	.pad #20
	sub	sp, sp, #20
	ldr	r1, [r3, #-1832]
	ldr	r2, [r3, #-1824]
	cmp	r1, r2
	bcs	.L691
	ldr	r0, .L717+4
	movw	r1, #3852
	mov	r2, #0
	str	r2, [r3, #-1840]
	ldrh	r1, [r0, r1]
	ldr	r0, [r3, #-2084]
	b	.L692
.L693:
	ldrh	lr, [r0], #2
	add	r2, r2, #1
	ldr	ip, [r3, #-1840]
	add	ip, lr, ip
	str	ip, [r3, #-1840]
.L692:
	cmp	r2, r1
	ldr	r4, .L717
	bcc	.L693
	ldr	r5, [r4, #-1840]
	mov	r0, r5
	bl	__aeabi_uidiv
	ldr	r2, .L717+4
	movw	r3, #3902
	ldrh	r1, [r2, r3]
	str	r0, [r4, #-1832]
	ldr	r0, [r4, #-1836]
	rsb	r0, r0, r5
	bl	__aeabi_uidiv
	str	r0, [r4, #-1840]
	b	.L694
.L691:
	ldr	r2, [r3, #-1828]
	cmp	r1, r2
	bls	.L694
	add	r2, r2, #1
	ldr	r4, .L717+4
	str	r2, [r3, #-1828]
	movw	ip, #3852
	mov	r2, #0
	b	.L695
.L696:
	ldr	r0, [r3, #-2084]
	mov	r1, r2, asl #1
	add	r2, r2, #1
	ldrh	r5, [r0, r1]
	add	r5, r5, #1
	strh	r5, [r0, r1]	@ movhi
.L695:
	ldrh	r1, [r4, ip]
	cmp	r2, r1
	bcc	.L696
.L694:
	ldr	r3, .L717
	ldr	r6, [r3, #-1824]
	ldr	r5, [r3, #-1832]
	add	r2, r6, #256
	cmp	r2, r5
	mov	r2, r3
	bls	.L697
	ldr	r1, [r3, #-1828]
	add	r0, r6, #768
	cmp	r0, r1
	bls	.L697
	ldr	r3, [r3, #-1884]
	cmp	r3, #0
	beq	.L715
	cmp	r6, #30
	bhi	.L715
.L697:
	ldr	r3, .L717+8
	ldrh	r0, [r2, r3]
	add	r0, r0, r0, asl #1
	ubfx	r0, r0, #2, #16
	bl	GetFreeBlockMaxEraseCount
	add	r3, r6, #64
	cmp	r0, r3
	mov	r8, r0
	bcs	.L699
	cmp	r6, #30
	bhi	.L715
.L699:
	ldr	r2, .L717
	ldr	r3, [r2, #-2068]
	cmp	r3, #0
	beq	.L715
	movw	r7, #65535
	ldr	r1, [r2, #-2072]
	mov	r4, r7
	ldr	r2, [r2, #-2084]
	mov	sl, r7
	ldr	ip, .L717+12
	mov	fp, #6
	b	.L700
.L703:
	ldrh	r9, [r3, #4]
	cmp	r9, #0
	beq	.L701
	rsb	r3, r1, r3
	mov	r3, r3, asr #1
	mul	r3, ip, r3
	uxth	r3, r3
	mov	r9, r3, asl #1
	ldrh	r9, [r2, r9]
	cmp	r9, r6
	bls	.L712
	cmp	r9, r7
	movcc	r7, r9
	movcc	r4, r3
.L701:
	mla	r3, fp, r0, r1
.L700:
	ldrh	r0, [r3, #0]
	cmp	r0, sl
	bne	.L703
	b	.L702
.L712:
	mov	r4, r3
.L702:
	movw	r3, #65535
	cmp	r4, r3
	beq	.L698
	mov	sl, r4, asl #1
	ldr	fp, .L717
	ldrh	r9, [r2, sl]
	cmp	r9, r6
	bls	.L704
	bl	GetFreeBlockMinEraseCount
	cmp	r0, r6
	strhi	r7, [fp, #-1824]
.L704:
	cmp	r9, #29
	ldr	r2, [fp, #-1884]
	movhi	r3, #0
	movls	r3, #1
	cmp	r2, #0
	moveq	r3, #0
	cmp	r3, #0
	beq	.L705
	add	r3, r9, #10
	cmp	r3, r5
	bls	.L706
	ldr	r2, .L717+4
	movw	r3, #3902
	ldrh	r3, [r2, r3]
	cmp	r3, r9
	bls	.L705
.L706:
	ldr	r6, .L717
	ldr	r3, .L717+16
	ldrh	r3, [r6, r3]
	cmp	r3, #64
	bls	.L705
	ldr	r3, [r6, #-2084]
	mov	r1, r4
	ldr	r0, .L717+20
	ldrh	r2, [r3, sl]
	bl	printk
	b	.L716
.L705:
	cmp	r9, r5
	bcs	.L715
	add	r3, r9, #128
	cmp	r8, r3
	ble	.L715
	add	r3, r9, #256
	ldr	r6, .L717
	cmp	r3, r5
	bcc	.L707
	ldr	r3, [r6, #-1828]
	add	r9, r9, #768
	cmp	r9, r3
	bcs	.L715
.L707:
	ldr	r3, [r6, #-2064]
	mov	r1, r4
	ldr	r0, .L717+24
	mov	r2, r5
	ldrh	r3, [r3, sl]
	str	r3, [sp, #0]
	ldr	r3, [r6, #-2084]
	ldrh	r3, [r3, sl]
	stmib	sp, {r3, r8}
	ldr	r3, [r6, #-1828]
	bl	printk
.L716:
	mov	r3, #1
	str	r3, [r6, #-1180]
	b	.L698
.L715:
	movw	r4, #65535
.L698:
	mov	r0, r4
	add	sp, sp, #20
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, sl, fp, pc}
.L718:
	.align	2
.L717:
	.word	.LANCHOR2
	.word	.LANCHOR0
	.word	-2048
	.word	-1431655765
	.word	-2056
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
	beq	.L720
	ldr	r2, .L721
	mov	r3, r0, asl #1
	mov	r1, #0
	ldr	r2, [r2, #-2064]
	strh	r1, [r2, r3]	@ movhi
	bl	INSERT_FREE_LIST
.L720:
	mov	r0, #0
	ldmfd	sp!, {r3, pc}
.L722:
	.align	2
.L721:
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
	ldr	r2, .L731
	mov	r3, #0
	stmfd	sp!, {r4, r5, r6, r7, r8, r9, sl, fp, lr}
	.save {r4, r5, r6, r7, r8, r9, sl, fp, lr}
	movw	sl, #3844
	str	r3, [r2, #-1176]
	mov	r8, #12
	ldr	r0, .L731+4
	mov	r7, #1
	movw	r6, #3922
	movw	fp, #3924
	b	.L724
.L725:
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
.L724:
	ldrh	r1, [r0, sl]
	cmp	r3, r1
	bcc	.L725
	b	.L730
.L727:
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
	b	.L729
.L730:
	ldr	r3, .L731
	mov	r8, #12
	ldr	ip, .L731+4
	mov	r7, #0
	movw	r6, #3922
	movw	r5, #3924
.L729:
	ldr	r2, [r3, #-1156]
	cmp	r1, r2
	bcc	.L727
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, sl, fp, pc}
.L732:
	.align	2
.L731:
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
	ldr	r3, .L740
	stmfd	sp!, {r4, r5, r6, r7, r8, r9, sl, fp, lr}
	.save {r4, r5, r6, r7, r8, r9, sl, fp, lr}
	mov	r5, #36
	ldr	r6, [r3, #-1156]
	mov	fp, #12
	ldr	ip, [r3, #-1172]
	mov	r3, #0
	mov	r4, r3
	b	.L734
.L737:
	mul	sl, fp, r2
	add	r8, ip, sl
	ldr	r9, [ip, sl]
	ldr	sl, [r7, #8]
	cmp	r9, sl
	streq	r4, [r8, #8]
	beq	.L736
.L735:
	add	r2, r2, #1
	uxth	r2, r2
.L739:
	cmp	r2, r6
	bcc	.L737
.L736:
	add	r3, r3, #1
	uxth	r3, r3
.L734:
	cmp	r3, r1
	ldmcsfd	sp!, {r4, r5, r6, r7, r8, r9, sl, fp, pc}
	mla	r7, r5, r3, r0
	mov	r2, #0
	b	.L739
.L741:
	.align	2
.L740:
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
	ldr	r3, .L749
	stmfd	sp!, {r4, r5, r6, r7, r8, sl, lr}
	.save {r4, r5, r6, r7, r8, sl, lr}
	mov	r6, #12
	ldr	r8, [r3, #-1156]
	mov	r5, #1
	ldr	r7, [r3, #-1172]
	mov	r4, #36
	mov	r3, #0
	b	.L743
.L746:
	mla	ip, r6, r2, r7
	ldr	sl, [ip, #8]
	cmp	sl, #0
	bne	.L744
	mla	r2, r4, r3, r0
	ldr	sl, [ip, #0]
	str	r5, [ip, #8]
	str	sl, [r2, #8]
	ldr	ip, [ip, #4]
	str	ip, [r2, #12]
	b	.L745
.L744:
	add	r2, r2, #1
	uxth	r2, r2
	b	.L747
.L748:
	mov	r2, #0
.L747:
	cmp	r2, r8
	bcc	.L746
.L745:
	add	r3, r3, #1
	uxth	r3, r3
.L743:
	cmp	r3, r1
	bcc	.L748
	ldmfd	sp!, {r4, r5, r6, r7, r8, sl, pc}
.L750:
	.align	2
.L749:
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
	ldr	r3, .L756
	ldr	r2, .L756+4
	ldrh	r1, [r3, r2]
	ldr	r2, [r3, #-1148]
	mov	r3, #0
	b	.L752
.L754:
	ldrh	ip, [r2], #2
	cmp	ip, r0
	beq	.L755
	add	r3, r3, #1
	uxth	r3, r3
.L752:
	cmp	r3, r1
	bne	.L754
	mov	r0, #0
	bx	lr
.L755:
	mov	r0, #1
	bx	lr
.L757:
	.align	2
.L756:
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
	ldr	r3, .L762
	ldr	r2, .L762+4
	ldrh	ip, [r3, r2]
	ldr	r2, [r3, #-1148]
	mov	r3, #0
	mov	r1, r2
	b	.L759
.L761:
	ldrh	r7, [r1], #2
	cmp	r7, r0
	beq	.L760
	add	r3, r3, #1
	uxth	r3, r3
.L759:
	cmp	r3, ip
	bne	.L761
	mov	r3, r3, asl #1
	strh	r0, [r2, r3]	@ movhi
	ldr	r2, .L762
	ldr	r3, .L762+4
	ldrh	r1, [r2, r3]
	add	r1, r1, #1
	strh	r1, [r2, r3]	@ movhi
.L760:
	ldr	r3, .L762
	mov	r0, #12
	ldr	r2, .L762+8
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
.L763:
	.align	2
.L762:
	.word	.LANCHOR2
	.word	-1152
	.word	-1140
	.fnend
	.size	FtlGcUpdatePage, .-FtlGcUpdatePage
	.align	2
	.global	FtlGcRefreshBlock
	.type	FtlGcRefreshBlock, %function
FtlGcRefreshBlock:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	stmfd	sp!, {r4, lr}
	.save {r4, lr}
	mov	r4, r0
	mov	r1, r4
	ldr	r0, .L768
	bl	printk
	ldr	r3, .L768+4
	ldr	r2, .L768+8
	movw	r1, #65535
	ldrh	r0, [r3, r2]
	cmp	r0, r1
	beq	.L767
.L765:
	ldr	r2, .L768+12
	ldrh	r0, [r3, r2]
	cmp	r0, r1
	bne	.L766
.L767:
	strh	r4, [r3, r2]	@ movhi
.L766:
	mov	r0, #0
	ldmfd	sp!, {r4, pc}
.L769:
	.align	2
.L768:
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
	ldr	r5, .L775
	mov	r2, r4
	ldr	r7, .L775+4
	ldrh	r1, [r5, r7]
	mov	r6, r0
	ldr	r0, .L775+8
	bl	printk
	mov	r0, r6
	bl	FtlGcRefreshBlock
	ldr	r3, [r5, #-1884]
	cmp	r3, #0
	beq	.L771
	ldr	r3, [r5, #-2084]
	mov	r6, r6, asl #1
	ldrh	r2, [r3, r6]
	cmp	r2, #29
	subhi	r2, r2, #30
	strhih	r2, [r3, r6]	@ movhi
.L771:
	ldrh	r1, [r5, r7]
	mov	r3, #0
	ldr	r2, .L775+12
	b	.L772
.L774:
	ldrh	r0, [r2, #2]!
	cmp	r0, r4
	beq	.L773
	add	r3, r3, #1
	uxth	r3, r3
.L772:
	cmp	r3, r1
	bne	.L774
	cmp	r3, #15
	bhi	.L773
	ldr	r2, .L775
	ldr	r1, .L775+16
	add	r0, r2, r3, asl #1
	add	r3, r3, #1
	strh	r4, [r0, r1]	@ movhi
	sub	r1, r1, #2
	strh	r3, [r2, r1]	@ movhi
.L773:
	mov	r0, #0
	ldmfd	sp!, {r3, r4, r5, r6, r7, pc}
.L776:
	.align	2
.L775:
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
	ldr	r2, .L780
	ldr	r3, .L780+4
	ldrh	r2, [r3, r2]
	cmp	r2, #0
	beq	.L778
	ldr	r1, .L780+8
	ldrh	r0, [r3, r1]
	movw	r1, #65535
	cmp	r0, r1
	bne	.L778
	ldr	r4, .L780+12
	ldr	r5, .L780+4
	ldrh	r1, [r3, r4]
	cmp	r1, r2
	movcs	r2, #0
	strcsh	r2, [r3, r4]	@ movhi
	ldrh	r2, [r5, r4]
	ldr	r3, .L780+16
	add	r2, r5, r2, asl #1
	ldrh	r0, [r2, r3]
	bl	P2V_block_in_plane
	bl	FtlGcRefreshBlock
	ldrh	r3, [r5, r4]
	add	r3, r3, #1
	strh	r3, [r5, r4]	@ movhi
.L778:
	mov	r0, #0
	ldmfd	sp!, {r3, r4, r5, pc}
.L781:
	.align	2
.L780:
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
	beq	.L783
	bl	memset
.L783:
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
	ldr	r4, .L785
	movw	r5, #3916
	ldr	r2, .L785+4
	mov	r1, #255
	ldr	r6, .L785+8
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
.L786:
	.align	2
.L785:
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
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	stmfd	sp!, {r3, r4, r5, r6, r7, r8, r9, sl, fp, lr}
	.save {r3, r4, r5, r6, r7, r8, r9, sl, fp, lr}
	movw	r3, #3854
	ldr	r7, .L796
	mov	r2, #6
	ldr	r4, .L796+4
	mov	r1, #0
	mov	r6, #0
	ldrh	r3, [r7, r3]
	mov	sl, r6
	ldr	r0, [r4, #-2072]
	mul	r2, r2, r3
	bl	ftl_memset
	ldr	r3, .L796+8
	str	r6, [r4, #-2052]
	str	r6, [r4, #-2068]
	strh	r6, [r4, r3]	@ movhi
	add	r3, r3, #8
	str	r6, [r4, #-2060]
	strh	r6, [r4, r3]	@ movhi
	mov	r4, r6
	b	.L788
.L790:
	add	r3, r7, r8
	mov	r1, r4
	add	r8, r8, #1
	ldrb	r0, [r3, #3870]	@ zero_extendqisi2
	bl	V2P_block
	uxth	r8, r8
	bl	FtlBbmIsBadBlock
	cmp	r0, #0
	ldreqh	r3, [r7, r9]
	addeq	r5, r5, r3
	uxtheq	r5, r5
	b	.L794
.L795:
	mov	r5, #0
	movw	fp, #3844
	mov	r8, r5
	movw	r9, #3912
.L794:
	ldrh	r3, [r7, fp]
	cmp	r3, r8
	bhi	.L790
	cmp	r5, #0
	beq	.L791
	mov	r1, r5
	mov	r0, #32768
	bl	__aeabi_idiv
	uxth	r5, r0
.L791:
	ldr	r3, .L796+4
	mov	r1, #6
	ldr	r2, [r3, #-2072]
	mla	r2, r1, r4, r2
	strh	r5, [r2, #4]	@ movhi
	ldr	r2, .L796+12
	ldrh	r2, [r3, r2]
	cmp	r2, r4
	beq	.L792
	ldr	r2, .L796+16
	ldrh	r2, [r3, r2]
	cmp	r2, r4
	beq	.L792
	ldr	r2, .L796+20
	ldrh	r2, [r3, r2]
	cmp	r2, r4
	beq	.L792
	ldr	r2, [r3, #-2064]
	mov	r3, r4, asl #1
	ldrh	r3, [r2, r3]
	cmp	r3, #0
	bne	.L793
	add	r6, r6, #1
	mov	r0, r4
	uxth	r6, r6
	bl	INSERT_FREE_LIST
	b	.L792
.L793:
	add	sl, sl, #1
	mov	r0, r4
	uxth	sl, sl
	bl	INSERT_DATA_LIST
.L792:
	add	r4, r4, #1
	uxth	r4, r4
.L788:
	movw	r2, #3852
	ldrh	r3, [r7, r2]
	cmp	r3, r4
	bhi	.L795
	ldr	r3, .L796+4
	mov	r0, #0
	ldr	r2, .L796+8
	strh	sl, [r3, r2]	@ movhi
	add	r2, r2, #8
	strh	r6, [r3, r2]	@ movhi
	ldmfd	sp!, {r3, r4, r5, r6, r7, r8, r9, sl, fp, pc}
.L797:
	.align	2
.L796:
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
	ldr	r4, .L801
	movw	r6, #3922
	ldr	r5, .L801+4
	movw	r7, #3950
	mov	r8, #12
	ldr	r2, [r4, #3940]
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
	b	.L799
.L800:
	mul	r3, r8, r2
	ldr	r1, [r5, #-1900]
	add	sl, r1, r3
	str	ip, [sl, #4]
	strh	r0, [r1, r3]	@ movhi
	ldr	r1, [r5, #-1900]
	ldr	sl, [r5, #-1092]
	add	r3, r1, r3
	ldrh	r1, [r4, r6]
	mul	r1, r2, r1
	add	r2, r2, #1
	uxth	r2, r2
	bic	r1, r1, #3
	add	r1, sl, r1
	str	r1, [r3, #8]
.L799:
	ldrh	r3, [r4, r7]
	ldr	r1, .L801
	cmp	r3, r2
	ldr	r3, .L801+4
	bhi	.L800
	ldr	r0, .L801+8
	mvn	r2, #0
	strh	r2, [r3, r0]	@ movhi
	ldr	r0, .L801+12
	strh	r2, [r3, r0]	@ movhi
	ldr	r0, [r1, #3940]
	ldr	r2, .L801+16
	strh	r0, [r3, r2]	@ movhi
	sub	r2, r2, #6
	ldr	r0, .L801+20
	strh	r0, [r3, r2]	@ movhi
	add	r2, r2, #36
	ldrh	r0, [r3, r2]
	sub	r2, r2, #32
	strh	r0, [r3, r2]	@ movhi
	movw	r2, #3948
	ldrh	r1, [r1, r2]
	ldr	r2, .L801+24
	strh	r1, [r3, r2]	@ movhi
	ldr	r2, [r3, #-1044]
	str	r2, [r3, #-1076]
	ldr	r2, [r3, #-1040]
	str	r2, [r3, #-1072]
	ldr	r2, [r3, #-1096]
	str	r2, [r3, #-1068]
	ldr	r2, [r3, #-1036]
	str	r2, [r3, #-1064]
	ldmfd	sp!, {r3, r4, r5, r6, r7, r8, sl, pc}
.L802:
	.align	2
.L801:
	.word	.LANCHOR0
	.word	.LANCHOR2
	.word	-1086
	.word	-1088
	.word	-1078
	.word	-3902
	.word	-1082
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
	b	.L804
.L808:
	ldr	r0, [r7, r3, asl #2]
	mov	r2, #0
	ubfx	r0, r0, #10, #16
	b	.L805
.L807:
	mov	r1, r2, asl #1
	add	r2, r2, #1
	ldrh	ip, [r6, r1]
	uxth	r2, r2
	cmp	ip, r0
	ldreqh	ip, [r5, r1]
	addeq	ip, ip, #1
	streqh	ip, [r5, r1]	@ movhi
.L805:
	ldrh	r1, [r4, #10]
	cmp	r1, r2
	bhi	.L807
	add	r3, r3, #1
	uxth	r3, r3
.L804:
	ldrh	r2, [r4, #6]
	cmp	r2, r3
	bhi	.L808
	mov	sl, #0
	ldrh	fp, [r5, #0]
	mov	r7, sl
	ldr	r3, .L814
	movw	r2, #3914
	b	.L809
.L813:
	ldrh	r1, [r4, #0]
	cmp	r1, r7
	bne	.L810
	ldrh	r0, [r4, #2]
	ldrh	r1, [r3, r2]
	cmp	r0, r1
	movcc	r0, r7, asl #1
	strcch	r1, [r5, r0]	@ movhi
.L810:
	mov	r9, r7, asl #1
	ldrh	r8, [r5, r9]
	cmp	fp, r8
	movhi	sl, r7
	movhi	fp, r8
	cmp	r8, #0
	bne	.L812
	ldrh	r0, [r6, r9]
	cmp	r0, #0
	beq	.L812
	mov	r1, #1
	stmia	sp, {r2, r3}
	bl	FtlFreeSysBlkQueueIn
	strh	r8, [r6, r9]	@ movhi
	ldmia	sp, {r2, r3}
	ldrh	r1, [r4, #8]
	sub	r1, r1, #1
	strh	r1, [r4, #8]	@ movhi
.L812:
	add	r7, r7, #1
	uxth	r7, r7
.L809:
	ldrh	r1, [r4, #10]
	cmp	r1, r7
	bhi	.L813
	mov	r0, sl
	ldmfd	sp!, {r1, r2, r3, r4, r5, r6, r7, r8, r9, sl, fp, pc}
.L815:
	.align	2
.L814:
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
	ldr	r3, .L817
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
.L818:
	.align	2
.L817:
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
	ldr	r3, .L820
	movw	r2, #3980
	mvn	r1, #0
	ldr	r0, .L820+4
	strh	r1, [r3, r2]	@ movhi
	add	r2, r2, #6
	mov	r1, #0
	strh	r1, [r3, r2]	@ movhi
	mov	r1, #255
	mov	r2, #16
	b	ftl_memset
.L821:
	.align	2
.L820:
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
	ldr	r3, .L826
	ldr	r2, .L826+4
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
.L824:
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
	bne	.L824
	ldmfd	sp!, {r4, r5, r6, pc}
.L827:
	.align	2
.L826:
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
	ldr	r5, .L829
	mov	r4, #0
	ldr	r2, .L829+4
	mov	r1, r4
	ldr	r6, .L829+8
	movw	r7, #3854
	str	r3, [r5, #-1012]
	strh	r3, [r5, r2]	@ movhi
	movw	r3, #3956
	strh	r4, [r6, r3]	@ movhi
	movw	r3, #3932
	ldrh	r2, [r6, r3]
	ldr	r0, [r6, #3960]
	str	r4, [r5, #-1028]
	mov	r2, r2, asl #1
	str	r4, [r5, #-1020]
	str	r4, [r5, #-1016]
	str	r4, [r5, #-1884]
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
	ldr	r0, .L829+12
	bl	ftl_memset
	mov	r1, r4
	mov	r2, #512
	ldr	r0, .L829+16
	bl	ftl_memset
	bl	FtlGcBufInit
	bl	FtlL2PDataInit
	mov	r0, r4
	ldmfd	sp!, {r3, r4, r5, r6, r7, pc}
.L830:
	.align	2
.L829:
	.word	.LANCHOR2
	.word	-1024
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
	movw	r3, #65535
	ldr	r4, .L862
	mov	r6, #0
	ldr	r2, .L862+4
	mov	r0, #1024
	ldr	r5, .L862+8
	mov	r7, #12
	str	r3, [r4, #-996]
	mvn	r3, #0
	strh	r3, [r4, r2]	@ movhi
	add	r2, r2, #2
	str	r6, [r4, #-1004]
	movw	sl, #3844
	strh	r3, [r4, r2]	@ movhi
	mov	r2, #32
	ldr	r3, .L862+12
	mov	r8, #36
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
	add	r3, r3, #36
	str	r6, [r4, #-1876]
	strh	r6, [r4, r3]	@ movhi
	movw	r3, #3918
	ldrh	r1, [r5, r3]
	str	r6, [r4, #-1868]
	str	r6, [r4, #-1864]
	str	r6, [r4, #-1880]
	str	r6, [r4, #-1840]
	str	r6, [r4, #-1836]
	str	r6, [r4, #-2080]
	str	r6, [r4, #-1828]
	str	r6, [r4, #-1824]
	str	r6, [r4, #-1000]
	str	r6, [r4, #-1180]
	str	r6, [r4, #-992]
	str	r6, [r4, #-1188]
	str	r6, [r5, #3976]
	bl	__aeabi_idiv
	movw	r3, #3844
	ldrh	r3, [r5, r3]
	str	r6, [r5, #3964]
	movw	r6, #3916
	mov	r3, r3, asl #2
	cmp	r0, r3
	str	r0, [r4, #-988]
	ldrh	r0, [r5, r6]
	strhi	r3, [r4, #-988]
	ldr	r4, .L862
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
	str	r0, [r4, #-984]
	mov	r0, r6
	bl	ftl_malloc
	str	r0, [r4, #-980]
	mov	r0, r9
	bl	ftl_malloc
	str	r0, [r4, #-976]
	mov	r0, r6
	bl	ftl_malloc
	str	r0, [r4, #-2088]
	mov	r0, r6
	bl	ftl_malloc
	str	r0, [r4, #-1160]
	ldr	r0, [r4, #-988]
	mul	r0, r8, r0
	bl	ftl_malloc
	movw	r8, #3922
	ldrh	r3, [r5, sl]
	ldrh	r6, [r5, r8]
	mov	r3, r3, asl #1
	add	r3, r3, #1
	str	r3, [r4, #-1156]
	str	r0, [r5, #3968]
	mov	r0, r6
	bl	ftl_malloc
	str	r0, [r4, #-2076]
	mov	r0, r6
	bl	ftl_malloc
	str	r0, [r4, #-972]
	mov	r0, r6
	bl	ftl_malloc
	str	r0, [r4, #-968]
	ldr	r0, [r4, #-1156]
	mul	r0, r0, r6
	bl	ftl_malloc
	str	r0, [r4, #-1168]
	ldr	r0, [r4, #-988]
	mul	r0, r0, r6
	bl	ftl_malloc
	str	r0, [r4, #-964]
	mov	r0, r6
	bl	ftl_malloc
	str	r0, [r4, #-960]
	mov	r0, r6
	bl	ftl_malloc
	movw	r6, #3924
	str	r0, [r4, #-956]
	ldr	r0, [r4, #-1156]
	mul	r0, r7, r0
	bl	ftl_malloc
	ldrh	r3, [r5, r6]
	ldrh	sl, [r5, sl]
	mul	sl, sl, r3
	str	r0, [r4, #-1172]
	mov	r0, sl
	bl	ftl_malloc
	str	r0, [r4, #-952]
	mov	r0, sl, asl #3
	bl	ftl_malloc
	ldrh	r3, [r5, r6]
	ldr	sl, .L862+16
	str	r0, [r4, #-948]
	ldr	r0, [r4, #-1156]
	mul	r0, r0, r3
	bl	ftl_malloc
	ldrh	r3, [r5, r6]
	movw	r6, #3854
	str	r0, [r4, #-1164]
	ldr	r0, [r4, #-988]
	mul	r0, r0, r3
	bl	ftl_malloc
	str	r0, [r4, #-944]
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
	str	r0, [r4, #-936]
	add	r0, r0, #32
	str	r0, [r4, #-2084]
	mov	r0, sl
	bl	ftl_malloc
	str	r0, [r4, #-932]
	mov	r0, sl
	bl	ftl_malloc
	ldr	sl, [r5, #3940]
	mov	sl, sl, asl #1
	str	r0, [r4, #-2064]
	mov	r0, sl
	bl	ftl_malloc
	str	r0, [r4, #-1044]
	mov	r0, sl
	bl	ftl_malloc
	movw	sl, #3932
	str	r0, [r4, #-1096]
	ldrh	r0, [r5, r6]
	mov	r0, r0, lsr #3
	add	r0, r0, #4
	bl	ftl_malloc
	str	r0, [r4, #-1888]
	ldrh	r0, [r5, sl]
	mov	r0, r0, asl #1
	bl	ftl_malloc
	str	r0, [r5, #3960]
	ldrh	r0, [r5, sl]
	mov	r0, r0, asl #1
	bl	ftl_malloc
	str	r0, [r4, #-928]
	ldrh	r0, [r5, sl]
	add	sl, sl, #2
	mov	r0, r0, asl #2
	bl	ftl_malloc
	str	r0, [r4, #-924]
	ldrh	r0, [r5, sl]
	mov	r0, r0, asl #2
	bl	ftl_malloc
	ldrh	r2, [r5, sl]
	mov	r1, #0
	add	sl, sl, #16
	mov	r2, r2, asl #2
	str	r0, [r4, #-920]
	bl	ftl_memset
	movw	r3, #3948
	ldrh	r0, [r5, r3]
	mov	r0, r0, asl #2
	bl	ftl_malloc
	str	r0, [r4, #-1036]
	ldr	r0, [r5, #3940]
	mov	r0, r0, asl #2
	bl	ftl_malloc
	str	r0, [r4, #-1040]
	ldrh	r0, [r5, sl]
	mul	r0, r7, r0
	bl	ftl_malloc
	ldrh	r3, [r5, sl]
	movw	r7, #3866
	str	r0, [r4, #-1900]
	ldrh	r0, [r5, r8]
	mul	r0, r0, r3
	bl	ftl_malloc
	ldrh	r3, [r5, r6]
	ldr	r6, .L862+20
	str	r0, [r4, #-1092]
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
	str	r0, [r4, #-2072]
	mov	r0, r3, asl #2
	bl	ftl_malloc
	ldrh	r1, [r4, r6]
	mov	r3, #1
	ldr	ip, .L862+8
	mov	r1, r1, asl #2
	mov	r2, r1
	str	r0, [r5, #4008]
	ldrh	r5, [r5, r7]
	ldr	r0, .L862+24
	b	.L833
.L834:
	ldr	r4, [ip, #4008]
	add	r3, r3, #1
	add	r4, r4, r2
	add	r2, r2, r1
	str	r4, [r0, #4]!
.L833:
	cmp	r3, r5
	bcc	.L834
	ldr	r0, .L862+28
	mov	r2, #0
	mov	r1, r2
	add	r0, r0, r3, asl #2
	b	.L835
.L836:
	add	ip, r0, r2
	add	r3, r3, #1
	add	r2, r2, #4
	str	r1, [ip, #28]
.L835:
	cmp	r3, #7
	bls	.L836
	ldr	r3, .L862
	ldr	r2, [r3, #-1044]
	cmp	r2, #0
	beq	.L861
.L837:
	ldr	r2, [r3, #-1096]
	cmp	r2, #0
	beq	.L861
.L839:
	ldr	r2, [r3, #-1036]
	cmp	r2, #0
	beq	.L861
.L840:
	ldr	r2, [r3, #-1040]
	cmp	r2, #0
	beq	.L861
.L841:
	ldr	r2, [r3, #-1900]
	cmp	r2, #0
	beq	.L861
.L842:
	ldr	r2, [r3, #-1092]
	cmp	r2, #0
	beq	.L861
.L843:
	ldr	r2, [r3, #-2072]
	cmp	r2, #0
	beq	.L861
.L844:
	ldr	r2, .L862+8
	ldr	r2, [r2, #4008]
	cmp	r2, #0
	beq	.L861
.L845:
	ldr	r3, [r3, #-2064]
	cmp	r3, #0
	beq	.L861
.L846:
	ldr	r3, .L862
	ldr	r2, [r3, #-1148]
	cmp	r2, #0
	beq	.L861
	ldr	r2, [r3, #-1144]
	cmp	r2, #0
	beq	.L861
.L848:
	ldr	r2, [r3, #-984]
	cmp	r2, #0
	beq	.L861
	ldr	r2, [r3, #-976]
	cmp	r2, #0
	beq	.L861
	ldr	r2, [r3, #-2088]
	cmp	r2, #0
	beq	.L861
	ldr	r2, [r3, #-1160]
	cmp	r2, #0
	beq	.L861
	ldr	r2, [r3, #-980]
	cmp	r2, #0
	beq	.L861
.L850:
	ldr	r2, [r3, #-2076]
	cmp	r2, #0
	beq	.L861
	ldr	r2, [r3, #-972]
	cmp	r2, #0
	beq	.L861
	ldr	r3, [r3, #-968]
	cmp	r3, #0
	beq	.L861
	ldr	r3, .L862
	ldr	r2, [r3, #-1168]
	cmp	r2, #0
	beq	.L861
	ldr	r2, [r3, #-960]
	cmp	r2, #0
	beq	.L861
	ldr	r2, [r3, #-956]
	cmp	r2, #0
	beq	.L861
	ldr	r2, [r3, #-1172]
	cmp	r2, #0
	beq	.L861
.L852:
	ldr	r2, [r3, #-952]
	cmp	r2, #0
	beq	.L861
	ldr	r2, [r3, #-948]
	cmp	r2, #0
	beq	.L861
	ldr	r2, [r3, #-1164]
	cmp	r2, #0
	beq	.L861
.L854:
	ldr	r2, [r3, #-2084]
	cmp	r2, #0
	beq	.L861
	ldr	r3, [r3, #-1008]
	cmp	r3, #0
	beq	.L861
.L856:
	ldr	r3, .L862+8
	ldr	r3, [r3, #3960]
	cmp	r3, #0
	beq	.L861
.L857:
	ldr	r3, .L862
	ldr	r2, [r3, #-928]
	cmp	r2, #0
	beq	.L861
.L858:
	ldr	r2, [r3, #-924]
	cmp	r2, #0
	beq	.L861
.L859:
	ldr	r3, [r3, #-920]
	cmp	r3, #0
	bne	.L860
.L861:
	ldr	r0, .L862+32
	ldr	r1, .L862+36
	bl	printk
	mvn	r0, #0
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, sl, pc}
.L860:
	mov	r0, #0
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, sl, pc}
.L863:
	.align	2
.L862:
	.word	.LANCHOR2
	.word	-1138
	.word	.LANCHOR0
	.word	-1192
	.word	-940
	.word	-1032
	.word	.LANCHOR0+4008
	.word	.LANCHOR0+3980
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
	ldr	r3, .L867
	ldr	r1, [r3, #2776]
	ldr	r0, [r3, #4]
	ldrb	r2, [r1, #9]	@ zero_extendqisi2
	mul	r2, r0, r2
	mov	r0, #0
	uxth	r2, r2
	strb	r2, [r4, #4]
	mov	ip, r2, lsr #8
	strb	ip, [r4, #5]
	ldrb	ip, [r3, #3836]	@ zero_extendqisi2
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
	ldr	r2, [r3, #2776]
	ldrb	ip, [r3, #3766]	@ zero_extendqisi2
	mov	r3, r0
	ldrb	r1, [r2, #9]	@ zero_extendqisi2
	strb	r1, [r4, #6]
	mov	r1, #32
	strb	r1, [r4, #8]
	ldrb	r2, [r2, #7]	@ zero_extendqisi2
	ldr	r1, .L867+4
	strb	r0, [r4, #10]
	strb	r2, [r4, #9]
	mov	r2, #1
	b	.L865
.L866:
	ldrb	r5, [r3, r1]	@ zero_extendqisi2
	add	r3, r3, #1
	ldrb	r0, [r4, #10]	@ zero_extendqisi2
	orr	r0, r0, r2, asl r5
	strb	r0, [r4, #10]
.L865:
	uxtb	r0, r3
	cmp	r0, ip
	bcc	.L866
	ldmfd	sp!, {r3, r4, r5, pc}
.L868:
	.align	2
.L867:
	.word	.LANCHOR0
	.word	.LANCHOR0+3768
	.fnend
	.size	ReadFlashInfo, .-ReadFlashInfo
	.align	2
	.global	FlashDieInfoInit
	.type	FlashDieInfoInit, %function
FlashDieInfoInit:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	ldr	r1, .L881
	mov	r2, #0
	stmfd	sp!, {r3, r4, r5, r6, r7, r8, sl, lr}
	.save {r3, r4, r5, r6, r7, r8, sl, lr}
	ldr	r3, .L881+4
	strb	r2, [r1, #-916]
	ldr	r1, .L881+8
	strb	r2, [r3, #3766]
	movw	r2, #2698
	ldrh	r2, [r1, r2]
	cmp	r2, #256
	str	r2, [r3, #4]
	movhi	r2, #512
	bhi	.L880
	cmp	r2, #128
	bls	.L871
	mov	r2, #256
.L880:
	str	r2, [r3, #4]
.L871:
	mov	r1, #0
	mov	r2, #8
	ldr	r0, .L881+12
	mov	r6, #0
	bl	ftl_memset
	mov	r1, #0
	mov	r2, #32
	ldr	r0, .L881+16
	bl	ftl_memset
	ldr	r0, .L881+20
	mov	r1, #0
	mov	r2, #128
	bl	ftl_memset
	ldr	r7, .L881+24
	ldr	r5, .L881+4
	mov	r8, r7
.L873:
	ldr	r4, [r5, #2776]
	mov	r1, r8
	add	r0, r4, #1
	ldrb	r2, [r4, #0]	@ zero_extendqisi2
	bl	FlashMemCmp8
	cmp	r0, #0
	bne	.L872
	ldrb	r3, [r5, #3766]	@ zero_extendqisi2
	add	r2, r5, r3, asl #2
	str	r0, [r2, #2740]
	add	r2, r5, r3
	add	r3, r3, #1
	strb	r3, [r5, #3766]
	strb	r6, [r2, #3768]
.L872:
	add	r6, r6, #1
	add	r8, r8, #8
	cmp	r6, #4
	bne	.L873
	ldr	r5, .L881+4
	ldr	r3, .L881
	ldrb	r2, [r5, #3766]	@ zero_extendqisi2
	strb	r2, [r3, #-916]
	ldrb	r3, [r4, #8]	@ zero_extendqisi2
	cmp	r3, #2
	bne	.L874
	add	sl, r4, #1
	mov	r6, #0
	mov	r8, r5
.L877:
	mov	r0, sl
	mov	r1, r7
	ldrb	r2, [r4, #0]	@ zero_extendqisi2
	bl	FlashMemCmp8
	cmp	r0, #0
	bne	.L875
	ldrb	r1, [r4, #13]	@ zero_extendqisi2
	ldr	r0, [r5, #4]
	ldrb	r3, [r5, #3766]	@ zero_extendqisi2
	mul	r0, r0, r1
	ldrh	r1, [r4, #14]
	add	r2, r5, r3, asl #2
	and	r1, r1, #65280
	mul	r1, r1, r0
	str	r1, [r2, #2740]
	ldrb	r0, [r4, #23]	@ zero_extendqisi2
	cmp	r0, #0
	movne	r1, r1, asl #1
	strne	r1, [r2, #2740]
	add	r2, r8, r3
	add	r3, r3, #1
	strb	r3, [r8, #3766]
	strb	r6, [r2, #3768]
.L875:
	add	r6, r6, #1
	add	r7, r7, #8
	cmp	r6, #4
	bne	.L877
.L874:
	ldr	r3, .L881+4
	ldrb	r1, [r4, #13]	@ zero_extendqisi2
	ldr	r2, .L881
	ldrb	r3, [r3, #3766]	@ zero_extendqisi2
	mul	r1, r1, r3
	ldrh	r3, [r4, #14]
	mul	r1, r3, r1
	ldr	r3, .L881+28
	strh	r1, [r2, r3]	@ movhi
	ldmfd	sp!, {r3, r4, r5, r6, r7, r8, sl, pc}
.L882:
	.align	2
.L881:
	.word	.LANCHOR2
	.word	.LANCHOR0
	.word	.LANCHOR1
	.word	.LANCHOR0+3768
	.word	.LANCHOR0+2740
	.word	.LANCHOR0+2780
	.word	.LANCHOR0+2132
	.word	-914
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
	bne	.L884
	ldr	r3, .L923
.L885:
	strh	r0, [r3, #2]!	@ movhi
	add	r0, r0, #1
	uxth	r0, r0
	cmp	r0, #256
	bne	.L885
	b	.L886
.L884:
	cmp	r0, #1
	bne	.L887
	ldr	r0, .L923+4
	mov	r2, #0
	mov	r3, r2
.L890:
	uxth	r1, r3
	cmp	r1, #3
	bls	.L888
	tst	r3, #1
	moveq	r1, #2
	movne	r1, #3
	rsb	r1, r1, r2
	uxth	r1, r1
.L888:
	add	r3, r3, #1
	strh	r1, [r2, r0]	@ movhi
	cmp	r3, #256
	add	r2, r2, #2
	bne	.L890
	b	.L886
.L887:
	cmp	r0, #2
	bne	.L891
	mov	r3, #0
	ldr	r0, .L923
	movw	r2, #65535
	mov	r1, r3
	b	.L922
.L894:
	cmp	r3, #1
	movls	r1, r3
	movhi	r1, r2
.L922:
	add	r3, r3, #1
	add	r2, r2, #2
	strh	r1, [r0, #2]!	@ movhi
	uxth	r3, r3
	uxth	r2, r2
	cmp	r3, #256
	bne	.L894
	b	.L886
.L891:
	cmp	r0, #3
	bne	.L895
	ldr	r0, .L923+4
	mov	r2, #0
	mov	r3, r2
.L898:
	uxth	r1, r3
	cmp	r1, #5
	bls	.L896
	tst	r3, #1
	moveq	r1, #4
	movne	r1, #5
	rsb	r1, r1, r2
	uxth	r1, r1
.L896:
	add	r3, r3, #1
	strh	r1, [r2, r0]	@ movhi
	cmp	r3, #256
	add	r2, r2, #2
	bne	.L898
	b	.L886
.L895:
	cmp	r0, #4
	mov	r2, #0
	bne	.L899
	ldr	r3, .L923+8
	movw	r1, #2228
	strh	r2, [r3, r1]	@ movhi
	movw	r2, #2230
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
	ldr	r0, .L923+12
	strh	r1, [r3, r2]	@ movhi
	mov	r2, #2240
	mov	r1, #7
	strh	r1, [r3, r2]	@ movhi
	add	r2, r2, #2
	mov	r1, #8
	strh	r1, [r3, r2]	@ movhi
	mov	r2, #16
	mov	r3, r1
.L901:
	tst	r3, #1
	add	r3, r3, #1
	moveq	r1, #6
	movne	r1, #7
	cmp	r3, #256
	rsb	r1, r1, r2
	add	r2, r2, #2
	strh	r1, [r0, #2]!	@ movhi
	uxth	r2, r2
	bne	.L901
	b	.L886
.L899:
	cmp	r0, #5
	bne	.L902
	ldr	r1, .L923
	mov	r3, r2
.L903:
	strh	r3, [r1, #2]!	@ movhi
	add	r3, r3, #1
	uxth	r3, r3
	cmp	r3, #16
	bne	.L903
	ldr	r2, .L923+16
.L904:
	strh	r3, [r2, #2]!	@ movhi
	add	r3, r3, #2
	uxth	r3, r3
	cmp	r3, #496
	bne	.L904
	b	.L886
.L902:
	cmp	r0, #6
	bne	.L886
	ldr	r0, .L923
	mov	r3, r2
.L907:
	uxth	r1, r3
	cmp	r1, #5
	bls	.L905
	tst	r3, #1
	moveq	r1, #10
	movne	r1, #12
	rsb	r1, r1, r2
	uxth	r1, r1
.L905:
	add	r3, r3, #1
	add	r2, r2, #3
	cmp	r3, #256
	strh	r1, [r0, #2]!	@ movhi
	uxth	r2, r2
	bne	.L907
.L886:
	mov	r2, #1024
	ldr	r0, .L923+20
	mov	r1, #255
	uxth	r4, r4
	bl	ftl_memset
	ldr	r2, .L923
	mov	r3, #0
	ldr	r0, .L923+24
	b	.L908
.L909:
	ldrh	r1, [r2, #2]!
	add	r3, r3, #1
	uxth	r3, r3
	add	ip, r0, r1, asl #1
	sub	ip, ip, #912
	strh	r1, [ip, #0]	@ movhi
.L908:
	cmp	r3, r4
	bcc	.L909
	ldmfd	sp!, {r4, pc}
.L924:
	.align	2
.L923:
	.word	.LANCHOR0+2226
	.word	.LANCHOR0+2228
	.word	.LANCHOR0
	.word	.LANCHOR0+2242
	.word	.LANCHOR0+2258
	.word	.LANCHOR2-912
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
	ldr	r4, .L930
	mov	ip, r0
	mov	r2, r3
	ldr	r4, [r4, #112]
	add	r4, r4, #4096
	add	r1, r4, r1
	movne	r0, r1
	movne	r1, ip
	ldmfd	sp!, {r4, lr}
	b	memcpy
.L931:
	.align	2
.L930:
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
	ldr	r3, .L937
	ldr	r0, [r3, #3952]
	bx	lr
.L938:
	.align	2
.L937:
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
	b	.L940
.L946:
	cmp	r8, #0
	bne	.L941
	ldr	r0, .L948
	mov	r1, r6
	mov	r2, r7
	bl	printk
.L941:
	cmp	r5, #4
	ldreq	r0, .L948+4
	ldreq	r1, [r4, r7, asl #2]
	beq	.L947
	cmp	r5, #2
	moveq	r3, r7, asl #1
	ldreq	r0, .L948+4
	ldreqsh	r1, [r4, r3]
	ldrne	r0, .L948+4
	ldrneb	r1, [r4, r7]	@ zero_extendqisi2
.L947:
	add	r8, r8, #1
	bl	printk
	cmp	r8, #15
	bls	.L945
	ldr	r0, .L948+8
	mov	r8, #0
	ldr	r1, .L948+12
	bl	printk
.L945:
	add	r7, r7, #1
.L940:
	cmp	r7, sl
	bne	.L946
	ldr	r0, .L948+8
	ldr	r1, .L948+12
	ldmfd	sp!, {r3, r4, r5, r6, r7, r8, sl, lr}
	b	printk
.L949:
	.align	2
.L948:
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
	ldr	r6, .L973
	add	r0, r6, r0, asl #3
	ldr	r3, [r6, #12]
	ldr	r4, [r0, #16]
	cmp	r3, #3
	bls	.L967
	ldr	r3, [r4, #16]
	tst	r3, #4
	beq	.L967
	mov	r0, r4
	bl	wait_for_nandc_xfer_completed
	ldr	r5, [r4, #16]
	ldr	r3, [r4, #8]
	ands	r5, r5, #2
	str	r3, [sp, #0]
	movne	r5, #0
	bne	.L971
	b	.L972
.L957:
	ldr	r3, [r6, #12]
	cmp	r3, #5
	bls	.L954
	ldr	r3, [r4, #0]
	str	r3, [sp, #4]
	ldr	r3, [sp, #4]
	tst	r3, #8192
	beq	.L954
	ldr	r3, [sp, #4]
	tst	r3, #131072
	bne	.L955
.L954:
	add	r5, r5, #1
	bic	r3, r5, #-16777216
	cmp	r3, #0
	bne	.L971
	ldr	r2, [r4, #28]
	mov	r1, r5
	ldr	r3, [sp, #0]
	ubfx	r2, r2, #16, #5
	ldr	r0, .L973+4
	ubfx	r3, r3, #22, #6
	bl	printk
	ldr	r0, .L973+8
	mov	r1, r4
	mov	r2, #4
	mov	r3, #512
	bl	rknand_print_hex
.L971:
	ldr	r2, [r4, #28]
	ldr	r3, [sp, #0]
	ubfx	r2, r2, #16, #5
	ubfx	r3, r3, #22, #6
	cmp	r2, r3
	blt	.L957
.L955:
	ldr	r4, .L973
	ldr	r3, [r4, #3820]
	cmp	r3, #0
	beq	.L958
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
	b	.L958
.L961:
	ldr	r3, [r4, #8]
	add	r5, r5, #1
	str	r3, [sp, #0]
	bic	r3, r5, #-16777216
	cmp	r3, #0
	bne	.L972
	ldr	r2, [sp, #0]
	mov	r1, r5
	ldr	r3, [r4, #28]
	ldr	r0, .L973+12
	ubfx	r3, r3, #16, #5
	bl	printk
	ldr	r0, .L973+8
	mov	r1, r4
	mov	r2, #4
	mov	r3, #512
	bl	rknand_print_hex
.L972:
	ldr	r3, [sp, #0]
	tst	r3, #1048576
	beq	.L961
	ldr	r6, .L973
	ldr	r3, [r6, #3828]
	cmp	r3, #0
	beq	.L962
	mov	r0, r4
	bl	NandcSendDumpDataStart
.L962:
	ldr	r3, [r6, #3820]
	ldr	r5, .L973
	cmp	r3, #0
	beq	.L963
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
.L963:
	ldr	r3, .L973
	ldr	r3, [r3, #3828]
	cmp	r3, #0
	beq	.L958
	mov	r0, r4
	bl	NandcSendDumpDataDone
.L958:
	ldr	r3, .L973
	mov	r2, #0
	str	r2, [r3, #3820]
	b	.L950
.L967:
	ldr	r3, [r4, #8]
	str	r3, [sp, #0]
	ldr	r3, [sp, #0]
	tst	r3, #1048576
	beq	.L967
.L950:
	ldmfd	sp!, {r2, r3, r4, r5, r6, pc}
.L974:
	.align	2
.L973:
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
	ldr	r3, .L1013
	.pad #92
	sub	sp, sp, #92
	tst	r4, #63
	add	r3, r3, r0, asl #3
	mov	r7, r0
	mov	fp, r1
	mov	sl, r2
	ldr	r5, [sp, #128]
	ldr	r6, [r3, #16]
	bne	.L976
	cmp	r5, #0
	bne	.L977
	add	r0, sp, #24
	mov	r1, #255
	mov	r2, #64
	add	r5, sp, #24
	bl	ftl_memset
.L977:
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
	bne	.L978
	ldr	r1, .L1013
	mov	r0, sl, lsr #1
	mov	r3, fp
	mov	r2, fp
	ldr	ip, [r1, #3832]
	mov	r4, r1
	cmp	ip, #24
	movhi	ip, #128
	movls	ip, #64
	b	.L980
.L981:
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
.L980:
	cmp	r2, r0
	add	r7, r3, ip
	ldr	r1, .L1013
	bcc	.L981
	mov	r3, #0
	ldr	r0, [r1, #3832]
	mov	sl, sl, lsr #2
	ldr	r1, [r1, #12]
	mov	r4, r3
	b	.L982
.L988:
	add	r2, r3, #8
	ldr	r2, [r6, r2, asl #2]
	str	r2, [sp, #20]
	ldr	r2, [sp, #20]
	tst	r2, #4
	bne	.L1003
	ldr	r2, [sp, #20]
	ands	r2, r2, #32768
	bne	.L1003
	cmp	r1, #5
	bls	.L984
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
	b	.L1011
.L984:
	cmp	r1, #3
	bls	.L986
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
.L1011:
	orr	r2, ip, r2, asl #5
.L986:
	cmp	r4, r2
	movcc	r4, r2
	b	.L983
.L1003:
	mvn	r4, #0
.L983:
	add	r3, r3, #1
.L982:
	cmp	r3, sl
	bcs	.L978
	cmp	r0, #0
	bne	.L988
.L978:
	mov	r3, #0
	str	r3, [r6, #16]
	b	.L989
.L976:
	cmp	r1, #1
	mov	r8, #0
	bne	.L1009
	b	.L990
.L993:
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
.L990:
	cmp	r8, sl
	bcc	.L993
	mov	r4, #0
	b	.L989
.L1009:
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
	b	.L1012
.L998:
	mov	r0, r7
	bl	NandcXferComp
	ldr	r3, [fp, #32]
	add	ip, r5, #2
	cmp	ip, sl
	str	r3, [sp, #20]
	bcs	.L995
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
.L995:
	ldr	r3, [sp, #20]
	tst	r3, #4
	mvnne	r4, #0
	bne	.L996
	ldr	r2, [sp, #20]
	ldr	r3, [sp, #20]
	ubfx	r2, r2, #3, #5
	ubfx	r3, r3, #27, #1
	orr	r3, r2, r3, asl #5
	cmp	r4, r3
	movcc	r4, r3
.L996:
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
.L1012:
	cmp	ip, sl
	mov	r5, ip
	bcc	.L998
	mov	r6, fp
	ldr	fp, [sp, #12]
.L989:
	ldr	r3, .L1013
	rsbs	fp, fp, #1
	movcc	fp, #0
	ldr	r3, [r3, #12]
	cmp	r3, #5
	movls	fp, #0
	cmp	fp, #0
	beq	.L999
	ldr	r3, [r6, #0]
	and	r2, r3, #139264
	cmp	r2, #139264
	orreq	r3, r3, #131072
	streq	r3, [r6, #0]
	mvneq	r4, #0
.L999:
	mov	r0, r4
	add	sp, sp, #92
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, sl, fp, pc}
.L1014:
	.align	2
.L1013:
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
	ldr	r3, .L1018
	subs	r4, r0, #0
	mov	r5, r1
	mov	r6, r2
	ldrb	r7, [r3, #2697]	@ zero_extendqisi2
	bne	.L1016
	ldr	r3, .L1018+4
	ldrb	r2, [r3, #1]	@ zero_extendqisi2
	ldr	r1, [r3, #4]
	mul	r2, r1, r2
	cmp	r5, r2
	bcs	.L1016
	ldrb	r3, [r3, #0]	@ zero_extendqisi2
	cmp	r3, #0
	subeq	r7, r7, #2
	movne	r7, #4
.L1016:
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
.L1019:
	.align	2
.L1018:
	.word	.LANCHOR1
	.word	.LANCHOR0
	.fnend
	.size	FlashProgPage, .-FlashProgPage
	.align	2
	.global	FlashPageProgMsbFFData
	.type	FlashPageProgMsbFFData, %function
FlashPageProgMsbFFData:
	.fnstart
	@ args = 0, pretend = 0, frame = 24
	@ frame_needed = 0, uses_anonymous_args = 0
	stmfd	sp!, {r4, r5, r6, r7, r8, r9, sl, fp, lr}
	.save {r4, r5, r6, r7, r8, r9, sl, fp, lr}
	mov	r8, r2
	ldr	r2, .L1031
	.pad #28
	sub	sp, sp, #28
	mov	r6, r0
	str	r1, [sp, #12]
	ldrb	r1, [r2, #80]	@ zero_extendqisi2
	ldr	r3, [r2, #2776]
	cmp	r1, #0
	ldrb	r3, [r3, #19]	@ zero_extendqisi2
	beq	.L1021
	ldr	r1, [r2, #3788]
	ldr	r2, .L1031+4
	cmp	r1, r2
	beq	.L1020
.L1021:
	sub	r2, r3, #5
	uxtb	r2, r2
	cmp	r3, #68
	cmpne	r2, #1
	bls	.L1024
	cmp	r3, #35
	cmpne	r3, #17
	beq	.L1024
	cmp	r3, #18
	cmpne	r3, #19
	beq	.L1024
	b	.L1020
.L1029:
	add	r3, sl, r8, asl #1
	sub	r3, r3, #912
	ldrh	r2, [r3, #0]
	movw	r3, #65535
	cmp	r2, r3
	bne	.L1020
	mov	r1, #255
	ldr	r0, [sl, #116]
	mov	r2, #32768
	bl	ftl_memset
	ldr	r1, [sp, #20]
	cmp	r1, #1
	bhi	.L1025
	ldr	r3, .L1031+8
	ldr	r2, [r3, #2964]
	cmp	r2, #0
	beq	.L1026
	ldr	r1, [sp, #16]
	mov	r0, r6
	mov	fp, #0
	ldrb	r4, [r1, #20]	@ zero_extendqisi2
	ldr	r5, [r1, #16]
	add	r4, r4, #8
	str	r2, [sp, #8]
	str	r3, [sp, #4]
	bl	NandcFlashCs
	add	r7, r5, r4, asl #8
	mov	r1, #239
	mov	r0, r6
	str	r1, [r7, #8]
	mov	r1, #146
	str	r1, [r7, #4]
	str	fp, [r5, r4, asl #8]
	str	fp, [r5, r4, asl #8]
	str	fp, [r5, r4, asl #8]
	str	fp, [r5, r4, asl #8]
	bl	NandcFlashDeCs
	ldr	r2, [sp, #8]
	cmp	r2, #1
	bne	.L1027
	mov	r0, r6
	bl	NandcFlashCs
	mov	r2, #238
	mov	r0, #2000
	str	r2, [r7, #8]
	mov	r2, #146
	str	r2, [r7, #4]
	bl	NandcDelayns
	ldr	r2, [r5, r4, asl #8]
	mov	r0, r6
	str	r2, [sp, #8]
	bl	NandcFlashDeCs
	ldr	r2, [sp, #8]
	ldr	r3, [sp, #4]
	tst	r2, #255
	moveq	r2, #2
	streq	r2, [r3, #2964]
.L1028:
	strne	fp, [r3, #2964]
	bne	.L1020
.L1027:
	ldr	r3, [sp, #12]
	mov	r0, r6
	ldr	r2, [sl, #116]
	add	r1, r8, r3
	mov	r3, #0
	bl	FlashProgPage
	mov	r0, r6
	bl	NandcFlashCs
	mov	r3, #239
	mov	r0, r6
	str	r3, [r7, #8]
	mov	r3, #146
	str	r3, [r7, #4]
	mov	r3, #1
	str	r3, [r5, r4, asl #8]
	mov	r3, #0
	str	r3, [r5, r4, asl #8]
	str	r3, [r5, r4, asl #8]
	str	r3, [r5, r4, asl #8]
	bl	NandcFlashDeCs
	b	.L1026
.L1025:
	ldr	r2, [sp, #12]
	mov	r0, r6
	mov	r3, #0
	add	r1, r8, r2
	ldr	r2, [sl, #116]
	bl	FlashProgPage
.L1026:
	add	r8, r8, #1
	uxth	r8, r8
	b	.L1030
.L1024:
	ldr	sl, .L1031+12
	sub	r3, r3, #17
	ldr	r9, .L1031
	uxtb	r3, r3
	str	r3, [sp, #20]
	add	r3, r9, r6, asl #3
	str	r3, [sp, #16]
.L1030:
	ldr	r3, [r9, #2776]
	ldrh	r3, [r3, #10]
	cmp	r3, r8
	bhi	.L1029
.L1020:
	add	sp, sp, #28
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, sl, fp, pc}
.L1032:
	.align	2
.L1031:
	.word	.LANCHOR0
	.word	1446522928
	.word	.LANCHOR1
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
	ldr	r3, .L1036
	subs	r4, r0, #0
	mov	r6, r1
	mov	r5, r2
	ldrb	r7, [r3, #2697]	@ zero_extendqisi2
	bne	.L1034
	ldr	r3, .L1036+4
	ldrb	r2, [r3, #1]	@ zero_extendqisi2
	ldr	r3, [r3, #4]
	mul	r3, r3, r2
	cmp	r1, r3
	movcc	r7, #4
.L1034:
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
.L1037:
	.align	2
.L1036:
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
	ldr	fp, .L1049
	mov	r6, r0
	str	r1, [sp, #4]
	mov	sl, r2
	add	r3, fp, r0
	mov	r8, #0
	ldrb	r9, [fp, #2910]	@ zero_extendqisi2
	mvn	r4, #0
	ldrb	r5, [r3, #2920]	@ zero_extendqisi2
	bl	NandcWaitFlashReady
	mov	ip, fp
	b	.L1039
.L1044:
	add	r5, r5, #1
	ldrb	r1, [fp, #2909]	@ zero_extendqisi2
	mov	r0, r6
	ldr	r2, .L1049+4
	uxtb	r5, r5
	str	ip, [sp, #0]
	cmp	r5, r9
	movcs	r5, #0
	mov	r3, r5
	bl	HynixSetRRPara
	mov	r0, r6
	ldr	r1, [sp, #4]
	mov	r2, sl
	mov	r3, r7
	bl	FlashReadRawPage
	ldr	ip, [sp, #0]
	cmn	r0, #1
	beq	.L1041
	ldrb	r3, [ip, #3836]	@ zero_extendqisi2
	cmn	r4, #1
	moveq	r4, r0
	add	r3, r3, r3, asl #1
	cmp	r0, r3, lsr #2
	bcc	.L1048
	mov	r7, #0
	mov	sl, r7
.L1041:
	add	r8, r8, #1
.L1039:
	cmp	r8, r9
	bcc	.L1044
	b	.L1043
.L1048:
	mov	r4, r0
.L1043:
	ldr	r3, .L1049
	adds	r0, r4, #1
	add	r6, r3, r6
	movne	r0, #1
	ldrb	r3, [r3, #3836]	@ zero_extendqisi2
	strb	r5, [r6, #2920]
	add	r3, r3, r3, asl #1
	cmp	r4, r3, lsr #2
	movcc	r0, #0
	andcs	r0, r0, #1
	cmp	r0, #0
	moveq	r0, r4
	movne	r0, #256
	ldmfd	sp!, {r1, r2, r3, r4, r5, r6, r7, r8, r9, sl, fp, pc}
.L1050:
	.align	2
.L1049:
	.word	.LANCHOR0
	.word	.LANCHOR0+2912
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
	.pad #20
	sub	sp, sp, #20
	mov	fp, r2
	mov	r9, r3
	str	r1, [sp, #12]
	mov	sl, #0
	str	r0, [sp, #8]
	bl	NandcWaitFlashReady
	ldr	r1, [sp, #8]
	mvn	r5, #0
	ldr	r3, .L1063
	ldr	ip, .L1063+4
	add	r2, r3, r1, asl #3
	ldrb	r7, [r2, #20]	@ zero_extendqisi2
	ldr	r6, [r2, #16]
	add	r7, r7, #8
	mov	r8, r6
	add	r4, r6, r7, asl #8
	b	.L1062
.L1056:
	mov	r2, #239
	mov	r3, #137
	str	r2, [r4, #8]
	mov	r0, #200
	str	r3, [r4, #4]
	mov	r6, #0
	str	ip, [sp, #4]
	add	sl, sl, #1
	bl	NandcDelayns
	str	sl, [r4, #0]
	ldr	r0, [sp, #8]
	mov	r2, fp
	str	r6, [r4, #0]
	mov	r3, r9
	str	r6, [r4, #0]
	ldr	r1, [sp, #12]
	str	r6, [r4, #0]
	bl	FlashReadRawPage
	ldr	ip, [sp, #4]
	cmn	r0, #1
	beq	.L1062
	ldr	r1, .L1063
	cmn	r5, #1
	moveq	r5, r0
	ldrb	r3, [r1, #3836]	@ zero_extendqisi2
	add	r3, r3, r3, asl #1
	cmp	r0, r3, lsr #2
	bcc	.L1059
	mov	r9, r6
	mov	fp, r6
.L1062:
	ldrb	r3, [ip, #120]	@ zero_extendqisi2
	cmp	sl, r3
	bcc	.L1056
	mov	r6, r8
	b	.L1055
.L1059:
	mov	r6, r8
	mov	r5, r0
.L1055:
	mov	r3, #239
	mov	r0, #200
	str	r3, [r4, #8]
	mov	r3, #137
	str	r3, [r4, #4]
	bl	NandcDelayns
	mov	r3, #0
	str	r3, [r6, r7, asl #8]
	adds	r0, r5, #1
	str	r3, [r6, r7, asl #8]
	str	r3, [r6, r7, asl #8]
	movne	r0, #1
	str	r3, [r6, r7, asl #8]
	ldr	r3, .L1063
	ldrb	r3, [r3, #3836]	@ zero_extendqisi2
	add	r3, r3, r3, asl #1
	cmp	r5, r3, lsr #2
	movcc	r0, #0
	andcs	r0, r0, #1
	cmp	r0, #0
	moveq	r0, r5
	movne	r0, #256
	add	sp, sp, #20
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, sl, fp, pc}
.L1064:
	.align	2
.L1063:
	.word	.LANCHOR0
	.word	.LANCHOR2
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
	ldr	ip, .L1074
	mov	r5, #1
	mvn	r4, #0
	ldr	fp, .L1074+4
	add	r3, ip, r8, asl #3
	ldrb	sl, [r3, #20]	@ zero_extendqisi2
	ldr	r2, [r3, #16]
	add	sl, sl, #8
	add	sl, r2, sl, asl #8
	b	.L1066
.L1070:
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
	beq	.L1067
	ldrb	r3, [ip, #3836]	@ zero_extendqisi2
	cmn	r4, #1
	moveq	r4, r0
	add	r3, r3, r3, asl #1
	cmp	r0, r3, lsr #2
	bcc	.L1073
	mov	r6, #0
	mov	r7, r6
.L1067:
	add	r5, r5, #1
.L1066:
	ldrb	r3, [fp, #120]	@ zero_extendqisi2
	add	r3, r3, #1
	cmp	r5, r3
	bcc	.L1070
	b	.L1069
.L1073:
	mov	r4, r0
.L1069:
	mov	r0, sl
	mov	r1, #0
	bl	SamsungSetRRPara
	ldr	r3, .L1074
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
.L1075:
	.align	2
.L1074:
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
	ldr	r3, .L1097
	add	r2, r3, r8, asl #3
	ldr	r1, [r2, #16]
	ldrb	r4, [r2, #20]	@ zero_extendqisi2
	ldrb	r2, [r3, #3765]	@ zero_extendqisi2
	add	r4, r4, #8
	sub	r2, r2, #67
	add	r4, r1, r4, asl #8
	uxtb	r2, r2
	cmp	r2, #1
	movls	sl, #0
	bls	.L1077
	ldrb	sl, [r3, #3777]	@ zero_extendqisi2
	cmp	sl, #0
	beq	.L1078
	mov	r0, #0
	mov	sl, #1
	bl	NandcSetDdrMode
.L1078:
	mov	r3, #92
	str	r3, [r4, #8]
	mov	r3, #197
	str	r3, [r4, #8]
.L1077:
	mov	r6, #1
	mvn	r5, #0
	ldr	fp, .L1097
	b	.L1079
.L1088:
	ldrb	r3, [fp, #3765]	@ zero_extendqisi2
	mov	r0, r4
	uxtb	r1, r6
	sub	r3, r3, #67
	uxtb	r3, r3
	cmp	r3, #1
	bhi	.L1080
	bl	SandiskSetRRPara
	b	.L1081
.L1080:
	bl	ToshibaSetRRPara
.L1081:
	ldr	r2, .L1097
	ldrb	r3, [r2, #3765]	@ zero_extendqisi2
	cmp	r3, #34
	bne	.L1082
	ldr	r2, .L1097+4
	ldrb	r3, [r2, #120]	@ zero_extendqisi2
	sub	r3, r3, #3
	cmp	r6, r3
	moveq	r3, #179
	streq	r3, [r4, #8]
.L1082:
	cmp	sl, #0
	mov	r3, #38
	str	r3, [r4, #8]
	mov	r3, #93
	str	r3, [r4, #8]
	beq	.L1083
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
	b	.L1084
.L1083:
	mov	r3, r7
	mov	r0, r8
	ldr	r1, [sp, #4]
	mov	r2, r9
	bl	FlashReadRawPage
	mov	r3, r0
.L1084:
	cmn	r3, #1
	beq	.L1085
	ldrb	r2, [fp, #3836]	@ zero_extendqisi2
	cmn	r5, #1
	moveq	r5, r3
	add	r2, r2, r2, asl #1
	cmp	r3, r2, lsr #2
	bcc	.L1096
	mov	r7, #0
	mov	r9, r7
.L1085:
	add	r6, r6, #1
.L1079:
	ldr	r2, .L1097+4
	ldrb	r3, [r2, #120]	@ zero_extendqisi2
	add	r3, r3, #1
	cmp	r6, r3
	bcc	.L1088
	b	.L1087
.L1096:
	mov	r5, r3
.L1087:
	ldr	r6, .L1097
	mov	r0, r4
	mov	r1, #0
	ldrb	r3, [r6, #3765]	@ zero_extendqisi2
	sub	r3, r3, #67
	uxtb	r3, r3
	cmp	r3, #1
	bhi	.L1089
	bl	SandiskSetRRPara
	b	.L1090
.L1089:
	bl	ToshibaSetRRPara
.L1090:
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
	beq	.L1092
	mov	r0, #4
	bl	NandcSetDdrMode
.L1092:
	mov	r0, r5
	ldmfd	sp!, {r1, r2, r3, r4, r5, r6, r7, r8, r9, sl, fp, pc}
.L1098:
	.align	2
.L1097:
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
	ldr	r5, .L1106
	ldr	r7, .L1106+4
	ldr	r8, .L1106+8
	ldr	r3, [r5, #3780]
	ldrb	r0, [r7, #128]	@ zero_extendqisi2
	str	r3, [r7, #124]
	bl	FlashBchSel
	mov	r1, #0
	mov	r2, #2048
	ldr	r0, [r5, #3780]
	bl	ftl_memset
	ldr	r3, [r7, #124]
	ldr	r1, .L1106+12
	mov	r2, #32
	str	r8, [r3, #0]
	ldr	r4, [r7, #124]
	ldrb	r3, [r5, #3766]	@ zero_extendqisi2
	add	r0, r4, #16
	strh	r3, [r4, #12]	@ movhi
	ldrb	r3, [r5, #1]	@ zero_extendqisi2
	strh	r3, [r4, #14]	@ movhi
	ldrb	r3, [r5, #3777]	@ zero_extendqisi2
	str	r3, [r4, #1076]
	bl	memcpy
	add	r0, r4, #80
	ldr	r1, .L1106+16
	mov	r2, #8
	bl	memcpy
	add	r0, r4, #96
	ldr	r1, .L1106+20
	mov	r2, #32
	bl	memcpy
	ldr	r0, [r7, #124]
	ldr	r1, .L1106+24
	mov	r2, #32
	add	r0, r0, #160
	bl	memcpy
	ldr	r4, [r7, #124]
	add	r1, r5, #48
	mov	r2, #32
	add	r0, r4, #192
	bl	memcpy
	mov	r2, #852
	add	r0, r4, #224
	ldr	r1, .L1106+28
	bl	memcpy
	add	r0, r4, #12
	movw	r1, #2036
	bl	JSHash
	movw	r3, #1592
	str	r3, [r4, #4]
	ldr	r3, [r7, #116]
	str	r3, [r7, #124]
	str	r0, [r4, #8]
	mov	r0, #0
	bl	flash_enter_slc_mode
	mov	r4, r6
.L1102:
	ldr	r1, [r5, #4]
	mov	r0, #0
	mov	r2, r0
	mul	r1, r1, r4
	bl	FlashEraseBlock
	ldr	r1, [r5, #4]
	mov	r0, #0
	ldr	r2, [r5, #3780]
	mov	r3, r0
	mul	r1, r1, r4
	bl	FlashProgPage
	ldr	r1, [r5, #4]
	mov	r0, #0
	ldr	r2, [r5, #3780]
	mov	r3, r0
	mul	r1, r1, r4
	add	r1, r1, #1
	bl	FlashProgPage
	ldr	r1, [r5, #4]
	mov	r0, #0
	ldr	r2, [r7, #116]
	mov	r3, r0
	mul	r1, r1, r4
	bl	FlashReadRawPage
	cmn	r0, #1
	beq	.L1100
	ldr	sl, [r7, #124]
	ldr	r3, [sl, #0]
	cmp	r3, r8
	bne	.L1100
	add	r0, sl, #12
	movw	r1, #2036
	bl	JSHash
	ldr	r3, [sl, #8]
	cmp	r3, r0
	bne	.L1100
	add	r3, r4, #1
	str	r3, [r7, #132]
	ldr	r3, [r5, #4]
	add	r6, r6, #1
	cmp	r6, #1
	mul	r3, r3, r4
	str	r3, [r7, #136]
	bhi	.L1101
.L1100:
	add	r4, r4, #1
	cmp	r4, #4
	bne	.L1102
.L1101:
	mov	r0, #0
	bl	flash_exit_slc_mode
	cmp	r6, #0
	mvneq	r0, #0
	movne	r0, #0
	ldmfd	sp!, {r3, r4, r5, r6, r7, r8, sl, pc}
.L1107:
	.align	2
.L1106:
	.word	.LANCHOR0
	.word	.LANCHOR2
	.word	1312902724
	.word	.LANCHOR0+2132
	.word	.LANCHOR0+3768
	.word	.LANCHOR0+2740
	.word	.LANCHOR1+2688
	.word	.LANCHOR0+2908
	.fnend
	.size	FlashSavePhyInfo, .-FlashSavePhyInfo
	.align	2
	.global	FlashReadIdbDataRaw
	.type	FlashReadIdbDataRaw, %function
FlashReadIdbDataRaw:
	.fnstart
	@ args = 0, pretend = 0, frame = 8
	@ frame_needed = 0, uses_anonymous_args = 0
	stmfd	sp!, {r0, r1, r2, r4, r5, r6, r7, r8, r9, sl, fp, lr}
	.save {r0, r1, r2, r4, r5, r6, r7, r8, r9, sl, fp, lr}
	mov	r2, #4
	mov	sl, r0
	ldr	r1, .L1119
	add	r0, sp, #4
	bl	memcpy
	ldr	r3, .L1119+4
	ldr	r2, [r3, #3788]
	ldrb	fp, [r3, #3836]	@ zero_extendqisi2
	ldr	r3, .L1119+8
	cmp	r2, r3
	bne	.L1109
	mov	r0, #0
	bl	flash_enter_slc_mode
.L1109:
	ldr	r7, .L1119+4
	mov	r0, sl
	mov	r1, #0
	mov	r2, #2048
	bl	ftl_memset
	mvn	r8, #0
	mov	r4, #2
	mov	r6, r7
	ldr	r9, .L1119+12
	b	.L1110
.L1117:
	mov	r5, #0
.L1112:
	add	r3, sp, #4
	ldrb	r0, [r3, r5]	@ zero_extendqisi2
	bl	FlashBchSel
	ldr	r1, [r7, #4]
	mov	r0, #0
	ldr	r2, [r7, #3780]
	mov	r3, r0
	mul	r1, r1, r4
	bl	FlashReadRawPage
	cmn	r0, #1
	bne	.L1111
	add	r5, r5, #1
	cmp	r5, #4
	bne	.L1112
	b	.L1113
.L1111:
	ldr	r3, [r6, #3780]
	ldr	r3, [r3, #0]
	cmp	r3, r9
	bne	.L1113
	add	r3, sp, #8
	ldr	r0, .L1119+16
	add	r5, r3, r5
	ldrb	r1, [r5, #-4]	@ zero_extendqisi2
	bl	printk
	mov	r2, #2048
	mov	r0, sl
	ldr	r1, [r6, #3780]
	bl	memcpy
	ldr	r3, [r6, #3780]
	ldr	r3, [r3, #512]
	strb	r3, [r6, #1]
	ldr	r3, .L1119+20
	ldr	r2, [r3, #132]
	cmp	r2, r4
	bls	.L1116
	str	r4, [r3, #132]
	mov	r8, #0
	bl	FlashSavePhyInfo
.L1113:
	add	r4, r4, #1
.L1110:
	ldrb	r3, [r7, #1]	@ zero_extendqisi2
	cmp	r4, r3
	bcc	.L1117
	b	.L1114
.L1116:
	mov	r8, #0
.L1114:
	mov	r0, fp
	bl	FlashBchSel
	ldr	r3, .L1119+4
	ldr	r2, [r3, #3788]
	ldr	r3, .L1119+8
	cmp	r2, r3
	bne	.L1115
	mov	r0, #0
	bl	flash_exit_slc_mode
.L1115:
	mov	r0, r8
	ldmfd	sp!, {r1, r2, r3, r4, r5, r6, r7, r8, r9, sl, fp, pc}
.L1120:
	.align	2
.L1119:
	.word	.LANCHOR3+11
	.word	.LANCHOR0
	.word	1446522928
	.word	-52655045
	.word	.LC87
	.word	.LANCHOR2
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
	ldr	r1, .L1131
	add	r0, sp, #4
	bl	memcpy
	ldr	r7, .L1131+4
	ldr	r2, .L1131+8
	movw	r3, #2698
	ldr	r5, .L1131+12
	mov	r4, #0
	mov	r0, r4
	mov	r8, #4
	ldrh	fp, [r2, r3]
	mvn	sl, #0
	ldr	r3, [r7, #3780]
	str	r4, [r5, #136]
	str	r3, [r5, #124]
	bl	flash_enter_slc_mode
	b	.L1122
.L1124:
	add	r3, sp, #4
	ldrb	r0, [r3, r6]	@ zero_extendqisi2
	bl	FlashBchSel
	mov	r0, #0
	mov	r1, r4
	ldr	r2, [r7, #3780]
	mov	r3, r0
	bl	FlashReadRawPage
	cmn	r0, #1
	bne	.L1123
	mov	r0, #0
	mov	r1, r9
	ldr	r2, [r7, #3780]
	mov	r3, r0
	bl	FlashReadRawPage
	cmn	r0, #1
	bne	.L1123
	add	r6, r6, #1
	cmp	r6, #4
	beq	.L1125
	b	.L1124
.L1123:
	ldr	r6, [r5, #124]
	ldr	r3, .L1131+16
	ldr	r2, [r6, #0]
	cmp	r2, r3
	bne	.L1125
	cmp	sl, #0
	bne	.L1126
	ldr	r2, .L1131+8
	movw	r3, #2698
	mov	r0, r4
	ldrh	r1, [r2, r3]
	bl	__aeabi_uidiv
	ldr	r3, .L1131+12
	add	r0, r0, #1
	str	r0, [r3, #132]
	b	.L1127
.L1126:
	add	r0, r6, #12
	movw	r1, #2036
	bl	JSHash
	ldr	r3, [r6, #8]
	cmp	r3, r0
	bne	.L1125
	ldr	sl, .L1131+8
	add	r1, r6, #160
	mov	r2, #32
	add	r0, sl, #2688
	bl	memcpy
	add	r1, r6, #192
	mov	r2, #32
	ldr	r0, .L1131+20
	bl	memcpy
	add	r1, r6, #224
	mov	r2, #852
	ldr	r0, .L1131+24
	bl	memcpy
	ldr	r3, [r6, #1076]
	mov	r0, r4
	str	r4, [r5, #136]
	strb	r3, [r7, #3777]
	movw	r3, #2698
	ldrh	r1, [sl, r3]
	mov	sl, #0
	bl	__aeabi_uidiv
	add	r0, r0, #1
	str	r0, [r5, #132]
	cmp	r0, #1
	moveq	r3, #2
	streq	r3, [r5, #132]
	ldrh	r3, [r6, #14]
	strb	r3, [r5, #140]
.L1125:
	subs	r8, r8, #1
	add	r4, r4, fp
	beq	.L1129
.L1122:
	add	r9, r4, #1
	mov	r6, #0
	b	.L1124
.L1129:
	mov	r0, r8
	bl	flash_exit_slc_mode
.L1127:
	mov	r0, sl
	ldmfd	sp!, {r1, r2, r3, r4, r5, r6, r7, r8, r9, sl, fp, pc}
.L1132:
	.align	2
.L1131:
	.word	.LANCHOR3+11
	.word	.LANCHOR0
	.word	.LANCHOR1
	.word	.LANCHOR2
	.word	1312902724
	.word	.LANCHOR0+48
	.word	.LANCHOR0+2908
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
	ldr	r4, .L1154
	.pad #20
	sub	sp, sp, #20
	mov	fp, r0
	mov	r9, r2
	str	r1, [sp, #0]
	ldr	r3, [r4, #2164]
	ldr	r7, [r4, #12]
	ldr	r3, [r3, #304]
	cmp	r7, #7
	movhi	r7, #12
	movls	r7, #6
	str	r3, [sp, #12]
	ldr	r3, [sp, #56]
	cmp	r3, #0
	moveq	r8, #1024
	beq	.L1135
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
	ldrb	r0, [r4, #3776]	@ zero_extendqisi2
	bl	FlashSetInterfaceMode
	ldrb	r0, [r4, #3776]	@ zero_extendqisi2
	bl	NandcSetMode
	cmn	r8, #1
	beq	.L1136
	ldr	r0, .L1154+4
	mov	r2, r8
	ldr	r1, [sp, #0]
	bl	printk
	cmp	r8, #9
	bhi	.L1137
	add	r4, r4, fp, asl #3
	ldr	r3, [r4, #16]
	ldr	r2, [r3, #3840]
	ldr	r2, [r3, #0]
	orr	r2, r2, #131072
	str	r2, [r3, #0]
.L1137:
	ldr	r3, .L1154+8
	ldr	r2, [r3, #144]
	add	r2, r2, #1
	str	r2, [r3, #144]
	cmp	r2, #2048
	movcs	sl, #0
	strcs	sl, [r3, #144]
	movcs	r9, sl
	bcc	.L1136
.L1135:
	mov	r4, #0
	str	fp, [sp, #4]
	mov	r5, r4
	mvn	r6, #0
	mov	fp, r4
	str	r4, [sp, #8]
.L1142:
	uxtb	r0, r7
	bl	NandcSetDdrPara
	mov	r3, sl
	ldr	r0, [sp, #4]
	mov	r2, r9
	ldr	r1, [sp, #0]
	bl	FlashReadRawPage
	add	r3, r8, #1
	cmp	r0, r3
	bhi	.L1138
	cmp	r0, #2
	bhi	.L1148
	add	r5, r5, #1
	cmp	r5, #9
	bls	.L1148
	mov	ip, fp
	rsb	r4, r5, r7
	ldr	fp, [sp, #4]
	mov	r8, r0
	mov	r6, #0
	b	.L1140
.L1138:
	cmp	fp, r5
	bcs	.L1149
	cmp	r5, #7
	rsb	r3, r5, r4
	str	r3, [sp, #8]
	bhi	.L1153
	mov	fp, r5
	b	.L1149
.L1148:
	mov	r6, #0
	mov	r4, r7
	mov	r8, r0
	mov	sl, r6
	mov	r9, r6
	b	.L1139
.L1149:
	mov	r5, #0
.L1139:
	add	r7, r7, #2
	cmp	r7, #69
	bls	.L1142
	mov	ip, fp
	ldr	fp, [sp, #4]
.L1140:
	cmp	ip, r5
	bcc	.L1143
	b	.L1141
.L1153:
	ldr	fp, [sp, #4]
.L1141:
	ldr	r4, [sp, #8]
.L1143:
	cmp	r4, #0
	beq	.L1144
	ldr	r0, .L1154+12
	mov	r1, r4
	bl	printk
	uxtb	r0, r4
	bl	NandcSetDdrPara
.L1144:
	cmn	r6, #1
	bne	.L1136
	ldr	r0, .L1154+16
	mov	r1, fp
	ldr	r2, [sp, #0]
	bl	printk
	ldr	r3, [sp, #56]
	cmp	r3, #0
	moveq	r8, r6
	beq	.L1136
	ldr	r3, [sp, #12]
	ubfx	r0, r3, #8, #8
	bl	NandcSetDdrPara
.L1136:
	mov	r0, r8
	add	sp, sp, #20
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, sl, fp, pc}
.L1155:
	.align	2
.L1154:
	.word	.LANCHOR0
	.word	.LC88
	.word	.LANCHOR2
	.word	.LC89
	.word	.LC90
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
	ldr	r5, .L1160
	mov	r4, #0
	mov	r6, r1
	ldrb	r0, [r5, #3776]	@ zero_extendqisi2
	bl	FlashSetInterfaceMode
	ldrb	r0, [r5, #3776]	@ zero_extendqisi2
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
	beq	.L1157
	cmn	r8, #1
	bne	.L1158
.L1157:
	ldrb	r3, [r5, #3776]	@ zero_extendqisi2
	tst	r3, #1
	beq	.L1158
	mov	r0, #1
	bl	FlashSetInterfaceMode
	mov	r0, #1
	bl	NandcSetMode
	ldr	r3, .L1160
	mov	r2, #0
	strb	r2, [r3, #3777]
	b	.L1159
.L1158:
	mov	r3, #1
	strb	r3, [r5, #3777]
.L1159:
	mov	r0, #0
	ldmfd	sp!, {r2, r3, r4, r5, r6, r7, r8, pc}
.L1161:
	.align	2
.L1160:
	.word	.LANCHOR0
	.fnend
	.size	FlashDdrParaScan, .-FlashDdrParaScan
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
	ldr	r6, .L1206
	bl	ftl_malloc
	ldr	r5, .L1206+4
	mov	r4, #0
	str	r0, [r6, #3780]
	mov	r0, #32768
	bl	ftl_malloc
	str	r0, [r5, #116]
	mov	r0, #4096
	bl	ftl_malloc
	str	r0, [r5, #148]
	mov	r0, #32768
	bl	ftl_malloc
	str	r0, [r5, #152]
	mov	r0, #4096
	bl	ftl_malloc
	mov	r3, #50
	str	r4, [r5, #132]
	strb	r3, [r5, #140]
	strb	r3, [r6, #1]
	mov	r3, #128
	str	r4, [r5, #144]
	str	r3, [r6, #4]
	mov	r3, #60
	strb	r4, [r6, #3777]
	strb	r3, [r5, #128]
	strb	r4, [r6, #0]
	str	r0, [r5, #156]
	mov	r0, r7
	bl	NandcInit
	ldr	r5, .L1206+8
.L1165:
	uxtb	r0, r4
	mov	r1, r5
	bl	FlashReadIDRaw
	cmp	r4, #0
	bne	.L1163
	ldrb	r3, [r6, #2132]	@ zero_extendqisi2
	sub	r3, r3, #1
	uxtb	r3, r3
	cmp	r3, #253
	bhi	.L1199
	ldrb	r3, [r6, #2133]	@ zero_extendqisi2
	cmp	r3, #255
	beq	.L1200
.L1163:
	add	r4, r4, #1
	add	r5, r5, #8
	cmp	r4, #4
	bne	.L1165
	ldr	r4, .L1206
	ldrb	r3, [r4, #2132]	@ zero_extendqisi2
	cmp	r3, #173
	beq	.L1166
	ldr	r0, [r4, #3784]
	bl	NandcSetDdrMode
.L1166:
	mov	r1, #0
	ldr	r0, .L1206+12
	mov	r2, #852
	bl	ftl_memset
	ldr	r3, .L1206+16
	str	r3, [r4, #2776]
	mov	r3, #0
	strb	r3, [r4, #8]
	ldrb	r3, [r4, #2133]	@ zero_extendqisi2
	sub	r1, r3, #218
	rsbs	ip, r1, #0
	adc	ip, ip, r1
	cmp	r3, #218
	cmpne	r3, #241
	beq	.L1167
	cmp	r3, #220
	bne	.L1168
	ldr	r2, .L1206
	ldrb	r2, [r2, #2135]	@ zero_extendqisi2
	cmp	r2, #149
	bne	.L1168
.L1167:
	ldr	r2, .L1206
	mov	r1, #1
	mov	r0, #16
	strb	r1, [r2, #0]
	ldr	r1, .L1206+4
	strb	r0, [r2, #1]
	strb	r0, [r1, #128]
	ldrb	r0, [r2, #2132]	@ zero_extendqisi2
	ldr	r2, .L1206+20
	cmp	r0, #152
	strb	r0, [r2, #2969]
	moveq	r0, #24
	streqb	r0, [r1, #128]
	cmp	ip, #0
	movwne	r3, #2982
	movne	r1, #2048
	strneh	r1, [r2, r3]	@ movhi
	mvnne	r3, #37
	bne	.L1203
.L1170:
	cmp	r3, #220
	bne	.L1171
	movw	r3, #2982
	mov	r1, #4096
	strh	r1, [r2, r3]	@ movhi
	mvn	r3, #35
.L1203:
	strb	r3, [r2, #2970]
.L1171:
	ldr	r1, .L1206+24
	mov	r2, #32
	ldr	r0, .L1206+28
	bl	memcpy
	ldr	r0, .L1206+16
	mov	r2, #32
	add	r1, r0, #280
	bl	memcpy
.L1168:
	ldr	r4, .L1206
	ldrb	r3, [r4, #0]	@ zero_extendqisi2
	cmp	r3, #0
	bne	.L1172
	bl	FlashLoadPhyInfoInRam
	cmp	r0, #0
	bne	.L1173
	ldr	r3, [r4, #2776]
	ldrh	r0, [r3, #16]
	ubfx	r0, r0, #8, #3
	strb	r0, [r4, #3776]
	tst	r0, #1
	bne	.L1173
	mov	r3, #1
	strb	r3, [r4, #3777]
	bl	FlashSetInterfaceMode
	ldrb	r0, [r4, #3776]	@ zero_extendqisi2
	bl	NandcSetMode
.L1173:
	ldr	r4, .L1206
	ldr	r3, [r4, #2776]
	ldrb	r3, [r3, #26]	@ zero_extendqisi2
	strb	r3, [r4, #80]
	bl	FlashLoadPhyInfo
	cmp	r0, #0
	beq	.L1172
	ldr	r3, [r4, #3784]
	cmp	r3, #0
	beq	.L1175
	mov	r0, #1
	bl	FlashSetInterfaceMode
	mov	r0, #1
	b	.L1204
.L1175:
	ldrb	r0, [r4, #3776]	@ zero_extendqisi2
	bl	FlashSetInterfaceMode
	ldrb	r0, [r4, #3776]	@ zero_extendqisi2
.L1204:
	bl	NandcSetMode
	bl	FlashLoadPhyInfo
	cmp	r0, #0
	beq	.L1172
	ldr	r4, .L1206
	mov	r0, #1
	bl	FlashSetInterfaceMode
	mov	r0, #1
	bl	NandcSetMode
	ldr	r0, .L1206+32
	ldr	r3, [r4, #2776]
	ldrh	r1, [r3, #14]
	bl	printk
	bl	FlashLoadPhyInfoInRam
	cmn	r0, #1
	ldmeqfd	sp!, {r3, r4, r5, r6, r7, pc}
	bl	FlashDieInfoInit
	ldr	r3, [r4, #2776]
	ldrb	r0, [r3, #19]	@ zero_extendqisi2
	bl	FlashGetReadRetryDefault
	ldr	r1, .L1206+36
	ldr	r0, .L1206+4
	ldr	r3, [r4, #2776]
	ldrh	r1, [r0, r1]
	ldrb	r2, [r3, #9]	@ zero_extendqisi2
	add	r1, r1, #4080
	add	r1, r1, #15
	cmp	r2, r1, lsr #12
	blt	.L1178
	ldrh	r1, [r3, #14]
	add	r1, r1, #255
	cmp	r2, r1, lsr #8
	bge	.L1179
.L1178:
	ldrh	r2, [r3, #14]
	bic	r2, r2, #255
	strh	r2, [r3, #14]	@ movhi
.L1179:
	ldr	r3, .L1206
	ldrb	r3, [r3, #3776]	@ zero_extendqisi2
	tst	r3, #6
	beq	.L1180
	bl	FlashSavePhyInfo
	ldr	r3, .L1206+4
	mov	r0, #0
	ldr	r1, [r3, #136]
	bl	FlashDdrParaScan
.L1180:
	bl	FlashSavePhyInfo
.L1172:
	ldr	r2, .L1206
	ldr	r0, [r2, #2776]
	ldrb	r3, [r0, #26]	@ zero_extendqisi2
	strb	r3, [r2, #80]
	ldrh	r3, [r0, #16]
	ubfx	r1, r3, #7, #1
	strb	r1, [r2, #8]
	ldr	r1, .L1206+4
	tst	r3, #64
	ubfx	ip, r3, #3, #1
	strb	ip, [r1, #160]
	ubfx	ip, r3, #4, #1
	strb	ip, [r2, #3837]
	ubfx	ip, r3, #8, #3
	strb	ip, [r2, #3776]
	mov	ip, #0
	str	ip, [r1, #164]
	beq	.L1181
	ldrb	ip, [r2, #2909]	@ zero_extendqisi2
	ldrb	r3, [r0, #19]	@ zero_extendqisi2
	strb	ip, [r2, #3764]
	ldrb	ip, [r2, #2910]	@ zero_extendqisi2
	strb	r3, [r2, #3765]
	strb	ip, [r1, #120]
	sub	ip, r3, #1
	uxtb	ip, ip
	cmp	ip, #5
	bhi	.L1182
	sub	r3, r3, #5
	ldr	ip, .L1206+40
	uxtb	r3, r3
	cmp	r3, #1
	str	ip, [r1, #164]
	movls	r3, #1
	strls	r3, [r2, #3828]
	b	.L1181
.L1182:
	sub	r2, r3, #17
	uxtb	r2, r2
	cmp	r2, #2
	bhi	.L1183
	ldr	r2, .L1206+44
	cmp	r3, #19
	moveq	r3, #15
	str	r2, [r1, #164]
	mov	r2, #7
	strb	r2, [r1, #120]
	streqb	r3, [r1, #120]
	b	.L1181
.L1183:
	cmp	r3, #33
	cmpne	r3, #65
	beq	.L1184
	cmp	r3, #66
	bne	.L1185
.L1184:
	ldr	r3, .L1206+4
	mov	r1, #4
	ldr	r2, .L1206+48
	str	r2, [r3, #164]
	ldr	r2, .L1206
	strb	r1, [r2, #3764]
	mov	r2, #7
	strb	r2, [r3, #120]
	b	.L1181
.L1185:
	cmp	r3, #67
	cmpne	r3, #34
	beq	.L1186
	cmp	r3, #35
	beq	.L1186
	cmp	r3, #68
	bne	.L1187
.L1186:
	ldr	r2, .L1206+4
	cmp	r3, #35
	cmpne	r3, #68
	ldr	r1, .L1206+48
	sub	r3, r3, #67
	uxtb	r3, r3
	str	r1, [r2, #164]
	mov	r1, #7
	strb	r1, [r2, #120]
	moveq	r1, #17
	streqb	r1, [r2, #120]
	cmp	r3, #1
	ldr	r3, .L1206
	movls	r2, #4
	movhi	r2, #5
	strb	r2, [r3, #3764]
	b	.L1181
.L1187:
	cmp	r3, #49
	ldreq	r3, .L1206+52
	streq	r3, [r1, #164]
.L1181:
	ldr	r5, .L1206
	ldr	r3, .L1206+56
	ldr	r2, [r5, #3788]
	cmp	r2, r3
	bne	.L1190
	ldrb	r3, [r5, #80]	@ zero_extendqisi2
	cmp	r3, #0
	movne	r3, #0
	strneb	r3, [r0, #18]
.L1190:
	ldr	r3, [r5, #2776]
	ldr	r4, .L1206
	ldrb	r1, [r3, #12]	@ zero_extendqisi2
	ldrh	r0, [r3, #10]
	ldrb	r6, [r3, #18]	@ zero_extendqisi2
	bl	__aeabi_idiv
	mov	r1, r0
	mov	r0, r6
	bl	BuildFlashLsbPageTable
	bl	FlashDieInfoInit
	ldrb	r3, [r5, #2132]	@ zero_extendqisi2
	cmp	r3, #44
	bne	.L1191
	ldrb	r3, [r4, #3777]	@ zero_extendqisi2
	cmp	r3, #0
	beq	.L1191
	ldr	r2, [r4, #3788]
	ldr	r3, .L1206+56
	cmp	r2, r3
	bne	.L1192
	ldrb	r3, [r4, #80]	@ zero_extendqisi2
	cmp	r3, #0
	bne	.L1191
.L1192:
	ldr	r3, .L1206
	mov	r2, #0
	mov	r0, #1
	strb	r2, [r3, #3777]
	bl	FlashSetInterfaceMode
	mov	r0, #1
	bl	NandcSetMode
.L1191:
	mov	r0, #0
	bl	flash_enter_slc_mode
	ldr	r2, .L1206
	ldrb	r3, [r2, #3776]	@ zero_extendqisi2
	tst	r3, #6
	beq	.L1193
	ldrb	r2, [r2, #3777]	@ zero_extendqisi2
	cmp	r2, #0
	bne	.L1194
	tst	r3, #1
	bne	.L1193
.L1194:
	ldr	r3, .L1206+4
	mov	r0, #0
	ldr	r1, [r3, #136]
	bl	FlashDdrParaScan
.L1193:
	ldr	r4, .L1206
	mov	r0, #0
	bl	flash_exit_slc_mode
	ldr	r3, [r4, #2776]
	ldrb	r0, [r3, #20]	@ zero_extendqisi2
	bl	FlashBchSel
	add	r0, r4, #84
	bl	FlashReadIdbDataRaw
	mov	r3, r4
	cmp	r0, #0
	movne	r3, #16
	strneb	r3, [r4, #1]
	bne	.L1197
	ldr	r2, .L1206+4
	ldrb	r1, [r3, #1]	@ zero_extendqisi2
	ldrb	r2, [r2, #140]	@ zero_extendqisi2
	cmp	r1, r2
	strhib	r2, [r3, #1]
	ldrb	r3, [r3, #1]	@ zero_extendqisi2
	cmp	r3, #15
	ldrls	r3, .L1206
	movls	r2, #16
	strlsb	r2, [r3, #1]
.L1197:
	ldr	r4, .L1206
	movw	r6, #2214
	ldr	r0, .L1206+60
	bl	FlashTimingCfg
	mov	r3, #2208
	ldr	r5, [r4, #2776]
	ldrb	r2, [r5, #12]	@ zero_extendqisi2
	ldrh	r7, [r5, #14]
	strh	r2, [r4, r3]	@ movhi
	ldrb	r3, [r5, #7]	@ zero_extendqisi2
	ldrb	r2, [r4, #3766]	@ zero_extendqisi2
	str	r3, [r4, #2204]
	ldr	r3, [r4, #2132]
	str	r3, [r4, #2200]
	movw	r3, #2210
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
	movw	r3, #2218
	mov	lr, #512
	movw	r2, #2222
	strh	r0, [r4, r3]	@ movhi
	movw	r0, #2220
	ldrb	ip, [r5, #9]	@ zero_extendqisi2
	strh	ip, [r4, r0]	@ movhi
	ldrb	r3, [r5, #9]	@ zero_extendqisi2
	ldrh	r1, [r5, #10]
	mul	r1, r1, r3
	mov	r3, #2224
	strh	lr, [r4, r3]	@ movhi
	add	r3, r3, #2
	ldrb	lr, [r4, #1]	@ zero_extendqisi2
	strh	lr, [r4, r3]	@ movhi
	uxth	r1, r1
	ldrb	lr, [r4, #0]	@ zero_extendqisi2
	strh	r1, [r4, r2]	@ movhi
	cmp	lr, #1
	bne	.L1198
	mov	lr, #16
	mov	r7, r7, lsr #1
	mov	ip, ip, asl #1
	mov	r1, r1, asl #1
	strb	lr, [r4, #1]
	strh	r1, [r4, r2]	@ movhi
	mov	r2, #8
	strh	r7, [r4, r6]	@ movhi
	strh	ip, [r4, r0]	@ movhi
	strh	r2, [r4, r3]	@ movhi
.L1198:
	ldrb	r0, [r5, #20]	@ zero_extendqisi2
	bl	FlashBchSel
	bl	FlashSuspend
	mov	r0, #0
	ldmfd	sp!, {r3, r4, r5, r6, r7, pc}
.L1199:
	mvn	r0, #1
	ldmfd	sp!, {r3, r4, r5, r6, r7, pc}
.L1200:
	mvn	r0, #1
	ldmfd	sp!, {r3, r4, r5, r6, r7, pc}
.L1207:
	.align	2
.L1206:
	.word	.LANCHOR0
	.word	.LANCHOR2
	.word	.LANCHOR0+2132
	.word	.LANCHOR0+2908
	.word	.LANCHOR1+2688
	.word	.LANCHOR1
	.word	.LANCHOR1+2592
	.word	.LANCHOR0+48
	.word	.LC91
	.word	-914
	.word	HynixReadRetrial
	.word	MicronReadRetrial
	.word	ToshibaReadRetrial
	.word	SamsungReadRetrial
	.word	1446522928
	.word	150000
	.fnend
	.size	FlashInit, .-FlashInit
	.align	2
	.global	FlashReadPage
	.type	FlashReadPage, %function
FlashReadPage:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	stmfd	sp!, {r0, r1, r4, r5, r6, r7, r8, r9, sl, lr}
	.save {r0, r1, r4, r5, r6, r7, r8, r9, sl, lr}
	mov	r6, r0
	mov	r5, r1
	mov	r8, r2
	mov	sl, r3
	bl	FlashReadRawPage
	cmn	r0, #1
	mov	r4, r0
	bne	.L1209
	ldr	r7, .L1214
	ldrb	r9, [r7, #8]	@ zero_extendqisi2
	cmp	r9, #0
	beq	.L1210
	mov	r3, #0
	mov	r0, r6
	strb	r3, [r7, #8]
	mov	r1, r5
	mov	r2, r8
	mov	r3, sl
	bl	FlashReadRawPage
	strb	r9, [r7, #8]
	cmn	r0, #1
	movne	r4, r0
	bne	.L1209
.L1210:
	ldr	r7, .L1214
	ldrb	r3, [r7, #3777]	@ zero_extendqisi2
	cmp	r3, #0
	beq	.L1209
	ldr	r3, [r7, #2164]
	mov	r0, r6
	mov	r1, r5
	mov	r2, r8
	ldr	r9, [r3, #304]
	mov	r3, #1
	str	r3, [sp, #0]
	mov	r3, sl
	bl	FlashDdrTunningRead
	cmn	r0, #1
	mov	r4, r0
	beq	.L1211
	ldrb	r3, [r7, #3836]	@ zero_extendqisi2
	cmp	r0, r3, lsr #1
	bls	.L1209
.L1211:
	ubfx	r0, r9, #8, #8
	bl	NandcSetDdrPara
.L1209:
	ldr	r3, .L1214+4
	ldr	ip, [r3, #164]
	adds	r3, ip, #0
	movne	r3, #1
	cmn	r4, #1
	movne	r3, #0
	cmp	r3, #0
	beq	.L1212
	mov	r1, r5
	mov	r2, r8
	mov	r3, sl
	mov	r0, r6
	blx	ip
	mov	r2, r6
	mov	r3, r5
	mov	r4, r0
	ldr	r0, .L1214+8
	mov	r1, r4
	bl	printk
.L1212:
	mov	r0, r4
	ldmfd	sp!, {r2, r3, r4, r5, r6, r7, r8, r9, sl, pc}
.L1215:
	.align	2
.L1214:
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
	ldr	r3, .L1229
	stmfd	sp!, {r4, r5, r6, r7, r8, r9, sl, fp, lr}
	.save {r4, r5, r6, r7, r8, r9, sl, fp, lr}
	mov	r8, r1
	.pad #28
	sub	sp, sp, #28
	mov	fp, r2
	ldrb	sl, [r3, #2697]	@ zero_extendqisi2
	mov	r4, r0
	mov	r7, #0
	ldr	r6, .L1229+4
	b	.L1217
.L1226:
	rsb	r3, r7, r8
	add	r2, sp, #20
	mov	r0, r4
	mov	r1, fp
	uxtb	r3, r3
	str	r3, [sp, #0]
	add	r3, sp, #16
	bl	LogAddr2PhyAddr
	ldr	r3, [sp, #16]
	ldrb	r2, [r6, #3766]	@ zero_extendqisi2
	cmp	r3, r2
	mvncs	r3, #0
	strcs	r3, [r4, #0]
	bcs	.L1219
	add	r3, r6, r3
	ldrb	r5, [r3, #3768]	@ zero_extendqisi2
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
	ldr	r1, [r6, #4]
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
	beq	.L1224
	ldr	r2, [r3, #8]
	cmn	r2, #1
	bne	.L1224
	ldr	r3, [r3, #0]
	cmn	r3, #1
	strne	r2, [r4, #0]
.L1224:
	ldr	r3, [r4, #0]
	cmn	r3, #1
	bne	.L1219
	ldr	r1, [r4, #4]
	ldr	r0, .L1229+8
	ldrb	r2, [r6, #3836]	@ zero_extendqisi2
	bl	printk
	ldr	r1, [r4, #8]
	cmp	r1, #0
	beq	.L1225
	ldr	r0, .L1229+12
	mov	r2, #4
	mov	r3, #8
	bl	rknand_print_hex
.L1225:
	ldr	r1, [r4, #12]
	cmp	r1, #0
	beq	.L1219
	mov	r2, #4
	ldr	r0, .L1229+16
	mov	r3, r2
	bl	rknand_print_hex
.L1219:
	add	r7, r7, #1
	add	r4, r4, #36
.L1217:
	cmp	r7, r8
	bne	.L1226
	mov	r0, #0
	add	sp, sp, #28
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, sl, fp, pc}
.L1230:
	.align	2
.L1229:
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
	ldr	r3, .L1267
	stmfd	sp!, {r4, r5, r6, r7, r8, r9, sl, fp, lr}
	.save {r4, r5, r6, r7, r8, r9, sl, fp, lr}
	.pad #52
	sub	sp, sp, #52
	ldrb	r3, [r3, #2697]	@ zero_extendqisi2
	mov	r9, r0
	str	r1, [sp, #28]
	str	r2, [sp, #32]
	str	r3, [sp, #20]
	ldr	r3, .L1267+4
	ldrb	r8, [r3, #0]	@ zero_extendqisi2
	ldrb	ip, [r3, #8]	@ zero_extendqisi2
	cmp	r8, #0
	str	ip, [sp, #36]
	moveq	sl, r8
	beq	.L1232
	bl	FlashReadSlc2KPages
	b	.L1233
.L1256:
	mov	r3, #36
	ldr	ip, [sp, #28]
	mul	r3, r3, sl
	add	r2, sp, #44
	ldr	r1, [sp, #32]
	ldr	r5, .L1267+4
	add	r6, r9, r3
	str	r3, [sp, #16]
	rsb	r3, sl, ip
	ldr	fp, [r6, #4]
	mov	r0, r6
	uxtb	r3, r3
	str	r3, [sp, #0]
	add	r3, sp, #40
	bl	LogAddr2PhyAddr
	ldr	r3, [sp, #40]
	ldrb	r2, [r5, #3766]	@ zero_extendqisi2
	cmp	r3, r2
	ldrcs	ip, [sp, #16]
	mvncs	r3, #0
	mov	r7, r0
	strcs	r3, [r9, ip]
	bcs	.L1235
	add	r3, r5, r3
	ldrb	r4, [r3, #3768]	@ zero_extendqisi2
	ldr	r3, .L1267+8
	mov	r0, r4
	ldrb	r3, [r3, #160]	@ zero_extendqisi2
	cmp	r3, #0
	moveq	r7, #0
	bl	NandcWaitFlashReady
	ldr	r3, [r5, #2776]
	ldrb	r3, [r3, #19]	@ zero_extendqisi2
	sub	r3, r3, #1
	uxtb	r3, r3
	cmp	r3, #5
	bhi	.L1237
	add	r2, r5, r4
	ldrb	r3, [r2, #2920]	@ zero_extendqisi2
	ldrb	r2, [r2, #3760]	@ zero_extendqisi2
	cmp	r2, r3
	beq	.L1237
	mov	r0, r4
	ldrb	r1, [r5, #2909]	@ zero_extendqisi2
	add	r2, r5, #2912
	bl	HynixSetRRPara
.L1237:
	mov	r0, r4
	mov	fp, fp, lsr #31
	bl	NandcFlashCs
	ldr	ip, [sp, #32]
	cmp	ip, #1
	orreq	fp, fp, #1
	str	fp, [sp, #24]
	cmp	fp, #0
	beq	.L1243
	ldr	r3, .L1267+4
	ldrb	r3, [r3, #80]	@ zero_extendqisi2
	cmp	r3, #0
	beq	.L1243
	mov	r0, r4
	bl	flash_enter_slc_mode
.L1243:
	ldr	fp, .L1267+4
.L1266:
	ldr	r1, [sp, #44]
	cmn	r1, #1
	cmpeq	r4, #255
	moveq	r3, #0
	movne	r3, #1
	moveq	r7, r3
	beq	.L1239
	cmp	r7, #0
	beq	.L1240
	ldr	r2, [fp, #4]
	mov	r0, r4
	add	r2, r1, r2
	bl	FlashReadDpCmd
	b	.L1241
.L1240:
	mov	r0, r4
	bl	FlashReadCmd
.L1241:
	mov	r0, r4
	bl	NandcWaitFlashReady
	cmp	r7, #0
	beq	.L1239
	mov	r0, r4
	ldr	r1, [sp, #44]
	bl	FlashReadDpDataOutCmd
.L1239:
	ldr	r3, [r6, #12]
	mov	r0, r4
	ldr	r2, [sp, #20]
	mov	r1, #0
	str	r3, [sp, #0]
	ldr	r3, [r6, #8]
	bl	NandcXferData
	ldrb	r2, [fp, #8]	@ zero_extendqisi2
	adds	r2, r2, #0
	movne	r2, #1
	cmn	r0, #1
	movne	r2, #0
	cmp	r2, #0
	movne	r2, #0
	movne	r7, #0
	strneb	r2, [fp, #8]
	bne	.L1266
.L1242:
	cmp	r7, #0
	mov	r5, r0
	beq	.L1244
	ldr	r3, .L1267+4
	mov	r0, r4
	ldr	r1, [r3, #4]
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
	ldr	r2, [sp, #20]
	bl	NandcXferData
	cmn	r0, #1
	mov	r8, r0
	moveq	r7, #0
.L1244:
	mov	r0, r4
	ldr	fp, .L1267+4
	bl	NandcFlashDeCs
	ldr	ip, [sp, #36]
	cmn	r5, #1
	strb	ip, [fp, #8]
	bne	.L1248
	ldrb	r3, [fp, #3777]	@ zero_extendqisi2
	cmp	r3, #0
	beq	.L1246
	ldr	r3, [fp, #2164]
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
	beq	.L1247
	ldrb	r3, [fp, #3836]	@ zero_extendqisi2
	cmp	r0, r3, lsr #1
	bls	.L1262
.L1247:
	ubfx	r0, r7, #8, #8
	bl	NandcSetDdrPara
	cmn	r5, #1
	bne	.L1262
.L1246:
	ldr	r3, .L1267+8
	mov	r0, r4
	ldr	r1, [sp, #44]
	ldr	r2, [r6, #8]
	ldr	r7, [r3, #164]
	ldr	r3, [r6, #12]
	cmp	r7, #0
	beq	.L1249
	blx	r7
	cmn	r0, #1
	mov	r5, r0
	bne	.L1263
	ldr	r2, .L1267+4
	ldr	r3, [r2, #2776]
	ldrb	r3, [r3, #19]	@ zero_extendqisi2
	sub	r3, r3, #1
	uxtb	r3, r3
	cmp	r3, #5
	bhi	.L1251
	ldrb	r1, [r2, #2909]	@ zero_extendqisi2
	mov	r0, r4
	add	r2, r2, #2912
	mov	r3, #0
	bl	HynixSetRRPara
.L1251:
	ldr	r1, [sp, #44]
	mov	r0, r4
	ldr	r2, [r6, #8]
	ldr	r3, [r6, #12]
	bl	FlashReadRawPage
	ldr	r3, .L1267+4
	ldr	r1, [r6, #4]
	ldrb	r2, [r3, #3836]	@ zero_extendqisi2
	mov	r5, r0
	ldr	r0, .L1267+12
	mov	r3, r5
	bl	printk
	b	.L1263
.L1249:
	bl	FlashReadRawPage
	mov	r5, r0
	b	.L1250
.L1262:
	mov	r7, #0
.L1248:
	ldr	r3, .L1267+4
	ldrb	r3, [r3, #3836]	@ zero_extendqisi2
	add	r3, r3, r3, asl #1
	cmp	r5, r3, lsr #2
	bls	.L1250
	ldr	r3, .L1267+8
	ldr	r3, [r3, #164]
	cmp	r3, #0
	moveq	r5, #256
	b	.L1250
.L1263:
	mov	r7, #0
.L1250:
	ldr	ip, [sp, #16]
	cmp	r5, #256
	cmnne	r5, #1
	movne	r2, #0
	str	r5, [r9, ip]
	strne	r2, [r9, ip]
	ldr	ip, [sp, #16]
	ldr	r3, [r9, ip]
	cmn	r3, #1
	bne	.L1253
	ldr	r2, .L1267+4
	ldr	r1, [r6, #4]
	ldr	r0, .L1267+16
	ldrb	r2, [r2, #3836]	@ zero_extendqisi2
	bl	printk
	ldr	r1, [r6, #12]
	cmp	r1, #0
	beq	.L1253
	mov	r2, #4
	ldr	r0, .L1267+20
	mov	r3, r2
	bl	rknand_print_hex
.L1253:
	cmp	r7, #0
	beq	.L1254
	ldr	r3, .L1267+4
	ldrb	r3, [r3, #3836]	@ zero_extendqisi2
	add	r3, r3, r3, asl #1
	cmp	r8, r3, lsr #2
	bls	.L1255
	ldr	r3, .L1267+8
	ldr	r3, [r3, #164]
	cmp	r3, #0
	moveq	r8, #256
.L1255:
	add	r3, sl, #1
	mov	r2, #36
	cmp	r8, #256
	cmnne	r8, #1
	mul	r3, r2, r3
	movne	r2, #0
	str	r8, [r9, r3]
	strne	r2, [r9, r3]
.L1254:
	ldr	ip, [sp, #24]
	add	sl, sl, r7
	cmp	ip, #0
	beq	.L1235
	ldr	r3, .L1267+4
	ldrb	r3, [r3, #80]	@ zero_extendqisi2
	cmp	r3, #0
	beq	.L1235
	mov	r0, r4
	bl	flash_exit_slc_mode
.L1235:
	add	sl, sl, #1
.L1232:
	ldr	ip, [sp, #28]
	cmp	sl, ip
	bcc	.L1256
	mov	r0, #0
.L1233:
	add	sp, sp, #52
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, sl, fp, pc}
.L1268:
	.align	2
.L1267:
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
	@ args = 0, pretend = 0, frame = 16
	@ frame_needed = 0, uses_anonymous_args = 0
	ldr	r2, .L1295
	movw	r3, #3000
	stmfd	sp!, {r4, r5, r6, r7, r8, r9, sl, fp, lr}
	.save {r4, r5, r6, r7, r8, r9, sl, fp, lr}
	.pad #20
	sub	sp, sp, #20
	ldrh	r4, [r2, r3]
	movw	r3, #65535
	mov	r5, r0
	str	r1, [sp, #12]
	cmp	r4, r3
	beq	.L1288
	cmp	r4, #0
	bne	.L1270
	b	.L1271
.L1288:
	mov	r4, #0
.L1270:
	ldr	r2, .L1295+4
	movw	r3, #3912
	ldr	fp, [sp, #12]
	ldrh	r3, [r2, r3]
	cmp	fp, r3
	bne	.L1272
.L1271:
	bl	FtlGcPageVarInit
.L1272:
	mvn	r8, #0
	mov	r7, #0
	movw	sl, #65535
.L1283:
	ldrh	r3, [r5, #0]
	mov	r0, #0
	strb	r0, [r5, #8]
	cmp	r3, sl
	beq	.L1289
.L1274:
.L1292:
	ldr	r3, .L1295+4
	movw	r2, #3844
	ldrh	r9, [r3, r2]
	ldr	r2, .L1295+8
	ldr	lr, [r2, #-984]
	ldr	ip, [r2, #-1164]
	movw	r2, #3924
	ldrh	r2, [r3, r2]
	mov	r3, #0
	mov	r6, r3
	str	r2, [sp, #8]
	mov	r2, r5
	b	.L1275
.L1277:
	ldrh	r0, [r2, #16]
	cmp	r0, sl
	beq	.L1276
	mov	fp, #36
	orr	r0, r4, r0, asl #10
	mla	r1, fp, r6, lr
	ldr	fp, [sp, #8]
	str	r0, [r1, #4]
	mov	r0, #0
	str	r0, [r1, #8]
	mul	r0, fp, r6
	add	r6, r6, #1
	uxth	r6, r6
	add	fp, r0, #3
	cmp	r0, #0
	movlt	r0, fp
	bic	r0, r0, #3
	add	r0, ip, r0
	str	r0, [r1, #12]
.L1276:
	add	r3, r3, #1
	add	r2, r2, #2
	uxth	r3, r3
.L1275:
	cmp	r3, r9
	bne	.L1277
	ldr	r9, .L1295+8
	mov	r1, r6
	mov	r2, #0
	ldr	r0, [r9, #-984]
	bl	FlashReadPages
	mov	r3, #36
	mul	r3, r3, r6
	mov	r6, #0
	str	r3, [sp, #8]
	b	.L1278
.L1284:
	ldr	fp, .L1295+8
	ldr	r1, [fp, #-984]
	add	r3, r1, r6
	ldr	r9, [r3, #4]
	stmia	sp, {r1, r3}
	ubfx	r0, r9, #10, #16
	bl	P2V_plane
	ldr	r1, [sp, #0]
	ldr	r3, [sp, #4]
	ldr	r1, [r1, r6]
	cmp	r1, #0
	mov	r2, r0
	bne	.L1279
	ldr	r3, [r3, #12]
	add	r6, r6, #36
	ldrh	r1, [r3, #0]
	cmp	r1, sl
	moveq	r3, #1
	streq	r3, [fp, #-1016]
	beq	.L1273
.L1280:
	ldr	r0, [r3, #12]
	mov	r1, r9
	ldr	r2, [r3, #8]
	bl	FtlGcUpdatePage
	b	.L1278
.L1279:
	ldr	r3, [fp, #-1884]
	cmp	r3, #0
	beq	.L1281
	ldrh	r3, [r5, #0]
	ldr	r2, [fp, #-2084]
	mov	r3, r3, asl #1
	ldrh	r3, [r2, r3]
	cmp	r3, #119
	movls	r8, r9
	bls	.L1282
.L1281:
	cmn	r1, #1
	moveq	r8, r9
.L1282:
	ldr	r2, .L1295+8
	mov	r0, #0	@ movhi
	ldrh	r3, [r5, #0]
	mov	r4, #0
	ldr	r2, [r2, #-2064]
	mov	r3, r3, asl #1
	strh	r0, [r2, r3]	@ movhi
	ldrh	r0, [r5, #0]
	bl	INSERT_FREE_LIST
	mvn	r3, #0
	strh	r3, [r5, #0]	@ movhi
	bl	FtlGcPageVarInit
	b	.L1283
.L1278:
	ldr	r3, [sp, #8]
	cmp	r6, r3
	bne	.L1284
	ldr	fp, [sp, #12]
	add	r7, r7, #1
	add	r4, r4, #1
	cmp	r7, fp
	uxth	r4, r4
	bcc	.L1285
	ldr	r2, .L1295
	movw	r3, #3000
	ldrh	r1, [r2, r3]
	cmp	r1, sl
	beq	.L1285
	add	r1, r1, r7
	strh	r1, [r2, r3]	@ movhi
	ldr	r2, .L1295+4
	add	r3, r3, #912
	ldrh	r3, [r2, r3]
	cmp	r3, r4
	bhi	.L1286
.L1285:
	ldr	r2, .L1295+4
	movw	r3, #3912
	ldrh	r3, [r2, r3]
	cmp	r3, r4
	bhi	.L1292
.L1289:
	mov	r2, #0
.L1273:
	ldr	r1, .L1295
	movw	r3, #3000
	mvn	r0, #0
	strh	r4, [r5, #2]	@ movhi
	strb	r2, [r5, #6]
	strh	r0, [r1, r3]	@ movhi
	mov	r0, r5
	mov	r1, r4
	bl	ftl_sb_update_avl_pages
.L1286:
	mov	r0, r8
	add	sp, sp, #20
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, sl, fp, pc}
.L1296:
	.align	2
.L1295:
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
	ldr	r5, .L1355
	.pad #28
	sub	sp, sp, #28
	ldr	r4, .L1355+4
	mov	r1, r6
	ldr	r3, .L1355+8
	movw	r7, #3932
	ldr	r2, [r5, #3940]
	ldr	r0, [r4, #-1040]
	mov	sl, r4
	strh	r6, [r4, r3]	@ movhi
	movw	r3, #3956
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
	ldr	r0, [r4, #-924]
	mov	r2, r2, asl #2
	bl	ftl_memset
	ldrh	r2, [r5, r7]
	mov	r1, r6
	ldr	r0, [r5, #3960]
	mov	r2, r2, asl #1
	bl	ftl_memset
	ldr	r0, .L1355+12
	mov	r1, #255
	mov	r2, #12
	bl	ftl_memset
	movw	r3, #3852
	ldrh	r3, [r5, r3]
	str	r3, [sp, #8]
	b	.L1298
.L1300:
	add	r3, r6, r5
	ldr	r1, [sp, #8]
	ldrb	r0, [r3, #3870]	@ zero_extendqisi2
	bl	V2P_block
	str	r0, [sp, #0]
	bl	FtlBbmIsBadBlock
	ldr	r3, [sp, #0]
	cmp	r0, #0
	bne	.L1299
	ldr	r2, [r4, #-984]
	mov	r3, r3, asl #10
	mla	r2, r9, r7, r2
	str	r3, [r2, #4]
	ldrh	r3, [r6, r8]
	str	r0, [r2, #8]
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
.L1299:
	add	r5, r5, #1
	uxth	r5, r5
	b	.L1336
.L1349:
	mov	r7, #0
	movw	fp, #3844
	mov	r5, r7
	mov	r9, #36
	movw	r8, #3924
.L1336:
	ldrh	r3, [r6, fp]
	cmp	r3, r5
	bhi	.L1300
	ldr	r0, [r4, #-984]
	mov	r1, r7
	mov	r2, #1
	mov	r8, #0
	bl	FlashReadPages
	mov	fp, r7
	str	r8, [sp, #12]
	b	.L1301
.L1335:
	ldr	r2, [r4, #-984]
	add	r0, r2, r8
	ldr	r7, [r2, r8]
	ldr	r3, [r0, #4]
	cmn	r7, #1
	ldr	r5, [r0, #12]
	ubfx	r6, r3, #10, #16
	bne	.L1302
	mov	r1, #1
	add	r3, r3, #1
	mov	r2, r1
	str	r3, [r0, #4]
	bl	FlashReadPages
	ldrh	r3, [r5, #0]
	movw	r0, #65535
	cmp	r3, r0
	ldreq	r3, [r4, #-984]
	streq	r7, [r3, r8]
.L1302:
	ldr	r3, [r4, #-984]
	ldr	r3, [r3, r8]
	cmn	r3, #1
	beq	.L1303
	ldr	r2, [r4, #-1848]
	ldr	r3, [r5, #4]
	cmn	r2, #1
	beq	.L1304
	cmp	r2, r3
	bhi	.L1305
.L1304:
	cmn	r3, #1
	addne	r3, r3, #1
	strne	r3, [sl, #-1848]
.L1305:
	ldrh	r3, [r5, #0]
	movw	r2, #61604
	cmp	r3, r2
	beq	.L1308
	bhi	.L1311
	movw	r2, #61574
	cmp	r3, r2
	bne	.L1306
	b	.L1353
.L1311:
	movw	r2, #61634
	cmp	r3, r2
	beq	.L1309
	movw	r1, #65535
	cmp	r3, r1
	bne	.L1306
	b	.L1354
.L1309:
	ldr	r3, .L1355
	ldr	r1, [r4, #-1040]
	ldr	r2, [r3, #3940]
	ldr	r3, .L1355+8
	uxth	r0, r2
	ldrh	r3, [r4, r3]
	sub	r7, r0, #1
	rsb	r0, r3, r0
	uxth	r7, r7
	sub	r0, r0, #1
	sxth	r0, r0
	str	r0, [sp, #4]
	b	.L1312
.L1320:
	mov	ip, r9, asl #2
	ldr	r0, [r1, r9, asl #2]
	str	ip, [sp, #16]
	ldr	ip, [r5, #4]
	cmp	ip, r0
	bls	.L1313
	ldr	r1, [r1, #0]
	cmp	r1, #0
	bne	.L1314
	cmp	r3, r2
	bne	.L1315
.L1314:
	ldr	r3, [r4, #-1044]
	mov	r1, #1
	ldrh	r0, [r3, #0]
	bl	FtlFreeSysBlkQueueIn
	b	.L1316
.L1315:
	ldr	r2, .L1355+8
	add	r3, r3, #1
	strh	r3, [r4, r2]	@ movhi
.L1316:
	sxth	ip, r7
	mov	r2, #0
	str	ip, [sp, #4]
	b	.L1317
.L1318:
	ldr	r0, [r4, #-1040]
	add	r1, r3, #1
	add	r2, r2, #1
	ldr	ip, [r0, r1, asl #2]
	mov	r1, r1, asl #1
	uxth	r2, r2
	str	ip, [r0, r3, asl #2]
	mov	r3, r3, asl #1
	ldr	r0, [r4, #-1044]
	ldrh	r1, [r0, r1]
	strh	r1, [r0, r3]	@ movhi
.L1317:
	ldr	r0, [sp, #4]
	sxth	r3, r2
	cmp	r3, r0
	bne	.L1318
	ldr	r3, [sl, #-1040]
	mov	r9, r9, asl #1
	ldr	r2, [r5, #4]
	ldr	ip, [sp, #16]
	str	r2, [r3, ip]
	ldr	r3, [sl, #-1044]
	strh	r6, [r3, r9]	@ movhi
	b	.L1319
.L1313:
	sub	r7, r7, #1
	uxth	r7, r7
.L1312:
	ldr	r0, [sp, #4]
	sxth	r9, r7
	cmp	r9, r0
	bgt	.L1320
.L1319:
	sxth	r7, r7
	cmp	r7, #0
	blt	.L1303
.L1321:
	ldr	r3, .L1355+8
	ldr	r1, .L1355
	ldrh	r2, [r4, r3]
	ldr	r1, [r1, #3940]
	rsb	r1, r2, r1
	sub	r1, r1, #1
	sxth	r1, r1
	cmp	r7, r1
	bgt	.L1306
	add	r2, r2, #1
	strh	r2, [r4, r3]	@ movhi
	ldr	r3, [r4, #-1040]
	ldr	r2, [r5, #4]
	str	r2, [r3, r7, asl #2]
	mov	r7, r7, asl #1
	ldr	r3, [r4, #-1044]
	b	.L1351
.L1353:
	ldr	r3, .L1355
	movw	r2, #3932
	movw	r0, #3956
	ldrh	r2, [r3, r2]
	ldrh	r3, [r3, r0]
	sub	r1, r2, #1
	uxth	r7, r1
	rsb	r1, r3, r1
	str	r1, [sp, #4]
	ldr	r1, [r4, #-924]
	b	.L1322
.L1330:
	mov	ip, r9, asl #2
	ldr	r0, [r1, r9, asl #2]
	str	ip, [sp, #16]
	ldr	ip, [r5, #4]
	cmp	ip, r0
	bls	.L1323
	ldr	r1, [r1, #0]
	cmp	r1, #0
	ldr	r1, .L1355
	bne	.L1324
	cmp	r3, r2
	bne	.L1325
.L1324:
	ldr	r3, [r1, #3960]
	mov	r1, #1
	ldrh	r0, [r3, #0]
	bl	FtlFreeSysBlkQueueIn
	b	.L1326
.L1325:
	movw	r2, #3956
	add	r3, r3, #1
	strh	r3, [r1, r2]	@ movhi
.L1326:
	sxth	r0, r7
	mov	r2, #0
	str	r0, [sp, #20]
	ldr	ip, .L1355
	str	r5, [sp, #4]
	b	.L1327
.L1328:
	ldr	r0, [r4, #-924]
	add	r1, r3, #1
	add	r2, r2, #1
	ldr	r5, [r0, r1, asl #2]
	mov	r1, r1, asl #1
	uxth	r2, r2
	str	r5, [r0, r3, asl #2]
	mov	r3, r3, asl #1
	ldr	r0, [ip, #3960]
	ldrh	r1, [r0, r1]
	strh	r1, [r0, r3]	@ movhi
.L1327:
	ldr	r0, [sp, #20]
	sxth	r3, r2
	cmp	r3, r0
	bne	.L1328
	ldr	r5, [sp, #4]
	mov	r9, r9, asl #1
	ldr	r3, [sl, #-924]
	ldr	ip, [sp, #16]
	ldr	r2, [r5, #4]
	str	r2, [r3, ip]
	ldr	r3, .L1355
	ldr	r3, [r3, #3960]
	strh	r6, [r3, r9]	@ movhi
	b	.L1329
.L1323:
	sub	r7, r7, #1
	uxth	r7, r7
.L1322:
	ldr	r0, [sp, #4]
	sxth	r9, r7
	cmp	r9, r0
	bgt	.L1330
.L1329:
	sxth	r7, r7
	cmp	r7, #0
	blt	.L1303
.L1331:
	ldr	r3, .L1355
	movw	r0, #3932
	movw	r2, #3956
	ldrh	r0, [r3, r0]
	ldrh	r1, [r3, r2]
	sub	r0, r0, #1
	rsb	r0, r1, r0
	sxth	r0, r0
	cmp	r7, r0
	bgt	.L1306
	add	r1, r1, #1
	strh	r1, [r3, r2]	@ movhi
	ldr	r1, [r5, #4]
	ldr	r2, [r4, #-924]
	str	r1, [r2, r7, asl #2]
	mov	r7, r7, asl #1
	ldr	r3, [r3, #3960]
.L1351:
	strh	r6, [r3, r7]	@ movhi
	b	.L1306
.L1308:
	ldr	r3, .L1355+16
	movw	r1, #65535
	ldrh	r2, [r4, r3]
	cmp	r2, r1
	streqh	r6, [r4, r3]	@ movhi
	beq	.L1352
	ldr	r3, .L1355+20
	movw	r2, #65535
	ldrh	r0, [r4, r3]
	cmp	r0, r2
	beq	.L1333
	mov	r1, #1
	bl	FtlFreeSysBlkQueueIn
.L1333:
	ldr	r3, [r5, #4]
	ldr	r2, [sl, #-1760]
	cmp	r2, r3
	ldr	r3, .L1355+20
	strcsh	r6, [r4, r3]	@ movhi
	bcs	.L1306
	ldr	r2, .L1355+16
	ldrh	r1, [r4, r2]
	strh	r6, [r4, r2]	@ movhi
	strh	r1, [r4, r3]	@ movhi
.L1352:
	ldr	r3, [r5, #4]
	str	r3, [r4, #-1760]
	b	.L1306
.L1354:
	mov	r0, r6
	mov	r1, #0
	b	.L1350
.L1303:
	mov	r0, r6
	mov	r1, #1
.L1350:
	bl	FtlFreeSysBlkQueueIn
.L1306:
	ldr	r5, [sp, #12]
	add	r8, r8, #36
	add	r3, r5, #1
	uxth	r3, r3
	str	r3, [sp, #12]
.L1301:
	ldr	ip, [sp, #12]
	cmp	ip, fp
	bne	.L1335
	ldr	r5, [sp, #8]
	add	r3, r5, #1
	uxth	r3, r3
	str	r3, [sp, #8]
.L1298:
	ldr	r6, .L1355
	movw	r3, #3854
	ldr	ip, [sp, #8]
	ldrh	r3, [r6, r3]
	cmp	r3, ip
	bhi	.L1349
	ldr	r1, .L1355+4
	ldr	r2, [r1, #-1044]
	ldrh	r3, [r2, #0]
	cmp	r3, #0
	bne	.L1337
	ldr	r0, .L1355+8
	ldrh	r1, [r1, r0]
	cmp	r1, #0
	ldrne	r0, [r6, #3940]
	bne	.L1338
	b	.L1337
.L1342:
	mov	r1, r1, asl #1
	ldrh	r1, [r2, r1]
	cmp	r1, #0
	beq	.L1339
	ldr	r7, .L1355
	sxth	r6, r3
	ldr	r1, .L1355+4
	mov	r5, #0
	b	.L1340
.L1341:
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
.L1340:
	ldr	r0, [r7, #3940]
	sxth	r2, r3
	cmp	r2, r0
	bcc	.L1341
	b	.L1337
.L1339:
	add	r3, r3, #1
	uxth	r3, r3
.L1338:
	sxth	r1, r3
	cmp	r1, r0
	bcc	.L1342
.L1337:
	ldr	r2, .L1355
	ldr	r1, [r2, #3960]
	ldrh	r3, [r1, #0]
	cmp	r3, #0
	bne	.L1343
	movw	r0, #3956
	ldrh	r0, [r2, r0]
	cmp	r0, #0
	movwne	r0, #3932
	ldrneh	r2, [r2, r0]
	bne	.L1344
	b	.L1343
.L1348:
	mov	ip, r0, asl #1
	ldrh	ip, [r1, ip]
	cmp	ip, #0
	beq	.L1345
	ldr	r1, .L1355
	movw	r8, #3932
	ldr	r7, .L1355+4
	mov	r6, #0
	b	.L1346
.L1347:
	ldr	r4, [r1, #3960]
	mov	ip, r2, asl #1
	rsb	r5, r0, r2
	add	r3, r3, #1
	ldrh	r9, [r4, ip]
	mov	sl, r5, asl #1
	uxth	r3, r3
	strh	r9, [r4, sl]	@ movhi
	ldr	r4, [r7, #-924]
	ldr	r2, [r4, r2, asl #2]
	str	r2, [r4, r5, asl #2]
	ldr	r2, [r1, #3960]
	strh	r6, [r2, ip]	@ movhi
.L1346:
	ldrh	ip, [r1, r8]
	sxth	r2, r3
	cmp	r2, ip
	blt	.L1347
	b	.L1343
.L1345:
	add	r3, r3, #1
	uxth	r3, r3
.L1344:
	sxth	r0, r3
	cmp	r0, r2
	blt	.L1348
.L1343:
	mov	r0, #0
	add	sp, sp, #28
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, sl, fp, pc}
.L1356:
	.align	2
.L1355:
	.word	.LANCHOR0
	.word	.LANCHOR2
	.word	-1048
	.word	.LANCHOR2-1768
	.word	-1768
	.word	-1764
	.fnend
	.size	FtlScanSysBlk, .-FtlScanSysBlk
	.align	2
	.global	FtlVendorPartRead
	.type	FtlVendorPartRead, %function
FtlVendorPartRead:
	.fnstart
	@ args = 0, pretend = 0, frame = 48
	@ frame_needed = 0, uses_anonymous_args = 0
	ldr	r3, .L1367
	stmfd	sp!, {r4, r5, r6, r7, r8, r9, sl, fp, lr}
	.save {r4, r5, r6, r7, r8, r9, sl, fp, lr}
	mov	r8, r2
	mov	r2, #3920
	mov	r5, r1
	ldrh	r9, [r3, r2]
	movw	r2, #3906
	ldrh	r2, [r3, r2]
	add	r1, r1, r0
	.pad #52
	sub	sp, sp, #52
	mov	r6, r0
	cmp	r1, r2
	mvnhi	r7, #0
	bhi	.L1358
	mov	r9, r0, lsr r9
	mov	r7, #0
	ldr	sl, .L1367+4
	mov	r9, r9, asl #2
	b	.L1359
.L1364:
	ldr	r3, [sl, #-920]
	mov	r0, r6
	ldr	r1, .L1367
	ldr	fp, [r3, r9]
	movw	r3, #3918
	ldrh	r4, [r1, r3]
	mov	r1, r4
	bl	__aeabi_uidivmod
	uxth	r3, r1
	rsb	r4, r3, r4
	uxth	r4, r4
	cmp	r4, r5
	uxthhi	r4, r5
	cmp	fp, #0
	beq	.L1361
	ldr	r1, .L1367+4
	add	r0, sp, #12
	str	fp, [sp, #16]
	ldr	r2, [r1, #-968]
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
	ldr	r1, [sl, #-968]
	moveq	r7, r2
	add	r1, r1, r3, asl #9
	mov	r2, r4, asl #9
	bl	memcpy
	b	.L1363
.L1361:
	mov	r0, r8
	mov	r1, fp
	mov	r2, r4, asl #9
	bl	ftl_memset
.L1363:
	rsb	r5, r4, r5
	add	r6, r6, r4
	add	r8, r8, r4, asl #9
	add	r9, r9, #4
.L1359:
	cmp	r5, #0
	bne	.L1364
.L1358:
	mov	r0, r7
	add	sp, sp, #52
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, sl, fp, pc}
.L1368:
	.align	2
.L1367:
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
	ldr	r5, .L1373
	mov	r1, #1
	ldr	r2, .L1373+4
	bl	FtlVendorPartRead
	ldr	r4, .L1373+8
	ldr	r3, [r5, #-1704]
	cmp	r3, r4
	beq	.L1370
	ldr	r0, .L1373+4
	mov	r1, #0
	mov	r2, #512
	bl	ftl_memset
	str	r4, [r5, #-1704]
.L1370:
	ldr	r2, [r5, #-1704]
	ldr	r3, .L1373
	cmp	r2, r4
	bne	.L1371
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
.L1371:
	ldr	r4, .L1373
	mov	r3, #0
	ldr	r2, [r4, #-1636]
	str	r3, [r4, #-1004]
	ldr	r3, .L1373+12
	cmp	r2, r3
	bne	.L1372
	mov	r3, #1
	ldr	r0, .L1373+16
	str	r3, [r4, #-1884]
	ldr	r1, .L1373+20
	bl	printk
.L1372:
	ldr	r3, .L1373+24
	movw	r2, #3902
	ldr	r0, [r4, #-1840]
	ldrh	r1, [r3, r2]
	ldr	r2, [r4, #-1836]
	mla	r0, r0, r1, r2
	movw	r2, #3852
	ldrh	r1, [r3, r2]
	bl	__aeabi_uidiv
	str	r0, [r4, #-1832]
	ldmfd	sp!, {r3, r4, r5, pc}
.L1374:
	.align	2
.L1373:
	.word	.LANCHOR2
	.word	.LANCHOR2-1704
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
	ldr	r4, .L1377
	ldr	r5, .L1377+4
	ldr	r2, [r4, #-936]
	ldrh	r1, [r4, r5]
	bl	FtlVendorPartRead
	ldr	r3, [r4, #-936]
	ldr	r2, [r3, #0]
	ldr	r3, .L1377+8
	cmp	r2, r3
	beq	.L1376
	ldr	r1, .L1377+12
	ldr	r0, .L1377+16
	bl	printk
	ldrh	r2, [r4, r5]
	ldr	r0, [r4, #-936]
	mov	r1, #0
	mov	r2, r2, asl #9
	bl	ftl_memset
.L1376:
	mov	r0, #0
	ldmfd	sp!, {r3, r4, r5, pc}
.L1378:
	.align	2
.L1377:
	.word	.LANCHOR2
	.word	-940
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
	ldr	r4, .L1382
	mov	r5, #12
	ldr	r3, [r4, #-1036]
	ldr	r7, [r3, r0, asl #2]
	cmp	r7, #0
	bne	.L1380
	mul	r5, r5, r1
	ldr	r3, [r4, #-1900]
	ldr	r2, .L1382+4
	mov	r1, #255
	add	r3, r3, r5
	ldr	r0, [r3, #8]
	movw	r3, #3922
	ldrh	r2, [r2, r3]
	bl	ftl_memset
	ldr	r3, [r4, #-1900]
	strh	r6, [r3, r5]	@ movhi
	ldr	r3, [r4, #-1900]
	add	r5, r3, r5
	str	r7, [r5, #4]
	b	.L1381
.L1380:
	mul	r5, r5, r1
	ldr	r3, [r4, #-1900]
	mov	r1, #1
	add	r0, r4, #168
	mov	r2, r1
	str	r7, [r4, #172]
	add	r3, r3, r5
	ldr	r3, [r3, #8]
	str	r3, [r4, #176]
	ldr	r3, [r4, #-952]
	str	r3, [r4, #180]
	bl	FlashReadPages
	ldr	r3, [r4, #-1900]
	mov	r1, #0
	add	r2, r3, r5
	str	r1, [r2, #4]
	strh	r6, [r3, r5]	@ movhi
.L1381:
	mov	r0, #0
	ldmfd	sp!, {r3, r4, r5, r6, r7, pc}
.L1383:
	.align	2
.L1382:
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
	ldr	r3, .L1395
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
	bne	.L1388
	b	.L1393
.L1391:
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
	bne	.L1389
	ldr	r3, [sp, #44]
	cmn	r3, #1
	bne	.L1389
	ldr	r3, [sp, #4]
	cmn	r3, #1
	subne	r4, r6, #1
	uxthne	r4, r4
	bne	.L1393
.L1389:
	add	r6, r6, #1
	uxth	r8, r6
.L1393:
	sxth	r6, r8
	sxth	r3, r4
	cmp	r6, r3
	ble	.L1391
.L1388:
	sxth	r0, r4
	add	sp, sp, #108
	ldmfd	sp!, {r4, r5, r6, r7, r8, sl, pc}
.L1396:
	.align	2
.L1395:
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
	ldr	r4, .L1411
	mov	r1, r5
	ldr	r2, .L1411+4
	ldr	r7, .L1411+8
	ldr	r3, [r4, #-952]
	str	r5, [r4, #176]
	ldr	r0, [r4, #-2064]
	str	r3, [r4, #180]
	movw	r3, #3852
	ldrh	r2, [r2, r3]
	mov	r2, r2, asl #1
	bl	ftl_memset
	ldrh	r0, [r4, r7]
	movw	r3, #65535
	cmp	r0, r3
	beq	.L1409
	mov	r1, #1
	bl	FtlGetLastWrittenPage
	ldr	r3, .L1411+12
	add	r2, r0, #1
	mov	r6, r0
	strh	r2, [r4, r3]	@ movhi
	b	.L1399
.L1401:
	ldrh	r2, [r4, r7]
	mov	r1, #1
	ldr	r0, .L1411+16
	sub	r5, r5, #1
	orr	r3, r3, r2, asl #10
	str	r3, [r4, #172]
	ldr	r3, [r4, #-2076]
	mov	r2, r1
	uxth	r5, r5
	str	r3, [r4, #176]
	bl	FlashReadPages
	ldr	r3, [r4, #168]
	cmn	r3, #1
	bne	.L1400
.L1399:
	add	r3, r5, r6
	sxth	r3, r3
	cmp	r3, #0
	bge	.L1401
.L1400:
	ldr	r4, .L1411
	mov	r2, #48
	ldr	r5, .L1411+4
	movw	r6, #3852
	ldr	r0, .L1411+20
	ldr	r7, [r4, #176]
	mov	r1, r7
	bl	memcpy
	ldrh	r2, [r5, r6]
	add	r1, r7, #48
	ldr	r0, [r4, #-2064]
	mov	r2, r2, asl #1
	bl	memcpy
	ldrh	r1, [r5, r6]
	ldr	r3, [r4, #176]
	ldr	r0, [r4, #-1888]
	mov	r2, r1, lsr #3
	add	r1, r1, #24
	add	r2, r2, #4
	mov	r1, r1, lsr #1
	add	r1, r3, r1, asl #2
	bl	memcpy
	ldr	r2, [r4, #-1820]
	ldr	r3, .L1411+24
	cmp	r2, r3
	bne	.L1409
	ldr	r3, .L1411+28
	ldr	r2, .L1411+32
	ldrb	r1, [r4, #-1810]	@ zero_extendqisi2
	ldrh	r3, [r4, r3]
	strh	r3, [r4, r2]	@ movhi
	movw	r2, #3866
	ldrh	r2, [r5, r2]
	cmp	r1, r2
	bne	.L1409
	movw	r2, #3912
	movw	r1, #3918
	ldrh	r2, [r5, r2]
	ldrh	r1, [r5, r1]
	ldr	r0, [r5, #3856]
	mul	r2, r3, r2
	str	r3, [r4, #204]
	str	r2, [r5, #3972]
	mul	r2, r1, r2
	str	r2, [r5, #3952]
	movw	r2, #3986
	ldrh	r2, [r5, r2]
	rsb	r0, r2, r0
	rsb	r0, r3, r0
	movw	r3, #3844
	ldrh	r1, [r5, r3]
	movw	r5, #65535
	bl	__aeabi_uidiv
	ldr	r3, .L1411+36
	ldr	r1, .L1411+40
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
	ldr	r0, .L1411+44
	strb	r3, [r4, #-2036]
	ldr	r3, .L1411+48
	strh	r1, [r4, r3]	@ movhi
	mov	r3, #0
	ldr	r1, .L1411+52
	strh	r3, [r4, r0]	@ movhi
	strb	r3, [r4, #-1750]
	ldrh	r0, [r4, r1]
	sub	r1, r1, #194
	strb	r3, [r4, #-1748]
	str	r3, [r4, #-1860]
	strh	r0, [r4, r1]	@ movhi
	add	r1, r1, #196
	ldr	r0, .L1411+56
	ldrh	r1, [r4, r1]
	mov	ip, r1, lsr #6
	and	r1, r1, #63
	strb	r1, [r4, #-1990]
	ldrb	r1, [r4, #-1808]	@ zero_extendqisi2
	strh	ip, [r4, r0]	@ movhi
	strb	r1, [r4, #-1988]
	ldr	r1, .L1411+60
	ldrh	r0, [r4, r1]
	sub	r1, r1, #150
	strh	r0, [r4, r1]	@ movhi
	add	r1, r1, #152
	ldr	r0, .L1411+64
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
	ldr	r4, .L1411
	ldr	r3, [r4, #-1784]
	ldr	r1, [r4, #-1844]
	cmp	r3, r1
	strhi	r3, [r4, #-1844]
	cmp	r2, r5
	beq	.L1404
	ldr	r0, .L1411+68
	bl	make_superblock
.L1404:
	ldr	r3, .L1411+72
	ldrh	r3, [r4, r3]
	cmp	r3, r5
	beq	.L1405
	ldr	r0, .L1411+76
	bl	make_superblock
.L1405:
	ldr	r5, .L1411
	movw	r4, #65535
	ldr	r3, .L1411+80
	ldrh	r3, [r5, r3]
	cmp	r3, r4
	beq	.L1406
	ldr	r0, .L1411+84
	bl	make_superblock
.L1406:
	ldr	r3, .L1411+48
	ldrh	r3, [r5, r3]
	cmp	r3, r4
	beq	.L1410
	ldr	r0, .L1411+88
	bl	make_superblock
	mov	r0, #0
	ldmfd	sp!, {r3, r4, r5, r6, r7, pc}
.L1409:
	mvn	r0, #0
	ldmfd	sp!, {r3, r4, r5, r6, r7, pc}
.L1410:
	mov	r0, #0
	ldmfd	sp!, {r3, r4, r5, r6, r7, pc}
.L1412:
	.align	2
.L1411:
	.word	.LANCHOR2
	.word	.LANCHOR0
	.word	-1768
	.word	-1766
	.word	.LANCHOR2+168
	.word	.LANCHOR2-1820
	.word	1179929683
	.word	-1812
	.word	-1762
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
	ldr	r6, .L1428
	ldr	r8, .L1428+4
	ldr	r4, [r6, #-952]
	mov	r7, r6
	str	r3, [r6, #176]
	str	r4, [r6, #180]
	bl	FtlBbtMemInit
	movw	r3, #3908
	ldrh	r5, [r8, r3]
	sub	r5, r5, #1
	uxth	r5, r5
	b	.L1414
.L1418:
	mov	r1, #1
	mov	r3, r5, asl #10
	ldr	r0, .L1428+8
	mov	r2, r1
	str	r3, [r6, #172]
	bl	FlashReadPages
	ldr	r3, [r6, #168]
	cmn	r3, #1
	bne	.L1415
	ldr	r3, [r6, #172]
	mov	r1, #1
	ldr	r0, .L1428+8
	mov	r2, r1
	add	r3, r3, #1
	str	r3, [r6, #172]
	bl	FlashReadPages
.L1415:
	ldr	r3, [r7, #168]
	cmn	r3, #1
	beq	.L1416
	ldrh	r2, [r4, #0]
	movw	r3, #61649
	cmp	r2, r3
	bne	.L1416
	ldr	r3, .L1428+4
	movw	r2, #3980
	strh	r5, [r3, r2]	@ movhi
	ldr	r2, [r4, #4]
	str	r2, [r3, #3988]
	mov	r2, #3984
	ldrh	r1, [r4, #8]
	strh	r1, [r3, r2]	@ movhi
	b	.L1417
.L1416:
	sub	r5, r5, #1
	uxth	r5, r5
.L1414:
	movw	r3, #3908
	ldrh	r3, [r8, r3]
	sub	r3, r3, #48
	cmp	r5, r3
	bgt	.L1418
.L1417:
	ldr	r5, .L1428+4
	movw	r8, #3980
	movw	r3, #65535
	ldrh	r2, [r5, r8]
	cmp	r2, r3
	beq	.L1427
	mov	r6, #3984
	ldrh	r2, [r5, r6]
	cmp	r2, r3
	beq	.L1420
	ldr	r7, .L1428
	mov	r1, #1
	mov	r2, r2, asl #10
	add	r0, r7, #168
	str	r2, [r7, #172]
	mov	r2, r1
	bl	FlashReadPages
	ldr	r3, [r7, #168]
	cmn	r3, #1
	beq	.L1420
	ldrh	r2, [r4, #0]
	movw	r3, #61649
	cmp	r2, r3
	bne	.L1420
	ldr	r3, [r4, #4]
	ldr	r2, [r5, #3988]
	cmp	r3, r2
	strhi	r3, [r5, #3988]
	ldrhih	r2, [r5, r6]
	ldrhih	r3, [r4, #8]
	strhih	r2, [r5, r8]	@ movhi
	strhih	r3, [r5, r6]	@ movhi
.L1420:
	ldr	r8, .L1428+4
	movw	r3, #3980
	mov	r1, #1
	mov	r6, #0
	ldr	r5, .L1428
	ldrh	r0, [r8, r3]
	bl	FtlGetLastWrittenPage
	movw	r3, #3982
	add	r2, r0, #1
	mov	r7, r0
	strh	r2, [r8, r3]	@ movhi
	b	.L1421
.L1423:
	movw	r2, #3980
	mov	r1, #1
	ldrh	r2, [r8, r2]
	sub	r6, r6, #1
	ldr	r0, .L1428+8
	uxth	r6, r6
	orr	r3, r3, r2, asl #10
	str	r3, [r5, #172]
	ldr	r3, [r5, #-2076]
	mov	r2, r1
	str	r3, [r5, #176]
	bl	FlashReadPages
	ldr	r3, [r5, #168]
	cmn	r3, #1
	bne	.L1422
.L1421:
	add	r3, r6, r7
	sxth	r3, r3
	cmp	r3, #0
	bge	.L1423
.L1422:
	ldr	r3, .L1428+4
	movw	r2, #3986
	ldrh	r1, [r4, #10]
	ldrh	r0, [r4, #12]
	strh	r1, [r3, r2]	@ movhi
	movw	r2, #65535
	cmp	r0, r2
	beq	.L1424
	ldr	r2, [r3, #3840]
	cmp	r0, r2
	beq	.L1424
	movw	r1, #3854
	ldrh	r3, [r3, r1]
	mov	r3, r3, lsr #2
	cmp	r2, r3
	bcs	.L1424
	cmp	r0, r3
	bcs	.L1424
	bl	FtlSysBlkNumInit
.L1424:
	ldr	r6, .L1428+12
	mov	r4, #0
	ldr	r8, .L1428+4
	movw	r7, #3866
	ldr	r5, .L1428
	b	.L1425
.L1426:
	ldr	r3, .L1428+16
	ldr	r1, [r5, #176]
	ldr	r0, [r6, #4]!
	ldrh	r2, [r5, r3]
	mov	r2, r2, asl #2
	mla	r1, r4, r2, r1
	bl	memcpy
	add	r4, r4, #1
.L1425:
	ldrh	r3, [r8, r7]
	cmp	r4, r3
	bcc	.L1426
	mov	r0, #0
	ldmfd	sp!, {r4, r5, r6, r7, r8, pc}
.L1427:
	mvn	r0, #0
	ldmfd	sp!, {r4, r5, r6, r7, r8, pc}
.L1429:
	.align	2
.L1428:
	.word	.LANCHOR2
	.word	.LANCHOR0
	.word	.LANCHOR2+168
	.word	.LANCHOR0+4004
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
	ldr	r3, .L1437
	movw	fp, #3866
	ldr	r7, .L1437+4
	ldr	r6, .L1437+8
	ldr	r2, [r3, #-2076]
	ldr	r8, [r3, #-952]
	str	r2, [r3, #176]
	str	r8, [r3, #180]
	b	.L1431
.L1436:
	movw	r9, #3908
	mvn	r3, #0
	ldrh	r4, [r6, r9]
	strh	r3, [r7], #2	@ movhi
	add	r4, r4, r3
	ldr	sl, .L1437
	uxth	r4, r4
	b	.L1432
.L1435:
	mla	r3, r3, r5, r4
	mov	r1, #1
	ldr	r0, .L1437+12
	mov	r2, r1
	mov	r3, r3, asl #10
	str	r3, [sl, #172]
	bl	FlashReadPages
	ldr	r3, [sl, #168]
	cmn	r3, #1
	beq	.L1433
	ldrh	r2, [r8, #0]
	movw	r3, #61664
	cmp	r2, r3
	streqh	r4, [r7, #-2]	@ movhi
	beq	.L1434
.L1433:
	sub	r4, r4, #1
	uxth	r4, r4
.L1432:
	ldrh	r3, [r6, r9]
	sub	r2, r3, #16
	cmp	r4, r2
	bgt	.L1435
.L1434:
	add	r5, r5, #1
.L1431:
	ldrh	r3, [r6, fp]
	cmp	r5, r3
	bcc	.L1436
	mov	r0, #0
	ldmfd	sp!, {r3, r4, r5, r6, r7, r8, r9, sl, fp, pc}
.L1438:
	.align	2
.L1437:
	.word	.LANCHOR2
	.word	.LANCHOR0+3992
	.word	.LANCHOR0
	.word	.LANCHOR2+168
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
	ldr	r3, .L1458
	mov	r6, r0
	mov	r8, #0
	ldr	r7, .L1458+4
	ldrb	fp, [r3, #2697]	@ zero_extendqisi2
	b	.L1440
.L1447:
	rsb	r3, r8, sl
	add	r2, sp, #20
	mov	r0, r6
	mov	r1, r9
	uxtb	r3, r3
	str	r3, [sp, #0]
	add	r3, sp, #24
	bl	LogAddr2PhyAddr
	ldr	r3, [sp, #24]
	ldrb	r2, [r7, #3766]	@ zero_extendqisi2
	cmp	r3, r2
	mvncs	r3, #0
	strcs	r3, [r6, #0]
	bcs	.L1442
	add	r3, r7, r3
	ldrb	r5, [r3, #3768]	@ zero_extendqisi2
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
	ldr	r1, [r7, #4]
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
	ldr	r1, [r7, #4]
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
.L1442:
	add	r8, r8, #1
	add	r6, r6, #36
.L1440:
	cmp	r8, sl
	bne	.L1447
	ldr	r3, [sp, #12]
	cmp	r3, #0
	movne	r6, #0
	ldrne	r5, .L1458+8
	bne	.L1448
	b	.L1449
.L1454:
	ldr	r3, [r4, #0]
	cmn	r3, #1
	bne	.L1450
	ldr	r1, [r4, #4]
	ldr	r0, .L1458+12
	bl	printk
	b	.L1451
.L1450:
	rsb	r3, r6, sl
	mov	r1, r9
	add	r2, sp, #20
	mov	r0, r4
	uxtb	r3, r3
	str	r3, [sp, #0]
	add	r3, sp, #24
	bl	LogAddr2PhyAddr
	ldr	r2, [r5, #152]
	mov	r3, #0
	mov	lr, r4
	add	ip, sp, #28
	str	r3, [r2, #0]
	ldr	r2, [r5, #156]
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
	ldr	r3, [r5, #152]
	str	r3, [sp, #36]
	ldr	r3, [r5, #156]
	str	r3, [sp, #40]
	bl	FlashReadPages
	ldr	r7, [sp, #28]
	cmn	r7, #1
	bne	.L1452
	ldr	r0, .L1458+16
	ldr	r1, [r4, #4]
	bl	printk
	str	r7, [r4, #0]
.L1452:
	ldr	r3, [r4, #12]
	cmp	r3, #0
	beq	.L1453
	ldr	r2, [r3, #0]
	ldr	r3, [r5, #156]
	ldr	r3, [r3, #0]
	cmp	r2, r3
	beq	.L1453
	ldr	r0, .L1458+20
	ldr	r1, [r4, #4]
	bl	printk
	mvn	r3, #0
	str	r3, [r4, #0]
.L1453:
	ldr	r3, [r4, #8]
	cmp	r3, #0
	beq	.L1451
	ldr	r2, [r3, #0]
	ldr	r3, [r5, #152]
	ldr	r3, [r3, #0]
	cmp	r2, r3
	beq	.L1451
	ldr	r0, .L1458+24
	ldr	r1, [r4, #4]
	bl	printk
	mvn	r3, #0
	str	r3, [r4, #0]
.L1451:
	add	r6, r6, #1
	add	r4, r4, #36
.L1448:
	cmp	r6, sl
	bne	.L1454
.L1449:
	mov	r0, #0
	add	sp, sp, #68
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, sl, fp, pc}
.L1459:
	.align	2
.L1458:
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
	ldr	r5, .L1487
	mov	r9, r0
	str	r1, [sp, #8]
	mov	sl, r2
	str	r3, [sp, #16]
	ldr	ip, [r5, #2776]
	ldrb	r6, [r5, #0]	@ zero_extendqisi2
	ldrb	ip, [ip, #19]	@ zero_extendqisi2
	cmp	r6, #0
	moveq	fp, r5
	str	ip, [sp, #20]
	ldr	ip, .L1487+4
	ldrb	ip, [ip, #2697]	@ zero_extendqisi2
	str	ip, [sp, #12]
	beq	.L1461
	bl	FlashProgSlc2KPages
	b	.L1462
.L1474:
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
	ldrb	r3, [r5, #3766]	@ zero_extendqisi2
	ldr	r2, [sp, #32]
	cmp	r2, r3
	mvncs	r3, #0
	strcs	r3, [r9, r4]
	mov	r8, r0
	bcs	.L1464
	ldrb	r1, [r5, #3837]	@ zero_extendqisi2
	add	r2, fp, r2, asl #4
	cmp	r1, #0
	ldr	r2, [r2, #2788]
	moveq	r8, #0
	cmp	r2, #0
	beq	.L1466
	cmp	r3, #1
	bne	.L1467
	ldr	r0, [r5, #2164]
	bl	NandcIqrWaitFlashReady
.L1467:
	ldrb	r0, [sp, #32]	@ zero_extendqisi2
	bl	FlashWaitCmdDone
.L1466:
	ldr	r2, [sp, #32]
	mov	r1, #0
	cmp	r8, #0
	add	r3, r5, r2, asl #4
	movne	r0, #36
	str	r1, [r3, #2792]
	ldr	r1, [sp, #28]
	str	r7, [r3, #2788]
	str	r1, [r3, #2784]
	addne	r1, r6, #1
	mlane	r1, r0, r1, r9
	strne	r1, [r3, #2792]
	add	r3, fp, r2
	add	r2, fp, r2, asl #4
	ldrb	r4, [r3, #3768]	@ zero_extendqisi2
	ldrb	r3, [fp, #3766]	@ zero_extendqisi2
	cmp	r3, #1
	strb	r4, [r2, #2780]
	mov	r0, r4
	bne	.L1469
	bl	NandcWaitFlashReady
	b	.L1470
.L1469:
	bl	NandcFlashCs
	ldr	r3, [sp, #32]
	mov	r0, r4
	ldr	r1, [sp, #28]
	add	r3, r5, r3, asl #2
	ldr	r2, [r3, #2740]
	adds	r2, r2, #0
	movne	r2, #1
	bl	FlashWaitReadyEN
	mov	r0, r4
	bl	NandcFlashDeCs
.L1470:
	ldr	r2, [sp, #20]
	sub	r3, r2, #1
	cmp	r3, #5
	bhi	.L1471
	add	r3, r5, r4
	ldrb	r3, [r3, #3760]	@ zero_extendqisi2
	cmp	r3, #0
	beq	.L1471
	mov	r0, r4
	ldrb	r1, [r5, #2909]	@ zero_extendqisi2
	ldr	r2, .L1487+8
	mov	r3, #0
	bl	HynixSetRRPara
.L1471:
	mov	r0, r4
	bl	NandcFlashCs
	cmp	sl, #1
	bne	.L1472
	ldrb	r3, [r5, #80]	@ zero_extendqisi2
	cmp	r3, #0
	beq	.L1472
	mov	r0, r4
	bl	flash_enter_slc_mode
.L1472:
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
	beq	.L1473
	mov	r0, r4
	ldr	r1, [sp, #28]
	bl	FlashProgDpFirstCmd
	ldr	r3, [sp, #32]
	ldr	r1, [sp, #28]
	mov	r0, r4
	add	r3, r5, r3, asl #2
	ldr	r2, [r3, #2740]
	adds	r2, r2, #0
	movne	r2, #1
	bl	FlashWaitReadyEN
	ldr	r3, [sp, #28]
	ldr	r1, [r5, #4]
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
.L1473:
	mov	r0, r4
	ldr	r1, [sp, #28]
	bl	FlashProgSecondCmd
	mov	r0, r4
	bl	NandcFlashDeCs
	add	r6, r6, r8
.L1464:
	add	r6, r6, #1
.L1461:
	ldr	r3, [sp, #8]
	cmp	r6, r3
	bcc	.L1474
	ldr	r5, .L1487
	mov	r4, #0
	ldr	r6, .L1487+12
	ldr	r0, [r5, #2164]
	bl	NandcIqrWaitFlashReady
	b	.L1475
.L1477:
	uxtb	r0, r4
	bl	FlashWaitCmdDone
	cmp	sl, #1
	bne	.L1476
	ldrb	r3, [r5, #80]	@ zero_extendqisi2
	cmp	r3, #0
	beq	.L1476
	ldrb	r0, [r6, r4, asl #4]	@ zero_extendqisi2
	bl	flash_exit_slc_mode
.L1476:
	add	r4, r4, #1
.L1475:
	ldrb	r3, [r5, #3766]	@ zero_extendqisi2
	cmp	r4, r3
	bcc	.L1477
	ldr	r2, [sp, #16]
	cmp	r2, #0
	ldreq	r0, [sp, #16]
	beq	.L1462
	mov	r5, #0
	ldr	r4, .L1487+16
	ldr	r6, [sp, #8]
	b	.L1478
.L1483:
	ldr	r3, [r9, #0]
	cmn	r3, #1
	bne	.L1479
	ldr	r1, [r9, #4]
	ldr	r0, .L1487+20
	bl	printk
	b	.L1480
.L1479:
	rsb	r3, r5, r6
	mov	r1, sl
	add	r2, sp, #28
	mov	r0, r9
	uxtb	r3, r3
	str	r3, [sp, #0]
	add	r3, sp, #32
	bl	LogAddr2PhyAddr
	ldr	r2, [r4, #152]
	mov	r3, #0
	mov	lr, r9
	add	ip, sp, #36
	str	r3, [r2, #0]
	ldr	r2, [r4, #156]
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
	ldr	r3, [r4, #152]
	str	r3, [sp, #44]
	ldr	r3, [r4, #156]
	str	r3, [sp, #48]
	bl	FlashReadPages
	ldr	r7, [sp, #36]
	cmn	r7, #1
	bne	.L1481
	ldr	r0, .L1487+24
	ldr	r1, [r9, #4]
	bl	printk
	str	r7, [r9, #0]
.L1481:
	ldr	r3, [r9, #12]
	cmp	r3, #0
	beq	.L1482
	ldr	r2, [r3, #0]
	ldr	r3, [r4, #156]
	ldr	r3, [r3, #0]
	cmp	r2, r3
	beq	.L1482
	ldr	r0, .L1487+28
	ldr	r1, [r9, #4]
	bl	printk
	mvn	r3, #0
	str	r3, [r9, #0]
.L1482:
	ldr	r3, [r9, #8]
	cmp	r3, #0
	beq	.L1480
	ldr	r2, [r3, #0]
	ldr	r3, [r4, #152]
	ldr	r3, [r3, #0]
	cmp	r2, r3
	beq	.L1480
	ldr	r0, .L1487+32
	ldr	r1, [r9, #4]
	bl	printk
	mvn	r3, #0
	str	r3, [r9, #0]
.L1480:
	add	r5, r5, #1
	add	r9, r9, #36
.L1478:
	cmp	r5, r6
	bne	.L1483
	mov	r0, #0
.L1462:
	add	sp, sp, #76
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, sl, fp, pc}
.L1488:
	.align	2
.L1487:
	.word	.LANCHOR0
	.word	.LANCHOR1
	.word	.LANCHOR0+2912
	.word	.LANCHOR0+2780
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
	mov	r8, #0
	ldr	r4, .L1498
	mov	r1, #255
	ldr	r6, .L1498+4
	movw	sl, #3852
	ldr	r7, .L1498+8
	ldr	r3, [r4, #-2076]
	ldr	r5, [r4, #-952]
	str	r3, [r4, #176]
	ldrh	r3, [r4, r6]
	str	r5, [r4, #180]
	str	r8, [r5, #12]
	strh	r3, [r5, #2]	@ movhi
	ldr	r3, .L1498+12
	strh	r3, [r5, #0]	@ movhi
	ldr	r3, [r4, #-1760]
	str	r8, [r5, #8]
	str	r3, [r5, #4]
	ldr	r3, .L1498+16
	str	r3, [r4, #-1820]
	ldr	r3, .L1498+20
	str	r3, [r4, #-1816]
	ldr	r3, .L1498+24
	ldrh	r2, [r4, r3]
	sub	r3, r3, #50
	strh	r2, [r4, r3]	@ movhi
	movw	r3, #3866
	ldrh	r3, [r7, r3]
	strb	r3, [r4, #-1810]
	ldr	r3, .L1498+28
	ldrh	r2, [r4, r3]
	add	r3, r3, #238
	strh	r2, [r4, r3]	@ movhi
	sub	r3, r3, #236
	ldrh	r2, [r4, r3]
	ldrb	r3, [r4, #-2038]	@ zero_extendqisi2
	orr	r2, r3, r2, asl #6
	ldr	r3, .L1498+32
	strh	r2, [r4, r3]	@ movhi
	ldrb	r3, [r4, #-2036]	@ zero_extendqisi2
	strb	r3, [r4, #-1809]
	ldr	r3, .L1498+36
	ldrh	r2, [r4, r3]
	add	r3, r3, #194
	strh	r2, [r4, r3]	@ movhi
	sub	r3, r3, #192
	ldrh	r2, [r4, r3]
	ldrb	r3, [r4, #-1990]	@ zero_extendqisi2
	orr	r2, r3, r2, asl #6
	ldr	r3, .L1498+40
	strh	r2, [r4, r3]	@ movhi
	ldrb	r3, [r4, #-1988]	@ zero_extendqisi2
	strb	r3, [r4, #-1808]
	ldr	r3, .L1498+44
	ldrh	r2, [r4, r3]
	add	r3, r3, #150
	strh	r2, [r4, r3]	@ movhi
	sub	r3, r3, #148
	ldr	r0, [r4, #176]
	ldrh	r2, [r4, r3]
	ldrb	r3, [r4, #-1942]	@ zero_extendqisi2
	orr	r2, r3, r2, asl #6
	ldr	r3, .L1498+48
	strh	r2, [r4, r3]	@ movhi
	ldrb	r3, [r4, #-1940]	@ zero_extendqisi2
	strb	r3, [r4, #-1807]
	ldr	r3, [r4, #-1840]
	str	r3, [r4, #-1788]
	ldr	r3, [r4, #-1848]
	str	r3, [r4, #-1780]
	ldr	r3, [r4, #-1844]
	str	r3, [r4, #-1784]
	movw	r3, #3922
	ldrh	r2, [r7, r3]
	bl	ftl_memset
	ldr	r1, .L1498+52
	mov	r2, #48
	ldr	r0, [r4, #176]
	bl	memcpy
	ldrh	r2, [r7, sl]
	ldr	r0, [r4, #176]
	ldr	r1, [r4, #-2064]
	mov	r2, r2, asl #1
	add	r0, r0, #48
	bl	memcpy
	ldrh	r2, [r7, sl]
	ldr	r3, [r4, #176]
	add	r0, r2, #24
	ldr	r1, [r4, #-1888]
	mov	r2, r2, lsr #3
	mov	r0, r0, lsr #1
	add	r2, r2, #4
	add	r0, r3, r0, asl #2
	bl	memcpy
	mov	r0, r8
	bl	FtlUpdateVaildLpn
	movw	r8, #65535
.L1497:
	ldr	r3, [r4, #-2076]
	mov	r1, #1
	ldr	r7, .L1498+56
	ldrh	r2, [r4, r6]
	str	r3, [r4, #176]
	ldr	r3, [r4, #-952]
	ldr	r0, .L1498+60
	str	r3, [r4, #180]
	ldrh	r3, [r4, r7]
	orr	r3, r3, r2, asl #10
	mov	r2, r1
	str	r3, [r4, #172]
	mov	r3, r1
	bl	FlashProgPages
	ldr	r1, .L1498+8
	movw	r3, #3914
	ldrh	r2, [r4, r7]
	ldrh	r3, [r1, r3]
	sub	r3, r3, #1
	cmp	r2, r3
	blt	.L1491
	ldr	r3, .L1498+64
	ldrh	r2, [r4, r6]
	ldrh	r8, [r4, r3]
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
	strh	r0, [r4, r6]	@ movhi
	str	r2, [r4, #172]
	mov	r2, r1
	str	r3, [r5, #4]
	mov	r3, r1
	strh	r0, [r5, #2]	@ movhi
	ldr	r0, .L1498+60
	bl	FlashProgPages
.L1491:
	ldr	r2, .L1498+56
	ldr	r1, [r4, #168]
	ldrh	r3, [r4, r2]
	cmn	r1, #1
	add	r3, r3, #1
	uxth	r3, r3
	strh	r3, [r4, r2]	@ movhi
	bne	.L1492
	cmp	r3, #1
	ldreq	r1, .L1498+8
	movweq	r3, #3914
	ldreqh	r3, [r1, r3]
	subeq	r3, r3, #1
	streqh	r3, [r4, r2]	@ movhi
	b	.L1497
.L1492:
	cmp	r3, #1
	beq	.L1497
	movw	r3, #65535
	cmp	r8, r3
	beq	.L1494
	mov	r0, r8
	mov	r1, #1
	bl	FtlFreeSysBlkQueueIn
.L1494:
	mov	r0, #0
	ldmfd	sp!, {r3, r4, r5, r6, r7, r8, sl, pc}
.L1499:
	.align	2
.L1498:
	.word	.LANCHOR2
	.word	-1768
	.word	.LANCHOR0
	.word	-3932
	.word	1179929683
	.word	1342177334
	.word	-1762
	.word	-2044
	.word	-1804
	.word	-1996
	.word	-1800
	.word	-1948
	.word	-1796
	.word	.LANCHOR2-1820
	.word	-1766
	.word	.LANCHOR2+168
	.word	-1764
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
	ldr	r7, .L1509
	mov	r6, #0
	ldr	sl, .L1509+4
	movw	r9, #3866
	ldr	r8, .L1509+8
	ldr	r3, [r7, #-952]
	ldr	r0, [r7, #-2076]
	str	r3, [r7, #180]
	movw	r3, #3922
	str	r0, [r7, #176]
	ldrh	r2, [sl, r3]
	bl	ftl_memset
	b	.L1501
.L1502:
	ldr	r3, .L1509+12
	ldr	r1, [r8, #4]!
	ldrh	r2, [r7, r3]
	ldr	r3, [r7, #176]
	mul	r0, r2, r6
	mov	r2, r2, asl #2
	add	r6, r6, #1
	add	r0, r3, r0, asl #2
	bl	memcpy
.L1501:
	ldrh	r3, [sl, r9]
	ldr	r4, .L1509+4
	cmp	r6, r3
	ldr	r5, .L1509
	blt	.L1502
	ldr	r6, [r5, #180]
	mov	r1, #255
	mov	r2, #16
	mov	r7, #0
	mov	fp, r7
	mov	r0, r6
	bl	ftl_memset
	ldr	r3, .L1509+16
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
	ldr	r3, [r4, #3840]
	strh	r3, [r6, #12]	@ movhi
	b	.L1508
.L1507:
	mov	r7, #1
.L1508:
	ldr	r3, [r5, #-2076]
	movw	r8, #3980
	ldr	r4, .L1509+4
	movw	sl, #3982
	mov	r9, #3984
	str	r3, [r5, #176]
	ldr	r3, [r5, #-952]
	ldrh	r1, [r4, r8]
	ldrh	r2, [r4, sl]
	str	r3, [r5, #180]
	mov	r3, #0
	ldrh	r0, [r6, #10]
	str	r3, [r5, #168]
	orr	r3, r2, r1, asl #10
	str	r3, [r5, #172]
	str	r0, [sp, #0]
	ldrh	r3, [r4, r9]
	ldr	r0, .L1509+20
	bl	printk
	mov	r1, #1
	mov	r2, r1
	mov	r3, r1
	ldr	r0, .L1509+24
	bl	FlashProgPages
	movw	r3, #3914
	ldrh	r3, [r4, r3]
	ldrh	r2, [r4, sl]
	sub	r3, r3, #1
	cmp	r2, r3
	blt	.L1504
	ldr	r3, [r4, #3988]
	mov	r1, #1
	ldrh	r2, [r4, r8]
	add	r3, r3, #1
	ldr	r0, [r5, #-2088]
	str	r3, [r4, #3988]
	str	r3, [r6, #4]
	ldrh	r3, [r4, r9]
	strh	r2, [r6, #8]	@ movhi
	strh	r2, [r4, r9]	@ movhi
	mov	r2, r1
	strh	r3, [r4, r8]	@ movhi
	mov	r3, r3, asl #10
	str	r3, [r5, #172]
	str	r3, [r0, #4]
	strh	fp, [r4, sl]	@ movhi
	bl	FlashEraseBlocks
	mov	r1, #1
	ldr	r0, .L1509+24
	mov	r2, r1
	mov	r3, r1
	bl	FlashProgPages
.L1504:
	ldr	r2, .L1509+4
	movw	r3, #3982
	ldrh	r1, [r2, r3]
	add	r1, r1, #1
	strh	r1, [r2, r3]	@ movhi
	ldr	r3, [r5, #168]
	cmn	r3, #1
	bne	.L1505
	ldr	r1, [r5, #172]
	ldr	r0, .L1509+28
	bl	printk
	b	.L1508
.L1505:
	cmp	r7, #0
	beq	.L1507
	mov	r0, #0
	ldmfd	sp!, {r1, r2, r3, r4, r5, r6, r7, r8, r9, sl, fp, pc}
.L1510:
	.align	2
.L1509:
	.word	.LANCHOR2
	.word	.LANCHOR0
	.word	.LANCHOR0+4004
	.word	-1032
	.word	-3887
	.word	.LC103
	.word	.LANCHOR2+168
	.word	.LC104
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
	ldr	r4, .L1521
	ldr	r3, .L1521+4
	ldrh	r3, [r4, r3]
	cmp	r3, #0
	movne	r6, #0
	movne	sl, r4
	bne	.L1512
	b	.L1513
.L1519:
	add	r3, r3, r6
	mov	r1, r9
	mov	r5, #0
	ldr	r7, .L1521+4
	ldrb	r0, [r3, #3870]	@ zero_extendqisi2
	bl	V2P_block
	mov	r8, r0
	b	.L1514
.L1518:
	add	r3, r4, r5, asl #1
	sub	r3, r3, #1120
	sub	r3, r3, #12
	ldrh	r3, [r3, #0]
	cmp	r3, r8
	bne	.L1515
	mov	r1, r8
	ldr	r0, .L1521+8
	bl	printk
	mov	r0, r8
	bl	FtlBbmMapBadBlock
	bl	FtlBbmTblFlush
	ldrh	r1, [r4, r7]
	mov	r3, r5
	b	.L1516
.L1517:
	add	r0, r3, #1
	add	r3, r4, r3, asl #1
	sub	r3, r3, #1120
	add	r2, r4, r0, asl #1
	sub	r2, r2, #1120
	sub	r2, r2, #12
	ldrh	r2, [r2, #0]
	strh	r2, [r3, #-12]	@ movhi
	uxth	r3, r0
.L1516:
	cmp	r3, r1
	bcc	.L1517
	sub	r1, r1, #1
	strh	r1, [sl, r7]	@ movhi
.L1515:
	add	r5, r5, #1
	uxth	r5, r5
.L1514:
	ldrh	r3, [r4, r7]
	cmp	r3, r5
	bhi	.L1518
	add	r6, r6, #1
	uxth	r6, r6
.L1512:
	ldr	r3, .L1521+12
	movw	r2, #3844
	ldrh	r2, [r3, r2]
	cmp	r2, r6
	bhi	.L1519
	bl	FtlGcReFreshBadBlk
.L1513:
	mov	r0, #0
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, sl, pc}
.L1522:
	.align	2
.L1521:
	.word	.LANCHOR2
	.word	-1134
	.word	.LC105
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
	ldr	r3, .L1531
	mov	r4, r0
	ldr	r1, [r3, #-2064]
	ldrh	r2, [r1, r2]
	cmp	r2, #0
	bne	.L1524
	ldr	r1, .L1531+4
	ldrh	r0, [r3, r1]
	cmp	r0, r4
	mvneq	r2, #0
	streqh	r2, [r3, r1]	@ movhi
	beq	.L1526
	ldr	r1, .L1531+8
	ldrh	r1, [r3, r1]
	cmp	r1, r4
	beq	.L1527
	ldr	r1, .L1531+12
	ldrh	r1, [r3, r1]
	cmp	r1, r4
	beq	.L1527
	ldr	r1, .L1531+16
	ldrh	r3, [r3, r1]
	cmp	r3, r4
	beq	.L1527
.L1526:
	mov	r1, r4
	ldr	r0, .L1531+20
	bl	List_remove_node
	ldr	r5, .L1531
	ldr	r3, .L1531+24
	mov	r0, r4
	ldrh	r2, [r5, r3]
	sub	r2, r2, #1
	strh	r2, [r5, r3]	@ movhi
	bl	free_data_superblock
	mov	r0, r4
	bl	FtlGcFreeBadSuperBlk
	mov	r2, #1
	b	.L1527
.L1524:
	bl	List_update_data_list
	mov	r2, #0
.L1527:
	mov	r0, r2
	ldmfd	sp!, {r3, r4, r5, pc}
.L1532:
	.align	2
.L1531:
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
	beq	.L1534
	ldr	r2, .L1539
	mov	r3, r0, asl #1
	ldr	r2, [r2, #-2064]
	ldrh	r4, [r2, r3]
	cmp	r4, #0
	subne	r4, r4, #1
	strneh	r4, [r2, r3]	@ movhi
	bne	.L1534
	ldr	r0, .L1539+4
	mov	r1, r5
	mov	r2, r4
	bl	printk
	mov	r0, r4
	ldmfd	sp!, {r3, r4, r5, r6, r7, pc}
.L1534:
	ldr	r7, .L1539
	movw	r3, #65535
	ldr	r6, .L1539+8
	ldrh	r0, [r7, r6]
	cmp	r0, r3
	streqh	r5, [r7, r6]	@ movhi
	moveq	r0, #0
	ldmeqfd	sp!, {r3, r4, r5, r6, r7, pc}
	cmp	r0, r5
	beq	.L1538
	bl	update_vpc_list
	strh	r5, [r7, r6]	@ movhi
	adds	r0, r0, #0
	movne	r0, #1
	ldmfd	sp!, {r3, r4, r5, r6, r7, pc}
.L1538:
	mov	r0, #0
	ldmfd	sp!, {r3, r4, r5, r6, r7, pc}
.L1540:
	.align	2
.L1539:
	.word	.LANCHOR2
	.word	.LC106
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
	ldr	r5, .L1553
	ldr	r7, .L1553+4
	add	r3, r0, r3, asl #1
	ldrh	r3, [r3, #16]
	b	.L1542
.L1543:
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
.L1542:
	cmp	r3, r6
	beq	.L1543
	ldrb	r2, [r4, #8]	@ zero_extendqisi2
	cmp	r2, #1
	bne	.L1545
	ldrb	r2, [r5, #80]	@ zero_extendqisi2
	cmp	r2, #0
	bne	.L1545
	ldrh	r2, [r4, #2]
	add	r2, r7, r2, asl #1
	sub	r2, r2, #912
	ldrh	r2, [r2, #0]
	cmp	r2, r6
	bne	.L1545
	ldrh	r3, [r4, #4]
	ldrh	r0, [r4, #0]
	sub	r3, r3, #1
	strh	r3, [r4, #4]	@ movhi
	bl	decrement_vpc_count
	b	.L1543
.L1545:
	ldrh	r6, [r4, #2]
	movw	r5, #65535
	mov	r7, r5
	orr	r6, r6, r3, asl #10
	ldrh	r3, [r4, #4]
	sub	r3, r3, #1
	strh	r3, [r4, #4]	@ movhi
.L1550:
	ldr	r2, .L1553
	movw	r3, #3844
	ldrh	r2, [r2, r3]
	ldrb	r3, [r4, #6]	@ zero_extendqisi2
.L1547:
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
	beq	.L1547
	strb	r3, [r4, #6]
	ldrb	r3, [r4, #8]	@ zero_extendqisi2
	cmp	r3, #1
	bne	.L1548
	ldr	r3, .L1553
	ldrb	r2, [r3, #80]	@ zero_extendqisi2
	cmp	r2, #0
	ldrh	r2, [r4, #2]
	bne	.L1551
	ldr	r3, .L1553+4
	add	r2, r3, r2, asl #1
	sub	r2, r2, #912
	ldrh	r3, [r2, #0]
	cmp	r3, r7
	bne	.L1548
	ldrh	r3, [r4, #4]
	cmp	r3, #0
	beq	.L1548
	sub	r3, r3, #1
	ldrh	r0, [r4, #0]
	strh	r3, [r4, #4]	@ movhi
	bl	decrement_vpc_count
	b	.L1550
.L1551:
	movw	r1, #3914
	ldrh	r1, [r3, r1]
	cmp	r2, r1
	bcc	.L1548
	ldr	r1, .L1553+4
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
	movw	r2, #3912
	ldrh	r3, [r3, r2]
	strb	r1, [r4, #6]
	strh	r3, [r4, #2]	@ movhi
.L1548:
	mov	r0, r6
	ldmfd	sp!, {r3, r4, r5, r6, r7, pc}
.L1554:
	.align	2
.L1553:
	.word	.LANCHOR0
	.word	.LANCHOR2
	.fnend
	.size	get_new_active_ppa, .-get_new_active_ppa
	.align	2
	.type	FtlSlcSuperblockCheck.part.17, %function
FtlSlcSuperblockCheck.part.17:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	stmfd	sp!, {r3, r4, r5, r6, r7, lr}
	.save {r3, r4, r5, r6, r7, lr}
	mov	r4, r0
	ldrb	r3, [r0, #6]	@ zero_extendqisi2
	movw	r6, #65535
	ldr	r5, .L1561
	ldr	r7, .L1561+4
	add	r3, r0, r3, asl #1
	ldrh	r3, [r3, #16]
	b	.L1556
.L1557:
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
.L1556:
	cmp	r3, r6
	beq	.L1557
	ldrb	r2, [r4, #8]	@ zero_extendqisi2
	cmp	r2, #1
	bne	.L1559
	ldrb	r3, [r5, #80]	@ zero_extendqisi2
	cmp	r3, #0
	bne	.L1559
	ldrh	r3, [r4, #2]
	add	r3, r7, r3, asl #1
	sub	r3, r3, #912
	ldrh	r3, [r3, #0]
	cmp	r3, r6
	bne	.L1559
	ldrh	r3, [r4, #4]
	ldrh	r0, [r4, #0]
	sub	r3, r3, #1
	strh	r3, [r4, #4]	@ movhi
	bl	decrement_vpc_count
	ldrh	r3, [r4, #4]
	cmp	r3, #0
	bne	.L1557
	ldrh	r2, [r4, #2]
	strb	r3, [r4, #6]
	add	r2, r2, #1
	strh	r2, [r4, #2]	@ movhi
	ldmfd	sp!, {r3, r4, r5, r6, r7, pc}
.L1559:
	ldr	r3, .L1561
	ldrb	r1, [r3, #80]	@ zero_extendqisi2
	cmp	r1, #0
	ldmeqfd	sp!, {r3, r4, r5, r6, r7, pc}
	cmp	r2, #1
	ldmnefd	sp!, {r3, r4, r5, r6, r7, pc}
	movw	r2, #3914
	ldrh	r1, [r4, #2]
	ldrh	r2, [r3, r2]
	cmp	r1, r2
	ldmccfd	sp!, {r3, r4, r5, r6, r7, pc}
	ldr	r1, .L1561+4
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
	movw	r2, #3912
	ldrh	r3, [r3, r2]
	strb	r1, [r4, #6]
	strh	r3, [r4, #2]	@ movhi
	ldmfd	sp!, {r3, r4, r5, r6, r7, pc}
.L1562:
	.align	2
.L1561:
	.word	.LANCHOR0
	.word	.LANCHOR2
	.fnend
	.size	FtlSlcSuperblockCheck.part.17, .-FtlSlcSuperblockCheck.part.17
	.align	2
	.global	FtlSlcSuperblockCheck
	.type	FtlSlcSuperblockCheck, %function
FtlSlcSuperblockCheck:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	@ link register save eliminated.
	ldrh	r3, [r0, #4]
	cmp	r3, #0
	bxeq	lr
	b	FtlSlcSuperblockCheck.part.17
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
	ldr	r4, .L1599
	mov	r5, r0
	mov	r7, r4
.L1595:
	ldr	r3, .L1599+4
	cmp	r5, r3
	bne	.L1567
	ldr	r3, .L1599+8
	ldrh	r2, [r4, r3]
	ldr	r3, [r4, #-1180]
	mov	r1, r2, lsr #1
	mul	ip, r3, r2
	add	r0, r1, #1
	add	r0, r0, ip, lsr #2
	ldr	ip, [r4, #-1884]
	cmp	ip, #0
	uxth	r0, r0
	beq	.L1589
	ldr	ip, [r4, #-1824]
	cmp	ip, #29
	bhi	.L1589
	cmp	ip, #2
	bls	.L1594
	tst	r2, #1
	beq	.L1568
	cmp	r3, #0
	moveq	r1, r3
	beq	.L1569
	b	.L1568
.L1567:
	ldrb	r3, [r5, #8]	@ zero_extendqisi2
	cmp	r3, #1
	bne	.L1594
	ldr	r3, .L1599+12
	movw	r2, #3864
	ldrh	r2, [r3, r2]
	cmp	r2, #1
	beq	.L1594
	ldrb	r3, [r3, #80]	@ zero_extendqisi2
	cmp	r3, #0
	bne	.L1594
	sub	r3, r3, #2048
	ldr	r2, [r4, #-1884]
	ldrh	r3, [r4, r3]
	cmp	r2, #0
	mov	r1, r3, lsr #3
	beq	.L1568
	ldr	r2, [r4, #-1824]
	cmp	r2, #1
	movls	r1, #7
	mulls	r1, r1, r3
	movls	r1, r1, lsr #3
	b	.L1568
.L1589:
	mov	r1, r0
.L1568:
	cmp	r1, #0
	subne	r1, r1, #1
	uxthne	r1, r1
	b	.L1569
.L1594:
	mov	r1, #0
.L1569:
	ldr	r0, .L1599+16
	bl	List_pop_index_node
	ldr	r3, .L1599+8
	ldrh	r2, [r4, r3]
	sub	r2, r2, #1
	strh	r2, [r4, r3]	@ movhi
	uxth	r6, r0
	mov	r0, r5
	strh	r6, [r5, #0]	@ movhi
	bl	make_superblock
	ldrb	r3, [r5, #7]	@ zero_extendqisi2
	cmp	r3, #0
	beq	.L1570
	ldr	r2, .L1599+12
	movw	r3, #3844
	ldr	ip, [r4, #-2088]
	mov	fp, #0
	mov	sl, r5
	mov	r1, r5
	ldrh	r8, [r2, r3]
	mov	r0, fp
	mov	r3, ip
	mov	r2, fp
	stmia	sp, {r5, ip}
	b	.L1571
.L1570:
	ldr	r2, [r4, #-2064]
	mov	r3, r6, asl #1
	mvn	r1, #0
	strh	r1, [r2, r3]	@ movhi
	b	.L1596
.L1574:
	str	r0, [r3, #8]
	movw	r5, #65535
	str	r0, [r3, #12]
	ldrh	lr, [r1, #16]
	cmp	lr, r5
	beq	.L1573
	ldr	r5, [sp, #4]
	mov	ip, #36
	mov	lr, lr, asl #10
	mla	r9, ip, fp, r5
	add	fp, fp, #1
	uxth	fp, fp
	str	lr, [r9, #4]
.L1573:
	add	r2, r2, #1
	add	r3, r3, #36
	add	r1, r1, #2
	uxth	r2, r2
.L1571:
	cmp	r2, r8
	bne	.L1574
	ldr	r3, [r4, #-1884]
	ldr	r5, [sp, #0]
	cmp	r3, #0
	beq	.L1575
	ldr	r3, .L1599+20
	cmp	r5, r3
	bne	.L1575
	ldr	r2, [r4, #-2084]
	mov	r3, r6, asl #1
	ldrh	r3, [r2, r3]
	cmp	r3, #30
	movhi	r3, #0
	strhib	r3, [r4, #-2036]
.L1575:
	ldrb	r3, [r5, #8]	@ zero_extendqisi2
	ldr	r2, [r4, #-2084]
	cmp	r3, #0
	mov	r3, r6, asl #1
	ldrh	r1, [r2, r3]
	bne	.L1576
	cmp	r1, #0
	ldrne	ip, .L1599+12
	movwne	r0, #3902
	moveq	r1, #2
	ldrneh	r0, [ip, r0]
	addne	r1, r1, r0
	strh	r1, [r2, r3]	@ movhi
	ldr	r3, [r4, #-1840]
	mov	r0, r6
	mov	r1, #0
	add	r3, r3, #1
	str	r3, [r4, #-1840]
	b	.L1598
.L1576:
	add	r1, r1, #1
	strh	r1, [r2, r3]	@ movhi
	ldr	r3, [r4, #-1836]
	mov	r0, r6
	mov	r1, #1
	add	r3, r3, #1
	str	r3, [r4, #-1836]
.L1598:
	bl	ftl_set_blk_mode
	ldr	r3, [r4, #-2084]
	mov	r9, r6, asl #1
	ldr	r2, [r4, #-1828]
	ldr	r0, [r7, #-1840]
	mov	r8, #0
	ldrh	r3, [r3, r9]
	cmp	r3, r2
	movw	r2, #3902
	strhi	r3, [r4, #-1828]
	ldr	r3, .L1599+12
	ldrh	r1, [r3, r2]
	ldr	r2, [r7, #-1836]
	mla	r0, r0, r1, r2
	movw	r2, #3852
	ldrh	r1, [r3, r2]
	bl	__aeabi_uidiv
	ldr	r3, [r7, #-936]
	ldr	r2, [r3, #16]
	add	r2, r2, #1
	str	r2, [r3, #16]
	ldr	r3, [r7, #-2088]
	str	r0, [r7, #-1832]
	b	.L1581
.L1582:
	add	r8, r8, #1
	ldr	r2, [r3, #-32]
	uxth	r8, r8
	bic	r2, r2, #1020
	bic	r2, r2, #3
	str	r2, [r3, #-32]
.L1581:
	cmp	r8, fp
	add	r3, r3, #36
	bne	.L1582
	mov	r2, r8
	ldr	r0, [r4, #-2088]
	ldrb	r1, [r5, #8]	@ zero_extendqisi2
	mov	fp, #0
	bl	FlashEraseBlocks
	mov	r2, #36
	mul	r2, r2, r8
	mov	r3, r6
	mov	ip, r9
	mov	r8, fp
	mov	r6, r5
	mov	r9, r2
	b	.L1583
.L1585:
	ldr	r1, [r4, #-2088]
	add	r0, r1, fp
	ldr	r5, [r1, fp]
	cmn	r5, #1
	bne	.L1584
	ldr	r0, [r0, #4]
	add	r8, r8, #1
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
.L1584:
	add	fp, fp, #36
	add	sl, sl, #2
.L1583:
	cmp	fp, r9
	bne	.L1585
	cmp	r8, #0
	mov	r5, r6
	mov	r9, ip
	mov	r6, r3
	beq	.L1586
	mov	r0, r3
	bl	update_multiplier_value
	bl	FtlBbmTblFlush
.L1586:
	ldrb	r3, [r5, #7]	@ zero_extendqisi2
	cmp	r3, #0
	bne	.L1587
	ldr	r3, [r7, #-2064]
	mvn	r2, #0
	strh	r2, [r3, r9]	@ movhi
.L1596:
	mov	r0, r6
	bl	INSERT_DATA_LIST
	b	.L1595
.L1587:
	ldr	r1, .L1599+12
	movw	r2, #3912
	mov	r0, #0
	strh	r0, [r5, #2]	@ movhi
	strb	r0, [r5, #6]
	ldrh	r2, [r1, r2]
	strh	r6, [r5, #0]	@ movhi
	mul	r2, r2, r3
	ldr	r3, .L1599
	ldr	r1, [r3, #-1848]
	uxth	r2, r2
	strh	r2, [r5, #4]	@ movhi
	str	r1, [r5, #12]
	add	r1, r1, #1
	str	r1, [r3, #-1848]
	ldrh	ip, [r5, #0]
	ldr	r1, [r3, #-2064]
	mov	r3, ip, asl #1
	strh	r2, [r1, r3]	@ movhi
	add	sp, sp, #20
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, sl, fp, pc}
.L1600:
	.align	2
.L1599:
	.word	.LANCHOR2
	.word	.LANCHOR2-1948
	.word	-2048
	.word	.LANCHOR0
	.word	.LANCHOR2-2052
	.word	.LANCHOR2-2044
	.fnend
	.size	allocate_data_superblock, .-allocate_data_superblock
	.align	2
	.global	FtlSuperblockPowerLostFix
	.type	FtlSuperblockPowerLostFix, %function
FtlSuperblockPowerLostFix:
	.fnstart
	@ args = 0, pretend = 0, frame = 40
	@ frame_needed = 0, uses_anonymous_args = 0
	ldr	r3, .L1608
	stmfd	sp!, {r4, r5, r6, r7, r8, lr}
	.save {r4, r5, r6, r7, r8, lr}
	mov	r4, r0
	ldrb	r7, [r3, #80]	@ zero_extendqisi2
	.pad #40
	sub	sp, sp, #40
	cmp	r7, #0
	beq	.L1602
	ldrb	r7, [r0, #8]	@ zero_extendqisi2
	sub	r0, r7, #1
	rsbs	r7, r0, #0
	adc	r7, r7, r0
.L1602:
	mov	r8, #7
	mvn	r6, #0
	ldr	r5, .L1608+4
	b	.L1603
.L1606:
	ldrh	r3, [r4, #4]
	cmp	r3, #0
	beq	.L1604
	mov	r0, r4
	bl	get_new_active_ppa
	ldr	r2, [r5, #-952]
	ldr	r3, [r5, #-2076]
	str	r6, [sp, #20]
	str	r2, [sp, #16]
	str	r3, [sp, #12]
	str	r6, [r2, #12]
	str	r6, [r2, #8]
	ldrh	r3, [r4, #0]
	str	r0, [sp, #8]
	add	r0, sp, #4
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
.L1603:
	subs	r8, r8, #1
	bne	.L1606
.L1604:
	ldr	r2, .L1608+4
	ldrh	r3, [r4, #0]
	ldrh	r1, [r4, #4]
	ldr	r2, [r2, #-2064]
	mov	r3, r3, asl #1
	ldrh	r0, [r2, r3]
	rsb	r1, r1, r0
	strh	r1, [r2, r3]	@ movhi
	ldr	r2, .L1608
	movw	r3, #3912
	ldrh	r3, [r2, r3]
	strh	r3, [r4, #2]	@ movhi
	mov	r3, #0
	strb	r3, [r4, #6]
	strh	r3, [r4, #4]	@ movhi
	add	sp, sp, #40
	ldmfd	sp!, {r4, r5, r6, r7, r8, pc}
.L1609:
	.align	2
.L1608:
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
	ldr	r9, .L1636
	mov	r6, #0
	mov	r8, r0
	mov	r7, r1
	mov	r4, r6
	mov	r5, r6
	ldrb	r2, [r9, #80]	@ zero_extendqisi2
	mov	fp, #36
	str	r0, [r9, #3976]
	ldr	sl, .L1636+4
	str	r2, [sp, #8]
	b	.L1611
.L1615:
	mul	r3, fp, r6
	ldr	r2, [sl, #-2088]
	mov	r0, #0
	mov	r1, r8
	str	r0, [r2, r3]
	add	r3, r9, r6
	ldrb	r0, [r3, #3870]	@ zero_extendqisi2
	bl	V2P_block
	cmp	r7, #0
	str	r0, [sp, #4]
	beq	.L1612
	bl	IsBlkInVendorPart
	cmp	r0, #0
	bne	.L1613
.L1612:
	ldr	r0, [sp, #4]
	bl	FtlBbmIsBadBlock
	cmp	r0, #0
	addne	r5, r5, #1
	uxthne	r5, r5
	bne	.L1613
	ldr	r3, [sl, #-2088]
	ldr	r1, [sp, #4]
	mla	r3, fp, r4, r3
	mov	r2, r1, asl #10
	str	r0, [r3, #8]
	movw	r0, #3924
	str	r2, [r3, #4]
	ldrh	r2, [r9, r0]
	mul	r2, r2, r4
	add	r4, r4, #1
	uxth	r4, r4
	add	r1, r2, #3
	cmp	r2, #0
	movlt	r2, r1
	ldr	r1, [sl, #-948]
	bic	r2, r2, #3
	add	r2, r1, r2
	str	r2, [r3, #12]
.L1613:
	add	r6, r6, #1
	uxth	r6, r6
.L1611:
	movw	r1, #3844
	ldrh	r3, [r9, r1]
	cmp	r3, r6
	bhi	.L1615
	cmp	r4, #0
	beq	.L1617
	ldr	r2, [sp, #8]
	mov	r9, #0
	ldr	r6, .L1636+4
	mov	fp, r9
	adds	sl, r2, #0
	mov	r2, r4
	movne	sl, #1
	ldr	r0, [r6, #-2088]
	mov	r1, sl
	bl	FlashEraseBlocks
.L1619:
	ldr	r3, [r6, #-2088]
	add	r2, r3, r9
	ldr	r3, [r3, r9]
	cmn	r3, #1
	bne	.L1618
	ldr	r0, [r2, #4]
	add	r5, r5, #1
	ubfx	r0, r0, #10, #16
	uxth	r5, r5
	bl	FtlBbmMapBadBlock
.L1618:
	add	fp, fp, #1
	add	r9, r9, #36
	uxth	fp, fp
	cmp	fp, r4
	bne	.L1619
.L1620:
	cmp	r7, #0
	mov	r6, #0
	ldrne	r2, .L1636
	movwne	r3, #3914
	movne	sl, #1
	moveq	r0, #1
	moveq	r1, #6
	stmeqib	sp, {r0, r1}
	ldrneh	r3, [r2, r3]
	strne	r3, [sp, #4]
	movne	r3, r3, lsr #2
	strne	r3, [sp, #8]
.L1630:
	mov	r9, #0
	ldr	fp, .L1636+4
	mov	r4, r9
	b	.L1621
.L1624:
	mov	r0, #36
	ldr	r2, [fp, #-2088]
	mul	r3, r0, r9
	mov	r1, #0
	str	r1, [r2, r3]
	mov	r1, r8
	ldr	r2, .L1636
	add	r3, r2, r9
	ldrb	r0, [r3, #3870]	@ zero_extendqisi2
	bl	V2P_block
	cmp	r7, #0
	str	r0, [sp, #12]
	beq	.L1622
	bl	IsBlkInVendorPart
	cmp	r0, #0
	bne	.L1623
.L1622:
	ldr	r0, [sp, #12]
	bl	FtlBbmIsBadBlock
	cmp	r0, #0
	bne	.L1623
	ldr	r3, [fp, #-2088]
	mov	r0, #36
	ldr	r1, [sp, #12]
	mla	r3, r0, r4, r3
	add	r2, r6, r1, asl #10
	ldr	r1, .L1636
	add	r0, r0, #3888
	str	r2, [r3, #4]
	ldr	r2, [fp, #-960]
	str	r2, [r3, #8]
	ldrh	r2, [r1, r0]
	mul	r2, r2, r4
	add	r4, r4, #1
	uxth	r4, r4
	add	r1, r2, #3
	cmp	r2, #0
	movlt	r2, r1
	ldr	r1, [fp, #-956]
	bic	r2, r2, #3
	add	r2, r1, r2
	str	r2, [r3, #12]
.L1623:
	add	r9, r9, #1
	uxth	r9, r9
.L1621:
	ldr	r0, .L1636
	movw	r2, #3844
	ldrh	r3, [r0, r2]
	cmp	r3, r9
	bhi	.L1624
	cmp	r4, #0
	beq	.L1617
	ldr	r9, .L1636+4
	mov	r3, #1
	mov	r1, r4
	mov	r2, sl
	mov	fp, #0
	ldr	r0, [r9, #-2088]
	bl	FlashProgPages
	mov	r3, #36
	mul	r3, r3, r4
	str	r3, [sp, #12]
	b	.L1626
.L1629:
	ldr	r2, [r9, #-2088]
	add	r3, r2, fp
	ldr	r2, [r2, fp]
	cmn	r2, #1
	bne	.L1627
	ldr	r0, [r3, #4]
	add	r5, r5, #1
	ubfx	r0, r0, #10, #16
	uxth	r5, r5
	bl	FtlBbmMapBadBlock
	b	.L1628
.L1627:
	cmp	r7, #0
	beq	.L1628
	ldr	r0, [r3, #4]
	mov	r1, #1
	ubfx	r0, r0, #10, #16
	bl	FtlFreeSysBlkQueueIn
.L1628:
	add	fp, fp, #36
.L1626:
	ldr	r2, [sp, #12]
	cmp	fp, r2
	bne	.L1629
	ldr	r3, [sp, #8]
	ldr	r0, [sp, #4]
	add	r6, r6, r3
	uxth	r6, r6
	cmp	r6, r0
	bcc	.L1630
	cmp	r8, #63
	bhi	.L1617
	ldr	r3, .L1636+4
	mov	r1, sl
	mov	r2, r4
	ldr	r0, [r3, #-2088]
	bl	FlashEraseBlocks
.L1617:
	mov	r0, r5
	add	sp, sp, #20
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, sl, fp, pc}
.L1637:
	.align	2
.L1636:
	.word	.LANCHOR0
	.word	.LANCHOR2
	.fnend
	.size	FtlLowFormatEraseBlock, .-FtlLowFormatEraseBlock
	.align	2
	.type	FlashTestBlk.part.19, %function
FlashTestBlk.part.19:
	.fnstart
	@ args = 0, pretend = 0, frame = 104
	@ frame_needed = 0, uses_anonymous_args = 0
.L1639:
	stmfd	sp!, {r4, r5, lr}
	.save {r4, r5, lr}
	.pad #108
	sub	sp, sp, #108
	ldr	r4, .L1641
	mov	r5, r0
	mov	r1, #165
	add	r0, sp, #40
	mov	r2, #32
	str	r0, [sp, #16]
	ldr	r3, [r4, #116]
	mov	r5, r5, asl #10
	str	r3, [sp, #12]
	bl	ftl_memset
	ldr	r0, [r4, #116]
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
	cmn	r4, #1
	mov	r2, #1
	movne	r4, #0
	bl	FlashEraseBlocks
	mov	r0, r4
	add	sp, sp, #108
	ldmfd	sp!, {r4, r5, pc}
.L1642:
	.align	2
.L1641:
	.word	.LANCHOR2
	.fnend
	.size	FlashTestBlk.part.19, .-FlashTestBlk.part.19
	.align	2
	.global	FlashTestBlk
	.type	FlashTestBlk, %function
FlashTestBlk:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	@ link register save eliminated.
	ldr	r3, .L1645
	ldr	r3, [r3, #132]
	cmp	r0, r3
	bcc	.L1644
	b	FlashTestBlk.part.19
.L1644:
	mov	r0, #0
	bx	lr
.L1646:
	.align	2
.L1645:
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
	stmfd	sp!, {r4, r5, r6, r7, r8, r9, sl, fp, lr}
	.save {r4, r5, r6, r7, r8, r9, sl, fp, lr}
	.pad #76
	sub	sp, sp, #76
	ldr	r4, .L1675
	movw	r1, #2214
	ldr	r3, .L1675+4
	mov	r5, #0
	ldr	r0, .L1675+8
	ldr	r2, [r4, #148]
	ldrh	r9, [r3, r1]
	mov	r1, #1
	str	r2, [sp, #28]
	movw	r2, #2212
	ldrh	r2, [r3, r2]
	mul	r9, r9, r2
	ldr	r2, [r3, #2776]
	ldrb	r2, [r2, #24]	@ zero_extendqisi2
	uxth	r9, r9
	str	r2, [sp, #12]
	ldrh	r2, [r3, #4]
	ldrb	r3, [r3, #0]	@ zero_extendqisi2
	cmp	r3, #1
	str	r2, [sp, #8]
	moveq	r3, r2, asl #1
	uxtheq	r3, r3
	streq	r3, [sp, #8]
	bl	printk
	ldr	r0, [r4, #148]
	mov	r1, #0
	mov	r2, #4096
	bl	ftl_memset
	mov	r3, r9, lsr #4
	str	r3, [sp, #20]
	sub	r3, r9, #1
	ldr	r4, .L1675+4
	uxth	r3, r3
	str	r3, [sp, #24]
	b	.L1649
.L1671:
	ldr	r3, .L1675
	add	r3, r3, r5, asl #1
	ldrh	r6, [r3, #208]
	cmp	r6, #0
	bne	.L1650
	movw	r3, #2220
	ldr	r0, [r4, #3780]
	ldrh	r2, [r4, r3]
	mov	r1, r6
	mov	r7, r6
	mov	sl, r6
	add	r8, r4, r5, asl #2
	mov	r2, r2, asl #9
	bl	ftl_memset
	add	r3, r4, r5
	ldrb	fp, [r3, #3768]	@ zero_extendqisi2
	b	.L1651
.L1659:
	ldr	r2, [sp, #12]
	mvn	r3, #0
	strb	r3, [sp, #34]
	tst	r2, #1
	strb	r3, [sp, #35]
	beq	.L1652
	ldr	r3, [r8, #2740]
	add	r2, sp, #34
	mov	r0, fp
	add	r3, r6, r3
	str	r3, [sp, #4]
	mov	r1, r3
	bl	FlashReadSpare
	ldrb	r2, [r4, #0]	@ zero_extendqisi2
	ldr	r3, [sp, #4]
	cmp	r2, #1
	bne	.L1652
	ldr	r1, [r4, #4]
	add	r2, sp, #35
	mov	r0, fp
	add	r1, r3, r1
	bl	FlashReadSpare
	ldrb	r3, [sp, #34]	@ zero_extendqisi2
	ldrb	r2, [sp, #35]	@ zero_extendqisi2
	and	r3, r2, r3
	strb	r3, [sp, #34]
.L1652:
	ldr	r3, [sp, #12]
	tst	r3, #2
	beq	.L1653
	ldr	r3, [r4, #2776]
	mov	r0, fp
	add	r2, sp, #35
	ldrh	r1, [r3, #10]
	ldr	r3, [r8, #2740]
	sub	r1, r1, #1
	add	r1, r1, r3
	add	r1, r1, r6
	bl	FlashReadSpare
.L1653:
	ldr	r3, [r4, #2776]
	ldrb	r3, [r3, #7]	@ zero_extendqisi2
	cmp	r3, #1
	cmpne	r3, #8
	ldrb	r3, [sp, #34]	@ zero_extendqisi2
	bne	.L1654
	cmp	r3, #0
	beq	.L1673
	ldrb	r0, [sp, #35]	@ zero_extendqisi2
	rsbs	r0, r0, #1
	movcc	r0, #0
	b	.L1655
.L1654:
	cmp	r3, #255
	bne	.L1673
	ldrb	r0, [sp, #35]	@ zero_extendqisi2
	subs	r0, r0, #255
	movne	r0, #1
	b	.L1655
.L1673:
	mov	r0, #1
.L1655:
	ldr	r2, [sp, #12]
	tst	r2, #4
	beq	.L1656
	ldr	r1, [r8, #2740]
	mov	r0, fp
	add	r1, r6, r1
	bl	SandiskProgTestBadBlock
.L1656:
	cmp	r0, #0
	beq	.L1657
	mov	r1, r5
	mov	r2, r7
	ldr	r0, .L1675+12
	add	sl, sl, #1
	bl	printk
	ldr	r3, [sp, #16]
	and	r0, r7, #31
	mov	ip, #1
	uxth	sl, sl
	mov	r2, r3, lsr #5
	ldr	r3, [r4, #3780]
	ldr	r1, [r3, r2, asl #2]
	orr	r1, r1, ip, asl r0
	str	r1, [r3, r2, asl #2]
	ldr	r2, [sp, #20]
	ldrb	r3, [r4, #3766]	@ zero_extendqisi2
	mul	r3, r3, r2
	cmp	sl, r3
	bgt	.L1658
.L1657:
	ldr	r3, [sp, #8]
	add	r7, r7, #1
	add	r6, r6, r3
.L1651:
	uxth	r3, r7
	str	r3, [sp, #16]
	cmp	r3, r9
	bcc	.L1659
.L1658:
	mov	r2, sl
	ldr	r0, .L1675+16
	mov	r1, r5
	bl	printk
	ldrb	r3, [r4, #3766]	@ zero_extendqisi2
	ldr	r2, [sp, #20]
	mul	r3, r3, r2
	cmp	sl, r3
	blt	.L1660
	movw	r3, #2220
	ldr	r0, [r4, #3780]
	ldrh	r2, [r4, r3]
	mov	r1, #0
	mov	r2, r2, asl #9
	bl	ftl_memset
.L1660:
	cmp	r5, #0
	bne	.L1661
	ldr	r3, .L1675
	mov	r8, r5
	mov	r7, r5
	mov	sl, #1
	ldrh	fp, [r3, #132]
	b	.L1662
.L1664:
	mov	r0, r6
	bl	FlashTestBlk
	cmp	r0, #0
	beq	.L1663
	mov	r1, r6
	ldr	r0, .L1675+20
	bl	printk
	ldr	r3, [r4, #3780]
	mov	r2, r6, lsr #5
	add	r7, r7, #1
	and	r6, r6, #31
	ldr	r1, [r3, r2, asl #2]
	uxth	r7, r7
	orr	r6, r1, sl, asl r6
	str	r6, [r3, r2, asl #2]
.L1663:
	add	r8, r8, #1
	uxth	r8, r8
.L1662:
	add	r6, r8, fp
	ldrb	r3, [r4, #1]	@ zero_extendqisi2
	uxth	r6, r6
	cmp	r3, r6
	bhi	.L1664
	ldr	r6, [sp, #24]
	sub	sl, r9, #50
	mov	r8, #1
	b	.L1665
.L1667:
	mov	r0, r6
	bl	FlashTestBlk
	cmp	r0, #0
	beq	.L1666
	mov	r1, r6
	ldr	r0, .L1675+20
	bl	printk
	ldr	r3, [r4, #3780]
	mov	r2, r6, lsr #5
	and	r0, r6, #31
	ldr	r1, [r3, r2, asl #2]
	orr	r1, r1, r8, asl r0
	str	r1, [r3, r2, asl #2]
.L1666:
	sub	r6, r6, #1
	uxth	r6, r6
.L1665:
	cmp	r6, sl
	bgt	.L1667
	ldr	r3, .L1675
	ldrb	r2, [r4, #1]	@ zero_extendqisi2
	ldr	r3, [r3, #132]
	rsb	r3, r3, r2
	cmp	r7, r3
	bcc	.L1661
	movw	r3, #2220
	ldr	r0, [r4, #3780]
	ldrh	r2, [r4, r3]
	mov	r1, #0
	mov	r2, r2, asl #9
	bl	ftl_memset
.L1661:
	mul	r8, r9, r5
	ldr	r7, .L1675
	ldr	r6, [sp, #24]
	ldr	fp, [sp, #28]
	add	sl, r7, r5, asl #1
.L1668:
	ldr	r0, .L1675+24
	mov	r1, r5
	mov	r2, r6
	bl	printk
	ldr	r3, [r4, #3780]
	b	.L1669
.L1670:
	sub	r6, r6, #1
	uxth	r6, r6
.L1669:
	mov	r1, r6, lsr #5
	and	r2, r6, #31
	ldr	r1, [r3, r1, asl #2]
	mov	r2, r1, lsr r2
	ands	r2, r2, #1
	bne	.L1670
	ldr	r3, .L1675+28
	mov	r1, #1
	strh	r6, [sl, #208]	@ movhi
	add	r0, sp, #36
	strh	r6, [fp, #2]	@ movhi
	strh	r3, [fp, #0]	@ movhi
	ldr	r3, [r4, #3780]
	strh	r2, [fp, #8]	@ movhi
	mov	r2, r1
	str	r3, [sp, #44]
	ldr	r3, [r7, #148]
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
	bne	.L1668
.L1650:
	add	r5, r5, #1
	uxtb	r5, r5
.L1649:
	ldrb	r3, [r4, #3766]	@ zero_extendqisi2
	cmp	r3, r5
	bhi	.L1671
	add	sp, sp, #76
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, sl, fp, pc}
.L1676:
	.align	2
.L1675:
	.word	.LANCHOR2
	.word	.LANCHOR0
	.word	.LC107
	.word	.LC108
	.word	.LC109
	.word	.LC110
	.word	.LC111
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
	sub	r3, r3, #1
	cmp	r2, r3
	blt	.L1678
	uxth	r0, r0
	mov	r0, r0, asl #1
	ldrh	r7, [r5, r0]
	cmp	r7, #0
	beq	.L1678
	ldr	r3, [r4, #32]
	cmp	r3, #0
	bne	.L1678
	mov	r2, #1
	ldr	r1, .L1685
	str	r2, [r4, #32]
	strh	r3, [r5, r0]	@ movhi
	ldrh	r3, [r4, #8]
	ldrh	r2, [r4, #2]
	sub	r3, r3, #1
	strh	r3, [r4, #8]	@ movhi
	movw	r3, #3914
	ldrh	r3, [r1, r3]
	cmp	r2, r3
	bcc	.L1679
	mov	r0, r4
	bl	ftl_map_blk_alloc_new_blk
.L1679:
	mov	r5, #0
	ldr	r6, .L1685+4
	mov	r9, r5
	b	.L1680
.L1683:
	ldr	r3, [r8, r5, asl #2]
	mov	sl, r5, asl #2
	cmp	r7, r3, lsr #10
	bne	.L1681
	ldr	r3, [r6, #-972]
	mov	r1, #1
	ldr	r0, .L1685+8
	mov	r2, r1
	str	r3, [r6, #176]
	ldr	r3, [r6, #-952]
	str	r3, [r6, #180]
	ldr	r3, [r8, r5, asl #2]
	str	r3, [r6, #172]
	bl	FlashReadPages
	ldr	r3, [r6, #168]
	cmn	r3, #1
	streq	r9, [r8, sl]
	beq	.L1681
	mov	r0, r4
	mov	r1, r5
	ldr	r2, [r6, #176]
	bl	FtlMapWritePage
.L1681:
	add	r5, r5, #1
	uxth	r5, r5
.L1680:
	ldrh	r3, [r4, #6]
	cmp	r3, r5
	bhi	.L1683
	mov	r0, r7
	mov	r1, #1
	bl	FtlFreeSysBlkQueueIn
	mov	r3, #0
	str	r3, [r4, #32]
.L1678:
	ldr	r1, .L1685
	movw	r3, #3914
	ldrh	r2, [r4, #2]
	ldrh	r3, [r1, r3]
	cmp	r2, r3
	bcc	.L1684
	mov	r0, r4
	bl	ftl_map_blk_alloc_new_blk
.L1684:
	mov	r0, #0
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, sl, pc}
.L1686:
	.align	2
.L1685:
	.word	.LANCHOR0
	.word	.LANCHOR2
	.word	.LANCHOR2+168
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
	bne	.L1688
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
	b	.L1689
.L1688:
	mov	r3, r3, asl #1
	ldr	r2, [r0, #28]
	mov	r1, #255
	ldrh	r6, [r5, r3]
	ldr	r5, .L1693
	ldrh	r3, [r0, #2]
	orr	r3, r3, r6, asl #10
	str	r3, [r5, #172]
	ldr	r3, [r5, #-2076]
	str	r3, [r5, #176]
	ldr	r3, [r5, #-952]
	str	r3, [r5, #180]
	str	r2, [r3, #4]
	ldr	r2, .L1693+4
	strh	r2, [r3, #8]	@ movhi
	ldrh	r2, [r0, #4]
	strh	r6, [r3, #2]	@ movhi
	strh	r2, [r3, #0]	@ movhi
	movw	r3, #3914
	ldr	r2, .L1693+8
	ldr	r0, [r5, #-2076]
	ldrh	r2, [r2, r3]
	mov	r2, r2, asl #3
	bl	ftl_memset
	mov	r2, #0
	mov	r3, r2
	b	.L1690
.L1692:
	ldr	r1, [r7, r3, asl #2]
	cmp	r6, r1, lsr #10
	bne	.L1691
	add	r2, r2, #1
	ldr	r1, [r5, #-2076]
	uxth	r2, r2
	str	r3, [r1, r2, asl #3]
	ldr	r1, [r5, #-2076]
	ldr	r0, [r7, r3, asl #2]
	add	r1, r1, r2, asl #3
	str	r0, [r1, #4]
.L1691:
	add	r3, r3, #1
	uxth	r3, r3
.L1690:
	ldrh	r1, [r4, #6]
	cmp	r1, r3
	bhi	.L1692
	mov	r1, #1
	mov	r3, #0
	ldr	r0, .L1693+12
	mov	r2, r1
	bl	FlashProgPages
	ldrh	r3, [r4, #2]
	mov	r0, r4
	add	r3, r3, #1
	strh	r3, [r4, #2]	@ movhi
	bl	ftl_map_blk_gc
.L1689:
	mov	r0, #0
	ldmfd	sp!, {r3, r4, r5, r6, r7, pc}
.L1694:
	.align	2
.L1693:
	.word	.LANCHOR2
	.word	-1291
	.word	.LANCHOR0
	.word	.LANCHOR2+168
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
	ldr	r5, .L1702
	mov	r6, r1
	ldr	r9, .L1702+4
	mov	r8, r2
	movw	fp, #3914
.L1701:
	ldr	r3, [r5, #-1864]
	add	r3, r3, #1
	str	r3, [r5, #-1864]
	ldrh	r3, [r9, fp]
	ldrh	r2, [r4, #2]
	sub	r3, r3, #1
	cmp	r2, r3
	bge	.L1696
	ldrh	r2, [r4, #0]
	movw	r3, #65535
	cmp	r2, r3
	bne	.L1697
.L1696:
	mov	r0, r4
	bl	Ftl_write_map_blk_to_last_page
.L1697:
	ldrh	r3, [r4, #0]
	mov	r1, #0
	ldr	r2, [r4, #12]
	ldr	r0, [r5, #-952]
	mov	r3, r3, asl #1
	ldr	r7, .L1702
	ldrh	sl, [r2, r3]
	mov	r2, #16
	ldrh	r3, [r4, #2]
	str	r8, [r5, #176]
	orr	r3, r3, sl, asl #10
	str	r0, [r5, #180]
	str	r3, [r5, #172]
	bl	ftl_memset
	ldr	r3, [r5, #180]
	mov	r1, #1
	ldr	r2, [r4, #28]
	add	r0, r7, #168
	strh	r6, [r3, #8]	@ movhi
	str	r2, [r3, #4]
	ldrh	r2, [r4, #4]
	strh	sl, [r3, #2]	@ movhi
	strh	r2, [r3, #0]	@ movhi
	mov	r3, r1
	mov	r2, r1
	bl	FlashProgPages
	ldrh	r3, [r4, #2]
	add	r3, r3, #1
	uxth	r3, r3
	strh	r3, [r4, #2]	@ movhi
	cmp	r3, #1
	beq	.L1701
	ldr	r3, [r5, #168]
	cmn	r3, #1
	beq	.L1701
	ldr	r2, [r7, #172]
	mov	r0, #0
	ldr	r3, [r4, #24]
	str	r2, [r3, r6, asl #2]
	ldmfd	sp!, {r3, r4, r5, r6, r7, r8, r9, sl, fp, pc}
.L1703:
	.align	2
.L1702:
	.word	.LANCHOR2
	.word	.LANCHOR0
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
	ldr	r5, .L1705
	mul	r4, r4, r0
	sub	r0, r5, #1088
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
.L1706:
	.align	2
.L1705:
	.word	.LANCHOR2
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
	ldr	r3, .L1716
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
	ldr	r3, .L1716+4
	uxth	r7, r7
	ldr	sl, [r3, #-1900]
	mov	r3, #0
	mov	r5, r3
	b	.L1708
.L1714:
	add	r3, r3, #12
	add	r1, sl, r3
	ldrh	r1, [r1, #-12]
	cmp	r1, r6
	bne	.L1709
.L1710:
	cmp	r8, #0
	ldr	r3, .L1716+4
	mov	r2, #12
	bne	.L1711
	ldr	r3, [r3, #-1900]
	mla	r2, r2, r5, r3
	ldr	r3, [r2, #8]
	ldr	r3, [r3, r7, asl #2]
	str	r3, [r4, #0]
	b	.L1712
.L1711:
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
	ldr	r2, .L1716+8
	strh	r6, [r3, r2]	@ movhi
.L1712:
	ldr	r3, .L1716+4
	mov	r2, #12
	mov	r0, #0
	ldr	r3, [r3, #-1900]
	mla	r5, r2, r5, r3
	ldr	r3, [r5, #4]
	cmn	r3, #1
	addne	r3, r3, #1
	strne	r3, [r5, #4]
	ldmfd	sp!, {r3, r4, r5, r6, r7, r8, sl, pc}
.L1709:
	add	r5, r5, #1
	uxth	r5, r5
.L1708:
	cmp	r5, r2
	bne	.L1714
	bl	select_l2p_ram_region
	mov	r3, #12
	mul	r3, r3, r0
	mov	r5, r0
	add	r2, sl, r3
	ldrh	r1, [sl, r3]
	movw	r3, #65535
	cmp	r1, r3
	beq	.L1715
	ldr	r3, [r2, #4]
	cmp	r3, #0
	bge	.L1715
	bl	flush_l2p_region
.L1715:
	mov	r0, r6
	mov	r1, r5
	bl	load_l2p_region
	b	.L1710
.L1717:
	.align	2
.L1716:
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
	ldr	r3, .L1724
	ldr	r1, [r3, #-2064]
	mov	r5, r0, asl #1
	ldrh	r2, [r1, r5]
	cmp	r2, #0
	addne	r2, r2, #1
	strneh	r2, [r1, r5]	@ movhi
	bne	.L1720
	ldr	r4, [r3, #-2052]
	cmp	r4, #0
	beq	.L1720
	ldr	r1, .L1724+4
	mov	ip, #6
	ldrh	lr, [r3, r1]
	ldr	r3, [r3, #-2072]
	ldr	r1, .L1724+8
	rsb	r4, r3, r4
	mov	r4, r4, asr #1
	mul	r4, r1, r4
	movw	r1, #65535
	uxth	r4, r4
	b	.L1721
.L1723:
	cmp	r4, r0
	bne	.L1722
	mov	r1, r4
	ldr	r0, .L1724+12
	bl	List_remove_node
	ldr	r6, .L1724
	ldr	r3, .L1724+4
	mov	r0, r4
	ldrh	r2, [r6, r3]
	sub	r2, r2, #1
	strh	r2, [r6, r3]	@ movhi
	bl	INSERT_DATA_LIST
	ldr	r3, [r6, #-2064]
	ldrh	r2, [r3, r5]
	add	r2, r2, #1
	strh	r2, [r3, r5]	@ movhi
	b	.L1720
.L1722:
	mul	r4, ip, r4
	ldrh	r4, [r3, r4]
	cmp	r4, r1
	beq	.L1720
	add	r2, r2, #1
	uxth	r2, r2
.L1721:
	cmp	r2, lr
	bne	.L1723
.L1720:
	mov	r0, r7
	add	r1, sp, #4
	mov	r2, #1
	bl	log2phys
	ldmfd	sp!, {r1, r2, r3, r4, r5, r6, r7, pc}
.L1725:
	.align	2
.L1724:
	.word	.LANCHOR2
	.word	-2048
	.word	-1431655765
	.word	.LANCHOR2-2052
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
	ldr	r5, .L1730
	sub	r6, r6, #1
	mov	r2, #0
	str	r2, [r0, #36]
	uxth	r6, r6
	ldr	r0, [r5, #-972]
	ldr	r2, [r5, #-952]
	str	r0, [r5, #176]
	str	r2, [r5, #180]
	ldr	r3, [r3, r6, asl #2]
	cmp	r3, #0
	str	r3, [r5, #172]
	beq	.L1728
	mov	r1, #1
	add	r0, r5, #168
	mov	r2, r1
	bl	FlashReadPages
	b	.L1729
.L1728:
	ldr	r2, .L1730+4
	movw	r3, #3922
	mov	r1, #255
	ldrh	r2, [r2, r3]
	bl	ftl_memset
.L1729:
	mov	r0, r4
	mov	r1, r6
	ldr	r2, [r5, #176]
	ldmfd	sp!, {r4, r5, r6, lr}
	b	FtlMapWritePage
.L1731:
	.align	2
.L1730:
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
	ldr	r3, .L1742
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
	bhi	.L1733
	mov	r8, r0, lsr r8
	mov	r7, #0
	ldr	sl, .L1742+4
	mov	ip, r8, asl #2
	str	ip, [sp, #8]
	b	.L1734
.L1739:
	ldr	r1, .L1742
	movw	r2, #3918
	ldr	ip, [sp, #8]
	mov	r0, r6
	ldr	r3, [sl, #-920]
	ldrh	fp, [r1, r2]
	ldr	r3, [r3, ip]
	mov	r1, fp
	str	r3, [sp, #4]
	bl	__aeabi_uidivmod
	ldr	r3, [sp, #4]
	ldr	r2, .L1742+4
	uxth	r1, r1
	str	r1, [sp, #12]
	rsb	r5, r1, fp
	uxth	r5, r5
	cmp	r5, r4
	uxthhi	r5, r4
	cmp	r3, #0
	beq	.L1736
	cmp	r5, fp
	beq	.L1736
	str	r3, [sp, #24]
	mov	r1, #1
	ldr	r3, [r2, #-968]
	add	r0, sp, #20
	mov	r2, r1
	str	r3, [sp, #28]
	mov	r3, #0
	str	r3, [sp, #32]
	bl	FlashReadPages
	b	.L1737
.L1736:
	ldr	ip, .L1742
	movw	r3, #3922
	ldr	r0, [r2, #-968]
	mov	r1, #0
	ldrh	r2, [ip, r3]
	bl	ftl_memset
.L1737:
	ldr	r3, [sp, #12]
	mov	fp, r5, asl #9
	ldr	r0, [sl, #-968]
	mov	r1, r9
	mov	r2, fp
	rsb	r4, r5, r4
	add	r0, r0, r3, asl #9
	add	r6, r6, r5
	bl	memcpy
	mov	r1, r8
	ldr	r0, .L1742+8
	add	r8, r8, #1
	ldr	r2, [sl, #-968]
	add	r9, r9, fp
	bl	FtlMapWritePage
	ldr	ip, [sp, #8]
	add	ip, ip, #4
	str	ip, [sp, #8]
	cmn	r0, #1
	moveq	r7, r0
.L1734:
	cmp	r4, #0
	bne	.L1739
.L1733:
	mov	r0, r7
	add	sp, sp, #60
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, sl, fp, pc}
.L1743:
	.align	2
.L1742:
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
	ldr	r3, .L1746
	ldr	r2, .L1746+4
	ldr	r1, [r3, #-1704]
	cmp	r1, r2
	bxne	lr
	ldr	r2, .L1746+8
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
	ldr	r2, [r3, #-1004]
	str	r2, [r3, #-1640]
	ldr	r2, .L1746+12
	b	FtlVendorPartWrite
.L1747:
	.align	2
.L1746:
	.word	.LANCHOR2
	.word	1179929683
	.word	1342177334
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
	ldr	r1, .L1755
	stmfd	sp!, {r3, lr}
	.save {r3, lr}
	ldr	r3, [r1, #-1884]
	cmp	r3, #0
	moveq	r2, #32
	beq	.L1749
	ldr	r2, [r1, #-1824]
	cmp	r2, #29
	movls	r2, #4
	movhi	r2, #32
.L1749:
	mov	r3, #264
	ldrh	r1, [r1, r3]
	cmp	r1, #31
	addls	r1, r1, #1
	ldrls	r2, .L1755
	strlsh	r1, [r2, r3]	@ movhi
	movls	r2, #1
	cmp	r0, #0
	ldr	r3, .L1755
	bne	.L1751
	ldr	r1, [r3, #-936]
	ldr	r0, [r1, #20]
	ldr	r1, [r1, #16]
	add	r2, r2, r0
	cmp	r1, r2
	bcc	.L1752
.L1751:
	ldr	r2, [r3, #-936]
	mov	r0, #64
	ldr	r1, [r2, #16]
	str	r1, [r2, #20]
	ldr	r1, .L1755+4
	str	r1, [r2, #0]
	ldr	r1, .L1755+8
	ldr	r2, [r3, #-936]
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
.L1752:
	mov	r0, #0
	ldmfd	sp!, {r3, pc}
.L1756:
	.align	2
.L1755:
	.word	.LANCHOR2
	.word	1112818501
	.word	-940
	.fnend
	.size	FtlEctTblFlush, .-FtlEctTblFlush
	.align	2
	.type	FtlReadRefresh.part.14, %function
FtlReadRefresh.part.14:
	.fnstart
	@ args = 0, pretend = 0, frame = 40
	@ frame_needed = 0, uses_anonymous_args = 0
	stmfd	sp!, {r4, r5, r6, lr}
	.save {r4, r5, r6, lr}
	.pad #40
	sub	sp, sp, #40
	ldr	r4, .L1762
	mov	r5, #2048
	ldr	r6, .L1762+4
.L1760:
	ldr	r0, [r4, #-1620]
	ldr	r3, [r6, #3972]
	cmp	r0, r3
	bcs	.L1758
	mov	r2, #0
	mov	r1, sp
	bl	log2phys
	ldr	r2, [sp, #0]
	ldr	r3, [r4, #-1620]
	cmn	r2, #1
	add	r3, r3, #1
	str	r3, [r4, #-1620]
	beq	.L1759
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
	bne	.L1758
	ldr	r0, [sp, #0]
	ubfx	r0, r0, #10, #16
	bl	P2V_block_in_plane
	bl	FtlGcRefreshBlock
	b	.L1758
.L1759:
	subs	r5, r5, #1
	bne	.L1760
.L1758:
	mvn	r0, #0
	add	sp, sp, #40
	ldmfd	sp!, {r4, r5, r6, pc}
.L1763:
	.align	2
.L1762:
	.word	.LANCHOR2
	.word	.LANCHOR0
	.fnend
	.size	FtlReadRefresh.part.14, .-FtlReadRefresh.part.14
	.align	2
	.global	FtlReadRefresh
	.type	FtlReadRefresh, %function
FtlReadRefresh:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	@ link register save eliminated.
	ldr	r3, .L1773
	ldr	r0, .L1773+4
	ldr	r2, [r3, #-1624]
	cmp	r2, #0
	mov	r2, r3
	beq	.L1765
	ldr	r1, [r3, #-1620]
	ldr	r2, [r0, #3972]
	cmp	r1, r2
	bcs	.L1766
	b	FtlReadRefresh.part.14
.L1766:
	mov	r2, #0
	str	r2, [r3, #-1624]
	str	r2, [r3, #-1620]
	ldr	r2, [r3, #-1880]
	b	.L1772
.L1765:
	ldrb	r0, [r0, #80]	@ zero_extendqisi2
	ldr	r1, [r3, #-1828]
	cmp	r0, #0
	ldr	r3, [r3, #-1884]
	addeq	r1, r3, r1, lsr #10
	moveq	r3, #33554432
	moveq	r1, r3, asr r1
	ldr	r3, [r2, #-1628]
	ldr	r2, [r2, #-1880]
	movne	r1, #1048576
	add	r0, r2, #1048576
	cmp	r3, r0
	bhi	.L1769
	add	r3, r1, r3
	cmp	r3, r2
	bcs	.L1767
.L1769:
	ldr	r3, .L1773
	mov	r1, #1
	str	r1, [r3, #-1624]
	mov	r1, #0
	str	r1, [r3, #-1620]
.L1772:
	str	r2, [r3, #-1628]
.L1767:
	mov	r0, #0
	bx	lr
.L1774:
	.align	2
.L1773:
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
	ldrh	sl, [r0, #6]
	mov	r1, #0
	ldr	r8, [r0, #24]
	mov	r9, #0
	ldr	r5, .L1795
	str	r3, [sp, #16]
	mov	r2, sl, asl #2
	ldrh	r3, [r0, #8]
	ldr	r7, [r0, #12]
	mov	r0, r8
	str	r3, [sp, #8]
	bl	ftl_memset
	ldr	r3, [r5, #-2076]
	ldr	r6, [r5, #-952]
	str	r9, [r4, #32]
	str	r3, [r5, #176]
	mvn	r3, #0
	str	r6, [r5, #180]
	strh	r3, [r4, #0]	@ movhi
	strh	r3, [r4, #2]	@ movhi
	mov	r3, #1
	str	r3, [r4, #36]
	ldr	r3, [sp, #8]
	str	r9, [r4, #28]
	sub	r3, r3, #1
	str	r3, [sp, #12]
	b	.L1776
.L1790:
	ldr	r0, [sp, #12]
	cmp	r3, r0
	mov	r3, r3, asl #1
	bne	.L1777
	ldrh	r0, [r7, r3]
	mov	r1, #1
	add	fp, r7, r3
	bl	FtlGetLastWrittenPage
	ldr	r1, [sp, #12]
	mov	r7, #0
	ldr	r2, [sp, #16]
	strh	r9, [r4, #0]	@ movhi
	ldr	r5, .L1795
	uxth	r0, r0
	add	r3, r0, #1
	strh	r3, [r4, #2]	@ movhi
	ldr	r3, [r2, r1, asl #2]
	sxth	r9, r0
	str	r7, [r5, #176]
	add	r9, r9, #1
	str	r3, [r4, #28]
	b	.L1778
.L1780:
	ldrh	r2, [fp, #0]
	mov	r1, #1
	ldr	r0, .L1795+4
	orr	r3, r3, r2, asl #10
	mov	r2, r1
	str	r3, [r5, #172]
	bl	FlashReadPages
	ldr	r3, [r5, #168]
	cmn	r3, #1
	beq	.L1779
	ldrh	r3, [r6, #8]
	cmp	r3, sl
	bcs	.L1779
	ldrh	r2, [r4, #4]
	ldrh	r1, [r6, #0]
	cmp	r1, r2
	ldreq	r2, [r5, #172]
	streq	r2, [r8, r3, asl #2]
.L1779:
	add	r7, r7, #1
	uxth	r7, r7
.L1778:
	sxth	r3, r7
	cmp	r3, r9
	blt	.L1780
	b	.L1781
.L1777:
	ldr	r2, [r5, #-2076]
	add	r0, r7, r3
	ldr	fp, .L1795+8
	str	r0, [sp, #20]
	str	r2, [r5, #176]
	ldrh	r1, [r7, r3]
	movw	r3, #3914
	ldrh	r2, [fp, r3]
	ldr	r0, .L1795+4
	sub	r2, r2, #1
	str	r3, [sp, #0]
	orr	r2, r2, r1, asl #10
	mov	r1, #1
	str	r2, [r5, #172]
	mov	r2, r1
	bl	FlashReadPages
	ldr	r2, [r5, #168]
	ldr	r3, [sp, #0]
	cmn	r2, #1
	beq	.L1782
	ldrh	r1, [r6, #0]
	ldrh	r2, [r4, #4]
	cmp	r1, r2
	bne	.L1782
	ldrh	r1, [r6, #8]
	movw	r2, #64245
	cmp	r1, r2
	bne	.L1782
	b	.L1794
.L1785:
	ldr	r0, [r5, #-2076]
	mov	ip, r1, asl #3
	add	r2, r2, #1
	ldr	r1, [r0, r1, asl #3]
	uxth	r2, r2
	uxth	r1, r1
	cmp	r1, sl
	addcc	r0, r0, ip
	ldrcc	r0, [r0, #4]
	strcc	r0, [r8, r1, asl #2]
	b	.L1783
.L1794:
	mov	r2, #0
.L1783:
	ldrh	r0, [fp, r3]
	sxth	r1, r2
	sub	r0, r0, #1
	cmp	r1, r0
	blt	.L1785
	b	.L1786
.L1782:
	ldr	r1, .L1795
	mov	fp, #0
	ldr	r3, .L1795+8
	add	ip, r1, #168
	str	fp, [r1, #176]
	b	.L1787
.L1789:
	ldr	r0, [sp, #20]
	ldrh	r1, [r0, #0]
	mov	r0, ip
	stmia	sp, {r3, ip}
	orr	r2, r2, r1, asl #10
	mov	r1, #1
	str	r2, [r5, #172]
	mov	r2, r1
	bl	FlashReadPages
	ldr	r2, [r5, #168]
	ldmia	sp, {r3, ip}
	cmn	r2, #1
	beq	.L1788
	ldrh	r2, [r6, #8]
	cmp	r2, sl
	bcs	.L1788
	ldrh	r1, [r4, #4]
	ldrh	r0, [r6, #0]
	cmp	r0, r1
	ldreq	r1, [r5, #172]
	streq	r1, [r8, r2, asl #2]
.L1788:
	add	fp, fp, #1
	uxth	fp, fp
.L1787:
	movw	r1, #3914
	sxth	r2, fp
	ldrh	r1, [r3, r1]
	cmp	r2, r1
	blt	.L1789
.L1786:
	add	r9, r9, #1
	uxth	r9, r9
.L1776:
	ldr	r1, [sp, #8]
	sxth	r3, r9
	cmp	r3, r1
	blt	.L1790
.L1781:
	mov	r0, r4
	bl	ftl_free_no_use_map_blk
	ldr	r1, .L1795+8
	movw	r3, #3914
	ldrh	r2, [r4, #2]
	ldrh	r3, [r1, r3]
	cmp	r2, r3
	bne	.L1791
	mov	r0, r4
	bl	ftl_map_blk_alloc_new_blk
.L1791:
	ldrh	r2, [r4, #8]
	ldrh	r3, [r4, #10]
	cmp	r2, r3
	bcc	.L1792
	mov	r0, r4
	bl	ftl_map_blk_gc
.L1792:
	mov	r0, #0
	add	sp, sp, #28
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, sl, fp, pc}
.L1796:
	.align	2
.L1795:
	.word	.LANCHOR2
	.word	.LANCHOR2+168
	.word	.LANCHOR0
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
	ldr	r0, .L1798
	bl	FtlMapTblRecovery
	mov	r0, #0
	ldmfd	sp!, {r3, pc}
.L1799:
	.align	2
.L1798:
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
	movw	r2, #3932
	ldr	r3, .L1801
	ldr	r0, .L1801+4
	ldrh	r2, [r3, r2]
	strh	r2, [r0, #234]	@ movhi
	ldr	r2, .L1801+8
	strh	r2, [r0, #228]	@ movhi
	movw	r2, #3956
	ldrh	r2, [r3, r2]
	strh	r2, [r0, #232]	@ movhi
	movw	r2, #3934
	ldrh	r2, [r3, r2]
	ldr	r3, [r3, #3960]
	strh	r2, [r0, #230]	@ movhi
	str	r3, [r0, #236]
	ldr	r3, [r0, #-924]
	str	r3, [r0, #240]
	ldr	r3, [r0, #-928]
	str	r3, [r0, #244]
	ldr	r3, [r0, #-920]
	str	r3, [r0, #248]
	add	r0, r0, #224
	bl	FtlMapTblRecovery
	mov	r0, #0
	ldmfd	sp!, {r3, pc}
.L1802:
	.align	2
.L1801:
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
	.pad #68
	sub	sp, sp, #68
	ldrh	r3, [r0, #2]
	mov	r4, r0
	ldr	r2, .L1888
	ldrb	fp, [r0, #6]	@ zero_extendqisi2
	str	r3, [sp, #20]
	movw	r3, #3912
	ldrh	r3, [r2, r3]
	ldr	ip, [sp, #20]
	str	fp, [sp, #32]
	cmp	r3, ip
	mov	r3, #0
	streqh	r3, [r0, #4]	@ movhi
	moveq	r2, r3	@ movhi
	ldrneh	r0, [r0, #16]
	movwne	r2, #65535
	bne	.L1806
	b	.L1885
.L1807:
	add	r3, r3, #1
	uxth	r3, r3
	add	r1, r4, r3, asl #1
	ldrh	r0, [r1, #16]
.L1806:
	cmp	r0, r2
	beq	.L1807
	ldrb	r1, [r4, #8]	@ zero_extendqisi2
	cmp	r1, #1
	bne	.L1808
	bl	FtlGetLastWrittenPage
	cmn	r0, #1
	mov	r7, r0
	beq	.L1809
	ldr	r3, .L1888
	ldrb	r2, [r3, #80]	@ zero_extendqisi2
	cmp	r2, #0
	bne	.L1878
	add	r3, r3, r0, asl #1
	movw	r2, #2228
	ldrh	r5, [r3, r2]
	b	.L1872
.L1808:
	mov	r1, #0
	bl	FtlGetLastWrittenPage
	cmn	r0, #1
	mov	r7, r0
	beq	.L1809
.L1878:
	mov	r5, r7
.L1872:
	ldr	r3, .L1888
	movw	r2, #3844
	mov	r6, #0
	str	r4, [sp, #40]
	movw	ip, #65535
	ldrh	r9, [r3, r2]
	ldr	r2, .L1888+4
	ldr	sl, [r2, #-984]
	ldr	r8, [r2, #-1164]
	movw	r2, #3924
	ldrh	lr, [r3, r2]
	mov	r2, r4
	mov	r3, r6
	b	.L1811
.L1809:
	mov	r3, #0
	strh	r3, [r4, #2]	@ movhi
	mov	r2, r3	@ movhi
.L1885:
	strb	r2, [r4, #6]
	b	.L1805
.L1813:
	ldrh	r0, [r2, #16]
	cmp	r0, ip
	beq	.L1812
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
.L1812:
	add	r3, r3, #1
	add	r2, r2, #2
	uxth	r3, r3
.L1811:
	cmp	r3, r9
	bne	.L1813
	ldrb	r3, [r4, #8]	@ zero_extendqisi2
	cmp	r3, #1
	movne	r3, #0
	bne	.L1879
	ldr	r3, .L1888
	ldrb	r3, [r3, #80]	@ zero_extendqisi2
	adds	r3, r3, #0
	movne	r3, #1
.L1879:
	ldr	r8, .L1888+4
	mov	r1, r6
	str	r3, [sp, #28]
	mov	sl, #0
	ldr	r2, [sp, #28]
	ldr	r0, [r8, #-984]
	bl	FlashReadPages
	ldr	fp, [r8, #-1844]
	uxth	r2, r5
	ldr	r9, [r8, #-984]
	movw	ip, #65535
	mov	r3, r8
	str	fp, [sp, #16]
	mov	r8, r2
	str	ip, [sp, #12]
	mov	fp, r7
	b	.L1815
.L1820:
	ldr	r2, [r9, #0]
	cmp	r2, #0
	strne	r8, [sp, #12]
	bne	.L1818
	ldr	r2, [r9, #12]
	ldr	r7, [r2, #4]
	cmn	r7, #1
	beq	.L1817
	ldr	r1, [r3, #-1844]
	mov	r0, r7
	str	r2, [sp, #8]
	str	r3, [sp, #4]
	bl	ftl_cmp_data_ver
	ldr	r3, [sp, #4]
	ldr	r2, [sp, #8]
	cmp	r0, #0
	addne	r7, r7, #1
	strne	r7, [r3, #-1844]
.L1817:
	ldr	r2, [r2, #0]
	cmn	r2, #1
	bne	.L1818
	ldr	r3, .L1888+4
	mov	r2, #36
	uxth	r7, fp
	ldr	r3, [r3, #-984]
	mla	sl, r2, sl, r3
	ldr	r0, [sl, #4]
	b	.L1880
.L1818:
	add	sl, sl, #1
	add	r9, r9, #36
	uxth	sl, sl
.L1815:
	cmp	sl, r6
	bne	.L1820
	ldr	r3, .L1888+4
	add	r7, fp, #1
	uxth	r7, r7
	ldr	r3, [r3, #-984]
	ldr	r0, [r3, #4]
.L1880:
	ubfx	r0, r0, #10, #16
	bl	P2V_plane
	ldrb	r2, [r4, #8]	@ zero_extendqisi2
	ldr	r3, .L1888
	cmp	r2, #1
	str	r0, [sp, #24]
	bne	.L1821
	ldrb	r1, [r3, #80]	@ zero_extendqisi2
	cmp	r1, #0
	addeq	r7, r3, r7, asl #1
	movweq	r1, #2228
	ldreqh	r7, [r7, r1]
.L1821:
	movw	r1, #3912
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
	beq	.L1886
.L1823:
	ldr	r3, [sp, #16]
	ldr	fp, [sp, #12]
	sub	r6, r3, #1
	movw	r3, #65535
	cmp	fp, r3
	bne	.L1824
	cmp	r2, #0
	bne	.L1825
.L1824:
	ldr	r2, .L1888+4
	uxth	r3, r5
	ldr	ip, [sp, #20]
	mvn	fp, #0
	mov	sl, r3
	mov	r9, fp
	ldr	r1, [r2, #-1012]
	ldr	r8, .L1888+4
	cmn	r1, #1
	streq	r6, [r2, #-1012]
	ldr	r2, [r2, #-1012]
	str	r2, [sp, #12]
	add	r2, ip, #7
	cmp	r3, r2
	subgt	r5, r3, #7
	mov	r3, r7
	ldrle	r5, [sp, #20]
	mov	r7, r6
	uxthgt	r5, r5
	b	.L1828
.L1830:
	ldrh	r0, [r1, #16]
	movw	lr, #65535
	cmp	r0, lr
	beq	.L1829
	ldr	ip, [sp, #16]
	mov	r3, #36
	orr	r0, r5, r0, asl #10
	mla	lr, r3, r6, ip
	add	r6, r6, #1
	uxth	r6, r6
	str	r0, [lr, #4]
.L1829:
	add	r2, r2, #1
	add	r1, r1, #2
	uxth	r2, r2
.L1841:
	ldr	lr, [sp, #44]
	cmp	r2, lr
	bne	.L1830
	ldr	r3, [sp, #36]
	mov	r1, r6
	ldr	r0, [r8, #-984]
	ldr	r2, [sp, #28]
	str	r3, [sp, #4]
	bl	FlashReadPages
	ldr	r2, .L1888
	add	r0, r8, r5, asl #1
	mov	r1, #0
	str	r0, [sp, #16]
	ldr	r3, [sp, #4]
	ldrb	ip, [r2, #80]	@ zero_extendqisi2
	ldr	r2, [r8, #-984]
	str	ip, [sp, #36]
	b	.L1831
.L1839:
	ldr	r0, [r2, #0]
	cmp	r0, #0
	bne	.L1832
	ldr	r0, [r2, #12]
	movw	ip, #65535
	ldrh	lr, [r0, #0]
	cmp	lr, ip
	beq	.L1833
	ldr	r0, [r0, #4]
	cmn	r0, #1
	beq	.L1833
	cmn	fp, #1
	ldr	r9, [r8, #-1012]
	str	r0, [r8, #-1012]
	bne	.L1833
	ldr	lr, [sp, #16]
	sub	r0, lr, #912
	ldrh	r0, [r0, #0]
	cmp	r0, ip
	bne	.L1834
	ldr	r0, [sp, #36]
	cmp	r0, #0
	beq	.L1833
.L1834:
	cmp	r9, r7
	mvneq	fp, #0
	movne	fp, r9
	b	.L1833
.L1832:
	mov	r6, r7
	mov	r7, r3
	ldrb	r3, [r4, #8]	@ zero_extendqisi2
	cmp	r3, #0
	bne	.L1825
	ldr	r3, .L1888+4
	movw	r2, #65535
	add	r5, r3, r5, asl #1
	sub	r5, r5, #912
	ldrh	r1, [r5, #0]
	cmp	r1, r2
	bne	.L1835
	cmn	fp, #1
	bne	.L1881
.L1836:
	ldr	fp, [sp, #12]
	cmp	fp, r6
	beq	.L1837
.L1881:
	str	fp, [r3, #-1012]
	b	.L1825
.L1837:
	ldr	r2, [r3, #-1012]
	b	.L1887
.L1835:
	cmp	r9, r6
	beq	.L1838
	cmn	r9, #1
	strne	r9, [r3, #-1012]
	b	.L1825
.L1838:
	ldr	r2, [r3, #-1012]
	cmp	r2, r6
	beq	.L1825
.L1887:
	sub	r2, r2, #1
	b	.L1882
.L1833:
	add	r1, r1, #1
	add	r2, r2, #36
	uxth	r1, r1
.L1831:
	cmp	r1, r6
	bne	.L1839
	add	r5, r5, #1
	uxth	r5, r5
.L1828:
	cmp	r5, sl
	bhi	.L1840
	ldr	r1, .L1888
	movw	r2, #3844
	ldr	lr, [r8, #-984]
	mov	r6, #0
	str	r3, [sp, #36]
	ldrh	ip, [r1, r2]
	mov	r1, r4
	str	lr, [sp, #16]
	mov	r2, r6
	str	ip, [sp, #44]
	b	.L1841
.L1840:
	mov	r6, r7
	mov	r7, r3
	ldr	r3, .L1888+4
	mvn	r2, #0
.L1882:
	str	r2, [r3, #-1012]
.L1825:
	ldr	r5, .L1888+4
	movw	r3, #266
	mov	r2, #1
	sub	r0, r5, #1088
	strh	r2, [r5, r3]	@ movhi
	bl	FtlMapBlkWriteDumpData
	ldr	r9, [sp, #20]
	str	r7, [sp, #36]
.L1842:
	ldr	r3, .L1888
	movw	r2, #3844
	mov	r7, #0
	ldr	lr, [r5, #-984]
	mov	fp, #36
	ldrh	r8, [r3, r2]
	mov	r2, r4
	ldrb	ip, [r3, #80]	@ zero_extendqisi2
	mov	r3, r7
	b	.L1843
.L1846:
	ldrh	r0, [r2, #16]
	movw	r1, #65535
	cmp	r0, r1
	beq	.L1844
	mla	r1, fp, r7, lr
	orr	r0, r9, r0, asl #10
	str	r0, [r1, #4]
	ldrb	sl, [r4, #8]	@ zero_extendqisi2
	cmp	sl, #1
	bne	.L1845
	cmp	ip, #0
	orrne	r0, r0, #-2147483648
	strne	r0, [r1, #4]
.L1845:
	add	r7, r7, #1
	uxth	r7, r7
.L1844:
	add	r3, r3, #1
	add	r2, r2, #2
	uxth	r3, r3
.L1843:
	cmp	r3, r8
	bne	.L1846
	mov	r1, r7
	ldr	r0, [r5, #-984]
	ldr	r2, [sp, #28]
	bl	FlashReadPages
	mov	r3, #36
	mul	r3, r3, r7
	mov	r7, #0
	str	r3, [sp, #44]
	b	.L1847
.L1868:
	ldr	sl, [r5, #-984]
	add	sl, sl, r7
	ldr	r8, [sl, #4]
	ubfx	r0, r8, #10, #16
	str	r8, [sp, #60]
	bl	P2V_plane
	ldr	r3, [sp, #20]
	cmp	r9, r3
	bcc	.L1848
	ldr	fp, [sp, #32]
	mov	ip, r3
	cmp	r0, fp
	movcs	r3, #0
	movcc	r3, #1
	cmp	r9, ip
	movne	r3, #0
	cmp	r3, #0
	bne	.L1848
	ldr	r3, [sp, #24]
	ldr	ip, [sp, #36]
	cmp	r0, r3
	cmpeq	r9, ip
	beq	.L1849
	ldr	r3, [sl, #0]
	cmn	r3, #1
	beq	.L1850
	ldr	r8, [sl, #12]
	movw	lr, #61589
	ldrh	r3, [r8, #0]
	cmp	r3, lr
	bne	.L1857
.L1851:
	ldr	r6, [r8, #4]
	cmn	r6, #1
	beq	.L1852
	ldr	r1, [r5, #-1844]
	mov	r0, r6
	bl	ftl_cmp_data_ver
	cmp	r0, #0
	addne	r3, r6, #1
	strne	r3, [r5, #-1844]
.L1852:
	ldr	sl, [r8, #8]
	add	r1, sp, #56
	ldr	r3, [r8, #12]
	mov	r2, #0
	mov	r0, sl
	str	r3, [sp, #52]
	bl	log2phys
	ldr	r1, [r5, #-1012]
	cmn	r1, #1
	beq	.L1853
	mov	r0, r6
	bl	ftl_cmp_data_ver
	cmp	r0, #0
	beq	.L1853
	ldr	r3, [sp, #52]
	cmn	r3, #1
	beq	.L1854
	ldr	r0, [r5, #-984]
	mov	r1, #1
	mov	r2, #0
	add	r0, r0, r7
	ldr	fp, [r0, #12]
	str	r3, [r0, #4]
	str	fp, [sp, #12]
	bl	FlashReadPages
	ldr	r3, [r5, #-984]
	ldr	ip, [fp, #4]
	add	fp, r3, r7
	ldr	r3, [r3, r7]
	str	ip, [sp, #16]
	cmn	r3, #1
	bne	.L1855
	b	.L1856
.L1854:
	ldr	r3, [sp, #60]
	ldr	r2, [sp, #56]
	cmp	r2, r3
	bne	.L1857
	mov	r0, sl
	add	r1, sp, #52
	mov	r2, #1
	bl	log2phys
.L1857:
	ldrh	r0, [r4, #0]
	b	.L1884
.L1855:
	ldr	r3, [sp, #12]
	ldr	r8, [r3, #8]
	cmp	r8, sl
	bne	.L1856
	ldr	r0, [r5, #-1012]
	ldr	r1, [sp, #16]
	bl	ftl_cmp_data_ver
	cmp	r0, #0
	beq	.L1856
	ldr	r3, [sp, #56]
	ldr	r2, [sp, #60]
	cmp	r3, r2
	beq	.L1861
.L1858:
	ldr	r2, [sp, #52]
	cmp	r3, r2
	beq	.L1856
	cmn	r3, #1
	streq	r3, [fp, #0]
	beq	.L1860
	ldr	ip, [fp, #12]
	mov	r0, fp
	str	r3, [fp, #4]
	mov	r1, #1
	mov	r2, #0
	str	ip, [sp, #12]
	bl	FlashReadPages
.L1860:
	ldr	r3, [r5, #-984]
	ldr	r3, [r3, r7]
	cmn	r3, #1
	beq	.L1861
	ldr	r3, [sp, #12]
	ldr	r0, [r5, #-1012]
	ldr	sl, [r3, #4]
	mov	r1, sl
	bl	ftl_cmp_data_ver
	cmp	r0, #0
	beq	.L1861
	ldr	r0, [sp, #16]
	mov	r1, sl
	bl	ftl_cmp_data_ver
	cmp	r0, #0
	beq	.L1856
.L1861:
	mov	r0, r8
	ldr	r1, [sp, #52]
	bl	FtlReUsePrevPpa
.L1856:
	ldrh	r0, [r4, #0]
	mvn	r3, #0
	str	r3, [sp, #52]
	bl	decrement_vpc_count
	b	.L1863
.L1853:
	ldr	r3, [sp, #60]
	ldr	r2, [sp, #56]
	cmp	r2, r3
	beq	.L1863
	mov	r0, sl
	add	r1, sp, #60
	mov	r2, #1
	bl	log2phys
	ldr	fp, [sp, #56]
	cmn	fp, #1
	beq	.L1863
	ldr	r3, [sp, #52]
	cmp	fp, r3
	beq	.L1863
	ubfx	r0, fp, #10, #16
	bl	P2V_block_in_plane
	ldr	r3, .L1888+8
	ldrh	r3, [r5, r3]
	cmp	r3, r0
	beq	.L1864
	ldr	r3, .L1888+12
	ldrh	r3, [r5, r3]
	cmp	r3, r0
	beq	.L1864
	ldr	r3, .L1888+16
	ldrh	r3, [r5, r3]
	cmp	r3, r0
	bne	.L1863
.L1864:
	ldr	ip, .L1888+4
	mov	r1, #1
	mov	r2, #0
	ldr	r0, [ip, #-984]
	str	fp, [r0, #4]
	ldr	r8, [r0, #12]
	bl	FlashReadPages
	ldr	r0, .L1888+4
	ldr	r1, [r8, #4]
	ldr	r3, [r0, #-984]
	ldr	r3, [r3, #0]
	cmn	r3, #1
	beq	.L1863
	mov	r0, r6
	bl	ftl_cmp_data_ver
	cmp	r0, #0
	bne	.L1863
	mov	r0, sl
	add	r1, sp, #56
	mov	r2, #1
	bl	log2phys
.L1863:
	ldr	r0, [sp, #52]
	cmn	r0, #1
	beq	.L1848
	ubfx	r0, r0, #10, #16
	bl	P2V_block_in_plane
	ldr	r2, [r5, #-2064]
	mov	r3, r0, asl #1
	mov	r1, r0
	ldrh	r3, [r2, r3]
	cmp	r3, #0
	beq	.L1865
.L1884:
	bl	decrement_vpc_count
	b	.L1848
.L1865:
	ldr	r0, .L1888+20
	bl	printk
	b	.L1848
.L1850:
	ldr	r3, [r5, #268]
	cmp	r3, #31
	addls	r2, r5, r3, asl #2
	addls	r3, r3, #1
	strls	r3, [r5, #268]
	strls	r8, [r2, #272]
	ldrh	r0, [r4, #0]
	bl	decrement_vpc_count
	ldr	r3, [r5, #-1012]
	cmn	r3, #1
	beq	.L1883
.L1867:
	cmp	r3, r6
	bls	.L1848
.L1883:
	str	r6, [r5, #-1012]
.L1848:
	add	r7, r7, #36
.L1847:
	ldr	r3, [sp, #44]
	cmp	r7, r3
	bne	.L1868
	ldr	r3, .L1888
	add	r9, r9, #1
	movw	r2, #3912
	uxth	r9, r9
	ldrh	r2, [r3, r2]
	cmp	r9, r2
	bne	.L1842
	mov	r2, #0
	movw	r1, #3844
	strh	r2, [r4, #4]	@ movhi
	ldrh	r1, [r3, r1]
	mov	r3, r2
	strh	r9, [r4, #2]	@ movhi
	movw	r2, #65535
	ldr	r0, [sp, #40]
	b	.L1869
.L1871:
	add	r0, r0, #2
	ldrh	ip, [r0, #14]
	cmp	ip, r2
	strneb	r3, [r4, #6]
	bne	.L1805
.L1870:
	add	r3, r3, #1
	uxth	r3, r3
.L1869:
	cmp	r3, r1
	bne	.L1871
	b	.L1805
.L1849:
	ldr	r7, [sp, #36]
	ldr	fp, [sp, #24]
	strh	r7, [r4, #2]	@ movhi
	strb	fp, [r4, #6]
.L1886:
	mov	r0, r4
	mov	r1, r7
	mov	r2, fp
	bl	ftl_sb_update_avl_pages
.L1805:
	mov	r0, #0
	add	sp, sp, #68
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, sl, fp, pc}
.L1889:
	.align	2
.L1888:
	.word	.LANCHOR0
	.word	.LANCHOR2
	.word	-2044
	.word	-1996
	.word	-1948
	.word	.LC112
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
	ldr	r4, .L1899
	ldr	r3, .L1899+4
	mov	r2, r4
	ldrh	r3, [r4, r3]
	cmp	r3, #0
	beq	.L1891
	ldrb	r1, [r4, #-2036]	@ zero_extendqisi2
	cmp	r1, #0
	bne	.L1891
	ldr	r5, .L1899+8
	movw	r0, #3912
	ldrb	r1, [r4, #-2037]	@ zero_extendqisi2
	ldrh	r0, [r5, r0]
	mul	r1, r0, r1
	cmp	r3, r1
	beq	.L1891
	ldrb	r8, [r4, #-2034]	@ zero_extendqisi2
	movw	r3, #3844
	ldr	r7, [r5, #3972]
	cmp	r8, #0
	ldrh	r6, [r5, r3]
	bne	.L1890
	sub	r7, r7, #1
	mov	r1, sp
	mov	r2, r8
	mov	r0, r7
	bl	log2phys
	ldr	r3, [sp, #0]
	ldr	r0, [r4, #-2076]
	ldr	r4, [r4, #-952]
	cmn	r3, #1
	str	r3, [sp, #8]
	str	r7, [sp, #20]
	str	r0, [sp, #12]
	str	r4, [sp, #16]
	str	r8, [r4, #4]
	beq	.L1893
	add	r0, sp, #4
	mov	r1, #1
	mov	r2, r8
	bl	FlashReadPages
	b	.L1894
.L1893:
	movw	r3, #3922
	mov	r1, #255
	ldrh	r2, [r5, r3]
	bl	ftl_memset
.L1894:
	ldr	r3, .L1899+12
	mov	r6, r6, asl #2
	ldr	r5, .L1899
	ldr	sl, .L1899+4
	strh	r3, [r4, #0]	@ movhi
	ldr	r8, .L1899+16
	b	.L1895
.L1898:
	ldrh	r3, [r5, sl]
	cmp	r3, #0
	beq	.L1896
	ldr	r3, [sp, #8]
	sub	r6, r6, #1
	str	r7, [r4, #8]
	ldr	r0, .L1899+20
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
.L1895:
	cmp	r6, #0
	bne	.L1898
.L1896:
	ldr	r3, .L1899
	mov	r2, #1
	strb	r2, [r3, #-2034]
	b	.L1890
.L1891:
	mov	r3, #0
	strb	r3, [r2, #-2034]
.L1890:
	add	sp, sp, #44
	ldmfd	sp!, {r4, r5, r6, r7, r8, sl, pc}
.L1900:
	.align	2
.L1899:
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
	ldr	r7, .L1905
	movw	r6, #3950
	ldr	r5, .L1905+4
	b	.L1902
.L1904:
	ldr	r3, [r5, #-1900]
	mov	r2, #12
	mla	r3, r2, r4, r3
	ldr	r3, [r3, #4]
	cmp	r3, #0
	bge	.L1903
	mov	r0, r4
	bl	flush_l2p_region
.L1903:
	add	r4, r4, #1
	uxth	r4, r4
.L1902:
	ldrh	r3, [r7, r6]
	cmp	r3, r4
	bhi	.L1904
	mov	r0, #0
	ldmfd	sp!, {r3, r4, r5, r6, r7, pc}
.L1906:
	.align	2
.L1905:
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
	ldr	r6, .L1914
	ldr	r1, .L1914+4
	ldr	r0, .L1914+8
	bl	printk
	movw	r3, #3854
	ldrh	r2, [r6, r3]
	mov	r1, #0
	ldr	r5, .L1914+12
	mov	r2, r2, asl #1
	ldr	r0, [r5, #-932]
	bl	ftl_memset
	b	.L1908
.L1910:
	mov	r0, r4
	add	r1, sp, #4
	mov	r2, #0
	bl	log2phys
	ldr	r0, [sp, #4]
	cmn	r0, #1
	beq	.L1909
	ubfx	r0, r0, #10, #16
	bl	P2V_block_in_plane
	ldr	r3, [r5, #-932]
	mov	r0, r0, asl #1
	ldrh	r2, [r3, r0]
	add	r2, r2, #1
	strh	r2, [r3, r0]	@ movhi
.L1909:
	add	r4, r4, #1
.L1908:
	ldr	r3, [r6, #3972]
	cmp	r4, r3
	bcc	.L1910
	mov	r4, #0
	ldr	r7, .L1914
	movw	r8, #3852
	ldr	r5, .L1914+12
	b	.L1911
.L1913:
	ldr	r3, [r5, #-2064]
	mov	r6, r4, asl #1
	ldrh	r2, [r3, r6]
	ldr	r3, [r5, #-932]
	ldrh	r3, [r3, r6]
	cmp	r2, r3
	beq	.L1912
	movw	r1, #65535
	cmp	r2, r1
	beq	.L1912
	ldr	r0, .L1914+16
	mov	r1, r4
	bl	printk
	ldr	r3, .L1914+20
	ldrh	r3, [r5, r3]
	cmp	r3, r4
	beq	.L1912
	ldr	r3, .L1914+24
	ldrh	r3, [r5, r3]
	cmp	r3, r4
	beq	.L1912
	ldr	r3, .L1914+28
	ldrh	r3, [r5, r3]
	cmp	r3, r4
	beq	.L1912
	ldr	r3, [r5, #-932]
	mov	r0, r4
	ldrh	r2, [r3, r6]
	ldr	r3, [r5, #-2064]
	strh	r2, [r3, r6]	@ movhi
	bl	update_vpc_list
	bl	l2p_flush
	bl	FtlVpcTblFlush
.L1912:
	add	r4, r4, #1
	uxth	r4, r4
.L1911:
	ldrh	r3, [r7, r8]
	cmp	r3, r4
	bhi	.L1913
	ldmfd	sp!, {r2, r3, r4, r5, r6, r7, r8, pc}
.L1915:
	.align	2
.L1914:
	.word	.LANCHOR0
	.word	.LANCHOR3+15
	.word	.LC113
	.word	.LANCHOR2
	.word	.LC114
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
	stmfd	sp!, {r4, r5, r6, lr}
	.save {r4, r5, r6, lr}
	movw	r3, #65535
	ldrh	r5, [r0, #0]
	mov	r4, r0
	cmp	r5, r3
	beq	.L1917
	ldr	r3, .L1927
	mov	r0, r5
	ldr	r2, [r3, #-2064]
	mov	r3, r5, asl #1
	ldrh	r3, [r2, r3]
	cmp	r3, #0
	beq	.L1918
	bl	INSERT_DATA_LIST
	b	.L1917
.L1918:
	bl	INSERT_FREE_LIST
.L1917:
	mov	r3, #0
	strb	r3, [r4, #8]
	ldr	r3, .L1927+4
	cmp	r4, r3
	beq	.L1919
	ldr	r2, .L1927+8
	movw	r3, #3864
	ldrh	r3, [r2, r3]
	cmp	r3, #1
	beq	.L1919
	ldrb	r2, [r2, #80]	@ zero_extendqisi2
	cmp	r2, #0
	beq	.L1920
.L1919:
	mov	r3, #1
	strb	r3, [r4, #8]
	b	.L1921
.L1920:
	ldr	r2, .L1927+12
	cmp	r4, r2
	bne	.L1921
	cmp	r3, #3
	ldr	r3, .L1927
	beq	.L1922
	ldr	r2, [r3, #-1708]
	cmp	r2, #1
	bne	.L1923
.L1922:
	mov	r2, #1
	strb	r2, [r3, #-2036]
.L1923:
	ldr	r3, [r3, #-1884]
	ldr	r2, .L1927
	cmp	r3, #0
	beq	.L1921
	ldr	r3, [r2, #-1824]
	cmp	r3, #29
	movls	r3, #1
	strlsb	r3, [r2, #-2036]
.L1921:
	ldr	r3, .L1927+16
	ldr	r6, .L1927
	ldrh	r0, [r6, r3]
	movw	r3, #65535
	cmp	r0, r3
	beq	.L1924
	cmp	r5, r0
	bne	.L1925
	ldr	r2, [r6, #-2064]
	mov	r3, r0, asl #1
	ldrh	r3, [r2, r3]
	cmp	r3, #0
	beq	.L1926
.L1925:
	bl	update_vpc_list
.L1926:
	ldr	r3, .L1927+16
	mvn	r2, #0
	strh	r2, [r6, r3]	@ movhi
.L1924:
	mov	r0, r4
	bl	allocate_data_superblock
	bl	l2p_flush
	bl	FtlVpcTblFlush
	mov	r0, #0
	bl	FtlEctTblFlush
	mov	r0, #0
	ldmfd	sp!, {r4, r5, r6, pc}
.L1928:
	.align	2
.L1927:
	.word	.LANCHOR2
	.word	.LANCHOR2-1996
	.word	.LANCHOR0
	.word	.LANCHOR2-2044
	.word	-1024
	.fnend
	.size	allocate_new_data_superblock, .-allocate_new_data_superblock
	.align	2
	.type	rk_ftl_garbage_collect.part.18, %function
rk_ftl_garbage_collect.part.18:
	.fnstart
	@ args = 0, pretend = 0, frame = 24
	@ frame_needed = 0, uses_anonymous_args = 0
	ldr	r3, .L2025
	movw	ip, #65535
	ldr	r2, .L2025+4
	stmfd	sp!, {r4, r5, r6, r7, r8, r9, sl, fp, lr}
	.save {r4, r5, r6, r7, r8, r9, sl, fp, lr}
	mov	r7, r0
	ldrh	r0, [r3, r2]
	.pad #36
	sub	sp, sp, #36
	cmp	r0, ip
	str	r1, [sp, #20]
	beq	.L1930
	ldr	r1, .L2025+8
	ldrh	r4, [r3, r1]
	cmp	r4, ip
	streqh	r0, [r3, r1]	@ movhi
	mvneq	r1, #0
	streqh	r1, [r3, r2]	@ movhi
.L1930:
	ldr	r2, [r3, #-1188]
	cmp	r7, #1
	ldr	r5, .L2025
	add	r2, r2, #1
	add	r2, r2, r7, asl #7
	str	r2, [r3, #-1188]
	bne	.L2005
	ldr	r3, [r5, #-1884]
	cmp	r3, #0
	beq	.L2005
	ldr	r3, [r5, #-1824]
	cmp	r3, #29
	bhi	.L2005
	mov	r6, #400
	movw	r4, #65535
	ldrh	r3, [r5, r6]
	add	r2, r2, r3
	str	r2, [r5, #-1188]
	bl	FtlGcReFreshBadBlk
	ldr	r3, .L2025+12
	ldrh	r3, [r5, r3]
	cmp	r3, r4
	bne	.L1931
	ldr	r2, .L2025+8
	ldrh	r4, [r5, r2]
	cmp	r4, r3
	movne	r4, r3
	bne	.L1931
	ldr	r3, [r5, #-1188]
	cmp	r3, #1024
	bls	.L1931
	mov	r3, #0
	str	r3, [r5, #-1188]
	strh	r3, [r5, r6]	@ movhi
	ldr	r3, [r5, #-1824]
	cmp	r3, #0
	moveq	r3, #6
	beq	.L2020
	cmp	r3, #5
	bhi	.L1933
	mov	r3, #18
.L2020:
	strh	r3, [r5, r6]	@ movhi
.L1933:
	mov	r0, #32
	movw	r8, #65535
	bl	List_get_gc_head_node
	uxth	r2, r0
	cmp	r2, r8
	beq	.L1934
	ldr	r5, .L2025
	ldr	r6, .L2025+16
	ldrh	r0, [r5, r6]
	cmp	r0, #0
	moveq	r3, #1
	streqh	r3, [r5, r6]	@ movhi
	beq	.L1934
	ldr	r3, .L2025+20
	movw	ip, #3914
	movw	lr, #3844
	ldr	r9, [r5, #-2064]
	mov	r2, r2, asl #1
	ldrh	ip, [r3, ip]
	ldrh	r3, [r3, lr]
	ldrh	r1, [r9, r2]
	mul	r3, r3, ip
	add	r3, r3, #1
	cmp	r1, r3
	bgt	.L1934
	add	fp, r0, #1
	mov	sl, #0
	str	sl, [r5, #-1180]
	uxth	fp, fp
	strh	fp, [r5, r6]	@ movhi
	str	r2, [sp, #12]
	bl	List_get_gc_head_node
	ldr	r2, [sp, #12]
	uxth	r4, r0
	cmp	r4, r8
	beq	.L1934
	ldrh	r2, [r9, r2]
	mov	r8, r4, asl #1
	ldr	r0, .L2025+24
	mov	r1, fp
	ldrh	r3, [r9, r8]
	str	r2, [sp, #0]
	mov	r2, r4
	bl	printk
	ldrh	r3, [r5, r6]
	cmp	r3, #40
	bls	.L1936
	ldr	r3, [r5, #-2064]
	ldrh	r3, [r3, r8]
	cmp	r3, #32
	strhih	sl, [r5, r6]	@ movhi
.L1936:
	mov	r1, #6
	b	.L2021
.L1934:
	bl	GetSwlReplaceBlock
	movw	r3, #65535
	cmp	r0, r3
	mov	r4, r0
	bne	.L1931
	mov	r1, #0
.L2021:
	ldr	r2, .L2025
	mov	r3, #400
	strh	r1, [r2, r3]	@ movhi
	b	.L1931
.L2005:
	movw	r4, #65535
.L1931:
	ldr	r3, .L2025+12
	ldr	r6, .L2025
	ldrh	r2, [r6, r3]
	movw	r3, #65535
	cmp	r2, r3
	bne	.L1937
	ldr	r3, .L2025+28
	ldrh	r3, [r6, r3]
	cmp	r3, r2
	bne	.L1937
	ldr	r2, .L2025+8
	ldrh	sl, [r6, r2]
	cmp	sl, r3
	bne	.L1937
	ldr	r8, .L2025+32
	ldr	r2, [r6, #-1188]
	ldrh	r3, [r6, r8]
	cmp	r3, #23
	movhi	r3, #1024
	movls	r3, #5120
	cmp	r2, r3
	bls	.L1937
	mov	r2, #400
	mov	r3, #0
	str	r3, [r6, #-1188]
	strh	r3, [r6, r2]	@ movhi
	bl	GetSwlReplaceBlock
	cmp	r0, sl
	mov	r4, r0
	bne	.L1939
	ldr	r3, .L2025+36
	ldrh	r1, [r6, r8]
	ldrh	r2, [r6, r3]
	cmp	r1, r2
	movcs	r2, #80
	strcsh	r2, [r6, r3]	@ movhi
	bcs	.L1949
	mov	r0, #64
	bl	List_get_gc_head_node
	uxth	r3, r0
	cmp	r3, r4
	beq	.L1949
	ldr	r2, [r6, #-1004]
	cmp	r2, #0
	bne	.L1942
	ldr	r2, .L2025+20
	movw	r1, #3864
	ldrh	r1, [r2, r1]
	cmp	r1, #3
	beq	.L1942
	ldr	r1, [r6, #-1708]
	cmp	r1, #0
	bne	.L1942
	ldr	r1, [r6, #-1884]
	cmp	r1, #0
	bne	.L1942
	ldrb	r0, [r2, #80]	@ zero_extendqisi2
	cmp	r0, #0
	beq	.L1943
.L1942:
	ldr	r1, .L2025
	mov	r3, r3, asl #1
	movw	ip, #3844
	movw	lr, #3864
	ldr	r2, [r1, #-2064]
	ldrh	r0, [r2, r3]
	movw	r2, #3914
	ldr	r3, .L2025+20
	ldrh	r2, [r3, r2]
	ldrh	ip, [r3, ip]
	ldrh	r3, [r3, lr]
	mul	ip, ip, r2
	cmp	r3, #3
	moveq	r3, r2, lsr #1
	movne	r3, #0
	add	r3, ip, r3
	cmp	r0, r3
	bgt	.L1945
	mov	r0, #0
	bl	List_get_gc_head_node
	ldr	r2, .L2025+20
	ldr	r3, .L2025
	ldr	r2, [r2, #3972]
	ldr	r1, [r3, #-1892]
	add	r2, r2, r2, asl #1
	cmp	r1, r2, lsr #2
	ldr	r2, .L2025+36
	movhi	r1, #128
	movls	r1, #160
	strh	r1, [r3, r2]	@ movhi
	uxth	r4, r0
	b	.L1947
.L1945:
	ldr	r3, .L2025+36
	mov	r2, #128
	strh	r2, [r1, r3]	@ movhi
	b	.L1949
.L1943:
	ldr	r2, [r6, #-2064]
	mov	r3, r3, asl #1
	ldr	r6, .L2025
	ldr	r5, .L2025+36
	ldrh	r3, [r2, r3]
	cmp	r3, #7
	movhi	r3, #64
	strhih	r3, [r6, r5]	@ movhi
	bhi	.L1949
	bl	List_get_gc_head_node
	mov	r3, #128
	strh	r3, [r6, r5]	@ movhi
	uxth	r4, r0
.L1947:
	movw	r3, #65535
	cmp	r4, r3
	beq	.L1949
.L1939:
	ldr	r1, .L2025
	mov	r0, r4, asl #1
	ldr	r3, .L2025+32
	ldr	ip, [r1, #-2084]
	ldrh	r2, [r1, r3]
	ldr	r3, [r1, #-2064]
	ldrh	r3, [r3, r0]
	ldrh	r0, [ip, r0]
	str	r0, [sp, #0]
	ldr	r0, .L2025+40
	ldrh	r1, [r1, r0]
	ldr	r0, .L2025+44
	str	r1, [sp, #4]
	mov	r1, r4
	bl	printk
.L1949:
	bl	FtlGcReFreshBadBlk
.L1937:
	movw	r1, #65535
	rsb	ip, r1, r4
	rsbs	r2, ip, #0
	ldr	r5, .L2025
	adc	r2, r2, ip
	cmp	r7, #0
	movne	r7, #0
	andeq	r7, r2, #1
	cmp	r7, #0
	beq	.L1950
	ldr	r3, .L2025+32
	ldrh	r2, [r5, r3]
	cmp	r2, #24
	movhi	r6, #1
	bhi	.L1951
	ldr	r1, .L2025+20
	movw	r3, #3912
	cmp	r2, #16
	ldrh	r6, [r1, r3]
	movhi	r6, r6, lsr #5
	bhi	.L1951
	cmp	r2, #12
	movhi	r6, r6, lsr #4
	bhi	.L1951
	cmp	r2, #8
	movhi	r6, r6, lsr #2
.L1951:
	ldr	r1, .L2025+40
	ldr	r3, .L2025
	ldrh	r0, [r5, r1]
	cmp	r0, r2
	mov	r0, r1
	bcs	.L1955
	ldr	r2, .L2025+28
	movw	ip, #65535
	ldrh	r2, [r3, r2]
	cmp	r2, ip
	bne	.L1956
	ldr	ip, .L2025+8
	ldrh	ip, [r3, ip]
	cmp	ip, r2
	bne	.L1956
	mov	r2, #400
	ldrh	r0, [r3, r2]
	cmp	r0, #0
	bne	.L1957
	ldr	r2, .L2025+20
	ldr	ip, [r3, #-1892]
	ldr	r2, [r2, #3972]
	add	r2, r2, r2, asl #1
	cmp	ip, r2, lsr #2
	movcs	r2, #18
	bcs	.L2023
.L1957:
	ldr	r3, .L2025
	ldr	r2, .L2025+48
	ldr	r1, .L2025+40
	ldrh	r2, [r3, r2]
	add	r2, r2, r2, asl #1
	mov	r2, r2, asr #2
.L2023:
	strh	r2, [r3, r1]	@ movhi
	mov	r2, #0
	ldr	r3, .L2025
	str	r2, [r3, #-1180]
	b	.L1960
.L1956:
	ldr	r3, .L2025
	ldr	r2, .L2025+48
	ldrh	r2, [r3, r2]
	add	r2, r2, r2, asl #1
	mov	r2, r2, asr #2
	strh	r2, [r3, r0]	@ movhi
.L1955:
	ldr	r3, .L2025
	movw	r4, #65535
	ldr	r8, [sp, #20]
	ldr	r2, [r3, #-1884]
	cmp	r8, #2
	movhi	r3, #0
	movls	r3, #1
	cmp	r2, #0
	moveq	r3, #0
	cmp	r3, #0
	addne	r6, r6, #1
	uxthne	r6, r6
	b	.L1961
.L1950:
	ldr	r3, .L2025+28
	ldrh	r3, [r5, r3]
	cmp	r3, r1
	bne	.L1962
	ldr	r1, .L2025+8
	ldrh	r1, [r5, r1]
	cmp	r1, r3
	movne	r2, #0
	andeq	r2, r2, #1
	cmp	r2, #0
	beq	.L1962
	ldr	r2, .L2025+12
	ldrh	r4, [r5, r2]
	cmp	r4, r3
	movne	r4, r3
	bne	.L1962
	ldr	r3, .L2025+32
	mov	r6, #400
	str	r7, [r5, #-1180]
	ldrh	r7, [r5, r3]
	add	r3, r3, #856
	ldrh	r2, [r5, r3]
	cmp	r2, r7
	bcs	.L1963
	ldrh	r2, [r5, r6]
	cmp	r2, #0
	bne	.L1964
	ldr	r2, .L2025+20
	ldr	r1, [r5, #-1892]
	ldr	r2, [r2, #3972]
	add	r2, r2, r2, asl #1
	cmp	r1, r2, lsr #2
	movcs	r2, #18
	strcsh	r2, [r5, r3]	@ movhi
	bcs	.L1966
.L1964:
	ldr	r3, .L2025
	ldr	r2, .L2025+48
	ldr	r1, .L2025+40
	ldrh	r2, [r3, r2]
	add	r2, r2, r2, asl #1
	mov	r2, r2, asr #2
	strh	r2, [r3, r1]	@ movhi
.L1966:
	bl	FtlReadRefresh
	mov	r3, #400
	ldr	r2, .L2025
	b	.L2024
.L1963:
	ldrh	r0, [r5, r6]
	cmp	r0, #0
	bne	.L1962
	ldr	r2, .L2025+48
	ldrh	r4, [r5, r2]
	add	r2, r4, r4, asl #1
	mov	r2, r2, asr #2
	strh	r2, [r5, r3]	@ movhi
	bl	List_get_gc_head_node
	ldr	r3, [r5, #-2064]
	ldr	r1, .L2025+20
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
	ble	.L1967
	sub	r4, r4, #1
	cmp	r7, r4
	blt	.L1967
	bl	FtlReadRefresh
	ldrh	r0, [r5, r6]
	b	.L1960
.L1967:
	cmp	r3, #0
	movwne	r4, #65535
	bne	.L1962
	movw	r0, #65535
	bl	decrement_vpc_count
	ldr	r3, .L2025+32
	ldr	r2, .L2025
	ldrh	r0, [r2, r3]
	add	r0, r0, #1
	b	.L1960
.L1962:
	ldr	r3, .L2025
	ldr	r6, [r3, #-1884]
	cmp	r6, #0
	movne	r6, #2
	moveq	r6, #1
.L1961:
	ldr	r3, .L2025
	movw	r0, #65535
	ldr	r2, .L2025+12
	ldrh	r1, [r3, r2]
	cmp	r1, r0
	bne	.L1968
	cmp	r4, r1
	strneh	r4, [r3, r2]	@ movhi
	bne	.L1970
	ldr	r1, .L2025+8
	ldrh	r0, [r3, r1]
	cmp	r0, r4
	strneh	r0, [r3, r2]	@ movhi
	mvnne	r2, #0
	strneh	r2, [r3, r1]	@ movhi
.L1970:
	mov	r2, #0
	strb	r2, [r3, #-1748]
	ldr	r3, .L2025+20
	ldr	r5, .L2025
	ldr	r7, .L2025+12
	ldrb	r3, [r3, #80]	@ zero_extendqisi2
	cmp	r3, r2
	beq	.L1971
	ldrh	r0, [r5, r7]
	bl	ftl_get_blk_mode
	strb	r0, [r5, #-1748]
.L1971:
	ldr	r5, .L2025
	ldrh	r0, [r5, r7]
	bl	IsBlkInGcList
	ldr	r7, .L2025+12
	mov	r3, r5
	cmp	r0, #0
	ldrne	r2, .L2025+12
	mvnne	r1, #0
	strneh	r1, [r5, r2]	@ movhi
	ldrh	r2, [r3, r7]
	movw	r3, #65535
	ldr	r5, .L2025
	cmp	r2, r3
	beq	.L1968
	ldr	r0, .L2025+52
	bl	make_superblock
	ldr	r1, .L2025+56
	movw	r2, #402
	mov	r3, #0
	strh	r3, [r5, r2]	@ movhi
	strh	r3, [r5, r1]	@ movhi
	strb	r3, [r5, #-1750]
	ldrh	r3, [r5, r7]
	ldr	r2, [r5, #-2064]
	mov	r3, r3, asl #1
	ldrh	r2, [r2, r3]
	mov	r3, #404
	strh	r2, [r5, r3]	@ movhi
.L1968:
	ldr	r3, .L2025
	ldr	r0, .L2025+12
	ldr	r2, .L2025+60
	ldrh	r1, [r3, r0]
	ldrh	r2, [r3, r2]
	cmp	r2, r1
	mov	r2, r3
	beq	.L1973
	ldr	ip, .L2025+64
	ldrh	r3, [r3, ip]
	cmp	r3, r1
	strne	r4, [sp, #20]
	bne	.L2019
.L1974:
.L1973:
	mvn	r3, #0
	strh	r3, [r2, r0]	@ movhi
	mov	r3, #400
.L2024:
	ldrh	r0, [r2, r3]
	b	.L1960
.L2019:
	ldr	r4, .L2025
	movw	r0, #65535
	ldr	r9, .L2025+12
	ldrh	r3, [r4, r9]
	cmp	r3, r0
	bne	.L1976
	mov	r3, #0
	str	r3, [r4, #-1180]
.L2018:
	ldr	r8, .L2025+16
	ldrh	r7, [r4, r8]
	mov	r0, r7
	bl	List_get_gc_head_node
	movw	r1, #65535
	uxth	r5, r0
	strh	r5, [r4, r9]	@ movhi
	cmp	r5, r1
	ldreq	r3, .L2025
	moveq	r2, #0
	moveq	r0, #8
	streqh	r2, [r3, r8]	@ movhi
	beq	.L1960
.L1978:
	mov	r0, r5
	add	r7, r7, #1
	bl	IsBlkInGcList
	cmp	r0, #0
	strneh	r7, [r4, r8]	@ movhi
	bne	.L2018
	ldr	ip, .L2025+20
	uxth	r7, r7
	movw	r3, #3912
	strh	r7, [r4, r8]	@ movhi
	movw	r8, #3844
	ldr	r1, [r4, #-2064]
	ldrh	r3, [ip, r3]
	mov	r2, r5, asl #1
	ldrh	ip, [ip, r8]
	ldrh	r0, [r1, r2]
	mul	r3, ip, r3
	add	ip, r3, r3, lsr #31
	cmp	r0, ip, asr #1
	bgt	.L1981
	cmp	r7, #48
	bls	.L1982
	cmp	r0, #8
	bls	.L1982
	ldr	r0, .L2025+68
	ldrh	r0, [r4, r0]
	cmp	r0, #35
	bhi	.L1982
.L1981:
	ldr	r0, .L2025+16
	mov	ip, #0
	strh	ip, [r4, r0]	@ movhi
.L1982:
	ldrh	r2, [r1, r2]
	movw	r0, #65535
	ldr	r8, [sp, #20]
	ldr	sl, .L2025
	cmp	r2, r3
	cmpge	r8, r0
	movne	r3, #0
	moveq	r3, #1
	bne	.L1983
	ldr	r3, .L2025+12
	mvn	r2, #0
	strh	r2, [sl, r3]	@ movhi
	add	r3, r3, #572
	mov	r2, #0
	strh	r2, [sl, r3]	@ movhi
	mov	r3, #400
	ldrh	r0, [sl, r3]
	b	.L1960
.L1983:
	cmp	r2, #0
	bne	.L1984
	movw	r0, #65535
	bl	decrement_vpc_count
	ldr	r3, .L2025+16
	ldrh	r2, [r4, r3]
	add	r2, r2, #1
	strh	r2, [r4, r3]	@ movhi
	b	.L2018
.L1984:
	strb	r3, [sl, #-1748]
	ldr	r3, .L2025+20
	ldrb	r3, [r3, #80]	@ zero_extendqisi2
	cmp	r3, #0
	beq	.L1985
	mov	r0, r5
	bl	ftl_get_blk_mode
	strb	r0, [sl, #-1748]
.L1985:
	ldr	r4, .L2025
	ldr	r0, .L2025+52
	bl	make_superblock
	movw	r1, #402
	mov	r3, #0
	strh	r3, [r4, r1]	@ movhi
	ldr	r1, .L2025+12
	ldr	r0, [r4, #-2064]
	ldrh	r1, [r4, r1]
	mov	r1, r1, asl #1
	ldrh	r0, [r0, r1]
	mov	r1, #404
	strb	r3, [r4, #-1750]
	strh	r0, [r4, r1]	@ movhi
	ldr	r1, .L2025+56
	strh	r3, [r4, r1]	@ movhi
.L1976:
	bl	FtlReadRefresh
	ldr	r3, .L2025
	mov	r2, #1
	movw	r1, #3912
	str	r2, [r3, #-1000]
	ldr	r2, .L2025+20
	ldrh	r9, [r2, r1]
	ldrb	r1, [r2, #80]	@ zero_extendqisi2
	cmp	r1, #0
	beq	.L1986
	ldrb	r1, [r3, #-1748]	@ zero_extendqisi2
	cmp	r1, #1
	movweq	r1, #3914
	ldreqh	r9, [r2, r1]
.L1986:
	ldr	r2, .L2025+56
	mov	sl, #0
	ldr	r4, .L2025
	mov	fp, r9
	ldrh	r3, [r3, r2]
	add	r2, r3, r6
	cmp	r2, r9
	rsbgt	r6, r3, r9
	uxthgt	r6, r6
	b	.L1988
.L1990:
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
.L1996:
	cmp	r3, r5
	bne	.L1990
	ldr	r0, [r4, #-1160]
	mov	r1, r7
	ldrb	r2, [r4, #-1748]	@ zero_extendqisi2
	mov	r5, #0
	bl	FlashReadPages
	mov	r9, r5
	mov	r8, r6
	b	.L1991
.L1994:
	ldr	r3, [r4, #-1160]
	add	r2, r3, r5
	ldr	r3, [r3, r5]
	ldr	r6, [r2, #12]
	cmn	r3, #1
	beq	.L1992
	ldrh	r3, [r6, #0]
	movw	r0, #61589
	cmp	r3, r0
	bne	.L1992
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
	bne	.L1992
	movw	r3, #402
	ldr	r0, [r4, #-1176]
	ldrh	r2, [r4, r3]
	ldr	r1, [r1, #16]
	add	r2, r2, #1
	strh	r2, [r4, r3]	@ movhi
	ldr	r2, [r4, #-980]
	mov	r3, #36
	mla	r2, r3, r0, r2
	str	r1, [r2, #16]
	str	r2, [sp, #12]
	str	r3, [sp, #16]
	bl	Ftl_get_new_temp_ppa
	ldr	r2, [sp, #12]
	ldr	r1, [r4, #-1176]
	str	r0, [r2, #4]
	ldr	r3, [sp, #16]
	ldr	r2, [r4, #-980]
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
	ldr	r3, .L2025+28
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
	ldr	r3, .L2025+20
	ldrb	r3, [r3, #80]	@ zero_extendqisi2
	cmp	r3, #0
	bne	.L1993
	ldrb	r3, [r4, #-1941]	@ zero_extendqisi2
	ldr	r2, [r4, #-1176]
	cmp	r2, r3
	beq	.L1993
	ldr	r3, .L2025+72
	ldrh	r3, [r4, r3]
	cmp	r3, #0
	bne	.L1992
.L1993:
	bl	Ftl_gc_temp_data_write_back
	cmp	r0, #0
	ldrne	r3, .L2025
	movne	r2, #0
	strne	r2, [r3, #-1000]
	movne	r2, #400
	bne	.L2024
.L1992:
	add	r9, r9, #1
	add	r5, r5, #36
	uxth	r9, r9
.L1991:
	cmp	r9, r7
	bne	.L1994
	add	sl, sl, #1
	mov	r6, r8
.L1988:
	uxth	r3, sl
	ldr	r2, .L2025+56
	cmp	r3, r6
	ldr	r3, .L2025
	bcs	.L1995
	ldr	r1, .L2025+20
	movw	r3, #3844
	ldrh	ip, [r4, r2]
	mov	r7, #0
	ldr	lr, [r4, #-1160]
	mov	r0, #36
	ldrh	r5, [r1, r3]
	add	ip, ip, sl
	ldr	r2, .L2025+76
	mov	r3, r7
	b	.L1996
.L1995:
	ldrh	r1, [r3, r2]
	mov	r9, fp
	add	r6, r6, r1
	uxth	r6, r6
	strh	r6, [r3, r2]	@ movhi
	cmp	r6, fp
	bcs	.L1997
	movw	r2, #402
	mov	r1, #404
	ldrh	r2, [r3, r2]
	ldrh	r3, [r3, r1]
	cmp	r2, r3
	bne	.L1998
.L1997:
	ldr	r5, .L2025
	ldr	r3, [r5, #-1176]
	cmp	r3, #0
	beq	.L1999
	bl	Ftl_gc_temp_data_write_back
	cmp	r0, #0
	movne	r3, #0
	strne	r3, [r5, #-1000]
	movne	r3, #400
	ldrneh	r0, [r5, r3]
	bne	.L1960
.L1999:
	movw	r2, #402
	ldr	r3, .L2025
	ldrh	ip, [r5, r2]
	cmp	ip, #0
	bne	.L2000
	ldr	r2, .L2025+12
	ldr	r0, [r3, #-2064]
	ldrh	r1, [r3, r2]
	mov	r1, r1, asl #1
	ldrh	r4, [r0, r1]
	cmp	r4, #0
	beq	.L2000
	strh	ip, [r0, r1]	@ movhi
	ldrh	r0, [r3, r2]
	bl	update_vpc_list
	bl	FtlCacheWriteBack
	bl	l2p_flush
	bl	FtlVpcTblFlush
.L2000:
	ldr	r3, .L2025+12
	mvn	r1, #0
	ldr	r2, .L2025
	strh	r1, [r2, r3]	@ movhi
.L1998:
	ldr	r3, .L2025
	ldr	r2, .L2025+32
	ldrh	r2, [r3, r2]
	cmp	r2, #2
	ldrls	r2, .L2025+20
	movwls	r3, #3912
	ldrlsh	r6, [r2, r3]
	bls	.L2019
.L2001:
	mov	r1, #0
	str	r1, [r3, #-1000]
	mov	r1, #400
	ldrh	r0, [r3, r1]
	cmp	r0, #0
	addeq	r0, r2, #1
.L1960:
	add	sp, sp, #36
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, sl, fp, pc}
.L2026:
	.align	2
.L2025:
	.word	.LANCHOR2
	.word	-1136
	.word	-1138
	.word	-1756
	.word	-1184
	.word	.LANCHOR0
	.word	.LC115
	.word	-1948
	.word	-2048
	.word	-1190
	.word	-1192
	.word	.LC116
	.word	-1772
	.word	.LANCHOR2-1756
	.word	-1754
	.word	-2044
	.word	-1996
	.word	-1152
	.word	-1944
	.word	.LANCHOR2-1742
	.fnend
	.size	rk_ftl_garbage_collect.part.18, .-rk_ftl_garbage_collect.part.18
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
	ldr	r3, .L2033
	mov	r5, r1
	ldr	r0, [r3, #-1000]
	cmp	r0, #0
	movne	r0, #0
	ldmnefd	sp!, {r3, r4, r5, pc}
	ldr	r2, .L2033+4
	ldrh	r2, [r3, r2]
	cmp	r2, #47
	ldmlsfd	sp!, {r3, r4, r5, pc}
	ldr	r1, .L2033+8
	movw	r2, #3000
	ldrh	r1, [r1, r2]
	movw	r2, #65535
	cmp	r1, r2
	beq	.L2029
	ldr	r1, .L2033+12
	ldrh	r3, [r3, r1]
	cmp	r3, r2
	beq	.L2029
	mov	r0, #1
	bl	FtlGcFreeTempBlock
	cmp	r0, #0
	bne	.L2032
.L2029:
	mov	r0, r4
	mov	r1, r5
	ldmfd	sp!, {r3, r4, r5, lr}
	b	rk_ftl_garbage_collect.part.18
.L2032:
	mov	r0, #1
	ldmfd	sp!, {r3, r4, r5, pc}
.L2034:
	.align	2
.L2033:
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
	ldr	r3, .L2057
	stmfd	sp!, {r0, r1, r4, r5, r6, r7, r8, r9, sl, lr}
	.save {r0, r1, r4, r5, r6, r7, r8, r9, sl, lr}
	ldr	r4, [r3, #408]
	ldr	r3, .L2057+4
	ldr	r1, [r3, #3964]
	cmp	r1, #0
	beq	.L2036
	ldrb	r8, [r3, #80]	@ zero_extendqisi2
	cmp	r8, #0
	beq	.L2037
	ldrb	r8, [r4, #8]	@ zero_extendqisi2
	sub	lr, r8, #1
	rsbs	r8, lr, #0
	adc	r8, r8, lr
.L2037:
	ldr	r0, [r3, #3968]
	mov	r2, r8
	ldrb	r3, [r4, #9]	@ zero_extendqisi2
	mov	r5, #0
	bl	FlashProgPages
	mov	r7, r5
	ldr	r6, .L2057+4
	ldr	r9, .L2057
	b	.L2038
.L2043:
	ldr	r2, [r6, #3968]
	add	r3, r2, r5
	ldr	r2, [r2, r5]
	cmn	r2, #1
	beq	.L2056
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
	beq	.L2041
	ubfx	r0, r0, #10, #16
	bl	P2V_block_in_plane
	ldr	r2, [r9, #-2064]
	mov	r3, r0, asl #1
	mov	sl, r0
	ldrh	r2, [r2, r3]
	cmp	r2, #0
	bne	.L2042
	ldr	r0, .L2057+8
	mov	r1, sl
	bl	printk
.L2042:
	mov	r0, sl
	bl	decrement_vpc_count
.L2041:
	add	r7, r7, #1
	add	r5, r5, #36
.L2038:
	ldr	r3, [r6, #3964]
	cmp	r7, r3
	bcc	.L2043
	b	.L2044
.L2052:
	ldr	r3, [r6, #3968]
	mvn	r2, #0
	movw	sl, #3912
	str	r2, [r3, r5]
	b	.L2045
.L2048:
	ldr	r0, [r3, #4]
	ubfx	r0, r0, #10, #16
	bl	P2V_block_in_plane
	ldrh	r3, [r4, #0]
	cmp	r3, r0
	bne	.L2046
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
.L2046:
	ldrh	r3, [r4, #4]
	cmp	r3, #0
	bne	.L2047
	mov	r0, r4
	bl	allocate_new_data_superblock
.L2047:
	ldr	r3, [r7, #-1608]
	add	r3, r3, #1
	str	r3, [r7, #-1608]
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
.L2045:
	ldr	r2, [r6, #3968]
	add	r3, r2, r5
	ldr	r2, [r2, r5]
	cmn	r2, #1
	beq	.L2048
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
	beq	.L2050
	ubfx	r0, r0, #10, #16
	bl	P2V_block_in_plane
	ldr	r2, [r7, #-2064]
	mov	r3, r0, asl #1
	mov	sl, r0
	ldrh	r2, [r2, r3]
	cmp	r2, #0
	bne	.L2051
	ldr	r0, .L2057+8
	mov	r1, sl
	bl	printk
.L2051:
	mov	r0, sl
	bl	decrement_vpc_count
.L2050:
	add	r9, r9, #1
	add	r5, r5, #36
	b	.L2039
.L2056:
	ldr	r6, .L2057+4
	mov	r5, #0
	ldr	r7, .L2057
	mov	r9, r5
.L2039:
	ldr	r3, [r6, #3964]
	cmp	r9, r3
	bcc	.L2052
	movw	r4, #16386
	ldr	r6, .L2057
	ldr	r5, .L2057+12
	b	.L2053
.L2054:
	mov	r0, #1
	mov	r1, r0
	bl	rk_ftl_garbage_collect
	subs	r4, r4, #1
	beq	.L2044
.L2053:
	ldrh	r3, [r6, r5]
	cmp	r3, #0
	bne	.L2054
.L2044:
	ldr	r3, .L2057+4
	mov	r2, #0
	str	r2, [r3, #3964]
.L2036:
	mov	r0, #0
	ldmfd	sp!, {r2, r3, r4, r5, r6, r7, r8, r9, sl, pc}
.L2058:
	.align	2
.L2057:
	.word	.LANCHOR2
	.word	.LANCHOR0
	.word	.LC117
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
	movw	r3, #3912
	ldr	r2, .L2081
	ldrh	r1, [r2, r3]
	ldr	r2, .L2081+4
	ldr	r3, .L2081+8
	ldrh	r4, [r3, r2]
	movw	r2, #65535
	cmp	r4, r2
	beq	.L2061
	cmp	r0, #0
	beq	.L2062
	ldr	ip, .L2081+12
	movw	r0, #3000
	ldrh	lr, [ip, r0]
	cmp	lr, r2
	movne	r1, #2
	bne	.L2062
	mov	r2, #0
	strh	r2, [ip, r0]	@ movhi
	sub	r2, r2, #2048
	ldrh	r3, [r3, r2]
	cmp	r3, #17
	movhi	r1, #2
.L2062:
	ldr	r0, .L2081+16
	bl	FtlGcScanTempBlk
	cmn	r0, #1
	str	r0, [sp, #4]
	beq	.L2063
	ldr	r3, .L2081+8
	mov	r4, r4, asl #1
	ldr	r3, [r3, #-2084]
	ldrh	r2, [r3, r4]
	cmp	r2, #4
	bls	.L2064
	sub	r2, r2, #5
	mov	r0, #1
	strh	r2, [r3, r4]	@ movhi
	bl	FtlEctTblFlush
.L2064:
	ldr	r3, .L2081+8
	ldr	r2, [r3, #-1016]
	cmp	r2, #0
	bne	.L2065
	ldr	r2, [r3, #-1608]
	ldr	r0, [sp, #4]
	add	r2, r2, #1
	str	r2, [r3, #-1608]
	ubfx	r0, r0, #10, #16
	bl	FtlBbmMapBadBlock
	bl	FtlBbmTblFlush
.L2065:
	ldr	r3, .L2081+8
	mov	r2, #0
	str	r2, [r3, #-1016]
	b	.L2076
.L2063:
	ldr	r2, .L2081+12
	movw	r3, #3000
	ldrh	r2, [r2, r3]
	movw	r3, #65535
	cmp	r2, r3
	bne	.L2076
.L2061:
	ldr	r6, .L2081+8
	movw	r3, #65535
	ldr	r5, .L2081+4
	mov	r4, #0
	str	r4, [r6, #-1016]
	ldrh	r2, [r6, r5]
	cmp	r2, r3
	moveq	r0, r4
	beq	.L2066
	bl	FtlCacheWriteBack
	ldr	ip, .L2081
	movw	r0, #3912
	ldrb	r1, [r6, #-1941]	@ zero_extendqisi2
	ldrh	r3, [r6, r5]
	ldrh	r0, [ip, r0]
	ldr	r2, [r6, #-2064]
	ldr	sl, .L2081+20
	mov	r3, r3, asl #1
	mul	r1, r0, r1
	strh	r1, [r2, r3]	@ movhi
	ldr	r3, [r6, #-1860]
	ldrh	r2, [r6, sl]
	add	r3, r2, r3
	str	r3, [r6, #-1860]
	b	.L2067
.L2070:
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
	bne	.L2068
	ubfx	r0, r0, #10, #16
	bl	P2V_block_in_plane
	add	r1, r5, #4
	mov	r2, #1
	mov	r7, r0
	ldr	r0, [r5, #8]
	bl	log2phys
	mov	r0, r7
	b	.L2079
.L2068:
	ldr	r3, [r5, #4]
	cmp	r0, r3
	beq	.L2069
	ldr	r3, .L2081+4
	ldrh	r0, [r6, r3]
.L2079:
	bl	decrement_vpc_count
.L2069:
	add	r4, r4, #1
	uxth	r4, r4
.L2067:
	ldrh	r3, [r6, sl]
	ldr	r5, .L2081+8
	cmp	r3, r4
	bhi	.L2070
	movw	r0, #65535
	bl	decrement_vpc_count
	ldr	r3, .L2081+4
	ldr	r1, [r5, #-2064]
	ldrh	r0, [r5, r3]
	mov	r5, r3
	mov	r2, r0, asl #1
	ldrh	r2, [r1, r2]
	cmp	r2, #0
	beq	.L2071
	bl	INSERT_DATA_LIST
	b	.L2072
.L2071:
	bl	INSERT_FREE_LIST
.L2072:
	ldr	r4, .L2081+8
	mvn	r3, #0
	ldr	r2, .L2081+20
	strh	r3, [r4, r5]	@ movhi
	mov	r3, #0
	strh	r3, [r4, r2]	@ movhi
	ldr	r2, .L2081+24
	strh	r3, [r4, r2]	@ movhi
	bl	l2p_flush
	bl	FtlVpcTblFlush
	ldr	r3, [r4, #-1884]
	mov	r1, r4
	ldr	r2, .L2081+28
	cmp	r3, #0
	beq	.L2073
	ldr	r3, [r4, #-1824]
	cmp	r3, #29
	bhi	.L2073
	ldrh	r3, [r4, r2]
	sub	r2, r2, #276
	mvn	r1, #0
	ldrh	r2, [r4, r2]
	cmp	r2, r3
	movcc	r3, r3, asl #1
	ldrcc	r2, .L2081+32
	strcch	r3, [r4, r2]	@ movhi
	ldr	r3, .L2081+36
	ldr	r2, .L2081+8
	b	.L2080
.L2073:
	ldr	r0, .L2081+40
	ldrh	r2, [r1, r2]
	ldr	r3, .L2081+8
	ldrh	r0, [r1, r0]
	add	r2, r2, r2, asl #1
	cmp	r0, r2, lsr #2
	ble	.L2078
	ldr	r2, .L2081+36
	mvn	r1, #0
	strh	r1, [r3, r2]	@ movhi
	add	r2, r2, #564
	mov	r1, #20
.L2080:
	strh	r1, [r3, r2]	@ movhi
	b	.L2078
.L2076:
	mov	r0, #1
	b	.L2066
.L2078:
	mov	r0, #0
.L2066:
	ldmfd	sp!, {r1, r2, r3, r4, r5, r6, r7, r8, sl, pc}
.L2082:
	.align	2
.L2081:
	.word	.LANCHOR0
	.word	-1948
	.word	.LANCHOR2
	.word	.LANCHOR1
	.word	.LANCHOR2-1948
	.word	-1140
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
	ldr	r3, .L2092
	ldrb	r3, [r3, #80]	@ zero_extendqisi2
	cmp	r3, #0
	ldr	r3, .L2092+4
	beq	.L2084
	ldr	r2, [r3, #-1176]
	tst	r2, #1
	beq	.L2084
	ldr	r2, .L2092+8
	ldrh	r2, [r3, r2]
	cmp	r2, #0
	bne	.L2089
.L2084:
	mov	r2, #0
	ldr	r0, [r3, #-980]
	ldr	r1, [r3, #-1176]
	mov	r3, r2
	bl	FlashProgPages
	mov	r4, #0
	ldr	r5, .L2092+4
	mov	r6, #36
	b	.L2086
.L2088:
	mul	r3, r6, r4
	ldr	r1, [r5, #-980]
	add	r2, r1, r3
	ldr	r1, [r1, r3]
	cmn	r1, #1
	bne	.L2087
	ldr	r2, .L2092+12
	mov	lr, #0
	ldr	ip, [r7, #-2064]
	ldrh	r0, [r7, r2]
	mov	r0, r0, asl #1
	strh	lr, [ip, r0]	@ movhi
	strh	r1, [r7, r2]	@ movhi
	ldr	r2, [r7, #-1608]
	add	r2, r2, #1
	str	r2, [r7, #-1608]
	ldr	r2, [r7, #-980]
	add	r3, r2, r3
	ldr	r0, [r3, #4]
	ubfx	r0, r0, #10, #16
	bl	FtlBbmMapBadBlock
	bl	FtlBbmTblFlush
	bl	FtlGcPageVarInit
	b	.L2091
.L2087:
	ldr	r3, [r2, #12]
	add	r4, r4, #1
	ldr	r1, [r2, #4]
	uxth	r4, r4
	ldr	r0, [r3, #12]
	ldr	r2, [r3, #8]
	bl	FtlGcUpdatePage
.L2086:
	ldr	r1, [r5, #-1176]
	ldr	r7, .L2092+4
	cmp	r4, r1
	bcc	.L2088
	ldr	r0, [r7, #-980]
	bl	FtlGcBufFree
	ldr	r3, .L2092+8
	mov	r0, #0
	str	r0, [r7, #-1176]
	ldrh	r3, [r7, r3]
	cmp	r3, r0
	ldmnefd	sp!, {r3, r4, r5, r6, r7, pc}
	mov	r0, #1
	bl	FtlGcFreeTempBlock
.L2091:
	mov	r0, #1
	ldmfd	sp!, {r3, r4, r5, r6, r7, pc}
.L2089:
	mov	r0, #0
	ldmfd	sp!, {r3, r4, r5, r6, r7, pc}
.L2093:
	.align	2
.L2092:
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
	movw	r5, #3912
	ldr	r6, .L2096
	ldr	r0, .L2096+4
	ldr	r4, .L2096+8
	ldrh	r1, [r6, r5]
	bl	FtlGcScanTempBlk
	ldr	r3, .L2096+12
	ldrh	r2, [r4, r3]
	ldrh	r3, [r6, r5]
	cmp	r2, r3
	ldmccfd	sp!, {r4, r5, r6, pc}
	sub	r0, r4, #1088
	bl	FtlMapBlkWriteDumpData
	mov	r0, #0
	bl	FtlGcFreeTempBlock
	mov	r3, #0
	str	r3, [r4, #-1016]
	ldmfd	sp!, {r4, r5, r6, pc}
.L2097:
	.align	2
.L2096:
	.word	.LANCHOR0
	.word	.LANCHOR2-1948
	.word	.LANCHOR2
	.word	-1946
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
	movw	r2, #266
	ldr	r3, .L2099
	mov	r4, #0
	ldr	r5, .L2099+4
	strh	r4, [r3, r2]	@ movhi
	str	r4, [r3, #268]
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
.L2100:
	.align	2
.L2099:
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
	stmfd	sp!, {r4, r5, r6, lr}
	.save {r4, r5, r6, lr}
	ldr	r5, .L2112
	ldr	r4, .L2112+4
	ldr	r0, [r5, #3848]
	uxth	r0, r0
	bl	FtlFreeSysBlkQueueInit
	bl	FtlScanSysBlk
	ldr	r3, .L2112+8
	ldrh	r2, [r4, r3]
	movw	r3, #65535
	cmp	r2, r3
	beq	.L2110
	bl	FtlLoadSysInfo
	subs	r6, r0, #0
	bne	.L2110
	bl	FtlLoadMapInfo
	bl	FtlLoadVonderInfo
	bl	Ftl_load_ext_data
	bl	FtlLoadEctTbl
	bl	FtlFreeSysBLkSort
	bl	SupperBlkListInit
	bl	FtlPowerLostRecovery
	movw	r3, #3950
	ldrh	r2, [r5, r3]
	mov	r3, r6
	ldr	r1, [r4, #-1900]
	b	.L2103
.L2105:
	add	r0, r1, r6
	add	r6, r6, #12
	ldr	r0, [r0, #4]
	cmp	r0, #0
	blt	.L2104
	add	r3, r3, #1
.L2103:
	cmp	r3, r2
	blt	.L2105
.L2104:
	ldr	r4, .L2112+4
	cmp	r3, r2
	ldr	r1, .L2112+12
	ldrh	r0, [r4, r1]
	add	r0, r0, #1
	strh	r0, [r4, r1]	@ movhi
	blt	.L2106
	movw	r3, #266
	ldrh	r3, [r4, r3]
	cmp	r3, #0
	beq	.L2107
.L2106:
	ldr	r0, .L2112+16
	bl	FtlSuperblockPowerLostFix
	ldr	r0, .L2112+20
	bl	FtlSuperblockPowerLostFix
	ldr	r3, .L2112+24
	ldr	r1, [r4, #-2064]
	ldr	r2, .L2112+28
	ldrh	r3, [r4, r3]
	ldrh	r0, [r4, r2]
	mov	r3, r3, asl #1
	ldrh	ip, [r1, r3]
	rsb	r0, r0, ip
	strh	r0, [r1, r3]	@ movhi
	ldr	r0, .L2112
	movw	r1, #3912
	ldr	r3, .L2112+32
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
	ldr	r1, .L2112+36
	strh	r3, [r4, r2]	@ movhi
	strb	r3, [r4, #-1990]
	strh	r0, [r4, r1]	@ movhi
	ldr	r0, .L2112+40
	bl	FtlMapBlkWriteDumpData
	ldr	r0, .L2112+44
	bl	FtlMapBlkWriteDumpData
	ldr	r3, .L2112+48
	ldrh	r2, [r4, r3]
	add	r2, r2, #1
	strh	r2, [r4, r3]	@ movhi
	bl	l2p_flush
	bl	FtlVpcTblFlush
	bl	FtlVpcTblFlush
.L2107:
	mov	r0, #1
	ldr	r4, .L2112+4
	bl	FtlUpdateVaildLpn
	ldr	r3, .L2112+24
	ldrh	r0, [r4, r3]
	movw	r3, #65535
	cmp	r0, r3
	beq	.L2108
	ldr	r3, .L2112+28
	ldrh	r3, [r4, r3]
	cmp	r3, #0
	bne	.L2108
	ldr	r3, .L2112+52
	ldrh	r3, [r4, r3]
	cmp	r3, #0
	bne	.L2108
	bl	FtlGcRefreshBlock
	ldr	r3, .L2112+56
	ldrh	r0, [r4, r3]
	bl	FtlGcRefreshBlock
	ldr	r0, .L2112+16
	bl	allocate_new_data_superblock
	ldr	r0, .L2112+20
	bl	allocate_new_data_superblock
	add	r0, r4, #224
	bl	FtlMapBlkWriteDumpData
.L2108:
	ldr	r3, .L2112+12
	ldr	r2, .L2112+4
	ldrh	r0, [r2, r3]
	ands	r4, r0, #31
	bne	.L2111
	bl	FtlVpcCheckAndModify
	mov	r0, r4
	ldmfd	sp!, {r4, r5, r6, pc}
.L2110:
	mvn	r0, #0
	ldmfd	sp!, {r4, r5, r6, pc}
.L2111:
	mov	r0, #0
	ldmfd	sp!, {r4, r5, r6, pc}
.L2113:
	.align	2
.L2112:
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
	.word	.LANCHOR2-1088
	.word	.LANCHOR2+224
	.word	-1790
	.word	-1992
	.word	-1996
	.fnend
	.size	FtlSysBlkInit, .-FtlSysBlkInit
	.align	2
	.global	FtlInit
	.type	FtlInit, %function
FtlInit:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
.L2115:
	stmfd	sp!, {r3, r4, r5, lr}
	.save {r3, r4, r5, lr}
	mvn	r3, #0
	ldr	r4, .L2117
	mov	r2, #0
	ldr	r1, .L2117+4
	mov	r5, r0
	ldr	r0, .L2117+8
	str	r3, [r4, #2960]
	ldr	r3, .L2117+12
	str	r2, [r3, #412]
	bl	printk
	mov	r0, r5
	bl	FtlConstantsInit
	bl	FtlMemInit
	bl	FtlVariablesInit
	ldr	r3, .L2117+16
	ldr	r0, [r3, #3848]
	uxth	r0, r0
	bl	FtlFreeSysBlkQueueInit
	bl	FtlLoadBbt
	cmp	r0, #0
	bne	.L2116
	bl	FtlSysBlkInit
	cmp	r0, #0
	moveq	r3, #1
	streq	r3, [r4, #2960]
.L2116:
	mov	r0, #0
	ldmfd	sp!, {r3, r4, r5, pc}
.L2118:
	.align	2
.L2117:
	.word	.LANCHOR1
	.word	.LC75
	.word	.LC74
	.word	.LANCHOR2
	.word	.LANCHOR0
	.fnend
	.size	FtlInit, .-FtlInit
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
	ldr	r4, .L2123
	mov	r5, #0
	add	r1, r4, #420
	str	r5, [r4, #112]
	str	r5, [r4, #420]
	str	r0, [r4, #416]
	add	r0, r4, #112
	bl	rknand_get_reg_addr
	ldr	r3, [r4, #112]
	cmp	r3, r5
	mvneq	r4, #0
	beq	.L2120
	bl	rk_nandc_irq_init
	ldr	r0, [r4, #416]
	mov	r1, r5
	mov	r2, r5
	mov	r3, #2048
	bl	FlashSramLoadStore
	bl	rknand_flash_cs_init
	ldr	r0, [r4, #112]
	bl	FlashInit
	subs	r4, r0, #0
	bne	.L2121
	ldr	r0, .L2123+4
	bl	FtlInit
.L2121:
	ldr	r0, .L2123+8
	mov	r1, r4
	bl	printk
.L2120:
	mov	r0, r4
	ldmfd	sp!, {r3, r4, r5, pc}
.L2124:
	.align	2
.L2123:
	.word	.LANCHOR2
	.word	.LANCHOR0+2200
	.word	.LC118
	.fnend
	.size	rk_ftl_init, .-rk_ftl_init
	.align	2
	.global	Ftl_get_new_temp_ppa
	.type	Ftl_get_new_temp_ppa, %function
Ftl_get_new_temp_ppa:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	stmfd	sp!, {r3, r4, r5, lr}
	.save {r3, r4, r5, lr}
	ldr	r3, .L2128
	ldr	r5, .L2128+4
	ldrh	r2, [r5, r3]
	movw	r3, #65535
	cmp	r2, r3
	beq	.L2126
	ldr	r3, .L2128+8
	ldrh	r3, [r5, r3]
	cmp	r3, #0
	bne	.L2127
.L2126:
	bl	FtlCacheWriteBack
	mov	r0, #0
	bl	FtlGcFreeTempBlock
	ldr	r0, .L2128+12
	mov	r4, #0
	strb	r4, [r5, #-1940]
	bl	allocate_data_superblock
	ldr	r3, .L2128+16
	strh	r4, [r5, r3]	@ movhi
	add	r3, r3, #12
	strh	r4, [r5, r3]	@ movhi
	bl	l2p_flush
	bl	FtlVpcTblFlush
	mov	r0, r4
	bl	FtlEctTblFlush
.L2127:
	ldr	r0, .L2128+12
	ldmfd	sp!, {r3, r4, r5, lr}
	b	get_new_active_ppa
.L2129:
	.align	2
.L2128:
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
	ldr	r5, .L2139
	mov	r7, r0
	mov	r4, r1
	ldr	r3, [r5, #3952]
	cmp	r2, r3
	mvnhi	r0, #0
	bhi	.L2131
	cmp	r1, #31
	movls	r0, #0
	bls	.L2131
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
	beq	.L2132
	rsb	r6, r7, r6
	add	r5, r0, #1
	cmp	r6, r4
	movcs	r6, r4
	uxth	r6, r6
	rsb	r4, r6, r4
.L2132:
	mvn	r3, #0
	ldr	r7, .L2139
	str	r3, [sp, #4]
	movw	r6, #3918
	b	.L2133
.L2135:
	mov	r0, r5
	mov	r1, sp
	mov	r2, #0
	bl	log2phys
	ldr	r3, [sp, #0]
	cmn	r3, #1
	beq	.L2134
	ldr	r3, .L2139+4
	add	r1, sp, #4
	mov	r0, r5
	ldr	r2, [r3, #424]
	add	r2, r2, #1
	str	r2, [r3, #424]
	ldr	r2, [r3, #-1876]
	add	r2, r2, #1
	str	r2, [r3, #-1876]
	mov	r2, #1
	bl	log2phys
	ldr	r0, [sp, #0]
	ubfx	r0, r0, #10, #16
	bl	P2V_block_in_plane
	bl	decrement_vpc_count
.L2134:
	ldrh	r3, [r7, r6]
	add	r5, r5, #1
	rsb	r4, r3, r4
.L2133:
	ldrh	r3, [r7, r6]
	cmp	r4, r3
	bcs	.L2135
	ldr	r3, .L2139+4
	mov	r4, #0
	ldr	r2, [r3, #424]
	cmp	r2, #32
	bls	.L2138
	str	r4, [r3, #424]
	bl	l2p_flush
	bl	FtlVpcTblFlush
.L2138:
	mov	r0, r4
.L2131:
	ldmfd	sp!, {r1, r2, r3, r4, r5, r6, r7, pc}
.L2140:
	.align	2
.L2139:
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
	bl	FtlVpcTblFlush
	mov	r0, #1
	bl	FtlEctTblFlush
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
	ldr	r3, .L2144
	ldr	r3, [r3, #2960]
	cmp	r3, #1
	bne	.L2143
	bl	FtlSysFlush
.L2143:
	mov	r0, #0
	ldmfd	sp!, {r3, pc}
.L2145:
	.align	2
.L2144:
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
	mov	r8, r3
	bne	.L2148
	add	r0, r1, #256
	mov	r1, r2
	mov	r2, r3
	bl	FtlVendorPartWrite
	b	.L2149
.L2148:
	ldr	r9, .L2197
	add	r7, r2, r1
	ldr	r3, [r9, #3952]
	cmp	r7, r3
	mvnhi	r0, #0
	bhi	.L2149
	ldr	r6, .L2197+4
	mov	r3, #2048
	mov	r0, r1
	str	r3, [r6, #428]
	movw	r3, #3918
	ldrh	sl, [r9, r3]
	mov	r1, sl
	bl	__aeabi_uidiv
	mov	r1, sl
	str	r0, [sp, #20]
	sub	r0, r7, #1
	bl	__aeabi_uidiv
	ldr	r3, [sp, #20]
	cmp	r5, #8
	rsb	fp, r3, r0
	ldr	r3, [r6, #-1872]
	add	r7, fp, #1
	str	r7, [sp, #16]
	add	r3, r7, r3
	str	r3, [r6, #-1872]
	ldr	r3, [r6, #-1856]
	str	r0, [sp, #44]
	add	r3, r5, r3
	str	r3, [r6, #-1856]
	ldr	r3, .L2197+8
	add	r7, r3, #48
	movhi	r7, r3
	ldr	r3, [r9, #3964]
	cmp	r3, #0
	beq	.L2183
	ldr	r2, [r9, #3968]
	sub	r3, r3, #1
	mov	r7, #36
	ldr	lr, [sp, #20]
	mla	r7, r7, r3, r2
	ldr	r3, [r7, #16]
	cmp	lr, r3
	strne	r5, [sp, #32]
	bne	.L2152
	ldr	r3, [r6, #-1868]
	mov	r0, r4
	mov	r1, sl
	add	r3, r3, #1
	str	r3, [r6, #-1868]
	ldr	r3, [r6, #432]
	add	r3, r3, #1
	str	r3, [r6, #432]
	bl	__aeabi_uidivmod
	ldr	r0, [r7, #8]
	rsb	r9, r1, sl
	add	r0, r0, r1, asl #9
	cmp	r9, r5
	movcs	r9, r5
	mov	r1, r8
	mov	r3, r9, asl #9
	str	r3, [sp, #4]
	mov	r2, r3
	bl	memcpy
	cmp	fp, #0
	ldr	r3, [sp, #4]
	bne	.L2153
	ldr	r2, [r6, #432]
	cmp	r2, #2
	movle	r0, fp
	ble	.L2149
.L2153:
	add	r8, r8, r3
	add	r4, r4, r9
	ldr	r3, [sp, #20]
	rsb	r7, r9, r5
	str	fp, [sp, #16]
	add	r3, r3, #1
	str	r7, [sp, #32]
	str	r3, [sp, #20]
.L2152:
	ldr	r3, .L2197+4
	mov	r2, #0
	ldr	r7, [r3, #408]
	str	r2, [r3, #432]
	b	.L2151
.L2183:
	str	r5, [sp, #32]
.L2151:
	ldr	r0, [sp, #20]
	ldr	r1, [sp, #44]
	bl	FtlCacheMetchLpa
	cmp	r0, #0
	beq	.L2154
	bl	FtlCacheWriteBack
.L2154:
	cmp	r5, sl, asl #1
	ldr	r5, .L2197
	mov	fp, #0
	ldr	r6, [sp, #20]
	movcc	r3, #0
	movcs	r3, #1
	str	r3, [sp, #24]
	ldr	r3, .L2197+4
	mov	sl, r5
	str	fp, [sp, #40]
	mov	r9, r8
	str	r7, [r3, #408]
	b	.L2194
.L2180:
	ldrh	r1, [r7, #4]
	cmp	r1, #0
	bne	.L2156
	ldr	r3, .L2197+8
	ldr	r2, .L2197+4
	cmp	r7, r3
	ldr	r8, .L2197+12
	bne	.L2157
	ldr	r3, .L2197+16
	ldrh	r3, [r2, r3]
	cmp	r3, #0
	bne	.L2158
	ldr	r0, .L2197+20
	str	r3, [sp, #4]
	bl	allocate_new_data_superblock
	ldr	r3, [sp, #4]
	str	r3, [r8, #3004]
.L2158:
	ldr	r0, .L2197+8
	bl	allocate_new_data_superblock
	ldr	r2, [r8, #3004]
	ldr	r3, .L2197+20
	cmp	r2, #0
	movne	r7, r3
	b	.L2159
.L2157:
	str	r1, [r8, #3004]
	ldr	r1, .L2197+24
	ldrh	r2, [r2, r1]
	cmp	r2, #0
	movne	r7, r3
	bne	.L2159
	mov	r0, r7
	bl	allocate_new_data_superblock
.L2159:
	ldrh	r3, [r7, #4]
	cmp	r3, #0
	bne	.L2160
	mov	r0, r7
	bl	allocate_new_data_superblock
.L2160:
	ldr	r3, .L2197+4
	str	r7, [r3, #408]
.L2156:
	ldr	r2, .L2197+4
	mov	r8, r7
	ldrh	r3, [r7, #4]
	ldr	r1, [r2, #-988]
	ldr	r2, [r5, #3964]
	rsb	r2, r2, r1
	cmp	r3, r2
	movcc	r2, r3
	ldr	r3, [sp, #16]
	cmp	r2, r3
	movcc	r3, r2
	str	r3, [sp, #36]
	mov	r3, #0
	str	r3, [sp, #28]
	b	.L2161
.L2177:
	ldrh	r3, [r8, #4]
	cmp	r3, #0
	beq	.L2189
	ldr	r7, [sp, #44]
	rsb	r2, r7, r6
	ldr	r7, [sp, #24]
	rsbs	r3, r2, #0
	adc	r3, r3, r2
	tst	r3, r7
	beq	.L2163
	ldr	r7, [sp, #28]
	cmp	r7, #0
	beq	.L2163
	movw	lr, #3918
	ldr	r7, [sp, #32]
	ldrh	r2, [r5, lr]
	add	r1, r7, r4
	mls	r1, r2, r6, r1
	cmp	r1, r2
	bne	.L2189
.L2163:
	add	r1, sp, #56
	mov	r2, #0
	mov	r0, r6
	str	r3, [sp, #4]
	bl	log2phys
	mov	r0, r8
	bl	get_new_active_ppa
	ldr	lr, [r5, #3964]
	ldr	r1, [r5, #3968]
	movw	r2, #3924
	mov	ip, #36
	ldrh	r2, [r5, r2]
	ldr	r7, .L2197+4
	mla	r1, ip, lr, r1
	mul	lr, lr, r2
	ldr	r7, [r7, #-944]
	bic	lr, lr, #3
	str	r7, [sp, #52]
	str	lr, [sp, #48]
	add	r7, r7, lr
	str	r6, [r1, #16]
	str	r7, [r1, #12]
	str	r0, [r1, #4]
	movw	r0, #3922
	ldrh	lr, [r5, r0]
	ldr	r0, [r5, #3964]
	mul	lr, r0, lr
	ldr	r0, .L2197+4
	bic	lr, lr, #3
	str	lr, [sp, #12]
	ldr	lr, [r0, #-964]
	ldr	r0, [sp, #12]
	add	lr, lr, r0
	mov	r0, r7
	str	lr, [r1, #8]
	mov	r1, #0
	str	ip, [sp, #8]
	bl	ftl_memset
	ldr	r1, [sp, #20]
	ldr	ip, [sp, #8]
	rsb	r3, r1, r6
	rsbs	r1, r3, #0
	adc	r1, r1, r3
	ldr	r3, [sp, #4]
	str	r1, [sp, #12]
	orrs	r1, r1, r3
	beq	.L2164
	ldr	r2, [sp, #12]
	cmp	r2, #0
	beq	.L2165
	movw	r3, #3918
	mov	r0, r4
	ldrh	fp, [r5, r3]
	mov	r1, fp
	bl	__aeabi_uidivmod
	ldr	r3, [sp, #32]
	rsb	fp, r1, fp
	str	r1, [sp, #40]
	cmp	fp, r3
	movcs	fp, r3
	b	.L2166
.L2165:
	cmp	r3, #0
	beq	.L2166
	ldr	r3, [sp, #32]
	movw	lr, #3918
	add	fp, r3, r4
	ldrh	r3, [r5, lr]
	mls	fp, r3, r6, fp
	ldr	r3, [sp, #12]
	str	r3, [sp, #40]
	uxth	fp, fp
.L2166:
	movw	lr, #3918
	ldrh	r3, [r5, lr]
	cmp	fp, r3
	bne	.L2167
	ldr	r3, [sp, #12]
	ldr	r0, [sl, #3964]
	cmp	r3, #0
	ldr	r2, [sl, #3968]
	ldr	r3, [sp, #24]
	muleq	r1, r6, fp
	movne	r1, r9
	rsbeq	r1, r4, r1
	addeq	r1, r9, r1, asl #9
	cmp	r3, #0
	mov	r3, #36
	mla	r3, r3, r0, r2
	strne	r1, [r3, #8]
	bne	.L2170
	ldr	r0, [r3, #8]
	movw	r3, #3922
	ldrh	r2, [sl, r3]
	b	.L2195
.L2167:
	ldr	r2, [sp, #56]
	mov	r3, #36
	cmn	r2, #1
	beq	.L2171
	ldr	r1, [r5, #3964]
	add	r0, sp, #60
	str	r2, [sp, #64]
	ldr	r2, [r5, #3968]
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
	ldr	r3, .L2197+4
	ldreq	r2, [r3, #-1632]
	addeq	r2, r2, #1
	streq	r2, [r3, #-1632]
	beq	.L2173
	ldr	r2, [r7, #8]
	cmp	r2, r6
	beq	.L2173
	ldr	r2, [r3, #-1632]
	ldr	r0, .L2197+28
	add	r2, r2, #1
	str	r2, [r3, #-1632]
	mov	r2, r6
	ldr	r1, [r7, #8]
	bl	printk
	b	.L2173
.L2171:
	ldr	r1, [r5, #3964]
	ldr	r2, [r5, #3968]
	mla	r3, r3, r1, r2
	mov	r1, #0
	ldr	r0, [r3, #8]
	movw	r3, #3922
	ldrh	r2, [r5, r3]
	bl	ftl_memset
.L2173:
	ldr	r3, [sp, #12]
	ldr	r1, [r5, #3964]
	cmp	r3, #0
	ldr	r2, [r5, #3968]
	mov	r3, #36
	movweq	lr, #3918
	mla	r3, r3, r1, r2
	ldreqh	r1, [r5, lr]
	movne	r1, r9
	mov	r2, fp, asl #9
	muleq	r1, r1, r6
	ldrne	r0, [r3, #8]
	ldrne	r3, [sp, #40]
	rsbeq	r1, r4, r1
	ldreq	r0, [r3, #8]
	addne	r0, r0, r3, asl #9
	addeq	r1, r9, r1, asl #9
	b	.L2195
.L2164:
	ldr	r3, [sp, #24]
	ldr	r2, [r5, #3964]
	cmp	r3, #0
	ldr	r3, [r5, #3968]
	mla	ip, ip, r2, r3
	beq	.L2175
	movw	lr, #3918
	ldrh	r3, [r5, lr]
	mul	r3, r3, r6
	rsb	r3, r4, r3
	add	r3, r9, r3, asl #9
	str	r3, [ip, #8]
	b	.L2170
.L2175:
	movw	r0, #3918
	movw	r3, #3922
	ldrh	r1, [r5, r0]
	ldrh	r2, [r5, r3]
	ldr	r0, [ip, #8]
	mul	r1, r1, r6
	rsb	r1, r4, r1
	add	r1, r9, r1, asl #9
.L2195:
	bl	memcpy
.L2170:
	ldr	r3, .L2197+32
	ldr	r0, [sp, #52]
	ldr	lr, [sp, #48]
	strh	r3, [r0, lr]	@ movhi
	ldr	r3, .L2197+4
	str	r6, [r7, #8]
	add	r6, r6, #1
	ldr	r2, [r3, #-1844]
	str	r2, [r7, #4]
	add	r2, r2, #1
	cmn	r2, #1
	str	r2, [r3, #-1844]
	moveq	r2, #0
	streq	r2, [r3, #-1844]
	ldr	r3, [sp, #56]
	str	r3, [r7, #12]
	ldrh	r3, [r8, #0]
	strh	r3, [r7, #2]	@ movhi
	ldr	r3, [r5, #3964]
	add	r3, r3, #1
	str	r3, [r5, #3964]
	ldr	r3, [sp, #28]
	add	r3, r3, #1
	str	r3, [sp, #28]
.L2161:
	ldr	r7, [sp, #28]
	ldr	r3, [sp, #36]
	cmp	r7, r3
	bne	.L2177
	mov	r7, r8
	b	.L2162
.L2189:
	ldr	r3, [sp, #28]
	mov	r7, r8
	str	r3, [sp, #36]
.L2162:
	ldr	r3, [sp, #16]
	ldr	lr, [sp, #36]
	ldr	r2, [r5, #3964]
	rsb	r3, lr, r3
	str	r3, [sp, #16]
	ldr	r3, .L2197+4
	ldr	r0, [sp, #24]
	ldr	r3, [r3, #-988]
	cmp	r2, r3
	orrcs	r0, r0, #1
	uxtb	r3, r0
	cmp	r3, #0
	bne	.L2178
	ldrh	r2, [r7, #4]
	cmp	r2, #0
	bne	.L2192
.L2178:
	bl	FtlCacheWriteBack
	ldr	lr, [sp, #16]
	mov	r3, #0
	str	r3, [sl, #3964]
	cmp	lr, #3
	ldr	r3, [sp, #24]
	movls	r3, #0
.L2192:
	str	r3, [sp, #24]
.L2194:
	ldr	r3, [sp, #16]
	cmp	r3, #0
	bne	.L2180
	mov	r0, r3
	ldr	r7, [sp, #44]
	ldr	r3, [sp, #20]
	rsb	r1, r3, r7
	bl	rk_ftl_garbage_collect
	ldr	r0, [sp, #16]
.L2149:
	add	sp, sp, #100
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, sl, fp, pc}
.L2198:
	.align	2
.L2197:
	.word	.LANCHOR0
	.word	.LANCHOR2
	.word	.LANCHOR2-2044
	.word	.LANCHOR1
	.word	-1992
	.word	.LANCHOR2-1996
	.word	-2040
	.word	.LC119
	.word	-3947
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
	bne	.L2201
	add	r0, r1, #256
	mov	r1, r2
	mov	r2, r3
	bl	FtlVendorPartRead
	str	r0, [sp, #16]
	b	.L2202
.L2201:
	ldr	r3, [sp, #28]
	add	r3, r3, r1
	str	r3, [sp, #20]
	ldr	r3, .L2231
	ldr	r1, [sp, #20]
	ldr	r2, [r3, #3952]
	cmp	r1, r2
	mvnhi	r3, #0
	strhi	r3, [sp, #16]
	bhi	.L2202
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
	ldr	r3, .L2231+4
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
	beq	.L2203
	bl	FtlCacheWriteBack
.L2203:
	mov	r9, #0
	ldr	r6, [sp, #12]
	str	r9, [sp, #32]
	mov	r5, r9
	str	r9, [sp, #16]
	ldr	r7, .L2231
	b	.L2226
.L2220:
	mov	r2, #0
	mov	r0, r6
	add	r1, sp, #60
	bl	log2phys
	ldr	r2, [sp, #60]
	cmn	r2, #1
	bne	.L2227
	b	.L2230
.L2208:
	mla	r0, r0, r6, sl
	cmp	r0, r4
	bcc	.L2207
	ldr	r2, [sp, #20]
	cmp	r0, r2
	bcs	.L2207
	rsb	r0, r4, r0
	mov	r1, #0
	mov	r2, #512
	str	r3, [sp, #4]
	add	r0, r8, r0, asl #9
	bl	ftl_memset
	ldr	r3, [sp, #4]
.L2207:
	add	sl, sl, #1
	b	.L2205
.L2230:
	mov	sl, #0
	movw	r3, #3918
.L2205:
	ldrh	r0, [r7, r3]
	cmp	sl, r0
	bcc	.L2208
	b	.L2209
.L2227:
	ldr	r3, .L2231+4
	mov	sl, #36
	ldr	r1, [r3, #-984]
	mla	sl, sl, r5, r1
	ldr	r1, [sp, #12]
	cmp	r6, r1
	str	r2, [sl, #4]
	movw	r2, #3918
	bne	.L2210
	ldr	r3, [r3, #-960]
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
	b	.L2211
.L2210:
	cmp	r6, fp
	ldrneh	r3, [r7, r2]
	mulne	r3, r3, r6
	bne	.L2228
	ldr	r3, [r3, #-956]
	ldrh	r2, [r7, r2]
	ldr	r1, [sp, #20]
	str	r3, [sl, #8]
	mul	r3, r2, r6
	rsb	r9, r3, r1
	cmp	r9, r2
	bne	.L2211
.L2228:
	rsb	r3, r4, r3
	add	r3, r8, r3, asl #9
	str	r3, [sl, #8]
.L2211:
	movw	r3, #3924
	ldr	r2, .L2231+4
	ldrh	r3, [r7, r3]
	str	r6, [sl, #16]
	ldr	r2, [r2, #-948]
	mul	r3, r3, r5
	add	r5, r5, #1
	bic	r3, r3, #3
	add	r3, r2, r3
	str	r3, [sl, #12]
.L2209:
	ldr	r3, [sp, #8]
	add	r6, r6, #1
	subs	r3, r3, #1
	str	r3, [sp, #8]
	beq	.L2213
	movw	r3, #3844
	ldrh	r3, [r7, r3]
	cmp	r5, r3, asl #3
	bne	.L2226
.L2213:
	cmp	r5, #0
	beq	.L2226
	ldr	sl, .L2231+4
	mov	r1, r5
	mov	r2, #0
	ldr	r0, [sl, #-984]
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
.L2219:
	ldr	r3, [sp, #24]
	mov	r5, #36
	ldr	r1, [sp, #12]
	mul	r5, r5, r3
	ldr	r3, [sl, #-984]
	add	r3, r3, r5
	ldr	r2, [r3, #16]
	cmp	r2, r1
	bne	.L2215
	ldr	r1, [r3, #8]
	ldr	r3, [sl, #-960]
	cmp	r1, r3
	bne	.L2216
	ldr	r3, [sp, #40]
	mov	r0, r8
	ldr	r2, [sp, #44]
	add	r1, r1, r3
	b	.L2229
.L2215:
	cmp	r2, fp
	bne	.L2216
	ldr	r1, [r3, #8]
	ldr	r3, [sl, #-956]
	cmp	r1, r3
	bne	.L2216
	movw	r2, #3918
	ldrh	r0, [r7, r2]
	ldr	r2, [sp, #48]
	mul	r0, r0, fp
	rsb	r0, r4, r0
	add	r0, r8, r0, asl #9
.L2229:
	bl	memcpy
.L2216:
	ldr	r3, [sl, #-984]
	add	r0, r3, r5
	ldr	r2, [r3, r5]
	cmn	r2, #1
	streq	r2, [sp, #16]
	ldreq	r1, [sl, #-1632]
	addeq	r1, r1, #1
	streq	r1, [sl, #-1632]
	ldr	r3, [r3, r5]
	cmp	r3, #256
	bne	.L2218
	ldr	r0, [r0, #4]
	ubfx	r0, r0, #10, #16
	bl	P2V_block_in_plane
	bl	FtlGcRefreshBlock
.L2218:
	ldr	r3, [sp, #24]
	add	r3, r3, #1
	str	r3, [sp, #24]
	cmp	r3, r6
	bne	.L2219
	ldr	r6, [sp, #52]
	mov	r5, #0
.L2226:
	ldr	r3, [sp, #8]
	cmp	r3, #0
	bne	.L2220
	ldr	r3, .L2231+8
	ldr	r2, .L2231+4
	ldrh	r3, [r2, r3]
	cmp	r3, #0
	beq	.L2202
	ldr	r0, [sp, #8]
	mov	r1, #1
	bl	rk_ftl_garbage_collect
.L2202:
	ldr	r0, [sp, #16]
	add	sp, sp, #68
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, sl, fp, pc}
.L2232:
	.align	2
.L2231:
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
	.global	FlashLoadFactorBbt
	.type	FlashLoadFactorBbt, %function
FlashLoadFactorBbt:
	.fnstart
	@ args = 0, pretend = 0, frame = 48
	@ frame_needed = 0, uses_anonymous_args = 0
	stmfd	sp!, {r4, r5, r6, r7, r8, r9, sl, fp, lr}
	.save {r4, r5, r6, r7, r8, r9, sl, fp, lr}
	movw	r2, #2214
	ldr	r9, .L2243
	movw	r3, #2212
	ldr	r4, .L2243+4
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
	ldr	r3, [r4, #148]
	str	r6, [sp, #20]
	mov	r4, r6
	str	r3, [sp, #24]
	uxth	r8, r8
	add	r3, r8, r7
	uxth	r3, r3
	str	r3, [sp, #4]
	b	.L2235
.L2241:
	ldr	r5, [sp, #4]
	sub	r3, r8, #8
	mul	fp, r8, r4
	ldr	sl, .L2243+4
	b	.L2236
.L2239:
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
	beq	.L2237
	ldr	r2, [sl, #148]
	ldrh	r1, [r2, #0]
	movw	r2, #61664
	cmp	r1, r2
	bne	.L2237
	ldr	r0, .L2243+8
	mov	r1, r4
	mov	r2, r5
	add	r6, r6, #1
	bl	printk
	ldr	r3, .L2243+4
	uxth	r6, r6
	add	r3, r3, r4, asl #1
	strh	r5, [r3, #208]	@ movhi
	b	.L2238
.L2237:
	sub	r5, r5, #1
	uxth	r5, r5
.L2236:
	cmp	r5, r3
	bgt	.L2239
.L2238:
	ldrb	r3, [r9, #3766]	@ zero_extendqisi2
	add	r4, r4, #1
	cmp	r3, r6
	uxtb	r4, r4
	moveq	r7, #0
.L2235:
	ldrb	r3, [r9, #3766]	@ zero_extendqisi2
	cmp	r3, r4
	bhi	.L2241
	mov	r0, r7
	add	sp, sp, #52
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, sl, fp, pc}
.L2244:
	.align	2
.L2243:
	.word	.LANCHOR0
	.word	.LANCHOR2
	.word	.LC120
	.fnend
	.size	FlashLoadFactorBbt, .-FlashLoadFactorBbt
	.align	2
	.global	FlashReadFacBbtData
	.type	FlashReadFacBbtData, %function
FlashReadFacBbtData:
	.fnstart
	@ args = 0, pretend = 0, frame = 40
	@ frame_needed = 0, uses_anonymous_args = 0
	ldr	r3, .L2255
	stmfd	sp!, {r4, r5, r6, r7, r8, r9, sl, lr}
	.save {r4, r5, r6, r7, r8, r9, sl, lr}
	mov	r6, r1
	mov	r9, r2
	movw	r1, #2214
	movw	r2, #2212
	ldrh	r8, [r3, r1]
	ldrh	r2, [r3, r2]
	.pad #40
	sub	sp, sp, #40
	ldr	r3, [r3, #3780]
	mov	r4, r0
	ldr	r7, .L2255+4
	mul	r8, r8, r2
	str	r3, [sp, #12]
	ldr	r3, [r7, #148]
	str	r3, [sp, #16]
	uxth	r8, r8
	sub	r5, r8, #1
	mul	sl, r8, r6
	uxth	r5, r5
	sub	r8, r8, #16
	b	.L2246
.L2252:
	mov	r1, #1
	add	r3, r5, sl
	add	r0, sp, #4
	mov	r2, r1
	mov	r3, r3, asl #10
	str	r3, [sp, #8]
	bl	FlashReadPages
	ldr	r3, [sp, #4]
	cmn	r3, #1
	beq	.L2247
	ldr	r3, [r7, #148]
	ldrh	r2, [r3, #0]
	movw	r3, #61664
	cmp	r2, r3
	bne	.L2247
	cmp	r4, #0
	moveq	r0, r4
	beq	.L2248
	cmp	r6, #0
	beq	.L2254
	b	.L2250
.L2251:
	ldr	r3, [r0, #3780]
	ubfx	r2, r6, #5, #16
	and	r5, r6, #31
	add	r6, r6, #1
	ldr	lr, [r3, r2, asl #2]
	uxth	r6, r6
	orr	lr, lr, r1, asl r5
	str	lr, [r3, r2, asl #2]
	b	.L2249
.L2254:
	ldr	ip, .L2255+4
	mov	r1, #1
	ldr	r0, .L2255
.L2249:
	ldr	r3, [ip, #132]
	cmp	r6, r3
	bcc	.L2251
.L2250:
	ldr	r3, .L2255
	mov	r2, r9
	mov	r0, r4
	ldr	r1, [r3, #3780]
	bl	memcpy
	mov	r2, #4
	ldr	r0, .L2255+8
	mov	r1, r4
	mov	r3, r2
	bl	rknand_print_hex
	mov	r0, #0
	b	.L2248
.L2247:
	sub	r5, r5, #1
	uxth	r5, r5
.L2246:
	cmp	r5, r8
	bgt	.L2252
	mvn	r0, #0
.L2248:
	add	sp, sp, #40
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, sl, pc}
.L2256:
	.align	2
.L2255:
	.word	.LANCHOR0
	.word	.LANCHOR2
	.word	.LC121
	.fnend
	.size	FlashReadFacBbtData, .-FlashReadFacBbtData
	.align	2
	.global	FlashGetBadBlockList
	.type	FlashGetBadBlockList, %function
FlashGetBadBlockList:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	ldr	r3, .L2264
	stmfd	sp!, {r4, r5, r6, r7, r8, lr}
	.save {r4, r5, r6, r7, r8, lr}
	mov	r5, r0
	ldr	r3, [r3, #2776]
	ldr	r6, .L2264+4
	ldrb	r2, [r3, #13]	@ zero_extendqisi2
	ldrh	r4, [r3, #14]
	ldr	r0, [r6, #116]
	mul	r4, r4, r2
	uxth	r4, r4
	add	r2, r4, #7
	mov	r2, r2, lsr #3
	bl	FlashReadFacBbtData
	cmn	r0, #1
	beq	.L2263
	mov	r2, #0
	mov	r0, r4, lsr #4
	mov	r3, r2
	sub	r4, r4, #1
	mov	r1, #1
	b	.L2259
.L2261:
	ldr	ip, [r6, #116]
	mov	r8, r2, lsr #5
	and	r7, r2, #31
	ldr	ip, [ip, r8, asl #2]
	ands	ip, ip, r1, asl r7
	movne	ip, r3, asl #1
	addne	r3, r3, #1
	uxthne	r3, r3
	strneh	r2, [r5, ip]	@ movhi
	cmp	r3, r0
	bcs	.L2263
	add	r2, r2, #1
	uxth	r2, r2
.L2259:
	cmp	r2, r4
	blt	.L2261
	b	.L2258
.L2263:
	mov	r3, #0
.L2258:
	mov	r3, r3, asl #1
	mvn	r2, #0
	mov	r0, #0
	strh	r2, [r5, r3]	@ movhi
	ldmfd	sp!, {r4, r5, r6, r7, r8, pc}
.L2265:
	.align	2
.L2264:
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
	ldr	r7, .L2283
	bl	FtlLoadFactoryBbt
	ldr	r6, .L2283+4
	add	sl, r7, #12
	ldr	r4, .L2283+8
	b	.L2267
.L2273:
	ldrh	r3, [sl], #2
	movw	r2, #65535
	ldr	r0, [r4, #-2076]
	movw	fp, #3908
	ldr	r9, [r4, #-952]
	cmp	r3, r2
	str	r0, [r4, #176]
	str	r9, [r4, #180]
	beq	.L2268
	ldrh	r8, [r6, fp]
	mov	r1, #1
	mov	r2, r1
	ldr	r0, .L2283+12
	mla	r8, r8, r5, r3
	mov	r3, r8, asl #10
	str	r3, [r4, #172]
	bl	FlashReadPages
	ldrh	r2, [r6, fp]
	ldr	r0, [r7, #28]
	add	r2, r2, #7
	ldr	r1, [r4, #176]
	mov	r2, r2, lsr #3
	bl	memcpy
	b	.L2269
.L2268:
	mov	r1, r5
	bl	FlashGetBadBlockList
	ldr	r0, [r4, #176]
	ldr	r1, [r7, #28]
	bl	FtlBbt2Bitmap
	ldrh	fp, [r6, fp]
.L2271:
	sub	fp, fp, #1
	uxth	fp, fp
.L2282:
	movw	r8, #3908
	ldrh	r0, [r6, r8]
	mla	r0, r0, r5, fp
	uxth	r0, r0
	bl	FtlBbmIsBadBlock
	cmp	r0, #1
	beq	.L2271
	mov	r1, #0
	mov	r2, #16
	strh	fp, [sl, #-2]	@ movhi
	ldr	r0, [r4, #-952]
	bl	ftl_memset
	ldr	r3, .L2283+16
	strh	r3, [r9, #0]	@ movhi
	mov	r3, #0
	str	r3, [r9, #4]
	ldrh	r3, [sl, #-2]
	ldrh	r8, [r6, r8]
	strh	r3, [r9, #2]	@ movhi
	ldrh	r3, [sl, #-2]
	ldr	r1, [r7, #28]
	ldr	r0, [r4, #176]
	mla	r8, r8, r5, r3
	mov	r3, r8, asl #10
	str	r3, [r4, #172]
	ldr	r3, .L2283+20
	ldrh	r2, [r4, r3]
	mov	r2, r2, asl #2
	bl	memcpy
	mov	r1, #1
	mov	r2, r1
	ldr	r0, .L2283+12
	bl	FlashEraseBlocks
	mov	r1, #1
	mov	r3, r1
	ldr	r0, .L2283+12
	mov	r2, r1
	bl	FlashProgPages
	ldr	r3, [r4, #168]
	cmn	r3, #1
	bne	.L2269
	uxth	r0, r8
	bl	FtlBbmMapBadBlock
	b	.L2282
.L2269:
	uxth	r0, r8
	add	r5, r5, #1
	bl	FtlBbmMapBadBlock
	add	r7, r7, #4
.L2267:
	movw	r3, #3866
	ldrh	r3, [r6, r3]
	cmp	r5, r3
	bcc	.L2273
	mov	r4, #0
	ldr	r7, .L2283+4
	movw	r6, #3926
	b	.L2274
.L2275:
	mov	r0, r4
	add	r4, r4, #1
	bl	FtlBbmMapBadBlock
	uxth	r4, r4
.L2274:
	ldrh	r3, [r7, r6]
	ldr	r5, .L2283+4
	cmp	r3, r4
	bhi	.L2275
	movw	r7, #3992
	movw	r6, #3980
	ldrh	r4, [r5, r7]
	sub	r4, r4, #1
	uxth	r4, r4
	b	.L2276
.L2281:
	mov	r0, r4
	bl	FtlBbmIsBadBlock
	cmp	r0, #1
	beq	.L2277
	mov	r0, r4
	bl	FlashTestBlk
	cmp	r0, #0
	beq	.L2278
	mov	r0, r4
	bl	FtlBbmMapBadBlock
	b	.L2277
.L2278:
	ldrh	r2, [r5, r6]
	movw	r3, #65535
	cmp	r2, r3
	streqh	r4, [r5, r6]	@ movhi
.L2279:
	ldrne	r2, .L2283+4
	movne	r3, #3984
	strneh	r4, [r2, r3]	@ movhi
	bne	.L2280
.L2277:
	sub	r4, r4, #1
	uxth	r4, r4
.L2276:
	ldrh	r3, [r5, r7]
	sub	r3, r3, #48
	cmp	r4, r3
	bgt	.L2281
.L2280:
	ldr	r4, .L2283+4
	movw	r7, #3980
	ldr	r3, .L2283+8
	mov	r6, #3984
	mov	r5, #0
	movw	r8, #3982
	str	r5, [r4, #3988]
	mov	r1, #1
	ldr	r0, [r3, #-2088]
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
	ldr	r3, [r4, #3988]
	ldrh	r2, [r4, r6]
	add	r3, r3, #1
	str	r3, [r4, #3988]
	ldrh	r3, [r4, r7]
	strh	r5, [r4, r8]	@ movhi
	strh	r2, [r4, r7]	@ movhi
	strh	r3, [r4, r6]	@ movhi
	bl	FtlBbmTblFlush
	mov	r0, r5
	ldmfd	sp!, {r3, r4, r5, r6, r7, r8, r9, sl, fp, pc}
.L2284:
	.align	2
.L2283:
	.word	.LANCHOR0+3980
	.word	.LANCHOR0
	.word	.LANCHOR2
	.word	.LANCHOR2+168
	.word	-3872
	.word	-1032
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
	ldr	r3, .L2305
	str	r2, [r3, #-1848]
	str	r2, [r3, #-1844]
	ldr	r3, .L2305+4
	ldr	r0, [r3, #3848]
	uxth	r0, r0
	bl	FtlFreeSysBlkQueueInit
	bl	FtlLoadBbt
	cmp	r0, #0
	beq	.L2286
	bl	FtlMakeBbt
.L2286:
	mov	r3, #0
	ldr	ip, .L2305+4
	movw	r0, #3918
	ldr	r2, .L2305
	ldr	r1, .L2305+8
	b	.L2287
.L2288:
	ldr	lr, [r2, #-960]
	mvn	r4, r3
	orr	r4, r3, r4, asl #16
	str	r4, [lr, r3, asl #2]
	ldr	lr, [r2, #-956]
	str	r1, [lr, r3, asl #2]
	add	r3, r3, #1
	uxth	r3, r3
.L2287:
	ldrh	lr, [ip, r0]
	ldr	r8, .L2305+4
	cmp	r3, lr, asl #7
	blt	.L2288
	movw	r3, #3852
	mov	r5, #0
	ldrh	r6, [r8, r3]
	movw	r7, #3854
	b	.L2289
.L2290:
	mov	r0, r6
	mov	r1, #1
	bl	FtlLowFormatEraseBlock
	add	r6, r6, #1
	uxth	r6, r6
	add	r5, r5, r0
	uxth	r5, r5
.L2289:
	ldrh	r3, [r8, r7]
	ldr	r4, .L2305+4
	cmp	r3, r6
	bhi	.L2290
	movw	r3, #3844
	ldrh	r1, [r4, r3]
	sub	r3, r5, #3
	cmp	r3, r1, asl #1
	bge	.L2291
.L2295:
	mov	r4, #0
	ldr	r8, .L2305+4
	mov	r6, r4
	movw	r7, #3852
	b	.L2292
.L2291:
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
	b	.L2293
.L2294:
	mov	r0, r5
	mov	r1, #1
	bl	FtlLowFormatEraseBlock
	add	r5, r5, #1
	uxth	r5, r5
.L2293:
	ldrh	r3, [r4, r6]
	cmp	r3, r5
	bhi	.L2294
	b	.L2295
.L2296:
	mov	r0, r6
	mov	r1, #0
	bl	FtlLowFormatEraseBlock
	add	r6, r6, #1
	uxth	r6, r6
	add	r4, r4, r0
	uxth	r4, r4
.L2292:
	ldrh	r3, [r8, r7]
	ldr	r5, .L2305+4
	cmp	r3, r6
	bhi	.L2296
	movw	r3, #3854
	ldr	fp, [r5, #3856]
	ldrh	r3, [r5, r3]
	ldr	r9, .L2305
	mov	r0, fp
	ldr	sl, .L2305+12
	str	r3, [r5, #3976]
	movw	r3, #3844
	ldrh	r6, [r5, r3]
	mov	r1, r6
	bl	__aeabi_uidiv
	ubfx	r8, r0, #5, #16
	mov	r7, r0
	add	r3, r8, #36
	strh	r3, [r9, sl]	@ movhi
	mov	r3, #24
	str	r0, [r5, #3972]
	mul	r3, r3, r6
	cmp	r4, r3
	ble	.L2297
	rsb	r0, r4, fp
	mov	r1, r6
	bl	__aeabi_uidiv
	str	r0, [r5, #3972]
	mov	r0, r0, lsr #5
	add	r0, r0, #24
	strh	r0, [r9, sl]	@ movhi
.L2297:
	ldr	r5, .L2305
	ldr	r3, [r5, #-1884]
	cmp	r3, #1
	bne	.L2298
	mov	r0, r4
	mov	r1, r6
	bl	__aeabi_uidiv
	ldr	sl, .L2305+12
	ldrh	r9, [r5, sl]
	uxtah	r0, r9, r0
	add	r9, r9, r0, lsr #2
	strh	r9, [r5, sl]	@ movhi
.L2298:
	ldr	r0, .L2305+4
	mov	r3, #3904
	ldrh	r3, [r0, r3]
	cmp	r3, #0
	beq	.L2299
	ldr	r1, .L2305
	ldr	r2, .L2305+12
	ldrh	ip, [r1, r2]
	add	ip, ip, r3, lsr #1
	strh	ip, [r1, r2]	@ movhi
	mul	ip, r6, r3
	cmp	ip, r4
	strgt	r7, [r0, #3972]
	addgt	r3, r3, #32
	addgt	r8, r8, r3
	strgth	r8, [r1, r2]	@ movhi
.L2299:
	ldr	r4, .L2305
	ldr	r5, .L2305+4
	ldr	r3, .L2305+12
	ldr	r7, .L2305+16
	ldr	r2, [r5, #3972]
	ldrh	r3, [r4, r3]
	rsb	r3, r3, r2
	mul	r6, r6, r3
	movw	r3, #3912
	ldrh	r3, [r5, r3]
	str	r6, [r4, #204]
	mul	r6, r3, r6
	movw	r3, #3918
	ldrh	r3, [r5, r3]
	str	r6, [r5, #3972]
	mul	r6, r3, r6
	str	r6, [r5, #3952]
	mvn	r6, #0
	bl	FtlBbmTblFlush
	movw	r3, #3854
	ldrh	r2, [r5, r3]
	mov	r1, #0
	ldr	r0, [r4, #-2064]
	mov	r2, r2, asl #1
	bl	ftl_memset
	ldr	r1, .L2305+20
	mov	r3, #0
	ldr	r2, .L2305+24
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
	movw	r3, #3852
	strh	r6, [r4, r2]	@ movhi
	ldrh	r2, [r5, r3]
	ldr	r0, [r4, #-1888]
	mov	r2, r2, lsr #3
	bl	ftl_memset
.L2300:
	ldr	r0, .L2305+28
	bl	make_superblock
	ldrb	r3, [r4, #-2037]	@ zero_extendqisi2
	ldr	r5, .L2305
	cmp	r3, #0
	ldr	r3, .L2305+16
	bne	.L2301
	ldrh	r3, [r4, r7]
	ldr	r2, [r4, #-2064]
	mov	r3, r3, asl #1
	strh	r6, [r2, r3]	@ movhi
	ldrh	r3, [r4, r7]
	add	r3, r3, #1
	strh	r3, [r4, r7]	@ movhi
	b	.L2300
.L2301:
	ldr	r2, [r5, #-1848]
	mvn	r7, #0
	ldr	r1, [r5, #-2064]
	ldr	r6, .L2305+32
	str	r2, [r5, #-2032]
	add	r2, r2, #1
	str	r2, [r5, #-1848]
	ldr	r2, .L2305+36
	ldrh	r0, [r5, r2]
	ldrh	r2, [r5, r3]
	mov	r2, r2, asl #1
	strh	r0, [r1, r2]	@ movhi
	mov	r2, #0
	ldr	r0, .L2305+40
	ldrh	r3, [r5, r3]
	strb	r2, [r5, #-1990]
	add	r3, r3, #1
	strh	r2, [r5, r0]	@ movhi
	strh	r3, [r5, r6]	@ movhi
	mov	r3, #1
	strb	r3, [r5, #-1988]
.L2302:
	ldr	r0, .L2305+44
	bl	make_superblock
	ldrb	r3, [r5, #-1989]	@ zero_extendqisi2
	ldr	r4, .L2305
	cmp	r3, #0
	bne	.L2303
	ldrh	r3, [r5, r6]
	ldr	r2, [r5, #-2064]
	mov	r3, r3, asl #1
	strh	r7, [r2, r3]	@ movhi
	ldrh	r3, [r5, r6]
	add	r3, r3, #1
	strh	r3, [r5, r6]	@ movhi
	b	.L2302
.L2303:
	ldr	r3, [r4, #-1848]
	mvn	r5, #0
	ldr	r2, [r4, #-2064]
	str	r3, [r4, #-1984]
	add	r3, r3, #1
	str	r3, [r4, #-1848]
	ldr	r3, .L2305+48
	ldrh	r1, [r4, r3]
	sub	r3, r3, #4
	ldrh	r3, [r4, r3]
	mov	r3, r3, asl #1
	strh	r1, [r2, r3]	@ movhi
	ldr	r3, .L2305+52
	strh	r5, [r4, r3]	@ movhi
	bl	FtlFreeSysBlkQueueOut
	ldr	r3, .L2305+56
	mov	r2, #0
	strh	r0, [r4, r3]	@ movhi
	add	r3, r3, #2
	strh	r2, [r4, r3]	@ movhi
	add	r3, r3, #2
	ldr	r2, [r4, #204]
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
	mov	r0, #0
	ldreq	r3, .L2305+60
	moveq	r2, #1
	streq	r2, [r3, #2960]
	ldmfd	sp!, {r3, r4, r5, r6, r7, r8, r9, sl, fp, pc}
.L2306:
	.align	2
.L2305:
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
	.size	FtlLowFormat, .-FtlLowFormat
	.align	2
	.global	FtlReInitForSDUpdata
	.type	FtlReInitForSDUpdata, %function
FtlReInitForSDUpdata:
	.fnstart
	@ args = 0, pretend = 0, frame = 16
	@ frame_needed = 0, uses_anonymous_args = 0
	stmfd	sp!, {r0, r1, r2, r3, r4, lr}
	.save {r0, r1, r2, r3, r4, lr}
	ldr	r4, .L2331
	ldr	r0, [r4, #112]
	bl	FlashInit
	cmp	r0, #0
	movne	r0, #0
	bne	.L2308
	bl	FlashLoadFactorBbt
	cmp	r0, #0
	beq	.L2309
	bl	FlashMakeFactorBbt
.L2309:
	ldr	r0, [r4, #116]
	bl	FlashReadIdbDataRaw
	cmp	r0, #0
	beq	.L2310
	mov	r1, #0
	mov	r2, #16
	mov	r0, sp
	bl	FlashReadFacBbtData
	ldr	r1, [sp, #0]
	mov	r3, #0
	mov	r0, #1
	mov	r2, r3
.L2312:
	ands	ip, r1, r0, asl r2
	add	r2, r2, #1
	addne	r3, r3, #1
	cmp	r2, #16
	bne	.L2312
	cmp	r3, #6
	ldrls	r3, .L2331+4
	bls	.L2327
	mov	r2, #0
	mov	r0, #1
.L2313:
	ands	ip, r1, r0, asl r2
	add	r2, r2, #1
	addne	r3, r3, #1
	cmp	r2, #24
	bne	.L2313
	cmp	r3, #17
	ldr	r3, .L2331+4
	movhi	r2, #36
.L2327:
	strb	r2, [r3, #1]
	movw	r2, #2226
	ldr	r3, .L2331+4
	ldrb	r1, [r3, #1]	@ zero_extendqisi2
	strh	r1, [r3, r2]	@ movhi
.L2310:
	ldr	r1, .L2331+8
	mov	r4, #1
	ldr	r0, .L2331+12
	bl	printk
	ldr	r0, .L2331+16
	bl	FtlConstantsInit
	bl	FtlVariablesInit
	ldr	r3, .L2331+4
	ldr	r0, [r3, #3848]
	uxth	r0, r0
	bl	FtlFreeSysBlkQueueInit
	b	.L2317
.L2319:
	add	r4, r4, #1
.L2317:
	bl	FtlLoadBbt
	cmp	r0, #0
	bne	.L2329
.L2318:
	bl	FtlSysBlkInit
	cmp	r0, #0
	beq	.L2320
.L2329:
	bl	FtlLowFormat
	cmp	r4, #3
	bls	.L2319
	b	.L2330
.L2320:
	ldr	r3, .L2331+20
	mov	r2, #1
	str	r2, [r3, #2960]
	b	.L2308
.L2330:
	mvn	r0, #0
.L2308:
	add	sp, sp, #16
	ldmfd	sp!, {r4, pc}
.L2332:
	.align	2
.L2331:
	.word	.LANCHOR2
	.word	.LANCHOR0
	.word	.LC75
	.word	.LC74
	.word	.LANCHOR0+2200
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
	ldr	r3, .L2335
	ldr	r2, [r3, #3788]
	ldr	r3, .L2335+4
	cmp	r2, r3
	bxne	lr
	b	flash_enter_slc_mode
.L2336:
	.align	2
.L2335:
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
	ldr	r3, .L2339
	ldr	r2, [r3, #3788]
	ldr	r3, .L2339+4
	cmp	r2, r3
	bxne	lr
	b	flash_exit_slc_mode
.L2340:
	.align	2
.L2339:
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
	ldr	r4, .L2346
	mov	r6, r0
	str	r2, [sp, #20]
	mov	sl, r1
	ldr	r0, .L2346+4
	mov	r1, r6
	ldr	r2, [r4, #2776]
	ldr	r3, [r4, #4]
	ldr	r8, .L2346+8
	ldrb	r5, [r2, #9]	@ zero_extendqisi2
	mov	r2, sl
	mul	r5, r5, r3
	bl	printk
	ldr	r3, [r4, #2776]
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
	b	.L2342
.L2344:
	add	r1, r5, r7
	rsb	r3, r2, #4
	ubfx	r1, r1, #2, #16
	ldrb	r0, [r4, #80]	@ zero_extendqisi2
	uxth	r3, r3
	str	r3, [sp, #16]
	add	r3, r4, r1, asl #1
	cmp	r0, #0
	add	r3, r3, #2224
	add	r3, r3, #4
	ldrh	r3, [r3, #0]
	beq	.L2343
	ldr	r0, [r4, #3788]
	ldr	ip, .L2346+12
	cmp	r0, ip
	moveq	r3, r1
.L2343:
	ldr	ip, [sp, #12]
	ldrb	r0, [r8, #128]	@ zero_extendqisi2
	add	r2, r2, ip
	ldrb	ip, [r4, #3836]	@ zero_extendqisi2
	ldr	fp, [r8, #116]
	mla	r2, r9, r3, r2
	ldr	r3, [r4, #2776]
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
	ldr	r1, [r8, #116]
	add	r0, ip, r5, asl #9
	bl	memcpy
	ldr	ip, [sp, #16]
	mov	r2, #0
	add	r5, ip, r5
	uxth	r5, r5
.L2342:
	cmp	r5, sl
	bcc	.L2344
	mov	r1, r6
	mov	r2, sl
	mov	r3, #0
	ldr	r0, .L2346+16
	bl	printk
	mov	r0, #0
	add	sp, sp, #28
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, sl, fp, pc}
.L2347:
	.align	2
.L2346:
	.word	.LANCHOR0
	.word	.LC122
	.word	.LANCHOR2
	.word	1446522928
	.word	.LC123
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
	ldr	r4, .L2355
	mov	r5, r0
	str	r2, [sp, #20]
	mov	r8, r1
	ldr	r0, .L2355+4
	mov	r1, r5
	ldr	r2, [r4, #2776]
	mov	r7, r5
	ldr	r3, [r4, #4]
	ldr	fp, .L2355+8
	ldrb	r6, [r2, #9]	@ zero_extendqisi2
	mov	r2, r8
	mul	r6, r6, r3
	bl	printk
	mov	r0, #0
	bl	flash_boot_enter_slc_mode
	movw	r3, #2220
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
	ldr	r3, [r4, #2776]
	mov	r1, r6
	mov	r0, r5
	mov	r6, #0
	ldrb	sl, [r3, #9]	@ zero_extendqisi2
	bl	__aeabi_uidivmod
	mov	r9, r1
	rsb	r1, r1, r5
	str	r1, [sp, #16]
	b	.L2349
.L2353:
	add	r3, r6, r9
	ubfx	r3, r3, #2, #16
	cmp	r3, #0
	beq	.L2350
	add	r1, r3, #1
	ldrb	r0, [r4, #80]	@ zero_extendqisi2
	add	r2, r4, r1, asl #1
	cmp	r0, #0
	add	r2, r2, #2224
	add	r2, r2, #4
	ldrh	r2, [r2, #0]
	beq	.L2351
	ldr	r0, [r4, #3788]
	cmp	r0, fp
	uxtheq	r2, r1
.L2351:
	sub	r2, r2, #1
	mov	r2, r2, asl #2
	str	r2, [sp, #24]
	mov	r2, #0
	str	r2, [sp, #28]
.L2350:
	add	r2, r4, r3, asl #1
	add	r2, r2, #2224
	ldrh	r5, [r2, #4]
	ldrb	r2, [r4, #80]	@ zero_extendqisi2
	cmp	r2, #0
	beq	.L2352
	ldr	r2, [r4, #3788]
	cmp	r2, fp
	moveq	r5, r3
.L2352:
	ldr	r2, [sp, #16]
	ldr	r1, [sp, #20]
	ldrb	ip, [r4, #3836]	@ zero_extendqisi2
	mla	r3, sl, r5, r2
	add	r2, r1, r6, asl #9
	ldr	r1, .L2355+12
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
	ldr	r1, [r4, #2776]
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
.L2349:
	cmp	r6, r8
	bcc	.L2353
	mov	r1, r7
	mov	r2, r8
	mov	r3, #0
	ldr	r0, .L2355+16
	bl	printk
	mov	r5, r7
	mov	r0, #0
	add	sp, sp, #92
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, sl, fp, pc}
.L2356:
	.align	2
.L2355:
	.word	.LANCHOR0
	.word	.LC124
	.word	1446522928
	.word	.LANCHOR2
	.word	.LC125
	.fnend
	.size	IDBlockWriteData, .-IDBlockWriteData
	.align	2
	.global	write_idblock
	.type	write_idblock, %function
write_idblock:
	.fnstart
	@ args = 0, pretend = 0, frame = 8
	@ frame_needed = 0, uses_anonymous_args = 0
	ldr	r3, .L2370
	stmfd	sp!, {r4, r5, r6, r7, r8, r9, sl, fp, lr}
	.save {r4, r5, r6, r7, r8, r9, sl, fp, lr}
	mov	r7, r2
	ldr	r2, [r3, #4]
	.pad #20
	sub	sp, sp, #20
	ldr	r3, [r3, #2776]
	mov	r6, r0
	mov	r4, r1
	mov	r0, #256000
	mov	r1, #208
	ldrb	r3, [r3, #9]	@ zero_extendqisi2
	mul	r3, r3, r2
	mov	r2, #6
	uxth	r3, r3
	str	r3, [sp, #8]
	bl	kmalloc_order_trace
	subs	r5, r0, #0
	mvneq	r0, #0
	beq	.L2358
	add	sl, r6, #508
	add	sl, sl, #3
	mov	sl, sl, lsr #9
	cmp	sl, #255
	bhi	.L2359
	add	r0, r4, sl, asl #9
	mov	r1, r4
	rsb	r2, sl, #256
	bl	memcpy
.L2359:
	ldr	r8, .L2370
	mov	r3, #5
	ldr	r0, .L2370+4
	mov	r1, r7
	mov	r2, #4
	add	sl, sl, #128
	bl	rknand_print_hex
	ldrb	r2, [r8, #1]	@ zero_extendqisi2
	ldr	r1, [r4, #512]
	cmp	sl, #256
	movcs	sl, #256
	ldr	r0, .L2370+8
	mov	fp, #0
	bl	printk
	ldrb	r3, [r8, #1]	@ zero_extendqisi2
	ldr	r2, [r4, #512]
	mov	r1, sl
	ldr	r0, .L2370+12
	mov	r9, fp
	cmp	r2, r3
	mov	r8, r4
	mov	r2, r6
	strhi	r3, [r4, #512]
	mov	r6, r7
	bl	printk
	mov	r1, sl, asl #7
	str	r1, [sp, #12]
.L2365:
	ldr	r1, .L2370
	movw	r2, #2226
	ldr	r3, [r6], #4
	ldrh	r2, [r1, r2]
	cmp	r3, r2
	bcs	.L2361
	ldr	r2, .L2370+16
	ldr	r2, [r2, #132]
	cmp	r3, r2
	bcc	.L2361
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
.L2364:
	ldr	r1, [r5, r3]
	add	r3, r3, #4
	add	r2, r8, r3
	ldr	r2, [r2, #-4]
	cmp	r1, r2
	beq	.L2362
	bic	r4, r7, #255
	stmia	sp, {r1, r2}
	mov	r3, r7
	mov	r1, r9
	ldr	r2, [r6, #-4]
	mov	r4, r4, asl #2
	ldr	r0, .L2370+20
	bl	printk
	ldr	r0, .L2370+24
	add	r1, r8, r4
	mov	r2, #4
	mov	r3, #256
	bl	rknand_print_hex
	mov	r3, #256
	mov	r2, #4
	ldr	r0, .L2370+28
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
	ldr	r0, .L2370+32
	bl	printk
	ldr	r2, [sp, #12]
	cmp	r7, r2
	bcc	.L2361
	b	.L2363
.L2362:
	ldr	r1, [sp, #12]
	add	r7, r7, #1
	cmp	r7, r1
	bne	.L2364
.L2363:
	add	fp, fp, #1
.L2361:
	add	r9, r9, #1
	cmp	r9, #5
	bne	.L2365
	mov	r0, r5
	bl	kfree
	cmp	fp, #0
	mvneq	r0, #0
	movne	r0, #0
.L2358:
	add	sp, sp, #20
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, sl, fp, pc}
.L2371:
	.align	2
.L2370:
	.word	.LANCHOR0
	.word	.LC126
	.word	.LC127
	.word	.LC128
	.word	.LANCHOR2
	.word	.LC129
	.word	.LC130
	.word	.LC131
	.word	.LC132
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
	ldr	ip, .L2375
	b	.L2373
.L2374:
	ldrb	r4, [r3, r2]	@ zero_extendqisi2
	add	r2, r2, #1
	eor	r4, r4, r0, lsr #24
	add	r4, ip, r4, asl #2
	ldr	r4, [r4, #3008]
	eor	r0, r4, r0, asl #8
.L2373:
	cmp	r2, r1
	bne	.L2374
	ldmfd	sp!, {r4, pc}
.L2376:
	.align	2
.L2375:
	.word	.LANCHOR1
	.fnend
	.size	CRC_32, .-CRC_32
	.align	2
	.type	rknand_sys_storage_ioctl, %function
rknand_sys_storage_ioctl:
	.fnstart
	@ args = 0, pretend = 0, frame = 528
	@ frame_needed = 0, uses_anonymous_args = 0
	ldr	r3, .L2493
	stmfd	sp!, {r4, r5, r6, lr}
	.save {r4, r5, r6, lr}
	cmp	r1, r3
	.pad #528
	sub	sp, sp, #528
	mov	r5, r1
	mov	r4, r2
	beq	.L2386
	bhi	.L2395
	ldr	r3, .L2493+4
	cmp	r1, r3
	beq	.L2383
	bhi	.L2396
	sub	r3, r3, #125
	cmp	r1, r3
	beq	.L2380
	bhi	.L2397
	sub	r3, r3, #237
	cmp	r1, r3
	bne	.L2438
	b	.L2489
.L2397:
	ldr	r3, .L2493+8
	cmp	r1, r3
	beq	.L2381
	add	r3, r3, #1
	cmp	r1, r3
	bne	.L2438
	b	.L2490
.L2396:
	ldr	r3, .L2493+12
	cmp	r1, r3
	beq	.L2385
	bhi	.L2398
	ldr	r3, .L2493+16
	cmp	r1, r3
	bne	.L2438
	b	.L2491
.L2398:
	ldr	r3, .L2493+20
	cmp	r1, r3
	beq	.L2385
	add	r3, r3, #10
	cmp	r1, r3
	bne	.L2438
	b	.L2385
.L2395:
	ldr	r3, .L2493+24
	cmp	r1, r3
	beq	.L2391
	bhi	.L2399
	sub	r3, r3, #78
	cmp	r1, r3
	beq	.L2388
	bcc	.L2387
	add	r3, r3, #21
	cmp	r1, r3
	beq	.L2389
	add	r3, r3, #56
	cmp	r1, r3
	bne	.L2438
	b	.L2492
.L2399:
	ldr	r3, .L2493+28
	cmp	r1, r3
	beq	.L2394
	bhi	.L2400
	sub	r3, r3, #956
	sub	r3, r3, #1
	cmp	r1, r3
	beq	.L2392
	add	r3, r3, #956
	cmp	r1, r3
	bne	.L2438
	b	.L2393
.L2400:
	ldr	r3, .L2493+32
	cmp	r1, r3
	beq	.L2393
	add	r3, r3, #1
	cmp	r1, r3
	bne	.L2438
	b	.L2394
.L2387:
	ldr	r0, .L2493+36
	bl	printk
	ldr	r3, .L2493+40
	mov	r1, #208
	mov	r2, #4096
	ldr	r0, [r3, #48]
	bl	kmem_cache_alloc_trace
	subs	r5, r0, #0
	beq	.L2446
	mov	r1, r4
	mov	r2, #512
	bl	rk_copy_from_user
	cmp	r0, #0
	bne	.L2488
.L2401:
	ldr	r0, .L2493+44
	ldmia	r5, {r1, r2}
	bl	printk
	ldr	r3, [r5, #4]
	cmp	r3, #8
	str	r3, [sp, #4]
	bhi	.L2409
	bl	rknand_device_unlock
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
	beq	.L2474
	ldr	r0, .L2493+48
.L2487:
	bl	printk
	b	.L2409
.L2388:
	ldr	r0, .L2493+52
	bl	printk
	ldr	r3, .L2493+40
	mov	r1, #208
	mov	r2, #4096
	ldr	r0, [r3, #48]
	bl	kmem_cache_alloc_trace
	subs	r5, r0, #0
	beq	.L2446
	mov	r1, r4
	mov	r2, #4096
	bl	rk_copy_from_user
	cmp	r0, #0
	bne	.L2488
.L2405:
	ldr	r4, .L2493+56
	ldr	r0, .L2493+60
	ldmia	r5, {r1, r2}
	bl	printk
	ldr	r3, [r4, #436]
	cmp	r3, #0
	bne	.L2406
	mov	r0, #260096
	mov	r1, #208
	mov	r2, #6
	bl	kmalloc_order_trace
	cmp	r0, #0
	str	r0, [r4, #436]
	beq	.L2409
.L2406:
	ldr	r2, [r5, #4]
	movw	r3, #4088
	cmp	r2, r3
	bhi	.L2409
	ldr	r3, [r5, #0]
	cmp	r3, #251904
	bhi	.L2409
	ldr	r1, .L2493+56
	ldr	r0, [r1, #436]
	add	r1, r5, #8
	add	r0, r0, r3
	bl	memcpy
.L2474:
	mov	r0, r5
	bl	kfree
.L2475:
	mov	r4, #0
	b	.L2404
.L2492:
	ldr	r0, .L2493+64
	bl	printk
	ldr	r3, .L2493+40
	mov	r1, #208
	mov	r2, #4096
	ldr	r0, [r3, #48]
	bl	kmem_cache_alloc_trace
	subs	r5, r0, #0
	beq	.L2446
	mov	r1, r4
	mov	r2, #28
	bl	rk_copy_from_user
	cmp	r0, #0
	beq	.L2408
.L2488:
	ldr	r0, .L2493+68
	b	.L2487
.L2408:
	ldmia	r5, {r1, r2}
	ldr	r0, .L2493+72
	bl	printk
	ldr	r1, [r5, #0]
	cmp	r1, #256000
	bhi	.L2409
	ldr	r4, .L2493+56
	ldr	r0, [r4, #436]
	cmp	r0, #0
	beq	.L2409
.L2410:
	bl	CRC_32
	ldr	r3, [r5, #4]
	cmp	r3, r0
	beq	.L2411
	mov	r0, r5
	bl	kfree
	b	.L2452
.L2411:
	bl	rknand_device_unlock
	ldr	r1, [r4, #436]
	add	r2, r5, #8
	ldr	r0, [r5, #0]
	bl	write_idblock
	mov	r6, #0
	bl	rknand_device_unlock
	ldr	r0, [r4, #436]
	bl	kfree
	str	r6, [r4, #436]
	mov	r0, r5
	mov	r4, r6
	bl	kfree
	b	.L2404
.L2409:
	mov	r0, r5
	b	.L2478
.L2389:
	ldr	r0, .L2493+76
	bl	printk
	ldr	r3, .L2493+40
	mov	r1, #208
	mov	r2, #4096
	ldr	r0, [r3, #48]
	bl	kmem_cache_alloc_trace
	subs	r5, r0, #0
	beq	.L2446
	bl	ReadFlashInfo
	mov	r0, r4
	mov	r1, r5
	mov	r2, #11
	b	.L2485
.L2386:
	ldr	r0, .L2493+80
	bl	printk
	bl	rknand_device_unlock
	bl	FtlReInitForSDUpdata
	mov	r6, r0
	bl	rknand_device_unlock
	cmp	r6, #0
	bne	.L2469
	bl	nand_blk_add_whole_disk
	ldr	r3, .L2493+40
	mov	r1, #208
	mov	r2, #4096
	ldr	r0, [r3, #48]
	bl	kmem_cache_alloc_trace
	subs	r5, r0, #0
	beq	.L2446
	bl	rknand_device_unlock
	mov	r1, r6
	mov	r2, #64
	mov	r0, r5
	bl	FlashReadFacBbtData
	bl	rknand_device_unlock
	ldr	r0, .L2493+84
	mov	r1, r5
	mov	r2, #4
	mov	r3, #8
	bl	rknand_print_hex
	mov	r0, r4
	mov	r1, r5
	mov	r2, #64
	b	.L2485
.L2391:
	ldr	r0, .L2493+88
	bl	printk
	ldr	r3, .L2493+56
	add	r1, sp, #528
	mov	r0, r4
	mov	r2, #4
	ldr	r3, [r3, #440]
	ldr	r3, [r3, #20]
	str	r3, [r1, #-524]!
	b	.L2482
.L2392:
	ldr	r0, .L2493+92
	bl	printk
	ldr	r3, .L2493+40
	mov	r1, #208
	mov	r2, #4096
	ldr	r0, [r3, #48]
	bl	kmem_cache_alloc_trace
	subs	r5, r0, #0
	beq	.L2446
	bl	rknand_device_unlock
	mov	r1, #264
	mov	r2, #2
	mov	r3, r5
	mov	r0, #16
	bl	FtlRead
	bl	rknand_device_unlock
	mov	r0, r4
	mov	r1, r5
	mov	r2, #1024
.L2485:
	bl	rk_copy_to_user
	subs	r4, r0, #0
	mov	r0, r5
	beq	.L2414
.L2478:
	bl	kfree
	b	.L2469
.L2414:
	bl	kfree
	b	.L2404
.L2380:
	ldr	r0, .L2493+96
	bl	printk
	add	r0, sp, #8
	mov	r1, r4
	mov	r2, #520
	bl	rk_copy_from_user
	subs	r5, r0, #0
	bne	.L2479
.L2415:
	ldr	r2, [sp, #8]
	ldr	r3, .L2493+100
	cmp	r2, r3
	bne	.L2471
	ldr	r3, [sp, #12]
	cmp	r3, #512
	bhi	.L2471
	ldr	r6, .L2493+56
	mov	r2, #512
	add	r0, sp, #8
	ldr	r1, [r6, #440]
	bl	memcpy
	ldr	r2, [r6, #444]
	ldr	r3, .L2493+104
	cmp	r2, r3
	beq	.L2416
	add	r0, sp, #72
	mov	r1, #128
	str	r5, [sp, #16]
	str	r5, [sp, #20]
	bl	__memzero
.L2416:
	add	r0, sp, #264
	mov	r1, #256
	mov	r3, #0
	str	r3, [sp, #24]
	bl	__memzero
	b	.L2481
.L2381:
	ldr	r0, .L2493+108
	bl	printk
	add	r0, sp, #8
	mov	r1, r4
	mov	r2, #520
	bl	rk_copy_from_user
	cmp	r0, #0
	bne	.L2479
.L2417:
	ldr	r2, [sp, #8]
	ldr	r3, .L2493+100
	cmp	r2, r3
	bne	.L2471
	ldr	r3, [sp, #12]
	cmp	r3, #512
	bhi	.L2471
	ldr	r4, .L2493+56
	ldr	r3, .L2493+104
	ldr	r2, [r4, #444]
	cmp	r2, r3
	bne	.L2452
	ldr	r3, [sp, #20]
	sub	r2, r3, #1
	cmp	r2, #127
	mvnhi	r4, #2
	bhi	.L2378
	ldr	r0, [r4, #440]
	add	r1, sp, #72
	str	r3, [r0, #12]
	add	r0, r0, #64
	ldr	r2, [sp, #20]
	bl	memcpy
	mov	r0, #1
	ldr	r1, [r4, #440]
	b	.L2476
.L2491:
	ldr	r0, .L2493+112
	bl	printk
	add	r0, sp, #8
	mov	r1, r4
	mov	r2, #520
	bl	rk_copy_from_user
	cmp	r0, #0
	bne	.L2479
.L2418:
	ldr	r2, [sp, #8]
	ldr	r3, .L2493+116
	cmp	r2, r3
	bne	.L2471
	ldr	r3, [sp, #12]
	cmp	r3, #512
	bhi	.L2471
	ldr	r5, .L2493+56
	ldr	r4, [r5, #448]
	cmp	r4, #0
	beq	.L2378
	ldr	r3, [r5, #452]
	ldr	r2, .L2493+120
	ldr	r1, [r3, #0]
	cmp	r1, r2
	beq	.L2419
	str	r2, [r3, #0]
	mov	r2, #504
	ldr	r3, [r5, #452]
	str	r2, [r3, #4]
	mov	r2, #0
	str	r2, [r3, #8]
	str	r2, [r3, #12]
.L2419:
	ldr	r1, [r5, #452]
	mov	r4, #0
	mov	r0, r4
	str	r4, [r1, #16]
	bl	StorageSysDataStore
	ldr	r3, [r5, #440]
	ldr	r2, .L2493+100
	ldr	r5, .L2493+56
	ldr	r1, [r3, #0]
	cmp	r1, r2
	strne	r2, [r3, #0]
	ldr	r0, [r5, #440]
	mov	r1, #128
	ldrne	r3, .L2493+56
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
	b	.L2404
.L2383:
	ldr	r0, .L2493+124
	bl	printk
	mov	r1, r4
	add	r0, sp, #8
	mov	r2, #520
	bl	rk_copy_from_user
	subs	r4, r0, #0
	bne	.L2479
.L2421:
	ldr	r2, [sp, #8]
	ldr	r3, .L2493+128
	cmp	r2, r3
	bne	.L2471
	ldr	r3, [sp, #12]
	cmp	r3, #512
	bhi	.L2471
	ldr	r5, .L2493+56
	ldr	r3, [r5, #448]
	cmp	r3, #1
	beq	.L2378
	ldr	r3, [r5, #452]
	mov	r0, #0
	ldr	r2, .L2493+120
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
	ldr	r2, .L2493+100
	ldr	r1, [r3, #0]
	cmp	r1, r2
	beq	.L2423
	str	r2, [r3, #0]
	mov	r1, #504
	ldr	r3, .L2493+56
	mov	r2, #0
	ldr	r3, [r3, #440]
	stmib	r3, {r1, r2}
.L2423:
	ldr	r5, .L2493+56
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
	b	.L2404
.L2490:
	ldr	r0, .L2493+132
	bl	printk
	add	r0, sp, #8
	mov	r1, r4
	mov	r2, #520
	bl	rk_copy_from_user
	cmp	r0, #0
	bne	.L2479
.L2424:
	ldr	r2, [sp, #8]
	ldr	r3, .L2493+136
	cmp	r2, r3
	bne	.L2471
	ldr	r2, [sp, #12]
	cmp	r2, #512
	addls	r0, sp, #16
	ldrls	r1, .L2493+140
	bls	.L2480
	b	.L2471
.L2385:
	ldr	r3, .L2493+20
	cmp	r5, r3
	ldreq	r0, .L2493+144
	beq	.L2472
	ldr	r3, .L2493+148
	cmp	r5, r3
	ldreq	r0, .L2493+152
	ldrne	r0, .L2493+156
.L2472:
	bl	printk
	add	r0, sp, #8
	mov	r1, r4
	mov	r2, #520
	bl	rk_copy_from_user
	cmp	r0, #0
	bne	.L2479
.L2428:
	ldr	r2, [sp, #8]
	ldr	r3, .L2493+160
	cmp	r2, r3
	bne	.L2469
	ldr	r3, .L2493+148
	ldr	r6, .L2493+56
	cmp	r5, r3
	bne	.L2429
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
	b	.L2378
.L2429:
	ldr	r3, [r6, #968]
	cmp	r3, #10
	bhi	.L2469
	ldr	r3, [r6, #440]
	ldr	r1, [sp, #12]
	ldr	r2, [r3, #24]
	cmp	r2, r1
	beq	.L2430
	cmp	r2, #0
	beq	.L2430
	ldr	r0, .L2493+164
	bl	printk
	ldr	r3, [r6, #968]
	add	r3, r3, #1
	str	r3, [r6, #968]
	b	.L2469
.L2430:
	ldr	r0, .L2493+56
	mov	r2, #0
	str	r2, [r0, #968]
	ldr	r0, .L2493+20
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
	b	.L2404
.L2393:
	ldr	r0, .L2493+168
	bl	printk
	add	r0, sp, #8
	mov	r1, r4
	mov	r2, #520
	bl	rk_copy_from_user
	cmp	r0, #0
	bne	.L2479
.L2433:
	ldr	r2, [sp, #8]
	ldr	r3, .L2493+172
	cmp	r2, r3
	bne	.L2471
	ldr	r2, [sp, #12]
	cmp	r2, #504
	bhi	.L2471
	ldr	r3, .L2493+176
	add	r0, sp, #16
	cmp	r5, r3
	ldr	r3, .L2493+56
	ldreq	r1, [r3, #972]
	ldrne	r1, [r3, #976]
	add	r1, r1, #8
.L2480:
	bl	memcpy
.L2481:
	add	r1, sp, #8
	mov	r0, r4
	mov	r2, #520
.L2482:
	bl	rk_copy_to_user
	subs	r4, r0, #0
	bne	.L2469
	b	.L2404
.L2394:
	ldr	r0, .L2493+180
	bl	printk
	add	r0, sp, #8
	mov	r1, r4
	mov	r2, #520
	bl	rk_copy_from_user
	cmp	r0, #0
	beq	.L2436
.L2479:
	ldr	r0, .L2493+68
	bl	printk
	b	.L2469
.L2436:
	ldr	r2, [sp, #8]
	ldr	r3, .L2493+172
	cmp	r2, r3
	bne	.L2471
	ldr	r2, [sp, #12]
	cmp	r2, #504
	bhi	.L2471
	ldr	r3, .L2493+28
	add	r2, r2, #8
	ldr	r4, .L2493+56
	cmp	r5, r3
	bne	.L2437
	add	r1, sp, #8
	ldr	r0, [r4, #972]
	bl	memcpy
	ldr	r1, [r4, #972]
	mov	r0, #2
.L2476:
	bl	StorageSysDataStore
	mov	r4, r0
	b	.L2404
.L2437:
	add	r1, sp, #8
	ldr	r0, [r4, #976]
	bl	memcpy
	mov	r0, #3
	ldr	r1, [r4, #976]
	b	.L2476
.L2489:
	bl	rknand_dev_flush
	b	.L2475
.L2471:
	mvn	r4, #0
.L2404:
	mov	r1, r4
	ldr	r0, .L2493+184
	bl	printk
	b	.L2378
.L2438:
	mvn	r4, #21
	b	.L2378
.L2446:
	mvn	r4, #11
	b	.L2378
.L2452:
	mvn	r4, #1
	b	.L2378
.L2469:
	mvn	r4, #13
.L2378:
	mov	r0, r4
	add	sp, sp, #528
	ldmfd	sp!, {r4, r5, r6, pc}
.L2494:
	.align	2
.L2493:
	.word	1074033155
	.word	1074029694
	.word	1074029570
	.word	1074031656
	.word	1074029695
	.word	1074031666
	.word	1074033235
	.word	1074034193
	.word	1074034194
	.word	.LC133
	.word	kmalloc_caches
	.word	.LC135
	.word	.LC136
	.word	.LC137
	.word	.LANCHOR2
	.word	.LC138
	.word	.LC139
	.word	.LC134
	.word	.LC140
	.word	.LC141
	.word	.LC142
	.word	.LC143
	.word	.LC144
	.word	.LC145
	.word	.LC146
	.word	1263358532
	.word	-1067903959
	.word	.LC147
	.word	.LC148
	.word	1112753220
	.word	1146313043
	.word	.LC149
	.word	1112755781
	.word	.LC150
	.word	1094995539
	.word	.LANCHOR2+456
	.word	.LC151
	.word	1074031676
	.word	.LC152
	.word	.LC153
	.word	1280262987
	.word	.LC154
	.word	.LC155
	.word	1145980246
	.word	1074034192
	.word	.LC156
	.word	.LC157
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
	ldr	r4, .L2498
	mov	r6, #0
	add	r0, r4, #456
	ldr	r5, [r4, #416]
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
	beq	.L2496
	mov	r0, r5
	mov	r1, #508
	bl	JSHash
	cmp	r7, r0
	beq	.L2496
	str	r6, [r5, #16]
	ldr	r0, .L2498+4
	str	r6, [r4, #448]
	bl	printk
.L2496:
	ldr	r3, [r4, #448]
	mov	r0, #2
	ldr	r4, .L2498
	cmp	r3, #0
	ldrne	r2, .L2498+8
	ldrne	r3, .L2498
	ldr	r1, [r4, #972]
	strne	r2, [r3, #444]
	bl	StorageSysDataLoad
	ldr	r1, [r4, #976]
	mov	r0, #3
	bl	StorageSysDataLoad
	ldr	r0, .L2498+12
	ldmfd	sp!, {r3, r4, r5, r6, r7, lr}
	b	misc_register
.L2499:
	.align	2
.L2498:
	.word	.LANCHOR2
	.word	.LC158
	.word	-1067903959
	.word	.LANCHOR1+4032
	.fnend
	.size	rk_ftl_storage_sys_init, .-rk_ftl_storage_sys_init
	.align	2
	.global	StorageSysDataDeInit
	.type	StorageSysDataDeInit, %function
StorageSysDataDeInit:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	stmfd	sp!, {r3, lr}
	.save {r3, lr}
	ldr	r0, .L2501
	bl	misc_deregister
	mov	r0, #0
	ldmfd	sp!, {r3, pc}
.L2502:
	.align	2
.L2501:
	.word	.LANCHOR1+4032
	.fnend
	.size	StorageSysDataDeInit, .-StorageSysDataDeInit
	.global	rknand_sys_storage_fops
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
	.type	__func__.14255, %object
	.size	__func__.14255, 11
__func__.14255:
	.ascii	"FtlMemInit\000"
.LC0:
	.byte	60
	.byte	40
	.byte	24
	.byte	16
	.type	__func__.15131, %object
	.size	__func__.15131, 21
__func__.15131:
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
	.ascii	"FTL version: 5.0.36 20170512\000"
.LC76:
	.ascii	"GetSwlReplaceBlock min_ec_id =%x %x\012\000"
.LC77:
	.ascii	"swblk %x ,avg = %x max= %x vpc= %x,ec=%x ,max ec=%x"
	.ascii	"\012\000"
.LC78:
	.ascii	"FtlGcRefreshBlock  0x%x \012\000"
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
	.ascii	"ECC:%d\012\000"
.LC88:
	.ascii	"sdr read ok %x ecc=%d\012\000"
.LC89:
	.ascii	"sync para %d\012\000"
.LC90:
	.ascii	"TOG mode Read error %x %x\012\000"
.LC91:
	.ascii	"FlashLoadPhyInfo fail %x!!\012\000"
.LC92:
	.ascii	"read retry status %x %x %x\012\000"
.LC93:
	.ascii	"Read pageadd=%x  ecc=%x err=%x\012\000"
.LC94:
	.ascii	"data:\000"
.LC95:
	.ascii	"spare:\000"
.LC96:
	.ascii	"ReadRetry pageadd=%x  ecc=%x err=%x\012\000"
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
	.ascii	"FtlBbmTblFlush id=%x,page=%x,previd=%x cnt=%d\012\000"
.LC104:
	.ascii	"FtlBbmTblFlush error:%x\012\000"
.LC105:
	.ascii	"FtlGcFreeBadSuperBlk 0x%x\012\000"
.LC106:
	.ascii	"decrement_vpc_count %x = %d\012\000"
.LC107:
	.ascii	"FlashMakeFactorBbt %d\012\000"
.LC108:
	.ascii	"bad block:%d %d\012\000"
.LC109:
	.ascii	"FMFB:%d %d\012\000"
.LC110:
	.ascii	"E:bad block:%d\012\000"
.LC111:
	.ascii	"FMFB:Save %d %d\012\000"
.LC112:
	.ascii	"spuer block %x vpn is 0\012 \000"
.LC113:
	.ascii	"...%s enter...\012\000"
.LC114:
	.ascii	"FtlCheckVpc %x = %x  %x\012\000"
.LC115:
	.ascii	"%d GC datablk  = %x vpc %x %x\012\000"
.LC116:
	.ascii	"SWL %x, FSB = %x vpc= %x,ec=%x th=%x\012\000"
.LC117:
	.ascii	"Ftlwrite decrement_vpc_count %x = %d\012\000"
.LC118:
	.ascii	"FtlInit %x\012\000"
.LC119:
	.ascii	"FtlWrite: lpa error:%x %x\012\000"
.LC120:
	.ascii	"FLFB:%d %d\012\000"
.LC121:
	.ascii	"BBT:\000"
.LC122:
	.ascii	"IdBlockReadData %x %x\012\000"
.LC123:
	.ascii	"IdBlockReadData %x %x ret= %x\012\000"
.LC124:
	.ascii	"IDBlockWriteData %x %x\012\000"
.LC125:
	.ascii	"IDBlockWriteData %x %x ret= %x\012\000"
.LC126:
	.ascii	"idblk:\000"
.LC127:
	.ascii	"idb reverse %x %x\012\000"
.LC128:
	.ascii	"write_idblock totle_sec %x %x\012\000"
.LC129:
	.ascii	"write and check error:%d idb=%x,offset=%x,r=%x,w=%x"
	.ascii	"\012\000"
.LC130:
	.ascii	"write\000"
.LC131:
	.ascii	"read\000"
.LC132:
	.ascii	"write_idblock error\012\000"
.LC133:
	.ascii	"READ_SECTOR_IO\012\000"
.LC134:
	.ascii	"rk_copy_from_user error\012\000"
.LC135:
	.ascii	"READ_SECTOR_IO %x %x\012\000"
.LC136:
	.ascii	"rk_copy_to_user error\012\000"
.LC137:
	.ascii	"WRITE_SECTOR_IO\012\000"
.LC138:
	.ascii	"WRITE_SECTOR_IO %x %x\012\000"
.LC139:
	.ascii	"END_WRITE_SECTOR_IO\012\000"
.LC140:
	.ascii	"END_WRITE_SECTOR_IO %x %x\012\000"
.LC141:
	.ascii	"GET_FLASH_INFO_IO\012\000"
.LC142:
	.ascii	"GET_BAD_BLOCK_IO\012\000"
.LC143:
	.ascii	"bbt:\000"
.LC144:
	.ascii	"GET_LOCK_FLAG_IO\012\000"
.LC145:
	.ascii	"GET_PUBLIC_KEY_IO\012\000"
.LC146:
	.ascii	"RKNAND_GET_DRM_KEY\012\000"
.LC147:
	.ascii	"RKNAND_STORE_DRM_KEY\012\000"
.LC148:
	.ascii	"RKNAND_DIASBLE_SECURE_BOOT\012\000"
.LC149:
	.ascii	"RKNAND_ENASBLE_SECURE_BOOT\012\000"
.LC150:
	.ascii	"RKNAND_GET_SN_SECTOR\012\000"
.LC151:
	.ascii	"RKNAND_LOADER_UNLOCK\012\000"
.LC152:
	.ascii	"RKNAND_LOADER_STATUS\012\000"
.LC153:
	.ascii	"RKNAND_LOADER_LOCK\012\000"
.LC154:
	.ascii	"LockKey not match %d\012\000"
.LC155:
	.ascii	"RKNAND_GET_VENDOR_SECTOR\012\000"
.LC156:
	.ascii	"RKNAND_STORE_VENDOR_SECTOR\012\000"
.LC157:
	.ascii	"return ret = %lx\012\000"
.LC158:
	.ascii	"secureBootEn check error\012\000"
.LC159:
	.ascii	"rknand_sys_storage\000"
	.data
	.align	2
	.set	.LANCHOR1,. + 0
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
	.byte	6
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
	.byte	-75
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
	.short	1088
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
	.type	gFtlInitStatus, %object
	.size	gFtlInitStatus, 4
gFtlInitStatus:
	.word	-1
	.type	micron_flash_randomizer_flag, %object
	.size	micron_flash_randomizer_flag, 4
micron_flash_randomizer_flag:
	.word	1
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
	.type	rknand_sys_storage_dev, %object
	.size	rknand_sys_storage_dev, 36
rknand_sys_storage_dev:
	.word	255
	.word	.LC159
	.word	rknand_sys_storage_fops
	.space	24
	.type	rknand_sys_storage_fops, %object
	.size	rknand_sys_storage_fops, 112
rknand_sys_storage_fops:
	.space	36
	.word	rknand_sys_storage_ioctl
	.word	rknand_sys_storage_ioctl
	.space	4
	.word	rknand_sys_storage_open
	.space	4
	.word	rknand_sys_storage_release
	.space	52
	.bss
	.align	2
	.set	.LANCHOR0,. + 0
	.set	.LANCHOR2,. + 8184
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
	.type	gNandcVer, %object
	.size	gNandcVer, 4
gNandcVer:
	.space	4
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
	.type	gReadRetryInfo, %object
	.size	gReadRetryInfo, 852
gReadRetryInfo:
	.space	852
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
	.type	gFlashPageBuffer0, %object
	.size	gFlashPageBuffer0, 4
gFlashPageBuffer0:
	.space	4
	.type	gBootDdrMode, %object
	.size	gBootDdrMode, 4
gBootDdrMode:
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
	.type	gMultiPageReadEn, %object
	.size	gMultiPageReadEn, 1
gMultiPageReadEn:
	.space	1
	.space	3
	.type	gpReadRetrial, %object
	.size	gpReadRetrial, 4
gpReadRetrial:
	.space	4
	.type	req_sys, %object
	.size	req_sys, 36
req_sys:
	.space	36
	.type	g_MaxLbn, %object
	.size	g_MaxLbn, 4
g_MaxLbn:
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
