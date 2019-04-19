/* SPDX-License-Identifier: GPL-2.0 */

/* Copyright (c) 2018 Rockchip Electronics Co. Ltd.
 * date: 2019-04-19
 */
	.file	"rk_sftl.c"
	.global	__udivsi3
	.text
	.align	2
	.syntax unified
	.arm
	.fpu softvfp
	.type	l2p_addr_tran, %function
l2p_addr_tran:
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 1, uses_anonymous_args = 0
	mov	ip, sp
	push	{r4, r5, r6, r7, r8, r9, fp, ip, lr, pc}
	sub	fp, ip, #4
	ldr	r3, .L3
	mov	r8, r1
	mov	r9, r2
	ldr	r6, [r0, #4]
	ldrh	r4, [r3, #8]
	ldrh	r5, [r3, #10]
	ldrh	r3, [r3, #14]
	lsr	r7, r6, #10
	ubfx	r6, r6, #0, #10
	cmp	r3, #4
	uxth	r0, r7
	lsreq	r4, r4, #1
	lsleq	r5, r5, #1
	uxth	r7, r7
	mov	r1, r4
	uxtheq	r5, r5
	bl	__udivsi3
	uxth	r0, r0
	mls	r4, r0, r4, r7
	mla	r4, r5, r4, r6
	str	r4, [r8]
	str	r0, [r9]
	mov	r0, #0
	ldmfd	sp, {r4, r5, r6, r7, r8, r9, fp, sp, pc}
.L4:
	.align	2
.L3:
	.word	.LANCHOR0
	.size	l2p_addr_tran, .-l2p_addr_tran
	.align	2
	.syntax unified
	.arm
	.fpu softvfp
	.type	ftl_set_blk_mode.part.6, %function
ftl_set_blk_mode.part.6:
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 1, uses_anonymous_args = 0
	mov	ip, sp
	push	{fp, ip, lr, pc}
	sub	fp, ip, #4
	ldr	r3, .L6
	lsr	r1, r0, #5
	mov	ip, #1
	and	r0, r0, #31
	ldr	r2, [r3, #24]
	ldr	r3, [r2, r1, lsl #2]
	orr	r0, r3, ip, lsl r0
	str	r0, [r2, r1, lsl #2]
	ldmfd	sp, {fp, sp, pc}
.L7:
	.align	2
.L6:
	.word	.LANCHOR0
	.size	ftl_set_blk_mode.part.6, .-ftl_set_blk_mode.part.6
	.align	2
	.global	ftl_print_sblk_info
	.syntax unified
	.arm
	.fpu softvfp
	.type	ftl_print_sblk_info, %function
ftl_print_sblk_info:
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 1, uses_anonymous_args = 0
	mov	ip, sp
	push	{r4, fp, ip, lr, pc}
	sub	fp, ip, #4
	sub	sp, sp, #12
	ldr	r4, .L9
	ldr	r1, .L9+4
	ldr	r0, .L9+8
	bl	sftl_printk
	ldrh	r1, [r4, #28]
	ldr	ip, [r4, #76]
	ldrb	r3, [r4, #34]	@ zero_extendqisi2
	ldrh	r2, [r4, #30]
	lsl	r0, r1, #1
	ldrh	r0, [ip, r0]
	str	r0, [sp, #8]
	ldrh	r0, [r4, #32]
	str	r0, [sp, #4]
	ldrb	r0, [r4, #36]	@ zero_extendqisi2
	str	r0, [sp]
	ldr	r0, .L9+12
	bl	sftl_printk
	ldrh	r1, [r4, #80]
	ldr	ip, [r4, #76]
	ldrb	r3, [r4, #86]	@ zero_extendqisi2
	ldrh	r2, [r4, #82]
	lsl	r0, r1, #1
	ldrh	r0, [ip, r0]
	str	r0, [sp, #8]
	ldrh	r0, [r4, #84]
	str	r0, [sp, #4]
	ldrb	r0, [r4, #88]	@ zero_extendqisi2
	str	r0, [sp]
	ldr	r0, .L9+16
	bl	sftl_printk
	ldrh	r1, [r4, #128]
	ldr	ip, [r4, #76]
	ldrb	r3, [r4, #134]	@ zero_extendqisi2
	ldrh	r2, [r4, #130]
	lsl	r0, r1, #1
	ldrh	r0, [ip, r0]
	str	r0, [sp, #8]
	ldrh	r0, [r4, #132]
	str	r0, [sp, #4]
	ldrb	r0, [r4, #136]	@ zero_extendqisi2
	str	r0, [sp]
	ldr	r0, .L9+20
	bl	sftl_printk
	ldrh	r1, [r4, #176]
	ldr	ip, [r4, #76]
	ldrb	r3, [r4, #182]	@ zero_extendqisi2
	ldrh	r2, [r4, #178]
	lsl	r0, r1, #1
	ldrh	r0, [ip, r0]
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
	bl	sftl_printk
	sub	sp, fp, #16
	ldmfd	sp, {r4, fp, sp, pc}
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
	.size	ftl_print_sblk_info, .-ftl_print_sblk_info
	.align	2
	.global	Ftl_log2
	.syntax unified
	.arm
	.fpu softvfp
	.type	Ftl_log2, %function
Ftl_log2:
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 1, uses_anonymous_args = 0
	mov	ip, sp
	push	{fp, ip, lr, pc}
	sub	fp, ip, #4
	mov	r1, #0
	mov	r2, #1
.L12:
	cmp	r2, r0
	uxth	r3, r1
	add	r1, r1, #1
	bls	.L13
	sub	r0, r3, #1
	uxth	r0, r0
	ldmfd	sp, {fp, sp, pc}
.L13:
	lsl	r2, r2, #1
	b	.L12
	.size	Ftl_log2, .-Ftl_log2
	.align	2
	.global	FtlPrintInfo
	.syntax unified
	.arm
	.fpu softvfp
	.type	FtlPrintInfo, %function
FtlPrintInfo:
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 1, uses_anonymous_args = 0
	mov	ip, sp
	push	{fp, ip, lr, pc}
	sub	fp, ip, #4
	ldmfd	sp, {fp, sp, pc}
	.size	FtlPrintInfo, .-FtlPrintInfo
	.align	2
	.global	FtlSysBlkNumInit
	.syntax unified
	.arm
	.fpu softvfp
	.type	FtlSysBlkNumInit, %function
FtlSysBlkNumInit:
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 1, uses_anonymous_args = 0
	mov	ip, sp
	push	{fp, ip, lr, pc}
	sub	fp, ip, #4
	ldr	r3, .L16
	uxth	r0, r0
	ldrh	r2, [r3, #236]
	cmp	r0, #24
	ldrh	r1, [r3, #246]
	movcc	r0, #24
	str	r0, [r3, #232]
	mul	r2, r0, r2
	sub	r0, r1, r0
	ldr	r1, [r3, #252]
	strh	r0, [r3, #244]	@ movhi
	mov	r0, #0
	str	r2, [r3, #240]
	sub	r2, r1, r2
	str	r2, [r3, #248]
	ldmfd	sp, {fp, sp, pc}
.L17:
	.align	2
.L16:
	.word	.LANCHOR0
	.size	FtlSysBlkNumInit, .-FtlSysBlkNumInit
	.global	__divsi3
	.align	2
	.global	FtlConstantsInit
	.syntax unified
	.arm
	.fpu softvfp
	.type	FtlConstantsInit, %function
FtlConstantsInit:
	@ args = 0, pretend = 0, frame = 8
	@ frame_needed = 1, uses_anonymous_args = 0
	mov	ip, sp
	push	{r4, r5, r6, r7, r8, r9, r10, fp, ip, lr, pc}
	sub	fp, ip, #4
	sub	sp, sp, #8
	mov	r3, r0
	ldr	r4, .L32
	movw	r5, #262
	ldrh	r0, [r0]
	ldrh	r6, [r3, #2]
	add	r2, r4, #256
	ldrh	ip, [r3, #14]
	strh	r0, [r2]	@ movhi
	movw	r2, #258
	strh	r6, [r4, r2]	@ movhi
	add	r1, r4, #260
	ldrh	lr, [r3, #4]
	cmp	ip, #4
	ldrh	r2, [r3, #6]
	strh	ip, [r4, r5]	@ movhi
	strh	lr, [r1]	@ movhi
	strh	r2, [r4, #246]	@ movhi
	bne	.L19
	lsr	r2, r2, #1
	strh	r2, [r4, #246]	@ movhi
	mov	r2, #8
	strh	r2, [r4, r5]	@ movhi
.L20:
	ldr	ip, .L32+4
	mov	r2, #0
.L21:
	strb	r2, [r2, ip]
	add	r2, r2, #1
	cmp	r2, #32
	bne	.L21
	ldr	r2, .L32+8
	cmp	r0, #1
	mov	ip, #5
	ldrh	r5, [r4, #246]
	ldr	r8, .L32+12
	mov	lr, #0
	strh	ip, [r2]	@ movhi
	movw	ip, #298
	strheq	r0, [r2]	@ movhi
	mov	r2, #640
	ldrh	r0, [r1]
	mov	r7, r8
	strh	r2, [r7], #4	@ movhi
	movw	r2, #302
	strh	lr, [r4, ip]	@ movhi
	str	r3, [fp, #-44]
	smulbb	r6, r6, r0
	smulbb	r0, r0, r5
	uxth	r6, r6
	uxth	r0, r0
	strh	r6, [r4, #236]	@ movhi
	strh	r0, [r4, r2]	@ movhi
	bl	Ftl_log2
	ldr	r3, [fp, #-44]
	movw	r2, #306
	strh	r0, [r7]	@ movhi
	mov	r7, r8
	add	r8, r8, #24
	ldrh	r10, [r3, #12]
	str	r3, [fp, #-48]
	strh	r10, [r4, r2]	@ movhi
	movw	r2, #310
	smulbb	r1, r6, r10
	strh	r10, [r8, #-16]	@ movhi
	strh	r1, [r4, r2]	@ movhi
	movw	r2, #262
	ldrh	r1, [r4, r2]
	mov	r0, r1
	str	r1, [fp, #-44]
	bl	Ftl_log2
	ldr	r1, [fp, #-44]
	mov	r9, r0
	strh	r0, [r7, #12]!	@ movhi
	movw	r0, #314
	ldr	r3, [fp, #-48]
	lsl	r2, r1, #9
	uxth	r2, r2
	strh	r2, [r4, r0]	@ movhi
	lsr	r2, r2, #8
	mov	r0, #5120
	strh	r2, [r8, #-8]	@ movhi
	ldrh	r2, [r3, #20]
	movw	r3, #318
	strh	r2, [r4, r3]	@ movhi
	mul	r3, r5, r6
	lsl	r5, r5, #6
	str	r3, [r4, #252]
	mul	r3, r1, r3
	mul	r1, r10, r1
	mul	r3, r10, r3
	asr	r3, r3, #11
	str	r3, [r4, #320]
	bl	__divsi3
	uxth	r0, r0
	movw	r2, #326
	mov	r1, r6
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
	mul	r3, r6, r5
	add	r5, r5, #8
	str	r3, [r4, #332]
	bl	__udivsi3
	uxtah	r0, r5, r0
	cmp	r6, #1
	addeq	r0, r0, #4
	str	r0, [r4, #232]
	ldrh	r0, [r4, #232]
	bl	FtlSysBlkNumInit
	ldr	r3, [r4, #232]
	mov	r0, #0
	str	r0, [r4, #344]
	str	r3, [r4, #336]
	ldr	r3, [r4, #248]
	lsl	r2, r3, #2
	movw	r3, #306
	ldrh	r3, [r4, r3]
	mul	r3, r3, r2
	ldrh	r2, [r7]
	add	r2, r2, #9
	lsr	r3, r3, r2
	ldr	r2, .L32+20
	add	r3, r3, #2
	strh	r3, [r2]	@ movhi
	mov	r2, #32
	movw	r3, #342
	strh	r2, [r4, r3]	@ movhi
	ldrh	r3, [r8]
	add	r3, r3, #3
	strh	r3, [r8]	@ movhi
	ldr	r3, [r4, #332]
	add	r3, r3, #3
	str	r3, [r4, #332]
	sub	sp, fp, #40
	ldmfd	sp, {r4, r5, r6, r7, r8, r9, r10, fp, sp, pc}
.L19:
	cmp	lr, #1
	cmpeq	ip, #8
	lsreq	r2, r2, #1
	strheq	r2, [r4, #246]	@ movhi
	moveq	r2, #2
	strheq	r2, [r1]	@ movhi
	b	.L20
.L33:
	.align	2
.L32:
	.word	.LANCHOR0
	.word	.LANCHOR0+264
	.word	.LANCHOR0+296
	.word	.LANCHOR0+300
	.word	.LANCHOR0+328
	.word	.LANCHOR0+340
	.size	FtlConstantsInit, .-FtlConstantsInit
	.align	2
	.global	IsBlkInVendorPart
	.syntax unified
	.arm
	.fpu softvfp
	.type	IsBlkInVendorPart, %function
IsBlkInVendorPart:
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 1, uses_anonymous_args = 0
	mov	ip, sp
	push	{fp, ip, lr, pc}
	sub	fp, ip, #4
	ldr	r3, .L40
	uxth	r1, r0
	add	r2, r3, #348
	ldrh	r0, [r2]
	cmp	r0, #0
	ldmfdeq	sp, {fp, sp, pc}
	ldr	r2, [r3, #352]
	add	r3, r3, #324
	ldrh	r3, [r3]
	add	r3, r2, r3, lsl #1
.L36:
	cmp	r2, r3
	bne	.L37
	mov	r0, #0
	ldmfd	sp, {fp, sp, pc}
.L37:
	ldrh	r0, [r2], #2
	cmp	r1, r0
	bne	.L36
	mov	r0, #1
	ldmfd	sp, {fp, sp, pc}
.L41:
	.align	2
.L40:
	.word	.LANCHOR0
	.size	IsBlkInVendorPart, .-IsBlkInVendorPart
	.align	2
	.global	FtlCacheWriteBack
	.syntax unified
	.arm
	.fpu softvfp
	.type	FtlCacheWriteBack, %function
FtlCacheWriteBack:
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 1, uses_anonymous_args = 0
	mov	ip, sp
	push	{fp, ip, lr, pc}
	sub	fp, ip, #4
	mov	r0, #0
	ldmfd	sp, {fp, sp, pc}
	.size	FtlCacheWriteBack, .-FtlCacheWriteBack
	.align	2
	.global	sftl_get_density
	.syntax unified
	.arm
	.fpu softvfp
	.type	sftl_get_density, %function
sftl_get_density:
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 1, uses_anonymous_args = 0
	mov	ip, sp
	push	{fp, ip, lr, pc}
	sub	fp, ip, #4
	ldr	r3, .L44
	ldr	r0, [r3, #344]
	ldmfd	sp, {fp, sp, pc}
.L45:
	.align	2
.L44:
	.word	.LANCHOR0
	.size	sftl_get_density, .-sftl_get_density
	.global	__umodsi3
	.align	2
	.global	FtlBbmMapBadBlock
	.syntax unified
	.arm
	.fpu softvfp
	.type	FtlBbmMapBadBlock, %function
FtlBbmMapBadBlock:
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 1, uses_anonymous_args = 0
	mov	ip, sp
	push	{r4, r5, r6, r7, fp, ip, lr, pc}
	sub	fp, ip, #4
	sub	sp, sp, #4
	movw	r3, #302
	ldr	r4, .L47
	uxth	r5, r0
	mov	r0, r5
	ldrh	r7, [r4, r3]
	mov	r1, r7
	bl	__udivsi3
	uxth	r6, r0
	mov	r1, r7
	mov	r0, r5
	bl	__umodsi3
	add	r2, r4, r6, lsl #2
	uxth	r3, r0
	ldr	r2, [r2, #384]
	lsr	r0, r3, #5
	and	ip, r3, #31
	mov	lr, #1
	add	r4, r4, #356
	ldr	r1, [r2, r0, lsl #2]
	orr	r1, r1, lr, lsl ip
	str	r1, [r2, r0, lsl #2]
	mov	r2, r6
	str	r1, [sp]
	mov	r1, r5
	ldr	r0, .L47+4
	bl	sftl_printk
	ldrh	r3, [r4, #6]
	mov	r0, #0
	add	r3, r3, #1
	strh	r3, [r4, #6]	@ movhi
	ldmib	sp, {r4, r5, r6, r7, fp, sp, pc}
.L48:
	.align	2
.L47:
	.word	.LANCHOR0
	.word	.LC7
	.size	FtlBbmMapBadBlock, .-FtlBbmMapBadBlock
	.align	2
	.global	FtlBbmIsBadBlock
	.syntax unified
	.arm
	.fpu softvfp
	.type	FtlBbmIsBadBlock, %function
FtlBbmIsBadBlock:
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 1, uses_anonymous_args = 0
	mov	ip, sp
	push	{r4, r5, r6, r7, fp, ip, lr, pc}
	sub	fp, ip, #4
	ldr	r5, .L50
	movw	r3, #302
	uxth	r6, r0
	ldrh	r7, [r5, r3]
	mov	r0, r6
	mov	r1, r7
	bl	__umodsi3
	mov	r1, r7
	uxth	r4, r0
	mov	r0, r6
	bl	__udivsi3
	uxth	r0, r0
	lsr	r2, r4, #5
	add	r5, r5, r0, lsl #2
	and	r4, r4, #31
	ldr	r3, [r5, #384]
	ldr	r0, [r3, r2, lsl #2]
	lsr	r0, r0, r4
	and	r0, r0, #1
	ldmfd	sp, {r4, r5, r6, r7, fp, sp, pc}
.L51:
	.align	2
.L50:
	.word	.LANCHOR0
	.size	FtlBbmIsBadBlock, .-FtlBbmIsBadBlock
	.align	2
	.global	FtlBbtInfoPrint
	.syntax unified
	.arm
	.fpu softvfp
	.type	FtlBbtInfoPrint, %function
FtlBbtInfoPrint:
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 1, uses_anonymous_args = 0
	mov	ip, sp
	push	{fp, ip, lr, pc}
	sub	fp, ip, #4
	ldmfd	sp, {fp, sp, pc}
	.size	FtlBbtInfoPrint, .-FtlBbtInfoPrint
	.align	2
	.global	FtlBbtMemInit
	.syntax unified
	.arm
	.fpu softvfp
	.type	FtlBbtMemInit, %function
FtlBbtMemInit:
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 1, uses_anonymous_args = 0
	mov	ip, sp
	push	{fp, ip, lr, pc}
	sub	fp, ip, #4
	ldr	r0, .L54
	mvn	r2, #0
	mov	r1, #255
	add	r3, r0, #356
	add	r0, r0, #368
	strh	r2, [r3]	@ movhi
	mov	r2, #0
	strh	r2, [r3, #6]	@ movhi
	mov	r2, #16
	bl	memset
	ldmfd	sp, {fp, sp, pc}
.L55:
	.align	2
.L54:
	.word	.LANCHOR0
	.size	FtlBbtMemInit, .-FtlBbtMemInit
	.align	2
	.global	FtlBbtCalcTotleCnt
	.syntax unified
	.arm
	.fpu softvfp
	.type	FtlBbtCalcTotleCnt, %function
FtlBbtCalcTotleCnt:
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 1, uses_anonymous_args = 0
	mov	ip, sp
	push	{r4, r5, r6, fp, ip, lr, pc}
	sub	fp, ip, #4
	ldr	r3, .L63
	movw	r2, #302
	movw	r1, #258
	mov	r5, #0
	ldrh	r2, [r3, r2]
	mov	r4, r5
	ldrh	r6, [r3, r1]
	mul	r6, r6, r2
.L57:
	uxth	r0, r5
	cmp	r0, r6
	blt	.L59
	mov	r0, r4
	ldmfd	sp, {r4, r5, r6, fp, sp, pc}
.L59:
	bl	FtlBbmIsBadBlock
	cmp	r0, #0
	add	r5, r5, #1
	addne	r4, r4, #1
	uxthne	r4, r4
	b	.L57
.L64:
	.align	2
.L63:
	.word	.LANCHOR0
	.size	FtlBbtCalcTotleCnt, .-FtlBbtCalcTotleCnt
	.align	2
	.global	V2P_block
	.syntax unified
	.arm
	.fpu softvfp
	.type	V2P_block, %function
V2P_block:
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 1, uses_anonymous_args = 0
	mov	ip, sp
	push	{r4, r5, r6, r7, fp, ip, lr, pc}
	sub	fp, ip, #4
	ldr	r4, .L66
	uxth	r7, r0
	uxth	r5, r1
	add	r3, r4, #260
	mov	r0, r7
	ldrh	r6, [r3]
	mov	r1, r6
	bl	__udivsi3
	movw	r3, #302
	smulbb	r5, r6, r5
	ldrh	r4, [r4, r3]
	mov	r1, r6
	smulbb	r4, r4, r0
	mov	r0, r7
	bl	__umodsi3
	add	r0, r5, r0
	add	r0, r4, r0
	uxth	r0, r0
	ldmfd	sp, {r4, r5, r6, r7, fp, sp, pc}
.L67:
	.align	2
.L66:
	.word	.LANCHOR0
	.size	V2P_block, .-V2P_block
	.align	2
	.global	P2V_plane
	.syntax unified
	.arm
	.fpu softvfp
	.type	P2V_plane, %function
P2V_plane:
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 1, uses_anonymous_args = 0
	mov	ip, sp
	push	{r4, r5, r6, fp, ip, lr, pc}
	sub	fp, ip, #4
	ldr	r3, .L69
	uxth	r5, r0
	add	r2, r3, #260
	mov	r0, r5
	ldrh	r6, [r2]
	movw	r2, #302
	ldrh	r1, [r3, r2]
	bl	__udivsi3
	mov	r1, r6
	smulbb	r4, r0, r6
	mov	r0, r5
	bl	__umodsi3
	add	r0, r4, r0
	uxth	r0, r0
	ldmfd	sp, {r4, r5, r6, fp, sp, pc}
.L70:
	.align	2
.L69:
	.word	.LANCHOR0
	.size	P2V_plane, .-P2V_plane
	.align	2
	.global	P2V_block_in_plane
	.syntax unified
	.arm
	.fpu softvfp
	.type	P2V_block_in_plane, %function
P2V_block_in_plane:
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 1, uses_anonymous_args = 0
	mov	ip, sp
	push	{r4, fp, ip, lr, pc}
	sub	fp, ip, #4
	ldr	r4, .L72
	movw	r3, #302
	uxth	r0, r0
	ldrh	r1, [r4, r3]
	add	r4, r4, #260
	bl	__umodsi3
	ldrh	r1, [r4]
	uxth	r0, r0
	bl	__udivsi3
	uxth	r0, r0
	ldmfd	sp, {r4, fp, sp, pc}
.L73:
	.align	2
.L72:
	.word	.LANCHOR0
	.size	P2V_block_in_plane, .-P2V_block_in_plane
	.align	2
	.global	ftl_cmp_data_ver
	.syntax unified
	.arm
	.fpu softvfp
	.type	ftl_cmp_data_ver, %function
ftl_cmp_data_ver:
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 1, uses_anonymous_args = 0
	mov	ip, sp
	push	{fp, ip, lr, pc}
	sub	fp, ip, #4
	cmp	r0, r1
	bls	.L75
	sub	r0, r0, r1
	cmp	r0, #-2147483648
	movhi	r0, #0
	movls	r0, #1
	ldmfd	sp, {fp, sp, pc}
.L75:
	sub	r0, r1, r0
	cmp	r0, #-2147483648
	movls	r0, #0
	movhi	r0, #1
	ldmfd	sp, {fp, sp, pc}
	.size	ftl_cmp_data_ver, .-ftl_cmp_data_ver
	.align	2
	.global	FtlFreeSysBlkQueueInit
	.syntax unified
	.arm
	.fpu softvfp
	.type	FtlFreeSysBlkQueueInit, %function
FtlFreeSysBlkQueueInit:
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 1, uses_anonymous_args = 0
	mov	ip, sp
	push	{r4, fp, ip, lr, pc}
	sub	fp, ip, #4
	ldr	r2, .L78
	mov	r4, #0
	mov	r1, #2048
	add	r3, r2, #416
	strh	r0, [r3]	@ movhi
	add	r0, r2, #424
	strh	r4, [r3, #2]	@ movhi
	strh	r4, [r3, #4]	@ movhi
	strh	r4, [r3, #6]	@ movhi
	bl	__memzero
	mov	r0, r4
	ldmfd	sp, {r4, fp, sp, pc}
.L79:
	.align	2
.L78:
	.word	.LANCHOR0
	.size	FtlFreeSysBlkQueueInit, .-FtlFreeSysBlkQueueInit
	.align	2
	.global	FtlFreeSysBlkQueueEmpty
	.syntax unified
	.arm
	.fpu softvfp
	.type	FtlFreeSysBlkQueueEmpty, %function
FtlFreeSysBlkQueueEmpty:
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 1, uses_anonymous_args = 0
	mov	ip, sp
	push	{fp, ip, lr, pc}
	sub	fp, ip, #4
	ldr	r3, .L81
	ldrh	r0, [r3, #6]
	clz	r0, r0
	lsr	r0, r0, #5
	ldmfd	sp, {fp, sp, pc}
.L82:
	.align	2
.L81:
	.word	.LANCHOR0+416
	.size	FtlFreeSysBlkQueueEmpty, .-FtlFreeSysBlkQueueEmpty
	.align	2
	.global	FtlFreeSysBlkQueueFull
	.syntax unified
	.arm
	.fpu softvfp
	.type	FtlFreeSysBlkQueueFull, %function
FtlFreeSysBlkQueueFull:
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 1, uses_anonymous_args = 0
	mov	ip, sp
	push	{fp, ip, lr, pc}
	sub	fp, ip, #4
	ldr	r3, .L84
	ldrh	r0, [r3, #6]
	sub	r0, r0, #1024
	clz	r0, r0
	lsr	r0, r0, #5
	ldmfd	sp, {fp, sp, pc}
.L85:
	.align	2
.L84:
	.word	.LANCHOR0+416
	.size	FtlFreeSysBlkQueueFull, .-FtlFreeSysBlkQueueFull
	.align	2
	.global	FtlFreeSysBLkSort
	.syntax unified
	.arm
	.fpu softvfp
	.type	FtlFreeSysBLkSort, %function
FtlFreeSysBLkSort:
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 1, uses_anonymous_args = 0
	mov	ip, sp
	push	{r4, fp, ip, lr, pc}
	sub	fp, ip, #4
	ldr	r3, .L95
	ldrh	r2, [r3, #6]
	cmp	r2, #0
	ldmfdeq	sp, {r4, fp, sp, pc}
	ldr	r2, .L95+4
	mov	r4, #0
	ldrh	r1, [r3, #2]
	mov	lr, r4
	ldrh	ip, [r2, #28]
	ldrh	r2, [r3, #4]
	and	ip, ip, #31
.L88:
	uxth	r0, r4
	add	r4, r4, #1
	cmp	ip, r0
	bgt	.L89
	cmp	lr, #0
	strhne	r1, [r3, #2]	@ movhi
	strhne	r2, [r3, #4]	@ movhi
	ldmfd	sp, {r4, fp, sp, pc}
.L89:
	add	r0, r3, r1, lsl #1
	add	r1, r1, #1
	ubfx	r1, r1, #0, #10
	ldrh	lr, [r0, #8]
	add	r0, r3, r2, lsl #1
	strh	lr, [r0, #8]	@ movhi
	mov	lr, #1
	add	r2, r2, lr
	ubfx	r2, r2, #0, #10
	b	.L88
.L96:
	.align	2
.L95:
	.word	.LANCHOR0+416
	.word	.LANCHOR0+2472
	.size	FtlFreeSysBLkSort, .-FtlFreeSysBLkSort
	.align	2
	.global	IsInFreeQueue
	.syntax unified
	.arm
	.fpu softvfp
	.type	IsInFreeQueue, %function
IsInFreeQueue:
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 1, uses_anonymous_args = 0
	mov	ip, sp
	push	{fp, ip, lr, pc}
	sub	fp, ip, #4
	ldr	r3, .L103
	uxth	r0, r0
	ldrh	ip, [r3, #6]
	cmp	ip, #1024
	ldrhne	lr, [r3, #2]
	movne	r1, #0
	bne	.L99
.L101:
	mov	r0, #0
	ldmfd	sp, {fp, sp, pc}
.L100:
	add	r2, r1, lr
	ubfx	r2, r2, #0, #10
	add	r2, r3, r2, lsl #1
	ldrh	r2, [r2, #8]
	cmp	r2, r0
	beq	.L102
	add	r1, r1, #1
.L99:
	cmp	r1, ip
	bcc	.L100
	b	.L101
.L102:
	mov	r0, #1
	ldmfd	sp, {fp, sp, pc}
.L104:
	.align	2
.L103:
	.word	.LANCHOR0+416
	.size	IsInFreeQueue, .-IsInFreeQueue
	.align	2
	.global	insert_data_list
	.syntax unified
	.arm
	.fpu softvfp
	.type	insert_data_list, %function
insert_data_list:
	@ args = 0, pretend = 0, frame = 4
	@ frame_needed = 1, uses_anonymous_args = 0
	mov	ip, sp
	push	{r4, r5, r6, r7, r8, r9, r10, fp, ip, lr, pc}
	sub	fp, ip, #4
	sub	sp, sp, #4
	uxth	r0, r0
	ldr	r1, .L120
	ldrh	r3, [r1, #244]
	mov	lr, r1
	cmp	r3, r0
	bls	.L107
	ldr	r6, [r1, #2520]
	lsl	r5, r0, #3
	mvn	ip, #0
	add	r2, r6, r5
	strh	ip, [r2, #2]	@ movhi
	strh	ip, [r6, r5]	@ movhi
	ldr	r3, [r1, #2524]
	cmp	r3, #0
	bne	.L108
.L119:
	str	r2, [r1, #2524]
	b	.L107
.L108:
	ldr	r8, [r1, #76]
	lsl	r1, r0, #1
	ldr	r7, [lr, #2520]
	movw	r9, #65535
	ldrh	lr, [lr, #244]
	ldrh	r4, [r8, r1]
	ldrh	r1, [r2, #4]
	str	lr, [fp, #-44]
	mov	lr, #0
	cmp	r1, #0
	mulne	ip, r1, r4
	sub	r1, r3, r7
	ubfx	r1, r1, #3, #16
.L113:
	ldr	r4, [fp, #-44]
	add	lr, lr, #1
	uxth	lr, lr
	cmp	lr, r4
	movls	r4, #0
	movhi	r4, #1
	cmp	r0, r1
	orreq	r4, r4, #1
	cmp	r4, #0
	bne	.L107
	ldrh	r4, [r3, #4]
	cmp	r4, #0
	beq	.L111
	lsl	r10, r1, #1
	ldrh	r10, [r8, r10]
	mul	r4, r4, r10
	cmp	r4, ip
	bcs	.L111
	ldrh	r4, [r3]
	cmp	r4, r9
	bne	.L112
	strh	r1, [r2, #2]	@ movhi
	strh	r0, [r3]	@ movhi
	ldr	r3, .L120
	str	r2, [r3, #2528]
.L107:
	mov	r0, #0
	ldmib	sp, {r4, r5, r6, r7, r8, r9, r10, fp, sp, pc}
.L112:
	add	r3, r7, r4, lsl #3
	mov	r1, r4
	b	.L113
.L111:
	strh	r1, [r6, r5]	@ movhi
	ldrh	r1, [r3, #2]
	strh	r1, [r2, #2]	@ movhi
	ldr	r1, .L120
	ldr	ip, [r1, #2524]
	cmp	r3, ip
	strheq	r0, [r3, #2]	@ movhi
	beq	.L119
.L114:
	ldrh	r2, [r3, #2]
	ldr	r1, [r1, #2520]
	lsl	r2, r2, #3
	strh	r0, [r1, r2]	@ movhi
	strh	r0, [r3, #2]	@ movhi
	b	.L107
.L121:
	.align	2
.L120:
	.word	.LANCHOR0
	.size	insert_data_list, .-insert_data_list
	.align	2
	.global	INSERT_DATA_LIST
	.syntax unified
	.arm
	.fpu softvfp
	.type	INSERT_DATA_LIST, %function
INSERT_DATA_LIST:
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 1, uses_anonymous_args = 0
	mov	ip, sp
	push	{fp, ip, lr, pc}
	sub	fp, ip, #4
	uxth	r0, r0
	bl	insert_data_list
	ldr	r2, .L124
	movw	r1, #2532
	ldrh	r3, [r2, r1]
	add	r3, r3, #1
	uxth	r3, r3
	strh	r3, [r2, r1]	@ movhi
	ldrh	r2, [r2, #244]
	cmp	r2, r3
	ldmfdcs	sp, {fp, sp, pc}
	mov	r2, #214
	ldr	r1, .L124+4
	ldr	r0, .L124+8
	bl	sftl_printk
	ldmfd	sp, {fp, sp, pc}
.L125:
	.align	2
.L124:
	.word	.LANCHOR0
	.word	.LANCHOR1
	.word	.LC8
	.size	INSERT_DATA_LIST, .-INSERT_DATA_LIST
	.align	2
	.global	insert_free_list
	.syntax unified
	.arm
	.fpu softvfp
	.type	insert_free_list, %function
insert_free_list:
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 1, uses_anonymous_args = 0
	mov	ip, sp
	push	{r4, r5, r6, r7, r8, r9, fp, ip, lr, pc}
	sub	fp, ip, #4
	uxth	r0, r0
	movw	lr, #65535
	cmp	r0, lr
	beq	.L127
	ldr	r2, .L133
	lsl	r5, r0, #3
	mvn	r3, #0
	ldr	r6, [r2, #2520]
	mov	r1, r2
	add	ip, r6, r5
	strh	r3, [ip, #2]	@ movhi
	strh	r3, [r6, r5]	@ movhi
	ldr	r3, [r2, #2536]
	cmp	r3, #0
	streq	ip, [r2, #2536]
	beq	.L127
	ldr	r8, [r2, #2540]
	lsl	r2, r0, #1
	ldr	r7, [r1, #2520]
	mov	r4, lr
	ldrh	r9, [r8, r2]
	sub	r2, r3, r7
	ubfx	r2, r2, #3, #16
.L131:
	lsl	lr, r2, #1
	ldrh	lr, [r8, lr]
	cmp	lr, r9
	bcs	.L129
	ldrh	lr, [r3]
	cmp	lr, r4
	bne	.L130
	strh	r2, [ip, #2]	@ movhi
	strh	r0, [r3]	@ movhi
.L127:
	mov	r0, #0
	ldmfd	sp, {r4, r5, r6, r7, r8, r9, fp, sp, pc}
.L130:
	add	r3, r7, lr, lsl #3
	mov	r2, lr
	b	.L131
.L129:
	ldrh	lr, [r3, #2]
	strh	lr, [ip, #2]	@ movhi
	strh	r2, [r6, r5]	@ movhi
	ldr	r2, [r1, #2536]
	cmp	r3, r2
	ldrhne	r2, [r3, #2]
	ldrne	r1, [r1, #2520]
	strheq	r0, [r3, #2]	@ movhi
	streq	ip, [r1, #2536]
	lslne	r2, r2, #3
	strhne	r0, [r1, r2]	@ movhi
	strhne	r0, [r3, #2]	@ movhi
	b	.L127
.L134:
	.align	2
.L133:
	.word	.LANCHOR0
	.size	insert_free_list, .-insert_free_list
	.align	2
	.global	INSERT_FREE_LIST
	.syntax unified
	.arm
	.fpu softvfp
	.type	INSERT_FREE_LIST, %function
INSERT_FREE_LIST:
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 1, uses_anonymous_args = 0
	mov	ip, sp
	push	{fp, ip, lr, pc}
	sub	fp, ip, #4
	uxth	r0, r0
	bl	insert_free_list
	ldr	r2, .L137
	ldrh	r3, [r2, #228]
	add	r3, r3, #1
	uxth	r3, r3
	strh	r3, [r2, #228]	@ movhi
	ldrh	r2, [r2, #244]
	cmp	r2, r3
	ldmfdcs	sp, {fp, sp, pc}
	mov	r2, #207
	ldr	r1, .L137+4
	ldr	r0, .L137+8
	bl	sftl_printk
	ldmfd	sp, {fp, sp, pc}
.L138:
	.align	2
.L137:
	.word	.LANCHOR0
	.word	.LANCHOR1+17
	.word	.LC8
	.size	INSERT_FREE_LIST, .-INSERT_FREE_LIST
	.align	2
	.global	List_remove_node
	.syntax unified
	.arm
	.fpu softvfp
	.type	List_remove_node, %function
List_remove_node:
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 1, uses_anonymous_args = 0
	mov	ip, sp
	push	{r4, r5, r6, r7, r8, fp, ip, lr, pc}
	sub	fp, ip, #4
	ldr	r4, .L146
	uxth	r1, r1
	movw	r3, #65535
	mov	r8, r0
	ldr	r7, [r4, #2520]
	lsl	r6, r1, #3
	add	r5, r7, r6
	ldrh	r2, [r5, #2]
	cmp	r2, r3
	bne	.L140
	ldr	r3, [r0]
	cmp	r5, r3
	beq	.L140
	mov	r2, #372
	ldr	r1, .L146+4
	ldr	r0, .L146+8
	bl	sftl_printk
.L140:
	ldr	r3, [r8]
	ldrh	r2, [r7, r6]
	cmp	r5, r3
	movw	r3, #65535
	bne	.L141
	cmp	r2, r3
	ldrne	r3, [r4, #2520]
	moveq	r3, #0
	streq	r3, [r8]
	addne	r2, r3, r2, lsl #3
	mvnne	r3, #0
	strne	r2, [r8]
	strhne	r3, [r2, #2]	@ movhi
.L143:
	mvn	r3, #0
	mov	r0, #0
	strh	r3, [r7, r6]	@ movhi
	strh	r3, [r5, #2]	@ movhi
	ldmfd	sp, {r4, r5, r6, r7, r8, fp, sp, pc}
.L141:
	cmp	r2, r3
	ldrh	r3, [r5, #2]
	bne	.L144
	cmp	r3, r2
	ldrne	r2, [r4, #2520]
	lslne	r3, r3, #3
	mvnne	r1, #0
	beq	.L143
.L145:
	strh	r1, [r2, r3]	@ movhi
	b	.L143
.L144:
	ldr	r1, [r4, #2520]
	add	r2, r1, r2, lsl #3
	strh	r3, [r2, #2]	@ movhi
	ldrh	r3, [r5, #2]
	ldrh	r1, [r7, r6]
	ldr	r2, [r4, #2520]
	lsl	r3, r3, #3
	b	.L145
.L147:
	.align	2
.L146:
	.word	.LANCHOR0
	.word	.LANCHOR1+34
	.word	.LC8
	.size	List_remove_node, .-List_remove_node
	.align	2
	.global	List_pop_index_node
	.syntax unified
	.arm
	.fpu softvfp
	.type	List_pop_index_node, %function
List_pop_index_node:
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 1, uses_anonymous_args = 0
	mov	ip, sp
	push	{r4, fp, ip, lr, pc}
	sub	fp, ip, #4
	ldr	r3, [r0]
	uxth	r1, r1
	cmp	r3, #0
	movweq	r4, #65535
	beq	.L148
	ldr	r2, .L155
	movw	ip, #65535
	ldr	r4, [r2, #2520]
.L150:
	cmp	r1, #0
	bne	.L151
.L153:
	sub	r3, r3, r4
	ubfx	r4, r3, #3, #16
	mov	r1, r4
	bl	List_remove_node
.L148:
	mov	r0, r4
	ldmfd	sp, {r4, fp, sp, pc}
.L151:
	ldrh	r2, [r3]
	cmp	r2, ip
	beq	.L153
	sub	r1, r1, #1
	add	r3, r4, r2, lsl #3
	uxth	r1, r1
	b	.L150
.L156:
	.align	2
.L155:
	.word	.LANCHOR0
	.size	List_pop_index_node, .-List_pop_index_node
	.align	2
	.global	List_pop_head_node
	.syntax unified
	.arm
	.fpu softvfp
	.type	List_pop_head_node, %function
List_pop_head_node:
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 1, uses_anonymous_args = 0
	mov	ip, sp
	push	{fp, ip, lr, pc}
	sub	fp, ip, #4
	mov	r1, #0
	bl	List_pop_index_node
	ldmfd	sp, {fp, sp, pc}
	.size	List_pop_head_node, .-List_pop_head_node
	.align	2
	.global	List_get_gc_head_node
	.syntax unified
	.arm
	.fpu softvfp
	.type	List_get_gc_head_node, %function
List_get_gc_head_node:
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 1, uses_anonymous_args = 0
	mov	ip, sp
	push	{fp, ip, lr, pc}
	sub	fp, ip, #4
	ldr	r1, .L164
	uxth	r2, r0
	ldr	r3, [r1, #2524]
	cmp	r3, #0
	ldrne	r1, [r1, #2520]
	movwne	r0, #65535
	bne	.L160
	movw	r0, #65535
	ldmfd	sp, {fp, sp, pc}
.L162:
	sub	r2, r2, #1
	add	r3, r1, r3, lsl #3
	uxth	r2, r2
.L160:
	cmp	r2, #0
	beq	.L161
	ldrh	r3, [r3]
	cmp	r3, r0
	bne	.L162
	ldmfd	sp, {fp, sp, pc}
.L161:
	sub	r3, r3, r1
	ubfx	r0, r3, #3, #16
	ldmfd	sp, {fp, sp, pc}
.L165:
	.align	2
.L164:
	.word	.LANCHOR0
	.size	List_get_gc_head_node, .-List_get_gc_head_node
	.align	2
	.global	List_update_data_list
	.syntax unified
	.arm
	.fpu softvfp
	.type	List_update_data_list, %function
List_update_data_list:
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 1, uses_anonymous_args = 0
	mov	ip, sp
	push	{r4, r5, r6, r7, r8, fp, ip, lr, pc}
	sub	fp, ip, #4
	ldr	r4, .L179
	uxth	r5, r0
	ldrh	r3, [r4, #28]
	cmp	r3, r5
	beq	.L168
	ldrh	r3, [r4, #80]
	cmp	r3, r5
	beq	.L168
	ldrh	r3, [r4, #128]
	cmp	r3, r5
	beq	.L168
	ldr	r8, [r4, #2520]
	ldr	r3, [r4, #2524]
	add	r7, r8, r5, lsl #3
	cmp	r7, r3
	beq	.L168
	ldr	r2, [r4, #76]
	lsl	r3, r5, #1
	ldrh	r6, [r7, #4]
	ldrh	r3, [r2, r3]
	cmp	r6, #0
	mvneq	r6, #0
	mulne	r6, r6, r3
	ldr	r3, [r8, r5, lsl #3]
	cmn	r3, #1
	bne	.L171
	movw	r2, #463
	ldr	r1, .L179+4
	ldr	r0, .L179+8
	bl	sftl_printk
.L171:
	ldr	r3, [r8, r5, lsl #3]
	cmn	r3, #1
	beq	.L168
	ldrh	r2, [r7, #2]
	ldr	r1, [r4, #76]
	lsl	r2, r2, #3
	lsr	r3, r2, #2
	ldrh	r0, [r1, r3]
	ldr	r1, [r4, #2520]
	add	r2, r1, r2
	ldrh	r3, [r2, #4]
	cmp	r3, #0
	mulne	r3, r3, r0
	mvneq	r3, #0
	cmp	r6, r3
	bcs	.L168
	mov	r1, r5
	ldr	r0, .L179+12
	bl	List_remove_node
	movw	r3, #2532
	ldrh	r3, [r4, r3]
	cmp	r3, #0
	bne	.L173
	movw	r2, #474
	ldr	r1, .L179+4
	ldr	r0, .L179+8
	bl	sftl_printk
.L173:
	movw	r2, #2532
	mov	r0, r5
	ldrh	r3, [r4, r2]
	sub	r3, r3, #1
	strh	r3, [r4, r2]	@ movhi
	bl	INSERT_DATA_LIST
.L168:
	mov	r0, #0
	ldmfd	sp, {r4, r5, r6, r7, r8, fp, sp, pc}
.L180:
	.align	2
.L179:
	.word	.LANCHOR0
	.word	.LANCHOR1+51
	.word	.LC8
	.word	.LANCHOR0+2524
	.size	List_update_data_list, .-List_update_data_list
	.align	2
	.global	select_l2p_ram_region
	.syntax unified
	.arm
	.fpu softvfp
	.type	select_l2p_ram_region, %function
select_l2p_ram_region:
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 1, uses_anonymous_args = 0
	mov	ip, sp
	push	{r4, r5, r6, r7, fp, ip, lr, pc}
	sub	fp, ip, #4
	ldr	r0, .L191
	movw	r3, #342
	mov	r1, #0
	movw	ip, #65535
	ldrh	r2, [r0, r3]
	mov	lr, r0
	ldr	r3, [r0, #2544]
	mov	r0, #12
.L182:
	uxth	r4, r1
	cmp	r4, r2
	bcc	.L184
	mov	r4, r2
	mov	r1, #0
	mov	r0, #-2147483648
	mov	r6, #12
.L185:
	uxth	r5, r1
	cmp	r5, r2
	bcc	.L187
	cmp	r4, r2
	bcc	.L183
	movw	r1, #2548
	mov	r4, r2
	ldrh	r5, [lr, r1]
	mvn	r0, #0
	mov	r1, #0
.L188:
	uxth	ip, r1
	cmp	ip, r2
	bcc	.L190
	cmp	r4, r2
	bcc	.L183
	movw	r2, #789
	ldr	r1, .L191+4
	ldr	r0, .L191+8
	bl	sftl_printk
	b	.L183
.L184:
	add	r1, r1, #1
	mla	r5, r0, r1, r3
	ldrh	r5, [r5, #-12]
	cmp	r5, ip
	bne	.L182
.L183:
	mov	r0, r4
	ldmfd	sp, {r4, r5, r6, r7, fp, sp, pc}
.L187:
	mla	ip, r6, r1, r3
	add	r1, r1, #1
	ldr	ip, [ip, #4]
	cmp	r0, ip
	movls	r7, #0
	movhi	r7, #1
	cmp	ip, #0
	movlt	r7, #0
	cmp	r7, #0
	movne	r0, ip
	movne	r4, r5
	b	.L185
.L190:
	ldr	lr, [r3, #4]
	cmp	r0, lr
	bls	.L189
	ldrh	r6, [r3]
	cmp	r6, r5
	movne	r0, lr
	movne	r4, ip
.L189:
	add	r1, r1, #1
	add	r3, r3, #12
	b	.L188
.L192:
	.align	2
.L191:
	.word	.LANCHOR0
	.word	.LANCHOR1+73
	.word	.LC8
	.size	select_l2p_ram_region, .-select_l2p_ram_region
	.align	2
	.global	FtlUpdateVaildLpn
	.syntax unified
	.arm
	.fpu softvfp
	.type	FtlUpdateVaildLpn, %function
FtlUpdateVaildLpn:
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 1, uses_anonymous_args = 0
	mov	ip, sp
	push	{fp, ip, lr, pc}
	sub	fp, ip, #4
	ldr	ip, .L199
	movw	r1, #2550
	ldrh	r2, [ip, r1]
	mov	r3, ip
	cmp	r2, #4
	cmpls	r0, #0
	bne	.L194
	add	r2, r2, #1
	strh	r2, [ip, r1]	@ movhi
	ldmfd	sp, {fp, sp, pc}
.L194:
	mov	r2, #0
	movw	lr, #65535
	strh	r2, [ip, r1]	@ movhi
	str	r2, [ip, #2552]
	ldrh	r1, [ip, #244]
	ldr	r2, [ip, #76]
	add	r1, r2, r1, lsl #1
.L195:
	cmp	r2, r1
	bne	.L197
	ldmfd	sp, {fp, sp, pc}
.L197:
	ldrh	ip, [r2], #2
	cmp	ip, lr
	ldrne	r0, [r3, #2552]
	addne	r0, r0, ip
	strne	r0, [r3, #2552]
	b	.L195
.L200:
	.align	2
.L199:
	.word	.LANCHOR0
	.size	FtlUpdateVaildLpn, .-FtlUpdateVaildLpn
	.align	2
	.global	ftl_set_blk_mode
	.syntax unified
	.arm
	.fpu softvfp
	.type	ftl_set_blk_mode, %function
ftl_set_blk_mode:
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 1, uses_anonymous_args = 0
	mov	ip, sp
	push	{fp, ip, lr, pc}
	sub	fp, ip, #4
	cmp	r1, #0
	uxth	r0, r0
	beq	.L202
	bl	ftl_set_blk_mode.part.6
	ldmfd	sp, {fp, sp, pc}
.L202:
	ldr	r3, .L204
	lsr	r1, r0, #5
	mov	ip, #1
	and	r0, r0, #31
	ldr	r2, [r3, #24]
	ldr	r3, [r2, r1, lsl #2]
	bic	r0, r3, ip, lsl r0
	str	r0, [r2, r1, lsl #2]
	ldmfd	sp, {fp, sp, pc}
.L205:
	.align	2
.L204:
	.word	.LANCHOR0
	.size	ftl_set_blk_mode, .-ftl_set_blk_mode
	.align	2
	.global	ftl_get_blk_mode
	.syntax unified
	.arm
	.fpu softvfp
	.type	ftl_get_blk_mode, %function
ftl_get_blk_mode:
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 1, uses_anonymous_args = 0
	mov	ip, sp
	push	{fp, ip, lr, pc}
	sub	fp, ip, #4
	ldr	r3, .L207
	uxth	r0, r0
	ldr	r3, [r3, #24]
	lsr	r2, r0, #5
	and	r0, r0, #31
	ldr	r3, [r3, r2, lsl #2]
	lsr	r0, r3, r0
	and	r0, r0, #1
	ldmfd	sp, {fp, sp, pc}
.L208:
	.align	2
.L207:
	.word	.LANCHOR0
	.size	ftl_get_blk_mode, .-ftl_get_blk_mode
	.align	2
	.global	ftl_sb_update_avl_pages
	.syntax unified
	.arm
	.fpu softvfp
	.type	ftl_sb_update_avl_pages, %function
ftl_sb_update_avl_pages:
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 1, uses_anonymous_args = 0
	mov	ip, sp
	push	{r4, r5, fp, ip, lr, pc}
	sub	fp, ip, #4
	mov	r3, #0
	uxth	r2, r2
	uxth	r5, r1
	strh	r3, [r0, #4]	@ movhi
	ldr	r3, .L216
	add	ip, r0, r2, lsl #1
	movw	r1, #65535
	add	ip, ip, #14
	ldrh	lr, [r3, #236]
.L210:
	cmp	r2, lr
	bcc	.L212
	movw	r2, #306
	add	ip, r0, #16
	ldrh	r1, [r3, r2]
	movw	r4, #65535
	mov	r3, #0
	sub	r1, r1, #1
	sub	r1, r1, r5
	uxth	r1, r1
.L213:
	uxth	r2, r3
	cmp	lr, r2
	bhi	.L215
	ldmfd	sp, {r4, r5, fp, sp, pc}
.L212:
	ldrh	r4, [ip, #2]!
	add	r2, r2, #1
	uxth	r2, r2
	cmp	r4, r1
	ldrhne	r4, [r0, #4]
	addne	r4, r4, #1
	strhne	r4, [r0, #4]	@ movhi
	b	.L210
.L215:
	ldrh	r2, [ip], #2
	add	r3, r3, #1
	cmp	r2, r4
	ldrhne	r2, [r0, #4]
	addne	r2, r1, r2
	strhne	r2, [r0, #4]	@ movhi
	b	.L213
.L217:
	.align	2
.L216:
	.word	.LANCHOR0
	.size	ftl_sb_update_avl_pages, .-ftl_sb_update_avl_pages
	.align	2
	.global	FtlSlcSuperblockCheck
	.syntax unified
	.arm
	.fpu softvfp
	.type	FtlSlcSuperblockCheck, %function
FtlSlcSuperblockCheck:
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 1, uses_anonymous_args = 0
	mov	ip, sp
	push	{fp, ip, lr, pc}
	sub	fp, ip, #4
	ldrh	r3, [r0, #4]
	cmp	r3, #0
	ldmfdeq	sp, {fp, sp, pc}
	ldrh	r2, [r0]
	movw	r3, #65535
	cmp	r2, r3
	ldmfdeq	sp, {fp, sp, pc}
	ldrb	r2, [r0, #6]	@ zero_extendqisi2
	mov	lr, #0
	add	r2, r0, r2, lsl #1
	ldrh	r1, [r2, #16]
	ldr	r2, .L224
	ldrh	ip, [r2, #236]
	mov	r2, r3
.L221:
	cmp	r1, r2
	beq	.L223
	ldmfd	sp, {fp, sp, pc}
.L223:
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
	b	.L221
.L225:
	.align	2
.L224:
	.word	.LANCHOR0
	.size	FtlSlcSuperblockCheck, .-FtlSlcSuperblockCheck
	.align	2
	.global	make_superblock
	.syntax unified
	.arm
	.fpu softvfp
	.type	make_superblock, %function
make_superblock:
	@ args = 0, pretend = 0, frame = 4
	@ frame_needed = 1, uses_anonymous_args = 0
	mov	ip, sp
	push	{r4, r5, r6, r7, r8, r9, r10, fp, ip, lr, pc}
	sub	fp, ip, #4
	sub	sp, sp, #4
	mov	r4, r0
	ldr	r6, .L232
	ldrh	r2, [r0]
	ldrh	r3, [r6, #244]
	cmp	r2, r3
	bcc	.L227
	movw	r2, #2148
	ldr	r1, .L232+4
	ldr	r0, .L232+8
	bl	sftl_printk
.L227:
	ldrh	r9, [r6, #236]
	add	r8, r4, #16
	ldr	r10, .L232+12
	mvn	r7, #0
	mov	r5, #0
	strh	r5, [r4, #4]	@ movhi
	strb	r5, [r4, #7]
.L228:
	uxth	r3, r5
	ldrh	r1, [r4]
	cmp	r9, r3
	bhi	.L230
	movw	r2, #306
	ldrb	r3, [r4, #7]	@ zero_extendqisi2
	ldrh	r2, [r6, r2]
	lsl	r1, r1, #1
	mov	r0, #0
	smulbb	r3, r3, r2
	strh	r3, [r4, #4]	@ movhi
	mov	r3, #0
	strb	r3, [r4, #9]
	ldr	r3, [r6, #2540]
	ldrh	r2, [r3, r1]
	movw	r3, #10000
	cmp	r2, r3
	movhi	r3, #1
	strbhi	r3, [r4, #9]
	ldmib	sp, {r4, r5, r6, r7, r8, r9, r10, fp, sp, pc}
.L230:
	ldrb	r0, [r10, r5]	@ zero_extendqisi2
	add	r8, r8, #2
	bl	V2P_block
	strh	r7, [r8, #-2]	@ movhi
	add	r5, r5, #1
	str	r0, [fp, #-44]
	bl	FtlBbmIsBadBlock
	cmp	r0, #0
	ldreq	r3, [fp, #-44]
	strheq	r3, [r8, #-2]	@ movhi
	ldrbeq	r3, [r4, #7]	@ zero_extendqisi2
	addeq	r3, r3, #1
	strbeq	r3, [r4, #7]
	b	.L228
.L233:
	.align	2
.L232:
	.word	.LANCHOR0
	.word	.LANCHOR1+95
	.word	.LC8
	.word	.LANCHOR0+264
	.size	make_superblock, .-make_superblock
	.align	2
	.global	update_multiplier_value
	.syntax unified
	.arm
	.fpu softvfp
	.type	update_multiplier_value, %function
update_multiplier_value:
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 1, uses_anonymous_args = 0
	mov	ip, sp
	push	{r4, r5, r6, r7, r8, r9, r10, fp, ip, lr, pc}
	sub	fp, ip, #4
	ldr	r3, .L240
	movw	r2, #306
	mov	r5, #0
	uxth	r6, r0
	mov	r4, r5
	ldrh	r8, [r3, #236]
	mov	r7, r3
	ldrh	r9, [r3, r2]
	add	r10, r3, #264
.L235:
	uxth	r3, r5
	cmp	r8, r3
	bhi	.L237
	cmp	r4, #0
	moveq	r0, r4
	beq	.L238
	mov	r1, r4
	mov	r0, #32768
	bl	__divsi3
.L238:
	ldr	r1, [r7, #2520]
	add	r1, r1, r6, lsl #3
	strh	r0, [r1, #4]	@ movhi
	mov	r0, #0
	ldmfd	sp, {r4, r5, r6, r7, r8, r9, r10, fp, sp, pc}
.L237:
	mov	r1, r6
	ldrb	r0, [r10, r5]	@ zero_extendqisi2
	bl	V2P_block
	bl	FtlBbmIsBadBlock
	cmp	r0, #0
	add	r5, r5, #1
	addeq	r4, r4, r9
	uxtheq	r4, r4
	b	.L235
.L241:
	.align	2
.L240:
	.word	.LANCHOR0
	.size	update_multiplier_value, .-update_multiplier_value
	.align	2
	.global	GetFreeBlockMinEraseCount
	.syntax unified
	.arm
	.fpu softvfp
	.type	GetFreeBlockMinEraseCount, %function
GetFreeBlockMinEraseCount:
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 1, uses_anonymous_args = 0
	mov	ip, sp
	push	{fp, ip, lr, pc}
	sub	fp, ip, #4
	ldr	r3, .L245
	ldr	r0, [r3, #2536]
	cmp	r0, #0
	ldrne	r2, [r3, #2520]
	ldrne	r3, [r3, #2540]
	subne	r0, r0, r2
	ubfxne	r0, r0, #3, #16
	lslne	r0, r0, #1
	ldrhne	r0, [r3, r0]
	ldmfd	sp, {fp, sp, pc}
.L246:
	.align	2
.L245:
	.word	.LANCHOR0
	.size	GetFreeBlockMinEraseCount, .-GetFreeBlockMinEraseCount
	.align	2
	.global	GetFreeBlockMaxEraseCount
	.syntax unified
	.arm
	.fpu softvfp
	.type	GetFreeBlockMaxEraseCount, %function
GetFreeBlockMaxEraseCount:
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 1, uses_anonymous_args = 0
	mov	ip, sp
	push	{r4, r5, fp, ip, lr, pc}
	sub	fp, ip, #4
	ldr	r2, .L255
	uxth	r4, r0
	ldr	r0, [r2, #2536]
	cmp	r0, #0
	ldmfdeq	sp, {r4, r5, fp, sp, pc}
	ldrh	r3, [r2, #228]
	mov	r1, #0
	ldr	ip, [r2, #2520]
	movw	r5, #65535
	rsb	r3, r3, r3, lsl #3
	asr	r3, r3, #3
	cmp	r4, r3
	uxthgt	r4, r3
	sub	r3, r0, ip
	ubfx	r3, r3, #3, #16
.L250:
	uxth	r0, r1
	cmp	r4, r0
	bls	.L252
	lsl	r0, r3, #3
	add	r1, r1, #1
	ldrh	lr, [ip, r0]
	cmp	lr, r5
	bne	.L254
.L252:
	ldr	r2, [r2, #2540]
	lsl	r3, r3, #1
	ldrh	r0, [r2, r3]
	ldmfd	sp, {r4, r5, fp, sp, pc}
.L254:
	mov	r3, lr
	b	.L250
.L256:
	.align	2
.L255:
	.word	.LANCHOR0
	.size	GetFreeBlockMaxEraseCount, .-GetFreeBlockMaxEraseCount
	.align	2
	.global	FtlPrintInfo2buf
	.syntax unified
	.arm
	.fpu softvfp
	.type	FtlPrintInfo2buf, %function
FtlPrintInfo2buf:
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 1, uses_anonymous_args = 0
	mov	ip, sp
	push	{r4, r5, r6, r7, r8, r9, r10, fp, ip, lr, pc}
	sub	fp, ip, #4
	sub	sp, sp, #16
	mov	r6, r0
	ldr	r5, .L269
	add	r4, r6, #12
	ldr	r1, .L269+4
	bl	strcpy
	ldr	r3, [r5, #320]
	mov	r1, #64
	ldr	r2, .L269+8
	mov	r0, r4
	bl	snprintf
	add	r4, r4, r0
	ldr	r1, .L269+12
	mov	r0, r4
	add	r4, r4, #10
	bl	strcpy
	ldr	r3, [r5, #2556]
	mov	r1, #64
	ldr	r2, .L269+16
	mov	r0, r4
	bl	snprintf
	add	r4, r4, r0
	ldr	r3, [r5, #2552]
	ldr	r2, .L269+20
	mov	r1, #64
	mov	r0, r4
	add	r7, r5, #356
	bl	snprintf
	add	r4, r4, r0
	ldr	r3, [r5, #2560]
	ldr	r2, .L269+24
	mov	r1, #64
	mov	r0, r4
	ldr	r8, .L269+28
	bl	snprintf
	add	r4, r4, r0
	ldr	r3, [r5, #2564]
	ldr	r2, .L269+32
	mov	r1, #64
	mov	r0, r4
	bl	snprintf
	add	r4, r4, r0
	ldr	r3, [r5, #2568]
	ldr	r2, .L269+36
	mov	r1, #64
	mov	r0, r4
	bl	snprintf
	add	r4, r4, r0
	ldr	r3, [r5, #2572]
	ldr	r2, .L269+40
	mov	r1, #64
	mov	r0, r4
	bl	snprintf
	add	r4, r4, r0
	ldr	r3, [r5, #2576]
	ldr	r2, .L269+44
	mov	r1, #64
	mov	r0, r4
	bl	snprintf
	add	r4, r4, r0
	ldr	r3, [r5, #2580]
	ldr	r2, .L269+48
	mov	r1, #64
	mov	r0, r4
	bl	snprintf
	ldr	r3, [r5, #2584]
	add	r4, r4, r0
	ldr	r2, .L269+52
	mov	r1, #64
	mov	r0, r4
	lsr	r3, r3, #11
	bl	snprintf
	ldr	r3, [r5, #2588]
	add	r4, r4, r0
	ldr	r2, .L269+56
	mov	r1, #64
	mov	r0, r4
	lsr	r3, r3, #11
	bl	snprintf
	add	r4, r4, r0
	ldr	r3, [r5, #2592]
	ldr	r2, .L269+60
	mov	r1, #64
	mov	r0, r4
	bl	snprintf
	add	r4, r4, r0
	ldr	r3, [r5, #2596]
	ldr	r2, .L269+64
	mov	r1, #64
	mov	r0, r4
	bl	snprintf
	add	r4, r4, r0
	ldrh	r3, [r7, #6]
	ldr	r2, .L269+68
	mov	r1, #64
	mov	r0, r4
	bl	snprintf
	add	r4, r4, r0
	ldrh	r3, [r5, #228]
	ldr	r2, .L269+72
	mov	r1, #64
	mov	r0, r4
	bl	snprintf
	add	r4, r4, r0
	ldr	r3, [r5, #2600]
	ldr	r2, .L269+76
	mov	r1, #64
	mov	r0, r4
	bl	snprintf
	add	r4, r4, r0
	ldr	r3, [r5, #2604]
	ldr	r2, .L269+80
	mov	r1, #64
	mov	r0, r4
	bl	snprintf
	add	r4, r4, r0
	ldr	r3, [r5, #2608]
	ldr	r2, .L269+84
	mov	r1, #64
	mov	r0, r4
	bl	snprintf
	add	r4, r4, r0
	ldr	r3, [r5, #2612]
	ldr	r2, .L269+88
	mov	r1, #64
	mov	r0, r4
	bl	snprintf
	add	r4, r4, r0
	ldr	r3, [r5, #2616]
	ldr	r2, .L269+92
	mov	r1, #64
	mov	r0, r4
	bl	snprintf
	add	r4, r4, r0
	ldr	r3, [r5, #2620]
	ldr	r2, .L269+96
	mov	r1, #64
	mov	r0, r4
	bl	snprintf
	add	r4, r4, r0
	ldrh	r3, [r8, #30]
	ldr	r2, .L269+100
	mov	r1, #64
	mov	r0, r4
	bl	snprintf
	add	r4, r4, r0
	ldrh	r3, [r8, #28]
	ldr	r2, .L269+104
	mov	r1, #64
	mov	r0, r4
	bl	snprintf
	add	r4, r4, r0
	ldr	r3, [r5, #344]
	ldr	r2, .L269+108
	mov	r1, #64
	mov	r0, r4
	bl	snprintf
	add	r4, r4, r0
	ldr	r3, [r5, #336]
	ldr	r2, .L269+112
	mov	r1, #64
	mov	r0, r4
	bl	snprintf
	add	r4, r4, r0
	ldr	r3, [r5, #232]
	ldr	r2, .L269+116
	mov	r1, #64
	mov	r0, r4
	bl	snprintf
	add	r3, r5, #416
	add	r4, r4, r0
	ldrh	r3, [r3, #6]
	mov	r1, #64
	ldr	r2, .L269+120
	mov	r0, r4
	bl	snprintf
	add	r4, r4, r0
	ldrh	r3, [r5, #244]
	ldr	r2, .L269+124
	mov	r1, #64
	mov	r0, r4
	bl	snprintf
	add	r3, r5, #2624
	add	r4, r4, r0
	ldrh	r3, [r3]
	mov	r1, #64
	ldr	r2, .L269+128
	mov	r0, r4
	bl	snprintf
	add	r4, r4, r0
	ldr	r3, [r5, #248]
	ldr	r2, .L269+132
	mov	r1, #64
	mov	r0, r4
	bl	snprintf
	movw	r3, #2628
	add	r4, r4, r0
	ldrh	r3, [r5, r3]
	mov	r1, #64
	ldr	r2, .L269+136
	mov	r0, r4
	bl	snprintf
	add	r4, r4, r0
	ldrh	r3, [r7]
	ldr	r2, .L269+140
	mov	r1, #64
	mov	r0, r4
	bl	snprintf
	add	r4, r4, r0
	ldrh	r3, [r5, #30]
	ldr	r2, .L269+144
	mov	r1, #64
	mov	r0, r4
	bl	snprintf
	add	r4, r4, r0
	ldrb	r3, [r5, #34]	@ zero_extendqisi2
	ldr	r2, .L269+148
	mov	r1, #64
	mov	r0, r4
	bl	snprintf
	add	r4, r4, r0
	ldrh	r3, [r5, #28]
	ldr	r2, .L269+152
	mov	r1, #64
	mov	r0, r4
	bl	snprintf
	add	r4, r4, r0
	ldrb	r3, [r5, #36]	@ zero_extendqisi2
	ldr	r2, .L269+156
	mov	r1, #64
	mov	r0, r4
	bl	snprintf
	add	r4, r4, r0
	ldrh	r3, [r5, #32]
	ldr	r2, .L269+160
	mov	r1, #64
	mov	r0, r4
	bl	snprintf
	ldrh	r3, [r5, #28]
	add	r4, r4, r0
	ldr	r2, [r5, #76]
	mov	r1, #64
	mov	r0, r4
	lsl	r3, r3, #1
	ldrh	r3, [r2, r3]
	ldr	r2, .L269+164
	bl	snprintf
	add	r4, r4, r0
	ldrh	r3, [r5, #82]
	ldr	r2, .L269+168
	mov	r1, #64
	mov	r0, r4
	bl	snprintf
	add	r4, r4, r0
	ldrb	r3, [r5, #86]	@ zero_extendqisi2
	ldr	r2, .L269+172
	mov	r1, #64
	mov	r0, r4
	bl	snprintf
	add	r4, r4, r0
	ldrh	r3, [r5, #80]
	ldr	r2, .L269+176
	mov	r1, #64
	mov	r0, r4
	bl	snprintf
	add	r4, r4, r0
	ldrb	r3, [r5, #88]	@ zero_extendqisi2
	ldr	r2, .L269+180
	mov	r1, #64
	mov	r0, r4
	bl	snprintf
	add	r4, r4, r0
	ldrh	r3, [r5, #84]
	ldr	r2, .L269+184
	mov	r1, #64
	mov	r0, r4
	bl	snprintf
	ldrh	r3, [r5, #80]
	add	r4, r4, r0
	ldr	r2, [r5, #76]
	mov	r1, #64
	mov	r0, r4
	lsl	r3, r3, #1
	ldrh	r3, [r2, r3]
	ldr	r2, .L269+188
	bl	snprintf
	add	r4, r4, r0
	ldrh	r3, [r5, #130]
	ldr	r2, .L269+192
	mov	r1, #64
	mov	r0, r4
	bl	snprintf
	add	r4, r4, r0
	ldrb	r3, [r5, #134]	@ zero_extendqisi2
	ldr	r2, .L269+196
	mov	r1, #64
	mov	r0, r4
	bl	snprintf
	add	r4, r4, r0
	ldrh	r3, [r5, #128]
	ldr	r2, .L269+200
	mov	r1, #64
	mov	r0, r4
	bl	snprintf
	add	r4, r4, r0
	ldrb	r3, [r5, #136]	@ zero_extendqisi2
	ldr	r2, .L269+204
	mov	r1, #64
	mov	r0, r4
	bl	snprintf
	add	r4, r4, r0
	ldrh	r3, [r5, #132]
	ldr	r2, .L269+208
	mov	r1, #64
	mov	r0, r4
	bl	snprintf
	add	r4, r4, r0
	ldrh	r3, [r5, #178]
	ldr	r2, .L269+212
	mov	r1, #64
	mov	r0, r4
	bl	snprintf
	add	r4, r4, r0
	ldrb	r3, [r5, #182]	@ zero_extendqisi2
	ldr	r2, .L269+216
	mov	r1, #64
	mov	r0, r4
	bl	snprintf
	add	r4, r4, r0
	ldrh	r3, [r5, #176]
	ldr	r2, .L269+220
	mov	r1, #64
	mov	r0, r4
	bl	snprintf
	add	r4, r4, r0
	ldrb	r3, [r5, #184]	@ zero_extendqisi2
	ldr	r2, .L269+224
	mov	r1, #64
	mov	r0, r4
	bl	snprintf
	add	r4, r4, r0
	ldrh	r3, [r5, #180]
	ldr	r2, .L269+228
	mov	r1, #64
	mov	r0, r4
	bl	snprintf
	ldr	r3, [r5, #2720]
	add	r4, r4, r0
	ldr	r2, .L269+232
	mov	r1, #64
	mov	r0, r4
	str	r3, [sp, #4]
	ldr	r3, [r5, #2728]
	str	r3, [sp]
	ldr	r3, [r5, #2724]
	bl	snprintf
	add	r4, r4, r0
	ldr	r3, [r5, #2716]
	ldr	r2, .L269+236
	mov	r1, #64
	mov	r0, r4
	bl	snprintf
	add	r4, r4, r0
	ldr	r3, [r5, #2740]
	ldr	r2, .L269+240
	mov	r1, #64
	mov	r0, r4
	bl	snprintf
	movw	r3, #3156
	add	r4, r4, r0
	ldrh	r3, [r5, r3]
	mov	r1, #64
	ldr	r2, .L269+244
	mov	r0, r4
	bl	snprintf
	movw	r3, #3158
	add	r4, r4, r0
	ldrh	r3, [r5, r3]
	mov	r1, #64
	ldr	r2, .L269+248
	mov	r0, r4
	bl	snprintf
	add	r4, r4, r0
	ldr	r3, [r5, #3160]
	ldr	r2, .L269+252
	mov	r1, #64
	mov	r0, r4
	bl	snprintf
	movw	r3, #3164
	add	r4, r4, r0
	ldrh	r3, [r5, r3]
	mov	r1, #64
	ldr	r2, .L269+256
	mov	r0, r4
	bl	snprintf
	add	r4, r4, r0
	bl	GetFreeBlockMinEraseCount
	ldr	r2, .L269+260
	mov	r3, r0
	mov	r1, #64
	mov	r0, r4
	bl	snprintf
	add	r4, r4, r0
	ldrh	r0, [r5, #228]
	bl	GetFreeBlockMaxEraseCount
	ldr	r2, .L269+264
	mov	r3, r0
	mov	r1, #64
	mov	r0, r4
	bl	snprintf
	ldr	r3, .L269+268
	add	r4, r4, r0
	ldr	r3, [r3]
	cmp	r3, #1
	beq	.L258
.L263:
	sub	r0, r4, r6
	sub	sp, fp, #40
	ldmfd	sp, {r4, r5, r6, r7, r8, r9, r10, fp, sp, pc}
.L258:
	ldrh	r3, [r5, #176]
	movw	r2, #65535
	cmp	r3, r2
	beq	.L260
	ldr	r2, [r5, #76]
	lsl	r3, r3, #1
	mov	r0, r4
	mov	r1, #64
	ldrh	r3, [r2, r3]
	ldr	r2, .L269+272
	bl	snprintf
	add	r4, r4, r0
.L260:
	mov	r0, #0
	ldr	r9, .L269+276
	bl	List_get_gc_head_node
	uxth	r0, r0
	mov	r7, #0
	movw	r10, #65535
.L262:
	cmp	r0, r10
	beq	.L261
	ldr	r3, [r5, #2540]
	lsl	r2, r0, #1
	lsl	r8, r0, #3
	mov	r1, #64
	ldrh	r3, [r3, r2]
	str	r3, [sp, #12]
	ldr	r3, [r5, #2520]
	add	r3, r3, r8
	ldrh	r3, [r3, #4]
	str	r3, [sp, #8]
	ldr	r3, [r5, #76]
	ldrh	r3, [r3, r2]
	mov	r2, r9
	stm	sp, {r0, r3}
	mov	r3, r7
	mov	r0, r4
	add	r7, r7, #1
	bl	snprintf
	ldr	r3, [r5, #2520]
	cmp	r7, #16
	add	r4, r4, r0
	ldrh	r0, [r3, r8]
	bne	.L262
.L261:
	ldr	r3, [r5, #2536]
	mov	r7, #0
	ldr	r2, [r5, #2520]
	movw	r9, #65535
	ldr	r10, .L269+280
	sub	r3, r3, r2
	ubfx	r3, r3, #3, #16
.L264:
	cmp	r3, r9
	beq	.L263
	ldr	r1, [r5, #2540]
	lsl	r2, r3, #1
	lsl	r8, r3, #3
	mov	r0, r4
	ldrh	r2, [r1, r2]
	mov	r1, #64
	str	r2, [sp, #8]
	ldr	r2, [r5, #2520]
	add	r2, r2, r8
	ldrh	r2, [r2, #4]
	str	r3, [sp]
	mov	r3, r7
	add	r7, r7, #1
	str	r2, [sp, #4]
	mov	r2, r10
	bl	snprintf
	cmp	r7, #4
	add	r4, r4, r0
	beq	.L263
	ldr	r3, [r5, #2520]
	ldrh	r3, [r3, r8]
	b	.L264
.L270:
	.align	2
.L269:
	.word	.LANCHOR0
	.word	.LC9
	.word	.LC10
	.word	.LC11
	.word	.LC12
	.word	.LC13
	.word	.LC14
	.word	.LANCHOR0+2472
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
	.size	FtlPrintInfo2buf, .-FtlPrintInfo2buf
	.align	2
	.global	rknand_proc_ftlread
	.syntax unified
	.arm
	.fpu softvfp
	.type	rknand_proc_ftlread, %function
rknand_proc_ftlread:
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 1, uses_anonymous_args = 0
	mov	ip, sp
	push	{r4, r5, fp, ip, lr, pc}
	sub	fp, ip, #4
	cmp	r0, #2048
	mov	r5, r1
	blt	.L273
	ldr	r3, .L274
	mov	r1, #64
	ldr	r2, .L274+4
	mov	r0, r5
	bl	snprintf
	add	r4, r5, r0
	mov	r0, r4
	bl	FtlPrintInfo2buf
	add	r0, r4, r0
	sub	r0, r0, r5
	ldmfd	sp, {r4, r5, fp, sp, pc}
.L273:
	mov	r0, #0
	ldmfd	sp, {r4, r5, fp, sp, pc}
.L275:
	.align	2
.L274:
	.word	.LC0
	.word	.LC77
	.size	rknand_proc_ftlread, .-rknand_proc_ftlread
	.align	2
	.global	GetSwlReplaceBlock
	.syntax unified
	.arm
	.fpu softvfp
	.type	GetSwlReplaceBlock, %function
GetSwlReplaceBlock:
	@ args = 0, pretend = 0, frame = 4
	@ frame_needed = 1, uses_anonymous_args = 0
	mov	ip, sp
	push	{r4, r5, r6, r7, r8, r9, r10, fp, ip, lr, pc}
	sub	fp, ip, #4
	sub	sp, sp, #16
	ldr	r4, .L300
	ldr	r2, [r4, #2608]
	ldr	r3, [r4, #2620]
	cmp	r2, r3
	bcs	.L277
	ldr	r2, [r4, #2540]
	mov	r3, #0
	ldrh	r1, [r4, #244]
	str	r3, [r4, #2600]
	sub	r2, r2, #2
.L278:
	cmp	r3, r1
	bcc	.L279
	ldr	r5, [r4, #2600]
	mov	r0, r5
	bl	__udivsi3
	ldr	r3, .L300+4
	str	r0, [r4, #2608]
	ldr	r0, [r4, #2604]
	ldrh	r1, [r3]
	sub	r0, r5, r0
	bl	__udivsi3
	str	r0, [r4, #2600]
.L280:
	ldr	r6, [r4, #2620]
	ldr	r7, [r4, #2608]
	add	r3, r6, #256
	cmp	r3, r7
	bls	.L285
	ldr	r2, [r4, #2616]
	add	r3, r6, #768
	cmp	r3, r2
	bls	.L285
.L287:
	movw	r5, #65535
.L286:
	mov	r0, r5
	sub	sp, fp, #40
	ldmfd	sp, {r4, r5, r6, r7, r8, r9, r10, fp, sp, pc}
.L279:
	ldrh	r0, [r2, #2]!
	add	r3, r3, #1
	ldr	ip, [r4, #2600]
	add	r0, r0, ip
	str	r0, [r4, #2600]
	b	.L278
.L277:
	ldr	r3, [r4, #2616]
	cmp	r2, r3
	addhi	r3, r3, #1
	strhi	r3, [r4, #2616]
	movhi	r3, #0
	bls	.L280
.L282:
	ldrh	r2, [r4, #244]
	cmp	r3, r2
	bcs	.L280
	ldr	r0, [r4, #2540]
	lsl	r1, r3, #1
	add	r3, r3, #1
	ldrh	r2, [r0, r1]
	add	r2, r2, #1
	strh	r2, [r0, r1]	@ movhi
	b	.L282
.L285:
	ldrh	r0, [r4, #228]
	add	r0, r0, r0, lsl #1
	asr	r0, r0, #2
	bl	GetFreeBlockMaxEraseCount
	add	r3, r6, #64
	mov	r9, r0
	cmp	r0, r3
	bcc	.L287
	ldr	r3, [r4, #2524]
	cmp	r3, #0
	beq	.L287
	ldrh	r10, [r4, #244]
	movw	r1, #65535
	ldr	lr, [r4, #2520]
	mov	r0, #0
	ldr	r8, [r4, #2540]
	mov	r2, r1
.L288:
	ldrh	ip, [r3]
	movw	r5, #65535
	cmp	ip, r5
	bne	.L291
	mov	r5, r2
.L290:
	movw	r3, #65535
	cmp	r5, r3
	beq	.L287
	lsl	r3, r5, #1
	ldrh	r10, [r8, r3]
	cmp	r6, r10
	bcs	.L292
	str	r3, [fp, #-44]
	bl	GetFreeBlockMinEraseCount
	ldr	r3, [fp, #-44]
	cmp	r6, r0
	strcc	r1, [r4, #2620]
.L292:
	cmp	r7, r10
	bls	.L287
	add	r2, r10, #128
	cmp	r9, r2
	ble	.L287
	add	r2, r10, #256
	cmp	r7, r2
	bhi	.L293
	ldr	r2, [r4, #2616]
	add	r10, r10, #768
	cmp	r10, r2
	bcs	.L287
.L293:
	str	r9, [sp, #8]
	mov	r1, r5
	ldrh	r2, [r8, r3]
	ldr	r0, .L300+8
	str	r2, [sp, #4]
	ldr	r2, [r4, #76]
	ldrh	r3, [r2, r3]
	mov	r2, r7
	str	r3, [sp]
	ldr	r3, [r4, #2616]
	bl	sftl_printk
	mov	r3, #1
	str	r3, [r4, #3168]
	b	.L286
.L291:
	add	r0, r0, #1
	uxth	r0, r0
	cmp	r0, r10
	bhi	.L287
	ldrh	r5, [r3, #4]
	cmp	r5, #0
	beq	.L289
	sub	r3, r3, lr
	asr	r3, r3, #3
	uxth	r5, r3
	lsl	r3, r5, #1
	ldrh	r3, [r8, r3]
	cmp	r6, r3
	bcs	.L290
	cmp	r1, r3
	movhi	r1, r3
	movhi	r2, r5
.L289:
	add	r3, lr, ip, lsl #3
	b	.L288
.L301:
	.align	2
.L300:
	.word	.LANCHOR0
	.word	.LANCHOR0+296
	.word	.LC78
	.size	GetSwlReplaceBlock, .-GetSwlReplaceBlock
	.align	2
	.global	free_data_superblock
	.syntax unified
	.arm
	.fpu softvfp
	.type	free_data_superblock, %function
free_data_superblock:
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 1, uses_anonymous_args = 0
	mov	ip, sp
	push	{fp, ip, lr, pc}
	sub	fp, ip, #4
	uxth	r0, r0
	movw	r3, #65535
	cmp	r0, r3
	beq	.L303
	ldr	r3, .L304
	mov	r1, #0
	ldr	r2, [r3, #76]
	lsl	r3, r0, #1
	strh	r1, [r2, r3]	@ movhi
	bl	INSERT_FREE_LIST
.L303:
	mov	r0, #0
	ldmfd	sp, {fp, sp, pc}
.L305:
	.align	2
.L304:
	.word	.LANCHOR0
	.size	free_data_superblock, .-free_data_superblock
	.align	2
	.global	get_new_active_ppa
	.syntax unified
	.arm
	.fpu softvfp
	.type	get_new_active_ppa, %function
get_new_active_ppa:
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 1, uses_anonymous_args = 0
	mov	ip, sp
	push	{r4, r5, r6, fp, ip, lr, pc}
	sub	fp, ip, #4
	ldrh	r2, [r0]
	movw	r3, #65535
	mov	r4, r0
	cmp	r2, r3
	bne	.L307
	movw	r2, #2781
	ldr	r1, .L321
	ldr	r0, .L321+4
	bl	sftl_printk
.L307:
	ldr	r6, .L321+8
	movw	r3, #306
	ldrh	r2, [r4, #2]
	ldrh	r3, [r6, r3]
	cmp	r2, r3
	bne	.L308
	movw	r2, #2782
	ldr	r1, .L321
	ldr	r0, .L321+4
	bl	sftl_printk
.L308:
	ldrh	r3, [r4, #4]
	cmp	r3, #0
	bne	.L309
	movw	r2, #2783
	ldr	r1, .L321
	ldr	r0, .L321+4
	bl	sftl_printk
.L309:
	ldrb	r2, [r4, #6]	@ zero_extendqisi2
	mov	r3, #0
	ldrh	r0, [r6, #236]
	movw	ip, #65535
	strb	r3, [r4, #10]
	add	r2, r4, r2, lsl #1
	ldrh	r1, [r2, #16]
	mov	r2, r3
.L310:
	cmp	r1, ip
	beq	.L312
	movw	r3, #306
	ldrh	r5, [r4, #2]
	ldrh	ip, [r6, r3]
	cmp	r5, ip
	movwcs	r5, #65535
	bcs	.L306
	ldrh	r2, [r4, #4]
	orr	r5, r5, r1, lsl #10
	ldrb	r3, [r4, #6]	@ zero_extendqisi2
	movw	lr, #65535
	sub	r2, r2, #1
	uxth	r2, r2
	strh	r2, [r4, #4]	@ movhi
.L315:
	add	r3, r3, #1
	uxtb	r3, r3
	cmp	r0, r3
	ldrheq	r1, [r4, #2]
	moveq	r3, #0
	addeq	r1, r1, #1
	strheq	r1, [r4, #2]	@ movhi
	add	r1, r4, r3, lsl #1
	ldrh	r1, [r1, #16]
	cmp	r1, lr
	beq	.L315
	strb	r3, [r4, #6]
	cmp	r2, #0
	ldrh	r3, [r4, #2]
	sub	r3, r3, ip
	clz	r3, r3
	lsr	r3, r3, #5
	moveq	r3, #0
	cmp	r3, #0
	beq	.L306
	movw	r2, #2806
	ldr	r1, .L321
	ldr	r0, .L321+4
	bl	sftl_printk
.L306:
	mov	r0, r5
	ldmfd	sp, {r4, r5, r6, fp, sp, pc}
.L312:
	ldrb	r3, [r4, #6]	@ zero_extendqisi2
	add	r3, r3, #1
	uxtb	r3, r3
	cmp	r3, r0
	strb	r3, [r4, #6]
	ldrheq	r3, [r4, #2]
	strbeq	r2, [r4, #6]
	addeq	r3, r3, #1
	strheq	r3, [r4, #2]	@ movhi
	ldrb	r3, [r4, #6]	@ zero_extendqisi2
	add	r3, r4, r3, lsl #1
	ldrh	r1, [r3, #16]
	b	.L310
.L322:
	.align	2
.L321:
	.word	.LANCHOR1+111
	.word	.LC8
	.word	.LANCHOR0
	.size	get_new_active_ppa, .-get_new_active_ppa
	.align	2
	.global	FtlGcBufInit
	.syntax unified
	.arm
	.fpu softvfp
	.type	FtlGcBufInit, %function
FtlGcBufInit:
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 1, uses_anonymous_args = 0
	mov	ip, sp
	push	{r4, r5, r6, r7, r8, r9, fp, ip, lr, pc}
	sub	fp, ip, #4
	ldr	r5, .L328
	mov	lr, #12
	mov	r4, #1
	ldr	r2, .L328+4
	mov	r6, #20
	mov	r3, #0
	str	r3, [r2, #3172]
.L324:
	ldrh	r1, [r2, #236]
	uxth	r0, r3
	add	ip, r3, #1
	cmp	r0, r1
	bcc	.L325
	ldr	lr, .L328
	mov	r0, #12
	mov	ip, #0
.L326:
	ldr	r3, [r2, #3192]
	cmp	r1, r3
	bcc	.L327
	ldmfd	sp, {r4, r5, r6, r7, r8, r9, fp, sp, pc}
.L325:
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
	ldr	r1, .L328+8
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
	b	.L324
.L327:
	mul	r4, r0, r1
	ldr	r6, [r2, #3176]
	add	r3, r6, r4
	str	ip, [r3, #8]
	ldrh	r3, [lr]
	mul	r3, r1, r3
	add	r5, r3, #3
	cmp	r3, #0
	movlt	r3, r5
	ldr	r5, [r2, #3180]
	bic	r3, r3, #3
	add	r3, r5, r3
	str	r3, [r6, r4]
	ldr	r3, [r2, #3176]
	add	r4, r3, r4
	ldr	r3, .L328+8
	ldrh	r3, [r3]
	mul	r3, r1, r3
	add	r1, r1, #1
	uxth	r1, r1
	add	r5, r3, #3
	cmp	r3, #0
	movlt	r3, r5
	ldr	r5, [r2, #3184]
	bic	r3, r3, #3
	add	r3, r5, r3
	str	r3, [r4, #4]
	b	.L326
.L329:
	.align	2
.L328:
	.word	.LANCHOR0+314
	.word	.LANCHOR0
	.word	.LANCHOR0+316
	.size	FtlGcBufInit, .-FtlGcBufInit
	.align	2
	.global	FtlGcBufFree
	.syntax unified
	.arm
	.fpu softvfp
	.type	FtlGcBufFree, %function
FtlGcBufFree:
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 1, uses_anonymous_args = 0
	mov	ip, sp
	push	{r4, r5, r6, r7, r8, r9, r10, fp, ip, lr, pc}
	sub	fp, ip, #4
	ldr	r3, .L337
	mov	lr, #0
	mov	r5, #20
	mov	r7, #12
	mov	r8, lr
	ldr	r6, [r3, #3192]
	ldr	r4, [r3, #3176]
.L331:
	uxth	r3, lr
	cmp	r1, r3
	ldmfdls	sp, {r4, r5, r6, r7, r8, r9, r10, fp, sp, pc}
	mla	ip, r5, r3, r0
	mov	r2, #0
.L332:
	uxth	r3, r2
	cmp	r6, r3
	bls	.L333
	mul	r3, r7, r3
	add	r2, r2, #1
	ldr	r10, [r4, r3]
	add	r9, r4, r3
	ldr	r3, [ip, #8]
	cmp	r10, r3
	bne	.L332
	str	r8, [r9, #8]
.L333:
	add	lr, lr, #1
	b	.L331
.L338:
	.align	2
.L337:
	.word	.LANCHOR0
	.size	FtlGcBufFree, .-FtlGcBufFree
	.align	2
	.global	FtlGcBufAlloc
	.syntax unified
	.arm
	.fpu softvfp
	.type	FtlGcBufAlloc, %function
FtlGcBufAlloc:
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 1, uses_anonymous_args = 0
	mov	ip, sp
	push	{r4, r5, r6, r7, r8, r9, fp, ip, lr, pc}
	sub	fp, ip, #4
	ldr	r3, .L346
	mov	ip, #0
	mov	r6, #12
	mov	r7, #1
	mov	r8, #20
	ldr	r4, [r3, #3192]
	ldr	r5, [r3, #3176]
.L340:
	uxth	r2, ip
	cmp	r1, r2
	bhi	.L344
	ldmfd	sp, {r4, r5, r6, r7, r8, r9, fp, sp, pc}
.L344:
	mov	lr, #0
.L341:
	uxth	r3, lr
	cmp	r4, r3
	bls	.L342
	mla	r3, r6, r3, r5
	add	lr, lr, #1
	ldr	r9, [r3, #8]
	cmp	r9, #0
	bne	.L341
	mla	r2, r8, r2, r0
	ldr	lr, [r3]
	str	r7, [r3, #8]
	str	lr, [r2, #8]
	ldr	r3, [r3, #4]
	str	r3, [r2, #12]
.L342:
	add	ip, ip, #1
	b	.L340
.L347:
	.align	2
.L346:
	.word	.LANCHOR0
	.size	FtlGcBufAlloc, .-FtlGcBufAlloc
	.align	2
	.global	IsBlkInGcList
	.syntax unified
	.arm
	.fpu softvfp
	.type	IsBlkInGcList, %function
IsBlkInGcList:
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 1, uses_anonymous_args = 0
	mov	ip, sp
	push	{fp, ip, lr, pc}
	sub	fp, ip, #4
	ldr	r2, .L353
	uxth	r0, r0
	ldr	r3, [r2, #3196]
	add	r2, r2, #3200
	ldrh	r2, [r2]
	add	r2, r3, r2, lsl #1
.L349:
	cmp	r3, r2
	bne	.L351
	mov	r0, #0
	ldmfd	sp, {fp, sp, pc}
.L351:
	ldrh	r1, [r3], #2
	cmp	r1, r0
	bne	.L349
	mov	r0, #1
	ldmfd	sp, {fp, sp, pc}
.L354:
	.align	2
.L353:
	.word	.LANCHOR0
	.size	IsBlkInGcList, .-IsBlkInGcList
	.align	2
	.global	FtlGcUpdatePage
	.syntax unified
	.arm
	.fpu softvfp
	.type	FtlGcUpdatePage, %function
FtlGcUpdatePage:
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 1, uses_anonymous_args = 0
	mov	ip, sp
	push	{r4, r5, r6, r7, r8, fp, ip, lr, pc}
	sub	fp, ip, #4
	mov	r4, r0
	ubfx	r0, r0, #10, #16
	mov	r5, r1
	mov	r6, r2
	bl	P2V_block_in_plane
	ldr	r3, .L359
	mov	ip, #0
	add	lr, r3, #3200
	ldr	r1, [r3, #3196]
	ldrh	r7, [lr]
	sub	r2, r1, #2
.L356:
	uxth	r8, ip
	cmp	r8, r7
	bcc	.L358
	moveq	ip, r8
	lsleq	ip, ip, #1
	strheq	r0, [r1, ip]	@ movhi
	ldrheq	r2, [lr]
	addeq	r2, r2, #1
	strheq	r2, [lr]	@ movhi
	b	.L357
.L358:
	ldrh	r8, [r2, #2]!
	add	ip, ip, #1
	cmp	r8, r0
	bne	.L356
.L357:
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
	ldmfd	sp, {r4, r5, r6, r7, r8, fp, sp, pc}
.L360:
	.align	2
.L359:
	.word	.LANCHOR0
	.size	FtlGcUpdatePage, .-FtlGcUpdatePage
	.align	2
	.global	FtlGcRefreshBlock
	.syntax unified
	.arm
	.fpu softvfp
	.type	FtlGcRefreshBlock, %function
FtlGcRefreshBlock:
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 1, uses_anonymous_args = 0
	mov	ip, sp
	push	{r4, fp, ip, lr, pc}
	sub	fp, ip, #4
	uxth	r4, r0
	ldr	r0, .L364
	mov	r1, r4
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
	strheq	r4, [r3, #226]	@ movhi
	beq	.L362
	cmp	r1, r2
	strheq	r4, [r3, #224]	@ movhi
.L362:
	mov	r0, #0
	ldmfd	sp, {r4, fp, sp, pc}
.L365:
	.align	2
.L364:
	.word	.LC79
	.word	.LANCHOR0
	.size	FtlGcRefreshBlock, .-FtlGcRefreshBlock
	.align	2
	.global	FtlGcMarkBadPhyBlk
	.syntax unified
	.arm
	.fpu softvfp
	.type	FtlGcMarkBadPhyBlk, %function
FtlGcMarkBadPhyBlk:
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 1, uses_anonymous_args = 0
	mov	ip, sp
	push	{r4, r5, r6, r7, fp, ip, lr, pc}
	sub	fp, ip, #4
	ldr	r4, .L370
	uxth	r5, r0
	movw	r6, #3210
	mov	r0, r5
	bl	P2V_block_in_plane
	mov	r2, r5
	mov	r7, r0
	ldrh	r1, [r4, r6]
	ldr	r0, .L370+4
	bl	sftl_printk
	mov	r0, r7
	bl	FtlGcRefreshBlock
	ldrh	r3, [r4, r6]
	mov	r2, #0
	ldr	r0, .L370+8
.L367:
	uxth	r1, r2
	cmp	r3, r1
	bhi	.L369
	cmp	r3, #15
	movwls	r2, #3210
	addls	r1, r3, #1
	strhls	r1, [r4, r2]	@ movhi
	lslls	r3, r3, #1
	ldrls	r2, .L370+8
	strhls	r5, [r2, r3]	@ movhi
	b	.L368
.L369:
	add	r2, r2, #1
	add	r1, r0, r2, lsl #1
	ldrh	r1, [r1, #-2]
	cmp	r1, r5
	bne	.L367
.L368:
	mov	r0, #0
	ldmfd	sp, {r4, r5, r6, r7, fp, sp, pc}
.L371:
	.align	2
.L370:
	.word	.LANCHOR0
	.word	.LC80
	.word	.LANCHOR0+3212
	.size	FtlGcMarkBadPhyBlk, .-FtlGcMarkBadPhyBlk
	.align	2
	.global	FtlGcReFreshBadBlk
	.syntax unified
	.arm
	.fpu softvfp
	.type	FtlGcReFreshBadBlk, %function
FtlGcReFreshBadBlk:
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 1, uses_anonymous_args = 0
	mov	ip, sp
	push	{r4, r5, fp, ip, lr, pc}
	sub	fp, ip, #4
	ldr	r4, .L378
	movw	r3, #3210
	ldrh	r2, [r4, r3]
	cmp	r2, #0
	beq	.L373
	ldrh	r1, [r4, #226]
	movw	r3, #65535
	cmp	r1, r3
	bne	.L373
	movw	r3, #3246
	movw	r5, #3246
	ldrh	r1, [r4, r3]
	cmp	r1, r2
	movcs	r2, #0
	strhcs	r2, [r4, r3]	@ movhi
	ldrh	r3, [r4, r5]
	ldr	r2, .L378+4
	lsl	r3, r3, #1
	ldrh	r0, [r2, r3]
	bl	P2V_block_in_plane
	bl	FtlGcRefreshBlock
	ldrh	r3, [r4, r5]
	add	r3, r3, #1
	strh	r3, [r4, r5]	@ movhi
.L373:
	mov	r0, #0
	ldmfd	sp, {r4, r5, fp, sp, pc}
.L379:
	.align	2
.L378:
	.word	.LANCHOR0
	.word	.LANCHOR0+3212
	.size	FtlGcReFreshBadBlk, .-FtlGcReFreshBadBlk
	.align	2
	.global	ftl_malloc
	.syntax unified
	.arm
	.fpu softvfp
	.type	ftl_malloc, %function
ftl_malloc:
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 1, uses_anonymous_args = 0
	mov	ip, sp
	push	{fp, ip, lr, pc}
	sub	fp, ip, #4
	ldr	r1, .L381
	bl	__kmalloc
	ldmfd	sp, {fp, sp, pc}
.L382:
	.align	2
.L381:
	.word	37748929
	.size	ftl_malloc, .-ftl_malloc
	.align	2
	.global	ftl_free
	.syntax unified
	.arm
	.fpu softvfp
	.type	ftl_free, %function
ftl_free:
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 1, uses_anonymous_args = 0
	mov	ip, sp
	push	{fp, ip, lr, pc}
	sub	fp, ip, #4
	bl	kfree
	ldmfd	sp, {fp, sp, pc}
	.size	ftl_free, .-ftl_free
	.align	2
	.global	rknand_print_hex
	.syntax unified
	.arm
	.fpu softvfp
	.type	rknand_print_hex, %function
rknand_print_hex:
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 1, uses_anonymous_args = 0
	mov	ip, sp
	push	{r4, r5, r6, r7, r8, r9, r10, fp, ip, lr, pc}
	sub	fp, ip, #4
	mov	r6, #0
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
	bl	sftl_printk
	ldmfd	sp, {r4, r5, r6, r7, r8, r9, r10, fp, sp, pc}
.L391:
	cmp	r5, #0
	bne	.L386
	mov	r3, r6
	mov	r2, r7
	mov	r1, r10
	ldr	r0, .L393+4
	bl	sftl_printk
.L386:
	cmp	r8, #4
	ldreq	r1, [r7, r4, lsl #2]
	ldreq	r0, .L393+8
	beq	.L392
	cmp	r8, #2
	lsleq	r3, r4, #1
	ldreq	r0, .L393+12
	ldrbne	r1, [r7, r4]	@ zero_extendqisi2
	ldrheq	r1, [r7, r3]
	ldrne	r0, .L393+16
.L392:
	add	r5, r5, #1
	bl	sftl_printk
	cmp	r5, #15
	bls	.L390
	mov	r5, #0
	ldr	r0, .L393
	bl	sftl_printk
.L390:
	add	r4, r4, #1
	add	r6, r6, r8
	b	.L385
.L394:
	.align	2
.L393:
	.word	.LC85
	.word	.LC81
	.word	.LC82
	.word	.LC83
	.word	.LC84
	.size	rknand_print_hex, .-rknand_print_hex
	.align	2
	.global	FlashEraseBlocks
	.syntax unified
	.arm
	.fpu softvfp
	.type	FlashEraseBlocks, %function
FlashEraseBlocks:
	@ args = 0, pretend = 0, frame = 12
	@ frame_needed = 1, uses_anonymous_args = 0
	mov	ip, sp
	push	{r4, r5, r6, r7, r8, r9, r10, fp, ip, lr, pc}
	sub	fp, ip, #4
	sub	sp, sp, #12
	mov	r7, #0
	ldr	r6, .L411
	mov	r8, r2
	mov	r4, r0
	mov	r5, r0
	ldrh	r9, [r6, #12]
	lsl	r10, r9, #3
.L396:
	cmp	r7, r8
	beq	.L400
	sub	r2, fp, #48
	sub	r1, fp, #44
	mov	r0, r5
	bl	l2p_addr_tran
	ldr	ip, [fp, #-48]
	cmp	ip, #0
	bne	.L397
	ldr	r3, [fp, #-44]
	cmp	r10, r3
	bls	.L397
	ldr	r5, .L411+4
	ldr	r6, .L411+8
	b	.L410
.L399:
	mvn	r3, #0
	ldr	r2, [fp, #-44]
	str	r3, [r4, #-20]
	mov	r1, r5
	mov	r0, r6
	str	ip, [fp, #-52]
	bl	sftl_printk
	mov	r3, #16
	mov	r2, #4
	ldr	r1, [r4, #-12]
	ldr	r0, .L411+12
	bl	rknand_print_hex
	mov	r3, #4
	ldr	r1, [r4, #-8]
	mov	r2, r3
	ldr	r0, .L411+16
	bl	rknand_print_hex
	ldr	ip, [fp, #-52]
	add	ip, ip, #1
.L410:
	cmp	ip, r8
	add	r4, r4, #20
	bne	.L399
	bl	dump_stack
.L400:
	mov	r0, #0
	sub	sp, fp, #40
	ldmfd	sp, {r4, r5, r6, r7, r8, r9, r10, fp, sp, pc}
.L397:
	ldr	r3, [r6, #3252]
	uxtb	r0, ip
	ldr	r1, [fp, #-44]
	blx	r3
	cmp	r0, #0
	mvnne	r3, #0
	streq	r0, [r5]
	strne	r3, [r5]
	ldrh	r3, [r6, #14]
	cmp	r3, #4
	bne	.L404
	ldr	r1, [fp, #-44]
	ldr	r3, [r6, #3252]
	ldrb	r0, [fp, #-48]	@ zero_extendqisi2
	add	r1, r9, r1
	blx	r3
	cmp	r0, #0
	mvnne	r3, #0
	strne	r3, [r5]
.L404:
	add	r7, r7, #1
	add	r5, r5, #20
	b	.L396
.L412:
	.align	2
.L411:
	.word	.LANCHOR0
	.word	.LANCHOR1+130
	.word	.LC86
	.word	.LC87
	.word	.LC88
	.size	FlashEraseBlocks, .-FlashEraseBlocks
	.align	2
	.global	FtlFreeSysBlkQueueIn
	.syntax unified
	.arm
	.fpu softvfp
	.type	FtlFreeSysBlkQueueIn, %function
FtlFreeSysBlkQueueIn:
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 1, uses_anonymous_args = 0
	mov	ip, sp
	push	{r4, r5, r6, r7, fp, ip, lr, pc}
	sub	fp, ip, #4
	uxth	r6, r0
	movw	r2, #65533
	uxth	r1, r1
	sub	r3, r6, #1
	uxth	r3, r3
	cmp	r3, r2
	ldmfdhi	sp, {r4, r5, r6, r7, fp, sp, pc}
	ldr	r5, .L422
	add	r4, r5, #416
	ldrh	r3, [r4, #6]
	cmp	r3, #1024
	ldmfdeq	sp, {r4, r5, r6, r7, fp, sp, pc}
	cmp	r1, #0
	beq	.L415
	mov	r0, r6
	bl	P2V_block_in_plane
	mov	r7, r0
	ldr	r0, [r5, #3272]
	lsl	r3, r6, #10
	mov	r2, #1
	mov	r1, r2
	str	r3, [r0, #4]
	bl	FlashEraseBlocks
	ldr	r2, [r5, #2540]
	lsl	r0, r7, #1
	ldrh	r3, [r2, r0]
	add	r3, r3, #1
	strh	r3, [r2, r0]	@ movhi
	ldr	r3, [r5, #2612]
	add	r3, r3, #1
	str	r3, [r5, #2612]
.L415:
	ldrh	r3, [r4, #6]
	add	r3, r3, #1
	strh	r3, [r4, #6]	@ movhi
	ldrh	r3, [r4, #4]
	add	r2, r4, r3, lsl #1
	add	r3, r3, #1
	ubfx	r3, r3, #0, #10
	strh	r6, [r2, #8]	@ movhi
	strh	r3, [r4, #4]	@ movhi
	ldmfd	sp, {r4, r5, r6, r7, fp, sp, pc}
.L423:
	.align	2
.L422:
	.word	.LANCHOR0
	.size	FtlFreeSysBlkQueueIn, .-FtlFreeSysBlkQueueIn
	.align	2
	.global	FtlFreeSysBlkQueueOut
	.syntax unified
	.arm
	.fpu softvfp
	.type	FtlFreeSysBlkQueueOut, %function
FtlFreeSysBlkQueueOut:
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 1, uses_anonymous_args = 0
	mov	ip, sp
	push	{r4, r5, r6, fp, ip, lr, pc}
	sub	fp, ip, #4
	ldr	r6, .L429
	add	r4, r6, #416
	ldrh	r2, [r4, #6]
	cmp	r2, #0
	movweq	r5, #65535
	beq	.L425
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
.L425:
	sub	r3, r5, #1
	movw	r2, #65533
	uxth	r3, r3
	cmp	r3, r2
	bls	.L426
	ldrh	r2, [r4, #6]
	mov	r1, r5
	ldr	r0, .L429+4
	bl	sftl_printk
.L427:
	b	.L427
.L426:
	mov	r0, r5
	ldmfd	sp, {r4, r5, r6, fp, sp, pc}
.L430:
	.align	2
.L429:
	.word	.LANCHOR0
	.word	.LC89
	.size	FtlFreeSysBlkQueueOut, .-FtlFreeSysBlkQueueOut
	.align	2
	.global	ftl_map_blk_alloc_new_blk
	.syntax unified
	.arm
	.fpu softvfp
	.type	ftl_map_blk_alloc_new_blk, %function
ftl_map_blk_alloc_new_blk:
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 1, uses_anonymous_args = 0
	mov	ip, sp
	push	{r4, r5, r6, r7, fp, ip, lr, pc}
	sub	fp, ip, #4
	ldrh	r1, [r0, #10]
	mov	r4, r0
	mov	r3, #0
	ldr	r2, [r0, #12]
.L432:
	uxth	r5, r3
	cmp	r5, r1
	bcs	.L435
	mov	r7, r2
	add	r3, r3, #1
	ldrh	r6, [r7]
	add	r2, r2, #2
	cmp	r6, #0
	bne	.L432
	bl	FtlFreeSysBlkQueueOut
	sub	r3, r0, #1
	movw	r2, #65533
	uxth	r3, r3
	mov	r1, r0
	strh	r0, [r7]	@ movhi
	cmp	r3, r2
	bls	.L433
	ldr	r3, .L439
	ldr	r0, .L439+4
	ldrh	r2, [r3, #6]
	bl	sftl_printk
.L434:
	b	.L434
.L433:
	ldr	r3, [r4, #28]
	strh	r6, [r4, #2]	@ movhi
	strh	r5, [r4]	@ movhi
	add	r3, r3, #1
	str	r3, [r4, #28]
	ldrh	r3, [r4, #8]
	add	r3, r3, #1
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
	mov	r0, #0
	ldmfd	sp, {r4, r5, r6, r7, fp, sp, pc}
.L440:
	.align	2
.L439:
	.word	.LANCHOR0+416
	.word	.LC90
	.word	.LANCHOR1+147
	.word	.LC8
	.size	ftl_map_blk_alloc_new_blk, .-ftl_map_blk_alloc_new_blk
	.align	2
	.global	FlashGetBadBlockList
	.syntax unified
	.arm
	.fpu softvfp
	.type	FlashGetBadBlockList, %function
FlashGetBadBlockList:
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 1, uses_anonymous_args = 0
	mov	ip, sp
	push	{r4, r5, r6, fp, ip, lr, pc}
	sub	fp, ip, #4
	ldr	r5, .L448
	mov	r4, r0
	mov	r6, r1
	mov	r2, #256
	mov	r1, #255
	bl	memset
	ldr	r3, [r5, #3248]
	mov	r1, r6
	mov	r0, r4
	blx	r3
	uxth	r0, r0
	cmp	r0, #50
	bls	.L442
	mov	r2, #256
	mov	r1, #255
	mov	r0, r4
	bl	memset
	mov	r0, #0
.L442:
	ldrh	r3, [r5, #14]
	cmp	r3, #4
	moveq	r3, r4
	addeq	r1, r3, r0, lsl #1
	beq	.L444
	ldmfd	sp, {r4, r5, r6, fp, sp, pc}
.L445:
	ldrh	r2, [r3]
	lsr	r2, r2, #1
	strh	r2, [r3], #2	@ movhi
.L444:
	cmp	r3, r1
	bne	.L445
	ldmfd	sp, {r4, r5, r6, fp, sp, pc}
.L449:
	.align	2
.L448:
	.word	.LANCHOR0
	.size	FlashGetBadBlockList, .-FlashGetBadBlockList
	.align	2
	.global	ftl_memset
	.syntax unified
	.arm
	.fpu softvfp
	.type	ftl_memset, %function
ftl_memset:
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 1, uses_anonymous_args = 0
	mov	ip, sp
	push	{r4, fp, ip, lr, pc}
	sub	fp, ip, #4
	cmp	r2, #0
	mov	r4, r0
	beq	.L451
	bl	memset
.L451:
	mov	r0, r4
	ldmfd	sp, {r4, fp, sp, pc}
	.size	ftl_memset, .-ftl_memset
	.align	2
	.global	FtlMemInit
	.syntax unified
	.arm
	.fpu softvfp
	.type	FtlMemInit, %function
FtlMemInit:
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 1, uses_anonymous_args = 0
	mov	ip, sp
	push	{r4, r5, r6, r7, r8, r9, fp, ip, lr, pc}
	sub	fp, ip, #4
	ldr	r4, .L556
	movw	r3, #65535
	movw	r6, #310
	mov	r2, #32
	mov	r5, #0
	str	r3, [r4, #3280]
	mvn	r3, #0
	ldrh	r0, [r4, r6]
	mov	r7, #12
	strh	r3, [r4, #226]	@ movhi
	add	r9, r4, #316
	strh	r3, [r4, #224]	@ movhi
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
	ldrh	r3, [r4, #236]
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
	ldrh	r3, [r4, #236]
	movw	r8, #314
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
	movw	r3, #262
	str	r0, [r4, #3328]
	ldrh	r0, [r4, r3]
	lsl	r0, r0, #2
	bl	ftl_malloc
	ldrh	r3, [r9]
	ldrh	r6, [r4, #236]
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
	ldrh	r0, [r4, #246]
	lsl	r0, r0, #1
	uxth	r0, r0
	strh	r0, [r6]	@ movhi
	bl	ftl_malloc
	str	r0, [r4, #3348]
	ldrh	r0, [r6]
	ldr	r3, .L556+4
	add	r0, r0, #544
	add	r0, r0, #3
	lsr	r0, r0, #9
	strh	r0, [r6]	@ movhi
	and	r0, r3, r0, lsl #9
	bl	ftl_malloc
	ldrh	r6, [r4, #246]
	str	r0, [r4, #3352]
	add	r0, r0, #32
	str	r0, [r4, #2540]
	lsl	r6, r6, #1
	mov	r0, r6
	bl	ftl_malloc
	str	r0, [r4, #3356]
	mov	r0, r6
	bl	ftl_malloc
	ldr	r6, [r4, #332]
	str	r0, [r4, #76]
	lsl	r6, r6, #1
	mov	r0, r6
	bl	ftl_malloc
	str	r0, [r4, #3360]
	mov	r0, r6
	bl	ftl_malloc
	str	r0, [r4, #3364]
	add	r6, r4, #324
	ldrh	r0, [r4, #246]
	lsr	r0, r0, #3
	add	r0, r0, #4
	bl	ftl_malloc
	str	r0, [r4, #24]
	ldrh	r0, [r6]
	lsl	r0, r0, #1
	bl	ftl_malloc
	str	r0, [r4, #352]
	ldrh	r0, [r6]
	lsl	r0, r0, #1
	bl	ftl_malloc
	str	r0, [r4, #3368]
	ldrh	r0, [r6]
	movw	r6, #326
	lsl	r0, r0, #2
	bl	ftl_malloc
	str	r0, [r4, #3372]
	ldrh	r0, [r4, r6]
	lsl	r0, r0, #2
	bl	ftl_malloc
	ldrh	r2, [r4, r6]
	mov	r1, r5
	str	r0, [r4, #3376]
	movw	r5, #342
	movw	r6, #258
	lsl	r2, r2, #2
	bl	ftl_memset
	add	r3, r4, #340
	ldrh	r0, [r3]
	lsl	r0, r0, #2
	bl	ftl_malloc
	str	r0, [r4, #3380]
	ldr	r0, [r4, #332]
	lsl	r0, r0, #2
	bl	ftl_malloc
	str	r0, [r4, #3384]
	ldrh	r0, [r4, r5]
	mul	r0, r7, r0
	bl	ftl_malloc
	ldrh	r3, [r4, r5]
	add	r5, r4, #3392
	str	r0, [r4, #2544]
	ldrh	r0, [r4, r8]
	mul	r0, r0, r3
	bl	ftl_malloc
	str	r0, [r4, #3388]
	ldrh	r0, [r4, #246]
	lsl	r0, r0, #3
	bl	ftl_malloc
	movw	r3, #302
	str	r0, [r4, #2520]
	ldrh	r0, [r4, r3]
	ldrh	r3, [r4, r6]
	add	r0, r0, #31
	asr	r0, r0, #5
	strh	r0, [r5]	@ movhi
	mul	r0, r0, r3
	lsl	r0, r0, #2
	bl	ftl_malloc
	ldrh	r2, [r5]
	add	ip, r4, #384
	ldrh	lr, [r4, r6]
	mov	r3, #1
	str	r0, [r4, #384]
	lsl	r2, r2, #2
	mov	r1, r2
.L456:
	cmp	r3, lr
	bcc	.L457
	ldr	r2, .L556+8
	mov	r1, #0
	add	r3, r2, r3, lsl #2
	add	r2, r2, #56
	add	r3, r3, #24
.L458:
	cmp	r2, r3
	bne	.L459
	ldr	r3, [r4, #3360]
	cmp	r3, #0
	bne	.L460
.L462:
	ldr	r1, .L556+12
	ldr	r0, .L556+16
	bl	sftl_printk
	mvn	r0, #0
	ldmfd	sp, {r4, r5, r6, r7, r8, r9, fp, sp, pc}
.L457:
	ldr	r0, [r4, #384]
	add	r3, r3, #1
	add	r0, r0, r1
	add	r1, r1, r2
	str	r0, [ip, #4]!
	b	.L456
.L459:
	str	r1, [r3, #4]!
	b	.L458
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
	ldr	r3, .L556
	ldr	r2, [r3, #3368]
	cmp	r2, #0
	beq	.L462
	ldr	r2, [r3, #3372]
	cmp	r2, #0
	beq	.L462
	ldr	r3, [r3, #3376]
	cmp	r3, #0
	beq	.L462
	mov	r0, #0
	ldmfd	sp, {r4, r5, r6, r7, r8, r9, fp, sp, pc}
.L557:
	.align	2
.L556:
	.word	.LANCHOR0
	.word	33553920
	.word	.LANCHOR0+356
	.word	.LANCHOR1+173
	.word	.LC91
	.size	FtlMemInit, .-FtlMemInit
	.align	2
	.global	FtlBbt2Bitmap
	.syntax unified
	.arm
	.fpu softvfp
	.type	FtlBbt2Bitmap, %function
FtlBbt2Bitmap:
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 1, uses_anonymous_args = 0
	mov	ip, sp
	push	{r4, r5, r6, r7, r8, fp, ip, lr, pc}
	sub	fp, ip, #4
	ldr	r3, .L564
	mov	r4, r0
	mov	r6, r1
	ldr	r7, .L564+4
	sub	r5, r4, #2
	ldrh	r2, [r3]
	add	r4, r4, #1020
	ldr	r8, .L564+8
	add	r4, r4, #2
	mov	r1, #0
	mov	r0, r6
	lsl	r2, r2, #2
	bl	ftl_memset
.L561:
	ldrh	r3, [r5, #2]
	movw	r2, #65535
	cmp	r3, r2
	ldmfdeq	sp, {r4, r5, r6, r7, r8, fp, sp, pc}
	ldrh	r2, [r7]
	cmp	r2, r3
	bhi	.L560
	mov	r2, #74
	mov	r1, r8
	ldr	r0, .L564+12
	bl	sftl_printk
.L560:
	ldrh	r3, [r5, #2]!
	mov	r0, #1
	cmp	r4, r5
	lsr	r1, r3, #5
	and	r3, r3, #31
	ldr	r2, [r6, r1, lsl #2]
	orr	r3, r2, r0, lsl r3
	str	r3, [r6, r1, lsl #2]
	bne	.L561
	ldmfd	sp, {r4, r5, r6, r7, r8, fp, sp, pc}
.L565:
	.align	2
.L564:
	.word	.LANCHOR0+3392
	.word	.LANCHOR0+302
	.word	.LANCHOR1+184
	.word	.LC8
	.size	FtlBbt2Bitmap, .-FtlBbt2Bitmap
	.align	2
	.global	ftl_free_no_use_map_blk
	.syntax unified
	.arm
	.fpu softvfp
	.type	ftl_free_no_use_map_blk, %function
ftl_free_no_use_map_blk:
	@ args = 0, pretend = 0, frame = 4
	@ frame_needed = 1, uses_anonymous_args = 0
	mov	ip, sp
	push	{r4, r5, r6, r7, r8, r9, r10, fp, ip, lr, pc}
	sub	fp, ip, #4
	sub	sp, sp, #4
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
.L567:
	ldrh	r1, [r4, #6]
	uxth	r3, r2
	cmp	r1, r3
	bhi	.L571
	ldr	r3, .L586
	mov	r6, #0
	mov	r8, r6
	mov	r10, r6
	ldrh	r2, [r3]
	ldrh	r3, [r4]
	lsl	r3, r3, #1
	strh	r2, [r5, r3]	@ movhi
	ldrh	r9, [r5]
.L572:
	ldrh	r3, [r4, #10]
	uxth	r1, r6
	cmp	r3, r1
	bhi	.L576
	mov	r0, r8
	ldmib	sp, {r4, r5, r6, r7, r8, r9, r10, fp, sp, pc}
.L571:
	uxth	r3, r2
	mov	r1, #0
	ldr	r0, [r6, r3, lsl #2]
	ubfx	r0, r0, #10, #16
.L568:
	ldrh	ip, [r4, #10]
	uxth	r3, r1
	cmp	ip, r3
	addls	r2, r2, #1
	bls	.L567
.L570:
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
	b	.L568
.L576:
	uxth	r3, r6
	lsl	r3, r3, #1
	ldrh	r2, [r5, r3]
	cmp	r9, r2
	bls	.L573
	ldrh	r0, [r7, r3]
	add	ip, r7, r3
	cmp	r0, #0
	bne	.L574
.L575:
	add	r6, r6, #1
	b	.L572
.L573:
	cmp	r2, #0
	bne	.L575
	ldrh	r0, [r7, r3]
	add	ip, r7, r3
	cmp	r0, #0
	beq	.L575
.L577:
	mov	r1, #1
	str	ip, [fp, #-44]
	bl	FtlFreeSysBlkQueueIn
	ldr	ip, [fp, #-44]
	strh	r10, [ip]	@ movhi
	ldrh	r3, [r4, #8]
	sub	r3, r3, #1
	strh	r3, [r4, #8]	@ movhi
	b	.L575
.L574:
	subs	r9, r2, #0
	mov	r8, r1
	beq	.L577
	b	.L575
.L587:
	.align	2
.L586:
	.word	.LANCHOR0+308
	.size	ftl_free_no_use_map_blk, .-ftl_free_no_use_map_blk
	.align	2
	.global	FtlL2PDataInit
	.syntax unified
	.arm
	.fpu softvfp
	.type	FtlL2PDataInit, %function
FtlL2PDataInit:
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 1, uses_anonymous_args = 0
	mov	ip, sp
	push	{r4, r5, r6, r7, r8, fp, ip, lr, pc}
	sub	fp, ip, #4
	ldr	r4, .L591
	mov	r1, #0
	mvn	r5, #0
	ldr	r2, [r4, #332]
	ldr	r0, [r4, #3364]
	lsl	r2, r2, #1
	bl	ftl_memset
	movw	r3, #314
	movw	r2, #342
	ldrh	r3, [r4, r3]
	mov	r1, #255
	ldrh	r2, [r4, r2]
	ldr	r0, [r4, #3388]
	mul	r2, r2, r3
	bl	ftl_memset
	ldr	r0, .L591+4
	mov	r2, #0
	mov	r3, r4
	mov	lr, #12
	mov	r4, r2
	sub	r6, r0, #28
.L589:
	ldrh	r7, [r0]
	uxth	r1, r2
	add	ip, r2, #1
	cmp	r7, r1
	bhi	.L590
	ldr	r2, .L591+8
	mvn	r1, #0
	movw	r0, #3396
	strh	r1, [r3, r0]	@ movhi
	strh	r1, [r2, #2]	@ movhi
	ldr	r1, [r3, #332]
	strh	r1, [r2, #10]	@ movhi
	ldr	r1, .L591+12
	strh	r1, [r2, #4]	@ movhi
	ldrh	r1, [r2, #44]
	strh	r1, [r2, #8]	@ movhi
	sub	r1, r2, #3056
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
	ldmfd	sp, {r4, r5, r6, r7, r8, fp, sp, pc}
.L590:
	uxth	r2, r2
	ldr	r7, [r3, #2544]
	mul	r1, lr, r2
	add	r8, r7, r1
	str	r4, [r8, #4]
	strh	r5, [r7, r1]	@ movhi
	ldr	r7, [r3, #2544]
	add	r1, r7, r1
	ldrh	r7, [r6]
	mul	r2, r2, r7
	ldr	r7, [r3, #3388]
	bic	r2, r2, #3
	add	r2, r7, r2
	str	r2, [r1, #8]
	mov	r2, ip
	b	.L589
.L592:
	.align	2
.L591:
	.word	.LANCHOR0
	.word	.LANCHOR0+342
	.word	.LANCHOR0+3396
	.word	-3902
	.size	FtlL2PDataInit, .-FtlL2PDataInit
	.align	2
	.global	FtlVariablesInit
	.syntax unified
	.arm
	.fpu softvfp
	.type	FtlVariablesInit, %function
FtlVariablesInit:
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 1, uses_anonymous_args = 0
	mov	ip, sp
	push	{r4, r5, fp, ip, lr, pc}
	sub	fp, ip, #4
	ldr	r4, .L594
	mvn	r3, #0
	movw	r2, #3442
	mov	r5, #0
	strh	r3, [r4, r2]	@ movhi
	mov	r1, r5
	str	r3, [r4, #3452]
	add	r3, r4, #348
	strh	r5, [r3]	@ movhi
	add	r3, r4, #324
	ldrh	r2, [r3]
	ldr	r0, [r4, #352]
	str	r5, [r4, #3444]
	str	r5, [r4, #3448]
	lsl	r2, r2, #1
	bl	ftl_memset
	ldrh	r2, [r4, #246]
	mov	r1, r5
	ldr	r0, [r4, #2540]
	lsl	r2, r2, #1
	bl	ftl_memset
	ldrh	r2, [r4, #246]
	mov	r1, r5
	ldr	r0, [r4, #3348]
	lsl	r2, r2, #1
	bl	ftl_memset
	add	r0, r4, #2464
	mov	r1, #48
	add	r0, r0, #8
	bl	__memzero
	add	r0, r4, #2640
	mov	r1, #512
	add	r0, r0, #4
	bl	__memzero
	bl	FtlGcBufInit
	bl	FtlL2PDataInit
	mov	r0, r5
	ldmfd	sp, {r4, r5, fp, sp, pc}
.L595:
	.align	2
.L594:
	.word	.LANCHOR0
	.size	FtlVariablesInit, .-FtlVariablesInit
	.align	2
	.global	SupperBlkListInit
	.syntax unified
	.arm
	.fpu softvfp
	.type	SupperBlkListInit, %function
SupperBlkListInit:
	@ args = 0, pretend = 0, frame = 12
	@ frame_needed = 1, uses_anonymous_args = 0
	mov	ip, sp
	push	{r4, r5, r6, r7, r8, r9, r10, fp, ip, lr, pc}
	sub	fp, ip, #4
	sub	sp, sp, #12
	mov	r5, #0
	ldr	r4, .L607
	mov	r1, #0
	mov	r8, r5
	mov	r6, r5
	ldrh	r2, [r4, #246]
	ldr	r0, [r4, #2520]
	lsl	r2, r2, #3
	bl	ftl_memset
	movw	r3, #2532
	str	r5, [r4, #2536]
	str	r5, [r4, #2524]
	str	r5, [r4, #2528]
	strh	r5, [r4, r3]	@ movhi
	strh	r5, [r4, #228]	@ movhi
.L597:
	ldrh	r3, [r4, #244]
	uxth	r10, r5
	cmp	r10, r3
	bcs	.L604
	ldr	r3, .L607+4
	mov	r9, r10
	ldrh	ip, [r4, #236]
	ldrh	r3, [r3]
	str	r3, [fp, #-44]
	mov	r3, #0
	mov	r7, r3
	b	.L605
.L599:
	ldr	r2, .L607+8
	mov	r1, r9
	str	ip, [fp, #-52]
	str	r3, [fp, #-48]
	ldrb	r0, [r2, r3]	@ zero_extendqisi2
	bl	V2P_block
	bl	FtlBbmIsBadBlock
	cmp	r0, #0
	ldr	r3, [fp, #-48]
	ldreq	r2, [fp, #-44]
	ldr	ip, [fp, #-52]
	add	r3, r3, #1
	addeq	r7, r7, r2
	uxtheq	r7, r7
.L605:
	uxth	r1, r3
	cmp	ip, r1
	bhi	.L599
	cmp	r7, #0
	beq	.L600
	mov	r1, r7
	mov	r0, #32768
	bl	__divsi3
	uxth	r7, r0
.L601:
	ldr	r3, [r4, #2520]
	add	r3, r3, r9, lsl #3
	strh	r7, [r3, #4]	@ movhi
	ldrh	r3, [r4, #28]
	cmp	r3, r10
	beq	.L602
	ldrh	r3, [r4, #80]
	cmp	r3, r10
	beq	.L602
	ldrh	r3, [r4, #128]
	cmp	r3, r10
	beq	.L602
	ldr	r2, [r4, #76]
	lsl	r3, r9, #1
	ldrh	r3, [r2, r3]
	cmp	r3, #0
	bne	.L603
	add	r8, r8, #1
	mov	r0, r9
	uxth	r8, r8
	bl	INSERT_FREE_LIST
.L602:
	add	r5, r5, #1
	b	.L597
.L600:
	ldr	r1, [r4, #76]
	lsl	r3, r9, #1
	mvn	r0, #0
	strh	r0, [r1, r3]	@ movhi
	b	.L601
.L603:
	add	r6, r6, #1
	mov	r0, r9
	uxth	r6, r6
	bl	INSERT_DATA_LIST
	b	.L602
.L604:
	movw	r2, #2532
	strh	r8, [r4, #228]	@ movhi
	strh	r6, [r4, r2]	@ movhi
	add	r6, r6, r8
	cmp	r6, r3
	ble	.L606
	movw	r2, #2210
	ldr	r1, .L607+12
	ldr	r0, .L607+16
	bl	sftl_printk
.L606:
	mov	r0, #0
	sub	sp, fp, #40
	ldmfd	sp, {r4, r5, r6, r7, r8, r9, r10, fp, sp, pc}
.L608:
	.align	2
.L607:
	.word	.LANCHOR0
	.word	.LANCHOR0+306
	.word	.LANCHOR0+264
	.word	.LANCHOR1+198
	.word	.LC8
	.size	SupperBlkListInit, .-SupperBlkListInit
	.align	2
	.global	FtlGcPageVarInit
	.syntax unified
	.arm
	.fpu softvfp
	.type	FtlGcPageVarInit, %function
FtlGcPageVarInit:
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 1, uses_anonymous_args = 0
	mov	ip, sp
	push	{r4, r5, fp, ip, lr, pc}
	sub	fp, ip, #4
	ldr	r4, .L610
	mov	r3, #0
	movw	r5, #310
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
	mov	r1, #255
	ldr	r0, [r4, #3204]
	mul	r2, r2, r3
	bl	ftl_memset
	bl	FtlGcBufInit
	ldmfd	sp, {r4, r5, fp, sp, pc}
.L611:
	.align	2
.L610:
	.word	.LANCHOR0
	.size	FtlGcPageVarInit, .-FtlGcPageVarInit
	.align	2
	.global	ftl_memcpy
	.syntax unified
	.arm
	.fpu softvfp
	.type	ftl_memcpy, %function
ftl_memcpy:
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 1, uses_anonymous_args = 0
	mov	ip, sp
	push	{fp, ip, lr, pc}
	sub	fp, ip, #4
	bl	memcpy
	ldmfd	sp, {fp, sp, pc}
	.size	ftl_memcpy, .-ftl_memcpy
	.align	2
	.global	FlashReadPages
	.syntax unified
	.arm
	.fpu softvfp
	.type	FlashReadPages, %function
FlashReadPages:
	@ args = 0, pretend = 0, frame = 12
	@ frame_needed = 1, uses_anonymous_args = 0
	mov	ip, sp
	push	{r4, r5, r6, r7, r8, r9, r10, fp, ip, lr, pc}
	sub	fp, ip, #4
	sub	sp, sp, #12
	mov	r7, #0
	ldr	r5, .L652
	mov	r8, r1
	ldr	r9, .L652+4
	mov	r4, r0
	ldr	r10, .L652+8
	ldrh	r3, [r5, #12]
	str	r3, [fp, #-52]
.L614:
	cmp	r7, r8
	bne	.L627
	mov	r0, #0
	sub	sp, fp, #40
	ldmfd	sp, {r4, r5, r6, r7, r8, r9, r10, fp, sp, pc}
.L627:
	ldr	r3, [r4, #8]
	cmp	r3, #0
	beq	.L615
	ldr	r3, [r4, #12]
	cmp	r3, #0
	bne	.L616
.L615:
	mov	r2, #96
	mov	r1, r9
	mov	r0, r10
	bl	sftl_printk
.L616:
	sub	r2, fp, #48
	sub	r1, fp, #44
	mov	r0, r4
	bl	l2p_addr_tran
	ldr	r0, [fp, #-48]
	cmp	r0, #3
	mvnhi	r3, #0
	strhi	r3, [r4]
	bhi	.L618
	ldr	r6, [r4, #8]
	ldr	r3, [r4, #12]
	ldr	ip, [r5, #3260]
	tst	r6, #63
	ldr	r1, [fp, #-44]
	ldrne	r6, [r5, #3324]
	mov	r2, r6
	blx	ip
	str	r0, [r4]
	ldrh	r3, [r5, #14]
	cmp	r3, #4
	bne	.L621
	ldr	r0, [fp, #-52]
	add	r2, r6, #2048
	ldr	r3, [r4, #12]
	ldr	r1, [fp, #-44]
	ldr	ip, [r5, #3260]
	add	r3, r3, #8
	add	r1, r0, r1
	ldrb	r0, [fp, #-48]	@ zero_extendqisi2
	blx	ip
	cmn	r0, #1
	beq	.L622
	ldr	r3, [r4, #12]
	ldr	r2, [r3, #12]
	cmn	r2, #1
	bne	.L623
	ldr	r2, [r3, #8]
	cmn	r2, #1
	bne	.L623
	ldr	r3, [r3]
	cmn	r3, #1
	beq	.L623
.L622:
	mvn	r3, #0
	str	r3, [r4]
.L623:
	ldr	r3, [r4]
	sub	r0, r0, #256
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
	bne	.L621
	ldr	r1, [r4, #4]
	ldr	r2, [fp, #-44]
	ldr	r0, .L652+12
	bl	sftl_printk
	ldr	r1, [r4, #8]
	cmp	r1, #0
	beq	.L626
	mov	r3, #4
	ldr	r0, .L652+16
	mov	r2, r3
	bl	rknand_print_hex
.L626:
	ldr	r1, [r4, #12]
	cmp	r1, #0
	beq	.L621
	mov	r3, #4
	ldr	r0, .L652+20
	mov	r2, r3
	bl	rknand_print_hex
.L621:
	ldr	r3, [r5, #3324]
	cmp	r6, r3
	bne	.L618
	ldr	r0, [r4, #8]
	cmp	r6, r0
	beq	.L618
	ldr	r3, .L652+24
	mov	r1, r6
	ldrh	r2, [r3]
	lsl	r2, r2, #9
	bl	ftl_memcpy
.L618:
	add	r7, r7, #1
	add	r4, r4, #20
	b	.L614
.L653:
	.align	2
.L652:
	.word	.LANCHOR0
	.word	.LANCHOR1+216
	.word	.LC8
	.word	.LC92
	.word	.LC93
	.word	.LC94
	.word	.LANCHOR0+262
	.size	FlashReadPages, .-FlashReadPages
	.align	2
	.global	FtlLoadFactoryBbt
	.syntax unified
	.arm
	.fpu softvfp
	.type	FtlLoadFactoryBbt, %function
FtlLoadFactoryBbt:
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 1, uses_anonymous_args = 0
	mov	ip, sp
	push	{r4, r5, r6, r7, r8, r9, r10, fp, ip, lr, pc}
	sub	fp, ip, #4
	ldr	r5, .L664
	mov	r7, #0
	add	r6, r5, #364
	ldr	r3, [r5, #3304]
	ldr	r8, [r5, #3336]
	add	r6, r6, #2
	sub	r9, r6, #64
	str	r3, [r5, #3464]
	str	r8, [r5, #3468]
.L655:
	ldr	r3, .L664+4
	ldrh	r3, [r3]
	cmp	r7, r3
	bcc	.L660
	mov	r0, #0
	ldmfd	sp, {r4, r5, r6, r7, r8, r9, r10, fp, sp, pc}
.L660:
	ldrh	r4, [r9]
	mvn	r3, #0
	ldr	r10, .L664+8
	strh	r3, [r6, #2]!	@ movhi
	add	r4, r4, r3
	uxth	r4, r4
.L656:
	ldrh	r3, [r9]
	sub	r2, r3, #16
	cmp	r4, r2
	ble	.L658
	mla	r3, r7, r3, r4
	mov	r2, #1
	mov	r1, r2
	mov	r0, r10
	lsl	r3, r3, #10
	str	r3, [r5, #3460]
	bl	FlashReadPages
	ldr	r3, [r5, #3456]
	cmn	r3, #1
	beq	.L657
	ldrh	r2, [r8]
	movw	r3, #61664
	cmp	r2, r3
	bne	.L657
	strh	r4, [r6]	@ movhi
.L658:
	add	r7, r7, #1
	b	.L655
.L657:
	sub	r4, r4, #1
	uxth	r4, r4
	b	.L656
.L665:
	.align	2
.L664:
	.word	.LANCHOR0
	.word	.LANCHOR0+258
	.word	.LANCHOR0+3456
	.size	FtlLoadFactoryBbt, .-FtlLoadFactoryBbt
	.align	2
	.global	FtlGetLastWrittenPage
	.syntax unified
	.arm
	.fpu softvfp
	.type	FtlGetLastWrittenPage, %function
FtlGetLastWrittenPage:
	@ args = 0, pretend = 0, frame = 84
	@ frame_needed = 1, uses_anonymous_args = 0
	mov	ip, sp
	push	{r4, r5, r6, r7, r8, fp, ip, lr, pc}
	sub	fp, ip, #4
	sub	sp, sp, #84
	cmp	r1, #1
	ldr	r3, .L677
	movwne	r2, #306
	lsl	r6, r0, #10
	mov	r7, r1
	sub	r0, fp, #116
	addeq	r2, r3, #308
	ldrhne	r5, [r3, r2]
	ldrheq	r5, [r2]
	mov	r2, r1
	ldr	r3, [r3, #3328]
	mov	r1, #1
	sub	r5, r5, #1
	sxth	r5, r5
	str	r3, [fp, #-108]
	sub	r3, fp, #96
	str	r3, [fp, #-104]
	orr	r3, r5, r6
	str	r3, [fp, #-112]
	bl	FlashReadPages
	ldr	r3, [fp, #-96]
	cmn	r3, #1
	moveq	r8, #0
	beq	.L670
.L669:
	mov	r0, r5
	sub	sp, fp, #32
	ldmfd	sp, {r4, r5, r6, r7, r8, fp, sp, pc}
.L673:
	add	r3, r8, r5
	mov	r2, r7
	add	r3, r3, r3, lsr #31
	mov	r1, #1
	sub	r0, fp, #116
	asr	r4, r3, #1
	sxth	r3, r4
	orr	r3, r3, r6
	str	r3, [fp, #-112]
	bl	FlashReadPages
	ldr	r3, [fp, #-96]
	cmn	r3, #1
	bne	.L671
	ldr	r3, [fp, #-92]
	cmn	r3, #1
	bne	.L671
	ldr	r3, [fp, #-116]
	cmn	r3, #1
	subne	r4, r4, #1
	sxthne	r5, r4
	bne	.L670
.L671:
	add	r4, r4, #1
	sxth	r8, r4
.L670:
	cmp	r8, r5
	ble	.L673
	b	.L669
.L678:
	.align	2
.L677:
	.word	.LANCHOR0
	.size	FtlGetLastWrittenPage, .-FtlGetLastWrittenPage
	.align	2
	.global	FtlScanSysBlk
	.syntax unified
	.arm
	.fpu softvfp
	.type	FtlScanSysBlk, %function
FtlScanSysBlk:
	@ args = 0, pretend = 0, frame = 24
	@ frame_needed = 1, uses_anonymous_args = 0
	mov	ip, sp
	push	{r4, r5, r6, r7, r8, r9, r10, fp, ip, lr, pc}
	sub	fp, ip, #4
	sub	sp, sp, #24
	mov	r5, #0
	ldr	r4, .L761
	mov	r1, r5
	ldr	r2, [r4, #332]
	add	r3, r4, #3440
	strh	r5, [r3]	@ movhi
	add	r3, r4, #348
	ldr	r0, [r4, #3384]
	add	r6, r4, #324
	strh	r5, [r3]	@ movhi
	mov	r7, r4
	lsl	r2, r2, #2
	bl	ftl_memset
	ldr	r2, [r4, #332]
	mov	r1, r5
	ldr	r0, [r4, #3360]
	lsl	r2, r2, #1
	bl	ftl_memset
	ldrh	r2, [r6]
	mov	r1, r5
	ldr	r0, [r4, #3372]
	lsl	r2, r2, #2
	bl	ftl_memset
	ldrh	r2, [r6]
	mov	r1, r5
	ldr	r0, [r4, #352]
	mov	r5, r4
	lsl	r2, r2, #1
	bl	ftl_memset
	mov	r2, #16
	mov	r1, #255
	add	r0, r6, #2304
	bl	memset
	ldrh	r3, [r4, #244]
	str	r3, [fp, #-44]
.L680:
	ldrh	r3, [r5, #246]
	ldr	r2, [fp, #-44]
	cmp	r3, r2
	bls	.L721
	ldr	r8, .L761+4
	mov	r6, #0
	ldrh	ip, [r5, #236]
	mov	r10, r6
	ldr	r9, [r5, #3292]
	ldrh	r1, [r8], #-52
	ldr	r2, [r5, #3180]
	ldr	r3, [r5, #3184]
	str	r1, [fp, #-48]
	b	.L722
.L682:
	ldr	r1, [fp, #-44]
	ldrb	r0, [r8, r6]	@ zero_extendqisi2
	str	r3, [fp, #-64]
	str	r2, [fp, #-60]
	str	ip, [fp, #-56]
	bl	V2P_block
	str	r0, [fp, #-52]
	bl	FtlBbmIsBadBlock
	cmp	r0, #0
	ldr	ip, [fp, #-56]
	ldr	r2, [fp, #-60]
	ldr	r3, [fp, #-64]
	bne	.L681
	mov	r1, #20
	mla	r0, r1, r10, r9
	ldr	r1, [fp, #-52]
	lsl	r1, r1, #10
	stmib	r0, {r1, r2}
	ldr	r1, [fp, #-48]
	mul	r1, r1, r10
	add	lr, r1, #3
	cmp	r1, #0
	movlt	r1, lr
	bic	r1, r1, #3
	add	r1, r3, r1
	str	r1, [r0, #12]
	add	r1, r10, #1
	uxth	r10, r1
.L681:
	add	r6, r6, #1
.L722:
	uxth	r1, r6
	cmp	ip, r1
	bhi	.L682
	cmp	r10, #0
	bne	.L683
.L720:
	ldr	r3, [fp, #-44]
	add	r3, r3, #1
	uxth	r3, r3
	str	r3, [fp, #-44]
	b	.L680
.L683:
	mov	r2, #1
	mov	r1, r10
	mov	r0, r9
	bl	FlashReadPages
	mov	r3, #0
.L760:
	str	r3, [fp, #-48]
	ldrh	r3, [fp, #-48]
	cmp	r10, r3
	bls	.L720
	ldr	r3, [fp, #-48]
	mov	r9, #20
	mul	r9, r9, r3
	ldr	r3, [r4, #3292]
	add	r2, r3, r9
	ldr	r3, [r3, r9]
	ldr	r1, [r2, #4]
	ldr	r6, [r2, #12]
	cmn	r3, #1
	lsr	ip, r1, #10
	uxth	r8, ip
	bne	.L687
	mov	r3, #16
.L689:
	ldr	r0, [r4, #3292]
	str	ip, [fp, #-56]
	str	r3, [fp, #-52]
	add	r0, r0, r9
	ldr	r2, [r0, #4]
	add	r2, r2, #1
	str	r2, [r0, #4]
	mov	r2, #1
	mov	r1, r2
	bl	FlashReadPages
	ldrh	r2, [r6]
	movw	r3, #65535
	ldr	ip, [fp, #-56]
	cmp	r2, r3
	ldr	r3, [fp, #-52]
	bne	.L686
	ldr	r3, [r4, #3292]
	mvn	r2, #0
	str	r2, [r3, r9]
	ldr	r3, [r4, #3292]
	ldr	r3, [r3, r9]
	cmp	r3, r2
	bne	.L687
.L688:
	mov	r1, #1
	b	.L759
.L686:
	ldr	r2, [r4, #3292]
	ldr	r2, [r2, r9]
	cmn	r2, #1
	bne	.L687
	sub	r3, r3, #1
	uxth	r3, r3
	cmp	r3, #0
	bne	.L689
	b	.L688
.L687:
	ldr	r2, [r5, #2592]
	ldr	r3, [r6, #4]
	cmn	r2, #1
	beq	.L690
	cmp	r2, r3
	bhi	.L691
.L690:
	cmn	r3, #1
	addne	r2, r3, #1
	strne	r2, [r7, #2592]
.L691:
	ldrh	r2, [r6]
	movw	r1, #61604
	cmp	r2, r1
	beq	.L693
	bhi	.L694
	movw	r3, #61574
	cmp	r2, r3
	beq	.L695
.L692:
	ldr	r3, [fp, #-48]
	add	r3, r3, #1
	b	.L760
.L694:
	movw	r3, #61634
	cmp	r2, r3
	beq	.L696
	movw	r3, #65535
	cmp	r2, r3
	moveq	r1, #0
	bne	.L692
.L759:
	uxth	r0, ip
	bl	FtlFreeSysBlkQueueIn
	b	.L692
.L696:
	ldr	r9, .L761+8
	ldr	r3, [r4, #332]
	ldrh	r2, [r9]
	cmp	r2, r3
	bls	.L698
	movw	r2, #1225
	ldr	r1, .L761+12
	ldr	r0, .L761+16
	bl	sftl_printk
.L698:
	ldr	r0, [r4, #332]
	ldrh	r2, [r9]
	ldr	ip, [r4, #3384]
	uxth	r1, r0
	sub	r3, r1, #1
	sub	r1, r1, r2
	sub	r1, r1, #1
	sxth	r3, r3
	sxth	r1, r1
	str	r1, [fp, #-52]
.L699:
	ldr	r1, [fp, #-52]
	cmp	r3, r1
	bgt	.L705
	cmp	r3, #0
	bge	.L737
	b	.L692
.L705:
	lsl	lr, r3, #2
	ldr	r1, [r6, #4]
	str	lr, [fp, #-56]
	ldr	lr, [ip, r3, lsl #2]
	cmp	r1, lr
	bls	.L700
	ldr	r1, [ip]
	cmp	r1, #0
	bne	.L701
	cmp	r0, r2
	addne	r2, r2, #1
	strhne	r2, [r9]	@ movhi
.L701:
	uxth	ip, r3
	mov	r1, #0
.L702:
	uxth	r0, r1
	sxth	r2, r1
	cmp	ip, r0
	bhi	.L703
	ldr	r1, [r6, #4]
	cmp	r3, #0
	ldr	r2, [r7, #3384]
	ldr	r0, [fp, #-56]
	str	r1, [r2, r0]
	lsl	r2, r3, #1
	ldr	r1, [r7, #3360]
	strh	r8, [r1, r2]	@ movhi
	blt	.L692
	ldr	r2, .L761+8
	ldr	r1, [r7, #332]
	ldrh	r2, [r2]
	sub	r1, r1, r2
	sub	r1, r1, #1
	sxth	r1, r1
	cmp	r3, r1
	bgt	.L692
.L737:
	add	r2, r2, #1
	ldr	r1, [r6, #4]
	strh	r2, [r9]	@ movhi
	ldr	r2, [r4, #3384]
	str	r1, [r2, r3, lsl #2]
	lsl	r3, r3, #1
	ldr	r2, [r4, #3360]
.L757:
	strh	r8, [r2, r3]	@ movhi
	b	.L692
.L703:
	ldr	r0, [r5, #3384]
	add	r1, r1, #1
	add	lr, r0, r2, lsl #2
	ldr	lr, [lr, #4]
	str	lr, [r0, r2, lsl #2]
	lsl	r2, r2, #1
	ldr	r0, [r5, #3360]
	add	lr, r0, r2
	ldrh	lr, [lr, #2]
	strh	lr, [r0, r2]	@ movhi
	b	.L702
.L700:
	sub	r3, r3, #1
	sxth	r3, r3
	b	.L699
.L695:
	ldr	r9, .L761+20
	ldrh	r2, [r9]
	ldrh	r3, [r9, #-24]
	cmp	r2, r3
	bls	.L708
	movw	r2, #1266
	ldr	r1, .L761+12
	ldr	r0, .L761+16
	bl	sftl_printk
.L708:
	ldrh	r2, [r9, #-24]
	ldrh	r1, [r9]
	ldr	ip, [r4, #3372]
	sub	r0, r2, #1
	sxth	r3, r0
	sub	r0, r0, r1
	str	r0, [fp, #-52]
.L709:
	ldr	r0, [fp, #-52]
	cmp	r3, r0
	ble	.L714
	lsl	lr, r3, #2
	ldr	r0, [r6, #4]
	str	lr, [fp, #-56]
	ldr	lr, [ip, r3, lsl #2]
	cmp	r0, lr
	bls	.L710
	sub	r2, r2, r1
	ldr	r0, [ip]
	clz	r2, r2
	uxth	ip, r3
	lsr	r2, r2, #5
	cmp	r0, #0
	orrne	r2, r2, #1
	cmp	r2, #0
	addeq	r1, r1, #1
	strheq	r1, [r9]	@ movhi
	mov	r1, #0
.L712:
	uxth	r0, r1
	sxth	r2, r1
	cmp	ip, r0
	bhi	.L713
	ldr	r1, [r6, #4]
	ldr	r2, [r7, #3372]
	ldr	r0, [fp, #-56]
	str	r1, [r2, r0]
	lsl	r2, r3, #1
	ldr	r1, [r7, #352]
	strh	r8, [r1, r2]	@ movhi
.L714:
	cmp	r3, #0
	blt	.L692
	ldr	r0, .L761+20
	ldrh	r2, [r0, #-24]
	ldrh	r1, [r0]
	sub	r2, r2, #1
	sub	r2, r2, r1
	sxth	r2, r2
	cmp	r3, r2
	bgt	.L692
	add	r1, r1, #1
	ldr	r2, [r4, #3372]
	strh	r1, [r0]	@ movhi
	ldr	r1, [r6, #4]
	str	r1, [r2, r3, lsl #2]
	lsl	r3, r3, #1
	ldr	r2, [r4, #352]
	b	.L757
.L713:
	ldr	r0, [r5, #3372]
	add	r1, r1, #1
	add	lr, r0, r2, lsl #2
	ldr	lr, [lr, #4]
	str	lr, [r0, r2, lsl #2]
	lsl	r2, r2, #1
	ldr	r0, [r5, #352]
	add	lr, r0, r2
	ldrh	lr, [lr, #2]
	strh	lr, [r0, r2]	@ movhi
	b	.L712
.L710:
	sub	r3, r3, #1
	sxth	r3, r3
	b	.L709
.L693:
	ldr	r9, .L761+24
	movw	r2, #65535
	ldrh	r1, [r9]
	cmp	r1, r2
	strheq	r8, [r9]	@ movhi
	beq	.L758
	ldrh	r0, [r9, #4]
	cmp	r0, r2
	beq	.L717
	mov	r1, #1
	bl	FtlFreeSysBlkQueueIn
.L717:
	ldr	r3, [r6, #4]
	ldr	r2, [r4, #2636]
	cmp	r2, r3
	strhcs	r8, [r9, #4]	@ movhi
	bcs	.L692
	ldrh	r3, [r9]
	strh	r8, [r9]	@ movhi
	strh	r3, [r9, #4]	@ movhi
	ldr	r3, [r6, #4]
.L758:
	str	r3, [r4, #2636]
	b	.L692
.L721:
	ldr	r2, [r5, #3360]
	ldrh	r3, [r2]
	cmp	r3, #0
	beq	.L723
.L726:
	ldr	r0, [r4, #352]
	ldrh	r2, [r0]
	cmp	r2, #0
	beq	.L724
.L725:
	ldr	r3, .L761+8
	ldrh	r2, [r3]
	ldr	r3, [r4, #332]
	cmp	r2, r3
	bls	.L756
	movw	r2, #1391
	ldr	r1, .L761+12
	ldr	r0, .L761+16
	bl	sftl_printk
.L756:
	mov	r0, #0
	sub	sp, fp, #40
	ldmfd	sp, {r4, r5, r6, r7, r8, r9, r10, fp, sp, pc}
.L723:
	ldr	r1, .L761+8
	ldrh	r1, [r1]
	cmp	r1, #0
	ldrne	r1, [r5, #332]
	beq	.L726
.L727:
	sxth	ip, r3
	cmp	ip, r1
	bcs	.L726
	lsl	r0, ip, #1
	add	r3, r3, #1
	ldrh	r0, [r2, r0]
	cmp	r0, #0
	beq	.L727
	mov	r3, ip
	mov	lr, #0
.L728:
	ldr	r2, [r7, #332]
	cmp	r3, r2
	bcs	.L726
	ldr	r1, [r7, #3360]
	lsl	r2, r3, #1
	sub	r0, r3, ip
	lsl	r5, r0, #1
	ldrh	r6, [r1, r2]
	strh	r6, [r1, r5]	@ movhi
	ldr	r1, [r7, #3384]
	ldr	r5, [r1, r3, lsl #2]
	add	r3, r3, #1
	sxth	r3, r3
	str	r5, [r1, r0, lsl #2]
	ldr	r1, [r7, #3360]
	strh	lr, [r1, r2]	@ movhi
	b	.L728
.L724:
	ldr	r3, .L761+20
	ldrh	r1, [r3]
	cmp	r1, #0
	ldrhne	ip, [r3, #-24]!
	movne	r1, r3
	beq	.L725
.L733:
	sxth	r3, r2
	cmp	r3, ip
	mov	lr, r3
	bge	.L725
	lsl	r5, r3, #1
	add	r2, r2, #1
	ldrh	r5, [r0, r5]
	cmp	r5, #0
	beq	.L733
	mov	r5, #0
.L734:
	ldrh	r2, [r1]
	cmp	r3, r2
	bge	.L725
	ldr	r0, [r4, #352]
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
	ldr	r0, [r4, #352]
	strh	r5, [r0, r2]	@ movhi
	b	.L734
.L762:
	.align	2
.L761:
	.word	.LANCHOR0
	.word	.LANCHOR0+316
	.word	.LANCHOR0+3440
	.word	.LANCHOR1+231
	.word	.LC8
	.word	.LANCHOR0+348
	.word	.LANCHOR0+2628
	.size	FtlScanSysBlk, .-FtlScanSysBlk
	.align	2
	.global	FtlLoadBbt
	.syntax unified
	.arm
	.fpu softvfp
	.type	FtlLoadBbt, %function
FtlLoadBbt:
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 1, uses_anonymous_args = 0
	mov	ip, sp
	push	{r4, r5, r6, r7, r8, fp, ip, lr, pc}
	sub	fp, ip, #4
	ldr	r4, .L795
	ldr	r8, .L795+4
	ldr	r3, [r4, #3304]
	add	r7, r4, #3456
	ldr	r6, [r4, #3336]
	str	r3, [r4, #3464]
	str	r6, [r4, #3468]
	bl	FtlBbtMemInit
	movw	r3, #302
	ldrh	r5, [r4, r3]
	sub	r5, r5, #1
	uxth	r5, r5
.L764:
	ldrh	r3, [r8]
	sub	r3, r3, #16
	cmp	r5, r3
	ble	.L767
	lsl	r3, r5, #10
	mov	r2, #1
	mov	r1, r2
	mov	r0, r7
	str	r3, [r4, #3460]
	bl	FlashReadPages
	ldr	r3, [r4, #3456]
	cmn	r3, #1
	bne	.L765
	ldr	r3, [r4, #3460]
	mov	r2, #1
	mov	r1, r2
	mov	r0, r7
	add	r3, r3, #1
	str	r3, [r4, #3460]
	bl	FlashReadPages
.L765:
	ldr	r3, [r4, #3456]
	cmn	r3, #1
	beq	.L766
	ldrh	r2, [r6]
	movw	r3, #61649
	cmp	r2, r3
	bne	.L766
	ldr	r2, [r6, #4]
	ldr	r3, .L795+8
	str	r2, [r4, #364]
	ldrh	r2, [r6, #8]
	strh	r5, [r3]	@ movhi
	strh	r2, [r3, #4]	@ movhi
.L767:
	ldr	r5, .L795+8
	movw	r2, #65535
	ldrh	r3, [r5]
	cmp	r3, r2
	beq	.L781
	ldrh	r3, [r5, #4]
	cmp	r3, r2
	beq	.L771
	lsl	r3, r3, #10
	mov	r2, #1
	mov	r1, r2
	ldr	r0, .L795+12
	str	r3, [r4, #3460]
	bl	FlashReadPages
	ldr	r3, [r4, #3456]
	cmn	r3, #1
	beq	.L771
	ldrh	r2, [r6]
	movw	r3, #61649
	cmp	r2, r3
	bne	.L771
	ldr	r3, [r6, #4]
	ldr	r2, [r4, #364]
	cmp	r3, r2
	ldrhhi	r2, [r5, #4]
	strhi	r3, [r4, #364]
	ldrhhi	r3, [r6, #8]
	strhhi	r2, [r5]	@ movhi
	strhhi	r3, [r5, #4]	@ movhi
.L771:
	ldr	r8, .L795+12
	mov	r1, #1
	ldrh	r0, [r5]
	bl	FtlGetLastWrittenPage
	sxth	r7, r0
	add	r0, r0, #1
	strh	r0, [r5, #2]	@ movhi
.L773:
	cmp	r7, #0
	bge	.L776
	mov	r2, #251
	ldr	r1, .L795+16
	ldr	r0, .L795+20
	bl	sftl_printk
.L775:
	ldrh	r3, [r6, #10]
	ldrh	r0, [r6, #12]
	strh	r3, [r5, #6]	@ movhi
	movw	r3, #65535
	cmp	r0, r3
	beq	.L778
	ldr	r2, [r4, #232]
	cmp	r0, r2
	beq	.L778
	ldrh	r3, [r4, #246]
	lsr	r3, r3, #2
	cmp	r0, r3
	cmpcc	r2, r3
	bcs	.L778
	bl	FtlSysBlkNumInit
.L778:
	ldr	r6, .L795+24
	mov	r5, #0
	ldr	r8, .L795+28
	sub	r7, r6, #122
.L779:
	ldrh	r3, [r7]
	cmp	r5, r3
	bcc	.L780
	mov	r0, #0
	ldmfd	sp, {r4, r5, r6, r7, r8, fp, sp, pc}
.L766:
	sub	r5, r5, #1
	uxth	r5, r5
	b	.L764
.L776:
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
	beq	.L774
	ldrh	r2, [r6]
	movw	r3, #61649
	cmp	r2, r3
	beq	.L775
.L774:
	sub	r7, r7, #1
	sxth	r7, r7
	b	.L773
.L780:
	ldrh	r2, [r8]
	ldr	r1, [r4, #3464]
	ldr	r0, [r6, #4]!
	lsl	r2, r2, #2
	mla	r1, r5, r2, r1
	add	r5, r5, #1
	bl	ftl_memcpy
	b	.L779
.L781:
	mvn	r0, #0
	ldmfd	sp, {r4, r5, r6, r7, r8, fp, sp, pc}
.L796:
	.align	2
.L795:
	.word	.LANCHOR0
	.word	.LANCHOR0+302
	.word	.LANCHOR0+356
	.word	.LANCHOR0+3456
	.word	.LANCHOR1+245
	.word	.LC8
	.word	.LANCHOR0+380
	.word	.LANCHOR0+3392
	.size	FtlLoadBbt, .-FtlLoadBbt
	.align	2
	.global	FlashProgPages
	.syntax unified
	.arm
	.fpu softvfp
	.type	FlashProgPages, %function
FlashProgPages:
	@ args = 0, pretend = 0, frame = 48
	@ frame_needed = 1, uses_anonymous_args = 0
	mov	ip, sp
	push	{r4, r5, r6, r7, r8, r9, r10, fp, ip, lr, pc}
	sub	fp, ip, #4
	sub	sp, sp, #48
	mov	r8, #0
	ldr	r6, .L836
	mov	r4, r0
	str	r3, [fp, #-88]
	mov	r5, r0
	str	r1, [fp, #-76]
	ldrh	r3, [r6, #12]
	mov	r9, r6
	mov	r10, r6
	str	r2, [fp, #-84]
	str	r3, [fp, #-72]
	lsl	r3, r3, #3
	str	r3, [fp, #-80]
.L798:
	ldr	r3, [fp, #-76]
	cmp	r8, r3
	bne	.L812
	ldr	r3, [fp, #-88]
	cmp	r3, #0
	beq	.L806
	mov	r5, #0
	ldr	r6, .L836+4
	ldr	r7, .L836+8
	b	.L813
.L812:
	ldr	r3, [r5, #8]
	cmp	r3, #0
	beq	.L799
	ldr	r3, [r5, #12]
	cmp	r3, #0
	bne	.L800
.L799:
	mov	r2, #148
	ldr	r1, .L836+12
	ldr	r0, .L836+16
	bl	sftl_printk
.L800:
	sub	r2, fp, #68
	sub	r1, fp, #64
	mov	r0, r5
	bl	l2p_addr_tran
	ldr	r7, [fp, #-68]
	cmp	r7, #3
	bls	.L801
.L835:
	mvn	r3, #0
	str	r3, [r5]
	b	.L802
.L801:
	cmp	r7, #0
	bne	.L803
	ldr	r3, [fp, #-64]
	ldr	r2, [fp, #-80]
	cmp	r2, r3
	bls	.L803
	ldr	r5, .L836+12
	ldr	r6, .L836+20
	b	.L834
.L805:
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
	ldr	r0, .L836+24
	bl	rknand_print_hex
	mov	r3, #4
	ldr	r1, [r4, #-8]
	mov	r2, r3
	ldr	r0, .L836+28
	bl	rknand_print_hex
.L834:
	ldr	r3, [fp, #-76]
	add	r4, r4, #20
	cmp	r7, r3
	bne	.L805
	bl	dump_stack
.L806:
	mov	r0, #0
	sub	sp, fp, #40
	ldmfd	sp, {r4, r5, r6, r7, r8, r9, r10, fp, sp, pc}
.L803:
	ldr	r1, [r5, #8]
	tst	r1, #63
	moveq	r7, r1
	beq	.L807
	ldr	r7, [r10, #3324]
	cmp	r1, r7
	beq	.L807
	ldr	r3, .L836+32
	mov	r0, r7
	ldrh	r2, [r3]
	lsl	r2, r2, #9
	bl	ftl_memcpy
.L807:
	ldr	r3, [r5, #12]
	mov	r2, r7
	ldr	ip, [r10, #3256]
	ldr	r1, [fp, #-64]
	ldrb	r0, [fp, #-68]	@ zero_extendqisi2
	blx	ip
	cmp	r0, #0
	mvnne	r3, #0
	streq	r0, [r5]
	strne	r3, [r5]
	ldrh	r3, [r6, #14]
	cmp	r3, #4
	bne	.L802
	ldr	r0, [fp, #-72]
	add	r2, r7, #2048
	ldr	r3, [r5, #12]
	ldr	r1, [fp, #-64]
	ldr	ip, [r6, #3256]
	add	r3, r3, #8
	add	r1, r0, r1
	ldrb	r0, [fp, #-68]	@ zero_extendqisi2
	blx	ip
	cmp	r0, #0
	bne	.L835
.L802:
	add	r8, r8, #1
	add	r5, r5, #20
	b	.L798
.L818:
	ldr	r2, [r9, #3324]
	mov	r3, #0
	mov	r1, #1
	sub	r0, fp, #60
	str	r3, [r2]
	ldr	r2, [r9, #3332]
	str	r3, [r2]
	ldr	r3, [r4, #4]
	ldr	r2, [fp, #-84]
	str	r3, [fp, #-56]
	ldr	r3, [r9, #3324]
	str	r3, [fp, #-52]
	ldr	r3, [r9, #3332]
	str	r3, [fp, #-48]
	bl	FlashReadPages
	ldr	r10, [fp, #-60]
	cmn	r10, #1
	bne	.L814
	ldr	r1, [r4, #4]
	mov	r0, r6
	bl	sftl_printk
	str	r10, [r4]
.L814:
	ldr	r10, [fp, #-60]
	cmp	r10, #256
	bne	.L815
	ldr	r1, [r4, #4]
	mov	r0, r7
	bl	sftl_printk
	str	r10, [r4]
.L815:
	ldr	r3, [r4, #12]
	cmp	r3, #0
	beq	.L816
	ldr	r2, [r3]
	ldr	r3, .L836
	ldr	r3, [r3, #3332]
	ldr	r3, [r3]
	cmp	r2, r3
	beq	.L816
	ldr	r1, [r4, #4]
	ldr	r0, .L836+36
	bl	sftl_printk
	mvn	r3, #0
	str	r3, [r4]
.L816:
	ldr	r3, [r4, #8]
	cmp	r3, #0
	beq	.L817
	ldr	r2, [r3]
	ldr	r3, .L836
	ldr	r3, [r3, #3324]
	ldr	r3, [r3]
	cmp	r2, r3
	beq	.L817
	ldr	r1, [r4, #4]
	ldr	r0, .L836+40
	bl	sftl_printk
	mvn	r3, #0
	str	r3, [r4]
.L817:
	add	r5, r5, #1
	add	r4, r4, #20
.L813:
	cmp	r8, r5
	bne	.L818
	b	.L806
.L837:
	.align	2
.L836:
	.word	.LANCHOR0
	.word	.LC95
	.word	.LC96
	.word	.LANCHOR1+256
	.word	.LC8
	.word	.LC86
	.word	.LC87
	.word	.LC88
	.word	.LANCHOR0+262
	.word	.LC97
	.word	.LC98
	.size	FlashProgPages, .-FlashProgPages
	.align	2
	.global	FtlLowFormatEraseBlock
	.syntax unified
	.arm
	.fpu softvfp
	.type	FtlLowFormatEraseBlock, %function
FtlLowFormatEraseBlock:
	@ args = 0, pretend = 0, frame = 12
	@ frame_needed = 1, uses_anonymous_args = 0
	mov	ip, sp
	push	{r4, r5, r6, r7, r8, r9, r10, fp, ip, lr, pc}
	sub	fp, ip, #4
	sub	sp, sp, #12
	mov	r10, #0
	ldr	ip, .L880
	uxtb	r5, r1
	uxth	r3, r0
	mov	r6, r10
	mov	r4, r10
	mov	r9, ip
	mov	r8, #20
	str	r3, [fp, #-44]
	str	r3, [ip, #3288]
.L839:
	ldrh	r1, [r9, #236]
	uxth	r3, r10
	cmp	r1, r3
	bhi	.L843
	cmp	r6, #0
	beq	.L838
	mov	r7, #0
	mov	r8, #20
	mov	r2, r6
	mov	r1, #0
	ldr	r0, [r9, #3272]
	bl	FlashEraseBlocks
.L846:
	uxth	r3, r7
	cmp	r6, r3
	bhi	.L848
.L849:
	cmp	r5, #0
	mov	r7, #0
	ldrne	r3, .L880+4
	moveq	r3, #2
	streq	r5, [fp, #-48]
	streq	r3, [fp, #-52]
	ldrhne	r3, [r3]
	strne	r3, [fp, #-52]
	movne	r3, #1
	strne	r3, [fp, #-48]
.L858:
	mov	r10, #0
	mov	r6, r10
.L850:
	ldrh	r1, [r9, #236]
	uxth	r3, r10
	cmp	r1, r3
	bhi	.L853
	cmp	r6, #0
	beq	.L838
	mov	r8, #0
	mov	r10, #20
	mov	r3, #1
	ldr	r2, [fp, #-48]
	mov	r1, r6
	ldr	r0, [r9, #3272]
	bl	FlashProgPages
.L855:
	uxth	r3, r8
	cmp	r6, r3
	bhi	.L857
	add	r7, r7, #1
	ldr	r2, [fp, #-52]
	uxth	r3, r7
	cmp	r2, r3
	bhi	.L858
	mov	r7, #0
	mov	r8, #20
.L859:
	uxth	r3, r7
	cmp	r6, r3
	bhi	.L861
	ldr	r3, [fp, #-44]
	adds	r5, r5, #0
	movne	r5, #1
	cmp	r3, #63
	orrls	r5, r5, #1
	cmp	r5, #0
	beq	.L838
	mov	r2, r6
	ldr	r1, [fp, #-48]
	ldr	r0, [r9, #3272]
	bl	FlashEraseBlocks
.L838:
	mov	r0, r4
	sub	sp, fp, #40
	ldmfd	sp, {r4, r5, r6, r7, r8, r9, r10, fp, sp, pc}
.L843:
	uxth	r3, r10
	ldr	r0, [r9, #3272]
	mov	r2, #0
	mul	r1, r8, r3
	add	r3, r9, r3
	str	r2, [r0, r1]
	ldr	r1, [fp, #-44]
	ldrb	r0, [r3, #264]	@ zero_extendqisi2
	bl	V2P_block
	cmp	r5, #0
	mov	r7, r0
	beq	.L840
	bl	IsBlkInVendorPart
	cmp	r0, #0
	bne	.L841
.L840:
	mov	r0, r7
	bl	FtlBbmIsBadBlock
	cmp	r0, #0
	addne	r4, r4, #1
	uxthne	r4, r4
	bne	.L841
	ldr	r1, [r9, #3272]
	lsl	r7, r7, #10
	ldr	r3, [r9, #3320]
	mla	r1, r8, r6, r1
	str	r3, [r1, #8]
	ldr	r3, .L880+8
	str	r7, [r1, #4]
	ldrh	r3, [r3]
	mul	r3, r6, r3
	add	r6, r6, #1
	uxth	r6, r6
	add	r0, r3, #3
	cmp	r3, #0
	movlt	r3, r0
	ldr	r0, [r9, #3340]
	bic	r3, r3, #3
	add	r3, r0, r3
	str	r3, [r1, #12]
.L841:
	add	r10, r10, #1
	b	.L839
.L848:
	mul	r3, r8, r7
	ldr	r2, [r9, #3272]
	add	r1, r2, r3
	ldr	r3, [r2, r3]
	cmn	r3, #1
	bne	.L847
	ldr	r0, [r1, #4]
	add	r4, r4, #1
	uxth	r4, r4
	ubfx	r0, r0, #10, #16
	bl	FtlBbmMapBadBlock
.L847:
	add	r7, r7, #1
	b	.L846
.L853:
	uxth	r3, r10
	mov	r2, #20
	ldr	r0, [r9, #3272]
	mul	r1, r2, r3
	mov	r2, #0
	add	r3, r9, r3
	str	r2, [r0, r1]
	ldr	r1, [fp, #-44]
	ldrb	r0, [r3, #264]	@ zero_extendqisi2
	bl	V2P_block
	cmp	r5, #0
	mov	r8, r0
	beq	.L851
	bl	IsBlkInVendorPart
	cmp	r0, #0
	bne	.L852
.L851:
	mov	r0, r8
	bl	FtlBbmIsBadBlock
	cmp	r0, #0
	bne	.L852
	ldr	r1, [r9, #3272]
	mov	r3, #20
	add	r8, r7, r8, lsl #10
	mla	r1, r3, r6, r1
	ldr	r3, [r9, #3316]
	str	r3, [r1, #8]
	ldr	r3, .L880+8
	str	r8, [r1, #4]
	ldrh	r3, [r3]
	mul	r3, r6, r3
	add	r6, r6, #1
	uxth	r6, r6
	add	r0, r3, #3
	cmp	r3, #0
	movlt	r3, r0
	ldr	r0, [r9, #3320]
	bic	r3, r3, #3
	add	r3, r0, r3
	str	r3, [r1, #12]
.L852:
	add	r10, r10, #1
	b	.L850
.L857:
	mul	r3, r10, r8
	ldr	r2, [r9, #3272]
	add	r1, r2, r3
	ldr	r3, [r2, r3]
	cmp	r3, #0
	beq	.L856
	ldr	r0, [r1, #4]
	add	r4, r4, #1
	uxth	r4, r4
	ubfx	r0, r0, #10, #16
	bl	FtlBbmMapBadBlock
.L856:
	add	r8, r8, #1
	b	.L855
.L861:
	cmp	r5, #0
	beq	.L860
	mul	r3, r8, r7
	ldr	r2, [r9, #3272]
	add	r1, r2, r3
	ldr	r3, [r2, r3]
	cmp	r3, #0
	bne	.L860
	ldr	r0, [r1, #4]
	mov	r1, #1
	ubfx	r0, r0, #10, #16
	bl	FtlFreeSysBlkQueueIn
.L860:
	add	r7, r7, #1
	b	.L859
.L881:
	.align	2
.L880:
	.word	.LANCHOR0
	.word	.LANCHOR0+308
	.word	.LANCHOR0+316
	.size	FtlLowFormatEraseBlock, .-FtlLowFormatEraseBlock
	.align	2
	.global	FlashTestBlk
	.syntax unified
	.arm
	.fpu softvfp
	.type	FlashTestBlk, %function
FlashTestBlk:
	@ args = 0, pretend = 0, frame = 84
	@ frame_needed = 1, uses_anonymous_args = 0
	mov	ip, sp
	push	{r4, r5, fp, ip, lr, pc}
	sub	fp, ip, #4
	sub	sp, sp, #84
	uxth	r5, r0
	cmp	r5, #11
	movls	r4, #0
	bls	.L882
	ldr	r3, .L887
	sub	r0, fp, #84
	mov	r2, #32
	mov	r1, #165
	str	r0, [fp, #-92]
	lsl	r5, r5, #10
	ldr	r4, [r3, #3328]
	str	r4, [fp, #-96]
	bl	memset
	ldr	r3, .L887+4
	mov	r2, #1
	mov	r1, r2
	sub	r0, fp, #104
	str	r3, [r4]	@ unaligned
	str	r3, [r4, #4]	@ unaligned
	str	r5, [fp, #-100]
	bl	FlashEraseBlocks
	mov	r3, #1
	sub	r0, fp, #104
	mov	r2, r3
	mov	r1, r3
	bl	FlashProgPages
	ldr	r3, [fp, #-104]
	cmp	r3, #0
	mvnne	r4, #0
	bne	.L884
	add	r3, r5, #1
	sub	r0, fp, #104
	str	r3, [fp, #-100]
	mov	r3, #1
	mov	r2, r3
	mov	r1, r3
	bl	FlashProgPages
	ldr	r4, [fp, #-104]
	adds	r4, r4, #0
	movne	r4, #1
	rsb	r4, r4, #0
.L884:
	mov	r2, #1
	mov	r1, #0
	sub	r0, fp, #104
	str	r5, [fp, #-100]
	bl	FlashEraseBlocks
.L882:
	mov	r0, r4
	sub	sp, fp, #20
	ldmfd	sp, {r4, r5, fp, sp, pc}
.L888:
	.align	2
.L887:
	.word	.LANCHOR0
	.word	1515870810
	.size	FlashTestBlk, .-FlashTestBlk
	.align	2
	.global	FtlBbmTblFlush
	.syntax unified
	.arm
	.fpu softvfp
	.type	FtlBbmTblFlush, %function
FtlBbmTblFlush:
	@ args = 0, pretend = 0, frame = 4
	@ frame_needed = 1, uses_anonymous_args = 0
	mov	ip, sp
	push	{r4, r5, r6, r7, r8, r9, r10, fp, ip, lr, pc}
	sub	fp, ip, #4
	sub	sp, sp, #8
	mov	r5, #0
	ldr	r4, .L901
	mov	r1, #0
	ldr	r3, [r4, #3336]
	add	r6, r4, #380
	ldr	r0, [r4, #3304]
	sub	r7, r6, #122
	add	r8, r4, #3392
	str	r3, [r4, #3468]
	movw	r3, #314
	ldrh	r2, [r4, r3]
	str	r0, [r4, #3464]
	bl	ftl_memset
.L890:
	ldrh	r3, [r7]
	cmp	r5, r3
	blt	.L891
	ldr	r6, [r4, #3468]
	mov	r2, #16
	mov	r1, #255
	ldr	r5, .L901+4
	ldr	r10, .L901+8
	mov	r7, #0
	mov	r0, r6
	mov	r8, r7
	bl	memset
	ldr	r3, .L901+12
	sub	r9, r5, #48
	strh	r3, [r6]	@ movhi
	ldr	r3, [r4, #364]
	str	r3, [r6, #4]
	ldrh	r3, [r5]
	strh	r3, [r6, #2]	@ movhi
	ldrh	r3, [r5, #4]
	strh	r3, [r6, #8]	@ movhi
	ldrh	r3, [r5, #6]
	strh	r3, [r6, #10]	@ movhi
	ldr	r3, [r4, #232]
	strh	r3, [r6, #12]	@ movhi
	ldr	r3, [r4, #2608]
	strh	r3, [r6, #14]	@ movhi
.L892:
	ldr	r3, [r4, #3304]
	mov	ip, #0
	ldrh	r2, [r5, #2]
	ldrh	r1, [r5]
	str	r3, [r4, #3464]
	ldr	r3, [r4, #3336]
	str	ip, [r4, #3456]
	str	ip, [fp, #-44]
	str	r3, [r4, #3468]
	orr	r3, r2, r1, lsl #10
	ldrh	r0, [r6, #10]
	str	r3, [r4, #3460]
	ldrh	r3, [r5, #4]
	str	r0, [sp]
	mov	r0, r10
	bl	sftl_printk
	ldrh	r3, [r9]
	ldrh	r2, [r5, #2]
	ldr	ip, [fp, #-44]
	sub	r3, r3, #1
	cmp	r2, r3
	blt	.L893
	ldr	r3, [r4, #364]
	ldrh	r2, [r5]
	ldr	r0, [r4, #3272]
	add	r3, r3, #1
	strh	ip, [r5, #2]	@ movhi
	str	r3, [r4, #364]
	str	r3, [r6, #4]
	ldrh	r3, [r5, #4]
	strh	r2, [r6, #8]	@ movhi
	strh	r2, [r5, #4]	@ movhi
	mov	r2, #1
	strh	r3, [r5]	@ movhi
	mov	r1, r2
	lsl	r3, r3, #10
	str	r3, [r4, #3460]
	str	r3, [r0, #4]
	bl	FlashEraseBlocks
.L893:
	mov	r3, #1
	ldr	r0, .L901+16
	mov	r2, r3
	mov	r1, r3
	bl	FlashProgPages
	ldrh	r3, [r5, #2]
	add	r3, r3, #1
	strh	r3, [r5, #2]	@ movhi
	ldr	r3, [r4, #3456]
	cmn	r3, #1
	bne	.L894
	add	r7, r7, #1
	ldr	r1, [r4, #3460]
	uxth	r7, r7
	ldr	r0, .L901+20
	bl	sftl_printk
	cmp	r7, #3
	bls	.L892
	mov	r2, r7
	ldr	r1, [r4, #3460]
	ldr	r0, .L901+24
	bl	sftl_printk
.L896:
	b	.L896
.L891:
	ldrh	r2, [r8]
	ldr	r3, [r4, #3464]
	ldr	r1, [r6, #4]!
	mul	r0, r2, r5
	lsl	r2, r2, #2
	add	r5, r5, #1
	add	r0, r3, r0, lsl #2
	bl	ftl_memcpy
	b	.L890
.L897:
	mov	r8, #1
	b	.L892
.L894:
	add	r8, r8, #1
	cmp	r8, #1
	ble	.L897
	cmp	r3, #256
	beq	.L892
	mov	r0, #0
	sub	sp, fp, #40
	ldmfd	sp, {r4, r5, r6, r7, r8, r9, r10, fp, sp, pc}
.L902:
	.align	2
.L901:
	.word	.LANCHOR0
	.word	.LANCHOR0+356
	.word	.LC99
	.word	-3887
	.word	.LANCHOR0+3456
	.word	.LC100
	.word	.LC101
	.size	FtlBbmTblFlush, .-FtlBbmTblFlush
	.align	2
	.global	allocate_data_superblock
	.syntax unified
	.arm
	.fpu softvfp
	.type	allocate_data_superblock, %function
allocate_data_superblock:
	@ args = 0, pretend = 0, frame = 12
	@ frame_needed = 1, uses_anonymous_args = 0
	mov	ip, sp
	push	{r4, r5, r6, r7, r8, r9, r10, fp, ip, lr, pc}
	sub	fp, ip, #4
	sub	sp, sp, #12
	mov	r4, r0
	ldr	r9, .L939
.L904:
	ldr	r3, .L939
	ldr	r10, .L939+4
	ldrh	r2, [r3, #228]
	ldrh	r1, [r10]
	add	r2, r2, r1
	ldrh	r1, [r3, #244]
	cmp	r2, r1
	ble	.L905
	mov	r2, #2656
	ldr	r1, .L939+8
	ldr	r0, .L939+12
	bl	sftl_printk
.L905:
	ldr	r2, .L939+16
	cmp	r4, r2
	movne	r1, #0
	bne	.L906
	ldrh	r2, [r9, #228]
	ldr	r1, [r9, #3168]
	mul	r0, r1, r2
	lsr	r1, r2, #1
	add	r1, r1, #1
	add	r1, r1, r0, lsr #2
	uxth	r1, r1
	cmp	r1, #0
	subne	r1, r1, #1
	uxthne	r1, r1
.L906:
	ldr	r0, .L939+20
	bl	List_pop_index_node
	ldrh	r2, [r9, #228]
	mov	r7, r0
	uxth	r8, r0
	cmp	r2, #0
	bne	.L907
	movw	r2, #2665
	ldr	r1, .L939+8
	ldr	r0, .L939+12
	bl	sftl_printk
.L907:
	ldrh	r2, [r9, #228]
	sub	r2, r2, #1
	strh	r2, [r9, #228]	@ movhi
	ldrh	r2, [r9, #244]
	cmp	r2, r8
	bls	.L904
	uxth	r7, r7
	ldr	r2, [r9, #76]
	lsl	r6, r7, #1
	ldrh	r5, [r2, r6]
	cmp	r5, #0
	bne	.L904
	strh	r8, [r4]	@ movhi
	mov	r0, r4
	bl	make_superblock
	ldrb	r2, [r4, #7]	@ zero_extendqisi2
	cmp	r2, #0
	bne	.L909
	ldr	r2, [r9, #76]
	mvn	r1, #0
	mov	r0, r7
	strh	r1, [r2, r6]	@ movhi
	bl	INSERT_DATA_LIST
	ldrh	r1, [r10]
	ldrh	r2, [r9, #228]
	add	r2, r2, r1
	ldrh	r1, [r9, #244]
	cmp	r2, r1
	ble	.L904
	movw	r2, #2679
	ldr	r1, .L939+8
	ldr	r0, .L939+12
	bl	sftl_printk
	b	.L904
.L909:
	ldrh	r1, [r10]
	ldrh	r2, [r9, #228]
	add	r2, r2, r1
	ldrh	r1, [r9, #244]
	cmp	r2, r1
	ble	.L911
	movw	r2, #2682
	ldr	r1, .L939+8
	ldr	r0, .L939+12
	bl	sftl_printk
.L911:
	ldr	r0, [r9, #3272]
	mov	r1, #20
	ldrh	lr, [r9, #236]
	add	ip, r4, #16
	str	r1, [fp, #-44]
	mov	r2, r0
	mla	r3, r1, lr, r0
	mov	lr, #0
.L912:
	cmp	r3, r2
	bne	.L914
	cmp	r5, #0
	bne	.L915
	movw	r2, #2693
	ldr	r1, .L939+8
	ldr	r0, .L939+12
	bl	sftl_printk
.L915:
	ldrh	r2, [r9, #176]
	cmp	r2, r8
	bne	.L916
	movw	r2, #2695
	ldr	r1, .L939+8
	ldr	r0, .L939+12
	bl	sftl_printk
.L916:
	ldrb	r2, [r4, #8]	@ zero_extendqisi2
	ldr	r1, [r9, #2540]
	ldr	r10, .L939+24
	cmp	r2, #0
	ldrh	r2, [r1, r6]
	bne	.L917
	cmp	r2, #0
	ldrhne	r0, [r10]
	moveq	r2, #2
	addne	r2, r2, r0
	mov	r0, r7
	strh	r2, [r1, r6]	@ movhi
	mov	r1, #0
	ldr	r2, [r9, #2600]
	add	r2, r2, #1
	str	r2, [r9, #2600]
	bl	ftl_set_blk_mode
.L920:
	ldr	r2, [r9, #2540]
	ldr	r1, [r9, #2616]
	ldr	ip, [r9, #2600]
	ldrh	r2, [r2, r6]
	ldrh	r0, [r10]
	cmp	r2, r1
	ldrh	r1, [r9, #244]
	strhi	r2, [r9, #2616]
	ldr	r2, [r9, #2604]
	mla	r0, ip, r0, r2
	bl	__udivsi3
	ldr	r1, [r9, #3352]
	str	r0, [r9, #2608]
	ldr	r0, [r9, #3272]
	ldr	r2, [r1, #16]
	ldr	ip, .L939+28
	add	r2, r2, #1
	str	r2, [r1, #16]
	mov	r1, #20
	mla	r1, r1, r5, r0
	add	r2, r0, #4
	add	r1, r1, #24
.L922:
	add	r2, r2, #20
	cmp	r1, r2
	bne	.L923
	ldrb	r1, [r4, #8]	@ zero_extendqisi2
	mov	r2, r5
	ldr	r0, [r9, #3272]
	mov	r10, #0
	bl	FlashEraseBlocks
	mov	r1, r10
	mov	ip, #20
.L924:
	uxth	r2, r10
	cmp	r5, r2
	bhi	.L926
	cmp	r1, #0
	ble	.L927
	mov	r0, r7
	bl	update_multiplier_value
	bl	FtlBbmTblFlush
.L927:
	ldrb	r1, [r4, #7]	@ zero_extendqisi2
	cmp	r1, #0
	bne	.L928
	ldr	r2, [r9, #76]
	mvn	r1, #0
	strh	r1, [r2, r6]	@ movhi
	b	.L904
.L914:
	str	lr, [r2, #8]
	movw	r10, #65535
	str	lr, [r2, #12]
	add	r2, r2, #20
	ldrh	r1, [ip], #2
	cmp	r1, r10
	ldrne	r10, [fp, #-44]
	lslne	r1, r1, #10
	mlane	r10, r10, r5, r0
	addne	r5, r5, #1
	uxthne	r5, r5
	strne	r1, [r10, #4]
	b	.L912
.L917:
	add	r2, r2, #1
	mov	r0, r7
	strh	r2, [r1, r6]	@ movhi
	ldr	r2, [r9, #2604]
	add	r2, r2, #1
	str	r2, [r9, #2604]
	bl	ftl_set_blk_mode.part.6
	b	.L920
.L923:
	ldr	r0, [r2, #-20]
	and	r0, r0, ip
	str	r0, [r2, #-20]
	b	.L922
.L926:
	mul	r2, ip, r10
	ldr	lr, [r9, #3272]
	add	r0, lr, r2
	ldr	r2, [lr, r2]
	cmn	r2, #1
	bne	.L925
	ldr	r0, [r0, #4]
	add	r1, r1, #1
	str	ip, [fp, #-52]
	str	r2, [fp, #-48]
	ubfx	r0, r0, #10, #16
	str	r1, [fp, #-44]
	bl	FtlBbmMapBadBlock
	ldr	r2, [fp, #-48]
	add	r0, r4, r10, lsl #1
	ldr	ip, [fp, #-52]
	ldr	r1, [fp, #-44]
	strh	r2, [r0, #16]	@ movhi
	ldrb	r2, [r4, #7]	@ zero_extendqisi2
	sub	r2, r2, #1
	strb	r2, [r4, #7]
.L925:
	add	r10, r10, #1
	b	.L924
.L928:
	movw	r2, #306
	ldrh	r2, [r9, r2]
	strh	r8, [r4]	@ movhi
	smulbb	r2, r2, r1
	mov	r1, #0
	strh	r1, [r4, #2]	@ movhi
	strb	r1, [r4, #6]
	ldr	r1, [r9, #2592]
	uxth	r2, r2
	ldr	r0, [r9, #76]
	strh	r2, [r4, #4]	@ movhi
	str	r1, [r4, #12]
	add	r1, r1, #1
	str	r1, [r9, #2592]
	ldrh	r1, [r4]
	lsl	r3, r1, #1
	strh	r2, [r0, r3]	@ movhi
	ldrh	r3, [r4, #4]
	cmp	r3, #0
	beq	.L929
	ldrb	r3, [r4, #7]	@ zero_extendqisi2
	cmp	r3, #0
	bne	.L930
.L929:
	movw	r2, #2748
	ldr	r1, .L939+8
	ldr	r0, .L939+12
	bl	sftl_printk
.L930:
	mov	r0, #0
	sub	sp, fp, #40
	ldmfd	sp, {r4, r5, r6, r7, r8, r9, r10, fp, sp, pc}
.L940:
	.align	2
.L939:
	.word	.LANCHOR0
	.word	.LANCHOR0+2532
	.word	.LANCHOR1+271
	.word	.LC8
	.word	.LANCHOR0+128
	.word	.LANCHOR0+2536
	.word	.LANCHOR0+296
	.word	-1024
	.size	allocate_data_superblock, .-allocate_data_superblock
	.align	2
	.global	FtlGcFreeBadSuperBlk
	.syntax unified
	.arm
	.fpu softvfp
	.type	FtlGcFreeBadSuperBlk, %function
FtlGcFreeBadSuperBlk:
	@ args = 0, pretend = 0, frame = 8
	@ frame_needed = 1, uses_anonymous_args = 0
	mov	ip, sp
	push	{r4, r5, r6, r7, r8, r9, r10, fp, ip, lr, pc}
	sub	fp, ip, #4
	sub	sp, sp, #8
	uxth	r3, r0
	ldr	r6, .L953
	str	r3, [fp, #-44]
	movw	r3, #3210
	ldrh	r3, [r6, r3]
	cmp	r3, #0
	movne	r7, #0
	ldrne	r10, .L953+4
	bne	.L943
.L942:
	mov	r0, #0
	sub	sp, fp, #40
	ldmfd	sp, {r4, r5, r6, r7, r8, r9, r10, fp, sp, pc}
.L949:
	uxtah	r3, r6, r7
	ldr	r1, [fp, #-44]
	mov	r8, #0
	ldrb	r0, [r3, #264]	@ zero_extendqisi2
	bl	V2P_block
	mov	r9, r0
.L944:
	ldrh	r3, [r10]
	uxth	r4, r8
	cmp	r3, r4
	bhi	.L948
	add	r7, r7, #1
.L943:
	ldrh	r2, [r6, #236]
	uxth	r3, r7
	cmp	r2, r3
	bhi	.L949
	bl	FtlGcReFreshBadBlk
	b	.L942
.L948:
	uxth	r3, r8
	ldr	r5, .L953+8
	lsl	r2, r3, #1
	ldrh	r2, [r5, r2]
	cmp	r2, r9
	bne	.L945
	mov	r1, r9
	ldr	r0, .L953+12
	str	r3, [fp, #-48]
	bl	sftl_printk
	mov	r0, r9
	bl	FtlBbmMapBadBlock
	bl	FtlBbmTblFlush
	ldr	r3, [fp, #-48]
	ldrh	r2, [r10]
	add	r3, r5, r3, lsl #1
.L946:
	cmp	r4, r2
	bcc	.L947
	sub	r2, r2, #1
	strh	r2, [r10]	@ movhi
.L945:
	add	r8, r8, #1
	b	.L944
.L947:
	ldrh	r1, [r3, #2]!
	add	r4, r4, #1
	uxth	r4, r4
	strh	r1, [r3, #-2]	@ movhi
	b	.L946
.L954:
	.align	2
.L953:
	.word	.LANCHOR0
	.word	.LANCHOR0+3210
	.word	.LANCHOR0+3212
	.word	.LC102
	.size	FtlGcFreeBadSuperBlk, .-FtlGcFreeBadSuperBlk
	.align	2
	.global	update_vpc_list
	.syntax unified
	.arm
	.fpu softvfp
	.type	update_vpc_list, %function
update_vpc_list:
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 1, uses_anonymous_args = 0
	mov	ip, sp
	push	{r4, r5, r6, fp, ip, lr, pc}
	sub	fp, ip, #4
	ldr	r4, .L967
	uxth	r5, r0
	ldr	r2, [r4, #76]
	lsl	r3, r5, #1
	ldrh	r3, [r2, r3]
	cmp	r3, #0
	bne	.L956
	ldrh	r3, [r4, #226]
	cmp	r3, r5
	mvneq	r3, #0
	strheq	r3, [r4, #226]	@ movhi
	ldrh	r3, [r4, #224]
	cmp	r3, r5
	mvneq	r3, #0
	strheq	r3, [r4, #224]	@ movhi
	ldrh	r3, [r4, #176]
	cmp	r3, r5
	mvneq	r3, #0
	strheq	r3, [r4, #176]	@ movhi
	beq	.L960
	ldrh	r3, [r4, #28]
	cmp	r3, r5
	beq	.L965
	ldrh	r3, [r4, #80]
	cmp	r3, r5
	beq	.L965
	ldrh	r3, [r4, #128]
	cmp	r3, r5
	beq	.L965
.L960:
	mov	r1, r5
	ldr	r0, .L967+4
	bl	List_remove_node
	movw	r3, #2532
	ldrh	r3, [r4, r3]
	cmp	r3, #0
	bne	.L962
	movw	r2, #2824
	ldr	r1, .L967+8
	ldr	r0, .L967+12
	bl	sftl_printk
.L962:
	movw	r6, #2532
	mov	r0, r5
	ldrh	r3, [r4, r6]
	sub	r3, r3, #1
	strh	r3, [r4, r6]	@ movhi
	bl	free_data_superblock
	mov	r0, r5
	bl	FtlGcFreeBadSuperBlk
	ldrh	r2, [r4, r6]
	ldrh	r3, [r4, #228]
	add	r3, r3, r2
	ldrh	r2, [r4, #244]
	cmp	r3, r2
	ble	.L966
	movw	r2, #2827
	ldr	r1, .L967+8
	ldr	r0, .L967+12
	bl	sftl_printk
.L966:
	mov	r0, #1
	ldmfd	sp, {r4, r5, r6, fp, sp, pc}
.L956:
	mov	r0, r5
	bl	List_update_data_list
.L965:
	mov	r0, #0
	ldmfd	sp, {r4, r5, r6, fp, sp, pc}
.L968:
	.align	2
.L967:
	.word	.LANCHOR0
	.word	.LANCHOR0+2524
	.word	.LANCHOR1+296
	.word	.LC8
	.size	update_vpc_list, .-update_vpc_list
	.align	2
	.global	decrement_vpc_count
	.syntax unified
	.arm
	.fpu softvfp
	.type	decrement_vpc_count, %function
decrement_vpc_count:
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 1, uses_anonymous_args = 0
	mov	ip, sp
	push	{r4, r5, r6, r7, fp, ip, lr, pc}
	sub	fp, ip, #4
	uxth	r6, r0
	movw	r3, #65535
	cmp	r6, r3
	ldr	r4, .L981
	beq	.L970
	ldr	r3, [r4, #76]
	lsl	r5, r6, #1
	ldrh	r2, [r3, r5]
	cmp	r2, #0
	subne	r2, r2, #1
	strhne	r2, [r3, r5]	@ movhi
	bne	.L970
	mov	r1, r6
	ldr	r0, .L981+4
	bl	sftl_printk
	ldr	r3, [r4, #76]
	ldrh	r5, [r3, r5]
	cmp	r5, #0
	beq	.L972
.L979:
	mov	r5, #0
.L969:
	mov	r0, r5
	ldmfd	sp, {r4, r5, r6, r7, fp, sp, pc}
.L972:
	movw	r2, #2842
.L980:
	ldr	r1, .L981+8
	ldr	r0, .L981+12
	bl	sftl_printk
	b	.L969
.L970:
	movw	r7, #3442
	movw	r3, #65535
	ldrh	r0, [r4, r7]
	cmp	r0, r3
	strheq	r6, [r4, r7]	@ movhi
	beq	.L979
.L974:
	cmp	r6, r0
	beq	.L979
	bl	update_vpc_list
	ldr	r2, [r4, #2520]
	adds	r5, r0, #0
	ldr	r3, [r4, #2524]
	movne	r5, #1
	ldr	r1, [r4, #76]
	strh	r6, [r4, r7]	@ movhi
	sub	r3, r3, r2
	asr	r3, r3, #3
	uxth	r2, r3
	uxth	r3, r3
	lsl	r2, r2, #1
	cmp	r3, r6
	ldrh	r2, [r1, r2]
	clz	r2, r2
	lsr	r2, r2, #5
	moveq	r2, #0
	cmp	r2, #0
	beq	.L969
	movw	r2, #2858
	b	.L980
.L982:
	.align	2
.L981:
	.word	.LANCHOR0
	.word	.LC103
	.word	.LANCHOR1+312
	.word	.LC8
	.size	decrement_vpc_count, .-decrement_vpc_count
	.align	2
	.global	FtlSuperblockPowerLostFix
	.syntax unified
	.arm
	.fpu softvfp
	.type	FtlSuperblockPowerLostFix, %function
FtlSuperblockPowerLostFix:
	@ args = 0, pretend = 0, frame = 20
	@ frame_needed = 1, uses_anonymous_args = 0
	mov	ip, sp
	push	{r4, r5, r6, r7, r8, fp, ip, lr, pc}
	sub	fp, ip, #4
	sub	sp, sp, #20
	mvn	r3, #0
	str	r3, [fp, #-36]
	mov	r6, #0
	ldr	r3, .L996
	movw	r1, #61589
	mov	r4, r0
	ldr	r2, [r3, #3304]
	mov	r8, r3
	ldr	r5, [r3, #3336]
	str	r2, [fp, #-44]
	mvn	r2, #2
	str	r5, [fp, #-40]
	str	r2, [r5, #8]
	mvn	r2, #1
	str	r2, [r5, #12]
	ldrh	r2, [r0]
	strh	r6, [r5]	@ movhi
	strh	r2, [r5, #2]	@ movhi
	ldr	r2, [r3, #3304]
	str	r1, [r2]
	ldr	r1, .L996+4
	ldr	r2, [r3, #3304]
	str	r1, [r2, #4]
	ldrh	r2, [r0, #4]
	tst	r2, #1
	moveq	r7, #6
	movne	r7, #7
.L989:
	ldrh	r3, [r4, #4]
	cmp	r3, #0
	bne	.L985
.L986:
	ldrh	r3, [r4]
	ldr	r1, [r8, #76]
	ldrh	r0, [r4, #4]
	lsl	r3, r3, #1
	ldrh	r2, [r1, r3]
	sub	r2, r2, r0
	strh	r2, [r1, r3]	@ movhi
	movw	r3, #306
	ldrh	r3, [r8, r3]
	strh	r3, [r4, #2]	@ movhi
	mov	r3, #0
	strb	r3, [r4, #6]
	strh	r3, [r4, #4]	@ movhi
	sub	sp, fp, #32
	ldmfd	sp, {r4, r5, r6, r7, r8, fp, sp, pc}
.L985:
	mov	r0, r4
	bl	get_new_active_ppa
	cmn	r0, #1
	str	r0, [fp, #-48]
	beq	.L986
	ldr	r3, [r8, #2596]
	mov	r1, #1
	sub	r0, fp, #52
	str	r3, [r5, #4]
	add	r3, r3, #1
	cmn	r3, #1
	moveq	r3, r6
	str	r3, [r8, #2596]
	mov	r3, #0
	mov	r2, r3
	bl	FlashProgPages
	ldrh	r0, [r4]
	bl	decrement_vpc_count
	subs	r7, r7, #1
	bne	.L989
	b	.L986
.L997:
	.align	2
.L996:
	.word	.LANCHOR0
	.word	305419896
	.size	FtlSuperblockPowerLostFix, .-FtlSuperblockPowerLostFix
	.align	2
	.global	FtlMakeBbt
	.syntax unified
	.arm
	.fpu softvfp
	.type	FtlMakeBbt, %function
FtlMakeBbt:
	@ args = 0, pretend = 0, frame = 8
	@ frame_needed = 1, uses_anonymous_args = 0
	mov	ip, sp
	push	{r4, r5, r6, r7, r8, r9, r10, fp, ip, lr, pc}
	sub	fp, ip, #4
	sub	sp, sp, #8
	mov	r8, #0
	ldr	r6, .L1018
	bl	FtlBbtMemInit
	bl	FtlLoadFactoryBbt
	sub	r7, r6, #384
	sub	r9, r6, #18
	sub	r4, r6, #28
	mov	r5, r7
.L999:
	ldr	r3, .L1018+4
	ldrh	r2, [r3]
	cmp	r8, r2
	bcc	.L1005
	mov	r0, #0
	add	r6, r3, #60
.L1006:
	ldrh	r2, [r6]
	uxth	r3, r0
	add	r7, r0, #1
	cmp	r2, r3
	bhi	.L1007
	ldrh	r6, [r4, #12]
	movw	r7, #65535
	sub	r6, r6, #1
	uxth	r6, r6
.L1008:
	ldrh	r3, [r4, #12]
	sub	r3, r3, #48
	cmp	r6, r3
	ble	.L1012
	mov	r0, r6
	bl	FtlBbmIsBadBlock
	cmp	r0, #1
	beq	.L1009
	mov	r0, r6
	bl	FlashTestBlk
	cmp	r0, #0
	beq	.L1010
	mov	r0, r6
	bl	FtlBbmMapBadBlock
.L1009:
	sub	r6, r6, #1
	uxth	r6, r6
	b	.L1008
.L1005:
	ldr	r2, [r7, #3336]
	movw	r1, #65535
	ldr	r0, [r7, #3304]
	str	r2, [fp, #-48]
	str	r2, [r7, #3468]
	ldrh	r2, [r9, #2]!
	str	r0, [r7, #3464]
	cmp	r2, r1
	beq	.L1000
	ldrh	r10, [r3, #44]
	ldr	r0, .L1018+8
	str	r3, [fp, #-44]
	mla	r10, r8, r10, r2
	lsl	r2, r10, #10
	str	r2, [r7, #3460]
	mov	r2, #1
	mov	r1, r2
	bl	FlashReadPages
	ldr	r3, [fp, #-44]
	ldr	r1, [r7, #3464]
	ldr	r0, [r6]
	ldrh	r2, [r3, #44]
	add	r2, r2, #7
	asr	r2, r2, #3
	bl	ftl_memcpy
.L1001:
	uxth	r0, r10
	add	r8, r8, #1
	add	r6, r6, #4
	bl	FtlBbmMapBadBlock
	b	.L999
.L1000:
	mov	r1, r8
	str	r3, [fp, #-44]
	bl	FlashGetBadBlockList
	ldr	r1, [r6]
	ldr	r0, [r7, #3464]
	bl	FtlBbt2Bitmap
	ldr	r3, [fp, #-44]
	ldrh	r3, [r3, #44]
.L1017:
	sub	r3, r3, #1
	uxth	r3, r3
	str	r3, [fp, #-44]
.L1002:
	ldr	r10, .L1018+12
	ldr	r3, [fp, #-44]
	ldrh	r0, [r10]
	smlabb	r0, r0, r8, r3
	uxth	r0, r0
	bl	FtlBbmIsBadBlock
	cmp	r0, #1
	beq	.L1003
	ldrh	r3, [fp, #-44]
	mov	r1, #16
	ldr	r0, [r5, #3336]
	strh	r3, [r9]	@ movhi
	bl	__memzero
	mov	r1, #4096
	ldr	r0, [r5, #3304]
	bl	__memzero
	ldr	r2, [fp, #-48]
	ldr	r3, .L1018+16
	strh	r3, [r2]	@ movhi
	mov	r3, #0
	str	r3, [r2, #4]
	ldrh	r3, [r9]
	ldrh	r10, [r10]
	strh	r3, [r2, #2]	@ movhi
	ldrh	r3, [r9]
	ldr	r1, [r6]
	ldr	r0, [r5, #3464]
	mla	r10, r8, r10, r3
	lsl	r3, r10, #10
	str	r3, [r5, #3460]
	ldr	r3, .L1018+20
	ldrh	r2, [r3]
	lsl	r2, r2, #2
	bl	ftl_memcpy
	mov	r2, #1
	ldr	r0, .L1018+8
	mov	r1, r2
	bl	FlashEraseBlocks
	mov	r3, #1
	ldr	r0, .L1018+8
	mov	r2, r3
	mov	r1, r3
	bl	FlashProgPages
	ldr	r3, [r5, #3456]
	cmn	r3, #1
	bne	.L1001
	uxth	r0, r10
	bl	FtlBbmMapBadBlock
	b	.L1002
.L1003:
	ldr	r3, [fp, #-44]
	b	.L1017
.L1007:
	uxth	r0, r0
	bl	FtlBbmMapBadBlock
	mov	r0, r7
	b	.L1006
.L1010:
	ldrh	r3, [r4]
	cmp	r3, r7
	strheq	r6, [r4]	@ movhi
	beq	.L1009
.L1011:
	strh	r6, [r4, #4]	@ movhi
.L1012:
	ldrh	r3, [r4]
	mov	r6, #0
	ldr	r0, [r5, #3272]
	mov	r1, #1
	str	r6, [r5, #364]
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
	ldr	r3, [r5, #364]
	ldrh	r2, [r4, #4]
	strh	r6, [r4, #2]	@ movhi
	add	r3, r3, #1
	str	r3, [r5, #364]
	ldrh	r3, [r4]
	strh	r2, [r4]	@ movhi
	strh	r3, [r4, #4]	@ movhi
	bl	FtlBbmTblFlush
	mov	r0, r6
	sub	sp, fp, #40
	ldmfd	sp, {r4, r5, r6, r7, r8, r9, r10, fp, sp, pc}
.L1019:
	.align	2
.L1018:
	.word	.LANCHOR0+384
	.word	.LANCHOR0+258
	.word	.LANCHOR0+3456
	.word	.LANCHOR0+302
	.word	-3872
	.word	.LANCHOR0+3392
	.size	FtlMakeBbt, .-FtlMakeBbt
	.align	2
	.global	ftl_memcmp
	.syntax unified
	.arm
	.fpu softvfp
	.type	ftl_memcmp, %function
ftl_memcmp:
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 1, uses_anonymous_args = 0
	mov	ip, sp
	push	{fp, ip, lr, pc}
	sub	fp, ip, #4
	bl	memcmp
	ldmfd	sp, {fp, sp, pc}
	.size	ftl_memcmp, .-ftl_memcmp
	.align	2
	.global	js_hash
	.syntax unified
	.arm
	.fpu softvfp
	.type	js_hash, %function
js_hash:
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 1, uses_anonymous_args = 0
	mov	ip, sp
	push	{fp, ip, lr, pc}
	sub	fp, ip, #4
	ldr	r3, .L1024
	add	r1, r0, r1
.L1022:
	cmp	r0, r1
	bne	.L1023
	mov	r0, r3
	ldmfd	sp, {fp, sp, pc}
.L1023:
	lsr	r2, r3, #2
	ldrb	ip, [r0], #1	@ zero_extendqisi2
	add	r2, r2, r3, lsl #5
	add	r2, r2, ip
	eor	r3, r3, r2
	b	.L1022
.L1025:
	.align	2
.L1024:
	.word	1204201446
	.size	js_hash, .-js_hash
	.align	2
	.global	Ftl_write_map_blk_to_last_page
	.syntax unified
	.arm
	.fpu softvfp
	.type	Ftl_write_map_blk_to_last_page, %function
Ftl_write_map_blk_to_last_page:
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 1, uses_anonymous_args = 0
	mov	ip, sp
	push	{r4, r5, r6, r7, r8, fp, ip, lr, pc}
	sub	fp, ip, #4
	ldrh	r3, [r0]
	movw	r2, #65535
	mov	r4, r0
	ldr	r5, [r0, #12]
	cmp	r3, r2
	bne	.L1027
	ldrh	r3, [r0, #8]
	cmp	r3, #0
	beq	.L1028
	movw	r2, #641
	ldr	r1, .L1036
	ldr	r0, .L1036+4
	bl	sftl_printk
.L1028:
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
.L1029:
	mov	r0, #0
	ldmfd	sp, {r4, r5, r6, r7, r8, fp, sp, pc}
.L1027:
	lsl	r3, r3, #1
	ldr	r7, [r0, #24]
	mov	r1, #255
	ldrh	r8, [r5, r3]
	ldrh	r3, [r0, #2]
	ldr	r5, .L1036+8
	orr	r3, r3, r8, lsl #10
	ldr	r6, [r5, #3336]
	str	r3, [r5, #3460]
	ldr	r3, [r5, #3304]
	str	r6, [r5, #3468]
	str	r3, [r5, #3464]
	ldr	r3, [r0, #28]
	str	r3, [r6, #4]
	ldr	r3, .L1036+12
	strh	r3, [r6, #8]	@ movhi
	ldrh	r3, [r0, #4]
	strh	r8, [r6, #2]	@ movhi
	strh	r3, [r6]	@ movhi
	add	r3, r5, #308
	ldrh	r2, [r3]
	ldr	r0, [r5, #3304]
	lsl	r2, r2, #3
	bl	ftl_memset
	mov	r2, #0
	mov	r3, r2
.L1030:
	ldrh	r0, [r4, #6]
	uxth	r1, r2
	cmp	r0, r1
	bhi	.L1032
	movw	r3, #314
	ldr	r0, [r5, #3464]
	ldrh	r1, [r5, r3]
	bl	js_hash
	mov	r2, #1
	str	r0, [r6, #12]
	mov	r3, #0
	mov	r1, r2
	ldr	r0, .L1036+16
	bl	FlashProgPages
	ldrh	r3, [r4, #2]
	mov	r0, r4
	add	r3, r3, #1
	strh	r3, [r4, #2]	@ movhi
	bl	ftl_map_blk_gc
	b	.L1029
.L1032:
	uxth	r1, r2
	ldr	r0, [r7, r1, lsl #2]
	cmp	r8, r0, lsr #10
	bne	.L1031
	ldr	r0, [r5, #3304]
	add	r3, r3, #1
	uxth	r3, r3
	str	r1, [r0, r3, lsl #3]
	ldr	r0, [r7, r1, lsl #2]
	ldr	r1, [r5, #3304]
	add	r1, r1, r3, lsl #3
	str	r0, [r1, #4]
.L1031:
	add	r2, r2, #1
	b	.L1030
.L1037:
	.align	2
.L1036:
	.word	.LANCHOR1+332
	.word	.LC8
	.word	.LANCHOR0
	.word	-1291
	.word	.LANCHOR0+3456
	.size	Ftl_write_map_blk_to_last_page, .-Ftl_write_map_blk_to_last_page
	.align	2
	.global	FtlMapWritePage
	.syntax unified
	.arm
	.fpu softvfp
	.type	FtlMapWritePage, %function
FtlMapWritePage:
	@ args = 0, pretend = 0, frame = 4
	@ frame_needed = 1, uses_anonymous_args = 0
	mov	ip, sp
	push	{r4, r5, r6, r7, r8, r9, r10, fp, ip, lr, pc}
	sub	fp, ip, #4
	sub	sp, sp, #4
	mov	r6, #0
	ldr	r5, .L1056
	mov	r4, r0
	mov	r7, r1
	mov	r9, r2
	add	r8, r5, #308
.L1039:
	ldr	r3, [r5, #2576]
	add	r3, r3, #1
	str	r3, [r5, #2576]
	ldrh	r3, [r8]
	ldrh	r2, [r4, #2]
	sub	r3, r3, #1
	cmp	r2, r3
	bge	.L1040
	ldrh	r2, [r4]
	movw	r3, #65535
	cmp	r2, r3
	bne	.L1041
.L1040:
	mov	r0, r4
	bl	Ftl_write_map_blk_to_last_page
.L1041:
	ldrh	r3, [r4]
	ldr	r2, [r4, #12]
	lsl	r3, r3, #1
	ldrh	r3, [r2, r3]
	cmp	r3, #0
	bne	.L1042
	mov	r2, #700
	ldr	r1, .L1056+4
	ldr	r0, .L1056+8
	bl	sftl_printk
.L1042:
	ldrh	r2, [r4]
	ldrh	r3, [r4, #10]
	cmp	r2, r3
	bcc	.L1043
	movw	r2, #701
	ldr	r1, .L1056+4
	ldr	r0, .L1056+8
	bl	sftl_printk
.L1043:
	ldrh	r3, [r4]
	mov	r1, #16
	ldr	r2, [r4, #12]
	ldr	r0, [r5, #3336]
	lsl	r3, r3, #1
	ldrh	r2, [r2, r3]
	ldrh	r3, [r4, #2]
	str	r0, [r5, #3468]
	str	r2, [fp, #-44]
	str	r9, [r5, #3464]
	orr	r3, r3, r2, lsl #10
	str	r3, [r5, #3460]
	bl	__memzero
	ldr	r3, [r4, #28]
	ldr	r10, [r5, #3468]
	ldr	r2, [fp, #-44]
	str	r3, [r10, #4]
	strh	r7, [r10, #8]	@ movhi
	ldrh	r3, [r4, #4]
	strh	r2, [r10, #2]	@ movhi
	strh	r3, [r10]	@ movhi
	ldr	r3, .L1056+12
	ldr	r0, [r5, #3464]
	ldrh	r1, [r3]
	bl	js_hash
	mov	r3, #1
	str	r0, [r10, #12]
	mov	r2, r3
	mov	r1, r3
	ldr	r0, .L1056+16
	bl	FlashProgPages
	ldrh	r3, [r4, #2]
	add	r3, r3, #1
	uxth	r3, r3
	strh	r3, [r4, #2]	@ movhi
	ldr	r2, [r5, #3456]
	cmn	r2, #1
	bne	.L1044
	ldr	r1, [r5, #3460]
	add	r6, r6, #1
	ldr	r0, .L1056+20
	uxth	r6, r6
	bl	sftl_printk
	ldrh	r3, [r4, #2]
	cmp	r3, #2
	ldrhls	r3, [r8]
	subls	r3, r3, #1
	strhls	r3, [r4, #2]	@ movhi
	cmp	r6, #3
	bls	.L1046
	mov	r2, r6
	ldr	r1, [r5, #3460]
	ldr	r0, .L1056+24
	bl	sftl_printk
.L1047:
	b	.L1047
.L1046:
	ldr	r3, [r4, #32]
	cmp	r3, #0
	beq	.L1039
.L1055:
	b	.L1055
.L1044:
	cmp	r3, #1
	cmpne	r2, #256
	beq	.L1050
	ldr	r0, [r4, #36]
	cmp	r0, #0
	beq	.L1051
.L1050:
	mov	r3, #0
	str	r3, [r4, #36]
	b	.L1039
.L1051:
	ldr	r2, [r5, #3460]
	ldr	r3, [r4, #24]
	str	r2, [r3, r7, lsl #2]
	ldmib	sp, {r4, r5, r6, r7, r8, r9, r10, fp, sp, pc}
.L1057:
	.align	2
.L1056:
	.word	.LANCHOR0
	.word	.LANCHOR1+363
	.word	.LC8
	.word	.LANCHOR0+314
	.word	.LANCHOR0+3456
	.word	.LC104
	.word	.LC105
	.size	FtlMapWritePage, .-FtlMapWritePage
	.align	2
	.global	load_l2p_region
	.syntax unified
	.arm
	.fpu softvfp
	.type	load_l2p_region, %function
load_l2p_region:
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 1, uses_anonymous_args = 0
	mov	ip, sp
	push	{r4, r5, r6, r7, r8, r9, r10, fp, ip, lr, pc}
	sub	fp, ip, #4
	ldr	r9, .L1065
	uxth	r6, r0
	uxth	r10, r1
	ldrh	r3, [r9]
	cmp	r3, r6
	bcs	.L1059
	movw	r2, #485
	ldr	r1, .L1065+4
	ldr	r0, .L1065+8
	bl	sftl_printk
.L1059:
	ldr	r7, .L1065+12
	mov	r5, #12
	ldr	r3, [r7, #3380]
	mov	r4, r7
	ldr	r8, [r3, r6, lsl #2]
	cmp	r8, #0
	bne	.L1060
	mul	r5, r5, r10
	ldr	r3, [r7, #2544]
	movw	r2, #314
	mov	r1, #255
	ldrh	r2, [r7, r2]
	add	r3, r3, r5
	ldr	r0, [r3, #8]
	bl	ftl_memset
	ldr	r3, [r7, #2544]
	strh	r6, [r3, r5]	@ movhi
	ldr	r3, [r7, #2544]
	add	r5, r3, r5
	str	r8, [r5, #4]
.L1061:
	mov	r0, #0
	ldmfd	sp, {r4, r5, r6, r7, r8, r9, r10, fp, sp, pc}
.L1060:
	mul	r5, r5, r10
	ldr	r3, [r7, #2544]
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
	beq	.L1062
	mov	r2, r8
	mov	r1, r6
	ldr	r0, .L1065+16
	bl	sftl_printk
	mov	r3, #4
	ldr	r1, [r4, #3468]
	mov	r2, r3
	ldr	r0, .L1065+20
	bl	rknand_print_hex
	ldrh	r3, [r9]
	mov	r2, #4
	ldr	r1, [r4, #3380]
	ldr	r0, .L1065+24
	bl	rknand_print_hex
.L1063:
	ldrh	r3, [r7, #8]
	cmp	r3, r6
	beq	.L1064
	mov	r2, #508
	ldr	r1, .L1065+4
	ldr	r0, .L1065+8
	bl	sftl_printk
.L1064:
	ldr	r3, [r4, #2544]
	mov	r1, #0
	add	r2, r3, r5
	str	r1, [r2, #4]
	strh	r6, [r3, r5]	@ movhi
	b	.L1061
.L1062:
	ldr	r3, [r4, #3456]
	cmp	r3, #256
	bne	.L1063
	mov	r2, r8
	mov	r1, r6
	ldr	r0, .L1065+28
	bl	sftl_printk
	ldr	r3, [r4, #2544]
	mov	r1, r6
	ldr	r0, .L1065+32
	add	r3, r3, r5
	ldr	r2, [r3, #8]
	bl	FtlMapWritePage
	b	.L1063
.L1066:
	.align	2
.L1065:
	.word	.LANCHOR0+340
	.word	.LANCHOR1+379
	.word	.LC8
	.word	.LANCHOR0
	.word	.LC106
	.word	.LC94
	.word	.LC107
	.word	.LC108
	.word	.LANCHOR0+3396
	.size	load_l2p_region, .-load_l2p_region
	.align	2
	.global	ftl_map_blk_gc
	.syntax unified
	.arm
	.fpu softvfp
	.type	ftl_map_blk_gc, %function
ftl_map_blk_gc:
	@ args = 0, pretend = 0, frame = 8
	@ frame_needed = 1, uses_anonymous_args = 0
	mov	ip, sp
	push	{r4, r5, r6, r7, r8, r9, r10, fp, ip, lr, pc}
	sub	fp, ip, #4
	sub	sp, sp, #8
	mov	r4, r0
	ldr	r5, [r0, #12]
	ldr	r9, [r0, #24]
	bl	ftl_free_no_use_map_blk
	ldrh	r3, [r4, #10]
	ldrh	r2, [r4, #8]
	sub	r3, r3, #5
	cmp	r2, r3
	blt	.L1068
	uxth	r0, r0
	lsl	r0, r0, #1
	ldrh	r8, [r5, r0]
	cmp	r8, #0
	beq	.L1068
	ldr	r3, [r4, #32]
	cmp	r3, #0
	bne	.L1068
	mov	r2, #1
	str	r2, [r4, #32]
	strh	r3, [r5, r0]	@ movhi
	ldrh	r3, [r4, #8]
	ldrh	r2, [r4, #2]
	sub	r3, r3, #1
	strh	r3, [r4, #8]	@ movhi
	ldr	r3, .L1083
	ldrh	r3, [r3]
	cmp	r2, r3
	bcc	.L1069
	mov	r0, r4
	bl	ftl_map_blk_alloc_new_blk
.L1069:
	ldr	r5, .L1083+4
	mov	r6, #0
.L1070:
	ldrh	r3, [r4, #6]
	uxth	r10, r6
	cmp	r3, r10
	bhi	.L1077
	mov	r1, #1
	mov	r0, r8
	bl	FtlFreeSysBlkQueueIn
	mov	r3, #0
	str	r3, [r4, #32]
.L1068:
	ldr	r3, .L1083
	ldrh	r2, [r4, #2]
	ldrh	r3, [r3]
	cmp	r2, r3
	bcc	.L1078
	mov	r0, r4
	bl	ftl_map_blk_alloc_new_blk
.L1078:
	mov	r0, #0
	sub	sp, fp, #40
	ldmfd	sp, {r4, r5, r6, r7, r8, r9, r10, fp, sp, pc}
.L1077:
	uxth	r7, r6
	add	r3, r9, r7, lsl #2
	str	r3, [fp, #-44]
	ldr	r3, [r9, r7, lsl #2]
	cmp	r8, r3, lsr #10
	bne	.L1071
	ldr	r3, [r5, #3308]
	ldr	r0, .L1083+8
	str	r3, [r5, #3464]
	ldr	r3, [r5, #3336]
	str	r3, [r5, #3468]
	ldr	r2, [r9, r7, lsl #2]
	str	r3, [fp, #-48]
	str	r2, [r5, #3460]
	mov	r2, #1
	mov	r1, r2
	bl	FlashReadPages
	ldr	r3, [fp, #-48]
	ldrh	r2, [r3, #8]
	cmp	r2, r10
	beq	.L1072
	movw	r2, #611
	ldr	r1, .L1083+12
	ldr	r0, .L1083+16
	bl	sftl_printk
	ldr	r3, [fp, #-48]
.L1072:
	ldr	r2, [r5, #3456]
	cmn	r2, #1
	bne	.L1073
.L1075:
	ldr	r2, [fp, #-44]
	mov	r3, #0
	str	r3, [r2]
.L1074:
	b	.L1074
.L1073:
	ldrh	r2, [r3, #8]
	cmp	r2, r10
	bne	.L1075
	ldrh	r2, [r3]
	ldrh	r3, [r4, #4]
	cmp	r2, r3
	bne	.L1075
	ldr	r2, [r5, #3464]
	mov	r1, r7
	mov	r0, r4
	bl	FtlMapWritePage
.L1071:
	add	r6, r6, #1
	b	.L1070
.L1084:
	.align	2
.L1083:
	.word	.LANCHOR0+308
	.word	.LANCHOR0
	.word	.LANCHOR0+3456
	.word	.LANCHOR1+395
	.word	.LC8
	.size	ftl_map_blk_gc, .-ftl_map_blk_gc
	.align	2
	.global	flush_l2p_region
	.syntax unified
	.arm
	.fpu softvfp
	.type	flush_l2p_region, %function
flush_l2p_region:
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 1, uses_anonymous_args = 0
	mov	ip, sp
	push	{r4, r5, fp, ip, lr, pc}
	sub	fp, ip, #4
	ldr	r5, .L1086
	uxth	r0, r0
	mov	r4, #12
	mul	r4, r4, r0
	ldr	r3, [r5, #2544]
	add	r0, r5, #3392
	add	r0, r0, #4
	add	r2, r3, r4
	ldrh	r1, [r3, r4]
	ldr	r2, [r2, #8]
	bl	FtlMapWritePage
	ldr	r0, [r5, #2544]
	add	r4, r0, r4
	mov	r0, #0
	ldr	r3, [r4, #4]
	bic	r3, r3, #-2147483648
	str	r3, [r4, #4]
	ldmfd	sp, {r4, r5, fp, sp, pc}
.L1087:
	.align	2
.L1086:
	.word	.LANCHOR0
	.size	flush_l2p_region, .-flush_l2p_region
	.align	2
	.global	log2phys
	.syntax unified
	.arm
	.fpu softvfp
	.type	log2phys, %function
log2phys:
	@ args = 0, pretend = 0, frame = 8
	@ frame_needed = 1, uses_anonymous_args = 0
	mov	ip, sp
	push	{r4, r5, r6, r7, r8, r9, r10, fp, ip, lr, pc}
	sub	fp, ip, #4
	sub	sp, sp, #8
	mov	r6, r0
	ldr	r4, .L1101
	mov	r7, r1
	mov	r10, r2
	add	r3, r4, #312
	ldrh	r5, [r3]
	ldr	r3, [r4, #2556]
	cmp	r0, r3
	bcc	.L1089
	movw	r2, #813
	ldr	r1, .L1101+4
	ldr	r0, .L1101+8
	bl	sftl_printk
.L1089:
	ldr	r3, [r4, #2556]
	cmp	r6, r3
	bcs	.L1090
	add	r5, r5, #7
	ldr	ip, [r4, #2544]
	lsr	r3, r6, r5
	mov	r1, #0
	mov	r2, #12
	str	r3, [fp, #-44]
	movw	r3, #342
	ldrh	r9, [fp, #-44]
	ldrh	r0, [r4, r3]
.L1091:
	uxth	r8, r1
	cmp	r8, r0
	bcc	.L1096
	str	r2, [fp, #-48]
	bl	select_l2p_ram_region
	ldr	r2, [fp, #-48]
	mov	r8, r0
	ldr	r1, [r4, #2544]
	mul	r2, r2, r0
	add	ip, r1, r2
	ldrh	r1, [r1, r2]
	movw	r2, #65535
	cmp	r1, r2
	beq	.L1097
	ldr	r2, [ip, #4]
	cmp	r2, #0
	bge	.L1097
	bl	flush_l2p_region
.L1097:
	mov	r1, r8
	ldrh	r0, [fp, #-44]
	bl	load_l2p_region
	b	.L1093
.L1090:
	cmp	r10, #0
	mvn	r0, #0
	streq	r0, [r7]
.L1088:
	sub	sp, fp, #40
	ldmfd	sp, {r4, r5, r6, r7, r8, r9, r10, fp, sp, pc}
.L1096:
	add	r1, r1, #1
	mla	lr, r2, r1, ip
	ldrh	lr, [lr, #-12]
	cmp	lr, r9
	bne	.L1091
.L1093:
	mvn	r2, #0
	cmp	r10, #0
	bic	r5, r6, r2, lsl r5
	mov	r2, #12
	uxth	r5, r5
	bne	.L1094
	ldr	r1, [r4, #2544]
	mla	r2, r2, r8, r1
	ldr	r2, [r2, #8]
	ldr	r2, [r2, r5, lsl #2]
	str	r2, [r7]
.L1095:
	ldr	r1, [r4, #2544]
	mov	r2, #12
	mov	r0, #0
	mla	r3, r2, r8, r1
	ldr	r2, [r3, #4]
	cmn	r2, #1
	addne	r2, r2, #1
	strne	r2, [r3, #4]
	b	.L1088
.L1094:
	mul	r2, r2, r8
	ldr	r1, [r4, #2544]
	ldr	r0, [r7]
	add	r1, r1, r2
	ldr	r1, [r1, #8]
	str	r0, [r1, r5, lsl #2]
	ldr	r1, [r4, #2544]
	add	r2, r1, r2
	ldr	r1, [r2, #4]
	orr	r1, r1, #-2147483648
	str	r1, [r2, #4]
	movw	r2, #2548
	strh	r9, [r4, r2]	@ movhi
	b	.L1095
.L1102:
	.align	2
.L1101:
	.word	.LANCHOR0
	.word	.LANCHOR1+410
	.word	.LC8
	.size	log2phys, .-log2phys
	.align	2
	.global	FtlWriteDump_data
	.syntax unified
	.arm
	.fpu softvfp
	.type	FtlWriteDump_data, %function
FtlWriteDump_data:
	@ args = 0, pretend = 0, frame = 24
	@ frame_needed = 1, uses_anonymous_args = 0
	mov	ip, sp
	push	{r4, r5, r6, r7, r8, r9, r10, fp, ip, lr, pc}
	sub	fp, ip, #4
	sub	sp, sp, #24
	ldr	r4, .L1121
	ldrh	r2, [r4, #32]
	cmp	r2, #0
	beq	.L1104
	ldrb	r3, [r4, #36]	@ zero_extendqisi2
	cmp	r3, #0
	bne	.L1104
	movw	r3, #306
	ldrb	r1, [r4, #35]	@ zero_extendqisi2
	ldrh	r3, [r4, r3]
	mul	r3, r3, r1
	cmp	r2, r3
	beq	.L1104
	ldrb	r7, [r4, #38]	@ zero_extendqisi2
	cmp	r7, #0
	bne	.L1103
	ldr	r6, [r4, #2556]
	mov	r2, r7
	sub	r1, fp, #64
	ldrh	r8, [r4, #236]
	sub	r6, r6, #1
	mov	r0, r6
	bl	log2phys
	ldr	r3, [fp, #-64]
	ldr	r5, [r4, #3336]
	ldr	r0, [r4, #3304]
	cmn	r3, #1
	str	r3, [fp, #-56]
	str	r6, [fp, #-44]
	str	r0, [fp, #-52]
	str	r5, [fp, #-48]
	str	r7, [r5, #4]
	beq	.L1106
	mov	r2, r7
	mov	r1, #1
	sub	r0, fp, #60
	bl	FlashReadPages
.L1107:
	ldr	r10, .L1121+4
	mov	r7, #0
	ldr	r3, .L1121+8
	lsl	r8, r8, #2
	mov	r9, r7
	strh	r3, [r5]	@ movhi
.L1108:
	cmp	r8, r7
	bne	.L1112
.L1109:
	mov	r3, #1
.L1120:
	strb	r3, [r4, #38]
.L1103:
	sub	sp, fp, #40
	ldmfd	sp, {r4, r5, r6, r7, r8, r9, r10, fp, sp, pc}
.L1106:
	movw	r3, #314
	mov	r1, #255
	ldrh	r2, [r4, r3]
	bl	ftl_memset
	b	.L1107
.L1112:
	ldrh	r3, [r4, #32]
	cmp	r3, #0
	beq	.L1109
	ldr	r3, [fp, #-56]
	mov	r0, r10
	str	r6, [r5, #8]
	add	r7, r7, #1
	str	r3, [r5, #12]
	ldrh	r3, [r4, #28]
	strh	r3, [r5, #2]	@ movhi
	bl	get_new_active_ppa
	ldr	r3, [r4, #2596]
	mov	r1, #1
	str	r0, [fp, #-56]
	sub	r0, fp, #60
	str	r3, [r5, #4]
	add	r3, r3, #1
	cmn	r3, #1
	moveq	r3, r9
	str	r3, [r4, #2596]
	mov	r3, #0
	mov	r2, r3
	bl	FlashProgPages
	ldrh	r0, [r4, #28]
	bl	decrement_vpc_count
	b	.L1108
.L1104:
	mov	r3, #0
	b	.L1120
.L1122:
	.align	2
.L1121:
	.word	.LANCHOR0
	.word	.LANCHOR0+28
	.word	-3947
	.size	FtlWriteDump_data, .-FtlWriteDump_data
	.align	2
	.global	FtlReUsePrevPpa
	.syntax unified
	.arm
	.fpu softvfp
	.type	FtlReUsePrevPpa, %function
FtlReUsePrevPpa:
	@ args = 0, pretend = 0, frame = 4
	@ frame_needed = 1, uses_anonymous_args = 0
	mov	ip, sp
	push	{r4, r5, r6, r7, fp, ip, lr, pc}
	sub	fp, ip, #4
	sub	sp, sp, #4
	mov	r6, r0
	ldr	r7, .L1133
	ubfx	r0, r1, #10, #16
	str	r1, [fp, #-32]
	bl	P2V_block_in_plane
	ldr	r2, [r7, #76]
	lsl	r5, r0, #1
	ldrh	r3, [r2, r5]
	cmp	r3, #0
	bne	.L1124
	ldr	r4, [r7, #2536]
	cmp	r4, #0
	beq	.L1125
	ldr	r2, [r7, #2520]
	movw	lr, #65535
	ldrh	ip, [r7, #228]
	sub	r4, r4, r2
	ubfx	r4, r4, #3, #16
.L1126:
	uxth	r1, r3
	cmp	ip, r1
	bls	.L1125
	cmp	r4, r0
	bne	.L1127
	mov	r1, r4
	ldr	r0, .L1133+4
	bl	List_remove_node
	ldrh	r3, [r7, #228]
	cmp	r3, #0
	bne	.L1128
	movw	r2, #1733
	ldr	r1, .L1133+8
	ldr	r0, .L1133+12
	bl	sftl_printk
.L1128:
	ldrh	r3, [r7, #228]
	mov	r0, r4
	sub	r3, r3, #1
	strh	r3, [r7, #228]	@ movhi
	bl	INSERT_DATA_LIST
	ldr	r2, [r7, #76]
	ldrh	r3, [r2, r5]
.L1124:
	add	r3, r3, #1
	strh	r3, [r2, r5]	@ movhi
	b	.L1125
.L1127:
	lsl	r4, r4, #3
	add	r3, r3, #1
	ldrh	r4, [r2, r4]
	cmp	r4, lr
	bne	.L1126
.L1125:
	mov	r2, #1
	sub	r1, fp, #32
	mov	r0, r6
	bl	log2phys
	ldmib	sp, {r4, r5, r6, r7, fp, sp, pc}
.L1134:
	.align	2
.L1133:
	.word	.LANCHOR0
	.word	.LANCHOR0+2536
	.word	.LANCHOR1+419
	.word	.LC8
	.size	FtlReUsePrevPpa, .-FtlReUsePrevPpa
	.align	2
	.global	FtlRecoverySuperblock
	.syntax unified
	.arm
	.fpu softvfp
	.type	FtlRecoverySuperblock, %function
FtlRecoverySuperblock:
	@ args = 0, pretend = 0, frame = 48
	@ frame_needed = 1, uses_anonymous_args = 0
	mov	ip, sp
	push	{r4, r5, r6, r7, r8, r9, r10, fp, ip, lr, pc}
	sub	fp, ip, #4
	sub	sp, sp, #48
	movw	r2, #65535
	ldrh	r3, [r0]
	mov	r10, r0
	cmp	r3, r2
	beq	.L1259
	ldrh	r3, [r0, #2]
	ldr	r5, .L1267
	str	r3, [fp, #-64]
	ldrb	r3, [r0, #6]	@ zero_extendqisi2
	ldr	r1, [fp, #-64]
	str	r3, [fp, #-84]
	movw	r3, #306
	ldrh	r3, [r5, r3]
	cmp	r3, r1
	mov	r3, #0
	strheq	r3, [r0, #4]	@ movhi
	ldrhne	r0, [r0, #16]
	bne	.L1139
.L1265:
	strb	r3, [r10, #6]
.L1259:
	mov	r0, #0
	sub	sp, fp, #40
	ldmfd	sp, {r4, r5, r6, r7, r8, r9, r10, fp, sp, pc}
.L1140:
	uxth	r1, r3
	add	r1, r10, r1, lsl #1
	ldrh	r0, [r1, #16]
.L1139:
	cmp	r0, r2
	uxth	r6, r3
	add	r3, r3, #1
	beq	.L1140
	mov	r1, #1
	bl	FtlGetLastWrittenPage
	cmn	r0, #1
	mov	r4, r0
	beq	.L1141
	ldrh	r3, [r5, #236]
	mov	r2, #0
	ldr	r0, [r5, #3292]
	movw	r7, #65535
	ldr	lr, [r5, #3184]
	mov	r8, #20
	str	r3, [fp, #-56]
	ldr	r3, [r5, #3180]
	str	r3, [fp, #-60]
	movw	r3, #314
	ldrh	r3, [r5, r3]
	mov	r5, r2
	str	r3, [fp, #-68]
	ldr	r3, .L1267+4
	ldrh	r6, [r3]
	add	r3, r10, #16
	mov	ip, r3
	str	r3, [fp, #-80]
.L1142:
	ldr	r1, [fp, #-56]
	uxth	r3, r2
	cmp	r1, r3
	bhi	.L1146
	ldr	r6, .L1267
	mov	r2, #0
	mov	r1, r5
	movw	r8, #65535
	bl	FlashReadPages
	ldr	r3, [r6, #2596]
	uxth	r2, r4
	ldr	r9, [r6, #3292]
	str	r2, [fp, #-60]
	sub	r3, r3, #1
	str	r3, [fp, #-56]
	mov	r7, r9
	mov	r3, #0
.L1147:
	uxth	r2, r3
	cmp	r5, r2
	bhi	.L1152
	addeq	r3, r4, #1
	uxtheq	r3, r3
	streq	r3, [fp, #-60]
	bne	.L1150
.L1260:
	ldr	r0, [r9, #4]
	ubfx	r0, r0, #10, #16
	bl	P2V_plane
	movw	r3, #306
	ldr	r2, [fp, #-60]
	ldrh	r3, [r6, r3]
	sub	r1, fp, #68
	str	r0, [fp, #-68]
	ldr	r0, [fp, #-84]
	cmp	r3, r2
	ldrheq	r3, [fp, #-60]
	strheq	r3, [r10, #2]	@ movhi
	moveq	r3, #0
	strbeq	r3, [r10, #6]
	strheq	r3, [r10, #4]	@ movhi
	ldm	r1, {r1, r2, r3}
	cmp	r3, r2
	cmpeq	r1, r0
	moveq	r2, r1
	moveq	r1, r3
	beq	.L1266
	movw	r3, #65535
	cmp	r8, r3
	bne	.L1156
	ldrb	r3, [r10, #8]	@ zero_extendqisi2
	cmp	r3, #0
	bne	.L1157
.L1156:
	ldr	r3, [r6, #3452]
	uxth	r7, r4
	uxth	r4, r4
	cmn	r3, #1
	ldreq	r3, [fp, #-56]
	streq	r3, [r6, #3452]
	ldr	r3, [fp, #-64]
	ldr	r6, .L1267
	add	r3, r3, #7
	cmp	r4, r3
	subgt	r4, r7, #7
	ldrle	r4, [fp, #-64]
	uxthgt	r4, r4
.L1160:
	cmp	r4, r7
	bhi	.L1167
	mov	r3, #0
	ldrh	r8, [r6, #236]
	ldr	r0, [r6, #3292]
	mov	r5, r3
	ldr	r1, [fp, #-80]
	movw	lr, #65535
	mov	ip, #20
	b	.L1168
.L1141:
	ldr	r3, [fp, #-64]
	cmp	r3, #0
	beq	.L1143
	movw	r2, #1800
	ldr	r1, .L1267+8
	ldr	r0, .L1267+12
	bl	sftl_printk
.L1143:
	ldr	r3, [fp, #-84]
	cmp	r3, #0
	cmpne	r6, r3
	beq	.L1144
	movw	r2, #1801
	ldr	r1, .L1267+8
	ldr	r0, .L1267+12
	bl	sftl_printk
.L1144:
	mov	r3, #0
	strh	r3, [r10, #2]	@ movhi
	b	.L1265
.L1146:
	ldrh	r3, [ip], #2
	cmp	r3, r7
	beq	.L1145
	mla	r1, r8, r5, r0
	orr	r3, r4, r3, lsl #10
	str	r3, [r1, #4]
	ldr	r3, [fp, #-68]
	mul	r3, r3, r5
	add	r9, r3, #3
	cmp	r3, #0
	movlt	r3, r9
	ldr	r9, [fp, #-60]
	bic	r3, r3, #3
	add	r3, r9, r3
	str	r3, [r1, #8]
	mul	r3, r6, r5
	add	r5, r5, #1
	uxth	r5, r5
	add	r9, r3, #3
	cmp	r3, #0
	movlt	r3, r9
	bic	r3, r3, #3
	add	r3, lr, r3
	str	r3, [r1, #12]
.L1145:
	add	r2, r2, #1
	b	.L1142
.L1152:
	ldr	r2, [r7]
	cmp	r2, #0
	bne	.L1148
	ldr	ip, [r7, #12]
	ldr	r2, [ip, #4]
	cmn	r2, #1
	beq	.L1149
	ldr	r1, .L1267
	mov	r0, r2
	str	ip, [fp, #-68]
	ldr	r1, [r1, #2596]
	bl	ftl_cmp_data_ver
	ldr	ip, [fp, #-68]
	cmp	r0, #0
	ldrne	r1, .L1267
	addne	r2, r2, #1
	strne	r2, [r1, #2596]
.L1149:
	ldr	r2, [ip]
	cmn	r2, #1
	bne	.L1151
.L1150:
	uxth	r2, r4
	uxth	r3, r3
	str	r2, [fp, #-60]
	mov	r2, #20
	mla	r9, r2, r3, r9
	b	.L1260
.L1148:
	ldr	r8, [fp, #-60]
.L1151:
	add	r3, r3, #1
	add	r7, r7, #20
	b	.L1147
.L1162:
	ldrh	r2, [r1], #2
	add	r3, r3, #1
	cmp	r2, lr
	mlane	r9, ip, r5, r0
	addne	r5, r5, #1
	orrne	r2, r4, r2, lsl #10
	uxthne	r5, r5
	strne	r2, [r9, #4]
.L1168:
	uxth	r2, r3
	cmp	r8, r2
	bhi	.L1162
	mov	r1, r5
	mov	r2, #0
	bl	FlashReadPages
	ldr	r3, [r6, #3292]
	mov	r2, #20
	movw	r1, #65535
	mla	r5, r2, r5, r3
.L1163:
	cmp	r5, r3
	addeq	r4, r4, #1
	uxtheq	r4, r4
	beq	.L1160
.L1166:
	ldr	r2, [r3]
	cmp	r2, #0
	bne	.L1157
	ldr	r2, [r3, #12]
	ldrh	r0, [r2]
	cmp	r0, r1
	beq	.L1165
	ldr	r2, [r2, #4]
	cmn	r2, #1
	strne	r2, [r6, #3452]
.L1165:
	add	r3, r3, #20
	b	.L1163
.L1167:
	mvn	r3, #0
	str	r3, [r6, #3452]
.L1157:
	ldr	r7, .L1267
	mov	r2, #1
	ldr	r9, [fp, #-64]
	movw	r3, #3476
	mov	r4, r7
	strh	r2, [r7, r3]	@ movhi
.L1169:
	ldrh	ip, [r4, #236]
	movw	lr, #65535
	ldr	r0, [r4, #3292]
	mov	r5, #20
	ldr	r1, [fp, #-80]
	mov	r3, #0
	str	r3, [fp, #-72]
.L1170:
	uxth	r2, r3
	cmp	ip, r2
	bhi	.L1172
	mov	r2, #0
	ldr	r1, [fp, #-72]
	bl	FlashReadPages
	mov	r3, #0
.L1264:
	str	r3, [fp, #-76]
	ldr	r2, [fp, #-72]
	ldrh	r3, [fp, #-76]
	cmp	r2, r3
	bhi	.L1200
	ldr	r3, .L1267+16
	add	r9, r9, #1
	uxth	r9, r9
	ldrh	r3, [r3]
	cmp	r3, r9
	bne	.L1169
	ldrh	r2, [r4, #236]
	movw	r0, #65535
	mov	r3, #0
	strh	r9, [r10, #2]	@ movhi
	strh	r3, [r10, #4]	@ movhi
.L1201:
	uxth	r1, r3
	cmp	r1, r2
	bcs	.L1259
	ldr	r1, [fp, #-80]
	ldrh	ip, [r1], #2
	cmp	ip, r0
	str	r1, [fp, #-80]
	add	r1, r3, #1
	bne	.L1265
	mov	r3, r1
	b	.L1201
.L1172:
	ldrh	r2, [r1], #2
	cmp	r2, lr
	beq	.L1171
	ldr	r6, [fp, #-72]
	orr	r2, r9, r2, lsl #10
	mla	r6, r5, r6, r0
	str	r2, [r6, #4]
	ldr	r2, [fp, #-72]
	add	r2, r2, #1
	uxth	r2, r2
	str	r2, [fp, #-72]
.L1171:
	add	r3, r3, #1
	b	.L1170
.L1200:
	ldr	r3, [fp, #-76]
	mov	r6, #20
	ldr	r2, [r4, #3292]
	mul	r6, r6, r3
	str	r2, [fp, #-88]
	add	r8, r2, r6
	ldr	r5, [r8, #4]
	ubfx	r0, r5, #10, #16
	str	r5, [fp, #-44]
	bl	P2V_plane
	ldr	r3, [fp, #-64]
	cmp	r9, r3
	bcc	.L1174
	ldr	r2, [fp, #-84]
	moveq	r3, #1
	movne	r3, #0
	cmp	r2, r0
	movls	r3, #0
	andhi	r3, r3, #1
	cmp	r3, #0
	bne	.L1174
	ldr	r3, [fp, #-60]
	ldr	r2, [fp, #-68]
	cmp	r9, r3
	cmpeq	r2, r0
	beq	.L1175
	ldr	r2, [fp, #-88]
	ldr	r3, [r2, r6]
	cmn	r3, #1
	beq	.L1176
	ldr	r8, [r8, #12]
	movw	r3, #61589
	ldrh	r2, [r8]
	cmp	r2, r3
	ldrhne	r0, [r10]
	bne	.L1261
	ldr	r3, [r8, #4]
	cmn	r3, #1
	str	r3, [fp, #-56]
	beq	.L1178
	mov	r0, r3
	ldr	r1, [r4, #2596]
	bl	ftl_cmp_data_ver
	cmp	r0, #0
	ldrne	r3, [fp, #-56]
	addne	r3, r3, #1
	strne	r3, [r4, #2596]
.L1178:
	ldrh	r2, [r8]
	movw	r3, #61589
	cmp	r2, r3
	beq	.L1179
	mov	r2, #1952
	ldr	r1, .L1267+8
	ldr	r0, .L1267+12
	bl	sftl_printk
.L1179:
	ldr	r5, [r8, #8]
	sub	r1, fp, #48
	ldr	r3, [r8, #12]
	mov	r2, #0
	mov	r0, r5
	str	r3, [fp, #-52]
	bl	log2phys
	ldr	r1, [r4, #3452]
	cmn	r1, #1
	beq	.L1180
	ldr	r0, [fp, #-56]
	bl	ftl_cmp_data_ver
	cmp	r0, #0
	beq	.L1180
	ldr	r2, [fp, #-52]
	cmn	r2, #1
	beq	.L1181
	ldr	r0, [r4, #3292]
	mov	r1, #1
	add	r0, r0, r6
	str	r2, [r0, #4]
	mov	r2, #0
	ldr	r8, [r0, #12]
	bl	FlashReadPages
	ldr	r2, [r4, #3292]
	ldr	r1, [r2, r6]
	add	r3, r2, r6
	cmn	r1, #1
	bne	.L1182
.L1183:
	mvn	r3, #0
	str	r3, [fp, #-52]
.L1190:
	ldr	r6, [fp, #-52]
	cmn	r6, #1
	beq	.L1174
.L1204:
	ubfx	r0, r6, #10, #16
	bl	P2V_block_in_plane
	ldrh	r3, [r4, #244]
	mov	r5, r0
	cmp	r3, r0
	bhi	.L1196
	movw	r2, #2057
	ldr	r1, .L1267+8
	ldr	r0, .L1267+12
	bl	sftl_printk
.L1196:
	ldr	r2, [r7, #76]
	lsl	r3, r5, #1
	ldrh	r3, [r2, r3]
	cmp	r3, #0
	beq	.L1197
	mov	r0, r5
.L1261:
	bl	decrement_vpc_count
	b	.L1174
.L1181:
	ldr	r3, [fp, #-44]
	ldr	r2, [fp, #-48]
	cmp	r2, r3
	bne	.L1174
	mov	r2, #1
	sub	r1, fp, #52
	mov	r0, r5
	bl	log2phys
.L1174:
	ldr	r3, [fp, #-76]
	add	r3, r3, #1
	b	.L1264
.L1182:
	ldr	r1, [r8, #8]
	cmp	r5, r1
	bne	.L1183
	ldr	r1, [r8, #4]
	ldr	r0, [r4, #3452]
	str	r1, [fp, #-88]
	bl	ftl_cmp_data_ver
	cmp	r0, #0
	beq	.L1183
	ldr	r1, [fp, #-48]
	ldr	r0, [fp, #-44]
	cmp	r1, r0
	bne	.L1185
.L1188:
	ldr	r1, [fp, #-52]
	mov	r0, r5
	bl	FtlReUsePrevPpa
	b	.L1183
.L1185:
	ldr	r0, [fp, #-52]
	cmp	r1, r0
	beq	.L1183
	cmn	r1, #1
	streq	r1, [r2, r6]
	beq	.L1187
	str	r1, [r3, #4]
	mov	r2, #0
	mov	r1, #1
	mov	r0, r3
	ldr	r8, [r3, #12]
	bl	FlashReadPages
.L1187:
	ldr	r2, [r4, #3292]
	ldr	r2, [r2, r6]
	cmn	r2, #1
	beq	.L1188
	ldr	r3, [r8, #4]
	ldr	r0, [r4, #3452]
	mov	r1, r3
	bl	ftl_cmp_data_ver
	cmp	r0, #0
	beq	.L1188
	mov	r1, r3
	ldr	r0, [fp, #-88]
	bl	ftl_cmp_data_ver
	cmp	r0, #0
	beq	.L1183
	b	.L1188
.L1180:
	ldr	r3, [fp, #-44]
	ldr	r2, [fp, #-48]
	cmp	r2, r3
	beq	.L1190
	ldr	r1, [fp, #-52]
	cmn	r1, #1
	beq	.L1192
	ldr	r3, [r4, #252]
	cmp	r3, r1, lsr #10
	ldrls	r0, .L1267+20
	bls	.L1263
.L1192:
	mov	r2, #1
	sub	r1, fp, #44
	mov	r0, r5
	bl	log2phys
	ldr	r6, [fp, #-48]
	cmn	r6, #1
	beq	.L1190
	ldr	r3, [fp, #-52]
	cmp	r6, r3
	beq	.L1204
	ubfx	r0, r6, #10, #16
	bl	P2V_block_in_plane
	ldrh	r3, [r4, #28]
	cmp	r3, r0
	beq	.L1195
	ldrh	r3, [r4, #80]
	cmp	r3, r0
	beq	.L1195
	ldrh	r3, [r4, #128]
	cmp	r3, r0
	bne	.L1190
.L1195:
	ldr	r0, [r7, #3292]
	mov	r2, #0
	mov	r1, #1
	str	r6, [r0, #4]
	ldr	r8, [r0, #12]
	bl	FlashReadPages
	ldr	r3, [r7, #3292]
	ldr	r3, [r3]
	cmn	r3, #1
	beq	.L1190
	ldr	r1, [r8, #4]
	ldr	r0, [fp, #-56]
	bl	ftl_cmp_data_ver
	cmp	r0, #0
	bne	.L1190
	mov	r2, #1
	sub	r1, fp, #48
	mov	r0, r5
	bl	log2phys
	b	.L1190
.L1197:
	ldr	r0, .L1267+24
	mov	r1, r5
.L1263:
	bl	sftl_printk
	b	.L1174
.L1176:
	ldr	r3, [r4, #3480]
	cmp	r3, #31
	addls	r2, r4, r3, lsl #2
	addls	r3, r3, #1
	strls	r3, [r4, #3480]
	strls	r5, [r2, #3484]
	ldrh	r0, [r10]
	bl	decrement_vpc_count
	ldr	r3, [r4, #3452]
	cmn	r3, #1
	ldreq	r3, [fp, #-56]
	beq	.L1262
	ldr	r2, [fp, #-56]
	cmp	r2, r3
	bcs	.L1174
	mov	r3, r2
.L1262:
	str	r3, [r4, #3452]
	b	.L1174
.L1175:
	ldrb	r3, [fp, #-68]	@ zero_extendqisi2
	ldr	r2, [fp, #-68]
	ldr	r1, [fp, #-60]
	strb	r3, [r10, #6]
	ldrh	r3, [fp, #-60]
	strh	r3, [r10, #2]	@ movhi
.L1266:
	mov	r0, r10
	bl	ftl_sb_update_avl_pages
	b	.L1259
.L1268:
	.align	2
.L1267:
	.word	.LANCHOR0
	.word	.LANCHOR0+316
	.word	.LANCHOR1+435
	.word	.LC8
	.word	.LANCHOR0+306
	.word	.LC109
	.word	.LC110
	.size	FtlRecoverySuperblock, .-FtlRecoverySuperblock
	.align	2
	.global	ftl_check_vpc
	.syntax unified
	.arm
	.fpu softvfp
	.type	ftl_check_vpc, %function
ftl_check_vpc:
	@ args = 0, pretend = 0, frame = 4
	@ frame_needed = 1, uses_anonymous_args = 0
	mov	ip, sp
	push	{r4, r5, r6, r7, r8, r9, r10, fp, ip, lr, pc}
	sub	fp, ip, #4
	sub	sp, sp, #4
	mov	r4, #0
	ldr	r6, .L1293
	ldr	r1, .L1293+4
	ldr	r0, .L1293+8
	mov	r5, r6
	bl	sftl_printk
	mov	r1, #8192
	ldr	r0, .L1293+12
	bl	__memzero
.L1270:
	ldr	r3, [r6, #2556]
	cmp	r4, r3
	bcc	.L1272
	ldr	r8, .L1293+12
	mov	r4, #0
	ldr	r9, .L1293+16
	mov	r6, r4
.L1273:
	ldrh	r2, [r5, #244]
	uxth	r3, r4
	cmp	r2, r3
	bhi	.L1275
	ldr	r4, [r5, #2536]
	cmp	r4, #0
	beq	.L1276
	ldr	r3, [r5, #2520]
	mov	r7, #0
	ldrh	r8, [r5, #228]
	ldr	r9, .L1293+12
	ldr	r10, .L1293+20
	sub	r4, r4, r3
	ubfx	r4, r4, #3, #16
.L1277:
	uxth	r3, r7
	cmp	r8, r3
	bls	.L1276
	ldr	r2, [r5, #76]
	lsl	r3, r4, #1
	ldrh	r2, [r2, r3]
	cmp	r2, #0
	beq	.L1278
	mov	r6, #1
	ldrh	r3, [r9, r3]
	mov	r1, r4
	mov	r0, r10
	bl	sftl_printk
.L1278:
	ldr	r3, [r5, #2520]
	lsl	r4, r4, #3
	add	r7, r7, #1
	ldrh	r4, [r3, r4]
	movw	r3, #65535
	cmp	r4, r3
	bne	.L1277
.L1276:
	cmp	r6, #0
	ldmibeq	sp, {r4, r5, r6, r7, r8, r9, r10, fp, sp, pc}
	movw	r2, #2383
	ldr	r1, .L1293+4
	ldr	r0, .L1293+24
	bl	sftl_printk
	ldmib	sp, {r4, r5, r6, r7, r8, r9, r10, fp, sp, pc}
.L1272:
	mov	r2, #0
	sub	r1, fp, #44
	mov	r0, r4
	bl	log2phys
	ldr	r0, [fp, #-44]
	cmn	r0, #1
	beq	.L1271
	ubfx	r0, r0, #10, #16
	bl	P2V_block_in_plane
	ldr	r2, .L1293+12
	lsl	r0, r0, #1
	ldrh	r3, [r2, r0]
	add	r3, r3, #1
	strh	r3, [r2, r0]	@ movhi
.L1271:
	add	r4, r4, #1
	b	.L1270
.L1275:
	uxth	r1, r4
	ldr	r3, [r5, #76]
	lsl	r7, r1, #1
	ldrh	r2, [r3, r7]
	ldrh	r3, [r8, r7]
	cmp	r2, r3
	beq	.L1274
	mov	r0, r9
	bl	sftl_printk
	ldr	r3, [r5, #76]
	movw	r2, #65535
	ldrh	r3, [r3, r7]
	cmp	r3, r2
	beq	.L1274
	ldrh	r2, [r8, r7]
	cmp	r2, r3
	movhi	r6, #1
.L1274:
	add	r4, r4, #1
	b	.L1273
.L1294:
	.align	2
.L1293:
	.word	.LANCHOR0
	.word	.LANCHOR1+457
	.word	.LC111
	.word	check_vpc_table
	.word	.LC112
	.word	.LC113
	.word	.LC8
	.size	ftl_check_vpc, .-ftl_check_vpc
	.align	2
	.global	ftl_scan_all_data
	.syntax unified
	.arm
	.fpu softvfp
	.type	ftl_scan_all_data, %function
ftl_scan_all_data:
	@ args = 0, pretend = 0, frame = 4
	@ frame_needed = 1, uses_anonymous_args = 0
	mov	ip, sp
	push	{r4, r5, r6, r7, fp, ip, lr, pc}
	sub	fp, ip, #4
	sub	sp, sp, #24
	mov	r5, #0
	ldr	r6, .L1303
	mov	r1, #0
	ldr	r0, .L1303+4
	bl	sftl_printk
	mov	r4, r6
.L1296:
	ldr	r3, [r6, #2556]
	cmp	r5, r3
	bcc	.L1302
	sub	sp, fp, #28
	ldmfd	sp, {r4, r5, r6, r7, fp, sp, pc}
.L1302:
	mov	r2, #0
	sub	r1, fp, #32
	mov	r0, r5
	bl	log2phys
	ubfx	r3, r5, #0, #11
	cmp	r3, #0
	bne	.L1297
	ldr	r2, [fp, #-32]
	mov	r1, r5
	ldr	r0, .L1303+8
	bl	sftl_printk
.L1297:
	ldr	r3, [fp, #-32]
	cmn	r3, #1
	beq	.L1299
	str	r3, [r4, #3460]
	mov	r2, #0
	ldr	r3, [r4, #3304]
	mov	r1, #1
	ldr	r7, [r4, #3336]
	ldr	r0, .L1303+12
	str	r3, [r4, #3464]
	str	r5, [r4, #3472]
	str	r7, [r4, #3468]
	str	r2, [r4, #3456]
	bl	FlashReadPages
	ldr	r3, [r4, #3456]
	cmn	r3, #1
	cmpne	r3, #256
	beq	.L1300
	ldr	r3, [r7, #8]
	cmp	r5, r3
	beq	.L1299
.L1300:
	ldr	r2, [r4, #3464]
	ldr	r3, [r4, #3468]
	ldr	r0, .L1303+16
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
.L1299:
	add	r5, r5, #1
	b	.L1296
.L1304:
	.align	2
.L1303:
	.word	.LANCHOR0
	.word	.LC114
	.word	.LC115
	.word	.LANCHOR0+3456
	.word	.LC116
	.size	ftl_scan_all_data, .-ftl_scan_all_data
	.align	2
	.global	FtlGcScanTempBlk
	.syntax unified
	.arm
	.fpu softvfp
	.type	FtlGcScanTempBlk, %function
FtlGcScanTempBlk:
	@ args = 0, pretend = 0, frame = 48
	@ frame_needed = 1, uses_anonymous_args = 0
	mov	ip, sp
	push	{r4, r5, r6, r7, r8, r9, r10, fp, ip, lr, pc}
	sub	fp, ip, #4
	sub	sp, sp, #48
	mov	r4, r0
	ldr	r3, .L1337
	str	r1, [fp, #-76]
	ldrh	r5, [r3, #4]
	movw	r3, #65535
	cmp	r5, r3
	beq	.L1330
	cmp	r5, #0
	bne	.L1306
.L1307:
	bl	FtlGcPageVarInit
	b	.L1308
.L1330:
	mov	r5, #0
.L1306:
	ldr	r2, .L1337+4
	movw	r3, #306
	ldrh	r3, [r2, r3]
	ldr	r2, [fp, #-76]
	cmp	r3, r2
	beq	.L1307
.L1308:
	ldr	r6, .L1337+4
	mov	r3, #0
	str	r3, [fp, #-68]
.L1324:
	ldrh	r2, [r4]
	mov	r3, #0
	strb	r3, [r4, #8]
	movw	r3, #65535
	cmp	r2, r3
	beq	.L1309
.L1327:
	ldrh	r3, [r6, #236]
	add	ip, r4, #16
	ldr	r0, [r6, #3292]
	movw	r9, #65535
	mov	lr, #20
	str	r3, [fp, #-72]
	ldr	r3, [r6, #3180]
	str	r3, [fp, #-80]
	ldr	r3, .L1337+8
	ldrh	r2, [r3]
	ldrh	r10, [r3, #2]
	str	r2, [fp, #-84]
	ldr	r2, [r6, #3184]
	str	r2, [fp, #-88]
	mov	r2, #0
	mov	r7, r2
.L1310:
	ldr	r1, [fp, #-72]
	uxth	r3, r2
	cmp	r1, r3
	bhi	.L1312
	mov	r8, #0
	mov	r2, #0
	mov	r1, r7
	bl	FlashReadPages
.L1313:
	uxth	r3, r8
	cmp	r7, r3
	bhi	.L1325
	ldr	r3, [fp, #-68]
	add	r5, r5, #1
	uxth	r5, r5
	add	r3, r3, #1
	str	r3, [fp, #-68]
	ldr	r2, [fp, #-68]
	ldr	r3, [fp, #-76]
	cmp	r3, r2
	ldr	r2, .L1337+12
	bls	.L1326
.L1328:
	ldrh	r3, [r2]
	cmp	r3, r5
	bhi	.L1327
.L1309:
	ldr	r3, .L1337
	mvn	r2, #0
	strh	r5, [r4, #2]	@ movhi
	mov	r1, r5
	mov	r0, r4
	strh	r2, [r3, #4]	@ movhi
	mov	r2, #0
	strb	r2, [r4, #6]
	bl	ftl_sb_update_avl_pages
	b	.L1329
.L1312:
	ldrh	r3, [ip], #2
	cmp	r3, r9
	beq	.L1311
	mla	r1, lr, r7, r0
	orr	r3, r5, r3, lsl #10
	str	r3, [r1, #4]
	ldr	r3, [fp, #-84]
	mul	r3, r3, r7
	add	r8, r3, #3
	cmp	r3, #0
	movlt	r3, r8
	ldr	r8, [fp, #-80]
	bic	r3, r3, #3
	add	r3, r8, r3
	str	r3, [r1, #8]
	mul	r3, r10, r7
	add	r7, r7, #1
	uxth	r7, r7
	add	r8, r3, #3
	cmp	r3, #0
	movlt	r3, r8
	ldr	r8, [fp, #-88]
	bic	r3, r3, #3
	add	r3, r8, r3
	str	r3, [r1, #12]
.L1311:
	add	r2, r2, #1
	b	.L1310
.L1325:
	mov	ip, #20
	ldr	r2, [r6, #3292]
	mul	ip, ip, r8
	ldr	r10, [r2, ip]
	add	r3, r2, ip
	ldr	r1, [r3, #4]
	ldr	r9, [r3, #12]
	cmp	r10, #0
	str	r1, [fp, #-72]
	bne	.L1314
	ldrh	r1, [r9]
	movw	r2, #65535
	cmp	r1, r2
	bne	.L1315
.L1335:
	ldrh	r3, [r4]
	mov	r1, #0
	ldr	r2, [r6, #76]
	mov	r5, #0
	lsl	r3, r3, #1
	strh	r1, [r2, r3]	@ movhi
	ldrh	r0, [r4]
	bl	INSERT_FREE_LIST
	mvn	r3, #0
	strh	r3, [r4]	@ movhi
	strh	r3, [r6, #176]	@ movhi
	bl	FtlGcPageVarInit
	b	.L1324
.L1315:
	ldr	r0, [r9, #8]
	ldr	r2, [r6, #2556]
	str	ip, [fp, #-80]
	cmp	r0, r2
	bhi	.L1335
	mov	r2, r10
	sub	r1, fp, #64
	bl	log2phys
	ldr	r1, [fp, #-64]
	ldr	r2, [r9, #12]
	ldr	ip, [fp, #-80]
	cmn	r1, #1
	sub	r0, r2, r1
	clz	r0, r0
	lsr	r0, r0, #5
	moveq	r0, #0
	cmp	r0, #0
	bne	.L1318
.L1323:
	ldr	r2, [r9, #8]
.L1336:
	ldr	r1, [fp, #-72]
	add	r8, r8, #1
	ldr	r0, [r9, #12]
	bl	FtlGcUpdatePage
	b	.L1313
.L1318:
	str	r2, [fp, #-56]
	mov	r1, #1
	ldr	r2, [r6, #3320]
	sub	r0, fp, #60
	str	ip, [fp, #-80]
	str	r2, [fp, #-52]
	ldr	r2, [r6, #3340]
	str	r2, [fp, #-48]
	mov	r2, r10
	bl	FlashReadPages
	ldr	r2, .L1337+16
	ldr	ip, [fp, #-80]
	ldr	r0, [fp, #-52]
	ldrh	r1, [r2]
	ldr	r2, [r6, #3292]
	lsl	r1, r1, #7
	add	ip, r2, ip
	mov	r2, r10
.L1320:
	cmp	r2, r1
	beq	.L1323
	ldr	lr, [ip, #8]
	ldr	r10, [lr, r2, lsl #2]
	ldr	lr, [r0, r2, lsl #2]
	cmp	r10, lr
	beq	.L1321
	ldr	r2, [fp, #-56]
	ldrh	r1, [r4]
	ldr	r0, .L1337+20
	bl	sftl_printk
	b	.L1335
.L1321:
	add	r2, r2, #1
	b	.L1320
.L1314:
	mvn	r2, #0
	b	.L1336
.L1326:
	ldr	r1, .L1337
	movw	r0, #65535
	ldrh	r3, [r1, #4]
	cmp	r3, r0
	beq	.L1328
	ldr	r0, [fp, #-68]
	add	r3, r3, r0
	strh	r3, [r1, #4]	@ movhi
	ldrh	r3, [r2]
	cmp	r3, r5
	bls	.L1328
.L1329:
	mvn	r0, #0
	sub	sp, fp, #40
	ldmfd	sp, {r4, r5, r6, r7, r8, r9, r10, fp, sp, pc}
.L1338:
	.align	2
.L1337:
	.word	.LANCHOR2
	.word	.LANCHOR0
	.word	.LANCHOR0+314
	.word	.LANCHOR0+306
	.word	.LANCHOR0+262
	.word	.LC117
	.size	FtlGcScanTempBlk, .-FtlGcScanTempBlk
	.align	2
	.global	FtlReadRefresh
	.syntax unified
	.arm
	.fpu softvfp
	.type	FtlReadRefresh, %function
FtlReadRefresh:
	@ args = 0, pretend = 0, frame = 88
	@ frame_needed = 1, uses_anonymous_args = 0
	mov	ip, sp
	push	{r4, r5, r6, r7, r8, r9, r10, fp, ip, lr, pc}
	sub	fp, ip, #4
	sub	sp, sp, #88
	ldr	r6, .L1355
	ldr	r9, [r6, #2724]
	mov	r5, r6
	cmp	r9, #0
	beq	.L1340
	ldr	r2, [r6, #2728]
	ldr	r3, [r6, #2556]
	cmp	r2, r3
	bcs	.L1341
	mov	r4, #2048
.L1346:
	ldr	r0, [r5, #2728]
	ldr	r3, [r5, #2556]
	cmp	r0, r3
	bcc	.L1342
.L1345:
	mvn	r0, #0
.L1339:
	sub	sp, fp, #40
	ldmfd	sp, {r4, r5, r6, r7, r8, r9, r10, fp, sp, pc}
.L1342:
	mov	r2, #0
	sub	r1, fp, #128
	bl	log2phys
	ldr	r2, [fp, #-128]
	ldr	r3, [r5, #2728]
	cmn	r2, #1
	add	r3, r3, #1
	str	r3, [r5, #2728]
	beq	.L1344
	str	r3, [fp, #-108]
	sub	r0, fp, #40
	ldr	r3, [r5, #3328]
	mov	r1, #1
	str	r2, [fp, #-120]
	mov	r2, #0
	str	r2, [r0, #-84]!
	str	r3, [fp, #-116]
	sub	r3, fp, #104
	str	r3, [fp, #-112]
	bl	FlashReadPages
	ldr	r3, [fp, #-124]
	cmp	r3, #256
	bne	.L1345
	ldr	r0, [fp, #-128]
	ubfx	r0, r0, #10, #16
	bl	P2V_block_in_plane
	bl	FtlGcRefreshBlock
	b	.L1345
.L1344:
	subs	r4, r4, #1
	bne	.L1346
	b	.L1345
.L1341:
	ldr	r3, [r6, #2560]
	mov	r0, #0
	str	r0, [r6, #2724]
	str	r0, [r6, #2728]
	str	r3, [r6, #2720]
	b	.L1339
.L1340:
	ldr	r1, [r6, #2616]
	movw	r4, #10000
	ldr	r8, [r6, #2560]
	ldr	r7, [r6, #2720]
	cmp	r1, r4
	ldr	r10, .L1355+4
	add	r3, r8, #1048576
	movhi	r4, #31
	movls	r4, #63
	cmp	r7, r3
	bhi	.L1350
	ldr	r3, [r6, #2556]
	lsr	r1, r1, #10
	mov	r0, #1000
	add	r1, r1, #1
	mul	r0, r0, r3
	bl	__udivsi3
	add	r0, r0, r7
	cmp	r8, r0
	bhi	.L1350
	ldrh	r3, [r10, #28]
	ands	r0, r4, r3
	movne	r0, r9
	bne	.L1339
	ldr	r2, [r6, #2744]
	cmp	r3, r2
	beq	.L1339
.L1350:
	ldrh	r3, [r10, #28]
	mov	r0, #0
	str	r0, [r5, #2728]
	str	r8, [r5, #2720]
	str	r3, [r5, #2744]
	mov	r3, #1
	str	r3, [r5, #2724]
	b	.L1339
.L1356:
	.align	2
.L1355:
	.word	.LANCHOR0
	.word	.LANCHOR0+2472
	.size	FtlReadRefresh, .-FtlReadRefresh
	.align	2
	.global	l2p_flush
	.syntax unified
	.arm
	.fpu softvfp
	.type	l2p_flush, %function
l2p_flush:
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 1, uses_anonymous_args = 0
	mov	ip, sp
	push	{r4, r5, r6, fp, ip, lr, pc}
	sub	fp, ip, #4
	ldr	r6, .L1361
	mov	r4, #0
	ldr	r5, .L1361+4
	bl	FtlWriteDump_data
.L1358:
	ldrh	r2, [r5]
	uxth	r3, r4
	cmp	r2, r3
	bhi	.L1360
	mov	r0, #0
	ldmfd	sp, {r4, r5, r6, fp, sp, pc}
.L1360:
	ldr	r2, [r6, #2544]
	uxth	r0, r4
	mov	r3, #12
	mla	r3, r3, r0, r2
	ldr	r3, [r3, #4]
	cmp	r3, #0
	bge	.L1359
	bl	flush_l2p_region
.L1359:
	add	r4, r4, #1
	b	.L1358
.L1362:
	.align	2
.L1361:
	.word	.LANCHOR0
	.word	.LANCHOR0+342
	.size	l2p_flush, .-l2p_flush
	.align	2
	.global	FtlVendorPartWrite
	.syntax unified
	.arm
	.fpu softvfp
	.type	FtlVendorPartWrite, %function
FtlVendorPartWrite:
	@ args = 0, pretend = 0, frame = 100
	@ frame_needed = 1, uses_anonymous_args = 0
	mov	ip, sp
	push	{r4, r5, r6, r7, r8, r9, r10, fp, ip, lr, pc}
	sub	fp, ip, #4
	sub	sp, sp, #100
	mov	r7, r0
	ldr	r4, .L1372
	mov	r5, r1
	str	r2, [fp, #-128]
	add	r2, r0, r1
	ldrh	r3, [r4]
	sub	r4, r4, #300
	cmp	r2, r3
	mvnhi	r8, #0
	bhi	.L1363
	add	r3, r4, #312
	mov	r8, #0
	ldrh	r6, [r3]
	lsr	r6, r0, r6
	lsl	r10, r6, #2
.L1365:
	cmp	r5, #0
	bne	.L1370
.L1363:
	mov	r0, r8
	sub	sp, fp, #40
	ldmfd	sp, {r4, r5, r6, r7, r8, r9, r10, fp, sp, pc}
.L1370:
	ldr	r3, [r4, #3376]
	mov	r0, r7
	ldr	ip, [r3, r10]
	ldr	r3, .L1372+4
	str	ip, [fp, #-140]
	ldrh	r2, [r3]
	mov	r1, r2
	str	r2, [fp, #-136]
	bl	__umodsi3
	ldr	r2, [fp, #-136]
	ldr	ip, [fp, #-140]
	str	r0, [fp, #-132]
	sub	r3, r2, r0
	uxth	r9, r3
	cmp	r5, r9
	uxthcc	r9, r5
	cmp	ip, #0
	cmpne	r9, r2
	movne	r1, #1
	moveq	r1, #0
	beq	.L1367
	ldr	r2, [r4, #3312]
	sub	r0, fp, #124
	str	ip, [fp, #-120]
	str	r2, [fp, #-116]
	sub	r2, fp, #104
	str	r2, [fp, #-112]
	mov	r2, #1
	mov	r1, r2
	bl	FlashReadPages
.L1368:
	ldr	r3, [fp, #-132]
	lsl	ip, r9, #9
	ldr	r0, [r4, #3312]
	sub	r5, r5, r9
	mov	r2, ip
	ldr	r1, [fp, #-128]
	str	ip, [fp, #-136]
	add	r7, r7, r9
	add	r10, r10, #4
	add	r0, r0, r3, lsl #9
	bl	ftl_memcpy
	mov	r1, r6
	ldr	r2, [r4, #3312]
	ldr	r0, .L1372+8
	add	r6, r6, #1
	bl	FtlMapWritePage
	ldr	r3, [fp, #-128]
	cmn	r0, #1
	ldr	ip, [fp, #-136]
	mvneq	r8, #0
	add	r3, r3, ip
	str	r3, [fp, #-128]
	b	.L1365
.L1367:
	ldr	r3, .L1372+12
	ldr	r0, [r4, #3312]
	ldrh	r2, [r3]
	bl	ftl_memset
	b	.L1368
.L1373:
	.align	2
.L1372:
	.word	.LANCHOR0+300
	.word	.LANCHOR0+262
	.word	.LANCHOR0+3612
	.word	.LANCHOR0+314
	.size	FtlVendorPartWrite, .-FtlVendorPartWrite
	.align	2
	.global	Ftl_save_ext_data
	.syntax unified
	.arm
	.fpu softvfp
	.type	Ftl_save_ext_data, %function
Ftl_save_ext_data:
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 1, uses_anonymous_args = 0
	mov	ip, sp
	push	{fp, ip, lr, pc}
	sub	fp, ip, #4
	ldr	r3, .L1376
	ldr	r2, .L1376+4
	ldr	r1, [r3, #2644]
	cmp	r1, r2
	ldmfdne	sp, {fp, sp, pc}
	ldr	r2, .L1376+8
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
	ldr	r2, .L1376+12
	bl	FtlVendorPartWrite
	ldmfd	sp, {fp, sp, pc}
.L1377:
	.align	2
.L1376:
	.word	.LANCHOR0
	.word	1179929683
	.word	1342177352
	.word	.LANCHOR0+2644
	.size	Ftl_save_ext_data, .-Ftl_save_ext_data
	.align	2
	.global	FtlEctTblFlush
	.syntax unified
	.arm
	.fpu softvfp
	.type	FtlEctTblFlush, %function
FtlEctTblFlush:
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 1, uses_anonymous_args = 0
	mov	ip, sp
	push	{fp, ip, lr, pc}
	sub	fp, ip, #4
	ldr	r3, .L1383
	movw	r1, #3656
	ldrh	r2, [r3, r1]
	cmp	r2, #31
	addls	r2, r2, #1
	movhi	r2, #32
	strhls	r2, [r3, r1]	@ movhi
	movls	r2, #1
	cmp	r0, #0
	bne	.L1380
	ldr	r1, [r3, #3352]
	ldr	r0, [r1, #20]
	ldr	r1, [r1, #16]
	add	r2, r2, r0
	cmp	r1, r2
	bcc	.L1381
.L1380:
	ldr	r2, [r3, #3352]
	mov	r0, #64
	ldr	r1, [r2, #16]
	str	r1, [r2, #20]
	ldr	r1, .L1383+4
	str	r1, [r2]
	ldr	r2, [r3, #3352]
	ldr	r3, .L1383+8
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
.L1381:
	mov	r0, #0
	ldmfd	sp, {fp, sp, pc}
.L1384:
	.align	2
.L1383:
	.word	.LANCHOR0
	.word	1112818501
	.word	.LANCHOR0+3344
	.size	FtlEctTblFlush, .-FtlEctTblFlush
	.align	2
	.global	FtlVendorPartRead
	.syntax unified
	.arm
	.fpu softvfp
	.type	FtlVendorPartRead, %function
FtlVendorPartRead:
	@ args = 0, pretend = 0, frame = 96
	@ frame_needed = 1, uses_anonymous_args = 0
	mov	ip, sp
	push	{r4, r5, r6, r7, r8, r9, r10, fp, ip, lr, pc}
	sub	fp, ip, #4
	sub	sp, sp, #96
	mov	r10, r2
	ldr	r5, .L1395
	add	r2, r0, r1
	mov	r8, r0
	mov	r7, r1
	ldrh	r3, [r5]
	sub	r5, r5, #300
	cmp	r2, r3
	mvnhi	r9, #0
	bhi	.L1385
	add	r3, r5, #312
	mov	r9, #0
	ldrh	r6, [r3]
	lsr	r6, r0, r6
	lsl	r3, r6, #2
	str	r3, [fp, #-128]
.L1387:
	cmp	r7, #0
	bne	.L1393
.L1385:
	mov	r0, r9
	sub	sp, fp, #40
	ldmfd	sp, {r4, r5, r6, r7, r8, r9, r10, fp, sp, pc}
.L1393:
	ldr	r2, [fp, #-128]
	mov	r0, r8
	ldr	r3, [r5, #3376]
	ldr	r3, [r3, r2]
	str	r3, [fp, #-136]
	ldr	r3, .L1395+4
	ldrh	r4, [r3]
	mov	r1, r4
	bl	__umodsi3
	sub	r4, r4, r0
	ldr	r3, [fp, #-136]
	uxth	r4, r4
	str	r0, [fp, #-132]
	cmp	r7, r4
	uxthcc	r4, r7
	cmp	r3, #0
	lsl	r2, r4, #9
	str	r2, [fp, #-136]
	beq	.L1389
	ldr	r2, [r5, #3312]
	sub	r0, fp, #124
	str	r3, [fp, #-120]
	str	r3, [fp, #-136]
	str	r2, [fp, #-116]
	sub	r2, fp, #104
	str	r2, [fp, #-112]
	mov	r2, #1
	mov	r1, r2
	bl	FlashReadPages
	ldr	r2, [fp, #-124]
	ldr	r3, [fp, #-136]
	cmn	r2, #1
	ldr	r2, [r5, #3456]
	mvneq	r9, #0
	cmp	r2, #256
	bne	.L1391
	mov	r2, r3
	mov	r1, r6
	ldr	r0, .L1395+8
	bl	sftl_printk
	ldr	r2, [r5, #3312]
	mov	r1, r6
	ldr	r0, .L1395+12
	bl	FtlMapWritePage
.L1391:
	ldr	r1, [r5, #3312]
	lsl	r2, r4, #9
	ldr	r3, [fp, #-132]
	mov	r0, r10
	add	r1, r1, r3, lsl #9
	bl	ftl_memcpy
.L1392:
	ldr	r3, [fp, #-128]
	add	r6, r6, #1
	sub	r7, r7, r4
	add	r8, r8, r4
	add	r10, r10, r4, lsl #9
	add	r3, r3, #4
	str	r3, [fp, #-128]
	b	.L1387
.L1389:
	lsl	r2, r4, #9
	mov	r1, r3
	mov	r0, r10
	bl	ftl_memset
	b	.L1392
.L1396:
	.align	2
.L1395:
	.word	.LANCHOR0+300
	.word	.LANCHOR0+262
	.word	.LC118
	.word	.LANCHOR0+3612
	.size	FtlVendorPartRead, .-FtlVendorPartRead
	.align	2
	.global	FtlLoadEctTbl
	.syntax unified
	.arm
	.fpu softvfp
	.type	FtlLoadEctTbl, %function
FtlLoadEctTbl:
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 1, uses_anonymous_args = 0
	mov	ip, sp
	push	{r4, r5, fp, ip, lr, pc}
	sub	fp, ip, #4
	ldr	r4, .L1399
	mov	r0, #64
	add	r5, r4, #3344
	ldr	r2, [r4, #3352]
	ldrh	r1, [r5]
	bl	FtlVendorPartRead
	ldr	r3, [r4, #3352]
	ldr	r2, [r3]
	ldr	r3, .L1399+4
	cmp	r2, r3
	beq	.L1398
	ldr	r1, .L1399+8
	ldr	r0, .L1399+12
	bl	sftl_printk
	ldrh	r2, [r5]
	mov	r1, #0
	ldr	r0, [r4, #3352]
	lsl	r2, r2, #9
	bl	ftl_memset
.L1398:
	mov	r0, #0
	ldmfd	sp, {r4, r5, fp, sp, pc}
.L1400:
	.align	2
.L1399:
	.word	.LANCHOR0
	.word	1112818501
	.word	.LC119
	.word	.LC77
	.size	FtlLoadEctTbl, .-FtlLoadEctTbl
	.align	2
	.global	Ftl_load_ext_data
	.syntax unified
	.arm
	.fpu softvfp
	.type	Ftl_load_ext_data, %function
Ftl_load_ext_data:
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 1, uses_anonymous_args = 0
	mov	ip, sp
	push	{r4, r5, fp, ip, lr, pc}
	sub	fp, ip, #4
	ldr	r4, .L1404
	mov	r1, #1
	mov	r0, #0
	ldr	r2, .L1404+4
	bl	FtlVendorPartRead
	ldr	r5, .L1404+8
	ldr	r3, [r4, #2644]
	cmp	r3, r5
	beq	.L1402
	mov	r1, #512
	ldr	r0, .L1404+4
	bl	__memzero
	str	r5, [r4, #2644]
.L1402:
	ldr	r3, [r4, #2644]
	cmp	r3, r5
	bne	.L1403
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
.L1403:
	ldr	r3, .L1404+12
	ldr	r0, [r4, #2600]
	ldrh	r1, [r4, #244]
	ldrh	r2, [r3]
	ldr	r3, [r4, #2604]
	mla	r0, r0, r2, r3
	bl	__udivsi3
	str	r0, [r4, #2608]
	ldmfd	sp, {r4, r5, fp, sp, pc}
.L1405:
	.align	2
.L1404:
	.word	.LANCHOR0
	.word	.LANCHOR0+2644
	.word	1179929683
	.word	.LANCHOR0+296
	.size	Ftl_load_ext_data, .-Ftl_load_ext_data
	.align	2
	.global	FtlMapBlkWriteDump_data
	.syntax unified
	.arm
	.fpu softvfp
	.type	FtlMapBlkWriteDump_data, %function
FtlMapBlkWriteDump_data:
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 1, uses_anonymous_args = 0
	mov	ip, sp
	push	{r4, r5, r6, fp, ip, lr, pc}
	sub	fp, ip, #4
	ldr	r3, [r0, #36]
	mov	r6, r0
	cmp	r3, #0
	ldmfdeq	sp, {r4, r5, r6, fp, sp, pc}
	mov	r3, #0
	ldrh	r5, [r0, #6]
	str	r3, [r0, #36]
	ldr	r3, .L1413
	ldr	r2, [r0, #24]
	sub	r5, r5, #1
	uxth	r5, r5
	ldr	r0, [r3, #3308]
	mov	r4, r3
	ldr	r1, [r3, #3336]
	str	r0, [r3, #3464]
	str	r1, [r3, #3468]
	ldr	r2, [r2, r5, lsl #2]
	cmp	r2, #0
	str	r2, [r3, #3460]
	beq	.L1408
	mov	r2, #1
	add	r0, r3, #3456
	mov	r1, r2
	bl	FlashReadPages
.L1409:
	ldr	r2, [r4, #3464]
	mov	r1, r5
	mov	r0, r6
	bl	FtlMapWritePage
	ldmfd	sp, {r4, r5, r6, fp, sp, pc}
.L1408:
	movw	r3, #314
	mov	r1, #255
	ldrh	r2, [r4, r3]
	bl	ftl_memset
	b	.L1409
.L1414:
	.align	2
.L1413:
	.word	.LANCHOR0
	.size	FtlMapBlkWriteDump_data, .-FtlMapBlkWriteDump_data
	.align	2
	.global	FtlVpcTblFlush
	.syntax unified
	.arm
	.fpu softvfp
	.type	FtlVpcTblFlush, %function
FtlVpcTblFlush:
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 1, uses_anonymous_args = 0
	mov	ip, sp
	push	{r4, r5, r6, r7, r8, r9, r10, fp, ip, lr, pc}
	sub	fp, ip, #4
	ldr	r4, .L1424
	mov	r5, #0
	mov	r1, #255
	ldr	r6, .L1424+4
	ldr	r3, [r4, #3304]
	add	r9, r4, #308
	ldr	r7, [r4, #3336]
	mov	r10, r9
	sub	r8, r6, #156
	str	r3, [r4, #3464]
	movw	r3, #2628
	ldrh	r3, [r4, r3]
	str	r7, [r4, #3468]
	str	r5, [r7, #12]
	strh	r3, [r7, #2]	@ movhi
	ldr	r3, .L1424+8
	strh	r3, [r7]	@ movhi
	ldr	r3, [r4, #2636]
	str	r5, [r7, #8]
	ldrh	r2, [r4, #30]
	str	r3, [r7, #4]
	ldr	r3, .L1424+12
	str	r3, [r4, #2472]
	ldr	r3, .L1424+16
	str	r3, [r4, #2476]
	ldrh	r3, [r6, #6]
	strh	r3, [r6, #-148]	@ movhi
	movw	r3, #258
	ldrh	r3, [r4, r3]
	strb	r3, [r4, #2482]
	ldrh	r3, [r4, #28]
	strh	r3, [r6, #-142]	@ movhi
	ldrb	r3, [r4, #34]	@ zero_extendqisi2
	orr	r3, r3, r2, lsl #6
	ldrh	r2, [r4, #82]
	strh	r3, [r6, #-140]	@ movhi
	ldrb	r3, [r4, #36]	@ zero_extendqisi2
	strb	r3, [r4, #2483]
	ldrh	r3, [r4, #80]
	strh	r3, [r6, #-138]	@ movhi
	ldrb	r3, [r4, #86]	@ zero_extendqisi2
	orr	r3, r3, r2, lsl #6
	strh	r3, [r6, #-136]	@ movhi
	ldrb	r3, [r4, #88]	@ zero_extendqisi2
	strb	r3, [r4, #2484]
	ldrh	r3, [r4, #128]
	strh	r3, [r6, #-134]	@ movhi
	ldrh	r2, [r4, #130]
	ldrb	r3, [r4, #134]	@ zero_extendqisi2
	ldr	r0, [r4, #3464]
	orr	r3, r3, r2, lsl #6
	strh	r3, [r6, #-132]	@ movhi
	ldrb	r3, [r4, #136]	@ zero_extendqisi2
	strb	r3, [r4, #2485]
	ldr	r3, [r4, #2600]
	str	r3, [r4, #2504]
	ldr	r3, [r4, #2592]
	str	r3, [r4, #2512]
	ldr	r3, [r4, #2596]
	str	r3, [r4, #2508]
	movw	r3, #314
	ldrh	r2, [r4, r3]
	bl	ftl_memset
	mov	r1, r8
	mov	r2, #48
	ldr	r0, [r4, #3464]
	movw	r8, #65535
	bl	ftl_memcpy
	ldrh	r2, [r4, #244]
	ldr	r0, [r4, #3464]
	ldr	r1, [r4, #76]
	lsl	r2, r2, #1
	add	r0, r0, #48
	bl	ftl_memcpy
	ldrh	r0, [r4, #244]
	ldr	r3, [r4, #3464]
	ldr	r1, [r4, #24]
	lsr	r2, r0, #3
	add	r0, r0, #24
	lsl	r0, r0, #1
	add	r2, r2, #4
	bic	r0, r0, #3
	add	r0, r3, r0
	bl	ftl_memcpy
	mov	r0, r5
	bl	FtlUpdateVaildLpn
.L1416:
	ldr	r3, [r4, #3304]
	ldrh	r1, [r6, #2]
	ldrh	r2, [r6]
	str	r3, [r4, #3464]
	ldr	r3, [r4, #3336]
	str	r3, [r4, #3468]
	orr	r3, r1, r2, lsl #10
	str	r3, [r4, #3460]
	ldrh	r3, [r9]
	sub	r3, r3, #1
	cmp	r1, r3
	blt	.L1417
	mov	r3, #0
	ldrh	r8, [r6, #4]
	strh	r3, [r6, #2]	@ movhi
	strh	r2, [r6, #4]	@ movhi
	bl	FtlFreeSysBlkQueueOut
	ldr	r3, [r4, #2592]
	strh	r0, [r6]	@ movhi
	add	r2, r3, #1
	str	r3, [r4, #2636]
	str	r2, [r4, #2592]
	lsl	r2, r0, #10
	str	r2, [r4, #3460]
	str	r3, [r7, #4]
	strh	r0, [r7, #2]	@ movhi
.L1417:
	ldr	r3, .L1424+20
	ldr	r0, [r4, #3304]
	ldrh	r1, [r3]
	bl	js_hash
	mov	r3, #1
	str	r0, [r7, #12]
	mov	r2, r3
	mov	r1, r3
	ldr	r0, .L1424+24
	bl	FlashProgPages
	ldrh	r3, [r6, #2]
	ldr	r2, [r4, #3456]
	add	r3, r3, #1
	uxth	r3, r3
	cmn	r2, #1
	strh	r3, [r6, #2]	@ movhi
	bne	.L1418
	cmp	r3, #1
	bne	.L1419
	movw	r2, #1138
	ldr	r1, .L1424+28
	ldr	r0, .L1424+32
	bl	sftl_printk
.L1419:
	ldrh	r3, [r6, #2]
	add	r5, r5, #1
	uxth	r5, r5
	cmp	r3, #1
	ldrheq	r3, [r10]
	subeq	r3, r3, #1
	strheq	r3, [r6, #2]	@ movhi
	cmp	r5, #3
	bls	.L1416
	mov	r2, r5
	ldr	r1, [r4, #3460]
	ldr	r0, .L1424+36
	bl	sftl_printk
.L1422:
	b	.L1422
.L1418:
	cmp	r2, #256
	cmpne	r3, #1
	beq	.L1416
	movw	r3, #65535
	cmp	r8, r3
	beq	.L1423
	mov	r1, #1
	mov	r0, r8
	bl	FtlFreeSysBlkQueueIn
.L1423:
	mov	r0, #0
	ldmfd	sp, {r4, r5, r6, r7, r8, r9, r10, fp, sp, pc}
.L1425:
	.align	2
.L1424:
	.word	.LANCHOR0
	.word	.LANCHOR0+2628
	.word	-3932
	.word	1179929683
	.word	1342177352
	.word	.LANCHOR0+314
	.word	.LANCHOR0+3456
	.word	.LANCHOR1+471
	.word	.LC8
	.word	.LC120
	.size	FtlVpcTblFlush, .-FtlVpcTblFlush
	.align	2
	.global	FtlSysFlush
	.syntax unified
	.arm
	.fpu softvfp
	.type	FtlSysFlush, %function
FtlSysFlush:
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 1, uses_anonymous_args = 0
	mov	ip, sp
	push	{fp, ip, lr, pc}
	sub	fp, ip, #4
	bl	l2p_flush
	mov	r0, #1
	bl	FtlEctTblFlush
	bl	FtlVpcTblFlush
	mov	r0, #0
	ldmfd	sp, {fp, sp, pc}
	.size	FtlSysFlush, .-FtlSysFlush
	.align	2
	.global	sftl_deinit
	.syntax unified
	.arm
	.fpu softvfp
	.type	sftl_deinit, %function
sftl_deinit:
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 1, uses_anonymous_args = 0
	mov	ip, sp
	push	{fp, ip, lr, pc}
	sub	fp, ip, #4
	ldr	r3, .L1429
	ldr	r3, [r3]
	cmp	r3, #1
	bne	.L1428
	bl	FtlSysFlush
.L1428:
	mov	r0, #0
	ldmfd	sp, {fp, sp, pc}
.L1430:
	.align	2
.L1429:
	.word	.LANCHOR2
	.size	sftl_deinit, .-sftl_deinit
	.align	2
	.global	sftl_discard
	.syntax unified
	.arm
	.fpu softvfp
	.type	sftl_discard, %function
sftl_discard:
	@ args = 0, pretend = 0, frame = 8
	@ frame_needed = 1, uses_anonymous_args = 0
	mov	ip, sp
	push	{r4, r5, r6, r7, r8, fp, ip, lr, pc}
	sub	fp, ip, #4
	sub	sp, sp, #8
	add	r2, r0, r1
	ldr	r4, .L1446
	mov	r8, r0
	mov	r6, r1
	ldr	r3, [r4, #344]
	cmp	r2, r3
	mvnhi	r0, #0
	bhi	.L1431
	cmp	r1, #31
	bhi	.L1433
.L1438:
	mov	r0, #0
.L1431:
	sub	sp, fp, #32
	ldmfd	sp, {r4, r5, r6, r7, r8, fp, sp, pc}
.L1433:
	movw	r3, #262
	ldrh	r5, [r4, r3]
	mov	r1, r5
	bl	__udivsi3
	smulbb	r3, r0, r5
	mov	r7, r0
	sub	r8, r8, r3
	uxth	r8, r8
	cmp	r8, #0
	beq	.L1434
	sub	r5, r5, r8
	add	r7, r0, #1
	cmp	r5, r6
	movcs	r5, r6
	uxth	r5, r5
	sub	r6, r6, r5
.L1434:
	ldr	r5, .L1446+4
	mvn	r3, #0
	str	r3, [fp, #-36]
	mov	r8, r5
.L1435:
	ldrh	r3, [r5]
	cmp	r6, r3
	bcs	.L1437
	ldr	r3, [r4, #3660]
	cmp	r3, #32
	bls	.L1438
	mov	r5, #0
	str	r5, [r4, #3660]
	bl	l2p_flush
	bl	FtlVpcTblFlush
	b	.L1438
.L1437:
	mov	r2, #0
	sub	r1, fp, #40
	mov	r0, r7
	bl	log2phys
	ldr	r3, [fp, #-40]
	cmn	r3, #1
	beq	.L1436
	ldr	r3, [r4, #3660]
	mov	r2, #1
	sub	r1, fp, #36
	mov	r0, r7
	add	r3, r3, #1
	str	r3, [r4, #3660]
	ldr	r3, [r4, #2564]
	add	r3, r3, #1
	str	r3, [r4, #2564]
	bl	log2phys
	ldr	r0, [fp, #-40]
	ubfx	r0, r0, #10, #16
	bl	P2V_block_in_plane
	bl	decrement_vpc_count
.L1436:
	ldrh	r3, [r8]
	add	r7, r7, #1
	sub	r6, r6, r3
	b	.L1435
.L1447:
	.align	2
.L1446:
	.word	.LANCHOR0
	.word	.LANCHOR0+262
	.size	sftl_discard, .-sftl_discard
	.align	2
	.global	FtlVpcCheckAndModify
	.syntax unified
	.arm
	.fpu softvfp
	.type	FtlVpcCheckAndModify, %function
FtlVpcCheckAndModify:
	@ args = 0, pretend = 0, frame = 4
	@ frame_needed = 1, uses_anonymous_args = 0
	mov	ip, sp
	push	{r4, r5, r6, r7, r8, r9, r10, fp, ip, lr, pc}
	sub	fp, ip, #4
	sub	sp, sp, #4
	mov	r5, #0
	ldr	r4, .L1461
	ldr	r1, .L1461+4
	ldr	r0, .L1461+8
	bl	sftl_printk
	ldrh	r2, [r4, #246]
	mov	r1, #0
	ldr	r0, [r4, #3356]
	lsl	r2, r2, #1
	bl	ftl_memset
.L1449:
	ldr	r3, [r4, #2556]
	cmp	r5, r3
	bcc	.L1451
	ldr	r10, .L1461+12
	mov	r7, #0
	movw	r9, #65535
.L1452:
	ldrh	r3, [r4, #244]
	uxth	r6, r7
	cmp	r3, r6
	bhi	.L1457
	bl	l2p_flush
	bl	FtlVpcTblFlush
	ldmib	sp, {r4, r5, r6, r7, r8, r9, r10, fp, sp, pc}
.L1451:
	mov	r2, #0
	sub	r1, fp, #44
	mov	r0, r5
	bl	log2phys
	ldr	r0, [fp, #-44]
	cmn	r0, #1
	beq	.L1450
	ubfx	r0, r0, #10, #16
	bl	P2V_block_in_plane
	ldr	r2, [r4, #3356]
	lsl	r0, r0, #1
	ldrh	r3, [r2, r0]
	add	r3, r3, #1
	strh	r3, [r2, r0]	@ movhi
.L1450:
	add	r5, r5, #1
	b	.L1449
.L1457:
	uxth	r8, r7
	ldr	r3, [r4, #76]
	lsl	r5, r8, #1
	ldrh	r2, [r3, r5]
	ldr	r3, [r4, #3356]
	ldrh	r3, [r3, r5]
	cmp	r2, r9
	cmpne	r2, r3
	beq	.L1454
	mov	r1, r8
	mov	r0, r10
	bl	sftl_printk
	ldrh	r3, [r4, #28]
	cmp	r3, r6
	beq	.L1454
	ldrh	r3, [r4, #128]
	cmp	r3, r6
	beq	.L1454
	ldrh	r3, [r4, #80]
	cmp	r3, r6
	beq	.L1454
	ldr	r3, [r4, #76]
	ldrh	r2, [r3, r5]
	cmp	r2, #0
	ldr	r2, [r4, #3356]
	ldrh	r2, [r2, r5]
	strh	r2, [r3, r5]	@ movhi
	bne	.L1456
.L1454:
	add	r7, r7, #1
	b	.L1452
.L1456:
	mov	r0, r8
	bl	update_vpc_list
	b	.L1454
.L1462:
	.align	2
.L1461:
	.word	.LANCHOR0
	.word	.LANCHOR1+486
	.word	.LC111
	.word	.LC121
	.size	FtlVpcCheckAndModify, .-FtlVpcCheckAndModify
	.align	2
	.global	allocate_new_data_superblock
	.syntax unified
	.arm
	.fpu softvfp
	.type	allocate_new_data_superblock, %function
allocate_new_data_superblock:
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 1, uses_anonymous_args = 0
	mov	ip, sp
	push	{r4, r5, r6, fp, ip, lr, pc}
	sub	fp, ip, #4
	ldr	r4, .L1473
	mov	r6, r0
	ldrh	r5, [r0]
	ldrh	r3, [r4, #244]
	cmp	r3, r5
	bcs	.L1464
	movw	r2, #2755
	ldr	r1, .L1473+4
	ldr	r0, .L1473+8
	bl	sftl_printk
.L1464:
	movw	r3, #65535
	cmp	r5, r3
	beq	.L1465
	ldr	r2, [r4, #76]
	lsl	r3, r5, #1
	mov	r0, r5
	ldrh	r3, [r2, r3]
	cmp	r3, #0
	beq	.L1466
	bl	INSERT_DATA_LIST
.L1465:
	mov	r3, #1
	strb	r3, [r6, #8]
	movw	r3, #3442
	ldrh	r0, [r4, r3]
	movw	r3, #65535
	cmp	r0, r3
	beq	.L1467
	cmp	r5, r0
	bne	.L1468
	ldr	r2, [r4, #76]
	lsl	r3, r0, #1
	ldrh	r3, [r2, r3]
	cmp	r3, #0
	beq	.L1469
.L1468:
	bl	update_vpc_list
.L1469:
	mvn	r2, #0
	movw	r3, #3442
	strh	r2, [r4, r3]	@ movhi
.L1467:
	mov	r0, r6
	bl	allocate_data_superblock
	bl	l2p_flush
	mov	r0, #0
	bl	FtlEctTblFlush
	bl	FtlVpcTblFlush
	mov	r0, #0
	ldmfd	sp, {r4, r5, r6, fp, sp, pc}
.L1466:
	bl	INSERT_FREE_LIST
	b	.L1465
.L1474:
	.align	2
.L1473:
	.word	.LANCHOR0
	.word	.LANCHOR1+507
	.word	.LC8
	.size	allocate_new_data_superblock, .-allocate_new_data_superblock
	.align	2
	.global	FtlProgPages
	.syntax unified
	.arm
	.fpu softvfp
	.type	FtlProgPages, %function
FtlProgPages:
	@ args = 0, pretend = 0, frame = 4
	@ frame_needed = 1, uses_anonymous_args = 0
	mov	ip, sp
	push	{r4, r5, r6, r7, r8, r9, r10, fp, ip, lr, pc}
	sub	fp, ip, #4
	sub	sp, sp, #4
	mov	r6, #0
	ldr	r8, .L1488
	mov	r5, r3
	mov	r2, #0
	ldrb	r3, [r3, #9]	@ zero_extendqisi2
	mov	r4, r0
	mov	r9, r1
	bl	FlashProgPages
.L1476:
	cmp	r6, r9
	beq	.L1482
	ldr	r7, .L1488+4
	b	.L1483
.L1478:
	ldr	r1, [r4, #4]
	mov	r0, r7
	bl	sftl_printk
	ldr	r0, [r4, #4]
	ubfx	r0, r0, #10, #16
	bl	P2V_block_in_plane
	bl	decrement_vpc_count
	ldrh	r3, [r5, #4]
	cmp	r3, #0
	bne	.L1477
	mov	r0, r5
	bl	allocate_new_data_superblock
.L1477:
	mov	r0, r5
	bl	get_new_active_ppa
	mov	r2, #0
	str	r0, [r4, #4]
	str	r0, [fp, #-44]
	mov	r1, #1
	ldrb	r3, [r5, #9]	@ zero_extendqisi2
	mov	r0, r4
	bl	FlashProgPages
.L1483:
	ldr	r2, [r4]
	cmn	r2, #1
	cmpne	r2, #256
	beq	.L1478
	ldrb	r2, [r5, #6]	@ zero_extendqisi2
	ldrh	r3, [r8, #236]
	cmp	r2, r3
	bcc	.L1479
	movw	r2, #985
	ldr	r1, .L1488+8
	ldr	r0, .L1488+12
	bl	sftl_printk
.L1479:
	ldr	r3, [r4, #4]
	sub	r1, fp, #40
	mov	r2, #1
	ldr	r0, [r4, #16]
	str	r3, [r1, #-4]!
	bl	log2phys
	ldr	r3, [r4, #12]
	ldr	r10, [r3, #12]
	ubfx	r0, r10, #10, #16
	bl	P2V_block_in_plane
	cmn	r10, #1
	mov	r7, r0
	beq	.L1480
	ldr	r2, [r8, #76]
	lsl	r3, r0, #1
	ldrh	r2, [r2, r3]
	cmp	r2, #0
	bne	.L1481
	mov	r1, r0
	ldr	r0, .L1488+16
	bl	sftl_printk
.L1481:
	mov	r0, r7
	bl	decrement_vpc_count
.L1480:
	add	r6, r6, #1
	add	r4, r4, #20
	b	.L1476
.L1482:
	ldr	r3, .L1488
	ldrb	r2, [r5, #6]	@ zero_extendqisi2
	ldrh	r3, [r3, #236]
	cmp	r2, r3
	ldmibcc	sp, {r4, r5, r6, r7, r8, r9, r10, fp, sp, pc}
	mov	r2, #1000
	ldr	r1, .L1488+8
	ldr	r0, .L1488+12
	bl	sftl_printk
	ldmib	sp, {r4, r5, r6, r7, r8, r9, r10, fp, sp, pc}
.L1489:
	.align	2
.L1488:
	.word	.LANCHOR0
	.word	.LC122
	.word	.LANCHOR1+536
	.word	.LC8
	.word	.LC123
	.size	FtlProgPages, .-FtlProgPages
	.align	2
	.global	FtlGcFreeTempBlock
	.syntax unified
	.arm
	.fpu softvfp
	.type	FtlGcFreeTempBlock, %function
FtlGcFreeTempBlock:
	@ args = 0, pretend = 0, frame = 4
	@ frame_needed = 1, uses_anonymous_args = 0
	mov	ip, sp
	push	{r4, r5, r6, r7, r8, r9, r10, fp, ip, lr, pc}
	sub	fp, ip, #4
	sub	sp, sp, #4
	movw	r3, #306
	ldr	r4, .L1514
	ldrh	r2, [r4, #128]
	ldrh	r1, [r4, r3]
	movw	r3, #65535
	cmp	r2, r3
	bne	.L1491
.L1498:
	ldrh	r2, [r4, #128]
	mov	r3, #0
	str	r3, [r4, #3448]
	movw	r3, #65535
	cmp	r2, r3
	bne	.L1511
.L1492:
	mov	r0, #0
	ldmib	sp, {r4, r5, r6, r7, r8, r9, r10, fp, sp, pc}
.L1491:
	cmp	r0, #0
	ldr	r5, .L1514+4
	beq	.L1494
	ldrh	r2, [r5, #4]
	cmp	r2, r3
	beq	.L1495
.L1496:
	mov	r1, #2
.L1494:
	ldr	r0, .L1514+8
	bl	FtlGcScanTempBlk
	ldrh	r2, [r5, #4]
	movw	r3, #65535
	str	r0, [fp, #-44]
	cmp	r2, r3
	beq	.L1498
	mov	r0, #1
	ldmib	sp, {r4, r5, r6, r7, r8, r9, r10, fp, sp, pc}
.L1495:
	mov	r3, #0
	strh	r3, [r5, #4]	@ movhi
	ldrh	r3, [r4, #228]
	cmp	r3, #17
	bhi	.L1496
	b	.L1494
.L1511:
	movw	r3, #3208
	ldrb	r1, [r4, #135]	@ zero_extendqisi2
	ldrh	r2, [r4, r3]
	movw	r3, #306
	ldrh	r3, [r4, r3]
	mul	r3, r3, r1
	cmp	r2, r3
	beq	.L1499
	mov	r2, #164
	ldr	r1, .L1514+12
	ldr	r0, .L1514+16
	bl	sftl_printk
.L1499:
	movw	r0, #306
	ldrb	r2, [r4, #135]	@ zero_extendqisi2
	ldrh	r0, [r4, r0]
	mov	r5, #0
	ldrh	r3, [r4, #128]
	mov	r9, #12
	ldr	r1, [r4, #76]
	ldr	r8, .L1514+20
	smulbb	r2, r2, r0
	lsl	r3, r3, #1
	strh	r2, [r1, r3]	@ movhi
	movw	r3, #3208
	ldr	r2, [r4, #2580]
	ldrh	r3, [r4, r3]
	add	r3, r3, r2
	str	r3, [r4, #2580]
.L1500:
	ldrh	r2, [r8]
	uxth	r3, r5
	cmp	r2, r3
	bhi	.L1504
	movw	r0, #65535
	bl	decrement_vpc_count
	ldrh	r0, [r4, #128]
	ldr	r2, [r4, #76]
	lsl	r3, r0, #1
	ldrh	r3, [r2, r3]
	cmp	r3, #0
	beq	.L1505
	bl	INSERT_DATA_LIST
.L1506:
	ldr	r6, .L1514+24
	movw	r3, #3208
	mvn	r7, #0
	mov	r5, #0
	strh	r5, [r4, r3]	@ movhi
	strh	r5, [r6]	@ movhi
	sub	r6, r6, #576
	strh	r7, [r4, #128]	@ movhi
	bl	l2p_flush
	bl	FtlVpcTblFlush
	ldrh	r3, [r6]
	ldrh	r2, [r4, #228]
	strh	r7, [r4, #176]	@ movhi
	add	r3, r3, r3, lsl #1
	cmp	r2, r3, asr #2
	movgt	r2, #20
	movwgt	r3, #3156
	strhgt	r2, [r4, r3]	@ movhi
	b	.L1492
.L1504:
	uxth	r7, r5
	ldr	r10, [r4, #3204]
	ldr	r3, [r4, #2556]
	mul	r7, r9, r7
	add	r6, r10, r7
	ldr	r0, [r6, #8]
	cmp	r0, r3
	bcc	.L1501
.L1512:
	ldrh	r0, [r4, #128]
	b	.L1513
.L1501:
	mov	r2, #0
	sub	r1, fp, #44
	bl	log2phys
	ldr	r3, [fp, #-44]
	ldr	r0, [r10, r7]
	cmp	r0, r3
	bne	.L1503
	ubfx	r0, r0, #10, #16
	bl	P2V_block_in_plane
	mov	r2, #1
	mov	r7, r0
	add	r1, r6, #4
	ldr	r0, [r6, #8]
	bl	log2phys
	mov	r0, r7
.L1513:
	bl	decrement_vpc_count
	b	.L1502
.L1503:
	ldr	r2, [r6, #4]
	cmp	r3, r2
	bne	.L1512
.L1502:
	add	r5, r5, #1
	b	.L1500
.L1505:
	bl	INSERT_FREE_LIST
	b	.L1506
.L1515:
	.align	2
.L1514:
	.word	.LANCHOR0
	.word	.LANCHOR2
	.word	.LANCHOR0+128
	.word	.LANCHOR1+549
	.word	.LC8
	.word	.LANCHOR0+3208
	.word	.LANCHOR0+3200
	.size	FtlGcFreeTempBlock, .-FtlGcFreeTempBlock
	.align	2
	.global	FtlGcPageRecovery
	.syntax unified
	.arm
	.fpu softvfp
	.type	FtlGcPageRecovery, %function
FtlGcPageRecovery:
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 1, uses_anonymous_args = 0
	mov	ip, sp
	push	{r4, r5, fp, ip, lr, pc}
	sub	fp, ip, #4
	ldr	r4, .L1518
	movw	r5, #306
	ldrh	r1, [r4, r5]
	add	r0, r4, #128
	bl	FtlGcScanTempBlk
	ldrh	r2, [r4, #130]
	ldrh	r3, [r4, r5]
	cmp	r2, r3
	ldmfdcc	sp, {r4, r5, fp, sp, pc}
	ldr	r0, .L1518+4
	bl	FtlMapBlkWriteDump_data
	mov	r0, #0
	bl	FtlGcFreeTempBlock
	mov	r3, #0
	str	r3, [r4, #3448]
	ldmfd	sp, {r4, r5, fp, sp, pc}
.L1519:
	.align	2
.L1518:
	.word	.LANCHOR0
	.word	.LANCHOR0+3396
	.size	FtlGcPageRecovery, .-FtlGcPageRecovery
	.align	2
	.global	FtlPowerLostRecovery
	.syntax unified
	.arm
	.fpu softvfp
	.type	FtlPowerLostRecovery, %function
FtlPowerLostRecovery:
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 1, uses_anonymous_args = 0
	mov	ip, sp
	push	{r4, r5, r6, fp, ip, lr, pc}
	sub	fp, ip, #4
	ldr	r4, .L1521
	mov	r5, #0
	add	r6, r4, #28
	str	r5, [r4, #3480]
	mov	r0, r6
	add	r4, r4, #80
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
	ldmfd	sp, {r4, r5, r6, fp, sp, pc}
.L1522:
	.align	2
.L1521:
	.word	.LANCHOR0
	.size	FtlPowerLostRecovery, .-FtlPowerLostRecovery
	.align	2
	.global	Ftl_gc_temp_data_write_back
	.syntax unified
	.arm
	.fpu softvfp
	.type	Ftl_gc_temp_data_write_back, %function
Ftl_gc_temp_data_write_back:
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 1, uses_anonymous_args = 0
	mov	ip, sp
	push	{r4, r5, r6, fp, ip, lr, pc}
	sub	fp, ip, #4
	ldr	r4, .L1531
	mov	r3, #0
	mov	r5, #0
	mov	r6, #20
	mov	r2, r3
	ldr	r1, [r4, #3172]
	ldr	r0, [r4, #3296]
	bl	FlashProgPages
.L1524:
	ldr	r1, [r4, #3172]
	uxth	r3, r5
	cmp	r1, r3
	bhi	.L1527
	ldr	r0, [r4, #3296]
	bl	FtlGcBufFree
	ldrh	r3, [r4, #132]
	mov	r0, #0
	str	r0, [r4, #3172]
	cmp	r3, r0
	ldmfdne	sp, {r4, r5, r6, fp, sp, pc}
	mov	r0, #1
	bl	FtlGcFreeTempBlock
	mov	r0, #1
	ldmfd	sp, {r4, r5, r6, fp, sp, pc}
.L1527:
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
	b	.L1524
.L1532:
	.align	2
.L1531:
	.word	.LANCHOR0
	.size	Ftl_gc_temp_data_write_back, .-Ftl_gc_temp_data_write_back
	.align	2
	.global	Ftl_get_new_temp_ppa
	.syntax unified
	.arm
	.fpu softvfp
	.type	Ftl_get_new_temp_ppa, %function
Ftl_get_new_temp_ppa:
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 1, uses_anonymous_args = 0
	mov	ip, sp
	push	{r4, r5, fp, ip, lr, pc}
	sub	fp, ip, #4
	ldr	r4, .L1536
	movw	r3, #65535
	ldrh	r2, [r4, #128]
	cmp	r2, r3
	beq	.L1534
	ldrh	r3, [r4, #132]
	cmp	r3, #0
	bne	.L1535
.L1534:
	mov	r0, #0
	mov	r5, #0
	bl	FtlGcFreeTempBlock
	ldr	r0, .L1536+4
	strb	r5, [r4, #136]
	bl	allocate_data_superblock
	ldr	r3, .L1536+8
	strh	r5, [r3]	@ movhi
	movw	r3, #3208
	strh	r5, [r4, r3]	@ movhi
	bl	l2p_flush
	mov	r0, r5
	bl	FtlEctTblFlush
	bl	FtlVpcTblFlush
.L1535:
	ldr	r0, .L1536+4
	bl	get_new_active_ppa
	ldmfd	sp, {r4, r5, fp, sp, pc}
.L1537:
	.align	2
.L1536:
	.word	.LANCHOR0
	.word	.LANCHOR0+128
	.word	.LANCHOR0+3200
	.size	Ftl_get_new_temp_ppa, .-Ftl_get_new_temp_ppa
	.align	2
	.global	rk_ftl_garbage_collect
	.syntax unified
	.arm
	.fpu softvfp
	.type	rk_ftl_garbage_collect, %function
rk_ftl_garbage_collect:
	@ args = 0, pretend = 0, frame = 32
	@ frame_needed = 1, uses_anonymous_args = 0
	mov	ip, sp
	push	{r4, r5, r6, r7, r8, r9, r10, fp, ip, lr, pc}
	sub	fp, ip, #4
	sub	sp, sp, #40
	ldr	r3, .L1667
	str	r0, [fp, #-64]
	ldr	r0, [r3, #3276]
	mov	r4, r3
	cmp	r0, #0
	movne	r0, #0
	bne	.L1538
	movw	r2, #2532
	ldrh	r2, [r3, r2]
	cmp	r2, #47
	bls	.L1538
	ldr	r2, .L1667+4
	ldrh	r1, [r2, #4]
	movw	r2, #65535
	cmp	r1, r2
	bne	.L1540
.L1543:
	ldrh	r3, [r4, #224]
	movw	r2, #65535
	cmp	r3, r2
	bne	.L1541
.L1542:
	bl	FtlReadRefresh
	ldr	r3, [r4, #3160]
	movw	r6, #65535
	ldr	r2, [fp, #-64]
	add	r3, r3, #1
	add	r3, r3, r2, lsl #7
	ldrh	r2, [r4, #176]
	str	r3, [r4, #3160]
	cmp	r2, r6
	bne	.L1544
	ldrh	r6, [r4, #128]
	cmp	r6, r2
	movne	r6, r2
	bne	.L1544
	ldrh	r5, [r4, #226]
	cmp	r5, r6
	bne	.L1544
	ldrh	r2, [r4, #228]
	cmp	r2, #24
	movcc	r2, #5120
	movcs	r2, #1024
	cmp	r3, r2
	movls	r6, r5
	bls	.L1544
	ldr	r3, .L1667+8
	mov	r8, #0
	str	r8, [r4, #3160]
	strh	r8, [r3]	@ movhi
	bl	GetSwlReplaceBlock
	cmp	r0, r5
	mov	r6, r0
	movne	r5, r0
	bne	.L1546
	movw	r7, #3158
	ldrh	r2, [r4, #228]
	ldrh	r3, [r4, r7]
	cmp	r2, r3
	bcs	.L1547
	mov	r0, #64
	bl	List_get_gc_head_node
	uxth	r3, r0
	cmp	r3, r6
	beq	.L1549
	mov	r0, r3
	ldr	r3, [r4, #76]
	lsl	r0, r0, #1
	ldrh	r3, [r3, r0]
	cmp	r3, #7
	bhi	.L1550
	mov	r0, r8
	bl	List_get_gc_head_node
	uxth	r5, r0
	mov	r3, #128
	strh	r3, [r4, r7]	@ movhi
	cmp	r5, r6
	bne	.L1546
.L1549:
	bl	FtlGcReFreshBadBlk
.L1544:
	movw	r1, #65535
	ldr	r2, [fp, #-64]
	sub	r3, r6, r1
	clz	r3, r3
	lsr	r3, r3, #5
	cmp	r2, #0
	movne	r2, #0
	andeq	r2, r3, #1
	cmp	r2, #0
	beq	.L1552
	ldrh	r3, [r4, #228]
	cmp	r3, #24
	movhi	r5, #1
	bhi	.L1553
	movw	r2, #306
	cmp	r3, #16
	ldrh	r5, [r4, r2]
	lsrhi	r5, r5, #5
	bhi	.L1553
	cmp	r3, #12
	lsrhi	r5, r5, #4
	bhi	.L1553
	cmp	r3, #8
	lsrhi	r5, r5, #2
.L1553:
	movw	r2, #3156
	ldrh	r1, [r4, r2]
	cmp	r1, r3
	bcs	.L1557
	ldrh	r3, [r4, #128]
	movw	r1, #65535
	cmp	r3, r1
	bne	.L1558
	ldrh	r1, [r4, #226]
	cmp	r1, r3
	bne	.L1558
	ldr	r3, .L1667+8
	ldrh	r0, [r3]
	cmp	r0, #0
	bne	.L1559
	ldr	r3, [r4, #2556]
	ldr	r1, [r4, #2552]
	add	r3, r3, r3, lsl #1
	cmp	r1, r3, lsr #2
	movcs	r3, #18
	bcs	.L1660
.L1559:
	ldr	r3, .L1667+12
	movw	r2, #3156
	ldrh	r3, [r3]
	add	r3, r3, r3, lsl #1
	asr	r3, r3, #2
.L1660:
	strh	r3, [r4, r2]	@ movhi
	mov	r3, #0
	str	r3, [r4, #3168]
.L1538:
	sub	sp, fp, #40
	ldmfd	sp, {r4, r5, r6, r7, r8, r9, r10, fp, sp, pc}
.L1540:
	ldrh	r3, [r3, #128]
	cmp	r3, r2
	beq	.L1543
	mov	r0, #1
	bl	FtlGcFreeTempBlock
	cmp	r0, #0
	beq	.L1543
	mov	r0, #1
	b	.L1538
.L1541:
	ldrh	r1, [r4, #226]
	cmp	r1, r2
	strheq	r3, [r4, #226]	@ movhi
	mvneq	r3, #0
	strheq	r3, [r4, #224]	@ movhi
	b	.L1542
.L1550:
	mov	r3, #64
.L1659:
	strh	r3, [r4, r7]	@ movhi
	b	.L1549
.L1547:
	mov	r3, #80
	b	.L1659
.L1546:
	movw	r0, #3156
	ldr	r3, [r4, #76]
	ldrh	r0, [r4, r0]
	lsl	r1, r5, #1
	ldrh	r2, [r4, #228]
	mov	r6, r5
	ldrh	r3, [r3, r1]
	str	r0, [sp, #4]
	ldr	r0, [r4, #2540]
	ldrh	r1, [r0, r1]
	ldr	r0, .L1667+16
	str	r1, [sp]
	mov	r1, r5
	bl	sftl_printk
	b	.L1549
.L1558:
	ldr	r3, .L1667+12
	movw	r2, #3156
	ldrh	r3, [r3]
	add	r3, r3, r3, lsl #1
	asr	r3, r3, #2
	strh	r3, [r4, r2]	@ movhi
.L1557:
	movw	r3, #3210
	movw	r6, #65535
	ldrh	r3, [r4, r3]
	cmp	r3, #0
	addne	r5, r5, #32
	uxthne	r5, r5
.L1563:
	ldrh	r3, [r4, #176]
	movw	r2, #65535
	cmp	r3, r2
	bne	.L1572
	cmp	r6, r3
	strhne	r6, [r4, #176]	@ movhi
	bne	.L1574
	ldrh	r3, [r4, #226]
	cmp	r3, r6
	beq	.L1574
	ldr	r2, [r4, #76]
	lsl	r3, r3, #1
	ldrh	r3, [r2, r3]
	cmp	r3, #0
	mvneq	r3, #0
	strheq	r3, [r4, #226]	@ movhi
	ldrh	r3, [r4, #226]
	strh	r3, [r4, #176]	@ movhi
	mvn	r3, #0
	strh	r3, [r4, #226]	@ movhi
.L1574:
	ldrh	r0, [r4, #176]
	mov	r3, #0
	strb	r3, [r4, #184]
	movw	r3, #65535
	cmp	r0, r3
	beq	.L1572
	bl	IsBlkInGcList
	cmp	r0, #0
	mvnne	r3, #0
	strhne	r3, [r4, #176]	@ movhi
	movw	r3, #65535
	ldrh	r2, [r4, #176]
	cmp	r2, r3
	beq	.L1572
	ldr	r0, .L1667+20
	bl	make_superblock
	mov	r3, #0
	movw	r2, #3666
	strh	r3, [r4, r2]	@ movhi
	strh	r3, [r4, #178]	@ movhi
	strb	r3, [r4, #182]
	ldrh	r3, [r4, #176]
	ldr	r2, [r4, #76]
	lsl	r3, r3, #1
	ldrh	r2, [r2, r3]
	movw	r3, #3668
	strh	r2, [r4, r3]	@ movhi
.L1572:
	ldrh	r3, [r4, #176]
	ldrh	r2, [r4, #28]
	cmp	r2, r3
	beq	.L1578
	ldrh	r2, [r4, #80]
	cmp	r2, r3
	beq	.L1578
	ldrh	r2, [r4, #128]
	cmp	r2, r3
	bne	.L1579
.L1578:
	mvn	r3, #0
	strh	r3, [r4, #176]	@ movhi
.L1579:
	ldr	r4, .L1667
	mov	r7, r4
.L1614:
	ldrh	r2, [r4, #176]
	movw	r3, #65535
	cmp	r2, r3
	bne	.L1580
	ldr	r10, .L1667+24
	mov	r3, #0
	str	r3, [r4, #3168]
	mov	r9, r10
.L1581:
	ldrh	r8, [r10]
	mov	r0, r8
	bl	List_get_gc_head_node
	uxth	r2, r0
	movw	r3, #65535
	cmp	r2, r3
	strh	r2, [r7, #176]	@ movhi
	bne	.L1582
	mov	r3, #0
	mov	r0, #8
	strh	r3, [r10]	@ movhi
	b	.L1538
.L1552:
	ldrh	r7, [r4, #128]
	cmp	r7, r1
	bne	.L1628
	ldrh	r1, [r4, #226]
	cmp	r1, r7
	movne	r3, #0
	andeq	r3, r3, #1
	cmp	r3, #0
	beq	.L1628
	ldrh	r3, [r4, #176]
	cmp	r3, r7
	beq	.L1564
.L1569:
	mov	r6, r7
.L1628:
	mov	r5, #1
	b	.L1563
.L1564:
	str	r2, [r4, #3168]
	movw	r2, #3156
	ldrh	r1, [r4, #228]
	ldrh	r3, [r4, r2]
	ldr	r5, .L1667+8
	cmp	r1, r3
	bls	.L1565
	ldrh	r3, [r5]
	cmp	r3, #0
	bne	.L1566
	ldr	r3, [r4, #2556]
	ldr	r1, [r4, #2552]
	add	r3, r3, r3, lsl #1
	cmp	r1, r3, lsr #2
	movcs	r3, #18
	bcs	.L1661
.L1566:
	ldr	r3, .L1667+12
	movw	r2, #3156
	ldrh	r3, [r3]
	add	r3, r3, r3, lsl #1
	asr	r3, r3, #2
.L1661:
	strh	r3, [r4, r2]	@ movhi
	bl	FtlReadRefresh
	mov	r0, #0
	bl	List_get_gc_head_node
	uxth	r0, r0
	ldr	r3, [r4, #76]
	lsl	r0, r0, #1
	ldrh	r3, [r3, r0]
	cmp	r3, #4
	bls	.L1565
.L1664:
	ldrh	r0, [r5]
	b	.L1538
.L1565:
	ldrh	r0, [r5]
	cmp	r0, #0
	bne	.L1569
	ldr	r3, .L1667+12
	movw	r2, #3156
	ldrh	r6, [r3]
	add	r3, r6, r6, lsl #1
	asr	r3, r3, #2
	strh	r3, [r4, r2]	@ movhi
	bl	List_get_gc_head_node
	uxth	r0, r0
	ldr	r3, [r4, #76]
	lsl	r0, r0, #1
	ldrh	r2, [r3, r0]
	ldr	r3, .L1667+28
	ldrh	r1, [r3]
	ldrh	r3, [r4, #236]
	mul	r3, r3, r1
	add	r3, r3, r3, lsr #31
	cmp	r2, r3, asr #1
	ble	.L1570
	ldrh	r3, [r4, #228]
	sub	r6, r6, #1
	cmp	r3, r6
	blt	.L1570
	bl	FtlReadRefresh
	b	.L1664
.L1570:
	cmp	r2, #0
	bne	.L1569
	movw	r0, #65535
	bl	decrement_vpc_count
	ldrh	r0, [r4, #228]
	add	r0, r0, #1
	b	.L1538
.L1582:
	uxth	r3, r0
	str	r2, [fp, #-56]
	add	r8, r8, #1
	mov	r0, r3
	str	r3, [fp, #-52]
	bl	IsBlkInGcList
	cmp	r0, #0
	ldr	r3, [fp, #-52]
	ldr	r2, [fp, #-56]
	strhne	r8, [r10]	@ movhi
	bne	.L1581
	lsl	r0, r3, #1
	ldr	r3, .L1667+32
	ldrh	lr, [r7, #236]
	uxth	r8, r8
	ldr	r1, [r7, #76]
	ldrh	r3, [r3]
	strh	r8, [r10]	@ movhi
	ldrh	ip, [r1, r0]
	mul	r3, lr, r3
	add	lr, r3, r3, lsr #31
	cmp	ip, lr, asr #1
	bgt	.L1585
	cmp	r8, #48
	cmphi	ip, #8
	bls	.L1586
	ldr	ip, .L1667+36
	ldrh	ip, [ip]
	cmp	ip, #35
	bhi	.L1586
.L1585:
	mov	ip, #0
	strh	ip, [r9]	@ movhi
.L1586:
	ldrh	r1, [r1, r0]
	movw	r0, #65535
	cmp	r3, r1
	cmple	r6, r0
	bne	.L1587
	ldrh	r3, [r9]
	cmp	r3, #3
	bhi	.L1587
	mvn	r3, #0
	strh	r3, [r7, #176]	@ movhi
	mov	r3, #0
	strh	r3, [r9]	@ movhi
.L1666:
	ldr	r3, .L1667+8
	b	.L1665
.L1587:
	cmp	r1, #0
	bne	.L1588
	movw	r0, #65535
	bl	decrement_vpc_count
	ldrh	r3, [r9]
	add	r3, r3, #1
	strh	r3, [r9]	@ movhi
	b	.L1581
.L1588:
	mov	r3, #0
	strb	r3, [r7, #184]
	ldrh	r3, [r7, #28]
	cmp	r3, r2
	bne	.L1589
	movw	r2, #717
	ldr	r1, .L1667+40
	ldr	r0, .L1667+44
	bl	sftl_printk
.L1589:
	ldrh	r2, [r7, #176]
	ldrh	r3, [r7, #80]
	cmp	r2, r3
	bne	.L1590
	movw	r2, #718
	ldr	r1, .L1667+40
	ldr	r0, .L1667+44
	bl	sftl_printk
.L1590:
	ldrh	r2, [r7, #176]
	ldrh	r3, [r7, #128]
	cmp	r2, r3
	bne	.L1591
	movw	r2, #719
	ldr	r1, .L1667+40
	ldr	r0, .L1667+44
	bl	sftl_printk
.L1591:
	ldr	r0, .L1667+20
	bl	make_superblock
	ldrh	r2, [r7, #176]
	mov	r3, #0
	ldr	r1, .L1667+48
	ldr	r0, [r7, #76]
	lsl	r2, r2, #1
	strh	r3, [r1]	@ movhi
	ldrh	r2, [r0, r2]
	strh	r3, [r7, #178]	@ movhi
	strb	r3, [r7, #182]
	strh	r2, [r1, #2]	@ movhi
.L1580:
	mov	r3, #1
	str	r3, [r7, #3276]
	ldr	r3, .L1667+32
	ldrh	r3, [r3]
	str	r3, [fp, #-52]
	ldr	r3, [fp, #-64]
	cmp	r3, #0
	beq	.L1592
	ldr	r2, [fp, #-52]
	ldrh	r3, [r7, #236]
	ldr	r1, [r7, #76]
	mul	r3, r2, r3
	ldrh	r2, [r7, #176]
	lsl	r2, r2, #1
	ldrh	r2, [r1, r2]
	sub	r3, r3, r2
	add	r2, r3, #3
	cmp	r3, #0
	movlt	r3, r2
	add	r5, r5, r3, asr #2
	uxth	r5, r5
.L1592:
	ldrh	r3, [r7, #178]
	ldr	r1, [fp, #-52]
	add	r2, r3, r5
	cmp	r2, r1
	movgt	r2, r1
	subgt	r5, r2, r3
	mov	r3, #0
	uxthgt	r5, r5
.L1663:
	str	r3, [fp, #-60]
	ldrh	r3, [fp, #-60]
	cmp	r5, r3
	bls	.L1602
	ldr	r3, [fp, #-60]
	mov	lr, #20
	ldrh	r1, [r4, #178]
	ldrh	r9, [r4, #236]
	ldr	r0, [r4, #3188]
	add	r1, r1, r3
	mov	r3, #0
	ldr	ip, .L1667+52
	mov	r8, r3
	b	.L1603
.L1596:
	ldrh	r2, [ip, #2]!
	movw	r10, #65535
	add	r3, r3, #1
	cmp	r2, r10
	mlane	r10, lr, r8, r0
	addne	r8, r8, #1
	orrne	r2, r1, r2, lsl #10
	uxthne	r8, r8
	strne	r2, [r10, #4]
.L1603:
	uxth	r2, r3
	cmp	r9, r2
	bhi	.L1596
	ldrb	r2, [r4, #184]	@ zero_extendqisi2
	mov	r1, r8
	bl	FlashReadPages
	mov	r3, #0
.L1662:
	str	r3, [fp, #-56]
	ldrh	r3, [fp, #-56]
	cmp	r8, r3
	ldrls	r3, [fp, #-60]
	addls	r3, r3, #1
	bls	.L1663
.L1601:
	ldr	r3, [fp, #-56]
	mov	r9, #20
	mul	r9, r9, r3
	ldr	r3, [r4, #3188]
	add	r2, r3, r9
	ldr	r3, [r3, r9]
	cmn	r3, #1
	beq	.L1598
	ldr	r10, [r2, #12]
	movw	r2, #61589
	ldrh	r1, [r10]
	cmp	r1, r2
	bne	.L1598
	ldr	ip, [r10, #8]
	cmn	ip, #1
	bne	.L1599
	movw	r2, #753
	ldr	r1, .L1667+40
	ldr	r0, .L1667+44
	str	ip, [fp, #-68]
	bl	sftl_printk
	ldr	ip, [fp, #-68]
.L1599:
	mov	r2, #0
	sub	r1, fp, #48
	mov	r0, ip
	bl	log2phys
	ldr	r2, [r4, #3188]
	ldr	r1, [fp, #-48]
	add	r2, r2, r9
	ldr	r0, [r2, #4]
	cmp	r0, r1
	bne	.L1598
	ldr	r3, .L1667+48
	mov	ip, #20
	ldr	r0, [r4, #3172]
	ldr	r2, [r2, #16]
	ldrh	r1, [r3]
	str	ip, [fp, #-72]
	add	r1, r1, #1
	strh	r1, [r3]	@ movhi
	ldr	r1, [r4, #3296]
	mla	r1, ip, r0, r1
	str	r2, [r1, #16]
	str	r1, [fp, #-68]
	bl	Ftl_get_new_temp_ppa
	ldr	r1, [fp, #-68]
	ldr	r2, [r4, #3296]
	ldr	ip, [fp, #-72]
	str	r0, [r1, #4]
	ldr	r1, [r4, #3172]
	mla	r2, ip, r1, r2
	ldr	r1, [r4, #3188]
	add	r1, r1, r9
	ldr	r0, [r1, #8]
	str	r0, [r2, #8]
	ldr	r1, [r1, #12]
	str	r1, [r2, #12]
	mov	r1, #1
	ldr	r2, [fp, #-48]
	str	r2, [r10, #12]
	ldrh	r2, [r4, #128]
	strh	r2, [r10, #2]	@ movhi
	ldr	r2, [r4, #2596]
	ldr	r3, [r4, #3172]
	ldr	r0, [r4, #3188]
	str	r2, [r10, #4]
	add	r3, r3, #1
	add	r0, r0, r9
	str	r3, [r4, #3172]
	bl	FtlGcBufAlloc
	ldrb	r2, [r4, #135]	@ zero_extendqisi2
	ldr	r3, [r4, #3172]
	cmp	r2, r3
	beq	.L1600
	ldrh	r3, [r4, #132]
	cmp	r3, #0
	bne	.L1598
.L1600:
	bl	Ftl_gc_temp_data_write_back
	cmp	r0, #0
	beq	.L1598
	ldr	r3, .L1667
	mov	r2, #0
	mvn	r1, #0
	str	r2, [r3, #3276]
	strh	r1, [r3, #176]	@ movhi
	strh	r2, [r3, #178]	@ movhi
	add	r3, r3, #3664
.L1665:
	ldrh	r0, [r3]
	b	.L1538
.L1598:
	ldr	r3, [fp, #-56]
	add	r3, r3, #1
	b	.L1662
.L1602:
	ldrh	r3, [r4, #178]
	add	r5, r5, r3
	ldr	r3, [fp, #-52]
	uxth	r5, r5
	cmp	r3, r5
	strh	r5, [r4, #178]	@ movhi
	bhi	.L1604
	ldr	r3, [r4, #3172]
	cmp	r3, #0
	beq	.L1605
	bl	Ftl_gc_temp_data_write_back
	cmp	r0, #0
	movne	r3, #0
	strne	r3, [r4, #3276]
	bne	.L1666
.L1605:
	ldr	r3, .L1667+48
	ldrh	r5, [r3]
	cmp	r5, #0
	bne	.L1606
	ldrh	r3, [r4, #176]
	ldr	r2, [r4, #76]
	lsl	r3, r3, #1
	ldrh	r3, [r2, r3]
	cmp	r3, #0
	beq	.L1606
.L1607:
	ldr	r3, [r4, #2556]
	cmp	r5, r3
	bcs	.L1612
	mov	r2, #0
	sub	r1, fp, #44
	mov	r0, r5
	bl	log2phys
	ldr	r0, [fp, #-44]
	cmn	r0, #1
	beq	.L1608
	ubfx	r0, r0, #10, #16
	bl	P2V_block_in_plane
	ldrh	r3, [r4, #176]
	cmp	r3, r0
	bne	.L1608
.L1612:
	ldr	r3, [r4, #2556]
	cmp	r5, r3
	bcc	.L1606
	ldrh	r3, [r4, #176]
	mov	r1, #0
	ldr	r2, [r4, #76]
	lsl	r3, r3, #1
	strh	r1, [r2, r3]	@ movhi
	ldrh	r0, [r4, #176]
	bl	update_vpc_list
	bl	l2p_flush
	bl	FtlVpcTblFlush
.L1606:
	mvn	r3, #0
	strh	r3, [r4, #176]	@ movhi
.L1604:
	mov	r3, #0
	str	r3, [r4, #3276]
	ldrh	r3, [r4, #228]
	cmp	r3, #2
	bhi	.L1613
	ldr	r3, .L1667+32
	ldrh	r5, [r3]
	b	.L1614
.L1608:
	add	r5, r5, #1
	b	.L1607
.L1613:
	ldr	r2, .L1667+8
	ldrh	r0, [r2]
	cmp	r0, #0
	addeq	r0, r3, #1
	b	.L1538
.L1668:
	.align	2
.L1667:
	.word	.LANCHOR0
	.word	.LANCHOR2
	.word	.LANCHOR0+3664
	.word	.LANCHOR0+2624
	.word	.LC124
	.word	.LANCHOR0+176
	.word	.LANCHOR0+3164
	.word	.LANCHOR0+308
	.word	.LANCHOR0+306
	.word	.LANCHOR0+3200
	.word	.LANCHOR1+568
	.word	.LC8
	.word	.LANCHOR0+3666
	.word	.LANCHOR0+190
	.size	rk_ftl_garbage_collect, .-rk_ftl_garbage_collect
	.align	2
	.global	FtlRead
	.syntax unified
	.arm
	.fpu softvfp
	.type	FtlRead, %function
FtlRead:
	@ args = 0, pretend = 0, frame = 52
	@ frame_needed = 1, uses_anonymous_args = 0
	mov	ip, sp
	push	{r4, r5, r6, r7, r8, r9, r10, fp, ip, lr, pc}
	sub	fp, ip, #4
	sub	sp, sp, #52
	uxtb	r0, r0
	mov	r6, r1
	str	r2, [fp, #-52]
	cmp	r0, #16
	mov	r9, r3
	bne	.L1670
	mov	r2, r3
	ldr	r1, [fp, #-52]
	add	r0, r6, #256
	bl	FtlVendorPartRead
	str	r0, [fp, #-48]
.L1669:
	ldr	r0, [fp, #-48]
	sub	sp, fp, #40
	ldmfd	sp, {r4, r5, r6, r7, r8, r9, r10, fp, sp, pc}
.L1670:
	ldr	r3, [fp, #-52]
	ldr	r10, .L1705
	add	r3, r1, r3
	str	r3, [fp, #-60]
	ldr	r2, [fp, #-60]
	ldr	r3, [r10, #344]
	cmp	r2, r3
	mvnhi	r3, #0
	strhi	r3, [fp, #-48]
	bhi	.L1669
	ldr	r3, .L1705+4
	ldr	r7, [r3]
	cmn	r7, #1
	streq	r7, [fp, #-48]
	beq	.L1669
	movw	r3, #262
	mov	r0, r6
	ldrh	r4, [r10, r3]
	mov	r8, #0
	mov	r5, r8
	mov	r7, r10
	mov	r1, r4
	bl	__udivsi3
	ldr	r3, [fp, #-60]
	mov	r1, r4
	str	r0, [fp, #-64]
	sub	r0, r3, #1
	bl	__udivsi3
	ldr	r3, [fp, #-64]
	ldr	r2, [fp, #-52]
	ldr	r4, [fp, #-64]
	rsb	r3, r3, #1
	str	r0, [fp, #-68]
	add	r3, r3, r0
	str	r8, [fp, #-76]
	str	r3, [fp, #-56]
	ldr	r3, [r10, #2588]
	str	r8, [fp, #-72]
	str	r8, [fp, #-48]
	add	r3, r3, r2
	ldr	r2, [fp, #-56]
	str	r3, [r10, #2588]
	ldr	r3, [r10, #2560]
	add	r3, r3, r2
	str	r3, [r10, #2560]
.L1672:
	ldr	r3, [fp, #-56]
	cmp	r3, #0
	bne	.L1689
	ldr	r3, .L1705
	movw	r2, #3210
	ldrh	r2, [r3, r2]
	cmp	r2, #0
	bne	.L1690
	ldrh	r3, [r3, #228]
	cmp	r3, #31
	bhi	.L1669
.L1690:
	mov	r1, #1
	mov	r0, #0
	bl	rk_ftl_garbage_collect
	b	.L1669
.L1689:
	mov	r2, #0
	sub	r1, fp, #44
	mov	r0, r4
	bl	log2phys
	ldr	r2, [fp, #-44]
	cmn	r2, #1
	moveq	r10, #0
	ldreq	r2, .L1705+8
	beq	.L1674
	ldr	r1, [r7, #3292]
	mov	r10, #20
	ldr	r3, [fp, #-64]
	mla	r10, r10, r5, r1
	cmp	r4, r3
	str	r2, [r10, #4]
	ldr	r2, .L1705+8
	bne	.L1678
	ldrh	r2, [r2]
	mov	r0, r6
	ldr	r1, [r7, #3316]
	str	r2, [fp, #-76]
	str	r1, [r10, #8]
	mov	r1, r2
	bl	__umodsi3
	ldr	r2, [fp, #-76]
	ldr	r3, [fp, #-52]
	str	r0, [fp, #-72]
	sub	r1, r2, r0
	cmp	r3, r1
	movcs	r3, r1
	cmp	r3, r2
	str	r3, [fp, #-76]
	streq	r9, [r10, #8]
.L1679:
	ldr	r3, .L1705+12
	ldr	r1, [r7, #3340]
	str	r4, [r10, #16]
	ldrh	r2, [r3]
	mul	r2, r5, r2
	add	r5, r5, #1
	bic	r2, r2, #3
	add	r2, r1, r2
	str	r2, [r10, #12]
	b	.L1677
.L1676:
	mla	r0, r0, r4, r10
	ldr	r1, [fp, #-60]
	cmp	r1, r0
	movhi	r1, #1
	movls	r1, #0
	cmp	r6, r0
	movhi	r1, #0
	cmp	r1, #0
	beq	.L1675
	sub	r0, r0, r6
	mov	r1, #512
	add	r0, r9, r0, lsl #9
	str	r2, [fp, #-80]
	bl	__memzero
	ldr	r2, [fp, #-80]
.L1675:
	add	r10, r10, #1
.L1674:
	ldrh	r0, [r2]
	cmp	r10, r0
	bcc	.L1676
.L1677:
	ldr	r3, [fp, #-56]
	add	r4, r4, #1
	subs	r3, r3, #1
	str	r3, [fp, #-56]
	beq	.L1681
	ldrh	r2, [r7, #236]
	cmp	r5, r2, lsl #2
	bne	.L1672
.L1681:
	cmp	r5, #0
	beq	.L1672
	mov	r2, #0
	mov	r1, r5
	ldr	r0, [r7, #3292]
	mov	r10, #0
	bl	FlashReadPages
	lsl	r3, r8, #9
	str	r3, [fp, #-88]
	ldr	r3, [fp, #-72]
	lsl	r3, r3, #9
	str	r3, [fp, #-80]
	ldr	r3, [fp, #-76]
	lsl	r3, r3, #9
	str	r3, [fp, #-84]
.L1688:
	mov	ip, #20
	ldr	r2, [r7, #3292]
	mul	ip, ip, r10
	ldr	r3, [fp, #-64]
	add	r2, r2, ip
	ldr	r1, [r2, #16]
	cmp	r3, r1
	bne	.L1683
	ldr	r1, [r2, #8]
	ldr	r2, [r7, #3316]
	cmp	r1, r2
	bne	.L1684
	ldr	r3, [fp, #-80]
	mov	r0, r9
	ldr	r2, [fp, #-84]
	str	ip, [fp, #-92]
	add	r1, r1, r3
.L1704:
	bl	ftl_memcpy
	ldr	ip, [fp, #-92]
.L1684:
	ldr	r1, [r7, #3292]
	add	r2, r1, ip
	ldr	r0, [r2, #12]
	ldr	lr, [r2, #16]
	ldr	r0, [r0, #8]
	cmp	lr, r0
	ldrne	r0, [r7, #2716]
	addne	r0, r0, #1
	strne	r0, [r7, #2716]
	ldr	lr, [r1, ip]
	cmn	lr, #1
	ldreq	r0, [r7, #2716]
	streq	lr, [fp, #-48]
	addeq	r0, r0, #1
	streq	r0, [r7, #2716]
	ldr	r1, [r1, ip]
	cmp	r1, #256
	bne	.L1687
	ldr	r0, [r2, #4]
	ubfx	r0, r0, #10, #16
	bl	P2V_block_in_plane
	bl	FtlGcRefreshBlock
.L1687:
	add	r10, r10, #1
	cmp	r5, r10
	bne	.L1688
	mov	r5, #0
	b	.L1672
.L1678:
	ldr	r3, [fp, #-68]
	cmp	r4, r3
	ldrhne	r2, [r2]
	mulne	r2, r4, r2
	bne	.L1703
	ldr	r1, [r7, #3320]
	ldr	r3, [fp, #-60]
	str	r1, [r10, #8]
	ldrh	r1, [r2]
	mul	r2, r1, r4
	sub	r8, r3, r2
	cmp	r1, r8
	bne	.L1679
.L1703:
	sub	r2, r2, r6
	add	r2, r9, r2, lsl #9
	str	r2, [r10, #8]
	b	.L1679
.L1683:
	ldr	r3, [fp, #-68]
	cmp	r3, r1
	bne	.L1684
	ldr	r1, [r2, #8]
	ldr	r2, [r7, #3320]
	cmp	r1, r2
	bne	.L1684
	ldr	r2, .L1705+8
	str	ip, [fp, #-92]
	ldrh	r0, [r2]
	ldr	r2, [fp, #-88]
	mul	r0, r3, r0
	sub	r0, r0, r6
	add	r0, r9, r0, lsl #9
	b	.L1704
.L1706:
	.align	2
.L1705:
	.word	.LANCHOR0
	.word	.LANCHOR2
	.word	.LANCHOR0+262
	.word	.LANCHOR0+316
	.size	FtlRead, .-FtlRead
	.align	2
	.global	sftl_read
	.syntax unified
	.arm
	.fpu softvfp
	.type	sftl_read, %function
sftl_read:
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 1, uses_anonymous_args = 0
	mov	ip, sp
	push	{fp, ip, lr, pc}
	sub	fp, ip, #4
	mov	r3, r2
	mov	r2, r1
	mov	r1, r0
	mov	r0, #0
	bl	FtlRead
	ldmfd	sp, {fp, sp, pc}
	.size	sftl_read, .-sftl_read
	.align	2
	.global	sftl_vendor_read
	.syntax unified
	.arm
	.fpu softvfp
	.type	sftl_vendor_read, %function
sftl_vendor_read:
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 1, uses_anonymous_args = 0
	mov	ip, sp
	push	{fp, ip, lr, pc}
	sub	fp, ip, #4
	mov	r3, r2
	mov	r2, r1
	mov	r1, r0
	mov	r0, #16
	bl	FtlRead
	ldmfd	sp, {fp, sp, pc}
	.size	sftl_vendor_read, .-sftl_vendor_read
	.align	2
	.global	FtlWrite
	.syntax unified
	.arm
	.fpu softvfp
	.type	FtlWrite, %function
FtlWrite:
	@ args = 0, pretend = 0, frame = 68
	@ frame_needed = 1, uses_anonymous_args = 0
	mov	ip, sp
	push	{r4, r5, r6, r7, r8, r9, r10, fp, ip, lr, pc}
	sub	fp, ip, #4
	sub	sp, sp, #68
	uxtb	r0, r0
	mov	r9, r1
	str	r2, [fp, #-80]
	cmp	r0, #16
	str	r3, [fp, #-84]
	bne	.L1710
	mov	r2, r3
	ldr	r1, [fp, #-80]
	add	r0, r9, #256
	bl	FtlVendorPartWrite
.L1709:
	sub	sp, fp, #40
	ldmfd	sp, {r4, r5, r6, r7, r8, r9, r10, fp, sp, pc}
.L1710:
	ldr	r3, [fp, #-80]
	ldr	r4, .L1761
	add	r6, r1, r3
	ldr	r3, [r4, #344]
	cmp	r6, r3
	mvnhi	r0, #0
	bhi	.L1709
	ldr	r5, .L1761+4
	ldr	r0, [r5]
	cmn	r0, #1
	beq	.L1709
	mov	r3, #2048
	mov	r0, r9
	str	r3, [r4, #3672]
	movw	r3, #262
	ldrh	r7, [r4, r3]
	mov	r1, r7
	bl	__udivsi3
	mov	r1, r7
	str	r0, [fp, #-76]
	sub	r0, r6, #1
	bl	__udivsi3
	ldr	r2, [fp, #-76]
	str	r0, [fp, #-92]
	sub	r3, r0, r2
	ldr	r2, [fp, #-80]
	str	r3, [fp, #-96]
	add	r3, r3, #1
	str	r3, [fp, #-72]
	ldr	r3, [r4, #2584]
	add	r3, r3, r2
	ldr	r2, [fp, #-72]
	str	r3, [r4, #2584]
	ldr	r3, [r4, #2568]
	add	r3, r3, r2
	str	r3, [r4, #2568]
	ldr	r3, [r5, #8]
	cmp	r3, #0
	addeq	r4, r4, #28
	beq	.L1712
	ldrh	r3, [r4, #32]
	cmp	r3, #0
	addne	r4, r4, #28
	addeq	r4, r4, #80
.L1712:
	ldr	r6, [fp, #-76]
	ldr	r10, .L1761
.L1713:
	ldr	r3, [fp, #-72]
	cmp	r3, #0
	bne	.L1741
	mov	r0, r3
	ldr	r1, [fp, #-96]
	bl	rk_ftl_garbage_collect
	ldrh	r3, [r10, #228]
	cmp	r3, #5
	movls	r4, #256
	ldrls	r5, .L1761+8
	bls	.L1745
.L1744:
	mov	r0, #0
	b	.L1709
.L1741:
	ldrb	r2, [r4, #6]	@ zero_extendqisi2
	ldrh	r3, [r10, #236]
	cmp	r2, r3
	bcc	.L1714
	movw	r2, #1041
	ldr	r1, .L1761+12
	ldr	r0, .L1761+16
	bl	sftl_printk
.L1714:
	ldrh	r2, [r4, #4]
	cmp	r2, #0
	bne	.L1715
	ldr	r3, .L1761+20
	ldr	r5, .L1761+4
	cmp	r4, r3
	bne	.L1716
	ldrh	r7, [r10, #84]
	cmp	r7, #0
	bne	.L1717
	add	r0, r4, #52
	bl	allocate_new_data_superblock
	str	r7, [r5, #8]
.L1717:
	ldr	r0, .L1761+20
	bl	allocate_new_data_superblock
	ldr	r4, .L1761+20
	ldr	r2, [r5, #8]
	add	r3, r4, #52
	cmp	r2, #0
	movne	r4, r3
.L1718:
	ldrh	r3, [r4, #4]
	cmp	r3, #0
	bne	.L1715
	mov	r0, r4
	bl	allocate_new_data_superblock
.L1715:
	ldrh	r3, [r4, #4]
	ldr	r2, [fp, #-72]
	cmp	r3, r2
	movcs	r3, r2
	ldrb	r2, [r4, #7]	@ zero_extendqisi2
	lsl	r2, r2, #2
	cmp	r3, r2
	movcs	r3, r2
	ldrb	r2, [r4, #6]	@ zero_extendqisi2
	str	r3, [fp, #-108]
	ldrh	r3, [r10, #236]
	cmp	r2, r3
	bcc	.L1719
	movw	r2, #1074
	ldr	r1, .L1761+12
	ldr	r0, .L1761+16
	bl	sftl_printk
.L1719:
	mov	r3, #0
.L1760:
	str	r3, [fp, #-68]
	ldr	r3, [fp, #-68]
	ldr	r2, [fp, #-108]
	cmp	r3, r2
	bne	.L1739
.L1721:
	mov	r3, r4
	mov	r2, #0
	ldr	r1, [fp, #-68]
	ldr	r0, [r10, #3300]
	bl	FtlProgPages
	ldr	r3, [fp, #-68]
	ldr	r2, [fp, #-72]
	cmp	r3, r2
	bls	.L1740
	mov	r2, #1152
	ldr	r1, .L1761+12
	ldr	r0, .L1761+16
	bl	sftl_printk
.L1740:
	ldr	r3, [fp, #-72]
	ldr	r2, [fp, #-68]
	sub	r3, r3, r2
	str	r3, [fp, #-72]
	b	.L1713
.L1716:
	str	r2, [r5, #8]
	ldrh	r2, [r10, #32]
	cmp	r2, #0
	movne	r4, r3
	bne	.L1715
	mov	r0, r4
	bl	allocate_new_data_superblock
	b	.L1718
.L1739:
	ldrh	r3, [r4, #4]
	cmp	r3, #0
	beq	.L1721
	mov	r2, #0
	sub	r1, fp, #64
	mov	r0, r6
	mov	r7, #20
	bl	log2phys
	mov	r0, r4
	bl	get_new_active_ppa
	ldr	r2, .L1761+24
	ldr	r1, [fp, #-68]
	ldr	r3, [fp, #-68]
	ldrh	r2, [r2]
	mul	r7, r7, r3
	ldr	r3, [r10, #3300]
	mul	r1, r2, r1
	add	r3, r3, r7
	str	r0, [r3, #4]
	bic	r1, r1, #3
	str	r6, [r3, #16]
	str	r1, [fp, #-100]
	ldr	r0, [fp, #-100]
	ldr	r1, [r10, #3340]
	add	r8, r1, r0
	str	r1, [fp, #-104]
	str	r8, [r3, #12]
	mov	r1, #0
	mov	r0, r8
	bl	ftl_memset
	ldr	r3, [fp, #-76]
	ldr	r2, [fp, #-92]
	cmp	r6, r2
	cmpne	r6, r3
	ldr	r3, .L1761+28
	bne	.L1722
	ldr	r2, [fp, #-76]
	cmp	r6, r2
	bne	.L1723
	ldrh	r5, [r3]
	mov	r0, r9
	mov	r1, r5
	bl	__umodsi3
	ldr	r3, [fp, #-80]
	sub	r5, r5, r0
	str	r0, [fp, #-88]
	cmp	r5, r3
	movcs	r5, r3
.L1724:
	ldr	r3, .L1761+28
	ldrh	r3, [r3]
	cmp	r5, r3
	ldr	r3, [fp, #-76]
	bne	.L1725
	cmp	r6, r3
	ldr	r3, [r10, #3300]
	add	r7, r3, r7
	ldreq	r3, [fp, #-84]
	beq	.L1757
	mul	r5, r5, r6
	ldr	r3, [fp, #-84]
	sub	r5, r5, r9
	add	r5, r3, r5, lsl #9
	str	r5, [r7, #8]
.L1727:
	ldrb	r2, [r4, #6]	@ zero_extendqisi2
	ldrh	r3, [r10, #236]
	cmp	r2, r3
	bcc	.L1736
	movw	r2, #1143
	ldr	r1, .L1761+12
	ldr	r0, .L1761+16
	bl	sftl_printk
.L1736:
	ldr	r3, .L1761+32
	ldr	r2, [fp, #-104]
	ldr	r1, [fp, #-100]
	strh	r3, [r2, r1]	@ movhi
	ldr	r3, [r10, #2596]
	str	r6, [r8, #8]
	add	r6, r6, #1
	str	r3, [r8, #4]
	add	r3, r3, #1
	cmn	r3, #1
	moveq	r3, #0
	str	r3, [r10, #2596]
	ldr	r3, [fp, #-64]
	str	r3, [r8, #12]
	ldrh	r3, [r4]
	strh	r3, [r8, #2]	@ movhi
	ldr	r3, [fp, #-68]
	add	r3, r3, #1
	b	.L1760
.L1723:
	ldr	r2, [fp, #-80]
	add	r5, r9, r2
	ldrh	r2, [r3]
	mov	r3, #0
	str	r3, [fp, #-88]
	smulbb	r2, r2, r6
	sub	r5, r5, r2
	uxth	r5, r5
	b	.L1724
.L1725:
	cmp	r6, r3
	ldr	r3, [r10, #3300]
	ldreq	r2, [r10, #3316]
	ldrne	r2, [r10, #3320]
	add	r3, r3, r7
	str	r2, [r3, #8]
	ldr	r3, [fp, #-64]
	cmn	r3, #1
	beq	.L1730
	str	r3, [fp, #-56]
	mov	r1, #1
	ldr	r3, [r10, #3300]
	sub	r0, fp, #60
	str	r6, [fp, #-44]
	add	r3, r3, r7
	ldr	r2, [r3, #8]
	ldr	r3, [r3, #12]
	str	r2, [fp, #-52]
	mov	r2, #0
	str	r3, [fp, #-48]
	bl	FlashReadPages
	ldr	r3, [fp, #-60]
	cmn	r3, #1
	bne	.L1731
	ldr	r2, [r10, #2716]
	ldr	r0, .L1761+36
	add	r2, r2, #1
	str	r2, [r10, #2716]
	mov	r2, r6
	ldr	r1, [r8, #8]
	bl	sftl_printk
.L1734:
	ldr	r3, [fp, #-76]
	lsl	r2, r5, #9
	cmp	r6, r3
	bne	.L1735
	ldr	r3, [r10, #3300]
	ldr	r1, [fp, #-84]
	add	r7, r3, r7
	ldr	r3, [fp, #-88]
	ldr	r0, [r7, #8]
	add	r0, r0, r3, lsl #9
.L1758:
	bl	ftl_memcpy
	b	.L1727
.L1731:
	ldr	r3, [r8, #8]
	cmp	r6, r3
	beq	.L1733
	ldr	r3, [r10, #2716]
	mov	r2, r6
	ldr	r0, .L1761+40
	add	r3, r3, #1
	str	r3, [r10, #2716]
	ldr	r1, [r8, #8]
	bl	sftl_printk
.L1733:
	ldr	r3, [r8, #8]
	cmp	r6, r3
	beq	.L1734
	movw	r2, #1128
	ldr	r1, .L1761+12
	ldr	r0, .L1761+16
	bl	sftl_printk
	b	.L1734
.L1730:
	ldr	r3, [r10, #3300]
	mov	r1, #0
	ldr	r2, .L1761+44
	add	r3, r3, r7
	ldrh	r2, [r2]
	ldr	r0, [r3, #8]
	bl	ftl_memset
	b	.L1734
.L1735:
	ldr	r3, .L1761+28
	ldrh	r1, [r3]
	ldr	r3, [r10, #3300]
	mul	r1, r6, r1
	add	r7, r3, r7
	ldr	r3, [fp, #-84]
	ldr	r0, [r7, #8]
	sub	r1, r1, r9
	add	r1, r3, r1, lsl #9
	b	.L1758
.L1722:
	ldrh	r3, [r3]
	ldr	r2, [r10, #3300]
	mul	r3, r6, r3
	add	r7, r2, r7
	ldr	r2, [fp, #-84]
	sub	r3, r3, r9
	add	r3, r2, r3, lsl #9
.L1757:
	str	r3, [r7, #8]
	b	.L1727
.L1745:
	ldrh	r3, [r10, #176]
	movw	r2, #65535
	cmp	r3, r2
	bne	.L1743
	ldrh	r2, [r10, #226]
	cmp	r2, r3
	bne	.L1743
	mov	r0, #0
	bl	List_get_gc_head_node
	uxth	r0, r0
	bl	FtlGcRefreshBlock
.L1743:
	ldr	r2, .L1761+48
	mov	r1, #1
	mov	r3, #128
	mov	r0, r1
	strh	r3, [r5]	@ movhi
	strh	r3, [r2]	@ movhi
	bl	rk_ftl_garbage_collect
	mov	r1, #1
	mov	r0, #0
	bl	rk_ftl_garbage_collect
	ldrh	r3, [r10, #228]
	cmp	r3, #2
	bhi	.L1744
	subs	r4, r4, #1
	bne	.L1745
	b	.L1744
.L1762:
	.align	2
.L1761:
	.word	.LANCHOR0
	.word	.LANCHOR2
	.word	.LANCHOR0+3158
	.word	.LANCHOR1+591
	.word	.LC8
	.word	.LANCHOR0+28
	.word	.LANCHOR0+316
	.word	.LANCHOR0+262
	.word	-3947
	.word	.LC125
	.word	.LC126
	.word	.LANCHOR0+314
	.word	.LANCHOR0+3156
	.size	FtlWrite, .-FtlWrite
	.align	2
	.global	sftl_vendor_write
	.syntax unified
	.arm
	.fpu softvfp
	.type	sftl_vendor_write, %function
sftl_vendor_write:
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 1, uses_anonymous_args = 0
	mov	ip, sp
	push	{fp, ip, lr, pc}
	sub	fp, ip, #4
	mov	r3, r2
	mov	r2, r1
	mov	r1, r0
	mov	r0, #16
	bl	FtlWrite
	ldmfd	sp, {fp, sp, pc}
	.size	sftl_vendor_write, .-sftl_vendor_write
	.align	2
	.global	sftl_gc
	.syntax unified
	.arm
	.fpu softvfp
	.type	sftl_gc, %function
sftl_gc:
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 1, uses_anonymous_args = 0
	mov	ip, sp
	push	{fp, ip, lr, pc}
	sub	fp, ip, #4
	mov	r1, #1
	mov	r0, r1
	bl	rk_ftl_garbage_collect
	ldmfd	sp, {fp, sp, pc}
	.size	sftl_gc, .-sftl_gc
	.align	2
	.global	FtlLoadSysInfo
	.syntax unified
	.arm
	.fpu softvfp
	.type	FtlLoadSysInfo, %function
FtlLoadSysInfo:
	@ args = 0, pretend = 0, frame = 4
	@ frame_needed = 1, uses_anonymous_args = 0
	mov	ip, sp
	push	{r4, r5, r6, r7, r8, r9, r10, fp, ip, lr, pc}
	sub	fp, ip, #4
	sub	sp, sp, #16
	mov	r1, #0
	ldr	r4, .L1795
	movw	r7, #2628
	ldr	r6, .L1795+4
	ldr	r3, [r4, #3304]
	ldrh	r2, [r4, #244]
	ldr	r0, [r4, #76]
	str	r3, [r4, #3464]
	ldr	r3, [r4, #3336]
	lsl	r2, r2, #1
	str	r3, [r4, #3468]
	bl	ftl_memset
	ldrh	r0, [r4, r7]
	movw	r3, #65535
	cmp	r0, r3
	bne	.L1766
.L1777:
	mvn	r0, #0
.L1765:
	sub	sp, fp, #40
	ldmfd	sp, {r4, r5, r6, r7, r8, r9, r10, fp, sp, pc}
.L1766:
	mov	r1, #1
	ldr	r9, .L1795+8
	bl	FtlGetLastWrittenPage
	ldrsh	r7, [r4, r7]
	add	r8, r4, #3456
	sxth	r5, r0
	add	r0, r0, #1
	strh	r0, [r6, #2]	@ movhi
.L1768:
	cmp	r5, #0
	bge	.L1774
	movw	r2, #1465
	ldr	r1, .L1795+12
	ldr	r0, .L1795+16
	bl	sftl_printk
.L1773:
	movw	r2, #314
	ldrh	r3, [r4, #244]
	ldrh	r2, [r4, r2]
	add	r3, r3, #24
	cmp	r2, r3, lsl #1
	bcs	.L1776
	movw	r2, #1467
	ldr	r1, .L1795+12
	ldr	r0, .L1795+16
	bl	sftl_printk
.L1776:
	ldr	r5, .L1795+20
	mov	r2, #48
	ldr	r1, [r4, #3464]
	mov	r0, r5
	bl	ftl_memcpy
	ldrh	r2, [r4, #244]
	ldr	r1, [r4, #3464]
	ldr	r0, [r4, #76]
	lsl	r2, r2, #1
	add	r1, r1, #48
	bl	ftl_memcpy
	ldrh	r1, [r4, #244]
	ldr	r3, [r4, #3464]
	ldr	r0, [r4, #24]
	lsr	r2, r1, #3
	add	r1, r1, #24
	lsl	r1, r1, #1
	add	r2, r2, #4
	bic	r1, r1, #3
	add	r1, r3, r1
	bl	ftl_memcpy
	ldr	r2, [r4, #2472]
	ldr	r3, .L1795+8
	cmp	r2, r3
	bne	.L1777
	movw	r3, #258
	ldrb	r2, [r4, #2482]	@ zero_extendqisi2
	ldrh	r3, [r4, r3]
	ldrh	r7, [r5, #8]
	cmp	r2, r3
	strh	r7, [r6, #6]	@ movhi
	bne	.L1777
	movw	r3, #306
	movw	r2, #262
	ldrh	r3, [r4, r3]
	ldrh	r2, [r4, r2]
	ldr	r6, [r4, #248]
	str	r7, [r4, #3676]
	mul	r3, r7, r3
	ldrh	r1, [r4, #236]
	str	r3, [r4, #2556]
	mul	r3, r3, r2
	str	r3, [r4, #344]
	ldr	r3, .L1795+24
	ldrh	r0, [r3, #6]
	sub	r0, r6, r0
	sub	r0, r0, r7
	bl	__udivsi3
	cmp	r7, r6
	strh	r0, [r5, #152]	@ movhi
	bls	.L1778
	movw	r2, #1489
	ldr	r1, .L1795+12
	ldr	r0, .L1795+16
	bl	sftl_printk
.L1778:
	ldrh	r3, [r5, #16]
	ldrh	r1, [r5, #14]
	lsr	r2, r3, #6
	and	r3, r3, #63
	strb	r3, [r4, #34]
	strh	r2, [r4, #30]	@ movhi
	ldrh	r2, [r5, #18]
	ldrb	r3, [r4, #2483]	@ zero_extendqisi2
	strh	r1, [r4, #28]	@ movhi
	strh	r2, [r4, #80]	@ movhi
	ldrh	r2, [r5, #20]
	strb	r3, [r4, #36]
	mvn	r3, #0
	strh	r3, [r4, #176]	@ movhi
	mov	r3, #0
	strh	r3, [r4, #178]	@ movhi
	lsr	r0, r2, #6
	and	r2, r2, #63
	strb	r2, [r4, #86]
	ldrb	r2, [r4, #2484]	@ zero_extendqisi2
	strh	r0, [r4, #82]	@ movhi
	strb	r3, [r4, #182]
	strb	r2, [r4, #88]
	ldrh	r2, [r5, #22]
	strb	r3, [r4, #184]
	str	r3, [r4, #2580]
	strh	r2, [r4, #128]	@ movhi
	ldrh	r2, [r5, #24]
	str	r3, [r4, #2568]
	str	r3, [r4, #2560]
	str	r3, [r4, #2576]
	lsr	r0, r2, #6
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
	strhi	r3, [r4, #2592]
	ldr	r3, [r4, #2508]
	cmp	r3, r2
	strhi	r3, [r4, #2596]
	movw	r3, #65535
	cmp	r1, r3
	beq	.L1781
	ldr	r0, .L1795+28
	bl	make_superblock
.L1781:
	ldrh	r2, [r4, #80]
	movw	r3, #65535
	cmp	r2, r3
	beq	.L1782
	ldr	r0, .L1795+32
	bl	make_superblock
.L1782:
	ldrh	r2, [r4, #128]
	movw	r3, #65535
	cmp	r2, r3
	beq	.L1783
	ldr	r0, .L1795+36
	bl	make_superblock
.L1783:
	ldrh	r2, [r4, #176]
	movw	r3, #65535
	cmp	r2, r3
	beq	.L1784
	ldr	r0, .L1795+40
	bl	make_superblock
.L1784:
	mov	r0, #0
	b	.L1765
.L1774:
	orr	r3, r5, r7, lsl #10
	mov	r2, #1
	mov	r1, r2
	str	r3, [r4, #3460]
	mov	r0, r8
	ldr	r3, [r4, #3304]
	str	r3, [r4, #3464]
	bl	FlashReadPages
	ldr	r3, [r4, #3468]
	ldr	r3, [r3, #12]
	cmp	r3, #0
	str	r3, [fp, #-44]
	beq	.L1769
	ldr	r2, [r4, #3456]
	cmn	r2, #1
	beq	.L1769
	ldr	r10, .L1795+44
	ldr	r0, [r4, #3464]
	ldrh	r1, [r10]
	bl	js_hash
	ldr	r3, [fp, #-44]
	cmp	r3, r0
	beq	.L1769
	str	r0, [sp, #8]
	mov	r2, r7
	str	r3, [sp, #4]
	ldrh	r3, [r6, #4]
	ldr	r1, .L1795+12
	ldr	r0, .L1795+48
	str	r3, [sp]
	mov	r3, r5
	bl	sftl_printk
	cmp	r5, #0
	bne	.L1770
	ldrh	r3, [r6, #4]
	cmp	r7, r3
	sxthne	r7, r3
	ldrhne	r5, [r10, #-6]
	bne	.L1772
.L1770:
	mvn	r3, #0
	str	r3, [r4, #3456]
.L1769:
	ldr	r3, [r4, #3456]
	cmn	r3, #1
	beq	.L1772
	ldr	r3, [r4, #3304]
	ldr	r3, [r3]
	cmp	r3, r9
	bne	.L1772
	ldr	r3, [r4, #3336]
	ldrh	r2, [r3]
	movw	r3, #61604
	cmp	r2, r3
	beq	.L1773
.L1772:
	sub	r5, r5, #1
	sxth	r5, r5
	b	.L1768
.L1796:
	.align	2
.L1795:
	.word	.LANCHOR0
	.word	.LANCHOR0+2628
	.word	1179929683
	.word	.LANCHOR1+600
	.word	.LC8
	.word	.LANCHOR0+2472
	.word	.LANCHOR0+356
	.word	.LANCHOR0+28
	.word	.LANCHOR0+80
	.word	.LANCHOR0+128
	.word	.LANCHOR0+176
	.word	.LANCHOR0+314
	.word	.LC127
	.size	FtlLoadSysInfo, .-FtlLoadSysInfo
	.align	2
	.global	FtlMapTblRecovery
	.syntax unified
	.arm
	.fpu softvfp
	.type	FtlMapTblRecovery, %function
FtlMapTblRecovery:
	@ args = 0, pretend = 0, frame = 28
	@ frame_needed = 1, uses_anonymous_args = 0
	mov	ip, sp
	push	{r4, r5, r6, r7, r8, r9, r10, fp, ip, lr, pc}
	sub	fp, ip, #4
	sub	sp, sp, #36
	mov	r1, #0
	ldr	r3, [r0, #24]
	mov	r4, r0
	ldr	r9, .L1838
	mov	r7, #0
	ldr	r8, [r0, #12]
	str	r3, [fp, #-44]
	ldr	r3, [r0, #16]
	str	r3, [fp, #-56]
	ldrh	r3, [r0, #6]
	str	r3, [fp, #-48]
	ldrh	r3, [r0, #8]
	ldr	r0, [fp, #-44]
	str	r3, [fp, #-52]
	ldr	r3, [fp, #-48]
	lsl	r2, r3, #2
	bl	ftl_memset
	ldr	r3, [r9, #3304]
	ldr	r5, [r9, #3336]
	str	r7, [r4, #32]
	str	r3, [r9, #3464]
	mvn	r3, #0
	str	r5, [r9, #3468]
	strh	r3, [r4]	@ movhi
	strh	r3, [r4, #2]	@ movhi
	mov	r3, #1
	str	r7, [r4, #28]
	str	r3, [r4, #36]
.L1798:
	ldr	r3, [fp, #-52]
	sxth	r6, r7
	cmp	r6, r3
	bge	.L1817
	ldr	r3, [fp, #-52]
	sub	r3, r3, #1
	cmp	r6, r3
	lsl	r3, r6, #1
	bne	.L1799
	add	r2, r8, r3
	ldrh	r0, [r8, r3]
	mov	r1, #1
	str	r2, [fp, #-52]
	bl	FtlGetLastWrittenPage
	sxth	r3, r0
	ldr	r9, .L1838
	add	r0, r0, #1
	strh	r7, [r4]	@ movhi
	str	r3, [fp, #-60]
	mov	r7, #0
	ldr	r3, [fp, #-56]
	add	r10, r9, #3456
	strh	r0, [r4, #2]	@ movhi
	ldr	r3, [r3, r6, lsl #2]
	str	r3, [r4, #28]
.L1800:
	ldr	r3, [fp, #-60]
	sxth	r8, r7
	add	r2, r3, #1
	cmp	r8, r2
	blt	.L1803
.L1817:
	mov	r0, r4
	bl	ftl_free_no_use_map_blk
	ldr	r3, .L1838+4
	ldrh	r2, [r4, #2]
	ldrh	r3, [r3]
	cmp	r2, r3
	bne	.L1805
	mov	r0, r4
	bl	ftl_map_blk_alloc_new_blk
.L1805:
	mov	r0, r4
	bl	ftl_map_blk_gc
	mov	r0, r4
	bl	ftl_map_blk_gc
	mov	r0, #0
	sub	sp, fp, #40
	ldmfd	sp, {r4, r5, r6, r7, r8, r9, r10, fp, sp, pc}
.L1803:
	ldr	r3, [fp, #-52]
	mov	r0, r10
	ldrh	r2, [r3]
	orr	r2, r8, r2, lsl #10
	str	r2, [r9, #3460]
	mov	r2, #1
	mov	r1, r2
	bl	FlashReadPages
	ldr	r2, [r9, #3468]
	ldr	r2, [r2, #12]
	cmp	r2, #0
	str	r2, [fp, #-56]
	beq	.L1801
	ldr	r1, [r9, #3456]
	cmn	r1, #1
	beq	.L1801
	ldr	r1, .L1838+8
	ldr	r0, [r9, #3464]
	ldrh	r1, [r1]
	bl	js_hash
	ldr	r2, [fp, #-56]
	cmp	r2, r0
	beq	.L1801
	str	r0, [sp, #4]
	mov	r3, r8
	str	r2, [sp]
	mov	r2, r6
	ldr	r1, .L1838+12
	ldr	r0, .L1838+16
	bl	sftl_printk
	mvn	r3, #0
	str	r3, [r9, #3456]
.L1801:
	ldr	r3, [r9, #3456]
	cmn	r3, #1
	beq	.L1802
	ldrh	r3, [r5, #8]
	ldr	r2, [fp, #-48]
	cmp	r2, r3
	bls	.L1802
	ldrh	r1, [r5]
	ldrh	r2, [r4, #4]
	cmp	r1, r2
	ldreq	r2, [r9, #3460]
	ldreq	r1, [fp, #-44]
	streq	r2, [r1, r3, lsl #2]
.L1802:
	add	r7, r7, #1
	b	.L1800
.L1799:
	ldr	r2, [r9, #3304]
	ldr	r0, .L1838+20
	str	r2, [r9, #3464]
	add	r2, r8, r3
	str	r2, [fp, #-60]
	ldrh	r2, [r8, r3]
	ldr	r3, .L1838+4
	ldrh	r3, [r3]
	sub	r3, r3, #1
	orr	r3, r3, r2, lsl #10
	mov	r2, #1
	mov	r1, r2
	str	r3, [r9, #3460]
	bl	FlashReadPages
	ldr	r3, [r9, #3456]
	cmn	r3, #1
	beq	.L1819
	ldrh	r2, [r5]
	ldrh	r3, [r4, #4]
	cmp	r2, r3
	bne	.L1819
	ldrh	r2, [r5, #8]
	movw	r3, #64245
	cmp	r2, r3
	beq	.L1807
.L1819:
	mov	r10, #0
.L1808:
	ldr	r2, .L1838+4
	sxth	r3, r10
	ldrh	r2, [r2]
	cmp	r3, r2
	bge	.L1815
	ldr	r2, [fp, #-60]
	ldr	r0, .L1838+20
	str	r3, [fp, #-68]
	ldrh	r2, [r2]
	orr	r2, r3, r2, lsl #10
	str	r2, [r9, #3460]
	mov	r2, #1
	mov	r1, r2
	bl	FlashReadPages
	ldr	r2, [r9, #3468]
	ldr	r2, [r2, #12]
	cmp	r2, #0
	str	r2, [fp, #-64]
	beq	.L1812
	ldr	r1, [r9, #3456]
	cmn	r1, #1
	beq	.L1812
	ldr	r1, .L1838+8
	ldr	r0, [r9, #3464]
	ldrh	r1, [r1]
	bl	js_hash
	ldr	r2, [fp, #-64]
	cmp	r2, r0
	beq	.L1812
	str	r0, [sp, #4]
	str	r2, [sp]
	mov	r2, r6
	ldr	r3, [fp, #-68]
	ldr	r1, .L1838+12
	ldr	r0, .L1838+24
	bl	sftl_printk
	mvn	r3, #0
	str	r3, [r9, #3456]
.L1812:
	ldr	r3, [r9, #3456]
	cmn	r3, #1
	beq	.L1813
	ldrh	r3, [r5, #8]
	ldr	r2, [fp, #-48]
	cmp	r2, r3
	bls	.L1813
	ldrh	r1, [r5]
	ldrh	r2, [r4, #4]
	cmp	r1, r2
	ldreq	r2, [r9, #3460]
	ldreq	r1, [fp, #-44]
	streq	r2, [r1, r3, lsl #2]
.L1813:
	add	r10, r10, #1
	b	.L1808
.L1807:
	ldr	ip, .L1838+4
	mov	r1, #0
	mov	lr, #4
.L1809:
	ldrh	r2, [ip]
	sxth	r3, r1
	sub	r2, r2, #1
	cmp	r3, r2
	blt	.L1811
.L1815:
	add	r7, r7, #1
	b	.L1798
.L1811:
	ldr	r0, [r9, #3304]
	add	r1, r1, #1
	ldr	r10, [fp, #-48]
	ldr	r2, [r0, r3, lsl #3]
	uxth	r6, r2
	cmp	r10, r6
	addhi	r3, lr, r3, lsl #3
	movhi	r2, r6
	ldrhi	r3, [r0, r3]
	ldrhi	r0, [fp, #-44]
	strhi	r3, [r0, r2, lsl #2]
	b	.L1809
.L1839:
	.align	2
.L1838:
	.word	.LANCHOR0
	.word	.LANCHOR0+308
	.word	.LANCHOR0+314
	.word	.LANCHOR1+615
	.word	.LC128
	.word	.LANCHOR0+3456
	.word	.LC129
	.size	FtlMapTblRecovery, .-FtlMapTblRecovery
	.align	2
	.global	FtlLoadVonderInfo
	.syntax unified
	.arm
	.fpu softvfp
	.type	FtlLoadVonderInfo, %function
FtlLoadVonderInfo:
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 1, uses_anonymous_args = 0
	mov	ip, sp
	push	{fp, ip, lr, pc}
	sub	fp, ip, #4
	ldr	r3, .L1841
	add	r2, r3, #324
	add	r0, r3, #3600
	ldrh	r2, [r2]
	add	r0, r0, #12
	strh	r2, [r0, #10]	@ movhi
	ldr	r2, .L1841+4
	strh	r2, [r0, #4]	@ movhi
	add	r2, r3, #348
	ldrh	r2, [r2]
	strh	r2, [r0, #8]	@ movhi
	movw	r2, #326
	ldrh	r2, [r3, r2]
	strh	r2, [r0, #6]	@ movhi
	ldr	r2, [r3, #352]
	str	r2, [r3, #3624]
	ldr	r2, [r3, #3372]
	str	r2, [r3, #3628]
	ldr	r2, [r3, #3368]
	str	r2, [r3, #3632]
	ldr	r2, [r3, #3376]
	str	r2, [r3, #3636]
	bl	FtlMapTblRecovery
	mov	r0, #0
	ldmfd	sp, {fp, sp, pc}
.L1842:
	.align	2
.L1841:
	.word	.LANCHOR0
	.word	-3962
	.size	FtlLoadVonderInfo, .-FtlLoadVonderInfo
	.align	2
	.global	FtlLoadMapInfo
	.syntax unified
	.arm
	.fpu softvfp
	.type	FtlLoadMapInfo, %function
FtlLoadMapInfo:
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 1, uses_anonymous_args = 0
	mov	ip, sp
	push	{fp, ip, lr, pc}
	sub	fp, ip, #4
	bl	FtlL2PDataInit
	ldr	r0, .L1844
	bl	FtlMapTblRecovery
	mov	r0, #0
	ldmfd	sp, {fp, sp, pc}
.L1845:
	.align	2
.L1844:
	.word	.LANCHOR0+3396
	.size	FtlLoadMapInfo, .-FtlLoadMapInfo
	.align	2
	.global	FtlSysBlkInit
	.syntax unified
	.arm
	.fpu softvfp
	.type	FtlSysBlkInit, %function
FtlSysBlkInit:
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 1, uses_anonymous_args = 0
	mov	ip, sp
	push	{r4, r5, r6, fp, ip, lr, pc}
	sub	fp, ip, #4
	ldr	r4, .L1862
	mov	r2, #0
	movw	r3, #3476
	ldrh	r0, [r4, #240]
	strh	r2, [r4, r3]	@ movhi
	bl	FtlFreeSysBlkQueueInit
	bl	FtlScanSysBlk
	movw	r3, #2628
	ldrh	r2, [r4, r3]
	movw	r3, #65535
	cmp	r2, r3
	bne	.L1847
.L1849:
	mvn	r6, #0
.L1846:
	mov	r0, r6
	ldmfd	sp, {r4, r5, r6, fp, sp, pc}
.L1847:
	bl	FtlLoadSysInfo
	subs	r6, r0, #0
	bne	.L1849
	bl	FtlLoadMapInfo
	bl	FtlLoadVonderInfo
	bl	Ftl_load_ext_data
	bl	FtlLoadEctTbl
	bl	FtlFreeSysBLkSort
	bl	SupperBlkListInit
	bl	FtlPowerLostRecovery
	mov	r0, #1
	bl	FtlUpdateVaildLpn
	ldr	r2, [r4, #2544]
	movw	r3, #342
	ldrh	r1, [r4, r3]
	mov	r0, #12
	mov	r3, r6
.L1850:
	cmp	r3, r1
	bge	.L1855
	mla	ip, r0, r3, r2
	ldr	ip, [ip, #4]
	cmp	ip, #0
	bge	.L1851
.L1855:
	ldr	r5, .L1862+4
	cmp	r3, r1
	ldrh	r2, [r5, #28]
	add	r2, r2, #1
	strh	r2, [r5, #28]	@ movhi
	bge	.L1861
.L1852:
	ldr	r0, .L1862+8
	bl	FtlSuperblockPowerLostFix
	ldr	r0, .L1862+12
	bl	FtlSuperblockPowerLostFix
	ldrh	r3, [r4, #28]
	ldr	r1, [r4, #76]
	ldrh	r0, [r4, #32]
	lsl	r3, r3, #1
	ldrh	r2, [r1, r3]
	sub	r2, r2, r0
	movw	r0, #306
	strh	r2, [r1, r3]	@ movhi
	ldrh	r2, [r4, #80]
	ldrh	r3, [r4, r0]
	ldr	ip, [r4, #76]
	ldrh	lr, [r4, #84]
	lsl	r2, r2, #1
	strh	r3, [r4, #30]	@ movhi
	mov	r3, #0
	strb	r3, [r4, #34]
	strh	r3, [r4, #32]	@ movhi
	ldrh	r1, [ip, r2]
	sub	r1, r1, lr
	strh	r1, [ip, r2]	@ movhi
	strb	r3, [r4, #86]
	strh	r3, [r4, #84]	@ movhi
	ldrh	r3, [r5, #30]
	ldrh	r2, [r4, r0]
	add	r3, r3, #1
	strh	r2, [r4, #82]	@ movhi
	strh	r3, [r5, #30]	@ movhi
	bl	l2p_flush
	bl	FtlVpcTblFlush
	bl	FtlVpcTblFlush
	b	.L1856
.L1851:
	add	r3, r3, #1
	b	.L1850
.L1861:
	movw	r3, #3476
	ldrh	r3, [r4, r3]
	cmp	r3, #0
	bne	.L1852
.L1856:
	ldrh	r0, [r4, #28]
	movw	r3, #65535
	cmp	r0, r3
	beq	.L1857
	ldrh	r3, [r4, #32]
	cmp	r3, #0
	bne	.L1857
	ldrh	r3, [r4, #84]
	cmp	r3, #0
	bne	.L1857
	bl	FtlGcRefreshBlock
	ldrh	r0, [r4, #80]
	bl	FtlGcRefreshBlock
	bl	FtlVpcTblFlush
	ldr	r0, .L1862+8
	bl	allocate_new_data_superblock
	ldr	r0, .L1862+12
	bl	allocate_new_data_superblock
.L1857:
	bl	FtlVpcCheckAndModify
	b	.L1846
.L1863:
	.align	2
.L1862:
	.word	.LANCHOR0
	.word	.LANCHOR0+2472
	.word	.LANCHOR0+28
	.word	.LANCHOR0+80
	.size	FtlSysBlkInit, .-FtlSysBlkInit
	.align	2
	.global	ftl_low_format
	.syntax unified
	.arm
	.fpu softvfp
	.type	ftl_low_format, %function
ftl_low_format:
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 1, uses_anonymous_args = 0
	mov	ip, sp
	push	{r4, r5, r6, r7, r8, r9, r10, fp, ip, lr, pc}
	sub	fp, ip, #4
	ldr	r4, .L1888
	mov	r3, #0
	ldrh	r0, [r4, #240]
	str	r3, [r4, #2592]
	str	r3, [r4, #2596]
	str	r3, [r4, #2608]
	bl	FtlFreeSysBlkQueueInit
	bl	FtlLoadBbt
	cmp	r0, #0
	beq	.L1865
	bl	FtlMakeBbt
.L1865:
	ldr	r0, .L1888+4
	mov	r2, #0
	ldr	ip, .L1888+8
.L1866:
	ldrh	r1, [r0]
	uxth	r3, r2
	add	r2, r2, #1
	cmp	r3, r1, lsl #7
	blt	.L1867
	ldrh	r6, [r4, #244]
	mov	r5, #0
.L1868:
	ldrh	r3, [r4, #246]
	cmp	r3, r6
	bhi	.L1869
	ldrh	r1, [r4, #236]
	sub	r3, r5, #3
	cmp	r3, r1, lsl #1
	bge	.L1870
.L1874:
	mov	r0, #0
	mov	r7, r0
.L1871:
	ldrh	r2, [r4, #244]
	uxth	r3, r0
	add	r5, r0, #1
	cmp	r2, r3
	bhi	.L1875
	ldrh	r3, [r4, #246]
	ldrh	r5, [r4, #236]
	ldr	r10, [r4, #248]
	str	r3, [r4, #3288]
	mov	r1, r5
	ldr	r6, .L1888+12
	mov	r0, r10
	bl	__udivsi3
	ubfx	r8, r0, #5, #16
	mov	r9, r0
	add	r3, r8, #36
	str	r0, [r4, #2556]
	strh	r3, [r6]	@ movhi
	mov	r3, #24
	mul	r3, r3, r5
	cmp	r7, r3
	ble	.L1876
	mov	r1, r5
	sub	r0, r10, r7
	bl	__udivsi3
	str	r0, [r4, #2556]
	lsr	r0, r0, #5
	add	r0, r0, #24
	strh	r0, [r6]	@ movhi
.L1876:
	movw	r3, #298
	ldrh	r3, [r4, r3]
	cmp	r3, #0
	beq	.L1878
	ldrh	r2, [r6]
	add	r2, r2, r3, lsr #1
	strh	r2, [r6]	@ movhi
	mul	r2, r5, r3
	cmp	r7, r2
	addlt	r3, r3, #32
	strlt	r9, [r4, #2556]
	addlt	r3, r8, r3
	strhlt	r3, [r6]	@ movhi
.L1878:
	ldrh	r2, [r6]
	ldr	r3, [r4, #2556]
	ldr	r6, .L1888+16
	sub	r3, r3, r2
	mul	r5, r5, r3
	movw	r3, #306
	ldrh	r3, [r4, r3]
	str	r5, [r4, #3676]
	mul	r5, r5, r3
	movw	r3, #262
	ldrh	r3, [r4, r3]
	str	r5, [r4, #2556]
	mul	r5, r5, r3
	str	r5, [r4, #344]
	mvn	r5, #0
	bl	FtlBbmTblFlush
	ldrh	r2, [r4, #246]
	mov	r1, #0
	ldr	r0, [r4, #76]
	lsl	r2, r2, #1
	bl	ftl_memset
	ldrh	r2, [r4, #244]
	mov	r3, #0
	str	r3, [r4, #2552]
	mov	r1, #255
	strh	r3, [r4, #178]	@ movhi
	strb	r3, [r4, #182]
	strb	r3, [r4, #184]
	lsr	r2, r2, #3
	strh	r3, [r4, #30]	@ movhi
	strb	r3, [r4, #34]
	strh	r3, [r4, #28]	@ movhi
	mov	r3, #1
	ldr	r0, [r4, #24]
	strh	r5, [r4, #176]	@ movhi
	strb	r3, [r4, #36]
	bl	ftl_memset
.L1880:
	mov	r0, r6
	bl	make_superblock
	ldrb	r3, [r4, #35]	@ zero_extendqisi2
	cmp	r3, #0
	ldrh	r3, [r4, #28]
	bne	.L1881
	ldr	r2, [r4, #76]
	lsl	r3, r3, #1
	strh	r5, [r2, r3]	@ movhi
	ldrh	r3, [r4, #28]
	add	r3, r3, #1
	strh	r3, [r4, #28]	@ movhi
	b	.L1880
.L1867:
	ldr	lr, [r4, #3316]
	mvn	r1, r3
	orr	r1, r3, r1, lsl #16
	str	r1, [lr, r3, lsl #2]
	ldr	r1, [r4, #3320]
	str	ip, [r1, r3, lsl #2]
	b	.L1866
.L1869:
	mov	r0, r6
	mov	r1, #1
	bl	FtlLowFormatEraseBlock
	add	r6, r6, #1
	add	r5, r5, r0
	uxth	r5, r5
	uxth	r6, r6
	b	.L1868
.L1870:
	mov	r0, r5
	bl	__udivsi3
	ldr	r3, [r4, #336]
	add	r0, r0, r3
	uxth	r0, r0
	bl	FtlSysBlkNumInit
	ldrh	r0, [r4, #240]
	bl	FtlFreeSysBlkQueueInit
	ldrh	r5, [r4, #244]
.L1872:
	ldrh	r3, [r4, #246]
	cmp	r3, r5
	bls	.L1874
	mov	r0, r5
	mov	r1, #1
	add	r5, r5, #1
	bl	FtlLowFormatEraseBlock
	uxth	r5, r5
	b	.L1872
.L1875:
	mov	r1, #0
	uxth	r0, r0
	bl	FtlLowFormatEraseBlock
	add	r7, r7, r0
	mov	r0, r5
	uxth	r7, r7
	b	.L1871
.L1881:
	ldr	r2, [r4, #2592]
	lsl	r3, r3, #1
	ldrh	r1, [r4, #32]
	mvn	r5, #0
	ldr	r6, .L1888+20
	str	r2, [r4, #40]
	add	r2, r2, #1
	str	r2, [r4, #2592]
	ldr	r2, [r4, #76]
	strh	r1, [r2, r3]	@ movhi
	mov	r3, #0
	strh	r3, [r4, #82]	@ movhi
	strb	r3, [r4, #86]
	ldrh	r3, [r4, #28]
	add	r3, r3, #1
	strh	r3, [r4, #80]	@ movhi
	mov	r3, #1
	strb	r3, [r4, #88]
.L1882:
	mov	r0, r6
	bl	make_superblock
	ldrb	r3, [r4, #87]	@ zero_extendqisi2
	cmp	r3, #0
	ldrh	r3, [r4, #80]
	bne	.L1883
	ldr	r2, [r4, #76]
	lsl	r3, r3, #1
	strh	r5, [r2, r3]	@ movhi
	ldrh	r3, [r4, #80]
	add	r3, r3, #1
	strh	r3, [r4, #80]	@ movhi
	b	.L1882
.L1883:
	ldr	r2, [r4, #2592]
	lsl	r3, r3, #1
	ldrh	r1, [r4, #84]
	mvn	r5, #0
	str	r2, [r4, #92]
	add	r2, r2, #1
	str	r2, [r4, #2592]
	ldr	r2, [r4, #76]
	strh	r1, [r2, r3]	@ movhi
	strh	r5, [r4, #128]	@ movhi
	bl	FtlFreeSysBlkQueueOut
	ldr	r3, .L1888+24
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
	ldreq	r3, .L1888+28
	moveq	r2, #1
	streq	r2, [r3]
	ldmfd	sp, {r4, r5, r6, r7, r8, r9, r10, fp, sp, pc}
.L1889:
	.align	2
.L1888:
	.word	.LANCHOR0
	.word	.LANCHOR0+262
	.word	168778952
	.word	.LANCHOR0+2624
	.word	.LANCHOR0+28
	.word	.LANCHOR0+80
	.word	.LANCHOR0+2628
	.word	.LANCHOR2
	.size	ftl_low_format, .-ftl_low_format
	.align	2
	.global	sftl_init
	.syntax unified
	.arm
	.fpu softvfp
	.type	sftl_init, %function
sftl_init:
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 1, uses_anonymous_args = 0
	mov	ip, sp
	push	{r4, r5, fp, ip, lr, pc}
	sub	fp, ip, #4
	ldr	r4, .L1896
	mvn	r3, #0
	ldr	r5, .L1896+4
	ldr	r1, .L1896+8
	ldr	r0, .L1896+12
	str	r3, [r5]
	bl	sftl_printk
	mov	r0, r4
	bl	FtlConstantsInit
	bl	FtlMemInit
	bl	FtlVariablesInit
	ldrh	r0, [r4, #240]
	bl	FtlFreeSysBlkQueueInit
	bl	FtlLoadBbt
	cmp	r0, #0
	bne	.L1894
	bl	FtlSysBlkInit
	cmp	r0, #0
	bne	.L1894
	mov	r3, #1
	str	r3, [r5]
	ldrh	r3, [r4, #228]
	cmp	r3, #15
	bhi	.L1894
	movw	r4, #8129
.L1893:
	mov	r1, #1
	mov	r0, #0
	bl	rk_ftl_garbage_collect
	subs	r4, r4, #1
	bne	.L1893
.L1894:
	mov	r0, #0
	ldmfd	sp, {r4, r5, fp, sp, pc}
.L1897:
	.align	2
.L1896:
	.word	.LANCHOR0
	.word	.LANCHOR2
	.word	.LC0
	.word	.LC77
	.size	sftl_init, .-sftl_init
	.align	2
	.global	FtlWriteToIDB
	.syntax unified
	.arm
	.fpu softvfp
	.type	FtlWriteToIDB, %function
FtlWriteToIDB:
	@ args = 0, pretend = 0, frame = 100
	@ frame_needed = 1, uses_anonymous_args = 0
	mov	ip, sp
	push	{r4, r5, r6, r7, r8, r9, r10, fp, ip, lr, pc}
	sub	fp, ip, #4
	sub	sp, sp, #104
	add	r8, r1, r0
	sub	r9, r8, #1
	mov	r5, r0
	cmp	r9, #63
	mov	r4, r1
	movhi	r3, #0
	movls	r3, #1
	cmp	r0, #576
	orrcs	r3, r3, #1
	mov	r7, r2
	cmp	r3, #0
	ldr	r6, .L1960
	beq	.L1899
	ldr	r3, [r6, #3680]
	cmp	r3, #0
	bne	.L1900
.L1959:
	mov	r4, #0
	b	.L1898
.L1900:
	ldr	r3, [r6, #3684]
	ldr	r2, [r3]
	str	r3, [fp, #-112]
	ldr	r3, .L1960+4
	cmp	r2, r3
	bne	.L1902
	ldr	r3, [fp, #-112]
	mov	r2, #0
	ldrh	r1, [r6, #10]
	add	r0, r3, #262144
	movw	r3, #65535
.L1906:
	ldr	ip, [r0, #-4]
	cmp	ip, #0
	bne	.L1903
	ldr	ip, [fp, #-112]
	sub	r3, r3, #1
	ldr	ip, [ip, r2, lsl #2]
	add	r2, r2, #1
	cmp	r2, #4096
	movhi	r2, #0
	cmp	r3, #4096
	str	ip, [r0, #-4]!
	bne	.L1906
	mov	r3, #512
	b	.L1957
.L1903:
	add	r3, r3, #127
	lsr	r3, r3, #7
.L1957:
	str	r3, [fp, #-116]
	lsl	r1, r1, #2
	ldr	r3, [fp, #-116]
	uxth	r1, r1
	ldr	r4, .L1960
	add	r0, r3, #4
	bl	__udivsi3
	add	r3, r0, #1
	mov	r6, r4
	str	r3, [fp, #-120]
	ldr	r3, [fp, #-116]
	lsl	r3, r3, #7
	str	r3, [fp, #-132]
	mov	r3, #0
	str	r3, [fp, #-124]
	str	r3, [fp, #-108]
.L1928:
	mov	r1, #512
	ldr	r0, [r4, #3688]
	bl	__memzero
	ldr	r3, [fp, #-108]
	ldrh	r7, [r4, #10]
	mul	r8, r3, r7
	ldr	r3, [r4, #3264]
	cmp	r3, #0
	moveq	r10, #6
	beq	.L1907
	ldr	r3, [r4, #3268]
	cmp	r3, #0
	moveq	r10, #6
	movne	r10, #9
.L1907:
	ldr	r3, [r6, #3252]
	mov	r1, r8
	mov	r0, #0
	blx	r3
	ldr	r3, [fp, #-120]
	cmp	r3, #1
	beq	.L1908
	ldr	r3, [r6, #3252]
	add	r1, r7, r8
	mov	r0, #0
	blx	r3
.L1908:
	cmp	r10, #9
	movne	r5, #0
	bne	.L1909
	ldr	r5, [r6, #3688]
	mov	r1, #1024
	mov	r0, r5
	bl	__memzero
	ldr	r3, .L1960+8
	mov	r1, #12
	mov	r2, #4
	str	r1, [r5, #4]
	strb	r2, [r5, #17]
	add	r0, r5, r1
	str	r3, [r5]
	mov	r3, #0
	ldrh	r2, [r6, #10]
	str	r3, [r5, #12]
	strb	r3, [r5, #16]
	strh	r2, [r5, #18]	@ movhi
	mov	r2, #16
	strb	r3, [r5, #20]
	strb	r2, [r5, #21]
	strh	r3, [r5, #22]	@ movhi
	bl	js_hash
	str	r0, [r5, #8]
.L1909:
	ldr	r3, [fp, #-120]
	mov	r9, r8
	mul	r3, r7, r3
	mov	r7, #0
	str	r3, [fp, #-136]
	ldr	r3, [fp, #-112]
	str	r3, [fp, #-128]
.L1910:
	ldr	r3, [fp, #-136]
	cmp	r7, r3
	beq	.L1917
	cmp	r10, #9
	addne	r3, r8, r7
	addeq	r3, r9, #1
	lslne	r3, r3, #2
	cmp	r7, #0
	cmpeq	r10, #9
	moveq	r0, #1
	str	r3, [fp, #-104]
	movw	r3, #61424
	str	r3, [fp, #-100]
	movne	r0, #0
	bne	.L1913
	ldr	r3, [r4, #3264]
	mov	r0, #70
	blx	r3
	sub	r3, fp, #104
	ldr	ip, [r4, #3256]
	mov	r2, r5
	mov	r1, r8
	mov	r0, #0
	blx	ip
	ldr	r2, [r4, #3264]
	str	r0, [fp, #-140]
	ldrb	r0, [r4, #22]	@ zero_extendqisi2
	blx	r2
	ldr	r3, [fp, #-140]
	cmn	r3, #1
	bne	.L1914
.L1917:
	ldrb	r3, [r4, #14]	@ zero_extendqisi2
	ldrh	r10, [r4, #10]
	ldr	r7, [r4, #3688]
	str	r3, [fp, #-128]
	ldr	r3, [fp, #-108]
	mul	r9, r3, r10
	ldr	r3, [r4, #3264]
	cmp	r3, #0
	moveq	r8, #6
	beq	.L1916
	ldr	r3, [r4, #3268]
	cmp	r3, #0
	moveq	r8, #6
	movne	r8, #9
.L1916:
	ldr	r3, [fp, #-120]
	mov	r5, #0
	mul	r10, r10, r3
.L1919:
	cmp	r5, r10
	beq	.L1923
	cmp	r5, #0
	cmpeq	r8, #9
	moveq	r0, #1
	movne	r0, #0
	bne	.L1920
	ldr	r3, [r4, #3264]
	mov	r0, #70
	blx	r3
	ldr	r3, [r4, #3268]
	mov	r0, #2
	blx	r3
	mov	r2, r7
	ldr	ip, [r4, #3260]
	mov	r1, r9
	sub	r3, fp, #104
	mov	r0, #0
	blx	ip
	ldr	r3, [r4, #3268]
	ldr	r0, [fp, #-128]
	blx	r3
	ldr	r3, [r4, #3264]
	ldrb	r0, [r4, #22]	@ zero_extendqisi2
	blx	r3
	ldr	r3, [r7]
	ldr	r2, .L1960+8
	cmp	r3, r2
	beq	.L1921
.L1923:
	ldr	r0, [r4, #3688]
	mov	r5, #0
	ldr	r3, [fp, #-112]
.L1922:
	mov	r8, r3
	ldr	r1, [r0, r5, lsl #2]
	ldr	r2, [r8]
	lsl	r7, r5, #2
	add	r3, r3, #4
	cmp	r1, r2
	beq	.L1925
	mov	r1, #512
	bl	__memzero
	ldr	r2, [r6, #3688]
	ldr	r1, [fp, #-108]
	str	r5, [sp]
	ldr	r0, .L1960+12
	ldr	r3, [r8]
	ldr	r2, [r2, r7]
	bl	sftl_printk
	ldrh	r1, [r6, #10]
	mov	r0, #0
	ldr	r2, [fp, #-108]
	ldr	r3, [r6, #3252]
	mul	r1, r2, r1
	blx	r3
.L1926:
	ldr	r3, [fp, #-108]
	ldr	r2, [fp, #-120]
	add	r3, r3, r2
	cmp	r3, #15
	str	r3, [fp, #-108]
	bls	.L1928
	ldr	r3, [fp, #-124]
	cmp	r3, #0
	bne	.L1929
.L1902:
	mvn	r3, #0
	str	r3, [fp, #-116]
.L1929:
	ldr	r5, .L1960
	mov	r3, #0
	ldr	r4, [fp, #-116]
	ldr	r0, [r5, #3684]
	str	r3, [r5, #3680]
	bl	kfree
	ldr	r0, [r5, #3688]
	bl	kfree
.L1898:
	mov	r0, r4
	sub	sp, fp, #40
	ldmfd	sp, {r4, r5, r6, r7, r8, r9, r10, fp, sp, pc}
.L1913:
	ldr	ip, [r4, #3256]
	sub	r3, fp, #104
	ldr	r2, [fp, #-128]
	mov	r1, r9
	blx	ip
	cmn	r0, #1
	beq	.L1917
	ldr	r3, [fp, #-128]
	add	r3, r3, #2048
	str	r3, [fp, #-128]
.L1914:
	add	r7, r7, #1
	add	r9, r9, #1
	b	.L1910
.L1920:
	ldr	ip, [r4, #3260]
	sub	r3, fp, #104
	mov	r2, r7
	add	r1, r9, r5
	blx	ip
	cmn	r0, #1
	beq	.L1923
	ldr	r2, [fp, #-100]
	movw	r3, #61424
	cmp	r2, r3
	bne	.L1923
	add	r7, r7, #2048
.L1921:
	add	r5, r5, #1
	b	.L1919
.L1925:
	ldr	r2, [fp, #-132]
	add	r5, r5, #1
	cmp	r5, r2
	bne	.L1922
	ldr	r3, [fp, #-124]
	add	r3, r3, #1
	cmp	r3, #5
	str	r3, [fp, #-124]
	bls	.L1926
	b	.L1929
.L1899:
	cmp	r0, #64
	bne	.L1930
	mov	r0, #262144
	bl	ftl_malloc
	str	r0, [r6, #3684]
	mov	r0, #262144
	bl	ftl_malloc
	ldr	r3, [r6, #3684]
	str	r0, [r6, #3688]
	cmp	r3, #0
	cmpne	r0, #0
	beq	.L1931
	mov	r2, #1
	mov	r1, #262144
	mov	r0, r3
	str	r2, [r6, #3680]
	bl	__memzero
.L1930:
	ldr	r3, [r6, #3680]
	cmp	r3, #0
	beq	.L1959
	cmp	r5, #63
	ldr	r1, [r6, #3684]
	ldrhi	r6, .L1960+16
	rsbls	r5, r5, #64
	subls	r4, r4, r5
	addls	r7, r7, r5, lsl #9
	movls	r6, r1
	ldr	r0, .L1960+20
	addhi	r6, r5, r6
	mov	r2, #262144
	addhi	r6, r1, r6, lsl #9
	cmp	r9, #576
	subcs	r4, r4, r8
	subcs	r4, r4, #444
	mov	r3, r6
	subcs	r4, r4, #2
	str	r4, [sp]
	bl	sftl_printk
	lsl	r2, r4, #9
	mov	r1, r7
	mov	r0, r6
	bl	ftl_memcpy
	b	.L1959
.L1931:
	ldr	r1, .L1960+24
	ldr	r0, .L1960+28
	bl	sftl_printk
	b	.L1930
.L1961:
	.align	2
.L1960:
	.word	.LANCHOR0
	.word	-52655045
	.word	1179535694
	.word	.LC130
	.word	8388544
	.word	.LC132
	.word	.LANCHOR1+633
	.word	.LC131
	.size	FtlWriteToIDB, .-FtlWriteToIDB
	.align	2
	.global	sftl_write
	.syntax unified
	.arm
	.fpu softvfp
	.type	sftl_write, %function
sftl_write:
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 1, uses_anonymous_args = 0
	mov	ip, sp
	push	{r4, r5, r6, fp, ip, lr, pc}
	sub	fp, ip, #4
	mov	r5, r1
	mov	r6, r2
	mov	r4, r0
	bl	FtlWriteToIDB
	mov	r3, r6
	mov	r2, r5
	mov	r1, r4
	mov	r0, #0
	bl	FtlWrite
	ldmfd	sp, {r4, r5, r6, fp, sp, pc}
	.size	sftl_write, .-sftl_write
	.align	2
	.global	rk_sftl_vendor_dev_ops_register
	.syntax unified
	.arm
	.fpu softvfp
	.type	rk_sftl_vendor_dev_ops_register, %function
rk_sftl_vendor_dev_ops_register:
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 1, uses_anonymous_args = 0
	mov	ip, sp
	push	{fp, ip, lr, pc}
	sub	fp, ip, #4
	ldr	r2, .L1966
	ldr	r3, [r2, #3692]
	cmp	r3, #0
	streq	r0, [r2, #3692]
	moveq	r0, r3
	streq	r1, [r2, #3696]
	mvnne	r0, #0
	ldmfd	sp, {fp, sp, pc}
.L1967:
	.align	2
.L1966:
	.word	.LANCHOR0
	.size	rk_sftl_vendor_dev_ops_register, .-rk_sftl_vendor_dev_ops_register
	.align	2
	.global	rk_sftl_vendor_storage_init
	.syntax unified
	.arm
	.fpu softvfp
	.type	rk_sftl_vendor_storage_init, %function
rk_sftl_vendor_storage_init:
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 1, uses_anonymous_args = 0
	mov	ip, sp
	push	{r4, r5, r6, r7, r8, r9, fp, ip, lr, pc}
	sub	fp, ip, #4
	mov	r0, #65536
	ldr	r4, .L1977
	bl	ftl_malloc
	cmp	r0, #0
	str	r0, [r4, #3700]
	mvneq	r9, #11
	beq	.L1968
	ldr	r8, .L1977+4
	mov	r6, #0
	mov	r5, r6
	mov	r7, r6
.L1972:
	ldr	r3, [r4, #3692]
	mov	r1, #128
	ldr	r2, [r4, #3700]
	lsl	r0, r7, #7
	blx	r3
	subs	r9, r0, #0
	bne	.L1970
	ldr	r2, [r4, #3700]
	ldr	r3, [r2]
	cmp	r3, r8
	bne	.L1971
	add	r1, r2, #61440
	ldr	r3, [r2, #4]
	ldr	r1, [r1, #4092]
	cmp	r3, r5
	sub	r1, r1, r3
	clz	r1, r1
	lsr	r1, r1, #5
	movls	r1, #0
	cmp	r1, #0
	movne	r6, r7
	movne	r5, r3
.L1971:
	add	r7, r7, #1
	cmp	r7, #2
	bne	.L1972
	cmp	r5, #0
	beq	.L1973
	ldr	r3, [r4, #3692]
	mov	r1, #128
	lsl	r0, r6, #7
	blx	r3
	subs	r9, r0, #0
	beq	.L1968
.L1970:
	ldr	r0, [r4, #3700]
	mvn	r9, #0
	bl	kfree
	mov	r3, #0
	str	r3, [r4, #3700]
	b	.L1968
.L1973:
	mov	r1, #65536
	mov	r0, r2
	bl	__memzero
	ldr	r3, [r4, #3700]
	mov	r2, #1
	add	r1, r3, #61440
	str	r2, [r3, #4]
	str	r8, [r3]
	str	r2, [r1, #4092]
	ldr	r2, .L1977+8
	strh	r5, [r3, #12]	@ movhi
	strh	r2, [r3, #14]	@ movhi
.L1968:
	mov	r0, r9
	ldmfd	sp, {r4, r5, r6, r7, r8, r9, fp, sp, pc}
.L1978:
	.align	2
.L1977:
	.word	.LANCHOR0
	.word	1380668996
	.word	-1032
	.size	rk_sftl_vendor_storage_init, .-rk_sftl_vendor_storage_init
	.align	2
	.global	rk_sftl_vendor_read
	.syntax unified
	.arm
	.fpu softvfp
	.type	rk_sftl_vendor_read, %function
rk_sftl_vendor_read:
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 1, uses_anonymous_args = 0
	mov	ip, sp
	push	{r4, r5, fp, ip, lr, pc}
	sub	fp, ip, #4
	ldr	r3, .L1985
	mov	r4, r0
	mov	r0, r1
	ldr	ip, [r3, #3700]
	cmp	ip, #0
	ldrhne	lr, [ip, #10]
	movne	r3, #0
	bne	.L1981
.L1984:
	mvn	r0, #0
	ldmfd	sp, {r4, r5, fp, sp, pc}
.L1982:
	add	r3, r3, #1
.L1981:
	cmp	r3, lr
	bcs	.L1984
	add	r1, ip, r3, lsl #3
	ldrh	r5, [r1, #16]
	cmp	r5, r4
	bne	.L1982
	ldrh	r4, [r1, #20]
	ldrh	r1, [r1, #18]
	cmp	r4, r2
	movcs	r4, r2
	add	r1, r1, #1024
	mov	r2, r4
	add	r1, ip, r1
	bl	memcpy
	mov	r0, r4
	ldmfd	sp, {r4, r5, fp, sp, pc}
.L1986:
	.align	2
.L1985:
	.word	.LANCHOR0
	.size	rk_sftl_vendor_read, .-rk_sftl_vendor_read
	.align	2
	.global	rk_sftl_vendor_write
	.syntax unified
	.arm
	.fpu softvfp
	.type	rk_sftl_vendor_write, %function
rk_sftl_vendor_write:
	@ args = 0, pretend = 0, frame = 24
	@ frame_needed = 1, uses_anonymous_args = 0
	mov	ip, sp
	push	{r4, r5, r6, r7, r8, r9, r10, fp, ip, lr, pc}
	sub	fp, ip, #4
	sub	sp, sp, #24
	mov	ip, r1
	ldr	r8, .L2006
	mov	r9, r2
	ldr	r4, [r8, #3700]
	cmp	r4, #0
	beq	.L2002
	ldrh	r2, [r4, #10]
	add	r7, r9, #63
	ldrh	r3, [r4, #8]
	bic	r7, r7, #63
	mov	r5, #0
	str	r3, [fp, #-44]
.L1989:
	cmp	r5, r2
	bcc	.L1997
	ldrh	r1, [r4, #14]
	cmp	r7, r1
	bhi	.L2002
	add	r3, r4, r2, lsl #3
	uxth	r7, r7
	strh	r0, [r3, #16]	@ movhi
	ldrh	r2, [r4, #12]
	strh	r9, [r3, #20]	@ movhi
	strh	r2, [r3, #18]	@ movhi
	add	r2, r2, r7
	sub	r7, r1, r7
	strh	r2, [r4, #12]	@ movhi
	strh	r7, [r4, #14]	@ movhi
	mov	r2, r9
	ldrh	r0, [r3, #18]
	mov	r1, ip
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
	ldr	r3, [r8, #3696]
	b	.L2005
.L1997:
	add	r6, r4, r5, lsl #3
	ldrh	r3, [r6, #16]
	cmp	r3, r0
	str	r3, [fp, #-48]
	bne	.L1990
	ldrh	r1, [r6, #20]
	add	r3, r4, #1024
	add	r1, r1, #63
	bic	r1, r1, #63
	cmp	r9, r1
	str	r1, [fp, #-52]
	bls	.L1991
	ldrh	r1, [r4, #14]
	cmp	r7, r1
	bhi	.L2002
	ldrh	r8, [r6, #18]
	sub	r2, r2, #1
	str	r2, [fp, #-56]
.L1992:
	ldr	r2, [fp, #-56]
	add	r6, r6, #8
	cmp	r5, r2
	bcc	.L1993
	ldrh	r2, [fp, #-48]
	add	r5, r4, r5, lsl #3
	uxth	r8, r8
	mov	r1, ip
	strh	r9, [r5, #20]	@ movhi
	add	r0, r3, r8
	strh	r2, [r5, #16]	@ movhi
	strh	r8, [r5, #18]	@ movhi
	mov	r2, r9
	bl	memcpy
	uxth	r3, r7
	ldrh	r7, [r4, #14]
	add	r8, r8, r3
	sub	r7, r7, r3
	ldr	r3, [fp, #-52]
	strh	r8, [r4, #12]	@ movhi
	add	r7, r7, r3
	strh	r7, [r4, #14]	@ movhi
.L1994:
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
	ldr	r3, .L2006
	ldr	r3, [r3, #3696]
.L2005:
	ldr	r0, [fp, #-44]
	mov	r2, r4
	mov	r1, #128
	lsl	r0, r0, #7
	blx	r3
	mov	r0, #0
.L1987:
	sub	sp, fp, #40
	ldmfd	sp, {r4, r5, r6, r7, r8, r9, r10, fp, sp, pc}
.L1993:
	ldrh	r10, [r6, #20]
	add	r0, r3, r8
	ldrh	r2, [r6, #16]
	add	r5, r5, #1
	ldrh	r1, [r6, #18]
	strh	r10, [r6, #12]	@ movhi
	add	r10, r10, #63
	bic	r10, r10, #63
	strh	r2, [r6, #8]	@ movhi
	strh	r8, [r6, #10]	@ movhi
	add	r1, r3, r1
	mov	r2, r10
	str	ip, [fp, #-64]
	str	r3, [fp, #-60]
	add	r8, r8, r10
	bl	memcpy
	ldr	ip, [fp, #-64]
	ldr	r3, [fp, #-60]
	b	.L1992
.L1991:
	add	r5, r4, r5, lsl #3
	mov	r2, r9
	mov	r1, ip
	ldrh	r0, [r5, #18]
	add	r0, r3, r0
	bl	memcpy
	strh	r9, [r6, #20]	@ movhi
	b	.L1994
.L1990:
	add	r5, r5, #1
	b	.L1989
.L2002:
	mvn	r0, #0
	b	.L1987
.L2007:
	.align	2
.L2006:
	.word	.LANCHOR0
	.size	rk_sftl_vendor_write, .-rk_sftl_vendor_write
	.align	2
	.global	rk_sftl_vendor_storage_ioctl
	.syntax unified
	.arm
	.fpu softvfp
	.type	rk_sftl_vendor_storage_ioctl, %function
rk_sftl_vendor_storage_ioctl:
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 1, uses_anonymous_args = 0
	mov	ip, sp
	push	{r4, r5, r6, fp, ip, lr, pc}
	sub	fp, ip, #4
	mov	r0, #4096
	mov	r4, r2
	mov	r6, r1
	bl	ftl_malloc
	subs	r5, r0, #0
	mvneq	r4, #0
	beq	.L2008
	ldr	r3, .L2036
	cmp	r6, r3
	beq	.L2011
	add	r3, r3, #1
	cmp	r6, r3
	beq	.L2012
.L2031:
	mvn	r4, #13
	b	.L2010
.L2011:
	ldr	r6, .L2036+4
	mov	r3, sp
	and	r3, r3, r6
	ldr	r3, [r3, #72]
	.syntax divided
@ 564 "./arch/arm/include/asm/uaccess.h" 1
	adds r2, r4, #8; sbcccs r2, r2, r3; movcc r3, #0
@ 0 "" 2
	.arm
	.syntax unified
	cmp	r3, #0
	bne	.L2013
	mov	r2, #8
	mov	r1, r4
	bl	arm_copy_from_user
	cmp	r0, #0
	beq	.L2032
.L2014:
	mov	r2, r4
	mov	r1, #256
.L2035:
	ldr	r0, .L2036+8
	bl	sftl_printk
	b	.L2031
.L2013:
	mov	r1, #8
	bl	__memzero
	b	.L2014
.L2032:
	ldr	r2, [r5]
	ldr	r3, .L2036+12
	cmp	r2, r3
	beq	.L2016
.L2017:
	mvn	r4, #0
.L2010:
	mov	r0, r5
	bl	kfree
.L2008:
	mov	r0, r4
	ldmfd	sp, {r4, r5, r6, fp, sp, pc}
.L2016:
	ldrh	r2, [r5, #6]
	add	r1, r5, #8
	ldrh	r0, [r5, #4]
	bl	rk_sftl_vendor_read
	cmn	r0, #1
	beq	.L2017
	mov	r3, sp
	uxth	r2, r0
	and	r6, r6, r3
	strh	r0, [r5, #6]	@ movhi
	add	r2, r2, #8
	ldr	r3, [r6, #72]
	.syntax divided
@ 573 "./arch/arm/include/asm/uaccess.h" 1
	adds r1, r4, r2; sbcccs r1, r1, r3; movcc r3, #0
@ 0 "" 2
	.arm
	.syntax unified
	cmp	r3, #0
	bne	.L2031
	mov	r0, r4
	mov	r1, r5
	bl	arm_copy_to_user
	subs	r4, r0, #0
	beq	.L2010
	b	.L2031
.L2012:
	ldr	r6, .L2036+4
	mov	r3, sp
	and	r3, r3, r6
	ldr	r3, [r3, #72]
	.syntax divided
@ 564 "./arch/arm/include/asm/uaccess.h" 1
	adds r2, r4, #8; sbcccs r2, r2, r3; movcc r3, #0
@ 0 "" 2
	.arm
	.syntax unified
	cmp	r3, #0
	bne	.L2019
	mov	r2, #8
	mov	r1, r4
	bl	arm_copy_from_user
	cmp	r0, #0
	beq	.L2033
.L2020:
	mov	r2, r4
	mov	r1, #276
	b	.L2035
.L2019:
	mov	r1, #8
	bl	__memzero
	b	.L2020
.L2033:
	ldr	r2, [r5]
	ldr	r3, .L2036+12
	cmp	r2, r3
	bne	.L2017
	ldrh	r2, [r5, #6]
	movw	r3, #4087
	cmp	r2, r3
	bhi	.L2017
	mov	r3, sp
	add	r2, r2, #8
	and	r6, r6, r3
	ldr	r3, [r6, #72]
	.syntax divided
@ 564 "./arch/arm/include/asm/uaccess.h" 1
	adds r1, r4, r2; sbcccs r1, r1, r3; movcc r3, #0
@ 0 "" 2
	.arm
	.syntax unified
	cmp	r3, #0
	bne	.L2022
	mov	r1, r4
	mov	r0, r5
	bl	arm_copy_from_user
	cmp	r0, #0
	beq	.L2034
.L2023:
	mov	r2, r4
	movw	r1, #283
	b	.L2035
.L2022:
	mov	r1, r2
	mov	r0, r5
	bl	__memzero
	b	.L2023
.L2034:
	ldrh	r2, [r5, #6]
	add	r1, r5, #8
	ldrh	r0, [r5, #4]
	bl	rk_sftl_vendor_write
	mov	r4, r0
	b	.L2010
.L2037:
	.align	2
.L2036:
	.word	1074034177
	.word	-8192
	.word	.LC133
	.word	1448232273
	.size	rk_sftl_vendor_storage_ioctl, .-rk_sftl_vendor_storage_ioctl
	.align	2
	.global	rk_sftl_vendor_register
	.syntax unified
	.arm
	.fpu softvfp
	.type	rk_sftl_vendor_register, %function
rk_sftl_vendor_register:
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 1, uses_anonymous_args = 0
	mov	ip, sp
	push	{fp, ip, lr, pc}
	sub	fp, ip, #4
	ldr	r0, .L2039
	bl	misc_register
	ldmfd	sp, {fp, sp, pc}
.L2040:
	.align	2
.L2039:
	.word	.LANCHOR2+12
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
	.type	__func__.3968, %object
	.size	__func__.3968, 17
__func__.3968:
	.ascii	"INSERT_DATA_LIST\000"
	.type	__func__.3963, %object
	.size	__func__.3963, 17
__func__.3963:
	.ascii	"INSERT_FREE_LIST\000"
	.type	__func__.3999, %object
	.size	__func__.3999, 17
__func__.3999:
	.ascii	"List_remove_node\000"
	.type	__func__.4031, %object
	.size	__func__.4031, 22
__func__.4031:
	.ascii	"List_update_data_list\000"
	.type	__func__.4138, %object
	.size	__func__.4138, 22
__func__.4138:
	.ascii	"select_l2p_ram_region\000"
	.type	__func__.4465, %object
	.size	__func__.4465, 16
__func__.4465:
	.ascii	"make_superblock\000"
	.type	__func__.4640, %object
	.size	__func__.4640, 19
__func__.4640:
	.ascii	"get_new_active_ppa\000"
	.type	__func__.15063, %object
	.size	__func__.15063, 17
__func__.15063:
	.ascii	"FlashEraseBlocks\000"
	.type	__func__.4073, %object
	.size	__func__.4073, 26
__func__.4073:
	.ascii	"ftl_map_blk_alloc_new_blk\000"
	.type	__func__.3578, %object
	.size	__func__.3578, 11
__func__.3578:
	.ascii	"FtlMemInit\000"
	.type	__func__.3805, %object
	.size	__func__.3805, 14
__func__.3805:
	.ascii	"FtlBbt2Bitmap\000"
	.type	__func__.4486, %object
	.size	__func__.4486, 18
__func__.4486:
	.ascii	"SupperBlkListInit\000"
	.type	__func__.15020, %object
	.size	__func__.15020, 15
__func__.15020:
	.ascii	"FlashReadPages\000"
	.type	__func__.4250, %object
	.size	__func__.4250, 14
__func__.4250:
	.ascii	"FtlScanSysBlk\000"
	.type	__func__.3848, %object
	.size	__func__.3848, 11
__func__.3848:
	.ascii	"FtlLoadBbt\000"
	.type	__func__.15039, %object
	.size	__func__.15039, 15
__func__.15039:
	.ascii	"FlashProgPages\000"
	.type	__func__.4612, %object
	.size	__func__.4612, 25
__func__.4612:
	.ascii	"allocate_data_superblock\000"
	.type	__func__.4653, %object
	.size	__func__.4653, 16
__func__.4653:
	.ascii	"update_vpc_list\000"
	.type	__func__.4660, %object
	.size	__func__.4660, 20
__func__.4660:
	.ascii	"decrement_vpc_count\000"
	.type	__func__.4099, %object
	.size	__func__.4099, 31
__func__.4099:
	.ascii	"Ftl_write_map_blk_to_last_page\000"
	.type	__func__.4113, %object
	.size	__func__.4113, 16
__func__.4113:
	.ascii	"FtlMapWritePage\000"
	.type	__func__.4040, %object
	.size	__func__.4040, 16
__func__.4040:
	.ascii	"load_l2p_region\000"
	.type	__func__.4084, %object
	.size	__func__.4084, 15
__func__.4084:
	.ascii	"ftl_map_blk_gc\000"
	.type	__func__.4155, %object
	.size	__func__.4155, 9
__func__.4155:
	.ascii	"log2phys\000"
	.type	__func__.4374, %object
	.size	__func__.4374, 16
__func__.4374:
	.ascii	"FtlReUsePrevPpa\000"
	.type	__func__.4408, %object
	.size	__func__.4408, 22
__func__.4408:
	.ascii	"FtlRecoverySuperblock\000"
	.type	__func__.4527, %object
	.size	__func__.4527, 14
__func__.4527:
	.ascii	"ftl_check_vpc\000"
	.type	__func__.4228, %object
	.size	__func__.4228, 15
__func__.4228:
	.ascii	"FtlVpcTblFlush\000"
	.type	__func__.4511, %object
	.size	__func__.4511, 21
__func__.4511:
	.ascii	"FtlVpcCheckAndModify\000"
	.type	__func__.4633, %object
	.size	__func__.4633, 29
__func__.4633:
	.ascii	"allocate_new_data_superblock\000"
	.type	__func__.3702, %object
	.size	__func__.3702, 13
__func__.3702:
	.ascii	"FtlProgPages\000"
	.type	__func__.4730, %object
	.size	__func__.4730, 19
__func__.4730:
	.ascii	"FtlGcFreeTempBlock\000"
	.type	__func__.4843, %object
	.size	__func__.4843, 23
__func__.4843:
	.ascii	"rk_ftl_garbage_collect\000"
	.type	__func__.3730, %object
	.size	__func__.3730, 9
__func__.3730:
	.ascii	"FtlWrite\000"
	.type	__func__.4306, %object
	.size	__func__.4306, 15
__func__.4306:
	.ascii	"FtlLoadSysInfo\000"
	.type	__func__.4328, %object
	.size	__func__.4328, 18
__func__.4328:
	.ascii	"FtlMapTblRecovery\000"
	.type	__func__.15187, %object
	.size	__func__.15187, 14
__func__.15187:
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
	.ascii	"write_idblock fail! %x %x %x %x\012\000"
.LC131:
	.ascii	"%s idb buffer alloc fail\012\000"
.LC132:
	.ascii	"%p %x %p %x\012\000"
.LC133:
	.ascii	"copy_from_user error %d %p %p\012\000"
.LC134:
	.ascii	"vendor_storage\000"
