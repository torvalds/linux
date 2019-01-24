/* SPDX-License-Identifier: GPL-2.0 */

/* Copyright (c) 2018 Rockchip Electronics Co. Ltd.
 * date: 2019-02-20
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
	ldr	r4, .L28
	movw	ip, #262
	ldrh	r1, [r0]
	ldrh	r6, [r0, #2]
	add	r2, r4, #256
	ldrh	r0, [r0, #4]
	strh	r1, [r2]	@ movhi
	movw	r2, #258
	strh	r6, [r4, r2]	@ movhi
	add	r2, r4, #260
	ldrh	lr, [r3, #14]
	strh	r0, [r2]	@ movhi
	ldrh	r2, [r3, #6]
	cmp	lr, #4
	strh	lr, [r4, ip]	@ movhi
	strh	r2, [r4, #246]	@ movhi
	lsreq	r2, r2, #1
	strheq	r2, [r4, #246]	@ movhi
	moveq	r2, #8
	strheq	r2, [r4, ip]	@ movhi
	mov	r2, #0
	ldr	ip, .L28+4
.L20:
	strb	r2, [r2, ip]
	add	r2, r2, #1
	cmp	r2, #32
	bne	.L20
	ldrh	r5, [r4, #246]
	smulbb	r6, r6, r0
	ldr	r8, .L28+8
	cmp	r1, #1
	ldr	r2, .L28+12
	mov	ip, #5
	uxth	r6, r6
	mov	lr, #0
	smulbb	r0, r0, r5
	mov	r7, r8
	strh	ip, [r2]	@ movhi
	movw	ip, #298
	strheq	r1, [r2]	@ movhi
	mov	r2, #640
	uxth	r0, r0
	strh	r2, [r7], #4	@ movhi
	movw	r2, #302
	strh	lr, [r4, ip]	@ movhi
	str	r3, [fp, #-44]
	strh	r0, [r4, r2]	@ movhi
	strh	r6, [r4, #236]	@ movhi
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
	ldr	r3, .L28+16
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
	ldr	r2, .L28+20
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
.L29:
	.align	2
.L28:
	.word	.LANCHOR0
	.word	.LANCHOR0+264
	.word	.LANCHOR0+300
	.word	.LANCHOR0+296
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
	ldr	r3, .L36
	uxth	r1, r0
	add	r2, r3, #348
	ldrh	r0, [r2]
	cmp	r0, #0
	ldmfdeq	sp, {fp, sp, pc}
	ldr	r2, [r3, #352]
	add	r3, r3, #324
	ldrh	r3, [r3]
	add	r3, r2, r3, lsl #1
.L32:
	cmp	r2, r3
	bne	.L33
	mov	r0, #0
	ldmfd	sp, {fp, sp, pc}
.L33:
	ldrh	r0, [r2], #2
	cmp	r1, r0
	bne	.L32
	mov	r0, #1
	ldmfd	sp, {fp, sp, pc}
.L37:
	.align	2
.L36:
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
	ldr	r3, .L40
	ldr	r0, [r3, #344]
	ldmfd	sp, {fp, sp, pc}
.L41:
	.align	2
.L40:
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
	ldr	r4, .L43
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
	ldr	r0, .L43+4
	bl	sftl_printk
	ldrh	r3, [r4, #6]
	mov	r0, #0
	add	r3, r3, #1
	strh	r3, [r4, #6]	@ movhi
	ldmib	sp, {r4, r5, r6, r7, fp, sp, pc}
.L44:
	.align	2
.L43:
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
	ldr	r5, .L46
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
.L47:
	.align	2
.L46:
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
	ldr	r0, .L50
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
.L51:
	.align	2
.L50:
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
	ldr	r3, .L59
	movw	r2, #302
	movw	r1, #258
	mov	r5, #0
	ldrh	r2, [r3, r2]
	mov	r4, r5
	ldrh	r6, [r3, r1]
	mul	r6, r6, r2
.L53:
	uxth	r0, r5
	cmp	r0, r6
	blt	.L55
	mov	r0, r4
	ldmfd	sp, {r4, r5, r6, fp, sp, pc}
.L55:
	bl	FtlBbmIsBadBlock
	cmp	r0, #0
	add	r5, r5, #1
	addne	r4, r4, #1
	uxthne	r4, r4
	b	.L53
.L60:
	.align	2
.L59:
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
	ldr	r4, .L62
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
.L63:
	.align	2
.L62:
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
	ldr	r3, .L65
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
.L66:
	.align	2
.L65:
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
	ldr	r4, .L68
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
.L69:
	.align	2
.L68:
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
	bls	.L71
	sub	r0, r0, r1
	cmp	r0, #-2147483648
	movhi	r0, #0
	movls	r0, #1
	ldmfd	sp, {fp, sp, pc}
.L71:
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
	ldr	r2, .L74
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
.L75:
	.align	2
.L74:
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
	ldr	r3, .L77
	ldrh	r0, [r3, #6]
	clz	r0, r0
	lsr	r0, r0, #5
	ldmfd	sp, {fp, sp, pc}
.L78:
	.align	2
.L77:
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
	ldr	r3, .L80
	ldrh	r0, [r3, #6]
	sub	r0, r0, #1024
	clz	r0, r0
	lsr	r0, r0, #5
	ldmfd	sp, {fp, sp, pc}
.L81:
	.align	2
.L80:
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
	ldr	r3, .L91
	ldrh	r2, [r3, #6]
	cmp	r2, #0
	ldmfdeq	sp, {r4, fp, sp, pc}
	ldr	r2, .L91+4
	mov	r4, #0
	ldrh	r1, [r3, #2]
	mov	lr, r4
	ldrh	ip, [r2, #28]
	ldrh	r2, [r3, #4]
	and	ip, ip, #31
.L84:
	uxth	r0, r4
	add	r4, r4, #1
	cmp	ip, r0
	bgt	.L85
	cmp	lr, #0
	strhne	r1, [r3, #2]	@ movhi
	strhne	r2, [r3, #4]	@ movhi
	ldmfd	sp, {r4, fp, sp, pc}
.L85:
	add	r0, r3, r1, lsl #1
	add	r1, r1, #1
	ubfx	r1, r1, #0, #10
	ldrh	lr, [r0, #8]
	add	r0, r3, r2, lsl #1
	strh	lr, [r0, #8]	@ movhi
	mov	lr, #1
	add	r2, r2, lr
	ubfx	r2, r2, #0, #10
	b	.L84
.L92:
	.align	2
.L91:
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
	ldr	r3, .L99
	uxth	r0, r0
	ldrh	ip, [r3, #6]
	cmp	ip, #1024
	ldrhne	lr, [r3, #2]
	movne	r1, #0
	bne	.L95
.L97:
	mov	r0, #0
	ldmfd	sp, {fp, sp, pc}
.L96:
	add	r2, r1, lr
	ubfx	r2, r2, #0, #10
	add	r2, r3, r2, lsl #1
	ldrh	r2, [r2, #8]
	cmp	r2, r0
	beq	.L98
	add	r1, r1, #1
.L95:
	cmp	r1, ip
	bcc	.L96
	b	.L97
.L98:
	mov	r0, #1
	ldmfd	sp, {fp, sp, pc}
.L100:
	.align	2
.L99:
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
	ldr	r1, .L116
	ldrh	r3, [r1, #244]
	mov	lr, r1
	cmp	r3, r0
	bls	.L103
	ldr	r6, [r1, #2520]
	lsl	r5, r0, #3
	mvn	ip, #0
	add	r2, r6, r5
	strh	ip, [r2, #2]	@ movhi
	strh	ip, [r6, r5]	@ movhi
	ldr	r3, [r1, #2524]
	cmp	r3, #0
	bne	.L104
.L115:
	str	r2, [r1, #2524]
	b	.L103
.L104:
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
.L109:
	ldr	r4, [fp, #-44]
	add	lr, lr, #1
	uxth	lr, lr
	cmp	lr, r4
	movls	r4, #0
	movhi	r4, #1
	cmp	r0, r1
	orreq	r4, r4, #1
	cmp	r4, #0
	bne	.L103
	ldrh	r4, [r3, #4]
	cmp	r4, #0
	beq	.L107
	lsl	r10, r1, #1
	ldrh	r10, [r8, r10]
	mul	r4, r4, r10
	cmp	r4, ip
	bcs	.L107
	ldrh	r4, [r3]
	cmp	r4, r9
	bne	.L108
	strh	r1, [r2, #2]	@ movhi
	strh	r0, [r3]	@ movhi
	ldr	r3, .L116
	str	r2, [r3, #2528]
.L103:
	mov	r0, #0
	ldmib	sp, {r4, r5, r6, r7, r8, r9, r10, fp, sp, pc}
.L108:
	add	r3, r7, r4, lsl #3
	mov	r1, r4
	b	.L109
.L107:
	strh	r1, [r6, r5]	@ movhi
	ldrh	r1, [r3, #2]
	strh	r1, [r2, #2]	@ movhi
	ldr	r1, .L116
	ldr	ip, [r1, #2524]
	cmp	r3, ip
	strheq	r0, [r3, #2]	@ movhi
	beq	.L115
.L110:
	ldrh	r2, [r3, #2]
	ldr	r1, [r1, #2520]
	lsl	r2, r2, #3
	strh	r0, [r1, r2]	@ movhi
	strh	r0, [r3, #2]	@ movhi
	b	.L103
.L117:
	.align	2
.L116:
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
	ldr	r2, .L120
	movw	r1, #2532
	ldrh	r3, [r2, r1]
	add	r3, r3, #1
	uxth	r3, r3
	strh	r3, [r2, r1]	@ movhi
	ldrh	r2, [r2, #244]
	cmp	r2, r3
	ldmfdcs	sp, {fp, sp, pc}
	mov	r2, #214
	ldr	r1, .L120+4
	ldr	r0, .L120+8
	bl	sftl_printk
	ldmfd	sp, {fp, sp, pc}
.L121:
	.align	2
.L120:
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
	beq	.L123
	ldr	r2, .L129
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
	beq	.L123
	ldr	r8, [r2, #2540]
	lsl	r2, r0, #1
	ldr	r7, [r1, #2520]
	mov	r4, lr
	ldrh	r9, [r8, r2]
	sub	r2, r3, r7
	ubfx	r2, r2, #3, #16
.L127:
	lsl	lr, r2, #1
	ldrh	lr, [r8, lr]
	cmp	lr, r9
	bcs	.L125
	ldrh	lr, [r3]
	cmp	lr, r4
	bne	.L126
	strh	r2, [ip, #2]	@ movhi
	strh	r0, [r3]	@ movhi
.L123:
	mov	r0, #0
	ldmfd	sp, {r4, r5, r6, r7, r8, r9, fp, sp, pc}
.L126:
	add	r3, r7, lr, lsl #3
	mov	r2, lr
	b	.L127
.L125:
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
	b	.L123
.L130:
	.align	2
.L129:
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
	ldr	r2, .L133
	ldrh	r3, [r2, #228]
	add	r3, r3, #1
	uxth	r3, r3
	strh	r3, [r2, #228]	@ movhi
	ldrh	r2, [r2, #244]
	cmp	r2, r3
	ldmfdcs	sp, {fp, sp, pc}
	mov	r2, #207
	ldr	r1, .L133+4
	ldr	r0, .L133+8
	bl	sftl_printk
	ldmfd	sp, {fp, sp, pc}
.L134:
	.align	2
.L133:
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
	ldr	r4, .L142
	uxth	r1, r1
	movw	r3, #65535
	mov	r8, r0
	ldr	r7, [r4, #2520]
	lsl	r6, r1, #3
	add	r5, r7, r6
	ldrh	r2, [r5, #2]
	cmp	r2, r3
	bne	.L136
	ldr	r3, [r0]
	cmp	r5, r3
	beq	.L136
	mov	r2, #372
	ldr	r1, .L142+4
	ldr	r0, .L142+8
	bl	sftl_printk
.L136:
	ldr	r3, [r8]
	ldrh	r2, [r7, r6]
	cmp	r5, r3
	movw	r3, #65535
	bne	.L137
	cmp	r2, r3
	ldrne	r3, [r4, #2520]
	moveq	r3, #0
	streq	r3, [r8]
	addne	r2, r3, r2, lsl #3
	mvnne	r3, #0
	strne	r2, [r8]
	strhne	r3, [r2, #2]	@ movhi
.L139:
	mvn	r3, #0
	mov	r0, #0
	strh	r3, [r7, r6]	@ movhi
	strh	r3, [r5, #2]	@ movhi
	ldmfd	sp, {r4, r5, r6, r7, r8, fp, sp, pc}
.L137:
	cmp	r2, r3
	ldrh	r3, [r5, #2]
	bne	.L140
	cmp	r3, r2
	ldrne	r2, [r4, #2520]
	lslne	r3, r3, #3
	mvnne	r1, #0
	beq	.L139
.L141:
	strh	r1, [r2, r3]	@ movhi
	b	.L139
.L140:
	ldr	r1, [r4, #2520]
	add	r2, r1, r2, lsl #3
	strh	r3, [r2, #2]	@ movhi
	ldrh	r3, [r5, #2]
	ldrh	r1, [r7, r6]
	ldr	r2, [r4, #2520]
	lsl	r3, r3, #3
	b	.L141
.L143:
	.align	2
.L142:
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
	beq	.L144
	ldr	r2, .L151
	movw	ip, #65535
	ldr	r4, [r2, #2520]
.L146:
	cmp	r1, #0
	bne	.L147
.L149:
	sub	r3, r3, r4
	ubfx	r4, r3, #3, #16
	mov	r1, r4
	bl	List_remove_node
.L144:
	mov	r0, r4
	ldmfd	sp, {r4, fp, sp, pc}
.L147:
	ldrh	r2, [r3]
	cmp	r2, ip
	beq	.L149
	sub	r1, r1, #1
	add	r3, r4, r2, lsl #3
	uxth	r1, r1
	b	.L146
.L152:
	.align	2
.L151:
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
	ldr	r1, .L160
	uxth	r2, r0
	ldr	r3, [r1, #2524]
	cmp	r3, #0
	ldrne	r1, [r1, #2520]
	movwne	r0, #65535
	bne	.L156
	movw	r0, #65535
	ldmfd	sp, {fp, sp, pc}
.L158:
	sub	r2, r2, #1
	add	r3, r1, r3, lsl #3
	uxth	r2, r2
.L156:
	cmp	r2, #0
	beq	.L157
	ldrh	r3, [r3]
	cmp	r3, r0
	bne	.L158
	ldmfd	sp, {fp, sp, pc}
.L157:
	sub	r3, r3, r1
	ubfx	r0, r3, #3, #16
	ldmfd	sp, {fp, sp, pc}
.L161:
	.align	2
.L160:
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
	ldr	r4, .L175
	uxth	r5, r0
	ldrh	r3, [r4, #28]
	cmp	r3, r5
	beq	.L164
	ldrh	r3, [r4, #80]
	cmp	r3, r5
	beq	.L164
	ldrh	r3, [r4, #128]
	cmp	r3, r5
	beq	.L164
	ldr	r8, [r4, #2520]
	ldr	r3, [r4, #2524]
	add	r7, r8, r5, lsl #3
	cmp	r7, r3
	beq	.L164
	ldr	r2, [r4, #76]
	lsl	r3, r5, #1
	ldrh	r6, [r7, #4]
	ldrh	r3, [r2, r3]
	cmp	r6, #0
	mvneq	r6, #0
	mulne	r6, r6, r3
	ldr	r3, [r8, r5, lsl #3]
	cmn	r3, #1
	bne	.L167
	movw	r2, #463
	ldr	r1, .L175+4
	ldr	r0, .L175+8
	bl	sftl_printk
.L167:
	ldr	r3, [r8, r5, lsl #3]
	cmn	r3, #1
	beq	.L164
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
	bcs	.L164
	mov	r1, r5
	ldr	r0, .L175+12
	bl	List_remove_node
	movw	r3, #2532
	ldrh	r3, [r4, r3]
	cmp	r3, #0
	bne	.L169
	movw	r2, #474
	ldr	r1, .L175+4
	ldr	r0, .L175+8
	bl	sftl_printk
.L169:
	movw	r2, #2532
	mov	r0, r5
	ldrh	r3, [r4, r2]
	sub	r3, r3, #1
	strh	r3, [r4, r2]	@ movhi
	bl	INSERT_DATA_LIST
.L164:
	mov	r0, #0
	ldmfd	sp, {r4, r5, r6, r7, r8, fp, sp, pc}
.L176:
	.align	2
.L175:
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
	ldr	r0, .L187
	movw	r3, #342
	mov	r1, #0
	movw	ip, #65535
	ldrh	r2, [r0, r3]
	mov	lr, r0
	ldr	r3, [r0, #2544]
	mov	r0, #12
.L178:
	uxth	r4, r1
	cmp	r4, r2
	bcc	.L180
	mov	r4, r2
	mov	r1, #0
	mov	r0, #-2147483648
	mov	r6, #12
.L181:
	uxth	r5, r1
	cmp	r5, r2
	bcc	.L183
	cmp	r4, r2
	bcc	.L179
	movw	r1, #2548
	mov	r4, r2
	ldrh	r5, [lr, r1]
	mvn	r0, #0
	mov	r1, #0
.L184:
	uxth	ip, r1
	cmp	ip, r2
	bcc	.L186
	cmp	r4, r2
	bcc	.L179
	movw	r2, #789
	ldr	r1, .L187+4
	ldr	r0, .L187+8
	bl	sftl_printk
	b	.L179
.L180:
	add	r1, r1, #1
	mla	r5, r0, r1, r3
	ldrh	r5, [r5, #-12]
	cmp	r5, ip
	bne	.L178
.L179:
	mov	r0, r4
	ldmfd	sp, {r4, r5, r6, r7, fp, sp, pc}
.L183:
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
	b	.L181
.L186:
	ldr	lr, [r3, #4]
	cmp	r0, lr
	bls	.L185
	ldrh	r6, [r3]
	cmp	r6, r5
	movne	r0, lr
	movne	r4, ip
.L185:
	add	r1, r1, #1
	add	r3, r3, #12
	b	.L184
.L188:
	.align	2
.L187:
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
	ldr	ip, .L195
	movw	r1, #2550
	ldrh	r2, [ip, r1]
	mov	r3, ip
	cmp	r2, #4
	cmpls	r0, #0
	bne	.L190
	add	r2, r2, #1
	strh	r2, [ip, r1]	@ movhi
	ldmfd	sp, {fp, sp, pc}
.L190:
	mov	r2, #0
	movw	lr, #65535
	strh	r2, [ip, r1]	@ movhi
	str	r2, [ip, #2552]
	ldrh	r1, [ip, #244]
	ldr	r2, [ip, #76]
	add	r1, r2, r1, lsl #1
.L191:
	cmp	r2, r1
	bne	.L193
	ldmfd	sp, {fp, sp, pc}
.L193:
	ldrh	ip, [r2], #2
	cmp	ip, lr
	ldrne	r0, [r3, #2552]
	addne	r0, r0, ip
	strne	r0, [r3, #2552]
	b	.L191
.L196:
	.align	2
.L195:
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
	beq	.L198
	bl	ftl_set_blk_mode.part.6
	ldmfd	sp, {fp, sp, pc}
.L198:
	ldr	r3, .L200
	lsr	r1, r0, #5
	mov	ip, #1
	and	r0, r0, #31
	ldr	r2, [r3, #24]
	ldr	r3, [r2, r1, lsl #2]
	bic	r0, r3, ip, lsl r0
	str	r0, [r2, r1, lsl #2]
	ldmfd	sp, {fp, sp, pc}
.L201:
	.align	2
.L200:
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
	ldr	r3, .L203
	uxth	r0, r0
	ldr	r3, [r3, #24]
	lsr	r2, r0, #5
	and	r0, r0, #31
	ldr	r3, [r3, r2, lsl #2]
	lsr	r0, r3, r0
	and	r0, r0, #1
	ldmfd	sp, {fp, sp, pc}
.L204:
	.align	2
.L203:
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
	ldr	r3, .L212
	add	ip, r0, r2, lsl #1
	movw	r1, #65535
	add	ip, ip, #14
	ldrh	lr, [r3, #236]
.L206:
	cmp	r2, lr
	bcc	.L208
	movw	r2, #306
	add	ip, r0, #16
	ldrh	r1, [r3, r2]
	movw	r4, #65535
	mov	r3, #0
	sub	r1, r1, #1
	sub	r1, r1, r5
	uxth	r1, r1
.L209:
	uxth	r2, r3
	cmp	lr, r2
	bhi	.L211
	ldmfd	sp, {r4, r5, fp, sp, pc}
.L208:
	ldrh	r4, [ip, #2]!
	add	r2, r2, #1
	uxth	r2, r2
	cmp	r4, r1
	ldrhne	r4, [r0, #4]
	addne	r4, r4, #1
	strhne	r4, [r0, #4]	@ movhi
	b	.L206
.L211:
	ldrh	r2, [ip], #2
	add	r3, r3, #1
	cmp	r2, r4
	ldrhne	r2, [r0, #4]
	addne	r2, r1, r2
	strhne	r2, [r0, #4]	@ movhi
	b	.L209
.L213:
	.align	2
.L212:
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
	ldr	r2, .L220
	ldrh	ip, [r2, #236]
	mov	r2, r3
.L217:
	cmp	r1, r2
	beq	.L219
	ldmfd	sp, {fp, sp, pc}
.L219:
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
	b	.L217
.L221:
	.align	2
.L220:
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
	ldr	r6, .L228
	ldrh	r2, [r0]
	ldrh	r3, [r6, #244]
	cmp	r2, r3
	bcc	.L223
	movw	r2, #2148
	ldr	r1, .L228+4
	ldr	r0, .L228+8
	bl	sftl_printk
.L223:
	ldrh	r9, [r6, #236]
	add	r8, r4, #16
	ldr	r10, .L228+12
	mvn	r7, #0
	mov	r5, #0
	strh	r5, [r4, #4]	@ movhi
	strb	r5, [r4, #7]
.L224:
	uxth	r3, r5
	ldrh	r1, [r4]
	cmp	r9, r3
	bhi	.L226
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
.L226:
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
	b	.L224
.L229:
	.align	2
.L228:
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
	ldr	r3, .L236
	movw	r2, #306
	mov	r5, #0
	uxth	r6, r0
	mov	r4, r5
	ldrh	r8, [r3, #236]
	mov	r7, r3
	ldrh	r9, [r3, r2]
	add	r10, r3, #264
.L231:
	uxth	r3, r5
	cmp	r8, r3
	bhi	.L233
	cmp	r4, #0
	moveq	r0, r4
	beq	.L234
	mov	r1, r4
	mov	r0, #32768
	bl	__divsi3
.L234:
	ldr	r1, [r7, #2520]
	add	r1, r1, r6, lsl #3
	strh	r0, [r1, #4]	@ movhi
	mov	r0, #0
	ldmfd	sp, {r4, r5, r6, r7, r8, r9, r10, fp, sp, pc}
.L233:
	mov	r1, r6
	ldrb	r0, [r10, r5]	@ zero_extendqisi2
	bl	V2P_block
	bl	FtlBbmIsBadBlock
	cmp	r0, #0
	add	r5, r5, #1
	addeq	r4, r4, r9
	uxtheq	r4, r4
	b	.L231
.L237:
	.align	2
.L236:
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
	ldr	r3, .L241
	ldr	r0, [r3, #2536]
	cmp	r0, #0
	ldrne	r2, [r3, #2520]
	ldrne	r3, [r3, #2540]
	subne	r0, r0, r2
	ubfxne	r0, r0, #3, #16
	lslne	r0, r0, #1
	ldrhne	r0, [r3, r0]
	ldmfd	sp, {fp, sp, pc}
.L242:
	.align	2
.L241:
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
	ldr	r2, .L251
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
.L246:
	uxth	r0, r1
	cmp	r4, r0
	bls	.L248
	lsl	r0, r3, #3
	add	r1, r1, #1
	ldrh	lr, [ip, r0]
	cmp	lr, r5
	bne	.L250
.L248:
	ldr	r2, [r2, #2540]
	lsl	r3, r3, #1
	ldrh	r0, [r2, r3]
	ldmfd	sp, {r4, r5, fp, sp, pc}
.L250:
	mov	r3, lr
	b	.L246
.L252:
	.align	2
.L251:
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
	ldr	r5, .L265
	add	r4, r6, #12
	ldr	r1, .L265+4
	bl	strcpy
	ldr	r3, [r5, #320]
	mov	r1, #64
	ldr	r2, .L265+8
	mov	r0, r4
	bl	snprintf
	add	r4, r4, r0
	ldr	r1, .L265+12
	mov	r0, r4
	add	r4, r4, #10
	bl	strcpy
	ldr	r3, [r5, #2556]
	mov	r1, #64
	ldr	r2, .L265+16
	mov	r0, r4
	bl	snprintf
	add	r4, r4, r0
	ldr	r3, [r5, #2552]
	ldr	r2, .L265+20
	mov	r1, #64
	mov	r0, r4
	add	r7, r5, #356
	bl	snprintf
	add	r4, r4, r0
	ldr	r3, [r5, #2560]
	ldr	r2, .L265+24
	mov	r1, #64
	mov	r0, r4
	ldr	r8, .L265+28
	bl	snprintf
	add	r4, r4, r0
	ldr	r3, [r5, #2564]
	ldr	r2, .L265+32
	mov	r1, #64
	mov	r0, r4
	bl	snprintf
	add	r4, r4, r0
	ldr	r3, [r5, #2568]
	ldr	r2, .L265+36
	mov	r1, #64
	mov	r0, r4
	bl	snprintf
	add	r4, r4, r0
	ldr	r3, [r5, #2572]
	ldr	r2, .L265+40
	mov	r1, #64
	mov	r0, r4
	bl	snprintf
	add	r4, r4, r0
	ldr	r3, [r5, #2576]
	ldr	r2, .L265+44
	mov	r1, #64
	mov	r0, r4
	bl	snprintf
	add	r4, r4, r0
	ldr	r3, [r5, #2580]
	ldr	r2, .L265+48
	mov	r1, #64
	mov	r0, r4
	bl	snprintf
	ldr	r3, [r5, #2584]
	add	r4, r4, r0
	ldr	r2, .L265+52
	mov	r1, #64
	mov	r0, r4
	lsr	r3, r3, #11
	bl	snprintf
	ldr	r3, [r5, #2588]
	add	r4, r4, r0
	ldr	r2, .L265+56
	mov	r1, #64
	mov	r0, r4
	lsr	r3, r3, #11
	bl	snprintf
	add	r4, r4, r0
	ldr	r3, [r5, #2592]
	ldr	r2, .L265+60
	mov	r1, #64
	mov	r0, r4
	bl	snprintf
	add	r4, r4, r0
	ldr	r3, [r5, #2596]
	ldr	r2, .L265+64
	mov	r1, #64
	mov	r0, r4
	bl	snprintf
	add	r4, r4, r0
	ldrh	r3, [r7, #6]
	ldr	r2, .L265+68
	mov	r1, #64
	mov	r0, r4
	bl	snprintf
	add	r4, r4, r0
	ldrh	r3, [r5, #228]
	ldr	r2, .L265+72
	mov	r1, #64
	mov	r0, r4
	bl	snprintf
	add	r4, r4, r0
	ldr	r3, [r5, #2600]
	ldr	r2, .L265+76
	mov	r1, #64
	mov	r0, r4
	bl	snprintf
	add	r4, r4, r0
	ldr	r3, [r5, #2604]
	ldr	r2, .L265+80
	mov	r1, #64
	mov	r0, r4
	bl	snprintf
	add	r4, r4, r0
	ldr	r3, [r5, #2608]
	ldr	r2, .L265+84
	mov	r1, #64
	mov	r0, r4
	bl	snprintf
	add	r4, r4, r0
	ldr	r3, [r5, #2612]
	ldr	r2, .L265+88
	mov	r1, #64
	mov	r0, r4
	bl	snprintf
	add	r4, r4, r0
	ldr	r3, [r5, #2616]
	ldr	r2, .L265+92
	mov	r1, #64
	mov	r0, r4
	bl	snprintf
	add	r4, r4, r0
	ldr	r3, [r5, #2620]
	ldr	r2, .L265+96
	mov	r1, #64
	mov	r0, r4
	bl	snprintf
	add	r4, r4, r0
	ldrh	r3, [r8, #30]
	ldr	r2, .L265+100
	mov	r1, #64
	mov	r0, r4
	bl	snprintf
	add	r4, r4, r0
	ldrh	r3, [r8, #28]
	ldr	r2, .L265+104
	mov	r1, #64
	mov	r0, r4
	bl	snprintf
	add	r4, r4, r0
	ldr	r3, [r5, #344]
	ldr	r2, .L265+108
	mov	r1, #64
	mov	r0, r4
	bl	snprintf
	add	r4, r4, r0
	ldr	r3, [r5, #336]
	ldr	r2, .L265+112
	mov	r1, #64
	mov	r0, r4
	bl	snprintf
	add	r4, r4, r0
	ldr	r3, [r5, #232]
	ldr	r2, .L265+116
	mov	r1, #64
	mov	r0, r4
	bl	snprintf
	add	r3, r5, #416
	add	r4, r4, r0
	ldrh	r3, [r3, #6]
	mov	r1, #64
	ldr	r2, .L265+120
	mov	r0, r4
	bl	snprintf
	add	r4, r4, r0
	ldrh	r3, [r5, #244]
	ldr	r2, .L265+124
	mov	r1, #64
	mov	r0, r4
	bl	snprintf
	add	r3, r5, #2624
	add	r4, r4, r0
	ldrh	r3, [r3]
	mov	r1, #64
	ldr	r2, .L265+128
	mov	r0, r4
	bl	snprintf
	add	r4, r4, r0
	ldr	r3, [r5, #248]
	ldr	r2, .L265+132
	mov	r1, #64
	mov	r0, r4
	bl	snprintf
	movw	r3, #2628
	add	r4, r4, r0
	ldrh	r3, [r5, r3]
	mov	r1, #64
	ldr	r2, .L265+136
	mov	r0, r4
	bl	snprintf
	add	r4, r4, r0
	ldrh	r3, [r7]
	ldr	r2, .L265+140
	mov	r1, #64
	mov	r0, r4
	bl	snprintf
	add	r4, r4, r0
	ldrh	r3, [r5, #30]
	ldr	r2, .L265+144
	mov	r1, #64
	mov	r0, r4
	bl	snprintf
	add	r4, r4, r0
	ldrb	r3, [r5, #34]	@ zero_extendqisi2
	ldr	r2, .L265+148
	mov	r1, #64
	mov	r0, r4
	bl	snprintf
	add	r4, r4, r0
	ldrh	r3, [r5, #28]
	ldr	r2, .L265+152
	mov	r1, #64
	mov	r0, r4
	bl	snprintf
	add	r4, r4, r0
	ldrb	r3, [r5, #36]	@ zero_extendqisi2
	ldr	r2, .L265+156
	mov	r1, #64
	mov	r0, r4
	bl	snprintf
	add	r4, r4, r0
	ldrh	r3, [r5, #32]
	ldr	r2, .L265+160
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
	ldr	r2, .L265+164
	bl	snprintf
	add	r4, r4, r0
	ldrh	r3, [r5, #82]
	ldr	r2, .L265+168
	mov	r1, #64
	mov	r0, r4
	bl	snprintf
	add	r4, r4, r0
	ldrb	r3, [r5, #86]	@ zero_extendqisi2
	ldr	r2, .L265+172
	mov	r1, #64
	mov	r0, r4
	bl	snprintf
	add	r4, r4, r0
	ldrh	r3, [r5, #80]
	ldr	r2, .L265+176
	mov	r1, #64
	mov	r0, r4
	bl	snprintf
	add	r4, r4, r0
	ldrb	r3, [r5, #88]	@ zero_extendqisi2
	ldr	r2, .L265+180
	mov	r1, #64
	mov	r0, r4
	bl	snprintf
	add	r4, r4, r0
	ldrh	r3, [r5, #84]
	ldr	r2, .L265+184
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
	ldr	r2, .L265+188
	bl	snprintf
	add	r4, r4, r0
	ldrh	r3, [r5, #130]
	ldr	r2, .L265+192
	mov	r1, #64
	mov	r0, r4
	bl	snprintf
	add	r4, r4, r0
	ldrb	r3, [r5, #134]	@ zero_extendqisi2
	ldr	r2, .L265+196
	mov	r1, #64
	mov	r0, r4
	bl	snprintf
	add	r4, r4, r0
	ldrh	r3, [r5, #128]
	ldr	r2, .L265+200
	mov	r1, #64
	mov	r0, r4
	bl	snprintf
	add	r4, r4, r0
	ldrb	r3, [r5, #136]	@ zero_extendqisi2
	ldr	r2, .L265+204
	mov	r1, #64
	mov	r0, r4
	bl	snprintf
	add	r4, r4, r0
	ldrh	r3, [r5, #132]
	ldr	r2, .L265+208
	mov	r1, #64
	mov	r0, r4
	bl	snprintf
	add	r4, r4, r0
	ldrh	r3, [r5, #178]
	ldr	r2, .L265+212
	mov	r1, #64
	mov	r0, r4
	bl	snprintf
	add	r4, r4, r0
	ldrb	r3, [r5, #182]	@ zero_extendqisi2
	ldr	r2, .L265+216
	mov	r1, #64
	mov	r0, r4
	bl	snprintf
	add	r4, r4, r0
	ldrh	r3, [r5, #176]
	ldr	r2, .L265+220
	mov	r1, #64
	mov	r0, r4
	bl	snprintf
	add	r4, r4, r0
	ldrb	r3, [r5, #184]	@ zero_extendqisi2
	ldr	r2, .L265+224
	mov	r1, #64
	mov	r0, r4
	bl	snprintf
	add	r4, r4, r0
	ldrh	r3, [r5, #180]
	ldr	r2, .L265+228
	mov	r1, #64
	mov	r0, r4
	bl	snprintf
	ldr	r3, [r5, #2720]
	add	r4, r4, r0
	ldr	r2, .L265+232
	mov	r1, #64
	mov	r0, r4
	str	r3, [sp, #4]
	ldr	r3, [r5, #2728]
	str	r3, [sp]
	ldr	r3, [r5, #2724]
	bl	snprintf
	add	r4, r4, r0
	ldr	r3, [r5, #2716]
	ldr	r2, .L265+236
	mov	r1, #64
	mov	r0, r4
	bl	snprintf
	add	r4, r4, r0
	ldr	r3, [r5, #2740]
	ldr	r2, .L265+240
	mov	r1, #64
	mov	r0, r4
	bl	snprintf
	movw	r3, #3156
	add	r4, r4, r0
	ldrh	r3, [r5, r3]
	mov	r1, #64
	ldr	r2, .L265+244
	mov	r0, r4
	bl	snprintf
	movw	r3, #3158
	add	r4, r4, r0
	ldrh	r3, [r5, r3]
	mov	r1, #64
	ldr	r2, .L265+248
	mov	r0, r4
	bl	snprintf
	add	r4, r4, r0
	ldr	r3, [r5, #3160]
	ldr	r2, .L265+252
	mov	r1, #64
	mov	r0, r4
	bl	snprintf
	movw	r3, #3164
	add	r4, r4, r0
	ldrh	r3, [r5, r3]
	mov	r1, #64
	ldr	r2, .L265+256
	mov	r0, r4
	bl	snprintf
	add	r4, r4, r0
	bl	GetFreeBlockMinEraseCount
	ldr	r2, .L265+260
	mov	r3, r0
	mov	r1, #64
	mov	r0, r4
	bl	snprintf
	add	r4, r4, r0
	ldrh	r0, [r5, #228]
	bl	GetFreeBlockMaxEraseCount
	ldr	r2, .L265+264
	mov	r3, r0
	mov	r1, #64
	mov	r0, r4
	bl	snprintf
	ldr	r3, .L265+268
	add	r4, r4, r0
	ldr	r3, [r3]
	cmp	r3, #1
	beq	.L254
.L259:
	sub	r0, r4, r6
	sub	sp, fp, #40
	ldmfd	sp, {r4, r5, r6, r7, r8, r9, r10, fp, sp, pc}
.L254:
	ldrh	r3, [r5, #176]
	movw	r2, #65535
	cmp	r3, r2
	beq	.L256
	ldr	r2, [r5, #76]
	lsl	r3, r3, #1
	mov	r0, r4
	mov	r1, #64
	ldrh	r3, [r2, r3]
	ldr	r2, .L265+272
	bl	snprintf
	add	r4, r4, r0
.L256:
	mov	r0, #0
	ldr	r9, .L265+276
	bl	List_get_gc_head_node
	uxth	r0, r0
	mov	r7, #0
	movw	r10, #65535
.L258:
	cmp	r0, r10
	beq	.L257
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
	bne	.L258
.L257:
	ldr	r3, [r5, #2536]
	mov	r7, #0
	ldr	r2, [r5, #2520]
	movw	r9, #65535
	ldr	r10, .L265+280
	sub	r3, r3, r2
	ubfx	r3, r3, #3, #16
.L260:
	cmp	r3, r9
	beq	.L259
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
	beq	.L259
	ldr	r3, [r5, #2520]
	ldrh	r3, [r3, r8]
	b	.L260
.L266:
	.align	2
.L265:
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
	blt	.L269
	ldr	r3, .L270
	mov	r1, #64
	ldr	r2, .L270+4
	mov	r0, r5
	bl	snprintf
	add	r4, r5, r0
	mov	r0, r4
	bl	FtlPrintInfo2buf
	add	r0, r4, r0
	sub	r0, r0, r5
	ldmfd	sp, {r4, r5, fp, sp, pc}
.L269:
	mov	r0, #0
	ldmfd	sp, {r4, r5, fp, sp, pc}
.L271:
	.align	2
.L270:
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
	ldr	r4, .L296
	ldr	r2, [r4, #2608]
	ldr	r3, [r4, #2620]
	cmp	r2, r3
	bcs	.L273
	ldr	r2, [r4, #2540]
	mov	r3, #0
	ldrh	r1, [r4, #244]
	str	r3, [r4, #2600]
	sub	r2, r2, #2
.L274:
	cmp	r3, r1
	bcc	.L275
	ldr	r5, [r4, #2600]
	mov	r0, r5
	bl	__udivsi3
	ldr	r3, .L296+4
	str	r0, [r4, #2608]
	ldr	r0, [r4, #2604]
	ldrh	r1, [r3]
	sub	r0, r5, r0
	bl	__udivsi3
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
	sub	sp, fp, #40
	ldmfd	sp, {r4, r5, r6, r7, r8, r9, r10, fp, sp, pc}
.L275:
	ldrh	r0, [r2, #2]!
	add	r3, r3, #1
	ldr	ip, [r4, #2600]
	add	r0, r0, ip
	str	r0, [r4, #2600]
	b	.L274
.L273:
	ldr	r3, [r4, #2616]
	cmp	r2, r3
	addhi	r3, r3, #1
	strhi	r3, [r4, #2616]
	movhi	r3, #0
	bls	.L276
.L278:
	ldrh	r2, [r4, #244]
	cmp	r3, r2
	bcs	.L276
	ldr	r0, [r4, #2540]
	lsl	r1, r3, #1
	add	r3, r3, #1
	ldrh	r2, [r0, r1]
	add	r2, r2, #1
	strh	r2, [r0, r1]	@ movhi
	b	.L278
.L281:
	ldrh	r0, [r4, #228]
	add	r0, r0, r0, lsl #1
	asr	r0, r0, #2
	bl	GetFreeBlockMaxEraseCount
	add	r3, r6, #64
	mov	r9, r0
	cmp	r0, r3
	bcc	.L283
	ldr	r3, [r4, #2524]
	cmp	r3, #0
	beq	.L283
	ldrh	r10, [r4, #244]
	movw	r1, #65535
	ldr	lr, [r4, #2520]
	mov	r0, #0
	ldr	r8, [r4, #2540]
	mov	r2, r1
.L284:
	ldrh	ip, [r3]
	movw	r5, #65535
	cmp	ip, r5
	bne	.L287
	mov	r5, r2
.L286:
	movw	r3, #65535
	cmp	r5, r3
	beq	.L283
	lsl	r3, r5, #1
	ldrh	r10, [r8, r3]
	cmp	r6, r10
	bcs	.L288
	str	r3, [fp, #-44]
	bl	GetFreeBlockMinEraseCount
	ldr	r3, [fp, #-44]
	cmp	r6, r0
	strcc	r1, [r4, #2620]
.L288:
	cmp	r7, r10
	bls	.L283
	add	r2, r10, #128
	cmp	r9, r2
	ble	.L283
	add	r2, r10, #256
	cmp	r7, r2
	bhi	.L289
	ldr	r2, [r4, #2616]
	add	r10, r10, #768
	cmp	r10, r2
	bcs	.L283
.L289:
	str	r9, [sp, #8]
	mov	r1, r5
	ldrh	r2, [r8, r3]
	ldr	r0, .L296+8
	str	r2, [sp, #4]
	ldr	r2, [r4, #76]
	ldrh	r3, [r2, r3]
	mov	r2, r7
	str	r3, [sp]
	ldr	r3, [r4, #2616]
	bl	sftl_printk
	mov	r3, #1
	str	r3, [r4, #3168]
	b	.L282
.L287:
	add	r0, r0, #1
	uxth	r0, r0
	cmp	r0, r10
	bhi	.L283
	ldrh	r5, [r3, #4]
	cmp	r5, #0
	beq	.L285
	sub	r3, r3, lr
	asr	r3, r3, #3
	uxth	r5, r3
	lsl	r3, r5, #1
	ldrh	r3, [r8, r3]
	cmp	r6, r3
	bcs	.L286
	cmp	r1, r3
	movhi	r1, r3
	movhi	r2, r5
.L285:
	add	r3, lr, ip, lsl #3
	b	.L284
.L297:
	.align	2
.L296:
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
	beq	.L299
	ldr	r3, .L300
	mov	r1, #0
	ldr	r2, [r3, #76]
	lsl	r3, r0, #1
	strh	r1, [r2, r3]	@ movhi
	bl	INSERT_FREE_LIST
.L299:
	mov	r0, #0
	ldmfd	sp, {fp, sp, pc}
.L301:
	.align	2
.L300:
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
	bne	.L303
	movw	r2, #2781
	ldr	r1, .L317
	ldr	r0, .L317+4
	bl	sftl_printk
.L303:
	ldr	r6, .L317+8
	movw	r3, #306
	ldrh	r2, [r4, #2]
	ldrh	r3, [r6, r3]
	cmp	r2, r3
	bne	.L304
	movw	r2, #2782
	ldr	r1, .L317
	ldr	r0, .L317+4
	bl	sftl_printk
.L304:
	ldrh	r3, [r4, #4]
	cmp	r3, #0
	bne	.L305
	movw	r2, #2783
	ldr	r1, .L317
	ldr	r0, .L317+4
	bl	sftl_printk
.L305:
	ldrb	r2, [r4, #6]	@ zero_extendqisi2
	mov	r3, #0
	ldrh	r0, [r6, #236]
	movw	ip, #65535
	strb	r3, [r4, #10]
	add	r2, r4, r2, lsl #1
	ldrh	r1, [r2, #16]
	mov	r2, r3
.L306:
	cmp	r1, ip
	beq	.L308
	movw	r3, #306
	ldrh	r5, [r4, #2]
	ldrh	ip, [r6, r3]
	cmp	r5, ip
	movwcs	r5, #65535
	bcs	.L302
	ldrh	r2, [r4, #4]
	orr	r5, r5, r1, lsl #10
	ldrb	r3, [r4, #6]	@ zero_extendqisi2
	movw	lr, #65535
	sub	r2, r2, #1
	uxth	r2, r2
	strh	r2, [r4, #4]	@ movhi
.L311:
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
	beq	.L311
	strb	r3, [r4, #6]
	cmp	r2, #0
	ldrh	r3, [r4, #2]
	sub	r3, r3, ip
	clz	r3, r3
	lsr	r3, r3, #5
	moveq	r3, #0
	cmp	r3, #0
	beq	.L302
	movw	r2, #2806
	ldr	r1, .L317
	ldr	r0, .L317+4
	bl	sftl_printk
.L302:
	mov	r0, r5
	ldmfd	sp, {r4, r5, r6, fp, sp, pc}
.L308:
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
	b	.L306
.L318:
	.align	2
.L317:
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
	ldr	r5, .L324
	mov	lr, #12
	mov	r4, #1
	ldr	r2, .L324+4
	mov	r6, #20
	mov	r3, #0
	str	r3, [r2, #3172]
.L320:
	ldrh	r1, [r2, #236]
	uxth	r0, r3
	add	ip, r3, #1
	cmp	r0, r1
	bcc	.L321
	ldr	lr, .L324
	mov	r0, #12
	mov	ip, #0
.L322:
	ldr	r3, [r2, #3192]
	cmp	r1, r3
	bcc	.L323
	ldmfd	sp, {r4, r5, r6, r7, r8, r9, fp, sp, pc}
.L321:
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
	ldr	r1, .L324+8
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
	b	.L320
.L323:
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
	ldr	r3, .L324+8
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
	b	.L322
.L325:
	.align	2
.L324:
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
	ldr	r3, .L333
	mov	lr, #0
	mov	r5, #20
	mov	r7, #12
	mov	r8, lr
	ldr	r6, [r3, #3192]
	ldr	r4, [r3, #3176]
.L327:
	uxth	r3, lr
	cmp	r1, r3
	ldmfdls	sp, {r4, r5, r6, r7, r8, r9, r10, fp, sp, pc}
	mla	ip, r5, r3, r0
	mov	r2, #0
.L328:
	uxth	r3, r2
	cmp	r6, r3
	bls	.L329
	mul	r3, r7, r3
	add	r2, r2, #1
	ldr	r10, [r4, r3]
	add	r9, r4, r3
	ldr	r3, [ip, #8]
	cmp	r10, r3
	bne	.L328
	str	r8, [r9, #8]
.L329:
	add	lr, lr, #1
	b	.L327
.L334:
	.align	2
.L333:
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
	ldr	r3, .L342
	mov	ip, #0
	mov	r6, #12
	mov	r7, #1
	mov	r8, #20
	ldr	r4, [r3, #3192]
	ldr	r5, [r3, #3176]
.L336:
	uxth	r2, ip
	cmp	r1, r2
	bhi	.L340
	ldmfd	sp, {r4, r5, r6, r7, r8, r9, fp, sp, pc}
.L340:
	mov	lr, #0
.L337:
	uxth	r3, lr
	cmp	r4, r3
	bls	.L338
	mla	r3, r6, r3, r5
	add	lr, lr, #1
	ldr	r9, [r3, #8]
	cmp	r9, #0
	bne	.L337
	mla	r2, r8, r2, r0
	ldr	lr, [r3]
	str	r7, [r3, #8]
	str	lr, [r2, #8]
	ldr	r3, [r3, #4]
	str	r3, [r2, #12]
.L338:
	add	ip, ip, #1
	b	.L336
.L343:
	.align	2
.L342:
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
	ldr	r2, .L349
	uxth	r0, r0
	ldr	r3, [r2, #3196]
	add	r2, r2, #3200
	ldrh	r2, [r2]
	add	r2, r3, r2, lsl #1
.L345:
	cmp	r3, r2
	bne	.L347
	mov	r0, #0
	ldmfd	sp, {fp, sp, pc}
.L347:
	ldrh	r1, [r3], #2
	cmp	r1, r0
	bne	.L345
	mov	r0, #1
	ldmfd	sp, {fp, sp, pc}
.L350:
	.align	2
.L349:
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
	ldr	r3, .L355
	mov	ip, #0
	add	lr, r3, #3200
	ldr	r1, [r3, #3196]
	ldrh	r7, [lr]
	sub	r2, r1, #2
.L352:
	uxth	r8, ip
	cmp	r8, r7
	bcc	.L354
	moveq	ip, r8
	lsleq	ip, ip, #1
	strheq	r0, [r1, ip]	@ movhi
	ldrheq	r2, [lr]
	addeq	r2, r2, #1
	strheq	r2, [lr]	@ movhi
	b	.L353
.L354:
	ldrh	r8, [r2, #2]!
	add	ip, ip, #1
	cmp	r8, r0
	bne	.L352
.L353:
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
.L356:
	.align	2
.L355:
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
	ldr	r0, .L360
	mov	r1, r4
	bl	sftl_printk
	ldr	r3, .L360+4
	ldrh	r0, [r3, #226]
	cmp	r4, r0
	beq	.L358
	ldrh	r1, [r3, #224]
	cmp	r4, r1
	beq	.L358
	movw	r2, #65535
	cmp	r0, r2
	strheq	r4, [r3, #226]	@ movhi
	beq	.L358
	cmp	r1, r2
	strheq	r4, [r3, #224]	@ movhi
.L358:
	mov	r0, #0
	ldmfd	sp, {r4, fp, sp, pc}
.L361:
	.align	2
.L360:
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
	ldr	r4, .L366
	uxth	r5, r0
	movw	r6, #3210
	mov	r0, r5
	bl	P2V_block_in_plane
	mov	r2, r5
	mov	r7, r0
	ldrh	r1, [r4, r6]
	ldr	r0, .L366+4
	bl	sftl_printk
	mov	r0, r7
	bl	FtlGcRefreshBlock
	ldrh	r3, [r4, r6]
	mov	r2, #0
	ldr	r0, .L366+8
.L363:
	uxth	r1, r2
	cmp	r3, r1
	bhi	.L365
	cmp	r3, #15
	movwls	r2, #3210
	addls	r1, r3, #1
	strhls	r1, [r4, r2]	@ movhi
	lslls	r3, r3, #1
	ldrls	r2, .L366+8
	strhls	r5, [r2, r3]	@ movhi
	b	.L364
.L365:
	add	r2, r2, #1
	add	r1, r0, r2, lsl #1
	ldrh	r1, [r1, #-2]
	cmp	r1, r5
	bne	.L363
.L364:
	mov	r0, #0
	ldmfd	sp, {r4, r5, r6, r7, fp, sp, pc}
.L367:
	.align	2
.L366:
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
	ldr	r4, .L374
	movw	r3, #3210
	ldrh	r2, [r4, r3]
	cmp	r2, #0
	beq	.L369
	ldrh	r1, [r4, #226]
	movw	r3, #65535
	cmp	r1, r3
	bne	.L369
	movw	r3, #3246
	movw	r5, #3246
	ldrh	r1, [r4, r3]
	cmp	r1, r2
	movcs	r2, #0
	strhcs	r2, [r4, r3]	@ movhi
	ldrh	r3, [r4, r5]
	ldr	r2, .L374+4
	lsl	r3, r3, #1
	ldrh	r0, [r2, r3]
	bl	P2V_block_in_plane
	bl	FtlGcRefreshBlock
	ldrh	r3, [r4, r5]
	add	r3, r3, #1
	strh	r3, [r4, r5]	@ movhi
.L369:
	mov	r0, #0
	ldmfd	sp, {r4, r5, fp, sp, pc}
.L375:
	.align	2
.L374:
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
	ldr	r1, .L377
	bl	__kmalloc
	ldmfd	sp, {fp, sp, pc}
.L378:
	.align	2
.L377:
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
.L381:
	cmp	r4, r9
	bcc	.L387
	ldr	r0, .L389
	bl	sftl_printk
	ldmfd	sp, {r4, r5, r6, r7, r8, r9, r10, fp, sp, pc}
.L387:
	cmp	r5, #0
	bne	.L382
	mov	r3, r6
	mov	r2, r7
	mov	r1, r10
	ldr	r0, .L389+4
	bl	sftl_printk
.L382:
	cmp	r8, #4
	ldreq	r1, [r7, r4, lsl #2]
	ldreq	r0, .L389+8
	beq	.L388
	cmp	r8, #2
	lsleq	r3, r4, #1
	ldreq	r0, .L389+12
	ldrbne	r1, [r7, r4]	@ zero_extendqisi2
	ldrheq	r1, [r7, r3]
	ldrne	r0, .L389+16
.L388:
	add	r5, r5, #1
	bl	sftl_printk
	cmp	r5, #15
	bls	.L386
	mov	r5, #0
	ldr	r0, .L389
	bl	sftl_printk
.L386:
	add	r4, r4, #1
	add	r6, r6, r8
	b	.L381
.L390:
	.align	2
.L389:
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
	ldr	r6, .L407
	mov	r8, r2
	mov	r4, r0
	mov	r5, r0
	ldrh	r9, [r6, #12]
	lsl	r10, r9, #3
.L392:
	cmp	r7, r8
	beq	.L396
	sub	r2, fp, #48
	sub	r1, fp, #44
	mov	r0, r5
	bl	l2p_addr_tran
	ldr	ip, [fp, #-48]
	cmp	ip, #0
	bne	.L393
	ldr	r3, [fp, #-44]
	cmp	r10, r3
	bls	.L393
	ldr	r5, .L407+4
	ldr	r6, .L407+8
	b	.L406
.L395:
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
	ldr	r0, .L407+12
	bl	rknand_print_hex
	mov	r3, #4
	ldr	r1, [r4, #-8]
	mov	r2, r3
	ldr	r0, .L407+16
	bl	rknand_print_hex
	ldr	ip, [fp, #-52]
	add	ip, ip, #1
.L406:
	cmp	ip, r8
	add	r4, r4, #20
	bne	.L395
	bl	dump_stack
.L396:
	mov	r0, #0
	sub	sp, fp, #40
	ldmfd	sp, {r4, r5, r6, r7, r8, r9, r10, fp, sp, pc}
.L393:
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
	bne	.L400
	ldr	r1, [fp, #-44]
	ldr	r3, [r6, #3252]
	ldrb	r0, [fp, #-48]	@ zero_extendqisi2
	add	r1, r9, r1
	blx	r3
	cmp	r0, #0
	mvnne	r3, #0
	strne	r3, [r5]
.L400:
	add	r7, r7, #1
	add	r5, r5, #20
	b	.L392
.L408:
	.align	2
.L407:
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
	ldr	r5, .L418
	add	r4, r5, #416
	ldrh	r3, [r4, #6]
	cmp	r3, #1024
	ldmfdeq	sp, {r4, r5, r6, r7, fp, sp, pc}
	cmp	r1, #0
	beq	.L411
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
.L411:
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
.L419:
	.align	2
.L418:
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
	ldr	r6, .L425
	add	r4, r6, #416
	ldrh	r2, [r4, #6]
	cmp	r2, #0
	movweq	r5, #65535
	beq	.L421
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
.L421:
	sub	r3, r5, #1
	movw	r2, #65533
	uxth	r3, r3
	cmp	r3, r2
	bls	.L422
	ldrh	r2, [r4, #6]
	mov	r1, r5
	ldr	r0, .L425+4
	bl	sftl_printk
.L423:
	b	.L423
.L422:
	mov	r0, r5
	ldmfd	sp, {r4, r5, r6, fp, sp, pc}
.L426:
	.align	2
.L425:
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
.L428:
	uxth	r5, r3
	cmp	r5, r1
	bcs	.L431
	mov	r7, r2
	add	r3, r3, #1
	ldrh	r6, [r7]
	add	r2, r2, #2
	cmp	r6, #0
	bne	.L428
	bl	FtlFreeSysBlkQueueOut
	sub	r3, r0, #1
	movw	r2, #65533
	uxth	r3, r3
	mov	r1, r0
	strh	r0, [r7]	@ movhi
	cmp	r3, r2
	bls	.L429
	ldr	r3, .L435
	ldr	r0, .L435+4
	ldrh	r2, [r3, #6]
	bl	sftl_printk
.L430:
	b	.L430
.L429:
	ldr	r3, [r4, #28]
	strh	r6, [r4, #2]	@ movhi
	strh	r5, [r4]	@ movhi
	add	r3, r3, #1
	str	r3, [r4, #28]
	ldrh	r3, [r4, #8]
	add	r3, r3, #1
	strh	r3, [r4, #8]	@ movhi
.L431:
	ldrh	r3, [r4, #10]
	cmp	r3, r5
	bhi	.L433
	movw	r2, #578
	ldr	r1, .L435+8
	ldr	r0, .L435+12
	bl	sftl_printk
.L433:
	mov	r0, #0
	ldmfd	sp, {r4, r5, r6, r7, fp, sp, pc}
.L436:
	.align	2
.L435:
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
	ldr	r5, .L444
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
	bls	.L438
	mov	r2, #256
	mov	r1, #255
	mov	r0, r4
	bl	memset
	mov	r0, #0
.L438:
	ldrh	r3, [r5, #14]
	cmp	r3, #4
	moveq	r3, r4
	addeq	r1, r3, r0, lsl #1
	beq	.L440
	ldmfd	sp, {r4, r5, r6, fp, sp, pc}
.L441:
	ldrh	r2, [r3]
	lsr	r2, r2, #1
	strh	r2, [r3], #2	@ movhi
.L440:
	cmp	r3, r1
	bne	.L441
	ldmfd	sp, {r4, r5, r6, fp, sp, pc}
.L445:
	.align	2
.L444:
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
	beq	.L447
	bl	memset
.L447:
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
	ldr	r4, .L552
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
	ldr	r3, .L552+4
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
.L452:
	cmp	r3, lr
	bcc	.L453
	ldr	r2, .L552+8
	mov	r1, #0
	add	r3, r2, r3, lsl #2
	add	r2, r2, #56
	add	r3, r3, #24
.L454:
	cmp	r2, r3
	bne	.L455
	ldr	r3, [r4, #3360]
	cmp	r3, #0
	bne	.L456
.L458:
	ldr	r1, .L552+12
	ldr	r0, .L552+16
	bl	sftl_printk
	mvn	r0, #0
	ldmfd	sp, {r4, r5, r6, r7, r8, r9, fp, sp, pc}
.L453:
	ldr	r0, [r4, #384]
	add	r3, r3, #1
	add	r0, r0, r1
	add	r1, r1, r2
	str	r0, [ip, #4]!
	b	.L452
.L455:
	str	r1, [r3, #4]!
	b	.L454
.L456:
	ldr	r3, [r4, #3364]
	cmp	r3, #0
	beq	.L458
	ldr	r3, [r4, #3380]
	cmp	r3, #0
	beq	.L458
	ldr	r3, [r4, #3384]
	cmp	r3, #0
	beq	.L458
	ldr	r3, [r4, #2544]
	cmp	r3, #0
	beq	.L458
	ldr	r3, [r4, #3388]
	cmp	r3, #0
	beq	.L458
	ldr	r3, [r4, #2520]
	cmp	r3, #0
	beq	.L458
	ldr	r3, [r4, #384]
	cmp	r3, #0
	beq	.L458
	ldr	r3, [r4, #76]
	cmp	r3, #0
	beq	.L458
	ldr	r3, [r4, #3196]
	cmp	r3, #0
	beq	.L458
	ldr	r3, [r4, #3204]
	cmp	r3, #0
	beq	.L458
	ldr	r3, [r4, #3292]
	cmp	r3, #0
	beq	.L458
	ldr	r3, [r4, #3300]
	cmp	r3, #0
	beq	.L458
	ldr	r3, [r4, #3272]
	cmp	r3, #0
	beq	.L458
	ldr	r3, [r4, #3188]
	cmp	r3, #0
	beq	.L458
	ldr	r3, [r4, #3296]
	cmp	r3, #0
	beq	.L458
	ldr	r3, [r4, #3304]
	cmp	r3, #0
	beq	.L458
	ldr	r3, [r4, #3308]
	cmp	r3, #0
	beq	.L458
	ldr	r3, [r4, #3312]
	cmp	r3, #0
	beq	.L458
	ldr	r3, [r4, #3180]
	cmp	r3, #0
	beq	.L458
	ldr	r3, [r4, #3316]
	cmp	r3, #0
	beq	.L458
	ldr	r3, [r4, #3320]
	cmp	r3, #0
	beq	.L458
	ldr	r3, [r4, #3176]
	cmp	r3, #0
	beq	.L458
	ldr	r3, [r4, #3336]
	cmp	r3, #0
	beq	.L458
	ldr	r3, [r4, #3340]
	cmp	r3, #0
	beq	.L458
	ldr	r3, [r4, #3184]
	cmp	r3, #0
	beq	.L458
	ldr	r3, [r4, #2540]
	cmp	r3, #0
	beq	.L458
	ldr	r3, [r4, #3348]
	cmp	r3, #0
	beq	.L458
	ldr	r3, [r4, #352]
	cmp	r3, #0
	beq	.L458
	ldr	r3, .L552
	ldr	r2, [r3, #3368]
	cmp	r2, #0
	beq	.L458
	ldr	r2, [r3, #3372]
	cmp	r2, #0
	beq	.L458
	ldr	r3, [r3, #3376]
	cmp	r3, #0
	beq	.L458
	mov	r0, #0
	ldmfd	sp, {r4, r5, r6, r7, r8, r9, fp, sp, pc}
.L553:
	.align	2
.L552:
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
	ldr	r3, .L560
	mov	r4, r0
	mov	r6, r1
	ldr	r7, .L560+4
	sub	r5, r4, #2
	ldrh	r2, [r3]
	add	r4, r4, #1020
	ldr	r8, .L560+8
	add	r4, r4, #2
	mov	r1, #0
	mov	r0, r6
	lsl	r2, r2, #2
	bl	ftl_memset
.L557:
	ldrh	r3, [r5, #2]
	movw	r2, #65535
	cmp	r3, r2
	ldmfdeq	sp, {r4, r5, r6, r7, r8, fp, sp, pc}
	ldrh	r2, [r7]
	cmp	r2, r3
	bhi	.L556
	mov	r2, #74
	mov	r1, r8
	ldr	r0, .L560+12
	bl	sftl_printk
.L556:
	ldrh	r3, [r5, #2]!
	mov	r0, #1
	cmp	r4, r5
	lsr	r1, r3, #5
	and	r3, r3, #31
	ldr	r2, [r6, r1, lsl #2]
	orr	r3, r2, r0, lsl r3
	str	r3, [r6, r1, lsl #2]
	bne	.L557
	ldmfd	sp, {r4, r5, r6, r7, r8, fp, sp, pc}
.L561:
	.align	2
.L560:
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
.L563:
	ldrh	r1, [r4, #6]
	uxth	r3, r2
	cmp	r1, r3
	bhi	.L567
	ldr	r3, .L582
	mov	r6, #0
	mov	r8, r6
	mov	r10, r6
	ldrh	r2, [r3]
	ldrh	r3, [r4]
	lsl	r3, r3, #1
	strh	r2, [r5, r3]	@ movhi
	ldrh	r9, [r5]
.L568:
	ldrh	r3, [r4, #10]
	uxth	r1, r6
	cmp	r3, r1
	bhi	.L572
	mov	r0, r8
	ldmib	sp, {r4, r5, r6, r7, r8, r9, r10, fp, sp, pc}
.L567:
	uxth	r3, r2
	mov	r1, #0
	ldr	r0, [r6, r3, lsl #2]
	ubfx	r0, r0, #10, #16
.L564:
	ldrh	ip, [r4, #10]
	uxth	r3, r1
	cmp	ip, r3
	addls	r2, r2, #1
	bls	.L563
.L566:
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
	b	.L564
.L572:
	uxth	r3, r6
	lsl	r3, r3, #1
	ldrh	r2, [r5, r3]
	cmp	r9, r2
	bls	.L569
	ldrh	r0, [r7, r3]
	add	ip, r7, r3
	cmp	r0, #0
	bne	.L570
.L571:
	add	r6, r6, #1
	b	.L568
.L569:
	cmp	r2, #0
	bne	.L571
	ldrh	r0, [r7, r3]
	add	ip, r7, r3
	cmp	r0, #0
	beq	.L571
.L573:
	mov	r1, #1
	str	ip, [fp, #-44]
	bl	FtlFreeSysBlkQueueIn
	ldr	ip, [fp, #-44]
	strh	r10, [ip]	@ movhi
	ldrh	r3, [r4, #8]
	sub	r3, r3, #1
	strh	r3, [r4, #8]	@ movhi
	b	.L571
.L570:
	subs	r9, r2, #0
	mov	r8, r1
	beq	.L573
	b	.L571
.L583:
	.align	2
.L582:
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
	ldr	r4, .L587
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
	ldr	r0, .L587+4
	mov	r2, #0
	mov	r3, r4
	mov	lr, #12
	mov	r4, r2
	sub	r6, r0, #28
.L585:
	ldrh	r7, [r0]
	uxth	r1, r2
	add	ip, r2, #1
	cmp	r7, r1
	bhi	.L586
	ldr	r2, .L587+8
	mvn	r1, #0
	movw	r0, #3396
	strh	r1, [r3, r0]	@ movhi
	strh	r1, [r2, #2]	@ movhi
	ldr	r1, [r3, #332]
	strh	r1, [r2, #10]	@ movhi
	ldr	r1, .L587+12
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
.L586:
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
	b	.L585
.L588:
	.align	2
.L587:
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
	ldr	r4, .L590
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
.L591:
	.align	2
.L590:
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
	ldr	r4, .L603
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
.L593:
	ldrh	r3, [r4, #244]
	uxth	r10, r5
	cmp	r10, r3
	bcs	.L600
	ldr	r3, .L603+4
	mov	r9, r10
	ldrh	ip, [r4, #236]
	ldrh	r3, [r3]
	str	r3, [fp, #-44]
	mov	r3, #0
	mov	r7, r3
	b	.L601
.L595:
	ldr	r2, .L603+8
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
.L601:
	uxth	r1, r3
	cmp	ip, r1
	bhi	.L595
	cmp	r7, #0
	beq	.L596
	mov	r1, r7
	mov	r0, #32768
	bl	__divsi3
	uxth	r7, r0
.L597:
	ldr	r3, [r4, #2520]
	add	r3, r3, r9, lsl #3
	strh	r7, [r3, #4]	@ movhi
	ldrh	r3, [r4, #28]
	cmp	r3, r10
	beq	.L598
	ldrh	r3, [r4, #80]
	cmp	r3, r10
	beq	.L598
	ldrh	r3, [r4, #128]
	cmp	r3, r10
	beq	.L598
	ldr	r2, [r4, #76]
	lsl	r3, r9, #1
	ldrh	r3, [r2, r3]
	cmp	r3, #0
	bne	.L599
	add	r8, r8, #1
	mov	r0, r9
	uxth	r8, r8
	bl	INSERT_FREE_LIST
.L598:
	add	r5, r5, #1
	b	.L593
.L596:
	ldr	r1, [r4, #76]
	lsl	r3, r9, #1
	mvn	r0, #0
	strh	r0, [r1, r3]	@ movhi
	b	.L597
.L599:
	add	r6, r6, #1
	mov	r0, r9
	uxth	r6, r6
	bl	INSERT_DATA_LIST
	b	.L598
.L600:
	movw	r2, #2532
	strh	r8, [r4, #228]	@ movhi
	strh	r6, [r4, r2]	@ movhi
	add	r6, r6, r8
	cmp	r6, r3
	ble	.L602
	movw	r2, #2210
	ldr	r1, .L603+12
	ldr	r0, .L603+16
	bl	sftl_printk
.L602:
	mov	r0, #0
	sub	sp, fp, #40
	ldmfd	sp, {r4, r5, r6, r7, r8, r9, r10, fp, sp, pc}
.L604:
	.align	2
.L603:
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
	ldr	r4, .L606
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
.L607:
	.align	2
.L606:
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
	ldr	r5, .L648
	mov	r8, r1
	ldr	r9, .L648+4
	mov	r4, r0
	ldr	r10, .L648+8
	ldrh	r3, [r5, #12]
	str	r3, [fp, #-52]
.L610:
	cmp	r7, r8
	bne	.L623
	mov	r0, #0
	sub	sp, fp, #40
	ldmfd	sp, {r4, r5, r6, r7, r8, r9, r10, fp, sp, pc}
.L623:
	ldr	r3, [r4, #8]
	cmp	r3, #0
	beq	.L611
	ldr	r3, [r4, #12]
	cmp	r3, #0
	bne	.L612
.L611:
	mov	r2, #96
	mov	r1, r9
	mov	r0, r10
	bl	sftl_printk
.L612:
	sub	r2, fp, #48
	sub	r1, fp, #44
	mov	r0, r4
	bl	l2p_addr_tran
	ldr	r0, [fp, #-48]
	cmp	r0, #3
	mvnhi	r3, #0
	strhi	r3, [r4]
	bhi	.L614
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
	bne	.L617
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
	beq	.L618
	ldr	r3, [r4, #12]
	ldr	r2, [r3, #12]
	cmn	r2, #1
	bne	.L619
	ldr	r2, [r3, #8]
	cmn	r2, #1
	bne	.L619
	ldr	r3, [r3]
	cmn	r3, #1
	beq	.L619
.L618:
	mvn	r3, #0
	str	r3, [r4]
.L619:
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
	bne	.L617
	ldr	r1, [r4, #4]
	ldr	r2, [fp, #-44]
	ldr	r0, .L648+12
	bl	sftl_printk
	ldr	r1, [r4, #8]
	cmp	r1, #0
	beq	.L622
	mov	r3, #4
	ldr	r0, .L648+16
	mov	r2, r3
	bl	rknand_print_hex
.L622:
	ldr	r1, [r4, #12]
	cmp	r1, #0
	beq	.L617
	mov	r3, #4
	ldr	r0, .L648+20
	mov	r2, r3
	bl	rknand_print_hex
.L617:
	ldr	r3, [r5, #3324]
	cmp	r6, r3
	bne	.L614
	ldr	r0, [r4, #8]
	cmp	r6, r0
	beq	.L614
	ldr	r3, .L648+24
	mov	r1, r6
	ldrh	r2, [r3]
	lsl	r2, r2, #9
	bl	ftl_memcpy
.L614:
	add	r7, r7, #1
	add	r4, r4, #20
	b	.L610
.L649:
	.align	2
.L648:
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
	ldr	r5, .L660
	mov	r7, #0
	add	r6, r5, #364
	ldr	r3, [r5, #3304]
	ldr	r8, [r5, #3336]
	add	r6, r6, #2
	sub	r9, r6, #64
	str	r3, [r5, #3464]
	str	r8, [r5, #3468]
.L651:
	ldr	r3, .L660+4
	ldrh	r3, [r3]
	cmp	r7, r3
	bcc	.L656
	mov	r0, #0
	ldmfd	sp, {r4, r5, r6, r7, r8, r9, r10, fp, sp, pc}
.L656:
	ldrh	r4, [r9]
	mvn	r3, #0
	ldr	r10, .L660+8
	strh	r3, [r6, #2]!	@ movhi
	add	r4, r4, r3
	uxth	r4, r4
.L652:
	ldrh	r3, [r9]
	sub	r2, r3, #16
	cmp	r4, r2
	ble	.L654
	mla	r3, r7, r3, r4
	mov	r2, #1
	mov	r1, r2
	mov	r0, r10
	lsl	r3, r3, #10
	str	r3, [r5, #3460]
	bl	FlashReadPages
	ldr	r3, [r5, #3456]
	cmn	r3, #1
	beq	.L653
	ldrh	r2, [r8]
	movw	r3, #61664
	cmp	r2, r3
	bne	.L653
	strh	r4, [r6]	@ movhi
.L654:
	add	r7, r7, #1
	b	.L651
.L653:
	sub	r4, r4, #1
	uxth	r4, r4
	b	.L652
.L661:
	.align	2
.L660:
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
	ldr	r3, .L673
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
	beq	.L666
.L665:
	mov	r0, r5
	sub	sp, fp, #32
	ldmfd	sp, {r4, r5, r6, r7, r8, fp, sp, pc}
.L669:
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
	bne	.L667
	ldr	r3, [fp, #-92]
	cmn	r3, #1
	bne	.L667
	ldr	r3, [fp, #-116]
	cmn	r3, #1
	subne	r4, r4, #1
	sxthne	r5, r4
	bne	.L666
.L667:
	add	r4, r4, #1
	sxth	r8, r4
.L666:
	cmp	r8, r5
	ble	.L669
	b	.L665
.L674:
	.align	2
.L673:
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
	ldr	r4, .L757
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
.L676:
	ldrh	r3, [r5, #246]
	ldr	r2, [fp, #-44]
	cmp	r3, r2
	bls	.L717
	ldr	r8, .L757+4
	mov	r6, #0
	ldrh	ip, [r5, #236]
	mov	r10, r6
	ldr	r9, [r5, #3292]
	ldrh	r1, [r8], #-52
	ldr	r2, [r5, #3180]
	ldr	r3, [r5, #3184]
	str	r1, [fp, #-48]
	b	.L718
.L678:
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
	bne	.L677
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
.L677:
	add	r6, r6, #1
.L718:
	uxth	r1, r6
	cmp	ip, r1
	bhi	.L678
	cmp	r10, #0
	bne	.L679
.L716:
	ldr	r3, [fp, #-44]
	add	r3, r3, #1
	uxth	r3, r3
	str	r3, [fp, #-44]
	b	.L676
.L679:
	mov	r2, #1
	mov	r1, r10
	mov	r0, r9
	bl	FlashReadPages
	mov	r3, #0
.L756:
	str	r3, [fp, #-48]
	ldrh	r3, [fp, #-48]
	cmp	r10, r3
	bls	.L716
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
	bne	.L683
	mov	r3, #16
.L685:
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
	bne	.L682
	ldr	r3, [r4, #3292]
	mvn	r2, #0
	str	r2, [r3, r9]
	ldr	r3, [r4, #3292]
	ldr	r3, [r3, r9]
	cmp	r3, r2
	bne	.L683
.L684:
	mov	r1, #1
	b	.L755
.L682:
	ldr	r2, [r4, #3292]
	ldr	r2, [r2, r9]
	cmn	r2, #1
	bne	.L683
	sub	r3, r3, #1
	uxth	r3, r3
	cmp	r3, #0
	bne	.L685
	b	.L684
.L683:
	ldr	r2, [r5, #2592]
	ldr	r3, [r6, #4]
	cmn	r2, #1
	beq	.L686
	cmp	r2, r3
	bhi	.L687
.L686:
	cmn	r3, #1
	addne	r2, r3, #1
	strne	r2, [r7, #2592]
.L687:
	ldrh	r2, [r6]
	movw	r1, #61604
	cmp	r2, r1
	beq	.L689
	bhi	.L690
	movw	r3, #61574
	cmp	r2, r3
	beq	.L691
.L688:
	ldr	r3, [fp, #-48]
	add	r3, r3, #1
	b	.L756
.L690:
	movw	r3, #61634
	cmp	r2, r3
	beq	.L692
	movw	r3, #65535
	cmp	r2, r3
	moveq	r1, #0
	bne	.L688
.L755:
	uxth	r0, ip
	bl	FtlFreeSysBlkQueueIn
	b	.L688
.L692:
	ldr	r9, .L757+8
	ldr	r3, [r4, #332]
	ldrh	r2, [r9]
	cmp	r2, r3
	bls	.L694
	movw	r2, #1225
	ldr	r1, .L757+12
	ldr	r0, .L757+16
	bl	sftl_printk
.L694:
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
.L695:
	ldr	r1, [fp, #-52]
	cmp	r3, r1
	bgt	.L701
	cmp	r3, #0
	bge	.L733
	b	.L688
.L701:
	lsl	lr, r3, #2
	ldr	r1, [r6, #4]
	str	lr, [fp, #-56]
	ldr	lr, [ip, r3, lsl #2]
	cmp	r1, lr
	bls	.L696
	ldr	r1, [ip]
	cmp	r1, #0
	bne	.L697
	cmp	r0, r2
	addne	r2, r2, #1
	strhne	r2, [r9]	@ movhi
.L697:
	uxth	ip, r3
	mov	r1, #0
.L698:
	uxth	r0, r1
	sxth	r2, r1
	cmp	ip, r0
	bhi	.L699
	ldr	r1, [r6, #4]
	cmp	r3, #0
	ldr	r2, [r7, #3384]
	ldr	r0, [fp, #-56]
	str	r1, [r2, r0]
	lsl	r2, r3, #1
	ldr	r1, [r7, #3360]
	strh	r8, [r1, r2]	@ movhi
	blt	.L688
	ldr	r2, .L757+8
	ldr	r1, [r7, #332]
	ldrh	r2, [r2]
	sub	r1, r1, r2
	sub	r1, r1, #1
	sxth	r1, r1
	cmp	r3, r1
	bgt	.L688
.L733:
	add	r2, r2, #1
	ldr	r1, [r6, #4]
	strh	r2, [r9]	@ movhi
	ldr	r2, [r4, #3384]
	str	r1, [r2, r3, lsl #2]
	lsl	r3, r3, #1
	ldr	r2, [r4, #3360]
.L753:
	strh	r8, [r2, r3]	@ movhi
	b	.L688
.L699:
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
	b	.L698
.L696:
	sub	r3, r3, #1
	sxth	r3, r3
	b	.L695
.L691:
	ldr	r9, .L757+20
	ldrh	r2, [r9]
	ldrh	r3, [r9, #-24]
	cmp	r2, r3
	bls	.L704
	movw	r2, #1266
	ldr	r1, .L757+12
	ldr	r0, .L757+16
	bl	sftl_printk
.L704:
	ldrh	r2, [r9, #-24]
	ldrh	r1, [r9]
	ldr	ip, [r4, #3372]
	sub	r0, r2, #1
	sxth	r3, r0
	sub	r0, r0, r1
	str	r0, [fp, #-52]
.L705:
	ldr	r0, [fp, #-52]
	cmp	r3, r0
	ble	.L710
	lsl	lr, r3, #2
	ldr	r0, [r6, #4]
	str	lr, [fp, #-56]
	ldr	lr, [ip, r3, lsl #2]
	cmp	r0, lr
	bls	.L706
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
.L708:
	uxth	r0, r1
	sxth	r2, r1
	cmp	ip, r0
	bhi	.L709
	ldr	r1, [r6, #4]
	ldr	r2, [r7, #3372]
	ldr	r0, [fp, #-56]
	str	r1, [r2, r0]
	lsl	r2, r3, #1
	ldr	r1, [r7, #352]
	strh	r8, [r1, r2]	@ movhi
.L710:
	cmp	r3, #0
	blt	.L688
	ldr	r0, .L757+20
	ldrh	r2, [r0, #-24]
	ldrh	r1, [r0]
	sub	r2, r2, #1
	sub	r2, r2, r1
	sxth	r2, r2
	cmp	r3, r2
	bgt	.L688
	add	r1, r1, #1
	ldr	r2, [r4, #3372]
	strh	r1, [r0]	@ movhi
	ldr	r1, [r6, #4]
	str	r1, [r2, r3, lsl #2]
	lsl	r3, r3, #1
	ldr	r2, [r4, #352]
	b	.L753
.L709:
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
	b	.L708
.L706:
	sub	r3, r3, #1
	sxth	r3, r3
	b	.L705
.L689:
	ldr	r9, .L757+24
	movw	r2, #65535
	ldrh	r1, [r9]
	cmp	r1, r2
	strheq	r8, [r9]	@ movhi
	beq	.L754
	ldrh	r0, [r9, #4]
	cmp	r0, r2
	beq	.L713
	mov	r1, #1
	bl	FtlFreeSysBlkQueueIn
.L713:
	ldr	r3, [r6, #4]
	ldr	r2, [r4, #2636]
	cmp	r2, r3
	strhcs	r8, [r9, #4]	@ movhi
	bcs	.L688
	ldrh	r3, [r9]
	strh	r8, [r9]	@ movhi
	strh	r3, [r9, #4]	@ movhi
	ldr	r3, [r6, #4]
.L754:
	str	r3, [r4, #2636]
	b	.L688
.L717:
	ldr	r2, [r5, #3360]
	ldrh	r3, [r2]
	cmp	r3, #0
	beq	.L719
.L722:
	ldr	r0, [r4, #352]
	ldrh	r2, [r0]
	cmp	r2, #0
	beq	.L720
.L721:
	ldr	r3, .L757+8
	ldrh	r2, [r3]
	ldr	r3, [r4, #332]
	cmp	r2, r3
	bls	.L752
	movw	r2, #1391
	ldr	r1, .L757+12
	ldr	r0, .L757+16
	bl	sftl_printk
.L752:
	mov	r0, #0
	sub	sp, fp, #40
	ldmfd	sp, {r4, r5, r6, r7, r8, r9, r10, fp, sp, pc}
.L719:
	ldr	r1, .L757+8
	ldrh	r1, [r1]
	cmp	r1, #0
	ldrne	r1, [r5, #332]
	beq	.L722
.L723:
	sxth	ip, r3
	cmp	ip, r1
	bcs	.L722
	lsl	r0, ip, #1
	add	r3, r3, #1
	ldrh	r0, [r2, r0]
	cmp	r0, #0
	beq	.L723
	mov	r3, ip
	mov	lr, #0
.L724:
	ldr	r2, [r7, #332]
	cmp	r3, r2
	bcs	.L722
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
	b	.L724
.L720:
	ldr	r3, .L757+20
	ldrh	r1, [r3]
	cmp	r1, #0
	ldrhne	ip, [r3, #-24]!
	movne	r1, r3
	beq	.L721
.L729:
	sxth	r3, r2
	cmp	r3, ip
	mov	lr, r3
	bge	.L721
	lsl	r5, r3, #1
	add	r2, r2, #1
	ldrh	r5, [r0, r5]
	cmp	r5, #0
	beq	.L729
	mov	r5, #0
.L730:
	ldrh	r2, [r1]
	cmp	r3, r2
	bge	.L721
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
	b	.L730
.L758:
	.align	2
.L757:
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
	ldr	r4, .L791
	ldr	r8, .L791+4
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
.L760:
	ldrh	r3, [r8]
	sub	r3, r3, #16
	cmp	r5, r3
	ble	.L763
	lsl	r3, r5, #10
	mov	r2, #1
	mov	r1, r2
	mov	r0, r7
	str	r3, [r4, #3460]
	bl	FlashReadPages
	ldr	r3, [r4, #3456]
	cmn	r3, #1
	bne	.L761
	ldr	r3, [r4, #3460]
	mov	r2, #1
	mov	r1, r2
	mov	r0, r7
	add	r3, r3, #1
	str	r3, [r4, #3460]
	bl	FlashReadPages
.L761:
	ldr	r3, [r4, #3456]
	cmn	r3, #1
	beq	.L762
	ldrh	r2, [r6]
	movw	r3, #61649
	cmp	r2, r3
	bne	.L762
	ldr	r2, [r6, #4]
	ldr	r3, .L791+8
	str	r2, [r4, #364]
	ldrh	r2, [r6, #8]
	strh	r5, [r3]	@ movhi
	strh	r2, [r3, #4]	@ movhi
.L763:
	ldr	r5, .L791+8
	movw	r2, #65535
	ldrh	r3, [r5]
	cmp	r3, r2
	beq	.L777
	ldrh	r3, [r5, #4]
	cmp	r3, r2
	beq	.L767
	lsl	r3, r3, #10
	mov	r2, #1
	mov	r1, r2
	ldr	r0, .L791+12
	str	r3, [r4, #3460]
	bl	FlashReadPages
	ldr	r3, [r4, #3456]
	cmn	r3, #1
	beq	.L767
	ldrh	r2, [r6]
	movw	r3, #61649
	cmp	r2, r3
	bne	.L767
	ldr	r3, [r6, #4]
	ldr	r2, [r4, #364]
	cmp	r3, r2
	ldrhhi	r2, [r5, #4]
	strhi	r3, [r4, #364]
	ldrhhi	r3, [r6, #8]
	strhhi	r2, [r5]	@ movhi
	strhhi	r3, [r5, #4]	@ movhi
.L767:
	ldr	r8, .L791+12
	mov	r1, #1
	ldrh	r0, [r5]
	bl	FtlGetLastWrittenPage
	sxth	r7, r0
	add	r0, r0, #1
	strh	r0, [r5, #2]	@ movhi
.L769:
	cmp	r7, #0
	bge	.L772
	mov	r2, #251
	ldr	r1, .L791+16
	ldr	r0, .L791+20
	bl	sftl_printk
.L771:
	ldrh	r3, [r6, #10]
	ldrh	r0, [r6, #12]
	strh	r3, [r5, #6]	@ movhi
	movw	r3, #65535
	cmp	r0, r3
	beq	.L774
	ldr	r2, [r4, #232]
	cmp	r0, r2
	beq	.L774
	ldrh	r3, [r4, #246]
	lsr	r3, r3, #2
	cmp	r0, r3
	cmpcc	r2, r3
	bcs	.L774
	bl	FtlSysBlkNumInit
.L774:
	ldr	r6, .L791+24
	mov	r5, #0
	ldr	r8, .L791+28
	sub	r7, r6, #122
.L775:
	ldrh	r3, [r7]
	cmp	r5, r3
	bcc	.L776
	mov	r0, #0
	ldmfd	sp, {r4, r5, r6, r7, r8, fp, sp, pc}
.L762:
	sub	r5, r5, #1
	uxth	r5, r5
	b	.L760
.L772:
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
	beq	.L770
	ldrh	r2, [r6]
	movw	r3, #61649
	cmp	r2, r3
	beq	.L771
.L770:
	sub	r7, r7, #1
	sxth	r7, r7
	b	.L769
.L776:
	ldrh	r2, [r8]
	ldr	r1, [r4, #3464]
	ldr	r0, [r6, #4]!
	lsl	r2, r2, #2
	mla	r1, r5, r2, r1
	add	r5, r5, #1
	bl	ftl_memcpy
	b	.L775
.L777:
	mvn	r0, #0
	ldmfd	sp, {r4, r5, r6, r7, r8, fp, sp, pc}
.L792:
	.align	2
.L791:
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
	ldr	r6, .L832
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
.L794:
	ldr	r3, [fp, #-76]
	cmp	r8, r3
	bne	.L808
	ldr	r3, [fp, #-88]
	cmp	r3, #0
	beq	.L802
	mov	r5, #0
	ldr	r6, .L832+4
	ldr	r7, .L832+8
	b	.L809
.L808:
	ldr	r3, [r5, #8]
	cmp	r3, #0
	beq	.L795
	ldr	r3, [r5, #12]
	cmp	r3, #0
	bne	.L796
.L795:
	mov	r2, #148
	ldr	r1, .L832+12
	ldr	r0, .L832+16
	bl	sftl_printk
.L796:
	sub	r2, fp, #68
	sub	r1, fp, #64
	mov	r0, r5
	bl	l2p_addr_tran
	ldr	r7, [fp, #-68]
	cmp	r7, #3
	bls	.L797
.L831:
	mvn	r3, #0
	str	r3, [r5]
	b	.L798
.L797:
	cmp	r7, #0
	bne	.L799
	ldr	r3, [fp, #-64]
	ldr	r2, [fp, #-80]
	cmp	r2, r3
	bls	.L799
	ldr	r5, .L832+12
	ldr	r6, .L832+20
	b	.L830
.L801:
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
	ldr	r0, .L832+24
	bl	rknand_print_hex
	mov	r3, #4
	ldr	r1, [r4, #-8]
	mov	r2, r3
	ldr	r0, .L832+28
	bl	rknand_print_hex
.L830:
	ldr	r3, [fp, #-76]
	add	r4, r4, #20
	cmp	r7, r3
	bne	.L801
	bl	dump_stack
.L802:
	mov	r0, #0
	sub	sp, fp, #40
	ldmfd	sp, {r4, r5, r6, r7, r8, r9, r10, fp, sp, pc}
.L799:
	ldr	r1, [r5, #8]
	tst	r1, #63
	moveq	r7, r1
	beq	.L803
	ldr	r7, [r10, #3324]
	cmp	r1, r7
	beq	.L803
	ldr	r3, .L832+32
	mov	r0, r7
	ldrh	r2, [r3]
	lsl	r2, r2, #9
	bl	ftl_memcpy
.L803:
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
	bne	.L798
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
	bne	.L831
.L798:
	add	r8, r8, #1
	add	r5, r5, #20
	b	.L794
.L814:
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
	bne	.L810
	ldr	r1, [r4, #4]
	mov	r0, r6
	bl	sftl_printk
	str	r10, [r4]
.L810:
	ldr	r10, [fp, #-60]
	cmp	r10, #256
	bne	.L811
	ldr	r1, [r4, #4]
	mov	r0, r7
	bl	sftl_printk
	str	r10, [r4]
.L811:
	ldr	r3, [r4, #12]
	cmp	r3, #0
	beq	.L812
	ldr	r2, [r3]
	ldr	r3, .L832
	ldr	r3, [r3, #3332]
	ldr	r3, [r3]
	cmp	r2, r3
	beq	.L812
	ldr	r1, [r4, #4]
	ldr	r0, .L832+36
	bl	sftl_printk
	mvn	r3, #0
	str	r3, [r4]
.L812:
	ldr	r3, [r4, #8]
	cmp	r3, #0
	beq	.L813
	ldr	r2, [r3]
	ldr	r3, .L832
	ldr	r3, [r3, #3324]
	ldr	r3, [r3]
	cmp	r2, r3
	beq	.L813
	ldr	r1, [r4, #4]
	ldr	r0, .L832+40
	bl	sftl_printk
	mvn	r3, #0
	str	r3, [r4]
.L813:
	add	r5, r5, #1
	add	r4, r4, #20
.L809:
	cmp	r8, r5
	bne	.L814
	b	.L802
.L833:
	.align	2
.L832:
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
	ldr	ip, .L876
	uxtb	r5, r1
	uxth	r3, r0
	mov	r6, r10
	mov	r4, r10
	mov	r9, ip
	mov	r8, #20
	str	r3, [fp, #-44]
	str	r3, [ip, #3288]
.L835:
	ldrh	r1, [r9, #236]
	uxth	r3, r10
	cmp	r1, r3
	bhi	.L839
	cmp	r6, #0
	beq	.L834
	mov	r7, #0
	mov	r8, #20
	mov	r2, r6
	mov	r1, #0
	ldr	r0, [r9, #3272]
	bl	FlashEraseBlocks
.L842:
	uxth	r3, r7
	cmp	r6, r3
	bhi	.L844
.L845:
	cmp	r5, #0
	mov	r7, #0
	ldrne	r3, .L876+4
	moveq	r3, #2
	streq	r5, [fp, #-48]
	streq	r3, [fp, #-52]
	ldrhne	r3, [r3]
	strne	r3, [fp, #-52]
	movne	r3, #1
	strne	r3, [fp, #-48]
.L854:
	mov	r10, #0
	mov	r6, r10
.L846:
	ldrh	r1, [r9, #236]
	uxth	r3, r10
	cmp	r1, r3
	bhi	.L849
	cmp	r6, #0
	beq	.L834
	mov	r8, #0
	mov	r10, #20
	mov	r3, #1
	ldr	r2, [fp, #-48]
	mov	r1, r6
	ldr	r0, [r9, #3272]
	bl	FlashProgPages
.L851:
	uxth	r3, r8
	cmp	r6, r3
	bhi	.L853
	add	r7, r7, #1
	ldr	r2, [fp, #-52]
	uxth	r3, r7
	cmp	r2, r3
	bhi	.L854
	mov	r7, #0
	mov	r8, #20
.L855:
	uxth	r3, r7
	cmp	r6, r3
	bhi	.L857
	ldr	r3, [fp, #-44]
	adds	r5, r5, #0
	movne	r5, #1
	cmp	r3, #63
	orrls	r5, r5, #1
	cmp	r5, #0
	beq	.L834
	mov	r2, r6
	ldr	r1, [fp, #-48]
	ldr	r0, [r9, #3272]
	bl	FlashEraseBlocks
.L834:
	mov	r0, r4
	sub	sp, fp, #40
	ldmfd	sp, {r4, r5, r6, r7, r8, r9, r10, fp, sp, pc}
.L839:
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
	beq	.L836
	bl	IsBlkInVendorPart
	cmp	r0, #0
	bne	.L837
.L836:
	mov	r0, r7
	bl	FtlBbmIsBadBlock
	cmp	r0, #0
	addne	r4, r4, #1
	uxthne	r4, r4
	bne	.L837
	ldr	r1, [r9, #3272]
	lsl	r7, r7, #10
	ldr	r3, [r9, #3320]
	mla	r1, r8, r6, r1
	str	r3, [r1, #8]
	ldr	r3, .L876+8
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
.L837:
	add	r10, r10, #1
	b	.L835
.L844:
	mul	r3, r8, r7
	ldr	r2, [r9, #3272]
	add	r1, r2, r3
	ldr	r3, [r2, r3]
	cmn	r3, #1
	bne	.L843
	ldr	r0, [r1, #4]
	add	r4, r4, #1
	uxth	r4, r4
	ubfx	r0, r0, #10, #16
	bl	FtlBbmMapBadBlock
.L843:
	add	r7, r7, #1
	b	.L842
.L849:
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
	beq	.L847
	bl	IsBlkInVendorPart
	cmp	r0, #0
	bne	.L848
.L847:
	mov	r0, r8
	bl	FtlBbmIsBadBlock
	cmp	r0, #0
	bne	.L848
	ldr	r1, [r9, #3272]
	mov	r3, #20
	add	r8, r7, r8, lsl #10
	mla	r1, r3, r6, r1
	ldr	r3, [r9, #3316]
	str	r3, [r1, #8]
	ldr	r3, .L876+8
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
.L848:
	add	r10, r10, #1
	b	.L846
.L853:
	mul	r3, r10, r8
	ldr	r2, [r9, #3272]
	add	r1, r2, r3
	ldr	r3, [r2, r3]
	cmp	r3, #0
	beq	.L852
	ldr	r0, [r1, #4]
	add	r4, r4, #1
	uxth	r4, r4
	ubfx	r0, r0, #10, #16
	bl	FtlBbmMapBadBlock
.L852:
	add	r8, r8, #1
	b	.L851
.L857:
	cmp	r5, #0
	beq	.L856
	mul	r3, r8, r7
	ldr	r2, [r9, #3272]
	add	r1, r2, r3
	ldr	r3, [r2, r3]
	cmp	r3, #0
	bne	.L856
	ldr	r0, [r1, #4]
	mov	r1, #1
	ubfx	r0, r0, #10, #16
	bl	FtlFreeSysBlkQueueIn
.L856:
	add	r7, r7, #1
	b	.L855
.L877:
	.align	2
.L876:
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
	bls	.L878
	ldr	r3, .L883
	sub	r0, fp, #84
	mov	r2, #32
	mov	r1, #165
	str	r0, [fp, #-92]
	lsl	r5, r5, #10
	ldr	r4, [r3, #3328]
	str	r4, [fp, #-96]
	bl	memset
	ldr	r3, .L883+4
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
	bne	.L880
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
.L880:
	mov	r2, #1
	mov	r1, #0
	sub	r0, fp, #104
	str	r5, [fp, #-100]
	bl	FlashEraseBlocks
.L878:
	mov	r0, r4
	sub	sp, fp, #20
	ldmfd	sp, {r4, r5, fp, sp, pc}
.L884:
	.align	2
.L883:
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
	ldr	r4, .L897
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
.L886:
	ldrh	r3, [r7]
	cmp	r5, r3
	blt	.L887
	ldr	r6, [r4, #3468]
	mov	r2, #16
	mov	r1, #255
	ldr	r5, .L897+4
	ldr	r10, .L897+8
	mov	r7, #0
	mov	r0, r6
	mov	r8, r7
	bl	memset
	ldr	r3, .L897+12
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
.L888:
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
	blt	.L889
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
.L889:
	mov	r3, #1
	ldr	r0, .L897+16
	mov	r2, r3
	mov	r1, r3
	bl	FlashProgPages
	ldrh	r3, [r5, #2]
	add	r3, r3, #1
	strh	r3, [r5, #2]	@ movhi
	ldr	r3, [r4, #3456]
	cmn	r3, #1
	bne	.L890
	add	r7, r7, #1
	ldr	r1, [r4, #3460]
	uxth	r7, r7
	ldr	r0, .L897+20
	bl	sftl_printk
	cmp	r7, #3
	bls	.L888
	mov	r2, r7
	ldr	r1, [r4, #3460]
	ldr	r0, .L897+24
	bl	sftl_printk
.L892:
	b	.L892
.L887:
	ldrh	r2, [r8]
	ldr	r3, [r4, #3464]
	ldr	r1, [r6, #4]!
	mul	r0, r2, r5
	lsl	r2, r2, #2
	add	r5, r5, #1
	add	r0, r3, r0, lsl #2
	bl	ftl_memcpy
	b	.L886
.L893:
	mov	r8, #1
	b	.L888
.L890:
	add	r8, r8, #1
	cmp	r8, #1
	ble	.L893
	cmp	r3, #256
	beq	.L888
	mov	r0, #0
	sub	sp, fp, #40
	ldmfd	sp, {r4, r5, r6, r7, r8, r9, r10, fp, sp, pc}
.L898:
	.align	2
.L897:
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
	ldr	r9, .L935
.L900:
	ldr	r3, .L935
	ldr	r10, .L935+4
	ldrh	r2, [r3, #228]
	ldrh	r1, [r10]
	add	r2, r2, r1
	ldrh	r1, [r3, #244]
	cmp	r2, r1
	ble	.L901
	mov	r2, #2656
	ldr	r1, .L935+8
	ldr	r0, .L935+12
	bl	sftl_printk
.L901:
	ldr	r2, .L935+16
	cmp	r4, r2
	movne	r1, #0
	bne	.L902
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
.L902:
	ldr	r0, .L935+20
	bl	List_pop_index_node
	ldrh	r2, [r9, #228]
	mov	r7, r0
	uxth	r8, r0
	cmp	r2, #0
	bne	.L903
	movw	r2, #2665
	ldr	r1, .L935+8
	ldr	r0, .L935+12
	bl	sftl_printk
.L903:
	ldrh	r2, [r9, #228]
	sub	r2, r2, #1
	strh	r2, [r9, #228]	@ movhi
	ldrh	r2, [r9, #244]
	cmp	r2, r8
	bls	.L900
	uxth	r7, r7
	ldr	r2, [r9, #76]
	lsl	r6, r7, #1
	ldrh	r5, [r2, r6]
	cmp	r5, #0
	bne	.L900
	strh	r8, [r4]	@ movhi
	mov	r0, r4
	bl	make_superblock
	ldrb	r2, [r4, #7]	@ zero_extendqisi2
	cmp	r2, #0
	bne	.L905
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
	ble	.L900
	movw	r2, #2679
	ldr	r1, .L935+8
	ldr	r0, .L935+12
	bl	sftl_printk
	b	.L900
.L905:
	ldrh	r1, [r10]
	ldrh	r2, [r9, #228]
	add	r2, r2, r1
	ldrh	r1, [r9, #244]
	cmp	r2, r1
	ble	.L907
	movw	r2, #2682
	ldr	r1, .L935+8
	ldr	r0, .L935+12
	bl	sftl_printk
.L907:
	ldr	r0, [r9, #3272]
	mov	r1, #20
	ldrh	lr, [r9, #236]
	add	ip, r4, #16
	str	r1, [fp, #-44]
	mov	r2, r0
	mla	r3, r1, lr, r0
	mov	lr, #0
.L908:
	cmp	r3, r2
	bne	.L910
	cmp	r5, #0
	bne	.L911
	movw	r2, #2693
	ldr	r1, .L935+8
	ldr	r0, .L935+12
	bl	sftl_printk
.L911:
	ldrh	r2, [r9, #176]
	cmp	r2, r8
	bne	.L912
	movw	r2, #2695
	ldr	r1, .L935+8
	ldr	r0, .L935+12
	bl	sftl_printk
.L912:
	ldrb	r2, [r4, #8]	@ zero_extendqisi2
	ldr	r1, [r9, #2540]
	ldr	r10, .L935+24
	cmp	r2, #0
	ldrh	r2, [r1, r6]
	bne	.L913
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
.L916:
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
	ldr	ip, .L935+28
	add	r2, r2, #1
	str	r2, [r1, #16]
	mov	r1, #20
	mla	r1, r1, r5, r0
	add	r2, r0, #4
	add	r1, r1, #24
.L918:
	add	r2, r2, #20
	cmp	r1, r2
	bne	.L919
	ldrb	r1, [r4, #8]	@ zero_extendqisi2
	mov	r2, r5
	ldr	r0, [r9, #3272]
	mov	r10, #0
	bl	FlashEraseBlocks
	mov	r1, r10
	mov	ip, #20
.L920:
	uxth	r2, r10
	cmp	r5, r2
	bhi	.L922
	cmp	r1, #0
	ble	.L923
	mov	r0, r7
	bl	update_multiplier_value
	bl	FtlBbmTblFlush
.L923:
	ldrb	r1, [r4, #7]	@ zero_extendqisi2
	cmp	r1, #0
	bne	.L924
	ldr	r2, [r9, #76]
	mvn	r1, #0
	strh	r1, [r2, r6]	@ movhi
	b	.L900
.L910:
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
	b	.L908
.L913:
	add	r2, r2, #1
	mov	r0, r7
	strh	r2, [r1, r6]	@ movhi
	ldr	r2, [r9, #2604]
	add	r2, r2, #1
	str	r2, [r9, #2604]
	bl	ftl_set_blk_mode.part.6
	b	.L916
.L919:
	ldr	r0, [r2, #-20]
	and	r0, r0, ip
	str	r0, [r2, #-20]
	b	.L918
.L922:
	mul	r2, ip, r10
	ldr	lr, [r9, #3272]
	add	r0, lr, r2
	ldr	r2, [lr, r2]
	cmn	r2, #1
	bne	.L921
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
.L921:
	add	r10, r10, #1
	b	.L920
.L924:
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
	beq	.L925
	ldrb	r3, [r4, #7]	@ zero_extendqisi2
	cmp	r3, #0
	bne	.L926
.L925:
	movw	r2, #2748
	ldr	r1, .L935+8
	ldr	r0, .L935+12
	bl	sftl_printk
.L926:
	mov	r0, #0
	sub	sp, fp, #40
	ldmfd	sp, {r4, r5, r6, r7, r8, r9, r10, fp, sp, pc}
.L936:
	.align	2
.L935:
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
	ldr	r6, .L949
	str	r3, [fp, #-44]
	movw	r3, #3210
	ldrh	r3, [r6, r3]
	cmp	r3, #0
	movne	r7, #0
	ldrne	r10, .L949+4
	bne	.L939
.L938:
	mov	r0, #0
	sub	sp, fp, #40
	ldmfd	sp, {r4, r5, r6, r7, r8, r9, r10, fp, sp, pc}
.L945:
	uxtah	r3, r6, r7
	ldr	r1, [fp, #-44]
	mov	r8, #0
	ldrb	r0, [r3, #264]	@ zero_extendqisi2
	bl	V2P_block
	mov	r9, r0
.L940:
	ldrh	r3, [r10]
	uxth	r4, r8
	cmp	r3, r4
	bhi	.L944
	add	r7, r7, #1
.L939:
	ldrh	r2, [r6, #236]
	uxth	r3, r7
	cmp	r2, r3
	bhi	.L945
	bl	FtlGcReFreshBadBlk
	b	.L938
.L944:
	uxth	r3, r8
	ldr	r5, .L949+8
	lsl	r2, r3, #1
	ldrh	r2, [r5, r2]
	cmp	r2, r9
	bne	.L941
	mov	r1, r9
	ldr	r0, .L949+12
	str	r3, [fp, #-48]
	bl	sftl_printk
	mov	r0, r9
	bl	FtlBbmMapBadBlock
	bl	FtlBbmTblFlush
	ldr	r3, [fp, #-48]
	ldrh	r2, [r10]
	add	r3, r5, r3, lsl #1
.L942:
	cmp	r4, r2
	bcc	.L943
	sub	r2, r2, #1
	strh	r2, [r10]	@ movhi
.L941:
	add	r8, r8, #1
	b	.L940
.L943:
	ldrh	r1, [r3, #2]!
	add	r4, r4, #1
	uxth	r4, r4
	strh	r1, [r3, #-2]	@ movhi
	b	.L942
.L950:
	.align	2
.L949:
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
	ldr	r4, .L963
	uxth	r5, r0
	ldr	r2, [r4, #76]
	lsl	r3, r5, #1
	ldrh	r3, [r2, r3]
	cmp	r3, #0
	bne	.L952
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
	beq	.L956
	ldrh	r3, [r4, #28]
	cmp	r3, r5
	beq	.L961
	ldrh	r3, [r4, #80]
	cmp	r3, r5
	beq	.L961
	ldrh	r3, [r4, #128]
	cmp	r3, r5
	beq	.L961
.L956:
	mov	r1, r5
	ldr	r0, .L963+4
	bl	List_remove_node
	movw	r3, #2532
	ldrh	r3, [r4, r3]
	cmp	r3, #0
	bne	.L958
	movw	r2, #2824
	ldr	r1, .L963+8
	ldr	r0, .L963+12
	bl	sftl_printk
.L958:
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
	ble	.L962
	movw	r2, #2827
	ldr	r1, .L963+8
	ldr	r0, .L963+12
	bl	sftl_printk
.L962:
	mov	r0, #1
	ldmfd	sp, {r4, r5, r6, fp, sp, pc}
.L952:
	mov	r0, r5
	bl	List_update_data_list
.L961:
	mov	r0, #0
	ldmfd	sp, {r4, r5, r6, fp, sp, pc}
.L964:
	.align	2
.L963:
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
	ldr	r4, .L977
	beq	.L966
	ldr	r3, [r4, #76]
	lsl	r5, r6, #1
	ldrh	r2, [r3, r5]
	cmp	r2, #0
	subne	r2, r2, #1
	strhne	r2, [r3, r5]	@ movhi
	bne	.L966
	mov	r1, r6
	ldr	r0, .L977+4
	bl	sftl_printk
	ldr	r3, [r4, #76]
	ldrh	r5, [r3, r5]
	cmp	r5, #0
	beq	.L968
.L975:
	mov	r5, #0
.L965:
	mov	r0, r5
	ldmfd	sp, {r4, r5, r6, r7, fp, sp, pc}
.L968:
	movw	r2, #2842
.L976:
	ldr	r1, .L977+8
	ldr	r0, .L977+12
	bl	sftl_printk
	b	.L965
.L966:
	movw	r7, #3442
	movw	r3, #65535
	ldrh	r0, [r4, r7]
	cmp	r0, r3
	strheq	r6, [r4, r7]	@ movhi
	beq	.L975
.L970:
	cmp	r6, r0
	beq	.L975
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
	beq	.L965
	movw	r2, #2858
	b	.L976
.L978:
	.align	2
.L977:
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
	ldr	r3, .L992
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
	ldr	r1, .L992+4
	ldr	r2, [r3, #3304]
	str	r1, [r2, #4]
	ldrh	r2, [r0, #4]
	tst	r2, #1
	moveq	r7, #6
	movne	r7, #7
.L985:
	ldrh	r3, [r4, #4]
	cmp	r3, #0
	bne	.L981
.L982:
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
.L981:
	mov	r0, r4
	bl	get_new_active_ppa
	cmn	r0, #1
	str	r0, [fp, #-48]
	beq	.L982
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
	bne	.L985
	b	.L982
.L993:
	.align	2
.L992:
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
	ldr	r6, .L1014
	bl	FtlBbtMemInit
	bl	FtlLoadFactoryBbt
	sub	r7, r6, #384
	sub	r9, r6, #18
	sub	r4, r6, #28
	mov	r5, r7
.L995:
	ldr	r3, .L1014+4
	ldrh	r2, [r3]
	cmp	r8, r2
	bcc	.L1001
	mov	r0, #0
	add	r6, r3, #60
.L1002:
	ldrh	r2, [r6]
	uxth	r3, r0
	add	r7, r0, #1
	cmp	r2, r3
	bhi	.L1003
	ldrh	r6, [r4, #12]
	movw	r7, #65535
	sub	r6, r6, #1
	uxth	r6, r6
.L1004:
	ldrh	r3, [r4, #12]
	sub	r3, r3, #48
	cmp	r6, r3
	ble	.L1008
	mov	r0, r6
	bl	FtlBbmIsBadBlock
	cmp	r0, #1
	beq	.L1005
	mov	r0, r6
	bl	FlashTestBlk
	cmp	r0, #0
	beq	.L1006
	mov	r0, r6
	bl	FtlBbmMapBadBlock
.L1005:
	sub	r6, r6, #1
	uxth	r6, r6
	b	.L1004
.L1001:
	ldr	r2, [r7, #3336]
	movw	r1, #65535
	ldr	r0, [r7, #3304]
	str	r2, [fp, #-48]
	str	r2, [r7, #3468]
	ldrh	r2, [r9, #2]!
	str	r0, [r7, #3464]
	cmp	r2, r1
	beq	.L996
	ldrh	r10, [r3, #44]
	ldr	r0, .L1014+8
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
.L997:
	uxth	r0, r10
	add	r8, r8, #1
	add	r6, r6, #4
	bl	FtlBbmMapBadBlock
	b	.L995
.L996:
	mov	r1, r8
	str	r3, [fp, #-44]
	bl	FlashGetBadBlockList
	ldr	r1, [r6]
	ldr	r0, [r7, #3464]
	bl	FtlBbt2Bitmap
	ldr	r3, [fp, #-44]
	ldrh	r3, [r3, #44]
.L1013:
	sub	r3, r3, #1
	uxth	r3, r3
	str	r3, [fp, #-44]
.L998:
	ldr	r10, .L1014+12
	ldr	r3, [fp, #-44]
	ldrh	r0, [r10]
	smlabb	r0, r0, r8, r3
	uxth	r0, r0
	bl	FtlBbmIsBadBlock
	cmp	r0, #1
	beq	.L999
	ldrh	r3, [fp, #-44]
	mov	r1, #16
	ldr	r0, [r5, #3336]
	strh	r3, [r9]	@ movhi
	bl	__memzero
	mov	r1, #4096
	ldr	r0, [r5, #3304]
	bl	__memzero
	ldr	r2, [fp, #-48]
	ldr	r3, .L1014+16
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
	ldr	r3, .L1014+20
	ldrh	r2, [r3]
	lsl	r2, r2, #2
	bl	ftl_memcpy
	mov	r2, #1
	ldr	r0, .L1014+8
	mov	r1, r2
	bl	FlashEraseBlocks
	mov	r3, #1
	ldr	r0, .L1014+8
	mov	r2, r3
	mov	r1, r3
	bl	FlashProgPages
	ldr	r3, [r5, #3456]
	cmn	r3, #1
	bne	.L997
	uxth	r0, r10
	bl	FtlBbmMapBadBlock
	b	.L998
.L999:
	ldr	r3, [fp, #-44]
	b	.L1013
.L1003:
	uxth	r0, r0
	bl	FtlBbmMapBadBlock
	mov	r0, r7
	b	.L1002
.L1006:
	ldrh	r3, [r4]
	cmp	r3, r7
	strheq	r6, [r4]	@ movhi
	beq	.L1005
.L1007:
	strh	r6, [r4, #4]	@ movhi
.L1008:
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
.L1015:
	.align	2
.L1014:
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
	ldr	r3, .L1020
	add	r1, r0, r1
.L1018:
	cmp	r0, r1
	bne	.L1019
	mov	r0, r3
	ldmfd	sp, {fp, sp, pc}
.L1019:
	lsr	r2, r3, #2
	ldrb	ip, [r0], #1	@ zero_extendqisi2
	add	r2, r2, r3, lsl #5
	add	r2, r2, ip
	eor	r3, r3, r2
	b	.L1018
.L1021:
	.align	2
.L1020:
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
	bne	.L1023
	ldrh	r3, [r0, #8]
	cmp	r3, #0
	beq	.L1024
	movw	r2, #641
	ldr	r1, .L1032
	ldr	r0, .L1032+4
	bl	sftl_printk
.L1024:
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
.L1025:
	mov	r0, #0
	ldmfd	sp, {r4, r5, r6, r7, r8, fp, sp, pc}
.L1023:
	lsl	r3, r3, #1
	ldr	r7, [r0, #24]
	mov	r1, #255
	ldrh	r8, [r5, r3]
	ldrh	r3, [r0, #2]
	ldr	r5, .L1032+8
	orr	r3, r3, r8, lsl #10
	ldr	r6, [r5, #3336]
	str	r3, [r5, #3460]
	ldr	r3, [r5, #3304]
	str	r6, [r5, #3468]
	str	r3, [r5, #3464]
	ldr	r3, [r0, #28]
	str	r3, [r6, #4]
	ldr	r3, .L1032+12
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
.L1026:
	ldrh	r0, [r4, #6]
	uxth	r1, r2
	cmp	r0, r1
	bhi	.L1028
	movw	r3, #314
	ldr	r0, [r5, #3464]
	ldrh	r1, [r5, r3]
	bl	js_hash
	mov	r2, #1
	str	r0, [r6, #12]
	mov	r3, #0
	mov	r1, r2
	ldr	r0, .L1032+16
	bl	FlashProgPages
	ldrh	r3, [r4, #2]
	mov	r0, r4
	add	r3, r3, #1
	strh	r3, [r4, #2]	@ movhi
	bl	ftl_map_blk_gc
	b	.L1025
.L1028:
	uxth	r1, r2
	ldr	r0, [r7, r1, lsl #2]
	cmp	r8, r0, lsr #10
	bne	.L1027
	ldr	r0, [r5, #3304]
	add	r3, r3, #1
	uxth	r3, r3
	str	r1, [r0, r3, lsl #3]
	ldr	r0, [r7, r1, lsl #2]
	ldr	r1, [r5, #3304]
	add	r1, r1, r3, lsl #3
	str	r0, [r1, #4]
.L1027:
	add	r2, r2, #1
	b	.L1026
.L1033:
	.align	2
.L1032:
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
	ldr	r5, .L1052
	mov	r4, r0
	mov	r7, r1
	mov	r9, r2
	add	r8, r5, #308
.L1035:
	ldr	r3, [r5, #2576]
	add	r3, r3, #1
	str	r3, [r5, #2576]
	ldrh	r3, [r8]
	ldrh	r2, [r4, #2]
	sub	r3, r3, #1
	cmp	r2, r3
	bge	.L1036
	ldrh	r2, [r4]
	movw	r3, #65535
	cmp	r2, r3
	bne	.L1037
.L1036:
	mov	r0, r4
	bl	Ftl_write_map_blk_to_last_page
.L1037:
	ldrh	r3, [r4]
	ldr	r2, [r4, #12]
	lsl	r3, r3, #1
	ldrh	r3, [r2, r3]
	cmp	r3, #0
	bne	.L1038
	mov	r2, #700
	ldr	r1, .L1052+4
	ldr	r0, .L1052+8
	bl	sftl_printk
.L1038:
	ldrh	r2, [r4]
	ldrh	r3, [r4, #10]
	cmp	r2, r3
	bcc	.L1039
	movw	r2, #701
	ldr	r1, .L1052+4
	ldr	r0, .L1052+8
	bl	sftl_printk
.L1039:
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
	ldr	r3, .L1052+12
	ldr	r0, [r5, #3464]
	ldrh	r1, [r3]
	bl	js_hash
	mov	r3, #1
	str	r0, [r10, #12]
	mov	r2, r3
	mov	r1, r3
	ldr	r0, .L1052+16
	bl	FlashProgPages
	ldrh	r3, [r4, #2]
	add	r3, r3, #1
	uxth	r3, r3
	strh	r3, [r4, #2]	@ movhi
	ldr	r2, [r5, #3456]
	cmn	r2, #1
	bne	.L1040
	ldr	r1, [r5, #3460]
	add	r6, r6, #1
	ldr	r0, .L1052+20
	uxth	r6, r6
	bl	sftl_printk
	ldrh	r3, [r4, #2]
	cmp	r3, #2
	ldrhls	r3, [r8]
	subls	r3, r3, #1
	strhls	r3, [r4, #2]	@ movhi
	cmp	r6, #3
	bls	.L1042
	mov	r2, r6
	ldr	r1, [r5, #3460]
	ldr	r0, .L1052+24
	bl	sftl_printk
.L1043:
	b	.L1043
.L1042:
	ldr	r3, [r4, #32]
	cmp	r3, #0
	beq	.L1035
.L1051:
	b	.L1051
.L1040:
	cmp	r3, #1
	cmpne	r2, #256
	beq	.L1046
	ldr	r0, [r4, #36]
	cmp	r0, #0
	beq	.L1047
.L1046:
	mov	r3, #0
	str	r3, [r4, #36]
	b	.L1035
.L1047:
	ldr	r2, [r5, #3460]
	ldr	r3, [r4, #24]
	str	r2, [r3, r7, lsl #2]
	ldmib	sp, {r4, r5, r6, r7, r8, r9, r10, fp, sp, pc}
.L1053:
	.align	2
.L1052:
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
	ldr	r9, .L1061
	uxth	r6, r0
	uxth	r10, r1
	ldrh	r3, [r9]
	cmp	r3, r6
	bcs	.L1055
	movw	r2, #485
	ldr	r1, .L1061+4
	ldr	r0, .L1061+8
	bl	sftl_printk
.L1055:
	ldr	r7, .L1061+12
	mov	r5, #12
	ldr	r3, [r7, #3380]
	mov	r4, r7
	ldr	r8, [r3, r6, lsl #2]
	cmp	r8, #0
	bne	.L1056
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
.L1057:
	mov	r0, #0
	ldmfd	sp, {r4, r5, r6, r7, r8, r9, r10, fp, sp, pc}
.L1056:
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
	beq	.L1058
	mov	r2, r8
	mov	r1, r6
	ldr	r0, .L1061+16
	bl	sftl_printk
	mov	r3, #4
	ldr	r1, [r4, #3468]
	mov	r2, r3
	ldr	r0, .L1061+20
	bl	rknand_print_hex
	ldrh	r3, [r9]
	mov	r2, #4
	ldr	r1, [r4, #3380]
	ldr	r0, .L1061+24
	bl	rknand_print_hex
.L1059:
	ldrh	r3, [r7, #8]
	cmp	r3, r6
	beq	.L1060
	mov	r2, #508
	ldr	r1, .L1061+4
	ldr	r0, .L1061+8
	bl	sftl_printk
.L1060:
	ldr	r3, [r4, #2544]
	mov	r1, #0
	add	r2, r3, r5
	str	r1, [r2, #4]
	strh	r6, [r3, r5]	@ movhi
	b	.L1057
.L1058:
	ldr	r3, [r4, #3456]
	cmp	r3, #256
	bne	.L1059
	mov	r2, r8
	mov	r1, r6
	ldr	r0, .L1061+28
	bl	sftl_printk
	ldr	r3, [r4, #2544]
	mov	r1, r6
	ldr	r0, .L1061+32
	add	r3, r3, r5
	ldr	r2, [r3, #8]
	bl	FtlMapWritePage
	b	.L1059
.L1062:
	.align	2
.L1061:
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
	blt	.L1064
	uxth	r0, r0
	lsl	r0, r0, #1
	ldrh	r8, [r5, r0]
	cmp	r8, #0
	beq	.L1064
	ldr	r3, [r4, #32]
	cmp	r3, #0
	bne	.L1064
	mov	r2, #1
	str	r2, [r4, #32]
	strh	r3, [r5, r0]	@ movhi
	ldrh	r3, [r4, #8]
	ldrh	r2, [r4, #2]
	sub	r3, r3, #1
	strh	r3, [r4, #8]	@ movhi
	ldr	r3, .L1079
	ldrh	r3, [r3]
	cmp	r2, r3
	bcc	.L1065
	mov	r0, r4
	bl	ftl_map_blk_alloc_new_blk
.L1065:
	ldr	r5, .L1079+4
	mov	r6, #0
.L1066:
	ldrh	r3, [r4, #6]
	uxth	r10, r6
	cmp	r3, r10
	bhi	.L1073
	mov	r1, #1
	mov	r0, r8
	bl	FtlFreeSysBlkQueueIn
	mov	r3, #0
	str	r3, [r4, #32]
.L1064:
	ldr	r3, .L1079
	ldrh	r2, [r4, #2]
	ldrh	r3, [r3]
	cmp	r2, r3
	bcc	.L1074
	mov	r0, r4
	bl	ftl_map_blk_alloc_new_blk
.L1074:
	mov	r0, #0
	sub	sp, fp, #40
	ldmfd	sp, {r4, r5, r6, r7, r8, r9, r10, fp, sp, pc}
.L1073:
	uxth	r7, r6
	add	r3, r9, r7, lsl #2
	str	r3, [fp, #-44]
	ldr	r3, [r9, r7, lsl #2]
	cmp	r8, r3, lsr #10
	bne	.L1067
	ldr	r3, [r5, #3308]
	ldr	r0, .L1079+8
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
	beq	.L1068
	movw	r2, #611
	ldr	r1, .L1079+12
	ldr	r0, .L1079+16
	bl	sftl_printk
	ldr	r3, [fp, #-48]
.L1068:
	ldr	r2, [r5, #3456]
	cmn	r2, #1
	bne	.L1069
.L1071:
	ldr	r2, [fp, #-44]
	mov	r3, #0
	str	r3, [r2]
.L1070:
	b	.L1070
.L1069:
	ldrh	r2, [r3, #8]
	cmp	r2, r10
	bne	.L1071
	ldrh	r2, [r3]
	ldrh	r3, [r4, #4]
	cmp	r2, r3
	bne	.L1071
	ldr	r2, [r5, #3464]
	mov	r1, r7
	mov	r0, r4
	bl	FtlMapWritePage
.L1067:
	add	r6, r6, #1
	b	.L1066
.L1080:
	.align	2
.L1079:
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
	ldr	r5, .L1082
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
.L1083:
	.align	2
.L1082:
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
	ldr	r4, .L1097
	mov	r7, r1
	mov	r10, r2
	add	r3, r4, #312
	ldrh	r5, [r3]
	ldr	r3, [r4, #2556]
	cmp	r0, r3
	bcc	.L1085
	movw	r2, #813
	ldr	r1, .L1097+4
	ldr	r0, .L1097+8
	bl	sftl_printk
.L1085:
	ldr	r3, [r4, #2556]
	cmp	r6, r3
	bcs	.L1086
	add	r5, r5, #7
	ldr	ip, [r4, #2544]
	lsr	r3, r6, r5
	mov	r1, #0
	mov	r2, #12
	str	r3, [fp, #-44]
	movw	r3, #342
	ldrh	r9, [fp, #-44]
	ldrh	r0, [r4, r3]
.L1087:
	uxth	r8, r1
	cmp	r8, r0
	bcc	.L1092
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
	beq	.L1093
	ldr	r2, [ip, #4]
	cmp	r2, #0
	bge	.L1093
	bl	flush_l2p_region
.L1093:
	mov	r1, r8
	ldrh	r0, [fp, #-44]
	bl	load_l2p_region
	b	.L1089
.L1086:
	cmp	r10, #0
	mvn	r0, #0
	streq	r0, [r7]
.L1084:
	sub	sp, fp, #40
	ldmfd	sp, {r4, r5, r6, r7, r8, r9, r10, fp, sp, pc}
.L1092:
	add	r1, r1, #1
	mla	lr, r2, r1, ip
	ldrh	lr, [lr, #-12]
	cmp	lr, r9
	bne	.L1087
.L1089:
	mvn	r2, #0
	cmp	r10, #0
	bic	r5, r6, r2, lsl r5
	mov	r2, #12
	uxth	r5, r5
	bne	.L1090
	ldr	r1, [r4, #2544]
	mla	r2, r2, r8, r1
	ldr	r2, [r2, #8]
	ldr	r2, [r2, r5, lsl #2]
	str	r2, [r7]
.L1091:
	ldr	r1, [r4, #2544]
	mov	r2, #12
	mov	r0, #0
	mla	r3, r2, r8, r1
	ldr	r2, [r3, #4]
	cmn	r2, #1
	addne	r2, r2, #1
	strne	r2, [r3, #4]
	b	.L1084
.L1090:
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
	b	.L1091
.L1098:
	.align	2
.L1097:
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
	ldr	r4, .L1117
	ldrh	r2, [r4, #32]
	cmp	r2, #0
	beq	.L1100
	ldrb	r3, [r4, #36]	@ zero_extendqisi2
	cmp	r3, #0
	bne	.L1100
	movw	r3, #306
	ldrb	r1, [r4, #35]	@ zero_extendqisi2
	ldrh	r3, [r4, r3]
	mul	r3, r3, r1
	cmp	r2, r3
	beq	.L1100
	ldrb	r7, [r4, #38]	@ zero_extendqisi2
	cmp	r7, #0
	bne	.L1099
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
	beq	.L1102
	mov	r2, r7
	mov	r1, #1
	sub	r0, fp, #60
	bl	FlashReadPages
.L1103:
	ldr	r10, .L1117+4
	mov	r7, #0
	ldr	r3, .L1117+8
	lsl	r8, r8, #2
	mov	r9, r7
	strh	r3, [r5]	@ movhi
.L1104:
	cmp	r8, r7
	bne	.L1108
.L1105:
	mov	r3, #1
.L1116:
	strb	r3, [r4, #38]
.L1099:
	sub	sp, fp, #40
	ldmfd	sp, {r4, r5, r6, r7, r8, r9, r10, fp, sp, pc}
.L1102:
	movw	r3, #314
	mov	r1, #255
	ldrh	r2, [r4, r3]
	bl	ftl_memset
	b	.L1103
.L1108:
	ldrh	r3, [r4, #32]
	cmp	r3, #0
	beq	.L1105
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
	b	.L1104
.L1100:
	mov	r3, #0
	b	.L1116
.L1118:
	.align	2
.L1117:
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
	ldr	r7, .L1129
	ubfx	r0, r1, #10, #16
	str	r1, [fp, #-32]
	bl	P2V_block_in_plane
	ldr	r2, [r7, #76]
	lsl	r5, r0, #1
	ldrh	r3, [r2, r5]
	cmp	r3, #0
	bne	.L1120
	ldr	r4, [r7, #2536]
	cmp	r4, #0
	beq	.L1121
	ldr	r2, [r7, #2520]
	movw	lr, #65535
	ldrh	ip, [r7, #228]
	sub	r4, r4, r2
	ubfx	r4, r4, #3, #16
.L1122:
	uxth	r1, r3
	cmp	ip, r1
	bls	.L1121
	cmp	r4, r0
	bne	.L1123
	mov	r1, r4
	ldr	r0, .L1129+4
	bl	List_remove_node
	ldrh	r3, [r7, #228]
	cmp	r3, #0
	bne	.L1124
	movw	r2, #1733
	ldr	r1, .L1129+8
	ldr	r0, .L1129+12
	bl	sftl_printk
.L1124:
	ldrh	r3, [r7, #228]
	mov	r0, r4
	sub	r3, r3, #1
	strh	r3, [r7, #228]	@ movhi
	bl	INSERT_DATA_LIST
	ldr	r2, [r7, #76]
	ldrh	r3, [r2, r5]
.L1120:
	add	r3, r3, #1
	strh	r3, [r2, r5]	@ movhi
	b	.L1121
.L1123:
	lsl	r4, r4, #3
	add	r3, r3, #1
	ldrh	r4, [r2, r4]
	cmp	r4, lr
	bne	.L1122
.L1121:
	mov	r2, #1
	sub	r1, fp, #32
	mov	r0, r6
	bl	log2phys
	ldmib	sp, {r4, r5, r6, r7, fp, sp, pc}
.L1130:
	.align	2
.L1129:
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
	beq	.L1255
	ldrh	r3, [r0, #2]
	ldr	r5, .L1263
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
	bne	.L1135
.L1261:
	strb	r3, [r10, #6]
.L1255:
	mov	r0, #0
	sub	sp, fp, #40
	ldmfd	sp, {r4, r5, r6, r7, r8, r9, r10, fp, sp, pc}
.L1136:
	uxth	r1, r3
	add	r1, r10, r1, lsl #1
	ldrh	r0, [r1, #16]
.L1135:
	cmp	r0, r2
	uxth	r6, r3
	add	r3, r3, #1
	beq	.L1136
	mov	r1, #1
	bl	FtlGetLastWrittenPage
	cmn	r0, #1
	mov	r4, r0
	beq	.L1137
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
	ldr	r3, .L1263+4
	ldrh	r6, [r3]
	add	r3, r10, #16
	mov	ip, r3
	str	r3, [fp, #-80]
.L1138:
	ldr	r1, [fp, #-56]
	uxth	r3, r2
	cmp	r1, r3
	bhi	.L1142
	ldr	r6, .L1263
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
.L1143:
	uxth	r2, r3
	cmp	r5, r2
	bhi	.L1148
	addeq	r3, r4, #1
	uxtheq	r3, r3
	streq	r3, [fp, #-60]
	bne	.L1146
.L1256:
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
	beq	.L1262
	movw	r3, #65535
	cmp	r8, r3
	bne	.L1152
	ldrb	r3, [r10, #8]	@ zero_extendqisi2
	cmp	r3, #0
	bne	.L1153
.L1152:
	ldr	r3, [r6, #3452]
	uxth	r7, r4
	uxth	r4, r4
	cmn	r3, #1
	ldreq	r3, [fp, #-56]
	streq	r3, [r6, #3452]
	ldr	r3, [fp, #-64]
	ldr	r6, .L1263
	add	r3, r3, #7
	cmp	r4, r3
	subgt	r4, r7, #7
	ldrle	r4, [fp, #-64]
	uxthgt	r4, r4
.L1156:
	cmp	r4, r7
	bhi	.L1163
	mov	r3, #0
	ldrh	r8, [r6, #236]
	ldr	r0, [r6, #3292]
	mov	r5, r3
	ldr	r1, [fp, #-80]
	movw	lr, #65535
	mov	ip, #20
	b	.L1164
.L1137:
	ldr	r3, [fp, #-64]
	cmp	r3, #0
	beq	.L1139
	movw	r2, #1800
	ldr	r1, .L1263+8
	ldr	r0, .L1263+12
	bl	sftl_printk
.L1139:
	ldr	r3, [fp, #-84]
	cmp	r3, #0
	cmpne	r6, r3
	beq	.L1140
	movw	r2, #1801
	ldr	r1, .L1263+8
	ldr	r0, .L1263+12
	bl	sftl_printk
.L1140:
	mov	r3, #0
	strh	r3, [r10, #2]	@ movhi
	b	.L1261
.L1142:
	ldrh	r3, [ip], #2
	cmp	r3, r7
	beq	.L1141
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
.L1141:
	add	r2, r2, #1
	b	.L1138
.L1148:
	ldr	r2, [r7]
	cmp	r2, #0
	bne	.L1144
	ldr	ip, [r7, #12]
	ldr	r2, [ip, #4]
	cmn	r2, #1
	beq	.L1145
	ldr	r1, .L1263
	mov	r0, r2
	str	ip, [fp, #-68]
	ldr	r1, [r1, #2596]
	bl	ftl_cmp_data_ver
	ldr	ip, [fp, #-68]
	cmp	r0, #0
	ldrne	r1, .L1263
	addne	r2, r2, #1
	strne	r2, [r1, #2596]
.L1145:
	ldr	r2, [ip]
	cmn	r2, #1
	bne	.L1147
.L1146:
	uxth	r2, r4
	uxth	r3, r3
	str	r2, [fp, #-60]
	mov	r2, #20
	mla	r9, r2, r3, r9
	b	.L1256
.L1144:
	ldr	r8, [fp, #-60]
.L1147:
	add	r3, r3, #1
	add	r7, r7, #20
	b	.L1143
.L1158:
	ldrh	r2, [r1], #2
	add	r3, r3, #1
	cmp	r2, lr
	mlane	r9, ip, r5, r0
	addne	r5, r5, #1
	orrne	r2, r4, r2, lsl #10
	uxthne	r5, r5
	strne	r2, [r9, #4]
.L1164:
	uxth	r2, r3
	cmp	r8, r2
	bhi	.L1158
	mov	r1, r5
	mov	r2, #0
	bl	FlashReadPages
	ldr	r3, [r6, #3292]
	mov	r2, #20
	movw	r1, #65535
	mla	r5, r2, r5, r3
.L1159:
	cmp	r5, r3
	addeq	r4, r4, #1
	uxtheq	r4, r4
	beq	.L1156
.L1162:
	ldr	r2, [r3]
	cmp	r2, #0
	bne	.L1153
	ldr	r2, [r3, #12]
	ldrh	r0, [r2]
	cmp	r0, r1
	beq	.L1161
	ldr	r2, [r2, #4]
	cmn	r2, #1
	strne	r2, [r6, #3452]
.L1161:
	add	r3, r3, #20
	b	.L1159
.L1163:
	mvn	r3, #0
	str	r3, [r6, #3452]
.L1153:
	ldr	r7, .L1263
	mov	r2, #1
	ldr	r9, [fp, #-64]
	movw	r3, #3476
	mov	r4, r7
	strh	r2, [r7, r3]	@ movhi
.L1165:
	ldrh	ip, [r4, #236]
	movw	lr, #65535
	ldr	r0, [r4, #3292]
	mov	r5, #20
	ldr	r1, [fp, #-80]
	mov	r3, #0
	str	r3, [fp, #-72]
.L1166:
	uxth	r2, r3
	cmp	ip, r2
	bhi	.L1168
	mov	r2, #0
	ldr	r1, [fp, #-72]
	bl	FlashReadPages
	mov	r3, #0
.L1260:
	str	r3, [fp, #-76]
	ldr	r2, [fp, #-72]
	ldrh	r3, [fp, #-76]
	cmp	r2, r3
	bhi	.L1196
	ldr	r3, .L1263+16
	add	r9, r9, #1
	uxth	r9, r9
	ldrh	r3, [r3]
	cmp	r3, r9
	bne	.L1165
	ldrh	r2, [r4, #236]
	movw	r0, #65535
	mov	r3, #0
	strh	r9, [r10, #2]	@ movhi
	strh	r3, [r10, #4]	@ movhi
.L1197:
	uxth	r1, r3
	cmp	r1, r2
	bcs	.L1255
	ldr	r1, [fp, #-80]
	ldrh	ip, [r1], #2
	cmp	ip, r0
	str	r1, [fp, #-80]
	add	r1, r3, #1
	bne	.L1261
	mov	r3, r1
	b	.L1197
.L1168:
	ldrh	r2, [r1], #2
	cmp	r2, lr
	beq	.L1167
	ldr	r6, [fp, #-72]
	orr	r2, r9, r2, lsl #10
	mla	r6, r5, r6, r0
	str	r2, [r6, #4]
	ldr	r2, [fp, #-72]
	add	r2, r2, #1
	uxth	r2, r2
	str	r2, [fp, #-72]
.L1167:
	add	r3, r3, #1
	b	.L1166
.L1196:
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
	bcc	.L1170
	ldr	r2, [fp, #-84]
	moveq	r3, #1
	movne	r3, #0
	cmp	r2, r0
	movls	r3, #0
	andhi	r3, r3, #1
	cmp	r3, #0
	bne	.L1170
	ldr	r3, [fp, #-60]
	ldr	r2, [fp, #-68]
	cmp	r9, r3
	cmpeq	r2, r0
	beq	.L1171
	ldr	r2, [fp, #-88]
	ldr	r3, [r2, r6]
	cmn	r3, #1
	beq	.L1172
	ldr	r8, [r8, #12]
	movw	r3, #61589
	ldrh	r2, [r8]
	cmp	r2, r3
	ldrhne	r0, [r10]
	bne	.L1257
	ldr	r3, [r8, #4]
	cmn	r3, #1
	str	r3, [fp, #-56]
	beq	.L1174
	mov	r0, r3
	ldr	r1, [r4, #2596]
	bl	ftl_cmp_data_ver
	cmp	r0, #0
	ldrne	r3, [fp, #-56]
	addne	r3, r3, #1
	strne	r3, [r4, #2596]
.L1174:
	ldrh	r2, [r8]
	movw	r3, #61589
	cmp	r2, r3
	beq	.L1175
	mov	r2, #1952
	ldr	r1, .L1263+8
	ldr	r0, .L1263+12
	bl	sftl_printk
.L1175:
	ldr	r5, [r8, #8]
	sub	r1, fp, #48
	ldr	r3, [r8, #12]
	mov	r2, #0
	mov	r0, r5
	str	r3, [fp, #-52]
	bl	log2phys
	ldr	r1, [r4, #3452]
	cmn	r1, #1
	beq	.L1176
	ldr	r0, [fp, #-56]
	bl	ftl_cmp_data_ver
	cmp	r0, #0
	beq	.L1176
	ldr	r2, [fp, #-52]
	cmn	r2, #1
	beq	.L1177
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
	bne	.L1178
.L1179:
	mvn	r3, #0
	str	r3, [fp, #-52]
.L1186:
	ldr	r6, [fp, #-52]
	cmn	r6, #1
	beq	.L1170
.L1200:
	ubfx	r0, r6, #10, #16
	bl	P2V_block_in_plane
	ldrh	r3, [r4, #244]
	mov	r5, r0
	cmp	r3, r0
	bhi	.L1192
	movw	r2, #2057
	ldr	r1, .L1263+8
	ldr	r0, .L1263+12
	bl	sftl_printk
.L1192:
	ldr	r2, [r7, #76]
	lsl	r3, r5, #1
	ldrh	r3, [r2, r3]
	cmp	r3, #0
	beq	.L1193
	mov	r0, r5
.L1257:
	bl	decrement_vpc_count
	b	.L1170
.L1177:
	ldr	r3, [fp, #-44]
	ldr	r2, [fp, #-48]
	cmp	r2, r3
	bne	.L1170
	mov	r2, #1
	sub	r1, fp, #52
	mov	r0, r5
	bl	log2phys
.L1170:
	ldr	r3, [fp, #-76]
	add	r3, r3, #1
	b	.L1260
.L1178:
	ldr	r1, [r8, #8]
	cmp	r5, r1
	bne	.L1179
	ldr	r1, [r8, #4]
	ldr	r0, [r4, #3452]
	str	r1, [fp, #-88]
	bl	ftl_cmp_data_ver
	cmp	r0, #0
	beq	.L1179
	ldr	r1, [fp, #-48]
	ldr	r0, [fp, #-44]
	cmp	r1, r0
	bne	.L1181
.L1184:
	ldr	r1, [fp, #-52]
	mov	r0, r5
	bl	FtlReUsePrevPpa
	b	.L1179
.L1181:
	ldr	r0, [fp, #-52]
	cmp	r1, r0
	beq	.L1179
	cmn	r1, #1
	streq	r1, [r2, r6]
	beq	.L1183
	str	r1, [r3, #4]
	mov	r2, #0
	mov	r1, #1
	mov	r0, r3
	ldr	r8, [r3, #12]
	bl	FlashReadPages
.L1183:
	ldr	r2, [r4, #3292]
	ldr	r2, [r2, r6]
	cmn	r2, #1
	beq	.L1184
	ldr	r3, [r8, #4]
	ldr	r0, [r4, #3452]
	mov	r1, r3
	bl	ftl_cmp_data_ver
	cmp	r0, #0
	beq	.L1184
	mov	r1, r3
	ldr	r0, [fp, #-88]
	bl	ftl_cmp_data_ver
	cmp	r0, #0
	beq	.L1179
	b	.L1184
.L1176:
	ldr	r3, [fp, #-44]
	ldr	r2, [fp, #-48]
	cmp	r2, r3
	beq	.L1186
	ldr	r1, [fp, #-52]
	cmn	r1, #1
	beq	.L1188
	ldr	r3, [r4, #252]
	cmp	r3, r1, lsr #10
	ldrls	r0, .L1263+20
	bls	.L1259
.L1188:
	mov	r2, #1
	sub	r1, fp, #44
	mov	r0, r5
	bl	log2phys
	ldr	r6, [fp, #-48]
	cmn	r6, #1
	beq	.L1186
	ldr	r3, [fp, #-52]
	cmp	r6, r3
	beq	.L1200
	ubfx	r0, r6, #10, #16
	bl	P2V_block_in_plane
	ldrh	r3, [r4, #28]
	cmp	r3, r0
	beq	.L1191
	ldrh	r3, [r4, #80]
	cmp	r3, r0
	beq	.L1191
	ldrh	r3, [r4, #128]
	cmp	r3, r0
	bne	.L1186
.L1191:
	ldr	r0, [r7, #3292]
	mov	r2, #0
	mov	r1, #1
	str	r6, [r0, #4]
	ldr	r8, [r0, #12]
	bl	FlashReadPages
	ldr	r3, [r7, #3292]
	ldr	r3, [r3]
	cmn	r3, #1
	beq	.L1186
	ldr	r1, [r8, #4]
	ldr	r0, [fp, #-56]
	bl	ftl_cmp_data_ver
	cmp	r0, #0
	bne	.L1186
	mov	r2, #1
	sub	r1, fp, #48
	mov	r0, r5
	bl	log2phys
	b	.L1186
.L1193:
	ldr	r0, .L1263+24
	mov	r1, r5
.L1259:
	bl	sftl_printk
	b	.L1170
.L1172:
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
	beq	.L1258
	ldr	r2, [fp, #-56]
	cmp	r2, r3
	bcs	.L1170
	mov	r3, r2
.L1258:
	str	r3, [r4, #3452]
	b	.L1170
.L1171:
	ldrb	r3, [fp, #-68]	@ zero_extendqisi2
	ldr	r2, [fp, #-68]
	ldr	r1, [fp, #-60]
	strb	r3, [r10, #6]
	ldrh	r3, [fp, #-60]
	strh	r3, [r10, #2]	@ movhi
.L1262:
	mov	r0, r10
	bl	ftl_sb_update_avl_pages
	b	.L1255
.L1264:
	.align	2
.L1263:
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
	ldr	r6, .L1289
	ldr	r1, .L1289+4
	ldr	r0, .L1289+8
	mov	r5, r6
	bl	sftl_printk
	mov	r1, #8192
	ldr	r0, .L1289+12
	bl	__memzero
.L1266:
	ldr	r3, [r6, #2556]
	cmp	r4, r3
	bcc	.L1268
	ldr	r8, .L1289+12
	mov	r4, #0
	ldr	r9, .L1289+16
	mov	r6, r4
.L1269:
	ldrh	r2, [r5, #244]
	uxth	r3, r4
	cmp	r2, r3
	bhi	.L1271
	ldr	r4, [r5, #2536]
	cmp	r4, #0
	beq	.L1272
	ldr	r3, [r5, #2520]
	mov	r7, #0
	ldrh	r8, [r5, #228]
	ldr	r9, .L1289+12
	ldr	r10, .L1289+20
	sub	r4, r4, r3
	ubfx	r4, r4, #3, #16
.L1273:
	uxth	r3, r7
	cmp	r8, r3
	bls	.L1272
	ldr	r2, [r5, #76]
	lsl	r3, r4, #1
	ldrh	r2, [r2, r3]
	cmp	r2, #0
	beq	.L1274
	mov	r6, #1
	ldrh	r3, [r9, r3]
	mov	r1, r4
	mov	r0, r10
	bl	sftl_printk
.L1274:
	ldr	r3, [r5, #2520]
	lsl	r4, r4, #3
	add	r7, r7, #1
	ldrh	r4, [r3, r4]
	movw	r3, #65535
	cmp	r4, r3
	bne	.L1273
.L1272:
	cmp	r6, #0
	ldmibeq	sp, {r4, r5, r6, r7, r8, r9, r10, fp, sp, pc}
	movw	r2, #2383
	ldr	r1, .L1289+4
	ldr	r0, .L1289+24
	bl	sftl_printk
	ldmib	sp, {r4, r5, r6, r7, r8, r9, r10, fp, sp, pc}
.L1268:
	mov	r2, #0
	sub	r1, fp, #44
	mov	r0, r4
	bl	log2phys
	ldr	r0, [fp, #-44]
	cmn	r0, #1
	beq	.L1267
	ubfx	r0, r0, #10, #16
	bl	P2V_block_in_plane
	ldr	r2, .L1289+12
	lsl	r0, r0, #1
	ldrh	r3, [r2, r0]
	add	r3, r3, #1
	strh	r3, [r2, r0]	@ movhi
.L1267:
	add	r4, r4, #1
	b	.L1266
.L1271:
	uxth	r1, r4
	ldr	r3, [r5, #76]
	lsl	r7, r1, #1
	ldrh	r2, [r3, r7]
	ldrh	r3, [r8, r7]
	cmp	r2, r3
	beq	.L1270
	mov	r0, r9
	bl	sftl_printk
	ldr	r3, [r5, #76]
	movw	r2, #65535
	ldrh	r3, [r3, r7]
	cmp	r3, r2
	beq	.L1270
	ldrh	r2, [r8, r7]
	cmp	r2, r3
	movhi	r6, #1
.L1270:
	add	r4, r4, #1
	b	.L1269
.L1290:
	.align	2
.L1289:
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
	ldr	r6, .L1299
	mov	r1, #0
	ldr	r0, .L1299+4
	bl	sftl_printk
	mov	r4, r6
.L1292:
	ldr	r3, [r6, #2556]
	cmp	r5, r3
	bcc	.L1298
	sub	sp, fp, #28
	ldmfd	sp, {r4, r5, r6, r7, fp, sp, pc}
.L1298:
	mov	r2, #0
	sub	r1, fp, #32
	mov	r0, r5
	bl	log2phys
	ubfx	r3, r5, #0, #11
	cmp	r3, #0
	bne	.L1293
	ldr	r2, [fp, #-32]
	mov	r1, r5
	ldr	r0, .L1299+8
	bl	sftl_printk
.L1293:
	ldr	r3, [fp, #-32]
	cmn	r3, #1
	beq	.L1295
	str	r3, [r4, #3460]
	mov	r2, #0
	ldr	r3, [r4, #3304]
	mov	r1, #1
	ldr	r7, [r4, #3336]
	ldr	r0, .L1299+12
	str	r3, [r4, #3464]
	str	r5, [r4, #3472]
	str	r7, [r4, #3468]
	str	r2, [r4, #3456]
	bl	FlashReadPages
	ldr	r3, [r4, #3456]
	cmn	r3, #1
	cmpne	r3, #256
	beq	.L1296
	ldr	r3, [r7, #8]
	cmp	r5, r3
	beq	.L1295
.L1296:
	ldr	r2, [r4, #3464]
	ldr	r3, [r4, #3468]
	ldr	r0, .L1299+16
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
.L1295:
	add	r5, r5, #1
	b	.L1292
.L1300:
	.align	2
.L1299:
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
	ldr	r3, .L1333
	str	r1, [fp, #-76]
	ldrh	r5, [r3, #4]
	movw	r3, #65535
	cmp	r5, r3
	beq	.L1326
	cmp	r5, #0
	bne	.L1302
.L1303:
	bl	FtlGcPageVarInit
	b	.L1304
.L1326:
	mov	r5, #0
.L1302:
	ldr	r2, .L1333+4
	movw	r3, #306
	ldrh	r3, [r2, r3]
	ldr	r2, [fp, #-76]
	cmp	r3, r2
	beq	.L1303
.L1304:
	ldr	r6, .L1333+4
	mov	r3, #0
	str	r3, [fp, #-68]
.L1320:
	ldrh	r2, [r4]
	mov	r3, #0
	strb	r3, [r4, #8]
	movw	r3, #65535
	cmp	r2, r3
	beq	.L1305
.L1323:
	ldrh	r3, [r6, #236]
	add	ip, r4, #16
	ldr	r0, [r6, #3292]
	movw	r9, #65535
	mov	lr, #20
	str	r3, [fp, #-72]
	ldr	r3, [r6, #3180]
	str	r3, [fp, #-80]
	ldr	r3, .L1333+8
	ldrh	r2, [r3]
	ldrh	r10, [r3, #2]
	str	r2, [fp, #-84]
	ldr	r2, [r6, #3184]
	str	r2, [fp, #-88]
	mov	r2, #0
	mov	r7, r2
.L1306:
	ldr	r1, [fp, #-72]
	uxth	r3, r2
	cmp	r1, r3
	bhi	.L1308
	mov	r8, #0
	mov	r2, #0
	mov	r1, r7
	bl	FlashReadPages
.L1309:
	uxth	r3, r8
	cmp	r7, r3
	bhi	.L1321
	ldr	r3, [fp, #-68]
	add	r5, r5, #1
	uxth	r5, r5
	add	r3, r3, #1
	str	r3, [fp, #-68]
	ldr	r2, [fp, #-68]
	ldr	r3, [fp, #-76]
	cmp	r3, r2
	ldr	r2, .L1333+12
	bls	.L1322
.L1324:
	ldrh	r3, [r2]
	cmp	r3, r5
	bhi	.L1323
.L1305:
	ldr	r3, .L1333
	mvn	r2, #0
	strh	r5, [r4, #2]	@ movhi
	mov	r1, r5
	mov	r0, r4
	strh	r2, [r3, #4]	@ movhi
	mov	r2, #0
	strb	r2, [r4, #6]
	bl	ftl_sb_update_avl_pages
	b	.L1325
.L1308:
	ldrh	r3, [ip], #2
	cmp	r3, r9
	beq	.L1307
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
.L1307:
	add	r2, r2, #1
	b	.L1306
.L1321:
	mov	ip, #20
	ldr	r2, [r6, #3292]
	mul	ip, ip, r8
	ldr	r10, [r2, ip]
	add	r3, r2, ip
	ldr	r1, [r3, #4]
	ldr	r9, [r3, #12]
	cmp	r10, #0
	str	r1, [fp, #-72]
	bne	.L1310
	ldrh	r1, [r9]
	movw	r2, #65535
	cmp	r1, r2
	bne	.L1311
.L1331:
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
	b	.L1320
.L1311:
	ldr	r0, [r9, #8]
	ldr	r2, [r6, #2556]
	str	ip, [fp, #-80]
	cmp	r0, r2
	bhi	.L1331
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
	bne	.L1314
.L1319:
	ldr	r2, [r9, #8]
.L1332:
	ldr	r1, [fp, #-72]
	add	r8, r8, #1
	ldr	r0, [r9, #12]
	bl	FtlGcUpdatePage
	b	.L1309
.L1314:
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
	ldr	r2, .L1333+16
	ldr	ip, [fp, #-80]
	ldr	r0, [fp, #-52]
	ldrh	r1, [r2]
	ldr	r2, [r6, #3292]
	lsl	r1, r1, #7
	add	ip, r2, ip
	mov	r2, r10
.L1316:
	cmp	r2, r1
	beq	.L1319
	ldr	lr, [ip, #8]
	ldr	r10, [lr, r2, lsl #2]
	ldr	lr, [r0, r2, lsl #2]
	cmp	r10, lr
	beq	.L1317
	ldr	r2, [fp, #-56]
	ldrh	r1, [r4]
	ldr	r0, .L1333+20
	bl	sftl_printk
	b	.L1331
.L1317:
	add	r2, r2, #1
	b	.L1316
.L1310:
	mvn	r2, #0
	b	.L1332
.L1322:
	ldr	r1, .L1333
	movw	r0, #65535
	ldrh	r3, [r1, #4]
	cmp	r3, r0
	beq	.L1324
	ldr	r0, [fp, #-68]
	add	r3, r3, r0
	strh	r3, [r1, #4]	@ movhi
	ldrh	r3, [r2]
	cmp	r3, r5
	bls	.L1324
.L1325:
	mvn	r0, #0
	sub	sp, fp, #40
	ldmfd	sp, {r4, r5, r6, r7, r8, r9, r10, fp, sp, pc}
.L1334:
	.align	2
.L1333:
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
	ldr	r6, .L1351
	ldr	r9, [r6, #2724]
	mov	r5, r6
	cmp	r9, #0
	beq	.L1336
	ldr	r2, [r6, #2728]
	ldr	r3, [r6, #2556]
	cmp	r2, r3
	bcs	.L1337
	mov	r4, #2048
.L1342:
	ldr	r0, [r5, #2728]
	ldr	r3, [r5, #2556]
	cmp	r0, r3
	bcc	.L1338
.L1341:
	mvn	r0, #0
.L1335:
	sub	sp, fp, #40
	ldmfd	sp, {r4, r5, r6, r7, r8, r9, r10, fp, sp, pc}
.L1338:
	mov	r2, #0
	sub	r1, fp, #128
	bl	log2phys
	ldr	r2, [fp, #-128]
	ldr	r3, [r5, #2728]
	cmn	r2, #1
	add	r3, r3, #1
	str	r3, [r5, #2728]
	beq	.L1340
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
	bne	.L1341
	ldr	r0, [fp, #-128]
	ubfx	r0, r0, #10, #16
	bl	P2V_block_in_plane
	bl	FtlGcRefreshBlock
	b	.L1341
.L1340:
	subs	r4, r4, #1
	bne	.L1342
	b	.L1341
.L1337:
	ldr	r3, [r6, #2560]
	mov	r0, #0
	str	r0, [r6, #2724]
	str	r0, [r6, #2728]
	str	r3, [r6, #2720]
	b	.L1335
.L1336:
	ldr	r1, [r6, #2616]
	movw	r4, #10000
	ldr	r8, [r6, #2560]
	ldr	r7, [r6, #2720]
	cmp	r1, r4
	ldr	r10, .L1351+4
	add	r3, r8, #1048576
	movhi	r4, #31
	movls	r4, #63
	cmp	r7, r3
	bhi	.L1346
	ldr	r3, [r6, #2556]
	lsr	r1, r1, #10
	mov	r0, #1000
	add	r1, r1, #1
	mul	r0, r0, r3
	bl	__udivsi3
	add	r0, r0, r7
	cmp	r8, r0
	bhi	.L1346
	ldrh	r3, [r10, #28]
	ands	r0, r4, r3
	movne	r0, r9
	bne	.L1335
	ldr	r2, [r6, #2744]
	cmp	r3, r2
	beq	.L1335
.L1346:
	ldrh	r3, [r10, #28]
	mov	r0, #0
	str	r0, [r5, #2728]
	str	r8, [r5, #2720]
	str	r3, [r5, #2744]
	mov	r3, #1
	str	r3, [r5, #2724]
	b	.L1335
.L1352:
	.align	2
.L1351:
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
	ldr	r6, .L1357
	mov	r4, #0
	ldr	r5, .L1357+4
	bl	FtlWriteDump_data
.L1354:
	ldrh	r2, [r5]
	uxth	r3, r4
	cmp	r2, r3
	bhi	.L1356
	mov	r0, #0
	ldmfd	sp, {r4, r5, r6, fp, sp, pc}
.L1356:
	ldr	r2, [r6, #2544]
	uxth	r0, r4
	mov	r3, #12
	mla	r3, r3, r0, r2
	ldr	r3, [r3, #4]
	cmp	r3, #0
	bge	.L1355
	bl	flush_l2p_region
.L1355:
	add	r4, r4, #1
	b	.L1354
.L1358:
	.align	2
.L1357:
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
	ldr	r4, .L1368
	mov	r5, r1
	str	r2, [fp, #-128]
	add	r2, r0, r1
	ldrh	r3, [r4]
	sub	r4, r4, #300
	cmp	r2, r3
	mvnhi	r8, #0
	bhi	.L1359
	add	r3, r4, #312
	mov	r8, #0
	ldrh	r6, [r3]
	lsr	r6, r0, r6
	lsl	r10, r6, #2
.L1361:
	cmp	r5, #0
	bne	.L1366
.L1359:
	mov	r0, r8
	sub	sp, fp, #40
	ldmfd	sp, {r4, r5, r6, r7, r8, r9, r10, fp, sp, pc}
.L1366:
	ldr	r3, [r4, #3376]
	mov	r0, r7
	ldr	ip, [r3, r10]
	ldr	r3, .L1368+4
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
	beq	.L1363
	ldr	r2, [r4, #3312]
	sub	r0, fp, #124
	str	ip, [fp, #-120]
	str	r2, [fp, #-116]
	sub	r2, fp, #104
	str	r2, [fp, #-112]
	mov	r2, #1
	mov	r1, r2
	bl	FlashReadPages
.L1364:
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
	ldr	r0, .L1368+8
	add	r6, r6, #1
	bl	FtlMapWritePage
	ldr	r3, [fp, #-128]
	cmn	r0, #1
	ldr	ip, [fp, #-136]
	mvneq	r8, #0
	add	r3, r3, ip
	str	r3, [fp, #-128]
	b	.L1361
.L1363:
	ldr	r3, .L1368+12
	ldr	r0, [r4, #3312]
	ldrh	r2, [r3]
	bl	ftl_memset
	b	.L1364
.L1369:
	.align	2
.L1368:
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
	ldr	r3, .L1372
	ldr	r2, .L1372+4
	ldr	r1, [r3, #2644]
	cmp	r1, r2
	ldmfdne	sp, {fp, sp, pc}
	ldr	r2, .L1372+8
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
	ldr	r2, .L1372+12
	bl	FtlVendorPartWrite
	ldmfd	sp, {fp, sp, pc}
.L1373:
	.align	2
.L1372:
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
	ldr	r3, .L1379
	movw	r1, #3656
	ldrh	r2, [r3, r1]
	cmp	r2, #31
	addls	r2, r2, #1
	movhi	r2, #32
	strhls	r2, [r3, r1]	@ movhi
	movls	r2, #1
	cmp	r0, #0
	bne	.L1376
	ldr	r1, [r3, #3352]
	ldr	r0, [r1, #20]
	ldr	r1, [r1, #16]
	add	r2, r2, r0
	cmp	r1, r2
	bcc	.L1377
.L1376:
	ldr	r2, [r3, #3352]
	mov	r0, #64
	ldr	r1, [r2, #16]
	str	r1, [r2, #20]
	ldr	r1, .L1379+4
	str	r1, [r2]
	ldr	r2, [r3, #3352]
	ldr	r3, .L1379+8
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
.L1377:
	mov	r0, #0
	ldmfd	sp, {fp, sp, pc}
.L1380:
	.align	2
.L1379:
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
	ldr	r5, .L1391
	add	r2, r0, r1
	mov	r8, r0
	mov	r7, r1
	ldrh	r3, [r5]
	sub	r5, r5, #300
	cmp	r2, r3
	mvnhi	r9, #0
	bhi	.L1381
	add	r3, r5, #312
	mov	r9, #0
	ldrh	r6, [r3]
	lsr	r6, r0, r6
	lsl	r3, r6, #2
	str	r3, [fp, #-128]
.L1383:
	cmp	r7, #0
	bne	.L1389
.L1381:
	mov	r0, r9
	sub	sp, fp, #40
	ldmfd	sp, {r4, r5, r6, r7, r8, r9, r10, fp, sp, pc}
.L1389:
	ldr	r2, [fp, #-128]
	mov	r0, r8
	ldr	r3, [r5, #3376]
	ldr	r3, [r3, r2]
	str	r3, [fp, #-136]
	ldr	r3, .L1391+4
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
	beq	.L1385
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
	bne	.L1387
	mov	r2, r3
	mov	r1, r6
	ldr	r0, .L1391+8
	bl	sftl_printk
	ldr	r2, [r5, #3312]
	mov	r1, r6
	ldr	r0, .L1391+12
	bl	FtlMapWritePage
.L1387:
	ldr	r1, [r5, #3312]
	lsl	r2, r4, #9
	ldr	r3, [fp, #-132]
	mov	r0, r10
	add	r1, r1, r3, lsl #9
	bl	ftl_memcpy
.L1388:
	ldr	r3, [fp, #-128]
	add	r6, r6, #1
	sub	r7, r7, r4
	add	r8, r8, r4
	add	r10, r10, r4, lsl #9
	add	r3, r3, #4
	str	r3, [fp, #-128]
	b	.L1383
.L1385:
	lsl	r2, r4, #9
	mov	r1, r3
	mov	r0, r10
	bl	ftl_memset
	b	.L1388
.L1392:
	.align	2
.L1391:
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
	ldr	r4, .L1395
	mov	r0, #64
	add	r5, r4, #3344
	ldr	r2, [r4, #3352]
	ldrh	r1, [r5]
	bl	FtlVendorPartRead
	ldr	r3, [r4, #3352]
	ldr	r2, [r3]
	ldr	r3, .L1395+4
	cmp	r2, r3
	beq	.L1394
	ldr	r1, .L1395+8
	ldr	r0, .L1395+12
	bl	sftl_printk
	ldrh	r2, [r5]
	mov	r1, #0
	ldr	r0, [r4, #3352]
	lsl	r2, r2, #9
	bl	ftl_memset
.L1394:
	mov	r0, #0
	ldmfd	sp, {r4, r5, fp, sp, pc}
.L1396:
	.align	2
.L1395:
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
	ldr	r4, .L1400
	mov	r1, #1
	mov	r0, #0
	ldr	r2, .L1400+4
	bl	FtlVendorPartRead
	ldr	r5, .L1400+8
	ldr	r3, [r4, #2644]
	cmp	r3, r5
	beq	.L1398
	mov	r1, #512
	ldr	r0, .L1400+4
	bl	__memzero
	str	r5, [r4, #2644]
.L1398:
	ldr	r3, [r4, #2644]
	cmp	r3, r5
	bne	.L1399
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
.L1399:
	ldr	r3, .L1400+12
	ldr	r0, [r4, #2600]
	ldrh	r1, [r4, #244]
	ldrh	r2, [r3]
	ldr	r3, [r4, #2604]
	mla	r0, r0, r2, r3
	bl	__udivsi3
	str	r0, [r4, #2608]
	ldmfd	sp, {r4, r5, fp, sp, pc}
.L1401:
	.align	2
.L1400:
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
	ldr	r3, .L1409
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
	beq	.L1404
	mov	r2, #1
	add	r0, r3, #3456
	mov	r1, r2
	bl	FlashReadPages
.L1405:
	ldr	r2, [r4, #3464]
	mov	r1, r5
	mov	r0, r6
	bl	FtlMapWritePage
	ldmfd	sp, {r4, r5, r6, fp, sp, pc}
.L1404:
	movw	r3, #314
	mov	r1, #255
	ldrh	r2, [r4, r3]
	bl	ftl_memset
	b	.L1405
.L1410:
	.align	2
.L1409:
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
	ldr	r4, .L1420
	mov	r5, #0
	mov	r1, #255
	ldr	r6, .L1420+4
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
	ldr	r3, .L1420+8
	strh	r3, [r7]	@ movhi
	ldr	r3, [r4, #2636]
	str	r5, [r7, #8]
	ldrh	r2, [r4, #30]
	str	r3, [r7, #4]
	ldr	r3, .L1420+12
	str	r3, [r4, #2472]
	ldr	r3, .L1420+16
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
.L1412:
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
	blt	.L1413
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
.L1413:
	ldr	r3, .L1420+20
	ldr	r0, [r4, #3304]
	ldrh	r1, [r3]
	bl	js_hash
	mov	r3, #1
	str	r0, [r7, #12]
	mov	r2, r3
	mov	r1, r3
	ldr	r0, .L1420+24
	bl	FlashProgPages
	ldrh	r3, [r6, #2]
	ldr	r2, [r4, #3456]
	add	r3, r3, #1
	uxth	r3, r3
	cmn	r2, #1
	strh	r3, [r6, #2]	@ movhi
	bne	.L1414
	cmp	r3, #1
	bne	.L1415
	movw	r2, #1138
	ldr	r1, .L1420+28
	ldr	r0, .L1420+32
	bl	sftl_printk
.L1415:
	ldrh	r3, [r6, #2]
	add	r5, r5, #1
	uxth	r5, r5
	cmp	r3, #1
	ldrheq	r3, [r10]
	subeq	r3, r3, #1
	strheq	r3, [r6, #2]	@ movhi
	cmp	r5, #3
	bls	.L1412
	mov	r2, r5
	ldr	r1, [r4, #3460]
	ldr	r0, .L1420+36
	bl	sftl_printk
.L1418:
	b	.L1418
.L1414:
	cmp	r2, #256
	cmpne	r3, #1
	beq	.L1412
	movw	r3, #65535
	cmp	r8, r3
	beq	.L1419
	mov	r1, #1
	mov	r0, r8
	bl	FtlFreeSysBlkQueueIn
.L1419:
	mov	r0, #0
	ldmfd	sp, {r4, r5, r6, r7, r8, r9, r10, fp, sp, pc}
.L1421:
	.align	2
.L1420:
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
	ldr	r3, .L1425
	ldr	r3, [r3]
	cmp	r3, #1
	bne	.L1424
	bl	FtlSysFlush
.L1424:
	mov	r0, #0
	ldmfd	sp, {fp, sp, pc}
.L1426:
	.align	2
.L1425:
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
	ldr	r4, .L1442
	mov	r8, r0
	mov	r6, r1
	ldr	r3, [r4, #344]
	cmp	r2, r3
	mvnhi	r0, #0
	bhi	.L1427
	cmp	r1, #31
	bhi	.L1429
.L1434:
	mov	r0, #0
.L1427:
	sub	sp, fp, #32
	ldmfd	sp, {r4, r5, r6, r7, r8, fp, sp, pc}
.L1429:
	movw	r3, #262
	ldrh	r5, [r4, r3]
	mov	r1, r5
	bl	__udivsi3
	smulbb	r3, r0, r5
	mov	r7, r0
	sub	r8, r8, r3
	uxth	r8, r8
	cmp	r8, #0
	beq	.L1430
	sub	r5, r5, r8
	add	r7, r0, #1
	cmp	r5, r6
	movcs	r5, r6
	uxth	r5, r5
	sub	r6, r6, r5
.L1430:
	ldr	r5, .L1442+4
	mvn	r3, #0
	str	r3, [fp, #-36]
	mov	r8, r5
.L1431:
	ldrh	r3, [r5]
	cmp	r6, r3
	bcs	.L1433
	ldr	r3, [r4, #3660]
	cmp	r3, #32
	bls	.L1434
	mov	r5, #0
	str	r5, [r4, #3660]
	bl	l2p_flush
	bl	FtlVpcTblFlush
	b	.L1434
.L1433:
	mov	r2, #0
	sub	r1, fp, #40
	mov	r0, r7
	bl	log2phys
	ldr	r3, [fp, #-40]
	cmn	r3, #1
	beq	.L1432
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
.L1432:
	ldrh	r3, [r8]
	add	r7, r7, #1
	sub	r6, r6, r3
	b	.L1431
.L1443:
	.align	2
.L1442:
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
	ldr	r4, .L1457
	ldr	r1, .L1457+4
	ldr	r0, .L1457+8
	bl	sftl_printk
	ldrh	r2, [r4, #246]
	mov	r1, #0
	ldr	r0, [r4, #3356]
	lsl	r2, r2, #1
	bl	ftl_memset
.L1445:
	ldr	r3, [r4, #2556]
	cmp	r5, r3
	bcc	.L1447
	ldr	r10, .L1457+12
	mov	r7, #0
	movw	r9, #65535
.L1448:
	ldrh	r3, [r4, #244]
	uxth	r6, r7
	cmp	r3, r6
	bhi	.L1453
	bl	l2p_flush
	bl	FtlVpcTblFlush
	ldmib	sp, {r4, r5, r6, r7, r8, r9, r10, fp, sp, pc}
.L1447:
	mov	r2, #0
	sub	r1, fp, #44
	mov	r0, r5
	bl	log2phys
	ldr	r0, [fp, #-44]
	cmn	r0, #1
	beq	.L1446
	ubfx	r0, r0, #10, #16
	bl	P2V_block_in_plane
	ldr	r2, [r4, #3356]
	lsl	r0, r0, #1
	ldrh	r3, [r2, r0]
	add	r3, r3, #1
	strh	r3, [r2, r0]	@ movhi
.L1446:
	add	r5, r5, #1
	b	.L1445
.L1453:
	uxth	r8, r7
	ldr	r3, [r4, #76]
	lsl	r5, r8, #1
	ldrh	r2, [r3, r5]
	ldr	r3, [r4, #3356]
	ldrh	r3, [r3, r5]
	cmp	r2, r9
	cmpne	r2, r3
	beq	.L1450
	mov	r1, r8
	mov	r0, r10
	bl	sftl_printk
	ldrh	r3, [r4, #28]
	cmp	r3, r6
	beq	.L1450
	ldrh	r3, [r4, #128]
	cmp	r3, r6
	beq	.L1450
	ldrh	r3, [r4, #80]
	cmp	r3, r6
	beq	.L1450
	ldr	r3, [r4, #76]
	ldrh	r2, [r3, r5]
	cmp	r2, #0
	ldr	r2, [r4, #3356]
	ldrh	r2, [r2, r5]
	strh	r2, [r3, r5]	@ movhi
	bne	.L1452
.L1450:
	add	r7, r7, #1
	b	.L1448
.L1452:
	mov	r0, r8
	bl	update_vpc_list
	b	.L1450
.L1458:
	.align	2
.L1457:
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
	ldr	r4, .L1469
	mov	r6, r0
	ldrh	r5, [r0]
	ldrh	r3, [r4, #244]
	cmp	r3, r5
	bcs	.L1460
	movw	r2, #2755
	ldr	r1, .L1469+4
	ldr	r0, .L1469+8
	bl	sftl_printk
.L1460:
	movw	r3, #65535
	cmp	r5, r3
	beq	.L1461
	ldr	r2, [r4, #76]
	lsl	r3, r5, #1
	mov	r0, r5
	ldrh	r3, [r2, r3]
	cmp	r3, #0
	beq	.L1462
	bl	INSERT_DATA_LIST
.L1461:
	mov	r3, #1
	strb	r3, [r6, #8]
	movw	r3, #3442
	ldrh	r0, [r4, r3]
	movw	r3, #65535
	cmp	r0, r3
	beq	.L1463
	cmp	r5, r0
	bne	.L1464
	ldr	r2, [r4, #76]
	lsl	r3, r0, #1
	ldrh	r3, [r2, r3]
	cmp	r3, #0
	beq	.L1465
.L1464:
	bl	update_vpc_list
.L1465:
	mvn	r2, #0
	movw	r3, #3442
	strh	r2, [r4, r3]	@ movhi
.L1463:
	mov	r0, r6
	bl	allocate_data_superblock
	bl	l2p_flush
	mov	r0, #0
	bl	FtlEctTblFlush
	bl	FtlVpcTblFlush
	mov	r0, #0
	ldmfd	sp, {r4, r5, r6, fp, sp, pc}
.L1462:
	bl	INSERT_FREE_LIST
	b	.L1461
.L1470:
	.align	2
.L1469:
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
	ldr	r8, .L1484
	mov	r5, r3
	mov	r2, #0
	ldrb	r3, [r3, #9]	@ zero_extendqisi2
	mov	r4, r0
	mov	r9, r1
	bl	FlashProgPages
.L1472:
	cmp	r6, r9
	beq	.L1478
	ldr	r7, .L1484+4
	b	.L1479
.L1474:
	ldr	r1, [r4, #4]
	mov	r0, r7
	bl	sftl_printk
	ldr	r0, [r4, #4]
	ubfx	r0, r0, #10, #16
	bl	P2V_block_in_plane
	bl	decrement_vpc_count
	ldrh	r3, [r5, #4]
	cmp	r3, #0
	bne	.L1473
	mov	r0, r5
	bl	allocate_new_data_superblock
.L1473:
	mov	r0, r5
	bl	get_new_active_ppa
	mov	r2, #0
	str	r0, [r4, #4]
	str	r0, [fp, #-44]
	mov	r1, #1
	ldrb	r3, [r5, #9]	@ zero_extendqisi2
	mov	r0, r4
	bl	FlashProgPages
.L1479:
	ldr	r2, [r4]
	cmn	r2, #1
	cmpne	r2, #256
	beq	.L1474
	ldrb	r2, [r5, #6]	@ zero_extendqisi2
	ldrh	r3, [r8, #236]
	cmp	r2, r3
	bcc	.L1475
	movw	r2, #982
	ldr	r1, .L1484+8
	ldr	r0, .L1484+12
	bl	sftl_printk
.L1475:
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
	beq	.L1476
	ldr	r2, [r8, #76]
	lsl	r3, r0, #1
	ldrh	r2, [r2, r3]
	cmp	r2, #0
	bne	.L1477
	mov	r1, r0
	ldr	r0, .L1484+16
	bl	sftl_printk
.L1477:
	mov	r0, r7
	bl	decrement_vpc_count
.L1476:
	add	r6, r6, #1
	add	r4, r4, #20
	b	.L1472
.L1478:
	ldr	r3, .L1484
	ldrb	r2, [r5, #6]	@ zero_extendqisi2
	ldrh	r3, [r3, #236]
	cmp	r2, r3
	ldmibcc	sp, {r4, r5, r6, r7, r8, r9, r10, fp, sp, pc}
	movw	r2, #997
	ldr	r1, .L1484+8
	ldr	r0, .L1484+12
	bl	sftl_printk
	ldmib	sp, {r4, r5, r6, r7, r8, r9, r10, fp, sp, pc}
.L1485:
	.align	2
.L1484:
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
	ldr	r4, .L1510
	ldrh	r2, [r4, #128]
	ldrh	r1, [r4, r3]
	movw	r3, #65535
	cmp	r2, r3
	bne	.L1487
.L1494:
	ldrh	r2, [r4, #128]
	mov	r3, #0
	str	r3, [r4, #3448]
	movw	r3, #65535
	cmp	r2, r3
	bne	.L1507
.L1488:
	mov	r0, #0
	ldmib	sp, {r4, r5, r6, r7, r8, r9, r10, fp, sp, pc}
.L1487:
	cmp	r0, #0
	ldr	r5, .L1510+4
	beq	.L1490
	ldrh	r2, [r5, #4]
	cmp	r2, r3
	beq	.L1491
.L1492:
	mov	r1, #2
.L1490:
	ldr	r0, .L1510+8
	bl	FtlGcScanTempBlk
	ldrh	r2, [r5, #4]
	movw	r3, #65535
	str	r0, [fp, #-44]
	cmp	r2, r3
	beq	.L1494
	mov	r0, #1
	ldmib	sp, {r4, r5, r6, r7, r8, r9, r10, fp, sp, pc}
.L1491:
	mov	r3, #0
	strh	r3, [r5, #4]	@ movhi
	ldrh	r3, [r4, #228]
	cmp	r3, #17
	bhi	.L1492
	b	.L1490
.L1507:
	movw	r3, #3208
	ldrb	r1, [r4, #135]	@ zero_extendqisi2
	ldrh	r2, [r4, r3]
	movw	r3, #306
	ldrh	r3, [r4, r3]
	mul	r3, r3, r1
	cmp	r2, r3
	beq	.L1495
	mov	r2, #164
	ldr	r1, .L1510+12
	ldr	r0, .L1510+16
	bl	sftl_printk
.L1495:
	movw	r0, #306
	ldrb	r2, [r4, #135]	@ zero_extendqisi2
	ldrh	r0, [r4, r0]
	mov	r5, #0
	ldrh	r3, [r4, #128]
	mov	r9, #12
	ldr	r1, [r4, #76]
	ldr	r8, .L1510+20
	smulbb	r2, r2, r0
	lsl	r3, r3, #1
	strh	r2, [r1, r3]	@ movhi
	movw	r3, #3208
	ldr	r2, [r4, #2580]
	ldrh	r3, [r4, r3]
	add	r3, r3, r2
	str	r3, [r4, #2580]
.L1496:
	ldrh	r2, [r8]
	uxth	r3, r5
	cmp	r2, r3
	bhi	.L1500
	movw	r0, #65535
	bl	decrement_vpc_count
	ldrh	r0, [r4, #128]
	ldr	r2, [r4, #76]
	lsl	r3, r0, #1
	ldrh	r3, [r2, r3]
	cmp	r3, #0
	beq	.L1501
	bl	INSERT_DATA_LIST
.L1502:
	ldr	r6, .L1510+24
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
	b	.L1488
.L1500:
	uxth	r7, r5
	ldr	r10, [r4, #3204]
	ldr	r3, [r4, #2556]
	mul	r7, r9, r7
	add	r6, r10, r7
	ldr	r0, [r6, #8]
	cmp	r0, r3
	bcc	.L1497
.L1508:
	ldrh	r0, [r4, #128]
	b	.L1509
.L1497:
	mov	r2, #0
	sub	r1, fp, #44
	bl	log2phys
	ldr	r3, [fp, #-44]
	ldr	r0, [r10, r7]
	cmp	r0, r3
	bne	.L1499
	ubfx	r0, r0, #10, #16
	bl	P2V_block_in_plane
	mov	r2, #1
	mov	r7, r0
	add	r1, r6, #4
	ldr	r0, [r6, #8]
	bl	log2phys
	mov	r0, r7
.L1509:
	bl	decrement_vpc_count
	b	.L1498
.L1499:
	ldr	r2, [r6, #4]
	cmp	r3, r2
	bne	.L1508
.L1498:
	add	r5, r5, #1
	b	.L1496
.L1501:
	bl	INSERT_FREE_LIST
	b	.L1502
.L1511:
	.align	2
.L1510:
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
	ldr	r4, .L1514
	movw	r5, #306
	ldrh	r1, [r4, r5]
	add	r0, r4, #128
	bl	FtlGcScanTempBlk
	ldrh	r2, [r4, #130]
	ldrh	r3, [r4, r5]
	cmp	r2, r3
	ldmfdcc	sp, {r4, r5, fp, sp, pc}
	ldr	r0, .L1514+4
	bl	FtlMapBlkWriteDump_data
	mov	r0, #0
	bl	FtlGcFreeTempBlock
	mov	r3, #0
	str	r3, [r4, #3448]
	ldmfd	sp, {r4, r5, fp, sp, pc}
.L1515:
	.align	2
.L1514:
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
	ldr	r4, .L1517
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
.L1518:
	.align	2
.L1517:
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
	ldr	r4, .L1527
	mov	r3, #0
	mov	r5, #0
	mov	r6, #20
	mov	r2, r3
	ldr	r1, [r4, #3172]
	ldr	r0, [r4, #3296]
	bl	FlashProgPages
.L1520:
	ldr	r1, [r4, #3172]
	uxth	r3, r5
	cmp	r1, r3
	bhi	.L1523
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
.L1523:
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
	b	.L1520
.L1528:
	.align	2
.L1527:
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
	ldr	r4, .L1532
	movw	r3, #65535
	ldrh	r2, [r4, #128]
	cmp	r2, r3
	beq	.L1530
	ldrh	r3, [r4, #132]
	cmp	r3, #0
	bne	.L1531
.L1530:
	mov	r0, #0
	mov	r5, #0
	bl	FtlGcFreeTempBlock
	ldr	r0, .L1532+4
	strb	r5, [r4, #136]
	bl	allocate_data_superblock
	ldr	r3, .L1532+8
	strh	r5, [r3]	@ movhi
	movw	r3, #3208
	strh	r5, [r4, r3]	@ movhi
	bl	l2p_flush
	mov	r0, r5
	bl	FtlEctTblFlush
	bl	FtlVpcTblFlush
.L1531:
	ldr	r0, .L1532+4
	bl	get_new_active_ppa
	ldmfd	sp, {r4, r5, fp, sp, pc}
.L1533:
	.align	2
.L1532:
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
	ldr	r3, .L1662
	str	r0, [fp, #-64]
	ldr	r0, [r3, #3276]
	mov	r4, r3
	cmp	r0, #0
	movne	r0, #0
	bne	.L1534
	movw	r2, #2532
	ldrh	r2, [r3, r2]
	cmp	r2, #47
	bls	.L1534
	ldr	r2, .L1662+4
	ldrh	r1, [r2, #4]
	movw	r2, #65535
	cmp	r1, r2
	bne	.L1536
.L1539:
	ldrh	r3, [r4, #224]
	movw	r2, #65535
	cmp	r3, r2
	bne	.L1537
.L1538:
	bl	FtlReadRefresh
	ldr	r3, [r4, #3160]
	movw	r6, #65535
	ldr	r2, [fp, #-64]
	add	r3, r3, #1
	add	r3, r3, r2, lsl #7
	ldrh	r2, [r4, #176]
	str	r3, [r4, #3160]
	cmp	r2, r6
	bne	.L1540
	ldrh	r6, [r4, #128]
	cmp	r6, r2
	movne	r6, r2
	bne	.L1540
	ldrh	r5, [r4, #226]
	cmp	r5, r6
	bne	.L1540
	ldrh	r2, [r4, #228]
	cmp	r2, #24
	movcc	r2, #5120
	movcs	r2, #1024
	cmp	r3, r2
	movls	r6, r5
	bls	.L1540
	ldr	r3, .L1662+8
	mov	r8, #0
	str	r8, [r4, #3160]
	strh	r8, [r3]	@ movhi
	bl	GetSwlReplaceBlock
	cmp	r0, r5
	mov	r6, r0
	movne	r5, r0
	bne	.L1542
	movw	r7, #3158
	ldrh	r2, [r4, #228]
	ldrh	r3, [r4, r7]
	cmp	r2, r3
	bcs	.L1543
	mov	r0, #64
	bl	List_get_gc_head_node
	uxth	r3, r0
	cmp	r3, r6
	beq	.L1545
	mov	r0, r3
	ldr	r3, [r4, #76]
	lsl	r0, r0, #1
	ldrh	r3, [r3, r0]
	cmp	r3, #7
	bhi	.L1546
	mov	r0, r8
	bl	List_get_gc_head_node
	uxth	r5, r0
	mov	r3, #128
	strh	r3, [r4, r7]	@ movhi
	cmp	r5, r6
	bne	.L1542
.L1545:
	bl	FtlGcReFreshBadBlk
.L1540:
	movw	r1, #65535
	ldr	r2, [fp, #-64]
	sub	r3, r6, r1
	clz	r3, r3
	lsr	r3, r3, #5
	cmp	r2, #0
	movne	r2, #0
	andeq	r2, r3, #1
	cmp	r2, #0
	beq	.L1548
	ldrh	r3, [r4, #228]
	cmp	r3, #24
	movhi	r5, #1
	bhi	.L1549
	movw	r2, #306
	cmp	r3, #16
	ldrh	r5, [r4, r2]
	lsrhi	r5, r5, #5
	bhi	.L1549
	cmp	r3, #12
	lsrhi	r5, r5, #4
	bhi	.L1549
	cmp	r3, #8
	lsrhi	r5, r5, #2
.L1549:
	movw	r2, #3156
	ldrh	r1, [r4, r2]
	cmp	r1, r3
	bcs	.L1553
	ldrh	r3, [r4, #128]
	movw	r1, #65535
	cmp	r3, r1
	bne	.L1554
	ldrh	r1, [r4, #226]
	cmp	r1, r3
	bne	.L1554
	ldr	r3, .L1662+8
	ldrh	r0, [r3]
	cmp	r0, #0
	bne	.L1555
	ldr	r3, [r4, #2556]
	ldr	r1, [r4, #2552]
	add	r3, r3, r3, lsl #1
	cmp	r1, r3, lsr #2
	movcs	r3, #18
	bcs	.L1655
.L1555:
	ldr	r3, .L1662+12
	movw	r2, #3156
	ldrh	r3, [r3]
	add	r3, r3, r3, lsl #1
	asr	r3, r3, #2
.L1655:
	strh	r3, [r4, r2]	@ movhi
	mov	r3, #0
	str	r3, [r4, #3168]
.L1534:
	sub	sp, fp, #40
	ldmfd	sp, {r4, r5, r6, r7, r8, r9, r10, fp, sp, pc}
.L1536:
	ldrh	r3, [r3, #128]
	cmp	r3, r2
	beq	.L1539
	mov	r0, #1
	bl	FtlGcFreeTempBlock
	cmp	r0, #0
	beq	.L1539
	mov	r0, #1
	b	.L1534
.L1537:
	ldrh	r1, [r4, #226]
	cmp	r1, r2
	strheq	r3, [r4, #226]	@ movhi
	mvneq	r3, #0
	strheq	r3, [r4, #224]	@ movhi
	b	.L1538
.L1546:
	mov	r3, #64
.L1654:
	strh	r3, [r4, r7]	@ movhi
	b	.L1545
.L1543:
	mov	r3, #80
	b	.L1654
.L1542:
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
	ldr	r0, .L1662+16
	str	r1, [sp]
	mov	r1, r5
	bl	sftl_printk
	b	.L1545
.L1554:
	ldr	r3, .L1662+12
	movw	r2, #3156
	ldrh	r3, [r3]
	add	r3, r3, r3, lsl #1
	asr	r3, r3, #2
	strh	r3, [r4, r2]	@ movhi
.L1553:
	movw	r3, #3210
	movw	r6, #65535
	ldrh	r3, [r4, r3]
	cmp	r3, #0
	addne	r5, r5, #32
	uxthne	r5, r5
.L1559:
	ldrh	r3, [r4, #176]
	movw	r2, #65535
	cmp	r3, r2
	bne	.L1568
	cmp	r6, r3
	strhne	r6, [r4, #176]	@ movhi
	bne	.L1570
	ldrh	r3, [r4, #226]
	cmp	r3, r6
	beq	.L1570
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
.L1570:
	ldrh	r0, [r4, #176]
	mov	r3, #0
	strb	r3, [r4, #184]
	movw	r3, #65535
	cmp	r0, r3
	beq	.L1568
	bl	IsBlkInGcList
	cmp	r0, #0
	mvnne	r3, #0
	strhne	r3, [r4, #176]	@ movhi
	movw	r3, #65535
	ldrh	r2, [r4, #176]
	cmp	r2, r3
	beq	.L1568
	ldr	r0, .L1662+20
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
.L1568:
	ldrh	r3, [r4, #176]
	ldrh	r2, [r4, #28]
	cmp	r2, r3
	beq	.L1574
	ldrh	r2, [r4, #80]
	cmp	r2, r3
	beq	.L1574
	ldrh	r2, [r4, #128]
	cmp	r2, r3
	bne	.L1575
.L1574:
	mvn	r3, #0
	strh	r3, [r4, #176]	@ movhi
.L1575:
	ldr	r4, .L1662
	mov	r7, r4
.L1609:
	ldrh	r2, [r4, #176]
	movw	r3, #65535
	cmp	r2, r3
	bne	.L1576
	ldr	r10, .L1662+24
	mov	r3, #0
	str	r3, [r4, #3168]
	mov	r9, r10
.L1577:
	ldrh	r8, [r10]
	mov	r0, r8
	bl	List_get_gc_head_node
	uxth	r2, r0
	movw	r3, #65535
	cmp	r2, r3
	strh	r2, [r7, #176]	@ movhi
	bne	.L1578
	mov	r3, #0
	mov	r0, #8
	strh	r3, [r10]	@ movhi
	b	.L1534
.L1548:
	ldrh	r7, [r4, #128]
	cmp	r7, r1
	bne	.L1623
	ldrh	r1, [r4, #226]
	cmp	r1, r7
	movne	r3, #0
	andeq	r3, r3, #1
	cmp	r3, #0
	beq	.L1623
	ldrh	r3, [r4, #176]
	cmp	r3, r7
	beq	.L1560
.L1565:
	mov	r6, r7
.L1623:
	mov	r5, #1
	b	.L1559
.L1560:
	str	r2, [r4, #3168]
	movw	r2, #3156
	ldrh	r1, [r4, #228]
	ldrh	r3, [r4, r2]
	ldr	r5, .L1662+8
	cmp	r1, r3
	bls	.L1561
	ldrh	r3, [r5]
	cmp	r3, #0
	bne	.L1562
	ldr	r3, [r4, #2556]
	ldr	r1, [r4, #2552]
	add	r3, r3, r3, lsl #1
	cmp	r1, r3, lsr #2
	movcs	r3, #18
	bcs	.L1656
.L1562:
	ldr	r3, .L1662+12
	movw	r2, #3156
	ldrh	r3, [r3]
	add	r3, r3, r3, lsl #1
	asr	r3, r3, #2
.L1656:
	strh	r3, [r4, r2]	@ movhi
	bl	FtlReadRefresh
	mov	r0, #0
	bl	List_get_gc_head_node
	uxth	r0, r0
	ldr	r3, [r4, #76]
	lsl	r0, r0, #1
	ldrh	r3, [r3, r0]
	cmp	r3, #4
	bls	.L1561
.L1659:
	ldrh	r0, [r5]
	b	.L1534
.L1561:
	ldrh	r0, [r5]
	cmp	r0, #0
	bne	.L1565
	ldr	r3, .L1662+12
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
	ldr	r3, .L1662+28
	ldrh	r1, [r3]
	ldrh	r3, [r4, #236]
	mul	r3, r3, r1
	add	r3, r3, r3, lsr #31
	cmp	r2, r3, asr #1
	ble	.L1566
	ldrh	r3, [r4, #228]
	sub	r6, r6, #1
	cmp	r3, r6
	blt	.L1566
	bl	FtlReadRefresh
	b	.L1659
.L1566:
	cmp	r2, #0
	bne	.L1565
	movw	r0, #65535
	bl	decrement_vpc_count
	ldrh	r0, [r4, #228]
	add	r0, r0, #1
	b	.L1534
.L1578:
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
	bne	.L1577
	lsl	r0, r3, #1
	ldr	r3, .L1662+32
	ldrh	lr, [r7, #236]
	uxth	r8, r8
	ldr	r1, [r7, #76]
	ldrh	r3, [r3]
	strh	r8, [r10]	@ movhi
	ldrh	ip, [r1, r0]
	mul	r3, lr, r3
	add	lr, r3, r3, lsr #31
	cmp	ip, lr, asr #1
	bgt	.L1581
	cmp	ip, #8
	cmphi	r8, #48
	bls	.L1582
	ldr	ip, .L1662+36
	ldrh	ip, [ip]
	cmp	ip, #35
	bhi	.L1582
.L1581:
	mov	ip, #0
	strh	ip, [r9]	@ movhi
.L1582:
	ldrh	r1, [r1, r0]
	movw	r0, #65535
	cmp	r3, r1
	cmple	r6, r0
	bne	.L1583
	ldrh	r3, [r9]
	cmp	r3, #3
	bhi	.L1583
	mvn	r3, #0
	strh	r3, [r7, #176]	@ movhi
	mov	r3, #0
	strh	r3, [r9]	@ movhi
.L1661:
	ldr	r3, .L1662+8
	b	.L1660
.L1583:
	cmp	r1, #0
	bne	.L1584
	movw	r0, #65535
	bl	decrement_vpc_count
	ldrh	r3, [r9]
	add	r3, r3, #1
	strh	r3, [r9]	@ movhi
	b	.L1577
.L1584:
	mov	r3, #0
	strb	r3, [r7, #184]
	ldrh	r3, [r7, #28]
	cmp	r3, r2
	bne	.L1585
	movw	r2, #717
	ldr	r1, .L1662+40
	ldr	r0, .L1662+44
	bl	sftl_printk
.L1585:
	ldrh	r2, [r7, #176]
	ldrh	r3, [r7, #80]
	cmp	r2, r3
	bne	.L1586
	movw	r2, #718
	ldr	r1, .L1662+40
	ldr	r0, .L1662+44
	bl	sftl_printk
.L1586:
	ldrh	r2, [r7, #176]
	ldrh	r3, [r7, #128]
	cmp	r2, r3
	bne	.L1587
	movw	r2, #719
	ldr	r1, .L1662+40
	ldr	r0, .L1662+44
	bl	sftl_printk
.L1587:
	ldr	r0, .L1662+20
	bl	make_superblock
	ldrh	r2, [r7, #176]
	mov	r3, #0
	ldr	r1, .L1662+48
	ldr	r0, [r7, #76]
	lsl	r2, r2, #1
	strh	r3, [r1]	@ movhi
	ldrh	r2, [r0, r2]
	strh	r3, [r7, #178]	@ movhi
	strb	r3, [r7, #182]
	strh	r2, [r1, #2]	@ movhi
.L1576:
	mov	r3, #1
	str	r3, [r7, #3276]
	ldr	r3, .L1662+32
	ldrh	r3, [r3]
	str	r3, [fp, #-52]
	ldr	r3, [fp, #-64]
	cmp	r3, #0
	beq	.L1588
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
.L1588:
	ldrh	r3, [r7, #178]
	ldr	r1, [fp, #-52]
	add	r2, r3, r5
	cmp	r2, r1
	movgt	r2, r1
	subgt	r5, r2, r3
	mov	r3, #0
	uxthgt	r5, r5
.L1658:
	str	r3, [fp, #-60]
	ldrh	r3, [fp, #-60]
	cmp	r5, r3
	bls	.L1598
	ldr	r3, [fp, #-60]
	mov	lr, #20
	ldrh	r1, [r4, #178]
	ldrh	r9, [r4, #236]
	ldr	r0, [r4, #3188]
	add	r1, r1, r3
	mov	r3, #0
	ldr	ip, .L1662+52
	mov	r8, r3
	b	.L1599
.L1592:
	ldrh	r2, [ip, #2]!
	movw	r10, #65535
	add	r3, r3, #1
	cmp	r2, r10
	mlane	r10, lr, r8, r0
	addne	r8, r8, #1
	orrne	r2, r1, r2, lsl #10
	uxthne	r8, r8
	strne	r2, [r10, #4]
.L1599:
	uxth	r2, r3
	cmp	r9, r2
	bhi	.L1592
	ldrb	r2, [r4, #184]	@ zero_extendqisi2
	mov	r1, r8
	bl	FlashReadPages
	mov	r3, #0
.L1657:
	str	r3, [fp, #-56]
	ldrh	r3, [fp, #-56]
	cmp	r8, r3
	ldrls	r3, [fp, #-60]
	addls	r3, r3, #1
	bls	.L1658
.L1597:
	ldr	r3, [fp, #-56]
	mov	r9, #20
	mul	r9, r9, r3
	ldr	r3, [r4, #3188]
	add	r2, r3, r9
	ldr	r3, [r3, r9]
	cmn	r3, #1
	beq	.L1594
	ldr	r10, [r2, #12]
	movw	r2, #61589
	ldrh	r1, [r10]
	cmp	r1, r2
	bne	.L1594
	ldr	ip, [r10, #8]
	cmn	ip, #1
	bne	.L1595
	movw	r2, #753
	ldr	r1, .L1662+40
	ldr	r0, .L1662+44
	str	ip, [fp, #-68]
	bl	sftl_printk
	ldr	ip, [fp, #-68]
.L1595:
	mov	r2, #0
	sub	r1, fp, #48
	mov	r0, ip
	bl	log2phys
	ldr	r2, [r4, #3188]
	ldr	r1, [fp, #-48]
	add	r2, r2, r9
	ldr	r0, [r2, #4]
	cmp	r0, r1
	bne	.L1594
	ldr	r3, .L1662+48
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
	beq	.L1596
	ldrh	r3, [r4, #132]
	cmp	r3, #0
	bne	.L1594
.L1596:
	bl	Ftl_gc_temp_data_write_back
	cmp	r0, #0
	beq	.L1594
	ldr	r3, .L1662
	mov	r2, #0
	mvn	r1, #0
	str	r2, [r3, #3276]
	strh	r1, [r3, #176]	@ movhi
	strh	r2, [r3, #178]	@ movhi
	add	r3, r3, #3664
.L1660:
	ldrh	r0, [r3]
	b	.L1534
.L1594:
	ldr	r3, [fp, #-56]
	add	r3, r3, #1
	b	.L1657
.L1598:
	ldrh	r3, [r4, #178]
	add	r5, r5, r3
	ldr	r3, [fp, #-52]
	uxth	r5, r5
	cmp	r3, r5
	strh	r5, [r4, #178]	@ movhi
	bhi	.L1600
	ldr	r3, [r4, #3172]
	cmp	r3, #0
	beq	.L1601
	bl	Ftl_gc_temp_data_write_back
	cmp	r0, #0
	movne	r3, #0
	strne	r3, [r4, #3276]
	bne	.L1661
.L1601:
	ldr	r0, .L1662+48
	ldrh	r5, [r0]
	cmp	r5, #0
	bne	.L1602
	ldrh	r1, [r4, #176]
	ldr	r2, [r4, #76]
	lsl	r3, r1, #1
	ldrh	r3, [r2, r3]
	cmp	r3, #0
	beq	.L1602
	ldrh	r0, [r0, #2]
	ldrh	r2, [r4, #178]
	str	r3, [sp]
	mov	r3, r5
	str	r0, [sp, #4]
	ldr	r0, .L1662+56
	bl	sftl_printk
.L1603:
	ldr	r3, [r4, #2556]
	cmp	r5, r3
	bcs	.L1605
	mov	r2, #0
	sub	r1, fp, #44
	mov	r0, r5
	bl	log2phys
	ldr	r8, [fp, #-44]
	cmn	r8, #1
	beq	.L1604
	ubfx	r0, r8, #10, #16
	bl	P2V_block_in_plane
	ldrh	r3, [r4, #176]
	cmp	r3, r0
	bne	.L1604
	mov	r2, r8
	mov	r1, r5
	ldr	r0, .L1662+60
	bl	sftl_printk
.L1605:
	ldr	r3, [r4, #2556]
	cmp	r5, r3
	bcc	.L1602
	ldrh	r3, [r4, #176]
	mov	r1, #0
	ldr	r2, [r4, #76]
	lsl	r3, r3, #1
	strh	r1, [r2, r3]	@ movhi
	ldrh	r0, [r4, #176]
	bl	update_vpc_list
	bl	l2p_flush
	bl	FtlVpcTblFlush
.L1602:
	mvn	r3, #0
	strh	r3, [r4, #176]	@ movhi
.L1600:
	mov	r3, #0
	str	r3, [r4, #3276]
	ldrh	r3, [r4, #228]
	cmp	r3, #2
	bhi	.L1608
	ldr	r3, .L1662+32
	ldrh	r5, [r3]
	b	.L1609
.L1604:
	add	r5, r5, #1
	b	.L1603
.L1608:
	ldr	r2, .L1662+8
	ldrh	r0, [r2]
	cmp	r0, #0
	addeq	r0, r3, #1
	b	.L1534
.L1663:
	.align	2
.L1662:
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
	.word	.LC125
	.word	.LC126
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
	bne	.L1665
	mov	r2, r3
	ldr	r1, [fp, #-52]
	add	r0, r6, #256
	bl	FtlVendorPartRead
	str	r0, [fp, #-48]
.L1664:
	ldr	r0, [fp, #-48]
	sub	sp, fp, #40
	ldmfd	sp, {r4, r5, r6, r7, r8, r9, r10, fp, sp, pc}
.L1665:
	ldr	r3, [fp, #-52]
	ldr	r10, .L1700
	add	r3, r1, r3
	str	r3, [fp, #-60]
	ldr	r2, [fp, #-60]
	ldr	r3, [r10, #344]
	cmp	r2, r3
	mvnhi	r3, #0
	strhi	r3, [fp, #-48]
	bhi	.L1664
	ldr	r3, .L1700+4
	ldr	r7, [r3]
	cmn	r7, #1
	streq	r7, [fp, #-48]
	beq	.L1664
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
.L1667:
	ldr	r3, [fp, #-56]
	cmp	r3, #0
	bne	.L1684
	ldr	r3, .L1700
	movw	r2, #3210
	ldrh	r2, [r3, r2]
	cmp	r2, #0
	bne	.L1685
	ldrh	r3, [r3, #228]
	cmp	r3, #31
	bhi	.L1664
.L1685:
	mov	r1, #1
	mov	r0, #0
	bl	rk_ftl_garbage_collect
	b	.L1664
.L1684:
	mov	r2, #0
	sub	r1, fp, #44
	mov	r0, r4
	bl	log2phys
	ldr	r2, [fp, #-44]
	cmn	r2, #1
	moveq	r10, #0
	ldreq	r2, .L1700+8
	beq	.L1669
	ldr	r1, [r7, #3292]
	mov	r10, #20
	ldr	r3, [fp, #-64]
	mla	r10, r10, r5, r1
	cmp	r4, r3
	str	r2, [r10, #4]
	ldr	r2, .L1700+8
	bne	.L1673
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
.L1674:
	ldr	r3, .L1700+12
	ldr	r1, [r7, #3340]
	str	r4, [r10, #16]
	ldrh	r2, [r3]
	mul	r2, r5, r2
	add	r5, r5, #1
	bic	r2, r2, #3
	add	r2, r1, r2
	str	r2, [r10, #12]
	b	.L1672
.L1671:
	mla	r0, r0, r4, r10
	ldr	r1, [fp, #-60]
	cmp	r1, r0
	movhi	r1, #1
	movls	r1, #0
	cmp	r6, r0
	movhi	r1, #0
	cmp	r1, #0
	beq	.L1670
	sub	r0, r0, r6
	mov	r1, #512
	add	r0, r9, r0, lsl #9
	str	r2, [fp, #-80]
	bl	__memzero
	ldr	r2, [fp, #-80]
.L1670:
	add	r10, r10, #1
.L1669:
	ldrh	r0, [r2]
	cmp	r10, r0
	bcc	.L1671
.L1672:
	ldr	r3, [fp, #-56]
	add	r4, r4, #1
	subs	r3, r3, #1
	str	r3, [fp, #-56]
	beq	.L1676
	ldrh	r2, [r7, #236]
	cmp	r5, r2, lsl #2
	bne	.L1667
.L1676:
	cmp	r5, #0
	beq	.L1667
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
.L1683:
	mov	ip, #20
	ldr	r2, [r7, #3292]
	mul	ip, ip, r10
	ldr	r3, [fp, #-64]
	add	r2, r2, ip
	ldr	r1, [r2, #16]
	cmp	r3, r1
	bne	.L1678
	ldr	r1, [r2, #8]
	ldr	r2, [r7, #3316]
	cmp	r1, r2
	bne	.L1679
	ldr	r3, [fp, #-80]
	mov	r0, r9
	ldr	r2, [fp, #-84]
	str	ip, [fp, #-92]
	add	r1, r1, r3
.L1699:
	bl	ftl_memcpy
	ldr	ip, [fp, #-92]
.L1679:
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
	bne	.L1682
	ldr	r0, [r2, #4]
	ubfx	r0, r0, #10, #16
	bl	P2V_block_in_plane
	bl	FtlGcRefreshBlock
.L1682:
	add	r10, r10, #1
	cmp	r5, r10
	bne	.L1683
	mov	r5, #0
	b	.L1667
.L1673:
	ldr	r3, [fp, #-68]
	cmp	r4, r3
	ldrhne	r2, [r2]
	mulne	r2, r4, r2
	bne	.L1698
	ldr	r1, [r7, #3320]
	ldr	r3, [fp, #-60]
	str	r1, [r10, #8]
	ldrh	r1, [r2]
	mul	r2, r1, r4
	sub	r8, r3, r2
	cmp	r1, r8
	bne	.L1674
.L1698:
	sub	r2, r2, r6
	add	r2, r9, r2, lsl #9
	str	r2, [r10, #8]
	b	.L1674
.L1678:
	ldr	r3, [fp, #-68]
	cmp	r3, r1
	bne	.L1679
	ldr	r1, [r2, #8]
	ldr	r2, [r7, #3320]
	cmp	r1, r2
	bne	.L1679
	ldr	r2, .L1700+8
	str	ip, [fp, #-92]
	ldrh	r0, [r2]
	ldr	r2, [fp, #-88]
	mul	r0, r3, r0
	sub	r0, r0, r6
	add	r0, r9, r0, lsl #9
	b	.L1699
.L1701:
	.align	2
.L1700:
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
	bne	.L1705
	mov	r2, r3
	ldr	r1, [fp, #-80]
	add	r0, r9, #256
	bl	FtlVendorPartWrite
.L1704:
	sub	sp, fp, #40
	ldmfd	sp, {r4, r5, r6, r7, r8, r9, r10, fp, sp, pc}
.L1705:
	ldr	r3, [fp, #-80]
	ldr	r4, .L1756
	add	r6, r1, r3
	ldr	r3, [r4, #344]
	cmp	r6, r3
	mvnhi	r0, #0
	bhi	.L1704
	ldr	r5, .L1756+4
	ldr	r0, [r5]
	cmn	r0, #1
	beq	.L1704
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
	beq	.L1707
	ldrh	r3, [r4, #32]
	cmp	r3, #0
	addne	r4, r4, #28
	addeq	r4, r4, #80
.L1707:
	ldr	r6, [fp, #-76]
	ldr	r10, .L1756
.L1708:
	ldr	r3, [fp, #-72]
	cmp	r3, #0
	bne	.L1736
	mov	r0, r3
	ldr	r1, [fp, #-96]
	bl	rk_ftl_garbage_collect
	ldrh	r3, [r10, #228]
	cmp	r3, #5
	movls	r4, #256
	ldrls	r5, .L1756+8
	bls	.L1740
.L1739:
	mov	r0, #0
	b	.L1704
.L1736:
	ldrb	r2, [r4, #6]	@ zero_extendqisi2
	ldrh	r3, [r10, #236]
	cmp	r2, r3
	bcc	.L1709
	movw	r2, #1038
	ldr	r1, .L1756+12
	ldr	r0, .L1756+16
	bl	sftl_printk
.L1709:
	ldrh	r2, [r4, #4]
	cmp	r2, #0
	bne	.L1710
	ldr	r3, .L1756+20
	ldr	r5, .L1756+4
	cmp	r4, r3
	bne	.L1711
	ldrh	r7, [r10, #84]
	cmp	r7, #0
	bne	.L1712
	add	r0, r4, #52
	bl	allocate_new_data_superblock
	str	r7, [r5, #8]
.L1712:
	ldr	r0, .L1756+20
	bl	allocate_new_data_superblock
	ldr	r4, .L1756+20
	ldr	r2, [r5, #8]
	add	r3, r4, #52
	cmp	r2, #0
	movne	r4, r3
.L1713:
	ldrh	r3, [r4, #4]
	cmp	r3, #0
	bne	.L1710
	mov	r0, r4
	bl	allocate_new_data_superblock
.L1710:
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
	bcc	.L1714
	movw	r2, #1071
	ldr	r1, .L1756+12
	ldr	r0, .L1756+16
	bl	sftl_printk
.L1714:
	mov	r3, #0
.L1755:
	str	r3, [fp, #-68]
	ldr	r3, [fp, #-68]
	ldr	r2, [fp, #-108]
	cmp	r3, r2
	bne	.L1734
.L1716:
	mov	r3, r4
	mov	r2, #0
	ldr	r1, [fp, #-68]
	ldr	r0, [r10, #3300]
	bl	FtlProgPages
	ldr	r3, [fp, #-68]
	ldr	r2, [fp, #-72]
	cmp	r3, r2
	bls	.L1735
	movw	r2, #1149
	ldr	r1, .L1756+12
	ldr	r0, .L1756+16
	bl	sftl_printk
.L1735:
	ldr	r3, [fp, #-72]
	ldr	r2, [fp, #-68]
	sub	r3, r3, r2
	str	r3, [fp, #-72]
	b	.L1708
.L1711:
	str	r2, [r5, #8]
	ldrh	r2, [r10, #32]
	cmp	r2, #0
	movne	r4, r3
	bne	.L1710
	mov	r0, r4
	bl	allocate_new_data_superblock
	b	.L1713
.L1734:
	ldrh	r3, [r4, #4]
	cmp	r3, #0
	beq	.L1716
	mov	r2, #0
	sub	r1, fp, #64
	mov	r0, r6
	mov	r7, #20
	bl	log2phys
	mov	r0, r4
	bl	get_new_active_ppa
	ldr	r2, .L1756+24
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
	ldr	r3, .L1756+28
	bne	.L1717
	ldr	r2, [fp, #-76]
	cmp	r6, r2
	bne	.L1718
	ldrh	r5, [r3]
	mov	r0, r9
	mov	r1, r5
	bl	__umodsi3
	ldr	r3, [fp, #-80]
	sub	r5, r5, r0
	str	r0, [fp, #-88]
	cmp	r5, r3
	movcs	r5, r3
.L1719:
	ldr	r3, .L1756+28
	ldrh	r3, [r3]
	cmp	r5, r3
	ldr	r3, [fp, #-76]
	bne	.L1720
	cmp	r6, r3
	ldr	r3, [r10, #3300]
	add	r7, r3, r7
	ldreq	r3, [fp, #-84]
	beq	.L1752
	mul	r5, r5, r6
	ldr	r3, [fp, #-84]
	sub	r5, r5, r9
	add	r5, r3, r5, lsl #9
	str	r5, [r7, #8]
.L1722:
	ldrb	r2, [r4, #6]	@ zero_extendqisi2
	ldrh	r3, [r10, #236]
	cmp	r2, r3
	bcc	.L1731
	movw	r2, #1140
	ldr	r1, .L1756+12
	ldr	r0, .L1756+16
	bl	sftl_printk
.L1731:
	ldr	r3, .L1756+32
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
	b	.L1755
.L1718:
	ldr	r2, [fp, #-80]
	add	r5, r9, r2
	ldrh	r2, [r3]
	mov	r3, #0
	str	r3, [fp, #-88]
	smulbb	r2, r2, r6
	sub	r5, r5, r2
	uxth	r5, r5
	b	.L1719
.L1720:
	cmp	r6, r3
	ldr	r3, [r10, #3300]
	ldreq	r2, [r10, #3316]
	ldrne	r2, [r10, #3320]
	add	r3, r3, r7
	str	r2, [r3, #8]
	ldr	r3, [fp, #-64]
	cmn	r3, #1
	beq	.L1725
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
	bne	.L1726
	ldr	r2, [r10, #2716]
	ldr	r0, .L1756+36
	add	r2, r2, #1
	str	r2, [r10, #2716]
	mov	r2, r6
	ldr	r1, [r8, #8]
	bl	sftl_printk
.L1729:
	ldr	r3, [fp, #-76]
	lsl	r2, r5, #9
	cmp	r6, r3
	bne	.L1730
	ldr	r3, [r10, #3300]
	ldr	r1, [fp, #-84]
	add	r7, r3, r7
	ldr	r3, [fp, #-88]
	ldr	r0, [r7, #8]
	add	r0, r0, r3, lsl #9
.L1753:
	bl	ftl_memcpy
	b	.L1722
.L1726:
	ldr	r3, [r8, #8]
	cmp	r6, r3
	beq	.L1728
	ldr	r3, [r10, #2716]
	mov	r2, r6
	ldr	r0, .L1756+40
	add	r3, r3, #1
	str	r3, [r10, #2716]
	ldr	r1, [r8, #8]
	bl	sftl_printk
.L1728:
	ldr	r3, [r8, #8]
	cmp	r6, r3
	beq	.L1729
	movw	r2, #1125
	ldr	r1, .L1756+12
	ldr	r0, .L1756+16
	bl	sftl_printk
	b	.L1729
.L1725:
	ldr	r3, [r10, #3300]
	mov	r1, #0
	ldr	r2, .L1756+44
	add	r3, r3, r7
	ldrh	r2, [r2]
	ldr	r0, [r3, #8]
	bl	ftl_memset
	b	.L1729
.L1730:
	ldr	r3, .L1756+28
	ldrh	r1, [r3]
	ldr	r3, [r10, #3300]
	mul	r1, r6, r1
	add	r7, r3, r7
	ldr	r3, [fp, #-84]
	ldr	r0, [r7, #8]
	sub	r1, r1, r9
	add	r1, r3, r1, lsl #9
	b	.L1753
.L1717:
	ldrh	r3, [r3]
	ldr	r2, [r10, #3300]
	mul	r3, r6, r3
	add	r7, r2, r7
	ldr	r2, [fp, #-84]
	sub	r3, r3, r9
	add	r3, r2, r3, lsl #9
.L1752:
	str	r3, [r7, #8]
	b	.L1722
.L1740:
	ldrh	r3, [r10, #176]
	movw	r2, #65535
	cmp	r3, r2
	bne	.L1738
	ldrh	r2, [r10, #226]
	cmp	r2, r3
	bne	.L1738
	mov	r0, #0
	bl	List_get_gc_head_node
	uxth	r0, r0
	bl	FtlGcRefreshBlock
.L1738:
	ldr	r2, .L1756+48
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
	bhi	.L1739
	subs	r4, r4, #1
	bne	.L1740
	b	.L1739
.L1757:
	.align	2
.L1756:
	.word	.LANCHOR0
	.word	.LANCHOR2
	.word	.LANCHOR0+3158
	.word	.LANCHOR1+591
	.word	.LC8
	.word	.LANCHOR0+28
	.word	.LANCHOR0+316
	.word	.LANCHOR0+262
	.word	-3947
	.word	.LC127
	.word	.LC128
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
	ldr	r4, .L1790
	movw	r7, #2628
	ldr	r6, .L1790+4
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
	bne	.L1761
.L1772:
	mvn	r0, #0
.L1760:
	sub	sp, fp, #40
	ldmfd	sp, {r4, r5, r6, r7, r8, r9, r10, fp, sp, pc}
.L1761:
	mov	r1, #1
	ldr	r9, .L1790+8
	bl	FtlGetLastWrittenPage
	ldrsh	r7, [r4, r7]
	add	r8, r4, #3456
	sxth	r5, r0
	add	r0, r0, #1
	strh	r0, [r6, #2]	@ movhi
.L1763:
	cmp	r5, #0
	bge	.L1769
	movw	r2, #1465
	ldr	r1, .L1790+12
	ldr	r0, .L1790+16
	bl	sftl_printk
.L1768:
	movw	r2, #314
	ldrh	r3, [r4, #244]
	ldrh	r2, [r4, r2]
	add	r3, r3, #24
	cmp	r2, r3, lsl #1
	bcs	.L1771
	movw	r2, #1467
	ldr	r1, .L1790+12
	ldr	r0, .L1790+16
	bl	sftl_printk
.L1771:
	ldr	r5, .L1790+20
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
	ldr	r3, .L1790+8
	cmp	r2, r3
	bne	.L1772
	movw	r3, #258
	ldrb	r2, [r4, #2482]	@ zero_extendqisi2
	ldrh	r3, [r4, r3]
	ldrh	r7, [r5, #8]
	cmp	r2, r3
	strh	r7, [r6, #6]	@ movhi
	bne	.L1772
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
	ldr	r3, .L1790+24
	ldrh	r0, [r3, #6]
	sub	r0, r6, r0
	sub	r0, r0, r7
	bl	__udivsi3
	cmp	r7, r6
	strh	r0, [r5, #152]	@ movhi
	bls	.L1773
	movw	r2, #1489
	ldr	r1, .L1790+12
	ldr	r0, .L1790+16
	bl	sftl_printk
.L1773:
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
	beq	.L1776
	ldr	r0, .L1790+28
	bl	make_superblock
.L1776:
	ldrh	r2, [r4, #80]
	movw	r3, #65535
	cmp	r2, r3
	beq	.L1777
	ldr	r0, .L1790+32
	bl	make_superblock
.L1777:
	ldrh	r2, [r4, #128]
	movw	r3, #65535
	cmp	r2, r3
	beq	.L1778
	ldr	r0, .L1790+36
	bl	make_superblock
.L1778:
	ldrh	r2, [r4, #176]
	movw	r3, #65535
	cmp	r2, r3
	beq	.L1779
	ldr	r0, .L1790+40
	bl	make_superblock
.L1779:
	mov	r0, #0
	b	.L1760
.L1769:
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
	beq	.L1764
	ldr	r2, [r4, #3456]
	cmn	r2, #1
	beq	.L1764
	ldr	r10, .L1790+44
	ldr	r0, [r4, #3464]
	ldrh	r1, [r10]
	bl	js_hash
	ldr	r3, [fp, #-44]
	cmp	r3, r0
	beq	.L1764
	str	r0, [sp, #8]
	mov	r2, r7
	str	r3, [sp, #4]
	ldrh	r3, [r6, #4]
	ldr	r1, .L1790+12
	ldr	r0, .L1790+48
	str	r3, [sp]
	mov	r3, r5
	bl	sftl_printk
	cmp	r5, #0
	bne	.L1765
	ldrh	r3, [r6, #4]
	cmp	r7, r3
	sxthne	r7, r3
	ldrhne	r5, [r10, #-6]
	bne	.L1767
.L1765:
	mvn	r3, #0
	str	r3, [r4, #3456]
.L1764:
	ldr	r3, [r4, #3456]
	cmn	r3, #1
	beq	.L1767
	ldr	r3, [r4, #3304]
	ldr	r3, [r3]
	cmp	r3, r9
	bne	.L1767
	ldr	r3, [r4, #3336]
	ldrh	r2, [r3]
	movw	r3, #61604
	cmp	r2, r3
	beq	.L1768
.L1767:
	sub	r5, r5, #1
	sxth	r5, r5
	b	.L1763
.L1791:
	.align	2
.L1790:
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
	.word	.LC129
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
	ldr	r9, .L1833
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
.L1793:
	ldr	r3, [fp, #-52]
	sxth	r6, r7
	cmp	r6, r3
	bge	.L1812
	ldr	r3, [fp, #-52]
	sub	r3, r3, #1
	cmp	r6, r3
	lsl	r3, r6, #1
	bne	.L1794
	add	r2, r8, r3
	ldrh	r0, [r8, r3]
	mov	r1, #1
	str	r2, [fp, #-52]
	bl	FtlGetLastWrittenPage
	sxth	r3, r0
	ldr	r9, .L1833
	add	r0, r0, #1
	strh	r7, [r4]	@ movhi
	str	r3, [fp, #-60]
	mov	r7, #0
	ldr	r3, [fp, #-56]
	add	r10, r9, #3456
	strh	r0, [r4, #2]	@ movhi
	ldr	r3, [r3, r6, lsl #2]
	str	r3, [r4, #28]
.L1795:
	ldr	r3, [fp, #-60]
	sxth	r8, r7
	add	r2, r3, #1
	cmp	r8, r2
	blt	.L1798
.L1812:
	mov	r0, r4
	bl	ftl_free_no_use_map_blk
	ldr	r3, .L1833+4
	ldrh	r2, [r4, #2]
	ldrh	r3, [r3]
	cmp	r2, r3
	bne	.L1800
	mov	r0, r4
	bl	ftl_map_blk_alloc_new_blk
.L1800:
	mov	r0, r4
	bl	ftl_map_blk_gc
	mov	r0, r4
	bl	ftl_map_blk_gc
	mov	r0, #0
	sub	sp, fp, #40
	ldmfd	sp, {r4, r5, r6, r7, r8, r9, r10, fp, sp, pc}
.L1798:
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
	beq	.L1796
	ldr	r1, [r9, #3456]
	cmn	r1, #1
	beq	.L1796
	ldr	r1, .L1833+8
	ldr	r0, [r9, #3464]
	ldrh	r1, [r1]
	bl	js_hash
	ldr	r2, [fp, #-56]
	cmp	r2, r0
	beq	.L1796
	str	r0, [sp, #4]
	mov	r3, r8
	str	r2, [sp]
	mov	r2, r6
	ldr	r1, .L1833+12
	ldr	r0, .L1833+16
	bl	sftl_printk
	mvn	r3, #0
	str	r3, [r9, #3456]
.L1796:
	ldr	r3, [r9, #3456]
	cmn	r3, #1
	beq	.L1797
	ldrh	r3, [r5, #8]
	ldr	r2, [fp, #-48]
	cmp	r2, r3
	bls	.L1797
	ldrh	r1, [r5]
	ldrh	r2, [r4, #4]
	cmp	r1, r2
	ldreq	r2, [r9, #3460]
	ldreq	r1, [fp, #-44]
	streq	r2, [r1, r3, lsl #2]
.L1797:
	add	r7, r7, #1
	b	.L1795
.L1794:
	ldr	r2, [r9, #3304]
	ldr	r0, .L1833+20
	str	r2, [r9, #3464]
	add	r2, r8, r3
	str	r2, [fp, #-60]
	ldrh	r2, [r8, r3]
	ldr	r3, .L1833+4
	ldrh	r3, [r3]
	sub	r3, r3, #1
	orr	r3, r3, r2, lsl #10
	mov	r2, #1
	mov	r1, r2
	str	r3, [r9, #3460]
	bl	FlashReadPages
	ldr	r3, [r9, #3456]
	cmn	r3, #1
	beq	.L1814
	ldrh	r2, [r5]
	ldrh	r3, [r4, #4]
	cmp	r2, r3
	bne	.L1814
	ldrh	r2, [r5, #8]
	movw	r3, #64245
	cmp	r2, r3
	beq	.L1802
.L1814:
	mov	r10, #0
.L1803:
	ldr	r2, .L1833+4
	sxth	r3, r10
	ldrh	r2, [r2]
	cmp	r3, r2
	bge	.L1810
	ldr	r2, [fp, #-60]
	ldr	r0, .L1833+20
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
	beq	.L1807
	ldr	r1, [r9, #3456]
	cmn	r1, #1
	beq	.L1807
	ldr	r1, .L1833+8
	ldr	r0, [r9, #3464]
	ldrh	r1, [r1]
	bl	js_hash
	ldr	r2, [fp, #-64]
	cmp	r2, r0
	beq	.L1807
	str	r0, [sp, #4]
	str	r2, [sp]
	mov	r2, r6
	ldr	r3, [fp, #-68]
	ldr	r1, .L1833+12
	ldr	r0, .L1833+24
	bl	sftl_printk
	mvn	r3, #0
	str	r3, [r9, #3456]
.L1807:
	ldr	r3, [r9, #3456]
	cmn	r3, #1
	beq	.L1808
	ldrh	r3, [r5, #8]
	ldr	r2, [fp, #-48]
	cmp	r2, r3
	bls	.L1808
	ldrh	r1, [r5]
	ldrh	r2, [r4, #4]
	cmp	r1, r2
	ldreq	r2, [r9, #3460]
	ldreq	r1, [fp, #-44]
	streq	r2, [r1, r3, lsl #2]
.L1808:
	add	r10, r10, #1
	b	.L1803
.L1802:
	ldr	ip, .L1833+4
	mov	r1, #0
	mov	lr, #4
.L1804:
	ldrh	r2, [ip]
	sxth	r3, r1
	sub	r2, r2, #1
	cmp	r3, r2
	blt	.L1806
.L1810:
	add	r7, r7, #1
	b	.L1793
.L1806:
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
	b	.L1804
.L1834:
	.align	2
.L1833:
	.word	.LANCHOR0
	.word	.LANCHOR0+308
	.word	.LANCHOR0+314
	.word	.LANCHOR1+615
	.word	.LC130
	.word	.LANCHOR0+3456
	.word	.LC131
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
	ldr	r3, .L1836
	add	r2, r3, #324
	add	r0, r3, #3600
	ldrh	r2, [r2]
	add	r0, r0, #12
	strh	r2, [r0, #10]	@ movhi
	ldr	r2, .L1836+4
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
.L1837:
	.align	2
.L1836:
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
	ldr	r0, .L1839
	bl	FtlMapTblRecovery
	mov	r0, #0
	ldmfd	sp, {fp, sp, pc}
.L1840:
	.align	2
.L1839:
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
	ldr	r4, .L1857
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
	bne	.L1842
.L1844:
	mvn	r6, #0
.L1841:
	mov	r0, r6
	ldmfd	sp, {r4, r5, r6, fp, sp, pc}
.L1842:
	bl	FtlLoadSysInfo
	subs	r6, r0, #0
	bne	.L1844
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
.L1845:
	cmp	r3, r1
	bge	.L1850
	mla	ip, r0, r3, r2
	ldr	ip, [ip, #4]
	cmp	ip, #0
	bge	.L1846
.L1850:
	ldr	r5, .L1857+4
	cmp	r3, r1
	ldrh	r2, [r5, #28]
	add	r2, r2, #1
	strh	r2, [r5, #28]	@ movhi
	bge	.L1856
.L1847:
	ldr	r0, .L1857+8
	bl	FtlSuperblockPowerLostFix
	ldr	r0, .L1857+12
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
	b	.L1851
.L1846:
	add	r3, r3, #1
	b	.L1845
.L1856:
	movw	r3, #3476
	ldrh	r3, [r4, r3]
	cmp	r3, #0
	bne	.L1847
.L1851:
	ldrh	r0, [r4, #28]
	movw	r3, #65535
	cmp	r0, r3
	beq	.L1852
	ldrh	r3, [r4, #32]
	cmp	r3, #0
	bne	.L1852
	ldrh	r3, [r4, #84]
	cmp	r3, #0
	bne	.L1852
	bl	FtlGcRefreshBlock
	ldrh	r0, [r4, #80]
	bl	FtlGcRefreshBlock
	bl	FtlVpcTblFlush
	ldr	r0, .L1857+8
	bl	allocate_new_data_superblock
	ldr	r0, .L1857+12
	bl	allocate_new_data_superblock
.L1852:
	bl	FtlVpcCheckAndModify
	b	.L1841
.L1858:
	.align	2
.L1857:
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
	ldr	r4, .L1883
	mov	r3, #0
	ldrh	r0, [r4, #240]
	str	r3, [r4, #2592]
	str	r3, [r4, #2596]
	str	r3, [r4, #2608]
	bl	FtlFreeSysBlkQueueInit
	bl	FtlLoadBbt
	cmp	r0, #0
	beq	.L1860
	bl	FtlMakeBbt
.L1860:
	ldr	r0, .L1883+4
	mov	r2, #0
	ldr	ip, .L1883+8
.L1861:
	ldrh	r1, [r0]
	uxth	r3, r2
	add	r2, r2, #1
	cmp	r3, r1, lsl #7
	blt	.L1862
	ldrh	r6, [r4, #244]
	mov	r5, #0
.L1863:
	ldrh	r3, [r4, #246]
	cmp	r3, r6
	bhi	.L1864
	ldrh	r1, [r4, #236]
	sub	r3, r5, #3
	cmp	r3, r1, lsl #1
	bge	.L1865
.L1869:
	mov	r0, #0
	mov	r7, r0
.L1866:
	ldrh	r2, [r4, #244]
	uxth	r3, r0
	add	r5, r0, #1
	cmp	r2, r3
	bhi	.L1870
	ldrh	r3, [r4, #246]
	ldrh	r5, [r4, #236]
	ldr	r10, [r4, #248]
	str	r3, [r4, #3288]
	mov	r1, r5
	ldr	r6, .L1883+12
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
	ble	.L1871
	mov	r1, r5
	sub	r0, r10, r7
	bl	__udivsi3
	str	r0, [r4, #2556]
	lsr	r0, r0, #5
	add	r0, r0, #24
	strh	r0, [r6]	@ movhi
.L1871:
	movw	r3, #298
	ldrh	r3, [r4, r3]
	cmp	r3, #0
	beq	.L1873
	ldrh	r2, [r6]
	add	r2, r2, r3, lsr #1
	strh	r2, [r6]	@ movhi
	mul	r2, r5, r3
	cmp	r7, r2
	addlt	r3, r3, #32
	strlt	r9, [r4, #2556]
	addlt	r3, r8, r3
	strhlt	r3, [r6]	@ movhi
.L1873:
	ldrh	r2, [r6]
	ldr	r3, [r4, #2556]
	ldr	r6, .L1883+16
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
.L1875:
	mov	r0, r6
	bl	make_superblock
	ldrb	r3, [r4, #35]	@ zero_extendqisi2
	cmp	r3, #0
	ldrh	r3, [r4, #28]
	bne	.L1876
	ldr	r2, [r4, #76]
	lsl	r3, r3, #1
	strh	r5, [r2, r3]	@ movhi
	ldrh	r3, [r4, #28]
	add	r3, r3, #1
	strh	r3, [r4, #28]	@ movhi
	b	.L1875
.L1862:
	ldr	lr, [r4, #3316]
	mvn	r1, r3
	orr	r1, r3, r1, lsl #16
	str	r1, [lr, r3, lsl #2]
	ldr	r1, [r4, #3320]
	str	ip, [r1, r3, lsl #2]
	b	.L1861
.L1864:
	mov	r0, r6
	mov	r1, #1
	bl	FtlLowFormatEraseBlock
	add	r6, r6, #1
	add	r5, r5, r0
	uxth	r5, r5
	uxth	r6, r6
	b	.L1863
.L1865:
	mov	r0, r5
	bl	__udivsi3
	ldr	r3, [r4, #336]
	add	r0, r0, r3
	uxth	r0, r0
	bl	FtlSysBlkNumInit
	ldrh	r0, [r4, #240]
	bl	FtlFreeSysBlkQueueInit
	ldrh	r5, [r4, #244]
.L1867:
	ldrh	r3, [r4, #246]
	cmp	r3, r5
	bls	.L1869
	mov	r0, r5
	mov	r1, #1
	add	r5, r5, #1
	bl	FtlLowFormatEraseBlock
	uxth	r5, r5
	b	.L1867
.L1870:
	mov	r1, #0
	uxth	r0, r0
	bl	FtlLowFormatEraseBlock
	add	r7, r7, r0
	mov	r0, r5
	uxth	r7, r7
	b	.L1866
.L1876:
	ldr	r2, [r4, #2592]
	lsl	r3, r3, #1
	ldrh	r1, [r4, #32]
	mvn	r5, #0
	ldr	r6, .L1883+20
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
.L1877:
	mov	r0, r6
	bl	make_superblock
	ldrb	r3, [r4, #87]	@ zero_extendqisi2
	cmp	r3, #0
	ldrh	r3, [r4, #80]
	bne	.L1878
	ldr	r2, [r4, #76]
	lsl	r3, r3, #1
	strh	r5, [r2, r3]	@ movhi
	ldrh	r3, [r4, #80]
	add	r3, r3, #1
	strh	r3, [r4, #80]	@ movhi
	b	.L1877
.L1878:
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
	ldr	r3, .L1883+24
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
	ldreq	r3, .L1883+28
	moveq	r2, #1
	streq	r2, [r3]
	ldmfd	sp, {r4, r5, r6, r7, r8, r9, r10, fp, sp, pc}
.L1884:
	.align	2
.L1883:
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
	ldr	r4, .L1891
	mvn	r3, #0
	ldr	r5, .L1891+4
	ldr	r1, .L1891+8
	ldr	r0, .L1891+12
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
	bne	.L1889
	bl	FtlSysBlkInit
	cmp	r0, #0
	bne	.L1889
	mov	r3, #1
	str	r3, [r5]
	ldrh	r3, [r4, #228]
	cmp	r3, #15
	bhi	.L1889
	movw	r4, #8129
.L1888:
	mov	r1, #1
	mov	r0, #0
	bl	rk_ftl_garbage_collect
	subs	r4, r4, #1
	bne	.L1888
.L1889:
	mov	r0, #0
	ldmfd	sp, {r4, r5, fp, sp, pc}
.L1892:
	.align	2
.L1891:
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
	ldr	r6, .L1955
	beq	.L1894
	ldr	r3, [r6, #3680]
	cmp	r3, #0
	bne	.L1895
.L1954:
	mov	r4, #0
	b	.L1893
.L1895:
	ldr	r3, [r6, #3684]
	ldr	r2, [r3]
	str	r3, [fp, #-112]
	ldr	r3, .L1955+4
	cmp	r2, r3
	bne	.L1897
	ldr	r3, [fp, #-112]
	mov	r2, #0
	ldrh	r1, [r6, #10]
	add	r0, r3, #262144
	movw	r3, #65535
.L1901:
	ldr	ip, [r0, #-4]
	cmp	ip, #0
	bne	.L1898
	ldr	ip, [fp, #-112]
	sub	r3, r3, #1
	ldr	ip, [ip, r2, lsl #2]
	add	r2, r2, #1
	cmp	r2, #4096
	movhi	r2, #0
	cmp	r3, #4096
	str	ip, [r0, #-4]!
	bne	.L1901
	mov	r3, #512
	b	.L1952
.L1898:
	add	r3, r3, #127
	lsr	r3, r3, #7
.L1952:
	str	r3, [fp, #-116]
	lsl	r1, r1, #2
	ldr	r3, [fp, #-116]
	uxth	r1, r1
	ldr	r4, .L1955
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
.L1923:
	mov	r1, #512
	ldr	r0, [r4, #3688]
	bl	__memzero
	ldr	r3, [fp, #-108]
	ldrh	r7, [r4, #10]
	mul	r8, r3, r7
	ldr	r3, [r4, #3264]
	cmp	r3, #0
	moveq	r10, #6
	beq	.L1902
	ldr	r3, [r4, #3268]
	cmp	r3, #0
	moveq	r10, #6
	movne	r10, #9
.L1902:
	ldr	r3, [r6, #3252]
	mov	r1, r8
	mov	r0, #0
	blx	r3
	ldr	r3, [fp, #-120]
	cmp	r3, #1
	beq	.L1903
	ldr	r3, [r6, #3252]
	add	r1, r7, r8
	mov	r0, #0
	blx	r3
.L1903:
	cmp	r10, #9
	movne	r5, #0
	bne	.L1904
	ldr	r5, [r6, #3688]
	mov	r1, #1024
	mov	r0, r5
	bl	__memzero
	ldr	r3, .L1955+8
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
.L1904:
	ldr	r3, [fp, #-120]
	mov	r9, r8
	mul	r3, r7, r3
	mov	r7, #0
	str	r3, [fp, #-136]
	ldr	r3, [fp, #-112]
	str	r3, [fp, #-128]
.L1905:
	ldr	r3, [fp, #-136]
	cmp	r7, r3
	beq	.L1912
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
	bne	.L1908
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
	bne	.L1909
.L1912:
	ldrb	r3, [r4, #14]	@ zero_extendqisi2
	ldrh	r10, [r4, #10]
	ldr	r7, [r4, #3688]
	str	r3, [fp, #-128]
	ldr	r3, [fp, #-108]
	mul	r9, r3, r10
	ldr	r3, [r4, #3264]
	cmp	r3, #0
	moveq	r8, #6
	beq	.L1911
	ldr	r3, [r4, #3268]
	cmp	r3, #0
	moveq	r8, #6
	movne	r8, #9
.L1911:
	ldr	r3, [fp, #-120]
	mov	r5, #0
	mul	r10, r10, r3
.L1914:
	cmp	r5, r10
	beq	.L1918
	cmp	r5, #0
	cmpeq	r8, #9
	moveq	r0, #1
	movne	r0, #0
	bne	.L1915
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
	ldr	r2, .L1955+8
	cmp	r3, r2
	beq	.L1916
.L1918:
	ldr	r0, [r4, #3688]
	mov	r5, #0
	ldr	r3, [fp, #-112]
.L1917:
	mov	r8, r3
	ldr	r1, [r0, r5, lsl #2]
	ldr	r2, [r8]
	lsl	r7, r5, #2
	add	r3, r3, #4
	cmp	r1, r2
	beq	.L1920
	mov	r1, #512
	bl	__memzero
	ldr	r2, [r6, #3688]
	ldr	r1, [fp, #-108]
	str	r5, [sp]
	ldr	r0, .L1955+12
	ldr	r3, [r8]
	ldr	r2, [r2, r7]
	bl	sftl_printk
	ldrh	r1, [r6, #10]
	mov	r0, #0
	ldr	r2, [fp, #-108]
	ldr	r3, [r6, #3252]
	mul	r1, r2, r1
	blx	r3
.L1921:
	ldr	r3, [fp, #-108]
	ldr	r2, [fp, #-120]
	add	r3, r3, r2
	cmp	r3, #15
	str	r3, [fp, #-108]
	bls	.L1923
	ldr	r3, [fp, #-124]
	cmp	r3, #0
	bne	.L1924
.L1897:
	mvn	r3, #0
	str	r3, [fp, #-116]
.L1924:
	ldr	r5, .L1955
	mov	r3, #0
	ldr	r4, [fp, #-116]
	ldr	r0, [r5, #3684]
	str	r3, [r5, #3680]
	bl	kfree
	ldr	r0, [r5, #3688]
	bl	kfree
.L1893:
	mov	r0, r4
	sub	sp, fp, #40
	ldmfd	sp, {r4, r5, r6, r7, r8, r9, r10, fp, sp, pc}
.L1908:
	ldr	ip, [r4, #3256]
	sub	r3, fp, #104
	ldr	r2, [fp, #-128]
	mov	r1, r9
	blx	ip
	cmn	r0, #1
	beq	.L1912
	ldr	r3, [fp, #-128]
	add	r3, r3, #2048
	str	r3, [fp, #-128]
.L1909:
	add	r7, r7, #1
	add	r9, r9, #1
	b	.L1905
.L1915:
	ldr	ip, [r4, #3260]
	sub	r3, fp, #104
	mov	r2, r7
	add	r1, r9, r5
	blx	ip
	cmn	r0, #1
	beq	.L1918
	ldr	r2, [fp, #-100]
	movw	r3, #61424
	cmp	r2, r3
	bne	.L1918
	add	r7, r7, #2048
.L1916:
	add	r5, r5, #1
	b	.L1914
.L1920:
	ldr	r2, [fp, #-132]
	add	r5, r5, #1
	cmp	r5, r2
	bne	.L1917
	ldr	r3, [fp, #-124]
	add	r3, r3, #1
	cmp	r3, #5
	str	r3, [fp, #-124]
	bls	.L1921
	b	.L1924
.L1894:
	cmp	r0, #64
	bne	.L1925
	mov	r0, #262144
	bl	ftl_malloc
	str	r0, [r6, #3684]
	mov	r0, #262144
	bl	ftl_malloc
	ldr	r3, [r6, #3684]
	str	r0, [r6, #3688]
	cmp	r3, #0
	cmpne	r0, #0
	beq	.L1926
	mov	r2, #1
	mov	r1, #262144
	mov	r0, r3
	str	r2, [r6, #3680]
	bl	__memzero
.L1925:
	ldr	r3, [r6, #3680]
	cmp	r3, #0
	beq	.L1954
	cmp	r5, #63
	ldr	r1, [r6, #3684]
	ldrhi	r6, .L1955+16
	rsbls	r5, r5, #64
	subls	r4, r4, r5
	addls	r7, r7, r5, lsl #9
	movls	r6, r1
	ldr	r0, .L1955+20
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
	b	.L1954
.L1926:
	ldr	r1, .L1955+24
	ldr	r0, .L1955+28
	bl	sftl_printk
	b	.L1925
.L1956:
	.align	2
.L1955:
	.word	.LANCHOR0
	.word	-52655045
	.word	1179535694
	.word	.LC132
	.word	8388544
	.word	.LC134
	.word	.LANCHOR1+633
	.word	.LC133
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
	ldr	r2, .L1961
	ldr	r3, [r2, #3692]
	cmp	r3, #0
	streq	r0, [r2, #3692]
	moveq	r0, r3
	streq	r1, [r2, #3696]
	mvnne	r0, #0
	ldmfd	sp, {fp, sp, pc}
.L1962:
	.align	2
.L1961:
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
	ldr	r4, .L1972
	bl	ftl_malloc
	cmp	r0, #0
	str	r0, [r4, #3700]
	mvneq	r9, #11
	beq	.L1963
	ldr	r8, .L1972+4
	mov	r6, #0
	mov	r5, r6
	mov	r7, r6
.L1967:
	ldr	r3, [r4, #3692]
	mov	r1, #128
	ldr	r2, [r4, #3700]
	lsl	r0, r7, #7
	blx	r3
	subs	r9, r0, #0
	bne	.L1965
	ldr	r2, [r4, #3700]
	ldr	r3, [r2]
	cmp	r3, r8
	bne	.L1966
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
.L1966:
	add	r7, r7, #1
	cmp	r7, #2
	bne	.L1967
	cmp	r5, #0
	beq	.L1968
	ldr	r3, [r4, #3692]
	mov	r1, #128
	lsl	r0, r6, #7
	blx	r3
	subs	r9, r0, #0
	beq	.L1963
.L1965:
	ldr	r0, [r4, #3700]
	mvn	r9, #0
	bl	kfree
	mov	r3, #0
	str	r3, [r4, #3700]
	b	.L1963
.L1968:
	mov	r1, #65536
	mov	r0, r2
	bl	__memzero
	ldr	r3, [r4, #3700]
	mov	r2, #1
	add	r1, r3, #61440
	str	r2, [r3, #4]
	str	r8, [r3]
	str	r2, [r1, #4092]
	ldr	r2, .L1972+8
	strh	r5, [r3, #12]	@ movhi
	strh	r2, [r3, #14]	@ movhi
.L1963:
	mov	r0, r9
	ldmfd	sp, {r4, r5, r6, r7, r8, r9, fp, sp, pc}
.L1973:
	.align	2
.L1972:
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
	ldr	r3, .L1980
	mov	r4, r0
	mov	r0, r1
	ldr	ip, [r3, #3700]
	cmp	ip, #0
	ldrhne	lr, [ip, #10]
	movne	r3, #0
	bne	.L1976
.L1979:
	mvn	r0, #0
	ldmfd	sp, {r4, r5, fp, sp, pc}
.L1977:
	add	r3, r3, #1
.L1976:
	cmp	r3, lr
	bcs	.L1979
	add	r1, ip, r3, lsl #3
	ldrh	r5, [r1, #16]
	cmp	r5, r4
	bne	.L1977
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
.L1981:
	.align	2
.L1980:
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
	ldr	r8, .L2001
	mov	r9, r2
	ldr	r4, [r8, #3700]
	cmp	r4, #0
	beq	.L1997
	ldrh	r2, [r4, #10]
	add	r7, r9, #63
	ldrh	r3, [r4, #8]
	bic	r7, r7, #63
	mov	r5, #0
	str	r3, [fp, #-44]
.L1984:
	cmp	r5, r2
	bcc	.L1992
	ldrh	r1, [r4, #14]
	cmp	r7, r1
	bhi	.L1997
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
	b	.L2000
.L1992:
	add	r6, r4, r5, lsl #3
	ldrh	r3, [r6, #16]
	cmp	r3, r0
	str	r3, [fp, #-48]
	bne	.L1985
	ldrh	r1, [r6, #20]
	add	r3, r4, #1024
	add	r1, r1, #63
	bic	r1, r1, #63
	cmp	r9, r1
	str	r1, [fp, #-52]
	bls	.L1986
	ldrh	r1, [r4, #14]
	cmp	r7, r1
	bhi	.L1997
	ldrh	r8, [r6, #18]
	sub	r2, r2, #1
	str	r2, [fp, #-56]
.L1987:
	ldr	r2, [fp, #-56]
	add	r6, r6, #8
	cmp	r5, r2
	bcc	.L1988
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
.L1989:
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
	ldr	r3, .L2001
	ldr	r3, [r3, #3696]
.L2000:
	ldr	r0, [fp, #-44]
	mov	r2, r4
	mov	r1, #128
	lsl	r0, r0, #7
	blx	r3
	mov	r0, #0
.L1982:
	sub	sp, fp, #40
	ldmfd	sp, {r4, r5, r6, r7, r8, r9, r10, fp, sp, pc}
.L1988:
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
	b	.L1987
.L1986:
	add	r5, r4, r5, lsl #3
	mov	r2, r9
	mov	r1, ip
	ldrh	r0, [r5, #18]
	add	r0, r3, r0
	bl	memcpy
	strh	r9, [r6, #20]	@ movhi
	b	.L1989
.L1985:
	add	r5, r5, #1
	b	.L1984
.L1997:
	mvn	r0, #0
	b	.L1982
.L2002:
	.align	2
.L2001:
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
	beq	.L2003
	ldr	r3, .L2031
	cmp	r6, r3
	beq	.L2006
	add	r3, r3, #1
	cmp	r6, r3
	beq	.L2007
.L2026:
	mvn	r4, #13
	b	.L2005
.L2006:
	ldr	r6, .L2031+4
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
	bne	.L2008
	mov	r2, #8
	mov	r1, r4
	bl	arm_copy_from_user
	cmp	r0, #0
	beq	.L2027
.L2009:
	mov	r2, r4
	mov	r1, #256
.L2030:
	ldr	r0, .L2031+8
	bl	sftl_printk
	b	.L2026
.L2008:
	mov	r1, #8
	bl	__memzero
	b	.L2009
.L2027:
	ldr	r2, [r5]
	ldr	r3, .L2031+12
	cmp	r2, r3
	beq	.L2011
.L2012:
	mvn	r4, #0
.L2005:
	mov	r0, r5
	bl	kfree
.L2003:
	mov	r0, r4
	ldmfd	sp, {r4, r5, r6, fp, sp, pc}
.L2011:
	ldrh	r2, [r5, #6]
	add	r1, r5, #8
	ldrh	r0, [r5, #4]
	bl	rk_sftl_vendor_read
	cmn	r0, #1
	beq	.L2012
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
	bne	.L2026
	mov	r0, r4
	mov	r1, r5
	bl	arm_copy_to_user
	subs	r4, r0, #0
	beq	.L2005
	b	.L2026
.L2007:
	ldr	r6, .L2031+4
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
	bne	.L2014
	mov	r2, #8
	mov	r1, r4
	bl	arm_copy_from_user
	cmp	r0, #0
	beq	.L2028
.L2015:
	mov	r2, r4
	mov	r1, #276
	b	.L2030
.L2014:
	mov	r1, #8
	bl	__memzero
	b	.L2015
.L2028:
	ldr	r2, [r5]
	ldr	r3, .L2031+12
	cmp	r2, r3
	bne	.L2012
	ldrh	r2, [r5, #6]
	movw	r3, #4087
	cmp	r2, r3
	bhi	.L2012
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
	bne	.L2017
	mov	r1, r4
	mov	r0, r5
	bl	arm_copy_from_user
	cmp	r0, #0
	beq	.L2029
.L2018:
	mov	r2, r4
	movw	r1, #283
	b	.L2030
.L2017:
	mov	r1, r2
	mov	r0, r5
	bl	__memzero
	b	.L2018
.L2029:
	ldrh	r2, [r5, #6]
	add	r1, r5, #8
	ldrh	r0, [r5, #4]
	bl	rk_sftl_vendor_write
	mov	r4, r0
	b	.L2005
.L2032:
	.align	2
.L2031:
	.word	1074034177
	.word	-8192
	.word	.LC135
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
	ldr	r0, .L2034
	bl	misc_register
	ldmfd	sp, {fp, sp, pc}
.L2035:
	.align	2
.L2034:
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
