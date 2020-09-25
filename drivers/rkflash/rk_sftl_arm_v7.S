/* SPDX-License-Identifier: GPL-2.0 */

/* Copyright (c) 2018 Rockchip Electronics Co. Ltd.
 * date: 2020-09-25
 */
	.arch armv7-a
	.eabi_attribute 20, 1
	.eabi_attribute 21, 1
	.eabi_attribute 23, 3
	.eabi_attribute 24, 1
	.eabi_attribute 25, 1
	.eabi_attribute 26, 2
	.eabi_attribute 30, 4
	.eabi_attribute 34, 1
	.eabi_attribute 18, 2
	.file	"rk_sftl.c"
	.syntax divided
	.syntax unified
	.global	__aeabi_uidiv
	.arm
	.syntax unified
	.text
	.align	2
	.syntax unified
	.arm
	.fpu softvfp
	.type	l2p_addr_tran, %function
l2p_addr_tran:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	ldr	r3, .L4
	push	{r4, r5, r6, r7, r8, r9, r10, lr}
	.save {r4, r5, r6, r7, r8, r9, r10, lr}
	mov	r8, r1
	ldr	r6, [r0, #4]
	mov	r9, r2
	ldrh	r4, [r3, #8]
	ldrh	r5, [r3, #10]
	ldrh	r3, [r3, #14]
	lsr	r7, r6, #10
	ubfx	r6, r6, #0, #10
	uxth	r0, r7
	uxth	r7, r7
	cmp	r3, #4
	lsreq	r4, r4, #1
	lsleq	r5, r5, #1
	mov	r1, r4
	uxtheq	r5, r5
	bl	__aeabi_uidiv
	uxth	r0, r0
	mls	r4, r0, r4, r7
	mla	r4, r5, r4, r6
	str	r4, [r8]
	str	r0, [r9]
	mov	r0, #0
	pop	{r4, r5, r6, r7, r8, r9, r10, pc}
.L5:
	.align	2
.L4:
	.word	.LANCHOR0
	.fnend
	.size	l2p_addr_tran, .-l2p_addr_tran
	.align	2
	.global	ftl_print_sblk_info
	.syntax unified
	.arm
	.fpu softvfp
	.type	ftl_print_sblk_info, %function
ftl_print_sblk_info:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	push	{r0, r1, r2, r3, r4, lr}
	.save {r4, lr}
	.pad #16
	ldr	r4, .L8
	ldr	r1, .L8+4
	ldr	r0, .L8+8
	bl	sftl_printk
	ldrh	r1, [r4, #24]
	ldr	ip, [r4, #72]
	ldrb	r3, [r4, #30]	@ zero_extendqisi2
	ldrh	r2, [r4, #26]
	lsl	r0, r1, #1
	ldrh	r0, [ip, r0]
	str	r0, [sp, #8]
	ldrh	r0, [r4, #28]
	str	r0, [sp, #4]
	ldrb	r0, [r4, #32]	@ zero_extendqisi2
	str	r0, [sp]
	ldr	r0, .L8+12
	bl	sftl_printk
	ldrh	r1, [r4, #76]
	ldr	ip, [r4, #72]
	ldrb	r3, [r4, #82]	@ zero_extendqisi2
	ldrh	r2, [r4, #78]
	lsl	r0, r1, #1
	ldrh	r0, [ip, r0]
	str	r0, [sp, #8]
	ldrh	r0, [r4, #80]
	str	r0, [sp, #4]
	ldrb	r0, [r4, #84]	@ zero_extendqisi2
	str	r0, [sp]
	ldr	r0, .L8+16
	bl	sftl_printk
	ldrh	r1, [r4, #124]
	ldr	ip, [r4, #72]
	ldrb	r3, [r4, #130]	@ zero_extendqisi2
	ldrh	r2, [r4, #126]
	lsl	r0, r1, #1
	ldrh	r0, [ip, r0]
	str	r0, [sp, #8]
	ldrh	r0, [r4, #128]
	str	r0, [sp, #4]
	ldrb	r0, [r4, #132]	@ zero_extendqisi2
	str	r0, [sp]
	ldr	r0, .L8+20
	bl	sftl_printk
	ldrh	r1, [r4, #172]
	ldr	ip, [r4, #72]
	ldrb	r3, [r4, #178]	@ zero_extendqisi2
	ldrh	r2, [r4, #174]
	lsl	r0, r1, #1
	ldrh	r0, [ip, r0]
	str	r0, [sp, #8]
	ldrh	r0, [r4, #176]
	str	r0, [sp, #4]
	ldrb	r0, [r4, #180]	@ zero_extendqisi2
	str	r0, [sp]
	ldr	r0, .L8+24
	bl	sftl_printk
	ldrh	r3, [r4, #220]
	ldrh	r2, [r4, #222]
	ldrh	r1, [r4, #224]
	ldr	r0, .L8+28
	add	sp, sp, #16
	@ sp needed
	pop	{r4, lr}
	b	sftl_printk
.L9:
	.align	2
.L8:
	.word	.LANCHOR0
	.word	.LC0
	.word	.LC1
	.word	.LC2
	.word	.LC3
	.word	.LC4
	.word	.LC5
	.word	.LC6
	.fnend
	.size	ftl_print_sblk_info, .-ftl_print_sblk_info
	.align	2
	.global	Ftl_log2
	.syntax unified
	.arm
	.fpu softvfp
	.type	Ftl_log2, %function
Ftl_log2:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	@ link register save eliminated.
	mov	r1, #0
	mov	r2, #1
.L11:
	cmp	r2, r0
	uxth	r3, r1
	add	r1, r1, #1
	bls	.L12
	sub	r0, r3, #1
	uxth	r0, r0
	bx	lr
.L12:
	lsl	r2, r2, #1
	b	.L11
	.fnend
	.size	Ftl_log2, .-Ftl_log2
	.align	2
	.global	FtlPrintInfo
	.syntax unified
	.arm
	.fpu softvfp
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
	.syntax unified
	.arm
	.fpu softvfp
	.type	FtlSysBlkNumInit, %function
FtlSysBlkNumInit:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	@ link register save eliminated.
	ldr	r3, .L15
	cmp	r0, #24
	movcc	r0, #24
	ldrh	r2, [r3, #232]
	ldrh	r1, [r3, #242]
	str	r0, [r3, #228]
	mul	r2, r0, r2
	sub	r0, r1, r0
	ldr	r1, [r3, #248]
	strh	r0, [r3, #240]	@ movhi
	mov	r0, #0
	str	r2, [r3, #236]
	sub	r2, r1, r2
	str	r2, [r3, #244]
	bx	lr
.L16:
	.align	2
.L15:
	.word	.LANCHOR0
	.fnend
	.size	FtlSysBlkNumInit, .-FtlSysBlkNumInit
	.global	__aeabi_idiv
	.align	2
	.global	FtlConstantsInit
	.syntax unified
	.arm
	.fpu softvfp
	.type	FtlConstantsInit, %function
FtlConstantsInit:
	.fnstart
	@ args = 0, pretend = 0, frame = 8
	@ frame_needed = 0, uses_anonymous_args = 0
	mov	r3, r0
	push	{r0, r1, r2, r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.save {r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.pad #12
	ldrh	ip, [r3, #14]
	movw	r5, #258
	ldr	r4, .L32
	ldrh	r0, [r0]
	cmp	ip, #4
	ldrh	r7, [r3, #2]
	ldrh	lr, [r3, #4]
	add	r1, r4, #256
	ldrh	r2, [r3, #6]
	strh	r0, [r4, #252]	@ movhi
	strh	r7, [r4, #254]	@ movhi
	strh	lr, [r1]	@ movhi
	strh	r2, [r4, #242]	@ movhi
	strh	ip, [r4, r5]	@ movhi
	bne	.L18
	lsr	r2, r2, #1
	strh	r2, [r4, #242]	@ movhi
	mov	r2, #8
	strh	r2, [r4, r5]	@ movhi
.L19:
	ldr	ip, .L32+4
	mov	r2, #0
.L20:
	strb	r2, [r2, ip]
	add	r2, r2, #1
	cmp	r2, #32
	bne	.L20
	ldr	r2, .L32+8
	cmp	r0, #1
	mov	ip, #5
	ldrh	r5, [r4, #242]
	ldr	r8, .L32+12
	mov	lr, #0
	strh	ip, [r2]	@ movhi
	movw	ip, #294
	strheq	r0, [r2]	@ movhi
	mov	r2, #640
	ldrh	r0, [r1]
	mov	r6, r8
	strh	r2, [r6], #4	@ movhi
	movw	r2, #298
	strh	lr, [r4, ip]	@ movhi
	str	r3, [sp, #4]
	smulbb	r7, r7, r0
	smulbb	r0, r0, r5
	uxth	r7, r7
	uxth	r0, r0
	strh	r7, [r4, #232]	@ movhi
	strh	r0, [r4, r2]	@ movhi
	bl	Ftl_log2
	ldr	r3, [sp, #4]
	movw	r2, #302
	strh	r0, [r6]	@ movhi
	mov	r6, r8
	add	r8, r8, #24
	ldrh	r10, [r3, #12]
	strh	r10, [r4, r2]	@ movhi
	movw	r2, #306
	smulbb	r1, r7, r10
	strh	r10, [r8, #-16]	@ movhi
	strh	r1, [r4, r2]	@ movhi
	movw	r2, #258
	ldrh	fp, [r4, r2]
	mov	r0, fp
	bl	Ftl_log2
	lsl	r2, fp, #9
	ldr	r3, [sp, #4]
	movw	r1, #310
	strh	r0, [r6, #12]!	@ movhi
	uxth	r2, r2
	mov	r9, r0
	mov	r0, #5120
	strh	r2, [r4, r1]	@ movhi
	lsr	r2, r2, #8
	mul	r1, r10, fp
	strh	r2, [r8, #-8]	@ movhi
	ldrh	r2, [r3, #20]
	movw	r3, #314
	strh	r2, [r4, r3]	@ movhi
	mul	r3, r5, r7
	lsl	r5, r5, #6
	str	r3, [r4, #248]
	mul	r3, fp, r3
	mul	r3, r10, r3
	asr	r3, r3, #11
	str	r3, [r4, #316]
	bl	__aeabi_idiv
	uxth	r0, r0
	movw	r2, #322
	mov	r1, r7
	cmp	r0, #4
	movls	r3, #4
	strhhi	r0, [r8]	@ movhi
	strhls	r3, [r8]	@ movhi
	mov	r3, #640
	asr	r3, r3, r9
	add	r9, r9, #9
	asr	r5, r5, r9
	ldrh	r0, [r8]
	add	r3, r3, #2
	strh	r3, [r4, r2]	@ movhi
	ldr	r3, .L32+16
	strh	r5, [r3]	@ movhi
	uxth	r5, r5
	mul	r3, r7, r5
	add	r5, r5, #8
	str	r3, [r4, #328]
	bl	__aeabi_uidiv
	uxtah	r0, r5, r0
	cmp	r7, #1
	addeq	r0, r0, #4
	str	r0, [r4, #228]
	ldrh	r0, [r4, #228]
	bl	FtlSysBlkNumInit
	ldr	r3, [r4, #228]
	mov	r0, #0
	str	r0, [r4, #340]
	str	r3, [r4, #332]
	ldr	r3, [r4, #244]
	lsl	r2, r3, #2
	movw	r3, #302
	ldrh	r3, [r4, r3]
	mul	r3, r3, r2
	ldrh	r2, [r6]
	add	r2, r2, #9
	lsr	r3, r3, r2
	ldr	r2, .L32+20
	add	r3, r3, #2
	strh	r3, [r2]	@ movhi
	mov	r2, #32
	movw	r3, #338
	strh	r2, [r4, r3]	@ movhi
	ldrh	r3, [r8]
	add	r3, r3, #3
	strh	r3, [r8]	@ movhi
	ldr	r3, [r4, #328]
	add	r3, r3, #3
	str	r3, [r4, #328]
	add	sp, sp, #12
	@ sp needed
	pop	{r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L18:
	cmp	lr, #1
	cmpeq	ip, #8
	lsreq	r2, r2, #1
	strheq	r2, [r4, #242]	@ movhi
	moveq	r2, #2
	strheq	r2, [r1]	@ movhi
	b	.L19
.L33:
	.align	2
.L32:
	.word	.LANCHOR0
	.word	.LANCHOR0+260
	.word	.LANCHOR0+292
	.word	.LANCHOR0+296
	.word	.LANCHOR0+324
	.word	.LANCHOR0+336
	.fnend
	.size	FtlConstantsInit, .-FtlConstantsInit
	.align	2
	.global	IsBlkInVendorPart
	.syntax unified
	.arm
	.fpu softvfp
	.type	IsBlkInVendorPart, %function
IsBlkInVendorPart:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	@ link register save eliminated.
	ldr	r2, .L41
	add	r3, r2, #344
	ldrh	r3, [r3]
	cmp	r3, #0
	beq	.L40
	ldr	r3, [r2, #348]
	add	r2, r2, #320
	ldrh	r2, [r2]
	add	r2, r3, r2, lsl #1
.L36:
	cmp	r3, r2
	bne	.L37
.L40:
	mov	r0, #0
	bx	lr
.L37:
	ldrh	r1, [r3], #2
	cmp	r0, r1
	bne	.L36
	mov	r0, #1
	bx	lr
.L42:
	.align	2
.L41:
	.word	.LANCHOR0
	.fnend
	.size	IsBlkInVendorPart, .-IsBlkInVendorPart
	.align	2
	.global	FtlCacheWriteBack
	.syntax unified
	.arm
	.fpu softvfp
	.type	FtlCacheWriteBack, %function
FtlCacheWriteBack:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	@ link register save eliminated.
	mov	r0, #0
	bx	lr
	.fnend
	.size	FtlCacheWriteBack, .-FtlCacheWriteBack
	.align	2
	.global	sftl_get_density
	.syntax unified
	.arm
	.fpu softvfp
	.type	sftl_get_density, %function
sftl_get_density:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	@ link register save eliminated.
	ldr	r3, .L45
	ldr	r0, [r3, #340]
	bx	lr
.L46:
	.align	2
.L45:
	.word	.LANCHOR0
	.fnend
	.size	sftl_get_density, .-sftl_get_density
	.global	__aeabi_uidivmod
	.align	2
	.global	FtlBbmMapBadBlock
	.syntax unified
	.arm
	.fpu softvfp
	.type	FtlBbmMapBadBlock, %function
FtlBbmMapBadBlock:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	push	{r0, r1, r2, r4, r5, r6, r7, lr}
	.save {r4, r5, r6, r7, lr}
	.pad #12
	movw	r3, #298
	ldr	r4, .L49
	mov	r5, r0
	ldrh	r7, [r4, r3]
	mov	r1, r7
	bl	__aeabi_uidiv
	uxth	r6, r0
	mov	r1, r7
	mov	r0, r5
	bl	__aeabi_uidivmod
	add	r2, r4, r6, lsl #2
	uxth	r3, r1
	ldr	r2, [r2, #380]
	lsr	r1, r3, #5
	and	ip, r3, #31
	mov	lr, #1
	add	r4, r4, #352
	ldr	r0, [r2, r1, lsl #2]
	orr	r0, r0, lr, lsl ip
	str	r0, [r2, r1, lsl #2]
	mov	r2, r6
	str	r0, [sp]
	mov	r1, r5
	ldr	r0, .L49+4
	bl	sftl_printk
	ldrh	r3, [r4, #6]
	mov	r0, #0
	add	r3, r3, #1
	strh	r3, [r4, #6]	@ movhi
	add	sp, sp, #12
	@ sp needed
	pop	{r4, r5, r6, r7, pc}
.L50:
	.align	2
.L49:
	.word	.LANCHOR0
	.word	.LC7
	.fnend
	.size	FtlBbmMapBadBlock, .-FtlBbmMapBadBlock
	.align	2
	.global	FtlBbmIsBadBlock
	.syntax unified
	.arm
	.fpu softvfp
	.type	FtlBbmIsBadBlock, %function
FtlBbmIsBadBlock:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	push	{r4, r5, r6, r7, r8, lr}
	.save {r4, r5, r6, r7, r8, lr}
	movw	r3, #298
	ldr	r5, .L53
	mov	r7, r0
	ldrh	r6, [r5, r3]
	mov	r1, r6
	bl	__aeabi_uidivmod
	mov	r0, r7
	uxth	r4, r1
	mov	r1, r6
	bl	__aeabi_uidiv
	uxth	r0, r0
	lsr	r2, r4, #5
	add	r5, r5, r0, lsl #2
	and	r4, r4, #31
	ldr	r3, [r5, #380]
	ldr	r0, [r3, r2, lsl #2]
	lsr	r0, r0, r4
	and	r0, r0, #1
	pop	{r4, r5, r6, r7, r8, pc}
.L54:
	.align	2
.L53:
	.word	.LANCHOR0
	.fnend
	.size	FtlBbmIsBadBlock, .-FtlBbmIsBadBlock
	.align	2
	.global	FtlBbtInfoPrint
	.syntax unified
	.arm
	.fpu softvfp
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
	.syntax unified
	.arm
	.fpu softvfp
	.type	FtlBbtCalcTotleCnt, %function
FtlBbtCalcTotleCnt:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	ldr	r3, .L64
	movw	r2, #298
	push	{r4, r5, r6, lr}
	.save {r4, r5, r6, lr}
	mov	r5, #0
	mov	r4, r5
	ldrh	r2, [r3, r2]
	ldrh	r6, [r3, #254]
	mul	r6, r6, r2
.L57:
	uxth	r0, r5
	cmp	r0, r6
	blt	.L59
	mov	r0, r4
	pop	{r4, r5, r6, pc}
.L59:
	bl	FtlBbmIsBadBlock
	cmp	r0, #0
	add	r5, r5, #1
	addne	r4, r4, #1
	uxthne	r4, r4
	b	.L57
.L65:
	.align	2
.L64:
	.word	.LANCHOR0
	.fnend
	.size	FtlBbtCalcTotleCnt, .-FtlBbtCalcTotleCnt
	.align	2
	.global	V2P_block
	.syntax unified
	.arm
	.fpu softvfp
	.type	V2P_block, %function
V2P_block:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	push	{r4, r5, r6, r7, r8, lr}
	.save {r4, r5, r6, r7, r8, lr}
	mov	r5, r1
	ldr	r4, .L68
	mov	r7, r0
	add	r3, r4, #256
	ldrh	r6, [r3]
	mov	r1, r6
	bl	__aeabi_uidiv
	movw	r3, #298
	smulbb	r5, r6, r5
	ldrh	r4, [r4, r3]
	mov	r1, r6
	smulbb	r4, r4, r0
	mov	r0, r7
	bl	__aeabi_uidivmod
	add	r0, r5, r1
	add	r0, r4, r0
	uxth	r0, r0
	pop	{r4, r5, r6, r7, r8, pc}
.L69:
	.align	2
.L68:
	.word	.LANCHOR0
	.fnend
	.size	V2P_block, .-V2P_block
	.align	2
	.global	P2V_plane
	.syntax unified
	.arm
	.fpu softvfp
	.type	P2V_plane, %function
P2V_plane:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	ldr	r3, .L72
	push	{r4, r5, r6, lr}
	.save {r4, r5, r6, lr}
	mov	r6, r0
	add	r2, r3, #256
	ldrh	r5, [r2]
	movw	r2, #298
	ldrh	r1, [r3, r2]
	bl	__aeabi_uidiv
	mov	r1, r5
	smulbb	r4, r0, r5
	mov	r0, r6
	bl	__aeabi_uidivmod
	add	r1, r4, r1
	uxth	r0, r1
	pop	{r4, r5, r6, pc}
.L73:
	.align	2
.L72:
	.word	.LANCHOR0
	.fnend
	.size	P2V_plane, .-P2V_plane
	.align	2
	.global	P2V_block_in_plane
	.syntax unified
	.arm
	.fpu softvfp
	.type	P2V_block_in_plane, %function
P2V_block_in_plane:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	push	{r4, lr}
	.save {r4, lr}
	movw	r3, #298
	ldr	r4, .L76
	ldrh	r1, [r4, r3]
	add	r4, r4, #256
	bl	__aeabi_uidivmod
	uxth	r0, r1
	ldrh	r1, [r4]
	bl	__aeabi_uidiv
	uxth	r0, r0
	pop	{r4, pc}
.L77:
	.align	2
.L76:
	.word	.LANCHOR0
	.fnend
	.size	P2V_block_in_plane, .-P2V_block_in_plane
	.align	2
	.global	ftl_cmp_data_ver
	.syntax unified
	.arm
	.fpu softvfp
	.type	ftl_cmp_data_ver, %function
ftl_cmp_data_ver:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	@ link register save eliminated.
	cmp	r0, r1
	bls	.L79
	sub	r0, r0, r1
	cmp	r0, #-2147483648
	movhi	r0, #0
	movls	r0, #1
	bx	lr
.L79:
	sub	r0, r1, r0
	cmp	r0, #-2147483648
	movls	r0, #0
	movhi	r0, #1
	bx	lr
	.fnend
	.size	ftl_cmp_data_ver, .-ftl_cmp_data_ver
	.align	2
	.global	FtlFreeSysBlkQueueEmpty
	.syntax unified
	.arm
	.fpu softvfp
	.type	FtlFreeSysBlkQueueEmpty, %function
FtlFreeSysBlkQueueEmpty:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	@ link register save eliminated.
	ldr	r3, .L82
	ldrh	r0, [r3, #6]
	clz	r0, r0
	lsr	r0, r0, #5
	bx	lr
.L83:
	.align	2
.L82:
	.word	.LANCHOR0+412
	.fnend
	.size	FtlFreeSysBlkQueueEmpty, .-FtlFreeSysBlkQueueEmpty
	.align	2
	.global	FtlFreeSysBlkQueueFull
	.syntax unified
	.arm
	.fpu softvfp
	.type	FtlFreeSysBlkQueueFull, %function
FtlFreeSysBlkQueueFull:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	@ link register save eliminated.
	ldr	r3, .L85
	ldrh	r0, [r3, #6]
	sub	r0, r0, #1024
	clz	r0, r0
	lsr	r0, r0, #5
	bx	lr
.L86:
	.align	2
.L85:
	.word	.LANCHOR0+412
	.fnend
	.size	FtlFreeSysBlkQueueFull, .-FtlFreeSysBlkQueueFull
	.align	2
	.global	FtlFreeSysBLkSort
	.syntax unified
	.arm
	.fpu softvfp
	.type	FtlFreeSysBLkSort, %function
FtlFreeSysBLkSort:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	ldr	r3, .L100
	ldrh	r2, [r3, #6]
	cmp	r2, #0
	bxeq	lr
	ldr	r2, .L100+4
	mov	r0, #0
	push	{r4, lr}
	.save {r4, lr}
	mov	r4, r0
	ldrh	r1, [r3, #2]
	ldrh	lr, [r2, #28]
	ldrh	r2, [r3, #4]
	and	lr, lr, #31
.L89:
	uxth	ip, r0
	add	r0, r0, #1
	cmp	lr, ip
	bgt	.L90
	cmp	r4, #0
	strhne	r1, [r3, #2]	@ movhi
	strhne	r2, [r3, #4]	@ movhi
	pop	{r4, pc}
.L90:
	add	ip, r3, r1, lsl #1
	add	r1, r1, #1
	ubfx	r1, r1, #0, #10
	ldrh	r4, [ip, #8]
	add	ip, r3, r2, lsl #1
	strh	r4, [ip, #8]	@ movhi
	mov	r4, #1
	add	r2, r2, r4
	ubfx	r2, r2, #0, #10
	b	.L89
.L101:
	.align	2
.L100:
	.word	.LANCHOR0+412
	.word	.LANCHOR0+2468
	.fnend
	.size	FtlFreeSysBLkSort, .-FtlFreeSysBLkSort
	.align	2
	.global	IsInFreeQueue
	.syntax unified
	.arm
	.fpu softvfp
	.type	IsInFreeQueue, %function
IsInFreeQueue:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	ldr	r3, .L112
	ldrh	ip, [r3, #6]
	cmp	ip, #1024
	beq	.L106
	str	lr, [sp, #-4]!
	.save {lr}
	mov	r1, #0
	ldrh	lr, [r3, #2]
.L104:
	cmp	r1, ip
	bcc	.L105
	mov	r0, #0
	ldr	pc, [sp], #4
.L105:
	add	r2, r1, lr
	ubfx	r2, r2, #0, #10
	add	r2, r3, r2, lsl #1
	ldrh	r2, [r2, #8]
	cmp	r2, r0
	beq	.L107
	add	r1, r1, #1
	b	.L104
.L106:
	mov	r0, #0
	bx	lr
.L107:
	mov	r0, #1
	ldr	pc, [sp], #4
.L113:
	.align	2
.L112:
	.word	.LANCHOR0+412
	.fnend
	.size	IsInFreeQueue, .-IsInFreeQueue
	.align	2
	.global	insert_data_list
	.syntax unified
	.arm
	.fpu softvfp
	.type	insert_data_list, %function
insert_data_list:
	.fnstart
	@ args = 0, pretend = 0, frame = 8
	@ frame_needed = 0, uses_anonymous_args = 0
	push	{r0, r1, r2, r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.save {r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.pad #12
	ldr	r2, .L129
	ldrh	r3, [r2, #240]
	cmp	r3, r0
	bls	.L116
	mov	r4, #6
	ldr	r7, [r2, #2516]
	mul	r4, r4, r0
	mvn	lr, #0
	add	ip, r7, r4
	strh	lr, [ip, #2]	@ movhi
	strh	lr, [r7, r4]	@ movhi
	ldr	r3, [r2, #2520]
	cmp	r3, #0
	streq	ip, [r2, #2520]
	beq	.L116
	ldr	r9, [r2, #72]
	mov	r1, r2
	lsl	r2, r0, #1
	ldr	r8, [r1, #2516]
	movw	r10, #65535
	ldrh	r5, [r9, r2]
	ldrh	r2, [ip, #4]
	cmp	r2, #0
	mulne	lr, r2, r5
	sub	r2, r3, r8
	asr	r5, r2, #1
	ldr	r2, .L129+4
	mul	r2, r2, r5
	ldrh	r5, [r1, #240]
	str	r5, [sp, #4]
	mov	r5, #0
	uxth	r2, r2
.L122:
	ldr	r6, [sp, #4]
	add	r5, r5, #1
	uxth	r5, r5
	cmp	r5, r6
	movls	r6, #0
	movhi	r6, #1
	cmp	r0, r2
	orreq	r6, r6, #1
	cmp	r6, #0
	bne	.L116
	ldrh	r6, [r3, #4]
	cmp	r6, #0
	beq	.L120
	lsl	fp, r2, #1
	ldrh	fp, [r9, fp]
	mul	r6, r6, fp
	cmp	r6, lr
	bcs	.L120
	ldrh	r6, [r3]
	cmp	r6, r10
	bne	.L121
	strh	r2, [ip, #2]	@ movhi
	strh	r0, [r3]	@ movhi
	str	ip, [r1, #2524]
.L116:
	mov	r0, #0
	add	sp, sp, #12
	@ sp needed
	pop	{r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L121:
	mov	r3, #6
	mov	r2, r6
	mla	r3, r3, r6, r8
	b	.L122
.L120:
	strh	r2, [r7, r4]	@ movhi
	ldrh	r2, [r3, #2]
	strh	r2, [ip, #2]	@ movhi
	ldr	r2, [r1, #2520]
	cmp	r3, r2
	ldrhne	ip, [r3, #2]
	movne	r2, #6
	ldrne	r1, [r1, #2516]
	strheq	r0, [r3, #2]	@ movhi
	streq	ip, [r1, #2520]
	mulne	r2, r2, ip
	strhne	r0, [r1, r2]	@ movhi
	strhne	r0, [r3, #2]	@ movhi
	b	.L116
.L130:
	.align	2
.L129:
	.word	.LANCHOR0
	.word	-1431655765
	.fnend
	.size	insert_data_list, .-insert_data_list
	.align	2
	.global	INSERT_DATA_LIST
	.syntax unified
	.arm
	.fpu softvfp
	.type	INSERT_DATA_LIST, %function
INSERT_DATA_LIST:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	push	{r4, lr}
	.save {r4, lr}
	bl	insert_data_list
	ldr	r2, .L134
	add	r1, r2, #2528
	ldrh	r2, [r2, #240]
	ldrh	r3, [r1]
	add	r3, r3, #1
	uxth	r3, r3
	cmp	r2, r3
	strh	r3, [r1]	@ movhi
	popcs	{r4, pc}
	mov	r2, #214
	ldr	r1, .L134+4
	ldr	r0, .L134+8
	pop	{r4, lr}
	b	sftl_printk
.L135:
	.align	2
.L134:
	.word	.LANCHOR0
	.word	.LANCHOR1
	.word	.LC8
	.fnend
	.size	INSERT_DATA_LIST, .-INSERT_DATA_LIST
	.align	2
	.global	insert_free_list
	.syntax unified
	.arm
	.fpu softvfp
	.type	insert_free_list, %function
insert_free_list:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	push	{r4, r5, r6, r7, r8, r9, r10, lr}
	.save {r4, r5, r6, r7, r8, r9, r10, lr}
	movw	r4, #65535
	cmp	r0, r4
	beq	.L137
	ldr	r2, .L144
	mov	r1, #6
	mul	r5, r1, r0
	mvn	r3, #0
	ldr	r6, [r2, #2516]
	mov	ip, r2
	add	lr, r6, r5
	strh	r3, [lr, #2]	@ movhi
	strh	r3, [r6, r5]	@ movhi
	ldr	r3, [r2, #2532]
	cmp	r3, #0
	streq	lr, [r2, #2532]
	beq	.L137
	ldr	r8, [r2, #2536]
	lsl	r2, r0, #1
	ldr	r7, [ip, #2516]
	ldrh	r9, [r8, r2]
	sub	r2, r3, r7
	asr	r10, r2, #1
	ldr	r2, .L144+4
	mul	r2, r2, r10
	mov	r10, r1
	uxth	r2, r2
.L141:
	lsl	r1, r2, #1
	ldrh	r1, [r8, r1]
	cmp	r1, r9
	bcs	.L139
	ldrh	r1, [r3]
	cmp	r1, r4
	bne	.L140
	strh	r2, [lr, #2]	@ movhi
	strh	r0, [r3]	@ movhi
.L137:
	mov	r0, #0
	pop	{r4, r5, r6, r7, r8, r9, r10, pc}
.L140:
	mla	r3, r10, r1, r7
	mov	r2, r1
	b	.L141
.L139:
	ldrh	r1, [r3, #2]
	strh	r1, [lr, #2]	@ movhi
	strh	r2, [r6, r5]	@ movhi
	ldr	r2, [ip, #2532]
	cmp	r3, r2
	ldrhne	lr, [r3, #2]
	movne	r2, #6
	ldrne	r1, [ip, #2516]
	strheq	r0, [r3, #2]	@ movhi
	streq	lr, [ip, #2532]
	mulne	r2, r2, lr
	strhne	r0, [r1, r2]	@ movhi
	strhne	r0, [r3, #2]	@ movhi
	b	.L137
.L145:
	.align	2
.L144:
	.word	.LANCHOR0
	.word	-1431655765
	.fnend
	.size	insert_free_list, .-insert_free_list
	.align	2
	.global	INSERT_FREE_LIST
	.syntax unified
	.arm
	.fpu softvfp
	.type	INSERT_FREE_LIST, %function
INSERT_FREE_LIST:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	push	{r4, lr}
	.save {r4, lr}
	bl	insert_free_list
	ldr	r2, .L149
	ldrh	r3, [r2, #224]
	add	r3, r3, #1
	uxth	r3, r3
	strh	r3, [r2, #224]	@ movhi
	ldrh	r2, [r2, #240]
	cmp	r2, r3
	popcs	{r4, pc}
	mov	r2, #207
	ldr	r1, .L149+4
	ldr	r0, .L149+8
	pop	{r4, lr}
	b	sftl_printk
.L150:
	.align	2
.L149:
	.word	.LANCHOR0
	.word	.LANCHOR1+17
	.word	.LC8
	.fnend
	.size	INSERT_FREE_LIST, .-INSERT_FREE_LIST
	.align	2
	.global	List_remove_node
	.syntax unified
	.arm
	.fpu softvfp
	.type	List_remove_node, %function
List_remove_node:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	push	{r4, r5, r6, r7, r8, lr}
	.save {r4, r5, r6, r7, r8, lr}
	mov	r6, #6
	ldr	r4, .L158
	mul	r6, r6, r1
	movw	r3, #65535
	mov	r8, r0
	ldr	r7, [r4, #2516]
	add	r5, r7, r6
	ldrh	r2, [r5, #2]
	cmp	r2, r3
	bne	.L152
	ldr	r3, [r0]
	cmp	r5, r3
	beq	.L152
	mov	r2, #372
	ldr	r1, .L158+4
	ldr	r0, .L158+8
	bl	sftl_printk
.L152:
	ldr	r3, [r8]
	movw	r2, #65535
	cmp	r5, r3
	ldrh	r3, [r7, r6]
	bne	.L153
	cmp	r3, r2
	ldrne	r2, [r4, #2516]
	movne	r1, #6
	moveq	r3, #0
	streq	r3, [r8]
	mlane	r3, r1, r3, r2
	mvnne	r2, #0
	strne	r3, [r8]
	strhne	r2, [r3, #2]	@ movhi
.L155:
	mvn	r3, #0
	mov	r0, #0
	strh	r3, [r7, r6]	@ movhi
	strh	r3, [r5, #2]	@ movhi
	pop	{r4, r5, r6, r7, r8, pc}
.L153:
	cmp	r3, r2
	ldrh	r1, [r5, #2]
	bne	.L156
	cmp	r1, r3
	movne	r3, #6
	ldrne	r2, [r4, #2516]
	mulne	r1, r3, r1
	mvnne	r3, #0
	strhne	r3, [r2, r1]	@ movhi
	b	.L155
.L156:
	ldr	r0, [r4, #2516]
	mov	r2, #6
	mla	r3, r2, r3, r0
	strh	r1, [r3, #2]	@ movhi
	ldrh	r0, [r5, #2]
	ldrh	r1, [r7, r6]
	ldr	r3, [r4, #2516]
	mul	r2, r2, r0
	strh	r1, [r3, r2]	@ movhi
	b	.L155
.L159:
	.align	2
.L158:
	.word	.LANCHOR0
	.word	.LANCHOR1+34
	.word	.LC8
	.fnend
	.size	List_remove_node, .-List_remove_node
	.align	2
	.global	List_pop_index_node
	.syntax unified
	.arm
	.fpu softvfp
	.type	List_pop_index_node, %function
List_pop_index_node:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	ldr	r3, [r0]
	cmp	r3, #0
	beq	.L166
	ldr	r2, .L171
	push	{r4, lr}
	.save {r4, lr}
	movw	lr, #65535
	mov	r4, #6
	ldr	r2, [r2, #2516]
.L162:
	cmp	r1, #0
	bne	.L163
.L165:
	ldr	r4, .L171+4
	sub	r3, r3, r2
	asr	r3, r3, #1
	mul	r4, r4, r3
	uxth	r1, r4
	bl	List_remove_node
	uxth	r0, r4
	pop	{r4, pc}
.L163:
	ldrh	ip, [r3]
	cmp	ip, lr
	beq	.L165
	sub	r1, r1, #1
	mla	r3, r4, ip, r2
	uxth	r1, r1
	b	.L162
.L166:
	movw	r0, #65535
	bx	lr
.L172:
	.align	2
.L171:
	.word	.LANCHOR0
	.word	-1431655765
	.fnend
	.size	List_pop_index_node, .-List_pop_index_node
	.align	2
	.global	List_pop_head_node
	.syntax unified
	.arm
	.fpu softvfp
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
	.syntax unified
	.arm
	.fpu softvfp
	.type	List_get_gc_head_node, %function
List_get_gc_head_node:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	@ link register save eliminated.
	ldr	r2, .L180
	ldr	r3, [r2, #2520]
	cmp	r3, #0
	ldrne	r1, [r2, #2516]
	movne	ip, #6
	movwne	r2, #65535
	bne	.L176
.L179:
	movw	r0, #65535
	bx	lr
.L178:
	sub	r0, r0, #1
	mla	r3, ip, r3, r1
	uxth	r0, r0
.L176:
	cmp	r0, #0
	beq	.L177
	ldrh	r3, [r3]
	cmp	r3, r2
	bne	.L178
	b	.L179
.L177:
	ldr	r0, .L180+4
	sub	r3, r3, r1
	asr	r3, r3, #1
	mul	r3, r0, r3
	uxth	r0, r3
	bx	lr
.L181:
	.align	2
.L180:
	.word	.LANCHOR0
	.word	-1431655765
	.fnend
	.size	List_get_gc_head_node, .-List_get_gc_head_node
	.align	2
	.global	List_update_data_list
	.syntax unified
	.arm
	.fpu softvfp
	.type	List_update_data_list, %function
List_update_data_list:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	push	{r4, r5, r6, r7, r8, r9, r10, lr}
	.save {r4, r5, r6, r7, r8, r9, r10, lr}
	ldr	r4, .L194
	ldrh	r3, [r4, #24]
	cmp	r3, r0
	beq	.L184
	ldrh	r3, [r4, #76]
	cmp	r3, r0
	beq	.L184
	ldrh	r3, [r4, #124]
	cmp	r3, r0
	beq	.L184
	mov	r7, #6
	ldr	r9, [r4, #2516]
	mul	r7, r7, r0
	ldr	r3, [r4, #2520]
	add	r8, r9, r7
	cmp	r8, r3
	beq	.L184
	ldr	r2, [r4, #72]
	lsl	r3, r0, #1
	ldrh	r6, [r8, #4]
	mov	r5, r0
	ldrh	r3, [r2, r3]
	movw	r2, #65535
	cmp	r6, #0
	mvneq	r6, #0
	mulne	r6, r6, r3
	ldrh	r3, [r8, #2]
	cmp	r3, r2
	bne	.L187
	ldrh	r2, [r9, r7]
	cmp	r2, r3
	bne	.L187
	movw	r2, #463
	ldr	r1, .L194+4
	ldr	r0, .L194+8
	bl	sftl_printk
.L187:
	ldrh	r3, [r8, #2]
	movw	r2, #65535
	cmp	r3, r2
	bne	.L188
	ldrh	r2, [r9, r7]
	cmp	r2, r3
	beq	.L184
.L188:
	mov	r2, #6
	mul	r2, r2, r3
	ldr	r3, .L194+12
	asr	r1, r2, #1
	mul	r3, r3, r1
	ldr	r1, [r4, #72]
	lsl	r3, r3, #1
	ldrh	r0, [r1, r3]
	ldr	r1, [r4, #2516]
	add	r2, r1, r2
	ldrh	r3, [r2, #4]
	cmp	r3, #0
	mulne	r3, r3, r0
	mvneq	r3, #0
	cmp	r6, r3
	bcs	.L184
	ldr	r4, .L194+16
	mov	r1, r5
	ldr	r0, .L194+20
	bl	List_remove_node
	ldrh	r3, [r4]
	cmp	r3, #0
	bne	.L190
	movw	r2, #474
	ldr	r1, .L194+4
	ldr	r0, .L194+8
	bl	sftl_printk
.L190:
	ldrh	r3, [r4]
	mov	r0, r5
	sub	r3, r3, #1
	strh	r3, [r4]	@ movhi
	bl	INSERT_DATA_LIST
.L184:
	mov	r0, #0
	pop	{r4, r5, r6, r7, r8, r9, r10, pc}
.L195:
	.align	2
.L194:
	.word	.LANCHOR0
	.word	.LANCHOR1+51
	.word	.LC8
	.word	-1431655765
	.word	.LANCHOR0+2528
	.word	.LANCHOR0+2520
	.fnend
	.size	List_update_data_list, .-List_update_data_list
	.align	2
	.global	select_l2p_ram_region
	.syntax unified
	.arm
	.fpu softvfp
	.type	select_l2p_ram_region, %function
select_l2p_ram_region:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	ldr	r3, .L207
	movw	r2, #338
	push	{r4, r5, r6, lr}
	.save {r4, r5, r6, lr}
	mov	r1, #0
	mov	r0, #12
	movw	ip, #65535
	ldrh	r2, [r3, r2]
	ldr	r3, [r3, #2540]
.L197:
	uxth	r4, r1
	cmp	r4, r2
	bcc	.L199
	mov	r4, r2
	mov	r1, #0
	mov	r0, #-2147483648
	mov	r5, #12
.L200:
	uxth	lr, r1
	cmp	lr, r2
	bcc	.L202
	cmp	r4, r2
	bcc	.L198
	ldr	r1, .L207+4
	mov	r4, r2
	mvn	r0, #0
	ldrh	r5, [r1]
	mov	r1, #0
.L203:
	uxth	ip, r1
	cmp	ip, r2
	bcc	.L205
	cmp	r4, r2
	bcc	.L198
	movw	r2, #789
	ldr	r1, .L207+8
	ldr	r0, .L207+12
	bl	sftl_printk
	b	.L198
.L199:
	add	r1, r1, #1
	mla	lr, r0, r1, r3
	ldrh	lr, [lr, #-12]
	cmp	lr, ip
	bne	.L197
.L198:
	mov	r0, r4
	pop	{r4, r5, r6, pc}
.L202:
	mla	ip, r5, r1, r3
	add	r1, r1, #1
	ldr	ip, [ip, #4]
	cmp	r0, ip
	movls	r6, #0
	movhi	r6, #1
	cmp	ip, #0
	movlt	r6, #0
	cmp	r6, #0
	movne	r0, ip
	movne	r4, lr
	b	.L200
.L205:
	ldr	lr, [r3, #4]
	cmp	r0, lr
	bls	.L204
	ldrh	r6, [r3]
	cmp	r6, r5
	movne	r0, lr
	movne	r4, ip
.L204:
	add	r1, r1, #1
	add	r3, r3, #12
	b	.L203
.L208:
	.align	2
.L207:
	.word	.LANCHOR0
	.word	.LANCHOR0+2544
	.word	.LANCHOR1+73
	.word	.LC8
	.fnend
	.size	select_l2p_ram_region, .-select_l2p_ram_region
	.align	2
	.global	FtlUpdateVaildLpn
	.syntax unified
	.arm
	.fpu softvfp
	.type	FtlUpdateVaildLpn, %function
FtlUpdateVaildLpn:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	ldr	ip, .L218
	movw	r1, #2546
	ldrh	r2, [ip, r1]
	cmp	r2, #4
	cmpls	r0, #0
	bne	.L210
	add	r2, r2, #1
	strh	r2, [ip, r1]	@ movhi
	bx	lr
.L210:
	mov	r2, #0
	str	lr, [sp, #-4]!
	.save {lr}
	strh	r2, [ip, r1]	@ movhi
	mov	r3, ip
	movw	lr, #65535
	str	r2, [ip, #2548]
	ldrh	r1, [ip, #240]
	ldr	r2, [ip, #72]
	add	r1, r2, r1, lsl #1
.L211:
	cmp	r2, r1
	bne	.L213
	ldr	pc, [sp], #4
.L213:
	ldrh	ip, [r2], #2
	cmp	ip, lr
	ldrne	r0, [r3, #2548]
	addne	r0, r0, ip
	strne	r0, [r3, #2548]
	b	.L211
.L219:
	.align	2
.L218:
	.word	.LANCHOR0
	.fnend
	.size	FtlUpdateVaildLpn, .-FtlUpdateVaildLpn
	.align	2
	.global	ftl_get_blk_mode
	.syntax unified
	.arm
	.fpu softvfp
	.type	ftl_get_blk_mode, %function
ftl_get_blk_mode:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	@ link register save eliminated.
	ldr	r3, .L221
	lsr	r2, r0, #5
	and	r0, r0, #31
	ldr	r3, [r3, #2552]
	ldr	r3, [r3, r2, lsl #2]
	lsr	r0, r3, r0
	and	r0, r0, #1
	bx	lr
.L222:
	.align	2
.L221:
	.word	.LANCHOR0
	.fnend
	.size	ftl_get_blk_mode, .-ftl_get_blk_mode
	.align	2
	.global	ftl_sb_update_avl_pages
	.syntax unified
	.arm
	.fpu softvfp
	.type	ftl_sb_update_avl_pages, %function
ftl_sb_update_avl_pages:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	mov	r3, #0
	push	{r4, r5, lr}
	.save {r4, r5, lr}
	strh	r3, [r0, #4]	@ movhi
	add	ip, r0, r2, lsl #1
	movw	r5, #65535
	ldr	r3, .L231
	add	ip, ip, #14
	ldrh	lr, [r3, #232]
.L224:
	cmp	r2, lr
	bcc	.L226
	movw	r2, #302
	add	ip, r0, #16
	ldrh	r3, [r3, r2]
	movw	r4, #65535
	sub	r3, r3, #1
	sub	r1, r3, r1
	mov	r3, #0
	uxth	r1, r1
.L227:
	uxth	r2, r3
	cmp	lr, r2
	bhi	.L229
	pop	{r4, r5, pc}
.L226:
	ldrh	r4, [ip, #2]!
	add	r2, r2, #1
	uxth	r2, r2
	cmp	r4, r5
	ldrhne	r4, [r0, #4]
	addne	r4, r4, #1
	strhne	r4, [r0, #4]	@ movhi
	b	.L224
.L229:
	ldrh	r2, [ip], #2
	add	r3, r3, #1
	cmp	r2, r4
	ldrhne	r2, [r0, #4]
	addne	r2, r1, r2
	strhne	r2, [r0, #4]	@ movhi
	b	.L227
.L232:
	.align	2
.L231:
	.word	.LANCHOR0
	.fnend
	.size	ftl_sb_update_avl_pages, .-ftl_sb_update_avl_pages
	.align	2
	.global	FtlSlcSuperblockCheck
	.syntax unified
	.arm
	.fpu softvfp
	.type	FtlSlcSuperblockCheck, %function
FtlSlcSuperblockCheck:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	ldrh	r3, [r0, #4]
	cmp	r3, #0
	bxeq	lr
	ldrh	r2, [r0]
	movw	r3, #65535
	cmp	r2, r3
	bxeq	lr
	ldrb	r2, [r0, #6]	@ zero_extendqisi2
	str	lr, [sp, #-4]!
	.save {lr}
	mov	lr, #0
	add	r2, r0, r2, lsl #1
	ldrh	r1, [r2, #16]
	ldr	r2, .L243
	ldrh	ip, [r2, #232]
	mov	r2, r3
.L236:
	cmp	r1, r2
	beq	.L238
	ldr	pc, [sp], #4
.L238:
	ldrb	r3, [r0, #6]	@ zero_extendqisi2
	add	r3, r3, #1
	uxtb	r3, r3
	cmp	r3, ip
	strb	r3, [r0, #6]
	ldrheq	r3, [r0, #2]
	strbeq	lr, [r0, #6]
	addeq	r3, r3, #1
	strheq	r3, [r0, #2]	@ movhi
	ldrb	r3, [r0, #6]	@ zero_extendqisi2
	add	r3, r0, r3, lsl #1
	ldrh	r1, [r3, #16]
	b	.L236
.L244:
	.align	2
.L243:
	.word	.LANCHOR0
	.fnend
	.size	FtlSlcSuperblockCheck, .-FtlSlcSuperblockCheck
	.align	2
	.global	make_superblock
	.syntax unified
	.arm
	.fpu softvfp
	.type	make_superblock, %function
make_superblock:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	push	{r3, r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.save {r3, r4, r5, r6, r7, r8, r9, r10, fp, lr}
	mov	r4, r0
	ldr	r6, .L252
	ldrh	r2, [r0]
	ldrh	r3, [r6, #240]
	cmp	r2, r3
	bcc	.L246
	movw	r2, #2150
	ldr	r1, .L252+4
	ldr	r0, .L252+8
	bl	sftl_printk
.L246:
	ldrh	r9, [r6, #232]
	add	r7, r4, #16
	ldr	r10, .L252+12
	mvn	r8, #0
	mov	r5, #0
	strh	r5, [r4, #4]	@ movhi
	strb	r5, [r4, #7]
.L247:
	uxth	r3, r5
	ldrh	r1, [r4]
	cmp	r9, r3
	bhi	.L249
	movw	r2, #302
	ldrb	r3, [r4, #7]	@ zero_extendqisi2
	ldrh	r2, [r6, r2]
	lsl	r1, r1, #1
	mov	r0, #0
	smulbb	r3, r3, r2
	strh	r3, [r4, #4]	@ movhi
	mov	r3, #0
	strb	r3, [r4, #9]
	ldr	r3, [r6, #2536]
	ldrh	r2, [r3, r1]
	movw	r3, #10000
	cmp	r2, r3
	movhi	r3, #1
	strbhi	r3, [r4, #9]
	pop	{r3, r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L249:
	ldrb	r0, [r10, r5]	@ zero_extendqisi2
	add	r7, r7, #2
	bl	V2P_block
	strh	r8, [r7, #-2]	@ movhi
	mov	fp, r0
	bl	FtlBbmIsBadBlock
	cmp	r0, #0
	add	r5, r5, #1
	strheq	fp, [r7, #-2]	@ movhi
	ldrbeq	r3, [r4, #7]	@ zero_extendqisi2
	addeq	r3, r3, #1
	strbeq	r3, [r4, #7]
	b	.L247
.L253:
	.align	2
.L252:
	.word	.LANCHOR0
	.word	.LANCHOR1+95
	.word	.LC8
	.word	.LANCHOR0+260
	.fnend
	.size	make_superblock, .-make_superblock
	.align	2
	.global	update_multiplier_value
	.syntax unified
	.arm
	.fpu softvfp
	.type	update_multiplier_value, %function
update_multiplier_value:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	ldr	r3, .L261
	movw	r2, #302
	push	{r4, r5, r6, r7, r8, r9, r10, lr}
	.save {r4, r5, r6, r7, r8, r9, r10, lr}
	mov	r5, #0
	mov	r7, r0
	mov	r4, r5
	mov	r6, r3
	add	r10, r3, #260
	ldrh	r8, [r3, #232]
	ldrh	r9, [r3, r2]
.L255:
	uxth	r3, r5
	cmp	r8, r3
	bhi	.L257
	cmp	r4, #0
	moveq	r0, r4
	beq	.L258
	mov	r1, r4
	mov	r0, #32768
	bl	__aeabi_idiv
.L258:
	ldr	r1, [r6, #2516]
	mov	r3, #6
	mla	r1, r3, r7, r1
	strh	r0, [r1, #4]	@ movhi
	mov	r0, #0
	pop	{r4, r5, r6, r7, r8, r9, r10, pc}
.L257:
	mov	r1, r7
	ldrb	r0, [r10, r5]	@ zero_extendqisi2
	bl	V2P_block
	bl	FtlBbmIsBadBlock
	cmp	r0, #0
	add	r5, r5, #1
	addeq	r4, r4, r9
	uxtheq	r4, r4
	b	.L255
.L262:
	.align	2
.L261:
	.word	.LANCHOR0
	.fnend
	.size	update_multiplier_value, .-update_multiplier_value
	.align	2
	.global	GetFreeBlockMinEraseCount
	.syntax unified
	.arm
	.fpu softvfp
	.type	GetFreeBlockMinEraseCount, %function
GetFreeBlockMinEraseCount:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	@ link register save eliminated.
	ldr	r2, .L266
	ldr	r0, [r2, #2532]
	cmp	r0, #0
	bxeq	lr
	ldr	r3, [r2, #2516]
	sub	r0, r0, r3
	ldr	r3, .L266+4
	asr	r0, r0, #1
	mul	r0, r3, r0
	ldr	r3, [r2, #2536]
	uxth	r0, r0
	lsl	r0, r0, #1
	ldrh	r0, [r3, r0]
	bx	lr
.L267:
	.align	2
.L266:
	.word	.LANCHOR0
	.word	-1431655765
	.fnend
	.size	GetFreeBlockMinEraseCount, .-GetFreeBlockMinEraseCount
	.align	2
	.global	GetFreeBlockMaxEraseCount
	.syntax unified
	.arm
	.fpu softvfp
	.type	GetFreeBlockMaxEraseCount, %function
GetFreeBlockMaxEraseCount:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	ldr	r1, .L280
	ldr	r3, [r1, #2532]
	cmp	r3, #0
	beq	.L274
	push	{r4, r5, lr}
	.save {r4, r5, lr}
	mov	r4, #6
	ldrh	r2, [r1, #224]
	movw	r5, #65535
	ldr	ip, [r1, #2516]
	rsb	r2, r2, r2, lsl #3
	sub	r3, r3, ip
	asr	r2, r2, #3
	asr	r3, r3, #1
	cmp	r0, r2
	uxthgt	r0, r2
	ldr	r2, .L280+4
	mul	r3, r2, r3
	mov	r2, #0
	uxth	r3, r3
.L271:
	uxth	lr, r2
	cmp	r0, lr
	bls	.L273
	mul	lr, r4, r3
	add	r2, r2, #1
	ldrh	lr, [ip, lr]
	cmp	lr, r5
	bne	.L275
.L273:
	ldr	r2, [r1, #2536]
	lsl	r3, r3, #1
	ldrh	r0, [r2, r3]
	pop	{r4, r5, pc}
.L275:
	mov	r3, lr
	b	.L271
.L274:
	mov	r0, r3
	bx	lr
.L281:
	.align	2
.L280:
	.word	.LANCHOR0
	.word	-1431655765
	.fnend
	.size	GetFreeBlockMaxEraseCount, .-GetFreeBlockMaxEraseCount
	.align	2
	.global	FtlPrintInfo2buf
	.syntax unified
	.arm
	.fpu softvfp
	.type	FtlPrintInfo2buf, %function
FtlPrintInfo2buf:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	push	{r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.save {r4, r5, r6, r7, r8, r9, r10, fp, lr}
	mov	r6, r0
	ldr	r5, .L295
	add	r4, r6, #12
	.pad #20
	sub	sp, sp, #20
	ldr	r1, .L295+4
	bl	strcpy
	ldr	r3, [r5, #316]
	mov	r1, #64
	ldr	r2, .L295+8
	mov	r0, r4
	bl	snprintf
	add	r4, r4, r0
	ldr	r1, .L295+12
	mov	r0, r4
	add	r4, r4, #10
	bl	strcpy
	ldr	r3, [r5, #2556]
	mov	r1, #64
	ldr	r2, .L295+16
	mov	r0, r4
	bl	snprintf
	add	r4, r4, r0
	ldr	r3, [r5, #2548]
	ldr	r2, .L295+20
	mov	r1, #64
	mov	r0, r4
	add	r7, r5, #352
	bl	snprintf
	add	r4, r4, r0
	ldr	r3, [r5, #2560]
	ldr	r2, .L295+24
	mov	r1, #64
	mov	r0, r4
	ldr	r8, .L295+28
	bl	snprintf
	add	r4, r4, r0
	ldr	r3, [r5, #2564]
	ldr	r2, .L295+32
	mov	r1, #64
	mov	r0, r4
	bl	snprintf
	add	r4, r4, r0
	ldr	r3, [r5, #2568]
	ldr	r2, .L295+36
	mov	r1, #64
	mov	r0, r4
	bl	snprintf
	add	r4, r4, r0
	ldr	r3, [r5, #2572]
	ldr	r2, .L295+40
	mov	r1, #64
	mov	r0, r4
	bl	snprintf
	add	r4, r4, r0
	ldr	r3, [r5, #2576]
	ldr	r2, .L295+44
	mov	r1, #64
	mov	r0, r4
	bl	snprintf
	add	r4, r4, r0
	ldr	r3, [r5, #2580]
	ldr	r2, .L295+48
	mov	r1, #64
	mov	r0, r4
	bl	snprintf
	ldr	r3, [r5, #2584]
	add	r4, r4, r0
	ldr	r2, .L295+52
	mov	r1, #64
	mov	r0, r4
	lsr	r3, r3, #11
	bl	snprintf
	ldr	r3, [r5, #2588]
	add	r4, r4, r0
	ldr	r2, .L295+56
	mov	r1, #64
	mov	r0, r4
	lsr	r3, r3, #11
	bl	snprintf
	add	r4, r4, r0
	ldr	r3, [r5, #2592]
	ldr	r2, .L295+60
	mov	r1, #64
	mov	r0, r4
	bl	snprintf
	add	r4, r4, r0
	ldr	r3, [r5, #2596]
	ldr	r2, .L295+64
	mov	r1, #64
	mov	r0, r4
	bl	snprintf
	add	r4, r4, r0
	ldrh	r3, [r7, #6]
	ldr	r2, .L295+68
	mov	r1, #64
	mov	r0, r4
	bl	snprintf
	add	r4, r4, r0
	ldrh	r3, [r5, #224]
	ldr	r2, .L295+72
	mov	r1, #64
	mov	r0, r4
	bl	snprintf
	add	r4, r4, r0
	ldr	r3, [r5, #2600]
	ldr	r2, .L295+76
	mov	r1, #64
	mov	r0, r4
	bl	snprintf
	add	r4, r4, r0
	ldr	r3, [r5, #2604]
	ldr	r2, .L295+80
	mov	r1, #64
	mov	r0, r4
	bl	snprintf
	add	r4, r4, r0
	ldr	r3, [r5, #2608]
	ldr	r2, .L295+84
	mov	r1, #64
	mov	r0, r4
	bl	snprintf
	add	r4, r4, r0
	ldr	r3, [r5, #2612]
	ldr	r2, .L295+88
	mov	r1, #64
	mov	r0, r4
	bl	snprintf
	add	r4, r4, r0
	ldr	r3, [r5, #2616]
	ldr	r2, .L295+92
	mov	r1, #64
	mov	r0, r4
	bl	snprintf
	add	r4, r4, r0
	ldr	r3, [r5, #2620]
	ldr	r2, .L295+96
	mov	r1, #64
	mov	r0, r4
	bl	snprintf
	add	r4, r4, r0
	ldrh	r3, [r8, #30]
	ldr	r2, .L295+100
	mov	r1, #64
	mov	r0, r4
	bl	snprintf
	add	r4, r4, r0
	ldrh	r3, [r8, #28]
	ldr	r2, .L295+104
	mov	r1, #64
	mov	r0, r4
	bl	snprintf
	add	r4, r4, r0
	ldr	r3, [r5, #340]
	ldr	r2, .L295+108
	mov	r1, #64
	mov	r0, r4
	bl	snprintf
	add	r4, r4, r0
	ldr	r3, [r5, #332]
	ldr	r2, .L295+112
	mov	r1, #64
	mov	r0, r4
	bl	snprintf
	add	r4, r4, r0
	ldr	r3, [r5, #228]
	ldr	r2, .L295+116
	mov	r1, #64
	mov	r0, r4
	bl	snprintf
	add	r3, r5, #412
	add	r4, r4, r0
	ldrh	r3, [r3, #6]
	mov	r1, #64
	ldr	r2, .L295+120
	mov	r0, r4
	bl	snprintf
	add	r4, r4, r0
	ldrh	r3, [r5, #240]
	ldr	r2, .L295+124
	mov	r1, #64
	mov	r0, r4
	bl	snprintf
	add	r3, r5, #2624
	add	r4, r4, r0
	ldrh	r3, [r3]
	mov	r1, #64
	ldr	r2, .L295+128
	mov	r0, r4
	bl	snprintf
	add	r4, r4, r0
	ldr	r3, [r5, #244]
	ldr	r2, .L295+132
	mov	r1, #64
	mov	r0, r4
	bl	snprintf
	movw	r3, #2628
	add	r4, r4, r0
	ldrh	r3, [r5, r3]
	mov	r1, #64
	ldr	r2, .L295+136
	mov	r0, r4
	bl	snprintf
	add	r4, r4, r0
	ldrh	r3, [r7]
	ldr	r2, .L295+140
	mov	r1, #64
	mov	r0, r4
	bl	snprintf
	add	r4, r4, r0
	ldrh	r3, [r5, #26]
	ldr	r2, .L295+144
	mov	r1, #64
	mov	r0, r4
	bl	snprintf
	add	r4, r4, r0
	ldrb	r3, [r5, #30]	@ zero_extendqisi2
	ldr	r2, .L295+148
	mov	r1, #64
	mov	r0, r4
	bl	snprintf
	add	r4, r4, r0
	ldrh	r3, [r5, #24]
	ldr	r2, .L295+152
	mov	r1, #64
	mov	r0, r4
	bl	snprintf
	add	r4, r4, r0
	ldrb	r3, [r5, #32]	@ zero_extendqisi2
	ldr	r2, .L295+156
	mov	r1, #64
	mov	r0, r4
	bl	snprintf
	add	r4, r4, r0
	ldrh	r3, [r5, #28]
	ldr	r2, .L295+160
	mov	r1, #64
	mov	r0, r4
	bl	snprintf
	ldrh	r3, [r5, #24]
	add	r4, r4, r0
	ldr	r2, [r5, #72]
	mov	r1, #64
	mov	r0, r4
	lsl	r3, r3, #1
	ldrh	r3, [r2, r3]
	ldr	r2, .L295+164
	bl	snprintf
	add	r4, r4, r0
	ldrh	r3, [r5, #78]
	ldr	r2, .L295+168
	mov	r1, #64
	mov	r0, r4
	bl	snprintf
	add	r4, r4, r0
	ldrb	r3, [r5, #82]	@ zero_extendqisi2
	ldr	r2, .L295+172
	mov	r1, #64
	mov	r0, r4
	bl	snprintf
	add	r4, r4, r0
	ldrh	r3, [r5, #76]
	ldr	r2, .L295+176
	mov	r1, #64
	mov	r0, r4
	bl	snprintf
	add	r4, r4, r0
	ldrb	r3, [r5, #84]	@ zero_extendqisi2
	ldr	r2, .L295+180
	mov	r1, #64
	mov	r0, r4
	bl	snprintf
	add	r4, r4, r0
	ldrh	r3, [r5, #80]
	ldr	r2, .L295+184
	mov	r1, #64
	mov	r0, r4
	bl	snprintf
	ldrh	r3, [r5, #76]
	add	r4, r4, r0
	ldr	r2, [r5, #72]
	mov	r1, #64
	mov	r0, r4
	lsl	r3, r3, #1
	ldrh	r3, [r2, r3]
	ldr	r2, .L295+188
	bl	snprintf
	add	r4, r4, r0
	ldrh	r3, [r5, #126]
	ldr	r2, .L295+192
	mov	r1, #64
	mov	r0, r4
	bl	snprintf
	add	r4, r4, r0
	ldrb	r3, [r5, #130]	@ zero_extendqisi2
	ldr	r2, .L295+196
	mov	r1, #64
	mov	r0, r4
	bl	snprintf
	add	r4, r4, r0
	ldrh	r3, [r5, #124]
	ldr	r2, .L295+200
	mov	r1, #64
	mov	r0, r4
	bl	snprintf
	add	r4, r4, r0
	ldrb	r3, [r5, #132]	@ zero_extendqisi2
	ldr	r2, .L295+204
	mov	r1, #64
	mov	r0, r4
	bl	snprintf
	add	r4, r4, r0
	ldrh	r3, [r5, #128]
	ldr	r2, .L295+208
	mov	r1, #64
	mov	r0, r4
	bl	snprintf
	add	r4, r4, r0
	ldrh	r3, [r5, #174]
	ldr	r2, .L295+212
	mov	r1, #64
	mov	r0, r4
	bl	snprintf
	add	r4, r4, r0
	ldrb	r3, [r5, #178]	@ zero_extendqisi2
	ldr	r2, .L295+216
	mov	r1, #64
	mov	r0, r4
	bl	snprintf
	add	r4, r4, r0
	ldrh	r3, [r5, #172]
	ldr	r2, .L295+220
	mov	r1, #64
	mov	r0, r4
	bl	snprintf
	add	r4, r4, r0
	ldrb	r3, [r5, #180]	@ zero_extendqisi2
	ldr	r2, .L295+224
	mov	r1, #64
	mov	r0, r4
	bl	snprintf
	add	r4, r4, r0
	ldrh	r3, [r5, #176]
	ldr	r2, .L295+228
	mov	r1, #64
	mov	r0, r4
	bl	snprintf
	ldr	r3, [r5, #2720]
	add	r4, r4, r0
	ldr	r2, .L295+232
	mov	r1, #64
	mov	r0, r4
	str	r3, [sp, #4]
	ldr	r3, [r5, #2728]
	str	r3, [sp]
	ldr	r3, [r5, #2724]
	bl	snprintf
	add	r4, r4, r0
	ldr	r3, [r5, #2716]
	ldr	r2, .L295+236
	mov	r1, #64
	mov	r0, r4
	bl	snprintf
	add	r4, r4, r0
	ldr	r3, [r5, #2740]
	ldr	r2, .L295+240
	mov	r1, #64
	mov	r0, r4
	bl	snprintf
	movw	r3, #3156
	add	r4, r4, r0
	ldrh	r3, [r5, r3]
	mov	r1, #64
	ldr	r2, .L295+244
	mov	r0, r4
	bl	snprintf
	movw	r3, #3158
	add	r4, r4, r0
	ldrh	r3, [r5, r3]
	mov	r1, #64
	ldr	r2, .L295+248
	mov	r0, r4
	bl	snprintf
	add	r4, r4, r0
	ldr	r3, [r5, #3160]
	ldr	r2, .L295+252
	mov	r1, #64
	mov	r0, r4
	bl	snprintf
	movw	r3, #3164
	add	r4, r4, r0
	ldrh	r3, [r5, r3]
	mov	r1, #64
	ldr	r2, .L295+256
	mov	r0, r4
	bl	snprintf
	add	r4, r4, r0
	bl	GetFreeBlockMinEraseCount
	ldr	r2, .L295+260
	mov	r3, r0
	mov	r1, #64
	mov	r0, r4
	bl	snprintf
	add	r4, r4, r0
	ldrh	r0, [r5, #224]
	bl	GetFreeBlockMaxEraseCount
	ldr	r2, .L295+264
	mov	r3, r0
	mov	r1, #64
	mov	r0, r4
	bl	snprintf
	ldr	r3, .L295+268
	add	r4, r4, r0
	ldr	r3, [r3]
	cmp	r3, #1
	beq	.L283
.L288:
	sub	r0, r4, r6
	add	sp, sp, #20
	@ sp needed
	pop	{r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L283:
	ldrh	r3, [r5, #172]
	movw	r2, #65535
	cmp	r3, r2
	beq	.L285
	ldr	r2, [r5, #72]
	lsl	r3, r3, #1
	mov	r0, r4
	mov	r1, #64
	ldrh	r3, [r2, r3]
	ldr	r2, .L295+272
	bl	snprintf
	add	r4, r4, r0
.L285:
	mov	r0, #0
	ldr	r9, .L295+276
	bl	List_get_gc_head_node
	uxth	r0, r0
	mov	r7, #0
	movw	fp, #65535
	mov	r10, #6
.L287:
	cmp	r0, fp
	beq	.L286
	ldr	r3, [r5, #2536]
	lsl	r2, r0, #1
	mul	r8, r10, r0
	mov	r1, #64
	ldrh	r3, [r3, r2]
	str	r3, [sp, #12]
	ldr	r3, [r5, #2516]
	add	r3, r3, r8
	ldrh	r3, [r3, #4]
	str	r3, [sp, #8]
	ldr	r3, [r5, #72]
	ldrh	r3, [r3, r2]
	mov	r2, r9
	stm	sp, {r0, r3}
	mov	r3, r7
	mov	r0, r4
	add	r7, r7, #1
	bl	snprintf
	ldr	r3, [r5, #2516]
	cmp	r7, #16
	add	r4, r4, r0
	ldrh	r0, [r3, r8]
	bne	.L287
.L286:
	ldr	r2, [r5, #2516]
	mov	r7, #0
	ldr	r3, [r5, #2532]
	movw	r9, #65535
	ldr	fp, .L295+280
	mov	r10, #6
	sub	r3, r3, r2
	ldr	r2, .L295+284
	asr	r3, r3, #1
	mul	r3, r2, r3
	uxth	r3, r3
.L289:
	cmp	r3, r9
	beq	.L288
	ldr	r1, [r5, #2536]
	lsl	r2, r3, #1
	mul	r8, r10, r3
	mov	r0, r4
	ldrh	r2, [r1, r2]
	mov	r1, #64
	str	r2, [sp, #8]
	ldr	r2, [r5, #2516]
	add	r2, r2, r8
	ldrh	r2, [r2, #4]
	str	r3, [sp]
	mov	r3, r7
	add	r7, r7, #1
	str	r2, [sp, #4]
	mov	r2, fp
	bl	snprintf
	cmp	r7, #4
	add	r4, r4, r0
	beq	.L288
	ldr	r3, [r5, #2516]
	ldrh	r3, [r3, r8]
	b	.L289
.L296:
	.align	2
.L295:
	.word	.LANCHOR0
	.word	.LC9
	.word	.LC10
	.word	.LC11
	.word	.LC12
	.word	.LC13
	.word	.LC14
	.word	.LANCHOR0+2468
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
	.word	.LANCHOR2
	.word	.LC74
	.word	.LC75
	.word	.LC76
	.word	-1431655765
	.fnend
	.size	FtlPrintInfo2buf, .-FtlPrintInfo2buf
	.align	2
	.global	rknand_proc_ftlread
	.syntax unified
	.arm
	.fpu softvfp
	.type	rknand_proc_ftlread, %function
rknand_proc_ftlread:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	cmp	r0, #2048
	blt	.L299
	push	{r4, r5, r6, lr}
	.save {r4, r5, r6, lr}
	mov	r5, r1
	ldr	r3, .L304
	mov	r1, #64
	mov	r0, r5
	ldr	r2, .L304+4
	bl	snprintf
	add	r4, r5, r0
	mov	r0, r4
	bl	FtlPrintInfo2buf
	add	r0, r4, r0
	sub	r0, r0, r5
	pop	{r4, r5, r6, pc}
.L299:
	mov	r0, #0
	bx	lr
.L305:
	.align	2
.L304:
	.word	.LC0
	.word	.LC77
	.fnend
	.size	rknand_proc_ftlread, .-rknand_proc_ftlread
	.align	2
	.global	GetSwlReplaceBlock
	.syntax unified
	.arm
	.fpu softvfp
	.type	GetSwlReplaceBlock, %function
GetSwlReplaceBlock:
	.fnstart
	@ args = 0, pretend = 0, frame = 8
	@ frame_needed = 0, uses_anonymous_args = 0
	push	{r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.save {r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.pad #28
	sub	sp, sp, #28
	ldr	r4, .L331
	ldr	r2, [r4, #2608]
	ldr	r3, [r4, #2620]
	cmp	r2, r3
	bcs	.L307
	ldr	r2, [r4, #2536]
	mov	r3, #0
	ldrh	r1, [r4, #240]
	str	r3, [r4, #2600]
	sub	r2, r2, #2
.L308:
	cmp	r3, r1
	bcc	.L309
	ldr	r5, [r4, #2600]
	mov	r0, r5
	bl	__aeabi_uidiv
	ldr	r3, .L331+4
	str	r0, [r4, #2608]
	ldr	r0, [r4, #2604]
	ldrh	r1, [r3]
	sub	r0, r5, r0
	bl	__aeabi_uidiv
	str	r0, [r4, #2600]
.L310:
	ldr	r6, [r4, #2620]
	ldr	r7, [r4, #2608]
	add	r3, r6, #256
	cmp	r3, r7
	bls	.L315
	ldr	r2, [r4, #2616]
	add	r3, r6, #768
	cmp	r3, r2
	bls	.L315
.L317:
	movw	r5, #65535
.L316:
	mov	r0, r5
	add	sp, sp, #28
	@ sp needed
	pop	{r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L309:
	ldrh	r0, [r2, #2]!
	add	r3, r3, #1
	ldr	ip, [r4, #2600]
	add	r0, r0, ip
	str	r0, [r4, #2600]
	b	.L308
.L307:
	ldr	r3, [r4, #2616]
	cmp	r2, r3
	addhi	r3, r3, #1
	strhi	r3, [r4, #2616]
	movhi	r3, #0
	bls	.L310
.L312:
	ldrh	r2, [r4, #240]
	cmp	r3, r2
	bcs	.L310
	ldr	r0, [r4, #2536]
	lsl	r1, r3, #1
	add	r3, r3, #1
	ldrh	r2, [r0, r1]
	add	r2, r2, #1
	strh	r2, [r0, r1]	@ movhi
	b	.L312
.L315:
	ldrh	r0, [r4, #224]
	add	r0, r0, r0, lsl #1
	ubfx	r0, r0, #2, #16
	bl	GetFreeBlockMaxEraseCount
	add	r3, r6, #64
	mov	r9, r0
	cmp	r0, r3
	bcc	.L317
	ldr	r3, [r4, #2520]
	cmp	r3, #0
	beq	.L317
	ldrh	r2, [r4, #240]
	movw	r1, #65535
	ldr	lr, [r4, #2516]
	mov	r0, #0
	ldr	r8, [r4, #2536]
	mov	fp, #6
	ldr	r10, .L331+8
	str	r2, [sp, #20]
	mov	r2, r1
.L318:
	ldrh	ip, [r3]
	movw	r5, #65535
	cmp	ip, r5
	bne	.L321
	mov	r5, r2
.L320:
	movw	r3, #65535
	cmp	r5, r3
	beq	.L317
	lsl	fp, r5, #1
	ldrh	r10, [r8, fp]
	cmp	r6, r10
	bcs	.L322
	bl	GetFreeBlockMinEraseCount
	cmp	r6, r0
	strcc	r1, [r4, #2620]
.L322:
	cmp	r7, r10
	bls	.L317
	add	r3, r10, #128
	cmp	r9, r3
	ble	.L317
	add	r3, r10, #256
	cmp	r7, r3
	bhi	.L323
	ldr	r3, [r4, #2616]
	add	r10, r10, #768
	cmp	r10, r3
	bcs	.L317
.L323:
	str	r9, [sp, #8]
	mov	r2, r7
	ldrh	r3, [r8, fp]
	mov	r1, r5
	ldr	r0, .L331+12
	str	r3, [sp, #4]
	ldr	r3, [r4, #72]
	ldrh	r3, [r3, fp]
	str	r3, [sp]
	ldr	r3, [r4, #2616]
	bl	sftl_printk
	mov	r3, #1
	str	r3, [r4, #3168]
	b	.L316
.L321:
	add	r0, r0, #1
	ldr	r5, [sp, #20]
	uxth	r0, r0
	cmp	r0, r5
	bhi	.L317
	ldrh	r5, [r3, #4]
	cmp	r5, #0
	beq	.L319
	sub	r3, r3, lr
	asr	r3, r3, #1
	mul	r3, r10, r3
	uxth	r5, r3
	lsl	r3, r5, #1
	ldrh	r3, [r8, r3]
	cmp	r6, r3
	bcs	.L320
	cmp	r1, r3
	movhi	r1, r3
	movhi	r2, r5
.L319:
	mla	r3, fp, ip, lr
	b	.L318
.L332:
	.align	2
.L331:
	.word	.LANCHOR0
	.word	.LANCHOR0+292
	.word	-1431655765
	.word	.LC78
	.fnend
	.size	GetSwlReplaceBlock, .-GetSwlReplaceBlock
	.align	2
	.global	free_data_superblock
	.syntax unified
	.arm
	.fpu softvfp
	.type	free_data_superblock, %function
free_data_superblock:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	movw	r2, #65535
	cmp	r0, r2
	beq	.L336
	ldr	r2, .L339
	lsl	r3, r0, #1
	push	{r4, lr}
	.save {r4, lr}
	mov	r1, #0
	ldr	r2, [r2, #72]
	strh	r1, [r2, r3]	@ movhi
	bl	INSERT_FREE_LIST
	mov	r0, #0
	pop	{r4, pc}
.L336:
	mov	r0, #0
	bx	lr
.L340:
	.align	2
.L339:
	.word	.LANCHOR0
	.fnend
	.size	free_data_superblock, .-free_data_superblock
	.align	2
	.global	get_new_active_ppa
	.syntax unified
	.arm
	.fpu softvfp
	.type	get_new_active_ppa, %function
get_new_active_ppa:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	push	{r4, r5, r6, lr}
	.save {r4, r5, r6, lr}
	movw	r3, #65535
	ldrh	r2, [r0]
	mov	r4, r0
	cmp	r2, r3
	bne	.L342
	movw	r2, #2785
	ldr	r1, .L357
	ldr	r0, .L357+4
	bl	sftl_printk
.L342:
	ldr	r6, .L357+8
	movw	r3, #302
	ldrh	r2, [r4, #2]
	ldrh	r3, [r6, r3]
	cmp	r2, r3
	bne	.L343
	movw	r2, #2786
	ldr	r1, .L357
	ldr	r0, .L357+4
	bl	sftl_printk
.L343:
	ldrh	r3, [r4, #4]
	cmp	r3, #0
	bne	.L344
	movw	r2, #2787
	ldr	r1, .L357
	ldr	r0, .L357+4
	bl	sftl_printk
.L344:
	ldrb	r2, [r4, #6]	@ zero_extendqisi2
	mov	r3, #0
	ldrh	r1, [r6, #232]
	movw	ip, #65535
	strb	r3, [r4, #10]
	add	r2, r4, r2, lsl #1
	ldrh	r0, [r2, #16]
	mov	r2, r3
.L345:
	cmp	r0, ip
	beq	.L347
	movw	r3, #302
	ldrh	r5, [r4, #2]
	ldrh	ip, [r6, r3]
	cmp	r5, ip
	movwcs	r5, #65535
	bcs	.L341
	ldrh	r2, [r4, #4]
	orr	r5, r5, r0, lsl #10
	ldrb	r3, [r4, #6]	@ zero_extendqisi2
	movw	lr, #65535
	sub	r2, r2, #1
	uxth	r2, r2
	strh	r2, [r4, #4]	@ movhi
.L350:
	add	r3, r3, #1
	uxtb	r3, r3
	cmp	r1, r3
	ldrheq	r0, [r4, #2]
	moveq	r3, #0
	addeq	r0, r0, #1
	strheq	r0, [r4, #2]	@ movhi
	add	r0, r4, r3, lsl #1
	ldrh	r0, [r0, #16]
	cmp	r0, lr
	beq	.L350
	strb	r3, [r4, #6]
	cmp	r2, #0
	ldrh	r3, [r4, #2]
	sub	r3, r3, ip
	clz	r3, r3
	lsr	r3, r3, #5
	moveq	r3, #0
	cmp	r3, #0
	beq	.L341
	movw	r2, #2810
	ldr	r1, .L357
	ldr	r0, .L357+4
	bl	sftl_printk
.L341:
	mov	r0, r5
	pop	{r4, r5, r6, pc}
.L347:
	ldrb	r3, [r4, #6]	@ zero_extendqisi2
	add	r3, r3, #1
	uxtb	r3, r3
	cmp	r3, r1
	strb	r3, [r4, #6]
	ldrheq	r3, [r4, #2]
	strbeq	r2, [r4, #6]
	addeq	r3, r3, #1
	strheq	r3, [r4, #2]	@ movhi
	ldrb	r3, [r4, #6]	@ zero_extendqisi2
	add	r3, r4, r3, lsl #1
	ldrh	r0, [r3, #16]
	b	.L345
.L358:
	.align	2
.L357:
	.word	.LANCHOR1+111
	.word	.LC8
	.word	.LANCHOR0
	.fnend
	.size	get_new_active_ppa, .-get_new_active_ppa
	.align	2
	.global	FtlGcBufInit
	.syntax unified
	.arm
	.fpu softvfp
	.type	FtlGcBufInit, %function
FtlGcBufInit:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	push	{r4, r5, r6, r7, r8, r9, lr}
	.save {r4, r5, r6, r7, r8, r9, lr}
	mov	lr, #12
	ldr	r5, .L365
	mov	r4, #1
	mov	r6, #20
	mov	r3, #0
	ldr	r2, .L365+4
	str	r3, [r2, #3172]
.L360:
	ldrh	r1, [r2, #232]
	uxth	r0, r3
	add	ip, r3, #1
	cmp	r0, r1
	bcc	.L361
	ldr	r4, .L365
	mov	ip, #12
	mov	lr, #0
.L362:
	ldr	r3, [r2, #3192]
	cmp	r1, r3
	bcc	.L363
	pop	{r4, r5, r6, r7, r8, r9, pc}
.L361:
	uxth	r3, r3
	ldr	r7, [r2, #3176]
	mul	r0, lr, r3
	add	r1, r7, r0
	str	r4, [r1, #8]
	ldrh	r1, [r5]
	mul	r1, r3, r1
	add	r8, r1, #3
	cmp	r1, #0
	movlt	r1, r8
	ldr	r8, [r2, #3180]
	bic	r1, r1, #3
	add	r1, r8, r1
	str	r1, [r7, r0]
	ldr	r1, .L365+8
	ldr	r8, [r2, #3176]
	ldrh	r1, [r1]
	add	r7, r8, r0
	mul	r1, r3, r1
	add	r9, r1, #3
	cmp	r1, #0
	movlt	r1, r9
	ldr	r9, [r2, #3184]
	bic	r1, r1, #3
	add	r1, r9, r1
	str	r1, [r7, #4]
	ldr	r1, [r2, #3188]
	mla	r3, r6, r3, r1
	ldr	r1, [r8, r0]
	str	r1, [r3, #8]
	ldr	r1, [r7, #4]
	str	r1, [r3, #12]
	mov	r3, ip
	b	.L360
.L363:
	mul	r5, ip, r1
	ldr	r6, [r2, #3176]
	add	r3, r6, r5
	str	lr, [r3, #8]
	ldrh	r3, [r4]
	mul	r3, r1, r3
	add	r0, r3, #3
	cmp	r3, #0
	movlt	r3, r0
	ldr	r0, [r2, #3180]
	bic	r3, r3, #3
	add	r3, r0, r3
	str	r3, [r6, r5]
	ldr	r3, .L365+8
	ldr	r0, [r2, #3176]
	ldrh	r3, [r3]
	add	r0, r0, r5
	mul	r3, r1, r3
	add	r1, r1, #1
	uxth	r1, r1
	add	r5, r3, #3
	cmp	r3, #0
	movlt	r3, r5
	ldr	r5, [r2, #3184]
	bic	r3, r3, #3
	add	r3, r5, r3
	str	r3, [r0, #4]
	b	.L362
.L366:
	.align	2
.L365:
	.word	.LANCHOR0+310
	.word	.LANCHOR0
	.word	.LANCHOR0+312
	.fnend
	.size	FtlGcBufInit, .-FtlGcBufInit
	.align	2
	.global	FtlGcBufFree
	.syntax unified
	.arm
	.fpu softvfp
	.type	FtlGcBufFree, %function
FtlGcBufFree:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	ldr	r3, .L375
	push	{r4, r5, r6, r7, r8, r9, r10, lr}
	.save {r4, r5, r6, r7, r8, r9, r10, lr}
	mov	lr, #0
	mov	r5, #20
	mov	r7, #12
	mov	r8, lr
	ldr	r6, [r3, #3192]
	ldr	r4, [r3, #3176]
.L368:
	uxth	r3, lr
	cmp	r1, r3
	popls	{r4, r5, r6, r7, r8, r9, r10, pc}
	mla	ip, r5, r3, r0
	mov	r2, #0
.L369:
	uxth	r3, r2
	cmp	r6, r3
	bls	.L370
	mul	r3, r7, r3
	add	r2, r2, #1
	ldr	r10, [r4, r3]
	add	r9, r4, r3
	ldr	r3, [ip, #8]
	cmp	r10, r3
	bne	.L369
	str	r8, [r9, #8]
.L370:
	add	lr, lr, #1
	b	.L368
.L376:
	.align	2
.L375:
	.word	.LANCHOR0
	.fnend
	.size	FtlGcBufFree, .-FtlGcBufFree
	.align	2
	.global	FtlGcBufAlloc
	.syntax unified
	.arm
	.fpu softvfp
	.type	FtlGcBufAlloc, %function
FtlGcBufAlloc:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	ldr	r3, .L385
	mov	ip, #0
	push	{r4, r5, r6, r7, r8, r9, lr}
	.save {r4, r5, r6, r7, r8, r9, lr}
	mov	r6, #12
	mov	r7, #1
	mov	r8, #20
	ldr	r4, [r3, #3192]
	ldr	r5, [r3, #3176]
.L378:
	uxth	r2, ip
	cmp	r1, r2
	bhi	.L382
	pop	{r4, r5, r6, r7, r8, r9, pc}
.L382:
	mov	lr, #0
.L379:
	uxth	r3, lr
	cmp	r4, r3
	bls	.L380
	mla	r3, r6, r3, r5
	add	lr, lr, #1
	ldr	r9, [r3, #8]
	cmp	r9, #0
	bne	.L379
	mla	r2, r8, r2, r0
	ldr	lr, [r3]
	str	r7, [r3, #8]
	str	lr, [r2, #8]
	ldr	r3, [r3, #4]
	str	r3, [r2, #12]
.L380:
	add	ip, ip, #1
	b	.L378
.L386:
	.align	2
.L385:
	.word	.LANCHOR0
	.fnend
	.size	FtlGcBufAlloc, .-FtlGcBufAlloc
	.align	2
	.global	IsBlkInGcList
	.syntax unified
	.arm
	.fpu softvfp
	.type	IsBlkInGcList, %function
IsBlkInGcList:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	@ link register save eliminated.
	ldr	r2, .L392
	ldr	r3, [r2, #3196]
	add	r2, r2, #3200
	ldrh	r2, [r2]
	add	r2, r3, r2, lsl #1
.L388:
	cmp	r3, r2
	bne	.L390
	mov	r0, #0
	bx	lr
.L390:
	ldrh	r1, [r3], #2
	cmp	r1, r0
	bne	.L388
	mov	r0, #1
	bx	lr
.L393:
	.align	2
.L392:
	.word	.LANCHOR0
	.fnend
	.size	IsBlkInGcList, .-IsBlkInGcList
	.align	2
	.global	FtlGcUpdatePage
	.syntax unified
	.arm
	.fpu softvfp
	.type	FtlGcUpdatePage, %function
FtlGcUpdatePage:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	push	{r4, r5, r6, r7, r8, lr}
	.save {r4, r5, r6, r7, r8, lr}
	mov	r4, r0
	ubfx	r0, r0, #10, #16
	mov	r5, r1
	mov	r6, r2
	bl	P2V_block_in_plane
	ldr	r3, .L399
	mov	ip, #0
	add	lr, r3, #3200
	ldr	r1, [r3, #3196]
	ldrh	r7, [lr]
	sub	r2, r1, #2
.L395:
	uxth	r8, ip
	cmp	r8, r7
	bcc	.L397
	moveq	ip, r8
	lsleq	ip, ip, #1
	strheq	r0, [r1, ip]	@ movhi
	ldrheq	r2, [lr]
	addeq	r2, r2, #1
	strheq	r2, [lr]	@ movhi
	b	.L396
.L397:
	ldrh	r8, [r2, #2]!
	add	ip, ip, #1
	cmp	r8, r0
	bne	.L395
.L396:
	movw	ip, #3208
	mov	r0, #12
	ldrh	r2, [r3, ip]
	mul	r0, r0, r2
	ldr	r2, [r3, #3204]
	add	r1, r2, r0
	stmib	r1, {r5, r6}
	str	r4, [r2, r0]
	ldrh	r2, [r3, ip]
	add	r2, r2, #1
	strh	r2, [r3, ip]	@ movhi
	pop	{r4, r5, r6, r7, r8, pc}
.L400:
	.align	2
.L399:
	.word	.LANCHOR0
	.fnend
	.size	FtlGcUpdatePage, .-FtlGcUpdatePage
	.align	2
	.global	FtlGcRefreshBlock
	.syntax unified
	.arm
	.fpu softvfp
	.type	FtlGcRefreshBlock, %function
FtlGcRefreshBlock:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	push	{r4, lr}
	.save {r4, lr}
	mov	r1, r0
	mov	r4, r0
	ldr	r0, .L405
	bl	sftl_printk
	ldr	r3, .L405+4
	ldrh	r0, [r3, #222]
	cmp	r4, r0
	beq	.L402
	ldrh	r1, [r3, #220]
	cmp	r4, r1
	beq	.L402
	movw	r2, #65535
	cmp	r0, r2
	strheq	r4, [r3, #222]	@ movhi
	beq	.L402
	cmp	r1, r2
	strheq	r4, [r3, #220]	@ movhi
.L402:
	mov	r0, #0
	pop	{r4, pc}
.L406:
	.align	2
.L405:
	.word	.LC79
	.word	.LANCHOR0
	.fnend
	.size	FtlGcRefreshBlock, .-FtlGcRefreshBlock
	.align	2
	.global	FtlGcMarkBadPhyBlk
	.syntax unified
	.arm
	.fpu softvfp
	.type	FtlGcMarkBadPhyBlk, %function
FtlGcMarkBadPhyBlk:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	push	{r4, r5, r6, r7, r8, lr}
	.save {r4, r5, r6, r7, r8, lr}
	mov	r5, r0
	ldr	r4, .L412
	movw	r6, #3210
	bl	P2V_block_in_plane
	mov	r2, r5
	mov	r7, r0
	ldrh	r1, [r4, r6]
	ldr	r0, .L412+4
	bl	sftl_printk
	mov	r0, r7
	bl	FtlGcRefreshBlock
	ldrh	r3, [r4, r6]
	mov	r2, #0
	ldr	r0, .L412+8
.L408:
	uxth	r1, r2
	cmp	r3, r1
	bhi	.L410
	cmp	r3, #15
	movwls	r2, #3210
	addls	r1, r3, #1
	strhls	r1, [r4, r2]	@ movhi
	lslls	r3, r3, #1
	ldrls	r2, .L412+8
	strhls	r5, [r2, r3]	@ movhi
	b	.L409
.L410:
	add	r2, r2, #1
	add	r1, r0, r2, lsl #1
	ldrh	r1, [r1, #-2]
	cmp	r1, r5
	bne	.L408
.L409:
	mov	r0, #0
	pop	{r4, r5, r6, r7, r8, pc}
.L413:
	.align	2
.L412:
	.word	.LANCHOR0
	.word	.LC80
	.word	.LANCHOR0+3212
	.fnend
	.size	FtlGcMarkBadPhyBlk, .-FtlGcMarkBadPhyBlk
	.align	2
	.global	FtlGcReFreshBadBlk
	.syntax unified
	.arm
	.fpu softvfp
	.type	FtlGcReFreshBadBlk, %function
FtlGcReFreshBadBlk:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	push	{r4, r5, r6, lr}
	.save {r4, r5, r6, lr}
	movw	r3, #3210
	ldr	r4, .L421
	ldrh	r2, [r4, r3]
	cmp	r2, #0
	beq	.L415
	ldrh	r1, [r4, #222]
	movw	r3, #65535
	cmp	r1, r3
	bne	.L415
	movw	r3, #3246
	movw	r5, #3246
	ldrh	r1, [r4, r3]
	cmp	r1, r2
	movcs	r2, #0
	strhcs	r2, [r4, r3]	@ movhi
	ldrh	r3, [r4, r5]
	ldr	r2, .L421+4
	lsl	r3, r3, #1
	ldrh	r0, [r2, r3]
	bl	P2V_block_in_plane
	bl	FtlGcRefreshBlock
	ldrh	r3, [r4, r5]
	add	r3, r3, #1
	strh	r3, [r4, r5]	@ movhi
.L415:
	mov	r0, #0
	pop	{r4, r5, r6, pc}
.L422:
	.align	2
.L421:
	.word	.LANCHOR0
	.word	.LANCHOR0+3212
	.fnend
	.size	FtlGcReFreshBadBlk, .-FtlGcReFreshBadBlk
	.align	2
	.global	ftl_malloc
	.syntax unified
	.arm
	.fpu softvfp
	.type	ftl_malloc, %function
ftl_malloc:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	@ link register save eliminated.
	ldr	r1, .L424
	b	__kmalloc
.L425:
	.align	2
.L424:
	.word	6291649
	.fnend
	.size	ftl_malloc, .-ftl_malloc
	.align	2
	.global	ftl_free
	.syntax unified
	.arm
	.fpu softvfp
	.type	ftl_free, %function
ftl_free:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	@ link register save eliminated.
	b	kfree
	.fnend
	.size	ftl_free, .-ftl_free
	.align	2
	.global	rknand_print_hex
	.syntax unified
	.arm
	.fpu softvfp
	.type	rknand_print_hex, %function
rknand_print_hex:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	push	{r3, r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.save {r3, r4, r5, r6, r7, r8, r9, r10, fp, lr}
	mov	r6, #0
	ldr	fp, .L437
	mov	r10, r0
	mov	r7, r1
	mov	r8, r2
	mov	r9, r3
	mov	r5, r6
	mov	r4, r6
.L428:
	cmp	r4, r9
	bcc	.L434
	ldr	r0, .L437+4
	pop	{r3, r4, r5, r6, r7, r8, r9, r10, fp, lr}
	b	sftl_printk
.L434:
	cmp	r5, #0
	bne	.L429
	mov	r3, r6
	mov	r2, r7
	mov	r1, r10
	ldr	r0, .L437+8
	bl	sftl_printk
.L429:
	cmp	r8, #4
	ldreq	r1, [r7, r4, lsl #2]
	ldreq	r0, .L437+12
	beq	.L436
	cmp	r8, #2
	lsleq	r3, r4, #1
	ldrbne	r1, [r7, r4]	@ zero_extendqisi2
	ldrne	r0, .L437+16
	moveq	r0, fp
	ldrheq	r1, [r7, r3]
.L436:
	add	r5, r5, #1
	bl	sftl_printk
	cmp	r5, #15
	bls	.L433
	mov	r5, #0
	ldr	r0, .L437+4
	bl	sftl_printk
.L433:
	add	r4, r4, #1
	add	r6, r6, r8
	b	.L428
.L438:
	.align	2
.L437:
	.word	.LC83
	.word	.LC85
	.word	.LC81
	.word	.LC82
	.word	.LC84
	.fnend
	.size	rknand_print_hex, .-rknand_print_hex
	.align	2
	.global	FlashEraseBlocks
	.syntax unified
	.arm
	.fpu softvfp
	.type	FlashEraseBlocks, %function
FlashEraseBlocks:
	.fnstart
	@ args = 0, pretend = 0, frame = 24
	@ frame_needed = 0, uses_anonymous_args = 0
	push	{r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.save {r4, r5, r6, r7, r8, r9, r10, fp, lr}
	mov	r8, r2
	ldr	r5, .L457
	.pad #28
	sub	sp, sp, #28
	mov	r4, r0
	mov	fp, r0
	ldr	r2, .L457+4
	mov	r7, #0
	ldrh	r9, [r5, #12]
	mvn	r10, #0
	ldr	r3, [r2]
	str	r2, [sp, #4]
	str	r3, [sp, #20]
	lsl	r3, r9, #3
	str	r3, [sp]
.L440:
	cmp	r7, r8
	beq	.L444
	add	r2, sp, #12
	add	r1, sp, #16
	mov	r0, fp
	bl	l2p_addr_tran
	ldr	r6, [sp, #12]
	cmp	r6, #0
	bne	.L441
	ldr	r2, [sp, #16]
	ldr	r3, [sp]
	cmp	r3, r2
	bls	.L441
	ldr	r5, .L457+8
	ldr	r7, .L457+12
	ldr	r9, .L457+16
	b	.L455
.L443:
	mvn	r3, #0
	ldr	r2, [sp, #16]
	str	r3, [r4, #-20]
	mov	r1, r5
	mov	r0, r7
	add	r6, r6, #1
	bl	sftl_printk
	mov	r3, #16
	mov	r2, #4
	ldr	r1, [r4, #-12]
	mov	r0, r9
	bl	rknand_print_hex
	mov	r3, #4
	ldr	r1, [r4, #-8]
	mov	r2, r3
	ldr	r0, .L457+20
	bl	rknand_print_hex
.L455:
	cmp	r6, r8
	add	r4, r4, #20
	bne	.L443
	bl	dump_stack
.L444:
	ldr	r3, [sp, #4]
	mov	r0, #0
	ldr	r2, [sp, #20]
	ldr	r3, [r3]
	cmp	r2, r3
	beq	.L451
	bl	__stack_chk_fail
.L441:
	ldr	r2, [r5, #3252]
	uxtb	r0, r6
	ldr	r1, [sp, #16]
	blx	r2
	subs	r2, r0, #0
	movne	r2, r10
	str	r2, [fp]
	ldrh	r2, [r5, #14]
	cmp	r2, #4
	bne	.L448
	ldr	r1, [sp, #16]
	ldr	r2, [r5, #3252]
	ldrb	r0, [sp, #12]	@ zero_extendqisi2
	add	r1, r9, r1
	blx	r2
	cmp	r0, #0
	strne	r10, [fp]
.L448:
	add	r7, r7, #1
	add	fp, fp, #20
	b	.L440
.L451:
	add	sp, sp, #28
	@ sp needed
	pop	{r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L458:
	.align	2
.L457:
	.word	.LANCHOR0
	.word	__stack_chk_guard
	.word	.LANCHOR1+130
	.word	.LC86
	.word	.LC87
	.word	.LC88
	.fnend
	.size	FlashEraseBlocks, .-FlashEraseBlocks
	.align	2
	.global	FtlFreeSysBlkQueueIn
	.syntax unified
	.arm
	.fpu softvfp
	.type	FtlFreeSysBlkQueueIn, %function
FtlFreeSysBlkQueueIn:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	sub	r3, r0, #1
	movw	r2, #65533
	uxth	r3, r3
	cmp	r3, r2
	bxhi	lr
	push	{r4, r5, r6, r7, r8, lr}
	.save {r4, r5, r6, r7, r8, lr}
	ldr	r6, .L472
	add	r4, r6, #412
	ldrh	r3, [r4, #6]
	cmp	r3, #1024
	popeq	{r4, r5, r6, r7, r8, pc}
	cmp	r1, #0
	mov	r5, r0
	beq	.L461
	bl	P2V_block_in_plane
	mov	r7, r0
	ldr	r0, [r6, #3272]
	lsl	r3, r5, #10
	mov	r2, #1
	mov	r1, r2
	str	r3, [r0, #4]
	bl	FlashEraseBlocks
	ldr	r2, [r6, #2536]
	lsl	r0, r7, #1
	ldrh	r3, [r2, r0]
	add	r3, r3, #1
	strh	r3, [r2, r0]	@ movhi
	ldr	r3, [r6, #2612]
	add	r3, r3, #1
	str	r3, [r6, #2612]
.L461:
	ldrh	r3, [r4, #6]
	add	r3, r3, #1
	strh	r3, [r4, #6]	@ movhi
	ldrh	r3, [r4, #4]
	add	r2, r4, r3, lsl #1
	add	r3, r3, #1
	ubfx	r3, r3, #0, #10
	strh	r5, [r2, #8]	@ movhi
	strh	r3, [r4, #4]	@ movhi
	pop	{r4, r5, r6, r7, r8, pc}
.L473:
	.align	2
.L472:
	.word	.LANCHOR0
	.fnend
	.size	FtlFreeSysBlkQueueIn, .-FtlFreeSysBlkQueueIn
	.align	2
	.global	FtlFreeSysBlkQueueOut
	.syntax unified
	.arm
	.fpu softvfp
	.type	FtlFreeSysBlkQueueOut, %function
FtlFreeSysBlkQueueOut:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	push	{r4, r5, r6, lr}
	.save {r4, r5, r6, lr}
	ldr	r6, .L480
	add	r4, r6, #412
	ldrh	r2, [r4, #6]
	cmp	r2, #0
	movweq	r5, #65535
	beq	.L475
	ldrh	r3, [r4, #2]
	sub	r2, r2, #1
	ldr	r0, [r6, #3272]
	strh	r2, [r4, #6]	@ movhi
	mov	r2, #1
	add	r1, r4, r3, lsl #1
	add	r3, r3, #1
	ubfx	r3, r3, #0, #10
	ldrh	r5, [r1, #8]
	mov	r1, r2
	strh	r3, [r4, #2]	@ movhi
	lsl	r3, r5, #10
	str	r3, [r0, #4]
	bl	FlashEraseBlocks
	ldr	r3, [r6, #2612]
	add	r3, r3, #1
	str	r3, [r6, #2612]
.L475:
	sub	r3, r5, #1
	movw	r2, #65533
	uxth	r3, r3
	cmp	r3, r2
	bls	.L476
	ldrh	r2, [r4, #6]
	mov	r1, r5
	ldr	r0, .L480+4
	bl	sftl_printk
.L477:
	b	.L477
.L476:
	mov	r0, r5
	pop	{r4, r5, r6, pc}
.L481:
	.align	2
.L480:
	.word	.LANCHOR0
	.word	.LC89
	.fnend
	.size	FtlFreeSysBlkQueueOut, .-FtlFreeSysBlkQueueOut
	.align	2
	.global	ftl_map_blk_alloc_new_blk
	.syntax unified
	.arm
	.fpu softvfp
	.type	ftl_map_blk_alloc_new_blk, %function
ftl_map_blk_alloc_new_blk:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	push	{r4, r5, r6, r7, r8, lr}
	.save {r4, r5, r6, r7, r8, lr}
	mov	r3, #0
	ldrh	r1, [r0, #10]
	mov	r4, r0
	ldr	r2, [r0, #12]
.L483:
	uxth	r5, r3
	cmp	r5, r1
	bcs	.L486
	mov	r7, r2
	add	r3, r3, #1
	ldrh	r6, [r7]
	add	r2, r2, #2
	cmp	r6, #0
	bne	.L483
	bl	FtlFreeSysBlkQueueOut
	sub	r3, r0, #1
	movw	r2, #65533
	uxth	r3, r3
	mov	r1, r0
	strh	r0, [r7]	@ movhi
	cmp	r3, r2
	bls	.L484
	ldr	r3, .L491
	ldr	r0, .L491+4
	ldrh	r2, [r3, #6]
	bl	sftl_printk
.L485:
	b	.L485
.L484:
	ldr	r3, [r4, #28]
	strh	r6, [r4, #2]	@ movhi
	strh	r5, [r4]	@ movhi
	add	r3, r3, #1
	str	r3, [r4, #28]
	ldrh	r3, [r4, #8]
	add	r3, r3, #1
	strh	r3, [r4, #8]	@ movhi
.L486:
	ldrh	r3, [r4, #10]
	cmp	r3, r5
	bhi	.L488
	movw	r2, #578
	ldr	r1, .L491+8
	ldr	r0, .L491+12
	bl	sftl_printk
.L488:
	mov	r0, #0
	pop	{r4, r5, r6, r7, r8, pc}
.L492:
	.align	2
.L491:
	.word	.LANCHOR0+412
	.word	.LC90
	.word	.LANCHOR1+147
	.word	.LC8
	.fnend
	.size	ftl_map_blk_alloc_new_blk, .-ftl_map_blk_alloc_new_blk
	.align	2
	.global	ftl_memset
	.syntax unified
	.arm
	.fpu softvfp
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
	.global	FtlMemInit
	.syntax unified
	.arm
	.fpu softvfp
	.type	FtlMemInit, %function
FtlMemInit:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	push	{r4, r5, r6, r7, r8, r9, r10, lr}
	.save {r4, r5, r6, r7, r8, r9, r10, lr}
	movw	r3, #65535
	ldr	r4, .L596
	movw	r6, #306
	mov	r2, #32
	mov	r5, #0
	mov	r7, #12
	str	r3, [r4, #3280]
	mvn	r3, #0
	ldrh	r0, [r4, r6]
	add	r9, r4, #312
	strh	r3, [r4, #222]	@ movhi
	strh	r3, [r4, #220]	@ movhi
	movw	r3, #3156
	strh	r2, [r4, r3]	@ movhi
	mov	r2, #128
	movw	r3, #3158
	lsl	r0, r0, #1
	strh	r2, [r4, r3]	@ movhi
	movw	r3, #3164
	strh	r5, [r4, r3]	@ movhi
	movw	r3, #3210
	strh	r5, [r4, r3]	@ movhi
	movw	r3, #3246
	strh	r5, [r4, r3]	@ movhi
	str	r5, [r4, #2592]
	str	r5, [r4, #2596]
	str	r5, [r4, #2580]
	str	r5, [r4, #2568]
	str	r5, [r4, #2564]
	str	r5, [r4, #2572]
	str	r5, [r4, #2576]
	str	r5, [r4, #2560]
	str	r5, [r4, #2600]
	str	r5, [r4, #2604]
	str	r5, [r4, #2612]
	str	r5, [r4, #2616]
	str	r5, [r4, #2620]
	str	r5, [r4, #3276]
	str	r5, [r4, #3168]
	str	r5, [r4, #3284]
	str	r5, [r4, #3160]
	str	r5, [r4, #3288]
	bl	ftl_malloc
	str	r0, [r4, #3196]
	ldrh	r0, [r4, r6]
	mov	r6, #20
	mul	r0, r7, r0
	bl	ftl_malloc
	ldrh	r3, [r4, #232]
	str	r0, [r4, #3204]
	mul	r6, r6, r3
	lsl	r8, r6, #2
	mov	r0, r8
	bl	ftl_malloc
	str	r0, [r4, #3292]
	mov	r0, r6
	bl	ftl_malloc
	str	r0, [r4, #3296]
	mov	r0, r8
	bl	ftl_malloc
	str	r0, [r4, #3300]
	mov	r0, r6
	bl	ftl_malloc
	str	r0, [r4, #3272]
	mov	r0, r6
	bl	ftl_malloc
	ldrh	r3, [r4, #232]
	movw	r8, #310
	ldrh	r6, [r4, r8]
	str	r0, [r4, #3188]
	lsl	r3, r3, #1
	mov	r0, r6
	add	r3, r3, #1
	str	r3, [r4, #3192]
	bl	ftl_malloc
	str	r0, [r4, #3304]
	mov	r0, r6
	bl	ftl_malloc
	str	r0, [r4, #3308]
	mov	r0, r6
	bl	ftl_malloc
	str	r0, [r4, #3312]
	ldr	r0, [r4, #3192]
	mul	r0, r0, r6
	bl	ftl_malloc
	str	r0, [r4, #3180]
	mov	r0, r6
	bl	ftl_malloc
	str	r0, [r4, #3316]
	mov	r0, r6
	bl	ftl_malloc
	str	r0, [r4, #3320]
	ldr	r0, [r4, #3192]
	mul	r0, r7, r0
	bl	ftl_malloc
	str	r0, [r4, #3176]
	mov	r0, r6
	bl	ftl_malloc
	str	r0, [r4, #3324]
	mov	r0, r6
	bl	ftl_malloc
	movw	r3, #258
	str	r0, [r4, #3328]
	ldrh	r0, [r4, r3]
	lsl	r0, r0, #2
	bl	ftl_malloc
	ldrh	r3, [r9]
	ldrh	r6, [r4, #232]
	str	r0, [r4, #3332]
	mul	r6, r6, r3
	mov	r0, r6
	bl	ftl_malloc
	str	r0, [r4, #3336]
	lsl	r0, r6, #2
	add	r6, r4, #3344
	bl	ftl_malloc
	ldrh	r3, [r9]
	str	r0, [r4, #3340]
	ldr	r0, [r4, #3192]
	mul	r0, r0, r3
	bl	ftl_malloc
	str	r0, [r4, #3184]
	ldrh	r0, [r4, #242]
	lsl	r0, r0, #1
	uxth	r0, r0
	strh	r0, [r6]	@ movhi
	bl	ftl_malloc
	str	r0, [r4, #3348]
	ldrh	r0, [r6]
	ldr	r3, .L596+4
	add	r0, r0, #544
	add	r0, r0, #3
	lsr	r0, r0, #9
	strh	r0, [r6]	@ movhi
	and	r0, r3, r0, lsl #9
	bl	ftl_malloc
	ldrh	r6, [r4, #242]
	str	r0, [r4, #3352]
	add	r0, r0, #32
	str	r0, [r4, #2536]
	lsl	r6, r6, #1
	mov	r0, r6
	bl	ftl_malloc
	str	r0, [r4, #3356]
	mov	r0, r6
	bl	ftl_malloc
	ldr	r6, [r4, #328]
	str	r0, [r4, #72]
	lsl	r6, r6, #1
	mov	r0, r6
	bl	ftl_malloc
	str	r0, [r4, #3360]
	mov	r0, r6
	add	r6, r4, #320
	bl	ftl_malloc
	str	r0, [r4, #3364]
	ldrh	r0, [r6]
	lsl	r0, r0, #1
	bl	ftl_malloc
	str	r0, [r4, #348]
	ldrh	r0, [r6]
	lsl	r0, r0, #1
	bl	ftl_malloc
	str	r0, [r4, #3368]
	ldrh	r0, [r6]
	movw	r6, #322
	lsl	r0, r0, #2
	bl	ftl_malloc
	str	r0, [r4, #3372]
	ldrh	r0, [r4, r6]
	lsl	r0, r0, #2
	bl	ftl_malloc
	ldrh	r2, [r4, r6]
	mov	r1, r5
	str	r0, [r4, #3376]
	movw	r5, #338
	lsl	r2, r2, #2
	bl	ftl_memset
	add	r3, r4, #336
	ldrh	r0, [r3]
	lsl	r0, r0, #2
	bl	ftl_malloc
	str	r0, [r4, #3380]
	ldr	r0, [r4, #328]
	lsl	r0, r0, #2
	bl	ftl_malloc
	str	r0, [r4, #3384]
	ldrh	r0, [r4, r5]
	mul	r0, r7, r0
	bl	ftl_malloc
	ldrh	r3, [r4, r5]
	add	r5, r4, #3392
	str	r0, [r4, #2540]
	ldrh	r0, [r4, r8]
	mul	r0, r0, r3
	bl	ftl_malloc
	ldrh	r3, [r4, #242]
	str	r0, [r4, #3388]
	mov	r0, #6
	mul	r0, r0, r3
	bl	ftl_malloc
	movw	r3, #298
	str	r0, [r4, #2516]
	ldrh	r0, [r4, r3]
	ldrh	r3, [r4, #254]
	add	r0, r0, #31
	asr	r0, r0, #5
	strh	r0, [r5]	@ movhi
	mul	r0, r0, r3
	lsl	r0, r0, #2
	bl	ftl_malloc
	ldrh	r2, [r5]
	add	ip, r4, #380
	ldrh	lr, [r4, #254]
	mov	r3, #1
	str	r0, [r4, #380]
	lsl	r2, r2, #2
	mov	r1, r2
.L495:
	cmp	r3, lr
	bcc	.L496
	ldr	r2, .L596+8
	mov	r1, #0
	add	r3, r2, r3, lsl #2
	add	r2, r2, #56
	add	r3, r3, #24
.L497:
	cmp	r2, r3
	bne	.L498
	ldr	r3, [r4, #3360]
	cmp	r3, #0
	bne	.L499
.L501:
	ldr	r1, .L596+12
	ldr	r0, .L596+16
	bl	sftl_printk
	mvn	r0, #0
	pop	{r4, r5, r6, r7, r8, r9, r10, pc}
.L496:
	ldr	r0, [r4, #380]
	add	r3, r3, #1
	add	r0, r0, r1
	add	r1, r1, r2
	str	r0, [ip, #4]!
	b	.L495
.L498:
	str	r1, [r3, #4]!
	b	.L497
.L499:
	ldr	r3, [r4, #3364]
	cmp	r3, #0
	beq	.L501
	ldr	r3, [r4, #3380]
	cmp	r3, #0
	beq	.L501
	ldr	r3, [r4, #3384]
	cmp	r3, #0
	beq	.L501
	ldr	r3, [r4, #2540]
	cmp	r3, #0
	beq	.L501
	ldr	r3, [r4, #3388]
	cmp	r3, #0
	beq	.L501
	ldr	r3, [r4, #2516]
	cmp	r3, #0
	beq	.L501
	ldr	r3, [r4, #380]
	cmp	r3, #0
	beq	.L501
	ldr	r3, [r4, #72]
	cmp	r3, #0
	beq	.L501
	ldr	r3, [r4, #3196]
	cmp	r3, #0
	beq	.L501
	ldr	r3, [r4, #3204]
	cmp	r3, #0
	beq	.L501
	ldr	r3, [r4, #3292]
	cmp	r3, #0
	beq	.L501
	ldr	r3, [r4, #3300]
	cmp	r3, #0
	beq	.L501
	ldr	r3, [r4, #3272]
	cmp	r3, #0
	beq	.L501
	ldr	r3, [r4, #3188]
	cmp	r3, #0
	beq	.L501
	ldr	r3, [r4, #3296]
	cmp	r3, #0
	beq	.L501
	ldr	r3, [r4, #3304]
	cmp	r3, #0
	beq	.L501
	ldr	r3, [r4, #3308]
	cmp	r3, #0
	beq	.L501
	ldr	r3, [r4, #3312]
	cmp	r3, #0
	beq	.L501
	ldr	r3, [r4, #3180]
	cmp	r3, #0
	beq	.L501
	ldr	r3, [r4, #3316]
	cmp	r3, #0
	beq	.L501
	ldr	r3, [r4, #3320]
	cmp	r3, #0
	beq	.L501
	ldr	r3, [r4, #3176]
	cmp	r3, #0
	beq	.L501
	ldr	r3, [r4, #3336]
	cmp	r3, #0
	beq	.L501
	ldr	r3, [r4, #3340]
	cmp	r3, #0
	beq	.L501
	ldr	r3, [r4, #3184]
	cmp	r3, #0
	beq	.L501
	ldr	r3, [r4, #2536]
	cmp	r3, #0
	beq	.L501
	ldr	r3, [r4, #3348]
	cmp	r3, #0
	beq	.L501
	ldr	r3, [r4, #348]
	cmp	r3, #0
	beq	.L501
	ldr	r3, .L596
	ldr	r2, [r3, #3368]
	cmp	r2, #0
	beq	.L501
	ldr	r2, [r3, #3372]
	cmp	r2, #0
	beq	.L501
	ldr	r3, [r3, #3376]
	cmp	r3, #0
	beq	.L501
	mov	r0, #0
	pop	{r4, r5, r6, r7, r8, r9, r10, pc}
.L597:
	.align	2
.L596:
	.word	.LANCHOR0
	.word	33553920
	.word	.LANCHOR0+352
	.word	.LANCHOR1+173
	.word	.LC91
	.fnend
	.size	FtlMemInit, .-FtlMemInit
	.align	2
	.global	FtlBbt2Bitmap
	.syntax unified
	.arm
	.fpu softvfp
	.type	FtlBbt2Bitmap, %function
FtlBbt2Bitmap:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	ldr	r3, .L605
	push	{r4, r5, r6, r7, r8, r9, r10, lr}
	.save {r4, r5, r6, r7, r8, r9, r10, lr}
	mov	r4, r0
	ldr	r7, .L605+4
	sub	r5, r4, #2
	add	r4, r4, #1020
	mov	r6, r1
	ldrh	r2, [r3]
	add	r4, r4, #2
	ldr	r8, .L605+8
	mov	r1, #0
	ldr	r9, .L605+12
	mov	r0, r6
	lsl	r2, r2, #2
	bl	ftl_memset
.L601:
	ldrh	r3, [r5, #2]
	movw	r2, #65535
	cmp	r3, r2
	popeq	{r4, r5, r6, r7, r8, r9, r10, pc}
	ldrh	r2, [r7]
	cmp	r2, r3
	bhi	.L600
	mov	r2, #74
	mov	r1, r8
	mov	r0, r9
	bl	sftl_printk
.L600:
	ldrh	r3, [r5, #2]!
	mov	r0, #1
	cmp	r4, r5
	lsr	r1, r3, #5
	and	r3, r3, #31
	ldr	r2, [r6, r1, lsl #2]
	orr	r3, r2, r0, lsl r3
	str	r3, [r6, r1, lsl #2]
	bne	.L601
	pop	{r4, r5, r6, r7, r8, r9, r10, pc}
.L606:
	.align	2
.L605:
	.word	.LANCHOR0+3392
	.word	.LANCHOR0+298
	.word	.LANCHOR1+184
	.word	.LC8
	.fnend
	.size	FtlBbt2Bitmap, .-FtlBbt2Bitmap
	.align	2
	.global	FtlBbtMemInit
	.syntax unified
	.arm
	.fpu softvfp
	.type	FtlBbtMemInit, %function
FtlBbtMemInit:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	@ link register save eliminated.
	ldr	r0, .L608
	mvn	r2, #0
	mov	r1, #255
	add	r3, r0, #352
	add	r0, r0, #364
	strh	r2, [r3]	@ movhi
	mov	r2, #0
	strh	r2, [r3, #6]	@ movhi
	mov	r2, #16
	b	ftl_memset
.L609:
	.align	2
.L608:
	.word	.LANCHOR0
	.fnend
	.size	FtlBbtMemInit, .-FtlBbtMemInit
	.align	2
	.global	FtlFreeSysBlkQueueInit
	.syntax unified
	.arm
	.fpu softvfp
	.type	FtlFreeSysBlkQueueInit, %function
FtlFreeSysBlkQueueInit:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	ldr	ip, .L612
	mov	r2, #2048
	push	{r4, lr}
	.save {r4, lr}
	mov	r4, #0
	mov	r1, r4
	add	r3, ip, #412
	strh	r0, [r3]	@ movhi
	add	r0, ip, #420
	strh	r4, [r3, #2]	@ movhi
	strh	r4, [r3, #4]	@ movhi
	strh	r4, [r3, #6]	@ movhi
	bl	ftl_memset
	mov	r0, r4
	pop	{r4, pc}
.L613:
	.align	2
.L612:
	.word	.LANCHOR0
	.fnend
	.size	FtlFreeSysBlkQueueInit, .-FtlFreeSysBlkQueueInit
	.align	2
	.global	ftl_free_no_use_map_blk
	.syntax unified
	.arm
	.fpu softvfp
	.type	ftl_free_no_use_map_blk, %function
ftl_free_no_use_map_blk:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	push	{r3, r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.save {r3, r4, r5, r6, r7, r8, r9, r10, fp, lr}
	mov	r1, #0
	ldrh	r2, [r0, #10]
	mov	r4, r0
	ldr	r5, [r0, #20]
	ldr	r7, [r0, #12]
	ldr	r6, [r0, #24]
	lsl	r2, r2, #1
	mov	r0, r5
	bl	ftl_memset
	mov	r2, #0
.L615:
	ldrh	r1, [r4, #6]
	uxth	r3, r2
	cmp	r1, r3
	bhi	.L619
	ldr	r3, .L635
	mov	r6, #0
	mov	r8, r6
	mov	r10, r6
	ldrh	r2, [r3]
	ldrh	r3, [r4]
	lsl	r3, r3, #1
	strh	r2, [r5, r3]	@ movhi
	ldrh	r9, [r5]
.L620:
	ldrh	r3, [r4, #10]
	uxth	r1, r6
	cmp	r3, r1
	bhi	.L624
	mov	r0, r8
	pop	{r3, r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L619:
	uxth	r3, r2
	mov	r1, #0
	ldr	r0, [r6, r3, lsl #2]
	ubfx	r0, r0, #10, #16
.L616:
	ldrh	ip, [r4, #10]
	uxth	r3, r1
	cmp	ip, r3
	addls	r2, r2, #1
	bls	.L615
.L618:
	uxth	r3, r1
	add	r1, r1, #1
	lsl	r3, r3, #1
	ldrh	ip, [r7, r3]
	adds	lr, ip, #0
	movne	lr, #1
	cmp	r0, ip
	movne	lr, #0
	cmp	lr, #0
	ldrhne	ip, [r5, r3]
	addne	ip, ip, #1
	strhne	ip, [r5, r3]	@ movhi
	b	.L616
.L624:
	uxth	r3, r6
	lsl	r3, r3, #1
	ldrh	r2, [r5, r3]
	cmp	r9, r2
	bls	.L621
	ldrh	r0, [r7, r3]
	add	fp, r7, r3
	cmp	r0, #0
	bne	.L622
.L623:
	add	r6, r6, #1
	b	.L620
.L621:
	cmp	r2, #0
	bne	.L623
	ldrh	r0, [r7, r3]
	add	fp, r7, r3
	cmp	r0, #0
	beq	.L623
.L625:
	mov	r1, #1
	bl	FtlFreeSysBlkQueueIn
	strh	r10, [fp]	@ movhi
	ldrh	r3, [r4, #8]
	sub	r3, r3, #1
	strh	r3, [r4, #8]	@ movhi
	b	.L623
.L622:
	subs	r9, r2, #0
	mov	r8, r1
	beq	.L625
	b	.L623
.L636:
	.align	2
.L635:
	.word	.LANCHOR0+304
	.fnend
	.size	ftl_free_no_use_map_blk, .-ftl_free_no_use_map_blk
	.align	2
	.global	FtlL2PDataInit
	.syntax unified
	.arm
	.fpu softvfp
	.type	FtlL2PDataInit, %function
FtlL2PDataInit:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	push	{r4, r5, r6, r7, r8, lr}
	.save {r4, r5, r6, r7, r8, lr}
	mov	r1, #0
	ldr	r4, .L641
	mvn	r5, #0
	ldr	r2, [r4, #328]
	ldr	r0, [r4, #3364]
	lsl	r2, r2, #1
	bl	ftl_memset
	movw	r3, #310
	movw	r2, #338
	ldrh	r3, [r4, r3]
	mov	r1, #255
	ldrh	r2, [r4, r2]
	ldr	r0, [r4, #3388]
	mul	r2, r2, r3
	bl	ftl_memset
	ldr	r0, .L641+4
	mov	r2, #0
	mov	r3, r4
	mov	lr, #12
	mov	r4, r2
	sub	r6, r0, #28
.L638:
	ldrh	r7, [r0]
	uxth	r1, r2
	add	ip, r2, #1
	cmp	r7, r1
	bhi	.L639
	ldr	r2, .L641+8
	mvn	r1, #0
	movw	r0, #3396
	strh	r1, [r3, r0]	@ movhi
	strh	r1, [r2, #2]	@ movhi
	ldr	r1, [r3, #328]
	strh	r1, [r2, #10]	@ movhi
	ldr	r1, .L641+12
	strh	r1, [r2, #4]	@ movhi
	ldrh	r1, [r2, #44]
	strh	r1, [r2, #8]	@ movhi
	ldr	r1, .L641+16
	ldrh	r1, [r1]
	strh	r1, [r2, #6]	@ movhi
	ldr	r2, [r3, #3360]
	str	r2, [r3, #3408]
	ldr	r2, [r3, #3384]
	str	r2, [r3, #3412]
	ldr	r2, [r3, #3364]
	str	r2, [r3, #3416]
	ldr	r2, [r3, #3380]
	str	r2, [r3, #3420]
	pop	{r4, r5, r6, r7, r8, pc}
.L639:
	uxth	r2, r2
	ldr	r1, [r3, #2540]
	mul	r7, lr, r2
	add	r8, r1, r7
	str	r4, [r8, #4]
	strh	r5, [r1, r7]	@ movhi
	ldr	r1, [r3, #2540]
	add	r1, r1, r7
	ldrh	r7, [r6]
	mul	r2, r2, r7
	ldr	r7, [r3, #3388]
	bic	r2, r2, #3
	add	r2, r7, r2
	str	r2, [r1, #8]
	mov	r2, ip
	b	.L638
.L642:
	.align	2
.L641:
	.word	.LANCHOR0
	.word	.LANCHOR0+338
	.word	.LANCHOR0+3396
	.word	-3902
	.word	.LANCHOR0+336
	.fnend
	.size	FtlL2PDataInit, .-FtlL2PDataInit
	.align	2
	.global	FtlVariablesInit
	.syntax unified
	.arm
	.fpu softvfp
	.type	FtlVariablesInit, %function
FtlVariablesInit:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	push	{r4, r5, r6, lr}
	.save {r4, r5, r6, lr}
	mvn	r3, #0
	ldr	r4, .L645
	movw	r2, #3442
	mov	r5, #0
	mov	r1, r5
	strh	r3, [r4, r2]	@ movhi
	str	r3, [r4, #3452]
	add	r3, r4, #344
	strh	r5, [r3]	@ movhi
	add	r3, r4, #320
	ldrh	r2, [r3]
	ldr	r0, [r4, #348]
	str	r5, [r4, #3444]
	str	r5, [r4, #3448]
	lsl	r2, r2, #1
	bl	ftl_memset
	ldrh	r2, [r4, #242]
	mov	r1, r5
	ldr	r0, [r4, #2536]
	lsl	r2, r2, #1
	bl	ftl_memset
	ldrh	r2, [r4, #242]
	mov	r1, r5
	ldr	r0, [r4, #3348]
	lsl	r2, r2, #1
	bl	ftl_memset
	add	r0, r4, #2464
	mov	r1, r5
	mov	r2, #48
	add	r0, r0, #4
	bl	ftl_memset
	add	r0, r4, #2640
	mov	r2, #512
	mov	r1, r5
	add	r0, r0, #4
	bl	ftl_memset
	bl	FtlGcBufInit
	bl	FtlL2PDataInit
	mov	r0, r5
	pop	{r4, r5, r6, pc}
.L646:
	.align	2
.L645:
	.word	.LANCHOR0
	.fnend
	.size	FtlVariablesInit, .-FtlVariablesInit
	.align	2
	.global	SupperBlkListInit
	.syntax unified
	.arm
	.fpu softvfp
	.type	SupperBlkListInit, %function
SupperBlkListInit:
	.fnstart
	@ args = 0, pretend = 0, frame = 16
	@ frame_needed = 0, uses_anonymous_args = 0
	push	{r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.save {r4, r5, r6, r7, r8, r9, r10, fp, lr}
	mov	r2, #6
	ldr	r4, .L659
	mov	r5, #0
	.pad #20
	sub	sp, sp, #20
	mov	r1, #0
	ldr	r10, .L659+4
	mov	r9, r5
	ldrh	r3, [r4, #242]
	mov	r6, r5
	ldr	r0, [r4, #2516]
	add	fp, r4, #260
	mul	r2, r2, r3
	bl	ftl_memset
	add	r3, r4, #2528
	str	r5, [r4, #2532]
	str	r5, [r4, #2520]
	str	r5, [r4, #2524]
	strh	r5, [r3]	@ movhi
	strh	r5, [r4, #224]	@ movhi
	str	r3, [sp, #4]
.L648:
	ldrh	r3, [r4, #240]
	uxth	r8, r5
	cmp	r8, r3
	bcs	.L655
	ldrh	r3, [r10]
	ldrh	r2, [r4, #232]
	str	r3, [sp]
	mov	r3, #0
	mov	r7, r3
	b	.L656
.L650:
	mov	r1, r8
	ldrb	r0, [fp, r3]	@ zero_extendqisi2
	str	r2, [sp, #12]
	str	r3, [sp, #8]
	bl	V2P_block
	bl	FtlBbmIsBadBlock
	cmp	r0, #0
	ldr	r3, [sp, #8]
	ldreq	r1, [sp]
	ldr	r2, [sp, #12]
	add	r3, r3, #1
	addeq	r7, r7, r1
	uxtheq	r7, r7
.L656:
	uxth	r1, r3
	cmp	r2, r1
	bhi	.L650
	cmp	r7, #0
	uxth	r3, r5
	beq	.L651
	mov	r1, r7
	mov	r0, #32768
	str	r3, [sp]
	bl	__aeabi_idiv
	ldr	r3, [sp]
	uxth	r7, r0
.L652:
	ldr	r1, [r4, #2516]
	mov	r2, #6
	mla	r2, r2, r3, r1
	strh	r7, [r2, #4]	@ movhi
	ldrh	r2, [r4, #24]
	cmp	r2, r8
	beq	.L653
	ldrh	r2, [r4, #76]
	cmp	r2, r8
	beq	.L653
	ldrh	r2, [r4, #124]
	cmp	r2, r8
	beq	.L653
	ldr	r2, [r4, #72]
	lsl	r3, r3, #1
	ldrh	r3, [r2, r3]
	cmp	r3, #0
	bne	.L654
	add	r9, r9, #1
	mov	r0, r8
	uxth	r9, r9
	bl	INSERT_FREE_LIST
.L653:
	add	r5, r5, #1
	b	.L648
.L651:
	ldr	r1, [r4, #72]
	lsl	r2, r3, #1
	mvn	r0, #0
	strh	r0, [r1, r2]	@ movhi
	b	.L652
.L654:
	add	r6, r6, #1
	mov	r0, r8
	uxth	r6, r6
	bl	INSERT_DATA_LIST
	b	.L653
.L655:
	ldr	r2, [sp, #4]
	strh	r9, [r4, #224]	@ movhi
	strh	r6, [r2]	@ movhi
	add	r6, r6, r9
	cmp	r6, r3
	ble	.L657
	movw	r2, #2212
	ldr	r1, .L659+8
	ldr	r0, .L659+12
	bl	sftl_printk
.L657:
	mov	r0, #0
	add	sp, sp, #20
	@ sp needed
	pop	{r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L660:
	.align	2
.L659:
	.word	.LANCHOR0
	.word	.LANCHOR0+302
	.word	.LANCHOR1+198
	.word	.LC8
	.fnend
	.size	SupperBlkListInit, .-SupperBlkListInit
	.align	2
	.global	FtlGcPageVarInit
	.syntax unified
	.arm
	.fpu softvfp
	.type	FtlGcPageVarInit, %function
FtlGcPageVarInit:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	push	{r4, r5, r6, lr}
	.save {r4, r5, r6, lr}
	mov	r3, #0
	ldr	r4, .L663
	movw	r5, #306
	mov	r1, #255
	add	r2, r4, #3200
	ldr	r0, [r4, #3196]
	strh	r3, [r2]	@ movhi
	movw	r2, #3208
	strh	r3, [r4, r2]	@ movhi
	ldrh	r2, [r4, r5]
	lsl	r2, r2, #1
	bl	ftl_memset
	ldrh	r3, [r4, r5]
	mov	r2, #12
	ldr	r0, [r4, #3204]
	mov	r1, #255
	mul	r2, r2, r3
	bl	ftl_memset
	pop	{r4, r5, r6, lr}
	b	FtlGcBufInit
.L664:
	.align	2
.L663:
	.word	.LANCHOR0
	.fnend
	.size	FtlGcPageVarInit, .-FtlGcPageVarInit
	.align	2
	.global	FlashGetBadBlockList
	.syntax unified
	.arm
	.fpu softvfp
	.type	FlashGetBadBlockList, %function
FlashGetBadBlockList:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	push	{r4, r5, r6, lr}
	.save {r4, r5, r6, lr}
	mov	r2, #256
	ldr	r5, .L673
	mov	r4, r0
	mov	r6, r1
	mov	r1, #255
	bl	ftl_memset
	ldr	r3, [r5, #3248]
	mov	r1, r6
	mov	r0, r4
	blx	r3
	uxth	r0, r0
	cmp	r0, #50
	bls	.L666
	mov	r2, #256
	mov	r1, #255
	mov	r0, r4
	bl	ftl_memset
	mov	r0, #0
.L666:
	ldrh	r3, [r5, #14]
	cmp	r3, #4
	moveq	r3, r4
	addeq	r1, r3, r0, lsl #1
	beq	.L668
	pop	{r4, r5, r6, pc}
.L669:
	ldrh	r2, [r3]
	lsr	r2, r2, #1
	strh	r2, [r3], #2	@ movhi
.L668:
	cmp	r3, r1
	bne	.L669
	pop	{r4, r5, r6, pc}
.L674:
	.align	2
.L673:
	.word	.LANCHOR0
	.fnend
	.size	FlashGetBadBlockList, .-FlashGetBadBlockList
	.align	2
	.global	ftl_memcpy
	.syntax unified
	.arm
	.fpu softvfp
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
	.global	FlashReadPages
	.syntax unified
	.arm
	.fpu softvfp
	.type	FlashReadPages, %function
FlashReadPages:
	.fnstart
	@ args = 0, pretend = 0, frame = 24
	@ frame_needed = 0, uses_anonymous_args = 0
	ldr	r3, .L717
	push	{r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.save {r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.pad #28
	sub	sp, sp, #28
	ldr	r5, .L717+4
	mov	r8, r1
	mov	r4, r0
	mov	r7, #0
	ldr	r2, [r3]
	mov	fp, r3
	ldr	r9, .L717+8
	ldr	r10, .L717+12
	str	r2, [sp, #20]
	ldrh	r2, [r5, #12]
	str	r2, [sp, #4]
.L677:
	cmp	r7, r8
	bne	.L690
	ldr	r2, [sp, #20]
	mov	r0, #0
	ldr	r3, [fp]
	cmp	r2, r3
	beq	.L691
	bl	__stack_chk_fail
.L690:
	ldr	r3, [r4, #8]
	cmp	r3, #0
	beq	.L678
	ldr	r3, [r4, #12]
	cmp	r3, #0
	bne	.L679
.L678:
	mov	r2, #96
	ldr	r1, .L717+16
	mov	r0, r9
	bl	sftl_printk
.L679:
	add	r2, sp, #12
	add	r1, sp, #16
	mov	r0, r4
	bl	l2p_addr_tran
	ldr	r0, [sp, #12]
	cmp	r0, #3
	mvnhi	r3, #0
	strhi	r3, [r4]
	bhi	.L681
	ldr	r6, [r4, #8]
	uxtb	r0, r0
	ldr	r3, [r4, #12]
	ldr	r1, [sp, #16]
	tst	r6, #63
	ldr	ip, [r5, #3260]
	ldrne	r6, [r5, #3324]
	mov	r2, r6
	blx	ip
	str	r0, [r4]
	ldrh	r3, [r5, #14]
	cmp	r3, #4
	bne	.L684
	ldr	r0, [sp, #4]
	add	r2, r6, #2048
	ldr	r3, [r4, #12]
	ldr	r1, [sp, #16]
	ldr	ip, [r5, #3260]
	add	r3, r3, #8
	add	r1, r0, r1
	ldrb	r0, [sp, #12]	@ zero_extendqisi2
	blx	ip
	cmn	r0, #1
	mov	r1, r0
	beq	.L685
	ldr	r3, [r4, #12]
	ldr	r2, [r3, #12]
	cmn	r2, #1
	bne	.L686
	ldr	r2, [r3, #8]
	cmn	r2, #1
	bne	.L686
	ldr	r3, [r3]
	cmn	r3, #1
	beq	.L686
.L685:
	mvn	r3, #0
	str	r3, [r4]
.L686:
	ldr	r3, [r4]
	sub	r0, r1, #256
	clz	r0, r0
	lsr	r0, r0, #5
	cmn	r3, #1
	moveq	r0, #0
	cmp	r0, #0
	movne	r3, #256
	strne	r3, [r4]
	ldr	r3, [r4]
	cmn	r3, #1
	cmpne	r3, #256
	bne	.L684
	ldr	r1, [r4, #4]
	ldr	r2, [sp, #16]
	ldr	r0, .L717+20
	bl	sftl_printk
	ldr	r1, [r4, #8]
	cmp	r1, #0
	beq	.L689
	mov	r3, #4
	ldr	r0, .L717+24
	mov	r2, r3
	bl	rknand_print_hex
.L689:
	ldr	r1, [r4, #12]
	cmp	r1, #0
	beq	.L684
	mov	r3, #4
	ldr	r0, .L717+28
	mov	r2, r3
	bl	rknand_print_hex
.L684:
	ldr	r3, [r5, #3324]
	cmp	r6, r3
	bne	.L681
	ldr	r0, [r4, #8]
	cmp	r6, r0
	beq	.L681
	ldrh	r2, [r10]
	mov	r1, r6
	lsl	r2, r2, #9
	bl	ftl_memcpy
.L681:
	add	r7, r7, #1
	add	r4, r4, #20
	b	.L677
.L691:
	add	sp, sp, #28
	@ sp needed
	pop	{r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L718:
	.align	2
.L717:
	.word	__stack_chk_guard
	.word	.LANCHOR0
	.word	.LC8
	.word	.LANCHOR0+258
	.word	.LANCHOR1+216
	.word	.LC92
	.word	.LC93
	.word	.LC94
	.fnend
	.size	FlashReadPages, .-FlashReadPages
	.align	2
	.global	FtlLoadFactoryBbt
	.syntax unified
	.arm
	.fpu softvfp
	.type	FtlLoadFactoryBbt, %function
FtlLoadFactoryBbt:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	push	{r3, r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.save {r3, r4, r5, r6, r7, r8, r9, r10, fp, lr}
	mov	r7, #0
	ldr	r4, .L730
	add	r6, r4, #360
	ldr	r3, [r4, #3304]
	ldr	r8, [r4, #3336]
	add	r6, r6, #2
	sub	r9, r6, #64
	add	r10, r4, #3456
	str	r3, [r4, #3464]
	str	r8, [r4, #3468]
.L720:
	ldrh	r3, [r4, #254]
	cmp	r7, r3
	bcc	.L725
	mov	r0, #0
	pop	{r3, r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L725:
	ldrh	r5, [r9]
	mvn	r3, #0
	movw	fp, #61664
	strh	r3, [r6, #2]!	@ movhi
	add	r5, r5, r3
	uxth	r5, r5
.L721:
	ldrh	r3, [r9]
	sub	r2, r3, #16
	cmp	r5, r2
	ble	.L723
	mla	r3, r7, r3, r5
	mov	r2, #1
	mov	r1, r2
	mov	r0, r10
	lsl	r3, r3, #10
	str	r3, [r4, #3460]
	bl	FlashReadPages
	ldr	r3, [r4, #3456]
	cmn	r3, #1
	beq	.L722
	ldrh	r3, [r8]
	cmp	r3, fp
	bne	.L722
	strh	r5, [r6]	@ movhi
.L723:
	add	r7, r7, #1
	b	.L720
.L722:
	sub	r5, r5, #1
	uxth	r5, r5
	b	.L721
.L731:
	.align	2
.L730:
	.word	.LANCHOR0
	.fnend
	.size	FtlLoadFactoryBbt, .-FtlLoadFactoryBbt
	.align	2
	.global	FtlGetLastWrittenPage
	.syntax unified
	.arm
	.fpu softvfp
	.type	FtlGetLastWrittenPage, %function
FtlGetLastWrittenPage:
	.fnstart
	@ args = 0, pretend = 0, frame = 88
	@ frame_needed = 0, uses_anonymous_args = 0
	push	{r4, r5, r6, r7, r8, r9, lr}
	.save {r4, r5, r6, r7, r8, r9, lr}
	.pad #92
	sub	sp, sp, #92
	ldr	r6, .L745
	cmp	r1, #1
	movwne	r2, #302
	lsl	r8, r0, #10
	mov	r7, r1
	ldr	r3, [r6]
	mov	r0, sp
	str	r3, [sp, #84]
	ldr	r3, .L745+4
	addeq	r2, r3, #304
	ldrhne	r5, [r3, r2]
	ldrheq	r5, [r2]
	mov	r2, r1
	ldr	r3, [r3, #3328]
	mov	r1, #1
	sub	r5, r5, #1
	sxth	r5, r5
	str	r3, [sp, #8]
	add	r3, sp, #20
	str	r3, [sp, #12]
	orr	r3, r5, r8
	str	r3, [sp, #4]
	bl	FlashReadPages
	ldr	r3, [sp, #20]
	cmn	r3, #1
	moveq	r9, #0
	beq	.L736
.L735:
	ldr	r2, [sp, #84]
	mov	r0, r5
	ldr	r3, [r6]
	cmp	r2, r3
	beq	.L740
	bl	__stack_chk_fail
.L739:
	add	r3, r9, r5
	mov	r2, r7
	add	r3, r3, r3, lsr #31
	mov	r1, #1
	mov	r0, sp
	asr	r4, r3, #1
	sxth	r3, r4
	orr	r3, r3, r8
	str	r3, [sp, #4]
	bl	FlashReadPages
	ldr	r3, [sp, #20]
	cmn	r3, #1
	bne	.L737
	ldr	r3, [sp, #24]
	cmn	r3, #1
	bne	.L737
	ldr	r3, [sp]
	cmn	r3, #1
	subne	r4, r4, #1
	sxthne	r5, r4
	bne	.L736
.L737:
	add	r4, r4, #1
	sxth	r9, r4
.L736:
	cmp	r9, r5
	ble	.L739
	b	.L735
.L740:
	add	sp, sp, #92
	@ sp needed
	pop	{r4, r5, r6, r7, r8, r9, pc}
.L746:
	.align	2
.L745:
	.word	__stack_chk_guard
	.word	.LANCHOR0
	.fnend
	.size	FtlGetLastWrittenPage, .-FtlGetLastWrittenPage
	.align	2
	.global	FtlScanSysBlk
	.syntax unified
	.arm
	.fpu softvfp
	.type	FtlScanSysBlk, %function
FtlScanSysBlk:
	.fnstart
	@ args = 0, pretend = 0, frame = 32
	@ frame_needed = 0, uses_anonymous_args = 0
	push	{r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.save {r4, r5, r6, r7, r8, r9, r10, fp, lr}
	mov	r5, #0
	ldr	r4, .L829
	mov	r1, r5
	.pad #36
	sub	sp, sp, #36
	ldr	r2, [r4, #328]
	add	r7, r4, #3440
	add	r6, r4, #344
	ldr	r0, [r4, #3384]
	strh	r5, [r7]	@ movhi
	add	r8, r4, #320
	strh	r5, [r6]	@ movhi
	lsl	r2, r2, #2
	bl	ftl_memset
	ldr	r2, [r4, #328]
	mov	r1, r5
	ldr	r0, [r4, #3360]
	lsl	r2, r2, #1
	bl	ftl_memset
	ldrh	r2, [r8]
	mov	r1, r5
	ldr	r0, [r4, #3372]
	lsl	r2, r2, #2
	bl	ftl_memset
	ldrh	r2, [r8]
	mov	r1, r5
	ldr	r0, [r4, #348]
	sub	r5, r7, #812
	lsl	r2, r2, #1
	bl	ftl_memset
	mov	r2, #16
	mov	r1, #255
	sub	r0, r7, #812
	bl	ftl_memset
	ldrh	r3, [r4, #240]
	str	r6, [sp, #16]
	stmib	sp, {r3, r7}
.L748:
	ldrh	r3, [r4, #242]
	ldr	r2, [sp, #4]
	cmp	r3, r2
	bls	.L789
	ldr	r7, .L829+4
	mov	r6, #0
	ldrh	r2, [r4, #232]
	mov	r10, r6
	ldr	r8, [r4, #3292]
	mov	r9, #20
	ldrh	r1, [r7], #-52
	ldr	r3, [r4, #3180]
	ldr	fp, [r4, #3184]
	str	r1, [sp, #12]
	b	.L790
.L750:
	ldr	r1, [sp, #4]
	ldrb	r0, [r7, r6]	@ zero_extendqisi2
	str	r3, [sp, #28]
	str	r2, [sp, #24]
	bl	V2P_block
	str	r0, [sp, #20]
	bl	FtlBbmIsBadBlock
	cmp	r0, #0
	ldr	r2, [sp, #24]
	ldr	r3, [sp, #28]
	bne	.L749
	ldr	r1, [sp, #20]
	mla	r0, r9, r10, r8
	lsl	r1, r1, #10
	stmib	r0, {r1, r3}
	ldr	r1, [sp, #12]
	mul	r1, r1, r10
	add	ip, r1, #3
	cmp	r1, #0
	movlt	r1, ip
	bic	r1, r1, #3
	add	r1, fp, r1
	str	r1, [r0, #12]
	add	r1, r10, #1
	uxth	r10, r1
.L749:
	add	r6, r6, #1
.L790:
	uxth	r1, r6
	cmp	r2, r1
	bhi	.L750
	cmp	r10, #0
	bne	.L751
.L788:
	ldr	r3, [sp, #4]
	add	r3, r3, #1
	uxth	r3, r3
	str	r3, [sp, #4]
	b	.L748
.L751:
	mov	fp, #0
	mov	r2, #1
	mov	r1, r10
	mov	r0, r8
	bl	FlashReadPages
.L752:
	uxth	r3, fp
	cmp	r10, r3
	bls	.L788
	mov	r8, #20
	ldr	r3, [r4, #3292]
	mul	r8, r8, fp
	add	r2, r3, r8
	ldr	r3, [r3, r8]
	ldr	r6, [r2, #4]
	ldr	r7, [r2, #12]
	cmn	r3, #1
	ubfx	r6, r6, #10, #16
	bne	.L755
	mov	r9, #16
.L757:
	ldr	r0, [r4, #3292]
	mov	r2, #1
	mov	r1, r2
	add	r0, r0, r8
	ldr	r3, [r0, #4]
	add	r3, r3, #1
	str	r3, [r0, #4]
	bl	FlashReadPages
	ldrh	r3, [r7]
	movw	r2, #65535
	cmp	r3, r2
	ldr	r3, [r4, #3292]
	bne	.L754
	mvn	r2, #0
	str	r2, [r3, r8]
	ldr	r3, [r4, #3292]
	ldr	r3, [r3, r8]
	cmp	r3, r2
	bne	.L755
.L756:
	mov	r1, #1
	b	.L828
.L754:
	ldr	r3, [r3, r8]
	cmn	r3, #1
	bne	.L755
	sub	r9, r9, #1
	uxth	r9, r9
	cmp	r9, #0
	bne	.L757
	b	.L756
.L755:
	ldr	r2, [r4, #2592]
	ldr	r3, [r7, #4]
	cmn	r2, #1
	beq	.L758
	cmp	r2, r3
	bhi	.L759
.L758:
	cmn	r3, #1
	addne	r2, r3, #1
	strne	r2, [r4, #2592]
.L759:
	ldrh	r2, [r7]
	movw	r1, #61604
	cmp	r2, r1
	beq	.L761
	bhi	.L762
	movw	r3, #61574
	cmp	r2, r3
	beq	.L763
.L760:
	add	fp, fp, #1
	b	.L752
.L762:
	movw	r3, #61634
	cmp	r2, r3
	beq	.L764
	movw	r3, #65535
	cmp	r2, r3
	moveq	r1, #0
	bne	.L760
.L828:
	mov	r0, r6
	bl	FtlFreeSysBlkQueueIn
	b	.L760
.L764:
	ldr	r3, [sp, #8]
	ldrh	r2, [r3]
	ldr	r3, [r4, #328]
	cmp	r2, r3
	bls	.L766
	movw	r2, #1225
	ldr	r1, .L829+8
	ldr	r0, .L829+12
	bl	sftl_printk
.L766:
	ldr	r0, [r4, #328]
	ldr	r2, [sp, #8]
	ldr	ip, [r4, #3384]
	ldrh	r2, [r2]
	uxth	r1, r0
	sub	r3, r1, #1
	sub	r1, r1, r2
	sxth	r3, r3
	sub	r1, r1, #1
	sxth	r1, r1
.L767:
	cmp	r3, r1
	bgt	.L773
	cmp	r3, #0
	bge	.L805
	b	.L760
.L773:
	ldr	r9, [r7, #4]
	lsl	lr, r3, #2
	ldr	r8, [ip, r3, lsl #2]
	cmp	r9, r8
	bls	.L768
	ldr	r1, [ip]
	cmp	r1, #0
	bne	.L769
	cmp	r0, r2
	ldrne	r1, .L829+16
	addne	r2, r2, #1
	strhne	r2, [r1]	@ movhi
.L769:
	uxth	ip, r3
	mov	r1, #0
.L770:
	uxth	r0, r1
	sxth	r2, r1
	cmp	ip, r0
	bhi	.L771
	ldr	r1, [r7, #4]
	cmp	r3, #0
	ldr	r2, [r4, #3384]
	str	r1, [r2, lr]
	lsl	r2, r3, #1
	ldr	r1, [r4, #3360]
	strh	r6, [r1, r2]	@ movhi
	blt	.L760
	ldr	r2, .L829+16
	ldr	r1, [r4, #328]
	ldrh	r2, [r2]
	sub	r1, r1, r2
	sub	r1, r1, #1
	sxth	r1, r1
	cmp	r3, r1
	bgt	.L760
.L805:
	ldr	r1, [sp, #8]
	add	r2, r2, #1
	strh	r2, [r1]	@ movhi
	ldr	r2, [r4, #3384]
	ldr	r1, [r7, #4]
	str	r1, [r2, r3, lsl #2]
	lsl	r3, r3, #1
	ldr	r2, [r4, #3360]
.L826:
	strh	r6, [r2, r3]	@ movhi
	b	.L760
.L771:
	ldr	r0, [r4, #3384]
	add	r1, r1, #1
	add	r8, r0, r2, lsl #2
	ldr	r8, [r8, #4]
	str	r8, [r0, r2, lsl #2]
	lsl	r2, r2, #1
	ldr	r0, [r4, #3360]
	add	r8, r0, r2
	ldrh	r8, [r8, #2]
	strh	r8, [r0, r2]	@ movhi
	b	.L770
.L768:
	sub	r3, r3, #1
	sxth	r3, r3
	b	.L767
.L763:
	ldr	r3, [sp, #16]
	ldr	r8, .L829+20
	ldrh	r2, [r3]
	ldrh	r3, [r8]
	cmp	r2, r3
	bls	.L776
	movw	r2, #1266
	ldr	r1, .L829+8
	ldr	r0, .L829+12
	bl	sftl_printk
.L776:
	ldr	lr, .L829+24
	ldrh	r2, [r8]
	ldr	ip, [r4, #3372]
	ldrh	r1, [lr]
	sub	r0, r2, #1
	sxth	r3, r0
	sub	r0, r0, r1
	str	r0, [sp, #12]
.L777:
	ldr	r0, [sp, #12]
	cmp	r3, r0
	ble	.L782
	ldr	r9, [ip, r3, lsl #2]
	lsl	r8, r3, #2
	ldr	r0, [r7, #4]
	cmp	r0, r9
	bls	.L778
	sub	r2, r2, r1
	ldr	r0, [ip]
	clz	r2, r2
	uxth	ip, r3
	lsr	r2, r2, #5
	cmp	r0, #0
	orrne	r2, r2, #1
	cmp	r2, #0
	addeq	r1, r1, #1
	strheq	r1, [lr]	@ movhi
	mov	r1, #0
.L780:
	uxth	r0, r1
	sxth	r2, r1
	cmp	ip, r0
	bhi	.L781
	ldr	r1, [r7, #4]
	ldr	r2, [r4, #3372]
	str	r1, [r2, r8]
	lsl	r2, r3, #1
	ldr	r1, [r4, #348]
	strh	r6, [r1, r2]	@ movhi
.L782:
	cmp	r3, #0
	blt	.L760
	ldr	r0, .L829+24
	ldrh	r2, [r0, #-24]
	ldrh	r1, [r0]
	sub	r2, r2, #1
	sub	r2, r2, r1
	sxth	r2, r2
	cmp	r3, r2
	bgt	.L760
	add	r1, r1, #1
	ldr	r2, [r4, #3372]
	strh	r1, [r0]	@ movhi
	ldr	r1, [r7, #4]
	str	r1, [r2, r3, lsl #2]
	lsl	r3, r3, #1
	ldr	r2, [r4, #348]
	b	.L826
.L781:
	ldr	r0, [r4, #3372]
	add	r1, r1, #1
	add	lr, r0, r2, lsl #2
	ldr	lr, [lr, #4]
	str	lr, [r0, r2, lsl #2]
	lsl	r2, r2, #1
	ldr	r0, [r4, #348]
	add	lr, r0, r2
	ldrh	lr, [lr, #2]
	strh	lr, [r0, r2]	@ movhi
	b	.L780
.L778:
	sub	r3, r3, #1
	sxth	r3, r3
	b	.L777
.L761:
	ldrh	r1, [r5]
	movw	r2, #65535
	cmp	r1, r2
	strheq	r6, [r5]	@ movhi
	beq	.L827
	ldrh	r0, [r5, #4]
	cmp	r0, r2
	beq	.L785
	mov	r1, #1
	bl	FtlFreeSysBlkQueueIn
.L785:
	ldr	r3, [r7, #4]
	ldr	r2, [r4, #2636]
	cmp	r2, r3
	strhcs	r6, [r5, #4]	@ movhi
	bcs	.L760
	ldrh	r3, [r5]
	strh	r6, [r5]	@ movhi
	strh	r3, [r5, #4]	@ movhi
	ldr	r3, [r7, #4]
.L827:
	str	r3, [r4, #2636]
	b	.L760
.L789:
	ldr	r1, [r4, #3360]
	ldrh	r3, [r1]
	cmp	r3, #0
	beq	.L791
.L794:
	ldr	r0, [r4, #348]
	ldrh	r2, [r0]
	cmp	r2, #0
	beq	.L792
.L793:
	ldr	r3, [sp, #8]
	ldrh	r2, [r3]
	ldr	r3, [r4, #328]
	cmp	r2, r3
	bls	.L824
	movw	r2, #1391
	ldr	r1, .L829+8
	ldr	r0, .L829+12
	bl	sftl_printk
.L824:
	mov	r0, #0
	add	sp, sp, #36
	@ sp needed
	pop	{r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L791:
	ldr	r2, .L829+16
	ldrh	r2, [r2]
	cmp	r2, #0
	ldrne	r0, [r4, #328]
	beq	.L794
.L795:
	sxth	r2, r3
	cmp	r2, r0
	bcs	.L794
	lsl	ip, r2, #1
	add	r3, r3, #1
	ldrh	ip, [r1, ip]
	cmp	ip, #0
	beq	.L795
	mov	r3, r2
	mov	lr, #0
.L796:
	ldr	r1, [r4, #328]
	cmp	r3, r1
	bcs	.L794
	ldr	r0, [r4, #3360]
	lsl	r1, r3, #1
	sub	ip, r3, r2
	lsl	r5, ip, #1
	ldrh	r6, [r0, r1]
	strh	r6, [r0, r5]	@ movhi
	ldr	r0, [r4, #3384]
	ldr	r5, [r0, r3, lsl #2]
	add	r3, r3, #1
	sxth	r3, r3
	str	r5, [r0, ip, lsl #2]
	ldr	r0, [r4, #3360]
	strh	lr, [r0, r1]	@ movhi
	b	.L796
.L792:
	ldr	r3, .L829+24
	ldrh	r1, [r3]
	cmp	r1, #0
	ldrhne	ip, [r3, #-24]!
	movne	r1, r3
	beq	.L793
.L801:
	sxth	r3, r2
	cmp	r3, ip
	mov	lr, r3
	bge	.L793
	lsl	r5, r3, #1
	add	r2, r2, #1
	ldrh	r5, [r0, r5]
	cmp	r5, #0
	beq	.L801
	mov	r5, #0
.L802:
	ldrh	r2, [r1]
	cmp	r3, r2
	bge	.L793
	ldr	r0, [r4, #348]
	lsl	r2, r3, #1
	sub	ip, r3, lr
	lsl	r6, ip, #1
	ldrh	r7, [r0, r2]
	strh	r7, [r0, r6]	@ movhi
	ldr	r0, [r4, #3372]
	ldr	r6, [r0, r3, lsl #2]
	add	r3, r3, #1
	sxth	r3, r3
	str	r6, [r0, ip, lsl #2]
	ldr	r0, [r4, #348]
	strh	r5, [r0, r2]	@ movhi
	b	.L802
.L830:
	.align	2
.L829:
	.word	.LANCHOR0
	.word	.LANCHOR0+312
	.word	.LANCHOR1+231
	.word	.LC8
	.word	.LANCHOR0+3440
	.word	.LANCHOR0+320
	.word	.LANCHOR0+344
	.fnend
	.size	FtlScanSysBlk, .-FtlScanSysBlk
	.align	2
	.global	FtlLoadBbt
	.syntax unified
	.arm
	.fpu softvfp
	.type	FtlLoadBbt, %function
FtlLoadBbt:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	push	{r4, r5, r6, r7, r8, r9, r10, lr}
	.save {r4, r5, r6, r7, r8, r9, r10, lr}
	ldr	r4, .L864
	ldr	r8, .L864+4
	ldr	r3, [r4, #3304]
	add	r7, r4, #3456
	ldr	r6, [r4, #3336]
	str	r3, [r4, #3464]
	str	r6, [r4, #3468]
	bl	FtlBbtMemInit
	movw	r3, #298
	ldrh	r5, [r4, r3]
	sub	r5, r5, #1
	uxth	r5, r5
.L832:
	ldrh	r3, [r8]
	sub	r3, r3, #16
	cmp	r5, r3
	ble	.L835
	lsl	r3, r5, #10
	mov	r2, #1
	mov	r1, r2
	mov	r0, r7
	str	r3, [r4, #3460]
	bl	FlashReadPages
	ldr	r3, [r4, #3456]
	cmn	r3, #1
	bne	.L833
	ldr	r3, [r4, #3460]
	mov	r2, #1
	mov	r1, r2
	mov	r0, r7
	add	r3, r3, #1
	str	r3, [r4, #3460]
	bl	FlashReadPages
.L833:
	ldr	r3, [r4, #3456]
	cmn	r3, #1
	beq	.L834
	ldrh	r2, [r6]
	movw	r3, #61649
	cmp	r2, r3
	bne	.L834
	ldr	r2, [r6, #4]
	ldr	r3, .L864+8
	str	r2, [r4, #360]
	ldrh	r2, [r6, #8]
	strh	r5, [r3]	@ movhi
	strh	r2, [r3, #4]	@ movhi
.L835:
	ldr	r5, .L864+8
	movw	r2, #65535
	ldrh	r3, [r5]
	cmp	r3, r2
	beq	.L849
	ldrh	r3, [r5, #4]
	cmp	r3, r2
	beq	.L839
	lsl	r3, r3, #10
	mov	r2, #1
	mov	r1, r2
	add	r0, r5, #3104
	str	r3, [r4, #3460]
	bl	FlashReadPages
	ldr	r3, [r4, #3456]
	cmn	r3, #1
	beq	.L839
	ldrh	r2, [r6]
	movw	r3, #61649
	cmp	r2, r3
	bne	.L839
	ldr	r3, [r6, #4]
	ldr	r2, [r4, #360]
	cmp	r3, r2
	ldrhhi	r2, [r5, #4]
	strhi	r3, [r4, #360]
	ldrhhi	r3, [r6, #8]
	strhhi	r2, [r5]	@ movhi
	strhhi	r3, [r5, #4]	@ movhi
.L839:
	ldr	r8, .L864+12
	mov	r1, #1
	ldrh	r0, [r5]
	movw	r9, #61649
	bl	FtlGetLastWrittenPage
	sxth	r7, r0
	add	r0, r0, #1
	strh	r0, [r5, #2]	@ movhi
.L841:
	cmp	r7, #0
	bge	.L844
	mov	r2, #251
	ldr	r1, .L864+16
	ldr	r0, .L864+20
	bl	sftl_printk
.L843:
	ldrh	r3, [r6, #10]
	ldrh	r0, [r6, #12]
	strh	r3, [r5, #6]	@ movhi
	movw	r3, #65535
	cmp	r0, r3
	beq	.L846
	ldr	r2, [r4, #228]
	cmp	r0, r2
	beq	.L846
	ldrh	r3, [r4, #242]
	lsr	r3, r3, #2
	cmp	r0, r3
	cmpcc	r2, r3
	bcs	.L846
	bl	FtlSysBlkNumInit
.L846:
	ldr	r6, .L864+24
	mov	r5, #0
	ldr	r7, .L864+28
.L847:
	ldrh	r3, [r4, #254]
	cmp	r5, r3
	bcc	.L848
	mov	r0, #0
	pop	{r4, r5, r6, r7, r8, r9, r10, pc}
.L834:
	sub	r5, r5, #1
	uxth	r5, r5
	b	.L832
.L844:
	ldrh	r3, [r5]
	mov	r2, #1
	mov	r1, r2
	mov	r0, r8
	orr	r3, r7, r3, lsl #10
	str	r3, [r4, #3460]
	ldr	r3, [r4, #3304]
	str	r3, [r4, #3464]
	bl	FlashReadPages
	ldr	r3, [r4, #3456]
	cmn	r3, #1
	beq	.L842
	ldrh	r3, [r6]
	cmp	r3, r9
	beq	.L843
.L842:
	sub	r7, r7, #1
	sxth	r7, r7
	b	.L841
.L848:
	ldrh	r2, [r7]
	ldr	r1, [r4, #3464]
	ldr	r0, [r6, #4]!
	lsl	r2, r2, #2
	mla	r1, r5, r2, r1
	add	r5, r5, #1
	bl	ftl_memcpy
	b	.L847
.L849:
	mvn	r0, #0
	pop	{r4, r5, r6, r7, r8, r9, r10, pc}
.L865:
	.align	2
.L864:
	.word	.LANCHOR0
	.word	.LANCHOR0+298
	.word	.LANCHOR0+352
	.word	.LANCHOR0+3456
	.word	.LANCHOR1+245
	.word	.LC8
	.word	.LANCHOR0+376
	.word	.LANCHOR0+3392
	.fnend
	.size	FtlLoadBbt, .-FtlLoadBbt
	.align	2
	.global	FlashProgPages
	.syntax unified
	.arm
	.fpu softvfp
	.type	FlashProgPages, %function
FlashProgPages:
	.fnstart
	@ args = 0, pretend = 0, frame = 48
	@ frame_needed = 0, uses_anonymous_args = 0
	push	{r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.save {r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.pad #52
	sub	sp, sp, #52
	ldr	r6, .L905
	mov	r10, r1
	mov	r4, r0
	mov	r5, r0
	ldr	r3, .L905+4
	mov	r8, #0
	ldrh	fp, [r6, #12]
	mov	r9, r6
	str	r2, [sp, #8]
	ldr	r2, [r3]
	str	r3, [sp, #12]
	str	r2, [sp, #44]
	lsl	r2, fp, #3
	str	r2, [sp, #4]
.L867:
	cmp	r8, r10
	bne	.L881
	ldr	r7, .L905+8
	mov	r5, #0
	ldr	r9, .L905+12
	ldr	r10, .L905+16
.L882:
	cmp	r8, r5
	beq	.L875
	ldr	r2, [r6, #3324]
	mov	r3, #0
	mov	r1, #1
	add	r0, sp, #24
	str	r3, [r2]
	ldr	r2, [r6, #3332]
	str	r3, [r2]
	ldr	r3, [r4, #4]
	ldr	r2, [sp, #8]
	str	r3, [sp, #28]
	ldr	r3, [r6, #3324]
	str	r3, [sp, #32]
	ldr	r3, [r6, #3332]
	str	r3, [sp, #36]
	bl	FlashReadPages
	ldr	fp, [sp, #24]
	cmn	fp, #1
	bne	.L883
	ldr	r1, [r4, #4]
	mov	r0, r7
	bl	sftl_printk
	str	fp, [r4]
.L883:
	ldr	fp, [sp, #24]
	cmp	fp, #256
	bne	.L884
	ldr	r1, [r4, #4]
	mov	r0, r9
	bl	sftl_printk
	str	fp, [r4]
.L884:
	ldr	r3, [r4, #12]
	cmp	r3, #0
	beq	.L885
	ldr	r2, [r3]
	ldr	r3, [r6, #3332]
	ldr	r3, [r3]
	cmp	r2, r3
	beq	.L885
	ldr	r1, [r4, #4]
	mov	r0, r10
	bl	sftl_printk
	mvn	r3, #0
	str	r3, [r4]
.L885:
	ldr	r3, [r4, #8]
	cmp	r3, #0
	beq	.L886
	ldr	r2, [r3]
	ldr	r3, [r6, #3324]
	ldr	r3, [r3]
	cmp	r2, r3
	beq	.L886
	ldr	r1, [r4, #4]
	ldr	r0, .L905+20
	bl	sftl_printk
	mvn	r3, #0
	str	r3, [r4]
.L886:
	add	r5, r5, #1
	add	r4, r4, #20
	b	.L882
.L881:
	ldr	r3, [r5, #8]
	cmp	r3, #0
	beq	.L868
	ldr	r3, [r5, #12]
	cmp	r3, #0
	bne	.L869
.L868:
	mov	r2, #148
	ldr	r1, .L905+24
	ldr	r0, .L905+28
	bl	sftl_printk
.L869:
	add	r2, sp, #16
	add	r1, sp, #20
	mov	r0, r5
	bl	l2p_addr_tran
	ldr	r7, [sp, #16]
	cmp	r7, #3
	bls	.L870
.L904:
	mvn	r3, #0
	str	r3, [r5]
	b	.L871
.L870:
	cmp	r7, #0
	bne	.L872
	ldr	r3, [sp, #20]
	ldr	r2, [sp, #4]
	cmp	r2, r3
	bls	.L872
	ldr	r5, .L905+24
	ldr	r6, .L905+32
	ldr	r8, .L905+36
	b	.L902
.L874:
	mvn	r3, #0
	ldr	r2, [r4, #-16]
	str	r3, [r4, #-20]
	mov	r1, r5
	mov	r0, r6
	add	r7, r7, #1
	bl	sftl_printk
	mov	r3, #16
	mov	r2, #4
	ldr	r1, [r4, #-12]
	mov	r0, r8
	bl	rknand_print_hex
	mov	r3, #4
	ldr	r1, [r4, #-8]
	mov	r2, r3
	ldr	r0, .L905+40
	bl	rknand_print_hex
.L902:
	cmp	r7, r10
	add	r4, r4, #20
	bne	.L874
	bl	dump_stack
.L875:
	ldr	r3, [sp, #12]
	mov	r0, #0
	ldr	r2, [sp, #44]
	ldr	r3, [r3]
	cmp	r2, r3
	beq	.L888
	bl	__stack_chk_fail
.L872:
	ldr	r1, [r5, #8]
	tst	r1, #63
	moveq	r7, r1
	beq	.L876
	ldr	r7, [r9, #3324]
	cmp	r1, r7
	beq	.L876
	ldr	r3, .L905+44
	mov	r0, r7
	ldrh	r2, [r3]
	lsl	r2, r2, #9
	bl	ftl_memcpy
.L876:
	ldr	r3, [r5, #12]
	mov	r2, r7
	ldr	r1, [sp, #20]
	ldrb	r0, [sp, #16]	@ zero_extendqisi2
	ldr	ip, [r9, #3256]
	blx	ip
	cmp	r0, #0
	mvnne	r3, #0
	streq	r0, [r5]
	strne	r3, [r5]
	ldrh	r3, [r9, #14]
	cmp	r3, #4
	bne	.L871
	ldr	r3, [r5, #12]
	add	r2, r7, #2048
	ldr	r1, [sp, #20]
	ldrb	r0, [sp, #16]	@ zero_extendqisi2
	add	r3, r3, #8
	ldr	ip, [r9, #3256]
	add	r1, fp, r1
	blx	ip
	cmp	r0, #0
	bne	.L904
.L871:
	add	r8, r8, #1
	add	r5, r5, #20
	b	.L867
.L888:
	add	sp, sp, #52
	@ sp needed
	pop	{r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L906:
	.align	2
.L905:
	.word	.LANCHOR0
	.word	__stack_chk_guard
	.word	.LC95
	.word	.LC96
	.word	.LC97
	.word	.LC98
	.word	.LANCHOR1+256
	.word	.LC8
	.word	.LC86
	.word	.LC87
	.word	.LC88
	.word	.LANCHOR0+258
	.fnend
	.size	FlashProgPages, .-FlashProgPages
	.align	2
	.global	FtlLowFormatEraseBlock
	.syntax unified
	.arm
	.fpu softvfp
	.type	FtlLowFormatEraseBlock, %function
FtlLowFormatEraseBlock:
	.fnstart
	@ args = 0, pretend = 0, frame = 16
	@ frame_needed = 0, uses_anonymous_args = 0
	push	{r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.save {r4, r5, r6, r7, r8, r9, r10, fp, lr}
	mov	r10, #0
	ldr	r4, .L950
	mov	r6, r1
	mov	r7, r10
	mov	r5, r10
	mov	r9, #20
	.pad #20
	sub	sp, sp, #20
	add	fp, r4, #312
	str	r0, [sp, #4]
	str	r0, [r4, #3288]
.L908:
	ldrh	r1, [r4, #232]
	uxth	r3, r10
	cmp	r1, r3
	bhi	.L912
	cmp	r7, #0
	beq	.L907
	mov	r8, #0
	mov	r9, #20
	mov	r2, r7
	mov	r1, #0
	ldr	r0, [r4, #3272]
	bl	FlashEraseBlocks
.L915:
	uxth	r3, r8
	cmp	r7, r3
	bhi	.L917
.L918:
	cmp	r6, #0
	mov	r8, #0
	ldrne	r3, .L950+4
	mov	fp, #20
	moveq	r3, #2
	streq	r6, [sp, #8]
	streq	r3, [sp, #12]
	ldrhne	r3, [r3]
	strne	r3, [sp, #12]
	movne	r3, #1
	strne	r3, [sp, #8]
.L927:
	mov	r10, #0
	mov	r7, r10
.L919:
	ldrh	r1, [r4, #232]
	uxth	r3, r10
	cmp	r1, r3
	bhi	.L922
	cmp	r7, #0
	beq	.L907
	mov	r9, #0
	mov	r3, #1
	ldr	r2, [sp, #8]
	mov	r1, r7
	ldr	r0, [r4, #3272]
	bl	FlashProgPages
.L924:
	uxth	r3, r9
	cmp	r7, r3
	bhi	.L926
	add	r8, r8, #1
	ldr	r2, [sp, #12]
	uxth	r3, r8
	cmp	r2, r3
	bhi	.L927
	mov	r8, #0
	mov	r9, #20
.L928:
	uxth	r3, r8
	cmp	r7, r3
	bhi	.L930
	ldr	r3, [sp, #4]
	adds	r6, r6, #0
	movne	r6, #1
	cmp	r3, #63
	orrls	r6, r6, #1
	cmp	r6, #0
	beq	.L907
	mov	r2, r7
	ldr	r1, [sp, #8]
	ldr	r0, [r4, #3272]
	bl	FlashEraseBlocks
.L907:
	mov	r0, r5
	add	sp, sp, #20
	@ sp needed
	pop	{r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L912:
	uxth	r3, r10
	ldr	r0, [r4, #3272]
	mov	r2, #0
	mul	r1, r9, r3
	add	r3, r4, r3
	str	r2, [r0, r1]
	ldr	r1, [sp, #4]
	ldrb	r0, [r3, #260]	@ zero_extendqisi2
	bl	V2P_block
	cmp	r6, #0
	mov	r8, r0
	beq	.L909
	bl	IsBlkInVendorPart
	cmp	r0, #0
	bne	.L910
.L909:
	mov	r0, r8
	bl	FtlBbmIsBadBlock
	cmp	r0, #0
	addne	r5, r5, #1
	uxthne	r5, r5
	bne	.L910
	ldr	r1, [r4, #3272]
	lsl	r8, r8, #10
	ldr	r3, [r4, #3320]
	mla	r1, r9, r7, r1
	str	r3, [r1, #8]
	ldrh	r3, [fp]
	str	r8, [r1, #4]
	mul	r3, r7, r3
	add	r7, r7, #1
	uxth	r7, r7
	add	r0, r3, #3
	cmp	r3, #0
	movlt	r3, r0
	ldr	r0, [r4, #3340]
	bic	r3, r3, #3
	add	r3, r0, r3
	str	r3, [r1, #12]
.L910:
	add	r10, r10, #1
	b	.L908
.L917:
	mul	r3, r9, r8
	ldr	r2, [r4, #3272]
	add	r1, r2, r3
	ldr	r3, [r2, r3]
	cmn	r3, #1
	bne	.L916
	ldr	r0, [r1, #4]
	add	r5, r5, #1
	uxth	r5, r5
	ubfx	r0, r0, #10, #16
	bl	FtlBbmMapBadBlock
.L916:
	add	r8, r8, #1
	b	.L915
.L922:
	uxth	r3, r10
	ldr	r0, [r4, #3272]
	mov	r2, #0
	mul	r1, fp, r3
	add	r3, r4, r3
	str	r2, [r0, r1]
	ldr	r1, [sp, #4]
	ldrb	r0, [r3, #260]	@ zero_extendqisi2
	bl	V2P_block
	cmp	r6, #0
	mov	r9, r0
	beq	.L920
	bl	IsBlkInVendorPart
	cmp	r0, #0
	bne	.L921
.L920:
	mov	r0, r9
	bl	FtlBbmIsBadBlock
	cmp	r0, #0
	bne	.L921
	ldr	r1, [r4, #3272]
	add	r9, r8, r9, lsl #10
	ldr	r3, [r4, #3316]
	mla	r1, fp, r7, r1
	str	r3, [r1, #8]
	ldr	r3, .L950+8
	str	r9, [r1, #4]
	ldrh	r3, [r3]
	mul	r3, r7, r3
	add	r7, r7, #1
	uxth	r7, r7
	add	r0, r3, #3
	cmp	r3, #0
	movlt	r3, r0
	ldr	r0, [r4, #3320]
	bic	r3, r3, #3
	add	r3, r0, r3
	str	r3, [r1, #12]
.L921:
	add	r10, r10, #1
	b	.L919
.L926:
	mul	r3, fp, r9
	ldr	r2, [r4, #3272]
	add	r1, r2, r3
	ldr	r3, [r2, r3]
	cmp	r3, #0
	beq	.L925
	ldr	r0, [r1, #4]
	add	r5, r5, #1
	uxth	r5, r5
	ubfx	r0, r0, #10, #16
	bl	FtlBbmMapBadBlock
.L925:
	add	r9, r9, #1
	b	.L924
.L930:
	cmp	r6, #0
	beq	.L929
	mul	r3, r9, r8
	ldr	r2, [r4, #3272]
	add	r1, r2, r3
	ldr	r3, [r2, r3]
	cmp	r3, #0
	bne	.L929
	ldr	r0, [r1, #4]
	mov	r1, #1
	ubfx	r0, r0, #10, #16
	bl	FtlFreeSysBlkQueueIn
.L929:
	add	r8, r8, #1
	b	.L928
.L951:
	.align	2
.L950:
	.word	.LANCHOR0
	.word	.LANCHOR0+304
	.word	.LANCHOR0+312
	.fnend
	.size	FtlLowFormatEraseBlock, .-FtlLowFormatEraseBlock
	.align	2
	.global	FlashTestBlk
	.syntax unified
	.arm
	.fpu softvfp
	.type	FlashTestBlk, %function
FlashTestBlk:
	.fnstart
	@ args = 0, pretend = 0, frame = 88
	@ frame_needed = 0, uses_anonymous_args = 0
	ldr	r3, .L959
	cmp	r0, #11
	push	{r4, r5, r6, lr}
	.save {r4, r5, r6, lr}
	.pad #88
	sub	sp, sp, #88
	movls	r4, #0
	mov	r6, r3
	ldr	r2, [r3]
	str	r2, [sp, #84]
	bls	.L952
	ldr	r4, .L959+4
	mov	r5, r0
	mov	r2, #32
	add	r0, sp, #20
	mov	r1, #165
	str	r0, [sp, #12]
	ldr	r3, [r4, #3328]
	lsl	r5, r5, #10
	str	r3, [sp, #8]
	bl	ftl_memset
	mov	r2, #8
	mov	r1, #90
	ldr	r0, [r4, #3328]
	bl	ftl_memset
	mov	r2, #1
	mov	r0, sp
	mov	r1, r2
	str	r5, [sp, #4]
	bl	FlashEraseBlocks
	mov	r3, #1
	mov	r0, sp
	mov	r2, r3
	mov	r1, r3
	bl	FlashProgPages
	ldr	r3, [sp]
	cmp	r3, #0
	mvnne	r4, #0
	bne	.L954
	add	r3, r5, #1
	mov	r0, sp
	str	r3, [sp, #4]
	mov	r3, #1
	mov	r2, r3
	mov	r1, r3
	bl	FlashProgPages
	ldr	r4, [sp]
	adds	r4, r4, #0
	movne	r4, #1
	rsb	r4, r4, #0
.L954:
	mov	r2, #1
	mov	r1, #0
	mov	r0, sp
	str	r5, [sp, #4]
	bl	FlashEraseBlocks
.L952:
	ldr	r2, [sp, #84]
	mov	r0, r4
	ldr	r3, [r6]
	cmp	r2, r3
	beq	.L955
	bl	__stack_chk_fail
.L955:
	add	sp, sp, #88
	@ sp needed
	pop	{r4, r5, r6, pc}
.L960:
	.align	2
.L959:
	.word	__stack_chk_guard
	.word	.LANCHOR0
	.fnend
	.size	FlashTestBlk, .-FlashTestBlk
	.align	2
	.global	FtlBbmTblFlush
	.syntax unified
	.arm
	.fpu softvfp
	.type	FtlBbmTblFlush, %function
FtlBbmTblFlush:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	push	{r0, r1, r2, r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.save {r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.pad #12
	mov	r5, #0
	ldr	r4, .L974
	mov	r1, #0
	ldr	r3, [r4, #3336]
	add	r6, r4, #376
	ldr	r0, [r4, #3304]
	add	r7, r4, #3392
	str	r3, [r4, #3468]
	movw	r3, #310
	ldrh	r2, [r4, r3]
	str	r0, [r4, #3464]
	bl	ftl_memset
.L962:
	ldrh	r3, [r4, #254]
	cmp	r5, r3
	blt	.L963
	ldr	r7, [r4, #3468]
	mov	r2, #16
	mov	r1, #255
	ldr	r6, .L974+4
	ldr	r10, .L974+8
	mov	r5, #0
	mov	r0, r7
	mov	r8, r5
	bl	ftl_memset
	ldr	r3, .L974+12
	sub	r9, r6, #48
	strh	r3, [r7]	@ movhi
	ldr	r3, [r4, #360]
	str	r3, [r7, #4]
	ldrh	r3, [r6]
	strh	r3, [r7, #2]	@ movhi
	ldrh	r3, [r6, #4]
	strh	r3, [r7, #8]	@ movhi
	ldrh	r3, [r6, #6]
	strh	r3, [r7, #10]	@ movhi
	ldr	r3, [r4, #228]
	strh	r3, [r7, #12]	@ movhi
	ldr	r3, [r4, #2608]
	strh	r3, [r7, #14]	@ movhi
.L964:
	ldr	r3, [r4, #3304]
	mov	fp, #0
	ldrh	r2, [r6, #2]
	ldrh	r1, [r6]
	str	r3, [r4, #3464]
	ldr	r3, [r4, #3336]
	str	fp, [r4, #3456]
	str	r3, [r4, #3468]
	orr	r3, r2, r1, lsl #10
	ldrh	r0, [r7, #10]
	str	r3, [r4, #3460]
	ldrh	r3, [r6, #4]
	str	r0, [sp]
	mov	r0, r10
	bl	sftl_printk
	ldrh	r3, [r9]
	ldrh	r2, [r6, #2]
	sub	r3, r3, #1
	cmp	r2, r3
	blt	.L965
	ldr	r3, [r4, #360]
	ldrh	r2, [r6]
	ldr	r0, [r4, #3272]
	add	r3, r3, #1
	strh	fp, [r6, #2]	@ movhi
	str	r3, [r4, #360]
	str	r3, [r7, #4]
	ldrh	r3, [r6, #4]
	strh	r2, [r7, #8]	@ movhi
	strh	r2, [r6, #4]	@ movhi
	mov	r2, #1
	strh	r3, [r6]	@ movhi
	mov	r1, r2
	lsl	r3, r3, #10
	str	r3, [r4, #3460]
	str	r3, [r0, #4]
	bl	FlashEraseBlocks
.L965:
	mov	r3, #1
	ldr	r0, .L974+16
	mov	r2, r3
	mov	r1, r3
	bl	FlashProgPages
	ldrh	r3, [r6, #2]
	add	r3, r3, #1
	strh	r3, [r6, #2]	@ movhi
	ldr	r3, [r4, #3456]
	cmn	r3, #1
	bne	.L966
	add	r5, r5, #1
	ldr	r1, [r4, #3460]
	uxth	r5, r5
	ldr	r0, .L974+20
	bl	sftl_printk
	cmp	r5, #3
	bls	.L964
	mov	r2, r5
	ldr	r1, [r4, #3460]
	ldr	r0, .L974+24
	bl	sftl_printk
.L968:
	b	.L968
.L963:
	ldrh	r2, [r7]
	ldr	r3, [r4, #3464]
	ldr	r1, [r6, #4]!
	mul	r0, r2, r5
	lsl	r2, r2, #2
	add	r5, r5, #1
	add	r0, r3, r0, lsl #2
	bl	ftl_memcpy
	b	.L962
.L969:
	mov	r8, #1
	b	.L964
.L966:
	add	r8, r8, #1
	cmp	r8, #1
	ble	.L969
	cmp	r3, #256
	beq	.L964
	mov	r0, #0
	add	sp, sp, #12
	@ sp needed
	pop	{r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L975:
	.align	2
.L974:
	.word	.LANCHOR0
	.word	.LANCHOR0+352
	.word	.LC99
	.word	-3887
	.word	.LANCHOR0+3456
	.word	.LC100
	.word	.LC101
	.fnend
	.size	FtlBbmTblFlush, .-FtlBbmTblFlush
	.align	2
	.global	allocate_data_superblock
	.syntax unified
	.arm
	.fpu softvfp
	.type	allocate_data_superblock, %function
allocate_data_superblock:
	.fnstart
	@ args = 0, pretend = 0, frame = 16
	@ frame_needed = 0, uses_anonymous_args = 0
	push	{r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.save {r4, r5, r6, r7, r8, r9, r10, fp, lr}
	mov	r5, r0
	ldr	r4, .L1013
	.pad #20
	sub	sp, sp, #20
	add	fp, r4, #2528
.L977:
	ldr	r2, .L1013+4
	ldrh	r3, [r4, #224]
	ldrh	r2, [r2]
	add	r3, r3, r2
	ldrh	r2, [r4, #240]
	cmp	r3, r2
	ble	.L978
	movw	r2, #2660
	ldr	r1, .L1013+8
	ldr	r0, .L1013+12
	bl	sftl_printk
.L978:
	ldr	r3, .L1013+16
	cmp	r5, r3
	movne	r1, #0
	bne	.L979
	ldrh	r3, [r4, #224]
	ldr	r1, [r4, #3168]
	mul	r2, r1, r3
	lsr	r1, r3, #1
	add	r1, r1, #1
	add	r1, r1, r2, lsr #2
	uxth	r1, r1
	cmp	r1, #0
	subne	r1, r1, #1
	uxthne	r1, r1
.L979:
	ldr	r0, .L1013+20
	bl	List_pop_index_node
	ldrh	r3, [r4, #224]
	mov	r6, r0
	uxth	r8, r0
	cmp	r3, #0
	bne	.L980
	movw	r2, #2669
	ldr	r1, .L1013+8
	ldr	r0, .L1013+12
	bl	sftl_printk
.L980:
	ldrh	r3, [r4, #224]
	sub	r3, r3, #1
	strh	r3, [r4, #224]	@ movhi
	ldrh	r3, [r4, #240]
	cmp	r3, r8
	bls	.L977
	uxth	r6, r6
	ldr	r3, [r4, #72]
	lsl	r6, r6, #1
	ldrh	r7, [r3, r6]
	cmp	r7, #0
	bne	.L977
	strh	r8, [r5]	@ movhi
	mov	r0, r5
	bl	make_superblock
	ldrb	r3, [r5, #7]	@ zero_extendqisi2
	cmp	r3, #0
	bne	.L982
	ldr	r3, [r4, #72]
	mvn	r2, #0
	mov	r0, r8
	strh	r2, [r3, r6]	@ movhi
	bl	INSERT_DATA_LIST
	ldrh	r2, [fp]
	ldrh	r3, [r4, #224]
	add	r3, r3, r2
	ldrh	r2, [r4, #240]
	cmp	r3, r2
	ble	.L977
	movw	r2, #2683
	ldr	r1, .L1013+8
	ldr	r0, .L1013+12
	bl	sftl_printk
	b	.L977
.L982:
	ldrh	r2, [fp]
	ldrh	r3, [r4, #224]
	add	r3, r3, r2
	ldrh	r2, [r4, #240]
	cmp	r3, r2
	ble	.L984
	movw	r2, #2686
	ldr	r1, .L1013+8
	ldr	r0, .L1013+12
	bl	sftl_printk
.L984:
	ldr	r1, [r4, #3272]
	mov	r9, #20
	ldrh	r2, [r4, #232]
	add	r10, r5, #16
	mov	r0, r10
	mov	ip, #0
	mov	r3, r1
	mla	r2, r9, r2, r1
	str	r2, [sp, #4]
.L985:
	ldr	r2, [sp, #4]
	cmp	r2, r3
	bne	.L987
	cmp	r7, #0
	bne	.L988
	movw	r2, #2697
	ldr	r1, .L1013+8
	ldr	r0, .L1013+12
	bl	sftl_printk
.L988:
	ldrh	r3, [r4, #172]
	cmp	r3, r8
	bne	.L989
	movw	r2, #2699
	ldr	r1, .L1013+8
	ldr	r0, .L1013+12
	bl	sftl_printk
.L989:
	ldrb	r3, [r5, #8]	@ zero_extendqisi2
	ldr	r1, [r4, #2536]
	ldr	r2, .L1013+24
	cmp	r3, #0
	ldrh	r3, [r1, r6]
	bne	.L990
	cmp	r3, #0
	ldrhne	r0, [r2]
	moveq	r3, #2
	addne	r3, r3, r0
	strh	r3, [r1, r6]	@ movhi
	ldr	r3, [r4, #2600]
	add	r3, r3, #1
	str	r3, [r4, #2600]
.L993:
	ldr	r3, [r4, #2536]
	ldr	r1, [r4, #2616]
	ldrh	r2, [r2]
	ldrh	r3, [r3, r6]
	ldr	r0, [r4, #2600]
	cmp	r3, r1
	ldrh	r1, [r4, #240]
	strhi	r3, [r4, #2616]
	ldr	r3, [r4, #2604]
	mla	r0, r0, r2, r3
	bl	__aeabi_uidiv
	ldr	r2, [r4, #3352]
	ldr	r1, [r4, #3272]
	str	r0, [r4, #2608]
	ldr	r3, [r2, #16]
	ldr	r0, .L1013+28
	add	r3, r3, #1
	str	r3, [r2, #16]
	mov	r2, #20
	mla	r2, r2, r7, r1
	add	r3, r1, #4
	add	r2, r2, #24
.L995:
	add	r3, r3, #20
	cmp	r2, r3
	bne	.L996
	ldrb	r1, [r5, #8]	@ zero_extendqisi2
	mov	r2, r7
	ldr	r0, [r4, #3272]
	mov	r9, #0
	bl	FlashEraseBlocks
	mov	r3, r9
	mov	r1, #20
.L997:
	uxth	r2, r9
	cmp	r7, r2
	bhi	.L999
	cmp	r3, #0
	ble	.L1000
	mov	r0, r8
	bl	update_multiplier_value
	bl	FtlBbmTblFlush
.L1000:
	ldrb	r2, [r5, #7]	@ zero_extendqisi2
	cmp	r2, #0
	bne	.L1001
	ldr	r3, [r4, #72]
	mvn	r2, #0
	strh	r2, [r3, r6]	@ movhi
	b	.L977
.L987:
	str	ip, [r3, #8]
	movw	lr, #65535
	str	ip, [r3, #12]
	add	r3, r3, #20
	ldrh	r2, [r0], #2
	cmp	r2, lr
	mlane	lr, r9, r7, r1
	lslne	r2, r2, #10
	addne	r7, r7, #1
	uxthne	r7, r7
	strne	r2, [lr, #4]
	b	.L985
.L990:
	add	r3, r3, #1
	strh	r3, [r1, r6]	@ movhi
	ldr	r3, [r4, #2604]
	add	r3, r3, #1
	str	r3, [r4, #2604]
	b	.L993
.L996:
	ldr	r1, [r3, #-20]
	and	r1, r1, r0
	str	r1, [r3, #-20]
	b	.L995
.L999:
	mul	r2, r1, r9
	ldr	r0, [r4, #3272]
	add	ip, r0, r2
	ldr	r2, [r0, r2]
	cmn	r2, #1
	bne	.L998
	ldr	r0, [ip, #4]
	add	r3, r3, #1
	str	r1, [sp, #12]
	str	r2, [sp, #8]
	ubfx	r0, r0, #10, #16
	str	r3, [sp, #4]
	bl	FtlBbmMapBadBlock
	ldr	r2, [sp, #8]
	ldr	r1, [sp, #12]
	ldr	r3, [sp, #4]
	strh	r2, [r10]	@ movhi
	ldrb	r2, [r5, #7]	@ zero_extendqisi2
	sub	r2, r2, #1
	strb	r2, [r5, #7]
.L998:
	add	r9, r9, #1
	add	r10, r10, #2
	b	.L997
.L1001:
	movw	r3, #302
	ldrh	r3, [r4, r3]
	strh	r8, [r5]	@ movhi
	smulbb	r3, r3, r2
	mov	r2, #0
	strh	r2, [r5, #2]	@ movhi
	strb	r2, [r5, #6]
	ldr	r2, [r4, #2592]
	uxth	r3, r3
	ldr	r1, [r4, #72]
	strh	r3, [r5, #4]	@ movhi
	str	r2, [r5, #12]
	add	r2, r2, #1
	str	r2, [r4, #2592]
	ldrh	r2, [r5]
	lsl	r2, r2, #1
	strh	r3, [r1, r2]	@ movhi
	ldrh	r3, [r5, #4]
	cmp	r3, #0
	beq	.L1002
	ldrb	r3, [r5, #7]	@ zero_extendqisi2
	cmp	r3, #0
	bne	.L1003
.L1002:
	mov	r2, #2752
	ldr	r1, .L1013+8
	ldr	r0, .L1013+12
	bl	sftl_printk
.L1003:
	mov	r0, #0
	add	sp, sp, #20
	@ sp needed
	pop	{r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L1014:
	.align	2
.L1013:
	.word	.LANCHOR0
	.word	.LANCHOR0+2528
	.word	.LANCHOR1+271
	.word	.LC8
	.word	.LANCHOR0+124
	.word	.LANCHOR0+2532
	.word	.LANCHOR0+292
	.word	-1024
	.fnend
	.size	allocate_data_superblock, .-allocate_data_superblock
	.align	2
	.global	FtlGcFreeBadSuperBlk
	.syntax unified
	.arm
	.fpu softvfp
	.type	FtlGcFreeBadSuperBlk, %function
FtlGcFreeBadSuperBlk:
	.fnstart
	@ args = 0, pretend = 0, frame = 8
	@ frame_needed = 0, uses_anonymous_args = 0
	push	{r0, r1, r2, r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.save {r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.pad #12
	movw	r3, #3210
	ldr	r6, .L1028
	ldrh	r3, [r6, r3]
	cmp	r3, #0
	beq	.L1016
	ldr	fp, .L1028+4
	mov	r7, #0
	str	r0, [sp, #4]
	add	r10, fp, #2
.L1017:
	ldrh	r2, [r6, #232]
	uxth	r3, r7
	cmp	r2, r3
	bhi	.L1023
	bl	FtlGcReFreshBadBlk
.L1016:
	mov	r0, #0
	add	sp, sp, #12
	@ sp needed
	pop	{r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L1023:
	uxtah	r3, r6, r7
	ldr	r1, [sp, #4]
	mov	r8, #0
	ldrb	r0, [r3, #260]	@ zero_extendqisi2
	bl	V2P_block
	mov	r9, r0
.L1018:
	ldrh	r3, [fp]
	uxth	r4, r8
	cmp	r3, r4
	addls	r7, r7, #1
	bls	.L1017
.L1022:
	uxth	r5, r8
	lsl	r3, r5, #1
	ldrh	r3, [r10, r3]
	cmp	r3, r9
	bne	.L1019
	mov	r1, r9
	ldr	r0, .L1028+8
	bl	sftl_printk
	mov	r0, r9
	bl	FtlBbmMapBadBlock
	bl	FtlBbmTblFlush
	ldrh	r1, [fp]
	add	r3, r10, r5, lsl #1
.L1020:
	cmp	r4, r1
	bcc	.L1021
	sub	r1, r1, #1
	strh	r1, [fp]	@ movhi
.L1019:
	add	r8, r8, #1
	b	.L1018
.L1021:
	ldrh	r0, [r3, #2]!
	add	r4, r4, #1
	uxth	r4, r4
	strh	r0, [r3, #-2]	@ movhi
	b	.L1020
.L1029:
	.align	2
.L1028:
	.word	.LANCHOR0
	.word	.LANCHOR0+3210
	.word	.LC102
	.fnend
	.size	FtlGcFreeBadSuperBlk, .-FtlGcFreeBadSuperBlk
	.align	2
	.global	update_vpc_list
	.syntax unified
	.arm
	.fpu softvfp
	.type	update_vpc_list, %function
update_vpc_list:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	push	{r4, r5, r6, lr}
	.save {r4, r5, r6, lr}
	lsl	r3, r0, #1
	ldr	r4, .L1043
	ldr	r2, [r4, #72]
	ldrh	r3, [r2, r3]
	cmp	r3, #0
	bne	.L1031
	ldrh	r3, [r4, #222]
	mov	r5, r0
	cmp	r3, r0
	mvneq	r3, #0
	strheq	r3, [r4, #222]	@ movhi
	ldrh	r3, [r4, #220]
	cmp	r3, r0
	mvneq	r3, #0
	strheq	r3, [r4, #220]	@ movhi
	ldrh	r3, [r4, #172]
	cmp	r3, r0
	mvneq	r3, #0
	strheq	r3, [r4, #172]	@ movhi
	beq	.L1035
	ldrh	r3, [r4, #24]
	cmp	r3, r0
	beq	.L1040
	ldrh	r3, [r4, #76]
	cmp	r3, r0
	beq	.L1040
	ldrh	r3, [r4, #124]
	cmp	r3, r0
	beq	.L1040
.L1035:
	ldr	r6, .L1043+4
	mov	r1, r5
	ldr	r0, .L1043+8
	bl	List_remove_node
	ldrh	r3, [r6]
	cmp	r3, #0
	bne	.L1037
	movw	r2, #2828
	ldr	r1, .L1043+12
	ldr	r0, .L1043+16
	bl	sftl_printk
.L1037:
	ldrh	r3, [r6]
	mov	r0, r5
	sub	r3, r3, #1
	strh	r3, [r6]	@ movhi
	bl	free_data_superblock
	mov	r0, r5
	bl	FtlGcFreeBadSuperBlk
	ldrh	r2, [r6]
	ldrh	r3, [r4, #224]
	add	r3, r3, r2
	ldrh	r2, [r4, #240]
	cmp	r3, r2
	ble	.L1041
	movw	r2, #2831
	ldr	r1, .L1043+12
	ldr	r0, .L1043+16
	bl	sftl_printk
.L1041:
	mov	r0, #1
	pop	{r4, r5, r6, pc}
.L1031:
	bl	List_update_data_list
.L1040:
	mov	r0, #0
	pop	{r4, r5, r6, pc}
.L1044:
	.align	2
.L1043:
	.word	.LANCHOR0
	.word	.LANCHOR0+2528
	.word	.LANCHOR0+2520
	.word	.LANCHOR1+296
	.word	.LC8
	.fnend
	.size	update_vpc_list, .-update_vpc_list
	.align	2
	.global	decrement_vpc_count
	.syntax unified
	.arm
	.fpu softvfp
	.type	decrement_vpc_count, %function
decrement_vpc_count:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	movw	r3, #65535
	push	{r4, r5, r6, r7, r8, lr}
	.save {r4, r5, r6, r7, r8, lr}
	cmp	r0, r3
	mov	r6, r0
	ldr	r4, .L1058
	beq	.L1046
	ldr	r3, [r4, #72]
	lsl	r5, r0, #1
	ldrh	r2, [r3, r5]
	cmp	r2, #0
	subne	r2, r2, #1
	strhne	r2, [r3, r5]	@ movhi
	bne	.L1046
	mov	r1, r0
	ldr	r0, .L1058+4
	bl	sftl_printk
	ldr	r3, [r4, #72]
	ldrh	r5, [r3, r5]
	cmp	r5, #0
	beq	.L1048
.L1056:
	mov	r5, #0
.L1045:
	mov	r0, r5
	pop	{r4, r5, r6, r7, r8, pc}
.L1048:
	movw	r2, #2846
.L1057:
	ldr	r1, .L1058+8
	ldr	r0, .L1058+12
	bl	sftl_printk
	b	.L1045
.L1046:
	movw	r7, #3442
	movw	r3, #65535
	ldrh	r0, [r4, r7]
	cmp	r0, r3
	strheq	r6, [r4, r7]	@ movhi
	beq	.L1056
.L1050:
	cmp	r6, r0
	beq	.L1056
	bl	update_vpc_list
	ldr	r2, [r4, #2516]
	adds	r5, r0, #0
	ldr	r3, [r4, #2520]
	movne	r5, #1
	ldr	r1, [r4, #72]
	strh	r6, [r4, r7]	@ movhi
	sub	r3, r3, r2
	asr	r2, r3, #1
	ldr	r3, .L1058+16
	mul	r3, r3, r2
	uxth	r2, r3
	uxth	r3, r3
	lsl	r2, r2, #1
	cmp	r3, r6
	ldrh	r2, [r1, r2]
	clz	r2, r2
	lsr	r2, r2, #5
	moveq	r2, #0
	cmp	r2, #0
	beq	.L1045
	movw	r2, #2862
	b	.L1057
.L1059:
	.align	2
.L1058:
	.word	.LANCHOR0
	.word	.LC103
	.word	.LANCHOR1+312
	.word	.LC8
	.word	-1431655765
	.fnend
	.size	decrement_vpc_count, .-decrement_vpc_count
	.align	2
	.global	FtlSuperblockPowerLostFix
	.syntax unified
	.arm
	.fpu softvfp
	.type	FtlSuperblockPowerLostFix, %function
FtlSuperblockPowerLostFix:
	.fnstart
	@ args = 0, pretend = 0, frame = 24
	@ frame_needed = 0, uses_anonymous_args = 0
	push	{r4, r5, r6, r7, r8, r9, lr}
	.save {r4, r5, r6, r7, r8, r9, lr}
	.pad #28
	sub	sp, sp, #28
	ldr	r7, .L1075
	mov	r8, #0
	movw	r2, #61589
	mov	r4, r0
	ldr	r5, .L1075+4
	ldr	r3, [r7]
	ldr	r6, [r5, #3336]
	str	r3, [sp, #20]
	mvn	r3, #0
	str	r3, [sp, #16]
	ldr	r3, [r5, #3304]
	str	r6, [sp, #12]
	str	r3, [sp, #8]
	mvn	r3, #2
	str	r3, [r6, #8]
	mvn	r3, #1
	str	r3, [r6, #12]
	ldrh	r3, [r0]
	strh	r8, [r6]	@ movhi
	strh	r3, [r6, #2]	@ movhi
	ldr	r3, [r5, #3304]
	str	r2, [r3]
	ldr	r2, .L1075+8
	ldr	r3, [r5, #3304]
	str	r2, [r3, #4]
	ldrh	r3, [r0, #4]
	tst	r3, #1
	moveq	r9, #6
	movne	r9, #7
.L1066:
	ldrh	r3, [r4, #4]
	cmp	r3, #0
	bne	.L1062
.L1063:
	ldrh	r3, [r4]
	ldr	r1, [r5, #72]
	ldrh	r0, [r4, #4]
	lsl	r3, r3, #1
	ldrh	r2, [r1, r3]
	sub	r2, r2, r0
	strh	r2, [r1, r3]	@ movhi
	movw	r3, #302
	ldrh	r3, [r5, r3]
	ldr	r2, [sp, #20]
	strh	r3, [r4, #2]	@ movhi
	mov	r3, #0
	strb	r3, [r4, #6]
	strh	r3, [r4, #4]	@ movhi
	ldr	r3, [r7]
	cmp	r2, r3
	beq	.L1067
	bl	__stack_chk_fail
.L1062:
	mov	r0, r4
	bl	get_new_active_ppa
	cmn	r0, #1
	str	r0, [sp, #4]
	beq	.L1063
	ldr	r3, [r5, #2596]
	mov	r1, #1
	mov	r0, sp
	str	r3, [r6, #4]
	add	r3, r3, #1
	cmn	r3, #1
	moveq	r3, r8
	str	r3, [r5, #2596]
	mov	r3, #0
	mov	r2, r3
	bl	FlashProgPages
	ldrh	r0, [r4]
	bl	decrement_vpc_count
	subs	r9, r9, #1
	bne	.L1066
	b	.L1063
.L1067:
	add	sp, sp, #28
	@ sp needed
	pop	{r4, r5, r6, r7, r8, r9, pc}
.L1076:
	.align	2
.L1075:
	.word	__stack_chk_guard
	.word	.LANCHOR0
	.word	305419896
	.fnend
	.size	FtlSuperblockPowerLostFix, .-FtlSuperblockPowerLostFix
	.align	2
	.global	FtlMakeBbt
	.syntax unified
	.arm
	.fpu softvfp
	.type	FtlMakeBbt, %function
FtlMakeBbt:
	.fnstart
	@ args = 0, pretend = 0, frame = 8
	@ frame_needed = 0, uses_anonymous_args = 0
	push	{r0, r1, r2, r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.save {r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.pad #12
	mov	r9, #0
	ldr	r8, .L1098
	bl	FtlBbtMemInit
	sub	r4, r8, #28
	sub	r6, r8, #380
	sub	r10, r8, #18
	mov	r5, r6
	sub	fp, r4, #54
	bl	FtlLoadFactoryBbt
.L1078:
	ldrh	r3, [r6, #254]
	cmp	r9, r3
	bcc	.L1084
	ldr	r7, .L1098+4
	mov	r6, #0
.L1085:
	ldrh	r3, [r7]
	uxth	r0, r6
	add	r6, r6, #1
	cmp	r3, r0
	bhi	.L1086
	ldrh	r6, [r4, #12]
	movw	r7, #65535
	sub	r6, r6, #1
	uxth	r6, r6
.L1087:
	ldrh	r3, [r4, #12]
	sub	r3, r3, #48
	cmp	r6, r3
	ble	.L1091
	mov	r0, r6
	bl	FtlBbmIsBadBlock
	cmp	r0, #1
	beq	.L1088
	mov	r0, r6
	bl	FlashTestBlk
	cmp	r0, #0
	beq	.L1089
	mov	r0, r6
	bl	FtlBbmMapBadBlock
.L1088:
	sub	r6, r6, #1
	uxth	r6, r6
	b	.L1087
.L1084:
	ldr	r3, [r6, #3336]
	movw	r2, #65535
	ldr	r0, [r6, #3304]
	str	r3, [sp, #4]
	str	r3, [r6, #3468]
	ldrh	r3, [r10, #2]!
	str	r0, [r6, #3464]
	cmp	r3, r2
	beq	.L1079
	ldrh	r7, [fp]
	mov	r2, #1
	mov	r1, r2
	ldr	r0, .L1098+8
	mla	r7, r9, r7, r3
	lsl	r3, r7, #10
	str	r3, [r6, #3460]
	bl	FlashReadPages
	ldrh	r2, [fp]
	ldr	r1, [r6, #3464]
	ldr	r0, [r8]
	add	r2, r2, #7
	asr	r2, r2, #3
	bl	ftl_memcpy
.L1080:
	uxth	r0, r7
	add	r9, r9, #1
	add	r8, r8, #4
	bl	FtlBbmMapBadBlock
	b	.L1078
.L1079:
	mov	r1, r9
	bl	FlashGetBadBlockList
	ldr	r1, [r8]
	ldr	r0, [r6, #3464]
	bl	FtlBbt2Bitmap
	ldrh	r3, [fp]
.L1097:
	sub	r3, r3, #1
	uxth	r3, r3
	str	r3, [sp]
.L1081:
	ldr	r3, [sp]
	ldrh	r0, [fp]
	smlabb	r0, r0, r9, r3
	uxth	r0, r0
	bl	FtlBbmIsBadBlock
	cmp	r0, #1
	beq	.L1082
	ldrh	r3, [sp]
	mov	r2, #16
	mov	r1, #0
	ldr	r0, [r5, #3336]
	strh	r3, [r10]	@ movhi
	bl	ftl_memset
	mov	r2, #4096
	mov	r1, #0
	ldr	r0, [r5, #3304]
	bl	ftl_memset
	ldr	r3, [sp, #4]
	movw	r2, 61664	@ movhi
	strh	r2, [r3]	@ movhi
	mov	r3, #0
	ldr	r2, [sp, #4]
	ldrh	r7, [fp]
	str	r3, [r2, #4]
	ldrh	r3, [r10]
	strh	r3, [r2, #2]	@ movhi
	ldrh	r3, [r10]
	ldr	r1, [r8]
	ldr	r0, [r5, #3464]
	mla	r7, r9, r7, r3
	lsl	r3, r7, #10
	str	r3, [r5, #3460]
	ldr	r3, .L1098+12
	ldrh	r2, [r3]
	lsl	r2, r2, #2
	bl	ftl_memcpy
	mov	r2, #1
	ldr	r0, .L1098+8
	mov	r1, r2
	bl	FlashEraseBlocks
	mov	r3, #1
	ldr	r0, .L1098+8
	mov	r2, r3
	mov	r1, r3
	bl	FlashProgPages
	ldr	r3, [r5, #3456]
	cmn	r3, #1
	bne	.L1080
	uxth	r0, r7
	bl	FtlBbmMapBadBlock
	b	.L1081
.L1082:
	ldr	r3, [sp]
	b	.L1097
.L1086:
	bl	FtlBbmMapBadBlock
	b	.L1085
.L1089:
	ldrh	r3, [r4]
	cmp	r3, r7
	strheq	r6, [r4]	@ movhi
	beq	.L1088
.L1090:
	strh	r6, [r4, #4]	@ movhi
.L1091:
	ldrh	r3, [r4]
	mov	r6, #0
	ldr	r0, [r5, #3272]
	mov	r1, #1
	str	r6, [r5, #360]
	mov	r2, #2
	strh	r6, [r4, #2]	@ movhi
	lsl	r3, r3, #10
	str	r3, [r0, #4]
	ldrh	r3, [r4, #4]
	lsl	r3, r3, #10
	str	r3, [r0, #24]
	bl	FlashEraseBlocks
	ldrh	r0, [r4]
	bl	FtlBbmMapBadBlock
	ldrh	r0, [r4, #4]
	bl	FtlBbmMapBadBlock
	bl	FtlBbmTblFlush
	ldr	r3, [r5, #360]
	ldrh	r2, [r4, #4]
	strh	r6, [r4, #2]	@ movhi
	add	r3, r3, #1
	str	r3, [r5, #360]
	ldrh	r3, [r4]
	strh	r2, [r4]	@ movhi
	strh	r3, [r4, #4]	@ movhi
	bl	FtlBbmTblFlush
	mov	r0, r6
	add	sp, sp, #12
	@ sp needed
	pop	{r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L1099:
	.align	2
.L1098:
	.word	.LANCHOR0+380
	.word	.LANCHOR0+314
	.word	.LANCHOR0+3456
	.word	.LANCHOR0+3392
	.fnend
	.size	FtlMakeBbt, .-FtlMakeBbt
	.align	2
	.global	ftl_memcmp
	.syntax unified
	.arm
	.fpu softvfp
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
	.syntax unified
	.arm
	.fpu softvfp
	.type	js_hash, %function
js_hash:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	@ link register save eliminated.
	ldr	r3, .L1104
	add	r1, r0, r1
.L1102:
	cmp	r0, r1
	bne	.L1103
	mov	r0, r3
	bx	lr
.L1103:
	lsr	r2, r3, #2
	ldrb	ip, [r0], #1	@ zero_extendqisi2
	add	r2, r2, r3, lsl #5
	add	r2, r2, ip
	eor	r3, r3, r2
	b	.L1102
.L1105:
	.align	2
.L1104:
	.word	1204201446
	.fnend
	.size	js_hash, .-js_hash
	.align	2
	.global	Ftl_write_map_blk_to_last_page
	.syntax unified
	.arm
	.fpu softvfp
	.type	Ftl_write_map_blk_to_last_page, %function
Ftl_write_map_blk_to_last_page:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	push	{r4, r5, r6, r7, r8, lr}
	.save {r4, r5, r6, r7, r8, lr}
	movw	r2, #65535
	ldrh	r3, [r0]
	mov	r4, r0
	ldr	r5, [r0, #12]
	cmp	r3, r2
	bne	.L1107
	ldrh	r3, [r0, #8]
	cmp	r3, #0
	beq	.L1108
	movw	r2, #641
	ldr	r1, .L1117
	ldr	r0, .L1117+4
	bl	sftl_printk
.L1108:
	ldrh	r3, [r4, #8]
	add	r3, r3, #1
	strh	r3, [r4, #8]	@ movhi
	bl	FtlFreeSysBlkQueueOut
	mov	r3, #0
	strh	r0, [r5]	@ movhi
	strh	r3, [r4, #2]	@ movhi
	strh	r3, [r4]	@ movhi
	ldr	r3, [r4, #28]
	add	r3, r3, #1
	str	r3, [r4, #28]
.L1109:
	mov	r0, #0
	pop	{r4, r5, r6, r7, r8, pc}
.L1107:
	lsl	r3, r3, #1
	ldr	r7, [r0, #24]
	mov	r1, #255
	ldrh	r8, [r5, r3]
	ldrh	r3, [r0, #2]
	ldr	r5, .L1117+8
	orr	r3, r3, r8, lsl #10
	ldr	r6, [r5, #3336]
	str	r3, [r5, #3460]
	ldr	r3, [r5, #3304]
	str	r6, [r5, #3468]
	str	r3, [r5, #3464]
	ldr	r3, [r0, #28]
	str	r3, [r6, #4]
	ldr	r3, .L1117+12
	strh	r3, [r6, #8]	@ movhi
	ldrh	r3, [r0, #4]
	strh	r8, [r6, #2]	@ movhi
	strh	r3, [r6]	@ movhi
	add	r3, r5, #304
	ldrh	r2, [r3]
	ldr	r0, [r5, #3304]
	lsl	r2, r2, #3
	bl	ftl_memset
	mov	r2, #0
	mov	r3, r2
.L1110:
	ldrh	r0, [r4, #6]
	uxth	r1, r2
	cmp	r0, r1
	bhi	.L1112
	movw	r3, #310
	ldr	r0, [r5, #3464]
	ldrh	r1, [r5, r3]
	bl	js_hash
	mov	r2, #1
	str	r0, [r6, #12]
	mov	r3, #0
	mov	r1, r2
	ldr	r0, .L1117+16
	bl	FlashProgPages
	ldrh	r3, [r4, #2]
	mov	r0, r4
	add	r3, r3, #1
	strh	r3, [r4, #2]	@ movhi
	bl	ftl_map_blk_gc
	b	.L1109
.L1112:
	uxth	r1, r2
	ldr	r0, [r7, r1, lsl #2]
	cmp	r8, r0, lsr #10
	bne	.L1111
	ldr	r0, [r5, #3304]
	add	r3, r3, #1
	uxth	r3, r3
	str	r1, [r0, r3, lsl #3]
	ldr	r0, [r7, r1, lsl #2]
	ldr	r1, [r5, #3304]
	add	r1, r1, r3, lsl #3
	str	r0, [r1, #4]
.L1111:
	add	r2, r2, #1
	b	.L1110
.L1118:
	.align	2
.L1117:
	.word	.LANCHOR1+332
	.word	.LC8
	.word	.LANCHOR0
	.word	-1291
	.word	.LANCHOR0+3456
	.fnend
	.size	Ftl_write_map_blk_to_last_page, .-Ftl_write_map_blk_to_last_page
	.align	2
	.global	FtlMapWritePage
	.syntax unified
	.arm
	.fpu softvfp
	.type	FtlMapWritePage, %function
FtlMapWritePage:
	.fnstart
	@ args = 0, pretend = 0, frame = 8
	@ frame_needed = 0, uses_anonymous_args = 0
	push	{r0, r1, r2, r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.save {r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.pad #12
	mov	r4, r0
	ldr	r5, .L1138
	mov	r7, r1
	mov	r9, r2
	mov	r6, #0
	add	r8, r5, #304
	mov	r10, r8
.L1120:
	ldr	r3, [r5, #2576]
	add	r3, r3, #1
	str	r3, [r5, #2576]
	ldrh	r3, [r8]
	ldrh	r2, [r4, #2]
	sub	r3, r3, #1
	cmp	r2, r3
	bge	.L1121
	ldrh	r2, [r4]
	movw	r3, #65535
	cmp	r2, r3
	bne	.L1122
.L1121:
	mov	r0, r4
	bl	Ftl_write_map_blk_to_last_page
.L1122:
	ldrh	r3, [r4]
	ldr	r2, [r4, #12]
	lsl	r3, r3, #1
	ldrh	r3, [r2, r3]
	cmp	r3, #0
	bne	.L1123
	mov	r2, #700
	ldr	r1, .L1138+4
	ldr	r0, .L1138+8
	bl	sftl_printk
.L1123:
	ldrh	r2, [r4]
	ldrh	r3, [r4, #10]
	cmp	r2, r3
	bcc	.L1124
	movw	r2, #701
	ldr	r1, .L1138+4
	ldr	r0, .L1138+8
	bl	sftl_printk
.L1124:
	ldrh	r3, [r4]
	mov	r1, #0
	ldr	r2, [r4, #12]
	ldr	r0, [r5, #3336]
	lsl	r3, r3, #1
	ldrh	r3, [r2, r3]
	ldrh	r2, [r4, #2]
	str	r9, [r5, #3464]
	str	r3, [sp, #4]
	str	r0, [r5, #3468]
	orr	r2, r2, r3, lsl #10
	str	r2, [r5, #3460]
	mov	r2, #16
	bl	ftl_memset
	ldr	r2, [r4, #28]
	ldr	fp, [r5, #3468]
	ldr	r3, [sp, #4]
	str	r2, [fp, #4]
	strh	r7, [fp, #8]	@ movhi
	ldrh	r2, [r4, #4]
	strh	r3, [fp, #2]	@ movhi
	ldr	r3, .L1138+12
	strh	r2, [fp]	@ movhi
	ldr	r0, [r5, #3464]
	ldrh	r1, [r3]
	bl	js_hash
	mov	r3, #1
	str	r0, [fp, #12]
	mov	r2, r3
	mov	r1, r3
	ldr	r0, .L1138+16
	bl	FlashProgPages
	ldrh	r3, [r4, #2]
	add	r3, r3, #1
	uxth	r3, r3
	strh	r3, [r4, #2]	@ movhi
	ldr	r2, [r5, #3456]
	cmn	r2, #1
	bne	.L1125
	ldr	r1, [r5, #3460]
	add	r6, r6, #1
	ldr	r0, .L1138+20
	uxth	r6, r6
	bl	sftl_printk
	ldrh	r3, [r4, #2]
	cmp	r3, #2
	ldrhls	r3, [r10]
	subls	r3, r3, #1
	strhls	r3, [r4, #2]	@ movhi
	cmp	r6, #3
	bls	.L1127
	mov	r2, r6
	ldr	r1, [r5, #3460]
	ldr	r0, .L1138+24
	bl	sftl_printk
.L1128:
	b	.L1128
.L1127:
	ldr	r3, [r4, #32]
	cmp	r3, #0
	beq	.L1120
.L1136:
	b	.L1136
.L1125:
	cmp	r3, #1
	cmpne	r2, #256
	beq	.L1131
	ldr	r0, [r4, #36]
	cmp	r0, #0
	beq	.L1132
.L1131:
	mov	r3, #0
	str	r3, [r4, #36]
	b	.L1120
.L1132:
	ldr	r2, [r5, #3460]
	ldr	r3, [r4, #24]
	str	r2, [r3, r7, lsl #2]
	add	sp, sp, #12
	@ sp needed
	pop	{r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L1139:
	.align	2
.L1138:
	.word	.LANCHOR0
	.word	.LANCHOR1+363
	.word	.LC8
	.word	.LANCHOR0+310
	.word	.LANCHOR0+3456
	.word	.LC104
	.word	.LC105
	.fnend
	.size	FtlMapWritePage, .-FtlMapWritePage
	.align	2
	.global	load_l2p_region
	.syntax unified
	.arm
	.fpu softvfp
	.type	load_l2p_region, %function
load_l2p_region:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	push	{r4, r5, r6, r7, r8, r9, r10, lr}
	.save {r4, r5, r6, r7, r8, r9, r10, lr}
	mov	r6, r0
	ldr	r9, .L1148
	mov	r10, r1
	ldrh	r3, [r9]
	cmp	r3, r0
	bcs	.L1141
	movw	r2, #485
	ldr	r1, .L1148+4
	ldr	r0, .L1148+8
	bl	sftl_printk
.L1141:
	ldr	r7, .L1148+12
	mov	r5, #12
	ldr	r3, [r7, #3380]
	mov	r4, r7
	ldr	r8, [r3, r6, lsl #2]
	cmp	r8, #0
	bne	.L1142
	mul	r5, r5, r10
	ldr	r3, [r7, #2540]
	movw	r2, #310
	mov	r1, #255
	ldrh	r2, [r7, r2]
	add	r3, r3, r5
	ldr	r0, [r3, #8]
	bl	ftl_memset
	ldr	r3, [r7, #2540]
	strh	r6, [r3, r5]	@ movhi
	ldr	r3, [r7, #2540]
	add	r5, r3, r5
	str	r8, [r5, #4]
.L1143:
	mov	r0, #0
	pop	{r4, r5, r6, r7, r8, r9, r10, pc}
.L1142:
	mul	r5, r5, r10
	ldr	r3, [r7, #2540]
	mov	r2, #1
	add	r0, r7, #3456
	mov	r1, r2
	str	r8, [r7, #3460]
	add	r3, r3, r5
	ldr	r3, [r3, #8]
	str	r3, [r7, #3464]
	ldr	r3, [r7, #3336]
	str	r3, [r7, #3468]
	bl	FlashReadPages
	ldr	r7, [r7, #3468]
	ldrh	r3, [r7, #8]
	cmp	r3, r6
	beq	.L1144
	mov	r2, r8
	mov	r1, r6
	ldr	r0, .L1148+16
	bl	sftl_printk
	mov	r3, #4
	ldr	r1, [r4, #3468]
	mov	r2, r3
	ldr	r0, .L1148+20
	bl	rknand_print_hex
	ldrh	r3, [r9]
	mov	r2, #4
	ldr	r1, [r4, #3380]
	ldr	r0, .L1148+24
	bl	rknand_print_hex
.L1145:
	ldrh	r3, [r7, #8]
	cmp	r3, r6
	beq	.L1146
	mov	r2, #508
	ldr	r1, .L1148+4
	ldr	r0, .L1148+8
	bl	sftl_printk
.L1146:
	ldr	r3, [r4, #2540]
	mov	r1, #0
	add	r2, r3, r5
	str	r1, [r2, #4]
	strh	r6, [r3, r5]	@ movhi
	b	.L1143
.L1144:
	ldr	r3, [r4, #3456]
	cmp	r3, #256
	bne	.L1145
	mov	r2, r8
	mov	r1, r6
	ldr	r0, .L1148+28
	bl	sftl_printk
	ldr	r3, [r4, #2540]
	mov	r1, r6
	ldr	r0, .L1148+32
	add	r3, r3, r5
	ldr	r2, [r3, #8]
	bl	FtlMapWritePage
	b	.L1145
.L1149:
	.align	2
.L1148:
	.word	.LANCHOR0+336
	.word	.LANCHOR1+379
	.word	.LC8
	.word	.LANCHOR0
	.word	.LC106
	.word	.LC94
	.word	.LC107
	.word	.LC108
	.word	.LANCHOR0+3396
	.fnend
	.size	load_l2p_region, .-load_l2p_region
	.align	2
	.global	ftl_map_blk_gc
	.syntax unified
	.arm
	.fpu softvfp
	.type	ftl_map_blk_gc, %function
ftl_map_blk_gc:
	.fnstart
	@ args = 0, pretend = 0, frame = 8
	@ frame_needed = 0, uses_anonymous_args = 0
	push	{r0, r1, r2, r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.save {r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.pad #12
	mov	r4, r0
	ldr	r5, [r0, #12]
	ldr	r10, [r0, #24]
	bl	ftl_free_no_use_map_blk
	ldrh	r3, [r4, #10]
	ldrh	r2, [r4, #8]
	sub	r3, r3, #5
	cmp	r2, r3
	blt	.L1151
	uxth	r0, r0
	lsl	r0, r0, #1
	ldrh	r9, [r5, r0]
	cmp	r9, #0
	beq	.L1151
	ldr	r3, [r4, #32]
	cmp	r3, #0
	bne	.L1151
	mov	r2, #1
	str	r2, [r4, #32]
	strh	r3, [r5, r0]	@ movhi
	ldrh	r3, [r4, #8]
	ldrh	r2, [r4, #2]
	sub	r3, r3, #1
	strh	r3, [r4, #8]	@ movhi
	ldr	r3, .L1167
	ldrh	r3, [r3]
	cmp	r2, r3
	bcc	.L1152
	mov	r0, r4
	bl	ftl_map_blk_alloc_new_blk
.L1152:
	ldr	r5, .L1167+4
	mov	r6, #0
.L1153:
	ldrh	r2, [r4, #6]
	uxth	fp, r6
	cmp	r2, fp
	bhi	.L1160
	mov	r1, #1
	mov	r0, r9
	bl	FtlFreeSysBlkQueueIn
	mov	r3, #0
	str	r3, [r4, #32]
.L1151:
	ldr	r3, .L1167
	ldrh	r2, [r4, #2]
	ldrh	r3, [r3]
	cmp	r2, r3
	bcc	.L1161
	mov	r0, r4
	bl	ftl_map_blk_alloc_new_blk
.L1161:
	mov	r0, #0
	add	sp, sp, #12
	@ sp needed
	pop	{r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L1160:
	uxth	r7, r6
	ldr	r2, [r10, r7, lsl #2]
	add	r3, r10, r7, lsl #2
	str	r3, [sp, #4]
	cmp	r9, r2, lsr #10
	bne	.L1154
	ldr	r2, [r5, #3308]
	ldr	r8, [r5, #3336]
	ldr	r0, .L1167+8
	str	r2, [r5, #3464]
	str	r8, [r5, #3468]
	ldr	r2, [r10, r7, lsl #2]
	str	r2, [r5, #3460]
	mov	r2, #1
	mov	r1, r2
	bl	FlashReadPages
	ldrh	r2, [r8, #8]
	cmp	r2, fp
	beq	.L1155
	movw	r2, #611
	ldr	r1, .L1167+12
	ldr	r0, .L1167+16
	bl	sftl_printk
.L1155:
	ldr	r2, [r5, #3456]
	cmn	r2, #1
	bne	.L1156
.L1158:
	ldr	r2, [sp, #4]
	mov	r3, #0
	str	r3, [r2]
.L1157:
	b	.L1157
.L1156:
	ldrh	r2, [r8, #8]
	cmp	r2, fp
	bne	.L1158
	ldrh	r2, [r8]
	ldrh	r3, [r4, #4]
	cmp	r2, r3
	bne	.L1158
	ldr	r2, [r5, #3464]
	mov	r1, r7
	mov	r0, r4
	bl	FtlMapWritePage
.L1154:
	add	r6, r6, #1
	b	.L1153
.L1168:
	.align	2
.L1167:
	.word	.LANCHOR0+304
	.word	.LANCHOR0
	.word	.LANCHOR0+3456
	.word	.LANCHOR1+395
	.word	.LC8
	.fnend
	.size	ftl_map_blk_gc, .-ftl_map_blk_gc
	.align	2
	.global	flush_l2p_region
	.syntax unified
	.arm
	.fpu softvfp
	.type	flush_l2p_region, %function
flush_l2p_region:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	push	{r4, r5, r6, lr}
	.save {r4, r5, r6, lr}
	mov	r4, #12
	ldr	r5, .L1171
	mul	r4, r4, r0
	ldr	r3, [r5, #2540]
	add	r0, r5, #3392
	add	r0, r0, #4
	add	r2, r3, r4
	ldrh	r1, [r3, r4]
	ldr	r2, [r2, #8]
	bl	FtlMapWritePage
	ldr	r3, [r5, #2540]
	mov	r0, #0
	add	r4, r3, r4
	ldr	r3, [r4, #4]
	bic	r3, r3, #-2147483648
	str	r3, [r4, #4]
	pop	{r4, r5, r6, pc}
.L1172:
	.align	2
.L1171:
	.word	.LANCHOR0
	.fnend
	.size	flush_l2p_region, .-flush_l2p_region
	.align	2
	.global	log2phys
	.syntax unified
	.arm
	.fpu softvfp
	.type	log2phys, %function
log2phys:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	push	{r3, r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.save {r3, r4, r5, r6, r7, r8, r9, r10, fp, lr}
	mov	fp, r0
	ldr	r4, .L1187
	mov	r6, r1
	mov	r10, r2
	add	r3, r4, #308
	ldrh	r9, [r3]
	ldr	r3, [r4, #2556]
	cmp	r0, r3
	bcc	.L1174
	movw	r2, #813
	ldr	r1, .L1187+4
	ldr	r0, .L1187+8
	bl	sftl_printk
.L1174:
	ldr	r3, [r4, #2556]
	cmp	fp, r3
	bcs	.L1175
	add	r9, r9, #7
	ldr	r1, [r4, #2540]
	lsr	r7, fp, r9
	movw	r3, #338
	ldrh	r2, [r4, r3]
	mov	r8, #12
	uxth	r7, r7
	mov	r3, #0
.L1176:
	uxth	r5, r3
	cmp	r5, r2
	bcc	.L1181
	bl	select_l2p_ram_region
	mul	r8, r8, r0
	ldr	r3, [r4, #2540]
	mov	r5, r0
	ldrh	r1, [r3, r8]
	add	r2, r3, r8
	movw	r3, #65535
	cmp	r1, r3
	beq	.L1182
	ldr	r3, [r2, #4]
	cmp	r3, #0
	bge	.L1182
	bl	flush_l2p_region
.L1182:
	mov	r1, r5
	mov	r0, r7
	bl	load_l2p_region
	b	.L1178
.L1175:
	mvn	r0, #0
	cmp	r10, #0
	streq	r0, [r6]
	pop	{r3, r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L1181:
	add	r3, r3, #1
	mla	r0, r8, r3, r1
	ldrh	r0, [r0, #-12]
	cmp	r0, r7
	bne	.L1176
.L1178:
	mvn	r0, #0
	cmp	r10, #0
	bic	r0, fp, r0, lsl r9
	mov	r3, #12
	uxth	r0, r0
	bne	.L1179
	ldr	r2, [r4, #2540]
	mla	r3, r3, r5, r2
	ldr	r3, [r3, #8]
	ldr	r3, [r3, r0, lsl #2]
	str	r3, [r6]
.L1180:
	ldr	r2, [r4, #2540]
	mov	r3, #12
	mov	r0, #0
	mla	r5, r3, r5, r2
	ldr	r3, [r5, #4]
	cmn	r3, #1
	addne	r3, r3, #1
	strne	r3, [r5, #4]
	pop	{r3, r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L1179:
	mul	r3, r3, r5
	ldr	r2, [r4, #2540]
	ldr	r1, [r6]
	add	r2, r2, r3
	ldr	r2, [r2, #8]
	str	r1, [r2, r0, lsl #2]
	ldr	r2, [r4, #2540]
	add	r3, r2, r3
	ldr	r2, [r3, #4]
	orr	r2, r2, #-2147483648
	str	r2, [r3, #4]
	ldr	r3, .L1187+12
	strh	r7, [r3]	@ movhi
	b	.L1180
.L1188:
	.align	2
.L1187:
	.word	.LANCHOR0
	.word	.LANCHOR1+410
	.word	.LC8
	.word	.LANCHOR0+2544
	.fnend
	.size	log2phys, .-log2phys
	.align	2
	.global	FtlWriteDump_data
	.syntax unified
	.arm
	.fpu softvfp
	.type	FtlWriteDump_data, %function
FtlWriteDump_data:
	.fnstart
	@ args = 0, pretend = 0, frame = 32
	@ frame_needed = 0, uses_anonymous_args = 0
	push	{r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.save {r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.pad #36
	sub	sp, sp, #36
	ldr	r4, .L1209
	ldr	r7, .L1209+4
	ldrh	r2, [r4, #28]
	ldr	r3, [r7]
	cmp	r2, #0
	str	r3, [sp, #28]
	beq	.L1190
	ldrb	r3, [r4, #32]	@ zero_extendqisi2
	cmp	r3, #0
	bne	.L1190
	movw	r3, #302
	ldrb	r1, [r4, #31]	@ zero_extendqisi2
	ldrh	r3, [r4, r3]
	mul	r3, r3, r1
	cmp	r2, r3
	beq	.L1190
	ldrb	r8, [r4, #34]	@ zero_extendqisi2
	cmp	r8, #0
	bne	.L1189
	ldr	r6, [r4, #2556]
	mov	r2, r8
	add	r1, sp, #4
	ldrh	r9, [r4, #232]
	sub	r6, r6, #1
	mov	r0, r6
	bl	log2phys
	ldr	r3, [sp, #4]
	ldr	r5, [r4, #3336]
	ldr	r0, [r4, #3304]
	cmn	r3, #1
	str	r3, [sp, #12]
	str	r6, [sp, #24]
	str	r0, [sp, #16]
	str	r5, [sp, #20]
	str	r8, [r5, #4]
	beq	.L1192
	mov	r2, r8
	mov	r1, #1
	add	r0, sp, #8
	bl	FlashReadPages
.L1193:
	ldr	r10, .L1209+8
	mov	r8, #0
	ldr	r3, .L1209+12
	lsl	r9, r9, #2
	mov	fp, r8
	strh	r3, [r5]	@ movhi
.L1194:
	cmp	r9, r8
	bne	.L1198
.L1195:
	mov	r3, #1
.L1208:
	strb	r3, [r4, #34]
.L1189:
	ldr	r2, [sp, #28]
	ldr	r3, [r7]
	cmp	r2, r3
	beq	.L1200
	bl	__stack_chk_fail
.L1192:
	movw	r3, #310
	mov	r1, #255
	ldrh	r2, [r4, r3]
	bl	ftl_memset
	b	.L1193
.L1198:
	ldrh	r3, [r4, #28]
	cmp	r3, #0
	beq	.L1195
	ldr	r3, [sp, #12]
	mov	r0, r10
	str	r6, [r5, #8]
	add	r8, r8, #1
	str	r3, [r5, #12]
	ldrh	r3, [r4, #24]
	strh	r3, [r5, #2]	@ movhi
	bl	get_new_active_ppa
	ldr	r3, [r4, #2596]
	mov	r1, #1
	str	r0, [sp, #12]
	add	r0, sp, #8
	str	r3, [r5, #4]
	add	r3, r3, #1
	cmn	r3, #1
	moveq	r3, fp
	str	r3, [r4, #2596]
	mov	r3, #0
	mov	r2, r3
	bl	FlashProgPages
	ldrh	r0, [r4, #24]
	bl	decrement_vpc_count
	b	.L1194
.L1190:
	mov	r3, #0
	b	.L1208
.L1200:
	add	sp, sp, #36
	@ sp needed
	pop	{r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L1210:
	.align	2
.L1209:
	.word	.LANCHOR0
	.word	__stack_chk_guard
	.word	.LANCHOR0+24
	.word	-3947
	.fnend
	.size	FtlWriteDump_data, .-FtlWriteDump_data
	.align	2
	.global	FtlReUsePrevPpa
	.syntax unified
	.arm
	.fpu softvfp
	.type	FtlReUsePrevPpa, %function
FtlReUsePrevPpa:
	.fnstart
	@ args = 0, pretend = 0, frame = 8
	@ frame_needed = 0, uses_anonymous_args = 0
	push	{r0, r1, r4, r5, r6, r7, r8, lr}
	.save {r4, r5, r6, r7, r8, lr}
	.pad #8
	mov	r7, r0
	ldr	r5, .L1222
	ubfx	r0, r1, #10, #16
	str	r1, [sp, #4]
	bl	P2V_block_in_plane
	ldr	r2, [r5, #72]
	lsl	r6, r0, #1
	ldrh	r3, [r2, r6]
	cmp	r3, #0
	bne	.L1212
	ldr	r4, [r5, #2532]
	cmp	r4, #0
	beq	.L1213
	ldr	r1, [r5, #2516]
	mov	lr, #6
	ldr	r2, .L1222+4
	movw	r8, #65535
	ldrh	ip, [r5, #224]
	sub	r4, r4, r1
	asr	r4, r4, #1
	mul	r4, r2, r4
	uxth	r4, r4
.L1214:
	uxth	r2, r3
	cmp	ip, r2
	bls	.L1213
	cmp	r4, r0
	bne	.L1215
	mov	r1, r4
	ldr	r0, .L1222+8
	bl	List_remove_node
	ldrh	r3, [r5, #224]
	cmp	r3, #0
	bne	.L1216
	movw	r2, #1735
	ldr	r1, .L1222+12
	ldr	r0, .L1222+16
	bl	sftl_printk
.L1216:
	ldrh	r3, [r5, #224]
	mov	r0, r4
	sub	r3, r3, #1
	strh	r3, [r5, #224]	@ movhi
	bl	INSERT_DATA_LIST
	ldr	r2, [r5, #72]
	ldrh	r3, [r2, r6]
.L1212:
	add	r3, r3, #1
	strh	r3, [r2, r6]	@ movhi
	b	.L1213
.L1215:
	mul	r4, lr, r4
	add	r3, r3, #1
	ldrh	r4, [r1, r4]
	cmp	r4, r8
	bne	.L1214
.L1213:
	mov	r2, #1
	add	r1, sp, #4
	mov	r0, r7
	bl	log2phys
	add	sp, sp, #8
	@ sp needed
	pop	{r4, r5, r6, r7, r8, pc}
.L1223:
	.align	2
.L1222:
	.word	.LANCHOR0
	.word	-1431655765
	.word	.LANCHOR0+2532
	.word	.LANCHOR1+419
	.word	.LC8
	.fnend
	.size	FtlReUsePrevPpa, .-FtlReUsePrevPpa
	.align	2
	.global	FtlRecoverySuperblock
	.syntax unified
	.arm
	.fpu softvfp
	.type	FtlRecoverySuperblock, %function
FtlRecoverySuperblock:
	.fnstart
	@ args = 0, pretend = 0, frame = 48
	@ frame_needed = 0, uses_anonymous_args = 0
	ldr	r3, .L1358
	movw	r2, #65535
	push	{r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.save {r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.pad #52
	sub	sp, sp, #52
	ldr	r3, [r3]
	str	r3, [sp, #44]
	ldrh	r3, [r0]
	cmp	r3, r2
	beq	.L1226
	ldrh	r3, [r0, #2]
	mov	fp, r0
	ldr	r4, .L1358+4
	str	r3, [sp, #12]
	ldrb	r3, [r0, #6]	@ zero_extendqisi2
	ldr	r1, [sp, #12]
	str	r3, [sp, #28]
	movw	r3, #302
	ldrh	r3, [r4, r3]
	cmp	r3, r1
	mov	r3, #0
	strheq	r3, [r0, #4]	@ movhi
	ldrhne	r0, [r0, #16]
	bne	.L1228
.L1356:
	strb	r3, [fp, #6]
.L1226:
	ldr	r3, .L1358
	mov	r0, #0
	ldr	r2, [sp, #44]
	ldr	r3, [r3]
	cmp	r2, r3
	beq	.L1294
	bl	__stack_chk_fail
.L1229:
	uxth	r1, r3
	add	r1, fp, r1, lsl #1
	ldrh	r0, [r1, #16]
.L1228:
	cmp	r0, r2
	uxth	r6, r3
	add	r3, r3, #1
	beq	.L1229
	mov	r1, #1
	bl	FtlGetLastWrittenPage
	cmn	r0, #1
	mov	r5, r0
	beq	.L1230
	ldrh	r3, [r4, #232]
	mov	r2, #0
	ldr	r0, [r4, #3292]
	mov	r6, r2
	movw	r8, #65535
	mov	r9, #20
	str	r3, [sp, #4]
	ldr	r3, [r4, #3180]
	str	r3, [sp, #8]
	movw	r3, #310
	ldrh	lr, [r4, r3]
	ldr	r3, .L1358+8
	ldr	r4, [r4, #3184]
	ldrh	r7, [r3]
	add	r3, fp, #16
	mov	ip, r3
	str	r3, [sp, #24]
.L1231:
	ldr	r1, [sp, #4]
	uxth	r3, r2
	cmp	r3, r1
	bcc	.L1235
	ldr	r4, .L1358+4
	mov	r2, #0
	mov	r1, r6
	movw	r9, #65535
	bl	FlashReadPages
	ldr	r3, [r4, #2596]
	uxth	r2, r5
	ldr	r8, [r4, #3292]
	str	r2, [sp, #8]
	sub	r3, r3, #1
	str	r3, [sp, #4]
	mov	r7, r8
	mov	r3, #0
.L1236:
	uxth	r2, r3
	cmp	r6, r2
	bhi	.L1241
	addeq	r3, r5, #1
	uxtheq	r3, r3
	streq	r3, [sp, #8]
	bne	.L1239
.L1351:
	ldr	r0, [r8, #4]
	ubfx	r0, r0, #10, #16
	bl	P2V_plane
	movw	r3, #302
	ldr	r2, [sp, #8]
	ldrh	r3, [r4, r3]
	str	r0, [sp, #16]
	ldr	r1, [sp, #16]
	cmp	r3, r2
	ldr	r0, [sp, #28]
	ldrheq	r3, [sp, #8]
	ldr	r2, [sp, #12]
	strheq	r3, [fp, #2]	@ movhi
	moveq	r3, #0
	strbeq	r3, [fp, #6]
	strheq	r3, [fp, #4]	@ movhi
	ldr	r3, [sp, #8]
	cmp	r3, r2
	cmpeq	r1, r0
	moveq	r2, r1
	moveq	r1, r3
	beq	.L1357
	movw	r3, #65535
	cmp	r9, r3
	bne	.L1245
	ldrb	r3, [fp, #8]	@ zero_extendqisi2
	cmp	r3, #0
	bne	.L1246
.L1245:
	ldr	r3, [r4, #3452]
	uxth	r8, r5
	uxth	r5, r5
	ldr	r7, .L1358+4
	movw	r9, #65535
	cmn	r3, #1
	ldreq	r3, [sp, #4]
	streq	r3, [r4, #3452]
	ldr	r3, [sp, #12]
	add	r3, r3, #7
	cmp	r5, r3
	subgt	r5, r8, #7
	ldrle	r5, [sp, #12]
	uxthgt	r5, r5
.L1249:
	cmp	r5, r8
	bhi	.L1256
	mov	r3, #0
	ldrh	lr, [r7, #232]
	ldr	r0, [r7, #3292]
	mov	r6, r3
	ldr	r1, [sp, #24]
	mov	ip, #20
	b	.L1257
.L1230:
	ldr	r3, [sp, #12]
	cmp	r3, #0
	beq	.L1232
	movw	r2, #1802
	ldr	r1, .L1358+12
	ldr	r0, .L1358+16
	bl	sftl_printk
.L1232:
	ldr	r3, [sp, #28]
	cmp	r3, #0
	cmpne	r6, r3
	beq	.L1233
	movw	r2, #1803
	ldr	r1, .L1358+12
	ldr	r0, .L1358+16
	bl	sftl_printk
.L1233:
	mov	r3, #0
	strh	r3, [fp, #2]	@ movhi
	b	.L1356
.L1235:
	ldrh	r3, [ip], #2
	cmp	r3, r8
	beq	.L1234
	mla	r1, r9, r6, r0
	orr	r3, r5, r3, lsl #10
	str	r3, [r1, #4]
	mul	r3, lr, r6
	add	r10, r3, #3
	cmp	r3, #0
	movlt	r3, r10
	ldr	r10, [sp, #8]
	bic	r3, r3, #3
	add	r3, r10, r3
	str	r3, [r1, #8]
	mul	r3, r7, r6
	add	r6, r6, #1
	uxth	r6, r6
	add	r10, r3, #3
	cmp	r3, #0
	movlt	r3, r10
	bic	r3, r3, #3
	add	r3, r4, r3
	str	r3, [r1, #12]
.L1234:
	add	r2, r2, #1
	b	.L1231
.L1241:
	ldr	r2, [r7]
	cmp	r2, #0
	bne	.L1237
	ldr	r10, [r7, #12]
	ldr	r2, [r10, #4]
	cmn	r2, #1
	beq	.L1238
	ldr	r1, [r4, #2596]
	mov	r0, r2
	bl	ftl_cmp_data_ver
	cmp	r0, #0
	addne	r2, r2, #1
	strne	r2, [r4, #2596]
.L1238:
	ldr	r2, [r10]
	cmn	r2, #1
	bne	.L1240
.L1239:
	uxth	r2, r5
	uxth	r3, r3
	str	r2, [sp, #8]
	mov	r2, #20
	mla	r8, r2, r3, r8
	b	.L1351
.L1237:
	ldr	r9, [sp, #8]
.L1240:
	add	r3, r3, #1
	add	r7, r7, #20
	b	.L1236
.L1251:
	ldrh	r2, [r1], #2
	add	r3, r3, #1
	cmp	r2, r9
	mlane	r10, ip, r6, r0
	addne	r6, r6, #1
	orrne	r2, r5, r2, lsl #10
	uxthne	r6, r6
	strne	r2, [r10, #4]
.L1257:
	uxth	r2, r3
	cmp	lr, r2
	bhi	.L1251
	mov	r2, #0
	mov	r1, r6
	bl	FlashReadPages
	ldr	r3, [r7, #3292]
	mov	r2, #20
	mla	r6, r2, r6, r3
.L1252:
	cmp	r6, r3
	addeq	r5, r5, #1
	uxtheq	r5, r5
	beq	.L1249
.L1255:
	ldr	r2, [r3]
	cmp	r2, #0
	bne	.L1246
	ldr	r2, [r3, #12]
	ldrh	r1, [r2]
	cmp	r1, r9
	beq	.L1254
	ldr	r2, [r2, #4]
	cmn	r2, #1
	strne	r2, [r7, #3452]
.L1254:
	add	r3, r3, #20
	b	.L1252
.L1256:
	mvn	r3, #0
	str	r3, [r7, #3452]
.L1246:
	mov	r2, #1
	movw	r3, #3476
	strh	r2, [r4, r3]	@ movhi
	ldr	r9, [sp, #12]
	ldr	r4, .L1358+4
.L1258:
	ldrh	ip, [r4, #232]
	mov	r3, #0
	ldr	r0, [r4, #3292]
	mov	r10, r3
	ldr	r1, [sp, #24]
	movw	lr, #65535
	mov	r5, #20
.L1259:
	uxth	r2, r3
	cmp	ip, r2
	bhi	.L1261
	mov	r2, #0
	mov	r1, r10
	bl	FlashReadPages
	mov	r3, #0
.L1355:
	str	r3, [sp, #20]
	ldrh	r3, [sp, #20]
	cmp	r10, r3
	bhi	.L1289
	ldr	r3, .L1358+20
	add	r9, r9, #1
	uxth	r9, r9
	ldrh	r3, [r3]
	cmp	r3, r9
	bne	.L1258
	ldrh	r2, [r4, #232]
	movw	r0, #65535
	mov	r3, #0
	strh	r9, [fp, #2]	@ movhi
	strh	r3, [fp, #4]	@ movhi
.L1290:
	uxth	r1, r3
	cmp	r1, r2
	bcs	.L1226
	ldr	r1, [sp, #24]
	ldrh	ip, [r1], #2
	cmp	ip, r0
	str	r1, [sp, #24]
	add	r1, r3, #1
	bne	.L1356
	mov	r3, r1
	b	.L1290
.L1261:
	ldrh	r2, [r1], #2
	add	r3, r3, #1
	cmp	r2, lr
	mlane	r6, r5, r10, r0
	orrne	r2, r9, r2, lsl #10
	addne	r10, r10, #1
	uxthne	r10, r10
	strne	r2, [r6, #4]
	b	.L1259
.L1289:
	ldr	r3, [sp, #20]
	mov	r6, #20
	ldr	r8, [r4, #3292]
	mul	r6, r6, r3
	add	r7, r8, r6
	ldr	r5, [r7, #4]
	ubfx	r0, r5, #10, #16
	str	r5, [sp, #40]
	bl	P2V_plane
	ldr	r3, [sp, #12]
	cmp	r9, r3
	bcc	.L1263
	ldr	r2, [sp, #28]
	moveq	r3, #1
	movne	r3, #0
	cmp	r2, r0
	movls	r3, #0
	andhi	r3, r3, #1
	cmp	r3, #0
	bne	.L1263
	ldr	r3, [sp, #8]
	ldr	r2, [sp, #16]
	cmp	r9, r3
	cmpeq	r2, r0
	beq	.L1264
	ldr	r3, [r8, r6]
	cmn	r3, #1
	beq	.L1265
	ldr	r7, [r7, #12]
	movw	r3, #61589
	ldrh	r2, [r7]
	cmp	r2, r3
	ldrhne	r0, [fp]
	bne	.L1352
	ldr	r3, [r7, #4]
	cmn	r3, #1
	str	r3, [sp, #4]
	beq	.L1267
	mov	r0, r3
	ldr	r1, [r4, #2596]
	bl	ftl_cmp_data_ver
	cmp	r0, #0
	ldrne	r3, [sp, #4]
	addne	r3, r3, #1
	strne	r3, [r4, #2596]
.L1267:
	ldrh	r2, [r7]
	movw	r3, #61589
	cmp	r2, r3
	beq	.L1268
	movw	r2, #1954
	ldr	r1, .L1358+12
	ldr	r0, .L1358+16
	bl	sftl_printk
.L1268:
	ldr	r5, [r7, #8]
	add	r1, sp, #36
	ldr	r3, [r7, #12]
	mov	r2, #0
	mov	r0, r5
	str	r3, [sp, #32]
	bl	log2phys
	ldr	r1, [r4, #3452]
	cmn	r1, #1
	beq	.L1269
	ldr	r0, [sp, #4]
	bl	ftl_cmp_data_ver
	cmp	r0, #0
	beq	.L1269
	ldr	r3, [sp, #32]
	cmn	r3, #1
	beq	.L1270
	ldr	r0, [r4, #3292]
	mov	r2, #0
	mov	r1, #1
	add	r0, r0, r6
	str	r3, [r0, #4]
	ldr	r7, [r0, #12]
	bl	FlashReadPages
	ldr	r2, [r4, #3292]
	ldr	r1, [r2, r6]
	add	r3, r2, r6
	cmn	r1, #1
	bne	.L1271
.L1272:
	mvn	r3, #0
	str	r3, [sp, #32]
.L1279:
	ldr	r7, [sp, #32]
	cmn	r7, #1
	beq	.L1263
.L1293:
	ubfx	r0, r7, #10, #16
	bl	P2V_block_in_plane
	ldrh	r3, [r4, #240]
	mov	r5, r0
	cmp	r3, r0
	bhi	.L1285
	movw	r2, #2059
	ldr	r1, .L1358+12
	ldr	r0, .L1358+16
	bl	sftl_printk
.L1285:
	ldr	r2, [r4, #72]
	lsl	r3, r5, #1
	ldrh	r3, [r2, r3]
	cmp	r3, #0
	beq	.L1286
	mov	r0, r5
.L1352:
	bl	decrement_vpc_count
	b	.L1263
.L1270:
	ldr	r3, [sp, #40]
	ldr	r2, [sp, #36]
	cmp	r2, r3
	bne	.L1263
	mov	r2, #1
	add	r1, sp, #32
	mov	r0, r5
	bl	log2phys
.L1263:
	ldr	r3, [sp, #20]
	add	r3, r3, #1
	b	.L1355
.L1271:
	ldr	r1, [r7, #8]
	cmp	r5, r1
	bne	.L1272
	ldr	r8, [r7, #4]
	ldr	r0, [r4, #3452]
	mov	r1, r8
	bl	ftl_cmp_data_ver
	cmp	r0, #0
	beq	.L1272
	ldr	r1, [sp, #36]
	ldr	r0, [sp, #40]
	cmp	r1, r0
	bne	.L1274
.L1277:
	ldr	r1, [sp, #32]
	mov	r0, r5
	bl	FtlReUsePrevPpa
	b	.L1272
.L1274:
	ldr	r0, [sp, #32]
	cmp	r1, r0
	beq	.L1272
	cmn	r1, #1
	streq	r1, [r2, r6]
	beq	.L1276
	str	r1, [r3, #4]
	mov	r2, #0
	mov	r1, #1
	mov	r0, r3
	ldr	r7, [r3, #12]
	bl	FlashReadPages
.L1276:
	ldr	r3, [r4, #3292]
	ldr	r3, [r3, r6]
	cmn	r3, #1
	beq	.L1277
	ldr	r3, [r7, #4]
	ldr	r0, [r4, #3452]
	mov	r1, r3
	bl	ftl_cmp_data_ver
	cmp	r0, #0
	beq	.L1277
	mov	r1, r3
	mov	r0, r8
	bl	ftl_cmp_data_ver
	cmp	r0, #0
	beq	.L1272
	b	.L1277
.L1269:
	ldr	r3, [sp, #40]
	ldr	r2, [sp, #36]
	cmp	r2, r3
	beq	.L1279
	ldr	r1, [sp, #32]
	cmn	r1, #1
	beq	.L1281
	ldr	r3, [r4, #248]
	cmp	r3, r1, lsr #10
	ldrls	r0, .L1358+24
	bls	.L1354
.L1281:
	mov	r2, #1
	add	r1, sp, #40
	mov	r0, r5
	bl	log2phys
	ldr	r7, [sp, #36]
	cmn	r7, #1
	beq	.L1279
	ldr	r3, [sp, #32]
	cmp	r7, r3
	beq	.L1293
	ubfx	r0, r7, #10, #16
	bl	P2V_block_in_plane
	ldrh	r3, [r4, #24]
	cmp	r3, r0
	beq	.L1284
	ldrh	r3, [r4, #76]
	cmp	r3, r0
	beq	.L1284
	ldrh	r3, [r4, #124]
	cmp	r3, r0
	bne	.L1279
.L1284:
	ldr	r0, [r4, #3292]
	mov	r2, #0
	mov	r1, #1
	str	r7, [r0, #4]
	ldr	r6, [r0, #12]
	bl	FlashReadPages
	ldr	r3, [r4, #3292]
	ldr	r3, [r3]
	cmn	r3, #1
	beq	.L1279
	ldr	r1, [r6, #4]
	ldr	r0, [sp, #4]
	bl	ftl_cmp_data_ver
	cmp	r0, #0
	bne	.L1279
	mov	r2, #1
	add	r1, sp, #36
	mov	r0, r5
	bl	log2phys
	b	.L1279
.L1286:
	ldr	r0, .L1358+28
	mov	r1, r5
.L1354:
	bl	sftl_printk
	b	.L1263
.L1265:
	ldr	r3, [r4, #3480]
	cmp	r3, #31
	addls	r2, r4, r3, lsl #2
	addls	r3, r3, #1
	strls	r3, [r4, #3480]
	strls	r5, [r2, #3484]
	ldrh	r0, [fp]
	bl	decrement_vpc_count
	ldr	r3, [r4, #3452]
	cmn	r3, #1
	ldreq	r3, [sp, #4]
	beq	.L1353
	ldr	r2, [sp, #4]
	cmp	r2, r3
	bcs	.L1263
	mov	r3, r2
.L1353:
	str	r3, [r4, #3452]
	b	.L1263
.L1264:
	ldrb	r3, [sp, #16]	@ zero_extendqisi2
	ldr	r2, [sp, #16]
	ldr	r1, [sp, #8]
	strb	r3, [fp, #6]
	ldrh	r3, [sp, #8]
	strh	r3, [fp, #2]	@ movhi
.L1357:
	mov	r0, fp
	bl	ftl_sb_update_avl_pages
	b	.L1226
.L1294:
	add	sp, sp, #52
	@ sp needed
	pop	{r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L1359:
	.align	2
.L1358:
	.word	__stack_chk_guard
	.word	.LANCHOR0
	.word	.LANCHOR0+312
	.word	.LANCHOR1+435
	.word	.LC8
	.word	.LANCHOR0+302
	.word	.LC109
	.word	.LC110
	.fnend
	.size	FtlRecoverySuperblock, .-FtlRecoverySuperblock
	.align	2
	.global	ftl_check_vpc
	.syntax unified
	.arm
	.fpu softvfp
	.type	ftl_check_vpc, %function
ftl_check_vpc:
	.fnstart
	@ args = 0, pretend = 0, frame = 16
	@ frame_needed = 0, uses_anonymous_args = 0
	push	{r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.save {r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.pad #20
	sub	sp, sp, #20
	ldr	r5, .L1386
	mov	r4, #0
	ldr	r6, .L1386+4
	ldr	r3, [r5]
	ldr	r1, .L1386+8
	ldr	r0, .L1386+12
	ldr	r7, .L1386+16
	str	r3, [sp, #12]
	bl	sftl_printk
	mov	r2, #8192
	mov	r1, #0
	ldr	r0, .L1386+16
	bl	ftl_memset
	str	r5, [sp, #4]
	mov	r5, r6
.L1361:
	ldr	r3, [r6, #2556]
	cmp	r4, r3
	bcc	.L1363
	ldr	r8, .L1386+16
	mov	r4, #0
	ldr	r10, .L1386+20
	mov	r6, r4
	movw	r9, #65535
.L1364:
	ldrh	r2, [r5, #240]
	uxth	r3, r4
	cmp	r2, r3
	bhi	.L1366
	ldr	r4, [r5, #2532]
	cmp	r4, #0
	beq	.L1367
	ldr	r3, [r5, #2516]
	mov	r7, #0
	ldrh	r8, [r5, #224]
	mov	fp, #6
	ldr	r9, .L1386+16
	sub	r4, r4, r3
	ldr	r3, .L1386+24
	asr	r4, r4, #1
	ldr	r10, .L1386+28
	mul	r4, r3, r4
	uxth	r4, r4
.L1368:
	uxth	r3, r7
	cmp	r8, r3
	bls	.L1367
	ldr	r2, [r5, #72]
	lsl	r3, r4, #1
	ldrh	r2, [r2, r3]
	cmp	r2, #0
	beq	.L1369
	mov	r6, #1
	ldrh	r3, [r9, r3]
	mov	r1, r4
	mov	r0, r10
	bl	sftl_printk
.L1369:
	mul	r4, fp, r4
	ldr	r3, [r5, #2516]
	add	r7, r7, #1
	ldrh	r4, [r3, r4]
	movw	r3, #65535
	cmp	r4, r3
	bne	.L1368
.L1367:
	cmp	r6, #0
	beq	.L1360
	movw	r2, #2387
	ldr	r1, .L1386+8
	ldr	r0, .L1386+32
	bl	sftl_printk
.L1360:
	ldr	r3, [sp, #4]
	ldr	r2, [sp, #12]
	ldr	r3, [r3]
	cmp	r2, r3
	beq	.L1372
	bl	__stack_chk_fail
.L1363:
	mov	r2, #0
	add	r1, sp, #8
	mov	r0, r4
	bl	log2phys
	ldr	r0, [sp, #8]
	cmn	r0, #1
	beq	.L1362
	ubfx	r0, r0, #10, #16
	bl	P2V_block_in_plane
	lsl	r0, r0, #1
	ldrh	r3, [r7, r0]
	add	r3, r3, #1
	strh	r3, [r7, r0]	@ movhi
.L1362:
	add	r4, r4, #1
	b	.L1361
.L1366:
	uxth	r1, r4
	ldr	r3, [r5, #72]
	lsl	r7, r1, #1
	ldrh	r2, [r3, r7]
	ldrh	r3, [r8, r7]
	cmp	r2, r3
	beq	.L1365
	mov	r0, r10
	bl	sftl_printk
	ldr	r3, [r5, #72]
	ldrh	r3, [r3, r7]
	cmp	r3, r9
	beq	.L1365
	ldrh	r2, [r8, r7]
	cmp	r2, r3
	movhi	r6, #1
.L1365:
	add	r4, r4, #1
	b	.L1364
.L1372:
	add	sp, sp, #20
	@ sp needed
	pop	{r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L1387:
	.align	2
.L1386:
	.word	__stack_chk_guard
	.word	.LANCHOR0
	.word	.LANCHOR1+457
	.word	.LC111
	.word	check_vpc_table
	.word	.LC112
	.word	-1431655765
	.word	.LC113
	.word	.LC8
	.fnend
	.size	ftl_check_vpc, .-ftl_check_vpc
	.align	2
	.global	ftl_scan_all_data
	.syntax unified
	.arm
	.fpu softvfp
	.type	ftl_scan_all_data, %function
ftl_scan_all_data:
	.fnstart
	@ args = 0, pretend = 0, frame = 8
	@ frame_needed = 0, uses_anonymous_args = 0
	push	{r4, r5, r6, r7, r8, r9, lr}
	.save {r4, r5, r6, r7, r8, r9, lr}
	mov	r5, #0
	ldr	r6, .L1398
	.pad #36
	sub	sp, sp, #36
	mov	r1, #0
	ldr	r7, .L1398+4
	ldr	r9, .L1398+8
	ldr	r3, [r6]
	mov	r4, r7
	ldr	r0, .L1398+12
	str	r3, [sp, #28]
	bl	sftl_printk
.L1389:
	ldr	r3, [r7, #2556]
	cmp	r5, r3
	bcc	.L1395
	ldr	r2, [sp, #28]
	ldr	r3, [r6]
	cmp	r2, r3
	beq	.L1396
	bl	__stack_chk_fail
.L1395:
	mov	r2, #0
	add	r1, sp, #24
	mov	r0, r5
	bl	log2phys
	ubfx	r3, r5, #0, #11
	cmp	r3, #0
	bne	.L1390
	ldr	r2, [sp, #24]
	mov	r1, r5
	mov	r0, r9
	bl	sftl_printk
.L1390:
	ldr	r3, [sp, #24]
	cmn	r3, #1
	beq	.L1392
	str	r3, [r4, #3460]
	mov	r2, #0
	ldr	r3, [r4, #3304]
	mov	r1, #1
	ldr	r8, [r4, #3336]
	ldr	r0, .L1398+16
	str	r3, [r4, #3464]
	str	r5, [r4, #3472]
	str	r8, [r4, #3468]
	str	r2, [r4, #3456]
	bl	FlashReadPages
	ldr	r3, [r4, #3456]
	cmn	r3, #1
	cmpne	r3, #256
	beq	.L1393
	ldr	r3, [r8, #8]
	cmp	r5, r3
	beq	.L1392
.L1393:
	ldr	r2, [r4, #3464]
	ldr	r3, [r4, #3468]
	ldr	r0, .L1398+20
	ldr	r1, [r2, #4]
	str	r1, [sp, #16]
	mov	r1, r5
	ldr	r2, [r2]
	str	r2, [sp, #12]
	ldr	r2, [r3, #12]
	str	r2, [sp, #8]
	ldr	r2, [r3, #8]
	str	r2, [sp, #4]
	ldr	r2, [r3, #4]
	str	r2, [sp]
	ldr	r3, [r3]
	ldr	r2, [r4, #3460]
	bl	sftl_printk
.L1392:
	add	r5, r5, #1
	b	.L1389
.L1396:
	add	sp, sp, #36
	@ sp needed
	pop	{r4, r5, r6, r7, r8, r9, pc}
.L1399:
	.align	2
.L1398:
	.word	__stack_chk_guard
	.word	.LANCHOR0
	.word	.LC115
	.word	.LC114
	.word	.LANCHOR0+3456
	.word	.LC116
	.fnend
	.size	ftl_scan_all_data, .-ftl_scan_all_data
	.align	2
	.global	FtlGcScanTempBlk
	.syntax unified
	.arm
	.fpu softvfp
	.type	FtlGcScanTempBlk, %function
FtlGcScanTempBlk:
	.fnstart
	@ args = 0, pretend = 0, frame = 56
	@ frame_needed = 0, uses_anonymous_args = 0
	ldr	r3, .L1434
	push	{r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.save {r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.pad #60
	sub	sp, sp, #60
	mov	r4, r0
	str	r1, [sp, #12]
	ldr	r3, [r3]
	str	r3, [sp, #52]
	ldr	r3, .L1434+4
	ldrh	r5, [r3, #4]
	movw	r3, #65535
	cmp	r5, r3
	beq	.L1426
	cmp	r5, #0
	bne	.L1401
.L1402:
	bl	FtlGcPageVarInit
	b	.L1403
.L1426:
	mov	r5, #0
.L1401:
	ldr	r2, .L1434+8
	movw	r3, #302
	ldrh	r3, [r2, r3]
	ldr	r2, [sp, #12]
	cmp	r3, r2
	beq	.L1402
.L1403:
	ldr	r6, .L1434+8
	mov	r3, #0
	str	r3, [sp, #4]
.L1419:
	ldrh	r2, [r4]
	mov	r3, #0
	strb	r3, [r4, #8]
	movw	r3, #65535
	cmp	r2, r3
	beq	.L1404
.L1422:
	ldrh	r3, [r6, #232]
	mov	r2, #0
	ldr	r0, [r6, #3292]
	add	ip, r4, #16
	ldr	fp, [r6, #3184]
	mov	r8, r2
	str	r3, [sp, #8]
	movw	r7, #65535
	ldr	r3, [r6, #3180]
	mov	r9, #20
	str	r3, [sp, #16]
	ldr	r3, .L1434+12
	ldrh	r3, [r3]
	str	r3, [sp, #20]
	ldr	r3, .L1434+16
	ldrh	lr, [r3]
.L1405:
	ldr	r1, [sp, #8]
	uxth	r3, r2
	cmp	r1, r3
	bhi	.L1407
	mov	r10, #0
	mov	r2, #0
	mov	r1, r8
	bl	FlashReadPages
.L1408:
	uxth	r3, r10
	cmp	r8, r3
	bhi	.L1420
	ldr	r3, [sp, #4]
	add	r5, r5, #1
	uxth	r5, r5
	add	r3, r3, #1
	str	r3, [sp, #4]
	ldr	r2, [sp, #4]
	ldr	r3, [sp, #12]
	cmp	r3, r2
	ldr	r2, .L1434+20
	bls	.L1421
.L1423:
	ldrh	r3, [r2]
	cmp	r3, r5
	bhi	.L1422
.L1404:
	ldr	r3, .L1434+4
	mvn	r2, #0
	strh	r5, [r4, #2]	@ movhi
	mov	r1, r5
	mov	r0, r4
	strh	r2, [r3, #4]	@ movhi
	mov	r2, #0
	strb	r2, [r4, #6]
	bl	ftl_sb_update_avl_pages
	b	.L1424
.L1407:
	ldrh	r3, [ip], #2
	cmp	r3, r7
	beq	.L1406
	mla	r1, r9, r8, r0
	orr	r3, r5, r3, lsl #10
	str	r3, [r1, #4]
	ldr	r3, [sp, #20]
	mul	r3, r3, r8
	add	r10, r3, #3
	cmp	r3, #0
	movlt	r3, r10
	ldr	r10, [sp, #16]
	bic	r3, r3, #3
	add	r3, r10, r3
	str	r3, [r1, #8]
	mul	r3, lr, r8
	add	r8, r8, #1
	uxth	r8, r8
	add	r10, r3, #3
	cmp	r3, #0
	movlt	r3, r10
	bic	r3, r3, #3
	add	r3, fp, r3
	str	r3, [r1, #12]
.L1406:
	add	r2, r2, #1
	b	.L1405
.L1420:
	mov	r3, #20
	ldr	r2, [r6, #3292]
	mul	fp, r3, r10
	ldr	r7, [r2, fp]
	add	r1, r2, fp
	ldr	r3, [r1, #4]
	ldr	r9, [r1, #12]
	cmp	r7, #0
	str	r3, [sp, #8]
	bne	.L1409
	ldrh	r1, [r9]
	movw	r2, #65535
	cmp	r1, r2
	bne	.L1410
.L1432:
	ldrh	r3, [r4]
	mov	r1, #0
	ldr	r2, [r6, #72]
	mov	r5, #0
	lsl	r3, r3, #1
	strh	r1, [r2, r3]	@ movhi
	ldrh	r0, [r4]
	bl	INSERT_FREE_LIST
	mvn	r3, #0
	strh	r3, [r4]	@ movhi
	strh	r3, [r6, #172]	@ movhi
	bl	FtlGcPageVarInit
	b	.L1419
.L1410:
	ldr	r0, [r9, #8]
	ldr	r2, [r6, #2556]
	cmp	r0, r2
	bhi	.L1432
	mov	r2, r7
	add	r1, sp, #28
	bl	log2phys
	ldr	r2, [r9, #12]
	ldr	r1, [sp, #28]
	sub	r0, r2, r1
	cmn	r1, #1
	clz	r0, r0
	lsr	r0, r0, #5
	moveq	r0, #0
	cmp	r0, #0
	bne	.L1413
.L1418:
	ldr	r2, [r9, #8]
.L1433:
	ldr	r1, [sp, #8]
	add	r10, r10, #1
	ldr	r0, [r9, #12]
	bl	FtlGcUpdatePage
	b	.L1408
.L1413:
	str	r2, [sp, #36]
	mov	r1, #1
	ldr	r2, [r6, #3320]
	add	r0, sp, #32
	str	r2, [sp, #40]
	ldr	r2, [r6, #3340]
	str	r2, [sp, #44]
	mov	r2, r7
	bl	FlashReadPages
	ldr	r3, .L1434+24
	ldr	r1, [r6, #3292]
	ldrh	r2, [r3]
	add	r3, r1, fp
	ldr	r1, [sp, #40]
	lsl	r2, r2, #7
.L1415:
	cmp	r7, r2
	beq	.L1418
	ldr	r0, [r3, #8]
	ldr	ip, [r0, r7, lsl #2]
	ldr	r0, [r1, r7, lsl #2]
	cmp	ip, r0
	beq	.L1416
	ldr	r2, [sp, #36]
	ldrh	r1, [r4]
	ldr	r0, .L1434+28
	bl	sftl_printk
	b	.L1432
.L1416:
	add	r7, r7, #1
	b	.L1415
.L1409:
	mvn	r2, #0
	b	.L1433
.L1421:
	ldr	r1, .L1434+4
	movw	r0, #65535
	ldrh	r3, [r1, #4]
	cmp	r3, r0
	beq	.L1423
	ldr	r0, [sp, #4]
	add	r3, r3, r0
	strh	r3, [r1, #4]	@ movhi
	ldrh	r3, [r2]
	cmp	r3, r5
	bls	.L1423
.L1424:
	ldr	r3, .L1434
	mvn	r0, #0
	ldr	r2, [sp, #52]
	ldr	r3, [r3]
	cmp	r2, r3
	beq	.L1425
	bl	__stack_chk_fail
.L1425:
	add	sp, sp, #60
	@ sp needed
	pop	{r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L1435:
	.align	2
.L1434:
	.word	__stack_chk_guard
	.word	.LANCHOR2
	.word	.LANCHOR0
	.word	.LANCHOR0+310
	.word	.LANCHOR0+312
	.word	.LANCHOR0+302
	.word	.LANCHOR0+258
	.word	.LC117
	.fnend
	.size	FtlGcScanTempBlk, .-FtlGcScanTempBlk
	.align	2
	.global	FtlReadRefresh
	.syntax unified
	.arm
	.fpu softvfp
	.type	FtlReadRefresh, %function
FtlReadRefresh:
	.fnstart
	@ args = 0, pretend = 0, frame = 96
	@ frame_needed = 0, uses_anonymous_args = 0
	push	{r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.save {r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.pad #100
	sub	sp, sp, #100
	ldr	r6, .L1454
	ldr	r7, .L1454+4
	ldr	r10, [r6, #2724]
	mov	r5, r6
	ldr	r3, [r7]
	cmp	r10, #0
	str	r3, [sp, #92]
	beq	.L1437
	ldr	r2, [r6, #2728]
	ldr	r3, [r6, #2556]
	cmp	r2, r3
	bcs	.L1438
	mov	r4, #2048
.L1443:
	ldr	r0, [r5, #2728]
	ldr	r3, [r5, #2556]
	cmp	r0, r3
	bcc	.L1439
.L1442:
	mvn	r0, #0
.L1436:
	ldr	r2, [sp, #92]
	ldr	r3, [r7]
	cmp	r2, r3
	beq	.L1448
	bl	__stack_chk_fail
.L1439:
	mov	r2, #0
	add	r1, sp, #4
	bl	log2phys
	ldr	r2, [sp, #4]
	ldr	r3, [r5, #2728]
	cmn	r2, #1
	add	r3, r3, #1
	str	r3, [r5, #2728]
	beq	.L1441
	str	r3, [sp, #24]
	add	r0, sp, #96
	ldr	r3, [r5, #3328]
	mov	r1, #1
	str	r2, [sp, #12]
	mov	r2, #0
	str	r2, [r0, #-88]!
	str	r3, [sp, #16]
	add	r3, sp, #28
	str	r3, [sp, #20]
	bl	FlashReadPages
	ldr	r3, [sp, #8]
	cmp	r3, #256
	bne	.L1442
	ldr	r0, [sp, #4]
	ubfx	r0, r0, #10, #16
	bl	P2V_block_in_plane
	bl	FtlGcRefreshBlock
	b	.L1442
.L1441:
	subs	r4, r4, #1
	bne	.L1443
	b	.L1442
.L1438:
	ldr	r3, [r6, #2560]
	mov	r0, #0
	str	r0, [r6, #2724]
	str	r0, [r6, #2728]
	str	r3, [r6, #2720]
	b	.L1436
.L1437:
	ldr	r1, [r6, #2616]
	movw	r4, #10000
	ldr	r9, [r6, #2560]
	ldr	r8, [r6, #2720]
	cmp	r1, r4
	ldr	fp, .L1454+8
	add	r3, r9, #1048576
	movhi	r4, #31
	movls	r4, #63
	cmp	r8, r3
	bhi	.L1447
	ldr	r3, [r6, #2556]
	lsr	r1, r1, #10
	mov	r0, #1000
	add	r1, r1, #1
	mul	r0, r0, r3
	bl	__aeabi_uidiv
	add	r0, r0, r8
	cmp	r9, r0
	bhi	.L1447
	ldrh	r3, [fp, #28]
	ands	r0, r4, r3
	movne	r0, r10
	bne	.L1436
	ldr	r2, [r6, #2744]
	cmp	r3, r2
	beq	.L1436
.L1447:
	ldrh	r3, [fp, #28]
	mov	r0, #0
	str	r0, [r5, #2728]
	str	r9, [r5, #2720]
	str	r3, [r5, #2744]
	mov	r3, #1
	str	r3, [r5, #2724]
	b	.L1436
.L1448:
	add	sp, sp, #100
	@ sp needed
	pop	{r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L1455:
	.align	2
.L1454:
	.word	.LANCHOR0
	.word	__stack_chk_guard
	.word	.LANCHOR0+2468
	.fnend
	.size	FtlReadRefresh, .-FtlReadRefresh
	.align	2
	.global	l2p_flush
	.syntax unified
	.arm
	.fpu softvfp
	.type	l2p_flush, %function
l2p_flush:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	push	{r4, r5, r6, r7, r8, lr}
	.save {r4, r5, r6, r7, r8, lr}
	mov	r4, #0
	ldr	r6, .L1461
	mov	r7, #12
	ldr	r5, .L1461+4
	bl	FtlWriteDump_data
.L1457:
	ldrh	r3, [r5]
	uxth	r0, r4
	cmp	r3, r0
	bhi	.L1459
	mov	r0, #0
	pop	{r4, r5, r6, r7, r8, pc}
.L1459:
	ldr	r2, [r6, #2540]
	uxth	r3, r4
	mla	r3, r7, r3, r2
	ldr	r3, [r3, #4]
	cmp	r3, #0
	bge	.L1458
	bl	flush_l2p_region
.L1458:
	add	r4, r4, #1
	b	.L1457
.L1462:
	.align	2
.L1461:
	.word	.LANCHOR0
	.word	.LANCHOR0+338
	.fnend
	.size	l2p_flush, .-l2p_flush
	.align	2
	.global	FtlVendorPartWrite
	.syntax unified
	.arm
	.fpu softvfp
	.type	FtlVendorPartWrite, %function
FtlVendorPartWrite:
	.fnstart
	@ args = 0, pretend = 0, frame = 104
	@ frame_needed = 0, uses_anonymous_args = 0
	ldr	r3, .L1474
	push	{r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.save {r4, r5, r6, r7, r8, r9, r10, fp, lr}
	mov	r10, r2
	ldr	r4, .L1474+4
	.pad #108
	sub	sp, sp, #108
	mov	r6, r1
	add	r1, r0, r1
	ldr	r2, [r3]
	str	r3, [sp, #4]
	str	r2, [sp, #100]
	ldrh	r2, [r4]
	cmp	r1, r2
	mvnhi	r9, #0
	bhi	.L1463
	sub	r4, r4, #296
	mov	r8, r0
	add	r3, r4, #308
	mov	r9, #0
	ldrh	r7, [r3]
	lsr	r7, r0, r7
	lsl	fp, r7, #2
.L1465:
	cmp	r6, #0
	bne	.L1470
.L1463:
	ldr	r3, [sp, #4]
	mov	r0, r9
	ldr	r2, [sp, #100]
	ldr	r3, [r3]
	cmp	r2, r3
	beq	.L1471
	bl	__stack_chk_fail
.L1470:
	ldr	r3, [r4, #3376]
	mov	r0, r8
	ldr	r2, [r3, fp]
	ldr	r3, .L1474+8
	str	r2, [sp, #12]
	ldrh	r3, [r3]
	mov	r1, r3
	str	r3, [sp, #8]
	bl	__aeabi_uidivmod
	ldr	r3, [sp, #8]
	ldr	r2, [sp, #12]
	str	r1, [sp]
	sub	r5, r3, r1
	uxth	r5, r5
	cmp	r6, r5
	uxthcc	r5, r6
	cmp	r2, #0
	cmpne	r5, r3
	movne	r1, #1
	moveq	r1, #0
	beq	.L1467
	ldr	r3, [r4, #3312]
	add	r0, sp, #16
	str	r2, [sp, #20]
	mov	r2, #1
	mov	r1, r2
	str	r3, [sp, #24]
	add	r3, sp, #36
	str	r3, [sp, #28]
	bl	FlashReadPages
.L1468:
	lsl	r3, r5, #9
	ldr	r0, [r4, #3312]
	mov	r1, r10
	mov	r2, r3
	str	r3, [sp, #8]
	ldr	r3, [sp]
	sub	r6, r6, r5
	add	r8, r8, r5
	add	fp, fp, #4
	add	r0, r0, r3, lsl #9
	bl	ftl_memcpy
	mov	r1, r7
	ldr	r2, [r4, #3312]
	ldr	r0, .L1474+12
	add	r7, r7, #1
	bl	FtlMapWritePage
	ldr	r3, [sp, #8]
	cmn	r0, #1
	mvneq	r9, #0
	add	r10, r10, r3
	b	.L1465
.L1467:
	ldr	r3, .L1474+16
	ldr	r0, [r4, #3312]
	ldrh	r2, [r3]
	bl	ftl_memset
	b	.L1468
.L1471:
	add	sp, sp, #108
	@ sp needed
	pop	{r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L1475:
	.align	2
.L1474:
	.word	__stack_chk_guard
	.word	.LANCHOR0+296
	.word	.LANCHOR0+258
	.word	.LANCHOR0+3612
	.word	.LANCHOR0+310
	.fnend
	.size	FtlVendorPartWrite, .-FtlVendorPartWrite
	.align	2
	.global	Ftl_save_ext_data
	.syntax unified
	.arm
	.fpu softvfp
	.type	Ftl_save_ext_data, %function
Ftl_save_ext_data:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	@ link register save eliminated.
	ldr	r3, .L1478
	ldr	r2, .L1478+4
	ldr	r1, [r3, #2644]
	cmp	r1, r2
	bxne	lr
	ldr	r2, .L1478+8
	mov	r1, #1
	mov	r0, #0
	str	r2, [r3, #2648]
	ldr	r2, [r3, #2584]
	str	r2, [r3, #2732]
	ldr	r2, [r3, #2588]
	str	r2, [r3, #2736]
	ldr	r2, [r3, #2580]
	str	r2, [r3, #2652]
	ldr	r2, [r3, #2568]
	str	r2, [r3, #2656]
	ldr	r2, [r3, #2560]
	str	r2, [r3, #2660]
	ldr	r2, [r3, #2576]
	str	r2, [r3, #2664]
	ldr	r2, [r3, #2604]
	str	r2, [r3, #2672]
	ldr	r2, [r3, #2612]
	str	r2, [r3, #2676]
	ldr	r2, [r3, #2564]
	str	r2, [r3, #2680]
	ldr	r2, [r3, #2572]
	str	r2, [r3, #2684]
	ldr	r2, [r3, #2616]
	str	r2, [r3, #2688]
	ldr	r2, [r3, #2620]
	str	r2, [r3, #2692]
	ldr	r2, .L1478+12
	b	FtlVendorPartWrite
.L1479:
	.align	2
.L1478:
	.word	.LANCHOR0
	.word	1179929683
	.word	1342177365
	.word	.LANCHOR0+2644
	.fnend
	.size	Ftl_save_ext_data, .-Ftl_save_ext_data
	.align	2
	.global	FtlEctTblFlush
	.syntax unified
	.arm
	.fpu softvfp
	.type	FtlEctTblFlush, %function
FtlEctTblFlush:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	ldr	r3, .L1488
	movw	r1, #3656
	ldrh	r2, [r3, r1]
	cmp	r2, #31
	addls	r2, r2, #1
	movhi	r2, #32
	strhls	r2, [r3, r1]	@ movhi
	movls	r2, #1
	cmp	r0, #0
	bne	.L1482
	ldr	r1, [r3, #3352]
	ldr	r0, [r1, #20]
	ldr	r1, [r1, #16]
	add	r2, r2, r0
	cmp	r1, r2
	bcc	.L1486
.L1482:
	push	{r4, lr}
	.save {r4, lr}
	mov	r0, #64
	ldr	r2, [r3, #3352]
	ldr	r1, [r2, #16]
	str	r1, [r2, #20]
	ldr	r1, .L1488+4
	str	r1, [r2]
	ldr	r2, [r3, #3352]
	ldr	r3, .L1488+8
	ldrh	r1, [r3]
	lsl	r3, r1, #9
	str	r3, [r2, #12]
	ldr	r3, [r2, #8]
	add	r3, r3, #1
	str	r3, [r2, #8]
	mov	r3, #0
	str	r3, [r2, #4]
	bl	FtlVendorPartWrite
	bl	Ftl_save_ext_data
	mov	r0, #0
	pop	{r4, pc}
.L1486:
	mov	r0, #0
	bx	lr
.L1489:
	.align	2
.L1488:
	.word	.LANCHOR0
	.word	1112818501
	.word	.LANCHOR0+3344
	.fnend
	.size	FtlEctTblFlush, .-FtlEctTblFlush
	.align	2
	.global	sftl_vendor_write
	.syntax unified
	.arm
	.fpu softvfp
	.type	sftl_vendor_write, %function
sftl_vendor_write:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	@ link register save eliminated.
	add	r0, r0, #256
	b	FtlVendorPartWrite
	.fnend
	.size	sftl_vendor_write, .-sftl_vendor_write
	.align	2
	.global	FtlVendorPartRead
	.syntax unified
	.arm
	.fpu softvfp
	.type	FtlVendorPartRead, %function
FtlVendorPartRead:
	.fnstart
	@ args = 0, pretend = 0, frame = 104
	@ frame_needed = 0, uses_anonymous_args = 0
	ldr	r3, .L1503
	push	{r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.save {r4, r5, r6, r7, r8, r9, r10, fp, lr}
	mov	r10, r2
	ldr	r5, .L1503+4
	.pad #108
	sub	sp, sp, #108
	mov	r7, r1
	add	r1, r0, r1
	ldr	r2, [r3]
	str	r3, [sp, #8]
	str	r2, [sp, #100]
	ldrh	r2, [r5]
	cmp	r1, r2
	mvnhi	r9, #0
	bhi	.L1491
	sub	r5, r5, #296
	mov	r8, r0
	add	r3, r5, #308
	mov	r9, #0
	ldrh	r6, [r3]
	lsr	r6, r0, r6
	lsl	fp, r6, #2
.L1493:
	cmp	r7, #0
	bne	.L1499
.L1491:
	ldr	r3, [sp, #8]
	mov	r0, r9
	ldr	r2, [sp, #100]
	ldr	r3, [r3]
	cmp	r2, r3
	beq	.L1500
	bl	__stack_chk_fail
.L1499:
	ldr	r3, [r5, #3376]
	mov	r0, r8
	ldr	r3, [r3, fp]
	str	r3, [sp, #12]
	ldr	r3, .L1503+8
	ldrh	r4, [r3]
	mov	r1, r4
	bl	__aeabi_uidivmod
	sub	r4, r4, r1
	ldr	r3, [sp, #12]
	uxth	r4, r4
	str	r1, [sp, #4]
	cmp	r7, r4
	uxthcc	r4, r7
	cmp	r3, #0
	lsl	r2, r4, #9
	str	r2, [sp, #12]
	beq	.L1495
	ldr	r2, [r5, #3312]
	add	r0, sp, #16
	str	r3, [sp, #20]
	str	r3, [sp, #12]
	str	r2, [sp, #24]
	add	r2, sp, #36
	str	r2, [sp, #28]
	mov	r2, #1
	mov	r1, r2
	bl	FlashReadPages
	ldr	r2, [sp, #16]
	ldr	r3, [sp, #12]
	cmn	r2, #1
	ldr	r2, [r5, #3456]
	mvneq	r9, #0
	cmp	r2, #256
	bne	.L1497
	mov	r2, r3
	mov	r1, r6
	ldr	r0, .L1503+12
	bl	sftl_printk
	ldr	r2, [r5, #3312]
	mov	r1, r6
	ldr	r0, .L1503+16
	bl	FtlMapWritePage
.L1497:
	ldr	r1, [r5, #3312]
	lsl	r2, r4, #9
	ldr	r3, [sp, #4]
	mov	r0, r10
	add	r1, r1, r3, lsl #9
	bl	ftl_memcpy
.L1498:
	add	r6, r6, #1
	sub	r7, r7, r4
	add	r8, r8, r4
	add	r10, r10, r4, lsl #9
	add	fp, fp, #4
	b	.L1493
.L1495:
	lsl	r2, r4, #9
	mov	r1, r3
	mov	r0, r10
	bl	ftl_memset
	b	.L1498
.L1500:
	add	sp, sp, #108
	@ sp needed
	pop	{r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L1504:
	.align	2
.L1503:
	.word	__stack_chk_guard
	.word	.LANCHOR0+296
	.word	.LANCHOR0+258
	.word	.LC118
	.word	.LANCHOR0+3612
	.fnend
	.size	FtlVendorPartRead, .-FtlVendorPartRead
	.align	2
	.global	FtlLoadEctTbl
	.syntax unified
	.arm
	.fpu softvfp
	.type	FtlLoadEctTbl, %function
FtlLoadEctTbl:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	push	{r4, r5, r6, lr}
	.save {r4, r5, r6, lr}
	mov	r0, #64
	ldr	r4, .L1508
	add	r5, r4, #3344
	ldr	r2, [r4, #3352]
	ldrh	r1, [r5]
	bl	FtlVendorPartRead
	ldr	r3, [r4, #3352]
	ldr	r2, [r3]
	ldr	r3, .L1508+4
	cmp	r2, r3
	beq	.L1506
	ldr	r1, .L1508+8
	ldr	r0, .L1508+12
	bl	sftl_printk
	ldrh	r2, [r5]
	mov	r1, #0
	ldr	r0, [r4, #3352]
	lsl	r2, r2, #9
	bl	ftl_memset
.L1506:
	mov	r0, #0
	pop	{r4, r5, r6, pc}
.L1509:
	.align	2
.L1508:
	.word	.LANCHOR0
	.word	1112818501
	.word	.LC119
	.word	.LC77
	.fnend
	.size	FtlLoadEctTbl, .-FtlLoadEctTbl
	.align	2
	.global	Ftl_load_ext_data
	.syntax unified
	.arm
	.fpu softvfp
	.type	Ftl_load_ext_data, %function
Ftl_load_ext_data:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	push	{r4, r5, r6, lr}
	.save {r4, r5, r6, lr}
	mov	r1, #1
	ldr	r4, .L1514
	mov	r0, #0
	ldr	r2, .L1514+4
	bl	FtlVendorPartRead
	ldr	r5, .L1514+8
	ldr	r3, [r4, #2644]
	cmp	r3, r5
	beq	.L1511
	mov	r2, #512
	mov	r1, #0
	ldr	r0, .L1514+4
	bl	ftl_memset
	str	r5, [r4, #2644]
.L1511:
	ldr	r3, [r4, #2644]
	cmp	r3, r5
	bne	.L1512
	ldr	r3, [r4, #2732]
	str	r3, [r4, #2584]
	ldr	r3, [r4, #2736]
	str	r3, [r4, #2588]
	ldr	r3, [r4, #2652]
	str	r3, [r4, #2580]
	ldr	r3, [r4, #2656]
	str	r3, [r4, #2568]
	ldr	r3, [r4, #2660]
	str	r3, [r4, #2560]
	ldr	r3, [r4, #2664]
	str	r3, [r4, #2576]
	ldr	r3, [r4, #2672]
	str	r3, [r4, #2604]
	ldr	r3, [r4, #2676]
	str	r3, [r4, #2612]
	ldr	r3, [r4, #2680]
	str	r3, [r4, #2564]
	ldr	r3, [r4, #2684]
	str	r3, [r4, #2572]
	ldr	r3, [r4, #2688]
	str	r3, [r4, #2616]
	ldr	r3, [r4, #2692]
	str	r3, [r4, #2620]
.L1512:
	ldr	r3, .L1514+12
	ldr	r0, [r4, #2600]
	ldrh	r1, [r4, #240]
	ldrh	r2, [r3]
	ldr	r3, [r4, #2604]
	mla	r0, r0, r2, r3
	bl	__aeabi_uidiv
	str	r0, [r4, #2608]
	pop	{r4, r5, r6, pc}
.L1515:
	.align	2
.L1514:
	.word	.LANCHOR0
	.word	.LANCHOR0+2644
	.word	1179929683
	.word	.LANCHOR0+292
	.fnend
	.size	Ftl_load_ext_data, .-Ftl_load_ext_data
	.align	2
	.global	sftl_vendor_read
	.syntax unified
	.arm
	.fpu softvfp
	.type	sftl_vendor_read, %function
sftl_vendor_read:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	@ link register save eliminated.
	add	r0, r0, #256
	b	FtlVendorPartRead
	.fnend
	.size	sftl_vendor_read, .-sftl_vendor_read
	.align	2
	.global	FtlMapBlkWriteDump_data
	.syntax unified
	.arm
	.fpu softvfp
	.type	FtlMapBlkWriteDump_data, %function
FtlMapBlkWriteDump_data:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	ldr	r3, [r0, #36]
	cmp	r3, #0
	bxeq	lr
	mov	r3, #0
	push	{r4, r5, r6, lr}
	.save {r4, r5, r6, lr}
	str	r3, [r0, #36]
	mov	r5, r0
	ldr	r3, .L1524
	ldrh	r6, [r0, #6]
	ldr	r2, [r0, #24]
	ldr	r1, [r3, #3336]
	mov	r4, r3
	ldr	r0, [r3, #3308]
	sub	r6, r6, #1
	uxth	r6, r6
	str	r1, [r3, #3468]
	str	r0, [r3, #3464]
	ldr	r2, [r2, r6, lsl #2]
	cmp	r2, #0
	str	r2, [r3, #3460]
	beq	.L1519
	mov	r2, #1
	add	r0, r3, #3456
	mov	r1, r2
	bl	FlashReadPages
.L1520:
	ldr	r2, [r4, #3464]
	mov	r1, r6
	mov	r0, r5
	pop	{r4, r5, r6, lr}
	b	FtlMapWritePage
.L1519:
	movw	r3, #310
	mov	r1, #255
	ldrh	r2, [r4, r3]
	bl	ftl_memset
	b	.L1520
.L1525:
	.align	2
.L1524:
	.word	.LANCHOR0
	.fnend
	.size	FtlMapBlkWriteDump_data, .-FtlMapBlkWriteDump_data
	.align	2
	.global	FtlVpcTblFlush
	.syntax unified
	.arm
	.fpu softvfp
	.type	FtlVpcTblFlush, %function
FtlVpcTblFlush:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	push	{r3, r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.save {r3, r4, r5, r6, r7, r8, r9, r10, fp, lr}
	mov	r6, #0
	ldr	r4, .L1536
	mov	r1, #255
	ldr	r5, .L1536+4
	ldr	r3, [r4, #3304]
	add	r9, r4, #304
	ldr	r7, [r4, #3336]
	mov	fp, r9
	sub	r8, r5, #160
	add	r10, r9, #6
	str	r3, [r4, #3464]
	movw	r3, #2628
	ldrh	r3, [r4, r3]
	str	r7, [r4, #3468]
	str	r6, [r7, #12]
	strh	r3, [r7, #2]	@ movhi
	ldr	r3, .L1536+8
	strh	r3, [r7]	@ movhi
	ldr	r3, [r4, #2636]
	str	r6, [r7, #8]
	ldrh	r2, [r4, #26]
	str	r3, [r7, #4]
	ldr	r3, .L1536+12
	str	r3, [r4, #2468]
	ldr	r3, .L1536+16
	str	r3, [r4, #2472]
	ldrh	r3, [r5, #6]
	strh	r3, [r5, #-152]	@ movhi
	ldrh	r3, [r4, #254]
	strb	r3, [r4, #2478]
	ldrh	r3, [r4, #24]
	strh	r3, [r5, #-146]	@ movhi
	ldrb	r3, [r4, #30]	@ zero_extendqisi2
	orr	r3, r3, r2, lsl #6
	ldrh	r2, [r4, #78]
	strh	r3, [r5, #-144]	@ movhi
	ldrb	r3, [r4, #32]	@ zero_extendqisi2
	strb	r3, [r4, #2479]
	ldrh	r3, [r4, #76]
	strh	r3, [r5, #-142]	@ movhi
	ldrb	r3, [r4, #82]	@ zero_extendqisi2
	orr	r3, r3, r2, lsl #6
	strh	r3, [r5, #-140]	@ movhi
	ldrb	r3, [r4, #84]	@ zero_extendqisi2
	strb	r3, [r4, #2480]
	ldrh	r3, [r4, #124]
	ldrh	r2, [r4, #126]
	ldr	r0, [r4, #3464]
	strh	r3, [r5, #-138]	@ movhi
	ldrb	r3, [r4, #130]	@ zero_extendqisi2
	orr	r3, r3, r2, lsl #6
	strh	r3, [r5, #-136]	@ movhi
	ldrb	r3, [r4, #132]	@ zero_extendqisi2
	strb	r3, [r4, #2481]
	ldr	r3, [r4, #2600]
	str	r3, [r4, #2500]
	ldr	r3, [r4, #2592]
	str	r3, [r4, #2508]
	ldr	r3, [r4, #2596]
	str	r3, [r4, #2504]
	movw	r3, #310
	ldrh	r2, [r4, r3]
	bl	ftl_memset
	mov	r1, r8
	mov	r2, #48
	ldr	r0, [r4, #3464]
	movw	r8, #65535
	bl	ftl_memcpy
	ldrh	r2, [r4, #240]
	ldr	r0, [r4, #3464]
	ldr	r1, [r4, #72]
	lsl	r2, r2, #1
	add	r0, r0, #48
	bl	ftl_memcpy
	mov	r0, r6
	bl	FtlUpdateVaildLpn
.L1527:
	ldr	r3, [r4, #3304]
	ldrh	r1, [r5, #2]
	ldrh	r2, [r5]
	str	r3, [r4, #3464]
	ldr	r3, [r4, #3336]
	str	r3, [r4, #3468]
	orr	r3, r1, r2, lsl #10
	str	r3, [r4, #3460]
	ldrh	r3, [r9]
	sub	r3, r3, #1
	cmp	r1, r3
	blt	.L1528
	mov	r3, #0
	ldrh	r8, [r5, #4]
	strh	r3, [r5, #2]	@ movhi
	strh	r2, [r5, #4]	@ movhi
	bl	FtlFreeSysBlkQueueOut
	ldr	r3, [r4, #2592]
	strh	r0, [r5]	@ movhi
	add	r2, r3, #1
	str	r3, [r4, #2636]
	str	r2, [r4, #2592]
	lsl	r2, r0, #10
	str	r2, [r4, #3460]
	str	r3, [r7, #4]
	strh	r0, [r7, #2]	@ movhi
.L1528:
	ldrh	r1, [r10]
	ldr	r0, [r4, #3304]
	bl	js_hash
	mov	r3, #1
	str	r0, [r7, #12]
	mov	r2, r3
	mov	r1, r3
	ldr	r0, .L1536+20
	bl	FlashProgPages
	ldrh	r3, [r5, #2]
	ldr	r2, [r4, #3456]
	add	r3, r3, #1
	uxth	r3, r3
	cmn	r2, #1
	strh	r3, [r5, #2]	@ movhi
	bne	.L1529
	cmp	r3, #1
	bne	.L1530
	movw	r2, #1138
	ldr	r1, .L1536+24
	ldr	r0, .L1536+28
	bl	sftl_printk
.L1530:
	ldrh	r3, [r5, #2]
	add	r6, r6, #1
	uxth	r6, r6
	cmp	r3, #1
	ldrheq	r3, [fp]
	subeq	r3, r3, #1
	strheq	r3, [r5, #2]	@ movhi
	cmp	r6, #3
	bls	.L1527
	mov	r2, r6
	ldr	r1, [r4, #3460]
	ldr	r0, .L1536+32
	bl	sftl_printk
.L1533:
	b	.L1533
.L1529:
	cmp	r2, #256
	cmpne	r3, #1
	beq	.L1527
	movw	r3, #65535
	cmp	r8, r3
	beq	.L1534
	mov	r1, #1
	mov	r0, r8
	bl	FtlFreeSysBlkQueueIn
.L1534:
	mov	r0, #0
	pop	{r3, r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L1537:
	.align	2
.L1536:
	.word	.LANCHOR0
	.word	.LANCHOR0+2628
	.word	-3932
	.word	1179929683
	.word	1342177365
	.word	.LANCHOR0+3456
	.word	.LANCHOR1+471
	.word	.LC8
	.word	.LC120
	.fnend
	.size	FtlVpcTblFlush, .-FtlVpcTblFlush
	.align	2
	.global	FtlSysFlush
	.syntax unified
	.arm
	.fpu softvfp
	.type	FtlSysFlush, %function
FtlSysFlush:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	push	{r4, lr}
	.save {r4, lr}
	bl	l2p_flush
	mov	r0, #1
	bl	FtlEctTblFlush
	bl	FtlVpcTblFlush
	mov	r0, #0
	pop	{r4, pc}
	.fnend
	.size	FtlSysFlush, .-FtlSysFlush
	.align	2
	.global	sftl_deinit
	.syntax unified
	.arm
	.fpu softvfp
	.type	sftl_deinit, %function
sftl_deinit:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	ldr	r3, .L1546
	ldr	r3, [r3]
	cmp	r3, #1
	bne	.L1543
	push	{r4, lr}
	.save {r4, lr}
	bl	FtlSysFlush
	mov	r0, #0
	pop	{r4, pc}
.L1543:
	mov	r0, #0
	bx	lr
.L1547:
	.align	2
.L1546:
	.word	.LANCHOR2
	.fnend
	.size	sftl_deinit, .-sftl_deinit
	.align	2
	.global	sftl_discard
	.syntax unified
	.arm
	.fpu softvfp
	.type	sftl_discard, %function
sftl_discard:
	.fnstart
	@ args = 0, pretend = 0, frame = 16
	@ frame_needed = 0, uses_anonymous_args = 0
	push	{r4, r5, r6, r7, r8, r9, lr}
	.save {r4, r5, r6, r7, r8, r9, lr}
	.pad #20
	sub	sp, sp, #20
	ldr	r8, .L1566
	ldr	r4, .L1566+4
	ldr	r3, [r8]
	str	r3, [sp, #12]
	ldr	r3, [r4, #340]
	cmp	r3, r1
	cmpcs	r3, r0
	bls	.L1558
	add	r2, r0, r1
	mov	r7, r0
	cmp	r3, r2
	mov	r5, r1
	bcc	.L1558
	cmp	r1, #31
	bhi	.L1550
.L1555:
	mov	r0, #0
.L1548:
	ldr	r2, [sp, #12]
	ldr	r3, [r8]
	cmp	r2, r3
	beq	.L1556
	bl	__stack_chk_fail
.L1550:
	movw	r3, #258
	ldrh	r6, [r4, r3]
	mov	r1, r6
	bl	__aeabi_uidiv
	smulbb	r3, r0, r6
	mov	r9, r0
	sub	r7, r7, r3
	uxth	r7, r7
	cmp	r7, #0
	beq	.L1551
	sub	r6, r6, r7
	add	r9, r0, #1
	cmp	r6, r5
	movcs	r6, r5
	uxth	r6, r6
	sub	r5, r5, r6
.L1551:
	ldr	r6, .L1566+8
	mvn	r3, #0
	str	r3, [sp, #8]
	mov	r7, r6
.L1552:
	ldrh	r3, [r6]
	cmp	r5, r3
	bcs	.L1554
	ldr	r3, [r4, #3660]
	cmp	r3, #32
	bls	.L1555
	mov	r5, #0
	str	r5, [r4, #3660]
	bl	l2p_flush
	bl	FtlVpcTblFlush
	b	.L1555
.L1554:
	mov	r2, #0
	add	r1, sp, #4
	mov	r0, r9
	bl	log2phys
	ldr	r3, [sp, #4]
	cmn	r3, #1
	beq	.L1553
	ldr	r3, [r4, #3660]
	mov	r2, #1
	add	r1, sp, #8
	mov	r0, r9
	add	r3, r3, #1
	str	r3, [r4, #3660]
	ldr	r3, [r4, #2564]
	add	r3, r3, #1
	str	r3, [r4, #2564]
	bl	log2phys
	ldr	r0, [sp, #4]
	ubfx	r0, r0, #10, #16
	bl	P2V_block_in_plane
	bl	decrement_vpc_count
.L1553:
	ldrh	r3, [r7]
	add	r9, r9, #1
	sub	r5, r5, r3
	b	.L1552
.L1558:
	mvn	r0, #0
	b	.L1548
.L1556:
	add	sp, sp, #20
	@ sp needed
	pop	{r4, r5, r6, r7, r8, r9, pc}
.L1567:
	.align	2
.L1566:
	.word	__stack_chk_guard
	.word	.LANCHOR0
	.word	.LANCHOR0+258
	.fnend
	.size	sftl_discard, .-sftl_discard
	.align	2
	.global	FtlVpcCheckAndModify
	.syntax unified
	.arm
	.fpu softvfp
	.type	FtlVpcCheckAndModify, %function
FtlVpcCheckAndModify:
	.fnstart
	@ args = 0, pretend = 0, frame = 8
	@ frame_needed = 0, uses_anonymous_args = 0
	push	{r0, r1, r2, r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.save {r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.pad #12
	mov	r5, #0
	ldr	r7, .L1584
	ldr	r4, .L1584+4
	ldr	r3, [r7]
	ldr	r1, .L1584+8
	ldr	r0, .L1584+12
	str	r3, [sp, #4]
	bl	sftl_printk
	ldrh	r2, [r4, #242]
	mov	r1, #0
	ldr	r0, [r4, #3356]
	lsl	r2, r2, #1
	bl	ftl_memset
.L1569:
	ldr	r3, [r4, #2556]
	cmp	r5, r3
	bcc	.L1571
	ldr	r9, .L1584+16
	mov	r8, #0
	ldr	r10, .L1584+20
	ldr	fp, .L1584+8
.L1572:
	ldrh	r3, [r4, #240]
	uxth	r5, r8
	cmp	r3, r5
	bhi	.L1578
	bl	l2p_flush
	bl	FtlVpcTblFlush
	ldr	r2, [sp, #4]
	ldr	r3, [r7]
	cmp	r2, r3
	beq	.L1579
	bl	__stack_chk_fail
.L1571:
	mov	r2, #0
	mov	r1, sp
	mov	r0, r5
	bl	log2phys
	ldr	r0, [sp]
	cmn	r0, #1
	beq	.L1570
	ubfx	r0, r0, #10, #16
	bl	P2V_block_in_plane
	ldr	r2, [r4, #3356]
	lsl	r0, r0, #1
	ldrh	r3, [r2, r0]
	add	r3, r3, #1
	strh	r3, [r2, r0]	@ movhi
.L1570:
	add	r5, r5, #1
	b	.L1569
.L1578:
	uxth	r1, r8
	ldr	r3, [r4, #72]
	movw	r0, #65535
	lsl	r6, r1, #1
	ldrh	r2, [r3, r6]
	ldr	r3, [r4, #3356]
	ldrh	r3, [r3, r6]
	cmp	r2, r0
	cmpne	r2, r3
	beq	.L1574
	mov	r0, r9
	bl	sftl_printk
	ldrh	r3, [r4, #24]
	cmp	r3, r5
	beq	.L1574
	ldrh	r3, [r4, #124]
	cmp	r3, r5
	beq	.L1574
	ldrh	r3, [r4, #76]
	cmp	r3, r5
	beq	.L1574
	ldr	r3, [r4, #72]
	ldrh	r2, [r3, r6]
	cmp	r2, #0
	ldr	r2, [r4, #3356]
	ldrh	r2, [r2, r6]
	strh	r2, [r3, r6]	@ movhi
	bne	.L1576
	mov	r1, r5
	mov	r0, r10
	bl	List_remove_node
	ldrh	r3, [r4, #224]
	cmp	r3, #0
	bne	.L1577
	movw	r2, #2323
	mov	r1, fp
	ldr	r0, .L1584+24
	bl	sftl_printk
.L1577:
	ldrh	r3, [r4, #224]
	mov	r0, r5
	sub	r3, r3, #1
	strh	r3, [r4, #224]	@ movhi
	bl	INSERT_DATA_LIST
.L1574:
	add	r8, r8, #1
	b	.L1572
.L1576:
	mov	r0, r5
	bl	update_vpc_list
	b	.L1574
.L1579:
	add	sp, sp, #12
	@ sp needed
	pop	{r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L1585:
	.align	2
.L1584:
	.word	__stack_chk_guard
	.word	.LANCHOR0
	.word	.LANCHOR1+486
	.word	.LC111
	.word	.LC121
	.word	.LANCHOR0+2532
	.word	.LC8
	.fnend
	.size	FtlVpcCheckAndModify, .-FtlVpcCheckAndModify
	.align	2
	.global	allocate_new_data_superblock
	.syntax unified
	.arm
	.fpu softvfp
	.type	allocate_new_data_superblock, %function
allocate_new_data_superblock:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	push	{r4, r5, r6, lr}
	.save {r4, r5, r6, lr}
	mov	r6, r0
	ldr	r4, .L1597
	ldrh	r5, [r0]
	ldrh	r3, [r4, #240]
	cmp	r3, r5
	bcs	.L1587
	movw	r2, #2759
	ldr	r1, .L1597+4
	ldr	r0, .L1597+8
	bl	sftl_printk
.L1587:
	movw	r3, #65535
	cmp	r5, r3
	beq	.L1588
	ldr	r2, [r4, #72]
	lsl	r3, r5, #1
	mov	r0, r5
	ldrh	r3, [r2, r3]
	cmp	r3, #0
	beq	.L1589
	bl	INSERT_DATA_LIST
.L1588:
	mov	r3, #1
	strb	r3, [r6, #8]
	movw	r3, #3442
	ldrh	r0, [r4, r3]
	movw	r3, #65535
	cmp	r0, r3
	beq	.L1590
	cmp	r5, r0
	bne	.L1591
	ldr	r2, [r4, #72]
	lsl	r3, r0, #1
	ldrh	r3, [r2, r3]
	cmp	r3, #0
	beq	.L1592
.L1591:
	bl	update_vpc_list
.L1592:
	mvn	r2, #0
	movw	r3, #3442
	strh	r2, [r4, r3]	@ movhi
.L1590:
	mov	r0, r6
	bl	allocate_data_superblock
	bl	l2p_flush
	mov	r0, #0
	bl	FtlEctTblFlush
	bl	FtlVpcTblFlush
	mov	r0, #0
	pop	{r4, r5, r6, pc}
.L1589:
	bl	INSERT_FREE_LIST
	b	.L1588
.L1598:
	.align	2
.L1597:
	.word	.LANCHOR0
	.word	.LANCHOR1+507
	.word	.LC8
	.fnend
	.size	allocate_new_data_superblock, .-allocate_new_data_superblock
	.align	2
	.global	FtlProgPages
	.syntax unified
	.arm
	.fpu softvfp
	.type	FtlProgPages, %function
FtlProgPages:
	.fnstart
	@ args = 0, pretend = 0, frame = 16
	@ frame_needed = 0, uses_anonymous_args = 0
	push	{r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.save {r4, r5, r6, r7, r8, r9, r10, fp, lr}
	mov	r5, r3
	ldr	r6, .L1614
	.pad #20
	sub	sp, sp, #20
	mov	r7, #0
	mov	r2, #0
	ldr	fp, .L1614+4
	mov	r4, r0
	ldr	r3, [r6]
	mov	r10, r1
	ldr	r9, .L1614+8
	str	r3, [sp, #12]
	ldrb	r3, [r5, #9]	@ zero_extendqisi2
	bl	FlashProgPages
.L1600:
	cmp	r7, r10
	bne	.L1607
	ldr	r3, .L1614+8
	ldrb	r2, [r5, #6]	@ zero_extendqisi2
	ldrh	r3, [r3, #232]
	cmp	r2, r3
	bcc	.L1599
	mov	r2, #1000
	ldr	r1, .L1614+12
	ldr	r0, .L1614+16
	bl	sftl_printk
.L1599:
	ldr	r2, [sp, #12]
	ldr	r3, [r6]
	cmp	r2, r3
	beq	.L1609
	bl	__stack_chk_fail
.L1602:
	ldr	r1, [r4, #4]
	mov	r0, fp
	bl	sftl_printk
	ldr	r0, [r4, #4]
	ubfx	r0, r0, #10, #16
	bl	P2V_block_in_plane
	bl	decrement_vpc_count
	ldrh	r3, [r5, #4]
	cmp	r3, #0
	bne	.L1601
	mov	r0, r5
	bl	allocate_new_data_superblock
.L1601:
	mov	r0, r5
	bl	get_new_active_ppa
	mov	r2, #0
	str	r0, [r4, #4]
	str	r0, [sp, #8]
	mov	r1, #1
	ldrb	r3, [r5, #9]	@ zero_extendqisi2
	mov	r0, r4
	bl	FlashProgPages
.L1607:
	ldr	r2, [r4]
	cmn	r2, #1
	cmpne	r2, #256
	beq	.L1602
	ldrb	r2, [r5, #6]	@ zero_extendqisi2
	ldrh	r3, [r9, #232]
	cmp	r2, r3
	bcc	.L1603
	movw	r2, #985
	ldr	r1, .L1614+12
	ldr	r0, .L1614+16
	bl	sftl_printk
.L1603:
	ldr	r3, [r4, #4]
	add	r1, sp, #16
	mov	r2, #1
	ldr	r0, [r4, #16]
	str	r3, [r1, #-8]!
	bl	log2phys
	ldr	r3, [r4, #12]
	ldr	r3, [r3, #12]
	ubfx	r0, r3, #10, #16
	str	r3, [sp, #4]
	bl	P2V_block_in_plane
	ldr	r3, [sp, #4]
	mov	r8, r0
	cmn	r3, #1
	beq	.L1604
	ldr	r2, [r9, #72]
	lsl	r3, r0, #1
	ldrh	r2, [r2, r3]
	cmp	r2, #0
	bne	.L1605
	mov	r1, r0
	ldr	r0, .L1614+20
	bl	sftl_printk
.L1605:
	mov	r0, r8
	bl	decrement_vpc_count
.L1604:
	add	r7, r7, #1
	add	r4, r4, #20
	b	.L1600
.L1609:
	add	sp, sp, #20
	@ sp needed
	pop	{r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L1615:
	.align	2
.L1614:
	.word	__stack_chk_guard
	.word	.LC122
	.word	.LANCHOR0
	.word	.LANCHOR1+536
	.word	.LC8
	.word	.LC123
	.fnend
	.size	FtlProgPages, .-FtlProgPages
	.align	2
	.global	FtlGcFreeTempBlock
	.syntax unified
	.arm
	.fpu softvfp
	.type	FtlGcFreeTempBlock, %function
FtlGcFreeTempBlock:
	.fnstart
	@ args = 0, pretend = 0, frame = 8
	@ frame_needed = 0, uses_anonymous_args = 0
	push	{r0, r1, r2, r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.save {r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.pad #12
	ldr	r6, .L1642
	ldr	r4, .L1642+4
	ldr	r3, [r6]
	ldrh	r2, [r4, #124]
	str	r3, [sp, #4]
	movw	r3, #302
	ldrh	r1, [r4, r3]
	movw	r3, #65535
	cmp	r2, r3
	bne	.L1617
.L1624:
	ldrh	r2, [r4, #124]
	mov	r3, #0
	str	r3, [r4, #3448]
	movw	r3, #65535
	cmp	r2, r3
	bne	.L1638
.L1618:
	mov	r0, #0
	b	.L1616
.L1617:
	cmp	r0, #0
	ldr	r5, .L1642+8
	beq	.L1620
	ldrh	r2, [r5, #4]
	cmp	r2, r3
	beq	.L1621
.L1622:
	mov	r1, #2
.L1620:
	ldr	r0, .L1642+12
	bl	FtlGcScanTempBlk
	ldrh	r2, [r5, #4]
	movw	r3, #65535
	str	r0, [sp]
	cmp	r2, r3
	movne	r0, #1
	beq	.L1624
.L1616:
	ldr	r2, [sp, #4]
	ldr	r3, [r6]
	cmp	r2, r3
	beq	.L1633
	bl	__stack_chk_fail
.L1621:
	mov	r3, #0
	strh	r3, [r5, #4]	@ movhi
	ldrh	r3, [r4, #224]
	cmp	r3, #17
	bhi	.L1622
	b	.L1620
.L1638:
	movw	r3, #3208
	ldrb	r1, [r4, #131]	@ zero_extendqisi2
	ldrh	r2, [r4, r3]
	movw	r3, #302
	ldrh	r3, [r4, r3]
	mul	r3, r3, r1
	cmp	r2, r3
	beq	.L1625
	mov	r2, #164
	ldr	r1, .L1642+16
	ldr	r0, .L1642+20
	bl	sftl_printk
.L1625:
	movw	r0, #302
	ldrb	r2, [r4, #131]	@ zero_extendqisi2
	ldrh	r0, [r4, r0]
	mov	r5, #0
	ldrh	r3, [r4, #124]
	mov	r10, #12
	ldr	r1, [r4, #72]
	ldr	r9, .L1642+24
	smulbb	r2, r2, r0
	lsl	r3, r3, #1
	strh	r2, [r1, r3]	@ movhi
	movw	r3, #3208
	ldr	r2, [r4, #2580]
	ldrh	r3, [r4, r3]
	add	r3, r3, r2
	str	r3, [r4, #2580]
.L1626:
	ldrh	r2, [r9]
	uxth	r3, r5
	cmp	r2, r3
	bhi	.L1630
	movw	r0, #65535
	bl	decrement_vpc_count
	ldrh	r0, [r4, #124]
	ldr	r2, [r4, #72]
	lsl	r3, r0, #1
	ldrh	r3, [r2, r3]
	cmp	r3, #0
	beq	.L1631
	bl	INSERT_DATA_LIST
.L1632:
	ldr	r7, .L1642+28
	movw	r3, #3208
	mvn	r8, #0
	mov	r5, #0
	strh	r5, [r4, r3]	@ movhi
	strh	r5, [r7]	@ movhi
	sub	r7, r7, #576
	strh	r8, [r4, #124]	@ movhi
	bl	l2p_flush
	bl	FtlVpcTblFlush
	ldrh	r3, [r7]
	ldrh	r2, [r4, #224]
	strh	r8, [r4, #172]	@ movhi
	add	r3, r3, r3, lsl #1
	cmp	r2, r3, asr #2
	movgt	r2, #20
	movwgt	r3, #3156
	strhgt	r2, [r4, r3]	@ movhi
	b	.L1618
.L1630:
	uxth	r8, r5
	ldr	fp, [r4, #3204]
	ldr	r3, [r4, #2556]
	mul	r8, r10, r8
	add	r7, fp, r8
	ldr	r0, [r7, #8]
	cmp	r0, r3
	bcc	.L1627
.L1640:
	ldrh	r0, [r4, #124]
	b	.L1641
.L1627:
	mov	r2, #0
	mov	r1, sp
	bl	log2phys
	ldr	r0, [fp, r8]
	ldr	r3, [sp]
	cmp	r0, r3
	bne	.L1629
	ubfx	r0, r0, #10, #16
	bl	P2V_block_in_plane
	mov	r2, #1
	mov	r8, r0
	add	r1, r7, #4
	ldr	r0, [r7, #8]
	bl	log2phys
	mov	r0, r8
.L1641:
	bl	decrement_vpc_count
	b	.L1628
.L1629:
	ldr	r2, [r7, #4]
	cmp	r3, r2
	bne	.L1640
.L1628:
	add	r5, r5, #1
	b	.L1626
.L1631:
	bl	INSERT_FREE_LIST
	b	.L1632
.L1633:
	add	sp, sp, #12
	@ sp needed
	pop	{r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L1643:
	.align	2
.L1642:
	.word	__stack_chk_guard
	.word	.LANCHOR0
	.word	.LANCHOR2
	.word	.LANCHOR0+124
	.word	.LANCHOR1+549
	.word	.LC8
	.word	.LANCHOR0+3208
	.word	.LANCHOR0+3200
	.fnend
	.size	FtlGcFreeTempBlock, .-FtlGcFreeTempBlock
	.align	2
	.global	FtlGcPageRecovery
	.syntax unified
	.arm
	.fpu softvfp
	.type	FtlGcPageRecovery, %function
FtlGcPageRecovery:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	push	{r4, r5, r6, lr}
	.save {r4, r5, r6, lr}
	movw	r5, #302
	ldr	r4, .L1647
	ldrh	r1, [r4, r5]
	add	r0, r4, #124
	bl	FtlGcScanTempBlk
	ldrh	r2, [r4, #126]
	ldrh	r3, [r4, r5]
	cmp	r2, r3
	popcc	{r4, r5, r6, pc}
	ldr	r0, .L1647+4
	bl	FtlMapBlkWriteDump_data
	mov	r0, #0
	bl	FtlGcFreeTempBlock
	mov	r3, #0
	str	r3, [r4, #3448]
	pop	{r4, r5, r6, pc}
.L1648:
	.align	2
.L1647:
	.word	.LANCHOR0
	.word	.LANCHOR0+3396
	.fnend
	.size	FtlGcPageRecovery, .-FtlGcPageRecovery
	.align	2
	.global	FtlPowerLostRecovery
	.syntax unified
	.arm
	.fpu softvfp
	.type	FtlPowerLostRecovery, %function
FtlPowerLostRecovery:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	push	{r4, r5, r6, lr}
	.save {r4, r5, r6, lr}
	mov	r5, #0
	ldr	r4, .L1651
	add	r6, r4, #24
	str	r5, [r4, #3480]
	mov	r0, r6
	add	r4, r4, #76
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
	pop	{r4, r5, r6, pc}
.L1652:
	.align	2
.L1651:
	.word	.LANCHOR0
	.fnend
	.size	FtlPowerLostRecovery, .-FtlPowerLostRecovery
	.align	2
	.global	Ftl_gc_temp_data_write_back
	.syntax unified
	.arm
	.fpu softvfp
	.type	Ftl_gc_temp_data_write_back, %function
Ftl_gc_temp_data_write_back:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	push	{r4, r5, r6, lr}
	.save {r4, r5, r6, lr}
	mov	r3, #0
	ldr	r4, .L1662
	mov	r5, #0
	mov	r6, #20
	mov	r2, r3
	ldr	r1, [r4, #3172]
	ldr	r0, [r4, #3296]
	bl	FlashProgPages
.L1654:
	ldr	r1, [r4, #3172]
	uxth	r3, r5
	cmp	r1, r3
	bhi	.L1657
	ldr	r0, [r4, #3296]
	bl	FtlGcBufFree
	ldrh	r3, [r4, #128]
	mov	r0, #0
	str	r0, [r4, #3172]
	cmp	r3, r0
	popne	{r4, r5, r6, pc}
	mov	r0, #1
	bl	FtlGcFreeTempBlock
	mov	r0, #1
	pop	{r4, r5, r6, pc}
.L1657:
	mul	r3, r6, r3
	ldr	r2, [r4, #3296]
	add	r5, r5, #1
	add	r1, r2, r3
	ldr	r2, [r2, r3]
	ldr	r0, [r1, #12]
	ldr	r1, [r1, #4]
	cmn	r2, #1
	ldrne	r2, [r0, #8]
	ldr	r0, [r0, #12]
	bl	FtlGcUpdatePage
	b	.L1654
.L1663:
	.align	2
.L1662:
	.word	.LANCHOR0
	.fnend
	.size	Ftl_gc_temp_data_write_back, .-Ftl_gc_temp_data_write_back
	.align	2
	.global	Ftl_get_new_temp_ppa
	.syntax unified
	.arm
	.fpu softvfp
	.type	Ftl_get_new_temp_ppa, %function
Ftl_get_new_temp_ppa:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	push	{r4, r5, r6, lr}
	.save {r4, r5, r6, lr}
	movw	r3, #65535
	ldr	r4, .L1668
	ldrh	r2, [r4, #124]
	cmp	r2, r3
	beq	.L1665
	ldrh	r3, [r4, #128]
	cmp	r3, #0
	bne	.L1666
.L1665:
	mov	r0, #0
	mov	r5, #0
	bl	FtlGcFreeTempBlock
	ldr	r0, .L1668+4
	strb	r5, [r4, #132]
	bl	allocate_data_superblock
	ldr	r3, .L1668+8
	strh	r5, [r3]	@ movhi
	movw	r3, #3208
	strh	r5, [r4, r3]	@ movhi
	bl	l2p_flush
	mov	r0, r5
	bl	FtlEctTblFlush
	bl	FtlVpcTblFlush
.L1666:
	ldr	r0, .L1668+4
	pop	{r4, r5, r6, lr}
	b	get_new_active_ppa
.L1669:
	.align	2
.L1668:
	.word	.LANCHOR0
	.word	.LANCHOR0+124
	.word	.LANCHOR0+3200
	.fnend
	.size	Ftl_get_new_temp_ppa, .-Ftl_get_new_temp_ppa
	.align	2
	.global	rk_ftl_garbage_collect
	.syntax unified
	.arm
	.fpu softvfp
	.type	rk_ftl_garbage_collect, %function
rk_ftl_garbage_collect:
	.fnstart
	@ args = 0, pretend = 0, frame = 40
	@ frame_needed = 0, uses_anonymous_args = 0
	ldr	r3, .L1800
	push	{r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.save {r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.pad #52
	sub	sp, sp, #52
	str	r0, [sp, #20]
	ldr	r3, [r3]
	str	r3, [sp, #44]
	ldr	r3, .L1800+4
	ldr	r0, [r3, #3276]
	cmp	r0, #0
	movne	r0, #0
	bne	.L1670
	add	r2, r3, #2528
	ldrh	r2, [r2]
	cmp	r2, #47
	bls	.L1670
	ldr	r2, .L1800+8
	mov	r4, r3
	ldrh	r1, [r2, #4]
	movw	r2, #65535
	cmp	r1, r2
	bne	.L1672
.L1675:
	ldrh	r3, [r4, #220]
	movw	r2, #65535
	cmp	r3, r2
	bne	.L1673
.L1674:
	bl	FtlReadRefresh
	ldr	r3, [r4, #3160]
	movw	r6, #65535
	ldr	r2, [sp, #20]
	add	r3, r3, #1
	add	r3, r3, r2, lsl #7
	ldrh	r2, [r4, #172]
	str	r3, [r4, #3160]
	cmp	r2, r6
	bne	.L1676
	ldrh	r6, [r4, #124]
	cmp	r6, r2
	movne	r6, r2
	bne	.L1676
	ldrh	r5, [r4, #222]
	cmp	r5, r6
	bne	.L1676
	ldrh	r2, [r4, #224]
	cmp	r2, #24
	movcc	r2, #5120
	movcs	r2, #1024
	cmp	r3, r2
	movls	r6, r5
	bls	.L1676
	ldr	r3, .L1800+12
	mov	r8, #0
	str	r8, [r4, #3160]
	strh	r8, [r3]	@ movhi
	bl	GetSwlReplaceBlock
	cmp	r0, r5
	mov	r6, r0
	movne	r5, r0
	bne	.L1678
	movw	r7, #3158
	ldrh	r2, [r4, #224]
	ldrh	r3, [r4, r7]
	cmp	r2, r3
	bcs	.L1679
	mov	r0, #64
	bl	List_get_gc_head_node
	uxth	r3, r0
	cmp	r3, r6
	beq	.L1681
	mov	r0, r3
	ldr	r3, [r4, #72]
	lsl	r0, r0, #1
	ldrh	r3, [r3, r0]
	cmp	r3, #7
	bhi	.L1682
	mov	r0, r8
	bl	List_get_gc_head_node
	uxth	r5, r0
	mov	r3, #128
	strh	r3, [r4, r7]	@ movhi
	cmp	r5, r6
	bne	.L1678
.L1681:
	bl	FtlGcReFreshBadBlk
.L1676:
	movw	r1, #65535
	ldr	r2, [sp, #20]
	sub	r3, r6, r1
	clz	r3, r3
	lsr	r3, r3, #5
	cmp	r2, #0
	movne	r2, #0
	andeq	r2, r3, #1
	cmp	r2, #0
	beq	.L1684
	ldrh	r3, [r4, #224]
	cmp	r3, #24
	movhi	r5, #1
	bhi	.L1685
	movw	r2, #302
	cmp	r3, #16
	ldrh	r5, [r4, r2]
	lsrhi	r5, r5, #5
	bhi	.L1685
	cmp	r3, #12
	lsrhi	r5, r5, #4
	bhi	.L1685
	cmp	r3, #8
	lsrhi	r5, r5, #2
.L1685:
	movw	r2, #3156
	ldrh	r1, [r4, r2]
	cmp	r1, r3
	bcs	.L1689
	ldrh	r3, [r4, #124]
	movw	r1, #65535
	cmp	r3, r1
	bne	.L1690
	ldrh	r1, [r4, #222]
	cmp	r1, r3
	bne	.L1690
	ldr	r3, .L1800+12
	ldrh	r0, [r3]
	cmp	r0, #0
	bne	.L1691
	ldr	r3, [r4, #2556]
	ldr	r1, [r4, #2548]
	add	r3, r3, r3, lsl #1
	cmp	r1, r3, lsr #2
	movcs	r3, #18
	bcs	.L1794
.L1691:
	ldr	r3, .L1800+16
	movw	r2, #3156
	ldrh	r3, [r3]
	add	r3, r3, r3, lsl #1
	asr	r3, r3, #2
.L1794:
	strh	r3, [r4, r2]	@ movhi
	mov	r3, #0
	str	r3, [r4, #3168]
.L1670:
	ldr	r3, .L1800
	ldr	r2, [sp, #44]
	ldr	r3, [r3]
	cmp	r2, r3
	beq	.L1748
	bl	__stack_chk_fail
.L1672:
	ldrh	r3, [r3, #124]
	cmp	r3, r2
	beq	.L1675
	mov	r0, #1
	bl	FtlGcFreeTempBlock
	cmp	r0, #0
	beq	.L1675
	mov	r0, #1
	b	.L1670
.L1673:
	ldrh	r1, [r4, #222]
	cmp	r1, r2
	strheq	r3, [r4, #222]	@ movhi
	mvneq	r3, #0
	strheq	r3, [r4, #220]	@ movhi
	b	.L1674
.L1682:
	mov	r3, #64
.L1793:
	strh	r3, [r4, r7]	@ movhi
	b	.L1681
.L1679:
	mov	r3, #80
	b	.L1793
.L1678:
	movw	r0, #3156
	ldr	r3, [r4, #72]
	ldrh	r0, [r4, r0]
	lsl	r1, r5, #1
	ldrh	r2, [r4, #224]
	mov	r6, r5
	ldrh	r3, [r3, r1]
	str	r0, [sp, #4]
	ldr	r0, [r4, #2536]
	ldrh	r1, [r0, r1]
	ldr	r0, .L1800+20
	str	r1, [sp]
	mov	r1, r5
	bl	sftl_printk
	b	.L1681
.L1690:
	ldr	r3, .L1800+16
	movw	r2, #3156
	ldrh	r3, [r3]
	add	r3, r3, r3, lsl #1
	asr	r3, r3, #2
	strh	r3, [r4, r2]	@ movhi
.L1689:
	movw	r3, #3210
	movw	r6, #65535
	ldrh	r3, [r4, r3]
	cmp	r3, #0
	addne	r5, r5, #32
	uxthne	r5, r5
.L1695:
	ldrh	r3, [r4, #172]
	movw	r2, #65535
	cmp	r3, r2
	bne	.L1704
	cmp	r6, r3
	strhne	r6, [r4, #172]	@ movhi
	bne	.L1706
	ldrh	r3, [r4, #222]
	cmp	r3, r6
	beq	.L1706
	ldr	r2, [r4, #72]
	lsl	r3, r3, #1
	ldrh	r3, [r2, r3]
	cmp	r3, #0
	mvneq	r3, #0
	strheq	r3, [r4, #222]	@ movhi
	ldrh	r3, [r4, #222]
	strh	r3, [r4, #172]	@ movhi
	mvn	r3, #0
	strh	r3, [r4, #222]	@ movhi
.L1706:
	ldrh	r0, [r4, #172]
	mov	r3, #0
	strb	r3, [r4, #180]
	movw	r3, #65535
	cmp	r0, r3
	beq	.L1704
	bl	IsBlkInGcList
	cmp	r0, #0
	mvnne	r3, #0
	strhne	r3, [r4, #172]	@ movhi
	movw	r3, #65535
	ldrh	r2, [r4, #172]
	cmp	r2, r3
	beq	.L1704
	ldr	r0, .L1800+24
	bl	make_superblock
	mov	r3, #0
	movw	r2, #3666
	strh	r3, [r4, r2]	@ movhi
	strh	r3, [r4, #174]	@ movhi
	strb	r3, [r4, #178]
	ldrh	r3, [r4, #172]
	ldr	r2, [r4, #72]
	lsl	r3, r3, #1
	ldrh	r2, [r2, r3]
	movw	r3, #3668
	strh	r2, [r4, r3]	@ movhi
.L1704:
	ldrh	r3, [r4, #172]
	ldrh	r2, [r4, #24]
	cmp	r2, r3
	beq	.L1710
	ldrh	r2, [r4, #76]
	cmp	r2, r3
	beq	.L1710
	ldrh	r2, [r4, #124]
	cmp	r2, r3
	bne	.L1711
.L1710:
	mvn	r3, #0
	strh	r3, [r4, #172]	@ movhi
.L1711:
	ldr	r7, .L1800+4
	mov	r4, r7
.L1746:
	ldrh	r2, [r7, #172]
	movw	r3, #65535
	cmp	r2, r3
	bne	.L1712
	ldr	fp, .L1800+28
	mov	r3, #0
	ldr	r10, .L1800+32
	str	r3, [r7, #3168]
	mov	r9, fp
.L1713:
	ldrh	r8, [fp]
	mov	r0, r8
	bl	List_get_gc_head_node
	uxth	r2, r0
	movw	r1, #65535
	cmp	r2, r1
	strh	r2, [r4, #172]	@ movhi
	bne	.L1714
	mov	r3, #0
	mov	r0, #8
	strh	r3, [fp]	@ movhi
	b	.L1670
.L1684:
	ldrh	r7, [r4, #124]
	cmp	r7, r1
	bne	.L1761
	ldrh	r1, [r4, #222]
	cmp	r1, r7
	movne	r3, #0
	andeq	r3, r3, #1
	cmp	r3, #0
	beq	.L1761
	ldrh	r3, [r4, #172]
	cmp	r3, r7
	beq	.L1696
.L1701:
	mov	r6, r7
.L1761:
	mov	r5, #1
	b	.L1695
.L1696:
	str	r2, [r4, #3168]
	movw	r2, #3156
	ldrh	r1, [r4, #224]
	ldrh	r3, [r4, r2]
	ldr	r5, .L1800+12
	cmp	r1, r3
	bls	.L1697
	ldrh	r3, [r5]
	cmp	r3, #0
	bne	.L1698
	ldr	r3, [r4, #2556]
	ldr	r1, [r4, #2548]
	add	r3, r3, r3, lsl #1
	cmp	r1, r3, lsr #2
	movcs	r3, #18
	bcs	.L1795
.L1698:
	ldr	r3, .L1800+16
	movw	r2, #3156
	ldrh	r3, [r3]
	add	r3, r3, r3, lsl #1
	asr	r3, r3, #2
.L1795:
	strh	r3, [r4, r2]	@ movhi
	bl	FtlReadRefresh
	mov	r0, #0
	bl	List_get_gc_head_node
	uxth	r0, r0
	ldr	r3, [r4, #72]
	lsl	r0, r0, #1
	ldrh	r3, [r3, r0]
	cmp	r3, #4
	bls	.L1697
.L1797:
	ldrh	r0, [r5]
	b	.L1670
.L1697:
	ldrh	r0, [r5]
	cmp	r0, #0
	bne	.L1701
	ldr	r8, .L1800+16
	movw	r2, #3156
	ldrh	r6, [r8]
	sub	r8, r8, #2320
	add	r3, r6, r6, lsl #1
	asr	r3, r3, #2
	strh	r3, [r4, r2]	@ movhi
	bl	List_get_gc_head_node
	uxth	r0, r0
	ldr	r3, [r4, #72]
	ldrh	r1, [r8]
	lsl	r0, r0, #1
	ldrh	r2, [r3, r0]
	ldrh	r3, [r4, #232]
	mul	r3, r3, r1
	add	r3, r3, r3, lsr #31
	cmp	r2, r3, asr #1
	ble	.L1702
	ldrh	r3, [r4, #224]
	sub	r6, r6, #1
	cmp	r3, r6
	blt	.L1702
	bl	FtlReadRefresh
	b	.L1797
.L1702:
	cmp	r2, #0
	bne	.L1701
	movw	r0, #65535
	bl	decrement_vpc_count
	ldrh	r0, [r4, #224]
	add	r0, r0, #1
	b	.L1670
.L1714:
	str	r0, [sp, #16]
	mov	r0, r2
	str	r2, [sp, #12]
	add	r8, r8, #1
	bl	IsBlkInGcList
	cmp	r0, #0
	ldr	r2, [sp, #12]
	ldr	r3, [sp, #16]
	strhne	r8, [fp]	@ movhi
	bne	.L1713
	uxth	r3, r3
	ldrh	lr, [r4, #232]
	ldr	r0, [r4, #72]
	uxth	r8, r8
	lsl	r1, r3, #1
	ldrh	r3, [r10]
	strh	r8, [fp]	@ movhi
	ldrh	ip, [r0, r1]
	mul	r3, lr, r3
	add	lr, r3, r3, lsr #31
	cmp	ip, lr, asr #1
	bgt	.L1717
	cmp	r8, #48
	cmphi	ip, #8
	bls	.L1718
	ldr	ip, .L1800+36
	ldrh	ip, [ip]
	cmp	ip, #35
	bhi	.L1718
.L1717:
	mov	ip, #0
	strh	ip, [r9]	@ movhi
.L1718:
	ldrh	r1, [r0, r1]
	movw	r0, #65535
	cmp	r3, r1
	cmple	r6, r0
	bne	.L1719
	ldrh	r3, [r9]
	cmp	r3, #3
	bhi	.L1719
	mvn	r3, #0
	strh	r3, [r4, #172]	@ movhi
	mov	r3, #0
	strh	r3, [r9]	@ movhi
.L1799:
	ldr	r3, .L1800+12
	b	.L1798
.L1719:
	cmp	r1, #0
	bne	.L1720
	movw	r0, #65535
	bl	decrement_vpc_count
	ldrh	r3, [r9]
	add	r3, r3, #1
	strh	r3, [r9]	@ movhi
	b	.L1713
.L1720:
	mov	r3, #0
	strb	r3, [r4, #180]
	ldrh	r3, [r4, #24]
	cmp	r3, r2
	bne	.L1721
	movw	r2, #717
	ldr	r1, .L1800+40
	ldr	r0, .L1800+44
	bl	sftl_printk
.L1721:
	ldrh	r2, [r4, #172]
	ldrh	r3, [r4, #76]
	cmp	r2, r3
	bne	.L1722
	movw	r2, #718
	ldr	r1, .L1800+40
	ldr	r0, .L1800+44
	bl	sftl_printk
.L1722:
	ldrh	r2, [r4, #172]
	ldrh	r3, [r4, #124]
	cmp	r2, r3
	bne	.L1723
	movw	r2, #719
	ldr	r1, .L1800+40
	ldr	r0, .L1800+44
	bl	sftl_printk
.L1723:
	ldr	r0, .L1800+24
	bl	make_superblock
	ldrh	r2, [r4, #172]
	mov	r3, #0
	ldr	r1, .L1800+48
	ldr	r0, [r4, #72]
	lsl	r2, r2, #1
	strh	r3, [r1]	@ movhi
	ldrh	r2, [r0, r2]
	strh	r3, [r4, #174]	@ movhi
	strb	r3, [r4, #178]
	strh	r2, [r1, #2]	@ movhi
.L1712:
	mov	r3, #1
	str	r3, [r4, #3276]
	ldr	r3, .L1800+32
	ldrh	r3, [r3]
	str	r3, [sp, #12]
	ldr	r3, [sp, #20]
	cmp	r3, #0
	beq	.L1724
	ldr	r2, [sp, #12]
	ldrh	r3, [r4, #232]
	ldr	r1, [r4, #72]
	mul	r3, r2, r3
	ldrh	r2, [r4, #172]
	lsl	r2, r2, #1
	ldrh	r2, [r1, r2]
	sub	r3, r3, r2
	add	r2, r3, #3
	cmp	r3, #0
	movlt	r3, r2
	add	r5, r5, r3, asr #2
	uxth	r5, r5
.L1724:
	ldrh	r3, [r4, #174]
	ldr	r1, [sp, #12]
	add	r2, r3, r5
	cmp	r2, r1
	movgt	r2, r1
	subgt	r5, r2, r3
	mov	r3, #0
	uxthgt	r5, r5
.L1796:
	str	r3, [sp, #16]
	ldrh	r3, [sp, #16]
	cmp	r5, r3
	bls	.L1734
	ldr	r3, [sp, #16]
	movw	r9, #65535
	ldrh	r8, [r7, #174]
	mov	r10, #20
	ldrh	ip, [r7, #232]
	ldr	r0, [r7, #3188]
	add	r8, r8, r3
	mov	r3, #0
	ldr	r1, .L1800+52
	mov	fp, r3
	b	.L1735
.L1728:
	ldrh	r2, [r1, #2]!
	add	r3, r3, #1
	cmp	r2, r9
	mlane	lr, r10, fp, r0
	orrne	r2, r8, r2, lsl #10
	strne	r2, [lr, #4]
	addne	r2, fp, #1
	uxthne	fp, r2
.L1735:
	uxth	r2, r3
	cmp	ip, r2
	bhi	.L1728
	mov	r10, #0
	ldrb	r2, [r7, #180]	@ zero_extendqisi2
	mov	r1, fp
	bl	FlashReadPages
.L1729:
	uxth	r3, r10
	cmp	fp, r3
	ldrls	r3, [sp, #16]
	addls	r3, r3, #1
	bls	.L1796
.L1733:
	mov	r8, #20
	ldr	r3, [r7, #3188]
	mul	r8, r8, r10
	add	r2, r3, r8
	ldr	r3, [r3, r8]
	cmn	r3, #1
	beq	.L1730
	ldr	r9, [r2, #12]
	movw	r3, #61589
	ldrh	r2, [r9]
	cmp	r2, r3
	bne	.L1730
	ldr	r3, [r9, #8]
	cmn	r3, #1
	bne	.L1731
	movw	r2, #753
	ldr	r1, .L1800+40
	ldr	r0, .L1800+44
	str	r3, [sp, #24]
	bl	sftl_printk
	ldr	r3, [sp, #24]
.L1731:
	mov	r2, #0
	add	r1, sp, #36
	mov	r0, r3
	bl	log2phys
	ldr	r1, [r4, #3188]
	ldr	r3, [sp, #36]
	add	r1, r1, r8
	ldr	r2, [r1, #4]
	cmp	r2, r3
	bne	.L1730
	ldr	r3, .L1800+48
	ldr	r2, .L1800+48
	ldr	r0, [r4, #3172]
	ldrh	r3, [r3]
	ldr	r1, [r1, #16]
	add	r3, r3, #1
	strh	r3, [r2]	@ movhi
	mov	r2, #20
	ldr	r3, [r4, #3296]
	str	r2, [sp, #28]
	mla	r3, r2, r0, r3
	str	r1, [r3, #16]
	str	r3, [sp, #24]
	bl	Ftl_get_new_temp_ppa
	ldr	r3, [sp, #24]
	ldr	r1, [r4, #3296]
	ldr	r2, [sp, #28]
	str	r0, [r3, #4]
	ldr	r3, [r4, #3172]
	mla	r3, r2, r3, r1
	ldr	r2, [r4, #3188]
	add	r2, r2, r8
	ldr	r1, [r2, #8]
	str	r1, [r3, #8]
	mov	r1, #1
	ldr	r2, [r2, #12]
	str	r2, [r3, #12]
	ldr	r3, [sp, #36]
	str	r3, [r9, #12]
	ldrh	r3, [r4, #124]
	strh	r3, [r9, #2]	@ movhi
	ldr	r3, [r4, #2596]
	ldr	r0, [r4, #3188]
	str	r3, [r9, #4]
	ldr	r3, [r4, #3172]
	add	r0, r0, r8
	add	r3, r3, #1
	str	r3, [r4, #3172]
	bl	FtlGcBufAlloc
	ldrb	r2, [r4, #131]	@ zero_extendqisi2
	ldr	r3, [r4, #3172]
	cmp	r2, r3
	beq	.L1732
	ldrh	r3, [r4, #128]
	cmp	r3, #0
	bne	.L1730
.L1732:
	bl	Ftl_gc_temp_data_write_back
	cmp	r0, #0
	beq	.L1730
	ldr	r3, .L1800+4
	mov	r2, #0
	mvn	r1, #0
	str	r2, [r3, #3276]
	strh	r1, [r3, #172]	@ movhi
	strh	r2, [r3, #174]	@ movhi
	add	r3, r3, #3664
.L1798:
	ldrh	r0, [r3]
	b	.L1670
.L1730:
	add	r10, r10, #1
	b	.L1729
.L1734:
	ldrh	r3, [r7, #174]
	add	r5, r5, r3
	ldr	r3, [sp, #12]
	uxth	r5, r5
	cmp	r3, r5
	strh	r5, [r7, #174]	@ movhi
	bhi	.L1736
	ldr	r3, [r7, #3172]
	cmp	r3, #0
	beq	.L1737
	bl	Ftl_gc_temp_data_write_back
	cmp	r0, #0
	movne	r3, #0
	strne	r3, [r7, #3276]
	bne	.L1799
.L1737:
	ldr	r3, .L1800+48
	ldrh	r5, [r3]
	cmp	r5, #0
	bne	.L1738
	ldrh	r3, [r7, #172]
	ldr	r2, [r7, #72]
	lsl	r3, r3, #1
	ldrh	r3, [r2, r3]
	cmp	r3, #0
	beq	.L1738
.L1739:
	ldr	r3, [r7, #2556]
	cmp	r5, r3
	bcs	.L1744
	mov	r2, #0
	add	r1, sp, #40
	mov	r0, r5
	bl	log2phys
	ldr	r0, [sp, #40]
	cmn	r0, #1
	beq	.L1740
	ubfx	r0, r0, #10, #16
	bl	P2V_block_in_plane
	ldrh	r3, [r7, #172]
	cmp	r3, r0
	bne	.L1740
.L1744:
	ldr	r3, [r7, #2556]
	cmp	r5, r3
	bcc	.L1738
	ldrh	r3, [r7, #172]
	mov	r1, #0
	ldr	r2, [r7, #72]
	lsl	r3, r3, #1
	strh	r1, [r2, r3]	@ movhi
	ldrh	r0, [r7, #172]
	bl	update_vpc_list
	bl	l2p_flush
	bl	FtlVpcTblFlush
.L1738:
	mvn	r3, #0
	strh	r3, [r7, #172]	@ movhi
.L1736:
	mov	r3, #0
	str	r3, [r7, #3276]
	ldrh	r3, [r7, #224]
	cmp	r3, #2
	bhi	.L1745
	ldr	r3, .L1800+32
	ldrh	r5, [r3]
	b	.L1746
.L1740:
	add	r5, r5, #1
	b	.L1739
.L1745:
	ldr	r2, .L1800+12
	ldrh	r0, [r2]
	cmp	r0, #0
	addeq	r0, r3, #1
	b	.L1670
.L1748:
	add	sp, sp, #52
	@ sp needed
	pop	{r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L1801:
	.align	2
.L1800:
	.word	__stack_chk_guard
	.word	.LANCHOR0
	.word	.LANCHOR2
	.word	.LANCHOR0+3664
	.word	.LANCHOR0+2624
	.word	.LC124
	.word	.LANCHOR0+172
	.word	.LANCHOR0+3164
	.word	.LANCHOR0+302
	.word	.LANCHOR0+3200
	.word	.LANCHOR1+568
	.word	.LC8
	.word	.LANCHOR0+3666
	.word	.LANCHOR0+186
	.fnend
	.size	rk_ftl_garbage_collect, .-rk_ftl_garbage_collect
	.align	2
	.global	FtlRead
	.syntax unified
	.arm
	.fpu softvfp
	.type	FtlRead, %function
FtlRead:
	.fnstart
	@ args = 0, pretend = 0, frame = 56
	@ frame_needed = 0, uses_anonymous_args = 0
	push	{r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.save {r4, r5, r6, r7, r8, r9, r10, fp, lr}
	mov	r10, r3
	ldr	r3, .L1841
	cmp	r0, #16
	.pad #60
	sub	sp, sp, #60
	mov	r5, r1
	str	r2, [sp, #4]
	ldr	r3, [r3]
	str	r3, [sp, #52]
	bne	.L1803
	mov	r2, r10
	ldr	r1, [sp, #4]
	add	r0, r5, #256
	bl	FtlVendorPartRead
	mov	r6, r0
.L1802:
	ldr	r3, .L1841
	mov	r0, r6
	ldr	r2, [sp, #52]
	ldr	r3, [r3]
	cmp	r2, r3
	beq	.L1824
	bl	__stack_chk_fail
.L1803:
	ldr	r4, .L1841+4
	ldr	r2, [sp, #4]
	ldr	r3, [r4, #340]
	cmp	r2, r3
	cmpls	r1, r3
	movcs	r9, #1
	movcc	r9, #0
	bcs	.L1826
	add	r2, r1, r2
	cmp	r3, r2
	str	r2, [sp, #12]
	bcc	.L1826
	ldr	r3, .L1841+8
	ldr	r6, [r3]
	cmn	r6, #1
	beq	.L1802
	movw	r3, #258
	mov	r0, r5
	ldrh	r6, [r4, r3]
	mov	r8, r9
	mov	r1, r6
	bl	__aeabi_uidiv
	ldr	r3, [sp, #12]
	mov	r1, r6
	str	r0, [sp, #16]
	mov	r6, r9
	sub	r0, r3, #1
	bl	__aeabi_uidiv
	ldr	r3, [sp, #16]
	ldr	r2, [sp, #4]
	ldr	r7, [sp, #16]
	rsb	r3, r3, #1
	str	r0, [sp, #20]
	add	r3, r3, r0
	str	r9, [sp, #28]
	str	r3, [sp, #8]
	ldr	r3, [r4, #2588]
	str	r9, [sp, #24]
	add	r3, r3, r2
	ldr	r2, [sp, #8]
	str	r3, [r4, #2588]
	ldr	r3, [r4, #2560]
	add	r3, r3, r2
	str	r3, [r4, #2560]
.L1805:
	ldr	r3, [sp, #8]
	cmp	r3, #0
	bne	.L1822
	ldr	r3, .L1841+4
	movw	r2, #3210
	ldrh	r2, [r3, r2]
	cmp	r2, #0
	bne	.L1823
	ldrh	r3, [r3, #224]
	cmp	r3, #31
	bhi	.L1802
.L1823:
	mov	r1, #1
	mov	r0, #0
	bl	rk_ftl_garbage_collect
	b	.L1802
.L1822:
	mov	r2, #0
	add	r1, sp, #48
	mov	r0, r7
	bl	log2phys
	ldr	r3, [sp, #48]
	cmn	r3, #1
	moveq	fp, #0
	beq	.L1807
	ldr	r2, [r4, #3292]
	mov	fp, #20
	mla	fp, fp, r8, r2
	str	r3, [fp, #4]
	ldr	r3, [sp, #16]
	cmp	r7, r3
	bne	.L1811
	ldr	r3, [r4, #3316]
	mov	r0, r5
	str	r3, [fp, #8]
	ldr	r3, .L1841+12
	ldrh	r3, [r3]
	mov	r1, r3
	str	r3, [sp, #28]
	bl	__aeabi_uidivmod
	ldr	r3, [sp, #28]
	str	r1, [sp, #24]
	sub	r2, r3, r1
	ldr	r1, [sp, #4]
	cmp	r1, r2
	movcc	r2, r1
	cmp	r2, r3
	str	r2, [sp, #28]
	streq	r10, [fp, #8]
.L1812:
	ldr	r3, .L1841+16
	ldr	r2, [r4, #3340]
	str	r7, [fp, #16]
	ldrh	r3, [r3]
	mul	r3, r8, r3
	add	r8, r8, #1
	bic	r3, r3, #3
	add	r3, r2, r3
	str	r3, [fp, #12]
	b	.L1810
.L1809:
	mla	r0, r0, r7, fp
	ldr	r2, [sp, #12]
	cmp	r5, r0
	movls	r3, #1
	movhi	r3, #0
	cmp	r2, r0
	movls	r3, #0
	cmp	r3, #0
	beq	.L1808
	sub	r0, r0, r5
	mov	r2, #512
	mov	r1, #0
	add	r0, r10, r0, lsl #9
	bl	ftl_memset
.L1808:
	add	fp, fp, #1
.L1807:
	ldr	r3, .L1841+12
	ldrh	r0, [r3]
	cmp	fp, r0
	bcc	.L1809
.L1810:
	ldr	r3, [sp, #8]
	add	r7, r7, #1
	subs	r3, r3, #1
	str	r3, [sp, #8]
	beq	.L1814
	ldrh	r3, [r4, #232]
	cmp	r8, r3, lsl #2
	bne	.L1805
.L1814:
	cmp	r8, #0
	beq	.L1805
	mov	r2, #0
	mov	r1, r8
	ldr	r0, [r4, #3292]
	mov	fp, #0
	bl	FlashReadPages
	lsl	r3, r9, #9
	str	r3, [sp, #40]
	ldr	r3, [sp, #24]
	lsl	r3, r3, #9
	str	r3, [sp, #32]
	ldr	r3, [sp, #28]
	lsl	r3, r3, #9
	str	r3, [sp, #36]
.L1821:
	mov	r3, #20
	ldr	r2, [r4, #3292]
	mul	r3, r3, fp
	ldr	r0, [sp, #16]
	add	r2, r2, r3
	ldr	r1, [r2, #16]
	cmp	r0, r1
	bne	.L1816
	ldr	r1, [r2, #8]
	ldr	r2, [r4, #3316]
	cmp	r1, r2
	bne	.L1817
	str	r3, [sp, #44]
	mov	r0, r10
	ldr	r3, [sp, #32]
	ldr	r2, [sp, #36]
	add	r1, r1, r3
.L1840:
	bl	ftl_memcpy
	ldr	r3, [sp, #44]
.L1817:
	ldr	r1, [r4, #3292]
	add	r2, r1, r3
	ldr	r0, [r2, #12]
	ldr	ip, [r2, #16]
	ldr	r0, [r0, #8]
	cmp	ip, r0
	ldrne	r0, [r4, #2716]
	addne	r0, r0, #1
	strne	r0, [r4, #2716]
	ldr	ip, [r1, r3]
	cmn	ip, #1
	ldreq	r0, [r4, #2716]
	moveq	r6, ip
	addeq	r0, r0, #1
	streq	r0, [r4, #2716]
	ldr	r3, [r1, r3]
	cmp	r3, #256
	bne	.L1820
	ldr	r0, [r2, #4]
	ubfx	r0, r0, #10, #16
	bl	P2V_block_in_plane
	bl	FtlGcRefreshBlock
.L1820:
	add	fp, fp, #1
	cmp	r8, fp
	bne	.L1821
	mov	r8, #0
	b	.L1805
.L1811:
	ldr	r3, [sp, #20]
	cmp	r7, r3
	bne	.L1813
	ldr	r3, [r4, #3320]
	ldr	r1, [sp, #12]
	str	r3, [fp, #8]
	ldr	r3, .L1841+12
	ldrh	r2, [r3]
	mul	r3, r2, r7
	sub	r9, r1, r3
	cmp	r2, r9
	bne	.L1812
.L1839:
	sub	r3, r3, r5
	add	r3, r10, r3, lsl #9
	str	r3, [fp, #8]
	b	.L1812
.L1813:
	ldr	r3, .L1841+12
	ldrh	r3, [r3]
	mul	r3, r7, r3
	b	.L1839
.L1816:
	ldr	r0, [sp, #20]
	cmp	r0, r1
	bne	.L1817
	ldr	r1, [r2, #8]
	ldr	r2, [r4, #3320]
	cmp	r1, r2
	bne	.L1817
	str	r3, [sp, #44]
	ldr	r3, .L1841+12
	ldr	r2, [sp, #40]
	ldrh	r0, [r3]
	ldr	r3, [sp, #20]
	mul	r0, r3, r0
	sub	r0, r0, r5
	add	r0, r10, r0, lsl #9
	b	.L1840
.L1826:
	mvn	r6, #0
	b	.L1802
.L1824:
	add	sp, sp, #60
	@ sp needed
	pop	{r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L1842:
	.align	2
.L1841:
	.word	__stack_chk_guard
	.word	.LANCHOR0
	.word	.LANCHOR2
	.word	.LANCHOR0+258
	.word	.LANCHOR0+312
	.fnend
	.size	FtlRead, .-FtlRead
	.align	2
	.global	sftl_read
	.syntax unified
	.arm
	.fpu softvfp
	.type	sftl_read, %function
sftl_read:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	@ link register save eliminated.
	mov	r3, r2
	mov	r2, r1
	mov	r1, r0
	mov	r0, #0
	b	FtlRead
	.fnend
	.size	sftl_read, .-sftl_read
	.align	2
	.global	FtlWrite
	.syntax unified
	.arm
	.fpu softvfp
	.type	FtlWrite, %function
FtlWrite:
	.fnstart
	@ args = 0, pretend = 0, frame = 72
	@ frame_needed = 0, uses_anonymous_args = 0
	push	{r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.save {r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.pad #76
	sub	sp, sp, #76
	str	r3, [sp, #12]
	cmp	r0, #16
	mov	r10, r1
	ldr	r3, .L1899
	str	r2, [sp, #8]
	ldr	r3, [r3]
	str	r3, [sp, #68]
	bne	.L1845
	ldr	r2, [sp, #12]
	add	r0, r10, #256
	ldr	r1, [sp, #8]
	bl	FtlVendorPartWrite
.L1844:
	ldr	r3, .L1899
	ldr	r2, [sp, #68]
	ldr	r3, [r3]
	cmp	r2, r3
	beq	.L1881
	bl	__stack_chk_fail
.L1845:
	ldr	r4, .L1899+4
	ldr	r2, [sp, #8]
	ldr	r3, [r4, #340]
	cmp	r2, r3
	cmpls	r1, r3
	bcs	.L1883
	add	r6, r1, r2
	cmp	r3, r6
	bcc	.L1883
	ldr	r5, .L1899+8
	ldr	r0, [r5]
	cmn	r0, #1
	beq	.L1844
	mov	r3, #2048
	mov	r0, r10
	str	r3, [r4, #3672]
	movw	r3, #258
	ldrh	r7, [r4, r3]
	mov	r1, r7
	bl	__aeabi_uidiv
	mov	r1, r7
	str	r0, [sp, #4]
	sub	r0, r6, #1
	bl	__aeabi_uidiv
	ldr	r2, [sp, #4]
	str	r0, [sp, #20]
	sub	r3, r0, r2
	ldr	r2, [sp, #8]
	add	fp, r3, #1
	str	r3, [sp, #24]
	ldr	r3, [r4, #2584]
	add	r3, r3, r2
	str	r3, [r4, #2584]
	ldr	r3, [r4, #2568]
	add	r3, r3, fp
	str	r3, [r4, #2568]
	ldr	r3, [r5, #8]
	cmp	r3, #0
	addeq	r4, r4, #24
	beq	.L1847
	ldrh	r3, [r4, #28]
	cmp	r3, #0
	addne	r4, r4, #24
	addeq	r4, r4, #76
.L1847:
	ldr	r7, [sp, #4]
	ldr	r5, .L1899+4
.L1848:
	cmp	fp, #0
	bne	.L1876
	ldr	r1, [sp, #24]
	mov	r0, fp
	bl	rk_ftl_garbage_collect
	ldrh	r3, [r5, #224]
	cmp	r3, #5
	movls	r4, #256
	ldrls	r6, .L1899+12
	bls	.L1880
.L1879:
	mov	r0, #0
	b	.L1844
.L1876:
	ldrb	r2, [r4, #6]	@ zero_extendqisi2
	ldrh	r3, [r5, #232]
	cmp	r2, r3
	bcc	.L1849
	movw	r2, #1041
	ldr	r1, .L1899+16
	ldr	r0, .L1899+20
	bl	sftl_printk
.L1849:
	ldrh	r2, [r4, #4]
	cmp	r2, #0
	bne	.L1850
	ldr	r3, .L1899+24
	ldr	r6, .L1899+8
	cmp	r4, r3
	bne	.L1851
	ldrh	r8, [r5, #80]
	cmp	r8, #0
	bne	.L1852
	add	r0, r4, #52
	bl	allocate_new_data_superblock
	str	r8, [r6, #8]
.L1852:
	ldr	r0, .L1899+24
	bl	allocate_new_data_superblock
	ldr	r4, .L1899+24
	ldr	r2, [r6, #8]
	add	r3, r4, #52
	cmp	r2, #0
	movne	r4, r3
.L1853:
	ldrh	r3, [r4, #4]
	cmp	r3, #0
	bne	.L1850
	mov	r0, r4
	bl	allocate_new_data_superblock
.L1850:
	ldrb	r2, [r4, #7]	@ zero_extendqisi2
	ldrh	r3, [r4, #4]
	lsl	r2, r2, #2
	cmp	r3, fp
	movcs	r3, fp
	cmp	r2, r3
	movcc	r3, r2
	ldrb	r2, [r4, #6]	@ zero_extendqisi2
	str	r3, [sp, #36]
	ldrh	r3, [r5, #232]
	cmp	r2, r3
	bcc	.L1854
	movw	r2, #1074
	ldr	r1, .L1899+16
	ldr	r0, .L1899+20
	bl	sftl_printk
.L1854:
	mov	r3, #0
.L1898:
	str	r3, [sp]
	ldr	r3, [sp]
	ldr	r2, [sp, #36]
	cmp	r3, r2
	bne	.L1874
.L1856:
	mov	r3, r4
	mov	r2, #0
	ldr	r1, [sp]
	ldr	r0, [r5, #3300]
	bl	FtlProgPages
	ldr	r3, [sp]
	cmp	r3, fp
	bls	.L1875
	mov	r2, #1152
	ldr	r1, .L1899+16
	ldr	r0, .L1899+20
	bl	sftl_printk
.L1875:
	ldr	r3, [sp]
	sub	fp, fp, r3
	b	.L1848
.L1851:
	str	r2, [r6, #8]
	ldrh	r2, [r5, #28]
	cmp	r2, #0
	movne	r4, r3
	bne	.L1850
	mov	r0, r4
	bl	allocate_new_data_superblock
	b	.L1853
.L1874:
	ldrh	r3, [r4, #4]
	cmp	r3, #0
	beq	.L1856
	mov	r2, #0
	add	r1, sp, #44
	mov	r0, r7
	mov	r8, #20
	bl	log2phys
	mov	r0, r4
	bl	get_new_active_ppa
	ldr	r2, .L1899+28
	ldr	r1, [sp]
	ldr	r3, [sp]
	ldrh	r2, [r2]
	mul	r8, r8, r3
	ldr	r3, [r5, #3300]
	mul	r1, r2, r1
	add	r3, r3, r8
	str	r0, [r3, #4]
	bic	r1, r1, #3
	str	r7, [r3, #16]
	str	r1, [sp, #28]
	ldr	r0, [sp, #28]
	ldr	r1, [r5, #3340]
	add	r9, r1, r0
	str	r1, [sp, #32]
	str	r9, [r3, #12]
	mov	r1, #0
	mov	r0, r9
	bl	ftl_memset
	ldr	r3, [sp, #4]
	ldr	r2, [sp, #20]
	cmp	r7, r2
	cmpne	r7, r3
	bne	.L1857
	cmp	r7, r3
	bne	.L1858
	ldr	r3, .L1899+32
	mov	r0, r10
	ldrh	r6, [r3]
	mov	r1, r6
	bl	__aeabi_uidivmod
	ldr	r3, [sp, #8]
	sub	r6, r6, r1
	str	r1, [sp, #16]
	cmp	r6, r3
	movcs	r6, r3
.L1859:
	ldr	r3, .L1899+32
	ldrh	r3, [r3]
	cmp	r6, r3
	ldr	r3, [sp, #4]
	bne	.L1860
	cmp	r7, r3
	ldr	r3, [r5, #3300]
	add	r8, r3, r8
	ldreq	r3, [sp, #12]
	beq	.L1895
	mul	r6, r6, r7
	ldr	r3, [sp, #12]
	sub	r6, r6, r10
	add	r6, r3, r6, lsl #9
	str	r6, [r8, #8]
.L1862:
	ldrb	r2, [r4, #6]	@ zero_extendqisi2
	ldrh	r3, [r5, #232]
	cmp	r2, r3
	bcc	.L1871
	movw	r2, #1143
	ldr	r1, .L1899+16
	ldr	r0, .L1899+20
	bl	sftl_printk
.L1871:
	ldr	r3, .L1899+36
	ldr	r2, [sp, #32]
	ldr	r1, [sp, #28]
	strh	r3, [r2, r1]	@ movhi
	ldr	r3, [r5, #2596]
	str	r7, [r9, #8]
	add	r7, r7, #1
	str	r3, [r9, #4]
	add	r3, r3, #1
	cmn	r3, #1
	moveq	r3, #0
	str	r3, [r5, #2596]
	ldr	r3, [sp, #44]
	str	r3, [r9, #12]
	ldrh	r3, [r4]
	strh	r3, [r9, #2]	@ movhi
	ldr	r3, [sp]
	add	r3, r3, #1
	b	.L1898
.L1858:
	ldr	r3, [sp, #8]
	add	r6, r10, r3
	ldr	r3, .L1899+32
	ldrh	r2, [r3]
	mov	r3, #0
	str	r3, [sp, #16]
	smulbb	r2, r2, r7
	sub	r6, r6, r2
	uxth	r6, r6
	b	.L1859
.L1860:
	cmp	r7, r3
	ldr	r3, [r5, #3300]
	ldreq	r2, [r5, #3316]
	ldrne	r2, [r5, #3320]
	add	r3, r3, r8
	str	r2, [r3, #8]
	ldr	r3, [sp, #44]
	cmn	r3, #1
	beq	.L1865
	str	r3, [sp, #52]
	mov	r1, #1
	ldr	r3, [r5, #3300]
	add	r0, sp, #48
	str	r7, [sp, #64]
	add	r3, r3, r8
	ldr	r2, [r3, #8]
	ldr	r3, [r3, #12]
	str	r2, [sp, #56]
	mov	r2, #0
	str	r3, [sp, #60]
	bl	FlashReadPages
	ldr	r3, [sp, #48]
	cmn	r3, #1
	bne	.L1866
	ldr	r2, [r5, #2716]
	ldr	r0, .L1899+40
	add	r2, r2, #1
	str	r2, [r5, #2716]
	mov	r2, r7
	ldr	r1, [r9, #8]
	bl	sftl_printk
.L1869:
	ldr	r3, [sp, #4]
	lsl	r2, r6, #9
	cmp	r7, r3
	bne	.L1870
	ldr	r3, [r5, #3300]
	ldr	r1, [sp, #12]
	add	r8, r3, r8
	ldr	r3, [sp, #16]
	ldr	r0, [r8, #8]
	add	r0, r0, r3, lsl #9
.L1896:
	bl	ftl_memcpy
	b	.L1862
.L1866:
	ldr	r3, [r9, #8]
	cmp	r7, r3
	beq	.L1868
	ldr	r3, [r5, #2716]
	mov	r2, r7
	ldr	r0, .L1899+44
	add	r3, r3, #1
	str	r3, [r5, #2716]
	ldr	r1, [r9, #8]
	bl	sftl_printk
.L1868:
	ldr	r3, [r9, #8]
	cmp	r7, r3
	beq	.L1869
	movw	r2, #1128
	ldr	r1, .L1899+16
	ldr	r0, .L1899+20
	bl	sftl_printk
	b	.L1869
.L1865:
	ldr	r3, [r5, #3300]
	mov	r1, #0
	ldr	r2, .L1899+48
	add	r3, r3, r8
	ldrh	r2, [r2]
	ldr	r0, [r3, #8]
	bl	ftl_memset
	b	.L1869
.L1870:
	ldr	r3, .L1899+32
	ldrh	r1, [r3]
	ldr	r3, [r5, #3300]
	mul	r1, r7, r1
	add	r8, r3, r8
	ldr	r3, [sp, #12]
	ldr	r0, [r8, #8]
	sub	r1, r1, r10
	add	r1, r3, r1, lsl #9
	b	.L1896
.L1857:
	ldr	r3, [r5, #3300]
	ldr	r2, [sp, #12]
	add	r8, r3, r8
	ldr	r3, .L1899+32
	ldrh	r3, [r3]
	mul	r3, r7, r3
	sub	r3, r3, r10
	add	r3, r2, r3, lsl #9
.L1895:
	str	r3, [r8, #8]
	b	.L1862
.L1880:
	ldrh	r3, [r5, #172]
	movw	r2, #65535
	cmp	r3, r2
	bne	.L1878
	ldrh	r2, [r5, #222]
	cmp	r2, r3
	bne	.L1878
	mov	r0, #0
	bl	List_get_gc_head_node
	uxth	r0, r0
	bl	FtlGcRefreshBlock
.L1878:
	ldr	r2, .L1899+52
	mov	r1, #1
	mov	r3, #128
	mov	r0, r1
	strh	r3, [r6]	@ movhi
	strh	r3, [r2]	@ movhi
	bl	rk_ftl_garbage_collect
	mov	r1, #1
	mov	r0, #0
	bl	rk_ftl_garbage_collect
	ldrh	r3, [r5, #224]
	cmp	r3, #2
	bhi	.L1879
	subs	r4, r4, #1
	bne	.L1880
	b	.L1879
.L1883:
	mvn	r0, #0
	b	.L1844
.L1881:
	add	sp, sp, #76
	@ sp needed
	pop	{r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L1900:
	.align	2
.L1899:
	.word	__stack_chk_guard
	.word	.LANCHOR0
	.word	.LANCHOR2
	.word	.LANCHOR0+3158
	.word	.LANCHOR1+591
	.word	.LC8
	.word	.LANCHOR0+24
	.word	.LANCHOR0+312
	.word	.LANCHOR0+258
	.word	-3947
	.word	.LC125
	.word	.LC126
	.word	.LANCHOR0+310
	.word	.LANCHOR0+3156
	.fnend
	.size	FtlWrite, .-FtlWrite
	.align	2
	.global	sftl_gc
	.syntax unified
	.arm
	.fpu softvfp
	.type	sftl_gc, %function
sftl_gc:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	@ link register save eliminated.
	mov	r1, #1
	mov	r0, r1
	b	rk_ftl_garbage_collect
	.fnend
	.size	sftl_gc, .-sftl_gc
	.align	2
	.global	FtlLoadSysInfo
	.syntax unified
	.arm
	.fpu softvfp
	.type	FtlLoadSysInfo, %function
FtlLoadSysInfo:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	push	{r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.save {r4, r5, r6, r7, r8, r9, r10, fp, lr}
	mov	r1, #0
	ldr	r4, .L1933
	.pad #20
	sub	sp, sp, #20
	movw	r6, #2628
	ldr	r3, [r4, #3304]
	ldrh	r2, [r4, #240]
	ldr	r0, [r4, #72]
	str	r3, [r4, #3464]
	ldr	r3, [r4, #3336]
	lsl	r2, r2, #1
	str	r3, [r4, #3468]
	bl	ftl_memset
	ldrh	r0, [r4, r6]
	movw	r3, #65535
	cmp	r0, r3
	bne	.L1903
.L1914:
	mvn	r0, #0
.L1902:
	add	sp, sp, #20
	@ sp needed
	pop	{r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L1903:
	mov	r1, #1
	ldr	r9, .L1933+4
	bl	FtlGetLastWrittenPage
	ldr	r10, .L1933+8
	add	r8, r4, #3456
	ldrsh	r6, [r4, r6]
	sxth	r5, r0
	ldr	r7, .L1933+12
	add	r0, r0, #1
	strh	r0, [r7, #2]	@ movhi
.L1905:
	cmp	r5, #0
	bge	.L1911
	movw	r2, #1467
	ldr	r1, .L1933+16
	ldr	r0, .L1933+20
	bl	sftl_printk
	b	.L1910
.L1911:
	orr	r3, r5, r6, lsl #10
	mov	r2, #1
	mov	r1, r2
	str	r3, [r4, #3460]
	mov	r0, r8
	ldr	r3, [r4, #3304]
	str	r3, [r4, #3464]
	bl	FlashReadPages
	ldr	r3, [r4, #3468]
	ldr	fp, [r3, #12]
	cmp	fp, #0
	beq	.L1906
	ldr	r3, [r4, #3456]
	cmn	r3, #1
	beq	.L1906
	ldrh	r1, [r10]
	ldr	r0, [r4, #3464]
	bl	js_hash
	cmp	fp, r0
	beq	.L1906
	str	r0, [sp, #8]
	mov	r2, r6
	str	fp, [sp, #4]
	ldrh	r3, [r7, #4]
	ldr	r1, .L1933+16
	ldr	r0, .L1933+24
	str	r3, [sp]
	mov	r3, r5
	bl	sftl_printk
	cmp	r5, #0
	bne	.L1907
	ldrh	r3, [r7, #4]
	cmp	r6, r3
	beq	.L1907
	sxth	r6, r3
	ldr	r3, .L1933+28
	ldrh	r5, [r3]
.L1909:
	sub	r5, r5, #1
	sxth	r5, r5
	b	.L1905
.L1907:
	mvn	r3, #0
	str	r3, [r4, #3456]
.L1906:
	ldr	r3, [r4, #3456]
	cmn	r3, #1
	beq	.L1909
	ldr	r3, [r4, #3304]
	ldr	r3, [r3]
	cmp	r3, r9
	bne	.L1909
	ldr	r3, [r4, #3336]
	ldrh	r2, [r3]
	movw	r3, #61604
	cmp	r2, r3
	bne	.L1909
.L1910:
	movw	r2, #310
	ldrh	r3, [r4, #240]
	ldrh	r2, [r4, r2]
	add	r3, r3, #24
	cmp	r2, r3, lsl #1
	bcs	.L1913
	movw	r2, #1469
	ldr	r1, .L1933+16
	ldr	r0, .L1933+20
	bl	sftl_printk
.L1913:
	ldr	r5, .L1933+32
	mov	r2, #48
	ldr	r1, [r4, #3464]
	mov	r0, r5
	bl	ftl_memcpy
	ldrh	r2, [r4, #240]
	ldr	r1, [r4, #3464]
	ldr	r0, [r4, #72]
	lsl	r2, r2, #1
	add	r1, r1, #48
	bl	ftl_memcpy
	ldr	r2, [r4, #2468]
	ldr	r3, .L1933+4
	cmp	r2, r3
	bne	.L1914
	ldrb	r2, [r4, #2478]	@ zero_extendqisi2
	ldrh	r3, [r4, #254]
	ldrh	r6, [r5, #8]
	cmp	r2, r3
	strh	r6, [r7, #6]	@ movhi
	bne	.L1914
	movw	r3, #302
	movw	r2, #258
	ldrh	r3, [r4, r3]
	add	r8, r5, #156
	ldrh	r2, [r4, r2]
	ldr	r7, [r4, #244]
	str	r6, [r4, #3676]
	mul	r3, r6, r3
	ldrh	r1, [r4, #232]
	str	r3, [r4, #2556]
	mul	r3, r3, r2
	str	r3, [r4, #340]
	sub	r3, r8, #2272
	ldrh	r0, [r3, #6]
	sub	r0, r7, r0
	sub	r0, r0, r6
	bl	__aeabi_uidiv
	cmp	r6, r7
	strh	r0, [r5, #156]	@ movhi
	bls	.L1915
	movw	r2, #1491
	ldr	r1, .L1933+16
	ldr	r0, .L1933+20
	bl	sftl_printk
.L1915:
	ldrh	r3, [r5, #16]
	ldrh	r1, [r5, #14]
	lsr	r2, r3, #6
	and	r3, r3, #63
	strb	r3, [r4, #30]
	strh	r2, [r4, #26]	@ movhi
	ldrh	r2, [r5, #18]
	ldrb	r3, [r4, #2479]	@ zero_extendqisi2
	strh	r1, [r4, #24]	@ movhi
	strh	r2, [r4, #76]	@ movhi
	ldrh	r2, [r5, #20]
	strb	r3, [r4, #32]
	mvn	r3, #0
	strh	r3, [r4, #172]	@ movhi
	mov	r3, #0
	strh	r3, [r4, #174]	@ movhi
	lsr	r0, r2, #6
	and	r2, r2, #63
	strb	r2, [r4, #82]
	ldrb	r2, [r4, #2480]	@ zero_extendqisi2
	strh	r0, [r4, #78]	@ movhi
	strb	r3, [r4, #178]
	strb	r2, [r4, #84]
	ldrh	r2, [r5, #22]
	strb	r3, [r4, #180]
	str	r3, [r4, #2580]
	strh	r2, [r4, #124]	@ movhi
	ldrh	r2, [r5, #24]
	str	r3, [r4, #2568]
	str	r3, [r4, #2560]
	str	r3, [r4, #2576]
	lsr	r0, r2, #6
	and	r2, r2, #63
	strb	r2, [r4, #130]
	ldrb	r2, [r4, #2481]	@ zero_extendqisi2
	str	r3, [r4, #2604]
	strh	r0, [r4, #126]	@ movhi
	strb	r2, [r4, #132]
	ldr	r2, [r4, #2500]
	str	r2, [r4, #2600]
	str	r3, [r4, #2616]
	ldr	r2, [r4, #2592]
	str	r3, [r4, #2572]
	ldr	r3, [r4, #2508]
	cmp	r3, r2
	ldr	r2, [r4, #2596]
	strhi	r3, [r4, #2592]
	ldr	r3, [r4, #2504]
	cmp	r3, r2
	strhi	r3, [r4, #2596]
	movw	r3, #65535
	cmp	r1, r3
	beq	.L1918
	ldr	r0, .L1933+36
	bl	make_superblock
.L1918:
	ldrh	r2, [r4, #76]
	movw	r3, #65535
	cmp	r2, r3
	beq	.L1919
	ldr	r0, .L1933+40
	bl	make_superblock
.L1919:
	ldrh	r2, [r4, #124]
	movw	r3, #65535
	cmp	r2, r3
	beq	.L1920
	ldr	r0, .L1933+44
	bl	make_superblock
.L1920:
	ldrh	r2, [r4, #172]
	movw	r3, #65535
	cmp	r2, r3
	beq	.L1921
	ldr	r0, .L1933+48
	bl	make_superblock
.L1921:
	mov	r0, #0
	b	.L1902
.L1934:
	.align	2
.L1933:
	.word	.LANCHOR0
	.word	1179929683
	.word	.LANCHOR0+310
	.word	.LANCHOR0+2628
	.word	.LANCHOR1+600
	.word	.LC8
	.word	.LC127
	.word	.LANCHOR0+304
	.word	.LANCHOR0+2468
	.word	.LANCHOR0+24
	.word	.LANCHOR0+76
	.word	.LANCHOR0+124
	.word	.LANCHOR0+172
	.fnend
	.size	FtlLoadSysInfo, .-FtlLoadSysInfo
	.align	2
	.global	FtlMapTblRecovery
	.syntax unified
	.arm
	.fpu softvfp
	.type	FtlMapTblRecovery, %function
FtlMapTblRecovery:
	.fnstart
	@ args = 0, pretend = 0, frame = 32
	@ frame_needed = 0, uses_anonymous_args = 0
	push	{r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.save {r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.pad #44
	sub	sp, sp, #44
	ldr	r3, [r0, #24]
	mov	r4, r0
	mov	r1, #0
	mov	r7, #0
	ldr	r5, .L1977
	str	r3, [sp, #12]
	ldr	r3, [r0, #16]
	ldr	r8, [r0, #12]
	add	r10, r5, #304
	str	r3, [sp, #24]
	ldrh	r3, [r0, #6]
	str	r3, [sp, #16]
	ldrh	r3, [r0, #8]
	ldr	r0, [sp, #12]
	str	r3, [sp, #20]
	ldr	r3, [sp, #16]
	lsl	r2, r3, #2
	bl	ftl_memset
	ldr	r3, [r5, #3304]
	ldr	fp, [r5, #3336]
	str	r7, [r4, #32]
	str	r3, [r5, #3464]
	mvn	r3, #0
	str	fp, [r5, #3468]
	strh	r3, [r4]	@ movhi
	strh	r3, [r4, #2]	@ movhi
	mov	r3, #1
	str	r7, [r4, #28]
	str	r3, [r4, #36]
.L1936:
	ldr	r3, [sp, #20]
	sxth	r6, r7
	cmp	r6, r3
	bge	.L1955
	ldr	r3, [sp, #20]
	sub	r3, r3, #1
	cmp	r6, r3
	lsl	r3, r6, #1
	bne	.L1937
	ldrh	r0, [r8, r3]
	mov	r1, #1
	add	r9, r8, r3
	ldr	r5, .L1977
	bl	FtlGetLastWrittenPage
	sxth	r3, r0
	add	r0, r0, #1
	strh	r7, [r4]	@ movhi
	add	r10, r5, #3456
	str	r3, [sp, #20]
	mov	r7, #0
	ldr	r3, [sp, #24]
	strh	r0, [r4, #2]	@ movhi
	ldr	r3, [r3, r6, lsl #2]
	str	r3, [r4, #28]
.L1938:
	ldr	r3, [sp, #20]
	sxth	r8, r7
	add	r2, r3, #1
	cmp	r8, r2
	blt	.L1941
.L1955:
	mov	r0, r4
	bl	ftl_free_no_use_map_blk
	ldr	r3, .L1977+4
	ldrh	r2, [r4, #2]
	ldrh	r3, [r3]
	cmp	r2, r3
	bne	.L1943
	mov	r0, r4
	bl	ftl_map_blk_alloc_new_blk
.L1943:
	mov	r0, r4
	bl	ftl_map_blk_gc
	mov	r0, r4
	bl	ftl_map_blk_gc
	mov	r0, #0
	add	sp, sp, #44
	@ sp needed
	pop	{r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L1941:
	ldrh	r2, [r9]
	mov	r0, r10
	orr	r2, r8, r2, lsl #10
	str	r2, [r5, #3460]
	mov	r2, #1
	mov	r1, r2
	bl	FlashReadPages
	ldr	r2, [r5, #3468]
	ldr	r2, [r2, #12]
	cmp	r2, #0
	str	r2, [sp, #24]
	beq	.L1939
	ldr	r1, [r5, #3456]
	cmn	r1, #1
	beq	.L1939
	ldr	r3, .L1977+8
	ldr	r0, [r5, #3464]
	ldrh	r1, [r3]
	bl	js_hash
	ldr	r2, [sp, #24]
	cmp	r2, r0
	beq	.L1939
	str	r0, [sp, #4]
	mov	r3, r8
	str	r2, [sp]
	mov	r2, r6
	ldr	r1, .L1977+12
	ldr	r0, .L1977+16
	bl	sftl_printk
	mvn	r3, #0
	str	r3, [r5, #3456]
.L1939:
	ldr	r3, [r5, #3456]
	cmn	r3, #1
	beq	.L1940
	ldrh	r3, [fp, #8]
	ldr	r2, [sp, #16]
	cmp	r2, r3
	bls	.L1940
	ldrh	r1, [fp]
	ldrh	r2, [r4, #4]
	cmp	r1, r2
	ldreq	r2, [r5, #3460]
	ldreq	r1, [sp, #12]
	streq	r2, [r1, r3, lsl #2]
.L1940:
	add	r7, r7, #1
	b	.L1938
.L1937:
	ldr	r2, [r5, #3304]
	ldr	r0, .L1977+20
	str	r2, [r5, #3464]
	add	r2, r8, r3
	str	r2, [sp, #28]
	ldrh	r2, [r8, r3]
	ldrh	r3, [r10]
	sub	r3, r3, #1
	orr	r3, r3, r2, lsl #10
	mov	r2, #1
	mov	r1, r2
	str	r3, [r5, #3460]
	bl	FlashReadPages
	ldr	r3, [r5, #3456]
	cmn	r3, #1
	beq	.L1957
	ldrh	r2, [fp]
	ldrh	r3, [r4, #4]
	cmp	r2, r3
	bne	.L1957
	ldrh	r2, [fp, #8]
	movw	r3, #64245
	cmp	r2, r3
	beq	.L1945
.L1957:
	mov	r9, #0
.L1946:
	ldrh	r2, [r10]
	sxth	r3, r9
	cmp	r3, r2
	bge	.L1953
	ldr	r2, [sp, #28]
	ldr	r0, .L1977+20
	str	r3, [sp, #36]
	ldrh	r2, [r2]
	orr	r2, r3, r2, lsl #10
	str	r2, [r5, #3460]
	mov	r2, #1
	mov	r1, r2
	bl	FlashReadPages
	ldr	r2, [r5, #3468]
	ldr	r2, [r2, #12]
	cmp	r2, #0
	str	r2, [sp, #32]
	beq	.L1950
	ldr	r1, [r5, #3456]
	cmn	r1, #1
	beq	.L1950
	ldr	r1, .L1977+8
	ldr	r0, [r5, #3464]
	ldrh	r1, [r1]
	bl	js_hash
	ldr	r2, [sp, #32]
	cmp	r2, r0
	beq	.L1950
	str	r0, [sp, #4]
	str	r2, [sp]
	mov	r2, r6
	ldr	r3, [sp, #36]
	ldr	r1, .L1977+12
	ldr	r0, .L1977+24
	bl	sftl_printk
	mvn	r3, #0
	str	r3, [r5, #3456]
.L1950:
	ldr	r3, [r5, #3456]
	cmn	r3, #1
	beq	.L1951
	ldrh	r3, [fp, #8]
	ldr	r2, [sp, #16]
	cmp	r2, r3
	bls	.L1951
	ldrh	r1, [fp]
	ldrh	r2, [r4, #4]
	cmp	r1, r2
	ldreq	r2, [r5, #3460]
	ldreq	r1, [sp, #12]
	streq	r2, [r1, r3, lsl #2]
.L1951:
	add	r9, r9, #1
	b	.L1946
.L1945:
	mov	r1, #0
	mov	r0, #4
.L1947:
	ldrh	r2, [r10]
	sxth	r3, r1
	sub	r2, r2, #1
	cmp	r3, r2
	blt	.L1949
.L1953:
	add	r7, r7, #1
	b	.L1936
.L1949:
	ldr	ip, [r5, #3304]
	add	r1, r1, #1
	ldr	r6, [sp, #16]
	ldr	r2, [ip, r3, lsl #3]
	uxth	lr, r2
	cmp	r6, lr
	addhi	r3, r0, r3, lsl #3
	movhi	r2, lr
	ldrhi	r3, [ip, r3]
	ldrhi	ip, [sp, #12]
	strhi	r3, [ip, r2, lsl #2]
	b	.L1947
.L1978:
	.align	2
.L1977:
	.word	.LANCHOR0
	.word	.LANCHOR0+304
	.word	.LANCHOR0+310
	.word	.LANCHOR1+615
	.word	.LC128
	.word	.LANCHOR0+3456
	.word	.LC129
	.fnend
	.size	FtlMapTblRecovery, .-FtlMapTblRecovery
	.align	2
	.global	FtlLoadVonderInfo
	.syntax unified
	.arm
	.fpu softvfp
	.type	FtlLoadVonderInfo, %function
FtlLoadVonderInfo:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	ldr	r3, .L1981
	push	{r4, lr}
	.save {r4, lr}
	add	r2, r3, #320
	add	r0, r3, #3600
	ldrh	r2, [r2]
	add	r0, r0, #12
	strh	r2, [r0, #10]	@ movhi
	ldr	r2, .L1981+4
	strh	r2, [r0, #4]	@ movhi
	add	r2, r3, #344
	ldrh	r2, [r2]
	strh	r2, [r0, #8]	@ movhi
	movw	r2, #322
	ldrh	r2, [r3, r2]
	strh	r2, [r0, #6]	@ movhi
	ldr	r2, [r3, #348]
	str	r2, [r3, #3624]
	ldr	r2, [r3, #3372]
	str	r2, [r3, #3628]
	ldr	r2, [r3, #3368]
	str	r2, [r3, #3632]
	ldr	r2, [r3, #3376]
	str	r2, [r3, #3636]
	bl	FtlMapTblRecovery
	mov	r0, #0
	pop	{r4, pc}
.L1982:
	.align	2
.L1981:
	.word	.LANCHOR0
	.word	-3962
	.fnend
	.size	FtlLoadVonderInfo, .-FtlLoadVonderInfo
	.align	2
	.global	FtlLoadMapInfo
	.syntax unified
	.arm
	.fpu softvfp
	.type	FtlLoadMapInfo, %function
FtlLoadMapInfo:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	push	{r4, lr}
	.save {r4, lr}
	bl	FtlL2PDataInit
	ldr	r0, .L1985
	bl	FtlMapTblRecovery
	mov	r0, #0
	pop	{r4, pc}
.L1986:
	.align	2
.L1985:
	.word	.LANCHOR0+3396
	.fnend
	.size	FtlLoadMapInfo, .-FtlLoadMapInfo
	.align	2
	.global	FtlSysBlkInit
	.syntax unified
	.arm
	.fpu softvfp
	.type	FtlSysBlkInit, %function
FtlSysBlkInit:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	push	{r4, r5, r6, lr}
	.save {r4, r5, r6, lr}
	mov	r2, #0
	ldr	r4, .L2004
	movw	r3, #3476
	ldrh	r0, [r4, #236]
	strh	r2, [r4, r3]	@ movhi
	bl	FtlFreeSysBlkQueueInit
	bl	FtlScanSysBlk
	movw	r3, #2628
	ldrh	r2, [r4, r3]
	movw	r3, #65535
	cmp	r2, r3
	bne	.L1988
.L1990:
	mvn	r6, #0
.L1987:
	mov	r0, r6
	pop	{r4, r5, r6, pc}
.L1988:
	bl	FtlLoadSysInfo
	subs	r6, r0, #0
	bne	.L1990
	bl	FtlLoadMapInfo
	bl	FtlLoadVonderInfo
	bl	Ftl_load_ext_data
	bl	FtlLoadEctTbl
	bl	FtlFreeSysBLkSort
	bl	SupperBlkListInit
	bl	FtlPowerLostRecovery
	mov	r0, #1
	bl	FtlUpdateVaildLpn
	ldr	r2, [r4, #2540]
	movw	r3, #338
	ldrh	r1, [r4, r3]
	mov	r0, #12
	mov	r3, r6
.L1991:
	cmp	r3, r1
	bge	.L1996
	mla	ip, r0, r3, r2
	ldr	ip, [ip, #4]
	cmp	ip, #0
	bge	.L1992
.L1996:
	ldr	r5, .L2004+4
	cmp	r3, r1
	ldrh	r2, [r5, #28]
	add	r2, r2, #1
	strh	r2, [r5, #28]	@ movhi
	bge	.L2002
.L1993:
	ldr	r0, .L2004+8
	bl	FtlSuperblockPowerLostFix
	ldr	r0, .L2004+12
	bl	FtlSuperblockPowerLostFix
	ldrh	r3, [r4, #24]
	ldr	r1, [r4, #72]
	ldrh	r0, [r4, #28]
	lsl	r3, r3, #1
	ldrh	r2, [r1, r3]
	sub	r2, r2, r0
	movw	r0, #302
	strh	r2, [r1, r3]	@ movhi
	ldrh	r2, [r4, #76]
	ldrh	r3, [r4, r0]
	ldr	ip, [r4, #72]
	ldrh	lr, [r4, #80]
	lsl	r2, r2, #1
	strh	r3, [r4, #26]	@ movhi
	mov	r3, #0
	strb	r3, [r4, #30]
	strh	r3, [r4, #28]	@ movhi
	ldrh	r1, [ip, r2]
	sub	r1, r1, lr
	strh	r1, [ip, r2]	@ movhi
	strb	r3, [r4, #82]
	strh	r3, [r4, #80]	@ movhi
	ldrh	r3, [r5, #30]
	ldrh	r2, [r4, r0]
	add	r3, r3, #1
	strh	r2, [r4, #78]	@ movhi
	strh	r3, [r5, #30]	@ movhi
	bl	l2p_flush
	bl	FtlVpcTblFlush
	bl	FtlVpcTblFlush
	b	.L1997
.L1992:
	add	r3, r3, #1
	b	.L1991
.L2002:
	movw	r3, #3476
	ldrh	r3, [r4, r3]
	cmp	r3, #0
	bne	.L1993
.L1997:
	ldrh	r0, [r4, #24]
	movw	r3, #65535
	cmp	r0, r3
	beq	.L1998
	ldrh	r3, [r4, #28]
	cmp	r3, #0
	bne	.L1998
	ldrh	r3, [r4, #80]
	cmp	r3, #0
	bne	.L1998
	bl	FtlGcRefreshBlock
	ldrh	r0, [r4, #76]
	bl	FtlGcRefreshBlock
	bl	FtlVpcTblFlush
	ldr	r0, .L2004+8
	bl	allocate_new_data_superblock
	ldr	r0, .L2004+12
	bl	allocate_new_data_superblock
.L1998:
	bl	FtlVpcCheckAndModify
	b	.L1987
.L2005:
	.align	2
.L2004:
	.word	.LANCHOR0
	.word	.LANCHOR0+2468
	.word	.LANCHOR0+24
	.word	.LANCHOR0+76
	.fnend
	.size	FtlSysBlkInit, .-FtlSysBlkInit
	.align	2
	.global	ftl_low_format
	.syntax unified
	.arm
	.fpu softvfp
	.type	ftl_low_format, %function
ftl_low_format:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	push	{r4, r5, r6, r7, r8, r9, r10, lr}
	.save {r4, r5, r6, r7, r8, r9, r10, lr}
	mov	r3, #0
	ldr	r4, .L2031
	ldrh	r0, [r4, #236]
	str	r3, [r4, #2592]
	str	r3, [r4, #2596]
	str	r3, [r4, #2608]
	bl	FtlFreeSysBlkQueueInit
	bl	FtlLoadBbt
	cmp	r0, #0
	beq	.L2007
	bl	FtlMakeBbt
.L2007:
	ldr	r0, .L2031+4
	mov	r2, #0
	ldr	ip, .L2031+8
.L2008:
	ldrh	r1, [r0]
	uxth	r3, r2
	add	r2, r2, #1
	cmp	r3, r1, lsl #7
	blt	.L2009
	ldrh	r6, [r4, #240]
	mov	r5, #0
.L2010:
	ldrh	r3, [r4, #242]
	cmp	r3, r6
	bhi	.L2011
	ldrh	r1, [r4, #232]
	sub	r3, r5, #3
	cmp	r3, r1, lsl #1
	bge	.L2012
.L2016:
	mov	r5, #0
	mov	r7, r5
.L2013:
	ldrh	r3, [r4, #240]
	uxth	r0, r5
	add	r5, r5, #1
	cmp	r3, r0
	bhi	.L2017
	ldrh	r3, [r4, #242]
	ldrh	r5, [r4, #232]
	ldr	r10, [r4, #244]
	str	r3, [r4, #3288]
	mov	r1, r5
	ldr	r6, .L2031+12
	mov	r0, r10
	bl	__aeabi_uidiv
	ubfx	r8, r0, #5, #16
	mov	r9, r0
	add	r3, r8, #36
	str	r0, [r4, #2556]
	strh	r3, [r6]	@ movhi
	mov	r3, #24
	mul	r3, r3, r5
	cmp	r7, r3
	ble	.L2018
	mov	r1, r5
	sub	r0, r10, r7
	bl	__aeabi_uidiv
	str	r0, [r4, #2556]
	lsr	r0, r0, #5
	add	r0, r0, #24
	strh	r0, [r6]	@ movhi
.L2018:
	movw	r3, #294
	ldrh	r3, [r4, r3]
	cmp	r3, #0
	beq	.L2020
	ldrh	r2, [r6]
	add	r2, r2, r3, lsr #1
	strh	r2, [r6]	@ movhi
	mul	r2, r5, r3
	cmp	r7, r2
	addlt	r3, r3, #32
	strlt	r9, [r4, #2556]
	addlt	r3, r8, r3
	strhlt	r3, [r6]	@ movhi
.L2020:
	ldrh	r2, [r6]
	ldr	r3, [r4, #2556]
	ldr	r6, .L2031+16
	sub	r3, r3, r2
	mul	r5, r5, r3
	movw	r3, #302
	ldrh	r3, [r4, r3]
	str	r5, [r4, #3676]
	mul	r5, r5, r3
	movw	r3, #258
	ldrh	r3, [r4, r3]
	str	r5, [r4, #2556]
	mul	r5, r5, r3
	str	r5, [r4, #340]
	bl	FtlBbmTblFlush
	ldrh	r2, [r4, #242]
	mov	r1, #0
	ldr	r0, [r4, #72]
	lsl	r2, r2, #1
	bl	ftl_memset
	mvn	r2, #0
	mov	r3, #0
	mov	r5, r2
	str	r3, [r4, #2548]
	strh	r3, [r4, #174]	@ movhi
	strb	r3, [r4, #178]
	strb	r3, [r4, #180]
	strh	r3, [r4, #26]	@ movhi
	strb	r3, [r4, #30]
	strh	r3, [r4, #24]	@ movhi
	mov	r3, #1
	strh	r2, [r4, #172]	@ movhi
	strb	r3, [r4, #32]
.L2022:
	mov	r0, r6
	bl	make_superblock
	ldrb	r3, [r4, #31]	@ zero_extendqisi2
	cmp	r3, #0
	ldrh	r3, [r4, #24]
	bne	.L2023
	ldr	r2, [r4, #72]
	lsl	r3, r3, #1
	strh	r5, [r2, r3]	@ movhi
	ldrh	r3, [r4, #24]
	add	r3, r3, #1
	strh	r3, [r4, #24]	@ movhi
	b	.L2022
.L2009:
	ldr	lr, [r4, #3316]
	mvn	r1, r3
	orr	r1, r3, r1, lsl #16
	str	r1, [lr, r3, lsl #2]
	ldr	r1, [r4, #3320]
	str	ip, [r1, r3, lsl #2]
	b	.L2008
.L2011:
	mov	r0, r6
	mov	r1, #1
	bl	FtlLowFormatEraseBlock
	add	r6, r6, #1
	add	r5, r5, r0
	uxth	r5, r5
	uxth	r6, r6
	b	.L2010
.L2012:
	mov	r0, r5
	bl	__aeabi_uidiv
	ldr	r3, [r4, #332]
	add	r0, r0, r3
	uxth	r0, r0
	bl	FtlSysBlkNumInit
	ldrh	r0, [r4, #236]
	bl	FtlFreeSysBlkQueueInit
	ldrh	r5, [r4, #240]
.L2014:
	ldrh	r3, [r4, #242]
	cmp	r3, r5
	bls	.L2016
	mov	r0, r5
	mov	r1, #1
	add	r5, r5, #1
	bl	FtlLowFormatEraseBlock
	uxth	r5, r5
	b	.L2014
.L2017:
	mov	r1, #0
	bl	FtlLowFormatEraseBlock
	add	r7, r7, r0
	uxth	r7, r7
	b	.L2013
.L2023:
	ldr	r2, [r4, #2592]
	lsl	r3, r3, #1
	ldrh	r1, [r4, #28]
	mvn	r5, #0
	ldr	r6, .L2031+20
	str	r2, [r4, #36]
	add	r2, r2, #1
	str	r2, [r4, #2592]
	ldr	r2, [r4, #72]
	strh	r1, [r2, r3]	@ movhi
	mov	r3, #0
	strh	r3, [r4, #78]	@ movhi
	strb	r3, [r4, #82]
	ldrh	r3, [r4, #24]
	add	r3, r3, #1
	strh	r3, [r4, #76]	@ movhi
	mov	r3, #1
	strb	r3, [r4, #84]
.L2024:
	mov	r0, r6
	bl	make_superblock
	ldrb	r3, [r4, #83]	@ zero_extendqisi2
	cmp	r3, #0
	ldrh	r3, [r4, #76]
	bne	.L2025
	ldr	r2, [r4, #72]
	lsl	r3, r3, #1
	strh	r5, [r2, r3]	@ movhi
	ldrh	r3, [r4, #76]
	add	r3, r3, #1
	strh	r3, [r4, #76]	@ movhi
	b	.L2024
.L2025:
	ldr	r2, [r4, #2592]
	lsl	r3, r3, #1
	ldrh	r1, [r4, #80]
	mvn	r5, #0
	str	r2, [r4, #88]
	add	r2, r2, #1
	str	r2, [r4, #2592]
	ldr	r2, [r4, #72]
	strh	r1, [r2, r3]	@ movhi
	strh	r5, [r4, #124]	@ movhi
	bl	FtlFreeSysBlkQueueOut
	ldr	r3, .L2031+24
	movw	r2, #2628
	strh	r0, [r4, r2]	@ movhi
	mov	r2, #0
	strh	r2, [r3, #2]	@ movhi
	ldr	r2, [r4, #3676]
	strh	r5, [r3, #4]	@ movhi
	strh	r2, [r3, #6]	@ movhi
	ldr	r3, [r4, #2592]
	str	r3, [r4, #2636]
	add	r3, r3, #1
	str	r3, [r4, #2592]
	bl	FtlVpcTblFlush
	bl	FtlSysBlkInit
	cmp	r0, #0
	mov	r0, #0
	ldreq	r3, .L2031+28
	moveq	r2, #1
	streq	r2, [r3]
	pop	{r4, r5, r6, r7, r8, r9, r10, pc}
.L2032:
	.align	2
.L2031:
	.word	.LANCHOR0
	.word	.LANCHOR0+258
	.word	168778952
	.word	.LANCHOR0+2624
	.word	.LANCHOR0+24
	.word	.LANCHOR0+76
	.word	.LANCHOR0+2628
	.word	.LANCHOR2
	.fnend
	.size	ftl_low_format, .-ftl_low_format
	.align	2
	.global	sftl_init
	.syntax unified
	.arm
	.fpu softvfp
	.type	sftl_init, %function
sftl_init:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	push	{r4, r5, r6, lr}
	.save {r4, r5, r6, lr}
	mvn	r3, #0
	ldr	r4, .L2040
	ldr	r5, .L2040+4
	ldr	r1, .L2040+8
	ldr	r0, .L2040+12
	str	r3, [r5]
	bl	sftl_printk
	mov	r0, r4
	bl	FtlConstantsInit
	bl	FtlMemInit
	bl	FtlVariablesInit
	ldrh	r0, [r4, #236]
	bl	FtlFreeSysBlkQueueInit
	bl	FtlLoadBbt
	cmp	r0, #0
	bne	.L2037
	bl	FtlSysBlkInit
	cmp	r0, #0
	bne	.L2037
	mov	r3, #1
	str	r3, [r5]
	ldrh	r3, [r4, #224]
	cmp	r3, #15
	bhi	.L2037
	movw	r4, #8129
.L2036:
	mov	r1, #1
	mov	r0, #0
	bl	rk_ftl_garbage_collect
	subs	r4, r4, #1
	bne	.L2036
.L2037:
	mov	r0, #0
	pop	{r4, r5, r6, pc}
.L2041:
	.align	2
.L2040:
	.word	.LANCHOR0
	.word	.LANCHOR2
	.word	.LC0
	.word	.LC77
	.fnend
	.size	sftl_init, .-sftl_init
	.align	2
	.global	FtlWriteToIDB
	.syntax unified
	.arm
	.fpu softvfp
	.type	FtlWriteToIDB, %function
FtlWriteToIDB:
	.fnstart
	@ args = 0, pretend = 0, frame = 104
	@ frame_needed = 0, uses_anonymous_args = 0
	push	{r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.save {r4, r5, r6, r7, r8, r9, r10, fp, lr}
	add	r8, r1, r0
	ldr	r3, .L2103
	sub	r10, r8, #1
	cmp	r10, #63
	.pad #116
	sub	sp, sp, #116
	movhi	r9, #0
	movls	r9, #1
	ldr	r3, [r3]
	cmp	r0, #576
	orrcs	r9, r9, #1
	cmp	r9, #0
	ldr	r5, .L2103+4
	str	r3, [sp, #108]
	beq	.L2043
	ldr	r3, [r5, #3680]
	cmp	r3, #0
	bne	.L2044
.L2102:
	mov	r4, #0
	b	.L2042
.L2044:
	ldr	r7, [r5, #3684]
	ldr	r3, .L2103+8
	ldr	r2, [r7]
	cmp	r2, r3
	bne	.L2046
	ldrh	r1, [r5, #10]
	add	r0, r7, #260096
	mov	r2, #0
	movw	r3, #65023
.L2050:
	ldr	ip, [r0, #-4]!
	cmp	ip, #0
	bne	.L2047
	ldr	ip, [r7, r2, lsl #2]
	add	r2, r2, #1
	cmp	r2, #4096
	sub	r3, r3, #1
	movhi	r2, #0
	cmp	r3, #4096
	str	ip, [r0, #2048]
	bne	.L2050
	mov	fp, #512
	b	.L2049
.L2047:
	add	r3, r3, #127
	lsr	fp, r3, #7
.L2049:
	lsl	r1, r1, #2
	add	r0, fp, #4
	ldr	r4, .L2103+4
	uxth	r1, r1
	bl	__aeabi_uidiv
	add	r3, r0, #1
	mov	r1, fp
	mov	r2, r3
	ldr	r0, .L2103+12
	str	r3, [sp, #16]
	bl	sftl_printk
	lsl	r3, fp, #7
	str	r3, [sp, #28]
	mov	r3, #0
	str	r3, [sp, #20]
	str	r3, [sp, #12]
.L2051:
	ldr	r3, [sp, #16]
	ldr	r2, [sp, #12]
	add	r3, r3, r2
	cmp	r3, #8
	str	r3, [sp, #24]
	bls	.L2073
	ldr	r3, [sp, #20]
	cmp	r3, #0
	bne	.L2074
.L2046:
	mvn	fp, #0
.L2074:
	ldr	r5, .L2103+4
	mov	r3, #0
	mov	r4, fp
	ldr	r0, [r5, #3684]
	str	r3, [r5, #3680]
	bl	kfree
	ldr	r0, [r5, #3688]
	bl	kfree
.L2042:
	ldr	r3, .L2103
	mov	r0, r4
	ldr	r2, [sp, #108]
	ldr	r3, [r3]
	cmp	r2, r3
	beq	.L2081
	bl	__stack_chk_fail
.L2073:
	mov	r2, #512
	mov	r1, #0
	ldr	r0, [r4, #3688]
	bl	memset
	ldrh	r6, [r4, #10]
	ldr	r3, [sp, #12]
	mul	r9, r6, r3
	ldr	r3, [r4, #3264]
	cmp	r3, #0
	moveq	r8, #6
	beq	.L2052
	ldr	r3, [r4, #3268]
	cmp	r3, #0
	moveq	r8, #6
	movne	r8, #9
.L2052:
	mov	r10, r9
	mov	r5, #0
.L2053:
	ldr	r3, [r4, #3252]
	mov	r1, r10
	mov	r0, #0
	add	r5, r5, #1
	blx	r3
	ldr	r3, [sp, #16]
	add	r10, r10, r6
	cmp	r3, r5
	bhi	.L2053
	cmp	r8, #9
	movne	r5, #0
	bne	.L2054
	ldr	r5, [r4, #3688]
	mov	r2, #1024
	mov	r1, #0
	mov	r0, r5
	bl	ftl_memset
	ldr	r3, .L2103+16
	mov	r1, #12
	mov	r2, #4
	str	r1, [r5, #4]
	strb	r2, [r5, #17]
	add	r0, r5, r1
	str	r3, [r5]
	mov	r3, #0
	ldrh	r2, [r4, #10]
	str	r3, [r5, #12]
	strb	r3, [r5, #16]
	strh	r2, [r5, #18]	@ movhi
	mov	r2, #16
	strb	r3, [r5, #20]
	strb	r2, [r5, #21]
	strh	r3, [r5, #22]	@ movhi
	bl	js_hash
	str	r0, [r5, #8]
.L2054:
	ldr	r3, [sp, #16]
	mov	r10, r7
	mul	r3, r6, r3
	mov	r6, #0
	str	r3, [sp, #32]
.L2055:
	ldr	r3, [sp, #32]
	cmp	r6, r3
	beq	.L2062
	cmp	r8, #9
	lslne	r3, r6, #2
	addeq	r3, r6, #1
	cmp	r6, #0
	cmpeq	r8, #9
	str	r3, [sp, #44]
	movw	r3, #61424
	str	r3, [sp, #48]
	moveq	r0, #1
	movne	r0, #0
	bne	.L2058
	ldr	r3, [r4, #3264]
	mov	r0, #70
	blx	r3
	mov	r2, r5
	add	r3, sp, #44
	mov	r1, r9
	ldr	ip, [r4, #3256]
	mov	r0, #0
	blx	ip
	ldr	r3, [r4, #3264]
	str	r0, [sp, #36]
	ldrb	r0, [r4, #22]	@ zero_extendqisi2
	blx	r3
	ldr	r2, [sp, #36]
	cmn	r2, #1
	bne	.L2059
.L2062:
	ldrb	r3, [r4, #14]	@ zero_extendqisi2
	ldr	r2, [sp, #12]
	ldr	r6, [r4, #3688]
	str	r3, [sp, #32]
	ldrh	r3, [r4, #10]
	mul	r9, r3, r2
	ldr	r2, [r4, #3264]
	cmp	r2, #0
	moveq	r8, #6
	beq	.L2061
	ldr	r2, [r4, #3268]
	cmp	r2, #0
	moveq	r8, #6
	movne	r8, #9
.L2061:
	ldr	r2, [sp, #16]
	mov	r5, #0
	mul	r10, r3, r2
.L2064:
	cmp	r5, r10
	beq	.L2068
	cmp	r5, #0
	cmpeq	r8, #9
	moveq	r0, #1
	movne	r0, #0
	bne	.L2065
	ldr	r3, [r4, #3264]
	mov	r0, #70
	blx	r3
	ldr	r3, [r4, #3268]
	mov	r0, #2
	blx	r3
	mov	r2, r6
	mov	r1, r9
	ldr	ip, [r4, #3260]
	add	r3, sp, #44
	mov	r0, #0
	blx	ip
	ldr	r3, [r4, #3268]
	ldr	r0, [sp, #32]
	blx	r3
	ldr	r3, [r4, #3264]
	ldrb	r0, [r4, #22]	@ zero_extendqisi2
	blx	r3
	ldr	r3, [r6]
	ldr	r2, .L2103+16
	cmp	r3, r2
	beq	.L2066
.L2068:
	ldr	r0, [r4, #3688]
	mov	r3, r7
	mov	r5, #0
	mov	r2, r0
.L2067:
	mov	r6, r2
	mov	r8, r3
	ldr	ip, [r6]
	add	r2, r2, #4
	ldr	r1, [r8]
	add	r3, r3, #4
	cmp	ip, r1
	beq	.L2070
	mov	r2, #512
	mov	r1, #0
	bl	memset
	ldr	r0, .L2103+20
	str	r5, [sp]
	ldr	r1, [sp, #12]
	ldr	r3, [r8]
	ldr	r2, [r6]
	bl	sftl_printk
	ldrh	r1, [r4, #10]
	mov	r0, #0
	ldr	r2, [sp, #12]
	ldr	r3, [r4, #3252]
	mul	r1, r2, r1
	blx	r3
.L2071:
	ldr	r3, [sp, #24]
	str	r3, [sp, #12]
	b	.L2051
.L2058:
	add	r3, sp, #44
	mov	r2, r10
	add	r1, r9, r6
	ldr	ip, [r4, #3256]
	blx	ip
	cmn	r0, #1
	beq	.L2062
	add	r10, r10, #2048
.L2059:
	add	r6, r6, #1
	b	.L2055
.L2065:
	add	r3, sp, #44
	mov	r2, r6
	add	r1, r9, r5
	ldr	ip, [r4, #3260]
	blx	ip
	cmn	r0, #1
	beq	.L2068
	ldr	r2, [sp, #48]
	movw	r3, #61424
	cmp	r2, r3
	bne	.L2068
	add	r6, r6, #2048
.L2066:
	add	r5, r5, #1
	b	.L2064
.L2070:
	ldr	r1, [sp, #28]
	add	r5, r5, #1
	cmp	r5, r1
	bne	.L2067
	ldr	r3, [sp, #20]
	add	r3, r3, #1
	cmp	r3, #5
	str	r3, [sp, #20]
	bls	.L2071
	b	.L2074
.L2043:
	cmp	r0, #64
	mov	r4, r0
	mov	r6, r1
	mov	r7, r2
	bne	.L2075
	mov	r0, #262144
	bl	ftl_malloc
	str	r0, [r5, #3684]
	mov	r0, #262144
	bl	ftl_malloc
	ldr	r3, [r5, #3684]
	str	r0, [r5, #3688]
	cmp	r3, #0
	cmpne	r0, #0
	beq	.L2076
	mov	r2, #1
	mov	r1, r9
	str	r2, [r5, #3680]
	mov	r0, r3
	mov	r2, #262144
	bl	ftl_memset
.L2075:
	ldr	r3, [r5, #3680]
	cmp	r3, #0
	beq	.L2102
	cmp	r4, #63
	ldr	r0, [r5, #3684]
	ldrhi	r3, .L2103+24
	rsbls	r1, r4, #64
	subls	r6, r6, r1
	movhi	r1, r7
	addls	r1, r7, r1, lsl #9
	addhi	r3, r4, r3
	addhi	r0, r0, r3, lsl #9
	cmp	r10, #576
	rsbcs	r8, r8, #576
	addcs	r6, r6, r8
	lsl	r2, r6, #9
	bl	ftl_memcpy
	b	.L2102
.L2076:
	ldr	r1, .L2103+28
	ldr	r0, .L2103+32
	bl	sftl_printk
	b	.L2075
.L2081:
	add	sp, sp, #116
	@ sp needed
	pop	{r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L2104:
	.align	2
.L2103:
	.word	__stack_chk_guard
	.word	.LANCHOR0
	.word	-52655045
	.word	.LC130
	.word	1179535694
	.word	.LC131
	.word	8388544
	.word	.LANCHOR1+633
	.word	.LC132
	.fnend
	.size	FtlWriteToIDB, .-FtlWriteToIDB
	.align	2
	.global	sftl_write
	.syntax unified
	.arm
	.fpu softvfp
	.type	sftl_write, %function
sftl_write:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	push	{r4, r5, r6, lr}
	.save {r4, r5, r6, lr}
	mov	r5, r1
	mov	r6, r2
	mov	r4, r0
	bl	FtlWriteToIDB
	mov	r3, r6
	mov	r2, r5
	mov	r1, r4
	mov	r0, #0
	pop	{r4, r5, r6, lr}
	b	FtlWrite
	.fnend
	.size	sftl_write, .-sftl_write
	.align	2
	.global	rk_sftl_vendor_dev_ops_register
	.syntax unified
	.arm
	.fpu softvfp
	.type	rk_sftl_vendor_dev_ops_register, %function
rk_sftl_vendor_dev_ops_register:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	@ link register save eliminated.
	ldr	r2, .L2110
	ldr	r3, [r2, #3692]
	cmp	r3, #0
	streq	r0, [r2, #3692]
	moveq	r0, r3
	streq	r1, [r2, #3696]
	mvnne	r0, #0
	bx	lr
.L2111:
	.align	2
.L2110:
	.word	.LANCHOR0
	.fnend
	.size	rk_sftl_vendor_dev_ops_register, .-rk_sftl_vendor_dev_ops_register
	.align	2
	.global	rk_sftl_vendor_storage_init
	.syntax unified
	.arm
	.fpu softvfp
	.type	rk_sftl_vendor_storage_init, %function
rk_sftl_vendor_storage_init:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	push	{r4, r5, r6, r7, r8, r9, r10, lr}
	.save {r4, r5, r6, r7, r8, r9, r10, lr}
	mov	r0, #65536
	ldr	r5, .L2122
	bl	ftl_malloc
	cmp	r0, #0
	str	r0, [r5, #3700]
	mvneq	r9, #11
	beq	.L2112
	ldr	r10, .L2122+4
	mov	r7, #0
	mov	r6, r7
	mov	r8, r7
.L2116:
	ldr	r3, [r5, #3692]
	mov	r1, #128
	ldr	r2, [r5, #3700]
	lsl	r0, r8, #7
	blx	r3
	subs	r9, r0, #0
	bne	.L2114
	ldr	r4, [r5, #3700]
	ldr	r3, [r4]
	cmp	r3, r10
	bne	.L2115
	add	r2, r4, #61440
	ldr	r3, [r4, #4]
	ldr	r2, [r2, #4092]
	cmp	r3, r6
	sub	r2, r2, r3
	clz	r2, r2
	lsr	r2, r2, #5
	movls	r2, #0
	cmp	r2, #0
	movne	r7, r8
	movne	r6, r3
.L2115:
	add	r8, r8, #1
	cmp	r8, #2
	bne	.L2116
	cmp	r6, #0
	beq	.L2117
	ldr	r3, [r5, #3692]
	mov	r2, r4
	mov	r1, #128
	lsl	r0, r7, #7
	blx	r3
	subs	r9, r0, #0
	beq	.L2112
.L2114:
	ldr	r0, [r5, #3700]
	mvn	r9, #0
	bl	kfree
	mov	r3, #0
	str	r3, [r5, #3700]
	b	.L2112
.L2117:
	mov	r2, #65536
	mov	r1, r6
	mov	r0, r4
	bl	memset
	mov	r3, #1
	add	r2, r4, #61440
	str	r3, [r4, #4]
	str	r10, [r4]
	str	r3, [r2, #4092]
	ldr	r3, .L2122+8
	strh	r6, [r4, #12]	@ movhi
	strh	r3, [r4, #14]	@ movhi
.L2112:
	mov	r0, r9
	pop	{r4, r5, r6, r7, r8, r9, r10, pc}
.L2123:
	.align	2
.L2122:
	.word	.LANCHOR0
	.word	1380668996
	.word	-1032
	.fnend
	.size	rk_sftl_vendor_storage_init, .-rk_sftl_vendor_storage_init
	.align	2
	.global	rk_sftl_vendor_read
	.syntax unified
	.arm
	.fpu softvfp
	.type	rk_sftl_vendor_read, %function
rk_sftl_vendor_read:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	ldr	r3, .L2134
	ldr	ip, [r3, #3700]
	cmp	ip, #0
	beq	.L2129
	push	{r4, r5, r6, lr}
	.save {r4, r5, r6, lr}
	mov	r3, #0
	ldrh	r4, [ip, #10]
.L2126:
	cmp	r3, r4
	bcc	.L2128
	mvn	r0, #0
	pop	{r4, r5, r6, pc}
.L2128:
	add	lr, ip, r3, lsl #3
	ldrh	r5, [lr, #16]
	cmp	r5, r0
	bne	.L2127
	ldrh	r4, [lr, #20]
	mov	r0, r1
	ldrh	r1, [lr, #18]
	cmp	r4, r2
	movcs	r4, r2
	add	r1, r1, #1024
	mov	r2, r4
	add	r1, ip, r1
	bl	memcpy
	mov	r0, r4
	pop	{r4, r5, r6, pc}
.L2127:
	add	r3, r3, #1
	b	.L2126
.L2129:
	mvn	r0, #0
	bx	lr
.L2135:
	.align	2
.L2134:
	.word	.LANCHOR0
	.fnend
	.size	rk_sftl_vendor_read, .-rk_sftl_vendor_read
	.align	2
	.global	rk_sftl_vendor_write
	.syntax unified
	.arm
	.fpu softvfp
	.type	rk_sftl_vendor_write, %function
rk_sftl_vendor_write:
	.fnstart
	@ args = 0, pretend = 0, frame = 24
	@ frame_needed = 0, uses_anonymous_args = 0
	push	{r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.save {r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.pad #28
	sub	sp, sp, #28
	ldr	r9, .L2156
	ldr	r4, [r9, #3700]
	cmp	r4, #0
	beq	.L2151
	mov	r8, r2
	ldrh	r2, [r4, #10]
	add	r6, r8, #63
	ldrh	r3, [r4, #8]
	mov	fp, r1
	bic	r6, r6, #63
	mov	r7, #0
	str	r3, [sp, #4]
.L2138:
	cmp	r7, r2
	bcc	.L2146
	ldrh	r1, [r4, #14]
	cmp	r6, r1
	bhi	.L2151
	add	r3, r4, r2, lsl #3
	uxth	r6, r6
	strh	r0, [r3, #16]	@ movhi
	ldrh	r2, [r4, #12]
	strh	r8, [r3, #20]	@ movhi
	strh	r2, [r3, #18]	@ movhi
	add	r2, r2, r6
	sub	r6, r1, r6
	strh	r2, [r4, #12]	@ movhi
	strh	r6, [r4, #14]	@ movhi
	mov	r2, r8
	ldrh	r0, [r3, #18]
	mov	r1, fp
	add	r0, r0, #1024
	add	r0, r4, r0
	bl	memcpy
	ldrh	r3, [r4, #10]
	add	r2, r4, #61440
	add	r3, r3, #1
	strh	r3, [r4, #10]	@ movhi
	ldr	r3, [r4, #4]
	add	r3, r3, #1
	str	r3, [r4, #4]
	str	r3, [r2, #4092]
	ldrh	r3, [r4, #8]
	add	r3, r3, #1
	uxth	r3, r3
	cmp	r3, #1
	movhi	r3, #0
	strh	r3, [r4, #8]	@ movhi
	ldr	r3, [r9, #3696]
	b	.L2155
.L2146:
	add	r5, r4, r7, lsl #3
	ldrh	r3, [r5, #16]
	cmp	r3, r0
	str	r3, [sp, #8]
	bne	.L2139
	ldrh	r1, [r5, #20]
	add	r3, r4, #1024
	add	r1, r1, #63
	bic	r1, r1, #63
	cmp	r8, r1
	str	r1, [sp, #12]
	bls	.L2140
	ldrh	r1, [r4, #14]
	cmp	r6, r1
	subls	r2, r2, #1
	ldrhls	r10, [r5, #18]
	strls	r2, [sp, #16]
	bls	.L2141
.L2151:
	mvn	r0, #0
	b	.L2136
.L2142:
	ldrh	r9, [r5, #20]
	add	r0, r3, r10
	ldrh	r2, [r5, #16]
	add	r7, r7, #1
	ldrh	r1, [r5, #18]
	strh	r9, [r5, #12]	@ movhi
	add	r9, r9, #63
	bic	r9, r9, #63
	strh	r2, [r5, #8]	@ movhi
	strh	r10, [r5, #10]	@ movhi
	add	r1, r3, r1
	mov	r2, r9
	str	r3, [sp, #20]
	bl	memcpy
	ldr	r3, [sp, #20]
	add	r10, r10, r9
.L2141:
	ldr	r2, [sp, #16]
	add	r5, r5, #8
	cmp	r7, r2
	bcc	.L2142
	ldrh	r2, [sp, #8]
	add	r7, r4, r7, lsl #3
	uxth	r5, r10
	uxtah	r0, r3, r10
	strh	r8, [r7, #20]	@ movhi
	strh	r2, [r7, #16]	@ movhi
	mov	r1, fp
	strh	r5, [r7, #18]	@ movhi
	mov	r2, r8
	bl	memcpy
	uxth	r3, r6
	ldrh	r6, [r4, #14]
	add	r5, r5, r3
	sub	r6, r6, r3
	ldr	r3, [sp, #12]
	strh	r5, [r4, #12]	@ movhi
	add	r6, r6, r3
	strh	r6, [r4, #14]	@ movhi
.L2143:
	ldr	r3, [r4, #4]
	add	r2, r4, #61440
	add	r3, r3, #1
	str	r3, [r4, #4]
	str	r3, [r2, #4092]
	ldrh	r3, [r4, #8]
	add	r3, r3, #1
	uxth	r3, r3
	cmp	r3, #1
	movhi	r3, #0
	strh	r3, [r4, #8]	@ movhi
	ldr	r3, .L2156
	ldr	r3, [r3, #3696]
.L2155:
	ldr	r0, [sp, #4]
	mov	r2, r4
	mov	r1, #128
	lsl	r0, r0, #7
	blx	r3
	mov	r0, #0
.L2136:
	add	sp, sp, #28
	@ sp needed
	pop	{r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L2140:
	ldrh	r0, [r5, #18]
	mov	r2, r8
	mov	r1, fp
	add	r0, r3, r0
	bl	memcpy
	strh	r8, [r5, #20]	@ movhi
	b	.L2143
.L2139:
	add	r7, r7, #1
	b	.L2138
.L2157:
	.align	2
.L2156:
	.word	.LANCHOR0
	.fnend
	.size	rk_sftl_vendor_write, .-rk_sftl_vendor_write
	.align	2
	.global	rk_sftl_vendor_storage_ioctl
	.syntax unified
	.arm
	.fpu softvfp
	.type	rk_sftl_vendor_storage_ioctl, %function
rk_sftl_vendor_storage_ioctl:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	push	{r4, r5, r6, r7, r8, lr}
	.save {r4, r5, r6, r7, r8, lr}
	mov	r0, #4096
	mov	r5, r2
	mov	r6, r1
	bl	ftl_malloc
	subs	r4, r0, #0
	mvneq	r5, #0
	beq	.L2158
	ldr	r3, .L2183
	cmp	r6, r3
	beq	.L2161
	add	r3, r3, #1
	cmp	r6, r3
	beq	.L2162
.L2180:
	mvn	r5, #13
	b	.L2160
.L2161:
	ldr	r6, .L2183+4
	mov	r3, sp
	and	r3, r3, r6
	ldr	r3, [r3, #8]
	.syntax divided
@ 114 "./include/linux/uaccess.h" 1
	adds r2, r5, #8; sbcccs r2, r2, r3; movcc r3, #0
@ 0 "" 2
	.arm
	.syntax unified
	cmp	r3, #0
	mov	r2, #8
	bne	.L2163
	mov	r1, r5
	bl	arm_copy_from_user
	subs	r2, r0, #0
	beq	.L2164
.L2163:
	rsb	r0, r2, #8
	mov	r1, #0
	add	r0, r4, r0
	bl	memset
	mov	r2, r5
	mov	r1, #256
.L2182:
	ldr	r0, .L2183+8
	bl	sftl_printk
	b	.L2180
.L2165:
	mov	r3, sp
	uxth	r2, r0
	and	r6, r6, r3
	strh	r0, [r4, #6]	@ movhi
	add	r2, r2, #8
	ldr	r3, [r6, #8]
	.syntax divided
@ 132 "./include/linux/uaccess.h" 1
	adds r1, r5, r2; sbcccs r1, r1, r3; movcc r3, #0
@ 0 "" 2
	.arm
	.syntax unified
	cmp	r3, #0
	bne	.L2180
	mov	r0, r5
	mov	r1, r4
	bl	arm_copy_to_user
	subs	r5, r0, #0
	beq	.L2160
	b	.L2180
.L2162:
	ldr	r7, .L2183+4
	mov	r3, sp
	and	r3, r3, r7
	ldr	r3, [r3, #8]
	.syntax divided
@ 114 "./include/linux/uaccess.h" 1
	adds r2, r5, #8; sbcccs r2, r2, r3; movcc r3, #0
@ 0 "" 2
	.arm
	.syntax unified
	cmp	r3, #0
	mov	r2, #8
	bne	.L2167
	mov	r1, r5
	bl	arm_copy_from_user
	subs	r2, r0, #0
	beq	.L2168
.L2167:
	rsb	r0, r2, #8
	mov	r1, #0
	add	r0, r4, r0
	bl	memset
	mov	r2, r5
	mov	r1, #276
	b	.L2182
.L2164:
	ldr	r2, [r4]
	ldr	r3, .L2183+12
	cmp	r2, r3
	bne	.L2169
	ldrh	r2, [r4, #6]
	add	r1, r4, #8
	ldrh	r0, [r4, #4]
	bl	rk_sftl_vendor_read
	cmn	r0, #1
	bne	.L2165
.L2169:
	mvn	r5, #0
.L2160:
	mov	r0, r4
	bl	kfree
.L2158:
	mov	r0, r5
	pop	{r4, r5, r6, r7, r8, pc}
.L2168:
	ldr	r2, [r4]
	ldr	r3, .L2183+12
	cmp	r2, r3
	bne	.L2169
	ldrh	r6, [r4, #6]
	movw	r3, #4087
	cmp	r6, r3
	bhi	.L2169
	mov	r3, sp
	add	r6, r6, #8
	and	r7, r7, r3
	ldr	r3, [r7, #8]
	.syntax divided
@ 114 "./include/linux/uaccess.h" 1
	adds r2, r5, r6; sbcccs r2, r2, r3; movcc r3, #0
@ 0 "" 2
	.arm
	.syntax unified
	cmp	r3, #0
	mov	r2, r6
	bne	.L2170
	mov	r1, r5
	mov	r0, r4
	bl	arm_copy_from_user
	subs	r2, r0, #0
	beq	.L2171
.L2170:
	sub	r6, r6, r2
	mov	r1, #0
	add	r0, r4, r6
	bl	memset
	mov	r2, r5
	movw	r1, #283
	b	.L2182
.L2171:
	ldrh	r2, [r4, #6]
	add	r1, r4, #8
	ldrh	r0, [r4, #4]
	bl	rk_sftl_vendor_write
	mov	r5, r0
	b	.L2160
.L2184:
	.align	2
.L2183:
	.word	1074034177
	.word	-8192
	.word	.LC133
	.word	1448232273
	.fnend
	.size	rk_sftl_vendor_storage_ioctl, .-rk_sftl_vendor_storage_ioctl
	.align	2
	.global	rk_sftl_vendor_register
	.syntax unified
	.arm
	.fpu softvfp
	.type	rk_sftl_vendor_register, %function
rk_sftl_vendor_register:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	@ link register save eliminated.
	ldr	r0, .L2186
	b	misc_register
.L2187:
	.align	2
.L2186:
	.word	.LANCHOR2+12
	.fnend
	.size	rk_sftl_vendor_register, .-rk_sftl_vendor_register
	.global	g_nand_ops
	.global	g_nand_phy_info
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
	.global	sftl_nand_check_spare_buf
	.global	sftl_temp_buf
	.global	sftl_nand_check_buf
	.global	g_cur_erase_blk
	.global	g_gc_skip_write_count
	.global	g_gc_head_data_block_count
	.global	g_gc_head_data_block
	.global	g_ftl_nand_free_count
	.global	g_in_swl_replace
	.global	g_in_gc_progress
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
	.global	p_plane_order_table
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
	.align	3
	.set	.LANCHOR1,. + 0
	.type	__func__.7564, %object
	.size	__func__.7564, 17
__func__.7564:
	.ascii	"INSERT_DATA_LIST\000"
	.type	__func__.7559, %object
	.size	__func__.7559, 17
__func__.7559:
	.ascii	"INSERT_FREE_LIST\000"
	.type	__func__.7595, %object
	.size	__func__.7595, 17
__func__.7595:
	.ascii	"List_remove_node\000"
	.type	__func__.7627, %object
	.size	__func__.7627, 22
__func__.7627:
	.ascii	"List_update_data_list\000"
	.type	__func__.7734, %object
	.size	__func__.7734, 22
__func__.7734:
	.ascii	"select_l2p_ram_region\000"
	.type	__func__.8057, %object
	.size	__func__.8057, 16
__func__.8057:
	.ascii	"make_superblock\000"
	.type	__func__.8233, %object
	.size	__func__.8233, 19
__func__.8233:
	.ascii	"get_new_active_ppa\000"
	.type	__func__.13957, %object
	.size	__func__.13957, 17
__func__.13957:
	.ascii	"FlashEraseBlocks\000"
	.type	__func__.7669, %object
	.size	__func__.7669, 26
__func__.7669:
	.ascii	"ftl_map_blk_alloc_new_blk\000"
	.type	__func__.7174, %object
	.size	__func__.7174, 11
__func__.7174:
	.ascii	"FtlMemInit\000"
	.type	__func__.7401, %object
	.size	__func__.7401, 14
__func__.7401:
	.ascii	"FtlBbt2Bitmap\000"
	.type	__func__.8078, %object
	.size	__func__.8078, 18
__func__.8078:
	.ascii	"SupperBlkListInit\000"
	.type	__func__.13914, %object
	.size	__func__.13914, 15
__func__.13914:
	.ascii	"FlashReadPages\000"
	.type	__func__.7846, %object
	.size	__func__.7846, 14
__func__.7846:
	.ascii	"FtlScanSysBlk\000"
	.type	__func__.7444, %object
	.size	__func__.7444, 11
__func__.7444:
	.ascii	"FtlLoadBbt\000"
	.type	__func__.13933, %object
	.size	__func__.13933, 15
__func__.13933:
	.ascii	"FlashProgPages\000"
	.type	__func__.8205, %object
	.size	__func__.8205, 25
__func__.8205:
	.ascii	"allocate_data_superblock\000"
	.type	__func__.8246, %object
	.size	__func__.8246, 16
__func__.8246:
	.ascii	"update_vpc_list\000"
	.type	__func__.8253, %object
	.size	__func__.8253, 20
__func__.8253:
	.ascii	"decrement_vpc_count\000"
	.type	__func__.7695, %object
	.size	__func__.7695, 31
__func__.7695:
	.ascii	"Ftl_write_map_blk_to_last_page\000"
	.type	__func__.7709, %object
	.size	__func__.7709, 16
__func__.7709:
	.ascii	"FtlMapWritePage\000"
	.type	__func__.7636, %object
	.size	__func__.7636, 16
__func__.7636:
	.ascii	"load_l2p_region\000"
	.type	__func__.7680, %object
	.size	__func__.7680, 15
__func__.7680:
	.ascii	"ftl_map_blk_gc\000"
	.type	__func__.7751, %object
	.size	__func__.7751, 9
__func__.7751:
	.ascii	"log2phys\000"
	.type	__func__.7966, %object
	.size	__func__.7966, 16
__func__.7966:
	.ascii	"FtlReUsePrevPpa\000"
	.type	__func__.8000, %object
	.size	__func__.8000, 22
__func__.8000:
	.ascii	"FtlRecoverySuperblock\000"
	.type	__func__.8120, %object
	.size	__func__.8120, 14
__func__.8120:
	.ascii	"ftl_check_vpc\000"
	.type	__func__.7824, %object
	.size	__func__.7824, 15
__func__.7824:
	.ascii	"FtlVpcTblFlush\000"
	.type	__func__.8103, %object
	.size	__func__.8103, 21
__func__.8103:
	.ascii	"FtlVpcCheckAndModify\000"
	.type	__func__.8226, %object
	.size	__func__.8226, 29
__func__.8226:
	.ascii	"allocate_new_data_superblock\000"
	.type	__func__.7298, %object
	.size	__func__.7298, 13
__func__.7298:
	.ascii	"FtlProgPages\000"
	.type	__func__.8323, %object
	.size	__func__.8323, 19
__func__.8323:
	.ascii	"FtlGcFreeTempBlock\000"
	.type	__func__.8436, %object
	.size	__func__.8436, 23
__func__.8436:
	.ascii	"rk_ftl_garbage_collect\000"
	.type	__func__.7326, %object
	.size	__func__.7326, 9
__func__.7326:
	.ascii	"FtlWrite\000"
	.type	__func__.7898, %object
	.size	__func__.7898, 15
__func__.7898:
	.ascii	"FtlLoadSysInfo\000"
	.type	__func__.7920, %object
	.size	__func__.7920, 18
__func__.7920:
	.ascii	"FtlMapTblRecovery\000"
	.type	__func__.14076, %object
	.size	__func__.14076, 14
__func__.14076:
	.ascii	"FtlWriteToIDB\000"
	.space	1
	.type	rk_sftl_vendor_storage_fops, %object
	.size	rk_sftl_vendor_storage_fops, 160
rk_sftl_vendor_storage_fops:
	.space	36
	.word	rk_sftl_vendor_storage_ioctl
	.word	rk_sftl_vendor_storage_ioctl
	.space	116
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
	.type	rkflash_vender_storage_dev, %object
	.size	rkflash_vender_storage_dev, 40
rkflash_vender_storage_dev:
	.word	255
	.word	.LC134
	.word	rk_sftl_vendor_storage_fops
	.space	28
	.bss
	.align	2
	.set	.LANCHOR0,. + 0
	.type	g_nand_phy_info, %object
	.size	g_nand_phy_info, 24
g_nand_phy_info:
	.space	24
	.type	g_active_superblock, %object
	.size	g_active_superblock, 48
g_active_superblock:
	.space	48
	.type	p_valid_page_count_table, %object
	.size	p_valid_page_count_table, 4
p_valid_page_count_table:
	.space	4
	.type	g_buffer_superblock, %object
	.size	g_buffer_superblock, 48
g_buffer_superblock:
	.space	48
	.type	g_gc_temp_superblock, %object
	.size	g_gc_temp_superblock, 48
g_gc_temp_superblock:
	.space	48
	.type	g_gc_superblock, %object
	.size	g_gc_superblock, 48
g_gc_superblock:
	.space	48
	.type	g_gc_next_blk_1, %object
	.size	g_gc_next_blk_1, 2
g_gc_next_blk_1:
	.space	2
	.type	g_gc_next_blk, %object
	.size	g_gc_next_blk, 2
g_gc_next_blk:
	.space	2
	.type	g_num_free_superblocks, %object
	.size	g_num_free_superblocks, 2
g_num_free_superblocks:
	.space	2
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
	.type	c_ftl_nand_sec_pre_page, %object
	.size	c_ftl_nand_sec_pre_page, 2
c_ftl_nand_sec_pre_page:
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
	.type	p_erase_count_table, %object
	.size	p_erase_count_table, 4
p_erase_count_table:
	.space	4
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
	.type	c_ftl_nand_data_op_blks_per_plane, %object
	.size	c_ftl_nand_data_op_blks_per_plane, 2
c_ftl_nand_data_op_blks_per_plane:
	.space	2
	.space	2
	.type	gSysInfo, %object
	.size	gSysInfo, 16
gSysInfo:
	.space	16
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
	.type	g_nand_ops, %object
	.size	g_nand_ops, 24
g_nand_ops:
	.space	24
	.type	req_erase, %object
	.size	req_erase, 4
req_erase:
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
	.type	p_io_data_buf_0, %object
	.size	p_io_data_buf_0, 4
p_io_data_buf_0:
	.space	4
	.type	p_io_data_buf_1, %object
	.size	p_io_data_buf_1, 4
p_io_data_buf_1:
	.space	4
	.type	sftl_nand_check_buf, %object
	.size	sftl_nand_check_buf, 4
sftl_nand_check_buf:
	.space	4
	.type	sftl_temp_buf, %object
	.size	sftl_temp_buf, 4
sftl_temp_buf:
	.space	4
	.type	sftl_nand_check_spare_buf, %object
	.size	sftl_nand_check_spare_buf, 4
sftl_nand_check_spare_buf:
	.space	4
	.type	p_sys_spare_buf, %object
	.size	p_sys_spare_buf, 4
p_sys_spare_buf:
	.space	4
	.type	p_io_spare_buf, %object
	.size	p_io_spare_buf, 4
p_io_spare_buf:
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
	.type	g_tmp_data_superblock_id, %object
	.size	g_tmp_data_superblock_id, 2
g_tmp_data_superblock_id:
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
	.type	req_sys, %object
	.size	req_sys, 20
req_sys:
	.space	20
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
	.type	gVendorBlkInfo, %object
	.size	gVendorBlkInfo, 44
gVendorBlkInfo:
	.space	44
	.type	g_ect_tbl_power_up_flush, %object
	.size	g_ect_tbl_power_up_flush, 2
g_ect_tbl_power_up_flush:
	.space	2
	.space	2
	.type	gc_discard_updated, %object
	.size	gc_discard_updated, 4
gc_discard_updated:
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
	.type	g_ftl_nand_free_count, %object
	.size	g_ftl_nand_free_count, 4
g_ftl_nand_free_count:
	.space	4
	.type	g_MaxLbn, %object
	.size	g_MaxLbn, 4
g_MaxLbn:
	.space	4
	.type	idb_need_write_back, %object
	.size	idb_need_write_back, 4
idb_need_write_back:
	.space	4
	.type	idb_buf, %object
	.size	idb_buf, 4
idb_buf:
	.space	4
	.type	gp_flash_check_buf, %object
	.size	gp_flash_check_buf, 4
gp_flash_check_buf:
	.space	4
	.type	_flash_read, %object
	.size	_flash_read, 4
_flash_read:
	.space	4
	.type	_flash_write, %object
	.size	_flash_write, 4
_flash_write:
	.space	4
	.type	g_vendor, %object
	.size	g_vendor, 4
g_vendor:
	.space	4
	.type	check_vpc_table, %object
	.size	check_vpc_table, 16384
check_vpc_table:
	.space	16384
	.type	gp_last_act_superblock, %object
	.size	gp_last_act_superblock, 4
gp_last_act_superblock:
	.space	4
	.section	.rodata.str1.1,"aMS",%progbits,1
.LC0:
	.ascii	"SFTL version: 5.0.55 20200925\000"
.LC1:
	.ascii	"\012%s\012\000"
.LC2:
	.ascii	"act blk: %x %x %x %x %x %x\012\000"
.LC3:
	.ascii	"buf blk: %x %x %x %x %x %x\012\000"
.LC4:
	.ascii	"tmp blk: %x %x %x %x %x %x\012\000"
.LC5:
	.ascii	"gc blk: %x %x %x %x %x %x\012\000"
.LC6:
	.ascii	"free blk: %x %x %x\012\000"
.LC7:
	.ascii	"phyBlk = 0x%x die = %d block_in_die = 0x%x 0x%8x\012"
	.ascii	"\000"
.LC8:
	.ascii	"\012!!!!! error @ func:%s - line:%d\012\000"
.LC9:
	.ascii	"FLASH INFO:\012\000"
.LC10:
	.ascii	"Device Capacity: %d MB\012\000"
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
	.ascii	"bad blk num = %d\012\000"
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
	.ascii	"WR_CHK = %x %x %x\012\000"
.LC66:
	.ascii	"Read Err Cnt = 0x%x\012\000"
.LC67:
	.ascii	"Prog Err Cnt = 0x%x\012\000"
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
	.ascii	"swblk %x ,avg = %x max= %x vpc= %x,ec=%x ,max ec=%x"
	.ascii	"\012\000"
.LC79:
	.ascii	"FtlGcRefreshBlock  0x%x\012\000"
.LC80:
	.ascii	"FtlGcMarkBadPhyBlk %d 0x%x\012\000"
.LC81:
	.ascii	"%s %p + 0x%x:\000"
.LC82:
	.ascii	"0x%08x,\000"
.LC83:
	.ascii	"0x%04x,\000"
.LC84:
	.ascii	"0x%02x,\000"
.LC85:
	.ascii	"\012\000"
.LC86:
	.ascii	"%s: addr: %x is in id block!!!!!!!!!!\012\000"
.LC87:
	.ascii	"not free: w: d:\000"
.LC88:
	.ascii	"not free: w: s:\000"
.LC89:
	.ascii	"FtlFreeSysBlkQueueOut = %x, free count = %d, error\012"
	.ascii	"\000"
.LC90:
	.ascii	"FtlFreeSysBlkQueueOut = %x, free count = %d\012\000"
.LC91:
	.ascii	"%s error allocating memory. return -1\012\000"
.LC92:
	.ascii	"FlashReadPages %x %x error_ecc_bits %d\012\000"
.LC93:
	.ascii	"data:\000"
.LC94:
	.ascii	"spare:\000"
.LC95:
	.ascii	"prog read error: = %x\012\000"
.LC96:
	.ascii	"prog read REFRESH: = %x\012\000"
.LC97:
	.ascii	"prog read s error: = %x %x %x\012\000"
.LC98:
	.ascii	"prog read d error: = %x %x %x\012\000"
.LC99:
	.ascii	"FtlBbmTblFlush id=%x,page=%x,previd=%x cnt=%d\012\000"
.LC100:
	.ascii	"FtlBbmTblFlush error:%x\012\000"
.LC101:
	.ascii	"FtlBbmTblFlush error = %x error count = %d\012\000"
.LC102:
	.ascii	"FtlGcFreeBadSuperBlk 0x%x\012\000"
.LC103:
	.ascii	"decrement_vpc_count %x = %d\012\000"
.LC104:
	.ascii	"FtlMapWritePage error = %x \012\000"
.LC105:
	.ascii	"FtlMapWritePage error = %x error count = %d\012\000"
.LC106:
	.ascii	"region_id = %x phyAddr = %x\012\000"
.LC107:
	.ascii	"map_ppn:\000"
.LC108:
	.ascii	"load_l2p_region refresh = %x phyAddr = %x\012\000"
.LC109:
	.ascii	"data prev_ppa = %x error...................\012\000"
.LC110:
	.ascii	"spuer block %x vpn is 0\012 \000"
.LC111:
	.ascii	"...%s enter...\012\000"
.LC112:
	.ascii	"FtlCheckVpc2 %x = %x  %x\012\000"
.LC113:
	.ascii	"free blk vpc error %x = %x  %x\012\000"
.LC114:
	.ascii	"ftl_scan_all_data = %x\012\000"
.LC115:
	.ascii	"scan lpa = %x ppa= %x\012\000"
.LC116:
	.ascii	"lpa = %x,addr= %x,spare= %x %x %x %x data=%x %x\012"
	.ascii	"\000"
.LC117:
	.ascii	"FtlGcScanTempBlk Error ID %x %x!!!!!!! \012\000"
.LC118:
	.ascii	"FtlVendorPartRead refresh = %x phyAddr = %x\012\000"
.LC119:
	.ascii	"no ect\000"
.LC120:
	.ascii	"FtlVpcTblFlush error = %x error count = %d\012\000"
.LC121:
	.ascii	"FtlCheckVpc %x = %x  %x\012\000"
.LC122:
	.ascii	"FtlProgPages error %x = %d\012\000"
.LC123:
	.ascii	"Ftlwrite decrement_vpc_count %x = %d\012\000"
.LC124:
	.ascii	"SWL %x, FSB = %x vpc= %x,ec=%x th=%x\012\000"
.LC125:
	.ascii	"FtlWrite: ecc error:%x %x %x\012\000"
.LC126:
	.ascii	"FtlWrite: lpa error:%x %x\012\000"
.LC127:
	.ascii	"%s hash error this.id =%x page =%x pre_id =%x hash "
	.ascii	"=%x hash_r =%x\012\000"
.LC128:
	.ascii	"%s last blk_id =%x page =%x hash error hash =%x has"
	.ascii	"h_r =%x\012\000"
.LC129:
	.ascii	"%s scan blk_id =%x page =%x hash error hash =%x has"
	.ascii	"h_r =%x\012\000"
.LC130:
	.ascii	"write_idblock %x %x\012\000"
.LC131:
	.ascii	"write_idblock fail! %x %x %x %x\012\000"
.LC132:
	.ascii	"%s idb buffer alloc fail\012\000"
.LC133:
	.ascii	"copy_from_user error %d %p %p\012\000"
.LC134:
	.ascii	"vendor_storage\000"
