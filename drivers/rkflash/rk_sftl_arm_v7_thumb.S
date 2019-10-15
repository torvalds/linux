/* SPDX-License-Identifier: GPL-2.0 */

/* Copyright (c) 2018 Rockchip Electronics Co. Ltd.
 * date: 2019-10-15
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
	ldrh	r1, [fp, #14]
	ldrh	r2, [r0]
	ldr	r4, .L29
	cmp	r1, #4
	ldrh	r6, [r0, #2]
	ldrh	r3, [fp, #6]
	ldrh	r0, [r0, #4]
	strh	r2, [r4, #256]	@ movhi
	strh	r6, [r4, #258]	@ movhi
	strh	r0, [r4, #260]	@ movhi
	strh	r3, [r4, #246]	@ movhi
	strh	r1, [r4, #262]	@ movhi
	bne	.L19
	lsrs	r3, r3, #1
	strh	r3, [r4, #246]	@ movhi
	movs	r3, #8
	strh	r3, [r4, #262]	@ movhi
.L20:
	ldr	r1, .L29+4
	movs	r3, #0
.L21:
	strb	r3, [r3, r1]
	adds	r3, r3, #1
	cmp	r3, #32
	bne	.L21
	ldrh	r5, [r4, #246]
	movs	r3, #5
	ldrh	r0, [r4, #260]
	cmp	r2, #1
	strh	r3, [r4, #296]	@ movhi
	mov	r7, #640
	mov	r3, #0
	it	eq
	strheq	r2, [r4, #296]	@ movhi
	strh	r3, [r4, #298]	@ movhi
	smulbb	r6, r6, r0
	strh	r7, [r4, #300]	@ movhi
	smulbb	r0, r0, r5
	uxth	r6, r6
	uxth	r0, r0
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
.L19:
	cmp	r1, #8
	bne	.L20
	cmp	r0, #1
	itttt	eq
	lsreq	r3, r3, #1
	strheq	r3, [r4, #246]	@ movhi
	moveq	r3, #2
	strheq	r3, [r4, #260]	@ movhi
	b	.L20
.L30:
	.align	2
.L29:
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
	ldr	r2, .L38
	ldrh	r3, [r2, #348]
	cbz	r3, .L37
	ldr	r3, [r2, #352]
	ldrh	r2, [r2, #324]
	add	r2, r3, r2, lsl #1
.L33:
	cmp	r3, r2
	bne	.L34
.L37:
	movs	r0, #0
	bx	lr
.L34:
	ldrh	r1, [r3], #2
	cmp	r0, r1
	bne	.L33
	movs	r0, #1
	bx	lr
.L39:
	.align	2
.L38:
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
	ldr	r3, .L42
	ldr	r0, [r3, #344]
	bx	lr
.L43:
	.align	2
.L42:
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
	ldr	r4, .L45
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
	ldr	r0, .L45+4
	bl	sftl_printk
	ldrh	r3, [r4, #362]
	movs	r0, #0
	adds	r3, r3, #1
	strh	r3, [r4, #362]	@ movhi
	add	sp, sp, #12
	@ sp needed
	pop	{r4, r5, r6, r7, pc}
.L46:
	.align	2
.L45:
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
	ldr	r5, .L48
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
.L49:
	.align	2
.L48:
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
	ldr	r3, .L58
	push	{r4, r5, r6, lr}
	.save {r4, r5, r6, lr}
	movs	r5, #0
	mov	r4, r5
	ldrh	r2, [r3, #302]
	ldrh	r6, [r3, #258]
	muls	r6, r2, r6
.L52:
	uxth	r0, r5
	cmp	r0, r6
	blt	.L54
	mov	r0, r4
	pop	{r4, r5, r6, pc}
.L54:
	bl	FtlBbmIsBadBlock
	cbz	r0, .L53
	adds	r4, r4, #1
	uxth	r4, r4
.L53:
	adds	r5, r5, #1
	b	.L52
.L59:
	.align	2
.L58:
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
	ldr	r4, .L61
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
.L62:
	.align	2
.L61:
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
	ldr	r3, .L64
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
.L65:
	.align	2
.L64:
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
	ldr	r4, .L67
	ldrh	r1, [r4, #302]
	bl	__aeabi_uidivmod
	uxth	r0, r1
	ldrh	r1, [r4, #260]
	bl	__aeabi_uidiv
	uxth	r0, r0
	pop	{r4, pc}
.L68:
	.align	2
.L67:
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
	bls	.L70
	subs	r0, r0, r1
	cmp	r0, #-2147483648
	ite	hi
	movhi	r0, #0
	movls	r0, #1
	bx	lr
.L70:
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
	ldr	r3, .L73
	ldrh	r0, [r3, #422]
	clz	r0, r0
	lsrs	r0, r0, #5
	bx	lr
.L74:
	.align	2
.L73:
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
	ldr	r3, .L76
	ldrh	r0, [r3, #422]
	sub	r3, r0, #1024
	rsbs	r0, r3, #0
	adcs	r0, r0, r3
	bx	lr
.L77:
	.align	2
.L76:
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
	ldr	r3, .L87
	push	{r4, r5, r6, lr}
	.save {r4, r5, r6, lr}
	ldrh	r2, [r3, #422]
	cbz	r2, .L78
	ldrh	r5, [r3, #2500]
	movs	r0, #0
	ldrh	r1, [r3, #418]
	mov	r6, r0
	ldrh	r2, [r3, #420]
	and	r5, r5, #31
.L80:
	uxth	r4, r0
	adds	r0, r0, #1
	cmp	r5, r4
	bgt	.L81
	cbz	r6, .L78
	strh	r1, [r3, #418]	@ movhi
	strh	r2, [r3, #420]	@ movhi
.L78:
	pop	{r4, r5, r6, pc}
.L81:
	add	r4, r3, r1, lsl #1
	adds	r1, r1, #1
	ubfx	r1, r1, #0, #10
	ldrh	r6, [r4, #424]
	add	r4, r3, r2, lsl #1
	strh	r6, [r4, #424]	@ movhi
	movs	r6, #1
	add	r2, r2, r6
	ubfx	r2, r2, #0, #10
	b	.L80
.L88:
	.align	2
.L87:
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
	ldr	r3, .L95
	push	{r4, r5, lr}
	.save {r4, r5, lr}
	ldrh	r4, [r3, #422]
	cmp	r4, #1024
	beq	.L93
	ldrh	r5, [r3, #418]
	movs	r1, #0
.L91:
	cmp	r1, r4
	bcc	.L92
.L93:
	movs	r0, #0
	pop	{r4, r5, pc}
.L92:
	adds	r2, r1, r5
	ubfx	r2, r2, #0, #10
	add	r2, r3, r2, lsl #1
	ldrh	r2, [r2, #424]
	cmp	r2, r0
	beq	.L94
	adds	r1, r1, #1
	b	.L91
.L94:
	movs	r0, #1
	pop	{r4, r5, pc}
.L96:
	.align	2
.L95:
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
	ldr	r2, .L112
	ldrh	r3, [r2, #244]
	mov	r1, r2
	cmp	r3, r0
	bls	.L99
	movs	r5, #6
	ldr	lr, [r2, #2520]
	muls	r5, r0, r5
	movw	r3, #65535
	add	r4, lr, r5
	strh	r3, [r4, #2]	@ movhi
	strh	r3, [lr, r5]	@ movhi
	ldr	r3, [r2, #2524]
	cbnz	r3, .L100
	str	r4, [r2, #2524]
.L99:
	movs	r0, #0
	add	sp, sp, #12
	@ sp needed
	pop	{r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L100:
	ldrh	r7, [r4, #4]
	ldr	r9, [r2, #76]
	ldrh	r2, [r9, r0, lsl #1]
	cbz	r7, .L107
	muls	r2, r7, r2
.L111:
	ldr	r8, [r1, #2520]
	movw	r10, #65535
	str	r2, [sp]
	sub	r2, r3, r8
	asrs	r6, r2, #1
	ldr	r2, .L112+4
	muls	r2, r6, r2
	ldrh	r6, [r1, #244]
	str	r6, [sp, #4]
	movs	r6, #0
	uxth	r2, r2
.L105:
	adds	r6, r6, #1
	ldr	r7, [sp, #4]
	uxth	r6, r6
	cmp	r6, r7
	bhi	.L99
	cmp	r0, r2
	beq	.L99
	ldrh	ip, [r3, #4]
	cmp	ip, #0
	beq	.L103
	ldrh	fp, [r9, r2, lsl #1]
	ldr	r7, [sp]
	mul	ip, ip, fp
	cmp	ip, r7
	bcs	.L103
	ldrh	ip, [r3]
	cmp	ip, r10
	bne	.L104
	strh	r2, [r4, #2]	@ movhi
	strh	r0, [r3]	@ movhi
	str	r4, [r1, #2528]
	b	.L99
.L107:
	mov	r2, #-1
	b	.L111
.L104:
	movs	r3, #6
	mov	r2, ip
	mla	r3, r3, ip, r8
	b	.L105
.L103:
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
	b	.L99
.L113:
	.align	2
.L112:
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
	ldr	r2, .L116
	ldrh	r3, [r2, #2532]
	adds	r3, r3, #1
	uxth	r3, r3
	strh	r3, [r2, #2532]	@ movhi
	ldrh	r2, [r2, #244]
	cmp	r2, r3
	bcs	.L114
	movs	r2, #214
	ldr	r1, .L116+4
	ldr	r0, .L116+8
	pop	{r3, lr}
	b	sftl_printk
.L114:
	pop	{r3, pc}
.L117:
	.align	2
.L116:
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
	beq	.L119
	ldr	r2, .L125
	movs	r1, #6
	mul	r7, r1, r0
	ldr	ip, [r2, #2520]
	mov	r5, r2
	add	r6, ip, r7
	strh	r4, [r6, #2]	@ movhi
	strh	r4, [ip, r7]	@ movhi
	ldr	r3, [r2, #2536]
	cbnz	r3, .L120
	str	r6, [r2, #2536]
.L119:
	movs	r0, #0
	pop	{r4, r5, r6, r7, r8, r9, r10, pc}
.L120:
	ldr	lr, [r2, #2520]
	ldr	r8, [r2, #2540]
	sub	r2, r3, lr
	asr	r10, r2, #1
	ldr	r2, .L125+4
	ldrh	r9, [r8, r0, lsl #1]
	mul	r2, r2, r10
	mov	r10, r4
	uxth	r2, r2
.L123:
	ldrh	r4, [r8, r2, lsl #1]
	cmp	r4, r9
	bcs	.L121
	ldrh	r4, [r3]
	cmp	r4, r10
	bne	.L122
	strh	r2, [r6, #2]	@ movhi
	strh	r0, [r3]	@ movhi
	b	.L119
.L122:
	mla	r3, r1, r4, lr
	mov	r2, r4
	b	.L123
.L121:
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
	b	.L119
.L126:
	.align	2
.L125:
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
	ldr	r2, .L129
	ldrh	r3, [r2, #228]
	adds	r3, r3, #1
	uxth	r3, r3
	strh	r3, [r2, #228]	@ movhi
	ldrh	r2, [r2, #244]
	cmp	r2, r3
	bcs	.L127
	movs	r2, #207
	ldr	r1, .L129+4
	ldr	r0, .L129+8
	pop	{r3, lr}
	b	sftl_printk
.L127:
	pop	{r3, pc}
.L130:
	.align	2
.L129:
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
	ldr	r4, .L137
	muls	r6, r1, r6
	movw	r3, #65535
	mov	r8, r0
	ldr	r7, [r4, #2520]
	adds	r5, r7, r6
	ldrh	r2, [r5, #2]
	cmp	r2, r3
	bne	.L132
	ldr	r3, [r0]
	cmp	r5, r3
	beq	.L132
	mov	r2, #372
	ldr	r1, .L137+4
	ldr	r0, .L137+8
	bl	sftl_printk
.L132:
	ldr	r3, [r8]
	movw	r1, #65535
	cmp	r5, r3
	ldrh	r3, [r7, r6]
	bne	.L133
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
.L135:
	movw	r3, #65535
	movs	r0, #0
	strh	r3, [r7, r6]	@ movhi
	strh	r3, [r5, #2]	@ movhi
	pop	{r4, r5, r6, r7, r8, pc}
.L133:
	cmp	r3, r1
	ldrh	r1, [r5, #2]
	bne	.L136
	cmp	r1, r3
	beq	.L135
	movs	r2, #6
	ldr	r0, [r4, #2520]
	muls	r1, r2, r1
	strh	r3, [r0, r1]	@ movhi
	b	.L135
.L136:
	ldr	r0, [r4, #2520]
	movs	r2, #6
	mla	r3, r2, r3, r0
	strh	r1, [r3, #2]	@ movhi
	ldrh	r0, [r5, #2]
	ldrh	r1, [r7, r6]
	ldr	r3, [r4, #2520]
	muls	r2, r0, r2
	strh	r1, [r3, r2]	@ movhi
	b	.L135
.L138:
	.align	2
.L137:
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
	cbz	r3, .L145
	ldr	r2, .L146
	movw	r5, #65535
	movs	r6, #6
	ldr	r2, [r2, #2520]
.L141:
	cbnz	r1, .L142
.L144:
	ldr	r4, .L146+4
	subs	r3, r3, r2
	asrs	r3, r3, #1
	muls	r4, r3, r4
	uxth	r1, r4
	bl	List_remove_node
	uxth	r0, r4
	pop	{r4, r5, r6, pc}
.L142:
	ldrh	r4, [r3]
	cmp	r4, r5
	beq	.L144
	subs	r1, r1, #1
	mla	r3, r6, r4, r2
	uxth	r1, r1
	b	.L141
.L145:
	movw	r0, #65535
	pop	{r4, r5, r6, pc}
.L147:
	.align	2
.L146:
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
	ldr	r2, .L155
	push	{r4, lr}
	.save {r4, lr}
	ldr	r3, [r2, #2524]
	cbz	r3, .L154
	ldr	r1, [r2, #2520]
	movs	r4, #6
	movw	r2, #65535
.L151:
	cbz	r0, .L152
	ldrh	r3, [r3]
	cmp	r3, r2
	bne	.L153
.L154:
	movw	r0, #65535
	pop	{r4, pc}
.L153:
	subs	r0, r0, #1
	mla	r3, r4, r3, r1
	uxth	r0, r0
	b	.L151
.L152:
	ldr	r0, .L155+4
	subs	r3, r3, r1
	asrs	r3, r3, #1
	muls	r3, r0, r3
	uxth	r0, r3
	pop	{r4, pc}
.L156:
	.align	2
.L155:
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
	ldr	r4, .L168
	ldrh	r3, [r4, #28]
	cmp	r3, r0
	beq	.L159
	ldrh	r3, [r4, #80]
	cmp	r3, r0
	beq	.L159
	ldrh	r3, [r4, #128]
	cmp	r3, r0
	beq	.L159
	movs	r7, #6
	ldr	r9, [r4, #2520]
	muls	r7, r0, r7
	ldr	r3, [r4, #2524]
	add	r8, r9, r7
	cmp	r8, r3
	beq	.L159
	ldrh	r6, [r8, #4]
	ldr	r3, [r4, #76]
	ldrh	r3, [r3, r0, lsl #1]
	cmp	r6, #0
	beq	.L166
	muls	r6, r3, r6
.L161:
	ldrh	r3, [r8, #2]
	movw	r2, #65535
	cmp	r3, r2
	bne	.L162
	ldrh	r2, [r9, r7]
	cmp	r2, r3
	bne	.L162
	movw	r2, #463
	ldr	r1, .L168+4
	ldr	r0, .L168+8
	bl	sftl_printk
.L162:
	ldrh	r3, [r8, #2]
	movw	r2, #65535
	cmp	r3, r2
	bne	.L163
	ldrh	r2, [r9, r7]
	cmp	r2, r3
	beq	.L159
.L163:
	movs	r2, #6
	muls	r2, r3, r2
	ldr	r3, .L168+12
	asrs	r1, r2, #1
	muls	r3, r1, r3
	ldr	r1, [r4, #76]
	ldrh	r0, [r1, r3, lsl #1]
	ldr	r1, [r4, #2520]
	add	r2, r2, r1
	ldrh	r3, [r2, #4]
	cbz	r3, .L167
	muls	r3, r0, r3
.L164:
	cmp	r6, r3
	bcs	.L159
	mov	r1, r5
	ldr	r0, .L168+16
	bl	List_remove_node
	ldrh	r3, [r4, #2532]
	cbnz	r3, .L165
	mov	r2, #474
	ldr	r1, .L168+4
	ldr	r0, .L168+8
	bl	sftl_printk
.L165:
	ldrh	r3, [r4, #2532]
	mov	r0, r5
	subs	r3, r3, #1
	strh	r3, [r4, #2532]	@ movhi
	bl	INSERT_DATA_LIST
.L159:
	movs	r0, #0
	pop	{r3, r4, r5, r6, r7, r8, r9, pc}
.L166:
	mov	r6, #-1
	b	.L161
.L167:
	mov	r3, #-1
	b	.L164
.L169:
	.align	2
.L168:
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
	ldr	r5, .L180
	movs	r0, #12
	movw	r6, #65535
	ldrh	r2, [r5, #342]
	ldr	r3, [r5, #2544]
.L171:
	uxth	r4, r1
	cmp	r4, r2
	bcc	.L173
	mov	r4, r2
	movs	r1, #0
	mov	r7, #-2147483648
	mov	ip, #12
.L174:
	uxth	r6, r1
	cmp	r6, r2
	bcc	.L176
	cmp	r4, r2
	bcc	.L172
	ldrh	r7, [r5, #2548]
	mov	r4, r2
	movs	r1, #0
	mov	r0, #-1
.L177:
	uxth	r5, r1
	cmp	r5, r2
	bcc	.L179
	cmp	r4, r2
	bcc	.L172
	movw	r2, #789
	ldr	r1, .L180+4
	ldr	r0, .L180+8
	bl	sftl_printk
	b	.L172
.L173:
	adds	r1, r1, #1
	mla	r7, r0, r1, r3
	ldrh	r7, [r7, #-12]
	cmp	r7, r6
	bne	.L171
.L172:
	mov	r0, r4
	pop	{r3, r4, r5, r6, r7, pc}
.L176:
	mla	r0, ip, r1, r3
	ldr	r0, [r0, #4]
	cmp	r0, #0
	blt	.L175
	cmp	r7, r0
	itt	hi
	movhi	r7, r0
	movhi	r4, r6
.L175:
	adds	r1, r1, #1
	b	.L174
.L179:
	ldr	r6, [r3, #4]
	cmp	r0, r6
	bls	.L178
	ldrh	ip, [r3]
	cmp	ip, r7
	itt	ne
	movne	r0, r6
	movne	r4, r5
.L178:
	adds	r1, r1, #1
	adds	r3, r3, #12
	b	.L177
.L181:
	.align	2
.L180:
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
	ldr	r1, .L188
	push	{r4, r5, lr}
	.save {r4, r5, lr}
	mov	r3, r1
	ldrh	r2, [r1, #2550]
	cmp	r2, #4
	bhi	.L183
	cbnz	r0, .L183
	adds	r2, r2, #1
	strh	r2, [r1, #2550]	@ movhi
	pop	{r4, r5, pc}
.L183:
	movs	r2, #0
	ldrh	r1, [r3, #244]
	strh	r2, [r3, #2550]	@ movhi
	movw	r5, #65535
	str	r2, [r3, #2552]
	ldr	r2, [r3, #76]
	add	r1, r2, r1, lsl #1
.L184:
	cmp	r2, r1
	bne	.L186
	pop	{r4, r5, pc}
.L186:
	ldrh	r4, [r2], #2
	cmp	r4, r5
	ittt	ne
	ldrne	r0, [r3, #2552]
	addne	r0, r0, r4
	strne	r0, [r3, #2552]
	b	.L184
.L189:
	.align	2
.L188:
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
	cbz	r1, .L191
	b	ftl_set_blk_mode.part.6
.L191:
	ldr	r2, .L192
	lsrs	r0, r0, #5
	and	r3, r3, #31
	ldr	r1, [r2, #24]
	movs	r2, #1
	lsl	r3, r2, r3
	ldr	r2, [r1, r0, lsl #2]
	bic	r2, r2, r3
	str	r2, [r1, r0, lsl #2]
	bx	lr
.L193:
	.align	2
.L192:
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
	ldr	r3, .L195
	lsrs	r2, r0, #5
	and	r0, r0, #31
	ldr	r3, [r3, #24]
	ldr	r3, [r3, r2, lsl #2]
	lsr	r0, r3, r0
	and	r0, r0, #1
	bx	lr
.L196:
	.align	2
.L195:
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
	ldr	r3, .L204
	adds	r4, r4, #14
	ldrh	r5, [r3, #236]
.L198:
	cmp	r2, r5
	bcc	.L200
	ldrh	r3, [r3, #306]
	add	r4, r0, #16
	movw	r6, #65535
	subs	r3, r3, #1
	subs	r1, r3, r1
	movs	r3, #0
	uxth	r1, r1
.L201:
	uxth	r2, r3
	cmp	r5, r2
	bhi	.L203
	pop	{r4, r5, r6, r7, pc}
.L200:
	ldrh	r6, [r4, #2]!
	adds	r2, r2, #1
	uxth	r2, r2
	cmp	r6, r7
	ittt	ne
	ldrhne	r6, [r0, #4]
	addne	r6, r6, #1
	strhne	r6, [r0, #4]	@ movhi
	b	.L198
.L203:
	ldrh	r2, [r4], #2
	adds	r3, r3, #1
	cmp	r2, r6
	ittt	ne
	ldrhne	r2, [r0, #4]
	addne	r2, r2, r1
	strhne	r2, [r0, #4]	@ movhi
	b	.L201
.L205:
	.align	2
.L204:
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
	cbz	r3, .L206
	ldrh	r2, [r0]
	movw	r3, #65535
	cmp	r2, r3
	beq	.L206
	ldrb	r2, [r0, #6]	@ zero_extendqisi2
	movs	r5, #0
	adds	r2, r2, #8
	ldrh	r1, [r0, r2, lsl #1]
	ldr	r2, .L212
	ldrh	r4, [r2, #236]
	mov	r2, r3
.L209:
	cmp	r1, r2
	beq	.L211
.L206:
	pop	{r4, r5, pc}
.L211:
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
	b	.L209
.L213:
	.align	2
.L212:
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
	ldr	r6, .L220
	ldrh	r2, [r0]
	ldrh	r3, [r6, #244]
	cmp	r2, r3
	bcc	.L215
	movw	r2, #2148
	ldr	r1, .L220+4
	ldr	r0, .L220+8
	bl	sftl_printk
.L215:
	ldrh	r9, [r6, #236]
	add	r7, r4, #16
	ldr	r10, .L220+12
	movw	r8, #65535
	movs	r5, #0
	strh	r5, [r4, #4]	@ movhi
	strb	r5, [r4, #7]
.L216:
	uxth	r3, r5
	ldrh	r1, [r4]
	cmp	r9, r3
	bhi	.L218
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
.L218:
	ldrb	r0, [r10, r5]	@ zero_extendqisi2
	bl	V2P_block
	strh	r8, [r7]	@ movhi
	mov	fp, r0
	bl	FtlBbmIsBadBlock
	cbnz	r0, .L217
	strh	fp, [r7]	@ movhi
	ldrb	r3, [r4, #7]	@ zero_extendqisi2
	adds	r3, r3, #1
	strb	r3, [r4, #7]
.L217:
	adds	r5, r5, #1
	adds	r7, r7, #2
	b	.L216
.L221:
	.align	2
.L220:
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
	ldr	r6, .L228
	mov	r7, r0
	mov	r4, r5
	ldrh	r8, [r6, #236]
	add	r10, r6, #264
	ldrh	r9, [r6, #306]
.L223:
	uxth	r3, r5
	cmp	r8, r3
	bhi	.L225
	cbz	r4, .L227
	mov	r1, r4
	mov	r0, #32768
	bl	__aeabi_idiv
.L226:
	ldr	r1, [r6, #2520]
	movs	r3, #6
	mla	r1, r3, r7, r1
	strh	r0, [r1, #4]	@ movhi
	movs	r0, #0
	pop	{r4, r5, r6, r7, r8, r9, r10, pc}
.L225:
	mov	r1, r7
	ldrb	r0, [r10, r5]	@ zero_extendqisi2
	bl	V2P_block
	bl	FtlBbmIsBadBlock
	cbnz	r0, .L224
	add	r4, r4, r9
	uxth	r4, r4
.L224:
	adds	r5, r5, #1
	b	.L223
.L227:
	mov	r0, r4
	b	.L226
.L229:
	.align	2
.L228:
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
	ldr	r2, .L233
	ldr	r0, [r2, #2536]
	cbz	r0, .L231
	ldr	r3, [r2, #2520]
	subs	r0, r0, r3
	ldr	r3, .L233+4
	asrs	r0, r0, #1
	muls	r0, r3, r0
	ldr	r3, [r2, #2540]
	uxth	r0, r0
	ldrh	r0, [r3, r0, lsl #1]
.L231:
	bx	lr
.L234:
	.align	2
.L233:
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
	ldr	r1, .L243
	push	{r4, r5, r6, r7, lr}
	.save {r4, r5, r6, r7, lr}
	ldr	r3, [r1, #2536]
	cbz	r3, .L241
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
	ldr	r2, .L243+4
	muls	r3, r2, r3
	movs	r2, #0
	uxth	r3, r3
.L238:
	uxth	r5, r2
	cmp	r0, r5
	bls	.L240
	mul	r5, r6, r3
	adds	r2, r2, #1
	ldrh	r5, [r4, r5]
	cmp	r5, r7
	bne	.L242
.L240:
	ldr	r2, [r1, #2540]
	ldrh	r0, [r2, r3, lsl #1]
	pop	{r4, r5, r6, r7, pc}
.L242:
	mov	r3, r5
	b	.L238
.L241:
	mov	r0, r3
	pop	{r4, r5, r6, r7, pc}
.L244:
	.align	2
.L243:
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
	ldr	r5, .L257
	add	r4, r6, #12
	.pad #20
	sub	sp, sp, #20
	ldr	r1, .L257+4
	bl	strcpy
	ldr	r3, [r5, #320]
	movs	r1, #64
	ldr	r2, .L257+8
	mov	r0, r4
	bl	snprintf
	add	r4, r4, r0
	ldr	r1, .L257+12
	mov	r0, r4
	adds	r4, r4, #10
	bl	strcpy
	ldr	r3, [r5, #2556]
	movs	r1, #64
	ldr	r2, .L257+16
	mov	r0, r4
	bl	snprintf
	add	r4, r4, r0
	ldr	r3, [r5, #2552]
	ldr	r2, .L257+20
	movs	r1, #64
	mov	r0, r4
	bl	snprintf
	add	r4, r4, r0
	ldr	r3, [r5, #2560]
	ldr	r2, .L257+24
	movs	r1, #64
	mov	r0, r4
	bl	snprintf
	add	r4, r4, r0
	ldr	r3, [r5, #2564]
	ldr	r2, .L257+28
	movs	r1, #64
	mov	r0, r4
	bl	snprintf
	add	r4, r4, r0
	ldr	r3, [r5, #2568]
	ldr	r2, .L257+32
	movs	r1, #64
	mov	r0, r4
	bl	snprintf
	add	r4, r4, r0
	ldr	r3, [r5, #2572]
	ldr	r2, .L257+36
	movs	r1, #64
	mov	r0, r4
	bl	snprintf
	add	r4, r4, r0
	ldr	r3, [r5, #2576]
	ldr	r2, .L257+40
	movs	r1, #64
	mov	r0, r4
	bl	snprintf
	add	r4, r4, r0
	ldr	r3, [r5, #2580]
	ldr	r2, .L257+44
	movs	r1, #64
	mov	r0, r4
	bl	snprintf
	ldr	r3, [r5, #2584]
	add	r4, r4, r0
	ldr	r2, .L257+48
	movs	r1, #64
	mov	r0, r4
	lsrs	r3, r3, #11
	bl	snprintf
	ldr	r3, [r5, #2588]
	add	r4, r4, r0
	ldr	r2, .L257+52
	movs	r1, #64
	mov	r0, r4
	lsrs	r3, r3, #11
	bl	snprintf
	add	r4, r4, r0
	ldr	r3, [r5, #2592]
	ldr	r2, .L257+56
	movs	r1, #64
	mov	r0, r4
	bl	snprintf
	add	r4, r4, r0
	ldr	r3, [r5, #2596]
	ldr	r2, .L257+60
	movs	r1, #64
	mov	r0, r4
	bl	snprintf
	add	r4, r4, r0
	ldrh	r3, [r5, #362]
	ldr	r2, .L257+64
	movs	r1, #64
	mov	r0, r4
	bl	snprintf
	add	r4, r4, r0
	ldrh	r3, [r5, #228]
	ldr	r2, .L257+68
	movs	r1, #64
	mov	r0, r4
	bl	snprintf
	add	r4, r4, r0
	ldr	r3, [r5, #2600]
	ldr	r2, .L257+72
	movs	r1, #64
	mov	r0, r4
	bl	snprintf
	add	r4, r4, r0
	ldr	r3, [r5, #2604]
	ldr	r2, .L257+76
	movs	r1, #64
	mov	r0, r4
	bl	snprintf
	add	r4, r4, r0
	ldr	r3, [r5, #2608]
	ldr	r2, .L257+80
	movs	r1, #64
	mov	r0, r4
	bl	snprintf
	add	r4, r4, r0
	ldr	r3, [r5, #2612]
	ldr	r2, .L257+84
	movs	r1, #64
	mov	r0, r4
	bl	snprintf
	add	r4, r4, r0
	ldr	r3, [r5, #2616]
	ldr	r2, .L257+88
	movs	r1, #64
	mov	r0, r4
	bl	snprintf
	add	r4, r4, r0
	ldr	r3, [r5, #2620]
	ldr	r2, .L257+92
	movs	r1, #64
	mov	r0, r4
	bl	snprintf
	add	r4, r4, r0
	ldrh	r3, [r5, #2502]
	ldr	r2, .L257+96
	movs	r1, #64
	mov	r0, r4
	bl	snprintf
	add	r4, r4, r0
	ldrh	r3, [r5, #2500]
	ldr	r2, .L257+100
	movs	r1, #64
	mov	r0, r4
	bl	snprintf
	add	r4, r4, r0
	ldr	r3, [r5, #344]
	ldr	r2, .L257+104
	movs	r1, #64
	mov	r0, r4
	bl	snprintf
	add	r4, r4, r0
	ldr	r3, [r5, #336]
	ldr	r2, .L257+108
	movs	r1, #64
	mov	r0, r4
	bl	snprintf
	add	r4, r4, r0
	ldr	r3, [r5, #232]
	ldr	r2, .L257+112
	movs	r1, #64
	mov	r0, r4
	bl	snprintf
	add	r4, r4, r0
	ldrh	r3, [r5, #422]
	ldr	r2, .L257+116
	movs	r1, #64
	mov	r0, r4
	bl	snprintf
	add	r4, r4, r0
	ldrh	r3, [r5, #244]
	ldr	r2, .L257+120
	movs	r1, #64
	mov	r0, r4
	bl	snprintf
	add	r4, r4, r0
	ldrh	r3, [r5, #2624]
	ldr	r2, .L257+124
	movs	r1, #64
	mov	r0, r4
	bl	snprintf
	add	r4, r4, r0
	ldr	r3, [r5, #248]
	ldr	r2, .L257+128
	movs	r1, #64
	mov	r0, r4
	bl	snprintf
	add	r4, r4, r0
	ldrh	r3, [r5, #2628]
	ldr	r2, .L257+132
	movs	r1, #64
	mov	r0, r4
	bl	snprintf
	add	r4, r4, r0
	ldrh	r3, [r5, #356]
	ldr	r2, .L257+136
	movs	r1, #64
	mov	r0, r4
	bl	snprintf
	add	r4, r4, r0
	ldrh	r3, [r5, #30]
	ldr	r2, .L257+140
	movs	r1, #64
	mov	r0, r4
	bl	snprintf
	add	r4, r4, r0
	ldrb	r3, [r5, #34]	@ zero_extendqisi2
	ldr	r2, .L257+144
	movs	r1, #64
	mov	r0, r4
	bl	snprintf
	add	r4, r4, r0
	ldrh	r3, [r5, #28]
	ldr	r2, .L257+148
	movs	r1, #64
	mov	r0, r4
	bl	snprintf
	add	r4, r4, r0
	ldrb	r3, [r5, #36]	@ zero_extendqisi2
	ldr	r2, .L257+152
	movs	r1, #64
	mov	r0, r4
	bl	snprintf
	add	r4, r4, r0
	ldrh	r3, [r5, #32]
	ldr	r2, .L257+156
	movs	r1, #64
	mov	r0, r4
	bl	snprintf
	ldrh	r2, [r5, #28]
	add	r4, r4, r0
	ldr	r3, [r5, #76]
	movs	r1, #64
	mov	r0, r4
	ldrh	r3, [r3, r2, lsl #1]
	ldr	r2, .L257+160
	bl	snprintf
	add	r4, r4, r0
	ldrh	r3, [r5, #82]
	ldr	r2, .L257+164
	movs	r1, #64
	mov	r0, r4
	bl	snprintf
	add	r4, r4, r0
	ldrb	r3, [r5, #86]	@ zero_extendqisi2
	ldr	r2, .L257+168
	movs	r1, #64
	mov	r0, r4
	bl	snprintf
	add	r4, r4, r0
	ldrh	r3, [r5, #80]
	ldr	r2, .L257+172
	movs	r1, #64
	mov	r0, r4
	bl	snprintf
	add	r4, r4, r0
	ldrb	r3, [r5, #88]	@ zero_extendqisi2
	ldr	r2, .L257+176
	movs	r1, #64
	mov	r0, r4
	bl	snprintf
	add	r4, r4, r0
	ldrh	r3, [r5, #84]
	ldr	r2, .L257+180
	movs	r1, #64
	mov	r0, r4
	bl	snprintf
	ldrh	r2, [r5, #80]
	add	r4, r4, r0
	ldr	r3, [r5, #76]
	movs	r1, #64
	mov	r0, r4
	ldrh	r3, [r3, r2, lsl #1]
	ldr	r2, .L257+184
	bl	snprintf
	add	r4, r4, r0
	ldrh	r3, [r5, #130]
	ldr	r2, .L257+188
	movs	r1, #64
	mov	r0, r4
	bl	snprintf
	add	r4, r4, r0
	ldrb	r3, [r5, #134]	@ zero_extendqisi2
	ldr	r2, .L257+192
	movs	r1, #64
	mov	r0, r4
	bl	snprintf
	add	r4, r4, r0
	ldrh	r3, [r5, #128]
	ldr	r2, .L257+196
	movs	r1, #64
	mov	r0, r4
	bl	snprintf
	add	r4, r4, r0
	ldrb	r3, [r5, #136]	@ zero_extendqisi2
	ldr	r2, .L257+200
	movs	r1, #64
	mov	r0, r4
	bl	snprintf
	add	r4, r4, r0
	ldrh	r3, [r5, #132]
	ldr	r2, .L257+204
	movs	r1, #64
	mov	r0, r4
	bl	snprintf
	add	r4, r4, r0
	ldrh	r3, [r5, #178]
	ldr	r2, .L257+208
	movs	r1, #64
	mov	r0, r4
	bl	snprintf
	add	r4, r4, r0
	ldrb	r3, [r5, #182]	@ zero_extendqisi2
	ldr	r2, .L257+212
	movs	r1, #64
	mov	r0, r4
	bl	snprintf
	add	r4, r4, r0
	ldrh	r3, [r5, #176]
	ldr	r2, .L257+216
	movs	r1, #64
	mov	r0, r4
	bl	snprintf
	add	r4, r4, r0
	ldrb	r3, [r5, #184]	@ zero_extendqisi2
	ldr	r2, .L257+220
	movs	r1, #64
	mov	r0, r4
	bl	snprintf
	add	r4, r4, r0
	ldrh	r3, [r5, #180]
	b	.L258
.L259:
	.align	2
.L257:
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
.L258:
	ldr	r2, .L260
	movs	r1, #64
	mov	r0, r4
	bl	snprintf
	ldr	r3, [r5, #2720]
	add	r4, r4, r0
	ldr	r2, .L260+4
	movs	r1, #64
	mov	r0, r4
	str	r3, [sp, #4]
	ldr	r3, [r5, #2728]
	str	r3, [sp]
	ldr	r3, [r5, #2724]
	bl	snprintf
	add	r4, r4, r0
	ldr	r3, [r5, #2716]
	ldr	r2, .L260+8
	movs	r1, #64
	mov	r0, r4
	bl	snprintf
	add	r4, r4, r0
	ldr	r3, [r5, #2740]
	ldr	r2, .L260+12
	movs	r1, #64
	mov	r0, r4
	bl	snprintf
	add	r4, r4, r0
	ldrh	r3, [r5, #3156]
	ldr	r2, .L260+16
	movs	r1, #64
	mov	r0, r4
	bl	snprintf
	add	r4, r4, r0
	ldrh	r3, [r5, #3158]
	ldr	r2, .L260+20
	movs	r1, #64
	mov	r0, r4
	bl	snprintf
	add	r4, r4, r0
	ldr	r3, [r5, #3160]
	ldr	r2, .L260+24
	movs	r1, #64
	mov	r0, r4
	bl	snprintf
	add	r4, r4, r0
	ldrh	r3, [r5, #3164]
	ldr	r2, .L260+28
	movs	r1, #64
	mov	r0, r4
	bl	snprintf
	add	r4, r4, r0
	bl	GetFreeBlockMinEraseCount
	ldr	r2, .L260+32
	mov	r3, r0
	movs	r1, #64
	mov	r0, r4
	bl	snprintf
	add	r4, r4, r0
	ldrh	r0, [r5, #228]
	bl	GetFreeBlockMaxEraseCount
	ldr	r2, .L260+36
	mov	r3, r0
	movs	r1, #64
	mov	r0, r4
	bl	snprintf
	ldr	r3, .L260+40
	add	r4, r4, r0
	ldr	r3, [r3]
	cmp	r3, #1
	beq	.L246
.L251:
	subs	r0, r4, r6
	add	sp, sp, #20
	@ sp needed
	pop	{r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L246:
	ldrh	r3, [r5, #176]
	movw	r2, #65535
	cmp	r3, r2
	beq	.L248
	ldr	r2, [r5, #76]
	mov	r0, r4
	movs	r1, #64
	ldrh	r3, [r2, r3, lsl #1]
	ldr	r2, .L260+44
	bl	snprintf
	add	r4, r4, r0
.L248:
	movs	r0, #0
	ldr	r9, .L260+52
	bl	List_get_gc_head_node
	uxth	r0, r0
	movs	r7, #0
	movw	fp, #65535
	mov	r10, #6
.L250:
	cmp	r0, fp
	beq	.L249
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
	bne	.L250
.L249:
	ldr	r2, [r5, #2520]
	movs	r7, #0
	ldr	r3, [r5, #2536]
	movw	r9, #65535
	ldr	fp, .L260+56
	mov	r10, #6
	subs	r3, r3, r2
	asrs	r2, r3, #1
	ldr	r3, .L260+48
	muls	r3, r2, r3
	uxth	r3, r3
.L252:
	cmp	r3, r9
	beq	.L251
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
	beq	.L251
	ldr	r3, [r5, #2520]
	ldrh	r3, [r3, r8]
	b	.L252
.L261:
	.align	2
.L260:
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
	blt	.L264
	ldr	r3, .L265
	movs	r1, #64
	ldr	r2, .L265+4
	mov	r0, r5
	bl	snprintf
	adds	r4, r5, r0
	mov	r0, r4
	bl	FtlPrintInfo2buf
	add	r0, r0, r4
	subs	r0, r0, r5
	pop	{r3, r4, r5, pc}
.L264:
	movs	r0, #0
	pop	{r3, r4, r5, pc}
.L266:
	.align	2
.L265:
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
	ldr	r4, .L291
	ldr	r2, [r4, #2608]
	ldr	r3, [r4, #2620]
	cmp	r2, r3
	bcs	.L268
	ldrh	r1, [r4, #244]
	movs	r3, #0
	ldr	r0, [r4, #2540]
	str	r3, [r4, #2600]
.L269:
	cmp	r3, r1
	bcc	.L270
	ldr	r5, [r4, #2600]
	mov	r0, r5
	bl	__aeabi_uidiv
	str	r0, [r4, #2608]
	ldr	r0, [r4, #2604]
	ldrh	r1, [r4, #296]
	subs	r0, r5, r0
	bl	__aeabi_uidiv
	str	r0, [r4, #2600]
.L271:
	ldr	r6, [r4, #2620]
	ldr	r7, [r4, #2608]
	add	r3, r6, #256
	cmp	r3, r7
	bls	.L276
	ldr	r2, [r4, #2616]
	add	r3, r6, #768
	cmp	r3, r2
	bls	.L276
.L278:
	movw	r5, #65535
.L277:
	mov	r0, r5
	add	sp, sp, #28
	@ sp needed
	pop	{r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L270:
	ldrh	r2, [r0, r3, lsl #1]
	adds	r3, r3, #1
	ldr	r5, [r4, #2600]
	add	r2, r2, r5
	str	r2, [r4, #2600]
	b	.L269
.L268:
	ldr	r3, [r4, #2616]
	cmp	r2, r3
	bls	.L271
	adds	r3, r3, #1
	str	r3, [r4, #2616]
	movs	r3, #0
.L273:
	ldrh	r2, [r4, #244]
	cmp	r3, r2
	bcs	.L271
	ldr	r1, [r4, #2540]
	ldrh	r2, [r1, r3, lsl #1]
	adds	r2, r2, #1
	strh	r2, [r1, r3, lsl #1]	@ movhi
	adds	r3, r3, #1
	b	.L273
.L276:
	ldrh	r0, [r4, #228]
	add	r0, r0, r0, lsl #1
	ubfx	r0, r0, #2, #16
	bl	GetFreeBlockMaxEraseCount
	add	r3, r6, #64
	mov	r9, r0
	cmp	r0, r3
	bcc	.L278
	ldr	r3, [r4, #2524]
	cmp	r3, #0
	beq	.L278
	ldrh	r2, [r4, #244]
	movw	r1, #65535
	ldr	lr, [r4, #2520]
	movs	r0, #0
	ldr	r8, [r4, #2540]
	mov	r10, r1
	str	r2, [sp, #20]
	mov	fp, #6
	mov	r2, r1
.L279:
	ldrh	ip, [r3]
	cmp	ip, r10
	bne	.L282
	mov	r5, r2
.L281:
	movw	r3, #65535
	cmp	r5, r3
	beq	.L278
	ldrh	r10, [r8, r5, lsl #1]
	lsl	fp, r5, #1
	cmp	r6, r10
	bcs	.L283
	bl	GetFreeBlockMinEraseCount
	cmp	r6, r0
	it	cc
	strcc	r1, [r4, #2620]
.L283:
	cmp	r7, r10
	bls	.L278
	add	r3, r10, #128
	cmp	r9, r3
	ble	.L278
	add	r3, r10, #256
	cmp	r7, r3
	bhi	.L284
	ldr	r3, [r4, #2616]
	add	r10, r10, #768
	cmp	r10, r3
	bcs	.L278
.L284:
	str	r9, [sp, #8]
	mov	r2, r7
	ldrh	r3, [r8, fp]
	mov	r1, r5
	ldr	r0, .L291+4
	str	r3, [sp, #4]
	ldr	r3, [r4, #76]
	ldrh	r3, [r3, fp]
	str	r3, [sp]
	ldr	r3, [r4, #2616]
	bl	sftl_printk
	movs	r3, #1
	str	r3, [r4, #3168]
	b	.L277
.L282:
	adds	r0, r0, #1
	ldr	r5, [sp, #20]
	uxth	r0, r0
	cmp	r0, r5
	bhi	.L278
	ldrh	r5, [r3, #4]
	cbz	r5, .L280
	ldr	r5, .L291+8
	sub	r3, r3, lr
	asrs	r3, r3, #1
	muls	r3, r5, r3
	uxth	r5, r3
	ldrh	r3, [r8, r5, lsl #1]
	cmp	r6, r3
	bcs	.L281
	cmp	r1, r3
	itt	hi
	movhi	r1, r3
	movhi	r2, r5
.L280:
	mla	r3, fp, ip, lr
	b	.L279
.L292:
	.align	2
.L291:
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
	beq	.L294
	ldr	r2, .L295
	movs	r1, #0
	ldr	r2, [r2, #76]
	strh	r1, [r2, r0, lsl #1]	@ movhi
	bl	INSERT_FREE_LIST
.L294:
	movs	r0, #0
	pop	{r3, pc}
.L296:
	.align	2
.L295:
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
	bne	.L298
	movw	r2, #2781
	ldr	r1, .L312
	ldr	r0, .L312+4
	bl	sftl_printk
.L298:
	ldr	r6, .L312+8
	ldrh	r2, [r4, #2]
	ldrh	r3, [r6, #306]
	cmp	r2, r3
	bne	.L299
	movw	r2, #2782
	ldr	r1, .L312
	ldr	r0, .L312+4
	bl	sftl_printk
.L299:
	ldrh	r3, [r4, #4]
	cbnz	r3, .L300
	movw	r2, #2783
	ldr	r1, .L312
	ldr	r0, .L312+4
	bl	sftl_printk
.L300:
	ldrb	r2, [r4, #6]	@ zero_extendqisi2
	movs	r3, #0
	ldrh	r1, [r6, #236]
	movw	r5, #65535
	strb	r3, [r4, #10]
	adds	r2, r2, #8
	ldrh	r0, [r4, r2, lsl #1]
	mov	r2, r3
.L301:
	cmp	r0, r5
	beq	.L303
	ldrh	r5, [r4, #2]
	ldrh	r6, [r6, #306]
	cmp	r5, r6
	bcs	.L307
	ldrh	r2, [r4, #4]
	orr	r5, r5, r0, lsl #10
	ldrb	r3, [r4, #6]	@ zero_extendqisi2
	movw	r0, #65535
	subs	r2, r2, #1
	uxth	r2, r2
	strh	r2, [r4, #4]	@ movhi
.L306:
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
	beq	.L306
	strb	r3, [r4, #6]
	ldrh	r3, [r4, #2]
	cmp	r3, r6
	bne	.L297
	cbz	r2, .L297
	movw	r2, #2806
	ldr	r1, .L312
	ldr	r0, .L312+4
	bl	sftl_printk
.L297:
	mov	r0, r5
	pop	{r3, r4, r5, r6, r7, pc}
.L303:
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
	b	.L301
.L307:
	movw	r5, #65535
	b	.L297
.L313:
	.align	2
.L312:
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
	ldr	r3, .L323
	mov	ip, #20
	push	{r4, r5, r6, r7, r8, r9, lr}
	.save {r4, r5, r6, r7, r8, r9, lr}
	mov	r9, #12
	movs	r6, #1
	movs	r2, #0
	mov	r7, r3
	str	r2, [r3, #3172]
.L315:
	ldrh	r1, [r3, #236]
	uxth	r0, r2
	adds	r4, r2, #1
	cmp	r0, r1
	bcc	.L318
	ldr	r7, .L323
	mov	ip, #12
	movs	r6, #0
.L319:
	ldr	r2, [r3, #3192]
	cmp	r1, r2
	bcc	.L322
	pop	{r4, r5, r6, r7, r8, r9, pc}
.L318:
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
	b	.L315
.L322:
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
	b	.L319
.L324:
	.align	2
.L323:
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
	ldr	r3, .L332
	push	{r4, r5, r6, r7, r8, r9, r10, lr}
	.save {r4, r5, r6, r7, r8, r9, r10, lr}
	movs	r5, #0
	mov	r10, #20
	mov	lr, #12
	mov	r8, r5
	ldr	ip, [r3, #3192]
	ldr	r6, [r3, #3176]
.L326:
	uxth	r3, r5
	cmp	r1, r3
	bls	.L325
	mla	r4, r10, r3, r0
	movs	r2, #0
.L327:
	uxth	r3, r2
	cmp	ip, r3
	bls	.L328
	mul	r3, lr, r3
	ldr	r7, [r4, #8]
	adds	r2, r2, #1
	add	r9, r6, r3
	ldr	r3, [r6, r3]
	cmp	r3, r7
	bne	.L327
	str	r8, [r9, #8]
.L328:
	adds	r5, r5, #1
	b	.L326
.L325:
	pop	{r4, r5, r6, r7, r8, r9, r10, pc}
.L333:
	.align	2
.L332:
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
	ldr	r3, .L341
	mov	ip, #12
	push	{r4, r5, r6, r7, r8, r9, lr}
	.save {r4, r5, r6, r7, r8, r9, lr}
	movs	r4, #0
	mov	lr, #1
	mov	r8, #20
	ldr	r5, [r3, #3192]
	ldr	r6, [r3, #3176]
.L335:
	uxth	r2, r4
	cmp	r1, r2
	bhi	.L339
	pop	{r4, r5, r6, r7, r8, r9, pc}
.L339:
	mov	r9, #0
.L336:
	uxth	r3, r9
	cmp	r5, r3
	bls	.L337
	mla	r3, ip, r3, r6
	add	r9, r9, #1
	ldr	r7, [r3, #8]
	cmp	r7, #0
	bne	.L336
	mla	r2, r8, r2, r0
	ldr	r7, [r3]
	str	lr, [r3, #8]
	str	r7, [r2, #8]
	ldr	r3, [r3, #4]
	str	r3, [r2, #12]
.L337:
	adds	r4, r4, #1
	b	.L335
.L342:
	.align	2
.L341:
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
	ldr	r2, .L348
	ldr	r3, [r2, #3196]
	ldrh	r2, [r2, #3200]
	add	r2, r3, r2, lsl #1
.L344:
	cmp	r3, r2
	bne	.L346
	movs	r0, #0
	bx	lr
.L346:
	ldrh	r1, [r3], #2
	cmp	r1, r0
	bne	.L344
	movs	r0, #1
	bx	lr
.L349:
	.align	2
.L348:
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
	ldr	r3, .L354
	ldrh	r1, [r3, #3200]
	ldr	r2, [r3, #3196]
.L351:
	uxth	ip, r4
	cmp	ip, r1
	bcc	.L353
	bne	.L352
	strh	r0, [r2, ip, lsl #1]	@ movhi
	ldrh	r0, [r3, #3200]
	adds	r0, r0, #1
	strh	r0, [r3, #3200]	@ movhi
	b	.L352
.L353:
	adds	r4, r4, #1
	add	ip, r2, r4, lsl #1
	ldrh	ip, [ip, #-2]
	cmp	ip, r0
	bne	.L351
.L352:
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
.L355:
	.align	2
.L354:
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
	ldr	r0, .L359
	bl	sftl_printk
	ldr	r3, .L359+4
	ldrh	r0, [r3, #226]
	cmp	r4, r0
	beq	.L357
	ldrh	r1, [r3, #224]
	cmp	r4, r1
	beq	.L357
	movw	r2, #65535
	cmp	r0, r2
	bne	.L358
	strh	r4, [r3, #226]	@ movhi
.L357:
	movs	r0, #0
	pop	{r4, pc}
.L358:
	cmp	r1, r2
	it	eq
	strheq	r4, [r3, #224]	@ movhi
	b	.L357
.L360:
	.align	2
.L359:
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
	ldr	r4, .L365
	bl	P2V_block_in_plane
	mov	r2, r5
	mov	r6, r0
	ldrh	r1, [r4, #3210]
	ldr	r0, .L365+4
	bl	sftl_printk
	mov	r0, r6
	bl	FtlGcRefreshBlock
	ldrh	r3, [r4, #3210]
	movs	r2, #0
	addw	r0, r4, #3212
.L362:
	uxth	r1, r2
	cmp	r3, r1
	bhi	.L364
	cmp	r3, #15
	itttt	ls
	addls	r2, r3, #1
	strhls	r2, [r4, #3210]	@ movhi
	addls	r4, r4, r3, lsl #1
	strhls	r5, [r4, #3212]	@ movhi
	b	.L363
.L364:
	adds	r2, r2, #1
	add	r1, r0, r2, lsl #1
	ldrh	r1, [r1, #-2]
	cmp	r1, r5
	bne	.L362
.L363:
	movs	r0, #0
	pop	{r4, r5, r6, pc}
.L366:
	.align	2
.L365:
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
	ldr	r4, .L373
	ldrh	r3, [r4, #3210]
	cbz	r3, .L368
	ldrh	r1, [r4, #226]
	movw	r2, #65535
	cmp	r1, r2
	bne	.L368
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
.L368:
	movs	r0, #0
	pop	{r4, pc}
.L374:
	.align	2
.L373:
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
	ldr	r1, .L376
	b	__kmalloc
.L377:
	.align	2
.L376:
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
	ldr	fp, .L388+16
	mov	r10, r0
	mov	r7, r1
	mov	r8, r2
	mov	r9, r3
	mov	r5, r6
	mov	r4, r6
.L380:
	cmp	r4, r9
	bcc	.L386
	ldr	r0, .L388
	pop	{r3, r4, r5, r6, r7, r8, r9, r10, fp, lr}
	b	sftl_printk
.L386:
	cbnz	r5, .L381
	mov	r3, r6
	mov	r2, r7
	mov	r1, r10
	ldr	r0, .L388+4
	bl	sftl_printk
.L381:
	cmp	r8, #4
	bne	.L382
	ldr	r1, [r7, r4, lsl #2]
	ldr	r0, .L388+8
.L387:
	adds	r5, r5, #1
	bl	sftl_printk
	cmp	r5, #15
	bls	.L385
	movs	r5, #0
	ldr	r0, .L388
	bl	sftl_printk
.L385:
	adds	r4, r4, #1
	add	r6, r6, r8
	b	.L380
.L382:
	cmp	r8, #2
	ittee	eq
	ldrheq	r1, [r7, r4, lsl #1]
	moveq	r0, fp
	ldrbne	r1, [r7, r4]	@ zero_extendqisi2
	ldrne	r0, .L388+12
	b	.L387
.L389:
	.align	2
.L388:
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
	ldr	r5, .L406
	mov	r4, r0
	mov	fp, r0
	movs	r7, #0
	mov	r10, #-1
	.pad #20
	sub	sp, sp, #20
	ldrh	r9, [r5, #12]
	lsl	r3, r9, #3
	str	r3, [sp, #4]
.L391:
	cmp	r7, r8
	beq	.L395
	add	r2, sp, #8
	add	r1, sp, #12
	mov	r0, fp
	bl	l2p_addr_tran
	ldr	r6, [sp, #8]
	cbnz	r6, .L392
	ldr	r2, [sp, #12]
	ldr	r3, [sp, #4]
	cmp	r3, r2
	bls	.L392
	ldr	r5, .L406+4
	ldr	r7, .L406+8
	ldr	r9, .L406+16
	b	.L405
.L394:
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
	ldr	r0, .L406+12
	bl	rknand_print_hex
.L405:
	cmp	r6, r8
	add	r4, r4, #20
	bne	.L394
	bl	dump_stack
.L395:
	movs	r0, #0
	add	sp, sp, #20
	@ sp needed
	pop	{r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L392:
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
	bne	.L399
	ldr	r1, [sp, #12]
	ldr	r2, [r5, #3252]
	ldrb	r0, [sp, #8]	@ zero_extendqisi2
	add	r1, r1, r9
	blx	r2
	cbz	r0, .L399
	str	r10, [fp]
.L399:
	adds	r7, r7, #1
	add	fp, fp, #20
	b	.L391
.L407:
	.align	2
.L406:
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
	bhi	.L408
	ldr	r4, .L417
	ldrh	r3, [r4, #422]
	cmp	r3, #1024
	beq	.L408
	cbz	r1, .L410
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
.L410:
	ldrh	r3, [r4, #422]
	adds	r3, r3, #1
	strh	r3, [r4, #422]	@ movhi
	ldrh	r3, [r4, #420]
	add	r2, r4, r3, lsl #1
	adds	r3, r3, #1
	ubfx	r3, r3, #0, #10
	strh	r5, [r2, #424]	@ movhi
	strh	r3, [r4, #420]	@ movhi
.L408:
	pop	{r4, r5, r6, pc}
.L418:
	.align	2
.L417:
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
	ldr	r4, .L424
	ldrh	r2, [r4, #422]
	cbz	r2, .L423
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
.L420:
	subs	r3, r5, #1
	movw	r2, #65533
	uxth	r3, r3
	cmp	r3, r2
	bls	.L421
	ldrh	r2, [r4, #422]
	mov	r1, r5
	ldr	r0, .L424+4
	bl	sftl_printk
.L422:
	b	.L422
.L423:
	movw	r5, #65535
	b	.L420
.L421:
	mov	r0, r5
	pop	{r3, r4, r5, pc}
.L425:
	.align	2
.L424:
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
.L427:
	uxth	r5, r3
	cmp	r5, r1
	bcs	.L430
	mov	r7, r2
	adds	r3, r3, #1
	ldrh	r6, [r7]
	adds	r2, r2, #2
	cmp	r6, #0
	bne	.L427
	bl	FtlFreeSysBlkQueueOut
	subs	r3, r0, #1
	movw	r2, #65533
	uxth	r3, r3
	mov	r1, r0
	strh	r0, [r7]	@ movhi
	cmp	r3, r2
	bls	.L428
	ldr	r3, .L434
	ldr	r0, .L434+4
	ldrh	r2, [r3, #422]
	bl	sftl_printk
.L429:
	b	.L429
.L428:
	ldr	r3, [r4, #28]
	strh	r6, [r4, #2]	@ movhi
	strh	r5, [r4]	@ movhi
	adds	r3, r3, #1
	str	r3, [r4, #28]
	ldrh	r3, [r4, #8]
	adds	r3, r3, #1
	strh	r3, [r4, #8]	@ movhi
.L430:
	ldrh	r3, [r4, #10]
	cmp	r3, r5
	bhi	.L432
	movw	r2, #578
	ldr	r1, .L434+8
	ldr	r0, .L434+12
	bl	sftl_printk
.L432:
	movs	r0, #0
	pop	{r3, r4, r5, r6, r7, pc}
.L435:
	.align	2
.L434:
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
	ldr	r4, .L538
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
	ldr	r0, .L538+4
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
.L438:
	cmp	r3, r6
	bcc	.L439
	ldr	r2, .L538+8
	movs	r1, #0
	add	r3, r2, r3, lsl #2
	adds	r2, r2, #56
	adds	r3, r3, #24
.L440:
	cmp	r2, r3
	bne	.L441
	ldr	r3, [r4, #3360]
	cbnz	r3, .L442
.L444:
	ldr	r1, .L538+12
	ldr	r0, .L538+16
	bl	sftl_printk
	mov	r0, #-1
	pop	{r4, r5, r6, r7, r8, pc}
.L439:
	ldr	r0, [r4, #384]
	adds	r3, r3, #1
	add	r0, r0, r1
	add	r1, r1, r2
	str	r0, [r5, #4]!
	b	.L438
.L441:
	str	r1, [r3, #4]!
	b	.L440
.L539:
	.align	2
.L538:
	.word	.LANCHOR0
	.word	33553920
	.word	.LANCHOR0+356
	.word	.LANCHOR1+173
	.word	.LC91
.L442:
	ldr	r3, [r4, #3364]
	cmp	r3, #0
	beq	.L444
	ldr	r3, [r4, #3380]
	cmp	r3, #0
	beq	.L444
	ldr	r3, [r4, #3384]
	cmp	r3, #0
	beq	.L444
	ldr	r3, [r4, #2544]
	cmp	r3, #0
	beq	.L444
	ldr	r3, [r4, #3388]
	cmp	r3, #0
	beq	.L444
	ldr	r3, [r4, #2520]
	cmp	r3, #0
	beq	.L444
	ldr	r3, [r4, #384]
	cmp	r3, #0
	beq	.L444
	ldr	r3, [r4, #76]
	cmp	r3, #0
	beq	.L444
	ldr	r3, [r4, #3196]
	cmp	r3, #0
	beq	.L444
	ldr	r3, [r4, #3204]
	cmp	r3, #0
	beq	.L444
	ldr	r3, [r4, #3292]
	cmp	r3, #0
	beq	.L444
	ldr	r3, [r4, #3300]
	cmp	r3, #0
	beq	.L444
	ldr	r3, [r4, #3272]
	cmp	r3, #0
	beq	.L444
	ldr	r3, [r4, #3188]
	cmp	r3, #0
	beq	.L444
	ldr	r3, [r4, #3296]
	cmp	r3, #0
	beq	.L444
	ldr	r3, [r4, #3304]
	cmp	r3, #0
	beq	.L444
	ldr	r3, [r4, #3308]
	cmp	r3, #0
	beq	.L444
	ldr	r3, [r4, #3312]
	cmp	r3, #0
	beq	.L444
	ldr	r3, [r4, #3180]
	cmp	r3, #0
	beq	.L444
	ldr	r3, [r4, #3316]
	cmp	r3, #0
	beq	.L444
	ldr	r3, [r4, #3320]
	cmp	r3, #0
	beq	.L444
	ldr	r3, [r4, #3176]
	cmp	r3, #0
	beq	.L444
	ldr	r3, [r4, #3336]
	cmp	r3, #0
	beq	.L444
	ldr	r3, [r4, #3340]
	cmp	r3, #0
	beq	.L444
	ldr	r3, [r4, #3184]
	cmp	r3, #0
	beq	.L444
	ldr	r3, [r4, #2540]
	cmp	r3, #0
	beq	.L444
	ldr	r3, [r4, #3348]
	cmp	r3, #0
	beq	.L444
	ldr	r3, [r4, #352]
	cmp	r3, #0
	beq	.L444
	ldr	r3, .L540
	ldr	r2, [r3, #3368]
	cmp	r2, #0
	beq	.L444
	ldr	r2, [r3, #3372]
	cmp	r2, #0
	beq	.L444
	ldr	r3, [r3, #3376]
	cmp	r3, #0
	beq	.L444
	movs	r0, #0
	pop	{r4, r5, r6, r7, r8, pc}
.L541:
	.align	2
.L540:
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
	ldr	r6, .L548
	mov	r7, r1
	subs	r4, r5, #2
	movw	r8, #65535
	ldr	r9, .L548+4
	addw	r5, r5, #1022
	ldrh	r2, [r6, #3392]
	movs	r1, #0
	ldr	r10, .L548+8
	mov	r0, r7
	lsls	r2, r2, #2
	bl	ftl_memset
.L545:
	ldrh	r3, [r4, #2]
	cmp	r3, r8
	beq	.L542
	ldrh	r2, [r6, #302]
	cmp	r2, r3
	bhi	.L544
	movs	r2, #74
	mov	r1, r9
	mov	r0, r10
	bl	sftl_printk
.L544:
	ldrh	r3, [r4, #2]!
	movs	r2, #1
	cmp	r5, r4
	lsr	r1, r3, #5
	and	r3, r3, #31
	lsl	r3, r2, r3
	ldr	r2, [r7, r1, lsl #2]
	orr	r2, r2, r3
	str	r2, [r7, r1, lsl #2]
	bne	.L545
.L542:
	pop	{r4, r5, r6, r7, r8, r9, r10, pc}
.L549:
	.align	2
.L548:
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
	ldr	r0, .L551
	movw	r3, #65535
	movs	r2, #16
	movs	r1, #255
	strh	r3, [r0, #356]	@ movhi
	movs	r3, #0
	strh	r3, [r0, #362]	@ movhi
	add	r0, r0, #368
	b	ftl_memset
.L552:
	.align	2
.L551:
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
	ldr	r3, .L554
	mov	r2, #2048
	push	{r4, lr}
	.save {r4, lr}
	movs	r4, #0
	mov	r1, r4
	strh	r0, [r3, #416]	@ movhi
	add	r0, r3, #424
	strh	r4, [r3, #418]	@ movhi
	strh	r4, [r3, #420]	@ movhi
	strh	r4, [r3, #422]	@ movhi
	bl	ftl_memset
	mov	r0, r4
	pop	{r4, pc}
.L555:
	.align	2
.L554:
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
.L557:
	ldrh	r1, [r4, #6]
	uxth	r2, r3
	cmp	r1, r2
	bhi	.L561
	ldr	r3, .L576
	movs	r6, #0
	mov	r9, r6
	mov	fp, r6
	ldrh	r2, [r3, #308]
	ldrh	r3, [r4]
	strh	r2, [r5, r3, lsl #1]	@ movhi
	ldrh	r10, [r5]
.L562:
	ldrh	r3, [r4, #10]
	uxth	r1, r6
	cmp	r3, r1
	bhi	.L566
	mov	r0, r9
	pop	{r3, r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L561:
	uxth	r2, r3
	ldr	r1, [r6, r2, lsl #2]
	movs	r2, #0
	ubfx	r1, r1, #10, #16
.L558:
	ldrh	ip, [r4, #10]
	uxth	r0, r2
	cmp	ip, r0
	bhi	.L560
	adds	r3, r3, #1
	b	.L557
.L560:
	uxth	r0, r2
	ldrh	ip, [r7, r0, lsl #1]
	cmp	ip, r1
	bne	.L559
	cbz	r1, .L559
	ldrh	ip, [r5, r0, lsl #1]
	add	ip, ip, #1
	strh	ip, [r5, r0, lsl #1]	@ movhi
.L559:
	adds	r2, r2, #1
	b	.L558
.L566:
	uxth	r2, r6
	ldrh	r3, [r5, r2, lsl #1]
	lsl	r8, r2, #1
	cmp	r10, r3
	bls	.L563
	ldrh	r0, [r7, r2, lsl #1]
	add	r8, r8, r7
	cbnz	r0, .L564
.L565:
	adds	r6, r6, #1
	b	.L562
.L563:
	cmp	r3, #0
	bne	.L565
	ldrh	r0, [r7, r2, lsl #1]
	add	r8, r8, r7
	cmp	r0, #0
	beq	.L565
.L567:
	movs	r1, #1
	bl	FtlFreeSysBlkQueueIn
	strh	fp, [r8]	@ movhi
	ldrh	r3, [r4, #8]
	subs	r3, r3, #1
	strh	r3, [r4, #8]	@ movhi
	b	.L565
.L564:
	mov	r9, r1
	mov	r10, r3
	cmp	r3, #0
	beq	.L567
	b	.L565
.L577:
	.align	2
.L576:
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
	ldr	r4, .L581
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
.L579:
	ldrh	r4, [r3, #342]
	uxth	r0, r2
	adds	r5, r2, #1
	cmp	r4, r0
	bhi	.L580
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
.L580:
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
	b	.L579
.L582:
	.align	2
.L581:
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
	ldr	r4, .L584
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
	mov	r1, r5
	movs	r2, #48
	addw	r0, r4, #2472
	bl	ftl_memset
	mov	r2, #512
	mov	r1, r5
	addw	r0, r4, #2644
	bl	ftl_memset
	bl	FtlGcBufInit
	bl	FtlL2PDataInit
	mov	r0, r5
	pop	{r3, r4, r5, pc}
.L585:
	.align	2
.L584:
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
	ldr	r4, .L597
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
.L587:
	ldrh	r3, [r4, #244]
	uxth	r8, r5
	cmp	r8, r3
	bcs	.L594
	ldrh	r3, [r4, #306]
	ldrh	r2, [r4, #236]
	str	r3, [sp, #4]
	movs	r3, #0
	mov	r7, r3
	b	.L595
.L589:
	ldrb	r0, [r10, r3]	@ zero_extendqisi2
	mov	r1, r8
	str	r2, [sp, #12]
	str	r3, [sp, #8]
	bl	V2P_block
	bl	FtlBbmIsBadBlock
	ldr	r3, [sp, #8]
	ldr	r2, [sp, #12]
	cbnz	r0, .L588
	ldr	r1, [sp, #4]
	add	r7, r7, r1
	uxth	r7, r7
.L588:
	adds	r3, r3, #1
.L595:
	uxth	r1, r3
	cmp	r2, r1
	bhi	.L589
	uxth	r3, r5
	cbz	r7, .L590
	mov	r1, r7
	str	r3, [sp, #4]
	mov	r0, #32768
	bl	__aeabi_idiv
	ldr	r3, [sp, #4]
	uxth	r7, r0
.L591:
	ldr	r1, [r4, #2520]
	movs	r2, #6
	mla	r2, r2, r3, r1
	strh	r7, [r2, #4]	@ movhi
	ldrh	r2, [r4, #28]
	cmp	r2, r8
	beq	.L592
	ldrh	r2, [r4, #80]
	cmp	r2, r8
	beq	.L592
	ldrh	r2, [r4, #128]
	cmp	r2, r8
	beq	.L592
	ldr	r2, [r4, #76]
	ldrh	r3, [r2, r3, lsl #1]
	cbnz	r3, .L593
	add	r9, r9, #1
	mov	r0, r8
	uxth	r9, r9
	bl	INSERT_FREE_LIST
.L592:
	adds	r5, r5, #1
	b	.L587
.L590:
	ldr	r2, [r4, #76]
	strh	fp, [r2, r3, lsl #1]	@ movhi
	b	.L591
.L593:
	adds	r6, r6, #1
	mov	r0, r8
	uxth	r6, r6
	bl	INSERT_DATA_LIST
	b	.L592
.L594:
	strh	r6, [r4, #2532]	@ movhi
	add	r6, r6, r9
	cmp	r6, r3
	strh	r9, [r4, #228]	@ movhi
	ble	.L596
	movw	r2, #2210
	ldr	r1, .L597+4
	ldr	r0, .L597+8
	bl	sftl_printk
.L596:
	movs	r0, #0
	add	sp, sp, #20
	@ sp needed
	pop	{r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L598:
	.align	2
.L597:
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
	ldr	r4, .L600
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
.L601:
	.align	2
.L600:
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
	ldr	r5, .L609
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
	bls	.L603
	mov	r2, #256
	movs	r1, #255
	mov	r0, r4
	bl	ftl_memset
	movs	r0, #0
.L603:
	ldrh	r3, [r5, #14]
	cmp	r3, #4
	bne	.L608
	add	r1, r4, r0, lsl #1
	mov	r3, r4
.L605:
	cmp	r3, r1
	bne	.L606
.L608:
	pop	{r4, r5, r6, pc}
.L606:
	ldrh	r2, [r3]
	lsrs	r2, r2, #1
	strh	r2, [r3], #2	@ movhi
	b	.L605
.L610:
	.align	2
.L609:
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
	@ args = 0, pretend = 0, frame = 16
	@ frame_needed = 0, uses_anonymous_args = 0
	push	{r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.save {r4, r5, r6, r7, r8, r9, r10, fp, lr}
	mov	r9, r1
	ldr	r5, .L652
	mov	r4, r0
	mov	r8, #0
	.pad #20
	sub	sp, sp, #20
	ldr	r10, .L652+16
	ldr	fp, .L652+20
	ldrh	r3, [r5, #12]
	str	r3, [sp, #4]
.L613:
	cmp	r8, r9
	bne	.L627
	movs	r0, #0
	add	sp, sp, #20
	@ sp needed
	pop	{r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L627:
	ldr	r3, [r4, #8]
	cbz	r3, .L614
	ldr	r3, [r4, #12]
	cbnz	r3, .L615
.L614:
	movs	r2, #96
	ldr	r1, .L652+4
	mov	r0, r10
	bl	sftl_printk
.L615:
	add	r2, sp, #8
	add	r1, sp, #12
	mov	r0, r4
	bl	l2p_addr_tran
	ldr	r0, [sp, #8]
	cmp	r0, #3
	bls	.L616
	mov	r3, #-1
	str	r3, [r4]
.L617:
	add	r8, r8, #1
	adds	r4, r4, #20
	b	.L613
.L616:
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
	bne	.L620
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
	beq	.L621
	ldr	r3, [r4, #12]
	ldr	r2, [r3, #12]
	adds	r2, r2, #1
	bne	.L622
	ldr	r2, [r3, #8]
	adds	r2, r2, #1
	bne	.L622
	ldr	r3, [r3]
	adds	r3, r3, #1
	beq	.L622
.L621:
	mov	r3, #-1
	str	r3, [r4]
.L622:
	ldr	r3, [r4]
	adds	r3, r3, #1
	beq	.L623
	cmp	r0, #256
	it	eq
	streq	r0, [r4]
.L623:
	ldr	r3, [r4]
	adds	r2, r3, #1
	beq	.L624
	cmp	r3, #256
	bne	.L620
.L624:
	ldr	r1, [r4, #4]
	mov	r0, fp
	ldr	r2, [sp, #12]
	bl	sftl_printk
	ldr	r1, [r4, #8]
	cbz	r1, .L626
	movs	r3, #4
	ldr	r0, .L652+8
	mov	r2, r3
	bl	rknand_print_hex
.L626:
	ldr	r1, [r4, #12]
	cbz	r1, .L620
	movs	r3, #4
	ldr	r0, .L652+12
	mov	r2, r3
	bl	rknand_print_hex
.L620:
	ldr	r3, [r5, #3324]
	cmp	r6, r3
	bne	.L617
	ldr	r0, [r4, #8]
	cmp	r6, r0
	beq	.L617
	ldrh	r2, [r5, #262]
	mov	r1, r6
	lsls	r2, r2, #9
	bl	ftl_memcpy
	b	.L617
.L653:
	.align	2
.L652:
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
	ldr	r4, .L664
	movw	r10, #61664
	ldr	r3, [r4, #3304]
	add	r7, r4, #366
	ldr	r8, [r4, #3336]
	add	r9, r4, #3456
	str	r3, [r4, #3464]
	str	r8, [r4, #3468]
.L655:
	ldrh	r3, [r4, #258]
	cmp	r6, r3
	bcc	.L660
	movs	r0, #0
	pop	{r4, r5, r6, r7, r8, r9, r10, pc}
.L660:
	ldrh	r5, [r4, #302]
	movw	r3, #65535
	strh	r3, [r7, #2]!	@ movhi
.L657:
	ldrh	r3, [r4, #302]
	subs	r5, r5, #1
	uxth	r5, r5
	sub	r2, r3, #16
	cmp	r5, r2
	ble	.L658
	mla	r3, r6, r3, r5
	movs	r2, #1
	mov	r1, r2
	mov	r0, r9
	lsls	r3, r3, #10
	str	r3, [r4, #3460]
	bl	FlashReadPages
	ldr	r3, [r4, #3456]
	adds	r3, r3, #1
	beq	.L657
	ldrh	r3, [r8]
	cmp	r3, r10
	bne	.L657
	strh	r5, [r7]	@ movhi
.L658:
	adds	r6, r6, #1
	b	.L655
.L665:
	.align	2
.L664:
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
	ldr	r3, .L677
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
	bne	.L669
	mov	r8, #0
.L670:
	cmp	r8, r5
	ble	.L673
.L669:
	mov	r0, r5
	add	sp, sp, #88
	@ sp needed
	pop	{r4, r5, r6, r7, r8, pc}
.L673:
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
	bne	.L671
	ldr	r3, [sp, #28]
	adds	r3, r3, #1
	bne	.L671
	ldr	r3, [sp, #4]
	adds	r3, r3, #1
	beq	.L671
	subs	r4, r4, #1
	sxth	r5, r4
	b	.L670
.L671:
	adds	r4, r4, #1
	sxth	r8, r4
	b	.L670
.L678:
	.align	2
.L677:
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
	ldr	r4, .L761
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
	bl	ftl_memset
	ldrh	r8, [r4, #244]
.L680:
	ldrh	r3, [r4, #246]
	cmp	r3, r8
	bls	.L722
	movs	r5, #0
	ldrh	fp, [r4, #236]
	ldr	r9, [r4, #3292]
	mov	r7, r5
	ldr	r3, [r4, #3180]
	mov	r10, #20
	ldr	r2, [r4, #3184]
	ldrh	r6, [r4, #316]
	b	.L723
.L683:
	str	r3, [sp, #8]
	mov	r1, r8
	ldr	r3, .L761+4
	str	r2, [sp, #12]
	ldrb	r0, [r3, r5]	@ zero_extendqisi2
	bl	V2P_block
	str	r0, [sp, #4]
	bl	FtlBbmIsBadBlock
	ldr	r3, [sp, #8]
	ldr	r2, [sp, #12]
	cbnz	r0, .L681
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
.L681:
	adds	r5, r5, #1
.L723:
	uxth	r1, r5
	cmp	fp, r1
	bhi	.L683
	cbnz	r7, .L684
.L721:
	add	r8, r8, #1
	uxth	r8, r8
	b	.L680
.L684:
	mov	r0, r9
	mov	r9, #0
	movs	r2, #1
	mov	r1, r7
	bl	FlashReadPages
.L685:
	uxth	r3, r9
	cmp	r7, r3
	bls	.L721
	movs	r3, #20
	mul	r10, r3, r9
	ldr	r3, [r4, #3292]
	add	r2, r3, r10
	ldr	r3, [r3, r10]
	ldr	r5, [r2, #4]
	ldr	r6, [r2, #12]
	adds	r3, r3, #1
	ubfx	r5, r5, #10, #16
	bne	.L688
	mov	fp, #16
	movw	r3, #65535
.L690:
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
	bne	.L687
	ldr	r3, [r4, #3292]
	mov	r2, #-1
	str	r2, [r3, r10]
	ldr	r3, [r4, #3292]
	ldr	r3, [r3, r10]
	cmp	r3, r2
	bne	.L688
.L689:
	movs	r1, #1
	b	.L760
.L687:
	ldr	r2, [r4, #3292]
	ldr	r2, [r2, r10]
	adds	r2, r2, #1
	bne	.L688
	add	fp, fp, #-1
	uxth	fp, fp
	cmp	fp, #0
	bne	.L690
	b	.L689
.L688:
	ldr	r2, [r4, #2592]
	ldr	r3, [r6, #4]
	adds	r1, r2, #1
	beq	.L691
	cmp	r2, r3
	bhi	.L692
.L691:
	adds	r2, r3, #1
	itt	ne
	addne	r2, r3, #1
	strne	r2, [r4, #2592]
.L692:
	ldrh	r2, [r6]
	movw	r1, #61604
	cmp	r2, r1
	beq	.L694
	bhi	.L695
	movw	r3, #61574
	cmp	r2, r3
	beq	.L696
.L693:
	add	r9, r9, #1
	b	.L685
.L695:
	movw	r3, #61634
	cmp	r2, r3
	beq	.L697
	movw	r3, #65535
	cmp	r2, r3
	bne	.L693
	movs	r1, #0
.L760:
	mov	r0, r5
	bl	FtlFreeSysBlkQueueIn
	b	.L693
.L697:
	ldrh	r2, [r4, #3440]
	ldr	r3, [r4, #332]
	cmp	r2, r3
	bls	.L699
	movw	r2, #1225
	ldr	r1, .L761+8
	ldr	r0, .L761+12
	bl	sftl_printk
.L699:
	ldr	r1, [r4, #332]
	ldrh	r0, [r4, #3440]
	ldr	r10, [r4, #3384]
	uxth	ip, r1
	add	r3, ip, #-1
	sub	ip, ip, r0
	add	ip, ip, #-1
	sxth	r3, r3
	sxth	ip, ip
.L700:
	cmp	r3, ip
	bgt	.L706
	cmp	r3, #0
	bge	.L738
	b	.L693
.L706:
	ldr	r2, [r6, #4]
	lsl	fp, r3, #2
	mov	lr, r2
	ldr	r2, [r10, r3, lsl #2]
	cmp	lr, r2
	bls	.L701
	ldr	r2, [r10]
	cbnz	r2, .L702
	cmp	r1, r0
	itt	ne
	addne	r0, r0, #1
	strhne	r0, [r4, #3440]	@ movhi
.L702:
	uxth	ip, r3
	movs	r2, #0
.L703:
	uxth	r0, r2
	sxth	r1, r2
	cmp	ip, r0
	bhi	.L704
	ldr	r2, [r4, #3384]
	cmp	r3, #0
	ldr	r1, [r6, #4]
	str	r1, [r2, fp]
	ldr	r2, [r4, #3360]
	strh	r5, [r2, r3, lsl #1]	@ movhi
	blt	.L693
	ldrh	r0, [r4, #3440]
	ldr	r2, [r4, #332]
	subs	r2, r2, r0
	subs	r2, r2, #1
	sxth	r2, r2
	cmp	r3, r2
	bgt	.L693
.L738:
	ldr	r2, [r4, #3384]
	adds	r0, r0, #1
	ldr	r1, [r6, #4]
	strh	r0, [r4, #3440]	@ movhi
	str	r1, [r2, r3, lsl #2]
	ldr	r2, [r4, #3360]
.L758:
	strh	r5, [r2, r3, lsl #1]	@ movhi
	b	.L693
.L704:
	ldr	r0, [r4, #3384]
	adds	r2, r2, #1
	add	lr, r0, r1, lsl #2
	ldr	lr, [lr, #4]
	str	lr, [r0, r1, lsl #2]
	ldr	r0, [r4, #3360]
	add	lr, r0, r1, lsl #1
	ldrh	lr, [lr, #2]
	strh	lr, [r0, r1, lsl #1]	@ movhi
	b	.L703
.L701:
	subs	r3, r3, #1
	sxth	r3, r3
	b	.L700
.L762:
	.align	2
.L761:
	.word	.LANCHOR0
	.word	.LANCHOR0+264
	.word	.LANCHOR1+231
	.word	.LC8
.L696:
	ldrh	r2, [r4, #348]
	ldrh	r3, [r4, #324]
	cmp	r2, r3
	bls	.L709
	movw	r2, #1266
	ldr	r1, .L763
	ldr	r0, .L763+4
	bl	sftl_printk
.L709:
	ldrh	lr, [r4, #324]
	ldrh	r0, [r4, #348]
	ldr	r10, [r4, #3372]
	add	ip, lr, #-1
	sxth	r3, ip
	sub	ip, ip, r0
.L710:
	cmp	r3, ip
	ble	.L715
	ldr	r2, [r6, #4]
	lsl	fp, r3, #2
	ldr	r1, [r10, r3, lsl #2]
	cmp	r2, r1
	bls	.L711
	ldr	r2, [r10]
	cbnz	r2, .L712
	cmp	lr, r0
	itt	ne
	addne	r0, r0, #1
	strhne	r0, [r4, #348]	@ movhi
.L712:
	uxth	ip, r3
	movs	r2, #0
.L713:
	uxth	r0, r2
	sxth	r1, r2
	cmp	ip, r0
	bhi	.L714
	ldr	r2, [r4, #3372]
	ldr	r1, [r6, #4]
	str	r1, [r2, fp]
	ldr	r2, [r4, #352]
	strh	r5, [r2, r3, lsl #1]	@ movhi
.L715:
	cmp	r3, #0
	blt	.L693
	ldrh	r2, [r4, #324]
	ldrh	r1, [r4, #348]
	subs	r2, r2, #1
	subs	r2, r2, r1
	sxth	r2, r2
	cmp	r3, r2
	bgt	.L693
	adds	r1, r1, #1
	ldr	r2, [r4, #3372]
	strh	r1, [r4, #348]	@ movhi
	ldr	r1, [r6, #4]
	str	r1, [r2, r3, lsl #2]
	ldr	r2, [r4, #352]
	b	.L758
.L714:
	ldr	r0, [r4, #3372]
	adds	r2, r2, #1
	add	lr, r0, r1, lsl #2
	ldr	lr, [lr, #4]
	str	lr, [r0, r1, lsl #2]
	ldr	r0, [r4, #352]
	add	lr, r0, r1, lsl #1
	ldrh	lr, [lr, #2]
	strh	lr, [r0, r1, lsl #1]	@ movhi
	b	.L713
.L711:
	subs	r3, r3, #1
	sxth	r3, r3
	b	.L710
.L694:
	ldrh	r1, [r4, #2628]
	movw	r2, #65535
	cmp	r1, r2
	bne	.L717
	strh	r5, [r4, #2628]	@ movhi
.L759:
	str	r3, [r4, #2636]
	b	.L693
.L717:
	ldrh	r0, [r4, #2632]
	cmp	r0, r2
	beq	.L718
	movs	r1, #1
	bl	FtlFreeSysBlkQueueIn
.L718:
	ldr	r2, [r4, #2636]
	ldr	r3, [r6, #4]
	cmp	r2, r3
	bcs	.L719
	ldrh	r3, [r4, #2628]
	strh	r5, [r4, #2628]	@ movhi
	strh	r3, [r4, #2632]	@ movhi
	ldr	r3, [r6, #4]
	b	.L759
.L719:
	strh	r5, [r4, #2632]	@ movhi
	b	.L693
.L722:
	ldr	r1, [r4, #3360]
	ldrh	r3, [r1]
	cbz	r3, .L724
.L727:
	ldr	r1, [r4, #352]
	ldrh	r2, [r1]
	cmp	r2, #0
	beq	.L725
.L726:
	ldrh	r2, [r4, #3440]
	ldr	r3, [r4, #332]
	cmp	r2, r3
	bls	.L757
	movw	r2, #1391
	ldr	r1, .L763
	ldr	r0, .L763+4
	bl	sftl_printk
.L757:
	movs	r0, #0
	add	sp, sp, #20
	@ sp needed
	pop	{r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L724:
	ldrh	r2, [r4, #3440]
	cmp	r2, #0
	beq	.L727
	ldr	r0, [r4, #332]
.L728:
	sxth	r2, r3
	cmp	r2, r0
	bcs	.L727
	ldrh	r5, [r1, r2, lsl #1]
	adds	r3, r3, #1
	cmp	r5, #0
	beq	.L728
	mov	r3, r2
	movs	r5, #0
.L729:
	ldr	r1, [r4, #332]
	cmp	r3, r1
	bcs	.L727
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
	b	.L729
.L725:
	ldrh	r3, [r4, #348]
	cmp	r3, #0
	beq	.L726
	ldrh	r0, [r4, #324]
.L734:
	sxth	r3, r2
	cmp	r3, r0
	mov	r5, r3
	bge	.L726
	ldrh	r6, [r1, r3, lsl #1]
	adds	r2, r2, #1
	cmp	r6, #0
	beq	.L734
	movs	r0, #0
.L735:
	ldrh	r2, [r4, #324]
	cmp	r3, r2
	bge	.L726
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
	b	.L735
.L764:
	.align	2
.L763:
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
	ldr	r4, .L794
	ldr	r3, [r4, #3304]
	add	r7, r4, #3456
	ldr	r6, [r4, #3336]
	str	r3, [r4, #3464]
	str	r6, [r4, #3468]
	bl	FtlBbtMemInit
	ldrh	r5, [r4, #302]
	subs	r5, r5, #1
	uxth	r5, r5
.L766:
	ldrh	r3, [r4, #302]
	subs	r3, r3, #16
	cmp	r5, r3
	ble	.L769
	lsls	r3, r5, #10
	movs	r2, #1
	mov	r1, r2
	mov	r0, r7
	str	r3, [r4, #3460]
	bl	FlashReadPages
	ldr	r3, [r4, #3456]
	adds	r3, r3, #1
	bne	.L767
	ldr	r3, [r4, #3460]
	movs	r2, #1
	mov	r1, r2
	mov	r0, r7
	adds	r3, r3, #1
	str	r3, [r4, #3460]
	bl	FlashReadPages
.L767:
	ldr	r3, [r4, #3456]
	adds	r3, r3, #1
	beq	.L768
	ldrh	r3, [r6]
	cmp	r3, r8
	bne	.L768
	ldr	r3, [r6, #4]
	strh	r5, [r4, #356]	@ movhi
	str	r3, [r4, #364]
	ldrh	r3, [r6, #8]
	strh	r3, [r4, #360]	@ movhi
.L769:
	ldrh	r3, [r4, #356]
	movw	r2, #65535
	cmp	r3, r2
	beq	.L783
	ldrh	r3, [r4, #360]
	cmp	r3, r2
	beq	.L773
	lsls	r3, r3, #10
	movs	r2, #1
	mov	r1, r2
	ldr	r0, .L794+4
	str	r3, [r4, #3460]
	bl	FlashReadPages
	ldr	r3, [r4, #3456]
	adds	r3, r3, #1
	beq	.L773
	ldrh	r2, [r6]
	movw	r3, #61649
	cmp	r2, r3
	bne	.L773
	ldr	r3, [r6, #4]
	ldr	r2, [r4, #364]
	cmp	r3, r2
	bls	.L773
	ldrh	r2, [r4, #360]
	str	r3, [r4, #364]
	ldrh	r3, [r6, #8]
	strh	r2, [r4, #356]	@ movhi
	strh	r3, [r4, #360]	@ movhi
.L773:
	ldr	r7, .L794+4
	movs	r1, #1
	ldrh	r0, [r4, #356]
	movw	r8, #61649
	bl	FtlGetLastWrittenPage
	sxth	r5, r0
	adds	r0, r0, #1
	strh	r0, [r4, #358]	@ movhi
.L775:
	cmp	r5, #0
	bge	.L778
	movs	r2, #251
	ldr	r1, .L794+8
	ldr	r0, .L794+12
	bl	sftl_printk
.L777:
	ldrh	r3, [r6, #10]
	ldrh	r0, [r6, #12]
	strh	r3, [r4, #362]	@ movhi
	movw	r3, #65535
	cmp	r0, r3
	beq	.L780
	ldr	r2, [r4, #232]
	cmp	r0, r2
	beq	.L780
	ldrh	r3, [r4, #246]
	lsrs	r3, r3, #2
	cmp	r2, r3
	bcs	.L780
	cmp	r0, r3
	bcs	.L780
	bl	FtlSysBlkNumInit
.L780:
	ldr	r6, .L794+16
	movs	r5, #0
.L781:
	ldrh	r3, [r4, #258]
	cmp	r5, r3
	bcc	.L782
	movs	r0, #0
	pop	{r4, r5, r6, r7, r8, pc}
.L768:
	subs	r5, r5, #1
	uxth	r5, r5
	b	.L766
.L778:
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
	beq	.L776
	ldrh	r3, [r6]
	cmp	r3, r8
	beq	.L777
.L776:
	subs	r5, r5, #1
	sxth	r5, r5
	b	.L775
.L782:
	ldrh	r2, [r4, #3392]
	ldr	r1, [r4, #3464]
	ldr	r0, [r6, #4]!
	lsls	r2, r2, #2
	mla	r1, r5, r2, r1
	adds	r5, r5, #1
	bl	ftl_memcpy
	b	.L781
.L783:
	mov	r0, #-1
	pop	{r4, r5, r6, r7, r8, pc}
.L795:
	.align	2
.L794:
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
	ldr	r6, .L835
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
.L797:
	cmp	r10, fp
	bne	.L811
	ldr	r3, [sp, #12]
	cmp	r3, #0
	beq	.L805
	movs	r5, #0
	ldr	r7, .L835+4
	ldr	r8, .L835+28
	ldr	r9, .L835+32
	b	.L812
.L811:
	ldr	r3, [r5, #8]
	cbz	r3, .L798
	ldr	r3, [r5, #12]
	cbnz	r3, .L799
.L798:
	movs	r2, #148
	ldr	r1, .L835+8
	ldr	r0, .L835+12
	bl	sftl_printk
.L799:
	add	r2, sp, #20
	add	r1, sp, #24
	mov	r0, r5
	bl	l2p_addr_tran
	ldr	r7, [sp, #20]
	cmp	r7, #3
	bls	.L800
.L834:
	mov	r3, #-1
	str	r3, [r5]
	b	.L801
.L800:
	cbnz	r7, .L802
	ldr	r3, [sp, #24]
	ldr	r2, [sp, #4]
	cmp	r2, r3
	bls	.L802
	ldr	r5, .L835+8
	ldr	r6, .L835+16
	ldr	r8, .L835+36
	b	.L833
.L804:
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
	ldr	r0, .L835+20
	bl	rknand_print_hex
.L833:
	cmp	r7, fp
	add	r4, r4, #20
	bne	.L804
	bl	dump_stack
.L805:
	movs	r0, #0
	add	sp, sp, #52
	@ sp needed
	pop	{r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L802:
	ldr	r1, [r5, #8]
	lsls	r3, r1, #26
	beq	.L819
	ldr	r9, [r8, #3324]
	cmp	r1, r9
	beq	.L806
	ldrh	r2, [r8, #262]
	mov	r0, r9
	lsls	r2, r2, #9
	bl	ftl_memcpy
.L806:
	ldr	r3, [r5, #12]
	mov	r2, r9
	ldr	r1, [sp, #24]
	ldrb	r0, [sp, #20]	@ zero_extendqisi2
	ldr	r7, [r8, #3256]
	blx	r7
	cbnz	r0, .L807
	str	r0, [r5]
.L808:
	ldrh	r3, [r8, #14]
	cmp	r3, #4
	bne	.L801
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
	bne	.L834
.L801:
	add	r10, r10, #1
	adds	r5, r5, #20
	b	.L797
.L819:
	mov	r9, r1
	b	.L806
.L807:
	mov	r3, #-1
	str	r3, [r5]
	b	.L808
.L817:
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
	bne	.L813
	ldr	r1, [r4, #4]
	mov	r0, r7
	bl	sftl_printk
	str	fp, [r4]
.L813:
	ldr	fp, [sp, #28]
	cmp	fp, #256
	bne	.L814
	ldr	r1, [r4, #4]
	mov	r0, r8
	bl	sftl_printk
	str	fp, [r4]
.L814:
	ldr	r3, [r4, #12]
	cbz	r3, .L815
	ldr	r2, [r3]
	ldr	r3, [r6, #3332]
	ldr	r3, [r3]
	cmp	r2, r3
	beq	.L815
	ldr	r1, [r4, #4]
	mov	r0, r9
	bl	sftl_printk
	mov	r3, #-1
	str	r3, [r4]
.L815:
	ldr	r3, [r4, #8]
	cbz	r3, .L816
	ldr	r2, [r3]
	ldr	r3, [r6, #3324]
	ldr	r3, [r3]
	cmp	r2, r3
	beq	.L816
	ldr	r1, [r4, #4]
	ldr	r0, .L835+24
	bl	sftl_printk
	mov	r3, #-1
	str	r3, [r4]
.L816:
	adds	r5, r5, #1
	adds	r4, r4, #20
.L812:
	cmp	r10, r5
	bne	.L817
	b	.L805
.L836:
	.align	2
.L835:
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
	ldr	r4, .L882
	mov	r7, r1
	mov	r6, r10
	mov	r5, r10
	mov	r9, #20
	.pad #20
	sub	sp, sp, #20
	mov	fp, r4
	str	r0, [sp, #4]
	str	r0, [r4, #3288]
.L838:
	ldrh	r1, [r4, #236]
	uxth	r3, r10
	cmp	r1, r3
	bhi	.L843
	cmp	r6, #0
	beq	.L837
	mov	r8, #0
	mov	r9, #20
	mov	r2, r6
	movs	r1, #0
	ldr	r0, [r4, #3272]
	bl	FlashEraseBlocks
.L846:
	uxth	r3, r8
	cmp	r6, r3
	bhi	.L848
	cmp	r7, #0
	beq	.L865
	ldrh	r3, [r4, #308]
	mov	r10, #1
	str	r3, [sp, #8]
.L849:
	mov	r8, #0
	mov	fp, #20
.L859:
	mov	r9, #0
	mov	r6, r9
.L850:
	ldrh	r1, [r4, #236]
	uxth	r3, r9
	cmp	r1, r3
	bhi	.L854
	cbz	r6, .L837
	mov	r9, #0
	movs	r3, #1
	mov	r2, r10
	mov	r1, r6
	ldr	r0, [r4, #3272]
	bl	FlashProgPages
.L856:
	uxth	r3, r9
	cmp	r6, r3
	bhi	.L858
	add	r8, r8, #1
	ldr	r2, [sp, #8]
	uxth	r3, r8
	cmp	r2, r3
	bhi	.L859
	mov	r8, #0
	mov	r9, #20
.L860:
	uxth	r3, r8
	cmp	r6, r3
	bhi	.L862
	ldr	r3, [sp, #4]
	cmp	r3, #63
	bls	.L863
	cbz	r7, .L837
.L863:
	mov	r2, r6
	mov	r1, r10
	ldr	r0, [r4, #3272]
	bl	FlashEraseBlocks
.L837:
	mov	r0, r5
	add	sp, sp, #20
	@ sp needed
	pop	{r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L843:
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
	cbz	r7, .L839
	bl	IsBlkInVendorPart
	cbnz	r0, .L840
.L839:
	mov	r0, r8
	bl	FtlBbmIsBadBlock
	cbnz	r0, .L841
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
.L840:
	add	r10, r10, #1
	b	.L838
.L841:
	adds	r5, r5, #1
	uxth	r5, r5
	b	.L840
.L848:
	mul	r3, r9, r8
	ldr	r2, [r4, #3272]
	adds	r1, r2, r3
	ldr	r3, [r2, r3]
	adds	r3, r3, #1
	bne	.L847
	ldr	r0, [r1, #4]
	adds	r5, r5, #1
	uxth	r5, r5
	ubfx	r0, r0, #10, #16
	bl	FtlBbmMapBadBlock
.L847:
	add	r8, r8, #1
	b	.L846
.L865:
	movs	r3, #2
	mov	r10, r7
	str	r3, [sp, #8]
	b	.L849
.L854:
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
	cbz	r7, .L851
	bl	IsBlkInVendorPart
	cbnz	r0, .L852
.L851:
	ldr	r0, [sp, #12]
	bl	FtlBbmIsBadBlock
	cbnz	r0, .L852
	ldr	r1, [r4, #3272]
	ldr	r3, [sp, #12]
	ldr	r2, .L882
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
.L852:
	add	r9, r9, #1
	b	.L850
.L858:
	mul	r3, fp, r9
	ldr	r2, [r4, #3272]
	adds	r1, r2, r3
	ldr	r3, [r2, r3]
	cbz	r3, .L857
	ldr	r0, [r1, #4]
	adds	r5, r5, #1
	uxth	r5, r5
	ubfx	r0, r0, #10, #16
	bl	FtlBbmMapBadBlock
.L857:
	add	r9, r9, #1
	b	.L856
.L862:
	cbz	r7, .L861
	mul	r3, r9, r8
	ldr	r2, [r4, #3272]
	adds	r1, r2, r3
	ldr	r3, [r2, r3]
	cbnz	r3, .L861
	ldr	r0, [r1, #4]
	movs	r1, #1
	ubfx	r0, r0, #10, #16
	bl	FtlFreeSysBlkQueueIn
.L861:
	add	r8, r8, #1
	b	.L860
.L883:
	.align	2
.L882:
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
	bls	.L887
	ldr	r4, .L889
	add	r0, sp, #24
	movs	r2, #32
	movs	r1, #165
	str	r0, [sp, #16]
	lsls	r5, r5, #10
	ldr	r3, [r4, #3328]
	str	r3, [sp, #12]
	bl	ftl_memset
	movs	r2, #8
	movs	r1, #90
	ldr	r0, [r4, #3328]
	bl	ftl_memset
	movs	r2, #1
	add	r0, sp, #4
	mov	r1, r2
	str	r5, [sp, #8]
	bl	FlashEraseBlocks
	movs	r3, #1
	add	r0, sp, #4
	mov	r2, r3
	mov	r1, r3
	bl	FlashProgPages
	ldr	r3, [sp, #4]
	cbnz	r3, .L888
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
.L886:
	movs	r2, #1
	movs	r1, #0
	add	r0, sp, #4
	str	r5, [sp, #8]
	bl	FlashEraseBlocks
.L884:
	mov	r0, r4
	add	sp, sp, #92
	@ sp needed
	pop	{r4, r5, pc}
.L888:
	mov	r4, #-1
	b	.L886
.L887:
	movs	r4, #0
	b	.L884
.L890:
	.align	2
.L889:
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
	ldr	r4, .L903
	movs	r1, #0
	ldr	r0, [r4, #3304]
	add	r6, r4, #380
	ldr	r3, [r4, #3336]
	ldrh	r2, [r4, #314]
	str	r0, [r4, #3464]
	str	r3, [r4, #3468]
	bl	ftl_memset
.L892:
	ldrh	r3, [r4, #258]
	cmp	r5, r3
	blt	.L893
	ldr	r6, [r4, #3468]
	movs	r2, #16
	movs	r1, #255
	ldr	r8, .L903+12
	ldr	r9, .L903+16
	movs	r5, #0
	mov	r0, r6
	mov	r7, r5
	bl	ftl_memset
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
.L894:
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
	blt	.L895
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
.L895:
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
	bne	.L896
	adds	r5, r5, #1
	ldr	r1, [r4, #3460]
	uxth	r5, r5
	ldr	r0, .L903+4
	bl	sftl_printk
	cmp	r5, #3
	bls	.L894
	mov	r2, r5
	ldr	r1, [r4, #3460]
	ldr	r0, .L903+8
	bl	sftl_printk
.L898:
	b	.L898
.L893:
	ldrh	r2, [r4, #3392]
	ldr	r3, [r4, #3464]
	ldr	r1, [r6, #4]!
	mul	r0, r2, r5
	lsls	r2, r2, #2
	adds	r5, r5, #1
	add	r0, r3, r0, lsl #2
	bl	ftl_memcpy
	b	.L892
.L899:
	movs	r7, #1
	b	.L894
.L896:
	adds	r7, r7, #1
	cmp	r7, #1
	ble	.L899
	cmp	r3, #256
	beq	.L894
	movs	r0, #0
	add	sp, sp, #8
	@ sp needed
	pop	{r4, r5, r6, r7, r8, r9, r10, pc}
.L904:
	.align	2
.L903:
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
	ldr	r4, .L941
	.pad #20
	sub	sp, sp, #20
	ldr	r9, .L941+20
.L906:
	ldr	r3, .L941
	ldr	r2, .L941
	ldrh	r3, [r3, #228]
	ldrh	r2, [r2, #2532]
	add	r3, r3, r2
	ldr	r2, .L941
	ldrh	r2, [r2, #244]
	cmp	r3, r2
	ble	.L907
	mov	r2, #2656
	mov	r1, r9
	ldr	r0, .L941+4
	bl	sftl_printk
.L907:
	ldr	r3, .L941+8
	cmp	r5, r3
	bne	.L933
	ldrh	r3, [r4, #228]
	ldr	r1, [r4, #3168]
	mul	r2, r1, r3
	lsrs	r1, r3, #1
	adds	r1, r1, #1
	add	r1, r1, r2, lsr #2
	uxth	r1, r1
	cbz	r1, .L908
	subs	r1, r1, #1
	uxth	r1, r1
.L908:
	ldr	r0, .L941+12
	bl	List_pop_index_node
	ldrh	r3, [r4, #228]
	mov	r6, r0
	uxth	r8, r0
	cbnz	r3, .L909
	movw	r2, #2665
	mov	r1, r9
	ldr	r0, .L941+4
	bl	sftl_printk
.L909:
	ldrh	r3, [r4, #228]
	subs	r3, r3, #1
	strh	r3, [r4, #228]	@ movhi
	ldrh	r3, [r4, #244]
	cmp	r3, r8
	bls	.L906
	ldr	r3, [r4, #76]
	uxth	r6, r6
	ldrh	r7, [r3, r6, lsl #1]
	cmp	r7, #0
	bne	.L906
	strh	r8, [r5]	@ movhi
	mov	r0, r5
	bl	make_superblock
	ldrb	r3, [r5, #7]	@ zero_extendqisi2
	cbnz	r3, .L911
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
	ble	.L906
	movw	r2, #2679
	mov	r1, r9
	ldr	r0, .L941+4
	bl	sftl_printk
	b	.L906
.L933:
	movs	r1, #0
	b	.L908
.L911:
	ldrh	r2, [r4, #2532]
	ldrh	r3, [r4, #228]
	add	r3, r3, r2
	ldrh	r2, [r4, #244]
	cmp	r3, r2
	ble	.L913
	movw	r2, #2682
	mov	r1, r9
	ldr	r0, .L941+4
	bl	sftl_printk
.L913:
	ldr	r1, [r4, #3272]
	mov	r10, #20
	ldrh	r2, [r4, #236]
	add	fp, r5, #16
	mov	r0, fp
	mov	ip, #0
	mov	r3, r1
	mla	r2, r10, r2, r1
	str	r2, [sp, #4]
.L914:
	ldr	r2, [sp, #4]
	cmp	r2, r3
	bne	.L916
	cbnz	r7, .L917
	movw	r2, #2693
	mov	r1, r9
	ldr	r0, .L941+4
	bl	sftl_printk
.L917:
	ldrh	r3, [r4, #176]
	cmp	r3, r8
	bne	.L918
	movw	r2, #2695
	mov	r1, r9
	ldr	r0, .L941+4
	bl	sftl_printk
.L918:
	ldrb	r3, [r5, #8]	@ zero_extendqisi2
	ldr	r2, [r4, #2540]
	cmp	r3, #0
	bne	.L919
	ldrh	r3, [r2, r6, lsl #1]
	cmp	r3, #0
	beq	.L920
	ldrh	r1, [r4, #296]
	add	r3, r3, r1
.L940:
	strh	r3, [r2, r6, lsl #1]	@ movhi
	movs	r1, #0
	ldr	r3, [r4, #2600]
	mov	r0, r8
	adds	r3, r3, #1
	str	r3, [r4, #2600]
	bl	ftl_set_blk_mode
.L922:
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
	ldr	r0, .L941+16
	adds	r3, r3, #1
	str	r3, [r2, #16]
	movs	r2, #20
	mla	r2, r2, r7, r1
	adds	r3, r1, #4
	adds	r2, r2, #24
.L924:
	adds	r3, r3, #20
	cmp	r2, r3
	bne	.L925
	ldrb	r1, [r5, #8]	@ zero_extendqisi2
	mov	r2, r7
	ldr	r0, [r4, #3272]
	mov	r10, #0
	bl	FlashEraseBlocks
	mov	r3, r10
	movs	r1, #20
.L926:
	uxth	r2, r10
	cmp	r7, r2
	bhi	.L928
	cmp	r3, #0
	ble	.L929
	mov	r0, r8
	bl	update_multiplier_value
	bl	FtlBbmTblFlush
.L929:
	ldrb	r2, [r5, #7]	@ zero_extendqisi2
	cmp	r2, #0
	bne	.L930
	ldr	r3, [r4, #76]
	movw	r2, #65535
	strh	r2, [r3, r6, lsl #1]	@ movhi
	b	.L906
.L916:
	str	ip, [r3, #8]
	movw	lr, #65535
	str	ip, [r3, #12]
	ldrh	r2, [r0], #2
	cmp	r2, lr
	beq	.L915
	mla	lr, r10, r7, r1
	adds	r7, r7, #1
	lsls	r2, r2, #10
	uxth	r7, r7
	str	r2, [lr, #4]
.L915:
	adds	r3, r3, #20
	b	.L914
.L920:
	movs	r3, #2
	b	.L940
.L919:
	ldrh	r3, [r2, r6, lsl #1]
	mov	r0, r8
	adds	r3, r3, #1
	strh	r3, [r2, r6, lsl #1]	@ movhi
	ldr	r3, [r4, #2604]
	adds	r3, r3, #1
	str	r3, [r4, #2604]
	bl	ftl_set_blk_mode.part.6
	b	.L922
.L925:
	ldr	r1, [r3, #-20]
	ands	r1, r1, r0
	str	r1, [r3, #-20]
	b	.L924
.L928:
	mul	r2, r1, r10
	ldr	r0, [r4, #3272]
	add	ip, r0, r2
	ldr	r2, [r0, r2]
	adds	r0, r2, #1
	bne	.L927
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
.L927:
	add	r10, r10, #1
	add	fp, fp, #2
	b	.L926
.L930:
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
	cbz	r3, .L931
	ldrb	r3, [r5, #7]	@ zero_extendqisi2
	cbnz	r3, .L932
.L931:
	movw	r2, #2748
	ldr	r1, .L941+20
	ldr	r0, .L941+4
	bl	sftl_printk
.L932:
	movs	r0, #0
	add	sp, sp, #20
	@ sp needed
	pop	{r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L942:
	.align	2
.L941:
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
	ldr	r4, .L955
	ldrh	r3, [r4, #3210]
	cbz	r3, .L944
	movs	r7, #0
	addw	r10, r4, #3212
.L945:
	ldrh	r2, [r4, #236]
	uxth	r3, r7
	cmp	r2, r3
	bhi	.L951
	bl	FtlGcReFreshBadBlk
.L944:
	movs	r0, #0
	pop	{r3, r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L951:
	uxtah	r3, r4, r7
	mov	r1, r8
	mov	fp, #0
	ldrb	r0, [r3, #264]	@ zero_extendqisi2
	bl	V2P_block
	mov	r9, r0
.L946:
	ldrh	r3, [r4, #3210]
	uxth	r5, fp
	cmp	r3, r5
	bhi	.L950
	adds	r7, r7, #1
	b	.L945
.L950:
	uxth	r6, fp
	add	r3, r4, r6, lsl #1
	ldrh	r3, [r3, #3212]
	cmp	r3, r9
	bne	.L947
	mov	r1, r9
	ldr	r0, .L955+4
	bl	sftl_printk
	mov	r0, r9
	bl	FtlBbmMapBadBlock
	bl	FtlBbmTblFlush
	ldrh	r1, [r4, #3210]
	add	r3, r10, r6, lsl #1
.L948:
	cmp	r5, r1
	bcc	.L949
	subs	r1, r1, #1
	strh	r1, [r4, #3210]	@ movhi
.L947:
	add	fp, fp, #1
	b	.L946
.L949:
	ldrh	r0, [r3, #2]!
	adds	r5, r5, #1
	uxth	r5, r5
	strh	r0, [r3, #-2]	@ movhi
	b	.L948
.L956:
	.align	2
.L955:
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
	ldr	r4, .L969
	ldr	r3, [r4, #76]
	ldrh	r3, [r3, r0, lsl #1]
	cmp	r3, #0
	bne	.L958
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
	bne	.L961
	movw	r3, #65535
	strh	r3, [r4, #176]	@ movhi
.L962:
	mov	r1, r5
	ldr	r0, .L969+4
	bl	List_remove_node
	ldrh	r3, [r4, #2532]
	cbnz	r3, .L964
	movw	r2, #2824
	ldr	r1, .L969+8
	ldr	r0, .L969+12
	bl	sftl_printk
.L964:
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
	ble	.L968
	movw	r2, #2827
	ldr	r1, .L969+8
	ldr	r0, .L969+12
	bl	sftl_printk
.L968:
	movs	r0, #1
	pop	{r3, r4, r5, pc}
.L961:
	ldrh	r3, [r4, #28]
	cmp	r3, r0
	beq	.L967
	ldrh	r3, [r4, #80]
	cmp	r3, r0
	beq	.L967
	ldrh	r3, [r4, #128]
	cmp	r3, r0
	bne	.L962
.L967:
	movs	r0, #0
	pop	{r3, r4, r5, pc}
.L958:
	bl	List_update_data_list
	b	.L967
.L970:
	.align	2
.L969:
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
	ldr	r5, .L980
	beq	.L972
	ldr	r3, [r5, #76]
	ldrh	r2, [r3, r0, lsl #1]
	cbnz	r2, .L973
	mov	r1, r0
	ldr	r0, .L980+4
	bl	sftl_printk
	ldr	r3, [r5, #76]
	ldrh	r6, [r3, r4, lsl #1]
	cbz	r6, .L974
.L978:
	movs	r6, #0
.L971:
	mov	r0, r6
	pop	{r4, r5, r6, pc}
.L974:
	movw	r2, #2842
.L979:
	ldr	r1, .L980+8
	ldr	r0, .L980+12
	bl	sftl_printk
	b	.L971
.L973:
	subs	r2, r2, #1
	strh	r2, [r3, r0, lsl #1]	@ movhi
.L972:
	ldrh	r0, [r5, #3442]
	movw	r3, #65535
	cmp	r0, r3
	bne	.L976
	strh	r4, [r5, #3442]	@ movhi
	b	.L978
.L976:
	cmp	r4, r0
	beq	.L978
	bl	update_vpc_list
	ldr	r2, [r5, #2520]
	adds	r6, r0, #0
	ldr	r3, [r5, #2524]
	it	ne
	movne	r6, #1
	strh	r4, [r5, #3442]	@ movhi
	subs	r3, r3, r2
	asrs	r2, r3, #1
	ldr	r3, .L980+16
	muls	r3, r2, r3
	ldr	r2, [r5, #76]
	uxth	r1, r3
	ldrh	r2, [r2, r1, lsl #1]
	cmp	r2, #0
	bne	.L971
	cmp	r4, r1
	beq	.L971
	movw	r2, #2858
	b	.L979
.L981:
	.align	2
.L980:
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
	ldr	r5, .L995
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
.L988:
	ldrh	r3, [r4, #4]
	cbnz	r3, .L984
.L985:
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
.L984:
	mov	r0, r4
	bl	get_new_active_ppa
	str	r0, [sp, #8]
	adds	r0, r0, #1
	beq	.L985
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
	bne	.L988
	b	.L985
.L996:
	.align	2
.L995:
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
	ldr	r9, .L1016+4
	bl	FtlBbtMemInit
	sub	r5, r9, #384
	sub	r10, r9, #18
	mov	r4, r5
	bl	FtlLoadFactoryBbt
.L998:
	ldrh	r3, [r5, #258]
	cmp	r8, r3
	bcc	.L1004
	movs	r5, #0
.L1005:
	ldrh	r3, [r4, #318]
	uxth	r0, r5
	adds	r5, r5, #1
	cmp	r3, r0
	bhi	.L1006
	ldrh	r5, [r4, #368]
	movw	r6, #65535
	subs	r5, r5, #1
	uxth	r5, r5
.L1007:
	ldrh	r3, [r4, #368]
	subs	r3, r3, #48
	cmp	r5, r3
	ble	.L1011
	mov	r0, r5
	bl	FtlBbmIsBadBlock
	cmp	r0, #1
	beq	.L1008
	mov	r0, r5
	bl	FlashTestBlk
	cmp	r0, #0
	beq	.L1009
	mov	r0, r5
	bl	FtlBbmMapBadBlock
.L1008:
	subs	r5, r5, #1
	uxth	r5, r5
	b	.L1007
.L1004:
	ldrh	r3, [r10, #2]!
	movw	r2, #65535
	ldr	r0, [r5, #3304]
	ldr	fp, [r5, #3336]
	cmp	r3, r2
	str	r0, [r5, #3464]
	str	fp, [r5, #3468]
	beq	.L999
	ldrh	r7, [r5, #302]
	movs	r2, #1
	mov	r1, r2
	ldr	r0, .L1016
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
.L1000:
	uxth	r0, r7
	add	r8, r8, #1
	add	r9, r9, #4
	bl	FtlBbmMapBadBlock
	b	.L998
.L999:
	mov	r1, r8
	bl	FlashGetBadBlockList
	ldr	r1, [r9]
	ldr	r0, [r5, #3464]
	bl	FtlBbt2Bitmap
	ldrh	r6, [r5, #302]
.L1002:
	subs	r6, r6, #1
	uxth	r6, r6
.L1001:
	ldrh	r0, [r4, #302]
	smlabb	r0, r0, r8, r6
	uxth	r0, r0
	bl	FtlBbmIsBadBlock
	cmp	r0, #1
	beq	.L1002
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
	ldr	r0, .L1016
	mov	r1, r2
	bl	FlashEraseBlocks
	movs	r3, #1
	ldr	r0, .L1016
	mov	r2, r3
	mov	r1, r3
	bl	FlashProgPages
	ldr	r3, [r4, #3456]
	adds	r3, r3, #1
	bne	.L1000
	uxth	r0, r7
	bl	FtlBbmMapBadBlock
	b	.L1001
.L1006:
	bl	FtlBbmMapBadBlock
	b	.L1005
.L1009:
	ldrh	r3, [r4, #356]
	cmp	r3, r6
	bne	.L1010
	strh	r5, [r4, #356]	@ movhi
	b	.L1008
.L1010:
	strh	r5, [r4, #360]	@ movhi
.L1011:
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
.L1017:
	.align	2
.L1016:
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
	ldr	r3, .L1022
	add	r1, r1, r0
	push	{r4, lr}
	.save {r4, lr}
.L1020:
	cmp	r0, r1
	bne	.L1021
	mov	r0, r3
	pop	{r4, pc}
.L1021:
	lsrs	r2, r3, #2
	ldrb	r4, [r0], #1	@ zero_extendqisi2
	add	r2, r2, r3, lsl #5
	add	r2, r2, r4
	eors	r3, r3, r2
	b	.L1020
.L1023:
	.align	2
.L1022:
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
	bne	.L1025
	ldrh	r3, [r0, #8]
	cbz	r3, .L1026
	movw	r2, #641
	ldr	r1, .L1034
	ldr	r0, .L1034+4
	bl	sftl_printk
.L1026:
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
.L1027:
	movs	r0, #0
	pop	{r4, r5, r6, r7, r8, pc}
.L1025:
	ldrh	r7, [r5, r3, lsl #1]
	movs	r1, #255
	ldrh	r3, [r0, #2]
	ldr	r5, .L1034+8
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
.L1028:
	ldrh	r0, [r4, #6]
	uxth	r1, r2
	cmp	r0, r1
	bhi	.L1030
	ldrh	r1, [r5, #314]
	ldr	r0, [r5, #3464]
	bl	js_hash
	movs	r2, #1
	str	r0, [r6, #12]
	movs	r3, #0
	mov	r1, r2
	ldr	r0, .L1034+12
	bl	FlashProgPages
	ldrh	r3, [r4, #2]
	mov	r0, r4
	adds	r3, r3, #1
	strh	r3, [r4, #2]	@ movhi
	bl	ftl_map_blk_gc
	b	.L1027
.L1030:
	uxth	r0, r2
	ldr	r1, [r8, r0, lsl #2]
	cmp	r7, r1, lsr #10
	bne	.L1029
	ldr	r1, [r5, #3304]
	adds	r3, r3, #1
	uxth	r3, r3
	str	r0, [r1, r3, lsl #3]
	ldr	r1, [r5, #3304]
	ldr	r0, [r8, r0, lsl #2]
	add	r1, r1, r3, lsl #3
	str	r0, [r1, #4]
.L1029:
	adds	r2, r2, #1
	b	.L1028
.L1035:
	.align	2
.L1034:
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
	ldr	r7, .L1060
	mov	r8, r1
	movs	r6, #0
	ldr	r9, .L1060+20
	mov	r5, r7
	str	r2, [sp, #4]
.L1037:
	ldr	r3, [r7, #2576]
	adds	r3, r3, #1
	str	r3, [r7, #2576]
	ldrh	r3, [r7, #308]
	ldrh	r2, [r4, #2]
	subs	r3, r3, #1
	cmp	r2, r3
	bge	.L1038
	ldrh	r2, [r4]
	movw	r3, #65535
	cmp	r2, r3
	bne	.L1039
.L1038:
	mov	r0, r4
	bl	Ftl_write_map_blk_to_last_page
.L1039:
	ldrh	r2, [r4]
	ldr	r3, [r4, #12]
	ldrh	r3, [r3, r2, lsl #1]
	cbnz	r3, .L1040
	mov	r2, #700
	mov	r1, r9
	ldr	r0, .L1060+4
	bl	sftl_printk
.L1040:
	ldrh	r2, [r4]
	ldrh	r3, [r4, #10]
	cmp	r2, r3
	bcc	.L1041
	movw	r2, #701
	mov	r1, r9
	ldr	r0, .L1060+4
	bl	sftl_printk
.L1041:
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
	ldrh	r1, [r5, #314]
	ldr	r0, [r5, #3464]
	bl	js_hash
	movs	r3, #1
	str	r0, [fp, #12]
	mov	r2, r3
	mov	r1, r3
	ldr	r0, .L1060+8
	bl	FlashProgPages
	ldrh	r3, [r4, #2]
	adds	r3, r3, #1
	uxth	r3, r3
	strh	r3, [r4, #2]	@ movhi
	ldr	r2, [r5, #3456]
	adds	r1, r2, #1
	bne	.L1042
	ldr	r1, [r5, #3460]
	adds	r6, r6, #1
	ldr	r0, .L1060+12
	uxth	r6, r6
	bl	sftl_printk
	ldrh	r3, [r4, #2]
	cmp	r3, #2
	ittt	ls
	ldrhls	r3, [r5, #308]
	addls	r3, r3, #-1
	strhls	r3, [r4, #2]	@ movhi
	cmp	r6, #3
	bls	.L1044
	mov	r2, r6
	ldr	r1, [r5, #3460]
	ldr	r0, .L1060+16
	bl	sftl_printk
.L1045:
	b	.L1045
.L1044:
	ldr	r3, [r4, #32]
	cmp	r3, #0
	beq	.L1037
.L1059:
	b	.L1059
.L1042:
	cmp	r3, #1
	beq	.L1048
	cmp	r2, #256
	beq	.L1048
	ldr	r0, [r4, #36]
	cbz	r0, .L1049
.L1048:
	movs	r3, #0
	str	r3, [r4, #36]
	b	.L1037
.L1049:
	ldr	r2, [r5, #3460]
	ldr	r3, [r4, #24]
	str	r2, [r3, r8, lsl #2]
	add	sp, sp, #12
	@ sp needed
	pop	{r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L1061:
	.align	2
.L1060:
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
	ldr	r4, .L1069
	mov	r8, r1
	ldrh	r3, [r4, #340]
	cmp	r3, r0
	bcs	.L1063
	movw	r2, #485
	ldr	r1, .L1069+4
	ldr	r0, .L1069+8
	bl	sftl_printk
.L1063:
	ldr	r3, [r4, #3380]
	movs	r5, #12
	ldr	r7, [r3, r6, lsl #2]
	cbnz	r7, .L1064
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
.L1065:
	movs	r0, #0
	pop	{r4, r5, r6, r7, r8, pc}
.L1064:
	mul	r5, r5, r8
	ldr	r3, [r4, #2544]
	movs	r2, #1
	ldr	r0, .L1069+12
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
	beq	.L1066
	mov	r2, r7
	mov	r1, r6
	ldr	r0, .L1069+16
	bl	sftl_printk
	movs	r3, #4
	ldr	r1, [r4, #3468]
	mov	r2, r3
	ldr	r0, .L1069+20
	bl	rknand_print_hex
	ldrh	r3, [r4, #340]
	movs	r2, #4
	ldr	r1, [r4, #3380]
	ldr	r0, .L1069+24
	bl	rknand_print_hex
.L1067:
	ldrh	r3, [r8, #8]
	cmp	r3, r6
	beq	.L1068
	mov	r2, #508
	ldr	r1, .L1069+4
	ldr	r0, .L1069+8
	bl	sftl_printk
.L1068:
	ldr	r3, [r4, #2544]
	movs	r1, #0
	adds	r2, r3, r5
	str	r1, [r2, #4]
	strh	r6, [r3, r5]	@ movhi
	b	.L1065
.L1066:
	ldr	r3, [r4, #3456]
	cmp	r3, #256
	bne	.L1067
	mov	r2, r7
	mov	r1, r6
	ldr	r0, .L1069+28
	bl	sftl_printk
	ldr	r3, [r4, #2544]
	mov	r1, r6
	ldr	r0, .L1069+32
	add	r3, r3, r5
	ldr	r2, [r3, #8]
	bl	FtlMapWritePage
	b	.L1067
.L1070:
	.align	2
.L1069:
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
	ldr	r5, .L1087
	subs	r3, r3, #5
	cmp	r2, r3
	blt	.L1072
	uxth	r0, r0
	ldrh	r9, [r6, r0, lsl #1]
	cmp	r9, #0
	beq	.L1072
	ldr	r3, [r4, #32]
	cbnz	r3, .L1072
	movs	r2, #1
	str	r2, [r4, #32]
	strh	r3, [r6, r0, lsl #1]	@ movhi
	ldrh	r3, [r4, #8]
	ldrh	r2, [r4, #2]
	subs	r3, r3, #1
	strh	r3, [r4, #8]	@ movhi
	ldrh	r3, [r5, #308]
	cmp	r2, r3
	bcc	.L1073
	mov	r0, r4
	bl	ftl_map_blk_alloc_new_blk
.L1073:
	movs	r6, #0
.L1074:
	ldrh	r2, [r4, #6]
	uxth	fp, r6
	cmp	r2, fp
	bhi	.L1081
	movs	r1, #1
	mov	r0, r9
	bl	FtlFreeSysBlkQueueIn
	movs	r3, #0
	str	r3, [r4, #32]
.L1072:
	ldrh	r2, [r4, #2]
	ldrh	r3, [r5, #308]
	cmp	r2, r3
	bcc	.L1082
	mov	r0, r4
	bl	ftl_map_blk_alloc_new_blk
.L1082:
	movs	r0, #0
	add	sp, sp, #12
	@ sp needed
	pop	{r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L1081:
	uxth	r7, r6
	ldr	r2, [r10, r7, lsl #2]
	add	r3, r10, r7, lsl #2
	str	r3, [sp, #4]
	cmp	r9, r2, lsr #10
	bne	.L1075
	ldr	r2, [r5, #3308]
	ldr	r8, [r5, #3336]
	ldr	r0, .L1087+4
	str	r2, [r5, #3464]
	str	r8, [r5, #3468]
	ldr	r2, [r10, r7, lsl #2]
	str	r2, [r5, #3460]
	movs	r2, #1
	mov	r1, r2
	bl	FlashReadPages
	ldrh	r2, [r8, #8]
	cmp	r2, fp
	beq	.L1076
	movw	r2, #611
	ldr	r1, .L1087+8
	ldr	r0, .L1087+12
	bl	sftl_printk
.L1076:
	ldr	r2, [r5, #3456]
	adds	r2, r2, #1
	bne	.L1077
.L1079:
	ldr	r2, [sp, #4]
	movs	r3, #0
	str	r3, [r2]
.L1078:
	b	.L1078
.L1077:
	ldrh	r2, [r8, #8]
	cmp	r2, fp
	bne	.L1079
	ldrh	r2, [r8]
	ldrh	r3, [r4, #4]
	cmp	r2, r3
	bne	.L1079
	ldr	r2, [r5, #3464]
	mov	r1, r7
	mov	r0, r4
	bl	FtlMapWritePage
.L1075:
	adds	r6, r6, #1
	b	.L1074
.L1088:
	.align	2
.L1087:
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
	ldr	r5, .L1090
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
.L1091:
	.align	2
.L1090:
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
	ldr	r4, .L1105
	mov	r5, r1
	mov	r7, r2
	ldr	r3, [r4, #2556]
	ldrh	r10, [r4, #312]
	cmp	r0, r3
	bcc	.L1093
	movw	r2, #813
	ldr	r1, .L1105+4
	ldr	r0, .L1105+8
	bl	sftl_printk
.L1093:
	ldr	r3, [r4, #2556]
	cmp	r9, r3
	bcs	.L1094
	add	r10, r10, #7
	ldrh	r2, [r4, #342]
	lsr	r6, r9, r10
	ldr	r1, [r4, #2544]
	movs	r3, #0
	uxth	r6, r6
	mov	fp, #12
.L1095:
	uxth	r8, r3
	cmp	r8, r2
	bcc	.L1100
	bl	select_l2p_ram_region
	mul	fp, fp, r0
	ldr	r3, [r4, #2544]
	mov	r8, r0
	ldrh	r1, [r3, fp]
	add	r2, r3, fp
	movw	r3, #65535
	cmp	r1, r3
	beq	.L1101
	ldr	r3, [r2, #4]
	cmp	r3, #0
	bge	.L1101
	bl	flush_l2p_region
.L1101:
	mov	r1, r8
	mov	r0, r6
	bl	load_l2p_region
	b	.L1097
.L1094:
	mov	r0, #-1
	cbnz	r7, .L1092
	str	r0, [r5]
	pop	{r3, r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L1100:
	adds	r3, r3, #1
	mla	r0, fp, r3, r1
	ldrh	r0, [r0, #-12]
	cmp	r0, r6
	bne	.L1095
.L1097:
	movs	r0, #1
	movs	r3, #12
	lsl	r0, r0, r10
	subs	r0, r0, #1
	and	r0, r0, r9
	uxth	r0, r0
	cbnz	r7, .L1098
	ldr	r2, [r4, #2544]
	mla	r3, r3, r8, r2
	ldr	r3, [r3, #8]
	ldr	r3, [r3, r0, lsl #2]
	str	r3, [r5]
.L1099:
	ldr	r2, [r4, #2544]
	movs	r3, #12
	mla	r8, r3, r8, r2
	ldr	r3, [r8, #4]
	adds	r2, r3, #1
	beq	.L1103
	adds	r3, r3, #1
	str	r3, [r8, #4]
.L1103:
	movs	r0, #0
.L1092:
	pop	{r3, r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L1098:
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
	b	.L1099
.L1106:
	.align	2
.L1105:
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
	ldr	r4, .L1125
	ldrh	r2, [r4, #32]
	cmp	r2, #0
	beq	.L1108
	ldrb	r3, [r4, #36]	@ zero_extendqisi2
	cmp	r3, #0
	bne	.L1108
	ldrb	r1, [r4, #35]	@ zero_extendqisi2
	ldrh	r3, [r4, #306]
	muls	r3, r1, r3
	cmp	r2, r3
	beq	.L1108
	ldrb	r7, [r4, #38]	@ zero_extendqisi2
	cbnz	r7, .L1107
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
	beq	.L1110
	mov	r2, r7
	movs	r1, #1
	add	r0, sp, #4
	bl	FlashReadPages
.L1111:
	ldr	r9, .L1125+4
	movs	r7, #0
	lsl	r8, r8, #2
	mov	r10, r7
	movw	r3, #61589
	strh	r3, [r5]	@ movhi
.L1112:
	cmp	r8, r7
	bne	.L1116
.L1113:
	movs	r3, #1
.L1124:
	strb	r3, [r4, #38]
.L1107:
	add	sp, sp, #24
	@ sp needed
	pop	{r4, r5, r6, r7, r8, r9, r10, pc}
.L1110:
	ldrh	r2, [r4, #314]
	movs	r1, #255
	bl	ftl_memset
	b	.L1111
.L1116:
	ldrh	r3, [r4, #32]
	cmp	r3, #0
	beq	.L1113
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
	b	.L1112
.L1108:
	movs	r3, #0
	b	.L1124
.L1126:
	.align	2
.L1125:
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
	ldr	r5, .L1137
	ubfx	r0, r1, #10, #16
	str	r1, [sp, #4]
	bl	P2V_block_in_plane
	ldr	r2, [r5, #76]
	mov	r7, r0
	ldrh	r3, [r2, r0, lsl #1]
	cbnz	r3, .L1128
	ldr	r4, [r5, #2536]
	cbz	r4, .L1129
	ldr	r1, [r5, #2520]
	mov	ip, #6
	ldr	r2, .L1137+4
	movw	lr, #65535
	ldrh	r0, [r5, #228]
	subs	r4, r4, r1
	asrs	r4, r4, #1
	muls	r4, r2, r4
	uxth	r4, r4
.L1130:
	uxth	r2, r3
	cmp	r0, r2
	bls	.L1129
	cmp	r4, r7
	bne	.L1131
	mov	r1, r4
	ldr	r0, .L1137+8
	bl	List_remove_node
	ldrh	r3, [r5, #228]
	cbnz	r3, .L1132
	movw	r2, #1733
	ldr	r1, .L1137+12
	ldr	r0, .L1137+16
	bl	sftl_printk
.L1132:
	ldrh	r3, [r5, #228]
	mov	r0, r4
	subs	r3, r3, #1
	strh	r3, [r5, #228]	@ movhi
	bl	INSERT_DATA_LIST
	ldr	r2, [r5, #76]
	ldrh	r3, [r2, r7, lsl #1]
.L1128:
	adds	r3, r3, #1
	strh	r3, [r2, r7, lsl #1]	@ movhi
	b	.L1129
.L1131:
	mul	r4, ip, r4
	adds	r3, r3, #1
	ldrh	r4, [r1, r4]
	cmp	r4, lr
	bne	.L1130
.L1129:
	movs	r2, #1
	add	r1, sp, #4
	mov	r0, r6
	bl	log2phys
	add	sp, sp, #12
	@ sp needed
	pop	{r4, r5, r6, r7, pc}
.L1138:
	.align	2
.L1137:
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
	beq	.L1267
	ldrh	r3, [r0, #2]
	ldr	r4, .L1275
	str	r3, [sp]
	ldrb	r3, [r0, #6]	@ zero_extendqisi2
	ldr	r1, [sp]
	str	r3, [sp, #20]
	ldrh	r3, [r4, #306]
	cmp	r3, r1
	mov	r3, #0
	bne	.L1142
	strh	r3, [r0, #4]	@ movhi
.L1273:
	strb	r3, [r10, #6]
.L1267:
	movs	r0, #0
	add	sp, sp, #52
	@ sp needed
	pop	{r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L1142:
	ldrh	r0, [r0, #16]
.L1143:
	cmp	r0, r2
	uxth	r6, r3
	add	r3, r3, #1
	beq	.L1144
	movs	r1, #1
	bl	FtlGetLastWrittenPage
	adds	r7, r0, #1
	mov	r5, r0
	beq	.L1145
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
.L1146:
	uxth	r3, r2
	cmp	fp, r3
	bhi	.L1152
	ldr	r6, .L1275
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
.L1153:
	uxth	r2, r3
	cmp	r4, r2
	bhi	.L1158
	bne	.L1156
	adds	r3, r5, #1
	uxth	r3, r3
	str	r3, [sp, #8]
.L1268:
	ldr	r0, [r8, #4]
	ubfx	r0, r0, #10, #16
	bl	P2V_plane
	ldrh	r3, [r6, #306]
	ldr	r2, [sp, #8]
	str	r0, [sp, #4]
	cmp	r3, r2
	bne	.L1160
	ldrh	r3, [sp, #8]
	strh	r3, [r10, #2]	@ movhi
	movs	r3, #0
	strb	r3, [r10, #6]
	strh	r3, [r10, #4]	@ movhi
.L1160:
	ldr	r3, [sp, #8]
	ldr	r2, [sp]
	cmp	r3, r2
	bne	.L1161
	ldr	r3, [sp, #4]
	ldr	r2, [sp, #20]
	cmp	r3, r2
	bne	.L1161
	ldr	r1, [sp, #8]
	mov	r2, r3
.L1274:
	mov	r0, r10
	bl	ftl_sb_update_avl_pages
	b	.L1267
.L1144:
	uxth	r1, r3
	adds	r1, r1, #8
	ldrh	r0, [r10, r1, lsl #1]
	b	.L1143
.L1145:
	ldr	r3, [sp]
	cbz	r3, .L1147
	mov	r2, #1800
	ldr	r1, .L1275+4
	ldr	r0, .L1275+8
	bl	sftl_printk
.L1147:
	ldr	r3, [sp, #20]
	cbz	r3, .L1148
	cmp	r6, r3
	beq	.L1148
	movw	r2, #1801
	ldr	r1, .L1275+4
	ldr	r0, .L1275+8
	bl	sftl_printk
.L1148:
	movs	r3, #0
	strh	r3, [r10, #2]	@ movhi
	b	.L1273
.L1152:
	ldrh	r3, [ip], #2
	cmp	r3, r9
	beq	.L1149
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
.L1149:
	adds	r2, r2, #1
	b	.L1146
.L1158:
	ldr	r2, [r7]
	cbnz	r2, .L1154
	ldr	r2, [r7, #12]
	str	r2, [sp, #4]
	ldr	r2, [r2, #4]
	adds	r0, r2, #1
	beq	.L1155
	ldr	r1, [r6, #2596]
	mov	r0, r2
	bl	ftl_cmp_data_ver
	cbz	r0, .L1155
	adds	r2, r2, #1
	str	r2, [r6, #2596]
.L1155:
	ldr	r2, [sp, #4]
	ldr	r2, [r2]
	adds	r2, r2, #1
	bne	.L1157
.L1156:
	uxth	r2, r5
	uxth	r3, r3
	str	r2, [sp, #8]
	movs	r2, #20
	mla	r8, r2, r3, r8
	b	.L1268
.L1154:
	ldr	r9, [sp, #8]
.L1157:
	adds	r3, r3, #1
	adds	r7, r7, #20
	b	.L1153
.L1161:
	movw	r3, #65535
	cmp	r9, r3
	bne	.L1162
	ldrb	r3, [r10, #8]	@ zero_extendqisi2
	cmp	r3, #0
	bne	.L1163
.L1162:
	ldr	r3, [r6, #3452]
	uxth	r7, r5
	uxth	r5, r5
	movw	r8, #65535
	adds	r3, r3, #1
	ldr	r3, [sp]
	it	eq
	streq	fp, [r6, #3452]
	ldr	r6, .L1275
	adds	r3, r3, #7
	cmp	r5, r3
	itet	gt
	subgt	r4, r7, #7
	ldrle	r4, [sp]
	uxthgt	r4, r4
.L1166:
	cmp	r4, r7
	bhi	.L1173
	movs	r3, #0
	ldrh	lr, [r6, #236]
	ldr	r0, [r6, #3292]
	mov	r5, r3
	ldr	r1, [sp, #16]
	mov	ip, #20
	b	.L1174
.L1168:
	ldrh	r2, [r1], #2
	cmp	r2, r8
	beq	.L1167
	mla	r9, ip, r5, r0
	adds	r5, r5, #1
	orr	r2, r4, r2, lsl #10
	uxth	r5, r5
	str	r2, [r9, #4]
.L1167:
	adds	r3, r3, #1
.L1174:
	uxth	r2, r3
	cmp	lr, r2
	bhi	.L1168
	movs	r2, #0
	mov	r1, r5
	bl	FlashReadPages
	ldr	r3, [r6, #3292]
	movs	r2, #20
	mla	r5, r2, r5, r3
.L1169:
	cmp	r5, r3
	bne	.L1172
	adds	r4, r4, #1
	uxth	r4, r4
	b	.L1166
.L1172:
	ldr	r2, [r3]
	cbnz	r2, .L1163
	ldr	r2, [r3, #12]
	ldrh	r1, [r2]
	cmp	r1, r8
	beq	.L1171
	ldr	r2, [r2, #4]
	adds	r1, r2, #1
	it	ne
	strne	r2, [r6, #3452]
.L1171:
	adds	r3, r3, #20
	b	.L1169
.L1173:
	mov	r3, #-1
	str	r3, [r6, #3452]
.L1163:
	ldr	r8, .L1275
	movs	r3, #1
	ldr	r5, [sp]
	mov	r4, r8
	strh	r3, [r8, #3476]	@ movhi
.L1175:
	ldrh	r6, [r4, #236]
	movw	r9, #65535
	ldr	r0, [r4, #3292]
	mov	ip, #20
	ldr	r1, [sp, #16]
	movs	r3, #0
	str	r3, [sp, #12]
.L1176:
	uxth	r2, r3
	cmp	r6, r2
	bhi	.L1178
	movs	r2, #0
	ldr	r1, [sp, #12]
	bl	FlashReadPages
	movs	r3, #0
.L1272:
	str	r3, [sp, #24]
	ldr	r2, [sp, #12]
	ldrh	r3, [sp, #24]
	cmp	r2, r3
	bhi	.L1208
	adds	r5, r5, #1
	ldrh	r3, [r4, #306]
	uxth	r5, r5
	cmp	r3, r5
	bne	.L1175
	ldrh	r2, [r4, #236]
	movw	r0, #65535
	movs	r3, #0
	strh	r5, [r10, #2]	@ movhi
	strh	r3, [r10, #4]	@ movhi
.L1209:
	uxth	r1, r3
	cmp	r1, r2
	bcs	.L1267
	ldr	r1, [sp, #16]
	ldrh	r4, [r1], #2
	cmp	r4, r0
	str	r1, [sp, #16]
	add	r1, r3, #1
	bne	.L1273
	mov	r3, r1
	b	.L1209
.L1178:
	ldrh	r2, [r1], #2
	cmp	r2, r9
	beq	.L1177
	ldr	r7, [sp, #12]
	orr	r2, r5, r2, lsl #10
	mla	lr, ip, r7, r0
	str	r2, [lr, #4]
	mov	r2, r7
	adds	r2, r2, #1
	uxth	r2, r2
	str	r2, [sp, #12]
.L1177:
	adds	r3, r3, #1
	b	.L1176
.L1276:
	.align	2
.L1275:
	.word	.LANCHOR0
	.word	.LANCHOR1+435
	.word	.LC8
.L1208:
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
	bcc	.L1180
	ldr	r3, [sp, #28]
	bne	.L1181
	ldr	r2, [sp, #20]
	cmp	r2, r0
	bhi	.L1180
.L1181:
	ldr	r2, [sp, #8]
	cmp	r5, r2
	bne	.L1182
	ldr	r2, [sp, #4]
	cmp	r2, r0
	beq	.L1183
.L1182:
	ldr	r3, [r3, r7]
	adds	r3, r3, #1
	beq	.L1184
	ldr	r9, [r9, #12]
	movw	r3, #61589
	ldrh	r2, [r9]
	cmp	r2, r3
	beq	.L1185
	ldrh	r0, [r10]
.L1269:
	bl	decrement_vpc_count
	b	.L1180
.L1185:
	ldr	fp, [r9, #4]
	cmp	fp, #-1
	beq	.L1186
	ldr	r1, [r4, #2596]
	mov	r0, fp
	bl	ftl_cmp_data_ver
	cbz	r0, .L1186
	add	r3, fp, #1
	str	r3, [r4, #2596]
.L1186:
	ldrh	r2, [r9]
	movw	r3, #61589
	cmp	r2, r3
	beq	.L1187
	mov	r2, #1952
	ldr	r1, .L1277
	ldr	r0, .L1277+4
	bl	sftl_printk
.L1187:
	ldr	r6, [r9, #8]
	add	r1, sp, #40
	ldr	r3, [r9, #12]
	movs	r2, #0
	mov	r0, r6
	str	r3, [sp, #36]
	bl	log2phys
	ldr	r1, [r4, #3452]
	adds	r0, r1, #1
	beq	.L1188
	mov	r0, fp
	bl	ftl_cmp_data_ver
	cmp	r0, #0
	beq	.L1188
	ldr	r3, [sp, #36]
	adds	r2, r3, #1
	beq	.L1189
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
	bne	.L1190
.L1191:
	mov	r3, #-1
	str	r3, [sp, #36]
.L1198:
	ldr	r0, [sp, #36]
	adds	r1, r0, #1
	beq	.L1180
.L1212:
	ubfx	r0, r0, #10, #16
	bl	P2V_block_in_plane
	ldrh	r3, [r4, #244]
	mov	r6, r0
	cmp	r3, r0
	bhi	.L1204
	movw	r2, #2057
	ldr	r1, .L1277
	ldr	r0, .L1277+4
	bl	sftl_printk
.L1204:
	ldr	r3, [r8, #76]
	ldrh	r3, [r3, r6, lsl #1]
	cmp	r3, #0
	beq	.L1205
	mov	r0, r6
	b	.L1269
.L1189:
	ldr	r3, [sp, #44]
	ldr	r2, [sp, #40]
	cmp	r2, r3
	bne	.L1180
	movs	r2, #1
	add	r1, sp, #36
	mov	r0, r6
	bl	log2phys
.L1180:
	ldr	r3, [sp, #24]
	adds	r3, r3, #1
	b	.L1272
.L1190:
	ldr	r1, [r9, #8]
	cmp	r6, r1
	bne	.L1191
	ldr	r1, [r9, #4]
	ldr	r0, [r4, #3452]
	str	r1, [sp, #28]
	bl	ftl_cmp_data_ver
	cmp	r0, #0
	beq	.L1191
	ldr	r1, [sp, #40]
	ldr	r0, [sp, #44]
	cmp	r1, r0
	bne	.L1193
.L1196:
	ldr	r1, [sp, #36]
	mov	r0, r6
	bl	FtlReUsePrevPpa
	b	.L1191
.L1193:
	ldr	r0, [sp, #36]
	cmp	r1, r0
	beq	.L1191
	adds	r0, r1, #1
	beq	.L1194
	str	r1, [r3, #4]
	movs	r2, #0
	movs	r1, #1
	mov	r0, r3
	ldr	r9, [r3, #12]
	bl	FlashReadPages
.L1195:
	ldr	r3, [r4, #3292]
	ldr	r3, [r3, r7]
	adds	r3, r3, #1
	beq	.L1196
	ldr	r3, [r9, #4]
	ldr	r0, [r4, #3452]
	mov	r1, r3
	bl	ftl_cmp_data_ver
	cmp	r0, #0
	beq	.L1196
	mov	r1, r3
	ldr	r0, [sp, #28]
	bl	ftl_cmp_data_ver
	cmp	r0, #0
	beq	.L1191
	b	.L1196
.L1194:
	str	r1, [r2, r7]
	b	.L1195
.L1188:
	ldr	r3, [sp, #44]
	ldr	r2, [sp, #40]
	cmp	r2, r3
	beq	.L1198
	ldr	r1, [sp, #36]
	adds	r0, r1, #1
	beq	.L1200
	ldr	r3, [r8, #252]
	cmp	r3, r1, lsr #10
	bhi	.L1200
	ldr	r0, .L1277+8
.L1271:
	bl	sftl_printk
	b	.L1180
.L1200:
	movs	r2, #1
	add	r1, sp, #44
	mov	r0, r6
	bl	log2phys
	ldr	r9, [sp, #40]
	cmp	r9, #-1
	beq	.L1198
	ldr	r3, [sp, #36]
	cmp	r9, r3
	beq	.L1202
	ubfx	r0, r9, #10, #16
	bl	P2V_block_in_plane
	ldrh	r3, [r4, #28]
	cmp	r3, r0
	beq	.L1203
	ldrh	r3, [r4, #80]
	cmp	r3, r0
	beq	.L1203
	ldrh	r3, [r4, #128]
	cmp	r3, r0
	bne	.L1198
.L1203:
	ldr	r0, [r8, #3292]
	movs	r2, #0
	movs	r1, #1
	str	r9, [r0, #4]
	ldr	r7, [r0, #12]
	bl	FlashReadPages
	ldr	r3, [r8, #3292]
	ldr	r3, [r3]
	adds	r3, r3, #1
	beq	.L1198
	ldr	r1, [r7, #4]
	mov	r0, fp
	bl	ftl_cmp_data_ver
	cmp	r0, #0
	bne	.L1198
	movs	r2, #1
	add	r1, sp, #40
	mov	r0, r6
	bl	log2phys
	b	.L1198
.L1205:
	mov	r1, r6
	ldr	r0, .L1277+12
	b	.L1271
.L1184:
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
	bne	.L1207
.L1270:
	str	fp, [r4, #3452]
	b	.L1180
.L1207:
	cmp	fp, r3
	bcs	.L1180
	b	.L1270
.L1183:
	ldrb	r3, [sp, #4]	@ zero_extendqisi2
	mov	r1, r5
	strh	r5, [r10, #2]	@ movhi
	ldr	r2, [sp, #4]
	strb	r3, [r10, #6]
	b	.L1274
.L1202:
	mov	r0, r9
	b	.L1212
.L1278:
	.align	2
.L1277:
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
	ldr	r6, .L1303
	ldr	r7, .L1303+4
	ldr	r1, .L1303+8
	mov	r5, r6
	ldr	r0, .L1303+12
	bl	sftl_printk
	mov	r2, #8192
	movs	r1, #0
	ldr	r0, .L1303+4
	bl	ftl_memset
.L1280:
	ldr	r3, [r6, #2556]
	cmp	r4, r3
	bcc	.L1282
	ldr	r8, .L1303+4
	movs	r4, #0
	ldr	r10, .L1303+24
	mov	r7, r4
	movw	r9, #65535
.L1283:
	ldrh	r2, [r5, #244]
	uxth	r3, r4
	cmp	r2, r3
	bhi	.L1285
	ldr	r4, [r5, #2536]
	cbz	r4, .L1286
	ldr	r3, [r5, #2520]
	movs	r6, #0
	ldrh	r8, [r5, #228]
	mov	fp, #6
	ldr	r9, .L1303+4
	subs	r4, r4, r3
	ldr	r3, .L1303+16
	asrs	r4, r4, #1
	ldr	r10, .L1303+28
	muls	r4, r3, r4
	uxth	r4, r4
.L1287:
	uxth	r3, r6
	cmp	r8, r3
	bls	.L1286
	ldr	r3, [r5, #76]
	ldrh	r2, [r3, r4, lsl #1]
	cbz	r2, .L1288
	movs	r7, #1
	ldrh	r3, [r9, r4, lsl #1]
	mov	r1, r4
	mov	r0, r10
	bl	sftl_printk
.L1288:
	mul	r4, fp, r4
	ldr	r3, [r5, #2520]
	adds	r6, r6, #1
	ldrh	r4, [r3, r4]
	movw	r3, #65535
	cmp	r4, r3
	bne	.L1287
.L1286:
	cbz	r7, .L1279
	movw	r2, #2383
	ldr	r1, .L1303+8
	ldr	r0, .L1303+20
	bl	sftl_printk
.L1279:
	add	sp, sp, #12
	@ sp needed
	pop	{r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L1282:
	movs	r2, #0
	add	r1, sp, #4
	mov	r0, r4
	bl	log2phys
	ldr	r0, [sp, #4]
	adds	r3, r0, #1
	beq	.L1281
	ubfx	r0, r0, #10, #16
	bl	P2V_block_in_plane
	ldrh	r3, [r7, r0, lsl #1]
	adds	r3, r3, #1
	strh	r3, [r7, r0, lsl #1]	@ movhi
.L1281:
	adds	r4, r4, #1
	b	.L1280
.L1285:
	ldr	r3, [r5, #76]
	uxth	r6, r4
	ldrh	r2, [r3, r6, lsl #1]
	ldrh	r3, [r8, r6, lsl #1]
	cmp	r2, r3
	beq	.L1284
	mov	r1, r6
	mov	r0, r10
	bl	sftl_printk
	ldr	r3, [r5, #76]
	ldrh	r3, [r3, r6, lsl #1]
	cmp	r3, r9
	beq	.L1284
	ldrh	r2, [r8, r6, lsl #1]
	cmp	r2, r3
	it	hi
	movhi	r7, #1
.L1284:
	adds	r4, r4, #1
	b	.L1283
.L1304:
	.align	2
.L1303:
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
	ldr	r6, .L1319
	.pad #32
	sub	sp, sp, #32
	movs	r1, #0
	ldr	r8, .L1319+16
	mov	r4, r6
	ldr	r0, .L1319+4
	bl	sftl_printk
.L1306:
	ldr	r3, [r6, #2556]
	cmp	r5, r3
	bcc	.L1312
	add	sp, sp, #32
	@ sp needed
	pop	{r4, r5, r6, r7, r8, pc}
.L1312:
	movs	r2, #0
	add	r1, sp, #28
	mov	r0, r5
	bl	log2phys
	ubfx	r3, r5, #0, #11
	cbnz	r3, .L1307
	ldr	r2, [sp, #28]
	mov	r1, r5
	mov	r0, r8
	bl	sftl_printk
.L1307:
	ldr	r3, [sp, #28]
	adds	r2, r3, #1
	beq	.L1309
	str	r3, [r4, #3460]
	movs	r2, #0
	ldr	r3, [r4, #3304]
	movs	r1, #1
	ldr	r7, [r4, #3336]
	ldr	r0, .L1319+8
	str	r3, [r4, #3464]
	str	r5, [r4, #3472]
	str	r7, [r4, #3468]
	str	r2, [r4, #3456]
	bl	FlashReadPages
	ldr	r3, [r4, #3456]
	cmp	r3, #256
	beq	.L1310
	adds	r3, r3, #1
	beq	.L1310
	ldr	r3, [r7, #8]
	cmp	r5, r3
	beq	.L1309
.L1310:
	ldr	r2, [r4, #3464]
	ldr	r3, [r4, #3468]
	ldr	r0, .L1319+12
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
.L1320:
	.align	2
.L1319:
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
	ldr	r3, .L1358
	push	{r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.save {r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.pad #44
	sub	sp, sp, #44
	mov	r5, r0
	str	r1, [sp, #4]
	ldrh	r6, [r3, #4]
	movw	r3, #65535
	cmp	r6, r3
	beq	.L1348
	cbnz	r6, .L1322
.L1323:
	bl	FtlGcPageVarInit
	b	.L1324
.L1348:
	movs	r6, #0
.L1322:
	ldr	r3, .L1358+4
	ldr	r2, [sp, #4]
	ldrh	r3, [r3, #306]
	cmp	r3, r2
	beq	.L1323
.L1324:
	mov	fp, #0
.L1342:
	ldrh	r2, [r5]
	movs	r3, #0
	strb	r3, [r5, #8]
	movw	r3, #65535
	cmp	r2, r3
	beq	.L1325
	ldr	r4, .L1358+4
.L1345:
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
.L1326:
	uxth	r3, r2
	cmp	ip, r3
	bhi	.L1330
	mov	r10, #0
	movs	r2, #0
	mov	r1, r7
	ldr	r0, [sp]
	bl	FlashReadPages
.L1331:
	uxth	r3, r10
	cmp	r7, r3
	bhi	.L1343
	ldr	r3, [sp, #4]
	add	fp, fp, #1
	adds	r6, r6, #1
	uxth	r6, r6
	cmp	r3, fp
	bls	.L1344
.L1346:
	ldrh	r3, [r4, #306]
	cmp	r3, r6
	bhi	.L1345
.L1325:
	ldr	r3, .L1358
	movw	r2, #65535
	strh	r6, [r5, #2]	@ movhi
	mov	r1, r6
	mov	r0, r5
	strh	r2, [r3, #4]	@ movhi
	movs	r2, #0
	strb	r2, [r5, #6]
	bl	ftl_sb_update_avl_pages
	b	.L1347
.L1330:
	ldrh	r3, [lr], #2
	cmp	r3, r8
	beq	.L1327
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
.L1327:
	adds	r2, r2, #1
	b	.L1326
.L1343:
	movs	r3, #20
	ldr	r2, [r4, #3292]
	mul	r3, r3, r10
	adds	r1, r2, r3
	ldr	r8, [r2, r3]
	ldr	r0, [r1, #4]
	ldr	r9, [r1, #12]
	str	r0, [sp]
	cmp	r8, #0
	bne	.L1332
	ldrh	r1, [r9]
	movw	r2, #65535
	cmp	r1, r2
	bne	.L1333
.L1356:
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
	b	.L1342
.L1333:
	ldr	r0, [r9, #8]
	ldr	r2, [r4, #2556]
	str	r3, [sp, #8]
	cmp	r0, r2
	bhi	.L1356
	mov	r2, r8
	add	r1, sp, #16
	bl	log2phys
	ldr	r2, [r9, #12]
	ldr	r1, [sp, #16]
	ldr	r3, [sp, #8]
	cmp	r2, r1
	beq	.L1336
.L1338:
	ldr	r2, [r9, #8]
.L1357:
	ldr	r1, [sp]
	add	r10, r10, #1
	ldr	r0, [r9, #12]
	bl	FtlGcUpdatePage
	b	.L1331
.L1336:
	str	r3, [sp, #8]
	adds	r3, r2, #1
	beq	.L1338
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
.L1339:
	cmp	r8, r2
	beq	.L1338
	ldr	r1, [ip, #8]
	ldr	r3, [r0, r8, lsl #2]
	ldr	r1, [r1, r8, lsl #2]
	cmp	r1, r3
	beq	.L1340
	ldr	r2, [sp, #24]
	ldrh	r1, [r5]
	ldr	r0, .L1358+8
	bl	sftl_printk
	b	.L1356
.L1340:
	add	r8, r8, #1
	b	.L1339
.L1332:
	mov	r2, #-1
	b	.L1357
.L1344:
	ldr	r2, .L1358
	movw	r1, #65535
	ldrh	r3, [r2, #4]
	cmp	r3, r1
	beq	.L1346
	add	r3, r3, fp
	strh	r3, [r2, #4]	@ movhi
	ldrh	r3, [r4, #306]
	cmp	r3, r6
	bls	.L1346
.L1347:
	mov	r0, #-1
	add	sp, sp, #44
	@ sp needed
	pop	{r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L1359:
	.align	2
.L1358:
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
	ldr	r6, .L1376
	ldr	r9, [r6, #2724]
	mov	r5, r6
	cmp	r9, #0
	beq	.L1361
	ldr	r2, [r6, #2728]
	ldr	r3, [r6, #2556]
	cmp	r2, r3
	bcs	.L1362
	mov	r4, #2048
.L1367:
	ldr	r0, [r5, #2728]
	ldr	r3, [r5, #2556]
	cmp	r0, r3
	bcc	.L1363
.L1366:
	mov	r0, #-1
.L1360:
	add	sp, sp, #92
	@ sp needed
	pop	{r4, r5, r6, r7, r8, r9, pc}
.L1363:
	movs	r2, #0
	mov	r1, sp
	bl	log2phys
	ldr	r2, [sp]
	ldr	r3, [r5, #2728]
	adds	r1, r2, #1
	add	r3, r3, #1
	str	r3, [r5, #2728]
	beq	.L1365
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
	bne	.L1366
	ldr	r0, [sp]
	ubfx	r0, r0, #10, #16
	bl	P2V_block_in_plane
	bl	FtlGcRefreshBlock
	b	.L1366
.L1365:
	subs	r4, r4, #1
	bne	.L1367
	b	.L1366
.L1362:
	ldr	r3, [r6, #2560]
	movs	r0, #0
	str	r0, [r6, #2724]
	str	r0, [r6, #2728]
	str	r3, [r6, #2720]
	b	.L1360
.L1361:
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
	bhi	.L1371
	ldr	r3, [r6, #2556]
	lsrs	r1, r1, #10
	mov	r0, #1000
	adds	r1, r1, #1
	muls	r0, r3, r0
	bl	__aeabi_uidiv
	add	r0, r0, r7
	cmp	r8, r0
	bhi	.L1371
	ldrh	r3, [r6, #2500]
	ands	r0, r4, r3
	bne	.L1373
	ldr	r2, [r6, #2744]
	cmp	r3, r2
	beq	.L1360
.L1371:
	ldrh	r3, [r5, #2500]
	movs	r0, #0
	str	r0, [r5, #2728]
	str	r8, [r5, #2720]
	str	r3, [r5, #2744]
	movs	r3, #1
	str	r3, [r5, #2724]
	b	.L1360
.L1373:
	mov	r0, r9
	b	.L1360
.L1377:
	.align	2
.L1376:
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
	ldr	r5, .L1382
	movs	r6, #12
	bl	FtlWriteDump_data
.L1379:
	ldrh	r3, [r5, #342]
	uxth	r0, r4
	cmp	r3, r0
	bhi	.L1381
	movs	r0, #0
	pop	{r4, r5, r6, pc}
.L1381:
	ldr	r2, [r5, #2544]
	uxth	r3, r4
	mla	r3, r6, r3, r2
	ldr	r3, [r3, #4]
	cmp	r3, #0
	bge	.L1380
	bl	flush_l2p_region
.L1380:
	adds	r4, r4, #1
	b	.L1379
.L1383:
	.align	2
.L1382:
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
	ldr	r4, .L1396
	adds	r2, r0, r1
	.pad #108
	sub	sp, sp, #108
	mov	r8, r0
	mov	r6, r1
	ldrh	r3, [r4, #300]
	cmp	r2, r3
	bhi	.L1392
	ldrh	r7, [r4, #312]
	mov	r9, #0
	lsr	r7, r0, r7
	lsl	fp, r7, #2
.L1386:
	cbnz	r6, .L1391
.L1384:
	mov	r0, r9
	add	sp, sp, #108
	@ sp needed
	pop	{r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L1391:
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
	cbz	r2, .L1388
	cmp	r5, r3
	beq	.L1388
	ldr	r3, [r4, #3312]
	add	r0, sp, #20
	str	r2, [sp, #24]
	movs	r2, #1
	mov	r1, r2
	str	r3, [sp, #28]
	add	r3, sp, #40
	str	r3, [sp, #32]
	bl	FlashReadPages
.L1389:
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
	ldr	r0, .L1396+4
	adds	r7, r7, #1
	bl	FtlMapWritePage
	ldr	r3, [sp, #8]
	adds	r0, r0, #1
	it	eq
	moveq	r9, #-1
	add	r10, r10, r3
	b	.L1386
.L1388:
	ldrh	r2, [r4, #314]
	movs	r1, #0
	ldr	r0, [r4, #3312]
	bl	ftl_memset
	b	.L1389
.L1392:
	mov	r9, #-1
	b	.L1384
.L1397:
	.align	2
.L1396:
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
	ldr	r2, .L1400
	ldr	r3, .L1400+4
	ldr	r1, [r2, #2644]
	cmp	r1, r3
	bne	.L1398
	ldr	r3, .L1400+8
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
.L1398:
	bx	lr
.L1401:
	.align	2
.L1400:
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
	ldr	r3, .L1407
	ldrh	r2, [r3, #3656]
	cmp	r2, #31
	itett	ls
	addls	r2, r2, #1
	movhi	r2, #32
	strhls	r2, [r3, #3656]	@ movhi
	movls	r2, #1
	cbnz	r0, .L1404
	ldr	r1, [r3, #3352]
	ldr	r0, [r1, #20]
	ldr	r1, [r1, #16]
	add	r2, r2, r0
	cmp	r1, r2
	bcc	.L1405
.L1404:
	ldr	r2, [r3, #3352]
	movs	r0, #64
	ldr	r1, [r2, #16]
	str	r1, [r2, #20]
	ldr	r1, .L1407+4
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
.L1405:
	movs	r0, #0
	pop	{r3, pc}
.L1408:
	.align	2
.L1407:
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
	ldr	r4, .L1420
	adds	r2, r0, r1
	.pad #108
	sub	sp, sp, #108
	mov	r8, r0
	mov	r7, r1
	ldrh	r3, [r4, #300]
	cmp	r2, r3
	bhi	.L1419
	ldrh	r6, [r4, #312]
	mov	r9, #0
	lsr	r6, r0, r6
	lsl	fp, r6, #2
.L1412:
	cbnz	r7, .L1418
.L1410:
	mov	r0, r9
	add	sp, sp, #108
	@ sp needed
	pop	{r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L1418:
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
	cbz	r3, .L1414
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
	bne	.L1416
	mov	r2, r3
	mov	r1, r6
	ldr	r0, .L1420+4
	bl	sftl_printk
	ldr	r2, [r4, #3312]
	mov	r1, r6
	ldr	r0, .L1420+8
	bl	FtlMapWritePage
.L1416:
	ldr	r1, [r4, #3312]
	lsls	r2, r5, #9
	ldr	r3, [sp, #4]
	mov	r0, r10
	add	r1, r1, r3, lsl #9
	bl	ftl_memcpy
.L1417:
	ldr	r3, [sp, #8]
	adds	r6, r6, #1
	subs	r7, r7, r5
	add	r8, r8, r5
	add	fp, fp, #4
	add	r10, r10, r3
	b	.L1412
.L1414:
	lsls	r2, r5, #9
	mov	r1, r3
	mov	r0, r10
	bl	ftl_memset
	b	.L1417
.L1419:
	mov	r9, #-1
	b	.L1410
.L1421:
	.align	2
.L1420:
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
	ldr	r4, .L1424
	ldr	r2, [r4, #3352]
	ldrh	r1, [r4, #3344]
	bl	FtlVendorPartRead
	ldr	r3, [r4, #3352]
	ldr	r2, [r3]
	ldr	r3, .L1424+4
	cmp	r2, r3
	beq	.L1423
	ldr	r1, .L1424+8
	ldr	r0, .L1424+12
	bl	sftl_printk
	ldrh	r2, [r4, #3344]
	movs	r1, #0
	ldr	r0, [r4, #3352]
	lsls	r2, r2, #9
	bl	ftl_memset
.L1423:
	movs	r0, #0
	pop	{r4, pc}
.L1425:
	.align	2
.L1424:
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
	ldr	r4, .L1429
	movs	r0, #0
	ldr	r5, .L1429+4
	addw	r2, r4, #2644
	bl	FtlVendorPartRead
	ldr	r3, [r4, #2644]
	cmp	r3, r5
	beq	.L1427
	mov	r2, #512
	movs	r1, #0
	addw	r0, r4, #2644
	bl	ftl_memset
	str	r5, [r4, #2644]
.L1427:
	ldr	r3, [r4, #2644]
	cmp	r3, r5
	bne	.L1428
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
.L1428:
	ldrh	r2, [r4, #296]
	ldr	r3, [r4, #2604]
	ldr	r0, [r4, #2600]
	ldrh	r1, [r4, #244]
	mla	r0, r0, r2, r3
	bl	__aeabi_uidiv
	str	r0, [r4, #2608]
	pop	{r3, r4, r5, pc}
.L1430:
	.align	2
.L1429:
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
	cbz	r3, .L1432
	ldrh	r5, [r0, #6]
	movs	r3, #0
	ldr	r4, .L1436
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
	cbz	r2, .L1434
	movs	r2, #1
	add	r0, r4, #3456
	mov	r1, r2
	bl	FlashReadPages
.L1435:
	ldr	r2, [r4, #3464]
	mov	r1, r5
	mov	r0, r6
	pop	{r4, r5, r6, lr}
	b	FtlMapWritePage
.L1434:
	ldrh	r2, [r4, #314]
	movs	r1, #255
	bl	ftl_memset
	b	.L1435
.L1432:
	pop	{r4, r5, r6, pc}
.L1437:
	.align	2
.L1436:
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
	ldr	r4, .L1453
	movs	r1, #255
	movw	r7, #65535
	ldr	r9, .L1453+16
	ldr	r3, [r4, #3304]
	add	r8, r4, #3456
	ldr	r6, [r4, #3336]
	ldr	r10, .L1453+20
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
	ldr	r3, .L1453+4
	str	r3, [r4, #2472]
	ldr	r3, .L1453+8
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
.L1439:
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
	blt	.L1440
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
.L1440:
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
	bne	.L1441
	cmp	r3, #1
	bne	.L1442
	movw	r2, #1138
	mov	r1, r9
	mov	r0, r10
	bl	sftl_printk
.L1442:
	ldrh	r3, [r4, #2630]
	adds	r5, r5, #1
	uxth	r5, r5
	cmp	r3, #1
	ittt	eq
	ldrheq	r3, [r4, #308]
	addeq	r3, r3, #-1
	strheq	r3, [r4, #2630]	@ movhi
	cmp	r5, #3
	bls	.L1439
	mov	r2, r5
	ldr	r1, [r4, #3460]
	ldr	r0, .L1453+12
	bl	sftl_printk
.L1445:
	b	.L1445
.L1441:
	cmp	r3, #1
	beq	.L1439
	cmp	r2, #256
	beq	.L1439
	movw	r3, #65535
	cmp	r7, r3
	beq	.L1446
	movs	r1, #1
	mov	r0, r7
	bl	FtlFreeSysBlkQueueIn
.L1446:
	movs	r0, #0
	pop	{r4, r5, r6, r7, r8, r9, r10, pc}
.L1454:
	.align	2
.L1453:
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
	ldr	r3, .L1458
	ldr	r3, [r3]
	cmp	r3, #1
	bne	.L1457
	bl	FtlSysFlush
.L1457:
	movs	r0, #0
	pop	{r3, pc}
.L1459:
	.align	2
.L1458:
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
	ldr	r4, .L1475
	mov	r8, r0
	mov	r6, r1
	ldr	r3, [r4, #344]
	cmp	r2, r3
	bhi	.L1468
	cmp	r1, #31
	bhi	.L1462
.L1467:
	movs	r0, #0
.L1460:
	add	sp, sp, #8
	@ sp needed
	pop	{r4, r5, r6, r7, r8, pc}
.L1462:
	ldrh	r5, [r4, #262]
	mov	r1, r5
	bl	__aeabi_uidiv
	smulbb	r3, r0, r5
	mov	r7, r0
	sub	r8, r8, r3
	uxth	r3, r8
	cbz	r3, .L1463
	subs	r5, r5, r3
	adds	r7, r7, #1
	cmp	r5, r6
	it	cs
	movcs	r5, r6
	uxth	r5, r5
	subs	r6, r6, r5
.L1463:
	mov	r3, #-1
	str	r3, [sp, #4]
.L1464:
	ldrh	r3, [r4, #262]
	cmp	r6, r3
	bcs	.L1466
	ldr	r3, [r4, #3660]
	cmp	r3, #32
	bls	.L1467
	movs	r5, #0
	str	r5, [r4, #3660]
	bl	l2p_flush
	bl	FtlVpcTblFlush
	b	.L1467
.L1466:
	movs	r2, #0
	mov	r1, sp
	mov	r0, r7
	bl	log2phys
	ldr	r3, [sp]
	adds	r3, r3, #1
	beq	.L1465
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
.L1465:
	ldrh	r3, [r4, #262]
	adds	r7, r7, #1
	subs	r6, r6, r3
	b	.L1464
.L1468:
	mov	r0, #-1
	b	.L1460
.L1476:
	.align	2
.L1475:
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
	ldr	r4, .L1490
	ldr	r1, .L1490+4
	ldr	r0, .L1490+8
	bl	sftl_printk
	ldrh	r2, [r4, #246]
	movs	r1, #0
	ldr	r0, [r4, #3356]
	lsls	r2, r2, #1
	bl	ftl_memset
.L1478:
	ldr	r3, [r4, #2556]
	cmp	r5, r3
	bcc	.L1480
	ldr	r9, .L1490+12
	movs	r7, #0
	movw	r8, #65535
.L1481:
	ldrh	r3, [r4, #244]
	uxth	r6, r7
	cmp	r3, r6
	bhi	.L1486
	bl	l2p_flush
	bl	FtlVpcTblFlush
	add	sp, sp, #12
	@ sp needed
	pop	{r4, r5, r6, r7, r8, r9, pc}
.L1480:
	movs	r2, #0
	add	r1, sp, #4
	mov	r0, r5
	bl	log2phys
	ldr	r0, [sp, #4]
	adds	r3, r0, #1
	beq	.L1479
	ubfx	r0, r0, #10, #16
	bl	P2V_block_in_plane
	ldr	r2, [r4, #3356]
	ldrh	r3, [r2, r0, lsl #1]
	adds	r3, r3, #1
	strh	r3, [r2, r0, lsl #1]	@ movhi
.L1479:
	adds	r5, r5, #1
	b	.L1478
.L1486:
	ldr	r3, [r4, #76]
	uxth	r5, r7
	ldrh	r2, [r3, r5, lsl #1]
	ldr	r3, [r4, #3356]
	ldrh	r3, [r3, r5, lsl #1]
	cmp	r2, r3
	beq	.L1483
	cmp	r2, r8
	beq	.L1483
	mov	r1, r5
	mov	r0, r9
	bl	sftl_printk
	ldrh	r3, [r4, #28]
	cmp	r3, r6
	beq	.L1483
	ldrh	r3, [r4, #128]
	cmp	r3, r6
	beq	.L1483
	ldrh	r3, [r4, #80]
	cmp	r3, r6
	beq	.L1483
	ldr	r3, [r4, #76]
	ldrh	r2, [r3, r5, lsl #1]
	cbnz	r2, .L1485
	ldr	r2, [r4, #3356]
	ldrh	r2, [r2, r5, lsl #1]
	strh	r2, [r3, r5, lsl #1]	@ movhi
.L1483:
	adds	r7, r7, #1
	b	.L1481
.L1485:
	ldr	r2, [r4, #3356]
	mov	r0, r6
	ldrh	r2, [r2, r5, lsl #1]
	strh	r2, [r3, r5, lsl #1]	@ movhi
	bl	update_vpc_list
	b	.L1483
.L1491:
	.align	2
.L1490:
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
	ldr	r4, .L1502
	ldrh	r5, [r0]
	ldrh	r3, [r4, #244]
	cmp	r3, r5
	bcs	.L1493
	movw	r2, #2755
	ldr	r1, .L1502+4
	ldr	r0, .L1502+8
	bl	sftl_printk
.L1493:
	movw	r3, #65535
	cmp	r5, r3
	beq	.L1494
	ldr	r3, [r4, #76]
	mov	r0, r5
	ldrh	r3, [r3, r5, lsl #1]
	cbz	r3, .L1495
	bl	INSERT_DATA_LIST
.L1494:
	ldrh	r0, [r4, #3442]
	movs	r3, #1
	strb	r3, [r6, #8]
	movw	r3, #65535
	cmp	r0, r3
	beq	.L1496
	cmp	r5, r0
	bne	.L1497
	ldr	r3, [r4, #76]
	ldrh	r3, [r3, r0, lsl #1]
	cbz	r3, .L1498
.L1497:
	bl	update_vpc_list
.L1498:
	movw	r3, #65535
	strh	r3, [r4, #3442]	@ movhi
.L1496:
	mov	r0, r6
	bl	allocate_data_superblock
	bl	l2p_flush
	movs	r0, #0
	bl	FtlEctTblFlush
	bl	FtlVpcTblFlush
	movs	r0, #0
	pop	{r4, r5, r6, pc}
.L1495:
	bl	INSERT_FREE_LIST
	b	.L1494
.L1503:
	.align	2
.L1502:
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
	ldr	r10, .L1523+16
	mov	r5, r3
	movs	r2, #0
	mov	r4, r0
	ldr	r8, .L1523
	mov	r9, r1
	ldrb	r3, [r3, #9]	@ zero_extendqisi2
	bl	FlashProgPages
.L1505:
	cmp	r6, r9
	bne	.L1512
	ldr	r3, .L1523
	ldrb	r2, [r5, #6]	@ zero_extendqisi2
	ldrh	r3, [r3, #236]
	cmp	r2, r3
	bcc	.L1504
	mov	r2, #1000
	ldr	r1, .L1523+4
	ldr	r0, .L1523+8
	bl	sftl_printk
.L1504:
	add	sp, sp, #12
	@ sp needed
	pop	{r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L1507:
	ldr	r1, [r4, #4]
	mov	r0, r10
	bl	sftl_printk
	ldr	r0, [r4, #4]
	ubfx	r0, r0, #10, #16
	bl	P2V_block_in_plane
	bl	decrement_vpc_count
	ldrh	r3, [r5, #4]
	cbnz	r3, .L1506
	mov	r0, r5
	bl	allocate_new_data_superblock
.L1506:
	mov	r0, r5
	bl	get_new_active_ppa
	movs	r2, #0
	str	r0, [r4, #4]
	str	r0, [sp, #4]
	movs	r1, #1
	ldrb	r3, [r5, #9]	@ zero_extendqisi2
	mov	r0, r4
	bl	FlashProgPages
.L1512:
	ldr	r2, [r4]
	adds	r3, r2, #1
	beq	.L1507
	cmp	r2, #256
	beq	.L1507
	ldrb	r2, [r5, #6]	@ zero_extendqisi2
	ldrh	r3, [r8, #236]
	cmp	r2, r3
	bcc	.L1508
	movw	r2, #985
	ldr	r1, .L1523+4
	ldr	r0, .L1523+8
	bl	sftl_printk
.L1508:
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
	beq	.L1509
	ldr	r3, [r8, #76]
	ldrh	r2, [r3, r0, lsl #1]
	cbnz	r2, .L1510
	mov	r1, r0
	ldr	r0, .L1523+12
	bl	sftl_printk
.L1510:
	mov	r0, r7
	bl	decrement_vpc_count
.L1509:
	adds	r6, r6, #1
	adds	r4, r4, #20
	b	.L1505
.L1524:
	.align	2
.L1523:
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
	ldr	r4, .L1549
	ldrh	r2, [r4, #128]
	ldrh	r1, [r4, #306]
	cmp	r2, r3
	bne	.L1526
.L1533:
	ldrh	r2, [r4, #128]
	movs	r3, #0
	str	r3, [r4, #3448]
	movw	r3, #65535
	cmp	r2, r3
	bne	.L1546
.L1527:
	movs	r0, #0
.L1525:
	add	sp, sp, #12
	@ sp needed
	pop	{r4, r5, r6, r7, r8, r9, pc}
.L1526:
	ldr	r5, .L1549+4
	cbz	r0, .L1529
	ldrh	r2, [r5, #4]
	cmp	r2, r3
	beq	.L1530
.L1531:
	movs	r1, #2
.L1529:
	ldr	r0, .L1549+8
	bl	FtlGcScanTempBlk
	ldrh	r2, [r5, #4]
	movw	r3, #65535
	str	r0, [sp, #4]
	cmp	r2, r3
	beq	.L1533
	movs	r0, #1
	b	.L1525
.L1530:
	movs	r3, #0
	strh	r3, [r5, #4]	@ movhi
	ldrh	r3, [r4, #228]
	cmp	r3, #17
	bhi	.L1531
	b	.L1529
.L1546:
	ldrb	r1, [r4, #135]	@ zero_extendqisi2
	ldrh	r3, [r4, #306]
	ldrh	r2, [r4, #3208]
	muls	r3, r1, r3
	cmp	r2, r3
	beq	.L1534
	movs	r2, #164
	ldr	r1, .L1549+12
	ldr	r0, .L1549+16
	bl	sftl_printk
.L1534:
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
.L1535:
	ldrh	r2, [r4, #3208]
	uxth	r3, r5
	cmp	r2, r3
	bhi	.L1539
	movw	r0, #65535
	bl	decrement_vpc_count
	ldrh	r0, [r4, #128]
	ldr	r3, [r4, #76]
	ldrh	r3, [r3, r0, lsl #1]
	cmp	r3, #0
	beq	.L1540
	bl	INSERT_DATA_LIST
.L1541:
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
	ble	.L1527
	movs	r3, #20
	strh	r3, [r4, #3156]	@ movhi
	b	.L1527
.L1539:
	uxth	r7, r5
	ldr	r9, [r4, #3204]
	ldr	r3, [r4, #2556]
	mul	r7, r8, r7
	add	r6, r9, r7
	ldr	r0, [r6, #8]
	cmp	r0, r3
	bcc	.L1536
.L1547:
	ldrh	r0, [r4, #128]
	b	.L1548
.L1536:
	movs	r2, #0
	add	r1, sp, #4
	bl	log2phys
	ldr	r0, [r9, r7]
	ldr	r3, [sp, #4]
	cmp	r0, r3
	bne	.L1538
	ubfx	r0, r0, #10, #16
	bl	P2V_block_in_plane
	movs	r2, #1
	mov	r7, r0
	adds	r1, r6, #4
	ldr	r0, [r6, #8]
	bl	log2phys
	mov	r0, r7
.L1548:
	bl	decrement_vpc_count
	b	.L1537
.L1538:
	ldr	r2, [r6, #4]
	cmp	r3, r2
	bne	.L1547
.L1537:
	adds	r5, r5, #1
	b	.L1535
.L1540:
	bl	INSERT_FREE_LIST
	b	.L1541
.L1550:
	.align	2
.L1549:
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
	ldr	r4, .L1553
	ldrh	r1, [r4, #306]
	add	r0, r4, #128
	bl	FtlGcScanTempBlk
	ldrh	r2, [r4, #130]
	ldrh	r3, [r4, #306]
	cmp	r2, r3
	bcc	.L1551
	addw	r0, r4, #3396
	bl	FtlMapBlkWriteDump_data
	movs	r0, #0
	bl	FtlGcFreeTempBlock
	movs	r3, #0
	str	r3, [r4, #3448]
.L1551:
	pop	{r4, pc}
.L1554:
	.align	2
.L1553:
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
	ldr	r4, .L1556
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
.L1557:
	.align	2
.L1556:
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
	ldr	r4, .L1566
	movs	r5, #0
	movs	r6, #20
	mov	r2, r3
	ldr	r1, [r4, #3172]
	ldr	r0, [r4, #3296]
	bl	FlashProgPages
.L1559:
	ldr	r1, [r4, #3172]
	uxth	r3, r5
	cmp	r1, r3
	bhi	.L1562
	ldr	r0, [r4, #3296]
	bl	FtlGcBufFree
	ldrh	r3, [r4, #132]
	movs	r0, #0
	str	r0, [r4, #3172]
	cbnz	r3, .L1558
	movs	r0, #1
	bl	FtlGcFreeTempBlock
	movs	r0, #1
.L1558:
	pop	{r4, r5, r6, pc}
.L1562:
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
	b	.L1559
.L1567:
	.align	2
.L1566:
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
	ldr	r4, .L1571
	ldrh	r2, [r4, #128]
	cmp	r2, r3
	beq	.L1569
	ldrh	r3, [r4, #132]
	cbnz	r3, .L1570
.L1569:
	movs	r0, #0
	movs	r5, #0
	bl	FtlGcFreeTempBlock
	ldr	r0, .L1571+4
	strb	r5, [r4, #136]
	bl	allocate_data_superblock
	strh	r5, [r4, #3200]	@ movhi
	strh	r5, [r4, #3208]	@ movhi
	bl	l2p_flush
	mov	r0, r5
	bl	FtlEctTblFlush
	bl	FtlVpcTblFlush
.L1570:
	ldr	r0, .L1571+4
	pop	{r3, r4, r5, lr}
	b	get_new_active_ppa
.L1572:
	.align	2
.L1571:
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
	ldr	r3, .L1704
	push	{r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.save {r4, r5, r6, r7, r8, r9, r10, fp, lr}
	mov	r9, r0
	.pad #44
	sub	sp, sp, #44
	mov	r4, r3
	ldr	r0, [r3, #3276]
	cmp	r0, #0
	bne	.L1656
	ldrh	r2, [r3, #2532]
	cmp	r2, #47
	bls	.L1573
	ldr	r2, .L1704+4
	ldrh	r1, [r2, #4]
	movw	r2, #65535
	cmp	r1, r2
	bne	.L1575
.L1578:
	ldrh	r2, [r4, #224]
	movw	r1, #65535
	cmp	r2, r1
	bne	.L1576
.L1577:
	bl	FtlReadRefresh
	ldr	r3, [r4, #3160]
	movw	r7, #65535
	ldrh	r1, [r4, #176]
	adds	r3, r3, #1
	add	r3, r3, r9, lsl #7
	cmp	r1, r7
	str	r3, [r4, #3160]
	bne	.L1579
	ldrh	r2, [r4, #128]
	cmp	r2, r1
	bne	.L1580
	ldrh	r6, [r4, #226]
	cmp	r6, r2
	bne	.L1581
	ldrh	r2, [r4, #228]
	cmp	r2, #24
	ite	cc
	movcc	r2, #5120
	movcs	r2, #1024
	cmp	r3, r2
	bls	.L1581
	movs	r5, #0
	str	r5, [r4, #3160]
	strh	r5, [r4, #3664]	@ movhi
	bl	GetSwlReplaceBlock
	cmp	r0, r6
	mov	r7, r0
	bne	.L1660
	ldrh	r2, [r4, #228]
	ldrh	r3, [r4, #3158]
	cmp	r2, r3
	bcs	.L1584
	movs	r0, #64
	bl	List_get_gc_head_node
	uxth	r3, r0
	cmp	r3, r7
	beq	.L1586
	mov	r0, r3
	ldr	r3, [r4, #76]
	ldrh	r3, [r3, r0, lsl #1]
	cmp	r3, #7
	bhi	.L1587
	mov	r0, r5
	bl	List_get_gc_head_node
	uxth	r5, r0
	movs	r3, #128
	strh	r3, [r4, #3158]	@ movhi
	cmp	r5, r7
	bne	.L1583
.L1586:
	bl	FtlGcReFreshBadBlk
	cmp	r9, #0
	bne	.L1589
	movw	r3, #65535
	cmp	r7, r3
	bne	.L1589
.L1654:
	ldrh	r3, [r4, #228]
	cmp	r3, #24
	bhi	.L1661
	cmp	r3, #16
	ldrh	r6, [r4, #306]
	bls	.L1591
	lsrs	r6, r6, #5
.L1590:
	ldrh	r2, [r4, #3156]
	cmp	r2, r3
	bcs	.L1594
	ldrh	r3, [r4, #128]
	movw	r2, #65535
	cmp	r3, r2
	bne	.L1595
	ldrh	r2, [r4, #226]
	cmp	r2, r3
	bne	.L1595
	ldrh	r0, [r4, #3664]
	cbnz	r0, .L1596
	ldr	r3, [r4, #2556]
	ldr	r2, [r4, #2552]
	add	r3, r3, r3, lsl #1
	cmp	r2, r3, lsr #2
	bcs	.L1597
.L1596:
	ldrh	r3, [r4, #2624]
	add	r3, r3, r3, lsl #1
	asrs	r3, r3, #2
.L1700:
	strh	r3, [r4, #3156]	@ movhi
	movs	r3, #0
	str	r3, [r4, #3168]
.L1573:
	add	sp, sp, #44
	@ sp needed
	pop	{r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L1575:
	ldrh	r3, [r3, #128]
	cmp	r3, r2
	beq	.L1578
	movs	r0, #1
	bl	FtlGcFreeTempBlock
	cmp	r0, #0
	beq	.L1578
	movs	r0, #1
	b	.L1573
.L1576:
	ldrh	r3, [r4, #226]
	cmp	r3, r1
	itt	eq
	strheq	r2, [r4, #226]	@ movhi
	strheq	r3, [r4, #224]	@ movhi
	b	.L1577
.L1587:
	movs	r3, #64
.L1699:
	strh	r3, [r4, #3158]	@ movhi
	b	.L1586
.L1584:
	movs	r3, #80
	b	.L1699
.L1660:
	mov	r5, r0
.L1583:
	ldr	r3, [r4, #76]
	mov	r7, r5
	ldrh	r1, [r4, #3156]
	ldrh	r2, [r4, #228]
	ldrh	r3, [r3, r5, lsl #1]
	str	r1, [sp, #4]
	ldr	r1, [r4, #2540]
	ldr	r0, .L1704+8
	ldrh	r1, [r1, r5, lsl #1]
	str	r1, [sp]
	mov	r1, r5
	bl	sftl_printk
	b	.L1586
.L1591:
	cmp	r3, #12
	bls	.L1592
	lsrs	r6, r6, #4
	b	.L1590
.L1592:
	cmp	r3, #8
	bls	.L1590
	lsrs	r6, r6, #2
	b	.L1590
.L1661:
	movs	r6, #1
	b	.L1590
.L1597:
	movs	r3, #18
	b	.L1700
.L1595:
	ldrh	r3, [r4, #2624]
	add	r3, r3, r3, lsl #1
	asrs	r3, r3, #2
	strh	r3, [r4, #3156]	@ movhi
.L1594:
	ldrh	r3, [r4, #3210]
	cbz	r3, .L1662
	adds	r6, r6, #32
	uxth	r6, r6
.L1662:
	movw	r7, #65535
.L1600:
	ldrh	r3, [r4, #176]
	movw	r2, #65535
	cmp	r3, r2
	bne	.L1609
	cmp	r7, r3
	beq	.L1610
	strh	r7, [r4, #176]	@ movhi
.L1611:
	ldrh	r0, [r4, #176]
	movw	r5, #65535
	movs	r3, #0
	strb	r3, [r4, #184]
	cmp	r0, r5
	beq	.L1609
	bl	IsBlkInGcList
	cbz	r0, .L1614
	strh	r5, [r4, #176]	@ movhi
.L1614:
	ldrh	r2, [r4, #176]
	movw	r3, #65535
	cmp	r2, r3
	beq	.L1609
	ldr	r0, .L1704+12
	bl	make_superblock
	movs	r3, #0
	ldrh	r2, [r4, #176]
	strh	r3, [r4, #178]	@ movhi
	strb	r3, [r4, #182]
	strh	r3, [r4, #3666]	@ movhi
	ldr	r3, [r4, #76]
	ldrh	r3, [r3, r2, lsl #1]
	strh	r3, [r4, #3668]	@ movhi
.L1609:
	ldrh	r3, [r4, #176]
	ldrh	r2, [r4, #28]
	cmp	r2, r3
	beq	.L1615
	ldrh	r2, [r4, #80]
	cmp	r2, r3
	beq	.L1615
	ldrh	r2, [r4, #128]
	cmp	r2, r3
	bne	.L1616
.L1615:
	movw	r3, #65535
	strh	r3, [r4, #176]	@ movhi
.L1616:
	ldr	r5, .L1704
	mov	r4, r5
.L1652:
	ldrh	r10, [r5, #176]
	movw	r3, #65535
	cmp	r10, r3
	bne	.L1617
	movs	r3, #0
	mov	fp, r3
	str	r3, [r5, #3168]
.L1618:
	ldrh	r8, [r4, #3164]
	mov	r0, r8
	bl	List_get_gc_head_node
	uxth	r2, r0
	cmp	r2, r10
	strh	r2, [r4, #176]	@ movhi
	bne	.L1619
	movs	r3, #0
	movs	r0, #8
	strh	r3, [r4, #3164]	@ movhi
	b	.L1573
.L1601:
	movs	r3, #0
	ldrh	r2, [r4, #228]
	str	r3, [r4, #3168]
	ldrh	r3, [r4, #3156]
	cmp	r2, r3
	bls	.L1602
	ldrh	r3, [r4, #3664]
	cbnz	r3, .L1603
	ldr	r3, [r4, #2556]
	ldr	r2, [r4, #2552]
	add	r3, r3, r3, lsl #1
	cmp	r2, r3, lsr #2
	bcs	.L1604
.L1603:
	ldrh	r3, [r4, #2624]
	add	r3, r3, r3, lsl #1
	asrs	r3, r3, #2
.L1701:
	strh	r3, [r4, #3156]	@ movhi
	bl	FtlReadRefresh
	movs	r0, #0
	bl	List_get_gc_head_node
	ldr	r3, [r4, #76]
	uxth	r0, r0
	ldrh	r3, [r3, r0, lsl #1]
	cmp	r3, #4
	bls	.L1602
.L1703:
	ldrh	r0, [r4, #3664]
	b	.L1573
.L1604:
	movs	r3, #18
	b	.L1701
.L1602:
	ldrh	r0, [r4, #3664]
	cmp	r0, #0
	bne	.L1606
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
	ble	.L1607
	ldrh	r3, [r4, #228]
	subs	r5, r5, #1
	cmp	r3, r5
	blt	.L1607
	bl	FtlReadRefresh
	b	.L1703
.L1607:
	cmp	r2, #0
	bne	.L1606
	movw	r0, #65535
	bl	decrement_vpc_count
	ldrh	r0, [r4, #228]
	adds	r0, r0, #1
	b	.L1573
.L1705:
	.align	2
.L1704:
	.word	.LANCHOR0
	.word	.LANCHOR2
	.word	.LC124
	.word	.LANCHOR0+176
.L1610:
	ldrh	r3, [r4, #226]
	cmp	r3, r7
	beq	.L1611
	ldr	r2, [r4, #76]
	ldrh	r3, [r2, r3, lsl #1]
	cbnz	r3, .L1612
	strh	r7, [r4, #226]	@ movhi
.L1612:
	ldrh	r3, [r4, #226]
	strh	r3, [r4, #176]	@ movhi
	movw	r3, #65535
	strh	r3, [r4, #226]	@ movhi
	b	.L1611
.L1619:
	str	r0, [sp, #16]
	mov	r0, r2
	str	r2, [sp, #12]
	add	r8, r8, #1
	bl	IsBlkInGcList
	ldr	r2, [sp, #12]
	ldr	r3, [sp, #16]
	cbz	r0, .L1620
	strh	r8, [r4, #3164]	@ movhi
	b	.L1618
.L1620:
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
	bgt	.L1622
	cmp	r8, #48
	bls	.L1623
	cmp	ip, #8
	bls	.L1623
	ldrh	ip, [r4, #3200]
	cmp	ip, #35
	bhi	.L1623
.L1622:
	strh	fp, [r4, #3164]	@ movhi
.L1623:
	ldrh	r3, [r0, r3, lsl #1]
	cmp	r1, r3
	bgt	.L1624
	cmp	r7, r10
	bne	.L1624
	ldrh	r1, [r4, #3164]
	cmp	r1, #3
	bhi	.L1624
	movs	r3, #0
	strh	r7, [r4, #176]	@ movhi
	strh	r3, [r4, #3164]	@ movhi
	b	.L1703
.L1624:
	cbnz	r3, .L1625
	movw	r0, #65535
	bl	decrement_vpc_count
	ldrh	r3, [r4, #3164]
	adds	r3, r3, #1
	strh	r3, [r4, #3164]	@ movhi
	b	.L1618
.L1625:
	movs	r3, #0
	strb	r3, [r4, #184]
	ldrh	r3, [r4, #28]
	cmp	r3, r2
	bne	.L1626
	movw	r2, #717
	ldr	r1, .L1706
	ldr	r0, .L1706+4
	bl	sftl_printk
.L1626:
	ldrh	r2, [r4, #176]
	ldrh	r3, [r4, #80]
	cmp	r2, r3
	bne	.L1627
	movw	r2, #718
	ldr	r1, .L1706
	ldr	r0, .L1706+4
	bl	sftl_printk
.L1627:
	ldrh	r2, [r4, #176]
	ldrh	r3, [r4, #128]
	cmp	r2, r3
	bne	.L1628
	movw	r2, #719
	ldr	r1, .L1706
	ldr	r0, .L1706+4
	bl	sftl_printk
.L1628:
	ldr	r0, .L1706+8
	bl	make_superblock
	ldrh	r1, [r4, #176]
	movs	r3, #0
	ldr	r2, [r4, #76]
	strh	r3, [r4, #3666]	@ movhi
	ldrh	r2, [r2, r1, lsl #1]
	strh	r3, [r4, #178]	@ movhi
	strb	r3, [r4, #182]
	strh	r2, [r4, #3668]	@ movhi
.L1617:
	movs	r3, #1
	str	r3, [r4, #3276]
	ldrh	r3, [r4, #306]
	str	r3, [sp, #16]
	cmp	r9, #0
	beq	.L1629
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
.L1629:
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
.L1702:
	str	r3, [sp, #20]
	ldrh	r3, [sp, #20]
	cmp	r6, r3
	bls	.L1640
	ldr	r3, [sp, #20]
	movw	r10, #65535
	ldrh	r8, [r5, #178]
	mov	fp, #20
	ldrh	ip, [r5, #236]
	ldr	r0, [r5, #3188]
	add	r8, r8, r3
	ldr	r3, .L1706+12
	str	r3, [sp, #24]
	movs	r3, #0
	str	r3, [sp, #12]
	b	.L1641
.L1634:
	ldr	r1, [sp, #24]
	ldrh	r2, [r1, #2]!
	cmp	r2, r10
	str	r1, [sp, #24]
	beq	.L1633
	ldr	r1, [sp, #12]
	orr	r2, r8, r2, lsl #10
	mla	lr, fp, r1, r0
	str	r2, [lr, #4]
	mov	r2, r1
	adds	r2, r2, #1
	uxth	r2, r2
	str	r2, [sp, #12]
.L1633:
	adds	r3, r3, #1
.L1641:
	uxth	r2, r3
	cmp	ip, r2
	bhi	.L1634
	mov	fp, #0
	ldrb	r2, [r5, #184]	@ zero_extendqisi2
	ldr	r1, [sp, #12]
	bl	FlashReadPages
.L1635:
	ldr	r2, [sp, #12]
	uxth	r3, fp
	cmp	r2, r3
	bhi	.L1639
	ldr	r3, [sp, #20]
	adds	r3, r3, #1
	b	.L1702
.L1639:
	mov	r8, #20
	ldr	r3, [r5, #3188]
	mul	r8, r8, fp
	add	r2, r3, r8
	ldr	r3, [r3, r8]
	adds	r3, r3, #1
	beq	.L1636
	ldr	r10, [r2, #12]
	movw	r3, #61589
	ldrh	r2, [r10]
	cmp	r2, r3
	bne	.L1636
	ldr	r3, [r10, #8]
	adds	r2, r3, #1
	bne	.L1637
	movw	r2, #753
	ldr	r1, .L1706
	ldr	r0, .L1706+4
	str	r3, [sp, #24]
	bl	sftl_printk
	ldr	r3, [sp, #24]
.L1637:
	movs	r2, #0
	mov	r0, r3
	add	r1, sp, #32
	bl	log2phys
	ldr	r0, [r4, #3188]
	ldr	r3, [sp, #32]
	add	r0, r0, r8
	ldr	r2, [r0, #4]
	cmp	r2, r3
	bne	.L1636
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
	beq	.L1638
	ldrh	r3, [r4, #132]
	cbnz	r3, .L1636
.L1638:
	bl	Ftl_gc_temp_data_write_back
	cbz	r0, .L1636
	ldr	r3, .L1706+16
	movs	r2, #0
	movw	r1, #65535
	str	r2, [r3, #3276]
	strh	r1, [r3, #176]	@ movhi
	strh	r2, [r3, #178]	@ movhi
	ldrh	r0, [r3, #3664]
	b	.L1573
.L1636:
	add	fp, fp, #1
	b	.L1635
.L1640:
	ldrh	r3, [r5, #178]
	add	r6, r6, r3
	ldr	r3, [sp, #16]
	uxth	r6, r6
	cmp	r3, r6
	strh	r6, [r5, #178]	@ movhi
	bhi	.L1642
	ldr	r3, [r5, #3172]
	cbz	r3, .L1643
	bl	Ftl_gc_temp_data_write_back
	cbz	r0, .L1643
	movs	r3, #0
	ldrh	r0, [r5, #3664]
	str	r3, [r5, #3276]
	b	.L1573
.L1643:
	ldrh	r6, [r5, #3666]
	cbnz	r6, .L1644
	ldrh	r2, [r5, #176]
	ldr	r3, [r5, #76]
	ldrh	r3, [r3, r2, lsl #1]
	cbz	r3, .L1644
.L1645:
	ldr	r3, [r5, #2556]
	cmp	r6, r3
	bcs	.L1650
	movs	r2, #0
	add	r1, sp, #36
	mov	r0, r6
	bl	log2phys
	ldr	r0, [sp, #36]
	adds	r3, r0, #1
	beq	.L1646
	ubfx	r0, r0, #10, #16
	bl	P2V_block_in_plane
	ldrh	r3, [r5, #176]
	cmp	r3, r0
	bne	.L1646
.L1650:
	ldr	r3, [r5, #2556]
	cmp	r6, r3
	bcc	.L1644
	ldrh	r2, [r5, #176]
	movs	r1, #0
	ldr	r3, [r5, #76]
	strh	r1, [r3, r2, lsl #1]	@ movhi
	ldrh	r0, [r5, #176]
	bl	update_vpc_list
	bl	l2p_flush
	bl	FtlVpcTblFlush
.L1644:
	movw	r3, #65535
	strh	r3, [r5, #176]	@ movhi
.L1642:
	movs	r3, #0
	str	r3, [r5, #3276]
	ldrh	r3, [r5, #228]
	cmp	r3, #2
	bhi	.L1651
	ldrh	r6, [r5, #306]
	b	.L1652
.L1646:
	adds	r6, r6, #1
	b	.L1645
.L1651:
	ldrh	r0, [r5, #3664]
	cmp	r0, #0
	bne	.L1573
	adds	r0, r3, #1
	b	.L1573
.L1656:
	movs	r0, #0
	b	.L1573
.L1579:
	cmp	r9, #0
	beq	.L1654
.L1589:
	ldrh	r2, [r4, #128]
	movw	r3, #65535
	cmp	r2, r3
	bne	.L1665
.L1655:
	ldrh	r3, [r4, #226]
	movw	r2, #65535
	cmp	r3, r2
	bne	.L1665
	cmp	r7, r3
	bne	.L1665
	ldrh	r3, [r4, #176]
	cmp	r3, r7
	beq	.L1601
.L1606:
	movw	r7, #65535
.L1665:
	movs	r6, #1
	b	.L1600
.L1581:
	cmp	r9, #0
	beq	.L1654
	movw	r7, #65535
	b	.L1655
.L1707:
	.align	2
.L1706:
	.word	.LANCHOR1+568
	.word	.LC8
	.word	.LANCHOR0+176
	.word	.LANCHOR0+190
	.word	.LANCHOR0
.L1580:
	cmp	r9, #0
	bne	.L1606
	b	.L1654
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
	bne	.L1709
	mov	r2, r3
	ldr	r1, [sp, #20]
	add	r0, r7, #256
	bl	FtlVendorPartRead
	mov	r8, r0
.L1708:
	mov	r0, r8
	add	sp, sp, #60
	@ sp needed
	pop	{r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L1709:
	ldr	r3, [sp, #20]
	ldr	r4, .L1741
	adds	r3, r1, r3
	str	r3, [sp, #8]
	ldr	r2, [sp, #8]
	ldr	r3, [r4, #344]
	cmp	r2, r3
	bhi	.L1730
	ldr	r3, .L1741+4
	ldr	r3, [r3]
	adds	r5, r3, #1
	beq	.L1731
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
.L1711:
	ldr	r3, [sp, #4]
	cbnz	r3, .L1728
	ldr	r3, .L1741
	ldrh	r2, [r3, #3210]
	cbnz	r2, .L1729
	ldrh	r3, [r3, #228]
	cmp	r3, #31
	bhi	.L1708
.L1729:
	movs	r1, #1
	movs	r0, #0
	bl	rk_ftl_garbage_collect
	b	.L1708
.L1728:
	movs	r2, #0
	add	r1, sp, #52
	mov	r0, r5
	bl	log2phys
	ldr	r2, [sp, #52]
	adds	r0, r2, #1
	bne	.L1712
	mov	fp, #0
.L1713:
	ldrh	r0, [r4, #262]
	cmp	fp, r0
	bcc	.L1715
.L1716:
	ldr	r3, [sp, #4]
	adds	r5, r5, #1
	subs	r3, r3, #1
	str	r3, [sp, #4]
	beq	.L1720
	ldrh	r3, [r4, #236]
	cmp	r6, r3, lsl #2
	bne	.L1711
.L1720:
	cmp	r6, #0
	beq	.L1711
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
.L1727:
	movs	r3, #20
	ldr	r2, [r4, #3292]
	mul	r3, r3, fp
	ldr	r0, [sp, #12]
	add	r2, r2, r3
	ldr	r1, [r2, #16]
	cmp	r0, r1
	bne	.L1722
	ldr	r1, [r2, #8]
	ldr	r2, [r4, #3316]
	cmp	r1, r2
	bne	.L1723
	str	r3, [sp, #44]
	mov	r0, r10
	ldr	r3, [sp, #32]
	ldr	r2, [sp, #36]
	add	r1, r1, r3
.L1740:
	bl	ftl_memcpy
	ldr	r3, [sp, #44]
.L1723:
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
	bne	.L1726
	ldr	r0, [r2, #4]
	ubfx	r0, r0, #10, #16
	bl	P2V_block_in_plane
	bl	FtlGcRefreshBlock
.L1726:
	add	fp, fp, #1
	cmp	r6, fp
	bne	.L1727
	movs	r6, #0
	b	.L1711
.L1715:
	mla	r0, r0, r5, fp
	cmp	r7, r0
	bhi	.L1714
	ldr	r3, [sp, #8]
	cmp	r3, r0
	bls	.L1714
	subs	r0, r0, r7
	mov	r2, #512
	movs	r1, #0
	add	r0, r10, r0, lsl #9
	bl	ftl_memset
.L1714:
	add	fp, fp, #1
	b	.L1713
.L1712:
	ldr	r3, [r4, #3292]
	movs	r1, #20
	ldrh	fp, [r4, #262]
	mla	r3, r1, r6, r3
	str	r2, [r3, #4]
	ldr	r2, [sp, #12]
	cmp	r5, r2
	bne	.L1717
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
	bne	.L1718
	str	r10, [r3, #8]
.L1718:
	ldrh	r2, [r4, #316]
	ldr	r1, [r4, #3340]
	str	r5, [r3, #16]
	muls	r2, r6, r2
	adds	r6, r6, #1
	bic	r2, r2, #3
	add	r2, r2, r1
	str	r2, [r3, #12]
	b	.L1716
.L1717:
	ldr	r2, [sp, #16]
	cmp	r5, r2
	bne	.L1719
	ldr	r2, [r4, #3320]
	ldr	r1, [sp, #8]
	str	r2, [r3, #8]
	mul	r2, fp, r5
	sub	r9, r1, r2
	cmp	fp, r9
	bne	.L1718
.L1739:
	subs	r2, r2, r7
	add	r2, r10, r2, lsl #9
	str	r2, [r3, #8]
	b	.L1718
.L1719:
	mul	r2, r5, fp
	b	.L1739
.L1722:
	ldr	r0, [sp, #16]
	cmp	r0, r1
	bne	.L1723
	ldr	r1, [r2, #8]
	ldr	r2, [r4, #3320]
	cmp	r1, r2
	bne	.L1723
	str	r3, [sp, #44]
	ldrh	r0, [r4, #262]
	ldr	r3, [sp, #16]
	ldr	r2, [sp, #40]
	muls	r0, r3, r0
	subs	r0, r0, r7
	add	r0, r10, r0, lsl #9
	b	.L1740
.L1730:
	mov	r8, #-1
	b	.L1708
.L1731:
	mov	r8, r3
	b	.L1708
.L1742:
	.align	2
.L1741:
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
	bne	.L1745
	mov	r2, r3
	ldr	r1, [sp, #12]
	add	r0, r10, #256
	bl	FtlVendorPartWrite
.L1744:
	add	sp, sp, #68
	@ sp needed
	pop	{r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L1745:
	ldr	r3, [sp, #12]
	ldr	r5, .L1796
	adds	r6, r1, r3
	ldr	r3, [r5, #344]
	cmp	r6, r3
	bhi	.L1782
	ldr	r4, .L1796+4
	ldr	r0, [r4]
	adds	r7, r0, #1
	beq	.L1744
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
	cbz	r3, .L1784
	ldrh	r2, [r5, #32]
	add	r3, r5, #28
	adds	r5, r5, #80
	cmp	r2, #0
	it	ne
	movne	r5, r3
.L1747:
	ldr	r7, [sp, #4]
	ldr	r4, .L1796
.L1748:
	ldr	r3, [sp]
	cbnz	r3, .L1777
	mov	r0, r3
	ldr	r1, [sp, #24]
	bl	rk_ftl_garbage_collect
	ldrh	r3, [r4, #228]
	cmp	r3, #5
	bls	.L1778
.L1780:
	movs	r0, #0
	b	.L1744
.L1784:
	adds	r5, r5, #28
	b	.L1747
.L1777:
	ldrb	r2, [r5, #6]	@ zero_extendqisi2
	ldrh	r3, [r4, #236]
	cmp	r2, r3
	bcc	.L1749
	movw	r2, #1041
	ldr	r1, .L1796+8
	ldr	r0, .L1796+12
	bl	sftl_printk
.L1749:
	ldrh	r2, [r5, #4]
	cbnz	r2, .L1750
	ldr	r3, .L1796+16
	ldr	r6, .L1796+4
	cmp	r5, r3
	bne	.L1751
	ldrh	r8, [r4, #84]
	cmp	r8, #0
	bne	.L1752
	add	r0, r5, #52
	bl	allocate_new_data_superblock
	str	r8, [r6, #8]
.L1752:
	ldr	r5, .L1796+16
	ldr	r0, .L1796+16
	bl	allocate_new_data_superblock
	ldr	r2, [r6, #8]
	add	r3, r5, #52
	cmp	r2, #0
	it	ne
	movne	r5, r3
.L1753:
	ldrh	r3, [r5, #4]
	cbnz	r3, .L1750
	mov	r0, r5
	bl	allocate_new_data_superblock
.L1750:
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
	bcc	.L1754
	movw	r2, #1074
	ldr	r1, .L1796+8
	ldr	r0, .L1796+12
	bl	sftl_printk
.L1754:
	mov	fp, #0
.L1755:
	ldr	r3, [sp, #36]
	cmp	fp, r3
	bne	.L1775
.L1756:
	mov	r3, r5
	movs	r2, #0
	mov	r1, fp
	ldr	r0, [r4, #3300]
	bl	FtlProgPages
	ldr	r3, [sp]
	cmp	fp, r3
	bls	.L1776
	mov	r2, #1152
	ldr	r1, .L1796+8
	ldr	r0, .L1796+12
	bl	sftl_printk
.L1776:
	ldr	r3, [sp]
	sub	r3, r3, fp
	str	r3, [sp]
	b	.L1748
.L1751:
	str	r2, [r6, #8]
	ldrh	r2, [r4, #32]
	cbnz	r2, .L1787
	mov	r0, r5
	bl	allocate_new_data_superblock
	b	.L1753
.L1787:
	mov	r5, r3
	b	.L1750
.L1775:
	ldrh	r3, [r5, #4]
	cmp	r3, #0
	beq	.L1756
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
	beq	.L1757
	ldr	r3, [sp, #20]
	cmp	r7, r3
	bne	.L1792
	ldrh	r2, [r4, #262]
	ldr	r3, [sp, #12]
	smulbb	r2, r2, r7
	add	r6, r10, r3
	movs	r3, #0
	str	r3, [sp, #16]
	subs	r6, r6, r2
	uxth	r6, r6
	b	.L1760
.L1757:
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
.L1760:
	ldrh	r3, [r4, #262]
	cmp	r6, r3
	ldr	r3, [sp, #4]
	bne	.L1761
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
.L1763:
	ldrb	r2, [r5, #6]	@ zero_extendqisi2
	ldrh	r3, [r4, #236]
	cmp	r2, r3
	bcc	.L1772
	movw	r2, #1143
	ldr	r1, .L1796+8
	ldr	r0, .L1796+12
	bl	sftl_printk
.L1772:
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
	b	.L1755
.L1761:
	cmp	r7, r3
	ldr	r3, [r4, #3300]
	ite	eq
	ldreq	r2, [r4, #3316]
	ldrne	r2, [r4, #3320]
	add	r3, r3, r8
	str	r2, [r3, #8]
	ldr	r3, [sp, #40]
	adds	r0, r3, #1
	beq	.L1766
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
	bne	.L1767
	ldr	r2, [r4, #2716]
	ldr	r0, .L1796+20
	adds	r2, r2, #1
	str	r2, [r4, #2716]
	mov	r2, r7
	ldr	r1, [r9, #8]
	bl	sftl_printk
.L1770:
	ldr	r3, [sp, #4]
	lsls	r2, r6, #9
	cmp	r7, r3
	bne	.L1771
	ldr	r3, [r4, #3300]
	ldr	r1, [sp, #8]
	add	r3, r3, r8
	ldr	r0, [r3, #8]
	ldr	r3, [sp, #16]
	add	r0, r0, r3, lsl #9
.L1794:
	bl	ftl_memcpy
	b	.L1763
.L1797:
	.align	2
.L1796:
	.word	.LANCHOR0
	.word	.LANCHOR2
	.word	.LANCHOR1+591
	.word	.LC8
	.word	.LANCHOR0+28
	.word	.LC125
.L1767:
	ldr	r3, [r9, #8]
	cmp	r7, r3
	beq	.L1769
	ldr	r3, [r4, #2716]
	mov	r2, r7
	ldr	r0, .L1798
	adds	r3, r3, #1
	str	r3, [r4, #2716]
	ldr	r1, [r9, #8]
	bl	sftl_printk
.L1769:
	ldr	r3, [r9, #8]
	cmp	r7, r3
	beq	.L1770
	mov	r2, #1128
	ldr	r1, .L1798+4
	ldr	r0, .L1798+8
	bl	sftl_printk
	b	.L1770
.L1766:
	ldr	r3, [r4, #3300]
	movs	r1, #0
	ldrh	r2, [r4, #314]
	add	r3, r3, r8
	ldr	r0, [r3, #8]
	bl	ftl_memset
	b	.L1770
.L1771:
	ldrh	r1, [r4, #262]
	ldr	r0, [sp, #8]
	ldr	r3, [r4, #3300]
	muls	r1, r7, r1
	add	r3, r3, r8
	sub	r1, r1, r10
	add	r1, r0, r1, lsl #9
	ldr	r0, [r3, #8]
	b	.L1794
.L1792:
	ldr	r3, [r4, #3300]
	ldr	r2, [sp, #8]
	add	r8, r8, r3
	ldrh	r3, [r4, #262]
	muls	r3, r7, r3
	sub	r3, r3, r10
	add	r3, r2, r3, lsl #9
	str	r3, [r8, #8]
	b	.L1763
.L1778:
	mov	r5, #256
	movw	r6, #65535
.L1781:
	ldrh	r3, [r4, #176]
	cmp	r3, r6
	bne	.L1779
	ldrh	r3, [r4, #226]
	cmp	r3, r6
	bne	.L1779
	movs	r0, #0
	bl	List_get_gc_head_node
	uxth	r0, r0
	bl	FtlGcRefreshBlock
.L1779:
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
	bhi	.L1780
	subs	r5, r5, #1
	bne	.L1781
	b	.L1780
.L1782:
	mov	r0, #-1
	b	.L1744
.L1799:
	.align	2
.L1798:
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
	ldr	r4, .L1831
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
	bne	.L1802
.L1813:
	mov	r0, #-1
.L1801:
	add	sp, sp, #16
	@ sp needed
	pop	{r4, r5, r6, r7, r8, r9, r10, pc}
.L1802:
	movs	r1, #1
	ldr	r8, .L1831+20
	bl	FtlGetLastWrittenPage
	ldr	r9, .L1831+4
	add	r7, r4, #3456
	ldrsh	r6, [r4, #2628]
	sxth	r5, r0
	adds	r0, r0, #1
	strh	r0, [r4, #2630]	@ movhi
.L1804:
	cmp	r5, #0
	bge	.L1810
	movw	r2, #1465
	ldr	r1, .L1831+4
	ldr	r0, .L1831+8
	bl	sftl_printk
	b	.L1809
.L1810:
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
	beq	.L1805
	ldr	r3, [r4, #3456]
	adds	r3, r3, #1
	beq	.L1805
	ldrh	r1, [r4, #314]
	ldr	r0, [r4, #3464]
	bl	js_hash
	cmp	r10, r0
	beq	.L1805
	ldrh	r3, [r4, #2632]
	mov	r2, r6
	str	r0, [sp, #8]
	mov	r1, r9
	str	r10, [sp, #4]
	str	r3, [sp]
	mov	r3, r5
	ldr	r0, .L1831+12
	bl	sftl_printk
	cbnz	r5, .L1806
	ldrh	r3, [r4, #2632]
	cmp	r6, r3
	beq	.L1806
	ldrh	r5, [r4, #308]
	sxth	r6, r3
.L1808:
	subs	r5, r5, #1
	sxth	r5, r5
	b	.L1804
.L1806:
	mov	r3, #-1
	str	r3, [r4, #3456]
.L1805:
	ldr	r3, [r4, #3456]
	adds	r3, r3, #1
	beq	.L1808
	ldr	r3, [r4, #3304]
	ldr	r3, [r3]
	cmp	r3, r8
	bne	.L1808
	ldr	r3, [r4, #3336]
	ldrh	r2, [r3]
	movw	r3, #61604
	cmp	r2, r3
	bne	.L1808
.L1809:
	ldrh	r3, [r4, #244]
	ldrh	r2, [r4, #314]
	adds	r3, r3, #24
	cmp	r2, r3, lsl #1
	bcs	.L1812
	movw	r2, #1467
	ldr	r1, .L1831+4
	ldr	r0, .L1831+8
	bl	sftl_printk
.L1812:
	movs	r2, #48
	ldr	r1, [r4, #3464]
	ldr	r0, .L1831+16
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
	ldr	r3, .L1831+20
	cmp	r2, r3
	bne	.L1813
	ldrb	r2, [r4, #2482]	@ zero_extendqisi2
	ldrh	r3, [r4, #258]
	ldrh	r5, [r4, #2480]
	cmp	r2, r3
	strh	r5, [r4, #2634]	@ movhi
	bne	.L1813
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
	bls	.L1814
	movw	r2, #1489
	ldr	r1, .L1831+4
	ldr	r0, .L1831+8
	bl	sftl_printk
.L1814:
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
	beq	.L1817
	ldr	r0, .L1831+24
	bl	make_superblock
.L1817:
	ldrh	r2, [r4, #80]
	movw	r3, #65535
	cmp	r2, r3
	beq	.L1818
	ldr	r0, .L1831+28
	bl	make_superblock
.L1818:
	ldrh	r2, [r4, #128]
	movw	r3, #65535
	cmp	r2, r3
	beq	.L1819
	ldr	r0, .L1831+32
	bl	make_superblock
.L1819:
	ldrh	r2, [r4, #176]
	movw	r3, #65535
	cmp	r2, r3
	beq	.L1820
	ldr	r0, .L1831+36
	bl	make_superblock
.L1820:
	movs	r0, #0
	b	.L1801
.L1832:
	.align	2
.L1831:
	.word	.LANCHOR0
	.word	.LANCHOR1+600
	.word	.LC8
	.word	.LC127
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
	ldr	r4, .L1874
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
.L1834:
	ldr	r3, [sp, #12]
	sxth	r6, r8
	cmp	r6, r3
	bge	.L1853
	ldr	r3, [sp, #12]
	lsls	r7, r6, #1
	subs	r3, r3, #1
	cmp	r6, r3
	bne	.L1835
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
.L1836:
	ldr	r3, [sp, #12]
	sxth	r9, r8
	adds	r2, r3, #1
	cmp	r9, r2
	blt	.L1839
.L1853:
	mov	r0, r5
	bl	ftl_free_no_use_map_blk
	ldrh	r2, [r5, #2]
	ldrh	r3, [r4, #308]
	cmp	r2, r3
	bne	.L1841
	mov	r0, r5
	bl	ftl_map_blk_alloc_new_blk
.L1841:
	mov	r0, r5
	bl	ftl_map_blk_gc
	mov	r0, r5
	bl	ftl_map_blk_gc
	movs	r0, #0
	add	sp, sp, #36
	@ sp needed
	pop	{r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L1839:
	ldrh	r2, [r7]
	ldr	r0, .L1874+4
	orr	r2, r9, r2, lsl #10
	str	r2, [r4, #3460]
	movs	r2, #1
	mov	r1, r2
	bl	FlashReadPages
	ldr	r2, [r4, #3468]
	ldr	r2, [r2, #12]
	str	r2, [sp, #16]
	cbz	r2, .L1837
	ldr	r1, [r4, #3456]
	adds	r1, r1, #1
	beq	.L1837
	ldrh	r1, [r4, #314]
	ldr	r0, [r4, #3464]
	bl	js_hash
	ldr	r2, [sp, #16]
	cmp	r2, r0
	beq	.L1837
	str	r0, [sp, #4]
	mov	r3, r9
	str	r2, [sp]
	mov	r2, r6
	ldr	r1, .L1874+8
	ldr	r0, .L1874+12
	bl	sftl_printk
	mov	r3, #-1
	str	r3, [r4, #3456]
.L1837:
	ldr	r3, [r4, #3456]
	adds	r3, r3, #1
	beq	.L1838
	ldrh	r3, [fp, #8]
	cmp	r10, r3
	bls	.L1838
	ldrh	r1, [fp]
	ldrh	r2, [r5, #4]
	cmp	r1, r2
	ittt	eq
	ldreq	r2, [r4, #3460]
	ldreq	r1, [sp, #8]
	streq	r2, [r1, r3, lsl #2]
.L1838:
	add	r8, r8, #1
	b	.L1836
.L1835:
	ldr	r3, [r4, #3304]
	ldr	r0, .L1874+4
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
	beq	.L1855
	ldrh	r2, [fp]
	ldrh	r3, [r5, #4]
	cmp	r2, r3
	bne	.L1855
	ldrh	r2, [fp, #8]
	movw	r3, #64245
	cmp	r2, r3
	beq	.L1843
.L1855:
	movs	r7, #0
.L1844:
	ldrh	r2, [r4, #308]
	sxth	r3, r7
	cmp	r3, r2
	bge	.L1851
	ldr	r2, [sp, #20]
	ldr	r0, .L1874+4
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
	cbz	r2, .L1848
	ldr	r1, [r4, #3456]
	adds	r1, r1, #1
	beq	.L1848
	ldrh	r1, [r4, #314]
	ldr	r0, [r4, #3464]
	bl	js_hash
	ldr	r2, [sp, #24]
	cmp	r2, r0
	beq	.L1848
	str	r0, [sp, #4]
	str	r2, [sp]
	mov	r2, r6
	ldr	r3, [sp, #28]
	ldr	r1, .L1874+8
	ldr	r0, .L1874+16
	bl	sftl_printk
	mov	r3, #-1
	str	r3, [r4, #3456]
.L1848:
	ldr	r3, [r4, #3456]
	adds	r3, r3, #1
	beq	.L1849
	ldrh	r3, [fp, #8]
	cmp	r10, r3
	bls	.L1849
	ldrh	r1, [fp]
	ldrh	r2, [r5, #4]
	cmp	r1, r2
	ittt	eq
	ldreq	r2, [r4, #3460]
	ldreq	r1, [sp, #8]
	streq	r2, [r1, r3, lsl #2]
.L1849:
	adds	r7, r7, #1
	b	.L1844
.L1843:
	movs	r1, #0
	movs	r0, #4
.L1845:
	ldrh	r2, [r4, #308]
	sxth	r3, r1
	subs	r2, r2, #1
	cmp	r3, r2
	blt	.L1847
.L1851:
	add	r8, r8, #1
	b	.L1834
.L1847:
	ldr	r6, [r4, #3304]
	ldr	r2, [r6, r3, lsl #3]
	uxth	r7, r2
	cmp	r10, r7
	bls	.L1846
	add	r3, r0, r3, lsl #3
	ldr	r3, [r6, r3]
	ldr	r6, [sp, #8]
	str	r3, [r6, r7, lsl #2]
.L1846:
	adds	r1, r1, #1
	b	.L1845
.L1875:
	.align	2
.L1874:
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
	ldr	r0, .L1877
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
.L1878:
	.align	2
.L1877:
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
	ldr	r0, .L1880
	bl	FtlMapTblRecovery
	movs	r0, #0
	pop	{r3, pc}
.L1881:
	.align	2
.L1880:
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
	ldr	r4, .L1898
	ldrh	r0, [r4, #240]
	strh	r3, [r4, #3476]	@ movhi
	bl	FtlFreeSysBlkQueueInit
	bl	FtlScanSysBlk
	ldrh	r2, [r4, #2628]
	movw	r3, #65535
	cmp	r2, r3
	bne	.L1883
.L1885:
	mov	r5, #-1
.L1882:
	mov	r0, r5
	pop	{r4, r5, r6, pc}
.L1883:
	bl	FtlLoadSysInfo
	mov	r5, r0
	cmp	r0, #0
	bne	.L1885
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
.L1886:
	cmp	r3, r1
	bge	.L1891
	mla	r6, r0, r3, r2
	ldr	r6, [r6, #4]
	cmp	r6, #0
	bge	.L1887
.L1891:
	ldrh	r2, [r4, #2500]
	cmp	r3, r1
	add	r2, r2, #1
	strh	r2, [r4, #2500]	@ movhi
	bge	.L1897
.L1888:
	ldr	r0, .L1898+4
	bl	FtlSuperblockPowerLostFix
	ldr	r0, .L1898+8
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
	b	.L1892
.L1887:
	adds	r3, r3, #1
	b	.L1886
.L1897:
	ldrh	r3, [r4, #3476]
	cmp	r3, #0
	bne	.L1888
.L1892:
	ldrh	r0, [r4, #28]
	movw	r3, #65535
	cmp	r0, r3
	beq	.L1893
	ldrh	r3, [r4, #32]
	cbnz	r3, .L1893
	ldrh	r3, [r4, #84]
	cbnz	r3, .L1893
	bl	FtlGcRefreshBlock
	ldrh	r0, [r4, #80]
	bl	FtlGcRefreshBlock
	bl	FtlVpcTblFlush
	ldr	r0, .L1898+4
	bl	allocate_new_data_superblock
	ldr	r0, .L1898+8
	bl	allocate_new_data_superblock
.L1893:
	bl	FtlVpcCheckAndModify
	b	.L1882
.L1899:
	.align	2
.L1898:
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
	ldr	r4, .L1924
	ldrh	r0, [r4, #240]
	str	r3, [r4, #2592]
	str	r3, [r4, #2596]
	str	r3, [r4, #2608]
	bl	FtlFreeSysBlkQueueInit
	bl	FtlLoadBbt
	cbz	r0, .L1901
	bl	FtlMakeBbt
.L1901:
	ldr	r0, .L1924+4
	movs	r2, #0
.L1902:
	ldrh	r1, [r4, #262]
	uxth	r3, r2
	adds	r2, r2, #1
	cmp	r3, r1, lsl #7
	blt	.L1903
	ldrh	r6, [r4, #244]
	movs	r5, #0
.L1904:
	ldrh	r3, [r4, #246]
	cmp	r3, r6
	bhi	.L1905
	ldrh	r1, [r4, #236]
	subs	r3, r5, #3
	cmp	r3, r1, lsl #1
	bge	.L1906
.L1910:
	movs	r5, #0
	mov	r6, r5
.L1907:
	ldrh	r3, [r4, #244]
	uxth	r0, r5
	adds	r5, r5, #1
	cmp	r3, r0
	bhi	.L1911
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
	ble	.L1912
	mov	r1, r5
	sub	r0, r9, r6
	bl	__aeabi_uidiv
	str	r0, [r4, #2556]
	lsrs	r0, r0, #5
	adds	r0, r0, #24
	strh	r0, [r4, #2624]	@ movhi
.L1912:
	ldrh	r3, [r4, #298]
	cbz	r3, .L1914
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
.L1914:
	ldrh	r2, [r4, #2624]
	ldr	r3, [r4, #2556]
	ldr	r6, .L1924+8
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
.L1916:
	mov	r0, r6
	bl	make_superblock
	ldrb	r3, [r4, #35]	@ zero_extendqisi2
	ldrh	r2, [r4, #28]
	cmp	r3, #0
	bne	.L1917
	ldr	r3, [r4, #76]
	strh	r5, [r3, r2, lsl #1]	@ movhi
	ldrh	r3, [r4, #28]
	adds	r3, r3, #1
	strh	r3, [r4, #28]	@ movhi
	b	.L1916
.L1903:
	ldr	r5, [r4, #3316]
	mvns	r1, r3
	orr	r1, r3, r1, lsl #16
	str	r1, [r5, r3, lsl #2]
	ldr	r1, [r4, #3320]
	str	r0, [r1, r3, lsl #2]
	b	.L1902
.L1905:
	mov	r0, r6
	movs	r1, #1
	bl	FtlLowFormatEraseBlock
	adds	r6, r6, #1
	add	r5, r5, r0
	uxth	r5, r5
	uxth	r6, r6
	b	.L1904
.L1906:
	mov	r0, r5
	bl	__aeabi_uidiv
	ldr	r3, [r4, #336]
	add	r0, r0, r3
	uxth	r0, r0
	bl	FtlSysBlkNumInit
	ldrh	r0, [r4, #240]
	bl	FtlFreeSysBlkQueueInit
	ldrh	r5, [r4, #244]
.L1908:
	ldrh	r3, [r4, #246]
	cmp	r3, r5
	bls	.L1910
	mov	r0, r5
	movs	r1, #1
	adds	r5, r5, #1
	bl	FtlLowFormatEraseBlock
	uxth	r5, r5
	b	.L1908
.L1911:
	movs	r1, #0
	bl	FtlLowFormatEraseBlock
	add	r6, r6, r0
	uxth	r6, r6
	b	.L1907
.L1917:
	ldr	r3, [r4, #2592]
	movw	r5, #65535
	ldrh	r1, [r4, #32]
	ldr	r6, .L1924+12
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
.L1918:
	mov	r0, r6
	bl	make_superblock
	ldrb	r3, [r4, #87]	@ zero_extendqisi2
	ldrh	r2, [r4, #80]
	cbnz	r3, .L1919
	ldr	r3, [r4, #76]
	strh	r5, [r3, r2, lsl #1]	@ movhi
	ldrh	r3, [r4, #80]
	adds	r3, r3, #1
	strh	r3, [r4, #80]	@ movhi
	b	.L1918
.L1919:
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
	cbnz	r0, .L1920
	ldr	r3, .L1924+16
	movs	r2, #1
	str	r2, [r3]
.L1920:
	movs	r0, #0
	pop	{r3, r4, r5, r6, r7, r8, r9, pc}
.L1925:
	.align	2
.L1924:
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
	ldr	r4, .L1932
	ldr	r5, .L1932+4
	ldr	r1, .L1932+8
	ldr	r0, .L1932+12
	str	r3, [r5]
	bl	sftl_printk
	mov	r0, r4
	bl	FtlConstantsInit
	bl	FtlMemInit
	bl	FtlVariablesInit
	ldrh	r0, [r4, #240]
	bl	FtlFreeSysBlkQueueInit
	bl	FtlLoadBbt
	cbnz	r0, .L1930
	bl	FtlSysBlkInit
	cbnz	r0, .L1930
	movs	r3, #1
	str	r3, [r5]
	ldrh	r3, [r4, #228]
	cmp	r3, #15
	bhi	.L1930
	movw	r4, #8129
.L1929:
	movs	r1, #1
	movs	r0, #0
	bl	rk_ftl_garbage_collect
	subs	r4, r4, #1
	bne	.L1929
.L1930:
	movs	r0, #0
	pop	{r3, r4, r5, pc}
.L1933:
	.align	2
.L1932:
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
	ldr	r5, .L2004
	bls	.L1935
	cmp	r0, #576
	bcc	.L1936
.L1935:
	ldr	r3, [r5, #3680]
	cbnz	r3, .L1937
.L2003:
	movs	r4, #0
	b	.L1934
.L1937:
	ldr	r3, [r5, #3684]
	ldr	r2, [r3]
	str	r3, [sp, #12]
	ldr	r3, .L2004+4
	cmp	r2, r3
	bne	.L1939
	ldr	r3, [sp, #12]
	movs	r2, #0
	ldrh	r1, [r5, #10]
	add	r0, r3, #260096
	movw	r3, #65023
.L1943:
	ldr	r4, [r0, #-4]!
	cbnz	r4, .L1940
	ldr	r4, [sp, #12]
	subs	r3, r3, #1
	ldr	r4, [r4, r2, lsl #2]
	adds	r2, r2, #1
	cmp	r2, #4096
	it	hi
	movhi	r2, #0
	cmp	r3, #4096
	str	r4, [r0, #2048]
	bne	.L1943
	mov	r3, #512
	b	.L2000
.L1940:
	adds	r3, r3, #127
	lsrs	r3, r3, #7
.L2000:
	lsls	r1, r1, #2
	str	r3, [sp, #8]
	ldr	r0, [sp, #8]
	uxth	r1, r1
	ldr	r4, .L2004
	bl	__aeabi_uidiv
	adds	r3, r0, #1
	ldr	r1, [sp, #8]
	mov	r2, r3
	ldr	r0, .L2004+8
	str	r3, [sp, #16]
	bl	sftl_printk
	ldr	r3, [sp, #8]
	lsls	r3, r3, #7
	str	r3, [sp, #28]
	movs	r3, #0
	mov	fp, r3
	str	r3, [sp, #20]
.L1965:
	mov	r2, #512
	movs	r1, #0
	ldr	r0, [r4, #3688]
	bl	memset
	ldrh	r7, [r4, #10]
	ldr	r3, [r4, #3264]
	mul	r9, fp, r7
	cmp	r3, #0
	beq	.L1973
	ldr	r3, [r4, #3268]
	cmp	r3, #0
	ite	eq
	moveq	r3, #6
	movne	r3, #9
.L2001:
	str	r3, [sp, #24]
	mov	r1, r9
	ldr	r3, .L2004
	movs	r0, #0
	ldr	r3, [r3, #3252]
	blx	r3
	ldr	r3, [sp, #16]
	cmp	r3, #1
	beq	.L1945
	ldr	r3, .L2004
	add	r1, r7, r9
	movs	r0, #0
	ldr	r3, [r3, #3252]
	blx	r3
.L1945:
	ldr	r3, [sp, #24]
	cmp	r3, #9
	bne	.L1975
	ldr	r3, .L2004
	mov	r2, #1024
	movs	r1, #0
	ldr	r5, [r3, #3688]
	mov	r0, r5
	bl	ftl_memset
	movs	r2, #4
	ldr	r3, .L2004+12
	strb	r2, [r5, #17]
	movs	r1, #12
	ldr	r2, .L2004
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
.L1946:
	ldr	r3, [sp, #16]
	mov	r10, r9
	mov	r8, #0
	muls	r3, r7, r3
	ldr	r7, [sp, #12]
	str	r3, [sp, #32]
.L1947:
	ldr	r3, [sp, #32]
	cmp	r8, r3
	beq	.L1954
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
	bne	.L1950
	ldr	r3, [sp, #24]
	cmp	r3, #9
	bne	.L1950
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
	bne	.L1951
.L1954:
	ldrb	r3, [r4, #14]	@ zero_extendqisi2
	ldr	r2, [r4, #3264]
	ldr	r8, [r4, #3688]
	str	r3, [sp, #24]
	ldrh	r3, [r4, #10]
	mul	r10, fp, r3
	cmp	r2, #0
	bne	.L1952
	mov	r9, #6
.L1953:
	ldr	r2, [sp, #16]
	movs	r7, #0
	mul	r5, r3, r2
.L1956:
	cmp	r7, r5
	beq	.L1960
	cmp	r7, #0
	bne	.L1957
	cmp	r9, #9
	bne	.L1957
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
	ldr	r2, .L2004+12
	cmp	r3, r2
	beq	.L1958
.L1960:
	ldr	r0, [r4, #3688]
	movs	r5, #0
	ldr	r1, [sp, #12]
	mov	ip, r0
.L1959:
	mov	r7, ip
	mov	r8, r1
	ldr	r2, [r7]
	add	ip, ip, #4
	ldr	r3, [r8]
	adds	r1, r1, #4
	cmp	r2, r3
	beq	.L1962
	mov	r2, #512
	movs	r1, #0
	bl	memset
	str	r5, [sp]
	mov	r1, fp
	ldr	r3, [r8]
	ldr	r0, .L2004+16
	ldr	r2, [r7]
	bl	sftl_printk
	ldr	r3, .L2004
	movs	r0, #0
	ldrh	r1, [r3, #10]
	ldr	r3, [r3, #3252]
	mul	r1, fp, r1
	blx	r3
.L1963:
	ldr	r3, [sp, #16]
	add	fp, fp, r3
	cmp	fp, #15
	bls	.L1965
	ldr	r3, [sp, #20]
	cbnz	r3, .L1966
.L1939:
	mov	r3, #-1
	str	r3, [sp, #8]
.L1966:
	ldr	r5, .L2004
	movs	r3, #0
	ldr	r4, [sp, #8]
	ldr	r0, [r5, #3684]
	str	r3, [r5, #3680]
	bl	kfree
	ldr	r0, [r5, #3688]
	bl	kfree
.L1934:
	mov	r0, r4
	add	sp, sp, #108
	@ sp needed
	pop	{r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L1973:
	movs	r3, #6
	b	.L2001
.L1975:
	movs	r5, #0
	b	.L1946
.L1950:
	add	r3, sp, #40
	mov	r2, r7
	mov	r1, r10
	movs	r0, #0
	ldr	r6, [r4, #3256]
	blx	r6
	adds	r0, r0, #1
	beq	.L1954
	add	r7, r7, #2048
.L1951:
	add	r8, r8, #1
	add	r10, r10, #1
	b	.L1947
.L1952:
	ldr	r2, [r4, #3268]
	cmp	r2, #0
	ite	eq
	moveq	r9, #6
	movne	r9, #9
	b	.L1953
.L1957:
	add	r3, sp, #40
	mov	r2, r8
	add	r1, r10, r7
	movs	r0, #0
	ldr	r6, [r4, #3260]
	blx	r6
	adds	r0, r0, #1
	beq	.L1960
	ldr	r2, [sp, #44]
	movw	r3, #61424
	cmp	r2, r3
	bne	.L1960
	add	r8, r8, #2048
.L1958:
	adds	r7, r7, #1
	b	.L1956
.L1962:
	ldr	r3, [sp, #28]
	adds	r5, r5, #1
	cmp	r5, r3
	bne	.L1959
	ldr	r3, [sp, #20]
	adds	r3, r3, #1
	cmp	r3, #5
	str	r3, [sp, #20]
	bls	.L1963
	b	.L1966
.L2005:
	.align	2
.L2004:
	.word	.LANCHOR0
	.word	-52655045
	.word	.LC130
	.word	1179535694
	.word	.LC131
.L1936:
	cmp	r0, #64
	bne	.L1967
	mov	r0, #262144
	bl	ftl_malloc
	str	r0, [r5, #3684]
	mov	r0, #262144
	bl	ftl_malloc
	ldr	r3, [r5, #3684]
	str	r0, [r5, #3688]
	cbz	r3, .L1968
	cbz	r0, .L1968
	movs	r2, #1
	movs	r1, #0
	str	r2, [r5, #3680]
	mov	r0, r3
	mov	r2, #262144
	bl	ftl_memset
.L1967:
	ldr	r3, [r5, #3680]
	cmp	r3, #0
	beq	.L2003
	cmp	r4, #63
	ldr	r1, [r5, #3684]
	iteee	hi
	ldrhi	r5, .L2006
	rsbls	r4, r4, #64
	subls	r6, r6, r4
	addls	r9, r9, r4, lsl #9
	it	ls
	movls	r5, r1
	ldr	r0, .L2006+4
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
	b	.L2003
.L1968:
	ldr	r1, .L2006+8
	ldr	r0, .L2006+12
	bl	sftl_printk
	b	.L1967
.L2007:
	.align	2
.L2006:
	.word	8388544
	.word	.LC133
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
	ldr	r2, .L2012
	ldr	r3, [r2, #3692]
	cbnz	r3, .L2011
	str	r0, [r2, #3692]
	mov	r0, r3
	str	r1, [r2, #3696]
	bx	lr
.L2011:
	mov	r0, #-1
	bx	lr
.L2013:
	.align	2
.L2012:
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
	ldr	r5, .L2022
	bl	ftl_malloc
	str	r0, [r5, #3700]
	cmp	r0, #0
	beq	.L2020
	ldr	r10, .L2022+4
	mov	r9, #0
	mov	r7, r9
	mov	r8, r9
.L2018:
	ldr	r3, [r5, #3692]
	movs	r1, #128
	ldr	r2, [r5, #3700]
	lsl	r0, r8, #7
	blx	r3
	mov	r6, r0
	cbnz	r0, .L2016
	ldr	r4, [r5, #3700]
	ldr	r3, [r4]
	cmp	r3, r10
	bne	.L2017
	movw	r3, #65532
	ldr	r2, [r4, #4]
	ldr	r3, [r4, r3]
	cmp	r3, r2
	bne	.L2017
	cmp	r3, r7
	itt	hi
	movhi	r9, r8
	movhi	r7, r3
.L2017:
	add	r8, r8, #1
	cmp	r8, #2
	bne	.L2018
	cbz	r7, .L2019
	ldr	r3, [r5, #3692]
	mov	r2, r4
	movs	r1, #128
	lsl	r0, r9, #7
	blx	r3
	mov	r6, r0
	cbz	r0, .L2014
.L2016:
	ldr	r0, [r5, #3700]
	mov	r6, #-1
	bl	kfree
	movs	r3, #0
	str	r3, [r5, #3700]
	b	.L2014
.L2019:
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
.L2014:
	mov	r0, r6
	pop	{r4, r5, r6, r7, r8, r9, r10, pc}
.L2020:
	mvn	r6, #11
	b	.L2014
.L2023:
	.align	2
.L2022:
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
	ldr	r3, .L2030
	mov	r0, r1
	ldr	r5, [r3, #3700]
	cbz	r5, .L2029
	ldrh	r6, [r5, #10]
	movs	r3, #0
.L2026:
	cmp	r3, r6
	bcc	.L2028
.L2029:
	mov	r0, #-1
	pop	{r3, r4, r5, r6, r7, pc}
.L2028:
	add	r1, r5, r3, lsl #3
	ldrh	r4, [r1, #16]
	cmp	r4, r7
	bne	.L2027
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
.L2027:
	adds	r3, r3, #1
	b	.L2026
.L2031:
	.align	2
.L2030:
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
	ldr	r2, .L2051
	.pad #28
	sub	sp, sp, #28
	mov	r3, r1
	ldr	r4, [r2, #3700]
	mov	r9, r2
	cmp	r4, #0
	beq	.L2047
	ldrh	r2, [r4, #10]
	add	r6, r8, #63
	ldrh	r1, [r4, #8]
	bic	r6, r6, #63
	movs	r7, #0
	str	r1, [sp, #4]
.L2034:
	cmp	r7, r2
	bcc	.L2042
	ldrh	r1, [r4, #14]
	cmp	r6, r1
	bhi	.L2047
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
	b	.L2050
.L2042:
	add	r5, r4, r7, lsl #3
	ldrh	r1, [r5, #16]
	cmp	r1, r0
	str	r1, [sp, #8]
	bne	.L2035
	ldrh	r1, [r5, #20]
	add	fp, r4, #1024
	adds	r1, r1, #63
	bic	r1, r1, #63
	cmp	r8, r1
	str	r1, [sp, #12]
	bls	.L2036
	ldrh	r1, [r4, #14]
	cmp	r6, r1
	bhi	.L2047
	ldrh	r10, [r5, #18]
	subs	r2, r2, #1
	str	r2, [sp, #16]
.L2037:
	ldr	r2, [sp, #16]
	adds	r5, r5, #8
	cmp	r7, r2
	bcc	.L2038
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
.L2039:
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
	ldr	r3, .L2051
	ldr	r3, [r3, #3696]
.L2050:
	ldr	r0, [sp, #4]
	mov	r2, r4
	movs	r1, #128
	lsls	r0, r0, #7
	blx	r3
	movs	r0, #0
.L2032:
	add	sp, sp, #28
	@ sp needed
	pop	{r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L2038:
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
	b	.L2037
.L2036:
	ldrh	r0, [r5, #18]
	mov	r2, r8
	mov	r1, r3
	add	r0, r0, fp
	bl	memcpy
	strh	r8, [r5, #20]	@ movhi
	b	.L2039
.L2035:
	adds	r7, r7, #1
	b	.L2034
.L2047:
	mov	r0, #-1
	b	.L2032
.L2052:
	.align	2
.L2051:
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
	beq	.L2069
	ldr	r3, .L2077
	cmp	r6, r3
	beq	.L2056
	adds	r3, r3, #1
	cmp	r6, r3
	beq	.L2057
.L2075:
	mvn	r5, #13
	b	.L2055
.L2056:
	ldr	r6, .L2077+4
	mov	r3, sp
	ands	r3, r3, r6
	ldr	r3, [r3, #8]
	.syntax unified
@ 114 "include/linux/uaccess.h" 1
	adds r2, r5, #8; sbcccs r2, r2, r3; movcc r3, #0
@ 0 "" 2
	.thumb
	.syntax unified
	movs	r2, #8
	cbnz	r3, .L2058
	mov	r1, r5
	bl	arm_copy_from_user
	mov	r2, r0
	cmp	r0, #0
	beq	.L2059
.L2058:
	rsb	r0, r2, #8
	movs	r1, #0
	add	r0, r0, r4
	bl	memset
	mov	r2, r5
	mov	r1, #256
.L2076:
	ldr	r0, .L2077+8
	bl	sftl_printk
	b	.L2075
.L2060:
	mov	r3, sp
	uxth	r2, r0
	ands	r6, r6, r3
	strh	r0, [r4, #6]	@ movhi
	adds	r2, r2, #8
	ldr	r3, [r6, #8]
	.syntax unified
@ 132 "include/linux/uaccess.h" 1
	adds r1, r5, r2; sbcccs r1, r1, r3; movcc r3, #0
@ 0 "" 2
	.thumb
	.syntax unified
	cmp	r3, #0
	bne	.L2075
	mov	r1, r4
	mov	r0, r5
	bl	arm_copy_to_user
	cmp	r0, #0
	bne	.L2075
.L2072:
	mov	r5, r0
	b	.L2055
.L2057:
	ldr	r7, .L2077+4
	mov	r3, sp
	ands	r3, r3, r7
	ldr	r3, [r3, #8]
	.syntax unified
@ 114 "include/linux/uaccess.h" 1
	adds r2, r5, #8; sbcccs r2, r2, r3; movcc r3, #0
@ 0 "" 2
	.thumb
	.syntax unified
	movs	r2, #8
	cbnz	r3, .L2062
	mov	r1, r5
	bl	arm_copy_from_user
	mov	r2, r0
	cbz	r0, .L2063
.L2062:
	rsb	r0, r2, #8
	movs	r1, #0
	add	r0, r0, r4
	bl	memset
	mov	r2, r5
	mov	r1, #276
	b	.L2076
.L2069:
	mov	r5, #-1
	b	.L2053
.L2059:
	ldr	r2, [r4]
	ldr	r3, .L2077+12
	cmp	r2, r3
	bne	.L2064
	ldrh	r2, [r4, #6]
	add	r1, r4, #8
	ldrh	r0, [r4, #4]
	bl	rk_sftl_vendor_read
	adds	r3, r0, #1
	bne	.L2060
.L2064:
	mov	r5, #-1
.L2055:
	mov	r0, r4
	bl	kfree
.L2053:
	mov	r0, r5
	pop	{r3, r4, r5, r6, r7, pc}
.L2063:
	ldr	r2, [r4]
	ldr	r3, .L2077+12
	cmp	r2, r3
	bne	.L2064
	ldrh	r6, [r4, #6]
	movw	r3, #4087
	cmp	r6, r3
	bhi	.L2064
	mov	r3, sp
	adds	r6, r6, #8
	ands	r7, r7, r3
	ldr	r3, [r7, #8]
	.syntax unified
@ 114 "include/linux/uaccess.h" 1
	adds r2, r5, r6; sbcccs r2, r2, r3; movcc r3, #0
@ 0 "" 2
	.thumb
	.syntax unified
	mov	r2, r6
	cbnz	r3, .L2065
	mov	r1, r5
	mov	r0, r4
	bl	arm_copy_from_user
	mov	r2, r0
	cbz	r0, .L2066
.L2065:
	subs	r6, r6, r2
	movs	r1, #0
	adds	r0, r4, r6
	bl	memset
	mov	r2, r5
	movw	r1, #283
	b	.L2076
.L2066:
	ldrh	r2, [r4, #6]
	add	r1, r4, #8
	ldrh	r0, [r4, #4]
	bl	rk_sftl_vendor_write
	b	.L2072
.L2078:
	.align	2
.L2077:
	.word	1074034177
	.word	-8192
	.word	.LC134
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
	ldr	r0, .L2080
	b	misc_register
.L2081:
	.align	2
.L2080:
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
	.type	__func__.7557, %object
	.size	__func__.7557, 17
__func__.7557:
	.ascii	"INSERT_DATA_LIST\000"
	.type	__func__.7552, %object
	.size	__func__.7552, 17
__func__.7552:
	.ascii	"INSERT_FREE_LIST\000"
	.type	__func__.7588, %object
	.size	__func__.7588, 17
__func__.7588:
	.ascii	"List_remove_node\000"
	.type	__func__.7620, %object
	.size	__func__.7620, 22
__func__.7620:
	.ascii	"List_update_data_list\000"
	.type	__func__.7727, %object
	.size	__func__.7727, 22
__func__.7727:
	.ascii	"select_l2p_ram_region\000"
	.type	__func__.8054, %object
	.size	__func__.8054, 16
__func__.8054:
	.ascii	"make_superblock\000"
	.type	__func__.8229, %object
	.size	__func__.8229, 19
__func__.8229:
	.ascii	"get_new_active_ppa\000"
	.type	__func__.13845, %object
	.size	__func__.13845, 17
__func__.13845:
	.ascii	"FlashEraseBlocks\000"
	.type	__func__.7662, %object
	.size	__func__.7662, 26
__func__.7662:
	.ascii	"ftl_map_blk_alloc_new_blk\000"
	.type	__func__.7167, %object
	.size	__func__.7167, 11
__func__.7167:
	.ascii	"FtlMemInit\000"
	.type	__func__.7394, %object
	.size	__func__.7394, 14
__func__.7394:
	.ascii	"FtlBbt2Bitmap\000"
	.type	__func__.8075, %object
	.size	__func__.8075, 18
__func__.8075:
	.ascii	"SupperBlkListInit\000"
	.type	__func__.13802, %object
	.size	__func__.13802, 15
__func__.13802:
	.ascii	"FlashReadPages\000"
	.type	__func__.7839, %object
	.size	__func__.7839, 14
__func__.7839:
	.ascii	"FtlScanSysBlk\000"
	.type	__func__.7437, %object
	.size	__func__.7437, 11
__func__.7437:
	.ascii	"FtlLoadBbt\000"
	.type	__func__.13821, %object
	.size	__func__.13821, 15
__func__.13821:
	.ascii	"FlashProgPages\000"
	.type	__func__.8201, %object
	.size	__func__.8201, 25
__func__.8201:
	.ascii	"allocate_data_superblock\000"
	.type	__func__.8242, %object
	.size	__func__.8242, 16
__func__.8242:
	.ascii	"update_vpc_list\000"
	.type	__func__.8249, %object
	.size	__func__.8249, 20
__func__.8249:
	.ascii	"decrement_vpc_count\000"
	.type	__func__.7688, %object
	.size	__func__.7688, 31
__func__.7688:
	.ascii	"Ftl_write_map_blk_to_last_page\000"
	.type	__func__.7702, %object
	.size	__func__.7702, 16
__func__.7702:
	.ascii	"FtlMapWritePage\000"
	.type	__func__.7629, %object
	.size	__func__.7629, 16
__func__.7629:
	.ascii	"load_l2p_region\000"
	.type	__func__.7673, %object
	.size	__func__.7673, 15
__func__.7673:
	.ascii	"ftl_map_blk_gc\000"
	.type	__func__.7744, %object
	.size	__func__.7744, 9
__func__.7744:
	.ascii	"log2phys\000"
	.type	__func__.7963, %object
	.size	__func__.7963, 16
__func__.7963:
	.ascii	"FtlReUsePrevPpa\000"
	.type	__func__.7997, %object
	.size	__func__.7997, 22
__func__.7997:
	.ascii	"FtlRecoverySuperblock\000"
	.type	__func__.8116, %object
	.size	__func__.8116, 14
__func__.8116:
	.ascii	"ftl_check_vpc\000"
	.type	__func__.7817, %object
	.size	__func__.7817, 15
__func__.7817:
	.ascii	"FtlVpcTblFlush\000"
	.type	__func__.8100, %object
	.size	__func__.8100, 21
__func__.8100:
	.ascii	"FtlVpcCheckAndModify\000"
	.type	__func__.8222, %object
	.size	__func__.8222, 29
__func__.8222:
	.ascii	"allocate_new_data_superblock\000"
	.type	__func__.7291, %object
	.size	__func__.7291, 13
__func__.7291:
	.ascii	"FtlProgPages\000"
	.type	__func__.8319, %object
	.size	__func__.8319, 19
__func__.8319:
	.ascii	"FtlGcFreeTempBlock\000"
	.type	__func__.8432, %object
	.size	__func__.8432, 23
__func__.8432:
	.ascii	"rk_ftl_garbage_collect\000"
	.type	__func__.7319, %object
	.size	__func__.7319, 9
__func__.7319:
	.ascii	"FtlWrite\000"
	.type	__func__.7895, %object
	.size	__func__.7895, 15
__func__.7895:
	.ascii	"FtlLoadSysInfo\000"
	.type	__func__.7917, %object
	.size	__func__.7917, 18
__func__.7917:
	.ascii	"FtlMapTblRecovery\000"
	.type	__func__.13960, %object
	.size	__func__.13960, 14
__func__.13960:
	.ascii	"FtlWriteToIDB\000"
	.space	1
	.type	rk_sftl_vendor_storage_fops, %object
	.size	rk_sftl_vendor_storage_fops, 128
rk_sftl_vendor_storage_fops:
	.space	36
	.word	rk_sftl_vendor_storage_ioctl
	.word	rk_sftl_vendor_storage_ioctl
	.space	84
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
	.word	.LC135
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
	.ascii	"%p %x %p %x\012\000"
.LC134:
	.ascii	"copy_from_user error %d %p %p\012\000"
.LC135:
	.ascii	"vendor_storage\000"
