/* SPDX-License-Identifier: GPL-2.0 */

/* Copyright (c) 2018 Rockchip Electronics Co. Ltd.
 * date: 2019-03-26
 */
	.file	"rk_sftl.c"
	.syntax divided
	.syntax unified
	.global	__udivsi3
	.arm
	.syntax unified
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
	ldr	r3, .L56
	movw	r2, #302
	movw	r1, #258
	mov	r5, #0
	ldrh	r2, [r3, r2]
	mov	r4, r5
	ldrh	r6, [r3, r1]
	mul	r6, r6, r2
.L50:
	uxth	r0, r5
	cmp	r0, r6
	blt	.L52
	mov	r0, r4
	ldmfd	sp, {r4, r5, r6, fp, sp, pc}
.L52:
	bl	FtlBbmIsBadBlock
	cmp	r0, #0
	add	r5, r5, #1
	addne	r4, r4, #1
	uxthne	r4, r4
	b	.L50
.L57:
	.align	2
.L56:
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
	ldr	r4, .L59
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
.L60:
	.align	2
.L59:
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
	ldr	r3, .L62
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
.L63:
	.align	2
.L62:
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
	ldr	r4, .L65
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
.L66:
	.align	2
.L65:
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
	bls	.L68
	sub	r0, r0, r1
	cmp	r0, #-2147483648
	movhi	r0, #0
	movls	r0, #1
	ldmfd	sp, {fp, sp, pc}
.L68:
	sub	r0, r1, r0
	cmp	r0, #-2147483648
	movls	r0, #0
	movhi	r0, #1
	ldmfd	sp, {fp, sp, pc}
	.size	ftl_cmp_data_ver, .-ftl_cmp_data_ver
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
	ldr	r3, .L71
	ldrh	r0, [r3, #6]
	clz	r0, r0
	lsr	r0, r0, #5
	ldmfd	sp, {fp, sp, pc}
.L72:
	.align	2
.L71:
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
	ldr	r3, .L74
	ldrh	r0, [r3, #6]
	sub	r0, r0, #1024
	clz	r0, r0
	lsr	r0, r0, #5
	ldmfd	sp, {fp, sp, pc}
.L75:
	.align	2
.L74:
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
	ldr	r3, .L85
	ldrh	r2, [r3, #6]
	cmp	r2, #0
	ldmfdeq	sp, {r4, fp, sp, pc}
	ldr	r2, .L85+4
	mov	r4, #0
	ldrh	r1, [r3, #2]
	mov	lr, r4
	ldrh	ip, [r2, #28]
	ldrh	r2, [r3, #4]
	and	ip, ip, #31
.L78:
	uxth	r0, r4
	add	r4, r4, #1
	cmp	ip, r0
	bgt	.L79
	cmp	lr, #0
	strhne	r1, [r3, #2]	@ movhi
	strhne	r2, [r3, #4]	@ movhi
	ldmfd	sp, {r4, fp, sp, pc}
.L79:
	add	r0, r3, r1, lsl #1
	add	r1, r1, #1
	ubfx	r1, r1, #0, #10
	ldrh	lr, [r0, #8]
	add	r0, r3, r2, lsl #1
	strh	lr, [r0, #8]	@ movhi
	mov	lr, #1
	add	r2, r2, lr
	ubfx	r2, r2, #0, #10
	b	.L78
.L86:
	.align	2
.L85:
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
	ldr	r3, .L93
	uxth	r0, r0
	ldrh	ip, [r3, #6]
	cmp	ip, #1024
	ldrhne	lr, [r3, #2]
	movne	r1, #0
	bne	.L89
.L91:
	mov	r0, #0
	ldmfd	sp, {fp, sp, pc}
.L90:
	add	r2, r1, lr
	ubfx	r2, r2, #0, #10
	add	r2, r3, r2, lsl #1
	ldrh	r2, [r2, #8]
	cmp	r2, r0
	beq	.L92
	add	r1, r1, #1
.L89:
	cmp	r1, ip
	bcc	.L90
	b	.L91
.L92:
	mov	r0, #1
	ldmfd	sp, {fp, sp, pc}
.L94:
	.align	2
.L93:
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
	ldr	r1, .L110
	ldrh	r3, [r1, #244]
	mov	lr, r1
	cmp	r3, r0
	bls	.L97
	ldr	r6, [r1, #2520]
	lsl	r5, r0, #3
	mvn	ip, #0
	add	r2, r6, r5
	strh	ip, [r2, #2]	@ movhi
	strh	ip, [r6, r5]	@ movhi
	ldr	r3, [r1, #2524]
	cmp	r3, #0
	bne	.L98
.L109:
	str	r2, [r1, #2524]
	b	.L97
.L98:
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
.L103:
	ldr	r4, [fp, #-44]
	add	lr, lr, #1
	uxth	lr, lr
	cmp	lr, r4
	movls	r4, #0
	movhi	r4, #1
	cmp	r0, r1
	orreq	r4, r4, #1
	cmp	r4, #0
	bne	.L97
	ldrh	r4, [r3, #4]
	cmp	r4, #0
	beq	.L101
	lsl	r10, r1, #1
	ldrh	r10, [r8, r10]
	mul	r4, r4, r10
	cmp	r4, ip
	bcs	.L101
	ldrh	r4, [r3]
	cmp	r4, r9
	bne	.L102
	strh	r1, [r2, #2]	@ movhi
	strh	r0, [r3]	@ movhi
	ldr	r3, .L110
	str	r2, [r3, #2528]
.L97:
	mov	r0, #0
	ldmib	sp, {r4, r5, r6, r7, r8, r9, r10, fp, sp, pc}
.L102:
	add	r3, r7, r4, lsl #3
	mov	r1, r4
	b	.L103
.L101:
	strh	r1, [r6, r5]	@ movhi
	ldrh	r1, [r3, #2]
	strh	r1, [r2, #2]	@ movhi
	ldr	r1, .L110
	ldr	ip, [r1, #2524]
	cmp	r3, ip
	strheq	r0, [r3, #2]	@ movhi
	beq	.L109
.L104:
	ldrh	r2, [r3, #2]
	ldr	r1, [r1, #2520]
	lsl	r2, r2, #3
	strh	r0, [r1, r2]	@ movhi
	strh	r0, [r3, #2]	@ movhi
	b	.L97
.L111:
	.align	2
.L110:
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
	ldr	r2, .L114
	movw	r1, #2532
	ldrh	r3, [r2, r1]
	add	r3, r3, #1
	uxth	r3, r3
	strh	r3, [r2, r1]	@ movhi
	ldrh	r2, [r2, #244]
	cmp	r2, r3
	ldmfdcs	sp, {fp, sp, pc}
	mov	r2, #214
	ldr	r1, .L114+4
	ldr	r0, .L114+8
	bl	sftl_printk
	ldmfd	sp, {fp, sp, pc}
.L115:
	.align	2
.L114:
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
	beq	.L117
	ldr	r2, .L123
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
	beq	.L117
	ldr	r8, [r2, #2540]
	lsl	r2, r0, #1
	ldr	r7, [r1, #2520]
	mov	r4, lr
	ldrh	r9, [r8, r2]
	sub	r2, r3, r7
	ubfx	r2, r2, #3, #16
.L121:
	lsl	lr, r2, #1
	ldrh	lr, [r8, lr]
	cmp	lr, r9
	bcs	.L119
	ldrh	lr, [r3]
	cmp	lr, r4
	bne	.L120
	strh	r2, [ip, #2]	@ movhi
	strh	r0, [r3]	@ movhi
.L117:
	mov	r0, #0
	ldmfd	sp, {r4, r5, r6, r7, r8, r9, fp, sp, pc}
.L120:
	add	r3, r7, lr, lsl #3
	mov	r2, lr
	b	.L121
.L119:
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
	b	.L117
.L124:
	.align	2
.L123:
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
	ldr	r2, .L127
	ldrh	r3, [r2, #228]
	add	r3, r3, #1
	uxth	r3, r3
	strh	r3, [r2, #228]	@ movhi
	ldrh	r2, [r2, #244]
	cmp	r2, r3
	ldmfdcs	sp, {fp, sp, pc}
	mov	r2, #207
	ldr	r1, .L127+4
	ldr	r0, .L127+8
	bl	sftl_printk
	ldmfd	sp, {fp, sp, pc}
.L128:
	.align	2
.L127:
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
	ldr	r4, .L136
	uxth	r1, r1
	movw	r3, #65535
	mov	r8, r0
	ldr	r7, [r4, #2520]
	lsl	r6, r1, #3
	add	r5, r7, r6
	ldrh	r2, [r5, #2]
	cmp	r2, r3
	bne	.L130
	ldr	r3, [r0]
	cmp	r5, r3
	beq	.L130
	mov	r2, #372
	ldr	r1, .L136+4
	ldr	r0, .L136+8
	bl	sftl_printk
.L130:
	ldr	r3, [r8]
	ldrh	r2, [r7, r6]
	cmp	r5, r3
	movw	r3, #65535
	bne	.L131
	cmp	r2, r3
	ldrne	r3, [r4, #2520]
	moveq	r3, #0
	streq	r3, [r8]
	addne	r2, r3, r2, lsl #3
	mvnne	r3, #0
	strne	r2, [r8]
	strhne	r3, [r2, #2]	@ movhi
.L133:
	mvn	r3, #0
	mov	r0, #0
	strh	r3, [r7, r6]	@ movhi
	strh	r3, [r5, #2]	@ movhi
	ldmfd	sp, {r4, r5, r6, r7, r8, fp, sp, pc}
.L131:
	cmp	r2, r3
	ldrh	r3, [r5, #2]
	bne	.L134
	cmp	r3, r2
	ldrne	r2, [r4, #2520]
	lslne	r3, r3, #3
	mvnne	r1, #0
	beq	.L133
.L135:
	strh	r1, [r2, r3]	@ movhi
	b	.L133
.L134:
	ldr	r1, [r4, #2520]
	add	r2, r1, r2, lsl #3
	strh	r3, [r2, #2]	@ movhi
	ldrh	r3, [r5, #2]
	ldrh	r1, [r7, r6]
	ldr	r2, [r4, #2520]
	lsl	r3, r3, #3
	b	.L135
.L137:
	.align	2
.L136:
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
	beq	.L138
	ldr	r2, .L145
	movw	ip, #65535
	ldr	r4, [r2, #2520]
.L140:
	cmp	r1, #0
	bne	.L141
.L143:
	sub	r3, r3, r4
	ubfx	r4, r3, #3, #16
	mov	r1, r4
	bl	List_remove_node
.L138:
	mov	r0, r4
	ldmfd	sp, {r4, fp, sp, pc}
.L141:
	ldrh	r2, [r3]
	cmp	r2, ip
	beq	.L143
	sub	r1, r1, #1
	add	r3, r4, r2, lsl #3
	uxth	r1, r1
	b	.L140
.L146:
	.align	2
.L145:
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
	ldr	r1, .L154
	uxth	r2, r0
	ldr	r3, [r1, #2524]
	cmp	r3, #0
	ldrne	r1, [r1, #2520]
	movwne	r0, #65535
	bne	.L150
	movw	r0, #65535
	ldmfd	sp, {fp, sp, pc}
.L152:
	sub	r2, r2, #1
	add	r3, r1, r3, lsl #3
	uxth	r2, r2
.L150:
	cmp	r2, #0
	beq	.L151
	ldrh	r3, [r3]
	cmp	r3, r0
	bne	.L152
	ldmfd	sp, {fp, sp, pc}
.L151:
	sub	r3, r3, r1
	ubfx	r0, r3, #3, #16
	ldmfd	sp, {fp, sp, pc}
.L155:
	.align	2
.L154:
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
	ldr	r4, .L169
	uxth	r5, r0
	ldrh	r3, [r4, #28]
	cmp	r3, r5
	beq	.L158
	ldrh	r3, [r4, #80]
	cmp	r3, r5
	beq	.L158
	ldrh	r3, [r4, #128]
	cmp	r3, r5
	beq	.L158
	ldr	r8, [r4, #2520]
	ldr	r3, [r4, #2524]
	add	r7, r8, r5, lsl #3
	cmp	r7, r3
	beq	.L158
	ldr	r2, [r4, #76]
	lsl	r3, r5, #1
	ldrh	r6, [r7, #4]
	ldrh	r3, [r2, r3]
	cmp	r6, #0
	mvneq	r6, #0
	mulne	r6, r6, r3
	ldr	r3, [r8, r5, lsl #3]
	cmn	r3, #1
	bne	.L161
	movw	r2, #463
	ldr	r1, .L169+4
	ldr	r0, .L169+8
	bl	sftl_printk
.L161:
	ldr	r3, [r8, r5, lsl #3]
	cmn	r3, #1
	beq	.L158
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
	bcs	.L158
	mov	r1, r5
	ldr	r0, .L169+12
	bl	List_remove_node
	movw	r3, #2532
	ldrh	r3, [r4, r3]
	cmp	r3, #0
	bne	.L163
	movw	r2, #474
	ldr	r1, .L169+4
	ldr	r0, .L169+8
	bl	sftl_printk
.L163:
	movw	r2, #2532
	mov	r0, r5
	ldrh	r3, [r4, r2]
	sub	r3, r3, #1
	strh	r3, [r4, r2]	@ movhi
	bl	INSERT_DATA_LIST
.L158:
	mov	r0, #0
	ldmfd	sp, {r4, r5, r6, r7, r8, fp, sp, pc}
.L170:
	.align	2
.L169:
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
	ldr	r0, .L181
	movw	r3, #342
	mov	r1, #0
	movw	ip, #65535
	ldrh	r2, [r0, r3]
	mov	lr, r0
	ldr	r3, [r0, #2544]
	mov	r0, #12
.L172:
	uxth	r4, r1
	cmp	r4, r2
	bcc	.L174
	mov	r4, r2
	mov	r1, #0
	mov	r0, #-2147483648
	mov	r6, #12
.L175:
	uxth	r5, r1
	cmp	r5, r2
	bcc	.L177
	cmp	r4, r2
	bcc	.L173
	movw	r1, #2548
	mov	r4, r2
	ldrh	r5, [lr, r1]
	mvn	r0, #0
	mov	r1, #0
.L178:
	uxth	ip, r1
	cmp	ip, r2
	bcc	.L180
	cmp	r4, r2
	bcc	.L173
	movw	r2, #789
	ldr	r1, .L181+4
	ldr	r0, .L181+8
	bl	sftl_printk
	b	.L173
.L174:
	add	r1, r1, #1
	mla	r5, r0, r1, r3
	ldrh	r5, [r5, #-12]
	cmp	r5, ip
	bne	.L172
.L173:
	mov	r0, r4
	ldmfd	sp, {r4, r5, r6, r7, fp, sp, pc}
.L177:
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
	b	.L175
.L180:
	ldr	lr, [r3, #4]
	cmp	r0, lr
	bls	.L179
	ldrh	r6, [r3]
	cmp	r6, r5
	movne	r0, lr
	movne	r4, ip
.L179:
	add	r1, r1, #1
	add	r3, r3, #12
	b	.L178
.L182:
	.align	2
.L181:
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
	ldr	ip, .L189
	movw	r1, #2550
	ldrh	r2, [ip, r1]
	mov	r3, ip
	cmp	r2, #4
	cmpls	r0, #0
	bne	.L184
	add	r2, r2, #1
	strh	r2, [ip, r1]	@ movhi
	ldmfd	sp, {fp, sp, pc}
.L184:
	mov	r2, #0
	movw	lr, #65535
	strh	r2, [ip, r1]	@ movhi
	str	r2, [ip, #2552]
	ldrh	r1, [ip, #244]
	ldr	r2, [ip, #76]
	add	r1, r2, r1, lsl #1
.L185:
	cmp	r2, r1
	bne	.L187
	ldmfd	sp, {fp, sp, pc}
.L187:
	ldrh	ip, [r2], #2
	cmp	ip, lr
	ldrne	r0, [r3, #2552]
	addne	r0, r0, ip
	strne	r0, [r3, #2552]
	b	.L185
.L190:
	.align	2
.L189:
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
	beq	.L192
	bl	ftl_set_blk_mode.part.6
	ldmfd	sp, {fp, sp, pc}
.L192:
	ldr	r3, .L194
	lsr	r1, r0, #5
	mov	ip, #1
	and	r0, r0, #31
	ldr	r2, [r3, #24]
	ldr	r3, [r2, r1, lsl #2]
	bic	r0, r3, ip, lsl r0
	str	r0, [r2, r1, lsl #2]
	ldmfd	sp, {fp, sp, pc}
.L195:
	.align	2
.L194:
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
	ldr	r3, .L197
	uxth	r0, r0
	ldr	r3, [r3, #24]
	lsr	r2, r0, #5
	and	r0, r0, #31
	ldr	r3, [r3, r2, lsl #2]
	lsr	r0, r3, r0
	and	r0, r0, #1
	ldmfd	sp, {fp, sp, pc}
.L198:
	.align	2
.L197:
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
	ldr	r3, .L206
	add	ip, r0, r2, lsl #1
	movw	r1, #65535
	add	ip, ip, #14
	ldrh	lr, [r3, #236]
.L200:
	cmp	r2, lr
	bcc	.L202
	movw	r2, #306
	add	ip, r0, #16
	ldrh	r1, [r3, r2]
	movw	r4, #65535
	mov	r3, #0
	sub	r1, r1, #1
	sub	r1, r1, r5
	uxth	r1, r1
.L203:
	uxth	r2, r3
	cmp	lr, r2
	bhi	.L205
	ldmfd	sp, {r4, r5, fp, sp, pc}
.L202:
	ldrh	r4, [ip, #2]!
	add	r2, r2, #1
	uxth	r2, r2
	cmp	r4, r1
	ldrhne	r4, [r0, #4]
	addne	r4, r4, #1
	strhne	r4, [r0, #4]	@ movhi
	b	.L200
.L205:
	ldrh	r2, [ip], #2
	add	r3, r3, #1
	cmp	r2, r4
	ldrhne	r2, [r0, #4]
	addne	r2, r1, r2
	strhne	r2, [r0, #4]	@ movhi
	b	.L203
.L207:
	.align	2
.L206:
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
	ldr	r2, .L214
	ldrh	ip, [r2, #236]
	mov	r2, r3
.L211:
	cmp	r1, r2
	beq	.L213
	ldmfd	sp, {fp, sp, pc}
.L213:
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
	b	.L211
.L215:
	.align	2
.L214:
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
	ldr	r6, .L222
	ldrh	r2, [r0]
	ldrh	r3, [r6, #244]
	cmp	r2, r3
	bcc	.L217
	movw	r2, #2148
	ldr	r1, .L222+4
	ldr	r0, .L222+8
	bl	sftl_printk
.L217:
	ldrh	r9, [r6, #236]
	add	r8, r4, #16
	ldr	r10, .L222+12
	mvn	r7, #0
	mov	r5, #0
	strh	r5, [r4, #4]	@ movhi
	strb	r5, [r4, #7]
.L218:
	uxth	r3, r5
	ldrh	r1, [r4]
	cmp	r9, r3
	bhi	.L220
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
.L220:
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
	b	.L218
.L223:
	.align	2
.L222:
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
	ldr	r3, .L230
	movw	r2, #306
	mov	r5, #0
	uxth	r6, r0
	mov	r4, r5
	ldrh	r8, [r3, #236]
	mov	r7, r3
	ldrh	r9, [r3, r2]
	add	r10, r3, #264
.L225:
	uxth	r3, r5
	cmp	r8, r3
	bhi	.L227
	cmp	r4, #0
	moveq	r0, r4
	beq	.L228
	mov	r1, r4
	mov	r0, #32768
	bl	__divsi3
.L228:
	ldr	r1, [r7, #2520]
	add	r1, r1, r6, lsl #3
	strh	r0, [r1, #4]	@ movhi
	mov	r0, #0
	ldmfd	sp, {r4, r5, r6, r7, r8, r9, r10, fp, sp, pc}
.L227:
	mov	r1, r6
	ldrb	r0, [r10, r5]	@ zero_extendqisi2
	bl	V2P_block
	bl	FtlBbmIsBadBlock
	cmp	r0, #0
	add	r5, r5, #1
	addeq	r4, r4, r9
	uxtheq	r4, r4
	b	.L225
.L231:
	.align	2
.L230:
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
	ldr	r3, .L235
	ldr	r0, [r3, #2536]
	cmp	r0, #0
	ldrne	r2, [r3, #2520]
	ldrne	r3, [r3, #2540]
	subne	r0, r0, r2
	ubfxne	r0, r0, #3, #16
	lslne	r0, r0, #1
	ldrhne	r0, [r3, r0]
	ldmfd	sp, {fp, sp, pc}
.L236:
	.align	2
.L235:
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
	ldr	r2, .L245
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
.L240:
	uxth	r0, r1
	cmp	r4, r0
	bls	.L242
	lsl	r0, r3, #3
	add	r1, r1, #1
	ldrh	lr, [ip, r0]
	cmp	lr, r5
	bne	.L244
.L242:
	ldr	r2, [r2, #2540]
	lsl	r3, r3, #1
	ldrh	r0, [r2, r3]
	ldmfd	sp, {r4, r5, fp, sp, pc}
.L244:
	mov	r3, lr
	b	.L240
.L246:
	.align	2
.L245:
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
	ldr	r5, .L259
	add	r4, r6, #12
	ldr	r1, .L259+4
	bl	strcpy
	ldr	r3, [r5, #320]
	mov	r1, #64
	ldr	r2, .L259+8
	mov	r0, r4
	bl	snprintf
	add	r4, r4, r0
	ldr	r1, .L259+12
	mov	r0, r4
	add	r4, r4, #10
	bl	strcpy
	ldr	r3, [r5, #2556]
	mov	r1, #64
	ldr	r2, .L259+16
	mov	r0, r4
	bl	snprintf
	add	r4, r4, r0
	ldr	r3, [r5, #2552]
	ldr	r2, .L259+20
	mov	r1, #64
	mov	r0, r4
	add	r7, r5, #356
	bl	snprintf
	add	r4, r4, r0
	ldr	r3, [r5, #2560]
	ldr	r2, .L259+24
	mov	r1, #64
	mov	r0, r4
	ldr	r8, .L259+28
	bl	snprintf
	add	r4, r4, r0
	ldr	r3, [r5, #2564]
	ldr	r2, .L259+32
	mov	r1, #64
	mov	r0, r4
	bl	snprintf
	add	r4, r4, r0
	ldr	r3, [r5, #2568]
	ldr	r2, .L259+36
	mov	r1, #64
	mov	r0, r4
	bl	snprintf
	add	r4, r4, r0
	ldr	r3, [r5, #2572]
	ldr	r2, .L259+40
	mov	r1, #64
	mov	r0, r4
	bl	snprintf
	add	r4, r4, r0
	ldr	r3, [r5, #2576]
	ldr	r2, .L259+44
	mov	r1, #64
	mov	r0, r4
	bl	snprintf
	add	r4, r4, r0
	ldr	r3, [r5, #2580]
	ldr	r2, .L259+48
	mov	r1, #64
	mov	r0, r4
	bl	snprintf
	ldr	r3, [r5, #2584]
	add	r4, r4, r0
	ldr	r2, .L259+52
	mov	r1, #64
	mov	r0, r4
	lsr	r3, r3, #11
	bl	snprintf
	ldr	r3, [r5, #2588]
	add	r4, r4, r0
	ldr	r2, .L259+56
	mov	r1, #64
	mov	r0, r4
	lsr	r3, r3, #11
	bl	snprintf
	add	r4, r4, r0
	ldr	r3, [r5, #2592]
	ldr	r2, .L259+60
	mov	r1, #64
	mov	r0, r4
	bl	snprintf
	add	r4, r4, r0
	ldr	r3, [r5, #2596]
	ldr	r2, .L259+64
	mov	r1, #64
	mov	r0, r4
	bl	snprintf
	add	r4, r4, r0
	ldrh	r3, [r7, #6]
	ldr	r2, .L259+68
	mov	r1, #64
	mov	r0, r4
	bl	snprintf
	add	r4, r4, r0
	ldrh	r3, [r5, #228]
	ldr	r2, .L259+72
	mov	r1, #64
	mov	r0, r4
	bl	snprintf
	add	r4, r4, r0
	ldr	r3, [r5, #2600]
	ldr	r2, .L259+76
	mov	r1, #64
	mov	r0, r4
	bl	snprintf
	add	r4, r4, r0
	ldr	r3, [r5, #2604]
	ldr	r2, .L259+80
	mov	r1, #64
	mov	r0, r4
	bl	snprintf
	add	r4, r4, r0
	ldr	r3, [r5, #2608]
	ldr	r2, .L259+84
	mov	r1, #64
	mov	r0, r4
	bl	snprintf
	add	r4, r4, r0
	ldr	r3, [r5, #2612]
	ldr	r2, .L259+88
	mov	r1, #64
	mov	r0, r4
	bl	snprintf
	add	r4, r4, r0
	ldr	r3, [r5, #2616]
	ldr	r2, .L259+92
	mov	r1, #64
	mov	r0, r4
	bl	snprintf
	add	r4, r4, r0
	ldr	r3, [r5, #2620]
	ldr	r2, .L259+96
	mov	r1, #64
	mov	r0, r4
	bl	snprintf
	add	r4, r4, r0
	ldrh	r3, [r8, #30]
	ldr	r2, .L259+100
	mov	r1, #64
	mov	r0, r4
	bl	snprintf
	add	r4, r4, r0
	ldrh	r3, [r8, #28]
	ldr	r2, .L259+104
	mov	r1, #64
	mov	r0, r4
	bl	snprintf
	add	r4, r4, r0
	ldr	r3, [r5, #344]
	ldr	r2, .L259+108
	mov	r1, #64
	mov	r0, r4
	bl	snprintf
	add	r4, r4, r0
	ldr	r3, [r5, #336]
	ldr	r2, .L259+112
	mov	r1, #64
	mov	r0, r4
	bl	snprintf
	add	r4, r4, r0
	ldr	r3, [r5, #232]
	ldr	r2, .L259+116
	mov	r1, #64
	mov	r0, r4
	bl	snprintf
	add	r3, r5, #416
	add	r4, r4, r0
	ldrh	r3, [r3, #6]
	mov	r1, #64
	ldr	r2, .L259+120
	mov	r0, r4
	bl	snprintf
	add	r4, r4, r0
	ldrh	r3, [r5, #244]
	ldr	r2, .L259+124
	mov	r1, #64
	mov	r0, r4
	bl	snprintf
	add	r3, r5, #2624
	add	r4, r4, r0
	ldrh	r3, [r3]
	mov	r1, #64
	ldr	r2, .L259+128
	mov	r0, r4
	bl	snprintf
	add	r4, r4, r0
	ldr	r3, [r5, #248]
	ldr	r2, .L259+132
	mov	r1, #64
	mov	r0, r4
	bl	snprintf
	movw	r3, #2628
	add	r4, r4, r0
	ldrh	r3, [r5, r3]
	mov	r1, #64
	ldr	r2, .L259+136
	mov	r0, r4
	bl	snprintf
	add	r4, r4, r0
	ldrh	r3, [r7]
	ldr	r2, .L259+140
	mov	r1, #64
	mov	r0, r4
	bl	snprintf
	add	r4, r4, r0
	ldrh	r3, [r5, #30]
	ldr	r2, .L259+144
	mov	r1, #64
	mov	r0, r4
	bl	snprintf
	add	r4, r4, r0
	ldrb	r3, [r5, #34]	@ zero_extendqisi2
	ldr	r2, .L259+148
	mov	r1, #64
	mov	r0, r4
	bl	snprintf
	add	r4, r4, r0
	ldrh	r3, [r5, #28]
	ldr	r2, .L259+152
	mov	r1, #64
	mov	r0, r4
	bl	snprintf
	add	r4, r4, r0
	ldrb	r3, [r5, #36]	@ zero_extendqisi2
	ldr	r2, .L259+156
	mov	r1, #64
	mov	r0, r4
	bl	snprintf
	add	r4, r4, r0
	ldrh	r3, [r5, #32]
	ldr	r2, .L259+160
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
	ldr	r2, .L259+164
	bl	snprintf
	add	r4, r4, r0
	ldrh	r3, [r5, #82]
	ldr	r2, .L259+168
	mov	r1, #64
	mov	r0, r4
	bl	snprintf
	add	r4, r4, r0
	ldrb	r3, [r5, #86]	@ zero_extendqisi2
	ldr	r2, .L259+172
	mov	r1, #64
	mov	r0, r4
	bl	snprintf
	add	r4, r4, r0
	ldrh	r3, [r5, #80]
	ldr	r2, .L259+176
	mov	r1, #64
	mov	r0, r4
	bl	snprintf
	add	r4, r4, r0
	ldrb	r3, [r5, #88]	@ zero_extendqisi2
	ldr	r2, .L259+180
	mov	r1, #64
	mov	r0, r4
	bl	snprintf
	add	r4, r4, r0
	ldrh	r3, [r5, #84]
	ldr	r2, .L259+184
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
	ldr	r2, .L259+188
	bl	snprintf
	add	r4, r4, r0
	ldrh	r3, [r5, #130]
	ldr	r2, .L259+192
	mov	r1, #64
	mov	r0, r4
	bl	snprintf
	add	r4, r4, r0
	ldrb	r3, [r5, #134]	@ zero_extendqisi2
	ldr	r2, .L259+196
	mov	r1, #64
	mov	r0, r4
	bl	snprintf
	add	r4, r4, r0
	ldrh	r3, [r5, #128]
	ldr	r2, .L259+200
	mov	r1, #64
	mov	r0, r4
	bl	snprintf
	add	r4, r4, r0
	ldrb	r3, [r5, #136]	@ zero_extendqisi2
	ldr	r2, .L259+204
	mov	r1, #64
	mov	r0, r4
	bl	snprintf
	add	r4, r4, r0
	ldrh	r3, [r5, #132]
	ldr	r2, .L259+208
	mov	r1, #64
	mov	r0, r4
	bl	snprintf
	add	r4, r4, r0
	ldrh	r3, [r5, #178]
	ldr	r2, .L259+212
	mov	r1, #64
	mov	r0, r4
	bl	snprintf
	add	r4, r4, r0
	ldrb	r3, [r5, #182]	@ zero_extendqisi2
	ldr	r2, .L259+216
	mov	r1, #64
	mov	r0, r4
	bl	snprintf
	add	r4, r4, r0
	ldrh	r3, [r5, #176]
	ldr	r2, .L259+220
	mov	r1, #64
	mov	r0, r4
	bl	snprintf
	add	r4, r4, r0
	ldrb	r3, [r5, #184]	@ zero_extendqisi2
	ldr	r2, .L259+224
	mov	r1, #64
	mov	r0, r4
	bl	snprintf
	add	r4, r4, r0
	ldrh	r3, [r5, #180]
	ldr	r2, .L259+228
	mov	r1, #64
	mov	r0, r4
	bl	snprintf
	ldr	r3, [r5, #2720]
	add	r4, r4, r0
	ldr	r2, .L259+232
	mov	r1, #64
	mov	r0, r4
	str	r3, [sp, #4]
	ldr	r3, [r5, #2728]
	str	r3, [sp]
	ldr	r3, [r5, #2724]
	bl	snprintf
	add	r4, r4, r0
	ldr	r3, [r5, #2716]
	ldr	r2, .L259+236
	mov	r1, #64
	mov	r0, r4
	bl	snprintf
	add	r4, r4, r0
	ldr	r3, [r5, #2740]
	ldr	r2, .L259+240
	mov	r1, #64
	mov	r0, r4
	bl	snprintf
	movw	r3, #3156
	add	r4, r4, r0
	ldrh	r3, [r5, r3]
	mov	r1, #64
	ldr	r2, .L259+244
	mov	r0, r4
	bl	snprintf
	movw	r3, #3158
	add	r4, r4, r0
	ldrh	r3, [r5, r3]
	mov	r1, #64
	ldr	r2, .L259+248
	mov	r0, r4
	bl	snprintf
	add	r4, r4, r0
	ldr	r3, [r5, #3160]
	ldr	r2, .L259+252
	mov	r1, #64
	mov	r0, r4
	bl	snprintf
	movw	r3, #3164
	add	r4, r4, r0
	ldrh	r3, [r5, r3]
	mov	r1, #64
	ldr	r2, .L259+256
	mov	r0, r4
	bl	snprintf
	add	r4, r4, r0
	bl	GetFreeBlockMinEraseCount
	ldr	r2, .L259+260
	mov	r3, r0
	mov	r1, #64
	mov	r0, r4
	bl	snprintf
	add	r4, r4, r0
	ldrh	r0, [r5, #228]
	bl	GetFreeBlockMaxEraseCount
	ldr	r2, .L259+264
	mov	r3, r0
	mov	r1, #64
	mov	r0, r4
	bl	snprintf
	ldr	r3, .L259+268
	add	r4, r4, r0
	ldr	r3, [r3]
	cmp	r3, #1
	beq	.L248
.L253:
	sub	r0, r4, r6
	sub	sp, fp, #40
	ldmfd	sp, {r4, r5, r6, r7, r8, r9, r10, fp, sp, pc}
.L248:
	ldrh	r3, [r5, #176]
	movw	r2, #65535
	cmp	r3, r2
	beq	.L250
	ldr	r2, [r5, #76]
	lsl	r3, r3, #1
	mov	r0, r4
	mov	r1, #64
	ldrh	r3, [r2, r3]
	ldr	r2, .L259+272
	bl	snprintf
	add	r4, r4, r0
.L250:
	mov	r0, #0
	ldr	r9, .L259+276
	bl	List_get_gc_head_node
	uxth	r0, r0
	mov	r7, #0
	movw	r10, #65535
.L252:
	cmp	r0, r10
	beq	.L251
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
	bne	.L252
.L251:
	ldr	r3, [r5, #2536]
	mov	r7, #0
	ldr	r2, [r5, #2520]
	movw	r9, #65535
	ldr	r10, .L259+280
	sub	r3, r3, r2
	ubfx	r3, r3, #3, #16
.L254:
	cmp	r3, r9
	beq	.L253
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
	beq	.L253
	ldr	r3, [r5, #2520]
	ldrh	r3, [r3, r8]
	b	.L254
.L260:
	.align	2
.L259:
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
	blt	.L263
	ldr	r3, .L264
	mov	r1, #64
	ldr	r2, .L264+4
	mov	r0, r5
	bl	snprintf
	add	r4, r5, r0
	mov	r0, r4
	bl	FtlPrintInfo2buf
	add	r0, r4, r0
	sub	r0, r0, r5
	ldmfd	sp, {r4, r5, fp, sp, pc}
.L263:
	mov	r0, #0
	ldmfd	sp, {r4, r5, fp, sp, pc}
.L265:
	.align	2
.L264:
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
	ldr	r4, .L290
	ldr	r2, [r4, #2608]
	ldr	r3, [r4, #2620]
	cmp	r2, r3
	bcs	.L267
	ldr	r2, [r4, #2540]
	mov	r3, #0
	ldrh	r1, [r4, #244]
	str	r3, [r4, #2600]
	sub	r2, r2, #2
.L268:
	cmp	r3, r1
	bcc	.L269
	ldr	r5, [r4, #2600]
	mov	r0, r5
	bl	__udivsi3
	ldr	r3, .L290+4
	str	r0, [r4, #2608]
	ldr	r0, [r4, #2604]
	ldrh	r1, [r3]
	sub	r0, r5, r0
	bl	__udivsi3
	str	r0, [r4, #2600]
.L270:
	ldr	r6, [r4, #2620]
	ldr	r7, [r4, #2608]
	add	r3, r6, #256
	cmp	r3, r7
	bls	.L275
	ldr	r2, [r4, #2616]
	add	r3, r6, #768
	cmp	r3, r2
	bls	.L275
.L277:
	movw	r5, #65535
.L276:
	mov	r0, r5
	sub	sp, fp, #40
	ldmfd	sp, {r4, r5, r6, r7, r8, r9, r10, fp, sp, pc}
.L269:
	ldrh	r0, [r2, #2]!
	add	r3, r3, #1
	ldr	ip, [r4, #2600]
	add	r0, r0, ip
	str	r0, [r4, #2600]
	b	.L268
.L267:
	ldr	r3, [r4, #2616]
	cmp	r2, r3
	addhi	r3, r3, #1
	strhi	r3, [r4, #2616]
	movhi	r3, #0
	bls	.L270
.L272:
	ldrh	r2, [r4, #244]
	cmp	r3, r2
	bcs	.L270
	ldr	r0, [r4, #2540]
	lsl	r1, r3, #1
	add	r3, r3, #1
	ldrh	r2, [r0, r1]
	add	r2, r2, #1
	strh	r2, [r0, r1]	@ movhi
	b	.L272
.L275:
	ldrh	r0, [r4, #228]
	add	r0, r0, r0, lsl #1
	asr	r0, r0, #2
	bl	GetFreeBlockMaxEraseCount
	add	r3, r6, #64
	mov	r9, r0
	cmp	r0, r3
	bcc	.L277
	ldr	r3, [r4, #2524]
	cmp	r3, #0
	beq	.L277
	ldrh	r10, [r4, #244]
	movw	r1, #65535
	ldr	lr, [r4, #2520]
	mov	r0, #0
	ldr	r8, [r4, #2540]
	mov	r2, r1
.L278:
	ldrh	ip, [r3]
	movw	r5, #65535
	cmp	ip, r5
	bne	.L281
	mov	r5, r2
.L280:
	movw	r3, #65535
	cmp	r5, r3
	beq	.L277
	lsl	r3, r5, #1
	ldrh	r10, [r8, r3]
	cmp	r6, r10
	bcs	.L282
	str	r3, [fp, #-44]
	bl	GetFreeBlockMinEraseCount
	ldr	r3, [fp, #-44]
	cmp	r6, r0
	strcc	r1, [r4, #2620]
.L282:
	cmp	r7, r10
	bls	.L277
	add	r2, r10, #128
	cmp	r9, r2
	ble	.L277
	add	r2, r10, #256
	cmp	r7, r2
	bhi	.L283
	ldr	r2, [r4, #2616]
	add	r10, r10, #768
	cmp	r10, r2
	bcs	.L277
.L283:
	str	r9, [sp, #8]
	mov	r1, r5
	ldrh	r2, [r8, r3]
	ldr	r0, .L290+8
	str	r2, [sp, #4]
	ldr	r2, [r4, #76]
	ldrh	r3, [r2, r3]
	mov	r2, r7
	str	r3, [sp]
	ldr	r3, [r4, #2616]
	bl	sftl_printk
	mov	r3, #1
	str	r3, [r4, #3168]
	b	.L276
.L281:
	add	r0, r0, #1
	uxth	r0, r0
	cmp	r0, r10
	bhi	.L277
	ldrh	r5, [r3, #4]
	cmp	r5, #0
	beq	.L279
	sub	r3, r3, lr
	asr	r3, r3, #3
	uxth	r5, r3
	lsl	r3, r5, #1
	ldrh	r3, [r8, r3]
	cmp	r6, r3
	bcs	.L280
	cmp	r1, r3
	movhi	r1, r3
	movhi	r2, r5
.L279:
	add	r3, lr, ip, lsl #3
	b	.L278
.L291:
	.align	2
.L290:
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
	beq	.L293
	ldr	r3, .L294
	mov	r1, #0
	ldr	r2, [r3, #76]
	lsl	r3, r0, #1
	strh	r1, [r2, r3]	@ movhi
	bl	INSERT_FREE_LIST
.L293:
	mov	r0, #0
	ldmfd	sp, {fp, sp, pc}
.L295:
	.align	2
.L294:
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
	bne	.L297
	movw	r2, #2781
	ldr	r1, .L311
	ldr	r0, .L311+4
	bl	sftl_printk
.L297:
	ldr	r6, .L311+8
	movw	r3, #306
	ldrh	r2, [r4, #2]
	ldrh	r3, [r6, r3]
	cmp	r2, r3
	bne	.L298
	movw	r2, #2782
	ldr	r1, .L311
	ldr	r0, .L311+4
	bl	sftl_printk
.L298:
	ldrh	r3, [r4, #4]
	cmp	r3, #0
	bne	.L299
	movw	r2, #2783
	ldr	r1, .L311
	ldr	r0, .L311+4
	bl	sftl_printk
.L299:
	ldrb	r2, [r4, #6]	@ zero_extendqisi2
	mov	r3, #0
	ldrh	r0, [r6, #236]
	movw	ip, #65535
	strb	r3, [r4, #10]
	add	r2, r4, r2, lsl #1
	ldrh	r1, [r2, #16]
	mov	r2, r3
.L300:
	cmp	r1, ip
	beq	.L302
	movw	r3, #306
	ldrh	r5, [r4, #2]
	ldrh	ip, [r6, r3]
	cmp	r5, ip
	movwcs	r5, #65535
	bcs	.L296
	ldrh	r2, [r4, #4]
	orr	r5, r5, r1, lsl #10
	ldrb	r3, [r4, #6]	@ zero_extendqisi2
	movw	lr, #65535
	sub	r2, r2, #1
	uxth	r2, r2
	strh	r2, [r4, #4]	@ movhi
.L305:
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
	beq	.L305
	strb	r3, [r4, #6]
	cmp	r2, #0
	ldrh	r3, [r4, #2]
	sub	r3, r3, ip
	clz	r3, r3
	lsr	r3, r3, #5
	moveq	r3, #0
	cmp	r3, #0
	beq	.L296
	movw	r2, #2806
	ldr	r1, .L311
	ldr	r0, .L311+4
	bl	sftl_printk
.L296:
	mov	r0, r5
	ldmfd	sp, {r4, r5, r6, fp, sp, pc}
.L302:
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
	b	.L300
.L312:
	.align	2
.L311:
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
	ldr	r5, .L318
	mov	lr, #12
	mov	r4, #1
	ldr	r2, .L318+4
	mov	r6, #20
	mov	r3, #0
	str	r3, [r2, #3172]
.L314:
	ldrh	r1, [r2, #236]
	uxth	r0, r3
	add	ip, r3, #1
	cmp	r0, r1
	bcc	.L315
	ldr	lr, .L318
	mov	r0, #12
	mov	ip, #0
.L316:
	ldr	r3, [r2, #3192]
	cmp	r1, r3
	bcc	.L317
	ldmfd	sp, {r4, r5, r6, r7, r8, r9, fp, sp, pc}
.L315:
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
	ldr	r1, .L318+8
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
	b	.L314
.L317:
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
	ldr	r3, .L318+8
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
	b	.L316
.L319:
	.align	2
.L318:
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
	ldr	r3, .L327
	mov	lr, #0
	mov	r5, #20
	mov	r7, #12
	mov	r8, lr
	ldr	r6, [r3, #3192]
	ldr	r4, [r3, #3176]
.L321:
	uxth	r3, lr
	cmp	r1, r3
	ldmfdls	sp, {r4, r5, r6, r7, r8, r9, r10, fp, sp, pc}
	mla	ip, r5, r3, r0
	mov	r2, #0
.L322:
	uxth	r3, r2
	cmp	r6, r3
	bls	.L323
	mul	r3, r7, r3
	add	r2, r2, #1
	ldr	r10, [r4, r3]
	add	r9, r4, r3
	ldr	r3, [ip, #8]
	cmp	r10, r3
	bne	.L322
	str	r8, [r9, #8]
.L323:
	add	lr, lr, #1
	b	.L321
.L328:
	.align	2
.L327:
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
	ldr	r3, .L336
	mov	ip, #0
	mov	r6, #12
	mov	r7, #1
	mov	r8, #20
	ldr	r4, [r3, #3192]
	ldr	r5, [r3, #3176]
.L330:
	uxth	r2, ip
	cmp	r1, r2
	bhi	.L334
	ldmfd	sp, {r4, r5, r6, r7, r8, r9, fp, sp, pc}
.L334:
	mov	lr, #0
.L331:
	uxth	r3, lr
	cmp	r4, r3
	bls	.L332
	mla	r3, r6, r3, r5
	add	lr, lr, #1
	ldr	r9, [r3, #8]
	cmp	r9, #0
	bne	.L331
	mla	r2, r8, r2, r0
	ldr	lr, [r3]
	str	r7, [r3, #8]
	str	lr, [r2, #8]
	ldr	r3, [r3, #4]
	str	r3, [r2, #12]
.L332:
	add	ip, ip, #1
	b	.L330
.L337:
	.align	2
.L336:
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
	ldr	r2, .L343
	uxth	r0, r0
	ldr	r3, [r2, #3196]
	add	r2, r2, #3200
	ldrh	r2, [r2]
	add	r2, r3, r2, lsl #1
.L339:
	cmp	r3, r2
	bne	.L341
	mov	r0, #0
	ldmfd	sp, {fp, sp, pc}
.L341:
	ldrh	r1, [r3], #2
	cmp	r1, r0
	bne	.L339
	mov	r0, #1
	ldmfd	sp, {fp, sp, pc}
.L344:
	.align	2
.L343:
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
	ldr	r3, .L349
	mov	ip, #0
	add	lr, r3, #3200
	ldr	r1, [r3, #3196]
	ldrh	r7, [lr]
	sub	r2, r1, #2
.L346:
	uxth	r8, ip
	cmp	r8, r7
	bcc	.L348
	moveq	ip, r8
	lsleq	ip, ip, #1
	strheq	r0, [r1, ip]	@ movhi
	ldrheq	r2, [lr]
	addeq	r2, r2, #1
	strheq	r2, [lr]	@ movhi
	b	.L347
.L348:
	ldrh	r8, [r2, #2]!
	add	ip, ip, #1
	cmp	r8, r0
	bne	.L346
.L347:
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
.L350:
	.align	2
.L349:
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
	ldr	r0, .L354
	mov	r1, r4
	bl	sftl_printk
	ldr	r3, .L354+4
	ldrh	r0, [r3, #226]
	cmp	r4, r0
	beq	.L352
	ldrh	r1, [r3, #224]
	cmp	r4, r1
	beq	.L352
	movw	r2, #65535
	cmp	r0, r2
	strheq	r4, [r3, #226]	@ movhi
	beq	.L352
	cmp	r1, r2
	strheq	r4, [r3, #224]	@ movhi
.L352:
	mov	r0, #0
	ldmfd	sp, {r4, fp, sp, pc}
.L355:
	.align	2
.L354:
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
	ldr	r4, .L360
	uxth	r5, r0
	movw	r6, #3210
	mov	r0, r5
	bl	P2V_block_in_plane
	mov	r2, r5
	mov	r7, r0
	ldrh	r1, [r4, r6]
	ldr	r0, .L360+4
	bl	sftl_printk
	mov	r0, r7
	bl	FtlGcRefreshBlock
	ldrh	r3, [r4, r6]
	mov	r2, #0
	ldr	r0, .L360+8
.L357:
	uxth	r1, r2
	cmp	r3, r1
	bhi	.L359
	cmp	r3, #15
	movwls	r2, #3210
	addls	r1, r3, #1
	strhls	r1, [r4, r2]	@ movhi
	lslls	r3, r3, #1
	ldrls	r2, .L360+8
	strhls	r5, [r2, r3]	@ movhi
	b	.L358
.L359:
	add	r2, r2, #1
	add	r1, r0, r2, lsl #1
	ldrh	r1, [r1, #-2]
	cmp	r1, r5
	bne	.L357
.L358:
	mov	r0, #0
	ldmfd	sp, {r4, r5, r6, r7, fp, sp, pc}
.L361:
	.align	2
.L360:
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
	ldr	r4, .L368
	movw	r3, #3210
	ldrh	r2, [r4, r3]
	cmp	r2, #0
	beq	.L363
	ldrh	r1, [r4, #226]
	movw	r3, #65535
	cmp	r1, r3
	bne	.L363
	movw	r3, #3246
	movw	r5, #3246
	ldrh	r1, [r4, r3]
	cmp	r1, r2
	movcs	r2, #0
	strhcs	r2, [r4, r3]	@ movhi
	ldrh	r3, [r4, r5]
	ldr	r2, .L368+4
	lsl	r3, r3, #1
	ldrh	r0, [r2, r3]
	bl	P2V_block_in_plane
	bl	FtlGcRefreshBlock
	ldrh	r3, [r4, r5]
	add	r3, r3, #1
	strh	r3, [r4, r5]	@ movhi
.L363:
	mov	r0, #0
	ldmfd	sp, {r4, r5, fp, sp, pc}
.L369:
	.align	2
.L368:
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
	ldr	r1, .L371
	bl	__kmalloc
	ldmfd	sp, {fp, sp, pc}
.L372:
	.align	2
.L371:
	.word	6291649
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
.L375:
	cmp	r4, r9
	bcc	.L381
	ldr	r0, .L383
	bl	sftl_printk
	ldmfd	sp, {r4, r5, r6, r7, r8, r9, r10, fp, sp, pc}
.L381:
	cmp	r5, #0
	bne	.L376
	mov	r3, r6
	mov	r2, r7
	mov	r1, r10
	ldr	r0, .L383+4
	bl	sftl_printk
.L376:
	cmp	r8, #4
	ldreq	r1, [r7, r4, lsl #2]
	ldreq	r0, .L383+8
	beq	.L382
	cmp	r8, #2
	lsleq	r3, r4, #1
	ldreq	r0, .L383+12
	ldrbne	r1, [r7, r4]	@ zero_extendqisi2
	ldrheq	r1, [r7, r3]
	ldrne	r0, .L383+16
.L382:
	add	r5, r5, #1
	bl	sftl_printk
	cmp	r5, #15
	bls	.L380
	mov	r5, #0
	ldr	r0, .L383
	bl	sftl_printk
.L380:
	add	r4, r4, #1
	add	r6, r6, r8
	b	.L375
.L384:
	.align	2
.L383:
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
	ldr	r6, .L401
	mov	r8, r2
	mov	r4, r0
	mov	r5, r0
	ldrh	r9, [r6, #12]
	lsl	r10, r9, #3
.L386:
	cmp	r7, r8
	beq	.L390
	sub	r2, fp, #48
	sub	r1, fp, #44
	mov	r0, r5
	bl	l2p_addr_tran
	ldr	ip, [fp, #-48]
	cmp	ip, #0
	bne	.L387
	ldr	r3, [fp, #-44]
	cmp	r10, r3
	bls	.L387
	ldr	r5, .L401+4
	ldr	r6, .L401+8
	b	.L400
.L389:
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
	ldr	r0, .L401+12
	bl	rknand_print_hex
	mov	r3, #4
	ldr	r1, [r4, #-8]
	mov	r2, r3
	ldr	r0, .L401+16
	bl	rknand_print_hex
	ldr	ip, [fp, #-52]
	add	ip, ip, #1
.L400:
	cmp	ip, r8
	add	r4, r4, #20
	bne	.L389
	bl	dump_stack
.L390:
	mov	r0, #0
	sub	sp, fp, #40
	ldmfd	sp, {r4, r5, r6, r7, r8, r9, r10, fp, sp, pc}
.L387:
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
	bne	.L394
	ldr	r1, [fp, #-44]
	ldr	r3, [r6, #3252]
	ldrb	r0, [fp, #-48]	@ zero_extendqisi2
	add	r1, r9, r1
	blx	r3
	cmp	r0, #0
	mvnne	r3, #0
	strne	r3, [r5]
.L394:
	add	r7, r7, #1
	add	r5, r5, #20
	b	.L386
.L402:
	.align	2
.L401:
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
	ldr	r5, .L412
	add	r4, r5, #416
	ldrh	r3, [r4, #6]
	cmp	r3, #1024
	ldmfdeq	sp, {r4, r5, r6, r7, fp, sp, pc}
	cmp	r1, #0
	beq	.L405
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
.L405:
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
.L413:
	.align	2
.L412:
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
	ldr	r6, .L419
	add	r4, r6, #416
	ldrh	r2, [r4, #6]
	cmp	r2, #0
	movweq	r5, #65535
	beq	.L415
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
.L415:
	sub	r3, r5, #1
	movw	r2, #65533
	uxth	r3, r3
	cmp	r3, r2
	bls	.L416
	ldrh	r2, [r4, #6]
	mov	r1, r5
	ldr	r0, .L419+4
	bl	sftl_printk
.L417:
	b	.L417
.L416:
	mov	r0, r5
	ldmfd	sp, {r4, r5, r6, fp, sp, pc}
.L420:
	.align	2
.L419:
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
.L422:
	uxth	r5, r3
	cmp	r5, r1
	bcs	.L425
	mov	r7, r2
	add	r3, r3, #1
	ldrh	r6, [r7]
	add	r2, r2, #2
	cmp	r6, #0
	bne	.L422
	bl	FtlFreeSysBlkQueueOut
	sub	r3, r0, #1
	movw	r2, #65533
	uxth	r3, r3
	mov	r1, r0
	strh	r0, [r7]	@ movhi
	cmp	r3, r2
	bls	.L423
	ldr	r3, .L429
	ldr	r0, .L429+4
	ldrh	r2, [r3, #6]
	bl	sftl_printk
.L424:
	b	.L424
.L423:
	ldr	r3, [r4, #28]
	strh	r6, [r4, #2]	@ movhi
	strh	r5, [r4]	@ movhi
	add	r3, r3, #1
	str	r3, [r4, #28]
	ldrh	r3, [r4, #8]
	add	r3, r3, #1
	strh	r3, [r4, #8]	@ movhi
.L425:
	ldrh	r3, [r4, #10]
	cmp	r3, r5
	bhi	.L427
	movw	r2, #578
	ldr	r1, .L429+8
	ldr	r0, .L429+12
	bl	sftl_printk
.L427:
	mov	r0, #0
	ldmfd	sp, {r4, r5, r6, r7, fp, sp, pc}
.L430:
	.align	2
.L429:
	.word	.LANCHOR0+416
	.word	.LC90
	.word	.LANCHOR1+147
	.word	.LC8
	.size	ftl_map_blk_alloc_new_blk, .-ftl_map_blk_alloc_new_blk
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
	push	{fp, ip, lr, pc}
	sub	fp, ip, #4
	bl	memset
	ldmfd	sp, {fp, sp, pc}
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
	ldr	r4, .L533
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
	ldr	r3, .L533+4
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
.L433:
	cmp	r3, lr
	bcc	.L434
	ldr	r2, .L533+8
	mov	r1, #0
	add	r3, r2, r3, lsl #2
	add	r2, r2, #56
	add	r3, r3, #24
.L435:
	cmp	r2, r3
	bne	.L436
	ldr	r3, [r4, #3360]
	cmp	r3, #0
	bne	.L437
.L439:
	ldr	r1, .L533+12
	ldr	r0, .L533+16
	bl	sftl_printk
	mvn	r0, #0
	ldmfd	sp, {r4, r5, r6, r7, r8, r9, fp, sp, pc}
.L434:
	ldr	r0, [r4, #384]
	add	r3, r3, #1
	add	r0, r0, r1
	add	r1, r1, r2
	str	r0, [ip, #4]!
	b	.L433
.L436:
	str	r1, [r3, #4]!
	b	.L435
.L437:
	ldr	r3, [r4, #3364]
	cmp	r3, #0
	beq	.L439
	ldr	r3, [r4, #3380]
	cmp	r3, #0
	beq	.L439
	ldr	r3, [r4, #3384]
	cmp	r3, #0
	beq	.L439
	ldr	r3, [r4, #2544]
	cmp	r3, #0
	beq	.L439
	ldr	r3, [r4, #3388]
	cmp	r3, #0
	beq	.L439
	ldr	r3, [r4, #2520]
	cmp	r3, #0
	beq	.L439
	ldr	r3, [r4, #384]
	cmp	r3, #0
	beq	.L439
	ldr	r3, [r4, #76]
	cmp	r3, #0
	beq	.L439
	ldr	r3, [r4, #3196]
	cmp	r3, #0
	beq	.L439
	ldr	r3, [r4, #3204]
	cmp	r3, #0
	beq	.L439
	ldr	r3, [r4, #3292]
	cmp	r3, #0
	beq	.L439
	ldr	r3, [r4, #3300]
	cmp	r3, #0
	beq	.L439
	ldr	r3, [r4, #3272]
	cmp	r3, #0
	beq	.L439
	ldr	r3, [r4, #3188]
	cmp	r3, #0
	beq	.L439
	ldr	r3, [r4, #3296]
	cmp	r3, #0
	beq	.L439
	ldr	r3, [r4, #3304]
	cmp	r3, #0
	beq	.L439
	ldr	r3, [r4, #3308]
	cmp	r3, #0
	beq	.L439
	ldr	r3, [r4, #3312]
	cmp	r3, #0
	beq	.L439
	ldr	r3, [r4, #3180]
	cmp	r3, #0
	beq	.L439
	ldr	r3, [r4, #3316]
	cmp	r3, #0
	beq	.L439
	ldr	r3, [r4, #3320]
	cmp	r3, #0
	beq	.L439
	ldr	r3, [r4, #3176]
	cmp	r3, #0
	beq	.L439
	ldr	r3, [r4, #3336]
	cmp	r3, #0
	beq	.L439
	ldr	r3, [r4, #3340]
	cmp	r3, #0
	beq	.L439
	ldr	r3, [r4, #3184]
	cmp	r3, #0
	beq	.L439
	ldr	r3, [r4, #2540]
	cmp	r3, #0
	beq	.L439
	ldr	r3, [r4, #3348]
	cmp	r3, #0
	beq	.L439
	ldr	r3, [r4, #352]
	cmp	r3, #0
	beq	.L439
	ldr	r3, .L533
	ldr	r2, [r3, #3368]
	cmp	r2, #0
	beq	.L439
	ldr	r2, [r3, #3372]
	cmp	r2, #0
	beq	.L439
	ldr	r3, [r3, #3376]
	cmp	r3, #0
	beq	.L439
	mov	r0, #0
	ldmfd	sp, {r4, r5, r6, r7, r8, r9, fp, sp, pc}
.L534:
	.align	2
.L533:
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
	ldr	r3, .L541
	mov	r4, r0
	mov	r6, r1
	ldr	r7, .L541+4
	sub	r5, r4, #2
	ldrh	r2, [r3]
	add	r4, r4, #1020
	ldr	r8, .L541+8
	add	r4, r4, #2
	mov	r1, #0
	mov	r0, r6
	lsl	r2, r2, #2
	bl	ftl_memset
.L538:
	ldrh	r3, [r5, #2]
	movw	r2, #65535
	cmp	r3, r2
	ldmfdeq	sp, {r4, r5, r6, r7, r8, fp, sp, pc}
	ldrh	r2, [r7]
	cmp	r2, r3
	bhi	.L537
	mov	r2, #74
	mov	r1, r8
	ldr	r0, .L541+12
	bl	sftl_printk
.L537:
	ldrh	r3, [r5, #2]!
	mov	r0, #1
	cmp	r4, r5
	lsr	r1, r3, #5
	and	r3, r3, #31
	ldr	r2, [r6, r1, lsl #2]
	orr	r3, r2, r0, lsl r3
	str	r3, [r6, r1, lsl #2]
	bne	.L538
	ldmfd	sp, {r4, r5, r6, r7, r8, fp, sp, pc}
.L542:
	.align	2
.L541:
	.word	.LANCHOR0+3392
	.word	.LANCHOR0+302
	.word	.LANCHOR1+184
	.word	.LC8
	.size	FtlBbt2Bitmap, .-FtlBbt2Bitmap
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
	ldr	r0, .L544
	mvn	r2, #0
	mov	r1, #255
	add	r3, r0, #356
	add	r0, r0, #368
	strh	r2, [r3]	@ movhi
	mov	r2, #0
	strh	r2, [r3, #6]	@ movhi
	mov	r2, #16
	bl	ftl_memset
	ldmfd	sp, {fp, sp, pc}
.L545:
	.align	2
.L544:
	.word	.LANCHOR0
	.size	FtlBbtMemInit, .-FtlBbtMemInit
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
	ldr	ip, .L547
	mov	r4, #0
	mov	r2, #2048
	mov	r1, r4
	add	r3, ip, #416
	strh	r0, [r3]	@ movhi
	add	r0, ip, #424
	strh	r4, [r3, #2]	@ movhi
	strh	r4, [r3, #4]	@ movhi
	strh	r4, [r3, #6]	@ movhi
	bl	ftl_memset
	mov	r0, r4
	ldmfd	sp, {r4, fp, sp, pc}
.L548:
	.align	2
.L547:
	.word	.LANCHOR0
	.size	FtlFreeSysBlkQueueInit, .-FtlFreeSysBlkQueueInit
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
.L550:
	ldrh	r1, [r4, #6]
	uxth	r3, r2
	cmp	r1, r3
	bhi	.L554
	ldr	r3, .L569
	mov	r6, #0
	mov	r8, r6
	mov	r10, r6
	ldrh	r2, [r3]
	ldrh	r3, [r4]
	lsl	r3, r3, #1
	strh	r2, [r5, r3]	@ movhi
	ldrh	r9, [r5]
.L555:
	ldrh	r3, [r4, #10]
	uxth	r1, r6
	cmp	r3, r1
	bhi	.L559
	mov	r0, r8
	ldmib	sp, {r4, r5, r6, r7, r8, r9, r10, fp, sp, pc}
.L554:
	uxth	r3, r2
	mov	r1, #0
	ldr	r0, [r6, r3, lsl #2]
	ubfx	r0, r0, #10, #16
.L551:
	ldrh	ip, [r4, #10]
	uxth	r3, r1
	cmp	ip, r3
	addls	r2, r2, #1
	bls	.L550
.L553:
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
	b	.L551
.L559:
	uxth	r3, r6
	lsl	r3, r3, #1
	ldrh	r2, [r5, r3]
	cmp	r9, r2
	bls	.L556
	ldrh	r0, [r7, r3]
	add	ip, r7, r3
	cmp	r0, #0
	bne	.L557
.L558:
	add	r6, r6, #1
	b	.L555
.L556:
	cmp	r2, #0
	bne	.L558
	ldrh	r0, [r7, r3]
	add	ip, r7, r3
	cmp	r0, #0
	beq	.L558
.L560:
	mov	r1, #1
	str	ip, [fp, #-44]
	bl	FtlFreeSysBlkQueueIn
	ldr	ip, [fp, #-44]
	strh	r10, [ip]	@ movhi
	ldrh	r3, [r4, #8]
	sub	r3, r3, #1
	strh	r3, [r4, #8]	@ movhi
	b	.L558
.L557:
	subs	r9, r2, #0
	mov	r8, r1
	beq	.L560
	b	.L558
.L570:
	.align	2
.L569:
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
	ldr	r4, .L574
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
	ldr	r0, .L574+4
	mov	r2, #0
	mov	r3, r4
	mov	lr, #12
	mov	r4, r2
	sub	r6, r0, #28
.L572:
	ldrh	r7, [r0]
	uxth	r1, r2
	add	ip, r2, #1
	cmp	r7, r1
	bhi	.L573
	ldr	r2, .L574+8
	mvn	r1, #0
	movw	r0, #3396
	strh	r1, [r3, r0]	@ movhi
	strh	r1, [r2, #2]	@ movhi
	ldr	r1, [r3, #332]
	strh	r1, [r2, #10]	@ movhi
	ldr	r1, .L574+12
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
.L573:
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
	b	.L572
.L575:
	.align	2
.L574:
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
	ldr	r4, .L577
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
	mov	r1, r5
	mov	r2, #48
	add	r0, r0, #8
	bl	ftl_memset
	add	r0, r4, #2640
	mov	r2, #512
	mov	r1, r5
	add	r0, r0, #4
	bl	ftl_memset
	bl	FtlGcBufInit
	bl	FtlL2PDataInit
	mov	r0, r5
	ldmfd	sp, {r4, r5, fp, sp, pc}
.L578:
	.align	2
.L577:
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
	ldr	r4, .L590
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
.L580:
	ldrh	r3, [r4, #244]
	uxth	r10, r5
	cmp	r10, r3
	bcs	.L587
	ldr	r3, .L590+4
	mov	r9, r10
	ldrh	ip, [r4, #236]
	ldrh	r3, [r3]
	str	r3, [fp, #-44]
	mov	r3, #0
	mov	r7, r3
	b	.L588
.L582:
	ldr	r2, .L590+8
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
.L588:
	uxth	r1, r3
	cmp	ip, r1
	bhi	.L582
	cmp	r7, #0
	beq	.L583
	mov	r1, r7
	mov	r0, #32768
	bl	__divsi3
	uxth	r7, r0
.L584:
	ldr	r3, [r4, #2520]
	add	r3, r3, r9, lsl #3
	strh	r7, [r3, #4]	@ movhi
	ldrh	r3, [r4, #28]
	cmp	r3, r10
	beq	.L585
	ldrh	r3, [r4, #80]
	cmp	r3, r10
	beq	.L585
	ldrh	r3, [r4, #128]
	cmp	r3, r10
	beq	.L585
	ldr	r2, [r4, #76]
	lsl	r3, r9, #1
	ldrh	r3, [r2, r3]
	cmp	r3, #0
	bne	.L586
	add	r8, r8, #1
	mov	r0, r9
	uxth	r8, r8
	bl	INSERT_FREE_LIST
.L585:
	add	r5, r5, #1
	b	.L580
.L583:
	ldr	r1, [r4, #76]
	lsl	r3, r9, #1
	mvn	r0, #0
	strh	r0, [r1, r3]	@ movhi
	b	.L584
.L586:
	add	r6, r6, #1
	mov	r0, r9
	uxth	r6, r6
	bl	INSERT_DATA_LIST
	b	.L585
.L587:
	movw	r2, #2532
	strh	r8, [r4, #228]	@ movhi
	strh	r6, [r4, r2]	@ movhi
	add	r6, r6, r8
	cmp	r6, r3
	ble	.L589
	movw	r2, #2210
	ldr	r1, .L590+12
	ldr	r0, .L590+16
	bl	sftl_printk
.L589:
	mov	r0, #0
	sub	sp, fp, #40
	ldmfd	sp, {r4, r5, r6, r7, r8, r9, r10, fp, sp, pc}
.L591:
	.align	2
.L590:
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
	ldr	r4, .L593
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
.L594:
	.align	2
.L593:
	.word	.LANCHOR0
	.size	FtlGcPageVarInit, .-FtlGcPageVarInit
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
	ldr	r5, .L602
	mov	r4, r0
	mov	r6, r1
	mov	r2, #256
	mov	r1, #255
	bl	ftl_memset
	ldr	r3, [r5, #3248]
	mov	r1, r6
	mov	r0, r4
	blx	r3
	uxth	r0, r0
	cmp	r0, #50
	bls	.L596
	mov	r2, #256
	mov	r1, #255
	mov	r0, r4
	bl	ftl_memset
	mov	r0, #0
.L596:
	ldrh	r3, [r5, #14]
	cmp	r3, #4
	moveq	r3, r4
	addeq	r1, r3, r0, lsl #1
	beq	.L598
	ldmfd	sp, {r4, r5, r6, fp, sp, pc}
.L599:
	ldrh	r2, [r3]
	lsr	r2, r2, #1
	strh	r2, [r3], #2	@ movhi
.L598:
	cmp	r3, r1
	bne	.L599
	ldmfd	sp, {r4, r5, r6, fp, sp, pc}
.L603:
	.align	2
.L602:
	.word	.LANCHOR0
	.size	FlashGetBadBlockList, .-FlashGetBadBlockList
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
	ldr	r5, .L644
	mov	r8, r1
	ldr	r9, .L644+4
	mov	r4, r0
	ldr	r10, .L644+8
	ldrh	r3, [r5, #12]
	str	r3, [fp, #-52]
.L606:
	cmp	r7, r8
	bne	.L619
	mov	r0, #0
	sub	sp, fp, #40
	ldmfd	sp, {r4, r5, r6, r7, r8, r9, r10, fp, sp, pc}
.L619:
	ldr	r3, [r4, #8]
	cmp	r3, #0
	beq	.L607
	ldr	r3, [r4, #12]
	cmp	r3, #0
	bne	.L608
.L607:
	mov	r2, #96
	mov	r1, r9
	mov	r0, r10
	bl	sftl_printk
.L608:
	sub	r2, fp, #48
	sub	r1, fp, #44
	mov	r0, r4
	bl	l2p_addr_tran
	ldr	r0, [fp, #-48]
	cmp	r0, #3
	mvnhi	r3, #0
	strhi	r3, [r4]
	bhi	.L610
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
	bne	.L613
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
	beq	.L614
	ldr	r3, [r4, #12]
	ldr	r2, [r3, #12]
	cmn	r2, #1
	bne	.L615
	ldr	r2, [r3, #8]
	cmn	r2, #1
	bne	.L615
	ldr	r3, [r3]
	cmn	r3, #1
	beq	.L615
.L614:
	mvn	r3, #0
	str	r3, [r4]
.L615:
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
	bne	.L613
	ldr	r1, [r4, #4]
	ldr	r2, [fp, #-44]
	ldr	r0, .L644+12
	bl	sftl_printk
	ldr	r1, [r4, #8]
	cmp	r1, #0
	beq	.L618
	mov	r3, #4
	ldr	r0, .L644+16
	mov	r2, r3
	bl	rknand_print_hex
.L618:
	ldr	r1, [r4, #12]
	cmp	r1, #0
	beq	.L613
	mov	r3, #4
	ldr	r0, .L644+20
	mov	r2, r3
	bl	rknand_print_hex
.L613:
	ldr	r3, [r5, #3324]
	cmp	r6, r3
	bne	.L610
	ldr	r0, [r4, #8]
	cmp	r6, r0
	beq	.L610
	ldr	r3, .L644+24
	mov	r1, r6
	ldrh	r2, [r3]
	lsl	r2, r2, #9
	bl	ftl_memcpy
.L610:
	add	r7, r7, #1
	add	r4, r4, #20
	b	.L606
.L645:
	.align	2
.L644:
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
	ldr	r5, .L656
	mov	r7, #0
	add	r6, r5, #364
	ldr	r3, [r5, #3304]
	ldr	r8, [r5, #3336]
	add	r6, r6, #2
	sub	r9, r6, #64
	str	r3, [r5, #3464]
	str	r8, [r5, #3468]
.L647:
	ldr	r3, .L656+4
	ldrh	r3, [r3]
	cmp	r7, r3
	bcc	.L652
	mov	r0, #0
	ldmfd	sp, {r4, r5, r6, r7, r8, r9, r10, fp, sp, pc}
.L652:
	ldrh	r4, [r9]
	mvn	r3, #0
	ldr	r10, .L656+8
	strh	r3, [r6, #2]!	@ movhi
	add	r4, r4, r3
	uxth	r4, r4
.L648:
	ldrh	r3, [r9]
	sub	r2, r3, #16
	cmp	r4, r2
	ble	.L650
	mla	r3, r7, r3, r4
	mov	r2, #1
	mov	r1, r2
	mov	r0, r10
	lsl	r3, r3, #10
	str	r3, [r5, #3460]
	bl	FlashReadPages
	ldr	r3, [r5, #3456]
	cmn	r3, #1
	beq	.L649
	ldrh	r2, [r8]
	movw	r3, #61664
	cmp	r2, r3
	bne	.L649
	strh	r4, [r6]	@ movhi
.L650:
	add	r7, r7, #1
	b	.L647
.L649:
	sub	r4, r4, #1
	uxth	r4, r4
	b	.L648
.L657:
	.align	2
.L656:
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
	ldr	r3, .L669
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
	beq	.L662
.L661:
	mov	r0, r5
	sub	sp, fp, #32
	ldmfd	sp, {r4, r5, r6, r7, r8, fp, sp, pc}
.L665:
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
	bne	.L663
	ldr	r3, [fp, #-92]
	cmn	r3, #1
	bne	.L663
	ldr	r3, [fp, #-116]
	cmn	r3, #1
	subne	r4, r4, #1
	sxthne	r5, r4
	bne	.L662
.L663:
	add	r4, r4, #1
	sxth	r8, r4
.L662:
	cmp	r8, r5
	ble	.L665
	b	.L661
.L670:
	.align	2
.L669:
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
	ldr	r4, .L753
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
	bl	ftl_memset
	ldrh	r3, [r4, #244]
	str	r3, [fp, #-44]
.L672:
	ldrh	r3, [r5, #246]
	ldr	r2, [fp, #-44]
	cmp	r3, r2
	bls	.L713
	ldr	r8, .L753+4
	mov	r6, #0
	ldrh	ip, [r5, #236]
	mov	r10, r6
	ldr	r9, [r5, #3292]
	ldrh	r1, [r8], #-52
	ldr	r2, [r5, #3180]
	ldr	r3, [r5, #3184]
	str	r1, [fp, #-48]
	b	.L714
.L674:
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
	bne	.L673
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
.L673:
	add	r6, r6, #1
.L714:
	uxth	r1, r6
	cmp	ip, r1
	bhi	.L674
	cmp	r10, #0
	bne	.L675
.L712:
	ldr	r3, [fp, #-44]
	add	r3, r3, #1
	uxth	r3, r3
	str	r3, [fp, #-44]
	b	.L672
.L675:
	mov	r2, #1
	mov	r1, r10
	mov	r0, r9
	bl	FlashReadPages
	mov	r3, #0
.L752:
	str	r3, [fp, #-48]
	ldrh	r3, [fp, #-48]
	cmp	r10, r3
	bls	.L712
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
	bne	.L679
	mov	r3, #16
.L681:
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
	bne	.L678
	ldr	r3, [r4, #3292]
	mvn	r2, #0
	str	r2, [r3, r9]
	ldr	r3, [r4, #3292]
	ldr	r3, [r3, r9]
	cmp	r3, r2
	bne	.L679
.L680:
	mov	r1, #1
	b	.L751
.L678:
	ldr	r2, [r4, #3292]
	ldr	r2, [r2, r9]
	cmn	r2, #1
	bne	.L679
	sub	r3, r3, #1
	uxth	r3, r3
	cmp	r3, #0
	bne	.L681
	b	.L680
.L679:
	ldr	r2, [r5, #2592]
	ldr	r3, [r6, #4]
	cmn	r2, #1
	beq	.L682
	cmp	r2, r3
	bhi	.L683
.L682:
	cmn	r3, #1
	addne	r2, r3, #1
	strne	r2, [r7, #2592]
.L683:
	ldrh	r2, [r6]
	movw	r1, #61604
	cmp	r2, r1
	beq	.L685
	bhi	.L686
	movw	r3, #61574
	cmp	r2, r3
	beq	.L687
.L684:
	ldr	r3, [fp, #-48]
	add	r3, r3, #1
	b	.L752
.L686:
	movw	r3, #61634
	cmp	r2, r3
	beq	.L688
	movw	r3, #65535
	cmp	r2, r3
	moveq	r1, #0
	bne	.L684
.L751:
	uxth	r0, ip
	bl	FtlFreeSysBlkQueueIn
	b	.L684
.L688:
	ldr	r9, .L753+8
	ldr	r3, [r4, #332]
	ldrh	r2, [r9]
	cmp	r2, r3
	bls	.L690
	movw	r2, #1225
	ldr	r1, .L753+12
	ldr	r0, .L753+16
	bl	sftl_printk
.L690:
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
.L691:
	ldr	r1, [fp, #-52]
	cmp	r3, r1
	bgt	.L697
	cmp	r3, #0
	bge	.L729
	b	.L684
.L697:
	lsl	lr, r3, #2
	ldr	r1, [r6, #4]
	str	lr, [fp, #-56]
	ldr	lr, [ip, r3, lsl #2]
	cmp	r1, lr
	bls	.L692
	ldr	r1, [ip]
	cmp	r1, #0
	bne	.L693
	cmp	r0, r2
	addne	r2, r2, #1
	strhne	r2, [r9]	@ movhi
.L693:
	uxth	ip, r3
	mov	r1, #0
.L694:
	uxth	r0, r1
	sxth	r2, r1
	cmp	ip, r0
	bhi	.L695
	ldr	r1, [r6, #4]
	cmp	r3, #0
	ldr	r2, [r7, #3384]
	ldr	r0, [fp, #-56]
	str	r1, [r2, r0]
	lsl	r2, r3, #1
	ldr	r1, [r7, #3360]
	strh	r8, [r1, r2]	@ movhi
	blt	.L684
	ldr	r2, .L753+8
	ldr	r1, [r7, #332]
	ldrh	r2, [r2]
	sub	r1, r1, r2
	sub	r1, r1, #1
	sxth	r1, r1
	cmp	r3, r1
	bgt	.L684
.L729:
	add	r2, r2, #1
	ldr	r1, [r6, #4]
	strh	r2, [r9]	@ movhi
	ldr	r2, [r4, #3384]
	str	r1, [r2, r3, lsl #2]
	lsl	r3, r3, #1
	ldr	r2, [r4, #3360]
.L749:
	strh	r8, [r2, r3]	@ movhi
	b	.L684
.L695:
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
	b	.L694
.L692:
	sub	r3, r3, #1
	sxth	r3, r3
	b	.L691
.L687:
	ldr	r9, .L753+20
	ldrh	r2, [r9]
	ldrh	r3, [r9, #-24]
	cmp	r2, r3
	bls	.L700
	movw	r2, #1266
	ldr	r1, .L753+12
	ldr	r0, .L753+16
	bl	sftl_printk
.L700:
	ldrh	r2, [r9, #-24]
	ldrh	r1, [r9]
	ldr	ip, [r4, #3372]
	sub	r0, r2, #1
	sxth	r3, r0
	sub	r0, r0, r1
	str	r0, [fp, #-52]
.L701:
	ldr	r0, [fp, #-52]
	cmp	r3, r0
	ble	.L706
	lsl	lr, r3, #2
	ldr	r0, [r6, #4]
	str	lr, [fp, #-56]
	ldr	lr, [ip, r3, lsl #2]
	cmp	r0, lr
	bls	.L702
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
.L704:
	uxth	r0, r1
	sxth	r2, r1
	cmp	ip, r0
	bhi	.L705
	ldr	r1, [r6, #4]
	ldr	r2, [r7, #3372]
	ldr	r0, [fp, #-56]
	str	r1, [r2, r0]
	lsl	r2, r3, #1
	ldr	r1, [r7, #352]
	strh	r8, [r1, r2]	@ movhi
.L706:
	cmp	r3, #0
	blt	.L684
	ldr	r0, .L753+20
	ldrh	r2, [r0, #-24]
	ldrh	r1, [r0]
	sub	r2, r2, #1
	sub	r2, r2, r1
	sxth	r2, r2
	cmp	r3, r2
	bgt	.L684
	add	r1, r1, #1
	ldr	r2, [r4, #3372]
	strh	r1, [r0]	@ movhi
	ldr	r1, [r6, #4]
	str	r1, [r2, r3, lsl #2]
	lsl	r3, r3, #1
	ldr	r2, [r4, #352]
	b	.L749
.L705:
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
	b	.L704
.L702:
	sub	r3, r3, #1
	sxth	r3, r3
	b	.L701
.L685:
	ldr	r9, .L753+24
	movw	r2, #65535
	ldrh	r1, [r9]
	cmp	r1, r2
	strheq	r8, [r9]	@ movhi
	beq	.L750
	ldrh	r0, [r9, #4]
	cmp	r0, r2
	beq	.L709
	mov	r1, #1
	bl	FtlFreeSysBlkQueueIn
.L709:
	ldr	r3, [r6, #4]
	ldr	r2, [r4, #2636]
	cmp	r2, r3
	strhcs	r8, [r9, #4]	@ movhi
	bcs	.L684
	ldrh	r3, [r9]
	strh	r8, [r9]	@ movhi
	strh	r3, [r9, #4]	@ movhi
	ldr	r3, [r6, #4]
.L750:
	str	r3, [r4, #2636]
	b	.L684
.L713:
	ldr	r2, [r5, #3360]
	ldrh	r3, [r2]
	cmp	r3, #0
	beq	.L715
.L718:
	ldr	r0, [r4, #352]
	ldrh	r2, [r0]
	cmp	r2, #0
	beq	.L716
.L717:
	ldr	r3, .L753+8
	ldrh	r2, [r3]
	ldr	r3, [r4, #332]
	cmp	r2, r3
	bls	.L748
	movw	r2, #1391
	ldr	r1, .L753+12
	ldr	r0, .L753+16
	bl	sftl_printk
.L748:
	mov	r0, #0
	sub	sp, fp, #40
	ldmfd	sp, {r4, r5, r6, r7, r8, r9, r10, fp, sp, pc}
.L715:
	ldr	r1, .L753+8
	ldrh	r1, [r1]
	cmp	r1, #0
	ldrne	r1, [r5, #332]
	beq	.L718
.L719:
	sxth	ip, r3
	cmp	ip, r1
	bcs	.L718
	lsl	r0, ip, #1
	add	r3, r3, #1
	ldrh	r0, [r2, r0]
	cmp	r0, #0
	beq	.L719
	mov	r3, ip
	mov	lr, #0
.L720:
	ldr	r2, [r7, #332]
	cmp	r3, r2
	bcs	.L718
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
	b	.L720
.L716:
	ldr	r3, .L753+20
	ldrh	r1, [r3]
	cmp	r1, #0
	ldrhne	ip, [r3, #-24]!
	movne	r1, r3
	beq	.L717
.L725:
	sxth	r3, r2
	cmp	r3, ip
	mov	lr, r3
	bge	.L717
	lsl	r5, r3, #1
	add	r2, r2, #1
	ldrh	r5, [r0, r5]
	cmp	r5, #0
	beq	.L725
	mov	r5, #0
.L726:
	ldrh	r2, [r1]
	cmp	r3, r2
	bge	.L717
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
	b	.L726
.L754:
	.align	2
.L753:
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
	ldr	r4, .L787
	ldr	r8, .L787+4
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
.L756:
	ldrh	r3, [r8]
	sub	r3, r3, #16
	cmp	r5, r3
	ble	.L759
	lsl	r3, r5, #10
	mov	r2, #1
	mov	r1, r2
	mov	r0, r7
	str	r3, [r4, #3460]
	bl	FlashReadPages
	ldr	r3, [r4, #3456]
	cmn	r3, #1
	bne	.L757
	ldr	r3, [r4, #3460]
	mov	r2, #1
	mov	r1, r2
	mov	r0, r7
	add	r3, r3, #1
	str	r3, [r4, #3460]
	bl	FlashReadPages
.L757:
	ldr	r3, [r4, #3456]
	cmn	r3, #1
	beq	.L758
	ldrh	r2, [r6]
	movw	r3, #61649
	cmp	r2, r3
	bne	.L758
	ldr	r2, [r6, #4]
	ldr	r3, .L787+8
	str	r2, [r4, #364]
	ldrh	r2, [r6, #8]
	strh	r5, [r3]	@ movhi
	strh	r2, [r3, #4]	@ movhi
.L759:
	ldr	r5, .L787+8
	movw	r2, #65535
	ldrh	r3, [r5]
	cmp	r3, r2
	beq	.L773
	ldrh	r3, [r5, #4]
	cmp	r3, r2
	beq	.L763
	lsl	r3, r3, #10
	mov	r2, #1
	mov	r1, r2
	ldr	r0, .L787+12
	str	r3, [r4, #3460]
	bl	FlashReadPages
	ldr	r3, [r4, #3456]
	cmn	r3, #1
	beq	.L763
	ldrh	r2, [r6]
	movw	r3, #61649
	cmp	r2, r3
	bne	.L763
	ldr	r3, [r6, #4]
	ldr	r2, [r4, #364]
	cmp	r3, r2
	ldrhhi	r2, [r5, #4]
	strhi	r3, [r4, #364]
	ldrhhi	r3, [r6, #8]
	strhhi	r2, [r5]	@ movhi
	strhhi	r3, [r5, #4]	@ movhi
.L763:
	ldr	r8, .L787+12
	mov	r1, #1
	ldrh	r0, [r5]
	bl	FtlGetLastWrittenPage
	sxth	r7, r0
	add	r0, r0, #1
	strh	r0, [r5, #2]	@ movhi
.L765:
	cmp	r7, #0
	bge	.L768
	mov	r2, #251
	ldr	r1, .L787+16
	ldr	r0, .L787+20
	bl	sftl_printk
.L767:
	ldrh	r3, [r6, #10]
	ldrh	r0, [r6, #12]
	strh	r3, [r5, #6]	@ movhi
	movw	r3, #65535
	cmp	r0, r3
	beq	.L770
	ldr	r2, [r4, #232]
	cmp	r0, r2
	beq	.L770
	ldrh	r3, [r4, #246]
	lsr	r3, r3, #2
	cmp	r0, r3
	cmpcc	r2, r3
	bcs	.L770
	bl	FtlSysBlkNumInit
.L770:
	ldr	r6, .L787+24
	mov	r5, #0
	ldr	r8, .L787+28
	sub	r7, r6, #122
.L771:
	ldrh	r3, [r7]
	cmp	r5, r3
	bcc	.L772
	mov	r0, #0
	ldmfd	sp, {r4, r5, r6, r7, r8, fp, sp, pc}
.L758:
	sub	r5, r5, #1
	uxth	r5, r5
	b	.L756
.L768:
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
	beq	.L766
	ldrh	r2, [r6]
	movw	r3, #61649
	cmp	r2, r3
	beq	.L767
.L766:
	sub	r7, r7, #1
	sxth	r7, r7
	b	.L765
.L772:
	ldrh	r2, [r8]
	ldr	r1, [r4, #3464]
	ldr	r0, [r6, #4]!
	lsl	r2, r2, #2
	mla	r1, r5, r2, r1
	add	r5, r5, #1
	bl	ftl_memcpy
	b	.L771
.L773:
	mvn	r0, #0
	ldmfd	sp, {r4, r5, r6, r7, r8, fp, sp, pc}
.L788:
	.align	2
.L787:
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
	ldr	r6, .L828
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
.L790:
	ldr	r3, [fp, #-76]
	cmp	r8, r3
	bne	.L804
	ldr	r3, [fp, #-88]
	cmp	r3, #0
	beq	.L798
	mov	r5, #0
	ldr	r6, .L828+4
	ldr	r7, .L828+8
	b	.L805
.L804:
	ldr	r3, [r5, #8]
	cmp	r3, #0
	beq	.L791
	ldr	r3, [r5, #12]
	cmp	r3, #0
	bne	.L792
.L791:
	mov	r2, #148
	ldr	r1, .L828+12
	ldr	r0, .L828+16
	bl	sftl_printk
.L792:
	sub	r2, fp, #68
	sub	r1, fp, #64
	mov	r0, r5
	bl	l2p_addr_tran
	ldr	r7, [fp, #-68]
	cmp	r7, #3
	bls	.L793
.L827:
	mvn	r3, #0
	str	r3, [r5]
	b	.L794
.L793:
	cmp	r7, #0
	bne	.L795
	ldr	r3, [fp, #-64]
	ldr	r2, [fp, #-80]
	cmp	r2, r3
	bls	.L795
	ldr	r5, .L828+12
	ldr	r6, .L828+20
	b	.L826
.L797:
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
	ldr	r0, .L828+24
	bl	rknand_print_hex
	mov	r3, #4
	ldr	r1, [r4, #-8]
	mov	r2, r3
	ldr	r0, .L828+28
	bl	rknand_print_hex
.L826:
	ldr	r3, [fp, #-76]
	add	r4, r4, #20
	cmp	r7, r3
	bne	.L797
	bl	dump_stack
.L798:
	mov	r0, #0
	sub	sp, fp, #40
	ldmfd	sp, {r4, r5, r6, r7, r8, r9, r10, fp, sp, pc}
.L795:
	ldr	r1, [r5, #8]
	tst	r1, #63
	moveq	r7, r1
	beq	.L799
	ldr	r7, [r10, #3324]
	cmp	r1, r7
	beq	.L799
	ldr	r3, .L828+32
	mov	r0, r7
	ldrh	r2, [r3]
	lsl	r2, r2, #9
	bl	ftl_memcpy
.L799:
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
	bne	.L794
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
	bne	.L827
.L794:
	add	r8, r8, #1
	add	r5, r5, #20
	b	.L790
.L810:
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
	bne	.L806
	ldr	r1, [r4, #4]
	mov	r0, r6
	bl	sftl_printk
	str	r10, [r4]
.L806:
	ldr	r10, [fp, #-60]
	cmp	r10, #256
	bne	.L807
	ldr	r1, [r4, #4]
	mov	r0, r7
	bl	sftl_printk
	str	r10, [r4]
.L807:
	ldr	r3, [r4, #12]
	cmp	r3, #0
	beq	.L808
	ldr	r2, [r3]
	ldr	r3, .L828
	ldr	r3, [r3, #3332]
	ldr	r3, [r3]
	cmp	r2, r3
	beq	.L808
	ldr	r1, [r4, #4]
	ldr	r0, .L828+36
	bl	sftl_printk
	mvn	r3, #0
	str	r3, [r4]
.L808:
	ldr	r3, [r4, #8]
	cmp	r3, #0
	beq	.L809
	ldr	r2, [r3]
	ldr	r3, .L828
	ldr	r3, [r3, #3324]
	ldr	r3, [r3]
	cmp	r2, r3
	beq	.L809
	ldr	r1, [r4, #4]
	ldr	r0, .L828+40
	bl	sftl_printk
	mvn	r3, #0
	str	r3, [r4]
.L809:
	add	r5, r5, #1
	add	r4, r4, #20
.L805:
	cmp	r8, r5
	bne	.L810
	b	.L798
.L829:
	.align	2
.L828:
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
	ldr	ip, .L872
	uxtb	r5, r1
	uxth	r3, r0
	mov	r6, r10
	mov	r4, r10
	mov	r9, ip
	mov	r8, #20
	str	r3, [fp, #-44]
	str	r3, [ip, #3288]
.L831:
	ldrh	r1, [r9, #236]
	uxth	r3, r10
	cmp	r1, r3
	bhi	.L835
	cmp	r6, #0
	beq	.L830
	mov	r7, #0
	mov	r8, #20
	mov	r2, r6
	mov	r1, #0
	ldr	r0, [r9, #3272]
	bl	FlashEraseBlocks
.L838:
	uxth	r3, r7
	cmp	r6, r3
	bhi	.L840
.L841:
	cmp	r5, #0
	mov	r7, #0
	ldrne	r3, .L872+4
	moveq	r3, #2
	streq	r5, [fp, #-48]
	streq	r3, [fp, #-52]
	ldrhne	r3, [r3]
	strne	r3, [fp, #-52]
	movne	r3, #1
	strne	r3, [fp, #-48]
.L850:
	mov	r10, #0
	mov	r6, r10
.L842:
	ldrh	r1, [r9, #236]
	uxth	r3, r10
	cmp	r1, r3
	bhi	.L845
	cmp	r6, #0
	beq	.L830
	mov	r8, #0
	mov	r10, #20
	mov	r3, #1
	ldr	r2, [fp, #-48]
	mov	r1, r6
	ldr	r0, [r9, #3272]
	bl	FlashProgPages
.L847:
	uxth	r3, r8
	cmp	r6, r3
	bhi	.L849
	add	r7, r7, #1
	ldr	r2, [fp, #-52]
	uxth	r3, r7
	cmp	r2, r3
	bhi	.L850
	mov	r7, #0
	mov	r8, #20
.L851:
	uxth	r3, r7
	cmp	r6, r3
	bhi	.L853
	ldr	r3, [fp, #-44]
	adds	r5, r5, #0
	movne	r5, #1
	cmp	r3, #63
	orrls	r5, r5, #1
	cmp	r5, #0
	beq	.L830
	mov	r2, r6
	ldr	r1, [fp, #-48]
	ldr	r0, [r9, #3272]
	bl	FlashEraseBlocks
.L830:
	mov	r0, r4
	sub	sp, fp, #40
	ldmfd	sp, {r4, r5, r6, r7, r8, r9, r10, fp, sp, pc}
.L835:
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
	beq	.L832
	bl	IsBlkInVendorPart
	cmp	r0, #0
	bne	.L833
.L832:
	mov	r0, r7
	bl	FtlBbmIsBadBlock
	cmp	r0, #0
	addne	r4, r4, #1
	uxthne	r4, r4
	bne	.L833
	ldr	r1, [r9, #3272]
	lsl	r7, r7, #10
	ldr	r3, [r9, #3320]
	mla	r1, r8, r6, r1
	str	r3, [r1, #8]
	ldr	r3, .L872+8
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
.L833:
	add	r10, r10, #1
	b	.L831
.L840:
	mul	r3, r8, r7
	ldr	r2, [r9, #3272]
	add	r1, r2, r3
	ldr	r3, [r2, r3]
	cmn	r3, #1
	bne	.L839
	ldr	r0, [r1, #4]
	add	r4, r4, #1
	uxth	r4, r4
	ubfx	r0, r0, #10, #16
	bl	FtlBbmMapBadBlock
.L839:
	add	r7, r7, #1
	b	.L838
.L845:
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
	beq	.L843
	bl	IsBlkInVendorPart
	cmp	r0, #0
	bne	.L844
.L843:
	mov	r0, r8
	bl	FtlBbmIsBadBlock
	cmp	r0, #0
	bne	.L844
	ldr	r1, [r9, #3272]
	mov	r3, #20
	add	r8, r7, r8, lsl #10
	mla	r1, r3, r6, r1
	ldr	r3, [r9, #3316]
	str	r3, [r1, #8]
	ldr	r3, .L872+8
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
.L844:
	add	r10, r10, #1
	b	.L842
.L849:
	mul	r3, r10, r8
	ldr	r2, [r9, #3272]
	add	r1, r2, r3
	ldr	r3, [r2, r3]
	cmp	r3, #0
	beq	.L848
	ldr	r0, [r1, #4]
	add	r4, r4, #1
	uxth	r4, r4
	ubfx	r0, r0, #10, #16
	bl	FtlBbmMapBadBlock
.L848:
	add	r8, r8, #1
	b	.L847
.L853:
	cmp	r5, #0
	beq	.L852
	mul	r3, r8, r7
	ldr	r2, [r9, #3272]
	add	r1, r2, r3
	ldr	r3, [r2, r3]
	cmp	r3, #0
	bne	.L852
	ldr	r0, [r1, #4]
	mov	r1, #1
	ubfx	r0, r0, #10, #16
	bl	FtlFreeSysBlkQueueIn
.L852:
	add	r7, r7, #1
	b	.L851
.L873:
	.align	2
.L872:
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
	bls	.L874
	ldr	r4, .L879
	sub	r0, fp, #84
	mov	r2, #32
	mov	r1, #165
	str	r0, [fp, #-92]
	lsl	r5, r5, #10
	ldr	r3, [r4, #3328]
	str	r3, [fp, #-96]
	bl	ftl_memset
	mov	r2, #8
	mov	r1, #90
	ldr	r0, [r4, #3328]
	bl	ftl_memset
	mov	r2, #1
	sub	r0, fp, #104
	mov	r1, r2
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
	bne	.L876
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
.L876:
	mov	r2, #1
	mov	r1, #0
	sub	r0, fp, #104
	str	r5, [fp, #-100]
	bl	FlashEraseBlocks
.L874:
	mov	r0, r4
	sub	sp, fp, #20
	ldmfd	sp, {r4, r5, fp, sp, pc}
.L880:
	.align	2
.L879:
	.word	.LANCHOR0
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
	ldr	r4, .L893
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
.L882:
	ldrh	r3, [r7]
	cmp	r5, r3
	blt	.L883
	ldr	r6, [r4, #3468]
	mov	r2, #16
	mov	r1, #255
	ldr	r5, .L893+4
	ldr	r10, .L893+8
	mov	r7, #0
	mov	r0, r6
	mov	r8, r7
	bl	ftl_memset
	ldr	r3, .L893+12
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
.L884:
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
	blt	.L885
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
.L885:
	mov	r3, #1
	ldr	r0, .L893+16
	mov	r2, r3
	mov	r1, r3
	bl	FlashProgPages
	ldrh	r3, [r5, #2]
	add	r3, r3, #1
	strh	r3, [r5, #2]	@ movhi
	ldr	r3, [r4, #3456]
	cmn	r3, #1
	bne	.L886
	add	r7, r7, #1
	ldr	r1, [r4, #3460]
	uxth	r7, r7
	ldr	r0, .L893+20
	bl	sftl_printk
	cmp	r7, #3
	bls	.L884
	mov	r2, r7
	ldr	r1, [r4, #3460]
	ldr	r0, .L893+24
	bl	sftl_printk
.L888:
	b	.L888
.L883:
	ldrh	r2, [r8]
	ldr	r3, [r4, #3464]
	ldr	r1, [r6, #4]!
	mul	r0, r2, r5
	lsl	r2, r2, #2
	add	r5, r5, #1
	add	r0, r3, r0, lsl #2
	bl	ftl_memcpy
	b	.L882
.L889:
	mov	r8, #1
	b	.L884
.L886:
	add	r8, r8, #1
	cmp	r8, #1
	ble	.L889
	cmp	r3, #256
	beq	.L884
	mov	r0, #0
	sub	sp, fp, #40
	ldmfd	sp, {r4, r5, r6, r7, r8, r9, r10, fp, sp, pc}
.L894:
	.align	2
.L893:
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
	ldr	r9, .L931
.L896:
	ldr	r3, .L931
	ldr	r10, .L931+4
	ldrh	r2, [r3, #228]
	ldrh	r1, [r10]
	add	r2, r2, r1
	ldrh	r1, [r3, #244]
	cmp	r2, r1
	ble	.L897
	mov	r2, #2656
	ldr	r1, .L931+8
	ldr	r0, .L931+12
	bl	sftl_printk
.L897:
	ldr	r2, .L931+16
	cmp	r4, r2
	movne	r1, #0
	bne	.L898
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
.L898:
	ldr	r0, .L931+20
	bl	List_pop_index_node
	ldrh	r2, [r9, #228]
	mov	r7, r0
	uxth	r8, r0
	cmp	r2, #0
	bne	.L899
	movw	r2, #2665
	ldr	r1, .L931+8
	ldr	r0, .L931+12
	bl	sftl_printk
.L899:
	ldrh	r2, [r9, #228]
	sub	r2, r2, #1
	strh	r2, [r9, #228]	@ movhi
	ldrh	r2, [r9, #244]
	cmp	r2, r8
	bls	.L896
	uxth	r7, r7
	ldr	r2, [r9, #76]
	lsl	r6, r7, #1
	ldrh	r5, [r2, r6]
	cmp	r5, #0
	bne	.L896
	strh	r8, [r4]	@ movhi
	mov	r0, r4
	bl	make_superblock
	ldrb	r2, [r4, #7]	@ zero_extendqisi2
	cmp	r2, #0
	bne	.L901
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
	ble	.L896
	movw	r2, #2679
	ldr	r1, .L931+8
	ldr	r0, .L931+12
	bl	sftl_printk
	b	.L896
.L901:
	ldrh	r1, [r10]
	ldrh	r2, [r9, #228]
	add	r2, r2, r1
	ldrh	r1, [r9, #244]
	cmp	r2, r1
	ble	.L903
	movw	r2, #2682
	ldr	r1, .L931+8
	ldr	r0, .L931+12
	bl	sftl_printk
.L903:
	ldr	r0, [r9, #3272]
	mov	r1, #20
	ldrh	lr, [r9, #236]
	add	ip, r4, #16
	str	r1, [fp, #-44]
	mov	r2, r0
	mla	r3, r1, lr, r0
	mov	lr, #0
.L904:
	cmp	r3, r2
	bne	.L906
	cmp	r5, #0
	bne	.L907
	movw	r2, #2693
	ldr	r1, .L931+8
	ldr	r0, .L931+12
	bl	sftl_printk
.L907:
	ldrh	r2, [r9, #176]
	cmp	r2, r8
	bne	.L908
	movw	r2, #2695
	ldr	r1, .L931+8
	ldr	r0, .L931+12
	bl	sftl_printk
.L908:
	ldrb	r2, [r4, #8]	@ zero_extendqisi2
	ldr	r1, [r9, #2540]
	ldr	r10, .L931+24
	cmp	r2, #0
	ldrh	r2, [r1, r6]
	bne	.L909
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
.L912:
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
	ldr	ip, .L931+28
	add	r2, r2, #1
	str	r2, [r1, #16]
	mov	r1, #20
	mla	r1, r1, r5, r0
	add	r2, r0, #4
	add	r1, r1, #24
.L914:
	add	r2, r2, #20
	cmp	r1, r2
	bne	.L915
	ldrb	r1, [r4, #8]	@ zero_extendqisi2
	mov	r2, r5
	ldr	r0, [r9, #3272]
	mov	r10, #0
	bl	FlashEraseBlocks
	mov	r1, r10
	mov	ip, #20
.L916:
	uxth	r2, r10
	cmp	r5, r2
	bhi	.L918
	cmp	r1, #0
	ble	.L919
	mov	r0, r7
	bl	update_multiplier_value
	bl	FtlBbmTblFlush
.L919:
	ldrb	r1, [r4, #7]	@ zero_extendqisi2
	cmp	r1, #0
	bne	.L920
	ldr	r2, [r9, #76]
	mvn	r1, #0
	strh	r1, [r2, r6]	@ movhi
	b	.L896
.L906:
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
	b	.L904
.L909:
	add	r2, r2, #1
	mov	r0, r7
	strh	r2, [r1, r6]	@ movhi
	ldr	r2, [r9, #2604]
	add	r2, r2, #1
	str	r2, [r9, #2604]
	bl	ftl_set_blk_mode.part.6
	b	.L912
.L915:
	ldr	r0, [r2, #-20]
	and	r0, r0, ip
	str	r0, [r2, #-20]
	b	.L914
.L918:
	mul	r2, ip, r10
	ldr	lr, [r9, #3272]
	add	r0, lr, r2
	ldr	r2, [lr, r2]
	cmn	r2, #1
	bne	.L917
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
.L917:
	add	r10, r10, #1
	b	.L916
.L920:
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
	beq	.L921
	ldrb	r3, [r4, #7]	@ zero_extendqisi2
	cmp	r3, #0
	bne	.L922
.L921:
	movw	r2, #2748
	ldr	r1, .L931+8
	ldr	r0, .L931+12
	bl	sftl_printk
.L922:
	mov	r0, #0
	sub	sp, fp, #40
	ldmfd	sp, {r4, r5, r6, r7, r8, r9, r10, fp, sp, pc}
.L932:
	.align	2
.L931:
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
	ldr	r6, .L945
	str	r3, [fp, #-44]
	movw	r3, #3210
	ldrh	r3, [r6, r3]
	cmp	r3, #0
	movne	r7, #0
	ldrne	r10, .L945+4
	bne	.L935
.L934:
	mov	r0, #0
	sub	sp, fp, #40
	ldmfd	sp, {r4, r5, r6, r7, r8, r9, r10, fp, sp, pc}
.L941:
	uxtah	r3, r6, r7
	ldr	r1, [fp, #-44]
	mov	r8, #0
	ldrb	r0, [r3, #264]	@ zero_extendqisi2
	bl	V2P_block
	mov	r9, r0
.L936:
	ldrh	r3, [r10]
	uxth	r4, r8
	cmp	r3, r4
	bhi	.L940
	add	r7, r7, #1
.L935:
	ldrh	r2, [r6, #236]
	uxth	r3, r7
	cmp	r2, r3
	bhi	.L941
	bl	FtlGcReFreshBadBlk
	b	.L934
.L940:
	uxth	r3, r8
	ldr	r5, .L945+8
	lsl	r2, r3, #1
	ldrh	r2, [r5, r2]
	cmp	r2, r9
	bne	.L937
	mov	r1, r9
	ldr	r0, .L945+12
	str	r3, [fp, #-48]
	bl	sftl_printk
	mov	r0, r9
	bl	FtlBbmMapBadBlock
	bl	FtlBbmTblFlush
	ldr	r3, [fp, #-48]
	ldrh	r2, [r10]
	add	r3, r5, r3, lsl #1
.L938:
	cmp	r4, r2
	bcc	.L939
	sub	r2, r2, #1
	strh	r2, [r10]	@ movhi
.L937:
	add	r8, r8, #1
	b	.L936
.L939:
	ldrh	r1, [r3, #2]!
	add	r4, r4, #1
	uxth	r4, r4
	strh	r1, [r3, #-2]	@ movhi
	b	.L938
.L946:
	.align	2
.L945:
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
	ldr	r4, .L959
	uxth	r5, r0
	ldr	r2, [r4, #76]
	lsl	r3, r5, #1
	ldrh	r3, [r2, r3]
	cmp	r3, #0
	bne	.L948
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
	beq	.L952
	ldrh	r3, [r4, #28]
	cmp	r3, r5
	beq	.L957
	ldrh	r3, [r4, #80]
	cmp	r3, r5
	beq	.L957
	ldrh	r3, [r4, #128]
	cmp	r3, r5
	beq	.L957
.L952:
	mov	r1, r5
	ldr	r0, .L959+4
	bl	List_remove_node
	movw	r3, #2532
	ldrh	r3, [r4, r3]
	cmp	r3, #0
	bne	.L954
	movw	r2, #2824
	ldr	r1, .L959+8
	ldr	r0, .L959+12
	bl	sftl_printk
.L954:
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
	ble	.L958
	movw	r2, #2827
	ldr	r1, .L959+8
	ldr	r0, .L959+12
	bl	sftl_printk
.L958:
	mov	r0, #1
	ldmfd	sp, {r4, r5, r6, fp, sp, pc}
.L948:
	mov	r0, r5
	bl	List_update_data_list
.L957:
	mov	r0, #0
	ldmfd	sp, {r4, r5, r6, fp, sp, pc}
.L960:
	.align	2
.L959:
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
	ldr	r4, .L973
	beq	.L962
	ldr	r3, [r4, #76]
	lsl	r5, r6, #1
	ldrh	r2, [r3, r5]
	cmp	r2, #0
	subne	r2, r2, #1
	strhne	r2, [r3, r5]	@ movhi
	bne	.L962
	mov	r1, r6
	ldr	r0, .L973+4
	bl	sftl_printk
	ldr	r3, [r4, #76]
	ldrh	r5, [r3, r5]
	cmp	r5, #0
	beq	.L964
.L971:
	mov	r5, #0
.L961:
	mov	r0, r5
	ldmfd	sp, {r4, r5, r6, r7, fp, sp, pc}
.L964:
	movw	r2, #2842
.L972:
	ldr	r1, .L973+8
	ldr	r0, .L973+12
	bl	sftl_printk
	b	.L961
.L962:
	movw	r7, #3442
	movw	r3, #65535
	ldrh	r0, [r4, r7]
	cmp	r0, r3
	strheq	r6, [r4, r7]	@ movhi
	beq	.L971
.L966:
	cmp	r6, r0
	beq	.L971
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
	beq	.L961
	movw	r2, #2858
	b	.L972
.L974:
	.align	2
.L973:
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
	ldr	r3, .L988
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
	ldr	r1, .L988+4
	ldr	r2, [r3, #3304]
	str	r1, [r2, #4]
	ldrh	r2, [r0, #4]
	tst	r2, #1
	moveq	r7, #6
	movne	r7, #7
.L981:
	ldrh	r3, [r4, #4]
	cmp	r3, #0
	bne	.L977
.L978:
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
.L977:
	mov	r0, r4
	bl	get_new_active_ppa
	cmn	r0, #1
	str	r0, [fp, #-48]
	beq	.L978
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
	bne	.L981
	b	.L978
.L989:
	.align	2
.L988:
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
	ldr	r6, .L1010
	bl	FtlBbtMemInit
	bl	FtlLoadFactoryBbt
	sub	r7, r6, #384
	sub	r9, r6, #18
	sub	r4, r6, #28
	mov	r5, r7
.L991:
	ldr	r3, .L1010+4
	ldrh	r2, [r3]
	cmp	r8, r2
	bcc	.L997
	mov	r0, #0
	add	r6, r3, #60
.L998:
	ldrh	r2, [r6]
	uxth	r3, r0
	add	r7, r0, #1
	cmp	r2, r3
	bhi	.L999
	ldrh	r6, [r4, #12]
	movw	r7, #65535
	sub	r6, r6, #1
	uxth	r6, r6
.L1000:
	ldrh	r3, [r4, #12]
	sub	r3, r3, #48
	cmp	r6, r3
	ble	.L1004
	mov	r0, r6
	bl	FtlBbmIsBadBlock
	cmp	r0, #1
	beq	.L1001
	mov	r0, r6
	bl	FlashTestBlk
	cmp	r0, #0
	beq	.L1002
	mov	r0, r6
	bl	FtlBbmMapBadBlock
.L1001:
	sub	r6, r6, #1
	uxth	r6, r6
	b	.L1000
.L997:
	ldr	r2, [r7, #3336]
	movw	r1, #65535
	ldr	r0, [r7, #3304]
	str	r2, [fp, #-48]
	str	r2, [r7, #3468]
	ldrh	r2, [r9, #2]!
	str	r0, [r7, #3464]
	cmp	r2, r1
	beq	.L992
	ldrh	r10, [r3, #44]
	ldr	r0, .L1010+8
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
.L993:
	uxth	r0, r10
	add	r8, r8, #1
	add	r6, r6, #4
	bl	FtlBbmMapBadBlock
	b	.L991
.L992:
	mov	r1, r8
	str	r3, [fp, #-44]
	bl	FlashGetBadBlockList
	ldr	r1, [r6]
	ldr	r0, [r7, #3464]
	bl	FtlBbt2Bitmap
	ldr	r3, [fp, #-44]
	ldrh	r3, [r3, #44]
.L1009:
	sub	r3, r3, #1
	uxth	r3, r3
	str	r3, [fp, #-44]
.L994:
	ldr	r10, .L1010+12
	ldr	r3, [fp, #-44]
	ldrh	r0, [r10]
	smlabb	r0, r0, r8, r3
	uxth	r0, r0
	bl	FtlBbmIsBadBlock
	cmp	r0, #1
	beq	.L995
	ldrh	r3, [fp, #-44]
	mov	r2, #16
	mov	r1, #0
	ldr	r0, [r5, #3336]
	strh	r3, [r9]	@ movhi
	bl	ftl_memset
	mov	r2, #4096
	mov	r1, #0
	ldr	r0, [r5, #3304]
	bl	ftl_memset
	ldr	r2, [fp, #-48]
	ldr	r3, .L1010+16
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
	ldr	r3, .L1010+20
	ldrh	r2, [r3]
	lsl	r2, r2, #2
	bl	ftl_memcpy
	mov	r2, #1
	ldr	r0, .L1010+8
	mov	r1, r2
	bl	FlashEraseBlocks
	mov	r3, #1
	ldr	r0, .L1010+8
	mov	r2, r3
	mov	r1, r3
	bl	FlashProgPages
	ldr	r3, [r5, #3456]
	cmn	r3, #1
	bne	.L993
	uxth	r0, r10
	bl	FtlBbmMapBadBlock
	b	.L994
.L995:
	ldr	r3, [fp, #-44]
	b	.L1009
.L999:
	uxth	r0, r0
	bl	FtlBbmMapBadBlock
	mov	r0, r7
	b	.L998
.L1002:
	ldrh	r3, [r4]
	cmp	r3, r7
	strheq	r6, [r4]	@ movhi
	beq	.L1001
.L1003:
	strh	r6, [r4, #4]	@ movhi
.L1004:
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
.L1011:
	.align	2
.L1010:
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
	ldr	r3, .L1016
	add	r1, r0, r1
.L1014:
	cmp	r0, r1
	bne	.L1015
	mov	r0, r3
	ldmfd	sp, {fp, sp, pc}
.L1015:
	lsr	r2, r3, #2
	ldrb	ip, [r0], #1	@ zero_extendqisi2
	add	r2, r2, r3, lsl #5
	add	r2, r2, ip
	eor	r3, r3, r2
	b	.L1014
.L1017:
	.align	2
.L1016:
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
	bne	.L1019
	ldrh	r3, [r0, #8]
	cmp	r3, #0
	beq	.L1020
	movw	r2, #641
	ldr	r1, .L1028
	ldr	r0, .L1028+4
	bl	sftl_printk
.L1020:
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
.L1021:
	mov	r0, #0
	ldmfd	sp, {r4, r5, r6, r7, r8, fp, sp, pc}
.L1019:
	lsl	r3, r3, #1
	ldr	r7, [r0, #24]
	mov	r1, #255
	ldrh	r8, [r5, r3]
	ldrh	r3, [r0, #2]
	ldr	r5, .L1028+8
	orr	r3, r3, r8, lsl #10
	ldr	r6, [r5, #3336]
	str	r3, [r5, #3460]
	ldr	r3, [r5, #3304]
	str	r6, [r5, #3468]
	str	r3, [r5, #3464]
	ldr	r3, [r0, #28]
	str	r3, [r6, #4]
	ldr	r3, .L1028+12
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
.L1022:
	ldrh	r0, [r4, #6]
	uxth	r1, r2
	cmp	r0, r1
	bhi	.L1024
	movw	r3, #314
	ldr	r0, [r5, #3464]
	ldrh	r1, [r5, r3]
	bl	js_hash
	mov	r2, #1
	str	r0, [r6, #12]
	mov	r3, #0
	mov	r1, r2
	ldr	r0, .L1028+16
	bl	FlashProgPages
	ldrh	r3, [r4, #2]
	mov	r0, r4
	add	r3, r3, #1
	strh	r3, [r4, #2]	@ movhi
	bl	ftl_map_blk_gc
	b	.L1021
.L1024:
	uxth	r1, r2
	ldr	r0, [r7, r1, lsl #2]
	cmp	r8, r0, lsr #10
	bne	.L1023
	ldr	r0, [r5, #3304]
	add	r3, r3, #1
	uxth	r3, r3
	str	r1, [r0, r3, lsl #3]
	ldr	r0, [r7, r1, lsl #2]
	ldr	r1, [r5, #3304]
	add	r1, r1, r3, lsl #3
	str	r0, [r1, #4]
.L1023:
	add	r2, r2, #1
	b	.L1022
.L1029:
	.align	2
.L1028:
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
	ldr	r5, .L1048
	mov	r4, r0
	mov	r7, r1
	mov	r9, r2
	add	r8, r5, #308
.L1031:
	ldr	r3, [r5, #2576]
	add	r3, r3, #1
	str	r3, [r5, #2576]
	ldrh	r3, [r8]
	ldrh	r2, [r4, #2]
	sub	r3, r3, #1
	cmp	r2, r3
	bge	.L1032
	ldrh	r2, [r4]
	movw	r3, #65535
	cmp	r2, r3
	bne	.L1033
.L1032:
	mov	r0, r4
	bl	Ftl_write_map_blk_to_last_page
.L1033:
	ldrh	r3, [r4]
	ldr	r2, [r4, #12]
	lsl	r3, r3, #1
	ldrh	r3, [r2, r3]
	cmp	r3, #0
	bne	.L1034
	mov	r2, #700
	ldr	r1, .L1048+4
	ldr	r0, .L1048+8
	bl	sftl_printk
.L1034:
	ldrh	r2, [r4]
	ldrh	r3, [r4, #10]
	cmp	r2, r3
	bcc	.L1035
	movw	r2, #701
	ldr	r1, .L1048+4
	ldr	r0, .L1048+8
	bl	sftl_printk
.L1035:
	ldrh	r3, [r4]
	mov	r1, #0
	ldr	r2, [r4, #12]
	ldr	r0, [r5, #3336]
	lsl	r3, r3, #1
	ldrh	ip, [r2, r3]
	mov	r2, #16
	ldrh	r3, [r4, #2]
	str	r9, [r5, #3464]
	str	ip, [fp, #-44]
	str	r0, [r5, #3468]
	orr	r3, r3, ip, lsl #10
	str	r3, [r5, #3460]
	bl	ftl_memset
	ldr	r3, [r4, #28]
	ldr	r10, [r5, #3468]
	ldr	ip, [fp, #-44]
	str	r3, [r10, #4]
	strh	r7, [r10, #8]	@ movhi
	ldrh	r3, [r4, #4]
	strh	ip, [r10, #2]	@ movhi
	strh	r3, [r10]	@ movhi
	ldr	r3, .L1048+12
	ldr	r0, [r5, #3464]
	ldrh	r1, [r3]
	bl	js_hash
	mov	r3, #1
	str	r0, [r10, #12]
	mov	r2, r3
	mov	r1, r3
	ldr	r0, .L1048+16
	bl	FlashProgPages
	ldrh	r3, [r4, #2]
	add	r3, r3, #1
	uxth	r3, r3
	strh	r3, [r4, #2]	@ movhi
	ldr	r2, [r5, #3456]
	cmn	r2, #1
	bne	.L1036
	ldr	r1, [r5, #3460]
	add	r6, r6, #1
	ldr	r0, .L1048+20
	uxth	r6, r6
	bl	sftl_printk
	ldrh	r3, [r4, #2]
	cmp	r3, #2
	ldrhls	r3, [r8]
	subls	r3, r3, #1
	strhls	r3, [r4, #2]	@ movhi
	cmp	r6, #3
	bls	.L1038
	mov	r2, r6
	ldr	r1, [r5, #3460]
	ldr	r0, .L1048+24
	bl	sftl_printk
.L1039:
	b	.L1039
.L1038:
	ldr	r3, [r4, #32]
	cmp	r3, #0
	beq	.L1031
.L1047:
	b	.L1047
.L1036:
	cmp	r3, #1
	cmpne	r2, #256
	beq	.L1042
	ldr	r0, [r4, #36]
	cmp	r0, #0
	beq	.L1043
.L1042:
	mov	r3, #0
	str	r3, [r4, #36]
	b	.L1031
.L1043:
	ldr	r2, [r5, #3460]
	ldr	r3, [r4, #24]
	str	r2, [r3, r7, lsl #2]
	ldmib	sp, {r4, r5, r6, r7, r8, r9, r10, fp, sp, pc}
.L1049:
	.align	2
.L1048:
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
	ldr	r9, .L1057
	uxth	r6, r0
	uxth	r10, r1
	ldrh	r3, [r9]
	cmp	r3, r6
	bcs	.L1051
	movw	r2, #485
	ldr	r1, .L1057+4
	ldr	r0, .L1057+8
	bl	sftl_printk
.L1051:
	ldr	r7, .L1057+12
	mov	r5, #12
	ldr	r3, [r7, #3380]
	mov	r4, r7
	ldr	r8, [r3, r6, lsl #2]
	cmp	r8, #0
	bne	.L1052
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
.L1053:
	mov	r0, #0
	ldmfd	sp, {r4, r5, r6, r7, r8, r9, r10, fp, sp, pc}
.L1052:
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
	beq	.L1054
	mov	r2, r8
	mov	r1, r6
	ldr	r0, .L1057+16
	bl	sftl_printk
	mov	r3, #4
	ldr	r1, [r4, #3468]
	mov	r2, r3
	ldr	r0, .L1057+20
	bl	rknand_print_hex
	ldrh	r3, [r9]
	mov	r2, #4
	ldr	r1, [r4, #3380]
	ldr	r0, .L1057+24
	bl	rknand_print_hex
.L1055:
	ldrh	r3, [r7, #8]
	cmp	r3, r6
	beq	.L1056
	mov	r2, #508
	ldr	r1, .L1057+4
	ldr	r0, .L1057+8
	bl	sftl_printk
.L1056:
	ldr	r3, [r4, #2544]
	mov	r1, #0
	add	r2, r3, r5
	str	r1, [r2, #4]
	strh	r6, [r3, r5]	@ movhi
	b	.L1053
.L1054:
	ldr	r3, [r4, #3456]
	cmp	r3, #256
	bne	.L1055
	mov	r2, r8
	mov	r1, r6
	ldr	r0, .L1057+28
	bl	sftl_printk
	ldr	r3, [r4, #2544]
	mov	r1, r6
	ldr	r0, .L1057+32
	add	r3, r3, r5
	ldr	r2, [r3, #8]
	bl	FtlMapWritePage
	b	.L1055
.L1058:
	.align	2
.L1057:
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
	blt	.L1060
	uxth	r0, r0
	lsl	r0, r0, #1
	ldrh	r8, [r5, r0]
	cmp	r8, #0
	beq	.L1060
	ldr	r3, [r4, #32]
	cmp	r3, #0
	bne	.L1060
	mov	r2, #1
	str	r2, [r4, #32]
	strh	r3, [r5, r0]	@ movhi
	ldrh	r3, [r4, #8]
	ldrh	r2, [r4, #2]
	sub	r3, r3, #1
	strh	r3, [r4, #8]	@ movhi
	ldr	r3, .L1075
	ldrh	r3, [r3]
	cmp	r2, r3
	bcc	.L1061
	mov	r0, r4
	bl	ftl_map_blk_alloc_new_blk
.L1061:
	ldr	r5, .L1075+4
	mov	r6, #0
.L1062:
	ldrh	r3, [r4, #6]
	uxth	r10, r6
	cmp	r3, r10
	bhi	.L1069
	mov	r1, #1
	mov	r0, r8
	bl	FtlFreeSysBlkQueueIn
	mov	r3, #0
	str	r3, [r4, #32]
.L1060:
	ldr	r3, .L1075
	ldrh	r2, [r4, #2]
	ldrh	r3, [r3]
	cmp	r2, r3
	bcc	.L1070
	mov	r0, r4
	bl	ftl_map_blk_alloc_new_blk
.L1070:
	mov	r0, #0
	sub	sp, fp, #40
	ldmfd	sp, {r4, r5, r6, r7, r8, r9, r10, fp, sp, pc}
.L1069:
	uxth	r7, r6
	add	r3, r9, r7, lsl #2
	str	r3, [fp, #-44]
	ldr	r3, [r9, r7, lsl #2]
	cmp	r8, r3, lsr #10
	bne	.L1063
	ldr	r3, [r5, #3308]
	ldr	r0, .L1075+8
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
	beq	.L1064
	movw	r2, #611
	ldr	r1, .L1075+12
	ldr	r0, .L1075+16
	bl	sftl_printk
	ldr	r3, [fp, #-48]
.L1064:
	ldr	r2, [r5, #3456]
	cmn	r2, #1
	bne	.L1065
.L1067:
	ldr	r2, [fp, #-44]
	mov	r3, #0
	str	r3, [r2]
.L1066:
	b	.L1066
.L1065:
	ldrh	r2, [r3, #8]
	cmp	r2, r10
	bne	.L1067
	ldrh	r2, [r3]
	ldrh	r3, [r4, #4]
	cmp	r2, r3
	bne	.L1067
	ldr	r2, [r5, #3464]
	mov	r1, r7
	mov	r0, r4
	bl	FtlMapWritePage
.L1063:
	add	r6, r6, #1
	b	.L1062
.L1076:
	.align	2
.L1075:
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
	ldr	r5, .L1078
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
.L1079:
	.align	2
.L1078:
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
	ldr	r4, .L1093
	mov	r7, r1
	mov	r10, r2
	add	r3, r4, #312
	ldrh	r5, [r3]
	ldr	r3, [r4, #2556]
	cmp	r0, r3
	bcc	.L1081
	movw	r2, #813
	ldr	r1, .L1093+4
	ldr	r0, .L1093+8
	bl	sftl_printk
.L1081:
	ldr	r3, [r4, #2556]
	cmp	r6, r3
	bcs	.L1082
	add	r5, r5, #7
	ldr	ip, [r4, #2544]
	lsr	r3, r6, r5
	mov	r1, #0
	mov	r2, #12
	str	r3, [fp, #-44]
	movw	r3, #342
	ldrh	r9, [fp, #-44]
	ldrh	r0, [r4, r3]
.L1083:
	uxth	r8, r1
	cmp	r8, r0
	bcc	.L1088
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
	beq	.L1089
	ldr	r2, [ip, #4]
	cmp	r2, #0
	bge	.L1089
	bl	flush_l2p_region
.L1089:
	mov	r1, r8
	ldrh	r0, [fp, #-44]
	bl	load_l2p_region
	b	.L1085
.L1082:
	cmp	r10, #0
	mvn	r0, #0
	streq	r0, [r7]
.L1080:
	sub	sp, fp, #40
	ldmfd	sp, {r4, r5, r6, r7, r8, r9, r10, fp, sp, pc}
.L1088:
	add	r1, r1, #1
	mla	lr, r2, r1, ip
	ldrh	lr, [lr, #-12]
	cmp	lr, r9
	bne	.L1083
.L1085:
	mvn	r2, #0
	cmp	r10, #0
	bic	r5, r6, r2, lsl r5
	mov	r2, #12
	uxth	r5, r5
	bne	.L1086
	ldr	r1, [r4, #2544]
	mla	r2, r2, r8, r1
	ldr	r2, [r2, #8]
	ldr	r2, [r2, r5, lsl #2]
	str	r2, [r7]
.L1087:
	ldr	r1, [r4, #2544]
	mov	r2, #12
	mov	r0, #0
	mla	r3, r2, r8, r1
	ldr	r2, [r3, #4]
	cmn	r2, #1
	addne	r2, r2, #1
	strne	r2, [r3, #4]
	b	.L1080
.L1086:
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
	b	.L1087
.L1094:
	.align	2
.L1093:
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
	ldr	r4, .L1113
	ldrh	r2, [r4, #32]
	cmp	r2, #0
	beq	.L1096
	ldrb	r3, [r4, #36]	@ zero_extendqisi2
	cmp	r3, #0
	bne	.L1096
	movw	r3, #306
	ldrb	r1, [r4, #35]	@ zero_extendqisi2
	ldrh	r3, [r4, r3]
	mul	r3, r3, r1
	cmp	r2, r3
	beq	.L1096
	ldrb	r7, [r4, #38]	@ zero_extendqisi2
	cmp	r7, #0
	bne	.L1095
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
	beq	.L1098
	mov	r2, r7
	mov	r1, #1
	sub	r0, fp, #60
	bl	FlashReadPages
.L1099:
	ldr	r10, .L1113+4
	mov	r7, #0
	ldr	r3, .L1113+8
	lsl	r8, r8, #2
	mov	r9, r7
	strh	r3, [r5]	@ movhi
.L1100:
	cmp	r8, r7
	bne	.L1104
.L1101:
	mov	r3, #1
.L1112:
	strb	r3, [r4, #38]
.L1095:
	sub	sp, fp, #40
	ldmfd	sp, {r4, r5, r6, r7, r8, r9, r10, fp, sp, pc}
.L1098:
	movw	r3, #314
	mov	r1, #255
	ldrh	r2, [r4, r3]
	bl	ftl_memset
	b	.L1099
.L1104:
	ldrh	r3, [r4, #32]
	cmp	r3, #0
	beq	.L1101
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
	b	.L1100
.L1096:
	mov	r3, #0
	b	.L1112
.L1114:
	.align	2
.L1113:
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
	ldr	r7, .L1125
	ubfx	r0, r1, #10, #16
	str	r1, [fp, #-32]
	bl	P2V_block_in_plane
	ldr	r2, [r7, #76]
	lsl	r5, r0, #1
	ldrh	r3, [r2, r5]
	cmp	r3, #0
	bne	.L1116
	ldr	r4, [r7, #2536]
	cmp	r4, #0
	beq	.L1117
	ldr	r2, [r7, #2520]
	movw	lr, #65535
	ldrh	ip, [r7, #228]
	sub	r4, r4, r2
	ubfx	r4, r4, #3, #16
.L1118:
	uxth	r1, r3
	cmp	ip, r1
	bls	.L1117
	cmp	r4, r0
	bne	.L1119
	mov	r1, r4
	ldr	r0, .L1125+4
	bl	List_remove_node
	ldrh	r3, [r7, #228]
	cmp	r3, #0
	bne	.L1120
	movw	r2, #1733
	ldr	r1, .L1125+8
	ldr	r0, .L1125+12
	bl	sftl_printk
.L1120:
	ldrh	r3, [r7, #228]
	mov	r0, r4
	sub	r3, r3, #1
	strh	r3, [r7, #228]	@ movhi
	bl	INSERT_DATA_LIST
	ldr	r2, [r7, #76]
	ldrh	r3, [r2, r5]
.L1116:
	add	r3, r3, #1
	strh	r3, [r2, r5]	@ movhi
	b	.L1117
.L1119:
	lsl	r4, r4, #3
	add	r3, r3, #1
	ldrh	r4, [r2, r4]
	cmp	r4, lr
	bne	.L1118
.L1117:
	mov	r2, #1
	sub	r1, fp, #32
	mov	r0, r6
	bl	log2phys
	ldmib	sp, {r4, r5, r6, r7, fp, sp, pc}
.L1126:
	.align	2
.L1125:
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
	beq	.L1251
	ldrh	r3, [r0, #2]
	ldr	r5, .L1259
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
	bne	.L1131
.L1257:
	strb	r3, [r10, #6]
.L1251:
	mov	r0, #0
	sub	sp, fp, #40
	ldmfd	sp, {r4, r5, r6, r7, r8, r9, r10, fp, sp, pc}
.L1132:
	uxth	r1, r3
	add	r1, r10, r1, lsl #1
	ldrh	r0, [r1, #16]
.L1131:
	cmp	r0, r2
	uxth	r6, r3
	add	r3, r3, #1
	beq	.L1132
	mov	r1, #1
	bl	FtlGetLastWrittenPage
	cmn	r0, #1
	mov	r4, r0
	beq	.L1133
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
	ldr	r3, .L1259+4
	ldrh	r6, [r3]
	add	r3, r10, #16
	mov	ip, r3
	str	r3, [fp, #-80]
.L1134:
	ldr	r1, [fp, #-56]
	uxth	r3, r2
	cmp	r1, r3
	bhi	.L1138
	ldr	r6, .L1259
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
.L1139:
	uxth	r2, r3
	cmp	r5, r2
	bhi	.L1144
	addeq	r3, r4, #1
	uxtheq	r3, r3
	streq	r3, [fp, #-60]
	bne	.L1142
.L1252:
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
	beq	.L1258
	movw	r3, #65535
	cmp	r8, r3
	bne	.L1148
	ldrb	r3, [r10, #8]	@ zero_extendqisi2
	cmp	r3, #0
	bne	.L1149
.L1148:
	ldr	r3, [r6, #3452]
	uxth	r7, r4
	uxth	r4, r4
	cmn	r3, #1
	ldreq	r3, [fp, #-56]
	streq	r3, [r6, #3452]
	ldr	r3, [fp, #-64]
	ldr	r6, .L1259
	add	r3, r3, #7
	cmp	r4, r3
	subgt	r4, r7, #7
	ldrle	r4, [fp, #-64]
	uxthgt	r4, r4
.L1152:
	cmp	r4, r7
	bhi	.L1159
	mov	r3, #0
	ldrh	r8, [r6, #236]
	ldr	r0, [r6, #3292]
	mov	r5, r3
	ldr	r1, [fp, #-80]
	movw	lr, #65535
	mov	ip, #20
	b	.L1160
.L1133:
	ldr	r3, [fp, #-64]
	cmp	r3, #0
	beq	.L1135
	movw	r2, #1800
	ldr	r1, .L1259+8
	ldr	r0, .L1259+12
	bl	sftl_printk
.L1135:
	ldr	r3, [fp, #-84]
	cmp	r3, #0
	cmpne	r6, r3
	beq	.L1136
	movw	r2, #1801
	ldr	r1, .L1259+8
	ldr	r0, .L1259+12
	bl	sftl_printk
.L1136:
	mov	r3, #0
	strh	r3, [r10, #2]	@ movhi
	b	.L1257
.L1138:
	ldrh	r3, [ip], #2
	cmp	r3, r7
	beq	.L1137
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
.L1137:
	add	r2, r2, #1
	b	.L1134
.L1144:
	ldr	r2, [r7]
	cmp	r2, #0
	bne	.L1140
	ldr	ip, [r7, #12]
	ldr	r2, [ip, #4]
	cmn	r2, #1
	beq	.L1141
	ldr	r1, .L1259
	mov	r0, r2
	str	ip, [fp, #-68]
	ldr	r1, [r1, #2596]
	bl	ftl_cmp_data_ver
	ldr	ip, [fp, #-68]
	cmp	r0, #0
	ldrne	r1, .L1259
	addne	r2, r2, #1
	strne	r2, [r1, #2596]
.L1141:
	ldr	r2, [ip]
	cmn	r2, #1
	bne	.L1143
.L1142:
	uxth	r2, r4
	uxth	r3, r3
	str	r2, [fp, #-60]
	mov	r2, #20
	mla	r9, r2, r3, r9
	b	.L1252
.L1140:
	ldr	r8, [fp, #-60]
.L1143:
	add	r3, r3, #1
	add	r7, r7, #20
	b	.L1139
.L1154:
	ldrh	r2, [r1], #2
	add	r3, r3, #1
	cmp	r2, lr
	mlane	r9, ip, r5, r0
	addne	r5, r5, #1
	orrne	r2, r4, r2, lsl #10
	uxthne	r5, r5
	strne	r2, [r9, #4]
.L1160:
	uxth	r2, r3
	cmp	r8, r2
	bhi	.L1154
	mov	r1, r5
	mov	r2, #0
	bl	FlashReadPages
	ldr	r3, [r6, #3292]
	mov	r2, #20
	movw	r1, #65535
	mla	r5, r2, r5, r3
.L1155:
	cmp	r5, r3
	addeq	r4, r4, #1
	uxtheq	r4, r4
	beq	.L1152
.L1158:
	ldr	r2, [r3]
	cmp	r2, #0
	bne	.L1149
	ldr	r2, [r3, #12]
	ldrh	r0, [r2]
	cmp	r0, r1
	beq	.L1157
	ldr	r2, [r2, #4]
	cmn	r2, #1
	strne	r2, [r6, #3452]
.L1157:
	add	r3, r3, #20
	b	.L1155
.L1159:
	mvn	r3, #0
	str	r3, [r6, #3452]
.L1149:
	ldr	r7, .L1259
	mov	r2, #1
	ldr	r9, [fp, #-64]
	movw	r3, #3476
	mov	r4, r7
	strh	r2, [r7, r3]	@ movhi
.L1161:
	ldrh	ip, [r4, #236]
	movw	lr, #65535
	ldr	r0, [r4, #3292]
	mov	r5, #20
	ldr	r1, [fp, #-80]
	mov	r3, #0
	str	r3, [fp, #-72]
.L1162:
	uxth	r2, r3
	cmp	ip, r2
	bhi	.L1164
	mov	r2, #0
	ldr	r1, [fp, #-72]
	bl	FlashReadPages
	mov	r3, #0
.L1256:
	str	r3, [fp, #-76]
	ldr	r2, [fp, #-72]
	ldrh	r3, [fp, #-76]
	cmp	r2, r3
	bhi	.L1192
	ldr	r3, .L1259+16
	add	r9, r9, #1
	uxth	r9, r9
	ldrh	r3, [r3]
	cmp	r3, r9
	bne	.L1161
	ldrh	r2, [r4, #236]
	movw	r0, #65535
	mov	r3, #0
	strh	r9, [r10, #2]	@ movhi
	strh	r3, [r10, #4]	@ movhi
.L1193:
	uxth	r1, r3
	cmp	r1, r2
	bcs	.L1251
	ldr	r1, [fp, #-80]
	ldrh	ip, [r1], #2
	cmp	ip, r0
	str	r1, [fp, #-80]
	add	r1, r3, #1
	bne	.L1257
	mov	r3, r1
	b	.L1193
.L1164:
	ldrh	r2, [r1], #2
	cmp	r2, lr
	beq	.L1163
	ldr	r6, [fp, #-72]
	orr	r2, r9, r2, lsl #10
	mla	r6, r5, r6, r0
	str	r2, [r6, #4]
	ldr	r2, [fp, #-72]
	add	r2, r2, #1
	uxth	r2, r2
	str	r2, [fp, #-72]
.L1163:
	add	r3, r3, #1
	b	.L1162
.L1192:
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
	bcc	.L1166
	ldr	r2, [fp, #-84]
	moveq	r3, #1
	movne	r3, #0
	cmp	r2, r0
	movls	r3, #0
	andhi	r3, r3, #1
	cmp	r3, #0
	bne	.L1166
	ldr	r3, [fp, #-60]
	ldr	r2, [fp, #-68]
	cmp	r9, r3
	cmpeq	r2, r0
	beq	.L1167
	ldr	r2, [fp, #-88]
	ldr	r3, [r2, r6]
	cmn	r3, #1
	beq	.L1168
	ldr	r8, [r8, #12]
	movw	r3, #61589
	ldrh	r2, [r8]
	cmp	r2, r3
	ldrhne	r0, [r10]
	bne	.L1253
	ldr	r3, [r8, #4]
	cmn	r3, #1
	str	r3, [fp, #-56]
	beq	.L1170
	mov	r0, r3
	ldr	r1, [r4, #2596]
	bl	ftl_cmp_data_ver
	cmp	r0, #0
	ldrne	r3, [fp, #-56]
	addne	r3, r3, #1
	strne	r3, [r4, #2596]
.L1170:
	ldrh	r2, [r8]
	movw	r3, #61589
	cmp	r2, r3
	beq	.L1171
	mov	r2, #1952
	ldr	r1, .L1259+8
	ldr	r0, .L1259+12
	bl	sftl_printk
.L1171:
	ldr	r5, [r8, #8]
	sub	r1, fp, #48
	ldr	r3, [r8, #12]
	mov	r2, #0
	mov	r0, r5
	str	r3, [fp, #-52]
	bl	log2phys
	ldr	r1, [r4, #3452]
	cmn	r1, #1
	beq	.L1172
	ldr	r0, [fp, #-56]
	bl	ftl_cmp_data_ver
	cmp	r0, #0
	beq	.L1172
	ldr	r2, [fp, #-52]
	cmn	r2, #1
	beq	.L1173
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
	bne	.L1174
.L1175:
	mvn	r3, #0
	str	r3, [fp, #-52]
.L1182:
	ldr	r6, [fp, #-52]
	cmn	r6, #1
	beq	.L1166
.L1196:
	ubfx	r0, r6, #10, #16
	bl	P2V_block_in_plane
	ldrh	r3, [r4, #244]
	mov	r5, r0
	cmp	r3, r0
	bhi	.L1188
	movw	r2, #2057
	ldr	r1, .L1259+8
	ldr	r0, .L1259+12
	bl	sftl_printk
.L1188:
	ldr	r2, [r7, #76]
	lsl	r3, r5, #1
	ldrh	r3, [r2, r3]
	cmp	r3, #0
	beq	.L1189
	mov	r0, r5
.L1253:
	bl	decrement_vpc_count
	b	.L1166
.L1173:
	ldr	r3, [fp, #-44]
	ldr	r2, [fp, #-48]
	cmp	r2, r3
	bne	.L1166
	mov	r2, #1
	sub	r1, fp, #52
	mov	r0, r5
	bl	log2phys
.L1166:
	ldr	r3, [fp, #-76]
	add	r3, r3, #1
	b	.L1256
.L1174:
	ldr	r1, [r8, #8]
	cmp	r5, r1
	bne	.L1175
	ldr	r1, [r8, #4]
	ldr	r0, [r4, #3452]
	str	r1, [fp, #-88]
	bl	ftl_cmp_data_ver
	cmp	r0, #0
	beq	.L1175
	ldr	r1, [fp, #-48]
	ldr	r0, [fp, #-44]
	cmp	r1, r0
	bne	.L1177
.L1180:
	ldr	r1, [fp, #-52]
	mov	r0, r5
	bl	FtlReUsePrevPpa
	b	.L1175
.L1177:
	ldr	r0, [fp, #-52]
	cmp	r1, r0
	beq	.L1175
	cmn	r1, #1
	streq	r1, [r2, r6]
	beq	.L1179
	str	r1, [r3, #4]
	mov	r2, #0
	mov	r1, #1
	mov	r0, r3
	ldr	r8, [r3, #12]
	bl	FlashReadPages
.L1179:
	ldr	r2, [r4, #3292]
	ldr	r2, [r2, r6]
	cmn	r2, #1
	beq	.L1180
	ldr	r3, [r8, #4]
	ldr	r0, [r4, #3452]
	mov	r1, r3
	bl	ftl_cmp_data_ver
	cmp	r0, #0
	beq	.L1180
	mov	r1, r3
	ldr	r0, [fp, #-88]
	bl	ftl_cmp_data_ver
	cmp	r0, #0
	beq	.L1175
	b	.L1180
.L1172:
	ldr	r3, [fp, #-44]
	ldr	r2, [fp, #-48]
	cmp	r2, r3
	beq	.L1182
	ldr	r1, [fp, #-52]
	cmn	r1, #1
	beq	.L1184
	ldr	r3, [r4, #252]
	cmp	r3, r1, lsr #10
	ldrls	r0, .L1259+20
	bls	.L1255
.L1184:
	mov	r2, #1
	sub	r1, fp, #44
	mov	r0, r5
	bl	log2phys
	ldr	r6, [fp, #-48]
	cmn	r6, #1
	beq	.L1182
	ldr	r3, [fp, #-52]
	cmp	r6, r3
	beq	.L1196
	ubfx	r0, r6, #10, #16
	bl	P2V_block_in_plane
	ldrh	r3, [r4, #28]
	cmp	r3, r0
	beq	.L1187
	ldrh	r3, [r4, #80]
	cmp	r3, r0
	beq	.L1187
	ldrh	r3, [r4, #128]
	cmp	r3, r0
	bne	.L1182
.L1187:
	ldr	r0, [r7, #3292]
	mov	r2, #0
	mov	r1, #1
	str	r6, [r0, #4]
	ldr	r8, [r0, #12]
	bl	FlashReadPages
	ldr	r3, [r7, #3292]
	ldr	r3, [r3]
	cmn	r3, #1
	beq	.L1182
	ldr	r1, [r8, #4]
	ldr	r0, [fp, #-56]
	bl	ftl_cmp_data_ver
	cmp	r0, #0
	bne	.L1182
	mov	r2, #1
	sub	r1, fp, #48
	mov	r0, r5
	bl	log2phys
	b	.L1182
.L1189:
	ldr	r0, .L1259+24
	mov	r1, r5
.L1255:
	bl	sftl_printk
	b	.L1166
.L1168:
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
	beq	.L1254
	ldr	r2, [fp, #-56]
	cmp	r2, r3
	bcs	.L1166
	mov	r3, r2
.L1254:
	str	r3, [r4, #3452]
	b	.L1166
.L1167:
	ldrb	r3, [fp, #-68]	@ zero_extendqisi2
	ldr	r2, [fp, #-68]
	ldr	r1, [fp, #-60]
	strb	r3, [r10, #6]
	ldrh	r3, [fp, #-60]
	strh	r3, [r10, #2]	@ movhi
.L1258:
	mov	r0, r10
	bl	ftl_sb_update_avl_pages
	b	.L1251
.L1260:
	.align	2
.L1259:
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
	ldr	r6, .L1285
	ldr	r1, .L1285+4
	ldr	r0, .L1285+8
	mov	r5, r6
	bl	sftl_printk
	mov	r2, #8192
	mov	r1, #0
	ldr	r0, .L1285+12
	bl	ftl_memset
.L1262:
	ldr	r3, [r6, #2556]
	cmp	r4, r3
	bcc	.L1264
	ldr	r8, .L1285+12
	mov	r4, #0
	ldr	r9, .L1285+16
	mov	r6, r4
.L1265:
	ldrh	r2, [r5, #244]
	uxth	r3, r4
	cmp	r2, r3
	bhi	.L1267
	ldr	r4, [r5, #2536]
	cmp	r4, #0
	beq	.L1268
	ldr	r3, [r5, #2520]
	mov	r7, #0
	ldrh	r8, [r5, #228]
	ldr	r9, .L1285+12
	ldr	r10, .L1285+20
	sub	r4, r4, r3
	ubfx	r4, r4, #3, #16
.L1269:
	uxth	r3, r7
	cmp	r8, r3
	bls	.L1268
	ldr	r2, [r5, #76]
	lsl	r3, r4, #1
	ldrh	r2, [r2, r3]
	cmp	r2, #0
	beq	.L1270
	mov	r6, #1
	ldrh	r3, [r9, r3]
	mov	r1, r4
	mov	r0, r10
	bl	sftl_printk
.L1270:
	ldr	r3, [r5, #2520]
	lsl	r4, r4, #3
	add	r7, r7, #1
	ldrh	r4, [r3, r4]
	movw	r3, #65535
	cmp	r4, r3
	bne	.L1269
.L1268:
	cmp	r6, #0
	ldmibeq	sp, {r4, r5, r6, r7, r8, r9, r10, fp, sp, pc}
	movw	r2, #2383
	ldr	r1, .L1285+4
	ldr	r0, .L1285+24
	bl	sftl_printk
	ldmib	sp, {r4, r5, r6, r7, r8, r9, r10, fp, sp, pc}
.L1264:
	mov	r2, #0
	sub	r1, fp, #44
	mov	r0, r4
	bl	log2phys
	ldr	r0, [fp, #-44]
	cmn	r0, #1
	beq	.L1263
	ubfx	r0, r0, #10, #16
	bl	P2V_block_in_plane
	ldr	r2, .L1285+12
	lsl	r0, r0, #1
	ldrh	r3, [r2, r0]
	add	r3, r3, #1
	strh	r3, [r2, r0]	@ movhi
.L1263:
	add	r4, r4, #1
	b	.L1262
.L1267:
	uxth	r1, r4
	ldr	r3, [r5, #76]
	lsl	r7, r1, #1
	ldrh	r2, [r3, r7]
	ldrh	r3, [r8, r7]
	cmp	r2, r3
	beq	.L1266
	mov	r0, r9
	bl	sftl_printk
	ldr	r3, [r5, #76]
	movw	r2, #65535
	ldrh	r3, [r3, r7]
	cmp	r3, r2
	beq	.L1266
	ldrh	r2, [r8, r7]
	cmp	r2, r3
	movhi	r6, #1
.L1266:
	add	r4, r4, #1
	b	.L1265
.L1286:
	.align	2
.L1285:
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
	ldr	r6, .L1295
	mov	r1, #0
	ldr	r0, .L1295+4
	bl	sftl_printk
	mov	r4, r6
.L1288:
	ldr	r3, [r6, #2556]
	cmp	r5, r3
	bcc	.L1294
	sub	sp, fp, #28
	ldmfd	sp, {r4, r5, r6, r7, fp, sp, pc}
.L1294:
	mov	r2, #0
	sub	r1, fp, #32
	mov	r0, r5
	bl	log2phys
	ubfx	r3, r5, #0, #11
	cmp	r3, #0
	bne	.L1289
	ldr	r2, [fp, #-32]
	mov	r1, r5
	ldr	r0, .L1295+8
	bl	sftl_printk
.L1289:
	ldr	r3, [fp, #-32]
	cmn	r3, #1
	beq	.L1291
	str	r3, [r4, #3460]
	mov	r2, #0
	ldr	r3, [r4, #3304]
	mov	r1, #1
	ldr	r7, [r4, #3336]
	ldr	r0, .L1295+12
	str	r3, [r4, #3464]
	str	r5, [r4, #3472]
	str	r7, [r4, #3468]
	str	r2, [r4, #3456]
	bl	FlashReadPages
	ldr	r3, [r4, #3456]
	cmn	r3, #1
	cmpne	r3, #256
	beq	.L1292
	ldr	r3, [r7, #8]
	cmp	r5, r3
	beq	.L1291
.L1292:
	ldr	r2, [r4, #3464]
	ldr	r3, [r4, #3468]
	ldr	r0, .L1295+16
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
.L1291:
	add	r5, r5, #1
	b	.L1288
.L1296:
	.align	2
.L1295:
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
	ldr	r3, .L1329
	str	r1, [fp, #-76]
	ldrh	r5, [r3, #4]
	movw	r3, #65535
	cmp	r5, r3
	beq	.L1322
	cmp	r5, #0
	bne	.L1298
.L1299:
	bl	FtlGcPageVarInit
	b	.L1300
.L1322:
	mov	r5, #0
.L1298:
	ldr	r2, .L1329+4
	movw	r3, #306
	ldrh	r3, [r2, r3]
	ldr	r2, [fp, #-76]
	cmp	r3, r2
	beq	.L1299
.L1300:
	ldr	r6, .L1329+4
	mov	r3, #0
	str	r3, [fp, #-68]
.L1316:
	ldrh	r2, [r4]
	mov	r3, #0
	strb	r3, [r4, #8]
	movw	r3, #65535
	cmp	r2, r3
	beq	.L1301
.L1319:
	ldrh	r3, [r6, #236]
	add	ip, r4, #16
	ldr	r0, [r6, #3292]
	movw	r9, #65535
	mov	lr, #20
	str	r3, [fp, #-72]
	ldr	r3, [r6, #3180]
	str	r3, [fp, #-80]
	ldr	r3, .L1329+8
	ldrh	r2, [r3]
	ldrh	r10, [r3, #2]
	str	r2, [fp, #-84]
	ldr	r2, [r6, #3184]
	str	r2, [fp, #-88]
	mov	r2, #0
	mov	r7, r2
.L1302:
	ldr	r1, [fp, #-72]
	uxth	r3, r2
	cmp	r1, r3
	bhi	.L1304
	mov	r8, #0
	mov	r2, #0
	mov	r1, r7
	bl	FlashReadPages
.L1305:
	uxth	r3, r8
	cmp	r7, r3
	bhi	.L1317
	ldr	r3, [fp, #-68]
	add	r5, r5, #1
	uxth	r5, r5
	add	r3, r3, #1
	str	r3, [fp, #-68]
	ldr	r2, [fp, #-68]
	ldr	r3, [fp, #-76]
	cmp	r3, r2
	ldr	r2, .L1329+12
	bls	.L1318
.L1320:
	ldrh	r3, [r2]
	cmp	r3, r5
	bhi	.L1319
.L1301:
	ldr	r3, .L1329
	mvn	r2, #0
	strh	r5, [r4, #2]	@ movhi
	mov	r1, r5
	mov	r0, r4
	strh	r2, [r3, #4]	@ movhi
	mov	r2, #0
	strb	r2, [r4, #6]
	bl	ftl_sb_update_avl_pages
	b	.L1321
.L1304:
	ldrh	r3, [ip], #2
	cmp	r3, r9
	beq	.L1303
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
.L1303:
	add	r2, r2, #1
	b	.L1302
.L1317:
	mov	ip, #20
	ldr	r2, [r6, #3292]
	mul	ip, ip, r8
	ldr	r10, [r2, ip]
	add	r3, r2, ip
	ldr	r1, [r3, #4]
	ldr	r9, [r3, #12]
	cmp	r10, #0
	str	r1, [fp, #-72]
	bne	.L1306
	ldrh	r1, [r9]
	movw	r2, #65535
	cmp	r1, r2
	bne	.L1307
.L1327:
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
	b	.L1316
.L1307:
	ldr	r0, [r9, #8]
	ldr	r2, [r6, #2556]
	str	ip, [fp, #-80]
	cmp	r0, r2
	bhi	.L1327
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
	bne	.L1310
.L1315:
	ldr	r2, [r9, #8]
.L1328:
	ldr	r1, [fp, #-72]
	add	r8, r8, #1
	ldr	r0, [r9, #12]
	bl	FtlGcUpdatePage
	b	.L1305
.L1310:
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
	ldr	r2, .L1329+16
	ldr	ip, [fp, #-80]
	ldr	r0, [fp, #-52]
	ldrh	r1, [r2]
	ldr	r2, [r6, #3292]
	lsl	r1, r1, #7
	add	ip, r2, ip
	mov	r2, r10
.L1312:
	cmp	r2, r1
	beq	.L1315
	ldr	lr, [ip, #8]
	ldr	r10, [lr, r2, lsl #2]
	ldr	lr, [r0, r2, lsl #2]
	cmp	r10, lr
	beq	.L1313
	ldr	r2, [fp, #-56]
	ldrh	r1, [r4]
	ldr	r0, .L1329+20
	bl	sftl_printk
	b	.L1327
.L1313:
	add	r2, r2, #1
	b	.L1312
.L1306:
	mvn	r2, #0
	b	.L1328
.L1318:
	ldr	r1, .L1329
	movw	r0, #65535
	ldrh	r3, [r1, #4]
	cmp	r3, r0
	beq	.L1320
	ldr	r0, [fp, #-68]
	add	r3, r3, r0
	strh	r3, [r1, #4]	@ movhi
	ldrh	r3, [r2]
	cmp	r3, r5
	bls	.L1320
.L1321:
	mvn	r0, #0
	sub	sp, fp, #40
	ldmfd	sp, {r4, r5, r6, r7, r8, r9, r10, fp, sp, pc}
.L1330:
	.align	2
.L1329:
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
	ldr	r6, .L1347
	ldr	r9, [r6, #2724]
	mov	r5, r6
	cmp	r9, #0
	beq	.L1332
	ldr	r2, [r6, #2728]
	ldr	r3, [r6, #2556]
	cmp	r2, r3
	bcs	.L1333
	mov	r4, #2048
.L1338:
	ldr	r0, [r5, #2728]
	ldr	r3, [r5, #2556]
	cmp	r0, r3
	bcc	.L1334
.L1337:
	mvn	r0, #0
.L1331:
	sub	sp, fp, #40
	ldmfd	sp, {r4, r5, r6, r7, r8, r9, r10, fp, sp, pc}
.L1334:
	mov	r2, #0
	sub	r1, fp, #128
	bl	log2phys
	ldr	r2, [fp, #-128]
	ldr	r3, [r5, #2728]
	cmn	r2, #1
	add	r3, r3, #1
	str	r3, [r5, #2728]
	beq	.L1336
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
	bne	.L1337
	ldr	r0, [fp, #-128]
	ubfx	r0, r0, #10, #16
	bl	P2V_block_in_plane
	bl	FtlGcRefreshBlock
	b	.L1337
.L1336:
	subs	r4, r4, #1
	bne	.L1338
	b	.L1337
.L1333:
	ldr	r3, [r6, #2560]
	mov	r0, #0
	str	r0, [r6, #2724]
	str	r0, [r6, #2728]
	str	r3, [r6, #2720]
	b	.L1331
.L1332:
	ldr	r1, [r6, #2616]
	movw	r4, #10000
	ldr	r8, [r6, #2560]
	ldr	r7, [r6, #2720]
	cmp	r1, r4
	ldr	r10, .L1347+4
	add	r3, r8, #1048576
	movhi	r4, #31
	movls	r4, #63
	cmp	r7, r3
	bhi	.L1342
	ldr	r3, [r6, #2556]
	lsr	r1, r1, #10
	mov	r0, #1000
	add	r1, r1, #1
	mul	r0, r0, r3
	bl	__udivsi3
	add	r0, r0, r7
	cmp	r8, r0
	bhi	.L1342
	ldrh	r3, [r10, #28]
	ands	r0, r4, r3
	movne	r0, r9
	bne	.L1331
	ldr	r2, [r6, #2744]
	cmp	r3, r2
	beq	.L1331
.L1342:
	ldrh	r3, [r10, #28]
	mov	r0, #0
	str	r0, [r5, #2728]
	str	r8, [r5, #2720]
	str	r3, [r5, #2744]
	mov	r3, #1
	str	r3, [r5, #2724]
	b	.L1331
.L1348:
	.align	2
.L1347:
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
	ldr	r6, .L1353
	mov	r4, #0
	ldr	r5, .L1353+4
	bl	FtlWriteDump_data
.L1350:
	ldrh	r2, [r5]
	uxth	r3, r4
	cmp	r2, r3
	bhi	.L1352
	mov	r0, #0
	ldmfd	sp, {r4, r5, r6, fp, sp, pc}
.L1352:
	ldr	r2, [r6, #2544]
	uxth	r0, r4
	mov	r3, #12
	mla	r3, r3, r0, r2
	ldr	r3, [r3, #4]
	cmp	r3, #0
	bge	.L1351
	bl	flush_l2p_region
.L1351:
	add	r4, r4, #1
	b	.L1350
.L1354:
	.align	2
.L1353:
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
	ldr	r4, .L1364
	mov	r5, r1
	str	r2, [fp, #-128]
	add	r2, r0, r1
	ldrh	r3, [r4]
	sub	r4, r4, #300
	cmp	r2, r3
	mvnhi	r8, #0
	bhi	.L1355
	add	r3, r4, #312
	mov	r8, #0
	ldrh	r6, [r3]
	lsr	r6, r0, r6
	lsl	r10, r6, #2
.L1357:
	cmp	r5, #0
	bne	.L1362
.L1355:
	mov	r0, r8
	sub	sp, fp, #40
	ldmfd	sp, {r4, r5, r6, r7, r8, r9, r10, fp, sp, pc}
.L1362:
	ldr	r3, [r4, #3376]
	mov	r0, r7
	ldr	ip, [r3, r10]
	ldr	r3, .L1364+4
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
	beq	.L1359
	ldr	r2, [r4, #3312]
	sub	r0, fp, #124
	str	ip, [fp, #-120]
	str	r2, [fp, #-116]
	sub	r2, fp, #104
	str	r2, [fp, #-112]
	mov	r2, #1
	mov	r1, r2
	bl	FlashReadPages
.L1360:
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
	ldr	r0, .L1364+8
	add	r6, r6, #1
	bl	FtlMapWritePage
	ldr	r3, [fp, #-128]
	cmn	r0, #1
	ldr	ip, [fp, #-136]
	mvneq	r8, #0
	add	r3, r3, ip
	str	r3, [fp, #-128]
	b	.L1357
.L1359:
	ldr	r3, .L1364+12
	ldr	r0, [r4, #3312]
	ldrh	r2, [r3]
	bl	ftl_memset
	b	.L1360
.L1365:
	.align	2
.L1364:
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
	ldr	r3, .L1368
	ldr	r2, .L1368+4
	ldr	r1, [r3, #2644]
	cmp	r1, r2
	ldmfdne	sp, {fp, sp, pc}
	ldr	r2, .L1368+8
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
	ldr	r2, .L1368+12
	bl	FtlVendorPartWrite
	ldmfd	sp, {fp, sp, pc}
.L1369:
	.align	2
.L1368:
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
	ldr	r3, .L1375
	movw	r1, #3656
	ldrh	r2, [r3, r1]
	cmp	r2, #31
	addls	r2, r2, #1
	movhi	r2, #32
	strhls	r2, [r3, r1]	@ movhi
	movls	r2, #1
	cmp	r0, #0
	bne	.L1372
	ldr	r1, [r3, #3352]
	ldr	r0, [r1, #20]
	ldr	r1, [r1, #16]
	add	r2, r2, r0
	cmp	r1, r2
	bcc	.L1373
.L1372:
	ldr	r2, [r3, #3352]
	mov	r0, #64
	ldr	r1, [r2, #16]
	str	r1, [r2, #20]
	ldr	r1, .L1375+4
	str	r1, [r2]
	ldr	r2, [r3, #3352]
	ldr	r3, .L1375+8
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
.L1373:
	mov	r0, #0
	ldmfd	sp, {fp, sp, pc}
.L1376:
	.align	2
.L1375:
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
	ldr	r5, .L1387
	add	r2, r0, r1
	mov	r8, r0
	mov	r7, r1
	ldrh	r3, [r5]
	sub	r5, r5, #300
	cmp	r2, r3
	mvnhi	r9, #0
	bhi	.L1377
	add	r3, r5, #312
	mov	r9, #0
	ldrh	r6, [r3]
	lsr	r6, r0, r6
	lsl	r3, r6, #2
	str	r3, [fp, #-128]
.L1379:
	cmp	r7, #0
	bne	.L1385
.L1377:
	mov	r0, r9
	sub	sp, fp, #40
	ldmfd	sp, {r4, r5, r6, r7, r8, r9, r10, fp, sp, pc}
.L1385:
	ldr	r2, [fp, #-128]
	mov	r0, r8
	ldr	r3, [r5, #3376]
	ldr	r3, [r3, r2]
	str	r3, [fp, #-136]
	ldr	r3, .L1387+4
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
	beq	.L1381
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
	bne	.L1383
	mov	r2, r3
	mov	r1, r6
	ldr	r0, .L1387+8
	bl	sftl_printk
	ldr	r2, [r5, #3312]
	mov	r1, r6
	ldr	r0, .L1387+12
	bl	FtlMapWritePage
.L1383:
	ldr	r1, [r5, #3312]
	lsl	r2, r4, #9
	ldr	r3, [fp, #-132]
	mov	r0, r10
	add	r1, r1, r3, lsl #9
	bl	ftl_memcpy
.L1384:
	ldr	r3, [fp, #-128]
	add	r6, r6, #1
	sub	r7, r7, r4
	add	r8, r8, r4
	add	r10, r10, r4, lsl #9
	add	r3, r3, #4
	str	r3, [fp, #-128]
	b	.L1379
.L1381:
	lsl	r2, r4, #9
	mov	r1, r3
	mov	r0, r10
	bl	ftl_memset
	b	.L1384
.L1388:
	.align	2
.L1387:
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
	ldr	r4, .L1391
	mov	r0, #64
	add	r5, r4, #3344
	ldr	r2, [r4, #3352]
	ldrh	r1, [r5]
	bl	FtlVendorPartRead
	ldr	r3, [r4, #3352]
	ldr	r2, [r3]
	ldr	r3, .L1391+4
	cmp	r2, r3
	beq	.L1390
	ldr	r1, .L1391+8
	ldr	r0, .L1391+12
	bl	sftl_printk
	ldrh	r2, [r5]
	mov	r1, #0
	ldr	r0, [r4, #3352]
	lsl	r2, r2, #9
	bl	ftl_memset
.L1390:
	mov	r0, #0
	ldmfd	sp, {r4, r5, fp, sp, pc}
.L1392:
	.align	2
.L1391:
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
	ldr	r4, .L1396
	mov	r1, #1
	mov	r0, #0
	ldr	r2, .L1396+4
	bl	FtlVendorPartRead
	ldr	r5, .L1396+8
	ldr	r3, [r4, #2644]
	cmp	r3, r5
	beq	.L1394
	mov	r2, #512
	mov	r1, #0
	ldr	r0, .L1396+4
	bl	ftl_memset
	str	r5, [r4, #2644]
.L1394:
	ldr	r3, [r4, #2644]
	cmp	r3, r5
	bne	.L1395
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
.L1395:
	ldr	r3, .L1396+12
	ldr	r0, [r4, #2600]
	ldrh	r1, [r4, #244]
	ldrh	r2, [r3]
	ldr	r3, [r4, #2604]
	mla	r0, r0, r2, r3
	bl	__udivsi3
	str	r0, [r4, #2608]
	ldmfd	sp, {r4, r5, fp, sp, pc}
.L1397:
	.align	2
.L1396:
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
	ldr	r3, .L1405
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
	beq	.L1400
	mov	r2, #1
	add	r0, r3, #3456
	mov	r1, r2
	bl	FlashReadPages
.L1401:
	ldr	r2, [r4, #3464]
	mov	r1, r5
	mov	r0, r6
	bl	FtlMapWritePage
	ldmfd	sp, {r4, r5, r6, fp, sp, pc}
.L1400:
	movw	r3, #314
	mov	r1, #255
	ldrh	r2, [r4, r3]
	bl	ftl_memset
	b	.L1401
.L1406:
	.align	2
.L1405:
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
	ldr	r4, .L1416
	mov	r5, #0
	mov	r1, #255
	ldr	r6, .L1416+4
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
	ldr	r3, .L1416+8
	strh	r3, [r7]	@ movhi
	ldr	r3, [r4, #2636]
	str	r5, [r7, #8]
	ldrh	r2, [r4, #30]
	str	r3, [r7, #4]
	ldr	r3, .L1416+12
	str	r3, [r4, #2472]
	ldr	r3, .L1416+16
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
.L1408:
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
	blt	.L1409
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
.L1409:
	ldr	r3, .L1416+20
	ldr	r0, [r4, #3304]
	ldrh	r1, [r3]
	bl	js_hash
	mov	r3, #1
	str	r0, [r7, #12]
	mov	r2, r3
	mov	r1, r3
	ldr	r0, .L1416+24
	bl	FlashProgPages
	ldrh	r3, [r6, #2]
	ldr	r2, [r4, #3456]
	add	r3, r3, #1
	uxth	r3, r3
	cmn	r2, #1
	strh	r3, [r6, #2]	@ movhi
	bne	.L1410
	cmp	r3, #1
	bne	.L1411
	movw	r2, #1138
	ldr	r1, .L1416+28
	ldr	r0, .L1416+32
	bl	sftl_printk
.L1411:
	ldrh	r3, [r6, #2]
	add	r5, r5, #1
	uxth	r5, r5
	cmp	r3, #1
	ldrheq	r3, [r10]
	subeq	r3, r3, #1
	strheq	r3, [r6, #2]	@ movhi
	cmp	r5, #3
	bls	.L1408
	mov	r2, r5
	ldr	r1, [r4, #3460]
	ldr	r0, .L1416+36
	bl	sftl_printk
.L1414:
	b	.L1414
.L1410:
	cmp	r2, #256
	cmpne	r3, #1
	beq	.L1408
	movw	r3, #65535
	cmp	r8, r3
	beq	.L1415
	mov	r1, #1
	mov	r0, r8
	bl	FtlFreeSysBlkQueueIn
.L1415:
	mov	r0, #0
	ldmfd	sp, {r4, r5, r6, r7, r8, r9, r10, fp, sp, pc}
.L1417:
	.align	2
.L1416:
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
	ldr	r3, .L1421
	ldr	r3, [r3]
	cmp	r3, #1
	bne	.L1420
	bl	FtlSysFlush
.L1420:
	mov	r0, #0
	ldmfd	sp, {fp, sp, pc}
.L1422:
	.align	2
.L1421:
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
	ldr	r4, .L1438
	mov	r8, r0
	mov	r6, r1
	ldr	r3, [r4, #344]
	cmp	r2, r3
	mvnhi	r0, #0
	bhi	.L1423
	cmp	r1, #31
	bhi	.L1425
.L1430:
	mov	r0, #0
.L1423:
	sub	sp, fp, #32
	ldmfd	sp, {r4, r5, r6, r7, r8, fp, sp, pc}
.L1425:
	movw	r3, #262
	ldrh	r5, [r4, r3]
	mov	r1, r5
	bl	__udivsi3
	smulbb	r3, r0, r5
	mov	r7, r0
	sub	r8, r8, r3
	uxth	r8, r8
	cmp	r8, #0
	beq	.L1426
	sub	r5, r5, r8
	add	r7, r0, #1
	cmp	r5, r6
	movcs	r5, r6
	uxth	r5, r5
	sub	r6, r6, r5
.L1426:
	ldr	r5, .L1438+4
	mvn	r3, #0
	str	r3, [fp, #-36]
	mov	r8, r5
.L1427:
	ldrh	r3, [r5]
	cmp	r6, r3
	bcs	.L1429
	ldr	r3, [r4, #3660]
	cmp	r3, #32
	bls	.L1430
	mov	r5, #0
	str	r5, [r4, #3660]
	bl	l2p_flush
	bl	FtlVpcTblFlush
	b	.L1430
.L1429:
	mov	r2, #0
	sub	r1, fp, #40
	mov	r0, r7
	bl	log2phys
	ldr	r3, [fp, #-40]
	cmn	r3, #1
	beq	.L1428
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
.L1428:
	ldrh	r3, [r8]
	add	r7, r7, #1
	sub	r6, r6, r3
	b	.L1427
.L1439:
	.align	2
.L1438:
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
	ldr	r4, .L1453
	ldr	r1, .L1453+4
	ldr	r0, .L1453+8
	bl	sftl_printk
	ldrh	r2, [r4, #246]
	mov	r1, #0
	ldr	r0, [r4, #3356]
	lsl	r2, r2, #1
	bl	ftl_memset
.L1441:
	ldr	r3, [r4, #2556]
	cmp	r5, r3
	bcc	.L1443
	ldr	r10, .L1453+12
	mov	r7, #0
	movw	r9, #65535
.L1444:
	ldrh	r3, [r4, #244]
	uxth	r6, r7
	cmp	r3, r6
	bhi	.L1449
	bl	l2p_flush
	bl	FtlVpcTblFlush
	ldmib	sp, {r4, r5, r6, r7, r8, r9, r10, fp, sp, pc}
.L1443:
	mov	r2, #0
	sub	r1, fp, #44
	mov	r0, r5
	bl	log2phys
	ldr	r0, [fp, #-44]
	cmn	r0, #1
	beq	.L1442
	ubfx	r0, r0, #10, #16
	bl	P2V_block_in_plane
	ldr	r2, [r4, #3356]
	lsl	r0, r0, #1
	ldrh	r3, [r2, r0]
	add	r3, r3, #1
	strh	r3, [r2, r0]	@ movhi
.L1442:
	add	r5, r5, #1
	b	.L1441
.L1449:
	uxth	r8, r7
	ldr	r3, [r4, #76]
	lsl	r5, r8, #1
	ldrh	r2, [r3, r5]
	ldr	r3, [r4, #3356]
	ldrh	r3, [r3, r5]
	cmp	r2, r9
	cmpne	r2, r3
	beq	.L1446
	mov	r1, r8
	mov	r0, r10
	bl	sftl_printk
	ldrh	r3, [r4, #28]
	cmp	r3, r6
	beq	.L1446
	ldrh	r3, [r4, #128]
	cmp	r3, r6
	beq	.L1446
	ldrh	r3, [r4, #80]
	cmp	r3, r6
	beq	.L1446
	ldr	r3, [r4, #76]
	ldrh	r2, [r3, r5]
	cmp	r2, #0
	ldr	r2, [r4, #3356]
	ldrh	r2, [r2, r5]
	strh	r2, [r3, r5]	@ movhi
	bne	.L1448
.L1446:
	add	r7, r7, #1
	b	.L1444
.L1448:
	mov	r0, r8
	bl	update_vpc_list
	b	.L1446
.L1454:
	.align	2
.L1453:
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
	ldr	r4, .L1465
	mov	r6, r0
	ldrh	r5, [r0]
	ldrh	r3, [r4, #244]
	cmp	r3, r5
	bcs	.L1456
	movw	r2, #2755
	ldr	r1, .L1465+4
	ldr	r0, .L1465+8
	bl	sftl_printk
.L1456:
	movw	r3, #65535
	cmp	r5, r3
	beq	.L1457
	ldr	r2, [r4, #76]
	lsl	r3, r5, #1
	mov	r0, r5
	ldrh	r3, [r2, r3]
	cmp	r3, #0
	beq	.L1458
	bl	INSERT_DATA_LIST
.L1457:
	mov	r3, #1
	strb	r3, [r6, #8]
	movw	r3, #3442
	ldrh	r0, [r4, r3]
	movw	r3, #65535
	cmp	r0, r3
	beq	.L1459
	cmp	r5, r0
	bne	.L1460
	ldr	r2, [r4, #76]
	lsl	r3, r0, #1
	ldrh	r3, [r2, r3]
	cmp	r3, #0
	beq	.L1461
.L1460:
	bl	update_vpc_list
.L1461:
	mvn	r2, #0
	movw	r3, #3442
	strh	r2, [r4, r3]	@ movhi
.L1459:
	mov	r0, r6
	bl	allocate_data_superblock
	bl	l2p_flush
	mov	r0, #0
	bl	FtlEctTblFlush
	bl	FtlVpcTblFlush
	mov	r0, #0
	ldmfd	sp, {r4, r5, r6, fp, sp, pc}
.L1458:
	bl	INSERT_FREE_LIST
	b	.L1457
.L1466:
	.align	2
.L1465:
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
	ldr	r8, .L1480
	mov	r5, r3
	mov	r2, #0
	ldrb	r3, [r3, #9]	@ zero_extendqisi2
	mov	r4, r0
	mov	r9, r1
	bl	FlashProgPages
.L1468:
	cmp	r6, r9
	beq	.L1474
	ldr	r7, .L1480+4
	b	.L1475
.L1470:
	ldr	r1, [r4, #4]
	mov	r0, r7
	bl	sftl_printk
	ldr	r0, [r4, #4]
	ubfx	r0, r0, #10, #16
	bl	P2V_block_in_plane
	bl	decrement_vpc_count
	ldrh	r3, [r5, #4]
	cmp	r3, #0
	bne	.L1469
	mov	r0, r5
	bl	allocate_new_data_superblock
.L1469:
	mov	r0, r5
	bl	get_new_active_ppa
	mov	r2, #0
	str	r0, [r4, #4]
	str	r0, [fp, #-44]
	mov	r1, #1
	ldrb	r3, [r5, #9]	@ zero_extendqisi2
	mov	r0, r4
	bl	FlashProgPages
.L1475:
	ldr	r2, [r4]
	cmn	r2, #1
	cmpne	r2, #256
	beq	.L1470
	ldrb	r2, [r5, #6]	@ zero_extendqisi2
	ldrh	r3, [r8, #236]
	cmp	r2, r3
	bcc	.L1471
	movw	r2, #982
	ldr	r1, .L1480+8
	ldr	r0, .L1480+12
	bl	sftl_printk
.L1471:
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
	beq	.L1472
	ldr	r2, [r8, #76]
	lsl	r3, r0, #1
	ldrh	r2, [r2, r3]
	cmp	r2, #0
	bne	.L1473
	mov	r1, r0
	ldr	r0, .L1480+16
	bl	sftl_printk
.L1473:
	mov	r0, r7
	bl	decrement_vpc_count
.L1472:
	add	r6, r6, #1
	add	r4, r4, #20
	b	.L1468
.L1474:
	ldr	r3, .L1480
	ldrb	r2, [r5, #6]	@ zero_extendqisi2
	ldrh	r3, [r3, #236]
	cmp	r2, r3
	ldmibcc	sp, {r4, r5, r6, r7, r8, r9, r10, fp, sp, pc}
	movw	r2, #997
	ldr	r1, .L1480+8
	ldr	r0, .L1480+12
	bl	sftl_printk
	ldmib	sp, {r4, r5, r6, r7, r8, r9, r10, fp, sp, pc}
.L1481:
	.align	2
.L1480:
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
	ldr	r4, .L1506
	ldrh	r2, [r4, #128]
	ldrh	r1, [r4, r3]
	movw	r3, #65535
	cmp	r2, r3
	bne	.L1483
.L1490:
	ldrh	r2, [r4, #128]
	mov	r3, #0
	str	r3, [r4, #3448]
	movw	r3, #65535
	cmp	r2, r3
	bne	.L1503
.L1484:
	mov	r0, #0
	ldmib	sp, {r4, r5, r6, r7, r8, r9, r10, fp, sp, pc}
.L1483:
	cmp	r0, #0
	ldr	r5, .L1506+4
	beq	.L1486
	ldrh	r2, [r5, #4]
	cmp	r2, r3
	beq	.L1487
.L1488:
	mov	r1, #2
.L1486:
	ldr	r0, .L1506+8
	bl	FtlGcScanTempBlk
	ldrh	r2, [r5, #4]
	movw	r3, #65535
	str	r0, [fp, #-44]
	cmp	r2, r3
	beq	.L1490
	mov	r0, #1
	ldmib	sp, {r4, r5, r6, r7, r8, r9, r10, fp, sp, pc}
.L1487:
	mov	r3, #0
	strh	r3, [r5, #4]	@ movhi
	ldrh	r3, [r4, #228]
	cmp	r3, #17
	bhi	.L1488
	b	.L1486
.L1503:
	movw	r3, #3208
	ldrb	r1, [r4, #135]	@ zero_extendqisi2
	ldrh	r2, [r4, r3]
	movw	r3, #306
	ldrh	r3, [r4, r3]
	mul	r3, r3, r1
	cmp	r2, r3
	beq	.L1491
	mov	r2, #164
	ldr	r1, .L1506+12
	ldr	r0, .L1506+16
	bl	sftl_printk
.L1491:
	movw	r0, #306
	ldrb	r2, [r4, #135]	@ zero_extendqisi2
	ldrh	r0, [r4, r0]
	mov	r5, #0
	ldrh	r3, [r4, #128]
	mov	r9, #12
	ldr	r1, [r4, #76]
	ldr	r8, .L1506+20
	smulbb	r2, r2, r0
	lsl	r3, r3, #1
	strh	r2, [r1, r3]	@ movhi
	movw	r3, #3208
	ldr	r2, [r4, #2580]
	ldrh	r3, [r4, r3]
	add	r3, r3, r2
	str	r3, [r4, #2580]
.L1492:
	ldrh	r2, [r8]
	uxth	r3, r5
	cmp	r2, r3
	bhi	.L1496
	movw	r0, #65535
	bl	decrement_vpc_count
	ldrh	r0, [r4, #128]
	ldr	r2, [r4, #76]
	lsl	r3, r0, #1
	ldrh	r3, [r2, r3]
	cmp	r3, #0
	beq	.L1497
	bl	INSERT_DATA_LIST
.L1498:
	ldr	r6, .L1506+24
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
	b	.L1484
.L1496:
	uxth	r7, r5
	ldr	r10, [r4, #3204]
	ldr	r3, [r4, #2556]
	mul	r7, r9, r7
	add	r6, r10, r7
	ldr	r0, [r6, #8]
	cmp	r0, r3
	bcc	.L1493
.L1504:
	ldrh	r0, [r4, #128]
	b	.L1505
.L1493:
	mov	r2, #0
	sub	r1, fp, #44
	bl	log2phys
	ldr	r3, [fp, #-44]
	ldr	r0, [r10, r7]
	cmp	r0, r3
	bne	.L1495
	ubfx	r0, r0, #10, #16
	bl	P2V_block_in_plane
	mov	r2, #1
	mov	r7, r0
	add	r1, r6, #4
	ldr	r0, [r6, #8]
	bl	log2phys
	mov	r0, r7
.L1505:
	bl	decrement_vpc_count
	b	.L1494
.L1495:
	ldr	r2, [r6, #4]
	cmp	r3, r2
	bne	.L1504
.L1494:
	add	r5, r5, #1
	b	.L1492
.L1497:
	bl	INSERT_FREE_LIST
	b	.L1498
.L1507:
	.align	2
.L1506:
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
	ldr	r4, .L1510
	movw	r5, #306
	ldrh	r1, [r4, r5]
	add	r0, r4, #128
	bl	FtlGcScanTempBlk
	ldrh	r2, [r4, #130]
	ldrh	r3, [r4, r5]
	cmp	r2, r3
	ldmfdcc	sp, {r4, r5, fp, sp, pc}
	ldr	r0, .L1510+4
	bl	FtlMapBlkWriteDump_data
	mov	r0, #0
	bl	FtlGcFreeTempBlock
	mov	r3, #0
	str	r3, [r4, #3448]
	ldmfd	sp, {r4, r5, fp, sp, pc}
.L1511:
	.align	2
.L1510:
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
	ldr	r4, .L1513
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
.L1514:
	.align	2
.L1513:
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
	ldr	r4, .L1523
	mov	r3, #0
	mov	r5, #0
	mov	r6, #20
	mov	r2, r3
	ldr	r1, [r4, #3172]
	ldr	r0, [r4, #3296]
	bl	FlashProgPages
.L1516:
	ldr	r1, [r4, #3172]
	uxth	r3, r5
	cmp	r1, r3
	bhi	.L1519
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
.L1519:
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
	b	.L1516
.L1524:
	.align	2
.L1523:
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
	ldr	r4, .L1528
	movw	r3, #65535
	ldrh	r2, [r4, #128]
	cmp	r2, r3
	beq	.L1526
	ldrh	r3, [r4, #132]
	cmp	r3, #0
	bne	.L1527
.L1526:
	mov	r0, #0
	mov	r5, #0
	bl	FtlGcFreeTempBlock
	ldr	r0, .L1528+4
	strb	r5, [r4, #136]
	bl	allocate_data_superblock
	ldr	r3, .L1528+8
	strh	r5, [r3]	@ movhi
	movw	r3, #3208
	strh	r5, [r4, r3]	@ movhi
	bl	l2p_flush
	mov	r0, r5
	bl	FtlEctTblFlush
	bl	FtlVpcTblFlush
.L1527:
	ldr	r0, .L1528+4
	bl	get_new_active_ppa
	ldmfd	sp, {r4, r5, fp, sp, pc}
.L1529:
	.align	2
.L1528:
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
	ldr	r3, .L1659
	str	r0, [fp, #-64]
	ldr	r0, [r3, #3276]
	mov	r4, r3
	cmp	r0, #0
	movne	r0, #0
	bne	.L1530
	movw	r2, #2532
	ldrh	r2, [r3, r2]
	cmp	r2, #47
	bls	.L1530
	ldr	r2, .L1659+4
	ldrh	r1, [r2, #4]
	movw	r2, #65535
	cmp	r1, r2
	bne	.L1532
.L1535:
	ldrh	r3, [r4, #224]
	movw	r2, #65535
	cmp	r3, r2
	bne	.L1533
.L1534:
	bl	FtlReadRefresh
	ldr	r3, [r4, #3160]
	movw	r6, #65535
	ldr	r2, [fp, #-64]
	add	r3, r3, #1
	add	r3, r3, r2, lsl #7
	ldrh	r2, [r4, #176]
	str	r3, [r4, #3160]
	cmp	r2, r6
	bne	.L1536
	ldrh	r6, [r4, #128]
	cmp	r6, r2
	movne	r6, r2
	bne	.L1536
	ldrh	r5, [r4, #226]
	cmp	r5, r6
	bne	.L1536
	ldrh	r2, [r4, #228]
	cmp	r2, #24
	movcc	r2, #5120
	movcs	r2, #1024
	cmp	r3, r2
	movls	r6, r5
	bls	.L1536
	ldr	r3, .L1659+8
	mov	r8, #0
	str	r8, [r4, #3160]
	strh	r8, [r3]	@ movhi
	bl	GetSwlReplaceBlock
	cmp	r0, r5
	mov	r6, r0
	movne	r5, r0
	bne	.L1538
	movw	r7, #3158
	ldrh	r2, [r4, #228]
	ldrh	r3, [r4, r7]
	cmp	r2, r3
	bcs	.L1539
	mov	r0, #64
	bl	List_get_gc_head_node
	uxth	r3, r0
	cmp	r3, r6
	beq	.L1541
	mov	r0, r3
	ldr	r3, [r4, #76]
	lsl	r0, r0, #1
	ldrh	r3, [r3, r0]
	cmp	r3, #7
	bhi	.L1542
	mov	r0, r8
	bl	List_get_gc_head_node
	uxth	r5, r0
	mov	r3, #128
	strh	r3, [r4, r7]	@ movhi
	cmp	r5, r6
	bne	.L1538
.L1541:
	bl	FtlGcReFreshBadBlk
.L1536:
	movw	r1, #65535
	ldr	r2, [fp, #-64]
	sub	r3, r6, r1
	clz	r3, r3
	lsr	r3, r3, #5
	cmp	r2, #0
	movne	r2, #0
	andeq	r2, r3, #1
	cmp	r2, #0
	beq	.L1544
	ldrh	r3, [r4, #228]
	cmp	r3, #24
	movhi	r5, #1
	bhi	.L1545
	movw	r2, #306
	cmp	r3, #16
	ldrh	r5, [r4, r2]
	lsrhi	r5, r5, #5
	bhi	.L1545
	cmp	r3, #12
	lsrhi	r5, r5, #4
	bhi	.L1545
	cmp	r3, #8
	lsrhi	r5, r5, #2
.L1545:
	movw	r2, #3156
	ldrh	r1, [r4, r2]
	cmp	r1, r3
	bcs	.L1549
	ldrh	r3, [r4, #128]
	movw	r1, #65535
	cmp	r3, r1
	bne	.L1550
	ldrh	r1, [r4, #226]
	cmp	r1, r3
	bne	.L1550
	ldr	r3, .L1659+8
	ldrh	r0, [r3]
	cmp	r0, #0
	bne	.L1551
	ldr	r3, [r4, #2556]
	ldr	r1, [r4, #2552]
	add	r3, r3, r3, lsl #1
	cmp	r1, r3, lsr #2
	movcs	r3, #18
	bcs	.L1652
.L1551:
	ldr	r3, .L1659+12
	movw	r2, #3156
	ldrh	r3, [r3]
	add	r3, r3, r3, lsl #1
	asr	r3, r3, #2
.L1652:
	strh	r3, [r4, r2]	@ movhi
	mov	r3, #0
	str	r3, [r4, #3168]
.L1530:
	sub	sp, fp, #40
	ldmfd	sp, {r4, r5, r6, r7, r8, r9, r10, fp, sp, pc}
.L1532:
	ldrh	r3, [r3, #128]
	cmp	r3, r2
	beq	.L1535
	mov	r0, #1
	bl	FtlGcFreeTempBlock
	cmp	r0, #0
	beq	.L1535
	mov	r0, #1
	b	.L1530
.L1533:
	ldrh	r1, [r4, #226]
	cmp	r1, r2
	strheq	r3, [r4, #226]	@ movhi
	mvneq	r3, #0
	strheq	r3, [r4, #224]	@ movhi
	b	.L1534
.L1542:
	mov	r3, #64
.L1651:
	strh	r3, [r4, r7]	@ movhi
	b	.L1541
.L1539:
	mov	r3, #80
	b	.L1651
.L1538:
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
	ldr	r0, .L1659+16
	str	r1, [sp]
	mov	r1, r5
	bl	sftl_printk
	b	.L1541
.L1550:
	ldr	r3, .L1659+12
	movw	r2, #3156
	ldrh	r3, [r3]
	add	r3, r3, r3, lsl #1
	asr	r3, r3, #2
	strh	r3, [r4, r2]	@ movhi
.L1549:
	movw	r3, #3210
	movw	r6, #65535
	ldrh	r3, [r4, r3]
	cmp	r3, #0
	addne	r5, r5, #32
	uxthne	r5, r5
.L1555:
	ldrh	r3, [r4, #176]
	movw	r2, #65535
	cmp	r3, r2
	bne	.L1564
	cmp	r6, r3
	strhne	r6, [r4, #176]	@ movhi
	bne	.L1566
	ldrh	r3, [r4, #226]
	cmp	r3, r6
	beq	.L1566
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
.L1566:
	ldrh	r0, [r4, #176]
	mov	r3, #0
	strb	r3, [r4, #184]
	movw	r3, #65535
	cmp	r0, r3
	beq	.L1564
	bl	IsBlkInGcList
	cmp	r0, #0
	mvnne	r3, #0
	strhne	r3, [r4, #176]	@ movhi
	movw	r3, #65535
	ldrh	r2, [r4, #176]
	cmp	r2, r3
	beq	.L1564
	ldr	r0, .L1659+20
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
.L1564:
	ldrh	r3, [r4, #176]
	ldrh	r2, [r4, #28]
	cmp	r2, r3
	beq	.L1570
	ldrh	r2, [r4, #80]
	cmp	r2, r3
	beq	.L1570
	ldrh	r2, [r4, #128]
	cmp	r2, r3
	bne	.L1571
.L1570:
	mvn	r3, #0
	strh	r3, [r4, #176]	@ movhi
.L1571:
	ldr	r4, .L1659
	mov	r7, r4
.L1606:
	ldrh	r2, [r4, #176]
	movw	r3, #65535
	cmp	r2, r3
	bne	.L1572
	ldr	r10, .L1659+24
	mov	r3, #0
	str	r3, [r4, #3168]
	mov	r9, r10
.L1573:
	ldrh	r8, [r10]
	mov	r0, r8
	bl	List_get_gc_head_node
	uxth	r2, r0
	movw	r3, #65535
	cmp	r2, r3
	strh	r2, [r7, #176]	@ movhi
	bne	.L1574
	mov	r3, #0
	mov	r0, #8
	strh	r3, [r10]	@ movhi
	b	.L1530
.L1544:
	ldrh	r7, [r4, #128]
	cmp	r7, r1
	bne	.L1620
	ldrh	r1, [r4, #226]
	cmp	r1, r7
	movne	r3, #0
	andeq	r3, r3, #1
	cmp	r3, #0
	beq	.L1620
	ldrh	r3, [r4, #176]
	cmp	r3, r7
	beq	.L1556
.L1561:
	mov	r6, r7
.L1620:
	mov	r5, #1
	b	.L1555
.L1556:
	str	r2, [r4, #3168]
	movw	r2, #3156
	ldrh	r1, [r4, #228]
	ldrh	r3, [r4, r2]
	ldr	r5, .L1659+8
	cmp	r1, r3
	bls	.L1557
	ldrh	r3, [r5]
	cmp	r3, #0
	bne	.L1558
	ldr	r3, [r4, #2556]
	ldr	r1, [r4, #2552]
	add	r3, r3, r3, lsl #1
	cmp	r1, r3, lsr #2
	movcs	r3, #18
	bcs	.L1653
.L1558:
	ldr	r3, .L1659+12
	movw	r2, #3156
	ldrh	r3, [r3]
	add	r3, r3, r3, lsl #1
	asr	r3, r3, #2
.L1653:
	strh	r3, [r4, r2]	@ movhi
	bl	FtlReadRefresh
	mov	r0, #0
	bl	List_get_gc_head_node
	uxth	r0, r0
	ldr	r3, [r4, #76]
	lsl	r0, r0, #1
	ldrh	r3, [r3, r0]
	cmp	r3, #4
	bls	.L1557
.L1656:
	ldrh	r0, [r5]
	b	.L1530
.L1557:
	ldrh	r0, [r5]
	cmp	r0, #0
	bne	.L1561
	ldr	r3, .L1659+12
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
	ldr	r3, .L1659+28
	ldrh	r1, [r3]
	ldrh	r3, [r4, #236]
	mul	r3, r3, r1
	add	r3, r3, r3, lsr #31
	cmp	r2, r3, asr #1
	ble	.L1562
	ldrh	r3, [r4, #228]
	sub	r6, r6, #1
	cmp	r3, r6
	blt	.L1562
	bl	FtlReadRefresh
	b	.L1656
.L1562:
	cmp	r2, #0
	bne	.L1561
	movw	r0, #65535
	bl	decrement_vpc_count
	ldrh	r0, [r4, #228]
	add	r0, r0, #1
	b	.L1530
.L1574:
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
	bne	.L1573
	lsl	r0, r3, #1
	ldr	r3, .L1659+32
	ldrh	lr, [r7, #236]
	uxth	r8, r8
	ldr	r1, [r7, #76]
	ldrh	r3, [r3]
	strh	r8, [r10]	@ movhi
	ldrh	ip, [r1, r0]
	mul	r3, lr, r3
	add	lr, r3, r3, lsr #31
	cmp	ip, lr, asr #1
	bgt	.L1577
	cmp	r8, #48
	cmphi	ip, #8
	bls	.L1578
	ldr	ip, .L1659+36
	ldrh	ip, [ip]
	cmp	ip, #35
	bhi	.L1578
.L1577:
	mov	ip, #0
	strh	ip, [r9]	@ movhi
.L1578:
	ldrh	r1, [r1, r0]
	movw	r0, #65535
	cmp	r3, r1
	cmple	r6, r0
	bne	.L1579
	ldrh	r3, [r9]
	cmp	r3, #3
	bhi	.L1579
	mvn	r3, #0
	strh	r3, [r7, #176]	@ movhi
	mov	r3, #0
	strh	r3, [r9]	@ movhi
.L1658:
	ldr	r3, .L1659+8
	b	.L1657
.L1579:
	cmp	r1, #0
	bne	.L1580
	movw	r0, #65535
	bl	decrement_vpc_count
	ldrh	r3, [r9]
	add	r3, r3, #1
	strh	r3, [r9]	@ movhi
	b	.L1573
.L1580:
	mov	r3, #0
	strb	r3, [r7, #184]
	ldrh	r3, [r7, #28]
	cmp	r3, r2
	bne	.L1581
	movw	r2, #717
	ldr	r1, .L1659+40
	ldr	r0, .L1659+44
	bl	sftl_printk
.L1581:
	ldrh	r2, [r7, #176]
	ldrh	r3, [r7, #80]
	cmp	r2, r3
	bne	.L1582
	movw	r2, #718
	ldr	r1, .L1659+40
	ldr	r0, .L1659+44
	bl	sftl_printk
.L1582:
	ldrh	r2, [r7, #176]
	ldrh	r3, [r7, #128]
	cmp	r2, r3
	bne	.L1583
	movw	r2, #719
	ldr	r1, .L1659+40
	ldr	r0, .L1659+44
	bl	sftl_printk
.L1583:
	ldr	r0, .L1659+20
	bl	make_superblock
	ldrh	r2, [r7, #176]
	mov	r3, #0
	ldr	r1, .L1659+48
	ldr	r0, [r7, #76]
	lsl	r2, r2, #1
	strh	r3, [r1]	@ movhi
	ldrh	r2, [r0, r2]
	strh	r3, [r7, #178]	@ movhi
	strb	r3, [r7, #182]
	strh	r2, [r1, #2]	@ movhi
.L1572:
	mov	r3, #1
	str	r3, [r7, #3276]
	ldr	r3, .L1659+32
	ldrh	r3, [r3]
	str	r3, [fp, #-52]
	ldr	r3, [fp, #-64]
	cmp	r3, #0
	beq	.L1584
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
.L1584:
	ldrh	r3, [r7, #178]
	ldr	r1, [fp, #-52]
	add	r2, r3, r5
	cmp	r2, r1
	movgt	r2, r1
	subgt	r5, r2, r3
	mov	r3, #0
	uxthgt	r5, r5
.L1655:
	str	r3, [fp, #-60]
	ldrh	r3, [fp, #-60]
	cmp	r5, r3
	bls	.L1594
	ldr	r3, [fp, #-60]
	mov	lr, #20
	ldrh	r1, [r4, #178]
	ldrh	r9, [r4, #236]
	ldr	r0, [r4, #3188]
	add	r1, r1, r3
	mov	r3, #0
	ldr	ip, .L1659+52
	mov	r8, r3
	b	.L1595
.L1588:
	ldrh	r2, [ip, #2]!
	movw	r10, #65535
	add	r3, r3, #1
	cmp	r2, r10
	mlane	r10, lr, r8, r0
	addne	r8, r8, #1
	orrne	r2, r1, r2, lsl #10
	uxthne	r8, r8
	strne	r2, [r10, #4]
.L1595:
	uxth	r2, r3
	cmp	r9, r2
	bhi	.L1588
	ldrb	r2, [r4, #184]	@ zero_extendqisi2
	mov	r1, r8
	bl	FlashReadPages
	mov	r3, #0
.L1654:
	str	r3, [fp, #-56]
	ldrh	r3, [fp, #-56]
	cmp	r8, r3
	ldrls	r3, [fp, #-60]
	addls	r3, r3, #1
	bls	.L1655
.L1593:
	ldr	r3, [fp, #-56]
	mov	r9, #20
	mul	r9, r9, r3
	ldr	r3, [r4, #3188]
	add	r2, r3, r9
	ldr	r3, [r3, r9]
	cmn	r3, #1
	beq	.L1590
	ldr	r10, [r2, #12]
	movw	r2, #61589
	ldrh	r1, [r10]
	cmp	r1, r2
	bne	.L1590
	ldr	ip, [r10, #8]
	cmn	ip, #1
	bne	.L1591
	movw	r2, #753
	ldr	r1, .L1659+40
	ldr	r0, .L1659+44
	str	ip, [fp, #-68]
	bl	sftl_printk
	ldr	ip, [fp, #-68]
.L1591:
	mov	r2, #0
	sub	r1, fp, #48
	mov	r0, ip
	bl	log2phys
	ldr	r2, [r4, #3188]
	ldr	r1, [fp, #-48]
	add	r2, r2, r9
	ldr	r0, [r2, #4]
	cmp	r0, r1
	bne	.L1590
	ldr	r3, .L1659+48
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
	beq	.L1592
	ldrh	r3, [r4, #132]
	cmp	r3, #0
	bne	.L1590
.L1592:
	bl	Ftl_gc_temp_data_write_back
	cmp	r0, #0
	beq	.L1590
	ldr	r3, .L1659
	mov	r2, #0
	mvn	r1, #0
	str	r2, [r3, #3276]
	strh	r1, [r3, #176]	@ movhi
	strh	r2, [r3, #178]	@ movhi
	add	r3, r3, #3664
.L1657:
	ldrh	r0, [r3]
	b	.L1530
.L1590:
	ldr	r3, [fp, #-56]
	add	r3, r3, #1
	b	.L1654
.L1594:
	ldrh	r3, [r4, #178]
	add	r5, r5, r3
	ldr	r3, [fp, #-52]
	uxth	r5, r5
	cmp	r3, r5
	strh	r5, [r4, #178]	@ movhi
	bhi	.L1596
	ldr	r3, [r4, #3172]
	cmp	r3, #0
	beq	.L1597
	bl	Ftl_gc_temp_data_write_back
	cmp	r0, #0
	movne	r3, #0
	strne	r3, [r4, #3276]
	bne	.L1658
.L1597:
	ldr	r3, .L1659+48
	ldrh	r5, [r3]
	cmp	r5, #0
	bne	.L1598
	ldrh	r3, [r4, #176]
	ldr	r2, [r4, #76]
	lsl	r3, r3, #1
	ldrh	r3, [r2, r3]
	cmp	r3, #0
	beq	.L1598
.L1599:
	ldr	r3, [r4, #2556]
	cmp	r5, r3
	bcs	.L1604
	mov	r2, #0
	sub	r1, fp, #44
	mov	r0, r5
	bl	log2phys
	ldr	r0, [fp, #-44]
	cmn	r0, #1
	beq	.L1600
	ubfx	r0, r0, #10, #16
	bl	P2V_block_in_plane
	ldrh	r3, [r4, #176]
	cmp	r3, r0
	bne	.L1600
.L1604:
	ldr	r3, [r4, #2556]
	cmp	r5, r3
	bcc	.L1598
	ldrh	r3, [r4, #176]
	mov	r1, #0
	ldr	r2, [r4, #76]
	lsl	r3, r3, #1
	strh	r1, [r2, r3]	@ movhi
	ldrh	r0, [r4, #176]
	bl	update_vpc_list
	bl	l2p_flush
	bl	FtlVpcTblFlush
.L1598:
	mvn	r3, #0
	strh	r3, [r4, #176]	@ movhi
.L1596:
	mov	r3, #0
	str	r3, [r4, #3276]
	ldrh	r3, [r4, #228]
	cmp	r3, #2
	bhi	.L1605
	ldr	r3, .L1659+32
	ldrh	r5, [r3]
	b	.L1606
.L1600:
	add	r5, r5, #1
	b	.L1599
.L1605:
	ldr	r2, .L1659+8
	ldrh	r0, [r2]
	cmp	r0, #0
	addeq	r0, r3, #1
	b	.L1530
.L1660:
	.align	2
.L1659:
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
	bne	.L1662
	mov	r2, r3
	ldr	r1, [fp, #-52]
	add	r0, r6, #256
	bl	FtlVendorPartRead
	str	r0, [fp, #-48]
.L1661:
	ldr	r0, [fp, #-48]
	sub	sp, fp, #40
	ldmfd	sp, {r4, r5, r6, r7, r8, r9, r10, fp, sp, pc}
.L1662:
	ldr	r3, [fp, #-52]
	ldr	r10, .L1697
	add	r3, r1, r3
	str	r3, [fp, #-60]
	ldr	r2, [fp, #-60]
	ldr	r3, [r10, #344]
	cmp	r2, r3
	mvnhi	r3, #0
	strhi	r3, [fp, #-48]
	bhi	.L1661
	ldr	r3, .L1697+4
	ldr	r7, [r3]
	cmn	r7, #1
	streq	r7, [fp, #-48]
	beq	.L1661
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
.L1664:
	ldr	r3, [fp, #-56]
	cmp	r3, #0
	bne	.L1681
	ldr	r3, .L1697
	movw	r2, #3210
	ldrh	r2, [r3, r2]
	cmp	r2, #0
	bne	.L1682
	ldrh	r3, [r3, #228]
	cmp	r3, #31
	bhi	.L1661
.L1682:
	mov	r1, #1
	mov	r0, #0
	bl	rk_ftl_garbage_collect
	b	.L1661
.L1681:
	mov	r2, #0
	sub	r1, fp, #44
	mov	r0, r4
	bl	log2phys
	ldr	r2, [fp, #-44]
	cmn	r2, #1
	moveq	r10, #0
	ldreq	ip, .L1697+8
	beq	.L1666
	ldr	r1, [r7, #3292]
	mov	r10, #20
	ldr	r3, [fp, #-64]
	mla	r10, r10, r5, r1
	cmp	r4, r3
	str	r2, [r10, #4]
	ldr	r2, .L1697+8
	bne	.L1670
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
.L1671:
	ldr	r3, .L1697+12
	ldr	r1, [r7, #3340]
	str	r4, [r10, #16]
	ldrh	r2, [r3]
	mul	r2, r5, r2
	add	r5, r5, #1
	bic	r2, r2, #3
	add	r2, r1, r2
	str	r2, [r10, #12]
	b	.L1669
.L1668:
	mla	r0, r0, r4, r10
	ldr	r3, [fp, #-60]
	cmp	r6, r0
	movls	r2, #1
	movhi	r2, #0
	cmp	r3, r0
	movls	r2, #0
	cmp	r2, #0
	beq	.L1667
	sub	r0, r0, r6
	mov	r2, #512
	mov	r1, #0
	add	r0, r9, r0, lsl #9
	str	ip, [fp, #-80]
	bl	ftl_memset
	ldr	ip, [fp, #-80]
.L1667:
	add	r10, r10, #1
.L1666:
	ldrh	r0, [ip]
	cmp	r10, r0
	bcc	.L1668
.L1669:
	ldr	r3, [fp, #-56]
	add	r4, r4, #1
	subs	r3, r3, #1
	str	r3, [fp, #-56]
	beq	.L1673
	ldrh	r2, [r7, #236]
	cmp	r5, r2, lsl #2
	bne	.L1664
.L1673:
	cmp	r5, #0
	beq	.L1664
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
.L1680:
	mov	ip, #20
	ldr	r2, [r7, #3292]
	mul	ip, ip, r10
	ldr	r3, [fp, #-64]
	add	r2, r2, ip
	ldr	r1, [r2, #16]
	cmp	r3, r1
	bne	.L1675
	ldr	r1, [r2, #8]
	ldr	r2, [r7, #3316]
	cmp	r1, r2
	bne	.L1676
	ldr	r3, [fp, #-80]
	mov	r0, r9
	ldr	r2, [fp, #-84]
	str	ip, [fp, #-92]
	add	r1, r1, r3
.L1696:
	bl	ftl_memcpy
	ldr	ip, [fp, #-92]
.L1676:
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
	bne	.L1679
	ldr	r0, [r2, #4]
	ubfx	r0, r0, #10, #16
	bl	P2V_block_in_plane
	bl	FtlGcRefreshBlock
.L1679:
	add	r10, r10, #1
	cmp	r5, r10
	bne	.L1680
	mov	r5, #0
	b	.L1664
.L1670:
	ldr	r3, [fp, #-68]
	cmp	r4, r3
	ldrhne	r2, [r2]
	mulne	r2, r4, r2
	bne	.L1695
	ldr	r1, [r7, #3320]
	ldr	r3, [fp, #-60]
	str	r1, [r10, #8]
	ldrh	r1, [r2]
	mul	r2, r1, r4
	sub	r8, r3, r2
	cmp	r1, r8
	bne	.L1671
.L1695:
	sub	r2, r2, r6
	add	r2, r9, r2, lsl #9
	str	r2, [r10, #8]
	b	.L1671
.L1675:
	ldr	r3, [fp, #-68]
	cmp	r3, r1
	bne	.L1676
	ldr	r1, [r2, #8]
	ldr	r2, [r7, #3320]
	cmp	r1, r2
	bne	.L1676
	ldr	r2, .L1697+8
	str	ip, [fp, #-92]
	ldrh	r0, [r2]
	ldr	r2, [fp, #-88]
	mul	r0, r3, r0
	sub	r0, r0, r6
	add	r0, r9, r0, lsl #9
	b	.L1696
.L1698:
	.align	2
.L1697:
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
	bne	.L1702
	mov	r2, r3
	ldr	r1, [fp, #-80]
	add	r0, r9, #256
	bl	FtlVendorPartWrite
.L1701:
	sub	sp, fp, #40
	ldmfd	sp, {r4, r5, r6, r7, r8, r9, r10, fp, sp, pc}
.L1702:
	ldr	r3, [fp, #-80]
	ldr	r4, .L1753
	add	r6, r1, r3
	ldr	r3, [r4, #344]
	cmp	r6, r3
	mvnhi	r0, #0
	bhi	.L1701
	ldr	r5, .L1753+4
	ldr	r0, [r5]
	cmn	r0, #1
	beq	.L1701
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
	beq	.L1704
	ldrh	r3, [r4, #32]
	cmp	r3, #0
	addne	r4, r4, #28
	addeq	r4, r4, #80
.L1704:
	ldr	r6, [fp, #-76]
	ldr	r10, .L1753
.L1705:
	ldr	r3, [fp, #-72]
	cmp	r3, #0
	bne	.L1733
	mov	r0, r3
	ldr	r1, [fp, #-96]
	bl	rk_ftl_garbage_collect
	ldrh	r3, [r10, #228]
	cmp	r3, #5
	movls	r4, #256
	ldrls	r5, .L1753+8
	bls	.L1737
.L1736:
	mov	r0, #0
	b	.L1701
.L1733:
	ldrb	r2, [r4, #6]	@ zero_extendqisi2
	ldrh	r3, [r10, #236]
	cmp	r2, r3
	bcc	.L1706
	movw	r2, #1038
	ldr	r1, .L1753+12
	ldr	r0, .L1753+16
	bl	sftl_printk
.L1706:
	ldrh	r2, [r4, #4]
	cmp	r2, #0
	bne	.L1707
	ldr	r3, .L1753+20
	ldr	r5, .L1753+4
	cmp	r4, r3
	bne	.L1708
	ldrh	r7, [r10, #84]
	cmp	r7, #0
	bne	.L1709
	add	r0, r4, #52
	bl	allocate_new_data_superblock
	str	r7, [r5, #8]
.L1709:
	ldr	r0, .L1753+20
	bl	allocate_new_data_superblock
	ldr	r4, .L1753+20
	ldr	r2, [r5, #8]
	add	r3, r4, #52
	cmp	r2, #0
	movne	r4, r3
.L1710:
	ldrh	r3, [r4, #4]
	cmp	r3, #0
	bne	.L1707
	mov	r0, r4
	bl	allocate_new_data_superblock
.L1707:
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
	bcc	.L1711
	movw	r2, #1071
	ldr	r1, .L1753+12
	ldr	r0, .L1753+16
	bl	sftl_printk
.L1711:
	mov	r3, #0
.L1752:
	str	r3, [fp, #-68]
	ldr	r3, [fp, #-68]
	ldr	r2, [fp, #-108]
	cmp	r3, r2
	bne	.L1731
.L1713:
	mov	r3, r4
	mov	r2, #0
	ldr	r1, [fp, #-68]
	ldr	r0, [r10, #3300]
	bl	FtlProgPages
	ldr	r3, [fp, #-68]
	ldr	r2, [fp, #-72]
	cmp	r3, r2
	bls	.L1732
	movw	r2, #1149
	ldr	r1, .L1753+12
	ldr	r0, .L1753+16
	bl	sftl_printk
.L1732:
	ldr	r3, [fp, #-72]
	ldr	r2, [fp, #-68]
	sub	r3, r3, r2
	str	r3, [fp, #-72]
	b	.L1705
.L1708:
	str	r2, [r5, #8]
	ldrh	r2, [r10, #32]
	cmp	r2, #0
	movne	r4, r3
	bne	.L1707
	mov	r0, r4
	bl	allocate_new_data_superblock
	b	.L1710
.L1731:
	ldrh	r3, [r4, #4]
	cmp	r3, #0
	beq	.L1713
	mov	r2, #0
	sub	r1, fp, #64
	mov	r0, r6
	mov	r7, #20
	bl	log2phys
	mov	r0, r4
	bl	get_new_active_ppa
	ldr	r2, .L1753+24
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
	ldr	r3, .L1753+28
	bne	.L1714
	ldr	r2, [fp, #-76]
	cmp	r6, r2
	bne	.L1715
	ldrh	r5, [r3]
	mov	r0, r9
	mov	r1, r5
	bl	__umodsi3
	ldr	r3, [fp, #-80]
	sub	r5, r5, r0
	str	r0, [fp, #-88]
	cmp	r5, r3
	movcs	r5, r3
.L1716:
	ldr	r3, .L1753+28
	ldrh	r3, [r3]
	cmp	r5, r3
	ldr	r3, [fp, #-76]
	bne	.L1717
	cmp	r6, r3
	ldr	r3, [r10, #3300]
	add	r7, r3, r7
	ldreq	r3, [fp, #-84]
	beq	.L1749
	mul	r5, r5, r6
	ldr	r3, [fp, #-84]
	sub	r5, r5, r9
	add	r5, r3, r5, lsl #9
	str	r5, [r7, #8]
.L1719:
	ldrb	r2, [r4, #6]	@ zero_extendqisi2
	ldrh	r3, [r10, #236]
	cmp	r2, r3
	bcc	.L1728
	movw	r2, #1140
	ldr	r1, .L1753+12
	ldr	r0, .L1753+16
	bl	sftl_printk
.L1728:
	ldr	r3, .L1753+32
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
	b	.L1752
.L1715:
	ldr	r2, [fp, #-80]
	add	r5, r9, r2
	ldrh	r2, [r3]
	mov	r3, #0
	str	r3, [fp, #-88]
	smulbb	r2, r2, r6
	sub	r5, r5, r2
	uxth	r5, r5
	b	.L1716
.L1717:
	cmp	r6, r3
	ldr	r3, [r10, #3300]
	ldreq	r2, [r10, #3316]
	ldrne	r2, [r10, #3320]
	add	r3, r3, r7
	str	r2, [r3, #8]
	ldr	r3, [fp, #-64]
	cmn	r3, #1
	beq	.L1722
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
	bne	.L1723
	ldr	r2, [r10, #2716]
	ldr	r0, .L1753+36
	add	r2, r2, #1
	str	r2, [r10, #2716]
	mov	r2, r6
	ldr	r1, [r8, #8]
	bl	sftl_printk
.L1726:
	ldr	r3, [fp, #-76]
	lsl	r2, r5, #9
	cmp	r6, r3
	bne	.L1727
	ldr	r3, [r10, #3300]
	ldr	r1, [fp, #-84]
	add	r7, r3, r7
	ldr	r3, [fp, #-88]
	ldr	r0, [r7, #8]
	add	r0, r0, r3, lsl #9
.L1750:
	bl	ftl_memcpy
	b	.L1719
.L1723:
	ldr	r3, [r8, #8]
	cmp	r6, r3
	beq	.L1725
	ldr	r3, [r10, #2716]
	mov	r2, r6
	ldr	r0, .L1753+40
	add	r3, r3, #1
	str	r3, [r10, #2716]
	ldr	r1, [r8, #8]
	bl	sftl_printk
.L1725:
	ldr	r3, [r8, #8]
	cmp	r6, r3
	beq	.L1726
	movw	r2, #1125
	ldr	r1, .L1753+12
	ldr	r0, .L1753+16
	bl	sftl_printk
	b	.L1726
.L1722:
	ldr	r3, [r10, #3300]
	mov	r1, #0
	ldr	r2, .L1753+44
	add	r3, r3, r7
	ldrh	r2, [r2]
	ldr	r0, [r3, #8]
	bl	ftl_memset
	b	.L1726
.L1727:
	ldr	r3, .L1753+28
	ldrh	r1, [r3]
	ldr	r3, [r10, #3300]
	mul	r1, r6, r1
	add	r7, r3, r7
	ldr	r3, [fp, #-84]
	ldr	r0, [r7, #8]
	sub	r1, r1, r9
	add	r1, r3, r1, lsl #9
	b	.L1750
.L1714:
	ldrh	r3, [r3]
	ldr	r2, [r10, #3300]
	mul	r3, r6, r3
	add	r7, r2, r7
	ldr	r2, [fp, #-84]
	sub	r3, r3, r9
	add	r3, r2, r3, lsl #9
.L1749:
	str	r3, [r7, #8]
	b	.L1719
.L1737:
	ldrh	r3, [r10, #176]
	movw	r2, #65535
	cmp	r3, r2
	bne	.L1735
	ldrh	r2, [r10, #226]
	cmp	r2, r3
	bne	.L1735
	mov	r0, #0
	bl	List_get_gc_head_node
	uxth	r0, r0
	bl	FtlGcRefreshBlock
.L1735:
	ldr	r2, .L1753+48
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
	bhi	.L1736
	subs	r4, r4, #1
	bne	.L1737
	b	.L1736
.L1754:
	.align	2
.L1753:
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
	ldr	r4, .L1787
	movw	r7, #2628
	ldr	r6, .L1787+4
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
	bne	.L1758
.L1769:
	mvn	r0, #0
.L1757:
	sub	sp, fp, #40
	ldmfd	sp, {r4, r5, r6, r7, r8, r9, r10, fp, sp, pc}
.L1758:
	mov	r1, #1
	ldr	r9, .L1787+8
	bl	FtlGetLastWrittenPage
	ldrsh	r7, [r4, r7]
	add	r8, r4, #3456
	sxth	r5, r0
	add	r0, r0, #1
	strh	r0, [r6, #2]	@ movhi
.L1760:
	cmp	r5, #0
	bge	.L1766
	movw	r2, #1465
	ldr	r1, .L1787+12
	ldr	r0, .L1787+16
	bl	sftl_printk
.L1765:
	movw	r2, #314
	ldrh	r3, [r4, #244]
	ldrh	r2, [r4, r2]
	add	r3, r3, #24
	cmp	r2, r3, lsl #1
	bcs	.L1768
	movw	r2, #1467
	ldr	r1, .L1787+12
	ldr	r0, .L1787+16
	bl	sftl_printk
.L1768:
	ldr	r5, .L1787+20
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
	ldr	r3, .L1787+8
	cmp	r2, r3
	bne	.L1769
	movw	r3, #258
	ldrb	r2, [r4, #2482]	@ zero_extendqisi2
	ldrh	r3, [r4, r3]
	ldrh	r7, [r5, #8]
	cmp	r2, r3
	strh	r7, [r6, #6]	@ movhi
	bne	.L1769
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
	ldr	r3, .L1787+24
	ldrh	r0, [r3, #6]
	sub	r0, r6, r0
	sub	r0, r0, r7
	bl	__udivsi3
	cmp	r7, r6
	strh	r0, [r5, #152]	@ movhi
	bls	.L1770
	movw	r2, #1489
	ldr	r1, .L1787+12
	ldr	r0, .L1787+16
	bl	sftl_printk
.L1770:
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
	beq	.L1773
	ldr	r0, .L1787+28
	bl	make_superblock
.L1773:
	ldrh	r2, [r4, #80]
	movw	r3, #65535
	cmp	r2, r3
	beq	.L1774
	ldr	r0, .L1787+32
	bl	make_superblock
.L1774:
	ldrh	r2, [r4, #128]
	movw	r3, #65535
	cmp	r2, r3
	beq	.L1775
	ldr	r0, .L1787+36
	bl	make_superblock
.L1775:
	ldrh	r2, [r4, #176]
	movw	r3, #65535
	cmp	r2, r3
	beq	.L1776
	ldr	r0, .L1787+40
	bl	make_superblock
.L1776:
	mov	r0, #0
	b	.L1757
.L1766:
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
	beq	.L1761
	ldr	r2, [r4, #3456]
	cmn	r2, #1
	beq	.L1761
	ldr	r10, .L1787+44
	ldr	r0, [r4, #3464]
	ldrh	r1, [r10]
	bl	js_hash
	ldr	r3, [fp, #-44]
	cmp	r3, r0
	beq	.L1761
	str	r0, [sp, #8]
	mov	r2, r7
	str	r3, [sp, #4]
	ldrh	r3, [r6, #4]
	ldr	r1, .L1787+12
	ldr	r0, .L1787+48
	str	r3, [sp]
	mov	r3, r5
	bl	sftl_printk
	cmp	r5, #0
	bne	.L1762
	ldrh	r3, [r6, #4]
	cmp	r7, r3
	sxthne	r7, r3
	ldrhne	r5, [r10, #-6]
	bne	.L1764
.L1762:
	mvn	r3, #0
	str	r3, [r4, #3456]
.L1761:
	ldr	r3, [r4, #3456]
	cmn	r3, #1
	beq	.L1764
	ldr	r3, [r4, #3304]
	ldr	r3, [r3]
	cmp	r3, r9
	bne	.L1764
	ldr	r3, [r4, #3336]
	ldrh	r2, [r3]
	movw	r3, #61604
	cmp	r2, r3
	beq	.L1765
.L1764:
	sub	r5, r5, #1
	sxth	r5, r5
	b	.L1760
.L1788:
	.align	2
.L1787:
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
	ldr	r9, .L1830
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
.L1790:
	ldr	r3, [fp, #-52]
	sxth	r6, r7
	cmp	r6, r3
	bge	.L1809
	ldr	r3, [fp, #-52]
	sub	r3, r3, #1
	cmp	r6, r3
	lsl	r3, r6, #1
	bne	.L1791
	add	r2, r8, r3
	ldrh	r0, [r8, r3]
	mov	r1, #1
	str	r2, [fp, #-52]
	bl	FtlGetLastWrittenPage
	sxth	r3, r0
	ldr	r9, .L1830
	add	r0, r0, #1
	strh	r7, [r4]	@ movhi
	str	r3, [fp, #-60]
	mov	r7, #0
	ldr	r3, [fp, #-56]
	add	r10, r9, #3456
	strh	r0, [r4, #2]	@ movhi
	ldr	r3, [r3, r6, lsl #2]
	str	r3, [r4, #28]
.L1792:
	ldr	r3, [fp, #-60]
	sxth	r8, r7
	add	r2, r3, #1
	cmp	r8, r2
	blt	.L1795
.L1809:
	mov	r0, r4
	bl	ftl_free_no_use_map_blk
	ldr	r3, .L1830+4
	ldrh	r2, [r4, #2]
	ldrh	r3, [r3]
	cmp	r2, r3
	bne	.L1797
	mov	r0, r4
	bl	ftl_map_blk_alloc_new_blk
.L1797:
	mov	r0, r4
	bl	ftl_map_blk_gc
	mov	r0, r4
	bl	ftl_map_blk_gc
	mov	r0, #0
	sub	sp, fp, #40
	ldmfd	sp, {r4, r5, r6, r7, r8, r9, r10, fp, sp, pc}
.L1795:
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
	beq	.L1793
	ldr	r1, [r9, #3456]
	cmn	r1, #1
	beq	.L1793
	ldr	r1, .L1830+8
	ldr	r0, [r9, #3464]
	ldrh	r1, [r1]
	bl	js_hash
	ldr	r2, [fp, #-56]
	cmp	r2, r0
	beq	.L1793
	str	r0, [sp, #4]
	mov	r3, r8
	str	r2, [sp]
	mov	r2, r6
	ldr	r1, .L1830+12
	ldr	r0, .L1830+16
	bl	sftl_printk
	mvn	r3, #0
	str	r3, [r9, #3456]
.L1793:
	ldr	r3, [r9, #3456]
	cmn	r3, #1
	beq	.L1794
	ldrh	r3, [r5, #8]
	ldr	r2, [fp, #-48]
	cmp	r2, r3
	bls	.L1794
	ldrh	r1, [r5]
	ldrh	r2, [r4, #4]
	cmp	r1, r2
	ldreq	r2, [r9, #3460]
	ldreq	r1, [fp, #-44]
	streq	r2, [r1, r3, lsl #2]
.L1794:
	add	r7, r7, #1
	b	.L1792
.L1791:
	ldr	r2, [r9, #3304]
	ldr	r0, .L1830+20
	str	r2, [r9, #3464]
	add	r2, r8, r3
	str	r2, [fp, #-60]
	ldrh	r2, [r8, r3]
	ldr	r3, .L1830+4
	ldrh	r3, [r3]
	sub	r3, r3, #1
	orr	r3, r3, r2, lsl #10
	mov	r2, #1
	mov	r1, r2
	str	r3, [r9, #3460]
	bl	FlashReadPages
	ldr	r3, [r9, #3456]
	cmn	r3, #1
	beq	.L1811
	ldrh	r2, [r5]
	ldrh	r3, [r4, #4]
	cmp	r2, r3
	bne	.L1811
	ldrh	r2, [r5, #8]
	movw	r3, #64245
	cmp	r2, r3
	beq	.L1799
.L1811:
	mov	r10, #0
.L1800:
	ldr	r2, .L1830+4
	sxth	r3, r10
	ldrh	r2, [r2]
	cmp	r3, r2
	bge	.L1807
	ldr	r2, [fp, #-60]
	ldr	r0, .L1830+20
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
	beq	.L1804
	ldr	r1, [r9, #3456]
	cmn	r1, #1
	beq	.L1804
	ldr	r1, .L1830+8
	ldr	r0, [r9, #3464]
	ldrh	r1, [r1]
	bl	js_hash
	ldr	r2, [fp, #-64]
	cmp	r2, r0
	beq	.L1804
	str	r0, [sp, #4]
	str	r2, [sp]
	mov	r2, r6
	ldr	r3, [fp, #-68]
	ldr	r1, .L1830+12
	ldr	r0, .L1830+24
	bl	sftl_printk
	mvn	r3, #0
	str	r3, [r9, #3456]
.L1804:
	ldr	r3, [r9, #3456]
	cmn	r3, #1
	beq	.L1805
	ldrh	r3, [r5, #8]
	ldr	r2, [fp, #-48]
	cmp	r2, r3
	bls	.L1805
	ldrh	r1, [r5]
	ldrh	r2, [r4, #4]
	cmp	r1, r2
	ldreq	r2, [r9, #3460]
	ldreq	r1, [fp, #-44]
	streq	r2, [r1, r3, lsl #2]
.L1805:
	add	r10, r10, #1
	b	.L1800
.L1799:
	ldr	ip, .L1830+4
	mov	r1, #0
	mov	lr, #4
.L1801:
	ldrh	r2, [ip]
	sxth	r3, r1
	sub	r2, r2, #1
	cmp	r3, r2
	blt	.L1803
.L1807:
	add	r7, r7, #1
	b	.L1790
.L1803:
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
	b	.L1801
.L1831:
	.align	2
.L1830:
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
	ldr	r3, .L1833
	add	r2, r3, #324
	add	r0, r3, #3600
	ldrh	r2, [r2]
	add	r0, r0, #12
	strh	r2, [r0, #10]	@ movhi
	ldr	r2, .L1833+4
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
.L1834:
	.align	2
.L1833:
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
	ldr	r0, .L1836
	bl	FtlMapTblRecovery
	mov	r0, #0
	ldmfd	sp, {fp, sp, pc}
.L1837:
	.align	2
.L1836:
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
	ldr	r4, .L1854
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
	bne	.L1839
.L1841:
	mvn	r6, #0
.L1838:
	mov	r0, r6
	ldmfd	sp, {r4, r5, r6, fp, sp, pc}
.L1839:
	bl	FtlLoadSysInfo
	subs	r6, r0, #0
	bne	.L1841
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
.L1842:
	cmp	r3, r1
	bge	.L1847
	mla	ip, r0, r3, r2
	ldr	ip, [ip, #4]
	cmp	ip, #0
	bge	.L1843
.L1847:
	ldr	r5, .L1854+4
	cmp	r3, r1
	ldrh	r2, [r5, #28]
	add	r2, r2, #1
	strh	r2, [r5, #28]	@ movhi
	bge	.L1853
.L1844:
	ldr	r0, .L1854+8
	bl	FtlSuperblockPowerLostFix
	ldr	r0, .L1854+12
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
	b	.L1848
.L1843:
	add	r3, r3, #1
	b	.L1842
.L1853:
	movw	r3, #3476
	ldrh	r3, [r4, r3]
	cmp	r3, #0
	bne	.L1844
.L1848:
	ldrh	r0, [r4, #28]
	movw	r3, #65535
	cmp	r0, r3
	beq	.L1849
	ldrh	r3, [r4, #32]
	cmp	r3, #0
	bne	.L1849
	ldrh	r3, [r4, #84]
	cmp	r3, #0
	bne	.L1849
	bl	FtlGcRefreshBlock
	ldrh	r0, [r4, #80]
	bl	FtlGcRefreshBlock
	bl	FtlVpcTblFlush
	ldr	r0, .L1854+8
	bl	allocate_new_data_superblock
	ldr	r0, .L1854+12
	bl	allocate_new_data_superblock
.L1849:
	bl	FtlVpcCheckAndModify
	b	.L1838
.L1855:
	.align	2
.L1854:
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
	ldr	r4, .L1880
	mov	r3, #0
	ldrh	r0, [r4, #240]
	str	r3, [r4, #2592]
	str	r3, [r4, #2596]
	str	r3, [r4, #2608]
	bl	FtlFreeSysBlkQueueInit
	bl	FtlLoadBbt
	cmp	r0, #0
	beq	.L1857
	bl	FtlMakeBbt
.L1857:
	ldr	r0, .L1880+4
	mov	r2, #0
	ldr	ip, .L1880+8
.L1858:
	ldrh	r1, [r0]
	uxth	r3, r2
	add	r2, r2, #1
	cmp	r3, r1, lsl #7
	blt	.L1859
	ldrh	r6, [r4, #244]
	mov	r5, #0
.L1860:
	ldrh	r3, [r4, #246]
	cmp	r3, r6
	bhi	.L1861
	ldrh	r1, [r4, #236]
	sub	r3, r5, #3
	cmp	r3, r1, lsl #1
	bge	.L1862
.L1866:
	mov	r0, #0
	mov	r7, r0
.L1863:
	ldrh	r2, [r4, #244]
	uxth	r3, r0
	add	r5, r0, #1
	cmp	r2, r3
	bhi	.L1867
	ldrh	r3, [r4, #246]
	ldrh	r5, [r4, #236]
	ldr	r10, [r4, #248]
	str	r3, [r4, #3288]
	mov	r1, r5
	ldr	r6, .L1880+12
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
	ble	.L1868
	mov	r1, r5
	sub	r0, r10, r7
	bl	__udivsi3
	str	r0, [r4, #2556]
	lsr	r0, r0, #5
	add	r0, r0, #24
	strh	r0, [r6]	@ movhi
.L1868:
	movw	r3, #298
	ldrh	r3, [r4, r3]
	cmp	r3, #0
	beq	.L1870
	ldrh	r2, [r6]
	add	r2, r2, r3, lsr #1
	strh	r2, [r6]	@ movhi
	mul	r2, r5, r3
	cmp	r7, r2
	addlt	r3, r3, #32
	strlt	r9, [r4, #2556]
	addlt	r3, r8, r3
	strhlt	r3, [r6]	@ movhi
.L1870:
	ldrh	r2, [r6]
	ldr	r3, [r4, #2556]
	ldr	r6, .L1880+16
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
.L1872:
	mov	r0, r6
	bl	make_superblock
	ldrb	r3, [r4, #35]	@ zero_extendqisi2
	cmp	r3, #0
	ldrh	r3, [r4, #28]
	bne	.L1873
	ldr	r2, [r4, #76]
	lsl	r3, r3, #1
	strh	r5, [r2, r3]	@ movhi
	ldrh	r3, [r4, #28]
	add	r3, r3, #1
	strh	r3, [r4, #28]	@ movhi
	b	.L1872
.L1859:
	ldr	lr, [r4, #3316]
	mvn	r1, r3
	orr	r1, r3, r1, lsl #16
	str	r1, [lr, r3, lsl #2]
	ldr	r1, [r4, #3320]
	str	ip, [r1, r3, lsl #2]
	b	.L1858
.L1861:
	mov	r0, r6
	mov	r1, #1
	bl	FtlLowFormatEraseBlock
	add	r6, r6, #1
	add	r5, r5, r0
	uxth	r5, r5
	uxth	r6, r6
	b	.L1860
.L1862:
	mov	r0, r5
	bl	__udivsi3
	ldr	r3, [r4, #336]
	add	r0, r0, r3
	uxth	r0, r0
	bl	FtlSysBlkNumInit
	ldrh	r0, [r4, #240]
	bl	FtlFreeSysBlkQueueInit
	ldrh	r5, [r4, #244]
.L1864:
	ldrh	r3, [r4, #246]
	cmp	r3, r5
	bls	.L1866
	mov	r0, r5
	mov	r1, #1
	add	r5, r5, #1
	bl	FtlLowFormatEraseBlock
	uxth	r5, r5
	b	.L1864
.L1867:
	mov	r1, #0
	uxth	r0, r0
	bl	FtlLowFormatEraseBlock
	add	r7, r7, r0
	mov	r0, r5
	uxth	r7, r7
	b	.L1863
.L1873:
	ldr	r2, [r4, #2592]
	lsl	r3, r3, #1
	ldrh	r1, [r4, #32]
	mvn	r5, #0
	ldr	r6, .L1880+20
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
.L1874:
	mov	r0, r6
	bl	make_superblock
	ldrb	r3, [r4, #87]	@ zero_extendqisi2
	cmp	r3, #0
	ldrh	r3, [r4, #80]
	bne	.L1875
	ldr	r2, [r4, #76]
	lsl	r3, r3, #1
	strh	r5, [r2, r3]	@ movhi
	ldrh	r3, [r4, #80]
	add	r3, r3, #1
	strh	r3, [r4, #80]	@ movhi
	b	.L1874
.L1875:
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
	ldr	r3, .L1880+24
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
	ldreq	r3, .L1880+28
	moveq	r2, #1
	streq	r2, [r3]
	ldmfd	sp, {r4, r5, r6, r7, r8, r9, r10, fp, sp, pc}
.L1881:
	.align	2
.L1880:
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
	ldr	r4, .L1888
	mvn	r3, #0
	ldr	r5, .L1888+4
	ldr	r1, .L1888+8
	ldr	r0, .L1888+12
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
	bne	.L1886
	bl	FtlSysBlkInit
	cmp	r0, #0
	bne	.L1886
	mov	r3, #1
	str	r3, [r5]
	ldrh	r3, [r4, #228]
	cmp	r3, #15
	bhi	.L1886
	movw	r4, #8129
.L1885:
	mov	r1, #1
	mov	r0, #0
	bl	rk_ftl_garbage_collect
	subs	r4, r4, #1
	bne	.L1885
.L1886:
	mov	r0, #0
	ldmfd	sp, {r4, r5, fp, sp, pc}
.L1889:
	.align	2
.L1888:
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
	movhi	r10, #0
	movls	r10, #1
	cmp	r0, #576
	orrcs	r10, r10, #1
	mov	r7, r2
	cmp	r10, #0
	ldr	r6, .L1952
	beq	.L1891
	ldr	r3, [r6, #3680]
	cmp	r3, #0
	bne	.L1892
.L1951:
	mov	r4, #0
	b	.L1890
.L1892:
	ldr	r3, [r6, #3684]
	ldr	r2, [r3]
	str	r3, [fp, #-112]
	ldr	r3, .L1952+4
	cmp	r2, r3
	bne	.L1894
	ldr	r3, [fp, #-112]
	mov	r2, #0
	ldrh	r1, [r6, #10]
	add	r0, r3, #262144
	movw	r3, #65535
.L1898:
	ldr	ip, [r0, #-4]
	cmp	ip, #0
	bne	.L1895
	ldr	ip, [fp, #-112]
	sub	r3, r3, #1
	ldr	ip, [ip, r2, lsl #2]
	add	r2, r2, #1
	cmp	r2, #4096
	movhi	r2, #0
	cmp	r3, #4096
	str	ip, [r0, #-4]!
	bne	.L1898
	mov	r3, #512
	b	.L1949
.L1895:
	add	r3, r3, #127
	lsr	r3, r3, #7
.L1949:
	str	r3, [fp, #-116]
	lsl	r1, r1, #2
	ldr	r3, [fp, #-116]
	uxth	r1, r1
	ldr	r4, .L1952
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
.L1920:
	mov	r2, #512
	mov	r1, #0
	ldr	r0, [r4, #3688]
	bl	memset
	ldr	r3, [fp, #-108]
	ldrh	r7, [r4, #10]
	mul	r8, r3, r7
	ldr	r3, [r4, #3264]
	cmp	r3, #0
	moveq	r10, #6
	beq	.L1899
	ldr	r3, [r4, #3268]
	cmp	r3, #0
	moveq	r10, #6
	movne	r10, #9
.L1899:
	ldr	r3, [r6, #3252]
	mov	r1, r8
	mov	r0, #0
	blx	r3
	ldr	r3, [fp, #-120]
	cmp	r3, #1
	beq	.L1900
	ldr	r3, [r6, #3252]
	add	r1, r7, r8
	mov	r0, #0
	blx	r3
.L1900:
	cmp	r10, #9
	movne	r5, #0
	bne	.L1901
	ldr	r5, [r6, #3688]
	mov	r2, #1024
	mov	r1, #0
	mov	r0, r5
	bl	ftl_memset
	ldr	r3, .L1952+8
	mov	r1, #12
	str	r1, [r5, #4]
	add	r0, r5, r1
	str	r3, [r5]
	mov	r3, #0
	str	r3, [r5, #12]
	strb	r3, [r5, #16]
	mov	r3, #4
	strb	r3, [r5, #17]
	ldrh	r3, [r6, #10]
	strh	r3, [r5, #18]	@ movhi
	mov	r3, #0
	strb	r3, [r5, #20]
	mov	r3, #16
	strb	r3, [r5, #21]
	mov	r3, #0	@ movhi
	strh	r3, [r5, #22]	@ movhi
	bl	js_hash
	str	r0, [r5, #8]
.L1901:
	ldr	r3, [fp, #-120]
	mov	r9, r8
	mul	r3, r7, r3
	mov	r7, #0
	str	r3, [fp, #-136]
	ldr	r3, [fp, #-112]
	str	r3, [fp, #-128]
.L1902:
	ldr	r3, [fp, #-136]
	cmp	r7, r3
	beq	.L1909
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
	bne	.L1905
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
	bne	.L1906
.L1909:
	ldrb	r3, [r4, #14]	@ zero_extendqisi2
	ldrh	r10, [r4, #10]
	ldr	r7, [r4, #3688]
	str	r3, [fp, #-128]
	ldr	r3, [fp, #-108]
	mul	r9, r3, r10
	ldr	r3, [r4, #3264]
	cmp	r3, #0
	moveq	r8, #6
	beq	.L1908
	ldr	r3, [r4, #3268]
	cmp	r3, #0
	moveq	r8, #6
	movne	r8, #9
.L1908:
	ldr	r3, [fp, #-120]
	mov	r5, #0
	mul	r10, r10, r3
.L1911:
	cmp	r5, r10
	beq	.L1915
	cmp	r5, #0
	cmpeq	r8, #9
	moveq	r0, #1
	movne	r0, #0
	bne	.L1912
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
	ldr	r2, .L1952+8
	cmp	r3, r2
	beq	.L1913
.L1915:
	ldr	r0, [r4, #3688]
	mov	r5, #0
	ldr	r3, [fp, #-112]
	mov	r2, r0
.L1914:
	mov	r7, r2
	mov	r8, r3
	ldr	ip, [r7]
	add	r2, r2, #4
	ldr	r1, [r8]
	add	r3, r3, #4
	cmp	ip, r1
	beq	.L1917
	mov	r2, #512
	mov	r1, #0
	bl	memset
	ldr	r1, [fp, #-108]
	str	r5, [sp]
	ldr	r0, .L1952+12
	ldr	r3, [r8]
	ldr	r2, [r7]
	bl	sftl_printk
	ldrh	r1, [r6, #10]
	mov	r0, #0
	ldr	r2, [fp, #-108]
	ldr	r3, [r6, #3252]
	mul	r1, r2, r1
	blx	r3
.L1918:
	ldr	r3, [fp, #-108]
	ldr	r2, [fp, #-120]
	add	r3, r3, r2
	cmp	r3, #15
	str	r3, [fp, #-108]
	bls	.L1920
	ldr	r3, [fp, #-124]
	cmp	r3, #0
	bne	.L1921
.L1894:
	mvn	r3, #0
	str	r3, [fp, #-116]
.L1921:
	ldr	r5, .L1952
	mov	r3, #0
	ldr	r4, [fp, #-116]
	ldr	r0, [r5, #3684]
	str	r3, [r5, #3680]
	bl	kfree
	ldr	r0, [r5, #3688]
	bl	kfree
.L1890:
	mov	r0, r4
	sub	sp, fp, #40
	ldmfd	sp, {r4, r5, r6, r7, r8, r9, r10, fp, sp, pc}
.L1905:
	ldr	ip, [r4, #3256]
	sub	r3, fp, #104
	ldr	r2, [fp, #-128]
	mov	r1, r9
	blx	ip
	cmn	r0, #1
	beq	.L1909
	ldr	r3, [fp, #-128]
	add	r3, r3, #2048
	str	r3, [fp, #-128]
.L1906:
	add	r7, r7, #1
	add	r9, r9, #1
	b	.L1902
.L1912:
	ldr	ip, [r4, #3260]
	sub	r3, fp, #104
	mov	r2, r7
	add	r1, r9, r5
	blx	ip
	cmn	r0, #1
	beq	.L1915
	ldr	r2, [fp, #-100]
	movw	r3, #61424
	cmp	r2, r3
	bne	.L1915
	add	r7, r7, #2048
.L1913:
	add	r5, r5, #1
	b	.L1911
.L1917:
	ldr	r1, [fp, #-132]
	add	r5, r5, #1
	cmp	r5, r1
	bne	.L1914
	ldr	r3, [fp, #-124]
	add	r3, r3, #1
	cmp	r3, #5
	str	r3, [fp, #-124]
	bls	.L1918
	b	.L1921
.L1891:
	cmp	r0, #64
	bne	.L1922
	mov	r0, #262144
	bl	ftl_malloc
	str	r0, [r6, #3684]
	mov	r0, #262144
	bl	ftl_malloc
	ldr	r3, [r6, #3684]
	str	r0, [r6, #3688]
	cmp	r3, #0
	cmpne	r0, #0
	beq	.L1923
	mov	r2, #1
	mov	r1, r10
	str	r2, [r6, #3680]
	mov	r0, r3
	mov	r2, #262144
	bl	ftl_memset
.L1922:
	ldr	r3, [r6, #3680]
	cmp	r3, #0
	beq	.L1951
	cmp	r5, #63
	ldr	r1, [r6, #3684]
	ldrhi	r6, .L1952+16
	rsbls	r5, r5, #64
	subls	r4, r4, r5
	addls	r7, r7, r5, lsl #9
	movls	r6, r1
	ldr	r0, .L1952+20
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
	b	.L1951
.L1923:
	ldr	r1, .L1952+24
	ldr	r0, .L1952+28
	bl	sftl_printk
	b	.L1922
.L1953:
	.align	2
.L1952:
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
	ldr	r2, .L1958
	ldr	r3, [r2, #3692]
	cmp	r3, #0
	streq	r0, [r2, #3692]
	moveq	r0, r3
	streq	r1, [r2, #3696]
	mvnne	r0, #0
	ldmfd	sp, {fp, sp, pc}
.L1959:
	.align	2
.L1958:
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
	push	{r4, r5, r6, r7, r8, r9, r10, fp, ip, lr, pc}
	sub	fp, ip, #4
	mov	r0, #65536
	ldr	r5, .L1969
	bl	ftl_malloc
	cmp	r0, #0
	str	r0, [r5, #3700]
	mvneq	r9, #11
	beq	.L1960
	ldr	r10, .L1969+4
	mov	r7, #0
	mov	r6, r7
	mov	r8, r7
.L1964:
	ldr	r3, [r5, #3692]
	mov	r1, #128
	ldr	r2, [r5, #3700]
	lsl	r0, r8, #7
	blx	r3
	subs	r9, r0, #0
	bne	.L1962
	ldr	r4, [r5, #3700]
	ldr	r3, [r4]
	cmp	r3, r10
	bne	.L1963
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
.L1963:
	add	r8, r8, #1
	cmp	r8, #2
	bne	.L1964
	cmp	r6, #0
	beq	.L1965
	ldr	r3, [r5, #3692]
	mov	r2, r4
	mov	r1, #128
	lsl	r0, r7, #7
	blx	r3
	subs	r9, r0, #0
	beq	.L1960
.L1962:
	ldr	r0, [r5, #3700]
	mvn	r9, #0
	bl	kfree
	mov	r3, #0
	str	r3, [r5, #3700]
	b	.L1960
.L1965:
	mov	r2, #65536
	mov	r1, r6
	mov	r0, r4
	bl	memset
	mov	r3, #1
	add	r2, r4, #61440
	str	r3, [r4, #4]
	str	r10, [r4]
	str	r3, [r2, #4092]
	ldr	r3, .L1969+8
	strh	r6, [r4, #12]	@ movhi
	strh	r3, [r4, #14]	@ movhi
.L1960:
	mov	r0, r9
	ldmfd	sp, {r4, r5, r6, r7, r8, r9, r10, fp, sp, pc}
.L1970:
	.align	2
.L1969:
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
	ldr	r3, .L1977
	mov	r4, r0
	mov	r0, r1
	ldr	ip, [r3, #3700]
	cmp	ip, #0
	ldrhne	lr, [ip, #10]
	movne	r3, #0
	bne	.L1973
.L1976:
	mvn	r0, #0
	ldmfd	sp, {r4, r5, fp, sp, pc}
.L1974:
	add	r3, r3, #1
.L1973:
	cmp	r3, lr
	bcs	.L1976
	add	r1, ip, r3, lsl #3
	ldrh	r5, [r1, #16]
	cmp	r5, r4
	bne	.L1974
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
.L1978:
	.align	2
.L1977:
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
	ldr	r8, .L1998
	mov	r9, r2
	ldr	r4, [r8, #3700]
	cmp	r4, #0
	beq	.L1994
	ldrh	r2, [r4, #10]
	add	r7, r9, #63
	ldrh	r3, [r4, #8]
	bic	r7, r7, #63
	mov	r5, #0
	str	r3, [fp, #-44]
.L1981:
	cmp	r5, r2
	bcc	.L1989
	ldrh	r1, [r4, #14]
	cmp	r7, r1
	bhi	.L1994
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
	b	.L1997
.L1989:
	add	r6, r4, r5, lsl #3
	ldrh	r3, [r6, #16]
	cmp	r3, r0
	str	r3, [fp, #-48]
	bne	.L1982
	ldrh	r1, [r6, #20]
	add	r3, r4, #1024
	add	r1, r1, #63
	bic	r1, r1, #63
	cmp	r9, r1
	str	r1, [fp, #-52]
	bls	.L1983
	ldrh	r1, [r4, #14]
	cmp	r7, r1
	bhi	.L1994
	ldrh	r8, [r6, #18]
	sub	r2, r2, #1
	str	r2, [fp, #-56]
.L1984:
	ldr	r2, [fp, #-56]
	add	r6, r6, #8
	cmp	r5, r2
	bcc	.L1985
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
.L1986:
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
	ldr	r3, .L1998
	ldr	r3, [r3, #3696]
.L1997:
	ldr	r0, [fp, #-44]
	mov	r2, r4
	mov	r1, #128
	lsl	r0, r0, #7
	blx	r3
	mov	r0, #0
.L1979:
	sub	sp, fp, #40
	ldmfd	sp, {r4, r5, r6, r7, r8, r9, r10, fp, sp, pc}
.L1985:
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
	b	.L1984
.L1983:
	add	r5, r4, r5, lsl #3
	mov	r2, r9
	mov	r1, ip
	ldrh	r0, [r5, #18]
	add	r0, r3, r0
	bl	memcpy
	strh	r9, [r6, #20]	@ movhi
	b	.L1986
.L1982:
	add	r5, r5, #1
	b	.L1981
.L1994:
	mvn	r0, #0
	b	.L1979
.L1999:
	.align	2
.L1998:
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
	push	{r4, r5, r6, r7, fp, ip, lr, pc}
	sub	fp, ip, #4
	mov	r0, #4096
	mov	r5, r2
	mov	r6, r1
	bl	ftl_malloc
	subs	r4, r0, #0
	mvneq	r5, #0
	beq	.L2000
	ldr	r3, .L2024
	cmp	r6, r3
	beq	.L2003
	add	r3, r3, #1
	cmp	r6, r3
	beq	.L2004
.L2022:
	mvn	r5, #13
	b	.L2002
.L2003:
	ldr	r6, .L2024+4
	mov	r3, sp
	and	r2, r3, r6
	ldr	r3, [r2, #8]
	.syntax divided
@ 114 "include/linux/uaccess.h" 1
	adds r1, r5, #8; sbcccs r1, r1, r3; movcc r3, #0
@ 0 "" 2
	.arm
	.syntax unified
	cmp	r3, #0
	bne	.L2018
	.syntax divided
@ 92 "./arch/arm/include/asm/domain.h" 1
	mrc	p15, 0, r7, c3, c0	@ get domain
@ 0 "" 2
	.arm
	.syntax unified
	bic	r3, r7, #12
	orr	r3, r3, #4
	.syntax divided
@ 102 "./arch/arm/include/asm/domain.h" 1
	mcr	p15, 0, r3, c3, c0	@ set domain
@ 0 "" 2
@ 105 "./arch/arm/include/asm/domain.h" 1
	isb 
@ 0 "" 2
	.arm
	.syntax unified
	mov	r2, #8
	mov	r1, r5
	bl	arm_copy_from_user
	mov	r2, r0
	.syntax divided
@ 102 "./arch/arm/include/asm/domain.h" 1
	mcr	p15, 0, r7, c3, c0	@ set domain
@ 0 "" 2
@ 105 "./arch/arm/include/asm/domain.h" 1
	isb 
@ 0 "" 2
	.arm
	.syntax unified
	cmp	r0, #0
	beq	.L2006
.L2005:
	rsb	r0, r2, #8
	mov	r1, #0
	add	r0, r4, r0
	bl	memset
	mov	r2, r5
	mov	r1, #256
.L2023:
	ldr	r0, .L2024+8
	bl	sftl_printk
	b	.L2022
.L2018:
	mov	r2, #8
	b	.L2005
.L2007:
	mov	r3, sp
	uxth	r2, r0
	and	r6, r6, r3
	strh	r0, [r4, #6]	@ movhi
	add	r2, r2, #8
	ldr	r3, [r6, #8]
	.syntax divided
@ 132 "include/linux/uaccess.h" 1
	adds r1, r5, r2; sbcccs r1, r1, r3; movcc r3, #0
@ 0 "" 2
	.arm
	.syntax unified
	cmp	r3, #0
	bne	.L2022
	.syntax divided
@ 92 "./arch/arm/include/asm/domain.h" 1
	mrc	p15, 0, r6, c3, c0	@ get domain
@ 0 "" 2
	.arm
	.syntax unified
	bic	r3, r6, #12
	orr	r3, r3, #4
	.syntax divided
@ 102 "./arch/arm/include/asm/domain.h" 1
	mcr	p15, 0, r3, c3, c0	@ set domain
@ 0 "" 2
@ 105 "./arch/arm/include/asm/domain.h" 1
	isb 
@ 0 "" 2
	.arm
	.syntax unified
	mov	r1, r4
	mov	r0, r5
	bl	arm_copy_to_user
	.syntax divided
@ 102 "./arch/arm/include/asm/domain.h" 1
	mcr	p15, 0, r6, c3, c0	@ set domain
@ 0 "" 2
@ 105 "./arch/arm/include/asm/domain.h" 1
	isb 
@ 0 "" 2
	.arm
	.syntax unified
	cmp	r0, #0
	bne	.L2022
.L2019:
	mov	r5, r0
	b	.L2002
.L2004:
	ldr	r7, .L2024+4
	mov	r3, sp
	and	r2, r3, r7
	ldr	r3, [r2, #8]
	.syntax divided
@ 114 "include/linux/uaccess.h" 1
	adds r1, r5, #8; sbcccs r1, r1, r3; movcc r3, #0
@ 0 "" 2
	.arm
	.syntax unified
	cmp	r3, #0
	bne	.L2020
	.syntax divided
@ 92 "./arch/arm/include/asm/domain.h" 1
	mrc	p15, 0, r6, c3, c0	@ get domain
@ 0 "" 2
	.arm
	.syntax unified
	bic	r3, r6, #12
	orr	r3, r3, #4
	.syntax divided
@ 102 "./arch/arm/include/asm/domain.h" 1
	mcr	p15, 0, r3, c3, c0	@ set domain
@ 0 "" 2
@ 105 "./arch/arm/include/asm/domain.h" 1
	isb 
@ 0 "" 2
	.arm
	.syntax unified
	mov	r2, #8
	mov	r1, r5
	bl	arm_copy_from_user
	mov	r2, r0
	.syntax divided
@ 102 "./arch/arm/include/asm/domain.h" 1
	mcr	p15, 0, r6, c3, c0	@ set domain
@ 0 "" 2
@ 105 "./arch/arm/include/asm/domain.h" 1
	isb 
@ 0 "" 2
	.arm
	.syntax unified
	cmp	r0, #0
	beq	.L2010
.L2009:
	rsb	r0, r2, #8
	mov	r1, #0
	add	r0, r4, r0
	bl	memset
	mov	r2, r5
	mov	r1, #276
	b	.L2023
.L2020:
	mov	r2, #8
	b	.L2009
.L2021:
	mov	r2, r6
	b	.L2012
.L2006:
	ldr	r2, [r4]
	ldr	r3, .L2024+12
	cmp	r2, r3
	bne	.L2011
	ldrh	r2, [r4, #6]
	add	r1, r4, #8
	ldrh	r0, [r4, #4]
	bl	rk_sftl_vendor_read
	cmn	r0, #1
	bne	.L2007
.L2011:
	mvn	r5, #0
.L2002:
	mov	r0, r4
	bl	kfree
.L2000:
	mov	r0, r5
	ldmfd	sp, {r4, r5, r6, r7, fp, sp, pc}
.L2010:
	ldr	r2, [r4]
	ldr	r3, .L2024+12
	cmp	r2, r3
	bne	.L2011
	ldrh	r6, [r4, #6]
	movw	r3, #4087
	cmp	r6, r3
	bhi	.L2011
	mov	r3, sp
	add	r6, r6, #8
	and	r7, r7, r3
	ldr	r3, [r7, #8]
	.syntax divided
@ 114 "include/linux/uaccess.h" 1
	adds r2, r5, r6; sbcccs r2, r2, r3; movcc r3, #0
@ 0 "" 2
	.arm
	.syntax unified
	cmp	r3, #0
	bne	.L2021
	.syntax divided
@ 92 "./arch/arm/include/asm/domain.h" 1
	mrc	p15, 0, r7, c3, c0	@ get domain
@ 0 "" 2
	.arm
	.syntax unified
	bic	r3, r7, #12
	orr	r3, r3, #4
	.syntax divided
@ 102 "./arch/arm/include/asm/domain.h" 1
	mcr	p15, 0, r3, c3, c0	@ set domain
@ 0 "" 2
@ 105 "./arch/arm/include/asm/domain.h" 1
	isb 
@ 0 "" 2
	.arm
	.syntax unified
	mov	r2, r6
	mov	r1, r5
	mov	r0, r4
	bl	arm_copy_from_user
	mov	r2, r0
	.syntax divided
@ 102 "./arch/arm/include/asm/domain.h" 1
	mcr	p15, 0, r7, c3, c0	@ set domain
@ 0 "" 2
@ 105 "./arch/arm/include/asm/domain.h" 1
	isb 
@ 0 "" 2
	.arm
	.syntax unified
	cmp	r0, #0
	beq	.L2013
.L2012:
	sub	r6, r6, r2
	mov	r1, #0
	add	r0, r4, r6
	bl	memset
	mov	r2, r5
	movw	r1, #283
	b	.L2023
.L2013:
	ldrh	r2, [r4, #6]
	add	r1, r4, #8
	ldrh	r0, [r4, #4]
	bl	rk_sftl_vendor_write
	b	.L2019
.L2025:
	.align	2
.L2024:
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
	ldr	r0, .L2027
	bl	misc_register
	ldmfd	sp, {fp, sp, pc}
.L2028:
	.align	2
.L2027:
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
	.type	__func__.4819, %object
	.size	__func__.4819, 17
__func__.4819:
	.ascii	"INSERT_DATA_LIST\000"
	.type	__func__.4814, %object
	.size	__func__.4814, 17
__func__.4814:
	.ascii	"INSERT_FREE_LIST\000"
	.type	__func__.4850, %object
	.size	__func__.4850, 17
__func__.4850:
	.ascii	"List_remove_node\000"
	.type	__func__.4882, %object
	.size	__func__.4882, 22
__func__.4882:
	.ascii	"List_update_data_list\000"
	.type	__func__.4989, %object
	.size	__func__.4989, 22
__func__.4989:
	.ascii	"select_l2p_ram_region\000"
	.type	__func__.5316, %object
	.size	__func__.5316, 16
__func__.5316:
	.ascii	"make_superblock\000"
	.type	__func__.5491, %object
	.size	__func__.5491, 19
__func__.5491:
	.ascii	"get_new_active_ppa\000"
	.type	__func__.10740, %object
	.size	__func__.10740, 17
__func__.10740:
	.ascii	"FlashEraseBlocks\000"
	.type	__func__.4924, %object
	.size	__func__.4924, 26
__func__.4924:
	.ascii	"ftl_map_blk_alloc_new_blk\000"
	.type	__func__.4429, %object
	.size	__func__.4429, 11
__func__.4429:
	.ascii	"FtlMemInit\000"
	.type	__func__.4656, %object
	.size	__func__.4656, 14
__func__.4656:
	.ascii	"FtlBbt2Bitmap\000"
	.type	__func__.5337, %object
	.size	__func__.5337, 18
__func__.5337:
	.ascii	"SupperBlkListInit\000"
	.type	__func__.10697, %object
	.size	__func__.10697, 15
__func__.10697:
	.ascii	"FlashReadPages\000"
	.type	__func__.5101, %object
	.size	__func__.5101, 14
__func__.5101:
	.ascii	"FtlScanSysBlk\000"
	.type	__func__.4699, %object
	.size	__func__.4699, 11
__func__.4699:
	.ascii	"FtlLoadBbt\000"
	.type	__func__.10716, %object
	.size	__func__.10716, 15
__func__.10716:
	.ascii	"FlashProgPages\000"
	.type	__func__.5463, %object
	.size	__func__.5463, 25
__func__.5463:
	.ascii	"allocate_data_superblock\000"
	.type	__func__.5504, %object
	.size	__func__.5504, 16
__func__.5504:
	.ascii	"update_vpc_list\000"
	.type	__func__.5511, %object
	.size	__func__.5511, 20
__func__.5511:
	.ascii	"decrement_vpc_count\000"
	.type	__func__.4950, %object
	.size	__func__.4950, 31
__func__.4950:
	.ascii	"Ftl_write_map_blk_to_last_page\000"
	.type	__func__.4964, %object
	.size	__func__.4964, 16
__func__.4964:
	.ascii	"FtlMapWritePage\000"
	.type	__func__.4891, %object
	.size	__func__.4891, 16
__func__.4891:
	.ascii	"load_l2p_region\000"
	.type	__func__.4935, %object
	.size	__func__.4935, 15
__func__.4935:
	.ascii	"ftl_map_blk_gc\000"
	.type	__func__.5006, %object
	.size	__func__.5006, 9
__func__.5006:
	.ascii	"log2phys\000"
	.type	__func__.5225, %object
	.size	__func__.5225, 16
__func__.5225:
	.ascii	"FtlReUsePrevPpa\000"
	.type	__func__.5259, %object
	.size	__func__.5259, 22
__func__.5259:
	.ascii	"FtlRecoverySuperblock\000"
	.type	__func__.5378, %object
	.size	__func__.5378, 14
__func__.5378:
	.ascii	"ftl_check_vpc\000"
	.type	__func__.5079, %object
	.size	__func__.5079, 15
__func__.5079:
	.ascii	"FtlVpcTblFlush\000"
	.type	__func__.5362, %object
	.size	__func__.5362, 21
__func__.5362:
	.ascii	"FtlVpcCheckAndModify\000"
	.type	__func__.5484, %object
	.size	__func__.5484, 29
__func__.5484:
	.ascii	"allocate_new_data_superblock\000"
	.type	__func__.4553, %object
	.size	__func__.4553, 13
__func__.4553:
	.ascii	"FtlProgPages\000"
	.type	__func__.5581, %object
	.size	__func__.5581, 19
__func__.5581:
	.ascii	"FtlGcFreeTempBlock\000"
	.type	__func__.5694, %object
	.size	__func__.5694, 23
__func__.5694:
	.ascii	"rk_ftl_garbage_collect\000"
	.type	__func__.4581, %object
	.size	__func__.4581, 9
__func__.4581:
	.ascii	"FtlWrite\000"
	.type	__func__.5157, %object
	.size	__func__.5157, 15
__func__.5157:
	.ascii	"FtlLoadSysInfo\000"
	.type	__func__.5179, %object
	.size	__func__.5179, 18
__func__.5179:
	.ascii	"FtlMapTblRecovery\000"
	.type	__func__.10855, %object
	.size	__func__.10855, 14
__func__.10855:
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
