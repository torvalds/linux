/*
 * New ftl for slc nand and spi nand
 * Copyright (c) 2016, Fuzhou Rockchip Electronics Co., Ltd
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 * date: 2017-9-28
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
	.file	"rk_sftl.c"
	.text
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
	b	.L2
.L3:
	add	r3, r3, #1
	mov	r2, r2, asl #1
	uxth	r3, r3
.L2:
	cmp	r2, r0
	bls	.L3
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
	ldr	r3, .L8
	cmp	r0, #23
	movls	r0, #24
	ldrh	r2, [r3, #4]
	ldrh	r1, [r3, #14]
	str	r0, [r3, #0]
	mul	r2, r2, r0
	rsb	r0, r0, r1
	ldr	r1, [r3, #20]
	strh	r0, [r3, #12]	@ movhi
	mov	r0, #0
	str	r2, [r3, #8]
	rsb	r2, r2, r1
	str	r2, [r3, #16]
	bx	lr
.L9:
	.align	2
.L8:
	.word	.LANCHOR0
	.fnend
	.size	FtlSysBlkNumInit, .-FtlSysBlkNumInit
	.global	__aeabi_idiv
	.global	__aeabi_uidiv
	.align	2
	.global	FtlConstantsInit
	.type	FtlConstantsInit, %function
FtlConstantsInit:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	stmfd	sp!, {r3, r4, r5, r6, r7, r8, r9, sl, fp, lr}
	.save {r3, r4, r5, r6, r7, r8, r9, sl, fp, lr}
	mov	sl, r0
	ldrh	r1, [r0, #0]
	ldr	r3, .L16
	ldrh	r5, [r0, #2]
	ldrh	r6, [sl, #6]
	ldrh	r0, [r0, #4]
	strh	r1, [r3, #24]	@ movhi
	strh	r5, [r3, #26]	@ movhi
	mov	r6, r6, lsr #1
	strh	r0, [r3, #28]	@ movhi
	strh	r6, [r3, #14]	@ movhi
	mov	r3, #0
.L11:
	ldr	r2, .L16
	add	ip, r2, #30
	strb	r3, [r3, ip]
	add	r3, r3, #1
	cmp	r3, #32
	bne	.L11
	mul	r5, r5, r0
	ldr	r4, .L16
	mul	r0, r0, r6
	cmp	r1, #1
	mov	r3, #5
	strh	r3, [r2, #62]	@ movhi
	streqh	r1, [r2, #62]	@ movhi
	mov	r3, #0
	strh	r3, [r2, #64]	@ movhi
	uxth	r5, r5
	mov	r8, #640
	strh	r5, [r4, #4]	@ movhi
	uxth	r0, r0
	strh	r8, [r4, #66]	@ movhi
	strh	r0, [r4, #68]	@ movhi
	bl	Ftl_log2
	ldrh	fp, [sl, #12]
	ldrh	r9, [sl, #14]
	mul	r3, fp, r5
	mov	r9, r9, asl #1
	strh	fp, [r4, #72]	@ movhi
	uxth	r9, r9
	strh	fp, [r4, #74]	@ movhi
	strh	r9, [r4, #78]	@ movhi
	strh	r3, [r4, #76]	@ movhi
	strh	r0, [r4, #70]	@ movhi
	mov	r0, r9
	bl	Ftl_log2
	mov	r3, r9, asl #9
	mul	r1, fp, r9
	uxth	r3, r3
	strh	r3, [r4, #82]	@ movhi
	mov	r3, r3, lsr #8
	strh	r3, [r4, #84]	@ movhi
	ldrh	r3, [sl, #20]
	strh	r3, [r4, #86]	@ movhi
	mul	r3, r6, r5
	mov	r6, r6, asl #6
	str	r3, [r4, #20]
	mul	r3, r9, r3
	mul	r3, fp, r3
	mov	r3, r3, asr #11
	str	r3, [r4, #88]
	mov	r7, r0
	strh	r0, [r4, #80]	@ movhi
	mov	r0, #5120
	mov	r8, r8, asr r7
	bl	__aeabi_idiv
	add	r7, r7, #9
	mov	r6, r6, asr r7
	mov	r1, r5
	add	r8, r8, #2
	strh	r8, [r4, #94]	@ movhi
	uxth	r6, r6
	strh	r6, [r4, #96]	@ movhi
	uxth	r0, r0
	strh	r0, [r4, #92]	@ movhi
	cmp	r0, #4
	movls	r3, #4
	strlsh	r3, [r4, #92]	@ movhi
	mul	r3, r5, r6
	ldrh	r0, [r4, #92]
	add	r6, r6, #8
	str	r3, [r4, #100]
	bl	__aeabi_uidiv
	cmp	r5, #1
	ldreq	r3, .L16
	uxtah	r6, r6, r0
	str	r6, [r4, #0]
	ldr	r4, .L16
	addeq	r6, r6, #4
	streq	r6, [r3, #0]
	ldrh	r0, [r4, #0]
	bl	FtlSysBlkNumInit
	ldr	r3, [r4, #0]
	ldrh	r2, [r4, #72]
	mov	r0, #0
	str	r0, [r4, #112]
	str	r3, [r4, #104]
	ldr	r3, [r4, #16]
	mov	r3, r3, asl #2
	mul	r3, r2, r3
	ldrh	r2, [r4, #80]
	add	r2, r2, #9
	mov	r3, r3, lsr r2
	add	r3, r3, #2
	strh	r3, [r4, #108]	@ movhi
	mov	r3, #32
	strh	r3, [r4, #110]	@ movhi
	ldrh	r3, [r4, #92]
	add	r3, r3, #3
	strh	r3, [r4, #92]	@ movhi
	ldr	r3, [r4, #100]
	add	r3, r3, #3
	str	r3, [r4, #100]
	ldmfd	sp!, {r3, r4, r5, r6, r7, r8, r9, sl, fp, pc}
.L17:
	.align	2
.L16:
	.word	.LANCHOR0
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
	ldr	r3, .L26
	ldrh	r2, [r3, #116]
	cmp	r2, #0
	ldrneh	r1, [r3, #92]
	ldrne	r2, [r3, #120]
	movne	r3, #0
	bne	.L20
	b	.L25
.L22:
	ldrh	ip, [r2], #2
	cmp	ip, r0
	beq	.L24
	add	r3, r3, #1
	uxth	r3, r3
.L20:
	cmp	r3, r1
	bne	.L22
	mov	r0, #0
	bx	lr
.L24:
	mov	r0, #1
	bx	lr
.L25:
	mov	r0, r2
	bx	lr
.L27:
	.align	2
.L26:
	.word	.LANCHOR0
	.fnend
	.size	IsBlkInVendorPart, .-IsBlkInVendorPart
	.align	2
	.global	sftl_get_density
	.type	sftl_get_density, %function
sftl_get_density:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	@ link register save eliminated.
	ldr	r3, .L29
	ldr	r0, [r3, #112]
	bx	lr
.L30:
	.align	2
.L29:
	.word	.LANCHOR0
	.fnend
	.size	sftl_get_density, .-sftl_get_density
	.align	2
	.global	FtlBbmMapBadBlock
	.type	FtlBbmMapBadBlock, %function
FtlBbmMapBadBlock:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	stmfd	sp!, {r0, r1, r4, r5, r6, lr}
	.save {r0, r1, r4, r5, r6, lr}
	mov	r5, r0
	ldr	r4, .L32
	ldrh	r6, [r4, #68]
	mov	r1, r6
	bl	__aeabi_uidiv
	uxth	r2, r0
	mls	r3, r6, r2, r5
	add	r1, r4, r2, asl #2
	mov	r6, #1
	ldr	r0, [r1, #152]
	uxth	r3, r3
	and	r1, r3, #31
	mov	ip, r3, lsr #5
	ldr	lr, [r0, ip, asl #2]
	orr	r1, lr, r6, asl r1
	str	r1, [r0, ip, asl #2]
	str	r1, [sp, #0]
	mov	r1, r5
	ldr	r0, .L32+4
	bl	printk
	ldrh	r3, [r4, #130]
	mov	r0, #0
	add	r3, r3, r6
	strh	r3, [r4, #130]	@ movhi
	ldmfd	sp!, {r2, r3, r4, r5, r6, pc}
.L33:
	.align	2
.L32:
	.word	.LANCHOR0
	.word	.LC0
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
	mov	r7, r0
	ldr	r5, .L35
	ldrh	r6, [r5, #68]
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
	ldr	r3, [r5, #152]
	ldr	r0, [r3, r2, asl #2]
	mov	r0, r0, lsr r4
	and	r0, r0, #1
	ldmfd	sp!, {r3, r4, r5, r6, r7, pc}
.L36:
	.align	2
.L35:
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
	mov	r4, #0
	mov	r5, r4
	ldr	r6, .L42
	b	.L39
.L41:
	mov	r0, r5
	add	r5, r5, #1
	bl	FtlBbmIsBadBlock
	uxth	r5, r5
	cmp	r0, #0
	addne	r4, r4, #1
	uxthne	r4, r4
.L39:
	ldrh	r3, [r6, #68]
	ldrh	r2, [r6, #26]
	mul	r3, r2, r3
	cmp	r5, r3
	blt	.L41
	mov	r0, r4
	ldmfd	sp!, {r4, r5, r6, pc}
.L43:
	.align	2
.L42:
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
	mov	r7, r1
	ldr	r5, .L45
	mov	r6, r0
	ldrh	r4, [r5, #28]
	mov	r1, r4
	bl	__aeabi_uidiv
	ldrh	r5, [r5, #68]
	mul	r7, r4, r7
	mov	r1, r4
	mla	r5, r5, r0, r7
	mov	r0, r6
	bl	__aeabi_uidivmod
	add	r1, r5, r1
	uxth	r0, r1
	ldmfd	sp!, {r3, r4, r5, r6, r7, pc}
.L46:
	.align	2
.L45:
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
	ldr	r3, .L48
	stmfd	sp!, {r4, r5, r6, lr}
	.save {r4, r5, r6, lr}
	mov	r6, r0
	ldrh	r1, [r3, #68]
	ldrh	r4, [r3, #28]
	bl	__aeabi_uidiv
	mov	r1, r4
	mul	r5, r0, r4
	mov	r0, r6
	bl	__aeabi_uidivmod
	add	r1, r5, r1
	uxth	r0, r1
	ldmfd	sp!, {r4, r5, r6, pc}
.L49:
	.align	2
.L48:
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
	ldr	r4, .L51
	ldrh	r1, [r4, #68]
	bl	__aeabi_uidivmod
	uxth	r0, r1
	ldrh	r1, [r4, #28]
	bl	__aeabi_uidiv
	uxth	r0, r0
	ldmfd	sp!, {r4, pc}
.L52:
	.align	2
.L51:
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
	bls	.L54
	rsb	r0, r1, r0
	cmp	r0, #-2147483648
	movhi	r0, #0
	movls	r0, #1
	bx	lr
.L54:
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
	ldr	r3, .L57
	ldrh	r0, [r3, #190]
	rsbs	r0, r0, #1
	movcc	r0, #0
	bx	lr
.L58:
	.align	2
.L57:
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
	ldr	r3, .L60
	ldrh	r0, [r3, #190]
	sub	r3, r0, #1024
	rsbs	r0, r3, #0
	adc	r0, r0, r3
	bx	lr
.L61:
	.align	2
.L60:
	.word	.LANCHOR0
	.fnend
	.size	FtlFreeSysBlkQueueFull, .-FtlFreeSysBlkQueueFull
	.align	2
	.global	FtlFreeSysBLkSort
	.type	FtlFreeSysBLkSort, %function
FtlFreeSysBLkSort:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	stmfd	sp!, {r3, r4, r5, r6, r7, lr}
	.save {r3, r4, r5, r6, r7, lr}
	mov	r5, #0
	ldr	r4, .L72
	b	.L63
.L64:
	ldrh	r3, [r4, #186]
	add	r3, r5, r3
	add	r3, r4, r3, asl #1
	ldrh	r0, [r3, #192]
	bl	P2V_block_in_plane
	ldr	r2, [r4, #2244]
	ldr	r3, [r4, #2240]
	mov	r0, r0, asl #1
	ldrh	r2, [r2, r0]
	str	r2, [r3, r5, asl #2]
	add	r5, r5, #1
	uxth	r5, r5
.L63:
	ldrh	r3, [r4, #190]
	cmp	r3, r5
	bhi	.L64
	mov	r3, #0
	ldr	r1, .L72
	b	.L65
.L70:
	add	r5, r3, #1
	ldr	r0, [r1, #2240]
	mov	r2, r3
	uxth	r5, r5
	mov	ip, r5
	b	.L66
.L68:
	ldr	r7, [r0, r2, asl #2]
	ldr	r6, [r0, ip, asl #2]
	cmp	r7, r6
	movhi	r2, ip
	add	ip, ip, #1
	uxth	ip, ip
.L66:
	cmp	ip, r4
	bcc	.L68
	cmp	r3, r2
	beq	.L69
	ldr	r4, [r0, r3, asl #2]
	ldr	ip, [r0, r2, asl #2]
	str	r4, [r0, r2, asl #2]
	ldr	r0, [r1, #2240]
	str	ip, [r0, r3, asl #2]
	ldrh	r0, [r1, #186]
	add	r2, r0, r2
	add	r3, r0, r3
	add	r2, r2, #4
	add	r3, r3, #4
	add	r2, r1, r2, asl #1
	add	r3, r1, r3, asl #1
	ldrh	ip, [r2, #184]
	ldrh	r0, [r3, #184]
	strh	r0, [r2, #184]	@ movhi
	strh	ip, [r3, #184]	@ movhi
.L69:
	mov	r3, r5
.L65:
	ldrh	r4, [r1, #190]
	sub	r2, r4, #1
	cmp	r3, r2
	blt	.L70
	ldmfd	sp!, {r3, r4, r5, r6, r7, pc}
.L73:
	.align	2
.L72:
	.word	.LANCHOR0
	.fnend
	.size	FtlFreeSysBLkSort, .-FtlFreeSysBLkSort
	.align	2
	.global	IsInFreeQueue
	.type	IsInFreeQueue, %function
IsInFreeQueue:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	ldr	r2, .L80
	stmfd	sp!, {r4, lr}
	.save {r4, lr}
	ldrh	r1, [r2, #190]
	cmp	r1, #1024
	ldrneh	ip, [r2, #186]
	movne	r3, #0
	bne	.L76
	b	.L78
.L77:
	add	r4, r3, ip
	mov	r4, r4, asl #22
	add	r4, r2, r4, lsr #21
	ldrh	r4, [r4, #192]
	cmp	r4, r0
	beq	.L79
	add	r3, r3, #1
.L76:
	cmp	r3, r1
	bcc	.L77
.L78:
	mov	r0, #0
	ldmfd	sp!, {r4, pc}
.L79:
	mov	r0, #1
	ldmfd	sp!, {r4, pc}
.L81:
	.align	2
.L80:
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
	ldr	r3, .L85
	ldrh	r1, [r3, #190]
	cmp	r1, #0
	subne	r1, r1, #1
	ldrneh	r2, [r3, #186]
	movweq	r0, #65535
	addne	r0, r3, r2, asl #1
	addne	r2, r2, #1
	bicne	r2, r2, #64512
	ldrneh	r0, [r0, #192]
	strneh	r1, [r3, #190]	@ movhi
	strneh	r2, [r3, #186]	@ movhi
	bx	lr
.L86:
	.align	2
.L85:
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
	beq	.L88
	ldr	r1, .L102
	mov	ip, #6
	mul	ip, ip, r0
	mvn	r6, #0
	ldr	r4, [r1, #2248]
	mov	r8, r1
	add	r2, r4, ip
	strh	r6, [r2, #2]	@ movhi
	strh	r6, [r4, ip]	@ movhi
	ldr	r3, [r1, #2252]
	cmp	r3, #0
	beq	.L101
.L89:
	ldr	r7, [r1, #2256]
	mov	sl, r0, asl #1
	ldrh	r1, [r2, #4]
	ldr	r9, .L102+4
	ldrh	r5, [r7, sl]
	cmp	r1, #0
	ldr	fp, [r8, #2244]
	mulne	r6, r1, r5
	ldr	r5, [r8, #2248]
	add	sl, fp, sl
	str	sl, [sp, #4]
	rsb	r1, r5, r3
	mov	r1, r1, asr #1
	mul	r1, r9, r1
	uxth	r1, r1
.L96:
	mov	r9, r1, asl #1
	ldrh	r8, [r3, #4]
	ldrh	sl, [r7, r9]
	cmp	r8, #0
	mvneq	r8, #0
	mulne	r8, r8, sl
	cmp	r8, r6
	bne	.L92
	ldrh	sl, [fp, r9]
	ldr	r9, [sp, #4]
	ldrh	r8, [r9, #0]
	cmp	sl, r8
	bcc	.L94
	b	.L93
.L92:
	bhi	.L93
.L94:
	ldrh	r8, [r3, #0]
	movw	sl, #65535
	cmp	r8, sl
	streqh	r1, [r2, #2]	@ movhi
	streqh	r0, [r3, #0]	@ movhi
	ldreq	r3, .L102
	streq	r2, [r3, #2260]
	beq	.L88
.L95:
	mov	r1, #6
	mla	r3, r1, r8, r5
	mov	r1, r8
	b	.L96
.L93:
	strh	r1, [r4, ip]	@ movhi
	ldrh	r1, [r3, #2]
	strh	r1, [r2, #2]	@ movhi
	ldr	r1, .L102
	ldr	ip, [r1, #2252]
	cmp	r3, ip
	bne	.L97
	strh	r0, [r3, #2]	@ movhi
.L101:
	str	r2, [r1, #2252]
	b	.L88
.L97:
	ldrh	r2, [r3, #2]
	mov	ip, #6
	ldr	r1, [r1, #2248]
	mul	r2, ip, r2
	strh	r0, [r1, r2]	@ movhi
	strh	r0, [r3, #2]	@ movhi
.L88:
	mov	r0, #0
	ldmfd	sp!, {r1, r2, r3, r4, r5, r6, r7, r8, r9, sl, fp, pc}
.L103:
	.align	2
.L102:
	.word	.LANCHOR0
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
	movw	r1, #2264
	ldr	r3, .L106
	ldrh	r2, [r3, r1]
	add	r2, r2, #1
	uxth	r2, r2
	strh	r2, [r3, r1]	@ movhi
	ldrh	r3, [r3, #12]
	cmp	r3, r2
	ldmcsfd	sp!, {r3, pc}
	ldr	r0, .L106+4
	mov	r2, #189
	ldr	r1, .L106+8
	ldmfd	sp!, {r3, lr}
	b	printk
.L107:
	.align	2
.L106:
	.word	.LANCHOR0
	.word	.LC1
	.word	.LANCHOR1
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
	beq	.L109
	ldr	r1, .L116
	mov	r5, #6
	mul	ip, r5, r0
	mvn	r3, #0
	ldr	r4, [r1, #2248]
	add	r2, r4, ip
	strh	r3, [r2, #2]	@ movhi
	strh	r3, [r4, ip]	@ movhi
	ldr	r3, [r1, #2268]
	cmp	r3, #0
	beq	.L115
.L110:
	ldr	sl, [r1, #2248]
	mov	r8, r0, asl #1
	ldr	r7, [r1, #2244]
	rsb	r1, sl, r3
	ldr	r9, .L116+4
	mov	r1, r1, asr #1
	ldrh	r8, [r7, r8]
	mul	r1, r9, r1
	mov	r9, r5
	uxth	r1, r1
.L113:
	mov	r5, r1, asl #1
	ldrh	r5, [r7, r5]
	cmp	r5, r8
	bcs	.L111
	ldrh	r5, [r3, #0]
	cmp	r5, r6
	streqh	r1, [r2, #2]	@ movhi
	streqh	r0, [r3, #0]	@ movhi
	beq	.L109
.L112:
	mla	r3, r9, r5, sl
	mov	r1, r5
	b	.L113
.L111:
	ldrh	r5, [r3, #2]
	strh	r5, [r2, #2]	@ movhi
	strh	r1, [r4, ip]	@ movhi
	ldr	r1, .L116
	ldr	ip, [r1, #2268]
	cmp	r3, ip
	bne	.L114
	strh	r0, [r3, #2]	@ movhi
.L115:
	str	r2, [r1, #2268]
	b	.L109
.L114:
	ldrh	r2, [r3, #2]
	mov	ip, #6
	ldr	r1, [r1, #2248]
	mul	r2, ip, r2
	strh	r0, [r1, r2]	@ movhi
	strh	r0, [r3, #2]	@ movhi
.L109:
	mov	r0, #0
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, sl, pc}
.L117:
	.align	2
.L116:
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
	mov	r1, #2272
	ldr	r3, .L120
	ldrh	r2, [r3, r1]
	add	r2, r2, #1
	uxth	r2, r2
	strh	r2, [r3, r1]	@ movhi
	ldrh	r3, [r3, #12]
	cmp	r3, r2
	ldmcsfd	sp!, {r3, pc}
	ldr	r0, .L120+4
	mov	r2, #182
	ldr	r1, .L120+8
	ldmfd	sp!, {r3, lr}
	b	printk
.L121:
	.align	2
.L120:
	.word	.LANCHOR0
	.word	.LC1
	.word	.LANCHOR1+17
	.fnend
	.size	INSERT_FREE_LIST, .-INSERT_FREE_LIST
	.align	2
	.global	List_remove_node
	.type	List_remove_node, %function
List_remove_node:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	stmfd	sp!, {r3, r4, r5, r6, r7, lr}
	.save {r3, r4, r5, r6, r7, lr}
	mov	r5, #6
	ldr	r3, .L128
	mov	r7, r0
	mul	r5, r5, r1
	ldr	r6, [r3, #2248]
	movw	r3, #65535
	add	r4, r6, r5
	ldrh	r2, [r4, #2]
	cmp	r2, r3
	bne	.L123
	ldr	r3, [r0, #0]
	cmp	r4, r3
	beq	.L123
	ldr	r0, .L128+4
	movw	r2, #337
	ldr	r1, .L128+8
	bl	printk
.L123:
	ldr	r3, [r7, #0]
	movw	r2, #65535
	cmp	r4, r3
	ldrh	r3, [r6, r5]
	bne	.L124
	cmp	r3, r2
	ldrne	r2, .L128
	movne	r1, #6
	moveq	r3, #0
	streq	r3, [r7, #0]
	ldrne	r2, [r2, #2248]
	mlane	r3, r1, r3, r2
	mvnne	r2, #0
	strne	r3, [r7, #0]
	strneh	r2, [r3, #2]	@ movhi
	b	.L126
.L124:
	cmp	r3, r2
	ldr	r1, .L128
	mov	r2, #6
	ldrh	r0, [r4, #2]
	muleq	r2, r2, r0
	ldreq	r3, [r1, #2248]
	mvneq	r1, #0
	streqh	r1, [r3, r2]	@ movhi
	beq	.L126
.L127:
	ldr	ip, [r1, #2248]
	mla	r3, r2, r3, ip
	strh	r0, [r3, #2]	@ movhi
	ldrh	ip, [r4, #2]
	ldrh	r0, [r6, r5]
	ldr	r3, [r1, #2248]
	mul	r2, r2, ip
	strh	r0, [r3, r2]	@ movhi
.L126:
	mvn	r3, #0
	mov	r0, #0
	strh	r3, [r6, r5]	@ movhi
	strh	r3, [r4, #2]	@ movhi
	ldmfd	sp!, {r3, r4, r5, r6, r7, pc}
.L129:
	.align	2
.L128:
	.word	.LANCHOR0
	.word	.LC1
	.word	.LANCHOR1+34
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
	beq	.L135
	ldr	r2, .L136
	movw	lr, #65535
	mov	ip, #6
	ldr	r4, [r2, #2248]
	b	.L132
.L134:
	mla	r3, ip, r2, r4
	sub	r1, r1, #1
	uxth	r1, r1
.L132:
	cmp	r1, #0
	beq	.L133
	ldrh	r2, [r3, #0]
	cmp	r2, lr
	bne	.L134
.L133:
	rsb	r4, r4, r3
	ldr	r3, .L136+4
	mov	r4, r4, asr #1
	mul	r4, r3, r4
	uxth	r4, r4
	mov	r1, r4
	bl	List_remove_node
	mov	r0, r4
	ldmfd	sp!, {r4, pc}
.L135:
	movw	r0, #65535
	ldmfd	sp!, {r4, pc}
.L137:
	.align	2
.L136:
	.word	.LANCHOR0
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
	ldr	r2, .L145
	ldr	r3, [r2, #2252]
	cmp	r3, #0
	beq	.L144
	ldr	r2, [r2, #2248]
	movw	r1, #65535
	mov	ip, #6
	b	.L141
.L143:
	mla	r3, ip, r3, r2
	sub	r0, r0, #1
	uxth	r0, r0
.L141:
	cmp	r0, #0
	beq	.L142
	ldrh	r3, [r3, #0]
	cmp	r3, r1
	bne	.L143
	mov	r0, r1
	bx	lr
.L142:
	rsb	r3, r2, r3
	ldr	r0, .L145+4
	mov	r3, r3, asr #1
	mul	r0, r0, r3
	uxth	r0, r0
	bx	lr
.L144:
	movw	r0, #65535
	bx	lr
.L146:
	.align	2
.L145:
	.word	.LANCHOR0
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
	stmfd	sp!, {r3, r4, r5, r6, r7, lr}
	.save {r3, r4, r5, r6, r7, lr}
	movw	r2, #2276
	ldr	r3, .L155
	mov	r4, r0
	ldrh	r2, [r3, r2]
	cmp	r2, r0
	beq	.L148
	movw	r2, #2324
	ldrh	r2, [r3, r2]
	cmp	r2, r0
	beq	.L148
	movw	r2, #2372
	ldrh	r2, [r3, r2]
	cmp	r2, r0
	beq	.L148
	mov	r2, #6
	ldr	r1, [r3, #2248]
	mul	r2, r2, r0
	ldr	r0, [r3, #2252]
	add	r5, r1, r2
	cmp	r5, r0
	beq	.L148
	ldr	r0, [r3, #2256]
	mov	r3, r4, asl #1
	ldrh	r6, [r5, #4]
	ldrh	r3, [r0, r3]
	muls	r6, r6, r3
	ldrh	r3, [r5, #2]
	mvneq	r6, #0
	movw	r0, #65535
	cmp	r3, r0
	bne	.L150
	ldrh	r2, [r1, r2]
	cmp	r2, r3
	bne	.L150
	ldr	r0, .L155+4
	movw	r2, #426
	ldr	r1, .L155+8
	bl	printk
.L150:
	ldr	r7, .L155
	mov	r1, #6
	ldrh	r2, [r5, #2]
	ldr	r3, [r7, #2248]
	mla	r2, r1, r2, r3
	ldr	r1, .L155+12
	rsb	r3, r3, r2
	mov	r3, r3, asr #1
	ldrh	r2, [r2, #4]
	mul	r3, r1, r3
	ldr	r1, [r7, #2256]
	uxth	r3, r3
	mov	r3, r3, asl #1
	ldrh	r3, [r1, r3]
	muls	r3, r2, r3
	mvneq	r3, #0
	cmp	r6, r3
	bcs	.L148
	ldr	r0, .L155+16
	mov	r1, r4
	movw	r5, #2264
	bl	List_remove_node
	ldrh	r3, [r7, r5]
	cmp	r3, #0
	bne	.L152
	ldr	r0, .L155+4
	movw	r2, #435
	ldr	r1, .L155+8
	bl	printk
.L152:
	ldr	r3, .L155
	mov	r0, r4
	ldrh	r2, [r3, r5]
	sub	r2, r2, #1
	strh	r2, [r3, r5]	@ movhi
	bl	INSERT_DATA_LIST
.L148:
	mov	r0, #0
	ldmfd	sp!, {r3, r4, r5, r6, r7, pc}
.L156:
	.align	2
.L155:
	.word	.LANCHOR0
	.word	.LC1
	.word	.LANCHOR1+51
	.word	-1431655765
	.word	.LANCHOR0+2252
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
	b	.L158
.L161:
	mov	r7, r3
	add	r3, r3, #2
	ldrh	r6, [r7, #0]
	cmp	r6, #0
	bne	.L159
	bl	FtlFreeSysBlkQueueOut
	cmp	r0, #0
	strh	r0, [r7, #0]	@ movhi
	beq	.L160
	ldr	r3, [r4, #28]
	strh	r6, [r4, #2]	@ movhi
	add	r3, r3, #1
	str	r3, [r4, #28]
	ldrh	r3, [r4, #8]
	strh	r5, [r4, #0]	@ movhi
	add	r3, r3, #1
	strh	r3, [r4, #8]	@ movhi
	b	.L160
.L159:
	add	r5, r5, #1
	uxth	r5, r5
.L158:
	cmp	r5, r2
	bne	.L161
.L160:
	ldrh	r3, [r4, #10]
	cmp	r3, r5
	bhi	.L162
	ldr	r0, .L163
	mov	r2, #532
	ldr	r1, .L163+4
	bl	printk
.L162:
	mov	r0, #0
	ldmfd	sp!, {r3, r4, r5, r6, r7, pc}
.L164:
	.align	2
.L163:
	.word	.LC1
	.word	.LANCHOR1+73
	.fnend
	.size	ftl_map_blk_alloc_new_blk, .-ftl_map_blk_alloc_new_blk
	.align	2
	.global	select_l2p_ram_region
	.type	select_l2p_ram_region, %function
select_l2p_ram_region:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	ldr	r2, .L177
	movw	r1, #65535
	stmfd	sp!, {r3, r4, r5, r6, r7, lr}
	.save {r3, r4, r5, r6, r7, lr}
	ldrh	r3, [r2, #110]
	ldr	r0, [r2, #2420]
	mov	r2, #0
	mov	r4, r2
	b	.L166
.L168:
	add	r2, r2, #12
	add	ip, r0, r2
	ldrh	ip, [ip, #-12]
	cmp	ip, r1
	beq	.L167
	add	r4, r4, #1
	uxth	r4, r4
.L166:
	cmp	r4, r3
	bne	.L168
	mov	r1, #0
	mov	r4, r3
	mov	r5, #-2147483648
	mov	r2, r1
	b	.L169
.L171:
	add	ip, r0, r1
	ldr	ip, [ip, #4]
	cmp	ip, #0
	blt	.L170
	cmp	ip, r5
	movcc	r5, ip
	movcc	r4, r2
.L170:
	add	r2, r2, #1
	add	r1, r1, #12
	uxth	r2, r2
.L169:
	cmp	r2, r3
	bne	.L171
	cmp	r4, r3
	bcc	.L167
	ldr	r1, .L177
	movw	r2, #2424
	mov	r4, r3
	mvn	ip, #0
	ldrh	r6, [r1, r2]
	mov	r1, #0
	mov	r2, r1
	b	.L172
.L174:
	add	r5, r0, r1
	ldr	r5, [r5, #4]
	cmp	r5, ip
	bcs	.L173
	ldrh	r7, [r0, r1]
	cmp	r7, r6
	movne	ip, r5
	movne	r4, r2
.L173:
	add	r2, r2, #1
	add	r1, r1, #12
	uxth	r2, r2
.L172:
	cmp	r2, r3
	bne	.L174
	cmp	r4, r2
	bcc	.L167
	ldr	r0, .L177+4
	movw	r2, #735
	ldr	r1, .L177+8
	bl	printk
.L167:
	mov	r0, r4
	ldmfd	sp!, {r3, r4, r5, r6, r7, pc}
.L178:
	.align	2
.L177:
	.word	.LANCHOR0
	.word	.LC1
	.word	.LANCHOR1+99
	.fnend
	.size	select_l2p_ram_region, .-select_l2p_ram_region
	.align	2
	.global	FtlUpdateVaildLpn
	.type	FtlUpdateVaildLpn, %function
FtlUpdateVaildLpn:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	ldr	r3, .L185
	movw	r2, #2426
	stmfd	sp!, {r4, r5, lr}
	.save {r4, r5, lr}
	ldrh	r1, [r3, r2]
	cmp	r1, #4
	add	ip, r1, #1
	mov	r1, r2
	strh	ip, [r3, r2]	@ movhi
	bhi	.L180
	cmp	r0, #0
	ldmeqfd	sp!, {r4, r5, pc}
.L180:
	mov	r2, #0
	ldrh	r4, [r3, #12]
	strh	r2, [r3, r1]	@ movhi
	movw	ip, #65535
	str	r2, [r3, #2428]
	ldr	r1, [r3, #2256]
	mov	r3, r2
	ldr	r2, .L185
	b	.L182
.L184:
	ldrh	r0, [r1], #2
	add	r3, r3, #1
	cmp	r0, ip
	uxth	r3, r3
	ldrne	r5, [r2, #2428]
	addne	r0, r0, r5
	strne	r0, [r2, #2428]
.L182:
	cmp	r3, r4
	bne	.L184
	ldmfd	sp!, {r4, r5, pc}
.L186:
	.align	2
.L185:
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
	ldr	r3, .L191
	mov	r2, r0, lsr #5
	cmp	r1, #0
	and	r0, r0, #31
	uxth	r2, r2
	mov	ip, #1
	ldr	r3, [r3, #2432]
	ldr	r1, [r3, r2, asl #2]
	orrne	r0, r1, ip, asl r0
	biceq	r0, r1, ip, asl r0
	str	r0, [r3, r2, asl #2]
	bx	lr
.L192:
	.align	2
.L191:
	.word	.LANCHOR0
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
	ldr	r3, .L194
	mov	r2, r0, lsr #5
	and	r0, r0, #31
	ldr	r3, [r3, #2432]
	ldr	r3, [r3, r2, asl #2]
	mov	r0, r3, lsr r0
	and	r0, r0, #1
	bx	lr
.L195:
	.align	2
.L194:
	.word	.LANCHOR0
	.fnend
	.size	ftl_get_blk_mode, .-ftl_get_blk_mode
	.align	2
	.global	ftl_sb_update_avl_pages
	.type	ftl_sb_update_avl_pages, %function
ftl_sb_update_avl_pages:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	mov	r3, #0
	strh	r3, [r0, #4]	@ movhi
	ldr	r3, .L203
	movw	ip, #65535
	stmfd	sp!, {r4, r5, r6, lr}
	.save {r4, r5, r6, lr}
	ldrh	r3, [r3, #4]
	b	.L197
.L199:
	add	r4, r0, r2, asl #1
	add	r2, r2, #1
	ldrh	r4, [r4, #16]
	uxth	r2, r2
	cmp	r4, ip
	ldrneh	r4, [r0, #4]
	addne	r4, r4, #1
	strneh	r4, [r0, #4]	@ movhi
.L197:
	cmp	r2, r3
	bcc	.L199
	ldr	r2, .L203
	mov	ip, r0
	movw	r4, #65535
	mvn	r1, r1
	ldrh	r5, [r2, #72]
	mov	r2, #0
	b	.L200
.L202:
	ldrh	r6, [ip, #16]
	add	r2, r2, #1
	add	ip, ip, #2
	cmp	r6, r4
	uxth	r2, r2
	ldrneh	r6, [r0, #4]
	addne	r6, r5, r6
	addne	r6, r6, r1
	strneh	r6, [r0, #4]	@ movhi
.L200:
	cmp	r2, r3
	bne	.L202
	ldmfd	sp!, {r4, r5, r6, pc}
.L204:
	.align	2
.L203:
	.word	.LANCHOR0
	.fnend
	.size	ftl_sb_update_avl_pages, .-ftl_sb_update_avl_pages
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
	ldrh	r2, [r0, #0]
	movw	r3, #65535
	cmp	r2, r3
	bxeq	lr
	ldrb	r2, [r0, #6]	@ zero_extendqisi2
	ldr	r1, .L210
	add	r2, r0, r2, asl #1
	ldrh	ip, [r1, #4]
	mov	r1, #0
	ldrh	r2, [r2, #16]
	b	.L207
.L209:
	ldrb	r2, [r0, #6]	@ zero_extendqisi2
	add	r2, r2, #1
	uxtb	r2, r2
	strb	r2, [r0, #6]
	cmp	r2, ip
	streqb	r1, [r0, #6]
	ldreqh	r2, [r0, #2]
	addeq	r2, r2, #1
	streqh	r2, [r0, #2]	@ movhi
	ldrb	r2, [r0, #6]	@ zero_extendqisi2
	add	r2, r0, r2, asl #1
	ldrh	r2, [r2, #16]
.L207:
	cmp	r2, r3
	beq	.L209
	bx	lr
.L211:
	.align	2
.L210:
	.word	.LANCHOR0
	.fnend
	.size	FtlSlcSuperblockCheck, .-FtlSlcSuperblockCheck
	.align	2
	.global	make_superblock
	.type	make_superblock, %function
make_superblock:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	stmfd	sp!, {r3, r4, r5, r6, r7, r8, sl, lr}
	.save {r3, r4, r5, r6, r7, r8, sl, lr}
	mov	r4, r0
	ldr	r3, .L218
	ldrh	r2, [r0, #0]
	ldrh	r3, [r3, #12]
	cmp	r2, r3
	bcc	.L213
	ldr	r0, .L218+4
	movw	r2, #2049
	ldr	r1, .L218+8
	bl	printk
.L213:
	mov	r5, #0
	ldr	r6, .L218
	strh	r5, [r4, #4]	@ movhi
	mvn	r7, #0
	strb	r5, [r4, #7]
	b	.L214
.L216:
	add	r3, r6, r5
	ldrh	r1, [r4, #0]
	add	r8, r5, #8
	add	r5, r5, #1
	ldrb	r0, [r3, #30]	@ zero_extendqisi2
	bl	V2P_block
	mov	r8, r8, asl #1
	uxth	r5, r5
	strh	r7, [r4, r8]	@ movhi
	mov	sl, r0
	bl	FtlBbmIsBadBlock
	cmp	r0, #0
	streqh	sl, [r4, r8]	@ movhi
	ldreqb	r3, [r4, #7]	@ zero_extendqisi2
	addeq	r3, r3, #1
	streqb	r3, [r4, #7]
.L214:
	ldrh	r2, [r6, #4]
	ldr	r3, .L218
	cmp	r2, r5
	bhi	.L216
	ldrb	r2, [r4, #7]	@ zero_extendqisi2
	ldrh	r1, [r3, #72]
	mul	r2, r1, r2
	strh	r2, [r4, #4]	@ movhi
	mov	r2, #0
	strb	r2, [r4, #9]
	ldr	r2, [r3, #2436]
	cmp	r2, #0
	beq	.L217
	ldrh	r1, [r4, #0]
	ldr	r2, [r3, #2244]
	mov	r3, r1, asl #1
	ldrh	r3, [r2, r3]
	cmp	r3, #59
	movls	r3, #1
	strlsb	r3, [r4, #9]
.L217:
	mov	r0, #0
	ldmfd	sp!, {r3, r4, r5, r6, r7, r8, sl, pc}
.L219:
	.align	2
.L218:
	.word	.LANCHOR0
	.word	.LC1
	.word	.LANCHOR1+121
	.fnend
	.size	make_superblock, .-make_superblock
	.align	2
	.global	update_multiplier_value
	.type	update_multiplier_value, %function
update_multiplier_value:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	stmfd	sp!, {r3, r4, r5, r6, r7, lr}
	.save {r3, r4, r5, r6, r7, lr}
	mov	r4, #0
	mov	r7, r0
	mov	r5, r4
	ldr	r6, .L225
	b	.L221
.L223:
	add	r3, r6, r5
	mov	r1, r7
	add	r5, r5, #1
	ldrb	r0, [r3, #30]	@ zero_extendqisi2
	bl	V2P_block
	uxth	r5, r5
	bl	FtlBbmIsBadBlock
	cmp	r0, #0
	ldreqh	r3, [r6, #72]
	addeq	r4, r4, r3
	uxtheq	r4, r4
.L221:
	ldrh	r3, [r6, #4]
	cmp	r3, r5
	bhi	.L223
	cmp	r4, #0
	beq	.L224
	mov	r1, r4
	mov	r0, #32768
	bl	__aeabi_idiv
	uxth	r4, r0
.L224:
	ldr	r3, .L225
	mov	r2, #6
	mov	r0, #0
	ldr	r3, [r3, #2248]
	mla	r7, r2, r7, r3
	strh	r4, [r7, #4]	@ movhi
	ldmfd	sp!, {r3, r4, r5, r6, r7, pc}
.L226:
	.align	2
.L225:
	.word	.LANCHOR0
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
	ldr	r3, .L230
	ldr	r0, [r3, #2268]
	cmp	r0, #0
	bxeq	lr
	ldr	r2, [r3, #2248]
	rsb	r0, r2, r0
	ldr	r2, .L230+4
	mov	r0, r0, asr #1
	mul	r0, r2, r0
	ldr	r2, [r3, #2244]
	uxth	r0, r0
	mov	r3, r0, asl #1
	ldrh	r0, [r2, r3]
	bx	lr
.L231:
	.align	2
.L230:
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
	ldr	r2, .L239
	stmfd	sp!, {r4, r5, lr}
	.save {r4, r5, lr}
	ldr	r3, [r2, #2268]
	cmp	r3, #0
	beq	.L238
	mov	r1, #2272
	mov	ip, #7
	ldrh	r1, [r2, r1]
	mov	r5, #6
	movw	r4, #65535
	mul	r1, ip, r1
	mov	r1, r1, asr #3
	cmp	r0, r1
	uxthgt	r0, r1
	ldr	r1, [r2, #2248]
	ldr	r2, .L239+4
	rsb	r3, r1, r3
	mov	r3, r3, asr #1
	mul	r3, r2, r3
	mov	r2, #0
	uxth	r3, r3
	b	.L235
.L237:
	mul	ip, r5, r3
	ldrh	ip, [r1, ip]
	cmp	ip, r4
	beq	.L236
	add	r2, r2, #1
	mov	r3, ip
	uxth	r2, r2
.L235:
	cmp	r2, r0
	bne	.L237
.L236:
	ldr	r2, .L239
	mov	r3, r3, asl #1
	ldr	r2, [r2, #2244]
	ldrh	r0, [r2, r3]
	ldmfd	sp!, {r4, r5, pc}
.L238:
	mov	r0, r3
	ldmfd	sp!, {r4, r5, pc}
.L240:
	.align	2
.L239:
	.word	.LANCHOR0
	.word	-1431655765
	.fnend
	.size	GetFreeBlockMaxEraseCount, .-GetFreeBlockMaxEraseCount
	.align	2
	.global	FtlPrintInfo2buf
	.type	FtlPrintInfo2buf, %function
FtlPrintInfo2buf:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	stmfd	sp!, {r0, r1, r2, r3, r4, r5, r6, r7, r8, r9, sl, lr}
	.save {r0, r1, r2, r3, r4, r5, r6, r7, r8, r9, sl, lr}
	mov	r8, r0
	ldr	r4, .L251
	add	r5, r8, #12
	ldr	r1, .L251+4
	mov	r7, #2272
	bl	strcpy
	mov	r0, r5
	ldr	r2, [r4, #88]
	movw	r6, #2276
	ldr	r1, .L251+8
	bl	sprintf
	ldr	r1, .L251+12
	add	r5, r5, r0
	mov	r0, r5
	add	r5, r5, #10
	bl	strcpy
	ldr	r1, .L251+16
	mov	r0, r5
	ldr	r2, [r4, #2440]
	bl	sprintf
	ldr	r1, .L251+20
	ldr	r2, [r4, #2428]
	add	r5, r5, r0
	mov	r0, r5
	bl	sprintf
	ldr	r1, .L251+24
	ldr	r2, [r4, #2444]
	add	r5, r5, r0
	mov	r0, r5
	bl	sprintf
	ldr	r1, .L251+28
	ldr	r2, [r4, #2448]
	add	r5, r5, r0
	mov	r0, r5
	bl	sprintf
	ldr	r1, .L251+32
	ldr	r2, [r4, #2452]
	add	r5, r5, r0
	mov	r0, r5
	bl	sprintf
	ldr	r1, .L251+36
	ldr	r2, [r4, #2456]
	add	r5, r5, r0
	mov	r0, r5
	bl	sprintf
	ldr	r1, .L251+40
	ldr	r2, [r4, #2460]
	add	r5, r5, r0
	mov	r0, r5
	bl	sprintf
	ldr	r1, .L251+44
	ldr	r2, [r4, #2464]
	add	r5, r5, r0
	mov	r0, r5
	bl	sprintf
	ldr	r2, [r4, #2468]
	ldr	r1, .L251+48
	mov	r2, r2, lsr #11
	add	r5, r5, r0
	mov	r0, r5
	bl	sprintf
	ldr	r2, [r4, #2472]
	ldr	r1, .L251+52
	mov	r2, r2, lsr #11
	add	r5, r5, r0
	mov	r0, r5
	bl	sprintf
	ldr	r1, .L251+56
	ldr	r2, [r4, #2476]
	add	r5, r5, r0
	mov	r0, r5
	bl	sprintf
	ldr	r1, .L251+60
	ldr	r2, [r4, #2480]
	add	r5, r5, r0
	mov	r0, r5
	bl	sprintf
	ldr	r1, .L251+64
	ldrh	r2, [r4, #130]
	add	r5, r5, r0
	mov	r0, r5
	bl	sprintf
	ldr	r1, .L251+68
	ldrh	r2, [r4, r7]
	add	r5, r5, r0
	mov	r0, r5
	bl	sprintf
	ldr	r1, .L251+72
	ldr	r2, [r4, #2484]
	add	r5, r5, r0
	mov	r0, r5
	bl	sprintf
	ldr	r1, .L251+76
	ldr	r2, [r4, #2488]
	add	r5, r5, r0
	mov	r0, r5
	bl	sprintf
	ldr	r1, .L251+80
	ldr	r2, [r4, #2492]
	add	r5, r5, r0
	mov	r0, r5
	bl	sprintf
	ldr	r1, .L251+84
	ldr	r2, [r4, #2496]
	add	r5, r5, r0
	mov	r0, r5
	bl	sprintf
	ldr	r1, .L251+88
	ldr	r2, [r4, #2500]
	add	r5, r5, r0
	mov	r0, r5
	bl	sprintf
	ldr	r1, .L251+92
	ldr	r2, [r4, #2504]
	add	r5, r5, r0
	mov	r0, r5
	bl	sprintf
	movw	r3, #2538
	ldrh	r2, [r4, r3]
	ldr	r1, .L251+96
	add	r5, r5, r0
	mov	r0, r5
	bl	sprintf
	movw	r3, #2536
	ldrh	r2, [r4, r3]
	ldr	r1, .L251+100
	add	r5, r5, r0
	mov	r0, r5
	bl	sprintf
	ldr	r1, .L251+104
	ldr	r2, [r4, #112]
	add	r5, r5, r0
	mov	r0, r5
	bl	sprintf
	ldr	r1, .L251+108
	ldr	r2, [r4, #104]
	add	r5, r5, r0
	mov	r0, r5
	bl	sprintf
	ldr	r1, .L251+112
	ldr	r2, [r4, #0]
	add	r5, r5, r0
	mov	r0, r5
	bl	sprintf
	ldr	r1, .L251+116
	ldrh	r2, [r4, #190]
	add	r5, r5, r0
	mov	r0, r5
	bl	sprintf
	ldr	r1, .L251+120
	ldrh	r2, [r4, #12]
	add	r5, r5, r0
	mov	r0, r5
	bl	sprintf
	movw	r3, #2556
	ldrh	r2, [r4, r3]
	ldr	r1, .L251+124
	add	r5, r5, r0
	mov	r0, r5
	bl	sprintf
	ldr	r1, .L251+128
	ldr	r2, [r4, #16]
	add	r5, r5, r0
	mov	r0, r5
	bl	sprintf
	mov	r3, #2560
	ldrh	r2, [r4, r3]
	ldr	r1, .L251+132
	add	r5, r5, r0
	mov	r0, r5
	bl	sprintf
	ldr	r1, .L251+136
	ldrh	r2, [r4, #124]
	add	r5, r5, r0
	mov	r0, r5
	bl	sprintf
	movw	r3, #2278
	ldrh	r2, [r4, r3]
	ldr	r1, .L251+140
	add	r5, r5, r0
	mov	r0, r5
	bl	sprintf
	ldr	r1, .L251+144
	ldrb	r2, [r4, #2282]	@ zero_extendqisi2
	add	r5, r5, r0
	mov	r0, r5
	bl	sprintf
	ldrh	r2, [r4, r6]
	ldr	r1, .L251+148
	add	r5, r5, r0
	mov	r0, r5
	bl	sprintf
	ldr	r1, .L251+152
	ldrb	r2, [r4, #2284]	@ zero_extendqisi2
	add	r5, r5, r0
	mov	r0, r5
	bl	sprintf
	movw	r3, #2280
	ldrh	r2, [r4, r3]
	ldr	r1, .L251+156
	add	r5, r5, r0
	mov	r0, r5
	bl	sprintf
	ldrh	r3, [r4, r6]
	ldr	r2, [r4, #2256]
	add	r6, r6, #48
	ldr	r1, .L251+160
	mov	r3, r3, asl #1
	ldrh	r2, [r2, r3]
	add	r5, r5, r0
	mov	r0, r5
	bl	sprintf
	movw	r3, #2326
	ldrh	r2, [r4, r3]
	ldr	r1, .L251+164
	add	r5, r5, r0
	mov	r0, r5
	bl	sprintf
	ldr	r1, .L251+168
	ldrb	r2, [r4, #2330]	@ zero_extendqisi2
	add	r5, r5, r0
	mov	r0, r5
	bl	sprintf
	ldrh	r2, [r4, r6]
	ldr	r1, .L251+172
	add	r5, r5, r0
	mov	r0, r5
	bl	sprintf
	ldr	r1, .L251+176
	ldrb	r2, [r4, #2332]	@ zero_extendqisi2
	add	r5, r5, r0
	mov	r0, r5
	bl	sprintf
	movw	r3, #2328
	ldrh	r2, [r4, r3]
	ldr	r1, .L251+180
	add	r5, r5, r0
	mov	r0, r5
	bl	sprintf
	ldrh	r3, [r4, r6]
	ldr	r2, [r4, #2256]
	add	r6, r6, #248
	ldr	r1, .L251+184
	mov	r3, r3, asl #1
	ldrh	r2, [r2, r3]
	add	r5, r5, r0
	mov	r0, r5
	bl	sprintf
	movw	r3, #2374
	ldrh	r2, [r4, r3]
	ldr	r1, .L251+188
	add	r5, r5, r0
	mov	r0, r5
	bl	sprintf
	ldr	r1, .L251+192
	ldrb	r2, [r4, #2378]	@ zero_extendqisi2
	add	r5, r5, r0
	mov	r0, r5
	bl	sprintf
	movw	r3, #2372
	ldrh	r2, [r4, r3]
	ldr	r1, .L251+196
	add	r5, r5, r0
	mov	r0, r5
	bl	sprintf
	ldr	r1, .L251+200
	ldrb	r2, [r4, #2380]	@ zero_extendqisi2
	add	r5, r5, r0
	mov	r0, r5
	bl	sprintf
	movw	r3, #2376
	ldrh	r2, [r4, r3]
	ldr	r1, .L251+204
	add	r5, r5, r0
	mov	r0, r5
	bl	sprintf
	movw	r3, #2574
	ldrh	r2, [r4, r3]
	ldr	r1, .L251+208
	add	r5, r5, r0
	mov	r0, r5
	bl	sprintf
	ldr	r1, .L251+212
	ldrb	r2, [r4, #2578]	@ zero_extendqisi2
	add	r5, r5, r0
	mov	r0, r5
	bl	sprintf
	ldr	r1, .L251+216
	ldrh	r2, [r4, r6]
	add	r5, r5, r0
	mov	r0, r5
	bl	sprintf
	ldr	r1, .L251+220
	ldrb	r2, [r4, #2580]	@ zero_extendqisi2
	add	r5, r5, r0
	mov	r0, r5
	bl	sprintf
	mov	r3, #2576
	ldrh	r2, [r4, r3]
	ldr	r1, .L251+224
	add	r5, r5, r0
	mov	r0, r5
	bl	sprintf
	ldr	r1, [r4, #2708]
	ldr	r3, [r4, #2436]
	ldr	r2, [r4, #2620]
	str	r1, [sp, #0]
	ldr	r1, [r4, #2700]
	orr	r2, r3, r2, asl #8
	str	r1, [sp, #4]
	ldr	r1, .L251+228
	ldr	r3, [r4, #2704]
	add	r5, r5, r0
	mov	r0, r5
	bl	sprintf
	ldr	r1, .L251+232
	ldr	r2, [r4, #2696]
	add	r5, r5, r0
	mov	r0, r5
	bl	sprintf
	ldr	r1, .L251+236
	ldr	r2, [r4, #2720]
	add	r5, r5, r0
	mov	r0, r5
	bl	sprintf
	mov	r3, #3136
	ldrh	r2, [r4, r3]
	ldr	r1, .L251+240
	add	r5, r5, r0
	mov	r0, r5
	bl	sprintf
	movw	r3, #3138
	ldrh	r2, [r4, r3]
	ldr	r1, .L251+244
	add	r5, r5, r0
	mov	r0, r5
	bl	sprintf
	ldr	r1, .L251+248
	ldr	r2, [r4, #3140]
	add	r5, r5, r0
	mov	r0, r5
	bl	sprintf
	movw	r3, #3144
	ldrh	r2, [r4, r3]
	ldr	r1, .L251+252
	add	r5, r5, r0
	mov	r0, r5
	bl	sprintf
	add	r5, r5, r0
	bl	GetFreeBlockMinEraseCount
	ldr	r1, .L251+256
	mov	r2, r0
	mov	r0, r5
	bl	sprintf
	add	r5, r5, r0
	ldrh	r0, [r4, r7]
	bl	GetFreeBlockMaxEraseCount
	ldr	r1, .L251+260
	mov	r2, r0
	mov	r0, r5
	bl	sprintf
	ldr	r3, .L251+264
	ldr	r3, [r3, #0]
	cmp	r3, #1
	add	r5, r5, r0
	bne	.L247
.L242:
	ldrh	r3, [r4, r6]
	movw	r2, #65535
	cmp	r3, r2
	beq	.L244
	ldr	r2, [r4, #2256]
	mov	r3, r3, asl #1
	mov	r0, r5
	ldr	r1, .L251+268
	ldrh	r2, [r2, r3]
	bl	sprintf
	add	r5, r5, r0
.L244:
	mov	r0, #0
	ldr	r4, .L251
	bl	List_get_gc_head_node
	mov	r6, #0
	movw	sl, #65535
	mov	r9, #6
	uxth	r3, r0
.L246:
	cmp	r3, sl
	beq	.L245
	ldr	r1, [r4, #2256]
	mov	r2, r3, asl #1
	mul	r7, r9, r3
	mov	r0, r5
	ldrh	r1, [r1, r2]
	str	r1, [sp, #0]
	ldr	r1, [r4, #2248]
	add	r1, r1, r7
	ldrh	r1, [r1, #4]
	str	r1, [sp, #4]
	ldr	r1, [r4, #2244]
	ldrh	r2, [r1, r2]
	ldr	r1, .L251+272
	str	r2, [sp, #8]
	mov	r2, r6
	bl	sprintf
	add	r6, r6, #1
	ldr	r3, [r4, #2248]
	cmp	r6, #16
	ldrh	r3, [r3, r7]
	add	r5, r5, r0
	bne	.L246
.L245:
	ldr	r6, .L251
	mov	r4, #0
	movw	sl, #65535
	mov	r9, #6
	ldr	r2, [r6, #2268]
	ldr	r3, [r6, #2248]
	rsb	r3, r3, r2
	ldr	r2, .L251+276
	mov	r3, r3, asr #1
	mul	r3, r2, r3
	uxth	r3, r3
.L248:
	cmp	r3, sl
	beq	.L247
	mul	r7, r9, r3
	ldr	r2, [r6, #2248]
	mov	r0, r5
	add	r2, r2, r7
	ldrh	r2, [r2, #4]
	str	r2, [sp, #0]
	mov	r2, r3, asl #1
	ldr	r1, [r6, #2244]
	ldrh	r2, [r1, r2]
	ldr	r1, .L251+280
	str	r2, [sp, #4]
	mov	r2, r4
	bl	sprintf
	add	r4, r4, #1
	ldr	r3, [r6, #2248]
	cmp	r4, #4
	ldrh	r3, [r3, r7]
	add	r5, r5, r0
	bne	.L248
.L247:
	rsb	r0, r8, r5
	add	sp, sp, #16
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, sl, pc}
.L252:
	.align	2
.L251:
	.word	.LANCHOR0
	.word	.LC2
	.word	.LC3
	.word	.LC4
	.word	.LC5
	.word	.LC6
	.word	.LC7
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
	.word	.LANCHOR2
	.word	.LC67
	.word	.LC68
	.word	-1431655765
	.word	.LC69
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
	ldr	r1, .L254
	ldr	r2, .L254+4
	bl	sprintf
	add	r5, r4, r0
	mov	r0, r5
	bl	FtlPrintInfo2buf
	add	r0, r5, r0
	rsb	r0, r4, r0
	ldmfd	sp!, {r3, r4, r5, pc}
.L255:
	.align	2
.L254:
	.word	.LC70
	.word	.LC71
	.fnend
	.size	rknand_proc_ftlread, .-rknand_proc_ftlread
	.align	2
	.global	GetSwlReplaceBlock
	.type	GetSwlReplaceBlock, %function
GetSwlReplaceBlock:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	ldr	r3, .L283
	stmfd	sp!, {r4, r5, r6, r7, r8, r9, sl, fp, lr}
	.save {r4, r5, r6, r7, r8, r9, sl, fp, lr}
	.pad #20
	sub	sp, sp, #20
	ldr	r1, [r3, #2492]
	ldr	r2, [r3, #2504]
	cmp	r1, r2
	bcs	.L257
	mov	r2, #0
	ldrh	r1, [r3, #12]
	str	r2, [r3, #2484]
	ldr	r0, [r3, #2244]
	b	.L258
.L259:
	ldrh	lr, [r0], #2
	add	r2, r2, #1
	ldr	ip, [r3, #2484]
	add	ip, lr, ip
	str	ip, [r3, #2484]
.L258:
	cmp	r2, r1
	ldr	r4, .L283
	bcc	.L259
	ldr	r5, [r4, #2484]
	mov	r0, r5
	bl	__aeabi_uidiv
	ldrh	r1, [r4, #62]
	str	r0, [r4, #2492]
	ldr	r0, [r4, #2488]
	rsb	r0, r0, r5
	bl	__aeabi_uidiv
	str	r0, [r4, #2484]
	b	.L260
.L257:
	ldr	r2, [r3, #2500]
	cmp	r1, r2
	addhi	r2, r2, #1
	strhi	r2, [r3, #2500]
	movhi	r2, #0
	bhi	.L261
	b	.L260
.L262:
	ldr	r0, [r3, #2244]
	mov	r1, r2, asl #1
	add	r2, r2, #1
	ldrh	ip, [r0, r1]
	add	ip, ip, #1
	strh	ip, [r0, r1]	@ movhi
.L261:
	ldrh	r1, [r3, #12]
	cmp	r2, r1
	bcc	.L262
.L260:
	ldr	r3, .L283
	ldr	r6, [r3, #2504]
	ldr	r5, [r3, #2492]
	add	r2, r6, #256
	cmp	r2, r5
	mov	r2, r3
	bls	.L263
	ldr	r1, [r3, #2500]
	add	r0, r6, #768
	cmp	r0, r1
	bls	.L263
	ldr	r3, [r3, #2436]
	cmp	r3, #0
	beq	.L281
	cmp	r6, #30
	bhi	.L281
.L263:
	mov	r3, #2272
	ldrh	r0, [r2, r3]
	add	r0, r0, r0, asl #1
	ubfx	r0, r0, #2, #16
	bl	GetFreeBlockMaxEraseCount
	add	r3, r6, #64
	cmp	r0, r3
	mov	r8, r0
	bcs	.L265
	cmp	r6, #30
	bhi	.L281
.L265:
	ldr	r2, .L283
	ldr	r3, [r2, #2252]
	cmp	r3, #0
	beq	.L281
	movw	r7, #65535
	ldr	r1, [r2, #2248]
	mov	r4, r7
	ldr	r2, [r2, #2244]
	mov	sl, r7
	ldr	ip, .L283+4
	mov	fp, #6
	b	.L266
.L269:
	ldrh	r9, [r3, #4]
	cmp	r9, #0
	beq	.L267
	rsb	r3, r1, r3
	mov	r3, r3, asr #1
	mul	r3, ip, r3
	uxth	r3, r3
	mov	r9, r3, asl #1
	ldrh	r9, [r2, r9]
	cmp	r9, r6
	bls	.L278
	cmp	r9, r7
	movcc	r7, r9
	movcc	r4, r3
.L267:
	mla	r3, fp, r0, r1
.L266:
	ldrh	r0, [r3, #0]
	cmp	r0, sl
	bne	.L269
	b	.L268
.L278:
	mov	r4, r3
.L268:
	movw	r3, #65535
	cmp	r4, r3
	beq	.L264
	mov	sl, r4, asl #1
	ldr	fp, .L283
	ldrh	r9, [r2, sl]
	cmp	r9, r6
	bls	.L270
	bl	GetFreeBlockMinEraseCount
	cmp	r0, r6
	strhi	r7, [fp, #2504]
.L270:
	cmp	r9, #29
	ldr	r2, [fp, #2436]
	movhi	r3, #0
	movls	r3, #1
	cmp	r2, #0
	moveq	r3, #0
	cmp	r3, #0
	beq	.L271
	add	r3, r9, #10
	cmp	r3, r5
	bls	.L272
	ldr	r3, .L283
	ldrh	r3, [r3, #62]
	cmp	r3, r9
	bls	.L271
.L272:
	ldr	r6, .L283
	movw	r3, #2264
	ldrh	r3, [r6, r3]
	cmp	r3, #64
	bls	.L271
	ldr	r3, [r6, #2244]
	mov	r1, r4
	ldr	r0, .L283+8
	ldrh	r2, [r3, sl]
	bl	printk
	b	.L282
.L271:
	cmp	r9, r5
	bcs	.L281
	add	r3, r9, #128
	cmp	r8, r3
	ble	.L281
	add	r3, r9, #256
	ldr	r6, .L283
	cmp	r3, r5
	bcc	.L273
	ldr	r3, [r6, #2500]
	add	r9, r9, #768
	cmp	r9, r3
	bcs	.L281
.L273:
	ldr	r3, [r6, #2256]
	mov	r1, r4
	ldr	r0, .L283+12
	mov	r2, r5
	ldrh	r3, [r3, sl]
	str	r3, [sp, #0]
	ldr	r3, [r6, #2244]
	ldrh	r3, [r3, sl]
	stmib	sp, {r3, r8}
	ldr	r3, [r6, #2500]
	bl	printk
.L282:
	mov	r3, #1
	str	r3, [r6, #3148]
	b	.L264
.L281:
	movw	r4, #65535
.L264:
	mov	r0, r4
	add	sp, sp, #20
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, sl, fp, pc}
.L284:
	.align	2
.L283:
	.word	.LANCHOR0
	.word	-1431655765
	.word	.LC72
	.word	.LC73
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
	beq	.L286
	ldr	r2, .L287
	mov	r3, r0, asl #1
	mov	r1, #0
	ldr	r2, [r2, #2256]
	strh	r1, [r2, r3]	@ movhi
	bl	INSERT_FREE_LIST
.L286:
	mov	r0, #0
	ldmfd	sp!, {r3, pc}
.L288:
	.align	2
.L287:
	.word	.LANCHOR0
	.fnend
	.size	free_data_superblock, .-free_data_superblock
	.align	2
	.global	get_new_active_ppa
	.type	get_new_active_ppa, %function
get_new_active_ppa:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	stmfd	sp!, {r3, r4, r5, lr}
	.save {r3, r4, r5, lr}
	movw	r3, #65535
	ldrh	r2, [r0, #0]
	mov	r4, r0
	cmp	r2, r3
	bne	.L290
	ldr	r0, .L300
	movw	r2, #2710
	ldr	r1, .L300+4
	bl	printk
.L290:
	ldr	r3, .L300+8
	ldrh	r2, [r4, #2]
	ldrh	r3, [r3, #72]
	cmp	r2, r3
	bne	.L291
	ldr	r0, .L300
	movw	r2, #2711
	ldr	r1, .L300+4
	bl	printk
.L291:
	ldrh	r3, [r4, #4]
	cmp	r3, #0
	bne	.L292
	ldr	r0, .L300
	movw	r2, #2712
	ldr	r1, .L300+4
	bl	printk
.L292:
	ldrb	r3, [r4, #6]	@ zero_extendqisi2
	mov	r2, #0
	ldr	r1, .L300+8
	strb	r2, [r4, #10]
	add	r3, r4, r3, asl #1
	ldrh	r0, [r1, #4]
	movw	r1, #65535
	ldrh	r3, [r3, #16]
	b	.L293
.L295:
	ldrb	r3, [r4, #6]	@ zero_extendqisi2
	add	r3, r3, #1
	uxtb	r3, r3
	strb	r3, [r4, #6]
	cmp	r3, r0
	streqb	r2, [r4, #6]
	ldreqh	r3, [r4, #2]
	addeq	r3, r3, #1
	streqh	r3, [r4, #2]	@ movhi
	ldrb	r3, [r4, #6]	@ zero_extendqisi2
	add	r3, r4, r3, asl #1
	ldrh	r3, [r3, #16]
.L293:
	cmp	r3, r1
	beq	.L295
	ldrh	r5, [r4, #2]
	movw	r1, #65535
	ldrh	r2, [r4, #4]
	orr	r5, r5, r3, asl #10
	ldr	r3, .L300+8
	sub	r2, r2, #1
	ldrh	r0, [r3, #4]
	uxth	r2, r2
	ldrb	r3, [r4, #6]	@ zero_extendqisi2
	strh	r2, [r4, #4]	@ movhi
.L297:
	add	r3, r3, #1
	uxtb	r3, r3
	cmp	r3, r0
	ldreqh	r3, [r4, #2]
	addeq	r3, r3, #1
	streqh	r3, [r4, #2]	@ movhi
	moveq	r3, #0
	add	ip, r4, r3, asl #1
	ldrh	ip, [ip, #16]
	cmp	ip, r1
	beq	.L297
	strb	r3, [r4, #6]
	ldr	r3, .L300+8
	ldrh	r1, [r4, #2]
	ldrh	r3, [r3, #72]
	cmp	r1, r3
	bne	.L298
	cmp	r2, #0
	beq	.L298
	ldr	r0, .L300
	movw	r2, #2733
	ldr	r1, .L300+4
	bl	printk
.L298:
	mov	r0, r5
	ldmfd	sp!, {r3, r4, r5, pc}
.L301:
	.align	2
.L300:
	.word	.LC1
	.word	.LANCHOR1+137
	.word	.LANCHOR0
	.fnend
	.size	get_new_active_ppa, .-get_new_active_ppa
	.align	2
	.global	FtlGcBufInit
	.type	FtlGcBufInit, %function
FtlGcBufInit:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	ldr	r3, .L310
	mov	r1, #0
	stmfd	sp!, {r4, r5, r6, r7, r8, lr}
	.save {r4, r5, r6, r7, r8, lr}
	mov	r7, #12
	str	r1, [r3, #3152]
	mov	r6, #1
	mov	r5, #20
	b	.L303
.L304:
	mul	r2, r7, r1
	ldr	ip, [r3, #3156]
	add	r0, ip, r2
	str	r6, [r0, #8]
	ldrh	r0, [r3, #82]
	mul	r0, r0, r1
	add	r4, r0, #3
	cmp	r0, #0
	movlt	r0, r4
	ldr	r4, [r3, #3160]
	bic	r0, r0, #3
	add	r0, r4, r0
	str	r0, [ip, r2]
	ldrh	r0, [r3, #84]
	ldr	r4, [r3, #3156]
	mul	r0, r0, r1
	add	ip, r4, r2
	add	r8, r0, #3
	cmp	r0, #0
	movlt	r0, r8
	ldr	r8, [r3, #3164]
	bic	r0, r0, #3
	add	r0, r8, r0
	str	r0, [ip, #4]
	ldr	r0, [r3, #3168]
	ldr	r2, [r4, r2]
	mla	r0, r5, r1, r0
	add	r1, r1, #1
	uxth	r1, r1
	str	r2, [r0, #8]
	ldr	r2, [ip, #4]
	str	r2, [r0, #12]
.L303:
	ldrh	r2, [r3, #4]
	cmp	r1, r2
	bcc	.L304
	b	.L309
.L306:
	mul	r1, r5, r2
	ldr	ip, [r3, #3156]
	add	r0, ip, r1
	str	r4, [r0, #8]
	ldrh	r0, [r3, #82]
	mul	r0, r0, r2
	add	r6, r0, #3
	cmp	r0, #0
	movlt	r0, r6
	ldr	r6, [r3, #3160]
	bic	r0, r0, #3
	add	r0, r6, r0
	str	r0, [ip, r1]
	ldr	r0, [r3, #3156]
	add	r0, r0, r1
	ldrh	r1, [r3, #84]
	mul	r1, r1, r2
	add	r2, r2, #1
	uxth	r2, r2
	add	ip, r1, #3
	cmp	r1, #0
	movlt	r1, ip
	ldr	ip, [r3, #3164]
	bic	r1, r1, #3
	add	r1, ip, r1
	str	r1, [r0, #4]
	b	.L308
.L309:
	ldr	r3, .L310
	mov	r5, #12
	mov	r4, #0
.L308:
	ldr	r1, [r3, #3172]
	cmp	r2, r1
	bcc	.L306
	ldmfd	sp!, {r4, r5, r6, r7, r8, pc}
.L311:
	.align	2
.L310:
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
	ldr	r3, .L319
	stmfd	sp!, {r4, r5, r6, r7, r8, r9, sl, fp, lr}
	.save {r4, r5, r6, r7, r8, r9, sl, fp, lr}
	mov	r5, #20
	ldr	r6, [r3, #3172]
	mov	fp, #12
	ldr	ip, [r3, #3156]
	mov	r3, #0
	mov	r4, r3
	b	.L313
.L316:
	mul	sl, fp, r2
	add	r8, ip, sl
	ldr	r9, [ip, sl]
	ldr	sl, [r7, #8]
	cmp	r9, sl
	streq	r4, [r8, #8]
	beq	.L315
.L314:
	add	r2, r2, #1
	uxth	r2, r2
.L318:
	cmp	r2, r6
	bcc	.L316
.L315:
	add	r3, r3, #1
	uxth	r3, r3
.L313:
	cmp	r3, r1
	ldmcsfd	sp!, {r4, r5, r6, r7, r8, r9, sl, fp, pc}
	mla	r7, r5, r3, r0
	mov	r2, #0
	b	.L318
.L320:
	.align	2
.L319:
	.word	.LANCHOR0
	.fnend
	.size	FtlGcBufFree, .-FtlGcBufFree
	.align	2
	.global	FtlGcBufAlloc
	.type	FtlGcBufAlloc, %function
FtlGcBufAlloc:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	ldr	r3, .L328
	stmfd	sp!, {r4, r5, r6, r7, r8, sl, lr}
	.save {r4, r5, r6, r7, r8, sl, lr}
	mov	r6, #12
	ldr	r8, [r3, #3172]
	mov	r5, #1
	ldr	r7, [r3, #3156]
	mov	r4, #20
	mov	r3, #0
	b	.L322
.L325:
	mla	ip, r6, r2, r7
	ldr	sl, [ip, #8]
	cmp	sl, #0
	bne	.L323
	mla	r2, r4, r3, r0
	ldr	sl, [ip, #0]
	str	r5, [ip, #8]
	str	sl, [r2, #8]
	ldr	ip, [ip, #4]
	str	ip, [r2, #12]
	b	.L324
.L323:
	add	r2, r2, #1
	uxth	r2, r2
	b	.L326
.L327:
	mov	r2, #0
.L326:
	cmp	r2, r8
	bcc	.L325
.L324:
	add	r3, r3, #1
	uxth	r3, r3
.L322:
	cmp	r3, r1
	bcc	.L327
	ldmfd	sp!, {r4, r5, r6, r7, r8, sl, pc}
.L329:
	.align	2
.L328:
	.word	.LANCHOR0
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
	ldr	r3, .L335
	movw	r2, #3176
	ldrh	r1, [r3, r2]
	ldr	r2, [r3, #3180]
	mov	r3, #0
	b	.L331
.L333:
	ldrh	ip, [r2], #2
	cmp	ip, r0
	beq	.L334
	add	r3, r3, #1
	uxth	r3, r3
.L331:
	cmp	r3, r1
	bne	.L333
	mov	r0, #0
	bx	lr
.L334:
	mov	r0, #1
	bx	lr
.L336:
	.align	2
.L335:
	.word	.LANCHOR0
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
	ldr	r3, .L341
	movw	r2, #3176
	ldrh	ip, [r3, r2]
	ldr	r2, [r3, #3180]
	mov	r3, #0
	mov	r1, r2
	b	.L338
.L340:
	ldrh	r7, [r1], #2
	cmp	r7, r0
	beq	.L339
	add	r3, r3, #1
	uxth	r3, r3
.L338:
	cmp	r3, ip
	bne	.L340
	mov	r3, r3, asl #1
	strh	r0, [r2, r3]	@ movhi
	movw	r3, #3176
	ldr	r2, .L341
	ldrh	r1, [r2, r3]
	add	r1, r1, #1
	strh	r1, [r2, r3]	@ movhi
.L339:
	ldr	r3, .L341
	movw	r2, #3188
	mov	r0, #12
	ldrh	r1, [r3, r2]
	mul	r1, r0, r1
	ldr	r0, [r3, #3184]
	add	ip, r0, r1
	stmib	ip, {r5, r6}
	str	r4, [r0, r1]
	ldrh	r1, [r3, r2]
	add	r1, r1, #1
	strh	r1, [r3, r2]	@ movhi
	ldmfd	sp!, {r3, r4, r5, r6, r7, pc}
.L342:
	.align	2
.L341:
	.word	.LANCHOR0
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
	ldr	r0, .L346
	bl	printk
	ldr	r3, .L346+4
	movw	ip, #3190
	ldrh	r5, [r3, ip]
	cmp	r5, r4
	beq	.L344
	movw	r2, #3192
	ldrh	r0, [r3, r2]
	cmp	r0, r4
	beq	.L344
	movw	r1, #65535
	cmp	r5, r1
	streqh	r4, [r3, ip]	@ movhi
	beq	.L344
	cmp	r0, r1
	streqh	r4, [r3, r2]	@ movhi
.L344:
	mov	r0, #0
	ldmfd	sp!, {r3, r4, r5, pc}
.L347:
	.align	2
.L346:
	.word	.LC74
	.word	.LANCHOR0
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
	ldr	r5, .L353
	movw	r7, #3194
	mov	r2, r4
	ldrh	r1, [r5, r7]
	mov	r6, r0
	ldr	r0, .L353+4
	bl	printk
	mov	r0, r6
	bl	FtlGcRefreshBlock
	ldr	r3, [r5, #2436]
	cmp	r3, #0
	beq	.L349
	ldr	r3, [r5, #2244]
	mov	r6, r6, asl #1
	ldrh	r2, [r3, r6]
	cmp	r2, #29
	subhi	r2, r2, #30
	strhih	r2, [r3, r6]	@ movhi
.L349:
	ldrh	r1, [r5, r7]
	mov	r3, #0
	ldr	r2, .L353+8
	b	.L350
.L352:
	ldrh	r0, [r2, #2]!
	cmp	r0, r4
	beq	.L351
	add	r3, r3, #1
	uxth	r3, r3
.L350:
	cmp	r3, r1
	bne	.L352
	cmp	r3, #15
	bhi	.L351
	ldr	r2, .L353
	movw	r1, #3196
	add	r0, r2, r3, asl #1
	add	r3, r3, #1
	strh	r4, [r0, r1]	@ movhi
	movw	r1, #3194
	strh	r3, [r2, r1]	@ movhi
.L351:
	mov	r0, #0
	ldmfd	sp!, {r3, r4, r5, r6, r7, pc}
.L354:
	.align	2
.L353:
	.word	.LANCHOR0
	.word	.LC75
	.word	.LANCHOR0+3194
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
	movw	r2, #3194
	ldr	r3, .L358
	ldrh	r2, [r3, r2]
	cmp	r2, #0
	beq	.L356
	movw	r1, #3190
	ldrh	r0, [r3, r1]
	movw	r1, #65535
	cmp	r0, r1
	bne	.L356
	movw	r4, #3230
	ldr	r5, .L358
	ldrh	r1, [r3, r4]
	cmp	r1, r2
	movcs	r2, #0
	strcsh	r2, [r3, r4]	@ movhi
	ldrh	r2, [r5, r4]
	movw	r3, #3196
	add	r2, r5, r2, asl #1
	ldrh	r0, [r2, r3]
	bl	P2V_block_in_plane
	bl	FtlGcRefreshBlock
	ldrh	r3, [r5, r4]
	add	r3, r3, #1
	strh	r3, [r5, r4]	@ movhi
.L356:
	mov	r0, #0
	ldmfd	sp!, {r3, r4, r5, pc}
.L359:
	.align	2
.L358:
	.word	.LANCHOR0
	.fnend
	.size	FtlGcReFreshBadBlk, .-FtlGcReFreshBadBlk
	.align	2
	.global	FlashReadPages
	.type	FlashReadPages, %function
FlashReadPages:
	.fnstart
	@ args = 0, pretend = 0, frame = 8
	@ frame_needed = 0, uses_anonymous_args = 0
	stmfd	sp!, {r0, r1, r2, r4, r5, r6, r7, r8, r9, sl, fp, lr}
	.save {r0, r1, r2, r4, r5, r6, r7, r8, r9, sl, fp, lr}
	mov	r9, r1
	ldr	sl, .L367
	mov	r4, r0
	mov	r5, #0
	ldr	r6, .L367+4
	ldrh	r3, [sl, #12]
	str	r3, [sp, #4]
	b	.L361
.L366:
	ldr	r3, [r4, #8]
	cmp	r3, #0
	beq	.L362
	ldr	r3, [r4, #12]
	cmp	r3, #0
	bne	.L363
.L362:
	ldr	r0, .L367+8
	mov	r2, #45
	ldr	r1, .L367+12
	bl	printk
.L363:
	ldrh	r7, [sl, #8]
	ldr	fp, [r4, #4]
	mov	r7, r7, lsr #1
	ubfx	r8, fp, #10, #16
	mov	fp, fp, asl #22
	mov	r1, r7
	mov	r0, r8
	bl	__aeabi_uidiv
	ldrh	r3, [sl, #10]
	mov	fp, fp, lsr #22
	ldr	ip, [r6, #12]
	ldr	r2, [r4, #8]
	mov	r3, r3, asl #1
	uxth	r0, r0
	mls	r8, r7, r0, r8
	uxtb	r7, r0
	mov	r0, r7
	mla	r8, r8, r3, fp
	ldr	r3, [r4, #12]
	mov	r1, r8
	blx	ip
	ldr	r2, [r4, #8]
	ldr	r3, [r4, #12]
	ldr	lr, [sp, #4]
	add	r2, r2, #2048
	add	r3, r3, #8
	add	r1, r8, lr
	str	r0, [r4, #0]
	mov	r0, r7
	ldr	ip, [r6, #12]
	blx	ip
	cmn	r0, #1
	beq	.L364
	ldr	r3, [r4, #12]
	ldr	r2, [r3, #12]
	cmn	r2, #1
	bne	.L365
	ldr	r2, [r3, #8]
	cmn	r2, #1
	bne	.L365
	ldr	r3, [r3, #0]
	cmn	r3, #1
	beq	.L365
.L364:
	mvn	r3, #0
	str	r3, [r4, #0]
.L365:
	add	r5, r5, #1
	add	r4, r4, #20
.L361:
	cmp	r5, r9
	bne	.L366
	mov	r0, #0
	ldmfd	sp!, {r1, r2, r3, r4, r5, r6, r7, r8, r9, sl, fp, pc}
.L368:
	.align	2
.L367:
	.word	g_nand_phy_info
	.word	g_nand_ops
	.word	.LC1
	.word	.LANCHOR1+156
	.fnend
	.size	FlashReadPages, .-FlashReadPages
	.align	2
	.global	FtlGetLastWrittenPage
	.type	FtlGetLastWrittenPage, %function
FtlGetLastWrittenPage:
	.fnstart
	@ args = 0, pretend = 0, frame = 88
	@ frame_needed = 0, uses_anonymous_args = 0
	ldr	r3, .L380
	cmp	r1, #1
	stmfd	sp!, {r4, r5, r6, r7, r8, sl, lr}
	.save {r4, r5, r6, r7, r8, sl, lr}
	.pad #92
	sub	sp, sp, #92
	ldreqh	r4, [r3, #74]
	add	r7, sp, #4
	ldrneh	r4, [r3, #72]
	mov	r5, r1
	ldr	r3, .L380+4
	mov	sl, r0, asl #10
	sub	r4, r4, #1
	mov	r0, r7
	mov	r1, #1
	mov	r2, r5
	uxth	r4, r4
	str	r3, [sp, #12]
	add	r3, sp, #24
	str	r3, [sp, #16]
	sxth	r3, r4
	orr	r3, r3, sl
	str	r3, [sp, #8]
	bl	FlashReadPages
	ldr	r3, [sp, #24]
	cmn	r3, #1
	bne	.L373
	b	.L379
.L376:
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
	ldr	r3, [sp, #24]
	cmn	r3, #1
	bne	.L374
	ldr	r3, [sp, #28]
	cmn	r3, #1
	subeq	r4, r6, #1
	uxtheq	r4, r4
	beq	.L378
.L374:
	add	r6, r6, #1
	uxth	r8, r6
	b	.L378
.L379:
	mov	r8, #0
.L378:
	sxth	r6, r8
	sxth	r3, r4
	cmp	r6, r3
	ble	.L376
.L373:
	sxth	r0, r4
	add	sp, sp, #92
	ldmfd	sp!, {r4, r5, r6, r7, r8, sl, pc}
.L381:
	.align	2
.L380:
	.word	.LANCHOR0
	.word	.LANCHOR0+3232
	.fnend
	.size	FtlGetLastWrittenPage, .-FtlGetLastWrittenPage
	.align	2
	.global	FtlLoadFactoryBbt
	.type	FtlLoadFactoryBbt, %function
FtlLoadFactoryBbt:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	stmfd	sp!, {r4, r5, r6, r7, r8, r9, sl, lr}
	.save {r4, r5, r6, r7, r8, r9, sl, lr}
	mov	r5, #0
	ldr	r8, .L389
	ldr	r6, .L389+4
	ldr	sl, [r8, #-836]
	add	r7, r6, #136
	ldr	r3, [r6, #2240]
	str	sl, [r8, #-844]
	str	r3, [r8, #-848]
	b	.L383
.L388:
	ldrh	r4, [r6, #68]
	mvn	r3, #0
	movw	r9, #61664
	strh	r3, [r7], #2	@ movhi
	add	r4, r4, r3
	uxth	r4, r4
	b	.L384
.L387:
	mla	r3, r3, r5, r4
	mov	r1, #1
	ldr	r0, .L389+8
	mov	r2, r1
	mov	r3, r3, asl #10
	str	r3, [r8, #-852]
	bl	FlashReadPages
	ldr	r3, [r8, #-856]
	cmn	r3, #1
	beq	.L385
	ldrh	r3, [sl, #0]
	cmp	r3, r9
	streqh	r4, [r7, #-2]	@ movhi
	beq	.L386
.L385:
	sub	r4, r4, #1
	uxth	r4, r4
.L384:
	ldrh	r3, [r6, #68]
	sub	r2, r3, #16
	cmp	r4, r2
	bgt	.L387
.L386:
	add	r5, r5, #1
.L383:
	ldrh	r3, [r6, #26]
	cmp	r5, r3
	bcc	.L388
	mov	r0, #0
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, sl, pc}
.L390:
	.align	2
.L389:
	.word	.LANCHOR3
	.word	.LANCHOR0
	.word	.LANCHOR3-856
	.fnend
	.size	FtlLoadFactoryBbt, .-FtlLoadFactoryBbt
	.align	2
	.global	FlashProgPages
	.type	FlashProgPages, %function
FlashProgPages:
	.fnstart
	@ args = 0, pretend = 0, frame = 40
	@ frame_needed = 0, uses_anonymous_args = 0
	stmfd	sp!, {r4, r5, r6, r7, r8, r9, sl, fp, lr}
	.save {r4, r5, r6, r7, r8, r9, sl, fp, lr}
	.pad #44
	sub	sp, sp, #44
	ldr	r9, .L404
	mov	r4, r0
	str	r1, [sp, #0]
	mov	r5, r0
	str	r2, [sp, #8]
	mov	r6, #0
	ldrh	r1, [r9, #12]
	str	r3, [sp, #12]
	ldr	r7, .L404+4
	str	r1, [sp, #4]
	b	.L392
.L397:
	ldr	r3, [r5, #8]
	cmp	r3, #0
	beq	.L393
	ldr	r3, [r5, #12]
	cmp	r3, #0
	bne	.L394
.L393:
	ldr	r0, .L404+8
	mov	r2, #73
	ldr	r1, .L404+12
	bl	printk
.L394:
	ldrh	r8, [r9, #8]
	add	r6, r6, #1
	ldr	fp, [r5, #4]
	mov	r8, r8, lsr #1
	ubfx	sl, fp, #10, #16
	mov	fp, fp, asl #22
	mov	r1, r8
	mov	r0, sl
	bl	__aeabi_uidiv
	ldrh	r3, [r9, #10]
	mov	fp, fp, lsr #22
	ldr	r2, [r5, #8]
	ldr	ip, [r7, #8]
	mov	r3, r3, asl #1
	uxth	r0, r0
	mls	sl, r8, r0, sl
	uxtb	r8, r0
	mov	r0, r8
	mla	sl, sl, r3, fp
	ldr	r3, [r5, #12]
	mov	r1, sl
	blx	ip
	ldr	lr, .L404+4
	ldr	r2, [r5, #8]
	add	r2, r2, #2048
	cmp	r0, #0
	str	r0, [r5, #0]
	mov	r0, r8
	mvnne	r3, #0
	strne	r3, [r5, #0]
	ldr	r3, [r5, #12]
	ldr	ip, [lr, #8]
	ldr	lr, [sp, #4]
	add	r3, r3, #8
	add	r1, sl, lr
	blx	ip
	cmp	r0, #0
	mvnne	r3, #0
	strne	r3, [r5, #0]
	add	r5, r5, #20
.L392:
	ldr	r1, [sp, #0]
	cmp	r6, r1
	bne	.L397
	ldr	r3, [sp, #12]
	cmp	r3, #0
	beq	.L398
	ldr	r5, .L404+16
	mov	r6, #0
	sub	r7, r5, #832
	b	.L399
.L403:
	mov	r3, #0
	str	r3, [r5, #-832]
	str	r3, [r5, #3264]
	add	r0, sp, #20
	ldr	r3, [r4, #4]
	mov	r1, #1
	ldr	r2, [sp, #8]
	str	r7, [sp, #28]
	str	r3, [sp, #24]
	ldr	r3, .L404+20
	str	r3, [sp, #32]
	bl	FlashReadPages
	ldr	r8, [sp, #20]
	cmn	r8, #1
	bne	.L400
	ldr	r0, .L404+24
	ldr	r1, [r4, #4]
	bl	printk
	str	r8, [r4, #0]
.L400:
	ldr	r3, [r4, #12]
	cmp	r3, #0
	beq	.L401
	ldr	r2, [r3, #0]
	ldr	r3, [r5, #3264]
	cmp	r2, r3
	beq	.L401
	ldr	r0, .L404+28
	ldr	r1, [r4, #4]
	bl	printk
	mvn	r3, #0
	str	r3, [r4, #0]
.L401:
	ldr	r3, [r4, #8]
	cmp	r3, #0
	beq	.L402
	ldr	r2, [r3, #0]
	ldr	r3, [r5, #-832]
	cmp	r2, r3
	beq	.L402
	ldr	r0, .L404+32
	ldr	r1, [r4, #4]
	bl	printk
	mvn	r3, #0
	str	r3, [r4, #0]
.L402:
	add	r6, r6, #1
	add	r4, r4, #20
.L399:
	ldr	r1, [sp, #0]
	cmp	r6, r1
	bne	.L403
.L398:
	mov	r0, #0
	add	sp, sp, #44
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, sl, fp, pc}
.L405:
	.align	2
.L404:
	.word	g_nand_phy_info
	.word	g_nand_ops
	.word	.LC1
	.word	.LANCHOR1+171
	.word	.LANCHOR3
	.word	.LANCHOR3+3264
	.word	.LC76
	.word	.LC77
	.word	.LC78
	.fnend
	.size	FlashProgPages, .-FlashProgPages
	.align	2
	.global	FlashEraseBlocks
	.type	FlashEraseBlocks, %function
FlashEraseBlocks:
	.fnstart
	@ args = 0, pretend = 0, frame = 8
	@ frame_needed = 0, uses_anonymous_args = 0
	stmfd	sp!, {r0, r1, r2, r4, r5, r6, r7, r8, r9, sl, fp, lr}
	.save {r0, r1, r2, r4, r5, r6, r7, r8, r9, sl, fp, lr}
	mov	r4, r0
	ldr	r7, .L411
	mov	r5, #0
	str	r2, [sp, #4]
	ldr	r6, .L411+4
	ldrh	r2, [r7, #12]
	mov	fp, r6
	str	r2, [sp, #0]
	b	.L407
.L410:
	ldrh	sl, [r7, #8]
	add	r5, r5, #1
	ldr	r9, [r4, #4]
	mov	sl, sl, lsr #1
	ubfx	r8, r9, #10, #16
	mov	r9, r9, asl #22
	mov	r1, sl
	mov	r0, r8
	bl	__aeabi_uidiv
	ldrh	r3, [r7, #10]
	mov	r9, r9, lsr #22
	mov	r3, r3, asl #1
	uxth	r0, r0
	mls	r8, sl, r0, r8
	uxtb	sl, r0
	mov	r0, sl
	mla	r8, r8, r3, r9
	ldr	r3, [r6, #4]
	mov	r1, r8
	blx	r3
	ldr	r2, [sp, #0]
	add	r1, r8, r2
	cmp	r0, #0
	str	r0, [r4, #0]
	mov	r0, sl
	mvnne	r3, #0
	strne	r3, [r4, #0]
	ldr	r3, [fp, #4]
	blx	r3
	cmp	r0, #0
	mvnne	r3, #0
	strne	r3, [r4, #0]
	add	r4, r4, #20
.L407:
	ldr	r3, [sp, #4]
	cmp	r5, r3
	bne	.L410
	mov	r0, #0
	ldmfd	sp!, {r1, r2, r3, r4, r5, r6, r7, r8, r9, sl, fp, pc}
.L412:
	.align	2
.L411:
	.word	g_nand_phy_info
	.word	g_nand_ops
	.fnend
	.size	FlashEraseBlocks, .-FlashEraseBlocks
	.align	2
	.global	FtlFreeSysBlkQueueIn
	.type	FtlFreeSysBlkQueueIn, %function
FtlFreeSysBlkQueueIn:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	stmfd	sp!, {r4, r5, r6, lr}
	.save {r4, r5, r6, lr}
	mov	r5, r0
	ldr	r4, .L416
	ldrh	r3, [r4, #190]
	cmp	r3, #1024
	ldmeqfd	sp!, {r4, r5, r6, pc}
	cmp	r1, #0
	beq	.L415
	bl	P2V_block_in_plane
	ldr	r3, .L416+4
	mov	r1, #1
	mov	r2, r1
	mov	r6, r0
	ldr	r0, [r3, #3776]
	mov	r3, r5, asl #10
	mov	r6, r6, asl #1
	str	r3, [r0, #4]
	bl	FlashEraseBlocks
	ldr	r3, [r4, #2244]
	ldrh	r2, [r3, r6]
	add	r2, r2, #1
	strh	r2, [r3, r6]	@ movhi
	ldr	r3, [r4, #2496]
	add	r3, r3, #1
	str	r3, [r4, #2496]
.L415:
	ldr	r3, .L416
	ldrh	r2, [r3, #190]
	add	r2, r2, #1
	strh	r2, [r3, #190]	@ movhi
	ldrh	r2, [r3, #188]
	add	r1, r3, r2, asl #1
	add	r2, r2, #1
	bic	r2, r2, #64512
	strh	r5, [r1, #192]	@ movhi
	strh	r2, [r3, #188]	@ movhi
	ldmfd	sp!, {r4, r5, r6, pc}
.L417:
	.align	2
.L416:
	.word	.LANCHOR0
	.word	.LANCHOR3
	.fnend
	.size	FtlFreeSysBlkQueueIn, .-FtlFreeSysBlkQueueIn
	.align	2
	.global	FtlLowFormatEraseBlock
	.type	FtlLowFormatEraseBlock, %function
FtlLowFormatEraseBlock:
	.fnstart
	@ args = 0, pretend = 0, frame = 16
	@ frame_needed = 0, uses_anonymous_args = 0
	stmfd	sp!, {r4, r5, r6, r7, r8, r9, sl, fp, lr}
	.save {r4, r5, r6, r7, r8, r9, sl, fp, lr}
	mov	r7, #0
	ldr	r8, .L446
	.pad #20
	sub	sp, sp, #20
	mov	sl, r0
	mov	r6, r1
	mov	r4, r7
	mov	r5, r7
	str	r0, [r8, #3780]
	mov	fp, #20
	ldr	r9, .L446+4
	b	.L419
.L423:
	mul	r3, fp, r7
	ldr	r2, [r8, #3776]
	mov	r1, #0
	str	r1, [r2, r3]
	add	r3, r9, r7
	mov	r1, sl
	ldrb	r0, [r3, #30]	@ zero_extendqisi2
	bl	V2P_block
	cmp	r6, #0
	str	r0, [sp, #4]
	beq	.L420
	bl	IsBlkInVendorPart
	cmp	r0, #0
	bne	.L421
.L420:
	ldr	r0, [sp, #4]
	bl	FtlBbmIsBadBlock
	cmp	r0, #0
	addne	r5, r5, #1
	uxthne	r5, r5
	bne	.L421
	ldr	r3, [r8, #3776]
	ldr	r1, [sp, #4]
	mla	r3, fp, r4, r3
	mov	r2, r1, asl #10
	str	r2, [r3, #4]
	ldr	r2, [r8, #3784]
	str	r2, [r3, #8]
	ldrh	r2, [r9, #84]
	mul	r2, r2, r4
	add	r4, r4, #1
	uxth	r4, r4
	add	r1, r2, #3
	cmp	r2, #0
	movlt	r2, r1
	ldr	r1, [r8, #3788]
	bic	r2, r2, #3
	add	r2, r1, r2
	str	r2, [r3, #12]
.L421:
	add	r7, r7, #1
	uxth	r7, r7
.L419:
	ldrh	r3, [r9, #4]
	cmp	r3, r7
	bhi	.L423
	cmp	r4, #0
	beq	.L425
	ldr	r7, .L446
	mov	r1, #0
	mov	r2, r4
	mov	r8, #0
	mov	r9, r8
	ldr	r0, [r7, #3776]
	bl	FlashEraseBlocks
.L427:
	ldr	r3, [r7, #3776]
	add	r2, r3, r8
	ldr	r3, [r3, r8]
	cmn	r3, #1
	bne	.L426
	ldr	r0, [r2, #4]
	add	r5, r5, #1
	ubfx	r0, r0, #10, #16
	uxth	r5, r5
	bl	FtlBbmMapBadBlock
.L426:
	add	r9, r9, #1
	add	r8, r8, #20
	uxth	r9, r9
	cmp	r9, r4
	bne	.L427
.L428:
	cmp	r6, #0
	mov	r8, #0
	mov	r7, r6
	streq	r6, [sp, #4]
	ldrne	r3, .L446+4
	moveq	r9, #1
	moveq	r1, #6
	streq	r1, [sp, #8]
	ldrneh	r9, [r3, #74]
	movne	r3, #1
	strne	r3, [sp, #4]
	movne	r2, r9, lsr #2
	strne	r2, [sp, #8]
.L436:
	mov	r6, #0
	ldr	fp, .L446
	mov	r4, r6
	b	.L429
.L432:
	mov	r1, #20
	ldr	r2, [fp, #3776]
	mul	r3, r1, r6
	mov	r1, #0
	str	r1, [r2, r3]
	mov	r1, sl
	ldr	r2, .L446+4
	add	r3, r2, r6
	ldrb	r0, [r3, #30]	@ zero_extendqisi2
	bl	V2P_block
	cmp	r7, #0
	str	r0, [sp, #12]
	beq	.L430
	bl	IsBlkInVendorPart
	cmp	r0, #0
	bne	.L431
.L430:
	ldr	r0, [sp, #12]
	bl	FtlBbmIsBadBlock
	cmp	r0, #0
	bne	.L431
	ldr	r3, [fp, #3776]
	mov	r1, #20
	mla	r3, r1, r4, r3
	ldr	r1, [sp, #12]
	add	r2, r8, r1, asl #10
	ldr	r1, .L446+4
	str	r2, [r3, #4]
	ldr	r2, [fp, #3792]
	str	r2, [r3, #8]
	ldrh	r2, [r1, #84]
	mul	r2, r2, r4
	add	r4, r4, #1
	uxth	r4, r4
	add	r1, r2, #3
	cmp	r2, #0
	movlt	r2, r1
	ldr	r1, [fp, #3784]
	bic	r2, r2, #3
	add	r2, r1, r2
	str	r2, [r3, #12]
.L431:
	add	r6, r6, #1
	uxth	r6, r6
.L429:
	ldr	r2, .L446+4
	ldrh	r3, [r2, #4]
	cmp	r3, r6
	bhi	.L432
	cmp	r4, #0
	beq	.L425
	ldr	r6, .L446
	mov	r1, r4
	ldr	r2, [sp, #4]
	mov	r3, #1
	mov	fp, #0
	ldr	r0, [r6, #3776]
	bl	FlashProgPages
	mov	ip, r7
	mov	r7, r4
	mov	r4, fp
.L435:
	ldr	r2, [r6, #3776]
	add	r1, r2, fp
	ldr	r2, [r2, fp]
	cmp	r2, #0
	beq	.L434
	ldr	r0, [r1, #4]
	add	r5, r5, #1
	str	ip, [sp, #0]
	ubfx	r0, r0, #10, #16
	uxth	r5, r5
	bl	FtlBbmMapBadBlock
	ldr	ip, [sp, #0]
.L434:
	add	r4, r4, #1
	add	fp, fp, #20
	uxth	r4, r4
	cmp	r4, r7
	bne	.L435
	ldr	r3, [sp, #8]
	mov	r4, r7
	mov	r7, ip
	add	r8, r8, r3
	uxth	r8, r8
	cmp	r8, r9
	bcc	.L436
	ldr	r9, .L446
	mov	r7, #0
	mov	r6, ip
	mov	r8, r7
.L438:
	cmp	r6, #0
	beq	.L437
	ldr	r3, [r9, #3776]
	add	r2, r3, r7
	ldr	r3, [r3, r7]
	cmp	r3, #0
	bne	.L437
	ldr	r0, [r2, #4]
	mov	r1, #1
	ubfx	r0, r0, #10, #16
	bl	FtlFreeSysBlkQueueIn
.L437:
	add	r8, r8, #1
	add	r7, r7, #20
	uxth	r8, r8
	cmp	r8, r4
	bne	.L438
	cmp	sl, #63
	movhi	sl, #0
	movls	sl, #1
	cmp	r6, #0
	moveq	r6, sl
	orrne	r6, sl, #1
	cmp	r6, #0
	beq	.L425
	ldr	r3, .L446
	mov	r2, r8
	ldr	r1, [sp, #4]
	ldr	r0, [r3, #3776]
	bl	FlashEraseBlocks
.L425:
	mov	r0, r5
	add	sp, sp, #20
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, sl, fp, pc}
.L447:
	.align	2
.L446:
	.word	.LANCHOR3
	.word	.LANCHOR0
	.fnend
	.size	FtlLowFormatEraseBlock, .-FtlLowFormatEraseBlock
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
	beq	.L449
	bl	memset
.L449:
	mov	r0, r4
	ldmfd	sp!, {r4, pc}
	.fnend
	.size	ftl_memset, .-ftl_memset
	.align	2
	.global	FlashGetBadBlockList
	.type	FlashGetBadBlockList, %function
FlashGetBadBlockList:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	stmfd	sp!, {r3, r4, r5, lr}
	.save {r3, r4, r5, lr}
	mov	r2, #256
	mov	r5, r1
	mov	r1, #255
	mov	r4, r0
	bl	ftl_memset
	ldr	r3, .L454
	mov	r0, r4
	mov	r1, r5
	ldr	r3, [r3, #0]
	blx	r3
	uxth	r3, r0
	cmp	r3, #50
	bls	.L451
	mov	r0, r4
	mov	r1, #255
	mov	r2, #256
	bl	ftl_memset
	mov	r3, #0
.L451:
	mov	r0, #0
	b	.L452
.L453:
	add	r0, r0, #1
	ldrh	r2, [r4, #0]
	uxth	r0, r0
	mov	r2, r2, lsr #1
	strh	r2, [r4], #2	@ movhi
.L452:
	cmp	r0, r3
	bne	.L453
	ldmfd	sp!, {r3, r4, r5, pc}
.L455:
	.align	2
.L454:
	.word	g_nand_ops
	.fnend
	.size	FlashGetBadBlockList, .-FlashGetBadBlockList
	.align	2
	.global	FlashTestBlk
	.type	FlashTestBlk, %function
FlashTestBlk:
	.fnstart
	@ args = 0, pretend = 0, frame = 88
	@ frame_needed = 0, uses_anonymous_args = 0
	cmp	r0, #11
	stmfd	sp!, {r4, r5, lr}
	.save {r4, r5, lr}
	mov	r4, r0
	.pad #92
	sub	sp, sp, #92
	movls	r4, #0
	bls	.L457
.L458:
	ldr	r5, .L461
	add	r0, sp, #24
	mov	r1, #165
	mov	r2, #32
	str	r0, [sp, #16]
	mov	r4, r4, asl #10
	str	r5, [sp, #12]
	bl	ftl_memset
	mov	r1, #90
	mov	r2, #8
	mov	r0, r5
	bl	ftl_memset
	mov	r1, #1
	mov	r2, r1
	add	r0, sp, #4
	str	r4, [sp, #8]
	bl	FlashEraseBlocks
	mov	r1, #1
	add	r0, sp, #4
	mov	r2, r1
	mov	r3, r1
	bl	FlashProgPages
	ldr	r4, [sp, #4]
	add	r0, sp, #4
	mov	r1, #0
	cmp	r4, #0
	mov	r2, #1
	mvnne	r4, #0
	bl	FlashEraseBlocks
.L457:
	mov	r0, r4
	add	sp, sp, #92
	ldmfd	sp!, {r4, r5, pc}
.L462:
	.align	2
.L461:
	.word	.LANCHOR0+3232
	.fnend
	.size	FlashTestBlk, .-FlashTestBlk
	.align	2
	.global	FtlGcPageVarInit
	.type	FtlGcPageVarInit, %function
FtlGcPageVarInit:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	stmfd	sp!, {r4, lr}
	.save {r4, lr}
	movw	r2, #3176
	ldr	r4, .L464
	mov	r3, #0
	mov	r1, #255
	strh	r3, [r4, r2]	@ movhi
	add	r2, r2, #12
	ldr	r0, [r4, #3180]
	strh	r3, [r4, r2]	@ movhi
	ldrh	r2, [r4, #76]
	mov	r2, r2, asl #1
	bl	ftl_memset
	ldrh	r3, [r4, #76]
	mov	r2, #12
	ldr	r0, [r4, #3184]
	mov	r1, #255
	mul	r2, r2, r3
	bl	ftl_memset
	ldmfd	sp!, {r4, lr}
	b	FtlGcBufInit
.L465:
	.align	2
.L464:
	.word	.LANCHOR0
	.fnend
	.size	FtlGcPageVarInit, .-FtlGcPageVarInit
	.align	2
	.global	FtlGcScanTempBlk
	.type	FtlGcScanTempBlk, %function
FtlGcScanTempBlk:
	.fnstart
	@ args = 0, pretend = 0, frame = 24
	@ frame_needed = 0, uses_anonymous_args = 0
	ldr	r3, .L487
	stmfd	sp!, {r4, r5, r6, r7, r8, r9, sl, fp, lr}
	.save {r4, r5, r6, r7, r8, r9, sl, fp, lr}
	.pad #28
	sub	sp, sp, #28
	ldrh	r4, [r3, #4]
	movw	r3, #65535
	mov	r5, r0
	str	r1, [sp, #12]
	cmp	r4, r3
	beq	.L483
	cmp	r4, #0
	bne	.L467
	b	.L468
.L483:
	mov	r4, #0
.L467:
	ldr	r3, .L487+4
	ldr	fp, [sp, #12]
	ldrh	r3, [r3, #72]
	cmp	fp, r3
	bne	.L469
.L468:
	bl	FtlGcPageVarInit
.L469:
	mov	r7, #0
	movw	r9, #65535
.L478:
	ldrh	r3, [r5, #0]
	mov	r1, #0
	strb	r1, [r5, #8]
	cmp	r3, r9
	beq	.L484
.L471:
	mov	sl, r5
.L485:
	ldr	r3, .L487+4
	ldr	r2, .L487+8
	ldrh	r8, [r3, #4]
	ldr	r6, [r2, #3796]
	ldr	lr, [r3, #3160]
	ldr	r2, [r3, #3164]
	ldrh	ip, [r3, #82]
	ldrh	r3, [r3, #84]
	str	r2, [sp, #16]
	mov	r2, sl
	str	r3, [sp, #20]
	mov	r3, #0
	mov	r5, r3
	b	.L472
.L474:
	ldrh	r0, [r2, #16]
	cmp	r0, r9
	beq	.L473
	mov	fp, #20
	orr	r0, r4, r0, asl #10
	mla	r1, fp, r5, r6
	str	r0, [r1, #4]
	mul	r0, ip, r5
	add	fp, r0, #3
	cmp	r0, #0
	movlt	r0, fp
	ldr	fp, [sp, #20]
	bic	r0, r0, #3
	add	r0, lr, r0
	str	r0, [r1, #8]
	mul	r0, fp, r5
	add	r5, r5, #1
	uxth	r5, r5
	add	fp, r0, #3
	cmp	r0, #0
	movlt	r0, fp
	ldr	fp, [sp, #16]
	bic	r0, r0, #3
	add	r0, fp, r0
	str	r0, [r1, #12]
.L473:
	add	r3, r3, #1
	add	r2, r2, #2
	uxth	r3, r3
.L472:
	cmp	r3, r8
	bne	.L474
	ldr	r8, .L487+8
	mov	r1, r5
	mov	r2, #0
	mov	r6, #0
	mov	fp, r6
	ldr	r0, [r8, #3796]
	bl	FlashReadPages
	b	.L475
.L479:
	ldr	r2, .L487+8
	ldr	r1, [r2, #3796]
	add	r3, r1, r6
	ldr	r8, [r3, #4]
	stmib	sp, {r1, r3}
	ubfx	r0, r8, #10, #16
	bl	P2V_plane
	ldr	r1, [sp, #4]
	ldr	r3, [sp, #8]
	ldr	r1, [r1, r6]
	cmp	r1, #0
	mov	r2, r0
	bne	.L476
	ldr	r3, [r3, #12]
	add	r6, r6, #20
	ldrh	r1, [r3, #0]
	cmp	r1, r9
	ldreq	r3, .L487+8
	moveq	r1, #1
	moveq	r5, sl
	streq	r1, [r3, #3800]
	beq	.L470
.L477:
	add	fp, fp, #1
	ldr	r0, [r3, #12]
	mov	r1, r8
	ldr	r2, [r3, #8]
	bl	FtlGcUpdatePage
	uxth	fp, fp
	b	.L475
.L476:
	ldr	r2, .L487+4
	mov	fp, #0	@ movhi
	ldrh	r3, [sl, #0]
	mov	r5, sl
	mov	r4, #0
	ldr	r2, [r2, #2256]
	mov	r3, r3, asl #1
	strh	fp, [r2, r3]	@ movhi
	ldrh	r0, [sl, #0]
	bl	INSERT_FREE_LIST
	mvn	r3, #0
	strh	r3, [sl, #0]	@ movhi
	bl	FtlGcPageVarInit
	b	.L478
.L475:
	cmp	fp, r5
	bne	.L479
	ldr	r3, [sp, #12]
	add	r7, r7, #1
	add	r4, r4, #1
	cmp	r7, r3
	ldr	r3, .L487+4
	uxth	r4, r4
	bcc	.L480
	ldr	r2, .L487
	ldrh	r1, [r2, #4]
	cmp	r1, r9
	beq	.L480
	add	r1, r1, r7
	strh	r1, [r2, #4]	@ movhi
	ldrh	r2, [r3, #72]
	cmp	r2, r4
	bhi	.L481
.L480:
	ldrh	r3, [r3, #72]
	cmp	r3, r4
	bhi	.L485
	mov	r5, sl
.L484:
	mov	r2, #0
.L470:
	ldr	r3, .L487
	mvn	r1, #0
	strh	r4, [r5, #2]	@ movhi
	mov	r0, r5
	strb	r2, [r5, #6]
	strh	r1, [r3, #4]	@ movhi
	mov	r1, r4
	bl	ftl_sb_update_avl_pages
.L481:
	mvn	r0, #0
	add	sp, sp, #28
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, sl, fp, pc}
.L488:
	.align	2
.L487:
	.word	.LANCHOR2
	.word	.LANCHOR0
	.word	.LANCHOR3
	.fnend
	.size	FtlGcScanTempBlk, .-FtlGcScanTempBlk
	.align	2
	.global	SupperBlkListInit
	.type	SupperBlkListInit, %function
SupperBlkListInit:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	stmfd	sp!, {r4, r5, r6, r7, r8, r9, sl, lr}
	.save {r4, r5, r6, r7, r8, r9, sl, lr}
	mov	r2, #6
	ldr	r4, .L499
	mov	r1, #0
	mov	r7, #0
	mov	r8, r7
	mov	r5, r7
	ldrh	r3, [r4, #14]
	mov	r9, r4
	ldr	r0, [r4, #2248]
	mul	r2, r2, r3
	bl	ftl_memset
	movw	r3, #2264
	strh	r7, [r4, r3]	@ movhi
	mov	r3, #2272
	str	r7, [r4, #2268]
	str	r7, [r4, #2252]
	str	r7, [r4, #2260]
	strh	r7, [r4, r3]	@ movhi
	b	.L490
.L492:
	add	r3, r4, sl
	mov	r1, r5
	add	sl, sl, #1
	ldrb	r0, [r3, #30]	@ zero_extendqisi2
	bl	V2P_block
	uxth	sl, sl
	bl	FtlBbmIsBadBlock
	cmp	r0, #0
	ldreqh	r3, [r4, #72]
	addeq	r6, r6, r3
	uxtheq	r6, r6
	b	.L496
.L498:
	mov	r6, #0
	mov	sl, r6
.L496:
	ldrh	r3, [r4, #4]
	cmp	r3, sl
	bhi	.L492
	cmp	r6, #0
	beq	.L493
	mov	r1, r6
	mov	r0, #32768
	bl	__aeabi_idiv
	uxth	r6, r0
.L493:
	ldr	r3, [r9, #2248]
	mov	r2, #6
	mla	r3, r2, r5, r3
	strh	r6, [r3, #4]	@ movhi
	movw	r3, #2276
	ldrh	r3, [r9, r3]
	cmp	r3, r5
	beq	.L494
	movw	r3, #2324
	ldrh	r3, [r4, r3]
	cmp	r3, r5
	beq	.L494
	movw	r3, #2372
	ldrh	r3, [r4, r3]
	cmp	r3, r5
	beq	.L494
	ldr	r2, [r4, #2256]
	mov	r3, r5, asl #1
	ldrh	r3, [r2, r3]
	cmp	r3, #0
	bne	.L495
	add	r7, r7, #1
	mov	r0, r5
	uxth	r7, r7
	bl	INSERT_FREE_LIST
	b	.L494
.L495:
	add	r8, r8, #1
	mov	r0, r5
	uxth	r8, r8
	bl	INSERT_DATA_LIST
.L494:
	add	r5, r5, #1
	uxth	r5, r5
.L490:
	ldrh	r3, [r4, #12]
	ldr	r2, .L499
	cmp	r5, r3
	bcc	.L498
	movw	r1, #2264
	strh	r8, [r2, r1]	@ movhi
	mov	r1, #2272
	strh	r7, [r2, r1]	@ movhi
	add	r7, r8, r7
	cmp	r7, r3
	ble	.L497
	ldr	r0, .L499+4
	movw	r2, #2108
	ldr	r1, .L499+8
	bl	printk
.L497:
	mov	r0, #0
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, sl, pc}
.L500:
	.align	2
.L499:
	.word	.LANCHOR0
	.word	.LC1
	.word	.LANCHOR1+186
	.fnend
	.size	SupperBlkListInit, .-SupperBlkListInit
	.align	2
	.global	FtlL2PDataInit
	.type	FtlL2PDataInit, %function
FtlL2PDataInit:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	stmfd	sp!, {r3, r4, r5, r6, r7, lr}
	.save {r3, r4, r5, r6, r7, lr}
	mov	r1, #0
	ldr	r4, .L504
	mov	r6, #12
	ldr	r5, .L504+4
	ldr	r2, [r4, #100]
	ldr	r0, [r5, #3804]
	mov	r2, r2, asl #1
	bl	ftl_memset
	ldrh	r3, [r4, #82]
	ldrh	r2, [r4, #110]
	mov	r1, #255
	ldr	r0, [r5, #3808]
	mul	r2, r2, r3
	bl	ftl_memset
	mov	r3, #0
	mov	ip, r3
	mvn	r0, #0
	b	.L502
.L503:
	mul	r2, r6, r3
	ldr	r1, [r4, #2420]
	add	r7, r1, r2
	str	ip, [r7, #4]
	strh	r0, [r1, r2]	@ movhi
	ldr	r1, [r4, #2420]
	ldr	r7, [r5, #3808]
	add	r2, r1, r2
	ldrh	r1, [r4, #82]
	mul	r1, r3, r1
	add	r3, r3, #1
	uxth	r3, r3
	bic	r1, r1, #3
	add	r1, r7, r1
	str	r1, [r2, #8]
.L502:
	ldrh	r1, [r4, #110]
	ldr	r2, .L504
	cmp	r1, r3
	bhi	.L503
	ldr	r3, .L504+4
	movw	r0, #3814
	mvn	r1, #0
	strh	r1, [r3, r0]	@ movhi
	movw	r0, #3812
	strh	r1, [r3, r0]	@ movhi
	movw	r1, #3822
	ldr	r0, [r2, #100]
	strh	r0, [r3, r1]	@ movhi
	movw	r1, #3816
	ldr	r0, .L504+8
	strh	r0, [r3, r1]	@ movhi
	add	r1, r1, #36
	ldrh	r0, [r3, r1]
	movw	r1, #3820
	strh	r0, [r3, r1]	@ movhi
	ldrh	r1, [r2, #108]
	movw	r2, #3818
	strh	r1, [r3, r2]	@ movhi
	ldr	r2, [r3, #3856]
	str	r2, [r3, #3824]
	ldr	r2, [r3, #3860]
	str	r2, [r3, #3828]
	ldr	r2, [r3, #3804]
	str	r2, [r3, #3832]
	ldr	r2, [r3, #3864]
	str	r2, [r3, #3836]
	ldmfd	sp!, {r3, r4, r5, r6, r7, pc}
.L505:
	.align	2
.L504:
	.word	.LANCHOR0
	.word	.LANCHOR3
	.word	-3902
	.fnend
	.size	FtlL2PDataInit, .-FtlL2PDataInit
	.align	2
	.global	FtlScanSysBlk
	.type	FtlScanSysBlk, %function
FtlScanSysBlk:
	.fnstart
	@ args = 0, pretend = 0, frame = 24
	@ frame_needed = 0, uses_anonymous_args = 0
	stmfd	sp!, {r4, r5, r6, r7, r8, r9, sl, fp, lr}
	.save {r4, r5, r6, r7, r8, r9, sl, fp, lr}
	movw	r3, #3852
	ldr	r5, .L565
	mov	r6, #0
	ldr	r4, .L565+4
	.pad #28
	sub	sp, sp, #28
	mov	r1, r6
	ldr	r2, [r5, #100]
	strh	r6, [r4, r3]	@ movhi
	mov	r9, r4
	strh	r6, [r5, #116]	@ movhi
	mov	r2, r2, asl #2
	ldr	r0, [r4, #3860]
	bl	ftl_memset
	ldr	r2, [r5, #100]
	mov	r1, r6
	ldr	r0, [r4, #3856]
	mov	r2, r2, asl #1
	bl	ftl_memset
	ldrh	r2, [r5, #92]
	mov	r1, r6
	ldr	r0, [r4, #3868]
	mov	r2, r2, asl #2
	bl	ftl_memset
	ldrh	r2, [r5, #92]
	ldr	r0, [r5, #120]
	mov	r1, r6
	mov	r2, r2, asl #1
	bl	ftl_memset
	add	r0, r5, #2560
	mov	r1, #255
	mov	r2, #12
	bl	ftl_memset
	ldrh	r5, [r5, #12]
	str	r5, [sp, #8]
	b	.L507
.L509:
	add	r3, r5, r6
	ldr	r1, [sp, #8]
	ldrb	r0, [r3, #30]	@ zero_extendqisi2
	bl	V2P_block
	mov	r8, r0
	bl	FtlBbmIsBadBlock
	cmp	r0, #0
	bne	.L508
	ldr	r3, [r4, #3796]
	mov	r8, r8, asl #10
	ldr	r2, [r5, #3160]
	mla	r3, r7, sl, r3
	str	r2, [r3, #8]
	ldrh	r2, [r5, #84]
	str	r8, [r3, #4]
	mul	r2, r2, sl
	add	sl, sl, #1
	uxth	sl, sl
	add	r1, r2, #3
	cmp	r2, #0
	movlt	r2, r1
	ldr	r1, [r5, #3164]
	bic	r2, r2, #3
	add	r2, r1, r2
	str	r2, [r3, #12]
.L508:
	add	r6, r6, #1
	uxth	r6, r6
	b	.L543
.L557:
	mov	sl, #0
	mov	r7, #20
	mov	r6, sl
.L543:
	ldrh	r3, [r5, #4]
	cmp	r3, r6
	bhi	.L509
	cmp	sl, #0
	beq	.L510
	ldr	r0, [r4, #3796]
	mov	r1, sl
	mov	r2, #1
	mov	r6, #0
	bl	FlashReadPages
	str	r6, [sp, #12]
	str	sl, [sp, #20]
.L542:
	ldr	r3, [r4, #3796]
	add	r2, r3, r6
	ldr	r3, [r3, r6]
	ldr	r7, [r2, #4]
	cmn	r3, #1
	ldr	r5, [r2, #12]
	ubfx	r7, r7, #10, #16
	bne	.L511
	mov	r8, #16
.L513:
	ldr	r0, [r4, #3796]
	mov	r1, #1
	mov	r2, r1
	add	r0, r0, r6
	ldr	r3, [r0, #4]
	add	r3, r3, #1
	str	r3, [r0, #4]
	bl	FlashReadPages
	ldrh	r3, [r5, #0]
	movw	r0, #65535
	cmp	r3, r0
	ldreq	r3, [r9, #3796]
	mvneq	r2, #0
	streq	r2, [r3, r6]
	beq	.L511
.L512:
	ldr	r3, [r4, #3796]
	ldr	r3, [r3, r6]
	cmn	r3, #1
	bne	.L511
	sub	r8, r8, #1
	uxth	r8, r8
	cmp	r8, #0
	bne	.L513
.L511:
	ldr	r3, [r4, #3796]
	ldr	r3, [r3, r6]
	cmn	r3, #1
	beq	.L514
	ldr	r2, .L565
	ldr	r3, [r5, #4]
	ldr	r1, [r2, #2476]
	cmn	r1, #1
	beq	.L515
	cmp	r1, r3
	bhi	.L516
.L515:
	cmn	r3, #1
	addne	r3, r3, #1
	strne	r3, [r2, #2476]
.L516:
	ldrh	r3, [r5, #0]
	movw	r1, #61604
	cmp	r3, r1
	beq	.L519
	bhi	.L522
	movw	r2, #61574
	cmp	r3, r2
	bne	.L517
	b	.L563
.L522:
	movw	r2, #61634
	cmp	r3, r2
	beq	.L520
	movw	r2, #65535
	cmp	r3, r2
	bne	.L517
	b	.L564
.L520:
	ldr	r8, .L565
	movw	r3, #3852
	ldrh	r2, [r4, r3]
	ldr	r3, [r8, #100]
	cmp	r2, r3
	bls	.L523
	ldr	r0, .L565+8
	movw	r2, #1163
	ldr	r1, .L565+12
	bl	printk
.L523:
	ldr	r0, [r8, #100]
	movw	r2, #3852
	ldrh	r1, [r4, r2]
	mov	sl, r7
	ldr	ip, [r4, #3860]
	mov	r8, r6
	uxth	fp, r0
	sub	r3, fp, #1
	rsb	fp, r1, fp
	sub	fp, fp, #1
	uxth	r3, r3
	sxth	fp, fp
	b	.L524
.L530:
	mov	r6, r2, asl #2
	ldr	r7, [r5, #4]
	str	r6, [sp, #16]
	ldr	r6, [ip, r2, asl #2]
	cmp	r7, r6
	bls	.L525
	ldr	ip, [ip, #0]
	mov	r7, sl
	mov	r6, r8
	cmp	ip, #0
	bne	.L526
	cmp	r1, r0
	addne	r1, r1, #1
	movwne	r0, #3852
	strneh	r1, [r4, r0]	@ movhi
.L526:
	mov	r0, #0
	sxth	sl, r3
	b	.L527
.L528:
	ldr	r8, [r4, #3860]
	add	ip, r1, #1
	add	r0, r0, #1
	ldr	fp, [r8, ip, asl #2]
	mov	ip, ip, asl #1
	uxth	r0, r0
	str	fp, [r8, r1, asl #2]
	mov	r1, r1, asl #1
	ldr	r8, [r4, #3856]
	ldrh	ip, [r8, ip]
	strh	ip, [r8, r1]	@ movhi
.L527:
	sxth	r1, r0
	cmp	r1, sl
	bne	.L528
	ldr	r1, [r9, #3860]
	mov	r2, r2, asl #1
	ldr	r0, [r5, #4]
	ldr	ip, [sp, #16]
	str	r0, [r1, ip]
	ldr	r1, [r9, #3856]
	strh	r7, [r1, r2]	@ movhi
	b	.L529
.L525:
	sub	r3, r3, #1
	uxth	r3, r3
.L524:
	sxth	r2, r3
	cmp	r2, fp
	bgt	.L530
	mov	r7, sl
	mov	r6, r8
.L529:
	sxth	r3, r3
	cmp	r3, #0
	blt	.L517
	ldr	r0, .L565
	movw	r2, #3852
	ldrh	r1, [r4, r2]
	ldr	r0, [r0, #100]
	rsb	r0, r1, r0
	sub	r0, r0, #1
	sxth	r0, r0
	cmp	r3, r0
	bgt	.L517
	add	r1, r1, #1
	strh	r1, [r4, r2]	@ movhi
	ldr	r2, [r4, #3860]
	ldr	r1, [r5, #4]
	str	r1, [r2, r3, asl #2]
	mov	r3, r3, asl #1
	ldr	r2, [r4, #3856]
	b	.L541
.L563:
	ldr	r8, .L565
	ldrh	r2, [r8, #116]
	ldrh	r3, [r8, #92]
	cmp	r2, r3
	bls	.L531
	ldr	r0, .L565+8
	movw	r2, #1204
	ldr	r1, .L565+12
	bl	printk
.L531:
	ldrh	r0, [r8, #92]
	mov	sl, r7
	ldrh	r1, [r8, #116]
	mov	r8, r6
	sub	fp, r0, #1
	ldr	ip, [r4, #3868]
	uxth	r3, fp
	rsb	fp, r1, fp
	b	.L532
.L538:
	mov	r6, r2, asl #2
	ldr	r7, [r5, #4]
	str	r6, [sp, #16]
	ldr	r6, [ip, r2, asl #2]
	cmp	r7, r6
	bls	.L533
	ldr	ip, [ip, #0]
	mov	r7, sl
	mov	r6, r8
	cmp	ip, #0
	bne	.L534
	cmp	r1, r0
	addne	r1, r1, #1
	ldrne	r0, .L565
	strneh	r1, [r0, #116]	@ movhi
.L534:
	mov	r0, #0
	sxth	fp, r3
	ldr	sl, .L565
	mov	r8, r3
	str	r2, [sp, #4]
	b	.L535
.L536:
	ldr	ip, [r4, #3868]
	add	r1, r3, #1
	add	r0, r0, #1
	ldr	r2, [ip, r1, asl #2]
	mov	r1, r1, asl #1
	uxth	r0, r0
	str	r2, [ip, r3, asl #2]
	mov	r3, r3, asl #1
	ldr	ip, [sl, #120]
	ldrh	r1, [ip, r1]
	strh	r1, [ip, r3]	@ movhi
.L535:
	sxth	r3, r0
	cmp	r3, fp
	bne	.L536
	ldr	r1, [r9, #3868]
	mov	r3, r8
	ldr	r0, [r5, #4]
	ldr	ip, [sp, #16]
	ldr	r2, [sp, #4]
	str	r0, [r1, ip]
	ldr	r1, .L565
	mov	r2, r2, asl #1
	ldr	r1, [r1, #120]
	strh	r7, [r1, r2]	@ movhi
	b	.L537
.L533:
	sub	r3, r3, #1
	uxth	r3, r3
.L532:
	sxth	r2, r3
	cmp	r2, fp
	bgt	.L538
	mov	r7, sl
	mov	r6, r8
.L537:
	sxth	r3, r3
	cmp	r3, #0
	blt	.L517
	ldr	r2, .L565
	ldrh	r0, [r2, #92]
	ldrh	r1, [r2, #116]
	sub	r0, r0, #1
	rsb	r0, r1, r0
	sxth	r0, r0
	cmp	r3, r0
	bgt	.L517
	add	r1, r1, #1
	ldr	r0, [r5, #4]
	strh	r1, [r2, #116]	@ movhi
	ldr	r1, [r4, #3868]
	str	r0, [r1, r3, asl #2]
	mov	r3, r3, asl #1
	ldr	r2, [r2, #120]
	b	.L541
.L519:
	mov	r1, #2560
	movw	r0, #65535
	ldrh	r2, [r2, r1]
	ldr	r3, .L565
	cmp	r2, r0
	beq	.L562
.L539:
	movw	r2, #2564
	ldrh	r0, [r3, r2]
	movw	r2, #65535
	cmp	r0, r2
	beq	.L540
	mov	r1, #1
	bl	FtlFreeSysBlkQueueIn
.L540:
	ldr	r3, .L565
	ldr	r2, [r5, #4]
	ldr	r1, [r3, #2568]
	cmp	r1, r2
	movw	r2, #2564
	bcs	.L541
	mov	r1, #2560
	ldrh	r0, [r3, r1]
	strh	r0, [r3, r2]	@ movhi
.L562:
	ldr	r2, [r5, #4]
	strh	r7, [r3, r1]	@ movhi
	str	r2, [r3, #2568]
	b	.L517
.L541:
	strh	r7, [r3, r2]	@ movhi
	b	.L517
.L564:
	mov	r0, r7
	mov	r1, #0
	b	.L561
.L514:
	mov	r0, r7
	mov	r1, #1
.L561:
	bl	FtlFreeSysBlkQueueIn
.L517:
	ldr	ip, [sp, #12]
	add	r6, r6, #20
	add	r3, ip, #1
	ldr	ip, [sp, #20]
	uxth	r3, r3
	str	r3, [sp, #12]
	cmp	r3, ip
	bne	.L542
.L510:
	ldr	r6, [sp, #8]
	add	r3, r6, #1
	uxth	r3, r3
	str	r3, [sp, #8]
.L507:
	ldr	r5, .L565
	ldr	ip, [sp, #8]
	ldrh	r3, [r5, #14]
	cmp	r3, ip
	bhi	.L557
	ldr	r1, .L565+4
	ldr	r2, [r1, #3856]
	ldrh	r3, [r2, #0]
	cmp	r3, #0
	bne	.L544
	movw	r0, #3852
	ldrh	r1, [r1, r0]
	cmp	r1, #0
	ldrne	r0, [r5, #100]
	bne	.L545
	b	.L544
.L549:
	mov	r1, r1, asl #1
	ldrh	r1, [r2, r1]
	cmp	r1, #0
	beq	.L546
	ldr	r7, .L565
	sxth	r6, r3
	ldr	r1, .L565+4
	mov	r5, #0
	b	.L547
.L548:
	ldr	ip, [r1, #3856]
	mov	r0, r2, asl #1
	rsb	r4, r6, r2
	add	r3, r3, #1
	ldrh	sl, [ip, r0]
	mov	r8, r4, asl #1
	uxth	r3, r3
	strh	sl, [ip, r8]	@ movhi
	ldr	ip, [r1, #3860]
	ldr	r2, [ip, r2, asl #2]
	str	r2, [ip, r4, asl #2]
	ldr	r2, [r1, #3856]
	strh	r5, [r2, r0]	@ movhi
.L547:
	ldr	r0, [r7, #100]
	sxth	r2, r3
	cmp	r2, r0
	bcc	.L548
	b	.L544
.L546:
	add	r3, r3, #1
	uxth	r3, r3
.L545:
	sxth	r1, r3
	cmp	r1, r0
	bcc	.L549
.L544:
	ldr	r2, .L565
	ldr	r1, [r2, #120]
	ldrh	r3, [r1, #0]
	cmp	r3, #0
	bne	.L550
	ldrh	r0, [r2, #116]
	cmp	r0, #0
	ldrneh	r2, [r2, #92]
	bne	.L551
	b	.L550
.L555:
	mov	ip, r0, asl #1
	ldrh	ip, [r1, ip]
	cmp	ip, #0
	beq	.L552
	ldr	r1, .L565
	mov	r6, #0
	ldr	r7, .L565+4
	b	.L553
.L554:
	ldr	r4, [r1, #120]
	mov	ip, r2, asl #1
	rsb	r5, r0, r2
	add	r3, r3, #1
	ldrh	sl, [r4, ip]
	mov	r8, r5, asl #1
	uxth	r3, r3
	strh	sl, [r4, r8]	@ movhi
	ldr	r4, [r7, #3868]
	ldr	r2, [r4, r2, asl #2]
	str	r2, [r4, r5, asl #2]
	ldr	r2, [r1, #120]
	strh	r6, [r2, ip]	@ movhi
.L553:
	ldrh	ip, [r1, #92]
	sxth	r2, r3
	cmp	r2, ip
	blt	.L554
	b	.L550
.L552:
	add	r3, r3, #1
	uxth	r3, r3
.L551:
	sxth	r0, r3
	cmp	r0, r2
	blt	.L555
.L550:
	ldr	r2, .L565+4
	movw	r3, #3852
	ldrh	r2, [r2, r3]
	ldr	r3, .L565
	ldr	r3, [r3, #100]
	cmp	r2, r3
	bls	.L556
	ldr	r0, .L565+8
	movw	r2, #1329
	ldr	r1, .L565+12
	bl	printk
.L556:
	mov	r0, #0
	add	sp, sp, #28
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, sl, fp, pc}
.L566:
	.align	2
.L565:
	.word	.LANCHOR0
	.word	.LANCHOR3
	.word	.LC1
	.word	.LANCHOR1+204
	.fnend
	.size	FtlScanSysBlk, .-FtlScanSysBlk
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
	b	.L568
.L572:
	ldr	r0, [r7, r3, asl #2]
	mov	r2, #0
	ubfx	r0, r0, #10, #16
	b	.L569
.L571:
	mov	r1, r2, asl #1
	add	r2, r2, #1
	ldrh	ip, [r6, r1]
	uxth	r2, r2
	cmp	ip, r0
	ldreqh	ip, [r5, r1]
	addeq	ip, ip, #1
	streqh	ip, [r5, r1]	@ movhi
.L569:
	ldrh	r1, [r4, #10]
	cmp	r1, r2
	bhi	.L571
	add	r3, r3, #1
	uxth	r3, r3
.L568:
	ldrh	r2, [r4, #6]
	cmp	r2, r3
	bhi	.L572
	mov	sl, #0
	ldrh	fp, [r5, #0]
	mov	r7, sl
	ldr	r3, .L578
	b	.L573
.L577:
	ldrh	r2, [r4, #0]
	cmp	r2, r7
	bne	.L574
	ldrh	r1, [r4, #2]
	ldrh	r2, [r3, #74]
	cmp	r1, r2
	movcc	r1, r7, asl #1
	strcch	r2, [r5, r1]	@ movhi
.L574:
	mov	r9, r7, asl #1
	ldrh	r8, [r5, r9]
	cmp	fp, r8
	movhi	sl, r7
	movhi	fp, r8
	cmp	r8, #0
	bne	.L576
	ldrh	r0, [r6, r9]
	cmp	r0, #0
	beq	.L576
	mov	r1, #1
	str	r3, [sp, #4]
	bl	FtlFreeSysBlkQueueIn
	strh	r8, [r6, r9]	@ movhi
	ldr	r3, [sp, #4]
	ldrh	r2, [r4, #8]
	sub	r2, r2, #1
	strh	r2, [r4, #8]	@ movhi
.L576:
	add	r7, r7, #1
	uxth	r7, r7
.L573:
	ldrh	r2, [r4, #10]
	cmp	r2, r7
	bhi	.L577
	mov	r0, sl
	ldmfd	sp!, {r1, r2, r3, r4, r5, r6, r7, r8, r9, sl, fp, pc}
.L579:
	.align	2
.L578:
	.word	.LANCHOR0
	.fnend
	.size	ftl_free_no_use_map_blk, .-ftl_free_no_use_map_blk
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
	bne	.L581
	ldrh	r3, [r0, #8]
	cmp	r3, #0
	beq	.L582
	ldr	r0, .L587
	mov	r2, #592
	ldr	r1, .L587+4
	bl	printk
.L582:
	ldrh	r3, [r4, #8]
	add	r3, r3, #1
	strh	r3, [r4, #8]	@ movhi
	bl	FtlFreeSysBlkQueueOut
	mov	r3, #0
	strh	r0, [r5, #0]	@ movhi
	strh	r3, [r4, #2]	@ movhi
	strh	r3, [r4, #0]	@ movhi
	ldr	r3, [r4, #28]
	add	r3, r3, #1
	str	r3, [r4, #28]
	b	.L583
.L581:
	mov	r3, r3, asl #1
	ldr	r2, .L587+8
	mov	r1, #255
	ldrh	r6, [r5, r3]
	ldr	r5, .L587+12
	ldrh	r3, [r0, #2]
	orr	r3, r3, r6, asl #10
	str	r3, [r2, #-852]
	ldr	r3, [r5, #2240]
	str	r3, [r2, #-848]
	ldr	r3, [r2, #-836]
	str	r3, [r2, #-844]
	ldr	r2, [r0, #28]
	str	r2, [r3, #4]
	ldr	r2, .L587+16
	strh	r2, [r3, #8]	@ movhi
	ldrh	r2, [r0, #4]
	strh	r6, [r3, #2]	@ movhi
	strh	r2, [r3, #0]	@ movhi
	ldrh	r2, [r5, #74]
	ldr	r0, [r5, #2240]
	mov	r2, r2, asl #3
	bl	ftl_memset
	mov	r2, #0
	mov	r3, r2
	b	.L584
.L586:
	ldr	r1, [r7, r3, asl #2]
	cmp	r6, r1, lsr #10
	bne	.L585
	add	r2, r2, #1
	ldr	r1, [r5, #2240]
	uxth	r2, r2
	str	r3, [r1, r2, asl #3]
	ldr	r1, [r5, #2240]
	ldr	r0, [r7, r3, asl #2]
	add	r1, r1, r2, asl #3
	str	r0, [r1, #4]
.L585:
	add	r3, r3, #1
	uxth	r3, r3
.L584:
	ldrh	r1, [r4, #6]
	cmp	r1, r3
	bhi	.L586
	mov	r1, #1
	mov	r3, #0
	ldr	r0, .L587+20
	mov	r2, r1
	bl	FlashProgPages
	ldrh	r3, [r4, #2]
	mov	r0, r4
	add	r3, r3, #1
	strh	r3, [r4, #2]	@ movhi
	bl	ftl_map_blk_gc
.L583:
	mov	r0, #0
	ldmfd	sp!, {r3, r4, r5, r6, r7, pc}
.L588:
	.align	2
.L587:
	.word	.LC1
	.word	.LANCHOR1+218
	.word	.LANCHOR3
	.word	.LANCHOR0
	.word	-1291
	.word	.LANCHOR3-856
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
	ldr	r7, .L605
	mov	r9, r1
	ldr	r5, .L605+4
	mov	fp, r2
	mov	r6, #0
.L603:
	ldr	r3, [r7, #2460]
	add	r3, r3, #1
	str	r3, [r7, #2460]
	ldrh	r3, [r7, #74]
	ldrh	r2, [r4, #2]
	sub	r3, r3, #1
	cmp	r2, r3
	bge	.L591
	ldrh	r3, [r4, #0]
	movw	r2, #65535
	cmp	r3, r2
	bne	.L592
.L591:
	mov	r0, r4
	bl	Ftl_write_map_blk_to_last_page
.L592:
	ldrh	r3, [r4, #0]
	ldr	r2, [r4, #12]
	mov	r3, r3, asl #1
	ldrh	r3, [r2, r3]
	cmp	r3, #0
	bne	.L593
	ldr	r0, .L605+8
	movw	r2, #650
	ldr	r1, .L605+12
	bl	printk
.L593:
	ldrh	r2, [r4, #0]
	ldrh	r3, [r4, #10]
	cmp	r2, r3
	bcc	.L594
	ldr	r0, .L605+8
	movw	r2, #651
	ldr	r1, .L605+12
	bl	printk
.L594:
	ldrh	r3, [r4, #0]
	mov	r1, #0
	ldr	r2, [r4, #12]
	ldr	r0, [r5, #-836]
	mov	r3, r3, asl #1
	ldr	r8, .L605+4
	ldrh	sl, [r2, r3]
	mov	r2, #16
	ldrh	r3, [r4, #2]
	str	fp, [r5, #-848]
	orr	r3, r3, sl, asl #10
	str	r0, [r5, #-844]
	str	r3, [r5, #-852]
	bl	ftl_memset
	ldr	r3, [r5, #-844]
	mov	r1, #1
	ldr	r2, [r4, #28]
	sub	r0, r8, #856
	strh	r9, [r3, #8]	@ movhi
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
	ldr	r2, [r5, #-856]
	cmn	r2, #1
	bne	.L595
	ldr	r0, .L605+16
	add	r6, r6, #1
	ldr	r1, [r5, #-852]
	bl	printk
	ldrh	r3, [r4, #2]
	uxth	r6, r6
	cmp	r3, #2
	ldrlsh	r3, [r7, #74]
	subls	r3, r3, #1
	strlsh	r3, [r4, #2]	@ movhi
	cmp	r6, #3
	bls	.L597
	ldr	r3, .L605+4
	mov	r2, r6
	ldr	r0, .L605+20
	ldr	r1, [r3, #-852]
	bl	printk
.L598:
	b	.L598
.L597:
	ldr	r3, [r4, #32]
	cmp	r3, #0
	beq	.L603
.L604:
	b	.L604
.L595:
	cmp	r3, #1
	beq	.L603
	ldr	r2, [r8, #-852]
	mov	r0, #0
	ldr	r3, [r4, #24]
	str	r2, [r3, r9, asl #2]
	ldmfd	sp!, {r3, r4, r5, r6, r7, r8, r9, sl, fp, pc}
.L606:
	.align	2
.L605:
	.word	.LANCHOR0
	.word	.LANCHOR3
	.word	.LC1
	.word	.LANCHOR1+249
	.word	.LC79
	.word	.LC80
	.fnend
	.size	FtlMapWritePage, .-FtlMapWritePage
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
	ldr	r5, [r0, #12]
	ldr	r8, [r0, #24]
	bl	ftl_free_no_use_map_blk
	ldrh	r3, [r4, #10]
	ldrh	r2, [r4, #8]
	sub	r3, r3, #4
	cmp	r2, r3
	blt	.L608
	uxth	r0, r0
	mov	r0, r0, asl #1
	ldrh	r7, [r5, r0]
	cmp	r7, #0
	beq	.L608
	ldr	r3, [r4, #32]
	cmp	r3, #0
	bne	.L608
	mov	r2, #1
	str	r2, [r4, #32]
	strh	r3, [r5, r0]	@ movhi
	ldrh	r3, [r4, #8]
	ldrh	r2, [r4, #2]
	sub	r3, r3, #1
	strh	r3, [r4, #8]	@ movhi
	ldr	r3, .L616
	ldrh	r3, [r3, #74]
	cmp	r2, r3
	bcc	.L609
	mov	r0, r4
	bl	ftl_map_blk_alloc_new_blk
.L609:
	ldr	r6, .L616+4
	mov	r5, #0
	mov	r9, r6
	b	.L610
.L614:
	ldr	r3, [r8, r5, asl #2]
	mov	fp, r5, asl #2
	cmp	r7, r3, lsr #10
	bne	.L611
	ldr	r3, [r6, #3872]
	mov	r1, #1
	ldr	sl, [r6, #-836]
	mov	r2, r1
	ldr	r0, .L616+8
	str	r3, [r6, #-848]
	str	sl, [r6, #-844]
	ldr	r3, [r8, r5, asl #2]
	str	r3, [r6, #-852]
	bl	FlashReadPages
	ldrh	r3, [sl, #8]
	cmp	r3, r5
	beq	.L612
	ldr	r0, .L616+12
	mov	r2, #564
	ldr	r1, .L616+16
	bl	printk
.L612:
	ldr	r3, [r9, #-856]
	cmn	r3, #1
	moveq	r3, #0
	streq	r3, [r8, fp]
	beq	.L611
	mov	r0, r4
	mov	r1, r5
	ldr	r2, [r6, #-848]
	bl	FtlMapWritePage
.L611:
	add	r5, r5, #1
	uxth	r5, r5
.L610:
	ldrh	r3, [r4, #6]
	cmp	r3, r5
	bhi	.L614
	mov	r0, r7
	mov	r1, #1
	bl	FtlFreeSysBlkQueueIn
	mov	r3, #0
	str	r3, [r4, #32]
.L608:
	ldr	r3, .L616
	ldrh	r2, [r4, #2]
	ldrh	r3, [r3, #74]
	cmp	r2, r3
	bcc	.L615
	mov	r0, r4
	bl	ftl_map_blk_alloc_new_blk
.L615:
	mov	r0, #0
	ldmfd	sp!, {r3, r4, r5, r6, r7, r8, r9, sl, fp, pc}
.L617:
	.align	2
.L616:
	.word	.LANCHOR0
	.word	.LANCHOR3
	.word	.LANCHOR3-856
	.word	.LC1
	.word	.LANCHOR1+265
	.fnend
	.size	ftl_map_blk_gc, .-ftl_map_blk_gc
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
	ldrh	r8, [r0, #6]
	mov	r1, #0
	ldr	r9, [r0, #24]
	str	r3, [sp, #16]
	ldrh	r3, [r0, #8]
	mov	r2, r8, asl #2
	ldr	fp, [r0, #12]
	mov	r0, r9
	ldr	r6, .L638
	str	r3, [sp, #8]
	bl	ftl_memset
	ldr	r3, .L638+4
	ldr	r2, [r6, #2240]
	ldr	r5, [r3, #-836]
	str	r2, [r3, #-848]
	mov	r2, #1
	str	r2, [r4, #36]
	str	r5, [r3, #-844]
	mvn	r3, #0
	strh	r3, [r4, #0]	@ movhi
	strh	r3, [r4, #2]	@ movhi
	mov	r3, #0
	str	r3, [r4, #32]
	str	r3, [r4, #28]
	str	r3, [sp, #4]
	ldr	r3, [sp, #8]
	sub	r3, r3, #1
	str	r3, [sp, #12]
	b	.L619
.L632:
	ldr	r1, [sp, #12]
	cmp	r3, r1
	bne	.L620
	mov	r3, r1, asl #1
	mov	r1, #1
	add	r2, fp, r3
	str	r2, [sp, #8]
	ldrh	r0, [fp, r3]
	mov	r7, #0
	bl	FtlGetLastWrittenPage
	ldr	r1, [sp, #12]
	ldr	r2, [sp, #16]
	ldr	r6, .L638+4
	uxth	r0, r0
	add	r3, r0, #1
	strh	r3, [r4, #2]	@ movhi
	ldr	r3, [sp, #4]
	sxth	sl, r0
	add	sl, sl, #1
	strh	r3, [r4, #0]	@ movhi
	ldr	r3, [r2, r1, asl #2]
	str	r3, [r4, #28]
	b	.L621
.L623:
	ldr	r1, [sp, #8]
	ldr	r0, .L638+8
	ldrh	r2, [r1, #0]
	mov	r1, #1
	orr	r3, r3, r2, asl #10
	mov	r2, r1
	str	r3, [r6, #-852]
	bl	FlashReadPages
	ldr	r3, [r6, #-856]
	cmn	r3, #1
	beq	.L622
	ldrh	r3, [r5, #8]
	cmp	r3, r8
	bcs	.L622
	ldrh	r2, [r4, #4]
	ldrh	r1, [r5, #0]
	cmp	r1, r2
	ldreq	r2, [r6, #-852]
	streq	r2, [r9, r3, asl #2]
.L622:
	add	r7, r7, #1
	uxth	r7, r7
.L621:
	sxth	r3, r7
	cmp	r3, sl
	blt	.L623
	b	.L624
.L620:
	ldr	r7, .L638+4
	mov	r3, r3, asl #1
	ldr	r2, [r6, #2240]
	mov	r1, #1
	sub	r0, r7, #856
	str	r2, [r7, #-848]
	add	r2, fp, r3
	str	r2, [sp, #20]
	ldrh	r2, [fp, r3]
	ldrh	r3, [r6, #74]
	sub	r3, r3, #1
	orr	r3, r3, r2, asl #10
	mov	r2, r1
	str	r3, [r7, #-852]
	bl	FlashReadPages
	ldr	r3, [r7, #-856]
	cmn	r3, #1
	beq	.L635
	ldrh	r2, [r5, #0]
	ldrh	r3, [r4, #4]
	cmp	r2, r3
	bne	.L635
	ldrh	r2, [r5, #8]
	movw	r3, #64245
	cmp	r2, r3
	bne	.L635
	b	.L636
.L628:
	ldr	r1, [r6, #2240]
	mov	r0, r2, asl #3
	add	r3, r3, #1
	ldr	r2, [r1, r2, asl #3]
	uxth	r3, r3
	uxth	r2, r2
	cmp	r2, r8
	addcc	r1, r1, r0
	ldrcc	r1, [r1, #4]
	strcc	r1, [r9, r2, asl #2]
	b	.L626
.L636:
	mov	r3, #0
.L626:
	ldrh	r1, [r6, #74]
	sxth	r2, r3
	sub	r1, r1, #1
	cmp	r2, r1
	blt	.L628
	b	.L629
.L631:
	ldr	r1, [sp, #20]
	ldr	r0, .L638+8
	ldrh	r2, [r1, #0]
	mov	r1, #1
	orr	r3, r3, r2, asl #10
	mov	r2, r1
	str	r3, [sl, #-852]
	bl	FlashReadPages
	ldr	r3, [sl, #-856]
	cmn	r3, #1
	beq	.L630
	ldrh	r3, [r5, #8]
	cmp	r3, r8
	bcs	.L630
	ldrh	r2, [r4, #4]
	ldrh	r1, [r5, #0]
	cmp	r1, r2
	ldreq	r2, [sl, #-852]
	streq	r2, [r9, r3, asl #2]
.L630:
	add	r7, r7, #1
	uxth	r7, r7
	b	.L637
.L635:
	ldr	sl, .L638+4
	mov	r7, #0
.L637:
	ldrh	r2, [r6, #74]
	sxth	r3, r7
	cmp	r3, r2
	blt	.L631
.L629:
	ldr	r2, [sp, #4]
	add	r3, r2, #1
	uxth	r3, r3
	str	r3, [sp, #4]
.L619:
	ldr	r1, [sp, #4]
	ldr	r2, [sp, #8]
	sxth	r3, r1
	cmp	r3, r2
	blt	.L632
.L624:
	mov	r0, r4
	bl	ftl_free_no_use_map_blk
	ldr	r3, .L638
	ldrh	r2, [r4, #2]
	ldrh	r3, [r3, #74]
	cmp	r2, r3
	bne	.L633
	mov	r0, r4
	bl	ftl_map_blk_alloc_new_blk
.L633:
	mov	r0, r4
	bl	ftl_map_blk_gc
	mov	r0, r4
	bl	ftl_map_blk_gc
	mov	r0, #0
	add	sp, sp, #28
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, sl, fp, pc}
.L639:
	.align	2
.L638:
	.word	.LANCHOR0
	.word	.LANCHOR3
	.word	.LANCHOR3-856
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
	ldr	r0, .L641
	bl	FtlMapTblRecovery
	mov	r0, #0
	ldmfd	sp!, {r3, pc}
.L642:
	.align	2
.L641:
	.word	.LANCHOR3+3812
	.fnend
	.size	FtlLoadMapInfo, .-FtlLoadMapInfo
	.align	2
	.global	FtlLoadVonderInfo
	.type	FtlLoadVonderInfo, %function
FtlLoadVonderInfo:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	ldr	r2, .L644
	movw	r1, #3886
	stmfd	sp!, {r3, lr}
	.save {r3, lr}
	ldr	r3, .L644+4
	ldrh	r0, [r2, #92]
	strh	r0, [r3, r1]	@ movhi
	movw	r1, #3880
	ldr	r0, .L644+8
	strh	r0, [r3, r1]	@ movhi
	add	r1, r1, #4
	ldrh	r0, [r2, #116]
	strh	r0, [r3, r1]	@ movhi
	movw	r1, #3882
	ldrh	r0, [r2, #94]
	ldr	r2, [r2, #120]
	strh	r0, [r3, r1]	@ movhi
	str	r2, [r3, #3888]
	ldr	r2, [r3, #3868]
	ldr	r0, .L644+12
	str	r2, [r3, #3892]
	ldr	r2, [r3, #3916]
	str	r2, [r3, #3896]
	ldr	r2, [r3, #3920]
	str	r2, [r3, #3900]
	bl	FtlMapTblRecovery
	mov	r0, #0
	ldmfd	sp!, {r3, pc}
.L645:
	.align	2
.L644:
	.word	.LANCHOR0
	.word	.LANCHOR3
	.word	-3962
	.word	.LANCHOR3+3876
	.fnend
	.size	FtlLoadVonderInfo, .-FtlLoadVonderInfo
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
	ldr	r5, .L647
	mul	r4, r4, r0
	ldr	r0, .L647+4
	ldr	r2, [r5, #2420]
	add	r3, r2, r4
	ldrh	r1, [r2, r4]
	ldr	r2, [r3, #8]
	bl	FtlMapWritePage
	ldr	r3, [r5, #2420]
	mov	r0, #0
	add	r4, r3, r4
	ldr	r3, [r4, #4]
	bic	r3, r3, #-2147483648
	str	r3, [r4, #4]
	ldmfd	sp!, {r3, r4, r5, pc}
.L648:
	.align	2
.L647:
	.word	.LANCHOR0
	.word	.LANCHOR3+3812
	.fnend
	.size	flush_l2p_region, .-flush_l2p_region
	.align	2
	.global	FtlMapBlkWriteDump_data
	.type	FtlMapBlkWriteDump_data, %function
FtlMapBlkWriteDump_data:
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
	ldr	r5, .L653
	sub	r6, r6, #1
	mov	r2, #0
	str	r2, [r0, #36]
	uxth	r6, r6
	ldr	r0, [r5, #3872]
	ldr	r2, [r5, #-836]
	str	r0, [r5, #-848]
	str	r2, [r5, #-844]
	ldr	r3, [r3, r6, asl #2]
	cmp	r3, #0
	str	r3, [r5, #-852]
	beq	.L651
	mov	r1, #1
	sub	r0, r5, #856
	mov	r2, r1
	bl	FlashReadPages
	b	.L652
.L651:
	ldr	r3, .L653+4
	mov	r1, #255
	ldrh	r2, [r3, #82]
	bl	ftl_memset
.L652:
	mov	r0, r4
	mov	r1, r6
	ldr	r2, [r5, #-848]
	ldmfd	sp!, {r4, r5, r6, lr}
	b	FtlMapWritePage
.L654:
	.align	2
.L653:
	.word	.LANCHOR3
	.word	.LANCHOR0
	.fnend
	.size	FtlMapBlkWriteDump_data, .-FtlMapBlkWriteDump_data
	.align	2
	.global	load_l2p_region
	.type	load_l2p_region, %function
load_l2p_region:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	stmfd	sp!, {r3, r4, r5, r6, r7, r8, sl, lr}
	.save {r3, r4, r5, r6, r7, r8, sl, lr}
	mov	r5, r0
	ldr	r3, .L661
	mov	r7, r1
	ldrh	r3, [r3, #108]
	cmp	r3, r0
	bcs	.L656
	ldr	r0, .L661+4
	movw	r2, #446
	ldr	r1, .L661+8
	bl	printk
.L656:
	ldr	r4, .L661+12
	mov	r8, #12
	ldr	r6, .L661
	ldr	r3, [r4, #3864]
	ldr	sl, [r3, r5, asl #2]
	cmp	sl, #0
	bne	.L657
	mul	r8, r8, r7
	ldr	r3, [r6, #2420]
	mov	r1, #255
	ldrh	r2, [r6, #82]
	add	r3, r3, r8
	ldr	r0, [r3, #8]
	bl	ftl_memset
	ldr	r3, [r6, #2420]
	strh	r5, [r3, r8]	@ movhi
	ldr	r3, [r6, #2420]
	add	r8, r3, r8
	str	sl, [r8, #4]
	b	.L658
.L657:
	mul	r8, r8, r7
	ldr	r3, [r6, #2420]
	mov	r1, #1
	sub	r0, r4, #856
	mov	r2, r1
	str	sl, [r4, #-852]
	add	r3, r3, r8
	ldr	r3, [r3, #8]
	str	r3, [r4, #-848]
	ldr	r3, [r4, #-836]
	str	r3, [r4, #-844]
	bl	FlashReadPages
	ldr	r7, [r4, #-844]
	ldrh	r3, [r7, #8]
	cmp	r3, r5
	beq	.L659
	mov	r1, r5
	mov	r2, sl
	ldr	r0, .L661+16
	bl	printk
	mov	r2, #4
	mov	r3, r2
	ldr	r0, .L661+20
	ldr	r1, [r4, #-844]
	bl	rknand_print_hex
	ldr	r0, .L661+24
	ldr	r1, [r4, #3864]
	mov	r2, #4
	ldrh	r3, [r6, #108]
	bl	rknand_print_hex
.L659:
	ldrh	r3, [r7, #8]
	cmp	r3, r5
	beq	.L660
	ldr	r0, .L661+4
	movw	r2, #467
	ldr	r1, .L661+8
	bl	printk
.L660:
	ldr	r3, .L661
	mov	r1, #0
	ldr	r3, [r3, #2420]
	add	r2, r3, r8
	str	r1, [r2, #4]
	strh	r5, [r3, r8]	@ movhi
.L658:
	mov	r0, #0
	ldmfd	sp!, {r3, r4, r5, r6, r7, r8, sl, pc}
.L662:
	.align	2
.L661:
	.word	.LANCHOR0
	.word	.LC1
	.word	.LANCHOR1+280
	.word	.LANCHOR3
	.word	.LC81
	.word	.LC82
	.word	.LC83
	.fnend
	.size	load_l2p_region, .-load_l2p_region
	.align	2
	.global	log2phys
	.type	log2phys, %function
log2phys:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	stmfd	sp!, {r4, r5, r6, r7, r8, lr}
	.save {r4, r5, r6, r7, r8, lr}
	mvn	r3, #0
	ldr	r7, .L673
	mov	r6, r1
	mov	r8, r2
	ldrh	r5, [r7, #80]
	add	r5, r5, #7
	mov	r4, r0, lsr r5
	bic	r5, r0, r3, asl r5
	ldr	r3, [r7, #2440]
	uxth	r4, r4
	cmp	r0, r3
	uxth	r5, r5
	bcc	.L664
	ldr	r0, .L673+4
	movw	r2, #759
	ldr	r1, .L673+8
	bl	printk
.L664:
	mov	r3, #0
	ldrh	r1, [r7, #110]
	ldr	r2, [r7, #2420]
	mov	r7, r3
	b	.L665
.L671:
	add	r3, r3, #12
	add	r0, r2, r3
	ldrh	r0, [r0, #-12]
	cmp	r0, r4
	bne	.L666
.L667:
	cmp	r8, #0
	ldr	r3, .L673
	mov	r2, #12
	bne	.L668
	ldr	r3, [r3, #2420]
	mla	r2, r2, r7, r3
	ldr	r3, [r2, #8]
	ldr	r3, [r3, r5, asl #2]
	str	r3, [r6, #0]
	b	.L669
.L668:
	mul	r2, r2, r7
	ldr	r1, [r3, #2420]
	ldr	r0, [r6, #0]
	add	r1, r1, r2
	ldr	r1, [r1, #8]
	str	r0, [r1, r5, asl #2]
	ldr	r1, [r3, #2420]
	add	r2, r1, r2
	ldr	r1, [r2, #4]
	orr	r1, r1, #-2147483648
	str	r1, [r2, #4]
	movw	r2, #2424
	strh	r4, [r3, r2]	@ movhi
.L669:
	ldr	r3, .L673
	mov	r2, #12
	mov	r0, #0
	ldr	r3, [r3, #2420]
	mla	r7, r2, r7, r3
	ldr	r3, [r7, #4]
	cmn	r3, #1
	addne	r3, r3, #1
	strne	r3, [r7, #4]
	ldmfd	sp!, {r4, r5, r6, r7, r8, pc}
.L666:
	add	r7, r7, #1
	uxth	r7, r7
.L665:
	cmp	r7, r1
	bne	.L671
	bl	select_l2p_ram_region
	mov	r3, #12
	ldr	r2, .L673
	ldr	r2, [r2, #2420]
	mul	r3, r3, r0
	mov	r7, r0
	add	r1, r2, r3
	ldrh	r2, [r2, r3]
	movw	r3, #65535
	cmp	r2, r3
	beq	.L672
	ldr	r3, [r1, #4]
	cmp	r3, #0
	bge	.L672
	bl	flush_l2p_region
.L672:
	mov	r0, r4
	mov	r1, r7
	bl	load_l2p_region
	b	.L667
.L674:
	.align	2
.L673:
	.word	.LANCHOR0
	.word	.LC1
	.word	.LANCHOR1+296
	.fnend
	.size	log2phys, .-log2phys
	.align	2
	.type	FtlReadRefresh.part.10, %function
FtlReadRefresh.part.10:
	.fnstart
	@ args = 0, pretend = 0, frame = 88
	@ frame_needed = 0, uses_anonymous_args = 0
	stmfd	sp!, {r4, r5, lr}
	.save {r4, r5, lr}
	.pad #92
	sub	sp, sp, #92
	ldr	r4, .L680
	mov	r5, #2048
.L678:
	ldr	r0, [r4, #2708]
	ldr	r3, [r4, #2440]
	cmp	r0, r3
	bcs	.L676
	mov	r2, #0
	mov	r1, sp
	bl	log2phys
	ldr	r2, [sp, #0]
	ldr	r3, [r4, #2708]
	cmn	r2, #1
	add	r3, r3, #1
	str	r3, [r4, #2708]
	beq	.L677
	str	r3, [sp, #20]
	add	r0, sp, #88
	ldr	r3, .L680+4
	mov	r1, #1
	str	r2, [sp, #8]
	mov	r2, #0
	str	r2, [r0, #-84]!
	str	r3, [sp, #12]
	add	r3, sp, #24
	str	r3, [sp, #16]
	bl	FlashReadPages
	ldr	r3, [sp, #4]
	cmp	r3, #256
	bne	.L676
	ldr	r0, [sp, #0]
	ubfx	r0, r0, #10, #16
	bl	P2V_block_in_plane
	bl	FtlGcRefreshBlock
	b	.L676
.L677:
	subs	r5, r5, #1
	bne	.L678
.L676:
	mvn	r0, #0
	add	sp, sp, #92
	ldmfd	sp!, {r4, r5, pc}
.L681:
	.align	2
.L680:
	.word	.LANCHOR0
	.word	.LANCHOR0+3232
	.fnend
	.size	FtlReadRefresh.part.10, .-FtlReadRefresh.part.10
	.align	2
	.global	FtlReadRefresh
	.type	FtlReadRefresh, %function
FtlReadRefresh:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	ldr	r2, .L688
	stmfd	sp!, {r4, lr}
	.save {r4, lr}
	ldr	r3, [r2, #2704]
	cmp	r3, #0
	mov	r3, r2
	beq	.L683
	ldr	r1, [r2, #2708]
	ldr	r3, [r2, #2440]
	cmp	r1, r3
	bcs	.L684
	ldmfd	sp!, {r4, lr}
	b	FtlReadRefresh.part.10
.L684:
	mov	r3, #0
	str	r3, [r2, #2704]
	str	r3, [r2, #2708]
	ldr	r3, [r2, #2444]
	str	r3, [r2, #2700]
	b	.L685
.L683:
	ldr	r1, [r2, #2700]
	ldr	ip, [r2, #2500]
	ldr	r0, [r2, #2436]
	ldr	r2, [r2, #2444]
	add	r4, r2, #1048576
	cmp	r1, r4
	bhi	.L686
	add	r0, r0, ip, lsr #10
	mov	ip, #33554432
	add	r1, r1, ip, asr r0
	cmp	r1, r2
	bcc	.L686
	ldrb	r1, [r3, #2536]	@ zero_extendqisi2
	cmp	r1, #0
	bne	.L685
.L686:
	mov	r1, #1
	str	r2, [r3, #2700]
	str	r1, [r3, #2704]
	mov	r1, #0
	str	r1, [r3, #2708]
.L685:
	mov	r0, #0
	ldmfd	sp!, {r4, pc}
.L689:
	.align	2
.L688:
	.word	.LANCHOR0
	.fnend
	.size	FtlReadRefresh, .-FtlReadRefresh
	.align	2
	.global	ftl_scan_all_data
	.type	ftl_scan_all_data, %function
ftl_scan_all_data:
	.fnstart
	@ args = 0, pretend = 0, frame = 8
	@ frame_needed = 0, uses_anonymous_args = 0
	stmfd	sp!, {r4, r5, r6, r7, r8, lr}
	.save {r4, r5, r6, r7, r8, lr}
	mov	r1, #0
	ldr	r5, .L696
	.pad #32
	sub	sp, sp, #32
	ldr	r0, .L696+4
	mov	r4, #0
	bl	printk
	ldr	r7, .L696+8
	mov	r6, r5
	b	.L691
.L695:
	mov	r0, r4
	add	r1, sp, #28
	mov	r2, #0
	bl	log2phys
	movs	r3, r4, asl #21
	bne	.L692
	ldr	r0, .L696+12
	mov	r1, r4
	ldr	r2, [sp, #28]
	bl	printk
.L692:
	ldr	r3, [sp, #28]
	cmn	r3, #1
	beq	.L693
	str	r3, [r5, #-852]
	mov	r2, #0
	ldr	r3, [r7, #2240]
	mov	r1, #1
	ldr	r8, [r5, #-836]
	ldr	r0, .L696+16
	str	r3, [r5, #-848]
	str	r4, [r5, #-840]
	str	r8, [r5, #-844]
	str	r2, [r5, #-856]
	bl	FlashReadPages
	ldr	r3, [r5, #-856]
	cmn	r3, #1
	cmpne	r3, #256
	beq	.L694
	ldr	r3, [r8, #8]
	cmp	r3, r4
	beq	.L693
.L694:
	ldr	r3, [r6, #-844]
	ldr	r2, [r6, #-848]
	ldr	r0, .L696+20
	ldr	r1, [r3, #4]
	str	r1, [sp, #0]
	ldr	r1, [r3, #8]
	str	r1, [sp, #4]
	ldr	r1, [r3, #12]
	str	r1, [sp, #8]
	ldr	r1, [r2, #0]
	str	r1, [sp, #12]
	mov	r1, r4
	ldr	r2, [r2, #4]
	str	r2, [sp, #16]
	ldr	r2, [r6, #-852]
	ldr	r3, [r3, #0]
	bl	printk
.L693:
	add	r4, r4, #1
.L691:
	ldr	r3, [r7, #2440]
	cmp	r4, r3
	bcc	.L695
	add	sp, sp, #32
	ldmfd	sp!, {r4, r5, r6, r7, r8, pc}
.L697:
	.align	2
.L696:
	.word	.LANCHOR3
	.word	.LC84
	.word	.LANCHOR0
	.word	.LC85
	.word	.LANCHOR3-856
	.word	.LC86
	.fnend
	.size	ftl_scan_all_data, .-ftl_scan_all_data
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
	ldr	r2, .L705
	ldr	r3, [r2, #2256]
	mov	r4, r0, asl #1
	mov	r5, r0
	ldrh	r1, [r3, r4]
	cmp	r1, #0
	addne	r1, r1, #1
	strneh	r1, [r3, r4]	@ movhi
	bne	.L700
	ldr	r3, [r2, #2268]
	cmp	r3, #0
	beq	.L700
	mov	r0, #2272
	mov	ip, #6
	ldrh	lr, [r2, r0]
	ldr	r2, [r2, #2248]
	ldr	r0, .L705+4
	rsb	r3, r2, r3
	mov	r3, r3, asr #1
	mul	r3, r0, r3
	movw	r0, #65535
	uxth	r3, r3
	b	.L701
.L704:
	cmp	r3, r5
	bne	.L702
	ldr	r6, .L705
	mov	r1, r5
	ldr	r0, .L705+8
	bl	List_remove_node
	mov	r3, #2272
	ldrh	r3, [r6, r3]
	cmp	r3, #0
	bne	.L703
	ldr	r0, .L705+12
	movw	r2, #1640
	ldr	r1, .L705+16
	bl	printk
.L703:
	mov	r3, #2272
	mov	r0, r5
	ldrh	r2, [r6, r3]
	sub	r2, r2, #1
	strh	r2, [r6, r3]	@ movhi
	bl	INSERT_DATA_LIST
	ldr	r3, [r6, #2256]
	ldrh	r2, [r3, r4]
	add	r2, r2, #1
	strh	r2, [r3, r4]	@ movhi
	b	.L700
.L702:
	mul	r3, ip, r3
	ldrh	r3, [r2, r3]
	cmp	r3, r0
	beq	.L700
	add	r1, r1, #1
	uxth	r1, r1
.L701:
	cmp	r1, lr
	bne	.L704
.L700:
	mov	r0, r7
	add	r1, sp, #4
	mov	r2, #1
	bl	log2phys
	ldmfd	sp!, {r1, r2, r3, r4, r5, r6, r7, pc}
.L706:
	.align	2
.L705:
	.word	.LANCHOR0
	.word	-1431655765
	.word	.LANCHOR0+2268
	.word	.LC1
	.word	.LANCHOR1+305
	.fnend
	.size	FtlReUsePrevPpa, .-FtlReUsePrevPpa
	.align	2
	.global	ftl_check_vpc
	.type	ftl_check_vpc, %function
ftl_check_vpc:
	.fnstart
	@ args = 0, pretend = 0, frame = 8
	@ frame_needed = 0, uses_anonymous_args = 0
	stmfd	sp!, {r0, r1, r4, r5, r6, r7, r8, r9, sl, lr}
	.save {r0, r1, r4, r5, r6, r7, r8, r9, sl, lr}
	mov	r4, #0
	ldr	r1, .L720
	ldr	r0, .L720+4
	bl	printk
	ldr	r0, .L720+8
	mov	r1, #0
	mov	r2, #8192
	bl	ftl_memset
	ldr	r6, .L720+12
	ldr	r5, .L720+16
	b	.L708
.L710:
	mov	r0, r4
	add	r1, sp, #4
	mov	r2, #0
	bl	log2phys
	ldr	r0, [sp, #4]
	cmn	r0, #1
	beq	.L709
	ubfx	r0, r0, #10, #16
	bl	P2V_block_in_plane
	add	r0, r5, r0, asl #1
	add	r0, r0, #3920
	ldrh	r3, [r0, #4]
	add	r3, r3, #1
	strh	r3, [r0, #4]	@ movhi
.L709:
	add	r4, r4, #1
.L708:
	ldr	r3, [r6, #2440]
	cmp	r4, r3
	bcc	.L710
	mov	r4, #0
	ldr	r7, .L720+12
	mov	r5, r4
	ldr	sl, .L720+16
	movw	r9, #65535
	b	.L711
.L713:
	mov	r8, r5, asl #1
	ldr	r3, [r7, #2256]
	add	r6, sl, r8
	add	r6, r6, #3920
	ldrh	r2, [r3, r8]
	ldrh	r3, [r6, #4]
	add	r6, r6, #4
	cmp	r2, r3
	beq	.L712
	ldr	r0, .L720+20
	mov	r1, r5
	bl	printk
	ldr	r3, [r7, #2256]
	ldrh	r3, [r3, r8]
	cmp	r3, r9
	beq	.L712
	ldrh	r2, [r6, #0]
	cmp	r2, r3
	movhi	r4, #1
.L712:
	add	r5, r5, #1
	uxth	r5, r5
.L711:
	ldrh	r2, [r7, #12]
	ldr	r3, .L720+12
	cmp	r2, r5
	bhi	.L713
	ldr	r2, [r3, #2268]
	cmp	r2, #0
	beq	.L714
	ldr	r5, [r3, #2248]
	mov	r1, #2272
	ldrh	r8, [r3, r1]
	mov	r6, #0
	rsb	r2, r5, r2
	ldr	r5, .L720+24
	mov	r7, r3
	ldr	sl, .L720+16
	mov	r2, r2, asr #1
	movw	r9, #65535
	mul	r5, r5, r2
	uxth	r5, r5
	b	.L715
.L717:
	ldr	r2, [r7, #2256]
	mov	r3, r5, asl #1
	ldrh	r2, [r2, r3]
	cmp	r2, #0
	beq	.L716
	add	r3, sl, r3
	ldr	r0, .L720+28
	add	r3, r3, #3920
	mov	r1, r5
	add	r3, r3, #4
	mov	r4, #1
	ldrh	r3, [r3, #0]
	bl	printk
.L716:
	mov	r2, #6
	ldr	r3, [r7, #2248]
	mul	r5, r2, r5
	ldrh	r5, [r3, r5]
	cmp	r5, r9
	beq	.L714
	add	r6, r6, #1
	uxth	r6, r6
.L715:
	cmp	r6, r8
	bne	.L717
.L714:
	cmp	r4, #0
	beq	.L707
	ldr	r0, .L720+32
	movw	r2, #2299
	ldr	r1, .L720
	bl	printk
.L707:
	ldmfd	sp!, {r2, r3, r4, r5, r6, r7, r8, r9, sl, pc}
.L721:
	.align	2
.L720:
	.word	.LANCHOR1+321
	.word	.LC87
	.word	.LANCHOR3+3924
	.word	.LANCHOR0
	.word	.LANCHOR3
	.word	.LC88
	.word	-1431655765
	.word	.LC89
	.word	.LC1
	.fnend
	.size	ftl_check_vpc, .-ftl_check_vpc
	.align	2
	.global	FtlFreeSysBlkQueueInit
	.type	FtlFreeSysBlkQueueInit, %function
FtlFreeSysBlkQueueInit:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	ldr	r3, .L723
	mov	r2, #2048
	stmfd	sp!, {r4, lr}
	.save {r4, lr}
	mov	r4, #0
	strh	r0, [r3, #184]	@ movhi
	mov	r1, r4
	strh	r4, [r3, #186]	@ movhi
	add	r0, r3, #192
	strh	r4, [r3, #188]	@ movhi
	strh	r4, [r3, #190]	@ movhi
	bl	ftl_memset
	mov	r0, r4
	ldmfd	sp!, {r4, pc}
.L724:
	.align	2
.L723:
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
	ldr	r0, .L726
	mvn	r3, #0
	mov	r1, #255
	mov	r2, #16
	strh	r3, [r0, #124]	@ movhi
	mov	r3, #0
	strh	r3, [r0, #130]	@ movhi
	add	r0, r0, #136
	b	ftl_memset
.L727:
	.align	2
.L726:
	.word	.LANCHOR0
	.fnend
	.size	FtlBbtMemInit, .-FtlBbtMemInit
	.align	2
	.global	FtlLoadBbt
	.type	FtlLoadBbt, %function
FtlLoadBbt:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	stmfd	sp!, {r4, r5, r6, r7, r8, lr}
	.save {r4, r5, r6, r7, r8, lr}
	ldr	r6, .L747
	ldr	r8, .L747+4
	ldr	r4, [r6, #-836]
	mov	r7, r6
	ldr	r3, [r8, #2240]
	str	r4, [r6, #-844]
	str	r3, [r6, #-848]
	bl	FtlBbtMemInit
	ldrh	r5, [r8, #68]
	sub	r5, r5, #1
	uxth	r5, r5
	b	.L729
.L733:
	mov	r1, #1
	mov	r3, r5, asl #10
	ldr	r0, .L747+8
	mov	r2, r1
	str	r3, [r6, #-852]
	bl	FlashReadPages
	ldr	r3, [r6, #-856]
	cmn	r3, #1
	bne	.L730
	ldr	r3, [r6, #-852]
	mov	r1, #1
	ldr	r0, .L747+8
	mov	r2, r1
	add	r3, r3, #1
	str	r3, [r6, #-852]
	bl	FlashReadPages
.L730:
	ldr	r3, [r7, #-856]
	cmn	r3, #1
	beq	.L731
	ldrh	r2, [r4, #0]
	movw	r3, #61649
	cmp	r2, r3
	bne	.L731
	ldr	r3, .L747+4
	ldr	r2, [r4, #4]
	strh	r5, [r3, #124]	@ movhi
	str	r2, [r3, #132]
	ldrh	r2, [r4, #8]
	strh	r2, [r3, #128]	@ movhi
	b	.L732
.L731:
	sub	r5, r5, #1
	uxth	r5, r5
.L729:
	ldrh	r3, [r8, #68]
	sub	r3, r3, #16
	cmp	r5, r3
	bgt	.L733
.L732:
	ldr	r5, .L747+4
	movw	r3, #65535
	ldrh	r2, [r5, #124]
	cmp	r2, r3
	beq	.L744
	ldrh	r2, [r5, #128]
	cmp	r2, r3
	beq	.L735
	ldr	r6, .L747
	mov	r1, #1
	mov	r2, r2, asl #10
	sub	r0, r6, #856
	str	r2, [r6, #-852]
	mov	r2, r1
	bl	FlashReadPages
	ldr	r3, [r6, #-856]
	cmn	r3, #1
	beq	.L735
	ldrh	r2, [r4, #0]
	movw	r3, #61649
	cmp	r2, r3
	bne	.L735
	ldr	r3, [r4, #4]
	ldr	r2, [r5, #132]
	cmp	r3, r2
	strhi	r3, [r5, #132]
	ldrhih	r2, [r5, #128]
	ldrhih	r3, [r4, #8]
	strhih	r2, [r5, #124]	@ movhi
	strhih	r3, [r5, #128]	@ movhi
.L735:
	ldr	r7, .L747+4
	mov	r1, #1
	mov	r6, #0
	ldr	r5, .L747
	ldrh	r0, [r7, #124]
	bl	FtlGetLastWrittenPage
	add	r3, r0, #1
	mov	r8, r0
	strh	r3, [r7, #126]	@ movhi
	b	.L736
.L739:
	ldrh	r2, [r7, #124]
	mov	r1, #1
	ldr	r0, .L747+8
	orr	r3, r3, r2, asl #10
	str	r3, [r5, #-852]
	ldr	r3, [r7, #2240]
	mov	r2, r1
	str	r3, [r5, #-848]
	bl	FlashReadPages
	ldr	r3, [r5, #-856]
	cmn	r3, #1
	beq	.L737
	ldrh	r2, [r4, #0]
	movw	r3, #61649
	cmp	r2, r3
	beq	.L738
.L737:
	sub	r6, r6, #1
	uxth	r6, r6
.L736:
	add	r3, r6, r8
	sxth	r3, r3
	cmp	r3, #0
	bge	.L739
	b	.L746
.L738:
	ldrh	r2, [r4, #10]
	ldr	r3, .L747+4
	ldrh	r0, [r4, #12]
	strh	r2, [r3, #130]	@ movhi
	movw	r2, #65535
	cmp	r0, r2
	beq	.L741
	ldr	r2, [r3, #0]
	cmp	r0, r2
	beq	.L741
	ldrh	r3, [r3, #14]
	mov	r3, r3, lsr #2
	cmp	r2, r3
	bcs	.L741
	cmp	r0, r3
	bcs	.L741
	bl	FtlSysBlkNumInit
.L741:
	ldr	r5, .L747+12
	mov	r4, #0
	ldr	r7, .L747+16
	movw	r6, #3940
	sub	r8, r5, #148
	b	.L742
.L743:
	ldr	r3, .L747
	ldrh	r2, [r7, r6]
	ldr	r0, [r5, #4]!
	ldr	r1, [r3, #-848]
	mov	r2, r2, asl #2
	mla	r1, r4, r2, r1
	bl	memcpy
	add	r4, r4, #1
.L742:
	ldrh	r3, [r8, #26]
	cmp	r4, r3
	bcc	.L743
	mov	r0, #0
	ldmfd	sp!, {r4, r5, r6, r7, r8, pc}
.L744:
	mvn	r0, #0
	ldmfd	sp!, {r4, r5, r6, r7, r8, pc}
.L746:
	ldr	r1, .L747+20
	mov	r2, #254
	ldr	r0, .L747+24
	bl	printk
	b	.L738
.L748:
	.align	2
.L747:
	.word	.LANCHOR3
	.word	.LANCHOR0
	.word	.LANCHOR3-856
	.word	.LANCHOR0+148
	.word	.LANCHOR4
	.word	.LANCHOR1+335
	.word	.LC1
	.fnend
	.size	FtlLoadBbt, .-FtlLoadBbt
	.align	2
	.global	FtlBbt2Bitmap
	.type	FtlBbt2Bitmap, %function
FtlBbt2Bitmap:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	ldr	r2, .L754
	stmfd	sp!, {r3, r4, r5, r6, r7, r8, sl, lr}
	.save {r3, r4, r5, r6, r7, r8, sl, lr}
	movw	r3, #3940
	ldrh	r2, [r2, r3]
	mov	r6, r0
	mov	r5, r1
	mov	r0, r1
	mov	r1, #0
	ldr	r8, .L754+4
	mov	r2, r2, asl #2
	mov	r4, #0
	bl	ftl_memset
	movw	sl, #65535
	mov	r7, #1
.L752:
	ldrh	r3, [r6, r4]
	cmp	r3, sl
	ldmeqfd	sp!, {r3, r4, r5, r6, r7, r8, sl, pc}
	ldrh	r2, [r8, #68]
	cmp	r2, r3
	bhi	.L751
	ldr	r0, .L754+8
	mov	r2, #74
	ldr	r1, .L754+12
	bl	printk
.L751:
	ldrh	r2, [r6, r4]
	add	r4, r4, #2
	cmp	r4, #1024
	mov	r3, r2, lsr #5
	and	r2, r2, #31
	ldr	r1, [r5, r3, asl #2]
	orr	r2, r1, r7, asl r2
	str	r2, [r5, r3, asl #2]
	bne	.L752
	ldmfd	sp!, {r3, r4, r5, r6, r7, r8, sl, pc}
.L755:
	.align	2
.L754:
	.word	.LANCHOR4
	.word	.LANCHOR0
	.word	.LC1
	.word	.LANCHOR1+346
	.fnend
	.size	FtlBbt2Bitmap, .-FtlBbt2Bitmap
	.align	2
	.global	FtlVariablesInit
	.type	FtlVariablesInit, %function
FtlVariablesInit:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	stmfd	sp!, {r4, r5, r6, lr}
	.save {r4, r5, r6, lr}
	movw	r2, #3948
	ldr	r6, .L757
	mvn	r3, #0
	ldr	r5, .L757+4
	mov	r4, #0
	mov	r1, r4
	strh	r3, [r6, r2]	@ movhi
	ldr	r2, .L757+8
	ldr	r0, [r5, #120]
	str	r3, [r6, #3956]
	str	r4, [r2, #3800]
	ldrh	r2, [r5, #92]
	str	r4, [r6, #3944]
	str	r4, [r6, #3952]
	mov	r2, r2, asl #1
	str	r4, [r5, #2436]
	strh	r4, [r5, #116]	@ movhi
	bl	ftl_memset
	ldrh	r2, [r5, #14]
	mov	r1, r4
	ldr	r0, [r5, #2244]
	mov	r2, r2, asl #1
	bl	ftl_memset
	ldrh	r2, [r5, #14]
	mov	r1, r4
	ldr	r0, [r6, #3960]
	mov	r2, r2, asl #1
	bl	ftl_memset
	mov	r1, r4
	mov	r2, #48
	ldr	r0, .L757+12
	bl	ftl_memset
	mov	r1, r4
	mov	r2, #512
	add	r0, r5, #2624
	bl	ftl_memset
	bl	FtlGcBufInit
	bl	FtlL2PDataInit
	mov	r0, r4
	ldmfd	sp!, {r4, r5, r6, pc}
.L758:
	.align	2
.L757:
	.word	.LANCHOR4
	.word	.LANCHOR0
	.word	.LANCHOR3
	.word	.LANCHOR0+2508
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
	ldr	r5, .L790
	movw	r2, #3190
	ldr	r4, .L790+4
	mov	r6, #0
	mov	r0, #256
	mov	r7, #12
	str	r3, [r5, #3972]
	mov	sl, #20
	ldr	r3, .L790+8
	ldrh	r1, [r4, #78]
	str	r6, [r4, #2620]
	str	r6, [r3, #3780]
	mvn	r3, #0
	strh	r3, [r4, r2]	@ movhi
	add	r2, r2, #2
	str	r6, [r4, #2476]
	strh	r3, [r4, r2]	@ movhi
	mov	r3, #3136
	mov	r2, #32
	strh	r2, [r4, r3]	@ movhi
	add	r3, r3, #2
	mov	r2, #128
	str	r6, [r4, #2480]
	strh	r2, [r4, r3]	@ movhi
	add	r3, r3, #6
	str	r6, [r4, #2464]
	strh	r6, [r4, r3]	@ movhi
	add	r3, r3, #50
	str	r6, [r4, #2452]
	strh	r6, [r4, r3]	@ movhi
	add	r3, r3, #36
	str	r6, [r4, #2448]
	strh	r6, [r4, r3]	@ movhi
	str	r6, [r4, #2456]
	str	r6, [r4, #2460]
	str	r6, [r4, #2444]
	str	r6, [r4, #2484]
	str	r6, [r4, #2488]
	str	r6, [r4, #2496]
	str	r6, [r4, #2500]
	str	r6, [r4, #2504]
	str	r6, [r4, #3148]
	str	r6, [r4, #3140]
	str	r6, [r5, #3964]
	str	r6, [r5, #3968]
	str	r6, [r5, #3976]
	bl	__aeabi_idiv
	ldrh	r3, [r4, #4]
	str	r6, [r5, #3984]
	ldr	r6, .L790+8
	mov	r3, r3, asl #2
	cmp	r0, r3
	str	r0, [r5, #3980]
	ldrh	r0, [r4, #76]
	strhi	r3, [r5, #3980]
	mov	r0, r0, asl #1
	bl	ftl_malloc
	str	r0, [r4, #3180]
	ldrh	r0, [r4, #76]
	mul	r0, r7, r0
	bl	ftl_malloc
	ldrh	r8, [r4, #4]
	mul	r8, sl, r8
	mov	r9, r8, asl #2
	str	r0, [r4, #3184]
	mov	r0, r9
	bl	ftl_malloc
	str	r0, [r6, #3796]
	mov	r0, r8
	bl	ftl_malloc
	str	r0, [r5, #3988]
	mov	r0, r9
	bl	ftl_malloc
	str	r0, [r5, #3992]
	mov	r0, r8
	bl	ftl_malloc
	str	r0, [r6, #3776]
	mov	r0, r8
	bl	ftl_malloc
	str	r0, [r4, #3168]
	ldr	r0, [r5, #3980]
	mul	r0, sl, r0
	bl	ftl_malloc
	ldrh	r3, [r4, #4]
	ldrh	r8, [r4, #82]
	mov	r3, r3, asl #1
	add	r3, r3, #1
	str	r3, [r4, #3172]
	str	r0, [r5, #3996]
	mov	r0, r8
	bl	ftl_malloc
	str	r0, [r4, #2240]
	mov	r0, r8
	bl	ftl_malloc
	str	r0, [r6, #3872]
	mov	r0, r8
	bl	ftl_malloc
	str	r0, [r5, #4000]
	ldr	r0, [r4, #3172]
	mul	r0, r0, r8
	bl	ftl_malloc
	str	r0, [r4, #3160]
	ldr	r0, [r5, #3980]
	mul	r0, r0, r8
	bl	ftl_malloc
	str	r0, [r5, #4004]
	mov	r0, r8
	bl	ftl_malloc
	str	r0, [r6, #3792]
	mov	r0, r8
	bl	ftl_malloc
	str	r0, [r6, #3784]
	ldr	r0, [r4, #3172]
	mul	r0, r7, r0
	bl	ftl_malloc
	ldrh	r3, [r4, #84]
	ldrh	r8, [r4, #4]
	mul	r8, r8, r3
	str	r0, [r4, #3156]
	mov	r0, r8
	bl	ftl_malloc
	str	r0, [r6, #-836]
	mov	r0, r8, asl #2
	bl	ftl_malloc
	ldrh	r3, [r4, #84]
	movw	r8, #4012
	str	r0, [r6, #3788]
	ldr	r0, [r4, #3172]
	mul	r0, r0, r3
	bl	ftl_malloc
	ldrh	r3, [r4, #84]
	str	r0, [r4, #3164]
	ldr	r0, [r5, #3980]
	mul	r0, r0, r3
	bl	ftl_malloc
	str	r0, [r5, #4008]
	ldrh	r0, [r4, #14]
	mov	r0, r0, asl #1
	uxth	r0, r0
	strh	r0, [r5, r8]	@ movhi
	bl	ftl_malloc
	str	r0, [r5, #3960]
	ldrh	r0, [r5, r8]
	add	r0, r0, #544
	add	r0, r0, #3
	mov	r0, r0, lsr #9
	strh	r0, [r5, r8]	@ movhi
	mov	r0, r0, asl #9
	bl	ftl_malloc
	ldrh	r8, [r4, #14]
	mov	r8, r8, asl #1
	str	r0, [r5, #4016]
	add	r0, r0, #32
	str	r0, [r4, #2244]
	mov	r0, r8
	bl	ftl_malloc
	str	r0, [r5, #4020]
	mov	r0, r8
	bl	ftl_malloc
	ldr	r8, [r4, #100]
	mov	r8, r8, asl #1
	str	r0, [r4, #2256]
	mov	r0, r8
	bl	ftl_malloc
	str	r0, [r6, #3856]
	mov	r0, r8
	bl	ftl_malloc
	str	r0, [r6, #3804]
	ldrh	r0, [r4, #14]
	mov	r0, r0, lsr #3
	add	r0, r0, #4
	bl	ftl_malloc
	str	r0, [r4, #2432]
	ldrh	r0, [r4, #92]
	mov	r0, r0, asl #1
	bl	ftl_malloc
	str	r0, [r4, #120]
	ldrh	r0, [r4, #92]
	mov	r0, r0, asl #1
	bl	ftl_malloc
	str	r0, [r6, #3916]
	ldrh	r0, [r4, #92]
	mov	r0, r0, asl #2
	bl	ftl_malloc
	str	r0, [r6, #3868]
	ldrh	r0, [r4, #94]
	mov	r0, r0, asl #2
	bl	ftl_malloc
	ldrh	r2, [r4, #94]
	mov	r1, #0
	mov	r2, r2, asl #2
	str	r0, [r6, #3920]
	bl	ftl_memset
	ldrh	r0, [r4, #108]
	mov	r0, r0, asl #2
	bl	ftl_malloc
	str	r0, [r6, #3864]
	ldr	r0, [r4, #100]
	mov	r0, r0, asl #2
	bl	ftl_malloc
	str	r0, [r6, #3860]
	ldrh	r0, [r4, #110]
	mul	r0, r7, r0
	bl	ftl_malloc
	ldrh	r3, [r4, #110]
	str	r0, [r4, #2420]
	ldrh	r0, [r4, #82]
	mul	r0, r0, r3
	bl	ftl_malloc
	ldrh	r3, [r4, #14]
	str	r0, [r6, #3808]
	mov	r0, #6
	mul	r0, r0, r3
	bl	ftl_malloc
	ldrh	r3, [r4, #68]
	ldrh	r2, [r4, #26]
	movw	r6, #3940
	add	r3, r3, #31
	mov	r3, r3, lsr #5
	strh	r3, [r5, r6]	@ movhi
	mul	r3, r2, r3
	str	r0, [r4, #2248]
	mov	r0, r3, asl #2
	bl	ftl_malloc
	ldrh	r1, [r5, r6]
	mov	r3, #1
	mov	r1, r1, asl #2
	mov	r2, r1
	str	r0, [r4, #152]
	ldr	r0, .L790+12
	ldrh	r4, [r4, #26]
	sub	ip, r0, #152
	b	.L761
.L762:
	ldr	r5, [ip, #152]
	add	r3, r3, #1
	add	r5, r5, r2
	add	r2, r2, r1
	str	r5, [r0, #4]!
.L761:
	cmp	r3, r4
	bcc	.L762
	ldr	r0, .L790+16
	mov	r2, #0
	mov	r1, r2
	add	r0, r0, r3, asl #2
	b	.L763
.L764:
	add	ip, r0, r2
	add	r3, r3, #1
	add	r2, r2, #4
	str	r1, [ip, #28]
.L763:
	cmp	r3, #7
	bls	.L764
	ldr	r3, .L790+8
	ldr	r2, [r3, #3856]
	cmp	r2, #0
	beq	.L789
.L765:
	ldr	r2, [r3, #3804]
	cmp	r2, #0
	beq	.L789
.L767:
	ldr	r2, [r3, #3864]
	cmp	r2, #0
	beq	.L789
.L768:
	ldr	r2, [r3, #3860]
	cmp	r2, #0
	beq	.L789
.L769:
	ldr	r2, .L790+4
	ldr	r1, [r2, #2420]
	cmp	r1, #0
	beq	.L789
.L770:
	ldr	r3, [r3, #3808]
	cmp	r3, #0
	beq	.L789
.L771:
	ldr	r3, [r2, #2248]
	cmp	r3, #0
	beq	.L789
.L772:
	ldr	r3, [r2, #152]
	cmp	r3, #0
	beq	.L789
.L773:
	ldr	r3, [r2, #2256]
	cmp	r3, #0
	beq	.L789
.L774:
	ldr	r3, .L790+4
	ldr	r2, [r3, #3180]
	cmp	r2, #0
	beq	.L789
	ldr	r2, [r3, #3184]
	cmp	r2, #0
	beq	.L789
.L776:
	ldr	r1, .L790+8
	ldr	r2, [r1, #3796]
	cmp	r2, #0
	beq	.L789
	ldr	r2, .L790
	ldr	r0, [r2, #3992]
	cmp	r0, #0
	beq	.L789
	ldr	r0, [r1, #3776]
	cmp	r0, #0
	beq	.L789
	ldr	r0, [r3, #3168]
	cmp	r0, #0
	beq	.L789
	ldr	r0, [r2, #3988]
	cmp	r0, #0
	beq	.L789
.L778:
	ldr	r3, [r3, #2240]
	cmp	r3, #0
	beq	.L789
	ldr	r3, [r1, #3872]
	cmp	r3, #0
	beq	.L789
	ldr	r3, [r2, #4000]
	cmp	r3, #0
	beq	.L789
	ldr	r3, .L790+4
	ldr	r2, [r3, #3160]
	cmp	r2, #0
	beq	.L789
	ldr	r2, .L790+8
	ldr	r1, [r2, #3792]
	cmp	r1, #0
	beq	.L789
	ldr	r1, [r2, #3784]
	cmp	r1, #0
	beq	.L789
	ldr	r1, [r3, #3156]
	cmp	r1, #0
	beq	.L789
.L780:
	ldr	r1, [r2, #-836]
	cmp	r1, #0
	beq	.L789
	ldr	r2, [r2, #3788]
	cmp	r2, #0
	beq	.L789
	ldr	r2, [r3, #3164]
	cmp	r2, #0
	beq	.L789
.L782:
	ldr	r2, [r3, #2244]
	cmp	r2, #0
	beq	.L789
	ldr	r2, .L790
	ldr	r2, [r2, #3960]
	cmp	r2, #0
	beq	.L789
.L784:
	ldr	r3, [r3, #120]
	cmp	r3, #0
	beq	.L789
.L785:
	ldr	r3, .L790+8
	ldr	r2, [r3, #3916]
	cmp	r2, #0
	beq	.L789
.L786:
	ldr	r2, [r3, #3868]
	cmp	r2, #0
	beq	.L789
.L787:
	ldr	r3, [r3, #3920]
	cmp	r3, #0
	bne	.L788
.L789:
	ldr	r0, .L790+20
	ldr	r1, .L790+24
	bl	printk
	mvn	r0, #0
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, sl, pc}
.L788:
	mov	r0, #0
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, sl, pc}
.L791:
	.align	2
.L790:
	.word	.LANCHOR4
	.word	.LANCHOR0
	.word	.LANCHOR3
	.word	.LANCHOR0+152
	.word	.LANCHOR0+124
	.word	.LC90
	.word	.LANCHOR1+360
	.fnend
	.size	FtlMemInit, .-FtlMemInit
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
	ldr	r4, .L812
	ldr	r5, .L812+4
	ldr	r3, [r4, #2240]
	ldrh	r2, [r4, #12]
	ldr	r0, [r4, #2256]
	str	r3, [r5, #-848]
	ldr	r3, [r5, #-836]
	mov	r2, r2, asl #1
	str	r3, [r5, #-844]
	bl	ftl_memset
	mov	r3, #2560
	ldrh	r0, [r4, r3]
	movw	r3, #65535
	cmp	r0, r3
	beq	.L808
	mov	r1, #1
	mov	r6, #0
	bl	FtlGetLastWrittenPage
	movw	r3, #2562
	add	r2, r0, #1
	mov	r7, r0
	strh	r2, [r4, r3]	@ movhi
	b	.L794
.L797:
	mov	r2, #2560
	mov	r1, #1
	ldrh	r2, [r4, r2]
	ldr	r0, .L812+8
	orr	r3, r3, r2, asl #10
	str	r3, [r5, #-852]
	ldr	r3, [r4, #2240]
	mov	r2, r1
	str	r3, [r5, #-848]
	bl	FlashReadPages
	ldr	r3, [r5, #-856]
	cmn	r3, #1
	beq	.L795
	ldr	r3, [r4, #2240]
	ldr	r2, [r3, #0]
	ldr	r3, .L812+12
	cmp	r2, r3
	bne	.L795
	ldr	r3, [r5, #-836]
	ldrh	r2, [r3, #0]
	movw	r3, #61604
	cmp	r2, r3
	beq	.L796
.L795:
	sub	r6, r6, #1
	uxth	r6, r6
.L794:
	add	r3, r6, r7
	sxth	r3, r3
	cmp	r3, #0
	bge	.L797
	b	.L811
.L796:
	ldr	r5, .L812
	ldrh	r2, [r5, #12]
	ldrh	r3, [r5, #82]
	add	r2, r2, #24
	cmp	r3, r2, asl #1
	bcs	.L799
	ldr	r0, .L812+16
	movw	r2, #1391
	ldr	r1, .L812+20
	bl	printk
.L799:
	ldr	r6, .L812+4
	mov	r2, #48
	ldr	r0, .L812+24
	ldr	r4, .L812
	ldr	r7, [r6, #-848]
	mov	r1, r7
	bl	memcpy
	ldrh	r2, [r5, #12]
	add	r1, r7, #48
	ldr	r0, [r5, #2256]
	mov	r2, r2, asl #1
	bl	memcpy
	ldrh	r1, [r5, #12]
	ldr	r3, [r6, #-848]
	ldr	r0, [r5, #2432]
	mov	r2, r1, lsr #3
	add	r1, r1, #24
	add	r2, r2, #4
	mov	r1, r1, lsr #1
	add	r1, r3, r1, asl #2
	bl	memcpy
	ldr	r2, [r5, #2508]
	ldr	r3, .L812+12
	cmp	r2, r3
	bne	.L808
	movw	r3, #2516
	ldrb	r2, [r4, #2518]	@ zero_extendqisi2
	ldrh	r5, [r4, r3]
	add	r3, r3, #50
	strh	r5, [r4, r3]	@ movhi
	ldrh	r3, [r4, #26]
	cmp	r2, r3
	bne	.L808
	ldr	r3, .L812+28
	ldrh	r2, [r4, #78]
	ldr	r6, [r4, #16]
	str	r5, [r3, #4024]
	ldrh	r3, [r4, #72]
	ldrh	r0, [r4, #130]
	ldrh	r1, [r4, #4]
	mul	r3, r5, r3
	rsb	r0, r0, r6
	rsb	r0, r5, r0
	str	r3, [r4, #2440]
	mul	r3, r2, r3
	str	r3, [r4, #112]
	bl	__aeabi_uidiv
	cmp	r5, r6
	movw	r3, #2556
	strh	r0, [r4, r3]	@ movhi
	bls	.L800
	ldr	r0, .L812+16
	movw	r2, #1413
	ldr	r1, .L812+20
	bl	printk
.L800:
	ldr	r3, .L812
	movw	r2, #2522
	movw	r0, #2278
	movw	r5, #65535
	ldrh	r1, [r3, r2]
	movw	r2, #2276
	strh	r1, [r3, r2]	@ movhi
	add	r2, r2, #248
	ldrh	r2, [r3, r2]
	mov	ip, r2, lsr #6
	and	r2, r2, #63
	strb	r2, [r3, #2282]
	ldrb	r2, [r3, #2519]	@ zero_extendqisi2
	strh	ip, [r3, r0]	@ movhi
	mvn	r0, #0
	movw	ip, #2574
	strb	r2, [r3, #2284]
	movw	r2, #2572
	strh	r0, [r3, r2]	@ movhi
	movw	r0, #2526
	mov	r2, #0
	strh	r2, [r3, ip]	@ movhi
	ldrh	ip, [r3, r0]
	movw	r0, #2324
	strb	r2, [r3, #2578]
	strb	r2, [r3, #2580]
	strh	ip, [r3, r0]	@ movhi
	mov	r0, #2528
	ldrh	r0, [r3, r0]
	movw	ip, #2326
	str	r2, [r3, #2464]
	str	r2, [r3, #2452]
	mov	r4, r0, lsr #6
	and	r0, r0, #63
	strb	r0, [r3, #2330]
	ldrb	r0, [r3, #2520]	@ zero_extendqisi2
	strh	r4, [r3, ip]	@ movhi
	str	r2, [r3, #2444]
	strb	r0, [r3, #2332]
	movw	r0, #2530
	ldrh	ip, [r3, r0]
	movw	r0, #2372
	str	r2, [r3, #2460]
	str	r2, [r3, #2488]
	strh	ip, [r3, r0]	@ movhi
	add	r0, r0, #160
	movw	ip, #2374
	str	r2, [r3, #2500]
	ldrh	r0, [r3, r0]
	mov	r4, r0, lsr #6
	and	r0, r0, #63
	strb	r0, [r3, #2378]
	ldrb	r0, [r3, #2521]	@ zero_extendqisi2
	strh	r4, [r3, ip]	@ movhi
	strb	r0, [r3, #2380]
	ldr	r0, [r3, #2540]
	str	r0, [r3, #2484]
	str	r2, [r3, #2456]
	ldr	r2, [r3, #2548]
	ldr	r0, [r3, #2476]
	cmp	r2, r0
	strhi	r2, [r3, #2476]
	ldr	r2, [r3, #2544]
	ldr	r3, [r3, #2480]
	cmp	r2, r3
	ldrhi	r3, .L812
	strhi	r2, [r3, #2480]
	cmp	r1, r5
	beq	.L803
	ldr	r0, .L812+32
	bl	make_superblock
.L803:
	ldr	r4, .L812
	movw	r3, #2324
	ldrh	r3, [r4, r3]
	cmp	r3, r5
	beq	.L804
	ldr	r0, .L812+36
	bl	make_superblock
.L804:
	movw	r3, #2372
	ldrh	r3, [r4, r3]
	movw	r4, #65535
	cmp	r3, r4
	beq	.L805
	ldr	r0, .L812+40
	bl	make_superblock
.L805:
	ldr	r2, .L812
	movw	r3, #2572
	ldrh	r3, [r2, r3]
	cmp	r3, r4
	beq	.L809
	ldr	r0, .L812+44
	bl	make_superblock
	mov	r0, #0
	ldmfd	sp!, {r3, r4, r5, r6, r7, pc}
.L808:
	mvn	r0, #0
	ldmfd	sp!, {r3, r4, r5, r6, r7, pc}
.L809:
	mov	r0, #0
	ldmfd	sp!, {r3, r4, r5, r6, r7, pc}
.L811:
	ldr	r1, .L812+20
	movw	r2, #1389
	ldr	r0, .L812+16
	bl	printk
	b	.L796
.L813:
	.align	2
.L812:
	.word	.LANCHOR0
	.word	.LANCHOR3
	.word	.LANCHOR3-856
	.word	1179929683
	.word	.LC1
	.word	.LANCHOR1+371
	.word	.LANCHOR0+2508
	.word	.LANCHOR4
	.word	.LANCHOR0+2276
	.word	.LANCHOR0+2324
	.word	.LANCHOR0+2372
	.word	.LANCHOR0+2572
	.fnend
	.size	FtlLoadSysInfo, .-FtlLoadSysInfo
	.align	2
	.global	FtlVpcTblFlush
	.type	FtlVpcTblFlush, %function
FtlVpcTblFlush:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	stmfd	sp!, {r3, r4, r5, r6, r7, r8, r9, sl, fp, lr}
	.save {r3, r4, r5, r6, r7, r8, r9, sl, fp, lr}
	mov	r7, #0
	ldr	r4, .L826
	mov	r1, #255
	ldr	r6, .L826+4
	movw	r9, #65535
	ldr	r3, [r4, #2240]
	mov	fp, r4
	ldr	r5, [r6, #-836]
	str	r3, [r6, #-848]
	mov	r3, #2560
	ldrh	r3, [r4, r3]
	str	r5, [r6, #-844]
	str	r7, [r5, #12]
	strh	r3, [r5, #2]	@ movhi
	ldr	r3, .L826+8
	strh	r3, [r5, #0]	@ movhi
	ldr	r3, [r4, #2568]
	str	r7, [r5, #8]
	str	r3, [r5, #4]
	ldr	r3, .L826+12
	str	r3, [r4, #2508]
	ldr	r3, .L826+16
	str	r3, [r4, #2512]
	movw	r3, #2566
	ldrh	r2, [r4, r3]
	movw	r3, #2516
	strh	r2, [r4, r3]	@ movhi
	ldrh	r3, [r4, #26]
	strb	r3, [r4, #2518]
	movw	r3, #2276
	ldrh	r2, [r4, r3]
	add	r3, r3, #246
	strh	r2, [r4, r3]	@ movhi
	movw	r3, #2278
	ldrh	r2, [r4, r3]
	ldrb	r3, [r4, #2282]	@ zero_extendqisi2
	orr	r2, r3, r2, asl #6
	movw	r3, #2524
	strh	r2, [r4, r3]	@ movhi
	ldrb	r3, [r4, #2284]	@ zero_extendqisi2
	strb	r3, [r4, #2519]
	movw	r3, #2324
	ldrh	r2, [r4, r3]
	add	r3, r3, #202
	strh	r2, [r4, r3]	@ movhi
	movw	r3, #2326
	ldrh	r2, [r4, r3]
	ldrb	r3, [r4, #2330]	@ zero_extendqisi2
	orr	r2, r3, r2, asl #6
	mov	r3, #2528
	strh	r2, [r4, r3]	@ movhi
	ldrb	r3, [r4, #2332]	@ zero_extendqisi2
	strb	r3, [r4, #2520]
	movw	r3, #2372
	ldrh	r2, [r4, r3]
	add	r3, r3, #158
	strh	r2, [r4, r3]	@ movhi
	movw	r3, #2374
	ldrh	r2, [r4, r3]
	ldrb	r3, [r4, #2378]	@ zero_extendqisi2
	ldr	r0, [r6, #-848]
	orr	r2, r3, r2, asl #6
	movw	r3, #2532
	strh	r2, [r4, r3]	@ movhi
	ldrb	r3, [r4, #2380]	@ zero_extendqisi2
	ldrh	r2, [r4, #82]
	strb	r3, [r4, #2521]
	ldr	r3, [r4, #2484]
	str	r3, [r4, #2540]
	ldr	r3, [r4, #2476]
	str	r3, [r4, #2548]
	ldr	r3, [r4, #2480]
	str	r3, [r4, #2544]
	bl	ftl_memset
	ldr	r1, .L826+20
	mov	r2, #48
	ldr	r0, [r6, #-848]
	bl	memcpy
	ldrh	r2, [r4, #12]
	ldr	r0, [r6, #-848]
	ldr	r1, [r4, #2256]
	mov	r2, r2, asl #1
	add	r0, r0, #48
	bl	memcpy
	ldrh	r2, [r4, #12]
	ldr	r3, [r6, #-848]
	add	r0, r2, #24
	ldr	r1, [r4, #2432]
	mov	r2, r2, lsr #3
	mov	r0, r0, lsr #1
	add	r2, r2, #4
	add	r0, r3, r0, asl #2
	bl	memcpy
	mov	r0, r7
	bl	FtlUpdateVaildLpn
.L825:
	ldr	r3, [r4, #2240]
	mov	sl, #2560
	ldrh	r2, [r4, sl]
	movw	r8, #2562
	mov	r1, #1
	ldr	r0, .L826+24
	str	r3, [r6, #-848]
	ldr	r3, [r6, #-836]
	str	r3, [r6, #-844]
	ldrh	r3, [r4, r8]
	orr	r3, r3, r2, asl #10
	mov	r2, r1
	str	r3, [r6, #-852]
	mov	r3, r1
	bl	FlashProgPages
	ldrh	r3, [r4, #74]
	ldrh	r2, [r4, r8]
	sub	r3, r3, #1
	cmp	r2, r3
	blt	.L816
	ldrh	r2, [r4, sl]
	movw	r3, #2564
	ldrh	r9, [r4, r3]
	strh	r2, [r4, r3]	@ movhi
	mov	r3, #0
	strh	r3, [r4, r8]	@ movhi
	bl	FtlFreeSysBlkQueueOut
	ldr	r3, [r4, #2476]
	mov	r1, #1
	add	r2, r3, #1
	str	r2, [r4, #2476]
	str	r3, [r4, #2568]
	mov	r2, r0, asl #10
	strh	r0, [r4, sl]	@ movhi
	str	r2, [r6, #-852]
	mov	r2, r1
	str	r3, [r5, #4]
	mov	r3, r1
	strh	r0, [r5, #2]	@ movhi
	ldr	r0, .L826+24
	bl	FlashProgPages
.L816:
	movw	r8, #2562
	ldr	r2, [r6, #-856]
	ldrh	r3, [r4, r8]
	cmn	r2, #1
	add	r3, r3, #1
	uxth	r3, r3
	strh	r3, [r4, r8]	@ movhi
	bne	.L817
	cmp	r3, #1
	bne	.L818
	ldr	r0, .L826+28
	movw	r2, #1076
	ldr	r1, .L826+32
	bl	printk
.L818:
	ldrh	r3, [fp, r8]
	add	r7, r7, #1
	cmp	r3, #1
	uxth	r7, r7
	ldreqh	r2, [r4, #74]
	movweq	r3, #2562
	subeq	r2, r2, #1
	streqh	r2, [r4, r3]	@ movhi
	cmp	r7, #3
	bls	.L825
	ldr	r3, .L826+4
	mov	r2, r7
	ldr	r0, .L826+36
	ldr	r1, [r3, #-852]
	bl	printk
.L821:
	b	.L821
.L817:
	cmp	r3, #1
	beq	.L825
	movw	r3, #65535
	cmp	r9, r3
	beq	.L822
	mov	r0, r9
	mov	r1, #1
	bl	FtlFreeSysBlkQueueIn
.L822:
	mov	r0, #0
	ldmfd	sp!, {r3, r4, r5, r6, r7, r8, r9, sl, fp, pc}
.L827:
	.align	2
.L826:
	.word	.LANCHOR0
	.word	.LANCHOR3
	.word	-3932
	.word	1179929683
	.word	1342177344
	.word	.LANCHOR0+2508
	.word	.LANCHOR3-856
	.word	.LC1
	.word	.LANCHOR1+386
	.word	.LC91
	.fnend
	.size	FtlVpcTblFlush, .-FtlVpcTblFlush
	.align	2
	.global	FtlVendorPartRead
	.type	FtlVendorPartRead, %function
FtlVendorPartRead:
	.fnstart
	@ args = 0, pretend = 0, frame = 96
	@ frame_needed = 0, uses_anonymous_args = 0
	ldr	r3, .L838
	stmfd	sp!, {r4, r5, r6, r7, r8, r9, sl, fp, lr}
	.save {r4, r5, r6, r7, r8, r9, sl, fp, lr}
	mov	r8, r2
	ldrh	sl, [r3, #80]
	add	r2, r1, r0
	ldrh	r3, [r3, #66]
	.pad #100
	sub	sp, sp, #100
	mov	r6, r0
	mov	r5, r1
	cmp	r2, r3
	mvnhi	r7, #0
	bhi	.L829
	mov	sl, r0, lsr sl
	mov	r7, #0
	ldr	r9, .L838+4
	mov	sl, sl, asl #2
	b	.L830
.L835:
	ldr	r2, .L838+8
	mov	r0, r6
	ldr	r3, [r2, #3920]
	ldr	fp, [r3, sl]
	ldr	r3, .L838
	ldrh	r4, [r3, #78]
	mov	r1, r4
	bl	__aeabi_uidivmod
	uxth	r3, r1
	rsb	r4, r3, r4
	uxth	r4, r4
	cmp	r4, r5
	uxthhi	r4, r5
	cmp	fp, #0
	beq	.L832
	ldr	r2, [r9, #4000]
	mov	r1, #1
	add	r0, sp, #12
	str	r3, [sp, #4]
	str	fp, [sp, #16]
	str	r2, [sp, #20]
	add	r2, sp, #32
	str	r2, [sp, #24]
	mov	r2, r1
	bl	FlashReadPages
	ldr	r2, [sp, #12]
	ldr	r3, [sp, #4]
	mov	r0, r8
	cmn	r2, #1
	moveq	r7, r2
	ldr	r2, .L838+4
	ldr	r1, [r2, #4000]
	mov	r2, r4, asl #9
	add	r1, r1, r3, asl #9
	bl	memcpy
	b	.L834
.L832:
	mov	r0, r8
	mov	r1, fp
	mov	r2, r4, asl #9
	bl	ftl_memset
.L834:
	rsb	r5, r4, r5
	add	r6, r6, r4
	add	r8, r8, r4, asl #9
	add	sl, sl, #4
.L830:
	cmp	r5, #0
	bne	.L835
.L829:
	mov	r0, r7
	add	sp, sp, #100
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, sl, fp, pc}
.L839:
	.align	2
.L838:
	.word	.LANCHOR0
	.word	.LANCHOR4
	.word	.LANCHOR3
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
	ldr	r4, .L844
	mov	r1, #1
	ldr	r5, .L844+4
	add	r2, r4, #2624
	bl	FtlVendorPartRead
	ldr	r3, [r4, #2624]
	cmp	r3, r5
	beq	.L841
	add	r0, r4, #2624
	mov	r1, #0
	mov	r2, #512
	bl	ftl_memset
	str	r5, [r4, #2624]
.L841:
	ldr	r2, [r4, #2624]
	ldr	r3, .L844
	cmp	r2, r5
	bne	.L842
	ldr	r2, [r3, #2712]
	str	r2, [r3, #2468]
	ldr	r2, [r3, #2716]
	str	r2, [r3, #2472]
	ldr	r2, [r3, #2632]
	str	r2, [r3, #2464]
	ldr	r2, [r3, #2636]
	str	r2, [r3, #2452]
	ldr	r2, [r3, #2640]
	str	r2, [r3, #2444]
	ldr	r2, [r3, #2644]
	str	r2, [r3, #2460]
	ldr	r2, [r3, #2652]
	str	r2, [r3, #2488]
	ldr	r2, [r3, #2656]
	str	r2, [r3, #2496]
	ldr	r2, [r3, #2660]
	str	r2, [r3, #2448]
	ldr	r2, [r3, #2664]
	str	r2, [r3, #2456]
	ldr	r2, [r3, #2668]
	str	r2, [r3, #2500]
	ldr	r2, [r3, #2672]
	str	r2, [r3, #2504]
	ldr	r2, [r3, #2684]
	str	r2, [r3, #2620]
.L842:
	ldr	r3, .L844+8
	mov	r2, #0
	ldr	r4, .L844
	str	r2, [r3, #3964]
	ldr	r2, [r4, #2692]
	ldr	r3, .L844+12
	cmp	r2, r3
	bne	.L843
	ldr	r0, .L844+16
	mov	r3, #1
	ldr	r1, .L844+20
	str	r3, [r4, #2436]
	bl	printk
.L843:
	ldrh	r2, [r4, #62]
	ldr	r3, [r4, #2488]
	ldr	r0, [r4, #2484]
	ldrh	r1, [r4, #12]
	mla	r0, r0, r2, r3
	bl	__aeabi_uidiv
	str	r0, [r4, #2492]
	ldmfd	sp!, {r3, r4, r5, pc}
.L845:
	.align	2
.L844:
	.word	.LANCHOR0
	.word	1179929683
	.word	.LANCHOR4
	.word	305432421
	.word	.LC70
	.word	.LC92
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
	movw	r5, #4012
	ldr	r4, .L848
	mov	r0, #64
	ldr	r2, [r4, #4016]
	ldrh	r1, [r4, r5]
	bl	FtlVendorPartRead
	ldr	r3, [r4, #4016]
	ldr	r2, [r3, #0]
	ldr	r3, .L848+4
	cmp	r2, r3
	beq	.L847
	ldr	r1, .L848+8
	ldr	r0, .L848+12
	bl	printk
	ldrh	r2, [r4, r5]
	ldr	r0, [r4, #4016]
	mov	r1, #0
	mov	r2, r2, asl #9
	bl	ftl_memset
.L847:
	mov	r0, #0
	ldmfd	sp!, {r3, r4, r5, pc}
.L849:
	.align	2
.L848:
	.word	.LANCHOR4
	.word	1112818501
	.word	.LC93
	.word	.LC70
	.fnend
	.size	FtlLoadEctTbl, .-FtlLoadEctTbl
	.align	2
	.global	FtlVendorPartWrite
	.type	FtlVendorPartWrite, %function
FtlVendorPartWrite:
	.fnstart
	@ args = 0, pretend = 0, frame = 96
	@ frame_needed = 0, uses_anonymous_args = 0
	ldr	r3, .L860
	stmfd	sp!, {r4, r5, r6, r7, r8, r9, sl, fp, lr}
	.save {r4, r5, r6, r7, r8, r9, sl, fp, lr}
	mov	r9, r2
	ldrh	r2, [r3, #66]
	mov	r4, r1
	add	r1, r1, r0
	.pad #100
	sub	sp, sp, #100
	cmp	r1, r2
	mov	r6, r0
	ldrh	r8, [r3, #80]
	mvnhi	r7, #0
	bhi	.L851
	mov	r8, r0, lsr r8
	mov	r7, #0
	mov	sl, r7
	mov	fp, r8, asl #2
	b	.L852
.L857:
	ldr	r2, .L860+4
	mov	r0, r6
	ldr	r3, [r2, #3920]
	ldr	r2, .L860
	ldr	r3, [r3, fp]
	ldrh	r7, [r2, #78]
	str	r3, [sp, #0]
	mov	r1, r7
	bl	__aeabi_uidivmod
	ldr	r3, [sp, #0]
	ldr	r2, .L860+8
	uxth	r1, r1
	str	r1, [sp, #4]
	rsb	r5, r1, r7
	uxth	r5, r5
	cmp	r5, r4
	uxthhi	r5, r4
	cmp	r3, #0
	beq	.L854
	cmp	r5, r7
	beq	.L854
	str	r3, [sp, #16]
	mov	r1, #1
	ldr	r3, [r2, #4000]
	add	r0, sp, #12
	mov	r2, r1
	str	r3, [sp, #20]
	add	r3, sp, #32
	str	r3, [sp, #24]
	bl	FlashReadPages
	b	.L855
.L854:
	ldr	r3, .L860
	mov	r1, #0
	ldr	r0, [r2, #4000]
	ldrh	r2, [r3, #82]
	bl	ftl_memset
.L855:
	ldr	r7, .L860+8
	mov	r3, r5, asl #9
	ldr	r2, [sp, #4]
	mov	r1, r9
	rsb	r4, r5, r4
	add	r6, r6, r5
	ldr	r0, [r7, #4000]
	add	fp, fp, #4
	str	r3, [sp, #0]
	add	r0, r0, r2, asl #9
	mov	r2, r3
	bl	memcpy
	mov	r1, r8
	ldr	r0, .L860+12
	add	r8, r8, #1
	ldr	r2, [r7, #4000]
	bl	FtlMapWritePage
	ldr	r3, [sp, #0]
	add	r9, r9, r3
	cmn	r0, #1
	moveq	sl, r0
.L852:
	cmp	r4, #0
	bne	.L857
	mov	r7, sl
.L851:
	mov	r0, r7
	add	sp, sp, #100
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, sl, fp, pc}
.L861:
	.align	2
.L860:
	.word	.LANCHOR0
	.word	.LANCHOR3
	.word	.LANCHOR4
	.word	.LANCHOR3+3876
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
	ldr	r2, .L864
	ldr	r3, .L864+4
	ldr	r1, [r2, #2624]
	cmp	r1, r3
	bxne	lr
	ldr	r3, .L864+8
	mov	r0, #0
	mov	r1, #1
	str	r3, [r2, #2628]
	ldr	r3, [r2, #2468]
	str	r3, [r2, #2712]
	ldr	r3, [r2, #2472]
	str	r3, [r2, #2716]
	ldr	r3, [r2, #2464]
	str	r3, [r2, #2632]
	ldr	r3, [r2, #2452]
	str	r3, [r2, #2636]
	ldr	r3, [r2, #2444]
	str	r3, [r2, #2640]
	ldr	r3, [r2, #2460]
	str	r3, [r2, #2644]
	ldr	r3, [r2, #2488]
	str	r3, [r2, #2652]
	ldr	r3, [r2, #2496]
	str	r3, [r2, #2656]
	ldr	r3, [r2, #2448]
	str	r3, [r2, #2660]
	ldr	r3, [r2, #2456]
	str	r3, [r2, #2664]
	ldr	r3, [r2, #2500]
	str	r3, [r2, #2668]
	ldr	r3, [r2, #2504]
	str	r3, [r2, #2672]
	ldr	r3, [r2, #2620]
	str	r3, [r2, #2684]
	ldr	r3, .L864+12
	ldr	r3, [r3, #3964]
	str	r3, [r2, #2688]
	add	r2, r2, #2624
	b	FtlVendorPartWrite
.L865:
	.align	2
.L864:
	.word	.LANCHOR0
	.word	1179929683
	.word	1342177344
	.word	.LANCHOR4
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
	ldr	r3, .L873
	ldr	r2, [r3, #2436]
	cmp	r2, #0
	moveq	r2, #32
	beq	.L867
	ldr	r2, [r3, #2504]
	cmp	r2, #29
	movls	r2, #4
	movhi	r2, #32
.L867:
	ldr	r3, .L873+4
	movw	r1, #4028
	ldrh	ip, [r3, r1]
	cmp	ip, #31
	addls	ip, ip, #1
	strlsh	ip, [r3, r1]	@ movhi
	movls	r2, #1
	cmp	r0, #0
	bne	.L869
	ldr	r1, [r3, #4016]
	ldr	r0, [r1, #20]
	ldr	r1, [r1, #16]
	add	r2, r2, r0
	cmp	r1, r2
	bcc	.L870
.L869:
	ldr	r2, [r3, #4016]
	mov	r0, #64
	ldr	r1, [r2, #16]
	str	r1, [r2, #20]
	ldr	r1, .L873+8
	str	r1, [r2, #0]
	movw	r1, #4012
	ldr	r2, [r3, #4016]
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
.L870:
	mov	r0, #0
	ldmfd	sp!, {r3, pc}
.L874:
	.align	2
.L873:
	.word	.LANCHOR0
	.word	.LANCHOR4
	.word	1112818501
	.fnend
	.size	FtlEctTblFlush, .-FtlEctTblFlush
	.align	2
	.global	FtlBbmTblFlush
	.type	FtlBbmTblFlush, %function
FtlBbmTblFlush:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	stmfd	sp!, {r0, r1, r4, r5, r6, r7, r8, r9, sl, lr}
	.save {r0, r1, r4, r5, r6, r7, r8, r9, sl, lr}
	mov	r1, #0
	ldr	sl, .L887
	mov	r5, #0
	ldr	r8, .L887+4
	add	r7, sl, #148
	ldr	r9, .L887+8
	ldr	r0, [sl, #2240]
	ldr	r3, [r8, #-836]
	ldrh	r2, [sl, #82]
	str	r0, [r8, #-848]
	str	r3, [r8, #-844]
	bl	ftl_memset
	b	.L876
.L877:
	movw	r3, #3940
	ldr	r1, [r7, #4]!
	ldrh	r2, [r9, r3]
	ldr	r3, [r8, #-848]
	mul	r0, r2, r5
	mov	r2, r2, asl #2
	add	r5, r5, #1
	add	r0, r3, r0, asl #2
	bl	memcpy
.L876:
	ldrh	r3, [sl, #26]
	ldr	r4, .L887
	cmp	r5, r3
	ldr	r6, .L887+4
	blt	.L877
	ldr	r5, [r6, #-844]
	mov	r1, #255
	mov	r2, #16
	mov	r7, #0
	mov	r8, r7
	mov	sl, r7
	mov	r0, r5
	mov	r9, r7
	bl	ftl_memset
	ldr	r3, .L887+12
	strh	r3, [r5, #0]	@ movhi
	ldr	r3, [r4, #132]
	str	r3, [r5, #4]
	ldrh	r3, [r4, #124]
	strh	r3, [r5, #2]	@ movhi
	ldrh	r3, [r4, #128]
	strh	r3, [r5, #8]	@ movhi
	ldrh	r3, [r4, #130]
	strh	r3, [r5, #10]	@ movhi
	ldr	r3, [r4, #0]
	strh	r3, [r5, #12]	@ movhi
	b	.L886
.L883:
	mov	r8, #1
.L886:
	ldr	r4, .L887
	str	r9, [r6, #-856]
	ldr	r3, [r4, #2240]
	ldrh	r1, [r4, #124]
	ldrh	r2, [r4, #126]
	str	r3, [r6, #-848]
	ldr	r3, [r6, #-836]
	str	r3, [r6, #-844]
	orr	r3, r2, r1, asl #10
	ldrh	r0, [r5, #10]
	str	r3, [r6, #-852]
	ldrh	r3, [r4, #128]
	str	r0, [sp, #0]
	ldr	r0, .L887+16
	bl	printk
	mov	r1, #1
	mov	r2, r1
	mov	r3, r1
	ldr	r0, .L887+20
	bl	FlashProgPages
	ldrh	r3, [r4, #74]
	ldrh	r2, [r4, #126]
	sub	r3, r3, #1
	cmp	r2, r3
	blt	.L879
	ldr	r3, [r4, #132]
	mov	r1, #1
	ldrh	r2, [r4, #124]
	add	r3, r3, #1
	ldr	r0, [r6, #3776]
	str	r3, [r4, #132]
	str	r3, [r5, #4]
	ldrh	r3, [r4, #128]
	strh	r2, [r5, #8]	@ movhi
	strh	r2, [r4, #128]	@ movhi
	mov	r2, r1
	strh	r3, [r4, #124]	@ movhi
	mov	r3, r3, asl #10
	str	r3, [r6, #-852]
	str	r3, [r0, #4]
	strh	sl, [r4, #126]	@ movhi
	bl	FlashEraseBlocks
	mov	r1, #1
	ldr	r0, .L887+20
	mov	r2, r1
	mov	r3, r1
	bl	FlashProgPages
.L879:
	ldr	r3, .L887
	ldrh	r2, [r3, #126]
	add	r2, r2, #1
	strh	r2, [r3, #126]	@ movhi
	ldr	r3, [r6, #-856]
	cmn	r3, #1
	bne	.L880
	add	r7, r7, #1
	ldr	r0, .L887+24
	ldr	r1, [r6, #-852]
	uxth	r7, r7
	bl	printk
	cmp	r7, #3
	bls	.L886
	ldr	r3, .L887+4
	mov	r2, r7
	ldr	r0, .L887+28
	ldr	r1, [r3, #-852]
	bl	printk
.L882:
	b	.L882
.L880:
	cmp	r8, #0
	beq	.L883
	mov	r0, #0
	ldmfd	sp!, {r2, r3, r4, r5, r6, r7, r8, r9, sl, pc}
.L888:
	.align	2
.L887:
	.word	.LANCHOR0
	.word	.LANCHOR3
	.word	.LANCHOR4
	.word	-3887
	.word	.LC94
	.word	.LANCHOR3-856
	.word	.LC95
	.word	.LC96
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
	movw	r3, #3194
	ldr	r4, .L899
	mov	r9, r0
	ldrh	r3, [r4, r3]
	cmp	r3, #0
	movne	r6, #0
	movne	sl, r4
	bne	.L890
	b	.L891
.L897:
	add	r3, r4, r6
	mov	r1, r9
	mov	r5, #0
	movw	r7, #3194
	ldrb	r0, [r3, #30]	@ zero_extendqisi2
	bl	V2P_block
	mov	r8, r0
	b	.L892
.L896:
	add	r3, r4, r5, asl #1
	add	r3, r3, #3184
	add	r3, r3, #12
	ldrh	r3, [r3, #0]
	cmp	r3, r8
	bne	.L893
	mov	r1, r8
	ldr	r0, .L899+4
	bl	printk
	mov	r0, r8
	bl	FtlBbmMapBadBlock
	bl	FtlBbmTblFlush
	ldrh	r1, [r4, r7]
	mov	r3, r5
	b	.L894
.L895:
	add	r0, r3, #1
	add	r3, r4, r3, asl #1
	add	r3, r3, #3184
	add	r2, r4, r0, asl #1
	add	r2, r2, #3184
	add	r2, r2, #12
	ldrh	r2, [r2, #0]
	strh	r2, [r3, #12]	@ movhi
	uxth	r3, r0
.L894:
	cmp	r3, r1
	bcc	.L895
	sub	r1, r1, #1
	strh	r1, [sl, r7]	@ movhi
.L893:
	add	r5, r5, #1
	uxth	r5, r5
.L892:
	ldrh	r3, [r4, r7]
	cmp	r3, r5
	bhi	.L896
	add	r6, r6, #1
	uxth	r6, r6
.L890:
	ldrh	r3, [r4, #4]
	cmp	r3, r6
	bhi	.L897
	bl	FtlGcReFreshBadBlk
.L891:
	mov	r0, #0
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, sl, pc}
.L900:
	.align	2
.L899:
	.word	.LANCHOR0
	.word	.LC97
	.fnend
	.size	FtlGcFreeBadSuperBlk, .-FtlGcFreeBadSuperBlk
	.align	2
	.type	update_vpc_list.part.11, %function
update_vpc_list.part.11:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	stmfd	sp!, {r4, r5, r6, lr}
	.save {r4, r5, r6, lr}
	mov	r6, r0
	ldr	r4, .L904
	mov	r1, r6
	ldr	r0, .L904+4
	movw	r5, #2264
	bl	List_remove_node
	ldrh	r3, [r4, r5]
	cmp	r3, #0
	bne	.L902
	ldr	r0, .L904+8
	movw	r2, #2747
	ldr	r1, .L904+12
	bl	printk
.L902:
	ldrh	r3, [r4, r5]
	mov	r0, r6
	sub	r3, r3, #1
	strh	r3, [r4, r5]	@ movhi
	bl	free_data_superblock
	mov	r0, r6
	bl	FtlGcFreeBadSuperBlk
	mov	r3, #2272
	ldrh	r2, [r4, r3]
	ldrh	r3, [r4, r5]
	add	r2, r2, r3
	ldrh	r3, [r4, #12]
	cmp	r2, r3
	ble	.L903
	ldr	r0, .L904+8
	movw	r2, #2750
	ldr	r1, .L904+12
	bl	printk
.L903:
	mov	r0, #1
	ldmfd	sp!, {r4, r5, r6, pc}
.L905:
	.align	2
.L904:
	.word	.LANCHOR0
	.word	.LANCHOR0+2252
	.word	.LC1
	.word	.LANCHOR1+401
	.fnend
	.size	update_vpc_list.part.11, .-update_vpc_list.part.11
	.align	2
	.global	update_vpc_list
	.type	update_vpc_list, %function
update_vpc_list:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	ldr	r2, .L914
	mov	r1, r0, asl #1
	stmfd	sp!, {r3, lr}
	.save {r3, lr}
	mov	r3, r0
	ldr	ip, [r2, #2256]
	ldrh	r1, [ip, r1]
	cmp	r1, #0
	bne	.L907
	movw	r1, #2572
	ldrh	r0, [r2, r1]
	cmp	r0, r3
	mvneq	r0, #0
	streqh	r0, [r2, r1]	@ movhi
	beq	.L909
	movw	r1, #2276
	ldrh	r1, [r2, r1]
	cmp	r1, r3
	beq	.L910
	movw	r1, #2324
	ldrh	r1, [r2, r1]
	cmp	r1, r3
	beq	.L910
	movw	r1, #2372
	ldrh	r2, [r2, r1]
	cmp	r2, r3
	beq	.L910
.L909:
	mov	r0, r3
	ldmfd	sp!, {r3, lr}
	b	update_vpc_list.part.11
.L907:
	bl	List_update_data_list
.L910:
	mov	r0, #0
	ldmfd	sp!, {r3, pc}
.L915:
	.align	2
.L914:
	.word	.LANCHOR0
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
	mov	r7, r0
	beq	.L917
	ldr	r6, .L923
	mov	r5, r0, asl #1
	ldr	r3, [r6, #2256]
	ldrh	r4, [r3, r5]
	cmp	r4, #0
	subne	r4, r4, #1
	strneh	r4, [r3, r5]	@ movhi
	bne	.L917
	ldr	r0, .L923+4
	mov	r1, r7
	mov	r2, r4
	bl	printk
	ldr	r3, [r6, #2256]
	ldrh	r5, [r3, r5]
	cmp	r5, #0
	bne	.L919
	ldr	r0, .L923+8
	movw	r2, #2765
	ldr	r1, .L923+12
	mov	r4, r5
	bl	printk
	b	.L919
.L917:
	ldr	r6, .L923+16
	movw	r5, #3948
	movw	r3, #65535
	ldrh	r0, [r6, r5]
	cmp	r0, r3
	streqh	r7, [r6, r5]	@ movhi
	beq	.L922
	cmp	r0, r7
	beq	.L922
	bl	update_vpc_list
	ldr	r3, .L923
	strh	r7, [r6, r5]	@ movhi
	ldr	r1, [r3, #2252]
	ldr	r2, [r3, #2248]
	rsb	r2, r2, r1
	ldr	r1, .L923+20
	mov	r2, r2, asr #1
	mul	r2, r1, r2
	ldr	r1, [r3, #2256]
	uxth	r2, r2
	mov	r3, r2, asl #1
	ldrh	r3, [r1, r3]
	adds	r4, r0, #0
	movne	r4, #1
	cmp	r3, #0
	bne	.L919
	cmp	r7, r2
	beq	.L919
	ldr	r1, .L923+12
	movw	r2, #2781
	ldr	r0, .L923+8
	bl	printk
	b	.L919
.L922:
	mov	r4, #0
.L919:
	mov	r0, r4
	ldmfd	sp!, {r3, r4, r5, r6, r7, pc}
.L924:
	.align	2
.L923:
	.word	.LANCHOR0
	.word	.LC98
	.word	.LC1
	.word	.LANCHOR1+417
	.word	.LANCHOR4
	.word	-1431655765
	.fnend
	.size	decrement_vpc_count, .-decrement_vpc_count
	.align	2
	.global	FtlSuperblockPowerLostFix
	.type	FtlSuperblockPowerLostFix, %function
FtlSuperblockPowerLostFix:
	.fnstart
	@ args = 0, pretend = 0, frame = 24
	@ frame_needed = 0, uses_anonymous_args = 0
	stmfd	sp!, {r4, r5, r6, r7, lr}
	.save {r4, r5, r6, r7, lr}
	mov	r4, r0
	.pad #28
	sub	sp, sp, #28
	mov	r6, #7
	ldr	r5, .L930
	ldr	r7, .L930+4
	b	.L926
.L929:
	ldrh	r3, [r4, #4]
	cmp	r3, #0
	beq	.L927
	mov	r0, r4
	bl	get_new_active_ppa
	cmn	r0, #1
	str	r0, [sp, #8]
	beq	.L927
	ldr	r3, [r5, #2240]
	mvn	r2, #0
	str	r2, [sp, #20]
	add	r0, sp, #4
	str	r3, [sp, #12]
	ldr	r3, [r7, #-836]
	str	r3, [sp, #16]
	str	r2, [r3, #8]
	str	r2, [r3, #12]
	ldrh	r2, [r4, #0]
	strh	r2, [r3, #2]	@ movhi
	mov	r2, #0
	strh	r2, [r3, #0]	@ movhi
	ldr	r1, [r5, #2480]
	cmn	r1, #2
	str	r1, [r3, #4]
	addne	r1, r1, #1
	moveq	r1, #0
	mov	r3, r2
	str	r1, [r5, #2480]
	mov	r1, #1
	bl	FlashProgPages
	ldrh	r0, [r4, #0]
	bl	decrement_vpc_count
.L926:
	subs	r6, r6, #1
	bne	.L929
.L927:
	ldr	r3, .L930
	ldrh	r2, [r4, #0]
	ldrh	r0, [r4, #4]
	ldr	r1, [r3, #2256]
	mov	r2, r2, asl #1
	ldrh	ip, [r1, r2]
	rsb	r0, r0, ip
	strh	r0, [r1, r2]	@ movhi
	ldrh	r3, [r3, #72]
	strh	r3, [r4, #2]	@ movhi
	mov	r3, #0
	strb	r3, [r4, #6]
	strh	r3, [r4, #4]	@ movhi
	add	sp, sp, #28
	ldmfd	sp!, {r4, r5, r6, r7, pc}
.L931:
	.align	2
.L930:
	.word	.LANCHOR0
	.word	.LANCHOR3
	.fnend
	.size	FtlSuperblockPowerLostFix, .-FtlSuperblockPowerLostFix
	.align	2
	.global	FtlRecoverySuperblock
	.type	FtlRecoverySuperblock, %function
FtlRecoverySuperblock:
	.fnstart
	@ args = 0, pretend = 0, frame = 56
	@ frame_needed = 0, uses_anonymous_args = 0
	stmfd	sp!, {r4, r5, r6, r7, r8, r9, sl, fp, lr}
	.save {r4, r5, r6, r7, r8, r9, sl, fp, lr}
	movw	r3, #65535
	ldrh	r2, [r0, #0]
	.pad #60
	sub	sp, sp, #60
	mov	r4, r0
	cmp	r2, r3
	beq	.L933
	ldr	r2, .L1003
	mov	r5, #0
	ldrh	r7, [r0, #2]
	ldrb	fp, [r0, #6]	@ zero_extendqisi2
	ldrh	r2, [r2, #72]
	str	r7, [sp, #4]
	cmp	r2, r7
	str	fp, [sp, #20]
	streqh	r5, [r0, #4]	@ movhi
	moveq	r3, r5	@ movhi
	ldrneh	r0, [r0, #16]
	bne	.L935
	b	.L1001
.L936:
	add	r5, r5, #1
	uxth	r5, r5
	add	r2, r4, r5, asl #1
	ldrh	r0, [r2, #16]
.L935:
	cmp	r0, r3
	beq	.L936
	mov	r1, #1
	bl	FtlGetLastWrittenPage
	cmn	r0, #1
	mov	sl, r0
	beq	.L937
	ldr	r3, .L1003
	mov	r5, #0
	ldr	r2, .L1003+4
	str	r4, [sp, #32]
	ldrh	r9, [r3, #4]
	ldr	r8, [r2, #3796]
	mov	r2, r4
	ldr	r7, [r3, #3160]
	ldrh	r6, [r3, #82]
	ldr	lr, [r3, #3164]
	ldrh	ip, [r3, #84]
	mov	r3, r5
	b	.L938
.L937:
	ldr	r3, [sp, #4]
	cmp	r3, #0
	beq	.L939
	ldr	r0, .L1003+8
	movw	r2, #1707
	ldr	r1, .L1003+12
	bl	printk
.L939:
	ldr	r7, [sp, #20]
	cmp	r7, r5
	cmpne	r7, #0
	beq	.L940
	ldr	r0, .L1003+8
	movw	r2, #1708
	ldr	r1, .L1003+12
	bl	printk
.L940:
	mov	r3, #0
	strh	r3, [r4, #2]	@ movhi
	strb	r3, [r4, #6]
	b	.L933
.L942:
	ldrh	r0, [r2, #16]
	movw	fp, #65535
	cmp	r0, fp
	beq	.L941
	mov	fp, #20
	orr	r0, sl, r0, asl #10
	mla	r1, fp, r5, r8
	str	r0, [r1, #4]
	mul	r0, r6, r5
	add	fp, r0, #3
	cmp	r0, #0
	movlt	r0, fp
	bic	r0, r0, #3
	add	r0, r7, r0
	str	r0, [r1, #8]
	mul	r0, ip, r5
	add	r5, r5, #1
	uxth	r5, r5
	add	fp, r0, #3
	cmp	r0, #0
	movlt	r0, fp
	bic	r0, r0, #3
	add	r0, lr, r0
	str	r0, [r1, #12]
.L941:
	add	r3, r3, #1
	add	r2, r2, #2
	uxth	r3, r3
.L938:
	cmp	r3, r9
	bne	.L942
	ldr	r6, .L1003+4
	mov	r2, #0
	ldr	r9, .L1003
	mov	r1, r5
	ldr	r0, [r6, #3796]
	bl	FlashReadPages
	ldr	r2, [r9, #2480]
	movw	r3, #65535
	str	r3, [sp, #8]
	uxth	r3, sl
	ldr	r7, [r6, #3796]
	str	r2, [sp, #16]
	mov	r6, #0
	mov	fp, r3
	b	.L943
.L948:
	ldr	r3, [r7, #0]
	cmp	r3, #0
	strne	fp, [sp, #8]
	bne	.L946
	ldr	r3, [r7, #12]
	ldr	r8, [r3, #4]
	cmn	r8, #1
	beq	.L945
	ldr	r1, [r9, #2480]
	mov	r0, r8
	str	r3, [sp, #0]
	bl	ftl_cmp_data_ver
	ldr	r3, [sp, #0]
	cmp	r0, #0
	addne	r8, r8, #1
	strne	r8, [r9, #2480]
.L945:
	ldr	r3, [r3, #0]
	cmn	r3, #1
	bne	.L946
	ldr	r3, .L1003+4
	mov	r2, #20
	uxth	fp, sl
	ldr	r3, [r3, #3796]
	mla	r6, r2, r6, r3
	ldr	r0, [r6, #4]
	b	.L998
.L946:
	add	r6, r6, #1
	add	r7, r7, #20
	uxth	r6, r6
.L943:
	cmp	r6, r5
	bne	.L948
	ldr	r3, .L1003+4
	add	fp, sl, #1
	uxth	fp, fp
	ldr	r3, [r3, #3796]
	ldr	r0, [r3, #4]
.L998:
	ubfx	r0, r0, #10, #16
	bl	P2V_plane
	ldr	r3, .L1003
	ldr	r1, [sp, #4]
	ldrh	r3, [r3, #72]
	cmp	r3, fp
	streqh	fp, [r4, #2]	@ movhi
	moveq	r3, #0
	streqb	r3, [r4, #6]
	streqh	r3, [r4, #4]	@ movhi
	ldr	r3, [sp, #20]
	str	r0, [sp, #12]
	ldr	r7, [sp, #12]
	cmp	r7, r3
	cmpeq	fp, r1
	beq	.L1002
.L950:
	ldr	r7, [sp, #8]
	movw	r3, #65535
	ldr	r2, [sp, #16]
	cmp	r7, r3
	sub	r5, r2, #1
	bne	.L951
	ldrb	r3, [r4, #8]	@ zero_extendqisi2
	cmp	r3, #0
	bne	.L952
.L951:
	ldr	r3, .L1003+16
	uxth	sl, sl
	ldr	r7, [sp, #4]
	movw	r9, #65535
	mov	r8, r5
	ldr	r2, [r3, #3956]
	cmn	r2, #1
	streq	r5, [r3, #3956]
	add	r3, r7, #7
	cmp	sl, r3
	subgt	r7, sl, #7
	ldrle	r7, [sp, #4]
	uxthgt	r7, r7
	b	.L996
.L957:
	ldrh	r1, [r2, #16]
	add	r3, r3, #1
	add	r2, r2, #2
	cmp	r1, r9
	uxth	r3, r3
	orrne	r1, r7, r1, asl #10
	mlane	r6, r0, r5, ip
	addne	r5, r5, #1
	uxthne	r5, r5
	strne	r1, [r6, #4]
.L962:
	cmp	r3, lr
	bne	.L957
	ldr	r6, .L1003+4
	mov	r2, #0
	mov	r1, r5
	ldr	r0, [r6, #3796]
	bl	FlashReadPages
	ldr	r3, [r6, #3796]
	mov	r2, #0
	ldr	r0, .L1003+16
	b	.L958
.L960:
	ldr	r1, [r3, #0]
	cmp	r1, #0
	bne	.L997
	ldr	r1, [r3, #12]
	ldrh	ip, [r1, #0]
	cmp	ip, r9
	beq	.L959
	ldr	r1, [r1, #4]
	cmn	r1, #1
	strne	r1, [r0, #3956]
.L959:
	add	r2, r2, #1
	add	r3, r3, #20
	uxth	r2, r2
.L958:
	cmp	r2, r5
	bne	.L960
	add	r7, r7, #1
	uxth	r7, r7
.L996:
	cmp	r7, sl
	bhi	.L961
	ldr	r3, .L1003+4
	mov	r5, #0
	ldr	r1, .L1003
	mov	r2, r4
	mov	r0, #20
	ldr	ip, [r3, #3796]
	mov	r3, r5
	ldrh	lr, [r1, #4]
	b	.L962
.L961:
	ldr	r3, .L1003+16
	mvn	r2, #0
	mov	r5, r8
	str	r2, [r3, #3956]
	b	.L952
.L997:
	mov	r5, r8
.L952:
	ldr	r2, .L1003+16
	movw	r3, #4030
	mov	r1, #1
	ldr	r0, .L1003+20
	ldr	r6, .L1003
	mov	sl, r4
	strh	r1, [r2, r3]	@ movhi
	bl	FtlMapBlkWriteDump_data
	ldr	r9, [sp, #4]
	str	fp, [sp, #28]
.L963:
	ldr	r3, .L1003+4
	mov	r4, #0
	ldrh	lr, [r6, #4]
	mov	r2, sl
	mov	r0, #20
	ldr	ip, [r3, #3796]
	mov	r3, r4
	b	.L964
.L966:
	ldrh	r1, [r2, #16]
	movw	r7, #65535
	add	r3, r3, #1
	add	r2, r2, #2
	cmp	r1, r7
	uxth	r3, r3
	orrne	r1, r9, r1, asl #10
	mlane	r7, r0, r4, ip
	addne	r4, r4, #1
	uxthne	r4, r4
	strne	r1, [r7, #4]
.L964:
	cmp	r3, lr
	bne	.L966
	ldr	r7, .L1003+4
	mov	r1, r4
	mov	r2, #0
	ldr	r0, [r7, #3796]
	bl	FlashReadPages
	mov	r3, #20
	mul	r3, r3, r4
	mov	r4, #0
	str	r3, [sp, #36]
	b	.L967
.L990:
	ldr	fp, [r7, #3796]
	add	fp, fp, r4
	ldr	r8, [fp, #4]
	ubfx	r0, r8, #10, #16
	str	r8, [sp, #52]
	bl	P2V_plane
	ldr	r3, [sp, #4]
	cmp	r9, r3
	bcc	.L968
	ldr	r1, [sp, #20]
	mov	r2, r3
	cmp	r0, r1
	movcs	r3, #0
	movcc	r3, #1
	cmp	r9, r2
	movne	r3, #0
	cmp	r3, #0
	bne	.L968
	ldr	r3, [sp, #12]
	ldr	r1, [sp, #28]
	cmp	r0, r3
	cmpeq	r9, r1
	beq	.L969
	ldr	r3, [fp, #0]
	cmn	r3, #1
	beq	.L970
	ldr	r8, [fp, #12]
	movw	r3, #61589
	ldrh	r2, [r8, #0]
	cmp	r2, r3
	bne	.L978
.L971:
	ldr	r5, [r8, #4]
	cmn	r5, #1
	beq	.L972
	ldr	r1, [r6, #2480]
	mov	r0, r5
	bl	ftl_cmp_data_ver
	cmp	r0, #0
	addne	r3, r5, #1
	strne	r3, [r6, #2480]
.L972:
	ldrh	r2, [r8, #0]
	movw	r3, #61589
	cmp	r2, r3
	beq	.L973
	ldr	r0, .L1003+8
	movw	r2, #1857
	ldr	r1, .L1003+12
	bl	printk
.L973:
	ldr	r3, [r8, #8]
	add	r1, sp, #48
	ldr	fp, .L1003+16
	mov	r2, #0
	str	r3, [sp, #8]
	ldr	r3, [r8, #12]
	ldr	r0, [sp, #8]
	str	r3, [sp, #44]
	bl	log2phys
	ldr	r1, [fp, #3956]
	cmn	r1, #1
	beq	.L974
	mov	r0, r5
	bl	ftl_cmp_data_ver
	cmp	r0, #0
	beq	.L974
	ldr	r3, [sp, #44]
	cmn	r3, #1
	beq	.L975
	ldr	r0, [r7, #3796]
	mov	r1, #1
	mov	r2, #0
	add	r0, r0, r4
	ldr	fp, [r0, #12]
	str	r3, [r0, #4]
	str	fp, [sp, #16]
	bl	FlashReadPages
	ldr	r3, [fp, #4]
	str	r3, [sp, #24]
	ldr	r3, [r7, #3796]
	add	fp, r3, r4
	ldr	r3, [r3, r4]
	cmn	r3, #1
	bne	.L976
	b	.L977
.L975:
	ldr	r3, [sp, #52]
	ldr	r2, [sp, #48]
	cmp	r2, r3
	bne	.L978
	ldr	r0, [sp, #8]
	add	r1, sp, #44
	mov	r2, #1
	bl	log2phys
.L978:
	ldrh	r0, [sl, #0]
	b	.L1000
.L976:
	ldr	r3, [sp, #16]
	ldr	r8, [r3, #8]
	ldr	r3, [sp, #8]
	cmp	r8, r3
	bne	.L977
	ldr	r1, .L1003+16
	ldr	r0, [r1, #3956]
	ldr	r1, [sp, #24]
	bl	ftl_cmp_data_ver
	cmp	r0, #0
	beq	.L977
	ldr	r3, [sp, #48]
	ldr	r2, [sp, #52]
	cmp	r3, r2
	beq	.L982
.L979:
	ldr	r2, [sp, #44]
	cmp	r3, r2
	beq	.L977
	cmn	r3, #1
	streq	r3, [fp, #0]
	beq	.L981
	ldr	r2, [fp, #12]
	mov	r0, fp
	str	r3, [fp, #4]
	mov	r1, #1
	str	r2, [sp, #16]
	mov	r2, #0
	bl	FlashReadPages
.L981:
	ldr	r3, [r7, #3796]
	ldr	r3, [r3, r4]
	cmn	r3, #1
	beq	.L982
	ldr	r3, [sp, #16]
	ldr	fp, [r3, #4]
	ldr	r3, .L1003+16
	mov	r1, fp
	ldr	r0, [r3, #3956]
	bl	ftl_cmp_data_ver
	cmp	r0, #0
	beq	.L982
	ldr	r0, [sp, #24]
	mov	r1, fp
	bl	ftl_cmp_data_ver
	cmp	r0, #0
	beq	.L977
.L982:
	mov	r0, r8
	ldr	r1, [sp, #44]
	bl	FtlReUsePrevPpa
.L977:
	ldrh	r0, [sl, #0]
	mvn	r3, #0
	str	r3, [sp, #44]
	bl	decrement_vpc_count
	b	.L984
.L974:
	ldr	r3, [sp, #52]
	ldr	r2, [sp, #48]
	cmp	r2, r3
	beq	.L984
	ldr	r0, [sp, #8]
	add	r1, sp, #52
	mov	r2, #1
	bl	log2phys
	ldr	fp, [sp, #48]
	cmn	fp, #1
	beq	.L984
	ldr	r3, [sp, #44]
	cmp	fp, r3
	beq	.L984
	ubfx	r0, fp, #10, #16
	bl	P2V_block_in_plane
	movw	r3, #2276
	ldrh	r3, [r6, r3]
	cmp	r3, r0
	beq	.L985
	movw	r3, #2324
	ldrh	r3, [r6, r3]
	cmp	r3, r0
	beq	.L985
	movw	r3, #2372
	ldrh	r3, [r6, r3]
	cmp	r3, r0
	bne	.L984
.L985:
	ldr	r0, [r7, #3796]
	mov	r1, #1
	mov	r2, #0
	str	fp, [r0, #4]
	ldr	r8, [r0, #12]
	bl	FlashReadPages
	ldr	r3, [r7, #3796]
	ldr	r1, [r8, #4]
	ldr	r3, [r3, #0]
	cmn	r3, #1
	beq	.L984
	mov	r0, r5
	bl	ftl_cmp_data_ver
	cmp	r0, #0
	bne	.L984
	ldr	r0, [sp, #8]
	add	r1, sp, #48
	mov	r2, #1
	bl	log2phys
.L984:
	ldr	r0, [sp, #44]
	cmn	r0, #1
	beq	.L968
	ubfx	r0, r0, #10, #16
	bl	P2V_block_in_plane
	ldrh	r3, [r6, #12]
	cmp	r3, r0
	mov	r8, r0
	bhi	.L986
	ldr	r0, .L1003+8
	movw	r2, #1958
	ldr	r1, .L1003+12
	bl	printk
.L986:
	ldr	fp, .L1003
	mov	r3, r8, asl #1
	ldr	r2, [fp, #2256]
	ldrh	r3, [r2, r3]
	cmp	r3, #0
	beq	.L987
	mov	r0, r8
.L1000:
	bl	decrement_vpc_count
	b	.L968
.L987:
	mov	r1, r8
	ldr	r0, .L1003+24
	bl	printk
	b	.L968
.L970:
	ldr	r3, .L1003+16
	ldr	r2, [r3, #4032]
	cmp	r2, #31
	addls	r1, r3, r2, asl #2
	addls	r2, r2, #1
	strls	r2, [r3, #4032]
	strls	r8, [r1, #4036]
	ldrh	r0, [sl, #0]
	bl	decrement_vpc_count
	ldr	r3, .L1003+16
	ldr	r2, [r3, #3956]
	cmn	r2, #1
	beq	.L999
.L989:
	cmp	r2, r5
	bls	.L968
.L999:
	str	r5, [r3, #3956]
.L968:
	add	r4, r4, #20
.L967:
	ldr	r3, [sp, #36]
	cmp	r4, r3
	bne	.L990
	add	r9, r9, #1
	ldrh	r3, [r6, #72]
	uxth	r9, r9
	cmp	r9, r3
	bne	.L963
	ldr	r2, .L1003
	mov	r3, #0
	mov	r4, sl
	strh	r9, [sl, #2]	@ movhi
	strh	r3, [sl, #4]	@ movhi
	ldrh	r1, [r2, #4]
	movw	r2, #65535
	ldr	r0, [sp, #32]
	b	.L991
.L993:
	add	r0, r0, #2
	ldrh	ip, [r0, #14]
	cmp	ip, r2
	beq	.L992
.L1001:
	strb	r3, [r4, #6]
	b	.L933
.L992:
	add	r3, r3, #1
	uxth	r3, r3
.L991:
	cmp	r3, r1
	bne	.L993
	b	.L933
.L969:
	ldr	fp, [sp, #28]
	mov	r4, sl
	ldr	r7, [sp, #12]
	strh	fp, [sl, #2]	@ movhi
	strb	r7, [sl, #6]
.L1002:
	mov	r0, r4
	mov	r1, fp
	mov	r2, r7
	bl	ftl_sb_update_avl_pages
.L933:
	mov	r0, #0
	add	sp, sp, #60
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, sl, fp, pc}
.L1004:
	.align	2
.L1003:
	.word	.LANCHOR0
	.word	.LANCHOR3
	.word	.LC1
	.word	.LANCHOR1+437
	.word	.LANCHOR4
	.word	.LANCHOR3+3812
	.word	.LC99
	.fnend
	.size	FtlRecoverySuperblock, .-FtlRecoverySuperblock
	.align	2
	.global	allocate_data_superblock
	.type	allocate_data_superblock, %function
allocate_data_superblock:
	.fnstart
	@ args = 0, pretend = 0, frame = 24
	@ frame_needed = 0, uses_anonymous_args = 0
	stmfd	sp!, {r4, r5, r6, r7, r8, r9, sl, fp, lr}
	.save {r4, r5, r6, r7, r8, r9, sl, fp, lr}
	.pad #28
	sub	sp, sp, #28
	ldr	r4, .L1044
	mov	r5, r0
	movw	fp, #2264
	mov	r7, r4
.L1041:
	mov	r3, #2272
	ldrh	r2, [r4, r3]
	ldrh	r3, [r4, fp]
	add	r2, r2, r3
	ldrh	r3, [r4, #12]
	cmp	r2, r3
	ble	.L1007
	ldr	r0, .L1044+4
	movw	r2, #2577
	ldr	r1, .L1044+8
	bl	printk
.L1007:
	ldr	r3, .L1044+12
	cmp	r5, r3
	bne	.L1037
	mov	r3, #2272
	ldrh	r2, [r4, r3]
	ldr	r3, [r4, #3148]
	mov	r1, r2, lsr #1
	mul	ip, r3, r2
	add	r0, r1, #1
	add	r0, r0, ip, lsr #2
	ldr	ip, [r4, #2436]
	cmp	ip, #0
	uxth	r0, r0
	beq	.L1036
	ldr	ip, [r4, #2504]
	cmp	ip, #29
	bhi	.L1036
	cmp	ip, #2
	bls	.L1037
	tst	r2, #1
	beq	.L1009
	cmp	r3, #0
	moveq	r1, r3
	beq	.L1008
	b	.L1009
.L1036:
	mov	r1, r0
.L1009:
	cmp	r1, #0
	subne	r1, r1, #1
	uxthne	r1, r1
	b	.L1008
.L1037:
	mov	r1, #0
.L1008:
	ldr	r0, .L1044+16
	bl	List_pop_index_node
	mov	r3, #2272
	ldrh	r3, [r4, r3]
	cmp	r3, #0
	uxth	r6, r0
	bne	.L1010
	ldr	r0, .L1044+4
	mov	r2, #2592
	ldr	r1, .L1044+8
	bl	printk
.L1010:
	mov	r8, #2272
	mov	r0, r5
	ldrh	r3, [r7, r8]
	sub	r3, r3, #1
	strh	r3, [r7, r8]	@ movhi
	strh	r6, [r5, #0]	@ movhi
	bl	make_superblock
	ldrb	r3, [r5, #7]	@ zero_extendqisi2
	cmp	r3, #0
	bne	.L1011
	ldr	r2, [r4, #2256]
	mov	r3, r6, asl #1
	mvn	r1, #0
	mov	r0, r6
	strh	r1, [r2, r3]	@ movhi
	bl	INSERT_DATA_LIST
	ldrh	r3, [r4, fp]
	ldrh	r2, [r4, r8]
	add	r2, r2, r3
	ldrh	r3, [r4, #12]
	cmp	r2, r3
	ble	.L1041
	ldr	r1, .L1044+8
	movw	r2, #2603
	ldr	r0, .L1044+4
	bl	printk
	b	.L1041
.L1011:
	ldrh	r3, [r4, fp]
	ldrh	r2, [r4, r8]
	add	r2, r2, r3
	ldrh	r3, [r4, #12]
	cmp	r2, r3
	ble	.L1013
	ldr	r0, .L1044+4
	movw	r2, #2606
	ldr	r1, .L1044+8
	bl	printk
.L1013:
	ldr	r3, .L1044+20
	mov	r8, #0
	ldrh	r1, [r4, #4]
	mov	r9, r5
	mov	r2, r8
	mov	r0, r8
	ldr	ip, [r3, #3776]
	str	r1, [sp, #16]
	mov	r1, r5
	str	r5, [sp, #4]
	mov	r3, ip
	str	ip, [sp, #20]
	b	.L1014
.L1016:
	str	r0, [r3, #8]
	movw	r5, #65535
	str	r0, [r3, #12]
	ldrh	lr, [r1, #16]
	cmp	lr, r5
	beq	.L1015
	ldr	r5, [sp, #20]
	mov	ip, #20
	mov	lr, lr, asl #10
	mla	sl, ip, r8, r5
	add	r8, r8, #1
	uxth	r8, r8
	str	lr, [sl, #4]
.L1015:
	add	r2, r2, #1
	add	r3, r3, #20
	add	r1, r1, #2
	uxth	r2, r2
.L1014:
	ldr	sl, [sp, #16]
	cmp	r2, sl
	bne	.L1016
	cmp	r8, #0
	ldr	r5, [sp, #4]
	bne	.L1017
	ldr	r0, .L1044+4
	movw	r2, #2617
	ldr	r1, .L1044+8
	bl	printk
.L1017:
	ldr	r3, [r4, #2436]
	cmp	r3, #0
	beq	.L1018
	ldr	r3, .L1044+24
	cmp	r5, r3
	bne	.L1018
	ldr	r2, [r4, #2244]
	mov	r3, r6, asl #1
	ldrh	r3, [r2, r3]
	cmp	r3, #30
	movhi	r3, #0
	strhib	r3, [r4, #2284]
.L1018:
	movw	r3, #2572
	ldrh	r3, [r7, r3]
	cmp	r3, r6
	bne	.L1019
	ldr	r0, .L1044+4
	mov	r2, #2624
	ldr	r1, .L1044+8
	bl	printk
.L1019:
	ldrb	r3, [r5, #8]	@ zero_extendqisi2
	ldr	r2, [r4, #2244]
	cmp	r3, #0
	mov	r3, r6, asl #1
	ldrh	r1, [r2, r3]
	bne	.L1020
	cmp	r1, #0
	ldrneh	r0, [r4, #62]
	moveq	r1, #2
	addne	r1, r1, r0
	strh	r1, [r2, r3]	@ movhi
	ldr	r3, [r4, #2484]
	mov	r0, r6
	mov	r1, #0
	add	r3, r3, #1
	str	r3, [r4, #2484]
	b	.L1043
.L1020:
	add	r1, r1, #1
	strh	r1, [r2, r3]	@ movhi
	ldr	r3, [r4, #2488]
	mov	r0, r6
	mov	r1, #1
	add	r3, r3, #1
	str	r3, [r4, #2488]
.L1043:
	bl	ftl_set_blk_mode
	ldr	r3, [r4, #2244]
	mov	ip, r6, asl #1
	ldr	r2, [r4, #2500]
	ldr	r0, [r7, #2484]
	mov	sl, #0
	ldrh	r3, [r3, ip]
	ldrh	r1, [r7, #12]
	cmp	r3, r2
	ldrh	r2, [r7, #62]
	strhi	r3, [r4, #2500]
	ldr	r3, [r7, #2488]
	str	ip, [sp, #16]
	mla	r0, r0, r2, r3
	bl	__aeabi_uidiv
	ldr	r3, .L1044+28
	ldr	r3, [r3, #4016]
	ldr	r2, [r3, #16]
	add	r2, r2, #1
	str	r2, [r3, #16]
	ldr	r3, .L1044+20
	ldr	r3, [r3, #3776]
	str	r0, [r7, #2492]
	b	.L1025
.L1026:
	add	sl, sl, #1
	ldr	r2, [r3, #-16]
	uxth	sl, sl
	bic	r2, r2, #1020
	bic	r2, r2, #3
	str	r2, [r3, #-16]
.L1025:
	cmp	sl, r8
	add	r3, r3, #20
	bne	.L1026
	ldr	r8, .L1044+20
	mov	r2, sl
	ldrb	r1, [r5, #8]	@ zero_extendqisi2
	ldr	r0, [r8, #3776]
	bl	FlashEraseBlocks
	mov	ip, #20
	mul	ip, ip, sl
	mov	sl, #0
	mov	r2, r8
	str	r6, [sp, #20]
	mov	r8, sl
	mov	r6, ip
	b	.L1027
.L1029:
	ldr	r1, [r2, #3776]
	add	r0, r1, sl
	ldr	r1, [r1, sl]
	cmn	r1, #1
	bne	.L1028
	ldr	r0, [r0, #4]
	add	r8, r8, #1
	str	r1, [sp, #12]
	ubfx	r0, r0, #10, #16
	str	r2, [sp, #8]
	bl	FtlBbmMapBadBlock
	ldr	r1, [sp, #12]
	strh	r1, [r9, #16]	@ movhi
	ldrb	r1, [r5, #7]	@ zero_extendqisi2
	sub	r1, r1, #1
	strb	r1, [r5, #7]
	ldr	r2, [sp, #8]
.L1028:
	add	sl, sl, #20
	add	r9, r9, #2
.L1027:
	cmp	sl, r6
	bne	.L1029
	cmp	r8, #0
	ldr	r6, [sp, #20]
	beq	.L1030
	mov	r0, r6
	bl	update_multiplier_value
	bl	FtlBbmTblFlush
.L1030:
	ldrb	r2, [r5, #7]	@ zero_extendqisi2
	ldr	r3, .L1044
	cmp	r2, #0
	bne	.L1031
	ldr	r3, [r4, #2256]
	mvn	r2, #0
	ldr	sl, [sp, #16]
	mov	r0, r6
	strh	r2, [r3, sl]	@ movhi
	bl	INSERT_DATA_LIST
	b	.L1041
.L1031:
	ldrh	r1, [r3, #72]
	strh	r6, [r5, #0]	@ movhi
	mul	r2, r1, r2
	mov	r1, #0
	strh	r1, [r5, #2]	@ movhi
	strb	r1, [r5, #6]
	ldr	r1, [r3, #2476]
	uxth	r2, r2
	strh	r2, [r5, #4]	@ movhi
	str	r1, [r5, #12]
	add	r1, r1, #1
	str	r1, [r3, #2476]
	ldrh	r0, [r5, #0]
	ldr	r1, [r3, #2256]
	mov	r3, r0, asl #1
	strh	r2, [r1, r3]	@ movhi
	ldrh	r3, [r5, #4]
	cmp	r3, #0
	beq	.L1032
	ldrb	r3, [r5, #7]	@ zero_extendqisi2
	cmp	r3, #0
	bne	.L1033
.L1032:
	ldr	r0, .L1044+4
	movw	r2, #2677
	ldr	r1, .L1044+8
	bl	printk
.L1033:
	mov	r0, #0
	add	sp, sp, #28
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, sl, fp, pc}
.L1045:
	.align	2
.L1044:
	.word	.LANCHOR0
	.word	.LC1
	.word	.LANCHOR1+459
	.word	.LANCHOR0+2372
	.word	.LANCHOR0+2268
	.word	.LANCHOR3
	.word	.LANCHOR0+2276
	.word	.LANCHOR4
	.fnend
	.size	allocate_data_superblock, .-allocate_data_superblock
	.align	2
	.global	FtlWriteDump_data
	.type	FtlWriteDump_data, %function
FtlWriteDump_data:
	.fnstart
	@ args = 0, pretend = 0, frame = 24
	@ frame_needed = 0, uses_anonymous_args = 0
	stmfd	sp!, {r4, r5, r6, r7, r8, sl, lr}
	.save {r4, r5, r6, r7, r8, sl, lr}
	movw	r3, #2280
	ldr	r5, .L1055
	.pad #28
	sub	sp, sp, #28
	ldrh	r3, [r5, r3]
	mov	r2, r5
	cmp	r3, #0
	beq	.L1047
	ldrb	r1, [r5, #2284]	@ zero_extendqisi2
	cmp	r1, #0
	bne	.L1047
	ldrb	r1, [r5, #2283]	@ zero_extendqisi2
	ldrh	r0, [r5, #72]
	mul	r1, r0, r1
	cmp	r3, r1
	beq	.L1047
	ldrb	r8, [r5, #2286]	@ zero_extendqisi2
	ldr	r7, [r5, #2440]
	cmp	r8, #0
	ldrh	r6, [r5, #4]
	bne	.L1046
	sub	r7, r7, #1
	mov	r2, r8
	mov	r1, sp
	mov	r0, r7
	bl	log2phys
	ldr	r2, .L1055+4
	ldr	r3, [sp, #0]
	ldr	r0, [r5, #2240]
	ldr	r4, [r2, #-836]
	cmn	r3, #1
	str	r3, [sp, #8]
	str	r7, [sp, #20]
	str	r0, [sp, #12]
	str	r4, [sp, #16]
	str	r8, [r4, #4]
	beq	.L1049
	add	r0, sp, #4
	mov	r1, #1
	mov	r2, r8
	bl	FlashReadPages
	b	.L1050
.L1049:
	mov	r1, #255
	ldrh	r2, [r5, #82]
	bl	ftl_memset
.L1050:
	ldr	r3, .L1055+8
	mov	r6, r6, asl #2
	ldr	r5, .L1055
	movw	sl, #2280
	movw	r8, #2276
	strh	r3, [r4, #0]	@ movhi
	b	.L1051
.L1054:
	ldrh	r3, [r5, sl]
	cmp	r3, #0
	beq	.L1052
	ldr	r3, [sp, #8]
	sub	r6, r6, #1
	str	r7, [r4, #8]
	ldr	r0, .L1055+12
	str	r3, [r4, #12]
	ldrh	r3, [r5, r8]
	strh	r3, [r4, #2]	@ movhi
	bl	get_new_active_ppa
	ldr	r3, [r5, #2480]
	mov	r2, #0
	mov	r1, #1
	str	r0, [sp, #8]
	add	r0, sp, #4
	str	r3, [r4, #4]
	add	r3, r3, #1
	cmn	r3, #1
	str	r3, [r5, #2480]
	moveq	r3, #0
	streq	r3, [r5, #2480]
	mov	r3, r2
	bl	FlashProgPages
	ldrh	r0, [r5, r8]
	bl	decrement_vpc_count
.L1051:
	cmp	r6, #0
	bne	.L1054
.L1052:
	ldr	r3, .L1055
	mov	r2, #1
	strb	r2, [r3, #2286]
	b	.L1046
.L1047:
	mov	r3, #0
	strb	r3, [r2, #2286]
.L1046:
	add	sp, sp, #28
	ldmfd	sp!, {r4, r5, r6, r7, r8, sl, pc}
.L1056:
	.align	2
.L1055:
	.word	.LANCHOR0
	.word	.LANCHOR3
	.word	-3947
	.word	.LANCHOR0+2276
	.fnend
	.size	FtlWriteDump_data, .-FtlWriteDump_data
	.align	2
	.global	l2p_flush
	.type	l2p_flush, %function
l2p_flush:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	stmfd	sp!, {r4, r5, r6, lr}
	.save {r4, r5, r6, lr}
	bl	FtlWriteDump_data
	mov	r4, #0
	ldr	r5, .L1061
	mov	r6, #12
	b	.L1058
.L1060:
	ldr	r3, [r5, #2420]
	mla	r3, r6, r4, r3
	ldr	r3, [r3, #4]
	cmp	r3, #0
	bge	.L1059
	mov	r0, r4
	bl	flush_l2p_region
.L1059:
	add	r4, r4, #1
	uxth	r4, r4
.L1058:
	ldrh	r3, [r5, #110]
	cmp	r3, r4
	bhi	.L1060
	mov	r0, #0
	ldmfd	sp!, {r4, r5, r6, pc}
.L1062:
	.align	2
.L1061:
	.word	.LANCHOR0
	.fnend
	.size	l2p_flush, .-l2p_flush
	.align	2
	.global	allocate_new_data_superblock
	.type	allocate_new_data_superblock, %function
allocate_new_data_superblock:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	stmfd	sp!, {r4, r5, r6, lr}
	.save {r4, r5, r6, lr}
	mov	r5, r0
	ldr	r6, .L1070
	ldrh	r4, [r0, #0]
	ldrh	r3, [r6, #12]
	cmp	r3, r4
	bcs	.L1064
	ldr	r0, .L1070+4
	movw	r2, #2684
	ldr	r1, .L1070+8
	bl	printk
.L1064:
	movw	r3, #65535
	cmp	r4, r3
	beq	.L1065
	ldr	r2, [r6, #2256]
	mov	r3, r4, asl #1
	mov	r0, r4
	ldrh	r3, [r2, r3]
	cmp	r3, #0
	beq	.L1066
	bl	INSERT_DATA_LIST
	b	.L1065
.L1066:
	bl	INSERT_FREE_LIST
.L1065:
	ldr	r2, .L1070+12
	mov	r3, #1
	strb	r3, [r5, #8]
	movw	r3, #3948
	ldrh	r0, [r2, r3]
	movw	r3, #65535
	cmp	r0, r3
	beq	.L1067
	cmp	r4, r0
	bne	.L1068
	ldr	r3, .L1070
	ldr	r2, [r3, #2256]
	mov	r3, r0, asl #1
	ldrh	r3, [r2, r3]
	cmp	r3, #0
	beq	.L1069
.L1068:
	bl	update_vpc_list
.L1069:
	ldr	r2, .L1070+12
	movw	r3, #3948
	mvn	r1, #0
	strh	r1, [r2, r3]	@ movhi
.L1067:
	mov	r0, r5
	bl	allocate_data_superblock
	bl	l2p_flush
	mov	r0, #0
	bl	FtlEctTblFlush
	bl	FtlVpcTblFlush
	mov	r0, #0
	ldmfd	sp!, {r4, r5, r6, pc}
.L1071:
	.align	2
.L1070:
	.word	.LANCHOR0
	.word	.LC1
	.word	.LANCHOR1+484
	.word	.LANCHOR4
	.fnend
	.size	allocate_new_data_superblock, .-allocate_new_data_superblock
	.align	2
	.global	FtlProgPages
	.type	FtlProgPages, %function
FtlProgPages:
	.fnstart
	@ args = 0, pretend = 0, frame = 8
	@ frame_needed = 0, uses_anonymous_args = 0
	stmfd	sp!, {r0, r1, r2, r4, r5, r6, r7, r8, sl, lr}
	.save {r0, r1, r2, r4, r5, r6, r7, r8, sl, lr}
	mov	r4, r3
	mov	r2, #0
	ldrb	r3, [r3, #9]	@ zero_extendqisi2
	mov	r5, r0
	mov	sl, r1
	mov	r7, #0
	bl	FlashProgPages
	ldr	r6, .L1085
	b	.L1073
.L1076:
	ldr	r0, [r5, #4]
	ubfx	r0, r0, #10, #16
	bl	P2V_block_in_plane
	ldrh	r3, [r4, #0]
	cmp	r3, r0
	bne	.L1074
	ldr	r2, [r6, #2256]
	mov	r3, r3, asl #1
	ldrh	r1, [r4, #4]
	ldrh	r0, [r2, r3]
	rsb	r1, r1, r0
	strh	r1, [r2, r3]	@ movhi
	ldrh	r3, [r6, #72]
	strb	r8, [r4, #6]
	strh	r8, [r4, #4]	@ movhi
	strh	r3, [r4, #2]	@ movhi
.L1074:
	ldrh	r3, [r4, #4]
	cmp	r3, #0
	bne	.L1075
	mov	r0, r4
	bl	allocate_new_data_superblock
.L1075:
	ldr	r3, [r6, #2720]
	add	r3, r3, #1
	str	r3, [r6, #2720]
	ldr	r0, [r5, #4]
	ubfx	r0, r0, #10, #16
	bl	FtlGcMarkBadPhyBlk
	mov	r0, r4
	bl	get_new_active_ppa
	mov	r1, #1
	mov	r2, #0
	str	r0, [r5, #4]
	str	r0, [sp, #4]
	mov	r0, r5
	ldrb	r3, [r4, #9]	@ zero_extendqisi2
	bl	FlashProgPages
	b	.L1083
.L1084:
	mov	r8, #0
.L1083:
	ldr	r3, [r5, #0]
	cmn	r3, #1
	beq	.L1076
	ldrb	r2, [r4, #6]	@ zero_extendqisi2
	ldrh	r3, [r6, #4]
	cmp	r2, r3
	bcc	.L1077
	ldr	r0, .L1085+4
	movw	r2, #994
	ldr	r1, .L1085+8
	bl	printk
.L1077:
	ldr	r3, [r5, #4]
	add	r1, sp, #4
	ldr	r0, [r5, #16]
	mov	r2, #1
	str	r3, [sp, #4]
	bl	log2phys
	ldr	r3, [r5, #12]
	ldr	r0, [r3, #12]
	cmn	r0, #1
	beq	.L1078
	ubfx	r0, r0, #10, #16
	bl	P2V_block_in_plane
	ldr	r2, [r6, #2256]
	mov	r3, r0, asl #1
	mov	r8, r0
	ldrh	r2, [r2, r3]
	cmp	r2, #0
	bne	.L1079
	ldr	r0, .L1085+12
	mov	r1, r8
	bl	printk
.L1079:
	mov	r0, r8
	bl	decrement_vpc_count
.L1078:
	add	r7, r7, #1
	add	r5, r5, #20
.L1073:
	cmp	r7, sl
	bne	.L1084
	ldr	r3, .L1085
	ldrb	r2, [r4, #6]	@ zero_extendqisi2
	ldrh	r3, [r3, #4]
	cmp	r2, r3
	bcc	.L1072
	ldr	r0, .L1085+4
	movw	r2, #1009
	ldr	r1, .L1085+8
	bl	printk
.L1072:
	ldmfd	sp!, {r1, r2, r3, r4, r5, r6, r7, r8, sl, pc}
.L1086:
	.align	2
.L1085:
	.word	.LANCHOR0
	.word	.LC1
	.word	.LANCHOR1+513
	.word	.LC100
	.fnend
	.size	FtlProgPages, .-FtlProgPages
	.align	2
	.global	FtlCacheWriteBack
	.type	FtlCacheWriteBack, %function
FtlCacheWriteBack:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	stmfd	sp!, {r4, lr}
	.save {r4, lr}
	ldr	r4, .L1089
	ldr	r0, [r4, #3944]
	cmp	r0, #0
	beq	.L1088
	ldr	r3, .L1089+4
	mov	r1, #1
	mov	r2, #0
	ldr	r3, [r3, #-4020]
	bl	FtlProgPages
	mov	r3, #0
	str	r3, [r4, #3944]
.L1088:
	mov	r0, #0
	ldmfd	sp!, {r4, pc}
.L1090:
	.align	2
.L1089:
	.word	.LANCHOR4
	.word	.LANCHOR5
	.fnend
	.size	FtlCacheWriteBack, .-FtlCacheWriteBack
	.align	2
	.global	FtlGcFreeTempBlock
	.type	FtlGcFreeTempBlock, %function
FtlGcFreeTempBlock:
	.fnstart
	@ args = 0, pretend = 0, frame = 8
	@ frame_needed = 0, uses_anonymous_args = 0
	ldr	r3, .L1114
	stmfd	sp!, {r0, r1, r2, r4, r5, r6, r7, r8, sl, lr}
	.save {r0, r1, r2, r4, r5, r6, r7, r8, sl, lr}
	movw	r2, #2372
	ldrh	r4, [r3, r2]
	movw	r2, #65535
	ldrh	r1, [r3, #72]
	cmp	r4, r2
	beq	.L1092
	cmp	r0, #0
	beq	.L1093
	ldr	r0, .L1114+4
	ldrh	ip, [r0, #4]
	cmp	ip, r2
	movne	r1, #2
	bne	.L1093
	mov	r2, #0
	strh	r2, [r0, #4]	@ movhi
	mov	r2, #2272
	ldrh	r3, [r3, r2]
	cmp	r3, #17
	movhi	r1, #2
.L1093:
	ldr	r0, .L1114+8
	bl	FtlGcScanTempBlk
	cmn	r0, #1
	str	r0, [sp, #4]
	beq	.L1094
	ldr	r3, .L1114
	mov	r4, r4, asl #1
	ldr	r3, [r3, #2244]
	ldrh	r2, [r3, r4]
	cmp	r2, #4
	bls	.L1095
	sub	r2, r2, #5
	mov	r0, #1
	strh	r2, [r3, r4]	@ movhi
	bl	FtlEctTblFlush
.L1095:
	ldr	r3, .L1114+12
	ldr	r3, [r3, #3800]
	cmp	r3, #0
	bne	.L1096
	ldr	r3, .L1114
	ldr	r0, [sp, #4]
	ldr	r2, [r3, #2720]
	ubfx	r0, r0, #10, #16
	add	r2, r2, #1
	str	r2, [r3, #2720]
	bl	FtlBbmMapBadBlock
	bl	FtlBbmTblFlush
.L1096:
	ldr	r3, .L1114+12
	mov	r2, #0
	str	r2, [r3, #3800]
	b	.L1109
.L1094:
	ldr	r3, .L1114+4
	ldrh	r2, [r3, #4]
	movw	r3, #65535
	cmp	r2, r3
	bne	.L1109
.L1092:
	ldr	r3, .L1114+12
	mov	r0, #0
	ldr	r4, .L1114
	str	r0, [r3, #3800]
	movw	r3, #2372
	ldrh	r2, [r4, r3]
	movw	r3, #65535
	cmp	r2, r3
	beq	.L1097
	bl	FtlCacheWriteBack
	movw	r3, #3188
	ldrh	r2, [r4, r3]
	ldrh	r1, [r4, #72]
	ldrb	r3, [r4, #2379]	@ zero_extendqisi2
	mul	r3, r1, r3
	cmp	r2, r3
	beq	.L1098
	ldr	r0, .L1114+16
	mov	r2, #163
	ldr	r1, .L1114+20
	bl	printk
.L1098:
	ldr	r5, .L1114
	movw	r3, #2372
	movw	sl, #3188
	mov	r6, #0
	ldrb	r1, [r5, #2379]	@ zero_extendqisi2
	ldrh	r0, [r5, #72]
	ldrh	r3, [r5, r3]
	ldr	r2, [r5, #2256]
	mul	r1, r0, r1
	mov	r3, r3, asl #1
	strh	r1, [r2, r3]	@ movhi
	ldr	r3, [r5, #2464]
	ldrh	r2, [r5, sl]
	add	r3, r2, r3
	str	r3, [r5, #2464]
	b	.L1099
.L1103:
	mov	r7, #12
	ldr	r8, [r5, #3184]
	mul	r7, r7, r6
	ldr	r3, [r5, #2440]
	add	r4, r8, r7
	ldr	r2, [r4, #8]
	cmp	r2, r3
	bcc	.L1100
	ldr	r0, .L1114+16
	mov	r2, #169
	ldr	r1, .L1114+20
	bl	printk
.L1100:
	ldr	r0, [r4, #8]
	add	r1, sp, #4
	mov	r2, #0
	bl	log2phys
	ldr	r0, [sp, #4]
	ldr	r3, [r8, r7]
	cmp	r0, r3
	bne	.L1101
	ubfx	r0, r0, #10, #16
	bl	P2V_block_in_plane
	add	r1, r4, #4
	mov	r2, #1
	mov	r7, r0
	ldr	r0, [r4, #8]
	bl	log2phys
	mov	r0, r7
	b	.L1112
.L1101:
	ldr	r3, [r4, #4]
	cmp	r0, r3
	beq	.L1102
	movw	r3, #2372
	ldrh	r0, [r5, r3]
.L1112:
	bl	decrement_vpc_count
.L1102:
	add	r6, r6, #1
	uxth	r6, r6
.L1099:
	ldrh	r3, [r5, sl]
	ldr	r4, .L1114
	cmp	r3, r6
	bhi	.L1103
	movw	r0, #65535
	movw	r5, #2372
	bl	decrement_vpc_count
	ldrh	r0, [r4, r5]
	ldr	r2, [r4, #2256]
	mov	r3, r0, asl #1
	ldrh	r3, [r2, r3]
	cmp	r3, #0
	beq	.L1104
	bl	INSERT_DATA_LIST
	b	.L1105
.L1104:
	bl	INSERT_FREE_LIST
.L1105:
	ldr	r4, .L1114
	movw	r2, #3188
	mvn	r3, #0
	strh	r3, [r4, r5]	@ movhi
	mov	r3, #0
	strh	r3, [r4, r2]	@ movhi
	movw	r2, #3176
	strh	r3, [r4, r2]	@ movhi
	bl	l2p_flush
	bl	FtlVpcTblFlush
	ldr	r3, [r4, #2436]
	mov	r1, r4
	movw	r2, #2556
	cmp	r3, #0
	beq	.L1106
	ldr	r3, [r4, #2504]
	cmp	r3, #29
	bhi	.L1106
	ldrh	r3, [r4, r2]
	mov	r2, #2272
	ldrh	r2, [r4, r2]
	mvn	r1, #0
	cmp	r2, r3
	movcc	r3, r3, asl #1
	movcc	r2, #3136
	strcch	r3, [r4, r2]	@ movhi
	movw	r3, #2572
	ldr	r2, .L1114
	b	.L1113
.L1106:
	ldrh	r2, [r1, r2]
	mov	r0, #2272
	ldrh	r0, [r1, r0]
	ldr	r3, .L1114
	add	r2, r2, r2, asl #1
	cmp	r0, r2, lsr #2
	ble	.L1111
	movw	r2, #2572
	mvn	r1, #0
	strh	r1, [r3, r2]	@ movhi
	mov	r1, #20
	mov	r2, #3136
.L1113:
	strh	r1, [r3, r2]	@ movhi
	b	.L1111
.L1109:
	mov	r0, #1
	b	.L1097
.L1111:
	mov	r0, #0
.L1097:
	ldmfd	sp!, {r1, r2, r3, r4, r5, r6, r7, r8, sl, pc}
.L1115:
	.align	2
.L1114:
	.word	.LANCHOR0
	.word	.LANCHOR2
	.word	.LANCHOR0+2372
	.word	.LANCHOR3
	.word	.LC1
	.word	.LANCHOR1+526
	.fnend
	.size	FtlGcFreeTempBlock, .-FtlGcFreeTempBlock
	.align	2
	.global	Ftl_gc_temp_data_write_back
	.type	Ftl_gc_temp_data_write_back, %function
Ftl_gc_temp_data_write_back:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	stmfd	sp!, {r4, r5, r6, r7, r8, lr}
	.save {r4, r5, r6, r7, r8, lr}
	mov	r2, #0
	ldr	r4, .L1122
	mov	r3, r2
	ldr	r5, .L1122+4
	mov	r6, #0
	mov	r7, #20
	ldr	r0, [r4, #3988]
	ldr	r1, [r5, #3152]
	bl	FlashProgPages
	b	.L1117
.L1120:
	mul	r3, r7, r6
	ldr	r1, [r4, #3988]
	add	r2, r1, r3
	ldr	r1, [r1, r3]
	cmn	r1, #1
	bne	.L1118
	movw	r2, #2372
	ldr	lr, [r8, #2256]
	ldrh	ip, [r8, r2]
	mov	r4, #0
	mov	ip, ip, asl #1
	strh	r4, [lr, ip]	@ movhi
	strh	r1, [r8, r2]	@ movhi
	ldr	r2, [r8, #2720]
	add	r2, r2, #1
	str	r2, [r8, #2720]
	ldr	r2, [r0, #3988]
	add	r3, r2, r3
	ldr	r0, [r3, #4]
	ubfx	r0, r0, #10, #16
	bl	FtlBbmMapBadBlock
	bl	FtlBbmTblFlush
	bl	FtlGcPageVarInit
	mov	r0, #1
	ldmfd	sp!, {r4, r5, r6, r7, r8, pc}
.L1118:
	ldr	r3, [r2, #12]
	add	r6, r6, #1
	ldr	r1, [r2, #4]
	uxth	r6, r6
	ldr	r0, [r3, #12]
	ldr	r2, [r3, #8]
	bl	FtlGcUpdatePage
.L1117:
	ldr	r1, [r5, #3152]
	ldr	r8, .L1122+4
	cmp	r6, r1
	ldr	r0, .L1122
	bcc	.L1120
	ldr	r0, [r0, #3988]
	bl	FtlGcBufFree
	movw	r3, #2376
	ldrh	r3, [r8, r3]
	mov	r0, #0
	str	r0, [r8, #3152]
	cmp	r3, r0
	ldmnefd	sp!, {r4, r5, r6, r7, r8, pc}
	mov	r0, #1
	bl	FtlGcFreeTempBlock
	mov	r0, #1
	ldmfd	sp!, {r4, r5, r6, r7, r8, pc}
.L1123:
	.align	2
.L1122:
	.word	.LANCHOR4
	.word	.LANCHOR0
	.fnend
	.size	Ftl_gc_temp_data_write_back, .-Ftl_gc_temp_data_write_back
	.align	2
	.global	FtlGcPageRecovery
	.type	FtlGcPageRecovery, %function
FtlGcPageRecovery:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	stmfd	sp!, {r4, lr}
	.save {r4, lr}
	ldr	r4, .L1126
	ldr	r0, .L1126+4
	ldrh	r1, [r4, #72]
	bl	FtlGcScanTempBlk
	movw	r3, #2374
	ldrh	r2, [r4, r3]
	ldrh	r3, [r4, #72]
	cmp	r2, r3
	ldmccfd	sp!, {r4, pc}
	ldr	r0, .L1126+8
	bl	FtlMapBlkWriteDump_data
	mov	r0, #0
	bl	FtlGcFreeTempBlock
	ldr	r3, .L1126+12
	mov	r2, #0
	str	r2, [r3, #3800]
	ldmfd	sp!, {r4, pc}
.L1127:
	.align	2
.L1126:
	.word	.LANCHOR0
	.word	.LANCHOR0+2372
	.word	.LANCHOR3+3812
	.word	.LANCHOR3
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
	ldr	r3, .L1129
	ldr	r5, .L1129+4
	str	r4, [r3, #4032]
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
.L1130:
	.align	2
.L1129:
	.word	.LANCHOR4
	.word	.LANCHOR0+2276
	.fnend
	.size	FtlPowerLostRecovery, .-FtlPowerLostRecovery
	.align	2
	.global	Ftl_get_new_temp_ppa
	.type	Ftl_get_new_temp_ppa, %function
Ftl_get_new_temp_ppa:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	stmfd	sp!, {r3, r4, r5, lr}
	.save {r3, r4, r5, lr}
	movw	r3, #2372
	ldr	r5, .L1134
	ldrh	r2, [r5, r3]
	movw	r3, #65535
	cmp	r2, r3
	beq	.L1132
	movw	r3, #2376
	ldrh	r3, [r5, r3]
	cmp	r3, #0
	bne	.L1133
.L1132:
	bl	FtlCacheWriteBack
	mov	r0, #0
	bl	FtlGcFreeTempBlock
	ldr	r0, .L1134+4
	mov	r4, #0
	strb	r4, [r5, #2380]
	bl	allocate_data_superblock
	movw	r3, #3176
	strh	r4, [r5, r3]	@ movhi
	add	r3, r3, #12
	strh	r4, [r5, r3]	@ movhi
	bl	l2p_flush
	mov	r0, r4
	bl	FtlEctTblFlush
	bl	FtlVpcTblFlush
.L1133:
	ldr	r0, .L1134+4
	ldmfd	sp!, {r3, r4, r5, lr}
	b	get_new_active_ppa
.L1135:
	.align	2
.L1134:
	.word	.LANCHOR0
	.word	.LANCHOR0+2372
	.fnend
	.size	Ftl_get_new_temp_ppa, .-Ftl_get_new_temp_ppa
	.align	2
	.type	rk_ftl_garbage_collect.part.12, %function
rk_ftl_garbage_collect.part.12:
	.fnstart
	@ args = 0, pretend = 0, frame = 24
	@ frame_needed = 0, uses_anonymous_args = 0
	stmfd	sp!, {r4, r5, r6, r7, r8, r9, sl, fp, lr}
	.save {r4, r5, r6, r7, r8, r9, sl, fp, lr}
	movw	r3, #3192
	ldr	r4, .L1223
	mov	r7, r1
	mov	r5, r0
	movw	r0, #65535
	.pad #36
	sub	sp, sp, #36
	ldrh	r1, [r4, r3]
	cmp	r1, r0
	beq	.L1137
	movw	r2, #3190
	ldrh	ip, [r4, r2]
	cmp	ip, r0
	streqh	r1, [r4, r2]	@ movhi
	mvneq	r2, #0
	streqh	r2, [r4, r3]	@ movhi
.L1137:
	bl	FtlReadRefresh
	ldr	r1, [r4, #3140]
	movw	r3, #2572
	ldr	r8, .L1223
	ldrh	r2, [r4, r3]
	add	r1, r1, #1
	add	r1, r1, r5, asl #7
	str	r1, [r4, #3140]
	movw	r4, #65535
	cmp	r2, r4
	bne	.L1138
	movw	r3, #2372
	ldrh	r3, [r8, r3]
	cmp	r3, r2
	movne	r4, r2
	bne	.L1138
	movw	r2, #3190
	ldrh	sl, [r8, r2]
	cmp	sl, r3
	movne	r4, r3
	bne	.L1138
	mov	r9, #2272
	ldrh	r3, [r8, r9]
	cmp	r3, #23
	movhi	r3, #1024
	movls	r3, #5120
	cmp	r1, r3
	movls	r4, sl
	bls	.L1138
	ldr	r2, .L1223+4
	mov	r3, #0
	ldr	r1, .L1223+8
	mov	r6, r8
	str	r3, [r8, #3140]
	strh	r3, [r1, r2]	@ movhi
	bl	GetSwlReplaceBlock
	cmp	r0, sl
	mov	r4, r0
	bne	.L1140
	movw	sl, #3138
	ldrh	r2, [r8, r9]
	ldrh	r3, [r8, sl]
	cmp	r2, r3
	movcs	r3, #80
	strcsh	r3, [r8, sl]	@ movhi
	bcs	.L1150
	mov	r0, #64
	bl	List_get_gc_head_node
	uxth	r0, r0
	cmp	r0, r4
	beq	.L1150
	ldr	r3, .L1223+12
	ldr	r3, [r3, #3964]
	cmp	r3, #0
	bne	.L1143
	ldr	r3, [r8, #2620]
	cmp	r3, #0
	beq	.L1144
.L1143:
	ldr	r3, .L1223
	mov	r0, r0, asl #1
	ldr	r2, [r3, #2256]
	ldrh	ip, [r3, #24]
	ldrh	r1, [r2, r0]
	cmp	ip, #3
	ldrh	r2, [r3, #74]
	ldrh	r0, [r3, #4]
	mul	r0, r0, r2
	moveq	r2, r2, lsr #1
	movne	r2, #0
	add	r2, r0, r2
	cmp	r1, r2
	bgt	.L1146
	mov	r0, #0
	bl	List_get_gc_head_node
	ldr	r3, .L1223
	ldr	r2, [r3, #2440]
	ldr	r1, [r3, #2428]
	add	r2, r2, r2, asl #1
	cmp	r1, r2, lsr #2
	movw	r2, #3138
	movhi	r1, #128
	movls	r1, #160
	strh	r1, [r3, r2]	@ movhi
	uxth	r4, r0
	b	.L1148
.L1146:
	movw	r2, #3138
	mov	r1, #128
	strh	r1, [r3, r2]	@ movhi
	b	.L1150
.L1144:
	ldr	r3, [r8, #2256]
	mov	r0, r0, asl #1
	ldrh	r3, [r3, r0]
	cmp	r3, #7
	movhi	r3, #64
	strhih	r3, [r8, sl]	@ movhi
	bhi	.L1150
	mov	r0, #0
	bl	List_get_gc_head_node
	mov	r3, #128
	strh	r3, [r8, sl]	@ movhi
	uxth	r4, r0
.L1148:
	movw	r3, #65535
	cmp	r4, r3
	beq	.L1150
.L1140:
	ldr	r1, .L1223
	mov	r3, #2272
	mov	r0, r4, asl #1
	ldrh	r2, [r1, r3]
	ldr	ip, [r1, #2244]
	ldr	r3, [r1, #2256]
	ldrh	r3, [r3, r0]
	ldrh	r0, [ip, r0]
	str	r0, [sp, #0]
	mov	r0, #3136
	ldrh	r1, [r1, r0]
	ldr	r0, .L1223+16
	str	r1, [sp, #4]
	mov	r1, r4
	bl	printk
.L1150:
	bl	FtlGcReFreshBadBlk
.L1138:
	movw	r1, #65535
	rsb	r0, r1, r4
	rsbs	r2, r0, #0
	ldr	r8, .L1223
	adc	r2, r2, r0
	cmp	r5, #0
	movne	r5, #0
	andeq	r5, r2, #1
	cmp	r5, #0
	beq	.L1151
	mov	r3, #2272
	ldrh	r2, [r8, r3]
	cmp	r2, #24
	movhi	r6, #1
	bhi	.L1152
	ldrh	r6, [r8, #72]
	cmp	r2, #16
	movhi	r6, r6, lsr #5
	bhi	.L1152
	cmp	r2, #12
	movhi	r6, r6, lsr #4
	bhi	.L1152
	cmp	r2, #8
	movhi	r6, r6, lsr #2
.L1152:
	mov	r1, #3136
	ldr	r3, .L1223
	ldrh	r0, [r8, r1]
	cmp	r0, r2
	bcs	.L1156
	movw	r2, #2372
	movw	r0, #65535
	ldrh	r2, [r3, r2]
	cmp	r2, r0
	bne	.L1157
	movw	r0, #3190
	ldrh	r0, [r3, r0]
	cmp	r0, r2
	bne	.L1157
	ldr	r2, .L1223+4
	ldr	r0, .L1223+8
	ldrh	r0, [r0, r2]
	cmp	r0, #0
	bne	.L1158
	ldr	r2, [r3, #2440]
	ldr	ip, [r3, #2428]
	add	r2, r2, r2, asl #1
	cmp	ip, r2, lsr #2
	movcs	r2, #18
	bcs	.L1221
.L1158:
	ldr	r3, .L1223
	movw	r2, #2556
	mov	r1, #3136
	ldrh	r2, [r3, r2]
	add	r2, r2, r2, asl #1
	mov	r2, r2, asr #2
.L1221:
	strh	r2, [r3, r1]	@ movhi
	mov	r2, #0
	ldr	r3, .L1223
	str	r2, [r3, #3148]
	b	.L1161
.L1157:
	ldr	r3, .L1223
	movw	r2, #2556
	mov	r1, #3136
	ldrh	r2, [r3, r2]
	add	r2, r2, r2, asl #1
	mov	r2, r2, asr #2
	strh	r2, [r3, r1]	@ movhi
.L1156:
	ldr	r3, .L1223
	cmp	r7, #2
	movw	r4, #65535
	movhi	r7, #0
	movls	r7, #1
	ldr	r3, [r3, #2436]
	cmp	r3, #0
	moveq	r7, #0
	cmp	r7, #0
	addne	r6, r6, #1
	uxthne	r6, r6
	b	.L1162
.L1151:
	movw	r3, #2372
	ldrh	r3, [r8, r3]
	cmp	r3, r1
	bne	.L1163
	movw	r1, #3190
	ldrh	r1, [r8, r1]
	cmp	r1, r3
	movne	r2, #0
	andeq	r2, r2, #1
	cmp	r2, #0
	beq	.L1163
	movw	r2, #2572
	ldrh	r4, [r8, r2]
	cmp	r4, r3
	movne	r4, r3
	bne	.L1163
	mov	r3, #2272
	str	r5, [r8, #3148]
	ldrh	r7, [r8, r3]
	mov	r3, #3136
	ldrh	r2, [r8, r3]
	ldr	r6, .L1223+8
	cmp	r2, r7
	ldr	r5, .L1223+4
	bcs	.L1164
	ldrh	r2, [r6, r5]
	cmp	r2, #0
	bne	.L1165
	ldr	r2, [r8, #2440]
	ldr	r1, [r8, #2428]
	add	r2, r2, r2, asl #1
	cmp	r1, r2, lsr #2
	movcs	r2, #18
	strcsh	r2, [r8, r3]	@ movhi
	bcs	.L1167
.L1165:
	ldr	r3, .L1223
	movw	r2, #2556
	mov	r1, #3136
	ldrh	r2, [r3, r2]
	add	r2, r2, r2, asl #1
	mov	r2, r2, asr #2
	strh	r2, [r3, r1]	@ movhi
.L1167:
	bl	FtlReadRefresh
	b	.L1222
.L1164:
	ldrh	r0, [r6, r5]
	cmp	r0, #0
	bne	.L1163
	movw	r2, #2556
	ldrh	r4, [r8, r2]
	add	r2, r4, r4, asl #1
	mov	r2, r2, asr #2
	strh	r2, [r8, r3]	@ movhi
	bl	List_get_gc_head_node
	ldrh	r2, [r8, #74]
	ldrh	r1, [r8, #4]
	ldr	r3, [r8, #2256]
	mul	r2, r1, r2
	add	r2, r2, r2, lsr #31
	uxth	r0, r0
	mov	r0, r0, asl #1
	ldrh	r3, [r3, r0]
	cmp	r3, r2, asr #1
	ble	.L1168
	sub	r4, r4, #1
	cmp	r7, r4
	blt	.L1168
	bl	FtlReadRefresh
	ldrh	r0, [r6, r5]
	b	.L1161
.L1168:
	cmp	r3, #0
	movwne	r4, #65535
	bne	.L1163
	movw	r0, #65535
	bl	decrement_vpc_count
	ldr	r2, .L1223
	mov	r3, #2272
	ldrh	r0, [r2, r3]
	add	r0, r0, #1
	b	.L1161
.L1163:
	ldr	r3, .L1223
	ldr	r6, [r3, #2436]
	cmp	r6, #0
	movne	r6, #2
	moveq	r6, #1
.L1162:
	ldr	r3, .L1223
	movw	r2, #2572
	movw	r0, #65535
	ldrh	r1, [r3, r2]
	cmp	r1, r0
	bne	.L1169
	cmp	r4, r1
	strneh	r4, [r3, r2]	@ movhi
	bne	.L1171
	movw	r2, #3190
	ldrh	r1, [r3, r2]
	cmp	r1, r4
	beq	.L1171
	ldr	r0, [r3, #2256]
	mov	r1, r1, asl #1
	ldrh	r1, [r0, r1]
	cmp	r1, #0
	mvneq	r1, #0
	streqh	r1, [r3, r2]	@ movhi
	ldr	r3, .L1223
	movw	r1, #2572
	ldrh	r0, [r3, r2]
	strh	r0, [r3, r1]	@ movhi
	mvn	r1, #0
	strh	r1, [r3, r2]	@ movhi
.L1171:
	ldr	r8, .L1223
	movw	r7, #2572
	mov	r3, #0
	ldrh	r0, [r8, r7]
	strb	r3, [r8, #2580]
	movw	r3, #65535
	cmp	r0, r3
	beq	.L1169
	bl	IsBlkInGcList
	ldr	r5, .L1223
	cmp	r0, #0
	mvnne	r3, #0
	strneh	r3, [r8, r7]	@ movhi
	ldrh	r2, [r8, r7]
	movw	r3, #65535
	cmp	r2, r3
	beq	.L1169
	ldr	r0, .L1223+20
	bl	make_superblock
	movw	r1, #2574
	mov	r3, #0
	ldr	r2, .L1223+8
	strh	r3, [r5, r1]	@ movhi
	ldr	r1, .L1223+24
	strb	r3, [r5, #2578]
	strh	r3, [r2, r1]	@ movhi
	movw	r3, #2572
	ldrh	r3, [r5, r3]
	ldr	r1, [r5, #2256]
	mov	r3, r3, asl #1
	ldrh	r1, [r1, r3]
	ldr	r3, .L1223+28
	strh	r1, [r2, r3]	@ movhi
.L1169:
	ldr	r3, .L1223
	movw	r1, #2572
	movw	r0, #2276
	ldrh	r2, [r3, r1]
	ldrh	r0, [r3, r0]
	cmp	r0, r2
	beq	.L1174
	movw	r0, #2324
	ldrh	r0, [r3, r0]
	cmp	r0, r2
	bne	.L1204
.L1174:
	mvn	r2, #0
	strh	r2, [r3, r1]	@ movhi
.L1204:
	mov	fp, r4
.L1219:
	ldr	r4, .L1223
	movw	sl, #2572
	movw	r1, #65535
	ldrh	r3, [r4, sl]
	cmp	r3, r1
	bne	.L1176
	movw	r5, #3144
	mov	r3, #0
	str	r3, [r4, #3148]
.L1218:
	ldrh	r8, [r4, r5]
	mov	r0, r8
	bl	List_get_gc_head_node
	movw	r2, #65535
	uxth	r7, r0
	strh	r7, [r4, sl]	@ movhi
	cmp	r7, r2
	bne	.L1178
	ldr	r2, .L1223
	movw	r3, #3144
	mov	r1, #0
	mov	r0, #8
	strh	r1, [r2, r3]	@ movhi
	b	.L1161
.L1178:
	mov	r0, r7
	add	r8, r8, #1
	bl	IsBlkInGcList
	cmp	r0, #0
	strneh	r8, [r4, r5]	@ movhi
	bne	.L1218
	ldrh	r1, [r4, #72]
	mov	r7, r7, asl #1
	ldrh	r0, [r4, #4]
	uxth	r8, r8
	ldr	r3, [r4, #2256]
	strh	r8, [r4, r5]	@ movhi
	mul	r0, r0, r1
	ldrh	r2, [r3, r7]
	add	r1, r0, r0, lsr #31
	cmp	r2, r1, asr #1
	bgt	.L1181
	cmp	r8, #48
	bls	.L1182
	cmp	r2, #8
	bls	.L1182
	movw	r2, #3176
	ldrh	r2, [r4, r2]
	cmp	r2, #35
	bhi	.L1182
.L1181:
	mov	r2, #0
	strh	r2, [r4, r5]	@ movhi
.L1182:
	ldrh	r2, [r3, r7]
	movw	r3, #65535
	movw	r1, #2572
	cmp	r2, r0
	cmpge	fp, r3
	ldr	r3, .L1223
	movne	r0, #0
	moveq	r0, #1
	bne	.L1183
	mvn	r2, #0
	strh	r2, [r3, r1]	@ movhi
	movw	r2, #3144
	mov	r1, #0
	strh	r1, [r3, r2]	@ movhi
.L1222:
	ldr	r3, .L1223+4
	ldr	r2, .L1223+8
	ldrh	r0, [r2, r3]
	b	.L1161
.L1183:
	cmp	r2, #0
	bne	.L1184
	movw	r0, #65535
	bl	decrement_vpc_count
	ldrh	r3, [r4, r5]
	add	r3, r3, #1
	strh	r3, [r4, r5]	@ movhi
	b	.L1218
.L1184:
	ldrh	r2, [r3, r1]
	movw	r1, #2276
	strb	r0, [r3, #2580]
	ldrh	r3, [r3, r1]
	cmp	r2, r3
	bne	.L1185
	ldr	r0, .L1223+32
	movw	r2, #719
	ldr	r1, .L1223+36
	bl	printk
.L1185:
	ldr	r4, .L1223
	movw	r5, #2572
	movw	r3, #2324
	ldrh	r2, [r4, r5]
	ldrh	r3, [r4, r3]
	cmp	r2, r3
	bne	.L1186
	ldr	r0, .L1223+32
	mov	r2, #720
	ldr	r1, .L1223+36
	bl	printk
.L1186:
	movw	r3, #2372
	ldrh	r2, [r4, r5]
	ldrh	r3, [r4, r3]
	cmp	r2, r3
	bne	.L1187
	ldr	r0, .L1223+32
	movw	r2, #721
	ldr	r1, .L1223+36
	bl	printk
.L1187:
	ldr	r0, .L1223+20
	bl	make_superblock
	ldr	r2, .L1223+8
	ldr	r0, .L1223+24
	mov	r3, #0
	ldr	r4, .L1223
	strh	r3, [r2, r0]	@ movhi
	movw	r0, #2572
	ldrh	r0, [r4, r0]
	ldr	ip, [r4, #2256]
	mov	r0, r0, asl #1
	ldrh	ip, [ip, r0]
	ldr	r0, .L1223+28
	strb	r3, [r4, #2578]
	strh	ip, [r2, r0]	@ movhi
	movw	r2, #2574
	strh	r3, [r4, r2]	@ movhi
.L1176:
	ldr	r3, .L1223+12
	mov	r2, #1
	mov	r9, #0
	ldr	r4, .L1223
	str	fp, [sp, #20]
	str	r2, [r3, #3968]
	movw	r2, #2574
	ldr	r3, .L1223
	ldrh	r7, [r3, #72]
	ldrh	r3, [r3, r2]
	add	r2, r3, r6
	str	r7, [sp, #16]
	cmp	r2, r7
	rsbgt	r6, r3, r7
	uxthgt	r6, r6
	mov	sl, r6
	b	.L1189
.L1191:
	ldrh	r1, [r2, #2]!
	movw	r7, #65535
	add	r3, r3, #1
	cmp	r1, r7
	uxth	r3, r3
	orrne	r1, ip, r1, asl #10
	mlane	r7, r0, r6, lr
	addne	r6, r6, #1
	uxthne	r6, r6
	strne	r1, [r7, #4]
.L1198:
	cmp	r3, r5
	bne	.L1191
	ldr	r0, [r4, #3168]
	mov	r1, r6
	ldrb	r2, [r4, #2580]	@ zero_extendqisi2
	mov	r5, #0
	bl	FlashReadPages
	mov	fp, r5
	b	.L1192
.L1196:
	ldr	r3, [r4, #3168]
	add	r2, r3, r5
	ldr	r3, [r3, r5]
	ldr	r7, [r2, #12]
	cmn	r3, #1
	beq	.L1193
	ldrh	r3, [r7, #0]
	movw	r1, #61589
	cmp	r3, r1
	bne	.L1193
	ldr	r8, [r7, #8]
	cmn	r8, #1
	bne	.L1194
	ldr	r0, .L1223+32
	movw	r2, #753
	ldr	r1, .L1223+36
	bl	printk
.L1194:
	add	r1, sp, #28
	mov	r2, #0
	mov	r0, r8
	bl	log2phys
	ldr	r1, [r4, #3168]
	ldr	r2, [sp, #28]
	add	r1, r1, r5
	ldr	r3, [r1, #4]
	cmp	r2, r3
	bne	.L1193
	ldr	r2, .L1223+8
	mov	r8, #20
	ldr	r3, .L1223+24
	ldr	r1, [r1, #16]
	ldrh	r0, [r2, r3]
	add	r0, r0, #1
	strh	r0, [r2, r3]	@ movhi
	ldr	r3, .L1223+12
	ldr	r0, [r4, #3152]
	ldr	r2, [r3, #3988]
	mla	r2, r8, r0, r2
	str	r1, [r2, #16]
	str	r2, [sp, #12]
	str	r3, [sp, #8]
	bl	Ftl_get_new_temp_ppa
	ldr	r2, [sp, #12]
	mov	r1, #1
	str	r0, [r2, #4]
	ldr	r3, [sp, #8]
	ldr	r2, [r4, #3152]
	ldr	r3, [r3, #3988]
	mla	r8, r8, r2, r3
	ldr	r3, [r4, #3168]
	add	r3, r3, r5
	ldr	r2, [r3, #8]
	str	r2, [r8, #8]
	ldr	r3, [r3, #12]
	str	r3, [r8, #12]
	ldr	r3, [sp, #28]
	str	r3, [r7, #12]
	movw	r3, #2372
	ldrh	r3, [r4, r3]
	strh	r3, [r7, #2]	@ movhi
	ldr	r3, [r4, #2480]
	ldr	r0, [r4, #3168]
	str	r3, [r7, #4]
	add	r0, r0, r5
	ldr	r3, [r4, #3152]
	add	r3, r3, #1
	str	r3, [r4, #3152]
	bl	FtlGcBufAlloc
	ldrb	r3, [r4, #2379]	@ zero_extendqisi2
	ldr	r2, [r4, #3152]
	cmp	r2, r3
	beq	.L1195
	movw	r3, #2376
	ldrh	r3, [r4, r3]
	cmp	r3, #0
	bne	.L1193
.L1195:
	bl	Ftl_gc_temp_data_write_back
	cmp	r0, #0
	ldrne	r3, .L1223+12
	movne	r2, #0
	strne	r2, [r3, #3968]
	bne	.L1222
.L1193:
	add	fp, fp, #1
	add	r5, r5, #20
	uxth	fp, fp
.L1192:
	cmp	fp, r6
	bne	.L1196
	add	r9, r9, #1
.L1189:
	uxth	r3, r9
	ldr	r2, .L1223
	cmp	r3, sl
	movw	r3, #2574
	bcs	.L1197
	ldrh	ip, [r4, r3]
	mov	r6, #0
	ldrh	r5, [r4, #4]
	mov	r3, r6
	ldr	lr, [r4, #3168]
	add	ip, ip, r9
	ldr	r2, .L1223+40
	mov	r0, #20
	b	.L1198
.L1197:
	ldrh	r1, [r2, r3]
	ldr	fp, [sp, #20]
	add	r6, sl, r1
	uxth	r6, r6
	strh	r6, [r2, r3]	@ movhi
	ldr	r3, [sp, #16]
	cmp	r6, r3
	bcs	.L1199
	ldr	r3, .L1223+8
	ldr	r2, .L1223+24
	ldr	r1, .L1223+28
	ldrh	r2, [r3, r2]
	ldrh	r3, [r3, r1]
	cmp	r2, r3
	bne	.L1200
.L1199:
	ldr	r3, .L1223
	ldr	r5, .L1223+8
	ldr	r3, [r3, #3152]
	cmp	r3, #0
	beq	.L1201
	bl	Ftl_gc_temp_data_write_back
	cmp	r0, #0
	beq	.L1201
	ldr	r3, .L1223+12
	mov	r2, #0
	str	r2, [r3, #3968]
	ldr	r3, .L1223+4
	ldrh	r0, [r5, r3]
	b	.L1161
.L1201:
	ldr	r3, .L1223+24
	ldrh	ip, [r5, r3]
	cmp	ip, #0
	bne	.L1202
	ldr	r3, .L1223
	movw	r2, #2572
	ldrh	r1, [r3, r2]
	ldr	r0, [r3, #2256]
	mov	r1, r1, asl #1
	ldrh	r4, [r0, r1]
	cmp	r4, #0
	beq	.L1202
	strh	ip, [r0, r1]	@ movhi
	ldrh	r0, [r3, r2]
	bl	update_vpc_list
	bl	FtlCacheWriteBack
	bl	l2p_flush
	bl	FtlVpcTblFlush
.L1202:
	ldr	r2, .L1223
	movw	r3, #2572
	mvn	r1, #0
	strh	r1, [r2, r3]	@ movhi
.L1200:
	ldr	r3, .L1223+12
	mov	r2, #0
	str	r2, [r3, #3968]
	mov	r3, #2272
	ldr	r2, .L1223
	ldrh	r3, [r2, r3]
	cmp	r3, #2
	ldrlsh	r6, [r2, #72]
	bls	.L1219
.L1203:
	ldr	r2, .L1223+4
	ldr	r1, .L1223+8
	ldrh	r0, [r1, r2]
	cmp	r0, #0
	addeq	r0, r3, #1
.L1161:
	add	sp, sp, #36
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, sl, fp, pc}
.L1224:
	.align	2
.L1223:
	.word	.LANCHOR0
	.word	-4016
	.word	.LANCHOR5
	.word	.LANCHOR4
	.word	.LC101
	.word	.LANCHOR0+2572
	.word	-4014
	.word	-4012
	.word	.LC1
	.word	.LANCHOR1+545
	.word	.LANCHOR0+2586
	.fnend
	.size	rk_ftl_garbage_collect.part.12, .-rk_ftl_garbage_collect.part.12
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
	ldr	r3, .L1231
	mov	r5, r1
	ldr	r0, [r3, #3968]
	cmp	r0, #0
	movne	r0, #0
	ldmnefd	sp!, {r3, r4, r5, pc}
	ldr	r2, .L1231+4
	movw	r3, #2264
	ldrh	r3, [r2, r3]
	cmp	r3, #47
	ldmlsfd	sp!, {r3, r4, r5, pc}
	ldr	r3, .L1231+8
	ldrh	r1, [r3, #4]
	movw	r3, #65535
	cmp	r1, r3
	beq	.L1227
	movw	r1, #2372
	ldrh	r2, [r2, r1]
	cmp	r2, r3
	beq	.L1227
	mov	r0, #1
	bl	FtlGcFreeTempBlock
	cmp	r0, #0
	bne	.L1230
.L1227:
	mov	r0, r4
	mov	r1, r5
	ldmfd	sp!, {r3, r4, r5, lr}
	b	rk_ftl_garbage_collect.part.12
.L1230:
	mov	r0, #1
	ldmfd	sp!, {r3, r4, r5, pc}
.L1232:
	.align	2
.L1231:
	.word	.LANCHOR4
	.word	.LANCHOR0
	.word	.LANCHOR2
	.fnend
	.size	rk_ftl_garbage_collect, .-rk_ftl_garbage_collect
	.align	2
	.global	sftl_gc
	.type	sftl_gc, %function
sftl_gc:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	@ link register save eliminated.
	mov	r0, #1
	mov	r1, r0
	b	rk_ftl_garbage_collect
	.fnend
	.size	sftl_gc, .-sftl_gc
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
	ldr	r5, .L1243
	mov	r7, r0
	mov	r4, r1
	ldr	r3, [r5, #112]
	cmp	r2, r3
	mvnhi	r0, #0
	bhi	.L1235
	cmp	r1, #31
	movls	r0, #0
	bls	.L1235
	bl	FtlCacheWriteBack
	ldrh	r6, [r5, #78]
	mov	r0, r7
	mov	r1, r6
	bl	__aeabi_uidiv
	mls	r7, r0, r6, r7
	mov	r5, r0
	uxth	r7, r7
	cmp	r7, #0
	beq	.L1236
	rsb	r6, r7, r6
	add	r5, r0, #1
	cmp	r6, r4
	movcs	r6, r4
	uxth	r6, r6
	rsb	r4, r6, r4
.L1236:
	mvn	r3, #0
	ldr	r6, .L1243
	str	r3, [sp, #4]
	ldr	r7, .L1243+4
	b	.L1237
.L1239:
	mov	r0, r5
	mov	r1, sp
	mov	r2, #0
	bl	log2phys
	ldr	r3, [sp, #0]
	cmn	r3, #1
	beq	.L1238
	ldr	r3, [r7, #-4008]
	add	r1, sp, #4
	mov	r2, #1
	mov	r0, r5
	add	r3, r3, #1
	str	r3, [r7, #-4008]
	ldr	r3, [r6, #2448]
	add	r3, r3, #1
	str	r3, [r6, #2448]
	bl	log2phys
	ldr	r0, [sp, #0]
	ubfx	r0, r0, #10, #16
	bl	P2V_block_in_plane
	bl	decrement_vpc_count
.L1238:
	ldrh	r3, [r6, #78]
	add	r5, r5, #1
	rsb	r4, r3, r4
.L1237:
	ldrh	r3, [r6, #78]
	cmp	r4, r3
	bcs	.L1239
	ldr	r3, .L1243+4
	mov	r4, #0
	ldr	r2, [r3, #-4008]
	cmp	r2, #32
	bls	.L1242
	str	r4, [r3, #-4008]
	bl	l2p_flush
	bl	FtlVpcTblFlush
.L1242:
	mov	r0, r4
.L1235:
	ldmfd	sp!, {r1, r2, r3, r4, r5, r6, r7, pc}
.L1244:
	.align	2
.L1243:
	.word	.LANCHOR0
	.word	.LANCHOR5
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
	.global	sftl_deinit
	.type	sftl_deinit, %function
sftl_deinit:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	stmfd	sp!, {r3, lr}
	.save {r3, lr}
	ldr	r3, .L1248
	ldr	r3, [r3, #0]
	cmp	r3, #1
	bne	.L1247
	bl	FtlSysFlush
.L1247:
	mov	r0, #0
	ldmfd	sp!, {r3, pc}
.L1249:
	.align	2
.L1248:
	.word	.LANCHOR2
	.fnend
	.size	sftl_deinit, .-sftl_deinit
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
	ldr	r6, .L1257
	ldr	r1, .L1257+4
	ldr	r0, .L1257+8
	bl	printk
	ldr	r5, .L1257+12
	ldrh	r2, [r6, #14]
	mov	r1, #0
	ldr	r0, [r5, #4020]
	mov	r2, r2, asl #1
	bl	ftl_memset
	b	.L1251
.L1253:
	mov	r0, r4
	add	r1, sp, #4
	mov	r2, #0
	bl	log2phys
	ldr	r0, [sp, #4]
	cmn	r0, #1
	beq	.L1252
	ubfx	r0, r0, #10, #16
	bl	P2V_block_in_plane
	ldr	r3, [r5, #4020]
	mov	r0, r0, asl #1
	ldrh	r2, [r3, r0]
	add	r2, r2, #1
	strh	r2, [r3, r0]	@ movhi
.L1252:
	add	r4, r4, #1
.L1251:
	ldr	r3, [r6, #2440]
	cmp	r4, r3
	bcc	.L1253
	mov	r4, #0
	ldr	r5, .L1257
	ldr	r7, .L1257+12
	movw	r8, #65535
	b	.L1254
.L1256:
	ldr	r3, [r5, #2256]
	mov	r6, r4, asl #1
	ldrh	r2, [r3, r6]
	ldr	r3, [r7, #4020]
	ldrh	r3, [r3, r6]
	cmp	r2, r3
	beq	.L1255
	cmp	r2, r8
	beq	.L1255
	ldr	r0, .L1257+16
	mov	r1, r4
	bl	printk
	movw	r3, #2276
	ldrh	r3, [r5, r3]
	cmp	r3, r4
	beq	.L1255
	movw	r3, #2372
	ldrh	r3, [r5, r3]
	cmp	r3, r4
	beq	.L1255
	movw	r3, #2324
	ldrh	r3, [r5, r3]
	cmp	r3, r4
	beq	.L1255
	ldr	r3, [r7, #4020]
	mov	r0, r4
	ldrh	r2, [r3, r6]
	ldr	r3, [r5, #2256]
	strh	r2, [r3, r6]	@ movhi
	bl	update_vpc_list
	bl	l2p_flush
	bl	FtlVpcTblFlush
.L1255:
	add	r4, r4, #1
	uxth	r4, r4
.L1254:
	ldrh	r3, [r5, #12]
	cmp	r3, r4
	bhi	.L1256
	ldmfd	sp!, {r2, r3, r4, r5, r6, r7, r8, pc}
.L1258:
	.align	2
.L1257:
	.word	.LANCHOR0
	.word	.LANCHOR1+568
	.word	.LC87
	.word	.LANCHOR4
	.word	.LC102
	.fnend
	.size	FtlVpcCheckAndModify, .-FtlVpcCheckAndModify
	.align	2
	.global	FtlSysBlkInit
	.type	FtlSysBlkInit, %function
FtlSysBlkInit:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	stmfd	sp!, {r3, r4, r5, lr}
	.save {r3, r4, r5, lr}
	movw	r3, #4030
	ldr	r2, .L1270
	mov	r1, #0
	ldr	r4, .L1270+4
	strh	r1, [r2, r3]	@ movhi
	ldrh	r0, [r4, #8]
	bl	FtlFreeSysBlkQueueInit
	bl	FtlScanSysBlk
	mov	r3, #2560
	ldrh	r2, [r4, r3]
	movw	r3, #65535
	cmp	r2, r3
	beq	.L1268
	bl	FtlLoadSysInfo
	subs	r5, r0, #0
	bne	.L1268
	bl	FtlLoadMapInfo
	bl	FtlLoadVonderInfo
	bl	Ftl_load_ext_data
	bl	FtlLoadEctTbl
	bl	FtlFreeSysBLkSort
	bl	SupperBlkListInit
	bl	FtlPowerLostRecovery
	mov	r0, #1
	bl	FtlUpdateVaildLpn
	ldrh	r2, [r4, #110]
	ldr	r1, [r4, #2420]
	mov	r3, r5
	b	.L1261
.L1263:
	add	r0, r1, r5
	add	r5, r5, #12
	ldr	r0, [r0, #4]
	cmp	r0, #0
	blt	.L1262
	add	r3, r3, #1
.L1261:
	cmp	r3, r2
	blt	.L1263
.L1262:
	ldr	r4, .L1270+4
	movw	r1, #2536
	cmp	r3, r2
	ldrh	r0, [r4, r1]
	add	r0, r0, #1
	strh	r0, [r4, r1]	@ movhi
	blt	.L1264
	ldr	r2, .L1270
	movw	r3, #4030
	ldrh	r3, [r2, r3]
	cmp	r3, #0
	beq	.L1265
.L1264:
	ldr	r0, .L1270+8
	bl	FtlSuperblockPowerLostFix
	ldr	r0, .L1270+12
	bl	FtlSuperblockPowerLostFix
	movw	r3, #2276
	ldrh	r3, [r4, r3]
	movw	r2, #2280
	ldr	r1, [r4, #2256]
	ldrh	r0, [r4, r2]
	mov	r3, r3, asl #1
	ldrh	ip, [r1, r3]
	rsb	r0, r0, ip
	strh	r0, [r1, r3]	@ movhi
	ldrh	r1, [r4, #72]
	movw	r3, #2278
	ldr	r0, [r4, #2256]
	strh	r1, [r4, r3]	@ movhi
	mov	r3, #0
	strh	r3, [r4, r2]	@ movhi
	add	r2, r2, #44
	strb	r3, [r4, #2282]
	ldrh	r1, [r4, r2]
	add	r2, r2, #4
	ldrh	ip, [r4, r2]
	mov	r1, r1, asl #1
	ldrh	lr, [r0, r1]
	rsb	ip, ip, lr
	strh	ip, [r0, r1]	@ movhi
	ldrh	r0, [r4, #72]
	movw	r1, #2326
	strh	r3, [r4, r2]	@ movhi
	strb	r3, [r4, #2330]
	strh	r0, [r4, r1]	@ movhi
	ldr	r0, .L1270+16
	bl	FtlMapBlkWriteDump_data
	ldr	r0, .L1270+20
	bl	FtlMapBlkWriteDump_data
	movw	r3, #2538
	ldrh	r2, [r4, r3]
	add	r2, r2, #1
	strh	r2, [r4, r3]	@ movhi
	bl	l2p_flush
	bl	FtlVpcTblFlush
	bl	FtlVpcTblFlush
.L1265:
	ldr	r4, .L1270+4
	movw	r3, #2276
	ldrh	r0, [r4, r3]
	movw	r3, #65535
	cmp	r0, r3
	beq	.L1266
	movw	r3, #2280
	ldrh	r3, [r4, r3]
	cmp	r3, #0
	bne	.L1266
	movw	r3, #2328
	ldrh	r3, [r4, r3]
	cmp	r3, #0
	bne	.L1266
	bl	FtlGcRefreshBlock
	movw	r3, #2324
	ldrh	r0, [r4, r3]
	bl	FtlGcRefreshBlock
	ldr	r0, .L1270+8
	bl	allocate_new_data_superblock
	ldr	r0, .L1270+12
	bl	allocate_new_data_superblock
.L1266:
	ldr	r2, .L1270+4
	movw	r3, #2536
	ldrh	r0, [r2, r3]
	ands	r4, r0, #31
	bne	.L1269
	bl	FtlVpcCheckAndModify
	mov	r0, r4
	ldmfd	sp!, {r3, r4, r5, pc}
.L1268:
	mvn	r0, #0
	ldmfd	sp!, {r3, r4, r5, pc}
.L1269:
	mov	r0, #0
	ldmfd	sp!, {r3, r4, r5, pc}
.L1271:
	.align	2
.L1270:
	.word	.LANCHOR4
	.word	.LANCHOR0
	.word	.LANCHOR0+2276
	.word	.LANCHOR0+2324
	.word	.LANCHOR3+3812
	.word	.LANCHOR3+3876
	.fnend
	.size	FtlSysBlkInit, .-FtlSysBlkInit
	.align	2
	.global	sftl_init
	.type	sftl_init, %function
sftl_init:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
.L1273:
	stmfd	sp!, {r3, r4, r5, lr}
	.save {r3, r4, r5, lr}
	mvn	r3, #0
	ldr	r4, .L1277
	ldr	r5, .L1277+4
	ldr	r1, .L1277+8
	str	r3, [r4, #0]
	ldr	r0, .L1277+12
	bl	printk
	ldr	r0, .L1277+16
	bl	FtlConstantsInit
	bl	FtlMemInit
	bl	FtlVariablesInit
	ldrh	r0, [r5, #8]
	bl	FtlFreeSysBlkQueueInit
	bl	FtlLoadBbt
	cmp	r0, #0
	bne	.L1274
	bl	FtlSysBlkInit
	cmp	r0, #0
	bne	.L1274
	mov	r3, #1
	str	r3, [r4, #0]
	mov	r3, #2272
	ldrh	r3, [r5, r3]
	cmp	r3, #15
	bhi	.L1274
	movw	r4, #8129
.L1275:
	mov	r0, #0
	mov	r1, #1
	bl	rk_ftl_garbage_collect
	subs	r4, r4, #1
	bne	.L1275
.L1274:
	mov	r0, #0
	ldmfd	sp!, {r3, r4, r5, pc}
.L1278:
	.align	2
.L1277:
	.word	.LANCHOR2
	.word	.LANCHOR0
	.word	.LC71
	.word	.LC70
	.word	g_nand_phy_info
	.fnend
	.size	sftl_init, .-sftl_init
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
	mov	r6, #0
	ldr	r7, .L1296
	bl	FtlLoadFactoryBbt
	ldr	r4, .L1296+4
	add	sl, r7, #12
	sub	r5, r7, #124
	b	.L1280
.L1286:
	ldrh	r3, [sl], #2
	movw	r2, #65535
	ldr	r0, [r5, #2240]
	ldr	r9, [r4, #-836]
	cmp	r3, r2
	str	r0, [r4, #-848]
	str	r9, [r4, #-844]
	beq	.L1281
	ldrh	r8, [r5, #68]
	mov	r1, #1
	mov	r2, r1
	ldr	r0, .L1296+8
	mla	r8, r8, r6, r3
	mov	r3, r8, asl #10
	str	r3, [r4, #-852]
	bl	FlashReadPages
	ldrh	r2, [r5, #68]
	ldr	r0, [r7, #28]
	add	r2, r2, #7
	ldr	r1, [r4, #-848]
	mov	r2, r2, lsr #3
	bl	memcpy
	b	.L1282
.L1281:
	mov	r1, r6
	bl	FlashGetBadBlockList
	ldr	r0, [r4, #-848]
	ldr	r1, [r7, #28]
	bl	FtlBbt2Bitmap
	ldrh	fp, [r5, #68]
.L1284:
	sub	fp, fp, #1
	uxth	fp, fp
.L1295:
	ldrh	r0, [r5, #68]
	mla	r0, r0, r6, fp
	uxth	r0, r0
	bl	FtlBbmIsBadBlock
	cmp	r0, #1
	beq	.L1284
	mov	r1, #0
	mov	r2, #16
	strh	fp, [sl, #-2]	@ movhi
	ldr	r0, [r4, #-836]
	bl	ftl_memset
	mov	r1, #0
	mov	r2, #4096
	ldr	r0, [r5, #2240]
	bl	ftl_memset
	ldr	r3, .L1296+12
	ldr	r2, .L1296+16
	strh	r3, [r9, #0]	@ movhi
	mov	r3, #0
	str	r3, [r9, #4]
	ldrh	r3, [sl, #-2]
	ldrh	r8, [r5, #68]
	strh	r3, [r9, #2]	@ movhi
	ldrh	r3, [sl, #-2]
	ldr	r1, [r7, #28]
	ldr	r0, [r4, #-848]
	mla	r8, r8, r6, r3
	mov	r3, r8, asl #10
	str	r3, [r4, #-852]
	movw	r3, #3940
	ldrh	r2, [r2, r3]
	mov	r2, r2, asl #2
	bl	memcpy
	mov	r1, #1
	mov	r2, r1
	ldr	r0, .L1296+8
	bl	FlashEraseBlocks
	mov	r1, #1
	mov	r3, r1
	ldr	r0, .L1296+8
	mov	r2, r1
	bl	FlashProgPages
	ldr	r3, [r4, #-856]
	cmn	r3, #1
	bne	.L1282
	uxth	r0, r8
	bl	FtlBbmMapBadBlock
	b	.L1295
.L1282:
	uxth	r0, r8
	add	r6, r6, #1
	bl	FtlBbmMapBadBlock
	add	r7, r7, #4
.L1280:
	ldrh	r3, [r5, #26]
	cmp	r6, r3
	bcc	.L1286
	mov	r4, #0
	ldr	r6, .L1296+20
	b	.L1287
.L1288:
	mov	r0, r4
	add	r4, r4, #1
	bl	FtlBbmMapBadBlock
	uxth	r4, r4
.L1287:
	ldrh	r3, [r6, #86]
	ldr	r5, .L1296+20
	cmp	r3, r4
	bhi	.L1288
	ldrh	r4, [r5, #136]
	movw	r6, #65535
	sub	r4, r4, #1
	uxth	r4, r4
	b	.L1289
.L1294:
	mov	r0, r4
	bl	FtlBbmIsBadBlock
	cmp	r0, #1
	beq	.L1290
	mov	r0, r4
	bl	FlashTestBlk
	cmp	r0, #0
	beq	.L1291
	mov	r0, r4
	bl	FtlBbmMapBadBlock
	b	.L1290
.L1291:
	ldrh	r3, [r5, #124]
	cmp	r3, r6
	streqh	r4, [r5, #124]	@ movhi
.L1292:
	ldrne	r3, .L1296+20
	strneh	r4, [r3, #128]	@ movhi
	bne	.L1293
.L1290:
	sub	r4, r4, #1
	uxth	r4, r4
.L1289:
	ldrh	r3, [r5, #136]
	sub	r3, r3, #48
	cmp	r4, r3
	bgt	.L1294
.L1293:
	ldr	r4, .L1296+20
	mov	r5, #0
	ldr	r3, .L1296+4
	mov	r1, #1
	mov	r2, #2
	str	r5, [r4, #132]
	ldr	r0, [r3, #3776]
	ldrh	r3, [r4, #124]
	strh	r5, [r4, #126]	@ movhi
	mov	r3, r3, asl #10
	str	r3, [r0, #4]
	ldrh	r3, [r4, #128]
	mov	r3, r3, asl #10
	str	r3, [r0, #24]
	bl	FlashEraseBlocks
	ldrh	r0, [r4, #124]
	bl	FtlBbmMapBadBlock
	ldrh	r0, [r4, #128]
	bl	FtlBbmMapBadBlock
	bl	FtlBbmTblFlush
	ldr	r3, [r4, #132]
	ldrh	r2, [r4, #128]
	add	r3, r3, #1
	str	r3, [r4, #132]
	ldrh	r3, [r4, #124]
	strh	r5, [r4, #126]	@ movhi
	strh	r2, [r4, #124]	@ movhi
	strh	r3, [r4, #128]	@ movhi
	bl	FtlBbmTblFlush
	mov	r0, r5
	ldmfd	sp!, {r3, r4, r5, r6, r7, r8, r9, sl, fp, pc}
.L1297:
	.align	2
.L1296:
	.word	.LANCHOR0+124
	.word	.LANCHOR3
	.word	.LANCHOR3-856
	.word	-3872
	.word	.LANCHOR4
	.word	.LANCHOR0
	.fnend
	.size	FtlMakeBbt, .-FtlMakeBbt
	.align	2
	.global	ftl_low_format
	.type	ftl_low_format, %function
ftl_low_format:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	ldr	r3, .L1318
	mov	r2, #0
	stmfd	sp!, {r4, r5, r6, r7, r8, r9, sl, lr}
	.save {r4, r5, r6, r7, r8, r9, sl, lr}
	str	r2, [r3, #2476]
	str	r2, [r3, #2480]
	ldrh	r0, [r3, #8]
	bl	FtlFreeSysBlkQueueInit
	bl	FtlLoadBbt
	cmp	r0, #0
	beq	.L1299
	bl	FtlMakeBbt
.L1299:
	mov	r3, #0
	ldr	r0, .L1318
	ldr	r2, .L1318+4
	ldr	r1, .L1318+8
	b	.L1300
.L1301:
	ldr	ip, [r2, #3792]
	mvn	lr, r3
	orr	lr, r3, lr, asl #16
	str	lr, [ip, r3, asl #2]
	ldr	ip, [r2, #3784]
	str	r1, [ip, r3, asl #2]
	add	r3, r3, #1
	uxth	r3, r3
.L1300:
	ldrh	ip, [r0, #78]
	ldr	r7, .L1318
	cmp	r3, ip, asl #7
	blt	.L1301
	ldrh	r6, [r7, #12]
	mov	r5, #0
	b	.L1302
.L1303:
	mov	r0, r6
	mov	r1, #1
	bl	FtlLowFormatEraseBlock
	add	r6, r6, #1
	uxth	r6, r6
	add	r5, r5, r0
	uxth	r5, r5
.L1302:
	ldrh	r3, [r7, #14]
	ldr	r4, .L1318
	cmp	r3, r6
	bhi	.L1303
	ldrh	r1, [r4, #4]
	sub	r3, r5, #3
	cmp	r3, r1, asl #1
	bge	.L1304
.L1308:
	mov	r4, #0
	ldr	r7, .L1318
	mov	r6, r4
	b	.L1305
.L1304:
	mov	r0, r5
	bl	__aeabi_uidiv
	ldr	r3, [r4, #104]
	add	r0, r0, r3
	uxth	r0, r0
	bl	FtlSysBlkNumInit
	ldrh	r0, [r4, #8]
	bl	FtlFreeSysBlkQueueInit
	ldrh	r5, [r4, #12]
	b	.L1306
.L1307:
	mov	r0, r5
	mov	r1, #1
	bl	FtlLowFormatEraseBlock
	add	r5, r5, #1
	uxth	r5, r5
.L1306:
	ldrh	r3, [r4, #14]
	cmp	r3, r5
	bhi	.L1307
	b	.L1308
.L1309:
	mov	r0, r6
	mov	r1, #0
	bl	FtlLowFormatEraseBlock
	add	r6, r6, #1
	uxth	r6, r6
	add	r4, r4, r0
	uxth	r4, r4
.L1305:
	ldrh	r3, [r7, #12]
	ldr	r5, .L1318
	cmp	r3, r6
	bhi	.L1309
	ldr	r3, .L1318+4
	movw	sl, #2556
	ldrh	r2, [r5, #14]
	ldrh	r6, [r5, #4]
	ldr	r9, [r5, #16]
	str	r2, [r3, #3780]
	mov	r1, r6
	mov	r0, r9
	bl	__aeabi_uidiv
	ubfx	r8, r0, #5, #16
	mov	r7, r0
	add	r3, r8, #36
	strh	r3, [r5, sl]	@ movhi
	mov	r3, #24
	str	r0, [r5, #2440]
	mul	r3, r3, r6
	cmp	r4, r3
	ble	.L1310
	rsb	r0, r4, r9
	mov	r1, r6
	bl	__aeabi_uidiv
	str	r0, [r5, #2440]
	mov	r0, r0, lsr #5
	add	r0, r0, #24
	strh	r0, [r5, sl]	@ movhi
.L1310:
	ldr	r5, .L1318
	ldr	r3, [r5, #2436]
	cmp	r3, #1
	bne	.L1311
	mov	r0, r4
	mov	r1, r6
	bl	__aeabi_uidiv
	movw	sl, #2556
	ldrh	r9, [r5, sl]
	uxtah	r0, r9, r0
	add	r9, r9, r0, lsr #2
	strh	r9, [r5, sl]	@ movhi
.L1311:
	ldr	r3, .L1318
	ldrh	r2, [r3, #64]
	cmp	r2, #0
	beq	.L1312
	movw	r1, #2556
	ldrh	r0, [r3, r1]
	add	r0, r0, r2, lsr #1
	strh	r0, [r3, r1]	@ movhi
	mul	r0, r6, r2
	cmp	r0, r4
	strgt	r7, [r3, #2440]
	addgt	r2, r2, #32
	addgt	r8, r8, r2
	strgth	r8, [r3, r1]	@ movhi
.L1312:
	ldr	r4, .L1318
	movw	r3, #2556
	mvn	r5, #0
	ldr	r2, [r4, #2440]
	ldrh	r3, [r4, r3]
	rsb	r3, r3, r2
	mul	r6, r6, r3
	ldr	r3, .L1318+12
	str	r6, [r3, #4024]
	ldrh	r3, [r4, #72]
	mul	r6, r3, r6
	ldrh	r3, [r4, #78]
	str	r6, [r4, #2440]
	mul	r6, r3, r6
	str	r6, [r4, #112]
	movw	r6, #2276
	bl	FtlBbmTblFlush
	ldrh	r2, [r4, #14]
	mov	r1, #0
	ldr	r0, [r4, #2256]
	mov	r2, r2, asl #1
	bl	ftl_memset
	movw	r2, #2572
	movw	r1, #2574
	strh	r5, [r4, r2]	@ movhi
	ldrh	r2, [r4, #12]
	mov	r3, #0
	strh	r3, [r4, r1]	@ movhi
	sub	r1, r1, #296
	str	r3, [r4, #2428]
	strh	r3, [r4, r1]	@ movhi
	mov	r2, r2, lsr #3
	strb	r3, [r4, #2578]
	mov	r1, #255
	strb	r3, [r4, #2580]
	strb	r3, [r4, #2282]
	strh	r3, [r4, r6]	@ movhi
	mov	r3, #1
	ldr	r0, [r4, #2432]
	strb	r3, [r4, #2284]
	bl	ftl_memset
.L1313:
	ldr	r0, .L1318+16
	bl	make_superblock
	ldrb	r3, [r4, #2283]	@ zero_extendqisi2
	ldr	r7, .L1318
	cmp	r3, #0
	movw	r3, #2276
	bne	.L1314
	ldrh	r3, [r4, r6]
	ldr	r2, [r4, #2256]
	mov	r3, r3, asl #1
	strh	r5, [r2, r3]	@ movhi
	ldrh	r3, [r4, r6]
	add	r3, r3, #1
	strh	r3, [r4, r6]	@ movhi
	b	.L1313
.L1314:
	ldr	r2, [r7, #2476]
	movw	r5, #2324
	ldr	r1, [r7, #2256]
	mvn	r6, #0
	str	r2, [r7, #2288]
	add	r2, r2, #1
	str	r2, [r7, #2476]
	movw	r2, #2280
	ldrh	r0, [r7, r2]
	ldrh	r2, [r7, r3]
	mov	r2, r2, asl #1
	strh	r0, [r1, r2]	@ movhi
	movw	r0, #2326
	ldrh	r3, [r7, r3]
	mov	r2, #0
	strh	r2, [r7, r0]	@ movhi
	add	r3, r3, #1
	strb	r2, [r7, #2330]
	strh	r3, [r7, r5]	@ movhi
	mov	r3, #1
	strb	r3, [r7, #2332]
.L1315:
	ldr	r0, .L1318+20
	bl	make_superblock
	ldrb	r3, [r7, #2331]	@ zero_extendqisi2
	ldr	r4, .L1318
	cmp	r3, #0
	bne	.L1316
	ldrh	r3, [r7, r5]
	ldr	r2, [r7, #2256]
	mov	r3, r3, asl #1
	strh	r6, [r2, r3]	@ movhi
	ldrh	r3, [r7, r5]
	add	r3, r3, #1
	strh	r3, [r7, r5]	@ movhi
	b	.L1315
.L1316:
	ldr	r3, [r4, #2476]
	mvn	r5, #0
	ldr	r2, [r4, #2256]
	str	r3, [r4, #2336]
	add	r3, r3, #1
	str	r3, [r4, #2476]
	movw	r3, #2328
	ldrh	r1, [r4, r3]
	movw	r3, #2324
	ldrh	r3, [r4, r3]
	mov	r3, r3, asl #1
	strh	r1, [r2, r3]	@ movhi
	movw	r3, #2372
	strh	r5, [r4, r3]	@ movhi
	bl	FtlFreeSysBlkQueueOut
	mov	r3, #2560
	mov	r2, #0
	strh	r0, [r4, r3]	@ movhi
	add	r3, r3, #2
	strh	r2, [r4, r3]	@ movhi
	add	r3, r3, #2
	strh	r5, [r4, r3]	@ movhi
	ldr	r3, .L1318+12
	ldr	r2, [r3, #4024]
	movw	r3, #2566
	strh	r2, [r4, r3]	@ movhi
	ldr	r3, [r4, #2476]
	str	r3, [r4, #2568]
	add	r3, r3, #1
	str	r3, [r4, #2476]
	bl	FtlVpcTblFlush
	bl	FtlSysBlkInit
	cmp	r0, #0
	mov	r0, #0
	ldreq	r3, .L1318+24
	moveq	r2, #1
	streq	r2, [r3, #0]
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, sl, pc}
.L1319:
	.align	2
.L1318:
	.word	.LANCHOR0
	.word	.LANCHOR3
	.word	168778952
	.word	.LANCHOR4
	.word	.LANCHOR0+2276
	.word	.LANCHOR0+2324
	.word	.LANCHOR2
	.fnend
	.size	ftl_low_format, .-ftl_low_format
	.align	2
	.global	FtlWrite
	.type	FtlWrite, %function
FtlWrite:
	.fnstart
	@ args = 0, pretend = 0, frame = 72
	@ frame_needed = 0, uses_anonymous_args = 0
	stmfd	sp!, {r4, r5, r6, r7, r8, r9, sl, fp, lr}
	.save {r4, r5, r6, r7, r8, r9, sl, fp, lr}
	cmp	r0, #16
	.pad #76
	sub	sp, sp, #76
	mov	r7, r1
	mov	r9, r3
	str	r2, [sp, #16]
	bne	.L1321
	add	r0, r1, #256
	mov	r1, r2
	mov	r2, r3
	bl	FtlVendorPartWrite
	b	.L1322
.L1321:
	ldr	r6, .L1374
	ldr	ip, [sp, #16]
	ldr	r3, [r6, #112]
	add	r8, ip, r1
	cmp	r8, r3
	mvnhi	r0, #0
	bhi	.L1322
	ldr	r3, .L1374+4
	ldr	r0, [r3, #0]
	cmn	r0, #1
	beq	.L1322
	ldr	r3, .L1374+8
	mov	r2, #2048
	ldrh	r5, [r6, #78]
	mov	r0, r1
	str	r2, [r3, #-4004]
	mov	r1, r5
	bl	__aeabi_uidiv
	mov	r1, r5
	str	r0, [sp, #12]
	sub	r0, r8, #1
	bl	__aeabi_uidiv
	ldr	ip, [sp, #12]
	ldr	r3, [r6, #2452]
	rsb	r4, ip, r0
	str	r0, [sp, #32]
	add	ip, r4, #1
	str	ip, [sp, #4]
	add	r3, ip, r3
	str	r3, [r6, #2452]
	ldr	r3, .L1374+12
	ldr	r8, [r3, #3944]
	cmp	r8, #0
	beq	.L1323
	ldr	r3, [r8, #16]
	ldr	ip, [sp, #12]
	cmp	ip, r3
	beq	.L1324
	bl	FtlCacheWriteBack
	b	.L1323
.L1324:
	ldr	r3, [r6, #2456]
	mov	r1, r5
	mov	r0, r7
	add	r3, r3, #1
	str	r3, [r6, #2456]
	bl	__aeabi_uidivmod
	ldr	ip, [sp, #16]
	ldr	r0, [r8, #8]
	rsb	r5, r1, r5
	add	r0, r0, r1, asl #9
	cmp	r5, ip
	movcs	r5, ip
	mov	r1, r9
	mov	r6, r5, asl #9
	mov	r2, r6
	bl	memcpy
	cmp	r4, #0
	moveq	r0, r4
	beq	.L1322
	ldr	ip, [sp, #16]
	add	r7, r7, r5
	add	r9, r9, r6
	rsb	ip, r5, ip
	str	ip, [sp, #16]
	ldr	ip, [sp, #12]
	add	ip, ip, #1
	str	ip, [sp, #12]
	bl	FtlCacheWriteBack
	str	r4, [sp, #4]
.L1323:
	ldr	r3, .L1374+4
	ldr	r3, [r3, #8]
	cmp	r3, #0
	ldreq	r4, .L1374+16
	beq	.L1325
	ldr	r2, .L1374
	movw	r3, #2280
	ldrh	r4, [r2, r3]
	ldr	r3, .L1374+16
	ldr	r2, .L1374+20
	cmp	r4, #0
	movne	r4, r3
	moveq	r4, r2
.L1325:
	mov	fp, #0
	ldr	r6, [sp, #12]
	str	fp, [sp, #28]
	mov	sl, r7
	ldr	r5, .L1374
	b	.L1326
.L1355:
	ldrb	r2, [r4, #6]	@ zero_extendqisi2
	ldrh	r3, [r5, #4]
	cmp	r2, r3
	bcc	.L1327
	ldr	r0, .L1374+24
	mov	r2, #1072
	ldr	r1, .L1374+28
	bl	printk
.L1327:
	ldrh	r7, [r4, #4]
	cmp	r7, #0
	bne	.L1328
	bl	FtlCacheWriteBack
	ldr	r3, .L1374+16
	cmp	r4, r3
	bne	.L1329
	movw	r3, #2328
	ldrh	r7, [r5, r3]
	cmp	r7, #0
	bne	.L1330
	add	r0, r4, #48
	bl	allocate_new_data_superblock
	ldr	r3, .L1374+4
	str	r7, [r3, #8]
.L1330:
	ldr	r0, .L1374+16
	bl	allocate_new_data_superblock
	ldr	r3, .L1374+4
	ldr	r2, [r3, #8]
	ldr	r3, .L1374+20
	cmp	r2, #0
	movne	r4, r3
	b	.L1331
.L1329:
	ldr	r2, .L1374+4
	str	r7, [r2, #8]
	movw	r2, #2280
	ldrh	r2, [r5, r2]
	cmp	r2, #0
	movne	r4, r3
	bne	.L1331
	mov	r0, r4
	bl	allocate_new_data_superblock
.L1331:
	ldrh	r3, [r4, #4]
	cmp	r3, #0
	bne	.L1328
	mov	r0, r4
	bl	allocate_new_data_superblock
.L1328:
	ldrb	r3, [r4, #7]	@ zero_extendqisi2
	ldrh	r2, [r4, #4]
	ldr	ip, [sp, #4]
	mov	r3, r3, asl #2
	cmp	r3, r2
	movcs	r3, r2
	ldrb	r2, [r4, #6]	@ zero_extendqisi2
	cmp	r3, ip
	movcc	ip, r3
	ldrh	r3, [r5, #4]
	str	ip, [sp, #8]
	cmp	r2, r3
	bcc	.L1332
	ldr	r0, .L1374+24
	movw	r2, #1105
	ldr	r1, .L1374+28
	bl	printk
.L1332:
	mov	r3, #0
	b	.L1373
.L1351:
	ldrh	r3, [r4, #4]
	cmp	r3, #0
	beq	.L1365
	add	r1, sp, #48
	mov	r2, #0
	mov	r0, r6
	mov	r7, #20
	bl	log2phys
	mov	r0, r4
	bl	get_new_active_ppa
	ldr	ip, [sp, #20]
	ldr	r3, .L1374+12
	ldrh	r2, [r5, #84]
	mul	r7, r7, ip
	ldr	r1, [r3, #3992]
	add	r1, r1, r7
	str	r6, [r1, #16]
	str	r0, [r1, #4]
	mul	r0, r2, ip
	bic	r0, r0, #3
	str	r0, [sp, #40]
	ldr	r0, .L1374+32
	ldr	ip, [sp, #40]
	ldr	r0, [r0, #3788]
	add	r8, r0, ip
	str	r8, [r1, #12]
	str	r0, [sp, #44]
	mov	r1, #0
	mov	r0, r8
	str	r3, [sp, #0]
	bl	ftl_memset
	ldr	ip, [sp, #12]
	rsb	lr, ip, r6
	rsbs	ip, lr, #0
	adc	ip, ip, lr
	str	ip, [sp, #24]
	ldr	ip, [sp, #32]
	ldr	r0, [sp, #24]
	rsb	r3, ip, r6
	rsbs	ip, r3, #0
	adc	ip, ip, r3
	str	ip, [sp, #36]
	orrs	ip, ip, r0
	ldr	r3, [sp, #0]
	beq	.L1335
	cmp	r0, #0
	beq	.L1336
	ldrh	fp, [r5, #78]
	mov	r0, sl
	mov	r1, fp
	bl	__aeabi_uidivmod
	ldr	ip, [sp, #16]
	rsb	fp, r1, fp
	str	r1, [sp, #28]
	cmp	fp, ip
	movcs	fp, ip
	b	.L1337
.L1336:
	ldr	r3, [sp, #36]
	cmp	r3, #0
	beq	.L1337
	ldr	ip, [sp, #16]
	ldrh	r3, [r5, #78]
	add	fp, ip, sl
	mls	fp, r3, r6, fp
	ldr	r3, [sp, #24]
	str	r3, [sp, #28]
	uxth	fp, fp
.L1337:
	ldrh	r3, [r5, #78]
	ldr	ip, [sp, #24]
	cmp	fp, r3
	ldr	r3, .L1374+12
	ldr	r3, [r3, #3992]
	bne	.L1338
	add	r7, r3, r7
	cmp	ip, #0
	strne	r9, [r7, #8]
	muleq	r3, fp, r6
	beq	.L1372
	b	.L1340
.L1338:
	ldr	r2, .L1374+32
	cmp	ip, #0
	add	r3, r3, r7
	ldrne	r2, [r2, #3792]
	ldreq	r2, [r2, #3784]
	str	r2, [r3, #8]
	ldr	r2, [sp, #48]
	ldr	r3, .L1374+12
	cmn	r2, #1
	beq	.L1343
	ldr	r3, [r3, #3992]
	add	r0, sp, #52
	str	r6, [sp, #68]
	mov	r1, #1
	add	r3, r3, r7
	str	r2, [sp, #56]
	ldr	r2, [r3, #8]
	ldr	r3, [r3, #12]
	str	r2, [sp, #60]
	mov	r2, #0
	str	r3, [sp, #64]
	bl	FlashReadPages
	ldr	r3, [sp, #52]
	cmn	r3, #1
	ldreq	r3, [r5, #2696]
	addeq	r3, r3, #1
	streq	r3, [r5, #2696]
	beq	.L1345
	ldr	r3, [r8, #8]
	cmp	r3, r6
	beq	.L1346
	ldr	r3, [r5, #2696]
	mov	r2, r6
	ldr	r0, .L1374+36
	add	r3, r3, #1
	str	r3, [r5, #2696]
	ldr	r1, [r8, #8]
	bl	printk
.L1346:
	ldr	r3, [r8, #8]
	cmp	r3, r6
	beq	.L1345
	ldr	r1, .L1374+28
	movw	r2, #1158
	ldr	r0, .L1374+24
	bl	printk
	b	.L1345
.L1343:
	ldr	r3, [r3, #3992]
	mov	r1, #0
	ldrh	r2, [r5, #82]
	add	r3, r3, r7
	ldr	r0, [r3, #8]
	bl	ftl_memset
.L1345:
	ldr	r3, [sp, #24]
	mov	r2, fp, asl #9
	cmp	r3, #0
	ldr	r3, .L1374+12
	ldreqh	r1, [r5, #78]
	movne	r1, r9
	ldr	r3, [r3, #3992]
	ldrne	ip, [sp, #28]
	muleq	r1, r1, r6
	add	r3, r3, r7
	ldrne	r0, [r3, #8]
	rsbeq	r1, sl, r1
	ldreq	r0, [r3, #8]
	addne	r0, r0, ip, asl #9
	addeq	r1, r9, r1, asl #9
	bl	memcpy
	ldr	r3, [sp, #36]
	cmp	r3, #0
	beq	.L1340
	ldrh	r3, [r4, #4]
	cmp	r3, #0
	beq	.L1340
	ldr	r3, .L1374+12
	ldr	r2, [r3, #3992]
	add	r7, r2, r7
	str	r7, [r3, #3944]
	ldr	r3, .L1374+8
	str	r4, [r3, #-4020]
	b	.L1340
.L1335:
	ldr	r3, [r3, #3992]
	add	r7, r3, r7
	ldrh	r3, [r5, #78]
	mul	r3, r3, r6
.L1372:
	rsb	r3, sl, r3
	add	r3, r9, r3, asl #9
	str	r3, [r7, #8]
.L1340:
	ldrb	r2, [r4, #6]	@ zero_extendqisi2
	ldrh	r3, [r5, #4]
	cmp	r2, r3
	bcc	.L1349
	ldr	r0, .L1374+24
	movw	r2, #1179
	ldr	r1, .L1374+28
	bl	printk
.L1349:
	ldr	r3, .L1374+40
	ldr	r1, .L1374
	ldr	r0, [sp, #44]
	ldr	ip, [sp, #40]
	strh	r3, [r0, ip]	@ movhi
	ldr	r3, [r1, #2480]
	str	r6, [r8, #8]
	add	r6, r6, #1
	str	r3, [r8, #4]
	add	r3, r3, #1
	cmn	r3, #1
	str	r3, [r1, #2480]
	moveq	r3, #0
	streq	r3, [r5, #2480]
	ldr	r3, [sp, #48]
	str	r3, [r8, #12]
	ldrh	r3, [r4, #0]
	strh	r3, [r8, #2]	@ movhi
	ldr	r3, [sp, #20]
	add	r3, r3, #1
.L1373:
	str	r3, [sp, #20]
	ldr	ip, [sp, #20]
	ldr	r0, [sp, #8]
	cmp	ip, r0
	bne	.L1351
	b	.L1334
.L1365:
	ldr	r3, [sp, #20]
	str	r3, [sp, #8]
.L1334:
	ldr	r3, .L1374+12
	ldr	r2, [r3, #3944]
	cmp	r2, #0
	beq	.L1352
	ldr	ip, [sp, #8]
	subs	ip, ip, #1
	str	ip, [sp, #8]
	beq	.L1353
	ldr	ip, [sp, #4]
	sub	ip, ip, #1
	str	ip, [sp, #4]
.L1352:
	ldr	r0, [r3, #3992]
	mov	r2, #0
	ldr	r1, [sp, #8]
	mov	r3, r4
	bl	FtlProgPages
	ldr	ip, [sp, #4]
	ldr	r0, [sp, #8]
	cmp	ip, r0
	bcs	.L1354
	ldr	r0, .L1374+24
	movw	r2, #1194
	ldr	r1, .L1374+28
	bl	printk
.L1354:
	ldr	ip, [sp, #4]
	ldr	r0, [sp, #8]
	rsb	ip, r0, ip
	str	ip, [sp, #4]
.L1326:
	ldr	ip, [sp, #4]
	cmp	ip, #0
	bne	.L1355
.L1353:
	ldr	ip, [sp, #32]
	mov	r0, #0
	ldr	r2, [sp, #12]
	ldr	r5, .L1374
	rsb	r1, r2, ip
	bl	rk_ftl_garbage_collect
	mov	r3, #2272
	ldrh	r3, [r5, r3]
	cmp	r3, #15
	bhi	.L1367
	movw	r6, #2572
	mov	r4, r5
.L1368:
	ldrh	r3, [r5, r6]
	movw	r2, #65535
	cmp	r3, r2
	bne	.L1356
	movw	r2, #3190
	ldrh	r2, [r5, r2]
	cmp	r2, r3
	bne	.L1356
	mov	r0, #0
	bl	List_get_gc_head_node
	uxth	r0, r0
	bl	FtlGcRefreshBlock
.L1356:
	movw	r2, #3138
	mov	r3, #128
	mov	r0, #1
	strh	r3, [r4, r2]	@ movhi
	mov	r2, #3136
	mov	r1, r0
	strh	r3, [r4, r2]	@ movhi
	bl	rk_ftl_garbage_collect
	mov	r0, #0
	mov	r1, #1
	bl	rk_ftl_garbage_collect
	mov	r3, #2272
	ldrh	r3, [r4, r3]
	cmp	r3, #8
	bls	.L1368
.L1367:
	mov	r0, #0
.L1322:
	add	sp, sp, #76
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, sl, fp, pc}
.L1375:
	.align	2
.L1374:
	.word	.LANCHOR0
	.word	.LANCHOR2
	.word	.LANCHOR5
	.word	.LANCHOR4
	.word	.LANCHOR0+2276
	.word	.LANCHOR0+2324
	.word	.LC1
	.word	.LANCHOR1+589
	.word	.LANCHOR3
	.word	.LC103
	.word	-3947
	.fnend
	.size	FtlWrite, .-FtlWrite
	.align	2
	.global	sftl_write
	.type	sftl_write, %function
sftl_write:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	mov	ip, r1
	mov	r3, r2
	stmfd	sp!, {r4, lr}
	.save {r4, lr}
	mov	r4, r0
	mov	r1, r4
	mov	r0, #0
	mov	r2, ip
	ldmfd	sp!, {r4, lr}
	b	FtlWrite
	.fnend
	.size	sftl_write, .-sftl_write
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
	str	r2, [sp, #36]
	bne	.L1378
	add	r0, r1, #256
	mov	r1, r2
	mov	r2, r3
	bl	FtlVendorPartRead
	str	r0, [sp, #8]
	b	.L1379
.L1378:
	ldr	r3, [sp, #36]
	ldr	r5, .L1408
	add	r3, r3, r1
	str	r3, [sp, #12]
	ldr	r1, [sp, #12]
	ldr	r3, [r5, #112]
	cmp	r1, r3
	bhi	.L1397
	ldr	r3, .L1408+4
	ldr	r3, [r3, #0]
	cmn	r3, #1
	beq	.L1398
	bl	FtlCacheWriteBack
	ldrh	r6, [r5, #78]
	mov	r0, r4
	mov	r9, #0
	mov	r1, r6
	bl	__aeabi_uidiv
	ldr	r3, [sp, #12]
	mov	r1, r6
	mov	r6, r9
	str	r0, [sp, #16]
	sub	r0, r3, #1
	bl	__aeabi_uidiv
	ldr	r3, [sp, #16]
	ldr	r7, [sp, #16]
	rsb	fp, r3, #1
	ldr	r3, [r5, #2444]
	str	r9, [sp, #32]
	str	r9, [sp, #28]
	str	r9, [sp, #8]
	add	fp, fp, r0
	str	r0, [sp, #20]
	add	r3, fp, r3
	str	r3, [r5, #2444]
	b	.L1403
.L1396:
	mov	r2, #0
	mov	r0, r7
	add	r1, sp, #60
	bl	log2phys
	ldr	r2, [sp, #60]
	cmn	r2, #1
	bne	.L1404
	b	.L1407
.L1384:
	mla	r0, r0, r7, sl
	cmp	r0, r4
	bcc	.L1383
	ldr	r3, [sp, #12]
	cmp	r0, r3
	bcs	.L1383
	rsb	r0, r4, r0
	mov	r1, #0
	mov	r2, #512
	add	r0, r8, r0, asl #9
	bl	ftl_memset
.L1383:
	add	sl, sl, #1
	b	.L1381
.L1407:
	mov	sl, #0
.L1381:
	ldrh	r0, [r5, #78]
	cmp	sl, r0
	bcc	.L1384
	b	.L1385
.L1404:
	ldr	r3, .L1408+8
	mov	sl, #20
	ldr	r1, [r3, #3796]
	mla	sl, sl, r6, r1
	ldr	r1, [sp, #16]
	cmp	r7, r1
	str	r2, [sl, #4]
	bne	.L1386
	ldr	r3, [r3, #3792]
	mov	r0, r4
	str	r3, [sl, #8]
	ldrh	r3, [r5, #78]
	mov	r1, r3
	str	r3, [sp, #4]
	bl	__aeabi_uidivmod
	ldr	r3, [sp, #4]
	str	r1, [sp, #28]
	rsb	r2, r1, r3
	ldr	r1, [sp, #36]
	cmp	r2, r1
	movcc	r1, r2
	str	r1, [sp, #32]
	cmp	r1, r3
	streq	r8, [sl, #8]
	b	.L1387
.L1386:
	ldr	r2, [sp, #20]
	cmp	r7, r2
	ldrneh	r3, [r5, #78]
	mulne	r3, r3, r7
	bne	.L1405
	ldr	r3, [r3, #3784]
	ldrh	r2, [r5, #78]
	ldr	r1, [sp, #12]
	str	r3, [sl, #8]
	mul	r3, r2, r7
	rsb	r9, r3, r1
	cmp	r9, r2
	bne	.L1387
.L1405:
	rsb	r3, r4, r3
	add	r3, r8, r3, asl #9
	str	r3, [sl, #8]
.L1387:
	ldrh	r3, [r5, #84]
	ldr	r2, .L1408+8
	str	r7, [sl, #16]
	mul	r3, r3, r6
	ldr	r2, [r2, #3788]
	add	r6, r6, #1
	bic	r3, r3, #3
	add	r3, r2, r3
	str	r3, [sl, #12]
.L1385:
	subs	fp, fp, #1
	add	r7, r7, #1
	beq	.L1389
	ldrh	r3, [r5, #4]
	cmp	r6, r3, asl #2
	bne	.L1403
.L1389:
	cmp	r6, #0
	beq	.L1403
	ldr	sl, .L1408+8
	mov	r1, r6
	mov	r2, #0
	ldr	r0, [sl, #3796]
	bl	FlashReadPages
	ldr	r3, [sp, #28]
	str	r7, [sp, #52]
	mov	r3, r3, asl #9
	str	r3, [sp, #40]
	ldr	r3, [sp, #32]
	mov	r3, r3, asl #9
	str	r3, [sp, #44]
	mov	r3, r9, asl #9
	str	r3, [sp, #48]
	mov	r3, #0
	str	r3, [sp, #24]
.L1395:
	ldr	r3, [sp, #24]
	mov	r1, #20
	mul	r7, r1, r3
	ldr	r3, [sl, #3796]
	ldr	r1, [sp, #16]
	add	r3, r3, r7
	ldr	r2, [r3, #16]
	cmp	r2, r1
	bne	.L1391
	ldr	r1, [r3, #8]
	ldr	r3, [sl, #3792]
	cmp	r1, r3
	bne	.L1392
	ldr	r3, [sp, #40]
	mov	r0, r8
	ldr	r2, [sp, #44]
	add	r1, r1, r3
	b	.L1406
.L1391:
	ldr	r1, [sp, #20]
	cmp	r2, r1
	bne	.L1392
	ldr	r1, [r3, #8]
	ldr	r3, [sl, #3784]
	cmp	r1, r3
	bne	.L1392
	ldrh	r0, [r5, #78]
	ldr	r3, [sp, #20]
	ldr	r2, [sp, #48]
	mul	r0, r0, r3
	rsb	r0, r4, r0
	add	r0, r8, r0, asl #9
.L1406:
	bl	memcpy
.L1392:
	ldr	r3, [sl, #3796]
	add	r0, r3, r7
	ldr	r2, [r3, r7]
	cmn	r2, #1
	streq	r2, [sp, #8]
	ldreq	r1, [r5, #2696]
	addeq	r1, r1, #1
	streq	r1, [r5, #2696]
	ldr	r3, [r3, r7]
	cmp	r3, #256
	bne	.L1394
	ldr	r0, [r0, #4]
	ubfx	r0, r0, #10, #16
	bl	P2V_block_in_plane
	bl	FtlGcRefreshBlock
.L1394:
	ldr	r3, [sp, #24]
	add	r3, r3, #1
	str	r3, [sp, #24]
	cmp	r3, r6
	bne	.L1395
	ldr	r7, [sp, #52]
	mov	r6, #0
.L1403:
	cmp	fp, #0
	bne	.L1396
	ldr	r2, .L1408
	movw	r3, #3194
	ldrh	r3, [r2, r3]
	cmp	r3, #0
	beq	.L1379
	mov	r0, fp
	mov	r1, #1
	bl	rk_ftl_garbage_collect
	b	.L1379
.L1397:
	mvn	r3, #0
.L1398:
	str	r3, [sp, #8]
.L1379:
	ldr	r0, [sp, #8]
	add	sp, sp, #68
	ldmfd	sp!, {r4, r5, r6, r7, r8, r9, sl, fp, pc}
.L1409:
	.align	2
.L1408:
	.word	.LANCHOR0
	.word	.LANCHOR2
	.word	.LANCHOR3
	.fnend
	.size	FtlRead, .-FtlRead
	.align	2
	.global	sftl_read
	.type	sftl_read, %function
sftl_read:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	mov	ip, r1
	mov	r3, r2
	stmfd	sp!, {r4, lr}
	.save {r4, lr}
	mov	r4, r0
	mov	r1, r4
	mov	r0, #0
	mov	r2, ip
	ldmfd	sp!, {r4, lr}
	b	FtlRead
	.fnend
	.size	sftl_read, .-sftl_read
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
	.global	ftl_temp_buf
	.global	gc_ink_free_return_value
	.global	check_vpc_table
	.global	FtlUpdateVaildLpnCount
	.global	g_ect_tbl_power_up_flush
	.global	power_up_flag
	.global	gFtlInitStatus
	.global	DeviceCapacity
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
	.section	.rodata
	.set	.LANCHOR1,. + 0
	.type	__func__.3657, %object
	.size	__func__.3657, 17
__func__.3657:
	.ascii	"INSERT_DATA_LIST\000"
	.type	__func__.3652, %object
	.size	__func__.3652, 17
__func__.3652:
	.ascii	"INSERT_FREE_LIST\000"
	.type	__func__.3687, %object
	.size	__func__.3687, 17
__func__.3687:
	.ascii	"List_remove_node\000"
	.type	__func__.3719, %object
	.size	__func__.3719, 22
__func__.3719:
	.ascii	"List_update_data_list\000"
	.type	__func__.3760, %object
	.size	__func__.3760, 26
__func__.3760:
	.ascii	"ftl_map_blk_alloc_new_blk\000"
	.type	__func__.3824, %object
	.size	__func__.3824, 22
__func__.3824:
	.ascii	"select_l2p_ram_region\000"
	.type	__func__.4147, %object
	.size	__func__.4147, 16
__func__.4147:
	.ascii	"make_superblock\000"
	.type	__func__.4321, %object
	.size	__func__.4321, 19
__func__.4321:
	.ascii	"get_new_active_ppa\000"
	.type	__func__.4560, %object
	.size	__func__.4560, 15
__func__.4560:
	.ascii	"FlashReadPages\000"
	.type	__func__.4577, %object
	.size	__func__.4577, 15
__func__.4577:
	.ascii	"FlashProgPages\000"
	.type	__func__.4168, %object
	.size	__func__.4168, 18
__func__.4168:
	.ascii	"SupperBlkListInit\000"
	.type	__func__.3936, %object
	.size	__func__.3936, 14
__func__.3936:
	.ascii	"FtlScanSysBlk\000"
	.type	__func__.3785, %object
	.size	__func__.3785, 31
__func__.3785:
	.ascii	"Ftl_write_map_blk_to_last_page\000"
	.type	__func__.3799, %object
	.size	__func__.3799, 16
__func__.3799:
	.ascii	"FtlMapWritePage\000"
	.type	__func__.3771, %object
	.size	__func__.3771, 15
__func__.3771:
	.ascii	"ftl_map_blk_gc\000"
	.type	__func__.3728, %object
	.size	__func__.3728, 16
__func__.3728:
	.ascii	"load_l2p_region\000"
	.type	__func__.3841, %object
	.size	__func__.3841, 9
__func__.3841:
	.ascii	"log2phys\000"
	.type	__func__.4056, %object
	.size	__func__.4056, 16
__func__.4056:
	.ascii	"FtlReUsePrevPpa\000"
	.type	__func__.4209, %object
	.size	__func__.4209, 14
__func__.4209:
	.ascii	"ftl_check_vpc\000"
	.type	__func__.3529, %object
	.size	__func__.3529, 11
__func__.3529:
	.ascii	"FtlLoadBbt\000"
	.type	__func__.3486, %object
	.size	__func__.3486, 14
__func__.3486:
	.ascii	"FtlBbt2Bitmap\000"
	.type	__func__.3270, %object
	.size	__func__.3270, 11
__func__.3270:
	.ascii	"FtlMemInit\000"
	.type	__func__.3993, %object
	.size	__func__.3993, 15
__func__.3993:
	.ascii	"FtlLoadSysInfo\000"
	.type	__func__.3914, %object
	.size	__func__.3914, 15
__func__.3914:
	.ascii	"FtlVpcTblFlush\000"
	.type	__func__.4334, %object
	.size	__func__.4334, 16
__func__.4334:
	.ascii	"update_vpc_list\000"
	.type	__func__.4341, %object
	.size	__func__.4341, 20
__func__.4341:
	.ascii	"decrement_vpc_count\000"
	.type	__func__.4090, %object
	.size	__func__.4090, 22
__func__.4090:
	.ascii	"FtlRecoverySuperblock\000"
	.type	__func__.4293, %object
	.size	__func__.4293, 25
__func__.4293:
	.ascii	"allocate_data_superblock\000"
	.type	__func__.4314, %object
	.size	__func__.4314, 29
__func__.4314:
	.ascii	"allocate_new_data_superblock\000"
	.type	__func__.3394, %object
	.size	__func__.3394, 13
__func__.3394:
	.ascii	"FtlProgPages\000"
	.type	__func__.4411, %object
	.size	__func__.4411, 19
__func__.4411:
	.ascii	"FtlGcFreeTempBlock\000"
	.type	__func__.4517, %object
	.size	__func__.4517, 23
__func__.4517:
	.ascii	"rk_ftl_garbage_collect\000"
	.type	__func__.4193, %object
	.size	__func__.4193, 21
__func__.4193:
	.ascii	"FtlVpcCheckAndModify\000"
	.type	__func__.3422, %object
	.size	__func__.3422, 9
__func__.3422:
	.ascii	"FtlWrite\000"
	.section	.rodata.str1.1,"aMS",%progbits,1
.LC0:
	.ascii	"phyBlk = 0x%x die = %d block_in_die = 0x%x 0x%8x\012"
	.ascii	"\000"
.LC1:
	.ascii	"\012!!!!! error @ func:%s - line:%d\012\000"
.LC2:
	.ascii	"FLASH INFO:\012\000"
.LC3:
	.ascii	"Device Capacity: %d MB\012\000"
.LC4:
	.ascii	"FTL INFO:\012\000"
.LC5:
	.ascii	"g_MaxLpn = 0x%x\012\000"
.LC6:
	.ascii	"g_VaildLpn = 0x%x\012\000"
.LC7:
	.ascii	"read_page_count = 0x%x\012\000"
.LC8:
	.ascii	"discard_page_count = 0x%x\012\000"
.LC9:
	.ascii	"write_page_count = 0x%x\012\000"
.LC10:
	.ascii	"cache_write_count = 0x%x\012\000"
.LC11:
	.ascii	"l2p_write_count = 0x%x\012\000"
.LC12:
	.ascii	"gc_page_count = 0x%x\012\000"
.LC13:
	.ascii	"totle_write = %d MB\012\000"
.LC14:
	.ascii	"totle_read = %d MB\012\000"
.LC15:
	.ascii	"GSV = 0x%x\012\000"
.LC16:
	.ascii	"GDV = 0x%x\012\000"
.LC17:
	.ascii	"bad blk num = %d\012\000"
.LC18:
	.ascii	"free_superblocks = 0x%x\012\000"
.LC19:
	.ascii	"mlc_EC = 0x%x\012\000"
.LC20:
	.ascii	"slc_EC = 0x%x\012\000"
.LC21:
	.ascii	"avg_EC = 0x%x\012\000"
.LC22:
	.ascii	"sys_EC = 0x%x\012\000"
.LC23:
	.ascii	"max_EC = 0x%x\012\000"
.LC24:
	.ascii	"min_EC = 0x%x\012\000"
.LC25:
	.ascii	"PLT = 0x%x\012\000"
.LC26:
	.ascii	"POT = 0x%x\012\000"
.LC27:
	.ascii	"MaxSector = 0x%x\012\000"
.LC28:
	.ascii	"init_sys_blks_pp = 0x%x\012\000"
.LC29:
	.ascii	"sys_blks_pp = 0x%x\012\000"
.LC30:
	.ascii	"free sysblock = 0x%x\012\000"
.LC31:
	.ascii	"data_blks_pp = 0x%x\012\000"
.LC32:
	.ascii	"data_op_blks_pp = 0x%x\012\000"
.LC33:
	.ascii	"max_data_blks = 0x%x\012\000"
.LC34:
	.ascii	"Sys.id = 0x%x\012\000"
.LC35:
	.ascii	"Bbt.id = 0x%x\012\000"
.LC36:
	.ascii	"ACT.page = 0x%x\012\000"
.LC37:
	.ascii	"ACT.plane = 0x%x\012\000"
.LC38:
	.ascii	"ACT.id = 0x%x\012\000"
.LC39:
	.ascii	"ACT.mode = 0x%x\012\000"
.LC40:
	.ascii	"ACT.a_pages = 0x%x\012\000"
.LC41:
	.ascii	"ACT VPC = 0x%x\012\000"
.LC42:
	.ascii	"BUF.page = 0x%x\012\000"
.LC43:
	.ascii	"BUF.plane = 0x%x\012\000"
.LC44:
	.ascii	"BUF.id = 0x%x\012\000"
.LC45:
	.ascii	"BUF.mode = 0x%x\012\000"
.LC46:
	.ascii	"BUF.a_pages = 0x%x\012\000"
.LC47:
	.ascii	"BUF VPC = 0x%x\012\000"
.LC48:
	.ascii	"TMP.page = 0x%x\012\000"
.LC49:
	.ascii	"TMP.plane = 0x%x\012\000"
.LC50:
	.ascii	"TMP.id = 0x%x\012\000"
.LC51:
	.ascii	"TMP.mode = 0x%x\012\000"
.LC52:
	.ascii	"TMP.a_pages = 0x%x\012\000"
.LC53:
	.ascii	"GC.page = 0x%x\012\000"
.LC54:
	.ascii	"GC.plane = 0x%x\012\000"
.LC55:
	.ascii	"GC.id = 0x%x\012\000"
.LC56:
	.ascii	"GC.mode = 0x%x\012\000"
.LC57:
	.ascii	"GC.a_pages = 0x%x\012\000"
.LC58:
	.ascii	"WR_CHK = 0x%x %x %x %x\012\000"
.LC59:
	.ascii	"Read Err Cnt = 0x%x\012\000"
.LC60:
	.ascii	"Prog Err Cnt = 0x%x\012\000"
.LC61:
	.ascii	"gc_free_blk_th= 0x%x\012\000"
.LC62:
	.ascii	"gc_merge_free_blk_th= 0x%x\012\000"
.LC63:
	.ascii	"gc_skip_write_count= 0x%x\012\000"
.LC64:
	.ascii	"gc_blk_index= 0x%x\012\000"
.LC65:
	.ascii	"free min EC= 0x%x\012\000"
.LC66:
	.ascii	"free max EC= 0x%x\012\000"
.LC67:
	.ascii	"GC__SB VPC = 0x%x\012\000"
.LC68:
	.ascii	"%d. [0x%x]=0x%x 0x%x  0x%x\012\000"
.LC69:
	.ascii	"free %d. [0x%x] 0x%x  0x%x\012\000"
.LC70:
	.ascii	"%s\012\000"
.LC71:
	.ascii	"FTL version: 5.0.41 20170928\000"
.LC72:
	.ascii	"GetSwlReplaceBlock min_ec_id =%x %x\012\000"
.LC73:
	.ascii	"swblk %x ,avg = %x max= %x vpc= %x,ec=%x ,max ec=%x"
	.ascii	"\012\000"
.LC74:
	.ascii	"FtlGcRefreshBlock  0x%x\012\000"
.LC75:
	.ascii	"FtlGcMarkBadPhyBlk %d 0x%x\012\000"
.LC76:
	.ascii	"prog read error: = %x\012\000"
.LC77:
	.ascii	"prog read s error: = %x %x %x\012\000"
.LC78:
	.ascii	"prog read d error: = %x %x %x\012\000"
.LC79:
	.ascii	"FtlMapWritePage error = %x \012\000"
.LC80:
	.ascii	"FtlMapWritePage error = %x error count = %d\012\000"
.LC81:
	.ascii	"region_id = %x phyAddr = %x\012\000"
.LC82:
	.ascii	"spare:\000"
.LC83:
	.ascii	"map_ppn:\000"
.LC84:
	.ascii	"ftl_scan_all_data = %x\012\000"
.LC85:
	.ascii	"scan lpa = %x ppa= %x\012\000"
.LC86:
	.ascii	"lba = %x,addr= %x,spare= %x %x %x %x data=%x %x\012"
	.ascii	"\000"
.LC87:
	.ascii	"...%s enter...\012\000"
.LC88:
	.ascii	"FtlCheckVpc2 %x = %x  %x\012\000"
.LC89:
	.ascii	"free blk vpc error %x = %x  %x\012\000"
.LC90:
	.ascii	"%s error allocating memory. return -1\012\000"
.LC91:
	.ascii	"FtlVpcTblFlush error = %x error count = %d\012\000"
.LC92:
	.ascii	"slc mode\000"
.LC93:
	.ascii	"no ect\000"
.LC94:
	.ascii	"FtlBbmTblFlush id=%x,page=%x,previd=%x cnt=%d\012\000"
.LC95:
	.ascii	"FtlBbmTblFlush error:%x\012\000"
.LC96:
	.ascii	"FtlBbmTblFlush error = %x error count = %d\012\000"
.LC97:
	.ascii	"FtlGcFreeBadSuperBlk 0x%x\012\000"
.LC98:
	.ascii	"decrement_vpc_count %x = %d\012\000"
.LC99:
	.ascii	"spuer block %x vpn is 0\012 \000"
.LC100:
	.ascii	"Ftlwrite decrement_vpc_count %x = %d\012\000"
.LC101:
	.ascii	"SWL %x, FSB = %x vpc= %x,ec=%x th=%x\012\000"
.LC102:
	.ascii	"FtlCheckVpc %x = %x  %x\012\000"
.LC103:
	.ascii	"FtlWrite: lpa error:%x %x\012\000"
	.data
	.align	2
	.set	.LANCHOR2,. + 0
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
	.bss
	.align	2
	.set	.LANCHOR0,. + 0
	.set	.LANCHOR3,. + 8184
	.set	.LANCHOR4,. + 24552
	.set	.LANCHOR5,. + 32736
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
	.type	gBbtInfo, %object
	.size	gBbtInfo, 60
gBbtInfo:
	.space	60
	.type	gSysFreeQueue, %object
	.size	gSysFreeQueue, 2056
gSysFreeQueue:
	.space	2056
	.type	p_sys_data_buf, %object
	.size	p_sys_data_buf, 4
p_sys_data_buf:
	.space	4
	.type	p_erase_count_table, %object
	.size	p_erase_count_table, 4
p_erase_count_table:
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
	.type	g_MaxLpn, %object
	.size	g_MaxLpn, 4
g_MaxLpn:
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
	.type	g_totle_sys_slc_erase_count, %object
	.size	g_totle_sys_slc_erase_count, 4
g_totle_sys_slc_erase_count:
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
	.type	ftl_temp_buf, %object
	.size	ftl_temp_buf, 4096
ftl_temp_buf:
	.space	4096
	.type	req_sys, %object
	.size	req_sys, 20
req_sys:
	.space	20
	.type	p_sys_spare_buf, %object
	.size	p_sys_spare_buf, 4
p_sys_spare_buf:
	.space	4
	.type	check_buf, %object
	.size	check_buf, 4096
check_buf:
	.space	4096
	.type	check_spare_buf, %object
	.size	check_spare_buf, 512
check_spare_buf:
	.space	512
	.type	req_erase, %object
	.size	req_erase, 4
req_erase:
	.space	4
	.type	g_cur_erase_blk, %object
	.size	g_cur_erase_blk, 4
g_cur_erase_blk:
	.space	4
	.type	p_io_data_buf_1, %object
	.size	p_io_data_buf_1, 4
p_io_data_buf_1:
	.space	4
	.type	p_io_spare_buf, %object
	.size	p_io_spare_buf, 4
p_io_spare_buf:
	.space	4
	.type	p_io_data_buf_0, %object
	.size	p_io_data_buf_0, 4
p_io_data_buf_0:
	.space	4
	.type	req_read, %object
	.size	req_read, 4
req_read:
	.space	4
	.type	ftl_gc_temp_power_lost_recovery_flag, %object
	.size	ftl_gc_temp_power_lost_recovery_flag, 4
ftl_gc_temp_power_lost_recovery_flag:
	.space	4
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
	.type	p_vendor_block_ver_table, %object
	.size	p_vendor_block_ver_table, 4
p_vendor_block_ver_table:
	.space	4
	.type	p_sys_data_buf_1, %object
	.size	p_sys_data_buf_1, 4
p_sys_data_buf_1:
	.space	4
	.type	gVendorBlkInfo, %object
	.size	gVendorBlkInfo, 40
gVendorBlkInfo:
	.space	40
	.type	p_vendor_block_valid_page_count, %object
	.size	p_vendor_block_valid_page_count, 4
p_vendor_block_valid_page_count:
	.space	4
	.type	p_vendor_region_ppn_table, %object
	.size	p_vendor_region_ppn_table, 4
p_vendor_region_ppn_table:
	.space	4
	.type	check_vpc_table, %object
	.size	check_vpc_table, 16384
check_vpc_table:
	.space	16384
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
	.type	g_wr_page_num, %object
	.size	g_wr_page_num, 4
g_wr_page_num:
	.space	4
	.type	req_gc_dst, %object
	.size	req_gc_dst, 4
req_gc_dst:
	.space	4
	.type	req_prgm, %object
	.size	req_prgm, 4
req_prgm:
	.space	4
	.type	req_wr_io, %object
	.size	req_wr_io, 4
req_wr_io:
	.space	4
	.type	p_vendor_data_buf, %object
	.size	p_vendor_data_buf, 4
p_vendor_data_buf:
	.space	4
	.type	p_wr_io_data_buf, %object
	.size	p_wr_io_data_buf, 4
p_wr_io_data_buf:
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
	.type	g_MaxLbn, %object
	.size	g_MaxLbn, 4
g_MaxLbn:
	.space	4
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
	.type	gp_last_act_superblock, %object
	.size	gp_last_act_superblock, 4
gp_last_act_superblock:
	.space	4
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
	.type	gc_discard_updated, %object
	.size	gc_discard_updated, 4
gc_discard_updated:
	.space	4
	.type	g_ftl_nand_free_count, %object
	.size	g_ftl_nand_free_count, 4
g_ftl_nand_free_count:
	.space	4
