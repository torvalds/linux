/*
 * Copyright (c) 2016, Fuzhou Rockchip Electronics Co., Ltd
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
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
	.type	rknand_sys_storage_release, %function
rknand_sys_storage_release:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	ldr	r0, [r1, #124]
	stmfd	sp!, {r3, lr}
	.save {r3, lr}
	cmp	r0, #0
	beq	.L2
	mov	r3, #0
	str	r3, [r1, #124]
	bl	kfree
.L2:
	mov	r0, #0
	ldmfd	sp!, {r3, pc}
	.fnend
	.size	rknand_sys_storage_release, .-rknand_sys_storage_release
	.align	2
	.type	rknand_sys_storage_open, %function
rknand_sys_storage_open:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	ldr	r3, .L6
	mov	r2, #4096
	stmfd	sp!, {r4, lr}
	.save {r4, lr}
	mov	r4, r1
	ldr	r0, [r3, #48]
	mov	r1, #208
	bl	kmem_cache_alloc_trace
	mov	r3, #0
	str	r3, [r4, #124]
	cmp	r0, #0
	bne	.L4
	ldr	r0, .L6+4
	bl	printk
	mvn	r0, #11
	ldmfd	sp!, {r4, pc}
.L4:
	str	r0, [r4, #124]
	mov	r0, r3
	ldmfd	sp!, {r4, pc}
.L7:
	.align	2
.L6:
	.word	kmalloc_caches
	.word	.LC1
	.fnend
	.size	rknand_sys_storage_open, .-rknand_sys_storage_open
	.align	2
	.global	FlashMemCmp8
	.type	FlashMemCmp8, %function
FlashMemCmp8:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	ldr	r3, .L18
	stmfd	sp!, {r4, lr}
	.save {r4, lr}
	ldrb	r3, [r3, #0]	@ zero_extendqisi2
	cmp	r3, #0
	beq	.L16
	ldrb	r3, [r1, #1]	@ zero_extendqisi2
	ldrb	ip, [r0, #1]	@ zero_extendqisi2
	cmp	ip, r3
	movne	r3, #0
	bne	.L16
	b	.L17
.L13:
	ldrb	r4, [r0, r3]	@ zero_extendqisi2
	ldrb	ip, [r1, r3]	@ zero_extendqisi2
	add	r3, r3, #1
	cmp	r4, ip
	beq	.L16
	mov	r0, r3
	ldmfd	sp!, {r4, pc}
.L16:
	cmp	r3, r2
	bne	.L13
	mov	r0, #0
	ldmfd	sp!, {r4, pc}
.L17:
	mov	r0, #0
	ldmfd	sp!, {r4, pc}
.L19:
	.align	2
.L18:
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
	ldr	r3, .L21
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
.L22:
	.align	2
.L21:
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
	beq	.L24
	bl	FlashRsvdBlkChk
	cmp	r0, #0
	orrne	r4, r4, #-1073741824
.L24:
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
	stmfd	sp!, {r3, r4, r5, r6, r7, lr}
	.save {r3, r4, r5, r6, r7, lr}
	mov	r6, r0
	ldr	r3, .L33
	mov	r5, r1
	ldr	r2, [r3, #12]
	mov	r7, r3
	cmp	r2, #5
	bls	.L28
	and	r5, r1, #127
	ldr	r3, .L33+4
	mov	r5, r5, asl #1
	ldrh	r4, [r3, r5]
	ldrb	r3, [r7, #8]	@ zero_extendqisi2
	cmp	r3, #0
	beq	.L29
	bl	FlashRsvdBlkChk
	cmp	r0, #0
	orrne	r4, r4, #-1073741824
.L29:
	add	r6, r7, r6, asl #3
	b	.L32
.L28:
	cmp	r2, #4
	ldmnefd	sp!, {r3, r4, r5, r6, r7, pc}
	and	r2, r1, #127
	ldr	ip, .L33+4
	ldrb	r3, [r3, #8]	@ zero_extendqisi2
	mov	r2, r2, asl #1
	cmp	r3, #0
	ldrh	r4, [ip, r2]
	mov	r4, r4, asl #8
	beq	.L31
	bl	FlashRsvdBlkChk
	cmp	r0, #0
	movne	r5, r5, asl #1
	uxtbne	r5, r5
	orrne	r5, r5, #1
	orrne	r4, r4, r5
.L31:
	ldr	r3, .L33
	add	r6, r3, r6, asl #3
.L32:
	ldr	r3, [r6, #16]
	str	r4, [r3, #336]
	ldmfd	sp!, {r3, r4, r5, r6, r7, pc}
.L34:
	.align	2
.L33:
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
	ldr	ip, .L36
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
.L37:
	.align	2
.L36:
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
	ldr	r2, .L42
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
	bne	.L39
	mov	ip, #6
	str	ip, [r3, #8]
	str	r2, [r3, #4]
	str	r2, [r3, #4]
	uxtb	r2, r1
	str	r2, [r3, #4]
	mov	r2, r1, lsr #8
	str	r2, [r3, #4]
	mov	r2, r1, lsr #16
	b	.L41
.L39:
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
.L41:
	str	r2, [r3, #4]
	mov	r2, #224
	str	r2, [r3, #8]
	ldmfd	sp!, {r4, lr}
	b	FlashSetRandomizer
.L43:
	.align	2
.L42:
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
	ldr	r3, .L46
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
.L47:
	.align	2
.L46:
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
	ldr	r3, .L50
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
.L51:
	.align	2
.L50:
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
	ldr	ip, .L53
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
.L54:
	.align	2
.L53:
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
	ldr	r3, .L57
	cmp	r2, #0
	add	r0, r3, r0, asl #3
	ldr	ip, [r0, #16]
	ldrb	r0, [r0, #20]	@ zero_extendqisi2
	add	r0, r0, #8
	add	r0, ip, r0, asl #8
	beq	.L56
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
.L56:
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
.L58:
	.align	2
.L57:
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
	ldr	ip, .L60
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
.L61:
	.align	2
.L60:
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
	ldr	r3, .L63
	add	r3, r3, r0, asl #3
	ldr	r2, [r3, #16]
	ldrb	r3, [r3, #20]	@ zero_extendqisi2
	add	r3, r3, #8
	add	r3, r2, r3, asl #8
	mov	r2, #16
	str	r2, [r3, #8]
	bx	lr
.L64:
	.align	2
.L63:
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
	ldr	r3, .L66
	add	r0, r3, r0, asl #3
	ldrb	r3, [r3, #58]	@ zero_extendqisi2
	ldrb	r2, [r0, #20]	@ zero_extendqisi2
	ldr	r1, [r0, #16]
	add	r2, r2, #8
	add	r2, r1, r2, asl #8
	str	r3, [r2, #8]
	bx	lr
.L67:
	.align	2
.L66:
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
	ldr	r0, .L71
	b	.L69
.L70:
	mov	ip, r0, asl #5
	ldrb	r4, [r3, r2]	@ zero_extendqisi2
	add	ip, ip, r0, lsr #2
	add	r2, r2, #1
	add	ip, ip, r4
	eor	r0, r0, ip
.L69:
	cmp	r2, r1
	bne	.L70
	ldmfd	sp!, {r4, pc}
.L72:
	.align	2
.L71:
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
	ldr	r1, .L76
	bl	memcpy
	mov	r0, #0
	ldmfd	sp!, {r3, pc}
.L77:
	.align	2
.L76:
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
	ldr	r6, .L90
.L82:
	add	r5, r6, r4, asl #5
	ldr	r1, .L90+4
	add	r0, r5, #1
	ldrb	r2, [r6, r4, asl #5]	@ zero_extendqisi2
	bl	FlashMemCmp8
	subs	r1, r0, #0
	bne	.L79
	cmp	r5, #0
	ldrneb	r2, [r5, #22]	@ zero_extendqisi2
	ldrne	r3, .L90+8
	bne	.L84
	b	.L89
.L79:
	add	r4, r4, #1
	cmp	r4, #72
	bne	.L82
	mvn	r0, #0
	ldmfd	sp!, {r4, r5, r6, pc}
.L84:
	ldrb	r0, [r3, r1, asl #5]	@ zero_extendqisi2
	cmp	r0, r2
	beq	.L83
	add	r1, r1, #1
	cmp	r1, #4
	bne	.L84
.L83:
	ldr	r3, .L90+8
	mov	r2, #32
	ldr	r0, .L90+12
	add	r1, r3, r1, asl #5
	bl	memcpy
	ldr	r0, .L90+16
	mov	r1, r5
	mov	r2, #32
	bl	memcpy
	mov	r0, #0
	ldmfd	sp!, {r4, r5, r6, pc}
.L89:
	mvn	r0, #0
	ldmfd	sp!, {r4, r5, r6, pc}
.L91:
	.align	2
.L90:
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
	ldr	r3, .L93
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
.L94:
	.align	2
.L93:
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
	ldr	r3, .L100
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
	ldr	r3, .L100
	uxth	r0, r0
	mls	sl, sl, r0, r9
	uxth	sl, sl
	bne	.L97
	ldrb	r2, [r3, #80]	@ zero_extendqisi2
	cmp	r2, #0
	addeq	r8, r3, r8, asl #1
	movweq	r2, #2228
	ldreqh	r8, [r8, r2]
.L97:
	add	r3, r3, r0, asl #2
	ldr	r3, [r3, #2740]
	mla	r7, r7, sl, r3
	ldrb	r3, [sp, #40]	@ zero_extendqisi2
	cmp	r3, #1
	add	r8, r7, r8
	str	r8, [r6, #0]
	str	r0, [fp, #0]
	bls	.L99
	ldr	r0, [r4, #4]
	ldr	r3, [r4, #40]
	add	r0, r0, #1024
	rsb	r3, r3, r0
	rsbs	r0, r3, #0
	adc	r0, r0, r3
	ldmfd	sp!, {r3, r4, r5, r6, r7, r8, r9, sl, fp, pc}
.L99:
	mov	r0, #0
	ldmfd	sp!, {r3, r4, r5, r6, r7, r8, r9, sl, fp, pc}
.L101:
	.align	2
.L100:
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
	ldr	r3, .L103
	ldr	r2, [r3, #2772]
	str	r0, [r3, #2772]
	mov	r0, r2
	bx	lr
.L104:
	.align	2
.L103:
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
	ldr	r3, .L106
	ldr	r3, [r3, #2776]
	ldrb	r0, [r3, #9]	@ zero_extendqisi2
	bx	lr
.L107:
	.align	2
.L106:
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
	ldr	r3, .L110
	add	r3, r3, r0, asl #3
	ldrb	r0, [r3, #20]	@ zero_extendqisi2
	ldr	r2, [r3, #16]
	add	r0, r0, #8
	add	r0, r2, r0, asl #8
	bx	lr
.L111:
	.align	2
.L110:
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
	ldr	r3, .L113
	mov	r2, r0, asl #8
	orr	r2, r2, r0, asl #16
	orr	r2, r2, #1
	ldr	r3, [r3, #2164]
	str	r2, [r3, #304]
	bx	lr
.L114:
	.align	2
.L113:
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
	ldr	r3, .L116
	orr	r0, r0, #16640
	ldr	r3, [r3, #2164]
	str	r0, [r3, #344]
	bx	lr
.L117:
	.align	2
.L116:
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
	ldr	r3, .L120
	cmp	r0, #0
	ldr	r2, [r3, #2164]
	ldr	r3, [r2, #0]
	bfc	r3, #13, #1
	orrne	r3, r3, #253952
	str	r3, [r2, #0]
	bx	lr
.L121:
	.align	2
.L120:
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
	ldr	r3, .L126
	stmfd	sp!, {r4, lr}
	.save {r4, lr}
	ldr	r3, [r3, #2164]
	ldr	r4, [r3, #0]
	ands	r3, r0, #6
	bfieq	r4, r3, #13, #1
	beq	.L125
	orr	r4, r4, #24576
	tst	r0, #4
	bfc	r4, #15, #1
	mov	r0, #0
	orr	r4, r4, #196608
	orrne	r4, r4, #32768
	bl	rknand_get_clk_rate
	ldr	r3, .L126
	movw	r2, #8322
	ldr	r3, [r3, #2164]
	str	r2, [r3, #344]
	ldr	r2, .L126+4
	str	r2, [r3, #304]
	mov	r2, #38
	str	r2, [r3, #308]
	mov	r2, #39
	str	r2, [r3, #308]
.L125:
	ldr	r3, .L126
	mov	r0, #0
	ldr	r3, [r3, #2164]
	str	r4, [r3, #0]
	ldmfd	sp!, {r4, pc}
.L127:
	.align	2
.L126:
	.word	.LANCHOR0
	.word	1052675
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
	ldr	r3, .L129
	ldr	r3, [r3, #16]
	ldr	r2, [r3, #0]
	bfi	r2, r0, #0, #8
	str	r2, [r3, #0]
	bx	lr
.L130:
	.align	2
.L129:
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
	ldr	r3, .L132
	add	r3, r3, r0, asl #3
	mov	r0, #1
	ldr	r2, [r3, #16]
	ldrb	r1, [r3, #20]	@ zero_extendqisi2
	ldr	r3, [r2, #0]
	mov	r1, r0, asl r1
	bfi	r3, r1, #0, #8
	str	r3, [r2, #0]
	bx	lr
.L133:
	.align	2
.L132:
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
	ldr	r3, .L135
	add	r3, r3, r0, asl #3
	ldr	r2, [r3, #16]
	ldr	r3, [r2, #0]
	bfc	r3, #0, #8
	bfc	r3, #17, #1
	str	r3, [r2, #0]
	bx	lr
.L136:
	.align	2
.L135:
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
.L138:
	ldr	r0, [sp, #4]
	cmp	r0, #0
	sub	r3, r0, #1
	str	r3, [sp, #4]
	bne	.L138
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
	ldr	r3, .L149
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
	bne	.L145
	cmp	r2, #0
	ldreqb	r2, [r3, #61]	@ zero_extendqisi2
	ldrneb	r2, [r3, #62]	@ zero_extendqisi2
	str	r2, [r0, #8]
	ldrb	r3, [r3, #63]	@ zero_extendqisi2
	cmp	r3, #0
	movne	r3, #0
	ldrne	r2, .L149
	bne	.L144
	b	.L145
.L146:
	mov	ip, r3, asl #3
	add	r3, r3, #1
	mov	ip, r1, lsr ip
	uxtb	ip, ip
	str	ip, [r0, #4]
.L144:
	ldrb	ip, [r2, #63]	@ zero_extendqisi2
	cmp	r3, ip
	bcc	.L146
.L145:
	mov	r0, #80
	bl	NandcDelayns
	ldr	r0, [r4, r5, asl #8]
	uxtb	r0, r0
	ldmfd	sp!, {r3, r4, r5, pc}
.L150:
	.align	2
.L149:
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
.L155:
	mov	r0, r6
	mov	r1, r5
	mov	r2, r4
	bl	FlashReadStatusEN
	cmp	r0, #255
	beq	.L155
	tst	r0, #64
	beq	.L155
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
	ldr	r3, .L159
	stmfd	sp!, {r4, r5, r6, lr}
	.save {r4, r5, r6, lr}
	add	r4, r3, r0, asl #4
	ldrb	r5, [r3, r0, asl #4]	@ zero_extendqisi2
	mov	r6, r0
	ldr	r3, [r4, #8]
	cmp	r3, #0
	beq	.L157
	mov	r0, r5
	bl	NandcFlashCs
	ldr	r3, .L159+4
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
.L157:
	mov	r0, #0
	ldmfd	sp!, {r4, r5, r6, pc}
.L160:
	.align	2
.L159:
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
	ldr	r3, .L166
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
	b	.L164
.L165:
	ldrb	r3, [r6, r7]	@ zero_extendqisi2
	mov	r0, #200
	str	r3, [r8, #4]
	bl	NandcDelayns
	ldrb	r3, [r9, r7]	@ zero_extendqisi2
	add	r7, r7, #1
	str	r3, [r8, #0]
.L164:
	uxtb	r3, r7
	cmp	r3, r5
	bcc	.L165
	mov	r3, #22
	mov	r0, r4
	str	r3, [r8, #8]
	bl	NandcFlashDeCs
	ldr	r3, .L166
	add	r4, r3, r4
	strb	sl, [r4, #3760]
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, sl, pc}
.L167:
	.align	2
.L166:
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
	ldr	r6, .L173
	ldr	r3, [r6, #2776]
	ldrb	r3, [r3, #19]	@ zero_extendqisi2
	sub	r3, r3, #1
	uxtb	r3, r3
	cmp	r3, #5
	ldmhifd	sp!, {r4, r5, r6, pc}
	ldr	r5, .L173+4
	mov	r4, #0
.L171:
	ldrb	r3, [r5, r4, asl #3]	@ zero_extendqisi2
	uxtb	r0, r4
	cmp	r3, #173
	bne	.L170
	ldrb	r1, [r6, #2909]	@ zero_extendqisi2
	mov	r3, #0
	ldr	r2, .L173+8
	bl	HynixSetRRPara
.L170:
	add	r4, r4, #1
	cmp	r4, #4
	bne	.L171
	ldmfd	sp!, {r4, r5, r6, pc}
.L174:
	.align	2
.L173:
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
	ldr	r3, .L178
	mov	r5, #0
	ldr	sl, .L178+4
	mov	r8, #161
	add	r1, r3, r1, asl #2
	add	r6, r1, #3
	mov	r7, r3
	b	.L176
.L177:
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
.L176:
	ldrb	r3, [sl, #3764]	@ zero_extendqisi2
	cmp	r5, r3
	bcc	.L177
	ldmfd	sp!, {r3, r4, r5, r6, r7, r8, sl, pc}
.L179:
	.align	2
.L178:
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
	ldr	r3, .L187
	mov	r4, r0
	add	r6, r6, r6, asl #2
	mov	r5, #0
	add	r2, r3, #45
	sub	sl, r3, #2784
	add	r7, r3, r6
	ldr	r8, .L187+4
	add	r6, r2, r6
	add	sl, sl, r1
	b	.L181
.L185:
	mov	r3, #85
	str	r3, [r4, #8]
	ldr	r3, .L187
	mov	r0, #200
	ldrsb	r3, [r5, r3]
	str	r3, [r4, #4]
	bl	NandcDelayns
	ldrb	r3, [r8, #3765]	@ zero_extendqisi2
	cmp	r3, #34
	ldreqsb	r3, [r7, #0]
	beq	.L186
	cmp	r3, #35
	addne	r3, sl, #2912
	addne	r3, r3, #12
	ldreqsb	r3, [r6, #0]
	ldrnesb	r3, [r3, #0]
.L186:
	add	r5, r5, #1
	add	r7, r7, #1
	add	r6, r6, #1
	str	r3, [r4, #0]
.L181:
	ldrb	r3, [r8, #3764]	@ zero_extendqisi2
	cmp	r5, r3
	bcc	.L185
	ldmfd	sp!, {r3, r4, r5, r6, r7, r8, sl, pc}
.L188:
	.align	2
.L187:
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
	ldr	r3, .L190
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
.L191:
	.align	2
.L190:
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
	ldr	r3, .L197
	stmfd	sp!, {r0, r1, r2, r4, r5, lr}
	.save {r0, r1, r2, r4, r5, lr}
	add	r3, r3, r0, asl #3
	ldr	r4, .L197+4
	ldr	r5, [r3, #16]
.L194:
	mov	r0, #100
	bl	NandcDelayns
	ldr	r3, [r5, #0]
	str	r3, [sp, #4]
	ldr	r3, [sp, #4]
	tst	r3, #512
	bne	.L195
	subs	r4, r4, #1
	bne	.L194
	mvn	r0, #0
	b	.L193
.L195:
	mov	r0, #0
.L193:
	ldmfd	sp!, {r1, r2, r3, r4, r5, pc}
.L198:
	.align	2
.L197:
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
	ldr	r5, .L207
	b	.L200
.L206:
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
	bcs	.L202
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
	bne	.L205
	ldr	r0, .L207+4
	ldr	r1, [sp, #8]
	bl	printk
.L205:
	mov	r0, sl
	bl	NandcFlashDeCs
.L202:
	sub	r6, r6, #1
	add	r7, r7, #1
	add	r4, r4, #36
	uxtb	r6, r6
.L200:
	cmp	r7, r8
	bne	.L206
	mov	r0, #0
	add	sp, sp, #20
	ldmfd	sp!, {r4, r5, r6, r7, r8, sl, pc}
.L208:
	.align	2
.L207:
	.word	.LANCHOR0
	.word	.LC2
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
	ldr	r1, .L215
	add	r3, r5, r5, asl #2
	mvn	r6, #4
	add	r0, r1, r3
	add	r1, r1, #45
	add	r1, r1, r3
	mov	r2, #0
	ldr	ip, .L215+4
	mul	r5, r6, r5
	b	.L210
.L213:
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
.L210:
	ldrb	r6, [ip, #3764]	@ zero_extendqisi2
	cmp	r2, r6
	bcc	.L213
	mov	r0, #0
	ldmfd	sp!, {r4, r5, r6, lr}
	b	NandcWaitFlashReady
.L216:
	.align	2
.L215:
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
	ldr	r3, .L218
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
.L219:
	.align	2
.L218:
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
	ldr	r3, .L221
	mov	ip, #0
	stmfd	sp!, {r4, r5, r6, lr}
	.save {r4, r5, r6, lr}
	mov	r6, r2
	ldrb	r2, [r3, #2697]	@ zero_extendqisi2
	ldr	r3, .L221+4
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
.L222:
	.align	2
.L221:
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
	ldr	r3, .L225
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
.L226:
	.align	2
.L225:
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
	ldr	r3, .L229
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
	bhi	.L227
	ldrb	r1, [r5, #2]	@ zero_extendqisi2
	ldrb	r3, [r5, #1]	@ zero_extendqisi2
	ldr	r0, .L229+4
	str	r1, [sp, #0]
	ldrb	r1, [r5, #3]	@ zero_extendqisi2
	str	r1, [sp, #4]
	ldrb	r1, [r5, #4]	@ zero_extendqisi2
	str	r1, [sp, #8]
	ldrb	r1, [r5, #5]	@ zero_extendqisi2
	str	r1, [sp, #12]
	add	r1, r4, #1
	bl	printk
.L227:
	ldmfd	sp!, {r0, r1, r2, r3, r4, r5, r6, r7, r8, pc}
.L230:
	.align	2
.L229:
	.word	.LANCHOR0
	.word	.LC3
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
	ldr	r9, .L245
	mov	r7, #239
	ldr	sl, .L245+4
	mov	r1, r4
	sub	r8, r9, #16
	mov	r6, #128
	mov	r5, #1
.L241:
	mov	r2, r9
	ldrb	lr, [r4, sl]	@ zero_extendqisi2
	ldr	r3, [r2, r4]!
	cmp	lr, #152
	cmpne	lr, #69
	ldrb	r2, [r2, #4]	@ zero_extendqisi2
	add	r2, r2, #8
	add	ip, r3, r2, asl #8
	beq	.L232
	cmp	lr, #173
	cmpne	lr, #44
	bne	.L233
.L232:
	cmp	r0, #1
	ldrb	fp, [r8, #3776]	@ zero_extendqisi2
	bne	.L234
	tst	fp, #1
	beq	.L233
	cmp	lr, #173
	str	r7, [ip, #8]
	streq	r0, [ip, #4]
	beq	.L244
	cmp	lr, #44
	streq	r0, [ip, #4]
	strne	r6, [ip, #4]
	moveq	ip, #5
	strne	r0, [r3, r2, asl #8]
	bne	.L239
	b	.L243
.L234:
	tst	fp, #4
	beq	.L233
	cmp	lr, #173
	str	r7, [ip, #8]
	streq	r5, [ip, #4]
	moveq	ip, #32
	beq	.L243
	cmp	lr, #44
	strne	r6, [ip, #4]
	bne	.L244
	str	r5, [ip, #4]
	mov	ip, #35
.L243:
	str	ip, [r3, r2, asl #8]
	b	.L239
.L244:
	str	r1, [r3, r2, asl #8]
.L239:
	str	r1, [r3, r2, asl #8]
	str	r1, [r3, r2, asl #8]
	str	r1, [r3, r2, asl #8]
.L233:
	add	r4, r4, #8
	cmp	r4, #32
	bne	.L241
	mov	r0, #0
	bl	NandcWaitFlashReady
	mov	r0, #0
	ldmfd	sp!, {r3, r4, r5, r6, r7, r8, r9, sl, fp, pc}
.L246:
	.align	2
.L245:
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
	ldr	r4, .L249
	bl	NandcWaitFlashReady
	bl	FlashSetReadRetryDefault
	ldrb	r3, [r4, #3777]	@ zero_extendqisi2
	cmp	r3, #0
	beq	.L248
	ldrb	r3, [r4, #3776]	@ zero_extendqisi2
	tst	r3, #1
	beq	.L248
	mov	r0, #1
	bl	FlashSetInterfaceMode
	mov	r0, #1
	bl	NandcSetMode
	mov	r3, #0
	strb	r3, [r4, #3777]
.L248:
	ldr	r3, [r4, #16]
	mov	r0, #0
	str	r0, [r3, #336]
	ldmfd	sp!, {r4, pc}
.L250:
	.align	2
.L249:
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
	ldr	r3, .L255
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
	ldrb	r3, [r3, #57]	@ zero_extendqisi2
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
	ldrb	r3, [r3, #57]	@ zero_extendqisi2
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
	.global	HynixGetReadRetryDefault
	.type	HynixGetReadRetryDefault, %function
HynixGetReadRetryDefault:
	.fnstart
	@ args = 0, pretend = 0, frame = 64
	@ frame_needed = 0, uses_anonymous_args = 0
	ldr	r3, .L316
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
	bne	.L258
	mvn	r2, #88
	strb	r2, [r3, #2912]
	ldr	r3, .L316+4
	mvn	r2, #8
	strb	r2, [r3, #2949]
	b	.L297
.L258:
	cmp	r4, #3
	bne	.L260
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
	b	.L312
.L260:
	cmp	r4, #4
	bne	.L261
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
.L312:
	mov	r6, #8
	strb	r2, [r3, #2919]
	mov	r5, r6
	b	.L259
.L261:
	cmp	r4, #5
	bne	.L262
	mov	r2, #56
	mov	r6, #8
	strb	r2, [r3, #2912]
	mov	r2, #57
	strb	r2, [r3, #2913]
	mov	r2, #58
	strb	r2, [r3, #2914]
	mov	r2, #59
	strb	r2, [r3, #2915]
	b	.L311
.L262:
	cmp	r4, #6
	bne	.L297
	mov	r2, #14
	mov	r6, #12
	strb	r2, [r3, #2912]
	mov	r2, #15
	strb	r2, [r3, #2913]
	mov	r2, #16
	strb	r2, [r3, #2914]
	mov	r2, #17
	strb	r2, [r3, #2915]
	b	.L311
.L297:
	mov	r6, #7
.L311:
	mov	r5, #4
.L259:
	sub	r3, r4, #1
	cmp	r3, #1
	bhi	.L310
	b	.L315
.L269:
	add	r1, r2, r8
	add	r4, r2, #2928
	ldr	fp, .L316+8
	mov	r7, #0
	ldrb	r1, [r1, #3768]	@ zero_extendqisi2
	mov	r5, #55
	add	r2, r2, r1, asl #3
	add	r4, r4, r1, asl #6
	ldrb	sl, [r2, #20]	@ zero_extendqisi2
	ldr	r1, [r2, #16]
	add	sl, sl, #8
	add	sl, r1, sl, asl #8
.L265:
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
	bcc	.L265
	mov	r2, #0
	b	.L266
.L267:
	ldrb	r7, [r0, r1, asl #2]	@ zero_extendqisi2
	ldrb	r5, [r4, r2]	@ zero_extendqisi2
	add	r5, r7, r5
	strb	r5, [ip, r1, asl #3]
	add	r1, r1, #1
	cmp	r1, #7
	bne	.L267
	add	r2, r2, #1
	cmp	r2, #4
	beq	.L268
.L266:
	mov	r1, #1
	add	ip, r4, r2
	add	r0, r2, r9
	b	.L267
.L268:
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
	b	.L263
.L315:
	ldr	r9, .L316+12
	mov	r3, r6
	mov	r8, #0
	mov	r6, r5
.L263:
	ldr	r2, .L316
	ldrb	r1, [r2, #3766]	@ zero_extendqisi2
	cmp	r1, r8
	bhi	.L269
	mov	r5, r6
	mov	r6, r3
	b	.L270
.L310:
	sub	r3, r4, #3
	cmp	r3, #3
	bhi	.L270
	mul	r3, r6, r5
	ldr	r8, .L316
	str	r6, [sp, #44]
	mov	r2, r3, asr #2
	mov	r3, r3, asl #4
	str	r3, [sp, #52]
	mov	r3, #0
	str	r2, [sp, #24]
	mov	r7, r3
	str	r3, [sp, #12]
	b	.L271
.L296:
	ldr	ip, [sp, #12]
	ldr	r1, .L316+16
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
	beq	.L313
.L272:
	sub	r3, r4, #5
	cmp	r3, #1
	ldrls	r2, .L316+8
	ldrlsb	r3, [r2, #4]	@ zero_extendqisi2
	strls	r3, [sl, #4]
	movls	r3, #82
	bls	.L314
.L274:
	mov	r3, #174
	str	r3, [sl, #4]
	mov	r3, #176
	str	r7, [r9, fp, asl #8]
.L313:
	str	r3, [sl, #4]
	mov	r3, #77
.L314:
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
.L278:
	add	r3, r3, #1
	ldr	r1, [sl, #0]
	uxtb	r3, r3
	cmp	r3, r2
	bcc	.L278
	ldr	ip, [r8, #3780]
	mov	r3, #0
	str	ip, [sp, #32]
.L279:
	ldr	r2, [sl, #0]
	ldr	lr, [sp, #32]
	strb	r2, [lr, r3]
	add	r3, r3, #1
	ldr	r2, [sp, #52]
	cmp	r3, r2
	blt	.L279
	ldr	r3, [sp, #24]
	mov	ip, r3, asl #2
	mov	lr, r3, asl #3
	ldr	r3, .L316
	str	ip, [sp, #40]
	ldr	r1, [r3, #3780]
	mov	r3, #8
	add	r1, r1, ip
.L281:
	mov	r2, #0
	mov	r0, r2
.L280:
	ldr	ip, [r1, r2]
	add	r0, r0, #1
	mvn	ip, ip
	str	ip, [r1, r2]
	ldr	ip, [sp, #24]
	add	r2, r2, #4
	cmp	r0, ip
	blt	.L280
	subs	r3, r3, #1
	add	r1, r1, lr
	bne	.L281
	str	r3, [sp, #20]
	str	r5, [sp, #60]
	b	.L282
.L286:
	mov	lr, #1
	mov	r0, #0
	mov	ip, lr, asl r2
	mov	lr, r0
	mov	r5, #16
	str	r3, [sp, #0]
	str	r5, [sp, #16]
.L284:
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
	bne	.L284
	cmp	lr, #8
	add	r2, r2, #1
	orrhi	r1, r1, ip
	cmp	r2, #32
	ldr	r3, [sp, #0]
	bne	.L286
	ldr	ip, [sp, #20]
	ldr	r2, [sp, #24]
	add	ip, ip, #1
	ldr	r5, [sp, #36]
	cmp	ip, r2
	str	ip, [sp, #20]
	str	r1, [r5, r3]
	add	r3, r3, #4
	bge	.L287
.L282:
	ldr	r5, [r8, #3780]
	mov	r1, #0
	mov	r2, r1
	add	ip, r5, r3
	str	ip, [sp, #56]
	str	r5, [sp, #36]
	b	.L286
.L287:
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
	b	.L291
.L292:
	ldrb	lr, [ip], #1	@ zero_extendqisi2
	strb	lr, [r1, r3]
	add	r3, r3, #1
	uxtb	lr, r3
	cmp	lr, r5
	bcc	.L292
	ldr	r3, [sp, #16]
	add	r2, r2, #1
	ldr	ip, [sp, #20]
	add	r0, r0, r3
	ldr	r3, [sp, #44]
	add	r1, r1, ip
	cmp	r2, r3
	bge	.L293
.L291:
	mov	ip, r0
	mov	r3, #0
	b	.L292
.L293:
	mov	r3, #255
	mov	r0, r6
	str	r3, [sl, #8]
	bl	NandcWaitFlashReady
	ldr	ip, [sp, #28]
	cmp	ip, #1
	movhi	r3, #56
	strhi	r3, [sl, #8]
	bhi	.L295
	ldr	lr, .L316+8
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
.L295:
	mov	r0, r6
	bl	NandcWaitFlashReady
	ldr	r2, [sp, #12]
	add	r3, r2, #1
	uxtb	r3, r3
	str	r3, [sp, #12]
.L271:
	ldrb	r3, [r8, #3766]	@ zero_extendqisi2
	ldr	ip, [sp, #12]
	cmp	r3, ip
	bhi	.L296
	ldr	r6, [sp, #44]
.L270:
	ldr	r3, .L316
	strb	r5, [r3, #2909]
	strb	r6, [r3, #2910]
	add	sp, sp, #68
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, sl, fp, pc}
.L317:
	.align	2
.L316:
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
	bhi	.L320
	ldmfd	sp!, {r3, lr}
	b	HynixGetReadRetryDefault
.L320:
	cmp	r3, #49
	bne	.L321
	ldr	r0, .L327
	mov	r2, #64
	ldr	r1, .L327+4
	strb	r3, [r0, #2908]
	mov	r3, #4
	strb	r3, [r0, #2909]
	mov	r3, #15
	strb	r3, [r0, #2910]
	add	r0, r0, #2912
	b	.L325
.L321:
	cmp	r3, #33
	cmpne	r3, #65
	beq	.L322
	cmp	r3, #66
	bne	.L323
.L322:
	ldr	r0, .L327
	strb	r3, [r0, #2908]
	mov	r3, #4
	b	.L326
.L323:
	cmp	r3, #34
	cmpne	r3, #67
	bne	.L324
	ldr	r0, .L327
	strb	r3, [r0, #2908]
	mov	r3, #5
.L326:
	strb	r3, [r0, #2909]
	mov	r2, #45
	mov	r3, #7
	ldr	r1, .L327+8
	strb	r3, [r0, #2910]
	add	r0, r0, #2912
	b	.L325
.L324:
	cmp	r3, #35
	cmpne	r3, #68
	ldmnefd	sp!, {r3, pc}
	ldr	r0, .L327
	mov	r2, #95
	ldr	r1, .L327+12
	strb	r3, [r0, #2908]
	mov	r3, #5
	strb	r3, [r0, #2909]
	mov	r3, #17
	strb	r3, [r0, #2910]
	add	r0, r0, #2912
.L325:
	bl	memcpy
	ldmfd	sp!, {r3, pc}
.L328:
	.align	2
.L327:
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
	ldr	r3, .L330
	add	r3, r3, r0, asl #3
	ldr	r3, [r3, #16]
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
	ldr	r1, .L342
	bl	__aeabi_idiv
	ldr	r3, .L342+4
	ldr	r3, [r3, #2164]
	cmp	r0, #250
	movwgt	r2, #8354
	bgt	.L340
	cmp	r0, #220
	bgt	.L341
.L335:
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
	.global	NandcInit
	.type	NandcInit, %function
NandcInit:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	stmfd	sp!, {r3, r4, r5, lr}
	.save {r3, r4, r5, lr}
	mov	r5, #0
	ldr	r4, .L345
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
	ldr	r2, .L345+4
	str	r2, [r3, #304]
	bl	ftl_malloc
	str	r5, [r4, #3820]
	str	r5, [r4, #3828]
	str	r0, [r4, #3792]
	str	r0, [r4, #3796]
	add	r0, r0, #32768
	str	r0, [r4, #3800]
	ldmfd	sp!, {r3, r4, r5, pc}
.L346:
	.align	2
.L345:
	.word	.LANCHOR0
	.word	1579009
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
	bls	.L348
	movw	r3, #4226
	movw	r2, #8322
	cmp	r0, r3
	cmpne	r0, r2
	bne	.L349
.L348:
	ldr	r3, .L350
	ldr	r3, [r3, #2164]
	str	r0, [r3, #4]
.L349:
	ldr	r3, .L350+4
	ldrb	r0, [r3, #2709]	@ zero_extendqisi2
	b	NandcTimeCfg
.L351:
	.align	2
.L350:
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
	ldr	ip, .L353
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
.L354:
	.align	2
.L353:
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
	ldr	r3, .L360
	mov	r1, #1
	ldr	r2, [r3, #2164]
	str	r0, [r3, #3832]
	mov	r3, #0
	str	r1, [r2, #8]
	mov	r1, #16
	cmp	r0, r1
	bfi	r3, r1, #8, #8
	bfc	r3, #18, #1
	beq	.L359
.L356:
	cmp	r0, #24
	orreq	r3, r3, #16
	beq	.L357
	cmp	r0, #40
	orr	r3, r3, #262144
	orr	r3, r3, #16
	bne	.L357
.L359:
	bfc	r3, #4, #1
.L357:
	orr	r3, r3, #1
	str	r3, [r2, #12]
	bx	lr
.L361:
	.align	2
.L360:
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
	ldr	r3, .L363
	strb	r0, [r3, #3836]
	b	NandcBchSel
.L364:
	.align	2
.L363:
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
	ldr	r3, .L370
	ldr	r5, .L370+4
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
.L367:
	ldrb	r3, [r5, r4, asl #3]	@ zero_extendqisi2
	sub	r3, r3, #1
	uxtb	r3, r3
	cmp	r3, #253
	bhi	.L366
	uxtb	r0, r4
	bl	FlashReset
.L366:
	add	r4, r4, #1
	cmp	r4, #4
	bne	.L367
	ldr	r4, .L370
	ldrb	r3, [r4, #3777]	@ zero_extendqisi2
	cmp	r3, #0
	beq	.L368
	mov	r0, #1
	bl	NandcSetMode
	ldrb	r0, [r4, #3776]	@ zero_extendqisi2
	bl	FlashSetInterfaceMode
	ldrb	r0, [r4, #3776]	@ zero_extendqisi2
	bl	NandcSetMode
	ldrb	r0, [r4, #2185]	@ zero_extendqisi2
	bl	NandcSetDdrPara
.L368:
	ldr	r3, .L370
	ldr	r3, [r3, #2776]
	ldrb	r0, [r3, #20]	@ zero_extendqisi2
	bl	FlashBchSel
	mov	r0, #0
	ldmfd	sp!, {r3, r4, r5, pc}
.L371:
	.align	2
.L370:
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
	bne	.L377
	ldmfd	sp!, {r4, lr}
	b	wait_for_nand_flash_ready
.L377:
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
	ldr	r5, .L393
	mov	r6, r0
	str	r1, [sp, #12]
	mov	r7, r2
	ldrb	r4, [r5, #0]	@ zero_extendqisi2
	cmp	r4, #0
	moveq	r9, r5
	beq	.L379
	mov	r1, r2
	bl	FlashEraseSLc2KBlocks
	b	.L380
.L387:
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
	bcs	.L382
	ldrb	r3, [r5, #3837]	@ zero_extendqisi2
	cmp	r3, #0
	add	r3, r9, r0, asl #4
	ldr	r3, [r3, #2788]
	moveq	sl, #0
	cmp	r3, #0
	beq	.L384
	uxtb	r0, r0
	bl	FlashWaitCmdDone
.L384:
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
	bne	.L386
	ldrb	r3, [r5, #80]	@ zero_extendqisi2
	cmp	r3, #0
	beq	.L386
	mov	r0, r8
	bl	flash_enter_slc_mode
.L386:
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
.L382:
	add	r4, r4, #1
.L379:
	cmp	r4, r7
	bcc	.L387
	ldr	r5, .L393
	mov	r4, #0
	ldr	r6, .L393+4
	ldr	r0, [r5, #2164]
	bl	NandcIqrWaitFlashReady
	b	.L388
.L390:
	uxtb	r0, r4
	bl	FlashWaitCmdDone
	ldr	r3, [sp, #12]
	cmp	r3, #1
	bne	.L389
	ldrb	r3, [r5, #80]	@ zero_extendqisi2
	cmp	r3, #0
	beq	.L389
	ldrb	r0, [r6, r4, asl #4]	@ zero_extendqisi2
	bl	flash_exit_slc_mode
.L389:
	add	r4, r4, #1
.L388:
	ldrb	r3, [r5, #3766]	@ zero_extendqisi2
	cmp	r4, r3
	bcc	.L390
	mov	r0, #0
.L380:
	add	sp, sp, #28
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, sl, fp, pc}
.L394:
	.align	2
.L393:
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
	ldr	r3, .L397
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
.L398:
	.align	2
.L397:
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
.L400:
	ldr	r3, [r0, #8]
	str	r3, [sp, #4]
	ldr	r3, [sp, #4]
	tst	r3, #1048576
	beq	.L400
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
	ldr	r2, .L415
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
	bls	.L403
	ldr	r3, [r5, #16]
	cmp	r9, #0
	cmpeq	r1, #0
	str	r3, [sp, #12]
	ldr	r3, [sp, #12]
	bfc	r3, #2, #1
	str	r3, [sp, #12]
	beq	.L404
	cmp	r6, #0
	beq	.L405
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
	b	.L407
.L410:
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
.L407:
	ldr	sl, [sp, #4]
	cmp	r0, sl
	bcc	.L410
.L405:
	mov	r0, r5
	add	r8, r8, #1
	bl	rk_nandc_xfer_irq_flag_init
	mov	r0, r5
	mov	r1, #0
	mov	r8, r8, asr #1
	bl	NandCIrqEnable
	bfi	r4, r8, #22, #6
	ldr	r8, .L415
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
.L404:
	ldr	r3, [sp, #12]
	str	r3, [r5, #16]
.L403:
	str	r7, [r5, #12]
	str	r4, [r5, #8]
	orr	r4, r4, #4
	str	r4, [r5, #8]
	add	sp, sp, #20
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, sl, fp, pc}
.L416:
	.align	2
.L415:
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
	bne	.L418
	cmp	r3, #0
	beq	.L419
	mov	r0, r2
	mov	r1, r3
	mov	r2, #1024
	bl	memcpy
.L419:
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
.L418:
	cmp	r3, #0
	beq	.L421
	mov	r1, r2
	mov	r0, r3
	mov	r2, #1024
	bl	memcpy
.L421:
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
	b	.L423
.L424:
	add	r3, r3, #1
	mov	r2, r2, asl #1
	uxth	r3, r3
.L423:
	cmp	r2, r0
	bls	.L424
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
	ldr	r3, .L429
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
.L430:
	.align	2
.L429:
	.word	.LANCHOR0
	.fnend
	.size	FtlSysBlkNumInit, .-FtlSysBlkNumInit
	.align	2
	.global	FtlConstantsInit
	.type	FtlConstantsInit, %function
FtlConstantsInit:
	.fnstart
	@ args = 0, pretend = 0, frame = 8
	@ frame_needed = 0, uses_anonymous_args = 0
	stmfd	sp!, {r0, r1, r2, r4, r5, r6, r7, r8, r9, sl, fp, lr}
	.save {r0, r1, r2, r4, r5, r6, r7, r8, r9, sl, fp, lr}
	movw	r2, #3864
	ldr	r3, .L444
	mov	r4, r0
	ldrh	lr, [r0, #8]
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
	ldr	r2, .L444+4
.L432:
	strb	r3, [r3, r2]
	add	r3, r3, #1
	cmp	r3, #32
	bne	.L432
	ldrh	r2, [r4, #14]
	ldrh	r3, [r4, #20]
	cmp	r3, r2, lsr #8
	bcs	.L433
	ldr	r2, .L444+4
	sub	ip, r1, #1
	uxtb	r7, r0
	mov	r3, #0
	rsb	sl, r0, r2
	str	sl, [sp, #0]
	mla	ip, r0, ip, r2
	mov	r8, r7, asl #1
	uxtb	r8, r8
	str	ip, [sp, #4]
	b	.L434
.L435:
	strb	ip, [r9, r2]
	add	fp, ip, r7
	add	ip, ip, r8
	add	r5, r5, #1
	strb	fp, [sl, r2]
	uxtb	ip, ip
.L437:
	cmp	r5, r1
	add	r2, r2, r0
	bcc	.L435
	add	r3, r3, #1
.L434:
	cmp	r3, r0
	bcs	.L436
	ldr	fp, [sp, #0]
	mov	r2, #0
	uxtb	ip, r3
	mov	r5, r2
	add	r9, fp, r3
	ldr	fp, [sp, #4]
	add	sl, fp, r3
	b	.L437
.L436:
	ldr	r3, .L444
	movw	r2, #3866
	mov	r1, r1, asl #1
	mov	r6, r6, lsr #1
	strh	r1, [r3, r2]	@ movhi
	movw	r2, #3854
	strh	r6, [r3, r2]	@ movhi
.L433:
	ldr	r3, .L444
	movw	r2, #3902
	cmp	lr, #1
	mov	r1, #5
	mov	ip, #0
	ldr	r8, .L444
	strh	r1, [r3, r2]	@ movhi
	mov	r1, #3904
	streqh	lr, [r3, r2]	@ movhi
	movw	r2, #3906
	strh	ip, [r3, r1]	@ movhi
	mov	r1, #4352
	strh	r1, [r3, r2]	@ movhi
	ldrb	r3, [r3, #0]	@ zero_extendqisi2
	cmp	r3, #0
	ldrne	r3, .L444
	movne	r1, #384
	strneh	r1, [r3, r2]	@ movhi
	movw	r3, #3866
	ldrh	r5, [r8, r3]
	movw	r3, #3844
	mul	r5, r5, r0
	uxth	r5, r5
	strh	r5, [r8, r3]	@ movhi
	add	r3, r3, #10
	ldrh	r6, [r8, r3]
	add	r3, r3, #54
	mul	r0, r0, r6
	uxth	r0, r0
	strh	r0, [r8, r3]	@ movhi
	bl	Ftl_log2
	ldrh	r9, [r4, #16]
	movw	r3, #3910
	ldrh	fp, [r4, #18]
	ldrh	sl, [r4, #20]
	mul	r2, r9, r5
	strh	r0, [r8, r3]	@ movhi
	add	r3, r3, #2
	mov	r0, sl
	strh	r9, [r8, r3]	@ movhi
	add	r3, r3, #2
	strh	fp, [r8, r3]	@ movhi
	add	r3, r3, #2
	strh	r2, [r8, r3]	@ movhi
	add	r3, r3, #2
	strh	sl, [r8, r3]	@ movhi
	bl	Ftl_log2
	mov	r3, #3920
	movw	r2, #3922
	cmp	r6, #1024
	mul	r1, fp, sl
	strh	r0, [r8, r3]	@ movhi
	mov	r3, sl, asl #9
	mov	r7, r0
	uxth	r3, r3
	strh	r3, [r8, r2]	@ movhi
	add	r2, r2, #2
	mov	r3, r3, lsr #8
	strh	r3, [r8, r2]	@ movhi
	movw	r3, #3926
	ldrh	r2, [r4, #26]
	mov	r4, r8
	strh	r2, [r8, r3]	@ movhi
	uxtbhi	r2, r6
	mul	r3, r6, r5
	str	r3, [r8, #3860]
	movhi	r3, #3904
	strhih	r2, [r8, r3]	@ movhi
	mov	r3, #3904
	ldrh	r3, [r8, r3]
	rsb	r3, r3, r6
	mov	r6, r6, asl #6
	mul	r3, r3, r5
	mul	r3, sl, r3
	mul	r9, r9, r3
	movw	r3, #3906
	mov	r9, r9, asr #11
	str	r9, [r8, #3928]
	ldrh	r8, [r8, r3]
	mov	r0, r8, asl #3
	mov	r8, r8, asr r7
	bl	__aeabi_idiv
	add	r7, r7, #9
	movw	r3, #3932
	mov	r6, r6, asr r7
	add	r8, r8, #2
	uxth	r6, r6
	uxth	r0, r0
	strh	r0, [r4, r3]	@ movhi
	cmp	r0, #4
	ldr	r4, .L444
	ldrls	r2, .L444
	movls	r1, #4
	strlsh	r1, [r2, r3]	@ movhi
	movw	r3, #3934
	strh	r8, [r4, r3]	@ movhi
	mov	r3, #3936
	strh	r6, [r4, r3]	@ movhi
	mov	r1, r5
	mul	r3, r5, r6
	add	r6, r6, #8
	str	r3, [r4, #3940]
	movw	r3, #3932
	ldrh	r0, [r4, r3]
	bl	__aeabi_uidiv
	cmp	r5, #1
	uxtah	r6, r6, r0
	str	r6, [r4, #3840]
	addeq	r6, r6, #4
	streq	r6, [r4, #3840]
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
	ldmfd	sp!, {r1, r2, r3, r4, r5, r6, r7, r8, r9, sl, fp, pc}
.L445:
	.align	2
.L444:
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
	ldr	r3, .L453
	movw	r2, #3956
	ldrh	r2, [r3, r2]
	cmp	r2, #0
	beq	.L451
	movw	r2, #3932
	ldrh	r1, [r3, r2]
	ldr	r2, [r3, #3960]
	mov	r3, #0
	b	.L448
.L450:
	ldrh	ip, [r2], #2
	cmp	ip, r0
	beq	.L452
	add	r3, r3, #1
	uxth	r3, r3
.L448:
	cmp	r3, r1
	bne	.L450
	mov	r0, #0
	bx	lr
.L452:
	mov	r0, #1
	bx	lr
.L451:
	mov	r0, r2
	bx	lr
.L454:
	.align	2
.L453:
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
	ldr	r2, .L463
	stmfd	sp!, {r4, lr}
	.save {r4, lr}
	ldr	r3, [r2, #3964]
	cmp	r3, #0
	beq	.L460
	ldr	ip, [r2, #3968]
	mov	r2, #0
.L459:
	ldr	r4, [ip, #16]
	cmp	r4, r0
	bcc	.L457
	cmp	r4, r1
	bls	.L461
.L457:
	add	r2, r2, #1
	add	ip, ip, #36
	cmp	r2, r3
	bne	.L459
	mov	r0, #0
	ldmfd	sp!, {r4, pc}
.L461:
	mov	r0, #1
	ldmfd	sp!, {r4, pc}
.L460:
	mov	r0, r3
	ldmfd	sp!, {r4, pc}
.L464:
	.align	2
.L463:
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
	ldr	r3, .L466
	ldr	r0, [r3, #3952]
	bx	lr
.L467:
	.align	2
.L466:
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
	ldr	r3, .L469
	ldr	r0, [r3, #3952]
	bx	lr
.L470:
	.align	2
.L469:
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
	ldr	r3, .L472
	ldr	r0, [r3, #3972]
	bx	lr
.L473:
	.align	2
.L472:
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
	ldr	r3, .L475
	movw	r2, #3844
	ldrh	r2, [r3, r2]
	ldr	r0, [r3, #3976]
	mul	r0, r0, r2
	bx	lr
.L476:
	.align	2
.L475:
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
	ldr	r3, .L478
	movw	r1, #3844
	movw	r2, #3854
	ldrh	r2, [r3, r2]
	ldrh	r0, [r3, r1]
	mul	r0, r0, r2
	bx	lr
.L479:
	.align	2
.L478:
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
	ldr	r4, .L481
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
	ldr	r0, .L481+4
	bl	printk
	movw	r3, #3986
	ldrh	r2, [r4, r3]
	mov	r0, #0
	add	r2, r2, r6
	strh	r2, [r4, r3]	@ movhi
	ldmfd	sp!, {r2, r3, r4, r5, r6, pc}
.L482:
	.align	2
.L481:
	.word	.LANCHOR0
	.word	.LC4
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
	ldr	r5, .L484
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
.L485:
	.align	2
.L484:
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
	ldr	r6, .L491
	movw	r8, #3908
	movw	r7, #3866
	b	.L488
.L490:
	mov	r0, r5
	add	r5, r5, #1
	bl	FtlBbmIsBadBlock
	uxth	r5, r5
	cmp	r0, #0
	addne	r4, r4, #1
	uxthne	r4, r4
.L488:
	ldrh	r3, [r6, r8]
	ldrh	r2, [r6, r7]
	mul	r3, r2, r3
	cmp	r5, r3
	blt	.L490
	mov	r0, r4
	ldmfd	sp!, {r4, r5, r6, r7, r8, pc}
.L492:
	.align	2
.L491:
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
	ldr	r5, .L494
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
.L495:
	.align	2
.L494:
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
	ldr	r3, .L497
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
.L498:
	.align	2
.L497:
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
	ldr	r4, .L500
	ldrh	r1, [r4, r3]
	bl	__aeabi_uidivmod
	movw	r3, #3868
	uxth	r0, r1
	ldrh	r1, [r4, r3]
	bl	__aeabi_uidiv
	uxth	r0, r0
	ldmfd	sp!, {r4, pc}
.L501:
	.align	2
.L500:
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
	bls	.L503
	rsb	r0, r1, r0
	cmp	r0, #-2147483648
	movhi	r0, #0
	movls	r0, #1
	bx	lr
.L503:
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
	ldr	r2, .L506
	movw	r3, #4046
	ldrh	r0, [r2, r3]
	rsbs	r0, r0, #1
	movcc	r0, #0
	bx	lr
.L507:
	.align	2
.L506:
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
	ldr	r2, .L509
	movw	r3, #4046
	ldrh	r0, [r2, r3]
	sub	r2, r0, #1024
	rsbs	r0, r2, #0
	adc	r0, r0, r2
	bx	lr
.L510:
	.align	2
.L509:
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
	ldr	r2, .L514
	movw	r3, #4046
	stmfd	sp!, {r4, r5, r6, lr}
	.save {r4, r5, r6, lr}
	mov	r5, r0
	ldrh	r3, [r2, r3]
	cmp	r3, #1024
	ldmeqfd	sp!, {r4, r5, r6, pc}
	cmp	r1, #0
	beq	.L513
	bl	P2V_block_in_plane
	ldr	r4, .L514+4
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
.L513:
	ldr	r3, .L514
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
.L515:
	.align	2
.L514:
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
	ldr	r5, .L526
	movw	r8, #4046
	movw	r7, #4042
	ldr	r6, .L526+4
	b	.L517
.L518:
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
.L517:
	ldrh	r3, [r5, r8]
	cmp	r3, r4
	bhi	.L518
	mov	r3, #0
	ldr	ip, .L526
	movw	r8, #4046
	ldr	r4, .L526+4
	movw	r7, #4042
	b	.L519
.L524:
	add	r6, r3, #1
	ldr	r1, [r4, #-2076]
	mov	r2, r3
	uxth	r6, r6
	mov	r0, r6
	b	.L520
.L522:
	ldr	r9, [r1, r2, asl #2]
	ldr	sl, [r1, r0, asl #2]
	cmp	r9, sl
	movhi	r2, r0
	add	r0, r0, #1
	uxth	r0, r0
.L520:
	cmp	r0, r5
	bcc	.L522
	cmp	r3, r2
	beq	.L523
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
.L523:
	mov	r3, r6
.L519:
	ldrh	r5, [ip, r8]
	sub	r2, r5, #1
	cmp	r3, r2
	blt	.L524
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, sl, pc}
.L527:
	.align	2
.L526:
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
	ldr	r2, .L534
	movw	r3, #4046
	stmfd	sp!, {r4, lr}
	.save {r4, lr}
	ldrh	r1, [r2, r3]
	cmp	r1, #1024
	beq	.L532
	movw	r3, #4042
	ldrh	ip, [r2, r3]
	mov	r3, #0
	b	.L530
.L531:
	add	r4, r3, ip
	mov	r4, r4, asl #22
	add	r4, r2, r4, lsr #21
	add	r4, r4, #4048
	ldrh	r4, [r4, #0]
	cmp	r4, r0
	beq	.L533
	add	r3, r3, #1
.L530:
	cmp	r3, r1
	bcc	.L531
.L532:
	mov	r0, #0
	ldmfd	sp!, {r4, pc}
.L533:
	mov	r0, #1
	ldmfd	sp!, {r4, pc}
.L535:
	.align	2
.L534:
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
	ldr	r3, .L539
	stmfd	sp!, {r4, r5, lr}
	.save {r4, r5, lr}
	movw	r4, #4046
	ldrh	r2, [r3, r4]
	cmp	r2, #0
	beq	.L538
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
.L538:
	mov	r0, r2
	ldmfd	sp!, {r4, r5, pc}
.L540:
	.align	2
.L539:
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
	beq	.L542
	ldr	r1, .L556
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
	beq	.L555
.L543:
	ldr	r7, [r1, #-2064]
	mov	sl, r0, asl #1
	ldrh	r1, [r2, #4]
	ldr	r9, .L556+4
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
.L550:
	mov	r9, r1, asl #1
	ldrh	r8, [r3, #4]
	ldrh	sl, [r7, r9]
	cmp	r8, #0
	mvneq	r8, #0
	mulne	r8, r8, sl
	cmp	r8, r6
	bne	.L546
	ldrh	sl, [fp, r9]
	ldr	r9, [sp, #4]
	ldrh	r8, [r9, #0]
	cmp	sl, r8
	bcc	.L548
	b	.L547
.L546:
	bhi	.L547
.L548:
	ldrh	r8, [r3, #0]
	movw	sl, #65535
	cmp	r8, sl
	streqh	r1, [r2, #2]	@ movhi
	streqh	r0, [r3, #0]	@ movhi
	ldreq	r3, .L556
	streq	r2, [r3, #-2060]
	beq	.L542
.L549:
	mov	r1, #6
	mla	r3, r1, r8, r5
	mov	r1, r8
	b	.L550
.L547:
	strh	r1, [r4, ip]	@ movhi
	ldrh	r1, [r3, #2]
	strh	r1, [r2, #2]	@ movhi
	ldr	r1, .L556
	ldr	ip, [r1, #-2068]
	cmp	r3, ip
	bne	.L551
	strh	r0, [r3, #2]	@ movhi
.L555:
	str	r2, [r1, #-2068]
	b	.L542
.L551:
	ldrh	r2, [r3, #2]
	mov	ip, #6
	ldr	r1, [r1, #-2072]
	mul	r2, ip, r2
	strh	r0, [r1, r2]	@ movhi
	strh	r0, [r3, #2]	@ movhi
.L542:
	mov	r0, #0
	ldmfd	sp!, {r1, r2, r3, r4, r5, r6, r7, r8, r9, sl, fp, pc}
.L557:
	.align	2
.L556:
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
	ldr	r2, .L559
	ldr	r3, .L559+4
	ldrh	r1, [r2, r3]
	add	r1, r1, #1
	strh	r1, [r2, r3]	@ movhi
	ldmfd	sp!, {r3, pc}
.L560:
	.align	2
.L559:
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
	beq	.L562
	ldr	r1, .L569
	mov	r5, #6
	mul	ip, r5, r0
	mvn	r3, #0
	ldr	r4, [r1, #-2072]
	add	r2, r4, ip
	strh	r3, [r2, #2]	@ movhi
	strh	r3, [r4, ip]	@ movhi
	ldr	r3, [r1, #-2052]
	cmp	r3, #0
	beq	.L568
.L563:
	ldr	sl, [r1, #-2072]
	mov	r8, r0, asl #1
	ldr	r7, [r1, #-2084]
	rsb	r1, sl, r3
	ldr	r9, .L569+4
	mov	r1, r1, asr #1
	ldrh	r8, [r7, r8]
	mul	r1, r9, r1
	mov	r9, r5
	uxth	r1, r1
.L566:
	mov	r5, r1, asl #1
	ldrh	r5, [r7, r5]
	cmp	r5, r8
	bcs	.L564
	ldrh	r5, [r3, #0]
	cmp	r5, r6
	streqh	r1, [r2, #2]	@ movhi
	streqh	r0, [r3, #0]	@ movhi
	beq	.L562
.L565:
	mla	r3, r9, r5, sl
	mov	r1, r5
	b	.L566
.L564:
	ldrh	r5, [r3, #2]
	strh	r5, [r2, #2]	@ movhi
	strh	r1, [r4, ip]	@ movhi
	ldr	r1, .L569
	ldr	ip, [r1, #-2052]
	cmp	r3, ip
	bne	.L567
	strh	r0, [r3, #2]	@ movhi
.L568:
	str	r2, [r1, #-2052]
	b	.L562
.L567:
	ldrh	r2, [r3, #2]
	mov	ip, #6
	ldr	r1, [r1, #-2072]
	mul	r2, ip, r2
	strh	r0, [r1, r2]	@ movhi
	strh	r0, [r3, #2]	@ movhi
.L562:
	mov	r0, #0
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, sl, pc}
.L570:
	.align	2
.L569:
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
	ldr	r2, .L572
	ldr	r3, .L572+4
	ldrh	r1, [r2, r3]
	add	r1, r1, #1
	strh	r1, [r2, r3]	@ movhi
	ldmfd	sp!, {r3, pc}
.L573:
	.align	2
.L572:
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
	ldr	ip, .L579
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
	bne	.L575
	cmp	r5, r6
	mlane	r2, r2, r5, r3
	moveq	r2, #0
	streq	r2, [r0, #0]
	strne	r2, [r0, #0]
	mvnne	r0, #0
	strneh	r0, [r2, #2]	@ movhi
	b	.L577
.L575:
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
.L577:
	mvn	r2, #0
	mov	r0, #0
	strh	r2, [r3, r1]	@ movhi
	strh	r2, [r4, #2]	@ movhi
	ldmfd	sp!, {r4, r5, r6, pc}
.L580:
	.align	2
.L579:
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
	beq	.L586
	ldr	r2, .L587
	movw	lr, #65535
	mov	ip, #6
	ldr	r4, [r2, #-2072]
	b	.L583
.L585:
	mla	r3, ip, r2, r4
	sub	r1, r1, #1
	uxth	r1, r1
.L583:
	cmp	r1, #0
	beq	.L584
	ldrh	r2, [r3, #0]
	cmp	r2, lr
	bne	.L585
.L584:
	rsb	r4, r4, r3
	ldr	r3, .L587+4
	mov	r4, r4, asr #1
	mul	r4, r3, r4
	uxth	r4, r4
	mov	r1, r4
	bl	List_remove_node
	mov	r0, r4
	ldmfd	sp!, {r4, pc}
.L586:
	movw	r0, #65535
	ldmfd	sp!, {r4, pc}
.L588:
	.align	2
.L587:
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
	ldr	r2, .L596
	ldr	r3, [r2, #-2068]
	cmp	r3, #0
	beq	.L595
	ldr	r2, [r2, #-2072]
	movw	r1, #65535
	mov	ip, #6
	b	.L592
.L594:
	mla	r3, ip, r3, r2
	sub	r0, r0, #1
	uxth	r0, r0
.L592:
	cmp	r0, #0
	beq	.L593
	ldrh	r3, [r3, #0]
	cmp	r3, r1
	bne	.L594
	mov	r0, r1
	bx	lr
.L593:
	rsb	r3, r2, r3
	ldr	r0, .L596+4
	mov	r3, r3, asr #1
	mul	r0, r0, r3
	uxth	r0, r0
	bx	lr
.L595:
	movw	r0, #65535
	bx	lr
.L597:
	.align	2
.L596:
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
	ldr	r4, .L604
	ldr	r3, .L604+4
	ldrh	r3, [r4, r3]
	cmp	r3, r0
	beq	.L599
	ldr	r3, .L604+8
	ldrh	r3, [r4, r3]
	cmp	r3, r0
	beq	.L599
	ldr	r3, .L604+12
	ldrh	r3, [r4, r3]
	cmp	r3, r0
	beq	.L599
	ldr	r3, [r4, #-2072]
	mov	ip, #6
	ldr	r2, [r4, #-2068]
	mla	r1, ip, r0, r3
	cmp	r1, r2
	beq	.L599
	ldrh	r6, [r1, #4]
	mov	r2, r5, asl #1
	ldrh	r1, [r1, #2]
	ldr	r0, [r4, #-2064]
	mla	r1, ip, r1, r3
	ldr	ip, .L604+16
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
	bcs	.L599
	ldr	r0, .L604+20
	mov	r1, r5
	bl	List_remove_node
	ldr	r3, .L604+24
	mov	r0, r5
	ldrh	r2, [r4, r3]
	sub	r2, r2, #1
	strh	r2, [r4, r3]	@ movhi
	bl	INSERT_DATA_LIST
.L599:
	mov	r0, #0
	ldmfd	sp!, {r4, r5, r6, pc}
.L605:
	.align	2
.L604:
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
	b	.L607
.L610:
	mov	r7, r3
	add	r3, r3, #2
	ldrh	r6, [r7, #0]
	cmp	r6, #0
	bne	.L608
	bl	FtlFreeSysBlkQueueOut
	cmp	r0, #0
	strh	r0, [r7, #0]	@ movhi
	beq	.L609
	ldr	r3, [r4, #28]
	strh	r6, [r4, #2]	@ movhi
	add	r3, r3, #1
	str	r3, [r4, #28]
	ldrh	r3, [r4, #8]
	strh	r5, [r4, #0]	@ movhi
	add	r3, r3, #1
	strh	r3, [r4, #8]	@ movhi
	b	.L609
.L608:
	add	r5, r5, #1
	uxth	r5, r5
.L607:
	cmp	r5, r2
	bne	.L610
.L609:
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
	ldr	r2, .L623
	movw	r3, #3950
	stmfd	sp!, {r4, r5, r6, r7, lr}
	.save {r4, r5, r6, r7, lr}
	movw	r1, #65535
	ldrh	r3, [r2, r3]
	ldr	r2, .L623+4
	ldr	ip, [r2, #-1900]
	mov	r2, #0
	mov	r0, r2
	b	.L612
.L614:
	add	r2, r2, #12
	add	r4, ip, r2
	ldrh	r4, [r4, #-12]
	cmp	r4, r1
	ldmeqfd	sp!, {r4, r5, r6, r7, pc}
	add	r0, r0, #1
	uxth	r0, r0
.L612:
	cmp	r0, r3
	bne	.L614
	mov	r1, #0
	mov	r0, r3
	mov	r5, #-2147483648
	mov	r2, r1
	b	.L615
.L617:
	add	r4, ip, r1
	ldr	r4, [r4, #4]
	cmp	r4, #0
	blt	.L616
	cmp	r4, r5
	movcc	r5, r4
	movcc	r0, r2
.L616:
	add	r2, r2, #1
	add	r1, r1, #12
	uxth	r2, r2
.L615:
	cmp	r2, r3
	bne	.L617
	cmp	r0, r3
	ldmccfd	sp!, {r4, r5, r6, r7, pc}
	ldr	r2, .L623+8
	mov	r0, r3
	ldr	r1, .L623+4
	mvn	r4, #0
	ldrh	r6, [r1, r2]
	mov	r2, #0
	mov	r1, r2
	b	.L618
.L620:
	add	r5, ip, r2
	ldr	r5, [r5, #4]
	cmp	r5, r4
	bcs	.L619
	ldrh	r7, [ip, r2]
	cmp	r7, r6
	movne	r4, r5
	movne	r0, r1
.L619:
	add	r1, r1, #1
	add	r2, r2, #12
	uxth	r1, r1
.L618:
	cmp	r1, r3
	bne	.L620
	ldmfd	sp!, {r4, r5, r6, r7, pc}
.L624:
	.align	2
.L623:
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
	ldr	r3, .L631
	ldr	r2, .L631+4
	stmfd	sp!, {r4, r5, lr}
	.save {r4, r5, lr}
	ldrh	r1, [r2, r3]
	cmp	r1, #4
	add	ip, r1, #1
	mov	r1, r3
	strh	ip, [r2, r3]	@ movhi
	bhi	.L626
	cmp	r0, #0
	ldmeqfd	sp!, {r4, r5, pc}
.L626:
	ldr	r0, .L631+8
	mov	r3, #0
	strh	r3, [r2, r1]	@ movhi
	movw	r1, #3852
	str	r3, [r2, #-1892]
	movw	ip, #65535
	ldrh	r4, [r0, r1]
	ldr	r1, [r2, #-2064]
	ldr	r2, .L631+4
	b	.L628
.L630:
	ldrh	r0, [r1], #2
	add	r3, r3, #1
	cmp	r0, ip
	uxth	r3, r3
	ldrne	r5, [r2, #-1892]
	addne	r0, r0, r5
	strne	r0, [r2, #-1892]
.L628:
	cmp	r3, r4
	bne	.L630
	ldmfd	sp!, {r4, r5, pc}
.L632:
	.align	2
.L631:
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
	ldr	r3, .L637
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
.L638:
	.align	2
.L637:
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
	ldr	r3, .L640
	mov	r2, r0, lsr #5
	and	r0, r0, #31
	ldr	r3, [r3, #-1888]
	ldr	r3, [r3, r2, asl #2]
	mov	r0, r3, lsr r0
	and	r0, r0, #1
	bx	lr
.L641:
	.align	2
.L640:
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
	ldr	ip, .L649
	mov	r3, #0
	strh	r3, [r0, #4]	@ movhi
	movw	r3, #3844
	stmfd	sp!, {r4, r5, r6, lr}
	.save {r4, r5, r6, lr}
	ldrh	r3, [ip, r3]
	movw	ip, #65535
	b	.L643
.L645:
	add	r4, r0, r2, asl #1
	add	r2, r2, #1
	ldrh	r4, [r4, #16]
	uxth	r2, r2
	cmp	r4, ip
	ldrneh	r4, [r0, #4]
	addne	r4, r4, #1
	strneh	r4, [r0, #4]	@ movhi
.L643:
	cmp	r2, r3
	bcc	.L645
	ldr	ip, .L649
	movw	r2, #3912
	movw	r4, #65535
	mvn	r1, r1
	ldrh	r5, [ip, r2]
	mov	ip, r0
	mov	r2, #0
	b	.L646
.L648:
	ldrh	r6, [ip, #16]
	add	r2, r2, #1
	add	ip, ip, #2
	cmp	r6, r4
	uxth	r2, r2
	ldrneh	r6, [r0, #4]
	addne	r6, r5, r6
	addne	r6, r6, r1
	strneh	r6, [r0, #4]	@ movhi
.L646:
	cmp	r2, r3
	bne	.L648
	ldmfd	sp!, {r4, r5, r6, pc}
.L650:
	.align	2
.L649:
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
	ldr	r6, .L656
	strh	r5, [r0, #4]	@ movhi
	movw	r8, #3844
	strb	r5, [r4, #7]
	mvn	r7, #0
	b	.L652
.L654:
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
.L652:
	ldrh	r3, [r6, r8]
	cmp	r3, r5
	bhi	.L654
	ldr	r1, .L656
	movw	r2, #3912
	ldrb	r3, [r4, #7]	@ zero_extendqisi2
	ldrh	r2, [r1, r2]
	mul	r3, r2, r3
	strh	r3, [r4, #4]	@ movhi
	mov	r3, #0
	strb	r3, [r4, #9]
	ldr	r3, .L656+4
	ldr	r2, [r3, #-1884]
	cmp	r2, #0
	beq	.L655
	ldrh	r1, [r4, #0]
	ldr	r2, [r3, #-2084]
	mov	r3, r1, asl #1
	ldrh	r3, [r2, r3]
	cmp	r3, #59
	movls	r3, #1
	strlsb	r3, [r4, #9]
.L655:
	mov	r0, #0
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, sl, pc}
.L657:
	.align	2
.L656:
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
	ldr	r6, .L663
	movw	sl, #3844
	movw	r8, #3912
	b	.L659
.L661:
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
.L659:
	ldrh	r3, [r6, sl]
	cmp	r3, r5
	bhi	.L661
	cmp	r4, #0
	beq	.L662
	mov	r1, r4
	mov	r0, #32768
	bl	__aeabi_idiv
	uxth	r4, r0
.L662:
	ldr	r3, .L663+4
	mov	r2, #6
	mov	r0, #0
	ldr	r3, [r3, #-2072]
	mla	r7, r2, r7, r3
	strh	r4, [r7, #4]	@ movhi
	ldmfd	sp!, {r3, r4, r5, r6, r7, r8, sl, pc}
.L664:
	.align	2
.L663:
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
	ldr	r3, .L668
	ldr	r0, [r3, #-2052]
	cmp	r0, #0
	bxeq	lr
	ldr	r2, [r3, #-2072]
	rsb	r0, r2, r0
	ldr	r2, .L668+4
	mov	r0, r0, asr #1
	mul	r0, r2, r0
	ldr	r2, [r3, #-2084]
	uxth	r0, r0
	mov	r3, r0, asl #1
	ldrh	r0, [r2, r3]
	bx	lr
.L669:
	.align	2
.L668:
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
	ldr	r2, .L677
	stmfd	sp!, {r4, r5, lr}
	.save {r4, r5, lr}
	ldr	r3, [r2, #-2052]
	cmp	r3, #0
	beq	.L676
	ldr	r1, .L677+4
	mov	ip, #7
	mov	r5, #6
	movw	r4, #65535
	ldrh	r1, [r2, r1]
	mul	r1, ip, r1
	mov	r1, r1, asr #3
	cmp	r0, r1
	uxthgt	r0, r1
	ldr	r1, [r2, #-2072]
	ldr	r2, .L677+8
	rsb	r3, r1, r3
	mov	r3, r3, asr #1
	mul	r3, r2, r3
	mov	r2, #0
	uxth	r3, r3
	b	.L673
.L675:
	mul	ip, r5, r3
	ldrh	ip, [r1, ip]
	cmp	ip, r4
	beq	.L674
	add	r2, r2, #1
	mov	r3, ip
	uxth	r2, r2
.L673:
	cmp	r2, r0
	bne	.L675
.L674:
	ldr	r2, .L677
	mov	r3, r3, asl #1
	ldr	r2, [r2, #-2084]
	ldrh	r0, [r2, r3]
	ldmfd	sp!, {r4, r5, pc}
.L676:
	mov	r0, r3
	ldmfd	sp!, {r4, r5, pc}
.L678:
	.align	2
.L677:
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
	ldr	r6, .L689
	add	r5, r8, #12
	.pad #32
	sub	sp, sp, #32
	ldr	r1, .L689+4
	bl	strcpy
	mov	r0, r5
	ldr	r1, .L689+8
	ldr	r2, [r6, #2200]
	bl	sprintf
	ldr	r1, .L689+12
	ldr	r2, [r6, #3928]
	add	r5, r5, r0
	mov	r0, r5
	bl	sprintf
	ldr	r3, .L689+16
	ldr	r3, [r3, #2960]
	cmp	r3, #1
	add	r5, r5, r0
	bne	.L685
.L680:
	add	r0, sp, #16
	add	r1, sp, #20
	add	r2, sp, #24
	add	r3, sp, #28
	bl	NandcGetTimeCfg
	ldr	r3, [sp, #24]
	ldr	r2, [sp, #16]
	mov	r0, r5
	ldr	r1, .L689+20
	str	r3, [sp, #0]
	ldr	r3, [sp, #28]
	ldr	r4, .L689+24
	ldr	r7, .L689+28
	str	r3, [sp, #4]
	ldr	r3, [sp, #20]
	bl	sprintf
	ldr	r1, .L689+32
	add	r5, r5, r0
	mov	r0, r5
	add	r5, r5, #10
	bl	strcpy
	ldr	r2, [r6, #3972]
	mov	r0, r5
	ldr	r1, .L689+36
	bl	sprintf
	ldr	r1, .L689+40
	ldr	r2, [r4, #-1892]
	add	r5, r5, r0
	mov	r0, r5
	bl	sprintf
	ldr	r1, .L689+44
	ldr	r2, [r4, #-1880]
	add	r5, r5, r0
	mov	r0, r5
	bl	sprintf
	ldr	r1, .L689+48
	ldr	r2, [r4, #-1876]
	add	r5, r5, r0
	mov	r0, r5
	bl	sprintf
	ldr	r1, .L689+52
	ldr	r2, [r4, #-1872]
	add	r5, r5, r0
	mov	r0, r5
	bl	sprintf
	ldr	r1, .L689+56
	ldr	r2, [r4, #-1868]
	add	r5, r5, r0
	mov	r0, r5
	bl	sprintf
	ldr	r1, .L689+60
	ldr	r2, [r4, #-1864]
	add	r5, r5, r0
	mov	r0, r5
	bl	sprintf
	ldr	r1, .L689+64
	ldr	r2, [r4, #-1860]
	add	r5, r5, r0
	mov	r0, r5
	bl	sprintf
	ldr	r2, [r4, #-1856]
	ldr	r1, .L689+68
	mov	r2, r2, lsr #11
	add	r5, r5, r0
	mov	r0, r5
	bl	sprintf
	ldr	r2, [r4, #-1852]
	ldr	r1, .L689+72
	mov	r2, r2, lsr #11
	add	r5, r5, r0
	mov	r0, r5
	bl	sprintf
	ldr	r1, .L689+76
	ldr	r2, [r4, #-1848]
	add	r5, r5, r0
	mov	r0, r5
	bl	sprintf
	ldr	r1, .L689+80
	ldr	r2, [r4, #-1844]
	add	r5, r5, r0
	mov	r0, r5
	bl	sprintf
	add	r5, r5, r0
	bl	FtlBbtCalcTotleCnt
	movw	r2, #3986
	ldr	r1, .L689+84
	ldrh	r2, [r6, r2]
	mov	r3, r0
	mov	r0, r5
	bl	sprintf
	ldr	r1, .L689+88
	ldrh	r2, [r4, r7]
	add	r5, r5, r0
	mov	r0, r5
	bl	sprintf
	ldr	r1, .L689+92
	ldr	r2, [r4, #-1840]
	add	r5, r5, r0
	mov	r0, r5
	bl	sprintf
	ldr	r1, .L689+96
	ldr	r2, [r4, #-1836]
	add	r5, r5, r0
	mov	r0, r5
	bl	sprintf
	ldr	r1, .L689+100
	ldr	r2, [r4, #-1832]
	add	r5, r5, r0
	mov	r0, r5
	bl	sprintf
	ldr	r1, .L689+104
	ldr	r2, [r4, #-2080]
	add	r5, r5, r0
	mov	r0, r5
	bl	sprintf
	ldr	r1, .L689+108
	ldr	r2, [r4, #-1828]
	add	r5, r5, r0
	mov	r0, r5
	bl	sprintf
	ldr	r1, .L689+112
	ldr	r2, [r4, #-1824]
	add	r5, r5, r0
	mov	r0, r5
	bl	sprintf
	ldr	r3, .L689+116
	ldr	r1, .L689+120
	ldrh	r2, [r4, r3]
	add	r5, r5, r0
	mov	r0, r5
	bl	sprintf
	ldr	r3, .L689+124
	ldr	r1, .L689+128
	ldrh	r2, [r4, r3]
	add	r5, r5, r0
	mov	r0, r5
	bl	sprintf
	ldr	r2, [r6, #3952]
	ldr	r1, .L689+132
	add	r5, r5, r0
	mov	r0, r5
	bl	sprintf
	ldr	r2, [r6, #3944]
	ldr	r1, .L689+136
	add	r5, r5, r0
	mov	r0, r5
	bl	sprintf
	ldr	r2, [r6, #3840]
	ldr	r1, .L689+140
	add	r5, r5, r0
	mov	r0, r5
	bl	sprintf
	movw	r3, #4046
	ldrh	r2, [r6, r3]
	ldr	r1, .L689+144
	add	r5, r5, r0
	mov	r0, r5
	bl	sprintf
	movw	r3, #3852
	ldrh	r2, [r6, r3]
	ldr	r1, .L689+148
	add	r5, r5, r0
	mov	r0, r5
	bl	sprintf
	ldr	r3, .L689+152
	ldr	r1, .L689+156
	ldrh	r2, [r4, r3]
	add	r5, r5, r0
	mov	r0, r5
	bl	sprintf
	ldr	r2, [r6, #3856]
	ldr	r1, .L689+160
	add	r5, r5, r0
	mov	r0, r5
	bl	sprintf
	ldr	r3, .L689+164
	ldr	r1, .L689+168
	ldrh	r2, [r4, r3]
	add	r5, r5, r0
	mov	r0, r5
	bl	sprintf
	movw	r3, #3980
	ldrh	r2, [r6, r3]
	ldr	r1, .L689+172
	ldr	r6, .L689+176
	add	r5, r5, r0
	mov	r0, r5
	bl	sprintf
	ldr	r3, .L689+180
	ldr	r1, .L689+184
	ldrh	r2, [r4, r3]
	add	r5, r5, r0
	mov	r0, r5
	bl	sprintf
	ldr	r1, .L689+188
	ldrb	r2, [r4, #-2038]	@ zero_extendqisi2
	add	r5, r5, r0
	mov	r0, r5
	bl	sprintf
	ldrh	r2, [r4, r6]
	ldr	r1, .L689+192
	add	r5, r5, r0
	mov	r0, r5
	bl	sprintf
	ldr	r1, .L689+196
	ldrb	r2, [r4, #-2036]	@ zero_extendqisi2
	add	r5, r5, r0
	mov	r0, r5
	bl	sprintf
	ldr	r3, .L689+200
	ldr	r1, .L689+204
	ldrh	r2, [r4, r3]
	add	r5, r5, r0
	mov	r0, r5
	bl	sprintf
	ldrh	r3, [r4, r6]
	ldr	r2, [r4, #-2064]
	add	r6, r6, #48
	ldr	r1, .L689+208
	mov	r3, r3, asl #1
	ldrh	r2, [r2, r3]
	add	r5, r5, r0
	mov	r0, r5
	bl	sprintf
	ldr	r3, .L689+212
	ldr	r1, .L689+216
	ldrh	r2, [r4, r3]
	add	r5, r5, r0
	mov	r0, r5
	bl	sprintf
	ldr	r1, .L689+220
	ldrb	r2, [r4, #-1990]	@ zero_extendqisi2
	add	r5, r5, r0
	mov	r0, r5
	bl	sprintf
	ldrh	r2, [r4, r6]
	ldr	r1, .L689+224
	add	r5, r5, r0
	mov	r0, r5
	bl	sprintf
	ldr	r1, .L689+228
	ldrb	r2, [r4, #-1988]	@ zero_extendqisi2
	add	r5, r5, r0
	mov	r0, r5
	bl	sprintf
	ldr	r3, .L689+232
	ldr	r1, .L689+236
	ldrh	r2, [r4, r3]
	add	r5, r5, r0
	mov	r0, r5
	bl	sprintf
	ldrh	r3, [r4, r6]
	ldr	r2, [r4, #-2064]
	add	r6, r6, #240
	ldr	r1, .L689+240
	mov	r3, r3, asl #1
	ldrh	r2, [r2, r3]
	add	r5, r5, r0
	mov	r0, r5
	bl	sprintf
	ldr	r3, .L689+244
	ldr	r1, .L689+248
	ldrh	r2, [r4, r3]
	add	r5, r5, r0
	mov	r0, r5
	bl	sprintf
	ldr	r1, .L689+252
	ldrb	r2, [r4, #-1942]	@ zero_extendqisi2
	add	r5, r5, r0
	mov	r0, r5
	bl	sprintf
	ldr	r3, .L689+256
	ldr	r1, .L689+260
	ldrh	r2, [r4, r3]
	add	r5, r5, r0
	mov	r0, r5
	bl	sprintf
	ldr	r1, .L689+264
	ldrb	r2, [r4, #-1940]	@ zero_extendqisi2
	add	r5, r5, r0
	mov	r0, r5
	bl	sprintf
	ldr	r3, .L689+268
	ldr	r1, .L689+272
	ldrh	r2, [r4, r3]
	add	r5, r5, r0
	mov	r0, r5
	bl	sprintf
	ldr	r3, .L689+276
	ldr	r1, .L689+280
	ldrh	r2, [r4, r3]
	add	r5, r5, r0
	mov	r0, r5
	bl	sprintf
	ldr	r1, .L689+284
	ldrb	r2, [r4, #-1750]	@ zero_extendqisi2
	add	r5, r5, r0
	mov	r0, r5
	bl	sprintf
	ldr	r1, .L689+288
	ldrh	r2, [r4, r6]
	add	r5, r5, r0
	mov	r0, r5
	bl	sprintf
	ldr	r1, .L689+292
	ldrb	r2, [r4, #-1748]	@ zero_extendqisi2
	add	r5, r5, r0
	mov	r0, r5
	bl	sprintf
	ldr	r3, .L689+296
	ldr	r1, .L689+300
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
	ldr	r1, .L689+304
	ldr	r3, [r4, #-1624]
	add	r5, r5, r0
	mov	r0, r5
	bl	sprintf
	ldr	r1, .L689+308
	ldr	r2, [r4, #-1632]
	add	r5, r5, r0
	mov	r0, r5
	bl	sprintf
	ldr	r1, .L689+312
	ldr	r2, [r4, #-1608]
	add	r5, r5, r0
	mov	r0, r5
	bl	sprintf
	ldr	r3, .L689+316
	ldr	r1, .L689+320
	ldrh	r2, [r4, r3]
	add	r5, r5, r0
	mov	r0, r5
	bl	sprintf
	ldr	r3, .L689+324
	ldr	r1, .L689+328
	ldrh	r2, [r4, r3]
	add	r5, r5, r0
	mov	r0, r5
	bl	sprintf
	ldr	r1, .L689+332
	ldr	r2, [r4, #-1188]
	add	r5, r5, r0
	mov	r0, r5
	bl	sprintf
	ldr	r3, .L689+336
	ldr	r1, .L689+340
	ldrh	r2, [r4, r3]
	add	r5, r5, r0
	mov	r0, r5
	bl	sprintf
	add	r5, r5, r0
	bl	GetFreeBlockMinEraseCount
	ldr	r1, .L689+344
	mov	r2, r0
	mov	r0, r5
	bl	sprintf
	add	r5, r5, r0
	ldrh	r0, [r4, r7]
	bl	GetFreeBlockMaxEraseCount
	ldr	r1, .L689+348
	mov	r2, r0
	mov	r0, r5
	bl	sprintf
	ldrh	r3, [r4, r6]
	movw	r2, #65535
	cmp	r3, r2
	add	r5, r5, r0
	beq	.L682
	ldr	r2, [r4, #-2064]
	mov	r3, r3, asl #1
	mov	r0, r5
	ldr	r1, .L689+352
	ldrh	r2, [r2, r3]
	bl	sprintf
	add	r5, r5, r0
.L682:
	mov	r0, #0
	ldr	r4, .L689+24
	bl	List_get_gc_head_node
	mov	r6, #0
	movw	sl, #65535
	mov	r9, #6
	uxth	r3, r0
.L684:
	cmp	r3, sl
	beq	.L683
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
	ldr	r1, .L689+356
	str	r2, [sp, #8]
	mov	r2, r6
	bl	sprintf
	add	r6, r6, #1
	ldr	r3, [r4, #-2072]
	cmp	r6, #16
	ldrh	r3, [r3, r7]
	add	r5, r5, r0
	bne	.L684
.L683:
	ldr	r6, .L689+24
	mov	r4, #0
	movw	sl, #65535
	mov	r9, #6
	ldr	r2, [r6, #-2052]
	ldr	r3, [r6, #-2072]
	rsb	r3, r3, r2
	ldr	r2, .L689+360
	mov	r3, r3, asr #1
	mul	r3, r2, r3
	uxth	r3, r3
.L686:
	cmp	r3, sl
	beq	.L685
	mul	r7, r9, r3
	ldr	r2, [r6, #-2072]
	mov	r0, r5
	add	r2, r2, r7
	ldrh	r2, [r2, #4]
	str	r2, [sp, #0]
	mov	r2, r3, asl #1
	ldr	r1, [r6, #-2084]
	ldrh	r2, [r1, r2]
	ldr	r1, .L689+364
	str	r2, [sp, #4]
	mov	r2, r4
	bl	sprintf
	add	r4, r4, #1
	ldr	r3, [r6, #-2072]
	cmp	r4, #4
	ldrh	r3, [r3, r7]
	add	r5, r5, r0
	bne	.L686
.L685:
	rsb	r0, r8, r5
	add	sp, sp, #32
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, sl, pc}
.L690:
	.align	2
.L689:
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
	ldr	r1, .L692
	ldr	r2, .L692+4
	bl	sprintf
	add	r5, r4, r0
	mov	r0, r5
	bl	FtlPrintInfo2buf
	add	r0, r5, r0
	rsb	r0, r4, r0
	ldmfd	sp!, {r3, r4, r5, pc}
.L693:
	.align	2
.L692:
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
	ldr	r3, .L721
	stmfd	sp!, {r4, r5, r6, r7, r8, r9, sl, fp, lr}
	.save {r4, r5, r6, r7, r8, r9, sl, fp, lr}
	.pad #20
	sub	sp, sp, #20
	ldr	r1, [r3, #-1832]
	ldr	r2, [r3, #-1824]
	cmp	r1, r2
	bcs	.L695
	ldr	r0, .L721+4
	movw	r1, #3852
	mov	r2, #0
	str	r2, [r3, #-1840]
	ldrh	r1, [r0, r1]
	ldr	r0, [r3, #-2084]
	b	.L696
.L697:
	ldrh	lr, [r0], #2
	add	r2, r2, #1
	ldr	ip, [r3, #-1840]
	add	ip, lr, ip
	str	ip, [r3, #-1840]
.L696:
	cmp	r2, r1
	ldr	r4, .L721
	bcc	.L697
	ldr	r5, [r4, #-1840]
	mov	r0, r5
	bl	__aeabi_uidiv
	ldr	r2, .L721+4
	movw	r3, #3902
	ldrh	r1, [r2, r3]
	str	r0, [r4, #-1832]
	ldr	r0, [r4, #-1836]
	rsb	r0, r0, r5
	bl	__aeabi_uidiv
	str	r0, [r4, #-1840]
	b	.L698
.L695:
	ldr	r2, [r3, #-1828]
	cmp	r1, r2
	bls	.L698
	add	r2, r2, #1
	ldr	r4, .L721+4
	str	r2, [r3, #-1828]
	movw	ip, #3852
	mov	r2, #0
	b	.L699
.L700:
	ldr	r0, [r3, #-2084]
	mov	r1, r2, asl #1
	add	r2, r2, #1
	ldrh	r5, [r0, r1]
	add	r5, r5, #1
	strh	r5, [r0, r1]	@ movhi
.L699:
	ldrh	r1, [r4, ip]
	cmp	r2, r1
	bcc	.L700
.L698:
	ldr	r3, .L721
	ldr	r6, [r3, #-1824]
	ldr	r5, [r3, #-1832]
	add	r2, r6, #256
	cmp	r2, r5
	mov	r2, r3
	bls	.L701
	ldr	r1, [r3, #-1828]
	add	r0, r6, #768
	cmp	r0, r1
	bls	.L701
	ldr	r3, [r3, #-1884]
	cmp	r3, #0
	beq	.L719
	cmp	r6, #30
	bhi	.L719
.L701:
	ldr	r3, .L721+8
	ldrh	r0, [r2, r3]
	add	r0, r0, r0, asl #1
	ubfx	r0, r0, #2, #16
	bl	GetFreeBlockMaxEraseCount
	add	r3, r6, #64
	cmp	r0, r3
	mov	r8, r0
	bcs	.L703
	cmp	r6, #30
	bhi	.L719
.L703:
	ldr	r2, .L721
	ldr	r3, [r2, #-2068]
	cmp	r3, #0
	beq	.L719
	movw	r7, #65535
	ldr	r1, [r2, #-2072]
	mov	r4, r7
	ldr	r2, [r2, #-2084]
	mov	sl, r7
	ldr	ip, .L721+12
	mov	fp, #6
	b	.L704
.L707:
	ldrh	r9, [r3, #4]
	cmp	r9, #0
	beq	.L705
	rsb	r3, r1, r3
	mov	r3, r3, asr #1
	mul	r3, ip, r3
	uxth	r3, r3
	mov	r9, r3, asl #1
	ldrh	r9, [r2, r9]
	cmp	r9, r6
	bls	.L716
	cmp	r9, r7
	movcc	r7, r9
	movcc	r4, r3
.L705:
	mla	r3, fp, r0, r1
.L704:
	ldrh	r0, [r3, #0]
	cmp	r0, sl
	bne	.L707
	b	.L706
.L716:
	mov	r4, r3
.L706:
	movw	r3, #65535
	cmp	r4, r3
	beq	.L702
	mov	sl, r4, asl #1
	ldr	fp, .L721
	ldrh	r9, [r2, sl]
	cmp	r9, r6
	bls	.L708
	bl	GetFreeBlockMinEraseCount
	cmp	r0, r6
	strhi	r7, [fp, #-1824]
.L708:
	cmp	r9, #29
	ldr	r2, [fp, #-1884]
	movhi	r3, #0
	movls	r3, #1
	cmp	r2, #0
	moveq	r3, #0
	cmp	r3, #0
	beq	.L709
	add	r3, r9, #10
	cmp	r3, r5
	bls	.L710
	ldr	r2, .L721+4
	movw	r3, #3902
	ldrh	r3, [r2, r3]
	cmp	r3, r9
	bls	.L709
.L710:
	ldr	r6, .L721
	ldr	r3, .L721+16
	ldrh	r3, [r6, r3]
	cmp	r3, #64
	bls	.L709
	ldr	r3, [r6, #-2084]
	mov	r1, r4
	ldr	r0, .L721+20
	ldrh	r2, [r3, sl]
	bl	printk
	b	.L720
.L709:
	cmp	r9, r5
	bcs	.L719
	add	r3, r9, #128
	cmp	r8, r3
	ble	.L719
	add	r3, r9, #256
	ldr	r6, .L721
	cmp	r3, r5
	bcc	.L711
	ldr	r3, [r6, #-1828]
	add	r9, r9, #768
	cmp	r9, r3
	bcs	.L719
.L711:
	ldr	r3, [r6, #-2064]
	mov	r1, r4
	ldr	r0, .L721+24
	mov	r2, r5
	ldrh	r3, [r3, sl]
	str	r3, [sp, #0]
	ldr	r3, [r6, #-2084]
	ldrh	r3, [r3, sl]
	stmib	sp, {r3, r8}
	ldr	r3, [r6, #-1828]
	bl	printk
.L720:
	mov	r3, #1
	str	r3, [r6, #-1180]
	b	.L702
.L719:
	movw	r4, #65535
.L702:
	mov	r0, r4
	add	sp, sp, #20
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, sl, fp, pc}
.L722:
	.align	2
.L721:
	.word	.LANCHOR2
	.word	.LANCHOR0
	.word	-2048
	.word	-1431655765
	.word	-2056
	.word	.LC77
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
	beq	.L724
	ldr	r2, .L725
	mov	r3, r0, asl #1
	mov	r1, #0
	ldr	r2, [r2, #-2064]
	strh	r1, [r2, r3]	@ movhi
	bl	INSERT_FREE_LIST
.L724:
	mov	r0, #0
	ldmfd	sp!, {r3, pc}
.L726:
	.align	2
.L725:
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
	ldr	r2, .L735
	mov	r3, #0
	stmfd	sp!, {r4, r5, r6, r7, r8, r9, sl, fp, lr}
	.save {r4, r5, r6, r7, r8, r9, sl, fp, lr}
	movw	sl, #3844
	str	r3, [r2, #-1176]
	mov	r8, #12
	ldr	r0, .L735+4
	mov	r7, #1
	movw	r6, #3922
	movw	fp, #3924
	b	.L728
.L729:
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
.L728:
	ldrh	r1, [r0, sl]
	cmp	r3, r1
	bcc	.L729
	b	.L734
.L731:
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
	b	.L733
.L734:
	ldr	r3, .L735
	mov	r8, #12
	ldr	ip, .L735+4
	mov	r7, #0
	movw	r6, #3922
	movw	r5, #3924
.L733:
	ldr	r2, [r3, #-1156]
	cmp	r1, r2
	bcc	.L731
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, sl, fp, pc}
.L736:
	.align	2
.L735:
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
	ldr	r3, .L744
	stmfd	sp!, {r4, r5, r6, r7, r8, r9, sl, fp, lr}
	.save {r4, r5, r6, r7, r8, r9, sl, fp, lr}
	mov	r5, #36
	ldr	r6, [r3, #-1156]
	mov	fp, #12
	ldr	ip, [r3, #-1172]
	mov	r3, #0
	mov	r4, r3
	b	.L738
.L741:
	mul	sl, fp, r2
	add	r8, ip, sl
	ldr	r9, [ip, sl]
	ldr	sl, [r7, #8]
	cmp	r9, sl
	streq	r4, [r8, #8]
	beq	.L740
.L739:
	add	r2, r2, #1
	uxth	r2, r2
.L743:
	cmp	r2, r6
	bcc	.L741
.L740:
	add	r3, r3, #1
	uxth	r3, r3
.L738:
	cmp	r3, r1
	ldmcsfd	sp!, {r4, r5, r6, r7, r8, r9, sl, fp, pc}
	mla	r7, r5, r3, r0
	mov	r2, #0
	b	.L743
.L745:
	.align	2
.L744:
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
	ldr	r3, .L753
	stmfd	sp!, {r4, r5, r6, r7, r8, sl, lr}
	.save {r4, r5, r6, r7, r8, sl, lr}
	mov	r6, #12
	ldr	r8, [r3, #-1156]
	mov	r5, #1
	ldr	r7, [r3, #-1172]
	mov	r4, #36
	mov	r3, #0
	b	.L747
.L750:
	mla	ip, r6, r2, r7
	ldr	sl, [ip, #8]
	cmp	sl, #0
	bne	.L748
	mla	r2, r4, r3, r0
	ldr	sl, [ip, #0]
	str	r5, [ip, #8]
	str	sl, [r2, #8]
	ldr	ip, [ip, #4]
	str	ip, [r2, #12]
	b	.L749
.L748:
	add	r2, r2, #1
	uxth	r2, r2
	b	.L751
.L752:
	mov	r2, #0
.L751:
	cmp	r2, r8
	bcc	.L750
.L749:
	add	r3, r3, #1
	uxth	r3, r3
.L747:
	cmp	r3, r1
	bcc	.L752
	ldmfd	sp!, {r4, r5, r6, r7, r8, sl, pc}
.L754:
	.align	2
.L753:
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
	ldr	r3, .L760
	ldr	r2, .L760+4
	ldrh	r1, [r3, r2]
	ldr	r2, [r3, #-1148]
	mov	r3, #0
	b	.L756
.L758:
	ldrh	ip, [r2], #2
	cmp	ip, r0
	beq	.L759
	add	r3, r3, #1
	uxth	r3, r3
.L756:
	cmp	r3, r1
	bne	.L758
	mov	r0, #0
	bx	lr
.L759:
	mov	r0, #1
	bx	lr
.L761:
	.align	2
.L760:
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
	ldr	r3, .L766
	ldr	r2, .L766+4
	ldrh	ip, [r3, r2]
	ldr	r2, [r3, #-1148]
	mov	r3, #0
	mov	r1, r2
	b	.L763
.L765:
	ldrh	r7, [r1], #2
	cmp	r7, r0
	beq	.L764
	add	r3, r3, #1
	uxth	r3, r3
.L763:
	cmp	r3, ip
	bne	.L765
	mov	r3, r3, asl #1
	strh	r0, [r2, r3]	@ movhi
	ldr	r2, .L766
	ldr	r3, .L766+4
	ldrh	r1, [r2, r3]
	add	r1, r1, #1
	strh	r1, [r2, r3]	@ movhi
.L764:
	ldr	r3, .L766
	mov	r0, #12
	ldr	r2, .L766+8
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
.L767:
	.align	2
.L766:
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
	ldr	r0, .L772
	bl	printk
	ldr	r3, .L772+4
	ldr	r2, .L772+8
	movw	r1, #65535
	ldrh	r0, [r3, r2]
	cmp	r0, r1
	beq	.L771
.L769:
	ldr	r2, .L772+12
	ldrh	r0, [r3, r2]
	cmp	r0, r1
	bne	.L770
.L771:
	strh	r4, [r3, r2]	@ movhi
.L770:
	mov	r0, #0
	ldmfd	sp!, {r4, pc}
.L773:
	.align	2
.L772:
	.word	.LC79
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
	ldr	r5, .L779
	mov	r2, r4
	ldr	r7, .L779+4
	ldrh	r1, [r5, r7]
	mov	r6, r0
	ldr	r0, .L779+8
	bl	printk
	mov	r0, r6
	bl	FtlGcRefreshBlock
	ldr	r3, [r5, #-1884]
	cmp	r3, #0
	beq	.L775
	ldr	r3, [r5, #-2084]
	mov	r6, r6, asl #1
	ldrh	r2, [r3, r6]
	cmp	r2, #29
	subhi	r2, r2, #30
	strhih	r2, [r3, r6]	@ movhi
.L775:
	ldrh	r1, [r5, r7]
	mov	r3, #0
	ldr	r2, .L779+12
	b	.L776
.L778:
	ldrh	r0, [r2, #2]!
	cmp	r0, r4
	beq	.L777
	add	r3, r3, #1
	uxth	r3, r3
.L776:
	cmp	r3, r1
	bne	.L778
	cmp	r3, #15
	bhi	.L777
	ldr	r2, .L779
	ldr	r1, .L779+16
	add	r0, r2, r3, asl #1
	add	r3, r3, #1
	strh	r4, [r0, r1]	@ movhi
	sub	r1, r1, #2
	strh	r3, [r2, r1]	@ movhi
.L777:
	mov	r0, #0
	ldmfd	sp!, {r3, r4, r5, r6, r7, pc}
.L780:
	.align	2
.L779:
	.word	.LANCHOR2
	.word	-1134
	.word	.LC80
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
	ldr	r2, .L784
	ldr	r3, .L784+4
	ldrh	r2, [r3, r2]
	cmp	r2, #0
	beq	.L782
	ldr	r1, .L784+8
	ldrh	r0, [r3, r1]
	movw	r1, #65535
	cmp	r0, r1
	bne	.L782
	ldr	r4, .L784+12
	ldr	r5, .L784+4
	ldrh	r1, [r3, r4]
	cmp	r1, r2
	movcs	r2, #0
	strcsh	r2, [r3, r4]	@ movhi
	ldrh	r2, [r5, r4]
	ldr	r3, .L784+16
	add	r2, r5, r2, asl #1
	ldrh	r0, [r2, r3]
	bl	P2V_block_in_plane
	bl	FtlGcRefreshBlock
	ldrh	r3, [r5, r4]
	add	r3, r3, #1
	strh	r3, [r5, r4]	@ movhi
.L782:
	mov	r0, #0
	ldmfd	sp!, {r3, r4, r5, pc}
.L785:
	.align	2
.L784:
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
	beq	.L787
	bl	memset
.L787:
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
	ldr	r4, .L789
	movw	r5, #3916
	ldr	r2, .L789+4
	mov	r1, #255
	ldr	r6, .L789+8
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
.L790:
	.align	2
.L789:
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
	ldr	r7, .L800
	mov	r2, #6
	ldr	r4, .L800+4
	mov	r1, #0
	mov	r6, #0
	ldrh	r3, [r7, r3]
	mov	sl, r6
	ldr	r0, [r4, #-2072]
	mul	r2, r2, r3
	bl	ftl_memset
	ldr	r3, .L800+8
	str	r6, [r4, #-2052]
	str	r6, [r4, #-2068]
	strh	r6, [r4, r3]	@ movhi
	add	r3, r3, #8
	str	r6, [r4, #-2060]
	strh	r6, [r4, r3]	@ movhi
	mov	r4, r6
	b	.L792
.L794:
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
	b	.L798
.L799:
	mov	r5, #0
	movw	fp, #3844
	mov	r8, r5
	movw	r9, #3912
.L798:
	ldrh	r3, [r7, fp]
	cmp	r3, r8
	bhi	.L794
	cmp	r5, #0
	beq	.L795
	mov	r1, r5
	mov	r0, #32768
	bl	__aeabi_idiv
	uxth	r5, r0
.L795:
	ldr	r3, .L800+4
	mov	r1, #6
	ldr	r2, [r3, #-2072]
	mla	r2, r1, r4, r2
	strh	r5, [r2, #4]	@ movhi
	ldr	r2, .L800+12
	ldrh	r2, [r3, r2]
	cmp	r2, r4
	beq	.L796
	ldr	r2, .L800+16
	ldrh	r2, [r3, r2]
	cmp	r2, r4
	beq	.L796
	ldr	r2, .L800+20
	ldrh	r2, [r3, r2]
	cmp	r2, r4
	beq	.L796
	ldr	r2, [r3, #-2064]
	mov	r3, r4, asl #1
	ldrh	r3, [r2, r3]
	cmp	r3, #0
	bne	.L797
	add	r6, r6, #1
	mov	r0, r4
	uxth	r6, r6
	bl	INSERT_FREE_LIST
	b	.L796
.L797:
	add	sl, sl, #1
	mov	r0, r4
	uxth	sl, sl
	bl	INSERT_DATA_LIST
.L796:
	add	r4, r4, #1
	uxth	r4, r4
.L792:
	movw	r2, #3852
	ldrh	r3, [r7, r2]
	cmp	r3, r4
	bhi	.L799
	ldr	r3, .L800+4
	mov	r0, #0
	ldr	r2, .L800+8
	strh	sl, [r3, r2]	@ movhi
	add	r2, r2, #8
	strh	r6, [r3, r2]	@ movhi
	ldmfd	sp!, {r3, r4, r5, r6, r7, r8, r9, sl, fp, pc}
.L801:
	.align	2
.L800:
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
	ldr	r4, .L805
	movw	r6, #3922
	ldr	r5, .L805+4
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
	b	.L803
.L804:
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
.L803:
	ldrh	r3, [r4, r7]
	ldr	r1, .L805
	cmp	r3, r2
	ldr	r3, .L805+4
	bhi	.L804
	ldr	r0, .L805+8
	mvn	r2, #0
	strh	r2, [r3, r0]	@ movhi
	ldr	r0, .L805+12
	strh	r2, [r3, r0]	@ movhi
	ldr	r0, [r1, #3940]
	ldr	r2, .L805+16
	strh	r0, [r3, r2]	@ movhi
	sub	r2, r2, #6
	ldr	r0, .L805+20
	strh	r0, [r3, r2]	@ movhi
	add	r2, r2, #36
	ldrh	r0, [r3, r2]
	sub	r2, r2, #32
	strh	r0, [r3, r2]	@ movhi
	movw	r2, #3948
	ldrh	r1, [r1, r2]
	ldr	r2, .L805+24
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
.L806:
	.align	2
.L805:
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
	b	.L808
.L812:
	ldr	r0, [r7, r3, asl #2]
	mov	r2, #0
	ubfx	r0, r0, #10, #16
	b	.L809
.L811:
	mov	r1, r2, asl #1
	add	r2, r2, #1
	ldrh	ip, [r6, r1]
	uxth	r2, r2
	cmp	ip, r0
	ldreqh	ip, [r5, r1]
	addeq	ip, ip, #1
	streqh	ip, [r5, r1]	@ movhi
.L809:
	ldrh	r1, [r4, #10]
	cmp	r1, r2
	bhi	.L811
	add	r3, r3, #1
	uxth	r3, r3
.L808:
	ldrh	r2, [r4, #6]
	cmp	r2, r3
	bhi	.L812
	mov	sl, #0
	ldrh	fp, [r5, #0]
	mov	r7, sl
	ldr	r3, .L818
	movw	r2, #3914
	b	.L813
.L817:
	ldrh	r1, [r4, #0]
	cmp	r1, r7
	bne	.L814
	ldrh	r0, [r4, #2]
	ldrh	r1, [r3, r2]
	cmp	r0, r1
	movcc	r0, r7, asl #1
	strcch	r1, [r5, r0]	@ movhi
.L814:
	mov	r9, r7, asl #1
	ldrh	r8, [r5, r9]
	cmp	fp, r8
	movhi	sl, r7
	movhi	fp, r8
	cmp	r8, #0
	bne	.L816
	ldrh	r0, [r6, r9]
	cmp	r0, #0
	beq	.L816
	mov	r1, #1
	stmia	sp, {r2, r3}
	bl	FtlFreeSysBlkQueueIn
	strh	r8, [r6, r9]	@ movhi
	ldmia	sp, {r2, r3}
	ldrh	r1, [r4, #8]
	sub	r1, r1, #1
	strh	r1, [r4, #8]	@ movhi
.L816:
	add	r7, r7, #1
	uxth	r7, r7
.L813:
	ldrh	r1, [r4, #10]
	cmp	r1, r7
	bhi	.L817
	mov	r0, sl
	ldmfd	sp!, {r1, r2, r3, r4, r5, r6, r7, r8, r9, sl, fp, pc}
.L819:
	.align	2
.L818:
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
	ldr	r3, .L821
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
.L822:
	.align	2
.L821:
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
	ldr	r3, .L824
	movw	r2, #3980
	mvn	r1, #0
	ldr	r0, .L824+4
	strh	r1, [r3, r2]	@ movhi
	add	r2, r2, #6
	mov	r1, #0
	strh	r1, [r3, r2]	@ movhi
	mov	r1, #255
	mov	r2, #16
	b	ftl_memset
.L825:
	.align	2
.L824:
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
	ldr	r3, .L830
	ldr	r2, .L830+4
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
.L828:
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
	bne	.L828
	ldmfd	sp!, {r4, r5, r6, pc}
.L831:
	.align	2
.L830:
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
	ldr	r5, .L833
	mov	r4, #0
	ldr	r2, .L833+4
	mov	r1, r4
	ldr	r6, .L833+8
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
	ldr	r0, .L833+12
	bl	ftl_memset
	mov	r1, r4
	mov	r2, #512
	ldr	r0, .L833+16
	bl	ftl_memset
	bl	FtlGcBufInit
	bl	FtlL2PDataInit
	mov	r0, r4
	ldmfd	sp!, {r3, r4, r5, r6, r7, pc}
.L834:
	.align	2
.L833:
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
	ldr	r4, .L866
	mov	r6, #0
	ldr	r2, .L866+4
	mov	r0, #1024
	ldr	r5, .L866+8
	mov	r7, #12
	str	r3, [r4, #-996]
	mvn	r3, #0
	strh	r3, [r4, r2]	@ movhi
	add	r2, r2, #2
	str	r6, [r4, #-1004]
	movw	sl, #3844
	strh	r3, [r4, r2]	@ movhi
	mov	r2, #32
	ldr	r3, .L866+12
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
	ldr	r4, .L866
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
	ldr	sl, .L866+16
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
	ldr	r6, .L866+20
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
	ldr	ip, .L866+8
	mov	r1, r1, asl #2
	mov	r2, r1
	str	r0, [r5, #4008]
	ldrh	r5, [r5, r7]
	ldr	r0, .L866+24
	b	.L837
.L838:
	ldr	r4, [ip, #4008]
	add	r3, r3, #1
	add	r4, r4, r2
	add	r2, r2, r1
	str	r4, [r0, #4]!
.L837:
	cmp	r3, r5
	bcc	.L838
	ldr	r0, .L866+28
	mov	r2, #0
	mov	r1, r2
	add	r0, r0, r3, asl #2
	b	.L839
.L840:
	add	ip, r0, r2
	add	r3, r3, #1
	add	r2, r2, #4
	str	r1, [ip, #28]
.L839:
	cmp	r3, #7
	bls	.L840
	ldr	r3, .L866
	ldr	r2, [r3, #-1044]
	cmp	r2, #0
	beq	.L865
.L841:
	ldr	r2, [r3, #-1096]
	cmp	r2, #0
	beq	.L865
.L843:
	ldr	r2, [r3, #-1036]
	cmp	r2, #0
	beq	.L865
.L844:
	ldr	r2, [r3, #-1040]
	cmp	r2, #0
	beq	.L865
.L845:
	ldr	r2, [r3, #-1900]
	cmp	r2, #0
	beq	.L865
.L846:
	ldr	r2, [r3, #-1092]
	cmp	r2, #0
	beq	.L865
.L847:
	ldr	r2, [r3, #-2072]
	cmp	r2, #0
	beq	.L865
.L848:
	ldr	r2, .L866+8
	ldr	r2, [r2, #4008]
	cmp	r2, #0
	beq	.L865
.L849:
	ldr	r3, [r3, #-2064]
	cmp	r3, #0
	beq	.L865
.L850:
	ldr	r3, .L866
	ldr	r2, [r3, #-1148]
	cmp	r2, #0
	beq	.L865
	ldr	r2, [r3, #-1144]
	cmp	r2, #0
	beq	.L865
.L852:
	ldr	r2, [r3, #-984]
	cmp	r2, #0
	beq	.L865
	ldr	r2, [r3, #-976]
	cmp	r2, #0
	beq	.L865
	ldr	r2, [r3, #-2088]
	cmp	r2, #0
	beq	.L865
	ldr	r2, [r3, #-1160]
	cmp	r2, #0
	beq	.L865
	ldr	r2, [r3, #-980]
	cmp	r2, #0
	beq	.L865
.L854:
	ldr	r2, [r3, #-2076]
	cmp	r2, #0
	beq	.L865
	ldr	r2, [r3, #-972]
	cmp	r2, #0
	beq	.L865
	ldr	r3, [r3, #-968]
	cmp	r3, #0
	beq	.L865
	ldr	r3, .L866
	ldr	r2, [r3, #-1168]
	cmp	r2, #0
	beq	.L865
	ldr	r2, [r3, #-960]
	cmp	r2, #0
	beq	.L865
	ldr	r2, [r3, #-956]
	cmp	r2, #0
	beq	.L865
	ldr	r2, [r3, #-1172]
	cmp	r2, #0
	beq	.L865
.L856:
	ldr	r2, [r3, #-952]
	cmp	r2, #0
	beq	.L865
	ldr	r2, [r3, #-948]
	cmp	r2, #0
	beq	.L865
	ldr	r2, [r3, #-1164]
	cmp	r2, #0
	beq	.L865
.L858:
	ldr	r2, [r3, #-2084]
	cmp	r2, #0
	beq	.L865
	ldr	r3, [r3, #-1008]
	cmp	r3, #0
	beq	.L865
.L860:
	ldr	r3, .L866+8
	ldr	r3, [r3, #3960]
	cmp	r3, #0
	beq	.L865
.L861:
	ldr	r3, .L866
	ldr	r2, [r3, #-928]
	cmp	r2, #0
	beq	.L865
.L862:
	ldr	r2, [r3, #-924]
	cmp	r2, #0
	beq	.L865
.L863:
	ldr	r3, [r3, #-920]
	cmp	r3, #0
	bne	.L864
.L865:
	ldr	r0, .L866+32
	ldr	r1, .L866+36
	bl	printk
	mvn	r0, #0
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, sl, pc}
.L864:
	mov	r0, #0
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, sl, pc}
.L867:
	.align	2
.L866:
	.word	.LANCHOR2
	.word	-1138
	.word	.LANCHOR0
	.word	-1192
	.word	-940
	.word	-1032
	.word	.LANCHOR0+4008
	.word	.LANCHOR0+3980
	.word	.LC81
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
	ldr	r3, .L871
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
	ldr	r1, .L871+4
	strb	r0, [r4, #10]
	strb	r2, [r4, #9]
	mov	r2, #1
	b	.L869
.L870:
	ldrb	r5, [r3, r1]	@ zero_extendqisi2
	add	r3, r3, #1
	ldrb	r0, [r4, #10]	@ zero_extendqisi2
	orr	r0, r0, r2, asl r5
	strb	r0, [r4, #10]
.L869:
	uxtb	r0, r3
	cmp	r0, ip
	bcc	.L870
	ldmfd	sp!, {r3, r4, r5, pc}
.L872:
	.align	2
.L871:
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
	ldr	r1, .L885
	mov	r2, #0
	stmfd	sp!, {r3, r4, r5, r6, r7, r8, sl, lr}
	.save {r3, r4, r5, r6, r7, r8, sl, lr}
	ldr	r3, .L885+4
	strb	r2, [r1, #-916]
	ldr	r1, .L885+8
	strb	r2, [r3, #3766]
	movw	r2, #2698
	ldrh	r2, [r1, r2]
	cmp	r2, #256
	str	r2, [r3, #4]
	movhi	r2, #512
	bhi	.L884
	cmp	r2, #128
	bls	.L875
	mov	r2, #256
.L884:
	str	r2, [r3, #4]
.L875:
	mov	r1, #0
	mov	r2, #8
	ldr	r0, .L885+12
	mov	r6, #0
	bl	ftl_memset
	mov	r1, #0
	mov	r2, #32
	ldr	r0, .L885+16
	bl	ftl_memset
	ldr	r0, .L885+20
	mov	r1, #0
	mov	r2, #128
	bl	ftl_memset
	ldr	r7, .L885+24
	ldr	r5, .L885+4
	mov	r8, r7
.L877:
	ldr	r4, [r5, #2776]
	mov	r1, r8
	add	r0, r4, #1
	ldrb	r2, [r4, #0]	@ zero_extendqisi2
	bl	FlashMemCmp8
	cmp	r0, #0
	bne	.L876
	ldrb	r3, [r5, #3766]	@ zero_extendqisi2
	add	r2, r5, r3, asl #2
	str	r0, [r2, #2740]
	add	r2, r5, r3
	add	r3, r3, #1
	strb	r3, [r5, #3766]
	strb	r6, [r2, #3768]
.L876:
	add	r6, r6, #1
	add	r8, r8, #8
	cmp	r6, #4
	bne	.L877
	ldr	r5, .L885+4
	ldr	r3, .L885
	ldrb	r2, [r5, #3766]	@ zero_extendqisi2
	strb	r2, [r3, #-916]
	ldrb	r3, [r4, #8]	@ zero_extendqisi2
	cmp	r3, #2
	bne	.L878
	add	sl, r4, #1
	mov	r6, #0
	mov	r8, r5
.L881:
	mov	r0, sl
	mov	r1, r7
	ldrb	r2, [r4, #0]	@ zero_extendqisi2
	bl	FlashMemCmp8
	cmp	r0, #0
	bne	.L879
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
.L879:
	add	r6, r6, #1
	add	r7, r7, #8
	cmp	r6, #4
	bne	.L881
.L878:
	ldr	r3, .L885+4
	ldrb	r1, [r4, #13]	@ zero_extendqisi2
	ldr	r2, .L885
	ldrb	r3, [r3, #3766]	@ zero_extendqisi2
	mul	r1, r1, r3
	ldrh	r3, [r4, #14]
	mul	r1, r3, r1
	ldr	r3, .L885+28
	strh	r1, [r2, r3]	@ movhi
	ldmfd	sp!, {r3, r4, r5, r6, r7, r8, sl, pc}
.L886:
	.align	2
.L885:
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
	bne	.L888
	ldr	r3, .L927
.L889:
	strh	r0, [r3, #2]!	@ movhi
	add	r0, r0, #1
	uxth	r0, r0
	cmp	r0, #256
	bne	.L889
	b	.L890
.L888:
	cmp	r0, #1
	bne	.L891
	ldr	r0, .L927+4
	mov	r2, #0
	mov	r3, r2
.L894:
	uxth	r1, r3
	cmp	r1, #3
	bls	.L892
	tst	r3, #1
	moveq	r1, #2
	movne	r1, #3
	rsb	r1, r1, r2
	uxth	r1, r1
.L892:
	add	r3, r3, #1
	strh	r1, [r2, r0]	@ movhi
	cmp	r3, #256
	add	r2, r2, #2
	bne	.L894
	b	.L890
.L891:
	cmp	r0, #2
	bne	.L895
	mov	r3, #0
	ldr	r0, .L927
	movw	r2, #65535
	mov	r1, r3
	b	.L926
.L898:
	cmp	r3, #1
	movls	r1, r3
	movhi	r1, r2
.L926:
	add	r3, r3, #1
	add	r2, r2, #2
	strh	r1, [r0, #2]!	@ movhi
	uxth	r3, r3
	uxth	r2, r2
	cmp	r3, #256
	bne	.L898
	b	.L890
.L895:
	cmp	r0, #3
	bne	.L899
	ldr	r0, .L927+4
	mov	r2, #0
	mov	r3, r2
.L902:
	uxth	r1, r3
	cmp	r1, #5
	bls	.L900
	tst	r3, #1
	moveq	r1, #4
	movne	r1, #5
	rsb	r1, r1, r2
	uxth	r1, r1
.L900:
	add	r3, r3, #1
	strh	r1, [r2, r0]	@ movhi
	cmp	r3, #256
	add	r2, r2, #2
	bne	.L902
	b	.L890
.L899:
	cmp	r0, #4
	mov	r2, #0
	bne	.L903
	ldr	r3, .L927+8
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
	ldr	r0, .L927+12
	strh	r1, [r3, r2]	@ movhi
	mov	r2, #2240
	mov	r1, #7
	strh	r1, [r3, r2]	@ movhi
	add	r2, r2, #2
	mov	r1, #8
	strh	r1, [r3, r2]	@ movhi
	mov	r2, #16
	mov	r3, r1
.L905:
	tst	r3, #1
	add	r3, r3, #1
	moveq	r1, #6
	movne	r1, #7
	cmp	r3, #256
	rsb	r1, r1, r2
	add	r2, r2, #2
	strh	r1, [r0, #2]!	@ movhi
	uxth	r2, r2
	bne	.L905
	b	.L890
.L903:
	cmp	r0, #5
	bne	.L906
	ldr	r1, .L927
	mov	r3, r2
.L907:
	strh	r3, [r1, #2]!	@ movhi
	add	r3, r3, #1
	uxth	r3, r3
	cmp	r3, #16
	bne	.L907
	ldr	r2, .L927+16
.L908:
	strh	r3, [r2, #2]!	@ movhi
	add	r3, r3, #2
	uxth	r3, r3
	cmp	r3, #496
	bne	.L908
	b	.L890
.L906:
	cmp	r0, #6
	bne	.L890
	ldr	r0, .L927
	mov	r3, r2
.L911:
	uxth	r1, r3
	cmp	r1, #5
	bls	.L909
	tst	r3, #1
	moveq	r1, #10
	movne	r1, #12
	rsb	r1, r1, r2
	uxth	r1, r1
.L909:
	add	r3, r3, #1
	add	r2, r2, #3
	cmp	r3, #256
	strh	r1, [r0, #2]!	@ movhi
	uxth	r2, r2
	bne	.L911
.L890:
	mov	r2, #1024
	ldr	r0, .L927+20
	mov	r1, #255
	uxth	r4, r4
	bl	ftl_memset
	ldr	r2, .L927
	mov	r3, #0
	ldr	r0, .L927+24
	b	.L912
.L913:
	ldrh	r1, [r2, #2]!
	add	r3, r3, #1
	uxth	r3, r3
	add	ip, r0, r1, asl #1
	sub	ip, ip, #912
	strh	r1, [ip, #0]	@ movhi
.L912:
	cmp	r3, r4
	bcc	.L913
	ldmfd	sp!, {r4, pc}
.L928:
	.align	2
.L927:
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
	ldr	r4, .L934
	mov	ip, r0
	mov	r2, r3
	ldr	r4, [r4, #112]
	add	r4, r4, #4096
	add	r1, r4, r1
	movne	r0, r1
	movne	r1, ip
	ldmfd	sp!, {r4, lr}
	b	memcpy
.L935:
	.align	2
.L934:
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
	ldr	r3, .L941
	ldr	r0, [r3, #3952]
	bx	lr
.L942:
	.align	2
.L941:
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
	b	.L944
.L950:
	cmp	r8, #0
	bne	.L945
	ldr	r0, .L952
	mov	r1, r6
	mov	r2, r7
	bl	printk
.L945:
	cmp	r5, #4
	ldreq	r0, .L952+4
	ldreq	r1, [r4, r7, asl #2]
	beq	.L951
	cmp	r5, #2
	moveq	r3, r7, asl #1
	ldreq	r0, .L952+4
	ldreqsh	r1, [r4, r3]
	ldrne	r0, .L952+4
	ldrneb	r1, [r4, r7]	@ zero_extendqisi2
.L951:
	add	r8, r8, #1
	bl	printk
	cmp	r8, #15
	bls	.L949
	ldr	r0, .L952+8
	mov	r8, #0
	ldr	r1, .L952+12
	bl	printk
.L949:
	add	r7, r7, #1
.L944:
	cmp	r7, sl
	bne	.L950
	ldr	r0, .L952+8
	ldr	r1, .L952+12
	ldmfd	sp!, {r3, r4, r5, r6, r7, r8, sl, lr}
	b	printk
.L953:
	.align	2
.L952:
	.word	.LC82
	.word	.LC83
	.word	.LC75
	.word	.LC84
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
	ldr	r6, .L977
	add	r0, r6, r0, asl #3
	ldr	r3, [r6, #12]
	ldr	r4, [r0, #16]
	cmp	r3, #3
	bls	.L971
	ldr	r3, [r4, #16]
	tst	r3, #4
	beq	.L971
	mov	r0, r4
	bl	wait_for_nandc_xfer_completed
	ldr	r5, [r4, #16]
	ldr	r3, [r4, #8]
	ands	r5, r5, #2
	str	r3, [sp, #0]
	movne	r5, #0
	bne	.L975
	b	.L976
.L961:
	ldr	r3, [r6, #12]
	cmp	r3, #5
	bls	.L958
	ldr	r3, [r4, #0]
	str	r3, [sp, #4]
	ldr	r3, [sp, #4]
	tst	r3, #8192
	beq	.L958
	ldr	r3, [sp, #4]
	tst	r3, #131072
	bne	.L959
.L958:
	add	r5, r5, #1
	bic	r3, r5, #-16777216
	cmp	r3, #0
	bne	.L975
	ldr	r2, [r4, #28]
	mov	r1, r5
	ldr	r3, [sp, #0]
	ubfx	r2, r2, #16, #5
	ldr	r0, .L977+4
	ubfx	r3, r3, #22, #6
	bl	printk
	ldr	r0, .L977+8
	mov	r1, r4
	mov	r2, #4
	mov	r3, #512
	bl	rknand_print_hex
.L975:
	ldr	r2, [r4, #28]
	ldr	r3, [sp, #0]
	ubfx	r2, r2, #16, #5
	ubfx	r3, r3, #22, #6
	cmp	r2, r3
	blt	.L961
.L959:
	ldr	r4, .L977
	ldr	r3, [r4, #3820]
	cmp	r3, #0
	beq	.L962
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
	b	.L962
.L965:
	ldr	r3, [r4, #8]
	add	r5, r5, #1
	str	r3, [sp, #0]
	bic	r3, r5, #-16777216
	cmp	r3, #0
	bne	.L976
	ldr	r2, [sp, #0]
	mov	r1, r5
	ldr	r3, [r4, #28]
	ldr	r0, .L977+12
	ubfx	r3, r3, #16, #5
	bl	printk
	ldr	r0, .L977+8
	mov	r1, r4
	mov	r2, #4
	mov	r3, #512
	bl	rknand_print_hex
.L976:
	ldr	r3, [sp, #0]
	tst	r3, #1048576
	beq	.L965
	ldr	r6, .L977
	ldr	r3, [r6, #3828]
	cmp	r3, #0
	beq	.L966
	mov	r0, r4
	bl	NandcSendDumpDataStart
.L966:
	ldr	r3, [r6, #3820]
	ldr	r5, .L977
	cmp	r3, #0
	beq	.L967
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
.L967:
	ldr	r3, .L977
	ldr	r3, [r3, #3828]
	cmp	r3, #0
	beq	.L962
	mov	r0, r4
	bl	NandcSendDumpDataDone
.L962:
	ldr	r3, .L977
	mov	r2, #0
	str	r2, [r3, #3820]
	b	.L954
.L971:
	ldr	r3, [r4, #8]
	str	r3, [sp, #0]
	ldr	r3, [sp, #0]
	tst	r3, #1048576
	beq	.L971
.L954:
	ldmfd	sp!, {r2, r3, r4, r5, r6, pc}
.L978:
	.align	2
.L977:
	.word	.LANCHOR0
	.word	.LC85
	.word	.LC86
	.word	.LC87
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
	ldr	r3, .L1017
	.pad #92
	sub	sp, sp, #92
	tst	r4, #63
	add	r3, r3, r0, asl #3
	mov	r7, r0
	mov	fp, r1
	mov	sl, r2
	ldr	r5, [sp, #128]
	ldr	r6, [r3, #16]
	bne	.L980
	cmp	r5, #0
	bne	.L981
	add	r0, sp, #24
	mov	r1, #255
	mov	r2, #64
	add	r5, sp, #24
	bl	ftl_memset
.L981:
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
	bne	.L982
	ldr	r1, .L1017
	mov	r0, sl, lsr #1
	mov	r3, fp
	mov	r2, fp
	ldr	ip, [r1, #3832]
	mov	r4, r1
	cmp	ip, #24
	movhi	ip, #128
	movls	ip, #64
	b	.L984
.L985:
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
.L984:
	cmp	r2, r0
	add	r7, r3, ip
	ldr	r1, .L1017
	bcc	.L985
	mov	r3, #0
	ldr	r0, [r1, #3832]
	mov	sl, sl, lsr #2
	ldr	r1, [r1, #12]
	mov	r4, r3
	b	.L986
.L992:
	add	r2, r3, #8
	ldr	r2, [r6, r2, asl #2]
	str	r2, [sp, #20]
	ldr	r2, [sp, #20]
	tst	r2, #4
	bne	.L1007
	ldr	r2, [sp, #20]
	ands	r2, r2, #32768
	bne	.L1007
	cmp	r1, #5
	bls	.L988
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
	b	.L1015
.L988:
	cmp	r1, #3
	bls	.L990
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
.L1015:
	orr	r2, ip, r2, asl #5
.L990:
	cmp	r4, r2
	movcc	r4, r2
	b	.L987
.L1007:
	mvn	r4, #0
.L987:
	add	r3, r3, #1
.L986:
	cmp	r3, sl
	bcs	.L982
	cmp	r0, #0
	bne	.L992
.L982:
	mov	r3, #0
	str	r3, [r6, #16]
	b	.L993
.L980:
	cmp	r1, #1
	mov	r8, #0
	bne	.L1013
	b	.L994
.L997:
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
.L994:
	cmp	r8, sl
	bcc	.L997
	mov	r4, #0
	b	.L993
.L1013:
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
	b	.L1016
.L1002:
	mov	r0, r7
	bl	NandcXferComp
	ldr	r3, [fp, #32]
	add	ip, r5, #2
	cmp	ip, sl
	str	r3, [sp, #20]
	bcs	.L999
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
.L999:
	ldr	r3, [sp, #20]
	tst	r3, #4
	mvnne	r4, #0
	bne	.L1000
	ldr	r2, [sp, #20]
	ldr	r3, [sp, #20]
	ubfx	r2, r2, #3, #5
	ubfx	r3, r3, #27, #1
	orr	r3, r2, r3, asl #5
	cmp	r4, r3
	movcc	r4, r3
.L1000:
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
.L1016:
	cmp	ip, sl
	mov	r5, ip
	bcc	.L1002
	mov	r6, fp
	ldr	fp, [sp, #12]
.L993:
	ldr	r3, .L1017
	rsbs	fp, fp, #1
	movcc	fp, #0
	ldr	r3, [r3, #12]
	cmp	r3, #5
	movls	fp, #0
	cmp	fp, #0
	beq	.L1003
	ldr	r3, [r6, #0]
	and	r2, r3, #139264
	cmp	r2, #139264
	orreq	r3, r3, #131072
	streq	r3, [r6, #0]
	mvneq	r4, #0
.L1003:
	mov	r0, r4
	add	sp, sp, #92
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, sl, fp, pc}
.L1018:
	.align	2
.L1017:
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
	ldr	r3, .L1022
	subs	r4, r0, #0
	mov	r5, r1
	mov	r6, r2
	ldrb	r7, [r3, #2697]	@ zero_extendqisi2
	bne	.L1020
	ldr	r3, .L1022+4
	ldrb	r2, [r3, #1]	@ zero_extendqisi2
	ldr	r1, [r3, #4]
	mul	r2, r1, r2
	cmp	r5, r2
	bcs	.L1020
	ldrb	r3, [r3, #0]	@ zero_extendqisi2
	cmp	r3, #0
	subeq	r7, r7, #2
	movne	r7, #4
.L1020:
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
.L1023:
	.align	2
.L1022:
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
	ldr	r2, .L1035
	.pad #28
	sub	sp, sp, #28
	mov	r6, r0
	str	r1, [sp, #12]
	ldrb	r1, [r2, #80]	@ zero_extendqisi2
	ldr	r3, [r2, #2776]
	cmp	r1, #0
	ldrb	r3, [r3, #19]	@ zero_extendqisi2
	beq	.L1025
	ldr	r1, [r2, #3788]
	ldr	r2, .L1035+4
	cmp	r1, r2
	beq	.L1024
.L1025:
	sub	r2, r3, #5
	uxtb	r2, r2
	cmp	r3, #68
	cmpne	r2, #1
	bls	.L1028
	cmp	r3, #35
	cmpne	r3, #17
	beq	.L1028
	cmp	r3, #18
	cmpne	r3, #19
	beq	.L1028
	b	.L1024
.L1033:
	add	r3, sl, r8, asl #1
	sub	r3, r3, #912
	ldrh	r2, [r3, #0]
	movw	r3, #65535
	cmp	r2, r3
	bne	.L1024
	mov	r1, #255
	ldr	r0, [sl, #116]
	mov	r2, #32768
	bl	ftl_memset
	ldr	r1, [sp, #20]
	cmp	r1, #1
	bhi	.L1029
	ldr	r3, .L1035+8
	ldr	r2, [r3, #2964]
	cmp	r2, #0
	beq	.L1030
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
	bne	.L1031
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
.L1032:
	strne	fp, [r3, #2964]
	bne	.L1024
.L1031:
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
	b	.L1030
.L1029:
	ldr	r2, [sp, #12]
	mov	r0, r6
	mov	r3, #0
	add	r1, r8, r2
	ldr	r2, [sl, #116]
	bl	FlashProgPage
.L1030:
	add	r8, r8, #1
	uxth	r8, r8
	b	.L1034
.L1028:
	ldr	sl, .L1035+12
	sub	r3, r3, #17
	ldr	r9, .L1035
	uxtb	r3, r3
	str	r3, [sp, #20]
	add	r3, r9, r6, asl #3
	str	r3, [sp, #16]
.L1034:
	ldr	r3, [r9, #2776]
	ldrh	r3, [r3, #10]
	cmp	r3, r8
	bhi	.L1033
.L1024:
	add	sp, sp, #28
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, sl, fp, pc}
.L1036:
	.align	2
.L1035:
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
	ldr	r3, .L1040
	subs	r4, r0, #0
	mov	r6, r1
	mov	r5, r2
	ldrb	r7, [r3, #2697]	@ zero_extendqisi2
	bne	.L1038
	ldr	r3, .L1040+4
	ldrb	r2, [r3, #1]	@ zero_extendqisi2
	ldr	r3, [r3, #4]
	mul	r3, r3, r2
	cmp	r1, r3
	movcc	r7, #4
.L1038:
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
.L1041:
	.align	2
.L1040:
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
	ldr	fp, .L1053
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
	b	.L1043
.L1048:
	add	r5, r5, #1
	ldrb	r1, [fp, #2909]	@ zero_extendqisi2
	mov	r0, r6
	ldr	r2, .L1053+4
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
	beq	.L1045
	ldrb	r3, [ip, #3836]	@ zero_extendqisi2
	cmn	r4, #1
	moveq	r4, r0
	add	r3, r3, r3, asl #1
	cmp	r0, r3, lsr #2
	bcc	.L1052
	mov	r7, #0
	mov	sl, r7
.L1045:
	add	r8, r8, #1
.L1043:
	cmp	r8, r9
	bcc	.L1048
	b	.L1047
.L1052:
	mov	r4, r0
.L1047:
	ldr	r3, .L1053
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
.L1054:
	.align	2
.L1053:
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
	ldr	r3, .L1067
	ldr	ip, .L1067+4
	add	r2, r3, r1, asl #3
	ldrb	r7, [r2, #20]	@ zero_extendqisi2
	ldr	r6, [r2, #16]
	add	r7, r7, #8
	mov	r8, r6
	add	r4, r6, r7, asl #8
	b	.L1066
.L1060:
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
	beq	.L1066
	ldr	r1, .L1067
	cmn	r5, #1
	moveq	r5, r0
	ldrb	r3, [r1, #3836]	@ zero_extendqisi2
	add	r3, r3, r3, asl #1
	cmp	r0, r3, lsr #2
	bcc	.L1063
	mov	r9, r6
	mov	fp, r6
.L1066:
	ldrb	r3, [ip, #120]	@ zero_extendqisi2
	cmp	sl, r3
	bcc	.L1060
	mov	r6, r8
	b	.L1059
.L1063:
	mov	r6, r8
	mov	r5, r0
.L1059:
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
	ldr	r3, .L1067
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
.L1068:
	.align	2
.L1067:
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
	ldr	ip, .L1078
	mov	r5, #1
	mvn	r4, #0
	ldr	fp, .L1078+4
	add	r3, ip, r8, asl #3
	ldrb	sl, [r3, #20]	@ zero_extendqisi2
	ldr	r2, [r3, #16]
	add	sl, sl, #8
	add	sl, r2, sl, asl #8
	b	.L1070
.L1074:
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
	beq	.L1071
	ldrb	r3, [ip, #3836]	@ zero_extendqisi2
	cmn	r4, #1
	moveq	r4, r0
	add	r3, r3, r3, asl #1
	cmp	r0, r3, lsr #2
	bcc	.L1077
	mov	r6, #0
	mov	r7, r6
.L1071:
	add	r5, r5, #1
.L1070:
	ldrb	r3, [fp, #120]	@ zero_extendqisi2
	add	r3, r3, #1
	cmp	r5, r3
	bcc	.L1074
	b	.L1073
.L1077:
	mov	r4, r0
.L1073:
	mov	r0, sl
	mov	r1, #0
	bl	SamsungSetRRPara
	ldr	r3, .L1078
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
.L1079:
	.align	2
.L1078:
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
	ldr	r3, .L1101
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
	bls	.L1081
	ldrb	sl, [r3, #3777]	@ zero_extendqisi2
	cmp	sl, #0
	beq	.L1082
	mov	r0, #0
	mov	sl, #1
	bl	NandcSetDdrMode
.L1082:
	mov	r3, #92
	str	r3, [r4, #8]
	mov	r3, #197
	str	r3, [r4, #8]
.L1081:
	mov	r6, #1
	mvn	r5, #0
	ldr	fp, .L1101
	b	.L1083
.L1092:
	ldrb	r3, [fp, #3765]	@ zero_extendqisi2
	mov	r0, r4
	uxtb	r1, r6
	sub	r3, r3, #67
	uxtb	r3, r3
	cmp	r3, #1
	bhi	.L1084
	bl	SandiskSetRRPara
	b	.L1085
.L1084:
	bl	ToshibaSetRRPara
.L1085:
	ldr	r2, .L1101
	ldrb	r3, [r2, #3765]	@ zero_extendqisi2
	cmp	r3, #34
	bne	.L1086
	ldr	r2, .L1101+4
	ldrb	r3, [r2, #120]	@ zero_extendqisi2
	sub	r3, r3, #3
	cmp	r6, r3
	moveq	r3, #179
	streq	r3, [r4, #8]
.L1086:
	cmp	sl, #0
	mov	r3, #38
	str	r3, [r4, #8]
	mov	r3, #93
	str	r3, [r4, #8]
	beq	.L1087
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
	b	.L1088
.L1087:
	mov	r3, r7
	mov	r0, r8
	ldr	r1, [sp, #4]
	mov	r2, r9
	bl	FlashReadRawPage
	mov	r3, r0
.L1088:
	cmn	r3, #1
	beq	.L1089
	ldrb	r2, [fp, #3836]	@ zero_extendqisi2
	cmn	r5, #1
	moveq	r5, r3
	add	r2, r2, r2, asl #1
	cmp	r3, r2, lsr #2
	bcc	.L1100
	mov	r7, #0
	mov	r9, r7
.L1089:
	add	r6, r6, #1
.L1083:
	ldr	r2, .L1101+4
	ldrb	r3, [r2, #120]	@ zero_extendqisi2
	add	r3, r3, #1
	cmp	r6, r3
	bcc	.L1092
	b	.L1091
.L1100:
	mov	r5, r3
.L1091:
	ldr	r6, .L1101
	mov	r0, r4
	mov	r1, #0
	ldrb	r3, [r6, #3765]	@ zero_extendqisi2
	sub	r3, r3, #67
	uxtb	r3, r3
	cmp	r3, #1
	bhi	.L1093
	bl	SandiskSetRRPara
	b	.L1094
.L1093:
	bl	ToshibaSetRRPara
.L1094:
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
	beq	.L1096
	mov	r0, #4
	bl	NandcSetDdrMode
.L1096:
	mov	r0, r5
	ldmfd	sp!, {r1, r2, r3, r4, r5, r6, r7, r8, r9, sl, fp, pc}
.L1102:
	.align	2
.L1101:
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
	ldr	r5, .L1110
	ldr	r7, .L1110+4
	ldr	r8, .L1110+8
	ldr	r3, [r5, #3780]
	ldrb	r0, [r7, #128]	@ zero_extendqisi2
	str	r3, [r7, #124]
	bl	FlashBchSel
	mov	r1, #0
	mov	r2, #2048
	ldr	r0, [r5, #3780]
	bl	ftl_memset
	ldr	r3, [r7, #124]
	ldr	r1, .L1110+12
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
	ldr	r1, .L1110+16
	mov	r2, #8
	bl	memcpy
	add	r0, r4, #96
	ldr	r1, .L1110+20
	mov	r2, #32
	bl	memcpy
	ldr	r0, [r7, #124]
	ldr	r1, .L1110+24
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
	ldr	r1, .L1110+28
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
.L1106:
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
	beq	.L1104
	ldr	sl, [r7, #124]
	ldr	r3, [sl, #0]
	cmp	r3, r8
	bne	.L1104
	add	r0, sl, #12
	movw	r1, #2036
	bl	JSHash
	ldr	r3, [sl, #8]
	cmp	r3, r0
	bne	.L1104
	add	r3, r4, #1
	str	r3, [r7, #132]
	ldr	r3, [r5, #4]
	add	r6, r6, #1
	cmp	r6, #1
	mul	r3, r3, r4
	str	r3, [r7, #136]
	bhi	.L1105
.L1104:
	add	r4, r4, #1
	cmp	r4, #4
	bne	.L1106
.L1105:
	mov	r0, #0
	bl	flash_exit_slc_mode
	cmp	r6, #0
	mvneq	r0, #0
	movne	r0, #0
	ldmfd	sp!, {r3, r4, r5, r6, r7, r8, sl, pc}
.L1111:
	.align	2
.L1110:
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
	ldr	r1, .L1123
	add	r0, sp, #4
	bl	memcpy
	ldr	r3, .L1123+4
	ldr	r2, [r3, #3788]
	ldrb	fp, [r3, #3836]	@ zero_extendqisi2
	ldr	r3, .L1123+8
	cmp	r2, r3
	bne	.L1113
	mov	r0, #0
	bl	flash_enter_slc_mode
.L1113:
	ldr	r7, .L1123+4
	mov	r0, sl
	mov	r1, #0
	mov	r2, #2048
	bl	ftl_memset
	mvn	r8, #0
	mov	r4, #2
	mov	r6, r7
	ldr	r9, .L1123+12
	b	.L1114
.L1121:
	mov	r5, #0
.L1116:
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
	bne	.L1115
	add	r5, r5, #1
	cmp	r5, #4
	bne	.L1116
	b	.L1117
.L1115:
	ldr	r3, [r6, #3780]
	ldr	r3, [r3, #0]
	cmp	r3, r9
	bne	.L1117
	add	r3, sp, #8
	ldr	r0, .L1123+16
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
	ldr	r3, .L1123+20
	ldr	r2, [r3, #132]
	cmp	r2, r4
	bls	.L1120
	str	r4, [r3, #132]
	mov	r8, #0
	bl	FlashSavePhyInfo
.L1117:
	add	r4, r4, #1
.L1114:
	ldrb	r3, [r7, #1]	@ zero_extendqisi2
	cmp	r4, r3
	bcc	.L1121
	b	.L1118
.L1120:
	mov	r8, #0
.L1118:
	mov	r0, fp
	bl	FlashBchSel
	ldr	r3, .L1123+4
	ldr	r2, [r3, #3788]
	ldr	r3, .L1123+8
	cmp	r2, r3
	bne	.L1119
	mov	r0, #0
	bl	flash_exit_slc_mode
.L1119:
	mov	r0, r8
	ldmfd	sp!, {r1, r2, r3, r4, r5, r6, r7, r8, r9, sl, fp, pc}
.L1124:
	.align	2
.L1123:
	.word	.LANCHOR3+11
	.word	.LANCHOR0
	.word	1446522928
	.word	-52655045
	.word	.LC88
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
	ldr	r1, .L1135
	add	r0, sp, #4
	bl	memcpy
	ldr	r7, .L1135+4
	ldr	r2, .L1135+8
	movw	r3, #2698
	ldr	r5, .L1135+12
	mov	r4, #0
	mov	r0, r4
	mov	r8, #4
	ldrh	fp, [r2, r3]
	mvn	sl, #0
	ldr	r3, [r7, #3780]
	str	r4, [r5, #136]
	str	r3, [r5, #124]
	bl	flash_enter_slc_mode
	b	.L1126
.L1128:
	add	r3, sp, #4
	ldrb	r0, [r3, r6]	@ zero_extendqisi2
	bl	FlashBchSel
	mov	r0, #0
	mov	r1, r4
	ldr	r2, [r7, #3780]
	mov	r3, r0
	bl	FlashReadRawPage
	cmn	r0, #1
	bne	.L1127
	mov	r0, #0
	mov	r1, r9
	ldr	r2, [r7, #3780]
	mov	r3, r0
	bl	FlashReadRawPage
	cmn	r0, #1
	bne	.L1127
	add	r6, r6, #1
	cmp	r6, #4
	beq	.L1129
	b	.L1128
.L1127:
	ldr	r6, [r5, #124]
	ldr	r3, .L1135+16
	ldr	r2, [r6, #0]
	cmp	r2, r3
	bne	.L1129
	cmp	sl, #0
	bne	.L1130
	ldr	r2, .L1135+8
	movw	r3, #2698
	mov	r0, r4
	ldrh	r1, [r2, r3]
	bl	__aeabi_uidiv
	ldr	r3, .L1135+12
	add	r0, r0, #1
	str	r0, [r3, #132]
	b	.L1131
.L1130:
	add	r0, r6, #12
	movw	r1, #2036
	bl	JSHash
	ldr	r3, [r6, #8]
	cmp	r3, r0
	bne	.L1129
	ldr	sl, .L1135+8
	add	r1, r6, #160
	mov	r2, #32
	add	r0, sl, #2688
	bl	memcpy
	add	r1, r6, #192
	mov	r2, #32
	ldr	r0, .L1135+20
	bl	memcpy
	add	r1, r6, #224
	mov	r2, #852
	ldr	r0, .L1135+24
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
.L1129:
	subs	r8, r8, #1
	add	r4, r4, fp
	beq	.L1133
.L1126:
	add	r9, r4, #1
	mov	r6, #0
	b	.L1128
.L1133:
	mov	r0, r8
	bl	flash_exit_slc_mode
.L1131:
	mov	r0, sl
	ldmfd	sp!, {r1, r2, r3, r4, r5, r6, r7, r8, r9, sl, fp, pc}
.L1136:
	.align	2
.L1135:
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
	ldr	r4, .L1158
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
	beq	.L1139
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
	beq	.L1140
	ldr	r0, .L1158+4
	mov	r2, r8
	ldr	r1, [sp, #0]
	bl	printk
	cmp	r8, #9
	bhi	.L1141
	add	r4, r4, fp, asl #3
	ldr	r3, [r4, #16]
	ldr	r2, [r3, #3840]
	ldr	r2, [r3, #0]
	orr	r2, r2, #131072
	str	r2, [r3, #0]
.L1141:
	ldr	r3, .L1158+8
	ldr	r2, [r3, #144]
	add	r2, r2, #1
	str	r2, [r3, #144]
	cmp	r2, #2048
	movcs	sl, #0
	strcs	sl, [r3, #144]
	movcs	r9, sl
	bcc	.L1140
.L1139:
	mov	r4, #0
	str	fp, [sp, #4]
	mov	r5, r4
	mvn	r6, #0
	mov	fp, r4
	str	r4, [sp, #8]
.L1146:
	uxtb	r0, r7
	bl	NandcSetDdrPara
	mov	r3, sl
	ldr	r0, [sp, #4]
	mov	r2, r9
	ldr	r1, [sp, #0]
	bl	FlashReadRawPage
	add	r3, r8, #1
	cmp	r0, r3
	bhi	.L1142
	cmp	r0, #2
	bhi	.L1152
	add	r5, r5, #1
	cmp	r5, #9
	bls	.L1152
	mov	ip, fp
	rsb	r4, r5, r7
	ldr	fp, [sp, #4]
	mov	r8, r0
	mov	r6, #0
	b	.L1144
.L1142:
	cmp	fp, r5
	bcs	.L1153
	cmp	r5, #7
	rsb	r3, r5, r4
	str	r3, [sp, #8]
	bhi	.L1157
	mov	fp, r5
	b	.L1153
.L1152:
	mov	r6, #0
	mov	r4, r7
	mov	r8, r0
	mov	sl, r6
	mov	r9, r6
	b	.L1143
.L1153:
	mov	r5, #0
.L1143:
	add	r7, r7, #2
	cmp	r7, #69
	bls	.L1146
	mov	ip, fp
	ldr	fp, [sp, #4]
.L1144:
	cmp	ip, r5
	bcc	.L1147
	b	.L1145
.L1157:
	ldr	fp, [sp, #4]
.L1145:
	ldr	r4, [sp, #8]
.L1147:
	cmp	r4, #0
	beq	.L1148
	ldr	r0, .L1158+12
	mov	r1, r4
	bl	printk
	uxtb	r0, r4
	bl	NandcSetDdrPara
.L1148:
	cmn	r6, #1
	bne	.L1140
	ldr	r0, .L1158+16
	mov	r1, fp
	ldr	r2, [sp, #0]
	bl	printk
	ldr	r3, [sp, #56]
	cmp	r3, #0
	moveq	r8, r6
	beq	.L1140
	ldr	r3, [sp, #12]
	ubfx	r0, r3, #8, #8
	bl	NandcSetDdrPara
.L1140:
	mov	r0, r8
	add	sp, sp, #20
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, sl, fp, pc}
.L1159:
	.align	2
.L1158:
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
	ldr	r5, .L1164
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
	beq	.L1161
	cmn	r8, #1
	bne	.L1162
.L1161:
	ldrb	r3, [r5, #3776]	@ zero_extendqisi2
	tst	r3, #1
	beq	.L1162
	mov	r0, #1
	bl	FlashSetInterfaceMode
	mov	r0, #1
	bl	NandcSetMode
	ldr	r3, .L1164
	mov	r2, #0
	strb	r2, [r3, #3777]
	b	.L1163
.L1162:
	mov	r3, #1
	strb	r3, [r5, #3777]
.L1163:
	mov	r0, #0
	ldmfd	sp!, {r2, r3, r4, r5, r6, r7, r8, pc}
.L1165:
	.align	2
.L1164:
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
	ldr	r6, .L1208
	bl	ftl_malloc
	ldr	r5, .L1208+4
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
	ldr	r5, .L1208+8
.L1169:
	uxtb	r0, r4
	mov	r1, r5
	bl	FlashReadIDRaw
	cmp	r4, #0
	bne	.L1167
	ldrb	r3, [r6, #2132]	@ zero_extendqisi2
	sub	r3, r3, #1
	uxtb	r3, r3
	cmp	r3, #253
	bhi	.L1201
	ldrb	r3, [r6, #2133]	@ zero_extendqisi2
	cmp	r3, #255
	beq	.L1202
.L1167:
	add	r4, r4, #1
	add	r5, r5, #8
	cmp	r4, #4
	bne	.L1169
	ldr	r4, .L1208
	ldrb	r3, [r4, #2132]	@ zero_extendqisi2
	cmp	r3, #173
	beq	.L1170
	ldr	r0, [r4, #3784]
	bl	NandcSetDdrMode
.L1170:
	mov	r1, #0
	ldr	r0, .L1208+12
	mov	r2, #852
	bl	ftl_memset
	ldr	r3, .L1208+16
	str	r3, [r4, #2776]
	mov	r3, #0
	strb	r3, [r4, #8]
	ldrb	r3, [r4, #2133]	@ zero_extendqisi2
	sub	r1, r3, #218
	rsbs	ip, r1, #0
	adc	ip, ip, r1
	cmp	r3, #218
	cmpne	r3, #241
	beq	.L1171
	cmp	r3, #220
	bne	.L1172
	ldr	r2, .L1208
	ldrb	r2, [r2, #2135]	@ zero_extendqisi2
	cmp	r2, #149
	bne	.L1172
.L1171:
	ldr	r2, .L1208
	mov	r1, #1
	mov	r0, #16
	strb	r1, [r2, #0]
	ldr	r1, .L1208+4
	strb	r0, [r2, #1]
	strb	r0, [r1, #128]
	ldrb	r0, [r2, #2132]	@ zero_extendqisi2
	ldr	r2, .L1208+20
	cmp	r0, #152
	strb	r0, [r2, #2969]
	moveq	r0, #24
	streqb	r0, [r1, #128]
	cmp	ip, #0
	movwne	r3, #2982
	movne	r1, #2048
	strneh	r1, [r2, r3]	@ movhi
	mvnne	r3, #37
	bne	.L1205
.L1174:
	cmp	r3, #220
	bne	.L1175
	movw	r3, #2982
	mov	r1, #4096
	strh	r1, [r2, r3]	@ movhi
	mvn	r3, #35
.L1205:
	strb	r3, [r2, #2970]
.L1175:
	ldr	r1, .L1208+24
	mov	r2, #32
	ldr	r0, .L1208+28
	bl	memcpy
	ldr	r0, .L1208+16
	mov	r2, #32
	add	r1, r0, #280
	bl	memcpy
.L1172:
	ldr	r4, .L1208
	ldrb	r3, [r4, #0]	@ zero_extendqisi2
	cmp	r3, #0
	bne	.L1176
	bl	FlashLoadPhyInfoInRam
	cmp	r0, #0
	bne	.L1177
	ldr	r3, [r4, #2776]
	ldrh	r0, [r3, #16]
	ubfx	r0, r0, #8, #3
	strb	r0, [r4, #3776]
	tst	r0, #1
	bne	.L1177
	mov	r3, #1
	strb	r3, [r4, #3777]
	bl	FlashSetInterfaceMode
	ldrb	r0, [r4, #3776]	@ zero_extendqisi2
	bl	NandcSetMode
.L1177:
	ldr	r4, .L1208
	ldr	r3, [r4, #2776]
	ldrb	r3, [r3, #26]	@ zero_extendqisi2
	strb	r3, [r4, #80]
	bl	FlashLoadPhyInfo
	cmp	r0, #0
	beq	.L1176
	ldr	r3, [r4, #3784]
	cmp	r3, #0
	beq	.L1179
	mov	r0, #1
	bl	FlashSetInterfaceMode
	mov	r0, #1
	b	.L1206
.L1179:
	ldrb	r0, [r4, #3776]	@ zero_extendqisi2
	bl	FlashSetInterfaceMode
	ldrb	r0, [r4, #3776]	@ zero_extendqisi2
.L1206:
	bl	NandcSetMode
	bl	FlashLoadPhyInfo
	cmp	r0, #0
	beq	.L1176
	ldr	r4, .L1208
	mov	r0, #1
	bl	FlashSetInterfaceMode
	mov	r0, #1
	bl	NandcSetMode
	ldr	r0, .L1208+32
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
	ldr	r1, .L1208+36
	ldr	r0, .L1208+4
	ldr	r3, [r4, #2776]
	ldrh	r1, [r0, r1]
	ldrb	r2, [r3, #9]	@ zero_extendqisi2
	add	r1, r1, #4080
	add	r1, r1, #15
	cmp	r2, r1, lsr #12
	blt	.L1182
	ldrh	r1, [r3, #14]
	add	r1, r1, #255
	cmp	r2, r1, lsr #8
	bge	.L1183
.L1182:
	ldrh	r2, [r3, #14]
	bic	r2, r2, #255
	strh	r2, [r3, #14]	@ movhi
.L1183:
	ldr	r3, .L1208
	ldrb	r3, [r3, #3776]	@ zero_extendqisi2
	tst	r3, #6
	beq	.L1184
	bl	FlashSavePhyInfo
	ldr	r3, .L1208+4
	mov	r0, #0
	ldr	r1, [r3, #136]
	bl	FlashDdrParaScan
.L1184:
	bl	FlashSavePhyInfo
.L1176:
	ldr	r1, .L1208
	ldr	r3, [r1, #2776]
	ldrb	r2, [r3, #26]	@ zero_extendqisi2
	strb	r2, [r1, #80]
	ldrh	r2, [r3, #16]
	ubfx	r0, r2, #7, #1
	strb	r0, [r1, #8]
	ldr	r0, .L1208+4
	tst	r2, #64
	ubfx	ip, r2, #3, #1
	strb	ip, [r0, #160]
	ubfx	ip, r2, #4, #1
	strb	ip, [r1, #3837]
	ubfx	ip, r2, #8, #3
	strb	ip, [r1, #3776]
	mov	ip, #0
	str	ip, [r0, #164]
	beq	.L1185
	ldrb	ip, [r1, #2909]	@ zero_extendqisi2
	ldrb	r2, [r3, #19]	@ zero_extendqisi2
	strb	ip, [r1, #3764]
	ldrb	ip, [r1, #2910]	@ zero_extendqisi2
	strb	r2, [r1, #3765]
	strb	ip, [r0, #120]
	sub	ip, r2, #1
	uxtb	ip, ip
	cmp	ip, #5
	bhi	.L1186
	sub	r2, r2, #5
	ldr	ip, .L1208+40
	uxtb	r2, r2
	cmp	r2, #1
	str	ip, [r0, #164]
	movls	r2, #1
	strls	r2, [r1, #3828]
	b	.L1185
.L1186:
	sub	r1, r2, #17
	uxtb	r1, r1
	cmp	r1, #2
	bhi	.L1187
	ldr	r1, .L1208+44
	cmp	r2, #19
	moveq	r2, #15
	str	r1, [r0, #164]
	mov	r1, #7
	strb	r1, [r0, #120]
	streqb	r2, [r0, #120]
	b	.L1185
.L1187:
	cmp	r2, #33
	cmpne	r2, #65
	beq	.L1188
	cmp	r2, #66
	bne	.L1189
.L1188:
	ldr	r2, .L1208+4
	mov	r0, #4
	ldr	r1, .L1208+48
	str	r1, [r2, #164]
	ldr	r1, .L1208
	strb	r0, [r1, #3764]
	mov	r1, #7
	strb	r1, [r2, #120]
	b	.L1185
.L1189:
	cmp	r2, #67
	cmpne	r2, #34
	beq	.L1190
	cmp	r2, #35
	beq	.L1190
	cmp	r2, #68
	bne	.L1191
.L1190:
	ldr	r1, .L1208+4
	cmp	r2, #35
	cmpne	r2, #68
	ldr	r0, .L1208+48
	sub	r2, r2, #67
	uxtb	r2, r2
	str	r0, [r1, #164]
	mov	r0, #7
	strb	r0, [r1, #120]
	moveq	r0, #17
	streqb	r0, [r1, #120]
	cmp	r2, #1
	ldr	r2, .L1208
	movls	r1, #4
	movhi	r1, #5
	strb	r1, [r2, #3764]
	b	.L1185
.L1191:
	cmp	r2, #49
	ldreq	r2, .L1208+52
	streq	r2, [r0, #164]
.L1185:
	ldrb	r1, [r3, #12]	@ zero_extendqisi2
	ldrh	r0, [r3, #10]
	ldrb	r4, [r3, #18]	@ zero_extendqisi2
	bl	__aeabi_idiv
	mov	r1, r0
	mov	r0, r4
	bl	BuildFlashLsbPageTable
	bl	FlashDieInfoInit
	ldr	r3, .L1208
	ldrb	r2, [r3, #2132]	@ zero_extendqisi2
	cmp	r2, #44
	bne	.L1194
	ldrb	r2, [r3, #3777]	@ zero_extendqisi2
	cmp	r2, #0
	beq	.L1194
	mov	r2, #0
	mov	r0, #1
	strb	r2, [r3, #3777]
	bl	FlashSetInterfaceMode
	mov	r0, #1
	bl	NandcSetMode
.L1194:
	mov	r0, #0
	bl	flash_enter_slc_mode
	ldr	r2, .L1208
	ldrb	r3, [r2, #3776]	@ zero_extendqisi2
	tst	r3, #6
	beq	.L1195
	ldrb	r2, [r2, #3777]	@ zero_extendqisi2
	cmp	r2, #0
	bne	.L1196
	tst	r3, #1
	bne	.L1195
.L1196:
	ldr	r3, .L1208+4
	mov	r0, #0
	ldr	r1, [r3, #136]
	bl	FlashDdrParaScan
.L1195:
	ldr	r4, .L1208
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
	bne	.L1199
	ldr	r2, .L1208+4
	ldrb	r1, [r3, #1]	@ zero_extendqisi2
	ldrb	r2, [r2, #140]	@ zero_extendqisi2
	cmp	r1, r2
	strhib	r2, [r3, #1]
	ldrb	r3, [r3, #1]	@ zero_extendqisi2
	cmp	r3, #15
	ldrls	r3, .L1208
	movls	r2, #16
	strlsb	r2, [r3, #1]
.L1199:
	ldr	r4, .L1208
	movw	r6, #2214
	ldr	r0, .L1208+56
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
	bne	.L1200
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
.L1200:
	ldrb	r0, [r5, #20]	@ zero_extendqisi2
	bl	FlashBchSel
	bl	FlashSuspend
	mov	r0, #0
	ldmfd	sp!, {r3, r4, r5, r6, r7, pc}
.L1201:
	mvn	r0, #1
	ldmfd	sp!, {r3, r4, r5, r6, r7, pc}
.L1202:
	mvn	r0, #1
	ldmfd	sp!, {r3, r4, r5, r6, r7, pc}
.L1209:
	.align	2
.L1208:
	.word	.LANCHOR0
	.word	.LANCHOR2
	.word	.LANCHOR0+2132
	.word	.LANCHOR0+2908
	.word	.LANCHOR1+2688
	.word	.LANCHOR1
	.word	.LANCHOR1+2592
	.word	.LANCHOR0+48
	.word	.LC92
	.word	-914
	.word	HynixReadRetrial
	.word	MicronReadRetrial
	.word	ToshibaReadRetrial
	.word	SamsungReadRetrial
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
	bne	.L1211
	ldr	r7, .L1216
	ldrb	r9, [r7, #8]	@ zero_extendqisi2
	cmp	r9, #0
	beq	.L1212
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
	bne	.L1211
.L1212:
	ldr	r7, .L1216
	ldrb	r3, [r7, #3777]	@ zero_extendqisi2
	cmp	r3, #0
	beq	.L1211
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
	beq	.L1213
	ldrb	r3, [r7, #3836]	@ zero_extendqisi2
	cmp	r0, r3, lsr #1
	bls	.L1211
.L1213:
	ubfx	r0, r9, #8, #8
	bl	NandcSetDdrPara
.L1211:
	ldr	r3, .L1216+4
	ldr	ip, [r3, #164]
	adds	r3, ip, #0
	movne	r3, #1
	cmn	r4, #1
	movne	r3, #0
	cmp	r3, #0
	beq	.L1214
	mov	r1, r5
	mov	r2, r8
	mov	r3, sl
	mov	r0, r6
	blx	ip
	mov	r2, r6
	mov	r3, r5
	mov	r4, r0
	ldr	r0, .L1216+8
	mov	r1, r4
	bl	printk
.L1214:
	mov	r0, r4
	ldmfd	sp!, {r2, r3, r4, r5, r6, r7, r8, r9, sl, pc}
.L1217:
	.align	2
.L1216:
	.word	.LANCHOR0
	.word	.LANCHOR2
	.word	.LC93
	.fnend
	.size	FlashReadPage, .-FlashReadPage
	.align	2
	.global	FlashReadSlc2KPages
	.type	FlashReadSlc2KPages, %function
FlashReadSlc2KPages:
	.fnstart
	@ args = 0, pretend = 0, frame = 16
	@ frame_needed = 0, uses_anonymous_args = 0
	ldr	r3, .L1231
	stmfd	sp!, {r4, r5, r6, r7, r8, r9, sl, fp, lr}
	.save {r4, r5, r6, r7, r8, r9, sl, fp, lr}
	mov	r8, r1
	.pad #28
	sub	sp, sp, #28
	mov	fp, r2
	ldrb	sl, [r3, #2697]	@ zero_extendqisi2
	mov	r4, r0
	mov	r7, #0
	ldr	r6, .L1231+4
	b	.L1219
.L1228:
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
	bcs	.L1221
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
	beq	.L1226
	ldr	r2, [r3, #8]
	cmn	r2, #1
	bne	.L1226
	ldr	r3, [r3, #0]
	cmn	r3, #1
	strne	r2, [r4, #0]
.L1226:
	ldr	r3, [r4, #0]
	cmn	r3, #1
	bne	.L1221
	ldr	r1, [r4, #4]
	ldr	r0, .L1231+8
	ldrb	r2, [r6, #3836]	@ zero_extendqisi2
	bl	printk
	ldr	r1, [r4, #8]
	cmp	r1, #0
	beq	.L1227
	ldr	r0, .L1231+12
	mov	r2, #4
	mov	r3, #8
	bl	rknand_print_hex
.L1227:
	ldr	r1, [r4, #12]
	cmp	r1, #0
	beq	.L1221
	mov	r2, #4
	ldr	r0, .L1231+16
	mov	r3, r2
	bl	rknand_print_hex
.L1221:
	add	r7, r7, #1
	add	r4, r4, #36
.L1219:
	cmp	r7, r8
	bne	.L1228
	mov	r0, #0
	add	sp, sp, #28
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, sl, fp, pc}
.L1232:
	.align	2
.L1231:
	.word	.LANCHOR1
	.word	.LANCHOR0
	.word	.LC94
	.word	.LC95
	.word	.LC96
	.fnend
	.size	FlashReadSlc2KPages, .-FlashReadSlc2KPages
	.align	2
	.global	FlashReadPages
	.type	FlashReadPages, %function
FlashReadPages:
	.fnstart
	@ args = 0, pretend = 0, frame = 40
	@ frame_needed = 0, uses_anonymous_args = 0
	ldr	r3, .L1269
	stmfd	sp!, {r4, r5, r6, r7, r8, r9, sl, fp, lr}
	.save {r4, r5, r6, r7, r8, r9, sl, fp, lr}
	.pad #52
	sub	sp, sp, #52
	ldrb	r3, [r3, #2697]	@ zero_extendqisi2
	mov	r9, r0
	str	r1, [sp, #28]
	str	r2, [sp, #32]
	str	r3, [sp, #20]
	ldr	r3, .L1269+4
	ldrb	r8, [r3, #0]	@ zero_extendqisi2
	ldrb	ip, [r3, #8]	@ zero_extendqisi2
	cmp	r8, #0
	str	ip, [sp, #36]
	moveq	sl, r8
	beq	.L1234
	bl	FlashReadSlc2KPages
	b	.L1235
.L1258:
	mov	r3, #36
	ldr	ip, [sp, #28]
	mul	r3, r3, sl
	add	r2, sp, #44
	ldr	r1, [sp, #32]
	ldr	r5, .L1269+4
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
	bcs	.L1237
	add	r3, r5, r3
	ldrb	r4, [r3, #3768]	@ zero_extendqisi2
	ldr	r3, .L1269+8
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
	bhi	.L1239
	add	r2, r5, r4
	ldrb	r3, [r2, #2920]	@ zero_extendqisi2
	ldrb	r2, [r2, #3760]	@ zero_extendqisi2
	cmp	r2, r3
	beq	.L1239
	mov	r0, r4
	ldrb	r1, [r5, #2909]	@ zero_extendqisi2
	add	r2, r5, #2912
	bl	HynixSetRRPara
.L1239:
	mov	r0, r4
	mov	fp, fp, lsr #31
	bl	NandcFlashCs
	ldr	ip, [sp, #32]
	cmp	ip, #1
	orreq	fp, fp, #1
	str	fp, [sp, #24]
	cmp	fp, #0
	beq	.L1245
	ldr	r3, .L1269+4
	ldrb	r3, [r3, #80]	@ zero_extendqisi2
	cmp	r3, #0
	beq	.L1245
	mov	r0, r4
	bl	flash_enter_slc_mode
.L1245:
	ldr	fp, .L1269+4
.L1268:
	ldr	r1, [sp, #44]
	cmn	r1, #1
	cmpeq	r4, #255
	moveq	r3, #0
	movne	r3, #1
	moveq	r7, r3
	beq	.L1241
	cmp	r7, #0
	beq	.L1242
	ldr	r2, [fp, #4]
	mov	r0, r4
	add	r2, r1, r2
	bl	FlashReadDpCmd
	b	.L1243
.L1242:
	mov	r0, r4
	bl	FlashReadCmd
.L1243:
	mov	r0, r4
	bl	NandcWaitFlashReady
	cmp	r7, #0
	beq	.L1241
	mov	r0, r4
	ldr	r1, [sp, #44]
	bl	FlashReadDpDataOutCmd
.L1241:
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
	bne	.L1268
.L1244:
	cmp	r7, #0
	mov	r5, r0
	beq	.L1246
	ldr	r3, .L1269+4
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
.L1246:
	mov	r0, r4
	ldr	fp, .L1269+4
	bl	NandcFlashDeCs
	ldr	ip, [sp, #36]
	cmn	r5, #1
	strb	ip, [fp, #8]
	bne	.L1250
	ldrb	r3, [fp, #3777]	@ zero_extendqisi2
	cmp	r3, #0
	beq	.L1248
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
	beq	.L1249
	ldrb	r3, [fp, #3836]	@ zero_extendqisi2
	cmp	r0, r3, lsr #1
	bls	.L1264
.L1249:
	ubfx	r0, r7, #8, #8
	bl	NandcSetDdrPara
	cmn	r5, #1
	bne	.L1264
.L1248:
	ldr	r3, .L1269+8
	mov	r0, r4
	ldr	r1, [sp, #44]
	ldr	r2, [r6, #8]
	ldr	r7, [r3, #164]
	ldr	r3, [r6, #12]
	cmp	r7, #0
	beq	.L1251
	blx	r7
	cmn	r0, #1
	mov	r5, r0
	bne	.L1265
	ldr	r2, .L1269+4
	ldr	r3, [r2, #2776]
	ldrb	r3, [r3, #19]	@ zero_extendqisi2
	sub	r3, r3, #1
	uxtb	r3, r3
	cmp	r3, #5
	bhi	.L1253
	ldrb	r1, [r2, #2909]	@ zero_extendqisi2
	mov	r0, r4
	add	r2, r2, #2912
	mov	r3, #0
	bl	HynixSetRRPara
.L1253:
	ldr	r1, [sp, #44]
	mov	r0, r4
	ldr	r2, [r6, #8]
	ldr	r3, [r6, #12]
	bl	FlashReadRawPage
	ldr	r3, .L1269+4
	ldr	r1, [r6, #4]
	ldrb	r2, [r3, #3836]	@ zero_extendqisi2
	mov	r5, r0
	ldr	r0, .L1269+12
	mov	r3, r5
	bl	printk
	b	.L1265
.L1251:
	bl	FlashReadRawPage
	mov	r5, r0
	b	.L1252
.L1264:
	mov	r7, #0
.L1250:
	ldr	r3, .L1269+4
	ldrb	r3, [r3, #3836]	@ zero_extendqisi2
	add	r3, r3, r3, asl #1
	cmp	r5, r3, lsr #2
	bls	.L1252
	ldr	r3, .L1269+8
	ldr	r3, [r3, #164]
	cmp	r3, #0
	moveq	r5, #256
	b	.L1252
.L1265:
	mov	r7, #0
.L1252:
	ldr	ip, [sp, #16]
	cmp	r5, #256
	cmnne	r5, #1
	movne	r2, #0
	str	r5, [r9, ip]
	strne	r2, [r9, ip]
	ldr	ip, [sp, #16]
	ldr	r3, [r9, ip]
	cmn	r3, #1
	bne	.L1255
	ldr	r2, .L1269+4
	ldr	r1, [r6, #4]
	ldr	r0, .L1269+16
	ldrb	r2, [r2, #3836]	@ zero_extendqisi2
	bl	printk
	ldr	r1, [r6, #12]
	cmp	r1, #0
	beq	.L1255
	mov	r2, #4
	ldr	r0, .L1269+20
	mov	r3, r2
	bl	rknand_print_hex
.L1255:
	cmp	r7, #0
	beq	.L1256
	ldr	r3, .L1269+4
	ldrb	r3, [r3, #3836]	@ zero_extendqisi2
	add	r3, r3, r3, asl #1
	cmp	r8, r3, lsr #2
	bls	.L1257
	ldr	r3, .L1269+8
	ldr	r3, [r3, #164]
	cmp	r3, #0
	moveq	r8, #256
.L1257:
	add	r3, sl, #1
	mov	r2, #36
	cmp	r8, #256
	cmnne	r8, #1
	mul	r3, r2, r3
	movne	r2, #0
	str	r8, [r9, r3]
	strne	r2, [r9, r3]
.L1256:
	ldr	ip, [sp, #24]
	add	sl, sl, r7
	cmp	ip, #0
	beq	.L1237
	ldr	r3, .L1269+4
	ldrb	r3, [r3, #80]	@ zero_extendqisi2
	cmp	r3, #0
	beq	.L1237
	mov	r0, r4
	bl	flash_exit_slc_mode
.L1237:
	add	sl, sl, #1
.L1234:
	ldr	ip, [sp, #28]
	cmp	sl, ip
	bcc	.L1258
	mov	r0, #0
.L1235:
	add	sp, sp, #52
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, sl, fp, pc}
.L1270:
	.align	2
.L1269:
	.word	.LANCHOR1
	.word	.LANCHOR0
	.word	.LANCHOR2
	.word	.LC97
	.word	.LC94
	.word	.LC96
	.fnend
	.size	FlashReadPages, .-FlashReadPages
	.align	2
	.global	FtlGcScanTempBlk
	.type	FtlGcScanTempBlk, %function
FtlGcScanTempBlk:
	.fnstart
	@ args = 0, pretend = 0, frame = 16
	@ frame_needed = 0, uses_anonymous_args = 0
	ldr	r2, .L1297
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
	beq	.L1290
	cmp	r4, #0
	bne	.L1272
	b	.L1273
.L1290:
	mov	r4, #0
.L1272:
	ldr	r2, .L1297+4
	movw	r3, #3912
	ldr	fp, [sp, #12]
	ldrh	r3, [r2, r3]
	cmp	fp, r3
	bne	.L1274
.L1273:
	bl	FtlGcPageVarInit
.L1274:
	mvn	r8, #0
	mov	r7, #0
	movw	sl, #65535
.L1285:
	ldrh	r3, [r5, #0]
	mov	r0, #0
	strb	r0, [r5, #8]
	cmp	r3, sl
	beq	.L1291
.L1276:
.L1294:
	ldr	r3, .L1297+4
	movw	r2, #3844
	ldrh	r9, [r3, r2]
	ldr	r2, .L1297+8
	ldr	lr, [r2, #-984]
	ldr	ip, [r2, #-1164]
	movw	r2, #3924
	ldrh	r2, [r3, r2]
	mov	r3, #0
	mov	r6, r3
	str	r2, [sp, #8]
	mov	r2, r5
	b	.L1277
.L1279:
	ldrh	r0, [r2, #16]
	cmp	r0, sl
	beq	.L1278
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
.L1278:
	add	r3, r3, #1
	add	r2, r2, #2
	uxth	r3, r3
.L1277:
	cmp	r3, r9
	bne	.L1279
	ldr	r9, .L1297+8
	mov	r1, r6
	mov	r2, #0
	ldr	r0, [r9, #-984]
	bl	FlashReadPages
	mov	r3, #36
	mul	r3, r3, r6
	mov	r6, #0
	str	r3, [sp, #8]
	b	.L1280
.L1286:
	ldr	fp, .L1297+8
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
	bne	.L1281
	ldr	r3, [r3, #12]
	add	r6, r6, #36
	ldrh	r1, [r3, #0]
	cmp	r1, sl
	moveq	r3, #1
	streq	r3, [fp, #-1016]
	beq	.L1275
.L1282:
	ldr	r0, [r3, #12]
	mov	r1, r9
	ldr	r2, [r3, #8]
	bl	FtlGcUpdatePage
	b	.L1280
.L1281:
	ldr	r3, [fp, #-1884]
	cmp	r3, #0
	beq	.L1283
	ldrh	r3, [r5, #0]
	ldr	r2, [fp, #-2084]
	mov	r3, r3, asl #1
	ldrh	r3, [r2, r3]
	cmp	r3, #119
	movls	r8, r9
	bls	.L1284
.L1283:
	cmn	r1, #1
	moveq	r8, r9
.L1284:
	ldr	r2, .L1297+8
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
	b	.L1285
.L1280:
	ldr	r3, [sp, #8]
	cmp	r6, r3
	bne	.L1286
	ldr	fp, [sp, #12]
	add	r7, r7, #1
	add	r4, r4, #1
	cmp	r7, fp
	uxth	r4, r4
	bcc	.L1287
	ldr	r2, .L1297
	movw	r3, #3000
	ldrh	r1, [r2, r3]
	cmp	r1, sl
	beq	.L1287
	add	r1, r1, r7
	strh	r1, [r2, r3]	@ movhi
	ldr	r2, .L1297+4
	add	r3, r3, #912
	ldrh	r3, [r2, r3]
	cmp	r3, r4
	bhi	.L1288
.L1287:
	ldr	r2, .L1297+4
	movw	r3, #3912
	ldrh	r3, [r2, r3]
	cmp	r3, r4
	bhi	.L1294
.L1291:
	mov	r2, #0
.L1275:
	ldr	r1, .L1297
	movw	r3, #3000
	mvn	r0, #0
	strh	r4, [r5, #2]	@ movhi
	strb	r2, [r5, #6]
	strh	r0, [r1, r3]	@ movhi
	mov	r0, r5
	mov	r1, r4
	bl	ftl_sb_update_avl_pages
.L1288:
	mov	r0, r8
	add	sp, sp, #20
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, sl, fp, pc}
.L1298:
	.align	2
.L1297:
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
	ldr	r5, .L1357
	.pad #28
	sub	sp, sp, #28
	ldr	r4, .L1357+4
	mov	r1, r6
	ldr	r3, .L1357+8
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
	ldr	r0, .L1357+12
	mov	r1, #255
	mov	r2, #12
	bl	ftl_memset
	movw	r3, #3852
	ldrh	r3, [r5, r3]
	str	r3, [sp, #8]
	b	.L1300
.L1302:
	add	r3, r6, r5
	ldr	r1, [sp, #8]
	ldrb	r0, [r3, #3870]	@ zero_extendqisi2
	bl	V2P_block
	str	r0, [sp, #0]
	bl	FtlBbmIsBadBlock
	ldr	r3, [sp, #0]
	cmp	r0, #0
	bne	.L1301
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
.L1301:
	add	r5, r5, #1
	uxth	r5, r5
	b	.L1338
.L1351:
	mov	r7, #0
	movw	fp, #3844
	mov	r5, r7
	mov	r9, #36
	movw	r8, #3924
.L1338:
	ldrh	r3, [r6, fp]
	cmp	r3, r5
	bhi	.L1302
	ldr	r0, [r4, #-984]
	mov	r1, r7
	mov	r2, #1
	mov	r8, #0
	bl	FlashReadPages
	mov	fp, r7
	str	r8, [sp, #12]
	b	.L1303
.L1337:
	ldr	r2, [r4, #-984]
	add	r0, r2, r8
	ldr	r7, [r2, r8]
	ldr	r3, [r0, #4]
	cmn	r7, #1
	ldr	r5, [r0, #12]
	ubfx	r6, r3, #10, #16
	bne	.L1304
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
.L1304:
	ldr	r3, [r4, #-984]
	ldr	r3, [r3, r8]
	cmn	r3, #1
	beq	.L1305
	ldr	r2, [r4, #-1848]
	ldr	r3, [r5, #4]
	cmn	r2, #1
	beq	.L1306
	cmp	r2, r3
	bhi	.L1307
.L1306:
	cmn	r3, #1
	addne	r3, r3, #1
	strne	r3, [sl, #-1848]
.L1307:
	ldrh	r3, [r5, #0]
	movw	r2, #61604
	cmp	r3, r2
	beq	.L1310
	bhi	.L1313
	movw	r2, #61574
	cmp	r3, r2
	bne	.L1308
	b	.L1355
.L1313:
	movw	r2, #61634
	cmp	r3, r2
	beq	.L1311
	movw	r1, #65535
	cmp	r3, r1
	bne	.L1308
	b	.L1356
.L1311:
	ldr	r3, .L1357
	ldr	r1, [r4, #-1040]
	ldr	r2, [r3, #3940]
	ldr	r3, .L1357+8
	uxth	r0, r2
	ldrh	r3, [r4, r3]
	sub	r7, r0, #1
	rsb	r0, r3, r0
	uxth	r7, r7
	sub	r0, r0, #1
	sxth	r0, r0
	str	r0, [sp, #4]
	b	.L1314
.L1322:
	mov	ip, r9, asl #2
	ldr	r0, [r1, r9, asl #2]
	str	ip, [sp, #16]
	ldr	ip, [r5, #4]
	cmp	ip, r0
	bls	.L1315
	ldr	r1, [r1, #0]
	cmp	r1, #0
	bne	.L1316
	cmp	r3, r2
	bne	.L1317
.L1316:
	ldr	r3, [r4, #-1044]
	mov	r1, #1
	ldrh	r0, [r3, #0]
	bl	FtlFreeSysBlkQueueIn
	b	.L1318
.L1317:
	ldr	r2, .L1357+8
	add	r3, r3, #1
	strh	r3, [r4, r2]	@ movhi
.L1318:
	sxth	ip, r7
	mov	r2, #0
	str	ip, [sp, #4]
	b	.L1319
.L1320:
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
.L1319:
	ldr	r0, [sp, #4]
	sxth	r3, r2
	cmp	r3, r0
	bne	.L1320
	ldr	r3, [sl, #-1040]
	mov	r9, r9, asl #1
	ldr	r2, [r5, #4]
	ldr	ip, [sp, #16]
	str	r2, [r3, ip]
	ldr	r3, [sl, #-1044]
	strh	r6, [r3, r9]	@ movhi
	b	.L1321
.L1315:
	sub	r7, r7, #1
	uxth	r7, r7
.L1314:
	ldr	r0, [sp, #4]
	sxth	r9, r7
	cmp	r9, r0
	bgt	.L1322
.L1321:
	sxth	r7, r7
	cmp	r7, #0
	blt	.L1305
.L1323:
	ldr	r3, .L1357+8
	ldr	r1, .L1357
	ldrh	r2, [r4, r3]
	ldr	r1, [r1, #3940]
	rsb	r1, r2, r1
	sub	r1, r1, #1
	sxth	r1, r1
	cmp	r7, r1
	bgt	.L1308
	add	r2, r2, #1
	strh	r2, [r4, r3]	@ movhi
	ldr	r3, [r4, #-1040]
	ldr	r2, [r5, #4]
	str	r2, [r3, r7, asl #2]
	mov	r7, r7, asl #1
	ldr	r3, [r4, #-1044]
	b	.L1353
.L1355:
	ldr	r3, .L1357
	movw	r2, #3932
	movw	r0, #3956
	ldrh	r2, [r3, r2]
	ldrh	r3, [r3, r0]
	sub	r1, r2, #1
	uxth	r7, r1
	rsb	r1, r3, r1
	str	r1, [sp, #4]
	ldr	r1, [r4, #-924]
	b	.L1324
.L1332:
	mov	ip, r9, asl #2
	ldr	r0, [r1, r9, asl #2]
	str	ip, [sp, #16]
	ldr	ip, [r5, #4]
	cmp	ip, r0
	bls	.L1325
	ldr	r1, [r1, #0]
	cmp	r1, #0
	ldr	r1, .L1357
	bne	.L1326
	cmp	r3, r2
	bne	.L1327
.L1326:
	ldr	r3, [r1, #3960]
	mov	r1, #1
	ldrh	r0, [r3, #0]
	bl	FtlFreeSysBlkQueueIn
	b	.L1328
.L1327:
	movw	r2, #3956
	add	r3, r3, #1
	strh	r3, [r1, r2]	@ movhi
.L1328:
	sxth	r0, r7
	mov	r2, #0
	str	r0, [sp, #20]
	ldr	ip, .L1357
	str	r5, [sp, #4]
	b	.L1329
.L1330:
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
.L1329:
	ldr	r0, [sp, #20]
	sxth	r3, r2
	cmp	r3, r0
	bne	.L1330
	ldr	r5, [sp, #4]
	mov	r9, r9, asl #1
	ldr	r3, [sl, #-924]
	ldr	ip, [sp, #16]
	ldr	r2, [r5, #4]
	str	r2, [r3, ip]
	ldr	r3, .L1357
	ldr	r3, [r3, #3960]
	strh	r6, [r3, r9]	@ movhi
	b	.L1331
.L1325:
	sub	r7, r7, #1
	uxth	r7, r7
.L1324:
	ldr	r0, [sp, #4]
	sxth	r9, r7
	cmp	r9, r0
	bgt	.L1332
.L1331:
	sxth	r7, r7
	cmp	r7, #0
	blt	.L1305
.L1333:
	ldr	r3, .L1357
	movw	r0, #3932
	movw	r2, #3956
	ldrh	r0, [r3, r0]
	ldrh	r1, [r3, r2]
	sub	r0, r0, #1
	rsb	r0, r1, r0
	sxth	r0, r0
	cmp	r7, r0
	bgt	.L1308
	add	r1, r1, #1
	strh	r1, [r3, r2]	@ movhi
	ldr	r1, [r5, #4]
	ldr	r2, [r4, #-924]
	str	r1, [r2, r7, asl #2]
	mov	r7, r7, asl #1
	ldr	r3, [r3, #3960]
.L1353:
	strh	r6, [r3, r7]	@ movhi
	b	.L1308
.L1310:
	ldr	r3, .L1357+16
	movw	r1, #65535
	ldrh	r2, [r4, r3]
	cmp	r2, r1
	streqh	r6, [r4, r3]	@ movhi
	beq	.L1354
	ldr	r3, .L1357+20
	movw	r2, #65535
	ldrh	r0, [r4, r3]
	cmp	r0, r2
	beq	.L1335
	mov	r1, #1
	bl	FtlFreeSysBlkQueueIn
.L1335:
	ldr	r3, [r5, #4]
	ldr	r2, [sl, #-1760]
	cmp	r2, r3
	ldr	r3, .L1357+20
	strcsh	r6, [r4, r3]	@ movhi
	bcs	.L1308
	ldr	r2, .L1357+16
	ldrh	r1, [r4, r2]
	strh	r6, [r4, r2]	@ movhi
	strh	r1, [r4, r3]	@ movhi
.L1354:
	ldr	r3, [r5, #4]
	str	r3, [r4, #-1760]
	b	.L1308
.L1356:
	mov	r0, r6
	mov	r1, #0
	b	.L1352
.L1305:
	mov	r0, r6
	mov	r1, #1
.L1352:
	bl	FtlFreeSysBlkQueueIn
.L1308:
	ldr	r5, [sp, #12]
	add	r8, r8, #36
	add	r3, r5, #1
	uxth	r3, r3
	str	r3, [sp, #12]
.L1303:
	ldr	ip, [sp, #12]
	cmp	ip, fp
	bne	.L1337
	ldr	r5, [sp, #8]
	add	r3, r5, #1
	uxth	r3, r3
	str	r3, [sp, #8]
.L1300:
	ldr	r6, .L1357
	movw	r3, #3854
	ldr	ip, [sp, #8]
	ldrh	r3, [r6, r3]
	cmp	r3, ip
	bhi	.L1351
	ldr	r1, .L1357+4
	ldr	r2, [r1, #-1044]
	ldrh	r3, [r2, #0]
	cmp	r3, #0
	bne	.L1339
	ldr	r0, .L1357+8
	ldrh	r1, [r1, r0]
	cmp	r1, #0
	ldrne	r0, [r6, #3940]
	bne	.L1340
	b	.L1339
.L1344:
	mov	r1, r1, asl #1
	ldrh	r1, [r2, r1]
	cmp	r1, #0
	beq	.L1341
	ldr	r7, .L1357
	sxth	r6, r3
	ldr	r1, .L1357+4
	mov	r5, #0
	b	.L1342
.L1343:
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
.L1342:
	ldr	r0, [r7, #3940]
	sxth	r2, r3
	cmp	r2, r0
	bcc	.L1343
	b	.L1339
.L1341:
	add	r3, r3, #1
	uxth	r3, r3
.L1340:
	sxth	r1, r3
	cmp	r1, r0
	bcc	.L1344
.L1339:
	ldr	r2, .L1357
	ldr	r1, [r2, #3960]
	ldrh	r3, [r1, #0]
	cmp	r3, #0
	bne	.L1345
	movw	r0, #3956
	ldrh	r0, [r2, r0]
	cmp	r0, #0
	movwne	r0, #3932
	ldrneh	r2, [r2, r0]
	bne	.L1346
	b	.L1345
.L1350:
	mov	ip, r0, asl #1
	ldrh	ip, [r1, ip]
	cmp	ip, #0
	beq	.L1347
	ldr	r1, .L1357
	movw	r8, #3932
	ldr	r7, .L1357+4
	mov	r6, #0
	b	.L1348
.L1349:
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
.L1348:
	ldrh	ip, [r1, r8]
	sxth	r2, r3
	cmp	r2, ip
	blt	.L1349
	b	.L1345
.L1347:
	add	r3, r3, #1
	uxth	r3, r3
.L1346:
	sxth	r0, r3
	cmp	r0, r2
	blt	.L1350
.L1345:
	mov	r0, #0
	add	sp, sp, #28
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, sl, fp, pc}
.L1358:
	.align	2
.L1357:
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
	ldr	r3, .L1369
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
	bhi	.L1360
	mov	r9, r0, lsr r9
	mov	r7, #0
	ldr	sl, .L1369+4
	mov	r9, r9, asl #2
	b	.L1361
.L1366:
	ldr	r3, [sl, #-920]
	mov	r0, r6
	ldr	r1, .L1369
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
	beq	.L1363
	ldr	r1, .L1369+4
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
	b	.L1365
.L1363:
	mov	r0, r8
	mov	r1, fp
	mov	r2, r4, asl #9
	bl	ftl_memset
.L1365:
	rsb	r5, r4, r5
	add	r6, r6, r4
	add	r8, r8, r4, asl #9
	add	r9, r9, #4
.L1361:
	cmp	r5, #0
	bne	.L1366
.L1360:
	mov	r0, r7
	add	sp, sp, #52
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, sl, fp, pc}
.L1370:
	.align	2
.L1369:
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
	ldr	r5, .L1375
	mov	r1, #1
	ldr	r2, .L1375+4
	bl	FtlVendorPartRead
	ldr	r4, .L1375+8
	ldr	r3, [r5, #-1704]
	cmp	r3, r4
	beq	.L1372
	ldr	r0, .L1375+4
	mov	r1, #0
	mov	r2, #512
	bl	ftl_memset
	str	r4, [r5, #-1704]
.L1372:
	ldr	r2, [r5, #-1704]
	ldr	r3, .L1375
	cmp	r2, r4
	bne	.L1373
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
.L1373:
	ldr	r4, .L1375
	mov	r3, #0
	ldr	r2, [r4, #-1636]
	str	r3, [r4, #-1004]
	ldr	r3, .L1375+12
	cmp	r2, r3
	bne	.L1374
	mov	r3, #1
	ldr	r0, .L1375+16
	str	r3, [r4, #-1884]
	ldr	r1, .L1375+20
	bl	printk
.L1374:
	ldr	r3, .L1375+24
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
.L1376:
	.align	2
.L1375:
	.word	.LANCHOR2
	.word	.LANCHOR2-1704
	.word	1179929683
	.word	305432421
	.word	.LC75
	.word	.LC98
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
	ldr	r4, .L1379
	ldr	r5, .L1379+4
	ldr	r2, [r4, #-936]
	ldrh	r1, [r4, r5]
	bl	FtlVendorPartRead
	ldr	r3, [r4, #-936]
	ldr	r2, [r3, #0]
	ldr	r3, .L1379+8
	cmp	r2, r3
	beq	.L1378
	ldr	r1, .L1379+12
	ldr	r0, .L1379+16
	bl	printk
	ldrh	r2, [r4, r5]
	ldr	r0, [r4, #-936]
	mov	r1, #0
	mov	r2, r2, asl #9
	bl	ftl_memset
.L1378:
	mov	r0, #0
	ldmfd	sp!, {r3, r4, r5, pc}
.L1380:
	.align	2
.L1379:
	.word	.LANCHOR2
	.word	-940
	.word	1112818501
	.word	.LC99
	.word	.LC75
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
	ldr	r4, .L1384
	mov	r5, #12
	ldr	r3, [r4, #-1036]
	ldr	r7, [r3, r0, asl #2]
	cmp	r7, #0
	bne	.L1382
	mul	r5, r5, r1
	ldr	r3, [r4, #-1900]
	ldr	r2, .L1384+4
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
	b	.L1383
.L1382:
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
.L1383:
	mov	r0, #0
	ldmfd	sp!, {r3, r4, r5, r6, r7, pc}
.L1385:
	.align	2
.L1384:
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
	ldr	r3, .L1397
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
	bne	.L1390
	b	.L1395
.L1393:
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
	bne	.L1391
	ldr	r3, [sp, #44]
	cmn	r3, #1
	bne	.L1391
	ldr	r3, [sp, #4]
	cmn	r3, #1
	subne	r4, r6, #1
	uxthne	r4, r4
	bne	.L1395
.L1391:
	add	r6, r6, #1
	uxth	r8, r6
.L1395:
	sxth	r6, r8
	sxth	r3, r4
	cmp	r6, r3
	ble	.L1393
.L1390:
	sxth	r0, r4
	add	sp, sp, #108
	ldmfd	sp!, {r4, r5, r6, r7, r8, sl, pc}
.L1398:
	.align	2
.L1397:
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
	ldr	r4, .L1413
	mov	r1, r5
	ldr	r2, .L1413+4
	ldr	r7, .L1413+8
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
	beq	.L1411
	mov	r1, #1
	bl	FtlGetLastWrittenPage
	ldr	r3, .L1413+12
	add	r2, r0, #1
	mov	r6, r0
	strh	r2, [r4, r3]	@ movhi
	b	.L1401
.L1403:
	ldrh	r2, [r4, r7]
	mov	r1, #1
	ldr	r0, .L1413+16
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
	bne	.L1402
.L1401:
	add	r3, r5, r6
	sxth	r3, r3
	cmp	r3, #0
	bge	.L1403
.L1402:
	ldr	r4, .L1413
	mov	r2, #48
	ldr	r5, .L1413+4
	movw	r6, #3852
	ldr	r0, .L1413+20
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
	ldr	r3, .L1413+24
	cmp	r2, r3
	bne	.L1411
	ldr	r3, .L1413+28
	ldr	r2, .L1413+32
	ldrb	r1, [r4, #-1810]	@ zero_extendqisi2
	ldrh	r3, [r4, r3]
	strh	r3, [r4, r2]	@ movhi
	movw	r2, #3866
	ldrh	r2, [r5, r2]
	cmp	r1, r2
	bne	.L1411
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
	ldr	r3, .L1413+36
	ldr	r1, .L1413+40
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
	ldr	r0, .L1413+44
	strb	r3, [r4, #-2036]
	ldr	r3, .L1413+48
	strh	r1, [r4, r3]	@ movhi
	mov	r3, #0
	ldr	r1, .L1413+52
	strh	r3, [r4, r0]	@ movhi
	strb	r3, [r4, #-1750]
	ldrh	r0, [r4, r1]
	sub	r1, r1, #194
	strb	r3, [r4, #-1748]
	str	r3, [r4, #-1860]
	strh	r0, [r4, r1]	@ movhi
	add	r1, r1, #196
	ldr	r0, .L1413+56
	ldrh	r1, [r4, r1]
	mov	ip, r1, lsr #6
	and	r1, r1, #63
	strb	r1, [r4, #-1990]
	ldrb	r1, [r4, #-1808]	@ zero_extendqisi2
	strh	ip, [r4, r0]	@ movhi
	strb	r1, [r4, #-1988]
	ldr	r1, .L1413+60
	ldrh	r0, [r4, r1]
	sub	r1, r1, #150
	strh	r0, [r4, r1]	@ movhi
	add	r1, r1, #152
	ldr	r0, .L1413+64
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
	ldr	r4, .L1413
	ldr	r3, [r4, #-1784]
	ldr	r1, [r4, #-1844]
	cmp	r3, r1
	strhi	r3, [r4, #-1844]
	cmp	r2, r5
	beq	.L1406
	ldr	r0, .L1413+68
	bl	make_superblock
.L1406:
	ldr	r3, .L1413+72
	ldrh	r3, [r4, r3]
	cmp	r3, r5
	beq	.L1407
	ldr	r0, .L1413+76
	bl	make_superblock
.L1407:
	ldr	r5, .L1413
	movw	r4, #65535
	ldr	r3, .L1413+80
	ldrh	r3, [r5, r3]
	cmp	r3, r4
	beq	.L1408
	ldr	r0, .L1413+84
	bl	make_superblock
.L1408:
	ldr	r3, .L1413+48
	ldrh	r3, [r5, r3]
	cmp	r3, r4
	beq	.L1412
	ldr	r0, .L1413+88
	bl	make_superblock
	mov	r0, #0
	ldmfd	sp!, {r3, r4, r5, r6, r7, pc}
.L1411:
	mvn	r0, #0
	ldmfd	sp!, {r3, r4, r5, r6, r7, pc}
.L1412:
	mov	r0, #0
	ldmfd	sp!, {r3, r4, r5, r6, r7, pc}
.L1414:
	.align	2
.L1413:
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
	ldr	r6, .L1430
	ldr	r8, .L1430+4
	ldr	r4, [r6, #-952]
	mov	r7, r6
	str	r3, [r6, #176]
	str	r4, [r6, #180]
	bl	FtlBbtMemInit
	movw	r3, #3908
	ldrh	r5, [r8, r3]
	sub	r5, r5, #1
	uxth	r5, r5
	b	.L1416
.L1420:
	mov	r1, #1
	mov	r3, r5, asl #10
	ldr	r0, .L1430+8
	mov	r2, r1
	str	r3, [r6, #172]
	bl	FlashReadPages
	ldr	r3, [r6, #168]
	cmn	r3, #1
	bne	.L1417
	ldr	r3, [r6, #172]
	mov	r1, #1
	ldr	r0, .L1430+8
	mov	r2, r1
	add	r3, r3, #1
	str	r3, [r6, #172]
	bl	FlashReadPages
.L1417:
	ldr	r3, [r7, #168]
	cmn	r3, #1
	beq	.L1418
	ldrh	r2, [r4, #0]
	movw	r3, #61649
	cmp	r2, r3
	bne	.L1418
	ldr	r3, .L1430+4
	movw	r2, #3980
	strh	r5, [r3, r2]	@ movhi
	ldr	r2, [r4, #4]
	str	r2, [r3, #3988]
	mov	r2, #3984
	ldrh	r1, [r4, #8]
	strh	r1, [r3, r2]	@ movhi
	b	.L1419
.L1418:
	sub	r5, r5, #1
	uxth	r5, r5
.L1416:
	movw	r3, #3908
	ldrh	r3, [r8, r3]
	sub	r3, r3, #48
	cmp	r5, r3
	bgt	.L1420
.L1419:
	ldr	r5, .L1430+4
	movw	r8, #3980
	movw	r3, #65535
	ldrh	r2, [r5, r8]
	cmp	r2, r3
	beq	.L1429
	mov	r6, #3984
	ldrh	r2, [r5, r6]
	cmp	r2, r3
	beq	.L1422
	ldr	r7, .L1430
	mov	r1, #1
	mov	r2, r2, asl #10
	add	r0, r7, #168
	str	r2, [r7, #172]
	mov	r2, r1
	bl	FlashReadPages
	ldr	r3, [r7, #168]
	cmn	r3, #1
	beq	.L1422
	ldrh	r2, [r4, #0]
	movw	r3, #61649
	cmp	r2, r3
	bne	.L1422
	ldr	r3, [r4, #4]
	ldr	r2, [r5, #3988]
	cmp	r3, r2
	strhi	r3, [r5, #3988]
	ldrhih	r2, [r5, r6]
	ldrhih	r3, [r4, #8]
	strhih	r2, [r5, r8]	@ movhi
	strhih	r3, [r5, r6]	@ movhi
.L1422:
	ldr	r8, .L1430+4
	movw	r3, #3980
	mov	r1, #1
	mov	r6, #0
	ldr	r5, .L1430
	ldrh	r0, [r8, r3]
	bl	FtlGetLastWrittenPage
	movw	r3, #3982
	add	r2, r0, #1
	mov	r7, r0
	strh	r2, [r8, r3]	@ movhi
	b	.L1423
.L1425:
	movw	r2, #3980
	mov	r1, #1
	ldrh	r2, [r8, r2]
	sub	r6, r6, #1
	ldr	r0, .L1430+8
	uxth	r6, r6
	orr	r3, r3, r2, asl #10
	str	r3, [r5, #172]
	ldr	r3, [r5, #-2076]
	mov	r2, r1
	str	r3, [r5, #176]
	bl	FlashReadPages
	ldr	r3, [r5, #168]
	cmn	r3, #1
	bne	.L1424
.L1423:
	add	r3, r6, r7
	sxth	r3, r3
	cmp	r3, #0
	bge	.L1425
.L1424:
	ldr	r3, .L1430+4
	movw	r2, #3986
	ldrh	r1, [r4, #10]
	ldrh	r0, [r4, #12]
	strh	r1, [r3, r2]	@ movhi
	movw	r2, #65535
	cmp	r0, r2
	beq	.L1426
	ldr	r2, [r3, #3840]
	cmp	r0, r2
	beq	.L1426
	movw	r1, #3854
	ldrh	r3, [r3, r1]
	mov	r3, r3, lsr #2
	cmp	r2, r3
	bcs	.L1426
	cmp	r0, r3
	bcs	.L1426
	bl	FtlSysBlkNumInit
.L1426:
	ldr	r6, .L1430+12
	mov	r4, #0
	ldr	r8, .L1430+4
	movw	r7, #3866
	ldr	r5, .L1430
	b	.L1427
.L1428:
	ldr	r3, .L1430+16
	ldr	r1, [r5, #176]
	ldr	r0, [r6, #4]!
	ldrh	r2, [r5, r3]
	mov	r2, r2, asl #2
	mla	r1, r4, r2, r1
	bl	memcpy
	add	r4, r4, #1
.L1427:
	ldrh	r3, [r8, r7]
	cmp	r4, r3
	bcc	.L1428
	mov	r0, #0
	ldmfd	sp!, {r4, r5, r6, r7, r8, pc}
.L1429:
	mvn	r0, #0
	ldmfd	sp!, {r4, r5, r6, r7, r8, pc}
.L1431:
	.align	2
.L1430:
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
	ldr	r3, .L1439
	movw	fp, #3866
	ldr	r7, .L1439+4
	ldr	r6, .L1439+8
	ldr	r2, [r3, #-2076]
	ldr	r8, [r3, #-952]
	str	r2, [r3, #176]
	str	r8, [r3, #180]
	b	.L1433
.L1438:
	movw	r9, #3908
	mvn	r3, #0
	ldrh	r4, [r6, r9]
	strh	r3, [r7], #2	@ movhi
	add	r4, r4, r3
	ldr	sl, .L1439
	uxth	r4, r4
	b	.L1434
.L1437:
	mla	r3, r3, r5, r4
	mov	r1, #1
	ldr	r0, .L1439+12
	mov	r2, r1
	mov	r3, r3, asl #10
	str	r3, [sl, #172]
	bl	FlashReadPages
	ldr	r3, [sl, #168]
	cmn	r3, #1
	beq	.L1435
	ldrh	r2, [r8, #0]
	movw	r3, #61664
	cmp	r2, r3
	streqh	r4, [r7, #-2]	@ movhi
	beq	.L1436
.L1435:
	sub	r4, r4, #1
	uxth	r4, r4
.L1434:
	ldrh	r3, [r6, r9]
	sub	r2, r3, #16
	cmp	r4, r2
	bgt	.L1437
.L1436:
	add	r5, r5, #1
.L1433:
	ldrh	r3, [r6, fp]
	cmp	r5, r3
	bcc	.L1438
	mov	r0, #0
	ldmfd	sp!, {r3, r4, r5, r6, r7, r8, r9, sl, fp, pc}
.L1440:
	.align	2
.L1439:
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
	ldr	r3, .L1460
	mov	r6, r0
	mov	r8, #0
	ldr	r7, .L1460+4
	ldrb	fp, [r3, #2697]	@ zero_extendqisi2
	b	.L1442
.L1449:
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
	bcs	.L1444
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
.L1444:
	add	r8, r8, #1
	add	r6, r6, #36
.L1442:
	cmp	r8, sl
	bne	.L1449
	ldr	r3, [sp, #12]
	cmp	r3, #0
	movne	r6, #0
	ldrne	r5, .L1460+8
	bne	.L1450
	b	.L1451
.L1456:
	ldr	r3, [r4, #0]
	cmn	r3, #1
	bne	.L1452
	ldr	r1, [r4, #4]
	ldr	r0, .L1460+12
	bl	printk
	b	.L1453
.L1452:
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
	bne	.L1454
	ldr	r0, .L1460+16
	ldr	r1, [r4, #4]
	bl	printk
	str	r7, [r4, #0]
.L1454:
	ldr	r3, [r4, #12]
	cmp	r3, #0
	beq	.L1455
	ldr	r2, [r3, #0]
	ldr	r3, [r5, #156]
	ldr	r3, [r3, #0]
	cmp	r2, r3
	beq	.L1455
	ldr	r0, .L1460+20
	ldr	r1, [r4, #4]
	bl	printk
	mvn	r3, #0
	str	r3, [r4, #0]
.L1455:
	ldr	r3, [r4, #8]
	cmp	r3, #0
	beq	.L1453
	ldr	r2, [r3, #0]
	ldr	r3, [r5, #152]
	ldr	r3, [r3, #0]
	cmp	r2, r3
	beq	.L1453
	ldr	r0, .L1460+24
	ldr	r1, [r4, #4]
	bl	printk
	mvn	r3, #0
	str	r3, [r4, #0]
.L1453:
	add	r6, r6, #1
	add	r4, r4, #36
.L1450:
	cmp	r6, sl
	bne	.L1456
.L1451:
	mov	r0, #0
	add	sp, sp, #68
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, sl, fp, pc}
.L1461:
	.align	2
.L1460:
	.word	.LANCHOR1
	.word	.LANCHOR0
	.word	.LANCHOR2
	.word	.LC100
	.word	.LC101
	.word	.LC102
	.word	.LC103
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
	ldr	r5, .L1489
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
	ldr	ip, .L1489+4
	ldrb	ip, [ip, #2697]	@ zero_extendqisi2
	str	ip, [sp, #12]
	beq	.L1463
	bl	FlashProgSlc2KPages
	b	.L1464
.L1476:
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
	bcs	.L1466
	ldrb	r1, [r5, #3837]	@ zero_extendqisi2
	add	r2, fp, r2, asl #4
	cmp	r1, #0
	ldr	r2, [r2, #2788]
	moveq	r8, #0
	cmp	r2, #0
	beq	.L1468
	cmp	r3, #1
	bne	.L1469
	ldr	r0, [r5, #2164]
	bl	NandcIqrWaitFlashReady
.L1469:
	ldrb	r0, [sp, #32]	@ zero_extendqisi2
	bl	FlashWaitCmdDone
.L1468:
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
	bne	.L1471
	bl	NandcWaitFlashReady
	b	.L1472
.L1471:
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
.L1472:
	ldr	r2, [sp, #20]
	sub	r3, r2, #1
	cmp	r3, #5
	bhi	.L1473
	add	r3, r5, r4
	ldrb	r3, [r3, #3760]	@ zero_extendqisi2
	cmp	r3, #0
	beq	.L1473
	mov	r0, r4
	ldrb	r1, [r5, #2909]	@ zero_extendqisi2
	ldr	r2, .L1489+8
	mov	r3, #0
	bl	HynixSetRRPara
.L1473:
	mov	r0, r4
	bl	NandcFlashCs
	cmp	sl, #1
	bne	.L1474
	ldrb	r3, [r5, #80]	@ zero_extendqisi2
	cmp	r3, #0
	beq	.L1474
	mov	r0, r4
	bl	flash_enter_slc_mode
.L1474:
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
	beq	.L1475
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
.L1475:
	mov	r0, r4
	ldr	r1, [sp, #28]
	bl	FlashProgSecondCmd
	mov	r0, r4
	bl	NandcFlashDeCs
	add	r6, r6, r8
.L1466:
	add	r6, r6, #1
.L1463:
	ldr	r3, [sp, #8]
	cmp	r6, r3
	bcc	.L1476
	ldr	r5, .L1489
	mov	r4, #0
	ldr	r6, .L1489+12
	ldr	r0, [r5, #2164]
	bl	NandcIqrWaitFlashReady
	b	.L1477
.L1479:
	uxtb	r0, r4
	bl	FlashWaitCmdDone
	cmp	sl, #1
	bne	.L1478
	ldrb	r3, [r5, #80]	@ zero_extendqisi2
	cmp	r3, #0
	beq	.L1478
	ldrb	r0, [r6, r4, asl #4]	@ zero_extendqisi2
	bl	flash_exit_slc_mode
.L1478:
	add	r4, r4, #1
.L1477:
	ldrb	r3, [r5, #3766]	@ zero_extendqisi2
	cmp	r4, r3
	bcc	.L1479
	ldr	r2, [sp, #16]
	cmp	r2, #0
	ldreq	r0, [sp, #16]
	beq	.L1464
	mov	r5, #0
	ldr	r4, .L1489+16
	ldr	r6, [sp, #8]
	b	.L1480
.L1485:
	ldr	r3, [r9, #0]
	cmn	r3, #1
	bne	.L1481
	ldr	r1, [r9, #4]
	ldr	r0, .L1489+20
	bl	printk
	b	.L1482
.L1481:
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
	bne	.L1483
	ldr	r0, .L1489+24
	ldr	r1, [r9, #4]
	bl	printk
	str	r7, [r9, #0]
.L1483:
	ldr	r3, [r9, #12]
	cmp	r3, #0
	beq	.L1484
	ldr	r2, [r3, #0]
	ldr	r3, [r4, #156]
	ldr	r3, [r3, #0]
	cmp	r2, r3
	beq	.L1484
	ldr	r0, .L1489+28
	ldr	r1, [r9, #4]
	bl	printk
	mvn	r3, #0
	str	r3, [r9, #0]
.L1484:
	ldr	r3, [r9, #8]
	cmp	r3, #0
	beq	.L1482
	ldr	r2, [r3, #0]
	ldr	r3, [r4, #152]
	ldr	r3, [r3, #0]
	cmp	r2, r3
	beq	.L1482
	ldr	r0, .L1489+32
	ldr	r1, [r9, #4]
	bl	printk
	mvn	r3, #0
	str	r3, [r9, #0]
.L1482:
	add	r5, r5, #1
	add	r9, r9, #36
.L1480:
	cmp	r5, r6
	bne	.L1485
	mov	r0, #0
.L1464:
	add	sp, sp, #76
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, sl, fp, pc}
.L1490:
	.align	2
.L1489:
	.word	.LANCHOR0
	.word	.LANCHOR1
	.word	.LANCHOR0+2912
	.word	.LANCHOR0+2780
	.word	.LANCHOR2
	.word	.LC100
	.word	.LC101
	.word	.LC102
	.word	.LC103
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
	ldr	r4, .L1500
	mov	r1, #255
	ldr	r6, .L1500+4
	movw	sl, #3852
	ldr	r7, .L1500+8
	ldr	r3, [r4, #-2076]
	ldr	r5, [r4, #-952]
	str	r3, [r4, #176]
	ldrh	r3, [r4, r6]
	str	r5, [r4, #180]
	str	r8, [r5, #12]
	strh	r3, [r5, #2]	@ movhi
	ldr	r3, .L1500+12
	strh	r3, [r5, #0]	@ movhi
	ldr	r3, [r4, #-1760]
	str	r8, [r5, #8]
	str	r3, [r5, #4]
	ldr	r3, .L1500+16
	str	r3, [r4, #-1820]
	ldr	r3, .L1500+20
	str	r3, [r4, #-1816]
	ldr	r3, .L1500+24
	ldrh	r2, [r4, r3]
	sub	r3, r3, #50
	strh	r2, [r4, r3]	@ movhi
	movw	r3, #3866
	ldrh	r3, [r7, r3]
	strb	r3, [r4, #-1810]
	ldr	r3, .L1500+28
	ldrh	r2, [r4, r3]
	add	r3, r3, #238
	strh	r2, [r4, r3]	@ movhi
	sub	r3, r3, #236
	ldrh	r2, [r4, r3]
	ldrb	r3, [r4, #-2038]	@ zero_extendqisi2
	orr	r2, r3, r2, asl #6
	ldr	r3, .L1500+32
	strh	r2, [r4, r3]	@ movhi
	ldrb	r3, [r4, #-2036]	@ zero_extendqisi2
	strb	r3, [r4, #-1809]
	ldr	r3, .L1500+36
	ldrh	r2, [r4, r3]
	add	r3, r3, #194
	strh	r2, [r4, r3]	@ movhi
	sub	r3, r3, #192
	ldrh	r2, [r4, r3]
	ldrb	r3, [r4, #-1990]	@ zero_extendqisi2
	orr	r2, r3, r2, asl #6
	ldr	r3, .L1500+40
	strh	r2, [r4, r3]	@ movhi
	ldrb	r3, [r4, #-1988]	@ zero_extendqisi2
	strb	r3, [r4, #-1808]
	ldr	r3, .L1500+44
	ldrh	r2, [r4, r3]
	add	r3, r3, #150
	strh	r2, [r4, r3]	@ movhi
	sub	r3, r3, #148
	ldr	r0, [r4, #176]
	ldrh	r2, [r4, r3]
	ldrb	r3, [r4, #-1942]	@ zero_extendqisi2
	orr	r2, r3, r2, asl #6
	ldr	r3, .L1500+48
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
	ldr	r1, .L1500+52
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
.L1499:
	ldr	r3, [r4, #-2076]
	mov	r1, #1
	ldr	r7, .L1500+56
	ldrh	r2, [r4, r6]
	str	r3, [r4, #176]
	ldr	r3, [r4, #-952]
	ldr	r0, .L1500+60
	str	r3, [r4, #180]
	ldrh	r3, [r4, r7]
	orr	r3, r3, r2, asl #10
	mov	r2, r1
	str	r3, [r4, #172]
	mov	r3, r1
	bl	FlashProgPages
	ldr	r1, .L1500+8
	movw	r3, #3914
	ldrh	r2, [r4, r7]
	ldrh	r3, [r1, r3]
	sub	r3, r3, #1
	cmp	r2, r3
	blt	.L1493
	ldr	r3, .L1500+64
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
	ldr	r0, .L1500+60
	bl	FlashProgPages
.L1493:
	ldr	r2, .L1500+56
	ldr	r1, [r4, #168]
	ldrh	r3, [r4, r2]
	cmn	r1, #1
	add	r3, r3, #1
	uxth	r3, r3
	strh	r3, [r4, r2]	@ movhi
	bne	.L1494
	cmp	r3, #1
	ldreq	r1, .L1500+8
	movweq	r3, #3914
	ldreqh	r3, [r1, r3]
	subeq	r3, r3, #1
	streqh	r3, [r4, r2]	@ movhi
	b	.L1499
.L1494:
	cmp	r3, #1
	beq	.L1499
	movw	r3, #65535
	cmp	r8, r3
	beq	.L1496
	mov	r0, r8
	mov	r1, #1
	bl	FtlFreeSysBlkQueueIn
.L1496:
	mov	r0, #0
	ldmfd	sp!, {r3, r4, r5, r6, r7, r8, sl, pc}
.L1501:
	.align	2
.L1500:
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
	ldr	r7, .L1511
	mov	r6, #0
	ldr	sl, .L1511+4
	movw	r9, #3866
	ldr	r8, .L1511+8
	ldr	r3, [r7, #-952]
	ldr	r0, [r7, #-2076]
	str	r3, [r7, #180]
	movw	r3, #3922
	str	r0, [r7, #176]
	ldrh	r2, [sl, r3]
	bl	ftl_memset
	b	.L1503
.L1504:
	ldr	r3, .L1511+12
	ldr	r1, [r8, #4]!
	ldrh	r2, [r7, r3]
	ldr	r3, [r7, #176]
	mul	r0, r2, r6
	mov	r2, r2, asl #2
	add	r6, r6, #1
	add	r0, r3, r0, asl #2
	bl	memcpy
.L1503:
	ldrh	r3, [sl, r9]
	ldr	r4, .L1511+4
	cmp	r6, r3
	ldr	r5, .L1511
	blt	.L1504
	ldr	r6, [r5, #180]
	mov	r1, #255
	mov	r2, #16
	mov	r7, #0
	mov	fp, r7
	mov	r0, r6
	bl	ftl_memset
	ldr	r3, .L1511+16
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
	b	.L1510
.L1509:
	mov	r7, #1
.L1510:
	ldr	r3, [r5, #-2076]
	movw	r8, #3980
	ldr	r4, .L1511+4
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
	ldr	r0, .L1511+20
	bl	printk
	mov	r1, #1
	mov	r2, r1
	mov	r3, r1
	ldr	r0, .L1511+24
	bl	FlashProgPages
	movw	r3, #3914
	ldrh	r3, [r4, r3]
	ldrh	r2, [r4, sl]
	sub	r3, r3, #1
	cmp	r2, r3
	blt	.L1506
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
	ldr	r0, .L1511+24
	mov	r2, r1
	mov	r3, r1
	bl	FlashProgPages
.L1506:
	ldr	r2, .L1511+4
	movw	r3, #3982
	ldrh	r1, [r2, r3]
	add	r1, r1, #1
	strh	r1, [r2, r3]	@ movhi
	ldr	r3, [r5, #168]
	cmn	r3, #1
	bne	.L1507
	ldr	r1, [r5, #172]
	ldr	r0, .L1511+28
	bl	printk
	b	.L1510
.L1507:
	cmp	r7, #0
	beq	.L1509
	mov	r0, #0
	ldmfd	sp!, {r1, r2, r3, r4, r5, r6, r7, r8, r9, sl, fp, pc}
.L1512:
	.align	2
.L1511:
	.word	.LANCHOR2
	.word	.LANCHOR0
	.word	.LANCHOR0+4004
	.word	-1032
	.word	-3887
	.word	.LC104
	.word	.LANCHOR2+168
	.word	.LC105
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
	ldr	r4, .L1523
	ldr	r3, .L1523+4
	ldrh	r3, [r4, r3]
	cmp	r3, #0
	movne	r6, #0
	movne	sl, r4
	bne	.L1514
	b	.L1515
.L1521:
	add	r3, r3, r6
	mov	r1, r9
	mov	r5, #0
	ldr	r7, .L1523+4
	ldrb	r0, [r3, #3870]	@ zero_extendqisi2
	bl	V2P_block
	mov	r8, r0
	b	.L1516
.L1520:
	add	r3, r4, r5, asl #1
	sub	r3, r3, #1120
	sub	r3, r3, #12
	ldrh	r3, [r3, #0]
	cmp	r3, r8
	bne	.L1517
	mov	r1, r8
	ldr	r0, .L1523+8
	bl	printk
	mov	r0, r8
	bl	FtlBbmMapBadBlock
	bl	FtlBbmTblFlush
	ldrh	r1, [r4, r7]
	mov	r3, r5
	b	.L1518
.L1519:
	add	r0, r3, #1
	add	r3, r4, r3, asl #1
	sub	r3, r3, #1120
	add	r2, r4, r0, asl #1
	sub	r2, r2, #1120
	sub	r2, r2, #12
	ldrh	r2, [r2, #0]
	strh	r2, [r3, #-12]	@ movhi
	uxth	r3, r0
.L1518:
	cmp	r3, r1
	bcc	.L1519
	sub	r1, r1, #1
	strh	r1, [sl, r7]	@ movhi
.L1517:
	add	r5, r5, #1
	uxth	r5, r5
.L1516:
	ldrh	r3, [r4, r7]
	cmp	r3, r5
	bhi	.L1520
	add	r6, r6, #1
	uxth	r6, r6
.L1514:
	ldr	r3, .L1523+12
	movw	r2, #3844
	ldrh	r2, [r3, r2]
	cmp	r2, r6
	bhi	.L1521
	bl	FtlGcReFreshBadBlk
.L1515:
	mov	r0, #0
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, sl, pc}
.L1524:
	.align	2
.L1523:
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
	ldr	r3, .L1533
	mov	r4, r0
	ldr	r1, [r3, #-2064]
	ldrh	r2, [r1, r2]
	cmp	r2, #0
	bne	.L1526
	ldr	r1, .L1533+4
	ldrh	r0, [r3, r1]
	cmp	r0, r4
	mvneq	r2, #0
	streqh	r2, [r3, r1]	@ movhi
	beq	.L1528
	ldr	r1, .L1533+8
	ldrh	r1, [r3, r1]
	cmp	r1, r4
	beq	.L1529
	ldr	r1, .L1533+12
	ldrh	r1, [r3, r1]
	cmp	r1, r4
	beq	.L1529
	ldr	r1, .L1533+16
	ldrh	r3, [r3, r1]
	cmp	r3, r4
	beq	.L1529
.L1528:
	mov	r1, r4
	ldr	r0, .L1533+20
	bl	List_remove_node
	ldr	r5, .L1533
	ldr	r3, .L1533+24
	mov	r0, r4
	ldrh	r2, [r5, r3]
	sub	r2, r2, #1
	strh	r2, [r5, r3]	@ movhi
	bl	free_data_superblock
	mov	r0, r4
	bl	FtlGcFreeBadSuperBlk
	mov	r2, #1
	b	.L1529
.L1526:
	bl	List_update_data_list
	mov	r2, #0
.L1529:
	mov	r0, r2
	ldmfd	sp!, {r3, r4, r5, pc}
.L1534:
	.align	2
.L1533:
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
	beq	.L1536
	ldr	r2, .L1541
	mov	r3, r0, asl #1
	ldr	r2, [r2, #-2064]
	ldrh	r4, [r2, r3]
	cmp	r4, #0
	subne	r4, r4, #1
	strneh	r4, [r2, r3]	@ movhi
	bne	.L1536
	ldr	r0, .L1541+4
	mov	r1, r5
	mov	r2, r4
	bl	printk
	mov	r0, r4
	ldmfd	sp!, {r3, r4, r5, r6, r7, pc}
.L1536:
	ldr	r7, .L1541
	movw	r3, #65535
	ldr	r6, .L1541+8
	ldrh	r0, [r7, r6]
	cmp	r0, r3
	streqh	r5, [r7, r6]	@ movhi
	moveq	r0, #0
	ldmeqfd	sp!, {r3, r4, r5, r6, r7, pc}
	cmp	r0, r5
	beq	.L1540
	bl	update_vpc_list
	strh	r5, [r7, r6]	@ movhi
	adds	r0, r0, #0
	movne	r0, #1
	ldmfd	sp!, {r3, r4, r5, r6, r7, pc}
.L1540:
	mov	r0, #0
	ldmfd	sp!, {r3, r4, r5, r6, r7, pc}
.L1542:
	.align	2
.L1541:
	.word	.LANCHOR2
	.word	.LC107
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
	ldr	r5, .L1555
	ldr	r7, .L1555+4
	add	r3, r0, r3, asl #1
	ldrh	r3, [r3, #16]
	b	.L1544
.L1545:
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
.L1544:
	cmp	r3, r6
	beq	.L1545
	ldrb	r2, [r4, #8]	@ zero_extendqisi2
	cmp	r2, #1
	bne	.L1547
	ldrb	r2, [r5, #80]	@ zero_extendqisi2
	cmp	r2, #0
	bne	.L1547
	ldrh	r2, [r4, #2]
	add	r2, r7, r2, asl #1
	sub	r2, r2, #912
	ldrh	r2, [r2, #0]
	cmp	r2, r6
	bne	.L1547
	ldrh	r3, [r4, #4]
	ldrh	r0, [r4, #0]
	sub	r3, r3, #1
	strh	r3, [r4, #4]	@ movhi
	bl	decrement_vpc_count
	b	.L1545
.L1547:
	ldrh	r6, [r4, #2]
	movw	r5, #65535
	mov	r7, r5
	orr	r6, r6, r3, asl #10
	ldrh	r3, [r4, #4]
	sub	r3, r3, #1
	strh	r3, [r4, #4]	@ movhi
.L1552:
	ldr	r2, .L1555
	movw	r3, #3844
	ldrh	r2, [r2, r3]
	ldrb	r3, [r4, #6]	@ zero_extendqisi2
.L1549:
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
	beq	.L1549
	strb	r3, [r4, #6]
	ldrb	r3, [r4, #8]	@ zero_extendqisi2
	cmp	r3, #1
	bne	.L1550
	ldr	r3, .L1555
	ldrb	r2, [r3, #80]	@ zero_extendqisi2
	cmp	r2, #0
	ldrh	r2, [r4, #2]
	bne	.L1553
	ldr	r3, .L1555+4
	add	r2, r3, r2, asl #1
	sub	r2, r2, #912
	ldrh	r3, [r2, #0]
	cmp	r3, r7
	bne	.L1550
	ldrh	r3, [r4, #4]
	cmp	r3, #0
	beq	.L1550
	sub	r3, r3, #1
	ldrh	r0, [r4, #0]
	strh	r3, [r4, #4]	@ movhi
	bl	decrement_vpc_count
	b	.L1552
.L1553:
	movw	r1, #3914
	ldrh	r1, [r3, r1]
	cmp	r2, r1
	bcc	.L1550
	ldr	r1, .L1555+4
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
.L1550:
	mov	r0, r6
	ldmfd	sp!, {r3, r4, r5, r6, r7, pc}
.L1556:
	.align	2
.L1555:
	.word	.LANCHOR0
	.word	.LANCHOR2
	.fnend
	.size	get_new_active_ppa, .-get_new_active_ppa
	.align	2
	.type	FtlSlcSuperblockCheck.part.18, %function
FtlSlcSuperblockCheck.part.18:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	stmfd	sp!, {r3, r4, r5, r6, r7, lr}
	.save {r3, r4, r5, r6, r7, lr}
	mov	r4, r0
	ldrb	r3, [r0, #6]	@ zero_extendqisi2
	movw	r6, #65535
	ldr	r5, .L1563
	ldr	r7, .L1563+4
	add	r3, r0, r3, asl #1
	ldrh	r3, [r3, #16]
	b	.L1558
.L1559:
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
.L1558:
	cmp	r3, r6
	beq	.L1559
	ldrb	r2, [r4, #8]	@ zero_extendqisi2
	cmp	r2, #1
	bne	.L1561
	ldrb	r3, [r5, #80]	@ zero_extendqisi2
	cmp	r3, #0
	bne	.L1561
	ldrh	r3, [r4, #2]
	add	r3, r7, r3, asl #1
	sub	r3, r3, #912
	ldrh	r3, [r3, #0]
	cmp	r3, r6
	bne	.L1561
	ldrh	r3, [r4, #4]
	ldrh	r0, [r4, #0]
	sub	r3, r3, #1
	strh	r3, [r4, #4]	@ movhi
	bl	decrement_vpc_count
	ldrh	r3, [r4, #4]
	cmp	r3, #0
	bne	.L1559
	ldrh	r2, [r4, #2]
	strb	r3, [r4, #6]
	add	r2, r2, #1
	strh	r2, [r4, #2]	@ movhi
	ldmfd	sp!, {r3, r4, r5, r6, r7, pc}
.L1561:
	ldr	r3, .L1563
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
	ldr	r1, .L1563+4
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
.L1564:
	.align	2
.L1563:
	.word	.LANCHOR0
	.word	.LANCHOR2
	.fnend
	.size	FtlSlcSuperblockCheck.part.18, .-FtlSlcSuperblockCheck.part.18
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
	b	FtlSlcSuperblockCheck.part.18
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
	ldr	r4, .L1601
	mov	r5, r0
	mov	r7, r4
.L1597:
	ldr	r3, .L1601+4
	cmp	r5, r3
	bne	.L1569
	ldr	r3, .L1601+8
	ldrh	r2, [r4, r3]
	ldr	r3, [r4, #-1180]
	mov	r1, r2, lsr #1
	mul	ip, r3, r2
	add	r0, r1, #1
	add	r0, r0, ip, lsr #2
	ldr	ip, [r4, #-1884]
	cmp	ip, #0
	uxth	r0, r0
	beq	.L1591
	ldr	ip, [r4, #-1824]
	cmp	ip, #29
	bhi	.L1591
	cmp	ip, #2
	bls	.L1596
	tst	r2, #1
	beq	.L1570
	cmp	r3, #0
	moveq	r1, r3
	beq	.L1571
	b	.L1570
.L1569:
	ldrb	r3, [r5, #8]	@ zero_extendqisi2
	cmp	r3, #1
	bne	.L1596
	ldr	r3, .L1601+12
	movw	r2, #3864
	ldrh	r2, [r3, r2]
	cmp	r2, #1
	beq	.L1596
	ldrb	r3, [r3, #80]	@ zero_extendqisi2
	cmp	r3, #0
	bne	.L1596
	sub	r3, r3, #2048
	ldr	r2, [r4, #-1884]
	ldrh	r3, [r4, r3]
	cmp	r2, #0
	mov	r1, r3, lsr #3
	beq	.L1570
	ldr	r2, [r4, #-1824]
	cmp	r2, #1
	movls	r1, #7
	mulls	r1, r1, r3
	movls	r1, r1, lsr #3
	b	.L1570
.L1591:
	mov	r1, r0
.L1570:
	cmp	r1, #0
	subne	r1, r1, #1
	uxthne	r1, r1
	b	.L1571
.L1596:
	mov	r1, #0
.L1571:
	ldr	r0, .L1601+16
	bl	List_pop_index_node
	ldr	r3, .L1601+8
	ldrh	r2, [r4, r3]
	sub	r2, r2, #1
	strh	r2, [r4, r3]	@ movhi
	uxth	r6, r0
	mov	r0, r5
	strh	r6, [r5, #0]	@ movhi
	bl	make_superblock
	ldrb	r3, [r5, #7]	@ zero_extendqisi2
	cmp	r3, #0
	beq	.L1572
	ldr	r2, .L1601+12
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
	b	.L1573
.L1572:
	ldr	r2, [r4, #-2064]
	mov	r3, r6, asl #1
	mvn	r1, #0
	strh	r1, [r2, r3]	@ movhi
	b	.L1598
.L1576:
	str	r0, [r3, #8]
	movw	r5, #65535
	str	r0, [r3, #12]
	ldrh	lr, [r1, #16]
	cmp	lr, r5
	beq	.L1575
	ldr	r5, [sp, #4]
	mov	ip, #36
	mov	lr, lr, asl #10
	mla	r9, ip, fp, r5
	add	fp, fp, #1
	uxth	fp, fp
	str	lr, [r9, #4]
.L1575:
	add	r2, r2, #1
	add	r3, r3, #36
	add	r1, r1, #2
	uxth	r2, r2
.L1573:
	cmp	r2, r8
	bne	.L1576
	ldr	r3, [r4, #-1884]
	ldr	r5, [sp, #0]
	cmp	r3, #0
	beq	.L1577
	ldr	r3, .L1601+20
	cmp	r5, r3
	bne	.L1577
	ldr	r2, [r4, #-2084]
	mov	r3, r6, asl #1
	ldrh	r3, [r2, r3]
	cmp	r3, #30
	movhi	r3, #0
	strhib	r3, [r4, #-2036]
.L1577:
	ldrb	r3, [r5, #8]	@ zero_extendqisi2
	ldr	r2, [r4, #-2084]
	cmp	r3, #0
	mov	r3, r6, asl #1
	ldrh	r1, [r2, r3]
	bne	.L1578
	cmp	r1, #0
	ldrne	ip, .L1601+12
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
	b	.L1600
.L1578:
	add	r1, r1, #1
	strh	r1, [r2, r3]	@ movhi
	ldr	r3, [r4, #-1836]
	mov	r0, r6
	mov	r1, #1
	add	r3, r3, #1
	str	r3, [r4, #-1836]
.L1600:
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
	ldr	r3, .L1601+12
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
	b	.L1583
.L1584:
	add	r8, r8, #1
	ldr	r2, [r3, #-32]
	uxth	r8, r8
	bic	r2, r2, #1020
	bic	r2, r2, #3
	str	r2, [r3, #-32]
.L1583:
	cmp	r8, fp
	add	r3, r3, #36
	bne	.L1584
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
	b	.L1585
.L1587:
	ldr	r1, [r4, #-2088]
	add	r0, r1, fp
	ldr	r5, [r1, fp]
	cmn	r5, #1
	bne	.L1586
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
.L1586:
	add	fp, fp, #36
	add	sl, sl, #2
.L1585:
	cmp	fp, r9
	bne	.L1587
	cmp	r8, #0
	mov	r5, r6
	mov	r9, ip
	mov	r6, r3
	beq	.L1588
	mov	r0, r3
	bl	update_multiplier_value
	bl	FtlBbmTblFlush
.L1588:
	ldrb	r3, [r5, #7]	@ zero_extendqisi2
	cmp	r3, #0
	bne	.L1589
	ldr	r3, [r7, #-2064]
	mvn	r2, #0
	strh	r2, [r3, r9]	@ movhi
.L1598:
	mov	r0, r6
	bl	INSERT_DATA_LIST
	b	.L1597
.L1589:
	ldr	r1, .L1601+12
	movw	r2, #3912
	mov	r0, #0
	strh	r0, [r5, #2]	@ movhi
	strb	r0, [r5, #6]
	ldrh	r2, [r1, r2]
	strh	r6, [r5, #0]	@ movhi
	mul	r2, r2, r3
	ldr	r3, .L1601
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
.L1602:
	.align	2
.L1601:
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
	ldr	r3, .L1610
	stmfd	sp!, {r4, r5, r6, r7, r8, sl, lr}
	.save {r4, r5, r6, r7, r8, sl, lr}
	mov	r4, r0
	ldr	r5, [r3, #-1164]
	.pad #44
	sub	sp, sp, #44
	ldr	r3, .L1610+4
	ldrb	r8, [r3, #80]	@ zero_extendqisi2
	cmp	r8, #0
	beq	.L1604
	ldrb	r8, [r0, #8]	@ zero_extendqisi2
	sub	r0, r8, #1
	rsbs	r8, r0, #0
	adc	r8, r8, r0
.L1604:
	mov	sl, #7
	mvn	r7, #0
	ldr	r6, .L1610
	b	.L1605
.L1608:
	ldrh	r3, [r4, #4]
	cmp	r3, #0
	beq	.L1606
	mov	r0, r4
	bl	get_new_active_ppa
	ldr	r3, [r6, #-2076]
	str	r7, [sp, #20]
	mov	r1, #1
	str	r3, [sp, #12]
	ldr	r3, [r6, #-952]
	str	r3, [sp, #16]
	str	r7, [r5, #8]
	str	r7, [r5, #12]
	ldrh	r3, [r4, #0]
	str	r0, [sp, #8]
	add	r0, sp, #4
	strh	r3, [r5, #2]	@ movhi
	mov	r3, #0
	strh	r3, [r5, #0]	@ movhi
	ldr	r2, [r6, #-1844]
	ldr	r5, [r6, #-952]
	cmn	r2, #2
	str	r2, [r5, #4]
	addne	r2, r2, #1
	moveq	r2, #0
	str	r2, [r6, #-1844]
	mov	r2, r8
	bl	FlashProgPages
	ldrh	r0, [r4, #0]
	bl	decrement_vpc_count
.L1605:
	subs	sl, sl, #1
	bne	.L1608
.L1606:
	ldr	r2, .L1610
	ldrh	r3, [r4, #0]
	ldrh	r1, [r4, #4]
	ldr	r2, [r2, #-2064]
	mov	r3, r3, asl #1
	ldrh	r0, [r2, r3]
	rsb	r1, r1, r0
	strh	r1, [r2, r3]	@ movhi
	ldr	r2, .L1610+4
	movw	r3, #3912
	ldrh	r3, [r2, r3]
	strh	r3, [r4, #2]	@ movhi
	mov	r3, #0
	strb	r3, [r4, #6]
	strh	r3, [r4, #4]	@ movhi
	add	sp, sp, #44
	ldmfd	sp!, {r4, r5, r6, r7, r8, sl, pc}
.L1611:
	.align	2
.L1610:
	.word	.LANCHOR2
	.word	.LANCHOR0
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
	ldr	r9, .L1638
	mov	r6, #0
	mov	r8, r0
	mov	r7, r1
	mov	r4, r6
	mov	r5, r6
	ldrb	r2, [r9, #80]	@ zero_extendqisi2
	mov	fp, #36
	str	r0, [r9, #3976]
	ldr	sl, .L1638+4
	str	r2, [sp, #8]
	b	.L1613
.L1617:
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
	beq	.L1614
	bl	IsBlkInVendorPart
	cmp	r0, #0
	bne	.L1615
.L1614:
	ldr	r0, [sp, #4]
	bl	FtlBbmIsBadBlock
	cmp	r0, #0
	addne	r5, r5, #1
	uxthne	r5, r5
	bne	.L1615
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
.L1615:
	add	r6, r6, #1
	uxth	r6, r6
.L1613:
	movw	r1, #3844
	ldrh	r3, [r9, r1]
	cmp	r3, r6
	bhi	.L1617
	cmp	r4, #0
	beq	.L1619
	ldr	r2, [sp, #8]
	mov	r9, #0
	ldr	r6, .L1638+4
	mov	fp, r9
	adds	sl, r2, #0
	mov	r2, r4
	movne	sl, #1
	ldr	r0, [r6, #-2088]
	mov	r1, sl
	bl	FlashEraseBlocks
.L1621:
	ldr	r3, [r6, #-2088]
	add	r2, r3, r9
	ldr	r3, [r3, r9]
	cmn	r3, #1
	bne	.L1620
	ldr	r0, [r2, #4]
	add	r5, r5, #1
	ubfx	r0, r0, #10, #16
	uxth	r5, r5
	bl	FtlBbmMapBadBlock
.L1620:
	add	fp, fp, #1
	add	r9, r9, #36
	uxth	fp, fp
	cmp	fp, r4
	bne	.L1621
.L1622:
	cmp	r7, #0
	mov	r6, #0
	ldrne	r2, .L1638
	movwne	r3, #3914
	movne	sl, #1
	moveq	r0, #1
	moveq	r1, #6
	stmeqib	sp, {r0, r1}
	ldrneh	r3, [r2, r3]
	strne	r3, [sp, #4]
	movne	r3, r3, lsr #2
	strne	r3, [sp, #8]
.L1632:
	mov	r9, #0
	ldr	fp, .L1638+4
	mov	r4, r9
	b	.L1623
.L1626:
	mov	r0, #36
	ldr	r2, [fp, #-2088]
	mul	r3, r0, r9
	mov	r1, #0
	str	r1, [r2, r3]
	mov	r1, r8
	ldr	r2, .L1638
	add	r3, r2, r9
	ldrb	r0, [r3, #3870]	@ zero_extendqisi2
	bl	V2P_block
	cmp	r7, #0
	str	r0, [sp, #12]
	beq	.L1624
	bl	IsBlkInVendorPart
	cmp	r0, #0
	bne	.L1625
.L1624:
	ldr	r0, [sp, #12]
	bl	FtlBbmIsBadBlock
	cmp	r0, #0
	bne	.L1625
	ldr	r3, [fp, #-2088]
	mov	r0, #36
	ldr	r1, [sp, #12]
	mla	r3, r0, r4, r3
	add	r2, r6, r1, asl #10
	ldr	r1, .L1638
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
.L1625:
	add	r9, r9, #1
	uxth	r9, r9
.L1623:
	ldr	r0, .L1638
	movw	r2, #3844
	ldrh	r3, [r0, r2]
	cmp	r3, r9
	bhi	.L1626
	cmp	r4, #0
	beq	.L1619
	ldr	r9, .L1638+4
	mov	r3, #1
	mov	r1, r4
	mov	r2, sl
	mov	fp, #0
	ldr	r0, [r9, #-2088]
	bl	FlashProgPages
	mov	r3, #36
	mul	r3, r3, r4
	str	r3, [sp, #12]
	b	.L1628
.L1631:
	ldr	r2, [r9, #-2088]
	add	r3, r2, fp
	ldr	r2, [r2, fp]
	cmn	r2, #1
	bne	.L1629
	ldr	r0, [r3, #4]
	add	r5, r5, #1
	ubfx	r0, r0, #10, #16
	uxth	r5, r5
	bl	FtlBbmMapBadBlock
	b	.L1630
.L1629:
	cmp	r7, #0
	beq	.L1630
	ldr	r0, [r3, #4]
	mov	r1, #1
	ubfx	r0, r0, #10, #16
	bl	FtlFreeSysBlkQueueIn
.L1630:
	add	fp, fp, #36
.L1628:
	ldr	r2, [sp, #12]
	cmp	fp, r2
	bne	.L1631
	ldr	r3, [sp, #8]
	ldr	r0, [sp, #4]
	add	r6, r6, r3
	uxth	r6, r6
	cmp	r6, r0
	bcc	.L1632
	cmp	r8, #63
	bhi	.L1619
	ldr	r3, .L1638+4
	mov	r1, sl
	mov	r2, r4
	ldr	r0, [r3, #-2088]
	bl	FlashEraseBlocks
.L1619:
	mov	r0, r5
	add	sp, sp, #20
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, sl, fp, pc}
.L1639:
	.align	2
.L1638:
	.word	.LANCHOR0
	.word	.LANCHOR2
	.fnend
	.size	FtlLowFormatEraseBlock, .-FtlLowFormatEraseBlock
	.align	2
	.type	FlashTestBlk.part.20, %function
FlashTestBlk.part.20:
	.fnstart
	@ args = 0, pretend = 0, frame = 104
	@ frame_needed = 0, uses_anonymous_args = 0
.L1641:
	stmfd	sp!, {r4, r5, lr}
	.save {r4, r5, lr}
	.pad #108
	sub	sp, sp, #108
	ldr	r4, .L1643
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
.L1644:
	.align	2
.L1643:
	.word	.LANCHOR2
	.fnend
	.size	FlashTestBlk.part.20, .-FlashTestBlk.part.20
	.align	2
	.global	FlashTestBlk
	.type	FlashTestBlk, %function
FlashTestBlk:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	@ link register save eliminated.
	ldr	r3, .L1647
	ldr	r3, [r3, #132]
	cmp	r0, r3
	bcc	.L1646
	b	FlashTestBlk.part.20
.L1646:
	mov	r0, #0
	bx	lr
.L1648:
	.align	2
.L1647:
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
	ldr	r4, .L1677
	movw	r1, #2214
	ldr	r3, .L1677+4
	mov	r5, #0
	ldr	r0, .L1677+8
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
	ldr	r4, .L1677+4
	uxth	r3, r3
	str	r3, [sp, #24]
	b	.L1651
.L1673:
	ldr	r3, .L1677
	add	r3, r3, r5, asl #1
	ldrh	r6, [r3, #208]
	cmp	r6, #0
	bne	.L1652
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
	b	.L1653
.L1661:
	ldr	r2, [sp, #12]
	mvn	r3, #0
	strb	r3, [sp, #34]
	tst	r2, #1
	strb	r3, [sp, #35]
	beq	.L1654
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
	bne	.L1654
	ldr	r1, [r4, #4]
	add	r2, sp, #35
	mov	r0, fp
	add	r1, r3, r1
	bl	FlashReadSpare
	ldrb	r3, [sp, #34]	@ zero_extendqisi2
	ldrb	r2, [sp, #35]	@ zero_extendqisi2
	and	r3, r2, r3
	strb	r3, [sp, #34]
.L1654:
	ldr	r3, [sp, #12]
	tst	r3, #2
	beq	.L1655
	ldr	r3, [r4, #2776]
	mov	r0, fp
	add	r2, sp, #35
	ldrh	r1, [r3, #10]
	ldr	r3, [r8, #2740]
	sub	r1, r1, #1
	add	r1, r1, r3
	add	r1, r1, r6
	bl	FlashReadSpare
.L1655:
	ldr	r3, [r4, #2776]
	ldrb	r3, [r3, #7]	@ zero_extendqisi2
	cmp	r3, #1
	cmpne	r3, #8
	ldrb	r3, [sp, #34]	@ zero_extendqisi2
	bne	.L1656
	cmp	r3, #0
	beq	.L1675
	ldrb	r0, [sp, #35]	@ zero_extendqisi2
	rsbs	r0, r0, #1
	movcc	r0, #0
	b	.L1657
.L1656:
	cmp	r3, #255
	bne	.L1675
	ldrb	r0, [sp, #35]	@ zero_extendqisi2
	subs	r0, r0, #255
	movne	r0, #1
	b	.L1657
.L1675:
	mov	r0, #1
.L1657:
	ldr	r2, [sp, #12]
	tst	r2, #4
	beq	.L1658
	ldr	r1, [r8, #2740]
	mov	r0, fp
	add	r1, r6, r1
	bl	SandiskProgTestBadBlock
.L1658:
	cmp	r0, #0
	beq	.L1659
	mov	r1, r5
	mov	r2, r7
	ldr	r0, .L1677+12
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
	bgt	.L1660
.L1659:
	ldr	r3, [sp, #8]
	add	r7, r7, #1
	add	r6, r6, r3
.L1653:
	uxth	r3, r7
	str	r3, [sp, #16]
	cmp	r3, r9
	bcc	.L1661
.L1660:
	mov	r2, sl
	ldr	r0, .L1677+16
	mov	r1, r5
	bl	printk
	ldrb	r3, [r4, #3766]	@ zero_extendqisi2
	ldr	r2, [sp, #20]
	mul	r3, r3, r2
	cmp	sl, r3
	blt	.L1662
	movw	r3, #2220
	ldr	r0, [r4, #3780]
	ldrh	r2, [r4, r3]
	mov	r1, #0
	mov	r2, r2, asl #9
	bl	ftl_memset
.L1662:
	cmp	r5, #0
	bne	.L1663
	ldr	r3, .L1677
	mov	r8, r5
	mov	r7, r5
	mov	sl, #1
	ldrh	fp, [r3, #132]
	b	.L1664
.L1666:
	mov	r0, r6
	bl	FlashTestBlk
	cmp	r0, #0
	beq	.L1665
	mov	r1, r6
	ldr	r0, .L1677+20
	bl	printk
	ldr	r3, [r4, #3780]
	mov	r2, r6, lsr #5
	add	r7, r7, #1
	and	r6, r6, #31
	ldr	r1, [r3, r2, asl #2]
	uxth	r7, r7
	orr	r6, r1, sl, asl r6
	str	r6, [r3, r2, asl #2]
.L1665:
	add	r8, r8, #1
	uxth	r8, r8
.L1664:
	add	r6, r8, fp
	ldrb	r3, [r4, #1]	@ zero_extendqisi2
	uxth	r6, r6
	cmp	r3, r6
	bhi	.L1666
	ldr	r6, [sp, #24]
	sub	sl, r9, #50
	mov	r8, #1
	b	.L1667
.L1669:
	mov	r0, r6
	bl	FlashTestBlk
	cmp	r0, #0
	beq	.L1668
	mov	r1, r6
	ldr	r0, .L1677+20
	bl	printk
	ldr	r3, [r4, #3780]
	mov	r2, r6, lsr #5
	and	r0, r6, #31
	ldr	r1, [r3, r2, asl #2]
	orr	r1, r1, r8, asl r0
	str	r1, [r3, r2, asl #2]
.L1668:
	sub	r6, r6, #1
	uxth	r6, r6
.L1667:
	cmp	r6, sl
	bgt	.L1669
	ldr	r3, .L1677
	ldrb	r2, [r4, #1]	@ zero_extendqisi2
	ldr	r3, [r3, #132]
	rsb	r3, r3, r2
	cmp	r7, r3
	bcc	.L1663
	movw	r3, #2220
	ldr	r0, [r4, #3780]
	ldrh	r2, [r4, r3]
	mov	r1, #0
	mov	r2, r2, asl #9
	bl	ftl_memset
.L1663:
	mul	r8, r9, r5
	ldr	r7, .L1677
	ldr	r6, [sp, #24]
	ldr	fp, [sp, #28]
	add	sl, r7, r5, asl #1
.L1670:
	ldr	r0, .L1677+24
	mov	r1, r5
	mov	r2, r6
	bl	printk
	ldr	r3, [r4, #3780]
	b	.L1671
.L1672:
	sub	r6, r6, #1
	uxth	r6, r6
.L1671:
	mov	r1, r6, lsr #5
	and	r2, r6, #31
	ldr	r1, [r3, r1, asl #2]
	mov	r2, r1, lsr r2
	ands	r2, r2, #1
	bne	.L1672
	ldr	r3, .L1677+28
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
	bne	.L1670
.L1652:
	add	r5, r5, #1
	uxtb	r5, r5
.L1651:
	ldrb	r3, [r4, #3766]	@ zero_extendqisi2
	cmp	r3, r5
	bhi	.L1673
	add	sp, sp, #76
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, sl, fp, pc}
.L1678:
	.align	2
.L1677:
	.word	.LANCHOR2
	.word	.LANCHOR0
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
	blt	.L1680
	uxth	r0, r0
	mov	r0, r0, asl #1
	ldrh	r7, [r5, r0]
	cmp	r7, #0
	beq	.L1680
	ldr	r3, [r4, #32]
	cmp	r3, #0
	bne	.L1680
	mov	r2, #1
	ldr	r1, .L1687
	str	r2, [r4, #32]
	strh	r3, [r5, r0]	@ movhi
	ldrh	r3, [r4, #8]
	ldrh	r2, [r4, #2]
	sub	r3, r3, #1
	strh	r3, [r4, #8]	@ movhi
	movw	r3, #3914
	ldrh	r3, [r1, r3]
	cmp	r2, r3
	bcc	.L1681
	mov	r0, r4
	bl	ftl_map_blk_alloc_new_blk
.L1681:
	mov	r5, #0
	ldr	r6, .L1687+4
	mov	r9, r5
	b	.L1682
.L1685:
	ldr	r3, [r8, r5, asl #2]
	mov	sl, r5, asl #2
	cmp	r7, r3, lsr #10
	bne	.L1683
	ldr	r3, [r6, #-972]
	mov	r1, #1
	ldr	r0, .L1687+8
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
	beq	.L1683
	mov	r0, r4
	mov	r1, r5
	ldr	r2, [r6, #176]
	bl	FtlMapWritePage
.L1683:
	add	r5, r5, #1
	uxth	r5, r5
.L1682:
	ldrh	r3, [r4, #6]
	cmp	r3, r5
	bhi	.L1685
	mov	r0, r7
	mov	r1, #1
	bl	FtlFreeSysBlkQueueIn
	mov	r3, #0
	str	r3, [r4, #32]
.L1680:
	ldr	r1, .L1687
	movw	r3, #3914
	ldrh	r2, [r4, #2]
	ldrh	r3, [r1, r3]
	cmp	r2, r3
	bcc	.L1686
	mov	r0, r4
	bl	ftl_map_blk_alloc_new_blk
.L1686:
	mov	r0, #0
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, sl, pc}
.L1688:
	.align	2
.L1687:
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
	bne	.L1690
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
	b	.L1691
.L1690:
	mov	r3, r3, asl #1
	ldr	r2, [r0, #28]
	mov	r1, #255
	ldrh	r6, [r5, r3]
	ldr	r5, .L1695
	ldrh	r3, [r0, #2]
	orr	r3, r3, r6, asl #10
	str	r3, [r5, #172]
	ldr	r3, [r5, #-2076]
	str	r3, [r5, #176]
	ldr	r3, [r5, #-952]
	str	r3, [r5, #180]
	str	r2, [r3, #4]
	ldr	r2, .L1695+4
	strh	r2, [r3, #8]	@ movhi
	ldrh	r2, [r0, #4]
	strh	r6, [r3, #2]	@ movhi
	strh	r2, [r3, #0]	@ movhi
	movw	r3, #3914
	ldr	r2, .L1695+8
	ldr	r0, [r5, #-2076]
	ldrh	r2, [r2, r3]
	mov	r2, r2, asl #3
	bl	ftl_memset
	mov	r2, #0
	mov	r3, r2
	b	.L1692
.L1694:
	ldr	r1, [r7, r3, asl #2]
	cmp	r6, r1, lsr #10
	bne	.L1693
	add	r2, r2, #1
	ldr	r1, [r5, #-2076]
	uxth	r2, r2
	str	r3, [r1, r2, asl #3]
	ldr	r1, [r5, #-2076]
	ldr	r0, [r7, r3, asl #2]
	add	r1, r1, r2, asl #3
	str	r0, [r1, #4]
.L1693:
	add	r3, r3, #1
	uxth	r3, r3
.L1692:
	ldrh	r1, [r4, #6]
	cmp	r1, r3
	bhi	.L1694
	mov	r1, #1
	mov	r3, #0
	ldr	r0, .L1695+12
	mov	r2, r1
	bl	FlashProgPages
	ldrh	r3, [r4, #2]
	mov	r0, r4
	add	r3, r3, #1
	strh	r3, [r4, #2]	@ movhi
	bl	ftl_map_blk_gc
.L1691:
	mov	r0, #0
	ldmfd	sp!, {r3, r4, r5, r6, r7, pc}
.L1696:
	.align	2
.L1695:
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
	ldr	r5, .L1704
	mov	r6, r1
	ldr	r9, .L1704+4
	mov	r8, r2
	movw	fp, #3914
.L1703:
	ldr	r3, [r5, #-1864]
	add	r3, r3, #1
	str	r3, [r5, #-1864]
	ldrh	r3, [r9, fp]
	ldrh	r2, [r4, #2]
	sub	r3, r3, #1
	cmp	r2, r3
	bge	.L1698
	ldrh	r2, [r4, #0]
	movw	r3, #65535
	cmp	r2, r3
	bne	.L1699
.L1698:
	mov	r0, r4
	bl	Ftl_write_map_blk_to_last_page
.L1699:
	ldrh	r3, [r4, #0]
	mov	r1, #0
	ldr	r2, [r4, #12]
	ldr	r0, [r5, #-952]
	mov	r3, r3, asl #1
	ldr	r7, .L1704
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
	beq	.L1703
	ldr	r3, [r5, #168]
	cmn	r3, #1
	beq	.L1703
	ldr	r2, [r7, #172]
	mov	r0, #0
	ldr	r3, [r4, #24]
	str	r2, [r3, r6, asl #2]
	ldmfd	sp!, {r3, r4, r5, r6, r7, r8, r9, sl, fp, pc}
.L1705:
	.align	2
.L1704:
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
	ldr	r5, .L1707
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
.L1708:
	.align	2
.L1707:
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
	ldr	r3, .L1718
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
	ldr	r3, .L1718+4
	uxth	r7, r7
	ldr	sl, [r3, #-1900]
	mov	r3, #0
	mov	r5, r3
	b	.L1710
.L1716:
	add	r3, r3, #12
	add	r1, sl, r3
	ldrh	r1, [r1, #-12]
	cmp	r1, r6
	bne	.L1711
.L1712:
	cmp	r8, #0
	ldr	r3, .L1718+4
	mov	r2, #12
	bne	.L1713
	ldr	r3, [r3, #-1900]
	mla	r2, r2, r5, r3
	ldr	r3, [r2, #8]
	ldr	r3, [r3, r7, asl #2]
	str	r3, [r4, #0]
	b	.L1714
.L1713:
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
	ldr	r2, .L1718+8
	strh	r6, [r3, r2]	@ movhi
.L1714:
	ldr	r3, .L1718+4
	mov	r2, #12
	mov	r0, #0
	ldr	r3, [r3, #-1900]
	mla	r5, r2, r5, r3
	ldr	r3, [r5, #4]
	cmn	r3, #1
	addne	r3, r3, #1
	strne	r3, [r5, #4]
	ldmfd	sp!, {r3, r4, r5, r6, r7, r8, sl, pc}
.L1711:
	add	r5, r5, #1
	uxth	r5, r5
.L1710:
	cmp	r5, r2
	bne	.L1716
	bl	select_l2p_ram_region
	mov	r3, #12
	mul	r3, r3, r0
	mov	r5, r0
	add	r2, sl, r3
	ldrh	r1, [sl, r3]
	movw	r3, #65535
	cmp	r1, r3
	beq	.L1717
	ldr	r3, [r2, #4]
	cmp	r3, #0
	bge	.L1717
	bl	flush_l2p_region
.L1717:
	mov	r0, r6
	mov	r1, r5
	bl	load_l2p_region
	b	.L1712
.L1719:
	.align	2
.L1718:
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
	ldr	r3, .L1726
	ldr	r1, [r3, #-2064]
	mov	r5, r0, asl #1
	ldrh	r2, [r1, r5]
	cmp	r2, #0
	addne	r2, r2, #1
	strneh	r2, [r1, r5]	@ movhi
	bne	.L1722
	ldr	r4, [r3, #-2052]
	cmp	r4, #0
	beq	.L1722
	ldr	r1, .L1726+4
	mov	ip, #6
	ldrh	lr, [r3, r1]
	ldr	r3, [r3, #-2072]
	ldr	r1, .L1726+8
	rsb	r4, r3, r4
	mov	r4, r4, asr #1
	mul	r4, r1, r4
	movw	r1, #65535
	uxth	r4, r4
	b	.L1723
.L1725:
	cmp	r4, r0
	bne	.L1724
	mov	r1, r4
	ldr	r0, .L1726+12
	bl	List_remove_node
	ldr	r6, .L1726
	ldr	r3, .L1726+4
	mov	r0, r4
	ldrh	r2, [r6, r3]
	sub	r2, r2, #1
	strh	r2, [r6, r3]	@ movhi
	bl	INSERT_DATA_LIST
	ldr	r3, [r6, #-2064]
	ldrh	r2, [r3, r5]
	add	r2, r2, #1
	strh	r2, [r3, r5]	@ movhi
	b	.L1722
.L1724:
	mul	r4, ip, r4
	ldrh	r4, [r3, r4]
	cmp	r4, r1
	beq	.L1722
	add	r2, r2, #1
	uxth	r2, r2
.L1723:
	cmp	r2, lr
	bne	.L1725
.L1722:
	mov	r0, r7
	add	r1, sp, #4
	mov	r2, #1
	bl	log2phys
	ldmfd	sp!, {r1, r2, r3, r4, r5, r6, r7, pc}
.L1727:
	.align	2
.L1726:
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
	ldr	r5, .L1732
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
	beq	.L1730
	mov	r1, #1
	add	r0, r5, #168
	mov	r2, r1
	bl	FlashReadPages
	b	.L1731
.L1730:
	ldr	r2, .L1732+4
	movw	r3, #3922
	mov	r1, #255
	ldrh	r2, [r2, r3]
	bl	ftl_memset
.L1731:
	mov	r0, r4
	mov	r1, r6
	ldr	r2, [r5, #176]
	ldmfd	sp!, {r4, r5, r6, lr}
	b	FtlMapWritePage
.L1733:
	.align	2
.L1732:
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
	ldr	r3, .L1744
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
	bhi	.L1735
	mov	r8, r0, lsr r8
	mov	r7, #0
	ldr	sl, .L1744+4
	mov	ip, r8, asl #2
	str	ip, [sp, #8]
	b	.L1736
.L1741:
	ldr	r1, .L1744
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
	ldr	r2, .L1744+4
	uxth	r1, r1
	str	r1, [sp, #12]
	rsb	r5, r1, fp
	uxth	r5, r5
	cmp	r5, r4
	uxthhi	r5, r4
	cmp	r3, #0
	beq	.L1738
	cmp	r5, fp
	beq	.L1738
	str	r3, [sp, #24]
	mov	r1, #1
	ldr	r3, [r2, #-968]
	add	r0, sp, #20
	mov	r2, r1
	str	r3, [sp, #28]
	mov	r3, #0
	str	r3, [sp, #32]
	bl	FlashReadPages
	b	.L1739
.L1738:
	ldr	ip, .L1744
	movw	r3, #3922
	ldr	r0, [r2, #-968]
	mov	r1, #0
	ldrh	r2, [ip, r3]
	bl	ftl_memset
.L1739:
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
	ldr	r0, .L1744+8
	add	r8, r8, #1
	ldr	r2, [sl, #-968]
	add	r9, r9, fp
	bl	FtlMapWritePage
	ldr	ip, [sp, #8]
	add	ip, ip, #4
	str	ip, [sp, #8]
	cmn	r0, #1
	moveq	r7, r0
.L1736:
	cmp	r4, #0
	bne	.L1741
.L1735:
	mov	r0, r7
	add	sp, sp, #60
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, sl, fp, pc}
.L1745:
	.align	2
.L1744:
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
	ldr	r3, .L1748
	ldr	r2, .L1748+4
	ldr	r1, [r3, #-1704]
	cmp	r1, r2
	bxne	lr
	ldr	r2, .L1748+8
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
	ldr	r2, .L1748+12
	b	FtlVendorPartWrite
.L1749:
	.align	2
.L1748:
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
	ldr	r1, .L1757
	stmfd	sp!, {r3, lr}
	.save {r3, lr}
	ldr	r3, [r1, #-1884]
	cmp	r3, #0
	moveq	r2, #32
	beq	.L1751
	ldr	r2, [r1, #-1824]
	cmp	r2, #29
	movls	r2, #4
	movhi	r2, #32
.L1751:
	mov	r3, #264
	ldrh	r1, [r1, r3]
	cmp	r1, #31
	addls	r1, r1, #1
	ldrls	r2, .L1757
	strlsh	r1, [r2, r3]	@ movhi
	movls	r2, #1
	cmp	r0, #0
	ldr	r3, .L1757
	bne	.L1753
	ldr	r1, [r3, #-936]
	ldr	r0, [r1, #20]
	ldr	r1, [r1, #16]
	add	r2, r2, r0
	cmp	r1, r2
	bcc	.L1754
.L1753:
	ldr	r2, [r3, #-936]
	mov	r0, #64
	ldr	r1, [r2, #16]
	str	r1, [r2, #20]
	ldr	r1, .L1757+4
	str	r1, [r2, #0]
	ldr	r1, .L1757+8
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
.L1754:
	mov	r0, #0
	ldmfd	sp!, {r3, pc}
.L1758:
	.align	2
.L1757:
	.word	.LANCHOR2
	.word	1112818501
	.word	-940
	.fnend
	.size	FtlEctTblFlush, .-FtlEctTblFlush
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
	ldr	r4, .L1764
	mov	r5, #2048
	ldr	r6, .L1764+4
.L1762:
	ldr	r0, [r4, #-1620]
	ldr	r3, [r6, #3972]
	cmp	r0, r3
	bcs	.L1760
	mov	r2, #0
	mov	r1, sp
	bl	log2phys
	ldr	r2, [sp, #0]
	ldr	r3, [r4, #-1620]
	cmn	r2, #1
	add	r3, r3, #1
	str	r3, [r4, #-1620]
	beq	.L1761
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
	bne	.L1760
	ldr	r0, [sp, #0]
	ubfx	r0, r0, #10, #16
	bl	P2V_block_in_plane
	bl	FtlGcRefreshBlock
	b	.L1760
.L1761:
	subs	r5, r5, #1
	bne	.L1762
.L1760:
	mvn	r0, #0
	add	sp, sp, #40
	ldmfd	sp!, {r4, r5, r6, pc}
.L1765:
	.align	2
.L1764:
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
	ldr	r3, .L1775
	ldr	r0, .L1775+4
	ldr	r2, [r3, #-1624]
	cmp	r2, #0
	mov	r2, r3
	beq	.L1767
	ldr	r1, [r3, #-1620]
	ldr	r2, [r0, #3972]
	cmp	r1, r2
	bcs	.L1768
	b	FtlReadRefresh.part.15
.L1768:
	mov	r2, #0
	str	r2, [r3, #-1624]
	str	r2, [r3, #-1620]
	ldr	r2, [r3, #-1880]
	b	.L1774
.L1767:
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
	bhi	.L1771
	add	r3, r1, r3
	cmp	r3, r2
	bcs	.L1769
.L1771:
	ldr	r3, .L1775
	mov	r1, #1
	str	r1, [r3, #-1624]
	mov	r1, #0
	str	r1, [r3, #-1620]
.L1774:
	str	r2, [r3, #-1628]
.L1769:
	mov	r0, #0
	bx	lr
.L1776:
	.align	2
.L1775:
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
	ldr	r5, .L1797
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
	b	.L1778
.L1792:
	ldr	r0, [sp, #12]
	cmp	r3, r0
	mov	r3, r3, asl #1
	bne	.L1779
	ldrh	r0, [r7, r3]
	mov	r1, #1
	add	fp, r7, r3
	bl	FtlGetLastWrittenPage
	ldr	r1, [sp, #12]
	mov	r7, #0
	ldr	r2, [sp, #16]
	strh	r9, [r4, #0]	@ movhi
	ldr	r5, .L1797
	uxth	r0, r0
	add	r3, r0, #1
	strh	r3, [r4, #2]	@ movhi
	ldr	r3, [r2, r1, asl #2]
	sxth	r9, r0
	str	r7, [r5, #176]
	add	r9, r9, #1
	str	r3, [r4, #28]
	b	.L1780
.L1782:
	ldrh	r2, [fp, #0]
	mov	r1, #1
	ldr	r0, .L1797+4
	orr	r3, r3, r2, asl #10
	mov	r2, r1
	str	r3, [r5, #172]
	bl	FlashReadPages
	ldr	r3, [r5, #168]
	cmn	r3, #1
	beq	.L1781
	ldrh	r3, [r6, #8]
	cmp	r3, sl
	bcs	.L1781
	ldrh	r2, [r4, #4]
	ldrh	r1, [r6, #0]
	cmp	r1, r2
	ldreq	r2, [r5, #172]
	streq	r2, [r8, r3, asl #2]
.L1781:
	add	r7, r7, #1
	uxth	r7, r7
.L1780:
	sxth	r3, r7
	cmp	r3, r9
	blt	.L1782
	b	.L1783
.L1779:
	ldr	r2, [r5, #-2076]
	add	r0, r7, r3
	ldr	fp, .L1797+8
	str	r0, [sp, #20]
	str	r2, [r5, #176]
	ldrh	r1, [r7, r3]
	movw	r3, #3914
	ldrh	r2, [fp, r3]
	ldr	r0, .L1797+4
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
	beq	.L1784
	ldrh	r1, [r6, #0]
	ldrh	r2, [r4, #4]
	cmp	r1, r2
	bne	.L1784
	ldrh	r1, [r6, #8]
	movw	r2, #64245
	cmp	r1, r2
	bne	.L1784
	b	.L1796
.L1787:
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
	b	.L1785
.L1796:
	mov	r2, #0
.L1785:
	ldrh	r0, [fp, r3]
	sxth	r1, r2
	sub	r0, r0, #1
	cmp	r1, r0
	blt	.L1787
	b	.L1788
.L1784:
	ldr	r1, .L1797
	mov	fp, #0
	ldr	r3, .L1797+8
	add	ip, r1, #168
	str	fp, [r1, #176]
	b	.L1789
.L1791:
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
	beq	.L1790
	ldrh	r2, [r6, #8]
	cmp	r2, sl
	bcs	.L1790
	ldrh	r1, [r4, #4]
	ldrh	r0, [r6, #0]
	cmp	r0, r1
	ldreq	r1, [r5, #172]
	streq	r1, [r8, r2, asl #2]
.L1790:
	add	fp, fp, #1
	uxth	fp, fp
.L1789:
	movw	r1, #3914
	sxth	r2, fp
	ldrh	r1, [r3, r1]
	cmp	r2, r1
	blt	.L1791
.L1788:
	add	r9, r9, #1
	uxth	r9, r9
.L1778:
	ldr	r1, [sp, #8]
	sxth	r3, r9
	cmp	r3, r1
	blt	.L1792
.L1783:
	mov	r0, r4
	bl	ftl_free_no_use_map_blk
	ldr	r1, .L1797+8
	movw	r3, #3914
	ldrh	r2, [r4, #2]
	ldrh	r3, [r1, r3]
	cmp	r2, r3
	bne	.L1793
	mov	r0, r4
	bl	ftl_map_blk_alloc_new_blk
.L1793:
	ldrh	r2, [r4, #8]
	ldrh	r3, [r4, #10]
	cmp	r2, r3
	bcc	.L1794
	mov	r0, r4
	bl	ftl_map_blk_gc
.L1794:
	mov	r0, #0
	add	sp, sp, #28
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, sl, fp, pc}
.L1798:
	.align	2
.L1797:
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
	ldr	r0, .L1800
	bl	FtlMapTblRecovery
	mov	r0, #0
	ldmfd	sp!, {r3, pc}
.L1801:
	.align	2
.L1800:
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
	ldr	r3, .L1803
	ldr	r0, .L1803+4
	ldrh	r2, [r3, r2]
	strh	r2, [r0, #234]	@ movhi
	ldr	r2, .L1803+8
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
.L1804:
	.align	2
.L1803:
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
	ldr	r2, .L1890
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
	bne	.L1808
	b	.L1887
.L1809:
	add	r3, r3, #1
	uxth	r3, r3
	add	r1, r4, r3, asl #1
	ldrh	r0, [r1, #16]
.L1808:
	cmp	r0, r2
	beq	.L1809
	ldrb	r1, [r4, #8]	@ zero_extendqisi2
	cmp	r1, #1
	bne	.L1810
	bl	FtlGetLastWrittenPage
	cmn	r0, #1
	mov	r7, r0
	beq	.L1811
	ldr	r3, .L1890
	ldrb	r2, [r3, #80]	@ zero_extendqisi2
	cmp	r2, #0
	bne	.L1880
	add	r3, r3, r0, asl #1
	movw	r2, #2228
	ldrh	r5, [r3, r2]
	b	.L1874
.L1810:
	mov	r1, #0
	bl	FtlGetLastWrittenPage
	cmn	r0, #1
	mov	r7, r0
	beq	.L1811
.L1880:
	mov	r5, r7
.L1874:
	ldr	r3, .L1890
	movw	r2, #3844
	mov	r6, #0
	str	r4, [sp, #40]
	movw	ip, #65535
	ldrh	r9, [r3, r2]
	ldr	r2, .L1890+4
	ldr	sl, [r2, #-984]
	ldr	r8, [r2, #-1164]
	movw	r2, #3924
	ldrh	lr, [r3, r2]
	mov	r2, r4
	mov	r3, r6
	b	.L1813
.L1811:
	mov	r3, #0
	strh	r3, [r4, #2]	@ movhi
	mov	r2, r3	@ movhi
.L1887:
	strb	r2, [r4, #6]
	b	.L1807
.L1815:
	ldrh	r0, [r2, #16]
	cmp	r0, ip
	beq	.L1814
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
.L1814:
	add	r3, r3, #1
	add	r2, r2, #2
	uxth	r3, r3
.L1813:
	cmp	r3, r9
	bne	.L1815
	ldrb	r3, [r4, #8]	@ zero_extendqisi2
	cmp	r3, #1
	movne	r3, #0
	bne	.L1881
	ldr	r3, .L1890
	ldrb	r3, [r3, #80]	@ zero_extendqisi2
	adds	r3, r3, #0
	movne	r3, #1
.L1881:
	ldr	r8, .L1890+4
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
	b	.L1817
.L1822:
	ldr	r2, [r9, #0]
	cmp	r2, #0
	strne	r8, [sp, #12]
	bne	.L1820
	ldr	r2, [r9, #12]
	ldr	r7, [r2, #4]
	cmn	r7, #1
	beq	.L1819
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
.L1819:
	ldr	r2, [r2, #0]
	cmn	r2, #1
	bne	.L1820
	ldr	r3, .L1890+4
	mov	r2, #36
	uxth	r7, fp
	ldr	r3, [r3, #-984]
	mla	sl, r2, sl, r3
	ldr	r0, [sl, #4]
	b	.L1882
.L1820:
	add	sl, sl, #1
	add	r9, r9, #36
	uxth	sl, sl
.L1817:
	cmp	sl, r6
	bne	.L1822
	ldr	r3, .L1890+4
	add	r7, fp, #1
	uxth	r7, r7
	ldr	r3, [r3, #-984]
	ldr	r0, [r3, #4]
.L1882:
	ubfx	r0, r0, #10, #16
	bl	P2V_plane
	ldrb	r2, [r4, #8]	@ zero_extendqisi2
	ldr	r3, .L1890
	cmp	r2, #1
	str	r0, [sp, #24]
	bne	.L1823
	ldrb	r1, [r3, #80]	@ zero_extendqisi2
	cmp	r1, #0
	addeq	r7, r3, r7, asl #1
	movweq	r1, #2228
	ldreqh	r7, [r7, r1]
.L1823:
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
	beq	.L1888
.L1825:
	ldr	r3, [sp, #16]
	ldr	fp, [sp, #12]
	sub	r6, r3, #1
	movw	r3, #65535
	cmp	fp, r3
	bne	.L1826
	cmp	r2, #0
	bne	.L1827
.L1826:
	ldr	r2, .L1890+4
	uxth	r3, r5
	ldr	ip, [sp, #20]
	mvn	fp, #0
	mov	sl, r3
	mov	r9, fp
	ldr	r1, [r2, #-1012]
	ldr	r8, .L1890+4
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
	b	.L1830
.L1832:
	ldrh	r0, [r1, #16]
	movw	lr, #65535
	cmp	r0, lr
	beq	.L1831
	ldr	ip, [sp, #16]
	mov	r3, #36
	orr	r0, r5, r0, asl #10
	mla	lr, r3, r6, ip
	add	r6, r6, #1
	uxth	r6, r6
	str	r0, [lr, #4]
.L1831:
	add	r2, r2, #1
	add	r1, r1, #2
	uxth	r2, r2
.L1843:
	ldr	lr, [sp, #44]
	cmp	r2, lr
	bne	.L1832
	ldr	r3, [sp, #36]
	mov	r1, r6
	ldr	r0, [r8, #-984]
	ldr	r2, [sp, #28]
	str	r3, [sp, #4]
	bl	FlashReadPages
	ldr	r2, .L1890
	add	r0, r8, r5, asl #1
	mov	r1, #0
	str	r0, [sp, #16]
	ldr	r3, [sp, #4]
	ldrb	ip, [r2, #80]	@ zero_extendqisi2
	ldr	r2, [r8, #-984]
	str	ip, [sp, #36]
	b	.L1833
.L1841:
	ldr	r0, [r2, #0]
	cmp	r0, #0
	bne	.L1834
	ldr	r0, [r2, #12]
	movw	ip, #65535
	ldrh	lr, [r0, #0]
	cmp	lr, ip
	beq	.L1835
	ldr	r0, [r0, #4]
	cmn	r0, #1
	beq	.L1835
	cmn	fp, #1
	ldr	r9, [r8, #-1012]
	str	r0, [r8, #-1012]
	bne	.L1835
	ldr	lr, [sp, #16]
	sub	r0, lr, #912
	ldrh	r0, [r0, #0]
	cmp	r0, ip
	bne	.L1836
	ldr	r0, [sp, #36]
	cmp	r0, #0
	beq	.L1835
.L1836:
	cmp	r9, r7
	mvneq	fp, #0
	movne	fp, r9
	b	.L1835
.L1834:
	mov	r6, r7
	mov	r7, r3
	ldrb	r3, [r4, #8]	@ zero_extendqisi2
	cmp	r3, #0
	bne	.L1827
	ldr	r3, .L1890+4
	movw	r2, #65535
	add	r5, r3, r5, asl #1
	sub	r5, r5, #912
	ldrh	r1, [r5, #0]
	cmp	r1, r2
	bne	.L1837
	cmn	fp, #1
	bne	.L1883
.L1838:
	ldr	fp, [sp, #12]
	cmp	fp, r6
	beq	.L1839
.L1883:
	str	fp, [r3, #-1012]
	b	.L1827
.L1839:
	ldr	r2, [r3, #-1012]
	b	.L1889
.L1837:
	cmp	r9, r6
	beq	.L1840
	cmn	r9, #1
	strne	r9, [r3, #-1012]
	b	.L1827
.L1840:
	ldr	r2, [r3, #-1012]
	cmp	r2, r6
	beq	.L1827
.L1889:
	sub	r2, r2, #1
	b	.L1884
.L1835:
	add	r1, r1, #1
	add	r2, r2, #36
	uxth	r1, r1
.L1833:
	cmp	r1, r6
	bne	.L1841
	add	r5, r5, #1
	uxth	r5, r5
.L1830:
	cmp	r5, sl
	bhi	.L1842
	ldr	r1, .L1890
	movw	r2, #3844
	ldr	lr, [r8, #-984]
	mov	r6, #0
	str	r3, [sp, #36]
	ldrh	ip, [r1, r2]
	mov	r1, r4
	str	lr, [sp, #16]
	mov	r2, r6
	str	ip, [sp, #44]
	b	.L1843
.L1842:
	mov	r6, r7
	mov	r7, r3
	ldr	r3, .L1890+4
	mvn	r2, #0
.L1884:
	str	r2, [r3, #-1012]
.L1827:
	ldr	r5, .L1890+4
	movw	r3, #266
	mov	r2, #1
	sub	r0, r5, #1088
	strh	r2, [r5, r3]	@ movhi
	bl	FtlMapBlkWriteDumpData
	ldr	r9, [sp, #20]
	str	r7, [sp, #36]
.L1844:
	ldr	r3, .L1890
	movw	r2, #3844
	mov	r7, #0
	ldr	lr, [r5, #-984]
	mov	fp, #36
	ldrh	r8, [r3, r2]
	mov	r2, r4
	ldrb	ip, [r3, #80]	@ zero_extendqisi2
	mov	r3, r7
	b	.L1845
.L1848:
	ldrh	r0, [r2, #16]
	movw	r1, #65535
	cmp	r0, r1
	beq	.L1846
	mla	r1, fp, r7, lr
	orr	r0, r9, r0, asl #10
	str	r0, [r1, #4]
	ldrb	sl, [r4, #8]	@ zero_extendqisi2
	cmp	sl, #1
	bne	.L1847
	cmp	ip, #0
	orrne	r0, r0, #-2147483648
	strne	r0, [r1, #4]
.L1847:
	add	r7, r7, #1
	uxth	r7, r7
.L1846:
	add	r3, r3, #1
	add	r2, r2, #2
	uxth	r3, r3
.L1845:
	cmp	r3, r8
	bne	.L1848
	mov	r1, r7
	ldr	r0, [r5, #-984]
	ldr	r2, [sp, #28]
	bl	FlashReadPages
	mov	r3, #36
	mul	r3, r3, r7
	mov	r7, #0
	str	r3, [sp, #44]
	b	.L1849
.L1870:
	ldr	sl, [r5, #-984]
	add	sl, sl, r7
	ldr	r8, [sl, #4]
	ubfx	r0, r8, #10, #16
	str	r8, [sp, #60]
	bl	P2V_plane
	ldr	r3, [sp, #20]
	cmp	r9, r3
	bcc	.L1850
	ldr	fp, [sp, #32]
	mov	ip, r3
	cmp	r0, fp
	movcs	r3, #0
	movcc	r3, #1
	cmp	r9, ip
	movne	r3, #0
	cmp	r3, #0
	bne	.L1850
	ldr	r3, [sp, #24]
	ldr	ip, [sp, #36]
	cmp	r0, r3
	cmpeq	r9, ip
	beq	.L1851
	ldr	r3, [sl, #0]
	cmn	r3, #1
	beq	.L1852
	ldr	r8, [sl, #12]
	movw	lr, #61589
	ldrh	r3, [r8, #0]
	cmp	r3, lr
	bne	.L1859
.L1853:
	ldr	r6, [r8, #4]
	cmn	r6, #1
	beq	.L1854
	ldr	r1, [r5, #-1844]
	mov	r0, r6
	bl	ftl_cmp_data_ver
	cmp	r0, #0
	addne	r3, r6, #1
	strne	r3, [r5, #-1844]
.L1854:
	ldr	sl, [r8, #8]
	add	r1, sp, #56
	ldr	r3, [r8, #12]
	mov	r2, #0
	mov	r0, sl
	str	r3, [sp, #52]
	bl	log2phys
	ldr	r1, [r5, #-1012]
	cmn	r1, #1
	beq	.L1855
	mov	r0, r6
	bl	ftl_cmp_data_ver
	cmp	r0, #0
	beq	.L1855
	ldr	r3, [sp, #52]
	cmn	r3, #1
	beq	.L1856
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
	bne	.L1857
	b	.L1858
.L1856:
	ldr	r3, [sp, #60]
	ldr	r2, [sp, #56]
	cmp	r2, r3
	bne	.L1859
	mov	r0, sl
	add	r1, sp, #52
	mov	r2, #1
	bl	log2phys
.L1859:
	ldrh	r0, [r4, #0]
	b	.L1886
.L1857:
	ldr	r3, [sp, #12]
	ldr	r8, [r3, #8]
	cmp	r8, sl
	bne	.L1858
	ldr	r0, [r5, #-1012]
	ldr	r1, [sp, #16]
	bl	ftl_cmp_data_ver
	cmp	r0, #0
	beq	.L1858
	ldr	r3, [sp, #56]
	ldr	r2, [sp, #60]
	cmp	r3, r2
	beq	.L1863
.L1860:
	ldr	r2, [sp, #52]
	cmp	r3, r2
	beq	.L1858
	cmn	r3, #1
	streq	r3, [fp, #0]
	beq	.L1862
	ldr	ip, [fp, #12]
	mov	r0, fp
	str	r3, [fp, #4]
	mov	r1, #1
	mov	r2, #0
	str	ip, [sp, #12]
	bl	FlashReadPages
.L1862:
	ldr	r3, [r5, #-984]
	ldr	r3, [r3, r7]
	cmn	r3, #1
	beq	.L1863
	ldr	r3, [sp, #12]
	ldr	r0, [r5, #-1012]
	ldr	sl, [r3, #4]
	mov	r1, sl
	bl	ftl_cmp_data_ver
	cmp	r0, #0
	beq	.L1863
	ldr	r0, [sp, #16]
	mov	r1, sl
	bl	ftl_cmp_data_ver
	cmp	r0, #0
	beq	.L1858
.L1863:
	mov	r0, r8
	ldr	r1, [sp, #52]
	bl	FtlReUsePrevPpa
.L1858:
	ldrh	r0, [r4, #0]
	mvn	r3, #0
	str	r3, [sp, #52]
	bl	decrement_vpc_count
	b	.L1865
.L1855:
	ldr	r3, [sp, #60]
	ldr	r2, [sp, #56]
	cmp	r2, r3
	beq	.L1865
	mov	r0, sl
	add	r1, sp, #60
	mov	r2, #1
	bl	log2phys
	ldr	fp, [sp, #56]
	cmn	fp, #1
	beq	.L1865
	ldr	r3, [sp, #52]
	cmp	fp, r3
	beq	.L1865
	ubfx	r0, fp, #10, #16
	bl	P2V_block_in_plane
	ldr	r3, .L1890+8
	ldrh	r3, [r5, r3]
	cmp	r3, r0
	beq	.L1866
	ldr	r3, .L1890+12
	ldrh	r3, [r5, r3]
	cmp	r3, r0
	beq	.L1866
	ldr	r3, .L1890+16
	ldrh	r3, [r5, r3]
	cmp	r3, r0
	bne	.L1865
.L1866:
	ldr	ip, .L1890+4
	mov	r1, #1
	mov	r2, #0
	ldr	r0, [ip, #-984]
	str	fp, [r0, #4]
	ldr	r8, [r0, #12]
	bl	FlashReadPages
	ldr	r0, .L1890+4
	ldr	r1, [r8, #4]
	ldr	r3, [r0, #-984]
	ldr	r3, [r3, #0]
	cmn	r3, #1
	beq	.L1865
	mov	r0, r6
	bl	ftl_cmp_data_ver
	cmp	r0, #0
	bne	.L1865
	mov	r0, sl
	add	r1, sp, #56
	mov	r2, #1
	bl	log2phys
.L1865:
	ldr	r0, [sp, #52]
	cmn	r0, #1
	beq	.L1850
	ubfx	r0, r0, #10, #16
	bl	P2V_block_in_plane
	ldr	r2, [r5, #-2064]
	mov	r3, r0, asl #1
	mov	r1, r0
	ldrh	r3, [r2, r3]
	cmp	r3, #0
	beq	.L1867
.L1886:
	bl	decrement_vpc_count
	b	.L1850
.L1867:
	ldr	r0, .L1890+20
	bl	printk
	b	.L1850
.L1852:
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
	beq	.L1885
.L1869:
	cmp	r3, r6
	bls	.L1850
.L1885:
	str	r6, [r5, #-1012]
.L1850:
	add	r7, r7, #36
.L1849:
	ldr	r3, [sp, #44]
	cmp	r7, r3
	bne	.L1870
	ldr	r3, .L1890
	add	r9, r9, #1
	movw	r2, #3912
	uxth	r9, r9
	ldrh	r2, [r3, r2]
	cmp	r9, r2
	bne	.L1844
	mov	r2, #0
	movw	r1, #3844
	strh	r2, [r4, #4]	@ movhi
	ldrh	r1, [r3, r1]
	mov	r3, r2
	strh	r9, [r4, #2]	@ movhi
	movw	r2, #65535
	ldr	r0, [sp, #40]
	b	.L1871
.L1873:
	add	r0, r0, #2
	ldrh	ip, [r0, #14]
	cmp	ip, r2
	strneb	r3, [r4, #6]
	bne	.L1807
.L1872:
	add	r3, r3, #1
	uxth	r3, r3
.L1871:
	cmp	r3, r1
	bne	.L1873
	b	.L1807
.L1851:
	ldr	r7, [sp, #36]
	ldr	fp, [sp, #24]
	strh	r7, [r4, #2]	@ movhi
	strb	fp, [r4, #6]
.L1888:
	mov	r0, r4
	mov	r1, r7
	mov	r2, fp
	bl	ftl_sb_update_avl_pages
.L1807:
	mov	r0, #0
	add	sp, sp, #68
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, sl, fp, pc}
.L1891:
	.align	2
.L1890:
	.word	.LANCHOR0
	.word	.LANCHOR2
	.word	-2044
	.word	-1996
	.word	-1948
	.word	.LC113
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
	ldr	r4, .L1901
	ldr	r3, .L1901+4
	mov	r2, r4
	ldrh	r3, [r4, r3]
	cmp	r3, #0
	beq	.L1893
	ldrb	r1, [r4, #-2036]	@ zero_extendqisi2
	cmp	r1, #0
	bne	.L1893
	ldr	r5, .L1901+8
	movw	r0, #3912
	ldrb	r1, [r4, #-2037]	@ zero_extendqisi2
	ldrh	r0, [r5, r0]
	mul	r1, r0, r1
	cmp	r3, r1
	beq	.L1893
	ldrb	r8, [r4, #-2034]	@ zero_extendqisi2
	movw	r3, #3844
	ldr	r7, [r5, #3972]
	cmp	r8, #0
	ldrh	r6, [r5, r3]
	bne	.L1892
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
	beq	.L1895
	add	r0, sp, #4
	mov	r1, #1
	mov	r2, r8
	bl	FlashReadPages
	b	.L1896
.L1895:
	movw	r3, #3922
	mov	r1, #255
	ldrh	r2, [r5, r3]
	bl	ftl_memset
.L1896:
	ldr	r3, .L1901+12
	mov	r6, r6, asl #2
	ldr	r5, .L1901
	ldr	sl, .L1901+4
	strh	r3, [r4, #0]	@ movhi
	ldr	r8, .L1901+16
	b	.L1897
.L1900:
	ldrh	r3, [r5, sl]
	cmp	r3, #0
	beq	.L1898
	ldr	r3, [sp, #8]
	sub	r6, r6, #1
	str	r7, [r4, #8]
	ldr	r0, .L1901+20
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
.L1897:
	cmp	r6, #0
	bne	.L1900
.L1898:
	ldr	r3, .L1901
	mov	r2, #1
	strb	r2, [r3, #-2034]
	b	.L1892
.L1893:
	mov	r3, #0
	strb	r3, [r2, #-2034]
.L1892:
	add	sp, sp, #44
	ldmfd	sp!, {r4, r5, r6, r7, r8, sl, pc}
.L1902:
	.align	2
.L1901:
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
	ldr	r7, .L1907
	movw	r6, #3950
	ldr	r5, .L1907+4
	b	.L1904
.L1906:
	ldr	r3, [r5, #-1900]
	mov	r2, #12
	mla	r3, r2, r4, r3
	ldr	r3, [r3, #4]
	cmp	r3, #0
	bge	.L1905
	mov	r0, r4
	bl	flush_l2p_region
.L1905:
	add	r4, r4, #1
	uxth	r4, r4
.L1904:
	ldrh	r3, [r7, r6]
	cmp	r3, r4
	bhi	.L1906
	mov	r0, #0
	ldmfd	sp!, {r3, r4, r5, r6, r7, pc}
.L1908:
	.align	2
.L1907:
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
	ldr	r6, .L1916
	ldr	r1, .L1916+4
	ldr	r0, .L1916+8
	bl	printk
	movw	r3, #3854
	ldrh	r2, [r6, r3]
	mov	r1, #0
	ldr	r5, .L1916+12
	mov	r2, r2, asl #1
	ldr	r0, [r5, #-932]
	bl	ftl_memset
	b	.L1910
.L1912:
	mov	r0, r4
	add	r1, sp, #4
	mov	r2, #0
	bl	log2phys
	ldr	r0, [sp, #4]
	cmn	r0, #1
	beq	.L1911
	ubfx	r0, r0, #10, #16
	bl	P2V_block_in_plane
	ldr	r3, [r5, #-932]
	mov	r0, r0, asl #1
	ldrh	r2, [r3, r0]
	add	r2, r2, #1
	strh	r2, [r3, r0]	@ movhi
.L1911:
	add	r4, r4, #1
.L1910:
	ldr	r3, [r6, #3972]
	cmp	r4, r3
	bcc	.L1912
	mov	r4, #0
	ldr	r7, .L1916
	movw	r8, #3852
	ldr	r5, .L1916+12
	b	.L1913
.L1915:
	ldr	r3, [r5, #-2064]
	mov	r6, r4, asl #1
	ldrh	r2, [r3, r6]
	ldr	r3, [r5, #-932]
	ldrh	r3, [r3, r6]
	cmp	r2, r3
	beq	.L1914
	movw	r1, #65535
	cmp	r2, r1
	beq	.L1914
	ldr	r0, .L1916+16
	mov	r1, r4
	bl	printk
	ldr	r3, .L1916+20
	ldrh	r3, [r5, r3]
	cmp	r3, r4
	beq	.L1914
	ldr	r3, .L1916+24
	ldrh	r3, [r5, r3]
	cmp	r3, r4
	beq	.L1914
	ldr	r3, .L1916+28
	ldrh	r3, [r5, r3]
	cmp	r3, r4
	beq	.L1914
	ldr	r3, [r5, #-932]
	mov	r0, r4
	ldrh	r2, [r3, r6]
	ldr	r3, [r5, #-2064]
	strh	r2, [r3, r6]	@ movhi
	bl	update_vpc_list
	bl	l2p_flush
	bl	FtlVpcTblFlush
.L1914:
	add	r4, r4, #1
	uxth	r4, r4
.L1913:
	ldrh	r3, [r7, r8]
	cmp	r3, r4
	bhi	.L1915
	ldmfd	sp!, {r2, r3, r4, r5, r6, r7, r8, pc}
.L1917:
	.align	2
.L1916:
	.word	.LANCHOR0
	.word	.LANCHOR3+15
	.word	.LC114
	.word	.LANCHOR2
	.word	.LC115
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
	beq	.L1919
	ldr	r3, .L1929
	mov	r0, r5
	ldr	r2, [r3, #-2064]
	mov	r3, r5, asl #1
	ldrh	r3, [r2, r3]
	cmp	r3, #0
	beq	.L1920
	bl	INSERT_DATA_LIST
	b	.L1919
.L1920:
	bl	INSERT_FREE_LIST
.L1919:
	mov	r3, #0
	strb	r3, [r4, #8]
	ldr	r3, .L1929+4
	cmp	r4, r3
	beq	.L1921
	ldr	r2, .L1929+8
	movw	r3, #3864
	ldrh	r3, [r2, r3]
	cmp	r3, #1
	beq	.L1921
	ldrb	r2, [r2, #80]	@ zero_extendqisi2
	cmp	r2, #0
	beq	.L1922
.L1921:
	mov	r3, #1
	strb	r3, [r4, #8]
	b	.L1923
.L1922:
	ldr	r2, .L1929+12
	cmp	r4, r2
	bne	.L1923
	cmp	r3, #3
	ldr	r3, .L1929
	beq	.L1924
	ldr	r2, [r3, #-1708]
	cmp	r2, #1
	bne	.L1925
.L1924:
	mov	r2, #1
	strb	r2, [r3, #-2036]
.L1925:
	ldr	r3, [r3, #-1884]
	ldr	r2, .L1929
	cmp	r3, #0
	beq	.L1923
	ldr	r3, [r2, #-1824]
	cmp	r3, #29
	movls	r3, #1
	strlsb	r3, [r2, #-2036]
.L1923:
	ldr	r3, .L1929+16
	ldr	r6, .L1929
	ldrh	r0, [r6, r3]
	movw	r3, #65535
	cmp	r0, r3
	beq	.L1926
	cmp	r5, r0
	bne	.L1927
	ldr	r2, [r6, #-2064]
	mov	r3, r0, asl #1
	ldrh	r3, [r2, r3]
	cmp	r3, #0
	beq	.L1928
.L1927:
	bl	update_vpc_list
.L1928:
	ldr	r3, .L1929+16
	mvn	r2, #0
	strh	r2, [r6, r3]	@ movhi
.L1926:
	mov	r0, r4
	bl	allocate_data_superblock
	bl	l2p_flush
	bl	FtlVpcTblFlush
	mov	r0, #0
	bl	FtlEctTblFlush
	mov	r0, #0
	ldmfd	sp!, {r4, r5, r6, pc}
.L1930:
	.align	2
.L1929:
	.word	.LANCHOR2
	.word	.LANCHOR2-1996
	.word	.LANCHOR0
	.word	.LANCHOR2-2044
	.word	-1024
	.fnend
	.size	allocate_new_data_superblock, .-allocate_new_data_superblock
	.align	2
	.type	rk_ftl_garbage_collect.part.19, %function
rk_ftl_garbage_collect.part.19:
	.fnstart
	@ args = 0, pretend = 0, frame = 24
	@ frame_needed = 0, uses_anonymous_args = 0
	ldr	r3, .L2027
	movw	ip, #65535
	ldr	r2, .L2027+4
	stmfd	sp!, {r4, r5, r6, r7, r8, r9, sl, fp, lr}
	.save {r4, r5, r6, r7, r8, r9, sl, fp, lr}
	mov	r7, r0
	ldrh	r0, [r3, r2]
	.pad #36
	sub	sp, sp, #36
	cmp	r0, ip
	str	r1, [sp, #20]
	beq	.L1932
	ldr	r1, .L2027+8
	ldrh	r4, [r3, r1]
	cmp	r4, ip
	streqh	r0, [r3, r1]	@ movhi
	mvneq	r1, #0
	streqh	r1, [r3, r2]	@ movhi
.L1932:
	ldr	r2, [r3, #-1188]
	cmp	r7, #1
	ldr	r5, .L2027
	add	r2, r2, #1
	add	r2, r2, r7, asl #7
	str	r2, [r3, #-1188]
	bne	.L2007
	ldr	r3, [r5, #-1884]
	cmp	r3, #0
	beq	.L2007
	ldr	r3, [r5, #-1824]
	cmp	r3, #29
	bhi	.L2007
	mov	r6, #400
	movw	r4, #65535
	ldrh	r3, [r5, r6]
	add	r2, r2, r3
	str	r2, [r5, #-1188]
	bl	FtlGcReFreshBadBlk
	ldr	r3, .L2027+12
	ldrh	r3, [r5, r3]
	cmp	r3, r4
	bne	.L1933
	ldr	r2, .L2027+8
	ldrh	r4, [r5, r2]
	cmp	r4, r3
	movne	r4, r3
	bne	.L1933
	ldr	r3, [r5, #-1188]
	cmp	r3, #1024
	bls	.L1933
	mov	r3, #0
	str	r3, [r5, #-1188]
	strh	r3, [r5, r6]	@ movhi
	ldr	r3, [r5, #-1824]
	cmp	r3, #0
	moveq	r3, #6
	beq	.L2022
	cmp	r3, #5
	bhi	.L1935
	mov	r3, #18
.L2022:
	strh	r3, [r5, r6]	@ movhi
.L1935:
	mov	r0, #32
	movw	r8, #65535
	bl	List_get_gc_head_node
	uxth	r2, r0
	cmp	r2, r8
	beq	.L1936
	ldr	r5, .L2027
	ldr	r6, .L2027+16
	ldrh	r0, [r5, r6]
	cmp	r0, #0
	moveq	r3, #1
	streqh	r3, [r5, r6]	@ movhi
	beq	.L1936
	ldr	r3, .L2027+20
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
	bgt	.L1936
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
	beq	.L1936
	ldrh	r2, [r9, r2]
	mov	r8, r4, asl #1
	ldr	r0, .L2027+24
	mov	r1, fp
	ldrh	r3, [r9, r8]
	str	r2, [sp, #0]
	mov	r2, r4
	bl	printk
	ldrh	r3, [r5, r6]
	cmp	r3, #40
	bls	.L1938
	ldr	r3, [r5, #-2064]
	ldrh	r3, [r3, r8]
	cmp	r3, #32
	strhih	sl, [r5, r6]	@ movhi
.L1938:
	mov	r1, #6
	b	.L2023
.L1936:
	bl	GetSwlReplaceBlock
	movw	r3, #65535
	cmp	r0, r3
	mov	r4, r0
	bne	.L1933
	mov	r1, #0
.L2023:
	ldr	r2, .L2027
	mov	r3, #400
	strh	r1, [r2, r3]	@ movhi
	b	.L1933
.L2007:
	movw	r4, #65535
.L1933:
	ldr	r3, .L2027+12
	ldr	r6, .L2027
	ldrh	r2, [r6, r3]
	movw	r3, #65535
	cmp	r2, r3
	bne	.L1939
	ldr	r3, .L2027+28
	ldrh	r3, [r6, r3]
	cmp	r3, r2
	bne	.L1939
	ldr	r2, .L2027+8
	ldrh	sl, [r6, r2]
	cmp	sl, r3
	bne	.L1939
	ldr	r8, .L2027+32
	ldr	r2, [r6, #-1188]
	ldrh	r3, [r6, r8]
	cmp	r3, #23
	movhi	r3, #1024
	movls	r3, #5120
	cmp	r2, r3
	bls	.L1939
	mov	r2, #400
	mov	r3, #0
	str	r3, [r6, #-1188]
	strh	r3, [r6, r2]	@ movhi
	bl	GetSwlReplaceBlock
	cmp	r0, sl
	mov	r4, r0
	bne	.L1941
	ldr	r3, .L2027+36
	ldrh	r1, [r6, r8]
	ldrh	r2, [r6, r3]
	cmp	r1, r2
	movcs	r2, #80
	strcsh	r2, [r6, r3]	@ movhi
	bcs	.L1951
	mov	r0, #64
	bl	List_get_gc_head_node
	uxth	r3, r0
	cmp	r3, r4
	beq	.L1951
	ldr	r2, [r6, #-1004]
	cmp	r2, #0
	bne	.L1944
	ldr	r2, .L2027+20
	movw	r1, #3864
	ldrh	r1, [r2, r1]
	cmp	r1, #3
	beq	.L1944
	ldr	r1, [r6, #-1708]
	cmp	r1, #0
	bne	.L1944
	ldr	r1, [r6, #-1884]
	cmp	r1, #0
	bne	.L1944
	ldrb	r0, [r2, #80]	@ zero_extendqisi2
	cmp	r0, #0
	beq	.L1945
.L1944:
	ldr	r1, .L2027
	mov	r3, r3, asl #1
	movw	ip, #3844
	movw	lr, #3864
	ldr	r2, [r1, #-2064]
	ldrh	r0, [r2, r3]
	movw	r2, #3914
	ldr	r3, .L2027+20
	ldrh	r2, [r3, r2]
	ldrh	ip, [r3, ip]
	ldrh	r3, [r3, lr]
	mul	ip, ip, r2
	cmp	r3, #3
	moveq	r3, r2, lsr #1
	movne	r3, #0
	add	r3, ip, r3
	cmp	r0, r3
	bgt	.L1947
	mov	r0, #0
	bl	List_get_gc_head_node
	ldr	r2, .L2027+20
	ldr	r3, .L2027
	ldr	r2, [r2, #3972]
	ldr	r1, [r3, #-1892]
	add	r2, r2, r2, asl #1
	cmp	r1, r2, lsr #2
	ldr	r2, .L2027+36
	movhi	r1, #128
	movls	r1, #160
	strh	r1, [r3, r2]	@ movhi
	uxth	r4, r0
	b	.L1949
.L1947:
	ldr	r3, .L2027+36
	mov	r2, #128
	strh	r2, [r1, r3]	@ movhi
	b	.L1951
.L1945:
	ldr	r2, [r6, #-2064]
	mov	r3, r3, asl #1
	ldr	r6, .L2027
	ldr	r5, .L2027+36
	ldrh	r3, [r2, r3]
	cmp	r3, #7
	movhi	r3, #64
	strhih	r3, [r6, r5]	@ movhi
	bhi	.L1951
	bl	List_get_gc_head_node
	mov	r3, #128
	strh	r3, [r6, r5]	@ movhi
	uxth	r4, r0
.L1949:
	movw	r3, #65535
	cmp	r4, r3
	beq	.L1951
.L1941:
	ldr	r1, .L2027
	mov	r0, r4, asl #1
	ldr	r3, .L2027+32
	ldr	ip, [r1, #-2084]
	ldrh	r2, [r1, r3]
	ldr	r3, [r1, #-2064]
	ldrh	r3, [r3, r0]
	ldrh	r0, [ip, r0]
	str	r0, [sp, #0]
	ldr	r0, .L2027+40
	ldrh	r1, [r1, r0]
	ldr	r0, .L2027+44
	str	r1, [sp, #4]
	mov	r1, r4
	bl	printk
.L1951:
	bl	FtlGcReFreshBadBlk
.L1939:
	movw	r1, #65535
	rsb	ip, r1, r4
	rsbs	r2, ip, #0
	ldr	r5, .L2027
	adc	r2, r2, ip
	cmp	r7, #0
	movne	r7, #0
	andeq	r7, r2, #1
	cmp	r7, #0
	beq	.L1952
	ldr	r3, .L2027+32
	ldrh	r2, [r5, r3]
	cmp	r2, #24
	movhi	r6, #1
	bhi	.L1953
	ldr	r1, .L2027+20
	movw	r3, #3912
	cmp	r2, #16
	ldrh	r6, [r1, r3]
	movhi	r6, r6, lsr #5
	bhi	.L1953
	cmp	r2, #12
	movhi	r6, r6, lsr #4
	bhi	.L1953
	cmp	r2, #8
	movhi	r6, r6, lsr #2
.L1953:
	ldr	r1, .L2027+40
	ldr	r3, .L2027
	ldrh	r0, [r5, r1]
	cmp	r0, r2
	mov	r0, r1
	bcs	.L1957
	ldr	r2, .L2027+28
	movw	ip, #65535
	ldrh	r2, [r3, r2]
	cmp	r2, ip
	bne	.L1958
	ldr	ip, .L2027+8
	ldrh	ip, [r3, ip]
	cmp	ip, r2
	bne	.L1958
	mov	r2, #400
	ldrh	r0, [r3, r2]
	cmp	r0, #0
	bne	.L1959
	ldr	r2, .L2027+20
	ldr	ip, [r3, #-1892]
	ldr	r2, [r2, #3972]
	add	r2, r2, r2, asl #1
	cmp	ip, r2, lsr #2
	movcs	r2, #18
	bcs	.L2025
.L1959:
	ldr	r3, .L2027
	ldr	r2, .L2027+48
	ldr	r1, .L2027+40
	ldrh	r2, [r3, r2]
	add	r2, r2, r2, asl #1
	mov	r2, r2, asr #2
.L2025:
	strh	r2, [r3, r1]	@ movhi
	mov	r2, #0
	ldr	r3, .L2027
	str	r2, [r3, #-1180]
	b	.L1962
.L1958:
	ldr	r3, .L2027
	ldr	r2, .L2027+48
	ldrh	r2, [r3, r2]
	add	r2, r2, r2, asl #1
	mov	r2, r2, asr #2
	strh	r2, [r3, r0]	@ movhi
.L1957:
	ldr	r3, .L2027
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
	b	.L1963
.L1952:
	ldr	r3, .L2027+28
	ldrh	r3, [r5, r3]
	cmp	r3, r1
	bne	.L1964
	ldr	r1, .L2027+8
	ldrh	r1, [r5, r1]
	cmp	r1, r3
	movne	r2, #0
	andeq	r2, r2, #1
	cmp	r2, #0
	beq	.L1964
	ldr	r2, .L2027+12
	ldrh	r4, [r5, r2]
	cmp	r4, r3
	movne	r4, r3
	bne	.L1964
	ldr	r3, .L2027+32
	mov	r6, #400
	str	r7, [r5, #-1180]
	ldrh	r7, [r5, r3]
	add	r3, r3, #856
	ldrh	r2, [r5, r3]
	cmp	r2, r7
	bcs	.L1965
	ldrh	r2, [r5, r6]
	cmp	r2, #0
	bne	.L1966
	ldr	r2, .L2027+20
	ldr	r1, [r5, #-1892]
	ldr	r2, [r2, #3972]
	add	r2, r2, r2, asl #1
	cmp	r1, r2, lsr #2
	movcs	r2, #18
	strcsh	r2, [r5, r3]	@ movhi
	bcs	.L1968
.L1966:
	ldr	r3, .L2027
	ldr	r2, .L2027+48
	ldr	r1, .L2027+40
	ldrh	r2, [r3, r2]
	add	r2, r2, r2, asl #1
	mov	r2, r2, asr #2
	strh	r2, [r3, r1]	@ movhi
.L1968:
	bl	FtlReadRefresh
	mov	r3, #400
	ldr	r2, .L2027
	b	.L2026
.L1965:
	ldrh	r0, [r5, r6]
	cmp	r0, #0
	bne	.L1964
	ldr	r2, .L2027+48
	ldrh	r4, [r5, r2]
	add	r2, r4, r4, asl #1
	mov	r2, r2, asr #2
	strh	r2, [r5, r3]	@ movhi
	bl	List_get_gc_head_node
	ldr	r3, [r5, #-2064]
	ldr	r1, .L2027+20
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
	ble	.L1969
	sub	r4, r4, #1
	cmp	r7, r4
	blt	.L1969
	bl	FtlReadRefresh
	ldrh	r0, [r5, r6]
	b	.L1962
.L1969:
	cmp	r3, #0
	movwne	r4, #65535
	bne	.L1964
	movw	r0, #65535
	bl	decrement_vpc_count
	ldr	r3, .L2027+32
	ldr	r2, .L2027
	ldrh	r0, [r2, r3]
	add	r0, r0, #1
	b	.L1962
.L1964:
	ldr	r3, .L2027
	ldr	r6, [r3, #-1884]
	cmp	r6, #0
	movne	r6, #2
	moveq	r6, #1
.L1963:
	ldr	r3, .L2027
	movw	r0, #65535
	ldr	r2, .L2027+12
	ldrh	r1, [r3, r2]
	cmp	r1, r0
	bne	.L1970
	cmp	r4, r1
	strneh	r4, [r3, r2]	@ movhi
	bne	.L1972
	ldr	r1, .L2027+8
	ldrh	r0, [r3, r1]
	cmp	r0, r4
	strneh	r0, [r3, r2]	@ movhi
	mvnne	r2, #0
	strneh	r2, [r3, r1]	@ movhi
.L1972:
	mov	r2, #0
	strb	r2, [r3, #-1748]
	ldr	r3, .L2027+20
	ldr	r5, .L2027
	ldr	r7, .L2027+12
	ldrb	r3, [r3, #80]	@ zero_extendqisi2
	cmp	r3, r2
	beq	.L1973
	ldrh	r0, [r5, r7]
	bl	ftl_get_blk_mode
	strb	r0, [r5, #-1748]
.L1973:
	ldr	r5, .L2027
	ldrh	r0, [r5, r7]
	bl	IsBlkInGcList
	ldr	r7, .L2027+12
	mov	r3, r5
	cmp	r0, #0
	ldrne	r2, .L2027+12
	mvnne	r1, #0
	strneh	r1, [r5, r2]	@ movhi
	ldrh	r2, [r3, r7]
	movw	r3, #65535
	ldr	r5, .L2027
	cmp	r2, r3
	beq	.L1970
	ldr	r0, .L2027+52
	bl	make_superblock
	ldr	r1, .L2027+56
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
.L1970:
	ldr	r3, .L2027
	ldr	r0, .L2027+12
	ldr	r2, .L2027+60
	ldrh	r1, [r3, r0]
	ldrh	r2, [r3, r2]
	cmp	r2, r1
	mov	r2, r3
	beq	.L1975
	ldr	ip, .L2027+64
	ldrh	r3, [r3, ip]
	cmp	r3, r1
	strne	r4, [sp, #20]
	bne	.L2021
.L1976:
.L1975:
	mvn	r3, #0
	strh	r3, [r2, r0]	@ movhi
	mov	r3, #400
.L2026:
	ldrh	r0, [r2, r3]
	b	.L1962
.L2021:
	ldr	r4, .L2027
	movw	r0, #65535
	ldr	r9, .L2027+12
	ldrh	r3, [r4, r9]
	cmp	r3, r0
	bne	.L1978
	mov	r3, #0
	str	r3, [r4, #-1180]
.L2020:
	ldr	r8, .L2027+16
	ldrh	r7, [r4, r8]
	mov	r0, r7
	bl	List_get_gc_head_node
	movw	r1, #65535
	uxth	r5, r0
	strh	r5, [r4, r9]	@ movhi
	cmp	r5, r1
	ldreq	r3, .L2027
	moveq	r2, #0
	moveq	r0, #8
	streqh	r2, [r3, r8]	@ movhi
	beq	.L1962
.L1980:
	mov	r0, r5
	add	r7, r7, #1
	bl	IsBlkInGcList
	cmp	r0, #0
	strneh	r7, [r4, r8]	@ movhi
	bne	.L2020
	ldr	ip, .L2027+20
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
	bgt	.L1983
	cmp	r7, #48
	bls	.L1984
	cmp	r0, #8
	bls	.L1984
	ldr	r0, .L2027+68
	ldrh	r0, [r4, r0]
	cmp	r0, #35
	bhi	.L1984
.L1983:
	ldr	r0, .L2027+16
	mov	ip, #0
	strh	ip, [r4, r0]	@ movhi
.L1984:
	ldrh	r2, [r1, r2]
	movw	r0, #65535
	ldr	r8, [sp, #20]
	ldr	sl, .L2027
	cmp	r2, r3
	cmpge	r8, r0
	movne	r3, #0
	moveq	r3, #1
	bne	.L1985
	ldr	r3, .L2027+12
	mvn	r2, #0
	strh	r2, [sl, r3]	@ movhi
	add	r3, r3, #572
	mov	r2, #0
	strh	r2, [sl, r3]	@ movhi
	mov	r3, #400
	ldrh	r0, [sl, r3]
	b	.L1962
.L1985:
	cmp	r2, #0
	bne	.L1986
	movw	r0, #65535
	bl	decrement_vpc_count
	ldr	r3, .L2027+16
	ldrh	r2, [r4, r3]
	add	r2, r2, #1
	strh	r2, [r4, r3]	@ movhi
	b	.L2020
.L1986:
	strb	r3, [sl, #-1748]
	ldr	r3, .L2027+20
	ldrb	r3, [r3, #80]	@ zero_extendqisi2
	cmp	r3, #0
	beq	.L1987
	mov	r0, r5
	bl	ftl_get_blk_mode
	strb	r0, [sl, #-1748]
.L1987:
	ldr	r4, .L2027
	ldr	r0, .L2027+52
	bl	make_superblock
	movw	r1, #402
	mov	r3, #0
	strh	r3, [r4, r1]	@ movhi
	ldr	r1, .L2027+12
	ldr	r0, [r4, #-2064]
	ldrh	r1, [r4, r1]
	mov	r1, r1, asl #1
	ldrh	r0, [r0, r1]
	mov	r1, #404
	strb	r3, [r4, #-1750]
	strh	r0, [r4, r1]	@ movhi
	ldr	r1, .L2027+56
	strh	r3, [r4, r1]	@ movhi
.L1978:
	bl	FtlReadRefresh
	ldr	r3, .L2027
	mov	r2, #1
	movw	r1, #3912
	str	r2, [r3, #-1000]
	ldr	r2, .L2027+20
	ldrh	r9, [r2, r1]
	ldrb	r1, [r2, #80]	@ zero_extendqisi2
	cmp	r1, #0
	beq	.L1988
	ldrb	r1, [r3, #-1748]	@ zero_extendqisi2
	cmp	r1, #1
	movweq	r1, #3914
	ldreqh	r9, [r2, r1]
.L1988:
	ldr	r2, .L2027+56
	mov	sl, #0
	ldr	r4, .L2027
	mov	fp, r9
	ldrh	r3, [r3, r2]
	add	r2, r3, r6
	cmp	r2, r9
	rsbgt	r6, r3, r9
	uxthgt	r6, r6
	b	.L1990
.L1992:
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
.L1998:
	cmp	r3, r5
	bne	.L1992
	ldr	r0, [r4, #-1160]
	mov	r1, r7
	ldrb	r2, [r4, #-1748]	@ zero_extendqisi2
	mov	r5, #0
	bl	FlashReadPages
	mov	r9, r5
	mov	r8, r6
	b	.L1993
.L1996:
	ldr	r3, [r4, #-1160]
	add	r2, r3, r5
	ldr	r3, [r3, r5]
	ldr	r6, [r2, #12]
	cmn	r3, #1
	beq	.L1994
	ldrh	r3, [r6, #0]
	movw	r0, #61589
	cmp	r3, r0
	bne	.L1994
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
	bne	.L1994
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
	ldr	r3, .L2027+28
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
	ldr	r3, .L2027+20
	ldrb	r3, [r3, #80]	@ zero_extendqisi2
	cmp	r3, #0
	bne	.L1995
	ldrb	r3, [r4, #-1941]	@ zero_extendqisi2
	ldr	r2, [r4, #-1176]
	cmp	r2, r3
	beq	.L1995
	ldr	r3, .L2027+72
	ldrh	r3, [r4, r3]
	cmp	r3, #0
	bne	.L1994
.L1995:
	bl	Ftl_gc_temp_data_write_back
	cmp	r0, #0
	ldrne	r3, .L2027
	movne	r2, #0
	strne	r2, [r3, #-1000]
	movne	r2, #400
	bne	.L2026
.L1994:
	add	r9, r9, #1
	add	r5, r5, #36
	uxth	r9, r9
.L1993:
	cmp	r9, r7
	bne	.L1996
	add	sl, sl, #1
	mov	r6, r8
.L1990:
	uxth	r3, sl
	ldr	r2, .L2027+56
	cmp	r3, r6
	ldr	r3, .L2027
	bcs	.L1997
	ldr	r1, .L2027+20
	movw	r3, #3844
	ldrh	ip, [r4, r2]
	mov	r7, #0
	ldr	lr, [r4, #-1160]
	mov	r0, #36
	ldrh	r5, [r1, r3]
	add	ip, ip, sl
	ldr	r2, .L2027+76
	mov	r3, r7
	b	.L1998
.L1997:
	ldrh	r1, [r3, r2]
	mov	r9, fp
	add	r6, r6, r1
	uxth	r6, r6
	strh	r6, [r3, r2]	@ movhi
	cmp	r6, fp
	bcs	.L1999
	movw	r2, #402
	mov	r1, #404
	ldrh	r2, [r3, r2]
	ldrh	r3, [r3, r1]
	cmp	r2, r3
	bne	.L2000
.L1999:
	ldr	r5, .L2027
	ldr	r3, [r5, #-1176]
	cmp	r3, #0
	beq	.L2001
	bl	Ftl_gc_temp_data_write_back
	cmp	r0, #0
	movne	r3, #0
	strne	r3, [r5, #-1000]
	movne	r3, #400
	ldrneh	r0, [r5, r3]
	bne	.L1962
.L2001:
	movw	r2, #402
	ldr	r3, .L2027
	ldrh	ip, [r5, r2]
	cmp	ip, #0
	bne	.L2002
	ldr	r2, .L2027+12
	ldr	r0, [r3, #-2064]
	ldrh	r1, [r3, r2]
	mov	r1, r1, asl #1
	ldrh	r4, [r0, r1]
	cmp	r4, #0
	beq	.L2002
	strh	ip, [r0, r1]	@ movhi
	ldrh	r0, [r3, r2]
	bl	update_vpc_list
	bl	FtlCacheWriteBack
	bl	l2p_flush
	bl	FtlVpcTblFlush
.L2002:
	ldr	r3, .L2027+12
	mvn	r1, #0
	ldr	r2, .L2027
	strh	r1, [r2, r3]	@ movhi
.L2000:
	ldr	r3, .L2027
	ldr	r2, .L2027+32
	ldrh	r2, [r3, r2]
	cmp	r2, #2
	ldrls	r2, .L2027+20
	movwls	r3, #3912
	ldrlsh	r6, [r2, r3]
	bls	.L2021
.L2003:
	mov	r1, #0
	str	r1, [r3, #-1000]
	mov	r1, #400
	ldrh	r0, [r3, r1]
	cmp	r0, #0
	addeq	r0, r2, #1
.L1962:
	add	sp, sp, #36
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, sl, fp, pc}
.L2028:
	.align	2
.L2027:
	.word	.LANCHOR2
	.word	-1136
	.word	-1138
	.word	-1756
	.word	-1184
	.word	.LANCHOR0
	.word	.LC116
	.word	-1948
	.word	-2048
	.word	-1190
	.word	-1192
	.word	.LC117
	.word	-1772
	.word	.LANCHOR2-1756
	.word	-1754
	.word	-2044
	.word	-1996
	.word	-1152
	.word	-1944
	.word	.LANCHOR2-1742
	.fnend
	.size	rk_ftl_garbage_collect.part.19, .-rk_ftl_garbage_collect.part.19
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
	ldr	r3, .L2035
	mov	r5, r1
	ldr	r0, [r3, #-1000]
	cmp	r0, #0
	movne	r0, #0
	ldmnefd	sp!, {r3, r4, r5, pc}
	ldr	r2, .L2035+4
	ldrh	r2, [r3, r2]
	cmp	r2, #47
	ldmlsfd	sp!, {r3, r4, r5, pc}
	ldr	r1, .L2035+8
	movw	r2, #3000
	ldrh	r1, [r1, r2]
	movw	r2, #65535
	cmp	r1, r2
	beq	.L2031
	ldr	r1, .L2035+12
	ldrh	r3, [r3, r1]
	cmp	r3, r2
	beq	.L2031
	mov	r0, #1
	bl	FtlGcFreeTempBlock
	cmp	r0, #0
	bne	.L2034
.L2031:
	mov	r0, r4
	mov	r1, r5
	ldmfd	sp!, {r3, r4, r5, lr}
	b	rk_ftl_garbage_collect.part.19
.L2034:
	mov	r0, #1
	ldmfd	sp!, {r3, r4, r5, pc}
.L2036:
	.align	2
.L2035:
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
	ldr	r3, .L2059
	stmfd	sp!, {r0, r1, r4, r5, r6, r7, r8, r9, sl, lr}
	.save {r0, r1, r4, r5, r6, r7, r8, r9, sl, lr}
	ldr	r4, [r3, #408]
	ldr	r3, .L2059+4
	ldr	r1, [r3, #3964]
	cmp	r1, #0
	beq	.L2038
	ldrb	r8, [r3, #80]	@ zero_extendqisi2
	cmp	r8, #0
	beq	.L2039
	ldrb	r8, [r4, #8]	@ zero_extendqisi2
	sub	lr, r8, #1
	rsbs	r8, lr, #0
	adc	r8, r8, lr
.L2039:
	ldr	r0, [r3, #3968]
	mov	r2, r8
	ldrb	r3, [r4, #9]	@ zero_extendqisi2
	mov	r5, #0
	bl	FlashProgPages
	mov	r7, r5
	ldr	r6, .L2059+4
	ldr	r9, .L2059
	b	.L2040
.L2045:
	ldr	r2, [r6, #3968]
	add	r3, r2, r5
	ldr	r2, [r2, r5]
	cmn	r2, #1
	beq	.L2058
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
	beq	.L2043
	ubfx	r0, r0, #10, #16
	bl	P2V_block_in_plane
	ldr	r2, [r9, #-2064]
	mov	r3, r0, asl #1
	mov	sl, r0
	ldrh	r2, [r2, r3]
	cmp	r2, #0
	bne	.L2044
	ldr	r0, .L2059+8
	mov	r1, sl
	bl	printk
.L2044:
	mov	r0, sl
	bl	decrement_vpc_count
.L2043:
	add	r7, r7, #1
	add	r5, r5, #36
.L2040:
	ldr	r3, [r6, #3964]
	cmp	r7, r3
	bcc	.L2045
	b	.L2046
.L2054:
	ldr	r3, [r6, #3968]
	mvn	r2, #0
	movw	sl, #3912
	str	r2, [r3, r5]
	b	.L2047
.L2050:
	ldr	r0, [r3, #4]
	ubfx	r0, r0, #10, #16
	bl	P2V_block_in_plane
	ldrh	r3, [r4, #0]
	cmp	r3, r0
	bne	.L2048
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
.L2048:
	ldrh	r3, [r4, #4]
	cmp	r3, #0
	bne	.L2049
	mov	r0, r4
	bl	allocate_new_data_superblock
.L2049:
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
.L2047:
	ldr	r2, [r6, #3968]
	add	r3, r2, r5
	ldr	r2, [r2, r5]
	cmn	r2, #1
	beq	.L2050
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
	beq	.L2052
	ubfx	r0, r0, #10, #16
	bl	P2V_block_in_plane
	ldr	r2, [r7, #-2064]
	mov	r3, r0, asl #1
	mov	sl, r0
	ldrh	r2, [r2, r3]
	cmp	r2, #0
	bne	.L2053
	ldr	r0, .L2059+8
	mov	r1, sl
	bl	printk
.L2053:
	mov	r0, sl
	bl	decrement_vpc_count
.L2052:
	add	r9, r9, #1
	add	r5, r5, #36
	b	.L2041
.L2058:
	ldr	r6, .L2059+4
	mov	r5, #0
	ldr	r7, .L2059
	mov	r9, r5
.L2041:
	ldr	r3, [r6, #3964]
	cmp	r9, r3
	bcc	.L2054
	movw	r4, #16386
	ldr	r6, .L2059
	ldr	r5, .L2059+12
	b	.L2055
.L2056:
	mov	r0, #1
	mov	r1, r0
	bl	rk_ftl_garbage_collect
	subs	r4, r4, #1
	beq	.L2046
.L2055:
	ldrh	r3, [r6, r5]
	cmp	r3, #0
	bne	.L2056
.L2046:
	ldr	r3, .L2059+4
	mov	r2, #0
	str	r2, [r3, #3964]
.L2038:
	mov	r0, #0
	ldmfd	sp!, {r2, r3, r4, r5, r6, r7, r8, r9, sl, pc}
.L2060:
	.align	2
.L2059:
	.word	.LANCHOR2
	.word	.LANCHOR0
	.word	.LC118
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
	ldr	r2, .L2083
	ldrh	r1, [r2, r3]
	ldr	r2, .L2083+4
	ldr	r3, .L2083+8
	ldrh	r4, [r3, r2]
	movw	r2, #65535
	cmp	r4, r2
	beq	.L2063
	cmp	r0, #0
	beq	.L2064
	ldr	ip, .L2083+12
	movw	r0, #3000
	ldrh	lr, [ip, r0]
	cmp	lr, r2
	movne	r1, #2
	bne	.L2064
	mov	r2, #0
	strh	r2, [ip, r0]	@ movhi
	sub	r2, r2, #2048
	ldrh	r3, [r3, r2]
	cmp	r3, #17
	movhi	r1, #2
.L2064:
	ldr	r0, .L2083+16
	bl	FtlGcScanTempBlk
	cmn	r0, #1
	str	r0, [sp, #4]
	beq	.L2065
	ldr	r3, .L2083+8
	mov	r4, r4, asl #1
	ldr	r3, [r3, #-2084]
	ldrh	r2, [r3, r4]
	cmp	r2, #4
	bls	.L2066
	sub	r2, r2, #5
	mov	r0, #1
	strh	r2, [r3, r4]	@ movhi
	bl	FtlEctTblFlush
.L2066:
	ldr	r3, .L2083+8
	ldr	r2, [r3, #-1016]
	cmp	r2, #0
	bne	.L2067
	ldr	r2, [r3, #-1608]
	ldr	r0, [sp, #4]
	add	r2, r2, #1
	str	r2, [r3, #-1608]
	ubfx	r0, r0, #10, #16
	bl	FtlBbmMapBadBlock
	bl	FtlBbmTblFlush
.L2067:
	ldr	r3, .L2083+8
	mov	r2, #0
	str	r2, [r3, #-1016]
	b	.L2078
.L2065:
	ldr	r2, .L2083+12
	movw	r3, #3000
	ldrh	r2, [r2, r3]
	movw	r3, #65535
	cmp	r2, r3
	bne	.L2078
.L2063:
	ldr	r6, .L2083+8
	movw	r3, #65535
	ldr	r5, .L2083+4
	mov	r4, #0
	str	r4, [r6, #-1016]
	ldrh	r2, [r6, r5]
	cmp	r2, r3
	moveq	r0, r4
	beq	.L2068
	bl	FtlCacheWriteBack
	ldr	ip, .L2083
	movw	r0, #3912
	ldrb	r1, [r6, #-1941]	@ zero_extendqisi2
	ldrh	r3, [r6, r5]
	ldrh	r0, [ip, r0]
	ldr	r2, [r6, #-2064]
	ldr	sl, .L2083+20
	mov	r3, r3, asl #1
	mul	r1, r0, r1
	strh	r1, [r2, r3]	@ movhi
	ldr	r3, [r6, #-1860]
	ldrh	r2, [r6, sl]
	add	r3, r2, r3
	str	r3, [r6, #-1860]
	b	.L2069
.L2072:
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
	bne	.L2070
	ubfx	r0, r0, #10, #16
	bl	P2V_block_in_plane
	add	r1, r5, #4
	mov	r2, #1
	mov	r7, r0
	ldr	r0, [r5, #8]
	bl	log2phys
	mov	r0, r7
	b	.L2081
.L2070:
	ldr	r3, [r5, #4]
	cmp	r0, r3
	beq	.L2071
	ldr	r3, .L2083+4
	ldrh	r0, [r6, r3]
.L2081:
	bl	decrement_vpc_count
.L2071:
	add	r4, r4, #1
	uxth	r4, r4
.L2069:
	ldrh	r3, [r6, sl]
	ldr	r5, .L2083+8
	cmp	r3, r4
	bhi	.L2072
	movw	r0, #65535
	bl	decrement_vpc_count
	ldr	r3, .L2083+4
	ldr	r1, [r5, #-2064]
	ldrh	r0, [r5, r3]
	mov	r5, r3
	mov	r2, r0, asl #1
	ldrh	r2, [r1, r2]
	cmp	r2, #0
	beq	.L2073
	bl	INSERT_DATA_LIST
	b	.L2074
.L2073:
	bl	INSERT_FREE_LIST
.L2074:
	ldr	r4, .L2083+8
	mvn	r3, #0
	ldr	r2, .L2083+20
	strh	r3, [r4, r5]	@ movhi
	mov	r3, #0
	strh	r3, [r4, r2]	@ movhi
	ldr	r2, .L2083+24
	strh	r3, [r4, r2]	@ movhi
	bl	l2p_flush
	bl	FtlVpcTblFlush
	ldr	r3, [r4, #-1884]
	mov	r1, r4
	ldr	r2, .L2083+28
	cmp	r3, #0
	beq	.L2075
	ldr	r3, [r4, #-1824]
	cmp	r3, #29
	bhi	.L2075
	ldrh	r3, [r4, r2]
	sub	r2, r2, #276
	mvn	r1, #0
	ldrh	r2, [r4, r2]
	cmp	r2, r3
	movcc	r3, r3, asl #1
	ldrcc	r2, .L2083+32
	strcch	r3, [r4, r2]	@ movhi
	ldr	r3, .L2083+36
	ldr	r2, .L2083+8
	b	.L2082
.L2075:
	ldr	r0, .L2083+40
	ldrh	r2, [r1, r2]
	ldr	r3, .L2083+8
	ldrh	r0, [r1, r0]
	add	r2, r2, r2, asl #1
	cmp	r0, r2, lsr #2
	ble	.L2080
	ldr	r2, .L2083+36
	mvn	r1, #0
	strh	r1, [r3, r2]	@ movhi
	add	r2, r2, #564
	mov	r1, #20
.L2082:
	strh	r1, [r3, r2]	@ movhi
	b	.L2080
.L2078:
	mov	r0, #1
	b	.L2068
.L2080:
	mov	r0, #0
.L2068:
	ldmfd	sp!, {r1, r2, r3, r4, r5, r6, r7, r8, sl, pc}
.L2084:
	.align	2
.L2083:
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
	ldr	r3, .L2094
	ldrb	r3, [r3, #80]	@ zero_extendqisi2
	cmp	r3, #0
	ldr	r3, .L2094+4
	beq	.L2086
	ldr	r2, [r3, #-1176]
	tst	r2, #1
	beq	.L2086
	ldr	r2, .L2094+8
	ldrh	r2, [r3, r2]
	cmp	r2, #0
	bne	.L2091
.L2086:
	mov	r2, #0
	ldr	r0, [r3, #-980]
	ldr	r1, [r3, #-1176]
	mov	r3, r2
	bl	FlashProgPages
	mov	r4, #0
	ldr	r5, .L2094+4
	mov	r6, #36
	b	.L2088
.L2090:
	mul	r3, r6, r4
	ldr	r1, [r5, #-980]
	add	r2, r1, r3
	ldr	r1, [r1, r3]
	cmn	r1, #1
	bne	.L2089
	ldr	r2, .L2094+12
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
	b	.L2093
.L2089:
	ldr	r3, [r2, #12]
	add	r4, r4, #1
	ldr	r1, [r2, #4]
	uxth	r4, r4
	ldr	r0, [r3, #12]
	ldr	r2, [r3, #8]
	bl	FtlGcUpdatePage
.L2088:
	ldr	r1, [r5, #-1176]
	ldr	r7, .L2094+4
	cmp	r4, r1
	bcc	.L2090
	ldr	r0, [r7, #-980]
	bl	FtlGcBufFree
	ldr	r3, .L2094+8
	mov	r0, #0
	str	r0, [r7, #-1176]
	ldrh	r3, [r7, r3]
	cmp	r3, r0
	ldmnefd	sp!, {r3, r4, r5, r6, r7, pc}
	mov	r0, #1
	bl	FtlGcFreeTempBlock
.L2093:
	mov	r0, #1
	ldmfd	sp!, {r3, r4, r5, r6, r7, pc}
.L2091:
	mov	r0, #0
	ldmfd	sp!, {r3, r4, r5, r6, r7, pc}
.L2095:
	.align	2
.L2094:
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
	ldr	r6, .L2098
	ldr	r0, .L2098+4
	ldr	r4, .L2098+8
	ldrh	r1, [r6, r5]
	bl	FtlGcScanTempBlk
	ldr	r3, .L2098+12
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
.L2099:
	.align	2
.L2098:
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
	ldr	r3, .L2101
	mov	r4, #0
	ldr	r5, .L2101+4
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
.L2102:
	.align	2
.L2101:
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
	ldr	r5, .L2114
	ldr	r4, .L2114+4
	ldr	r0, [r5, #3848]
	uxth	r0, r0
	bl	FtlFreeSysBlkQueueInit
	bl	FtlScanSysBlk
	ldr	r3, .L2114+8
	ldrh	r2, [r4, r3]
	movw	r3, #65535
	cmp	r2, r3
	beq	.L2112
	bl	FtlLoadSysInfo
	subs	r6, r0, #0
	bne	.L2112
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
	b	.L2105
.L2107:
	add	r0, r1, r6
	add	r6, r6, #12
	ldr	r0, [r0, #4]
	cmp	r0, #0
	blt	.L2106
	add	r3, r3, #1
.L2105:
	cmp	r3, r2
	blt	.L2107
.L2106:
	ldr	r4, .L2114+4
	cmp	r3, r2
	ldr	r1, .L2114+12
	ldrh	r0, [r4, r1]
	add	r0, r0, #1
	strh	r0, [r4, r1]	@ movhi
	blt	.L2108
	movw	r3, #266
	ldrh	r3, [r4, r3]
	cmp	r3, #0
	beq	.L2109
.L2108:
	ldr	r0, .L2114+16
	bl	FtlSuperblockPowerLostFix
	ldr	r0, .L2114+20
	bl	FtlSuperblockPowerLostFix
	ldr	r0, .L2114+24
	bl	FtlMapBlkWriteDumpData
	ldr	r0, .L2114+28
	bl	FtlMapBlkWriteDumpData
	ldr	r3, .L2114+32
	ldrh	r2, [r4, r3]
	add	r2, r2, #1
	strh	r2, [r4, r3]	@ movhi
	bl	l2p_flush
	bl	FtlVpcTblFlush
	bl	FtlVpcTblFlush
.L2109:
	mov	r0, #1
	ldr	r4, .L2114+4
	bl	FtlUpdateVaildLpn
	ldr	r3, .L2114+36
	ldrh	r0, [r4, r3]
	movw	r3, #65535
	cmp	r0, r3
	beq	.L2110
	ldr	r3, .L2114+40
	ldrh	r3, [r4, r3]
	cmp	r3, #0
	bne	.L2110
	ldr	r3, .L2114+44
	ldrh	r3, [r4, r3]
	cmp	r3, #0
	bne	.L2110
	bl	FtlGcRefreshBlock
	ldr	r3, .L2114+48
	ldrh	r0, [r4, r3]
	bl	FtlGcRefreshBlock
	ldr	r0, .L2114+16
	bl	allocate_new_data_superblock
	ldr	r0, .L2114+20
	bl	allocate_new_data_superblock
	add	r0, r4, #224
	bl	FtlMapBlkWriteDumpData
.L2110:
	ldr	r3, .L2114+12
	ldr	r2, .L2114+4
	ldrh	r0, [r2, r3]
	ands	r4, r0, #31
	bne	.L2113
	bl	FtlVpcCheckAndModify
	mov	r0, r4
	ldmfd	sp!, {r4, r5, r6, pc}
.L2112:
	mvn	r0, #0
	ldmfd	sp!, {r4, r5, r6, pc}
.L2113:
	mov	r0, #0
	ldmfd	sp!, {r4, r5, r6, pc}
.L2115:
	.align	2
.L2114:
	.word	.LANCHOR0
	.word	.LANCHOR2
	.word	-1768
	.word	-1792
	.word	.LANCHOR2-2044
	.word	.LANCHOR2-1996
	.word	.LANCHOR2-1088
	.word	.LANCHOR2+224
	.word	-1790
	.word	-2044
	.word	-2040
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
.L2117:
	stmfd	sp!, {r3, r4, r5, lr}
	.save {r3, r4, r5, lr}
	mvn	r3, #0
	ldr	r4, .L2119
	mov	r2, #0
	ldr	r1, .L2119+4
	mov	r5, r0
	ldr	r0, .L2119+8
	str	r3, [r4, #2960]
	ldr	r3, .L2119+12
	str	r2, [r3, #412]
	bl	printk
	mov	r0, r5
	bl	FtlConstantsInit
	bl	FtlMemInit
	bl	FtlVariablesInit
	ldr	r3, .L2119+16
	ldr	r0, [r3, #3848]
	uxth	r0, r0
	bl	FtlFreeSysBlkQueueInit
	bl	FtlLoadBbt
	cmp	r0, #0
	bne	.L2118
	bl	FtlSysBlkInit
	cmp	r0, #0
	moveq	r3, #1
	streq	r3, [r4, #2960]
.L2118:
	mov	r0, #0
	ldmfd	sp!, {r3, r4, r5, pc}
.L2120:
	.align	2
.L2119:
	.word	.LANCHOR1
	.word	.LC76
	.word	.LC75
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
	ldr	r4, .L2125
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
	beq	.L2122
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
	bne	.L2123
	ldr	r0, .L2125+4
	bl	FtlInit
.L2123:
	ldr	r0, .L2125+8
	mov	r1, r4
	bl	printk
.L2122:
	mov	r0, r4
	ldmfd	sp!, {r3, r4, r5, pc}
.L2126:
	.align	2
.L2125:
	.word	.LANCHOR2
	.word	.LANCHOR0+2200
	.word	.LC119
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
	ldr	r3, .L2130
	ldr	r5, .L2130+4
	ldrh	r2, [r5, r3]
	movw	r3, #65535
	cmp	r2, r3
	beq	.L2128
	ldr	r3, .L2130+8
	ldrh	r3, [r5, r3]
	cmp	r3, #0
	bne	.L2129
.L2128:
	bl	FtlCacheWriteBack
	mov	r0, #0
	bl	FtlGcFreeTempBlock
	ldr	r0, .L2130+12
	mov	r4, #0
	strb	r4, [r5, #-1940]
	bl	allocate_data_superblock
	ldr	r3, .L2130+16
	strh	r4, [r5, r3]	@ movhi
	add	r3, r3, #12
	strh	r4, [r5, r3]	@ movhi
	bl	l2p_flush
	bl	FtlVpcTblFlush
	mov	r0, r4
	bl	FtlEctTblFlush
.L2129:
	ldr	r0, .L2130+12
	ldmfd	sp!, {r3, r4, r5, lr}
	b	get_new_active_ppa
.L2131:
	.align	2
.L2130:
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
	ldr	r5, .L2141
	mov	r7, r0
	mov	r4, r1
	ldr	r3, [r5, #3952]
	cmp	r2, r3
	mvnhi	r0, #0
	bhi	.L2133
	cmp	r1, #31
	movls	r0, #0
	bls	.L2133
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
	beq	.L2134
	rsb	r6, r7, r6
	add	r5, r0, #1
	cmp	r6, r4
	movcs	r6, r4
	uxth	r6, r6
	rsb	r4, r6, r4
.L2134:
	mvn	r3, #0
	ldr	r7, .L2141
	str	r3, [sp, #4]
	movw	r6, #3918
	b	.L2135
.L2137:
	mov	r0, r5
	mov	r1, sp
	mov	r2, #0
	bl	log2phys
	ldr	r3, [sp, #0]
	cmn	r3, #1
	beq	.L2136
	ldr	r3, .L2141+4
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
.L2136:
	ldrh	r3, [r7, r6]
	add	r5, r5, #1
	rsb	r4, r3, r4
.L2135:
	ldrh	r3, [r7, r6]
	cmp	r4, r3
	bcs	.L2137
	ldr	r3, .L2141+4
	mov	r4, #0
	ldr	r2, [r3, #424]
	cmp	r2, #32
	bls	.L2140
	str	r4, [r3, #424]
	bl	l2p_flush
	bl	FtlVpcTblFlush
.L2140:
	mov	r0, r4
.L2133:
	ldmfd	sp!, {r1, r2, r3, r4, r5, r6, r7, pc}
.L2142:
	.align	2
.L2141:
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
	ldr	r3, .L2146
	ldr	r3, [r3, #2960]
	cmp	r3, #1
	bne	.L2145
	bl	FtlSysFlush
.L2145:
	mov	r0, #0
	ldmfd	sp!, {r3, pc}
.L2147:
	.align	2
.L2146:
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
	bne	.L2150
	add	r0, r1, #256
	mov	r1, r2
	mov	r2, r3
	bl	FtlVendorPartWrite
	b	.L2151
.L2150:
	ldr	r9, .L2199
	add	r7, r2, r1
	ldr	r3, [r9, #3952]
	cmp	r7, r3
	mvnhi	r0, #0
	bhi	.L2151
	ldr	r6, .L2199+4
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
	ldr	r3, .L2199+8
	add	r7, r3, #48
	movhi	r7, r3
	ldr	r3, [r9, #3964]
	cmp	r3, #0
	beq	.L2185
	ldr	r2, [r9, #3968]
	sub	r3, r3, #1
	mov	r7, #36
	ldr	lr, [sp, #20]
	mla	r7, r7, r3, r2
	ldr	r3, [r7, #16]
	cmp	lr, r3
	strne	r5, [sp, #32]
	bne	.L2154
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
	bne	.L2155
	ldr	r2, [r6, #432]
	cmp	r2, #2
	movle	r0, fp
	ble	.L2151
.L2155:
	add	r8, r8, r3
	add	r4, r4, r9
	ldr	r3, [sp, #20]
	rsb	r7, r9, r5
	str	fp, [sp, #16]
	add	r3, r3, #1
	str	r7, [sp, #32]
	str	r3, [sp, #20]
.L2154:
	ldr	r3, .L2199+4
	mov	r2, #0
	ldr	r7, [r3, #408]
	str	r2, [r3, #432]
	b	.L2153
.L2185:
	str	r5, [sp, #32]
.L2153:
	ldr	r0, [sp, #20]
	ldr	r1, [sp, #44]
	bl	FtlCacheMetchLpa
	cmp	r0, #0
	beq	.L2156
	bl	FtlCacheWriteBack
.L2156:
	cmp	r5, sl, asl #1
	ldr	r5, .L2199
	mov	fp, #0
	ldr	r6, [sp, #20]
	movcc	r3, #0
	movcs	r3, #1
	str	r3, [sp, #24]
	ldr	r3, .L2199+4
	mov	sl, r5
	str	fp, [sp, #40]
	mov	r9, r8
	str	r7, [r3, #408]
	b	.L2196
.L2182:
	ldrh	r1, [r7, #4]
	cmp	r1, #0
	bne	.L2158
	ldr	r3, .L2199+8
	ldr	r2, .L2199+4
	cmp	r7, r3
	ldr	r8, .L2199+12
	bne	.L2159
	ldr	r3, .L2199+16
	ldrh	r3, [r2, r3]
	cmp	r3, #0
	bne	.L2160
	ldr	r0, .L2199+20
	str	r3, [sp, #4]
	bl	allocate_new_data_superblock
	ldr	r3, [sp, #4]
	str	r3, [r8, #3004]
.L2160:
	ldr	r0, .L2199+8
	bl	allocate_new_data_superblock
	ldr	r2, [r8, #3004]
	ldr	r3, .L2199+20
	cmp	r2, #0
	movne	r7, r3
	b	.L2161
.L2159:
	str	r1, [r8, #3004]
	ldr	r1, .L2199+24
	ldrh	r2, [r2, r1]
	cmp	r2, #0
	movne	r7, r3
	bne	.L2161
	mov	r0, r7
	bl	allocate_new_data_superblock
.L2161:
	ldrh	r3, [r7, #4]
	cmp	r3, #0
	bne	.L2162
	mov	r0, r7
	bl	allocate_new_data_superblock
.L2162:
	ldr	r3, .L2199+4
	str	r7, [r3, #408]
.L2158:
	ldr	r2, .L2199+4
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
	b	.L2163
.L2179:
	ldrh	r3, [r8, #4]
	cmp	r3, #0
	beq	.L2191
	ldr	r7, [sp, #44]
	rsb	r2, r7, r6
	ldr	r7, [sp, #24]
	rsbs	r3, r2, #0
	adc	r3, r3, r2
	tst	r3, r7
	beq	.L2165
	ldr	r7, [sp, #28]
	cmp	r7, #0
	beq	.L2165
	movw	lr, #3918
	ldr	r7, [sp, #32]
	ldrh	r2, [r5, lr]
	add	r1, r7, r4
	mls	r1, r2, r6, r1
	cmp	r1, r2
	bne	.L2191
.L2165:
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
	ldr	r7, .L2199+4
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
	ldr	r0, .L2199+4
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
	beq	.L2166
	ldr	r2, [sp, #12]
	cmp	r2, #0
	beq	.L2167
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
	b	.L2168
.L2167:
	cmp	r3, #0
	beq	.L2168
	ldr	r3, [sp, #32]
	movw	lr, #3918
	add	fp, r3, r4
	ldrh	r3, [r5, lr]
	mls	fp, r3, r6, fp
	ldr	r3, [sp, #12]
	str	r3, [sp, #40]
	uxth	fp, fp
.L2168:
	movw	lr, #3918
	ldrh	r3, [r5, lr]
	cmp	fp, r3
	bne	.L2169
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
	bne	.L2172
	ldr	r0, [r3, #8]
	movw	r3, #3922
	ldrh	r2, [sl, r3]
	b	.L2197
.L2169:
	ldr	r2, [sp, #56]
	mov	r3, #36
	cmn	r2, #1
	beq	.L2173
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
	ldr	r3, .L2199+4
	ldreq	r2, [r3, #-1632]
	addeq	r2, r2, #1
	streq	r2, [r3, #-1632]
	beq	.L2175
	ldr	r2, [r7, #8]
	cmp	r2, r6
	beq	.L2175
	ldr	r2, [r3, #-1632]
	ldr	r0, .L2199+28
	add	r2, r2, #1
	str	r2, [r3, #-1632]
	mov	r2, r6
	ldr	r1, [r7, #8]
	bl	printk
	b	.L2175
.L2173:
	ldr	r1, [r5, #3964]
	ldr	r2, [r5, #3968]
	mla	r3, r3, r1, r2
	mov	r1, #0
	ldr	r0, [r3, #8]
	movw	r3, #3922
	ldrh	r2, [r5, r3]
	bl	ftl_memset
.L2175:
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
	b	.L2197
.L2166:
	ldr	r3, [sp, #24]
	ldr	r2, [r5, #3964]
	cmp	r3, #0
	ldr	r3, [r5, #3968]
	mla	ip, ip, r2, r3
	beq	.L2177
	movw	lr, #3918
	ldrh	r3, [r5, lr]
	mul	r3, r3, r6
	rsb	r3, r4, r3
	add	r3, r9, r3, asl #9
	str	r3, [ip, #8]
	b	.L2172
.L2177:
	movw	r0, #3918
	movw	r3, #3922
	ldrh	r1, [r5, r0]
	ldrh	r2, [r5, r3]
	ldr	r0, [ip, #8]
	mul	r1, r1, r6
	rsb	r1, r4, r1
	add	r1, r9, r1, asl #9
.L2197:
	bl	memcpy
.L2172:
	ldr	r3, .L2199+32
	ldr	r0, [sp, #52]
	ldr	lr, [sp, #48]
	strh	r3, [r0, lr]	@ movhi
	ldr	r3, .L2199+4
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
.L2163:
	ldr	r7, [sp, #28]
	ldr	r3, [sp, #36]
	cmp	r7, r3
	bne	.L2179
	mov	r7, r8
	b	.L2164
.L2191:
	ldr	r3, [sp, #28]
	mov	r7, r8
	str	r3, [sp, #36]
.L2164:
	ldr	r3, [sp, #16]
	ldr	lr, [sp, #36]
	ldr	r2, [r5, #3964]
	rsb	r3, lr, r3
	str	r3, [sp, #16]
	ldr	r3, .L2199+4
	ldr	r0, [sp, #24]
	ldr	r3, [r3, #-988]
	cmp	r2, r3
	orrcs	r0, r0, #1
	uxtb	r3, r0
	cmp	r3, #0
	bne	.L2180
	ldrh	r2, [r7, #4]
	cmp	r2, #0
	bne	.L2194
.L2180:
	bl	FtlCacheWriteBack
	ldr	lr, [sp, #16]
	mov	r3, #0
	str	r3, [sl, #3964]
	cmp	lr, #3
	ldr	r3, [sp, #24]
	movls	r3, #0
.L2194:
	str	r3, [sp, #24]
.L2196:
	ldr	r3, [sp, #16]
	cmp	r3, #0
	bne	.L2182
	mov	r0, r3
	ldr	r7, [sp, #44]
	ldr	r3, [sp, #20]
	rsb	r1, r3, r7
	bl	rk_ftl_garbage_collect
	ldr	r0, [sp, #16]
.L2151:
	add	sp, sp, #100
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, sl, fp, pc}
.L2200:
	.align	2
.L2199:
	.word	.LANCHOR0
	.word	.LANCHOR2
	.word	.LANCHOR2-2044
	.word	.LANCHOR1
	.word	-1992
	.word	.LANCHOR2-1996
	.word	-2040
	.word	.LC120
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
	bne	.L2203
	add	r0, r1, #256
	mov	r1, r2
	mov	r2, r3
	bl	FtlVendorPartRead
	str	r0, [sp, #16]
	b	.L2204
.L2203:
	ldr	r3, [sp, #28]
	add	r3, r3, r1
	str	r3, [sp, #20]
	ldr	r3, .L2233
	ldr	r1, [sp, #20]
	ldr	r2, [r3, #3952]
	cmp	r1, r2
	mvnhi	r3, #0
	strhi	r3, [sp, #16]
	bhi	.L2204
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
	ldr	r3, .L2233+4
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
	beq	.L2205
	bl	FtlCacheWriteBack
.L2205:
	mov	r9, #0
	ldr	r6, [sp, #12]
	str	r9, [sp, #32]
	mov	r5, r9
	str	r9, [sp, #16]
	ldr	r7, .L2233
	b	.L2228
.L2222:
	mov	r2, #0
	mov	r0, r6
	add	r1, sp, #60
	bl	log2phys
	ldr	r2, [sp, #60]
	cmn	r2, #1
	bne	.L2229
	b	.L2232
.L2210:
	mla	r0, r0, r6, sl
	cmp	r0, r4
	bcc	.L2209
	ldr	r2, [sp, #20]
	cmp	r0, r2
	bcs	.L2209
	rsb	r0, r4, r0
	mov	r1, #0
	mov	r2, #512
	str	r3, [sp, #4]
	add	r0, r8, r0, asl #9
	bl	ftl_memset
	ldr	r3, [sp, #4]
.L2209:
	add	sl, sl, #1
	b	.L2207
.L2232:
	mov	sl, #0
	movw	r3, #3918
.L2207:
	ldrh	r0, [r7, r3]
	cmp	sl, r0
	bcc	.L2210
	b	.L2211
.L2229:
	ldr	r3, .L2233+4
	mov	sl, #36
	ldr	r1, [r3, #-984]
	mla	sl, sl, r5, r1
	ldr	r1, [sp, #12]
	cmp	r6, r1
	str	r2, [sl, #4]
	movw	r2, #3918
	bne	.L2212
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
	b	.L2213
.L2212:
	cmp	r6, fp
	ldrneh	r3, [r7, r2]
	mulne	r3, r3, r6
	bne	.L2230
	ldr	r3, [r3, #-956]
	ldrh	r2, [r7, r2]
	ldr	r1, [sp, #20]
	str	r3, [sl, #8]
	mul	r3, r2, r6
	rsb	r9, r3, r1
	cmp	r9, r2
	bne	.L2213
.L2230:
	rsb	r3, r4, r3
	add	r3, r8, r3, asl #9
	str	r3, [sl, #8]
.L2213:
	movw	r3, #3924
	ldr	r2, .L2233+4
	ldrh	r3, [r7, r3]
	str	r6, [sl, #16]
	ldr	r2, [r2, #-948]
	mul	r3, r3, r5
	add	r5, r5, #1
	bic	r3, r3, #3
	add	r3, r2, r3
	str	r3, [sl, #12]
.L2211:
	ldr	r3, [sp, #8]
	add	r6, r6, #1
	subs	r3, r3, #1
	str	r3, [sp, #8]
	beq	.L2215
	movw	r3, #3844
	ldrh	r3, [r7, r3]
	cmp	r5, r3, asl #3
	bne	.L2228
.L2215:
	cmp	r5, #0
	beq	.L2228
	ldr	sl, .L2233+4
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
.L2221:
	ldr	r3, [sp, #24]
	mov	r5, #36
	ldr	r1, [sp, #12]
	mul	r5, r5, r3
	ldr	r3, [sl, #-984]
	add	r3, r3, r5
	ldr	r2, [r3, #16]
	cmp	r2, r1
	bne	.L2217
	ldr	r1, [r3, #8]
	ldr	r3, [sl, #-960]
	cmp	r1, r3
	bne	.L2218
	ldr	r3, [sp, #40]
	mov	r0, r8
	ldr	r2, [sp, #44]
	add	r1, r1, r3
	b	.L2231
.L2217:
	cmp	r2, fp
	bne	.L2218
	ldr	r1, [r3, #8]
	ldr	r3, [sl, #-956]
	cmp	r1, r3
	bne	.L2218
	movw	r2, #3918
	ldrh	r0, [r7, r2]
	ldr	r2, [sp, #48]
	mul	r0, r0, fp
	rsb	r0, r4, r0
	add	r0, r8, r0, asl #9
.L2231:
	bl	memcpy
.L2218:
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
	bne	.L2220
	ldr	r0, [r0, #4]
	ubfx	r0, r0, #10, #16
	bl	P2V_block_in_plane
	bl	FtlGcRefreshBlock
.L2220:
	ldr	r3, [sp, #24]
	add	r3, r3, #1
	str	r3, [sp, #24]
	cmp	r3, r6
	bne	.L2221
	ldr	r6, [sp, #52]
	mov	r5, #0
.L2228:
	ldr	r3, [sp, #8]
	cmp	r3, #0
	bne	.L2222
	ldr	r3, .L2233+8
	ldr	r2, .L2233+4
	ldrh	r3, [r2, r3]
	cmp	r3, #0
	beq	.L2204
	ldr	r0, [sp, #8]
	mov	r1, #1
	bl	rk_ftl_garbage_collect
.L2204:
	ldr	r0, [sp, #16]
	add	sp, sp, #68
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, sl, fp, pc}
.L2234:
	.align	2
.L2233:
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
	ldr	r9, .L2245
	movw	r3, #2212
	ldr	r4, .L2245+4
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
	b	.L2237
.L2243:
	ldr	r5, [sp, #4]
	sub	r3, r8, #8
	mul	fp, r8, r4
	ldr	sl, .L2245+4
	b	.L2238
.L2241:
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
	beq	.L2239
	ldr	r2, [sl, #148]
	ldrh	r1, [r2, #0]
	movw	r2, #61664
	cmp	r1, r2
	bne	.L2239
	ldr	r0, .L2245+8
	mov	r1, r4
	mov	r2, r5
	add	r6, r6, #1
	bl	printk
	ldr	r3, .L2245+4
	uxth	r6, r6
	add	r3, r3, r4, asl #1
	strh	r5, [r3, #208]	@ movhi
	b	.L2240
.L2239:
	sub	r5, r5, #1
	uxth	r5, r5
.L2238:
	cmp	r5, r3
	bgt	.L2241
.L2240:
	ldrb	r3, [r9, #3766]	@ zero_extendqisi2
	add	r4, r4, #1
	cmp	r3, r6
	uxtb	r4, r4
	moveq	r7, #0
.L2237:
	ldrb	r3, [r9, #3766]	@ zero_extendqisi2
	cmp	r3, r4
	bhi	.L2243
	mov	r0, r7
	add	sp, sp, #52
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, sl, fp, pc}
.L2246:
	.align	2
.L2245:
	.word	.LANCHOR0
	.word	.LANCHOR2
	.word	.LC121
	.fnend
	.size	FlashLoadFactorBbt, .-FlashLoadFactorBbt
	.align	2
	.global	FlashReadFacBbtData
	.type	FlashReadFacBbtData, %function
FlashReadFacBbtData:
	.fnstart
	@ args = 0, pretend = 0, frame = 40
	@ frame_needed = 0, uses_anonymous_args = 0
	ldr	r3, .L2257
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
	ldr	r7, .L2257+4
	mul	r8, r8, r2
	str	r3, [sp, #12]
	ldr	r3, [r7, #148]
	str	r3, [sp, #16]
	uxth	r8, r8
	sub	r5, r8, #1
	mul	sl, r8, r6
	uxth	r5, r5
	sub	r8, r8, #16
	b	.L2248
.L2254:
	mov	r1, #1
	add	r3, r5, sl
	add	r0, sp, #4
	mov	r2, r1
	mov	r3, r3, asl #10
	str	r3, [sp, #8]
	bl	FlashReadPages
	ldr	r3, [sp, #4]
	cmn	r3, #1
	beq	.L2249
	ldr	r3, [r7, #148]
	ldrh	r2, [r3, #0]
	movw	r3, #61664
	cmp	r2, r3
	bne	.L2249
	cmp	r4, #0
	moveq	r0, r4
	beq	.L2250
	cmp	r6, #0
	beq	.L2256
	b	.L2252
.L2253:
	ldr	r3, [r0, #3780]
	ubfx	r2, r6, #5, #16
	and	r5, r6, #31
	add	r6, r6, #1
	ldr	lr, [r3, r2, asl #2]
	uxth	r6, r6
	orr	lr, lr, r1, asl r5
	str	lr, [r3, r2, asl #2]
	b	.L2251
.L2256:
	ldr	ip, .L2257+4
	mov	r1, #1
	ldr	r0, .L2257
.L2251:
	ldr	r3, [ip, #132]
	cmp	r6, r3
	bcc	.L2253
.L2252:
	ldr	r3, .L2257
	mov	r2, r9
	mov	r0, r4
	ldr	r1, [r3, #3780]
	bl	memcpy
	mov	r2, #4
	ldr	r0, .L2257+8
	mov	r1, r4
	mov	r3, r2
	bl	rknand_print_hex
	mov	r0, #0
	b	.L2250
.L2249:
	sub	r5, r5, #1
	uxth	r5, r5
.L2248:
	cmp	r5, r8
	bgt	.L2254
	mvn	r0, #0
.L2250:
	add	sp, sp, #40
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, sl, pc}
.L2258:
	.align	2
.L2257:
	.word	.LANCHOR0
	.word	.LANCHOR2
	.word	.LC122
	.fnend
	.size	FlashReadFacBbtData, .-FlashReadFacBbtData
	.align	2
	.global	FlashGetBadBlockList
	.type	FlashGetBadBlockList, %function
FlashGetBadBlockList:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	ldr	r3, .L2266
	stmfd	sp!, {r4, r5, r6, r7, r8, lr}
	.save {r4, r5, r6, r7, r8, lr}
	mov	r5, r0
	ldr	r3, [r3, #2776]
	ldr	r6, .L2266+4
	ldrb	r2, [r3, #13]	@ zero_extendqisi2
	ldrh	r4, [r3, #14]
	ldr	r0, [r6, #116]
	mul	r4, r4, r2
	uxth	r4, r4
	add	r2, r4, #7
	mov	r2, r2, lsr #3
	bl	FlashReadFacBbtData
	cmn	r0, #1
	beq	.L2265
	mov	r2, #0
	mov	r0, r4, lsr #4
	mov	r3, r2
	sub	r4, r4, #1
	mov	r1, #1
	b	.L2261
.L2263:
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
	bcs	.L2265
	add	r2, r2, #1
	uxth	r2, r2
.L2261:
	cmp	r2, r4
	blt	.L2263
	b	.L2260
.L2265:
	mov	r3, #0
.L2260:
	mov	r3, r3, asl #1
	mvn	r2, #0
	mov	r0, #0
	strh	r2, [r5, r3]	@ movhi
	ldmfd	sp!, {r4, r5, r6, r7, r8, pc}
.L2267:
	.align	2
.L2266:
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
	ldr	r7, .L2285
	bl	FtlLoadFactoryBbt
	ldr	r6, .L2285+4
	add	sl, r7, #12
	ldr	r4, .L2285+8
	b	.L2269
.L2275:
	ldrh	r3, [sl], #2
	movw	r2, #65535
	ldr	r0, [r4, #-2076]
	movw	fp, #3908
	ldr	r9, [r4, #-952]
	cmp	r3, r2
	str	r0, [r4, #176]
	str	r9, [r4, #180]
	beq	.L2270
	ldrh	r8, [r6, fp]
	mov	r1, #1
	mov	r2, r1
	ldr	r0, .L2285+12
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
	b	.L2271
.L2270:
	mov	r1, r5
	bl	FlashGetBadBlockList
	ldr	r0, [r4, #176]
	ldr	r1, [r7, #28]
	bl	FtlBbt2Bitmap
	ldrh	fp, [r6, fp]
.L2273:
	sub	fp, fp, #1
	uxth	fp, fp
.L2284:
	movw	r8, #3908
	ldrh	r0, [r6, r8]
	mla	r0, r0, r5, fp
	uxth	r0, r0
	bl	FtlBbmIsBadBlock
	cmp	r0, #1
	beq	.L2273
	mov	r1, #0
	mov	r2, #16
	strh	fp, [sl, #-2]	@ movhi
	ldr	r0, [r4, #-952]
	bl	ftl_memset
	ldr	r3, .L2285+16
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
	ldr	r3, .L2285+20
	ldrh	r2, [r4, r3]
	mov	r2, r2, asl #2
	bl	memcpy
	mov	r1, #1
	mov	r2, r1
	ldr	r0, .L2285+12
	bl	FlashEraseBlocks
	mov	r1, #1
	mov	r3, r1
	ldr	r0, .L2285+12
	mov	r2, r1
	bl	FlashProgPages
	ldr	r3, [r4, #168]
	cmn	r3, #1
	bne	.L2271
	uxth	r0, r8
	bl	FtlBbmMapBadBlock
	b	.L2284
.L2271:
	uxth	r0, r8
	add	r5, r5, #1
	bl	FtlBbmMapBadBlock
	add	r7, r7, #4
.L2269:
	movw	r3, #3866
	ldrh	r3, [r6, r3]
	cmp	r5, r3
	bcc	.L2275
	mov	r4, #0
	ldr	r7, .L2285+4
	movw	r6, #3926
	b	.L2276
.L2277:
	mov	r0, r4
	add	r4, r4, #1
	bl	FtlBbmMapBadBlock
	uxth	r4, r4
.L2276:
	ldrh	r3, [r7, r6]
	ldr	r5, .L2285+4
	cmp	r3, r4
	bhi	.L2277
	movw	r7, #3992
	movw	r6, #3980
	ldrh	r4, [r5, r7]
	sub	r4, r4, #1
	uxth	r4, r4
	b	.L2278
.L2283:
	mov	r0, r4
	bl	FtlBbmIsBadBlock
	cmp	r0, #1
	beq	.L2279
	mov	r0, r4
	bl	FlashTestBlk
	cmp	r0, #0
	beq	.L2280
	mov	r0, r4
	bl	FtlBbmMapBadBlock
	b	.L2279
.L2280:
	ldrh	r2, [r5, r6]
	movw	r3, #65535
	cmp	r2, r3
	streqh	r4, [r5, r6]	@ movhi
.L2281:
	ldrne	r2, .L2285+4
	movne	r3, #3984
	strneh	r4, [r2, r3]	@ movhi
	bne	.L2282
.L2279:
	sub	r4, r4, #1
	uxth	r4, r4
.L2278:
	ldrh	r3, [r5, r7]
	sub	r3, r3, #48
	cmp	r4, r3
	bgt	.L2283
.L2282:
	ldr	r4, .L2285+4
	movw	r7, #3980
	ldr	r3, .L2285+8
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
.L2286:
	.align	2
.L2285:
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
	ldr	r3, .L2307
	str	r2, [r3, #-1848]
	str	r2, [r3, #-1844]
	ldr	r3, .L2307+4
	ldr	r0, [r3, #3848]
	uxth	r0, r0
	bl	FtlFreeSysBlkQueueInit
	bl	FtlLoadBbt
	cmp	r0, #0
	beq	.L2288
	bl	FtlMakeBbt
.L2288:
	mov	r3, #0
	ldr	ip, .L2307+4
	movw	r0, #3918
	ldr	r2, .L2307
	ldr	r1, .L2307+8
	b	.L2289
.L2290:
	ldr	lr, [r2, #-960]
	mvn	r4, r3
	orr	r4, r3, r4, asl #16
	str	r4, [lr, r3, asl #2]
	ldr	lr, [r2, #-956]
	str	r1, [lr, r3, asl #2]
	add	r3, r3, #1
	uxth	r3, r3
.L2289:
	ldrh	lr, [ip, r0]
	ldr	r8, .L2307+4
	cmp	r3, lr, asl #7
	blt	.L2290
	movw	r3, #3852
	mov	r5, #0
	ldrh	r6, [r8, r3]
	movw	r7, #3854
	b	.L2291
.L2292:
	mov	r0, r6
	mov	r1, #1
	bl	FtlLowFormatEraseBlock
	add	r6, r6, #1
	uxth	r6, r6
	add	r5, r5, r0
	uxth	r5, r5
.L2291:
	ldrh	r3, [r8, r7]
	ldr	r4, .L2307+4
	cmp	r3, r6
	bhi	.L2292
	movw	r3, #3844
	ldrh	r1, [r4, r3]
	sub	r3, r5, #3
	cmp	r3, r1, asl #1
	bge	.L2293
.L2297:
	mov	r4, #0
	ldr	r8, .L2307+4
	mov	r6, r4
	movw	r7, #3852
	b	.L2294
.L2293:
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
	b	.L2295
.L2296:
	mov	r0, r5
	mov	r1, #1
	bl	FtlLowFormatEraseBlock
	add	r5, r5, #1
	uxth	r5, r5
.L2295:
	ldrh	r3, [r4, r6]
	cmp	r3, r5
	bhi	.L2296
	b	.L2297
.L2298:
	mov	r0, r6
	mov	r1, #0
	bl	FtlLowFormatEraseBlock
	add	r6, r6, #1
	uxth	r6, r6
	add	r4, r4, r0
	uxth	r4, r4
.L2294:
	ldrh	r3, [r8, r7]
	ldr	r5, .L2307+4
	cmp	r3, r6
	bhi	.L2298
	movw	r3, #3854
	ldr	fp, [r5, #3856]
	ldrh	r3, [r5, r3]
	ldr	r9, .L2307
	mov	r0, fp
	ldr	sl, .L2307+12
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
	ble	.L2299
	rsb	r0, r4, fp
	mov	r1, r6
	bl	__aeabi_uidiv
	str	r0, [r5, #3972]
	mov	r0, r0, lsr #5
	add	r0, r0, #24
	strh	r0, [r9, sl]	@ movhi
.L2299:
	ldr	r5, .L2307
	ldr	r3, [r5, #-1884]
	cmp	r3, #1
	bne	.L2300
	mov	r0, r4
	mov	r1, r6
	bl	__aeabi_uidiv
	ldr	sl, .L2307+12
	ldrh	r9, [r5, sl]
	uxtah	r0, r9, r0
	add	r9, r9, r0, lsr #2
	strh	r9, [r5, sl]	@ movhi
.L2300:
	ldr	r0, .L2307+4
	mov	r3, #3904
	ldrh	r3, [r0, r3]
	cmp	r3, #0
	beq	.L2301
	ldr	r1, .L2307
	ldr	r2, .L2307+12
	ldrh	ip, [r1, r2]
	add	ip, ip, r3, lsr #1
	strh	ip, [r1, r2]	@ movhi
	mul	ip, r6, r3
	cmp	ip, r4
	strgt	r7, [r0, #3972]
	addgt	r3, r3, #32
	addgt	r8, r8, r3
	strgth	r8, [r1, r2]	@ movhi
.L2301:
	ldr	r4, .L2307
	ldr	r5, .L2307+4
	ldr	r3, .L2307+12
	ldr	r7, .L2307+16
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
	ldr	r1, .L2307+20
	mov	r3, #0
	ldr	r2, .L2307+24
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
.L2302:
	ldr	r0, .L2307+28
	bl	make_superblock
	ldrb	r3, [r4, #-2037]	@ zero_extendqisi2
	ldr	r5, .L2307
	cmp	r3, #0
	ldr	r3, .L2307+16
	bne	.L2303
	ldrh	r3, [r4, r7]
	ldr	r2, [r4, #-2064]
	mov	r3, r3, asl #1
	strh	r6, [r2, r3]	@ movhi
	ldrh	r3, [r4, r7]
	add	r3, r3, #1
	strh	r3, [r4, r7]	@ movhi
	b	.L2302
.L2303:
	ldr	r2, [r5, #-1848]
	mvn	r7, #0
	ldr	r1, [r5, #-2064]
	ldr	r6, .L2307+32
	str	r2, [r5, #-2032]
	add	r2, r2, #1
	str	r2, [r5, #-1848]
	ldr	r2, .L2307+36
	ldrh	r0, [r5, r2]
	ldrh	r2, [r5, r3]
	mov	r2, r2, asl #1
	strh	r0, [r1, r2]	@ movhi
	mov	r2, #0
	ldr	r0, .L2307+40
	ldrh	r3, [r5, r3]
	strb	r2, [r5, #-1990]
	add	r3, r3, #1
	strh	r2, [r5, r0]	@ movhi
	strh	r3, [r5, r6]	@ movhi
	mov	r3, #1
	strb	r3, [r5, #-1988]
.L2304:
	ldr	r0, .L2307+44
	bl	make_superblock
	ldrb	r3, [r5, #-1989]	@ zero_extendqisi2
	ldr	r4, .L2307
	cmp	r3, #0
	bne	.L2305
	ldrh	r3, [r5, r6]
	ldr	r2, [r5, #-2064]
	mov	r3, r3, asl #1
	strh	r7, [r2, r3]	@ movhi
	ldrh	r3, [r5, r6]
	add	r3, r3, #1
	strh	r3, [r5, r6]	@ movhi
	b	.L2304
.L2305:
	ldr	r3, [r4, #-1848]
	mvn	r5, #0
	ldr	r2, [r4, #-2064]
	str	r3, [r4, #-1984]
	add	r3, r3, #1
	str	r3, [r4, #-1848]
	ldr	r3, .L2307+48
	ldrh	r1, [r4, r3]
	sub	r3, r3, #4
	ldrh	r3, [r4, r3]
	mov	r3, r3, asl #1
	strh	r1, [r2, r3]	@ movhi
	ldr	r3, .L2307+52
	strh	r5, [r4, r3]	@ movhi
	bl	FtlFreeSysBlkQueueOut
	ldr	r3, .L2307+56
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
	ldreq	r3, .L2307+60
	moveq	r2, #1
	streq	r2, [r3, #2960]
	ldmfd	sp!, {r3, r4, r5, r6, r7, r8, r9, sl, fp, pc}
.L2308:
	.align	2
.L2307:
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
	ldr	r4, .L2333
	ldr	r0, [r4, #112]
	bl	FlashInit
	cmp	r0, #0
	movne	r0, #0
	bne	.L2310
	bl	FlashLoadFactorBbt
	cmp	r0, #0
	beq	.L2311
	bl	FlashMakeFactorBbt
.L2311:
	ldr	r0, [r4, #116]
	bl	FlashReadIdbDataRaw
	cmp	r0, #0
	beq	.L2312
	mov	r1, #0
	mov	r2, #16
	mov	r0, sp
	bl	FlashReadFacBbtData
	ldr	r1, [sp, #0]
	mov	r3, #0
	mov	r0, #1
	mov	r2, r3
.L2314:
	ands	ip, r1, r0, asl r2
	add	r2, r2, #1
	addne	r3, r3, #1
	cmp	r2, #16
	bne	.L2314
	cmp	r3, #6
	ldrls	r3, .L2333+4
	bls	.L2329
	mov	r2, #0
	mov	r0, #1
.L2315:
	ands	ip, r1, r0, asl r2
	add	r2, r2, #1
	addne	r3, r3, #1
	cmp	r2, #24
	bne	.L2315
	cmp	r3, #17
	ldr	r3, .L2333+4
	movhi	r2, #36
.L2329:
	strb	r2, [r3, #1]
	movw	r2, #2226
	ldr	r3, .L2333+4
	ldrb	r1, [r3, #1]	@ zero_extendqisi2
	strh	r1, [r3, r2]	@ movhi
.L2312:
	ldr	r1, .L2333+8
	mov	r4, #1
	ldr	r0, .L2333+12
	bl	printk
	ldr	r0, .L2333+16
	bl	FtlConstantsInit
	bl	FtlVariablesInit
	ldr	r3, .L2333+4
	ldr	r0, [r3, #3848]
	uxth	r0, r0
	bl	FtlFreeSysBlkQueueInit
	b	.L2319
.L2321:
	add	r4, r4, #1
.L2319:
	bl	FtlLoadBbt
	cmp	r0, #0
	bne	.L2331
.L2320:
	bl	FtlSysBlkInit
	cmp	r0, #0
	beq	.L2322
.L2331:
	bl	FtlLowFormat
	cmp	r4, #3
	bls	.L2321
	b	.L2332
.L2322:
	ldr	r3, .L2333+20
	mov	r2, #1
	str	r2, [r3, #2960]
	b	.L2310
.L2332:
	mvn	r0, #0
.L2310:
	add	sp, sp, #16
	ldmfd	sp!, {r4, pc}
.L2334:
	.align	2
.L2333:
	.word	.LANCHOR2
	.word	.LANCHOR0
	.word	.LC76
	.word	.LC75
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
	ldr	r3, .L2337
	ldr	r2, [r3, #3788]
	ldr	r3, .L2337+4
	cmp	r2, r3
	bxne	lr
	b	flash_enter_slc_mode
.L2338:
	.align	2
.L2337:
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
	ldr	r3, .L2341
	ldr	r2, [r3, #3788]
	ldr	r3, .L2341+4
	cmp	r2, r3
	bxne	lr
	b	flash_exit_slc_mode
.L2342:
	.align	2
.L2341:
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
	ldr	r4, .L2348
	mov	r6, r0
	str	r2, [sp, #20]
	mov	sl, r1
	ldr	r0, .L2348+4
	mov	r1, r6
	ldr	r2, [r4, #2776]
	ldr	r3, [r4, #4]
	ldr	r8, .L2348+8
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
	b	.L2344
.L2346:
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
	beq	.L2345
	ldr	r0, [r4, #3788]
	ldr	ip, .L2348+12
	cmp	r0, ip
	moveq	r3, r1
.L2345:
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
.L2344:
	cmp	r5, sl
	bcc	.L2346
	mov	r1, r6
	mov	r2, sl
	mov	r3, #0
	ldr	r0, .L2348+16
	bl	printk
	mov	r0, #0
	add	sp, sp, #28
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, sl, fp, pc}
.L2349:
	.align	2
.L2348:
	.word	.LANCHOR0
	.word	.LC123
	.word	.LANCHOR2
	.word	1446522928
	.word	.LC124
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
	ldr	r4, .L2357
	mov	r5, r0
	str	r2, [sp, #20]
	mov	r8, r1
	ldr	r0, .L2357+4
	mov	r1, r5
	ldr	r2, [r4, #2776]
	mov	r7, r5
	ldr	r3, [r4, #4]
	ldr	fp, .L2357+8
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
	b	.L2351
.L2355:
	add	r3, r6, r9
	ubfx	r3, r3, #2, #16
	cmp	r3, #0
	beq	.L2352
	add	r1, r3, #1
	ldrb	r0, [r4, #80]	@ zero_extendqisi2
	add	r2, r4, r1, asl #1
	cmp	r0, #0
	add	r2, r2, #2224
	add	r2, r2, #4
	ldrh	r2, [r2, #0]
	beq	.L2353
	ldr	r0, [r4, #3788]
	cmp	r0, fp
	uxtheq	r2, r1
.L2353:
	sub	r2, r2, #1
	mov	r2, r2, asl #2
	str	r2, [sp, #24]
	mov	r2, #0
	str	r2, [sp, #28]
.L2352:
	add	r2, r4, r3, asl #1
	add	r2, r2, #2224
	ldrh	r5, [r2, #4]
	ldrb	r2, [r4, #80]	@ zero_extendqisi2
	cmp	r2, #0
	beq	.L2354
	ldr	r2, [r4, #3788]
	cmp	r2, fp
	moveq	r5, r3
.L2354:
	ldr	r2, [sp, #16]
	ldr	r1, [sp, #20]
	ldrb	ip, [r4, #3836]	@ zero_extendqisi2
	mla	r3, sl, r5, r2
	add	r2, r1, r6, asl #9
	ldr	r1, .L2357+12
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
.L2351:
	cmp	r6, r8
	bcc	.L2355
	mov	r1, r7
	mov	r2, r8
	mov	r3, #0
	ldr	r0, .L2357+16
	bl	printk
	mov	r5, r7
	mov	r0, #0
	add	sp, sp, #92
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, sl, fp, pc}
.L2358:
	.align	2
.L2357:
	.word	.LANCHOR0
	.word	.LC125
	.word	1446522928
	.word	.LANCHOR2
	.word	.LC126
	.fnend
	.size	IDBlockWriteData, .-IDBlockWriteData
	.align	2
	.global	write_idblock
	.type	write_idblock, %function
write_idblock:
	.fnstart
	@ args = 0, pretend = 0, frame = 8
	@ frame_needed = 0, uses_anonymous_args = 0
	ldr	r3, .L2372
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
	beq	.L2360
	add	sl, r6, #508
	add	sl, sl, #3
	mov	sl, sl, lsr #9
	cmp	sl, #255
	bhi	.L2361
	add	r0, r4, sl, asl #9
	mov	r1, r4
	rsb	r2, sl, #256
	bl	memcpy
.L2361:
	ldr	r8, .L2372
	mov	r3, #5
	ldr	r0, .L2372+4
	mov	r1, r7
	mov	r2, #4
	add	sl, sl, #128
	bl	rknand_print_hex
	ldrb	r2, [r8, #1]	@ zero_extendqisi2
	ldr	r1, [r4, #512]
	cmp	sl, #256
	movcs	sl, #256
	ldr	r0, .L2372+8
	mov	fp, #0
	bl	printk
	ldrb	r3, [r8, #1]	@ zero_extendqisi2
	ldr	r2, [r4, #512]
	mov	r1, sl
	ldr	r0, .L2372+12
	mov	r9, fp
	cmp	r2, r3
	mov	r8, r4
	mov	r2, r6
	strhi	r3, [r4, #512]
	mov	r6, r7
	bl	printk
	mov	r1, sl, asl #7
	str	r1, [sp, #12]
.L2367:
	ldr	r1, .L2372
	movw	r2, #2226
	ldr	r3, [r6], #4
	ldrh	r2, [r1, r2]
	cmp	r3, r2
	bcs	.L2363
	ldr	r2, .L2372+16
	ldr	r2, [r2, #132]
	cmp	r3, r2
	bcc	.L2363
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
.L2366:
	ldr	r1, [r5, r3]
	add	r3, r3, #4
	add	r2, r8, r3
	ldr	r2, [r2, #-4]
	cmp	r1, r2
	beq	.L2364
	bic	r4, r7, #255
	stmia	sp, {r1, r2}
	mov	r3, r7
	mov	r1, r9
	ldr	r2, [r6, #-4]
	mov	r4, r4, asl #2
	ldr	r0, .L2372+20
	bl	printk
	ldr	r0, .L2372+24
	add	r1, r8, r4
	mov	r2, #4
	mov	r3, #256
	bl	rknand_print_hex
	mov	r3, #256
	mov	r2, #4
	ldr	r0, .L2372+28
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
	ldr	r0, .L2372+32
	bl	printk
	ldr	r2, [sp, #12]
	cmp	r7, r2
	bcc	.L2363
	b	.L2365
.L2364:
	ldr	r1, [sp, #12]
	add	r7, r7, #1
	cmp	r7, r1
	bne	.L2366
.L2365:
	add	fp, fp, #1
.L2363:
	add	r9, r9, #1
	cmp	r9, #5
	bne	.L2367
	mov	r0, r5
	bl	kfree
	cmp	fp, #0
	mvneq	r0, #0
	movne	r0, #0
.L2360:
	add	sp, sp, #20
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, sl, fp, pc}
.L2373:
	.align	2
.L2372:
	.word	.LANCHOR0
	.word	.LC127
	.word	.LC128
	.word	.LC129
	.word	.LANCHOR2
	.word	.LC130
	.word	.LC131
	.word	.LC132
	.word	.LC133
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
	ldr	ip, .L2377
	b	.L2375
.L2376:
	ldrb	r4, [r3, r2]	@ zero_extendqisi2
	add	r2, r2, #1
	eor	r4, r4, r0, lsr #24
	add	r4, ip, r4, asl #2
	ldr	r4, [r4, #3008]
	eor	r0, r4, r0, asl #8
.L2375:
	cmp	r2, r1
	bne	.L2376
	ldmfd	sp!, {r4, pc}
.L2378:
	.align	2
.L2377:
	.word	.LANCHOR1
	.fnend
	.size	CRC_32, .-CRC_32
	.align	2
	.type	rknand_sys_storage_ioctl, %function
rknand_sys_storage_ioctl:
	.fnstart
	@ args = 0, pretend = 0, frame = 520
	@ frame_needed = 0, uses_anonymous_args = 0
	ldr	r3, .L2488
	stmfd	sp!, {r4, r5, r6, lr}
	.save {r4, r5, r6, lr}
	cmp	r1, r3
	.pad #520
	sub	sp, sp, #520
	mov	r6, r1
	mov	r5, r2
	ldr	r4, [r0, #124]
	beq	.L2388
	bhi	.L2397
	ldr	r3, .L2488+4
	cmp	r1, r3
	beq	.L2385
	bhi	.L2398
	sub	r3, r3, #125
	cmp	r1, r3
	beq	.L2382
	bhi	.L2399
	sub	r3, r3, #237
	cmp	r1, r3
	bne	.L2431
	b	.L2484
.L2399:
	ldr	r3, .L2488+8
	cmp	r1, r3
	beq	.L2383
	add	r3, r3, #1
	cmp	r1, r3
	bne	.L2431
	b	.L2485
.L2398:
	ldr	r3, .L2488+12
	cmp	r1, r3
	beq	.L2387
	bhi	.L2400
	ldr	r3, .L2488+16
	cmp	r1, r3
	bne	.L2431
	b	.L2486
.L2400:
	ldr	r3, .L2488+20
	cmp	r1, r3
	beq	.L2387
	add	r3, r3, #10
	cmp	r1, r3
	bne	.L2431
	b	.L2387
.L2397:
	ldr	r3, .L2488+24
	cmp	r1, r3
	beq	.L2393
	bhi	.L2401
	sub	r3, r3, #78
	cmp	r1, r3
	beq	.L2390
	bcc	.L2389
	add	r3, r3, #21
	cmp	r1, r3
	beq	.L2391
	add	r3, r3, #56
	cmp	r1, r3
	bne	.L2431
	b	.L2487
.L2401:
	ldr	r3, .L2488+28
	cmp	r1, r3
	beq	.L2396
	bhi	.L2402
	sub	r3, r3, #956
	sub	r3, r3, #1
	cmp	r1, r3
	beq	.L2394
	add	r3, r3, #956
	cmp	r1, r3
	bne	.L2431
	b	.L2395
.L2402:
	ldr	r3, .L2488+32
	cmp	r1, r3
	beq	.L2395
	add	r3, r3, #1
	cmp	r1, r3
	bne	.L2431
	b	.L2396
.L2389:
	ldr	r0, .L2488+36
	bl	printk
	mov	r0, r4
	mov	r1, r5
	mov	r2, #512
	bl	rk_copy_from_user
	cmp	r0, #0
	bne	.L2477
.L2403:
	ldr	r0, .L2488+40
	ldmia	r4, {r1, r2}
	bl	printk
	ldr	r6, [r4, #4]
	cmp	r6, #8
	bhi	.L2470
	bl	rknand_device_unlock
	mov	r2, r4
	mov	r1, r6
	ldr	r0, [r4, #0]
	bl	IdBlockReadData
	bl	rknand_device_unlock
	mov	r1, r4
	mov	r0, r5
	mov	r2, r6, asl #9
	bl	rk_copy_to_user
	subs	r4, r0, #0
	ldrne	r0, .L2488+44
	beq	.L2404
	b	.L2479
.L2390:
	ldr	r0, .L2488+48
	bl	printk
	mov	r0, r4
	mov	r1, r5
	mov	r2, #4096
	bl	rk_copy_from_user
	cmp	r0, #0
	bne	.L2477
.L2405:
	ldr	r5, .L2488+52
	ldr	r0, .L2488+56
	ldmia	r4, {r1, r2}
	bl	printk
	ldr	r3, [r5, #436]
	cmp	r3, #0
	bne	.L2406
	mov	r0, #260096
	mov	r1, #208
	mov	r2, #6
	bl	kmalloc_order_trace
	cmp	r0, #0
	str	r0, [r5, #436]
	beq	.L2470
.L2406:
	ldr	r2, [r4, #4]
	movw	r3, #4088
	cmp	r2, r3
	bhi	.L2470
	ldr	r3, [r4, #0]
	cmp	r3, #251904
	bhi	.L2470
	ldr	r1, .L2488+52
	ldr	r0, [r1, #436]
	add	r1, r4, #8
	add	r0, r0, r3
	bl	memcpy
	b	.L2475
.L2487:
	ldr	r0, .L2488+60
	bl	printk
	mov	r0, r4
	mov	r1, r5
	mov	r2, #28
	bl	rk_copy_from_user
	cmp	r0, #0
	bne	.L2477
.L2407:
	ldmia	r4, {r1, r2}
	ldr	r0, .L2488+64
	bl	printk
	ldr	r1, [r4, #0]
	cmp	r1, #256000
	bhi	.L2470
	ldr	r5, .L2488+52
	ldr	r0, [r5, #436]
	cmp	r0, #0
	beq	.L2470
	bl	CRC_32
	ldr	r3, [r4, #4]
	cmp	r3, r0
	bne	.L2450
	bl	rknand_device_unlock
	add	r2, r4, #8
	ldr	r1, [r5, #436]
	ldr	r0, [r4, #0]
	mov	r4, #0
	bl	write_idblock
	bl	rknand_device_unlock
	ldr	r0, [r5, #436]
	bl	kfree
	str	r4, [r5, #436]
	b	.L2404
.L2391:
	ldr	r0, .L2488+68
	bl	printk
	mov	r0, r4
	bl	ReadFlashInfo
	mov	r0, r5
	mov	r1, r4
	mov	r2, #11
	b	.L2480
.L2388:
	ldr	r0, .L2488+72
	bl	printk
	bl	rknand_device_unlock
	bl	FtlReInitForSDUpdata
	mov	r6, r0
	bl	rknand_device_unlock
	cmp	r6, #0
	bne	.L2470
	bl	nand_blk_add_whole_disk
	bl	rknand_device_unlock
	mov	r1, r6
	mov	r2, #64
	mov	r0, r4
	bl	FlashReadFacBbtData
	bl	rknand_device_unlock
	ldr	r0, .L2488+76
	mov	r1, r4
	mov	r2, #4
	mov	r3, #8
	bl	rknand_print_hex
	mov	r0, r5
	mov	r1, r4
	mov	r2, #64
	b	.L2480
.L2393:
	ldr	r0, .L2488+80
	bl	printk
	ldr	r3, .L2488+52
	mov	r0, r5
	mov	r1, r4
	mov	r2, #4
	ldr	r3, [r3, #440]
	ldr	r3, [r3, #20]
	str	r3, [r4, #0]
	b	.L2480
.L2394:
	ldr	r0, .L2488+84
	bl	printk
	bl	rknand_device_unlock
	mov	r1, #264
	mov	r2, #2
	mov	r3, r4
	mov	r0, #16
	bl	FtlRead
	bl	rknand_device_unlock
	mov	r0, r5
	mov	r1, r4
	mov	r2, #1024
	b	.L2480
.L2382:
	ldr	r0, .L2488+88
	bl	printk
	mov	r0, sp
	mov	r1, r5
	mov	r2, #520
	bl	rk_copy_from_user
	subs	r4, r0, #0
	bne	.L2477
.L2408:
	ldr	r2, [sp, #0]
	ldr	r3, .L2488+92
	cmp	r2, r3
	bne	.L2472
	ldr	r3, [sp, #4]
	cmp	r3, #512
	bhi	.L2472
	ldr	r6, .L2488+52
	mov	r2, #512
	mov	r0, sp
	ldr	r1, [r6, #440]
	bl	memcpy
	ldr	r2, [r6, #444]
	ldr	r3, .L2488+96
	cmp	r2, r3
	beq	.L2409
	add	r0, sp, #64
	mov	r1, #128
	str	r4, [sp, #8]
	str	r4, [sp, #12]
	bl	__memzero
.L2409:
	add	r0, sp, #256
	mov	r1, #256
	mov	r3, #0
	str	r3, [sp, #16]
	bl	__memzero
	mov	r0, r5
	mov	r1, sp
	mov	r2, #520
.L2480:
	bl	rk_copy_to_user
	b	.L2478
.L2383:
	ldr	r0, .L2488+100
	bl	printk
	mov	r0, sp
	mov	r1, r5
	mov	r2, #520
	bl	rk_copy_from_user
	cmp	r0, #0
	ldrne	r0, .L2488+104
	bne	.L2479
	ldr	r2, [sp, #0]
	ldr	r3, .L2488+92
	cmp	r2, r3
	bne	.L2472
	ldr	r3, [sp, #4]
	cmp	r3, #512
	bhi	.L2472
	ldr	r4, .L2488+52
	ldr	r3, .L2488+96
	ldr	r2, [r4, #444]
	cmp	r2, r3
	bne	.L2450
	ldr	r3, [sp, #12]
	sub	r2, r3, #1
	cmp	r2, #127
	mvnhi	r4, #2
	bhi	.L2380
	ldr	r0, [r4, #440]
	add	r1, sp, #64
	str	r3, [r0, #12]
	add	r0, r0, #64
	ldr	r2, [sp, #12]
	bl	memcpy
	mov	r0, #1
	ldr	r1, [r4, #440]
	b	.L2476
.L2486:
	ldr	r0, .L2488+108
	bl	printk
	mov	r0, sp
	mov	r1, r5
	mov	r2, #520
	bl	rk_copy_from_user
	cmp	r0, #0
	bne	.L2477
.L2411:
	ldr	r2, [sp, #0]
	ldr	r3, .L2488+112
	cmp	r2, r3
	bne	.L2472
	ldr	r3, [sp, #4]
	cmp	r3, #512
	bhi	.L2472
	ldr	r5, .L2488+52
	ldr	r4, [r5, #448]
	cmp	r4, #0
	beq	.L2380
	ldr	r3, [r5, #452]
	ldr	r2, .L2488+116
	ldr	r1, [r3, #0]
	cmp	r1, r2
	beq	.L2412
	str	r2, [r3, #0]
	mov	r2, #504
	ldr	r3, [r5, #452]
	str	r2, [r3, #4]
	mov	r2, #0
	str	r2, [r3, #8]
	str	r2, [r3, #12]
.L2412:
	ldr	r1, [r5, #452]
	mov	r4, #0
	mov	r0, r4
	str	r4, [r1, #16]
	bl	StorageSysDataStore
	ldr	r3, [r5, #440]
	ldr	r2, .L2488+92
	ldr	r5, .L2488+52
	ldr	r1, [r3, #0]
	cmp	r1, r2
	strne	r2, [r3, #0]
	ldr	r0, [r5, #440]
	mov	r1, #128
	ldrne	r3, .L2488+52
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
.L2385:
	ldr	r0, .L2488+120
	bl	printk
	mov	r0, sp
	mov	r1, r5
	mov	r2, #520
	bl	rk_copy_from_user
	subs	r4, r0, #0
	bne	.L2477
.L2414:
	ldr	r2, [sp, #0]
	ldr	r3, .L2488+124
	cmp	r2, r3
	bne	.L2472
	ldr	r3, [sp, #4]
	cmp	r3, #512
	bhi	.L2472
	ldr	r5, .L2488+52
	ldr	r3, [r5, #448]
	cmp	r3, #1
	beq	.L2380
	ldr	r3, [r5, #452]
	mov	r0, #0
	ldr	r2, .L2488+116
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
	ldr	r2, .L2488+92
	ldr	r1, [r3, #0]
	cmp	r1, r2
	beq	.L2416
	str	r2, [r3, #0]
	mov	r1, #504
	ldr	r3, .L2488+52
	mov	r2, #0
	ldr	r3, [r3, #440]
	stmib	r3, {r1, r2}
.L2416:
	ldr	r5, .L2488+52
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
.L2485:
	ldr	r0, .L2488+128
	bl	printk
	mov	r0, sp
	mov	r1, r5
	mov	r2, #520
	bl	rk_copy_from_user
	cmp	r0, #0
	bne	.L2477
.L2417:
	ldr	r2, [sp, #0]
	ldr	r3, .L2488+132
	cmp	r2, r3
	bne	.L2472
	ldr	r2, [sp, #4]
	cmp	r2, #512
	addls	r0, sp, #8
	ldrls	r1, .L2488+136
	bls	.L2483
	b	.L2472
.L2387:
	ldr	r3, .L2488+20
	cmp	r6, r3
	ldreq	r0, .L2488+140
	beq	.L2473
	ldr	r3, .L2488+144
	cmp	r6, r3
	ldreq	r0, .L2488+148
	ldrne	r0, .L2488+152
.L2473:
	bl	printk
	mov	r0, sp
	mov	r1, r5
	mov	r2, #520
	bl	rk_copy_from_user
	cmp	r0, #0
	bne	.L2477
.L2421:
	ldr	r2, [sp, #0]
	ldr	r3, .L2488+156
	cmp	r2, r3
	bne	.L2470
	ldr	r3, .L2488+144
	ldr	r4, .L2488+52
	cmp	r6, r3
	bne	.L2422
	ldr	r3, [r4, #440]
	mov	r2, sp
	ldr	r3, [r3, #20]
	strb	r3, [sp, #8]
	str	r3, [sp, #4]
	bic	r3, r2, #8128
	bic	r3, r3, #63
	ldr	r3, [r3, #8]
@ 444 "/home/zyf/rk30/rk3288_android4.4/kernel/arch/arm/include/asm/uaccess.h" 1
	adds r2, r5, #16; sbcccs r2, r2, r3; movcc r3, #0
@ 0 "" 2
	cmp	r3, #0
	bne	.L2470
	mov	r0, r5
	mov	r1, sp
	mov	r2, #16
	bl	__copy_to_user
	cmp	r0, #0
	moveq	r4, r0
	mvnne	r4, #13
	b	.L2380
.L2422:
	ldr	r3, [r4, #968]
	cmp	r3, #10
	bhi	.L2470
	ldr	r3, [r4, #440]
	ldr	r1, [sp, #4]
	ldr	r2, [r3, #24]
	cmp	r2, r1
	beq	.L2423
	cmp	r2, #0
	beq	.L2423
	ldr	r0, .L2488+160
	bl	printk
	ldr	r3, [r4, #968]
	add	r3, r3, #1
	str	r3, [r4, #968]
	b	.L2470
.L2423:
	ldr	r0, .L2488+52
	mov	r2, #0
	str	r2, [r0, #968]
	ldr	r0, .L2488+20
	cmp	r6, r0
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
.L2395:
	ldr	r0, .L2488+164
	bl	printk
	mov	r0, sp
	mov	r1, r5
	mov	r2, #520
	bl	rk_copy_from_user
	cmp	r0, #0
	bne	.L2477
.L2426:
	ldr	r2, [sp, #0]
	ldr	r3, .L2488+168
	cmp	r2, r3
	bne	.L2472
	ldr	r2, [sp, #4]
	cmp	r2, #504
	bhi	.L2472
	ldr	r3, .L2488+172
	add	r0, sp, #8
	cmp	r6, r3
	ldr	r3, .L2488+52
	ldreq	r1, [r3, #972]
	ldrne	r1, [r3, #976]
	add	r1, r1, #8
.L2483:
	bl	memcpy
	mov	r2, sp
	bic	r3, r2, #8128
	bic	r3, r3, #63
	ldr	r3, [r3, #8]
@ 444 "/home/zyf/rk30/rk3288_android4.4/kernel/arch/arm/include/asm/uaccess.h" 1
	adds r2, r5, #520; sbcccs r2, r2, r3; movcc r3, #0
@ 0 "" 2
	cmp	r3, #0
	bne	.L2470
	mov	r0, r5
	mov	r1, sp
	mov	r2, #520
	bl	__copy_to_user
.L2478:
	subs	r4, r0, #0
	bne	.L2470
	b	.L2404
.L2396:
	ldr	r0, .L2488+176
	bl	printk
	mov	r0, sp
	mov	r1, r5
	mov	r2, #520
	bl	rk_copy_from_user
	cmp	r0, #0
	beq	.L2429
.L2477:
	ldr	r0, .L2488+180
.L2479:
	bl	printk
	b	.L2470
.L2429:
	ldr	r2, [sp, #0]
	ldr	r3, .L2488+168
	cmp	r2, r3
	bne	.L2472
	ldr	r2, [sp, #4]
	cmp	r2, #504
	bhi	.L2472
	ldr	r3, .L2488+28
	add	r2, r2, #8
	ldr	r4, .L2488+52
	cmp	r6, r3
	bne	.L2430
	mov	r1, sp
	ldr	r0, [r4, #972]
	bl	memcpy
	mov	r0, #2
	ldr	r1, [r4, #972]
	b	.L2476
.L2430:
	mov	r1, sp
	ldr	r0, [r4, #976]
	bl	memcpy
	ldr	r1, [r4, #976]
	mov	r0, #3
.L2476:
	bl	StorageSysDataStore
	mov	r4, r0
	b	.L2404
.L2484:
	bl	rknand_dev_flush
.L2475:
	mov	r4, #0
	b	.L2404
.L2472:
	mvn	r4, #0
.L2404:
	mov	r1, r4
	ldr	r0, .L2488+184
	bl	printk
	b	.L2380
.L2431:
	mvn	r4, #21
	b	.L2380
.L2450:
	mvn	r4, #1
	b	.L2380
.L2470:
	mvn	r4, #13
.L2380:
	mov	r0, r4
	add	sp, sp, #520
	ldmfd	sp!, {r4, r5, r6, pc}
.L2489:
	.align	2
.L2488:
	.word	1074033155
	.word	1074029694
	.word	1074029570
	.word	1074031656
	.word	1074029695
	.word	1074031666
	.word	1074033235
	.word	1074034193
	.word	1074034194
	.word	.LC134
	.word	.LC136
	.word	.LC137
	.word	.LC138
	.word	.LANCHOR2
	.word	.LC139
	.word	.LC140
	.word	.LC141
	.word	.LC142
	.word	.LC143
	.word	.LC144
	.word	.LC145
	.word	.LC146
	.word	.LC147
	.word	1263358532
	.word	-1067903959
	.word	.LC148
	.word	.LC149
	.word	.LC150
	.word	1112753220
	.word	1146313043
	.word	.LC151
	.word	1112755781
	.word	.LC152
	.word	1094995539
	.word	.LANCHOR2+456
	.word	.LC153
	.word	1074031676
	.word	.LC154
	.word	.LC155
	.word	1280262987
	.word	.LC156
	.word	.LC157
	.word	1145980246
	.word	1074034192
	.word	.LC158
	.word	.LC135
	.word	.LC159
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
	ldr	r4, .L2493
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
	beq	.L2491
	mov	r0, r5
	mov	r1, #508
	bl	JSHash
	cmp	r7, r0
	beq	.L2491
	str	r6, [r5, #16]
	ldr	r0, .L2493+4
	str	r6, [r4, #448]
	bl	printk
.L2491:
	ldr	r3, [r4, #448]
	mov	r0, #2
	ldr	r4, .L2493
	cmp	r3, #0
	ldrne	r2, .L2493+8
	ldrne	r3, .L2493
	ldr	r1, [r4, #972]
	strne	r2, [r3, #444]
	bl	StorageSysDataLoad
	ldr	r1, [r4, #976]
	mov	r0, #3
	bl	StorageSysDataLoad
	ldr	r0, .L2493+12
	ldmfd	sp!, {r3, r4, r5, r6, r7, lr}
	b	misc_register
.L2494:
	.align	2
.L2493:
	.word	.LANCHOR2
	.word	.LC160
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
	ldr	r0, .L2496
	bl	misc_deregister
	mov	r0, #0
	ldmfd	sp!, {r3, pc}
.L2497:
	.align	2
.L2496:
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
	.type	__func__.14422, %object
	.size	__func__.14422, 11
__func__.14422:
	.ascii	"FtlMemInit\000"
.LC0:
	.byte	60
	.byte	40
	.byte	24
	.byte	16
	.type	__func__.15298, %object
	.size	__func__.15298, 21
__func__.15298:
	.ascii	"FtlVpcCheckAndModify\000"
	.section	.rodata.str1.1,"aMS",%progbits,1
.LC1:
	.ascii	"Context allocation failed\012\000"
.LC2:
	.ascii	"FlashEraseBlocks pageAddr error %x \012\000"
.LC3:
	.ascii	"No.%d FLASH ID:%x %x %x %x %x %x\012\000"
.LC4:
	.ascii	"phyBlk = 0x%x die = %d block_in_die = 0x%x 0x%8x\012"
	.ascii	"\000"
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
	.ascii	"FTL version: 5.0.36 20170316\000"
.LC77:
	.ascii	"GetSwlReplaceBlock min_ec_id =%x %x\012\000"
.LC78:
	.ascii	"swblk %x ,avg = %x max= %x vpc= %x,ec=%x ,max ec=%x"
	.ascii	"\012\000"
.LC79:
	.ascii	"FtlGcRefreshBlock  0x%x \012\000"
.LC80:
	.ascii	"FtlGcMarkBadPhyBlk %d 0x%x\012\000"
.LC81:
	.ascii	"%s error allocating memory. return -1\012\000"
.LC82:
	.ascii	"%s 0x%x:\000"
.LC83:
	.ascii	"%x \000"
.LC84:
	.ascii	"\000"
.LC85:
	.ascii	"%d statReg->V6.mtrans_cnt=%d flReg.V6.page_num=%d\012"
	.ascii	"\000"
.LC86:
	.ascii	"nandc:\000"
.LC87:
	.ascii	"%d flReg.d32=%x %x\012\000"
.LC88:
	.ascii	"ECC:%d\012\000"
.LC89:
	.ascii	"sdr read ok %x ecc=%d\012\000"
.LC90:
	.ascii	"sync para %d\012\000"
.LC91:
	.ascii	"TOG mode Read error %x %x\012\000"
.LC92:
	.ascii	"FlashLoadPhyInfo fail %x!!\012\000"
.LC93:
	.ascii	"read retry status %x %x %x\012\000"
.LC94:
	.ascii	"Read pageadd=%x  ecc=%x err=%x\012\000"
.LC95:
	.ascii	"data:\000"
.LC96:
	.ascii	"spare:\000"
.LC97:
	.ascii	"ReadRetry pageadd=%x  ecc=%x err=%x\012\000"
.LC98:
	.ascii	"slc mode\000"
.LC99:
	.ascii	"no ect\000"
.LC100:
	.ascii	"prog error: = %x\012\000"
.LC101:
	.ascii	"prog read error: = %x\012\000"
.LC102:
	.ascii	"prog read s error: = %x %x %x\012\000"
.LC103:
	.ascii	"prog read d error: = %x %x %x\012\000"
.LC104:
	.ascii	"FtlBbmTblFlush id=%x,page=%x,previd=%x cnt=%d\012\000"
.LC105:
	.ascii	"FtlBbmTblFlush error:%x\012\000"
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
	.ascii	"spuer block %x vpn is 0\012 \000"
.LC114:
	.ascii	"...%s enter...\012\000"
.LC115:
	.ascii	"FtlCheckVpc %x = %x  %x\012\000"
.LC116:
	.ascii	"%d GC datablk  = %x vpc %x %x\012\000"
.LC117:
	.ascii	"SWL %x, FSB = %x vpc= %x,ec=%x th=%x\012\000"
.LC118:
	.ascii	"Ftlwrite decrement_vpc_count %x = %d\012\000"
.LC119:
	.ascii	"FtlInit %x\012\000"
.LC120:
	.ascii	"FtlWrite: lpa error:%x %x\012\000"
.LC121:
	.ascii	"FLFB:%d %d\012\000"
.LC122:
	.ascii	"BBT:\000"
.LC123:
	.ascii	"IdBlockReadData %x %x\012\000"
.LC124:
	.ascii	"IdBlockReadData %x %x ret= %x\012\000"
.LC125:
	.ascii	"IDBlockWriteData %x %x\012\000"
.LC126:
	.ascii	"IDBlockWriteData %x %x ret= %x\012\000"
.LC127:
	.ascii	"idblk:\000"
.LC128:
	.ascii	"idb reverse %x %x\012\000"
.LC129:
	.ascii	"write_idblock totle_sec %x %x\012\000"
.LC130:
	.ascii	"write and check error:%d idb=%x,offset=%x,r=%x,w=%x"
	.ascii	" \012\000"
.LC131:
	.ascii	"write\000"
.LC132:
	.ascii	"read\000"
.LC133:
	.ascii	"write_idblock error\012\000"
.LC134:
	.ascii	"READ_SECTOR_IO\012\000"
.LC135:
	.ascii	"rk_copy_from_user error \012\000"
.LC136:
	.ascii	"READ_SECTOR_IO %x %x\012\000"
.LC137:
	.ascii	"rk_copy_to_user error\012\000"
.LC138:
	.ascii	"WRITE_SECTOR_IO\012\000"
.LC139:
	.ascii	"WRITE_SECTOR_IO %x %x\012\000"
.LC140:
	.ascii	"END_WRITE_SECTOR_IO\012\000"
.LC141:
	.ascii	"END_WRITE_SECTOR_IO %x %x\012\000"
.LC142:
	.ascii	"GET_FLASH_INFO_IO\012\000"
.LC143:
	.ascii	"GET_BAD_BLOCK_IO\012\000"
.LC144:
	.ascii	"bbt:\000"
.LC145:
	.ascii	"GET_LOCK_FLAG_IO\012\000"
.LC146:
	.ascii	"GET_PUBLIC_KEY_IO\012\000"
.LC147:
	.ascii	"RKNAND_GET_DRM_KEY\012\000"
.LC148:
	.ascii	"RKNAND_STORE_DRM_KEY\012\000"
.LC149:
	.ascii	"copy_from_user error \012\000"
.LC150:
	.ascii	"RKNAND_DIASBLE_SECURE_BOOT\012\000"
.LC151:
	.ascii	"RKNAND_ENASBLE_SECURE_BOOT\012\000"
.LC152:
	.ascii	"RKNAND_GET_SN_SECTOR\012\000"
.LC153:
	.ascii	"RKNAND_LOADER_UNLOCK\012\000"
.LC154:
	.ascii	"RKNAND_LOADER_STATUS\012\000"
.LC155:
	.ascii	"RKNAND_LOADER_LOCK\012\000"
.LC156:
	.ascii	"LockKey not match %d\012\000"
.LC157:
	.ascii	"RKNAND_GET_VENDOR_SECTOR\012\000"
.LC158:
	.ascii	"RKNAND_STORE_VENDOR_SECTOR\012\000"
.LC159:
	.ascii	"return ret = %lx\012\000"
.LC160:
	.ascii	"secureBootEn check error\012\000"
.LC161:
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
	.word	.LC161
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
