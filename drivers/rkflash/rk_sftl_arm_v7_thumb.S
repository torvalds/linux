/* SPDX-License-Identifier: GPL-2.0 */

/* Copyright (c) 2018 Rockchip Electronics Co. Ltd.
 * date: 2019-02-20
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
	.eabi_attribute 18, 4
	.file	"rk_sftl.c"
	.global	__aeabi_uidiv
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
	.syntax unified
	.thumb
	.thumb_func
	.fpu softvfp
	.type	ftl_set_blk_mode.part.6, %function
ftl_set_blk_mode.part.6:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	@ link register save eliminated.
	ldr	r3, .L6
	lsrs	r1, r0, #5
	and	r0, r0, #31
	ldr	r2, [r3, #24]
	movs	r3, #1
	lsl	r0, r3, r0
	ldr	r3, [r2, r1, lsl #2]
	orrs	r3, r3, r0
	str	r3, [r2, r1, lsl #2]
	bx	lr
.L7:
	.align	2
.L6:
	.word	.LANCHOR0
	.fnend
	.size	ftl_set_blk_mode.part.6, .-ftl_set_blk_mode.part.6
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
	ldr	r4, .L9
	ldr	r1, .L9+4
	ldr	r0, .L9+8
	bl	sftl_printk
	ldrh	r1, [r4, #28]
	ldr	r0, [r4, #76]
	ldrb	r3, [r4, #34]	@ zero_extendqisi2
	ldrh	r2, [r4, #30]
	ldrh	r0, [r0, r1, lsl #1]
	str	r0, [sp, #8]
	ldrh	r0, [r4, #32]
	str	r0, [sp, #4]
	ldrb	r0, [r4, #36]	@ zero_extendqisi2
	str	r0, [sp]
	ldr	r0, .L9+12
	bl	sftl_printk
	ldrh	r1, [r4, #80]
	ldr	r0, [r4, #76]
	ldrb	r3, [r4, #86]	@ zero_extendqisi2
	ldrh	r2, [r4, #82]
	ldrh	r0, [r0, r1, lsl #1]
	str	r0, [sp, #8]
	ldrh	r0, [r4, #84]
	str	r0, [sp, #4]
	ldrb	r0, [r4, #88]	@ zero_extendqisi2
	str	r0, [sp]
	ldr	r0, .L9+16
	bl	sftl_printk
	ldrh	r1, [r4, #128]
	ldr	r0, [r4, #76]
	ldrb	r3, [r4, #134]	@ zero_extendqisi2
	ldrh	r2, [r4, #130]
	ldrh	r0, [r0, r1, lsl #1]
	str	r0, [sp, #8]
	ldrh	r0, [r4, #132]
	str	r0, [sp, #4]
	ldrb	r0, [r4, #136]	@ zero_extendqisi2
	str	r0, [sp]
	ldr	r0, .L9+20
	bl	sftl_printk
	ldrh	r1, [r4, #176]
	ldr	r0, [r4, #76]
	ldrb	r3, [r4, #182]	@ zero_extendqisi2
	ldrh	r2, [r4, #178]
	ldrh	r0, [r0, r1, lsl #1]
	str	r0, [sp, #8]
	ldrh	r0, [r4, #180]
	str	r0, [sp, #4]
	ldrb	r0, [r4, #184]	@ zero_extendqisi2
	str	r0, [sp]
	ldr	r0, .L9+24
	bl	sftl_printk
	ldrh	r3, [r4, #224]
	ldrh	r2, [r4, #226]
	ldrh	r1, [r4, #228]
	ldr	r0, .L9+28
	add	sp, sp, #16
	@ sp needed
	pop	{r4, lr}
	b	sftl_printk
.L10:
	.align	2
.L9:
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
.L12:
	cmp	r2, r0
	uxth	r3, r1
	add	r1, r1, #1
	bls	.L13
	subs	r0, r3, #1
	uxth	r0, r0
	bx	lr
.L13:
	lsls	r2, r2, #1
	b	.L12
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
	ldr	r3, .L16
	cmp	r0, #24
	it	cc
	movcc	r0, #24
	ldrh	r2, [r3, #236]
	ldrh	r1, [r3, #246]
	str	r0, [r3, #232]
	muls	r2, r0, r2
	subs	r0, r1, r0
	ldr	r1, [r3, #252]
	strh	r0, [r3, #244]	@ movhi
	movs	r0, #0
	str	r2, [r3, #240]
	subs	r2, r1, r2
	str	r2, [r3, #248]
	bx	lr
.L17:
	.align	2
.L16:
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
	ldrh	r3, [fp, #6]
	ldrh	r1, [fp, #14]
	ldr	r4, .L28
	ldrh	r2, [r0]
	cmp	r1, #4
	ldrh	r6, [r0, #2]
	strh	r3, [r4, #246]	@ movhi
	it	eq
	lsreq	r3, r3, #1
	strh	r1, [r4, #262]	@ movhi
	ldr	r1, .L28+4
	itt	eq
	strheq	r3, [r4, #246]	@ movhi
	moveq	r3, #8
	ldrh	r0, [r0, #4]
	it	eq
	strheq	r3, [r4, #262]	@ movhi
	movs	r3, #0
	strh	r2, [r4, #256]	@ movhi
	strh	r6, [r4, #258]	@ movhi
	strh	r0, [r4, #260]	@ movhi
.L20:
	strb	r3, [r3, r1]
	adds	r3, r3, #1
	cmp	r3, #32
	bne	.L20
	ldrh	r5, [r4, #246]
	smulbb	r6, r6, r0
	movs	r3, #5
	cmp	r2, #1
	strh	r3, [r4, #296]	@ movhi
	uxth	r6, r6
	mov	r3, #0
	smulbb	r0, r0, r5
	mov	r7, #640
	it	eq
	strheq	r2, [r4, #296]	@ movhi
	strh	r3, [r4, #298]	@ movhi
	uxth	r0, r0
	strh	r7, [r4, #300]	@ movhi
	strh	r6, [r4, #236]	@ movhi
	strh	r0, [r4, #302]	@ movhi
	bl	Ftl_log2
	ldrh	r9, [fp, #12]
	ldrh	r10, [r4, #262]
	strh	r0, [r4, #304]	@ movhi
	strh	r9, [r4, #306]	@ movhi
	smulbb	r3, r6, r9
	mov	r0, r10
	strh	r9, [r4, #308]	@ movhi
	strh	r3, [r4, #310]	@ movhi
	bl	Ftl_log2
	lsl	r3, r10, #9
	mov	r8, r0
	strh	r0, [r4, #312]	@ movhi
	mul	r1, r9, r10
	uxth	r3, r3
	mov	r0, #5120
	asr	r7, r7, r8
	strh	r3, [r4, #314]	@ movhi
	lsrs	r3, r3, #8
	add	r8, r8, #9
	strh	r3, [r4, #316]	@ movhi
	adds	r7, r7, #2
	ldrh	r3, [fp, #20]
	strh	r3, [r4, #318]	@ movhi
	mul	r3, r5, r6
	lsls	r5, r5, #6
	asr	r5, r5, r8
	str	r3, [r4, #252]
	mul	r3, r10, r3
	mul	r3, r9, r3
	asrs	r3, r3, #11
	str	r3, [r4, #320]
	bl	__aeabi_idiv
	uxth	r0, r0
	strh	r5, [r4, #328]	@ movhi
	uxth	r5, r5
	mov	r1, r6
	cmp	r0, #4
	strh	r7, [r4, #326]	@ movhi
	itet	ls
	movls	r3, #4
	strhhi	r0, [r4, #324]	@ movhi
	strhls	r3, [r4, #324]	@ movhi
	mul	r3, r6, r5
	ldrh	r0, [r4, #324]
	adds	r5, r5, #8
	str	r3, [r4, #332]
	bl	__aeabi_uidiv
	uxtah	r0, r5, r0
	cmp	r6, #1
	it	eq
	addeq	r0, r0, #4
	str	r0, [r4, #232]
	ldrh	r0, [r4, #232]
	bl	FtlSysBlkNumInit
	ldr	r3, [r4, #232]
	movs	r0, #0
	str	r0, [r4, #344]
	str	r3, [r4, #336]
	ldr	r3, [r4, #248]
	lsls	r2, r3, #2
	ldrh	r3, [r4, #306]
	muls	r3, r2, r3
	ldrh	r2, [r4, #312]
	adds	r2, r2, #9
	lsrs	r3, r3, r2
	adds	r3, r3, #2
	strh	r3, [r4, #340]	@ movhi
	movs	r3, #32
	strh	r3, [r4, #342]	@ movhi
	ldrh	r3, [r4, #324]
	adds	r3, r3, #3
	strh	r3, [r4, #324]	@ movhi
	ldr	r3, [r4, #332]
	adds	r3, r3, #3
	str	r3, [r4, #332]
	pop	{r3, r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L29:
	.align	2
.L28:
	.word	.LANCHOR0
	.word	.LANCHOR0+264
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
	ldr	r2, .L37
	ldrh	r3, [r2, #348]
	cbz	r3, .L36
	ldr	r3, [r2, #352]
	ldrh	r2, [r2, #324]
	add	r2, r3, r2, lsl #1
.L32:
	cmp	r3, r2
	bne	.L33
.L36:
	movs	r0, #0
	bx	lr
.L33:
	ldrh	r1, [r3], #2
	cmp	r0, r1
	bne	.L32
	movs	r0, #1
	bx	lr
.L38:
	.align	2
.L37:
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
	ldr	r3, .L41
	ldr	r0, [r3, #344]
	bx	lr
.L42:
	.align	2
.L41:
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
	ldr	r4, .L44
	ldrh	r7, [r4, #302]
	mov	r1, r7
	bl	__aeabi_uidiv
	uxth	r6, r0
	mov	r1, r7
	mov	r0, r5
	bl	__aeabi_uidivmod
	add	r2, r4, r6, lsl #2
	uxth	r3, r1
	ldr	r2, [r2, #384]
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
	ldr	r0, .L44+4
	bl	sftl_printk
	ldrh	r3, [r4, #362]
	movs	r0, #0
	adds	r3, r3, #1
	strh	r3, [r4, #362]	@ movhi
	add	sp, sp, #12
	@ sp needed
	pop	{r4, r5, r6, r7, pc}
.L45:
	.align	2
.L44:
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
	ldr	r5, .L47
	ldrh	r6, [r5, #302]
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
	ldr	r3, [r5, #384]
	ldr	r0, [r3, r2, lsl #2]
	lsrs	r0, r0, r4
	and	r0, r0, #1
	pop	{r3, r4, r5, r6, r7, pc}
.L48:
	.align	2
.L47:
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
	ldr	r0, .L51
	movw	r3, #65535
	movs	r2, #16
	movs	r1, #255
	strh	r3, [r0, #356]	@ movhi
	movs	r3, #0
	strh	r3, [r0, #362]	@ movhi
	add	r0, r0, #368
	b	memset
.L52:
	.align	2
.L51:
	.word	.LANCHOR0
	.fnend
	.size	FtlBbtMemInit, .-FtlBbtMemInit
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
	ldr	r3, .L60
	push	{r4, r5, r6, lr}
	.save {r4, r5, r6, lr}
	movs	r5, #0
	mov	r4, r5
	ldrh	r2, [r3, #302]
	ldrh	r6, [r3, #258]
	muls	r6, r2, r6
.L54:
	uxth	r0, r5
	cmp	r0, r6
	blt	.L56
	mov	r0, r4
	pop	{r4, r5, r6, pc}
.L56:
	bl	FtlBbmIsBadBlock
	cbz	r0, .L55
	adds	r4, r4, #1
	uxth	r4, r4
.L55:
	adds	r5, r5, #1
	b	.L54
.L61:
	.align	2
.L60:
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
	ldr	r4, .L63
	mov	r7, r0
	ldrh	r6, [r4, #260]
	mov	r1, r6
	bl	__aeabi_uidiv
	ldrh	r4, [r4, #302]
	smulbb	r5, r6, r5
	mov	r1, r6
	smulbb	r4, r4, r0
	mov	r0, r7
	bl	__aeabi_uidivmod
	adds	r0, r5, r1
	add	r0, r0, r4
	uxth	r0, r0
	pop	{r3, r4, r5, r6, r7, pc}
.L64:
	.align	2
.L63:
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
	ldr	r3, .L66
	push	{r4, r5, r6, lr}
	.save {r4, r5, r6, lr}
	mov	r6, r0
	ldrh	r5, [r3, #260]
	ldrh	r1, [r3, #302]
	bl	__aeabi_uidiv
	mov	r1, r5
	smulbb	r4, r0, r5
	mov	r0, r6
	bl	__aeabi_uidivmod
	add	r1, r1, r4
	uxth	r0, r1
	pop	{r4, r5, r6, pc}
.L67:
	.align	2
.L66:
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
	ldr	r4, .L69
	ldrh	r1, [r4, #302]
	bl	__aeabi_uidivmod
	uxth	r0, r1
	ldrh	r1, [r4, #260]
	bl	__aeabi_uidiv
	uxth	r0, r0
	pop	{r4, pc}
.L70:
	.align	2
.L69:
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
	bls	.L72
	subs	r0, r0, r1
	cmp	r0, #-2147483648
	ite	hi
	movhi	r0, #0
	movls	r0, #1
	bx	lr
.L72:
	subs	r0, r1, r0
	cmp	r0, #-2147483648
	ite	ls
	movls	r0, #0
	movhi	r0, #1
	bx	lr
	.fnend
	.size	ftl_cmp_data_ver, .-ftl_cmp_data_ver
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
	ldr	r3, .L75
	mov	r1, #2048
	push	{r4, lr}
	.save {r4, lr}
	movs	r4, #0
	strh	r0, [r3, #416]	@ movhi
	add	r0, r3, #424
	strh	r4, [r3, #418]	@ movhi
	strh	r4, [r3, #420]	@ movhi
	strh	r4, [r3, #422]	@ movhi
	bl	__memzero
	mov	r0, r4
	pop	{r4, pc}
.L76:
	.align	2
.L75:
	.word	.LANCHOR0
	.fnend
	.size	FtlFreeSysBlkQueueInit, .-FtlFreeSysBlkQueueInit
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
	ldr	r3, .L78
	ldrh	r0, [r3, #422]
	clz	r0, r0
	lsrs	r0, r0, #5
	bx	lr
.L79:
	.align	2
.L78:
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
	ldr	r3, .L81
	ldrh	r0, [r3, #422]
	sub	r3, r0, #1024
	rsbs	r0, r3, #0
	adcs	r0, r0, r3
	bx	lr
.L82:
	.align	2
.L81:
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
	ldr	r3, .L92
	push	{r4, r5, r6, lr}
	.save {r4, r5, r6, lr}
	ldrh	r2, [r3, #422]
	cbz	r2, .L83
	ldrh	r5, [r3, #2500]
	movs	r0, #0
	ldrh	r1, [r3, #418]
	mov	r6, r0
	ldrh	r2, [r3, #420]
	and	r5, r5, #31
.L85:
	uxth	r4, r0
	adds	r0, r0, #1
	cmp	r5, r4
	bgt	.L86
	cbz	r6, .L83
	strh	r1, [r3, #418]	@ movhi
	strh	r2, [r3, #420]	@ movhi
.L83:
	pop	{r4, r5, r6, pc}
.L86:
	add	r4, r3, r1, lsl #1
	adds	r1, r1, #1
	ubfx	r1, r1, #0, #10
	ldrh	r6, [r4, #424]
	add	r4, r3, r2, lsl #1
	strh	r6, [r4, #424]	@ movhi
	movs	r6, #1
	add	r2, r2, r6
	ubfx	r2, r2, #0, #10
	b	.L85
.L93:
	.align	2
.L92:
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
	ldr	r3, .L100
	push	{r4, r5, lr}
	.save {r4, r5, lr}
	ldrh	r4, [r3, #422]
	cmp	r4, #1024
	beq	.L98
	ldrh	r5, [r3, #418]
	movs	r1, #0
.L96:
	cmp	r1, r4
	bcc	.L97
.L98:
	movs	r0, #0
	pop	{r4, r5, pc}
.L97:
	adds	r2, r1, r5
	ubfx	r2, r2, #0, #10
	add	r2, r3, r2, lsl #1
	ldrh	r2, [r2, #424]
	cmp	r2, r0
	beq	.L99
	adds	r1, r1, #1
	b	.L96
.L99:
	movs	r0, #1
	pop	{r4, r5, pc}
.L101:
	.align	2
.L100:
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
	ldr	r2, .L117
	ldrh	r3, [r2, #244]
	mov	r1, r2
	cmp	r3, r0
	bls	.L104
	movs	r5, #6
	ldr	lr, [r2, #2520]
	muls	r5, r0, r5
	movw	r3, #65535
	add	r4, lr, r5
	strh	r3, [r4, #2]	@ movhi
	strh	r3, [lr, r5]	@ movhi
	ldr	r3, [r2, #2524]
	cbnz	r3, .L105
	str	r4, [r2, #2524]
.L104:
	movs	r0, #0
	add	sp, sp, #12
	@ sp needed
	pop	{r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L105:
	ldrh	r7, [r4, #4]
	ldr	r9, [r2, #76]
	ldrh	r2, [r9, r0, lsl #1]
	cbz	r7, .L112
	muls	r2, r7, r2
.L116:
	ldr	r8, [r1, #2520]
	movw	r10, #65535
	str	r2, [sp]
	sub	r2, r3, r8
	asrs	r6, r2, #1
	ldr	r2, .L117+4
	muls	r2, r6, r2
	ldrh	r6, [r1, #244]
	str	r6, [sp, #4]
	movs	r6, #0
	uxth	r2, r2
.L110:
	adds	r6, r6, #1
	ldr	r7, [sp, #4]
	uxth	r6, r6
	cmp	r6, r7
	bhi	.L104
	cmp	r0, r2
	beq	.L104
	ldrh	ip, [r3, #4]
	cmp	ip, #0
	beq	.L108
	ldrh	fp, [r9, r2, lsl #1]
	ldr	r7, [sp]
	mul	ip, ip, fp
	cmp	ip, r7
	bcs	.L108
	ldrh	ip, [r3]
	cmp	ip, r10
	bne	.L109
	strh	r2, [r4, #2]	@ movhi
	strh	r0, [r3]	@ movhi
	str	r4, [r1, #2528]
	b	.L104
.L112:
	mov	r2, #-1
	b	.L116
.L109:
	movs	r3, #6
	mov	r2, ip
	mla	r3, r3, ip, r8
	b	.L110
.L108:
	strh	r2, [lr, r5]	@ movhi
	ldrh	r2, [r3, #2]
	strh	r2, [r4, #2]	@ movhi
	ldr	r2, [r1, #2524]
	cmp	r3, r2
	ittte	ne
	ldrhne	r4, [r3, #2]
	movne	r2, #6
	ldrne	r1, [r1, #2520]
	strheq	r0, [r3, #2]	@ movhi
	iteee	eq
	streq	r4, [r1, #2524]
	mulne	r2, r2, r4
	strhne	r0, [r1, r2]	@ movhi
	strhne	r0, [r3, #2]	@ movhi
	b	.L104
.L118:
	.align	2
.L117:
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
	ldr	r2, .L121
	ldrh	r3, [r2, #2532]
	adds	r3, r3, #1
	uxth	r3, r3
	strh	r3, [r2, #2532]	@ movhi
	ldrh	r2, [r2, #244]
	cmp	r2, r3
	bcs	.L119
	movs	r2, #214
	ldr	r1, .L121+4
	ldr	r0, .L121+8
	pop	{r3, lr}
	b	sftl_printk
.L119:
	pop	{r3, pc}
.L122:
	.align	2
.L121:
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
	beq	.L124
	ldr	r2, .L130
	movs	r1, #6
	mul	r7, r1, r0
	ldr	ip, [r2, #2520]
	mov	r5, r2
	add	r6, ip, r7
	strh	r4, [r6, #2]	@ movhi
	strh	r4, [ip, r7]	@ movhi
	ldr	r3, [r2, #2536]
	cbnz	r3, .L125
	str	r6, [r2, #2536]
.L124:
	movs	r0, #0
	pop	{r4, r5, r6, r7, r8, r9, r10, pc}
.L125:
	ldr	lr, [r2, #2520]
	ldr	r8, [r2, #2540]
	sub	r2, r3, lr
	asr	r10, r2, #1
	ldr	r2, .L130+4
	ldrh	r9, [r8, r0, lsl #1]
	mul	r2, r2, r10
	mov	r10, r4
	uxth	r2, r2
.L128:
	ldrh	r4, [r8, r2, lsl #1]
	cmp	r4, r9
	bcs	.L126
	ldrh	r4, [r3]
	cmp	r4, r10
	bne	.L127
	strh	r2, [r6, #2]	@ movhi
	strh	r0, [r3]	@ movhi
	b	.L124
.L127:
	mla	r3, r1, r4, lr
	mov	r2, r4
	b	.L128
.L126:
	ldrh	r1, [r3, #2]
	strh	r1, [r6, #2]	@ movhi
	strh	r2, [ip, r7]	@ movhi
	ldr	r2, [r5, #2536]
	cmp	r3, r2
	ittte	ne
	ldrhne	r4, [r3, #2]
	movne	r2, #6
	ldrne	r1, [r5, #2520]
	strheq	r0, [r3, #2]	@ movhi
	iteee	eq
	streq	r6, [r5, #2536]
	mulne	r2, r2, r4
	strhne	r0, [r1, r2]	@ movhi
	strhne	r0, [r3, #2]	@ movhi
	b	.L124
.L131:
	.align	2
.L130:
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
	ldr	r2, .L134
	ldrh	r3, [r2, #228]
	adds	r3, r3, #1
	uxth	r3, r3
	strh	r3, [r2, #228]	@ movhi
	ldrh	r2, [r2, #244]
	cmp	r2, r3
	bcs	.L132
	movs	r2, #207
	ldr	r1, .L134+4
	ldr	r0, .L134+8
	pop	{r3, lr}
	b	sftl_printk
.L132:
	pop	{r3, pc}
.L135:
	.align	2
.L134:
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
	ldr	r4, .L142
	muls	r6, r1, r6
	movw	r3, #65535
	mov	r8, r0
	ldr	r7, [r4, #2520]
	adds	r5, r7, r6
	ldrh	r2, [r5, #2]
	cmp	r2, r3
	bne	.L137
	ldr	r3, [r0]
	cmp	r5, r3
	beq	.L137
	mov	r2, #372
	ldr	r1, .L142+4
	ldr	r0, .L142+8
	bl	sftl_printk
.L137:
	ldr	r3, [r8]
	movw	r1, #65535
	cmp	r5, r3
	ldrh	r3, [r7, r6]
	bne	.L138
	cmp	r3, r1
	ittee	ne
	ldrne	r0, [r4, #2520]
	movne	r2, #6
	moveq	r3, #0
	streq	r3, [r8]
	ittt	ne
	mlane	r3, r2, r3, r0
	strne	r3, [r8]
	strhne	r1, [r3, #2]	@ movhi
.L140:
	movw	r3, #65535
	movs	r0, #0
	strh	r3, [r7, r6]	@ movhi
	strh	r3, [r5, #2]	@ movhi
	pop	{r4, r5, r6, r7, r8, pc}
.L138:
	cmp	r3, r1
	ldrh	r1, [r5, #2]
	bne	.L141
	cmp	r1, r3
	beq	.L140
	movs	r2, #6
	ldr	r0, [r4, #2520]
	muls	r1, r2, r1
	strh	r3, [r0, r1]	@ movhi
	b	.L140
.L141:
	ldr	r0, [r4, #2520]
	movs	r2, #6
	mla	r3, r2, r3, r0
	strh	r1, [r3, #2]	@ movhi
	ldrh	r0, [r5, #2]
	ldrh	r1, [r7, r6]
	ldr	r3, [r4, #2520]
	muls	r2, r0, r2
	strh	r1, [r3, r2]	@ movhi
	b	.L140
.L143:
	.align	2
.L142:
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
	cbz	r3, .L150
	ldr	r2, .L151
	movw	r5, #65535
	movs	r6, #6
	ldr	r2, [r2, #2520]
.L146:
	cbnz	r1, .L147
.L149:
	ldr	r4, .L151+4
	subs	r3, r3, r2
	asrs	r3, r3, #1
	muls	r4, r3, r4
	uxth	r1, r4
	bl	List_remove_node
	uxth	r0, r4
	pop	{r4, r5, r6, pc}
.L147:
	ldrh	r4, [r3]
	cmp	r4, r5
	beq	.L149
	subs	r1, r1, #1
	mla	r3, r6, r4, r2
	uxth	r1, r1
	b	.L146
.L150:
	movw	r0, #65535
	pop	{r4, r5, r6, pc}
.L152:
	.align	2
.L151:
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
	ldr	r2, .L160
	push	{r4, lr}
	.save {r4, lr}
	ldr	r3, [r2, #2524]
	cbz	r3, .L159
	ldr	r1, [r2, #2520]
	movs	r4, #6
	movw	r2, #65535
.L156:
	cbz	r0, .L157
	ldrh	r3, [r3]
	cmp	r3, r2
	bne	.L158
.L159:
	movw	r0, #65535
	pop	{r4, pc}
.L158:
	subs	r0, r0, #1
	mla	r3, r4, r3, r1
	uxth	r0, r0
	b	.L156
.L157:
	ldr	r0, .L160+4
	subs	r3, r3, r1
	asrs	r3, r3, #1
	muls	r3, r0, r3
	uxth	r0, r3
	pop	{r4, pc}
.L161:
	.align	2
.L160:
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
	ldr	r4, .L173
	ldrh	r3, [r4, #28]
	cmp	r3, r0
	beq	.L164
	ldrh	r3, [r4, #80]
	cmp	r3, r0
	beq	.L164
	ldrh	r3, [r4, #128]
	cmp	r3, r0
	beq	.L164
	movs	r7, #6
	ldr	r9, [r4, #2520]
	muls	r7, r0, r7
	ldr	r3, [r4, #2524]
	add	r8, r9, r7
	cmp	r8, r3
	beq	.L164
	ldrh	r6, [r8, #4]
	ldr	r3, [r4, #76]
	ldrh	r3, [r3, r0, lsl #1]
	cmp	r6, #0
	beq	.L171
	muls	r6, r3, r6
.L166:
	ldrh	r3, [r8, #2]
	movw	r2, #65535
	cmp	r3, r2
	bne	.L167
	ldrh	r2, [r9, r7]
	cmp	r2, r3
	bne	.L167
	movw	r2, #463
	ldr	r1, .L173+4
	ldr	r0, .L173+8
	bl	sftl_printk
.L167:
	ldrh	r3, [r8, #2]
	movw	r2, #65535
	cmp	r3, r2
	bne	.L168
	ldrh	r2, [r9, r7]
	cmp	r2, r3
	beq	.L164
.L168:
	movs	r2, #6
	muls	r2, r3, r2
	ldr	r3, .L173+12
	asrs	r1, r2, #1
	muls	r3, r1, r3
	ldr	r1, [r4, #76]
	ldrh	r0, [r1, r3, lsl #1]
	ldr	r1, [r4, #2520]
	add	r2, r2, r1
	ldrh	r3, [r2, #4]
	cbz	r3, .L172
	muls	r3, r0, r3
.L169:
	cmp	r6, r3
	bcs	.L164
	mov	r1, r5
	ldr	r0, .L173+16
	bl	List_remove_node
	ldrh	r3, [r4, #2532]
	cbnz	r3, .L170
	mov	r2, #474
	ldr	r1, .L173+4
	ldr	r0, .L173+8
	bl	sftl_printk
.L170:
	ldrh	r3, [r4, #2532]
	mov	r0, r5
	subs	r3, r3, #1
	strh	r3, [r4, #2532]	@ movhi
	bl	INSERT_DATA_LIST
.L164:
	movs	r0, #0
	pop	{r3, r4, r5, r6, r7, r8, r9, pc}
.L171:
	mov	r6, #-1
	b	.L166
.L172:
	mov	r3, #-1
	b	.L169
.L174:
	.align	2
.L173:
	.word	.LANCHOR0
	.word	.LANCHOR1+51
	.word	.LC8
	.word	-1431655765
	.word	.LANCHOR0+2524
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
	ldr	r5, .L185
	movs	r0, #12
	movw	r6, #65535
	ldrh	r2, [r5, #342]
	ldr	r3, [r5, #2544]
.L176:
	uxth	r4, r1
	cmp	r4, r2
	bcc	.L178
	mov	r4, r2
	movs	r1, #0
	mov	r7, #-2147483648
	mov	ip, #12
.L179:
	uxth	r6, r1
	cmp	r6, r2
	bcc	.L181
	cmp	r4, r2
	bcc	.L177
	ldrh	r7, [r5, #2548]
	mov	r4, r2
	movs	r1, #0
	mov	r0, #-1
.L182:
	uxth	r5, r1
	cmp	r5, r2
	bcc	.L184
	cmp	r4, r2
	bcc	.L177
	movw	r2, #789
	ldr	r1, .L185+4
	ldr	r0, .L185+8
	bl	sftl_printk
	b	.L177
.L178:
	adds	r1, r1, #1
	mla	r7, r0, r1, r3
	ldrh	r7, [r7, #-12]
	cmp	r7, r6
	bne	.L176
.L177:
	mov	r0, r4
	pop	{r3, r4, r5, r6, r7, pc}
.L181:
	mla	r0, ip, r1, r3
	ldr	r0, [r0, #4]
	cmp	r0, #0
	blt	.L180
	cmp	r7, r0
	itt	hi
	movhi	r7, r0
	movhi	r4, r6
.L180:
	adds	r1, r1, #1
	b	.L179
.L184:
	ldr	r6, [r3, #4]
	cmp	r0, r6
	bls	.L183
	ldrh	ip, [r3]
	cmp	ip, r7
	itt	ne
	movne	r0, r6
	movne	r4, r5
.L183:
	adds	r1, r1, #1
	adds	r3, r3, #12
	b	.L182
.L186:
	.align	2
.L185:
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
	ldr	r1, .L193
	push	{r4, r5, lr}
	.save {r4, r5, lr}
	mov	r3, r1
	ldrh	r2, [r1, #2550]
	cmp	r2, #4
	bhi	.L188
	cbnz	r0, .L188
	adds	r2, r2, #1
	strh	r2, [r1, #2550]	@ movhi
	pop	{r4, r5, pc}
.L188:
	movs	r2, #0
	ldrh	r1, [r3, #244]
	strh	r2, [r3, #2550]	@ movhi
	movw	r5, #65535
	str	r2, [r3, #2552]
	ldr	r2, [r3, #76]
	add	r1, r2, r1, lsl #1
.L189:
	cmp	r2, r1
	bne	.L191
	pop	{r4, r5, pc}
.L191:
	ldrh	r4, [r2], #2
	cmp	r4, r5
	ittt	ne
	ldrne	r0, [r3, #2552]
	addne	r0, r0, r4
	strne	r0, [r3, #2552]
	b	.L189
.L194:
	.align	2
.L193:
	.word	.LANCHOR0
	.fnend
	.size	FtlUpdateVaildLpn, .-FtlUpdateVaildLpn
	.align	1
	.global	ftl_set_blk_mode
	.syntax unified
	.thumb
	.thumb_func
	.fpu softvfp
	.type	ftl_set_blk_mode, %function
ftl_set_blk_mode:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	@ link register save eliminated.
	mov	r3, r0
	cbz	r1, .L196
	b	ftl_set_blk_mode.part.6
.L196:
	ldr	r2, .L197
	lsrs	r0, r0, #5
	and	r3, r3, #31
	ldr	r1, [r2, #24]
	movs	r2, #1
	lsl	r3, r2, r3
	ldr	r2, [r1, r0, lsl #2]
	bic	r2, r2, r3
	str	r2, [r1, r0, lsl #2]
	bx	lr
.L198:
	.align	2
.L197:
	.word	.LANCHOR0
	.fnend
	.size	ftl_set_blk_mode, .-ftl_set_blk_mode
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
	ldr	r3, .L200
	lsrs	r2, r0, #5
	and	r0, r0, #31
	ldr	r3, [r3, #24]
	ldr	r3, [r3, r2, lsl #2]
	lsr	r0, r3, r0
	and	r0, r0, #1
	bx	lr
.L201:
	.align	2
.L200:
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
	ldr	r3, .L209
	adds	r4, r4, #14
	ldrh	r5, [r3, #236]
.L203:
	cmp	r2, r5
	bcc	.L205
	ldrh	r3, [r3, #306]
	add	r4, r0, #16
	movw	r6, #65535
	subs	r3, r3, #1
	subs	r1, r3, r1
	movs	r3, #0
	uxth	r1, r1
.L206:
	uxth	r2, r3
	cmp	r5, r2
	bhi	.L208
	pop	{r4, r5, r6, r7, pc}
.L205:
	ldrh	r6, [r4, #2]!
	adds	r2, r2, #1
	uxth	r2, r2
	cmp	r6, r7
	ittt	ne
	ldrhne	r6, [r0, #4]
	addne	r6, r6, #1
	strhne	r6, [r0, #4]	@ movhi
	b	.L203
.L208:
	ldrh	r2, [r4], #2
	adds	r3, r3, #1
	cmp	r2, r6
	ittt	ne
	ldrhne	r2, [r0, #4]
	addne	r2, r2, r1
	strhne	r2, [r0, #4]	@ movhi
	b	.L206
.L210:
	.align	2
.L209:
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
	cbz	r3, .L211
	ldrh	r2, [r0]
	movw	r3, #65535
	cmp	r2, r3
	beq	.L211
	ldrb	r2, [r0, #6]	@ zero_extendqisi2
	movs	r5, #0
	adds	r2, r2, #8
	ldrh	r1, [r0, r2, lsl #1]
	ldr	r2, .L217
	ldrh	r4, [r2, #236]
	mov	r2, r3
.L214:
	cmp	r1, r2
	beq	.L216
.L211:
	pop	{r4, r5, pc}
.L216:
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
	b	.L214
.L218:
	.align	2
.L217:
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
	ldr	r6, .L225
	ldrh	r2, [r0]
	ldrh	r3, [r6, #244]
	cmp	r2, r3
	bcc	.L220
	movw	r2, #2148
	ldr	r1, .L225+4
	ldr	r0, .L225+8
	bl	sftl_printk
.L220:
	ldrh	r9, [r6, #236]
	add	r7, r4, #16
	ldr	r10, .L225+12
	movw	r8, #65535
	movs	r5, #0
	strh	r5, [r4, #4]	@ movhi
	strb	r5, [r4, #7]
.L221:
	uxth	r3, r5
	ldrh	r1, [r4]
	cmp	r9, r3
	bhi	.L223
	ldrh	r2, [r6, #306]
	movs	r0, #0
	ldrb	r3, [r4, #7]	@ zero_extendqisi2
	smulbb	r3, r3, r2
	strh	r3, [r4, #4]	@ movhi
	movs	r3, #0
	strb	r3, [r4, #9]
	ldr	r3, [r6, #2540]
	ldrh	r2, [r3, r1, lsl #1]
	movw	r3, #10000
	cmp	r2, r3
	itt	hi
	movhi	r3, #1
	strbhi	r3, [r4, #9]
	pop	{r3, r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L223:
	ldrb	r0, [r10, r5]	@ zero_extendqisi2
	bl	V2P_block
	strh	r8, [r7]	@ movhi
	mov	fp, r0
	bl	FtlBbmIsBadBlock
	cbnz	r0, .L222
	strh	fp, [r7]	@ movhi
	ldrb	r3, [r4, #7]	@ zero_extendqisi2
	adds	r3, r3, #1
	strb	r3, [r4, #7]
.L222:
	adds	r5, r5, #1
	adds	r7, r7, #2
	b	.L221
.L226:
	.align	2
.L225:
	.word	.LANCHOR0
	.word	.LANCHOR1+95
	.word	.LC8
	.word	.LANCHOR0+264
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
	ldr	r6, .L233
	mov	r7, r0
	mov	r4, r5
	ldrh	r8, [r6, #236]
	add	r10, r6, #264
	ldrh	r9, [r6, #306]
.L228:
	uxth	r3, r5
	cmp	r8, r3
	bhi	.L230
	cbz	r4, .L232
	mov	r1, r4
	mov	r0, #32768
	bl	__aeabi_idiv
.L231:
	ldr	r1, [r6, #2520]
	movs	r3, #6
	mla	r1, r3, r7, r1
	strh	r0, [r1, #4]	@ movhi
	movs	r0, #0
	pop	{r4, r5, r6, r7, r8, r9, r10, pc}
.L230:
	mov	r1, r7
	ldrb	r0, [r10, r5]	@ zero_extendqisi2
	bl	V2P_block
	bl	FtlBbmIsBadBlock
	cbnz	r0, .L229
	add	r4, r4, r9
	uxth	r4, r4
.L229:
	adds	r5, r5, #1
	b	.L228
.L232:
	mov	r0, r4
	b	.L231
.L234:
	.align	2
.L233:
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
	ldr	r2, .L238
	ldr	r0, [r2, #2536]
	cbz	r0, .L236
	ldr	r3, [r2, #2520]
	subs	r0, r0, r3
	ldr	r3, .L238+4
	asrs	r0, r0, #1
	muls	r0, r3, r0
	ldr	r3, [r2, #2540]
	uxth	r0, r0
	ldrh	r0, [r3, r0, lsl #1]
.L236:
	bx	lr
.L239:
	.align	2
.L238:
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
	ldr	r1, .L248
	push	{r4, r5, r6, r7, lr}
	.save {r4, r5, r6, r7, lr}
	ldr	r3, [r1, #2536]
	cbz	r3, .L246
	ldrh	r2, [r1, #228]
	movs	r6, #6
	ldr	r4, [r1, #2520]
	movw	r7, #65535
	rsb	r2, r2, r2, lsl #3
	subs	r3, r3, r4
	asrs	r3, r3, #1
	asrs	r2, r2, #3
	cmp	r0, r2
	it	gt
	uxthgt	r0, r2
	ldr	r2, .L248+4
	muls	r3, r2, r3
	movs	r2, #0
	uxth	r3, r3
.L243:
	uxth	r5, r2
	cmp	r0, r5
	bls	.L245
	mul	r5, r6, r3
	adds	r2, r2, #1
	ldrh	r5, [r4, r5]
	cmp	r5, r7
	bne	.L247
.L245:
	ldr	r2, [r1, #2540]
	ldrh	r0, [r2, r3, lsl #1]
	pop	{r4, r5, r6, r7, pc}
.L247:
	mov	r3, r5
	b	.L243
.L246:
	mov	r0, r3
	pop	{r4, r5, r6, r7, pc}
.L249:
	.align	2
.L248:
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
	ldr	r5, .L262
	add	r4, r6, #12
	.pad #20
	sub	sp, sp, #20
	ldr	r1, .L262+4
	bl	strcpy
	ldr	r3, [r5, #320]
	movs	r1, #64
	ldr	r2, .L262+8
	mov	r0, r4
	bl	snprintf
	add	r4, r4, r0
	ldr	r1, .L262+12
	mov	r0, r4
	adds	r4, r4, #10
	bl	strcpy
	ldr	r3, [r5, #2556]
	movs	r1, #64
	ldr	r2, .L262+16
	mov	r0, r4
	bl	snprintf
	add	r4, r4, r0
	ldr	r3, [r5, #2552]
	ldr	r2, .L262+20
	movs	r1, #64
	mov	r0, r4
	bl	snprintf
	add	r4, r4, r0
	ldr	r3, [r5, #2560]
	ldr	r2, .L262+24
	movs	r1, #64
	mov	r0, r4
	bl	snprintf
	add	r4, r4, r0
	ldr	r3, [r5, #2564]
	ldr	r2, .L262+28
	movs	r1, #64
	mov	r0, r4
	bl	snprintf
	add	r4, r4, r0
	ldr	r3, [r5, #2568]
	ldr	r2, .L262+32
	movs	r1, #64
	mov	r0, r4
	bl	snprintf
	add	r4, r4, r0
	ldr	r3, [r5, #2572]
	ldr	r2, .L262+36
	movs	r1, #64
	mov	r0, r4
	bl	snprintf
	add	r4, r4, r0
	ldr	r3, [r5, #2576]
	ldr	r2, .L262+40
	movs	r1, #64
	mov	r0, r4
	bl	snprintf
	add	r4, r4, r0
	ldr	r3, [r5, #2580]
	ldr	r2, .L262+44
	movs	r1, #64
	mov	r0, r4
	bl	snprintf
	ldr	r3, [r5, #2584]
	add	r4, r4, r0
	ldr	r2, .L262+48
	movs	r1, #64
	mov	r0, r4
	lsrs	r3, r3, #11
	bl	snprintf
	ldr	r3, [r5, #2588]
	add	r4, r4, r0
	ldr	r2, .L262+52
	movs	r1, #64
	mov	r0, r4
	lsrs	r3, r3, #11
	bl	snprintf
	add	r4, r4, r0
	ldr	r3, [r5, #2592]
	ldr	r2, .L262+56
	movs	r1, #64
	mov	r0, r4
	bl	snprintf
	add	r4, r4, r0
	ldr	r3, [r5, #2596]
	ldr	r2, .L262+60
	movs	r1, #64
	mov	r0, r4
	bl	snprintf
	add	r4, r4, r0
	ldrh	r3, [r5, #362]
	ldr	r2, .L262+64
	movs	r1, #64
	mov	r0, r4
	bl	snprintf
	add	r4, r4, r0
	ldrh	r3, [r5, #228]
	ldr	r2, .L262+68
	movs	r1, #64
	mov	r0, r4
	bl	snprintf
	add	r4, r4, r0
	ldr	r3, [r5, #2600]
	ldr	r2, .L262+72
	movs	r1, #64
	mov	r0, r4
	bl	snprintf
	add	r4, r4, r0
	ldr	r3, [r5, #2604]
	ldr	r2, .L262+76
	movs	r1, #64
	mov	r0, r4
	bl	snprintf
	add	r4, r4, r0
	ldr	r3, [r5, #2608]
	ldr	r2, .L262+80
	movs	r1, #64
	mov	r0, r4
	bl	snprintf
	add	r4, r4, r0
	ldr	r3, [r5, #2612]
	ldr	r2, .L262+84
	movs	r1, #64
	mov	r0, r4
	bl	snprintf
	add	r4, r4, r0
	ldr	r3, [r5, #2616]
	ldr	r2, .L262+88
	movs	r1, #64
	mov	r0, r4
	bl	snprintf
	add	r4, r4, r0
	ldr	r3, [r5, #2620]
	ldr	r2, .L262+92
	movs	r1, #64
	mov	r0, r4
	bl	snprintf
	add	r4, r4, r0
	ldrh	r3, [r5, #2502]
	ldr	r2, .L262+96
	movs	r1, #64
	mov	r0, r4
	bl	snprintf
	add	r4, r4, r0
	ldrh	r3, [r5, #2500]
	ldr	r2, .L262+100
	movs	r1, #64
	mov	r0, r4
	bl	snprintf
	add	r4, r4, r0
	ldr	r3, [r5, #344]
	ldr	r2, .L262+104
	movs	r1, #64
	mov	r0, r4
	bl	snprintf
	add	r4, r4, r0
	ldr	r3, [r5, #336]
	ldr	r2, .L262+108
	movs	r1, #64
	mov	r0, r4
	bl	snprintf
	add	r4, r4, r0
	ldr	r3, [r5, #232]
	ldr	r2, .L262+112
	movs	r1, #64
	mov	r0, r4
	bl	snprintf
	add	r4, r4, r0
	ldrh	r3, [r5, #422]
	ldr	r2, .L262+116
	movs	r1, #64
	mov	r0, r4
	bl	snprintf
	add	r4, r4, r0
	ldrh	r3, [r5, #244]
	ldr	r2, .L262+120
	movs	r1, #64
	mov	r0, r4
	bl	snprintf
	add	r4, r4, r0
	ldrh	r3, [r5, #2624]
	ldr	r2, .L262+124
	movs	r1, #64
	mov	r0, r4
	bl	snprintf
	add	r4, r4, r0
	ldr	r3, [r5, #248]
	ldr	r2, .L262+128
	movs	r1, #64
	mov	r0, r4
	bl	snprintf
	add	r4, r4, r0
	ldrh	r3, [r5, #2628]
	ldr	r2, .L262+132
	movs	r1, #64
	mov	r0, r4
	bl	snprintf
	add	r4, r4, r0
	ldrh	r3, [r5, #356]
	ldr	r2, .L262+136
	movs	r1, #64
	mov	r0, r4
	bl	snprintf
	add	r4, r4, r0
	ldrh	r3, [r5, #30]
	ldr	r2, .L262+140
	movs	r1, #64
	mov	r0, r4
	bl	snprintf
	add	r4, r4, r0
	ldrb	r3, [r5, #34]	@ zero_extendqisi2
	ldr	r2, .L262+144
	movs	r1, #64
	mov	r0, r4
	bl	snprintf
	add	r4, r4, r0
	ldrh	r3, [r5, #28]
	ldr	r2, .L262+148
	movs	r1, #64
	mov	r0, r4
	bl	snprintf
	add	r4, r4, r0
	ldrb	r3, [r5, #36]	@ zero_extendqisi2
	ldr	r2, .L262+152
	movs	r1, #64
	mov	r0, r4
	bl	snprintf
	add	r4, r4, r0
	ldrh	r3, [r5, #32]
	ldr	r2, .L262+156
	movs	r1, #64
	mov	r0, r4
	bl	snprintf
	ldrh	r2, [r5, #28]
	add	r4, r4, r0
	ldr	r3, [r5, #76]
	movs	r1, #64
	mov	r0, r4
	ldrh	r3, [r3, r2, lsl #1]
	ldr	r2, .L262+160
	bl	snprintf
	add	r4, r4, r0
	ldrh	r3, [r5, #82]
	ldr	r2, .L262+164
	movs	r1, #64
	mov	r0, r4
	bl	snprintf
	add	r4, r4, r0
	ldrb	r3, [r5, #86]	@ zero_extendqisi2
	ldr	r2, .L262+168
	movs	r1, #64
	mov	r0, r4
	bl	snprintf
	add	r4, r4, r0
	ldrh	r3, [r5, #80]
	ldr	r2, .L262+172
	movs	r1, #64
	mov	r0, r4
	bl	snprintf
	add	r4, r4, r0
	ldrb	r3, [r5, #88]	@ zero_extendqisi2
	ldr	r2, .L262+176
	movs	r1, #64
	mov	r0, r4
	bl	snprintf
	add	r4, r4, r0
	ldrh	r3, [r5, #84]
	ldr	r2, .L262+180
	movs	r1, #64
	mov	r0, r4
	bl	snprintf
	ldrh	r2, [r5, #80]
	add	r4, r4, r0
	ldr	r3, [r5, #76]
	movs	r1, #64
	mov	r0, r4
	ldrh	r3, [r3, r2, lsl #1]
	ldr	r2, .L262+184
	bl	snprintf
	add	r4, r4, r0
	ldrh	r3, [r5, #130]
	ldr	r2, .L262+188
	movs	r1, #64
	mov	r0, r4
	bl	snprintf
	add	r4, r4, r0
	ldrb	r3, [r5, #134]	@ zero_extendqisi2
	ldr	r2, .L262+192
	movs	r1, #64
	mov	r0, r4
	bl	snprintf
	add	r4, r4, r0
	ldrh	r3, [r5, #128]
	ldr	r2, .L262+196
	movs	r1, #64
	mov	r0, r4
	bl	snprintf
	add	r4, r4, r0
	ldrb	r3, [r5, #136]	@ zero_extendqisi2
	ldr	r2, .L262+200
	movs	r1, #64
	mov	r0, r4
	bl	snprintf
	add	r4, r4, r0
	ldrh	r3, [r5, #132]
	ldr	r2, .L262+204
	movs	r1, #64
	mov	r0, r4
	bl	snprintf
	add	r4, r4, r0
	ldrh	r3, [r5, #178]
	ldr	r2, .L262+208
	movs	r1, #64
	mov	r0, r4
	bl	snprintf
	add	r4, r4, r0
	ldrb	r3, [r5, #182]	@ zero_extendqisi2
	ldr	r2, .L262+212
	movs	r1, #64
	mov	r0, r4
	bl	snprintf
	add	r4, r4, r0
	ldrh	r3, [r5, #176]
	ldr	r2, .L262+216
	movs	r1, #64
	mov	r0, r4
	bl	snprintf
	add	r4, r4, r0
	ldrb	r3, [r5, #184]	@ zero_extendqisi2
	ldr	r2, .L262+220
	movs	r1, #64
	mov	r0, r4
	bl	snprintf
	add	r4, r4, r0
	ldrh	r3, [r5, #180]
	b	.L263
.L264:
	.align	2
.L262:
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
.L263:
	ldr	r2, .L265
	movs	r1, #64
	mov	r0, r4
	bl	snprintf
	ldr	r3, [r5, #2720]
	add	r4, r4, r0
	ldr	r2, .L265+4
	movs	r1, #64
	mov	r0, r4
	str	r3, [sp, #4]
	ldr	r3, [r5, #2728]
	str	r3, [sp]
	ldr	r3, [r5, #2724]
	bl	snprintf
	add	r4, r4, r0
	ldr	r3, [r5, #2716]
	ldr	r2, .L265+8
	movs	r1, #64
	mov	r0, r4
	bl	snprintf
	add	r4, r4, r0
	ldr	r3, [r5, #2740]
	ldr	r2, .L265+12
	movs	r1, #64
	mov	r0, r4
	bl	snprintf
	add	r4, r4, r0
	ldrh	r3, [r5, #3156]
	ldr	r2, .L265+16
	movs	r1, #64
	mov	r0, r4
	bl	snprintf
	add	r4, r4, r0
	ldrh	r3, [r5, #3158]
	ldr	r2, .L265+20
	movs	r1, #64
	mov	r0, r4
	bl	snprintf
	add	r4, r4, r0
	ldr	r3, [r5, #3160]
	ldr	r2, .L265+24
	movs	r1, #64
	mov	r0, r4
	bl	snprintf
	add	r4, r4, r0
	ldrh	r3, [r5, #3164]
	ldr	r2, .L265+28
	movs	r1, #64
	mov	r0, r4
	bl	snprintf
	add	r4, r4, r0
	bl	GetFreeBlockMinEraseCount
	ldr	r2, .L265+32
	mov	r3, r0
	movs	r1, #64
	mov	r0, r4
	bl	snprintf
	add	r4, r4, r0
	ldrh	r0, [r5, #228]
	bl	GetFreeBlockMaxEraseCount
	ldr	r2, .L265+36
	mov	r3, r0
	movs	r1, #64
	mov	r0, r4
	bl	snprintf
	ldr	r3, .L265+40
	add	r4, r4, r0
	ldr	r3, [r3]
	cmp	r3, #1
	beq	.L251
.L256:
	subs	r0, r4, r6
	add	sp, sp, #20
	@ sp needed
	pop	{r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L251:
	ldrh	r3, [r5, #176]
	movw	r2, #65535
	cmp	r3, r2
	beq	.L253
	ldr	r2, [r5, #76]
	mov	r0, r4
	movs	r1, #64
	ldrh	r3, [r2, r3, lsl #1]
	ldr	r2, .L265+44
	bl	snprintf
	add	r4, r4, r0
.L253:
	movs	r0, #0
	ldr	r9, .L265+52
	bl	List_get_gc_head_node
	uxth	r0, r0
	movs	r7, #0
	movw	fp, #65535
	mov	r10, #6
.L255:
	cmp	r0, fp
	beq	.L254
	ldr	r3, [r5, #2540]
	mul	r8, r10, r0
	mov	r2, r9
	movs	r1, #64
	ldrh	r3, [r3, r0, lsl #1]
	str	r3, [sp, #12]
	ldr	r3, [r5, #2520]
	add	r3, r3, r8
	ldrh	r3, [r3, #4]
	str	r3, [sp, #8]
	ldr	r3, [r5, #76]
	ldrh	r3, [r3, r0, lsl #1]
	stm	sp, {r0, r3}
	mov	r3, r7
	mov	r0, r4
	adds	r7, r7, #1
	bl	snprintf
	ldr	r3, [r5, #2520]
	cmp	r7, #16
	add	r4, r4, r0
	ldrh	r0, [r3, r8]
	bne	.L255
.L254:
	ldr	r2, [r5, #2520]
	movs	r7, #0
	ldr	r3, [r5, #2536]
	movw	r9, #65535
	ldr	fp, .L265+56
	mov	r10, #6
	subs	r3, r3, r2
	asrs	r2, r3, #1
	ldr	r3, .L265+48
	muls	r3, r2, r3
	uxth	r3, r3
.L257:
	cmp	r3, r9
	beq	.L256
	ldr	r2, [r5, #2540]
	mul	r8, r10, r3
	mov	r0, r4
	movs	r1, #64
	ldrh	r2, [r2, r3, lsl #1]
	str	r2, [sp, #8]
	ldr	r2, [r5, #2520]
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
	beq	.L256
	ldr	r3, [r5, #2520]
	ldrh	r3, [r3, r8]
	b	.L257
.L266:
	.align	2
.L265:
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
	blt	.L269
	ldr	r3, .L270
	movs	r1, #64
	ldr	r2, .L270+4
	mov	r0, r5
	bl	snprintf
	adds	r4, r5, r0
	mov	r0, r4
	bl	FtlPrintInfo2buf
	add	r0, r0, r4
	subs	r0, r0, r5
	pop	{r3, r4, r5, pc}
.L269:
	movs	r0, #0
	pop	{r3, r4, r5, pc}
.L271:
	.align	2
.L270:
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
	ldr	r4, .L296
	ldr	r2, [r4, #2608]
	ldr	r3, [r4, #2620]
	cmp	r2, r3
	bcs	.L273
	ldrh	r1, [r4, #244]
	movs	r3, #0
	ldr	r0, [r4, #2540]
	str	r3, [r4, #2600]
.L274:
	cmp	r3, r1
	bcc	.L275
	ldr	r5, [r4, #2600]
	mov	r0, r5
	bl	__aeabi_uidiv
	str	r0, [r4, #2608]
	ldr	r0, [r4, #2604]
	ldrh	r1, [r4, #296]
	subs	r0, r5, r0
	bl	__aeabi_uidiv
	str	r0, [r4, #2600]
.L276:
	ldr	r6, [r4, #2620]
	ldr	r7, [r4, #2608]
	add	r3, r6, #256
	cmp	r3, r7
	bls	.L281
	ldr	r2, [r4, #2616]
	add	r3, r6, #768
	cmp	r3, r2
	bls	.L281
.L283:
	movw	r5, #65535
.L282:
	mov	r0, r5
	add	sp, sp, #28
	@ sp needed
	pop	{r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L275:
	ldrh	r2, [r0, r3, lsl #1]
	adds	r3, r3, #1
	ldr	r5, [r4, #2600]
	add	r2, r2, r5
	str	r2, [r4, #2600]
	b	.L274
.L273:
	ldr	r3, [r4, #2616]
	cmp	r2, r3
	bls	.L276
	adds	r3, r3, #1
	str	r3, [r4, #2616]
	movs	r3, #0
.L278:
	ldrh	r2, [r4, #244]
	cmp	r3, r2
	bcs	.L276
	ldr	r1, [r4, #2540]
	ldrh	r2, [r1, r3, lsl #1]
	adds	r2, r2, #1
	strh	r2, [r1, r3, lsl #1]	@ movhi
	adds	r3, r3, #1
	b	.L278
.L281:
	ldrh	r0, [r4, #228]
	add	r0, r0, r0, lsl #1
	ubfx	r0, r0, #2, #16
	bl	GetFreeBlockMaxEraseCount
	add	r3, r6, #64
	mov	r9, r0
	cmp	r0, r3
	bcc	.L283
	ldr	r3, [r4, #2524]
	cmp	r3, #0
	beq	.L283
	ldrh	r2, [r4, #244]
	movw	r1, #65535
	ldr	lr, [r4, #2520]
	movs	r0, #0
	ldr	r8, [r4, #2540]
	mov	r10, r1
	str	r2, [sp, #20]
	mov	fp, #6
	mov	r2, r1
.L284:
	ldrh	ip, [r3]
	cmp	ip, r10
	bne	.L287
	mov	r5, r2
.L286:
	movw	r3, #65535
	cmp	r5, r3
	beq	.L283
	ldrh	r10, [r8, r5, lsl #1]
	lsl	fp, r5, #1
	cmp	r6, r10
	bcs	.L288
	bl	GetFreeBlockMinEraseCount
	cmp	r6, r0
	it	cc
	strcc	r1, [r4, #2620]
.L288:
	cmp	r7, r10
	bls	.L283
	add	r3, r10, #128
	cmp	r9, r3
	ble	.L283
	add	r3, r10, #256
	cmp	r7, r3
	bhi	.L289
	ldr	r3, [r4, #2616]
	add	r10, r10, #768
	cmp	r10, r3
	bcs	.L283
.L289:
	str	r9, [sp, #8]
	mov	r2, r7
	ldrh	r3, [r8, fp]
	mov	r1, r5
	ldr	r0, .L296+4
	str	r3, [sp, #4]
	ldr	r3, [r4, #76]
	ldrh	r3, [r3, fp]
	str	r3, [sp]
	ldr	r3, [r4, #2616]
	bl	sftl_printk
	movs	r3, #1
	str	r3, [r4, #3168]
	b	.L282
.L287:
	adds	r0, r0, #1
	ldr	r5, [sp, #20]
	uxth	r0, r0
	cmp	r0, r5
	bhi	.L283
	ldrh	r5, [r3, #4]
	cbz	r5, .L285
	ldr	r5, .L296+8
	sub	r3, r3, lr
	asrs	r3, r3, #1
	muls	r3, r5, r3
	uxth	r5, r3
	ldrh	r3, [r8, r5, lsl #1]
	cmp	r6, r3
	bcs	.L286
	cmp	r1, r3
	itt	hi
	movhi	r1, r3
	movhi	r2, r5
.L285:
	mla	r3, fp, ip, lr
	b	.L284
.L297:
	.align	2
.L296:
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
	beq	.L299
	ldr	r2, .L300
	movs	r1, #0
	ldr	r2, [r2, #76]
	strh	r1, [r2, r0, lsl #1]	@ movhi
	bl	INSERT_FREE_LIST
.L299:
	movs	r0, #0
	pop	{r3, pc}
.L301:
	.align	2
.L300:
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
	bne	.L303
	movw	r2, #2781
	ldr	r1, .L317
	ldr	r0, .L317+4
	bl	sftl_printk
.L303:
	ldr	r6, .L317+8
	ldrh	r2, [r4, #2]
	ldrh	r3, [r6, #306]
	cmp	r2, r3
	bne	.L304
	movw	r2, #2782
	ldr	r1, .L317
	ldr	r0, .L317+4
	bl	sftl_printk
.L304:
	ldrh	r3, [r4, #4]
	cbnz	r3, .L305
	movw	r2, #2783
	ldr	r1, .L317
	ldr	r0, .L317+4
	bl	sftl_printk
.L305:
	ldrb	r2, [r4, #6]	@ zero_extendqisi2
	movs	r3, #0
	ldrh	r1, [r6, #236]
	movw	r5, #65535
	strb	r3, [r4, #10]
	adds	r2, r2, #8
	ldrh	r0, [r4, r2, lsl #1]
	mov	r2, r3
.L306:
	cmp	r0, r5
	beq	.L308
	ldrh	r5, [r4, #2]
	ldrh	r6, [r6, #306]
	cmp	r5, r6
	bcs	.L312
	ldrh	r2, [r4, #4]
	orr	r5, r5, r0, lsl #10
	ldrb	r3, [r4, #6]	@ zero_extendqisi2
	movw	r0, #65535
	subs	r2, r2, #1
	uxth	r2, r2
	strh	r2, [r4, #4]	@ movhi
.L311:
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
	beq	.L311
	strb	r3, [r4, #6]
	ldrh	r3, [r4, #2]
	cmp	r3, r6
	bne	.L302
	cbz	r2, .L302
	movw	r2, #2806
	ldr	r1, .L317
	ldr	r0, .L317+4
	bl	sftl_printk
.L302:
	mov	r0, r5
	pop	{r3, r4, r5, r6, r7, pc}
.L308:
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
	b	.L306
.L312:
	movw	r5, #65535
	b	.L302
.L318:
	.align	2
.L317:
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
	ldr	r3, .L328
	mov	ip, #20
	push	{r4, r5, r6, r7, r8, r9, lr}
	.save {r4, r5, r6, r7, r8, r9, lr}
	mov	r9, #12
	movs	r6, #1
	movs	r2, #0
	mov	r7, r3
	str	r2, [r3, #3172]
.L320:
	ldrh	r1, [r3, #236]
	uxth	r0, r2
	adds	r4, r2, #1
	cmp	r0, r1
	bcc	.L323
	ldr	r7, .L328
	mov	ip, #12
	movs	r6, #0
.L324:
	ldr	r2, [r3, #3192]
	cmp	r1, r2
	bcc	.L327
	pop	{r4, r5, r6, r7, r8, r9, pc}
.L323:
	uxth	r2, r2
	ldr	r8, [r3, #3176]
	mul	lr, r9, r2
	add	r1, r8, lr
	str	r6, [r1, #8]
	ldrh	r1, [r3, #314]
	muls	r1, r2, r1
	it	mi
	addmi	r1, r1, #3
	bic	r0, r1, #3
	ldr	r1, [r3, #3180]
	add	r1, r1, r0
	str	r1, [r8, lr]
	ldr	r5, [r7, #3184]
	ldr	r0, [r3, #3176]
	ldrh	r1, [r3, #316]
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
	b	.L320
.L327:
	mul	r0, ip, r1
	ldr	r4, [r3, #3176]
	ldr	r5, [r3, #3180]
	adds	r2, r4, r0
	str	r6, [r2, #8]
	ldrh	r2, [r3, #314]
	muls	r2, r1, r2
	it	mi
	addmi	r2, r2, #3
	bic	r2, r2, #3
	add	r2, r2, r5
	str	r2, [r4, r0]
	ldr	r2, [r3, #3176]
	ldr	r4, [r7, #3184]
	add	r0, r0, r2
	ldrh	r2, [r3, #316]
	muls	r2, r1, r2
	it	mi
	addmi	r2, r2, #3
	bic	r2, r2, #3
	adds	r1, r1, #1
	add	r2, r2, r4
	uxth	r1, r1
	str	r2, [r0, #4]
	b	.L324
.L329:
	.align	2
.L328:
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
	ldr	r3, .L337
	push	{r4, r5, r6, r7, r8, r9, r10, lr}
	.save {r4, r5, r6, r7, r8, r9, r10, lr}
	movs	r5, #0
	mov	r10, #20
	mov	lr, #12
	mov	r8, r5
	ldr	ip, [r3, #3192]
	ldr	r6, [r3, #3176]
.L331:
	uxth	r3, r5
	cmp	r1, r3
	bls	.L330
	mla	r4, r10, r3, r0
	movs	r2, #0
.L332:
	uxth	r3, r2
	cmp	ip, r3
	bls	.L333
	mul	r3, lr, r3
	ldr	r7, [r4, #8]
	adds	r2, r2, #1
	add	r9, r6, r3
	ldr	r3, [r6, r3]
	cmp	r3, r7
	bne	.L332
	str	r8, [r9, #8]
.L333:
	adds	r5, r5, #1
	b	.L331
.L330:
	pop	{r4, r5, r6, r7, r8, r9, r10, pc}
.L338:
	.align	2
.L337:
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
	ldr	r3, .L346
	mov	ip, #12
	push	{r4, r5, r6, r7, r8, r9, lr}
	.save {r4, r5, r6, r7, r8, r9, lr}
	movs	r4, #0
	mov	lr, #1
	mov	r8, #20
	ldr	r5, [r3, #3192]
	ldr	r6, [r3, #3176]
.L340:
	uxth	r2, r4
	cmp	r1, r2
	bhi	.L344
	pop	{r4, r5, r6, r7, r8, r9, pc}
.L344:
	mov	r9, #0
.L341:
	uxth	r3, r9
	cmp	r5, r3
	bls	.L342
	mla	r3, ip, r3, r6
	add	r9, r9, #1
	ldr	r7, [r3, #8]
	cmp	r7, #0
	bne	.L341
	mla	r2, r8, r2, r0
	ldr	r7, [r3]
	str	lr, [r3, #8]
	str	r7, [r2, #8]
	ldr	r3, [r3, #4]
	str	r3, [r2, #12]
.L342:
	adds	r4, r4, #1
	b	.L340
.L347:
	.align	2
.L346:
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
	ldr	r2, .L353
	ldr	r3, [r2, #3196]
	ldrh	r2, [r2, #3200]
	add	r2, r3, r2, lsl #1
.L349:
	cmp	r3, r2
	bne	.L351
	movs	r0, #0
	bx	lr
.L351:
	ldrh	r1, [r3], #2
	cmp	r1, r0
	bne	.L349
	movs	r0, #1
	bx	lr
.L354:
	.align	2
.L353:
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
	ldr	r3, .L359
	ldrh	r1, [r3, #3200]
	ldr	r2, [r3, #3196]
.L356:
	uxth	ip, r4
	cmp	ip, r1
	bcc	.L358
	bne	.L357
	strh	r0, [r2, ip, lsl #1]	@ movhi
	ldrh	r0, [r3, #3200]
	adds	r0, r0, #1
	strh	r0, [r3, #3200]	@ movhi
	b	.L357
.L358:
	adds	r4, r4, #1
	add	ip, r2, r4, lsl #1
	ldrh	ip, [ip, #-2]
	cmp	ip, r0
	bne	.L356
.L357:
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
.L360:
	.align	2
.L359:
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
	ldr	r0, .L364
	bl	sftl_printk
	ldr	r3, .L364+4
	ldrh	r0, [r3, #226]
	cmp	r4, r0
	beq	.L362
	ldrh	r1, [r3, #224]
	cmp	r4, r1
	beq	.L362
	movw	r2, #65535
	cmp	r0, r2
	bne	.L363
	strh	r4, [r3, #226]	@ movhi
.L362:
	movs	r0, #0
	pop	{r4, pc}
.L363:
	cmp	r1, r2
	it	eq
	strheq	r4, [r3, #224]	@ movhi
	b	.L362
.L365:
	.align	2
.L364:
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
	ldr	r4, .L370
	bl	P2V_block_in_plane
	mov	r2, r5
	mov	r6, r0
	ldrh	r1, [r4, #3210]
	ldr	r0, .L370+4
	bl	sftl_printk
	mov	r0, r6
	bl	FtlGcRefreshBlock
	ldrh	r3, [r4, #3210]
	movs	r2, #0
	addw	r0, r4, #3212
.L367:
	uxth	r1, r2
	cmp	r3, r1
	bhi	.L369
	cmp	r3, #15
	itttt	ls
	addls	r2, r3, #1
	strhls	r2, [r4, #3210]	@ movhi
	addls	r4, r4, r3, lsl #1
	strhls	r5, [r4, #3212]	@ movhi
	b	.L368
.L369:
	adds	r2, r2, #1
	add	r1, r0, r2, lsl #1
	ldrh	r1, [r1, #-2]
	cmp	r1, r5
	bne	.L367
.L368:
	movs	r0, #0
	pop	{r4, r5, r6, pc}
.L371:
	.align	2
.L370:
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
	ldr	r4, .L378
	ldrh	r3, [r4, #3210]
	cbz	r3, .L373
	ldrh	r1, [r4, #226]
	movw	r2, #65535
	cmp	r1, r2
	bne	.L373
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
.L373:
	movs	r0, #0
	pop	{r4, pc}
.L379:
	.align	2
.L378:
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
	ldr	r1, .L381
	b	__kmalloc
.L382:
	.align	2
.L381:
	.word	37748929
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
	ldr	fp, .L393+16
	mov	r10, r0
	mov	r7, r1
	mov	r8, r2
	mov	r9, r3
	mov	r5, r6
	mov	r4, r6
.L385:
	cmp	r4, r9
	bcc	.L391
	ldr	r0, .L393
	pop	{r3, r4, r5, r6, r7, r8, r9, r10, fp, lr}
	b	sftl_printk
.L391:
	cbnz	r5, .L386
	mov	r3, r6
	mov	r2, r7
	mov	r1, r10
	ldr	r0, .L393+4
	bl	sftl_printk
.L386:
	cmp	r8, #4
	bne	.L387
	ldr	r1, [r7, r4, lsl #2]
	ldr	r0, .L393+8
.L392:
	adds	r5, r5, #1
	bl	sftl_printk
	cmp	r5, #15
	bls	.L390
	movs	r5, #0
	ldr	r0, .L393
	bl	sftl_printk
.L390:
	adds	r4, r4, #1
	add	r6, r6, r8
	b	.L385
.L387:
	cmp	r8, #2
	ittee	eq
	ldrheq	r1, [r7, r4, lsl #1]
	moveq	r0, fp
	ldrbne	r1, [r7, r4]	@ zero_extendqisi2
	ldrne	r0, .L393+12
	b	.L392
.L394:
	.align	2
.L393:
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
	@ args = 0, pretend = 0, frame = 16
	@ frame_needed = 0, uses_anonymous_args = 0
	push	{r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.save {r4, r5, r6, r7, r8, r9, r10, fp, lr}
	mov	r8, r2
	ldr	r5, .L411
	mov	r4, r0
	mov	fp, r0
	movs	r7, #0
	mov	r10, #-1
	.pad #20
	sub	sp, sp, #20
	ldrh	r9, [r5, #12]
	lsl	r3, r9, #3
	str	r3, [sp, #4]
.L396:
	cmp	r7, r8
	beq	.L400
	add	r2, sp, #8
	add	r1, sp, #12
	mov	r0, fp
	bl	l2p_addr_tran
	ldr	r6, [sp, #8]
	cbnz	r6, .L397
	ldr	r2, [sp, #12]
	ldr	r3, [sp, #4]
	cmp	r3, r2
	bls	.L397
	ldr	r5, .L411+4
	ldr	r7, .L411+8
	ldr	r9, .L411+16
	b	.L410
.L399:
	mov	r3, #-1
	ldr	r2, [sp, #12]
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
	ldr	r0, .L411+12
	bl	rknand_print_hex
.L410:
	cmp	r6, r8
	add	r4, r4, #20
	bne	.L399
	bl	dump_stack
.L400:
	movs	r0, #0
	add	sp, sp, #20
	@ sp needed
	pop	{r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L397:
	ldr	r2, [r5, #3252]
	uxtb	r0, r6
	ldr	r1, [sp, #12]
	blx	r2
	cmp	r0, #0
	mov	r2, r0
	it	ne
	movne	r2, r10
	str	r2, [fp]
	ldrh	r2, [r5, #14]
	cmp	r2, #4
	bne	.L404
	ldr	r1, [sp, #12]
	ldr	r2, [r5, #3252]
	ldrb	r0, [sp, #8]	@ zero_extendqisi2
	add	r1, r1, r9
	blx	r2
	cbz	r0, .L404
	str	r10, [fp]
.L404:
	adds	r7, r7, #1
	add	fp, fp, #20
	b	.L396
.L412:
	.align	2
.L411:
	.word	.LANCHOR0
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
	bhi	.L413
	ldr	r4, .L422
	ldrh	r3, [r4, #422]
	cmp	r3, #1024
	beq	.L413
	cbz	r1, .L415
	bl	P2V_block_in_plane
	mov	r6, r0
	ldr	r0, [r4, #3272]
	lsls	r3, r5, #10
	movs	r2, #1
	mov	r1, r2
	str	r3, [r0, #4]
	bl	FlashEraseBlocks
	ldr	r2, [r4, #2540]
	ldrh	r3, [r2, r6, lsl #1]
	adds	r3, r3, #1
	strh	r3, [r2, r6, lsl #1]	@ movhi
	ldr	r3, [r4, #2612]
	adds	r3, r3, #1
	str	r3, [r4, #2612]
.L415:
	ldrh	r3, [r4, #422]
	adds	r3, r3, #1
	strh	r3, [r4, #422]	@ movhi
	ldrh	r3, [r4, #420]
	add	r2, r4, r3, lsl #1
	adds	r3, r3, #1
	ubfx	r3, r3, #0, #10
	strh	r5, [r2, #424]	@ movhi
	strh	r3, [r4, #420]	@ movhi
.L413:
	pop	{r4, r5, r6, pc}
.L423:
	.align	2
.L422:
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
	ldr	r4, .L429
	ldrh	r2, [r4, #422]
	cbz	r2, .L428
	ldrh	r3, [r4, #418]
	subs	r2, r2, #1
	ldr	r0, [r4, #3272]
	strh	r2, [r4, #422]	@ movhi
	movs	r2, #1
	add	r1, r4, r3, lsl #1
	adds	r3, r3, #1
	ubfx	r3, r3, #0, #10
	ldrh	r5, [r1, #424]
	mov	r1, r2
	strh	r3, [r4, #418]	@ movhi
	lsls	r3, r5, #10
	str	r3, [r0, #4]
	bl	FlashEraseBlocks
	ldr	r3, [r4, #2612]
	adds	r3, r3, #1
	str	r3, [r4, #2612]
.L425:
	subs	r3, r5, #1
	movw	r2, #65533
	uxth	r3, r3
	cmp	r3, r2
	bls	.L426
	ldrh	r2, [r4, #422]
	mov	r1, r5
	ldr	r0, .L429+4
	bl	sftl_printk
.L427:
	b	.L427
.L428:
	movw	r5, #65535
	b	.L425
.L426:
	mov	r0, r5
	pop	{r3, r4, r5, pc}
.L430:
	.align	2
.L429:
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
.L432:
	uxth	r5, r3
	cmp	r5, r1
	bcs	.L435
	mov	r7, r2
	adds	r3, r3, #1
	ldrh	r6, [r7]
	adds	r2, r2, #2
	cmp	r6, #0
	bne	.L432
	bl	FtlFreeSysBlkQueueOut
	subs	r3, r0, #1
	movw	r2, #65533
	uxth	r3, r3
	mov	r1, r0
	strh	r0, [r7]	@ movhi
	cmp	r3, r2
	bls	.L433
	ldr	r3, .L439
	ldr	r0, .L439+4
	ldrh	r2, [r3, #422]
	bl	sftl_printk
.L434:
	b	.L434
.L433:
	ldr	r3, [r4, #28]
	strh	r6, [r4, #2]	@ movhi
	strh	r5, [r4]	@ movhi
	adds	r3, r3, #1
	str	r3, [r4, #28]
	ldrh	r3, [r4, #8]
	adds	r3, r3, #1
	strh	r3, [r4, #8]	@ movhi
.L435:
	ldrh	r3, [r4, #10]
	cmp	r3, r5
	bhi	.L437
	movw	r2, #578
	ldr	r1, .L439+8
	ldr	r0, .L439+12
	bl	sftl_printk
.L437:
	movs	r0, #0
	pop	{r3, r4, r5, r6, r7, pc}
.L440:
	.align	2
.L439:
	.word	.LANCHOR0
	.word	.LC90
	.word	.LANCHOR1+147
	.word	.LC8
	.fnend
	.size	ftl_map_blk_alloc_new_blk, .-ftl_map_blk_alloc_new_blk
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
	ldr	r5, .L448
	mov	r4, r0
	mov	r6, r1
	movs	r1, #255
	bl	memset
	ldr	r3, [r5, #3248]
	mov	r1, r6
	mov	r0, r4
	blx	r3
	uxth	r0, r0
	cmp	r0, #50
	bls	.L442
	mov	r2, #256
	movs	r1, #255
	mov	r0, r4
	bl	memset
	movs	r0, #0
.L442:
	ldrh	r3, [r5, #14]
	cmp	r3, #4
	bne	.L447
	add	r1, r4, r0, lsl #1
	mov	r3, r4
.L444:
	cmp	r3, r1
	bne	.L445
.L447:
	pop	{r4, r5, r6, pc}
.L445:
	ldrh	r2, [r3]
	lsrs	r2, r2, #1
	strh	r2, [r3], #2	@ movhi
	b	.L444
.L449:
	.align	2
.L448:
	.word	.LANCHOR0
	.fnend
	.size	FlashGetBadBlockList, .-FlashGetBadBlockList
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
	push	{r4, lr}
	.save {r4, lr}
	mov	r4, r0
	cbz	r2, .L451
	bl	memset
.L451:
	mov	r0, r4
	pop	{r4, pc}
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
	ldr	r4, .L556
	movs	r5, #0
	movs	r7, #12
	movs	r6, #20
	ldrh	r0, [r4, #310]
	str	r3, [r4, #3280]
	strh	r3, [r4, #226]	@ movhi
	strh	r3, [r4, #224]	@ movhi
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
	ldrh	r0, [r4, #310]
	muls	r0, r7, r0
	bl	ftl_malloc
	ldrh	r3, [r4, #236]
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
	ldrh	r3, [r4, #236]
	ldrh	r6, [r4, #314]
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
	ldrh	r0, [r4, #262]
	lsls	r0, r0, #2
	bl	ftl_malloc
	ldrh	r3, [r4, #316]
	ldrh	r6, [r4, #236]
	str	r0, [r4, #3332]
	muls	r6, r3, r6
	mov	r0, r6
	bl	ftl_malloc
	str	r0, [r4, #3336]
	lsls	r0, r6, #2
	bl	ftl_malloc
	ldrh	r3, [r4, #316]
	str	r0, [r4, #3340]
	ldr	r0, [r4, #3192]
	muls	r0, r3, r0
	bl	ftl_malloc
	str	r0, [r4, #3184]
	ldrh	r0, [r4, #246]
	lsls	r0, r0, #1
	uxth	r0, r0
	strh	r0, [r4, #3344]	@ movhi
	bl	ftl_malloc
	ldrh	r3, [r4, #3344]
	str	r0, [r4, #3348]
	ldr	r0, .L556+4
	addw	r3, r3, #547
	lsrs	r3, r3, #9
	and	r0, r0, r3, lsl #9
	strh	r3, [r4, #3344]	@ movhi
	bl	ftl_malloc
	ldrh	r6, [r4, #246]
	str	r0, [r4, #3352]
	adds	r0, r0, #32
	str	r0, [r4, #2540]
	lsls	r6, r6, #1
	mov	r0, r6
	bl	ftl_malloc
	str	r0, [r4, #3356]
	mov	r0, r6
	bl	ftl_malloc
	ldr	r6, [r4, #332]
	str	r0, [r4, #76]
	lsls	r6, r6, #1
	mov	r0, r6
	bl	ftl_malloc
	str	r0, [r4, #3360]
	mov	r0, r6
	bl	ftl_malloc
	str	r0, [r4, #3364]
	ldrh	r0, [r4, #246]
	lsrs	r0, r0, #3
	adds	r0, r0, #4
	bl	ftl_malloc
	str	r0, [r4, #24]
	ldrh	r0, [r4, #324]
	lsls	r0, r0, #1
	bl	ftl_malloc
	str	r0, [r4, #352]
	ldrh	r0, [r4, #324]
	lsls	r0, r0, #1
	bl	ftl_malloc
	str	r0, [r4, #3368]
	ldrh	r0, [r4, #324]
	lsls	r0, r0, #2
	bl	ftl_malloc
	str	r0, [r4, #3372]
	ldrh	r0, [r4, #326]
	lsls	r0, r0, #2
	bl	ftl_malloc
	ldrh	r2, [r4, #326]
	mov	r1, r5
	str	r0, [r4, #3376]
	add	r5, r4, #384
	lsls	r2, r2, #2
	bl	ftl_memset
	ldrh	r0, [r4, #340]
	lsls	r0, r0, #2
	bl	ftl_malloc
	str	r0, [r4, #3380]
	ldr	r0, [r4, #332]
	lsls	r0, r0, #2
	bl	ftl_malloc
	str	r0, [r4, #3384]
	ldrh	r0, [r4, #342]
	muls	r0, r7, r0
	bl	ftl_malloc
	ldrh	r3, [r4, #342]
	str	r0, [r4, #2544]
	ldrh	r0, [r4, #314]
	muls	r0, r3, r0
	bl	ftl_malloc
	ldrh	r3, [r4, #246]
	str	r0, [r4, #3388]
	movs	r0, #6
	muls	r0, r3, r0
	bl	ftl_malloc
	str	r0, [r4, #2520]
	ldrh	r0, [r4, #302]
	ldrh	r3, [r4, #258]
	adds	r0, r0, #31
	asrs	r0, r0, #5
	strh	r0, [r4, #3392]	@ movhi
	muls	r0, r3, r0
	lsls	r0, r0, #2
	bl	ftl_malloc
	ldrh	r2, [r4, #3392]
	movs	r3, #1
	ldrh	r6, [r4, #258]
	str	r0, [r4, #384]
	lsls	r2, r2, #2
	mov	r1, r2
.L456:
	cmp	r3, r6
	bcc	.L457
	ldr	r2, .L556+8
	movs	r1, #0
	add	r3, r2, r3, lsl #2
	adds	r2, r2, #56
	adds	r3, r3, #24
.L458:
	cmp	r2, r3
	bne	.L459
	ldr	r3, [r4, #3360]
	cbnz	r3, .L460
.L462:
	ldr	r1, .L556+12
	ldr	r0, .L556+16
	bl	sftl_printk
	mov	r0, #-1
	pop	{r4, r5, r6, r7, r8, pc}
.L457:
	ldr	r0, [r4, #384]
	adds	r3, r3, #1
	add	r0, r0, r1
	add	r1, r1, r2
	str	r0, [r5, #4]!
	b	.L456
.L459:
	str	r1, [r3, #4]!
	b	.L458
.L557:
	.align	2
.L556:
	.word	.LANCHOR0
	.word	33553920
	.word	.LANCHOR0+356
	.word	.LANCHOR1+173
	.word	.LC91
.L460:
	ldr	r3, [r4, #3364]
	cmp	r3, #0
	beq	.L462
	ldr	r3, [r4, #3380]
	cmp	r3, #0
	beq	.L462
	ldr	r3, [r4, #3384]
	cmp	r3, #0
	beq	.L462
	ldr	r3, [r4, #2544]
	cmp	r3, #0
	beq	.L462
	ldr	r3, [r4, #3388]
	cmp	r3, #0
	beq	.L462
	ldr	r3, [r4, #2520]
	cmp	r3, #0
	beq	.L462
	ldr	r3, [r4, #384]
	cmp	r3, #0
	beq	.L462
	ldr	r3, [r4, #76]
	cmp	r3, #0
	beq	.L462
	ldr	r3, [r4, #3196]
	cmp	r3, #0
	beq	.L462
	ldr	r3, [r4, #3204]
	cmp	r3, #0
	beq	.L462
	ldr	r3, [r4, #3292]
	cmp	r3, #0
	beq	.L462
	ldr	r3, [r4, #3300]
	cmp	r3, #0
	beq	.L462
	ldr	r3, [r4, #3272]
	cmp	r3, #0
	beq	.L462
	ldr	r3, [r4, #3188]
	cmp	r3, #0
	beq	.L462
	ldr	r3, [r4, #3296]
	cmp	r3, #0
	beq	.L462
	ldr	r3, [r4, #3304]
	cmp	r3, #0
	beq	.L462
	ldr	r3, [r4, #3308]
	cmp	r3, #0
	beq	.L462
	ldr	r3, [r4, #3312]
	cmp	r3, #0
	beq	.L462
	ldr	r3, [r4, #3180]
	cmp	r3, #0
	beq	.L462
	ldr	r3, [r4, #3316]
	cmp	r3, #0
	beq	.L462
	ldr	r3, [r4, #3320]
	cmp	r3, #0
	beq	.L462
	ldr	r3, [r4, #3176]
	cmp	r3, #0
	beq	.L462
	ldr	r3, [r4, #3336]
	cmp	r3, #0
	beq	.L462
	ldr	r3, [r4, #3340]
	cmp	r3, #0
	beq	.L462
	ldr	r3, [r4, #3184]
	cmp	r3, #0
	beq	.L462
	ldr	r3, [r4, #2540]
	cmp	r3, #0
	beq	.L462
	ldr	r3, [r4, #3348]
	cmp	r3, #0
	beq	.L462
	ldr	r3, [r4, #352]
	cmp	r3, #0
	beq	.L462
	ldr	r3, .L558
	ldr	r2, [r3, #3368]
	cmp	r2, #0
	beq	.L462
	ldr	r2, [r3, #3372]
	cmp	r2, #0
	beq	.L462
	ldr	r3, [r3, #3376]
	cmp	r3, #0
	beq	.L462
	movs	r0, #0
	pop	{r4, r5, r6, r7, r8, pc}
.L559:
	.align	2
.L558:
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
	ldr	r6, .L566
	mov	r7, r1
	subs	r4, r5, #2
	movw	r8, #65535
	ldr	r9, .L566+4
	addw	r5, r5, #1022
	ldrh	r2, [r6, #3392]
	movs	r1, #0
	ldr	r10, .L566+8
	mov	r0, r7
	lsls	r2, r2, #2
	bl	ftl_memset
.L563:
	ldrh	r3, [r4, #2]
	cmp	r3, r8
	beq	.L560
	ldrh	r2, [r6, #302]
	cmp	r2, r3
	bhi	.L562
	movs	r2, #74
	mov	r1, r9
	mov	r0, r10
	bl	sftl_printk
.L562:
	ldrh	r3, [r4, #2]!
	movs	r2, #1
	cmp	r5, r4
	lsr	r1, r3, #5
	and	r3, r3, #31
	lsl	r3, r2, r3
	ldr	r2, [r7, r1, lsl #2]
	orr	r2, r2, r3
	str	r2, [r7, r1, lsl #2]
	bne	.L563
.L560:
	pop	{r4, r5, r6, r7, r8, r9, r10, pc}
.L567:
	.align	2
.L566:
	.word	.LANCHOR0
	.word	.LANCHOR1+184
	.word	.LC8
	.fnend
	.size	FtlBbt2Bitmap, .-FtlBbt2Bitmap
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
.L569:
	ldrh	r1, [r4, #6]
	uxth	r2, r3
	cmp	r1, r2
	bhi	.L573
	ldr	r3, .L588
	movs	r6, #0
	mov	r9, r6
	mov	fp, r6
	ldrh	r2, [r3, #308]
	ldrh	r3, [r4]
	strh	r2, [r5, r3, lsl #1]	@ movhi
	ldrh	r10, [r5]
.L574:
	ldrh	r3, [r4, #10]
	uxth	r1, r6
	cmp	r3, r1
	bhi	.L578
	mov	r0, r9
	pop	{r3, r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L573:
	uxth	r2, r3
	ldr	r1, [r6, r2, lsl #2]
	movs	r2, #0
	ubfx	r1, r1, #10, #16
.L570:
	ldrh	ip, [r4, #10]
	uxth	r0, r2
	cmp	ip, r0
	bhi	.L572
	adds	r3, r3, #1
	b	.L569
.L572:
	uxth	r0, r2
	ldrh	ip, [r7, r0, lsl #1]
	cmp	ip, r1
	bne	.L571
	cbz	r1, .L571
	ldrh	ip, [r5, r0, lsl #1]
	add	ip, ip, #1
	strh	ip, [r5, r0, lsl #1]	@ movhi
.L571:
	adds	r2, r2, #1
	b	.L570
.L578:
	uxth	r2, r6
	ldrh	r3, [r5, r2, lsl #1]
	lsl	r8, r2, #1
	cmp	r10, r3
	bls	.L575
	ldrh	r0, [r7, r2, lsl #1]
	add	r8, r8, r7
	cbnz	r0, .L576
.L577:
	adds	r6, r6, #1
	b	.L574
.L575:
	cmp	r3, #0
	bne	.L577
	ldrh	r0, [r7, r2, lsl #1]
	add	r8, r8, r7
	cmp	r0, #0
	beq	.L577
.L579:
	movs	r1, #1
	bl	FtlFreeSysBlkQueueIn
	strh	fp, [r8]	@ movhi
	ldrh	r3, [r4, #8]
	subs	r3, r3, #1
	strh	r3, [r4, #8]	@ movhi
	b	.L577
.L576:
	mov	r9, r1
	mov	r10, r3
	cmp	r3, #0
	beq	.L579
	b	.L577
.L589:
	.align	2
.L588:
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
	ldr	r4, .L593
	movs	r6, #12
	ldr	r2, [r4, #332]
	ldr	r0, [r4, #3364]
	lsls	r2, r2, #1
	bl	ftl_memset
	ldrh	r3, [r4, #314]
	movs	r1, #255
	ldrh	r2, [r4, #342]
	ldr	r0, [r4, #3388]
	muls	r2, r3, r2
	bl	ftl_memset
	movs	r2, #0
	mov	r3, r4
	movw	r1, #65535
	mov	r7, r2
.L591:
	ldrh	r4, [r3, #342]
	uxth	r0, r2
	adds	r5, r2, #1
	cmp	r4, r0
	bhi	.L592
	ldr	r2, [r3, #332]
	strh	r1, [r3, #3398]	@ movhi
	strh	r1, [r3, #3396]	@ movhi
	strh	r2, [r3, #3406]	@ movhi
	movw	r2, #61634
	strh	r2, [r3, #3400]	@ movhi
	ldrh	r2, [r3, #3440]
	strh	r2, [r3, #3404]	@ movhi
	ldrh	r2, [r3, #340]
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
.L592:
	uxth	r2, r2
	ldr	r0, [r3, #2544]
	mul	r4, r6, r2
	add	ip, r0, r4
	str	r7, [ip, #4]
	strh	r1, [r0, r4]	@ movhi
	ldr	r0, [r3, #2544]
	add	r0, r0, r4
	ldrh	r4, [r3, #314]
	muls	r2, r4, r2
	ldr	r4, [r3, #3388]
	bic	r2, r2, #3
	add	r2, r2, r4
	str	r2, [r0, #8]
	mov	r2, r5
	b	.L591
.L594:
	.align	2
.L593:
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
	ldr	r4, .L596
	movw	r3, #65535
	mov	r1, r5
	ldrh	r2, [r4, #324]
	strh	r3, [r4, #3442]	@ movhi
	mov	r3, #-1
	ldr	r0, [r4, #352]
	str	r3, [r4, #3452]
	lsls	r2, r2, #1
	str	r5, [r4, #3444]
	str	r5, [r4, #3448]
	strh	r5, [r4, #348]	@ movhi
	bl	ftl_memset
	ldrh	r2, [r4, #246]
	mov	r1, r5
	ldr	r0, [r4, #2540]
	lsls	r2, r2, #1
	bl	ftl_memset
	ldrh	r2, [r4, #246]
	mov	r1, r5
	ldr	r0, [r4, #3348]
	lsls	r2, r2, #1
	bl	ftl_memset
	addw	r0, r4, #2472
	movs	r1, #48
	bl	__memzero
	addw	r0, r4, #2644
	mov	r1, #512
	bl	__memzero
	bl	FtlGcBufInit
	bl	FtlL2PDataInit
	mov	r0, r5
	pop	{r3, r4, r5, pc}
.L597:
	.align	2
.L596:
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
	ldr	r4, .L609
	movs	r2, #6
	mov	r9, r5
	mov	r6, r5
	movw	fp, #65535
	.pad #20
	sub	sp, sp, #20
	ldrh	r3, [r4, #246]
	add	r10, r4, #264
	movs	r1, #0
	ldr	r0, [r4, #2520]
	muls	r2, r3, r2
	bl	ftl_memset
	str	r5, [r4, #2536]
	str	r5, [r4, #2524]
	str	r5, [r4, #2528]
	strh	r5, [r4, #2532]	@ movhi
	strh	r5, [r4, #228]	@ movhi
.L599:
	ldrh	r3, [r4, #244]
	uxth	r8, r5
	cmp	r8, r3
	bcs	.L606
	ldrh	r3, [r4, #306]
	ldrh	r2, [r4, #236]
	str	r3, [sp, #4]
	movs	r3, #0
	mov	r7, r3
	b	.L607
.L601:
	ldrb	r0, [r10, r3]	@ zero_extendqisi2
	mov	r1, r8
	str	r2, [sp, #12]
	str	r3, [sp, #8]
	bl	V2P_block
	bl	FtlBbmIsBadBlock
	ldr	r3, [sp, #8]
	ldr	r2, [sp, #12]
	cbnz	r0, .L600
	ldr	r1, [sp, #4]
	add	r7, r7, r1
	uxth	r7, r7
.L600:
	adds	r3, r3, #1
.L607:
	uxth	r1, r3
	cmp	r2, r1
	bhi	.L601
	uxth	r3, r5
	cbz	r7, .L602
	mov	r1, r7
	str	r3, [sp, #4]
	mov	r0, #32768
	bl	__aeabi_idiv
	ldr	r3, [sp, #4]
	uxth	r7, r0
.L603:
	ldr	r1, [r4, #2520]
	movs	r2, #6
	mla	r2, r2, r3, r1
	strh	r7, [r2, #4]	@ movhi
	ldrh	r2, [r4, #28]
	cmp	r2, r8
	beq	.L604
	ldrh	r2, [r4, #80]
	cmp	r2, r8
	beq	.L604
	ldrh	r2, [r4, #128]
	cmp	r2, r8
	beq	.L604
	ldr	r2, [r4, #76]
	ldrh	r3, [r2, r3, lsl #1]
	cbnz	r3, .L605
	add	r9, r9, #1
	mov	r0, r8
	uxth	r9, r9
	bl	INSERT_FREE_LIST
.L604:
	adds	r5, r5, #1
	b	.L599
.L602:
	ldr	r2, [r4, #76]
	strh	fp, [r2, r3, lsl #1]	@ movhi
	b	.L603
.L605:
	adds	r6, r6, #1
	mov	r0, r8
	uxth	r6, r6
	bl	INSERT_DATA_LIST
	b	.L604
.L606:
	strh	r6, [r4, #2532]	@ movhi
	add	r6, r6, r9
	cmp	r6, r3
	strh	r9, [r4, #228]	@ movhi
	ble	.L608
	movw	r2, #2210
	ldr	r1, .L609+4
	ldr	r0, .L609+8
	bl	sftl_printk
.L608:
	movs	r0, #0
	add	sp, sp, #20
	@ sp needed
	pop	{r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L610:
	.align	2
.L609:
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
	ldr	r4, .L612
	movs	r1, #255
	ldrh	r2, [r4, #310]
	ldr	r0, [r4, #3196]
	strh	r3, [r4, #3200]	@ movhi
	strh	r3, [r4, #3208]	@ movhi
	lsls	r2, r2, #1
	bl	ftl_memset
	ldrh	r3, [r4, #310]
	movs	r2, #12
	ldr	r0, [r4, #3204]
	movs	r1, #255
	muls	r2, r3, r2
	bl	ftl_memset
	pop	{r4, lr}
	b	FtlGcBufInit
.L613:
	.align	2
.L612:
	.word	.LANCHOR0
	.fnend
	.size	FtlGcPageVarInit, .-FtlGcPageVarInit
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
	@ args = 0, pretend = 0, frame = 16
	@ frame_needed = 0, uses_anonymous_args = 0
	push	{r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.save {r4, r5, r6, r7, r8, r9, r10, fp, lr}
	mov	r9, r1
	ldr	r5, .L655
	mov	r4, r0
	mov	r8, #0
	.pad #20
	sub	sp, sp, #20
	ldr	r10, .L655+16
	ldr	fp, .L655+20
	ldrh	r3, [r5, #12]
	str	r3, [sp, #4]
.L616:
	cmp	r8, r9
	bne	.L630
	movs	r0, #0
	add	sp, sp, #20
	@ sp needed
	pop	{r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L630:
	ldr	r3, [r4, #8]
	cbz	r3, .L617
	ldr	r3, [r4, #12]
	cbnz	r3, .L618
.L617:
	movs	r2, #96
	ldr	r1, .L655+4
	mov	r0, r10
	bl	sftl_printk
.L618:
	add	r2, sp, #8
	add	r1, sp, #12
	mov	r0, r4
	bl	l2p_addr_tran
	ldr	r0, [sp, #8]
	cmp	r0, #3
	bls	.L619
	mov	r3, #-1
	str	r3, [r4]
.L620:
	add	r8, r8, #1
	adds	r4, r4, #20
	b	.L616
.L619:
	ldr	r6, [r4, #8]
	uxtb	r0, r0
	ldr	r3, [r4, #12]
	ldr	r1, [sp, #12]
	lsls	r7, r6, #26
	ldr	r7, [r5, #3260]
	it	ne
	ldrne	r6, [r5, #3324]
	mov	r2, r6
	blx	r7
	str	r0, [r4]
	ldrh	r3, [r5, #14]
	cmp	r3, #4
	bne	.L623
	ldr	r0, [sp, #4]
	add	r2, r6, #2048
	ldr	r3, [r4, #12]
	ldr	r1, [sp, #12]
	ldr	r7, [r5, #3260]
	adds	r3, r3, #8
	add	r1, r1, r0
	ldrb	r0, [sp, #8]	@ zero_extendqisi2
	blx	r7
	adds	r1, r0, #1
	beq	.L624
	ldr	r3, [r4, #12]
	ldr	r2, [r3, #12]
	adds	r2, r2, #1
	bne	.L625
	ldr	r2, [r3, #8]
	adds	r2, r2, #1
	bne	.L625
	ldr	r3, [r3]
	adds	r3, r3, #1
	beq	.L625
.L624:
	mov	r3, #-1
	str	r3, [r4]
.L625:
	ldr	r3, [r4]
	adds	r3, r3, #1
	beq	.L626
	cmp	r0, #256
	it	eq
	streq	r0, [r4]
.L626:
	ldr	r3, [r4]
	adds	r2, r3, #1
	beq	.L627
	cmp	r3, #256
	bne	.L623
.L627:
	ldr	r1, [r4, #4]
	mov	r0, fp
	ldr	r2, [sp, #12]
	bl	sftl_printk
	ldr	r1, [r4, #8]
	cbz	r1, .L629
	movs	r3, #4
	ldr	r0, .L655+8
	mov	r2, r3
	bl	rknand_print_hex
.L629:
	ldr	r1, [r4, #12]
	cbz	r1, .L623
	movs	r3, #4
	ldr	r0, .L655+12
	mov	r2, r3
	bl	rknand_print_hex
.L623:
	ldr	r3, [r5, #3324]
	cmp	r6, r3
	bne	.L620
	ldr	r0, [r4, #8]
	cmp	r6, r0
	beq	.L620
	ldrh	r2, [r5, #262]
	mov	r1, r6
	lsls	r2, r2, #9
	bl	ftl_memcpy
	b	.L620
.L656:
	.align	2
.L655:
	.word	.LANCHOR0
	.word	.LANCHOR1+216
	.word	.LC93
	.word	.LC94
	.word	.LC8
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
	ldr	r4, .L667
	movw	r10, #61664
	ldr	r3, [r4, #3304]
	add	r7, r4, #366
	ldr	r8, [r4, #3336]
	add	r9, r4, #3456
	str	r3, [r4, #3464]
	str	r8, [r4, #3468]
.L658:
	ldrh	r3, [r4, #258]
	cmp	r6, r3
	bcc	.L663
	movs	r0, #0
	pop	{r4, r5, r6, r7, r8, r9, r10, pc}
.L663:
	ldrh	r5, [r4, #302]
	movw	r3, #65535
	strh	r3, [r7, #2]!	@ movhi
.L660:
	ldrh	r3, [r4, #302]
	subs	r5, r5, #1
	uxth	r5, r5
	sub	r2, r3, #16
	cmp	r5, r2
	ble	.L661
	mla	r3, r6, r3, r5
	movs	r2, #1
	mov	r1, r2
	mov	r0, r9
	lsls	r3, r3, #10
	str	r3, [r4, #3460]
	bl	FlashReadPages
	ldr	r3, [r4, #3456]
	adds	r3, r3, #1
	beq	.L660
	ldrh	r3, [r8]
	cmp	r3, r10
	bne	.L660
	strh	r5, [r7]	@ movhi
.L661:
	adds	r6, r6, #1
	b	.L658
.L668:
	.align	2
.L667:
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
	ldr	r3, .L680
	cmp	r1, #1
	push	{r4, r5, r6, r7, r8, lr}
	.save {r4, r5, r6, r7, r8, lr}
	lsl	r6, r0, #10
	.pad #88
	sub	sp, sp, #88
	mov	r2, r1
	mov	r7, r1
	it	eq
	ldrheq	r5, [r3, #308]
	mov	r1, #1
	it	ne
	ldrhne	r5, [r3, #306]
	add	r0, sp, #4
	ldr	r3, [r3, #3328]
	subs	r5, r5, #1
	sxth	r5, r5
	str	r3, [sp, #12]
	add	r3, sp, #24
	str	r3, [sp, #16]
	orr	r3, r5, r6
	str	r3, [sp, #8]
	bl	FlashReadPages
	ldr	r3, [sp, #24]
	adds	r3, r3, #1
	bne	.L672
	mov	r8, #0
.L673:
	cmp	r8, r5
	ble	.L676
.L672:
	mov	r0, r5
	add	sp, sp, #88
	@ sp needed
	pop	{r4, r5, r6, r7, r8, pc}
.L676:
	add	r3, r8, r5
	mov	r2, r7
	add	r3, r3, r3, lsr #31
	movs	r1, #1
	add	r0, sp, #4
	asrs	r4, r3, #1
	sxth	r3, r4
	orrs	r3, r3, r6
	str	r3, [sp, #8]
	bl	FlashReadPages
	ldr	r3, [sp, #24]
	adds	r3, r3, #1
	bne	.L674
	ldr	r3, [sp, #28]
	adds	r3, r3, #1
	bne	.L674
	ldr	r3, [sp, #4]
	adds	r3, r3, #1
	beq	.L674
	subs	r4, r4, #1
	sxth	r5, r4
	b	.L673
.L674:
	adds	r4, r4, #1
	sxth	r8, r4
	b	.L673
.L681:
	.align	2
.L680:
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
	ldr	r4, .L764
	mov	r1, r5
	.pad #20
	sub	sp, sp, #20
	ldr	r2, [r4, #332]
	ldr	r0, [r4, #3384]
	strh	r5, [r4, #3440]	@ movhi
	strh	r5, [r4, #348]	@ movhi
	lsls	r2, r2, #2
	bl	ftl_memset
	ldr	r2, [r4, #332]
	mov	r1, r5
	ldr	r0, [r4, #3360]
	lsls	r2, r2, #1
	bl	ftl_memset
	ldrh	r2, [r4, #324]
	mov	r1, r5
	ldr	r0, [r4, #3372]
	lsls	r2, r2, #2
	bl	ftl_memset
	ldrh	r2, [r4, #324]
	mov	r1, r5
	ldr	r0, [r4, #352]
	lsls	r2, r2, #1
	bl	ftl_memset
	movs	r2, #16
	movs	r1, #255
	addw	r0, r4, #2628
	bl	memset
	ldrh	r8, [r4, #244]
.L683:
	ldrh	r3, [r4, #246]
	cmp	r3, r8
	bls	.L725
	movs	r5, #0
	ldrh	fp, [r4, #236]
	ldr	r9, [r4, #3292]
	mov	r7, r5
	ldr	r3, [r4, #3180]
	mov	r10, #20
	ldr	r2, [r4, #3184]
	ldrh	r6, [r4, #316]
	b	.L726
.L686:
	str	r3, [sp, #8]
	mov	r1, r8
	ldr	r3, .L764+4
	str	r2, [sp, #12]
	ldrb	r0, [r3, r5]	@ zero_extendqisi2
	bl	V2P_block
	str	r0, [sp, #4]
	bl	FtlBbmIsBadBlock
	ldr	r3, [sp, #8]
	ldr	r2, [sp, #12]
	cbnz	r0, .L684
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
.L684:
	adds	r5, r5, #1
.L726:
	uxth	r1, r5
	cmp	fp, r1
	bhi	.L686
	cbnz	r7, .L687
.L724:
	add	r8, r8, #1
	uxth	r8, r8
	b	.L683
.L687:
	mov	r0, r9
	mov	r9, #0
	movs	r2, #1
	mov	r1, r7
	bl	FlashReadPages
.L688:
	uxth	r3, r9
	cmp	r7, r3
	bls	.L724
	movs	r3, #20
	mul	r10, r3, r9
	ldr	r3, [r4, #3292]
	add	r2, r3, r10
	ldr	r3, [r3, r10]
	ldr	r5, [r2, #4]
	ldr	r6, [r2, #12]
	adds	r3, r3, #1
	ubfx	r5, r5, #10, #16
	bne	.L691
	mov	fp, #16
	movw	r3, #65535
.L693:
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
	bne	.L690
	ldr	r3, [r4, #3292]
	mov	r2, #-1
	str	r2, [r3, r10]
	ldr	r3, [r4, #3292]
	ldr	r3, [r3, r10]
	cmp	r3, r2
	bne	.L691
.L692:
	movs	r1, #1
	b	.L763
.L690:
	ldr	r2, [r4, #3292]
	ldr	r2, [r2, r10]
	adds	r2, r2, #1
	bne	.L691
	add	fp, fp, #-1
	uxth	fp, fp
	cmp	fp, #0
	bne	.L693
	b	.L692
.L691:
	ldr	r2, [r4, #2592]
	ldr	r3, [r6, #4]
	adds	r1, r2, #1
	beq	.L694
	cmp	r2, r3
	bhi	.L695
.L694:
	adds	r2, r3, #1
	itt	ne
	addne	r2, r3, #1
	strne	r2, [r4, #2592]
.L695:
	ldrh	r2, [r6]
	movw	r1, #61604
	cmp	r2, r1
	beq	.L697
	bhi	.L698
	movw	r3, #61574
	cmp	r2, r3
	beq	.L699
.L696:
	add	r9, r9, #1
	b	.L688
.L698:
	movw	r3, #61634
	cmp	r2, r3
	beq	.L700
	movw	r3, #65535
	cmp	r2, r3
	bne	.L696
	movs	r1, #0
.L763:
	mov	r0, r5
	bl	FtlFreeSysBlkQueueIn
	b	.L696
.L700:
	ldrh	r2, [r4, #3440]
	ldr	r3, [r4, #332]
	cmp	r2, r3
	bls	.L702
	movw	r2, #1225
	ldr	r1, .L764+8
	ldr	r0, .L764+12
	bl	sftl_printk
.L702:
	ldr	r1, [r4, #332]
	ldrh	r0, [r4, #3440]
	ldr	r10, [r4, #3384]
	uxth	ip, r1
	add	r3, ip, #-1
	sub	ip, ip, r0
	add	ip, ip, #-1
	sxth	r3, r3
	sxth	ip, ip
.L703:
	cmp	r3, ip
	bgt	.L709
	cmp	r3, #0
	bge	.L741
	b	.L696
.L709:
	ldr	r2, [r6, #4]
	lsl	fp, r3, #2
	mov	lr, r2
	ldr	r2, [r10, r3, lsl #2]
	cmp	lr, r2
	bls	.L704
	ldr	r2, [r10]
	cbnz	r2, .L705
	cmp	r1, r0
	itt	ne
	addne	r0, r0, #1
	strhne	r0, [r4, #3440]	@ movhi
.L705:
	uxth	ip, r3
	movs	r2, #0
.L706:
	uxth	r0, r2
	sxth	r1, r2
	cmp	ip, r0
	bhi	.L707
	ldr	r2, [r4, #3384]
	cmp	r3, #0
	ldr	r1, [r6, #4]
	str	r1, [r2, fp]
	ldr	r2, [r4, #3360]
	strh	r5, [r2, r3, lsl #1]	@ movhi
	blt	.L696
	ldrh	r0, [r4, #3440]
	ldr	r2, [r4, #332]
	subs	r2, r2, r0
	subs	r2, r2, #1
	sxth	r2, r2
	cmp	r3, r2
	bgt	.L696
.L741:
	ldr	r2, [r4, #3384]
	adds	r0, r0, #1
	ldr	r1, [r6, #4]
	strh	r0, [r4, #3440]	@ movhi
	str	r1, [r2, r3, lsl #2]
	ldr	r2, [r4, #3360]
.L761:
	strh	r5, [r2, r3, lsl #1]	@ movhi
	b	.L696
.L707:
	ldr	r0, [r4, #3384]
	adds	r2, r2, #1
	add	lr, r0, r1, lsl #2
	ldr	lr, [lr, #4]
	str	lr, [r0, r1, lsl #2]
	ldr	r0, [r4, #3360]
	add	lr, r0, r1, lsl #1
	ldrh	lr, [lr, #2]
	strh	lr, [r0, r1, lsl #1]	@ movhi
	b	.L706
.L704:
	subs	r3, r3, #1
	sxth	r3, r3
	b	.L703
.L765:
	.align	2
.L764:
	.word	.LANCHOR0
	.word	.LANCHOR0+264
	.word	.LANCHOR1+231
	.word	.LC8
.L699:
	ldrh	r2, [r4, #348]
	ldrh	r3, [r4, #324]
	cmp	r2, r3
	bls	.L712
	movw	r2, #1266
	ldr	r1, .L766
	ldr	r0, .L766+4
	bl	sftl_printk
.L712:
	ldrh	lr, [r4, #324]
	ldrh	r0, [r4, #348]
	ldr	r10, [r4, #3372]
	add	ip, lr, #-1
	sxth	r3, ip
	sub	ip, ip, r0
.L713:
	cmp	r3, ip
	ble	.L718
	ldr	r2, [r6, #4]
	lsl	fp, r3, #2
	ldr	r1, [r10, r3, lsl #2]
	cmp	r2, r1
	bls	.L714
	ldr	r2, [r10]
	cbnz	r2, .L715
	cmp	lr, r0
	itt	ne
	addne	r0, r0, #1
	strhne	r0, [r4, #348]	@ movhi
.L715:
	uxth	ip, r3
	movs	r2, #0
.L716:
	uxth	r0, r2
	sxth	r1, r2
	cmp	ip, r0
	bhi	.L717
	ldr	r2, [r4, #3372]
	ldr	r1, [r6, #4]
	str	r1, [r2, fp]
	ldr	r2, [r4, #352]
	strh	r5, [r2, r3, lsl #1]	@ movhi
.L718:
	cmp	r3, #0
	blt	.L696
	ldrh	r2, [r4, #324]
	ldrh	r1, [r4, #348]
	subs	r2, r2, #1
	subs	r2, r2, r1
	sxth	r2, r2
	cmp	r3, r2
	bgt	.L696
	adds	r1, r1, #1
	ldr	r2, [r4, #3372]
	strh	r1, [r4, #348]	@ movhi
	ldr	r1, [r6, #4]
	str	r1, [r2, r3, lsl #2]
	ldr	r2, [r4, #352]
	b	.L761
.L717:
	ldr	r0, [r4, #3372]
	adds	r2, r2, #1
	add	lr, r0, r1, lsl #2
	ldr	lr, [lr, #4]
	str	lr, [r0, r1, lsl #2]
	ldr	r0, [r4, #352]
	add	lr, r0, r1, lsl #1
	ldrh	lr, [lr, #2]
	strh	lr, [r0, r1, lsl #1]	@ movhi
	b	.L716
.L714:
	subs	r3, r3, #1
	sxth	r3, r3
	b	.L713
.L697:
	ldrh	r1, [r4, #2628]
	movw	r2, #65535
	cmp	r1, r2
	bne	.L720
	strh	r5, [r4, #2628]	@ movhi
.L762:
	str	r3, [r4, #2636]
	b	.L696
.L720:
	ldrh	r0, [r4, #2632]
	cmp	r0, r2
	beq	.L721
	movs	r1, #1
	bl	FtlFreeSysBlkQueueIn
.L721:
	ldr	r2, [r4, #2636]
	ldr	r3, [r6, #4]
	cmp	r2, r3
	bcs	.L722
	ldrh	r3, [r4, #2628]
	strh	r5, [r4, #2628]	@ movhi
	strh	r3, [r4, #2632]	@ movhi
	ldr	r3, [r6, #4]
	b	.L762
.L722:
	strh	r5, [r4, #2632]	@ movhi
	b	.L696
.L725:
	ldr	r1, [r4, #3360]
	ldrh	r3, [r1]
	cbz	r3, .L727
.L730:
	ldr	r1, [r4, #352]
	ldrh	r2, [r1]
	cmp	r2, #0
	beq	.L728
.L729:
	ldrh	r2, [r4, #3440]
	ldr	r3, [r4, #332]
	cmp	r2, r3
	bls	.L760
	movw	r2, #1391
	ldr	r1, .L766
	ldr	r0, .L766+4
	bl	sftl_printk
.L760:
	movs	r0, #0
	add	sp, sp, #20
	@ sp needed
	pop	{r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L727:
	ldrh	r2, [r4, #3440]
	cmp	r2, #0
	beq	.L730
	ldr	r0, [r4, #332]
.L731:
	sxth	r2, r3
	cmp	r2, r0
	bcs	.L730
	ldrh	r5, [r1, r2, lsl #1]
	adds	r3, r3, #1
	cmp	r5, #0
	beq	.L731
	mov	r3, r2
	movs	r5, #0
.L732:
	ldr	r1, [r4, #332]
	cmp	r3, r1
	bcs	.L730
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
	b	.L732
.L728:
	ldrh	r3, [r4, #348]
	cmp	r3, #0
	beq	.L729
	ldrh	r0, [r4, #324]
.L737:
	sxth	r3, r2
	cmp	r3, r0
	mov	r5, r3
	bge	.L729
	ldrh	r6, [r1, r3, lsl #1]
	adds	r2, r2, #1
	cmp	r6, #0
	beq	.L737
	movs	r0, #0
.L738:
	ldrh	r2, [r4, #324]
	cmp	r3, r2
	bge	.L729
	ldr	r2, [r4, #352]
	subs	r1, r3, r5
	ldrh	r6, [r2, r3, lsl #1]
	strh	r6, [r2, r1, lsl #1]	@ movhi
	ldr	r2, [r4, #3372]
	ldr	r6, [r2, r3, lsl #2]
	str	r6, [r2, r1, lsl #2]
	ldr	r2, [r4, #352]
	strh	r0, [r2, r3, lsl #1]	@ movhi
	adds	r3, r3, #1
	sxth	r3, r3
	b	.L738
.L767:
	.align	2
.L766:
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
	ldr	r4, .L797
	ldr	r3, [r4, #3304]
	add	r7, r4, #3456
	ldr	r6, [r4, #3336]
	str	r3, [r4, #3464]
	str	r6, [r4, #3468]
	bl	FtlBbtMemInit
	ldrh	r5, [r4, #302]
	subs	r5, r5, #1
	uxth	r5, r5
.L769:
	ldrh	r3, [r4, #302]
	subs	r3, r3, #16
	cmp	r5, r3
	ble	.L772
	lsls	r3, r5, #10
	movs	r2, #1
	mov	r1, r2
	mov	r0, r7
	str	r3, [r4, #3460]
	bl	FlashReadPages
	ldr	r3, [r4, #3456]
	adds	r3, r3, #1
	bne	.L770
	ldr	r3, [r4, #3460]
	movs	r2, #1
	mov	r1, r2
	mov	r0, r7
	adds	r3, r3, #1
	str	r3, [r4, #3460]
	bl	FlashReadPages
.L770:
	ldr	r3, [r4, #3456]
	adds	r3, r3, #1
	beq	.L771
	ldrh	r3, [r6]
	cmp	r3, r8
	bne	.L771
	ldr	r3, [r6, #4]
	strh	r5, [r4, #356]	@ movhi
	str	r3, [r4, #364]
	ldrh	r3, [r6, #8]
	strh	r3, [r4, #360]	@ movhi
.L772:
	ldrh	r3, [r4, #356]
	movw	r2, #65535
	cmp	r3, r2
	beq	.L786
	ldrh	r3, [r4, #360]
	cmp	r3, r2
	beq	.L776
	lsls	r3, r3, #10
	movs	r2, #1
	mov	r1, r2
	ldr	r0, .L797+4
	str	r3, [r4, #3460]
	bl	FlashReadPages
	ldr	r3, [r4, #3456]
	adds	r3, r3, #1
	beq	.L776
	ldrh	r2, [r6]
	movw	r3, #61649
	cmp	r2, r3
	bne	.L776
	ldr	r3, [r6, #4]
	ldr	r2, [r4, #364]
	cmp	r3, r2
	bls	.L776
	ldrh	r2, [r4, #360]
	str	r3, [r4, #364]
	ldrh	r3, [r6, #8]
	strh	r2, [r4, #356]	@ movhi
	strh	r3, [r4, #360]	@ movhi
.L776:
	ldr	r7, .L797+4
	movs	r1, #1
	ldrh	r0, [r4, #356]
	movw	r8, #61649
	bl	FtlGetLastWrittenPage
	sxth	r5, r0
	adds	r0, r0, #1
	strh	r0, [r4, #358]	@ movhi
.L778:
	cmp	r5, #0
	bge	.L781
	movs	r2, #251
	ldr	r1, .L797+8
	ldr	r0, .L797+12
	bl	sftl_printk
.L780:
	ldrh	r3, [r6, #10]
	ldrh	r0, [r6, #12]
	strh	r3, [r4, #362]	@ movhi
	movw	r3, #65535
	cmp	r0, r3
	beq	.L783
	ldr	r2, [r4, #232]
	cmp	r0, r2
	beq	.L783
	ldrh	r3, [r4, #246]
	lsrs	r3, r3, #2
	cmp	r2, r3
	bcs	.L783
	cmp	r0, r3
	bcs	.L783
	bl	FtlSysBlkNumInit
.L783:
	ldr	r6, .L797+16
	movs	r5, #0
.L784:
	ldrh	r3, [r4, #258]
	cmp	r5, r3
	bcc	.L785
	movs	r0, #0
	pop	{r4, r5, r6, r7, r8, pc}
.L771:
	subs	r5, r5, #1
	uxth	r5, r5
	b	.L769
.L781:
	ldrh	r3, [r4, #356]
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
	beq	.L779
	ldrh	r3, [r6]
	cmp	r3, r8
	beq	.L780
.L779:
	subs	r5, r5, #1
	sxth	r5, r5
	b	.L778
.L785:
	ldrh	r2, [r4, #3392]
	ldr	r1, [r4, #3464]
	ldr	r0, [r6, #4]!
	lsls	r2, r2, #2
	mla	r1, r5, r2, r1
	adds	r5, r5, #1
	bl	ftl_memcpy
	b	.L784
.L786:
	mov	r0, #-1
	pop	{r4, r5, r6, r7, r8, pc}
.L798:
	.align	2
.L797:
	.word	.LANCHOR0
	.word	.LANCHOR0+3456
	.word	.LANCHOR1+245
	.word	.LC8
	.word	.LANCHOR0+380
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
	push	{r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.save {r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.pad #52
	sub	sp, sp, #52
	ldr	r6, .L838
	mov	fp, r1
	mov	r4, r0
	mov	r5, r0
	str	r3, [sp, #12]
	mov	r10, #0
	ldrh	r3, [r6, #12]
	mov	r8, r6
	str	r2, [sp, #8]
	str	r3, [sp]
	lsls	r3, r3, #3
	str	r3, [sp, #4]
.L800:
	cmp	r10, fp
	bne	.L814
	ldr	r3, [sp, #12]
	cmp	r3, #0
	beq	.L808
	movs	r5, #0
	ldr	r7, .L838+4
	ldr	r8, .L838+28
	ldr	r9, .L838+32
	b	.L815
.L814:
	ldr	r3, [r5, #8]
	cbz	r3, .L801
	ldr	r3, [r5, #12]
	cbnz	r3, .L802
.L801:
	movs	r2, #148
	ldr	r1, .L838+8
	ldr	r0, .L838+12
	bl	sftl_printk
.L802:
	add	r2, sp, #20
	add	r1, sp, #24
	mov	r0, r5
	bl	l2p_addr_tran
	ldr	r7, [sp, #20]
	cmp	r7, #3
	bls	.L803
.L837:
	mov	r3, #-1
	str	r3, [r5]
	b	.L804
.L803:
	cbnz	r7, .L805
	ldr	r3, [sp, #24]
	ldr	r2, [sp, #4]
	cmp	r2, r3
	bls	.L805
	ldr	r5, .L838+8
	ldr	r6, .L838+16
	ldr	r8, .L838+36
	b	.L836
.L807:
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
	ldr	r0, .L838+20
	bl	rknand_print_hex
.L836:
	cmp	r7, fp
	add	r4, r4, #20
	bne	.L807
	bl	dump_stack
.L808:
	movs	r0, #0
	add	sp, sp, #52
	@ sp needed
	pop	{r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L805:
	ldr	r1, [r5, #8]
	lsls	r3, r1, #26
	beq	.L822
	ldr	r9, [r8, #3324]
	cmp	r1, r9
	beq	.L809
	ldrh	r2, [r8, #262]
	mov	r0, r9
	lsls	r2, r2, #9
	bl	ftl_memcpy
.L809:
	ldr	r3, [r5, #12]
	mov	r2, r9
	ldr	r1, [sp, #24]
	ldrb	r0, [sp, #20]	@ zero_extendqisi2
	ldr	r7, [r8, #3256]
	blx	r7
	cbnz	r0, .L810
	str	r0, [r5]
.L811:
	ldrh	r3, [r8, #14]
	cmp	r3, #4
	bne	.L804
	ldr	r0, [sp]
	add	r2, r9, #2048
	ldr	r3, [r5, #12]
	ldr	r1, [sp, #24]
	ldr	r7, [r8, #3256]
	adds	r3, r3, #8
	add	r1, r1, r0
	ldrb	r0, [sp, #20]	@ zero_extendqisi2
	blx	r7
	cmp	r0, #0
	bne	.L837
.L804:
	add	r10, r10, #1
	adds	r5, r5, #20
	b	.L800
.L822:
	mov	r9, r1
	b	.L809
.L810:
	mov	r3, #-1
	str	r3, [r5]
	b	.L811
.L820:
	ldr	r2, [r6, #3324]
	movs	r3, #0
	movs	r1, #1
	add	r0, sp, #28
	str	r3, [r2]
	ldr	r2, [r6, #3332]
	str	r3, [r2]
	ldr	r3, [r4, #4]
	ldr	r2, [sp, #8]
	str	r3, [sp, #32]
	ldr	r3, [r6, #3324]
	str	r3, [sp, #36]
	ldr	r3, [r6, #3332]
	str	r3, [sp, #40]
	bl	FlashReadPages
	ldr	fp, [sp, #28]
	cmp	fp, #-1
	bne	.L816
	ldr	r1, [r4, #4]
	mov	r0, r7
	bl	sftl_printk
	str	fp, [r4]
.L816:
	ldr	fp, [sp, #28]
	cmp	fp, #256
	bne	.L817
	ldr	r1, [r4, #4]
	mov	r0, r8
	bl	sftl_printk
	str	fp, [r4]
.L817:
	ldr	r3, [r4, #12]
	cbz	r3, .L818
	ldr	r2, [r3]
	ldr	r3, [r6, #3332]
	ldr	r3, [r3]
	cmp	r2, r3
	beq	.L818
	ldr	r1, [r4, #4]
	mov	r0, r9
	bl	sftl_printk
	mov	r3, #-1
	str	r3, [r4]
.L818:
	ldr	r3, [r4, #8]
	cbz	r3, .L819
	ldr	r2, [r3]
	ldr	r3, [r6, #3324]
	ldr	r3, [r3]
	cmp	r2, r3
	beq	.L819
	ldr	r1, [r4, #4]
	ldr	r0, .L838+24
	bl	sftl_printk
	mov	r3, #-1
	str	r3, [r4]
.L819:
	adds	r5, r5, #1
	adds	r4, r4, #20
.L815:
	cmp	r10, r5
	bne	.L820
	b	.L808
.L839:
	.align	2
.L838:
	.word	.LANCHOR0
	.word	.LC95
	.word	.LANCHOR1+256
	.word	.LC8
	.word	.LC86
	.word	.LC88
	.word	.LC98
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
	ldr	r4, .L885
	mov	r7, r1
	mov	r6, r10
	mov	r5, r10
	mov	r9, #20
	.pad #20
	sub	sp, sp, #20
	mov	fp, r4
	str	r0, [sp, #4]
	str	r0, [r4, #3288]
.L841:
	ldrh	r1, [r4, #236]
	uxth	r3, r10
	cmp	r1, r3
	bhi	.L846
	cmp	r6, #0
	beq	.L840
	mov	r8, #0
	mov	r9, #20
	mov	r2, r6
	movs	r1, #0
	ldr	r0, [r4, #3272]
	bl	FlashEraseBlocks
.L849:
	uxth	r3, r8
	cmp	r6, r3
	bhi	.L851
	cmp	r7, #0
	beq	.L868
	ldrh	r3, [r4, #308]
	mov	r10, #1
	str	r3, [sp, #8]
.L852:
	mov	r8, #0
	mov	fp, #20
.L862:
	mov	r9, #0
	mov	r6, r9
.L853:
	ldrh	r1, [r4, #236]
	uxth	r3, r9
	cmp	r1, r3
	bhi	.L857
	cbz	r6, .L840
	mov	r9, #0
	movs	r3, #1
	mov	r2, r10
	mov	r1, r6
	ldr	r0, [r4, #3272]
	bl	FlashProgPages
.L859:
	uxth	r3, r9
	cmp	r6, r3
	bhi	.L861
	add	r8, r8, #1
	ldr	r2, [sp, #8]
	uxth	r3, r8
	cmp	r2, r3
	bhi	.L862
	mov	r8, #0
	mov	r9, #20
.L863:
	uxth	r3, r8
	cmp	r6, r3
	bhi	.L865
	ldr	r3, [sp, #4]
	cmp	r3, #63
	bls	.L866
	cbz	r7, .L840
.L866:
	mov	r2, r6
	mov	r1, r10
	ldr	r0, [r4, #3272]
	bl	FlashEraseBlocks
.L840:
	mov	r0, r5
	add	sp, sp, #20
	@ sp needed
	pop	{r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L846:
	uxth	r3, r10
	ldr	r0, [r4, #3272]
	movs	r2, #0
	mul	r1, r9, r3
	add	r3, r3, r4
	str	r2, [r0, r1]
	ldr	r1, [sp, #4]
	ldrb	r0, [r3, #264]	@ zero_extendqisi2
	bl	V2P_block
	mov	r8, r0
	cbz	r7, .L842
	bl	IsBlkInVendorPart
	cbnz	r0, .L843
.L842:
	mov	r0, r8
	bl	FtlBbmIsBadBlock
	cbnz	r0, .L844
	ldr	r1, [r4, #3272]
	lsl	r3, r8, #10
	ldr	r0, [fp, #3340]
	mla	r1, r9, r6, r1
	str	r3, [r1, #4]
	ldr	r3, [r4, #3320]
	str	r3, [r1, #8]
	ldrh	r3, [r4, #316]
	muls	r3, r6, r3
	add	r6, r6, #1
	it	mi
	addmi	r3, r3, #3
	uxth	r6, r6
	bic	r3, r3, #3
	add	r3, r3, r0
	str	r3, [r1, #12]
.L843:
	add	r10, r10, #1
	b	.L841
.L844:
	adds	r5, r5, #1
	uxth	r5, r5
	b	.L843
.L851:
	mul	r3, r9, r8
	ldr	r2, [r4, #3272]
	adds	r1, r2, r3
	ldr	r3, [r2, r3]
	adds	r3, r3, #1
	bne	.L850
	ldr	r0, [r1, #4]
	adds	r5, r5, #1
	uxth	r5, r5
	ubfx	r0, r0, #10, #16
	bl	FtlBbmMapBadBlock
.L850:
	add	r8, r8, #1
	b	.L849
.L868:
	movs	r3, #2
	mov	r10, r7
	str	r3, [sp, #8]
	b	.L852
.L857:
	uxth	r3, r9
	ldr	r0, [r4, #3272]
	movs	r2, #0
	mul	r1, fp, r3
	add	r3, r3, r4
	str	r2, [r0, r1]
	ldr	r1, [sp, #4]
	ldrb	r0, [r3, #264]	@ zero_extendqisi2
	bl	V2P_block
	str	r0, [sp, #12]
	cbz	r7, .L854
	bl	IsBlkInVendorPart
	cbnz	r0, .L855
.L854:
	ldr	r0, [sp, #12]
	bl	FtlBbmIsBadBlock
	cbnz	r0, .L855
	ldr	r1, [r4, #3272]
	ldr	r3, [sp, #12]
	ldr	r2, .L885
	mla	r1, fp, r6, r1
	ldr	r0, [r2, #3320]
	add	r3, r8, r3, lsl #10
	str	r3, [r1, #4]
	ldr	r3, [r4, #3316]
	str	r3, [r1, #8]
	ldrh	r3, [r4, #316]
	muls	r3, r6, r3
	add	r6, r6, #1
	it	mi
	addmi	r3, r3, #3
	uxth	r6, r6
	bic	r3, r3, #3
	add	r3, r3, r0
	str	r3, [r1, #12]
.L855:
	add	r9, r9, #1
	b	.L853
.L861:
	mul	r3, fp, r9
	ldr	r2, [r4, #3272]
	adds	r1, r2, r3
	ldr	r3, [r2, r3]
	cbz	r3, .L860
	ldr	r0, [r1, #4]
	adds	r5, r5, #1
	uxth	r5, r5
	ubfx	r0, r0, #10, #16
	bl	FtlBbmMapBadBlock
.L860:
	add	r9, r9, #1
	b	.L859
.L865:
	cbz	r7, .L864
	mul	r3, r9, r8
	ldr	r2, [r4, #3272]
	adds	r1, r2, r3
	ldr	r3, [r2, r3]
	cbnz	r3, .L864
	ldr	r0, [r1, #4]
	movs	r1, #1
	ubfx	r0, r0, #10, #16
	bl	FtlFreeSysBlkQueueIn
.L864:
	add	r8, r8, #1
	b	.L863
.L886:
	.align	2
.L885:
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
	cmp	r0, #11
	push	{r4, r5, lr}
	.save {r4, r5, lr}
	mov	r5, r0
	.pad #92
	sub	sp, sp, #92
	bls	.L890
	ldr	r3, .L892
	add	r0, sp, #24
	movs	r2, #32
	movs	r1, #165
	str	r0, [sp, #16]
	lsls	r5, r5, #10
	ldr	r4, [r3, #3328]
	str	r4, [sp, #12]
	bl	memset
	mov	r3, #1515870810
	movs	r2, #1
	str	r3, [r4]	@ unaligned
	mov	r1, r2
	str	r3, [r4, #4]	@ unaligned
	add	r0, sp, #4
	str	r5, [sp, #8]
	bl	FlashEraseBlocks
	movs	r3, #1
	add	r0, sp, #4
	mov	r2, r3
	mov	r1, r3
	bl	FlashProgPages
	ldr	r3, [sp, #4]
	cbnz	r3, .L891
	adds	r3, r5, #1
	add	r0, sp, #4
	str	r3, [sp, #8]
	movs	r3, #1
	mov	r2, r3
	mov	r1, r3
	bl	FlashProgPages
	ldr	r4, [sp, #4]
	adds	r4, r4, #0
	it	ne
	movne	r4, #1
	negs	r4, r4
.L889:
	movs	r2, #1
	movs	r1, #0
	add	r0, sp, #4
	str	r5, [sp, #8]
	bl	FlashEraseBlocks
.L887:
	mov	r0, r4
	add	sp, sp, #92
	@ sp needed
	pop	{r4, r5, pc}
.L891:
	mov	r4, #-1
	b	.L889
.L890:
	movs	r4, #0
	b	.L887
.L893:
	.align	2
.L892:
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
	ldr	r4, .L906
	movs	r1, #0
	ldr	r0, [r4, #3304]
	add	r6, r4, #380
	ldr	r3, [r4, #3336]
	ldrh	r2, [r4, #314]
	str	r0, [r4, #3464]
	str	r3, [r4, #3468]
	bl	ftl_memset
.L895:
	ldrh	r3, [r4, #258]
	cmp	r5, r3
	blt	.L896
	ldr	r6, [r4, #3468]
	movs	r2, #16
	movs	r1, #255
	ldr	r8, .L906+12
	ldr	r9, .L906+16
	movs	r5, #0
	mov	r0, r6
	mov	r7, r5
	bl	memset
	movw	r3, #61649
	strh	r3, [r6]	@ movhi
	ldr	r3, [r4, #364]
	str	r3, [r6, #4]
	ldrh	r3, [r4, #356]
	strh	r3, [r6, #2]	@ movhi
	ldrh	r3, [r4, #360]
	strh	r3, [r6, #8]	@ movhi
	ldrh	r3, [r4, #362]
	strh	r3, [r6, #10]	@ movhi
	ldr	r3, [r4, #232]
	strh	r3, [r6, #12]	@ movhi
	ldr	r3, [r4, #2608]
	strh	r3, [r6, #14]	@ movhi
.L897:
	ldr	r3, [r4, #3304]
	mov	r10, #0
	ldrh	r2, [r4, #358]
	ldrh	r1, [r4, #356]
	str	r3, [r4, #3464]
	ldr	r3, [r4, #3336]
	str	r10, [r4, #3456]
	str	r3, [r4, #3468]
	orr	r3, r2, r1, lsl #10
	ldrh	r0, [r6, #10]
	str	r3, [r4, #3460]
	ldrh	r3, [r4, #360]
	str	r0, [sp]
	mov	r0, r8
	bl	sftl_printk
	ldrh	r3, [r4, #308]
	ldrh	r2, [r4, #358]
	subs	r3, r3, #1
	cmp	r2, r3
	blt	.L898
	ldr	r3, [r4, #364]
	ldrh	r2, [r4, #356]
	ldr	r0, [r4, #3272]
	adds	r3, r3, #1
	strh	r10, [r4, #358]	@ movhi
	str	r3, [r4, #364]
	str	r3, [r6, #4]
	ldrh	r3, [r4, #360]
	strh	r2, [r6, #8]	@ movhi
	strh	r2, [r4, #360]	@ movhi
	movs	r2, #1
	strh	r3, [r4, #356]	@ movhi
	mov	r1, r2
	lsls	r3, r3, #10
	str	r3, [r4, #3460]
	str	r3, [r0, #4]
	bl	FlashEraseBlocks
.L898:
	movs	r3, #1
	mov	r0, r9
	mov	r2, r3
	mov	r1, r3
	bl	FlashProgPages
	ldrh	r3, [r4, #358]
	adds	r3, r3, #1
	strh	r3, [r4, #358]	@ movhi
	ldr	r3, [r4, #3456]
	adds	r2, r3, #1
	bne	.L899
	adds	r5, r5, #1
	ldr	r1, [r4, #3460]
	uxth	r5, r5
	ldr	r0, .L906+4
	bl	sftl_printk
	cmp	r5, #3
	bls	.L897
	mov	r2, r5
	ldr	r1, [r4, #3460]
	ldr	r0, .L906+8
	bl	sftl_printk
.L901:
	b	.L901
.L896:
	ldrh	r2, [r4, #3392]
	ldr	r3, [r4, #3464]
	ldr	r1, [r6, #4]!
	mul	r0, r2, r5
	lsls	r2, r2, #2
	adds	r5, r5, #1
	add	r0, r3, r0, lsl #2
	bl	ftl_memcpy
	b	.L895
.L902:
	movs	r7, #1
	b	.L897
.L899:
	adds	r7, r7, #1
	cmp	r7, #1
	ble	.L902
	cmp	r3, #256
	beq	.L897
	movs	r0, #0
	add	sp, sp, #8
	@ sp needed
	pop	{r4, r5, r6, r7, r8, r9, r10, pc}
.L907:
	.align	2
.L906:
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
	ldr	r4, .L944
	.pad #20
	sub	sp, sp, #20
	ldr	r9, .L944+20
.L909:
	ldr	r3, .L944
	ldr	r2, .L944
	ldrh	r3, [r3, #228]
	ldrh	r2, [r2, #2532]
	add	r3, r3, r2
	ldr	r2, .L944
	ldrh	r2, [r2, #244]
	cmp	r3, r2
	ble	.L910
	mov	r2, #2656
	mov	r1, r9
	ldr	r0, .L944+4
	bl	sftl_printk
.L910:
	ldr	r3, .L944+8
	cmp	r5, r3
	bne	.L936
	ldrh	r3, [r4, #228]
	ldr	r1, [r4, #3168]
	mul	r2, r1, r3
	lsrs	r1, r3, #1
	adds	r1, r1, #1
	add	r1, r1, r2, lsr #2
	uxth	r1, r1
	cbz	r1, .L911
	subs	r1, r1, #1
	uxth	r1, r1
.L911:
	ldr	r0, .L944+12
	bl	List_pop_index_node
	ldrh	r3, [r4, #228]
	mov	r6, r0
	uxth	r8, r0
	cbnz	r3, .L912
	movw	r2, #2665
	mov	r1, r9
	ldr	r0, .L944+4
	bl	sftl_printk
.L912:
	ldrh	r3, [r4, #228]
	subs	r3, r3, #1
	strh	r3, [r4, #228]	@ movhi
	ldrh	r3, [r4, #244]
	cmp	r3, r8
	bls	.L909
	ldr	r3, [r4, #76]
	uxth	r6, r6
	ldrh	r7, [r3, r6, lsl #1]
	cmp	r7, #0
	bne	.L909
	strh	r8, [r5]	@ movhi
	mov	r0, r5
	bl	make_superblock
	ldrb	r3, [r5, #7]	@ zero_extendqisi2
	cbnz	r3, .L914
	ldr	r3, [r4, #76]
	movw	r2, #65535
	mov	r0, r8
	strh	r2, [r3, r6, lsl #1]	@ movhi
	bl	INSERT_DATA_LIST
	ldrh	r2, [r4, #2532]
	ldrh	r3, [r4, #228]
	add	r3, r3, r2
	ldrh	r2, [r4, #244]
	cmp	r3, r2
	ble	.L909
	movw	r2, #2679
	mov	r1, r9
	ldr	r0, .L944+4
	bl	sftl_printk
	b	.L909
.L936:
	movs	r1, #0
	b	.L911
.L914:
	ldrh	r2, [r4, #2532]
	ldrh	r3, [r4, #228]
	add	r3, r3, r2
	ldrh	r2, [r4, #244]
	cmp	r3, r2
	ble	.L916
	movw	r2, #2682
	mov	r1, r9
	ldr	r0, .L944+4
	bl	sftl_printk
.L916:
	ldr	r1, [r4, #3272]
	mov	r10, #20
	ldrh	r2, [r4, #236]
	add	fp, r5, #16
	mov	r0, fp
	mov	ip, #0
	mov	r3, r1
	mla	r2, r10, r2, r1
	str	r2, [sp, #4]
.L917:
	ldr	r2, [sp, #4]
	cmp	r2, r3
	bne	.L919
	cbnz	r7, .L920
	movw	r2, #2693
	mov	r1, r9
	ldr	r0, .L944+4
	bl	sftl_printk
.L920:
	ldrh	r3, [r4, #176]
	cmp	r3, r8
	bne	.L921
	movw	r2, #2695
	mov	r1, r9
	ldr	r0, .L944+4
	bl	sftl_printk
.L921:
	ldrb	r3, [r5, #8]	@ zero_extendqisi2
	ldr	r2, [r4, #2540]
	cmp	r3, #0
	bne	.L922
	ldrh	r3, [r2, r6, lsl #1]
	cmp	r3, #0
	beq	.L923
	ldrh	r1, [r4, #296]
	add	r3, r3, r1
.L943:
	strh	r3, [r2, r6, lsl #1]	@ movhi
	movs	r1, #0
	ldr	r3, [r4, #2600]
	mov	r0, r8
	adds	r3, r3, #1
	str	r3, [r4, #2600]
	bl	ftl_set_blk_mode
.L925:
	ldr	r3, [r4, #2540]
	ldr	r2, [r4, #2616]
	ldrh	r0, [r4, #296]
	ldrh	r3, [r3, r6, lsl #1]
	ldrh	r1, [r4, #244]
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
	ldr	r0, .L944+16
	adds	r3, r3, #1
	str	r3, [r2, #16]
	movs	r2, #20
	mla	r2, r2, r7, r1
	adds	r3, r1, #4
	adds	r2, r2, #24
.L927:
	adds	r3, r3, #20
	cmp	r2, r3
	bne	.L928
	ldrb	r1, [r5, #8]	@ zero_extendqisi2
	mov	r2, r7
	ldr	r0, [r4, #3272]
	mov	r10, #0
	bl	FlashEraseBlocks
	mov	r3, r10
	movs	r1, #20
.L929:
	uxth	r2, r10
	cmp	r7, r2
	bhi	.L931
	cmp	r3, #0
	ble	.L932
	mov	r0, r8
	bl	update_multiplier_value
	bl	FtlBbmTblFlush
.L932:
	ldrb	r2, [r5, #7]	@ zero_extendqisi2
	cmp	r2, #0
	bne	.L933
	ldr	r3, [r4, #76]
	movw	r2, #65535
	strh	r2, [r3, r6, lsl #1]	@ movhi
	b	.L909
.L919:
	str	ip, [r3, #8]
	movw	lr, #65535
	str	ip, [r3, #12]
	ldrh	r2, [r0], #2
	cmp	r2, lr
	beq	.L918
	mla	lr, r10, r7, r1
	adds	r7, r7, #1
	lsls	r2, r2, #10
	uxth	r7, r7
	str	r2, [lr, #4]
.L918:
	adds	r3, r3, #20
	b	.L917
.L923:
	movs	r3, #2
	b	.L943
.L922:
	ldrh	r3, [r2, r6, lsl #1]
	mov	r0, r8
	adds	r3, r3, #1
	strh	r3, [r2, r6, lsl #1]	@ movhi
	ldr	r3, [r4, #2604]
	adds	r3, r3, #1
	str	r3, [r4, #2604]
	bl	ftl_set_blk_mode.part.6
	b	.L925
.L928:
	ldr	r1, [r3, #-20]
	ands	r1, r1, r0
	str	r1, [r3, #-20]
	b	.L927
.L931:
	mul	r2, r1, r10
	ldr	r0, [r4, #3272]
	add	ip, r0, r2
	ldr	r2, [r0, r2]
	adds	r0, r2, #1
	bne	.L930
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
.L930:
	add	r10, r10, #1
	add	fp, fp, #2
	b	.L929
.L933:
	ldrh	r3, [r4, #306]
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
	ldr	r2, [r4, #76]
	ldrh	r1, [r5]
	strh	r3, [r2, r1, lsl #1]	@ movhi
	ldrh	r3, [r5, #4]
	cbz	r3, .L934
	ldrb	r3, [r5, #7]	@ zero_extendqisi2
	cbnz	r3, .L935
.L934:
	movw	r2, #2748
	ldr	r1, .L944+20
	ldr	r0, .L944+4
	bl	sftl_printk
.L935:
	movs	r0, #0
	add	sp, sp, #20
	@ sp needed
	pop	{r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L945:
	.align	2
.L944:
	.word	.LANCHOR0
	.word	.LC8
	.word	.LANCHOR0+128
	.word	.LANCHOR0+2536
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
	ldr	r4, .L958
	ldrh	r3, [r4, #3210]
	cbz	r3, .L947
	movs	r7, #0
	addw	r10, r4, #3212
.L948:
	ldrh	r2, [r4, #236]
	uxth	r3, r7
	cmp	r2, r3
	bhi	.L954
	bl	FtlGcReFreshBadBlk
.L947:
	movs	r0, #0
	pop	{r3, r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L954:
	uxtah	r3, r4, r7
	mov	r1, r8
	mov	fp, #0
	ldrb	r0, [r3, #264]	@ zero_extendqisi2
	bl	V2P_block
	mov	r9, r0
.L949:
	ldrh	r3, [r4, #3210]
	uxth	r5, fp
	cmp	r3, r5
	bhi	.L953
	adds	r7, r7, #1
	b	.L948
.L953:
	uxth	r6, fp
	add	r3, r4, r6, lsl #1
	ldrh	r3, [r3, #3212]
	cmp	r3, r9
	bne	.L950
	mov	r1, r9
	ldr	r0, .L958+4
	bl	sftl_printk
	mov	r0, r9
	bl	FtlBbmMapBadBlock
	bl	FtlBbmTblFlush
	ldrh	r1, [r4, #3210]
	add	r3, r10, r6, lsl #1
.L951:
	cmp	r5, r1
	bcc	.L952
	subs	r1, r1, #1
	strh	r1, [r4, #3210]	@ movhi
.L950:
	add	fp, fp, #1
	b	.L949
.L952:
	ldrh	r0, [r3, #2]!
	adds	r5, r5, #1
	uxth	r5, r5
	strh	r0, [r3, #-2]	@ movhi
	b	.L951
.L959:
	.align	2
.L958:
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
	ldr	r4, .L972
	ldr	r3, [r4, #76]
	ldrh	r3, [r3, r0, lsl #1]
	cmp	r3, #0
	bne	.L961
	ldrh	r3, [r4, #226]
	cmp	r3, r0
	itt	eq
	movweq	r3, #65535
	strheq	r3, [r4, #226]	@ movhi
	ldrh	r3, [r4, #224]
	cmp	r3, r0
	itt	eq
	movweq	r3, #65535
	strheq	r3, [r4, #224]	@ movhi
	ldrh	r3, [r4, #176]
	cmp	r3, r0
	bne	.L964
	movw	r3, #65535
	strh	r3, [r4, #176]	@ movhi
.L965:
	mov	r1, r5
	ldr	r0, .L972+4
	bl	List_remove_node
	ldrh	r3, [r4, #2532]
	cbnz	r3, .L967
	movw	r2, #2824
	ldr	r1, .L972+8
	ldr	r0, .L972+12
	bl	sftl_printk
.L967:
	ldrh	r3, [r4, #2532]
	mov	r0, r5
	subs	r3, r3, #1
	strh	r3, [r4, #2532]	@ movhi
	bl	free_data_superblock
	mov	r0, r5
	bl	FtlGcFreeBadSuperBlk
	ldrh	r2, [r4, #2532]
	ldrh	r3, [r4, #228]
	add	r3, r3, r2
	ldrh	r2, [r4, #244]
	cmp	r3, r2
	ble	.L971
	movw	r2, #2827
	ldr	r1, .L972+8
	ldr	r0, .L972+12
	bl	sftl_printk
.L971:
	movs	r0, #1
	pop	{r3, r4, r5, pc}
.L964:
	ldrh	r3, [r4, #28]
	cmp	r3, r0
	beq	.L970
	ldrh	r3, [r4, #80]
	cmp	r3, r0
	beq	.L970
	ldrh	r3, [r4, #128]
	cmp	r3, r0
	bne	.L965
.L970:
	movs	r0, #0
	pop	{r3, r4, r5, pc}
.L961:
	bl	List_update_data_list
	b	.L970
.L973:
	.align	2
.L972:
	.word	.LANCHOR0
	.word	.LANCHOR0+2524
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
	ldr	r5, .L983
	beq	.L975
	ldr	r3, [r5, #76]
	ldrh	r2, [r3, r0, lsl #1]
	cbnz	r2, .L976
	mov	r1, r0
	ldr	r0, .L983+4
	bl	sftl_printk
	ldr	r3, [r5, #76]
	ldrh	r6, [r3, r4, lsl #1]
	cbz	r6, .L977
.L981:
	movs	r6, #0
.L974:
	mov	r0, r6
	pop	{r4, r5, r6, pc}
.L977:
	movw	r2, #2842
.L982:
	ldr	r1, .L983+8
	ldr	r0, .L983+12
	bl	sftl_printk
	b	.L974
.L976:
	subs	r2, r2, #1
	strh	r2, [r3, r0, lsl #1]	@ movhi
.L975:
	ldrh	r0, [r5, #3442]
	movw	r3, #65535
	cmp	r0, r3
	bne	.L979
	strh	r4, [r5, #3442]	@ movhi
	b	.L981
.L979:
	cmp	r4, r0
	beq	.L981
	bl	update_vpc_list
	ldr	r2, [r5, #2520]
	adds	r6, r0, #0
	ldr	r3, [r5, #2524]
	it	ne
	movne	r6, #1
	strh	r4, [r5, #3442]	@ movhi
	subs	r3, r3, r2
	asrs	r2, r3, #1
	ldr	r3, .L983+16
	muls	r3, r2, r3
	ldr	r2, [r5, #76]
	uxth	r1, r3
	ldrh	r2, [r2, r1, lsl #1]
	cmp	r2, #0
	bne	.L974
	cmp	r4, r1
	beq	.L974
	movw	r2, #2858
	b	.L982
.L984:
	.align	2
.L983:
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
	push	{r4, r5, r6, r7, r8, lr}
	.save {r4, r5, r6, r7, r8, lr}
	.pad #24
	sub	sp, sp, #24
	ldr	r5, .L998
	mov	r3, #-1
	movs	r7, #0
	movw	r1, #61589
	str	r3, [sp, #20]
	mov	r4, r0
	ldr	r2, [r5, #3304]
	ldr	r6, [r5, #3336]
	str	r2, [sp, #12]
	mvn	r2, #2
	str	r6, [sp, #16]
	str	r2, [r6, #8]
	mvn	r2, #1
	str	r2, [r6, #12]
	ldrh	r2, [r0]
	strh	r7, [r6]	@ movhi
	strh	r2, [r6, #2]	@ movhi
	ldr	r2, [r5, #3304]
	str	r1, [r2]
	add	r1, r1, #304087040
	ldr	r2, [r5, #3304]
	add	r1, r1, #1269760
	addw	r1, r1, #1507
	str	r1, [r2, #4]
	ldrh	r2, [r0, #4]
	tst	r2, #1
	ite	eq
	moveq	r8, #6
	movne	r8, #7
.L991:
	ldrh	r3, [r4, #4]
	cbnz	r3, .L987
.L988:
	ldr	r2, [r5, #76]
	ldrh	r1, [r4]
	ldrh	r0, [r4, #4]
	ldrh	r3, [r2, r1, lsl #1]
	subs	r3, r3, r0
	strh	r3, [r2, r1, lsl #1]	@ movhi
	ldrh	r3, [r5, #306]
	strh	r3, [r4, #2]	@ movhi
	movs	r3, #0
	strb	r3, [r4, #6]
	strh	r3, [r4, #4]	@ movhi
	add	sp, sp, #24
	@ sp needed
	pop	{r4, r5, r6, r7, r8, pc}
.L987:
	mov	r0, r4
	bl	get_new_active_ppa
	str	r0, [sp, #8]
	adds	r0, r0, #1
	beq	.L988
	ldr	r3, [r5, #2596]
	movs	r1, #1
	add	r0, sp, #4
	str	r3, [r6, #4]
	adds	r3, r3, #1
	adds	r2, r3, #1
	it	eq
	moveq	r3, r7
	str	r3, [r5, #2596]
	movs	r3, #0
	mov	r2, r3
	bl	FlashProgPages
	ldrh	r0, [r4]
	bl	decrement_vpc_count
	subs	r8, r8, #1
	bne	.L991
	b	.L988
.L999:
	.align	2
.L998:
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
	ldr	r9, .L1019+4
	bl	FtlBbtMemInit
	sub	r5, r9, #384
	sub	r10, r9, #18
	mov	r4, r5
	bl	FtlLoadFactoryBbt
.L1001:
	ldrh	r3, [r5, #258]
	cmp	r8, r3
	bcc	.L1007
	movs	r5, #0
.L1008:
	ldrh	r3, [r4, #318]
	uxth	r0, r5
	adds	r5, r5, #1
	cmp	r3, r0
	bhi	.L1009
	ldrh	r5, [r4, #368]
	movw	r6, #65535
	subs	r5, r5, #1
	uxth	r5, r5
.L1010:
	ldrh	r3, [r4, #368]
	subs	r3, r3, #48
	cmp	r5, r3
	ble	.L1014
	mov	r0, r5
	bl	FtlBbmIsBadBlock
	cmp	r0, #1
	beq	.L1011
	mov	r0, r5
	bl	FlashTestBlk
	cmp	r0, #0
	beq	.L1012
	mov	r0, r5
	bl	FtlBbmMapBadBlock
.L1011:
	subs	r5, r5, #1
	uxth	r5, r5
	b	.L1010
.L1007:
	ldrh	r3, [r10, #2]!
	movw	r2, #65535
	ldr	r0, [r5, #3304]
	ldr	fp, [r5, #3336]
	cmp	r3, r2
	str	r0, [r5, #3464]
	str	fp, [r5, #3468]
	beq	.L1002
	ldrh	r7, [r5, #302]
	movs	r2, #1
	mov	r1, r2
	ldr	r0, .L1019
	mla	r7, r8, r7, r3
	lsls	r3, r7, #10
	str	r3, [r5, #3460]
	bl	FlashReadPages
	ldrh	r2, [r5, #302]
	ldr	r1, [r5, #3464]
	ldr	r0, [r9]
	adds	r2, r2, #7
	asrs	r2, r2, #3
	bl	ftl_memcpy
.L1003:
	uxth	r0, r7
	add	r8, r8, #1
	add	r9, r9, #4
	bl	FtlBbmMapBadBlock
	b	.L1001
.L1002:
	mov	r1, r8
	bl	FlashGetBadBlockList
	ldr	r1, [r9]
	ldr	r0, [r5, #3464]
	bl	FtlBbt2Bitmap
	ldrh	r6, [r5, #302]
.L1005:
	subs	r6, r6, #1
	uxth	r6, r6
.L1004:
	ldrh	r0, [r4, #302]
	smlabb	r0, r0, r8, r6
	uxth	r0, r0
	bl	FtlBbmIsBadBlock
	cmp	r0, #1
	beq	.L1005
	movs	r1, #16
	ldr	r0, [r4, #3336]
	strh	r6, [r10]	@ movhi
	bl	__memzero
	mov	r1, #4096
	ldr	r0, [r4, #3304]
	bl	__memzero
	movw	r3, #61664
	strh	r3, [fp]	@ movhi
	movs	r3, #0
	str	r3, [fp, #4]
	ldrh	r3, [r10]
	ldrh	r7, [r4, #302]
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
	ldr	r0, .L1019
	mov	r1, r2
	bl	FlashEraseBlocks
	movs	r3, #1
	ldr	r0, .L1019
	mov	r2, r3
	mov	r1, r3
	bl	FlashProgPages
	ldr	r3, [r4, #3456]
	adds	r3, r3, #1
	bne	.L1003
	uxth	r0, r7
	bl	FtlBbmMapBadBlock
	b	.L1004
.L1009:
	bl	FtlBbmMapBadBlock
	b	.L1008
.L1012:
	ldrh	r3, [r4, #356]
	cmp	r3, r6
	bne	.L1013
	strh	r5, [r4, #356]	@ movhi
	b	.L1011
.L1013:
	strh	r5, [r4, #360]	@ movhi
.L1014:
	ldrh	r3, [r4, #356]
	movs	r5, #0
	ldr	r0, [r4, #3272]
	movs	r1, #1
	str	r5, [r4, #364]
	movs	r2, #2
	strh	r5, [r4, #358]	@ movhi
	lsls	r3, r3, #10
	str	r3, [r0, #4]
	ldrh	r3, [r4, #360]
	lsls	r3, r3, #10
	str	r3, [r0, #24]
	bl	FlashEraseBlocks
	ldrh	r0, [r4, #356]
	bl	FtlBbmMapBadBlock
	ldrh	r0, [r4, #360]
	bl	FtlBbmMapBadBlock
	bl	FtlBbmTblFlush
	ldr	r3, [r4, #364]
	ldrh	r2, [r4, #360]
	strh	r5, [r4, #358]	@ movhi
	adds	r3, r3, #1
	str	r3, [r4, #364]
	ldrh	r3, [r4, #356]
	strh	r2, [r4, #356]	@ movhi
	strh	r3, [r4, #360]	@ movhi
	bl	FtlBbmTblFlush
	mov	r0, r5
	pop	{r3, r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L1020:
	.align	2
.L1019:
	.word	.LANCHOR0+3456
	.word	.LANCHOR0+384
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
	ldr	r3, .L1025
	add	r1, r1, r0
	push	{r4, lr}
	.save {r4, lr}
.L1023:
	cmp	r0, r1
	bne	.L1024
	mov	r0, r3
	pop	{r4, pc}
.L1024:
	lsrs	r2, r3, #2
	ldrb	r4, [r0], #1	@ zero_extendqisi2
	add	r2, r2, r3, lsl #5
	add	r2, r2, r4
	eors	r3, r3, r2
	b	.L1023
.L1026:
	.align	2
.L1025:
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
	bne	.L1028
	ldrh	r3, [r0, #8]
	cbz	r3, .L1029
	movw	r2, #641
	ldr	r1, .L1037
	ldr	r0, .L1037+4
	bl	sftl_printk
.L1029:
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
.L1030:
	movs	r0, #0
	pop	{r4, r5, r6, r7, r8, pc}
.L1028:
	ldrh	r7, [r5, r3, lsl #1]
	movs	r1, #255
	ldrh	r3, [r0, #2]
	ldr	r5, .L1037+8
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
	ldrh	r2, [r5, #308]
	ldr	r0, [r5, #3304]
	lsls	r2, r2, #3
	bl	ftl_memset
	movs	r2, #0
	mov	r3, r2
.L1031:
	ldrh	r0, [r4, #6]
	uxth	r1, r2
	cmp	r0, r1
	bhi	.L1033
	ldrh	r1, [r5, #314]
	ldr	r0, [r5, #3464]
	bl	js_hash
	movs	r2, #1
	str	r0, [r6, #12]
	movs	r3, #0
	mov	r1, r2
	ldr	r0, .L1037+12
	bl	FlashProgPages
	ldrh	r3, [r4, #2]
	mov	r0, r4
	adds	r3, r3, #1
	strh	r3, [r4, #2]	@ movhi
	bl	ftl_map_blk_gc
	b	.L1030
.L1033:
	uxth	r0, r2
	ldr	r1, [r8, r0, lsl #2]
	cmp	r7, r1, lsr #10
	bne	.L1032
	ldr	r1, [r5, #3304]
	adds	r3, r3, #1
	uxth	r3, r3
	str	r0, [r1, r3, lsl #3]
	ldr	r1, [r5, #3304]
	ldr	r0, [r8, r0, lsl #2]
	add	r1, r1, r3, lsl #3
	str	r0, [r1, #4]
.L1032:
	adds	r2, r2, #1
	b	.L1031
.L1038:
	.align	2
.L1037:
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
	ldr	r7, .L1063
	mov	r8, r1
	movs	r6, #0
	ldr	r9, .L1063+20
	mov	r5, r7
	str	r2, [sp, #4]
.L1040:
	ldr	r3, [r7, #2576]
	adds	r3, r3, #1
	str	r3, [r7, #2576]
	ldrh	r3, [r7, #308]
	ldrh	r2, [r4, #2]
	subs	r3, r3, #1
	cmp	r2, r3
	bge	.L1041
	ldrh	r2, [r4]
	movw	r3, #65535
	cmp	r2, r3
	bne	.L1042
.L1041:
	mov	r0, r4
	bl	Ftl_write_map_blk_to_last_page
.L1042:
	ldrh	r2, [r4]
	ldr	r3, [r4, #12]
	ldrh	r3, [r3, r2, lsl #1]
	cbnz	r3, .L1043
	mov	r2, #700
	mov	r1, r9
	ldr	r0, .L1063+4
	bl	sftl_printk
.L1043:
	ldrh	r2, [r4]
	ldrh	r3, [r4, #10]
	cmp	r2, r3
	bcc	.L1044
	movw	r2, #701
	mov	r1, r9
	ldr	r0, .L1063+4
	bl	sftl_printk
.L1044:
	ldrh	r2, [r4]
	movs	r1, #16
	ldr	r3, [r4, #12]
	ldr	r0, [r5, #3336]
	ldrh	r10, [r3, r2, lsl #1]
	ldrh	r3, [r4, #2]
	str	r0, [r5, #3468]
	orr	r3, r3, r10, lsl #10
	str	r3, [r5, #3460]
	ldr	r3, [sp, #4]
	str	r3, [r5, #3464]
	bl	__memzero
	ldr	r3, [r4, #28]
	ldr	fp, [r5, #3468]
	str	r3, [fp, #4]
	strh	r8, [fp, #8]	@ movhi
	ldrh	r3, [r4, #4]
	strh	r10, [fp, #2]	@ movhi
	strh	r3, [fp]	@ movhi
	ldrh	r1, [r5, #314]
	ldr	r0, [r5, #3464]
	bl	js_hash
	movs	r3, #1
	str	r0, [fp, #12]
	mov	r2, r3
	mov	r1, r3
	ldr	r0, .L1063+8
	bl	FlashProgPages
	ldrh	r3, [r4, #2]
	adds	r3, r3, #1
	uxth	r3, r3
	strh	r3, [r4, #2]	@ movhi
	ldr	r2, [r5, #3456]
	adds	r1, r2, #1
	bne	.L1045
	ldr	r1, [r5, #3460]
	adds	r6, r6, #1
	ldr	r0, .L1063+12
	uxth	r6, r6
	bl	sftl_printk
	ldrh	r3, [r4, #2]
	cmp	r3, #2
	ittt	ls
	ldrhls	r3, [r5, #308]
	addls	r3, r3, #-1
	strhls	r3, [r4, #2]	@ movhi
	cmp	r6, #3
	bls	.L1047
	mov	r2, r6
	ldr	r1, [r5, #3460]
	ldr	r0, .L1063+16
	bl	sftl_printk
.L1048:
	b	.L1048
.L1047:
	ldr	r3, [r4, #32]
	cmp	r3, #0
	beq	.L1040
.L1062:
	b	.L1062
.L1045:
	cmp	r3, #1
	beq	.L1051
	cmp	r2, #256
	beq	.L1051
	ldr	r0, [r4, #36]
	cbz	r0, .L1052
.L1051:
	movs	r3, #0
	str	r3, [r4, #36]
	b	.L1040
.L1052:
	ldr	r2, [r5, #3460]
	ldr	r3, [r4, #24]
	str	r2, [r3, r8, lsl #2]
	add	sp, sp, #12
	@ sp needed
	pop	{r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L1064:
	.align	2
.L1063:
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
	ldr	r4, .L1072
	mov	r8, r1
	ldrh	r3, [r4, #340]
	cmp	r3, r0
	bcs	.L1066
	movw	r2, #485
	ldr	r1, .L1072+4
	ldr	r0, .L1072+8
	bl	sftl_printk
.L1066:
	ldr	r3, [r4, #3380]
	movs	r5, #12
	ldr	r7, [r3, r6, lsl #2]
	cbnz	r7, .L1067
	mul	r5, r5, r8
	ldr	r3, [r4, #2544]
	ldrh	r2, [r4, #314]
	movs	r1, #255
	add	r3, r3, r5
	ldr	r0, [r3, #8]
	bl	ftl_memset
	ldr	r3, [r4, #2544]
	strh	r6, [r3, r5]	@ movhi
	ldr	r3, [r4, #2544]
	add	r5, r5, r3
	str	r7, [r5, #4]
.L1068:
	movs	r0, #0
	pop	{r4, r5, r6, r7, r8, pc}
.L1067:
	mul	r5, r5, r8
	ldr	r3, [r4, #2544]
	movs	r2, #1
	ldr	r0, .L1072+12
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
	beq	.L1069
	mov	r2, r7
	mov	r1, r6
	ldr	r0, .L1072+16
	bl	sftl_printk
	movs	r3, #4
	ldr	r1, [r4, #3468]
	mov	r2, r3
	ldr	r0, .L1072+20
	bl	rknand_print_hex
	ldrh	r3, [r4, #340]
	movs	r2, #4
	ldr	r1, [r4, #3380]
	ldr	r0, .L1072+24
	bl	rknand_print_hex
.L1070:
	ldrh	r3, [r8, #8]
	cmp	r3, r6
	beq	.L1071
	mov	r2, #508
	ldr	r1, .L1072+4
	ldr	r0, .L1072+8
	bl	sftl_printk
.L1071:
	ldr	r3, [r4, #2544]
	movs	r1, #0
	adds	r2, r3, r5
	str	r1, [r2, #4]
	strh	r6, [r3, r5]	@ movhi
	b	.L1068
.L1069:
	ldr	r3, [r4, #3456]
	cmp	r3, #256
	bne	.L1070
	mov	r2, r7
	mov	r1, r6
	ldr	r0, .L1072+28
	bl	sftl_printk
	ldr	r3, [r4, #2544]
	mov	r1, r6
	ldr	r0, .L1072+32
	add	r3, r3, r5
	ldr	r2, [r3, #8]
	bl	FtlMapWritePage
	b	.L1070
.L1073:
	.align	2
.L1072:
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
	ldr	r5, .L1090
	subs	r3, r3, #5
	cmp	r2, r3
	blt	.L1075
	uxth	r0, r0
	ldrh	r9, [r6, r0, lsl #1]
	cmp	r9, #0
	beq	.L1075
	ldr	r3, [r4, #32]
	cbnz	r3, .L1075
	movs	r2, #1
	str	r2, [r4, #32]
	strh	r3, [r6, r0, lsl #1]	@ movhi
	ldrh	r3, [r4, #8]
	ldrh	r2, [r4, #2]
	subs	r3, r3, #1
	strh	r3, [r4, #8]	@ movhi
	ldrh	r3, [r5, #308]
	cmp	r2, r3
	bcc	.L1076
	mov	r0, r4
	bl	ftl_map_blk_alloc_new_blk
.L1076:
	movs	r6, #0
.L1077:
	ldrh	r2, [r4, #6]
	uxth	fp, r6
	cmp	r2, fp
	bhi	.L1084
	movs	r1, #1
	mov	r0, r9
	bl	FtlFreeSysBlkQueueIn
	movs	r3, #0
	str	r3, [r4, #32]
.L1075:
	ldrh	r2, [r4, #2]
	ldrh	r3, [r5, #308]
	cmp	r2, r3
	bcc	.L1085
	mov	r0, r4
	bl	ftl_map_blk_alloc_new_blk
.L1085:
	movs	r0, #0
	add	sp, sp, #12
	@ sp needed
	pop	{r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L1084:
	uxth	r7, r6
	ldr	r2, [r10, r7, lsl #2]
	add	r3, r10, r7, lsl #2
	str	r3, [sp, #4]
	cmp	r9, r2, lsr #10
	bne	.L1078
	ldr	r2, [r5, #3308]
	ldr	r8, [r5, #3336]
	ldr	r0, .L1090+4
	str	r2, [r5, #3464]
	str	r8, [r5, #3468]
	ldr	r2, [r10, r7, lsl #2]
	str	r2, [r5, #3460]
	movs	r2, #1
	mov	r1, r2
	bl	FlashReadPages
	ldrh	r2, [r8, #8]
	cmp	r2, fp
	beq	.L1079
	movw	r2, #611
	ldr	r1, .L1090+8
	ldr	r0, .L1090+12
	bl	sftl_printk
.L1079:
	ldr	r2, [r5, #3456]
	adds	r2, r2, #1
	bne	.L1080
.L1082:
	ldr	r2, [sp, #4]
	movs	r3, #0
	str	r3, [r2]
.L1081:
	b	.L1081
.L1080:
	ldrh	r2, [r8, #8]
	cmp	r2, fp
	bne	.L1082
	ldrh	r2, [r8]
	ldrh	r3, [r4, #4]
	cmp	r2, r3
	bne	.L1082
	ldr	r2, [r5, #3464]
	mov	r1, r7
	mov	r0, r4
	bl	FtlMapWritePage
.L1078:
	adds	r6, r6, #1
	b	.L1077
.L1091:
	.align	2
.L1090:
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
	ldr	r5, .L1093
	muls	r4, r0, r4
	ldr	r3, [r5, #2544]
	addw	r0, r5, #3396
	adds	r2, r3, r4
	ldrh	r1, [r3, r4]
	ldr	r2, [r2, #8]
	bl	FtlMapWritePage
	ldr	r3, [r5, #2544]
	movs	r0, #0
	add	r4, r4, r3
	ldr	r3, [r4, #4]
	bic	r3, r3, #-2147483648
	str	r3, [r4, #4]
	pop	{r3, r4, r5, pc}
.L1094:
	.align	2
.L1093:
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
	ldr	r4, .L1108
	mov	r5, r1
	mov	r7, r2
	ldr	r3, [r4, #2556]
	ldrh	r10, [r4, #312]
	cmp	r0, r3
	bcc	.L1096
	movw	r2, #813
	ldr	r1, .L1108+4
	ldr	r0, .L1108+8
	bl	sftl_printk
.L1096:
	ldr	r3, [r4, #2556]
	cmp	r9, r3
	bcs	.L1097
	add	r10, r10, #7
	ldrh	r2, [r4, #342]
	lsr	r6, r9, r10
	ldr	r1, [r4, #2544]
	movs	r3, #0
	uxth	r6, r6
	mov	fp, #12
.L1098:
	uxth	r8, r3
	cmp	r8, r2
	bcc	.L1103
	bl	select_l2p_ram_region
	mul	fp, fp, r0
	ldr	r3, [r4, #2544]
	mov	r8, r0
	ldrh	r1, [r3, fp]
	add	r2, r3, fp
	movw	r3, #65535
	cmp	r1, r3
	beq	.L1104
	ldr	r3, [r2, #4]
	cmp	r3, #0
	bge	.L1104
	bl	flush_l2p_region
.L1104:
	mov	r1, r8
	mov	r0, r6
	bl	load_l2p_region
	b	.L1100
.L1097:
	mov	r0, #-1
	cbnz	r7, .L1095
	str	r0, [r5]
	pop	{r3, r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L1103:
	adds	r3, r3, #1
	mla	r0, fp, r3, r1
	ldrh	r0, [r0, #-12]
	cmp	r0, r6
	bne	.L1098
.L1100:
	movs	r0, #1
	movs	r3, #12
	lsl	r0, r0, r10
	subs	r0, r0, #1
	and	r0, r0, r9
	uxth	r0, r0
	cbnz	r7, .L1101
	ldr	r2, [r4, #2544]
	mla	r3, r3, r8, r2
	ldr	r3, [r3, #8]
	ldr	r3, [r3, r0, lsl #2]
	str	r3, [r5]
.L1102:
	ldr	r2, [r4, #2544]
	movs	r3, #12
	mla	r8, r3, r8, r2
	ldr	r3, [r8, #4]
	adds	r2, r3, #1
	beq	.L1106
	adds	r3, r3, #1
	str	r3, [r8, #4]
.L1106:
	movs	r0, #0
.L1095:
	pop	{r3, r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L1101:
	mul	r3, r3, r8
	ldr	r2, [r4, #2544]
	ldr	r1, [r5]
	add	r2, r2, r3
	ldr	r2, [r2, #8]
	str	r1, [r2, r0, lsl #2]
	ldr	r2, [r4, #2544]
	strh	r6, [r4, #2548]	@ movhi
	add	r3, r3, r2
	ldr	r2, [r3, #4]
	orr	r2, r2, #-2147483648
	str	r2, [r3, #4]
	b	.L1102
.L1109:
	.align	2
.L1108:
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
	@ args = 0, pretend = 0, frame = 24
	@ frame_needed = 0, uses_anonymous_args = 0
	push	{r4, r5, r6, r7, r8, r9, r10, lr}
	.save {r4, r5, r6, r7, r8, r9, r10, lr}
	.pad #24
	sub	sp, sp, #24
	ldr	r4, .L1128
	ldrh	r2, [r4, #32]
	cmp	r2, #0
	beq	.L1111
	ldrb	r3, [r4, #36]	@ zero_extendqisi2
	cmp	r3, #0
	bne	.L1111
	ldrb	r1, [r4, #35]	@ zero_extendqisi2
	ldrh	r3, [r4, #306]
	muls	r3, r1, r3
	cmp	r2, r3
	beq	.L1111
	ldrb	r7, [r4, #38]	@ zero_extendqisi2
	cbnz	r7, .L1110
	ldr	r6, [r4, #2556]
	mov	r2, r7
	mov	r1, sp
	ldrh	r8, [r4, #236]
	subs	r6, r6, #1
	mov	r0, r6
	bl	log2phys
	ldr	r3, [sp]
	ldr	r5, [r4, #3336]
	ldr	r0, [r4, #3304]
	str	r3, [sp, #8]
	adds	r3, r3, #1
	str	r6, [sp, #20]
	str	r0, [sp, #12]
	str	r5, [sp, #16]
	str	r7, [r5, #4]
	beq	.L1113
	mov	r2, r7
	movs	r1, #1
	add	r0, sp, #4
	bl	FlashReadPages
.L1114:
	ldr	r9, .L1128+4
	movs	r7, #0
	lsl	r8, r8, #2
	mov	r10, r7
	movw	r3, #61589
	strh	r3, [r5]	@ movhi
.L1115:
	cmp	r8, r7
	bne	.L1119
.L1116:
	movs	r3, #1
.L1127:
	strb	r3, [r4, #38]
.L1110:
	add	sp, sp, #24
	@ sp needed
	pop	{r4, r5, r6, r7, r8, r9, r10, pc}
.L1113:
	ldrh	r2, [r4, #314]
	movs	r1, #255
	bl	ftl_memset
	b	.L1114
.L1119:
	ldrh	r3, [r4, #32]
	cmp	r3, #0
	beq	.L1116
	ldr	r3, [sp, #8]
	mov	r0, r9
	str	r6, [r5, #8]
	adds	r7, r7, #1
	str	r3, [r5, #12]
	ldrh	r3, [r4, #28]
	strh	r3, [r5, #2]	@ movhi
	bl	get_new_active_ppa
	ldr	r3, [r4, #2596]
	movs	r1, #1
	str	r0, [sp, #8]
	add	r0, sp, #4
	str	r3, [r5, #4]
	adds	r3, r3, #1
	adds	r2, r3, #1
	it	eq
	moveq	r3, r10
	str	r3, [r4, #2596]
	movs	r3, #0
	mov	r2, r3
	bl	FlashProgPages
	ldrh	r0, [r4, #28]
	bl	decrement_vpc_count
	b	.L1115
.L1111:
	movs	r3, #0
	b	.L1127
.L1129:
	.align	2
.L1128:
	.word	.LANCHOR0
	.word	.LANCHOR0+28
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
	ldr	r5, .L1140
	ubfx	r0, r1, #10, #16
	str	r1, [sp, #4]
	bl	P2V_block_in_plane
	ldr	r2, [r5, #76]
	mov	r7, r0
	ldrh	r3, [r2, r0, lsl #1]
	cbnz	r3, .L1131
	ldr	r4, [r5, #2536]
	cbz	r4, .L1132
	ldr	r1, [r5, #2520]
	mov	ip, #6
	ldr	r2, .L1140+4
	movw	lr, #65535
	ldrh	r0, [r5, #228]
	subs	r4, r4, r1
	asrs	r4, r4, #1
	muls	r4, r2, r4
	uxth	r4, r4
.L1133:
	uxth	r2, r3
	cmp	r0, r2
	bls	.L1132
	cmp	r4, r7
	bne	.L1134
	mov	r1, r4
	ldr	r0, .L1140+8
	bl	List_remove_node
	ldrh	r3, [r5, #228]
	cbnz	r3, .L1135
	movw	r2, #1733
	ldr	r1, .L1140+12
	ldr	r0, .L1140+16
	bl	sftl_printk
.L1135:
	ldrh	r3, [r5, #228]
	mov	r0, r4
	subs	r3, r3, #1
	strh	r3, [r5, #228]	@ movhi
	bl	INSERT_DATA_LIST
	ldr	r2, [r5, #76]
	ldrh	r3, [r2, r7, lsl #1]
.L1131:
	adds	r3, r3, #1
	strh	r3, [r2, r7, lsl #1]	@ movhi
	b	.L1132
.L1134:
	mul	r4, ip, r4
	adds	r3, r3, #1
	ldrh	r4, [r1, r4]
	cmp	r4, lr
	bne	.L1133
.L1132:
	movs	r2, #1
	add	r1, sp, #4
	mov	r0, r6
	bl	log2phys
	add	sp, sp, #12
	@ sp needed
	pop	{r4, r5, r6, r7, pc}
.L1141:
	.align	2
.L1140:
	.word	.LANCHOR0
	.word	-1431655765
	.word	.LANCHOR0+2536
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
	push	{r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.save {r4, r5, r6, r7, r8, r9, r10, fp, lr}
	movw	r2, #65535
	ldrh	r3, [r0]
	.pad #52
	sub	sp, sp, #52
	mov	r10, r0
	cmp	r3, r2
	beq	.L1270
	ldrh	r3, [r0, #2]
	ldr	r4, .L1278
	str	r3, [sp]
	ldrb	r3, [r0, #6]	@ zero_extendqisi2
	ldr	r1, [sp]
	str	r3, [sp, #20]
	ldrh	r3, [r4, #306]
	cmp	r3, r1
	mov	r3, #0
	bne	.L1145
	strh	r3, [r0, #4]	@ movhi
.L1276:
	strb	r3, [r10, #6]
.L1270:
	movs	r0, #0
	add	sp, sp, #52
	@ sp needed
	pop	{r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L1145:
	ldrh	r0, [r0, #16]
.L1146:
	cmp	r0, r2
	uxth	r6, r3
	add	r3, r3, #1
	beq	.L1147
	movs	r1, #1
	bl	FtlGetLastWrittenPage
	adds	r7, r0, #1
	mov	r5, r0
	beq	.L1148
	ldr	r3, [r4, #3180]
	movs	r2, #0
	ldrh	fp, [r4, #236]
	movw	r9, #65535
	ldr	r0, [r4, #3292]
	mov	lr, #20
	str	r3, [sp, #4]
	ldrh	r3, [r4, #314]
	ldr	r8, [r4, #3184]
	ldrh	r7, [r4, #316]
	mov	r4, r2
	str	r3, [sp, #8]
	add	r3, r10, #16
	mov	ip, r3
	str	r3, [sp, #16]
.L1149:
	uxth	r3, r2
	cmp	fp, r3
	bhi	.L1155
	ldr	r6, .L1278
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
.L1156:
	uxth	r2, r3
	cmp	r4, r2
	bhi	.L1161
	bne	.L1159
	adds	r3, r5, #1
	uxth	r3, r3
	str	r3, [sp, #8]
.L1271:
	ldr	r0, [r8, #4]
	ubfx	r0, r0, #10, #16
	bl	P2V_plane
	ldrh	r3, [r6, #306]
	ldr	r2, [sp, #8]
	str	r0, [sp, #4]
	cmp	r3, r2
	bne	.L1163
	ldrh	r3, [sp, #8]
	strh	r3, [r10, #2]	@ movhi
	movs	r3, #0
	strb	r3, [r10, #6]
	strh	r3, [r10, #4]	@ movhi
.L1163:
	ldr	r3, [sp, #8]
	ldr	r2, [sp]
	cmp	r3, r2
	bne	.L1164
	ldr	r3, [sp, #4]
	ldr	r2, [sp, #20]
	cmp	r3, r2
	bne	.L1164
	ldr	r1, [sp, #8]
	mov	r2, r3
.L1277:
	mov	r0, r10
	bl	ftl_sb_update_avl_pages
	b	.L1270
.L1147:
	uxth	r1, r3
	adds	r1, r1, #8
	ldrh	r0, [r10, r1, lsl #1]
	b	.L1146
.L1148:
	ldr	r3, [sp]
	cbz	r3, .L1150
	mov	r2, #1800
	ldr	r1, .L1278+4
	ldr	r0, .L1278+8
	bl	sftl_printk
.L1150:
	ldr	r3, [sp, #20]
	cbz	r3, .L1151
	cmp	r6, r3
	beq	.L1151
	movw	r2, #1801
	ldr	r1, .L1278+4
	ldr	r0, .L1278+8
	bl	sftl_printk
.L1151:
	movs	r3, #0
	strh	r3, [r10, #2]	@ movhi
	b	.L1276
.L1155:
	ldrh	r3, [ip], #2
	cmp	r3, r9
	beq	.L1152
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
.L1152:
	adds	r2, r2, #1
	b	.L1149
.L1161:
	ldr	r2, [r7]
	cbnz	r2, .L1157
	ldr	r2, [r7, #12]
	str	r2, [sp, #4]
	ldr	r2, [r2, #4]
	adds	r0, r2, #1
	beq	.L1158
	ldr	r1, [r6, #2596]
	mov	r0, r2
	bl	ftl_cmp_data_ver
	cbz	r0, .L1158
	adds	r2, r2, #1
	str	r2, [r6, #2596]
.L1158:
	ldr	r2, [sp, #4]
	ldr	r2, [r2]
	adds	r2, r2, #1
	bne	.L1160
.L1159:
	uxth	r2, r5
	uxth	r3, r3
	str	r2, [sp, #8]
	movs	r2, #20
	mla	r8, r2, r3, r8
	b	.L1271
.L1157:
	ldr	r9, [sp, #8]
.L1160:
	adds	r3, r3, #1
	adds	r7, r7, #20
	b	.L1156
.L1164:
	movw	r3, #65535
	cmp	r9, r3
	bne	.L1165
	ldrb	r3, [r10, #8]	@ zero_extendqisi2
	cmp	r3, #0
	bne	.L1166
.L1165:
	ldr	r3, [r6, #3452]
	uxth	r7, r5
	uxth	r5, r5
	movw	r8, #65535
	adds	r3, r3, #1
	ldr	r3, [sp]
	it	eq
	streq	fp, [r6, #3452]
	ldr	r6, .L1278
	adds	r3, r3, #7
	cmp	r5, r3
	itet	gt
	subgt	r4, r7, #7
	ldrle	r4, [sp]
	uxthgt	r4, r4
.L1169:
	cmp	r4, r7
	bhi	.L1176
	movs	r3, #0
	ldrh	lr, [r6, #236]
	ldr	r0, [r6, #3292]
	mov	r5, r3
	ldr	r1, [sp, #16]
	mov	ip, #20
	b	.L1177
.L1171:
	ldrh	r2, [r1], #2
	cmp	r2, r8
	beq	.L1170
	mla	r9, ip, r5, r0
	adds	r5, r5, #1
	orr	r2, r4, r2, lsl #10
	uxth	r5, r5
	str	r2, [r9, #4]
.L1170:
	adds	r3, r3, #1
.L1177:
	uxth	r2, r3
	cmp	lr, r2
	bhi	.L1171
	movs	r2, #0
	mov	r1, r5
	bl	FlashReadPages
	ldr	r3, [r6, #3292]
	movs	r2, #20
	mla	r5, r2, r5, r3
.L1172:
	cmp	r5, r3
	bne	.L1175
	adds	r4, r4, #1
	uxth	r4, r4
	b	.L1169
.L1175:
	ldr	r2, [r3]
	cbnz	r2, .L1166
	ldr	r2, [r3, #12]
	ldrh	r1, [r2]
	cmp	r1, r8
	beq	.L1174
	ldr	r2, [r2, #4]
	adds	r1, r2, #1
	it	ne
	strne	r2, [r6, #3452]
.L1174:
	adds	r3, r3, #20
	b	.L1172
.L1176:
	mov	r3, #-1
	str	r3, [r6, #3452]
.L1166:
	ldr	r8, .L1278
	movs	r3, #1
	ldr	r5, [sp]
	mov	r4, r8
	strh	r3, [r8, #3476]	@ movhi
.L1178:
	ldrh	r6, [r4, #236]
	movw	r9, #65535
	ldr	r0, [r4, #3292]
	mov	ip, #20
	ldr	r1, [sp, #16]
	movs	r3, #0
	str	r3, [sp, #12]
.L1179:
	uxth	r2, r3
	cmp	r6, r2
	bhi	.L1181
	movs	r2, #0
	ldr	r1, [sp, #12]
	bl	FlashReadPages
	movs	r3, #0
.L1275:
	str	r3, [sp, #24]
	ldr	r2, [sp, #12]
	ldrh	r3, [sp, #24]
	cmp	r2, r3
	bhi	.L1211
	adds	r5, r5, #1
	ldrh	r3, [r4, #306]
	uxth	r5, r5
	cmp	r3, r5
	bne	.L1178
	ldrh	r2, [r4, #236]
	movw	r0, #65535
	movs	r3, #0
	strh	r5, [r10, #2]	@ movhi
	strh	r3, [r10, #4]	@ movhi
.L1212:
	uxth	r1, r3
	cmp	r1, r2
	bcs	.L1270
	ldr	r1, [sp, #16]
	ldrh	r4, [r1], #2
	cmp	r4, r0
	str	r1, [sp, #16]
	add	r1, r3, #1
	bne	.L1276
	mov	r3, r1
	b	.L1212
.L1181:
	ldrh	r2, [r1], #2
	cmp	r2, r9
	beq	.L1180
	ldr	r7, [sp, #12]
	orr	r2, r5, r2, lsl #10
	mla	lr, ip, r7, r0
	str	r2, [lr, #4]
	mov	r2, r7
	adds	r2, r2, #1
	uxth	r2, r2
	str	r2, [sp, #12]
.L1180:
	adds	r3, r3, #1
	b	.L1179
.L1279:
	.align	2
.L1278:
	.word	.LANCHOR0
	.word	.LANCHOR1+435
	.word	.LC8
.L1211:
	ldr	r3, [sp, #24]
	movs	r7, #20
	muls	r7, r3, r7
	ldr	r3, [r4, #3292]
	str	r3, [sp, #28]
	add	r9, r3, r7
	ldr	r6, [r9, #4]
	ubfx	r0, r6, #10, #16
	str	r6, [sp, #44]
	bl	P2V_plane
	ldr	r3, [sp]
	cmp	r5, r3
	bcc	.L1183
	ldr	r3, [sp, #28]
	bne	.L1184
	ldr	r2, [sp, #20]
	cmp	r2, r0
	bhi	.L1183
.L1184:
	ldr	r2, [sp, #8]
	cmp	r5, r2
	bne	.L1185
	ldr	r2, [sp, #4]
	cmp	r2, r0
	beq	.L1186
.L1185:
	ldr	r3, [r3, r7]
	adds	r3, r3, #1
	beq	.L1187
	ldr	r9, [r9, #12]
	movw	r3, #61589
	ldrh	r2, [r9]
	cmp	r2, r3
	beq	.L1188
	ldrh	r0, [r10]
.L1272:
	bl	decrement_vpc_count
	b	.L1183
.L1188:
	ldr	fp, [r9, #4]
	cmp	fp, #-1
	beq	.L1189
	ldr	r1, [r4, #2596]
	mov	r0, fp
	bl	ftl_cmp_data_ver
	cbz	r0, .L1189
	add	r3, fp, #1
	str	r3, [r4, #2596]
.L1189:
	ldrh	r2, [r9]
	movw	r3, #61589
	cmp	r2, r3
	beq	.L1190
	mov	r2, #1952
	ldr	r1, .L1280
	ldr	r0, .L1280+4
	bl	sftl_printk
.L1190:
	ldr	r6, [r9, #8]
	add	r1, sp, #40
	ldr	r3, [r9, #12]
	movs	r2, #0
	mov	r0, r6
	str	r3, [sp, #36]
	bl	log2phys
	ldr	r1, [r4, #3452]
	adds	r0, r1, #1
	beq	.L1191
	mov	r0, fp
	bl	ftl_cmp_data_ver
	cmp	r0, #0
	beq	.L1191
	ldr	r3, [sp, #36]
	adds	r2, r3, #1
	beq	.L1192
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
	bne	.L1193
.L1194:
	mov	r3, #-1
	str	r3, [sp, #36]
.L1201:
	ldr	r0, [sp, #36]
	adds	r1, r0, #1
	beq	.L1183
.L1215:
	ubfx	r0, r0, #10, #16
	bl	P2V_block_in_plane
	ldrh	r3, [r4, #244]
	mov	r6, r0
	cmp	r3, r0
	bhi	.L1207
	movw	r2, #2057
	ldr	r1, .L1280
	ldr	r0, .L1280+4
	bl	sftl_printk
.L1207:
	ldr	r3, [r8, #76]
	ldrh	r3, [r3, r6, lsl #1]
	cmp	r3, #0
	beq	.L1208
	mov	r0, r6
	b	.L1272
.L1192:
	ldr	r3, [sp, #44]
	ldr	r2, [sp, #40]
	cmp	r2, r3
	bne	.L1183
	movs	r2, #1
	add	r1, sp, #36
	mov	r0, r6
	bl	log2phys
.L1183:
	ldr	r3, [sp, #24]
	adds	r3, r3, #1
	b	.L1275
.L1193:
	ldr	r1, [r9, #8]
	cmp	r6, r1
	bne	.L1194
	ldr	r1, [r9, #4]
	ldr	r0, [r4, #3452]
	str	r1, [sp, #28]
	bl	ftl_cmp_data_ver
	cmp	r0, #0
	beq	.L1194
	ldr	r1, [sp, #40]
	ldr	r0, [sp, #44]
	cmp	r1, r0
	bne	.L1196
.L1199:
	ldr	r1, [sp, #36]
	mov	r0, r6
	bl	FtlReUsePrevPpa
	b	.L1194
.L1196:
	ldr	r0, [sp, #36]
	cmp	r1, r0
	beq	.L1194
	adds	r0, r1, #1
	beq	.L1197
	str	r1, [r3, #4]
	movs	r2, #0
	movs	r1, #1
	mov	r0, r3
	ldr	r9, [r3, #12]
	bl	FlashReadPages
.L1198:
	ldr	r3, [r4, #3292]
	ldr	r3, [r3, r7]
	adds	r3, r3, #1
	beq	.L1199
	ldr	r3, [r9, #4]
	ldr	r0, [r4, #3452]
	mov	r1, r3
	bl	ftl_cmp_data_ver
	cmp	r0, #0
	beq	.L1199
	mov	r1, r3
	ldr	r0, [sp, #28]
	bl	ftl_cmp_data_ver
	cmp	r0, #0
	beq	.L1194
	b	.L1199
.L1197:
	str	r1, [r2, r7]
	b	.L1198
.L1191:
	ldr	r3, [sp, #44]
	ldr	r2, [sp, #40]
	cmp	r2, r3
	beq	.L1201
	ldr	r1, [sp, #36]
	adds	r0, r1, #1
	beq	.L1203
	ldr	r3, [r8, #252]
	cmp	r3, r1, lsr #10
	bhi	.L1203
	ldr	r0, .L1280+8
.L1274:
	bl	sftl_printk
	b	.L1183
.L1203:
	movs	r2, #1
	add	r1, sp, #44
	mov	r0, r6
	bl	log2phys
	ldr	r9, [sp, #40]
	cmp	r9, #-1
	beq	.L1201
	ldr	r3, [sp, #36]
	cmp	r9, r3
	beq	.L1205
	ubfx	r0, r9, #10, #16
	bl	P2V_block_in_plane
	ldrh	r3, [r4, #28]
	cmp	r3, r0
	beq	.L1206
	ldrh	r3, [r4, #80]
	cmp	r3, r0
	beq	.L1206
	ldrh	r3, [r4, #128]
	cmp	r3, r0
	bne	.L1201
.L1206:
	ldr	r0, [r8, #3292]
	movs	r2, #0
	movs	r1, #1
	str	r9, [r0, #4]
	ldr	r7, [r0, #12]
	bl	FlashReadPages
	ldr	r3, [r8, #3292]
	ldr	r3, [r3]
	adds	r3, r3, #1
	beq	.L1201
	ldr	r1, [r7, #4]
	mov	r0, fp
	bl	ftl_cmp_data_ver
	cmp	r0, #0
	bne	.L1201
	movs	r2, #1
	add	r1, sp, #40
	mov	r0, r6
	bl	log2phys
	b	.L1201
.L1208:
	mov	r1, r6
	ldr	r0, .L1280+12
	b	.L1274
.L1187:
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
	bne	.L1210
.L1273:
	str	fp, [r4, #3452]
	b	.L1183
.L1210:
	cmp	fp, r3
	bcs	.L1183
	b	.L1273
.L1186:
	ldrb	r3, [sp, #4]	@ zero_extendqisi2
	mov	r1, r5
	strh	r5, [r10, #2]	@ movhi
	ldr	r2, [sp, #4]
	strb	r3, [r10, #6]
	b	.L1277
.L1205:
	mov	r0, r9
	b	.L1215
.L1281:
	.align	2
.L1280:
	.word	.LANCHOR1+435
	.word	.LC8
	.word	.LC109
	.word	.LC110
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
	@ args = 0, pretend = 0, frame = 8
	@ frame_needed = 0, uses_anonymous_args = 0
	push	{r0, r1, r2, r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.save {r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.pad #12
	movs	r4, #0
	ldr	r6, .L1306
	ldr	r7, .L1306+4
	ldr	r1, .L1306+8
	mov	r5, r6
	ldr	r0, .L1306+12
	bl	sftl_printk
	mov	r1, #8192
	ldr	r0, .L1306+4
	bl	__memzero
.L1283:
	ldr	r3, [r6, #2556]
	cmp	r4, r3
	bcc	.L1285
	ldr	r8, .L1306+4
	movs	r4, #0
	ldr	r10, .L1306+24
	mov	r7, r4
	movw	r9, #65535
.L1286:
	ldrh	r2, [r5, #244]
	uxth	r3, r4
	cmp	r2, r3
	bhi	.L1288
	ldr	r4, [r5, #2536]
	cbz	r4, .L1289
	ldr	r3, [r5, #2520]
	movs	r6, #0
	ldrh	r8, [r5, #228]
	mov	fp, #6
	ldr	r9, .L1306+4
	subs	r4, r4, r3
	ldr	r3, .L1306+16
	asrs	r4, r4, #1
	ldr	r10, .L1306+28
	muls	r4, r3, r4
	uxth	r4, r4
.L1290:
	uxth	r3, r6
	cmp	r8, r3
	bls	.L1289
	ldr	r3, [r5, #76]
	ldrh	r2, [r3, r4, lsl #1]
	cbz	r2, .L1291
	movs	r7, #1
	ldrh	r3, [r9, r4, lsl #1]
	mov	r1, r4
	mov	r0, r10
	bl	sftl_printk
.L1291:
	mul	r4, fp, r4
	ldr	r3, [r5, #2520]
	adds	r6, r6, #1
	ldrh	r4, [r3, r4]
	movw	r3, #65535
	cmp	r4, r3
	bne	.L1290
.L1289:
	cbz	r7, .L1282
	movw	r2, #2383
	ldr	r1, .L1306+8
	ldr	r0, .L1306+20
	bl	sftl_printk
.L1282:
	add	sp, sp, #12
	@ sp needed
	pop	{r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L1285:
	movs	r2, #0
	add	r1, sp, #4
	mov	r0, r4
	bl	log2phys
	ldr	r0, [sp, #4]
	adds	r3, r0, #1
	beq	.L1284
	ubfx	r0, r0, #10, #16
	bl	P2V_block_in_plane
	ldrh	r3, [r7, r0, lsl #1]
	adds	r3, r3, #1
	strh	r3, [r7, r0, lsl #1]	@ movhi
.L1284:
	adds	r4, r4, #1
	b	.L1283
.L1288:
	ldr	r3, [r5, #76]
	uxth	r6, r4
	ldrh	r2, [r3, r6, lsl #1]
	ldrh	r3, [r8, r6, lsl #1]
	cmp	r2, r3
	beq	.L1287
	mov	r1, r6
	mov	r0, r10
	bl	sftl_printk
	ldr	r3, [r5, #76]
	ldrh	r3, [r3, r6, lsl #1]
	cmp	r3, r9
	beq	.L1287
	ldrh	r2, [r8, r6, lsl #1]
	cmp	r2, r3
	it	hi
	movhi	r7, #1
.L1287:
	adds	r4, r4, #1
	b	.L1286
.L1307:
	.align	2
.L1306:
	.word	.LANCHOR0
	.word	check_vpc_table
	.word	.LANCHOR1+457
	.word	.LC111
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
	push	{r4, r5, r6, r7, r8, lr}
	.save {r4, r5, r6, r7, r8, lr}
	movs	r5, #0
	ldr	r6, .L1322
	.pad #32
	sub	sp, sp, #32
	movs	r1, #0
	ldr	r8, .L1322+16
	mov	r4, r6
	ldr	r0, .L1322+4
	bl	sftl_printk
.L1309:
	ldr	r3, [r6, #2556]
	cmp	r5, r3
	bcc	.L1315
	add	sp, sp, #32
	@ sp needed
	pop	{r4, r5, r6, r7, r8, pc}
.L1315:
	movs	r2, #0
	add	r1, sp, #28
	mov	r0, r5
	bl	log2phys
	ubfx	r3, r5, #0, #11
	cbnz	r3, .L1310
	ldr	r2, [sp, #28]
	mov	r1, r5
	mov	r0, r8
	bl	sftl_printk
.L1310:
	ldr	r3, [sp, #28]
	adds	r2, r3, #1
	beq	.L1312
	str	r3, [r4, #3460]
	movs	r2, #0
	ldr	r3, [r4, #3304]
	movs	r1, #1
	ldr	r7, [r4, #3336]
	ldr	r0, .L1322+8
	str	r3, [r4, #3464]
	str	r5, [r4, #3472]
	str	r7, [r4, #3468]
	str	r2, [r4, #3456]
	bl	FlashReadPages
	ldr	r3, [r4, #3456]
	cmp	r3, #256
	beq	.L1313
	adds	r3, r3, #1
	beq	.L1313
	ldr	r3, [r7, #8]
	cmp	r5, r3
	beq	.L1312
.L1313:
	ldr	r2, [r4, #3464]
	ldr	r3, [r4, #3468]
	ldr	r0, .L1322+12
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
.L1312:
	adds	r5, r5, #1
	b	.L1309
.L1323:
	.align	2
.L1322:
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
	@ args = 0, pretend = 0, frame = 40
	@ frame_needed = 0, uses_anonymous_args = 0
	ldr	r3, .L1361
	push	{r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.save {r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.pad #44
	sub	sp, sp, #44
	mov	r5, r0
	str	r1, [sp, #4]
	ldrh	r6, [r3, #4]
	movw	r3, #65535
	cmp	r6, r3
	beq	.L1351
	cbnz	r6, .L1325
.L1326:
	bl	FtlGcPageVarInit
	b	.L1327
.L1351:
	movs	r6, #0
.L1325:
	ldr	r3, .L1361+4
	ldr	r2, [sp, #4]
	ldrh	r3, [r3, #306]
	cmp	r3, r2
	beq	.L1326
.L1327:
	mov	fp, #0
.L1345:
	ldrh	r2, [r5]
	movs	r3, #0
	strb	r3, [r5, #8]
	movw	r3, #65535
	cmp	r2, r3
	beq	.L1328
	ldr	r4, .L1361+4
.L1348:
	ldr	r3, [r4, #3292]
	movs	r2, #0
	ldrh	ip, [r4, #236]
	add	lr, r5, #16
	ldrh	r10, [r4, #314]
	mov	r7, r2
	str	r3, [sp]
	movw	r8, #65535
	ldr	r3, [r4, #3180]
	ldr	r9, [r4, #3184]
	str	r3, [sp, #8]
	ldrh	r3, [r4, #316]
	str	r3, [sp, #12]
.L1329:
	uxth	r3, r2
	cmp	ip, r3
	bhi	.L1333
	mov	r10, #0
	movs	r2, #0
	mov	r1, r7
	ldr	r0, [sp]
	bl	FlashReadPages
.L1334:
	uxth	r3, r10
	cmp	r7, r3
	bhi	.L1346
	ldr	r3, [sp, #4]
	add	fp, fp, #1
	adds	r6, r6, #1
	uxth	r6, r6
	cmp	r3, fp
	bls	.L1347
.L1349:
	ldrh	r3, [r4, #306]
	cmp	r3, r6
	bhi	.L1348
.L1328:
	ldr	r3, .L1361
	movw	r2, #65535
	strh	r6, [r5, #2]	@ movhi
	mov	r1, r6
	mov	r0, r5
	strh	r2, [r3, #4]	@ movhi
	movs	r2, #0
	strb	r2, [r5, #6]
	bl	ftl_sb_update_avl_pages
	b	.L1350
.L1333:
	ldrh	r3, [lr], #2
	cmp	r3, r8
	beq	.L1330
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
.L1330:
	adds	r2, r2, #1
	b	.L1329
.L1346:
	movs	r3, #20
	ldr	r2, [r4, #3292]
	mul	r3, r3, r10
	adds	r1, r2, r3
	ldr	r8, [r2, r3]
	ldr	r0, [r1, #4]
	ldr	r9, [r1, #12]
	str	r0, [sp]
	cmp	r8, #0
	bne	.L1335
	ldrh	r1, [r9]
	movw	r2, #65535
	cmp	r1, r2
	bne	.L1336
.L1359:
	ldrh	r2, [r5]
	movs	r1, #0
	ldr	r3, [r4, #76]
	movs	r6, #0
	strh	r1, [r3, r2, lsl #1]	@ movhi
	ldrh	r0, [r5]
	bl	INSERT_FREE_LIST
	movw	r3, #65535
	strh	r3, [r5]	@ movhi
	strh	r3, [r4, #176]	@ movhi
	bl	FtlGcPageVarInit
	b	.L1345
.L1336:
	ldr	r0, [r9, #8]
	ldr	r2, [r4, #2556]
	str	r3, [sp, #8]
	cmp	r0, r2
	bhi	.L1359
	mov	r2, r8
	add	r1, sp, #16
	bl	log2phys
	ldr	r2, [r9, #12]
	ldr	r1, [sp, #16]
	ldr	r3, [sp, #8]
	cmp	r2, r1
	beq	.L1339
.L1341:
	ldr	r2, [r9, #8]
.L1360:
	ldr	r1, [sp]
	add	r10, r10, #1
	ldr	r0, [r9, #12]
	bl	FtlGcUpdatePage
	b	.L1334
.L1339:
	str	r3, [sp, #8]
	adds	r3, r2, #1
	beq	.L1341
	str	r2, [sp, #24]
	movs	r1, #1
	ldr	r2, [r4, #3320]
	add	r0, sp, #20
	str	r2, [sp, #28]
	ldr	r2, [r4, #3340]
	str	r2, [sp, #32]
	mov	r2, r8
	bl	FlashReadPages
	ldrh	r2, [r4, #262]
	ldr	r1, [r4, #3292]
	ldr	r3, [sp, #8]
	ldr	r0, [sp, #28]
	lsls	r2, r2, #7
	add	ip, r3, r1
.L1342:
	cmp	r8, r2
	beq	.L1341
	ldr	r1, [ip, #8]
	ldr	r3, [r0, r8, lsl #2]
	ldr	r1, [r1, r8, lsl #2]
	cmp	r1, r3
	beq	.L1343
	ldr	r2, [sp, #24]
	ldrh	r1, [r5]
	ldr	r0, .L1361+8
	bl	sftl_printk
	b	.L1359
.L1343:
	add	r8, r8, #1
	b	.L1342
.L1335:
	mov	r2, #-1
	b	.L1360
.L1347:
	ldr	r2, .L1361
	movw	r1, #65535
	ldrh	r3, [r2, #4]
	cmp	r3, r1
	beq	.L1349
	add	r3, r3, fp
	strh	r3, [r2, #4]	@ movhi
	ldrh	r3, [r4, #306]
	cmp	r3, r6
	bls	.L1349
.L1350:
	mov	r0, #-1
	add	sp, sp, #44
	@ sp needed
	pop	{r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L1362:
	.align	2
.L1361:
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
	@ args = 0, pretend = 0, frame = 88
	@ frame_needed = 0, uses_anonymous_args = 0
	push	{r4, r5, r6, r7, r8, r9, lr}
	.save {r4, r5, r6, r7, r8, r9, lr}
	.pad #92
	sub	sp, sp, #92
	ldr	r6, .L1379
	ldr	r9, [r6, #2724]
	mov	r5, r6
	cmp	r9, #0
	beq	.L1364
	ldr	r2, [r6, #2728]
	ldr	r3, [r6, #2556]
	cmp	r2, r3
	bcs	.L1365
	mov	r4, #2048
.L1370:
	ldr	r0, [r5, #2728]
	ldr	r3, [r5, #2556]
	cmp	r0, r3
	bcc	.L1366
.L1369:
	mov	r0, #-1
.L1363:
	add	sp, sp, #92
	@ sp needed
	pop	{r4, r5, r6, r7, r8, r9, pc}
.L1366:
	movs	r2, #0
	mov	r1, sp
	bl	log2phys
	ldr	r2, [sp]
	ldr	r3, [r5, #2728]
	adds	r1, r2, #1
	add	r3, r3, #1
	str	r3, [r5, #2728]
	beq	.L1368
	str	r3, [sp, #20]
	add	r0, sp, #88
	ldr	r3, [r5, #3328]
	movs	r1, #1
	str	r2, [sp, #8]
	movs	r2, #0
	str	r2, [r0, #-84]!
	str	r3, [sp, #12]
	add	r3, sp, #24
	str	r3, [sp, #16]
	bl	FlashReadPages
	ldr	r3, [sp, #4]
	cmp	r3, #256
	bne	.L1369
	ldr	r0, [sp]
	ubfx	r0, r0, #10, #16
	bl	P2V_block_in_plane
	bl	FtlGcRefreshBlock
	b	.L1369
.L1368:
	subs	r4, r4, #1
	bne	.L1370
	b	.L1369
.L1365:
	ldr	r3, [r6, #2560]
	movs	r0, #0
	str	r0, [r6, #2724]
	str	r0, [r6, #2728]
	str	r3, [r6, #2720]
	b	.L1363
.L1364:
	ldr	r8, [r6, #2560]
	movw	r4, #10000
	ldr	r1, [r6, #2616]
	ldr	r7, [r6, #2720]
	add	r3, r8, #1048576
	cmp	r1, r4
	ite	hi
	movhi	r4, #31
	movls	r4, #63
	cmp	r7, r3
	bhi	.L1374
	ldr	r3, [r6, #2556]
	lsrs	r1, r1, #10
	mov	r0, #1000
	adds	r1, r1, #1
	muls	r0, r3, r0
	bl	__aeabi_uidiv
	add	r0, r0, r7
	cmp	r8, r0
	bhi	.L1374
	ldrh	r3, [r6, #2500]
	ands	r0, r4, r3
	bne	.L1376
	ldr	r2, [r6, #2744]
	cmp	r3, r2
	beq	.L1363
.L1374:
	ldrh	r3, [r5, #2500]
	movs	r0, #0
	str	r0, [r5, #2728]
	str	r8, [r5, #2720]
	str	r3, [r5, #2744]
	movs	r3, #1
	str	r3, [r5, #2724]
	b	.L1363
.L1376:
	mov	r0, r9
	b	.L1363
.L1380:
	.align	2
.L1379:
	.word	.LANCHOR0
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
	ldrh	r3, [r5, #342]
	uxth	r0, r4
	cmp	r3, r0
	bhi	.L1384
	movs	r0, #0
	pop	{r4, r5, r6, pc}
.L1384:
	ldr	r2, [r5, #2544]
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
	push	{r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.save {r4, r5, r6, r7, r8, r9, r10, fp, lr}
	mov	r10, r2
	ldr	r4, .L1399
	adds	r2, r0, r1
	.pad #108
	sub	sp, sp, #108
	mov	r8, r0
	mov	r6, r1
	ldrh	r3, [r4, #300]
	cmp	r2, r3
	bhi	.L1395
	ldrh	r7, [r4, #312]
	mov	r9, #0
	lsr	r7, r0, r7
	lsl	fp, r7, #2
.L1389:
	cbnz	r6, .L1394
.L1387:
	mov	r0, r9
	add	sp, sp, #108
	@ sp needed
	pop	{r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L1394:
	ldr	r3, [r4, #3376]
	mov	r0, r8
	ldr	r2, [r3, fp]
	ldrh	r3, [r4, #262]
	str	r2, [sp, #12]
	mov	r1, r3
	str	r3, [sp, #8]
	bl	__aeabi_uidivmod
	ldr	r3, [sp, #8]
	ldr	r2, [sp, #12]
	str	r1, [sp, #4]
	subs	r5, r3, r1
	uxth	r5, r5
	cmp	r6, r5
	it	cc
	uxthcc	r5, r6
	cbz	r2, .L1391
	cmp	r5, r3
	beq	.L1391
	ldr	r3, [r4, #3312]
	add	r0, sp, #20
	str	r2, [sp, #24]
	movs	r2, #1
	mov	r1, r2
	str	r3, [sp, #28]
	add	r3, sp, #40
	str	r3, [sp, #32]
	bl	FlashReadPages
.L1392:
	lsls	r3, r5, #9
	ldr	r0, [r4, #3312]
	mov	r1, r10
	mov	r2, r3
	str	r3, [sp, #8]
	ldr	r3, [sp, #4]
	subs	r6, r6, r5
	add	r8, r8, r5
	add	fp, fp, #4
	add	r0, r0, r3, lsl #9
	bl	ftl_memcpy
	mov	r1, r7
	ldr	r2, [r4, #3312]
	ldr	r0, .L1399+4
	adds	r7, r7, #1
	bl	FtlMapWritePage
	ldr	r3, [sp, #8]
	adds	r0, r0, #1
	it	eq
	moveq	r9, #-1
	add	r10, r10, r3
	b	.L1389
.L1391:
	ldrh	r2, [r4, #314]
	movs	r1, #0
	ldr	r0, [r4, #3312]
	bl	ftl_memset
	b	.L1392
.L1395:
	mov	r9, #-1
	b	.L1387
.L1400:
	.align	2
.L1399:
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
	ldr	r2, .L1403
	ldr	r3, .L1403+4
	ldr	r1, [r2, #2644]
	cmp	r1, r3
	bne	.L1401
	ldr	r3, .L1403+8
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
.L1401:
	bx	lr
.L1404:
	.align	2
.L1403:
	.word	.LANCHOR0
	.word	1179929683
	.word	1342177352
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
	ldr	r3, .L1410
	ldrh	r2, [r3, #3656]
	cmp	r2, #31
	itett	ls
	addls	r2, r2, #1
	movhi	r2, #32
	strhls	r2, [r3, #3656]	@ movhi
	movls	r2, #1
	cbnz	r0, .L1407
	ldr	r1, [r3, #3352]
	ldr	r0, [r1, #20]
	ldr	r1, [r1, #16]
	add	r2, r2, r0
	cmp	r1, r2
	bcc	.L1408
.L1407:
	ldr	r2, [r3, #3352]
	movs	r0, #64
	ldr	r1, [r2, #16]
	str	r1, [r2, #20]
	ldr	r1, .L1410+4
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
.L1408:
	movs	r0, #0
	pop	{r3, pc}
.L1411:
	.align	2
.L1410:
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
	push	{r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.save {r4, r5, r6, r7, r8, r9, r10, fp, lr}
	mov	r10, r2
	ldr	r4, .L1423
	adds	r2, r0, r1
	.pad #108
	sub	sp, sp, #108
	mov	r8, r0
	mov	r7, r1
	ldrh	r3, [r4, #300]
	cmp	r2, r3
	bhi	.L1422
	ldrh	r6, [r4, #312]
	mov	r9, #0
	lsr	r6, r0, r6
	lsl	fp, r6, #2
.L1415:
	cbnz	r7, .L1421
.L1413:
	mov	r0, r9
	add	sp, sp, #108
	@ sp needed
	pop	{r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L1421:
	ldr	r3, [r4, #3376]
	mov	r0, r8
	ldrh	r5, [r4, #262]
	ldr	r3, [r3, fp]
	mov	r1, r5
	str	r3, [sp, #8]
	bl	__aeabi_uidivmod
	subs	r5, r5, r1
	ldr	r3, [sp, #8]
	uxth	r5, r5
	str	r1, [sp, #4]
	cmp	r7, r5
	it	cc
	uxthcc	r5, r7
	lsls	r2, r5, #9
	str	r2, [sp, #8]
	cbz	r3, .L1417
	ldr	r2, [r4, #3312]
	add	r0, sp, #20
	str	r3, [sp, #24]
	str	r3, [sp, #12]
	str	r2, [sp, #28]
	add	r2, sp, #40
	str	r2, [sp, #32]
	movs	r2, #1
	mov	r1, r2
	bl	FlashReadPages
	ldr	r2, [sp, #20]
	ldr	r3, [sp, #12]
	adds	r2, r2, #1
	ldr	r2, [r4, #3456]
	it	eq
	moveq	r9, #-1
	cmp	r2, #256
	bne	.L1419
	mov	r2, r3
	mov	r1, r6
	ldr	r0, .L1423+4
	bl	sftl_printk
	ldr	r2, [r4, #3312]
	mov	r1, r6
	ldr	r0, .L1423+8
	bl	FtlMapWritePage
.L1419:
	ldr	r1, [r4, #3312]
	lsls	r2, r5, #9
	ldr	r3, [sp, #4]
	mov	r0, r10
	add	r1, r1, r3, lsl #9
	bl	ftl_memcpy
.L1420:
	ldr	r3, [sp, #8]
	adds	r6, r6, #1
	subs	r7, r7, r5
	add	r8, r8, r5
	add	fp, fp, #4
	add	r10, r10, r3
	b	.L1415
.L1417:
	lsls	r2, r5, #9
	mov	r1, r3
	mov	r0, r10
	bl	ftl_memset
	b	.L1420
.L1422:
	mov	r9, #-1
	b	.L1413
.L1424:
	.align	2
.L1423:
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
	ldr	r4, .L1427
	ldr	r2, [r4, #3352]
	ldrh	r1, [r4, #3344]
	bl	FtlVendorPartRead
	ldr	r3, [r4, #3352]
	ldr	r2, [r3]
	ldr	r3, .L1427+4
	cmp	r2, r3
	beq	.L1426
	ldr	r1, .L1427+8
	ldr	r0, .L1427+12
	bl	sftl_printk
	ldrh	r2, [r4, #3344]
	movs	r1, #0
	ldr	r0, [r4, #3352]
	lsls	r2, r2, #9
	bl	ftl_memset
.L1426:
	movs	r0, #0
	pop	{r4, pc}
.L1428:
	.align	2
.L1427:
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
	ldr	r4, .L1432
	movs	r0, #0
	ldr	r5, .L1432+4
	addw	r2, r4, #2644
	bl	FtlVendorPartRead
	ldr	r3, [r4, #2644]
	cmp	r3, r5
	beq	.L1430
	mov	r1, #512
	addw	r0, r4, #2644
	bl	__memzero
	str	r5, [r4, #2644]
.L1430:
	ldr	r3, [r4, #2644]
	cmp	r3, r5
	bne	.L1431
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
.L1431:
	ldrh	r2, [r4, #296]
	ldr	r3, [r4, #2604]
	ldr	r0, [r4, #2600]
	ldrh	r1, [r4, #244]
	mla	r0, r0, r2, r3
	bl	__aeabi_uidiv
	str	r0, [r4, #2608]
	pop	{r3, r4, r5, pc}
.L1433:
	.align	2
.L1432:
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
	cbz	r3, .L1435
	ldrh	r5, [r0, #6]
	movs	r3, #0
	ldr	r4, .L1439
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
	cbz	r2, .L1437
	movs	r2, #1
	add	r0, r4, #3456
	mov	r1, r2
	bl	FlashReadPages
.L1438:
	ldr	r2, [r4, #3464]
	mov	r1, r5
	mov	r0, r6
	pop	{r4, r5, r6, lr}
	b	FtlMapWritePage
.L1437:
	ldrh	r2, [r4, #314]
	movs	r1, #255
	bl	ftl_memset
	b	.L1438
.L1435:
	pop	{r4, r5, r6, pc}
.L1440:
	.align	2
.L1439:
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
	ldr	r4, .L1456
	movs	r1, #255
	movw	r7, #65535
	ldr	r9, .L1456+16
	ldr	r3, [r4, #3304]
	add	r8, r4, #3456
	ldr	r6, [r4, #3336]
	ldr	r10, .L1456+20
	str	r3, [r4, #3464]
	ldrh	r3, [r4, #2628]
	str	r6, [r4, #3468]
	str	r5, [r6, #12]
	strh	r3, [r6, #2]	@ movhi
	movw	r3, #61604
	strh	r3, [r6]	@ movhi
	ldr	r3, [r4, #2636]
	str	r5, [r6, #8]
	ldrh	r2, [r4, #30]
	str	r3, [r6, #4]
	ldr	r3, .L1456+4
	str	r3, [r4, #2472]
	ldr	r3, .L1456+8
	str	r3, [r4, #2476]
	ldrh	r3, [r4, #2634]
	strh	r3, [r4, #2480]	@ movhi
	ldrh	r3, [r4, #258]
	strb	r3, [r4, #2482]
	ldrh	r3, [r4, #28]
	strh	r3, [r4, #2486]	@ movhi
	ldrb	r3, [r4, #34]	@ zero_extendqisi2
	orr	r3, r3, r2, lsl #6
	ldrh	r2, [r4, #82]
	strh	r3, [r4, #2488]	@ movhi
	ldrb	r3, [r4, #36]	@ zero_extendqisi2
	strb	r3, [r4, #2483]
	ldrh	r3, [r4, #80]
	strh	r3, [r4, #2490]	@ movhi
	ldrb	r3, [r4, #86]	@ zero_extendqisi2
	orr	r3, r3, r2, lsl #6
	strh	r3, [r4, #2492]	@ movhi
	ldrb	r3, [r4, #88]	@ zero_extendqisi2
	strb	r3, [r4, #2484]
	ldrh	r3, [r4, #128]
	ldrh	r2, [r4, #130]
	ldr	r0, [r4, #3464]
	strh	r3, [r4, #2494]	@ movhi
	ldrb	r3, [r4, #134]	@ zero_extendqisi2
	orr	r3, r3, r2, lsl #6
	ldrh	r2, [r4, #314]
	strh	r3, [r4, #2496]	@ movhi
	ldrb	r3, [r4, #136]	@ zero_extendqisi2
	strb	r3, [r4, #2485]
	ldr	r3, [r4, #2600]
	str	r3, [r4, #2504]
	ldr	r3, [r4, #2592]
	str	r3, [r4, #2512]
	ldr	r3, [r4, #2596]
	str	r3, [r4, #2508]
	bl	ftl_memset
	movs	r2, #48
	addw	r1, r4, #2472
	ldr	r0, [r4, #3464]
	bl	ftl_memcpy
	ldrh	r2, [r4, #244]
	ldr	r0, [r4, #3464]
	ldr	r1, [r4, #76]
	lsls	r2, r2, #1
	adds	r0, r0, #48
	bl	ftl_memcpy
	ldrh	r0, [r4, #244]
	ldr	r3, [r4, #3464]
	ldr	r1, [r4, #24]
	lsrs	r2, r0, #3
	adds	r0, r0, #24
	lsls	r0, r0, #1
	adds	r2, r2, #4
	bic	r0, r0, #3
	add	r0, r0, r3
	bl	ftl_memcpy
	mov	r0, r5
	bl	FtlUpdateVaildLpn
.L1442:
	ldr	r3, [r4, #3304]
	ldrh	r1, [r4, #2630]
	ldrh	r2, [r4, #2628]
	str	r3, [r4, #3464]
	ldr	r3, [r4, #3336]
	str	r3, [r4, #3468]
	orr	r3, r1, r2, lsl #10
	str	r3, [r4, #3460]
	ldrh	r3, [r4, #308]
	subs	r3, r3, #1
	cmp	r1, r3
	blt	.L1443
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
.L1443:
	ldrh	r1, [r4, #314]
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
	bne	.L1444
	cmp	r3, #1
	bne	.L1445
	movw	r2, #1138
	mov	r1, r9
	mov	r0, r10
	bl	sftl_printk
.L1445:
	ldrh	r3, [r4, #2630]
	adds	r5, r5, #1
	uxth	r5, r5
	cmp	r3, #1
	ittt	eq
	ldrheq	r3, [r4, #308]
	addeq	r3, r3, #-1
	strheq	r3, [r4, #2630]	@ movhi
	cmp	r5, #3
	bls	.L1442
	mov	r2, r5
	ldr	r1, [r4, #3460]
	ldr	r0, .L1456+12
	bl	sftl_printk
.L1448:
	b	.L1448
.L1444:
	cmp	r3, #1
	beq	.L1442
	cmp	r2, #256
	beq	.L1442
	movw	r3, #65535
	cmp	r7, r3
	beq	.L1449
	movs	r1, #1
	mov	r0, r7
	bl	FtlFreeSysBlkQueueIn
.L1449:
	movs	r0, #0
	pop	{r4, r5, r6, r7, r8, r9, r10, pc}
.L1457:
	.align	2
.L1456:
	.word	.LANCHOR0
	.word	1179929683
	.word	1342177352
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
	ldr	r3, .L1461
	ldr	r3, [r3]
	cmp	r3, #1
	bne	.L1460
	bl	FtlSysFlush
.L1460:
	movs	r0, #0
	pop	{r3, pc}
.L1462:
	.align	2
.L1461:
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
	@ args = 0, pretend = 0, frame = 8
	@ frame_needed = 0, uses_anonymous_args = 0
	push	{r0, r1, r4, r5, r6, r7, r8, lr}
	.save {r4, r5, r6, r7, r8, lr}
	.pad #8
	adds	r2, r0, r1
	ldr	r4, .L1478
	mov	r8, r0
	mov	r6, r1
	ldr	r3, [r4, #344]
	cmp	r2, r3
	bhi	.L1471
	cmp	r1, #31
	bhi	.L1465
.L1470:
	movs	r0, #0
.L1463:
	add	sp, sp, #8
	@ sp needed
	pop	{r4, r5, r6, r7, r8, pc}
.L1465:
	ldrh	r5, [r4, #262]
	mov	r1, r5
	bl	__aeabi_uidiv
	smulbb	r3, r0, r5
	mov	r7, r0
	sub	r8, r8, r3
	uxth	r3, r8
	cbz	r3, .L1466
	subs	r5, r5, r3
	adds	r7, r7, #1
	cmp	r5, r6
	it	cs
	movcs	r5, r6
	uxth	r5, r5
	subs	r6, r6, r5
.L1466:
	mov	r3, #-1
	str	r3, [sp, #4]
.L1467:
	ldrh	r3, [r4, #262]
	cmp	r6, r3
	bcs	.L1469
	ldr	r3, [r4, #3660]
	cmp	r3, #32
	bls	.L1470
	movs	r5, #0
	str	r5, [r4, #3660]
	bl	l2p_flush
	bl	FtlVpcTblFlush
	b	.L1470
.L1469:
	movs	r2, #0
	mov	r1, sp
	mov	r0, r7
	bl	log2phys
	ldr	r3, [sp]
	adds	r3, r3, #1
	beq	.L1468
	ldr	r3, [r4, #3660]
	movs	r2, #1
	add	r1, sp, #4
	mov	r0, r7
	adds	r3, r3, #1
	str	r3, [r4, #3660]
	ldr	r3, [r4, #2564]
	adds	r3, r3, #1
	str	r3, [r4, #2564]
	bl	log2phys
	ldr	r0, [sp]
	ubfx	r0, r0, #10, #16
	bl	P2V_block_in_plane
	bl	decrement_vpc_count
.L1468:
	ldrh	r3, [r4, #262]
	adds	r7, r7, #1
	subs	r6, r6, r3
	b	.L1467
.L1471:
	mov	r0, #-1
	b	.L1463
.L1479:
	.align	2
.L1478:
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
	push	{r0, r1, r2, r4, r5, r6, r7, r8, r9, lr}
	.save {r4, r5, r6, r7, r8, r9, lr}
	.pad #12
	movs	r5, #0
	ldr	r4, .L1493
	ldr	r1, .L1493+4
	ldr	r0, .L1493+8
	bl	sftl_printk
	ldrh	r2, [r4, #246]
	movs	r1, #0
	ldr	r0, [r4, #3356]
	lsls	r2, r2, #1
	bl	ftl_memset
.L1481:
	ldr	r3, [r4, #2556]
	cmp	r5, r3
	bcc	.L1483
	ldr	r9, .L1493+12
	movs	r7, #0
	movw	r8, #65535
.L1484:
	ldrh	r3, [r4, #244]
	uxth	r6, r7
	cmp	r3, r6
	bhi	.L1489
	bl	l2p_flush
	bl	FtlVpcTblFlush
	add	sp, sp, #12
	@ sp needed
	pop	{r4, r5, r6, r7, r8, r9, pc}
.L1483:
	movs	r2, #0
	add	r1, sp, #4
	mov	r0, r5
	bl	log2phys
	ldr	r0, [sp, #4]
	adds	r3, r0, #1
	beq	.L1482
	ubfx	r0, r0, #10, #16
	bl	P2V_block_in_plane
	ldr	r2, [r4, #3356]
	ldrh	r3, [r2, r0, lsl #1]
	adds	r3, r3, #1
	strh	r3, [r2, r0, lsl #1]	@ movhi
.L1482:
	adds	r5, r5, #1
	b	.L1481
.L1489:
	ldr	r3, [r4, #76]
	uxth	r5, r7
	ldrh	r2, [r3, r5, lsl #1]
	ldr	r3, [r4, #3356]
	ldrh	r3, [r3, r5, lsl #1]
	cmp	r2, r3
	beq	.L1486
	cmp	r2, r8
	beq	.L1486
	mov	r1, r5
	mov	r0, r9
	bl	sftl_printk
	ldrh	r3, [r4, #28]
	cmp	r3, r6
	beq	.L1486
	ldrh	r3, [r4, #128]
	cmp	r3, r6
	beq	.L1486
	ldrh	r3, [r4, #80]
	cmp	r3, r6
	beq	.L1486
	ldr	r3, [r4, #76]
	ldrh	r2, [r3, r5, lsl #1]
	cbnz	r2, .L1488
	ldr	r2, [r4, #3356]
	ldrh	r2, [r2, r5, lsl #1]
	strh	r2, [r3, r5, lsl #1]	@ movhi
.L1486:
	adds	r7, r7, #1
	b	.L1484
.L1488:
	ldr	r2, [r4, #3356]
	mov	r0, r6
	ldrh	r2, [r2, r5, lsl #1]
	strh	r2, [r3, r5, lsl #1]	@ movhi
	bl	update_vpc_list
	b	.L1486
.L1494:
	.align	2
.L1493:
	.word	.LANCHOR0
	.word	.LANCHOR1+486
	.word	.LC111
	.word	.LC121
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
	ldr	r4, .L1505
	ldrh	r5, [r0]
	ldrh	r3, [r4, #244]
	cmp	r3, r5
	bcs	.L1496
	movw	r2, #2755
	ldr	r1, .L1505+4
	ldr	r0, .L1505+8
	bl	sftl_printk
.L1496:
	movw	r3, #65535
	cmp	r5, r3
	beq	.L1497
	ldr	r3, [r4, #76]
	mov	r0, r5
	ldrh	r3, [r3, r5, lsl #1]
	cbz	r3, .L1498
	bl	INSERT_DATA_LIST
.L1497:
	ldrh	r0, [r4, #3442]
	movs	r3, #1
	strb	r3, [r6, #8]
	movw	r3, #65535
	cmp	r0, r3
	beq	.L1499
	cmp	r5, r0
	bne	.L1500
	ldr	r3, [r4, #76]
	ldrh	r3, [r3, r0, lsl #1]
	cbz	r3, .L1501
.L1500:
	bl	update_vpc_list
.L1501:
	movw	r3, #65535
	strh	r3, [r4, #3442]	@ movhi
.L1499:
	mov	r0, r6
	bl	allocate_data_superblock
	bl	l2p_flush
	movs	r0, #0
	bl	FtlEctTblFlush
	bl	FtlVpcTblFlush
	movs	r0, #0
	pop	{r4, r5, r6, pc}
.L1498:
	bl	INSERT_FREE_LIST
	b	.L1497
.L1506:
	.align	2
.L1505:
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
	@ args = 0, pretend = 0, frame = 8
	@ frame_needed = 0, uses_anonymous_args = 0
	push	{r0, r1, r2, r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.save {r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.pad #12
	movs	r6, #0
	ldr	r10, .L1526+16
	mov	r5, r3
	movs	r2, #0
	mov	r4, r0
	ldr	r8, .L1526
	mov	r9, r1
	ldrb	r3, [r3, #9]	@ zero_extendqisi2
	bl	FlashProgPages
.L1508:
	cmp	r6, r9
	bne	.L1515
	ldr	r3, .L1526
	ldrb	r2, [r5, #6]	@ zero_extendqisi2
	ldrh	r3, [r3, #236]
	cmp	r2, r3
	bcc	.L1507
	movw	r2, #997
	ldr	r1, .L1526+4
	ldr	r0, .L1526+8
	bl	sftl_printk
.L1507:
	add	sp, sp, #12
	@ sp needed
	pop	{r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L1510:
	ldr	r1, [r4, #4]
	mov	r0, r10
	bl	sftl_printk
	ldr	r0, [r4, #4]
	ubfx	r0, r0, #10, #16
	bl	P2V_block_in_plane
	bl	decrement_vpc_count
	ldrh	r3, [r5, #4]
	cbnz	r3, .L1509
	mov	r0, r5
	bl	allocate_new_data_superblock
.L1509:
	mov	r0, r5
	bl	get_new_active_ppa
	movs	r2, #0
	str	r0, [r4, #4]
	str	r0, [sp, #4]
	movs	r1, #1
	ldrb	r3, [r5, #9]	@ zero_extendqisi2
	mov	r0, r4
	bl	FlashProgPages
.L1515:
	ldr	r2, [r4]
	adds	r3, r2, #1
	beq	.L1510
	cmp	r2, #256
	beq	.L1510
	ldrb	r2, [r5, #6]	@ zero_extendqisi2
	ldrh	r3, [r8, #236]
	cmp	r2, r3
	bcc	.L1511
	movw	r2, #982
	ldr	r1, .L1526+4
	ldr	r0, .L1526+8
	bl	sftl_printk
.L1511:
	ldr	r3, [r4, #4]
	add	r1, sp, #8
	movs	r2, #1
	ldr	r0, [r4, #16]
	str	r3, [r1, #-4]!
	bl	log2phys
	ldr	r3, [r4, #12]
	ldr	fp, [r3, #12]
	ubfx	r0, fp, #10, #16
	bl	P2V_block_in_plane
	cmp	fp, #-1
	mov	r7, r0
	beq	.L1512
	ldr	r3, [r8, #76]
	ldrh	r2, [r3, r0, lsl #1]
	cbnz	r2, .L1513
	mov	r1, r0
	ldr	r0, .L1526+12
	bl	sftl_printk
.L1513:
	mov	r0, r7
	bl	decrement_vpc_count
.L1512:
	adds	r6, r6, #1
	adds	r4, r4, #20
	b	.L1508
.L1527:
	.align	2
.L1526:
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
	push	{r0, r1, r2, r4, r5, r6, r7, r8, r9, lr}
	.save {r4, r5, r6, r7, r8, r9, lr}
	.pad #12
	movw	r3, #65535
	ldr	r4, .L1552
	ldrh	r2, [r4, #128]
	ldrh	r1, [r4, #306]
	cmp	r2, r3
	bne	.L1529
.L1536:
	ldrh	r2, [r4, #128]
	movs	r3, #0
	str	r3, [r4, #3448]
	movw	r3, #65535
	cmp	r2, r3
	bne	.L1549
.L1530:
	movs	r0, #0
.L1528:
	add	sp, sp, #12
	@ sp needed
	pop	{r4, r5, r6, r7, r8, r9, pc}
.L1529:
	ldr	r5, .L1552+4
	cbz	r0, .L1532
	ldrh	r2, [r5, #4]
	cmp	r2, r3
	beq	.L1533
.L1534:
	movs	r1, #2
.L1532:
	ldr	r0, .L1552+8
	bl	FtlGcScanTempBlk
	ldrh	r2, [r5, #4]
	movw	r3, #65535
	str	r0, [sp, #4]
	cmp	r2, r3
	beq	.L1536
	movs	r0, #1
	b	.L1528
.L1533:
	movs	r3, #0
	strh	r3, [r5, #4]	@ movhi
	ldrh	r3, [r4, #228]
	cmp	r3, #17
	bhi	.L1534
	b	.L1532
.L1549:
	ldrb	r1, [r4, #135]	@ zero_extendqisi2
	ldrh	r3, [r4, #306]
	ldrh	r2, [r4, #3208]
	muls	r3, r1, r3
	cmp	r2, r3
	beq	.L1537
	movs	r2, #164
	ldr	r1, .L1552+12
	ldr	r0, .L1552+16
	bl	sftl_printk
.L1537:
	ldrb	r3, [r4, #135]	@ zero_extendqisi2
	movs	r5, #0
	ldrh	r0, [r4, #306]
	mov	r8, #12
	ldr	r2, [r4, #76]
	ldrh	r1, [r4, #128]
	smulbb	r3, r3, r0
	strh	r3, [r2, r1, lsl #1]	@ movhi
	ldr	r2, [r4, #2580]
	ldrh	r3, [r4, #3208]
	add	r3, r3, r2
	str	r3, [r4, #2580]
.L1538:
	ldrh	r2, [r4, #3208]
	uxth	r3, r5
	cmp	r2, r3
	bhi	.L1542
	movw	r0, #65535
	bl	decrement_vpc_count
	ldrh	r0, [r4, #128]
	ldr	r3, [r4, #76]
	ldrh	r3, [r3, r0, lsl #1]
	cmp	r3, #0
	beq	.L1543
	bl	INSERT_DATA_LIST
.L1544:
	movw	r6, #65535
	movs	r5, #0
	strh	r6, [r4, #128]	@ movhi
	strh	r5, [r4, #3208]	@ movhi
	strh	r5, [r4, #3200]	@ movhi
	bl	l2p_flush
	bl	FtlVpcTblFlush
	ldrh	r3, [r4, #2624]
	ldrh	r2, [r4, #228]
	strh	r6, [r4, #176]	@ movhi
	add	r3, r3, r3, lsl #1
	cmp	r2, r3, asr #2
	ble	.L1530
	movs	r3, #20
	strh	r3, [r4, #3156]	@ movhi
	b	.L1530
.L1542:
	uxth	r7, r5
	ldr	r9, [r4, #3204]
	ldr	r3, [r4, #2556]
	mul	r7, r8, r7
	add	r6, r9, r7
	ldr	r0, [r6, #8]
	cmp	r0, r3
	bcc	.L1539
.L1550:
	ldrh	r0, [r4, #128]
	b	.L1551
.L1539:
	movs	r2, #0
	add	r1, sp, #4
	bl	log2phys
	ldr	r0, [r9, r7]
	ldr	r3, [sp, #4]
	cmp	r0, r3
	bne	.L1541
	ubfx	r0, r0, #10, #16
	bl	P2V_block_in_plane
	movs	r2, #1
	mov	r7, r0
	adds	r1, r6, #4
	ldr	r0, [r6, #8]
	bl	log2phys
	mov	r0, r7
.L1551:
	bl	decrement_vpc_count
	b	.L1540
.L1541:
	ldr	r2, [r6, #4]
	cmp	r3, r2
	bne	.L1550
.L1540:
	adds	r5, r5, #1
	b	.L1538
.L1543:
	bl	INSERT_FREE_LIST
	b	.L1544
.L1553:
	.align	2
.L1552:
	.word	.LANCHOR0
	.word	.LANCHOR2
	.word	.LANCHOR0+128
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
	ldr	r4, .L1556
	ldrh	r1, [r4, #306]
	add	r0, r4, #128
	bl	FtlGcScanTempBlk
	ldrh	r2, [r4, #130]
	ldrh	r3, [r4, #306]
	cmp	r2, r3
	bcc	.L1554
	addw	r0, r4, #3396
	bl	FtlMapBlkWriteDump_data
	movs	r0, #0
	bl	FtlGcFreeTempBlock
	movs	r3, #0
	str	r3, [r4, #3448]
.L1554:
	pop	{r4, pc}
.L1557:
	.align	2
.L1556:
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
	ldr	r4, .L1559
	add	r6, r4, #28
	str	r5, [r4, #3480]
	mov	r0, r6
	adds	r4, r4, #80
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
.L1560:
	.align	2
.L1559:
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
	ldr	r4, .L1569
	movs	r5, #0
	movs	r6, #20
	mov	r2, r3
	ldr	r1, [r4, #3172]
	ldr	r0, [r4, #3296]
	bl	FlashProgPages
.L1562:
	ldr	r1, [r4, #3172]
	uxth	r3, r5
	cmp	r1, r3
	bhi	.L1565
	ldr	r0, [r4, #3296]
	bl	FtlGcBufFree
	ldrh	r3, [r4, #132]
	movs	r0, #0
	str	r0, [r4, #3172]
	cbnz	r3, .L1561
	movs	r0, #1
	bl	FtlGcFreeTempBlock
	movs	r0, #1
.L1561:
	pop	{r4, r5, r6, pc}
.L1565:
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
	b	.L1562
.L1570:
	.align	2
.L1569:
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
	ldr	r4, .L1574
	ldrh	r2, [r4, #128]
	cmp	r2, r3
	beq	.L1572
	ldrh	r3, [r4, #132]
	cbnz	r3, .L1573
.L1572:
	movs	r0, #0
	movs	r5, #0
	bl	FtlGcFreeTempBlock
	ldr	r0, .L1574+4
	strb	r5, [r4, #136]
	bl	allocate_data_superblock
	strh	r5, [r4, #3200]	@ movhi
	strh	r5, [r4, #3208]	@ movhi
	bl	l2p_flush
	mov	r0, r5
	bl	FtlEctTblFlush
	bl	FtlVpcTblFlush
.L1573:
	ldr	r0, .L1574+4
	pop	{r3, r4, r5, lr}
	b	get_new_active_ppa
.L1575:
	.align	2
.L1574:
	.word	.LANCHOR0
	.word	.LANCHOR0+128
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
	@ args = 0, pretend = 0, frame = 32
	@ frame_needed = 0, uses_anonymous_args = 0
	ldr	r3, .L1706
	push	{r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.save {r4, r5, r6, r7, r8, r9, r10, fp, lr}
	mov	r9, r0
	.pad #44
	sub	sp, sp, #44
	mov	r4, r3
	ldr	r0, [r3, #3276]
	cmp	r0, #0
	bne	.L1658
	ldrh	r2, [r3, #2532]
	cmp	r2, #47
	bls	.L1576
	ldr	r2, .L1706+4
	ldrh	r1, [r2, #4]
	movw	r2, #65535
	cmp	r1, r2
	bne	.L1578
.L1581:
	ldrh	r2, [r4, #224]
	movw	r1, #65535
	cmp	r2, r1
	bne	.L1579
.L1580:
	bl	FtlReadRefresh
	ldr	r3, [r4, #3160]
	movw	r7, #65535
	ldrh	r1, [r4, #176]
	adds	r3, r3, #1
	add	r3, r3, r9, lsl #7
	cmp	r1, r7
	str	r3, [r4, #3160]
	bne	.L1582
	ldrh	r2, [r4, #128]
	cmp	r2, r1
	bne	.L1583
	ldrh	r6, [r4, #226]
	cmp	r6, r2
	bne	.L1584
	ldrh	r2, [r4, #228]
	cmp	r2, #24
	ite	cc
	movcc	r2, #5120
	movcs	r2, #1024
	cmp	r3, r2
	bls	.L1584
	movs	r5, #0
	str	r5, [r4, #3160]
	strh	r5, [r4, #3664]	@ movhi
	bl	GetSwlReplaceBlock
	cmp	r0, r6
	mov	r7, r0
	bne	.L1662
	ldrh	r2, [r4, #228]
	ldrh	r3, [r4, #3158]
	cmp	r2, r3
	bcs	.L1587
	movs	r0, #64
	bl	List_get_gc_head_node
	uxth	r3, r0
	cmp	r3, r7
	beq	.L1589
	mov	r0, r3
	ldr	r3, [r4, #76]
	ldrh	r3, [r3, r0, lsl #1]
	cmp	r3, #7
	bhi	.L1590
	mov	r0, r5
	bl	List_get_gc_head_node
	uxth	r5, r0
	movs	r3, #128
	strh	r3, [r4, #3158]	@ movhi
	cmp	r5, r7
	bne	.L1586
.L1589:
	bl	FtlGcReFreshBadBlk
	cmp	r9, #0
	bne	.L1592
	movw	r3, #65535
	cmp	r7, r3
	bne	.L1592
.L1656:
	ldrh	r3, [r4, #228]
	cmp	r3, #24
	bhi	.L1663
	cmp	r3, #16
	ldrh	r6, [r4, #306]
	bls	.L1594
	lsrs	r6, r6, #5
.L1593:
	ldrh	r2, [r4, #3156]
	cmp	r2, r3
	bcs	.L1597
	ldrh	r3, [r4, #128]
	movw	r2, #65535
	cmp	r3, r2
	bne	.L1598
	ldrh	r2, [r4, #226]
	cmp	r2, r3
	bne	.L1598
	ldrh	r0, [r4, #3664]
	cbnz	r0, .L1599
	ldr	r3, [r4, #2556]
	ldr	r2, [r4, #2552]
	add	r3, r3, r3, lsl #1
	cmp	r2, r3, lsr #2
	bcs	.L1600
.L1599:
	ldrh	r3, [r4, #2624]
	add	r3, r3, r3, lsl #1
	asrs	r3, r3, #2
.L1702:
	strh	r3, [r4, #3156]	@ movhi
	movs	r3, #0
	str	r3, [r4, #3168]
.L1576:
	add	sp, sp, #44
	@ sp needed
	pop	{r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L1578:
	ldrh	r3, [r3, #128]
	cmp	r3, r2
	beq	.L1581
	movs	r0, #1
	bl	FtlGcFreeTempBlock
	cmp	r0, #0
	beq	.L1581
	movs	r0, #1
	b	.L1576
.L1579:
	ldrh	r3, [r4, #226]
	cmp	r3, r1
	itt	eq
	strheq	r2, [r4, #226]	@ movhi
	strheq	r3, [r4, #224]	@ movhi
	b	.L1580
.L1590:
	movs	r3, #64
.L1701:
	strh	r3, [r4, #3158]	@ movhi
	b	.L1589
.L1587:
	movs	r3, #80
	b	.L1701
.L1662:
	mov	r5, r0
.L1586:
	ldr	r3, [r4, #76]
	mov	r7, r5
	ldrh	r1, [r4, #3156]
	ldrh	r2, [r4, #228]
	ldrh	r3, [r3, r5, lsl #1]
	str	r1, [sp, #4]
	ldr	r1, [r4, #2540]
	ldr	r0, .L1706+8
	ldrh	r1, [r1, r5, lsl #1]
	str	r1, [sp]
	mov	r1, r5
	bl	sftl_printk
	b	.L1589
.L1594:
	cmp	r3, #12
	bls	.L1595
	lsrs	r6, r6, #4
	b	.L1593
.L1595:
	cmp	r3, #8
	bls	.L1593
	lsrs	r6, r6, #2
	b	.L1593
.L1663:
	movs	r6, #1
	b	.L1593
.L1600:
	movs	r3, #18
	b	.L1702
.L1598:
	ldrh	r3, [r4, #2624]
	add	r3, r3, r3, lsl #1
	asrs	r3, r3, #2
	strh	r3, [r4, #3156]	@ movhi
.L1597:
	ldrh	r3, [r4, #3210]
	cbz	r3, .L1664
	adds	r6, r6, #32
	uxth	r6, r6
.L1664:
	movw	r7, #65535
.L1603:
	ldrh	r3, [r4, #176]
	movw	r2, #65535
	cmp	r3, r2
	bne	.L1612
	cmp	r7, r3
	beq	.L1613
	strh	r7, [r4, #176]	@ movhi
.L1614:
	ldrh	r0, [r4, #176]
	movw	r5, #65535
	movs	r3, #0
	strb	r3, [r4, #184]
	cmp	r0, r5
	beq	.L1612
	bl	IsBlkInGcList
	cbz	r0, .L1617
	strh	r5, [r4, #176]	@ movhi
.L1617:
	ldrh	r2, [r4, #176]
	movw	r3, #65535
	cmp	r2, r3
	beq	.L1612
	ldr	r0, .L1706+12
	bl	make_superblock
	movs	r3, #0
	ldrh	r2, [r4, #176]
	strh	r3, [r4, #178]	@ movhi
	strb	r3, [r4, #182]
	strh	r3, [r4, #3666]	@ movhi
	ldr	r3, [r4, #76]
	ldrh	r3, [r3, r2, lsl #1]
	strh	r3, [r4, #3668]	@ movhi
.L1612:
	ldrh	r3, [r4, #176]
	ldrh	r2, [r4, #28]
	cmp	r2, r3
	beq	.L1618
	ldrh	r2, [r4, #80]
	cmp	r2, r3
	beq	.L1618
	ldrh	r2, [r4, #128]
	cmp	r2, r3
	bne	.L1619
.L1618:
	movw	r3, #65535
	strh	r3, [r4, #176]	@ movhi
.L1619:
	ldr	r5, .L1706
	mov	r4, r5
.L1654:
	ldrh	r10, [r5, #176]
	movw	r3, #65535
	cmp	r10, r3
	bne	.L1620
	movs	r3, #0
	mov	fp, r3
	str	r3, [r5, #3168]
.L1621:
	ldrh	r8, [r4, #3164]
	mov	r0, r8
	bl	List_get_gc_head_node
	uxth	r2, r0
	cmp	r2, r10
	strh	r2, [r4, #176]	@ movhi
	bne	.L1622
	movs	r3, #0
	movs	r0, #8
	strh	r3, [r4, #3164]	@ movhi
	b	.L1576
.L1604:
	movs	r3, #0
	ldrh	r2, [r4, #228]
	str	r3, [r4, #3168]
	ldrh	r3, [r4, #3156]
	cmp	r2, r3
	bls	.L1605
	ldrh	r3, [r4, #3664]
	cbnz	r3, .L1606
	ldr	r3, [r4, #2556]
	ldr	r2, [r4, #2552]
	add	r3, r3, r3, lsl #1
	cmp	r2, r3, lsr #2
	bcs	.L1607
.L1606:
	ldrh	r3, [r4, #2624]
	add	r3, r3, r3, lsl #1
	asrs	r3, r3, #2
.L1703:
	strh	r3, [r4, #3156]	@ movhi
	bl	FtlReadRefresh
	movs	r0, #0
	bl	List_get_gc_head_node
	ldr	r3, [r4, #76]
	uxth	r0, r0
	ldrh	r3, [r3, r0, lsl #1]
	cmp	r3, #4
	bls	.L1605
.L1705:
	ldrh	r0, [r4, #3664]
	b	.L1576
.L1607:
	movs	r3, #18
	b	.L1703
.L1605:
	ldrh	r0, [r4, #3664]
	cmp	r0, #0
	bne	.L1609
	ldrh	r5, [r4, #2624]
	add	r3, r5, r5, lsl #1
	asrs	r3, r3, #2
	strh	r3, [r4, #3156]	@ movhi
	bl	List_get_gc_head_node
	ldr	r3, [r4, #76]
	uxth	r0, r0
	ldrh	r1, [r4, #308]
	ldrh	r2, [r3, r0, lsl #1]
	ldrh	r3, [r4, #236]
	muls	r3, r1, r3
	add	r3, r3, r3, lsr #31
	cmp	r2, r3, asr #1
	ble	.L1610
	ldrh	r3, [r4, #228]
	subs	r5, r5, #1
	cmp	r3, r5
	blt	.L1610
	bl	FtlReadRefresh
	b	.L1705
.L1610:
	cmp	r2, #0
	bne	.L1609
	movw	r0, #65535
	bl	decrement_vpc_count
	ldrh	r0, [r4, #228]
	adds	r0, r0, #1
	b	.L1576
.L1707:
	.align	2
.L1706:
	.word	.LANCHOR0
	.word	.LANCHOR2
	.word	.LC124
	.word	.LANCHOR0+176
.L1613:
	ldrh	r3, [r4, #226]
	cmp	r3, r7
	beq	.L1614
	ldr	r2, [r4, #76]
	ldrh	r3, [r2, r3, lsl #1]
	cbnz	r3, .L1615
	strh	r7, [r4, #226]	@ movhi
.L1615:
	ldrh	r3, [r4, #226]
	strh	r3, [r4, #176]	@ movhi
	movw	r3, #65535
	strh	r3, [r4, #226]	@ movhi
	b	.L1614
.L1622:
	str	r0, [sp, #16]
	mov	r0, r2
	str	r2, [sp, #12]
	add	r8, r8, #1
	bl	IsBlkInGcList
	ldr	r2, [sp, #12]
	ldr	r3, [sp, #16]
	cbz	r0, .L1623
	strh	r8, [r4, #3164]	@ movhi
	b	.L1621
.L1623:
	ldrh	lr, [r4, #236]
	uxth	r8, r8
	ldrh	r1, [r4, #306]
	uxth	r3, r3
	ldr	r0, [r4, #76]
	strh	r8, [r4, #3164]	@ movhi
	mul	r1, lr, r1
	ldrh	ip, [r0, r3, lsl #1]
	add	lr, r1, r1, lsr #31
	cmp	ip, lr, asr #1
	bgt	.L1625
	cmp	r8, #48
	bls	.L1626
	cmp	ip, #8
	bls	.L1626
	ldrh	ip, [r4, #3200]
	cmp	ip, #35
	bhi	.L1626
.L1625:
	strh	fp, [r4, #3164]	@ movhi
.L1626:
	ldrh	r3, [r0, r3, lsl #1]
	cmp	r1, r3
	bgt	.L1627
	cmp	r7, r10
	bne	.L1627
	ldrh	r1, [r4, #3164]
	cmp	r1, #3
	bhi	.L1627
	movs	r3, #0
	strh	r7, [r4, #176]	@ movhi
	strh	r3, [r4, #3164]	@ movhi
	b	.L1705
.L1627:
	cbnz	r3, .L1628
	movw	r0, #65535
	bl	decrement_vpc_count
	ldrh	r3, [r4, #3164]
	adds	r3, r3, #1
	strh	r3, [r4, #3164]	@ movhi
	b	.L1621
.L1628:
	movs	r3, #0
	strb	r3, [r4, #184]
	ldrh	r3, [r4, #28]
	cmp	r3, r2
	bne	.L1629
	movw	r2, #717
	ldr	r1, .L1708
	ldr	r0, .L1708+4
	bl	sftl_printk
.L1629:
	ldrh	r2, [r4, #176]
	ldrh	r3, [r4, #80]
	cmp	r2, r3
	bne	.L1630
	movw	r2, #718
	ldr	r1, .L1708
	ldr	r0, .L1708+4
	bl	sftl_printk
.L1630:
	ldrh	r2, [r4, #176]
	ldrh	r3, [r4, #128]
	cmp	r2, r3
	bne	.L1631
	movw	r2, #719
	ldr	r1, .L1708
	ldr	r0, .L1708+4
	bl	sftl_printk
.L1631:
	ldr	r0, .L1708+8
	bl	make_superblock
	ldrh	r1, [r4, #176]
	movs	r3, #0
	ldr	r2, [r4, #76]
	strh	r3, [r4, #3666]	@ movhi
	ldrh	r2, [r2, r1, lsl #1]
	strh	r3, [r4, #178]	@ movhi
	strb	r3, [r4, #182]
	strh	r2, [r4, #3668]	@ movhi
.L1620:
	movs	r3, #1
	str	r3, [r4, #3276]
	ldrh	r3, [r4, #306]
	str	r3, [sp, #16]
	cmp	r9, #0
	beq	.L1632
	ldr	r2, [sp, #16]
	ldrh	r3, [r4, #236]
	ldrh	r1, [r4, #176]
	muls	r3, r2, r3
	ldr	r2, [r4, #76]
	ldrh	r2, [r2, r1, lsl #1]
	subs	r3, r3, r2
	it	mi
	addmi	r3, r3, #3
	add	r6, r6, r3, asr #2
	uxth	r6, r6
.L1632:
	ldrh	r3, [r4, #178]
	ldr	r1, [sp, #16]
	adds	r2, r3, r6
	cmp	r2, r1
	itt	gt
	movgt	r2, r1
	subgt	r6, r2, r3
	mov	r3, #0
	it	gt
	uxthgt	r6, r6
.L1704:
	str	r3, [sp, #20]
	ldrh	r3, [sp, #20]
	cmp	r6, r3
	bls	.L1643
	ldr	r3, [sp, #20]
	movw	r10, #65535
	ldrh	r8, [r5, #178]
	mov	fp, #20
	ldrh	ip, [r5, #236]
	ldr	r0, [r5, #3188]
	add	r8, r8, r3
	ldr	r3, .L1708+12
	str	r3, [sp, #24]
	movs	r3, #0
	str	r3, [sp, #12]
	b	.L1644
.L1637:
	ldr	r1, [sp, #24]
	ldrh	r2, [r1, #2]!
	cmp	r2, r10
	str	r1, [sp, #24]
	beq	.L1636
	ldr	r1, [sp, #12]
	orr	r2, r8, r2, lsl #10
	mla	lr, fp, r1, r0
	str	r2, [lr, #4]
	mov	r2, r1
	adds	r2, r2, #1
	uxth	r2, r2
	str	r2, [sp, #12]
.L1636:
	adds	r3, r3, #1
.L1644:
	uxth	r2, r3
	cmp	ip, r2
	bhi	.L1637
	mov	fp, #0
	ldrb	r2, [r5, #184]	@ zero_extendqisi2
	ldr	r1, [sp, #12]
	bl	FlashReadPages
.L1638:
	ldr	r2, [sp, #12]
	uxth	r3, fp
	cmp	r2, r3
	bhi	.L1642
	ldr	r3, [sp, #20]
	adds	r3, r3, #1
	b	.L1704
.L1642:
	mov	r8, #20
	ldr	r3, [r5, #3188]
	mul	r8, r8, fp
	add	r2, r3, r8
	ldr	r3, [r3, r8]
	adds	r3, r3, #1
	beq	.L1639
	ldr	r10, [r2, #12]
	movw	r3, #61589
	ldrh	r2, [r10]
	cmp	r2, r3
	bne	.L1639
	ldr	r3, [r10, #8]
	adds	r2, r3, #1
	bne	.L1640
	movw	r2, #753
	ldr	r1, .L1708
	ldr	r0, .L1708+4
	str	r3, [sp, #24]
	bl	sftl_printk
	ldr	r3, [sp, #24]
.L1640:
	movs	r2, #0
	mov	r0, r3
	add	r1, sp, #32
	bl	log2phys
	ldr	r0, [r4, #3188]
	ldr	r3, [sp, #32]
	add	r0, r0, r8
	ldr	r2, [r0, #4]
	cmp	r2, r3
	bne	.L1639
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
	ldr	r3, [sp, #32]
	str	r3, [r10, #12]
	ldrh	r3, [r4, #128]
	strh	r3, [r10, #2]	@ movhi
	ldr	r3, [r4, #2596]
	ldr	r0, [r4, #3188]
	str	r3, [r10, #4]
	ldr	r3, [r4, #3172]
	add	r0, r0, r8
	adds	r3, r3, #1
	str	r3, [r4, #3172]
	bl	FtlGcBufAlloc
	ldrb	r2, [r4, #135]	@ zero_extendqisi2
	ldr	r3, [r4, #3172]
	cmp	r2, r3
	beq	.L1641
	ldrh	r3, [r4, #132]
	cbnz	r3, .L1639
.L1641:
	bl	Ftl_gc_temp_data_write_back
	cbz	r0, .L1639
	ldr	r3, .L1708+16
	movs	r2, #0
	movw	r1, #65535
	str	r2, [r3, #3276]
	strh	r1, [r3, #176]	@ movhi
	strh	r2, [r3, #178]	@ movhi
	ldrh	r0, [r3, #3664]
	b	.L1576
.L1639:
	add	fp, fp, #1
	b	.L1638
.L1643:
	ldrh	r3, [r5, #178]
	add	r6, r6, r3
	ldr	r3, [sp, #16]
	uxth	r6, r6
	cmp	r3, r6
	strh	r6, [r5, #178]	@ movhi
	bhi	.L1645
	ldr	r3, [r5, #3172]
	cbz	r3, .L1646
	bl	Ftl_gc_temp_data_write_back
	cbz	r0, .L1646
	movs	r3, #0
	ldrh	r0, [r5, #3664]
	str	r3, [r5, #3276]
	b	.L1576
.L1646:
	ldrh	r6, [r5, #3666]
	cmp	r6, #0
	bne	.L1647
	ldrh	r1, [r5, #176]
	ldr	r3, [r5, #76]
	ldrh	r3, [r3, r1, lsl #1]
	cbz	r3, .L1647
	ldrh	r0, [r5, #3668]
	ldrh	r2, [r5, #178]
	str	r3, [sp]
	mov	r3, r6
	str	r0, [sp, #4]
	ldr	r0, .L1708+20
	bl	sftl_printk
.L1648:
	ldr	r3, [r5, #2556]
	cmp	r6, r3
	bcs	.L1650
	movs	r2, #0
	add	r1, sp, #36
	mov	r0, r6
	bl	log2phys
	ldr	r8, [sp, #36]
	cmp	r8, #-1
	beq	.L1649
	ubfx	r0, r8, #10, #16
	bl	P2V_block_in_plane
	ldrh	r3, [r5, #176]
	cmp	r3, r0
	bne	.L1649
	mov	r2, r8
	mov	r1, r6
	ldr	r0, .L1708+24
	bl	sftl_printk
.L1650:
	ldr	r3, [r5, #2556]
	cmp	r6, r3
	bcc	.L1647
	ldrh	r2, [r5, #176]
	movs	r1, #0
	ldr	r3, [r5, #76]
	strh	r1, [r3, r2, lsl #1]	@ movhi
	ldrh	r0, [r5, #176]
	bl	update_vpc_list
	bl	l2p_flush
	bl	FtlVpcTblFlush
.L1647:
	movw	r3, #65535
	strh	r3, [r5, #176]	@ movhi
.L1645:
	movs	r3, #0
	str	r3, [r5, #3276]
	ldrh	r3, [r5, #228]
	cmp	r3, #2
	bhi	.L1653
	ldrh	r6, [r5, #306]
	b	.L1654
.L1649:
	adds	r6, r6, #1
	b	.L1648
.L1653:
	ldrh	r0, [r5, #3664]
	cmp	r0, #0
	bne	.L1576
	adds	r0, r3, #1
	b	.L1576
.L1658:
	movs	r0, #0
	b	.L1576
.L1709:
	.align	2
.L1708:
	.word	.LANCHOR1+568
	.word	.LC8
	.word	.LANCHOR0+176
	.word	.LANCHOR0+190
	.word	.LANCHOR0
	.word	.LC125
	.word	.LC126
.L1582:
	cmp	r9, #0
	beq	.L1656
.L1592:
	ldrh	r2, [r4, #128]
	movw	r3, #65535
	cmp	r2, r3
	bne	.L1667
.L1657:
	ldrh	r3, [r4, #226]
	movw	r2, #65535
	cmp	r3, r2
	bne	.L1667
	cmp	r7, r3
	bne	.L1667
	ldrh	r3, [r4, #176]
	cmp	r3, r7
	beq	.L1604
.L1609:
	movw	r7, #65535
.L1667:
	movs	r6, #1
	b	.L1603
.L1584:
	cmp	r9, #0
	beq	.L1656
	movw	r7, #65535
	b	.L1657
.L1583:
	cmp	r9, #0
	bne	.L1609
	b	.L1656
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
	cmp	r0, #16
	push	{r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.save {r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.pad #60
	sub	sp, sp, #60
	mov	r7, r1
	mov	r10, r3
	str	r2, [sp, #20]
	bne	.L1711
	mov	r2, r3
	ldr	r1, [sp, #20]
	add	r0, r7, #256
	bl	FtlVendorPartRead
	mov	r8, r0
.L1710:
	mov	r0, r8
	add	sp, sp, #60
	@ sp needed
	pop	{r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L1711:
	ldr	r3, [sp, #20]
	ldr	r4, .L1743
	adds	r3, r1, r3
	str	r3, [sp, #8]
	ldr	r2, [sp, #8]
	ldr	r3, [r4, #344]
	cmp	r2, r3
	bhi	.L1732
	ldr	r3, .L1743+4
	ldr	r3, [r3]
	adds	r5, r3, #1
	beq	.L1733
	ldrh	r5, [r4, #262]
	mov	r0, r7
	mov	r9, #0
	mov	r6, r9
	mov	r8, r9
	mov	r1, r5
	bl	__aeabi_uidiv
	ldr	r3, [sp, #8]
	mov	r1, r5
	str	r0, [sp, #12]
	subs	r0, r3, #1
	bl	__aeabi_uidiv
	ldr	r3, [sp, #12]
	ldr	r2, [sp, #20]
	ldr	r5, [sp, #12]
	rsb	r3, r3, #1
	str	r0, [sp, #16]
	add	r3, r3, r0
	str	r9, [sp, #28]
	str	r3, [sp, #4]
	ldr	r3, [r4, #2588]
	str	r9, [sp, #24]
	add	r3, r3, r2
	ldr	r2, [sp, #4]
	str	r3, [r4, #2588]
	ldr	r3, [r4, #2560]
	add	r3, r3, r2
	str	r3, [r4, #2560]
.L1713:
	ldr	r3, [sp, #4]
	cbnz	r3, .L1730
	ldr	r3, .L1743
	ldrh	r2, [r3, #3210]
	cbnz	r2, .L1731
	ldrh	r3, [r3, #228]
	cmp	r3, #31
	bhi	.L1710
.L1731:
	movs	r1, #1
	movs	r0, #0
	bl	rk_ftl_garbage_collect
	b	.L1710
.L1730:
	movs	r2, #0
	add	r1, sp, #52
	mov	r0, r5
	bl	log2phys
	ldr	r2, [sp, #52]
	adds	r0, r2, #1
	bne	.L1714
	mov	fp, #0
.L1715:
	ldrh	r0, [r4, #262]
	cmp	fp, r0
	bcc	.L1717
.L1718:
	ldr	r3, [sp, #4]
	adds	r5, r5, #1
	subs	r3, r3, #1
	str	r3, [sp, #4]
	beq	.L1722
	ldrh	r3, [r4, #236]
	cmp	r6, r3, lsl #2
	bne	.L1713
.L1722:
	cmp	r6, #0
	beq	.L1713
	movs	r2, #0
	mov	r1, r6
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
.L1729:
	movs	r3, #20
	ldr	r2, [r4, #3292]
	mul	r3, r3, fp
	ldr	r0, [sp, #12]
	add	r2, r2, r3
	ldr	r1, [r2, #16]
	cmp	r0, r1
	bne	.L1724
	ldr	r1, [r2, #8]
	ldr	r2, [r4, #3316]
	cmp	r1, r2
	bne	.L1725
	str	r3, [sp, #44]
	mov	r0, r10
	ldr	r3, [sp, #32]
	ldr	r2, [sp, #36]
	add	r1, r1, r3
.L1742:
	bl	ftl_memcpy
	ldr	r3, [sp, #44]
.L1725:
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
	moveq	r8, r0
	addeq	r1, r1, #1
	streq	r1, [r4, #2716]
	ldr	r3, [ip, r3]
	cmp	r3, #256
	bne	.L1728
	ldr	r0, [r2, #4]
	ubfx	r0, r0, #10, #16
	bl	P2V_block_in_plane
	bl	FtlGcRefreshBlock
.L1728:
	add	fp, fp, #1
	cmp	r6, fp
	bne	.L1729
	movs	r6, #0
	b	.L1713
.L1717:
	mla	r0, r0, r5, fp
	cmp	r7, r0
	bhi	.L1716
	ldr	r3, [sp, #8]
	cmp	r3, r0
	bls	.L1716
	subs	r0, r0, r7
	mov	r1, #512
	add	r0, r10, r0, lsl #9
	bl	__memzero
.L1716:
	add	fp, fp, #1
	b	.L1715
.L1714:
	ldr	r3, [r4, #3292]
	movs	r1, #20
	ldrh	fp, [r4, #262]
	mla	r3, r1, r6, r3
	str	r2, [r3, #4]
	ldr	r2, [sp, #12]
	cmp	r5, r2
	bne	.L1719
	ldr	r2, [r4, #3316]
	mov	r1, fp
	mov	r0, r7
	str	r3, [sp, #32]
	str	r2, [r3, #8]
	bl	__aeabi_uidivmod
	sub	r2, fp, r1
	str	r1, [sp, #24]
	ldr	r1, [sp, #20]
	mov	r3, r2
	cmp	r2, r1
	it	cs
	movcs	r3, r1
	cmp	r3, fp
	str	r3, [sp, #28]
	ldr	r3, [sp, #32]
	bne	.L1720
	str	r10, [r3, #8]
.L1720:
	ldrh	r2, [r4, #316]
	ldr	r1, [r4, #3340]
	str	r5, [r3, #16]
	muls	r2, r6, r2
	adds	r6, r6, #1
	bic	r2, r2, #3
	add	r2, r2, r1
	str	r2, [r3, #12]
	b	.L1718
.L1719:
	ldr	r2, [sp, #16]
	cmp	r5, r2
	bne	.L1721
	ldr	r2, [r4, #3320]
	ldr	r1, [sp, #8]
	str	r2, [r3, #8]
	mul	r2, fp, r5
	sub	r9, r1, r2
	cmp	fp, r9
	bne	.L1720
.L1741:
	subs	r2, r2, r7
	add	r2, r10, r2, lsl #9
	str	r2, [r3, #8]
	b	.L1720
.L1721:
	mul	r2, r5, fp
	b	.L1741
.L1724:
	ldr	r0, [sp, #16]
	cmp	r0, r1
	bne	.L1725
	ldr	r1, [r2, #8]
	ldr	r2, [r4, #3320]
	cmp	r1, r2
	bne	.L1725
	str	r3, [sp, #44]
	ldrh	r0, [r4, #262]
	ldr	r3, [sp, #16]
	ldr	r2, [sp, #40]
	muls	r0, r3, r0
	subs	r0, r0, r7
	add	r0, r10, r0, lsl #9
	b	.L1742
.L1732:
	mov	r8, #-1
	b	.L1710
.L1733:
	mov	r8, r3
	b	.L1710
.L1744:
	.align	2
.L1743:
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
	@ args = 0, pretend = 0, frame = 64
	@ frame_needed = 0, uses_anonymous_args = 0
	cmp	r0, #16
	push	{r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.save {r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.pad #68
	sub	sp, sp, #68
	mov	r10, r1
	str	r2, [sp, #12]
	str	r3, [sp, #8]
	bne	.L1747
	mov	r2, r3
	ldr	r1, [sp, #12]
	add	r0, r10, #256
	bl	FtlVendorPartWrite
.L1746:
	add	sp, sp, #68
	@ sp needed
	pop	{r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L1747:
	ldr	r3, [sp, #12]
	ldr	r5, .L1798
	adds	r6, r1, r3
	ldr	r3, [r5, #344]
	cmp	r6, r3
	bhi	.L1784
	ldr	r4, .L1798+4
	ldr	r0, [r4]
	adds	r7, r0, #1
	beq	.L1746
	ldrh	r7, [r5, #262]
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
	ldr	r2, [sp, #12]
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
	cbz	r3, .L1786
	ldrh	r2, [r5, #32]
	add	r3, r5, #28
	adds	r5, r5, #80
	cmp	r2, #0
	it	ne
	movne	r5, r3
.L1749:
	ldr	r7, [sp, #4]
	ldr	r4, .L1798
.L1750:
	ldr	r3, [sp]
	cbnz	r3, .L1779
	mov	r0, r3
	ldr	r1, [sp, #24]
	bl	rk_ftl_garbage_collect
	ldrh	r3, [r4, #228]
	cmp	r3, #5
	bls	.L1780
.L1782:
	movs	r0, #0
	b	.L1746
.L1786:
	adds	r5, r5, #28
	b	.L1749
.L1779:
	ldrb	r2, [r5, #6]	@ zero_extendqisi2
	ldrh	r3, [r4, #236]
	cmp	r2, r3
	bcc	.L1751
	movw	r2, #1038
	ldr	r1, .L1798+8
	ldr	r0, .L1798+12
	bl	sftl_printk
.L1751:
	ldrh	r2, [r5, #4]
	cbnz	r2, .L1752
	ldr	r3, .L1798+16
	ldr	r6, .L1798+4
	cmp	r5, r3
	bne	.L1753
	ldrh	r8, [r4, #84]
	cmp	r8, #0
	bne	.L1754
	add	r0, r5, #52
	bl	allocate_new_data_superblock
	str	r8, [r6, #8]
.L1754:
	ldr	r5, .L1798+16
	ldr	r0, .L1798+16
	bl	allocate_new_data_superblock
	ldr	r2, [r6, #8]
	add	r3, r5, #52
	cmp	r2, #0
	it	ne
	movne	r5, r3
.L1755:
	ldrh	r3, [r5, #4]
	cbnz	r3, .L1752
	mov	r0, r5
	bl	allocate_new_data_superblock
.L1752:
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
	ldrh	r3, [r4, #236]
	str	r2, [sp, #36]
	ldrb	r2, [r5, #6]	@ zero_extendqisi2
	cmp	r2, r3
	bcc	.L1756
	movw	r2, #1071
	ldr	r1, .L1798+8
	ldr	r0, .L1798+12
	bl	sftl_printk
.L1756:
	mov	fp, #0
.L1757:
	ldr	r3, [sp, #36]
	cmp	fp, r3
	bne	.L1777
.L1758:
	mov	r3, r5
	movs	r2, #0
	mov	r1, fp
	ldr	r0, [r4, #3300]
	bl	FtlProgPages
	ldr	r3, [sp]
	cmp	fp, r3
	bls	.L1778
	movw	r2, #1149
	ldr	r1, .L1798+8
	ldr	r0, .L1798+12
	bl	sftl_printk
.L1778:
	ldr	r3, [sp]
	sub	r3, r3, fp
	str	r3, [sp]
	b	.L1750
.L1753:
	str	r2, [r6, #8]
	ldrh	r2, [r4, #32]
	cbnz	r2, .L1789
	mov	r0, r5
	bl	allocate_new_data_superblock
	b	.L1755
.L1789:
	mov	r5, r3
	b	.L1752
.L1777:
	ldrh	r3, [r5, #4]
	cmp	r3, #0
	beq	.L1758
	movs	r2, #0
	add	r1, sp, #40
	mov	r0, r7
	mov	r8, #20
	bl	log2phys
	mov	r0, r5
	mul	r8, r8, fp
	bl	get_new_active_ppa
	ldrh	r2, [r4, #316]
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
	beq	.L1759
	ldr	r3, [sp, #20]
	cmp	r7, r3
	bne	.L1794
	ldrh	r2, [r4, #262]
	ldr	r3, [sp, #12]
	smulbb	r2, r2, r7
	add	r6, r10, r3
	movs	r3, #0
	str	r3, [sp, #16]
	subs	r6, r6, r2
	uxth	r6, r6
	b	.L1762
.L1759:
	ldrh	r6, [r4, #262]
	mov	r0, r10
	mov	r1, r6
	bl	__aeabi_uidivmod
	ldr	r3, [sp, #12]
	subs	r6, r6, r1
	str	r1, [sp, #16]
	cmp	r6, r3
	it	cs
	movcs	r6, r3
.L1762:
	ldrh	r3, [r4, #262]
	cmp	r6, r3
	ldr	r3, [sp, #4]
	bne	.L1763
	cmp	r7, r3
	ldr	r3, [r4, #3300]
	itte	ne
	mulne	r6, r6, r7
	ldrne	r2, [sp, #8]
	ldreq	r2, [sp, #8]
	add	r3, r3, r8
	itett	ne
	subne	r6, r6, r10
	streq	r2, [r3, #8]
	addne	r6, r2, r6, lsl #9
	strne	r6, [r3, #8]
.L1765:
	ldrb	r2, [r5, #6]	@ zero_extendqisi2
	ldrh	r3, [r4, #236]
	cmp	r2, r3
	bcc	.L1774
	movw	r2, #1140
	ldr	r1, .L1798+8
	ldr	r0, .L1798+12
	bl	sftl_printk
.L1774:
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
	ldr	r3, [sp, #40]
	str	r3, [r9, #12]
	ldrh	r3, [r5]
	strh	r3, [r9, #2]	@ movhi
	b	.L1757
.L1763:
	cmp	r7, r3
	ldr	r3, [r4, #3300]
	ite	eq
	ldreq	r2, [r4, #3316]
	ldrne	r2, [r4, #3320]
	add	r3, r3, r8
	str	r2, [r3, #8]
	ldr	r3, [sp, #40]
	adds	r0, r3, #1
	beq	.L1768
	str	r3, [sp, #48]
	movs	r1, #1
	ldr	r3, [r4, #3300]
	add	r0, sp, #44
	str	r7, [sp, #60]
	add	r3, r3, r8
	ldr	r2, [r3, #8]
	ldr	r3, [r3, #12]
	str	r2, [sp, #52]
	movs	r2, #0
	str	r3, [sp, #56]
	bl	FlashReadPages
	ldr	r3, [sp, #44]
	adds	r1, r3, #1
	bne	.L1769
	ldr	r2, [r4, #2716]
	ldr	r0, .L1798+20
	adds	r2, r2, #1
	str	r2, [r4, #2716]
	mov	r2, r7
	ldr	r1, [r9, #8]
	bl	sftl_printk
.L1772:
	ldr	r3, [sp, #4]
	lsls	r2, r6, #9
	cmp	r7, r3
	bne	.L1773
	ldr	r3, [r4, #3300]
	ldr	r1, [sp, #8]
	add	r3, r3, r8
	ldr	r0, [r3, #8]
	ldr	r3, [sp, #16]
	add	r0, r0, r3, lsl #9
.L1796:
	bl	ftl_memcpy
	b	.L1765
.L1799:
	.align	2
.L1798:
	.word	.LANCHOR0
	.word	.LANCHOR2
	.word	.LANCHOR1+591
	.word	.LC8
	.word	.LANCHOR0+28
	.word	.LC127
.L1769:
	ldr	r3, [r9, #8]
	cmp	r7, r3
	beq	.L1771
	ldr	r3, [r4, #2716]
	mov	r2, r7
	ldr	r0, .L1800
	adds	r3, r3, #1
	str	r3, [r4, #2716]
	ldr	r1, [r9, #8]
	bl	sftl_printk
.L1771:
	ldr	r3, [r9, #8]
	cmp	r7, r3
	beq	.L1772
	movw	r2, #1125
	ldr	r1, .L1800+4
	ldr	r0, .L1800+8
	bl	sftl_printk
	b	.L1772
.L1768:
	ldr	r3, [r4, #3300]
	movs	r1, #0
	ldrh	r2, [r4, #314]
	add	r3, r3, r8
	ldr	r0, [r3, #8]
	bl	ftl_memset
	b	.L1772
.L1773:
	ldrh	r1, [r4, #262]
	ldr	r0, [sp, #8]
	ldr	r3, [r4, #3300]
	muls	r1, r7, r1
	add	r3, r3, r8
	sub	r1, r1, r10
	add	r1, r0, r1, lsl #9
	ldr	r0, [r3, #8]
	b	.L1796
.L1794:
	ldr	r3, [r4, #3300]
	ldr	r2, [sp, #8]
	add	r8, r8, r3
	ldrh	r3, [r4, #262]
	muls	r3, r7, r3
	sub	r3, r3, r10
	add	r3, r2, r3, lsl #9
	str	r3, [r8, #8]
	b	.L1765
.L1780:
	mov	r5, #256
	movw	r6, #65535
.L1783:
	ldrh	r3, [r4, #176]
	cmp	r3, r6
	bne	.L1781
	ldrh	r3, [r4, #226]
	cmp	r3, r6
	bne	.L1781
	movs	r0, #0
	bl	List_get_gc_head_node
	uxth	r0, r0
	bl	FtlGcRefreshBlock
.L1781:
	movs	r1, #1
	movs	r3, #128
	mov	r0, r1
	strh	r3, [r4, #3158]	@ movhi
	strh	r3, [r4, #3156]	@ movhi
	bl	rk_ftl_garbage_collect
	movs	r1, #1
	movs	r0, #0
	bl	rk_ftl_garbage_collect
	ldrh	r3, [r4, #228]
	cmp	r3, #2
	bhi	.L1782
	subs	r5, r5, #1
	bne	.L1783
	b	.L1782
.L1784:
	mov	r0, #-1
	b	.L1746
.L1801:
	.align	2
.L1800:
	.word	.LC128
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
	ldr	r4, .L1833
	ldr	r3, [r4, #3304]
	ldrh	r2, [r4, #244]
	ldr	r0, [r4, #76]
	str	r3, [r4, #3464]
	ldr	r3, [r4, #3336]
	lsls	r2, r2, #1
	str	r3, [r4, #3468]
	bl	ftl_memset
	ldrh	r0, [r4, #2628]
	movw	r3, #65535
	cmp	r0, r3
	bne	.L1804
.L1815:
	mov	r0, #-1
.L1803:
	add	sp, sp, #16
	@ sp needed
	pop	{r4, r5, r6, r7, r8, r9, r10, pc}
.L1804:
	movs	r1, #1
	ldr	r8, .L1833+20
	bl	FtlGetLastWrittenPage
	ldr	r9, .L1833+4
	add	r7, r4, #3456
	ldrsh	r6, [r4, #2628]
	sxth	r5, r0
	adds	r0, r0, #1
	strh	r0, [r4, #2630]	@ movhi
.L1806:
	cmp	r5, #0
	bge	.L1812
	movw	r2, #1465
	ldr	r1, .L1833+4
	ldr	r0, .L1833+8
	bl	sftl_printk
	b	.L1811
.L1812:
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
	beq	.L1807
	ldr	r3, [r4, #3456]
	adds	r3, r3, #1
	beq	.L1807
	ldrh	r1, [r4, #314]
	ldr	r0, [r4, #3464]
	bl	js_hash
	cmp	r10, r0
	beq	.L1807
	ldrh	r3, [r4, #2632]
	mov	r2, r6
	str	r0, [sp, #8]
	mov	r1, r9
	str	r10, [sp, #4]
	str	r3, [sp]
	mov	r3, r5
	ldr	r0, .L1833+12
	bl	sftl_printk
	cbnz	r5, .L1808
	ldrh	r3, [r4, #2632]
	cmp	r6, r3
	beq	.L1808
	ldrh	r5, [r4, #308]
	sxth	r6, r3
.L1810:
	subs	r5, r5, #1
	sxth	r5, r5
	b	.L1806
.L1808:
	mov	r3, #-1
	str	r3, [r4, #3456]
.L1807:
	ldr	r3, [r4, #3456]
	adds	r3, r3, #1
	beq	.L1810
	ldr	r3, [r4, #3304]
	ldr	r3, [r3]
	cmp	r3, r8
	bne	.L1810
	ldr	r3, [r4, #3336]
	ldrh	r2, [r3]
	movw	r3, #61604
	cmp	r2, r3
	bne	.L1810
.L1811:
	ldrh	r3, [r4, #244]
	ldrh	r2, [r4, #314]
	adds	r3, r3, #24
	cmp	r2, r3, lsl #1
	bcs	.L1814
	movw	r2, #1467
	ldr	r1, .L1833+4
	ldr	r0, .L1833+8
	bl	sftl_printk
.L1814:
	movs	r2, #48
	ldr	r1, [r4, #3464]
	ldr	r0, .L1833+16
	bl	ftl_memcpy
	ldrh	r2, [r4, #244]
	ldr	r1, [r4, #3464]
	ldr	r0, [r4, #76]
	lsls	r2, r2, #1
	adds	r1, r1, #48
	bl	ftl_memcpy
	ldrh	r1, [r4, #244]
	ldr	r3, [r4, #3464]
	ldr	r0, [r4, #24]
	lsrs	r2, r1, #3
	adds	r1, r1, #24
	lsls	r1, r1, #1
	adds	r2, r2, #4
	bic	r1, r1, #3
	add	r1, r1, r3
	bl	ftl_memcpy
	ldr	r2, [r4, #2472]
	ldr	r3, .L1833+20
	cmp	r2, r3
	bne	.L1815
	ldrb	r2, [r4, #2482]	@ zero_extendqisi2
	ldrh	r3, [r4, #258]
	ldrh	r5, [r4, #2480]
	cmp	r2, r3
	strh	r5, [r4, #2634]	@ movhi
	bne	.L1815
	ldrh	r3, [r4, #306]
	ldrh	r2, [r4, #262]
	ldr	r6, [r4, #248]
	ldrh	r0, [r4, #362]
	muls	r3, r5, r3
	str	r5, [r4, #3676]
	ldrh	r1, [r4, #236]
	subs	r0, r6, r0
	subs	r0, r0, r5
	str	r3, [r4, #2556]
	muls	r3, r2, r3
	str	r3, [r4, #344]
	bl	__aeabi_uidiv
	cmp	r5, r6
	strh	r0, [r4, #2624]	@ movhi
	bls	.L1816
	movw	r2, #1489
	ldr	r1, .L1833+4
	ldr	r0, .L1833+8
	bl	sftl_printk
.L1816:
	ldrh	r3, [r4, #2488]
	ldrh	r1, [r4, #2486]
	lsrs	r2, r3, #6
	and	r3, r3, #63
	strb	r3, [r4, #34]
	strh	r2, [r4, #30]	@ movhi
	ldrh	r2, [r4, #2490]
	ldrb	r3, [r4, #2483]	@ zero_extendqisi2
	strh	r1, [r4, #28]	@ movhi
	strh	r2, [r4, #80]	@ movhi
	ldrh	r2, [r4, #2492]
	strb	r3, [r4, #36]
	movw	r3, #65535
	strh	r3, [r4, #176]	@ movhi
	movs	r3, #0
	strh	r3, [r4, #178]	@ movhi
	lsrs	r0, r2, #6
	and	r2, r2, #63
	strb	r2, [r4, #86]
	ldrb	r2, [r4, #2484]	@ zero_extendqisi2
	strh	r0, [r4, #82]	@ movhi
	strb	r3, [r4, #182]
	strb	r2, [r4, #88]
	ldrh	r2, [r4, #2494]
	strb	r3, [r4, #184]
	str	r3, [r4, #2580]
	strh	r2, [r4, #128]	@ movhi
	ldrh	r2, [r4, #2496]
	str	r3, [r4, #2568]
	str	r3, [r4, #2560]
	str	r3, [r4, #2576]
	lsrs	r0, r2, #6
	and	r2, r2, #63
	strb	r2, [r4, #134]
	ldrb	r2, [r4, #2485]	@ zero_extendqisi2
	str	r3, [r4, #2604]
	strh	r0, [r4, #130]	@ movhi
	strb	r2, [r4, #136]
	ldr	r2, [r4, #2504]
	str	r2, [r4, #2600]
	str	r3, [r4, #2616]
	ldr	r2, [r4, #2592]
	str	r3, [r4, #2572]
	ldr	r3, [r4, #2512]
	cmp	r3, r2
	ldr	r2, [r4, #2596]
	it	hi
	strhi	r3, [r4, #2592]
	ldr	r3, [r4, #2508]
	cmp	r3, r2
	it	hi
	strhi	r3, [r4, #2596]
	movw	r3, #65535
	cmp	r1, r3
	beq	.L1819
	ldr	r0, .L1833+24
	bl	make_superblock
.L1819:
	ldrh	r2, [r4, #80]
	movw	r3, #65535
	cmp	r2, r3
	beq	.L1820
	ldr	r0, .L1833+28
	bl	make_superblock
.L1820:
	ldrh	r2, [r4, #128]
	movw	r3, #65535
	cmp	r2, r3
	beq	.L1821
	ldr	r0, .L1833+32
	bl	make_superblock
.L1821:
	ldrh	r2, [r4, #176]
	movw	r3, #65535
	cmp	r2, r3
	beq	.L1822
	ldr	r0, .L1833+36
	bl	make_superblock
.L1822:
	movs	r0, #0
	b	.L1803
.L1834:
	.align	2
.L1833:
	.word	.LANCHOR0
	.word	.LANCHOR1+600
	.word	.LC8
	.word	.LC129
	.word	.LANCHOR0+2472
	.word	1179929683
	.word	.LANCHOR0+28
	.word	.LANCHOR0+80
	.word	.LANCHOR0+128
	.word	.LANCHOR0+176
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
	ldr	r4, .L1876
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
.L1836:
	ldr	r3, [sp, #12]
	sxth	r6, r8
	cmp	r6, r3
	bge	.L1855
	ldr	r3, [sp, #12]
	lsls	r7, r6, #1
	subs	r3, r3, #1
	cmp	r6, r3
	bne	.L1837
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
.L1838:
	ldr	r3, [sp, #12]
	sxth	r9, r8
	adds	r2, r3, #1
	cmp	r9, r2
	blt	.L1841
.L1855:
	mov	r0, r5
	bl	ftl_free_no_use_map_blk
	ldrh	r2, [r5, #2]
	ldrh	r3, [r4, #308]
	cmp	r2, r3
	bne	.L1843
	mov	r0, r5
	bl	ftl_map_blk_alloc_new_blk
.L1843:
	mov	r0, r5
	bl	ftl_map_blk_gc
	mov	r0, r5
	bl	ftl_map_blk_gc
	movs	r0, #0
	add	sp, sp, #36
	@ sp needed
	pop	{r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L1841:
	ldrh	r2, [r7]
	ldr	r0, .L1876+4
	orr	r2, r9, r2, lsl #10
	str	r2, [r4, #3460]
	movs	r2, #1
	mov	r1, r2
	bl	FlashReadPages
	ldr	r2, [r4, #3468]
	ldr	r2, [r2, #12]
	str	r2, [sp, #16]
	cbz	r2, .L1839
	ldr	r1, [r4, #3456]
	adds	r1, r1, #1
	beq	.L1839
	ldrh	r1, [r4, #314]
	ldr	r0, [r4, #3464]
	bl	js_hash
	ldr	r2, [sp, #16]
	cmp	r2, r0
	beq	.L1839
	str	r0, [sp, #4]
	mov	r3, r9
	str	r2, [sp]
	mov	r2, r6
	ldr	r1, .L1876+8
	ldr	r0, .L1876+12
	bl	sftl_printk
	mov	r3, #-1
	str	r3, [r4, #3456]
.L1839:
	ldr	r3, [r4, #3456]
	adds	r3, r3, #1
	beq	.L1840
	ldrh	r3, [fp, #8]
	cmp	r10, r3
	bls	.L1840
	ldrh	r1, [fp]
	ldrh	r2, [r5, #4]
	cmp	r1, r2
	ittt	eq
	ldreq	r2, [r4, #3460]
	ldreq	r1, [sp, #8]
	streq	r2, [r1, r3, lsl #2]
.L1840:
	add	r8, r8, #1
	b	.L1838
.L1837:
	ldr	r3, [r4, #3304]
	ldr	r0, .L1876+4
	str	r3, [r4, #3464]
	add	r3, r9, r7
	ldrh	r2, [r9, r6, lsl #1]
	str	r3, [sp, #20]
	ldrh	r3, [r4, #308]
	subs	r3, r3, #1
	orr	r3, r3, r2, lsl #10
	movs	r2, #1
	mov	r1, r2
	str	r3, [r4, #3460]
	bl	FlashReadPages
	ldr	r3, [r4, #3456]
	adds	r3, r3, #1
	beq	.L1857
	ldrh	r2, [fp]
	ldrh	r3, [r5, #4]
	cmp	r2, r3
	bne	.L1857
	ldrh	r2, [fp, #8]
	movw	r3, #64245
	cmp	r2, r3
	beq	.L1845
.L1857:
	movs	r7, #0
.L1846:
	ldrh	r2, [r4, #308]
	sxth	r3, r7
	cmp	r3, r2
	bge	.L1853
	ldr	r2, [sp, #20]
	ldr	r0, .L1876+4
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
	cbz	r2, .L1850
	ldr	r1, [r4, #3456]
	adds	r1, r1, #1
	beq	.L1850
	ldrh	r1, [r4, #314]
	ldr	r0, [r4, #3464]
	bl	js_hash
	ldr	r2, [sp, #24]
	cmp	r2, r0
	beq	.L1850
	str	r0, [sp, #4]
	str	r2, [sp]
	mov	r2, r6
	ldr	r3, [sp, #28]
	ldr	r1, .L1876+8
	ldr	r0, .L1876+16
	bl	sftl_printk
	mov	r3, #-1
	str	r3, [r4, #3456]
.L1850:
	ldr	r3, [r4, #3456]
	adds	r3, r3, #1
	beq	.L1851
	ldrh	r3, [fp, #8]
	cmp	r10, r3
	bls	.L1851
	ldrh	r1, [fp]
	ldrh	r2, [r5, #4]
	cmp	r1, r2
	ittt	eq
	ldreq	r2, [r4, #3460]
	ldreq	r1, [sp, #8]
	streq	r2, [r1, r3, lsl #2]
.L1851:
	adds	r7, r7, #1
	b	.L1846
.L1845:
	movs	r1, #0
	movs	r0, #4
.L1847:
	ldrh	r2, [r4, #308]
	sxth	r3, r1
	subs	r2, r2, #1
	cmp	r3, r2
	blt	.L1849
.L1853:
	add	r8, r8, #1
	b	.L1836
.L1849:
	ldr	r6, [r4, #3304]
	ldr	r2, [r6, r3, lsl #3]
	uxth	r7, r2
	cmp	r10, r7
	bls	.L1848
	add	r3, r0, r3, lsl #3
	ldr	r3, [r6, r3]
	ldr	r6, [sp, #8]
	str	r3, [r6, r7, lsl #2]
.L1848:
	adds	r1, r1, #1
	b	.L1847
.L1877:
	.align	2
.L1876:
	.word	.LANCHOR0
	.word	.LANCHOR0+3456
	.word	.LANCHOR1+615
	.word	.LC130
	.word	.LC131
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
	ldr	r0, .L1879
	push	{r3, lr}
	.save {r3, lr}
	ldrh	r3, [r0, #324]
	strh	r3, [r0, #3622]	@ movhi
	movw	r3, #61574
	strh	r3, [r0, #3616]	@ movhi
	ldrh	r3, [r0, #348]
	strh	r3, [r0, #3620]	@ movhi
	ldrh	r3, [r0, #326]
	strh	r3, [r0, #3618]	@ movhi
	ldr	r3, [r0, #352]
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
.L1880:
	.align	2
.L1879:
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
	ldr	r0, .L1882
	bl	FtlMapTblRecovery
	movs	r0, #0
	pop	{r3, pc}
.L1883:
	.align	2
.L1882:
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
	ldr	r4, .L1900
	ldrh	r0, [r4, #240]
	strh	r3, [r4, #3476]	@ movhi
	bl	FtlFreeSysBlkQueueInit
	bl	FtlScanSysBlk
	ldrh	r2, [r4, #2628]
	movw	r3, #65535
	cmp	r2, r3
	bne	.L1885
.L1887:
	mov	r5, #-1
.L1884:
	mov	r0, r5
	pop	{r4, r5, r6, pc}
.L1885:
	bl	FtlLoadSysInfo
	mov	r5, r0
	cmp	r0, #0
	bne	.L1887
	bl	FtlLoadMapInfo
	bl	FtlLoadVonderInfo
	bl	Ftl_load_ext_data
	bl	FtlLoadEctTbl
	bl	FtlFreeSysBLkSort
	bl	SupperBlkListInit
	bl	FtlPowerLostRecovery
	movs	r0, #1
	bl	FtlUpdateVaildLpn
	ldrh	r1, [r4, #342]
	mov	r3, r5
	ldr	r2, [r4, #2544]
	movs	r0, #12
.L1888:
	cmp	r3, r1
	bge	.L1893
	mla	r6, r0, r3, r2
	ldr	r6, [r6, #4]
	cmp	r6, #0
	bge	.L1889
.L1893:
	ldrh	r2, [r4, #2500]
	cmp	r3, r1
	add	r2, r2, #1
	strh	r2, [r4, #2500]	@ movhi
	bge	.L1899
.L1890:
	ldr	r0, .L1900+4
	bl	FtlSuperblockPowerLostFix
	ldr	r0, .L1900+8
	bl	FtlSuperblockPowerLostFix
	ldrh	r1, [r4, #28]
	ldr	r2, [r4, #76]
	ldrh	r0, [r4, #32]
	ldrh	r3, [r2, r1, lsl #1]
	subs	r3, r3, r0
	strh	r3, [r2, r1, lsl #1]	@ movhi
	ldrh	r3, [r4, #306]
	ldr	r1, [r4, #76]
	ldrh	r0, [r4, #80]
	strh	r3, [r4, #30]	@ movhi
	movs	r3, #0
	strb	r3, [r4, #34]
	strh	r3, [r4, #32]	@ movhi
	ldrh	r6, [r4, #84]
	ldrh	r2, [r1, r0, lsl #1]
	subs	r2, r2, r6
	strh	r2, [r1, r0, lsl #1]	@ movhi
	strb	r3, [r4, #86]
	strh	r3, [r4, #84]	@ movhi
	ldrh	r3, [r4, #2502]
	ldrh	r2, [r4, #306]
	adds	r3, r3, #1
	strh	r2, [r4, #82]	@ movhi
	strh	r3, [r4, #2502]	@ movhi
	bl	l2p_flush
	bl	FtlVpcTblFlush
	bl	FtlVpcTblFlush
	b	.L1894
.L1889:
	adds	r3, r3, #1
	b	.L1888
.L1899:
	ldrh	r3, [r4, #3476]
	cmp	r3, #0
	bne	.L1890
.L1894:
	ldrh	r0, [r4, #28]
	movw	r3, #65535
	cmp	r0, r3
	beq	.L1895
	ldrh	r3, [r4, #32]
	cbnz	r3, .L1895
	ldrh	r3, [r4, #84]
	cbnz	r3, .L1895
	bl	FtlGcRefreshBlock
	ldrh	r0, [r4, #80]
	bl	FtlGcRefreshBlock
	bl	FtlVpcTblFlush
	ldr	r0, .L1900+4
	bl	allocate_new_data_superblock
	ldr	r0, .L1900+8
	bl	allocate_new_data_superblock
.L1895:
	bl	FtlVpcCheckAndModify
	b	.L1884
.L1901:
	.align	2
.L1900:
	.word	.LANCHOR0
	.word	.LANCHOR0+28
	.word	.LANCHOR0+80
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
	ldr	r4, .L1926
	ldrh	r0, [r4, #240]
	str	r3, [r4, #2592]
	str	r3, [r4, #2596]
	str	r3, [r4, #2608]
	bl	FtlFreeSysBlkQueueInit
	bl	FtlLoadBbt
	cbz	r0, .L1903
	bl	FtlMakeBbt
.L1903:
	ldr	r0, .L1926+4
	movs	r2, #0
.L1904:
	ldrh	r1, [r4, #262]
	uxth	r3, r2
	adds	r2, r2, #1
	cmp	r3, r1, lsl #7
	blt	.L1905
	ldrh	r6, [r4, #244]
	movs	r5, #0
.L1906:
	ldrh	r3, [r4, #246]
	cmp	r3, r6
	bhi	.L1907
	ldrh	r1, [r4, #236]
	subs	r3, r5, #3
	cmp	r3, r1, lsl #1
	bge	.L1908
.L1912:
	movs	r5, #0
	mov	r6, r5
.L1909:
	ldrh	r3, [r4, #244]
	uxth	r0, r5
	adds	r5, r5, #1
	cmp	r3, r0
	bhi	.L1913
	ldrh	r3, [r4, #246]
	ldrh	r5, [r4, #236]
	ldr	r9, [r4, #248]
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
	ble	.L1914
	mov	r1, r5
	sub	r0, r9, r6
	bl	__aeabi_uidiv
	str	r0, [r4, #2556]
	lsrs	r0, r0, #5
	adds	r0, r0, #24
	strh	r0, [r4, #2624]	@ movhi
.L1914:
	ldrh	r3, [r4, #298]
	cbz	r3, .L1916
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
.L1916:
	ldrh	r2, [r4, #2624]
	ldr	r3, [r4, #2556]
	ldr	r6, .L1926+8
	subs	r3, r3, r2
	muls	r5, r3, r5
	ldrh	r3, [r4, #306]
	str	r5, [r4, #3676]
	muls	r5, r3, r5
	ldrh	r3, [r4, #262]
	str	r5, [r4, #2556]
	muls	r5, r3, r5
	str	r5, [r4, #344]
	movw	r5, #65535
	bl	FtlBbmTblFlush
	ldrh	r2, [r4, #246]
	movs	r1, #0
	ldr	r0, [r4, #76]
	lsls	r2, r2, #1
	bl	ftl_memset
	ldrh	r2, [r4, #244]
	movs	r3, #0
	str	r3, [r4, #2552]
	movs	r1, #255
	strh	r3, [r4, #178]	@ movhi
	strb	r3, [r4, #182]
	strb	r3, [r4, #184]
	lsrs	r2, r2, #3
	strh	r3, [r4, #30]	@ movhi
	strb	r3, [r4, #34]
	strh	r3, [r4, #28]	@ movhi
	movs	r3, #1
	ldr	r0, [r4, #24]
	strh	r5, [r4, #176]	@ movhi
	strb	r3, [r4, #36]
	bl	ftl_memset
.L1918:
	mov	r0, r6
	bl	make_superblock
	ldrb	r3, [r4, #35]	@ zero_extendqisi2
	ldrh	r2, [r4, #28]
	cmp	r3, #0
	bne	.L1919
	ldr	r3, [r4, #76]
	strh	r5, [r3, r2, lsl #1]	@ movhi
	ldrh	r3, [r4, #28]
	adds	r3, r3, #1
	strh	r3, [r4, #28]	@ movhi
	b	.L1918
.L1905:
	ldr	r5, [r4, #3316]
	mvns	r1, r3
	orr	r1, r3, r1, lsl #16
	str	r1, [r5, r3, lsl #2]
	ldr	r1, [r4, #3320]
	str	r0, [r1, r3, lsl #2]
	b	.L1904
.L1907:
	mov	r0, r6
	movs	r1, #1
	bl	FtlLowFormatEraseBlock
	adds	r6, r6, #1
	add	r5, r5, r0
	uxth	r5, r5
	uxth	r6, r6
	b	.L1906
.L1908:
	mov	r0, r5
	bl	__aeabi_uidiv
	ldr	r3, [r4, #336]
	add	r0, r0, r3
	uxth	r0, r0
	bl	FtlSysBlkNumInit
	ldrh	r0, [r4, #240]
	bl	FtlFreeSysBlkQueueInit
	ldrh	r5, [r4, #244]
.L1910:
	ldrh	r3, [r4, #246]
	cmp	r3, r5
	bls	.L1912
	mov	r0, r5
	movs	r1, #1
	adds	r5, r5, #1
	bl	FtlLowFormatEraseBlock
	uxth	r5, r5
	b	.L1910
.L1913:
	movs	r1, #0
	bl	FtlLowFormatEraseBlock
	add	r6, r6, r0
	uxth	r6, r6
	b	.L1909
.L1919:
	ldr	r3, [r4, #2592]
	movw	r5, #65535
	ldrh	r1, [r4, #32]
	ldr	r6, .L1926+12
	str	r3, [r4, #40]
	adds	r3, r3, #1
	str	r3, [r4, #2592]
	ldr	r3, [r4, #76]
	strh	r1, [r3, r2, lsl #1]	@ movhi
	movs	r3, #0
	strh	r3, [r4, #82]	@ movhi
	strb	r3, [r4, #86]
	ldrh	r3, [r4, #28]
	adds	r3, r3, #1
	strh	r3, [r4, #80]	@ movhi
	movs	r3, #1
	strb	r3, [r4, #88]
.L1920:
	mov	r0, r6
	bl	make_superblock
	ldrb	r3, [r4, #87]	@ zero_extendqisi2
	ldrh	r2, [r4, #80]
	cbnz	r3, .L1921
	ldr	r3, [r4, #76]
	strh	r5, [r3, r2, lsl #1]	@ movhi
	ldrh	r3, [r4, #80]
	adds	r3, r3, #1
	strh	r3, [r4, #80]	@ movhi
	b	.L1920
.L1921:
	ldr	r3, [r4, #2592]
	movw	r5, #65535
	ldrh	r1, [r4, #84]
	str	r3, [r4, #92]
	adds	r3, r3, #1
	str	r3, [r4, #2592]
	ldr	r3, [r4, #76]
	strh	r1, [r3, r2, lsl #1]	@ movhi
	strh	r5, [r4, #128]	@ movhi
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
	cbnz	r0, .L1922
	ldr	r3, .L1926+16
	movs	r2, #1
	str	r2, [r3]
.L1922:
	movs	r0, #0
	pop	{r3, r4, r5, r6, r7, r8, r9, pc}
.L1927:
	.align	2
.L1926:
	.word	.LANCHOR0
	.word	168778952
	.word	.LANCHOR0+28
	.word	.LANCHOR0+80
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
	ldr	r4, .L1934
	ldr	r5, .L1934+4
	ldr	r1, .L1934+8
	ldr	r0, .L1934+12
	str	r3, [r5]
	bl	sftl_printk
	mov	r0, r4
	bl	FtlConstantsInit
	bl	FtlMemInit
	bl	FtlVariablesInit
	ldrh	r0, [r4, #240]
	bl	FtlFreeSysBlkQueueInit
	bl	FtlLoadBbt
	cbnz	r0, .L1932
	bl	FtlSysBlkInit
	cbnz	r0, .L1932
	movs	r3, #1
	str	r3, [r5]
	ldrh	r3, [r4, #228]
	cmp	r3, #15
	bhi	.L1932
	movw	r4, #8129
.L1931:
	movs	r1, #1
	movs	r0, #0
	bl	rk_ftl_garbage_collect
	subs	r4, r4, #1
	bne	.L1931
.L1932:
	movs	r0, #0
	pop	{r3, r4, r5, pc}
.L1935:
	.align	2
.L1934:
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
	@ args = 0, pretend = 0, frame = 96
	@ frame_needed = 0, uses_anonymous_args = 0
	push	{r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.save {r4, r5, r6, r7, r8, r9, r10, fp, lr}
	adds	r7, r1, r0
	add	r8, r7, #-1
	.pad #108
	sub	sp, sp, #108
	cmp	r8, #63
	mov	r4, r0
	mov	r6, r1
	mov	r9, r2
	ldr	r5, .L2006
	bls	.L1937
	cmp	r0, #576
	bcc	.L1938
.L1937:
	ldr	r3, [r5, #3680]
	cbnz	r3, .L1939
.L2005:
	movs	r4, #0
	b	.L1936
.L1939:
	ldr	r3, [r5, #3684]
	ldr	r2, [r3]
	str	r3, [sp, #8]
	ldr	r3, .L2006+4
	cmp	r2, r3
	bne	.L1941
	ldr	r3, [sp, #8]
	movs	r2, #0
	ldrh	r1, [r5, #10]
	add	r0, r3, #262144
	movw	r3, #65535
.L1945:
	ldr	r4, [r0, #-4]
	cbnz	r4, .L1942
	ldr	r4, [sp, #8]
	subs	r3, r3, #1
	ldr	r4, [r4, r2, lsl #2]
	adds	r2, r2, #1
	cmp	r2, #4096
	it	hi
	movhi	r2, #0
	cmp	r3, #4096
	str	r4, [r0, #-4]!
	bne	.L1945
	mov	r3, #512
	b	.L2002
.L1942:
	adds	r3, r3, #127
	lsrs	r3, r3, #7
.L2002:
	str	r3, [sp, #12]
	lsls	r1, r1, #2
	ldr	r3, [sp, #12]
	uxth	r1, r1
	ldr	r4, .L2006
	adds	r0, r3, #4
	bl	__aeabi_uidiv
	adds	r3, r0, #1
	str	r3, [sp, #20]
	ldr	r3, [sp, #12]
	lsls	r3, r3, #7
	str	r3, [sp, #28]
	movs	r3, #0
	mov	fp, r3
	str	r3, [sp, #16]
.L1967:
	mov	r1, #512
	ldr	r0, [r4, #3688]
	bl	__memzero
	ldrh	r7, [r4, #10]
	ldr	r3, [r4, #3264]
	mul	r9, fp, r7
	cmp	r3, #0
	beq	.L1975
	ldr	r3, [r4, #3268]
	cmp	r3, #0
	ite	eq
	moveq	r3, #6
	movne	r3, #9
.L2003:
	str	r3, [sp, #24]
	mov	r1, r9
	ldr	r3, .L2006
	movs	r0, #0
	ldr	r3, [r3, #3252]
	blx	r3
	ldr	r3, [sp, #20]
	cmp	r3, #1
	beq	.L1947
	ldr	r3, .L2006
	add	r1, r7, r9
	movs	r0, #0
	ldr	r3, [r3, #3252]
	blx	r3
.L1947:
	ldr	r3, [sp, #24]
	cmp	r3, #9
	bne	.L1977
	ldr	r3, .L2006
	mov	r1, #1024
	ldr	r5, [r3, #3688]
	mov	r0, r5
	bl	__memzero
	movs	r2, #4
	ldr	r3, .L2006+8
	strb	r2, [r5, #17]
	movs	r1, #12
	ldr	r2, .L2006
	adds	r0, r5, r1
	str	r3, [r5]
	movs	r3, #0
	str	r1, [r5, #4]
	ldrh	r2, [r2, #10]
	str	r3, [r5, #12]
	strb	r3, [r5, #16]
	strh	r2, [r5, #18]	@ movhi
	movs	r2, #16
	strb	r3, [r5, #20]
	strb	r2, [r5, #21]
	strh	r3, [r5, #22]	@ movhi
	bl	js_hash
	str	r0, [r5, #8]
.L1948:
	ldr	r3, [sp, #20]
	mov	r10, r9
	mov	r8, #0
	muls	r3, r7, r3
	ldr	r7, [sp, #8]
	str	r3, [sp, #32]
.L1949:
	ldr	r3, [sp, #32]
	cmp	r8, r3
	beq	.L1956
	ldr	r3, [sp, #24]
	cmp	r3, #9
	itet	ne
	addne	r3, r9, r8
	addeq	r3, r10, #1
	lslne	r3, r3, #2
	str	r3, [sp, #40]
	movw	r3, #61424
	str	r3, [sp, #44]
	cmp	r8, #0
	bne	.L1952
	ldr	r3, [sp, #24]
	cmp	r3, #9
	bne	.L1952
	ldr	r3, [r4, #3264]
	movs	r0, #70
	blx	r3
	mov	r2, r5
	add	r3, sp, #40
	mov	r1, r9
	mov	r0, r8
	ldr	r6, [r4, #3256]
	blx	r6
	ldr	r3, [r4, #3264]
	str	r0, [sp, #36]
	ldrb	r0, [r4, #22]	@ zero_extendqisi2
	blx	r3
	ldr	r2, [sp, #36]
	adds	r2, r2, #1
	bne	.L1953
.L1956:
	ldrb	r3, [r4, #14]	@ zero_extendqisi2
	ldr	r2, [r4, #3264]
	ldr	r8, [r4, #3688]
	str	r3, [sp, #24]
	ldrh	r3, [r4, #10]
	mul	r10, fp, r3
	cmp	r2, #0
	bne	.L1954
	mov	r9, #6
.L1955:
	ldr	r2, [sp, #20]
	movs	r7, #0
	mul	r5, r3, r2
.L1958:
	cmp	r7, r5
	beq	.L1962
	cmp	r7, #0
	bne	.L1959
	cmp	r9, #9
	bne	.L1959
	ldr	r3, [r4, #3264]
	movs	r0, #70
	blx	r3
	ldr	r3, [r4, #3268]
	movs	r0, #2
	blx	r3
	mov	r2, r8
	mov	r1, r10
	add	r3, sp, #40
	mov	r0, r7
	ldr	r6, [r4, #3260]
	blx	r6
	ldr	r3, [r4, #3268]
	ldr	r0, [sp, #24]
	blx	r3
	ldr	r3, [r4, #3264]
	ldrb	r0, [r4, #22]	@ zero_extendqisi2
	blx	r3
	ldr	r3, [r8]
	ldr	r2, .L2006+8
	cmp	r3, r2
	beq	.L1960
.L1962:
	ldr	r0, [r4, #3688]
	movs	r5, #0
	ldr	r1, [sp, #8]
.L1961:
	mov	r8, r1
	ldr	r2, [r0, r5, lsl #2]
	ldr	r3, [r8]
	lsls	r7, r5, #2
	adds	r1, r1, #4
	cmp	r2, r3
	beq	.L1964
	mov	r1, #512
	bl	__memzero
	ldr	r3, .L2006
	mov	r1, fp
	ldr	r0, .L2006+12
	ldr	r2, [r3, #3688]
	str	r5, [sp]
	ldr	r3, [r8]
	ldr	r2, [r2, r7]
	bl	sftl_printk
	ldr	r3, .L2006
	movs	r0, #0
	ldrh	r1, [r3, #10]
	ldr	r3, [r3, #3252]
	mul	r1, fp, r1
	blx	r3
.L1965:
	ldr	r3, [sp, #20]
	add	fp, fp, r3
	cmp	fp, #15
	bls	.L1967
	ldr	r3, [sp, #16]
	cbnz	r3, .L1968
.L1941:
	mov	r3, #-1
	str	r3, [sp, #12]
.L1968:
	ldr	r5, .L2006
	movs	r3, #0
	ldr	r4, [sp, #12]
	ldr	r0, [r5, #3684]
	str	r3, [r5, #3680]
	bl	kfree
	ldr	r0, [r5, #3688]
	bl	kfree
.L1936:
	mov	r0, r4
	add	sp, sp, #108
	@ sp needed
	pop	{r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L1975:
	movs	r3, #6
	b	.L2003
.L1977:
	movs	r5, #0
	b	.L1948
.L1952:
	add	r3, sp, #40
	mov	r2, r7
	mov	r1, r10
	movs	r0, #0
	ldr	r6, [r4, #3256]
	blx	r6
	adds	r0, r0, #1
	beq	.L1956
	add	r7, r7, #2048
.L1953:
	add	r8, r8, #1
	add	r10, r10, #1
	b	.L1949
.L1954:
	ldr	r2, [r4, #3268]
	cmp	r2, #0
	ite	eq
	moveq	r9, #6
	movne	r9, #9
	b	.L1955
.L1959:
	add	r3, sp, #40
	mov	r2, r8
	add	r1, r10, r7
	movs	r0, #0
	ldr	r6, [r4, #3260]
	blx	r6
	adds	r0, r0, #1
	beq	.L1962
	ldr	r2, [sp, #44]
	movw	r3, #61424
	cmp	r2, r3
	bne	.L1962
	add	r8, r8, #2048
.L1960:
	adds	r7, r7, #1
	b	.L1958
.L1964:
	ldr	r3, [sp, #28]
	adds	r5, r5, #1
	cmp	r5, r3
	bne	.L1961
	ldr	r3, [sp, #16]
	adds	r3, r3, #1
	cmp	r3, #5
	str	r3, [sp, #16]
	bls	.L1965
	b	.L1968
.L2007:
	.align	2
.L2006:
	.word	.LANCHOR0
	.word	-52655045
	.word	1179535694
	.word	.LC132
.L1938:
	cmp	r0, #64
	bne	.L1969
	mov	r0, #262144
	bl	ftl_malloc
	str	r0, [r5, #3684]
	mov	r0, #262144
	bl	ftl_malloc
	ldr	r3, [r5, #3684]
	str	r0, [r5, #3688]
	cbz	r3, .L1970
	cbz	r0, .L1970
	movs	r2, #1
	mov	r1, #262144
	mov	r0, r3
	str	r2, [r5, #3680]
	bl	__memzero
.L1969:
	ldr	r3, [r5, #3680]
	cmp	r3, #0
	beq	.L2005
	cmp	r4, #63
	ldr	r1, [r5, #3684]
	iteee	hi
	ldrhi	r5, .L2008
	rsbls	r4, r4, #64
	subls	r6, r6, r4
	addls	r9, r9, r4, lsl #9
	it	ls
	movls	r5, r1
	ldr	r0, .L2008+4
	it	hi
	addhi	r5, r5, r4
	mov	r2, #262144
	it	hi
	addhi	r5, r1, r5, lsl #9
	cmp	r8, #576
	itt	cs
	subcs	r6, r6, r7
	subcs	r6, r6, #446
	mov	r3, r5
	str	r6, [sp]
	bl	sftl_printk
	lsls	r2, r6, #9
	mov	r1, r9
	mov	r0, r5
	bl	ftl_memcpy
	b	.L2005
.L1970:
	ldr	r1, .L2008+8
	ldr	r0, .L2008+12
	bl	sftl_printk
	b	.L1969
.L2009:
	.align	2
.L2008:
	.word	8388544
	.word	.LC134
	.word	.LANCHOR1+633
	.word	.LC133
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
	ldr	r2, .L2014
	ldr	r3, [r2, #3692]
	cbnz	r3, .L2013
	str	r0, [r2, #3692]
	mov	r0, r3
	str	r1, [r2, #3696]
	bx	lr
.L2013:
	mov	r0, #-1
	bx	lr
.L2015:
	.align	2
.L2014:
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
	push	{r3, r4, r5, r6, r7, r8, r9, lr}
	.save {r3, r4, r5, r6, r7, r8, r9, lr}
	mov	r0, #65536
	ldr	r4, .L2024
	bl	ftl_malloc
	str	r0, [r4, #3700]
	cmp	r0, #0
	beq	.L2022
	ldr	r8, .L2024+4
	mov	r9, #0
	mov	r7, r9
	mov	r6, r9
.L2020:
	ldr	r3, [r4, #3692]
	movs	r1, #128
	ldr	r2, [r4, #3700]
	lsls	r0, r6, #7
	blx	r3
	mov	r5, r0
	cbnz	r0, .L2018
	ldr	r2, [r4, #3700]
	ldr	r3, [r2]
	cmp	r3, r8
	bne	.L2019
	movw	r3, #65532
	ldr	r1, [r2, r3]
	ldr	r3, [r2, #4]
	cmp	r1, r3
	bne	.L2019
	cmp	r1, r7
	itt	hi
	movhi	r9, r6
	movhi	r7, r1
.L2019:
	adds	r6, r6, #1
	cmp	r6, #2
	bne	.L2020
	cbz	r7, .L2021
	ldr	r3, [r4, #3692]
	movs	r1, #128
	lsl	r0, r9, #7
	blx	r3
	mov	r5, r0
	cbz	r0, .L2016
.L2018:
	ldr	r0, [r4, #3700]
	mov	r5, #-1
	bl	kfree
	movs	r3, #0
	str	r3, [r4, #3700]
	b	.L2016
.L2021:
	mov	r1, #65536
	mov	r0, r2
	bl	__memzero
	ldr	r3, [r4, #3700]
	movs	r2, #1
	movw	r1, #65532
	str	r2, [r3, #4]
	str	r8, [r3]
	str	r2, [r3, r1]
	movw	r2, #64504
	strh	r7, [r3, #12]	@ movhi
	strh	r2, [r3, #14]	@ movhi
.L2016:
	mov	r0, r5
	pop	{r3, r4, r5, r6, r7, r8, r9, pc}
.L2022:
	mvn	r5, #11
	b	.L2016
.L2025:
	.align	2
.L2024:
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
	ldr	r3, .L2032
	mov	r0, r1
	ldr	r5, [r3, #3700]
	cbz	r5, .L2031
	ldrh	r6, [r5, #10]
	movs	r3, #0
.L2028:
	cmp	r3, r6
	bcc	.L2030
.L2031:
	mov	r0, #-1
	pop	{r3, r4, r5, r6, r7, pc}
.L2030:
	add	r1, r5, r3, lsl #3
	ldrh	r4, [r1, #16]
	cmp	r4, r7
	bne	.L2029
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
.L2029:
	adds	r3, r3, #1
	b	.L2028
.L2033:
	.align	2
.L2032:
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
	ldr	r2, .L2053
	.pad #28
	sub	sp, sp, #28
	mov	r3, r1
	ldr	r4, [r2, #3700]
	mov	r9, r2
	cmp	r4, #0
	beq	.L2049
	ldrh	r2, [r4, #10]
	add	r6, r8, #63
	ldrh	r1, [r4, #8]
	bic	r6, r6, #63
	movs	r7, #0
	str	r1, [sp, #4]
.L2036:
	cmp	r7, r2
	bcc	.L2044
	ldrh	r1, [r4, #14]
	cmp	r6, r1
	bhi	.L2049
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
	b	.L2052
.L2044:
	add	r5, r4, r7, lsl #3
	ldrh	r1, [r5, #16]
	cmp	r1, r0
	str	r1, [sp, #8]
	bne	.L2037
	ldrh	r1, [r5, #20]
	add	fp, r4, #1024
	adds	r1, r1, #63
	bic	r1, r1, #63
	cmp	r8, r1
	str	r1, [sp, #12]
	bls	.L2038
	ldrh	r1, [r4, #14]
	cmp	r6, r1
	bhi	.L2049
	ldrh	r10, [r5, #18]
	subs	r2, r2, #1
	str	r2, [sp, #16]
.L2039:
	ldr	r2, [sp, #16]
	adds	r5, r5, #8
	cmp	r7, r2
	bcc	.L2040
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
.L2041:
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
	ldr	r3, .L2053
	ldr	r3, [r3, #3696]
.L2052:
	ldr	r0, [sp, #4]
	mov	r2, r4
	movs	r1, #128
	lsls	r0, r0, #7
	blx	r3
	movs	r0, #0
.L2034:
	add	sp, sp, #28
	@ sp needed
	pop	{r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L2040:
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
	b	.L2039
.L2038:
	ldrh	r0, [r5, #18]
	mov	r2, r8
	mov	r1, r3
	add	r0, r0, fp
	bl	memcpy
	strh	r8, [r5, #20]	@ movhi
	b	.L2041
.L2037:
	adds	r7, r7, #1
	b	.L2036
.L2049:
	mov	r0, #-1
	b	.L2034
.L2054:
	.align	2
.L2053:
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
	push	{r4, r5, r6, lr}
	.save {r4, r5, r6, lr}
	mov	r0, #4096
	mov	r6, r1
	mov	r4, r2
	bl	ftl_malloc
	mov	r5, r0
	cmp	r0, #0
	beq	.L2072
	ldr	r3, .L2083
	cmp	r6, r3
	beq	.L2058
	adds	r3, r3, #1
	cmp	r6, r3
	beq	.L2059
.L2078:
	mvn	r4, #13
	b	.L2057
.L2058:
	ldr	r6, .L2083+4
	mov	r3, sp
	ands	r3, r3, r6
	ldr	r3, [r3, #72]
	.syntax unified
@ 564 "./arch/arm/include/asm/uaccess.h" 1
	adds r2, r4, #8; sbcccs r2, r2, r3; movcc r3, #0
@ 0 "" 2
	.thumb
	.syntax unified
	cbnz	r3, .L2060
	movs	r2, #8
	mov	r1, r4
	bl	arm_copy_from_user
	cbz	r0, .L2079
.L2061:
	mov	r2, r4
	mov	r1, #256
.L2082:
	ldr	r0, .L2083+8
	bl	sftl_printk
	b	.L2078
.L2060:
	movs	r1, #8
	bl	__memzero
	b	.L2061
.L2079:
	ldr	r2, [r5]
	ldr	r3, .L2083+12
	cmp	r2, r3
	beq	.L2063
.L2064:
	mov	r4, #-1
.L2057:
	mov	r0, r5
	bl	kfree
.L2055:
	mov	r0, r4
	pop	{r4, r5, r6, pc}
.L2063:
	ldrh	r2, [r5, #6]
	add	r1, r5, #8
	ldrh	r0, [r5, #4]
	bl	rk_sftl_vendor_read
	adds	r3, r0, #1
	beq	.L2064
	mov	r3, sp
	uxth	r2, r0
	ands	r6, r6, r3
	strh	r0, [r5, #6]	@ movhi
	adds	r2, r2, #8
	ldr	r3, [r6, #72]
	.syntax unified
@ 573 "./arch/arm/include/asm/uaccess.h" 1
	adds r1, r4, r2; sbcccs r1, r1, r3; movcc r3, #0
@ 0 "" 2
	.thumb
	.syntax unified
	cmp	r3, #0
	bne	.L2078
	mov	r1, r5
	mov	r0, r4
	bl	arm_copy_to_user
	cmp	r0, #0
	bne	.L2078
.L2074:
	mov	r4, r0
	b	.L2057
.L2059:
	ldr	r6, .L2083+4
	mov	r3, sp
	ands	r3, r3, r6
	ldr	r3, [r3, #72]
	.syntax unified
@ 564 "./arch/arm/include/asm/uaccess.h" 1
	adds r2, r4, #8; sbcccs r2, r2, r3; movcc r3, #0
@ 0 "" 2
	.thumb
	.syntax unified
	cbnz	r3, .L2066
	movs	r2, #8
	mov	r1, r4
	bl	arm_copy_from_user
	cbz	r0, .L2080
.L2067:
	mov	r2, r4
	mov	r1, #276
	b	.L2082
.L2066:
	movs	r1, #8
	bl	__memzero
	b	.L2067
.L2080:
	ldr	r2, [r5]
	ldr	r3, .L2083+12
	cmp	r2, r3
	bne	.L2064
	ldrh	r2, [r5, #6]
	movw	r3, #4087
	cmp	r2, r3
	bhi	.L2064
	mov	r3, sp
	adds	r2, r2, #8
	ands	r6, r6, r3
	ldr	r3, [r6, #72]
	.syntax unified
@ 564 "./arch/arm/include/asm/uaccess.h" 1
	adds r1, r4, r2; sbcccs r1, r1, r3; movcc r3, #0
@ 0 "" 2
	.thumb
	.syntax unified
	cbnz	r3, .L2069
	mov	r1, r4
	mov	r0, r5
	bl	arm_copy_from_user
	cbz	r0, .L2081
.L2070:
	mov	r2, r4
	movw	r1, #283
	b	.L2082
.L2069:
	mov	r1, r2
	mov	r0, r5
	bl	__memzero
	b	.L2070
.L2081:
	ldrh	r2, [r5, #6]
	add	r1, r5, #8
	ldrh	r0, [r5, #4]
	bl	rk_sftl_vendor_write
	b	.L2074
.L2072:
	mov	r4, #-1
	b	.L2055
.L2084:
	.align	2
.L2083:
	.word	1074034177
	.word	-8192
	.word	.LC135
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
	ldr	r0, .L2086
	b	misc_register
.L2087:
	.align	2
.L2086:
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
	.align	2
	.set	.LANCHOR1,. + 0
	.type	__func__.6707, %object
	.size	__func__.6707, 17
__func__.6707:
	.ascii	"INSERT_DATA_LIST\000"
	.type	__func__.6702, %object
	.size	__func__.6702, 17
__func__.6702:
	.ascii	"INSERT_FREE_LIST\000"
	.type	__func__.6738, %object
	.size	__func__.6738, 17
__func__.6738:
	.ascii	"List_remove_node\000"
	.type	__func__.6770, %object
	.size	__func__.6770, 22
__func__.6770:
	.ascii	"List_update_data_list\000"
	.type	__func__.6877, %object
	.size	__func__.6877, 22
__func__.6877:
	.ascii	"select_l2p_ram_region\000"
	.type	__func__.7204, %object
	.size	__func__.7204, 16
__func__.7204:
	.ascii	"make_superblock\000"
	.type	__func__.7379, %object
	.size	__func__.7379, 19
__func__.7379:
	.ascii	"get_new_active_ppa\000"
	.type	__func__.17802, %object
	.size	__func__.17802, 17
__func__.17802:
	.ascii	"FlashEraseBlocks\000"
	.type	__func__.6812, %object
	.size	__func__.6812, 26
__func__.6812:
	.ascii	"ftl_map_blk_alloc_new_blk\000"
	.type	__func__.6317, %object
	.size	__func__.6317, 11
__func__.6317:
	.ascii	"FtlMemInit\000"
	.type	__func__.6544, %object
	.size	__func__.6544, 14
__func__.6544:
	.ascii	"FtlBbt2Bitmap\000"
	.type	__func__.7225, %object
	.size	__func__.7225, 18
__func__.7225:
	.ascii	"SupperBlkListInit\000"
	.type	__func__.17759, %object
	.size	__func__.17759, 15
__func__.17759:
	.ascii	"FlashReadPages\000"
	.type	__func__.6989, %object
	.size	__func__.6989, 14
__func__.6989:
	.ascii	"FtlScanSysBlk\000"
	.type	__func__.6587, %object
	.size	__func__.6587, 11
__func__.6587:
	.ascii	"FtlLoadBbt\000"
	.type	__func__.17778, %object
	.size	__func__.17778, 15
__func__.17778:
	.ascii	"FlashProgPages\000"
	.type	__func__.7351, %object
	.size	__func__.7351, 25
__func__.7351:
	.ascii	"allocate_data_superblock\000"
	.type	__func__.7392, %object
	.size	__func__.7392, 16
__func__.7392:
	.ascii	"update_vpc_list\000"
	.type	__func__.7399, %object
	.size	__func__.7399, 20
__func__.7399:
	.ascii	"decrement_vpc_count\000"
	.type	__func__.6838, %object
	.size	__func__.6838, 31
__func__.6838:
	.ascii	"Ftl_write_map_blk_to_last_page\000"
	.type	__func__.6852, %object
	.size	__func__.6852, 16
__func__.6852:
	.ascii	"FtlMapWritePage\000"
	.type	__func__.6779, %object
	.size	__func__.6779, 16
__func__.6779:
	.ascii	"load_l2p_region\000"
	.type	__func__.6823, %object
	.size	__func__.6823, 15
__func__.6823:
	.ascii	"ftl_map_blk_gc\000"
	.type	__func__.6894, %object
	.size	__func__.6894, 9
__func__.6894:
	.ascii	"log2phys\000"
	.type	__func__.7113, %object
	.size	__func__.7113, 16
__func__.7113:
	.ascii	"FtlReUsePrevPpa\000"
	.type	__func__.7147, %object
	.size	__func__.7147, 22
__func__.7147:
	.ascii	"FtlRecoverySuperblock\000"
	.type	__func__.7266, %object
	.size	__func__.7266, 14
__func__.7266:
	.ascii	"ftl_check_vpc\000"
	.type	__func__.6967, %object
	.size	__func__.6967, 15
__func__.6967:
	.ascii	"FtlVpcTblFlush\000"
	.type	__func__.7250, %object
	.size	__func__.7250, 21
__func__.7250:
	.ascii	"FtlVpcCheckAndModify\000"
	.type	__func__.7372, %object
	.size	__func__.7372, 29
__func__.7372:
	.ascii	"allocate_new_data_superblock\000"
	.type	__func__.6441, %object
	.size	__func__.6441, 13
__func__.6441:
	.ascii	"FtlProgPages\000"
	.type	__func__.7469, %object
	.size	__func__.7469, 19
__func__.7469:
	.ascii	"FtlGcFreeTempBlock\000"
	.type	__func__.7582, %object
	.size	__func__.7582, 23
__func__.7582:
	.ascii	"rk_ftl_garbage_collect\000"
	.type	__func__.6469, %object
	.size	__func__.6469, 9
__func__.6469:
	.ascii	"FtlWrite\000"
	.type	__func__.7045, %object
	.size	__func__.7045, 15
__func__.7045:
	.ascii	"FtlLoadSysInfo\000"
	.type	__func__.7067, %object
	.size	__func__.7067, 18
__func__.7067:
	.ascii	"FtlMapTblRecovery\000"
	.type	__func__.17926, %object
	.size	__func__.17926, 14
__func__.17926:
	.ascii	"FtlWriteToIDB\000"
	.space	1
	.type	rk_sftl_vendor_storage_fops, %object
	.size	rk_sftl_vendor_storage_fops, 108
rk_sftl_vendor_storage_fops:
	.space	32
	.word	rk_sftl_vendor_storage_ioctl
	.word	rk_sftl_vendor_storage_ioctl
	.space	68
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
	.word	.LC136
	.word	rk_sftl_vendor_storage_fops
	.space	28
	.bss
	.align	2
	.set	.LANCHOR0,. + 0
	.type	g_nand_phy_info, %object
	.size	g_nand_phy_info, 24
g_nand_phy_info:
	.space	24
	.type	p_blk_mode_table, %object
	.size	p_blk_mode_table, 4
p_blk_mode_table:
	.space	4
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
	.ascii	"SFTL version: 5.0.50 20190215\000"
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
	.ascii	"lba = %x,addr= %x,spare= %x %x %x %x data=%x %x\012"
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
	.ascii	"g_gc_superblock_free %x %x %x %x %x\012\000"
.LC126:
	.ascii	"lpa=%x, ppa=%x\012\000"
.LC127:
	.ascii	"FtlWrite: ecc error:%x %x %x\012\000"
.LC128:
	.ascii	"FtlWrite: lpa error:%x %x\012\000"
.LC129:
	.ascii	"%s hash error this.id =%x page =%x pre_id =%x hash "
	.ascii	"=%x hash_r =%x\012\000"
.LC130:
	.ascii	"%s last blk_id =%x page =%x hash error hash =%x has"
	.ascii	"h_r =%x\012\000"
.LC131:
	.ascii	"%s scan blk_id =%x page =%x hash error hash =%x has"
	.ascii	"h_r =%x\012\000"
.LC132:
	.ascii	"write_idblock fail! %x %x %x %x\012\000"
.LC133:
	.ascii	"%s idb buffer alloc fail\012\000"
.LC134:
	.ascii	"%p %x %p %x\012\000"
.LC135:
	.ascii	"copy_from_user error %d %p %p\012\000"
.LC136:
	.ascii	"vendor_storage\000"
