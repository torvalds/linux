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
	.syntax unified
	.syntax unified
	.global	__aeabi_uidiv
	.thumb
	.syntax unified
	.text
	.align	1
	.syntax unified
	.thumb
	.thumb_func
	.fpu softvfp
	.type	l2p_addr_tran, %function
l2p_addr_tran:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	push	{r3, r4, r5, r6, r7, r8, r9, lr}
	.save {r3, r4, r5, r6, r7, r8, r9, lr}
	mov	r8, r1
	ldr	r3, .L3
	mov	r9, r2
	ldr	r6, [r0, #4]
	ldrh	r4, [r3, #8]
	ldrh	r5, [r3, #10]
	ldrh	r3, [r3, #14]
	lsrs	r7, r6, #10
	ubfx	r6, r6, #0, #10
	cmp	r3, #4
	uxth	r0, r7
	itt	eq
	lsreq	r4, r4, #1
	lsleq	r5, r5, #1
	uxth	r7, r7
	mov	r1, r4
	it	eq
	uxtheq	r5, r5
	bl	__aeabi_uidiv
	uxth	r0, r0
	mls	r4, r0, r4, r7
	mla	r4, r5, r4, r6
	str	r4, [r8]
	str	r0, [r9]
	movs	r0, #0
	pop	{r3, r4, r5, r6, r7, r8, r9, pc}
.L4:
	.align	2
.L3:
	.word	.LANCHOR0
	.fnend
	.size	l2p_addr_tran, .-l2p_addr_tran
	.align	1
	.global	ftl_print_sblk_info
	.syntax unified
	.thumb
	.thumb_func
	.fpu softvfp
	.type	ftl_print_sblk_info, %function
ftl_print_sblk_info:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	push	{r0, r1, r2, r3, r4, lr}
	.save {r4, lr}
	.pad #16
	ldr	r4, .L6
	ldr	r1, .L6+4
	ldr	r0, .L6+8
	bl	sftl_printk
	ldrh	r1, [r4, #24]
	ldr	r0, [r4, #72]
	ldrb	r3, [r4, #30]	@ zero_extendqisi2
	ldrh	r2, [r4, #26]
	ldrh	r0, [r0, r1, lsl #1]
	str	r0, [sp, #8]
	ldrh	r0, [r4, #28]
	str	r0, [sp, #4]
	ldrb	r0, [r4, #32]	@ zero_extendqisi2
	str	r0, [sp]
	ldr	r0, .L6+12
	bl	sftl_printk
	ldrh	r1, [r4, #76]
	ldr	r0, [r4, #72]
	ldrb	r3, [r4, #82]	@ zero_extendqisi2
	ldrh	r2, [r4, #78]
	ldrh	r0, [r0, r1, lsl #1]
	str	r0, [sp, #8]
	ldrh	r0, [r4, #80]
	str	r0, [sp, #4]
	ldrb	r0, [r4, #84]	@ zero_extendqisi2
	str	r0, [sp]
	ldr	r0, .L6+16
	bl	sftl_printk
	ldrh	r1, [r4, #124]
	ldr	r0, [r4, #72]
	ldrb	r3, [r4, #130]	@ zero_extendqisi2
	ldrh	r2, [r4, #126]
	ldrh	r0, [r0, r1, lsl #1]
	str	r0, [sp, #8]
	ldrh	r0, [r4, #128]
	str	r0, [sp, #4]
	ldrb	r0, [r4, #132]	@ zero_extendqisi2
	str	r0, [sp]
	ldr	r0, .L6+20
	bl	sftl_printk
	ldrh	r1, [r4, #172]
	ldr	r0, [r4, #72]
	ldrb	r3, [r4, #178]	@ zero_extendqisi2
	ldrh	r2, [r4, #174]
	ldrh	r0, [r0, r1, lsl #1]
	str	r0, [sp, #8]
	ldrh	r0, [r4, #176]
	str	r0, [sp, #4]
	ldrb	r0, [r4, #180]	@ zero_extendqisi2
	str	r0, [sp]
	ldr	r0, .L6+24
	bl	sftl_printk
	ldrh	r3, [r4, #220]
	ldrh	r2, [r4, #222]
	ldrh	r1, [r4, #224]
	ldr	r0, .L6+28
	add	sp, sp, #16
	@ sp needed
	pop	{r4, lr}
	b	sftl_printk
.L7:
	.align	2
.L6:
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
	.align	1
	.global	Ftl_log2
	.syntax unified
	.thumb
	.thumb_func
	.fpu softvfp
	.type	Ftl_log2, %function
Ftl_log2:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	@ link register save eliminated.
	movs	r1, #0
	movs	r2, #1
.L9:
	cmp	r2, r0
	uxth	r3, r1
	add	r1, r1, #1
	bls	.L10
	subs	r0, r3, #1
	uxth	r0, r0
	bx	lr
.L10:
	lsls	r2, r2, #1
	b	.L9
	.fnend
	.size	Ftl_log2, .-Ftl_log2
	.align	1
	.global	FtlPrintInfo
	.syntax unified
	.thumb
	.thumb_func
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
	.align	1
	.global	FtlSysBlkNumInit
	.syntax unified
	.thumb
	.thumb_func
	.fpu softvfp
	.type	FtlSysBlkNumInit, %function
FtlSysBlkNumInit:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	@ link register save eliminated.
	ldr	r3, .L13
	cmp	r0, #24
	it	cc
	movcc	r0, #24
	ldrh	r2, [r3, #232]
	ldrh	r1, [r3, #242]
	str	r0, [r3, #228]
	muls	r2, r0, r2
	subs	r0, r1, r0
	ldr	r1, [r3, #248]
	strh	r0, [r3, #240]	@ movhi
	movs	r0, #0
	str	r2, [r3, #236]
	subs	r2, r1, r2
	str	r2, [r3, #244]
	bx	lr
.L14:
	.align	2
.L13:
	.word	.LANCHOR0
	.fnend
	.size	FtlSysBlkNumInit, .-FtlSysBlkNumInit
	.global	__aeabi_idiv
	.align	1
	.global	FtlConstantsInit
	.syntax unified
	.thumb
	.thumb_func
	.fpu softvfp
	.type	FtlConstantsInit, %function
FtlConstantsInit:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	push	{r3, r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.save {r3, r4, r5, r6, r7, r8, r9, r10, fp, lr}
	mov	fp, r0
	ldrh	r1, [fp, #14]
	ldrh	r2, [r0]
	ldr	r4, .L26
	cmp	r1, #4
	ldrh	r6, [r0, #2]
	ldrh	r3, [fp, #6]
	ldrh	r0, [r0, #4]
	strh	r2, [r4, #252]	@ movhi
	strh	r6, [r4, #254]	@ movhi
	strh	r0, [r4, #256]	@ movhi
	strh	r3, [r4, #242]	@ movhi
	strh	r1, [r4, #258]	@ movhi
	bne	.L16
	lsrs	r3, r3, #1
	strh	r3, [r4, #242]	@ movhi
	movs	r3, #8
	strh	r3, [r4, #258]	@ movhi
.L17:
	ldr	r1, .L26+4
	movs	r3, #0
.L18:
	strb	r3, [r3, r1]
	adds	r3, r3, #1
	cmp	r3, #32
	bne	.L18
	ldrh	r5, [r4, #242]
	movs	r3, #5
	ldrh	r0, [r4, #256]
	cmp	r2, #1
	strh	r3, [r4, #292]	@ movhi
	mov	r7, #640
	mov	r3, #0
	it	eq
	strheq	r2, [r4, #292]	@ movhi
	strh	r3, [r4, #294]	@ movhi
	smulbb	r6, r6, r0
	strh	r7, [r4, #296]	@ movhi
	smulbb	r0, r0, r5
	uxth	r6, r6
	uxth	r0, r0
	strh	r6, [r4, #232]	@ movhi
	strh	r0, [r4, #298]	@ movhi
	bl	Ftl_log2
	ldrh	r9, [fp, #12]
	ldrh	r10, [r4, #258]
	strh	r0, [r4, #300]	@ movhi
	strh	r9, [r4, #302]	@ movhi
	smulbb	r3, r6, r9
	mov	r0, r10
	strh	r9, [r4, #304]	@ movhi
	strh	r3, [r4, #306]	@ movhi
	bl	Ftl_log2
	lsl	r3, r10, #9
	mov	r8, r0
	strh	r0, [r4, #308]	@ movhi
	mul	r1, r9, r10
	uxth	r3, r3
	mov	r0, #5120
	asr	r7, r7, r8
	strh	r3, [r4, #310]	@ movhi
	lsrs	r3, r3, #8
	add	r8, r8, #9
	strh	r3, [r4, #312]	@ movhi
	adds	r7, r7, #2
	ldrh	r3, [fp, #20]
	strh	r3, [r4, #314]	@ movhi
	mul	r3, r5, r6
	lsls	r5, r5, #6
	asr	r5, r5, r8
	str	r3, [r4, #248]
	mul	r3, r10, r3
	mul	r3, r9, r3
	asrs	r3, r3, #11
	str	r3, [r4, #316]
	bl	__aeabi_idiv
	uxth	r0, r0
	strh	r5, [r4, #324]	@ movhi
	uxth	r5, r5
	mov	r1, r6
	cmp	r0, #4
	strh	r7, [r4, #322]	@ movhi
	itet	ls
	movls	r3, #4
	strhhi	r0, [r4, #320]	@ movhi
	strhls	r3, [r4, #320]	@ movhi
	mul	r3, r6, r5
	ldrh	r0, [r4, #320]
	adds	r5, r5, #8
	str	r3, [r4, #328]
	bl	__aeabi_uidiv
	uxtah	r0, r5, r0
	cmp	r6, #1
	it	eq
	addeq	r0, r0, #4
	str	r0, [r4, #228]
	ldrh	r0, [r4, #228]
	bl	FtlSysBlkNumInit
	ldr	r3, [r4, #228]
	movs	r0, #0
	str	r0, [r4, #340]
	str	r3, [r4, #332]
	ldr	r3, [r4, #244]
	lsls	r2, r3, #2
	ldrh	r3, [r4, #302]
	muls	r3, r2, r3
	ldrh	r2, [r4, #308]
	adds	r2, r2, #9
	lsrs	r3, r3, r2
	adds	r3, r3, #2
	strh	r3, [r4, #336]	@ movhi
	movs	r3, #32
	strh	r3, [r4, #338]	@ movhi
	ldrh	r3, [r4, #320]
	adds	r3, r3, #3
	strh	r3, [r4, #320]	@ movhi
	ldr	r3, [r4, #328]
	adds	r3, r3, #3
	str	r3, [r4, #328]
	pop	{r3, r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L16:
	cmp	r1, #8
	bne	.L17
	cmp	r0, #1
	itttt	eq
	lsreq	r3, r3, #1
	strheq	r3, [r4, #242]	@ movhi
	moveq	r3, #2
	strheq	r3, [r4, #256]	@ movhi
	b	.L17
.L27:
	.align	2
.L26:
	.word	.LANCHOR0
	.word	.LANCHOR0+260
	.fnend
	.size	FtlConstantsInit, .-FtlConstantsInit
	.align	1
	.global	IsBlkInVendorPart
	.syntax unified
	.thumb
	.thumb_func
	.fpu softvfp
	.type	IsBlkInVendorPart, %function
IsBlkInVendorPart:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	@ link register save eliminated.
	ldr	r2, .L35
	ldrh	r3, [r2, #344]
	cbz	r3, .L34
	ldr	r3, [r2, #348]
	ldrh	r2, [r2, #320]
	add	r2, r3, r2, lsl #1
.L30:
	cmp	r3, r2
	bne	.L31
.L34:
	movs	r0, #0
	bx	lr
.L31:
	ldrh	r1, [r3], #2
	cmp	r0, r1
	bne	.L30
	movs	r0, #1
	bx	lr
.L36:
	.align	2
.L35:
	.word	.LANCHOR0
	.fnend
	.size	IsBlkInVendorPart, .-IsBlkInVendorPart
	.align	1
	.global	FtlCacheWriteBack
	.syntax unified
	.thumb
	.thumb_func
	.fpu softvfp
	.type	FtlCacheWriteBack, %function
FtlCacheWriteBack:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	@ link register save eliminated.
	movs	r0, #0
	bx	lr
	.fnend
	.size	FtlCacheWriteBack, .-FtlCacheWriteBack
	.align	1
	.global	sftl_get_density
	.syntax unified
	.thumb
	.thumb_func
	.fpu softvfp
	.type	sftl_get_density, %function
sftl_get_density:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	@ link register save eliminated.
	ldr	r3, .L39
	ldr	r0, [r3, #340]
	bx	lr
.L40:
	.align	2
.L39:
	.word	.LANCHOR0
	.fnend
	.size	sftl_get_density, .-sftl_get_density
	.global	__aeabi_uidivmod
	.align	1
	.global	FtlBbmMapBadBlock
	.syntax unified
	.thumb
	.thumb_func
	.fpu softvfp
	.type	FtlBbmMapBadBlock, %function
FtlBbmMapBadBlock:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	push	{r0, r1, r2, r4, r5, r6, r7, lr}
	.save {r4, r5, r6, r7, lr}
	.pad #12
	mov	r5, r0
	ldr	r4, .L42
	ldrh	r7, [r4, #298]
	mov	r1, r7
	bl	__aeabi_uidiv
	uxth	r6, r0
	mov	r1, r7
	mov	r0, r5
	bl	__aeabi_uidivmod
	add	r2, r4, r6, lsl #2
	uxth	r3, r1
	ldr	r2, [r2, #380]
	lsrs	r1, r3, #5
	and	r7, r3, #31
	movs	r0, #1
	lsls	r0, r0, r7
	ldr	r7, [r2, r1, lsl #2]
	orrs	r0, r0, r7
	str	r0, [r2, r1, lsl #2]
	mov	r2, r6
	str	r0, [sp]
	mov	r1, r5
	ldr	r0, .L42+4
	bl	sftl_printk
	ldrh	r3, [r4, #358]
	movs	r0, #0
	adds	r3, r3, #1
	strh	r3, [r4, #358]	@ movhi
	add	sp, sp, #12
	@ sp needed
	pop	{r4, r5, r6, r7, pc}
.L43:
	.align	2
.L42:
	.word	.LANCHOR0
	.word	.LC7
	.fnend
	.size	FtlBbmMapBadBlock, .-FtlBbmMapBadBlock
	.align	1
	.global	FtlBbmIsBadBlock
	.syntax unified
	.thumb
	.thumb_func
	.fpu softvfp
	.type	FtlBbmIsBadBlock, %function
FtlBbmIsBadBlock:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	push	{r3, r4, r5, r6, r7, lr}
	.save {r3, r4, r5, r6, r7, lr}
	mov	r7, r0
	ldr	r5, .L45
	ldrh	r6, [r5, #298]
	mov	r1, r6
	bl	__aeabi_uidivmod
	mov	r0, r7
	uxth	r4, r1
	mov	r1, r6
	bl	__aeabi_uidiv
	uxth	r0, r0
	lsrs	r2, r4, #5
	add	r5, r5, r0, lsl #2
	and	r4, r4, #31
	ldr	r3, [r5, #380]
	ldr	r0, [r3, r2, lsl #2]
	lsrs	r0, r0, r4
	and	r0, r0, #1
	pop	{r3, r4, r5, r6, r7, pc}
.L46:
	.align	2
.L45:
	.word	.LANCHOR0
	.fnend
	.size	FtlBbmIsBadBlock, .-FtlBbmIsBadBlock
	.align	1
	.global	FtlBbtInfoPrint
	.syntax unified
	.thumb
	.thumb_func
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
	.align	1
	.global	FtlBbtCalcTotleCnt
	.syntax unified
	.thumb
	.thumb_func
	.fpu softvfp
	.type	FtlBbtCalcTotleCnt, %function
FtlBbtCalcTotleCnt:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	ldr	r3, .L55
	push	{r4, r5, r6, lr}
	.save {r4, r5, r6, lr}
	movs	r5, #0
	mov	r4, r5
	ldrh	r2, [r3, #298]
	ldrh	r6, [r3, #254]
	muls	r6, r2, r6
.L49:
	uxth	r0, r5
	cmp	r0, r6
	blt	.L51
	mov	r0, r4
	pop	{r4, r5, r6, pc}
.L51:
	bl	FtlBbmIsBadBlock
	cbz	r0, .L50
	adds	r4, r4, #1
	uxth	r4, r4
.L50:
	adds	r5, r5, #1
	b	.L49
.L56:
	.align	2
.L55:
	.word	.LANCHOR0
	.fnend
	.size	FtlBbtCalcTotleCnt, .-FtlBbtCalcTotleCnt
	.align	1
	.global	V2P_block
	.syntax unified
	.thumb
	.thumb_func
	.fpu softvfp
	.type	V2P_block, %function
V2P_block:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	push	{r3, r4, r5, r6, r7, lr}
	.save {r3, r4, r5, r6, r7, lr}
	mov	r5, r1
	ldr	r4, .L58
	mov	r7, r0
	ldrh	r6, [r4, #256]
	mov	r1, r6
	bl	__aeabi_uidiv
	ldrh	r4, [r4, #298]
	smulbb	r5, r6, r5
	mov	r1, r6
	smulbb	r4, r4, r0
	mov	r0, r7
	bl	__aeabi_uidivmod
	adds	r0, r5, r1
	add	r0, r0, r4
	uxth	r0, r0
	pop	{r3, r4, r5, r6, r7, pc}
.L59:
	.align	2
.L58:
	.word	.LANCHOR0
	.fnend
	.size	V2P_block, .-V2P_block
	.align	1
	.global	P2V_plane
	.syntax unified
	.thumb
	.thumb_func
	.fpu softvfp
	.type	P2V_plane, %function
P2V_plane:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	ldr	r3, .L61
	push	{r4, r5, r6, lr}
	.save {r4, r5, r6, lr}
	mov	r6, r0
	ldrh	r5, [r3, #256]
	ldrh	r1, [r3, #298]
	bl	__aeabi_uidiv
	mov	r1, r5
	smulbb	r4, r0, r5
	mov	r0, r6
	bl	__aeabi_uidivmod
	add	r1, r1, r4
	uxth	r0, r1
	pop	{r4, r5, r6, pc}
.L62:
	.align	2
.L61:
	.word	.LANCHOR0
	.fnend
	.size	P2V_plane, .-P2V_plane
	.align	1
	.global	P2V_block_in_plane
	.syntax unified
	.thumb
	.thumb_func
	.fpu softvfp
	.type	P2V_block_in_plane, %function
P2V_block_in_plane:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	push	{r4, lr}
	.save {r4, lr}
	ldr	r4, .L64
	ldrh	r1, [r4, #298]
	bl	__aeabi_uidivmod
	uxth	r0, r1
	ldrh	r1, [r4, #256]
	bl	__aeabi_uidiv
	uxth	r0, r0
	pop	{r4, pc}
.L65:
	.align	2
.L64:
	.word	.LANCHOR0
	.fnend
	.size	P2V_block_in_plane, .-P2V_block_in_plane
	.align	1
	.global	ftl_cmp_data_ver
	.syntax unified
	.thumb
	.thumb_func
	.fpu softvfp
	.type	ftl_cmp_data_ver, %function
ftl_cmp_data_ver:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	@ link register save eliminated.
	cmp	r0, r1
	bls	.L67
	subs	r0, r0, r1
	cmp	r0, #-2147483648
	ite	hi
	movhi	r0, #0
	movls	r0, #1
	bx	lr
.L67:
	subs	r0, r1, r0
	cmp	r0, #-2147483648
	ite	ls
	movls	r0, #0
	movhi	r0, #1
	bx	lr
	.fnend
	.size	ftl_cmp_data_ver, .-ftl_cmp_data_ver
	.align	1
	.global	FtlFreeSysBlkQueueEmpty
	.syntax unified
	.thumb
	.thumb_func
	.fpu softvfp
	.type	FtlFreeSysBlkQueueEmpty, %function
FtlFreeSysBlkQueueEmpty:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	@ link register save eliminated.
	ldr	r3, .L70
	ldrh	r0, [r3, #418]
	clz	r0, r0
	lsrs	r0, r0, #5
	bx	lr
.L71:
	.align	2
.L70:
	.word	.LANCHOR0
	.fnend
	.size	FtlFreeSysBlkQueueEmpty, .-FtlFreeSysBlkQueueEmpty
	.align	1
	.global	FtlFreeSysBlkQueueFull
	.syntax unified
	.thumb
	.thumb_func
	.fpu softvfp
	.type	FtlFreeSysBlkQueueFull, %function
FtlFreeSysBlkQueueFull:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	@ link register save eliminated.
	ldr	r3, .L73
	ldrh	r0, [r3, #418]
	sub	r3, r0, #1024
	rsbs	r0, r3, #0
	adcs	r0, r0, r3
	bx	lr
.L74:
	.align	2
.L73:
	.word	.LANCHOR0
	.fnend
	.size	FtlFreeSysBlkQueueFull, .-FtlFreeSysBlkQueueFull
	.align	1
	.global	FtlFreeSysBLkSort
	.syntax unified
	.thumb
	.thumb_func
	.fpu softvfp
	.type	FtlFreeSysBLkSort, %function
FtlFreeSysBLkSort:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	ldr	r3, .L84
	push	{r4, r5, r6, lr}
	.save {r4, r5, r6, lr}
	ldrh	r2, [r3, #418]
	cbz	r2, .L75
	ldrh	r5, [r3, #2496]
	movs	r0, #0
	ldrh	r1, [r3, #414]
	mov	r6, r0
	ldrh	r2, [r3, #416]
	and	r5, r5, #31
.L77:
	uxth	r4, r0
	adds	r0, r0, #1
	cmp	r5, r4
	bgt	.L78
	cbz	r6, .L75
	strh	r1, [r3, #414]	@ movhi
	strh	r2, [r3, #416]	@ movhi
.L75:
	pop	{r4, r5, r6, pc}
.L78:
	add	r4, r3, r1, lsl #1
	adds	r1, r1, #1
	ubfx	r1, r1, #0, #10
	ldrh	r6, [r4, #420]
	add	r4, r3, r2, lsl #1
	strh	r6, [r4, #420]	@ movhi
	movs	r6, #1
	add	r2, r2, r6
	ubfx	r2, r2, #0, #10
	b	.L77
.L85:
	.align	2
.L84:
	.word	.LANCHOR0
	.fnend
	.size	FtlFreeSysBLkSort, .-FtlFreeSysBLkSort
	.align	1
	.global	IsInFreeQueue
	.syntax unified
	.thumb
	.thumb_func
	.fpu softvfp
	.type	IsInFreeQueue, %function
IsInFreeQueue:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	ldr	r3, .L92
	push	{r4, r5, lr}
	.save {r4, r5, lr}
	ldrh	r4, [r3, #418]
	cmp	r4, #1024
	beq	.L90
	ldrh	r5, [r3, #414]
	movs	r1, #0
.L88:
	cmp	r1, r4
	bcc	.L89
.L90:
	movs	r0, #0
	pop	{r4, r5, pc}
.L89:
	adds	r2, r1, r5
	ubfx	r2, r2, #0, #10
	add	r2, r3, r2, lsl #1
	ldrh	r2, [r2, #420]
	cmp	r2, r0
	beq	.L91
	adds	r1, r1, #1
	b	.L88
.L91:
	movs	r0, #1
	pop	{r4, r5, pc}
.L93:
	.align	2
.L92:
	.word	.LANCHOR0
	.fnend
	.size	IsInFreeQueue, .-IsInFreeQueue
	.align	1
	.global	insert_data_list
	.syntax unified
	.thumb
	.thumb_func
	.fpu softvfp
	.type	insert_data_list, %function
insert_data_list:
	.fnstart
	@ args = 0, pretend = 0, frame = 8
	@ frame_needed = 0, uses_anonymous_args = 0
	push	{r0, r1, r2, r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.save {r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.pad #12
	ldr	r2, .L109
	ldrh	r3, [r2, #240]
	mov	r1, r2
	cmp	r3, r0
	bls	.L96
	movs	r5, #6
	ldr	lr, [r2, #2516]
	muls	r5, r0, r5
	movw	r3, #65535
	add	r4, lr, r5
	strh	r3, [r4, #2]	@ movhi
	strh	r3, [lr, r5]	@ movhi
	ldr	r3, [r2, #2520]
	cbnz	r3, .L97
	str	r4, [r2, #2520]
.L96:
	movs	r0, #0
	add	sp, sp, #12
	@ sp needed
	pop	{r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L97:
	ldrh	r7, [r4, #4]
	ldr	r9, [r2, #72]
	ldrh	r2, [r9, r0, lsl #1]
	cbz	r7, .L104
	muls	r2, r7, r2
.L108:
	ldr	r8, [r1, #2516]
	movw	r10, #65535
	str	r2, [sp]
	sub	r2, r3, r8
	asrs	r6, r2, #1
	ldr	r2, .L109+4
	muls	r2, r6, r2
	ldrh	r6, [r1, #240]
	str	r6, [sp, #4]
	movs	r6, #0
	uxth	r2, r2
.L102:
	adds	r6, r6, #1
	ldr	r7, [sp, #4]
	uxth	r6, r6
	cmp	r6, r7
	bhi	.L96
	cmp	r0, r2
	beq	.L96
	ldrh	ip, [r3, #4]
	cmp	ip, #0
	beq	.L100
	ldrh	fp, [r9, r2, lsl #1]
	ldr	r7, [sp]
	mul	ip, ip, fp
	cmp	ip, r7
	bcs	.L100
	ldrh	ip, [r3]
	cmp	ip, r10
	bne	.L101
	strh	r2, [r4, #2]	@ movhi
	strh	r0, [r3]	@ movhi
	str	r4, [r1, #2524]
	b	.L96
.L104:
	mov	r2, #-1
	b	.L108
.L101:
	movs	r3, #6
	mov	r2, ip
	mla	r3, r3, ip, r8
	b	.L102
.L100:
	strh	r2, [lr, r5]	@ movhi
	ldrh	r2, [r3, #2]
	strh	r2, [r4, #2]	@ movhi
	ldr	r2, [r1, #2520]
	cmp	r3, r2
	ittte	ne
	ldrhne	r4, [r3, #2]
	movne	r2, #6
	ldrne	r1, [r1, #2516]
	strheq	r0, [r3, #2]	@ movhi
	iteee	eq
	streq	r4, [r1, #2520]
	mulne	r2, r2, r4
	strhne	r0, [r1, r2]	@ movhi
	strhne	r0, [r3, #2]	@ movhi
	b	.L96
.L110:
	.align	2
.L109:
	.word	.LANCHOR0
	.word	-1431655765
	.fnend
	.size	insert_data_list, .-insert_data_list
	.align	1
	.global	INSERT_DATA_LIST
	.syntax unified
	.thumb
	.thumb_func
	.fpu softvfp
	.type	INSERT_DATA_LIST, %function
INSERT_DATA_LIST:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	push	{r3, lr}
	.save {r3, lr}
	bl	insert_data_list
	ldr	r2, .L113
	ldrh	r3, [r2, #2528]
	adds	r3, r3, #1
	uxth	r3, r3
	strh	r3, [r2, #2528]	@ movhi
	ldrh	r2, [r2, #240]
	cmp	r2, r3
	bcs	.L111
	movs	r2, #214
	ldr	r1, .L113+4
	ldr	r0, .L113+8
	pop	{r3, lr}
	b	sftl_printk
.L111:
	pop	{r3, pc}
.L114:
	.align	2
.L113:
	.word	.LANCHOR0
	.word	.LANCHOR1
	.word	.LC8
	.fnend
	.size	INSERT_DATA_LIST, .-INSERT_DATA_LIST
	.align	1
	.global	insert_free_list
	.syntax unified
	.thumb
	.thumb_func
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
	beq	.L116
	ldr	r2, .L122
	movs	r1, #6
	mul	r7, r1, r0
	ldr	ip, [r2, #2516]
	mov	r5, r2
	add	r6, ip, r7
	strh	r4, [r6, #2]	@ movhi
	strh	r4, [ip, r7]	@ movhi
	ldr	r3, [r2, #2532]
	cbnz	r3, .L117
	str	r6, [r2, #2532]
.L116:
	movs	r0, #0
	pop	{r4, r5, r6, r7, r8, r9, r10, pc}
.L117:
	ldr	lr, [r2, #2516]
	ldr	r8, [r2, #2536]
	sub	r2, r3, lr
	asr	r10, r2, #1
	ldr	r2, .L122+4
	ldrh	r9, [r8, r0, lsl #1]
	mul	r2, r2, r10
	mov	r10, r4
	uxth	r2, r2
.L120:
	ldrh	r4, [r8, r2, lsl #1]
	cmp	r4, r9
	bcs	.L118
	ldrh	r4, [r3]
	cmp	r4, r10
	bne	.L119
	strh	r2, [r6, #2]	@ movhi
	strh	r0, [r3]	@ movhi
	b	.L116
.L119:
	mla	r3, r1, r4, lr
	mov	r2, r4
	b	.L120
.L118:
	ldrh	r1, [r3, #2]
	strh	r1, [r6, #2]	@ movhi
	strh	r2, [ip, r7]	@ movhi
	ldr	r2, [r5, #2532]
	cmp	r3, r2
	ittte	ne
	ldrhne	r4, [r3, #2]
	movne	r2, #6
	ldrne	r1, [r5, #2516]
	strheq	r0, [r3, #2]	@ movhi
	iteee	eq
	streq	r6, [r5, #2532]
	mulne	r2, r2, r4
	strhne	r0, [r1, r2]	@ movhi
	strhne	r0, [r3, #2]	@ movhi
	b	.L116
.L123:
	.align	2
.L122:
	.word	.LANCHOR0
	.word	-1431655765
	.fnend
	.size	insert_free_list, .-insert_free_list
	.align	1
	.global	INSERT_FREE_LIST
	.syntax unified
	.thumb
	.thumb_func
	.fpu softvfp
	.type	INSERT_FREE_LIST, %function
INSERT_FREE_LIST:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	push	{r3, lr}
	.save {r3, lr}
	bl	insert_free_list
	ldr	r2, .L126
	ldrh	r3, [r2, #224]
	adds	r3, r3, #1
	uxth	r3, r3
	strh	r3, [r2, #224]	@ movhi
	ldrh	r2, [r2, #240]
	cmp	r2, r3
	bcs	.L124
	movs	r2, #207
	ldr	r1, .L126+4
	ldr	r0, .L126+8
	pop	{r3, lr}
	b	sftl_printk
.L124:
	pop	{r3, pc}
.L127:
	.align	2
.L126:
	.word	.LANCHOR0
	.word	.LANCHOR1+17
	.word	.LC8
	.fnend
	.size	INSERT_FREE_LIST, .-INSERT_FREE_LIST
	.align	1
	.global	List_remove_node
	.syntax unified
	.thumb
	.thumb_func
	.fpu softvfp
	.type	List_remove_node, %function
List_remove_node:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	push	{r4, r5, r6, r7, r8, lr}
	.save {r4, r5, r6, r7, r8, lr}
	movs	r6, #6
	ldr	r4, .L134
	muls	r6, r1, r6
	movw	r3, #65535
	mov	r8, r0
	ldr	r7, [r4, #2516]
	adds	r5, r7, r6
	ldrh	r2, [r5, #2]
	cmp	r2, r3
	bne	.L129
	ldr	r3, [r0]
	cmp	r5, r3
	beq	.L129
	mov	r2, #372
	ldr	r1, .L134+4
	ldr	r0, .L134+8
	bl	sftl_printk
.L129:
	ldr	r3, [r8]
	movw	r1, #65535
	cmp	r5, r3
	ldrh	r3, [r7, r6]
	bne	.L130
	cmp	r3, r1
	ittee	ne
	ldrne	r0, [r4, #2516]
	movne	r2, #6
	moveq	r3, #0
	streq	r3, [r8]
	ittt	ne
	mlane	r3, r2, r3, r0
	strne	r3, [r8]
	strhne	r1, [r3, #2]	@ movhi
.L132:
	movw	r3, #65535
	movs	r0, #0
	strh	r3, [r7, r6]	@ movhi
	strh	r3, [r5, #2]	@ movhi
	pop	{r4, r5, r6, r7, r8, pc}
.L130:
	cmp	r3, r1
	ldrh	r1, [r5, #2]
	bne	.L133
	cmp	r1, r3
	beq	.L132
	movs	r2, #6
	ldr	r0, [r4, #2516]
	muls	r1, r2, r1
	strh	r3, [r0, r1]	@ movhi
	b	.L132
.L133:
	ldr	r0, [r4, #2516]
	movs	r2, #6
	mla	r3, r2, r3, r0
	strh	r1, [r3, #2]	@ movhi
	ldrh	r0, [r5, #2]
	ldrh	r1, [r7, r6]
	ldr	r3, [r4, #2516]
	muls	r2, r0, r2
	strh	r1, [r3, r2]	@ movhi
	b	.L132
.L135:
	.align	2
.L134:
	.word	.LANCHOR0
	.word	.LANCHOR1+34
	.word	.LC8
	.fnend
	.size	List_remove_node, .-List_remove_node
	.align	1
	.global	List_pop_index_node
	.syntax unified
	.thumb
	.thumb_func
	.fpu softvfp
	.type	List_pop_index_node, %function
List_pop_index_node:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	push	{r4, r5, r6, lr}
	.save {r4, r5, r6, lr}
	ldr	r3, [r0]
	cbz	r3, .L142
	ldr	r2, .L143
	movw	r5, #65535
	movs	r6, #6
	ldr	r2, [r2, #2516]
.L138:
	cbnz	r1, .L139
.L141:
	ldr	r4, .L143+4
	subs	r3, r3, r2
	asrs	r3, r3, #1
	muls	r4, r3, r4
	uxth	r1, r4
	bl	List_remove_node
	uxth	r0, r4
	pop	{r4, r5, r6, pc}
.L139:
	ldrh	r4, [r3]
	cmp	r4, r5
	beq	.L141
	subs	r1, r1, #1
	mla	r3, r6, r4, r2
	uxth	r1, r1
	b	.L138
.L142:
	movw	r0, #65535
	pop	{r4, r5, r6, pc}
.L144:
	.align	2
.L143:
	.word	.LANCHOR0
	.word	-1431655765
	.fnend
	.size	List_pop_index_node, .-List_pop_index_node
	.align	1
	.global	List_pop_head_node
	.syntax unified
	.thumb
	.thumb_func
	.fpu softvfp
	.type	List_pop_head_node, %function
List_pop_head_node:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	@ link register save eliminated.
	movs	r1, #0
	b	List_pop_index_node
	.fnend
	.size	List_pop_head_node, .-List_pop_head_node
	.align	1
	.global	List_get_gc_head_node
	.syntax unified
	.thumb
	.thumb_func
	.fpu softvfp
	.type	List_get_gc_head_node, %function
List_get_gc_head_node:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	ldr	r2, .L152
	push	{r4, lr}
	.save {r4, lr}
	ldr	r3, [r2, #2520]
	cbz	r3, .L151
	ldr	r1, [r2, #2516]
	movs	r4, #6
	movw	r2, #65535
.L148:
	cbz	r0, .L149
	ldrh	r3, [r3]
	cmp	r3, r2
	bne	.L150
.L151:
	movw	r0, #65535
	pop	{r4, pc}
.L150:
	subs	r0, r0, #1
	mla	r3, r4, r3, r1
	uxth	r0, r0
	b	.L148
.L149:
	ldr	r0, .L152+4
	subs	r3, r3, r1
	asrs	r3, r3, #1
	muls	r3, r0, r3
	uxth	r0, r3
	pop	{r4, pc}
.L153:
	.align	2
.L152:
	.word	.LANCHOR0
	.word	-1431655765
	.fnend
	.size	List_get_gc_head_node, .-List_get_gc_head_node
	.align	1
	.global	List_update_data_list
	.syntax unified
	.thumb
	.thumb_func
	.fpu softvfp
	.type	List_update_data_list, %function
List_update_data_list:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	push	{r3, r4, r5, r6, r7, r8, r9, lr}
	.save {r3, r4, r5, r6, r7, r8, r9, lr}
	mov	r5, r0
	ldr	r4, .L165
	ldrh	r3, [r4, #24]
	cmp	r3, r0
	beq	.L156
	ldrh	r3, [r4, #76]
	cmp	r3, r0
	beq	.L156
	ldrh	r3, [r4, #124]
	cmp	r3, r0
	beq	.L156
	movs	r7, #6
	ldr	r9, [r4, #2516]
	muls	r7, r0, r7
	ldr	r3, [r4, #2520]
	add	r8, r9, r7
	cmp	r8, r3
	beq	.L156
	ldrh	r6, [r8, #4]
	ldr	r3, [r4, #72]
	ldrh	r3, [r3, r0, lsl #1]
	cmp	r6, #0
	beq	.L163
	muls	r6, r3, r6
.L158:
	ldrh	r3, [r8, #2]
	movw	r2, #65535
	cmp	r3, r2
	bne	.L159
	ldrh	r2, [r9, r7]
	cmp	r2, r3
	bne	.L159
	movw	r2, #463
	ldr	r1, .L165+4
	ldr	r0, .L165+8
	bl	sftl_printk
.L159:
	ldrh	r3, [r8, #2]
	movw	r2, #65535
	cmp	r3, r2
	bne	.L160
	ldrh	r2, [r9, r7]
	cmp	r2, r3
	beq	.L156
.L160:
	movs	r2, #6
	muls	r2, r3, r2
	ldr	r3, .L165+12
	asrs	r1, r2, #1
	muls	r3, r1, r3
	ldr	r1, [r4, #72]
	ldrh	r0, [r1, r3, lsl #1]
	ldr	r1, [r4, #2516]
	add	r2, r2, r1
	ldrh	r3, [r2, #4]
	cbz	r3, .L164
	muls	r3, r0, r3
.L161:
	cmp	r6, r3
	bcs	.L156
	mov	r1, r5
	ldr	r0, .L165+16
	bl	List_remove_node
	ldrh	r3, [r4, #2528]
	cbnz	r3, .L162
	mov	r2, #474
	ldr	r1, .L165+4
	ldr	r0, .L165+8
	bl	sftl_printk
.L162:
	ldrh	r3, [r4, #2528]
	mov	r0, r5
	subs	r3, r3, #1
	strh	r3, [r4, #2528]	@ movhi
	bl	INSERT_DATA_LIST
.L156:
	movs	r0, #0
	pop	{r3, r4, r5, r6, r7, r8, r9, pc}
.L163:
	mov	r6, #-1
	b	.L158
.L164:
	mov	r3, #-1
	b	.L161
.L166:
	.align	2
.L165:
	.word	.LANCHOR0
	.word	.LANCHOR1+51
	.word	.LC8
	.word	-1431655765
	.word	.LANCHOR0+2520
	.fnend
	.size	List_update_data_list, .-List_update_data_list
	.align	1
	.global	select_l2p_ram_region
	.syntax unified
	.thumb
	.thumb_func
	.fpu softvfp
	.type	select_l2p_ram_region, %function
select_l2p_ram_region:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	push	{r3, r4, r5, r6, r7, lr}
	.save {r3, r4, r5, r6, r7, lr}
	movs	r1, #0
	ldr	r5, .L177
	movs	r0, #12
	movw	r6, #65535
	ldrh	r2, [r5, #338]
	ldr	r3, [r5, #2540]
.L168:
	uxth	r4, r1
	cmp	r4, r2
	bcc	.L170
	mov	r4, r2
	movs	r1, #0
	mov	r7, #-2147483648
	mov	ip, #12
.L171:
	uxth	r6, r1
	cmp	r6, r2
	bcc	.L173
	cmp	r4, r2
	bcc	.L169
	ldrh	r7, [r5, #2544]
	mov	r4, r2
	movs	r1, #0
	mov	r0, #-1
.L174:
	uxth	r5, r1
	cmp	r5, r2
	bcc	.L176
	cmp	r4, r2
	bcc	.L169
	movw	r2, #789
	ldr	r1, .L177+4
	ldr	r0, .L177+8
	bl	sftl_printk
	b	.L169
.L170:
	adds	r1, r1, #1
	mla	r7, r0, r1, r3
	ldrh	r7, [r7, #-12]
	cmp	r7, r6
	bne	.L168
.L169:
	mov	r0, r4
	pop	{r3, r4, r5, r6, r7, pc}
.L173:
	mla	r0, ip, r1, r3
	ldr	r0, [r0, #4]
	cmp	r0, #0
	blt	.L172
	cmp	r7, r0
	itt	hi
	movhi	r7, r0
	movhi	r4, r6
.L172:
	adds	r1, r1, #1
	b	.L171
.L176:
	ldr	r6, [r3, #4]
	cmp	r0, r6
	bls	.L175
	ldrh	ip, [r3]
	cmp	ip, r7
	itt	ne
	movne	r0, r6
	movne	r4, r5
.L175:
	adds	r1, r1, #1
	adds	r3, r3, #12
	b	.L174
.L178:
	.align	2
.L177:
	.word	.LANCHOR0
	.word	.LANCHOR1+73
	.word	.LC8
	.fnend
	.size	select_l2p_ram_region, .-select_l2p_ram_region
	.align	1
	.global	FtlUpdateVaildLpn
	.syntax unified
	.thumb
	.thumb_func
	.fpu softvfp
	.type	FtlUpdateVaildLpn, %function
FtlUpdateVaildLpn:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	ldr	r1, .L185
	push	{r4, r5, lr}
	.save {r4, r5, lr}
	mov	r3, r1
	ldrh	r2, [r1, #2546]
	cmp	r2, #4
	bhi	.L180
	cbnz	r0, .L180
	adds	r2, r2, #1
	strh	r2, [r1, #2546]	@ movhi
	pop	{r4, r5, pc}
.L180:
	movs	r2, #0
	ldrh	r1, [r3, #240]
	strh	r2, [r3, #2546]	@ movhi
	movw	r5, #65535
	str	r2, [r3, #2548]
	ldr	r2, [r3, #72]
	add	r1, r2, r1, lsl #1
.L181:
	cmp	r2, r1
	bne	.L183
	pop	{r4, r5, pc}
.L183:
	ldrh	r4, [r2], #2
	cmp	r4, r5
	ittt	ne
	ldrne	r0, [r3, #2548]
	addne	r0, r0, r4
	strne	r0, [r3, #2548]
	b	.L181
.L186:
	.align	2
.L185:
	.word	.LANCHOR0
	.fnend
	.size	FtlUpdateVaildLpn, .-FtlUpdateVaildLpn
	.align	1
	.global	ftl_get_blk_mode
	.syntax unified
	.thumb
	.thumb_func
	.fpu softvfp
	.type	ftl_get_blk_mode, %function
ftl_get_blk_mode:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	@ link register save eliminated.
	ldr	r3, .L188
	lsrs	r2, r0, #5
	and	r0, r0, #31
	ldr	r3, [r3, #2552]
	ldr	r3, [r3, r2, lsl #2]
	lsr	r0, r3, r0
	and	r0, r0, #1
	bx	lr
.L189:
	.align	2
.L188:
	.word	.LANCHOR0
	.fnend
	.size	ftl_get_blk_mode, .-ftl_get_blk_mode
	.align	1
	.global	ftl_sb_update_avl_pages
	.syntax unified
	.thumb
	.thumb_func
	.fpu softvfp
	.type	ftl_sb_update_avl_pages, %function
ftl_sb_update_avl_pages:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	movs	r3, #0
	push	{r4, r5, r6, r7, lr}
	.save {r4, r5, r6, r7, lr}
	strh	r3, [r0, #4]	@ movhi
	add	r4, r0, r2, lsl #1
	movw	r7, #65535
	ldr	r3, .L197
	adds	r4, r4, #14
	ldrh	r5, [r3, #232]
.L191:
	cmp	r2, r5
	bcc	.L193
	ldrh	r3, [r3, #302]
	add	r4, r0, #16
	movw	r6, #65535
	subs	r3, r3, #1
	subs	r1, r3, r1
	movs	r3, #0
	uxth	r1, r1
.L194:
	uxth	r2, r3
	cmp	r5, r2
	bhi	.L196
	pop	{r4, r5, r6, r7, pc}
.L193:
	ldrh	r6, [r4, #2]!
	adds	r2, r2, #1
	uxth	r2, r2
	cmp	r6, r7
	ittt	ne
	ldrhne	r6, [r0, #4]
	addne	r6, r6, #1
	strhne	r6, [r0, #4]	@ movhi
	b	.L191
.L196:
	ldrh	r2, [r4], #2
	adds	r3, r3, #1
	cmp	r2, r6
	ittt	ne
	ldrhne	r2, [r0, #4]
	addne	r2, r2, r1
	strhne	r2, [r0, #4]	@ movhi
	b	.L194
.L198:
	.align	2
.L197:
	.word	.LANCHOR0
	.fnend
	.size	ftl_sb_update_avl_pages, .-ftl_sb_update_avl_pages
	.align	1
	.global	FtlSlcSuperblockCheck
	.syntax unified
	.thumb
	.thumb_func
	.fpu softvfp
	.type	FtlSlcSuperblockCheck, %function
FtlSlcSuperblockCheck:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	ldrh	r3, [r0, #4]
	push	{r4, r5, lr}
	.save {r4, r5, lr}
	cbz	r3, .L199
	ldrh	r2, [r0]
	movw	r3, #65535
	cmp	r2, r3
	beq	.L199
	ldrb	r2, [r0, #6]	@ zero_extendqisi2
	movs	r5, #0
	adds	r2, r2, #8
	ldrh	r1, [r0, r2, lsl #1]
	ldr	r2, .L205
	ldrh	r4, [r2, #232]
	mov	r2, r3
.L202:
	cmp	r1, r2
	beq	.L204
.L199:
	pop	{r4, r5, pc}
.L204:
	ldrb	r3, [r0, #6]	@ zero_extendqisi2
	adds	r3, r3, #1
	uxtb	r3, r3
	cmp	r3, r4
	strb	r3, [r0, #6]
	itttt	eq
	ldrheq	r3, [r0, #2]
	strbeq	r5, [r0, #6]
	addeq	r3, r3, #1
	strheq	r3, [r0, #2]	@ movhi
	ldrb	r3, [r0, #6]	@ zero_extendqisi2
	adds	r3, r3, #8
	ldrh	r1, [r0, r3, lsl #1]
	b	.L202
.L206:
	.align	2
.L205:
	.word	.LANCHOR0
	.fnend
	.size	FtlSlcSuperblockCheck, .-FtlSlcSuperblockCheck
	.align	1
	.global	make_superblock
	.syntax unified
	.thumb
	.thumb_func
	.fpu softvfp
	.type	make_superblock, %function
make_superblock:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	push	{r3, r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.save {r3, r4, r5, r6, r7, r8, r9, r10, fp, lr}
	mov	r4, r0
	ldr	r6, .L213
	ldrh	r2, [r0]
	ldrh	r3, [r6, #240]
	cmp	r2, r3
	bcc	.L208
	movw	r2, #2150
	ldr	r1, .L213+4
	ldr	r0, .L213+8
	bl	sftl_printk
.L208:
	ldrh	r9, [r6, #232]
	add	r7, r4, #16
	ldr	r10, .L213+12
	movw	r8, #65535
	movs	r5, #0
	strh	r5, [r4, #4]	@ movhi
	strb	r5, [r4, #7]
.L209:
	uxth	r3, r5
	ldrh	r1, [r4]
	cmp	r9, r3
	bhi	.L211
	ldrh	r2, [r6, #302]
	movs	r0, #0
	ldrb	r3, [r4, #7]	@ zero_extendqisi2
	smulbb	r3, r3, r2
	strh	r3, [r4, #4]	@ movhi
	movs	r3, #0
	strb	r3, [r4, #9]
	ldr	r3, [r6, #2536]
	ldrh	r2, [r3, r1, lsl #1]
	movw	r3, #10000
	cmp	r2, r3
	itt	hi
	movhi	r3, #1
	strbhi	r3, [r4, #9]
	pop	{r3, r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L211:
	ldrb	r0, [r10, r5]	@ zero_extendqisi2
	bl	V2P_block
	strh	r8, [r7]	@ movhi
	mov	fp, r0
	bl	FtlBbmIsBadBlock
	cbnz	r0, .L210
	strh	fp, [r7]	@ movhi
	ldrb	r3, [r4, #7]	@ zero_extendqisi2
	adds	r3, r3, #1
	strb	r3, [r4, #7]
.L210:
	adds	r5, r5, #1
	adds	r7, r7, #2
	b	.L209
.L214:
	.align	2
.L213:
	.word	.LANCHOR0
	.word	.LANCHOR1+95
	.word	.LC8
	.word	.LANCHOR0+260
	.fnend
	.size	make_superblock, .-make_superblock
	.align	1
	.global	update_multiplier_value
	.syntax unified
	.thumb
	.thumb_func
	.fpu softvfp
	.type	update_multiplier_value, %function
update_multiplier_value:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	push	{r4, r5, r6, r7, r8, r9, r10, lr}
	.save {r4, r5, r6, r7, r8, r9, r10, lr}
	movs	r5, #0
	ldr	r6, .L221
	mov	r7, r0
	mov	r4, r5
	ldrh	r8, [r6, #232]
	add	r10, r6, #260
	ldrh	r9, [r6, #302]
.L216:
	uxth	r3, r5
	cmp	r8, r3
	bhi	.L218
	cbz	r4, .L220
	mov	r1, r4
	mov	r0, #32768
	bl	__aeabi_idiv
.L219:
	ldr	r1, [r6, #2516]
	movs	r3, #6
	mla	r1, r3, r7, r1
	strh	r0, [r1, #4]	@ movhi
	movs	r0, #0
	pop	{r4, r5, r6, r7, r8, r9, r10, pc}
.L218:
	mov	r1, r7
	ldrb	r0, [r10, r5]	@ zero_extendqisi2
	bl	V2P_block
	bl	FtlBbmIsBadBlock
	cbnz	r0, .L217
	add	r4, r4, r9
	uxth	r4, r4
.L217:
	adds	r5, r5, #1
	b	.L216
.L220:
	mov	r0, r4
	b	.L219
.L222:
	.align	2
.L221:
	.word	.LANCHOR0
	.fnend
	.size	update_multiplier_value, .-update_multiplier_value
	.align	1
	.global	GetFreeBlockMinEraseCount
	.syntax unified
	.thumb
	.thumb_func
	.fpu softvfp
	.type	GetFreeBlockMinEraseCount, %function
GetFreeBlockMinEraseCount:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	@ link register save eliminated.
	ldr	r2, .L226
	ldr	r0, [r2, #2532]
	cbz	r0, .L224
	ldr	r3, [r2, #2516]
	subs	r0, r0, r3
	ldr	r3, .L226+4
	asrs	r0, r0, #1
	muls	r0, r3, r0
	ldr	r3, [r2, #2536]
	uxth	r0, r0
	ldrh	r0, [r3, r0, lsl #1]
.L224:
	bx	lr
.L227:
	.align	2
.L226:
	.word	.LANCHOR0
	.word	-1431655765
	.fnend
	.size	GetFreeBlockMinEraseCount, .-GetFreeBlockMinEraseCount
	.align	1
	.global	GetFreeBlockMaxEraseCount
	.syntax unified
	.thumb
	.thumb_func
	.fpu softvfp
	.type	GetFreeBlockMaxEraseCount, %function
GetFreeBlockMaxEraseCount:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	ldr	r1, .L236
	push	{r4, r5, r6, r7, lr}
	.save {r4, r5, r6, r7, lr}
	ldr	r3, [r1, #2532]
	cbz	r3, .L234
	ldrh	r2, [r1, #224]
	movs	r6, #6
	ldr	r4, [r1, #2516]
	movw	r7, #65535
	rsb	r2, r2, r2, lsl #3
	subs	r3, r3, r4
	asrs	r3, r3, #1
	asrs	r2, r2, #3
	cmp	r0, r2
	it	gt
	uxthgt	r0, r2
	ldr	r2, .L236+4
	muls	r3, r2, r3
	movs	r2, #0
	uxth	r3, r3
.L231:
	uxth	r5, r2
	cmp	r0, r5
	bls	.L233
	mul	r5, r6, r3
	adds	r2, r2, #1
	ldrh	r5, [r4, r5]
	cmp	r5, r7
	bne	.L235
.L233:
	ldr	r2, [r1, #2536]
	ldrh	r0, [r2, r3, lsl #1]
	pop	{r4, r5, r6, r7, pc}
.L235:
	mov	r3, r5
	b	.L231
.L234:
	mov	r0, r3
	pop	{r4, r5, r6, r7, pc}
.L237:
	.align	2
.L236:
	.word	.LANCHOR0
	.word	-1431655765
	.fnend
	.size	GetFreeBlockMaxEraseCount, .-GetFreeBlockMaxEraseCount
	.align	1
	.global	FtlPrintInfo2buf
	.syntax unified
	.thumb
	.thumb_func
	.fpu softvfp
	.type	FtlPrintInfo2buf, %function
FtlPrintInfo2buf:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	push	{r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.save {r4, r5, r6, r7, r8, r9, r10, fp, lr}
	mov	r6, r0
	ldr	r5, .L250
	add	r4, r6, #12
	.pad #20
	sub	sp, sp, #20
	ldr	r1, .L250+4
	bl	strcpy
	ldr	r3, [r5, #316]
	movs	r1, #64
	ldr	r2, .L250+8
	mov	r0, r4
	bl	snprintf
	add	r4, r4, r0
	ldr	r1, .L250+12
	mov	r0, r4
	adds	r4, r4, #10
	bl	strcpy
	ldr	r3, [r5, #2556]
	movs	r1, #64
	ldr	r2, .L250+16
	mov	r0, r4
	bl	snprintf
	add	r4, r4, r0
	ldr	r3, [r5, #2548]
	ldr	r2, .L250+20
	movs	r1, #64
	mov	r0, r4
	bl	snprintf
	add	r4, r4, r0
	ldr	r3, [r5, #2560]
	ldr	r2, .L250+24
	movs	r1, #64
	mov	r0, r4
	bl	snprintf
	add	r4, r4, r0
	ldr	r3, [r5, #2564]
	ldr	r2, .L250+28
	movs	r1, #64
	mov	r0, r4
	bl	snprintf
	add	r4, r4, r0
	ldr	r3, [r5, #2568]
	ldr	r2, .L250+32
	movs	r1, #64
	mov	r0, r4
	bl	snprintf
	add	r4, r4, r0
	ldr	r3, [r5, #2572]
	ldr	r2, .L250+36
	movs	r1, #64
	mov	r0, r4
	bl	snprintf
	add	r4, r4, r0
	ldr	r3, [r5, #2576]
	ldr	r2, .L250+40
	movs	r1, #64
	mov	r0, r4
	bl	snprintf
	add	r4, r4, r0
	ldr	r3, [r5, #2580]
	ldr	r2, .L250+44
	movs	r1, #64
	mov	r0, r4
	bl	snprintf
	ldr	r3, [r5, #2584]
	add	r4, r4, r0
	ldr	r2, .L250+48
	movs	r1, #64
	mov	r0, r4
	lsrs	r3, r3, #11
	bl	snprintf
	ldr	r3, [r5, #2588]
	add	r4, r4, r0
	ldr	r2, .L250+52
	movs	r1, #64
	mov	r0, r4
	lsrs	r3, r3, #11
	bl	snprintf
	add	r4, r4, r0
	ldr	r3, [r5, #2592]
	ldr	r2, .L250+56
	movs	r1, #64
	mov	r0, r4
	bl	snprintf
	add	r4, r4, r0
	ldr	r3, [r5, #2596]
	ldr	r2, .L250+60
	movs	r1, #64
	mov	r0, r4
	bl	snprintf
	add	r4, r4, r0
	ldrh	r3, [r5, #358]
	ldr	r2, .L250+64
	movs	r1, #64
	mov	r0, r4
	bl	snprintf
	add	r4, r4, r0
	ldrh	r3, [r5, #224]
	ldr	r2, .L250+68
	movs	r1, #64
	mov	r0, r4
	bl	snprintf
	add	r4, r4, r0
	ldr	r3, [r5, #2600]
	ldr	r2, .L250+72
	movs	r1, #64
	mov	r0, r4
	bl	snprintf
	add	r4, r4, r0
	ldr	r3, [r5, #2604]
	ldr	r2, .L250+76
	movs	r1, #64
	mov	r0, r4
	bl	snprintf
	add	r4, r4, r0
	ldr	r3, [r5, #2608]
	ldr	r2, .L250+80
	movs	r1, #64
	mov	r0, r4
	bl	snprintf
	add	r4, r4, r0
	ldr	r3, [r5, #2612]
	ldr	r2, .L250+84
	movs	r1, #64
	mov	r0, r4
	bl	snprintf
	add	r4, r4, r0
	ldr	r3, [r5, #2616]
	ldr	r2, .L250+88
	movs	r1, #64
	mov	r0, r4
	bl	snprintf
	add	r4, r4, r0
	ldr	r3, [r5, #2620]
	ldr	r2, .L250+92
	movs	r1, #64
	mov	r0, r4
	bl	snprintf
	add	r4, r4, r0
	ldrh	r3, [r5, #2498]
	ldr	r2, .L250+96
	movs	r1, #64
	mov	r0, r4
	bl	snprintf
	add	r4, r4, r0
	ldrh	r3, [r5, #2496]
	ldr	r2, .L250+100
	movs	r1, #64
	mov	r0, r4
	bl	snprintf
	add	r4, r4, r0
	ldr	r3, [r5, #340]
	ldr	r2, .L250+104
	movs	r1, #64
	mov	r0, r4
	bl	snprintf
	add	r4, r4, r0
	ldr	r3, [r5, #332]
	ldr	r2, .L250+108
	movs	r1, #64
	mov	r0, r4
	bl	snprintf
	add	r4, r4, r0
	ldr	r3, [r5, #228]
	ldr	r2, .L250+112
	movs	r1, #64
	mov	r0, r4
	bl	snprintf
	add	r4, r4, r0
	ldrh	r3, [r5, #418]
	ldr	r2, .L250+116
	movs	r1, #64
	mov	r0, r4
	bl	snprintf
	add	r4, r4, r0
	ldrh	r3, [r5, #240]
	ldr	r2, .L250+120
	movs	r1, #64
	mov	r0, r4
	bl	snprintf
	add	r4, r4, r0
	ldrh	r3, [r5, #2624]
	ldr	r2, .L250+124
	movs	r1, #64
	mov	r0, r4
	bl	snprintf
	add	r4, r4, r0
	ldr	r3, [r5, #244]
	ldr	r2, .L250+128
	movs	r1, #64
	mov	r0, r4
	bl	snprintf
	add	r4, r4, r0
	ldrh	r3, [r5, #2628]
	ldr	r2, .L250+132
	movs	r1, #64
	mov	r0, r4
	bl	snprintf
	add	r4, r4, r0
	ldrh	r3, [r5, #352]
	ldr	r2, .L250+136
	movs	r1, #64
	mov	r0, r4
	bl	snprintf
	add	r4, r4, r0
	ldrh	r3, [r5, #26]
	ldr	r2, .L250+140
	movs	r1, #64
	mov	r0, r4
	bl	snprintf
	add	r4, r4, r0
	ldrb	r3, [r5, #30]	@ zero_extendqisi2
	ldr	r2, .L250+144
	movs	r1, #64
	mov	r0, r4
	bl	snprintf
	add	r4, r4, r0
	ldrh	r3, [r5, #24]
	ldr	r2, .L250+148
	movs	r1, #64
	mov	r0, r4
	bl	snprintf
	add	r4, r4, r0
	ldrb	r3, [r5, #32]	@ zero_extendqisi2
	ldr	r2, .L250+152
	movs	r1, #64
	mov	r0, r4
	bl	snprintf
	add	r4, r4, r0
	ldrh	r3, [r5, #28]
	ldr	r2, .L250+156
	movs	r1, #64
	mov	r0, r4
	bl	snprintf
	ldrh	r2, [r5, #24]
	add	r4, r4, r0
	ldr	r3, [r5, #72]
	movs	r1, #64
	mov	r0, r4
	ldrh	r3, [r3, r2, lsl #1]
	ldr	r2, .L250+160
	bl	snprintf
	add	r4, r4, r0
	ldrh	r3, [r5, #78]
	ldr	r2, .L250+164
	movs	r1, #64
	mov	r0, r4
	bl	snprintf
	add	r4, r4, r0
	ldrb	r3, [r5, #82]	@ zero_extendqisi2
	ldr	r2, .L250+168
	movs	r1, #64
	mov	r0, r4
	bl	snprintf
	add	r4, r4, r0
	ldrh	r3, [r5, #76]
	ldr	r2, .L250+172
	movs	r1, #64
	mov	r0, r4
	bl	snprintf
	add	r4, r4, r0
	ldrb	r3, [r5, #84]	@ zero_extendqisi2
	ldr	r2, .L250+176
	movs	r1, #64
	mov	r0, r4
	bl	snprintf
	add	r4, r4, r0
	ldrh	r3, [r5, #80]
	ldr	r2, .L250+180
	movs	r1, #64
	mov	r0, r4
	bl	snprintf
	ldrh	r2, [r5, #76]
	add	r4, r4, r0
	ldr	r3, [r5, #72]
	movs	r1, #64
	mov	r0, r4
	ldrh	r3, [r3, r2, lsl #1]
	ldr	r2, .L250+184
	bl	snprintf
	add	r4, r4, r0
	ldrh	r3, [r5, #126]
	ldr	r2, .L250+188
	movs	r1, #64
	mov	r0, r4
	bl	snprintf
	add	r4, r4, r0
	ldrb	r3, [r5, #130]	@ zero_extendqisi2
	ldr	r2, .L250+192
	movs	r1, #64
	mov	r0, r4
	bl	snprintf
	add	r4, r4, r0
	ldrh	r3, [r5, #124]
	ldr	r2, .L250+196
	movs	r1, #64
	mov	r0, r4
	bl	snprintf
	add	r4, r4, r0
	ldrb	r3, [r5, #132]	@ zero_extendqisi2
	ldr	r2, .L250+200
	movs	r1, #64
	mov	r0, r4
	bl	snprintf
	add	r4, r4, r0
	ldrh	r3, [r5, #128]
	ldr	r2, .L250+204
	movs	r1, #64
	mov	r0, r4
	bl	snprintf
	add	r4, r4, r0
	ldrh	r3, [r5, #174]
	ldr	r2, .L250+208
	movs	r1, #64
	mov	r0, r4
	bl	snprintf
	add	r4, r4, r0
	ldrb	r3, [r5, #178]	@ zero_extendqisi2
	ldr	r2, .L250+212
	movs	r1, #64
	mov	r0, r4
	bl	snprintf
	add	r4, r4, r0
	ldrh	r3, [r5, #172]
	ldr	r2, .L250+216
	movs	r1, #64
	mov	r0, r4
	bl	snprintf
	add	r4, r4, r0
	ldrb	r3, [r5, #180]	@ zero_extendqisi2
	ldr	r2, .L250+220
	movs	r1, #64
	mov	r0, r4
	bl	snprintf
	add	r4, r4, r0
	ldrh	r3, [r5, #176]
	b	.L251
.L252:
	.align	2
.L250:
	.word	.LANCHOR0
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
.L251:
	ldr	r2, .L253
	movs	r1, #64
	mov	r0, r4
	bl	snprintf
	ldr	r3, [r5, #2720]
	add	r4, r4, r0
	ldr	r2, .L253+4
	movs	r1, #64
	mov	r0, r4
	str	r3, [sp, #4]
	ldr	r3, [r5, #2728]
	str	r3, [sp]
	ldr	r3, [r5, #2724]
	bl	snprintf
	add	r4, r4, r0
	ldr	r3, [r5, #2716]
	ldr	r2, .L253+8
	movs	r1, #64
	mov	r0, r4
	bl	snprintf
	add	r4, r4, r0
	ldr	r3, [r5, #2740]
	ldr	r2, .L253+12
	movs	r1, #64
	mov	r0, r4
	bl	snprintf
	add	r4, r4, r0
	ldrh	r3, [r5, #3156]
	ldr	r2, .L253+16
	movs	r1, #64
	mov	r0, r4
	bl	snprintf
	add	r4, r4, r0
	ldrh	r3, [r5, #3158]
	ldr	r2, .L253+20
	movs	r1, #64
	mov	r0, r4
	bl	snprintf
	add	r4, r4, r0
	ldr	r3, [r5, #3160]
	ldr	r2, .L253+24
	movs	r1, #64
	mov	r0, r4
	bl	snprintf
	add	r4, r4, r0
	ldrh	r3, [r5, #3164]
	ldr	r2, .L253+28
	movs	r1, #64
	mov	r0, r4
	bl	snprintf
	add	r4, r4, r0
	bl	GetFreeBlockMinEraseCount
	ldr	r2, .L253+32
	mov	r3, r0
	movs	r1, #64
	mov	r0, r4
	bl	snprintf
	add	r4, r4, r0
	ldrh	r0, [r5, #224]
	bl	GetFreeBlockMaxEraseCount
	ldr	r2, .L253+36
	mov	r3, r0
	movs	r1, #64
	mov	r0, r4
	bl	snprintf
	ldr	r3, .L253+40
	add	r4, r4, r0
	ldr	r3, [r3]
	cmp	r3, #1
	beq	.L239
.L244:
	subs	r0, r4, r6
	add	sp, sp, #20
	@ sp needed
	pop	{r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L239:
	ldrh	r3, [r5, #172]
	movw	r2, #65535
	cmp	r3, r2
	beq	.L241
	ldr	r2, [r5, #72]
	mov	r0, r4
	movs	r1, #64
	ldrh	r3, [r2, r3, lsl #1]
	ldr	r2, .L253+44
	bl	snprintf
	add	r4, r4, r0
.L241:
	movs	r0, #0
	ldr	r9, .L253+52
	bl	List_get_gc_head_node
	uxth	r0, r0
	movs	r7, #0
	movw	fp, #65535
	mov	r10, #6
.L243:
	cmp	r0, fp
	beq	.L242
	ldr	r3, [r5, #2536]
	mul	r8, r10, r0
	mov	r2, r9
	movs	r1, #64
	ldrh	r3, [r3, r0, lsl #1]
	str	r3, [sp, #12]
	ldr	r3, [r5, #2516]
	add	r3, r3, r8
	ldrh	r3, [r3, #4]
	str	r3, [sp, #8]
	ldr	r3, [r5, #72]
	ldrh	r3, [r3, r0, lsl #1]
	stm	sp, {r0, r3}
	mov	r3, r7
	mov	r0, r4
	adds	r7, r7, #1
	bl	snprintf
	ldr	r3, [r5, #2516]
	cmp	r7, #16
	add	r4, r4, r0
	ldrh	r0, [r3, r8]
	bne	.L243
.L242:
	ldr	r2, [r5, #2516]
	movs	r7, #0
	ldr	r3, [r5, #2532]
	movw	r9, #65535
	ldr	fp, .L253+56
	mov	r10, #6
	subs	r3, r3, r2
	asrs	r2, r3, #1
	ldr	r3, .L253+48
	muls	r3, r2, r3
	uxth	r3, r3
.L245:
	cmp	r3, r9
	beq	.L244
	ldr	r2, [r5, #2536]
	mul	r8, r10, r3
	mov	r0, r4
	movs	r1, #64
	ldrh	r2, [r2, r3, lsl #1]
	str	r2, [sp, #8]
	ldr	r2, [r5, #2516]
	add	r2, r2, r8
	ldrh	r2, [r2, #4]
	str	r3, [sp]
	mov	r3, r7
	adds	r7, r7, #1
	str	r2, [sp, #4]
	mov	r2, fp
	bl	snprintf
	cmp	r7, #4
	add	r4, r4, r0
	beq	.L244
	ldr	r3, [r5, #2516]
	ldrh	r3, [r3, r8]
	b	.L245
.L254:
	.align	2
.L253:
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
	.word	-1431655765
	.word	.LC75
	.word	.LC76
	.fnend
	.size	FtlPrintInfo2buf, .-FtlPrintInfo2buf
	.align	1
	.global	rknand_proc_ftlread
	.syntax unified
	.thumb
	.thumb_func
	.fpu softvfp
	.type	rknand_proc_ftlread, %function
rknand_proc_ftlread:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	cmp	r0, #2048
	push	{r3, r4, r5, lr}
	.save {r3, r4, r5, lr}
	mov	r5, r1
	blt	.L257
	ldr	r3, .L258
	movs	r1, #64
	ldr	r2, .L258+4
	mov	r0, r5
	bl	snprintf
	adds	r4, r5, r0
	mov	r0, r4
	bl	FtlPrintInfo2buf
	add	r0, r0, r4
	subs	r0, r0, r5
	pop	{r3, r4, r5, pc}
.L257:
	movs	r0, #0
	pop	{r3, r4, r5, pc}
.L259:
	.align	2
.L258:
	.word	.LC0
	.word	.LC77
	.fnend
	.size	rknand_proc_ftlread, .-rknand_proc_ftlread
	.align	1
	.global	GetSwlReplaceBlock
	.syntax unified
	.thumb
	.thumb_func
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
	ldr	r4, .L284
	ldr	r2, [r4, #2608]
	ldr	r3, [r4, #2620]
	cmp	r2, r3
	bcs	.L261
	ldrh	r1, [r4, #240]
	movs	r3, #0
	ldr	r0, [r4, #2536]
	str	r3, [r4, #2600]
.L262:
	cmp	r3, r1
	bcc	.L263
	ldr	r5, [r4, #2600]
	mov	r0, r5
	bl	__aeabi_uidiv
	str	r0, [r4, #2608]
	ldr	r0, [r4, #2604]
	ldrh	r1, [r4, #292]
	subs	r0, r5, r0
	bl	__aeabi_uidiv
	str	r0, [r4, #2600]
.L264:
	ldr	r6, [r4, #2620]
	ldr	r7, [r4, #2608]
	add	r3, r6, #256
	cmp	r3, r7
	bls	.L269
	ldr	r2, [r4, #2616]
	add	r3, r6, #768
	cmp	r3, r2
	bls	.L269
.L271:
	movw	r5, #65535
.L270:
	mov	r0, r5
	add	sp, sp, #28
	@ sp needed
	pop	{r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L263:
	ldrh	r2, [r0, r3, lsl #1]
	adds	r3, r3, #1
	ldr	r5, [r4, #2600]
	add	r2, r2, r5
	str	r2, [r4, #2600]
	b	.L262
.L261:
	ldr	r3, [r4, #2616]
	cmp	r2, r3
	bls	.L264
	adds	r3, r3, #1
	str	r3, [r4, #2616]
	movs	r3, #0
.L266:
	ldrh	r2, [r4, #240]
	cmp	r3, r2
	bcs	.L264
	ldr	r1, [r4, #2536]
	ldrh	r2, [r1, r3, lsl #1]
	adds	r2, r2, #1
	strh	r2, [r1, r3, lsl #1]	@ movhi
	adds	r3, r3, #1
	b	.L266
.L269:
	ldrh	r0, [r4, #224]
	add	r0, r0, r0, lsl #1
	ubfx	r0, r0, #2, #16
	bl	GetFreeBlockMaxEraseCount
	add	r3, r6, #64
	mov	r9, r0
	cmp	r0, r3
	bcc	.L271
	ldr	r3, [r4, #2520]
	cmp	r3, #0
	beq	.L271
	ldrh	r2, [r4, #240]
	movw	r1, #65535
	ldr	lr, [r4, #2516]
	movs	r0, #0
	ldr	r8, [r4, #2536]
	mov	r10, r1
	str	r2, [sp, #20]
	mov	fp, #6
	mov	r2, r1
.L272:
	ldrh	ip, [r3]
	cmp	ip, r10
	bne	.L275
	mov	r5, r2
.L274:
	movw	r3, #65535
	cmp	r5, r3
	beq	.L271
	ldrh	r10, [r8, r5, lsl #1]
	lsl	fp, r5, #1
	cmp	r6, r10
	bcs	.L276
	bl	GetFreeBlockMinEraseCount
	cmp	r6, r0
	it	cc
	strcc	r1, [r4, #2620]
.L276:
	cmp	r7, r10
	bls	.L271
	add	r3, r10, #128
	cmp	r9, r3
	ble	.L271
	add	r3, r10, #256
	cmp	r7, r3
	bhi	.L277
	ldr	r3, [r4, #2616]
	add	r10, r10, #768
	cmp	r10, r3
	bcs	.L271
.L277:
	str	r9, [sp, #8]
	mov	r2, r7
	ldrh	r3, [r8, fp]
	mov	r1, r5
	ldr	r0, .L284+4
	str	r3, [sp, #4]
	ldr	r3, [r4, #72]
	ldrh	r3, [r3, fp]
	str	r3, [sp]
	ldr	r3, [r4, #2616]
	bl	sftl_printk
	movs	r3, #1
	str	r3, [r4, #3168]
	b	.L270
.L275:
	adds	r0, r0, #1
	ldr	r5, [sp, #20]
	uxth	r0, r0
	cmp	r0, r5
	bhi	.L271
	ldrh	r5, [r3, #4]
	cbz	r5, .L273
	ldr	r5, .L284+8
	sub	r3, r3, lr
	asrs	r3, r3, #1
	muls	r3, r5, r3
	uxth	r5, r3
	ldrh	r3, [r8, r5, lsl #1]
	cmp	r6, r3
	bcs	.L274
	cmp	r1, r3
	itt	hi
	movhi	r1, r3
	movhi	r2, r5
.L273:
	mla	r3, fp, ip, lr
	b	.L272
.L285:
	.align	2
.L284:
	.word	.LANCHOR0
	.word	.LC78
	.word	-1431655765
	.fnend
	.size	GetSwlReplaceBlock, .-GetSwlReplaceBlock
	.align	1
	.global	free_data_superblock
	.syntax unified
	.thumb
	.thumb_func
	.fpu softvfp
	.type	free_data_superblock, %function
free_data_superblock:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	movw	r2, #65535
	push	{r3, lr}
	.save {r3, lr}
	cmp	r0, r2
	beq	.L287
	ldr	r2, .L288
	movs	r1, #0
	ldr	r2, [r2, #72]
	strh	r1, [r2, r0, lsl #1]	@ movhi
	bl	INSERT_FREE_LIST
.L287:
	movs	r0, #0
	pop	{r3, pc}
.L289:
	.align	2
.L288:
	.word	.LANCHOR0
	.fnend
	.size	free_data_superblock, .-free_data_superblock
	.align	1
	.global	get_new_active_ppa
	.syntax unified
	.thumb
	.thumb_func
	.fpu softvfp
	.type	get_new_active_ppa, %function
get_new_active_ppa:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	push	{r3, r4, r5, r6, r7, lr}
	.save {r3, r4, r5, r6, r7, lr}
	movw	r3, #65535
	ldrh	r2, [r0]
	mov	r4, r0
	cmp	r2, r3
	bne	.L291
	movw	r2, #2785
	ldr	r1, .L305
	ldr	r0, .L305+4
	bl	sftl_printk
.L291:
	ldr	r6, .L305+8
	ldrh	r2, [r4, #2]
	ldrh	r3, [r6, #302]
	cmp	r2, r3
	bne	.L292
	movw	r2, #2786
	ldr	r1, .L305
	ldr	r0, .L305+4
	bl	sftl_printk
.L292:
	ldrh	r3, [r4, #4]
	cbnz	r3, .L293
	movw	r2, #2787
	ldr	r1, .L305
	ldr	r0, .L305+4
	bl	sftl_printk
.L293:
	ldrb	r2, [r4, #6]	@ zero_extendqisi2
	movs	r3, #0
	ldrh	r1, [r6, #232]
	movw	r5, #65535
	strb	r3, [r4, #10]
	adds	r2, r2, #8
	ldrh	r0, [r4, r2, lsl #1]
	mov	r2, r3
.L294:
	cmp	r0, r5
	beq	.L296
	ldrh	r5, [r4, #2]
	ldrh	r6, [r6, #302]
	cmp	r5, r6
	bcs	.L300
	ldrh	r2, [r4, #4]
	orr	r5, r5, r0, lsl #10
	ldrb	r3, [r4, #6]	@ zero_extendqisi2
	movw	r0, #65535
	subs	r2, r2, #1
	uxth	r2, r2
	strh	r2, [r4, #4]	@ movhi
.L299:
	adds	r3, r3, #1
	uxtb	r3, r3
	cmp	r1, r3
	itttt	eq
	ldrheq	r3, [r4, #2]
	addeq	r3, r3, #1
	strheq	r3, [r4, #2]	@ movhi
	moveq	r3, #0
	add	r7, r3, #8
	ldrh	r7, [r4, r7, lsl #1]
	cmp	r7, r0
	beq	.L299
	strb	r3, [r4, #6]
	ldrh	r3, [r4, #2]
	cmp	r3, r6
	bne	.L290
	cbz	r2, .L290
	movw	r2, #2810
	ldr	r1, .L305
	ldr	r0, .L305+4
	bl	sftl_printk
.L290:
	mov	r0, r5
	pop	{r3, r4, r5, r6, r7, pc}
.L296:
	ldrb	r3, [r4, #6]	@ zero_extendqisi2
	adds	r3, r3, #1
	uxtb	r3, r3
	cmp	r3, r1
	strb	r3, [r4, #6]
	itttt	eq
	ldrheq	r3, [r4, #2]
	strbeq	r2, [r4, #6]
	addeq	r3, r3, #1
	strheq	r3, [r4, #2]	@ movhi
	ldrb	r3, [r4, #6]	@ zero_extendqisi2
	adds	r3, r3, #8
	ldrh	r0, [r4, r3, lsl #1]
	b	.L294
.L300:
	movw	r5, #65535
	b	.L290
.L306:
	.align	2
.L305:
	.word	.LANCHOR1+111
	.word	.LC8
	.word	.LANCHOR0
	.fnend
	.size	get_new_active_ppa, .-get_new_active_ppa
	.align	1
	.global	FtlGcBufInit
	.syntax unified
	.thumb
	.thumb_func
	.fpu softvfp
	.type	FtlGcBufInit, %function
FtlGcBufInit:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	ldr	r3, .L316
	mov	ip, #20
	push	{r4, r5, r6, r7, r8, r9, lr}
	.save {r4, r5, r6, r7, r8, r9, lr}
	mov	r9, #12
	movs	r6, #1
	movs	r2, #0
	mov	r7, r3
	str	r2, [r3, #3172]
.L308:
	ldrh	r1, [r3, #232]
	uxth	r0, r2
	adds	r4, r2, #1
	cmp	r0, r1
	bcc	.L311
	ldr	r7, .L316
	mov	ip, #12
	movs	r6, #0
.L312:
	ldr	r2, [r3, #3192]
	cmp	r1, r2
	bcc	.L315
	pop	{r4, r5, r6, r7, r8, r9, pc}
.L311:
	uxth	r2, r2
	ldr	r8, [r3, #3176]
	mul	lr, r9, r2
	add	r1, r8, lr
	str	r6, [r1, #8]
	ldrh	r1, [r3, #310]
	muls	r1, r2, r1
	it	mi
	addmi	r1, r1, #3
	bic	r0, r1, #3
	ldr	r1, [r3, #3180]
	add	r1, r1, r0
	str	r1, [r8, lr]
	ldr	r5, [r7, #3184]
	ldr	r0, [r3, #3176]
	ldrh	r1, [r3, #312]
	muls	r1, r2, r1
	it	mi
	addmi	r1, r1, #3
	bic	r1, r1, #3
	add	r8, r0, lr
	add	r1, r1, r5
	str	r1, [r8, #4]
	ldr	r1, [r3, #3188]
	mla	r2, ip, r2, r1
	ldr	r1, [r0, lr]
	str	r1, [r2, #8]
	ldr	r1, [r8, #4]
	str	r1, [r2, #12]
	mov	r2, r4
	b	.L308
.L315:
	mul	r0, ip, r1
	ldr	r4, [r3, #3176]
	ldr	r5, [r3, #3180]
	adds	r2, r4, r0
	str	r6, [r2, #8]
	ldrh	r2, [r3, #310]
	muls	r2, r1, r2
	it	mi
	addmi	r2, r2, #3
	bic	r2, r2, #3
	add	r2, r2, r5
	str	r2, [r4, r0]
	ldr	r2, [r3, #3176]
	ldr	r4, [r7, #3184]
	add	r0, r0, r2
	ldrh	r2, [r3, #312]
	muls	r2, r1, r2
	it	mi
	addmi	r2, r2, #3
	bic	r2, r2, #3
	adds	r1, r1, #1
	add	r2, r2, r4
	uxth	r1, r1
	str	r2, [r0, #4]
	b	.L312
.L317:
	.align	2
.L316:
	.word	.LANCHOR0
	.fnend
	.size	FtlGcBufInit, .-FtlGcBufInit
	.align	1
	.global	FtlGcBufFree
	.syntax unified
	.thumb
	.thumb_func
	.fpu softvfp
	.type	FtlGcBufFree, %function
FtlGcBufFree:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	ldr	r3, .L325
	push	{r4, r5, r6, r7, r8, r9, r10, lr}
	.save {r4, r5, r6, r7, r8, r9, r10, lr}
	movs	r5, #0
	mov	r10, #20
	mov	lr, #12
	mov	r8, r5
	ldr	ip, [r3, #3192]
	ldr	r6, [r3, #3176]
.L319:
	uxth	r3, r5
	cmp	r1, r3
	bls	.L318
	mla	r4, r10, r3, r0
	movs	r2, #0
.L320:
	uxth	r3, r2
	cmp	ip, r3
	bls	.L321
	mul	r3, lr, r3
	ldr	r7, [r4, #8]
	adds	r2, r2, #1
	add	r9, r6, r3
	ldr	r3, [r6, r3]
	cmp	r3, r7
	bne	.L320
	str	r8, [r9, #8]
.L321:
	adds	r5, r5, #1
	b	.L319
.L318:
	pop	{r4, r5, r6, r7, r8, r9, r10, pc}
.L326:
	.align	2
.L325:
	.word	.LANCHOR0
	.fnend
	.size	FtlGcBufFree, .-FtlGcBufFree
	.align	1
	.global	FtlGcBufAlloc
	.syntax unified
	.thumb
	.thumb_func
	.fpu softvfp
	.type	FtlGcBufAlloc, %function
FtlGcBufAlloc:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	ldr	r3, .L334
	mov	ip, #12
	push	{r4, r5, r6, r7, r8, r9, lr}
	.save {r4, r5, r6, r7, r8, r9, lr}
	movs	r4, #0
	mov	lr, #1
	mov	r8, #20
	ldr	r5, [r3, #3192]
	ldr	r6, [r3, #3176]
.L328:
	uxth	r2, r4
	cmp	r1, r2
	bhi	.L332
	pop	{r4, r5, r6, r7, r8, r9, pc}
.L332:
	mov	r9, #0
.L329:
	uxth	r3, r9
	cmp	r5, r3
	bls	.L330
	mla	r3, ip, r3, r6
	add	r9, r9, #1
	ldr	r7, [r3, #8]
	cmp	r7, #0
	bne	.L329
	mla	r2, r8, r2, r0
	ldr	r7, [r3]
	str	lr, [r3, #8]
	str	r7, [r2, #8]
	ldr	r3, [r3, #4]
	str	r3, [r2, #12]
.L330:
	adds	r4, r4, #1
	b	.L328
.L335:
	.align	2
.L334:
	.word	.LANCHOR0
	.fnend
	.size	FtlGcBufAlloc, .-FtlGcBufAlloc
	.align	1
	.global	IsBlkInGcList
	.syntax unified
	.thumb
	.thumb_func
	.fpu softvfp
	.type	IsBlkInGcList, %function
IsBlkInGcList:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	@ link register save eliminated.
	ldr	r2, .L341
	ldr	r3, [r2, #3196]
	ldrh	r2, [r2, #3200]
	add	r2, r3, r2, lsl #1
.L337:
	cmp	r3, r2
	bne	.L339
	movs	r0, #0
	bx	lr
.L339:
	ldrh	r1, [r3], #2
	cmp	r1, r0
	bne	.L337
	movs	r0, #1
	bx	lr
.L342:
	.align	2
.L341:
	.word	.LANCHOR0
	.fnend
	.size	IsBlkInGcList, .-IsBlkInGcList
	.align	1
	.global	FtlGcUpdatePage
	.syntax unified
	.thumb
	.thumb_func
	.fpu softvfp
	.type	FtlGcUpdatePage, %function
FtlGcUpdatePage:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	push	{r3, r4, r5, r6, r7, lr}
	.save {r3, r4, r5, r6, r7, lr}
	mov	r5, r0
	ubfx	r0, r0, #10, #16
	mov	r6, r1
	mov	r7, r2
	movs	r4, #0
	bl	P2V_block_in_plane
	ldr	r3, .L347
	ldrh	r1, [r3, #3200]
	ldr	r2, [r3, #3196]
.L344:
	uxth	ip, r4
	cmp	ip, r1
	bcc	.L346
	bne	.L345
	strh	r0, [r2, ip, lsl #1]	@ movhi
	ldrh	r0, [r3, #3200]
	adds	r0, r0, #1
	strh	r0, [r3, #3200]	@ movhi
	b	.L345
.L346:
	adds	r4, r4, #1
	add	ip, r2, r4, lsl #1
	ldrh	ip, [ip, #-2]
	cmp	ip, r0
	bne	.L344
.L345:
	ldrh	r2, [r3, #3208]
	movs	r0, #12
	muls	r0, r2, r0
	ldr	r2, [r3, #3204]
	adds	r1, r2, r0
	str	r6, [r1, #4]
	str	r7, [r1, #8]
	str	r5, [r2, r0]
	ldrh	r2, [r3, #3208]
	adds	r2, r2, #1
	strh	r2, [r3, #3208]	@ movhi
	pop	{r3, r4, r5, r6, r7, pc}
.L348:
	.align	2
.L347:
	.word	.LANCHOR0
	.fnend
	.size	FtlGcUpdatePage, .-FtlGcUpdatePage
	.align	1
	.global	FtlGcRefreshBlock
	.syntax unified
	.thumb
	.thumb_func
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
	ldr	r0, .L352
	bl	sftl_printk
	ldr	r3, .L352+4
	ldrh	r0, [r3, #222]
	cmp	r4, r0
	beq	.L350
	ldrh	r1, [r3, #220]
	cmp	r4, r1
	beq	.L350
	movw	r2, #65535
	cmp	r0, r2
	bne	.L351
	strh	r4, [r3, #222]	@ movhi
.L350:
	movs	r0, #0
	pop	{r4, pc}
.L351:
	cmp	r1, r2
	it	eq
	strheq	r4, [r3, #220]	@ movhi
	b	.L350
.L353:
	.align	2
.L352:
	.word	.LC79
	.word	.LANCHOR0
	.fnend
	.size	FtlGcRefreshBlock, .-FtlGcRefreshBlock
	.align	1
	.global	FtlGcMarkBadPhyBlk
	.syntax unified
	.thumb
	.thumb_func
	.fpu softvfp
	.type	FtlGcMarkBadPhyBlk, %function
FtlGcMarkBadPhyBlk:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	push	{r4, r5, r6, lr}
	.save {r4, r5, r6, lr}
	mov	r5, r0
	ldr	r4, .L358
	bl	P2V_block_in_plane
	mov	r2, r5
	mov	r6, r0
	ldrh	r1, [r4, #3210]
	ldr	r0, .L358+4
	bl	sftl_printk
	mov	r0, r6
	bl	FtlGcRefreshBlock
	ldrh	r3, [r4, #3210]
	movs	r2, #0
	addw	r0, r4, #3212
.L355:
	uxth	r1, r2
	cmp	r3, r1
	bhi	.L357
	cmp	r3, #15
	itttt	ls
	addls	r2, r3, #1
	strhls	r2, [r4, #3210]	@ movhi
	addls	r4, r4, r3, lsl #1
	strhls	r5, [r4, #3212]	@ movhi
	b	.L356
.L357:
	adds	r2, r2, #1
	add	r1, r0, r2, lsl #1
	ldrh	r1, [r1, #-2]
	cmp	r1, r5
	bne	.L355
.L356:
	movs	r0, #0
	pop	{r4, r5, r6, pc}
.L359:
	.align	2
.L358:
	.word	.LANCHOR0
	.word	.LC80
	.fnend
	.size	FtlGcMarkBadPhyBlk, .-FtlGcMarkBadPhyBlk
	.align	1
	.global	FtlGcReFreshBadBlk
	.syntax unified
	.thumb
	.thumb_func
	.fpu softvfp
	.type	FtlGcReFreshBadBlk, %function
FtlGcReFreshBadBlk:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	push	{r4, lr}
	.save {r4, lr}
	ldr	r4, .L366
	ldrh	r3, [r4, #3210]
	cbz	r3, .L361
	ldrh	r1, [r4, #222]
	movw	r2, #65535
	cmp	r1, r2
	bne	.L361
	ldrh	r2, [r4, #3246]
	cmp	r2, r3
	itt	cs
	movcs	r3, #0
	strhcs	r3, [r4, #3246]	@ movhi
	ldrh	r3, [r4, #3246]
	add	r3, r4, r3, lsl #1
	ldrh	r0, [r3, #3212]
	bl	P2V_block_in_plane
	bl	FtlGcRefreshBlock
	ldrh	r3, [r4, #3246]
	adds	r3, r3, #1
	strh	r3, [r4, #3246]	@ movhi
.L361:
	movs	r0, #0
	pop	{r4, pc}
.L367:
	.align	2
.L366:
	.word	.LANCHOR0
	.fnend
	.size	FtlGcReFreshBadBlk, .-FtlGcReFreshBadBlk
	.align	1
	.global	ftl_malloc
	.syntax unified
	.thumb
	.thumb_func
	.fpu softvfp
	.type	ftl_malloc, %function
ftl_malloc:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	@ link register save eliminated.
	ldr	r1, .L369
	b	__kmalloc
.L370:
	.align	2
.L369:
	.word	6291649
	.fnend
	.size	ftl_malloc, .-ftl_malloc
	.align	1
	.global	ftl_free
	.syntax unified
	.thumb
	.thumb_func
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
	.align	1
	.global	rknand_print_hex
	.syntax unified
	.thumb
	.thumb_func
	.fpu softvfp
	.type	rknand_print_hex, %function
rknand_print_hex:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	push	{r3, r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.save {r3, r4, r5, r6, r7, r8, r9, r10, fp, lr}
	movs	r6, #0
	ldr	fp, .L381+16
	mov	r10, r0
	mov	r7, r1
	mov	r8, r2
	mov	r9, r3
	mov	r5, r6
	mov	r4, r6
.L373:
	cmp	r4, r9
	bcc	.L379
	ldr	r0, .L381
	pop	{r3, r4, r5, r6, r7, r8, r9, r10, fp, lr}
	b	sftl_printk
.L379:
	cbnz	r5, .L374
	mov	r3, r6
	mov	r2, r7
	mov	r1, r10
	ldr	r0, .L381+4
	bl	sftl_printk
.L374:
	cmp	r8, #4
	bne	.L375
	ldr	r1, [r7, r4, lsl #2]
	ldr	r0, .L381+8
.L380:
	adds	r5, r5, #1
	bl	sftl_printk
	cmp	r5, #15
	bls	.L378
	movs	r5, #0
	ldr	r0, .L381
	bl	sftl_printk
.L378:
	adds	r4, r4, #1
	add	r6, r6, r8
	b	.L373
.L375:
	cmp	r8, #2
	ittee	eq
	ldrheq	r1, [r7, r4, lsl #1]
	moveq	r0, fp
	ldrbne	r1, [r7, r4]	@ zero_extendqisi2
	ldrne	r0, .L381+12
	b	.L380
.L382:
	.align	2
.L381:
	.word	.LC85
	.word	.LC81
	.word	.LC82
	.word	.LC84
	.word	.LC83
	.fnend
	.size	rknand_print_hex, .-rknand_print_hex
	.align	1
	.global	FlashEraseBlocks
	.syntax unified
	.thumb
	.thumb_func
	.fpu softvfp
	.type	FlashEraseBlocks, %function
FlashEraseBlocks:
	.fnstart
	@ args = 0, pretend = 0, frame = 24
	@ frame_needed = 0, uses_anonymous_args = 0
	push	{r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.save {r4, r5, r6, r7, r8, r9, r10, fp, lr}
	mov	r8, r2
	ldr	r5, .L400
	.pad #28
	sub	sp, sp, #28
	mov	r4, r0
	mov	fp, r0
	ldr	r2, .L400+4
	movs	r7, #0
	ldrh	r9, [r5, #12]
	mov	r10, #-1
	ldr	r3, [r2]
	str	r2, [sp, #4]
	str	r3, [sp, #20]
	lsl	r3, r9, #3
	str	r3, [sp]
.L384:
	cmp	r7, r8
	beq	.L388
	add	r2, sp, #12
	add	r1, sp, #16
	mov	r0, fp
	bl	l2p_addr_tran
	ldr	r6, [sp, #12]
	cbnz	r6, .L385
	ldr	r2, [sp, #16]
	ldr	r3, [sp]
	cmp	r3, r2
	bls	.L385
	ldr	r5, .L400+8
	ldr	r7, .L400+12
	ldr	r9, .L400+20
	b	.L399
.L387:
	mov	r3, #-1
	ldr	r2, [sp, #16]
	str	r3, [r4, #-20]
	mov	r1, r5
	mov	r0, r7
	adds	r6, r6, #1
	bl	sftl_printk
	movs	r3, #16
	movs	r2, #4
	ldr	r1, [r4, #-12]
	mov	r0, r9
	bl	rknand_print_hex
	movs	r3, #4
	ldr	r1, [r4, #-8]
	mov	r2, r3
	ldr	r0, .L400+16
	bl	rknand_print_hex
.L399:
	cmp	r6, r8
	add	r4, r4, #20
	bne	.L387
	bl	dump_stack
.L388:
	ldr	r3, [sp, #4]
	movs	r0, #0
	ldr	r2, [sp, #20]
	ldr	r3, [r3]
	cmp	r2, r3
	beq	.L395
	bl	__stack_chk_fail
.L385:
	ldr	r2, [r5, #3252]
	uxtb	r0, r6
	ldr	r1, [sp, #16]
	blx	r2
	cmp	r0, #0
	mov	r2, r0
	it	ne
	movne	r2, r10
	str	r2, [fp]
	ldrh	r2, [r5, #14]
	cmp	r2, #4
	bne	.L392
	ldr	r1, [sp, #16]
	ldr	r2, [r5, #3252]
	ldrb	r0, [sp, #12]	@ zero_extendqisi2
	add	r1, r1, r9
	blx	r2
	cbz	r0, .L392
	str	r10, [fp]
.L392:
	adds	r7, r7, #1
	add	fp, fp, #20
	b	.L384
.L395:
	add	sp, sp, #28
	@ sp needed
	pop	{r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L401:
	.align	2
.L400:
	.word	.LANCHOR0
	.word	__stack_chk_guard
	.word	.LANCHOR1+130
	.word	.LC86
	.word	.LC88
	.word	.LC87
	.fnend
	.size	FlashEraseBlocks, .-FlashEraseBlocks
	.align	1
	.global	FtlFreeSysBlkQueueIn
	.syntax unified
	.thumb
	.thumb_func
	.fpu softvfp
	.type	FtlFreeSysBlkQueueIn, %function
FtlFreeSysBlkQueueIn:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	subs	r3, r0, #1
	movw	r2, #65533
	uxth	r3, r3
	push	{r4, r5, r6, lr}
	.save {r4, r5, r6, lr}
	mov	r5, r0
	cmp	r3, r2
	bhi	.L402
	ldr	r4, .L411
	ldrh	r3, [r4, #418]
	cmp	r3, #1024
	beq	.L402
	cbz	r1, .L404
	bl	P2V_block_in_plane
	mov	r6, r0
	ldr	r0, [r4, #3272]
	lsls	r3, r5, #10
	movs	r2, #1
	mov	r1, r2
	str	r3, [r0, #4]
	bl	FlashEraseBlocks
	ldr	r2, [r4, #2536]
	ldrh	r3, [r2, r6, lsl #1]
	adds	r3, r3, #1
	strh	r3, [r2, r6, lsl #1]	@ movhi
	ldr	r3, [r4, #2612]
	adds	r3, r3, #1
	str	r3, [r4, #2612]
.L404:
	ldrh	r3, [r4, #418]
	adds	r3, r3, #1
	strh	r3, [r4, #418]	@ movhi
	ldrh	r3, [r4, #416]
	add	r2, r4, r3, lsl #1
	adds	r3, r3, #1
	ubfx	r3, r3, #0, #10
	strh	r5, [r2, #420]	@ movhi
	strh	r3, [r4, #416]	@ movhi
.L402:
	pop	{r4, r5, r6, pc}
.L412:
	.align	2
.L411:
	.word	.LANCHOR0
	.fnend
	.size	FtlFreeSysBlkQueueIn, .-FtlFreeSysBlkQueueIn
	.align	1
	.global	FtlFreeSysBlkQueueOut
	.syntax unified
	.thumb
	.thumb_func
	.fpu softvfp
	.type	FtlFreeSysBlkQueueOut, %function
FtlFreeSysBlkQueueOut:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	push	{r3, r4, r5, lr}
	.save {r3, r4, r5, lr}
	ldr	r4, .L418
	ldrh	r2, [r4, #418]
	cbz	r2, .L417
	ldrh	r3, [r4, #414]
	subs	r2, r2, #1
	ldr	r0, [r4, #3272]
	strh	r2, [r4, #418]	@ movhi
	movs	r2, #1
	add	r1, r4, r3, lsl #1
	adds	r3, r3, #1
	ubfx	r3, r3, #0, #10
	ldrh	r5, [r1, #420]
	mov	r1, r2
	strh	r3, [r4, #414]	@ movhi
	lsls	r3, r5, #10
	str	r3, [r0, #4]
	bl	FlashEraseBlocks
	ldr	r3, [r4, #2612]
	adds	r3, r3, #1
	str	r3, [r4, #2612]
.L414:
	subs	r3, r5, #1
	movw	r2, #65533
	uxth	r3, r3
	cmp	r3, r2
	bls	.L415
	ldrh	r2, [r4, #418]
	mov	r1, r5
	ldr	r0, .L418+4
	bl	sftl_printk
.L416:
	b	.L416
.L417:
	movw	r5, #65535
	b	.L414
.L415:
	mov	r0, r5
	pop	{r3, r4, r5, pc}
.L419:
	.align	2
.L418:
	.word	.LANCHOR0
	.word	.LC89
	.fnend
	.size	FtlFreeSysBlkQueueOut, .-FtlFreeSysBlkQueueOut
	.align	1
	.global	ftl_map_blk_alloc_new_blk
	.syntax unified
	.thumb
	.thumb_func
	.fpu softvfp
	.type	ftl_map_blk_alloc_new_blk, %function
ftl_map_blk_alloc_new_blk:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	push	{r3, r4, r5, r6, r7, lr}
	.save {r3, r4, r5, r6, r7, lr}
	mov	r4, r0
	ldrh	r1, [r0, #10]
	movs	r3, #0
	ldr	r2, [r0, #12]
.L421:
	uxth	r5, r3
	cmp	r5, r1
	bcs	.L424
	mov	r7, r2
	adds	r3, r3, #1
	ldrh	r6, [r7]
	adds	r2, r2, #2
	cmp	r6, #0
	bne	.L421
	bl	FtlFreeSysBlkQueueOut
	subs	r3, r0, #1
	movw	r2, #65533
	uxth	r3, r3
	mov	r1, r0
	strh	r0, [r7]	@ movhi
	cmp	r3, r2
	bls	.L422
	ldr	r3, .L428
	ldr	r0, .L428+4
	ldrh	r2, [r3, #418]
	bl	sftl_printk
.L423:
	b	.L423
.L422:
	ldr	r3, [r4, #28]
	strh	r6, [r4, #2]	@ movhi
	strh	r5, [r4]	@ movhi
	adds	r3, r3, #1
	str	r3, [r4, #28]
	ldrh	r3, [r4, #8]
	adds	r3, r3, #1
	strh	r3, [r4, #8]	@ movhi
.L424:
	ldrh	r3, [r4, #10]
	cmp	r3, r5
	bhi	.L426
	movw	r2, #578
	ldr	r1, .L428+8
	ldr	r0, .L428+12
	bl	sftl_printk
.L426:
	movs	r0, #0
	pop	{r3, r4, r5, r6, r7, pc}
.L429:
	.align	2
.L428:
	.word	.LANCHOR0
	.word	.LC90
	.word	.LANCHOR1+147
	.word	.LC8
	.fnend
	.size	ftl_map_blk_alloc_new_blk, .-ftl_map_blk_alloc_new_blk
	.align	1
	.global	ftl_memset
	.syntax unified
	.thumb
	.thumb_func
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
	.align	1
	.global	FtlMemInit
	.syntax unified
	.thumb
	.thumb_func
	.fpu softvfp
	.type	FtlMemInit, %function
FtlMemInit:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	push	{r4, r5, r6, r7, r8, lr}
	.save {r4, r5, r6, r7, r8, lr}
	movw	r3, #65535
	ldr	r4, .L532
	movs	r5, #0
	movs	r7, #12
	movs	r6, #20
	ldrh	r0, [r4, #306]
	str	r3, [r4, #3280]
	strh	r3, [r4, #222]	@ movhi
	strh	r3, [r4, #220]	@ movhi
	movs	r3, #32
	strh	r3, [r4, #3156]	@ movhi
	lsls	r0, r0, #1
	movs	r3, #128
	str	r5, [r4, #2592]
	strh	r3, [r4, #3158]	@ movhi
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
	strh	r5, [r4, #3164]	@ movhi
	strh	r5, [r4, #3210]	@ movhi
	strh	r5, [r4, #3246]	@ movhi
	bl	ftl_malloc
	str	r0, [r4, #3196]
	ldrh	r0, [r4, #306]
	muls	r0, r7, r0
	bl	ftl_malloc
	ldrh	r3, [r4, #232]
	str	r0, [r4, #3204]
	muls	r6, r3, r6
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
	ldrh	r6, [r4, #310]
	str	r0, [r4, #3188]
	lsls	r3, r3, #1
	mov	r0, r6
	adds	r3, r3, #1
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
	muls	r0, r6, r0
	bl	ftl_malloc
	str	r0, [r4, #3180]
	mov	r0, r6
	bl	ftl_malloc
	str	r0, [r4, #3316]
	mov	r0, r6
	bl	ftl_malloc
	str	r0, [r4, #3320]
	ldr	r0, [r4, #3192]
	muls	r0, r7, r0
	bl	ftl_malloc
	str	r0, [r4, #3176]
	mov	r0, r6
	bl	ftl_malloc
	str	r0, [r4, #3324]
	mov	r0, r6
	bl	ftl_malloc
	str	r0, [r4, #3328]
	ldrh	r0, [r4, #258]
	lsls	r0, r0, #2
	bl	ftl_malloc
	ldrh	r3, [r4, #312]
	ldrh	r6, [r4, #232]
	str	r0, [r4, #3332]
	muls	r6, r3, r6
	mov	r0, r6
	bl	ftl_malloc
	str	r0, [r4, #3336]
	lsls	r0, r6, #2
	bl	ftl_malloc
	ldrh	r3, [r4, #312]
	str	r0, [r4, #3340]
	ldr	r0, [r4, #3192]
	muls	r0, r3, r0
	bl	ftl_malloc
	str	r0, [r4, #3184]
	ldrh	r0, [r4, #242]
	lsls	r0, r0, #1
	uxth	r0, r0
	strh	r0, [r4, #3344]	@ movhi
	bl	ftl_malloc
	ldrh	r3, [r4, #3344]
	str	r0, [r4, #3348]
	ldr	r0, .L532+4
	addw	r3, r3, #547
	lsrs	r3, r3, #9
	and	r0, r0, r3, lsl #9
	strh	r3, [r4, #3344]	@ movhi
	bl	ftl_malloc
	ldrh	r6, [r4, #242]
	str	r0, [r4, #3352]
	adds	r0, r0, #32
	str	r0, [r4, #2536]
	lsls	r6, r6, #1
	mov	r0, r6
	bl	ftl_malloc
	str	r0, [r4, #3356]
	mov	r0, r6
	bl	ftl_malloc
	ldr	r6, [r4, #328]
	str	r0, [r4, #72]
	lsls	r6, r6, #1
	mov	r0, r6
	bl	ftl_malloc
	str	r0, [r4, #3360]
	mov	r0, r6
	bl	ftl_malloc
	str	r0, [r4, #3364]
	ldrh	r0, [r4, #320]
	lsls	r0, r0, #1
	bl	ftl_malloc
	str	r0, [r4, #348]
	ldrh	r0, [r4, #320]
	lsls	r0, r0, #1
	bl	ftl_malloc
	str	r0, [r4, #3368]
	ldrh	r0, [r4, #320]
	lsls	r0, r0, #2
	bl	ftl_malloc
	str	r0, [r4, #3372]
	ldrh	r0, [r4, #322]
	lsls	r0, r0, #2
	bl	ftl_malloc
	ldrh	r2, [r4, #322]
	mov	r1, r5
	str	r0, [r4, #3376]
	add	r5, r4, #380
	lsls	r2, r2, #2
	bl	ftl_memset
	ldrh	r0, [r4, #336]
	lsls	r0, r0, #2
	bl	ftl_malloc
	str	r0, [r4, #3380]
	ldr	r0, [r4, #328]
	lsls	r0, r0, #2
	bl	ftl_malloc
	str	r0, [r4, #3384]
	ldrh	r0, [r4, #338]
	muls	r0, r7, r0
	bl	ftl_malloc
	ldrh	r3, [r4, #338]
	str	r0, [r4, #2540]
	ldrh	r0, [r4, #310]
	muls	r0, r3, r0
	bl	ftl_malloc
	ldrh	r3, [r4, #242]
	str	r0, [r4, #3388]
	movs	r0, #6
	muls	r0, r3, r0
	bl	ftl_malloc
	str	r0, [r4, #2516]
	ldrh	r0, [r4, #298]
	ldrh	r3, [r4, #254]
	adds	r0, r0, #31
	asrs	r0, r0, #5
	strh	r0, [r4, #3392]	@ movhi
	muls	r0, r3, r0
	lsls	r0, r0, #2
	bl	ftl_malloc
	ldrh	r2, [r4, #3392]
	movs	r3, #1
	ldrh	r6, [r4, #254]
	str	r0, [r4, #380]
	lsls	r2, r2, #2
	mov	r1, r2
.L432:
	cmp	r3, r6
	bcc	.L433
	ldr	r2, .L532+8
	movs	r1, #0
	add	r3, r2, r3, lsl #2
	adds	r2, r2, #56
	adds	r3, r3, #24
.L434:
	cmp	r2, r3
	bne	.L435
	ldr	r3, [r4, #3360]
	cbnz	r3, .L436
.L438:
	ldr	r1, .L532+12
	ldr	r0, .L532+16
	bl	sftl_printk
	mov	r0, #-1
	pop	{r4, r5, r6, r7, r8, pc}
.L433:
	ldr	r0, [r4, #380]
	adds	r3, r3, #1
	add	r0, r0, r1
	add	r1, r1, r2
	str	r0, [r5, #4]!
	b	.L432
.L435:
	str	r1, [r3, #4]!
	b	.L434
.L533:
	.align	2
.L532:
	.word	.LANCHOR0
	.word	33553920
	.word	.LANCHOR0+352
	.word	.LANCHOR1+173
	.word	.LC91
.L436:
	ldr	r3, [r4, #3364]
	cmp	r3, #0
	beq	.L438
	ldr	r3, [r4, #3380]
	cmp	r3, #0
	beq	.L438
	ldr	r3, [r4, #3384]
	cmp	r3, #0
	beq	.L438
	ldr	r3, [r4, #2540]
	cmp	r3, #0
	beq	.L438
	ldr	r3, [r4, #3388]
	cmp	r3, #0
	beq	.L438
	ldr	r3, [r4, #2516]
	cmp	r3, #0
	beq	.L438
	ldr	r3, [r4, #380]
	cmp	r3, #0
	beq	.L438
	ldr	r3, [r4, #72]
	cmp	r3, #0
	beq	.L438
	ldr	r3, [r4, #3196]
	cmp	r3, #0
	beq	.L438
	ldr	r3, [r4, #3204]
	cmp	r3, #0
	beq	.L438
	ldr	r3, [r4, #3292]
	cmp	r3, #0
	beq	.L438
	ldr	r3, [r4, #3300]
	cmp	r3, #0
	beq	.L438
	ldr	r3, [r4, #3272]
	cmp	r3, #0
	beq	.L438
	ldr	r3, [r4, #3188]
	cmp	r3, #0
	beq	.L438
	ldr	r3, [r4, #3296]
	cmp	r3, #0
	beq	.L438
	ldr	r3, [r4, #3304]
	cmp	r3, #0
	beq	.L438
	ldr	r3, [r4, #3308]
	cmp	r3, #0
	beq	.L438
	ldr	r3, [r4, #3312]
	cmp	r3, #0
	beq	.L438
	ldr	r3, [r4, #3180]
	cmp	r3, #0
	beq	.L438
	ldr	r3, [r4, #3316]
	cmp	r3, #0
	beq	.L438
	ldr	r3, [r4, #3320]
	cmp	r3, #0
	beq	.L438
	ldr	r3, [r4, #3176]
	cmp	r3, #0
	beq	.L438
	ldr	r3, [r4, #3336]
	cmp	r3, #0
	beq	.L438
	ldr	r3, [r4, #3340]
	cmp	r3, #0
	beq	.L438
	ldr	r3, [r4, #3184]
	cmp	r3, #0
	beq	.L438
	ldr	r3, [r4, #2536]
	cmp	r3, #0
	beq	.L438
	ldr	r3, [r4, #3348]
	cmp	r3, #0
	beq	.L438
	ldr	r3, [r4, #348]
	cmp	r3, #0
	beq	.L438
	ldr	r3, .L534
	ldr	r2, [r3, #3368]
	cmp	r2, #0
	beq	.L438
	ldr	r2, [r3, #3372]
	cmp	r2, #0
	beq	.L438
	ldr	r3, [r3, #3376]
	cmp	r3, #0
	beq	.L438
	movs	r0, #0
	pop	{r4, r5, r6, r7, r8, pc}
.L535:
	.align	2
.L534:
	.word	.LANCHOR0
	.fnend
	.size	FtlMemInit, .-FtlMemInit
	.align	1
	.global	FtlBbt2Bitmap
	.syntax unified
	.thumb
	.thumb_func
	.fpu softvfp
	.type	FtlBbt2Bitmap, %function
FtlBbt2Bitmap:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	push	{r4, r5, r6, r7, r8, r9, r10, lr}
	.save {r4, r5, r6, r7, r8, r9, r10, lr}
	mov	r5, r0
	ldr	r6, .L542
	mov	r7, r1
	subs	r4, r5, #2
	movw	r8, #65535
	ldr	r9, .L542+4
	addw	r5, r5, #1022
	ldrh	r2, [r6, #3392]
	movs	r1, #0
	ldr	r10, .L542+8
	mov	r0, r7
	lsls	r2, r2, #2
	bl	ftl_memset
.L539:
	ldrh	r3, [r4, #2]
	cmp	r3, r8
	beq	.L536
	ldrh	r2, [r6, #298]
	cmp	r2, r3
	bhi	.L538
	movs	r2, #74
	mov	r1, r9
	mov	r0, r10
	bl	sftl_printk
.L538:
	ldrh	r3, [r4, #2]!
	movs	r2, #1
	cmp	r5, r4
	lsr	r1, r3, #5
	and	r3, r3, #31
	lsl	r3, r2, r3
	ldr	r2, [r7, r1, lsl #2]
	orr	r2, r2, r3
	str	r2, [r7, r1, lsl #2]
	bne	.L539
.L536:
	pop	{r4, r5, r6, r7, r8, r9, r10, pc}
.L543:
	.align	2
.L542:
	.word	.LANCHOR0
	.word	.LANCHOR1+184
	.word	.LC8
	.fnend
	.size	FtlBbt2Bitmap, .-FtlBbt2Bitmap
	.align	1
	.global	FtlBbtMemInit
	.syntax unified
	.thumb
	.thumb_func
	.fpu softvfp
	.type	FtlBbtMemInit, %function
FtlBbtMemInit:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	@ link register save eliminated.
	ldr	r0, .L545
	movw	r3, #65535
	movs	r2, #16
	movs	r1, #255
	strh	r3, [r0, #352]	@ movhi
	movs	r3, #0
	strh	r3, [r0, #358]	@ movhi
	add	r0, r0, #364
	b	ftl_memset
.L546:
	.align	2
.L545:
	.word	.LANCHOR0
	.fnend
	.size	FtlBbtMemInit, .-FtlBbtMemInit
	.align	1
	.global	FtlFreeSysBlkQueueInit
	.syntax unified
	.thumb
	.thumb_func
	.fpu softvfp
	.type	FtlFreeSysBlkQueueInit, %function
FtlFreeSysBlkQueueInit:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	ldr	r3, .L548
	mov	r2, #2048
	push	{r4, lr}
	.save {r4, lr}
	movs	r4, #0
	mov	r1, r4
	strh	r0, [r3, #412]	@ movhi
	add	r0, r3, #420
	strh	r4, [r3, #414]	@ movhi
	strh	r4, [r3, #416]	@ movhi
	strh	r4, [r3, #418]	@ movhi
	bl	ftl_memset
	mov	r0, r4
	pop	{r4, pc}
.L549:
	.align	2
.L548:
	.word	.LANCHOR0
	.fnend
	.size	FtlFreeSysBlkQueueInit, .-FtlFreeSysBlkQueueInit
	.align	1
	.global	ftl_free_no_use_map_blk
	.syntax unified
	.thumb
	.thumb_func
	.fpu softvfp
	.type	ftl_free_no_use_map_blk, %function
ftl_free_no_use_map_blk:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	push	{r3, r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.save {r3, r4, r5, r6, r7, r8, r9, r10, fp, lr}
	movs	r1, #0
	ldrh	r2, [r0, #10]
	mov	r4, r0
	ldr	r5, [r0, #20]
	ldr	r7, [r0, #12]
	ldr	r6, [r0, #24]
	lsls	r2, r2, #1
	mov	r0, r5
	bl	ftl_memset
	movs	r3, #0
.L551:
	ldrh	r1, [r4, #6]
	uxth	r2, r3
	cmp	r1, r2
	bhi	.L555
	ldr	r3, .L570
	movs	r6, #0
	mov	r9, r6
	mov	fp, r6
	ldrh	r2, [r3, #304]
	ldrh	r3, [r4]
	strh	r2, [r5, r3, lsl #1]	@ movhi
	ldrh	r10, [r5]
.L556:
	ldrh	r3, [r4, #10]
	uxth	r1, r6
	cmp	r3, r1
	bhi	.L560
	mov	r0, r9
	pop	{r3, r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L555:
	uxth	r2, r3
	ldr	r1, [r6, r2, lsl #2]
	movs	r2, #0
	ubfx	r1, r1, #10, #16
.L552:
	ldrh	ip, [r4, #10]
	uxth	r0, r2
	cmp	ip, r0
	bhi	.L554
	adds	r3, r3, #1
	b	.L551
.L554:
	uxth	r0, r2
	ldrh	ip, [r7, r0, lsl #1]
	cmp	ip, r1
	bne	.L553
	cbz	r1, .L553
	ldrh	ip, [r5, r0, lsl #1]
	add	ip, ip, #1
	strh	ip, [r5, r0, lsl #1]	@ movhi
.L553:
	adds	r2, r2, #1
	b	.L552
.L560:
	uxth	r2, r6
	ldrh	r3, [r5, r2, lsl #1]
	lsl	r8, r2, #1
	cmp	r10, r3
	bls	.L557
	ldrh	r0, [r7, r2, lsl #1]
	add	r8, r8, r7
	cbnz	r0, .L558
.L559:
	adds	r6, r6, #1
	b	.L556
.L557:
	cmp	r3, #0
	bne	.L559
	ldrh	r0, [r7, r2, lsl #1]
	add	r8, r8, r7
	cmp	r0, #0
	beq	.L559
.L561:
	movs	r1, #1
	bl	FtlFreeSysBlkQueueIn
	strh	fp, [r8]	@ movhi
	ldrh	r3, [r4, #8]
	subs	r3, r3, #1
	strh	r3, [r4, #8]	@ movhi
	b	.L559
.L558:
	mov	r9, r1
	mov	r10, r3
	cmp	r3, #0
	beq	.L561
	b	.L559
.L571:
	.align	2
.L570:
	.word	.LANCHOR0
	.fnend
	.size	ftl_free_no_use_map_blk, .-ftl_free_no_use_map_blk
	.align	1
	.global	FtlL2PDataInit
	.syntax unified
	.thumb
	.thumb_func
	.fpu softvfp
	.type	FtlL2PDataInit, %function
FtlL2PDataInit:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	push	{r3, r4, r5, r6, r7, lr}
	.save {r3, r4, r5, r6, r7, lr}
	movs	r1, #0
	ldr	r4, .L575
	movs	r6, #12
	ldr	r2, [r4, #328]
	ldr	r0, [r4, #3364]
	lsls	r2, r2, #1
	bl	ftl_memset
	ldrh	r3, [r4, #310]
	movs	r1, #255
	ldrh	r2, [r4, #338]
	ldr	r0, [r4, #3388]
	muls	r2, r3, r2
	bl	ftl_memset
	movs	r2, #0
	mov	r3, r4
	movw	r1, #65535
	mov	r7, r2
.L573:
	ldrh	r4, [r3, #338]
	uxth	r0, r2
	adds	r5, r2, #1
	cmp	r4, r0
	bhi	.L574
	ldr	r2, [r3, #328]
	strh	r1, [r3, #3398]	@ movhi
	strh	r1, [r3, #3396]	@ movhi
	strh	r2, [r3, #3406]	@ movhi
	movw	r2, #61634
	strh	r2, [r3, #3400]	@ movhi
	ldrh	r2, [r3, #3440]
	strh	r2, [r3, #3404]	@ movhi
	ldrh	r2, [r3, #336]
	strh	r2, [r3, #3402]	@ movhi
	ldr	r2, [r3, #3360]
	str	r2, [r3, #3408]
	ldr	r2, [r3, #3384]
	str	r2, [r3, #3412]
	ldr	r2, [r3, #3364]
	str	r2, [r3, #3416]
	ldr	r2, [r3, #3380]
	str	r2, [r3, #3420]
	pop	{r3, r4, r5, r6, r7, pc}
.L574:
	uxth	r2, r2
	ldr	r0, [r3, #2540]
	mul	r4, r6, r2
	add	ip, r0, r4
	str	r7, [ip, #4]
	strh	r1, [r0, r4]	@ movhi
	ldr	r0, [r3, #2540]
	add	r0, r0, r4
	ldrh	r4, [r3, #310]
	muls	r2, r4, r2
	ldr	r4, [r3, #3388]
	bic	r2, r2, #3
	add	r2, r2, r4
	str	r2, [r0, #8]
	mov	r2, r5
	b	.L573
.L576:
	.align	2
.L575:
	.word	.LANCHOR0
	.fnend
	.size	FtlL2PDataInit, .-FtlL2PDataInit
	.align	1
	.global	FtlVariablesInit
	.syntax unified
	.thumb
	.thumb_func
	.fpu softvfp
	.type	FtlVariablesInit, %function
FtlVariablesInit:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	push	{r3, r4, r5, lr}
	.save {r3, r4, r5, lr}
	movs	r5, #0
	ldr	r4, .L578
	movw	r3, #65535
	mov	r1, r5
	ldrh	r2, [r4, #320]
	strh	r3, [r4, #3442]	@ movhi
	mov	r3, #-1
	ldr	r0, [r4, #348]
	str	r3, [r4, #3452]
	lsls	r2, r2, #1
	str	r5, [r4, #3444]
	str	r5, [r4, #3448]
	strh	r5, [r4, #344]	@ movhi
	bl	ftl_memset
	ldrh	r2, [r4, #242]
	mov	r1, r5
	ldr	r0, [r4, #2536]
	lsls	r2, r2, #1
	bl	ftl_memset
	ldrh	r2, [r4, #242]
	mov	r1, r5
	ldr	r0, [r4, #3348]
	lsls	r2, r2, #1
	bl	ftl_memset
	mov	r1, r5
	movs	r2, #48
	addw	r0, r4, #2468
	bl	ftl_memset
	mov	r2, #512
	mov	r1, r5
	addw	r0, r4, #2644
	bl	ftl_memset
	bl	FtlGcBufInit
	bl	FtlL2PDataInit
	mov	r0, r5
	pop	{r3, r4, r5, pc}
.L579:
	.align	2
.L578:
	.word	.LANCHOR0
	.fnend
	.size	FtlVariablesInit, .-FtlVariablesInit
	.align	1
	.global	SupperBlkListInit
	.syntax unified
	.thumb
	.thumb_func
	.fpu softvfp
	.type	SupperBlkListInit, %function
SupperBlkListInit:
	.fnstart
	@ args = 0, pretend = 0, frame = 16
	@ frame_needed = 0, uses_anonymous_args = 0
	push	{r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.save {r4, r5, r6, r7, r8, r9, r10, fp, lr}
	movs	r5, #0
	ldr	r4, .L591
	movs	r2, #6
	mov	r9, r5
	mov	r6, r5
	movw	fp, #65535
	.pad #20
	sub	sp, sp, #20
	ldrh	r3, [r4, #242]
	add	r10, r4, #260
	movs	r1, #0
	ldr	r0, [r4, #2516]
	muls	r2, r3, r2
	bl	ftl_memset
	str	r5, [r4, #2532]
	str	r5, [r4, #2520]
	str	r5, [r4, #2524]
	strh	r5, [r4, #2528]	@ movhi
	strh	r5, [r4, #224]	@ movhi
.L581:
	ldrh	r3, [r4, #240]
	uxth	r8, r5
	cmp	r8, r3
	bcs	.L588
	ldrh	r3, [r4, #302]
	ldrh	r2, [r4, #232]
	str	r3, [sp, #4]
	movs	r3, #0
	mov	r7, r3
	b	.L589
.L583:
	ldrb	r0, [r10, r3]	@ zero_extendqisi2
	mov	r1, r8
	str	r2, [sp, #12]
	str	r3, [sp, #8]
	bl	V2P_block
	bl	FtlBbmIsBadBlock
	ldr	r3, [sp, #8]
	ldr	r2, [sp, #12]
	cbnz	r0, .L582
	ldr	r1, [sp, #4]
	add	r7, r7, r1
	uxth	r7, r7
.L582:
	adds	r3, r3, #1
.L589:
	uxth	r1, r3
	cmp	r2, r1
	bhi	.L583
	uxth	r3, r5
	cbz	r7, .L584
	mov	r1, r7
	str	r3, [sp, #4]
	mov	r0, #32768
	bl	__aeabi_idiv
	ldr	r3, [sp, #4]
	uxth	r7, r0
.L585:
	ldr	r1, [r4, #2516]
	movs	r2, #6
	mla	r2, r2, r3, r1
	strh	r7, [r2, #4]	@ movhi
	ldrh	r2, [r4, #24]
	cmp	r2, r8
	beq	.L586
	ldrh	r2, [r4, #76]
	cmp	r2, r8
	beq	.L586
	ldrh	r2, [r4, #124]
	cmp	r2, r8
	beq	.L586
	ldr	r2, [r4, #72]
	ldrh	r3, [r2, r3, lsl #1]
	cbnz	r3, .L587
	add	r9, r9, #1
	mov	r0, r8
	uxth	r9, r9
	bl	INSERT_FREE_LIST
.L586:
	adds	r5, r5, #1
	b	.L581
.L584:
	ldr	r2, [r4, #72]
	strh	fp, [r2, r3, lsl #1]	@ movhi
	b	.L585
.L587:
	adds	r6, r6, #1
	mov	r0, r8
	uxth	r6, r6
	bl	INSERT_DATA_LIST
	b	.L586
.L588:
	strh	r6, [r4, #2528]	@ movhi
	add	r6, r6, r9
	cmp	r6, r3
	strh	r9, [r4, #224]	@ movhi
	ble	.L590
	movw	r2, #2212
	ldr	r1, .L591+4
	ldr	r0, .L591+8
	bl	sftl_printk
.L590:
	movs	r0, #0
	add	sp, sp, #20
	@ sp needed
	pop	{r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L592:
	.align	2
.L591:
	.word	.LANCHOR0
	.word	.LANCHOR1+198
	.word	.LC8
	.fnend
	.size	SupperBlkListInit, .-SupperBlkListInit
	.align	1
	.global	FtlGcPageVarInit
	.syntax unified
	.thumb
	.thumb_func
	.fpu softvfp
	.type	FtlGcPageVarInit, %function
FtlGcPageVarInit:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	push	{r4, lr}
	.save {r4, lr}
	movs	r3, #0
	ldr	r4, .L594
	movs	r1, #255
	ldrh	r2, [r4, #306]
	ldr	r0, [r4, #3196]
	strh	r3, [r4, #3200]	@ movhi
	strh	r3, [r4, #3208]	@ movhi
	lsls	r2, r2, #1
	bl	ftl_memset
	ldrh	r3, [r4, #306]
	movs	r2, #12
	ldr	r0, [r4, #3204]
	movs	r1, #255
	muls	r2, r3, r2
	bl	ftl_memset
	pop	{r4, lr}
	b	FtlGcBufInit
.L595:
	.align	2
.L594:
	.word	.LANCHOR0
	.fnend
	.size	FtlGcPageVarInit, .-FtlGcPageVarInit
	.align	1
	.global	FlashGetBadBlockList
	.syntax unified
	.thumb
	.thumb_func
	.fpu softvfp
	.type	FlashGetBadBlockList, %function
FlashGetBadBlockList:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	push	{r4, r5, r6, lr}
	.save {r4, r5, r6, lr}
	mov	r2, #256
	ldr	r5, .L603
	mov	r4, r0
	mov	r6, r1
	movs	r1, #255
	bl	ftl_memset
	ldr	r3, [r5, #3248]
	mov	r1, r6
	mov	r0, r4
	blx	r3
	uxth	r0, r0
	cmp	r0, #50
	bls	.L597
	mov	r2, #256
	movs	r1, #255
	mov	r0, r4
	bl	ftl_memset
	movs	r0, #0
.L597:
	ldrh	r3, [r5, #14]
	cmp	r3, #4
	bne	.L602
	add	r1, r4, r0, lsl #1
	mov	r3, r4
.L599:
	cmp	r3, r1
	bne	.L600
.L602:
	pop	{r4, r5, r6, pc}
.L600:
	ldrh	r2, [r3]
	lsrs	r2, r2, #1
	strh	r2, [r3], #2	@ movhi
	b	.L599
.L604:
	.align	2
.L603:
	.word	.LANCHOR0
	.fnend
	.size	FlashGetBadBlockList, .-FlashGetBadBlockList
	.align	1
	.global	ftl_memcpy
	.syntax unified
	.thumb
	.thumb_func
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
	.align	1
	.global	FlashReadPages
	.syntax unified
	.thumb
	.thumb_func
	.fpu softvfp
	.type	FlashReadPages, %function
FlashReadPages:
	.fnstart
	@ args = 0, pretend = 0, frame = 24
	@ frame_needed = 0, uses_anonymous_args = 0
	push	{r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.save {r4, r5, r6, r7, r8, r9, r10, fp, lr}
	mov	r9, r1
	ldr	r5, .L647
	mov	r4, r0
	mov	r8, #0
	.pad #28
	sub	sp, sp, #28
	ldr	r7, .L647+4
	ldrh	r10, [r5, #12]
	ldr	fp, .L647+24
	ldr	r2, [r7]
	str	r7, [sp, #4]
	str	r2, [sp, #20]
.L607:
	cmp	r8, r9
	bne	.L621
	ldr	r3, [sp, #4]
	movs	r0, #0
	ldr	r2, [sp, #20]
	ldr	r3, [r3]
	cmp	r2, r3
	beq	.L622
	bl	__stack_chk_fail
.L621:
	ldr	r3, [r4, #8]
	cbz	r3, .L608
	ldr	r3, [r4, #12]
	cbnz	r3, .L609
.L608:
	movs	r2, #96
	ldr	r1, .L647+8
	ldr	r0, .L647+12
	bl	sftl_printk
.L609:
	add	r2, sp, #12
	add	r1, sp, #16
	mov	r0, r4
	bl	l2p_addr_tran
	ldr	r0, [sp, #12]
	cmp	r0, #3
	bls	.L610
	mov	r3, #-1
	str	r3, [r4]
.L611:
	add	r8, r8, #1
	adds	r4, r4, #20
	b	.L607
.L610:
	ldr	r6, [r4, #8]
	uxtb	r0, r0
	ldr	r3, [r4, #12]
	ldr	r1, [sp, #16]
	lsls	r7, r6, #26
	ldr	r7, [r5, #3260]
	it	ne
	ldrne	r6, [r5, #3324]
	mov	r2, r6
	blx	r7
	str	r0, [r4]
	ldrh	r3, [r5, #14]
	cmp	r3, #4
	bne	.L614
	ldr	r3, [r4, #12]
	add	r2, r6, #2048
	ldr	r1, [sp, #16]
	ldrb	r0, [sp, #12]	@ zero_extendqisi2
	adds	r3, r3, #8
	ldr	r7, [r5, #3260]
	add	r1, r1, r10
	blx	r7
	adds	r1, r0, #1
	beq	.L615
	ldr	r3, [r4, #12]
	ldr	r2, [r3, #12]
	adds	r2, r2, #1
	bne	.L616
	ldr	r2, [r3, #8]
	adds	r2, r2, #1
	bne	.L616
	ldr	r3, [r3]
	adds	r3, r3, #1
	beq	.L616
.L615:
	mov	r3, #-1
	str	r3, [r4]
.L616:
	ldr	r3, [r4]
	adds	r3, r3, #1
	beq	.L617
	cmp	r0, #256
	it	eq
	streq	r0, [r4]
.L617:
	ldr	r3, [r4]
	adds	r2, r3, #1
	beq	.L618
	cmp	r3, #256
	bne	.L614
.L618:
	ldr	r1, [r4, #4]
	mov	r0, fp
	ldr	r2, [sp, #16]
	bl	sftl_printk
	ldr	r1, [r4, #8]
	cbz	r1, .L620
	movs	r3, #4
	ldr	r0, .L647+16
	mov	r2, r3
	bl	rknand_print_hex
.L620:
	ldr	r1, [r4, #12]
	cbz	r1, .L614
	movs	r3, #4
	ldr	r0, .L647+20
	mov	r2, r3
	bl	rknand_print_hex
.L614:
	ldr	r3, [r5, #3324]
	cmp	r6, r3
	bne	.L611
	ldr	r0, [r4, #8]
	cmp	r6, r0
	beq	.L611
	ldrh	r2, [r5, #258]
	mov	r1, r6
	lsls	r2, r2, #9
	bl	ftl_memcpy
	b	.L611
.L622:
	add	sp, sp, #28
	@ sp needed
	pop	{r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L648:
	.align	2
.L647:
	.word	.LANCHOR0
	.word	__stack_chk_guard
	.word	.LANCHOR1+216
	.word	.LC8
	.word	.LC93
	.word	.LC94
	.word	.LC92
	.fnend
	.size	FlashReadPages, .-FlashReadPages
	.align	1
	.global	FtlLoadFactoryBbt
	.syntax unified
	.thumb
	.thumb_func
	.fpu softvfp
	.type	FtlLoadFactoryBbt, %function
FtlLoadFactoryBbt:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	push	{r4, r5, r6, r7, r8, r9, r10, lr}
	.save {r4, r5, r6, r7, r8, r9, r10, lr}
	movs	r6, #0
	ldr	r4, .L659
	movw	r10, #61664
	ldr	r3, [r4, #3304]
	add	r7, r4, #362
	ldr	r8, [r4, #3336]
	add	r9, r4, #3456
	str	r3, [r4, #3464]
	str	r8, [r4, #3468]
.L650:
	ldrh	r3, [r4, #254]
	cmp	r6, r3
	bcc	.L655
	movs	r0, #0
	pop	{r4, r5, r6, r7, r8, r9, r10, pc}
.L655:
	ldrh	r5, [r4, #298]
	movw	r3, #65535
	strh	r3, [r7, #2]!	@ movhi
.L652:
	ldrh	r3, [r4, #298]
	subs	r5, r5, #1
	uxth	r5, r5
	sub	r2, r3, #16
	cmp	r5, r2
	ble	.L653
	mla	r3, r6, r3, r5
	movs	r2, #1
	mov	r1, r2
	mov	r0, r9
	lsls	r3, r3, #10
	str	r3, [r4, #3460]
	bl	FlashReadPages
	ldr	r3, [r4, #3456]
	adds	r3, r3, #1
	beq	.L652
	ldrh	r3, [r8]
	cmp	r3, r10
	bne	.L652
	strh	r5, [r7]	@ movhi
.L653:
	adds	r6, r6, #1
	b	.L650
.L660:
	.align	2
.L659:
	.word	.LANCHOR0
	.fnend
	.size	FtlLoadFactoryBbt, .-FtlLoadFactoryBbt
	.align	1
	.global	FtlGetLastWrittenPage
	.syntax unified
	.thumb
	.thumb_func
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
	ldr	r6, .L673
	cmp	r1, #1
	lsl	r8, r0, #10
	mov	r2, r1
	mov	r7, r1
	ldr	r3, [r6]
	mov	r1, #1
	mov	r0, sp
	str	r3, [sp, #84]
	ldr	r3, .L673+4
	ite	eq
	ldrheq	r5, [r3, #304]
	ldrhne	r5, [r3, #302]
	ldr	r3, [r3, #3328]
	subs	r5, r5, #1
	sxth	r5, r5
	str	r3, [sp, #8]
	add	r3, sp, #20
	str	r3, [sp, #12]
	orr	r3, r5, r8
	str	r3, [sp, #4]
	bl	FlashReadPages
	ldr	r3, [sp, #20]
	adds	r3, r3, #1
	bne	.L664
	mov	r9, #0
.L665:
	cmp	r9, r5
	ble	.L668
.L664:
	ldr	r2, [sp, #84]
	mov	r0, r5
	ldr	r3, [r6]
	cmp	r2, r3
	beq	.L669
	bl	__stack_chk_fail
.L668:
	add	r3, r9, r5
	mov	r2, r7
	add	r3, r3, r3, lsr #31
	movs	r1, #1
	mov	r0, sp
	asrs	r4, r3, #1
	sxth	r3, r4
	orr	r3, r3, r8
	str	r3, [sp, #4]
	bl	FlashReadPages
	ldr	r3, [sp, #20]
	adds	r3, r3, #1
	bne	.L666
	ldr	r3, [sp, #24]
	adds	r3, r3, #1
	bne	.L666
	ldr	r3, [sp]
	adds	r3, r3, #1
	beq	.L666
	subs	r4, r4, #1
	sxth	r5, r4
	b	.L665
.L666:
	adds	r4, r4, #1
	sxth	r9, r4
	b	.L665
.L669:
	add	sp, sp, #92
	@ sp needed
	pop	{r4, r5, r6, r7, r8, r9, pc}
.L674:
	.align	2
.L673:
	.word	__stack_chk_guard
	.word	.LANCHOR0
	.fnend
	.size	FtlGetLastWrittenPage, .-FtlGetLastWrittenPage
	.align	1
	.global	FtlScanSysBlk
	.syntax unified
	.thumb
	.thumb_func
	.fpu softvfp
	.type	FtlScanSysBlk, %function
FtlScanSysBlk:
	.fnstart
	@ args = 0, pretend = 0, frame = 16
	@ frame_needed = 0, uses_anonymous_args = 0
	push	{r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.save {r4, r5, r6, r7, r8, r9, r10, fp, lr}
	movs	r5, #0
	ldr	r4, .L757
	mov	r1, r5
	.pad #20
	sub	sp, sp, #20
	ldr	r2, [r4, #328]
	ldr	r0, [r4, #3384]
	strh	r5, [r4, #3440]	@ movhi
	strh	r5, [r4, #344]	@ movhi
	lsls	r2, r2, #2
	bl	ftl_memset
	ldr	r2, [r4, #328]
	mov	r1, r5
	ldr	r0, [r4, #3360]
	lsls	r2, r2, #1
	bl	ftl_memset
	ldrh	r2, [r4, #320]
	mov	r1, r5
	ldr	r0, [r4, #3372]
	lsls	r2, r2, #2
	bl	ftl_memset
	ldrh	r2, [r4, #320]
	mov	r1, r5
	ldr	r0, [r4, #348]
	lsls	r2, r2, #1
	bl	ftl_memset
	movs	r2, #16
	movs	r1, #255
	addw	r0, r4, #2628
	bl	ftl_memset
	ldrh	r8, [r4, #240]
.L676:
	ldrh	r3, [r4, #242]
	cmp	r3, r8
	bls	.L718
	movs	r5, #0
	ldrh	fp, [r4, #232]
	ldr	r9, [r4, #3292]
	mov	r7, r5
	ldr	r3, [r4, #3180]
	mov	r10, #20
	ldr	r2, [r4, #3184]
	ldrh	r6, [r4, #312]
	b	.L719
.L679:
	str	r3, [sp, #8]
	mov	r1, r8
	ldr	r3, .L757+4
	str	r2, [sp, #12]
	ldrb	r0, [r3, r5]	@ zero_extendqisi2
	bl	V2P_block
	str	r0, [sp, #4]
	bl	FtlBbmIsBadBlock
	ldr	r3, [sp, #8]
	ldr	r2, [sp, #12]
	cbnz	r0, .L677
	ldr	r1, [sp, #4]
	mla	r0, r10, r7, r9
	lsls	r1, r1, #10
	str	r3, [r0, #8]
	str	r1, [r0, #4]
	mov	r1, r6
	muls	r1, r7, r1
	add	r7, r7, #1
	it	mi
	addmi	r1, r1, #3
	uxth	r7, r7
	bic	r1, r1, #3
	add	r1, r1, r2
	str	r1, [r0, #12]
.L677:
	adds	r5, r5, #1
.L719:
	uxth	r1, r5
	cmp	fp, r1
	bhi	.L679
	cbnz	r7, .L680
.L717:
	add	r8, r8, #1
	uxth	r8, r8
	b	.L676
.L680:
	mov	r0, r9
	mov	r9, #0
	movs	r2, #1
	mov	r1, r7
	bl	FlashReadPages
.L681:
	uxth	r3, r9
	cmp	r7, r3
	bls	.L717
	movs	r3, #20
	mul	r10, r3, r9
	ldr	r3, [r4, #3292]
	add	r2, r3, r10
	ldr	r3, [r3, r10]
	ldr	r5, [r2, #4]
	ldr	r6, [r2, #12]
	adds	r3, r3, #1
	ubfx	r5, r5, #10, #16
	bne	.L684
	mov	fp, #16
	movw	r3, #65535
.L686:
	ldr	r0, [r4, #3292]
	str	r3, [sp, #4]
	add	r0, r0, r10
	ldr	r2, [r0, #4]
	adds	r2, r2, #1
	str	r2, [r0, #4]
	movs	r2, #1
	mov	r1, r2
	bl	FlashReadPages
	ldrh	r2, [r6]
	ldr	r3, [sp, #4]
	cmp	r2, r3
	bne	.L683
	ldr	r3, [r4, #3292]
	mov	r2, #-1
	str	r2, [r3, r10]
	ldr	r3, [r4, #3292]
	ldr	r3, [r3, r10]
	cmp	r3, r2
	bne	.L684
.L685:
	movs	r1, #1
	b	.L756
.L683:
	ldr	r2, [r4, #3292]
	ldr	r2, [r2, r10]
	adds	r2, r2, #1
	bne	.L684
	add	fp, fp, #-1
	uxth	fp, fp
	cmp	fp, #0
	bne	.L686
	b	.L685
.L684:
	ldr	r2, [r4, #2592]
	ldr	r3, [r6, #4]
	adds	r1, r2, #1
	beq	.L687
	cmp	r2, r3
	bhi	.L688
.L687:
	adds	r2, r3, #1
	itt	ne
	addne	r2, r3, #1
	strne	r2, [r4, #2592]
.L688:
	ldrh	r2, [r6]
	movw	r1, #61604
	cmp	r2, r1
	beq	.L690
	bhi	.L691
	movw	r3, #61574
	cmp	r2, r3
	beq	.L692
.L689:
	add	r9, r9, #1
	b	.L681
.L691:
	movw	r3, #61634
	cmp	r2, r3
	beq	.L693
	movw	r3, #65535
	cmp	r2, r3
	bne	.L689
	movs	r1, #0
.L756:
	mov	r0, r5
	bl	FtlFreeSysBlkQueueIn
	b	.L689
.L693:
	ldrh	r2, [r4, #3440]
	ldr	r3, [r4, #328]
	cmp	r2, r3
	bls	.L695
	movw	r2, #1225
	ldr	r1, .L757+8
	ldr	r0, .L757+12
	bl	sftl_printk
.L695:
	ldr	r1, [r4, #328]
	ldrh	r0, [r4, #3440]
	ldr	r10, [r4, #3384]
	uxth	ip, r1
	add	r3, ip, #-1
	sub	ip, ip, r0
	add	ip, ip, #-1
	sxth	r3, r3
	sxth	ip, ip
.L696:
	cmp	r3, ip
	bgt	.L702
	cmp	r3, #0
	bge	.L734
	b	.L689
.L702:
	ldr	r2, [r6, #4]
	lsl	fp, r3, #2
	mov	lr, r2
	ldr	r2, [r10, r3, lsl #2]
	cmp	lr, r2
	bls	.L697
	ldr	r2, [r10]
	cbnz	r2, .L698
	cmp	r1, r0
	itt	ne
	addne	r0, r0, #1
	strhne	r0, [r4, #3440]	@ movhi
.L698:
	uxth	ip, r3
	movs	r2, #0
.L699:
	uxth	r0, r2
	sxth	r1, r2
	cmp	ip, r0
	bhi	.L700
	ldr	r2, [r4, #3384]
	cmp	r3, #0
	ldr	r1, [r6, #4]
	str	r1, [r2, fp]
	ldr	r2, [r4, #3360]
	strh	r5, [r2, r3, lsl #1]	@ movhi
	blt	.L689
	ldrh	r0, [r4, #3440]
	ldr	r2, [r4, #328]
	subs	r2, r2, r0
	subs	r2, r2, #1
	sxth	r2, r2
	cmp	r3, r2
	bgt	.L689
.L734:
	ldr	r2, [r4, #3384]
	adds	r0, r0, #1
	ldr	r1, [r6, #4]
	strh	r0, [r4, #3440]	@ movhi
	str	r1, [r2, r3, lsl #2]
	ldr	r2, [r4, #3360]
.L754:
	strh	r5, [r2, r3, lsl #1]	@ movhi
	b	.L689
.L700:
	ldr	r0, [r4, #3384]
	adds	r2, r2, #1
	add	lr, r0, r1, lsl #2
	ldr	lr, [lr, #4]
	str	lr, [r0, r1, lsl #2]
	ldr	r0, [r4, #3360]
	add	lr, r0, r1, lsl #1
	ldrh	lr, [lr, #2]
	strh	lr, [r0, r1, lsl #1]	@ movhi
	b	.L699
.L697:
	subs	r3, r3, #1
	sxth	r3, r3
	b	.L696
.L758:
	.align	2
.L757:
	.word	.LANCHOR0
	.word	.LANCHOR0+260
	.word	.LANCHOR1+231
	.word	.LC8
.L692:
	ldrh	r2, [r4, #344]
	ldrh	r3, [r4, #320]
	cmp	r2, r3
	bls	.L705
	movw	r2, #1266
	ldr	r1, .L759
	ldr	r0, .L759+4
	bl	sftl_printk
.L705:
	ldrh	lr, [r4, #320]
	ldrh	r0, [r4, #344]
	ldr	r10, [r4, #3372]
	add	ip, lr, #-1
	sxth	r3, ip
	sub	ip, ip, r0
.L706:
	cmp	r3, ip
	ble	.L711
	ldr	r2, [r6, #4]
	lsl	fp, r3, #2
	ldr	r1, [r10, r3, lsl #2]
	cmp	r2, r1
	bls	.L707
	ldr	r2, [r10]
	cbnz	r2, .L708
	cmp	lr, r0
	itt	ne
	addne	r0, r0, #1
	strhne	r0, [r4, #344]	@ movhi
.L708:
	uxth	ip, r3
	movs	r2, #0
.L709:
	uxth	r0, r2
	sxth	r1, r2
	cmp	ip, r0
	bhi	.L710
	ldr	r2, [r4, #3372]
	ldr	r1, [r6, #4]
	str	r1, [r2, fp]
	ldr	r2, [r4, #348]
	strh	r5, [r2, r3, lsl #1]	@ movhi
.L711:
	cmp	r3, #0
	blt	.L689
	ldrh	r2, [r4, #320]
	ldrh	r1, [r4, #344]
	subs	r2, r2, #1
	subs	r2, r2, r1
	sxth	r2, r2
	cmp	r3, r2
	bgt	.L689
	adds	r1, r1, #1
	ldr	r2, [r4, #3372]
	strh	r1, [r4, #344]	@ movhi
	ldr	r1, [r6, #4]
	str	r1, [r2, r3, lsl #2]
	ldr	r2, [r4, #348]
	b	.L754
.L710:
	ldr	r0, [r4, #3372]
	adds	r2, r2, #1
	add	lr, r0, r1, lsl #2
	ldr	lr, [lr, #4]
	str	lr, [r0, r1, lsl #2]
	ldr	r0, [r4, #348]
	add	lr, r0, r1, lsl #1
	ldrh	lr, [lr, #2]
	strh	lr, [r0, r1, lsl #1]	@ movhi
	b	.L709
.L707:
	subs	r3, r3, #1
	sxth	r3, r3
	b	.L706
.L690:
	ldrh	r1, [r4, #2628]
	movw	r2, #65535
	cmp	r1, r2
	bne	.L713
	strh	r5, [r4, #2628]	@ movhi
.L755:
	str	r3, [r4, #2636]
	b	.L689
.L713:
	ldrh	r0, [r4, #2632]
	cmp	r0, r2
	beq	.L714
	movs	r1, #1
	bl	FtlFreeSysBlkQueueIn
.L714:
	ldr	r2, [r4, #2636]
	ldr	r3, [r6, #4]
	cmp	r2, r3
	bcs	.L715
	ldrh	r3, [r4, #2628]
	strh	r5, [r4, #2628]	@ movhi
	strh	r3, [r4, #2632]	@ movhi
	ldr	r3, [r6, #4]
	b	.L755
.L715:
	strh	r5, [r4, #2632]	@ movhi
	b	.L689
.L718:
	ldr	r1, [r4, #3360]
	ldrh	r3, [r1]
	cbz	r3, .L720
.L723:
	ldr	r1, [r4, #348]
	ldrh	r2, [r1]
	cmp	r2, #0
	beq	.L721
.L722:
	ldrh	r2, [r4, #3440]
	ldr	r3, [r4, #328]
	cmp	r2, r3
	bls	.L753
	movw	r2, #1391
	ldr	r1, .L759
	ldr	r0, .L759+4
	bl	sftl_printk
.L753:
	movs	r0, #0
	add	sp, sp, #20
	@ sp needed
	pop	{r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L720:
	ldrh	r2, [r4, #3440]
	cmp	r2, #0
	beq	.L723
	ldr	r0, [r4, #328]
.L724:
	sxth	r2, r3
	cmp	r2, r0
	bcs	.L723
	ldrh	r5, [r1, r2, lsl #1]
	adds	r3, r3, #1
	cmp	r5, #0
	beq	.L724
	mov	r3, r2
	movs	r5, #0
.L725:
	ldr	r1, [r4, #328]
	cmp	r3, r1
	bcs	.L723
	ldr	r1, [r4, #3360]
	subs	r0, r3, r2
	ldrh	r6, [r1, r3, lsl #1]
	strh	r6, [r1, r0, lsl #1]	@ movhi
	ldr	r1, [r4, #3384]
	ldr	r6, [r1, r3, lsl #2]
	str	r6, [r1, r0, lsl #2]
	ldr	r1, [r4, #3360]
	strh	r5, [r1, r3, lsl #1]	@ movhi
	adds	r3, r3, #1
	sxth	r3, r3
	b	.L725
.L721:
	ldrh	r3, [r4, #344]
	cmp	r3, #0
	beq	.L722
	ldrh	r0, [r4, #320]
.L730:
	sxth	r3, r2
	cmp	r3, r0
	mov	r5, r3
	bge	.L722
	ldrh	r6, [r1, r3, lsl #1]
	adds	r2, r2, #1
	cmp	r6, #0
	beq	.L730
	movs	r0, #0
.L731:
	ldrh	r2, [r4, #320]
	cmp	r3, r2
	bge	.L722
	ldr	r2, [r4, #348]
	subs	r1, r3, r5
	ldrh	r6, [r2, r3, lsl #1]
	strh	r6, [r2, r1, lsl #1]	@ movhi
	ldr	r2, [r4, #3372]
	ldr	r6, [r2, r3, lsl #2]
	str	r6, [r2, r1, lsl #2]
	ldr	r2, [r4, #348]
	strh	r0, [r2, r3, lsl #1]	@ movhi
	adds	r3, r3, #1
	sxth	r3, r3
	b	.L731
.L760:
	.align	2
.L759:
	.word	.LANCHOR1+231
	.word	.LC8
	.fnend
	.size	FtlScanSysBlk, .-FtlScanSysBlk
	.align	1
	.global	FtlLoadBbt
	.syntax unified
	.thumb
	.thumb_func
	.fpu softvfp
	.type	FtlLoadBbt, %function
FtlLoadBbt:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	push	{r4, r5, r6, r7, r8, lr}
	.save {r4, r5, r6, r7, r8, lr}
	movw	r8, #61649
	ldr	r4, .L790
	ldr	r3, [r4, #3304]
	add	r7, r4, #3456
	ldr	r6, [r4, #3336]
	str	r3, [r4, #3464]
	str	r6, [r4, #3468]
	bl	FtlBbtMemInit
	ldrh	r5, [r4, #298]
	subs	r5, r5, #1
	uxth	r5, r5
.L762:
	ldrh	r3, [r4, #298]
	subs	r3, r3, #16
	cmp	r5, r3
	ble	.L765
	lsls	r3, r5, #10
	movs	r2, #1
	mov	r1, r2
	mov	r0, r7
	str	r3, [r4, #3460]
	bl	FlashReadPages
	ldr	r3, [r4, #3456]
	adds	r3, r3, #1
	bne	.L763
	ldr	r3, [r4, #3460]
	movs	r2, #1
	mov	r1, r2
	mov	r0, r7
	adds	r3, r3, #1
	str	r3, [r4, #3460]
	bl	FlashReadPages
.L763:
	ldr	r3, [r4, #3456]
	adds	r3, r3, #1
	beq	.L764
	ldrh	r3, [r6]
	cmp	r3, r8
	bne	.L764
	ldr	r3, [r6, #4]
	strh	r5, [r4, #352]	@ movhi
	str	r3, [r4, #360]
	ldrh	r3, [r6, #8]
	strh	r3, [r4, #356]	@ movhi
.L765:
	ldrh	r3, [r4, #352]
	movw	r2, #65535
	cmp	r3, r2
	beq	.L779
	ldrh	r3, [r4, #356]
	cmp	r3, r2
	beq	.L769
	lsls	r3, r3, #10
	movs	r2, #1
	mov	r1, r2
	ldr	r0, .L790+4
	str	r3, [r4, #3460]
	bl	FlashReadPages
	ldr	r3, [r4, #3456]
	adds	r3, r3, #1
	beq	.L769
	ldrh	r2, [r6]
	movw	r3, #61649
	cmp	r2, r3
	bne	.L769
	ldr	r3, [r6, #4]
	ldr	r2, [r4, #360]
	cmp	r3, r2
	bls	.L769
	ldrh	r2, [r4, #356]
	str	r3, [r4, #360]
	ldrh	r3, [r6, #8]
	strh	r2, [r4, #352]	@ movhi
	strh	r3, [r4, #356]	@ movhi
.L769:
	ldr	r7, .L790+4
	movs	r1, #1
	ldrh	r0, [r4, #352]
	movw	r8, #61649
	bl	FtlGetLastWrittenPage
	sxth	r5, r0
	adds	r0, r0, #1
	strh	r0, [r4, #354]	@ movhi
.L771:
	cmp	r5, #0
	bge	.L774
	movs	r2, #251
	ldr	r1, .L790+8
	ldr	r0, .L790+12
	bl	sftl_printk
.L773:
	ldrh	r3, [r6, #10]
	ldrh	r0, [r6, #12]
	strh	r3, [r4, #358]	@ movhi
	movw	r3, #65535
	cmp	r0, r3
	beq	.L776
	ldr	r2, [r4, #228]
	cmp	r0, r2
	beq	.L776
	ldrh	r3, [r4, #242]
	lsrs	r3, r3, #2
	cmp	r2, r3
	bcs	.L776
	cmp	r0, r3
	bcs	.L776
	bl	FtlSysBlkNumInit
.L776:
	ldr	r6, .L790+16
	movs	r5, #0
.L777:
	ldrh	r3, [r4, #254]
	cmp	r5, r3
	bcc	.L778
	movs	r0, #0
	pop	{r4, r5, r6, r7, r8, pc}
.L764:
	subs	r5, r5, #1
	uxth	r5, r5
	b	.L762
.L774:
	ldrh	r3, [r4, #352]
	movs	r2, #1
	mov	r1, r2
	mov	r0, r7
	orr	r3, r5, r3, lsl #10
	str	r3, [r4, #3460]
	ldr	r3, [r4, #3304]
	str	r3, [r4, #3464]
	bl	FlashReadPages
	ldr	r3, [r4, #3456]
	adds	r3, r3, #1
	beq	.L772
	ldrh	r3, [r6]
	cmp	r3, r8
	beq	.L773
.L772:
	subs	r5, r5, #1
	sxth	r5, r5
	b	.L771
.L778:
	ldrh	r2, [r4, #3392]
	ldr	r1, [r4, #3464]
	ldr	r0, [r6, #4]!
	lsls	r2, r2, #2
	mla	r1, r5, r2, r1
	adds	r5, r5, #1
	bl	ftl_memcpy
	b	.L777
.L779:
	mov	r0, #-1
	pop	{r4, r5, r6, r7, r8, pc}
.L791:
	.align	2
.L790:
	.word	.LANCHOR0
	.word	.LANCHOR0+3456
	.word	.LANCHOR1+245
	.word	.LC8
	.word	.LANCHOR0+376
	.fnend
	.size	FtlLoadBbt, .-FtlLoadBbt
	.align	1
	.global	FlashProgPages
	.syntax unified
	.thumb
	.thumb_func
	.fpu softvfp
	.type	FlashProgPages, %function
FlashProgPages:
	.fnstart
	@ args = 0, pretend = 0, frame = 48
	@ frame_needed = 0, uses_anonymous_args = 0
	ldr	r3, .L830
	push	{r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.save {r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.pad #52
	sub	sp, sp, #52
	ldr	r6, .L830+4
	mov	fp, r1
	mov	r4, r0
	mov	r5, r0
	str	r2, [sp, #8]
	mov	r10, #0
	ldr	r2, [r3]
	mov	r8, r6
	str	r3, [sp, #12]
	str	r2, [sp, #44]
	ldrh	r2, [r6, #12]
	str	r2, [sp]
	lsls	r2, r2, #3
	str	r2, [sp, #4]
.L793:
	cmp	r10, fp
	bne	.L807
	ldr	r7, .L830+8
	movs	r5, #0
	ldr	r8, .L830+32
	ldr	r9, .L830+36
.L808:
	cmp	r10, r5
	beq	.L801
	ldr	r2, [r6, #3324]
	movs	r3, #0
	movs	r1, #1
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
	cmp	fp, #-1
	bne	.L809
	ldr	r1, [r4, #4]
	mov	r0, r7
	bl	sftl_printk
	str	fp, [r4]
.L809:
	ldr	fp, [sp, #24]
	cmp	fp, #256
	bne	.L810
	ldr	r1, [r4, #4]
	mov	r0, r8
	bl	sftl_printk
	str	fp, [r4]
.L810:
	ldr	r3, [r4, #12]
	cbz	r3, .L811
	ldr	r2, [r3]
	ldr	r3, [r6, #3332]
	ldr	r3, [r3]
	cmp	r2, r3
	beq	.L811
	ldr	r1, [r4, #4]
	mov	r0, r9
	bl	sftl_printk
	mov	r3, #-1
	str	r3, [r4]
.L811:
	ldr	r3, [r4, #8]
	cbz	r3, .L812
	ldr	r2, [r3]
	ldr	r3, [r6, #3324]
	ldr	r3, [r3]
	cmp	r2, r3
	beq	.L812
	ldr	r1, [r4, #4]
	ldr	r0, .L830+12
	bl	sftl_printk
	mov	r3, #-1
	str	r3, [r4]
.L812:
	adds	r5, r5, #1
	adds	r4, r4, #20
	b	.L808
.L807:
	ldr	r3, [r5, #8]
	cbz	r3, .L794
	ldr	r3, [r5, #12]
	cbnz	r3, .L795
.L794:
	movs	r2, #148
	ldr	r1, .L830+16
	ldr	r0, .L830+20
	bl	sftl_printk
.L795:
	add	r2, sp, #16
	add	r1, sp, #20
	mov	r0, r5
	bl	l2p_addr_tran
	ldr	r7, [sp, #16]
	cmp	r7, #3
	bls	.L796
.L829:
	mov	r3, #-1
	str	r3, [r5]
	b	.L797
.L796:
	cbnz	r7, .L798
	ldr	r3, [sp, #20]
	ldr	r2, [sp, #4]
	cmp	r2, r3
	bls	.L798
	ldr	r5, .L830+16
	ldr	r6, .L830+24
	ldr	r8, .L830+40
	b	.L828
.L800:
	mov	r3, #-1
	ldr	r2, [r4, #-16]
	str	r3, [r4, #-20]
	mov	r1, r5
	mov	r0, r6
	adds	r7, r7, #1
	bl	sftl_printk
	movs	r3, #16
	movs	r2, #4
	ldr	r1, [r4, #-12]
	mov	r0, r8
	bl	rknand_print_hex
	movs	r3, #4
	ldr	r1, [r4, #-8]
	mov	r2, r3
	ldr	r0, .L830+28
	bl	rknand_print_hex
.L828:
	cmp	r7, fp
	add	r4, r4, #20
	bne	.L800
	bl	dump_stack
.L801:
	ldr	r3, [sp, #12]
	movs	r0, #0
	ldr	r2, [sp, #44]
	ldr	r3, [r3]
	cmp	r2, r3
	beq	.L814
	bl	__stack_chk_fail
.L798:
	ldr	r1, [r5, #8]
	lsls	r3, r1, #26
	beq	.L815
	ldr	r9, [r8, #3324]
	cmp	r1, r9
	beq	.L802
	ldrh	r2, [r8, #258]
	mov	r0, r9
	lsls	r2, r2, #9
	bl	ftl_memcpy
.L802:
	ldr	r3, [r5, #12]
	mov	r2, r9
	ldr	r1, [sp, #20]
	ldrb	r0, [sp, #16]	@ zero_extendqisi2
	ldr	r7, [r8, #3256]
	blx	r7
	cbnz	r0, .L803
	str	r0, [r5]
.L804:
	ldrh	r3, [r8, #14]
	cmp	r3, #4
	bne	.L797
	ldr	r0, [sp]
	add	r2, r9, #2048
	ldr	r3, [r5, #12]
	ldr	r1, [sp, #20]
	ldr	r7, [r8, #3256]
	adds	r3, r3, #8
	add	r1, r1, r0
	ldrb	r0, [sp, #16]	@ zero_extendqisi2
	blx	r7
	cmp	r0, #0
	bne	.L829
.L797:
	add	r10, r10, #1
	adds	r5, r5, #20
	b	.L793
.L815:
	mov	r9, r1
	b	.L802
.L803:
	mov	r3, #-1
	str	r3, [r5]
	b	.L804
.L814:
	add	sp, sp, #52
	@ sp needed
	pop	{r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L831:
	.align	2
.L830:
	.word	__stack_chk_guard
	.word	.LANCHOR0
	.word	.LC95
	.word	.LC98
	.word	.LANCHOR1+256
	.word	.LC8
	.word	.LC86
	.word	.LC88
	.word	.LC96
	.word	.LC97
	.word	.LC87
	.fnend
	.size	FlashProgPages, .-FlashProgPages
	.align	1
	.global	FtlLowFormatEraseBlock
	.syntax unified
	.thumb
	.thumb_func
	.fpu softvfp
	.type	FtlLowFormatEraseBlock, %function
FtlLowFormatEraseBlock:
	.fnstart
	@ args = 0, pretend = 0, frame = 16
	@ frame_needed = 0, uses_anonymous_args = 0
	push	{r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.save {r4, r5, r6, r7, r8, r9, r10, fp, lr}
	mov	r10, #0
	ldr	r4, .L877
	mov	r7, r1
	mov	r6, r10
	mov	r5, r10
	mov	r9, #20
	.pad #20
	sub	sp, sp, #20
	mov	fp, r4
	str	r0, [sp, #4]
	str	r0, [r4, #3288]
.L833:
	ldrh	r1, [r4, #232]
	uxth	r3, r10
	cmp	r1, r3
	bhi	.L838
	cmp	r6, #0
	beq	.L832
	mov	r8, #0
	mov	r9, #20
	mov	r2, r6
	movs	r1, #0
	ldr	r0, [r4, #3272]
	bl	FlashEraseBlocks
.L841:
	uxth	r3, r8
	cmp	r6, r3
	bhi	.L843
	cmp	r7, #0
	beq	.L860
	ldrh	r3, [r4, #304]
	mov	r10, #1
	str	r3, [sp, #8]
.L844:
	mov	r8, #0
	mov	fp, #20
.L854:
	mov	r9, #0
	mov	r6, r9
.L845:
	ldrh	r1, [r4, #232]
	uxth	r3, r9
	cmp	r1, r3
	bhi	.L849
	cbz	r6, .L832
	mov	r9, #0
	movs	r3, #1
	mov	r2, r10
	mov	r1, r6
	ldr	r0, [r4, #3272]
	bl	FlashProgPages
.L851:
	uxth	r3, r9
	cmp	r6, r3
	bhi	.L853
	add	r8, r8, #1
	ldr	r2, [sp, #8]
	uxth	r3, r8
	cmp	r2, r3
	bhi	.L854
	mov	r8, #0
	mov	r9, #20
.L855:
	uxth	r3, r8
	cmp	r6, r3
	bhi	.L857
	ldr	r3, [sp, #4]
	cmp	r3, #63
	bls	.L858
	cbz	r7, .L832
.L858:
	mov	r2, r6
	mov	r1, r10
	ldr	r0, [r4, #3272]
	bl	FlashEraseBlocks
.L832:
	mov	r0, r5
	add	sp, sp, #20
	@ sp needed
	pop	{r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L838:
	uxth	r3, r10
	ldr	r0, [r4, #3272]
	movs	r2, #0
	mul	r1, r9, r3
	add	r3, r3, r4
	str	r2, [r0, r1]
	ldr	r1, [sp, #4]
	ldrb	r0, [r3, #260]	@ zero_extendqisi2
	bl	V2P_block
	mov	r8, r0
	cbz	r7, .L834
	bl	IsBlkInVendorPart
	cbnz	r0, .L835
.L834:
	mov	r0, r8
	bl	FtlBbmIsBadBlock
	cbnz	r0, .L836
	ldr	r1, [r4, #3272]
	lsl	r3, r8, #10
	ldr	r0, [fp, #3340]
	mla	r1, r9, r6, r1
	str	r3, [r1, #4]
	ldr	r3, [r4, #3320]
	str	r3, [r1, #8]
	ldrh	r3, [r4, #312]
	muls	r3, r6, r3
	add	r6, r6, #1
	it	mi
	addmi	r3, r3, #3
	uxth	r6, r6
	bic	r3, r3, #3
	add	r3, r3, r0
	str	r3, [r1, #12]
.L835:
	add	r10, r10, #1
	b	.L833
.L836:
	adds	r5, r5, #1
	uxth	r5, r5
	b	.L835
.L843:
	mul	r3, r9, r8
	ldr	r2, [r4, #3272]
	adds	r1, r2, r3
	ldr	r3, [r2, r3]
	adds	r3, r3, #1
	bne	.L842
	ldr	r0, [r1, #4]
	adds	r5, r5, #1
	uxth	r5, r5
	ubfx	r0, r0, #10, #16
	bl	FtlBbmMapBadBlock
.L842:
	add	r8, r8, #1
	b	.L841
.L860:
	movs	r3, #2
	mov	r10, r7
	str	r3, [sp, #8]
	b	.L844
.L849:
	uxth	r3, r9
	ldr	r0, [r4, #3272]
	movs	r2, #0
	mul	r1, fp, r3
	add	r3, r3, r4
	str	r2, [r0, r1]
	ldr	r1, [sp, #4]
	ldrb	r0, [r3, #260]	@ zero_extendqisi2
	bl	V2P_block
	str	r0, [sp, #12]
	cbz	r7, .L846
	bl	IsBlkInVendorPart
	cbnz	r0, .L847
.L846:
	ldr	r0, [sp, #12]
	bl	FtlBbmIsBadBlock
	cbnz	r0, .L847
	ldr	r1, [r4, #3272]
	ldr	r3, [sp, #12]
	ldr	r2, .L877
	mla	r1, fp, r6, r1
	ldr	r0, [r2, #3320]
	add	r3, r8, r3, lsl #10
	str	r3, [r1, #4]
	ldr	r3, [r4, #3316]
	str	r3, [r1, #8]
	ldrh	r3, [r4, #312]
	muls	r3, r6, r3
	add	r6, r6, #1
	it	mi
	addmi	r3, r3, #3
	uxth	r6, r6
	bic	r3, r3, #3
	add	r3, r3, r0
	str	r3, [r1, #12]
.L847:
	add	r9, r9, #1
	b	.L845
.L853:
	mul	r3, fp, r9
	ldr	r2, [r4, #3272]
	adds	r1, r2, r3
	ldr	r3, [r2, r3]
	cbz	r3, .L852
	ldr	r0, [r1, #4]
	adds	r5, r5, #1
	uxth	r5, r5
	ubfx	r0, r0, #10, #16
	bl	FtlBbmMapBadBlock
.L852:
	add	r9, r9, #1
	b	.L851
.L857:
	cbz	r7, .L856
	mul	r3, r9, r8
	ldr	r2, [r4, #3272]
	adds	r1, r2, r3
	ldr	r3, [r2, r3]
	cbnz	r3, .L856
	ldr	r0, [r1, #4]
	movs	r1, #1
	ubfx	r0, r0, #10, #16
	bl	FtlFreeSysBlkQueueIn
.L856:
	add	r8, r8, #1
	b	.L855
.L878:
	.align	2
.L877:
	.word	.LANCHOR0
	.fnend
	.size	FtlLowFormatEraseBlock, .-FtlLowFormatEraseBlock
	.align	1
	.global	FlashTestBlk
	.syntax unified
	.thumb
	.thumb_func
	.fpu softvfp
	.type	FlashTestBlk, %function
FlashTestBlk:
	.fnstart
	@ args = 0, pretend = 0, frame = 88
	@ frame_needed = 0, uses_anonymous_args = 0
	push	{r4, r5, r6, lr}
	.save {r4, r5, r6, lr}
	cmp	r0, #11
	ldr	r6, .L885
	.pad #88
	sub	sp, sp, #88
	mov	r5, r0
	ldr	r2, [r6]
	str	r2, [sp, #84]
	bls	.L883
	ldr	r4, .L885+4
	add	r0, sp, #20
	movs	r2, #32
	movs	r1, #165
	str	r0, [sp, #12]
	lsls	r5, r5, #10
	ldr	r3, [r4, #3328]
	str	r3, [sp, #8]
	bl	ftl_memset
	movs	r2, #8
	movs	r1, #90
	ldr	r0, [r4, #3328]
	bl	ftl_memset
	movs	r2, #1
	mov	r0, sp
	mov	r1, r2
	str	r5, [sp, #4]
	bl	FlashEraseBlocks
	movs	r3, #1
	mov	r0, sp
	mov	r2, r3
	mov	r1, r3
	bl	FlashProgPages
	ldr	r3, [sp]
	cbnz	r3, .L884
	adds	r3, r5, #1
	mov	r0, sp
	str	r3, [sp, #4]
	movs	r3, #1
	mov	r2, r3
	mov	r1, r3
	bl	FlashProgPages
	ldr	r4, [sp]
	adds	r4, r4, #0
	it	ne
	movne	r4, #1
	negs	r4, r4
.L881:
	movs	r2, #1
	movs	r1, #0
	mov	r0, sp
	str	r5, [sp, #4]
	bl	FlashEraseBlocks
.L879:
	ldr	r2, [sp, #84]
	mov	r0, r4
	ldr	r3, [r6]
	cmp	r2, r3
	beq	.L882
	bl	__stack_chk_fail
.L884:
	mov	r4, #-1
	b	.L881
.L883:
	movs	r4, #0
	b	.L879
.L882:
	add	sp, sp, #88
	@ sp needed
	pop	{r4, r5, r6, pc}
.L886:
	.align	2
.L885:
	.word	__stack_chk_guard
	.word	.LANCHOR0
	.fnend
	.size	FlashTestBlk, .-FlashTestBlk
	.align	1
	.global	FtlBbmTblFlush
	.syntax unified
	.thumb
	.thumb_func
	.fpu softvfp
	.type	FtlBbmTblFlush, %function
FtlBbmTblFlush:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	push	{r0, r1, r4, r5, r6, r7, r8, r9, r10, lr}
	.save {r4, r5, r6, r7, r8, r9, r10, lr}
	.pad #8
	movs	r5, #0
	ldr	r4, .L899
	movs	r1, #0
	ldr	r0, [r4, #3304]
	add	r6, r4, #376
	ldr	r3, [r4, #3336]
	ldrh	r2, [r4, #310]
	str	r0, [r4, #3464]
	str	r3, [r4, #3468]
	bl	ftl_memset
.L888:
	ldrh	r3, [r4, #254]
	cmp	r5, r3
	blt	.L889
	ldr	r6, [r4, #3468]
	movs	r2, #16
	movs	r1, #255
	ldr	r8, .L899+12
	ldr	r9, .L899+16
	movs	r5, #0
	mov	r0, r6
	mov	r7, r5
	bl	ftl_memset
	movw	r3, #61649
	strh	r3, [r6]	@ movhi
	ldr	r3, [r4, #360]
	str	r3, [r6, #4]
	ldrh	r3, [r4, #352]
	strh	r3, [r6, #2]	@ movhi
	ldrh	r3, [r4, #356]
	strh	r3, [r6, #8]	@ movhi
	ldrh	r3, [r4, #358]
	strh	r3, [r6, #10]	@ movhi
	ldr	r3, [r4, #228]
	strh	r3, [r6, #12]	@ movhi
	ldr	r3, [r4, #2608]
	strh	r3, [r6, #14]	@ movhi
.L890:
	ldr	r3, [r4, #3304]
	mov	r10, #0
	ldrh	r2, [r4, #354]
	ldrh	r1, [r4, #352]
	str	r3, [r4, #3464]
	ldr	r3, [r4, #3336]
	str	r10, [r4, #3456]
	str	r3, [r4, #3468]
	orr	r3, r2, r1, lsl #10
	ldrh	r0, [r6, #10]
	str	r3, [r4, #3460]
	ldrh	r3, [r4, #356]
	str	r0, [sp]
	mov	r0, r8
	bl	sftl_printk
	ldrh	r3, [r4, #304]
	ldrh	r2, [r4, #354]
	subs	r3, r3, #1
	cmp	r2, r3
	blt	.L891
	ldr	r3, [r4, #360]
	ldrh	r2, [r4, #352]
	ldr	r0, [r4, #3272]
	adds	r3, r3, #1
	strh	r10, [r4, #354]	@ movhi
	str	r3, [r4, #360]
	str	r3, [r6, #4]
	ldrh	r3, [r4, #356]
	strh	r2, [r6, #8]	@ movhi
	strh	r2, [r4, #356]	@ movhi
	movs	r2, #1
	strh	r3, [r4, #352]	@ movhi
	mov	r1, r2
	lsls	r3, r3, #10
	str	r3, [r4, #3460]
	str	r3, [r0, #4]
	bl	FlashEraseBlocks
.L891:
	movs	r3, #1
	mov	r0, r9
	mov	r2, r3
	mov	r1, r3
	bl	FlashProgPages
	ldrh	r3, [r4, #354]
	adds	r3, r3, #1
	strh	r3, [r4, #354]	@ movhi
	ldr	r3, [r4, #3456]
	adds	r2, r3, #1
	bne	.L892
	adds	r5, r5, #1
	ldr	r1, [r4, #3460]
	uxth	r5, r5
	ldr	r0, .L899+4
	bl	sftl_printk
	cmp	r5, #3
	bls	.L890
	mov	r2, r5
	ldr	r1, [r4, #3460]
	ldr	r0, .L899+8
	bl	sftl_printk
.L894:
	b	.L894
.L889:
	ldrh	r2, [r4, #3392]
	ldr	r3, [r4, #3464]
	ldr	r1, [r6, #4]!
	mul	r0, r2, r5
	lsls	r2, r2, #2
	adds	r5, r5, #1
	add	r0, r3, r0, lsl #2
	bl	ftl_memcpy
	b	.L888
.L895:
	movs	r7, #1
	b	.L890
.L892:
	adds	r7, r7, #1
	cmp	r7, #1
	ble	.L895
	cmp	r3, #256
	beq	.L890
	movs	r0, #0
	add	sp, sp, #8
	@ sp needed
	pop	{r4, r5, r6, r7, r8, r9, r10, pc}
.L900:
	.align	2
.L899:
	.word	.LANCHOR0
	.word	.LC100
	.word	.LC101
	.word	.LC99
	.word	.LANCHOR0+3456
	.fnend
	.size	FtlBbmTblFlush, .-FtlBbmTblFlush
	.align	1
	.global	allocate_data_superblock
	.syntax unified
	.thumb
	.thumb_func
	.fpu softvfp
	.type	allocate_data_superblock, %function
allocate_data_superblock:
	.fnstart
	@ args = 0, pretend = 0, frame = 16
	@ frame_needed = 0, uses_anonymous_args = 0
	push	{r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.save {r4, r5, r6, r7, r8, r9, r10, fp, lr}
	mov	r5, r0
	ldr	r4, .L937
	.pad #20
	sub	sp, sp, #20
	ldr	r9, .L937+20
.L902:
	ldr	r3, .L937
	ldr	r2, .L937
	ldrh	r3, [r3, #224]
	ldrh	r2, [r2, #2528]
	add	r3, r3, r2
	ldr	r2, .L937
	ldrh	r2, [r2, #240]
	cmp	r3, r2
	ble	.L903
	movw	r2, #2660
	mov	r1, r9
	ldr	r0, .L937+4
	bl	sftl_printk
.L903:
	ldr	r3, .L937+8
	cmp	r5, r3
	bne	.L929
	ldrh	r3, [r4, #224]
	ldr	r1, [r4, #3168]
	mul	r2, r1, r3
	lsrs	r1, r3, #1
	adds	r1, r1, #1
	add	r1, r1, r2, lsr #2
	uxth	r1, r1
	cbz	r1, .L904
	subs	r1, r1, #1
	uxth	r1, r1
.L904:
	ldr	r0, .L937+12
	bl	List_pop_index_node
	ldrh	r3, [r4, #224]
	mov	r6, r0
	uxth	r8, r0
	cbnz	r3, .L905
	movw	r2, #2669
	mov	r1, r9
	ldr	r0, .L937+4
	bl	sftl_printk
.L905:
	ldrh	r3, [r4, #224]
	subs	r3, r3, #1
	strh	r3, [r4, #224]	@ movhi
	ldrh	r3, [r4, #240]
	cmp	r3, r8
	bls	.L902
	ldr	r3, [r4, #72]
	uxth	r6, r6
	ldrh	r7, [r3, r6, lsl #1]
	cmp	r7, #0
	bne	.L902
	strh	r8, [r5]	@ movhi
	mov	r0, r5
	bl	make_superblock
	ldrb	r3, [r5, #7]	@ zero_extendqisi2
	cbnz	r3, .L907
	ldr	r3, [r4, #72]
	movw	r2, #65535
	mov	r0, r8
	strh	r2, [r3, r6, lsl #1]	@ movhi
	bl	INSERT_DATA_LIST
	ldrh	r2, [r4, #2528]
	ldrh	r3, [r4, #224]
	add	r3, r3, r2
	ldrh	r2, [r4, #240]
	cmp	r3, r2
	ble	.L902
	movw	r2, #2683
	mov	r1, r9
	ldr	r0, .L937+4
	bl	sftl_printk
	b	.L902
.L929:
	movs	r1, #0
	b	.L904
.L907:
	ldrh	r2, [r4, #2528]
	ldrh	r3, [r4, #224]
	add	r3, r3, r2
	ldrh	r2, [r4, #240]
	cmp	r3, r2
	ble	.L909
	movw	r2, #2686
	mov	r1, r9
	ldr	r0, .L937+4
	bl	sftl_printk
.L909:
	ldr	r1, [r4, #3272]
	mov	r10, #20
	ldrh	r2, [r4, #232]
	add	fp, r5, #16
	mov	r0, fp
	mov	ip, #0
	mov	r3, r1
	mla	r2, r10, r2, r1
	str	r2, [sp, #4]
.L910:
	ldr	r2, [sp, #4]
	cmp	r2, r3
	bne	.L912
	cbnz	r7, .L913
	movw	r2, #2697
	mov	r1, r9
	ldr	r0, .L937+4
	bl	sftl_printk
.L913:
	ldrh	r3, [r4, #172]
	cmp	r3, r8
	bne	.L914
	movw	r2, #2699
	mov	r1, r9
	ldr	r0, .L937+4
	bl	sftl_printk
.L914:
	ldrb	r3, [r5, #8]	@ zero_extendqisi2
	ldr	r2, [r4, #2536]
	cmp	r3, #0
	bne	.L915
	ldrh	r3, [r2, r6, lsl #1]
	cmp	r3, #0
	beq	.L916
	ldrh	r1, [r4, #292]
	add	r3, r3, r1
.L936:
	strh	r3, [r2, r6, lsl #1]	@ movhi
	ldr	r3, [r4, #2600]
	adds	r3, r3, #1
	str	r3, [r4, #2600]
.L918:
	ldr	r3, [r4, #2536]
	ldr	r2, [r4, #2616]
	ldrh	r0, [r4, #292]
	ldrh	r3, [r3, r6, lsl #1]
	ldrh	r1, [r4, #240]
	cmp	r3, r2
	ldr	r2, [r4, #2600]
	it	hi
	strhi	r3, [r4, #2616]
	ldr	r3, [r4, #2604]
	mla	r0, r2, r0, r3
	bl	__aeabi_uidiv
	ldr	r2, [r4, #3352]
	ldr	r1, [r4, #3272]
	str	r0, [r4, #2608]
	ldr	r3, [r2, #16]
	ldr	r0, .L937+16
	adds	r3, r3, #1
	str	r3, [r2, #16]
	movs	r2, #20
	mla	r2, r2, r7, r1
	adds	r3, r1, #4
	adds	r2, r2, #24
.L920:
	adds	r3, r3, #20
	cmp	r2, r3
	bne	.L921
	ldrb	r1, [r5, #8]	@ zero_extendqisi2
	mov	r2, r7
	ldr	r0, [r4, #3272]
	mov	r10, #0
	bl	FlashEraseBlocks
	mov	r3, r10
	movs	r1, #20
.L922:
	uxth	r2, r10
	cmp	r7, r2
	bhi	.L924
	cmp	r3, #0
	ble	.L925
	mov	r0, r8
	bl	update_multiplier_value
	bl	FtlBbmTblFlush
.L925:
	ldrb	r2, [r5, #7]	@ zero_extendqisi2
	cmp	r2, #0
	bne	.L926
	ldr	r3, [r4, #72]
	movw	r2, #65535
	strh	r2, [r3, r6, lsl #1]	@ movhi
	b	.L902
.L912:
	str	ip, [r3, #8]
	movw	lr, #65535
	str	ip, [r3, #12]
	ldrh	r2, [r0], #2
	cmp	r2, lr
	beq	.L911
	mla	lr, r10, r7, r1
	adds	r7, r7, #1
	lsls	r2, r2, #10
	uxth	r7, r7
	str	r2, [lr, #4]
.L911:
	adds	r3, r3, #20
	b	.L910
.L916:
	movs	r3, #2
	b	.L936
.L915:
	ldrh	r3, [r2, r6, lsl #1]
	adds	r3, r3, #1
	strh	r3, [r2, r6, lsl #1]	@ movhi
	ldr	r3, [r4, #2604]
	adds	r3, r3, #1
	str	r3, [r4, #2604]
	b	.L918
.L921:
	ldr	r1, [r3, #-20]
	ands	r1, r1, r0
	str	r1, [r3, #-20]
	b	.L920
.L924:
	mul	r2, r1, r10
	ldr	r0, [r4, #3272]
	add	ip, r0, r2
	ldr	r2, [r0, r2]
	adds	r0, r2, #1
	bne	.L923
	ldr	r0, [ip, #4]
	adds	r3, r3, #1
	str	r1, [sp, #12]
	str	r2, [sp, #8]
	ubfx	r0, r0, #10, #16
	str	r3, [sp, #4]
	bl	FtlBbmMapBadBlock
	ldr	r2, [sp, #8]
	ldr	r1, [sp, #12]
	ldr	r3, [sp, #4]
	strh	r2, [fp]	@ movhi
	ldrb	r2, [r5, #7]	@ zero_extendqisi2
	subs	r2, r2, #1
	strb	r2, [r5, #7]
.L923:
	add	r10, r10, #1
	add	fp, fp, #2
	b	.L922
.L926:
	ldrh	r3, [r4, #302]
	strh	r8, [r5]	@ movhi
	smulbb	r3, r3, r2
	movs	r2, #0
	strh	r2, [r5, #2]	@ movhi
	strb	r2, [r5, #6]
	ldr	r2, [r4, #2592]
	uxth	r3, r3
	strh	r3, [r5, #4]	@ movhi
	str	r2, [r5, #12]
	adds	r2, r2, #1
	str	r2, [r4, #2592]
	ldr	r2, [r4, #72]
	ldrh	r1, [r5]
	strh	r3, [r2, r1, lsl #1]	@ movhi
	ldrh	r3, [r5, #4]
	cbz	r3, .L927
	ldrb	r3, [r5, #7]	@ zero_extendqisi2
	cbnz	r3, .L928
.L927:
	mov	r2, #2752
	ldr	r1, .L937+20
	ldr	r0, .L937+4
	bl	sftl_printk
.L928:
	movs	r0, #0
	add	sp, sp, #20
	@ sp needed
	pop	{r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L938:
	.align	2
.L937:
	.word	.LANCHOR0
	.word	.LC8
	.word	.LANCHOR0+124
	.word	.LANCHOR0+2532
	.word	-1024
	.word	.LANCHOR1+271
	.fnend
	.size	allocate_data_superblock, .-allocate_data_superblock
	.align	1
	.global	FtlGcFreeBadSuperBlk
	.syntax unified
	.thumb
	.thumb_func
	.fpu softvfp
	.type	FtlGcFreeBadSuperBlk, %function
FtlGcFreeBadSuperBlk:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	push	{r3, r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.save {r3, r4, r5, r6, r7, r8, r9, r10, fp, lr}
	mov	r8, r0
	ldr	r4, .L951
	ldrh	r3, [r4, #3210]
	cbz	r3, .L940
	movs	r7, #0
	addw	r10, r4, #3212
.L941:
	ldrh	r2, [r4, #232]
	uxth	r3, r7
	cmp	r2, r3
	bhi	.L947
	bl	FtlGcReFreshBadBlk
.L940:
	movs	r0, #0
	pop	{r3, r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L947:
	uxtah	r3, r4, r7
	mov	r1, r8
	mov	fp, #0
	ldrb	r0, [r3, #260]	@ zero_extendqisi2
	bl	V2P_block
	mov	r9, r0
.L942:
	ldrh	r3, [r4, #3210]
	uxth	r5, fp
	cmp	r3, r5
	bhi	.L946
	adds	r7, r7, #1
	b	.L941
.L946:
	uxth	r6, fp
	add	r3, r4, r6, lsl #1
	ldrh	r3, [r3, #3212]
	cmp	r3, r9
	bne	.L943
	mov	r1, r9
	ldr	r0, .L951+4
	bl	sftl_printk
	mov	r0, r9
	bl	FtlBbmMapBadBlock
	bl	FtlBbmTblFlush
	ldrh	r1, [r4, #3210]
	add	r3, r10, r6, lsl #1
.L944:
	cmp	r5, r1
	bcc	.L945
	subs	r1, r1, #1
	strh	r1, [r4, #3210]	@ movhi
.L943:
	add	fp, fp, #1
	b	.L942
.L945:
	ldrh	r0, [r3, #2]!
	adds	r5, r5, #1
	uxth	r5, r5
	strh	r0, [r3, #-2]	@ movhi
	b	.L944
.L952:
	.align	2
.L951:
	.word	.LANCHOR0
	.word	.LC102
	.fnend
	.size	FtlGcFreeBadSuperBlk, .-FtlGcFreeBadSuperBlk
	.align	1
	.global	update_vpc_list
	.syntax unified
	.thumb
	.thumb_func
	.fpu softvfp
	.type	update_vpc_list, %function
update_vpc_list:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	push	{r3, r4, r5, lr}
	.save {r3, r4, r5, lr}
	mov	r5, r0
	ldr	r4, .L965
	ldr	r3, [r4, #72]
	ldrh	r3, [r3, r0, lsl #1]
	cmp	r3, #0
	bne	.L954
	ldrh	r3, [r4, #222]
	cmp	r3, r0
	itt	eq
	movweq	r3, #65535
	strheq	r3, [r4, #222]	@ movhi
	ldrh	r3, [r4, #220]
	cmp	r3, r0
	itt	eq
	movweq	r3, #65535
	strheq	r3, [r4, #220]	@ movhi
	ldrh	r3, [r4, #172]
	cmp	r3, r0
	bne	.L957
	movw	r3, #65535
	strh	r3, [r4, #172]	@ movhi
.L958:
	mov	r1, r5
	ldr	r0, .L965+4
	bl	List_remove_node
	ldrh	r3, [r4, #2528]
	cbnz	r3, .L960
	movw	r2, #2828
	ldr	r1, .L965+8
	ldr	r0, .L965+12
	bl	sftl_printk
.L960:
	ldrh	r3, [r4, #2528]
	mov	r0, r5
	subs	r3, r3, #1
	strh	r3, [r4, #2528]	@ movhi
	bl	free_data_superblock
	mov	r0, r5
	bl	FtlGcFreeBadSuperBlk
	ldrh	r2, [r4, #2528]
	ldrh	r3, [r4, #224]
	add	r3, r3, r2
	ldrh	r2, [r4, #240]
	cmp	r3, r2
	ble	.L964
	movw	r2, #2831
	ldr	r1, .L965+8
	ldr	r0, .L965+12
	bl	sftl_printk
.L964:
	movs	r0, #1
	pop	{r3, r4, r5, pc}
.L957:
	ldrh	r3, [r4, #24]
	cmp	r3, r0
	beq	.L963
	ldrh	r3, [r4, #76]
	cmp	r3, r0
	beq	.L963
	ldrh	r3, [r4, #124]
	cmp	r3, r0
	bne	.L958
.L963:
	movs	r0, #0
	pop	{r3, r4, r5, pc}
.L954:
	bl	List_update_data_list
	b	.L963
.L966:
	.align	2
.L965:
	.word	.LANCHOR0
	.word	.LANCHOR0+2520
	.word	.LANCHOR1+296
	.word	.LC8
	.fnend
	.size	update_vpc_list, .-update_vpc_list
	.align	1
	.global	decrement_vpc_count
	.syntax unified
	.thumb
	.thumb_func
	.fpu softvfp
	.type	decrement_vpc_count, %function
decrement_vpc_count:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	movw	r3, #65535
	push	{r4, r5, r6, lr}
	.save {r4, r5, r6, lr}
	cmp	r0, r3
	mov	r4, r0
	ldr	r5, .L976
	beq	.L968
	ldr	r3, [r5, #72]
	ldrh	r2, [r3, r0, lsl #1]
	cbnz	r2, .L969
	mov	r1, r0
	ldr	r0, .L976+4
	bl	sftl_printk
	ldr	r3, [r5, #72]
	ldrh	r6, [r3, r4, lsl #1]
	cbz	r6, .L970
.L974:
	movs	r6, #0
.L967:
	mov	r0, r6
	pop	{r4, r5, r6, pc}
.L970:
	movw	r2, #2846
.L975:
	ldr	r1, .L976+8
	ldr	r0, .L976+12
	bl	sftl_printk
	b	.L967
.L969:
	subs	r2, r2, #1
	strh	r2, [r3, r0, lsl #1]	@ movhi
.L968:
	ldrh	r0, [r5, #3442]
	movw	r3, #65535
	cmp	r0, r3
	bne	.L972
	strh	r4, [r5, #3442]	@ movhi
	b	.L974
.L972:
	cmp	r4, r0
	beq	.L974
	bl	update_vpc_list
	ldr	r2, [r5, #2516]
	adds	r6, r0, #0
	ldr	r3, [r5, #2520]
	it	ne
	movne	r6, #1
	strh	r4, [r5, #3442]	@ movhi
	subs	r3, r3, r2
	asrs	r2, r3, #1
	ldr	r3, .L976+16
	muls	r3, r2, r3
	ldr	r2, [r5, #72]
	uxth	r1, r3
	ldrh	r2, [r2, r1, lsl #1]
	cmp	r2, #0
	bne	.L967
	cmp	r4, r1
	beq	.L967
	movw	r2, #2862
	b	.L975
.L977:
	.align	2
.L976:
	.word	.LANCHOR0
	.word	.LC103
	.word	.LANCHOR1+312
	.word	.LC8
	.word	-1431655765
	.fnend
	.size	decrement_vpc_count, .-decrement_vpc_count
	.align	1
	.global	FtlSuperblockPowerLostFix
	.syntax unified
	.thumb
	.thumb_func
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
	ldr	r7, .L992
	mov	r8, #0
	movw	r2, #61589
	mov	r4, r0
	ldr	r5, .L992+4
	ldr	r3, [r7]
	ldr	r6, [r5, #3336]
	str	r3, [sp, #20]
	mov	r3, #-1
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
	add	r2, r2, #304087040
	ldr	r3, [r5, #3304]
	add	r2, r2, #1269760
	addw	r2, r2, #1507
	str	r2, [r3, #4]
	ldrh	r3, [r0, #4]
	tst	r3, #1
	ite	eq
	moveq	r9, #6
	movne	r9, #7
.L984:
	ldrh	r3, [r4, #4]
	cbnz	r3, .L980
.L981:
	ldr	r2, [r5, #72]
	ldrh	r1, [r4]
	ldrh	r0, [r4, #4]
	ldrh	r3, [r2, r1, lsl #1]
	subs	r3, r3, r0
	strh	r3, [r2, r1, lsl #1]	@ movhi
	ldrh	r3, [r5, #302]
	ldr	r2, [sp, #20]
	strh	r3, [r4, #2]	@ movhi
	movs	r3, #0
	strb	r3, [r4, #6]
	strh	r3, [r4, #4]	@ movhi
	ldr	r3, [r7]
	cmp	r2, r3
	beq	.L985
	bl	__stack_chk_fail
.L980:
	mov	r0, r4
	bl	get_new_active_ppa
	str	r0, [sp, #4]
	adds	r0, r0, #1
	beq	.L981
	ldr	r3, [r5, #2596]
	movs	r1, #1
	mov	r0, sp
	str	r3, [r6, #4]
	adds	r3, r3, #1
	adds	r2, r3, #1
	it	eq
	moveq	r3, r8
	str	r3, [r5, #2596]
	movs	r3, #0
	mov	r2, r3
	bl	FlashProgPages
	ldrh	r0, [r4]
	bl	decrement_vpc_count
	subs	r9, r9, #1
	bne	.L984
	b	.L981
.L985:
	add	sp, sp, #28
	@ sp needed
	pop	{r4, r5, r6, r7, r8, r9, pc}
.L993:
	.align	2
.L992:
	.word	__stack_chk_guard
	.word	.LANCHOR0
	.fnend
	.size	FtlSuperblockPowerLostFix, .-FtlSuperblockPowerLostFix
	.align	1
	.global	FtlMakeBbt
	.syntax unified
	.thumb
	.thumb_func
	.fpu softvfp
	.type	FtlMakeBbt, %function
FtlMakeBbt:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	push	{r3, r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.save {r3, r4, r5, r6, r7, r8, r9, r10, fp, lr}
	mov	r8, #0
	ldr	r9, .L1013+4
	bl	FtlBbtMemInit
	sub	r5, r9, #380
	sub	r10, r9, #18
	mov	r4, r5
	bl	FtlLoadFactoryBbt
.L995:
	ldrh	r3, [r5, #254]
	cmp	r8, r3
	bcc	.L1001
	movs	r5, #0
.L1002:
	ldrh	r3, [r4, #314]
	uxth	r0, r5
	adds	r5, r5, #1
	cmp	r3, r0
	bhi	.L1003
	ldrh	r5, [r4, #364]
	movw	r6, #65535
	subs	r5, r5, #1
	uxth	r5, r5
.L1004:
	ldrh	r3, [r4, #364]
	subs	r3, r3, #48
	cmp	r5, r3
	ble	.L1008
	mov	r0, r5
	bl	FtlBbmIsBadBlock
	cmp	r0, #1
	beq	.L1005
	mov	r0, r5
	bl	FlashTestBlk
	cmp	r0, #0
	beq	.L1006
	mov	r0, r5
	bl	FtlBbmMapBadBlock
.L1005:
	subs	r5, r5, #1
	uxth	r5, r5
	b	.L1004
.L1001:
	ldrh	r3, [r10, #2]!
	movw	r2, #65535
	ldr	r0, [r5, #3304]
	ldr	fp, [r5, #3336]
	cmp	r3, r2
	str	r0, [r5, #3464]
	str	fp, [r5, #3468]
	beq	.L996
	ldrh	r7, [r5, #298]
	movs	r2, #1
	mov	r1, r2
	ldr	r0, .L1013
	mla	r7, r8, r7, r3
	lsls	r3, r7, #10
	str	r3, [r5, #3460]
	bl	FlashReadPages
	ldrh	r2, [r5, #298]
	ldr	r1, [r5, #3464]
	ldr	r0, [r9]
	adds	r2, r2, #7
	asrs	r2, r2, #3
	bl	ftl_memcpy
.L997:
	uxth	r0, r7
	add	r8, r8, #1
	add	r9, r9, #4
	bl	FtlBbmMapBadBlock
	b	.L995
.L996:
	mov	r1, r8
	bl	FlashGetBadBlockList
	ldr	r1, [r9]
	ldr	r0, [r5, #3464]
	bl	FtlBbt2Bitmap
	ldrh	r6, [r5, #298]
.L999:
	subs	r6, r6, #1
	uxth	r6, r6
.L998:
	ldrh	r0, [r4, #298]
	smlabb	r0, r0, r8, r6
	uxth	r0, r0
	bl	FtlBbmIsBadBlock
	cmp	r0, #1
	beq	.L999
	movs	r2, #16
	movs	r1, #0
	strh	r6, [r10]	@ movhi
	ldr	r0, [r4, #3336]
	bl	ftl_memset
	mov	r2, #4096
	movs	r1, #0
	ldr	r0, [r4, #3304]
	bl	ftl_memset
	movw	r3, #61664
	strh	r3, [fp]	@ movhi
	movs	r3, #0
	str	r3, [fp, #4]
	ldrh	r3, [r10]
	ldrh	r7, [r4, #298]
	ldrh	r2, [r4, #3392]
	strh	r3, [fp, #2]	@ movhi
	ldrh	r3, [r10]
	ldr	r1, [r9]
	lsls	r2, r2, #2
	ldr	r0, [r4, #3464]
	mla	r7, r8, r7, r3
	lsls	r3, r7, #10
	str	r3, [r4, #3460]
	bl	ftl_memcpy
	movs	r2, #1
	ldr	r0, .L1013
	mov	r1, r2
	bl	FlashEraseBlocks
	movs	r3, #1
	ldr	r0, .L1013
	mov	r2, r3
	mov	r1, r3
	bl	FlashProgPages
	ldr	r3, [r4, #3456]
	adds	r3, r3, #1
	bne	.L997
	uxth	r0, r7
	bl	FtlBbmMapBadBlock
	b	.L998
.L1003:
	bl	FtlBbmMapBadBlock
	b	.L1002
.L1006:
	ldrh	r3, [r4, #352]
	cmp	r3, r6
	bne	.L1007
	strh	r5, [r4, #352]	@ movhi
	b	.L1005
.L1007:
	strh	r5, [r4, #356]	@ movhi
.L1008:
	ldrh	r3, [r4, #352]
	movs	r5, #0
	ldr	r0, [r4, #3272]
	movs	r1, #1
	str	r5, [r4, #360]
	movs	r2, #2
	strh	r5, [r4, #354]	@ movhi
	lsls	r3, r3, #10
	str	r3, [r0, #4]
	ldrh	r3, [r4, #356]
	lsls	r3, r3, #10
	str	r3, [r0, #24]
	bl	FlashEraseBlocks
	ldrh	r0, [r4, #352]
	bl	FtlBbmMapBadBlock
	ldrh	r0, [r4, #356]
	bl	FtlBbmMapBadBlock
	bl	FtlBbmTblFlush
	ldr	r3, [r4, #360]
	ldrh	r2, [r4, #356]
	strh	r5, [r4, #354]	@ movhi
	adds	r3, r3, #1
	str	r3, [r4, #360]
	ldrh	r3, [r4, #352]
	strh	r2, [r4, #352]	@ movhi
	strh	r3, [r4, #356]	@ movhi
	bl	FtlBbmTblFlush
	mov	r0, r5
	pop	{r3, r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L1014:
	.align	2
.L1013:
	.word	.LANCHOR0+3456
	.word	.LANCHOR0+380
	.fnend
	.size	FtlMakeBbt, .-FtlMakeBbt
	.align	1
	.global	ftl_memcmp
	.syntax unified
	.thumb
	.thumb_func
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
	.align	1
	.global	js_hash
	.syntax unified
	.thumb
	.thumb_func
	.fpu softvfp
	.type	js_hash, %function
js_hash:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	ldr	r3, .L1019
	add	r1, r1, r0
	push	{r4, lr}
	.save {r4, lr}
.L1017:
	cmp	r0, r1
	bne	.L1018
	mov	r0, r3
	pop	{r4, pc}
.L1018:
	lsrs	r2, r3, #2
	ldrb	r4, [r0], #1	@ zero_extendqisi2
	add	r2, r2, r3, lsl #5
	add	r2, r2, r4
	eors	r3, r3, r2
	b	.L1017
.L1020:
	.align	2
.L1019:
	.word	1204201446
	.fnend
	.size	js_hash, .-js_hash
	.align	1
	.global	Ftl_write_map_blk_to_last_page
	.syntax unified
	.thumb
	.thumb_func
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
	bne	.L1022
	ldrh	r3, [r0, #8]
	cbz	r3, .L1023
	movw	r2, #641
	ldr	r1, .L1031
	ldr	r0, .L1031+4
	bl	sftl_printk
.L1023:
	ldrh	r3, [r4, #8]
	adds	r3, r3, #1
	strh	r3, [r4, #8]	@ movhi
	bl	FtlFreeSysBlkQueueOut
	movs	r3, #0
	strh	r0, [r5]	@ movhi
	strh	r3, [r4, #2]	@ movhi
	strh	r3, [r4]	@ movhi
	ldr	r3, [r4, #28]
	adds	r3, r3, #1
	str	r3, [r4, #28]
.L1024:
	movs	r0, #0
	pop	{r4, r5, r6, r7, r8, pc}
.L1022:
	ldrh	r7, [r5, r3, lsl #1]
	movs	r1, #255
	ldrh	r3, [r0, #2]
	ldr	r5, .L1031+8
	ldr	r8, [r0, #24]
	orr	r3, r3, r7, lsl #10
	ldr	r6, [r5, #3336]
	str	r3, [r5, #3460]
	ldr	r3, [r5, #3304]
	str	r6, [r5, #3468]
	str	r3, [r5, #3464]
	ldr	r3, [r0, #28]
	str	r3, [r6, #4]
	movw	r3, #64245
	strh	r3, [r6, #8]	@ movhi
	ldrh	r3, [r0, #4]
	strh	r7, [r6, #2]	@ movhi
	strh	r3, [r6]	@ movhi
	ldrh	r2, [r5, #304]
	ldr	r0, [r5, #3304]
	lsls	r2, r2, #3
	bl	ftl_memset
	movs	r2, #0
	mov	r3, r2
.L1025:
	ldrh	r0, [r4, #6]
	uxth	r1, r2
	cmp	r0, r1
	bhi	.L1027
	ldrh	r1, [r5, #310]
	ldr	r0, [r5, #3464]
	bl	js_hash
	movs	r2, #1
	str	r0, [r6, #12]
	movs	r3, #0
	mov	r1, r2
	ldr	r0, .L1031+12
	bl	FlashProgPages
	ldrh	r3, [r4, #2]
	mov	r0, r4
	adds	r3, r3, #1
	strh	r3, [r4, #2]	@ movhi
	bl	ftl_map_blk_gc
	b	.L1024
.L1027:
	uxth	r0, r2
	ldr	r1, [r8, r0, lsl #2]
	cmp	r7, r1, lsr #10
	bne	.L1026
	ldr	r1, [r5, #3304]
	adds	r3, r3, #1
	uxth	r3, r3
	str	r0, [r1, r3, lsl #3]
	ldr	r1, [r5, #3304]
	ldr	r0, [r8, r0, lsl #2]
	add	r1, r1, r3, lsl #3
	str	r0, [r1, #4]
.L1026:
	adds	r2, r2, #1
	b	.L1025
.L1032:
	.align	2
.L1031:
	.word	.LANCHOR1+332
	.word	.LC8
	.word	.LANCHOR0
	.word	.LANCHOR0+3456
	.fnend
	.size	Ftl_write_map_blk_to_last_page, .-Ftl_write_map_blk_to_last_page
	.align	1
	.global	FtlMapWritePage
	.syntax unified
	.thumb
	.thumb_func
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
	ldr	r7, .L1057
	mov	r8, r1
	movs	r6, #0
	ldr	r9, .L1057+20
	mov	r5, r7
	str	r2, [sp, #4]
.L1034:
	ldr	r3, [r7, #2576]
	adds	r3, r3, #1
	str	r3, [r7, #2576]
	ldrh	r3, [r7, #304]
	ldrh	r2, [r4, #2]
	subs	r3, r3, #1
	cmp	r2, r3
	bge	.L1035
	ldrh	r2, [r4]
	movw	r3, #65535
	cmp	r2, r3
	bne	.L1036
.L1035:
	mov	r0, r4
	bl	Ftl_write_map_blk_to_last_page
.L1036:
	ldrh	r2, [r4]
	ldr	r3, [r4, #12]
	ldrh	r3, [r3, r2, lsl #1]
	cbnz	r3, .L1037
	mov	r2, #700
	mov	r1, r9
	ldr	r0, .L1057+4
	bl	sftl_printk
.L1037:
	ldrh	r2, [r4]
	ldrh	r3, [r4, #10]
	cmp	r2, r3
	bcc	.L1038
	movw	r2, #701
	mov	r1, r9
	ldr	r0, .L1057+4
	bl	sftl_printk
.L1038:
	ldrh	r2, [r4]
	movs	r1, #0
	ldr	r3, [r4, #12]
	ldr	r0, [r5, #3336]
	ldrh	r10, [r3, r2, lsl #1]
	ldrh	r2, [r4, #2]
	ldr	r3, [sp, #4]
	str	r0, [r5, #3468]
	orr	r2, r2, r10, lsl #10
	str	r3, [r5, #3464]
	str	r2, [r5, #3460]
	movs	r2, #16
	bl	ftl_memset
	ldr	r2, [r4, #28]
	ldr	fp, [r5, #3468]
	str	r2, [fp, #4]
	strh	r8, [fp, #8]	@ movhi
	ldrh	r2, [r4, #4]
	strh	r10, [fp, #2]	@ movhi
	strh	r2, [fp]	@ movhi
	ldrh	r1, [r5, #310]
	ldr	r0, [r5, #3464]
	bl	js_hash
	movs	r3, #1
	str	r0, [fp, #12]
	mov	r2, r3
	mov	r1, r3
	ldr	r0, .L1057+8
	bl	FlashProgPages
	ldrh	r3, [r4, #2]
	adds	r3, r3, #1
	uxth	r3, r3
	strh	r3, [r4, #2]	@ movhi
	ldr	r2, [r5, #3456]
	adds	r1, r2, #1
	bne	.L1039
	ldr	r1, [r5, #3460]
	adds	r6, r6, #1
	ldr	r0, .L1057+12
	uxth	r6, r6
	bl	sftl_printk
	ldrh	r3, [r4, #2]
	cmp	r3, #2
	ittt	ls
	ldrhls	r3, [r5, #304]
	addls	r3, r3, #-1
	strhls	r3, [r4, #2]	@ movhi
	cmp	r6, #3
	bls	.L1041
	mov	r2, r6
	ldr	r1, [r5, #3460]
	ldr	r0, .L1057+16
	bl	sftl_printk
.L1042:
	b	.L1042
.L1041:
	ldr	r3, [r4, #32]
	cmp	r3, #0
	beq	.L1034
.L1056:
	b	.L1056
.L1039:
	cmp	r3, #1
	beq	.L1045
	cmp	r2, #256
	beq	.L1045
	ldr	r0, [r4, #36]
	cbz	r0, .L1046
.L1045:
	movs	r3, #0
	str	r3, [r4, #36]
	b	.L1034
.L1046:
	ldr	r2, [r5, #3460]
	ldr	r3, [r4, #24]
	str	r2, [r3, r8, lsl #2]
	add	sp, sp, #12
	@ sp needed
	pop	{r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L1058:
	.align	2
.L1057:
	.word	.LANCHOR0
	.word	.LC8
	.word	.LANCHOR0+3456
	.word	.LC104
	.word	.LC105
	.word	.LANCHOR1+363
	.fnend
	.size	FtlMapWritePage, .-FtlMapWritePage
	.align	1
	.global	load_l2p_region
	.syntax unified
	.thumb
	.thumb_func
	.fpu softvfp
	.type	load_l2p_region, %function
load_l2p_region:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	push	{r4, r5, r6, r7, r8, lr}
	.save {r4, r5, r6, r7, r8, lr}
	mov	r6, r0
	ldr	r4, .L1066
	mov	r8, r1
	ldrh	r3, [r4, #336]
	cmp	r3, r0
	bcs	.L1060
	movw	r2, #485
	ldr	r1, .L1066+4
	ldr	r0, .L1066+8
	bl	sftl_printk
.L1060:
	ldr	r3, [r4, #3380]
	movs	r5, #12
	ldr	r7, [r3, r6, lsl #2]
	cbnz	r7, .L1061
	mul	r5, r5, r8
	ldr	r3, [r4, #2540]
	ldrh	r2, [r4, #310]
	movs	r1, #255
	add	r3, r3, r5
	ldr	r0, [r3, #8]
	bl	ftl_memset
	ldr	r3, [r4, #2540]
	strh	r6, [r3, r5]	@ movhi
	ldr	r3, [r4, #2540]
	add	r5, r5, r3
	str	r7, [r5, #4]
.L1062:
	movs	r0, #0
	pop	{r4, r5, r6, r7, r8, pc}
.L1061:
	mul	r5, r5, r8
	ldr	r3, [r4, #2540]
	movs	r2, #1
	ldr	r0, .L1066+12
	mov	r1, r2
	str	r7, [r4, #3460]
	add	r3, r3, r5
	ldr	r3, [r3, #8]
	str	r3, [r4, #3464]
	ldr	r3, [r4, #3336]
	str	r3, [r4, #3468]
	bl	FlashReadPages
	ldr	r8, [r4, #3468]
	ldrh	r3, [r8, #8]
	cmp	r3, r6
	beq	.L1063
	mov	r2, r7
	mov	r1, r6
	ldr	r0, .L1066+16
	bl	sftl_printk
	movs	r3, #4
	ldr	r1, [r4, #3468]
	mov	r2, r3
	ldr	r0, .L1066+20
	bl	rknand_print_hex
	ldrh	r3, [r4, #336]
	movs	r2, #4
	ldr	r1, [r4, #3380]
	ldr	r0, .L1066+24
	bl	rknand_print_hex
.L1064:
	ldrh	r3, [r8, #8]
	cmp	r3, r6
	beq	.L1065
	mov	r2, #508
	ldr	r1, .L1066+4
	ldr	r0, .L1066+8
	bl	sftl_printk
.L1065:
	ldr	r3, [r4, #2540]
	movs	r1, #0
	adds	r2, r3, r5
	str	r1, [r2, #4]
	strh	r6, [r3, r5]	@ movhi
	b	.L1062
.L1063:
	ldr	r3, [r4, #3456]
	cmp	r3, #256
	bne	.L1064
	mov	r2, r7
	mov	r1, r6
	ldr	r0, .L1066+28
	bl	sftl_printk
	ldr	r3, [r4, #2540]
	mov	r1, r6
	ldr	r0, .L1066+32
	add	r3, r3, r5
	ldr	r2, [r3, #8]
	bl	FtlMapWritePage
	b	.L1064
.L1067:
	.align	2
.L1066:
	.word	.LANCHOR0
	.word	.LANCHOR1+379
	.word	.LC8
	.word	.LANCHOR0+3456
	.word	.LC106
	.word	.LC94
	.word	.LC107
	.word	.LC108
	.word	.LANCHOR0+3396
	.fnend
	.size	load_l2p_region, .-load_l2p_region
	.align	1
	.global	ftl_map_blk_gc
	.syntax unified
	.thumb
	.thumb_func
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
	ldr	r6, [r0, #12]
	ldr	r10, [r0, #24]
	bl	ftl_free_no_use_map_blk
	ldrh	r3, [r4, #10]
	ldrh	r2, [r4, #8]
	ldr	r5, .L1084
	subs	r3, r3, #5
	cmp	r2, r3
	blt	.L1069
	uxth	r0, r0
	ldrh	r9, [r6, r0, lsl #1]
	cmp	r9, #0
	beq	.L1069
	ldr	r3, [r4, #32]
	cbnz	r3, .L1069
	movs	r2, #1
	str	r2, [r4, #32]
	strh	r3, [r6, r0, lsl #1]	@ movhi
	ldrh	r3, [r4, #8]
	ldrh	r2, [r4, #2]
	subs	r3, r3, #1
	strh	r3, [r4, #8]	@ movhi
	ldrh	r3, [r5, #304]
	cmp	r2, r3
	bcc	.L1070
	mov	r0, r4
	bl	ftl_map_blk_alloc_new_blk
.L1070:
	movs	r6, #0
.L1071:
	ldrh	r2, [r4, #6]
	uxth	fp, r6
	cmp	r2, fp
	bhi	.L1078
	movs	r1, #1
	mov	r0, r9
	bl	FtlFreeSysBlkQueueIn
	movs	r3, #0
	str	r3, [r4, #32]
.L1069:
	ldrh	r2, [r4, #2]
	ldrh	r3, [r5, #304]
	cmp	r2, r3
	bcc	.L1079
	mov	r0, r4
	bl	ftl_map_blk_alloc_new_blk
.L1079:
	movs	r0, #0
	add	sp, sp, #12
	@ sp needed
	pop	{r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L1078:
	uxth	r7, r6
	ldr	r2, [r10, r7, lsl #2]
	add	r3, r10, r7, lsl #2
	str	r3, [sp, #4]
	cmp	r9, r2, lsr #10
	bne	.L1072
	ldr	r2, [r5, #3308]
	ldr	r8, [r5, #3336]
	ldr	r0, .L1084+4
	str	r2, [r5, #3464]
	str	r8, [r5, #3468]
	ldr	r2, [r10, r7, lsl #2]
	str	r2, [r5, #3460]
	movs	r2, #1
	mov	r1, r2
	bl	FlashReadPages
	ldrh	r2, [r8, #8]
	cmp	r2, fp
	beq	.L1073
	movw	r2, #611
	ldr	r1, .L1084+8
	ldr	r0, .L1084+12
	bl	sftl_printk
.L1073:
	ldr	r2, [r5, #3456]
	adds	r2, r2, #1
	bne	.L1074
.L1076:
	ldr	r2, [sp, #4]
	movs	r3, #0
	str	r3, [r2]
.L1075:
	b	.L1075
.L1074:
	ldrh	r2, [r8, #8]
	cmp	r2, fp
	bne	.L1076
	ldrh	r2, [r8]
	ldrh	r3, [r4, #4]
	cmp	r2, r3
	bne	.L1076
	ldr	r2, [r5, #3464]
	mov	r1, r7
	mov	r0, r4
	bl	FtlMapWritePage
.L1072:
	adds	r6, r6, #1
	b	.L1071
.L1085:
	.align	2
.L1084:
	.word	.LANCHOR0
	.word	.LANCHOR0+3456
	.word	.LANCHOR1+395
	.word	.LC8
	.fnend
	.size	ftl_map_blk_gc, .-ftl_map_blk_gc
	.align	1
	.global	flush_l2p_region
	.syntax unified
	.thumb
	.thumb_func
	.fpu softvfp
	.type	flush_l2p_region, %function
flush_l2p_region:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	push	{r3, r4, r5, lr}
	.save {r3, r4, r5, lr}
	movs	r4, #12
	ldr	r5, .L1087
	muls	r4, r0, r4
	ldr	r3, [r5, #2540]
	addw	r0, r5, #3396
	adds	r2, r3, r4
	ldrh	r1, [r3, r4]
	ldr	r2, [r2, #8]
	bl	FtlMapWritePage
	ldr	r3, [r5, #2540]
	movs	r0, #0
	add	r4, r4, r3
	ldr	r3, [r4, #4]
	bic	r3, r3, #-2147483648
	str	r3, [r4, #4]
	pop	{r3, r4, r5, pc}
.L1088:
	.align	2
.L1087:
	.word	.LANCHOR0
	.fnend
	.size	flush_l2p_region, .-flush_l2p_region
	.align	1
	.global	log2phys
	.syntax unified
	.thumb
	.thumb_func
	.fpu softvfp
	.type	log2phys, %function
log2phys:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	push	{r3, r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.save {r3, r4, r5, r6, r7, r8, r9, r10, fp, lr}
	mov	r9, r0
	ldr	r4, .L1102
	mov	r5, r1
	mov	r7, r2
	ldr	r3, [r4, #2556]
	ldrh	r10, [r4, #308]
	cmp	r0, r3
	bcc	.L1090
	movw	r2, #813
	ldr	r1, .L1102+4
	ldr	r0, .L1102+8
	bl	sftl_printk
.L1090:
	ldr	r3, [r4, #2556]
	cmp	r9, r3
	bcs	.L1091
	add	r10, r10, #7
	ldrh	r2, [r4, #338]
	lsr	r6, r9, r10
	ldr	r1, [r4, #2540]
	movs	r3, #0
	uxth	r6, r6
	mov	fp, #12
.L1092:
	uxth	r8, r3
	cmp	r8, r2
	bcc	.L1097
	bl	select_l2p_ram_region
	mul	fp, fp, r0
	ldr	r3, [r4, #2540]
	mov	r8, r0
	ldrh	r1, [r3, fp]
	add	r2, r3, fp
	movw	r3, #65535
	cmp	r1, r3
	beq	.L1098
	ldr	r3, [r2, #4]
	cmp	r3, #0
	bge	.L1098
	bl	flush_l2p_region
.L1098:
	mov	r1, r8
	mov	r0, r6
	bl	load_l2p_region
	b	.L1094
.L1091:
	mov	r0, #-1
	cbnz	r7, .L1089
	str	r0, [r5]
	pop	{r3, r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L1097:
	adds	r3, r3, #1
	mla	r0, fp, r3, r1
	ldrh	r0, [r0, #-12]
	cmp	r0, r6
	bne	.L1092
.L1094:
	movs	r0, #1
	movs	r3, #12
	lsl	r0, r0, r10
	subs	r0, r0, #1
	and	r0, r0, r9
	uxth	r0, r0
	cbnz	r7, .L1095
	ldr	r2, [r4, #2540]
	mla	r3, r3, r8, r2
	ldr	r3, [r3, #8]
	ldr	r3, [r3, r0, lsl #2]
	str	r3, [r5]
.L1096:
	ldr	r2, [r4, #2540]
	movs	r3, #12
	mla	r8, r3, r8, r2
	ldr	r3, [r8, #4]
	adds	r2, r3, #1
	beq	.L1100
	adds	r3, r3, #1
	str	r3, [r8, #4]
.L1100:
	movs	r0, #0
.L1089:
	pop	{r3, r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L1095:
	mul	r3, r3, r8
	ldr	r2, [r4, #2540]
	ldr	r1, [r5]
	add	r2, r2, r3
	ldr	r2, [r2, #8]
	str	r1, [r2, r0, lsl #2]
	ldr	r2, [r4, #2540]
	strh	r6, [r4, #2544]	@ movhi
	add	r3, r3, r2
	ldr	r2, [r3, #4]
	orr	r2, r2, #-2147483648
	str	r2, [r3, #4]
	b	.L1096
.L1103:
	.align	2
.L1102:
	.word	.LANCHOR0
	.word	.LANCHOR1+410
	.word	.LC8
	.fnend
	.size	log2phys, .-log2phys
	.align	1
	.global	FtlWriteDump_data
	.syntax unified
	.thumb
	.thumb_func
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
	ldr	r4, .L1123
	ldr	r7, .L1123+4
	ldrh	r2, [r4, #28]
	ldr	r3, [r7]
	str	r3, [sp, #28]
	cmp	r2, #0
	beq	.L1105
	ldrb	r3, [r4, #32]	@ zero_extendqisi2
	cmp	r3, #0
	bne	.L1105
	ldrb	r1, [r4, #31]	@ zero_extendqisi2
	ldrh	r3, [r4, #302]
	muls	r3, r1, r3
	cmp	r2, r3
	beq	.L1105
	ldrb	r8, [r4, #34]	@ zero_extendqisi2
	cmp	r8, #0
	bne	.L1104
	ldr	r6, [r4, #2556]
	mov	r2, r8
	add	r1, sp, #4
	ldrh	r9, [r4, #232]
	subs	r6, r6, #1
	mov	r0, r6
	bl	log2phys
	ldr	r3, [sp, #4]
	ldr	r5, [r4, #3336]
	ldr	r0, [r4, #3304]
	str	r3, [sp, #12]
	adds	r3, r3, #1
	str	r6, [sp, #24]
	str	r0, [sp, #16]
	str	r5, [sp, #20]
	str	r8, [r5, #4]
	beq	.L1107
	mov	r2, r8
	movs	r1, #1
	add	r0, sp, #8
	bl	FlashReadPages
.L1108:
	ldr	r10, .L1123+8
	mov	r8, #0
	lsl	r9, r9, #2
	mov	fp, r8
	movw	r3, #61589
	strh	r3, [r5]	@ movhi
.L1109:
	cmp	r9, r8
	bne	.L1113
.L1110:
	movs	r3, #1
.L1122:
	strb	r3, [r4, #34]
.L1104:
	ldr	r2, [sp, #28]
	ldr	r3, [r7]
	cmp	r2, r3
	beq	.L1115
	bl	__stack_chk_fail
.L1107:
	ldrh	r2, [r4, #310]
	movs	r1, #255
	bl	ftl_memset
	b	.L1108
.L1113:
	ldrh	r3, [r4, #28]
	cmp	r3, #0
	beq	.L1110
	ldr	r3, [sp, #12]
	mov	r0, r10
	str	r6, [r5, #8]
	add	r8, r8, #1
	str	r3, [r5, #12]
	ldrh	r3, [r4, #24]
	strh	r3, [r5, #2]	@ movhi
	bl	get_new_active_ppa
	ldr	r3, [r4, #2596]
	movs	r1, #1
	str	r0, [sp, #12]
	add	r0, sp, #8
	str	r3, [r5, #4]
	adds	r3, r3, #1
	adds	r2, r3, #1
	it	eq
	moveq	r3, fp
	str	r3, [r4, #2596]
	movs	r3, #0
	mov	r2, r3
	bl	FlashProgPages
	ldrh	r0, [r4, #24]
	bl	decrement_vpc_count
	b	.L1109
.L1105:
	movs	r3, #0
	b	.L1122
.L1115:
	add	sp, sp, #36
	@ sp needed
	pop	{r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L1124:
	.align	2
.L1123:
	.word	.LANCHOR0
	.word	__stack_chk_guard
	.word	.LANCHOR0+24
	.fnend
	.size	FtlWriteDump_data, .-FtlWriteDump_data
	.align	1
	.global	FtlReUsePrevPpa
	.syntax unified
	.thumb
	.thumb_func
	.fpu softvfp
	.type	FtlReUsePrevPpa, %function
FtlReUsePrevPpa:
	.fnstart
	@ args = 0, pretend = 0, frame = 8
	@ frame_needed = 0, uses_anonymous_args = 0
	push	{r0, r1, r2, r4, r5, r6, r7, lr}
	.save {r4, r5, r6, r7, lr}
	.pad #12
	mov	r6, r0
	ldr	r5, .L1135
	ubfx	r0, r1, #10, #16
	str	r1, [sp, #4]
	bl	P2V_block_in_plane
	ldr	r2, [r5, #72]
	mov	r7, r0
	ldrh	r3, [r2, r0, lsl #1]
	cbnz	r3, .L1126
	ldr	r4, [r5, #2532]
	cbz	r4, .L1127
	ldr	r1, [r5, #2516]
	mov	ip, #6
	ldr	r2, .L1135+4
	movw	lr, #65535
	ldrh	r0, [r5, #224]
	subs	r4, r4, r1
	asrs	r4, r4, #1
	muls	r4, r2, r4
	uxth	r4, r4
.L1128:
	uxth	r2, r3
	cmp	r0, r2
	bls	.L1127
	cmp	r4, r7
	bne	.L1129
	mov	r1, r4
	ldr	r0, .L1135+8
	bl	List_remove_node
	ldrh	r3, [r5, #224]
	cbnz	r3, .L1130
	movw	r2, #1735
	ldr	r1, .L1135+12
	ldr	r0, .L1135+16
	bl	sftl_printk
.L1130:
	ldrh	r3, [r5, #224]
	mov	r0, r4
	subs	r3, r3, #1
	strh	r3, [r5, #224]	@ movhi
	bl	INSERT_DATA_LIST
	ldr	r2, [r5, #72]
	ldrh	r3, [r2, r7, lsl #1]
.L1126:
	adds	r3, r3, #1
	strh	r3, [r2, r7, lsl #1]	@ movhi
	b	.L1127
.L1129:
	mul	r4, ip, r4
	adds	r3, r3, #1
	ldrh	r4, [r1, r4]
	cmp	r4, lr
	bne	.L1128
.L1127:
	movs	r2, #1
	add	r1, sp, #4
	mov	r0, r6
	bl	log2phys
	add	sp, sp, #12
	@ sp needed
	pop	{r4, r5, r6, r7, pc}
.L1136:
	.align	2
.L1135:
	.word	.LANCHOR0
	.word	-1431655765
	.word	.LANCHOR0+2532
	.word	.LANCHOR1+419
	.word	.LC8
	.fnend
	.size	FtlReUsePrevPpa, .-FtlReUsePrevPpa
	.align	1
	.global	FtlRecoverySuperblock
	.syntax unified
	.thumb
	.thumb_func
	.fpu softvfp
	.type	FtlRecoverySuperblock, %function
FtlRecoverySuperblock:
	.fnstart
	@ args = 0, pretend = 0, frame = 48
	@ frame_needed = 0, uses_anonymous_args = 0
	ldr	r3, .L1274
	movw	r2, #65535
	push	{r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.save {r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.pad #52
	sub	sp, sp, #52
	mov	r10, r0
	ldr	r3, [r3]
	str	r3, [sp, #44]
	ldrh	r3, [r0]
	cmp	r3, r2
	beq	.L1139
	ldrh	r3, [r0, #2]
	ldr	r4, .L1274+4
	str	r3, [sp]
	ldrb	r3, [r0, #6]	@ zero_extendqisi2
	ldr	r1, [sp]
	str	r3, [sp, #20]
	ldrh	r3, [r4, #302]
	cmp	r3, r1
	mov	r3, #0
	bne	.L1140
	strh	r3, [r0, #4]	@ movhi
.L1272:
	strb	r3, [r10, #6]
.L1139:
	ldr	r3, .L1274
	movs	r0, #0
	ldr	r2, [sp, #44]
	ldr	r3, [r3]
	cmp	r2, r3
	beq	.L1211
	bl	__stack_chk_fail
.L1140:
	ldrh	r0, [r0, #16]
.L1141:
	cmp	r0, r2
	uxth	r6, r3
	add	r3, r3, #1
	beq	.L1142
	movs	r1, #1
	bl	FtlGetLastWrittenPage
	adds	r7, r0, #1
	mov	r5, r0
	beq	.L1143
	ldr	r3, [r4, #3180]
	movs	r2, #0
	ldrh	fp, [r4, #232]
	movw	r9, #65535
	ldr	r0, [r4, #3292]
	mov	lr, #20
	str	r3, [sp, #4]
	ldrh	r3, [r4, #310]
	ldr	r8, [r4, #3184]
	ldrh	r7, [r4, #312]
	mov	r4, r2
	str	r3, [sp, #8]
	add	r3, r10, #16
	mov	ip, r3
	str	r3, [sp, #16]
.L1144:
	uxth	r3, r2
	cmp	fp, r3
	bhi	.L1150
	ldr	r6, .L1274+4
	movs	r2, #0
	mov	r1, r4
	movw	r9, #65535
	bl	FlashReadPages
	ldr	r3, [r6, #2596]
	uxth	r2, r5
	ldr	r8, [r6, #3292]
	str	r2, [sp, #8]
	add	fp, r3, #-1
	movs	r3, #0
	mov	r7, r8
.L1151:
	uxth	r2, r3
	cmp	r4, r2
	bhi	.L1156
	bne	.L1154
	adds	r3, r5, #1
	uxth	r3, r3
	str	r3, [sp, #8]
.L1267:
	ldr	r0, [r8, #4]
	ubfx	r0, r0, #10, #16
	bl	P2V_plane
	ldrh	r3, [r6, #302]
	ldr	r2, [sp, #8]
	str	r0, [sp, #4]
	cmp	r3, r2
	bne	.L1158
	ldrh	r3, [sp, #8]
	strh	r3, [r10, #2]	@ movhi
	movs	r3, #0
	strb	r3, [r10, #6]
	strh	r3, [r10, #4]	@ movhi
.L1158:
	ldr	r3, [sp, #8]
	ldr	r2, [sp]
	cmp	r3, r2
	bne	.L1159
	ldr	r3, [sp, #4]
	ldr	r2, [sp, #20]
	cmp	r3, r2
	bne	.L1159
	ldr	r1, [sp, #8]
	mov	r2, r3
.L1273:
	mov	r0, r10
	bl	ftl_sb_update_avl_pages
	b	.L1139
.L1142:
	uxth	r1, r3
	adds	r1, r1, #8
	ldrh	r0, [r10, r1, lsl #1]
	b	.L1141
.L1143:
	ldr	r3, [sp]
	cbz	r3, .L1145
	movw	r2, #1802
	ldr	r1, .L1274+8
	ldr	r0, .L1274+12
	bl	sftl_printk
.L1145:
	ldr	r3, [sp, #20]
	cbz	r3, .L1146
	cmp	r6, r3
	beq	.L1146
	movw	r2, #1803
	ldr	r1, .L1274+8
	ldr	r0, .L1274+12
	bl	sftl_printk
.L1146:
	movs	r3, #0
	strh	r3, [r10, #2]	@ movhi
	b	.L1272
.L1150:
	ldrh	r3, [ip], #2
	cmp	r3, r9
	beq	.L1147
	mla	r1, lr, r4, r0
	orr	r3, r5, r3, lsl #10
	str	r3, [r1, #4]
	ldr	r3, [sp, #8]
	muls	r3, r4, r3
	it	mi
	addmi	r3, r3, #3
	bic	r3, r3, #3
	mov	r6, r3
	ldr	r3, [sp, #4]
	add	r6, r6, r3
	mov	r3, r7
	muls	r3, r4, r3
	add	r4, r4, #1
	it	mi
	addmi	r3, r3, #3
	uxth	r4, r4
	bic	r3, r3, #3
	str	r6, [r1, #8]
	add	r3, r3, r8
	str	r3, [r1, #12]
.L1147:
	adds	r2, r2, #1
	b	.L1144
.L1156:
	ldr	r2, [r7]
	cbnz	r2, .L1152
	ldr	r2, [r7, #12]
	str	r2, [sp, #4]
	ldr	r2, [r2, #4]
	adds	r0, r2, #1
	beq	.L1153
	ldr	r1, [r6, #2596]
	mov	r0, r2
	bl	ftl_cmp_data_ver
	cbz	r0, .L1153
	adds	r2, r2, #1
	str	r2, [r6, #2596]
.L1153:
	ldr	r2, [sp, #4]
	ldr	r2, [r2]
	adds	r2, r2, #1
	bne	.L1155
.L1154:
	uxth	r2, r5
	uxth	r3, r3
	str	r2, [sp, #8]
	movs	r2, #20
	mla	r8, r2, r3, r8
	b	.L1267
.L1152:
	ldr	r9, [sp, #8]
.L1155:
	adds	r3, r3, #1
	adds	r7, r7, #20
	b	.L1151
.L1159:
	movw	r3, #65535
	cmp	r9, r3
	bne	.L1160
	ldrb	r3, [r10, #8]	@ zero_extendqisi2
	cmp	r3, #0
	bne	.L1161
.L1160:
	ldr	r3, [r6, #3452]
	uxth	r7, r5
	uxth	r5, r5
	movw	r8, #65535
	adds	r3, r3, #1
	ldr	r3, [sp]
	it	eq
	streq	fp, [r6, #3452]
	ldr	r6, .L1274+4
	adds	r3, r3, #7
	cmp	r5, r3
	itet	gt
	subgt	r4, r7, #7
	ldrle	r4, [sp]
	uxthgt	r4, r4
.L1164:
	cmp	r4, r7
	bhi	.L1171
	movs	r3, #0
	ldrh	lr, [r6, #232]
	ldr	r0, [r6, #3292]
	mov	r5, r3
	ldr	r1, [sp, #16]
	mov	ip, #20
	b	.L1172
.L1166:
	ldrh	r2, [r1], #2
	cmp	r2, r8
	beq	.L1165
	mla	r9, ip, r5, r0
	adds	r5, r5, #1
	orr	r2, r4, r2, lsl #10
	uxth	r5, r5
	str	r2, [r9, #4]
.L1165:
	adds	r3, r3, #1
.L1172:
	uxth	r2, r3
	cmp	lr, r2
	bhi	.L1166
	movs	r2, #0
	mov	r1, r5
	bl	FlashReadPages
	ldr	r3, [r6, #3292]
	movs	r2, #20
	mla	r5, r2, r5, r3
.L1167:
	cmp	r5, r3
	bne	.L1170
	adds	r4, r4, #1
	uxth	r4, r4
	b	.L1164
.L1170:
	ldr	r2, [r3]
	cbnz	r2, .L1161
	ldr	r2, [r3, #12]
	ldrh	r1, [r2]
	cmp	r1, r8
	beq	.L1169
	ldr	r2, [r2, #4]
	adds	r1, r2, #1
	it	ne
	strne	r2, [r6, #3452]
.L1169:
	adds	r3, r3, #20
	b	.L1167
.L1275:
	.align	2
.L1274:
	.word	__stack_chk_guard
	.word	.LANCHOR0
	.word	.LANCHOR1+435
	.word	.LC8
.L1171:
	mov	r3, #-1
	str	r3, [r6, #3452]
.L1161:
	ldr	r8, .L1276+16
	movs	r3, #1
	ldr	r5, [sp]
	mov	r4, r8
	strh	r3, [r8, #3476]	@ movhi
.L1173:
	ldrh	r6, [r4, #232]
	movw	r9, #65535
	ldr	r0, [r4, #3292]
	mov	ip, #20
	ldr	r1, [sp, #16]
	movs	r3, #0
	str	r3, [sp, #12]
.L1174:
	uxth	r2, r3
	cmp	r6, r2
	bhi	.L1176
	movs	r2, #0
	ldr	r1, [sp, #12]
	bl	FlashReadPages
	movs	r3, #0
.L1271:
	str	r3, [sp, #24]
	ldr	r2, [sp, #12]
	ldrh	r3, [sp, #24]
	cmp	r2, r3
	bhi	.L1206
	adds	r5, r5, #1
	ldrh	r3, [r4, #302]
	uxth	r5, r5
	cmp	r3, r5
	bne	.L1173
	ldrh	r2, [r4, #232]
	movw	r0, #65535
	movs	r3, #0
	strh	r5, [r10, #2]	@ movhi
	strh	r3, [r10, #4]	@ movhi
.L1207:
	uxth	r1, r3
	cmp	r1, r2
	bcs	.L1139
	ldr	r1, [sp, #16]
	ldrh	r4, [r1], #2
	cmp	r4, r0
	str	r1, [sp, #16]
	add	r1, r3, #1
	bne	.L1272
	mov	r3, r1
	b	.L1207
.L1176:
	ldrh	r2, [r1], #2
	cmp	r2, r9
	beq	.L1175
	ldr	r7, [sp, #12]
	orr	r2, r5, r2, lsl #10
	mla	lr, ip, r7, r0
	str	r2, [lr, #4]
	mov	r2, r7
	adds	r2, r2, #1
	uxth	r2, r2
	str	r2, [sp, #12]
.L1175:
	adds	r3, r3, #1
	b	.L1174
.L1206:
	ldr	r3, [sp, #24]
	movs	r7, #20
	muls	r7, r3, r7
	ldr	r3, [r4, #3292]
	str	r3, [sp, #28]
	add	r9, r3, r7
	ldr	r6, [r9, #4]
	ubfx	r0, r6, #10, #16
	str	r6, [sp, #40]
	bl	P2V_plane
	ldr	r3, [sp]
	cmp	r5, r3
	bcc	.L1178
	ldr	r3, [sp, #28]
	bne	.L1179
	ldr	r2, [sp, #20]
	cmp	r2, r0
	bhi	.L1178
.L1179:
	ldr	r2, [sp, #8]
	cmp	r5, r2
	bne	.L1180
	ldr	r2, [sp, #4]
	cmp	r2, r0
	beq	.L1181
.L1180:
	ldr	r3, [r3, r7]
	adds	r3, r3, #1
	beq	.L1182
	ldr	r9, [r9, #12]
	movw	r3, #61589
	ldrh	r2, [r9]
	cmp	r2, r3
	beq	.L1183
	ldrh	r0, [r10]
.L1268:
	bl	decrement_vpc_count
	b	.L1178
.L1183:
	ldr	fp, [r9, #4]
	cmp	fp, #-1
	beq	.L1184
	ldr	r1, [r4, #2596]
	mov	r0, fp
	bl	ftl_cmp_data_ver
	cbz	r0, .L1184
	add	r3, fp, #1
	str	r3, [r4, #2596]
.L1184:
	ldrh	r2, [r9]
	movw	r3, #61589
	cmp	r2, r3
	beq	.L1185
	movw	r2, #1954
	ldr	r1, .L1276
	ldr	r0, .L1276+4
	bl	sftl_printk
.L1185:
	ldr	r6, [r9, #8]
	add	r1, sp, #36
	ldr	r3, [r9, #12]
	movs	r2, #0
	mov	r0, r6
	str	r3, [sp, #32]
	bl	log2phys
	ldr	r1, [r4, #3452]
	adds	r0, r1, #1
	beq	.L1186
	mov	r0, fp
	bl	ftl_cmp_data_ver
	cmp	r0, #0
	beq	.L1186
	ldr	r3, [sp, #32]
	adds	r2, r3, #1
	beq	.L1187
	ldr	r0, [r4, #3292]
	movs	r2, #0
	movs	r1, #1
	add	r0, r0, r7
	str	r3, [r0, #4]
	ldr	r9, [r0, #12]
	bl	FlashReadPages
	ldr	r2, [r4, #3292]
	ldr	r1, [r2, r7]
	adds	r3, r2, r7
	adds	r1, r1, #1
	bne	.L1188
.L1189:
	mov	r3, #-1
	str	r3, [sp, #32]
.L1196:
	ldr	r0, [sp, #32]
	adds	r1, r0, #1
	beq	.L1178
.L1210:
	ubfx	r0, r0, #10, #16
	bl	P2V_block_in_plane
	ldrh	r3, [r4, #240]
	mov	r6, r0
	cmp	r3, r0
	bhi	.L1202
	movw	r2, #2059
	ldr	r1, .L1276
	ldr	r0, .L1276+4
	bl	sftl_printk
.L1202:
	ldr	r3, [r8, #72]
	ldrh	r3, [r3, r6, lsl #1]
	cmp	r3, #0
	beq	.L1203
	mov	r0, r6
	b	.L1268
.L1187:
	ldr	r3, [sp, #40]
	ldr	r2, [sp, #36]
	cmp	r2, r3
	bne	.L1178
	movs	r2, #1
	add	r1, sp, #32
	mov	r0, r6
	bl	log2phys
.L1178:
	ldr	r3, [sp, #24]
	adds	r3, r3, #1
	b	.L1271
.L1188:
	ldr	r1, [r9, #8]
	cmp	r6, r1
	bne	.L1189
	ldr	r1, [r9, #4]
	ldr	r0, [r4, #3452]
	str	r1, [sp, #28]
	bl	ftl_cmp_data_ver
	cmp	r0, #0
	beq	.L1189
	ldr	r1, [sp, #36]
	ldr	r0, [sp, #40]
	cmp	r1, r0
	bne	.L1191
.L1194:
	ldr	r1, [sp, #32]
	mov	r0, r6
	bl	FtlReUsePrevPpa
	b	.L1189
.L1191:
	ldr	r0, [sp, #32]
	cmp	r1, r0
	beq	.L1189
	adds	r0, r1, #1
	beq	.L1192
	str	r1, [r3, #4]
	movs	r2, #0
	movs	r1, #1
	mov	r0, r3
	ldr	r9, [r3, #12]
	bl	FlashReadPages
.L1193:
	ldr	r3, [r4, #3292]
	ldr	r3, [r3, r7]
	adds	r3, r3, #1
	beq	.L1194
	ldr	r3, [r9, #4]
	ldr	r0, [r4, #3452]
	mov	r1, r3
	bl	ftl_cmp_data_ver
	cmp	r0, #0
	beq	.L1194
	mov	r1, r3
	ldr	r0, [sp, #28]
	bl	ftl_cmp_data_ver
	cmp	r0, #0
	beq	.L1189
	b	.L1194
.L1192:
	str	r1, [r2, r7]
	b	.L1193
.L1186:
	ldr	r3, [sp, #40]
	ldr	r2, [sp, #36]
	cmp	r2, r3
	beq	.L1196
	ldr	r1, [sp, #32]
	adds	r0, r1, #1
	beq	.L1198
	ldr	r3, [r8, #248]
	cmp	r3, r1, lsr #10
	bhi	.L1198
	ldr	r0, .L1276+8
.L1270:
	bl	sftl_printk
	b	.L1178
.L1198:
	movs	r2, #1
	add	r1, sp, #40
	mov	r0, r6
	bl	log2phys
	ldr	r9, [sp, #36]
	cmp	r9, #-1
	beq	.L1196
	ldr	r3, [sp, #32]
	cmp	r9, r3
	beq	.L1200
	ubfx	r0, r9, #10, #16
	bl	P2V_block_in_plane
	ldrh	r3, [r4, #24]
	cmp	r3, r0
	beq	.L1201
	ldrh	r3, [r4, #76]
	cmp	r3, r0
	beq	.L1201
	ldrh	r3, [r4, #124]
	cmp	r3, r0
	bne	.L1196
.L1201:
	ldr	r0, [r8, #3292]
	movs	r2, #0
	movs	r1, #1
	str	r9, [r0, #4]
	ldr	r7, [r0, #12]
	bl	FlashReadPages
	ldr	r3, [r8, #3292]
	ldr	r3, [r3]
	adds	r3, r3, #1
	beq	.L1196
	ldr	r1, [r7, #4]
	mov	r0, fp
	bl	ftl_cmp_data_ver
	cmp	r0, #0
	bne	.L1196
	movs	r2, #1
	add	r1, sp, #36
	mov	r0, r6
	bl	log2phys
	b	.L1196
.L1203:
	mov	r1, r6
	ldr	r0, .L1276+12
	b	.L1270
.L1182:
	ldr	r3, [r4, #3480]
	cmp	r3, #31
	itttt	ls
	addls	r2, r4, r3, lsl #2
	addls	r3, r3, #1
	strls	r3, [r4, #3480]
	strls	r6, [r2, #3484]
	ldrh	r0, [r10]
	bl	decrement_vpc_count
	ldr	r3, [r4, #3452]
	adds	r2, r3, #1
	bne	.L1205
.L1269:
	str	fp, [r4, #3452]
	b	.L1178
.L1205:
	cmp	fp, r3
	bcs	.L1178
	b	.L1269
.L1181:
	ldrb	r3, [sp, #4]	@ zero_extendqisi2
	mov	r1, r5
	strh	r5, [r10, #2]	@ movhi
	ldr	r2, [sp, #4]
	strb	r3, [r10, #6]
	b	.L1273
.L1200:
	mov	r0, r9
	b	.L1210
.L1211:
	add	sp, sp, #52
	@ sp needed
	pop	{r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L1277:
	.align	2
.L1276:
	.word	.LANCHOR1+435
	.word	.LC8
	.word	.LC109
	.word	.LC110
	.word	.LANCHOR0
	.fnend
	.size	FtlRecoverySuperblock, .-FtlRecoverySuperblock
	.align	1
	.global	ftl_check_vpc
	.syntax unified
	.thumb
	.thumb_func
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
	ldr	r5, .L1303
	movs	r4, #0
	ldr	r6, .L1303+4
	ldr	r3, [r5]
	ldr	r1, .L1303+8
	ldr	r0, .L1303+12
	ldr	r7, .L1303+16
	str	r3, [sp, #12]
	bl	sftl_printk
	mov	r2, #8192
	movs	r1, #0
	ldr	r0, .L1303+16
	bl	ftl_memset
	str	r5, [sp, #4]
	mov	r5, r6
.L1279:
	ldr	r3, [r6, #2556]
	cmp	r4, r3
	bcc	.L1281
	ldr	r8, .L1303+16
	movs	r4, #0
	ldr	r10, .L1303+28
	mov	r6, r4
	movw	r9, #65535
.L1282:
	ldrh	r2, [r5, #240]
	uxth	r3, r4
	cmp	r2, r3
	bhi	.L1284
	ldr	r4, [r5, #2532]
	cbz	r4, .L1285
	ldr	r3, [r5, #2516]
	movs	r7, #0
	ldrh	r8, [r5, #224]
	mov	fp, #6
	ldr	r9, .L1303+16
	subs	r4, r4, r3
	ldr	r3, .L1303+20
	asrs	r4, r4, #1
	ldr	r10, .L1303+32
	muls	r4, r3, r4
	uxth	r4, r4
.L1286:
	uxth	r3, r7
	cmp	r8, r3
	bls	.L1285
	ldr	r3, [r5, #72]
	ldrh	r2, [r3, r4, lsl #1]
	cbz	r2, .L1287
	movs	r6, #1
	ldrh	r3, [r9, r4, lsl #1]
	mov	r1, r4
	mov	r0, r10
	bl	sftl_printk
.L1287:
	mul	r4, fp, r4
	ldr	r3, [r5, #2516]
	adds	r7, r7, #1
	ldrh	r4, [r3, r4]
	movw	r3, #65535
	cmp	r4, r3
	bne	.L1286
.L1285:
	cbz	r6, .L1278
	movw	r2, #2387
	ldr	r1, .L1303+8
	ldr	r0, .L1303+24
	bl	sftl_printk
.L1278:
	ldr	r3, [sp, #4]
	ldr	r2, [sp, #12]
	ldr	r3, [r3]
	cmp	r2, r3
	beq	.L1290
	bl	__stack_chk_fail
.L1281:
	movs	r2, #0
	add	r1, sp, #8
	mov	r0, r4
	bl	log2phys
	ldr	r0, [sp, #8]
	adds	r3, r0, #1
	beq	.L1280
	ubfx	r0, r0, #10, #16
	bl	P2V_block_in_plane
	ldrh	r3, [r7, r0, lsl #1]
	adds	r3, r3, #1
	strh	r3, [r7, r0, lsl #1]	@ movhi
.L1280:
	adds	r4, r4, #1
	b	.L1279
.L1284:
	ldr	r3, [r5, #72]
	uxth	r7, r4
	ldrh	r2, [r3, r7, lsl #1]
	ldrh	r3, [r8, r7, lsl #1]
	cmp	r2, r3
	beq	.L1283
	mov	r1, r7
	mov	r0, r10
	bl	sftl_printk
	ldr	r3, [r5, #72]
	ldrh	r3, [r3, r7, lsl #1]
	cmp	r3, r9
	beq	.L1283
	ldrh	r2, [r8, r7, lsl #1]
	cmp	r2, r3
	it	hi
	movhi	r6, #1
.L1283:
	adds	r4, r4, #1
	b	.L1282
.L1290:
	add	sp, sp, #20
	@ sp needed
	pop	{r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L1304:
	.align	2
.L1303:
	.word	__stack_chk_guard
	.word	.LANCHOR0
	.word	.LANCHOR1+457
	.word	.LC111
	.word	check_vpc_table
	.word	-1431655765
	.word	.LC8
	.word	.LC112
	.word	.LC113
	.fnend
	.size	ftl_check_vpc, .-ftl_check_vpc
	.align	1
	.global	ftl_scan_all_data
	.syntax unified
	.thumb
	.thumb_func
	.fpu softvfp
	.type	ftl_scan_all_data, %function
ftl_scan_all_data:
	.fnstart
	@ args = 0, pretend = 0, frame = 8
	@ frame_needed = 0, uses_anonymous_args = 0
	push	{r4, r5, r6, r7, r8, r9, lr}
	.save {r4, r5, r6, r7, r8, r9, lr}
	movs	r5, #0
	ldr	r6, .L1320
	.pad #36
	sub	sp, sp, #36
	movs	r1, #0
	ldr	r7, .L1320+4
	ldr	r9, .L1320+20
	ldr	r3, [r6]
	mov	r4, r7
	ldr	r0, .L1320+8
	str	r3, [sp, #28]
	bl	sftl_printk
.L1306:
	ldr	r3, [r7, #2556]
	cmp	r5, r3
	bcc	.L1312
	ldr	r2, [sp, #28]
	ldr	r3, [r6]
	cmp	r2, r3
	beq	.L1313
	bl	__stack_chk_fail
.L1312:
	movs	r2, #0
	add	r1, sp, #24
	mov	r0, r5
	bl	log2phys
	ubfx	r3, r5, #0, #11
	cbnz	r3, .L1307
	ldr	r2, [sp, #24]
	mov	r1, r5
	mov	r0, r9
	bl	sftl_printk
.L1307:
	ldr	r3, [sp, #24]
	adds	r2, r3, #1
	beq	.L1309
	str	r3, [r4, #3460]
	movs	r2, #0
	ldr	r3, [r4, #3304]
	movs	r1, #1
	ldr	r8, [r4, #3336]
	ldr	r0, .L1320+12
	str	r3, [r4, #3464]
	str	r5, [r4, #3472]
	str	r8, [r4, #3468]
	str	r2, [r4, #3456]
	bl	FlashReadPages
	ldr	r3, [r4, #3456]
	cmp	r3, #256
	beq	.L1310
	adds	r3, r3, #1
	beq	.L1310
	ldr	r3, [r8, #8]
	cmp	r5, r3
	beq	.L1309
.L1310:
	ldr	r2, [r4, #3464]
	ldr	r3, [r4, #3468]
	ldr	r0, .L1320+16
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
.L1309:
	adds	r5, r5, #1
	b	.L1306
.L1313:
	add	sp, sp, #36
	@ sp needed
	pop	{r4, r5, r6, r7, r8, r9, pc}
.L1321:
	.align	2
.L1320:
	.word	__stack_chk_guard
	.word	.LANCHOR0
	.word	.LC114
	.word	.LANCHOR0+3456
	.word	.LC116
	.word	.LC115
	.fnend
	.size	ftl_scan_all_data, .-ftl_scan_all_data
	.align	1
	.global	FtlGcScanTempBlk
	.syntax unified
	.thumb
	.thumb_func
	.fpu softvfp
	.type	FtlGcScanTempBlk, %function
FtlGcScanTempBlk:
	.fnstart
	@ args = 0, pretend = 0, frame = 48
	@ frame_needed = 0, uses_anonymous_args = 0
	ldr	r3, .L1360
	push	{r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.save {r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.pad #52
	sub	sp, sp, #52
	mov	r5, r0
	str	r1, [sp, #4]
	ldr	r3, [r3]
	str	r3, [sp, #44]
	ldr	r3, .L1360+4
	ldrh	r6, [r3, #4]
	movw	r3, #65535
	cmp	r6, r3
	beq	.L1350
	cbnz	r6, .L1323
.L1324:
	bl	FtlGcPageVarInit
	b	.L1325
.L1350:
	movs	r6, #0
.L1323:
	ldr	r3, .L1360+8
	ldr	r2, [sp, #4]
	ldrh	r3, [r3, #302]
	cmp	r3, r2
	beq	.L1324
.L1325:
	mov	fp, #0
.L1343:
	ldrh	r2, [r5]
	movs	r3, #0
	strb	r3, [r5, #8]
	movw	r3, #65535
	cmp	r2, r3
	beq	.L1326
	ldr	r4, .L1360+8
.L1346:
	ldr	r3, [r4, #3292]
	movs	r2, #0
	ldrh	ip, [r4, #232]
	add	lr, r5, #16
	ldrh	r10, [r4, #310]
	mov	r7, r2
	str	r3, [sp]
	movw	r8, #65535
	ldr	r3, [r4, #3180]
	ldr	r9, [r4, #3184]
	str	r3, [sp, #8]
	ldrh	r3, [r4, #312]
	str	r3, [sp, #12]
.L1327:
	uxth	r3, r2
	cmp	ip, r3
	bhi	.L1331
	mov	r10, #0
	movs	r2, #0
	mov	r1, r7
	ldr	r0, [sp]
	bl	FlashReadPages
.L1332:
	uxth	r3, r10
	cmp	r7, r3
	bhi	.L1344
	ldr	r3, [sp, #4]
	add	fp, fp, #1
	adds	r6, r6, #1
	uxth	r6, r6
	cmp	r3, fp
	bls	.L1345
.L1347:
	ldrh	r3, [r4, #302]
	cmp	r3, r6
	bhi	.L1346
.L1326:
	ldr	r3, .L1360+4
	movw	r2, #65535
	strh	r6, [r5, #2]	@ movhi
	mov	r1, r6
	mov	r0, r5
	strh	r2, [r3, #4]	@ movhi
	movs	r2, #0
	strb	r2, [r5, #6]
	bl	ftl_sb_update_avl_pages
	b	.L1348
.L1331:
	ldrh	r3, [lr], #2
	cmp	r3, r8
	beq	.L1328
	ldr	r0, [sp]
	movs	r1, #20
	orr	r3, r6, r3, lsl #10
	mla	r1, r1, r7, r0
	str	r3, [r1, #4]
	mov	r3, r10
	muls	r3, r7, r3
	it	mi
	addmi	r3, r3, #3
	bic	r3, r3, #3
	mov	r0, r3
	ldr	r3, [sp, #8]
	add	r0, r0, r3
	ldr	r3, [sp, #12]
	muls	r3, r7, r3
	add	r7, r7, #1
	it	mi
	addmi	r3, r3, #3
	uxth	r7, r7
	bic	r3, r3, #3
	str	r0, [r1, #8]
	add	r3, r3, r9
	str	r3, [r1, #12]
.L1328:
	adds	r2, r2, #1
	b	.L1327
.L1344:
	movs	r3, #20
	ldr	r2, [r4, #3292]
	mul	r3, r3, r10
	adds	r1, r2, r3
	ldr	r8, [r2, r3]
	ldr	r0, [r1, #4]
	ldr	r9, [r1, #12]
	str	r0, [sp]
	cmp	r8, #0
	bne	.L1333
	ldrh	r1, [r9]
	movw	r2, #65535
	cmp	r1, r2
	bne	.L1334
.L1358:
	ldrh	r2, [r5]
	movs	r1, #0
	ldr	r3, [r4, #72]
	movs	r6, #0
	strh	r1, [r3, r2, lsl #1]	@ movhi
	ldrh	r0, [r5]
	bl	INSERT_FREE_LIST
	movw	r3, #65535
	strh	r3, [r5]	@ movhi
	strh	r3, [r4, #172]	@ movhi
	bl	FtlGcPageVarInit
	b	.L1343
.L1334:
	ldr	r0, [r9, #8]
	ldr	r2, [r4, #2556]
	str	r3, [sp, #8]
	cmp	r0, r2
	bhi	.L1358
	mov	r2, r8
	add	r1, sp, #20
	bl	log2phys
	ldr	r2, [r9, #12]
	ldr	r1, [sp, #20]
	ldr	r3, [sp, #8]
	cmp	r2, r1
	beq	.L1337
.L1339:
	ldr	r2, [r9, #8]
.L1359:
	ldr	r1, [sp]
	add	r10, r10, #1
	ldr	r0, [r9, #12]
	bl	FtlGcUpdatePage
	b	.L1332
.L1337:
	str	r3, [sp, #8]
	adds	r3, r2, #1
	beq	.L1339
	str	r2, [sp, #28]
	movs	r1, #1
	ldr	r2, [r4, #3320]
	add	r0, sp, #24
	str	r2, [sp, #32]
	ldr	r2, [r4, #3340]
	str	r2, [sp, #36]
	mov	r2, r8
	bl	FlashReadPages
	ldrh	r2, [r4, #258]
	ldr	r1, [r4, #3292]
	ldr	r3, [sp, #8]
	ldr	r0, [sp, #32]
	lsls	r2, r2, #7
	add	ip, r3, r1
.L1340:
	cmp	r8, r2
	beq	.L1339
	ldr	r1, [ip, #8]
	ldr	r3, [r0, r8, lsl #2]
	ldr	r1, [r1, r8, lsl #2]
	cmp	r1, r3
	beq	.L1341
	ldr	r2, [sp, #28]
	ldrh	r1, [r5]
	ldr	r0, .L1360+12
	bl	sftl_printk
	b	.L1358
.L1341:
	add	r8, r8, #1
	b	.L1340
.L1333:
	mov	r2, #-1
	b	.L1359
.L1345:
	ldr	r2, .L1360+4
	movw	r1, #65535
	ldrh	r3, [r2, #4]
	cmp	r3, r1
	beq	.L1347
	add	r3, r3, fp
	strh	r3, [r2, #4]	@ movhi
	ldrh	r3, [r4, #302]
	cmp	r3, r6
	bls	.L1347
.L1348:
	ldr	r3, .L1360
	mov	r0, #-1
	ldr	r2, [sp, #44]
	ldr	r3, [r3]
	cmp	r2, r3
	beq	.L1349
	bl	__stack_chk_fail
.L1349:
	add	sp, sp, #52
	@ sp needed
	pop	{r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L1361:
	.align	2
.L1360:
	.word	__stack_chk_guard
	.word	.LANCHOR2
	.word	.LANCHOR0
	.word	.LC117
	.fnend
	.size	FtlGcScanTempBlk, .-FtlGcScanTempBlk
	.align	1
	.global	FtlReadRefresh
	.syntax unified
	.thumb
	.thumb_func
	.fpu softvfp
	.type	FtlReadRefresh, %function
FtlReadRefresh:
	.fnstart
	@ args = 0, pretend = 0, frame = 96
	@ frame_needed = 0, uses_anonymous_args = 0
	push	{r4, r5, r6, r7, r8, r9, r10, lr}
	.save {r4, r5, r6, r7, r8, r9, r10, lr}
	.pad #96
	sub	sp, sp, #96
	ldr	r6, .L1379
	ldr	r7, .L1379+4
	ldr	r10, [r6, #2724]
	mov	r5, r6
	ldr	r3, [r7]
	str	r3, [sp, #92]
	cmp	r10, #0
	beq	.L1363
	ldr	r2, [r6, #2728]
	ldr	r3, [r6, #2556]
	cmp	r2, r3
	bcs	.L1364
	mov	r4, #2048
.L1369:
	ldr	r0, [r5, #2728]
	ldr	r3, [r5, #2556]
	cmp	r0, r3
	bcc	.L1365
.L1368:
	mov	r0, #-1
.L1362:
	ldr	r2, [sp, #92]
	ldr	r3, [r7]
	cmp	r2, r3
	beq	.L1374
	bl	__stack_chk_fail
.L1365:
	movs	r2, #0
	add	r1, sp, #4
	bl	log2phys
	ldr	r2, [sp, #4]
	ldr	r3, [r5, #2728]
	adds	r1, r2, #1
	add	r3, r3, #1
	str	r3, [r5, #2728]
	beq	.L1367
	str	r3, [sp, #24]
	add	r0, sp, #96
	ldr	r3, [r5, #3328]
	movs	r1, #1
	str	r2, [sp, #12]
	movs	r2, #0
	str	r2, [r0, #-88]!
	str	r3, [sp, #16]
	add	r3, sp, #28
	str	r3, [sp, #20]
	bl	FlashReadPages
	ldr	r3, [sp, #8]
	cmp	r3, #256
	bne	.L1368
	ldr	r0, [sp, #4]
	ubfx	r0, r0, #10, #16
	bl	P2V_block_in_plane
	bl	FtlGcRefreshBlock
	b	.L1368
.L1367:
	subs	r4, r4, #1
	bne	.L1369
	b	.L1368
.L1364:
	ldr	r3, [r6, #2560]
	movs	r0, #0
	str	r0, [r6, #2724]
	str	r0, [r6, #2728]
	str	r3, [r6, #2720]
	b	.L1362
.L1363:
	ldr	r9, [r6, #2560]
	movw	r4, #10000
	ldr	r1, [r6, #2616]
	ldr	r8, [r6, #2720]
	add	r3, r9, #1048576
	cmp	r1, r4
	ite	hi
	movhi	r4, #31
	movls	r4, #63
	cmp	r8, r3
	bhi	.L1373
	ldr	r3, [r6, #2556]
	lsrs	r1, r1, #10
	mov	r0, #1000
	adds	r1, r1, #1
	muls	r0, r3, r0
	bl	__aeabi_uidiv
	add	r0, r0, r8
	cmp	r9, r0
	bhi	.L1373
	ldrh	r3, [r6, #2496]
	ands	r0, r4, r3
	bne	.L1376
	ldr	r2, [r6, #2744]
	cmp	r3, r2
	beq	.L1362
.L1373:
	ldrh	r3, [r5, #2496]
	movs	r0, #0
	str	r0, [r5, #2728]
	str	r9, [r5, #2720]
	str	r3, [r5, #2744]
	movs	r3, #1
	str	r3, [r5, #2724]
	b	.L1362
.L1376:
	mov	r0, r10
	b	.L1362
.L1374:
	add	sp, sp, #96
	@ sp needed
	pop	{r4, r5, r6, r7, r8, r9, r10, pc}
.L1380:
	.align	2
.L1379:
	.word	.LANCHOR0
	.word	__stack_chk_guard
	.fnend
	.size	FtlReadRefresh, .-FtlReadRefresh
	.align	1
	.global	l2p_flush
	.syntax unified
	.thumb
	.thumb_func
	.fpu softvfp
	.type	l2p_flush, %function
l2p_flush:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	push	{r4, r5, r6, lr}
	.save {r4, r5, r6, lr}
	movs	r4, #0
	ldr	r5, .L1385
	movs	r6, #12
	bl	FtlWriteDump_data
.L1382:
	ldrh	r3, [r5, #338]
	uxth	r0, r4
	cmp	r3, r0
	bhi	.L1384
	movs	r0, #0
	pop	{r4, r5, r6, pc}
.L1384:
	ldr	r2, [r5, #2540]
	uxth	r3, r4
	mla	r3, r6, r3, r2
	ldr	r3, [r3, #4]
	cmp	r3, #0
	bge	.L1383
	bl	flush_l2p_region
.L1383:
	adds	r4, r4, #1
	b	.L1382
.L1386:
	.align	2
.L1385:
	.word	.LANCHOR0
	.fnend
	.size	l2p_flush, .-l2p_flush
	.align	1
	.global	FtlVendorPartWrite
	.syntax unified
	.thumb
	.thumb_func
	.fpu softvfp
	.type	FtlVendorPartWrite, %function
FtlVendorPartWrite:
	.fnstart
	@ args = 0, pretend = 0, frame = 104
	@ frame_needed = 0, uses_anonymous_args = 0
	ldr	r3, .L1400
	push	{r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.save {r4, r5, r6, r7, r8, r9, r10, fp, lr}
	mov	r10, r2
	ldr	r4, .L1400+4
	.pad #108
	sub	sp, sp, #108
	mov	r6, r1
	adds	r1, r0, r1
	ldr	r2, [r3]
	mov	r8, r0
	str	r3, [sp, #4]
	str	r2, [sp, #100]
	ldrh	r2, [r4, #296]
	cmp	r1, r2
	bhi	.L1396
	ldrh	r7, [r4, #308]
	mov	r9, #0
	lsr	r7, r0, r7
	lsl	fp, r7, #2
.L1389:
	cbnz	r6, .L1394
.L1387:
	ldr	r3, [sp, #4]
	mov	r0, r9
	ldr	r2, [sp, #100]
	ldr	r3, [r3]
	cmp	r2, r3
	beq	.L1395
	bl	__stack_chk_fail
.L1394:
	ldr	r3, [r4, #3376]
	mov	r0, r8
	ldr	r2, [r3, fp]
	ldrh	r3, [r4, #258]
	str	r2, [sp, #12]
	mov	r1, r3
	str	r3, [sp, #8]
	bl	__aeabi_uidivmod
	ldr	r3, [sp, #8]
	ldr	r2, [sp, #12]
	str	r1, [sp]
	subs	r5, r3, r1
	uxth	r5, r5
	cmp	r6, r5
	it	cc
	uxthcc	r5, r6
	cbz	r2, .L1391
	cmp	r5, r3
	beq	.L1391
	ldr	r3, [r4, #3312]
	add	r0, sp, #16
	str	r2, [sp, #20]
	movs	r2, #1
	mov	r1, r2
	str	r3, [sp, #24]
	add	r3, sp, #36
	str	r3, [sp, #28]
	bl	FlashReadPages
.L1392:
	lsls	r3, r5, #9
	ldr	r0, [r4, #3312]
	mov	r1, r10
	mov	r2, r3
	str	r3, [sp, #8]
	ldr	r3, [sp]
	subs	r6, r6, r5
	add	r8, r8, r5
	add	fp, fp, #4
	add	r0, r0, r3, lsl #9
	bl	ftl_memcpy
	mov	r1, r7
	ldr	r2, [r4, #3312]
	ldr	r0, .L1400+8
	adds	r7, r7, #1
	bl	FtlMapWritePage
	ldr	r3, [sp, #8]
	adds	r0, r0, #1
	it	eq
	moveq	r9, #-1
	add	r10, r10, r3
	b	.L1389
.L1391:
	ldrh	r2, [r4, #310]
	movs	r1, #0
	ldr	r0, [r4, #3312]
	bl	ftl_memset
	b	.L1392
.L1396:
	mov	r9, #-1
	b	.L1387
.L1395:
	add	sp, sp, #108
	@ sp needed
	pop	{r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L1401:
	.align	2
.L1400:
	.word	__stack_chk_guard
	.word	.LANCHOR0
	.word	.LANCHOR0+3612
	.fnend
	.size	FtlVendorPartWrite, .-FtlVendorPartWrite
	.align	1
	.global	Ftl_save_ext_data
	.syntax unified
	.thumb
	.thumb_func
	.fpu softvfp
	.type	Ftl_save_ext_data, %function
Ftl_save_ext_data:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	@ link register save eliminated.
	ldr	r2, .L1404
	ldr	r3, .L1404+4
	ldr	r1, [r2, #2644]
	cmp	r1, r3
	bne	.L1402
	ldr	r3, .L1404+8
	addw	r2, r2, #2644
	movs	r1, #1
	movs	r0, #0
	str	r3, [r2, #4]
	ldr	r3, [r2, #-60]
	str	r3, [r2, #88]
	ldr	r3, [r2, #-56]
	str	r3, [r2, #92]
	ldr	r3, [r2, #-64]
	str	r3, [r2, #8]
	ldr	r3, [r2, #-76]
	str	r3, [r2, #12]
	ldr	r3, [r2, #-84]
	str	r3, [r2, #16]
	ldr	r3, [r2, #-68]
	str	r3, [r2, #20]
	ldr	r3, [r2, #-40]
	str	r3, [r2, #28]
	ldr	r3, [r2, #-32]
	str	r3, [r2, #32]
	ldr	r3, [r2, #-80]
	str	r3, [r2, #36]
	ldr	r3, [r2, #-72]
	str	r3, [r2, #40]
	ldr	r3, [r2, #-28]
	str	r3, [r2, #44]
	ldr	r3, [r2, #-24]
	str	r3, [r2, #48]
	b	FtlVendorPartWrite
.L1402:
	bx	lr
.L1405:
	.align	2
.L1404:
	.word	.LANCHOR0
	.word	1179929683
	.word	1342177365
	.fnend
	.size	Ftl_save_ext_data, .-Ftl_save_ext_data
	.align	1
	.global	FtlEctTblFlush
	.syntax unified
	.thumb
	.thumb_func
	.fpu softvfp
	.type	FtlEctTblFlush, %function
FtlEctTblFlush:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	push	{r3, lr}
	.save {r3, lr}
	ldr	r3, .L1411
	ldrh	r2, [r3, #3656]
	cmp	r2, #31
	itett	ls
	addls	r2, r2, #1
	movhi	r2, #32
	strhls	r2, [r3, #3656]	@ movhi
	movls	r2, #1
	cbnz	r0, .L1408
	ldr	r1, [r3, #3352]
	ldr	r0, [r1, #20]
	ldr	r1, [r1, #16]
	add	r2, r2, r0
	cmp	r1, r2
	bcc	.L1409
.L1408:
	ldr	r2, [r3, #3352]
	movs	r0, #64
	ldr	r1, [r2, #16]
	str	r1, [r2, #20]
	ldr	r1, .L1411+4
	str	r1, [r2]
	ldrh	r1, [r3, #3344]
	ldr	r2, [r3, #3352]
	lsls	r3, r1, #9
	str	r3, [r2, #12]
	ldr	r3, [r2, #8]
	adds	r3, r3, #1
	str	r3, [r2, #8]
	movs	r3, #0
	str	r3, [r2, #4]
	bl	FtlVendorPartWrite
	bl	Ftl_save_ext_data
.L1409:
	movs	r0, #0
	pop	{r3, pc}
.L1412:
	.align	2
.L1411:
	.word	.LANCHOR0
	.word	1112818501
	.fnend
	.size	FtlEctTblFlush, .-FtlEctTblFlush
	.align	1
	.global	sftl_vendor_write
	.syntax unified
	.thumb
	.thumb_func
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
	.align	1
	.global	FtlVendorPartRead
	.syntax unified
	.thumb
	.thumb_func
	.fpu softvfp
	.type	FtlVendorPartRead, %function
FtlVendorPartRead:
	.fnstart
	@ args = 0, pretend = 0, frame = 104
	@ frame_needed = 0, uses_anonymous_args = 0
	ldr	r3, .L1425
	push	{r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.save {r4, r5, r6, r7, r8, r9, r10, fp, lr}
	mov	r10, r2
	ldr	r4, .L1425+4
	.pad #108
	sub	sp, sp, #108
	mov	r7, r1
	adds	r1, r0, r1
	ldr	r2, [r3]
	mov	r8, r0
	str	r3, [sp, #8]
	str	r2, [sp, #100]
	ldrh	r2, [r4, #296]
	cmp	r1, r2
	bhi	.L1424
	ldrh	r6, [r4, #308]
	mov	r9, #0
	lsr	r6, r0, r6
	lsl	fp, r6, #2
.L1416:
	cbnz	r7, .L1422
.L1414:
	ldr	r3, [sp, #8]
	mov	r0, r9
	ldr	r2, [sp, #100]
	ldr	r3, [r3]
	cmp	r2, r3
	beq	.L1423
	bl	__stack_chk_fail
.L1422:
	ldr	r3, [r4, #3376]
	mov	r0, r8
	ldrh	r5, [r4, #258]
	ldr	r3, [r3, fp]
	mov	r1, r5
	str	r3, [sp, #12]
	bl	__aeabi_uidivmod
	subs	r5, r5, r1
	ldr	r3, [sp, #12]
	uxth	r5, r5
	str	r1, [sp, #4]
	cmp	r7, r5
	it	cc
	uxthcc	r5, r7
	lsls	r2, r5, #9
	str	r2, [sp, #12]
	cbz	r3, .L1418
	ldr	r2, [r4, #3312]
	add	r0, sp, #16
	str	r3, [sp, #20]
	str	r3, [sp, #12]
	str	r2, [sp, #24]
	add	r2, sp, #36
	str	r2, [sp, #28]
	movs	r2, #1
	mov	r1, r2
	bl	FlashReadPages
	ldr	r2, [sp, #16]
	ldr	r3, [sp, #12]
	adds	r2, r2, #1
	ldr	r2, [r4, #3456]
	it	eq
	moveq	r9, #-1
	cmp	r2, #256
	bne	.L1420
	mov	r2, r3
	mov	r1, r6
	ldr	r0, .L1425+8
	bl	sftl_printk
	ldr	r2, [r4, #3312]
	mov	r1, r6
	ldr	r0, .L1425+12
	bl	FtlMapWritePage
.L1420:
	ldr	r1, [r4, #3312]
	lsls	r2, r5, #9
	ldr	r3, [sp, #4]
	mov	r0, r10
	add	r1, r1, r3, lsl #9
	bl	ftl_memcpy
.L1421:
	adds	r6, r6, #1
	subs	r7, r7, r5
	add	r8, r8, r5
	add	r10, r10, r5, lsl #9
	add	fp, fp, #4
	b	.L1416
.L1418:
	lsls	r2, r5, #9
	mov	r1, r3
	mov	r0, r10
	bl	ftl_memset
	b	.L1421
.L1424:
	mov	r9, #-1
	b	.L1414
.L1423:
	add	sp, sp, #108
	@ sp needed
	pop	{r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L1426:
	.align	2
.L1425:
	.word	__stack_chk_guard
	.word	.LANCHOR0
	.word	.LC118
	.word	.LANCHOR0+3612
	.fnend
	.size	FtlVendorPartRead, .-FtlVendorPartRead
	.align	1
	.global	FtlLoadEctTbl
	.syntax unified
	.thumb
	.thumb_func
	.fpu softvfp
	.type	FtlLoadEctTbl, %function
FtlLoadEctTbl:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	push	{r4, lr}
	.save {r4, lr}
	movs	r0, #64
	ldr	r4, .L1429
	ldr	r2, [r4, #3352]
	ldrh	r1, [r4, #3344]
	bl	FtlVendorPartRead
	ldr	r3, [r4, #3352]
	ldr	r2, [r3]
	ldr	r3, .L1429+4
	cmp	r2, r3
	beq	.L1428
	ldr	r1, .L1429+8
	ldr	r0, .L1429+12
	bl	sftl_printk
	ldrh	r2, [r4, #3344]
	movs	r1, #0
	ldr	r0, [r4, #3352]
	lsls	r2, r2, #9
	bl	ftl_memset
.L1428:
	movs	r0, #0
	pop	{r4, pc}
.L1430:
	.align	2
.L1429:
	.word	.LANCHOR0
	.word	1112818501
	.word	.LC119
	.word	.LC77
	.fnend
	.size	FtlLoadEctTbl, .-FtlLoadEctTbl
	.align	1
	.global	Ftl_load_ext_data
	.syntax unified
	.thumb
	.thumb_func
	.fpu softvfp
	.type	Ftl_load_ext_data, %function
Ftl_load_ext_data:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	push	{r3, r4, r5, lr}
	.save {r3, r4, r5, lr}
	movs	r1, #1
	ldr	r4, .L1434
	movs	r0, #0
	ldr	r5, .L1434+4
	addw	r2, r4, #2644
	bl	FtlVendorPartRead
	ldr	r3, [r4, #2644]
	cmp	r3, r5
	beq	.L1432
	mov	r2, #512
	movs	r1, #0
	addw	r0, r4, #2644
	bl	ftl_memset
	str	r5, [r4, #2644]
.L1432:
	ldr	r3, [r4, #2644]
	cmp	r3, r5
	bne	.L1433
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
.L1433:
	ldrh	r2, [r4, #292]
	ldr	r3, [r4, #2604]
	ldr	r0, [r4, #2600]
	ldrh	r1, [r4, #240]
	mla	r0, r0, r2, r3
	bl	__aeabi_uidiv
	str	r0, [r4, #2608]
	pop	{r3, r4, r5, pc}
.L1435:
	.align	2
.L1434:
	.word	.LANCHOR0
	.word	1179929683
	.fnend
	.size	Ftl_load_ext_data, .-Ftl_load_ext_data
	.align	1
	.global	sftl_vendor_read
	.syntax unified
	.thumb
	.thumb_func
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
	.align	1
	.global	FtlMapBlkWriteDump_data
	.syntax unified
	.thumb
	.thumb_func
	.fpu softvfp
	.type	FtlMapBlkWriteDump_data, %function
FtlMapBlkWriteDump_data:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	push	{r4, r5, r6, lr}
	.save {r4, r5, r6, lr}
	mov	r6, r0
	ldr	r3, [r0, #36]
	cbz	r3, .L1437
	ldrh	r5, [r0, #6]
	movs	r3, #0
	ldr	r4, .L1441
	ldr	r2, [r0, #24]
	str	r3, [r0, #36]
	subs	r5, r5, #1
	ldr	r0, [r4, #3308]
	uxth	r5, r5
	ldr	r1, [r4, #3336]
	str	r0, [r4, #3464]
	str	r1, [r4, #3468]
	ldr	r2, [r2, r5, lsl #2]
	str	r2, [r4, #3460]
	cbz	r2, .L1439
	movs	r2, #1
	add	r0, r4, #3456
	mov	r1, r2
	bl	FlashReadPages
.L1440:
	ldr	r2, [r4, #3464]
	mov	r1, r5
	mov	r0, r6
	pop	{r4, r5, r6, lr}
	b	FtlMapWritePage
.L1439:
	ldrh	r2, [r4, #310]
	movs	r1, #255
	bl	ftl_memset
	b	.L1440
.L1437:
	pop	{r4, r5, r6, pc}
.L1442:
	.align	2
.L1441:
	.word	.LANCHOR0
	.fnend
	.size	FtlMapBlkWriteDump_data, .-FtlMapBlkWriteDump_data
	.align	1
	.global	FtlVpcTblFlush
	.syntax unified
	.thumb
	.thumb_func
	.fpu softvfp
	.type	FtlVpcTblFlush, %function
FtlVpcTblFlush:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	push	{r4, r5, r6, r7, r8, r9, r10, lr}
	.save {r4, r5, r6, r7, r8, r9, r10, lr}
	movs	r5, #0
	ldr	r4, .L1458
	movs	r1, #255
	movw	r7, #65535
	ldr	r9, .L1458+16
	ldr	r3, [r4, #3304]
	add	r8, r4, #3456
	ldr	r6, [r4, #3336]
	ldr	r10, .L1458+20
	str	r3, [r4, #3464]
	ldrh	r3, [r4, #2628]
	str	r6, [r4, #3468]
	str	r5, [r6, #12]
	strh	r3, [r6, #2]	@ movhi
	movw	r3, #61604
	strh	r3, [r6]	@ movhi
	ldr	r3, [r4, #2636]
	str	r5, [r6, #8]
	ldrh	r2, [r4, #26]
	str	r3, [r6, #4]
	ldr	r3, .L1458+4
	str	r3, [r4, #2468]
	ldr	r3, .L1458+8
	str	r3, [r4, #2472]
	ldrh	r3, [r4, #2634]
	strh	r3, [r4, #2476]	@ movhi
	ldrh	r3, [r4, #254]
	strb	r3, [r4, #2478]
	ldrh	r3, [r4, #24]
	strh	r3, [r4, #2482]	@ movhi
	ldrb	r3, [r4, #30]	@ zero_extendqisi2
	orr	r3, r3, r2, lsl #6
	ldrh	r2, [r4, #78]
	strh	r3, [r4, #2484]	@ movhi
	ldrb	r3, [r4, #32]	@ zero_extendqisi2
	strb	r3, [r4, #2479]
	ldrh	r3, [r4, #76]
	strh	r3, [r4, #2486]	@ movhi
	ldrb	r3, [r4, #82]	@ zero_extendqisi2
	orr	r3, r3, r2, lsl #6
	strh	r3, [r4, #2488]	@ movhi
	ldrb	r3, [r4, #84]	@ zero_extendqisi2
	strb	r3, [r4, #2480]
	ldrh	r3, [r4, #124]
	ldrh	r2, [r4, #126]
	ldr	r0, [r4, #3464]
	strh	r3, [r4, #2490]	@ movhi
	ldrb	r3, [r4, #130]	@ zero_extendqisi2
	orr	r3, r3, r2, lsl #6
	ldrh	r2, [r4, #310]
	strh	r3, [r4, #2492]	@ movhi
	ldrb	r3, [r4, #132]	@ zero_extendqisi2
	strb	r3, [r4, #2481]
	ldr	r3, [r4, #2600]
	str	r3, [r4, #2500]
	ldr	r3, [r4, #2592]
	str	r3, [r4, #2508]
	ldr	r3, [r4, #2596]
	str	r3, [r4, #2504]
	bl	ftl_memset
	movs	r2, #48
	addw	r1, r4, #2468
	ldr	r0, [r4, #3464]
	bl	ftl_memcpy
	ldrh	r2, [r4, #240]
	ldr	r0, [r4, #3464]
	ldr	r1, [r4, #72]
	lsls	r2, r2, #1
	adds	r0, r0, #48
	bl	ftl_memcpy
	mov	r0, r5
	bl	FtlUpdateVaildLpn
.L1444:
	ldr	r3, [r4, #3304]
	ldrh	r1, [r4, #2630]
	ldrh	r2, [r4, #2628]
	str	r3, [r4, #3464]
	ldr	r3, [r4, #3336]
	str	r3, [r4, #3468]
	orr	r3, r1, r2, lsl #10
	str	r3, [r4, #3460]
	ldrh	r3, [r4, #304]
	subs	r3, r3, #1
	cmp	r1, r3
	blt	.L1445
	movs	r3, #0
	ldrh	r7, [r4, #2632]
	strh	r3, [r4, #2630]	@ movhi
	strh	r2, [r4, #2632]	@ movhi
	bl	FtlFreeSysBlkQueueOut
	ldr	r3, [r4, #2592]
	strh	r0, [r4, #2628]	@ movhi
	adds	r2, r3, #1
	str	r3, [r4, #2636]
	str	r2, [r4, #2592]
	lsls	r2, r0, #10
	str	r2, [r4, #3460]
	str	r3, [r6, #4]
	strh	r0, [r6, #2]	@ movhi
.L1445:
	ldrh	r1, [r4, #310]
	ldr	r0, [r4, #3304]
	bl	js_hash
	movs	r3, #1
	str	r0, [r6, #12]
	mov	r2, r3
	mov	r1, r3
	mov	r0, r8
	bl	FlashProgPages
	ldrh	r3, [r4, #2630]
	ldr	r2, [r4, #3456]
	adds	r3, r3, #1
	uxth	r3, r3
	adds	r1, r2, #1
	strh	r3, [r4, #2630]	@ movhi
	bne	.L1446
	cmp	r3, #1
	bne	.L1447
	movw	r2, #1138
	mov	r1, r9
	mov	r0, r10
	bl	sftl_printk
.L1447:
	ldrh	r3, [r4, #2630]
	adds	r5, r5, #1
	uxth	r5, r5
	cmp	r3, #1
	ittt	eq
	ldrheq	r3, [r4, #304]
	addeq	r3, r3, #-1
	strheq	r3, [r4, #2630]	@ movhi
	cmp	r5, #3
	bls	.L1444
	mov	r2, r5
	ldr	r1, [r4, #3460]
	ldr	r0, .L1458+12
	bl	sftl_printk
.L1450:
	b	.L1450
.L1446:
	cmp	r3, #1
	beq	.L1444
	cmp	r2, #256
	beq	.L1444
	movw	r3, #65535
	cmp	r7, r3
	beq	.L1451
	movs	r1, #1
	mov	r0, r7
	bl	FtlFreeSysBlkQueueIn
.L1451:
	movs	r0, #0
	pop	{r4, r5, r6, r7, r8, r9, r10, pc}
.L1459:
	.align	2
.L1458:
	.word	.LANCHOR0
	.word	1179929683
	.word	1342177365
	.word	.LC120
	.word	.LANCHOR1+471
	.word	.LC8
	.fnend
	.size	FtlVpcTblFlush, .-FtlVpcTblFlush
	.align	1
	.global	FtlSysFlush
	.syntax unified
	.thumb
	.thumb_func
	.fpu softvfp
	.type	FtlSysFlush, %function
FtlSysFlush:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	push	{r3, lr}
	.save {r3, lr}
	bl	l2p_flush
	movs	r0, #1
	bl	FtlEctTblFlush
	bl	FtlVpcTblFlush
	movs	r0, #0
	pop	{r3, pc}
	.fnend
	.size	FtlSysFlush, .-FtlSysFlush
	.align	1
	.global	sftl_deinit
	.syntax unified
	.thumb
	.thumb_func
	.fpu softvfp
	.type	sftl_deinit, %function
sftl_deinit:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	push	{r3, lr}
	.save {r3, lr}
	ldr	r3, .L1463
	ldr	r3, [r3]
	cmp	r3, #1
	bne	.L1462
	bl	FtlSysFlush
.L1462:
	movs	r0, #0
	pop	{r3, pc}
.L1464:
	.align	2
.L1463:
	.word	.LANCHOR2
	.fnend
	.size	sftl_deinit, .-sftl_deinit
	.align	1
	.global	sftl_discard
	.syntax unified
	.thumb
	.thumb_func
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
	ldr	r7, .L1483
	mov	r8, r0
	mov	r5, r1
	ldr	r4, .L1483+4
	ldr	r3, [r7]
	str	r3, [sp, #12]
	ldr	r3, [r4, #340]
	cmp	r3, r0
	bls	.L1476
	cmp	r3, r1
	bcc	.L1476
	adds	r2, r0, r1
	cmp	r3, r2
	bcc	.L1476
	cmp	r1, #31
	bhi	.L1467
.L1472:
	movs	r0, #0
.L1465:
	ldr	r2, [sp, #12]
	ldr	r3, [r7]
	cmp	r2, r3
	beq	.L1473
	bl	__stack_chk_fail
.L1467:
	ldrh	r6, [r4, #258]
	mov	r1, r6
	bl	__aeabi_uidiv
	smulbb	r3, r0, r6
	mov	r9, r0
	sub	r8, r8, r3
	uxth	r3, r8
	cbz	r3, .L1468
	subs	r6, r6, r3
	add	r9, r0, #1
	cmp	r6, r5
	it	cs
	movcs	r6, r5
	uxth	r6, r6
	subs	r5, r5, r6
.L1468:
	mov	r3, #-1
	str	r3, [sp, #8]
.L1469:
	ldrh	r3, [r4, #258]
	cmp	r5, r3
	bcs	.L1471
	ldr	r3, [r4, #3660]
	cmp	r3, #32
	bls	.L1472
	movs	r5, #0
	str	r5, [r4, #3660]
	bl	l2p_flush
	bl	FtlVpcTblFlush
	b	.L1472
.L1471:
	movs	r2, #0
	add	r1, sp, #4
	mov	r0, r9
	bl	log2phys
	ldr	r3, [sp, #4]
	adds	r3, r3, #1
	beq	.L1470
	ldr	r3, [r4, #3660]
	movs	r2, #1
	add	r1, sp, #8
	mov	r0, r9
	adds	r3, r3, #1
	str	r3, [r4, #3660]
	ldr	r3, [r4, #2564]
	adds	r3, r3, #1
	str	r3, [r4, #2564]
	bl	log2phys
	ldr	r0, [sp, #4]
	ubfx	r0, r0, #10, #16
	bl	P2V_block_in_plane
	bl	decrement_vpc_count
.L1470:
	ldrh	r3, [r4, #258]
	add	r9, r9, #1
	subs	r5, r5, r3
	b	.L1469
.L1476:
	mov	r0, #-1
	b	.L1465
.L1473:
	add	sp, sp, #20
	@ sp needed
	pop	{r4, r5, r6, r7, r8, r9, pc}
.L1484:
	.align	2
.L1483:
	.word	__stack_chk_guard
	.word	.LANCHOR0
	.fnend
	.size	sftl_discard, .-sftl_discard
	.align	1
	.global	FtlVpcCheckAndModify
	.syntax unified
	.thumb
	.thumb_func
	.fpu softvfp
	.type	FtlVpcCheckAndModify, %function
FtlVpcCheckAndModify:
	.fnstart
	@ args = 0, pretend = 0, frame = 8
	@ frame_needed = 0, uses_anonymous_args = 0
	push	{r0, r1, r2, r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.save {r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.pad #12
	movs	r5, #0
	ldr	r7, .L1500
	ldr	r4, .L1500+4
	ldr	r3, [r7]
	ldr	r1, .L1500+8
	ldr	r0, .L1500+12
	str	r3, [sp, #4]
	bl	sftl_printk
	ldrh	r2, [r4, #242]
	movs	r1, #0
	ldr	r0, [r4, #3356]
	lsls	r2, r2, #1
	bl	ftl_memset
.L1486:
	ldr	r3, [r4, #2556]
	cmp	r5, r3
	bcc	.L1488
	ldr	r9, .L1500+20
	mov	r8, #0
	ldr	r10, .L1500+24
	ldr	fp, .L1500+8
.L1489:
	ldrh	r3, [r4, #240]
	uxth	r6, r8
	cmp	r3, r6
	bhi	.L1495
	bl	l2p_flush
	bl	FtlVpcTblFlush
	ldr	r2, [sp, #4]
	ldr	r3, [r7]
	cmp	r2, r3
	beq	.L1496
	bl	__stack_chk_fail
.L1488:
	movs	r2, #0
	mov	r1, sp
	mov	r0, r5
	bl	log2phys
	ldr	r0, [sp]
	adds	r3, r0, #1
	beq	.L1487
	ubfx	r0, r0, #10, #16
	bl	P2V_block_in_plane
	ldr	r2, [r4, #3356]
	ldrh	r3, [r2, r0, lsl #1]
	adds	r3, r3, #1
	strh	r3, [r2, r0, lsl #1]	@ movhi
.L1487:
	adds	r5, r5, #1
	b	.L1486
.L1495:
	ldr	r3, [r4, #72]
	uxth	r5, r8
	ldrh	r2, [r3, r5, lsl #1]
	ldr	r3, [r4, #3356]
	ldrh	r3, [r3, r5, lsl #1]
	cmp	r2, r3
	beq	.L1491
	movw	r1, #65535
	cmp	r2, r1
	beq	.L1491
	mov	r1, r5
	mov	r0, r9
	bl	sftl_printk
	ldrh	r3, [r4, #24]
	cmp	r3, r6
	beq	.L1491
	ldrh	r3, [r4, #124]
	cmp	r3, r6
	beq	.L1491
	ldrh	r3, [r4, #76]
	cmp	r3, r6
	beq	.L1491
	ldr	r3, [r4, #72]
	ldrh	r2, [r3, r5, lsl #1]
	cbnz	r2, .L1493
	ldr	r2, [r4, #3356]
	mov	r1, r6
	mov	r0, r10
	ldrh	r2, [r2, r5, lsl #1]
	strh	r2, [r3, r5, lsl #1]	@ movhi
	bl	List_remove_node
	ldrh	r3, [r4, #224]
	cbnz	r3, .L1494
	movw	r2, #2323
	mov	r1, fp
	ldr	r0, .L1500+16
	bl	sftl_printk
.L1494:
	ldrh	r3, [r4, #224]
	mov	r0, r6
	subs	r3, r3, #1
	strh	r3, [r4, #224]	@ movhi
	bl	INSERT_DATA_LIST
.L1491:
	add	r8, r8, #1
	b	.L1489
.L1493:
	ldr	r2, [r4, #3356]
	mov	r0, r6
	ldrh	r2, [r2, r5, lsl #1]
	strh	r2, [r3, r5, lsl #1]	@ movhi
	bl	update_vpc_list
	b	.L1491
.L1496:
	add	sp, sp, #12
	@ sp needed
	pop	{r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L1501:
	.align	2
.L1500:
	.word	__stack_chk_guard
	.word	.LANCHOR0
	.word	.LANCHOR1+486
	.word	.LC111
	.word	.LC8
	.word	.LC121
	.word	.LANCHOR0+2532
	.fnend
	.size	FtlVpcCheckAndModify, .-FtlVpcCheckAndModify
	.align	1
	.global	allocate_new_data_superblock
	.syntax unified
	.thumb
	.thumb_func
	.fpu softvfp
	.type	allocate_new_data_superblock, %function
allocate_new_data_superblock:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	push	{r4, r5, r6, lr}
	.save {r4, r5, r6, lr}
	mov	r6, r0
	ldr	r4, .L1512
	ldrh	r5, [r0]
	ldrh	r3, [r4, #240]
	cmp	r3, r5
	bcs	.L1503
	movw	r2, #2759
	ldr	r1, .L1512+4
	ldr	r0, .L1512+8
	bl	sftl_printk
.L1503:
	movw	r3, #65535
	cmp	r5, r3
	beq	.L1504
	ldr	r3, [r4, #72]
	mov	r0, r5
	ldrh	r3, [r3, r5, lsl #1]
	cbz	r3, .L1505
	bl	INSERT_DATA_LIST
.L1504:
	ldrh	r0, [r4, #3442]
	movs	r3, #1
	strb	r3, [r6, #8]
	movw	r3, #65535
	cmp	r0, r3
	beq	.L1506
	cmp	r5, r0
	bne	.L1507
	ldr	r3, [r4, #72]
	ldrh	r3, [r3, r0, lsl #1]
	cbz	r3, .L1508
.L1507:
	bl	update_vpc_list
.L1508:
	movw	r3, #65535
	strh	r3, [r4, #3442]	@ movhi
.L1506:
	mov	r0, r6
	bl	allocate_data_superblock
	bl	l2p_flush
	movs	r0, #0
	bl	FtlEctTblFlush
	bl	FtlVpcTblFlush
	movs	r0, #0
	pop	{r4, r5, r6, pc}
.L1505:
	bl	INSERT_FREE_LIST
	b	.L1504
.L1513:
	.align	2
.L1512:
	.word	.LANCHOR0
	.word	.LANCHOR1+507
	.word	.LC8
	.fnend
	.size	allocate_new_data_superblock, .-allocate_new_data_superblock
	.align	1
	.global	FtlProgPages
	.syntax unified
	.thumb
	.thumb_func
	.fpu softvfp
	.type	FtlProgPages, %function
FtlProgPages:
	.fnstart
	@ args = 0, pretend = 0, frame = 16
	@ frame_needed = 0, uses_anonymous_args = 0
	push	{r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.save {r4, r5, r6, r7, r8, r9, r10, fp, lr}
	mov	r5, r3
	ldr	r6, .L1534
	.pad #20
	sub	sp, sp, #20
	movs	r7, #0
	movs	r2, #0
	ldr	fp, .L1534+20
	mov	r4, r0
	ldr	r3, [r6]
	mov	r10, r1
	ldr	r9, .L1534+4
	str	r3, [sp, #12]
	ldrb	r3, [r5, #9]	@ zero_extendqisi2
	bl	FlashProgPages
.L1515:
	cmp	r7, r10
	bne	.L1522
	ldr	r3, .L1534+4
	ldrb	r2, [r5, #6]	@ zero_extendqisi2
	ldrh	r3, [r3, #232]
	cmp	r2, r3
	bcc	.L1514
	mov	r2, #1000
	ldr	r1, .L1534+8
	ldr	r0, .L1534+12
	bl	sftl_printk
.L1514:
	ldr	r2, [sp, #12]
	ldr	r3, [r6]
	cmp	r2, r3
	beq	.L1524
	bl	__stack_chk_fail
.L1517:
	ldr	r1, [r4, #4]
	mov	r0, fp
	bl	sftl_printk
	ldr	r0, [r4, #4]
	ubfx	r0, r0, #10, #16
	bl	P2V_block_in_plane
	bl	decrement_vpc_count
	ldrh	r3, [r5, #4]
	cbnz	r3, .L1516
	mov	r0, r5
	bl	allocate_new_data_superblock
.L1516:
	mov	r0, r5
	bl	get_new_active_ppa
	movs	r2, #0
	str	r0, [r4, #4]
	str	r0, [sp, #8]
	movs	r1, #1
	ldrb	r3, [r5, #9]	@ zero_extendqisi2
	mov	r0, r4
	bl	FlashProgPages
.L1522:
	ldr	r2, [r4]
	adds	r3, r2, #1
	beq	.L1517
	cmp	r2, #256
	beq	.L1517
	ldrb	r2, [r5, #6]	@ zero_extendqisi2
	ldrh	r3, [r9, #232]
	cmp	r2, r3
	bcc	.L1518
	movw	r2, #985
	ldr	r1, .L1534+8
	ldr	r0, .L1534+12
	bl	sftl_printk
.L1518:
	ldr	r3, [r4, #4]
	add	r1, sp, #16
	movs	r2, #1
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
	adds	r3, r3, #1
	beq	.L1519
	ldr	r3, [r9, #72]
	ldrh	r2, [r3, r0, lsl #1]
	cbnz	r2, .L1520
	mov	r1, r0
	ldr	r0, .L1534+16
	bl	sftl_printk
.L1520:
	mov	r0, r8
	bl	decrement_vpc_count
.L1519:
	adds	r7, r7, #1
	adds	r4, r4, #20
	b	.L1515
.L1524:
	add	sp, sp, #20
	@ sp needed
	pop	{r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L1535:
	.align	2
.L1534:
	.word	__stack_chk_guard
	.word	.LANCHOR0
	.word	.LANCHOR1+536
	.word	.LC8
	.word	.LC123
	.word	.LC122
	.fnend
	.size	FtlProgPages, .-FtlProgPages
	.align	1
	.global	FtlGcFreeTempBlock
	.syntax unified
	.thumb
	.thumb_func
	.fpu softvfp
	.type	FtlGcFreeTempBlock, %function
FtlGcFreeTempBlock:
	.fnstart
	@ args = 0, pretend = 0, frame = 8
	@ frame_needed = 0, uses_anonymous_args = 0
	push	{r0, r1, r4, r5, r6, r7, r8, r9, r10, lr}
	.save {r4, r5, r6, r7, r8, r9, r10, lr}
	.pad #8
	ldr	r6, .L1561
	ldr	r4, .L1561+4
	ldr	r3, [r6]
	ldrh	r2, [r4, #124]
	str	r3, [sp, #4]
	movw	r3, #65535
	ldrh	r1, [r4, #302]
	cmp	r2, r3
	bne	.L1537
.L1544:
	ldrh	r2, [r4, #124]
	movs	r3, #0
	str	r3, [r4, #3448]
	movw	r3, #65535
	cmp	r2, r3
	bne	.L1558
.L1538:
	movs	r0, #0
.L1536:
	ldr	r2, [sp, #4]
	ldr	r3, [r6]
	cmp	r2, r3
	beq	.L1553
	bl	__stack_chk_fail
.L1537:
	ldr	r5, .L1561+8
	cbz	r0, .L1540
	ldrh	r2, [r5, #4]
	cmp	r2, r3
	beq	.L1541
.L1542:
	movs	r1, #2
.L1540:
	ldr	r0, .L1561+12
	bl	FtlGcScanTempBlk
	ldrh	r2, [r5, #4]
	movw	r3, #65535
	str	r0, [sp]
	cmp	r2, r3
	beq	.L1544
	movs	r0, #1
	b	.L1536
.L1541:
	movs	r3, #0
	strh	r3, [r5, #4]	@ movhi
	ldrh	r3, [r4, #224]
	cmp	r3, #17
	bhi	.L1542
	b	.L1540
.L1558:
	ldrb	r1, [r4, #131]	@ zero_extendqisi2
	ldrh	r3, [r4, #302]
	ldrh	r2, [r4, #3208]
	muls	r3, r1, r3
	cmp	r2, r3
	beq	.L1545
	movs	r2, #164
	ldr	r1, .L1561+16
	ldr	r0, .L1561+20
	bl	sftl_printk
.L1545:
	ldrb	r3, [r4, #131]	@ zero_extendqisi2
	movs	r5, #0
	ldrh	r0, [r4, #302]
	mov	r9, #12
	ldr	r2, [r4, #72]
	ldrh	r1, [r4, #124]
	smulbb	r3, r3, r0
	strh	r3, [r2, r1, lsl #1]	@ movhi
	ldr	r2, [r4, #2580]
	ldrh	r3, [r4, #3208]
	add	r3, r3, r2
	str	r3, [r4, #2580]
.L1546:
	ldrh	r2, [r4, #3208]
	uxth	r3, r5
	cmp	r2, r3
	bhi	.L1550
	movw	r0, #65535
	bl	decrement_vpc_count
	ldrh	r0, [r4, #124]
	ldr	r3, [r4, #72]
	ldrh	r3, [r3, r0, lsl #1]
	cmp	r3, #0
	beq	.L1551
	bl	INSERT_DATA_LIST
.L1552:
	movw	r7, #65535
	movs	r5, #0
	strh	r7, [r4, #124]	@ movhi
	strh	r5, [r4, #3208]	@ movhi
	strh	r5, [r4, #3200]	@ movhi
	bl	l2p_flush
	bl	FtlVpcTblFlush
	ldrh	r3, [r4, #2624]
	ldrh	r2, [r4, #224]
	strh	r7, [r4, #172]	@ movhi
	add	r3, r3, r3, lsl #1
	cmp	r2, r3, asr #2
	ble	.L1538
	movs	r3, #20
	strh	r3, [r4, #3156]	@ movhi
	b	.L1538
.L1550:
	uxth	r8, r5
	ldr	r10, [r4, #3204]
	ldr	r3, [r4, #2556]
	mul	r8, r9, r8
	add	r7, r10, r8
	ldr	r0, [r7, #8]
	cmp	r0, r3
	bcc	.L1547
.L1559:
	ldrh	r0, [r4, #124]
	b	.L1560
.L1547:
	movs	r2, #0
	mov	r1, sp
	bl	log2phys
	ldr	r0, [r10, r8]
	ldr	r3, [sp]
	cmp	r0, r3
	bne	.L1549
	ubfx	r0, r0, #10, #16
	bl	P2V_block_in_plane
	movs	r2, #1
	mov	r8, r0
	adds	r1, r7, #4
	ldr	r0, [r7, #8]
	bl	log2phys
	mov	r0, r8
.L1560:
	bl	decrement_vpc_count
	b	.L1548
.L1549:
	ldr	r2, [r7, #4]
	cmp	r3, r2
	bne	.L1559
.L1548:
	adds	r5, r5, #1
	b	.L1546
.L1551:
	bl	INSERT_FREE_LIST
	b	.L1552
.L1553:
	add	sp, sp, #8
	@ sp needed
	pop	{r4, r5, r6, r7, r8, r9, r10, pc}
.L1562:
	.align	2
.L1561:
	.word	__stack_chk_guard
	.word	.LANCHOR0
	.word	.LANCHOR2
	.word	.LANCHOR0+124
	.word	.LANCHOR1+549
	.word	.LC8
	.fnend
	.size	FtlGcFreeTempBlock, .-FtlGcFreeTempBlock
	.align	1
	.global	FtlGcPageRecovery
	.syntax unified
	.thumb
	.thumb_func
	.fpu softvfp
	.type	FtlGcPageRecovery, %function
FtlGcPageRecovery:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	push	{r4, lr}
	.save {r4, lr}
	ldr	r4, .L1565
	ldrh	r1, [r4, #302]
	add	r0, r4, #124
	bl	FtlGcScanTempBlk
	ldrh	r2, [r4, #126]
	ldrh	r3, [r4, #302]
	cmp	r2, r3
	bcc	.L1563
	addw	r0, r4, #3396
	bl	FtlMapBlkWriteDump_data
	movs	r0, #0
	bl	FtlGcFreeTempBlock
	movs	r3, #0
	str	r3, [r4, #3448]
.L1563:
	pop	{r4, pc}
.L1566:
	.align	2
.L1565:
	.word	.LANCHOR0
	.fnend
	.size	FtlGcPageRecovery, .-FtlGcPageRecovery
	.align	1
	.global	FtlPowerLostRecovery
	.syntax unified
	.thumb
	.thumb_func
	.fpu softvfp
	.type	FtlPowerLostRecovery, %function
FtlPowerLostRecovery:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	push	{r4, r5, r6, lr}
	.save {r4, r5, r6, lr}
	movs	r5, #0
	ldr	r4, .L1568
	add	r6, r4, #24
	str	r5, [r4, #3480]
	mov	r0, r6
	adds	r4, r4, #76
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
.L1569:
	.align	2
.L1568:
	.word	.LANCHOR0
	.fnend
	.size	FtlPowerLostRecovery, .-FtlPowerLostRecovery
	.align	1
	.global	Ftl_gc_temp_data_write_back
	.syntax unified
	.thumb
	.thumb_func
	.fpu softvfp
	.type	Ftl_gc_temp_data_write_back, %function
Ftl_gc_temp_data_write_back:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	push	{r4, r5, r6, lr}
	.save {r4, r5, r6, lr}
	movs	r3, #0
	ldr	r4, .L1578
	movs	r5, #0
	movs	r6, #20
	mov	r2, r3
	ldr	r1, [r4, #3172]
	ldr	r0, [r4, #3296]
	bl	FlashProgPages
.L1571:
	ldr	r1, [r4, #3172]
	uxth	r3, r5
	cmp	r1, r3
	bhi	.L1574
	ldr	r0, [r4, #3296]
	bl	FtlGcBufFree
	ldrh	r3, [r4, #128]
	movs	r0, #0
	str	r0, [r4, #3172]
	cbnz	r3, .L1570
	movs	r0, #1
	bl	FtlGcFreeTempBlock
	movs	r0, #1
.L1570:
	pop	{r4, r5, r6, pc}
.L1574:
	muls	r3, r6, r3
	ldr	r2, [r4, #3296]
	adds	r5, r5, #1
	adds	r1, r2, r3
	ldr	r2, [r2, r3]
	ldr	r0, [r1, #12]
	ldr	r1, [r1, #4]
	adds	r3, r2, #1
	it	ne
	ldrne	r2, [r0, #8]
	ldr	r0, [r0, #12]
	bl	FtlGcUpdatePage
	b	.L1571
.L1579:
	.align	2
.L1578:
	.word	.LANCHOR0
	.fnend
	.size	Ftl_gc_temp_data_write_back, .-Ftl_gc_temp_data_write_back
	.align	1
	.global	Ftl_get_new_temp_ppa
	.syntax unified
	.thumb
	.thumb_func
	.fpu softvfp
	.type	Ftl_get_new_temp_ppa, %function
Ftl_get_new_temp_ppa:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	push	{r3, r4, r5, lr}
	.save {r3, r4, r5, lr}
	movw	r3, #65535
	ldr	r4, .L1583
	ldrh	r2, [r4, #124]
	cmp	r2, r3
	beq	.L1581
	ldrh	r3, [r4, #128]
	cbnz	r3, .L1582
.L1581:
	movs	r0, #0
	movs	r5, #0
	bl	FtlGcFreeTempBlock
	ldr	r0, .L1583+4
	strb	r5, [r4, #132]
	bl	allocate_data_superblock
	strh	r5, [r4, #3200]	@ movhi
	strh	r5, [r4, #3208]	@ movhi
	bl	l2p_flush
	mov	r0, r5
	bl	FtlEctTblFlush
	bl	FtlVpcTblFlush
.L1582:
	ldr	r0, .L1583+4
	pop	{r3, r4, r5, lr}
	b	get_new_active_ppa
.L1584:
	.align	2
.L1583:
	.word	.LANCHOR0
	.word	.LANCHOR0+124
	.fnend
	.size	Ftl_get_new_temp_ppa, .-Ftl_get_new_temp_ppa
	.align	1
	.global	rk_ftl_garbage_collect
	.syntax unified
	.thumb
	.thumb_func
	.fpu softvfp
	.type	rk_ftl_garbage_collect, %function
rk_ftl_garbage_collect:
	.fnstart
	@ args = 0, pretend = 0, frame = 40
	@ frame_needed = 0, uses_anonymous_args = 0
	ldr	r3, .L1717
	push	{r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.save {r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.pad #52
	sub	sp, sp, #52
	mov	r9, r0
	ldr	r3, [r3]
	str	r3, [sp, #44]
	ldr	r3, .L1717+4
	ldr	r0, [r3, #3276]
	mov	r4, r3
	cmp	r0, #0
	bne	.L1669
	ldrh	r2, [r3, #2528]
	cmp	r2, #47
	bls	.L1585
	ldr	r2, .L1717+8
	ldrh	r1, [r2, #4]
	movw	r2, #65535
	cmp	r1, r2
	bne	.L1587
.L1590:
	ldrh	r2, [r4, #220]
	movw	r1, #65535
	cmp	r2, r1
	bne	.L1588
.L1589:
	bl	FtlReadRefresh
	ldr	r3, [r4, #3160]
	movw	r7, #65535
	ldrh	r1, [r4, #172]
	adds	r3, r3, #1
	add	r3, r3, r9, lsl #7
	cmp	r1, r7
	str	r3, [r4, #3160]
	bne	.L1591
	ldrh	r2, [r4, #124]
	cmp	r2, r1
	bne	.L1592
	ldrh	r6, [r4, #222]
	cmp	r6, r2
	bne	.L1593
	ldrh	r2, [r4, #224]
	cmp	r2, #24
	ite	cc
	movcc	r2, #5120
	movcs	r2, #1024
	cmp	r3, r2
	bls	.L1593
	movs	r5, #0
	str	r5, [r4, #3160]
	strh	r5, [r4, #3664]	@ movhi
	bl	GetSwlReplaceBlock
	cmp	r0, r6
	mov	r7, r0
	bne	.L1673
	ldrh	r2, [r4, #224]
	ldrh	r3, [r4, #3158]
	cmp	r2, r3
	bcs	.L1596
	movs	r0, #64
	bl	List_get_gc_head_node
	uxth	r3, r0
	cmp	r3, r7
	beq	.L1598
	mov	r0, r3
	ldr	r3, [r4, #72]
	ldrh	r3, [r3, r0, lsl #1]
	cmp	r3, #7
	bhi	.L1599
	mov	r0, r5
	bl	List_get_gc_head_node
	uxth	r5, r0
	movs	r3, #128
	strh	r3, [r4, #3158]	@ movhi
	cmp	r5, r7
	bne	.L1595
.L1598:
	bl	FtlGcReFreshBadBlk
	cmp	r9, #0
	bne	.L1601
	movw	r3, #65535
	cmp	r7, r3
	bne	.L1601
.L1666:
	ldrh	r3, [r4, #224]
	cmp	r3, #24
	bhi	.L1674
	cmp	r3, #16
	ldrh	r6, [r4, #302]
	bls	.L1603
	lsrs	r6, r6, #5
.L1602:
	ldrh	r2, [r4, #3156]
	cmp	r2, r3
	bcs	.L1606
	ldrh	r3, [r4, #124]
	movw	r2, #65535
	cmp	r3, r2
	bne	.L1607
	ldrh	r2, [r4, #222]
	cmp	r2, r3
	bne	.L1607
	ldrh	r0, [r4, #3664]
	cbnz	r0, .L1608
	ldr	r3, [r4, #2556]
	ldr	r2, [r4, #2548]
	add	r3, r3, r3, lsl #1
	cmp	r2, r3, lsr #2
	bcs	.L1609
.L1608:
	ldrh	r3, [r4, #2624]
	add	r3, r3, r3, lsl #1
	asrs	r3, r3, #2
.L1713:
	strh	r3, [r4, #3156]	@ movhi
	movs	r3, #0
	str	r3, [r4, #3168]
.L1585:
	ldr	r3, .L1717
	ldr	r2, [sp, #44]
	ldr	r3, [r3]
	cmp	r2, r3
	beq	.L1668
	bl	__stack_chk_fail
.L1587:
	ldrh	r3, [r3, #124]
	cmp	r3, r2
	beq	.L1590
	movs	r0, #1
	bl	FtlGcFreeTempBlock
	cmp	r0, #0
	beq	.L1590
	movs	r0, #1
	b	.L1585
.L1588:
	ldrh	r3, [r4, #222]
	cmp	r3, r1
	itt	eq
	strheq	r2, [r4, #222]	@ movhi
	strheq	r3, [r4, #220]	@ movhi
	b	.L1589
.L1599:
	movs	r3, #64
.L1712:
	strh	r3, [r4, #3158]	@ movhi
	b	.L1598
.L1596:
	movs	r3, #80
	b	.L1712
.L1673:
	mov	r5, r0
.L1595:
	ldr	r3, [r4, #72]
	mov	r7, r5
	ldrh	r1, [r4, #3156]
	ldrh	r2, [r4, #224]
	ldrh	r3, [r3, r5, lsl #1]
	str	r1, [sp, #4]
	ldr	r1, [r4, #2536]
	ldr	r0, .L1717+12
	ldrh	r1, [r1, r5, lsl #1]
	str	r1, [sp]
	mov	r1, r5
	bl	sftl_printk
	b	.L1598
.L1603:
	cmp	r3, #12
	bls	.L1604
	lsrs	r6, r6, #4
	b	.L1602
.L1604:
	cmp	r3, #8
	bls	.L1602
	lsrs	r6, r6, #2
	b	.L1602
.L1674:
	movs	r6, #1
	b	.L1602
.L1609:
	movs	r3, #18
	b	.L1713
.L1607:
	ldrh	r3, [r4, #2624]
	add	r3, r3, r3, lsl #1
	asrs	r3, r3, #2
	strh	r3, [r4, #3156]	@ movhi
.L1606:
	ldrh	r3, [r4, #3210]
	cbz	r3, .L1675
	adds	r6, r6, #32
	uxth	r6, r6
.L1675:
	movw	r7, #65535
.L1612:
	ldrh	r3, [r4, #172]
	movw	r2, #65535
	cmp	r3, r2
	bne	.L1621
	cmp	r7, r3
	beq	.L1622
	strh	r7, [r4, #172]	@ movhi
.L1623:
	ldrh	r0, [r4, #172]
	movw	r5, #65535
	movs	r3, #0
	strb	r3, [r4, #180]
	cmp	r0, r5
	beq	.L1621
	bl	IsBlkInGcList
	cbz	r0, .L1626
	strh	r5, [r4, #172]	@ movhi
.L1626:
	ldrh	r2, [r4, #172]
	movw	r3, #65535
	cmp	r2, r3
	beq	.L1621
	ldr	r0, .L1717+16
	bl	make_superblock
	movs	r3, #0
	ldrh	r2, [r4, #172]
	strh	r3, [r4, #174]	@ movhi
	strb	r3, [r4, #178]
	strh	r3, [r4, #3666]	@ movhi
	ldr	r3, [r4, #72]
	ldrh	r3, [r3, r2, lsl #1]
	strh	r3, [r4, #3668]	@ movhi
.L1621:
	ldrh	r3, [r4, #172]
	ldrh	r2, [r4, #24]
	cmp	r2, r3
	beq	.L1627
	ldrh	r2, [r4, #76]
	cmp	r2, r3
	beq	.L1627
	ldrh	r2, [r4, #124]
	cmp	r2, r3
	bne	.L1628
.L1627:
	movw	r3, #65535
	strh	r3, [r4, #172]	@ movhi
.L1628:
	ldr	r5, .L1717+4
	mov	r4, r5
.L1664:
	ldrh	r10, [r5, #172]
	movw	r3, #65535
	cmp	r10, r3
	bne	.L1629
	movs	r3, #0
	mov	fp, r3
	str	r3, [r5, #3168]
.L1630:
	ldrh	r8, [r4, #3164]
	mov	r0, r8
	bl	List_get_gc_head_node
	uxth	r2, r0
	cmp	r2, r10
	strh	r2, [r4, #172]	@ movhi
	bne	.L1631
	movs	r3, #0
	movs	r0, #8
	strh	r3, [r4, #3164]	@ movhi
	b	.L1585
.L1613:
	movs	r3, #0
	ldrh	r2, [r4, #224]
	str	r3, [r4, #3168]
	ldrh	r3, [r4, #3156]
	cmp	r2, r3
	bls	.L1614
	ldrh	r3, [r4, #3664]
	cbnz	r3, .L1615
	ldr	r3, [r4, #2556]
	ldr	r2, [r4, #2548]
	add	r3, r3, r3, lsl #1
	cmp	r2, r3, lsr #2
	bcs	.L1616
.L1615:
	ldrh	r3, [r4, #2624]
	add	r3, r3, r3, lsl #1
	asrs	r3, r3, #2
.L1714:
	strh	r3, [r4, #3156]	@ movhi
	bl	FtlReadRefresh
	movs	r0, #0
	bl	List_get_gc_head_node
	ldr	r3, [r4, #72]
	uxth	r0, r0
	ldrh	r3, [r3, r0, lsl #1]
	cmp	r3, #4
	bls	.L1614
.L1716:
	ldrh	r0, [r4, #3664]
	b	.L1585
.L1616:
	movs	r3, #18
	b	.L1714
.L1614:
	ldrh	r0, [r4, #3664]
	cmp	r0, #0
	bne	.L1618
	ldrh	r5, [r4, #2624]
	add	r3, r5, r5, lsl #1
	asrs	r3, r3, #2
	strh	r3, [r4, #3156]	@ movhi
	bl	List_get_gc_head_node
	ldr	r3, [r4, #72]
	uxth	r0, r0
	ldrh	r1, [r4, #304]
	ldrh	r2, [r3, r0, lsl #1]
	ldrh	r3, [r4, #232]
	muls	r3, r1, r3
	add	r3, r3, r3, lsr #31
	cmp	r2, r3, asr #1
	ble	.L1619
	ldrh	r3, [r4, #224]
	subs	r5, r5, #1
	cmp	r3, r5
	blt	.L1619
	bl	FtlReadRefresh
	b	.L1716
.L1718:
	.align	2
.L1717:
	.word	__stack_chk_guard
	.word	.LANCHOR0
	.word	.LANCHOR2
	.word	.LC124
	.word	.LANCHOR0+172
.L1619:
	cmp	r2, #0
	bne	.L1618
	movw	r0, #65535
	bl	decrement_vpc_count
	ldrh	r0, [r4, #224]
	adds	r0, r0, #1
	b	.L1585
.L1622:
	ldrh	r3, [r4, #222]
	cmp	r3, r7
	beq	.L1623
	ldr	r2, [r4, #72]
	ldrh	r3, [r2, r3, lsl #1]
	cbnz	r3, .L1624
	strh	r7, [r4, #222]	@ movhi
.L1624:
	ldrh	r3, [r4, #222]
	strh	r3, [r4, #172]	@ movhi
	movw	r3, #65535
	strh	r3, [r4, #222]	@ movhi
	b	.L1623
.L1631:
	str	r0, [sp, #16]
	mov	r0, r2
	str	r2, [sp, #12]
	add	r8, r8, #1
	bl	IsBlkInGcList
	ldr	r2, [sp, #12]
	ldr	r3, [sp, #16]
	cbz	r0, .L1632
	strh	r8, [r4, #3164]	@ movhi
	b	.L1630
.L1632:
	ldrh	lr, [r4, #232]
	uxth	r8, r8
	ldrh	r1, [r4, #302]
	uxth	r3, r3
	ldr	r0, [r4, #72]
	strh	r8, [r4, #3164]	@ movhi
	mul	r1, lr, r1
	ldrh	ip, [r0, r3, lsl #1]
	add	lr, r1, r1, lsr #31
	cmp	ip, lr, asr #1
	bgt	.L1634
	cmp	r8, #48
	bls	.L1635
	cmp	ip, #8
	bls	.L1635
	ldrh	ip, [r4, #3200]
	cmp	ip, #35
	bhi	.L1635
.L1634:
	strh	fp, [r4, #3164]	@ movhi
.L1635:
	ldrh	r3, [r0, r3, lsl #1]
	cmp	r1, r3
	bgt	.L1636
	cmp	r7, r10
	bne	.L1636
	ldrh	r1, [r4, #3164]
	cmp	r1, #3
	bhi	.L1636
	movs	r3, #0
	strh	r7, [r4, #172]	@ movhi
	strh	r3, [r4, #3164]	@ movhi
	b	.L1716
.L1636:
	cbnz	r3, .L1637
	movw	r0, #65535
	bl	decrement_vpc_count
	ldrh	r3, [r4, #3164]
	adds	r3, r3, #1
	strh	r3, [r4, #3164]	@ movhi
	b	.L1630
.L1637:
	movs	r3, #0
	strb	r3, [r4, #180]
	ldrh	r3, [r4, #24]
	cmp	r3, r2
	bne	.L1638
	movw	r2, #717
	ldr	r1, .L1719
	ldr	r0, .L1719+4
	bl	sftl_printk
.L1638:
	ldrh	r2, [r4, #172]
	ldrh	r3, [r4, #76]
	cmp	r2, r3
	bne	.L1639
	movw	r2, #718
	ldr	r1, .L1719
	ldr	r0, .L1719+4
	bl	sftl_printk
.L1639:
	ldrh	r2, [r4, #172]
	ldrh	r3, [r4, #124]
	cmp	r2, r3
	bne	.L1640
	movw	r2, #719
	ldr	r1, .L1719
	ldr	r0, .L1719+4
	bl	sftl_printk
.L1640:
	ldr	r0, .L1719+8
	bl	make_superblock
	ldrh	r1, [r4, #172]
	movs	r3, #0
	ldr	r2, [r4, #72]
	strh	r3, [r4, #3666]	@ movhi
	ldrh	r2, [r2, r1, lsl #1]
	strh	r3, [r4, #174]	@ movhi
	strb	r3, [r4, #178]
	strh	r2, [r4, #3668]	@ movhi
.L1629:
	movs	r3, #1
	str	r3, [r4, #3276]
	ldrh	r3, [r4, #302]
	str	r3, [sp, #16]
	cmp	r9, #0
	beq	.L1641
	ldr	r2, [sp, #16]
	ldrh	r3, [r4, #232]
	ldrh	r1, [r4, #172]
	muls	r3, r2, r3
	ldr	r2, [r4, #72]
	ldrh	r2, [r2, r1, lsl #1]
	subs	r3, r3, r2
	it	mi
	addmi	r3, r3, #3
	add	r6, r6, r3, asr #2
	uxth	r6, r6
.L1641:
	ldrh	r3, [r4, #174]
	ldr	r1, [sp, #16]
	adds	r2, r3, r6
	cmp	r2, r1
	itt	gt
	movgt	r2, r1
	subgt	r6, r2, r3
	mov	r3, #0
	it	gt
	uxthgt	r6, r6
.L1715:
	str	r3, [sp, #20]
	ldrh	r3, [sp, #20]
	cmp	r6, r3
	bls	.L1652
	ldr	r3, [sp, #20]
	movw	r10, #65535
	ldrh	r8, [r5, #174]
	mov	fp, #20
	ldrh	ip, [r5, #232]
	ldr	r0, [r5, #3188]
	add	r8, r8, r3
	ldr	r3, .L1719+12
	str	r3, [sp, #24]
	movs	r3, #0
	str	r3, [sp, #12]
	b	.L1653
.L1646:
	ldr	r1, [sp, #24]
	ldrh	r2, [r1, #2]!
	cmp	r2, r10
	str	r1, [sp, #24]
	beq	.L1645
	ldr	r1, [sp, #12]
	orr	r2, r8, r2, lsl #10
	mla	lr, fp, r1, r0
	str	r2, [lr, #4]
	mov	r2, r1
	adds	r2, r2, #1
	uxth	r2, r2
	str	r2, [sp, #12]
.L1645:
	adds	r3, r3, #1
.L1653:
	uxth	r2, r3
	cmp	ip, r2
	bhi	.L1646
	mov	fp, #0
	ldrb	r2, [r5, #180]	@ zero_extendqisi2
	ldr	r1, [sp, #12]
	bl	FlashReadPages
.L1647:
	ldr	r2, [sp, #12]
	uxth	r3, fp
	cmp	r2, r3
	bhi	.L1651
	ldr	r3, [sp, #20]
	adds	r3, r3, #1
	b	.L1715
.L1651:
	mov	r8, #20
	ldr	r3, [r5, #3188]
	mul	r8, r8, fp
	add	r2, r3, r8
	ldr	r3, [r3, r8]
	adds	r3, r3, #1
	beq	.L1648
	ldr	r10, [r2, #12]
	movw	r3, #61589
	ldrh	r2, [r10]
	cmp	r2, r3
	bne	.L1648
	ldr	r3, [r10, #8]
	adds	r2, r3, #1
	bne	.L1649
	movw	r2, #753
	ldr	r1, .L1719
	ldr	r0, .L1719+4
	str	r3, [sp, #24]
	bl	sftl_printk
	ldr	r3, [sp, #24]
.L1649:
	movs	r2, #0
	mov	r0, r3
	add	r1, sp, #36
	bl	log2phys
	ldr	r0, [r4, #3188]
	ldr	r3, [sp, #36]
	add	r0, r0, r8
	ldr	r2, [r0, #4]
	cmp	r2, r3
	bne	.L1648
	ldrh	r3, [r4, #3666]
	movs	r2, #20
	ldr	r1, [r4, #3172]
	str	r2, [sp, #28]
	adds	r3, r3, #1
	strh	r3, [r4, #3666]	@ movhi
	ldr	r3, [r4, #3296]
	mla	r3, r2, r1, r3
	ldr	r1, [r0, #16]
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
	movs	r1, #1
	ldr	r2, [r2, #12]
	str	r2, [r3, #12]
	ldr	r3, [sp, #36]
	str	r3, [r10, #12]
	ldrh	r3, [r4, #124]
	strh	r3, [r10, #2]	@ movhi
	ldr	r3, [r4, #2596]
	ldr	r0, [r4, #3188]
	str	r3, [r10, #4]
	ldr	r3, [r4, #3172]
	add	r0, r0, r8
	adds	r3, r3, #1
	str	r3, [r4, #3172]
	bl	FtlGcBufAlloc
	ldrb	r2, [r4, #131]	@ zero_extendqisi2
	ldr	r3, [r4, #3172]
	cmp	r2, r3
	beq	.L1650
	ldrh	r3, [r4, #128]
	cbnz	r3, .L1648
.L1650:
	bl	Ftl_gc_temp_data_write_back
	cbz	r0, .L1648
	ldr	r3, .L1719+16
	movs	r2, #0
	movw	r1, #65535
	str	r2, [r3, #3276]
	strh	r1, [r3, #172]	@ movhi
	strh	r2, [r3, #174]	@ movhi
	ldrh	r0, [r3, #3664]
	b	.L1585
.L1648:
	add	fp, fp, #1
	b	.L1647
.L1652:
	ldrh	r3, [r5, #174]
	add	r6, r6, r3
	ldr	r3, [sp, #16]
	uxth	r6, r6
	cmp	r3, r6
	strh	r6, [r5, #174]	@ movhi
	bhi	.L1654
	ldr	r3, [r5, #3172]
	cbz	r3, .L1655
	bl	Ftl_gc_temp_data_write_back
	cbz	r0, .L1655
	movs	r3, #0
	ldrh	r0, [r5, #3664]
	str	r3, [r5, #3276]
	b	.L1585
.L1655:
	ldrh	r6, [r5, #3666]
	cbnz	r6, .L1656
	ldrh	r2, [r5, #172]
	ldr	r3, [r5, #72]
	ldrh	r3, [r3, r2, lsl #1]
	cbz	r3, .L1656
.L1657:
	ldr	r3, [r5, #2556]
	cmp	r6, r3
	bcs	.L1662
	movs	r2, #0
	add	r1, sp, #40
	mov	r0, r6
	bl	log2phys
	ldr	r0, [sp, #40]
	adds	r3, r0, #1
	beq	.L1658
	ubfx	r0, r0, #10, #16
	bl	P2V_block_in_plane
	ldrh	r3, [r5, #172]
	cmp	r3, r0
	bne	.L1658
.L1662:
	ldr	r3, [r5, #2556]
	cmp	r6, r3
	bcc	.L1656
	ldrh	r2, [r5, #172]
	movs	r1, #0
	ldr	r3, [r5, #72]
	strh	r1, [r3, r2, lsl #1]	@ movhi
	ldrh	r0, [r5, #172]
	bl	update_vpc_list
	bl	l2p_flush
	bl	FtlVpcTblFlush
.L1656:
	movw	r3, #65535
	strh	r3, [r5, #172]	@ movhi
.L1654:
	movs	r3, #0
	str	r3, [r5, #3276]
	ldrh	r3, [r5, #224]
	cmp	r3, #2
	bhi	.L1663
	ldrh	r6, [r5, #302]
	b	.L1664
.L1658:
	adds	r6, r6, #1
	b	.L1657
.L1663:
	ldrh	r0, [r5, #3664]
	cmp	r0, #0
	bne	.L1585
	adds	r0, r3, #1
	b	.L1585
.L1669:
	movs	r0, #0
	b	.L1585
.L1591:
	cmp	r9, #0
	beq	.L1666
.L1601:
	ldrh	r2, [r4, #124]
	movw	r3, #65535
	cmp	r2, r3
	bne	.L1678
.L1667:
	ldrh	r3, [r4, #222]
	movw	r2, #65535
	cmp	r3, r2
	bne	.L1678
	cmp	r7, r3
	bne	.L1678
	ldrh	r3, [r4, #172]
	cmp	r3, r7
	beq	.L1613
.L1618:
	movw	r7, #65535
.L1678:
	movs	r6, #1
	b	.L1612
.L1593:
	cmp	r9, #0
	beq	.L1666
	movw	r7, #65535
	b	.L1667
.L1720:
	.align	2
.L1719:
	.word	.LANCHOR1+568
	.word	.LC8
	.word	.LANCHOR0+172
	.word	.LANCHOR0+186
	.word	.LANCHOR0
.L1592:
	cmp	r9, #0
	bne	.L1618
	b	.L1666
.L1668:
	add	sp, sp, #52
	@ sp needed
	pop	{r4, r5, r6, r7, r8, r9, r10, fp, pc}
	.fnend
	.size	rk_ftl_garbage_collect, .-rk_ftl_garbage_collect
	.align	1
	.global	FtlRead
	.syntax unified
	.thumb
	.thumb_func
	.fpu softvfp
	.type	FtlRead, %function
FtlRead:
	.fnstart
	@ args = 0, pretend = 0, frame = 56
	@ frame_needed = 0, uses_anonymous_args = 0
	push	{r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.save {r4, r5, r6, r7, r8, r9, r10, fp, lr}
	mov	r10, r3
	ldr	r3, .L1757
	cmp	r0, #16
	.pad #60
	sub	sp, sp, #60
	mov	r6, r1
	str	r2, [sp, #4]
	ldr	r3, [r3]
	str	r3, [sp, #52]
	bne	.L1722
	mov	r2, r10
	ldr	r1, [sp, #4]
	add	r0, r6, #256
	bl	FtlVendorPartRead
	mov	r5, r0
.L1721:
	ldr	r3, .L1757
	mov	r0, r5
	ldr	r2, [sp, #52]
	ldr	r3, [r3]
	cmp	r2, r3
	beq	.L1743
	bl	__stack_chk_fail
.L1722:
	ldr	r4, .L1757+4
	ldr	r3, [r4, #340]
	cmp	r1, r3
	bcs	.L1746
	ldr	r2, [sp, #4]
	cmp	r2, r3
	bhi	.L1746
	adds	r2, r1, r2
	cmp	r3, r2
	str	r2, [sp, #12]
	bcc	.L1746
	ldr	r3, .L1757+8
	ldr	r5, [r3]
	adds	r7, r5, #1
	beq	.L1721
	ldrh	r5, [r4, #258]
	mov	r0, r6
	mov	r9, #0
	mov	r8, r9
	mov	r1, r5
	bl	__aeabi_uidiv
	ldr	r3, [sp, #12]
	mov	r1, r5
	str	r0, [sp, #16]
	mov	r5, r9
	subs	r0, r3, #1
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
.L1724:
	ldr	r3, [sp, #8]
	cbnz	r3, .L1741
	ldr	r3, .L1757+4
	ldrh	r2, [r3, #3210]
	cbnz	r2, .L1742
	ldrh	r3, [r3, #224]
	cmp	r3, #31
	bhi	.L1721
.L1742:
	movs	r1, #1
	movs	r0, #0
	bl	rk_ftl_garbage_collect
	b	.L1721
.L1741:
	movs	r2, #0
	add	r1, sp, #48
	mov	r0, r7
	bl	log2phys
	ldr	r2, [sp, #48]
	adds	r0, r2, #1
	bne	.L1725
	mov	fp, #0
.L1726:
	ldrh	r0, [r4, #258]
	cmp	fp, r0
	bcc	.L1728
.L1729:
	ldr	r3, [sp, #8]
	adds	r7, r7, #1
	subs	r3, r3, #1
	str	r3, [sp, #8]
	beq	.L1733
	ldrh	r3, [r4, #232]
	cmp	r8, r3, lsl #2
	bne	.L1724
.L1733:
	cmp	r8, #0
	beq	.L1724
	movs	r2, #0
	mov	r1, r8
	ldr	r0, [r4, #3292]
	mov	fp, #0
	bl	FlashReadPages
	lsl	r3, r9, #9
	str	r3, [sp, #40]
	ldr	r3, [sp, #24]
	lsls	r3, r3, #9
	str	r3, [sp, #32]
	ldr	r3, [sp, #28]
	lsls	r3, r3, #9
	str	r3, [sp, #36]
.L1740:
	movs	r3, #20
	ldr	r2, [r4, #3292]
	mul	r3, r3, fp
	ldr	r0, [sp, #16]
	add	r2, r2, r3
	ldr	r1, [r2, #16]
	cmp	r0, r1
	bne	.L1735
	ldr	r1, [r2, #8]
	ldr	r2, [r4, #3316]
	cmp	r1, r2
	bne	.L1736
	str	r3, [sp, #44]
	mov	r0, r10
	ldr	r3, [sp, #32]
	ldr	r2, [sp, #36]
	add	r1, r1, r3
.L1756:
	bl	ftl_memcpy
	ldr	r3, [sp, #44]
.L1736:
	ldr	ip, [r4, #3292]
	add	r2, ip, r3
	ldr	r0, [r2, #12]
	ldr	r1, [r2, #16]
	ldr	r0, [r0, #8]
	cmp	r1, r0
	ittt	ne
	ldrne	r1, [r4, #2716]
	addne	r1, r1, #1
	strne	r1, [r4, #2716]
	ldr	r0, [ip, r3]
	adds	r1, r0, #1
	itttt	eq
	ldreq	r1, [r4, #2716]
	moveq	r5, r0
	addeq	r1, r1, #1
	streq	r1, [r4, #2716]
	ldr	r3, [ip, r3]
	cmp	r3, #256
	bne	.L1739
	ldr	r0, [r2, #4]
	ubfx	r0, r0, #10, #16
	bl	P2V_block_in_plane
	bl	FtlGcRefreshBlock
.L1739:
	add	fp, fp, #1
	cmp	r8, fp
	bne	.L1740
	mov	r8, #0
	b	.L1724
.L1728:
	mla	r0, r0, r7, fp
	cmp	r6, r0
	bhi	.L1727
	ldr	r3, [sp, #12]
	cmp	r3, r0
	bls	.L1727
	subs	r0, r0, r6
	mov	r2, #512
	movs	r1, #0
	add	r0, r10, r0, lsl #9
	bl	ftl_memset
.L1727:
	add	fp, fp, #1
	b	.L1726
.L1725:
	ldr	r3, [r4, #3292]
	movs	r1, #20
	ldrh	fp, [r4, #258]
	mla	r3, r1, r8, r3
	str	r2, [r3, #4]
	ldr	r2, [sp, #16]
	cmp	r7, r2
	bne	.L1730
	ldr	r2, [r4, #3316]
	mov	r1, fp
	mov	r0, r6
	str	r3, [sp, #32]
	str	r2, [r3, #8]
	bl	__aeabi_uidivmod
	sub	r2, fp, r1
	str	r1, [sp, #24]
	ldr	r1, [sp, #4]
	mov	r3, r2
	cmp	r2, r1
	it	cs
	movcs	r3, r1
	cmp	r3, fp
	str	r3, [sp, #28]
	ldr	r3, [sp, #32]
	bne	.L1731
	str	r10, [r3, #8]
.L1731:
	ldrh	r2, [r4, #312]
	ldr	r1, [r4, #3340]
	str	r7, [r3, #16]
	mul	r2, r8, r2
	add	r8, r8, #1
	bic	r2, r2, #3
	add	r2, r2, r1
	str	r2, [r3, #12]
	b	.L1729
.L1730:
	ldr	r2, [sp, #20]
	cmp	r7, r2
	bne	.L1732
	ldr	r2, [r4, #3320]
	ldr	r1, [sp, #12]
	str	r2, [r3, #8]
	mul	r2, fp, r7
	sub	r9, r1, r2
	cmp	fp, r9
	bne	.L1731
.L1755:
	subs	r2, r2, r6
	add	r2, r10, r2, lsl #9
	str	r2, [r3, #8]
	b	.L1731
.L1732:
	mul	r2, r7, fp
	b	.L1755
.L1735:
	ldr	r0, [sp, #20]
	cmp	r0, r1
	bne	.L1736
	ldr	r1, [r2, #8]
	ldr	r2, [r4, #3320]
	cmp	r1, r2
	bne	.L1736
	str	r3, [sp, #44]
	ldrh	r0, [r4, #258]
	ldr	r3, [sp, #20]
	ldr	r2, [sp, #40]
	muls	r0, r3, r0
	subs	r0, r0, r6
	add	r0, r10, r0, lsl #9
	b	.L1756
.L1746:
	mov	r5, #-1
	b	.L1721
.L1743:
	add	sp, sp, #60
	@ sp needed
	pop	{r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L1758:
	.align	2
.L1757:
	.word	__stack_chk_guard
	.word	.LANCHOR0
	.word	.LANCHOR2
	.fnend
	.size	FtlRead, .-FtlRead
	.align	1
	.global	sftl_read
	.syntax unified
	.thumb
	.thumb_func
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
	movs	r0, #0
	b	FtlRead
	.fnend
	.size	sftl_read, .-sftl_read
	.align	1
	.global	FtlWrite
	.syntax unified
	.thumb
	.thumb_func
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
	ldr	r3, .L1815
	str	r2, [sp, #8]
	ldr	r3, [r3]
	str	r3, [sp, #68]
	bne	.L1761
	ldr	r2, [sp, #12]
	add	r0, r10, #256
	ldr	r1, [sp, #8]
	bl	FtlVendorPartWrite
.L1760:
	ldr	r3, .L1815
	ldr	r2, [sp, #68]
	ldr	r3, [r3]
	cmp	r2, r3
	beq	.L1798
	bl	__stack_chk_fail
.L1761:
	ldr	r5, .L1815+4
	ldr	r3, [r5, #340]
	cmp	r1, r3
	bcs	.L1801
	ldr	r2, [sp, #8]
	cmp	r2, r3
	bhi	.L1801
	adds	r6, r1, r2
	cmp	r3, r6
	bcc	.L1801
	ldr	r4, .L1815+8
	ldr	r0, [r4]
	adds	r7, r0, #1
	beq	.L1760
	ldrh	r7, [r5, #258]
	mov	r3, #2048
	str	r3, [r5, #3672]
	mov	r0, r10
	mov	r1, r7
	bl	__aeabi_uidiv
	mov	r1, r7
	str	r0, [sp, #4]
	subs	r0, r6, #1
	bl	__aeabi_uidiv
	ldr	r2, [sp, #4]
	str	r0, [sp, #20]
	subs	r3, r0, r2
	ldr	r2, [sp, #8]
	str	r3, [sp, #24]
	adds	r3, r3, #1
	str	r3, [sp]
	ldr	r3, [r5, #2584]
	add	r3, r3, r2
	ldr	r2, [sp]
	str	r3, [r5, #2584]
	ldr	r3, [r5, #2568]
	add	r3, r3, r2
	str	r3, [r5, #2568]
	ldr	r3, [r4, #8]
	cbz	r3, .L1803
	ldrh	r2, [r5, #28]
	add	r3, r5, #24
	adds	r5, r5, #76
	cmp	r2, #0
	it	ne
	movne	r5, r3
.L1763:
	ldr	r7, [sp, #4]
	ldr	r4, .L1815+4
.L1764:
	ldr	r3, [sp]
	cbnz	r3, .L1793
	mov	r0, r3
	ldr	r1, [sp, #24]
	bl	rk_ftl_garbage_collect
	ldrh	r3, [r4, #224]
	cmp	r3, #5
	bls	.L1794
.L1796:
	movs	r0, #0
	b	.L1760
.L1803:
	adds	r5, r5, #24
	b	.L1763
.L1793:
	ldrb	r2, [r5, #6]	@ zero_extendqisi2
	ldrh	r3, [r4, #232]
	cmp	r2, r3
	bcc	.L1765
	movw	r2, #1041
	ldr	r1, .L1815+12
	ldr	r0, .L1815+16
	bl	sftl_printk
.L1765:
	ldrh	r2, [r5, #4]
	cbnz	r2, .L1766
	ldr	r3, .L1815+20
	ldr	r6, .L1815+8
	cmp	r5, r3
	bne	.L1767
	ldrh	r8, [r4, #80]
	cmp	r8, #0
	bne	.L1768
	add	r0, r5, #52
	bl	allocate_new_data_superblock
	str	r8, [r6, #8]
.L1768:
	ldr	r5, .L1815+20
	ldr	r0, .L1815+20
	bl	allocate_new_data_superblock
	ldr	r2, [r6, #8]
	add	r3, r5, #52
	cmp	r2, #0
	it	ne
	movne	r5, r3
.L1769:
	ldrh	r3, [r5, #4]
	cbnz	r3, .L1766
	mov	r0, r5
	bl	allocate_new_data_superblock
.L1766:
	ldrh	r2, [r5, #4]
	ldr	r3, [sp]
	cmp	r2, r3
	it	cs
	movcs	r2, r3
	ldrb	r3, [r5, #7]	@ zero_extendqisi2
	lsls	r3, r3, #2
	cmp	r2, r3
	it	cs
	movcs	r2, r3
	ldrh	r3, [r4, #232]
	str	r2, [sp, #36]
	ldrb	r2, [r5, #6]	@ zero_extendqisi2
	cmp	r2, r3
	bcc	.L1770
	movw	r2, #1074
	ldr	r1, .L1815+12
	ldr	r0, .L1815+16
	bl	sftl_printk
.L1770:
	mov	fp, #0
.L1771:
	ldr	r3, [sp, #36]
	cmp	fp, r3
	bne	.L1791
.L1772:
	mov	r3, r5
	movs	r2, #0
	mov	r1, fp
	ldr	r0, [r4, #3300]
	bl	FtlProgPages
	ldr	r3, [sp]
	cmp	fp, r3
	bls	.L1792
	mov	r2, #1152
	ldr	r1, .L1815+12
	ldr	r0, .L1815+16
	bl	sftl_printk
.L1792:
	ldr	r3, [sp]
	sub	r3, r3, fp
	str	r3, [sp]
	b	.L1764
.L1767:
	str	r2, [r6, #8]
	ldrh	r2, [r4, #28]
	cbnz	r2, .L1806
	mov	r0, r5
	bl	allocate_new_data_superblock
	b	.L1769
.L1806:
	mov	r5, r3
	b	.L1766
.L1791:
	ldrh	r3, [r5, #4]
	cmp	r3, #0
	beq	.L1772
	movs	r2, #0
	add	r1, sp, #44
	mov	r0, r7
	mov	r8, #20
	bl	log2phys
	mov	r0, r5
	mul	r8, r8, fp
	bl	get_new_active_ppa
	ldrh	r2, [r4, #312]
	ldr	r3, [r4, #3300]
	mul	r1, r2, fp
	add	r3, r3, r8
	str	r0, [r3, #4]
	str	r7, [r3, #16]
	bic	r1, r1, #3
	str	r1, [sp, #28]
	ldr	r0, [sp, #28]
	ldr	r1, [r4, #3340]
	add	r9, r1, r0
	str	r1, [sp, #32]
	str	r9, [r3, #12]
	movs	r1, #0
	mov	r0, r9
	bl	ftl_memset
	ldr	r3, [sp, #4]
	cmp	r7, r3
	beq	.L1773
	ldr	r3, [sp, #20]
	cmp	r7, r3
	bne	.L1811
	ldrh	r2, [r4, #258]
	ldr	r3, [sp, #8]
	smulbb	r2, r2, r7
	add	r6, r10, r3
	movs	r3, #0
	str	r3, [sp, #16]
	subs	r6, r6, r2
	uxth	r6, r6
	b	.L1776
.L1773:
	ldrh	r6, [r4, #258]
	mov	r0, r10
	mov	r1, r6
	bl	__aeabi_uidivmod
	ldr	r3, [sp, #8]
	subs	r6, r6, r1
	str	r1, [sp, #16]
	cmp	r6, r3
	it	cs
	movcs	r6, r3
.L1776:
	ldrh	r3, [r4, #258]
	cmp	r6, r3
	ldr	r3, [sp, #4]
	bne	.L1777
	cmp	r7, r3
	ldr	r3, [r4, #3300]
	itte	ne
	mulne	r6, r6, r7
	ldrne	r2, [sp, #12]
	ldreq	r2, [sp, #12]
	add	r3, r3, r8
	itett	ne
	subne	r6, r6, r10
	streq	r2, [r3, #8]
	addne	r6, r2, r6, lsl #9
	strne	r6, [r3, #8]
.L1779:
	ldrb	r2, [r5, #6]	@ zero_extendqisi2
	ldrh	r3, [r4, #232]
	cmp	r2, r3
	bcc	.L1788
	movw	r2, #1143
	ldr	r1, .L1815+12
	ldr	r0, .L1815+16
	bl	sftl_printk
.L1788:
	ldr	r2, [sp, #32]
	movw	r3, #61589
	ldr	r1, [sp, #28]
	add	fp, fp, #1
	strh	r3, [r2, r1]	@ movhi
	ldr	r3, [r4, #2596]
	str	r7, [r9, #8]
	adds	r7, r7, #1
	str	r3, [r9, #4]
	adds	r3, r3, #1
	adds	r2, r3, #1
	it	eq
	moveq	r3, #0
	str	r3, [r4, #2596]
	ldr	r3, [sp, #44]
	str	r3, [r9, #12]
	ldrh	r3, [r5]
	strh	r3, [r9, #2]	@ movhi
	b	.L1771
.L1816:
	.align	2
.L1815:
	.word	__stack_chk_guard
	.word	.LANCHOR0
	.word	.LANCHOR2
	.word	.LANCHOR1+591
	.word	.LC8
	.word	.LANCHOR0+24
.L1777:
	cmp	r7, r3
	ldr	r3, [r4, #3300]
	ite	eq
	ldreq	r2, [r4, #3316]
	ldrne	r2, [r4, #3320]
	add	r3, r3, r8
	str	r2, [r3, #8]
	ldr	r3, [sp, #44]
	adds	r0, r3, #1
	beq	.L1782
	str	r3, [sp, #52]
	movs	r1, #1
	ldr	r3, [r4, #3300]
	add	r0, sp, #48
	str	r7, [sp, #64]
	add	r3, r3, r8
	ldr	r2, [r3, #8]
	ldr	r3, [r3, #12]
	str	r2, [sp, #56]
	movs	r2, #0
	str	r3, [sp, #60]
	bl	FlashReadPages
	ldr	r3, [sp, #48]
	adds	r1, r3, #1
	bne	.L1783
	ldr	r2, [r4, #2716]
	ldr	r0, .L1817
	adds	r2, r2, #1
	str	r2, [r4, #2716]
	mov	r2, r7
	ldr	r1, [r9, #8]
	bl	sftl_printk
.L1786:
	ldr	r3, [sp, #4]
	lsls	r2, r6, #9
	cmp	r7, r3
	bne	.L1787
	ldr	r3, [r4, #3300]
	ldr	r1, [sp, #12]
	add	r3, r3, r8
	ldr	r0, [r3, #8]
	ldr	r3, [sp, #16]
	add	r0, r0, r3, lsl #9
.L1813:
	bl	ftl_memcpy
	b	.L1779
.L1783:
	ldr	r3, [r9, #8]
	cmp	r7, r3
	beq	.L1785
	ldr	r3, [r4, #2716]
	mov	r2, r7
	ldr	r0, .L1817+4
	adds	r3, r3, #1
	str	r3, [r4, #2716]
	ldr	r1, [r9, #8]
	bl	sftl_printk
.L1785:
	ldr	r3, [r9, #8]
	cmp	r7, r3
	beq	.L1786
	mov	r2, #1128
	ldr	r1, .L1817+8
	ldr	r0, .L1817+12
	bl	sftl_printk
	b	.L1786
.L1782:
	ldr	r3, [r4, #3300]
	movs	r1, #0
	ldrh	r2, [r4, #310]
	add	r3, r3, r8
	ldr	r0, [r3, #8]
	bl	ftl_memset
	b	.L1786
.L1787:
	ldrh	r1, [r4, #258]
	ldr	r0, [sp, #12]
	ldr	r3, [r4, #3300]
	muls	r1, r7, r1
	add	r3, r3, r8
	sub	r1, r1, r10
	add	r1, r0, r1, lsl #9
	ldr	r0, [r3, #8]
	b	.L1813
.L1811:
	ldr	r3, [r4, #3300]
	ldr	r2, [sp, #12]
	add	r8, r8, r3
	ldrh	r3, [r4, #258]
	muls	r3, r7, r3
	sub	r3, r3, r10
	add	r3, r2, r3, lsl #9
	str	r3, [r8, #8]
	b	.L1779
.L1794:
	mov	r5, #256
	movw	r6, #65535
.L1797:
	ldrh	r3, [r4, #172]
	cmp	r3, r6
	bne	.L1795
	ldrh	r3, [r4, #222]
	cmp	r3, r6
	bne	.L1795
	movs	r0, #0
	bl	List_get_gc_head_node
	uxth	r0, r0
	bl	FtlGcRefreshBlock
.L1795:
	movs	r1, #1
	movs	r3, #128
	mov	r0, r1
	strh	r3, [r4, #3158]	@ movhi
	strh	r3, [r4, #3156]	@ movhi
	bl	rk_ftl_garbage_collect
	movs	r1, #1
	movs	r0, #0
	bl	rk_ftl_garbage_collect
	ldrh	r3, [r4, #224]
	cmp	r3, #2
	bhi	.L1796
	subs	r5, r5, #1
	bne	.L1797
	b	.L1796
.L1801:
	mov	r0, #-1
	b	.L1760
.L1798:
	add	sp, sp, #76
	@ sp needed
	pop	{r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L1818:
	.align	2
.L1817:
	.word	.LC125
	.word	.LC126
	.word	.LANCHOR1+591
	.word	.LC8
	.fnend
	.size	FtlWrite, .-FtlWrite
	.align	1
	.global	sftl_gc
	.syntax unified
	.thumb
	.thumb_func
	.fpu softvfp
	.type	sftl_gc, %function
sftl_gc:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	@ link register save eliminated.
	movs	r1, #1
	mov	r0, r1
	b	rk_ftl_garbage_collect
	.fnend
	.size	sftl_gc, .-sftl_gc
	.align	1
	.global	FtlLoadSysInfo
	.syntax unified
	.thumb
	.thumb_func
	.fpu softvfp
	.type	FtlLoadSysInfo, %function
FtlLoadSysInfo:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	push	{r0, r1, r2, r3, r4, r5, r6, r7, r8, r9, r10, lr}
	.save {r4, r5, r6, r7, r8, r9, r10, lr}
	.pad #16
	movs	r1, #0
	ldr	r4, .L1850
	ldr	r3, [r4, #3304]
	ldrh	r2, [r4, #240]
	ldr	r0, [r4, #72]
	str	r3, [r4, #3464]
	ldr	r3, [r4, #3336]
	lsls	r2, r2, #1
	str	r3, [r4, #3468]
	bl	ftl_memset
	ldrh	r0, [r4, #2628]
	movw	r3, #65535
	cmp	r0, r3
	bne	.L1821
.L1832:
	mov	r0, #-1
.L1820:
	add	sp, sp, #16
	@ sp needed
	pop	{r4, r5, r6, r7, r8, r9, r10, pc}
.L1821:
	movs	r1, #1
	ldr	r8, .L1850+20
	bl	FtlGetLastWrittenPage
	ldr	r9, .L1850+4
	add	r7, r4, #3456
	ldrsh	r6, [r4, #2628]
	sxth	r5, r0
	adds	r0, r0, #1
	strh	r0, [r4, #2630]	@ movhi
.L1823:
	cmp	r5, #0
	bge	.L1829
	movw	r2, #1467
	ldr	r1, .L1850+4
	ldr	r0, .L1850+8
	bl	sftl_printk
	b	.L1828
.L1829:
	orr	r3, r5, r6, lsl #10
	movs	r2, #1
	mov	r1, r2
	str	r3, [r4, #3460]
	mov	r0, r7
	ldr	r3, [r4, #3304]
	str	r3, [r4, #3464]
	bl	FlashReadPages
	ldr	r3, [r4, #3468]
	ldr	r10, [r3, #12]
	cmp	r10, #0
	beq	.L1824
	ldr	r3, [r4, #3456]
	adds	r3, r3, #1
	beq	.L1824
	ldrh	r1, [r4, #310]
	ldr	r0, [r4, #3464]
	bl	js_hash
	cmp	r10, r0
	beq	.L1824
	ldrh	r3, [r4, #2632]
	mov	r2, r6
	str	r0, [sp, #8]
	mov	r1, r9
	str	r10, [sp, #4]
	str	r3, [sp]
	mov	r3, r5
	ldr	r0, .L1850+12
	bl	sftl_printk
	cbnz	r5, .L1825
	ldrh	r3, [r4, #2632]
	cmp	r6, r3
	beq	.L1825
	ldrh	r5, [r4, #304]
	sxth	r6, r3
.L1827:
	subs	r5, r5, #1
	sxth	r5, r5
	b	.L1823
.L1825:
	mov	r3, #-1
	str	r3, [r4, #3456]
.L1824:
	ldr	r3, [r4, #3456]
	adds	r3, r3, #1
	beq	.L1827
	ldr	r3, [r4, #3304]
	ldr	r3, [r3]
	cmp	r3, r8
	bne	.L1827
	ldr	r3, [r4, #3336]
	ldrh	r2, [r3]
	movw	r3, #61604
	cmp	r2, r3
	bne	.L1827
.L1828:
	ldrh	r3, [r4, #240]
	ldrh	r2, [r4, #310]
	adds	r3, r3, #24
	cmp	r2, r3, lsl #1
	bcs	.L1831
	movw	r2, #1469
	ldr	r1, .L1850+4
	ldr	r0, .L1850+8
	bl	sftl_printk
.L1831:
	movs	r2, #48
	ldr	r1, [r4, #3464]
	ldr	r0, .L1850+16
	bl	ftl_memcpy
	ldrh	r2, [r4, #240]
	ldr	r1, [r4, #3464]
	ldr	r0, [r4, #72]
	lsls	r2, r2, #1
	adds	r1, r1, #48
	bl	ftl_memcpy
	ldr	r2, [r4, #2468]
	ldr	r3, .L1850+20
	cmp	r2, r3
	bne	.L1832
	ldrb	r2, [r4, #2478]	@ zero_extendqisi2
	ldrh	r3, [r4, #254]
	ldrh	r5, [r4, #2476]
	cmp	r2, r3
	strh	r5, [r4, #2634]	@ movhi
	bne	.L1832
	ldrh	r3, [r4, #302]
	ldrh	r2, [r4, #258]
	ldr	r6, [r4, #244]
	ldrh	r0, [r4, #358]
	muls	r3, r5, r3
	str	r5, [r4, #3676]
	ldrh	r1, [r4, #232]
	subs	r0, r6, r0
	subs	r0, r0, r5
	str	r3, [r4, #2556]
	muls	r3, r2, r3
	str	r3, [r4, #340]
	bl	__aeabi_uidiv
	cmp	r5, r6
	strh	r0, [r4, #2624]	@ movhi
	bls	.L1833
	movw	r2, #1491
	ldr	r1, .L1850+4
	ldr	r0, .L1850+8
	bl	sftl_printk
.L1833:
	ldrh	r3, [r4, #2484]
	ldrh	r1, [r4, #2482]
	lsrs	r2, r3, #6
	and	r3, r3, #63
	strb	r3, [r4, #30]
	strh	r2, [r4, #26]	@ movhi
	ldrh	r2, [r4, #2486]
	ldrb	r3, [r4, #2479]	@ zero_extendqisi2
	strh	r1, [r4, #24]	@ movhi
	strh	r2, [r4, #76]	@ movhi
	ldrh	r2, [r4, #2488]
	strb	r3, [r4, #32]
	movw	r3, #65535
	strh	r3, [r4, #172]	@ movhi
	movs	r3, #0
	strh	r3, [r4, #174]	@ movhi
	lsrs	r0, r2, #6
	and	r2, r2, #63
	strb	r2, [r4, #82]
	ldrb	r2, [r4, #2480]	@ zero_extendqisi2
	strh	r0, [r4, #78]	@ movhi
	strb	r3, [r4, #178]
	strb	r2, [r4, #84]
	ldrh	r2, [r4, #2490]
	strb	r3, [r4, #180]
	str	r3, [r4, #2580]
	strh	r2, [r4, #124]	@ movhi
	ldrh	r2, [r4, #2492]
	str	r3, [r4, #2568]
	str	r3, [r4, #2560]
	str	r3, [r4, #2576]
	lsrs	r0, r2, #6
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
	it	hi
	strhi	r3, [r4, #2592]
	ldr	r3, [r4, #2504]
	cmp	r3, r2
	it	hi
	strhi	r3, [r4, #2596]
	movw	r3, #65535
	cmp	r1, r3
	beq	.L1836
	ldr	r0, .L1850+24
	bl	make_superblock
.L1836:
	ldrh	r2, [r4, #76]
	movw	r3, #65535
	cmp	r2, r3
	beq	.L1837
	ldr	r0, .L1850+28
	bl	make_superblock
.L1837:
	ldrh	r2, [r4, #124]
	movw	r3, #65535
	cmp	r2, r3
	beq	.L1838
	ldr	r0, .L1850+32
	bl	make_superblock
.L1838:
	ldrh	r2, [r4, #172]
	movw	r3, #65535
	cmp	r2, r3
	beq	.L1839
	ldr	r0, .L1850+36
	bl	make_superblock
.L1839:
	movs	r0, #0
	b	.L1820
.L1851:
	.align	2
.L1850:
	.word	.LANCHOR0
	.word	.LANCHOR1+600
	.word	.LC8
	.word	.LC127
	.word	.LANCHOR0+2468
	.word	1179929683
	.word	.LANCHOR0+24
	.word	.LANCHOR0+76
	.word	.LANCHOR0+124
	.word	.LANCHOR0+172
	.fnend
	.size	FtlLoadSysInfo, .-FtlLoadSysInfo
	.align	1
	.global	FtlMapTblRecovery
	.syntax unified
	.thumb
	.thumb_func
	.fpu softvfp
	.type	FtlMapTblRecovery, %function
FtlMapTblRecovery:
	.fnstart
	@ args = 0, pretend = 0, frame = 24
	@ frame_needed = 0, uses_anonymous_args = 0
	push	{r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.save {r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.pad #36
	sub	sp, sp, #36
	ldr	r3, [r0, #24]
	mov	r5, r0
	movs	r1, #0
	ldrh	r10, [r0, #6]
	str	r3, [sp, #8]
	ldr	r3, [r0, #16]
	ldr	r4, .L1893
	ldr	r9, [r0, #12]
	lsl	r2, r10, #2
	str	r3, [sp, #16]
	ldrh	r3, [r0, #8]
	ldr	r0, [sp, #8]
	str	r3, [sp, #12]
	bl	ftl_memset
	ldr	r3, [r4, #3304]
	movs	r2, #1
	ldr	fp, [r4, #3336]
	str	r2, [r5, #36]
	str	r3, [r4, #3464]
	movw	r3, #65535
	str	fp, [r4, #3468]
	strh	r3, [r5]	@ movhi
	strh	r3, [r5, #2]	@ movhi
	movs	r3, #0
	mov	r8, r3
	str	r3, [r5, #32]
	str	r3, [r5, #28]
.L1853:
	ldr	r3, [sp, #12]
	sxth	r6, r8
	cmp	r6, r3
	bge	.L1872
	ldr	r3, [sp, #12]
	lsls	r7, r6, #1
	subs	r3, r3, #1
	cmp	r6, r3
	bne	.L1854
	movs	r1, #1
	ldrh	r0, [r9, r6, lsl #1]
	bl	FtlGetLastWrittenPage
	sxth	r3, r0
	adds	r0, r0, #1
	strh	r8, [r5]	@ movhi
	add	r7, r7, r9
	str	r3, [sp, #12]
	mov	r8, #0
	ldr	r3, [sp, #16]
	strh	r0, [r5, #2]	@ movhi
	ldr	r3, [r3, r6, lsl #2]
	str	r3, [r5, #28]
.L1855:
	ldr	r3, [sp, #12]
	sxth	r9, r8
	adds	r2, r3, #1
	cmp	r9, r2
	blt	.L1858
.L1872:
	mov	r0, r5
	bl	ftl_free_no_use_map_blk
	ldrh	r2, [r5, #2]
	ldrh	r3, [r4, #304]
	cmp	r2, r3
	bne	.L1860
	mov	r0, r5
	bl	ftl_map_blk_alloc_new_blk
.L1860:
	mov	r0, r5
	bl	ftl_map_blk_gc
	mov	r0, r5
	bl	ftl_map_blk_gc
	movs	r0, #0
	add	sp, sp, #36
	@ sp needed
	pop	{r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L1858:
	ldrh	r2, [r7]
	ldr	r0, .L1893+4
	orr	r2, r9, r2, lsl #10
	str	r2, [r4, #3460]
	movs	r2, #1
	mov	r1, r2
	bl	FlashReadPages
	ldr	r2, [r4, #3468]
	ldr	r2, [r2, #12]
	str	r2, [sp, #16]
	cbz	r2, .L1856
	ldr	r1, [r4, #3456]
	adds	r1, r1, #1
	beq	.L1856
	ldrh	r1, [r4, #310]
	ldr	r0, [r4, #3464]
	bl	js_hash
	ldr	r2, [sp, #16]
	cmp	r2, r0
	beq	.L1856
	str	r0, [sp, #4]
	mov	r3, r9
	str	r2, [sp]
	mov	r2, r6
	ldr	r1, .L1893+8
	ldr	r0, .L1893+12
	bl	sftl_printk
	mov	r3, #-1
	str	r3, [r4, #3456]
.L1856:
	ldr	r3, [r4, #3456]
	adds	r3, r3, #1
	beq	.L1857
	ldrh	r3, [fp, #8]
	cmp	r10, r3
	bls	.L1857
	ldrh	r1, [fp]
	ldrh	r2, [r5, #4]
	cmp	r1, r2
	ittt	eq
	ldreq	r2, [r4, #3460]
	ldreq	r1, [sp, #8]
	streq	r2, [r1, r3, lsl #2]
.L1857:
	add	r8, r8, #1
	b	.L1855
.L1854:
	ldr	r3, [r4, #3304]
	ldr	r0, .L1893+4
	str	r3, [r4, #3464]
	add	r3, r9, r7
	ldrh	r2, [r9, r6, lsl #1]
	str	r3, [sp, #20]
	ldrh	r3, [r4, #304]
	subs	r3, r3, #1
	orr	r3, r3, r2, lsl #10
	movs	r2, #1
	mov	r1, r2
	str	r3, [r4, #3460]
	bl	FlashReadPages
	ldr	r3, [r4, #3456]
	adds	r3, r3, #1
	beq	.L1874
	ldrh	r2, [fp]
	ldrh	r3, [r5, #4]
	cmp	r2, r3
	bne	.L1874
	ldrh	r2, [fp, #8]
	movw	r3, #64245
	cmp	r2, r3
	beq	.L1862
.L1874:
	movs	r7, #0
.L1863:
	ldrh	r2, [r4, #304]
	sxth	r3, r7
	cmp	r3, r2
	bge	.L1870
	ldr	r2, [sp, #20]
	ldr	r0, .L1893+4
	str	r3, [sp, #28]
	ldrh	r2, [r2]
	orr	r2, r3, r2, lsl #10
	str	r2, [r4, #3460]
	movs	r2, #1
	mov	r1, r2
	bl	FlashReadPages
	ldr	r2, [r4, #3468]
	ldr	r2, [r2, #12]
	str	r2, [sp, #24]
	cbz	r2, .L1867
	ldr	r1, [r4, #3456]
	adds	r1, r1, #1
	beq	.L1867
	ldrh	r1, [r4, #310]
	ldr	r0, [r4, #3464]
	bl	js_hash
	ldr	r2, [sp, #24]
	cmp	r2, r0
	beq	.L1867
	str	r0, [sp, #4]
	str	r2, [sp]
	mov	r2, r6
	ldr	r3, [sp, #28]
	ldr	r1, .L1893+8
	ldr	r0, .L1893+16
	bl	sftl_printk
	mov	r3, #-1
	str	r3, [r4, #3456]
.L1867:
	ldr	r3, [r4, #3456]
	adds	r3, r3, #1
	beq	.L1868
	ldrh	r3, [fp, #8]
	cmp	r10, r3
	bls	.L1868
	ldrh	r1, [fp]
	ldrh	r2, [r5, #4]
	cmp	r1, r2
	ittt	eq
	ldreq	r2, [r4, #3460]
	ldreq	r1, [sp, #8]
	streq	r2, [r1, r3, lsl #2]
.L1868:
	adds	r7, r7, #1
	b	.L1863
.L1862:
	movs	r1, #0
	movs	r0, #4
.L1864:
	ldrh	r2, [r4, #304]
	sxth	r3, r1
	subs	r2, r2, #1
	cmp	r3, r2
	blt	.L1866
.L1870:
	add	r8, r8, #1
	b	.L1853
.L1866:
	ldr	r6, [r4, #3304]
	ldr	r2, [r6, r3, lsl #3]
	uxth	r7, r2
	cmp	r10, r7
	bls	.L1865
	add	r3, r0, r3, lsl #3
	ldr	r3, [r6, r3]
	ldr	r6, [sp, #8]
	str	r3, [r6, r7, lsl #2]
.L1865:
	adds	r1, r1, #1
	b	.L1864
.L1894:
	.align	2
.L1893:
	.word	.LANCHOR0
	.word	.LANCHOR0+3456
	.word	.LANCHOR1+615
	.word	.LC128
	.word	.LC129
	.fnend
	.size	FtlMapTblRecovery, .-FtlMapTblRecovery
	.align	1
	.global	FtlLoadVonderInfo
	.syntax unified
	.thumb
	.thumb_func
	.fpu softvfp
	.type	FtlLoadVonderInfo, %function
FtlLoadVonderInfo:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	ldr	r0, .L1896
	push	{r3, lr}
	.save {r3, lr}
	ldrh	r3, [r0, #320]
	strh	r3, [r0, #3622]	@ movhi
	movw	r3, #61574
	strh	r3, [r0, #3616]	@ movhi
	ldrh	r3, [r0, #344]
	strh	r3, [r0, #3620]	@ movhi
	ldrh	r3, [r0, #322]
	strh	r3, [r0, #3618]	@ movhi
	ldr	r3, [r0, #348]
	addw	r0, r0, #3612
	str	r3, [r0, #12]
	ldr	r3, [r0, #-240]
	str	r3, [r0, #16]
	ldr	r3, [r0, #-244]
	str	r3, [r0, #20]
	ldr	r3, [r0, #-236]
	str	r3, [r0, #24]
	bl	FtlMapTblRecovery
	movs	r0, #0
	pop	{r3, pc}
.L1897:
	.align	2
.L1896:
	.word	.LANCHOR0
	.fnend
	.size	FtlLoadVonderInfo, .-FtlLoadVonderInfo
	.align	1
	.global	FtlLoadMapInfo
	.syntax unified
	.thumb
	.thumb_func
	.fpu softvfp
	.type	FtlLoadMapInfo, %function
FtlLoadMapInfo:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	push	{r3, lr}
	.save {r3, lr}
	bl	FtlL2PDataInit
	ldr	r0, .L1899
	bl	FtlMapTblRecovery
	movs	r0, #0
	pop	{r3, pc}
.L1900:
	.align	2
.L1899:
	.word	.LANCHOR0+3396
	.fnend
	.size	FtlLoadMapInfo, .-FtlLoadMapInfo
	.align	1
	.global	FtlSysBlkInit
	.syntax unified
	.thumb
	.thumb_func
	.fpu softvfp
	.type	FtlSysBlkInit, %function
FtlSysBlkInit:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	push	{r4, r5, r6, lr}
	.save {r4, r5, r6, lr}
	movs	r3, #0
	ldr	r4, .L1917
	ldrh	r0, [r4, #236]
	strh	r3, [r4, #3476]	@ movhi
	bl	FtlFreeSysBlkQueueInit
	bl	FtlScanSysBlk
	ldrh	r2, [r4, #2628]
	movw	r3, #65535
	cmp	r2, r3
	bne	.L1902
.L1904:
	mov	r5, #-1
.L1901:
	mov	r0, r5
	pop	{r4, r5, r6, pc}
.L1902:
	bl	FtlLoadSysInfo
	mov	r5, r0
	cmp	r0, #0
	bne	.L1904
	bl	FtlLoadMapInfo
	bl	FtlLoadVonderInfo
	bl	Ftl_load_ext_data
	bl	FtlLoadEctTbl
	bl	FtlFreeSysBLkSort
	bl	SupperBlkListInit
	bl	FtlPowerLostRecovery
	movs	r0, #1
	bl	FtlUpdateVaildLpn
	ldrh	r1, [r4, #338]
	mov	r3, r5
	ldr	r2, [r4, #2540]
	movs	r0, #12
.L1905:
	cmp	r3, r1
	bge	.L1910
	mla	r6, r0, r3, r2
	ldr	r6, [r6, #4]
	cmp	r6, #0
	bge	.L1906
.L1910:
	ldrh	r2, [r4, #2496]
	cmp	r3, r1
	add	r2, r2, #1
	strh	r2, [r4, #2496]	@ movhi
	bge	.L1916
.L1907:
	ldr	r0, .L1917+4
	bl	FtlSuperblockPowerLostFix
	ldr	r0, .L1917+8
	bl	FtlSuperblockPowerLostFix
	ldrh	r1, [r4, #24]
	ldr	r2, [r4, #72]
	ldrh	r0, [r4, #28]
	ldrh	r3, [r2, r1, lsl #1]
	subs	r3, r3, r0
	strh	r3, [r2, r1, lsl #1]	@ movhi
	ldrh	r3, [r4, #302]
	ldr	r1, [r4, #72]
	ldrh	r0, [r4, #76]
	strh	r3, [r4, #26]	@ movhi
	movs	r3, #0
	strb	r3, [r4, #30]
	strh	r3, [r4, #28]	@ movhi
	ldrh	r6, [r4, #80]
	ldrh	r2, [r1, r0, lsl #1]
	subs	r2, r2, r6
	strh	r2, [r1, r0, lsl #1]	@ movhi
	strb	r3, [r4, #82]
	strh	r3, [r4, #80]	@ movhi
	ldrh	r3, [r4, #2498]
	ldrh	r2, [r4, #302]
	adds	r3, r3, #1
	strh	r2, [r4, #78]	@ movhi
	strh	r3, [r4, #2498]	@ movhi
	bl	l2p_flush
	bl	FtlVpcTblFlush
	bl	FtlVpcTblFlush
	b	.L1911
.L1906:
	adds	r3, r3, #1
	b	.L1905
.L1916:
	ldrh	r3, [r4, #3476]
	cmp	r3, #0
	bne	.L1907
.L1911:
	ldrh	r0, [r4, #24]
	movw	r3, #65535
	cmp	r0, r3
	beq	.L1912
	ldrh	r3, [r4, #28]
	cbnz	r3, .L1912
	ldrh	r3, [r4, #80]
	cbnz	r3, .L1912
	bl	FtlGcRefreshBlock
	ldrh	r0, [r4, #76]
	bl	FtlGcRefreshBlock
	bl	FtlVpcTblFlush
	ldr	r0, .L1917+4
	bl	allocate_new_data_superblock
	ldr	r0, .L1917+8
	bl	allocate_new_data_superblock
.L1912:
	bl	FtlVpcCheckAndModify
	b	.L1901
.L1918:
	.align	2
.L1917:
	.word	.LANCHOR0
	.word	.LANCHOR0+24
	.word	.LANCHOR0+76
	.fnend
	.size	FtlSysBlkInit, .-FtlSysBlkInit
	.align	1
	.global	ftl_low_format
	.syntax unified
	.thumb
	.thumb_func
	.fpu softvfp
	.type	ftl_low_format, %function
ftl_low_format:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	push	{r3, r4, r5, r6, r7, r8, r9, lr}
	.save {r3, r4, r5, r6, r7, r8, r9, lr}
	movs	r3, #0
	ldr	r4, .L1943
	ldrh	r0, [r4, #236]
	str	r3, [r4, #2592]
	str	r3, [r4, #2596]
	str	r3, [r4, #2608]
	bl	FtlFreeSysBlkQueueInit
	bl	FtlLoadBbt
	cbz	r0, .L1920
	bl	FtlMakeBbt
.L1920:
	ldr	r0, .L1943+4
	movs	r2, #0
.L1921:
	ldrh	r1, [r4, #258]
	uxth	r3, r2
	adds	r2, r2, #1
	cmp	r3, r1, lsl #7
	blt	.L1922
	ldrh	r6, [r4, #240]
	movs	r5, #0
.L1923:
	ldrh	r3, [r4, #242]
	cmp	r3, r6
	bhi	.L1924
	ldrh	r1, [r4, #232]
	subs	r3, r5, #3
	cmp	r3, r1, lsl #1
	bge	.L1925
.L1929:
	movs	r5, #0
	mov	r6, r5
.L1926:
	ldrh	r3, [r4, #240]
	uxth	r0, r5
	adds	r5, r5, #1
	cmp	r3, r0
	bhi	.L1930
	ldrh	r3, [r4, #242]
	ldrh	r5, [r4, #232]
	ldr	r9, [r4, #244]
	str	r3, [r4, #3288]
	mov	r1, r5
	mov	r0, r9
	bl	__aeabi_uidiv
	ubfx	r7, r0, #5, #16
	mov	r8, r0
	add	r3, r7, #36
	str	r0, [r4, #2556]
	strh	r3, [r4, #2624]	@ movhi
	movs	r3, #24
	muls	r3, r5, r3
	cmp	r6, r3
	ble	.L1931
	mov	r1, r5
	sub	r0, r9, r6
	bl	__aeabi_uidiv
	str	r0, [r4, #2556]
	lsrs	r0, r0, #5
	adds	r0, r0, #24
	strh	r0, [r4, #2624]	@ movhi
.L1931:
	ldrh	r3, [r4, #294]
	cbz	r3, .L1933
	ldrh	r2, [r4, #2624]
	add	r2, r2, r3, lsr #1
	strh	r2, [r4, #2624]	@ movhi
	mul	r2, r5, r3
	cmp	r6, r2
	itttt	lt
	addlt	r3, r3, #32
	strlt	r8, [r4, #2556]
	addlt	r3, r3, r7
	strhlt	r3, [r4, #2624]	@ movhi
.L1933:
	ldrh	r2, [r4, #2624]
	ldr	r3, [r4, #2556]
	ldr	r6, .L1943+8
	subs	r3, r3, r2
	muls	r5, r3, r5
	ldrh	r3, [r4, #302]
	str	r5, [r4, #3676]
	muls	r5, r3, r5
	ldrh	r3, [r4, #258]
	str	r5, [r4, #2556]
	muls	r5, r3, r5
	str	r5, [r4, #340]
	bl	FtlBbmTblFlush
	ldrh	r2, [r4, #242]
	movs	r1, #0
	ldr	r0, [r4, #72]
	lsls	r2, r2, #1
	bl	ftl_memset
	movw	r2, #65535
	movs	r3, #0
	mov	r5, r2
	str	r3, [r4, #2548]
	strh	r3, [r4, #174]	@ movhi
	strb	r3, [r4, #178]
	strb	r3, [r4, #180]
	strh	r3, [r4, #26]	@ movhi
	strb	r3, [r4, #30]
	strh	r3, [r4, #24]	@ movhi
	movs	r3, #1
	strh	r2, [r4, #172]	@ movhi
	strb	r3, [r4, #32]
.L1935:
	mov	r0, r6
	bl	make_superblock
	ldrb	r3, [r4, #31]	@ zero_extendqisi2
	ldrh	r2, [r4, #24]
	cmp	r3, #0
	bne	.L1936
	ldr	r3, [r4, #72]
	strh	r5, [r3, r2, lsl #1]	@ movhi
	ldrh	r3, [r4, #24]
	adds	r3, r3, #1
	strh	r3, [r4, #24]	@ movhi
	b	.L1935
.L1922:
	ldr	r5, [r4, #3316]
	mvns	r1, r3
	orr	r1, r3, r1, lsl #16
	str	r1, [r5, r3, lsl #2]
	ldr	r1, [r4, #3320]
	str	r0, [r1, r3, lsl #2]
	b	.L1921
.L1924:
	mov	r0, r6
	movs	r1, #1
	bl	FtlLowFormatEraseBlock
	adds	r6, r6, #1
	add	r5, r5, r0
	uxth	r5, r5
	uxth	r6, r6
	b	.L1923
.L1925:
	mov	r0, r5
	bl	__aeabi_uidiv
	ldr	r3, [r4, #332]
	add	r0, r0, r3
	uxth	r0, r0
	bl	FtlSysBlkNumInit
	ldrh	r0, [r4, #236]
	bl	FtlFreeSysBlkQueueInit
	ldrh	r5, [r4, #240]
.L1927:
	ldrh	r3, [r4, #242]
	cmp	r3, r5
	bls	.L1929
	mov	r0, r5
	movs	r1, #1
	adds	r5, r5, #1
	bl	FtlLowFormatEraseBlock
	uxth	r5, r5
	b	.L1927
.L1930:
	movs	r1, #0
	bl	FtlLowFormatEraseBlock
	add	r6, r6, r0
	uxth	r6, r6
	b	.L1926
.L1936:
	ldr	r3, [r4, #2592]
	movw	r5, #65535
	ldrh	r1, [r4, #28]
	ldr	r6, .L1943+12
	str	r3, [r4, #36]
	adds	r3, r3, #1
	str	r3, [r4, #2592]
	ldr	r3, [r4, #72]
	strh	r1, [r3, r2, lsl #1]	@ movhi
	movs	r3, #0
	strh	r3, [r4, #78]	@ movhi
	strb	r3, [r4, #82]
	ldrh	r3, [r4, #24]
	adds	r3, r3, #1
	strh	r3, [r4, #76]	@ movhi
	movs	r3, #1
	strb	r3, [r4, #84]
.L1937:
	mov	r0, r6
	bl	make_superblock
	ldrb	r3, [r4, #83]	@ zero_extendqisi2
	ldrh	r2, [r4, #76]
	cbnz	r3, .L1938
	ldr	r3, [r4, #72]
	strh	r5, [r3, r2, lsl #1]	@ movhi
	ldrh	r3, [r4, #76]
	adds	r3, r3, #1
	strh	r3, [r4, #76]	@ movhi
	b	.L1937
.L1938:
	ldr	r3, [r4, #2592]
	movw	r5, #65535
	ldrh	r1, [r4, #80]
	str	r3, [r4, #88]
	adds	r3, r3, #1
	str	r3, [r4, #2592]
	ldr	r3, [r4, #72]
	strh	r1, [r3, r2, lsl #1]	@ movhi
	strh	r5, [r4, #124]	@ movhi
	bl	FtlFreeSysBlkQueueOut
	movs	r3, #0
	strh	r0, [r4, #2628]	@ movhi
	strh	r3, [r4, #2630]	@ movhi
	ldr	r3, [r4, #3676]
	strh	r5, [r4, #2632]	@ movhi
	strh	r3, [r4, #2634]	@ movhi
	ldr	r3, [r4, #2592]
	str	r3, [r4, #2636]
	adds	r3, r3, #1
	str	r3, [r4, #2592]
	bl	FtlVpcTblFlush
	bl	FtlSysBlkInit
	cbnz	r0, .L1939
	ldr	r3, .L1943+16
	movs	r2, #1
	str	r2, [r3]
.L1939:
	movs	r0, #0
	pop	{r3, r4, r5, r6, r7, r8, r9, pc}
.L1944:
	.align	2
.L1943:
	.word	.LANCHOR0
	.word	168778952
	.word	.LANCHOR0+24
	.word	.LANCHOR0+76
	.word	.LANCHOR2
	.fnend
	.size	ftl_low_format, .-ftl_low_format
	.align	1
	.global	sftl_init
	.syntax unified
	.thumb
	.thumb_func
	.fpu softvfp
	.type	sftl_init, %function
sftl_init:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	push	{r3, r4, r5, lr}
	.save {r3, r4, r5, lr}
	mov	r3, #-1
	ldr	r4, .L1951
	ldr	r5, .L1951+4
	ldr	r1, .L1951+8
	ldr	r0, .L1951+12
	str	r3, [r5]
	bl	sftl_printk
	mov	r0, r4
	bl	FtlConstantsInit
	bl	FtlMemInit
	bl	FtlVariablesInit
	ldrh	r0, [r4, #236]
	bl	FtlFreeSysBlkQueueInit
	bl	FtlLoadBbt
	cbnz	r0, .L1949
	bl	FtlSysBlkInit
	cbnz	r0, .L1949
	movs	r3, #1
	str	r3, [r5]
	ldrh	r3, [r4, #224]
	cmp	r3, #15
	bhi	.L1949
	movw	r4, #8129
.L1948:
	movs	r1, #1
	movs	r0, #0
	bl	rk_ftl_garbage_collect
	subs	r4, r4, #1
	bne	.L1948
.L1949:
	movs	r0, #0
	pop	{r3, r4, r5, pc}
.L1952:
	.align	2
.L1951:
	.word	.LANCHOR0
	.word	.LANCHOR2
	.word	.LC0
	.word	.LC77
	.fnend
	.size	sftl_init, .-sftl_init
	.align	1
	.global	FtlWriteToIDB
	.syntax unified
	.thumb
	.thumb_func
	.fpu softvfp
	.type	FtlWriteToIDB, %function
FtlWriteToIDB:
	.fnstart
	@ args = 0, pretend = 0, frame = 104
	@ frame_needed = 0, uses_anonymous_args = 0
	ldr	r3, .L2020
	push	{r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.save {r4, r5, r6, r7, r8, r9, r10, fp, lr}
	add	r8, r1, r0
	add	r9, r8, #-1
	.pad #116
	sub	sp, sp, #116
	cmp	r9, #63
	mov	r4, r0
	ldr	r3, [r3]
	mov	r6, r1
	mov	r7, r2
	ldr	r5, .L2020+4
	str	r3, [sp, #108]
	bls	.L1954
	cmp	r0, #576
	bcc	.L1955
.L1954:
	ldr	r3, [r5, #3680]
	cbnz	r3, .L1956
.L2019:
	movs	r4, #0
	b	.L1953
.L1956:
	ldr	r8, [r5, #3684]
	ldr	r3, .L2020+8
	ldr	r2, [r8]
	cmp	r2, r3
	bne	.L1958
	ldrh	r1, [r5, #10]
	add	r0, r8, #260096
	movs	r2, #0
	movw	r3, #65023
.L1962:
	ldr	r4, [r0, #-4]!
	cbnz	r4, .L1959
	ldr	r4, [r8, r2, lsl #2]
	adds	r2, r2, #1
	cmp	r2, #4096
	add	r3, r3, #-1
	it	hi
	movhi	r2, #0
	cmp	r3, #4096
	str	r4, [r0, #2048]
	bne	.L1962
	mov	fp, #512
	b	.L1961
.L1959:
	adds	r3, r3, #127
	lsr	fp, r3, #7
.L1961:
	lsls	r1, r1, #2
	add	r0, fp, #4
	ldr	r4, .L2020+4
	uxth	r1, r1
	bl	__aeabi_uidiv
	adds	r3, r0, #1
	mov	r1, fp
	mov	r2, r3
	ldr	r0, .L2020+12
	str	r3, [sp, #16]
	bl	sftl_printk
	lsl	r3, fp, #7
	str	r3, [sp, #32]
	movs	r3, #0
	str	r3, [sp, #20]
	str	r3, [sp, #12]
.L1963:
	ldr	r3, [sp, #16]
	ldr	r2, [sp, #12]
	add	r3, r3, r2
	cmp	r3, #8
	str	r3, [sp, #28]
	bls	.L1985
	ldr	r3, [sp, #20]
	cbnz	r3, .L1986
.L1958:
	mov	fp, #-1
.L1986:
	ldr	r5, .L2020+4
	movs	r3, #0
	mov	r4, fp
	ldr	r0, [r5, #3684]
	str	r3, [r5, #3680]
	bl	kfree
	ldr	r0, [r5, #3688]
	bl	kfree
.L1953:
	ldr	r3, .L2020
	mov	r0, r4
	ldr	r2, [sp, #108]
	ldr	r3, [r3]
	cmp	r2, r3
	beq	.L1993
	bl	__stack_chk_fail
.L1985:
	mov	r2, #512
	movs	r1, #0
	ldr	r0, [r4, #3688]
	bl	memset
	ldrh	r6, [r4, #10]
	ldr	r3, [sp, #12]
	muls	r3, r6, r3
	str	r3, [sp, #24]
	ldr	r3, [r4, #3264]
	cmp	r3, #0
	beq	.L1994
	ldr	r3, [r4, #3268]
	cmp	r3, #0
	ite	eq
	moveq	r9, #6
	movne	r9, #9
.L1964:
	ldr	r7, [sp, #24]
	movs	r5, #0
.L1965:
	ldr	r3, [r4, #3252]
	mov	r1, r7
	movs	r0, #0
	adds	r5, r5, #1
	blx	r3
	ldr	r3, [sp, #16]
	add	r7, r7, r6
	cmp	r3, r5
	bhi	.L1965
	cmp	r9, #9
	bne	.L1996
	ldr	r5, [r4, #3688]
	mov	r2, #1024
	movs	r1, #0
	mov	r0, r5
	bl	ftl_memset
	ldr	r3, .L2020+16
	movs	r1, #12
	movs	r2, #4
	str	r1, [r5, #4]
	strb	r2, [r5, #17]
	adds	r0, r5, r1
	str	r3, [r5]
	movs	r3, #0
	ldrh	r2, [r4, #10]
	str	r3, [r5, #12]
	strb	r3, [r5, #16]
	strh	r2, [r5, #18]	@ movhi
	movs	r2, #16
	strb	r3, [r5, #20]
	strb	r2, [r5, #21]
	strh	r3, [r5, #22]	@ movhi
	bl	js_hash
	str	r0, [r5, #8]
.L1966:
	ldr	r3, [sp, #16]
	mov	r10, r8
	muls	r3, r6, r3
	movs	r6, #0
	str	r3, [sp, #36]
.L1967:
	ldr	r3, [sp, #36]
	cmp	r6, r3
	beq	.L1974
	cmp	r9, #9
	ite	ne
	lslne	r3, r6, #2
	addeq	r3, r6, #1
	str	r3, [sp, #44]
	movw	r3, #61424
	str	r3, [sp, #48]
	cmp	r6, #0
	bne	.L1970
	cmp	r9, #9
	bne	.L1970
	ldr	r3, [r4, #3264]
	movs	r0, #70
	blx	r3
	ldr	r7, [r4, #3256]
	add	r3, sp, #44
	mov	r2, r5
	ldr	r1, [sp, #24]
	mov	r0, r6
	blx	r7
	ldr	r3, [r4, #3264]
	mov	r7, r0
	ldrb	r0, [r4, #22]	@ zero_extendqisi2
	blx	r3
	adds	r7, r7, #1
	bne	.L1971
.L1974:
	ldrb	r3, [r4, #14]	@ zero_extendqisi2
	ldr	r2, [sp, #12]
	ldr	r7, [r4, #3688]
	str	r3, [sp, #24]
	ldrh	r3, [r4, #10]
	mul	r9, r3, r2
	ldr	r2, [r4, #3264]
	cmp	r2, #0
	bne	.L1972
	mov	r10, #6
.L1973:
	ldr	r2, [sp, #16]
	movs	r5, #0
	muls	r3, r2, r3
	str	r3, [sp, #36]
.L1976:
	ldr	r3, [sp, #36]
	cmp	r5, r3
	beq	.L1980
	cmp	r5, #0
	bne	.L1977
	cmp	r10, #9
	bne	.L1977
	ldr	r3, [r4, #3264]
	movs	r0, #70
	blx	r3
	ldr	r3, [r4, #3268]
	movs	r0, #2
	blx	r3
	mov	r2, r7
	mov	r1, r9
	add	r3, sp, #44
	mov	r0, r5
	ldr	r6, [r4, #3260]
	blx	r6
	ldr	r3, [r4, #3268]
	ldr	r0, [sp, #24]
	blx	r3
	ldr	r3, [r4, #3264]
	ldrb	r0, [r4, #22]	@ zero_extendqisi2
	blx	r3
	ldr	r3, [r7]
	ldr	r2, .L2020+16
	cmp	r3, r2
	beq	.L1978
.L1980:
	ldr	r0, [r4, #3688]
	mov	r1, r8
	movs	r5, #0
	mov	ip, r0
.L1979:
	mov	r6, ip
	mov	r7, r1
	ldr	r2, [r6]
	add	ip, ip, #4
	ldr	r3, [r7]
	adds	r1, r1, #4
	cmp	r2, r3
	beq	.L1982
	mov	r2, #512
	movs	r1, #0
	bl	memset
	str	r5, [sp]
	ldr	r3, [r7]
	ldr	r2, [r6]
	ldr	r1, [sp, #12]
	ldr	r0, .L2020+20
	bl	sftl_printk
	ldrh	r1, [r4, #10]
	movs	r0, #0
	ldr	r2, [sp, #12]
	ldr	r3, [r4, #3252]
	muls	r2, r1, r2
	mov	r1, r2
	blx	r3
.L1983:
	ldr	r3, [sp, #28]
	str	r3, [sp, #12]
	b	.L1963
.L1994:
	mov	r9, #6
	b	.L1964
.L1996:
	movs	r5, #0
	b	.L1966
.L1970:
	ldr	r1, [sp, #24]
	add	r3, sp, #44
	ldr	r7, [r4, #3256]
	mov	r2, r10
	movs	r0, #0
	add	r1, r1, r6
	blx	r7
	adds	r0, r0, #1
	beq	.L1974
	add	r10, r10, #2048
.L1971:
	adds	r6, r6, #1
	b	.L1967
.L1972:
	ldr	r2, [r4, #3268]
	cmp	r2, #0
	ite	eq
	moveq	r10, #6
	movne	r10, #9
	b	.L1973
.L1977:
	add	r3, sp, #44
	mov	r2, r7
	add	r1, r9, r5
	movs	r0, #0
	ldr	r6, [r4, #3260]
	blx	r6
	adds	r0, r0, #1
	beq	.L1980
	ldr	r2, [sp, #48]
	movw	r3, #61424
	cmp	r2, r3
	bne	.L1980
	add	r7, r7, #2048
.L1978:
	adds	r5, r5, #1
	b	.L1976
.L1982:
	ldr	r3, [sp, #32]
	adds	r5, r5, #1
	cmp	r5, r3
	bne	.L1979
	ldr	r3, [sp, #20]
	adds	r3, r3, #1
	cmp	r3, #5
	str	r3, [sp, #20]
	bls	.L1983
	b	.L1986
.L2021:
	.align	2
.L2020:
	.word	__stack_chk_guard
	.word	.LANCHOR0
	.word	-52655045
	.word	.LC130
	.word	1179535694
	.word	.LC131
.L1955:
	cmp	r0, #64
	bne	.L1987
	mov	r0, #262144
	bl	ftl_malloc
	str	r0, [r5, #3684]
	mov	r0, #262144
	bl	ftl_malloc
	ldr	r3, [r5, #3684]
	str	r0, [r5, #3688]
	cbz	r3, .L1988
	cbz	r0, .L1988
	movs	r2, #1
	movs	r1, #0
	str	r2, [r5, #3680]
	mov	r0, r3
	mov	r2, #262144
	bl	ftl_memset
.L1987:
	ldr	r3, [r5, #3680]
	cmp	r3, #0
	beq	.L2019
	cmp	r4, #63
	ldr	r0, [r5, #3684]
	iteet	hi
	ldrhi	r3, .L2022
	rsbls	r1, r4, #64
	subls	r6, r6, r1
	movhi	r1, r7
	itee	ls
	addls	r1, r7, r1, lsl #9
	addhi	r3, r3, r4
	addhi	r0, r0, r3, lsl #9
	cmp	r9, #576
	itt	cs
	rsbcs	r8, r8, #576
	addcs	r6, r6, r8
	lsls	r2, r6, #9
	bl	ftl_memcpy
	b	.L2019
.L1988:
	ldr	r1, .L2022+4
	ldr	r0, .L2022+8
	bl	sftl_printk
	b	.L1987
.L1993:
	add	sp, sp, #116
	@ sp needed
	pop	{r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L2023:
	.align	2
.L2022:
	.word	8388544
	.word	.LANCHOR1+633
	.word	.LC132
	.fnend
	.size	FtlWriteToIDB, .-FtlWriteToIDB
	.align	1
	.global	sftl_write
	.syntax unified
	.thumb
	.thumb_func
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
	movs	r0, #0
	pop	{r4, r5, r6, lr}
	b	FtlWrite
	.fnend
	.size	sftl_write, .-sftl_write
	.align	1
	.global	rk_sftl_vendor_dev_ops_register
	.syntax unified
	.thumb
	.thumb_func
	.fpu softvfp
	.type	rk_sftl_vendor_dev_ops_register, %function
rk_sftl_vendor_dev_ops_register:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	@ link register save eliminated.
	ldr	r2, .L2028
	ldr	r3, [r2, #3692]
	cbnz	r3, .L2027
	str	r0, [r2, #3692]
	mov	r0, r3
	str	r1, [r2, #3696]
	bx	lr
.L2027:
	mov	r0, #-1
	bx	lr
.L2029:
	.align	2
.L2028:
	.word	.LANCHOR0
	.fnend
	.size	rk_sftl_vendor_dev_ops_register, .-rk_sftl_vendor_dev_ops_register
	.align	1
	.global	rk_sftl_vendor_storage_init
	.syntax unified
	.thumb
	.thumb_func
	.fpu softvfp
	.type	rk_sftl_vendor_storage_init, %function
rk_sftl_vendor_storage_init:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	push	{r4, r5, r6, r7, r8, r9, r10, lr}
	.save {r4, r5, r6, r7, r8, r9, r10, lr}
	mov	r0, #65536
	ldr	r5, .L2038
	bl	ftl_malloc
	str	r0, [r5, #3700]
	cmp	r0, #0
	beq	.L2036
	ldr	r10, .L2038+4
	mov	r9, #0
	mov	r7, r9
	mov	r8, r9
.L2034:
	ldr	r3, [r5, #3692]
	movs	r1, #128
	ldr	r2, [r5, #3700]
	lsl	r0, r8, #7
	blx	r3
	mov	r6, r0
	cbnz	r0, .L2032
	ldr	r4, [r5, #3700]
	ldr	r3, [r4]
	cmp	r3, r10
	bne	.L2033
	movw	r3, #65532
	ldr	r2, [r4, #4]
	ldr	r3, [r4, r3]
	cmp	r3, r2
	bne	.L2033
	cmp	r3, r7
	itt	hi
	movhi	r9, r8
	movhi	r7, r3
.L2033:
	add	r8, r8, #1
	cmp	r8, #2
	bne	.L2034
	cbz	r7, .L2035
	ldr	r3, [r5, #3692]
	mov	r2, r4
	movs	r1, #128
	lsl	r0, r9, #7
	blx	r3
	mov	r6, r0
	cbz	r0, .L2030
.L2032:
	ldr	r0, [r5, #3700]
	mov	r6, #-1
	bl	kfree
	movs	r3, #0
	str	r3, [r5, #3700]
	b	.L2030
.L2035:
	mov	r2, #65536
	mov	r1, r7
	mov	r0, r4
	bl	memset
	movs	r3, #1
	movw	r2, #65532
	str	r3, [r4, #4]
	str	r10, [r4]
	str	r3, [r4, r2]
	movw	r3, #64504
	strh	r7, [r4, #12]	@ movhi
	strh	r3, [r4, #14]	@ movhi
.L2030:
	mov	r0, r6
	pop	{r4, r5, r6, r7, r8, r9, r10, pc}
.L2036:
	mvn	r6, #11
	b	.L2030
.L2039:
	.align	2
.L2038:
	.word	.LANCHOR0
	.word	1380668996
	.fnend
	.size	rk_sftl_vendor_storage_init, .-rk_sftl_vendor_storage_init
	.align	1
	.global	rk_sftl_vendor_read
	.syntax unified
	.thumb
	.thumb_func
	.fpu softvfp
	.type	rk_sftl_vendor_read, %function
rk_sftl_vendor_read:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	push	{r3, r4, r5, r6, r7, lr}
	.save {r3, r4, r5, r6, r7, lr}
	mov	r7, r0
	ldr	r3, .L2046
	mov	r0, r1
	ldr	r5, [r3, #3700]
	cbz	r5, .L2045
	ldrh	r6, [r5, #10]
	movs	r3, #0
.L2042:
	cmp	r3, r6
	bcc	.L2044
.L2045:
	mov	r0, #-1
	pop	{r3, r4, r5, r6, r7, pc}
.L2044:
	add	r1, r5, r3, lsl #3
	ldrh	r4, [r1, #16]
	cmp	r4, r7
	bne	.L2043
	ldrh	r4, [r1, #20]
	ldrh	r1, [r1, #18]
	cmp	r4, r2
	it	cs
	movcs	r4, r2
	add	r1, r1, #1024
	mov	r2, r4
	add	r1, r1, r5
	bl	memcpy
	mov	r0, r4
	pop	{r3, r4, r5, r6, r7, pc}
.L2043:
	adds	r3, r3, #1
	b	.L2042
.L2047:
	.align	2
.L2046:
	.word	.LANCHOR0
	.fnend
	.size	rk_sftl_vendor_read, .-rk_sftl_vendor_read
	.align	1
	.global	rk_sftl_vendor_write
	.syntax unified
	.thumb
	.thumb_func
	.fpu softvfp
	.type	rk_sftl_vendor_write, %function
rk_sftl_vendor_write:
	.fnstart
	@ args = 0, pretend = 0, frame = 24
	@ frame_needed = 0, uses_anonymous_args = 0
	push	{r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.save {r4, r5, r6, r7, r8, r9, r10, fp, lr}
	mov	r8, r2
	ldr	r2, .L2067
	.pad #28
	sub	sp, sp, #28
	mov	r3, r1
	ldr	r4, [r2, #3700]
	mov	r9, r2
	cmp	r4, #0
	beq	.L2063
	ldrh	r2, [r4, #10]
	add	r6, r8, #63
	ldrh	r1, [r4, #8]
	bic	r6, r6, #63
	movs	r7, #0
	str	r1, [sp, #4]
.L2050:
	cmp	r7, r2
	bcc	.L2058
	ldrh	r1, [r4, #14]
	cmp	r6, r1
	bhi	.L2063
	add	r2, r4, r2, lsl #3
	uxth	r6, r6
	strh	r0, [r2, #16]	@ movhi
	ldrh	r0, [r4, #12]
	strh	r8, [r2, #20]	@ movhi
	strh	r0, [r2, #18]	@ movhi
	add	r0, r0, r6
	subs	r6, r1, r6
	strh	r0, [r4, #12]	@ movhi
	strh	r6, [r4, #14]	@ movhi
	mov	r1, r3
	ldrh	r0, [r2, #18]
	mov	r2, r8
	add	r0, r0, #1024
	add	r0, r0, r4
	bl	memcpy
	ldrh	r3, [r4, #10]
	movw	r2, #65532
	adds	r3, r3, #1
	strh	r3, [r4, #10]	@ movhi
	ldr	r3, [r4, #4]
	adds	r3, r3, #1
	str	r3, [r4, #4]
	str	r3, [r4, r2]
	ldrh	r3, [r4, #8]
	adds	r3, r3, #1
	uxth	r3, r3
	cmp	r3, #1
	it	hi
	movhi	r3, #0
	strh	r3, [r4, #8]	@ movhi
	ldr	r3, [r9, #3696]
	b	.L2066
.L2058:
	add	r5, r4, r7, lsl #3
	ldrh	r1, [r5, #16]
	cmp	r1, r0
	str	r1, [sp, #8]
	bne	.L2051
	ldrh	r1, [r5, #20]
	add	fp, r4, #1024
	adds	r1, r1, #63
	bic	r1, r1, #63
	cmp	r8, r1
	str	r1, [sp, #12]
	bls	.L2052
	ldrh	r1, [r4, #14]
	cmp	r6, r1
	bhi	.L2063
	ldrh	r10, [r5, #18]
	subs	r2, r2, #1
	str	r2, [sp, #16]
.L2053:
	ldr	r2, [sp, #16]
	adds	r5, r5, #8
	cmp	r7, r2
	bcc	.L2054
	ldrh	r2, [sp, #8]
	add	r7, r4, r7, lsl #3
	uxth	r5, r10
	mov	r1, r3
	strh	r8, [r7, #20]	@ movhi
	uxtah	r0, fp, r10
	strh	r2, [r7, #16]	@ movhi
	mov	r2, r8
	strh	r5, [r7, #18]	@ movhi
	bl	memcpy
	uxth	r3, r6
	ldrh	r6, [r4, #14]
	add	r5, r5, r3
	subs	r6, r6, r3
	ldr	r3, [sp, #12]
	strh	r5, [r4, #12]	@ movhi
	add	r6, r6, r3
	strh	r6, [r4, #14]	@ movhi
.L2055:
	ldr	r3, [r4, #4]
	movw	r2, #65532
	adds	r3, r3, #1
	str	r3, [r4, #4]
	str	r3, [r4, r2]
	ldrh	r3, [r4, #8]
	adds	r3, r3, #1
	uxth	r3, r3
	cmp	r3, #1
	it	hi
	movhi	r3, #0
	strh	r3, [r4, #8]	@ movhi
	ldr	r3, .L2067
	ldr	r3, [r3, #3696]
.L2066:
	ldr	r0, [sp, #4]
	mov	r2, r4
	movs	r1, #128
	lsls	r0, r0, #7
	blx	r3
	movs	r0, #0
.L2048:
	add	sp, sp, #28
	@ sp needed
	pop	{r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L2054:
	ldrh	r9, [r5, #20]
	add	r0, fp, r10
	ldrh	r2, [r5, #16]
	adds	r7, r7, #1
	ldrh	r1, [r5, #18]
	strh	r9, [r5, #12]	@ movhi
	add	r9, r9, #63
	bic	r9, r9, #63
	strh	r2, [r5, #8]	@ movhi
	strh	r10, [r5, #10]	@ movhi
	mov	r2, r9
	add	r1, r1, fp
	str	r3, [sp, #20]
	add	r10, r10, r9
	bl	memcpy
	ldr	r3, [sp, #20]
	b	.L2053
.L2052:
	ldrh	r0, [r5, #18]
	mov	r2, r8
	mov	r1, r3
	add	r0, r0, fp
	bl	memcpy
	strh	r8, [r5, #20]	@ movhi
	b	.L2055
.L2051:
	adds	r7, r7, #1
	b	.L2050
.L2063:
	mov	r0, #-1
	b	.L2048
.L2068:
	.align	2
.L2067:
	.word	.LANCHOR0
	.fnend
	.size	rk_sftl_vendor_write, .-rk_sftl_vendor_write
	.align	1
	.global	rk_sftl_vendor_storage_ioctl
	.syntax unified
	.thumb
	.thumb_func
	.fpu softvfp
	.type	rk_sftl_vendor_storage_ioctl, %function
rk_sftl_vendor_storage_ioctl:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	push	{r3, r4, r5, r6, r7, lr}
	.save {r3, r4, r5, r6, r7, lr}
	mov	r0, #4096
	mov	r6, r1
	mov	r5, r2
	bl	ftl_malloc
	mov	r4, r0
	cmp	r0, #0
	beq	.L2085
	ldr	r3, .L2093
	cmp	r6, r3
	beq	.L2072
	adds	r3, r3, #1
	cmp	r6, r3
	beq	.L2073
.L2091:
	mvn	r5, #13
	b	.L2071
.L2072:
	ldr	r6, .L2093+4
	mov	r3, sp
	ands	r3, r3, r6
	ldr	r3, [r3, #8]
	.syntax unified
@ 114 "./include/linux/uaccess.h" 1
	adds r2, r5, #8; sbcccs r2, r2, r3; movcc r3, #0
@ 0 "" 2
	.thumb
	.syntax unified
	movs	r2, #8
	cbnz	r3, .L2074
	mov	r1, r5
	bl	arm_copy_from_user
	mov	r2, r0
	cmp	r0, #0
	beq	.L2075
.L2074:
	rsb	r0, r2, #8
	movs	r1, #0
	add	r0, r0, r4
	bl	memset
	mov	r2, r5
	mov	r1, #256
.L2092:
	ldr	r0, .L2093+8
	bl	sftl_printk
	b	.L2091
.L2076:
	mov	r3, sp
	uxth	r2, r0
	ands	r6, r6, r3
	strh	r0, [r4, #6]	@ movhi
	adds	r2, r2, #8
	ldr	r3, [r6, #8]
	.syntax unified
@ 132 "./include/linux/uaccess.h" 1
	adds r1, r5, r2; sbcccs r1, r1, r3; movcc r3, #0
@ 0 "" 2
	.thumb
	.syntax unified
	cmp	r3, #0
	bne	.L2091
	mov	r1, r4
	mov	r0, r5
	bl	arm_copy_to_user
	cmp	r0, #0
	bne	.L2091
.L2088:
	mov	r5, r0
	b	.L2071
.L2073:
	ldr	r7, .L2093+4
	mov	r3, sp
	ands	r3, r3, r7
	ldr	r3, [r3, #8]
	.syntax unified
@ 114 "./include/linux/uaccess.h" 1
	adds r2, r5, #8; sbcccs r2, r2, r3; movcc r3, #0
@ 0 "" 2
	.thumb
	.syntax unified
	movs	r2, #8
	cbnz	r3, .L2078
	mov	r1, r5
	bl	arm_copy_from_user
	mov	r2, r0
	cbz	r0, .L2079
.L2078:
	rsb	r0, r2, #8
	movs	r1, #0
	add	r0, r0, r4
	bl	memset
	mov	r2, r5
	mov	r1, #276
	b	.L2092
.L2085:
	mov	r5, #-1
	b	.L2069
.L2075:
	ldr	r2, [r4]
	ldr	r3, .L2093+12
	cmp	r2, r3
	bne	.L2080
	ldrh	r2, [r4, #6]
	add	r1, r4, #8
	ldrh	r0, [r4, #4]
	bl	rk_sftl_vendor_read
	adds	r3, r0, #1
	bne	.L2076
.L2080:
	mov	r5, #-1
.L2071:
	mov	r0, r4
	bl	kfree
.L2069:
	mov	r0, r5
	pop	{r3, r4, r5, r6, r7, pc}
.L2079:
	ldr	r2, [r4]
	ldr	r3, .L2093+12
	cmp	r2, r3
	bne	.L2080
	ldrh	r6, [r4, #6]
	movw	r3, #4087
	cmp	r6, r3
	bhi	.L2080
	mov	r3, sp
	adds	r6, r6, #8
	ands	r7, r7, r3
	ldr	r3, [r7, #8]
	.syntax unified
@ 114 "./include/linux/uaccess.h" 1
	adds r2, r5, r6; sbcccs r2, r2, r3; movcc r3, #0
@ 0 "" 2
	.thumb
	.syntax unified
	mov	r2, r6
	cbnz	r3, .L2081
	mov	r1, r5
	mov	r0, r4
	bl	arm_copy_from_user
	mov	r2, r0
	cbz	r0, .L2082
.L2081:
	subs	r6, r6, r2
	movs	r1, #0
	adds	r0, r4, r6
	bl	memset
	mov	r2, r5
	movw	r1, #283
	b	.L2092
.L2082:
	ldrh	r2, [r4, #6]
	add	r1, r4, #8
	ldrh	r0, [r4, #4]
	bl	rk_sftl_vendor_write
	b	.L2088
.L2094:
	.align	2
.L2093:
	.word	1074034177
	.word	-8192
	.word	.LC133
	.word	1448232273
	.fnend
	.size	rk_sftl_vendor_storage_ioctl, .-rk_sftl_vendor_storage_ioctl
	.align	1
	.global	rk_sftl_vendor_register
	.syntax unified
	.thumb
	.thumb_func
	.fpu softvfp
	.type	rk_sftl_vendor_register, %function
rk_sftl_vendor_register:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	@ link register save eliminated.
	ldr	r0, .L2096
	b	misc_register
.L2097:
	.align	2
.L2096:
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
