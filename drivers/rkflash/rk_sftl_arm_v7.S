/* SPDX-License-Identifier: GPL-2.0 */

/*
 * Copyright (c) 2018 Rockchip Electronics Co. Ltd.
 * date: 2018-08-14
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
.L9:
	cmp	r2, r0
	uxth	r3, r1
	add	r1, r1, #1
	bls	.L10
	sub	r0, r3, #1
	uxth	r0, r0
	ldmfd	sp, {fp, sp, pc}
.L10:
	lsl	r2, r2, #1
	b	.L9
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
	ldr	r3, .L13
	uxth	r0, r0
	ldrh	r2, [r3, #32]
	cmp	r0, #24
	ldrh	r1, [r3, #42]
	movcc	r0, #24
	str	r0, [r3, #28]
	mul	r2, r0, r2
	sub	r0, r1, r0
	ldr	r1, [r3, #48]
	strh	r0, [r3, #40]	@ movhi
	mov	r0, #0
	str	r2, [r3, #36]
	sub	r2, r1, r2
	str	r2, [r3, #44]
	ldmfd	sp, {fp, sp, pc}
.L14:
	.align	2
.L13:
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
	@ args = 0, pretend = 0, frame = 4
	@ frame_needed = 1, uses_anonymous_args = 0
	mov	ip, sp
	push	{r4, r5, r6, r7, r8, r9, r10, fp, ip, lr, pc}
	sub	fp, ip, #4
	sub	sp, sp, #4
	mov	r7, r0
	ldrh	r2, [r7, #14]
	ldr	r4, .L25
	ldr	ip, .L25+4
	cmp	r2, #4
	ldrh	r1, [r0]
	ldrheq	r2, [r7, #6]
	ldrh	r6, [r0, #2]
	ldrh	r0, [r0, #4]
	strh	r1, [r4, #52]	@ movhi
	lsreq	r2, r2, #1
	strh	r6, [r4, #54]	@ movhi
	strh	r0, [r4, #56]	@ movhi
	strheq	r2, [r4, #42]	@ movhi
	moveq	r2, #8
	strheq	r2, [r4, #58]	@ movhi
	mov	r2, #0
.L17:
	strb	r2, [r2, ip]
	add	r2, r2, #1
	cmp	r2, #32
	bne	.L17
	ldrh	r5, [r4, #42]
	smulbb	r6, r6, r0
	mov	r2, #5
	cmp	r1, #1
	mov	ip, #640
	uxth	r6, r6
	strh	r2, [r4, #92]	@ movhi
	smulbb	r0, r0, r5
	mov	r2, #0
	strheq	r1, [r4, #92]	@ movhi
	strh	ip, [r4, #96]	@ movhi
	uxth	r0, r0
	str	ip, [fp, #-44]
	strh	r2, [r4, #94]	@ movhi
	strh	r6, [r4, #32]	@ movhi
	strh	r0, [r4, #98]	@ movhi
	bl	Ftl_log2
	ldrh	r9, [r7, #12]
	ldrh	r10, [r4, #58]
	strh	r0, [r4, #100]	@ movhi
	strh	r9, [r4, #102]	@ movhi
	smulbb	r2, r6, r9
	mov	r0, r10
	strh	r9, [r4, #104]	@ movhi
	strh	r2, [r4, #106]	@ movhi
	bl	Ftl_log2
	ldrh	r3, [r7, #20]
	lsl	r2, r10, #9
	mov	r8, r0
	strh	r0, [r4, #108]	@ movhi
	uxth	r2, r2
	mul	r1, r9, r10
	strh	r3, [r4, #114]	@ movhi
	mov	r0, #5120
	mul	r3, r5, r6
	strh	r2, [r4, #110]	@ movhi
	lsr	r2, r2, #8
	lsl	r5, r5, #6
	strh	r2, [r4, #112]	@ movhi
	str	r3, [r4, #48]
	mul	r3, r10, r3
	mul	r3, r9, r3
	asr	r3, r3, #11
	str	r3, [r4, #116]
	bl	__divsi3
	ldr	ip, [fp, #-44]
	uxth	r0, r0
	mov	r1, r6
	cmp	r0, #4
	movls	r3, #4
	strhhi	r0, [r4, #120]	@ movhi
	asr	r7, ip, r8
	add	r8, r8, #9
	asr	r5, r5, r8
	strhls	r3, [r4, #120]	@ movhi
	add	r7, r7, #2
	ldrh	r0, [r4, #120]
	strh	r5, [r4, #124]	@ movhi
	uxth	r5, r5
	strh	r7, [r4, #122]	@ movhi
	mul	r3, r6, r5
	add	r5, r5, #8
	str	r3, [r4, #128]
	bl	__udivsi3
	uxtah	r0, r5, r0
	cmp	r6, #1
	addeq	r0, r0, #4
	str	r0, [r4, #28]
	ldrh	r0, [r4, #28]
	bl	FtlSysBlkNumInit
	ldr	r3, [r4, #28]
	mov	r0, #0
	str	r0, [r4, #140]
	str	r3, [r4, #132]
	ldr	r3, [r4, #44]
	lsl	r2, r3, #2
	ldrh	r3, [r4, #102]
	mul	r3, r3, r2
	ldrh	r2, [r4, #108]
	add	r2, r2, #9
	lsr	r3, r3, r2
	add	r3, r3, #2
	strh	r3, [r4, #136]	@ movhi
	mov	r3, #32
	strh	r3, [r4, #138]	@ movhi
	ldrh	r3, [r4, #120]
	add	r3, r3, #3
	strh	r3, [r4, #120]	@ movhi
	ldr	r3, [r4, #128]
	add	r3, r3, #3
	str	r3, [r4, #128]
	ldmib	sp, {r4, r5, r6, r7, r8, r9, r10, fp, sp, pc}
.L26:
	.align	2
.L25:
	.word	.LANCHOR0
	.word	.LANCHOR0+60
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
	ldr	r2, .L33
	uxth	r1, r0
	ldrh	r0, [r2, #144]
	cmp	r0, #0
	ldrne	r3, [r2, #148]
	ldrhne	r2, [r2, #120]
	addne	r2, r3, r2, lsl #1
	bne	.L29
	ldmfd	sp, {fp, sp, pc}
.L30:
	ldrh	r0, [r3], #2
	cmp	r1, r0
	beq	.L32
.L29:
	cmp	r3, r2
	bne	.L30
	mov	r0, #0
	ldmfd	sp, {fp, sp, pc}
.L32:
	mov	r0, #1
	ldmfd	sp, {fp, sp, pc}
.L34:
	.align	2
.L33:
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
	ldr	r3, .L37
	ldr	r0, [r3, #140]
	ldmfd	sp, {fp, sp, pc}
.L38:
	.align	2
.L37:
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
	uxth	r5, r0
	ldr	r4, .L40
	mov	r0, r5
	ldrh	r7, [r4, #98]
	mov	r1, r7
	bl	__udivsi3
	uxth	r6, r0
	mov	r1, r7
	mov	r0, r5
	bl	__umodsi3
	add	r2, r4, r6, lsl #2
	uxth	r3, r0
	ldr	r2, [r2, #180]
	lsr	r0, r3, #5
	and	ip, r3, #31
	mov	lr, #1
	ldr	r1, [r2, r0, lsl #2]
	orr	r1, r1, lr, lsl ip
	str	r1, [r2, r0, lsl #2]
	mov	r2, r6
	str	r1, [sp]
	mov	r1, r5
	ldr	r0, .L40+4
	bl	sftl_printk
	ldrh	r3, [r4, #158]
	mov	r0, #0
	add	r3, r3, #1
	strh	r3, [r4, #158]	@ movhi
	ldmib	sp, {r4, r5, r6, r7, fp, sp, pc}
.L41:
	.align	2
.L40:
	.word	.LANCHOR0
	.word	.LC0
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
	ldr	r5, .L43
	uxth	r6, r0
	ldrh	r7, [r5, #98]
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
	ldr	r3, [r5, #180]
	ldr	r0, [r3, r2, lsl #2]
	lsr	r0, r0, r4
	and	r0, r0, #1
	ldmfd	sp, {r4, r5, r6, r7, fp, sp, pc}
.L44:
	.align	2
.L43:
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
	ldr	r0, .L47
	mvn	r3, #0
	mov	r2, #16
	mov	r1, #255
	strh	r3, [r0, #152]	@ movhi
	mov	r3, #0
	strh	r3, [r0, #158]	@ movhi
	add	r0, r0, #164
	bl	memset
	ldmfd	sp, {fp, sp, pc}
.L48:
	.align	2
.L47:
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
	ldr	r3, .L56
	mov	r5, #0
	mov	r4, r5
	ldrh	r2, [r3, #98]
	ldrh	r6, [r3, #54]
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
	ldrh	r6, [r4, #56]
	mov	r0, r7
	mov	r1, r6
	bl	__udivsi3
	ldrh	r4, [r4, #98]
	smulbb	r5, r6, r5
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
	ldrh	r6, [r3, #56]
	mov	r0, r5
	ldrh	r1, [r3, #98]
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
	uxth	r0, r0
	ldrh	r1, [r4, #98]
	bl	__umodsi3
	ldrh	r1, [r4, #56]
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
	ldr	r3, .L71
	mov	r4, #0
	mov	r1, #2048
	strh	r0, [r3, #212]	@ movhi
	add	r0, r3, #220
	strh	r4, [r3, #214]	@ movhi
	strh	r4, [r3, #216]	@ movhi
	strh	r4, [r3, #218]	@ movhi
	bl	__memzero
	mov	r0, r4
	ldmfd	sp, {r4, fp, sp, pc}
.L72:
	.align	2
.L71:
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
	ldr	r3, .L74
	ldrh	r0, [r3, #218]
	clz	r0, r0
	lsr	r0, r0, #5
	ldmfd	sp, {fp, sp, pc}
.L75:
	.align	2
.L74:
	.word	.LANCHOR0
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
	ldr	r3, .L77
	ldrh	r0, [r3, #218]
	sub	r0, r0, #1024
	clz	r0, r0
	lsr	r0, r0, #5
	ldmfd	sp, {fp, sp, pc}
.L78:
	.align	2
.L77:
	.word	.LANCHOR0
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
	ldr	r3, .L88
	ldrh	r2, [r3, #218]
	cmp	r2, #0
	ldmfdeq	sp, {r4, fp, sp, pc}
	ldr	r2, .L88+4
	mov	r4, #0
	ldrh	r1, [r3, #214]
	mov	lr, r4
	ldrh	ip, [r2, #28]
	ldrh	r2, [r3, #216]
	and	ip, ip, #31
.L81:
	uxth	r0, r4
	add	r4, r4, #1
	cmp	ip, r0
	bgt	.L82
	cmp	lr, #0
	strhne	r1, [r3, #214]	@ movhi
	strhne	r2, [r3, #216]	@ movhi
	ldmfd	sp, {r4, fp, sp, pc}
.L82:
	add	r0, r3, r1, lsl #1
	add	r1, r1, #1
	ubfx	r1, r1, #0, #10
	ldrh	lr, [r0, #220]
	add	r0, r3, r2, lsl #1
	strh	lr, [r0, #220]	@ movhi
	mov	lr, #1
	add	r2, r2, lr
	ubfx	r2, r2, #0, #10
	b	.L81
.L89:
	.align	2
.L88:
	.word	.LANCHOR0
	.word	.LANCHOR0+2268
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
	ldr	r3, .L96
	uxth	r0, r0
	ldrh	ip, [r3, #218]
	cmp	ip, #1024
	ldrhne	lr, [r3, #214]
	movne	r1, #0
	bne	.L92
.L94:
	mov	r0, #0
	ldmfd	sp, {fp, sp, pc}
.L93:
	add	r2, r1, lr
	ubfx	r2, r2, #0, #10
	add	r2, r3, r2, lsl #1
	ldrh	r2, [r2, #220]
	cmp	r2, r0
	beq	.L95
	add	r1, r1, #1
.L92:
	cmp	r1, ip
	bcc	.L93
	b	.L94
.L95:
	mov	r0, #1
	ldmfd	sp, {fp, sp, pc}
.L97:
	.align	2
.L96:
	.word	.LANCHOR0
	.size	IsInFreeQueue, .-IsInFreeQueue
	.align	2
	.global	test_node_in_list
	.syntax unified
	.arm
	.fpu softvfp
	.type	test_node_in_list, %function
test_node_in_list:
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 1, uses_anonymous_args = 0
	mov	ip, sp
	push	{fp, ip, lr, pc}
	sub	fp, ip, #4
	ldr	r3, .L103
	uxth	r1, r1
	movw	ip, #65535
	ldr	r2, [r0]
	ldr	r0, [r3, #2316]
	sub	r3, r2, r0
	ubfx	r3, r3, #3, #16
.L100:
	cmp	r3, r1
	beq	.L101
	ldrh	r3, [r2]
	cmp	r3, ip
	beq	.L102
	add	r2, r0, r3, lsl #3
	b	.L100
.L101:
	mov	r0, #1
	ldmfd	sp, {fp, sp, pc}
.L102:
	mov	r0, #0
	ldmfd	sp, {fp, sp, pc}
.L104:
	.align	2
.L103:
	.word	.LANCHOR0
	.size	test_node_in_list, .-test_node_in_list
	.align	2
	.global	insert_data_list
	.syntax unified
	.arm
	.fpu softvfp
	.type	insert_data_list, %function
insert_data_list:
	@ args = 0, pretend = 0, frame = 12
	@ frame_needed = 1, uses_anonymous_args = 0
	mov	ip, sp
	push	{r4, r5, r6, r7, r8, r9, r10, fp, ip, lr, pc}
	sub	fp, ip, #4
	sub	sp, sp, #12
	uxth	r0, r0
	ldr	r1, .L121
	ldrh	r3, [r1, #40]
	mov	lr, r1
	cmp	r3, r0
	bls	.L107
	ldr	r6, [r1, #2316]
	lsl	r5, r0, #3
	mvn	ip, #0
	add	r2, r6, r5
	strh	ip, [r2, #2]	@ movhi
	strh	ip, [r6, r5]	@ movhi
	ldr	r3, [r1, #2320]
	cmp	r3, #0
	bne	.L108
.L120:
	str	r2, [r1, #2320]
	b	.L107
.L108:
	ldr	r7, [r1, #2324]
	lsl	r4, r0, #1
	ldrh	r1, [r2, #4]
	ldr	r10, [lr, #2316]
	ldrh	r8, [r7, r4]
	cmp	r1, #0
	streq	ip, [fp, #-44]
	mulne	r1, r1, r8
	ldrh	r8, [lr, #40]
	str	r8, [fp, #-48]
	ldr	r8, [lr, #2328]
	strne	r1, [fp, #-44]
	sub	r1, r3, r10
	ubfx	r1, r1, #3, #16
	add	lr, r8, r4
	str	lr, [fp, #-52]
	mov	lr, #0
.L116:
	ldr	r4, [fp, #-48]
	add	lr, lr, #1
	uxth	lr, lr
	cmp	lr, r4
	movls	r4, #0
	movhi	r4, #1
	cmp	r0, r1
	orreq	r4, r4, #1
	cmp	r4, #0
	bne	.L107
	lsl	r9, r1, #1
	ldrh	r4, [r7, r9]
	mov	ip, r4
	ldrh	r4, [r3, #4]
	cmp	r4, #0
	mulne	r4, r4, ip
	ldr	ip, [fp, #-44]
	mvneq	r4, #0
	cmp	ip, r4
	bne	.L112
	ldr	ip, [fp, #-52]
	ldrh	r9, [r8, r9]
	ldrh	r4, [ip]
	cmp	r9, r4
	bcc	.L114
.L113:
	strh	r1, [r6, r5]	@ movhi
	ldrh	r1, [r3, #2]
	strh	r1, [r2, #2]	@ movhi
	ldr	r1, .L121
	ldr	ip, [r1, #2320]
	cmp	r3, ip
	bne	.L117
	strh	r0, [r3, #2]	@ movhi
	b	.L120
.L112:
	bcc	.L113
.L114:
	ldrh	r4, [r3]
	movw	ip, #65535
	cmp	r4, ip
	bne	.L115
	strh	r1, [r2, #2]	@ movhi
	strh	r0, [r3]	@ movhi
	ldr	r3, .L121
	str	r2, [r3, #2332]
.L107:
	mov	r0, #0
	sub	sp, fp, #40
	ldmfd	sp, {r4, r5, r6, r7, r8, r9, r10, fp, sp, pc}
.L115:
	add	r3, r10, r4, lsl #3
	mov	r1, r4
	b	.L116
.L117:
	ldrh	r2, [r3, #2]
	ldr	r1, [r1, #2316]
	lsl	r2, r2, #3
	strh	r0, [r1, r2]	@ movhi
	strh	r0, [r3, #2]	@ movhi
	b	.L107
.L122:
	.align	2
.L121:
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
	ldr	r2, .L125
	add	r1, r2, #2336
	ldrh	r2, [r2, #40]
	ldrh	r3, [r1]
	add	r3, r3, #1
	uxth	r3, r3
	cmp	r2, r3
	strh	r3, [r1]	@ movhi
	ldmfdcs	sp, {fp, sp, pc}
	mov	r2, #221
	ldr	r1, .L125+4
	ldr	r0, .L125+8
	bl	sftl_printk
	ldmfd	sp, {fp, sp, pc}
.L126:
	.align	2
.L125:
	.word	.LANCHOR0
	.word	.LANCHOR1
	.word	.LC1
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
	beq	.L128
	ldr	r2, .L134
	lsl	r5, r0, #3
	mvn	r3, #0
	ldr	r6, [r2, #2316]
	mov	r1, r2
	add	ip, r6, r5
	strh	r3, [ip, #2]	@ movhi
	strh	r3, [r6, r5]	@ movhi
	ldr	r3, [r2, #2340]
	cmp	r3, #0
	streq	ip, [r2, #2340]
	beq	.L128
	ldr	r8, [r2, #2328]
	lsl	r2, r0, #1
	ldr	r7, [r1, #2316]
	mov	r4, lr
	ldrh	r9, [r8, r2]
	sub	r2, r3, r7
	ubfx	r2, r2, #3, #16
.L132:
	lsl	lr, r2, #1
	ldrh	lr, [r8, lr]
	cmp	lr, r9
	bcs	.L130
	ldrh	lr, [r3]
	cmp	lr, r4
	bne	.L131
	strh	r2, [ip, #2]	@ movhi
	strh	r0, [r3]	@ movhi
.L128:
	mov	r0, #0
	ldmfd	sp, {r4, r5, r6, r7, r8, r9, fp, sp, pc}
.L131:
	add	r3, r7, lr, lsl #3
	mov	r2, lr
	b	.L132
.L130:
	ldrh	lr, [r3, #2]
	strh	lr, [ip, #2]	@ movhi
	strh	r2, [r6, r5]	@ movhi
	ldr	r2, [r1, #2340]
	cmp	r3, r2
	ldrhne	r2, [r3, #2]
	ldrne	r1, [r1, #2316]
	strheq	r0, [r3, #2]	@ movhi
	streq	ip, [r1, #2340]
	lslne	r2, r2, #3
	strhne	r0, [r1, r2]	@ movhi
	strhne	r0, [r3, #2]	@ movhi
	b	.L128
.L135:
	.align	2
.L134:
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
	ldr	r2, .L138
	movw	r1, #2344
	ldrh	r3, [r2, r1]
	add	r3, r3, #1
	uxth	r3, r3
	strh	r3, [r2, r1]	@ movhi
	ldrh	r2, [r2, #40]
	cmp	r2, r3
	ldmfdcs	sp, {fp, sp, pc}
	mov	r2, #214
	ldr	r1, .L138+4
	ldr	r0, .L138+8
	bl	sftl_printk
	ldmfd	sp, {fp, sp, pc}
.L139:
	.align	2
.L138:
	.word	.LANCHOR0
	.word	.LANCHOR1+17
	.word	.LC1
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
	ldr	r4, .L147
	uxth	r1, r1
	movw	r3, #65535
	mov	r8, r0
	ldr	r7, [r4, #2316]
	lsl	r6, r1, #3
	add	r5, r7, r6
	ldrh	r2, [r5, #2]
	cmp	r2, r3
	bne	.L141
	ldr	r3, [r0]
	cmp	r5, r3
	beq	.L141
	movw	r2, #397
	ldr	r1, .L147+4
	ldr	r0, .L147+8
	bl	sftl_printk
.L141:
	ldr	r3, [r8]
	ldrh	r2, [r7, r6]
	cmp	r5, r3
	movw	r3, #65535
	bne	.L142
	cmp	r2, r3
	ldrne	r3, [r4, #2316]
	moveq	r3, #0
	streq	r3, [r8]
	addne	r2, r3, r2, lsl #3
	mvnne	r3, #0
	strne	r2, [r8]
	strhne	r3, [r2, #2]	@ movhi
.L144:
	mvn	r3, #0
	mov	r0, #0
	strh	r3, [r7, r6]	@ movhi
	strh	r3, [r5, #2]	@ movhi
	ldmfd	sp, {r4, r5, r6, r7, r8, fp, sp, pc}
.L142:
	cmp	r2, r3
	ldrh	r3, [r5, #2]
	bne	.L145
	cmp	r3, r2
	ldrne	r2, [r4, #2316]
	lslne	r3, r3, #3
	mvnne	r1, #0
	beq	.L144
.L146:
	strh	r1, [r2, r3]	@ movhi
	b	.L144
.L145:
	ldr	r1, [r4, #2316]
	add	r2, r1, r2, lsl #3
	strh	r3, [r2, #2]	@ movhi
	ldrh	r3, [r5, #2]
	ldrh	r1, [r7, r6]
	ldr	r2, [r4, #2316]
	lsl	r3, r3, #3
	b	.L146
.L148:
	.align	2
.L147:
	.word	.LANCHOR0
	.word	.LANCHOR1+34
	.word	.LC1
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
	beq	.L149
	ldr	r2, .L156
	movw	ip, #65535
	ldr	r4, [r2, #2316]
.L151:
	cmp	r1, #0
	bne	.L152
.L154:
	sub	r3, r3, r4
	ubfx	r4, r3, #3, #16
	mov	r1, r4
	bl	List_remove_node
.L149:
	mov	r0, r4
	ldmfd	sp, {r4, fp, sp, pc}
.L152:
	ldrh	r2, [r3]
	cmp	r2, ip
	beq	.L154
	sub	r1, r1, #1
	add	r3, r4, r2, lsl #3
	uxth	r1, r1
	b	.L151
.L157:
	.align	2
.L156:
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
	ldr	r1, .L165
	uxth	r2, r0
	ldr	r3, [r1, #2320]
	cmp	r3, #0
	ldrne	r1, [r1, #2316]
	movwne	r0, #65535
	bne	.L161
	movw	r0, #65535
	ldmfd	sp, {fp, sp, pc}
.L163:
	sub	r2, r2, #1
	add	r3, r1, r3, lsl #3
	uxth	r2, r2
.L161:
	cmp	r2, #0
	beq	.L162
	ldrh	r3, [r3]
	cmp	r3, r0
	bne	.L163
	ldmfd	sp, {fp, sp, pc}
.L162:
	sub	r3, r3, r1
	ubfx	r0, r3, #3, #16
	ldmfd	sp, {fp, sp, pc}
.L166:
	.align	2
.L165:
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
	ldr	r4, .L180
	movw	r3, #2348
	uxth	r5, r0
	ldrh	r3, [r4, r3]
	cmp	r3, r5
	beq	.L169
	movw	r3, #2396
	ldrh	r3, [r4, r3]
	cmp	r3, r5
	beq	.L169
	movw	r3, #2444
	ldrh	r3, [r4, r3]
	cmp	r3, r5
	beq	.L169
	ldr	r8, [r4, #2316]
	ldr	r3, [r4, #2320]
	add	r7, r8, r5, lsl #3
	cmp	r7, r3
	beq	.L169
	ldr	r2, [r4, #2324]
	lsl	r3, r5, #1
	ldrh	r6, [r7, #4]
	ldrh	r3, [r2, r3]
	cmp	r6, #0
	mvneq	r6, #0
	mulne	r6, r6, r3
	ldr	r3, [r8, r5, lsl #3]
	cmn	r3, #1
	bne	.L172
	mov	r2, #488
	ldr	r1, .L180+4
	ldr	r0, .L180+8
	bl	sftl_printk
.L172:
	ldr	r3, [r8, r5, lsl #3]
	cmn	r3, #1
	beq	.L169
	ldrh	r2, [r7, #2]
	ldr	r1, [r4, #2324]
	lsl	r2, r2, #3
	lsr	r3, r2, #2
	ldrh	r0, [r1, r3]
	ldr	r1, [r4, #2316]
	add	r2, r1, r2
	ldrh	r3, [r2, #4]
	cmp	r3, #0
	mulne	r3, r3, r0
	mvneq	r3, #0
	cmp	r6, r3
	bcs	.L169
	ldr	r4, .L180+12
	mov	r1, r5
	ldr	r0, .L180+16
	bl	List_remove_node
	ldrh	r3, [r4]
	cmp	r3, #0
	bne	.L174
	movw	r2, #499
	ldr	r1, .L180+4
	ldr	r0, .L180+8
	bl	sftl_printk
.L174:
	ldrh	r3, [r4]
	mov	r0, r5
	sub	r3, r3, #1
	strh	r3, [r4]	@ movhi
	bl	INSERT_DATA_LIST
.L169:
	mov	r0, #0
	ldmfd	sp, {r4, r5, r6, r7, r8, fp, sp, pc}
.L181:
	.align	2
.L180:
	.word	.LANCHOR0
	.word	.LANCHOR1+51
	.word	.LC1
	.word	.LANCHOR0+2336
	.word	.LANCHOR0+2320
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
	push	{r4, r5, r6, fp, ip, lr, pc}
	sub	fp, ip, #4
	ldr	r3, .L192
	mov	r1, #0
	mov	r0, #12
	movw	ip, #65535
	ldrh	r2, [r3, #138]
	ldr	r3, [r3, #2492]
.L183:
	uxth	r4, r1
	cmp	r4, r2
	bcc	.L185
	mov	r4, r2
	mov	r1, #0
	mov	r0, #-2147483648
	mov	r5, #12
.L186:
	uxth	lr, r1
	cmp	lr, r2
	bcc	.L188
	cmp	r4, r2
	bcc	.L184
	ldr	r1, .L192+4
	mov	r4, r2
	mvn	r0, #0
	ldrh	r5, [r1]
	mov	r1, #0
.L189:
	uxth	ip, r1
	cmp	ip, r2
	bcc	.L191
	cmp	r4, r2
	bcc	.L184
	movw	r2, #823
	ldr	r1, .L192+8
	ldr	r0, .L192+12
	bl	sftl_printk
	b	.L184
.L185:
	add	r1, r1, #1
	mla	lr, r0, r1, r3
	ldrh	lr, [lr, #-12]
	cmp	lr, ip
	bne	.L183
.L184:
	mov	r0, r4
	ldmfd	sp, {r4, r5, r6, fp, sp, pc}
.L188:
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
	b	.L186
.L191:
	ldr	lr, [r3, #4]
	cmp	r0, lr
	bls	.L190
	ldrh	r6, [r3]
	cmp	r6, r5
	movne	r0, lr
	movne	r4, ip
.L190:
	add	r1, r1, #1
	add	r3, r3, #12
	b	.L189
.L193:
	.align	2
.L192:
	.word	.LANCHOR0
	.word	.LANCHOR0+2496
	.word	.LANCHOR1+73
	.word	.LC1
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
	ldr	ip, .L200
	movw	r1, #2498
	ldrh	r2, [ip, r1]
	mov	r3, ip
	cmp	r2, #4
	cmpls	r0, #0
	bne	.L195
	add	r2, r2, #1
	strh	r2, [ip, r1]	@ movhi
	ldmfd	sp, {fp, sp, pc}
.L195:
	mov	r2, #0
	movw	lr, #65535
	strh	r2, [ip, r1]	@ movhi
	str	r2, [ip, #2500]
	ldrh	r1, [ip, #40]
	ldr	r2, [ip, #2324]
	add	r1, r2, r1, lsl #1
.L196:
	cmp	r2, r1
	bne	.L198
	ldmfd	sp, {fp, sp, pc}
.L198:
	ldrh	ip, [r2], #2
	cmp	ip, lr
	ldrne	r0, [r3, #2500]
	addne	r0, r0, ip
	strne	r0, [r3, #2500]
	b	.L196
.L201:
	.align	2
.L200:
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
	beq	.L203
	bl	ftl_set_blk_mode.part.6
	ldmfd	sp, {fp, sp, pc}
.L203:
	ldr	r3, .L205
	lsr	r1, r0, #5
	mov	ip, #1
	and	r0, r0, #31
	ldr	r2, [r3, #24]
	ldr	r3, [r2, r1, lsl #2]
	bic	r0, r3, ip, lsl r0
	str	r0, [r2, r1, lsl #2]
	ldmfd	sp, {fp, sp, pc}
.L206:
	.align	2
.L205:
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
	ldr	r3, .L208
	uxth	r0, r0
	ldr	r3, [r3, #24]
	lsr	r2, r0, #5
	and	r0, r0, #31
	ldr	r3, [r3, r2, lsl #2]
	lsr	r0, r3, r0
	and	r0, r0, #1
	ldmfd	sp, {fp, sp, pc}
.L209:
	.align	2
.L208:
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
	ldr	r3, .L217
	add	ip, r0, r2, lsl #1
	movw	r1, #65535
	add	ip, ip, #14
	ldrh	lr, [r3, #32]
.L211:
	cmp	r2, lr
	bcc	.L213
	ldrh	r1, [r3, #102]
	add	ip, r0, #16
	mov	r3, #0
	movw	r4, #65535
	sub	r1, r1, #1
	sub	r1, r1, r5
	uxth	r1, r1
.L214:
	uxth	r2, r3
	cmp	lr, r2
	bhi	.L216
	ldmfd	sp, {r4, r5, fp, sp, pc}
.L213:
	ldrh	r4, [ip, #2]!
	add	r2, r2, #1
	uxth	r2, r2
	cmp	r4, r1
	ldrhne	r4, [r0, #4]
	addne	r4, r4, #1
	strhne	r4, [r0, #4]	@ movhi
	b	.L211
.L216:
	ldrh	r2, [ip], #2
	add	r3, r3, #1
	cmp	r2, r4
	ldrhne	r2, [r0, #4]
	addne	r2, r1, r2
	strhne	r2, [r0, #4]	@ movhi
	b	.L214
.L218:
	.align	2
.L217:
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
	ldr	r2, .L225
	ldrh	ip, [r2, #32]
	mov	r2, r3
.L222:
	cmp	r1, r2
	beq	.L224
	ldmfd	sp, {fp, sp, pc}
.L224:
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
	b	.L222
.L226:
	.align	2
.L225:
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
	ldrh	r3, [r6, #40]
	cmp	r2, r3
	bcc	.L228
	movw	r2, #2146
	ldr	r1, .L232+4
	ldr	r0, .L232+8
	bl	sftl_printk
.L228:
	ldrh	r9, [r6, #32]
	add	r7, r4, #16
	ldr	r10, .L232+12
	mvn	r8, #0
	mov	r5, #0
	strh	r5, [r4, #4]	@ movhi
	strb	r5, [r4, #7]
.L229:
	uxth	r3, r5
	cmp	r9, r3
	bhi	.L231
	ldrb	r3, [r4, #7]	@ zero_extendqisi2
	mov	r0, #0
	ldrh	r2, [r6, #102]
	smulbb	r3, r3, r2
	strh	r3, [r4, #4]	@ movhi
	mov	r3, #1
	strb	r3, [r4, #9]
	ldmib	sp, {r4, r5, r6, r7, r8, r9, r10, fp, sp, pc}
.L231:
	ldrb	r0, [r10, r5]	@ zero_extendqisi2
	add	r7, r7, #2
	ldrh	r1, [r4]
	add	r5, r5, #1
	bl	V2P_block
	strh	r8, [r7, #-2]	@ movhi
	str	r0, [fp, #-44]
	bl	FtlBbmIsBadBlock
	cmp	r0, #0
	ldreq	r3, [fp, #-44]
	strheq	r3, [r7, #-2]	@ movhi
	ldrbeq	r3, [r4, #7]	@ zero_extendqisi2
	addeq	r3, r3, #1
	strbeq	r3, [r4, #7]
	b	.L229
.L233:
	.align	2
.L232:
	.word	.LANCHOR0
	.word	.LANCHOR1+95
	.word	.LC1
	.word	.LANCHOR0+60
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
	mov	r5, #0
	uxth	r6, r0
	mov	r4, r5
	ldrh	r8, [r3, #32]
	mov	r7, r3
	ldrh	r9, [r3, #102]
	add	r10, r3, #60
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
	ldr	r1, [r7, #2316]
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
	ldr	r0, [r3, #2340]
	cmp	r0, #0
	ldrne	r2, [r3, #2316]
	ldrne	r3, [r3, #2328]
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
	ldr	r0, [r2, #2340]
	cmp	r0, #0
	ldmfdeq	sp, {r4, r5, fp, sp, pc}
	movw	r3, #2344
	ldr	ip, [r2, #2316]
	ldrh	r3, [r2, r3]
	mov	r1, #0
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
	ldr	r2, [r2, #2328]
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
	sub	sp, sp, #12
	mov	r7, r0
	ldr	r6, .L269
	add	r5, r7, #12
	ldr	r1, .L269+4
	movw	r9, #2344
	bl	strcpy
	ldr	r2, [r6, #116]
	mov	r0, r5
	ldr	r1, .L269+8
	bl	sprintf
	add	r5, r5, r0
	ldr	r1, .L269+12
	mov	r0, r5
	add	r5, r5, #10
	bl	strcpy
	ldr	r2, [r6, #2504]
	mov	r0, r5
	ldr	r1, .L269+16
	bl	sprintf
	add	r5, r5, r0
	ldr	r2, [r6, #2500]
	ldr	r1, .L269+20
	mov	r0, r5
	bl	sprintf
	add	r5, r5, r0
	ldr	r2, [r6, #2508]
	ldr	r1, .L269+24
	mov	r0, r5
	bl	sprintf
	add	r5, r5, r0
	ldr	r2, [r6, #2512]
	ldr	r1, .L269+28
	mov	r0, r5
	bl	sprintf
	add	r5, r5, r0
	ldr	r2, [r6, #2516]
	ldr	r1, .L269+32
	mov	r0, r5
	bl	sprintf
	add	r5, r5, r0
	ldr	r2, [r6, #2520]
	ldr	r1, .L269+36
	mov	r0, r5
	bl	sprintf
	add	r5, r5, r0
	ldr	r2, [r6, #2524]
	ldr	r1, .L269+40
	mov	r0, r5
	bl	sprintf
	add	r5, r5, r0
	ldr	r2, [r6, #2528]
	ldr	r1, .L269+44
	mov	r0, r5
	bl	sprintf
	ldr	r2, [r6, #2532]
	add	r5, r5, r0
	ldr	r1, .L269+48
	mov	r0, r5
	ldr	r4, .L269+52
	ldr	r8, .L269+56
	lsr	r2, r2, #11
	bl	sprintf
	ldr	r2, [r6, #2536]
	add	r5, r5, r0
	ldr	r1, .L269+60
	mov	r0, r5
	lsr	r2, r2, #11
	bl	sprintf
	add	r5, r5, r0
	ldr	r2, [r6, #2540]
	ldr	r1, .L269+64
	mov	r0, r5
	bl	sprintf
	add	r5, r5, r0
	ldr	r2, [r6, #2544]
	ldr	r1, .L269+68
	mov	r0, r5
	bl	sprintf
	add	r5, r5, r0
	ldrh	r2, [r6, #158]
	ldr	r1, .L269+72
	mov	r0, r5
	bl	sprintf
	add	r5, r5, r0
	ldrh	r2, [r6, r9]
	ldr	r1, .L269+76
	mov	r0, r5
	bl	sprintf
	add	r5, r5, r0
	ldr	r2, [r6, #2548]
	ldr	r1, .L269+80
	mov	r0, r5
	bl	sprintf
	add	r5, r5, r0
	ldr	r2, [r6, #2552]
	ldr	r1, .L269+84
	mov	r0, r5
	bl	sprintf
	add	r5, r5, r0
	ldr	r2, [r6, #2556]
	ldr	r1, .L269+88
	mov	r0, r5
	bl	sprintf
	add	r5, r5, r0
	ldr	r2, [r6, #2560]
	ldr	r1, .L269+92
	mov	r0, r5
	bl	sprintf
	add	r5, r5, r0
	ldr	r2, [r6, #2564]
	ldr	r1, .L269+96
	mov	r0, r5
	bl	sprintf
	add	r5, r5, r0
	ldr	r2, [r6, #2568]
	ldr	r1, .L269+100
	mov	r0, r5
	bl	sprintf
	add	r5, r5, r0
	ldrh	r2, [r4, #30]
	ldr	r1, .L269+104
	mov	r0, r5
	bl	sprintf
	add	r5, r5, r0
	ldrh	r2, [r4, #28]
	ldr	r1, .L269+108
	mov	r0, r5
	bl	sprintf
	add	r5, r5, r0
	ldr	r2, [r6, #140]
	ldr	r1, .L269+112
	mov	r0, r5
	bl	sprintf
	add	r5, r5, r0
	ldr	r2, [r6, #132]
	ldr	r1, .L269+116
	mov	r0, r5
	bl	sprintf
	add	r5, r5, r0
	ldr	r2, [r6, #28]
	ldr	r1, .L269+120
	mov	r0, r5
	bl	sprintf
	add	r5, r5, r0
	ldrh	r2, [r6, #218]
	ldr	r1, .L269+124
	mov	r0, r5
	bl	sprintf
	add	r5, r5, r0
	ldrh	r2, [r6, #40]
	ldr	r1, .L269+128
	mov	r0, r5
	bl	sprintf
	movw	r3, #2572
	add	r5, r5, r0
	ldrh	r2, [r6, r3]
	mov	r0, r5
	ldr	r1, .L269+132
	movw	r4, #2348
	bl	sprintf
	add	r5, r5, r0
	ldr	r2, [r6, #44]
	ldr	r1, .L269+136
	mov	r0, r5
	bl	sprintf
	add	r3, r6, #2576
	add	r5, r5, r0
	ldrh	r2, [r3]
	mov	r0, r5
	ldr	r1, .L269+140
	bl	sprintf
	add	r5, r5, r0
	ldrh	r2, [r6, #152]
	ldr	r1, .L269+144
	mov	r0, r5
	bl	sprintf
	add	r5, r5, r0
	ldrh	r2, [r8, #2]
	ldr	r1, .L269+148
	mov	r0, r5
	bl	sprintf
	add	r5, r5, r0
	ldrb	r2, [r6, #2354]	@ zero_extendqisi2
	ldr	r1, .L269+152
	mov	r0, r5
	bl	sprintf
	add	r5, r5, r0
	ldrh	r2, [r6, r4]
	ldr	r1, .L269+156
	mov	r0, r5
	bl	sprintf
	add	r5, r5, r0
	ldrb	r2, [r6, #2356]	@ zero_extendqisi2
	ldr	r1, .L269+160
	mov	r0, r5
	bl	sprintf
	add	r5, r5, r0
	ldrh	r2, [r8, #4]
	ldr	r1, .L269+164
	mov	r0, r5
	bl	sprintf
	ldrh	r3, [r6, r4]
	add	r5, r5, r0
	ldr	r2, [r6, #2324]
	mov	r0, r5
	ldr	r1, .L269+168
	movw	r4, #2396
	lsl	r3, r3, #1
	ldrh	r2, [r2, r3]
	bl	sprintf
	add	r5, r5, r0
	ldrh	r2, [r8, #50]
	ldr	r1, .L269+172
	mov	r0, r5
	bl	sprintf
	add	r5, r5, r0
	ldrb	r2, [r6, #2402]	@ zero_extendqisi2
	ldr	r1, .L269+176
	mov	r0, r5
	bl	sprintf
	add	r5, r5, r0
	ldrh	r2, [r6, r4]
	ldr	r1, .L269+180
	mov	r0, r5
	bl	sprintf
	add	r5, r5, r0
	ldrb	r2, [r6, #2404]	@ zero_extendqisi2
	ldr	r1, .L269+184
	mov	r0, r5
	bl	sprintf
	add	r5, r5, r0
	ldrh	r2, [r8, #52]
	ldr	r1, .L269+188
	mov	r0, r5
	bl	sprintf
	ldrh	r3, [r6, r4]
	add	r5, r5, r0
	ldr	r2, [r6, #2324]
	mov	r0, r5
	ldr	r1, .L269+192
	add	r4, r8, #96
	lsl	r3, r3, #1
	ldrh	r2, [r2, r3]
	bl	sprintf
	add	r5, r5, r0
	ldrh	r2, [r8, #98]
	ldr	r1, .L269+196
	mov	r0, r5
	bl	sprintf
	add	r5, r5, r0
	ldrb	r2, [r6, #2450]	@ zero_extendqisi2
	ldr	r1, .L269+200
	mov	r0, r5
	bl	sprintf
	movw	r3, #2444
	add	r5, r5, r0
	ldrh	r2, [r6, r3]
	mov	r0, r5
	ldr	r1, .L269+204
	bl	sprintf
	add	r5, r5, r0
	ldrb	r2, [r6, #2452]	@ zero_extendqisi2
	ldr	r1, .L269+208
	mov	r0, r5
	bl	sprintf
	add	r5, r5, r0
	ldrh	r2, [r8, #100]
	ldr	r1, .L269+212
	mov	r0, r5
	bl	sprintf
	add	r5, r5, r0
	ldrh	r2, [r4, #146]
	ldr	r1, .L269+216
	mov	r0, r5
	bl	sprintf
	add	r5, r5, r0
	ldrb	r2, [r6, #2594]	@ zero_extendqisi2
	ldr	r1, .L269+220
	mov	r0, r5
	bl	sprintf
	movw	r8, #2588
	add	r5, r5, r0
	ldrh	r2, [r6, r8]
	mov	r0, r5
	ldr	r1, .L269+224
	bl	sprintf
	add	r5, r5, r0
	ldrb	r2, [r6, #2596]	@ zero_extendqisi2
	ldr	r1, .L269+228
	mov	r0, r5
	bl	sprintf
	add	r5, r5, r0
	ldrh	r2, [r4, #148]
	ldr	r1, .L269+232
	mov	r0, r5
	bl	sprintf
	ldr	r3, [r6, #2712]
	add	r5, r5, r0
	ldr	r2, [r6, #2716]
	mov	r0, r5
	ldr	r1, .L269+236
	str	r3, [sp]
	ldr	r3, [r6, #2720]
	bl	sprintf
	add	r4, r5, r0
	ldr	r2, [r6, #2708]
	ldr	r1, .L269+240
	mov	r0, r4
	bl	sprintf
	add	r4, r4, r0
	ldr	r2, [r6, #2732]
	ldr	r1, .L269+244
	mov	r0, r4
	bl	sprintf
	movw	r3, #3148
	add	r4, r4, r0
	ldrh	r2, [r6, r3]
	mov	r0, r4
	ldr	r1, .L269+248
	bl	sprintf
	movw	r3, #3150
	add	r4, r4, r0
	ldrh	r2, [r6, r3]
	mov	r0, r4
	ldr	r1, .L269+252
	bl	sprintf
	add	r4, r4, r0
	ldr	r2, [r6, #3152]
	ldr	r1, .L269+256
	mov	r0, r4
	bl	sprintf
	movw	r3, #3156
	add	r4, r4, r0
	ldrh	r2, [r6, r3]
	mov	r0, r4
	ldr	r1, .L269+260
	bl	sprintf
	add	r4, r4, r0
	bl	GetFreeBlockMinEraseCount
	ldr	r1, .L269+264
	mov	r2, r0
	mov	r0, r4
	bl	sprintf
	add	r4, r4, r0
	ldrh	r0, [r6, r9]
	bl	GetFreeBlockMaxEraseCount
	ldr	r1, .L269+268
	mov	r2, r0
	mov	r0, r4
	bl	sprintf
	ldr	r3, .L269+272
	add	r4, r4, r0
	ldr	r3, [r3]
	cmp	r3, #1
	beq	.L258
.L263:
	sub	r0, r4, r7
	sub	sp, fp, #40
	ldmfd	sp, {r4, r5, r6, r7, r8, r9, r10, fp, sp, pc}
.L258:
	ldrh	r3, [r6, r8]
	movw	r2, #65535
	cmp	r3, r2
	beq	.L260
	ldr	r2, [r6, #2324]
	lsl	r3, r3, #1
	mov	r0, r4
	ldr	r1, .L269+276
	ldrh	r2, [r2, r3]
	bl	sprintf
	add	r4, r4, r0
.L260:
	mov	r0, #0
	ldr	r9, .L269+280
	bl	List_get_gc_head_node
	uxth	r3, r0
	mov	r5, #0
	movw	r10, #65535
.L262:
	cmp	r3, r10
	beq	.L261
	ldr	r2, [r6, #2328]
	lsl	r1, r3, #1
	lsl	r8, r3, #3
	mov	r0, r4
	ldrh	r2, [r2, r1]
	str	r2, [sp, #8]
	ldr	r2, [r6, #2316]
	add	r2, r2, r8
	ldrh	r2, [r2, #4]
	str	r2, [sp, #4]
	ldr	r2, [r6, #2324]
	ldrh	r2, [r2, r1]
	mov	r1, r9
	str	r2, [sp]
	mov	r2, r5
	bl	sprintf
	add	r5, r5, #1
	ldr	r3, [r6, #2316]
	cmp	r5, #16
	add	r4, r4, r0
	ldrh	r3, [r3, r8]
	bne	.L262
.L261:
	ldr	r3, [r6, #2340]
	mov	r5, #0
	ldr	r2, [r6, #2316]
	movw	r9, #65535
	ldr	r10, .L269+284
	sub	r3, r3, r2
	ubfx	r3, r3, #3, #16
.L264:
	cmp	r3, r9
	beq	.L263
	ldr	r1, [r6, #2328]
	lsl	r2, r3, #1
	lsl	r8, r3, #3
	mov	r0, r4
	ldrh	r2, [r1, r2]
	mov	r1, r10
	str	r2, [sp, #4]
	ldr	r2, [r6, #2316]
	add	r2, r2, r8
	ldrh	r2, [r2, #4]
	str	r2, [sp]
	mov	r2, r5
	add	r5, r5, #1
	bl	sprintf
	cmp	r5, #4
	add	r4, r4, r0
	beq	.L263
	ldr	r3, [r6, #2316]
	ldrh	r3, [r3, r8]
	b	.L264
.L270:
	.align	2
.L269:
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
	.word	.LANCHOR0+2268
	.word	.LANCHOR0+2348
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
	.word	.LC69
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
	ldr	r2, .L272
	mov	r5, r0
	ldr	r1, .L272+4
	bl	sprintf
	add	r4, r5, r0
	mov	r0, r4
	bl	FtlPrintInfo2buf
	add	r0, r4, r0
	sub	r0, r0, r5
	ldmfd	sp, {r4, r5, fp, sp, pc}
.L273:
	.align	2
.L272:
	.word	.LC70
	.word	.LC71
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
	ldr	r4, .L298
	ldr	r2, [r4, #2556]
	ldr	r3, [r4, #2568]
	cmp	r2, r3
	bcs	.L275
	ldr	r2, [r4, #2328]
	mov	r3, #0
	ldrh	r1, [r4, #40]
	str	r3, [r4, #2548]
	sub	r2, r2, #2
.L276:
	cmp	r3, r1
	bcc	.L277
	ldr	r5, [r4, #2548]
	mov	r0, r5
	bl	__udivsi3
	str	r0, [r4, #2556]
	ldr	r0, [r4, #2552]
	ldrh	r1, [r4, #92]
	sub	r0, r5, r0
	bl	__udivsi3
	str	r0, [r4, #2548]
.L278:
	ldr	r6, [r4, #2568]
	ldr	r7, [r4, #2556]
	add	r3, r6, #256
	cmp	r3, r7
	bls	.L283
	ldr	r2, [r4, #2564]
	add	r3, r6, #768
	cmp	r3, r2
	bls	.L283
.L285:
	movw	r5, #65535
.L284:
	mov	r0, r5
	sub	sp, fp, #40
	ldmfd	sp, {r4, r5, r6, r7, r8, r9, r10, fp, sp, pc}
.L277:
	ldrh	r0, [r2, #2]!
	add	r3, r3, #1
	ldr	ip, [r4, #2548]
	add	r0, r0, ip
	str	r0, [r4, #2548]
	b	.L276
.L275:
	ldr	r3, [r4, #2564]
	cmp	r2, r3
	addhi	r3, r3, #1
	strhi	r3, [r4, #2564]
	movhi	r3, #0
	bls	.L278
.L280:
	ldrh	r2, [r4, #40]
	cmp	r3, r2
	bcs	.L278
	ldr	r0, [r4, #2328]
	lsl	r1, r3, #1
	add	r3, r3, #1
	ldrh	r2, [r0, r1]
	add	r2, r2, #1
	strh	r2, [r0, r1]	@ movhi
	b	.L280
.L283:
	movw	r3, #2344
	ldrh	r0, [r4, r3]
	add	r0, r0, r0, lsl #1
	asr	r0, r0, #2
	bl	GetFreeBlockMaxEraseCount
	add	r3, r6, #64
	mov	r9, r0
	cmp	r0, r3
	bcc	.L285
	ldr	r3, [r4, #2320]
	cmp	r3, #0
	beq	.L285
	ldrh	r10, [r4, #40]
	movw	r1, #65535
	ldr	lr, [r4, #2316]
	mov	r0, #0
	ldr	r8, [r4, #2328]
	mov	r2, r1
.L286:
	ldrh	ip, [r3]
	movw	r5, #65535
	cmp	ip, r5
	bne	.L289
	mov	r5, r2
.L288:
	movw	r3, #65535
	cmp	r5, r3
	beq	.L285
	lsl	r3, r5, #1
	ldrh	r10, [r8, r3]
	cmp	r6, r10
	bcs	.L290
	str	r3, [fp, #-44]
	bl	GetFreeBlockMinEraseCount
	ldr	r3, [fp, #-44]
	cmp	r6, r0
	strcc	r1, [r4, #2568]
.L290:
	cmp	r7, r10
	bls	.L285
	add	r2, r10, #128
	cmp	r9, r2
	ble	.L285
	add	r2, r10, #256
	cmp	r7, r2
	bhi	.L291
	ldr	r2, [r4, #2564]
	add	r10, r10, #768
	cmp	r10, r2
	bcs	.L285
.L291:
	str	r9, [sp, #8]
	mov	r1, r5
	ldrh	r2, [r8, r3]
	ldr	r0, .L298+4
	str	r2, [sp, #4]
	ldr	r2, [r4, #2324]
	ldrh	r3, [r2, r3]
	mov	r2, r7
	str	r3, [sp]
	ldr	r3, [r4, #2564]
	bl	sftl_printk
	mov	r3, #1
	str	r3, [r4, #3160]
	b	.L284
.L289:
	add	r0, r0, #1
	uxth	r0, r0
	cmp	r0, r10
	bhi	.L285
	ldrh	r5, [r3, #4]
	cmp	r5, #0
	beq	.L287
	sub	r3, r3, lr
	asr	r3, r3, #3
	uxth	r5, r3
	lsl	r3, r5, #1
	ldrh	r3, [r8, r3]
	cmp	r6, r3
	bcs	.L288
	cmp	r1, r3
	movhi	r1, r3
	movhi	r2, r5
.L287:
	add	r3, lr, ip, lsl #3
	b	.L286
.L299:
	.align	2
.L298:
	.word	.LANCHOR0
	.word	.LC72
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
	beq	.L301
	ldr	r3, .L302
	mov	r1, #0
	ldr	r2, [r3, #2324]
	lsl	r3, r0, #1
	strh	r1, [r2, r3]	@ movhi
	bl	INSERT_FREE_LIST
.L301:
	mov	r0, #0
	ldmfd	sp, {fp, sp, pc}
.L303:
	.align	2
.L302:
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
	bne	.L305
	movw	r2, #2776
	ldr	r1, .L318
	ldr	r0, .L318+4
	bl	sftl_printk
.L305:
	ldr	r5, .L318+8
	ldrh	r2, [r4, #2]
	ldrh	r3, [r5, #102]
	cmp	r2, r3
	bne	.L306
	movw	r2, #2777
	ldr	r1, .L318
	ldr	r0, .L318+4
	bl	sftl_printk
.L306:
	ldrh	r3, [r4, #4]
	cmp	r3, #0
	bne	.L307
	movw	r2, #2778
	ldr	r1, .L318
	ldr	r0, .L318+4
	bl	sftl_printk
.L307:
	ldrb	r2, [r4, #6]	@ zero_extendqisi2
	mov	r3, #0
	ldrh	r1, [r5, #32]
	movw	ip, #65535
	strb	r3, [r4, #10]
	add	r2, r4, r2, lsl #1
	ldrh	r0, [r2, #16]
	mov	r2, r3
.L308:
	cmp	r0, ip
	ldrb	r3, [r4, #6]	@ zero_extendqisi2
	beq	.L310
	ldrh	r2, [r4, #4]
	ldrh	r6, [r4, #2]
	sub	r2, r2, #1
	uxth	r2, r2
	orr	r6, r6, r0, lsl #10
	movw	r0, #65535
	strh	r2, [r4, #4]	@ movhi
.L312:
	add	r3, r3, #1
	uxtb	r3, r3
	cmp	r1, r3
	ldrheq	r3, [r4, #2]
	addeq	r3, r3, #1
	strheq	r3, [r4, #2]	@ movhi
	moveq	r3, #0
	add	ip, r4, r3, lsl #1
	ldrh	ip, [ip, #16]
	cmp	ip, r0
	beq	.L312
	strb	r3, [r4, #6]
	cmp	r2, #0
	ldrh	r3, [r4, #2]
	ldrh	r1, [r5, #102]
	sub	r3, r3, r1
	clz	r3, r3
	lsr	r3, r3, #5
	moveq	r3, #0
	cmp	r3, #0
	beq	.L304
	movw	r2, #2799
	ldr	r1, .L318
	ldr	r0, .L318+4
	bl	sftl_printk
.L304:
	mov	r0, r6
	ldmfd	sp, {r4, r5, r6, fp, sp, pc}
.L310:
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
	b	.L308
.L319:
	.align	2
.L318:
	.word	.LANCHOR1+111
	.word	.LC1
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
	push	{r4, r5, r6, r7, r8, fp, ip, lr, pc}
	sub	fp, ip, #4
	ldr	r3, .L325
	mov	lr, #12
	mov	r4, #1
	mov	r5, #20
	mov	r2, #0
	str	r2, [r3, #3164]
.L321:
	ldrh	r1, [r3, #32]
	uxth	r0, r2
	add	ip, r2, #1
	cmp	r0, r1
	bcc	.L322
	mov	ip, #12
	mov	lr, #0
.L323:
	ldr	r2, [r3, #3184]
	cmp	r1, r2
	bcc	.L324
	ldmfd	sp, {r4, r5, r6, r7, r8, fp, sp, pc}
.L322:
	uxth	r2, r2
	ldr	r6, [r3, #3168]
	mul	r0, lr, r2
	add	r1, r6, r0
	str	r4, [r1, #8]
	ldrh	r1, [r3, #110]
	mul	r1, r2, r1
	add	r7, r1, #3
	cmp	r1, #0
	movlt	r1, r7
	ldr	r7, [r3, #3172]
	bic	r1, r1, #3
	add	r1, r7, r1
	str	r1, [r6, r0]
	ldrh	r1, [r3, #112]
	ldr	r7, [r3, #3168]
	mul	r1, r2, r1
	add	r6, r7, r0
	add	r8, r1, #3
	cmp	r1, #0
	movlt	r1, r8
	ldr	r8, [r3, #3176]
	bic	r1, r1, #3
	add	r1, r8, r1
	str	r1, [r6, #4]
	ldr	r1, [r3, #3180]
	mla	r2, r5, r2, r1
	ldr	r1, [r7, r0]
	str	r1, [r2, #8]
	ldr	r1, [r6, #4]
	str	r1, [r2, #12]
	mov	r2, ip
	b	.L321
.L324:
	mul	r4, ip, r1
	ldr	r5, [r3, #3168]
	add	r2, r5, r4
	str	lr, [r2, #8]
	ldrh	r2, [r3, #110]
	mul	r2, r1, r2
	add	r0, r2, #3
	cmp	r2, #0
	movlt	r2, r0
	ldr	r0, [r3, #3172]
	bic	r2, r2, #3
	add	r2, r0, r2
	str	r2, [r5, r4]
	ldrh	r2, [r3, #112]
	ldr	r0, [r3, #3168]
	mul	r2, r1, r2
	add	r0, r0, r4
	add	r1, r1, #1
	uxth	r1, r1
	add	r4, r2, #3
	cmp	r2, #0
	movlt	r2, r4
	ldr	r4, [r3, #3176]
	bic	r2, r2, #3
	add	r2, r4, r2
	str	r2, [r0, #4]
	b	.L323
.L326:
	.align	2
.L325:
	.word	.LANCHOR0
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
	ldr	r3, .L334
	mov	lr, #0
	mov	r5, #20
	mov	r7, #12
	mov	r8, lr
	ldr	r6, [r3, #3184]
	ldr	r4, [r3, #3168]
.L328:
	uxth	r3, lr
	cmp	r1, r3
	ldmfdls	sp, {r4, r5, r6, r7, r8, r9, r10, fp, sp, pc}
	mla	ip, r5, r3, r0
	mov	r2, #0
.L329:
	uxth	r3, r2
	cmp	r6, r3
	bls	.L330
	mul	r3, r7, r3
	add	r2, r2, #1
	ldr	r10, [r4, r3]
	add	r9, r4, r3
	ldr	r3, [ip, #8]
	cmp	r10, r3
	bne	.L329
	str	r8, [r9, #8]
.L330:
	add	lr, lr, #1
	b	.L328
.L335:
	.align	2
.L334:
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
	ldr	r3, .L343
	mov	ip, #0
	mov	r6, #12
	mov	r7, #1
	mov	r8, #20
	ldr	r4, [r3, #3184]
	ldr	r5, [r3, #3168]
.L337:
	uxth	r2, ip
	cmp	r1, r2
	bhi	.L341
	ldmfd	sp, {r4, r5, r6, r7, r8, r9, fp, sp, pc}
.L341:
	mov	lr, #0
.L338:
	uxth	r3, lr
	cmp	r4, r3
	bls	.L339
	mla	r3, r6, r3, r5
	add	lr, lr, #1
	ldr	r9, [r3, #8]
	cmp	r9, #0
	bne	.L338
	mla	r2, r8, r2, r0
	ldr	lr, [r3]
	str	r7, [r3, #8]
	str	lr, [r2, #8]
	ldr	r3, [r3, #4]
	str	r3, [r2, #12]
.L339:
	add	ip, ip, #1
	b	.L337
.L344:
	.align	2
.L343:
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
	ldr	r2, .L350
	movw	r1, #3192
	uxth	r0, r0
	ldr	r3, [r2, #3188]
	ldrh	r2, [r2, r1]
	add	r2, r3, r2, lsl #1
.L346:
	cmp	r3, r2
	bne	.L348
	mov	r0, #0
	ldmfd	sp, {fp, sp, pc}
.L348:
	ldrh	r1, [r3], #2
	cmp	r1, r0
	bne	.L346
	mov	r0, #1
	ldmfd	sp, {fp, sp, pc}
.L351:
	.align	2
.L350:
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
	push	{r4, r5, r6, r7, fp, ip, lr, pc}
	sub	fp, ip, #4
	mov	r4, r0
	ubfx	r0, r0, #10, #16
	mov	r5, r1
	mov	r6, r2
	bl	P2V_block_in_plane
	ldr	ip, .L356
	movw	r3, #3192
	ldr	r2, [ip, #3188]
	ldrh	lr, [ip, r3]
	mov	r3, #0
	sub	r1, r2, #2
.L353:
	uxth	r7, r3
	cmp	r7, lr
	bcc	.L355
	bne	.L354
	lsl	r3, r7, #1
	strh	r0, [r2, r3]	@ movhi
	movw	r2, #3192
	ldrh	r3, [ip, r2]
	add	r3, r3, #1
	strh	r3, [ip, r2]	@ movhi
	b	.L354
.L355:
	ldrh	r7, [r1, #2]!
	add	r3, r3, #1
	cmp	r7, r0
	bne	.L353
.L354:
	ldr	r0, .L356+4
	mov	r3, #12
	ldrh	r2, [r0]
	mul	r3, r3, r2
	ldr	r2, [ip, #3196]
	add	r1, r2, r3
	stmib	r1, {r5, r6}
	str	r4, [r2, r3]
	ldrh	r3, [r0]
	add	r3, r3, #1
	strh	r3, [r0]	@ movhi
	ldmfd	sp, {r4, r5, r6, r7, fp, sp, pc}
.L357:
	.align	2
.L356:
	.word	.LANCHOR0
	.word	.LANCHOR0+3200
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
	push	{r4, r5, r6, r7, r8, fp, ip, lr, pc}
	sub	fp, ip, #4
	uxth	r4, r0
	movw	r7, #3202
	mov	r1, r4
	ldr	r0, .L363
	bl	sftl_printk
	ldr	r3, .L363+4
	ldrh	r8, [r3, r7]
	cmp	r4, r8
	beq	.L359
	movw	r5, #3204
	ldrh	r6, [r3, r5]
	cmp	r4, r6
	beq	.L359
	movw	ip, #3206
	ldrh	lr, [r3, ip]
	cmp	r4, lr
	beq	.L359
	movw	r1, #3208
	ldrh	r0, [r3, r1]
	cmp	r4, r0
	beq	.L359
	movw	r2, #65535
	cmp	r8, r2
	strheq	r4, [r3, r7]	@ movhi
	beq	.L359
	cmp	r6, r2
	strheq	r4, [r3, r5]	@ movhi
	beq	.L359
	cmp	lr, r2
	strheq	r4, [r3, ip]	@ movhi
	beq	.L359
	cmp	r0, r2
	strheq	r4, [r3, r1]	@ movhi
.L359:
	mov	r0, #0
	ldmfd	sp, {r4, r5, r6, r7, r8, fp, sp, pc}
.L364:
	.align	2
.L363:
	.word	.LC73
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
	ldr	r4, .L369
	uxth	r5, r0
	movw	r6, #3210
	mov	r0, r5
	bl	P2V_block_in_plane
	mov	r2, r5
	mov	r7, r0
	ldrh	r1, [r4, r6]
	ldr	r0, .L369+4
	bl	sftl_printk
	mov	r0, r7
	bl	FtlGcRefreshBlock
	ldrh	r3, [r4, r6]
	mov	r2, #0
	ldr	r0, .L369+8
.L366:
	uxth	r1, r2
	cmp	r3, r1
	bhi	.L368
	cmp	r3, #15
	movwls	r2, #3210
	addls	r1, r3, #1
	strhls	r1, [r4, r2]	@ movhi
	lslls	r3, r3, #1
	ldrls	r2, .L369+8
	strhls	r5, [r2, r3]	@ movhi
	b	.L367
.L368:
	add	r2, r2, #1
	add	r1, r0, r2, lsl #1
	ldrh	r1, [r1, #-2]
	cmp	r1, r5
	bne	.L366
.L367:
	mov	r0, #0
	ldmfd	sp, {r4, r5, r6, r7, fp, sp, pc}
.L370:
	.align	2
.L369:
	.word	.LANCHOR0
	.word	.LC74
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
	ldr	r4, .L377
	movw	r3, #3210
	ldrh	r2, [r4, r3]
	cmp	r2, #0
	beq	.L372
	movw	r3, #3202
	ldrh	r1, [r4, r3]
	movw	r3, #65535
	cmp	r1, r3
	bne	.L372
	movw	r3, #3246
	movw	r5, #3246
	ldrh	r1, [r4, r3]
	cmp	r1, r2
	movcs	r2, #0
	strhcs	r2, [r4, r3]	@ movhi
	ldrh	r3, [r4, r5]
	ldr	r2, .L377+4
	lsl	r3, r3, #1
	ldrh	r0, [r2, r3]
	bl	P2V_block_in_plane
	bl	FtlGcRefreshBlock
	ldrh	r3, [r4, r5]
	add	r3, r3, #1
	strh	r3, [r4, r5]	@ movhi
.L372:
	mov	r0, #0
	ldmfd	sp, {r4, r5, fp, sp, pc}
.L378:
	.align	2
.L377:
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
	ldr	r1, .L380
	bl	__kmalloc
	ldmfd	sp, {fp, sp, pc}
.L381:
	.align	2
.L380:
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
.L384:
	cmp	r4, r9
	bcc	.L390
	ldr	r0, .L392
	bl	sftl_printk
	ldmfd	sp, {r4, r5, r6, r7, r8, r9, r10, fp, sp, pc}
.L390:
	cmp	r5, #0
	bne	.L385
	mov	r3, r6
	mov	r2, r7
	mov	r1, r10
	ldr	r0, .L392+4
	bl	sftl_printk
.L385:
	cmp	r8, #4
	ldreq	r1, [r7, r4, lsl #2]
	ldreq	r0, .L392+8
	beq	.L391
	cmp	r8, #2
	lsleq	r3, r4, #1
	ldreq	r0, .L392+12
	ldrbne	r1, [r7, r4]	@ zero_extendqisi2
	ldrheq	r1, [r7, r3]
	ldrne	r0, .L392+16
.L391:
	add	r5, r5, #1
	bl	sftl_printk
	cmp	r5, #15
	bls	.L389
	mov	r5, #0
	ldr	r0, .L392
	bl	sftl_printk
.L389:
	add	r4, r4, #1
	add	r6, r6, r8
	b	.L384
.L393:
	.align	2
.L392:
	.word	.LC79
	.word	.LC75
	.word	.LC76
	.word	.LC77
	.word	.LC78
	.size	rknand_print_hex, .-rknand_print_hex
	.align	2
	.global	FlashReadPages
	.syntax unified
	.arm
	.fpu softvfp
	.type	FlashReadPages, %function
FlashReadPages:
	@ args = 0, pretend = 0, frame = 8
	@ frame_needed = 1, uses_anonymous_args = 0
	mov	ip, sp
	push	{r4, r5, r6, r7, r8, r9, r10, fp, ip, lr, pc}
	sub	fp, ip, #4
	sub	sp, sp, #8
	mov	r6, #0
	ldr	r5, .L412
	mov	r7, r1
	ldr	r9, .L412+4
	mov	r4, r0
	ldr	r10, .L412+8
	ldrh	r8, [r5, #12]
.L395:
	cmp	r6, r7
	bne	.L402
	mov	r0, #0
	sub	sp, fp, #40
	ldmfd	sp, {r4, r5, r6, r7, r8, r9, r10, fp, sp, pc}
.L402:
	ldr	r3, [r4, #8]
	cmp	r3, #0
	beq	.L396
	ldr	r3, [r4, #12]
	cmp	r3, #0
	bne	.L397
.L396:
	mov	r2, #101
	mov	r1, r9
	mov	r0, r10
	bl	sftl_printk
.L397:
	sub	r2, fp, #48
	sub	r1, fp, #44
	mov	r0, r4
	bl	l2p_addr_tran
	ldr	r3, [r4, #12]
	ldr	ip, [r5, #3260]
	ldr	r2, [r4, #8]
	ldr	r1, [fp, #-44]
	ldrb	r0, [fp, #-48]	@ zero_extendqisi2
	blx	ip
	str	r0, [r4]
	ldrh	r3, [r5, #14]
	cmp	r3, #4
	bne	.L399
	ldr	r3, [r4, #12]
	ldr	r2, [r4, #8]
	ldr	r1, [fp, #-44]
	ldr	ip, [r5, #3260]
	add	r3, r3, #8
	add	r2, r2, #2048
	ldrb	r0, [fp, #-48]	@ zero_extendqisi2
	add	r1, r8, r1
	blx	ip
	cmn	r0, #1
	beq	.L400
	ldr	r3, [r4, #12]
	ldr	r2, [r3, #12]
	cmn	r2, #1
	bne	.L399
	ldr	r2, [r3, #8]
	cmn	r2, #1
	bne	.L399
	ldr	r3, [r3]
	cmn	r3, #1
	beq	.L399
.L400:
	mvn	r3, #0
	str	r3, [r4]
.L399:
	add	r6, r6, #1
	add	r4, r4, #20
	b	.L395
.L413:
	.align	2
.L412:
	.word	.LANCHOR0
	.word	.LANCHOR1+130
	.word	.LC1
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
	ldr	r4, .L424
	mov	r6, #0
	mvn	r9, #0
	ldr	r3, [r4, #3284]
	add	r7, r4, #162
	ldr	r8, [r4, #3288]
	add	r10, r4, #3264
	str	r3, [r4, #3272]
	str	r8, [r4, #3276]
.L415:
	ldrh	r3, [r4, #54]
	cmp	r6, r3
	bcc	.L420
	mov	r0, #0
	ldmfd	sp, {r4, r5, r6, r7, r8, r9, r10, fp, sp, pc}
.L420:
	ldrh	r5, [r4, #98]
	strh	r9, [r7, #2]!	@ movhi
.L417:
	ldrh	r3, [r4, #98]
	sub	r5, r5, #1
	uxth	r5, r5
	sub	r2, r3, #16
	cmp	r5, r2
	ble	.L418
	mla	r3, r6, r3, r5
	mov	r2, #1
	mov	r1, r2
	mov	r0, r10
	lsl	r3, r3, #10
	str	r3, [r4, #3268]
	bl	FlashReadPages
	ldr	r3, [r4, #3264]
	cmn	r3, #1
	beq	.L417
	ldrh	r2, [r8]
	movw	r3, #61664
	cmp	r2, r3
	bne	.L417
	strh	r5, [r7]	@ movhi
.L418:
	add	r6, r6, #1
	b	.L415
.L425:
	.align	2
.L424:
	.word	.LANCHOR0
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
	ldr	r3, .L437
	lsl	r6, r0, #10
	mov	r2, r1
	mov	r7, r1
	sub	r0, fp, #116
	ldrheq	r5, [r3, #104]
	mov	r1, #1
	ldrhne	r5, [r3, #102]
	ldr	r3, .L437+4
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
	beq	.L430
.L429:
	mov	r0, r5
	sub	sp, fp, #32
	ldmfd	sp, {r4, r5, r6, r7, r8, fp, sp, pc}
.L433:
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
	bne	.L431
	ldr	r3, [fp, #-92]
	cmn	r3, #1
	bne	.L431
	ldr	r3, [fp, #-116]
	cmn	r3, #1
	subne	r4, r4, #1
	sxthne	r5, r4
	bne	.L430
.L431:
	add	r4, r4, #1
	sxth	r8, r4
.L430:
	cmp	r8, r5
	ble	.L433
	b	.L429
.L438:
	.align	2
.L437:
	.word	.LANCHOR0
	.word	ftl_temp_buf
	.size	FtlGetLastWrittenPage, .-FtlGetLastWrittenPage
	.align	2
	.global	FlashProgPages
	.syntax unified
	.arm
	.fpu softvfp
	.type	FlashProgPages, %function
FlashProgPages:
	@ args = 0, pretend = 0, frame = 32
	@ frame_needed = 1, uses_anonymous_args = 0
	mov	ip, sp
	push	{r4, r5, r6, r7, r8, r9, r10, fp, ip, lr, pc}
	sub	fp, ip, #4
	sub	sp, sp, #32
	mov	r9, r3
	ldr	r6, .L469
	mov	r10, r1
	mov	r8, r2
	mov	r4, r0
	mov	r5, r0
	mov	r7, #0
	ldrh	r3, [r6, #12]
	str	r3, [fp, #-72]
.L440:
	cmp	r7, r10
	bne	.L448
	cmp	r9, #0
	bne	.L455
.L468:
	mov	r0, #0
	sub	sp, fp, #40
	ldmfd	sp, {r4, r5, r6, r7, r8, r9, r10, fp, sp, pc}
.L448:
	ldr	r3, [r5, #8]
	cmp	r3, #0
	beq	.L441
	ldr	r3, [r5, #12]
	cmp	r3, #0
	bne	.L442
.L441:
	mov	r2, #131
	ldr	r1, .L469+4
	ldr	r0, .L469+8
	bl	sftl_printk
.L442:
	sub	r2, fp, #68
	sub	r1, fp, #64
	mov	r0, r5
	bl	l2p_addr_tran
	ldr	r3, [r5, #12]
	ldr	ip, [r6, #3256]
	ldr	r2, [r5, #8]
	ldr	r1, [fp, #-64]
	ldrb	r0, [fp, #-68]	@ zero_extendqisi2
	blx	ip
	cmp	r0, #0
	mvnne	r3, #0
	streq	r0, [r5]
	strne	r3, [r5]
	ldrh	r3, [r6, #14]
	cmp	r3, #4
	bne	.L446
	ldr	r0, [fp, #-72]
	ldr	r3, [r5, #12]
	ldr	r2, [r5, #8]
	ldr	r1, [fp, #-64]
	add	r3, r3, #8
	ldr	ip, [r6, #3256]
	add	r2, r2, #2048
	add	r1, r0, r1
	ldrb	r0, [fp, #-68]	@ zero_extendqisi2
	blx	ip
	cmp	r0, #0
	mvnne	r3, #0
	strne	r3, [r5]
.L446:
	add	r7, r7, #1
	add	r5, r5, #20
	b	.L440
.L453:
	mov	r3, #0
	mov	r2, r8
	str	r3, [r9]
	mov	r1, #1
	str	r3, [r6, #3292]
	sub	r0, fp, #60
	ldr	r3, [r4, #4]
	str	r9, [fp, #-52]
	str	r10, [fp, #-48]
	str	r3, [fp, #-56]
	bl	FlashReadPages
	ldr	r3, [fp, #-60]
	cmn	r3, #1
	bne	.L450
	ldr	r1, [r4, #4]
	ldr	r0, .L469+12
	str	r3, [fp, #-72]
	bl	sftl_printk
	ldr	r3, [fp, #-72]
	str	r3, [r4]
.L450:
	ldr	r3, [r4, #12]
	cmp	r3, #0
	beq	.L451
	ldr	r2, [r3]
	ldr	r3, [r6, #3292]
	cmp	r2, r3
	beq	.L451
	ldr	r1, [r4, #4]
	ldr	r0, .L469+16
	bl	sftl_printk
	mvn	r3, #0
	str	r3, [r4]
.L451:
	ldr	r3, [r4, #8]
	cmp	r3, #0
	beq	.L452
	ldr	r2, [r3]
	ldr	r3, [r9]
	cmp	r2, r3
	beq	.L452
	ldr	r1, [r4, #4]
	ldr	r0, .L469+20
	bl	sftl_printk
	mvn	r3, #0
	str	r3, [r4]
.L452:
	add	r5, r5, #1
	add	r4, r4, #20
.L449:
	cmp	r7, r5
	bne	.L453
	b	.L468
.L455:
	mov	r5, #0
	ldr	r9, .L469+24
	ldr	r10, .L469+28
	b	.L449
.L470:
	.align	2
.L469:
	.word	.LANCHOR0
	.word	.LANCHOR1+145
	.word	.LC1
	.word	.LC80
	.word	.LC81
	.word	.LC82
	.word	check_buf
	.word	.LANCHOR0+3292
	.size	FlashProgPages, .-FlashProgPages
	.align	2
	.global	FlashEraseBlocks
	.syntax unified
	.arm
	.fpu softvfp
	.type	FlashEraseBlocks, %function
FlashEraseBlocks:
	@ args = 0, pretend = 0, frame = 8
	@ frame_needed = 1, uses_anonymous_args = 0
	mov	ip, sp
	push	{r4, r5, r6, r7, r8, fp, ip, lr, pc}
	sub	fp, ip, #4
	sub	sp, sp, #8
	mov	r6, #0
	ldr	r5, .L482
	mov	r7, r2
	mov	r4, r0
	ldrh	r8, [r5, #12]
.L472:
	cmp	r6, r7
	bne	.L478
	mov	r0, #0
	sub	sp, fp, #32
	ldmfd	sp, {r4, r5, r6, r7, r8, fp, sp, pc}
.L478:
	sub	r2, fp, #40
	sub	r1, fp, #36
	mov	r0, r4
	bl	l2p_addr_tran
	ldr	r3, [r5, #3252]
	ldr	r1, [fp, #-36]
	ldrb	r0, [fp, #-40]	@ zero_extendqisi2
	blx	r3
	cmp	r0, #0
	mvnne	r3, #0
	streq	r0, [r4]
	strne	r3, [r4]
	ldrh	r3, [r5, #14]
	cmp	r3, #4
	bne	.L476
	ldr	r1, [fp, #-36]
	ldr	r3, [r5, #3252]
	ldrb	r0, [fp, #-40]	@ zero_extendqisi2
	add	r1, r8, r1
	blx	r3
	cmp	r0, #0
	mvnne	r3, #0
	strne	r3, [r4]
.L476:
	add	r6, r6, #1
	add	r4, r4, #20
	b	.L472
.L483:
	.align	2
.L482:
	.word	.LANCHOR0
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
	push	{r4, r5, r6, fp, ip, lr, pc}
	sub	fp, ip, #4
	uxth	r5, r0
	movw	r2, #65533
	uxth	r1, r1
	sub	r3, r5, #1
	uxth	r3, r3
	cmp	r3, r2
	ldmfdhi	sp, {r4, r5, r6, fp, sp, pc}
	ldr	r4, .L493
	ldrh	r3, [r4, #218]
	cmp	r3, #1024
	ldmfdeq	sp, {r4, r5, r6, fp, sp, pc}
	cmp	r1, #0
	beq	.L486
	mov	r0, r5
	bl	P2V_block_in_plane
	mov	r6, r0
	ldr	r0, [r4, #3804]
	lsl	r3, r5, #10
	mov	r2, #1
	mov	r1, r2
	str	r3, [r0, #4]
	bl	FlashEraseBlocks
	ldr	r2, [r4, #2328]
	lsl	r0, r6, #1
	ldrh	r3, [r2, r0]
	add	r3, r3, #1
	strh	r3, [r2, r0]	@ movhi
	ldr	r3, [r4, #2560]
	add	r3, r3, #1
	str	r3, [r4, #2560]
.L486:
	ldrh	r3, [r4, #218]
	add	r3, r3, #1
	strh	r3, [r4, #218]	@ movhi
	ldrh	r3, [r4, #216]
	add	r2, r4, r3, lsl #1
	add	r3, r3, #1
	ubfx	r3, r3, #0, #10
	strh	r5, [r2, #220]	@ movhi
	strh	r3, [r4, #216]	@ movhi
	ldmfd	sp, {r4, r5, r6, fp, sp, pc}
.L494:
	.align	2
.L493:
	.word	.LANCHOR0
	.size	FtlFreeSysBlkQueueIn, .-FtlFreeSysBlkQueueIn
	.align	2
	.global	FtlLowFormatEraseBlock
	.syntax unified
	.arm
	.fpu softvfp
	.type	FtlLowFormatEraseBlock, %function
FtlLowFormatEraseBlock:
	@ args = 0, pretend = 0, frame = 24
	@ frame_needed = 1, uses_anonymous_args = 0
	mov	ip, sp
	push	{r4, r5, r6, r7, r8, r9, r10, fp, ip, lr, pc}
	sub	fp, ip, #4
	sub	sp, sp, #24
	uxth	r3, r0
	mov	r10, #0
	uxtb	r6, r1
	str	r3, [fp, #-48]
	mov	r5, r10
	ldr	r3, .L537
	mov	r4, r10
	ldr	r2, [fp, #-48]
	mov	r9, #20
	mov	r8, r3
	str	r2, [r3, #3808]
.L496:
	ldrh	r1, [r8, #32]
	uxth	r2, r10
	cmp	r1, r2
	bhi	.L500
	cmp	r5, #0
	beq	.L495
	mov	r7, #0
	mov	r9, #20
	mov	r2, r5
	mov	r1, #0
	ldr	r0, [r8, #3804]
	bl	FlashEraseBlocks
.L503:
	uxth	r2, r7
	cmp	r5, r2
	bhi	.L505
.L506:
	cmp	r6, #0
	ldrhne	r3, [r8, #104]
	moveq	r3, #6
	ldr	r8, .L537
	streq	r3, [fp, #-60]
	moveq	r3, #1
	strne	r3, [fp, #-52]
	lsrne	r3, r3, #2
	mov	r10, r8
	streq	r3, [fp, #-52]
	strne	r3, [fp, #-60]
	movne	r3, #1
	strne	r3, [fp, #-56]
	mov	r3, #0
	streq	r6, [fp, #-56]
	str	r3, [fp, #-44]
.L515:
	mov	r7, #0
	mov	r5, r7
.L507:
	ldrh	r1, [r8, #32]
	uxth	r3, r7
	cmp	r1, r3
	bhi	.L510
	cmp	r5, #0
	beq	.L495
	ldr	r2, [fp, #-56]
	mov	r3, #1
	mov	r1, r5
	ldr	r0, [r8, #3804]
	bl	FlashProgPages
	mov	r9, #0
	mov	r2, #20
.L512:
	uxth	r3, r9
	cmp	r5, r3
	bhi	.L514
	ldr	r3, [fp, #-44]
	ldr	r2, [fp, #-60]
	add	r7, r3, r2
	uxth	r3, r7
	str	r3, [fp, #-44]
	ldr	r2, [fp, #-44]
	ldr	r3, [fp, #-52]
	cmp	r3, r2
	bhi	.L515
	mov	r7, #0
	mov	r9, #20
.L516:
	uxth	r3, r7
	cmp	r5, r3
	bhi	.L518
	ldr	r3, [fp, #-48]
	adds	r6, r6, #0
	movne	r6, #1
	cmp	r3, #63
	movhi	r8, r6
	orrls	r8, r6, #1
	cmp	r8, #0
	beq	.L495
	mov	r2, r5
	ldr	r1, [fp, #-56]
	ldr	r0, [r10, #3804]
	bl	FlashEraseBlocks
.L495:
	mov	r0, r4
	sub	sp, fp, #40
	ldmfd	sp, {r4, r5, r6, r7, r8, r9, r10, fp, sp, pc}
.L500:
	uxth	r2, r10
	ldr	r0, [r8, #3804]
	mov	r3, #0
	mul	r1, r9, r2
	add	r2, r8, r2
	str	r3, [r0, r1]
	ldr	r1, [fp, #-48]
	ldrb	r0, [r2, #60]	@ zero_extendqisi2
	bl	V2P_block
	cmp	r6, #0
	mov	r7, r0
	beq	.L497
	bl	IsBlkInVendorPart
	cmp	r0, #0
	bne	.L498
.L497:
	mov	r0, r7
	bl	FtlBbmIsBadBlock
	cmp	r0, #0
	addne	r4, r4, #1
	uxthne	r4, r4
	bne	.L498
	ldr	r1, [r8, #3804]
	lsl	r7, r7, #10
	ldr	r2, [r8, #3812]
	mla	r1, r9, r5, r1
	str	r2, [r1, #8]
	ldrh	r2, [r8, #112]
	str	r7, [r1, #4]
	mul	r2, r5, r2
	add	r5, r5, #1
	uxth	r5, r5
	add	r0, r2, #3
	cmp	r2, #0
	movlt	r2, r0
	ldr	r0, [r8, #3816]
	bic	r2, r2, #3
	add	r2, r0, r2
	str	r2, [r1, #12]
.L498:
	add	r10, r10, #1
	b	.L496
.L505:
	mul	r2, r9, r7
	ldr	r1, [r8, #3804]
	add	r0, r1, r2
	ldr	r2, [r1, r2]
	cmn	r2, #1
	bne	.L504
	ldr	r0, [r0, #4]
	add	r4, r4, #1
	uxth	r4, r4
	ubfx	r0, r0, #10, #16
	bl	FtlBbmMapBadBlock
.L504:
	add	r7, r7, #1
	b	.L503
.L510:
	uxth	r3, r7
	mov	r2, #20
	ldr	r0, [r8, #3804]
	mul	r1, r2, r3
	mov	r2, #0
	add	r3, r8, r3
	str	r2, [r0, r1]
	ldr	r1, [fp, #-48]
	ldrb	r0, [r3, #60]	@ zero_extendqisi2
	bl	V2P_block
	cmp	r6, #0
	mov	r9, r0
	beq	.L508
	bl	IsBlkInVendorPart
	cmp	r0, #0
	bne	.L509
.L508:
	mov	r0, r9
	bl	FtlBbmIsBadBlock
	cmp	r0, #0
	bne	.L509
	ldr	r1, [r8, #3804]
	mov	r3, #20
	mla	r1, r3, r5, r1
	ldr	r3, [fp, #-44]
	add	r9, r3, r9, lsl #10
	ldr	r3, [r8, #3820]
	str	r9, [r1, #4]
	str	r3, [r1, #8]
	ldrh	r3, [r8, #112]
	mul	r3, r5, r3
	add	r5, r5, #1
	uxth	r5, r5
	add	r0, r3, #3
	cmp	r3, #0
	movlt	r3, r0
	ldr	r0, [r8, #3812]
	bic	r3, r3, #3
	add	r3, r0, r3
	str	r3, [r1, #12]
.L509:
	add	r7, r7, #1
	b	.L507
.L514:
	mul	r3, r2, r9
	ldr	r1, [r10, #3804]
	add	r0, r1, r3
	ldr	r3, [r1, r3]
	cmp	r3, #0
	beq	.L513
	ldr	r0, [r0, #4]
	add	r4, r4, #1
	str	r2, [fp, #-64]
	uxth	r4, r4
	ubfx	r0, r0, #10, #16
	bl	FtlBbmMapBadBlock
	ldr	r2, [fp, #-64]
.L513:
	add	r9, r9, #1
	b	.L512
.L518:
	cmp	r6, #0
	beq	.L517
	mul	r3, r9, r7
	ldr	r2, [r10, #3804]
	add	r1, r2, r3
	ldr	r3, [r2, r3]
	cmp	r3, #0
	bne	.L517
	ldr	r0, [r1, #4]
	mov	r1, #1
	ubfx	r0, r0, #10, #16
	bl	FtlFreeSysBlkQueueIn
.L517:
	add	r7, r7, #1
	b	.L516
.L538:
	.align	2
.L537:
	.word	.LANCHOR0
	.size	FtlLowFormatEraseBlock, .-FtlLowFormatEraseBlock
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
	push	{r4, r5, r6, r7, fp, ip, lr, pc}
	sub	fp, ip, #4
	ldr	r4, .L546
	ldr	r6, .L546+4
.L540:
	ldrh	r1, [r4, #218]
	cmp	r1, #0
	beq	.L541
	ldrh	r3, [r4, #214]
	sub	r1, r1, #1
	strh	r1, [r4, #218]	@ movhi
	add	r2, r4, r3, lsl #1
	add	r3, r3, #1
	ubfx	r3, r3, #0, #10
	ldrh	r5, [r2, #220]
	strh	r3, [r4, #214]	@ movhi
	mov	r0, r5
	bl	P2V_block_in_plane
	mov	r7, r0
	ldr	r0, [r4, #3804]
	lsl	r3, r5, #10
	mov	r2, #1
	mov	r1, r2
	str	r3, [r0, #4]
	bl	FlashEraseBlocks
	ldr	r2, [r4, #2328]
	lsl	r0, r7, #1
	ldrh	r3, [r2, r0]
	add	r3, r3, #1
	strh	r3, [r2, r0]	@ movhi
	movw	r2, #65533
	ldr	r3, [r4, #2560]
	add	r3, r3, #1
	str	r3, [r4, #2560]
	sub	r3, r5, #1
	uxth	r3, r3
	cmp	r3, r2
	bhi	.L542
	mov	r0, r5
	ldmfd	sp, {r4, r5, r6, r7, fp, sp, pc}
.L541:
	ldr	r0, .L546+8
	bl	sftl_printk
.L544:
	b	.L544
.L542:
	ldrh	r2, [r4, #218]
	mov	r1, r5
	mov	r0, r6
	bl	sftl_printk
	b	.L540
.L547:
	.align	2
.L546:
	.word	.LANCHOR0
	.word	.LC84
	.word	.LC83
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
.L549:
	uxth	r5, r3
	cmp	r5, r1
	bcs	.L552
	mov	r7, r2
	add	r3, r3, #1
	ldrh	r6, [r7]
	add	r2, r2, #2
	cmp	r6, #0
	bne	.L549
	bl	FtlFreeSysBlkQueueOut
	sub	r3, r0, #1
	movw	r2, #65533
	uxth	r3, r3
	mov	r1, r0
	strh	r0, [r7]	@ movhi
	cmp	r3, r2
	bls	.L550
	ldr	r3, .L556
	ldr	r0, .L556+4
	ldrh	r2, [r3, #218]
	bl	sftl_printk
.L551:
	b	.L551
.L550:
	ldr	r3, [r4, #28]
	strh	r6, [r4, #2]	@ movhi
	strh	r5, [r4]	@ movhi
	add	r3, r3, #1
	str	r3, [r4, #28]
	ldrh	r3, [r4, #8]
	add	r3, r3, #1
	strh	r3, [r4, #8]	@ movhi
.L552:
	ldrh	r3, [r4, #10]
	cmp	r3, r5
	bhi	.L554
	movw	r2, #603
	ldr	r1, .L556+8
	ldr	r0, .L556+12
	bl	sftl_printk
.L554:
	mov	r0, #0
	ldmfd	sp, {r4, r5, r6, r7, fp, sp, pc}
.L557:
	.align	2
.L556:
	.word	.LANCHOR0
	.word	.LC85
	.word	.LANCHOR1+160
	.word	.LC1
	.size	ftl_map_blk_alloc_new_blk, .-ftl_map_blk_alloc_new_blk
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
	uxth	r4, r0
	cmp	r4, #11
	movls	r4, #0
	bls	.L558
	ldr	r5, .L561
	sub	r0, fp, #84
	mov	r2, #32
	mov	r1, #165
	str	r0, [fp, #-92]
	lsl	r4, r4, #10
	str	r5, [fp, #-96]
	bl	memset
	ldr	r3, .L561+4
	mov	r2, #1
	mov	r1, r2
	sub	r0, fp, #104
	str	r4, [fp, #-100]
	str	r3, [r5]
	str	r3, [r5, #4]
	bl	FlashEraseBlocks
	mov	r3, #1
	sub	r0, fp, #104
	mov	r2, r3
	mov	r1, r3
	bl	FlashProgPages
	ldr	r4, [fp, #-104]
	mov	r2, #1
	mov	r1, #0
	sub	r0, fp, #104
	adds	r4, r4, #0
	movne	r4, #1
	rsb	r4, r4, #0
	bl	FlashEraseBlocks
.L558:
	mov	r0, r4
	sub	sp, fp, #20
	ldmfd	sp, {r4, r5, fp, sp, pc}
.L562:
	.align	2
.L561:
	.word	ftl_temp_buf
	.word	1515870810
	.size	FlashTestBlk, .-FlashTestBlk
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
	ldr	r5, .L570
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
	bls	.L564
	mov	r2, #256
	mov	r1, #255
	mov	r0, r4
	bl	memset
	mov	r0, #0
.L564:
	ldrh	r3, [r5, #14]
	cmp	r3, #4
	moveq	r3, r4
	addeq	r1, r3, r0, lsl #1
	beq	.L566
	ldmfd	sp, {r4, r5, r6, fp, sp, pc}
.L567:
	ldrh	r2, [r3]
	lsr	r2, r2, #1
	strh	r2, [r3], #2	@ movhi
.L566:
	cmp	r3, r1
	bne	.L567
	ldmfd	sp, {r4, r5, r6, fp, sp, pc}
.L571:
	.align	2
.L570:
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
	beq	.L573
	bl	memset
.L573:
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
	push	{r4, r5, r6, r7, r8, fp, ip, lr, pc}
	sub	fp, ip, #4
	ldr	r4, .L678
	movw	r3, #65535
	movw	r2, #3202
	mov	r5, #0
	mov	r6, #12
	str	r3, [r4, #3828]
	mvn	r3, #0
	strh	r3, [r4, r2]	@ movhi
	movw	r2, #3204
	strh	r3, [r4, r2]	@ movhi
	mov	r2, #32
	movw	r3, #3148
	ldrh	r0, [r4, #106]
	strh	r2, [r4, r3]	@ movhi
	mov	r2, #128
	movw	r3, #3150
	str	r5, [r4, #2540]
	strh	r2, [r4, r3]	@ movhi
	movw	r3, #3156
	strh	r5, [r4, r3]	@ movhi
	movw	r3, #3210
	strh	r5, [r4, r3]	@ movhi
	lsl	r0, r0, #1
	movw	r3, #3246
	str	r5, [r4, #2544]
	strh	r5, [r4, r3]	@ movhi
	mov	r7, #20
	str	r5, [r4, #2528]
	str	r5, [r4, #2516]
	str	r5, [r4, #2512]
	str	r5, [r4, #2520]
	str	r5, [r4, #2524]
	str	r5, [r4, #2508]
	str	r5, [r4, #2548]
	str	r5, [r4, #2552]
	str	r5, [r4, #2560]
	str	r5, [r4, #2564]
	str	r5, [r4, #2568]
	str	r5, [r4, #3824]
	str	r5, [r4, #3160]
	str	r5, [r4, #3832]
	str	r5, [r4, #3152]
	str	r5, [r4, #3808]
	bl	ftl_malloc
	str	r0, [r4, #3188]
	ldrh	r0, [r4, #106]
	mul	r0, r6, r0
	bl	ftl_malloc
	ldrh	r3, [r4, #32]
	str	r0, [r4, #3196]
	mul	r7, r7, r3
	lsl	r8, r7, #2
	mov	r0, r8
	bl	ftl_malloc
	str	r0, [r4, #3836]
	mov	r0, r7
	bl	ftl_malloc
	str	r0, [r4, #3840]
	mov	r0, r8
	bl	ftl_malloc
	str	r0, [r4, #3844]
	mov	r0, r7
	bl	ftl_malloc
	str	r0, [r4, #3804]
	mov	r0, r7
	bl	ftl_malloc
	ldrh	r3, [r4, #32]
	ldrh	r7, [r4, #110]
	str	r0, [r4, #3180]
	lsl	r3, r3, #1
	mov	r0, r7
	add	r3, r3, #1
	str	r3, [r4, #3184]
	bl	ftl_malloc
	str	r0, [r4, #3284]
	mov	r0, r7
	bl	ftl_malloc
	str	r0, [r4, #3848]
	mov	r0, r7
	bl	ftl_malloc
	str	r0, [r4, #3852]
	ldr	r0, [r4, #3184]
	mul	r0, r0, r7
	bl	ftl_malloc
	str	r0, [r4, #3172]
	mov	r0, r7
	bl	ftl_malloc
	str	r0, [r4, #3820]
	mov	r0, r7
	bl	ftl_malloc
	str	r0, [r4, #3812]
	ldr	r0, [r4, #3184]
	mul	r0, r6, r0
	bl	ftl_malloc
	ldrh	r3, [r4, #112]
	ldrh	r7, [r4, #32]
	str	r0, [r4, #3168]
	mul	r7, r7, r3
	mov	r0, r7
	bl	ftl_malloc
	str	r0, [r4, #3288]
	lsl	r0, r7, #2
	add	r7, r4, #3856
	bl	ftl_malloc
	ldrh	r3, [r4, #112]
	str	r0, [r4, #3816]
	ldr	r0, [r4, #3184]
	mul	r0, r0, r3
	bl	ftl_malloc
	str	r0, [r4, #3176]
	ldrh	r0, [r4, #42]
	lsl	r0, r0, #1
	uxth	r0, r0
	strh	r0, [r7]	@ movhi
	bl	ftl_malloc
	str	r0, [r4, #3860]
	ldrh	r0, [r7]
	ldr	r3, .L678+4
	add	r0, r0, #544
	add	r0, r0, #3
	lsr	r0, r0, #9
	strh	r0, [r7]	@ movhi
	and	r0, r3, r0, lsl #9
	bl	ftl_malloc
	ldrh	r7, [r4, #42]
	str	r0, [r4, #3864]
	add	r0, r0, #32
	str	r0, [r4, #2328]
	lsl	r7, r7, #1
	mov	r0, r7
	bl	ftl_malloc
	str	r0, [r4, #3868]
	mov	r0, r7
	bl	ftl_malloc
	ldr	r7, [r4, #128]
	str	r0, [r4, #2324]
	lsl	r7, r7, #1
	mov	r0, r7
	bl	ftl_malloc
	str	r0, [r4, #3872]
	mov	r0, r7
	bl	ftl_malloc
	str	r0, [r4, #3876]
	ldrh	r0, [r4, #42]
	lsr	r0, r0, #3
	add	r0, r0, #4
	bl	ftl_malloc
	str	r0, [r4, #24]
	ldrh	r0, [r4, #120]
	lsl	r0, r0, #1
	bl	ftl_malloc
	str	r0, [r4, #148]
	ldrh	r0, [r4, #120]
	lsl	r0, r0, #1
	bl	ftl_malloc
	str	r0, [r4, #3880]
	ldrh	r0, [r4, #120]
	lsl	r0, r0, #2
	bl	ftl_malloc
	str	r0, [r4, #3884]
	ldrh	r0, [r4, #122]
	lsl	r0, r0, #2
	bl	ftl_malloc
	ldrh	r2, [r4, #122]
	mov	r1, r5
	str	r0, [r4, #3888]
	add	r5, r4, #3904
	lsl	r2, r2, #2
	bl	ftl_memset
	ldrh	r0, [r4, #136]
	lsl	r0, r0, #2
	bl	ftl_malloc
	str	r0, [r4, #3892]
	ldr	r0, [r4, #128]
	lsl	r0, r0, #2
	bl	ftl_malloc
	str	r0, [r4, #3896]
	ldrh	r0, [r4, #138]
	mul	r0, r6, r0
	bl	ftl_malloc
	ldrh	r3, [r4, #138]
	str	r0, [r4, #2492]
	ldrh	r0, [r4, #110]
	mul	r0, r0, r3
	bl	ftl_malloc
	str	r0, [r4, #3900]
	ldrh	r0, [r4, #42]
	lsl	r0, r0, #3
	bl	ftl_malloc
	str	r0, [r4, #2316]
	ldrh	r0, [r4, #98]
	ldrh	r3, [r4, #54]
	add	r0, r0, #31
	asr	r0, r0, #5
	strh	r0, [r5]	@ movhi
	mul	r0, r0, r3
	lsl	r0, r0, #2
	bl	ftl_malloc
	ldrh	r2, [r5]
	add	ip, r4, #180
	ldrh	lr, [r4, #54]
	mov	r3, #1
	str	r0, [r4, #180]
	lsl	r2, r2, #2
	mov	r1, r2
.L578:
	cmp	r3, lr
	bcc	.L579
	ldr	r2, .L678+8
	mov	r1, #0
	add	r3, r2, r3, lsl #2
	add	r2, r2, #56
	add	r3, r3, #24
.L580:
	cmp	r2, r3
	bne	.L581
	ldr	r3, [r4, #3872]
	cmp	r3, #0
	bne	.L582
.L584:
	ldr	r1, .L678+12
	ldr	r0, .L678+16
	bl	sftl_printk
	mvn	r0, #0
	ldmfd	sp, {r4, r5, r6, r7, r8, fp, sp, pc}
.L579:
	ldr	r0, [r4, #180]
	add	r3, r3, #1
	add	r0, r0, r1
	add	r1, r1, r2
	str	r0, [ip, #4]!
	b	.L578
.L581:
	str	r1, [r3, #4]!
	b	.L580
.L582:
	ldr	r3, [r4, #3876]
	cmp	r3, #0
	beq	.L584
	ldr	r3, [r4, #3892]
	cmp	r3, #0
	beq	.L584
	ldr	r3, [r4, #3896]
	cmp	r3, #0
	beq	.L584
	ldr	r3, [r4, #2492]
	cmp	r3, #0
	beq	.L584
	ldr	r3, [r4, #3900]
	cmp	r3, #0
	beq	.L584
	ldr	r3, [r4, #2316]
	cmp	r3, #0
	beq	.L584
	ldr	r3, [r4, #180]
	cmp	r3, #0
	beq	.L584
	ldr	r3, [r4, #2324]
	cmp	r3, #0
	beq	.L584
	ldr	r3, [r4, #3188]
	cmp	r3, #0
	beq	.L584
	ldr	r3, [r4, #3196]
	cmp	r3, #0
	beq	.L584
	ldr	r3, [r4, #3836]
	cmp	r3, #0
	beq	.L584
	ldr	r3, [r4, #3844]
	cmp	r3, #0
	beq	.L584
	ldr	r3, [r4, #3804]
	cmp	r3, #0
	beq	.L584
	ldr	r3, [r4, #3180]
	cmp	r3, #0
	beq	.L584
	ldr	r3, [r4, #3840]
	cmp	r3, #0
	beq	.L584
	ldr	r3, [r4, #3284]
	cmp	r3, #0
	beq	.L584
	ldr	r3, [r4, #3848]
	cmp	r3, #0
	beq	.L584
	ldr	r3, [r4, #3852]
	cmp	r3, #0
	beq	.L584
	ldr	r3, [r4, #3172]
	cmp	r3, #0
	beq	.L584
	ldr	r3, [r4, #3820]
	cmp	r3, #0
	beq	.L584
	ldr	r3, [r4, #3812]
	cmp	r3, #0
	beq	.L584
	ldr	r3, [r4, #3168]
	cmp	r3, #0
	beq	.L584
	ldr	r3, [r4, #3288]
	cmp	r3, #0
	beq	.L584
	ldr	r3, [r4, #3816]
	cmp	r3, #0
	beq	.L584
	ldr	r3, [r4, #3176]
	cmp	r3, #0
	beq	.L584
	ldr	r3, [r4, #2328]
	cmp	r3, #0
	beq	.L584
	ldr	r3, [r4, #3860]
	cmp	r3, #0
	beq	.L584
	ldr	r3, [r4, #148]
	cmp	r3, #0
	beq	.L584
	ldr	r3, .L678
	ldr	r2, [r3, #3880]
	cmp	r2, #0
	beq	.L584
	ldr	r2, [r3, #3884]
	cmp	r2, #0
	beq	.L584
	ldr	r3, [r3, #3888]
	cmp	r3, #0
	beq	.L584
	mov	r0, #0
	ldmfd	sp, {r4, r5, r6, r7, r8, fp, sp, pc}
.L679:
	.align	2
.L678:
	.word	.LANCHOR0
	.word	33553920
	.word	.LANCHOR0+152
	.word	.LANCHOR1+186
	.word	.LC86
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
	ldr	r6, .L686
	mov	r4, r0
	mov	r7, r1
	ldr	r8, .L686+4
	sub	r5, r4, #2
	ldrh	r2, [r6]
	add	r4, r4, #1020
	add	r4, r4, #2
	sub	r6, r6, #3904
	mov	r1, #0
	mov	r0, r7
	lsl	r2, r2, #2
	bl	ftl_memset
.L683:
	ldrh	r3, [r5, #2]
	movw	r2, #65535
	cmp	r3, r2
	ldmfdeq	sp, {r4, r5, r6, r7, r8, fp, sp, pc}
	ldrh	r2, [r6, #98]
	cmp	r2, r3
	bhi	.L682
	mov	r2, #74
	mov	r1, r8
	ldr	r0, .L686+8
	bl	sftl_printk
.L682:
	ldrh	r3, [r5, #2]!
	mov	r0, #1
	cmp	r4, r5
	lsr	r1, r3, #5
	and	r3, r3, #31
	ldr	r2, [r7, r1, lsl #2]
	orr	r3, r2, r0, lsl r3
	str	r3, [r7, r1, lsl #2]
	bne	.L683
	ldmfd	sp, {r4, r5, r6, r7, r8, fp, sp, pc}
.L687:
	.align	2
.L686:
	.word	.LANCHOR0+3904
	.word	.LANCHOR1+197
	.word	.LC1
	.size	FtlBbt2Bitmap, .-FtlBbt2Bitmap
	.align	2
	.global	ftl_free_no_use_map_blk
	.syntax unified
	.arm
	.fpu softvfp
	.type	ftl_free_no_use_map_blk, %function
ftl_free_no_use_map_blk:
	@ args = 0, pretend = 0, frame = 8
	@ frame_needed = 1, uses_anonymous_args = 0
	mov	ip, sp
	push	{r4, r5, r6, r7, r8, r9, r10, fp, ip, lr, pc}
	sub	fp, ip, #4
	sub	sp, sp, #8
	mov	r1, #0
	ldrh	r2, [r0, #10]
	mov	r4, r0
	ldr	r5, [r0, #20]
	ldr	r9, [r0, #12]
	ldr	r6, [r0, #24]
	lsl	r2, r2, #1
	mov	r0, r5
	bl	ftl_memset
	mov	r2, #0
.L689:
	ldrh	r1, [r4, #6]
	uxth	r3, r2
	cmp	r1, r3
	bhi	.L693
	ldrh	r2, [r5]
	mov	r7, #0
	ldr	ip, .L702
	mov	r10, r7
.L694:
	ldrh	r1, [r4, #10]
	uxth	r3, r7
	cmp	r1, r3
	bhi	.L698
	mov	r0, r10
	sub	sp, fp, #40
	ldmfd	sp, {r4, r5, r6, r7, r8, r9, r10, fp, sp, pc}
.L693:
	uxth	r3, r2
	mov	r1, #0
	ldr	r0, [r6, r3, lsl #2]
	ubfx	r0, r0, #10, #16
.L690:
	ldrh	ip, [r4, #10]
	uxth	r3, r1
	cmp	ip, r3
	addls	r2, r2, #1
	bls	.L689
.L692:
	uxth	r3, r1
	add	r1, r1, #1
	lsl	r3, r3, #1
	ldrh	ip, [r9, r3]
	cmp	ip, r0
	ldrheq	ip, [r5, r3]
	addeq	ip, ip, #1
	strheq	ip, [r5, r3]	@ movhi
	b	.L690
.L698:
	ldrh	r1, [r4]
	uxth	r6, r7
	cmp	r1, r3
	bne	.L695
	ldrh	r0, [r4, #2]
	ldrh	r1, [ip, #104]
	cmp	r0, r1
	lslcc	r0, r6, #1
	strhcc	r1, [r5, r0]	@ movhi
.L695:
	lsl	r6, r6, #1
	ldrh	r8, [r5, r6]
	cmp	r2, r8
	movhi	r10, r3
	movhi	r2, r8
	cmp	r8, #0
	bne	.L697
	ldrh	r0, [r9, r6]
	cmp	r0, #0
	beq	.L697
	mov	r1, #1
	str	ip, [fp, #-48]
	str	r2, [fp, #-44]
	bl	FtlFreeSysBlkQueueIn
	strh	r8, [r9, r6]	@ movhi
	ldr	ip, [fp, #-48]
	ldrh	r3, [r4, #8]
	ldr	r2, [fp, #-44]
	sub	r3, r3, #1
	strh	r3, [r4, #8]	@ movhi
.L697:
	add	r7, r7, #1
	b	.L694
.L703:
	.align	2
.L702:
	.word	.LANCHOR0
	.size	ftl_free_no_use_map_blk, .-ftl_free_no_use_map_blk
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
	push	{r4, r5, r6, r7, fp, ip, lr, pc}
	sub	fp, ip, #4
	ldrh	r3, [r0]
	movw	r2, #65535
	mov	r4, r0
	ldr	r5, [r0, #12]
	cmp	r3, r2
	bne	.L705
	ldrh	r3, [r0, #8]
	cmp	r3, #0
	beq	.L706
	movw	r2, #677
	ldr	r1, .L714
	ldr	r0, .L714+4
	bl	sftl_printk
.L706:
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
.L707:
	mov	r0, #0
	ldmfd	sp, {r4, r5, r6, r7, fp, sp, pc}
.L705:
	lsl	r3, r3, #1
	ldr	r2, [r0, #28]
	ldr	r6, [r0, #24]
	mov	r1, #255
	ldrh	r7, [r5, r3]
	ldrh	r3, [r0, #2]
	ldr	r5, .L714+8
	orr	r3, r3, r7, lsl #10
	str	r3, [r5, #3268]
	ldr	r3, [r5, #3284]
	str	r3, [r5, #3272]
	ldr	r3, [r5, #3288]
	str	r3, [r5, #3276]
	str	r2, [r3, #4]
	ldr	r2, .L714+12
	strh	r2, [r3, #8]	@ movhi
	ldrh	r2, [r0, #4]
	strh	r7, [r3, #2]	@ movhi
	strh	r2, [r3]	@ movhi
	ldrh	r2, [r5, #104]
	ldr	r0, [r5, #3284]
	lsl	r2, r2, #3
	bl	ftl_memset
	mov	r2, #0
	mov	r3, r2
.L708:
	ldrh	r0, [r4, #6]
	uxth	r1, r2
	cmp	r0, r1
	bhi	.L710
	mov	r2, #1
	mov	r3, #0
	mov	r1, r2
	ldr	r0, .L714+16
	bl	FlashProgPages
	ldrh	r3, [r4, #2]
	mov	r0, r4
	add	r3, r3, #1
	strh	r3, [r4, #2]	@ movhi
	bl	ftl_map_blk_gc
	b	.L707
.L710:
	uxth	r1, r2
	ldr	r0, [r6, r1, lsl #2]
	cmp	r7, r0, lsr #10
	bne	.L709
	ldr	r0, [r5, #3284]
	add	r3, r3, #1
	uxth	r3, r3
	str	r1, [r0, r3, lsl #3]
	ldr	r0, [r6, r1, lsl #2]
	ldr	r1, [r5, #3284]
	add	r1, r1, r3, lsl #3
	str	r0, [r1, #4]
.L709:
	add	r2, r2, #1
	b	.L708
.L715:
	.align	2
.L714:
	.word	.LANCHOR1+211
	.word	.LC1
	.word	.LANCHOR0
	.word	-1291
	.word	.LANCHOR0+3264
	.size	Ftl_write_map_blk_to_last_page, .-Ftl_write_map_blk_to_last_page
	.align	2
	.global	FtlMapWritePage
	.syntax unified
	.arm
	.fpu softvfp
	.type	FtlMapWritePage, %function
FtlMapWritePage:
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 1, uses_anonymous_args = 0
	mov	ip, sp
	push	{r4, r5, r6, r7, r8, r9, r10, fp, ip, lr, pc}
	sub	fp, ip, #4
	ldr	r7, .L736
	mov	r4, r0
	mov	r8, r1
	mov	r9, r2
	mov	r6, #0
	mov	r5, r7
.L717:
	ldr	r3, [r7, #2524]
	add	r3, r3, #1
	str	r3, [r7, #2524]
	ldrh	r3, [r7, #104]
	ldrh	r2, [r4, #2]
	sub	r3, r3, #1
	cmp	r2, r3
	bge	.L718
	ldrh	r2, [r4]
	movw	r3, #65535
	cmp	r2, r3
	bne	.L719
.L718:
	mov	r0, r4
	bl	Ftl_write_map_blk_to_last_page
.L719:
	ldrh	r3, [r4]
	ldr	r2, [r4, #12]
	lsl	r3, r3, #1
	ldrh	r3, [r2, r3]
	cmp	r3, #0
	bne	.L720
	movw	r2, #735
	ldr	r1, .L736+4
	ldr	r0, .L736+8
	bl	sftl_printk
.L720:
	ldrh	r2, [r4]
	ldrh	r3, [r4, #10]
	cmp	r2, r3
	bcc	.L721
	mov	r2, #736
	ldr	r1, .L736+4
	ldr	r0, .L736+8
	bl	sftl_printk
.L721:
	ldrh	r3, [r4]
	mov	r1, #16
	ldr	r2, [r4, #12]
	ldr	r0, [r5, #3288]
	lsl	r3, r3, #1
	ldrh	r10, [r2, r3]
	ldrh	r3, [r4, #2]
	str	r0, [r5, #3276]
	str	r9, [r5, #3272]
	orr	r3, r3, r10, lsl #10
	str	r3, [r5, #3268]
	bl	__memzero
	ldr	r3, [r5, #3276]
	ldr	r2, [r4, #28]
	ldr	r0, .L736+12
	strh	r8, [r3, #8]	@ movhi
	str	r2, [r3, #4]
	ldrh	r2, [r4, #4]
	strh	r10, [r3, #2]	@ movhi
	strh	r2, [r3]	@ movhi
	mov	r3, #1
	mov	r2, r3
	mov	r1, r3
	bl	FlashProgPages
	ldrh	r0, [r4, #2]
	add	r0, r0, #1
	uxth	r0, r0
	strh	r0, [r4, #2]	@ movhi
	ldr	r3, [r5, #3264]
	cmn	r3, #1
	bne	.L722
	ldr	r1, [r5, #3268]
	add	r6, r6, #1
	ldr	r0, .L736+16
	uxth	r6, r6
	bl	sftl_printk
	ldrh	r3, [r4, #2]
	cmp	r3, #2
	ldrhls	r3, [r5, #104]
	subls	r3, r3, #1
	strhls	r3, [r4, #2]	@ movhi
	cmp	r6, #3
	bls	.L724
	mov	r2, r6
	ldr	r1, [r5, #3268]
	ldr	r0, .L736+20
	bl	sftl_printk
.L725:
	b	.L725
.L724:
	ldr	r3, [r4, #32]
	cmp	r3, #0
	beq	.L717
.L735:
	b	.L735
.L722:
	cmp	r3, #0
	strhne	r10, [r4, #40]	@ movhi
	cmp	r0, #1
	cmpne	r3, #256
	moveq	r0, #1
	movne	r0, #0
	beq	.L717
	ldr	r2, [r5, #3268]
	ldr	r3, [r4, #24]
	str	r2, [r3, r8, lsl #2]
	ldmfd	sp, {r4, r5, r6, r7, r8, r9, r10, fp, sp, pc}
.L737:
	.align	2
.L736:
	.word	.LANCHOR0
	.word	.LANCHOR1+242
	.word	.LC1
	.word	.LANCHOR0+3264
	.word	.LC87
	.word	.LC88
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
	push	{r4, r5, r6, r7, r8, fp, ip, lr, pc}
	sub	fp, ip, #4
	ldr	r4, .L745
	uxth	r6, r0
	uxth	r8, r1
	ldrh	r3, [r4, #136]
	cmp	r3, r6
	bcs	.L739
	movw	r2, #510
	ldr	r1, .L745+4
	ldr	r0, .L745+8
	bl	sftl_printk
.L739:
	ldr	r3, [r4, #3892]
	mov	r5, #12
	ldr	r7, [r3, r6, lsl #2]
	cmp	r7, #0
	bne	.L740
	mul	r5, r5, r8
	ldr	r3, [r4, #2492]
	ldrh	r2, [r4, #110]
	mov	r1, #255
	add	r3, r3, r5
	ldr	r0, [r3, #8]
	bl	ftl_memset
	ldr	r3, [r4, #2492]
	strh	r6, [r3, r5]	@ movhi
	ldr	r3, [r4, #2492]
	add	r5, r3, r5
	str	r7, [r5, #4]
.L741:
	mov	r0, #0
	ldmfd	sp, {r4, r5, r6, r7, r8, fp, sp, pc}
.L740:
	mul	r5, r5, r8
	ldr	r3, [r4, #2492]
	mov	r2, #1
	ldr	r0, .L745+12
	mov	r1, r2
	str	r7, [r4, #3268]
	add	r3, r3, r5
	ldr	r3, [r3, #8]
	str	r3, [r4, #3272]
	ldr	r3, [r4, #3288]
	str	r3, [r4, #3276]
	bl	FlashReadPages
	ldr	r3, [r4, #3264]
	ldr	r8, [r4, #3276]
	cmp	r3, #256
	bne	.L742
	mov	r2, r7
	mov	r1, r6
	ldr	r0, .L745+16
	bl	sftl_printk
	ldr	r0, .L745+20
	lsr	r3, r7, #10
	mov	r1, r6
	strh	r3, [r0, #40]	@ movhi
	ldr	r3, [r4, #2492]
	add	r3, r3, r5
	ldr	r2, [r3, #8]
	bl	FtlMapWritePage
.L742:
	ldrh	r3, [r8, #8]
	cmp	r3, r6
	beq	.L743
	mov	r2, r7
	mov	r1, r6
	ldr	r0, .L745+24
	bl	sftl_printk
	mov	r3, #4
	ldr	r1, [r4, #3276]
	mov	r2, r3
	ldr	r0, .L745+28
	bl	rknand_print_hex
	ldrh	r3, [r4, #136]
	mov	r2, #4
	ldr	r1, [r4, #3892]
	ldr	r0, .L745+32
	bl	rknand_print_hex
.L743:
	ldrh	r3, [r8, #8]
	cmp	r3, r6
	beq	.L744
	movw	r2, #535
	ldr	r1, .L745+4
	ldr	r0, .L745+8
	bl	sftl_printk
.L744:
	ldr	r3, [r4, #2492]
	mov	r1, #0
	add	r2, r3, r5
	str	r1, [r2, #4]
	strh	r6, [r3, r5]	@ movhi
	b	.L741
.L746:
	.align	2
.L745:
	.word	.LANCHOR0
	.word	.LANCHOR1+258
	.word	.LC1
	.word	.LANCHOR0+3264
	.word	.LC89
	.word	.LANCHOR0+3908
	.word	.LC90
	.word	.LC91
	.word	.LC92
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
	ldr	r6, [r0, #12]
	ldr	r9, [r0, #24]
	bl	ftl_free_no_use_map_blk
	ldrh	ip, [r4, #10]
	ldrh	r2, [r4, #8]
	ldrh	r1, [r4, #40]
	sub	r3, ip, #4
	ldr	r5, .L770
	cmp	r2, r3
	bge	.L748
	movw	r3, #65535
	cmp	r1, r3
	beq	.L750
	ldrh	r2, [r4, #2]
	ldrh	r3, [r5, #104]
	cmp	r2, r3
	bcc	.L751
.L748:
	movw	r3, #65535
	uxth	r0, r0
	cmp	r1, r3
	beq	.L752
	ldrh	r2, [r5, #104]
	ldrh	r3, [r4, #2]
	cmp	r2, r3
	subls	lr, r6, #2
	movls	r2, #0
	bls	.L753
.L752:
	lsl	r0, r0, #1
	ldrh	r8, [r6, r0]
	cmp	r8, #0
	beq	.L750
	ldr	r3, [r4, #32]
	cmp	r3, #0
	bne	.L750
	mov	r2, #1
	str	r2, [r4, #32]
	strh	r3, [r6, r0]	@ movhi
	ldrh	r3, [r4, #8]
	ldrh	r2, [r4, #2]
	sub	r3, r3, #1
	strh	r3, [r4, #8]	@ movhi
	ldrh	r3, [r5, #104]
	cmp	r2, r3
	bcc	.L757
	mov	r0, r4
	bl	ftl_map_blk_alloc_new_blk
.L757:
	mov	r6, #0
.L758:
	ldrh	r3, [r4, #6]
	uxth	r10, r6
	cmp	r3, r10
	bhi	.L765
	mov	r1, #1
	mov	r0, r8
	bl	FtlFreeSysBlkQueueIn
	mov	r3, #0
	str	r3, [r4, #32]
.L750:
	ldrh	r2, [r4, #2]
	ldrh	r3, [r5, #104]
	cmp	r2, r3
	bcc	.L751
	mov	r0, r4
	bl	ftl_map_blk_alloc_new_blk
.L751:
	mov	r0, #0
	sub	sp, fp, #40
	ldmfd	sp, {r4, r5, r6, r7, r8, r9, r10, fp, sp, pc}
.L755:
	ldrh	r10, [lr, #2]!
	add	r8, r2, #1
	cmp	r10, r1
	beq	.L754
	mov	r2, r8
.L753:
	uxth	r7, r2
	cmp	r7, ip
	bcc	.L755
	mov	r7, r0
.L754:
	uxth	r2, r2
	ldr	r0, .L770+4
	lsl	r2, r2, #1
	ldrh	r2, [r6, r2]
	bl	sftl_printk
	mvn	r3, #0
	mov	r0, r7
	strh	r3, [r4, #40]	@ movhi
	b	.L752
.L765:
	uxth	r7, r6
	add	r3, r9, r7, lsl #2
	str	r3, [fp, #-44]
	ldr	r3, [r9, r7, lsl #2]
	cmp	r8, r3, lsr #10
	bne	.L759
	ldr	r3, [r5, #3848]
	ldr	r0, .L770+8
	str	r3, [r5, #3272]
	ldr	r3, [r5, #3288]
	str	r3, [r5, #3276]
	ldr	r2, [r9, r7, lsl #2]
	str	r3, [fp, #-48]
	str	r2, [r5, #3268]
	mov	r2, #1
	mov	r1, r2
	bl	FlashReadPages
	ldr	r3, [fp, #-48]
	ldrh	r2, [r3, #8]
	cmp	r2, r10
	beq	.L760
	movw	r2, #647
	ldr	r1, .L770+12
	ldr	r0, .L770+16
	bl	sftl_printk
	ldr	r3, [fp, #-48]
.L760:
	ldr	r2, [r5, #3264]
	cmn	r2, #1
	bne	.L761
.L763:
	ldr	r2, [fp, #-44]
	mov	r3, #0
	str	r3, [r2]
.L762:
	b	.L762
.L761:
	ldrh	r2, [r3, #8]
	cmp	r2, r10
	bne	.L763
	ldrh	r2, [r3]
	ldrh	r3, [r4, #4]
	cmp	r2, r3
	bne	.L763
	ldr	r2, [r5, #3272]
	mov	r1, r7
	mov	r0, r4
	bl	FtlMapWritePage
.L759:
	add	r6, r6, #1
	b	.L758
.L771:
	.align	2
.L770:
	.word	.LANCHOR0
	.word	.LC93
	.word	.LANCHOR0+3264
	.word	.LANCHOR1+274
	.word	.LC1
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
	ldr	r5, .L773
	uxth	r0, r0
	mov	r4, #12
	mul	r4, r4, r0
	ldr	r3, [r5, #2492]
	add	r0, r5, #3904
	add	r0, r0, #4
	add	r2, r3, r4
	ldrh	r1, [r3, r4]
	ldr	r2, [r2, #8]
	bl	FtlMapWritePage
	ldr	r0, [r5, #2492]
	add	r4, r0, r4
	mov	r0, #0
	ldr	r3, [r4, #4]
	bic	r3, r3, #-2147483648
	str	r3, [r4, #4]
	ldmfd	sp, {r4, r5, fp, sp, pc}
.L774:
	.align	2
.L773:
	.word	.LANCHOR0
	.size	flush_l2p_region, .-flush_l2p_region
	.align	2
	.global	log2phys
	.syntax unified
	.arm
	.fpu softvfp
	.type	log2phys, %function
log2phys:
	@ args = 0, pretend = 0, frame = 4
	@ frame_needed = 1, uses_anonymous_args = 0
	mov	ip, sp
	push	{r4, r5, r6, r7, r8, r9, r10, fp, ip, lr, pc}
	sub	fp, ip, #4
	sub	sp, sp, #4
	mvn	r6, #0
	ldr	r4, .L789
	mov	r7, r1
	mov	r10, r2
	ldrh	r3, [r4, #108]
	add	r3, r3, #7
	lsr	r9, r0, r3
	bic	r6, r0, r6, lsl r3
	ldr	r3, [r4, #2504]
	uxth	r8, r9
	uxth	r6, r6
	cmp	r0, r3
	bcc	.L776
	movw	r2, #847
	ldr	r1, .L789+4
	ldr	r0, .L789+8
	bl	sftl_printk
.L776:
	ldrh	r1, [r4, #138]
	mov	r2, #0
	ldr	r0, [r4, #2492]
	mov	r3, #12
.L777:
	uxth	r5, r2
	cmp	r5, r1
	bcc	.L782
	str	r3, [fp, #-44]
	bl	select_l2p_ram_region
	ldr	r3, [fp, #-44]
	mov	r5, r0
	ldr	r2, [r4, #2492]
	mul	r3, r3, r0
	add	r1, r2, r3
	ldrh	r2, [r2, r3]
	movw	r3, #65535
	cmp	r2, r3
	beq	.L783
	ldr	r3, [r1, #4]
	cmp	r3, #0
	bge	.L783
	bl	flush_l2p_region
.L783:
	mov	r1, r5
	uxth	r0, r9
	bl	load_l2p_region
	b	.L778
.L782:
	add	r2, r2, #1
	mla	ip, r3, r2, r0
	ldrh	ip, [ip, #-12]
	cmp	ip, r8
	bne	.L777
.L778:
	cmp	r10, #0
	mov	r3, #12
	bne	.L779
	ldr	r2, [r4, #2492]
	mla	r3, r3, r5, r2
	ldr	r3, [r3, #8]
	ldr	r3, [r3, r6, lsl #2]
	str	r3, [r7]
.L780:
	ldr	r2, [r4, #2492]
	mov	r3, #12
	mov	r0, #0
	mla	r5, r3, r5, r2
	ldr	r3, [r5, #4]
	cmn	r3, #1
	addne	r3, r3, #1
	strne	r3, [r5, #4]
	ldmib	sp, {r4, r5, r6, r7, r8, r9, r10, fp, sp, pc}
.L779:
	mul	r3, r3, r5
	ldr	r2, [r4, #2492]
	ldr	r1, [r7]
	add	r2, r2, r3
	ldr	r2, [r2, #8]
	str	r1, [r2, r6, lsl #2]
	ldr	r2, [r4, #2492]
	add	r3, r2, r3
	ldr	r2, [r3, #4]
	orr	r2, r2, #-2147483648
	str	r2, [r3, #4]
	ldr	r3, .L789+12
	strh	r8, [r3]	@ movhi
	b	.L780
.L790:
	.align	2
.L789:
	.word	.LANCHOR0
	.word	.LANCHOR1+289
	.word	.LC1
	.word	.LANCHOR0+2496
	.size	log2phys, .-log2phys
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
	ldr	r7, .L801
	ubfx	r0, r1, #10, #16
	str	r1, [fp, #-32]
	bl	P2V_block_in_plane
	ldr	r2, [r7, #2324]
	lsl	r5, r0, #1
	ldrh	r3, [r2, r5]
	cmp	r3, #0
	bne	.L792
	ldr	r4, [r7, #2340]
	cmp	r4, #0
	beq	.L793
	movw	r2, #2344
	movw	lr, #65535
	ldrh	ip, [r7, r2]
	ldr	r2, [r7, #2316]
	sub	r4, r4, r2
	ubfx	r4, r4, #3, #16
.L794:
	uxth	r1, r3
	cmp	ip, r1
	bls	.L793
	cmp	r4, r0
	bne	.L795
	mov	r1, r4
	ldr	r0, .L801+4
	bl	List_remove_node
	movw	r3, #2344
	ldrh	r3, [r7, r3]
	cmp	r3, #0
	bne	.L796
	movw	r2, #1737
	ldr	r1, .L801+8
	ldr	r0, .L801+12
	bl	sftl_printk
.L796:
	movw	r2, #2344
	mov	r0, r4
	ldrh	r3, [r7, r2]
	sub	r3, r3, #1
	strh	r3, [r7, r2]	@ movhi
	bl	INSERT_DATA_LIST
	ldr	r2, [r7, #2324]
	ldrh	r3, [r2, r5]
.L792:
	add	r3, r3, #1
	strh	r3, [r2, r5]	@ movhi
	b	.L793
.L795:
	lsl	r4, r4, #3
	add	r3, r3, #1
	ldrh	r4, [r2, r4]
	cmp	r4, lr
	bne	.L794
.L793:
	mov	r2, #1
	sub	r1, fp, #32
	mov	r0, r6
	bl	log2phys
	ldmib	sp, {r4, r5, r6, r7, fp, sp, pc}
.L802:
	.align	2
.L801:
	.word	.LANCHOR0
	.word	.LANCHOR0+2340
	.word	.LANCHOR1+298
	.word	.LC1
	.size	FtlReUsePrevPpa, .-FtlReUsePrevPpa
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
	ldr	r6, .L811
	mov	r1, #0
	ldr	r0, .L811+4
	bl	sftl_printk
	mov	r4, r6
.L804:
	ldr	r3, [r6, #2504]
	cmp	r5, r3
	bcc	.L810
	sub	sp, fp, #28
	ldmfd	sp, {r4, r5, r6, r7, fp, sp, pc}
.L810:
	mov	r2, #0
	sub	r1, fp, #32
	mov	r0, r5
	bl	log2phys
	ubfx	r3, r5, #0, #11
	cmp	r3, #0
	bne	.L805
	ldr	r2, [fp, #-32]
	mov	r1, r5
	ldr	r0, .L811+8
	bl	sftl_printk
.L805:
	ldr	r3, [fp, #-32]
	cmn	r3, #1
	beq	.L807
	str	r3, [r4, #3268]
	mov	r2, #0
	ldr	r3, [r4, #3284]
	mov	r1, #1
	ldr	r7, [r4, #3288]
	ldr	r0, .L811+12
	str	r3, [r4, #3272]
	str	r5, [r4, #3280]
	str	r7, [r4, #3276]
	str	r2, [r4, #3264]
	bl	FlashReadPages
	ldr	r3, [r4, #3264]
	cmn	r3, #1
	cmpne	r3, #256
	beq	.L808
	ldr	r3, [r7, #8]
	cmp	r5, r3
	beq	.L807
.L808:
	ldr	r2, [r4, #3272]
	ldr	r3, [r4, #3276]
	ldr	r0, .L811+16
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
	ldr	r2, [r4, #3268]
	bl	sftl_printk
.L807:
	add	r5, r5, #1
	b	.L804
.L812:
	.align	2
.L811:
	.word	.LANCHOR0
	.word	.LC94
	.word	.LC95
	.word	.LANCHOR0+3264
	.word	.LC96
	.size	ftl_scan_all_data, .-ftl_scan_all_data
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
	push	{r4, r5, fp, ip, lr, pc}
	sub	fp, ip, #4
	sub	sp, sp, #88
	ldr	r3, .L826
	ldr	r0, [r3, #2716]
	mov	r4, r3
	cmp	r0, #0
	beq	.L814
	ldr	r1, [r3, #2720]
	ldr	r2, [r3, #2504]
	cmp	r1, r2
	bcs	.L815
	mov	r5, #2048
.L820:
	ldr	r0, [r4, #2720]
	ldr	r3, [r4, #2504]
	cmp	r0, r3
	bcs	.L817
	mov	r2, #0
	sub	r1, fp, #108
	bl	log2phys
	ldr	r2, [fp, #-108]
	ldr	r3, [r4, #2720]
	cmn	r2, #1
	add	r3, r3, #1
	str	r3, [r4, #2720]
	beq	.L818
	str	r3, [fp, #-88]
	sub	r0, fp, #20
	ldr	r3, .L826+4
	mov	r1, #1
	str	r2, [fp, #-100]
	mov	r2, #0
	str	r2, [r0, #-84]!
	str	r3, [fp, #-96]
	sub	r3, fp, #84
	str	r3, [fp, #-92]
	bl	FlashReadPages
	ldr	r3, [fp, #-104]
	cmp	r3, #256
	bne	.L817
	ldr	r0, [fp, #-108]
	ubfx	r0, r0, #10, #16
	bl	P2V_block_in_plane
	bl	FtlGcRefreshBlock
.L817:
	mvn	r0, #0
.L813:
	sub	sp, fp, #20
	ldmfd	sp, {r4, r5, fp, sp, pc}
.L818:
	subs	r5, r5, #1
	bne	.L820
	b	.L817
.L815:
	ldr	r2, [r3, #2508]
	mov	r0, #0
	str	r0, [r3, #2716]
	str	r0, [r3, #2720]
	str	r2, [r3, #2712]
	b	.L813
.L814:
	ldr	r1, [r3, #2508]
	ldr	r2, [r3, #2712]
	add	ip, r1, #1048576
	cmp	r2, ip
	bhi	.L823
	ldr	r3, [r3, #2564]
	mov	ip, #33554432
	lsr	r3, r3, #10
	add	r2, r2, ip, asr r3
	cmp	r1, r2
	bhi	.L823
	ldr	r3, .L826+8
	ldrb	r3, [r3, #28]	@ zero_extendqisi2
	cmp	r3, #0
	bne	.L813
.L823:
	mov	r3, #1
	mov	r0, #0
	str	r3, [r4, #2716]
	str	r0, [r4, #2720]
	str	r1, [r4, #2712]
	b	.L813
.L827:
	.align	2
.L826:
	.word	.LANCHOR0
	.word	ftl_temp_buf
	.word	.LANCHOR0+2268
	.size	FtlReadRefresh, .-FtlReadRefresh
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
	ldr	r6, .L852
	ldr	r1, .L852+4
	ldr	r0, .L852+8
	mov	r5, r6
	bl	sftl_printk
	mov	r1, #8192
	ldr	r0, .L852+12
	bl	__memzero
.L829:
	ldr	r3, [r6, #2504]
	cmp	r4, r3
	bcc	.L831
	ldr	r8, .L852+12
	mov	r4, #0
	ldr	r9, .L852+16
	mov	r6, r4
.L832:
	ldrh	r2, [r5, #40]
	uxth	r3, r4
	cmp	r2, r3
	bhi	.L834
	ldr	r4, [r5, #2340]
	cmp	r4, #0
	beq	.L835
	movw	r3, #2344
	ldr	r9, .L852+12
	ldrh	r8, [r5, r3]
	mov	r7, #0
	ldr	r3, [r5, #2316]
	ldr	r10, .L852+20
	sub	r4, r4, r3
	ubfx	r4, r4, #3, #16
.L836:
	uxth	r3, r7
	cmp	r8, r3
	bls	.L835
	ldr	r2, [r5, #2324]
	lsl	r3, r4, #1
	ldrh	r2, [r2, r3]
	cmp	r2, #0
	beq	.L837
	mov	r6, #1
	ldrh	r3, [r9, r3]
	mov	r1, r4
	mov	r0, r10
	bl	sftl_printk
.L837:
	ldr	r3, [r5, #2316]
	lsl	r4, r4, #3
	add	r7, r7, #1
	ldrh	r4, [r3, r4]
	movw	r3, #65535
	cmp	r4, r3
	bne	.L836
.L835:
	cmp	r6, #0
	ldmibeq	sp, {r4, r5, r6, r7, r8, r9, r10, fp, sp, pc}
	movw	r2, #2373
	ldr	r1, .L852+4
	ldr	r0, .L852+24
	bl	sftl_printk
	ldmib	sp, {r4, r5, r6, r7, r8, r9, r10, fp, sp, pc}
.L831:
	mov	r2, #0
	sub	r1, fp, #44
	mov	r0, r4
	bl	log2phys
	ldr	r0, [fp, #-44]
	cmn	r0, #1
	beq	.L830
	ubfx	r0, r0, #10, #16
	bl	P2V_block_in_plane
	ldr	r2, .L852+12
	lsl	r0, r0, #1
	ldrh	r3, [r2, r0]
	add	r3, r3, #1
	strh	r3, [r2, r0]	@ movhi
.L830:
	add	r4, r4, #1
	b	.L829
.L834:
	uxth	r1, r4
	ldr	r3, [r5, #2324]
	lsl	r7, r1, #1
	ldrh	r2, [r3, r7]
	ldrh	r3, [r8, r7]
	cmp	r2, r3
	beq	.L833
	mov	r0, r9
	bl	sftl_printk
	ldr	r3, [r5, #2324]
	movw	r2, #65535
	ldrh	r3, [r3, r7]
	cmp	r3, r2
	beq	.L833
	ldrh	r2, [r8, r7]
	cmp	r2, r3
	movhi	r6, #1
.L833:
	add	r4, r4, #1
	b	.L832
.L853:
	.align	2
.L852:
	.word	.LANCHOR0
	.word	.LANCHOR1+314
	.word	.LC97
	.word	check_vpc_table
	.word	.LC98
	.word	.LC99
	.word	.LC1
	.size	ftl_check_vpc, .-ftl_check_vpc
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
	ldr	r3, .L861
	ldr	r2, [r0, #24]
	sub	r5, r5, #1
	uxth	r5, r5
	ldr	r0, [r3, #3848]
	mov	r4, r3
	ldr	r1, [r3, #3288]
	str	r0, [r3, #3272]
	str	r1, [r3, #3276]
	ldr	r2, [r2, r5, lsl #2]
	cmp	r2, #0
	str	r2, [r3, #3268]
	beq	.L856
	mov	r2, #1
	add	r0, r3, #3264
	mov	r1, r2
	bl	FlashReadPages
.L857:
	ldr	r2, [r4, #3272]
	mov	r1, r5
	mov	r0, r6
	bl	FtlMapWritePage
	ldmfd	sp, {r4, r5, r6, fp, sp, pc}
.L856:
	ldrh	r2, [r3, #110]
	mov	r1, #255
	bl	ftl_memset
	b	.L857
.L862:
	.align	2
.L861:
	.word	.LANCHOR0
	.size	FtlMapBlkWriteDump_data, .-FtlMapBlkWriteDump_data
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
	ldr	r4, .L944
	mov	r1, r5
	ldr	r2, [r4, #128]
	add	r3, r4, #3952
	ldr	r0, [r4, #3896]
	mov	r6, r4
	strh	r5, [r3]	@ movhi
	strh	r5, [r4, #144]	@ movhi
	lsl	r2, r2, #2
	bl	ftl_memset
	ldr	r2, [r4, #128]
	mov	r1, r5
	ldr	r0, [r4, #3872]
	lsl	r2, r2, #1
	bl	ftl_memset
	ldrh	r2, [r4, #120]
	mov	r1, r5
	ldr	r0, [r4, #3884]
	lsl	r2, r2, #2
	bl	ftl_memset
	ldrh	r2, [r4, #120]
	mov	r1, r5
	ldr	r0, [r4, #148]
	mov	r5, r4
	lsl	r2, r2, #1
	bl	ftl_memset
	mvn	r3, #0
	str	r3, [r4, #2576]
	str	r3, [r4, #2580]
	str	r3, [r4, #2584]
	ldrh	r3, [r4, #40]
	str	r3, [fp, #-44]
.L864:
	ldrh	r3, [r6, #42]
	ldr	r2, [fp, #-44]
	cmp	r3, r2
	bls	.L905
	ldr	r1, [r6, #3176]
	mov	r7, #0
	ldrh	r2, [r6, #32]
	mov	r10, r7
	ldr	r8, [r6, #3836]
	ldr	r3, [r6, #3172]
	str	r1, [fp, #-48]
	ldrh	r9, [r6, #112]
	ldr	ip, .L944+4
	b	.L906
.L866:
	ldrb	r0, [ip, r7]	@ zero_extendqisi2
	ldr	r1, [fp, #-44]
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
	bne	.L865
	mov	r1, #20
	mla	r0, r1, r10, r8
	ldr	r1, [fp, #-52]
	lsl	r1, r1, #10
	stmib	r0, {r1, r3}
	mul	r1, r9, r10
	add	lr, r1, #3
	cmp	r1, #0
	movlt	r1, lr
	ldr	lr, [fp, #-48]
	bic	r1, r1, #3
	add	r1, lr, r1
	str	r1, [r0, #12]
	add	r1, r10, #1
	uxth	r10, r1
.L865:
	add	r7, r7, #1
.L906:
	uxth	r1, r7
	cmp	r2, r1
	bhi	.L866
	cmp	r10, #0
	bne	.L867
.L904:
	ldr	r3, [fp, #-44]
	add	r3, r3, #1
	uxth	r3, r3
	str	r3, [fp, #-44]
	b	.L864
.L867:
	mov	r2, #1
	mov	r1, r10
	mov	r0, r8
	bl	FlashReadPages
	mov	r3, #0
.L943:
	str	r3, [fp, #-48]
	ldrh	r3, [fp, #-48]
	cmp	r10, r3
	bls	.L904
	ldr	r3, [fp, #-48]
	mov	r9, #20
	mul	r9, r9, r3
	ldr	r3, [r4, #3836]
	add	r2, r3, r9
	ldr	r3, [r3, r9]
	ldr	r1, [r2, #4]
	ldr	r7, [r2, #12]
	cmn	r3, #1
	lsr	ip, r1, #10
	uxth	r8, ip
	bne	.L871
	mov	r3, #16
.L873:
	ldr	r0, [r4, #3836]
	str	ip, [fp, #-56]
	str	r3, [fp, #-52]
	add	r0, r0, r9
	ldr	r2, [r0, #4]
	add	r2, r2, #1
	str	r2, [r0, #4]
	mov	r2, #1
	mov	r1, r2
	bl	FlashReadPages
	ldrh	r2, [r7]
	movw	r3, #65535
	ldr	ip, [fp, #-56]
	cmp	r2, r3
	ldr	r3, [fp, #-52]
	bne	.L870
	ldr	r3, [r4, #3836]
	mvn	r2, #0
	str	r2, [r3, r9]
	ldr	r3, [r4, #3836]
	ldr	r3, [r3, r9]
	cmp	r3, r2
	bne	.L871
.L872:
	mov	r1, #0
	uxth	r0, ip
	bl	FtlFreeSysBlkQueueIn
	b	.L876
.L870:
	ldr	r2, [r4, #3836]
	ldr	r2, [r2, r9]
	cmn	r2, #1
	bne	.L871
	sub	r3, r3, #1
	uxth	r3, r3
	cmp	r3, #0
	bne	.L873
	b	.L872
.L871:
	ldr	r2, [r6, #2540]
	ldr	r3, [r7, #4]
	cmn	r2, #1
	beq	.L874
	cmp	r2, r3
	bhi	.L875
.L874:
	cmn	r3, #1
	addne	r2, r3, #1
	strne	r2, [r5, #2540]
.L875:
	ldrh	r2, [r7]
	movw	r1, #61604
	cmp	r2, r1
	beq	.L877
	bhi	.L878
	movw	r3, #61574
	cmp	r2, r3
	beq	.L879
.L876:
	ldr	r3, [fp, #-48]
	add	r3, r3, #1
	b	.L943
.L878:
	movw	r3, #61634
	cmp	r2, r3
	beq	.L880
	movw	r3, #65535
	cmp	r2, r3
	beq	.L872
	b	.L876
.L880:
	ldr	r9, .L944+8
	ldr	r3, [r4, #128]
	ldrh	r2, [r9]
	cmp	r2, r3
	bls	.L882
	movw	r2, #1256
	ldr	r1, .L944+12
	ldr	r0, .L944+16
	bl	sftl_printk
.L882:
	ldr	r0, [r4, #128]
	ldrh	r2, [r9]
	ldr	ip, [r4, #3896]
	uxth	r1, r0
	sub	r3, r1, #1
	sub	r1, r1, r2
	sub	r1, r1, #1
	sxth	r3, r3
	sxth	r1, r1
	str	r1, [fp, #-52]
.L883:
	ldr	r1, [fp, #-52]
	cmp	r3, r1
	bgt	.L889
	cmp	r3, #0
	bge	.L921
	b	.L876
.L889:
	lsl	lr, r3, #2
	ldr	r1, [r7, #4]
	str	lr, [fp, #-56]
	ldr	lr, [ip, r3, lsl #2]
	cmp	r1, lr
	bls	.L884
	ldr	r1, [ip]
	cmp	r1, #0
	bne	.L885
	cmp	r0, r2
	addne	r2, r2, #1
	strhne	r2, [r9]	@ movhi
.L885:
	uxth	ip, r3
	mov	r1, #0
.L886:
	uxth	r0, r1
	sxth	r2, r1
	cmp	ip, r0
	bhi	.L887
	ldr	r1, [r7, #4]
	cmp	r3, #0
	ldr	r2, [r5, #3896]
	ldr	r0, [fp, #-56]
	str	r1, [r2, r0]
	lsl	r2, r3, #1
	ldr	r1, [r5, #3872]
	strh	r8, [r1, r2]	@ movhi
	blt	.L876
	ldr	r2, .L944+8
	ldr	r1, [r5, #128]
	ldrh	r2, [r2]
	sub	r1, r1, r2
	sub	r1, r1, #1
	sxth	r1, r1
	cmp	r3, r1
	bgt	.L876
.L921:
	add	r2, r2, #1
	ldr	r1, [r7, #4]
	strh	r2, [r9]	@ movhi
	ldr	r2, [r4, #3896]
	str	r1, [r2, r3, lsl #2]
	lsl	r3, r3, #1
	ldr	r2, [r4, #3872]
.L941:
	strh	r8, [r2, r3]	@ movhi
	b	.L876
.L887:
	ldr	r0, [r6, #3896]
	add	r1, r1, #1
	add	lr, r0, r2, lsl #2
	ldr	lr, [lr, #4]
	str	lr, [r0, r2, lsl #2]
	lsl	r2, r2, #1
	ldr	r0, [r6, #3872]
	add	lr, r0, r2
	ldrh	lr, [lr, #2]
	strh	lr, [r0, r2]	@ movhi
	b	.L886
.L884:
	sub	r3, r3, #1
	sxth	r3, r3
	b	.L883
.L879:
	ldrh	r2, [r4, #144]
	ldrh	r3, [r4, #120]
	cmp	r2, r3
	bls	.L892
	movw	r2, #1297
	ldr	r1, .L944+12
	ldr	r0, .L944+16
	bl	sftl_printk
.L892:
	ldrh	r2, [r4, #120]
	ldrh	r1, [r4, #144]
	ldr	ip, [r4, #3884]
	sub	r0, r2, #1
	sxth	r3, r0
	sub	r0, r0, r1
	str	r0, [fp, #-52]
.L893:
	ldr	r0, [fp, #-52]
	cmp	r3, r0
	ble	.L898
	ldr	r9, [ip, r3, lsl #2]
	lsl	lr, r3, #2
	ldr	r0, [r7, #4]
	cmp	r0, r9
	bls	.L894
	sub	r2, r2, r1
	ldr	r0, [ip]
	clz	r2, r2
	uxth	ip, r3
	lsr	r2, r2, #5
	cmp	r0, #0
	orrne	r2, r2, #1
	cmp	r2, #0
	addeq	r1, r1, #1
	strheq	r1, [r5, #144]	@ movhi
	mov	r1, #0
.L896:
	uxth	r0, r1
	sxth	r2, r1
	cmp	ip, r0
	bhi	.L897
	ldr	r1, [r7, #4]
	ldr	r2, [r5, #3884]
	str	r1, [r2, lr]
	lsl	r2, r3, #1
	ldr	r1, [r5, #148]
	strh	r8, [r1, r2]	@ movhi
.L898:
	cmp	r3, #0
	blt	.L876
	ldrh	r2, [r5, #120]
	ldrh	r1, [r5, #144]
	sub	r2, r2, #1
	sub	r2, r2, r1
	sxth	r2, r2
	cmp	r3, r2
	bgt	.L876
	add	r1, r1, #1
	ldr	r2, [r5, #3884]
	strh	r1, [r5, #144]	@ movhi
	ldr	r1, [r7, #4]
	str	r1, [r2, r3, lsl #2]
	lsl	r3, r3, #1
	ldr	r2, [r5, #148]
	b	.L941
.L897:
	ldr	r0, [r6, #3884]
	add	r1, r1, #1
	add	r9, r0, r2, lsl #2
	ldr	r9, [r9, #4]
	str	r9, [r0, r2, lsl #2]
	lsl	r2, r2, #1
	ldr	r0, [r6, #148]
	add	r9, r0, r2
	ldrh	r9, [r9, #2]
	strh	r9, [r0, r2]	@ movhi
	b	.L896
.L894:
	sub	r3, r3, #1
	sxth	r3, r3
	b	.L893
.L877:
	ldr	r9, .L944+20
	movw	r2, #65535
	ldrh	r1, [r9]
	cmp	r1, r2
	strheq	r8, [r9]	@ movhi
	beq	.L942
	ldrh	r0, [r9, #4]
	cmp	r0, r2
	beq	.L901
	mov	r1, #1
	bl	FtlFreeSysBlkQueueIn
.L901:
	ldr	r3, [r7, #4]
	ldr	r2, [r4, #2584]
	cmp	r2, r3
	strhcs	r8, [r9, #4]	@ movhi
	bcs	.L876
	ldrh	r3, [r9]
	strh	r8, [r9]	@ movhi
	strh	r3, [r9, #4]	@ movhi
	ldr	r3, [r7, #4]
.L942:
	str	r3, [r4, #2584]
	b	.L876
.L905:
	ldr	r2, [r6, #3872]
	ldrh	r3, [r2]
	cmp	r3, #0
	beq	.L907
.L910:
	ldr	r1, [r4, #148]
	ldrh	r2, [r1]
	cmp	r2, #0
	beq	.L908
.L909:
	ldr	r3, .L944+8
	ldrh	r2, [r3]
	ldr	r3, [r4, #128]
	cmp	r2, r3
	bls	.L940
	movw	r2, #1422
	ldr	r1, .L944+12
	ldr	r0, .L944+16
	bl	sftl_printk
.L940:
	mov	r0, #0
	sub	sp, fp, #40
	ldmfd	sp, {r4, r5, r6, r7, r8, r9, r10, fp, sp, pc}
.L907:
	ldr	r1, .L944+8
	ldrh	r1, [r1]
	cmp	r1, #0
	ldrne	r1, [r6, #128]
	beq	.L910
.L911:
	sxth	ip, r3
	cmp	ip, r1
	bcs	.L910
	lsl	r0, ip, #1
	add	r3, r3, #1
	ldrh	r0, [r2, r0]
	cmp	r0, #0
	beq	.L911
	mov	r3, ip
	mov	lr, #0
.L912:
	ldr	r2, [r5, #128]
	cmp	r3, r2
	bcs	.L910
	ldr	r1, [r5, #3872]
	lsl	r2, r3, #1
	sub	r0, r3, ip
	lsl	r6, r0, #1
	ldrh	r7, [r1, r2]
	strh	r7, [r1, r6]	@ movhi
	ldr	r1, [r5, #3896]
	ldr	r6, [r1, r3, lsl #2]
	add	r3, r3, #1
	sxth	r3, r3
	str	r6, [r1, r0, lsl #2]
	ldr	r1, [r5, #3872]
	strh	lr, [r1, r2]	@ movhi
	b	.L912
.L908:
	ldrh	r3, [r4, #144]
	cmp	r3, #0
	ldrhne	r0, [r4, #120]
	beq	.L909
.L917:
	sxth	r3, r2
	cmp	r3, r0
	mov	ip, r3
	bge	.L909
	lsl	lr, r3, #1
	add	r2, r2, #1
	ldrh	lr, [r1, lr]
	cmp	lr, #0
	beq	.L917
	mov	lr, #0
.L918:
	ldrh	r2, [r5, #120]
	cmp	r3, r2
	bge	.L909
	ldr	r1, [r5, #148]
	lsl	r2, r3, #1
	sub	r0, r3, ip
	lsl	r6, r0, #1
	ldrh	r7, [r1, r2]
	strh	r7, [r1, r6]	@ movhi
	ldr	r1, [r5, #3884]
	ldr	r6, [r1, r3, lsl #2]
	add	r3, r3, #1
	sxth	r3, r3
	str	r6, [r1, r0, lsl #2]
	ldr	r1, [r5, #148]
	strh	lr, [r1, r2]	@ movhi
	b	.L918
.L945:
	.align	2
.L944:
	.word	.LANCHOR0
	.word	.LANCHOR0+60
	.word	.LANCHOR0+3952
	.word	.LANCHOR1+328
	.word	.LC1
	.word	.LANCHOR0+2576
	.size	FtlScanSysBlk, .-FtlScanSysBlk
	.align	2
	.global	FtlMapTblRecovery
	.syntax unified
	.arm
	.fpu softvfp
	.type	FtlMapTblRecovery, %function
FtlMapTblRecovery:
	@ args = 0, pretend = 0, frame = 20
	@ frame_needed = 1, uses_anonymous_args = 0
	mov	ip, sp
	push	{r4, r5, r6, r7, r8, r9, r10, fp, ip, lr, pc}
	sub	fp, ip, #4
	sub	sp, sp, #20
	mov	r1, #0
	ldr	r3, [r0, #24]
	mov	r4, r0
	ldr	r6, .L971
	mov	r7, #0
	ldr	r8, [r0, #12]
	str	r3, [fp, #-44]
	ldr	r3, [r0, #16]
	mov	r5, r6
	str	r3, [fp, #-56]
	ldrh	r3, [r0, #6]
	str	r3, [fp, #-48]
	ldrh	r3, [r0, #8]
	ldr	r0, [fp, #-44]
	str	r3, [fp, #-52]
	ldr	r3, [fp, #-48]
	lsl	r2, r3, #2
	bl	ftl_memset
	ldr	r3, [r6, #3284]
	mvn	r2, #0
	ldr	r9, [r6, #3288]
	str	r7, [r4, #32]
	str	r3, [r6, #3272]
	str	r9, [r6, #3276]
	strh	r2, [r4]	@ movhi
	strh	r2, [r4, #2]	@ movhi
	mov	r2, #1
	str	r7, [r4, #28]
	str	r2, [r4, #36]
.L947:
	ldr	r3, [fp, #-52]
	sxth	r2, r7
	cmp	r2, r3
	bge	.L965
	ldr	r3, [fp, #-52]
	lsl	r0, r2, #1
	sub	r1, r3, #1
	cmp	r2, r1
	bne	.L948
	add	r10, r8, r0
	mov	r1, #1
	ldrh	r0, [r8, r0]
	mov	r8, #0
	str	r2, [fp, #-52]
	bl	FtlGetLastWrittenPage
	ldr	r3, [fp, #-56]
	sxth	r1, r0
	ldr	r2, [fp, #-52]
	add	r0, r0, #1
	ldr	ip, .L971+4
	strh	r7, [r4]	@ movhi
	add	r7, r1, #1
	strh	r0, [r4, #2]	@ movhi
	ldr	r2, [r3, r2, lsl #2]
	str	r2, [r4, #28]
.L949:
	sxth	r2, r8
	cmp	r2, r7
	blt	.L952
.L965:
	mov	r0, r4
	bl	ftl_free_no_use_map_blk
	ldrh	r2, [r4, #2]
	ldrh	r3, [r5, #104]
	cmp	r2, r3
	bne	.L954
	mov	r0, r4
	bl	ftl_map_blk_alloc_new_blk
.L954:
	mov	r0, r4
	bl	ftl_map_blk_gc
	mov	r0, r4
	bl	ftl_map_blk_gc
	mov	r0, #0
	sub	sp, fp, #40
	ldmfd	sp, {r4, r5, r6, r7, r8, r9, r10, fp, sp, pc}
.L952:
	ldrh	r1, [r10]
	mov	r0, ip
	str	ip, [fp, #-52]
	orr	r2, r2, r1, lsl #10
	str	r2, [r6, #3268]
	mov	r2, #1
	mov	r1, r2
	bl	FlashReadPages
	ldr	r2, [r6, #3264]
	ldr	ip, [fp, #-52]
	cmn	r2, #1
	ldrheq	r2, [r10]
	strheq	r2, [r4, #40]	@ movhi
	beq	.L951
	ldrh	r2, [r9, #8]
	ldr	r3, [fp, #-48]
	cmp	r3, r2
	bls	.L951
	ldrh	r1, [r4, #4]
	ldrh	r0, [r9]
	cmp	r0, r1
	ldreq	r1, [r6, #3268]
	ldreq	r3, [fp, #-44]
	streq	r1, [r3, r2, lsl #2]
.L951:
	add	r8, r8, #1
	b	.L949
.L948:
	ldr	r2, [r5, #3284]
	add	r3, r8, r0
	str	r3, [fp, #-60]
	str	r2, [r5, #3272]
	ldrh	r2, [r5, #104]
	ldrh	r1, [r8, r0]
	ldr	r0, .L971+4
	sub	r2, r2, #1
	orr	r2, r2, r1, lsl #10
	str	r2, [r5, #3268]
	mov	r2, #1
	mov	r1, r2
	bl	FlashReadPages
	ldr	r2, [r5, #3264]
	cmn	r2, #1
	beq	.L967
	ldrh	r1, [r9]
	ldrh	r2, [r4, #4]
	cmp	r1, r2
	bne	.L967
	ldrh	r1, [r9, #8]
	movw	r2, #64245
	cmp	r1, r2
	beq	.L956
.L967:
	mov	r10, #0
.L957:
	ldrh	r1, [r5, #104]
	sxth	r2, r10
	cmp	r2, r1
	bge	.L963
	ldr	r3, [fp, #-60]
	ldr	r0, .L971+4
	ldrh	r1, [r3]
	orr	r2, r2, r1, lsl #10
	str	r2, [r5, #3268]
	mov	r2, #1
	mov	r1, r2
	bl	FlashReadPages
	ldr	r2, [r5, #3264]
	cmn	r2, #1
	beq	.L961
	ldrh	r2, [r9, #8]
	ldr	r3, [fp, #-48]
	cmp	r3, r2
	bls	.L961
	ldrh	r1, [r4, #4]
	ldrh	r0, [r9]
	cmp	r0, r1
	ldreq	r1, [r5, #3268]
	ldreq	r3, [fp, #-44]
	streq	r1, [r3, r2, lsl #2]
.L961:
	add	r10, r10, #1
	b	.L957
.L956:
	mov	r0, #0
	mov	lr, #4
.L958:
	ldrh	r1, [r5, #104]
	sxth	r2, r0
	sub	r1, r1, #1
	cmp	r2, r1
	blt	.L960
.L963:
	add	r7, r7, #1
	b	.L947
.L960:
	ldr	ip, [r5, #3284]
	add	r0, r0, #1
	ldr	r3, [fp, #-48]
	ldr	r1, [ip, r2, lsl #3]
	uxth	r10, r1
	cmp	r3, r10
	addhi	r2, lr, r2, lsl #3
	ldrhi	r3, [fp, #-44]
	movhi	r1, r10
	ldrhi	r2, [ip, r2]
	strhi	r2, [r3, r1, lsl #2]
	b	.L958
.L972:
	.align	2
.L971:
	.word	.LANCHOR0
	.word	.LANCHOR0+3264
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
	ldr	r3, .L974
	ldrh	r2, [r3, #120]
	add	r0, r3, #3952
	add	r0, r0, #4
	strh	r2, [r0, #10]	@ movhi
	ldr	r2, .L974+4
	strh	r2, [r0, #4]	@ movhi
	ldrh	r2, [r3, #144]
	strh	r2, [r0, #8]	@ movhi
	ldrh	r2, [r3, #122]
	strh	r2, [r0, #6]	@ movhi
	ldr	r2, [r3, #148]
	str	r2, [r3, #3968]
	ldr	r2, [r3, #3884]
	str	r2, [r3, #3972]
	ldr	r2, [r3, #3880]
	str	r2, [r3, #3976]
	ldr	r2, [r3, #3888]
	str	r2, [r3, #3980]
	mvn	r3, #0
	strh	r3, [r0, #40]	@ movhi
	bl	FtlMapTblRecovery
	mov	r0, #0
	ldmfd	sp, {fp, sp, pc}
.L975:
	.align	2
.L974:
	.word	.LANCHOR0
	.word	-3962
	.size	FtlLoadVonderInfo, .-FtlLoadVonderInfo
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
	push	{r4, r5, r6, fp, ip, lr, pc}
	sub	fp, ip, #4
	ldr	r4, .L979
	mov	r1, #0
	mvn	r5, #0
	ldr	r2, [r4, #128]
	ldr	r0, [r4, #3876]
	lsl	r2, r2, #1
	bl	ftl_memset
	ldrh	r3, [r4, #110]
	mov	r1, #255
	ldrh	r2, [r4, #138]
	ldr	r0, [r4, #3900]
	mul	r2, r2, r3
	bl	ftl_memset
	mov	r2, #0
	mov	r3, r4
	mov	lr, #12
	mov	r4, r2
.L977:
	ldrh	r0, [r3, #138]
	uxth	r1, r2
	add	ip, r2, #1
	cmp	r0, r1
	bhi	.L978
	mvn	r1, #0
	movw	r0, #3908
	ldr	r2, .L979+4
	strh	r1, [r3, r0]	@ movhi
	ldr	r0, [r3, #128]
	strh	r1, [r2, #2]	@ movhi
	strh	r1, [r2, #40]	@ movhi
	strh	r0, [r2, #10]	@ movhi
	ldr	r0, .L979+8
	strh	r0, [r2, #4]	@ movhi
	ldrh	r0, [r2, #44]
	strh	r0, [r2, #8]	@ movhi
	ldrh	r0, [r3, #136]
	strh	r0, [r2, #6]	@ movhi
	ldr	r0, [r3, #3872]
	str	r0, [r3, #3920]
	ldr	r0, [r3, #3896]
	str	r0, [r3, #3924]
	ldr	r0, [r3, #3876]
	str	r0, [r3, #3928]
	ldr	r0, [r3, #3892]
	str	r0, [r3, #3932]
	ldmfd	sp, {r4, r5, r6, fp, sp, pc}
.L978:
	uxth	r2, r2
	ldr	r1, [r3, #2492]
	mul	r0, lr, r2
	add	r6, r1, r0
	str	r4, [r6, #4]
	strh	r5, [r1, r0]	@ movhi
	ldr	r1, [r3, #2492]
	add	r1, r1, r0
	ldrh	r0, [r3, #110]
	mul	r2, r2, r0
	ldr	r0, [r3, #3900]
	bic	r2, r2, #3
	add	r2, r0, r2
	str	r2, [r1, #8]
	mov	r2, ip
	b	.L977
.L980:
	.align	2
.L979:
	.word	.LANCHOR0
	.word	.LANCHOR0+3908
	.word	-3902
	.size	FtlL2PDataInit, .-FtlL2PDataInit
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
	ldr	r0, .L982
	bl	FtlMapTblRecovery
	mov	r0, #0
	ldmfd	sp, {fp, sp, pc}
.L983:
	.align	2
.L982:
	.word	.LANCHOR0+3908
	.size	FtlLoadMapInfo, .-FtlLoadMapInfo
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
	ldr	r4, .L985
	mvn	r3, #0
	mov	r5, #0
	mov	r1, r5
	add	r2, r4, #4000
	ldr	r0, [r4, #148]
	strh	r3, [r2]	@ movhi
	ldrh	r2, [r4, #120]
	str	r3, [r4, #4012]
	str	r5, [r4, #4004]
	str	r5, [r4, #4008]
	lsl	r2, r2, #1
	strh	r5, [r4, #144]	@ movhi
	bl	ftl_memset
	ldrh	r2, [r4, #42]
	mov	r1, r5
	ldr	r0, [r4, #2328]
	lsl	r2, r2, #1
	bl	ftl_memset
	ldrh	r2, [r4, #42]
	mov	r1, r5
	ldr	r0, [r4, #3860]
	lsl	r2, r2, #1
	bl	ftl_memset
	add	r0, r4, #2256
	mov	r1, #48
	add	r0, r0, #12
	bl	__memzero
	add	r0, r4, #2624
	mov	r1, #512
	add	r0, r0, #12
	bl	__memzero
	bl	FtlGcBufInit
	bl	FtlL2PDataInit
	mov	r0, r5
	ldmfd	sp, {r4, r5, fp, sp, pc}
.L986:
	.align	2
.L985:
	.word	.LANCHOR0
	.size	FtlVariablesInit, .-FtlVariablesInit
	.align	2
	.global	SupperBlkListInit
	.syntax unified
	.arm
	.fpu softvfp
	.type	SupperBlkListInit, %function
SupperBlkListInit:
	@ args = 0, pretend = 0, frame = 16
	@ frame_needed = 1, uses_anonymous_args = 0
	mov	ip, sp
	push	{r4, r5, r6, r7, r8, r9, r10, fp, ip, lr, pc}
	sub	fp, ip, #4
	sub	sp, sp, #16
	mov	r5, #0
	ldr	r4, .L998
	mov	r1, #0
	mov	r8, r5
	mov	r6, r5
	ldrh	r2, [r4, #42]
	ldr	r0, [r4, #2316]
	lsl	r2, r2, #3
	bl	ftl_memset
	add	r3, r4, #2336
	movw	r2, #2344
	str	r5, [r4, #2340]
	str	r5, [r4, #2320]
	str	r5, [r4, #2332]
	strh	r5, [r3]	@ movhi
	strh	r5, [r4, r2]	@ movhi
	str	r3, [fp, #-48]
.L988:
	ldrh	r3, [r4, #40]
	uxth	r10, r5
	cmp	r10, r3
	bcs	.L995
	ldrh	r3, [r4, #102]
	mov	r9, r10
	ldrh	ip, [r4, #32]
	str	r3, [fp, #-44]
	mov	r3, #0
	mov	r7, r3
	b	.L996
.L990:
	ldr	r2, .L998+4
	mov	r1, r9
	str	ip, [fp, #-56]
	str	r3, [fp, #-52]
	ldrb	r0, [r2, r3]	@ zero_extendqisi2
	bl	V2P_block
	bl	FtlBbmIsBadBlock
	cmp	r0, #0
	ldr	r3, [fp, #-52]
	ldreq	r2, [fp, #-44]
	ldr	ip, [fp, #-56]
	add	r3, r3, #1
	addeq	r7, r7, r2
	uxtheq	r7, r7
.L996:
	uxth	r1, r3
	cmp	ip, r1
	bhi	.L990
	cmp	r7, #0
	beq	.L991
	mov	r1, r7
	mov	r0, #32768
	bl	__divsi3
	uxth	r7, r0
.L992:
	ldr	r3, [r4, #2316]
	add	r3, r3, r9, lsl #3
	strh	r7, [r3, #4]	@ movhi
	ldr	r3, .L998+8
	ldrh	r1, [r3]
	cmp	r1, r10
	beq	.L993
	ldrh	r1, [r3, #48]
	cmp	r1, r10
	beq	.L993
	ldrh	r3, [r3, #96]
	cmp	r3, r10
	beq	.L993
	ldr	r2, [r4, #2324]
	lsl	r3, r9, #1
	ldrh	r3, [r2, r3]
	cmp	r3, #0
	bne	.L994
	add	r8, r8, #1
	mov	r0, r9
	uxth	r8, r8
	bl	INSERT_FREE_LIST
.L993:
	add	r5, r5, #1
	b	.L988
.L991:
	ldr	r1, [r4, #2324]
	lsl	r3, r9, #1
	mvn	r0, #0
	strh	r0, [r1, r3]	@ movhi
	b	.L992
.L994:
	add	r6, r6, #1
	mov	r0, r9
	uxth	r6, r6
	bl	INSERT_DATA_LIST
	b	.L993
.L995:
	ldr	r2, [fp, #-48]
	strh	r6, [r2]	@ movhi
	add	r6, r6, r8
	cmp	r6, r3
	movw	r2, #2344
	strh	r8, [r4, r2]	@ movhi
	ble	.L997
	movw	r2, #2206
	ldr	r1, .L998+12
	ldr	r0, .L998+16
	bl	sftl_printk
.L997:
	mov	r0, #0
	sub	sp, fp, #40
	ldmfd	sp, {r4, r5, r6, r7, r8, r9, r10, fp, sp, pc}
.L999:
	.align	2
.L998:
	.word	.LANCHOR0
	.word	.LANCHOR0+60
	.word	.LANCHOR0+2348
	.word	.LANCHOR1+342
	.word	.LC1
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
	push	{r4, fp, ip, lr, pc}
	sub	fp, ip, #4
	ldr	r4, .L1001
	mov	r3, #0
	movw	r2, #3192
	mov	r1, #255
	strh	r3, [r4, r2]	@ movhi
	add	r2, r4, #3200
	strh	r3, [r2]	@ movhi
	ldrh	r2, [r4, #106]
	ldr	r0, [r4, #3188]
	lsl	r2, r2, #1
	bl	ftl_memset
	ldrh	r3, [r4, #106]
	mov	r2, #12
	mov	r1, #255
	ldr	r0, [r4, #3196]
	mul	r2, r2, r3
	bl	ftl_memset
	bl	FtlGcBufInit
	ldmfd	sp, {r4, fp, sp, pc}
.L1002:
	.align	2
.L1001:
	.word	.LANCHOR0
	.size	FtlGcPageVarInit, .-FtlGcPageVarInit
	.align	2
	.global	FtlGcScanTempBlk
	.syntax unified
	.arm
	.fpu softvfp
	.type	FtlGcScanTempBlk, %function
FtlGcScanTempBlk:
	@ args = 0, pretend = 0, frame = 20
	@ frame_needed = 1, uses_anonymous_args = 0
	mov	ip, sp
	push	{r4, r5, r6, r7, r8, r9, r10, fp, ip, lr, pc}
	sub	fp, ip, #4
	sub	sp, sp, #20
	mov	r6, r0
	ldr	r3, .L1026
	mov	r10, r1
	ldrh	r4, [r3, #4]
	movw	r3, #65535
	cmp	r4, r3
	beq	.L1020
	cmp	r4, #0
	bne	.L1004
.L1005:
	bl	FtlGcPageVarInit
	b	.L1006
.L1020:
	mov	r4, #0
.L1004:
	ldr	r3, .L1026+4
	ldrh	r3, [r3, #102]
	cmp	r3, r10
	beq	.L1005
.L1006:
	ldr	r5, .L1026+4
	mov	r8, #0
.L1014:
	ldrh	r2, [r6]
	movw	r3, #65535
	mov	r0, #0
	strb	r0, [r6, #8]
	cmp	r2, r3
	beq	.L1007
.L1017:
	ldrh	r3, [r5, #32]
	mov	r2, #0
	ldr	r0, [r5, #3836]
	add	ip, r6, #16
	mov	r7, r2
	movw	r9, #65535
	str	r3, [fp, #-44]
	ldr	r3, [r5, #3172]
	str	r3, [fp, #-48]
	ldrh	r3, [r5, #110]
	str	r3, [fp, #-52]
	ldr	r3, [r5, #3176]
	str	r3, [fp, #-56]
	ldrh	r3, [r5, #112]
	str	r3, [fp, #-60]
.L1008:
	ldr	r1, [fp, #-44]
	uxth	r3, r2
	cmp	r1, r3
	bhi	.L1010
	mov	r9, #0
	mov	r2, #0
	mov	r1, r7
	bl	FlashReadPages
.L1011:
	uxth	r3, r9
	cmp	r7, r3
	bhi	.L1015
	add	r8, r8, #1
	add	r4, r4, #1
	cmp	r10, r8
	uxth	r4, r4
	bls	.L1016
.L1018:
	ldrh	r3, [r5, #102]
	cmp	r3, r4
	bhi	.L1017
	mov	r0, #0
	b	.L1007
.L1010:
	ldrh	r3, [ip], #2
	cmp	r3, r9
	beq	.L1009
	mov	r1, #20
	orr	r3, r4, r3, lsl #10
	mla	r1, r1, r7, r0
	str	r3, [r1, #4]
	ldr	r3, [fp, #-52]
	mul	r3, r3, r7
	add	lr, r3, #3
	cmp	r3, #0
	movlt	r3, lr
	ldr	lr, [fp, #-48]
	bic	r3, r3, #3
	add	r3, lr, r3
	str	r3, [r1, #8]
	ldr	r3, [fp, #-60]
	mul	r3, r3, r7
	add	r7, r7, #1
	uxth	r7, r7
	add	lr, r3, #3
	cmp	r3, #0
	movlt	r3, lr
	ldr	lr, [fp, #-56]
	bic	r3, r3, #3
	add	r3, lr, r3
	str	r3, [r1, #12]
.L1009:
	add	r2, r2, #1
	b	.L1008
.L1015:
	mov	r3, #20
	ldr	r2, [r5, #3836]
	mul	r3, r3, r9
	str	r2, [fp, #-52]
	add	ip, r2, r3
	str	r3, [fp, #-56]
	ldr	r1, [ip, #4]
	str	ip, [fp, #-48]
	ubfx	r0, r1, #10, #16
	str	r1, [fp, #-44]
	bl	P2V_plane
	ldr	r2, [fp, #-52]
	ldr	r3, [fp, #-56]
	ldr	r3, [r2, r3]
	cmp	r3, #0
	bne	.L1012
	ldr	ip, [fp, #-48]
	movw	r2, #65535
	add	r9, r9, #1
	ldr	r1, [fp, #-44]
	ldr	r3, [ip, #12]
	ldrh	ip, [r3]
	cmp	ip, r2
	bne	.L1013
	mov	r3, #1
	str	r3, [r5, #4008]
.L1007:
	ldr	r3, .L1026
	mvn	r2, #0
	strb	r0, [r6, #6]
	mov	r1, r4
	strh	r4, [r6, #2]	@ movhi
	strh	r2, [r3, #4]	@ movhi
	mov	r2, r0
	mov	r0, r6
	bl	ftl_sb_update_avl_pages
	b	.L1019
.L1013:
	ldr	r2, [r3, #8]
	ldr	r0, [r3, #12]
	bl	FtlGcUpdatePage
	b	.L1011
.L1012:
	ldrh	r3, [r6]
	mov	r4, #0
	ldr	r2, [r5, #2324]
	lsl	r3, r3, #1
	strh	r4, [r2, r3]	@ movhi
	ldrh	r0, [r6]
	bl	INSERT_FREE_LIST
	mvn	r3, #0
	strh	r3, [r6]	@ movhi
	bl	FtlGcPageVarInit
	b	.L1014
.L1016:
	ldr	r2, .L1026
	movw	r1, #65535
	ldrh	r3, [r2, #4]
	cmp	r3, r1
	beq	.L1018
	add	r3, r3, r8
	strh	r3, [r2, #4]	@ movhi
	ldrh	r3, [r5, #102]
	cmp	r3, r4
	bls	.L1018
.L1019:
	mvn	r0, #0
	sub	sp, fp, #40
	ldmfd	sp, {r4, r5, r6, r7, r8, r9, r10, fp, sp, pc}
.L1027:
	.align	2
.L1026:
	.word	.LANCHOR2
	.word	.LANCHOR0
	.size	FtlGcScanTempBlk, .-FtlGcScanTempBlk
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
	.global	FtlBbmTblFlush
	.syntax unified
	.arm
	.fpu softvfp
	.type	FtlBbmTblFlush, %function
FtlBbmTblFlush:
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 1, uses_anonymous_args = 0
	mov	ip, sp
	push	{r4, r5, r6, r7, r8, r9, r10, fp, ip, lr, pc}
	sub	fp, ip, #4
	sub	sp, sp, #4
	mov	r5, #0
	ldr	r4, .L1038
	mov	r1, #0
	ldr	r0, [r4, #3284]
	add	r6, r4, #176
	ldr	r3, [r4, #3288]
	add	r7, r4, #3904
	ldrh	r2, [r4, #110]
	str	r0, [r4, #3272]
	str	r3, [r4, #3276]
	bl	ftl_memset
.L1030:
	ldrh	r3, [r4, #54]
	cmp	r5, r3
	blt	.L1031
	ldr	r6, [r4, #3276]
	mov	r2, #16
	mov	r1, #255
	ldr	r9, .L1038+4
	ldr	r8, .L1038+8
	mov	r5, #0
	mov	r0, r6
	mov	r7, r5
	bl	memset
	ldr	r3, .L1038+12
	strh	r3, [r6]	@ movhi
	ldr	r3, [r4, #160]
	str	r3, [r6, #4]
	ldrh	r3, [r4, #152]
	strh	r3, [r6, #2]	@ movhi
	ldrh	r3, [r4, #156]
	strh	r3, [r6, #8]	@ movhi
	ldrh	r3, [r4, #158]
	strh	r3, [r6, #10]	@ movhi
	ldr	r3, [r4, #28]
	strh	r3, [r6, #12]	@ movhi
.L1032:
	ldr	r3, [r4, #3284]
	mov	r10, #0
	ldrh	r1, [r4, #152]
	ldrh	r2, [r4, #154]
	str	r3, [r4, #3272]
	ldr	r3, [r4, #3288]
	str	r10, [r4, #3264]
	str	r3, [r4, #3276]
	orr	r3, r2, r1, lsl #10
	ldrh	r0, [r6, #10]
	str	r3, [r4, #3268]
	ldrh	r3, [r4, #156]
	str	r0, [sp]
	mov	r0, r9
	bl	sftl_printk
	mov	r3, #1
	ldr	r0, .L1038+8
	mov	r2, r3
	mov	r1, r3
	bl	FlashProgPages
	ldrh	r3, [r4, #104]
	ldrh	r2, [r4, #154]
	sub	r3, r3, #1
	cmp	r2, r3
	blt	.L1033
	ldr	r3, [r4, #160]
	ldrh	r2, [r4, #152]
	ldr	r0, [r4, #3804]
	add	r3, r3, #1
	strh	r10, [r4, #154]	@ movhi
	str	r3, [r4, #160]
	str	r3, [r6, #4]
	ldrh	r3, [r4, #156]
	strh	r2, [r6, #8]	@ movhi
	strh	r2, [r4, #156]	@ movhi
	mov	r2, #1
	strh	r3, [r4, #152]	@ movhi
	mov	r1, r2
	lsl	r3, r3, #10
	str	r3, [r4, #3268]
	str	r3, [r0, #4]
	bl	FlashEraseBlocks
	mov	r3, #1
	mov	r0, r8
	mov	r2, r3
	mov	r1, r3
	bl	FlashProgPages
.L1033:
	ldrh	r3, [r4, #154]
	add	r3, r3, #1
	strh	r3, [r4, #154]	@ movhi
	ldr	r3, [r4, #3264]
	cmn	r3, #1
	bne	.L1034
	add	r5, r5, #1
	ldr	r1, [r4, #3268]
	uxth	r5, r5
	ldr	r0, .L1038+16
	bl	sftl_printk
	cmp	r5, #3
	bls	.L1032
	mov	r2, r5
	ldr	r1, [r4, #3268]
	ldr	r0, .L1038+20
	bl	sftl_printk
.L1036:
	b	.L1036
.L1031:
	ldrh	r2, [r7]
	ldr	r3, [r4, #3272]
	ldr	r1, [r6, #4]!
	mul	r0, r2, r5
	lsl	r2, r2, #2
	add	r5, r5, #1
	add	r0, r3, r0, lsl #2
	bl	ftl_memcpy
	b	.L1030
.L1037:
	mov	r7, #1
	b	.L1032
.L1034:
	cmp	r7, #0
	beq	.L1037
	mov	r0, #0
	ldmib	sp, {r4, r5, r6, r7, r8, r9, r10, fp, sp, pc}
.L1039:
	.align	2
.L1038:
	.word	.LANCHOR0
	.word	.LC100
	.word	.LANCHOR0+3264
	.word	-3887
	.word	.LC101
	.word	.LC102
	.size	FtlBbmTblFlush, .-FtlBbmTblFlush
	.align	2
	.global	allocate_data_superblock
	.syntax unified
	.arm
	.fpu softvfp
	.type	allocate_data_superblock, %function
allocate_data_superblock:
	@ args = 0, pretend = 0, frame = 16
	@ frame_needed = 1, uses_anonymous_args = 0
	mov	ip, sp
	push	{r4, r5, r6, r7, r8, r9, r10, fp, ip, lr, pc}
	sub	fp, ip, #4
	sub	sp, sp, #16
	mov	r5, r0
	ldr	r4, .L1075
	add	r9, r4, #2336
.L1041:
	ldr	r7, .L1075+4
	ldrh	r2, [r9]
	ldrh	r3, [r7]
	add	r3, r3, r2
	ldrh	r2, [r4, #40]
	cmp	r3, r2
	ble	.L1042
	movw	r2, #2654
	ldr	r1, .L1075+8
	ldr	r0, .L1075+12
	bl	sftl_printk
.L1042:
	ldr	r3, .L1075+16
	cmp	r5, r3
	movne	r1, #0
	bne	.L1043
	ldrh	r3, [r7]
	ldr	r1, [r4, #3160]
	mul	r2, r1, r3
	lsr	r1, r3, #1
	add	r1, r1, #1
	add	r1, r1, r2, lsr #2
	uxth	r1, r1
	cmp	r1, #0
	subne	r1, r1, #1
	uxthne	r1, r1
.L1043:
	ldr	r0, .L1075+20
	bl	List_pop_index_node
	ldrh	r3, [r7]
	mov	r6, r0
	uxth	r8, r0
	cmp	r3, #0
	bne	.L1044
	movw	r2, #2663
	ldr	r1, .L1075+8
	ldr	r0, .L1075+12
	bl	sftl_printk
.L1044:
	ldrh	r3, [r7]
	mov	r0, r5
	sub	r3, r3, #1
	strh	r3, [r7]	@ movhi
	strh	r8, [r5]	@ movhi
	bl	make_superblock
	ldrb	r3, [r5, #7]	@ zero_extendqisi2
	cmp	r3, #0
	bne	.L1045
	uxth	r0, r6
	ldr	r2, [r4, #2324]
	mvn	r1, #0
	lsl	r3, r0, #1
	strh	r1, [r2, r3]	@ movhi
	bl	INSERT_DATA_LIST
	ldrh	r2, [r9]
	ldrh	r3, [r7]
	add	r3, r3, r2
	ldrh	r2, [r4, #40]
	cmp	r3, r2
	ble	.L1041
	movw	r2, #2674
	ldr	r1, .L1075+8
	ldr	r0, .L1075+12
	bl	sftl_printk
	b	.L1041
.L1045:
	ldrh	r2, [r9]
	ldrh	r3, [r7]
	add	r3, r3, r2
	ldrh	r2, [r4, #40]
	cmp	r3, r2
	ble	.L1047
	movw	r2, #2677
	ldr	r1, .L1075+8
	ldr	r0, .L1075+12
	bl	sftl_printk
.L1047:
	ldr	r0, [r4, #3804]
	mov	r2, #20
	ldrh	r1, [r4, #32]
	mov	r7, #0
	add	ip, r5, #16
	mov	lr, r7
	mov	r3, r0
	mla	r1, r2, r1, r0
	str	r1, [fp, #-44]
.L1048:
	ldr	r1, [fp, #-44]
	cmp	r1, r3
	bne	.L1050
	cmp	r7, #0
	bne	.L1051
	mov	r2, #2688
	ldr	r1, .L1075+8
	ldr	r0, .L1075+12
	bl	sftl_printk
.L1051:
	ldr	r3, .L1075+24
	ldrh	r3, [r3]
	cmp	r3, r8
	bne	.L1052
	movw	r2, #2690
	ldr	r1, .L1075+8
	ldr	r0, .L1075+12
	bl	sftl_printk
.L1052:
	ldrb	r3, [r5, #8]	@ zero_extendqisi2
	uxth	r6, r6
	cmp	r3, #0
	bne	.L1053
	ldr	r1, [r4, #2328]
	lsl	r2, r6, #1
	ldrh	r3, [r1, r2]
	cmp	r3, #0
	ldrhne	r0, [r4, #92]
	moveq	r3, #2
	addne	r3, r3, r0
	mov	r0, r6
	strh	r3, [r1, r2]	@ movhi
	mov	r1, #0
	ldr	r3, [r4, #2548]
	add	r3, r3, #1
	str	r3, [r4, #2548]
	bl	ftl_set_blk_mode
.L1056:
	ldr	r3, [r4, #2328]
	lsl	r10, r6, #1
	ldr	r2, [r4, #2564]
	ldr	r0, [r4, #2548]
	ldrh	r3, [r3, r10]
	ldrh	r1, [r4, #40]
	cmp	r3, r2
	ldrh	r2, [r4, #92]
	strhi	r3, [r4, #2564]
	ldr	r3, [r4, #2552]
	mla	r0, r0, r2, r3
	bl	__udivsi3
	ldr	r2, [r4, #3864]
	ldr	r1, [r4, #3804]
	str	r0, [r4, #2556]
	ldr	r3, [r2, #16]
	ldr	r0, .L1075+28
	add	r3, r3, #1
	str	r3, [r2, #16]
	mov	r2, #20
	mla	r2, r2, r7, r1
	add	r3, r1, #4
	add	r2, r2, #24
.L1058:
	add	r3, r3, #20
	cmp	r2, r3
	bne	.L1059
	ldrb	r1, [r5, #8]	@ zero_extendqisi2
	mov	r2, r7
	ldr	r0, [r4, #3804]
	bl	FlashEraseBlocks
	mov	r3, #0
	mov	ip, #20
	mov	r1, r3
.L1060:
	uxth	r2, r3
	cmp	r7, r2
	bhi	.L1062
	cmp	r1, #0
	ble	.L1063
	mov	r0, r6
	bl	update_multiplier_value
	bl	FtlBbmTblFlush
.L1063:
	ldrb	r2, [r5, #7]	@ zero_extendqisi2
	cmp	r2, #0
	bne	.L1064
	ldr	r3, [r4, #2324]
	mvn	r2, #0
	strh	r2, [r3, r10]	@ movhi
	b	.L1041
.L1050:
	str	lr, [r3, #8]
	movw	r10, #65535
	str	lr, [r3, #12]
	add	r3, r3, #20
	ldrh	r1, [ip], #2
	cmp	r1, r10
	mlane	r10, r2, r7, r0
	lslne	r1, r1, #10
	addne	r7, r7, #1
	uxthne	r7, r7
	strne	r1, [r10, #4]
	b	.L1048
.L1053:
	ldr	r1, [r4, #2328]
	lsl	r3, r6, #1
	mov	r0, r6
	ldrh	r2, [r1, r3]
	add	r2, r2, #1
	strh	r2, [r1, r3]	@ movhi
	ldr	r3, [r4, #2552]
	add	r3, r3, #1
	str	r3, [r4, #2552]
	bl	ftl_set_blk_mode.part.6
	b	.L1056
.L1059:
	ldr	r1, [r3, #-20]
	and	r1, r1, r0
	str	r1, [r3, #-20]
	b	.L1058
.L1062:
	mul	r2, ip, r3
	ldr	lr, [r4, #3804]
	add	r0, lr, r2
	ldr	r2, [lr, r2]
	cmn	r2, #1
	bne	.L1061
	ldr	r0, [r0, #4]
	add	r1, r1, #1
	str	ip, [fp, #-56]
	str	r2, [fp, #-52]
	ubfx	r0, r0, #10, #16
	str	r3, [fp, #-48]
	str	r1, [fp, #-44]
	bl	FtlBbmMapBadBlock
	ldr	r3, [fp, #-48]
	ldr	r2, [fp, #-52]
	ldr	ip, [fp, #-56]
	ldr	r1, [fp, #-44]
	add	r0, r5, r3, lsl #1
	strh	r2, [r0, #16]	@ movhi
	ldrb	r2, [r5, #7]	@ zero_extendqisi2
	sub	r2, r2, #1
	strb	r2, [r5, #7]
.L1061:
	add	r3, r3, #1
	b	.L1060
.L1064:
	ldrh	r3, [r4, #102]
	strh	r8, [r5]	@ movhi
	smulbb	r3, r3, r2
	mov	r2, #0
	strh	r2, [r5, #2]	@ movhi
	strb	r2, [r5, #6]
	ldr	r2, [r4, #2540]
	uxth	r3, r3
	ldr	r1, [r4, #2324]
	strh	r3, [r5, #4]	@ movhi
	str	r2, [r5, #12]
	add	r2, r2, #1
	str	r2, [r4, #2540]
	ldrh	r2, [r5]
	lsl	r2, r2, #1
	strh	r3, [r1, r2]	@ movhi
	ldrh	r3, [r5, #4]
	cmp	r3, #0
	beq	.L1065
	ldrb	r3, [r5, #7]	@ zero_extendqisi2
	cmp	r3, #0
	bne	.L1066
.L1065:
	movw	r2, #2743
	ldr	r1, .L1075+8
	ldr	r0, .L1075+12
	bl	sftl_printk
.L1066:
	mov	r0, #0
	sub	sp, fp, #40
	ldmfd	sp, {r4, r5, r6, r7, r8, r9, r10, fp, sp, pc}
.L1076:
	.align	2
.L1075:
	.word	.LANCHOR0
	.word	.LANCHOR0+2344
	.word	.LANCHOR1+360
	.word	.LC1
	.word	.LANCHOR0+2444
	.word	.LANCHOR0+2340
	.word	.LANCHOR0+2588
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
	ldr	r6, .L1089
	str	r3, [fp, #-44]
	movw	r3, #3210
	ldrh	r3, [r6, r3]
	cmp	r3, #0
	movne	r7, #0
	ldrne	r10, .L1089+4
	bne	.L1079
.L1078:
	mov	r0, #0
	sub	sp, fp, #40
	ldmfd	sp, {r4, r5, r6, r7, r8, r9, r10, fp, sp, pc}
.L1085:
	uxtah	r3, r6, r7
	ldr	r1, [fp, #-44]
	mov	r8, #0
	ldrb	r0, [r3, #60]	@ zero_extendqisi2
	bl	V2P_block
	mov	r9, r0
.L1080:
	ldrh	r3, [r10]
	uxth	r4, r8
	cmp	r3, r4
	bhi	.L1084
	add	r7, r7, #1
.L1079:
	ldrh	r2, [r6, #32]
	uxth	r3, r7
	cmp	r2, r3
	bhi	.L1085
	bl	FtlGcReFreshBadBlk
	b	.L1078
.L1084:
	uxth	r3, r8
	ldr	r5, .L1089+8
	lsl	r2, r3, #1
	ldrh	r2, [r5, r2]
	cmp	r2, r9
	bne	.L1081
	mov	r1, r9
	ldr	r0, .L1089+12
	str	r3, [fp, #-48]
	bl	sftl_printk
	mov	r0, r9
	bl	FtlBbmMapBadBlock
	bl	FtlBbmTblFlush
	ldr	r3, [fp, #-48]
	ldrh	r2, [r10]
	add	r3, r5, r3, lsl #1
.L1082:
	cmp	r4, r2
	bcc	.L1083
	sub	r2, r2, #1
	strh	r2, [r10]	@ movhi
.L1081:
	add	r8, r8, #1
	b	.L1080
.L1083:
	ldrh	r1, [r3, #2]!
	add	r4, r4, #1
	uxth	r4, r4
	strh	r1, [r3, #-2]	@ movhi
	b	.L1082
.L1090:
	.align	2
.L1089:
	.word	.LANCHOR0
	.word	.LANCHOR0+3210
	.word	.LANCHOR0+3212
	.word	.LC103
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
	ldr	r3, .L1101
	uxth	r4, r0
	ldr	r1, [r3, #2324]
	lsl	r2, r4, #1
	mov	r6, r3
	ldrh	r0, [r1, r2]
	cmp	r0, #0
	bne	.L1092
	movw	r2, #2588
	ldrh	r1, [r3, r2]
	cmp	r1, r4
	mvneq	r3, #0
	strheq	r3, [r6, r2]	@ movhi
	beq	.L1094
	movw	r2, #2348
	ldrh	r2, [r3, r2]
	cmp	r2, r4
	ldmfdeq	sp, {r4, r5, r6, fp, sp, pc}
	movw	r2, #2396
	ldrh	r2, [r3, r2]
	cmp	r2, r4
	ldmfdeq	sp, {r4, r5, r6, fp, sp, pc}
	movw	r2, #2444
	ldrh	r3, [r3, r2]
	cmp	r3, r4
	ldmfdeq	sp, {r4, r5, r6, fp, sp, pc}
.L1094:
	ldr	r5, .L1101+4
	mov	r1, r4
	ldr	r0, .L1101+8
	bl	List_remove_node
	ldrh	r3, [r5]
	cmp	r3, #0
	bne	.L1096
	movw	r2, #2813
	ldr	r1, .L1101+12
	ldr	r0, .L1101+16
	bl	sftl_printk
.L1096:
	ldrh	r3, [r5]
	mov	r0, r4
	sub	r3, r3, #1
	strh	r3, [r5]	@ movhi
	bl	free_data_superblock
	mov	r0, r4
	bl	FtlGcFreeBadSuperBlk
	movw	r3, #2344
	ldrh	r2, [r5]
	ldrh	r3, [r6, r3]
	add	r3, r3, r2
	ldrh	r2, [r6, #40]
	cmp	r3, r2
	ble	.L1100
	mov	r2, #2816
	ldr	r1, .L1101+12
	ldr	r0, .L1101+16
	bl	sftl_printk
.L1100:
	mov	r0, #1
	ldmfd	sp, {r4, r5, r6, fp, sp, pc}
.L1092:
	mov	r0, r4
	bl	List_update_data_list
	mov	r0, #0
	ldmfd	sp, {r4, r5, r6, fp, sp, pc}
.L1102:
	.align	2
.L1101:
	.word	.LANCHOR0
	.word	.LANCHOR0+2336
	.word	.LANCHOR0+2320
	.word	.LANCHOR1+385
	.word	.LC1
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
	uxth	r4, r0
	movw	r3, #65535
	cmp	r4, r3
	ldr	r5, .L1118
	beq	.L1104
	ldr	r3, [r5, #2324]
	lsl	r6, r4, #1
	ldrh	r2, [r3, r6]
	cmp	r2, #0
	subne	r2, r2, #1
	strhne	r2, [r3, r6]	@ movhi
	bne	.L1104
	mov	r1, r4
	ldr	r0, .L1118+4
	bl	sftl_printk
	ldr	r3, [r5, #2324]
	ldrh	r3, [r3, r6]
	cmp	r3, #0
	bne	.L1106
	movw	r2, #2831
	ldr	r1, .L1118+8
	ldr	r0, .L1118+12
	bl	sftl_printk
.L1106:
	ldr	r3, [r5, #2324]
	mov	r2, #16
	mov	r1, r4
	ldr	r0, .L1118+16
	strh	r2, [r3, r6]	@ movhi
	bl	test_node_in_list
	cmp	r0, #0
	beq	.L1107
	mov	r1, r4
	ldr	r0, .L1118+16
	bl	List_remove_node
	movw	r3, #2344
	ldrh	r3, [r5, r3]
	cmp	r3, #0
	bne	.L1108
	movw	r2, #2835
	ldr	r1, .L1118+8
	ldr	r0, .L1118+12
	bl	sftl_printk
.L1108:
	movw	r2, #2344
	mov	r0, r4
	ldrh	r3, [r5, r2]
	sub	r3, r3, #1
	strh	r3, [r5, r2]	@ movhi
	bl	INSERT_DATA_LIST
	ldr	r3, [r5, #2324]
	mov	r1, r4
	ldr	r0, .L1118+20
	ldrh	r2, [r3, r6]
	bl	sftl_printk
.L1107:
	mov	r0, r4
	bl	FtlGcRefreshBlock
.L1111:
	mov	r6, #0
	b	.L1103
.L1104:
	ldr	r7, .L1118+24
	movw	r3, #65535
	ldrh	r0, [r7]
	cmp	r0, r3
	strheq	r4, [r7]	@ movhi
	beq	.L1111
	cmp	r4, r0
	beq	.L1111
	bl	update_vpc_list
	ldr	r2, [r5, #2316]
	adds	r6, r0, #0
	ldr	r3, [r5, #2320]
	movne	r6, #1
	ldr	r1, [r5, #2324]
	strh	r4, [r7]	@ movhi
	sub	r3, r3, r2
	asr	r3, r3, #3
	uxth	r2, r3
	uxth	r3, r3
	lsl	r2, r2, #1
	cmp	r3, r4
	ldrh	r2, [r1, r2]
	clz	r2, r2
	lsr	r2, r2, #5
	moveq	r2, #0
	cmp	r2, #0
	beq	.L1103
	movw	r2, #2856
	ldr	r1, .L1118+8
	ldr	r0, .L1118+12
	bl	sftl_printk
.L1103:
	mov	r0, r6
	ldmfd	sp, {r4, r5, r6, r7, fp, sp, pc}
.L1119:
	.align	2
.L1118:
	.word	.LANCHOR0
	.word	.LC104
	.word	.LANCHOR1+401
	.word	.LC1
	.word	.LANCHOR0+2340
	.word	.LC105
	.word	.LANCHOR0+4000
	.size	decrement_vpc_count, .-decrement_vpc_count
	.align	2
	.global	FtlWriteDump_data
	.syntax unified
	.arm
	.fpu softvfp
	.type	FtlWriteDump_data, %function
FtlWriteDump_data:
	@ args = 0, pretend = 0, frame = 28
	@ frame_needed = 1, uses_anonymous_args = 0
	mov	ip, sp
	push	{r4, r5, r6, r7, r8, r9, r10, fp, ip, lr, pc}
	sub	fp, ip, #4
	sub	sp, sp, #28
	ldr	r6, .L1138
	ldr	r4, .L1138+4
	ldrh	r2, [r6, #4]
	cmp	r2, #0
	beq	.L1121
	ldrb	r3, [r4, #2356]	@ zero_extendqisi2
	cmp	r3, #0
	bne	.L1121
	ldrb	r1, [r4, #2355]	@ zero_extendqisi2
	ldrh	r3, [r4, #102]
	mul	r3, r3, r1
	cmp	r2, r3
	beq	.L1121
	ldrb	r8, [r4, #2358]	@ zero_extendqisi2
	cmp	r8, #0
	bne	.L1120
	ldr	r7, [r4, #2504]
	mov	r2, r8
	sub	r1, fp, #64
	ldrh	r9, [r4, #32]
	sub	r7, r7, #1
	mov	r0, r7
	bl	log2phys
	ldr	r3, [fp, #-64]
	ldr	r5, [r4, #3288]
	ldr	r0, [r4, #3284]
	cmn	r3, #1
	str	r3, [fp, #-56]
	str	r7, [fp, #-44]
	str	r0, [fp, #-52]
	str	r5, [fp, #-48]
	str	r8, [r5, #4]
	beq	.L1123
	mov	r2, r8
	mov	r1, #1
	sub	r0, fp, #60
	bl	FlashReadPages
.L1124:
	ldr	ip, .L1138
	mov	r8, #0
	ldr	r3, .L1138+8
	lsl	r9, r9, #2
	mov	r10, r8
	strh	r3, [r5]	@ movhi
.L1125:
	cmp	r9, r8
	bne	.L1129
.L1126:
	mov	r3, #1
.L1137:
	strb	r3, [r4, #2358]
.L1120:
	sub	sp, fp, #40
	ldmfd	sp, {r4, r5, r6, r7, r8, r9, r10, fp, sp, pc}
.L1123:
	ldrh	r2, [r4, #110]
	mov	r1, #255
	bl	ftl_memset
	b	.L1124
.L1129:
	ldrh	r3, [r6, #4]
	cmp	r3, #0
	beq	.L1126
	ldr	r3, [fp, #-56]
	mov	r0, ip
	str	r7, [r5, #8]
	add	r8, r8, #1
	str	ip, [fp, #-68]
	str	r3, [r5, #12]
	ldrh	r3, [r6]
	strh	r3, [r5, #2]	@ movhi
	bl	get_new_active_ppa
	ldr	r3, [r4, #2544]
	mov	r1, #1
	str	r0, [fp, #-56]
	sub	r0, fp, #60
	str	r3, [r5, #4]
	add	r3, r3, #1
	cmn	r3, #1
	moveq	r3, r10
	str	r3, [r4, #2544]
	mov	r3, #0
	mov	r2, r3
	bl	FlashProgPages
	ldrh	r0, [r6]
	bl	decrement_vpc_count
	ldr	ip, [fp, #-68]
	b	.L1125
.L1121:
	mov	r3, #0
	b	.L1137
.L1139:
	.align	2
.L1138:
	.word	.LANCHOR0+2348
	.word	.LANCHOR0
	.word	-3947
	.size	FtlWriteDump_data, .-FtlWriteDump_data
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
	ldr	r5, .L1144
	mov	r4, #0
	mov	r6, #12
	bl	FtlWriteDump_data
.L1141:
	ldrh	r2, [r5, #138]
	uxth	r3, r4
	cmp	r2, r3
	bhi	.L1143
	mov	r0, #0
	ldmfd	sp, {r4, r5, r6, fp, sp, pc}
.L1143:
	ldr	r3, [r5, #2492]
	uxth	r0, r4
	mla	r3, r6, r0, r3
	ldr	r3, [r3, #4]
	cmp	r3, #0
	bge	.L1142
	bl	flush_l2p_region
.L1142:
	add	r4, r4, #1
	b	.L1141
.L1145:
	.align	2
.L1144:
	.word	.LANCHOR0
	.size	l2p_flush, .-l2p_flush
	.align	2
	.global	FtlRecoverySuperblock
	.syntax unified
	.arm
	.fpu softvfp
	.type	FtlRecoverySuperblock, %function
FtlRecoverySuperblock:
	@ args = 0, pretend = 0, frame = 44
	@ frame_needed = 1, uses_anonymous_args = 0
	mov	ip, sp
	push	{r4, r5, r6, r7, r8, r9, r10, fp, ip, lr, pc}
	sub	fp, ip, #4
	sub	sp, sp, #44
	movw	r2, #65535
	ldrh	r3, [r0]
	mov	r4, r0
	cmp	r3, r2
	beq	.L1267
	ldrh	r3, [r0, #2]
	ldr	r6, .L1274
	str	r3, [fp, #-64]
	ldrb	r3, [r0, #6]	@ zero_extendqisi2
	ldr	r1, [fp, #-64]
	str	r3, [fp, #-80]
	ldrh	r3, [r6, #102]
	cmp	r3, r1
	mov	r3, #0
	strheq	r3, [r0, #4]	@ movhi
	ldrhne	r0, [r0, #16]
	bne	.L1150
.L1272:
	strb	r3, [r4, #6]
.L1267:
	mov	r0, #0
	sub	sp, fp, #40
	ldmfd	sp, {r4, r5, r6, r7, r8, r9, r10, fp, sp, pc}
.L1151:
	uxth	r1, r3
	add	r1, r4, r1, lsl #1
	ldrh	r0, [r1, #16]
.L1150:
	cmp	r0, r2
	uxth	r7, r3
	add	r3, r3, #1
	beq	.L1151
	mov	r1, #1
	bl	FtlGetLastWrittenPage
	cmn	r0, #1
	mov	r5, r0
	beq	.L1152
	ldrh	r3, [r6, #32]
	mov	r2, #0
	ldr	r0, [r6, #3836]
	movw	r8, #65535
	ldr	lr, [r6, #3176]
	mov	r9, #20
	str	r3, [fp, #-56]
	ldr	r3, [r6, #3172]
	ldrh	r7, [r6, #112]
	str	r3, [fp, #-60]
	ldrh	r3, [r6, #110]
	mov	r6, r2
	str	r3, [fp, #-68]
	add	r3, r4, #16
	mov	ip, r3
	str	r3, [fp, #-76]
.L1153:
	ldr	r1, [fp, #-56]
	uxth	r3, r2
	cmp	r1, r3
	bhi	.L1157
	ldr	r7, .L1274
	mov	r2, #0
	mov	r1, r6
	bl	FlashReadPages
	ldr	r3, [r7, #2544]
	movw	r2, #65535
	ldr	r9, [r7, #3836]
	mov	r10, r7
	str	r2, [fp, #-72]
	uxth	r2, r5
	sub	r3, r3, #1
	str	r3, [fp, #-56]
	mov	r8, r9
	mov	r3, #0
	str	r2, [fp, #-60]
.L1158:
	uxth	r2, r3
	cmp	r6, r2
	bhi	.L1163
	addeq	r3, r5, #1
	uxtheq	r3, r3
	streq	r3, [fp, #-60]
	bne	.L1161
.L1268:
	ldr	r0, [r9, #4]
	ubfx	r0, r0, #10, #16
	bl	P2V_plane
	ldrh	r3, [r7, #102]
	sub	r1, fp, #68
	ldr	r2, [fp, #-60]
	str	r0, [fp, #-68]
	ldr	r0, [fp, #-80]
	cmp	r3, r2
	ldrheq	r3, [fp, #-60]
	strheq	r3, [r4, #2]	@ movhi
	moveq	r3, #0
	strbeq	r3, [r4, #6]
	strheq	r3, [r4, #4]	@ movhi
	ldm	r1, {r1, r2, r3}
	cmp	r3, r2
	cmpeq	r1, r0
	moveq	r2, r1
	moveq	r1, r3
	beq	.L1273
	ldr	r2, [fp, #-72]
	movw	r3, #65535
	cmp	r2, r3
	bne	.L1167
	ldrb	r3, [r4, #8]	@ zero_extendqisi2
	cmp	r3, #0
	bne	.L1168
.L1167:
	ldr	r3, [r7, #4012]
	uxth	r8, r5
	uxth	r5, r5
	cmn	r3, #1
	ldreq	r3, [fp, #-56]
	streq	r3, [r7, #4012]
	ldr	r3, [fp, #-64]
	ldr	r7, .L1274
	add	r3, r3, #7
	cmp	r5, r3
	subgt	r5, r8, #7
	ldrle	r5, [fp, #-64]
	uxthgt	r5, r5
.L1171:
	cmp	r5, r8
	bhi	.L1178
	mov	r3, #0
	ldrh	r9, [r7, #32]
	ldr	r0, [r7, #3836]
	mov	r6, r3
	ldr	r1, [fp, #-76]
	movw	lr, #65535
	mov	ip, #20
	b	.L1179
.L1152:
	ldr	r3, [fp, #-64]
	cmp	r3, #0
	beq	.L1154
	movw	r2, #1804
	ldr	r1, .L1274+4
	ldr	r0, .L1274+8
	bl	sftl_printk
.L1154:
	ldr	r3, [fp, #-80]
	cmp	r3, #0
	cmpne	r7, r3
	beq	.L1155
	movw	r2, #1805
	ldr	r1, .L1274+4
	ldr	r0, .L1274+8
	bl	sftl_printk
.L1155:
	mov	r3, #0
	strh	r3, [r4, #2]	@ movhi
	b	.L1272
.L1157:
	ldrh	r3, [ip], #2
	cmp	r3, r8
	beq	.L1156
	mla	r1, r9, r6, r0
	orr	r3, r5, r3, lsl #10
	str	r3, [r1, #4]
	ldr	r3, [fp, #-68]
	mul	r3, r3, r6
	add	r10, r3, #3
	cmp	r3, #0
	movlt	r3, r10
	ldr	r10, [fp, #-60]
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
	add	r3, lr, r3
	str	r3, [r1, #12]
.L1156:
	add	r2, r2, #1
	b	.L1153
.L1163:
	ldr	r2, [r8]
	cmp	r2, #0
	bne	.L1159
	ldr	ip, [r8, #12]
	ldr	r2, [ip, #4]
	cmn	r2, #1
	beq	.L1160
	ldr	r1, [r10, #2544]
	mov	r0, r2
	str	ip, [fp, #-68]
	bl	ftl_cmp_data_ver
	ldr	ip, [fp, #-68]
	cmp	r0, #0
	addne	r2, r2, #1
	strne	r2, [r10, #2544]
.L1160:
	ldr	r2, [ip]
	cmn	r2, #1
	bne	.L1162
.L1161:
	uxth	r2, r5
	uxth	r3, r3
	str	r2, [fp, #-60]
	mov	r2, #20
	mla	r9, r2, r3, r9
	b	.L1268
.L1159:
	ldr	r2, [fp, #-60]
	str	r2, [fp, #-72]
.L1162:
	add	r3, r3, #1
	add	r8, r8, #20
	b	.L1158
.L1173:
	ldrh	r2, [r1], #2
	add	r3, r3, #1
	cmp	r2, lr
	mlane	r10, ip, r6, r0
	addne	r6, r6, #1
	orrne	r2, r5, r2, lsl #10
	uxthne	r6, r6
	strne	r2, [r10, #4]
.L1179:
	uxth	r2, r3
	cmp	r9, r2
	bhi	.L1173
	mov	r1, r6
	mov	r2, #0
	bl	FlashReadPages
	ldr	r3, [r7, #3836]
	mov	r2, #20
	movw	r1, #65535
	mla	r6, r2, r6, r3
.L1174:
	cmp	r6, r3
	addeq	r5, r5, #1
	uxtheq	r5, r5
	beq	.L1171
.L1177:
	ldr	r2, [r3]
	cmp	r2, #0
	bne	.L1168
	ldr	r2, [r3, #12]
	ldrh	r0, [r2]
	cmp	r0, r1
	beq	.L1176
	ldr	r2, [r2, #4]
	cmn	r2, #1
	strne	r2, [r7, #4012]
.L1176:
	add	r3, r3, #20
	b	.L1174
.L1178:
	mvn	r3, #0
	str	r3, [r7, #4012]
.L1168:
	ldr	r5, .L1274+12
	mov	r3, #1
	mov	r0, r5
	sub	r5, r5, #4016
	strh	r3, [r0], #-108	@ movhi
	bl	FtlMapBlkWriteDump_data
	ldr	r9, [fp, #-64]
.L1180:
	ldrh	ip, [r5, #32]
	mov	r3, #0
	ldr	r0, [r5, #3836]
	mov	r10, r3
	ldr	r1, [fp, #-76]
	movw	lr, #65535
	mov	r6, #20
.L1181:
	uxth	r2, r3
	cmp	ip, r2
	bhi	.L1183
	mov	r2, #0
	mov	r1, r10
	bl	FlashReadPages
	mov	r3, #0
.L1271:
	str	r3, [fp, #-72]
	ldrh	r3, [fp, #-72]
	cmp	r10, r3
	bhi	.L1211
	add	r9, r9, #1
	ldrh	r3, [r5, #102]
	uxth	r9, r9
	cmp	r3, r9
	bne	.L1180
	ldrh	r2, [r5, #32]
	movw	r0, #65535
	mov	r3, #0
	strh	r9, [r4, #2]	@ movhi
	strh	r3, [r4, #4]	@ movhi
.L1212:
	uxth	r1, r3
	cmp	r1, r2
	bcs	.L1267
	ldr	r1, [fp, #-76]
	ldrh	ip, [r1], #2
	cmp	ip, r0
	str	r1, [fp, #-76]
	add	r1, r3, #1
	bne	.L1272
	mov	r3, r1
	b	.L1212
.L1183:
	ldrh	r2, [r1], #2
	add	r3, r3, #1
	cmp	r2, lr
	mlane	r7, r6, r10, r0
	orrne	r2, r9, r2, lsl #10
	addne	r10, r10, #1
	uxthne	r10, r10
	strne	r2, [r7, #4]
	b	.L1181
.L1211:
	ldr	r3, [fp, #-72]
	mov	r7, #20
	ldr	r2, [r5, #3836]
	mul	r7, r7, r3
	str	r2, [fp, #-84]
	add	r8, r2, r7
	ldr	r6, [r8, #4]
	ubfx	r0, r6, #10, #16
	str	r6, [fp, #-44]
	bl	P2V_plane
	ldr	r3, [fp, #-64]
	cmp	r9, r3
	bcc	.L1185
	ldr	r2, [fp, #-80]
	moveq	r3, #1
	movne	r3, #0
	cmp	r2, r0
	movls	r3, #0
	andhi	r3, r3, #1
	cmp	r3, #0
	bne	.L1185
	ldr	r3, [fp, #-60]
	ldr	r2, [fp, #-68]
	cmp	r9, r3
	cmpeq	r2, r0
	beq	.L1186
	ldr	r2, [fp, #-84]
	ldr	r3, [r2, r7]
	cmn	r3, #1
	beq	.L1187
	ldr	r8, [r8, #12]
	movw	r3, #61589
	ldrh	r2, [r8]
	cmp	r2, r3
	beq	.L1188
.L1195:
	ldrh	r0, [r4]
.L1270:
	bl	decrement_vpc_count
.L1185:
	ldr	r3, [fp, #-72]
	add	r3, r3, #1
	b	.L1271
.L1188:
	ldr	r3, [r8, #4]
	cmn	r3, #1
	str	r3, [fp, #-56]
	beq	.L1189
	mov	r0, r3
	ldr	r1, [r5, #2544]
	bl	ftl_cmp_data_ver
	cmp	r0, #0
	ldrne	r3, [fp, #-56]
	addne	r3, r3, #1
	strne	r3, [r5, #2544]
.L1189:
	ldrh	r2, [r8]
	movw	r3, #61589
	cmp	r2, r3
	beq	.L1190
	movw	r2, #1954
	ldr	r1, .L1274+4
	ldr	r0, .L1274+8
	bl	sftl_printk
.L1190:
	ldr	r6, [r8, #8]
	sub	r1, fp, #48
	ldr	r3, [r8, #12]
	mov	r2, #0
	mov	r0, r6
	str	r3, [fp, #-52]
	bl	log2phys
	ldr	r1, [r5, #4012]
	cmn	r1, #1
	beq	.L1191
	ldr	r0, [fp, #-56]
	bl	ftl_cmp_data_ver
	cmp	r0, #0
	beq	.L1191
	ldr	r2, [fp, #-52]
	cmn	r2, #1
	beq	.L1192
	ldr	r0, [r5, #3836]
	mov	r1, #1
	add	r0, r0, r7
	str	r2, [r0, #4]
	mov	r2, #0
	ldr	r8, [r0, #12]
	bl	FlashReadPages
	ldr	r2, [r5, #3836]
	ldr	r1, [r2, r7]
	add	r3, r2, r7
	cmn	r1, #1
	bne	.L1193
.L1194:
	mvn	r3, #0
	ldrh	r0, [r4]
	str	r3, [fp, #-52]
	bl	decrement_vpc_count
.L1202:
	ldr	r7, [fp, #-52]
	cmn	r7, #1
	beq	.L1185
.L1215:
	ubfx	r0, r7, #10, #16
	bl	P2V_block_in_plane
	ldrh	r3, [r5, #40]
	mov	r6, r0
	cmp	r3, r0
	bhi	.L1207
	movw	r2, #2055
	ldr	r1, .L1274+4
	ldr	r0, .L1274+8
	bl	sftl_printk
.L1207:
	ldr	r2, [r5, #2324]
	lsl	r3, r6, #1
	ldrh	r3, [r2, r3]
	cmp	r3, #0
	beq	.L1208
	mov	r0, r6
	b	.L1270
.L1192:
	ldr	r3, [fp, #-44]
	ldr	r2, [fp, #-48]
	cmp	r2, r3
	bne	.L1195
	mov	r2, #1
	sub	r1, fp, #52
	mov	r0, r6
	bl	log2phys
	b	.L1195
.L1193:
	ldr	r1, [r8, #8]
	cmp	r6, r1
	bne	.L1194
	ldr	r1, [r8, #4]
	ldr	r0, [r5, #4012]
	str	r1, [fp, #-84]
	bl	ftl_cmp_data_ver
	cmp	r0, #0
	beq	.L1194
	ldr	r1, [fp, #-48]
	ldr	r0, [fp, #-44]
	cmp	r1, r0
	bne	.L1197
.L1200:
	ldr	r1, [fp, #-52]
	mov	r0, r6
	bl	FtlReUsePrevPpa
	b	.L1194
.L1197:
	ldr	r0, [fp, #-52]
	cmp	r1, r0
	beq	.L1194
	cmn	r1, #1
	streq	r1, [r2, r7]
	beq	.L1199
	str	r1, [r3, #4]
	mov	r2, #0
	mov	r1, #1
	mov	r0, r3
	ldr	r8, [r3, #12]
	bl	FlashReadPages
.L1199:
	ldr	r2, [r5, #3836]
	ldr	r2, [r2, r7]
	cmn	r2, #1
	beq	.L1200
	ldr	r3, [r8, #4]
	ldr	r0, [r5, #4012]
	mov	r1, r3
	bl	ftl_cmp_data_ver
	cmp	r0, #0
	beq	.L1200
	mov	r1, r3
	ldr	r0, [fp, #-84]
	bl	ftl_cmp_data_ver
	cmp	r0, #0
	beq	.L1194
	b	.L1200
.L1191:
	ldr	r3, [fp, #-44]
	ldr	r2, [fp, #-48]
	cmp	r2, r3
	beq	.L1202
	mov	r2, #1
	sub	r1, fp, #44
	mov	r0, r6
	bl	log2phys
	ldr	r7, [fp, #-48]
	cmn	r7, #1
	beq	.L1202
	ldr	r3, [fp, #-52]
	cmp	r7, r3
	beq	.L1215
	ubfx	r0, r7, #10, #16
	bl	P2V_block_in_plane
	ldr	r3, .L1274+16
	ldrh	r2, [r3]
	cmp	r2, r0
	beq	.L1206
	ldrh	r2, [r3, #48]
	cmp	r2, r0
	beq	.L1206
	ldrh	r3, [r3, #96]
	cmp	r3, r0
	bne	.L1202
.L1206:
	ldr	r0, [r5, #3836]
	mov	r2, #0
	mov	r1, #1
	str	r7, [r0, #4]
	ldr	r8, [r0, #12]
	bl	FlashReadPages
	ldr	r3, [r5, #3836]
	ldr	r3, [r3]
	cmn	r3, #1
	beq	.L1202
	ldr	r1, [r8, #4]
	ldr	r0, [fp, #-56]
	bl	ftl_cmp_data_ver
	cmp	r0, #0
	bne	.L1202
	mov	r2, #1
	sub	r1, fp, #48
	mov	r0, r6
	bl	log2phys
	b	.L1202
.L1208:
	mov	r1, r6
	ldr	r0, .L1274+20
	bl	sftl_printk
	b	.L1185
.L1187:
	ldr	r3, [r5, #4020]
	cmp	r3, #31
	addls	r2, r5, r3, lsl #2
	addls	r3, r3, #1
	strls	r3, [r5, #4020]
	strls	r6, [r2, #4024]
	ldrh	r0, [r4]
	bl	decrement_vpc_count
	ldr	r3, [r5, #4012]
	cmn	r3, #1
	ldreq	r3, [fp, #-56]
	beq	.L1269
	ldr	r2, [fp, #-56]
	cmp	r2, r3
	bcs	.L1185
	mov	r3, r2
.L1269:
	str	r3, [r5, #4012]
	b	.L1185
.L1186:
	ldrb	r3, [fp, #-68]	@ zero_extendqisi2
	ldr	r2, [fp, #-68]
	ldr	r1, [fp, #-60]
	strb	r3, [r4, #6]
	ldrh	r3, [fp, #-60]
	strh	r3, [r4, #2]	@ movhi
.L1273:
	mov	r0, r4
	bl	ftl_sb_update_avl_pages
	b	.L1267
.L1275:
	.align	2
.L1274:
	.word	.LANCHOR0
	.word	.LANCHOR1+421
	.word	.LC1
	.word	.LANCHOR0+4016
	.word	.LANCHOR0+2348
	.word	.LC106
	.size	FtlRecoverySuperblock, .-FtlRecoverySuperblock
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
	mov	r7, #0
	ldr	r3, .L1287
	movw	r1, #61589
	mov	r4, r0
	mov	r6, #7
	ldr	r2, [r3, #3284]
	mov	r8, r3
	ldr	r5, [r3, #3288]
	str	r2, [fp, #-44]
	mvn	r2, #2
	str	r5, [fp, #-40]
	str	r2, [r5, #8]
	mvn	r2, #1
	str	r2, [r5, #12]
	ldrh	r2, [r0]
	strh	r7, [r5]	@ movhi
	strh	r2, [r5, #2]	@ movhi
	ldr	r2, [r3, #3284]
	str	r1, [r2]
	ldr	r1, .L1287+4
	ldr	r2, [r3, #3284]
	str	r1, [r2, #4]
.L1277:
	subs	r6, r6, #1
	beq	.L1279
	ldrh	r3, [r4, #4]
	cmp	r3, #0
	bne	.L1278
.L1279:
	ldrh	r3, [r4]
	ldr	r1, [r8, #2324]
	ldrh	r0, [r4, #4]
	lsl	r3, r3, #1
	ldrh	r2, [r1, r3]
	sub	r2, r2, r0
	strh	r2, [r1, r3]	@ movhi
	ldrh	r3, [r8, #102]
	strh	r3, [r4, #2]	@ movhi
	mov	r3, #0
	strb	r3, [r4, #6]
	strh	r3, [r4, #4]	@ movhi
	sub	sp, fp, #32
	ldmfd	sp, {r4, r5, r6, r7, r8, fp, sp, pc}
.L1278:
	mov	r0, r4
	bl	get_new_active_ppa
	cmn	r0, #1
	str	r0, [fp, #-48]
	beq	.L1279
	ldr	r3, [r8, #2544]
	mov	r1, #1
	sub	r0, fp, #52
	str	r3, [r5, #4]
	add	r3, r3, #1
	cmn	r3, #1
	moveq	r3, r7
	str	r3, [r8, #2544]
	mov	r3, #0
	mov	r2, r3
	bl	FlashProgPages
	ldrh	r0, [r4]
	bl	decrement_vpc_count
	b	.L1277
.L1288:
	.align	2
.L1287:
	.word	.LANCHOR0
	.word	305419896
	.size	FtlSuperblockPowerLostFix, .-FtlSuperblockPowerLostFix
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
	push	{r4, r5, r6, r7, fp, ip, lr, pc}
	sub	fp, ip, #4
	ldr	r4, .L1321
	ldr	r3, [r4, #3284]
	add	r7, r4, #3264
	ldr	r6, [r4, #3288]
	str	r3, [r4, #3272]
	str	r6, [r4, #3276]
	bl	FtlBbtMemInit
	ldrh	r5, [r4, #98]
	sub	r5, r5, #1
	uxth	r5, r5
.L1290:
	ldrh	r3, [r4, #98]
	sub	r3, r3, #16
	cmp	r5, r3
	ble	.L1293
	lsl	r3, r5, #10
	mov	r2, #1
	mov	r1, r2
	mov	r0, r7
	str	r3, [r4, #3268]
	bl	FlashReadPages
	ldr	r3, [r4, #3264]
	cmn	r3, #1
	bne	.L1291
	ldr	r3, [r4, #3268]
	mov	r2, #1
	mov	r1, r2
	mov	r0, r7
	add	r3, r3, #1
	str	r3, [r4, #3268]
	bl	FlashReadPages
.L1291:
	ldr	r3, [r4, #3264]
	cmn	r3, #1
	beq	.L1292
	ldrh	r2, [r6]
	movw	r3, #61649
	cmp	r2, r3
	bne	.L1292
	ldr	r3, [r6, #4]
	strh	r5, [r4, #152]	@ movhi
	str	r3, [r4, #160]
	ldrh	r3, [r6, #8]
	strh	r3, [r4, #156]	@ movhi
.L1293:
	ldrh	r3, [r4, #152]
	movw	r2, #65535
	cmp	r3, r2
	beq	.L1307
	ldrh	r3, [r4, #156]
	cmp	r3, r2
	beq	.L1297
	lsl	r3, r3, #10
	mov	r2, #1
	mov	r1, r2
	ldr	r0, .L1321+4
	str	r3, [r4, #3268]
	bl	FlashReadPages
	ldr	r3, [r4, #3264]
	cmn	r3, #1
	beq	.L1297
	ldrh	r2, [r6]
	movw	r3, #61649
	cmp	r2, r3
	bne	.L1297
	ldr	r3, [r6, #4]
	ldr	r2, [r4, #160]
	cmp	r3, r2
	ldrhhi	r2, [r4, #156]
	strhi	r3, [r4, #160]
	ldrhhi	r3, [r6, #8]
	strhhi	r2, [r4, #152]	@ movhi
	strhhi	r3, [r4, #156]	@ movhi
.L1297:
	ldr	r7, .L1321+4
	mov	r1, #1
	ldrh	r0, [r4, #152]
	bl	FtlGetLastWrittenPage
	sxth	r5, r0
	add	r0, r0, #1
	strh	r0, [r4, #154]	@ movhi
.L1299:
	cmp	r5, #0
	bge	.L1302
	mov	r2, #254
	ldr	r1, .L1321+8
	ldr	r0, .L1321+12
	bl	sftl_printk
.L1301:
	ldrh	r3, [r6, #10]
	ldrh	r0, [r6, #12]
	strh	r3, [r4, #158]	@ movhi
	movw	r3, #65535
	cmp	r0, r3
	beq	.L1304
	ldr	r2, [r4, #28]
	cmp	r0, r2
	beq	.L1304
	ldrh	r3, [r4, #42]
	lsr	r3, r3, #2
	cmp	r0, r3
	cmpcc	r2, r3
	bcs	.L1304
	bl	FtlSysBlkNumInit
.L1304:
	ldr	r6, .L1321+16
	mov	r5, #0
	add	r7, r6, #3728
.L1305:
	ldrh	r3, [r4, #54]
	cmp	r5, r3
	bcc	.L1306
	mov	r0, #0
	ldmfd	sp, {r4, r5, r6, r7, fp, sp, pc}
.L1292:
	sub	r5, r5, #1
	uxth	r5, r5
	b	.L1290
.L1302:
	ldrh	r3, [r4, #152]
	mov	r2, #1
	mov	r1, r2
	mov	r0, r7
	orr	r3, r5, r3, lsl #10
	str	r3, [r4, #3268]
	ldr	r3, [r4, #3284]
	str	r3, [r4, #3272]
	bl	FlashReadPages
	ldr	r3, [r4, #3264]
	cmn	r3, #1
	beq	.L1300
	ldrh	r2, [r6]
	movw	r3, #61649
	cmp	r2, r3
	beq	.L1301
.L1300:
	sub	r5, r5, #1
	sxth	r5, r5
	b	.L1299
.L1306:
	ldrh	r2, [r7]
	ldr	r1, [r4, #3272]
	ldr	r0, [r6, #4]!
	lsl	r2, r2, #2
	mla	r1, r5, r2, r1
	add	r5, r5, #1
	bl	ftl_memcpy
	b	.L1305
.L1307:
	mvn	r0, #0
	ldmfd	sp, {r4, r5, r6, r7, fp, sp, pc}
.L1322:
	.align	2
.L1321:
	.word	.LANCHOR0
	.word	.LANCHOR0+3264
	.word	.LANCHOR1+443
	.word	.LC1
	.word	.LANCHOR0+176
	.size	FtlLoadBbt, .-FtlLoadBbt
	.align	2
	.global	FtlMakeBbt
	.syntax unified
	.arm
	.fpu softvfp
	.type	FtlMakeBbt, %function
FtlMakeBbt:
	@ args = 0, pretend = 0, frame = 4
	@ frame_needed = 1, uses_anonymous_args = 0
	mov	ip, sp
	push	{r4, r5, r6, r7, r8, r9, r10, fp, ip, lr, pc}
	sub	fp, ip, #4
	sub	sp, sp, #4
	mov	r7, #0
	ldr	r8, .L1342
	bl	FtlBbtMemInit
	bl	FtlLoadFactoryBbt
	sub	r5, r8, #180
	sub	r9, r8, #18
	mov	r4, r5
.L1324:
	ldrh	r3, [r5, #54]
	cmp	r7, r3
	bcc	.L1330
	mov	r0, #0
.L1331:
	ldrh	r2, [r4, #114]
	uxth	r3, r0
	add	r5, r0, #1
	cmp	r2, r3
	bhi	.L1332
	ldrh	r5, [r4, #164]
	movw	r6, #65535
	sub	r5, r5, #1
	uxth	r5, r5
.L1333:
	ldrh	r3, [r4, #164]
	sub	r3, r3, #48
	cmp	r5, r3
	ble	.L1337
	mov	r0, r5
	bl	FtlBbmIsBadBlock
	cmp	r0, #1
	beq	.L1334
	mov	r0, r5
	bl	FlashTestBlk
	cmp	r0, #0
	beq	.L1335
	mov	r0, r5
	bl	FtlBbmMapBadBlock
.L1334:
	sub	r5, r5, #1
	uxth	r5, r5
	b	.L1333
.L1330:
	ldrh	r3, [r9, #2]!
	movw	r2, #65535
	ldr	r0, [r5, #3284]
	ldr	r10, [r5, #3288]
	cmp	r3, r2
	str	r0, [r5, #3272]
	str	r10, [r5, #3276]
	beq	.L1325
	ldrh	ip, [r5, #98]
	mov	r2, #1
	mov	r1, r2
	ldr	r0, .L1342+4
	mla	ip, r7, ip, r3
	lsl	r3, ip, #10
	str	ip, [fp, #-44]
	str	r3, [r5, #3268]
	bl	FlashReadPages
	ldrh	r2, [r5, #98]
	ldr	r1, [r5, #3272]
	ldr	r0, [r8]
	add	r2, r2, #7
	asr	r2, r2, #3
	bl	ftl_memcpy
	ldr	ip, [fp, #-44]
.L1326:
	uxth	r0, ip
	add	r7, r7, #1
	add	r8, r8, #4
	bl	FtlBbmMapBadBlock
	b	.L1324
.L1325:
	mov	r1, r7
	bl	FlashGetBadBlockList
	ldr	r1, [r8]
	ldr	r0, [r5, #3272]
	bl	FtlBbt2Bitmap
	ldrh	r6, [r5, #98]
.L1328:
	sub	r6, r6, #1
	uxth	r6, r6
.L1327:
	ldrh	r0, [r4, #98]
	smlabb	r0, r0, r7, r6
	uxth	r0, r0
	bl	FtlBbmIsBadBlock
	cmp	r0, #1
	beq	.L1328
	mov	r1, #16
	ldr	r0, [r4, #3288]
	strh	r6, [r9]	@ movhi
	bl	__memzero
	mov	r1, #4096
	ldr	r0, [r4, #3284]
	bl	__memzero
	ldr	r3, .L1342+8
	strh	r3, [r10]	@ movhi
	mov	r3, #0
	str	r3, [r10, #4]
	ldrh	r3, [r9]
	ldrh	ip, [r4, #98]
	strh	r3, [r10, #2]	@ movhi
	ldrh	r3, [r9]
	ldr	r1, [r8]
	ldr	r0, [r4, #3272]
	mla	ip, r7, ip, r3
	lsl	r3, ip, #10
	str	ip, [fp, #-44]
	str	r3, [r4, #3268]
	ldr	r3, .L1342+12
	ldrh	r2, [r3]
	lsl	r2, r2, #2
	bl	ftl_memcpy
	mov	r2, #1
	ldr	r0, .L1342+4
	mov	r1, r2
	bl	FlashEraseBlocks
	mov	r3, #1
	ldr	r0, .L1342+4
	mov	r2, r3
	mov	r1, r3
	bl	FlashProgPages
	ldr	r3, [r4, #3264]
	ldr	ip, [fp, #-44]
	cmn	r3, #1
	bne	.L1326
	uxth	r0, ip
	bl	FtlBbmMapBadBlock
	b	.L1327
.L1332:
	uxth	r0, r0
	bl	FtlBbmMapBadBlock
	mov	r0, r5
	b	.L1331
.L1335:
	ldrh	r3, [r4, #152]
	cmp	r3, r6
	strheq	r5, [r4, #152]	@ movhi
	beq	.L1334
.L1336:
	strh	r5, [r4, #156]	@ movhi
.L1337:
	ldrh	r3, [r4, #152]
	mov	r5, #0
	ldr	r0, [r4, #3804]
	mov	r1, #1
	str	r5, [r4, #160]
	mov	r2, #2
	strh	r5, [r4, #154]	@ movhi
	lsl	r3, r3, #10
	str	r3, [r0, #4]
	ldrh	r3, [r4, #156]
	lsl	r3, r3, #10
	str	r3, [r0, #24]
	bl	FlashEraseBlocks
	ldrh	r0, [r4, #152]
	bl	FtlBbmMapBadBlock
	ldrh	r0, [r4, #156]
	bl	FtlBbmMapBadBlock
	bl	FtlBbmTblFlush
	ldr	r3, [r4, #160]
	ldrh	r2, [r4, #156]
	strh	r5, [r4, #154]	@ movhi
	add	r3, r3, #1
	str	r3, [r4, #160]
	ldrh	r3, [r4, #152]
	strh	r2, [r4, #152]	@ movhi
	strh	r3, [r4, #156]	@ movhi
	bl	FtlBbmTblFlush
	mov	r0, r5
	ldmib	sp, {r4, r5, r6, r7, r8, r9, r10, fp, sp, pc}
.L1343:
	.align	2
.L1342:
	.word	.LANCHOR0+180
	.word	.LANCHOR0+3264
	.word	-3872
	.word	.LANCHOR0+3904
	.size	FtlMakeBbt, .-FtlMakeBbt
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
	ldr	r4, .L1353
	mov	r5, r1
	str	r2, [fp, #-128]
	add	r2, r0, r1
	ldrh	r3, [r4, #96]
	cmp	r2, r3
	mvnhi	r8, #0
	bhi	.L1344
	ldrh	r6, [r4, #108]
	mov	r8, #0
	lsr	r6, r0, r6
	lsl	r10, r6, #2
.L1346:
	cmp	r5, #0
	bne	.L1351
.L1344:
	mov	r0, r8
	sub	sp, fp, #40
	ldmfd	sp, {r4, r5, r6, r7, r8, r9, r10, fp, sp, pc}
.L1351:
	ldr	r3, [r4, #3888]
	mov	r0, r7
	ldrh	r2, [r4, #58]
	ldr	ip, [r3, r10]
	mov	r1, r2
	str	r2, [fp, #-136]
	str	ip, [fp, #-140]
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
	beq	.L1348
	ldr	r2, [r4, #3852]
	sub	r0, fp, #124
	str	ip, [fp, #-120]
	str	r2, [fp, #-116]
	sub	r2, fp, #104
	str	r2, [fp, #-112]
	mov	r2, #1
	mov	r1, r2
	bl	FlashReadPages
.L1349:
	ldr	r3, [fp, #-132]
	lsl	ip, r9, #9
	ldr	r0, [r4, #3852]
	sub	r5, r5, r9
	mov	r2, ip
	ldr	r1, [fp, #-128]
	str	ip, [fp, #-136]
	add	r7, r7, r9
	add	r10, r10, #4
	add	r0, r0, r3, lsl #9
	bl	ftl_memcpy
	mov	r1, r6
	ldr	r2, [r4, #3852]
	ldr	r0, .L1353+4
	add	r6, r6, #1
	bl	FtlMapWritePage
	ldr	r3, [fp, #-128]
	cmn	r0, #1
	ldr	ip, [fp, #-136]
	mvneq	r8, #0
	add	r3, r3, ip
	str	r3, [fp, #-128]
	b	.L1346
.L1348:
	ldrh	r2, [r4, #110]
	ldr	r0, [r4, #3852]
	bl	ftl_memset
	b	.L1349
.L1354:
	.align	2
.L1353:
	.word	.LANCHOR0
	.word	.LANCHOR0+3956
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
	ldr	r3, .L1357
	ldr	r2, .L1357+4
	ldr	r1, [r3, #2636]
	cmp	r1, r2
	ldmfdne	sp, {fp, sp, pc}
	ldr	r2, .L1357+8
	mov	r1, #1
	mov	r0, #0
	str	r2, [r3, #2640]
	ldr	r2, [r3, #2532]
	str	r2, [r3, #2724]
	ldr	r2, [r3, #2536]
	str	r2, [r3, #2728]
	ldr	r2, [r3, #2528]
	str	r2, [r3, #2644]
	ldr	r2, [r3, #2516]
	str	r2, [r3, #2648]
	ldr	r2, [r3, #2508]
	str	r2, [r3, #2652]
	ldr	r2, [r3, #2524]
	str	r2, [r3, #2656]
	ldr	r2, [r3, #2552]
	str	r2, [r3, #2664]
	ldr	r2, [r3, #2560]
	str	r2, [r3, #2668]
	ldr	r2, [r3, #2512]
	str	r2, [r3, #2672]
	ldr	r2, [r3, #2520]
	str	r2, [r3, #2676]
	ldr	r2, [r3, #2564]
	str	r2, [r3, #2680]
	ldr	r2, [r3, #2568]
	str	r2, [r3, #2684]
	ldr	r2, .L1357+12
	bl	FtlVendorPartWrite
	ldmfd	sp, {fp, sp, pc}
.L1358:
	.align	2
.L1357:
	.word	.LANCHOR0
	.word	1179929683
	.word	1342177349
	.word	.LANCHOR0+2636
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
	ldr	r2, .L1364
	ldrh	r3, [r2]
	cmp	r3, #31
	addls	r3, r3, #1
	movhi	r3, #32
	strhls	r3, [r2]	@ movhi
	movls	r3, #1
	cmp	r0, #0
	ldr	r2, .L1364+4
	bne	.L1361
	ldr	r1, [r2, #3864]
	ldr	r0, [r1, #20]
	ldr	r1, [r1, #16]
	add	r3, r3, r0
	cmp	r1, r3
	bcc	.L1362
.L1361:
	ldr	r3, [r2, #3864]
	mov	r0, #64
	ldr	r1, [r3, #16]
	str	r1, [r3, #20]
	ldr	r1, .L1364+8
	str	r1, [r3]
	ldr	r3, .L1364+12
	ldr	r2, [r2, #3864]
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
.L1362:
	mov	r0, #0
	ldmfd	sp, {fp, sp, pc}
.L1365:
	.align	2
.L1364:
	.word	.LANCHOR3-4032
	.word	.LANCHOR0
	.word	1112818501
	.word	.LANCHOR0+3856
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
	mov	r9, r2
	ldr	r3, .L1376
	mov	r6, r1
	add	r1, r0, r1
	mov	r7, r0
	ldrh	r2, [r3, #96]
	mov	r10, r3
	cmp	r1, r2
	mvnhi	r8, #0
	bhi	.L1366
	ldrh	r5, [r3, #108]
	mov	r8, #0
	lsr	r5, r0, r5
	lsl	r3, r5, #2
	str	r3, [fp, #-128]
.L1368:
	cmp	r6, #0
	bne	.L1374
.L1366:
	mov	r0, r8
	sub	sp, fp, #40
	ldmfd	sp, {r4, r5, r6, r7, r8, r9, r10, fp, sp, pc}
.L1374:
	ldr	r3, [fp, #-128]
	mov	r0, r7
	ldr	r2, [r10, #3888]
	ldrh	r4, [r10, #58]
	ldr	ip, [r2, r3]
	mov	r1, r4
	str	ip, [fp, #-136]
	bl	__umodsi3
	sub	r4, r4, r0
	ldr	ip, [fp, #-136]
	uxth	r4, r4
	str	r0, [fp, #-132]
	cmp	r6, r4
	uxthcc	r4, r6
	cmp	ip, #0
	lsl	r3, r4, #9
	str	r3, [fp, #-136]
	beq	.L1370
	ldr	r2, [r10, #3852]
	sub	r0, fp, #124
	str	ip, [fp, #-120]
	str	ip, [fp, #-136]
	str	r2, [fp, #-116]
	sub	r2, fp, #104
	str	r2, [fp, #-112]
	mov	r2, #1
	mov	r1, r2
	bl	FlashReadPages
	ldr	r2, [fp, #-124]
	ldr	ip, [fp, #-136]
	cmn	r2, #1
	ldr	r2, [r10, #3264]
	mvneq	r8, #0
	cmp	r2, #256
	bne	.L1372
	mov	r2, ip
	mov	r1, r5
	ldr	r0, .L1376+4
	bl	sftl_printk
	ldr	r2, [r10, #3852]
	mov	r1, r5
	ldr	r0, .L1376+8
	bl	FtlMapWritePage
.L1372:
	ldr	r1, [r10, #3852]
	lsl	r2, r4, #9
	ldr	r3, [fp, #-132]
	mov	r0, r9
	add	r1, r1, r3, lsl #9
	bl	ftl_memcpy
.L1373:
	ldr	r3, [fp, #-128]
	add	r5, r5, #1
	sub	r6, r6, r4
	add	r7, r7, r4
	add	r9, r9, r4, lsl #9
	add	r3, r3, #4
	str	r3, [fp, #-128]
	b	.L1368
.L1370:
	lsl	r2, r4, #9
	mov	r1, ip
	mov	r0, r9
	bl	ftl_memset
	b	.L1373
.L1377:
	.align	2
.L1376:
	.word	.LANCHOR0
	.word	.LC107
	.word	.LANCHOR0+3956
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
	ldr	r4, .L1380
	mov	r0, #64
	add	r5, r4, #3856
	ldr	r2, [r4, #3864]
	ldrh	r1, [r5]
	bl	FtlVendorPartRead
	ldr	r3, [r4, #3864]
	ldr	r2, [r3]
	ldr	r3, .L1380+4
	cmp	r2, r3
	beq	.L1379
	ldr	r1, .L1380+8
	ldr	r0, .L1380+12
	bl	sftl_printk
	ldrh	r2, [r5]
	mov	r1, #0
	ldr	r0, [r4, #3864]
	lsl	r2, r2, #9
	bl	ftl_memset
.L1379:
	mov	r0, #0
	ldmfd	sp, {r4, r5, fp, sp, pc}
.L1381:
	.align	2
.L1380:
	.word	.LANCHOR0
	.word	1112818501
	.word	.LC108
	.word	.LC71
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
	ldr	r4, .L1385
	mov	r1, #1
	mov	r0, #0
	ldr	r2, .L1385+4
	bl	FtlVendorPartRead
	ldr	r5, .L1385+8
	ldr	r3, [r4, #2636]
	cmp	r3, r5
	beq	.L1383
	mov	r1, #512
	ldr	r0, .L1385+4
	bl	__memzero
	str	r5, [r4, #2636]
.L1383:
	ldr	r3, [r4, #2636]
	cmp	r3, r5
	bne	.L1384
	ldr	r3, [r4, #2724]
	str	r3, [r4, #2532]
	ldr	r3, [r4, #2728]
	str	r3, [r4, #2536]
	ldr	r3, [r4, #2644]
	str	r3, [r4, #2528]
	ldr	r3, [r4, #2648]
	str	r3, [r4, #2516]
	ldr	r3, [r4, #2652]
	str	r3, [r4, #2508]
	ldr	r3, [r4, #2656]
	str	r3, [r4, #2524]
	ldr	r3, [r4, #2664]
	str	r3, [r4, #2552]
	ldr	r3, [r4, #2668]
	str	r3, [r4, #2560]
	ldr	r3, [r4, #2672]
	str	r3, [r4, #2512]
	ldr	r3, [r4, #2676]
	str	r3, [r4, #2520]
	ldr	r3, [r4, #2680]
	str	r3, [r4, #2564]
	ldr	r3, [r4, #2684]
	str	r3, [r4, #2568]
.L1384:
	ldrh	r2, [r4, #92]
	ldr	r3, [r4, #2552]
	ldr	r0, [r4, #2548]
	ldrh	r1, [r4, #40]
	mla	r0, r0, r2, r3
	bl	__udivsi3
	str	r0, [r4, #2556]
	ldmfd	sp, {r4, r5, fp, sp, pc}
.L1386:
	.align	2
.L1385:
	.word	.LANCHOR0
	.word	.LANCHOR0+2636
	.word	1179929683
	.size	Ftl_load_ext_data, .-Ftl_load_ext_data
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
	ldr	r4, .L1396
	mov	r6, #0
	mov	r1, #255
	ldr	r10, .L1396+4
	ldr	r3, [r4, #3284]
	add	r5, r4, #2576
	ldr	r8, [r4, #3288]
	sub	r7, r5, #308
	add	r9, r4, #3264
	str	r3, [r4, #3272]
	ldrh	r3, [r5]
	str	r8, [r4, #3276]
	str	r6, [r8, #12]
	strh	r3, [r8, #2]	@ movhi
	ldr	r3, .L1396+8
	strh	r3, [r8]	@ movhi
	ldr	r3, [r4, #2584]
	str	r6, [r8, #8]
	ldrh	r2, [r5, #-226]
	str	r3, [r8, #4]
	ldr	r3, .L1396+12
	str	r3, [r4, #2268]
	ldr	r3, .L1396+16
	str	r3, [r4, #2272]
	ldrh	r3, [r5, #6]
	strh	r3, [r7, #8]	@ movhi
	ldrh	r3, [r4, #54]
	strb	r3, [r4, #2278]
	movw	r3, #2348
	ldrh	r3, [r4, r3]
	strh	r3, [r7, #14]	@ movhi
	ldrb	r3, [r4, #2354]	@ zero_extendqisi2
	orr	r3, r3, r2, lsl #6
	ldrh	r2, [r5, #-178]
	strh	r3, [r7, #16]	@ movhi
	ldrb	r3, [r4, #2356]	@ zero_extendqisi2
	strb	r3, [r4, #2279]
	movw	r3, #2396
	ldrh	r3, [r4, r3]
	strh	r3, [r7, #18]	@ movhi
	ldrb	r3, [r4, #2402]	@ zero_extendqisi2
	orr	r3, r3, r2, lsl #6
	strh	r3, [r7, #20]	@ movhi
	ldrb	r3, [r4, #2404]	@ zero_extendqisi2
	strb	r3, [r4, #2280]
	movw	r3, #2444
	ldrh	r3, [r4, r3]
	strh	r3, [r7, #22]	@ movhi
	ldrh	r2, [r5, #-130]
	ldrb	r3, [r4, #2450]	@ zero_extendqisi2
	ldr	r0, [r4, #3272]
	orr	r3, r3, r2, lsl #6
	ldrh	r2, [r4, #110]
	strh	r3, [r7, #24]	@ movhi
	ldrb	r3, [r4, #2452]	@ zero_extendqisi2
	strb	r3, [r4, #2281]
	ldr	r3, [r4, #2548]
	str	r3, [r4, #2300]
	ldr	r3, [r4, #2540]
	str	r3, [r4, #2308]
	ldr	r3, [r4, #2544]
	str	r3, [r4, #2304]
	movw	r3, #3202
	ldrh	r3, [r4, r3]
	strh	r3, [r7, #44]	@ movhi
	movw	r3, #3204
	ldrh	r3, [r4, r3]
	strh	r3, [r7, #46]	@ movhi
	bl	ftl_memset
	mov	r1, r7
	mov	r2, #48
	ldr	r0, [r4, #3272]
	movw	r7, #65535
	bl	ftl_memcpy
	ldrh	r2, [r4, #40]
	ldr	r0, [r4, #3272]
	ldr	r1, [r4, #2324]
	lsl	r2, r2, #1
	add	r0, r0, #48
	bl	ftl_memcpy
	ldrh	r0, [r4, #40]
	ldr	r3, [r4, #3272]
	ldr	r1, [r4, #24]
	lsr	r2, r0, #3
	add	r0, r0, #24
	lsl	r0, r0, #1
	add	r2, r2, #4
	bic	r0, r0, #3
	add	r0, r3, r0
	bl	ftl_memcpy
	mov	r0, r6
	bl	FtlUpdateVaildLpn
.L1388:
	ldr	r3, [r4, #3284]
	mov	r0, r9
	ldrh	r2, [r5]
	str	r3, [r4, #3272]
	ldr	r3, [r4, #3288]
	str	r3, [r4, #3276]
	ldrh	r3, [r5, #2]
	orr	r3, r3, r2, lsl #10
	str	r3, [r4, #3268]
	mov	r3, #1
	mov	r2, r3
	mov	r1, r3
	bl	FlashProgPages
	ldrh	r3, [r4, #104]
	ldrh	r2, [r5, #2]
	sub	r3, r3, #1
	cmp	r2, r3
	blt	.L1389
	ldrh	r3, [r5]
	ldrh	r7, [r5, #4]
	strh	r3, [r5, #4]	@ movhi
	mov	r3, #0
	strh	r3, [r5, #2]	@ movhi
	bl	FtlFreeSysBlkQueueOut
	ldr	r3, [r4, #2540]
	strh	r0, [r5]	@ movhi
	add	r2, r3, #1
	str	r3, [r4, #2584]
	str	r2, [r4, #2540]
	lsl	r2, r0, #10
	str	r2, [r4, #3268]
	str	r3, [r8, #4]
	mov	r3, #1
	strh	r0, [r8, #2]	@ movhi
	mov	r2, r3
	mov	r1, r3
	mov	r0, r9
	bl	FlashProgPages
.L1389:
	ldrh	r3, [r5, #2]
	ldr	r2, [r4, #3264]
	add	r3, r3, #1
	uxth	r3, r3
	cmn	r2, #1
	strh	r3, [r5, #2]	@ movhi
	bne	.L1390
	cmp	r3, #1
	bne	.L1391
	movw	r2, #1169
	mov	r1, r10
	ldr	r0, .L1396+20
	bl	sftl_printk
.L1391:
	ldrh	r3, [r5, #2]
	add	r6, r6, #1
	uxth	r6, r6
	cmp	r3, #1
	ldrheq	r3, [r4, #104]
	subeq	r3, r3, #1
	strheq	r3, [r5, #2]	@ movhi
	cmp	r6, #3
	bls	.L1388
	mov	r2, r6
	ldr	r1, [r4, #3268]
	ldr	r0, .L1396+24
	bl	sftl_printk
.L1394:
	b	.L1394
.L1390:
	cmp	r2, #256
	cmpne	r3, #1
	beq	.L1388
	movw	r3, #65535
	cmp	r7, r3
	beq	.L1395
	mov	r1, #1
	mov	r0, r7
	bl	FtlFreeSysBlkQueueIn
.L1395:
	mov	r0, #0
	ldmfd	sp, {r4, r5, r6, r7, r8, r9, r10, fp, sp, pc}
.L1397:
	.align	2
.L1396:
	.word	.LANCHOR0
	.word	.LANCHOR1+454
	.word	-3932
	.word	1179929683
	.word	1342177349
	.word	.LC1
	.word	.LC109
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
	ldr	r3, .L1401
	ldr	r3, [r3]
	cmp	r3, #1
	bne	.L1400
	bl	FtlSysFlush
.L1400:
	mov	r0, #0
	ldmfd	sp, {fp, sp, pc}
.L1402:
	.align	2
.L1401:
	.word	.LANCHOR2
	.size	sftl_deinit, .-sftl_deinit
	.align	2
	.global	FtlDiscard
	.syntax unified
	.arm
	.fpu softvfp
	.type	FtlDiscard, %function
FtlDiscard:
	@ args = 0, pretend = 0, frame = 8
	@ frame_needed = 1, uses_anonymous_args = 0
	mov	ip, sp
	push	{r4, r5, r6, r7, r8, fp, ip, lr, pc}
	sub	fp, ip, #4
	sub	sp, sp, #8
	add	r2, r0, r1
	ldr	r6, .L1418
	mov	r8, r0
	mov	r5, r1
	ldr	r3, [r6, #140]
	cmp	r2, r3
	mvnhi	r0, #0
	bhi	.L1403
	cmp	r1, #31
	bhi	.L1405
.L1410:
	mov	r0, #0
.L1403:
	sub	sp, fp, #32
	ldmfd	sp, {r4, r5, r6, r7, r8, fp, sp, pc}
.L1405:
	ldrh	r4, [r6, #58]
	mov	r1, r4
	bl	__udivsi3
	smulbb	r3, r0, r4
	mov	r7, r0
	sub	r8, r8, r3
	uxth	r8, r8
	cmp	r8, #0
	beq	.L1406
	sub	r4, r4, r8
	add	r7, r0, #1
	cmp	r4, r5
	movcs	r4, r5
	uxth	r4, r4
	sub	r5, r5, r4
.L1406:
	ldr	r4, .L1418+4
	mvn	r3, #0
	str	r3, [fp, #-36]
.L1407:
	ldrh	r3, [r6, #58]
	cmp	r5, r3
	bcs	.L1409
	ldr	r3, .L1418+4
	ldr	r2, [r3, #-4028]
	cmp	r2, #32
	bls	.L1410
	mov	r4, #0
	str	r4, [r3, #-4028]
	bl	l2p_flush
	bl	FtlVpcTblFlush
	b	.L1410
.L1409:
	mov	r2, #0
	sub	r1, fp, #40
	mov	r0, r7
	bl	log2phys
	ldr	r3, [fp, #-40]
	cmn	r3, #1
	beq	.L1408
	ldr	r3, [r4, #-4028]
	mov	r2, #1
	sub	r1, fp, #36
	mov	r0, r7
	add	r3, r3, #1
	str	r3, [r4, #-4028]
	ldr	r3, [r6, #2512]
	add	r3, r3, #1
	str	r3, [r6, #2512]
	bl	log2phys
	ldr	r0, [fp, #-40]
	ubfx	r0, r0, #10, #16
	bl	P2V_block_in_plane
	bl	decrement_vpc_count
.L1408:
	ldrh	r3, [r6, #58]
	add	r7, r7, #1
	sub	r5, r5, r3
	b	.L1407
.L1419:
	.align	2
.L1418:
	.word	.LANCHOR0
	.word	.LANCHOR3
	.size	FtlDiscard, .-FtlDiscard
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
	ldr	r4, .L1432
	ldr	r1, .L1432+4
	ldr	r0, .L1432+8
	bl	sftl_printk
	ldrh	r2, [r4, #42]
	mov	r1, #0
	ldr	r0, [r4, #3868]
	lsl	r2, r2, #1
	bl	ftl_memset
.L1421:
	ldr	r3, [r4, #2504]
	cmp	r5, r3
	bcc	.L1423
	ldr	r9, .L1432+12
	mov	r6, #0
	ldr	r10, .L1432+16
.L1424:
	ldrh	r3, [r4, #40]
	uxth	r5, r6
	cmp	r3, r5
	bhi	.L1428
	ldmib	sp, {r4, r5, r6, r7, r8, r9, r10, fp, sp, pc}
.L1423:
	mov	r2, #0
	sub	r1, fp, #44
	mov	r0, r5
	bl	log2phys
	ldr	r0, [fp, #-44]
	cmn	r0, #1
	beq	.L1422
	ubfx	r0, r0, #10, #16
	bl	P2V_block_in_plane
	ldr	r2, [r4, #3868]
	lsl	r0, r0, #1
	ldrh	r3, [r2, r0]
	add	r3, r3, #1
	strh	r3, [r2, r0]	@ movhi
.L1422:
	add	r5, r5, #1
	b	.L1421
.L1428:
	uxth	r8, r6
	ldr	r3, [r4, #2324]
	movw	r1, #65535
	lsl	r7, r8, #1
	ldrh	r2, [r3, r7]
	ldr	r3, [r4, #3868]
	ldrh	r3, [r3, r7]
	cmp	r2, r1
	cmpne	r2, r3
	beq	.L1426
	mov	r1, r8
	mov	r0, r9
	bl	sftl_printk
	ldrh	r3, [r10]
	cmp	r3, r5
	beq	.L1426
	ldr	r3, .L1432+20
	ldrh	r2, [r3]
	cmp	r2, r5
	beq	.L1426
	ldrh	r3, [r3, #-48]
	cmp	r3, r5
	beq	.L1426
	ldr	r3, [r4, #3868]
	mov	r0, r8
	ldrh	r2, [r3, r7]
	ldr	r3, [r4, #2324]
	strh	r2, [r3, r7]	@ movhi
	bl	update_vpc_list
	bl	l2p_flush
	bl	FtlVpcTblFlush
.L1426:
	add	r6, r6, #1
	b	.L1424
.L1433:
	.align	2
.L1432:
	.word	.LANCHOR0
	.word	.LANCHOR1+469
	.word	.LC97
	.word	.LC110
	.word	.LANCHOR0+2348
	.word	.LANCHOR0+2444
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
	push	{r4, r5, r6, r7, fp, ip, lr, pc}
	sub	fp, ip, #4
	ldr	r5, .L1444
	mov	r7, r0
	ldrh	r4, [r0]
	ldrh	r3, [r5, #40]
	cmp	r3, r4
	bcs	.L1435
	movw	r2, #2750
	ldr	r1, .L1444+4
	ldr	r0, .L1444+8
	bl	sftl_printk
.L1435:
	movw	r3, #65535
	cmp	r4, r3
	beq	.L1436
	ldr	r2, [r5, #2324]
	lsl	r3, r4, #1
	mov	r0, r4
	ldrh	r3, [r2, r3]
	cmp	r3, #0
	beq	.L1437
	bl	INSERT_DATA_LIST
.L1436:
	mov	r3, #1
	movw	r2, #65535
	strb	r3, [r7, #8]
	ldr	r3, .L1444+12
	ldrh	r0, [r3]
	mov	r6, r3
	cmp	r0, r2
	beq	.L1438
	cmp	r4, r0
	bne	.L1439
	ldr	r2, [r5, #2324]
	lsl	r3, r0, #1
	ldrh	r3, [r2, r3]
	cmp	r3, #0
	beq	.L1440
.L1439:
	bl	update_vpc_list
.L1440:
	mvn	r3, #0
	strh	r3, [r6]	@ movhi
.L1438:
	mov	r0, r7
	bl	allocate_data_superblock
	bl	l2p_flush
	mov	r0, #0
	bl	FtlEctTblFlush
	bl	FtlVpcTblFlush
	mov	r0, #0
	ldmfd	sp, {r4, r5, r6, r7, fp, sp, pc}
.L1437:
	bl	INSERT_FREE_LIST
	b	.L1436
.L1445:
	.align	2
.L1444:
	.word	.LANCHOR0
	.word	.LANCHOR1+490
	.word	.LC1
	.word	.LANCHOR0+4000
	.size	allocate_new_data_superblock, .-allocate_new_data_superblock
	.align	2
	.global	FtlProgPages
	.syntax unified
	.arm
	.fpu softvfp
	.type	FtlProgPages, %function
FtlProgPages:
	@ args = 0, pretend = 0, frame = 8
	@ frame_needed = 1, uses_anonymous_args = 0
	mov	ip, sp
	push	{r4, r5, r6, r7, r8, r9, r10, fp, ip, lr, pc}
	sub	fp, ip, #4
	sub	sp, sp, #8
	mov	r7, #0
	ldr	r6, .L1460
	mov	r4, r3
	ldr	r10, .L1460+4
	mov	r2, #0
	ldrb	r3, [r3, #9]	@ zero_extendqisi2
	mov	r5, r0
	mov	r9, r1
	bl	FlashProgPages
.L1447:
	cmp	r7, r9
	beq	.L1454
	mov	r8, #0
	b	.L1455
.L1450:
	ldr	r0, [r5, #4]
	ubfx	r0, r0, #10, #16
	bl	P2V_block_in_plane
	ldrh	r3, [r4]
	cmp	r3, r0
	bne	.L1448
	ldr	r1, [r6, #2324]
	lsl	r3, r3, #1
	ldrh	r0, [r4, #4]
	ldrh	r2, [r1, r3]
	sub	r2, r2, r0
	strh	r2, [r1, r3]	@ movhi
	ldrh	r3, [r6, #102]
	strb	r8, [r4, #6]
	strh	r8, [r4, #4]	@ movhi
	strh	r3, [r4, #2]	@ movhi
.L1448:
	ldrh	r3, [r4, #4]
	cmp	r3, #0
	bne	.L1449
	mov	r0, r4
	bl	allocate_new_data_superblock
.L1449:
	ldr	r3, [r6, #2732]
	add	r3, r3, #1
	str	r3, [r6, #2732]
	ldr	r0, [r5, #4]
	ubfx	r0, r0, #10, #16
	bl	FtlGcMarkBadPhyBlk
	mov	r0, r4
	bl	get_new_active_ppa
	mov	r2, #0
	str	r0, [r5, #4]
	str	r0, [fp, #-44]
	mov	r1, #1
	ldrb	r3, [r4, #9]	@ zero_extendqisi2
	mov	r0, r5
	bl	FlashProgPages
.L1455:
	ldr	r3, [r5]
	cmn	r3, #1
	beq	.L1450
	ldrb	r2, [r4, #6]	@ zero_extendqisi2
	ldrh	r3, [r6, #32]
	cmp	r2, r3
	bcc	.L1451
	mov	r2, #936
	mov	r1, r10
	ldr	r0, .L1460+8
	bl	sftl_printk
.L1451:
	ldr	r3, [r5, #4]
	sub	r1, fp, #40
	mov	r2, #1
	ldr	r0, [r5, #16]
	str	r3, [r1, #-4]!
	bl	log2phys
	ldr	r3, [r5, #12]
	ldr	r3, [r3, #12]
	ubfx	r0, r3, #10, #16
	str	r3, [fp, #-48]
	bl	P2V_block_in_plane
	ldr	r3, [fp, #-48]
	mov	r8, r0
	cmn	r3, #1
	beq	.L1452
	ldr	r2, [r6, #2324]
	lsl	r3, r0, #1
	ldrh	r2, [r2, r3]
	cmp	r2, #0
	bne	.L1453
	mov	r1, r0
	ldr	r0, .L1460+12
	bl	sftl_printk
.L1453:
	mov	r0, r8
	bl	decrement_vpc_count
.L1452:
	add	r7, r7, #1
	add	r5, r5, #20
	b	.L1447
.L1454:
	ldrb	r2, [r4, #6]	@ zero_extendqisi2
	ldrh	r3, [r6, #32]
	cmp	r2, r3
	bcc	.L1446
	movw	r2, #951
	ldr	r1, .L1460+4
	ldr	r0, .L1460+8
	bl	sftl_printk
.L1446:
	sub	sp, fp, #40
	ldmfd	sp, {r4, r5, r6, r7, r8, r9, r10, fp, sp, pc}
.L1461:
	.align	2
.L1460:
	.word	.LANCHOR0
	.word	.LANCHOR1+519
	.word	.LC1
	.word	.LC111
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
	movw	r3, #2444
	ldr	r4, .L1488
	movw	r2, #65535
	ldrh	r5, [r4, r3]
	ldrh	r1, [r4, #102]
	cmp	r5, r2
	bne	.L1463
.L1473:
	mov	r3, #0
	str	r3, [r4, #4008]
	movw	r3, #2444
	ldrh	r2, [r4, r3]
	movw	r3, #65535
	cmp	r2, r3
	bne	.L1486
.L1464:
	mov	r0, #0
	ldmib	sp, {r4, r5, r6, r7, r8, r9, r10, fp, sp, pc}
.L1463:
	cmp	r0, #0
	beq	.L1466
	ldr	r3, .L1488+4
	ldrh	r0, [r3, #4]
	cmp	r0, r2
	beq	.L1467
.L1468:
	mov	r1, #2
.L1466:
	ldr	r0, .L1488+8
	bl	FtlGcScanTempBlk
	cmn	r0, #1
	str	r0, [fp, #-44]
	beq	.L1469
	ldr	r2, [r4, #2328]
	lsl	r5, r5, #1
	ldrh	r3, [r2, r5]
	cmp	r3, #4
	bls	.L1470
	sub	r3, r3, #5
	mov	r0, #1
	strh	r3, [r2, r5]	@ movhi
	bl	FtlEctTblFlush
.L1470:
	ldr	r3, [r4, #4008]
	cmp	r3, #0
	bne	.L1471
	ldr	r3, [r4, #2732]
	ldr	r0, [fp, #-44]
	add	r3, r3, #1
	ubfx	r0, r0, #10, #16
	str	r3, [r4, #2732]
	bl	FtlBbmMapBadBlock
	bl	FtlBbmTblFlush
.L1471:
	mov	r3, #0
	str	r3, [r4, #4008]
.L1482:
	mov	r0, #1
	ldmib	sp, {r4, r5, r6, r7, r8, r9, r10, fp, sp, pc}
.L1467:
	mov	r2, #0
	strh	r2, [r3, #4]	@ movhi
	movw	r3, #2344
	ldrh	r3, [r4, r3]
	cmp	r3, #17
	bhi	.L1468
	b	.L1466
.L1469:
	ldr	r3, .L1488+4
	ldrh	r2, [r3, #4]
	movw	r3, #65535
	cmp	r2, r3
	bne	.L1482
	b	.L1473
.L1486:
	ldrb	r1, [r4, #2451]	@ zero_extendqisi2
	ldrh	r3, [r4, #102]
	ldr	r6, .L1488+12
	mul	r3, r3, r1
	ldrh	r2, [r6]
	cmp	r2, r3
	beq	.L1474
	mov	r2, #162
	ldr	r1, .L1488+16
	ldr	r0, .L1488+20
	bl	sftl_printk
.L1474:
	ldrb	r2, [r4, #2451]	@ zero_extendqisi2
	movw	r3, #2444
	ldrh	r0, [r4, #102]
	mov	r5, #0
	ldrh	r3, [r4, r3]
	ldr	r1, [r4, #2324]
	ldr	r10, .L1488+20
	smulbb	r2, r2, r0
	lsl	r3, r3, #1
	strh	r2, [r1, r3]	@ movhi
	ldr	r2, [r4, #2528]
	ldrh	r3, [r6]
	add	r3, r3, r2
	str	r3, [r4, #2528]
.L1475:
	ldrh	r2, [r6]
	uxth	r3, r5
	cmp	r2, r3
	bhi	.L1479
	movw	r0, #65535
	bl	decrement_vpc_count
	movw	r3, #2444
	ldr	r1, [r4, #2324]
	ldrh	r0, [r4, r3]
	mov	r5, r3
	lsl	r2, r0, #1
	ldrh	r2, [r1, r2]
	cmp	r2, #0
	beq	.L1480
	bl	INSERT_DATA_LIST
.L1481:
	mvn	r7, #0
	movw	r3, #3192
	strh	r7, [r4, r5]	@ movhi
	mov	r5, #0
	strh	r5, [r4, r3]	@ movhi
	strh	r5, [r6]	@ movhi
	bl	l2p_flush
	bl	FtlVpcTblFlush
	movw	r3, #2344
	ldrh	r2, [r4, r3]
	movw	r3, #2572
	ldrh	r3, [r4, r3]
	add	r3, r3, r3, lsl #1
	cmp	r2, r3, asr #2
	movwgt	r3, #2588
	movgt	r2, #20
	strhgt	r7, [r4, r3]	@ movhi
	movwgt	r3, #3148
	strhgt	r2, [r4, r3]	@ movhi
	b	.L1464
.L1479:
	uxth	r3, r5
	mov	r7, #12
	ldr	r8, [r4, #3196]
	mul	r7, r7, r3
	ldr	r2, [r4, #2504]
	add	r9, r8, r7
	ldr	r1, [r9, #8]
	cmp	r1, r2
	bcc	.L1476
	mov	r2, #168
	ldr	r1, .L1488+16
	mov	r0, r10
	bl	sftl_printk
.L1476:
	mov	r2, #0
	sub	r1, fp, #44
	ldr	r0, [r9, #8]
	bl	log2phys
	ldr	r2, [fp, #-44]
	ldr	r0, [r8, r7]
	cmp	r0, r2
	bne	.L1477
	ubfx	r0, r0, #10, #16
	bl	P2V_block_in_plane
	mov	r2, #1
	mov	r7, r0
	add	r1, r9, #4
	ldr	r0, [r9, #8]
	bl	log2phys
	mov	r0, r7
.L1487:
	bl	decrement_vpc_count
.L1478:
	add	r5, r5, #1
	b	.L1475
.L1477:
	ldr	r3, [r9, #4]
	cmp	r2, r3
	beq	.L1478
	ldr	r3, .L1488+8
	ldrh	r0, [r3]
	b	.L1487
.L1480:
	bl	INSERT_FREE_LIST
	b	.L1481
.L1489:
	.align	2
.L1488:
	.word	.LANCHOR0
	.word	.LANCHOR2
	.word	.LANCHOR0+2444
	.word	.LANCHOR0+3200
	.word	.LANCHOR1+532
	.word	.LC1
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
	ldr	r4, .L1492
	ldr	r5, .L1492+4
	ldrh	r1, [r4, #102]
	mov	r0, r5
	bl	FtlGcScanTempBlk
	ldrh	r2, [r5, #2]
	ldrh	r3, [r4, #102]
	cmp	r2, r3
	ldmfdcc	sp, {r4, r5, fp, sp, pc}
	ldr	r0, .L1492+8
	bl	FtlMapBlkWriteDump_data
	mov	r0, #0
	bl	FtlGcFreeTempBlock
	mov	r3, #0
	str	r3, [r4, #4008]
	ldmfd	sp, {r4, r5, fp, sp, pc}
.L1493:
	.align	2
.L1492:
	.word	.LANCHOR0
	.word	.LANCHOR0+2444
	.word	.LANCHOR0+3908
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
	ldr	r4, .L1495
	mov	r6, #0
	add	r5, r4, #2336
	str	r6, [r4, #4020]
	add	r5, r5, #12
	add	r4, r4, #2384
	mov	r0, r5
	add	r4, r4, #12
	bl	FtlRecoverySuperblock
	mov	r0, r5
	bl	FtlSlcSuperblockCheck
	mov	r0, r4
	bl	FtlRecoverySuperblock
	mov	r0, r4
	bl	FtlSlcSuperblockCheck
	bl	FtlGcPageRecovery
	movw	r0, #65535
	bl	decrement_vpc_count
	mov	r0, r6
	ldmfd	sp, {r4, r5, r6, fp, sp, pc}
.L1496:
	.align	2
.L1495:
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
	ldr	r4, .L1504
	mov	r3, #0
	mov	r5, #0
	mov	r6, #20
	mov	r2, r3
	ldr	r1, [r4, #3164]
	ldr	r0, [r4, #3840]
	bl	FlashProgPages
.L1498:
	ldr	r1, [r4, #3164]
	uxth	r3, r5
	cmp	r1, r3
	bhi	.L1501
	ldr	r0, [r4, #3840]
	bl	FtlGcBufFree
	ldr	r3, .L1504+4
	mov	r0, #0
	str	r0, [r4, #3164]
	ldrh	r3, [r3, #4]
	cmp	r3, r0
	ldmfdne	sp, {r4, r5, r6, fp, sp, pc}
	mov	r0, #1
	bl	FtlGcFreeTempBlock
	b	.L1503
.L1501:
	mul	r3, r6, r3
	ldr	r2, [r4, #3840]
	add	r5, r5, #1
	ldr	r0, [r2, r3]
	add	r1, r2, r3
	cmn	r0, #1
	bne	.L1499
	movw	r1, #2444
	ldr	ip, [r4, #2324]
	ldrh	r2, [r4, r1]
	mov	lr, #0
	lsl	r2, r2, #1
	strh	lr, [ip, r2]	@ movhi
	ldr	r2, [r4, #2732]
	strh	r0, [r4, r1]	@ movhi
	add	r2, r2, #1
	str	r2, [r4, #2732]
	ldr	r2, [r4, #3840]
	add	r3, r2, r3
	ldr	r0, [r3, #4]
	ubfx	r0, r0, #10, #16
	bl	FtlBbmMapBadBlock
	bl	FtlBbmTblFlush
	bl	FtlGcPageVarInit
.L1503:
	mov	r0, #1
	ldmfd	sp, {r4, r5, r6, fp, sp, pc}
.L1499:
	ldr	r3, [r1, #12]
	ldr	r1, [r1, #4]
	ldr	r2, [r3, #8]
	ldr	r0, [r3, #12]
	bl	FtlGcUpdatePage
	b	.L1498
.L1505:
	.align	2
.L1504:
	.word	.LANCHOR0
	.word	.LANCHOR0+2444
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
	ldr	r4, .L1509
	movw	r3, #2444
	ldrh	r2, [r4, r3]
	movw	r3, #65535
	cmp	r2, r3
	beq	.L1507
	ldr	r3, .L1509+4
	ldrh	r3, [r3, #4]
	cmp	r3, #0
	bne	.L1508
.L1507:
	mov	r0, #0
	mov	r5, #0
	bl	FtlGcFreeTempBlock
	ldr	r0, .L1509+4
	strb	r5, [r4, #2452]
	bl	allocate_data_superblock
	movw	r3, #3192
	strh	r5, [r4, r3]	@ movhi
	ldr	r3, .L1509+8
	strh	r5, [r3]	@ movhi
	bl	l2p_flush
	mov	r0, r5
	bl	FtlEctTblFlush
	bl	FtlVpcTblFlush
.L1508:
	ldr	r0, .L1509+4
	bl	get_new_active_ppa
	ldmfd	sp, {r4, r5, fp, sp, pc}
.L1510:
	.align	2
.L1509:
	.word	.LANCHOR0
	.word	.LANCHOR0+2444
	.word	.LANCHOR0+3200
	.size	Ftl_get_new_temp_ppa, .-Ftl_get_new_temp_ppa
	.align	2
	.global	rk_ftl_garbage_collect
	.syntax unified
	.arm
	.fpu softvfp
	.type	rk_ftl_garbage_collect, %function
rk_ftl_garbage_collect:
	@ args = 0, pretend = 0, frame = 28
	@ frame_needed = 1, uses_anonymous_args = 0
	mov	ip, sp
	push	{r4, r5, r6, r7, r8, r9, r10, fp, ip, lr, pc}
	sub	fp, ip, #4
	sub	sp, sp, #36
	mov	r6, r0
	ldr	r3, .L1623
	ldr	r0, [r3, #3824]
	mov	r5, r3
	cmp	r0, #0
	movne	r0, #0
	bne	.L1511
	add	r2, r3, #2336
	ldrh	r2, [r2]
	cmp	r2, #47
	bls	.L1511
	ldr	r2, .L1623+4
	ldrh	r1, [r2, #4]
	movw	r2, #65535
	cmp	r1, r2
	bne	.L1513
.L1516:
	movw	ip, #3204
	movw	r2, #65535
	ldrh	r7, [r5, ip]
	cmp	r7, r2
	bne	.L1514
.L1515:
	ldr	r3, [r5, #3152]
	movw	r2, #2588
	ldrh	r2, [r5, r2]
	movw	r4, #65535
	add	r3, r3, #1
	add	r3, r3, r6, lsl #7
	cmp	r2, r4
	str	r3, [r5, #3152]
	bne	.L1517
	movw	r1, #2444
	ldrh	r4, [r5, r1]
	cmp	r4, r2
	movne	r4, r2
	bne	.L1517
	movw	r2, #3202
	ldrh	r7, [r5, r2]
	cmp	r7, r4
	bne	.L1517
	movw	r10, #2344
	ldrh	r2, [r5, r10]
	cmp	r2, #24
	movcc	r2, #5120
	movcs	r2, #1024
	cmp	r3, r2
	movls	r4, r7
	bls	.L1517
	ldr	r3, .L1623+8
	mov	r9, #0
	str	r9, [r5, #3152]
	strh	r9, [r3, #-8]	@ movhi
	bl	GetSwlReplaceBlock
	cmp	r0, r7
	mov	r4, r0
	movne	r7, r0
	bne	.L1519
	movw	r8, #3150
	ldrh	r2, [r5, r10]
	ldrh	r3, [r5, r8]
	cmp	r2, r3
	bcs	.L1520
	mov	r0, #64
	bl	List_get_gc_head_node
	uxth	r3, r0
	cmp	r3, r4
	beq	.L1522
	mov	r0, r3
	ldr	r3, [r5, #2324]
	lsl	r0, r0, #1
	ldrh	r3, [r3, r0]
	cmp	r3, #7
	bhi	.L1523
	mov	r0, r9
	bl	List_get_gc_head_node
	uxth	r7, r0
	mov	r3, #128
	strh	r3, [r5, r8]	@ movhi
	cmp	r7, r4
	bne	.L1519
.L1522:
	bl	FtlGcReFreshBadBlk
.L1517:
	movw	r2, #65535
	sub	r3, r4, r2
	clz	r3, r3
	lsr	r3, r3, #5
	cmp	r6, #0
	movne	r6, #0
	andeq	r6, r3, #1
	cmp	r6, #0
	beq	.L1525
	movw	r3, #2344
	ldrh	r3, [r5, r3]
	cmp	r3, #24
	movhi	r6, #1
	bhi	.L1526
	ldrh	r6, [r5, #102]
	cmp	r3, #16
	lsrhi	r6, r6, #5
	bhi	.L1526
	cmp	r3, #12
	lsrhi	r6, r6, #4
	bhi	.L1526
	cmp	r3, #8
	lsrhi	r6, r6, #2
.L1526:
	movw	r2, #3148
	ldrh	r1, [r5, r2]
	cmp	r1, r3
	bcs	.L1530
	movw	r3, #2444
	movw	r1, #65535
	ldrh	r3, [r5, r3]
	cmp	r3, r1
	bne	.L1531
	movw	r1, #3202
	ldrh	r1, [r5, r1]
	cmp	r1, r3
	bne	.L1531
	ldr	r3, .L1623+8
	ldrh	r0, [r3, #-8]
	cmp	r0, #0
	bne	.L1532
	ldr	r3, [r5, #2504]
	ldr	r1, [r5, #2500]
	add	r3, r3, r3, lsl #1
	cmp	r1, r3, lsr #2
	movcs	r3, #18
	bcs	.L1616
.L1532:
	movw	r3, #2572
	movw	r2, #3148
	ldrh	r3, [r5, r3]
	add	r3, r3, r3, lsl #1
	asr	r3, r3, #2
.L1616:
	strh	r3, [r5, r2]	@ movhi
	mov	r3, #0
	str	r3, [r5, #3160]
.L1511:
	sub	sp, fp, #40
	ldmfd	sp, {r4, r5, r6, r7, r8, r9, r10, fp, sp, pc}
.L1513:
	movw	r1, #2444
	ldrh	r3, [r3, r1]
	cmp	r3, r2
	beq	.L1516
	mov	r0, #1
	bl	FtlGcFreeTempBlock
	cmp	r0, #0
	beq	.L1516
	mov	r0, #1
	b	.L1511
.L1514:
	movw	r4, #3202
	ldrh	r3, [r5, r4]
	cmp	r3, r2
	bne	.L1515
	movw	r1, #3206
	ldrh	lr, [r5, r1]
	cmp	lr, r3
	beq	.L1515
	movw	r2, #3208
	ldrh	r0, [r5, r2]
	cmp	r0, r3
	mvnne	r3, #0
	strhne	r7, [r5, r4]	@ movhi
	strhne	lr, [r5, ip]	@ movhi
	strhne	r0, [r5, r1]	@ movhi
	strhne	r3, [r5, r2]	@ movhi
	b	.L1515
.L1523:
	mov	r3, #64
.L1615:
	strh	r3, [r5, r8]	@ movhi
	b	.L1522
.L1520:
	mov	r3, #80
	b	.L1615
.L1519:
	movw	r0, #3148
	ldr	r3, [r5, #2324]
	ldrh	r0, [r5, r0]
	lsl	r1, r7, #1
	movw	r2, #2344
	ldrh	r3, [r3, r1]
	mov	r4, r7
	ldrh	r2, [r5, r2]
	str	r0, [sp, #4]
	ldr	r0, [r5, #2328]
	ldrh	r1, [r0, r1]
	ldr	r0, .L1623+12
	str	r1, [sp]
	mov	r1, r7
	bl	sftl_printk
	b	.L1522
.L1531:
	movw	r3, #2572
	movw	r2, #3148
	ldrh	r3, [r5, r3]
	add	r3, r3, r3, lsl #1
	asr	r3, r3, #2
	strh	r3, [r5, r2]	@ movhi
.L1530:
	movw	r3, #3210
	movw	r4, #65535
	ldrh	r3, [r5, r3]
	cmp	r3, #0
	addne	r6, r6, #32
	uxthne	r6, r6
.L1536:
	movw	r3, #2588
	ldrh	r2, [r5, r3]
	movw	r1, #65535
	cmp	r2, r1
	bne	.L1545
	cmp	r4, r2
	strhne	r4, [r5, r3]	@ movhi
	bne	.L1547
	movw	r2, #3202
	ldrh	r3, [r5, r2]
	cmp	r3, r4
	beq	.L1547
	ldr	r1, [r5, #2324]
	lsl	r3, r3, #1
	ldrh	r3, [r1, r3]
	cmp	r3, #0
	mvneq	r3, #0
	strheq	r3, [r5, r2]	@ movhi
	movw	r3, #3202
	ldrh	r1, [r5, r3]
	movw	r2, #2588
	strh	r1, [r5, r2]	@ movhi
	mvn	r2, #0
	strh	r2, [r5, r3]	@ movhi
.L1547:
	movw	r7, #2588
	mov	r3, #0
	ldrh	r0, [r5, r7]
	strb	r3, [r5, #2596]
	movw	r3, #65535
	cmp	r0, r3
	beq	.L1545
	bl	IsBlkInGcList
	cmp	r0, #0
	ldr	r8, .L1623+16
	mvnne	r3, #0
	strhne	r3, [r5, r7]	@ movhi
	movw	r7, #2588
	ldrh	r2, [r5, r7]
	movw	r3, #65535
	cmp	r2, r3
	beq	.L1545
	mov	r0, r8
	bl	make_superblock
	ldr	r2, .L1623+8
	mov	r3, #0
	strh	r3, [r8, #2]	@ movhi
	strb	r3, [r5, #2594]
	strh	r3, [r2, #-6]	@ movhi
	ldrh	r3, [r5, r7]
	ldr	r1, [r5, #2324]
	lsl	r3, r3, #1
	ldrh	r3, [r1, r3]
	strh	r3, [r2, #-4]	@ movhi
.L1545:
	movw	r3, #2588
	movw	r2, #2348
	ldrh	r3, [r5, r3]
	ldrh	r2, [r5, r2]
	cmp	r2, r3
	beq	.L1551
	movw	r2, #2396
	ldrh	r2, [r5, r2]
	cmp	r2, r3
	ldrne	r7, .L1623
	movne	r5, r7
	bne	.L1552
.L1551:
	mvn	r2, #0
	movw	r3, #2588
.L1621:
	strh	r2, [r5, r3]	@ movhi
	ldr	r3, .L1623+8
.L1622:
	ldrh	r0, [r3, #-8]
	b	.L1511
.L1525:
	movw	r1, #2444
	ldrh	r7, [r5, r1]
	cmp	r7, r2
	bne	.L1593
	movw	r2, #3202
	ldrh	r2, [r5, r2]
	cmp	r2, r7
	movne	r3, #0
	andeq	r3, r3, #1
	cmp	r3, #0
	beq	.L1593
	movw	r3, #2588
	ldrh	r3, [r5, r3]
	cmp	r3, r7
	beq	.L1537
.L1542:
	mov	r4, r7
.L1593:
	mov	r6, #1
	b	.L1536
.L1537:
	movw	r3, #2344
	movw	r2, #3148
	ldrh	r8, [r5, r3]
	ldrh	r3, [r5, r2]
	str	r6, [r5, #3160]
	ldr	r4, .L1623+8
	cmp	r3, r8
	bcs	.L1538
	ldrh	r3, [r4, #-8]
	cmp	r3, #0
	bne	.L1539
	ldr	r3, [r5, #2504]
	ldr	r1, [r5, #2500]
	add	r3, r3, r3, lsl #1
	cmp	r1, r3, lsr #2
	movcs	r3, #18
	bcs	.L1617
.L1539:
	movw	r3, #2572
	movw	r2, #3148
	ldrh	r3, [r5, r3]
	add	r3, r3, r3, lsl #1
	asr	r3, r3, #2
.L1617:
	strh	r3, [r5, r2]	@ movhi
.L1620:
	bl	FtlReadRefresh
	ldrh	r0, [r4, #-8]
	b	.L1511
.L1538:
	ldrh	r0, [r4, #-8]
	cmp	r0, #0
	bne	.L1542
	movw	r3, #2572
	ldrh	r6, [r5, r3]
	add	r3, r6, r6, lsl #1
	asr	r3, r3, #2
	strh	r3, [r5, r2]	@ movhi
	bl	List_get_gc_head_node
	uxth	r0, r0
	ldr	r3, [r5, #2324]
	ldrh	r1, [r5, #104]
	lsl	r0, r0, #1
	ldrh	r2, [r3, r0]
	ldrh	r3, [r5, #32]
	mul	r3, r3, r1
	add	r3, r3, r3, lsr #31
	cmp	r2, r3, asr #1
	ble	.L1543
	sub	r6, r6, #1
	cmp	r8, r6
	bge	.L1620
.L1543:
	cmp	r2, #0
	bne	.L1542
	movw	r0, #65535
	bl	decrement_vpc_count
	movw	r3, #2344
	ldrh	r0, [r5, r3]
	add	r0, r0, #1
	b	.L1511
.L1555:
	uxth	r2, r0
	str	r1, [fp, #-52]
	add	r10, r10, #1
	mov	r0, r2
	str	r2, [fp, #-48]
	bl	IsBlkInGcList
	cmp	r0, #0
	ldr	r2, [fp, #-48]
	ldr	r1, [fp, #-52]
	strhne	r10, [r9]	@ movhi
	bne	.L1554
	ldrh	lr, [r5, #32]
	uxth	r10, r10
	ldrh	r3, [r5, #102]
	lsl	r2, r2, #1
	ldr	r0, [r5, #2324]
	strh	r10, [r9]	@ movhi
	mul	r3, lr, r3
	ldrh	ip, [r0, r2]
	add	lr, r3, r3, lsr #31
	cmp	ip, lr, asr #1
	bgt	.L1558
	cmp	ip, #8
	cmphi	r10, #48
	bls	.L1559
	ldr	ip, .L1623+20
	ldrh	ip, [ip]
	cmp	ip, #35
	bhi	.L1559
.L1558:
	ldr	lr, .L1623+24
	mov	ip, #0
	strh	ip, [lr]	@ movhi
.L1559:
	ldrh	ip, [r0, r2]
	movw	r0, #65535
	cmp	r3, ip
	cmple	r4, r0
	moveq	r3, #1
	movne	r3, #0
	bne	.L1560
	mvn	r2, #0
	movw	r3, #2588
	strh	r2, [r5, r3]	@ movhi
	mov	r2, #0
	movw	r3, #3156
	b	.L1621
.L1560:
	cmp	ip, #0
	bne	.L1561
	bl	decrement_vpc_count
	ldr	r3, .L1623+24
	ldr	r2, .L1623+24
	ldrh	r3, [r3]
	add	r3, r3, #1
	strh	r3, [r2]	@ movhi
	b	.L1554
.L1561:
	strb	r3, [r5, #2596]
	ldr	r3, .L1623+28
	ldrh	r3, [r3]
	cmp	r3, r1
	bne	.L1562
	movw	r2, #665
	ldr	r1, .L1623+32
	ldr	r0, .L1623+36
	bl	sftl_printk
.L1562:
	ldr	r3, .L1623+40
	ldrh	r2, [r8]
	ldrh	r3, [r3]
	cmp	r2, r3
	bne	.L1563
	movw	r2, #666
	ldr	r1, .L1623+32
	ldr	r0, .L1623+36
	bl	sftl_printk
.L1563:
	ldr	r3, .L1623+44
	ldrh	r2, [r8]
	ldrh	r3, [r3]
	cmp	r2, r3
	bne	.L1564
	movw	r2, #667
	ldr	r1, .L1623+32
	ldr	r0, .L1623+36
	bl	sftl_printk
.L1564:
	ldr	r0, .L1623+16
	bl	make_superblock
	ldrh	r2, [r8]
	mov	r3, #0
	ldr	r1, .L1623+48
	ldr	r0, [r5, #2324]
	lsl	r2, r2, #1
	strh	r3, [r1]	@ movhi
	ldrh	r2, [r0, r2]
	strh	r3, [r8, #2]	@ movhi
	strb	r3, [r5, #2594]
	strh	r2, [r1, #2]	@ movhi
.L1553:
	bl	FtlReadRefresh
	mov	r3, #1
	str	r3, [r5, #3824]
	ldrh	r3, [r5, #102]
	str	r3, [fp, #-56]
	ldrh	r3, [r8, #2]
	ldr	r1, [fp, #-56]
	add	r2, r3, r6
	cmp	r2, r1
	movgt	r2, r1
	subgt	r6, r2, r3
	mov	r3, #0
	uxthgt	r6, r6
.L1619:
	str	r3, [fp, #-52]
	ldrh	r3, [fp, #-52]
	ldr	r8, .L1623+16
	cmp	r6, r3
	bls	.L1574
	ldrh	r9, [r8, #2]
	add	r1, r8, #14
	ldr	r3, [fp, #-52]
	movw	r10, #65535
	ldrh	ip, [r7, #32]
	ldr	r0, [r7, #3180]
	add	r9, r9, r3
	mov	r3, #0
	mov	r8, r3
	b	.L1575
.L1568:
	ldrh	r2, [r1, #2]!
	add	r3, r3, #1
	cmp	r2, r10
	movne	lr, #20
	orrne	r2, r9, r2, lsl #10
	mlane	lr, lr, r8, r0
	addne	r8, r8, #1
	uxthne	r8, r8
	strne	r2, [lr, #4]
.L1575:
	uxth	r2, r3
	cmp	ip, r2
	bhi	.L1568
	ldrb	r2, [r7, #2596]	@ zero_extendqisi2
	mov	r1, r8
	bl	FlashReadPages
	mov	r3, #0
.L1618:
	str	r3, [fp, #-48]
	ldrh	r3, [fp, #-48]
	cmp	r3, r8
	ldrcs	r3, [fp, #-52]
	addcs	r3, r3, #1
	bcs	.L1619
.L1573:
	ldr	r3, [fp, #-48]
	mov	r9, #20
	mul	r9, r9, r3
	ldr	r3, [r7, #3180]
	add	r2, r3, r9
	ldr	r3, [r3, r9]
	cmn	r3, #1
	beq	.L1570
	ldr	r10, [r2, #12]
	movw	r3, #61589
	ldrh	r2, [r10]
	cmp	r2, r3
	bne	.L1570
	ldr	r3, [r10, #8]
	cmn	r3, #1
	bne	.L1571
	movw	r2, #703
	ldr	r1, .L1623+32
	ldr	r0, .L1623+36
	str	r3, [fp, #-60]
	bl	sftl_printk
	ldr	r3, [fp, #-60]
.L1571:
	mov	r2, #0
	mov	r0, r3
	sub	r1, fp, #44
	bl	log2phys
	ldr	r0, [r5, #3180]
	ldr	r3, [fp, #-44]
	add	r0, r0, r9
	ldr	r2, [r0, #4]
	cmp	r2, r3
	bne	.L1570
	ldr	r3, .L1623+48
	mov	r1, #20
	ldr	lr, [r5, #3164]
	ldr	r0, [r0, #16]
	ldrh	r2, [r3]
	str	r1, [fp, #-64]
	add	r2, r2, #1
	strh	r2, [r3], #6	@ movhi
	ldr	r2, [r5, #3840]
	str	r3, [fp, #-68]
	mla	r2, r1, lr, r2
	str	r0, [r2, #16]
	str	r2, [fp, #-60]
	bl	Ftl_get_new_temp_ppa
	ldr	r2, [fp, #-60]
	ldr	r1, [fp, #-64]
	str	r0, [r2, #4]
	ldr	r0, [r5, #3164]
	ldr	r2, [r5, #3840]
	mla	r1, r1, r0, r2
	ldr	r2, [r5, #3180]
	add	r2, r2, r9
	ldr	r0, [r2, #8]
	str	r0, [r1, #8]
	ldr	r2, [r2, #12]
	str	r2, [r1, #12]
	ldr	r2, [fp, #-44]
	str	r2, [r10, #12]
	ldr	r2, .L1623+44
	ldrh	r1, [r2]
	str	r2, [fp, #-60]
	strh	r1, [r10, #2]	@ movhi
	ldr	r1, [r5, #2544]
	ldr	r0, [r5, #3180]
	str	r1, [r10, #4]
	ldr	r1, [r5, #3164]
	add	r0, r0, r9
	add	r1, r1, #1
	str	r1, [r5, #3164]
	mov	r1, #1
	bl	FtlGcBufAlloc
	ldrb	r0, [r5, #2451]	@ zero_extendqisi2
	ldr	r1, [r5, #3164]
	ldr	r3, [fp, #-68]
	cmp	r0, r1
	beq	.L1572
	ldr	r2, [fp, #-60]
	ldrh	r2, [r2, #4]
	cmp	r2, #0
	bne	.L1570
.L1572:
	str	r3, [fp, #-60]
	bl	Ftl_gc_temp_data_write_back
	cmp	r0, #0
	ldr	r3, [fp, #-60]
	ldrne	r2, .L1623
	movne	r1, #0
	strne	r1, [r2, #3824]
	bne	.L1622
.L1570:
	ldr	r3, [fp, #-48]
	add	r3, r3, #1
	b	.L1618
.L1574:
	ldrh	r3, [r8, #2]
	add	r6, r6, r3
	ldr	r3, [fp, #-56]
	uxth	r6, r6
	cmp	r3, r6
	strh	r6, [r8, #2]	@ movhi
	bhi	.L1576
	ldr	r3, [r7, #3164]
	ldr	r9, .L1623+8
	cmp	r3, #0
	beq	.L1577
	bl	Ftl_gc_temp_data_write_back
	cmp	r0, #0
	movne	r3, #0
	ldrhne	r0, [r9, #-8]
	strne	r3, [r7, #3824]
	bne	.L1511
.L1577:
	ldrh	r6, [r9, #-6]
	cmp	r6, #0
	bne	.L1578
	ldrh	r1, [r8]
	ldr	r2, [r7, #2324]
	lsl	r3, r1, #1
	ldrh	r3, [r2, r3]
	cmp	r3, #0
	beq	.L1578
	ldrh	r0, [r9, #-4]
	ldrh	r2, [r8, #2]
	str	r3, [sp]
	mov	r3, r6
	str	r0, [sp, #4]
	ldr	r0, .L1623+52
	bl	sftl_printk
	ldrh	r3, [r8]
	ldr	r2, [r7, #2324]
	lsl	r3, r3, #1
	strh	r6, [r2, r3]	@ movhi
	ldrh	r0, [r8]
	bl	update_vpc_list
	bl	l2p_flush
	bl	FtlVpcTblFlush
.L1578:
	mvn	r3, #0
	strh	r3, [r8]	@ movhi
.L1576:
	ldr	r3, .L1623+56
	ldrh	r3, [r3]
	cmp	r3, #2
	bhi	.L1579
	ldrh	r6, [r7, #102]
.L1552:
	ldr	r8, .L1623+16
	movw	r3, #65535
	ldrh	r2, [r8]
	cmp	r2, r3
	bne	.L1553
	add	r9, r8, #568
	mov	r3, #0
	str	r3, [r7, #3160]
.L1554:
	ldrh	r10, [r9]
	mov	r0, r10
	bl	List_get_gc_head_node
	ldr	r3, .L1623+16
	uxth	r1, r0
	strh	r1, [r3]	@ movhi
	movw	r3, #65535
	cmp	r1, r3
	bne	.L1555
	mov	r3, #0
	mov	r0, #8
	strh	r3, [r9]	@ movhi
	b	.L1511
.L1579:
	mov	r2, #0
	str	r2, [r7, #3824]
	ldr	r2, .L1623+8
	ldrh	r0, [r2, #-8]
	cmp	r0, #0
	addeq	r0, r3, #1
	b	.L1511
.L1624:
	.align	2
.L1623:
	.word	.LANCHOR0
	.word	.LANCHOR2
	.word	.LANCHOR3-4016
	.word	.LC112
	.word	.LANCHOR0+2588
	.word	.LANCHOR0+3192
	.word	.LANCHOR0+3156
	.word	.LANCHOR0+2348
	.word	.LANCHOR1+551
	.word	.LC1
	.word	.LANCHOR0+2396
	.word	.LANCHOR0+2444
	.word	.LANCHOR3-4022
	.word	.LC113
	.word	.LANCHOR0+2344
	.size	rk_ftl_garbage_collect, .-rk_ftl_garbage_collect
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
	sub	sp, sp, #72
	uxtb	r0, r0
	mov	r7, r1
	str	r2, [fp, #-72]
	cmp	r0, #16
	mov	r9, r3
	bne	.L1627
	mov	r2, r3
	ldr	r1, [fp, #-72]
	add	r0, r7, #256
	bl	FtlVendorPartRead
	str	r0, [fp, #-48]
.L1626:
	ldr	r0, [fp, #-48]
	sub	sp, fp, #40
	ldmfd	sp, {r4, r5, r6, r7, r8, r9, r10, fp, sp, pc}
.L1627:
	ldr	r3, [fp, #-72]
	ldr	r4, .L1661
	add	r3, r1, r3
	str	r3, [fp, #-56]
	ldr	r2, [fp, #-56]
	ldr	r3, [r4, #140]
	cmp	r2, r3
	bhi	.L1648
	ldr	r3, .L1661+4
	ldr	r3, [r3]
	cmn	r3, #1
	beq	.L1649
	ldrh	r5, [r4, #58]
	mov	r0, r7
	mov	r8, #0
	mov	r6, r8
	mov	r1, r5
	bl	__udivsi3
	ldr	r3, [fp, #-56]
	mov	r1, r5
	str	r0, [fp, #-60]
	sub	r0, r3, #1
	bl	__udivsi3
	ldr	r3, [fp, #-60]
	ldr	r5, [fp, #-60]
	str	r0, [fp, #-64]
	rsb	r3, r3, #1
	str	r8, [fp, #-80]
	add	r3, r3, r0
	str	r8, [fp, #-76]
	str	r3, [fp, #-52]
	ldr	r2, [fp, #-52]
	ldr	r3, [r4, #2508]
	str	r8, [fp, #-48]
	add	r3, r3, r2
	str	r3, [r4, #2508]
.L1629:
	ldr	r3, [fp, #-52]
	cmp	r3, #0
	bne	.L1646
	ldr	r3, .L1661
	movw	r2, #3210
	ldrh	r2, [r3, r2]
	cmp	r2, #0
	bne	.L1647
	movw	r2, #2344
	ldrh	r3, [r3, r2]
	cmp	r3, #31
	bhi	.L1626
.L1647:
	mov	r1, #1
	mov	r0, #0
	bl	rk_ftl_garbage_collect
	b	.L1626
.L1646:
	mov	r2, #0
	sub	r1, fp, #44
	mov	r0, r5
	bl	log2phys
	ldr	r2, [fp, #-44]
	cmn	r2, #1
	moveq	r10, #0
	beq	.L1631
	ldr	r3, [r4, #3836]
	mov	r1, #20
	ldrh	r10, [r4, #58]
	mla	r3, r1, r6, r3
	str	r2, [r3, #4]
	ldr	r2, [fp, #-60]
	cmp	r5, r2
	bne	.L1635
	ldr	r2, [r4, #3820]
	mov	r1, r10
	mov	r0, r7
	str	r3, [fp, #-68]
	str	r2, [r3, #8]
	bl	__umodsi3
	ldr	r3, [fp, #-72]
	sub	r2, r10, r0
	str	r0, [fp, #-76]
	cmp	r3, r2
	movcs	r3, r2
	cmp	r3, r10
	str	r3, [fp, #-80]
	ldr	r3, [fp, #-68]
	streq	r9, [r3, #8]
.L1636:
	ldrh	r2, [r4, #112]
	ldr	r1, [r4, #3816]
	str	r5, [r3, #16]
	mul	r2, r6, r2
	add	r6, r6, #1
	bic	r2, r2, #3
	add	r2, r1, r2
	str	r2, [r3, #12]
	b	.L1634
.L1633:
	mla	r0, r0, r5, r10
	ldr	r3, [fp, #-56]
	cmp	r3, r0
	movhi	r3, #1
	movls	r3, #0
	cmp	r7, r0
	movhi	r3, #0
	cmp	r3, #0
	beq	.L1632
	sub	r0, r0, r7
	mov	r1, #512
	add	r0, r9, r0, lsl #9
	bl	__memzero
.L1632:
	add	r10, r10, #1
.L1631:
	ldrh	r0, [r4, #58]
	cmp	r10, r0
	bcc	.L1633
.L1634:
	ldr	r3, [fp, #-52]
	add	r5, r5, #1
	subs	r3, r3, #1
	str	r3, [fp, #-52]
	beq	.L1638
	ldrh	r3, [r4, #32]
	cmp	r6, r3, lsl #2
	bne	.L1629
.L1638:
	cmp	r6, #0
	beq	.L1629
	mov	r2, #0
	mov	r1, r6
	ldr	r0, [r4, #3836]
	bl	FlashReadPages
	lsl	r3, r8, #9
	str	r3, [fp, #-92]
	ldr	r3, [fp, #-76]
	lsl	r3, r3, #9
	str	r3, [fp, #-84]
	ldr	r3, [fp, #-80]
	lsl	r3, r3, #9
	str	r3, [fp, #-88]
	mov	r3, #0
	str	r3, [fp, #-68]
.L1645:
	ldr	r3, [fp, #-68]
	mov	ip, #20
	ldr	r1, [fp, #-60]
	mul	r10, ip, r3
	ldr	r3, [r4, #3836]
	add	r3, r3, r10
	ldr	r2, [r3, #16]
	cmp	r1, r2
	bne	.L1640
	ldr	r1, [r3, #8]
	ldr	r3, [r4, #3820]
	cmp	r1, r3
	bne	.L1641
	ldr	r3, [fp, #-84]
	mov	r0, r9
	ldr	r2, [fp, #-88]
	add	r1, r1, r3
.L1660:
	bl	ftl_memcpy
.L1641:
	ldr	r1, [r4, #3836]
	add	r1, r1, r10
	ldr	r3, [r1, #12]
	ldr	r2, [r1, #16]
	ldr	r3, [r3, #8]
	cmp	r2, r3
	beq	.L1642
	ldr	r3, [r4, #2708]
	add	r3, r3, #1
	str	r3, [r4, #2708]
	ldr	r2, [r1, #8]
	ldr	r3, [r1, #12]
	ldr	r0, [r2, #4]
	str	r0, [sp, #16]
	ldr	r2, [r2]
	ldr	r0, .L1661+8
	str	r2, [sp, #12]
	ldr	r2, [r3, #12]
	str	r2, [sp, #8]
	ldr	r2, [r3, #8]
	str	r2, [sp, #4]
	ldr	r2, [r3, #4]
	str	r2, [sp]
	ldr	r2, [r1, #4]
	ldr	r3, [r3]
	ldr	r1, [r1, #16]
	bl	sftl_printk
.L1642:
	ldr	r3, [r4, #3836]
	ldr	r1, [r3, r10]
	add	r0, r3, r10
	cmn	r1, #1
	ldreq	r2, [r4, #2708]
	streq	r1, [fp, #-48]
	addeq	r2, r2, #1
	streq	r2, [r4, #2708]
	ldr	r3, [r3, r10]
	cmp	r3, #256
	bne	.L1644
	ldr	r0, [r0, #4]
	ubfx	r0, r0, #10, #16
	bl	P2V_block_in_plane
	bl	FtlGcRefreshBlock
.L1644:
	ldr	r3, [fp, #-68]
	add	r3, r3, #1
	cmp	r6, r3
	str	r3, [fp, #-68]
	bne	.L1645
	mov	r6, #0
	b	.L1629
.L1635:
	ldr	r2, [fp, #-64]
	cmp	r5, r2
	bne	.L1637
	ldr	r2, [r4, #3812]
	ldr	r1, [fp, #-56]
	str	r2, [r3, #8]
	mul	r2, r10, r5
	sub	r8, r1, r2
	cmp	r10, r8
	subeq	r2, r2, r7
	addeq	r2, r9, r2, lsl #9
	streq	r2, [r3, #8]
	b	.L1636
.L1637:
	mul	r10, r5, r10
	sub	r10, r10, r7
	add	r10, r9, r10, lsl #9
	str	r10, [r3, #8]
	b	.L1636
.L1640:
	ldr	r1, [fp, #-64]
	cmp	r1, r2
	bne	.L1641
	ldr	r1, [r3, #8]
	ldr	r3, [r4, #3812]
	cmp	r1, r3
	bne	.L1641
	ldrh	r0, [r4, #58]
	ldr	r3, [fp, #-64]
	ldr	r2, [fp, #-92]
	mul	r0, r3, r0
	sub	r0, r0, r7
	add	r0, r9, r0, lsl #9
	b	.L1660
.L1648:
	mvn	r3, #0
.L1649:
	str	r3, [fp, #-48]
	b	.L1626
.L1662:
	.align	2
.L1661:
	.word	.LANCHOR0
	.word	.LANCHOR2
	.word	.LC96
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
	str	r2, [fp, #-84]
	cmp	r0, #16
	str	r3, [fp, #-80]
	bne	.L1666
	mov	r2, r3
	ldr	r1, [fp, #-84]
	add	r0, r9, #256
	bl	FtlVendorPartWrite
.L1665:
	sub	sp, fp, #40
	ldmfd	sp, {r4, r5, r6, r7, r8, r9, r10, fp, sp, pc}
.L1666:
	ldr	r3, [fp, #-84]
	ldr	r4, .L1716
	add	r6, r1, r3
	ldr	r3, [r4, #140]
	cmp	r6, r3
	mvnhi	r0, #0
	bhi	.L1665
	ldr	r5, .L1716+4
	ldr	r0, [r5]
	cmn	r0, #1
	beq	.L1665
	ldr	r3, .L1716+8
	mov	r2, #2048
	ldrh	r7, [r4, #58]
	mov	r0, r9
	str	r2, [r3, #-4016]
	mov	r1, r7
	bl	__udivsi3
	mov	r1, r7
	str	r0, [fp, #-76]
	sub	r0, r6, #1
	bl	__udivsi3
	ldr	r2, [fp, #-76]
	str	r0, [fp, #-92]
	sub	r3, r0, r2
	str	r3, [fp, #-96]
	add	r3, r3, #1
	str	r3, [fp, #-72]
	ldr	r2, [fp, #-72]
	ldr	r3, [r4, #2516]
	add	r3, r3, r2
	str	r3, [r4, #2516]
	ldr	r3, [r5, #8]
	ldr	r4, .L1716+12
	cmp	r3, #0
	beq	.L1668
	ldrh	r3, [r4, #4]
	add	r0, r4, #48
	cmp	r3, #0
	moveq	r4, r0
.L1668:
	ldr	r5, [fp, #-76]
	ldr	r10, .L1716
.L1669:
	ldr	r3, [fp, #-72]
	cmp	r3, #0
	bne	.L1697
	mov	r0, r3
	ldr	r1, [fp, #-96]
	bl	rk_ftl_garbage_collect
	movw	r3, #2344
	ldrh	r3, [r10, r3]
	cmp	r3, #15
	bls	.L1698
.L1700:
	mov	r0, #0
	b	.L1665
.L1697:
	ldrb	r2, [r4, #6]	@ zero_extendqisi2
	ldrh	r3, [r10, #32]
	cmp	r2, r3
	bcc	.L1670
	movw	r2, #991
	ldr	r1, .L1716+16
	ldr	r0, .L1716+20
	bl	sftl_printk
.L1670:
	ldrh	r2, [r4, #4]
	cmp	r2, #0
	bne	.L1671
	ldr	r3, .L1716+12
	ldr	r6, .L1716+4
	cmp	r4, r3
	bne	.L1672
	add	r0, r4, #48
	ldrh	r7, [r0, #4]
	cmp	r7, #0
	bne	.L1673
	bl	allocate_new_data_superblock
	str	r7, [r6, #8]
.L1673:
	ldr	r0, .L1716+12
	bl	allocate_new_data_superblock
	ldr	r4, .L1716+12
	ldr	r3, [r6, #8]
	add	r0, r4, #48
	cmp	r3, #0
	movne	r4, r0
.L1674:
	ldrh	r3, [r4, #4]
	cmp	r3, #0
	bne	.L1671
	mov	r0, r4
	bl	allocate_new_data_superblock
.L1671:
	ldrb	r2, [r4, #7]	@ zero_extendqisi2
	ldrh	r3, [r4, #4]
	ldr	r1, [fp, #-72]
	lsl	r2, r2, #2
	cmp	r3, r1
	movcs	r3, r1
	cmp	r2, r3
	movcc	r3, r2
	ldrb	r2, [r4, #6]	@ zero_extendqisi2
	str	r3, [fp, #-108]
	ldrh	r3, [r10, #32]
	cmp	r2, r3
	bcc	.L1675
	mov	r2, #1024
	ldr	r1, .L1716+16
	ldr	r0, .L1716+20
	bl	sftl_printk
.L1675:
	mov	r3, #0
.L1715:
	str	r3, [fp, #-68]
	ldr	r3, [fp, #-68]
	ldr	r2, [fp, #-108]
	cmp	r3, r2
	bne	.L1695
.L1677:
	mov	r3, r4
	mov	r2, #0
	ldr	r1, [fp, #-68]
	ldr	r0, [r10, #3844]
	bl	FtlProgPages
	ldr	r3, [fp, #-68]
	ldr	r2, [fp, #-72]
	cmp	r3, r2
	bls	.L1696
	movw	r2, #1101
	ldr	r1, .L1716+16
	ldr	r0, .L1716+20
	bl	sftl_printk
.L1696:
	ldr	r3, [fp, #-72]
	ldr	r2, [fp, #-68]
	sub	r3, r3, r2
	str	r3, [fp, #-72]
	b	.L1669
.L1672:
	str	r2, [r6, #8]
	ldrh	r2, [r3, #4]
	cmp	r2, #0
	movne	r4, r3
	bne	.L1671
	mov	r0, r4
	bl	allocate_new_data_superblock
	b	.L1674
.L1695:
	ldrh	r3, [r4, #4]
	cmp	r3, #0
	beq	.L1677
	mov	r2, #0
	sub	r1, fp, #64
	mov	r0, r5
	mov	r7, #20
	bl	log2phys
	mov	r0, r4
	bl	get_new_active_ppa
	ldrh	r2, [r10, #112]
	ldr	r1, [fp, #-68]
	ldr	r3, [fp, #-68]
	mul	r1, r2, r1
	mul	r7, r7, r3
	ldr	r3, [r10, #3844]
	bic	r1, r1, #3
	str	r1, [fp, #-100]
	add	r3, r3, r7
	ldr	r1, [r10, #3816]
	str	r0, [r3, #4]
	ldr	r0, [fp, #-100]
	str	r1, [fp, #-104]
	str	r5, [r3, #16]
	add	r8, r1, r0
	mov	r1, #0
	str	r8, [r3, #12]
	mov	r0, r8
	bl	ftl_memset
	ldr	r3, [fp, #-76]
	ldr	r2, [fp, #-92]
	ldrh	r6, [r10, #58]
	cmp	r5, r2
	cmpne	r5, r3
	bne	.L1678
	cmp	r5, r3
	bne	.L1679
	mov	r1, r6
	mov	r0, r9
	bl	__umodsi3
	ldr	r3, [fp, #-84]
	sub	r6, r6, r0
	str	r0, [fp, #-88]
	cmp	r6, r3
	movcs	r6, r3
.L1680:
	ldrh	r3, [r10, #58]
	cmp	r6, r3
	ldr	r3, [fp, #-76]
	bne	.L1681
	cmp	r5, r3
	ldr	r3, [r10, #3844]
	add	r7, r3, r7
	ldreq	r3, [fp, #-80]
	streq	r3, [r7, #8]
	beq	.L1683
.L1712:
	mul	r6, r5, r6
	ldr	r3, [fp, #-80]
	sub	r6, r6, r9
	add	r6, r3, r6, lsl #9
	str	r6, [r7, #8]
	b	.L1683
.L1679:
	ldr	r3, [fp, #-84]
	add	r2, r9, r3
	smulbb	r3, r6, r5
	sub	r6, r2, r3
	mov	r3, #0
	uxth	r6, r6
	str	r3, [fp, #-88]
	b	.L1680
.L1681:
	cmp	r5, r3
	ldr	r3, [r10, #3844]
	ldreq	r2, [r10, #3820]
	ldrne	r2, [r10, #3812]
	add	r3, r3, r7
	str	r2, [r3, #8]
	ldr	r3, [fp, #-64]
	cmn	r3, #1
	beq	.L1686
	str	r3, [fp, #-56]
	mov	r1, #1
	ldr	r3, [r10, #3844]
	sub	r0, fp, #60
	str	r5, [fp, #-44]
	add	r3, r3, r7
	ldr	r2, [r3, #8]
	ldr	r3, [r3, #12]
	str	r2, [fp, #-52]
	mov	r2, #0
	str	r3, [fp, #-48]
	bl	FlashReadPages
	ldr	r3, [fp, #-60]
	cmn	r3, #1
	ldreq	r3, [r10, #2708]
	addeq	r3, r3, #1
	streq	r3, [r10, #2708]
	beq	.L1690
	ldr	r3, [r8, #8]
	cmp	r5, r3
	beq	.L1689
	ldr	r3, [r10, #2708]
	mov	r2, r5
	ldr	r0, .L1716+24
	add	r3, r3, #1
	str	r3, [r10, #2708]
	ldr	r1, [r8, #8]
	bl	sftl_printk
.L1689:
	ldr	r3, [r8, #8]
	cmp	r5, r3
	beq	.L1690
	movw	r2, #1077
	ldr	r1, .L1716+16
	ldr	r0, .L1716+20
	bl	sftl_printk
.L1690:
	ldr	r3, [fp, #-76]
	lsl	r2, r6, #9
	cmp	r5, r3
	bne	.L1691
	ldr	r3, [r10, #3844]
	ldr	r1, [fp, #-80]
	add	r7, r3, r7
	ldr	r3, [fp, #-88]
	ldr	r0, [r7, #8]
	add	r0, r0, r3, lsl #9
.L1713:
	bl	ftl_memcpy
.L1683:
	ldrb	r2, [r4, #6]	@ zero_extendqisi2
	ldrh	r3, [r10, #32]
	cmp	r2, r3
	bcc	.L1692
	movw	r2, #1092
	ldr	r1, .L1716+16
	ldr	r0, .L1716+20
	bl	sftl_printk
.L1692:
	ldr	r3, .L1716+28
	ldr	r2, [fp, #-104]
	ldr	r1, [fp, #-100]
	strh	r3, [r2, r1]	@ movhi
	ldr	r3, [r10, #2544]
	str	r5, [r8, #8]
	add	r5, r5, #1
	str	r3, [r8, #4]
	add	r3, r3, #1
	cmn	r3, #1
	moveq	r3, #0
	str	r3, [r10, #2544]
	ldr	r3, [fp, #-64]
	str	r3, [r8, #12]
	ldrh	r3, [r4]
	strh	r3, [r8, #2]	@ movhi
	ldr	r3, [fp, #-68]
	add	r3, r3, #1
	b	.L1715
.L1686:
	ldr	r3, [r10, #3844]
	mov	r1, #0
	ldrh	r2, [r10, #110]
	add	r3, r3, r7
	ldr	r0, [r3, #8]
	bl	ftl_memset
	b	.L1690
.L1691:
	ldrh	r1, [r10, #58]
	ldr	r3, [r10, #3844]
	mul	r1, r5, r1
	add	r7, r3, r7
	ldr	r3, [fp, #-80]
	ldr	r0, [r7, #8]
	sub	r1, r1, r9
	add	r1, r3, r1, lsl #9
	b	.L1713
.L1678:
	ldr	r3, [r10, #3844]
	add	r7, r3, r7
	b	.L1712
.L1698:
	ldr	r4, .L1716+32
	ldr	r5, .L1716+36
.L1710:
	ldrh	r3, [r4]
	movw	r2, #65535
	cmp	r3, r2
	bne	.L1699
	ldrh	r2, [r5]
	cmp	r2, r3
	bne	.L1699
	mov	r0, #0
	bl	List_get_gc_head_node
	uxth	r0, r0
	bl	FtlGcRefreshBlock
.L1699:
	ldr	r3, .L1716+40
	mov	r1, #1
	mov	r2, #128
	mov	r0, r1
	strh	r2, [r3]	@ movhi
	strh	r2, [r3, #-2]	@ movhi
	bl	rk_ftl_garbage_collect
	mov	r1, #1
	mov	r0, #0
	bl	rk_ftl_garbage_collect
	ldr	r3, .L1716+44
	ldrh	r3, [r3]
	cmp	r3, #8
	bls	.L1710
	b	.L1700
.L1717:
	.align	2
.L1716:
	.word	.LANCHOR0
	.word	.LANCHOR2
	.word	.LANCHOR3
	.word	.LANCHOR0+2348
	.word	.LANCHOR1+574
	.word	.LC1
	.word	.LC114
	.word	-3947
	.word	.LANCHOR0+2588
	.word	.LANCHOR0+3202
	.word	.LANCHOR0+3150
	.word	.LANCHOR0+2344
	.size	FtlWrite, .-FtlWrite
	.global	__modsi3
	.align	2
	.global	sftl_write
	.syntax unified
	.arm
	.fpu softvfp
	.type	sftl_write, %function
sftl_write:
	@ args = 0, pretend = 0, frame = 116
	@ frame_needed = 1, uses_anonymous_args = 0
	mov	ip, sp
	push	{r4, r5, r6, r7, r8, r9, r10, fp, ip, lr, pc}
	sub	fp, ip, #4
	sub	sp, sp, #120
	add	ip, r0, r1
	sub	lr, ip, #1
	mov	r3, r1
	cmp	lr, #63
	mov	r10, r0
	movhi	r3, #0
	movls	r3, #1
	cmp	r0, #576
	orrhi	r3, r3, #1
	str	r1, [fp, #-112]
	cmp	r3, #0
	str	r2, [fp, #-132]
	ldr	r4, .L1749
	beq	.L1719
	ldr	r3, [r4, #-4012]
	cmp	r3, #0
	beq	.L1720
	ldr	r2, .L1749+4
	mov	r0, #512
	ldrh	r3, [r2, #14]
	str	r3, [fp, #-116]
	ldrh	r3, [r2, #10]
	ldrb	r1, [fp, #-116]	@ zero_extendqisi2
	smulbb	r3, r3, r1
	uxth	r3, r3
	mov	r1, r3
	str	r3, [fp, #-120]
	bl	__udivsi3
	ldr	r2, .L1749+8
	mov	r3, #0
	ldr	r1, .L1749+12
	str	r0, [fp, #-108]
	sub	ip, r2, #262144
.L1724:
	ldr	r0, [r2, #-4]
	cmp	r0, #0
	beq	.L1721
.L1725:
	ldr	r3, [fp, #-108]
	mov	r4, #0
	ldr	r2, [fp, #-120]
	ldr	r9, .L1749+16
	ldr	r5, .L1749+4
	str	r4, [fp, #-124]
	mul	r3, r3, r2
	str	r3, [fp, #-144]
	ldr	r3, [fp, #-108]
	str	r3, [fp, #-128]
.L1722:
	mov	r1, #512
	mov	r0, r9
	bl	__memzero
	ldrh	r8, [r5, #14]
	mov	r0, r4
	ldrh	r6, [r5, #10]
	mov	r1, r8
	uxtb	r7, r8
	uxth	r3, r7
	str	r3, [fp, #-136]
	ldrh	r3, [fp, #-136]
	smulbb	r6, r6, r3
	bl	__udivsi3
	uxth	r6, r6
	mov	r1, r0
	ldr	r3, [r5, #3252]
	mov	r0, #0
	blx	r3
	cmp	r6, #512
	bcs	.L1726
	mov	r1, r8
	add	r0, r6, r4
	bl	__udivsi3
	ldr	r3, [r5, #3252]
	mov	r1, r0
	mov	r0, #0
	blx	r3
.L1726:
	mov	r1, r6
	mov	r0, r4
	bl	__umodsi3
	mov	r6, #0
	mov	r8, r0
	sub	r3, r4, r0
	str	r3, [fp, #-140]
.L1727:
	cmp	r6, #512
	bcc	.L1728
	ldrb	r7, [r5, #14]	@ zero_extendqisi2
	mov	r0, r4
	ldrh	r1, [r5, #10]
	mov	r6, #0
	uxth	r3, r7
	str	r3, [fp, #-136]
	ldrh	r3, [fp, #-136]
	smulbb	r1, r1, r3
	uxth	r1, r1
	bl	__umodsi3
	mov	r1, r7
	mov	ip, r0
	sub	r3, r4, r0
	uxth	r0, r0
	str	r3, [fp, #-140]
	str	ip, [fp, #-148]
	bl	__modsi3
	ldr	ip, [fp, #-148]
	uxth	r3, r0
.L1729:
	cmp	r6, #512
	bcs	.L1733
	ldr	r2, [fp, #-136]
	add	r0, ip, r6
	mov	r1, r7
	str	ip, [fp, #-148]
	str	r3, [fp, #-156]
	sub	r8, r2, r3
	add	r2, r9, r6, lsl #9
	uxth	r8, r8
	str	r2, [fp, #-152]
	bl	__udivsi3
	ldr	r2, [fp, #-140]
	uxth	r0, r0
	ldr	r3, [fp, #-156]
	mov	r1, r7
	add	r3, r3, r2
	mla	r0, r7, r0, r3
	bl	__udivsi3
	ldr	ip, [r5, #3260]
	mov	r1, r0
	sub	r3, fp, #104
	ldr	r2, [fp, #-152]
	mov	r0, #0
	blx	ip
	cmn	r0, #1
	ldr	ip, [fp, #-148]
	bne	.L1730
.L1733:
	ldr	r3, .L1749+20
	mov	r6, #0
.L1731:
	mov	r7, r3
	ldr	r1, [r9, r6, lsl #2]
	ldr	r2, [r7]
	add	r3, r3, #4
	cmp	r1, r2
	beq	.L1734
	mov	r1, #512
	mov	r0, r9
	bl	__memzero
	ldr	r2, [r9, r6, lsl #2]
	mov	r1, r4
	str	r6, [sp]
	ldr	r0, .L1749+24
	ldr	r3, [r7]
	bl	sftl_printk
	ldr	r1, [fp, #-116]
	mov	r0, r4
	bl	__udivsi3
	ldr	r3, [r5, #3252]
	mov	r1, r0
	mov	r0, #0
	blx	r3
	ldr	r3, [fp, #-108]
	cmp	r3, #1
	bls	.L1735
	ldr	r3, [fp, #-120]
	ldr	r1, [fp, #-116]
	add	r0, r3, r4
	bl	__udivsi3
	ldr	r3, [r5, #3252]
	mov	r1, r0
	mov	r0, #0
	blx	r3
.L1735:
	ldr	r2, [fp, #-128]
	ldr	r3, [fp, #-108]
	add	r3, r3, r2
	ldr	r2, [fp, #-144]
	add	r4, r4, r2
	ldr	r2, [fp, #-128]
	cmp	r2, #15
	bls	.L1741
.L1740:
	ldr	r3, .L1749
	mov	r2, #0
	str	r2, [r3, #-4012]
.L1720:
	ldr	r3, [fp, #-132]
	mov	r1, r10
	ldr	r2, [fp, #-112]
	mov	r0, #0
	bl	FtlWrite
	sub	sp, fp, #40
	ldmfd	sp, {r4, r5, r6, r7, r8, r9, r10, fp, sp, pc}
.L1721:
	ldr	r0, [ip, r3, lsl #2]
	add	r3, r3, #1
	cmp	r3, #4096
	movhi	r3, #0
	str	r0, [r2, #-4]!
	cmp	r1, r2
	bne	.L1724
	b	.L1725
.L1741:
	str	r3, [fp, #-128]
	b	.L1722
.L1728:
	mov	r1, r7
	add	r0, r8, r6
	bl	__udivsi3
	uxth	ip, r0
	mov	r1, r7
	ldr	r0, [fp, #-140]
	mul	r3, r7, ip
	str	ip, [fp, #-152]
	str	r3, [fp, #-104]
	movw	r3, #61424
	str	r3, [fp, #-100]
	ldr	r3, .L1749+20
	add	r2, r3, r6, lsl #9
	str	r2, [fp, #-148]
	bl	__udivsi3
	ldr	ip, [fp, #-152]
	sub	r3, fp, #104
	ldr	r2, [fp, #-148]
	add	r1, r0, ip
	mov	r0, #0
	ldr	ip, [r5, #3256]
	blx	ip
	ldr	r3, [fp, #-136]
	add	r6, r3, r6
	uxth	r6, r6
	b	.L1727
.L1730:
	add	r6, r8, r6
	mov	r3, #0
	uxth	r6, r6
	b	.L1729
.L1734:
	add	r6, r6, #1
	cmp	r6, #65536
	bne	.L1731
	ldr	r3, [fp, #-124]
	add	r3, r3, #1
	cmp	r3, #5
	str	r3, [fp, #-124]
	bls	.L1735
	b	.L1740
.L1719:
	cmp	r0, #63
	ldr	r0, .L1749+20
	ldrls	r3, [fp, #-112]
	rsbls	r1, r10, #64
	ldrhi	r3, .L1749+28
	ldrhi	r2, [fp, #-112]
	subls	r2, r3, r1
	ldrls	r3, [fp, #-132]
	addhi	r3, r10, r3
	ldrhi	r1, [fp, #-132]
	addhi	r0, r0, r3, lsl #9
	addls	r1, r3, r1, lsl #9
	cmp	lr, #576
	subhi	r2, r2, ip
	subhi	r2, r2, #444
	mov	r3, #1
	subhi	r2, r2, #3
	str	r3, [r4, #-4012]
	lsl	r2, r2, #9
	bl	memcpy
	b	.L1720
.L1750:
	.align	2
.L1749:
	.word	.LANCHOR3
	.word	.LANCHOR0
	.word	idb_buf+262144
	.word	idb_buf+16388
	.word	gp_flash_check_buf
	.word	idb_buf
	.word	.LC115
	.word	8388544
	.size	sftl_write, .-sftl_write
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
	.global	FtlLoadSysInfo
	.syntax unified
	.arm
	.fpu softvfp
	.type	FtlLoadSysInfo, %function
FtlLoadSysInfo:
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 1, uses_anonymous_args = 0
	mov	ip, sp
	push	{r4, r5, r6, r7, r8, fp, ip, lr, pc}
	sub	fp, ip, #4
	ldr	r4, .L1773
	mov	r1, #0
	ldr	r3, [r4, #3284]
	add	r6, r4, #2576
	ldrh	r2, [r4, #40]
	ldr	r0, [r4, #2324]
	str	r3, [r4, #3272]
	ldr	r3, [r4, #3288]
	lsl	r2, r2, #1
	str	r3, [r4, #3276]
	bl	ftl_memset
	ldrh	r0, [r6]
	movw	r3, #65535
	cmp	r0, r3
	bne	.L1753
.L1761:
	mvn	r0, #0
	ldmfd	sp, {r4, r5, r6, r7, r8, fp, sp, pc}
.L1753:
	ldr	r8, .L1773+4
	mov	r1, #1
	add	r7, r4, #3264
	bl	FtlGetLastWrittenPage
	sxth	r5, r0
	add	r0, r0, #1
	strh	r0, [r6, #2]	@ movhi
.L1755:
	cmp	r5, #0
	bge	.L1758
	movw	r2, #1482
	ldr	r1, .L1773+8
	ldr	r0, .L1773+12
	bl	sftl_printk
.L1757:
	ldrh	r3, [r4, #40]
	ldrh	r2, [r4, #110]
	add	r3, r3, #24
	cmp	r2, r3, lsl #1
	bcs	.L1760
	movw	r2, #1484
	ldr	r1, .L1773+8
	ldr	r0, .L1773+12
	bl	sftl_printk
.L1760:
	ldr	r5, .L1773+16
	mov	r2, #48
	ldr	r1, [r4, #3272]
	mov	r0, r5
	bl	ftl_memcpy
	ldrh	r2, [r4, #40]
	ldr	r1, [r4, #3272]
	ldr	r0, [r4, #2324]
	lsl	r2, r2, #1
	add	r1, r1, #48
	bl	ftl_memcpy
	ldrh	r1, [r4, #40]
	ldr	r3, [r4, #3272]
	ldr	r0, [r4, #24]
	lsr	r2, r1, #3
	add	r1, r1, #24
	lsl	r1, r1, #1
	add	r2, r2, #4
	bic	r1, r1, #3
	add	r1, r3, r1
	bl	ftl_memcpy
	ldr	r2, [r4, #2268]
	ldr	r3, .L1773+4
	cmp	r2, r3
	bne	.L1761
	ldrb	r2, [r4, #2278]	@ zero_extendqisi2
	ldrh	r3, [r4, #54]
	ldrh	r7, [r5, #8]
	cmp	r2, r3
	strh	r7, [r6, #6]	@ movhi
	bne	.L1761
	ldr	r3, .L1773+20
	ldrh	r2, [r4, #58]
	ldr	r6, [r4, #44]
	str	r7, [r3, #-4008]
	ldrh	r3, [r4, #102]
	ldrh	r0, [r4, #158]
	ldrh	r1, [r4, #32]
	mul	r3, r7, r3
	sub	r0, r6, r0
	sub	r0, r0, r7
	str	r3, [r4, #2504]
	mul	r3, r3, r2
	str	r3, [r4, #140]
	bl	__udivsi3
	cmp	r7, r6
	movw	r3, #2572
	strh	r0, [r4, r3]	@ movhi
	bls	.L1762
	movw	r2, #1506
	ldr	r1, .L1773+8
	ldr	r0, .L1773+12
	bl	sftl_printk
.L1762:
	ldrh	r0, [r5, #14]
	movw	r3, #2348
	ldr	r1, .L1773+24
	mvn	ip, #0
	strh	r0, [r4, r3]	@ movhi
	ldrh	r3, [r5, #16]
	lsr	r2, r3, #6
	and	r3, r3, #63
	strb	r3, [r4, #2354]
	ldrb	r3, [r4, #2279]	@ zero_extendqisi2
	strh	r2, [r1, #2]	@ movhi
	movw	r2, #2396
	strb	r3, [r4, #2356]
	movw	r3, #2588
	strh	ip, [r4, r3]	@ movhi
	mov	r3, #0
	ldrh	ip, [r5, #18]
	strh	r3, [r1, #242]	@ movhi
	strb	r3, [r4, #2594]
	strh	ip, [r4, r2]	@ movhi
	ldrh	r2, [r5, #20]
	strb	r3, [r4, #2596]
	str	r3, [r4, #2528]
	str	r3, [r4, #2516]
	lsr	ip, r2, #6
	and	r2, r2, #63
	strb	r2, [r4, #2402]
	ldrb	r2, [r4, #2280]	@ zero_extendqisi2
	strh	ip, [r1, #50]	@ movhi
	ldrh	ip, [r5, #22]
	strb	r2, [r4, #2404]
	movw	r2, #2444
	str	r3, [r4, #2508]
	strh	ip, [r4, r2]	@ movhi
	ldrh	r2, [r5, #24]
	str	r3, [r4, #2524]
	str	r3, [r4, #2552]
	lsr	ip, r2, #6
	and	r2, r2, #63
	strb	r2, [r4, #2450]
	ldrb	r2, [r4, #2281]	@ zero_extendqisi2
	strh	ip, [r1, #98]	@ movhi
	strb	r2, [r4, #2452]
	ldr	r2, [r4, #2300]
	str	r2, [r4, #2548]
	str	r3, [r4, #2564]
	ldr	r2, [r4, #2540]
	str	r3, [r4, #2520]
	ldr	r3, [r4, #2308]
	cmp	r3, r2
	ldr	r2, [r4, #2544]
	strhi	r3, [r4, #2540]
	ldr	r3, [r4, #2304]
	cmp	r3, r2
	strhi	r3, [r4, #2544]
	movw	r3, #65535
	cmp	r0, r3
	beq	.L1765
	ldr	r0, .L1773+24
	bl	make_superblock
.L1765:
	movw	r3, #2396
	ldrh	r2, [r4, r3]
	movw	r3, #65535
	cmp	r2, r3
	beq	.L1766
	ldr	r0, .L1773+28
	bl	make_superblock
.L1766:
	movw	r3, #2444
	ldrh	r2, [r4, r3]
	movw	r3, #65535
	cmp	r2, r3
	beq	.L1767
	ldr	r0, .L1773+32
	bl	make_superblock
.L1767:
	movw	r3, #2588
	ldrh	r2, [r4, r3]
	movw	r3, #65535
	cmp	r2, r3
	beq	.L1768
	ldr	r0, .L1773+36
	bl	make_superblock
.L1768:
	mov	r0, #0
	ldmfd	sp, {r4, r5, r6, r7, r8, fp, sp, pc}
.L1758:
	ldrh	r3, [r6]
	mov	r2, #1
	mov	r1, r2
	mov	r0, r7
	orr	r3, r5, r3, lsl #10
	str	r3, [r4, #3268]
	ldr	r3, [r4, #3284]
	str	r3, [r4, #3272]
	bl	FlashReadPages
	ldr	r3, [r4, #3264]
	cmn	r3, #1
	beq	.L1756
	ldr	r3, [r4, #3284]
	ldr	r3, [r3]
	cmp	r3, r8
	bne	.L1756
	ldr	r3, [r4, #3288]
	ldrh	r2, [r3]
	movw	r3, #61604
	cmp	r2, r3
	beq	.L1757
.L1756:
	sub	r5, r5, #1
	sxth	r5, r5
	b	.L1755
.L1774:
	.align	2
.L1773:
	.word	.LANCHOR0
	.word	1179929683
	.word	.LANCHOR1+583
	.word	.LC1
	.word	.LANCHOR0+2268
	.word	.LANCHOR3
	.word	.LANCHOR0+2348
	.word	.LANCHOR0+2396
	.word	.LANCHOR0+2444
	.word	.LANCHOR0+2588
	.size	FtlLoadSysInfo, .-FtlLoadSysInfo
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
	push	{r4, r5, r6, r7, r8, fp, ip, lr, pc}
	sub	fp, ip, #4
	ldr	r4, .L1799
	mov	r3, #0
	add	r6, r4, #4016
	ldrh	r0, [r4, #36]
	strh	r3, [r6]	@ movhi
	bl	FtlFreeSysBlkQueueInit
	bl	FtlScanSysBlk
	add	r3, r4, #2576
	ldrh	r2, [r3]
	movw	r3, #65535
	cmp	r2, r3
	bne	.L1776
.L1778:
	mvn	r8, #0
.L1775:
	mov	r0, r8
	ldmfd	sp, {r4, r5, r6, r7, r8, fp, sp, pc}
.L1776:
	bl	FtlLoadSysInfo
	subs	r8, r0, #0
	bne	.L1778
	bl	FtlLoadMapInfo
	bl	FtlLoadVonderInfo
	bl	Ftl_load_ext_data
	bl	FtlLoadEctTbl
	bl	FtlFreeSysBLkSort
	bl	SupperBlkListInit
	bl	FtlPowerLostRecovery
	mov	r0, #1
	bl	FtlUpdateVaildLpn
	ldrh	r1, [r4, #138]
	mov	r3, r8
	ldr	r2, [r4, #2492]
	mov	r0, #12
.L1779:
	cmp	r3, r1
	bge	.L1784
	mla	ip, r0, r3, r2
	ldr	ip, [ip, #4]
	cmp	ip, #0
	bge	.L1780
.L1784:
	ldr	r5, .L1799+4
	cmp	r3, r1
	ldrh	r2, [r5, #28]
	add	r7, r5, #80
	add	r2, r2, #1
	strh	r2, [r5, #28]	@ movhi
	bge	.L1798
.L1781:
	movw	r3, #2348
	ldr	r1, [r4, #2324]
	ldrh	r3, [r4, r3]
	ldrh	r0, [r7, #4]
	lsl	r3, r3, #1
	ldrh	r2, [r1, r3]
	sub	r2, r2, r0
	strh	r2, [r1, r3]	@ movhi
	movw	r2, #2396
	ldrh	r2, [r4, r2]
	ldrh	r3, [r4, #102]
	ldr	r1, .L1799+8
	ldr	ip, [r4, #2324]
	lsl	r2, r2, #1
	strh	r3, [r7, #2]	@ movhi
	mov	r3, #0
	ldrh	lr, [r1, #4]
	strb	r3, [r4, #2354]
	strh	r3, [r7, #4]	@ movhi
	ldrh	r0, [ip, r2]
	sub	r0, r0, lr
	strh	r0, [ip, r2]	@ movhi
	ldrh	r2, [r4, #102]
	ldr	r0, .L1799+12
	strh	r3, [r1, #4]	@ movhi
	strh	r2, [r1, #2]	@ movhi
	strb	r3, [r4, #2402]
	bl	FtlMapBlkWriteDump_data
	ldr	r0, .L1799+16
	bl	FtlMapBlkWriteDump_data
	ldrh	r3, [r5, #30]
	add	r3, r3, #1
	strh	r3, [r5, #30]	@ movhi
	bl	l2p_flush
	bl	FtlVpcTblFlush
	bl	FtlVpcTblFlush
	b	.L1785
.L1780:
	add	r3, r3, #1
	b	.L1779
.L1798:
	ldrh	r3, [r6]
	cmp	r3, #0
	bne	.L1781
.L1785:
	ldrh	r6, [r5, #44]
	movw	r2, #65533
	sub	r3, r6, #1
	uxth	r3, r3
	cmp	r3, r2
	bhi	.L1787
	movw	r3, #3202
	mov	r1, r6
	ldr	r0, .L1799+20
	strh	r6, [r4, r3]	@ movhi
	bl	test_node_in_list
	cmp	r0, #0
	bne	.L1788
	ldrh	r3, [r4, #40]
	cmp	r3, r6
	bls	.L1788
	ldr	r3, [r4, #2324]
	lsl	r6, r6, #1
	ldrh	r3, [r3, r6]
	cmp	r3, #0
	bne	.L1787
.L1788:
	mvn	r2, #0
	movw	r3, #3202
	strh	r2, [r4, r3]	@ movhi
.L1787:
	ldrh	r6, [r5, #46]
	movw	r2, #65533
	sub	r3, r6, #1
	uxth	r3, r3
	cmp	r3, r2
	bhi	.L1791
	movw	r3, #3204
	mov	r1, r6
	ldr	r0, .L1799+20
	strh	r6, [r4, r3]	@ movhi
	bl	test_node_in_list
	cmp	r0, #0
	bne	.L1792
	ldrh	r3, [r4, #40]
	cmp	r3, r6
	bls	.L1792
	ldr	r3, [r4, #2324]
	lsl	r6, r6, #1
	ldrh	r3, [r3, r6]
	cmp	r3, #0
	bne	.L1791
.L1792:
	mvn	r2, #0
	movw	r3, #3204
	strh	r2, [r4, r3]	@ movhi
.L1791:
	movw	r3, #2348
	ldrh	r0, [r4, r3]
	movw	r3, #65535
	cmp	r0, r3
	beq	.L1794
	ldrh	r3, [r7, #4]
	cmp	r3, #0
	bne	.L1794
	ldr	r6, .L1799+8
	ldrh	r3, [r6, #4]
	cmp	r3, #0
	bne	.L1794
	bl	FtlGcRefreshBlock
	movw	r3, #2396
	ldrh	r0, [r4, r3]
	bl	FtlGcRefreshBlock
	bl	FtlVpcTblFlush
	sub	r0, r6, #48
	bl	allocate_new_data_superblock
	mov	r0, r6
	bl	allocate_new_data_superblock
.L1794:
	ldrh	r3, [r5, #28]
	tst	r3, #31
	bne	.L1775
	bl	FtlVpcCheckAndModify
	b	.L1775
.L1800:
	.align	2
.L1799:
	.word	.LANCHOR0
	.word	.LANCHOR0+2268
	.word	.LANCHOR0+2396
	.word	.LANCHOR0+3908
	.word	.LANCHOR0+3956
	.word	.LANCHOR0+2340
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
	ldr	r4, .L1825
	mov	r3, #0
	ldrh	r0, [r4, #36]
	str	r3, [r4, #2540]
	str	r3, [r4, #2544]
	bl	FtlFreeSysBlkQueueInit
	bl	FtlLoadBbt
	cmp	r0, #0
	beq	.L1802
	bl	FtlMakeBbt
.L1802:
	ldr	r0, .L1825+4
	mov	r2, #0
.L1803:
	ldrh	r1, [r4, #58]
	uxth	r3, r2
	add	r2, r2, #1
	cmp	r3, r1, lsl #7
	blt	.L1804
	ldrh	r6, [r4, #40]
	mov	r5, #0
.L1805:
	ldrh	r3, [r4, #42]
	cmp	r3, r6
	bhi	.L1806
	ldrh	r1, [r4, #32]
	sub	r3, r5, #3
	cmp	r3, r1, lsl #1
	bge	.L1807
.L1811:
	mov	r0, #0
	mov	r6, r0
.L1808:
	ldrh	r2, [r4, #40]
	uxth	r3, r0
	add	r5, r0, #1
	cmp	r2, r3
	bhi	.L1812
	ldrh	r3, [r4, #42]
	movw	r9, #2572
	ldrh	r5, [r4, #32]
	ldr	r10, [r4, #44]
	str	r3, [r4, #3808]
	mov	r1, r5
	mov	r0, r10
	bl	__udivsi3
	ubfx	r7, r0, #5, #16
	mov	r8, r0
	add	r3, r7, #36
	str	r0, [r4, #2504]
	strh	r3, [r4, r9]	@ movhi
	mov	r3, #24
	mul	r3, r3, r5
	cmp	r6, r3
	ble	.L1813
	mov	r1, r5
	sub	r0, r10, r6
	bl	__udivsi3
	str	r0, [r4, #2504]
	lsr	r0, r0, #5
	add	r0, r0, #24
	strh	r0, [r4, r9]	@ movhi
.L1813:
	ldrh	r3, [r4, #94]
	cmp	r3, #0
	beq	.L1815
	movw	r2, #2572
	ldrh	r1, [r4, r2]
	add	r1, r1, r3, lsr #1
	strh	r1, [r4, r2]	@ movhi
	mul	r1, r5, r3
	cmp	r6, r1
	addlt	r3, r3, #32
	strlt	r8, [r4, #2504]
	addlt	r3, r7, r3
	strhlt	r3, [r4, r2]	@ movhi
.L1815:
	movw	r3, #2572
	ldr	r6, .L1825+8
	ldrh	r2, [r4, r3]
	mvn	r7, #0
	ldr	r3, [r4, #2504]
	sub	r3, r3, r2
	mul	r5, r5, r3
	ldrh	r3, [r4, #102]
	str	r5, [r6, #-4008]
	mul	r5, r5, r3
	ldrh	r3, [r4, #58]
	str	r5, [r4, #2504]
	mul	r5, r5, r3
	str	r5, [r4, #140]
	bl	FtlBbmTblFlush
	ldrh	r2, [r4, #42]
	mov	r1, #0
	ldr	r0, [r4, #2324]
	ldr	r5, .L1825+12
	lsl	r2, r2, #1
	sub	r5, r5, #240
	bl	ftl_memset
	movw	r2, #2588
	mov	r3, #0
	strh	r7, [r4, r2]	@ movhi
	movw	r2, #2348
	strh	r3, [r4, r2]	@ movhi
	mov	r1, #255
	ldrh	r2, [r4, #40]
	strh	r3, [r5, #242]	@ movhi
	mov	r8, r5
	str	r3, [r4, #2500]
	strb	r3, [r4, #2594]
	strb	r3, [r4, #2596]
	lsr	r2, r2, #3
	strh	r3, [r5, #2]	@ movhi
	strb	r3, [r4, #2354]
	mov	r3, #1
	ldr	r0, [r4, #24]
	strb	r3, [r4, #2356]
	bl	ftl_memset
.L1817:
	mov	r0, r8
	bl	make_superblock
	ldrb	r3, [r4, #2355]	@ zero_extendqisi2
	cmp	r3, #0
	ldrh	r3, [r5]
	bne	.L1818
	ldr	r2, [r4, #2324]
	lsl	r3, r3, #1
	strh	r7, [r2, r3]	@ movhi
	ldrh	r3, [r5]
	add	r3, r3, #1
	strh	r3, [r5]	@ movhi
	b	.L1817
.L1804:
	ldr	ip, [r4, #3820]
	mvn	r1, r3
	orr	r1, r3, r1, lsl #16
	str	r1, [ip, r3, lsl #2]
	ldr	r1, [r4, #3812]
	str	r0, [r1, r3, lsl #2]
	b	.L1803
.L1806:
	mov	r0, r6
	mov	r1, #1
	bl	FtlLowFormatEraseBlock
	add	r6, r6, #1
	add	r5, r5, r0
	uxth	r5, r5
	uxth	r6, r6
	b	.L1805
.L1807:
	mov	r0, r5
	bl	__udivsi3
	ldr	r3, [r4, #132]
	add	r0, r0, r3
	uxth	r0, r0
	bl	FtlSysBlkNumInit
	ldrh	r0, [r4, #36]
	bl	FtlFreeSysBlkQueueInit
	ldrh	r5, [r4, #40]
.L1809:
	ldrh	r3, [r4, #42]
	cmp	r3, r5
	bls	.L1811
	mov	r0, r5
	mov	r1, #1
	add	r5, r5, #1
	bl	FtlLowFormatEraseBlock
	uxth	r5, r5
	b	.L1809
.L1812:
	mov	r1, #0
	uxth	r0, r0
	bl	FtlLowFormatEraseBlock
	add	r6, r6, r0
	mov	r0, r5
	uxth	r6, r6
	b	.L1808
.L1818:
	ldr	r2, [r4, #2540]
	lsl	r3, r3, #1
	ldrh	r1, [r5, #4]
	mvn	r7, #0
	str	r2, [r4, #2360]
	add	r2, r2, #1
	str	r2, [r4, #2540]
	ldr	r2, [r4, #2324]
	strh	r1, [r2, r3]	@ movhi
	mov	r3, #0
	ldr	r2, .L1825+16
	movw	r1, #2396
	strb	r3, [r4, #2402]
	strh	r3, [r2, #2]	@ movhi
	mov	r8, r2
	ldrh	r3, [r5]
	mov	r5, r2
	add	r3, r3, #1
	strh	r3, [r4, r1]	@ movhi
	mov	r3, #1
	strb	r3, [r4, #2404]
.L1819:
	mov	r0, r8
	bl	make_superblock
	ldrb	r3, [r4, #2403]	@ zero_extendqisi2
	cmp	r3, #0
	ldrh	r3, [r5]
	bne	.L1820
	ldr	r2, [r4, #2324]
	lsl	r3, r3, #1
	strh	r7, [r2, r3]	@ movhi
	ldrh	r3, [r5]
	add	r3, r3, #1
	strh	r3, [r5]	@ movhi
	b	.L1819
.L1820:
	ldr	r2, [r4, #2540]
	lsl	r3, r3, #1
	ldrh	r1, [r5, #4]
	mvn	r5, #0
	str	r2, [r4, #2408]
	add	r2, r2, #1
	str	r2, [r4, #2540]
	ldr	r2, [r4, #2324]
	strh	r1, [r2, r3]	@ movhi
	movw	r3, #2444
	strh	r5, [r4, r3]	@ movhi
	bl	FtlFreeSysBlkQueueOut
	ldr	r3, .L1825+20
	mov	r2, #0
	strh	r2, [r3, #2]	@ movhi
	ldr	r2, [r6, #-4008]
	strh	r5, [r3, #4]	@ movhi
	strh	r0, [r3]	@ movhi
	strh	r2, [r3, #6]	@ movhi
	ldr	r3, [r4, #2540]
	str	r3, [r4, #2584]
	add	r3, r3, #1
	str	r3, [r4, #2540]
	bl	FtlVpcTblFlush
	bl	FtlSysBlkInit
	cmp	r0, #0
	mov	r0, #0
	ldreq	r3, .L1825+24
	moveq	r2, #1
	streq	r2, [r3]
	ldmfd	sp, {r4, r5, r6, r7, r8, r9, r10, fp, sp, pc}
.L1826:
	.align	2
.L1825:
	.word	.LANCHOR0
	.word	168778952
	.word	.LANCHOR3
	.word	.LANCHOR0+2588
	.word	.LANCHOR0+2396
	.word	.LANCHOR0+2576
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
	ldr	r4, .L1833
	mvn	r3, #0
	ldr	r5, .L1833+4
	ldr	r1, .L1833+8
	ldr	r0, .L1833+12
	str	r3, [r5]
	bl	sftl_printk
	mov	r0, r4
	bl	FtlConstantsInit
	bl	FtlMemInit
	bl	FtlVariablesInit
	ldrh	r0, [r4, #36]
	bl	FtlFreeSysBlkQueueInit
	bl	FtlLoadBbt
	cmp	r0, #0
	bne	.L1831
	bl	FtlSysBlkInit
	cmp	r0, #0
	bne	.L1831
	mov	r3, #1
	str	r3, [r5]
	movw	r3, #2344
	ldrh	r3, [r4, r3]
	cmp	r3, #15
	bhi	.L1831
	movw	r4, #8129
.L1830:
	mov	r1, #1
	mov	r0, #0
	bl	rk_ftl_garbage_collect
	subs	r4, r4, #1
	bne	.L1830
.L1831:
	mov	r0, #0
	ldmfd	sp, {r4, r5, fp, sp, pc}
.L1834:
	.align	2
.L1833:
	.word	.LANCHOR0
	.word	.LANCHOR2
	.word	.LC70
	.word	.LC71
	.size	sftl_init, .-sftl_init
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
	ldr	r2, .L1839
	ldr	r3, [r2, #-4004]
	cmp	r3, #0
	streq	r0, [r2, #-4004]
	moveq	r0, r3
	streq	r1, [r2, #-4000]
	mvnne	r0, #0
	ldmfd	sp, {fp, sp, pc}
.L1840:
	.align	2
.L1839:
	.word	.LANCHOR3
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
	ldr	r4, .L1850
	bl	ftl_malloc
	cmp	r0, #0
	str	r0, [r4, #-3996]
	mvneq	r9, #11
	beq	.L1841
	ldr	r8, .L1850+4
	mov	r6, #0
	mov	r5, r6
	mov	r7, r6
.L1845:
	ldr	r3, [r4, #-4004]
	mov	r1, #128
	ldr	r2, [r4, #-3996]
	lsl	r0, r7, #7
	blx	r3
	subs	r9, r0, #0
	bne	.L1843
	ldr	r2, [r4, #-3996]
	ldr	r3, [r2]
	cmp	r3, r8
	bne	.L1844
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
.L1844:
	add	r7, r7, #1
	cmp	r7, #2
	bne	.L1845
	cmp	r5, #0
	beq	.L1846
	ldr	r3, [r4, #-4004]
	mov	r1, #128
	lsl	r0, r6, #7
	blx	r3
	subs	r9, r0, #0
	beq	.L1841
.L1843:
	ldr	r0, [r4, #-3996]
	mvn	r9, #0
	bl	kfree
	mov	r3, #0
	str	r3, [r4, #-3996]
	b	.L1841
.L1846:
	mov	r1, #65536
	mov	r0, r2
	bl	__memzero
	ldr	r3, [r4, #-3996]
	mov	r2, #1
	add	r1, r3, #61440
	str	r2, [r3, #4]
	str	r8, [r3]
	str	r2, [r1, #4092]
	ldr	r2, .L1850+8
	strh	r5, [r3, #12]	@ movhi
	strh	r2, [r3, #14]	@ movhi
.L1841:
	mov	r0, r9
	ldmfd	sp, {r4, r5, r6, r7, r8, r9, fp, sp, pc}
.L1851:
	.align	2
.L1850:
	.word	.LANCHOR3
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
	ldr	r3, .L1858
	mov	r4, r0
	mov	r0, r1
	ldr	ip, [r3, #-3996]
	cmp	ip, #0
	ldrhne	lr, [ip, #10]
	movne	r3, #0
	bne	.L1854
.L1857:
	mvn	r0, #0
	ldmfd	sp, {r4, r5, fp, sp, pc}
.L1855:
	add	r3, r3, #1
.L1854:
	cmp	r3, lr
	bcs	.L1857
	add	r1, ip, r3, lsl #3
	ldrh	r5, [r1, #16]
	cmp	r5, r4
	bne	.L1855
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
.L1859:
	.align	2
.L1858:
	.word	.LANCHOR3
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
	ldr	r8, .L1879
	mov	r9, r2
	ldr	r4, [r8, #-3996]
	cmp	r4, #0
	beq	.L1875
	ldrh	r2, [r4, #10]
	add	r7, r9, #63
	ldrh	r3, [r4, #8]
	bic	r7, r7, #63
	mov	r5, #0
	str	r3, [fp, #-44]
.L1862:
	cmp	r5, r2
	bcc	.L1870
	ldrh	r1, [r4, #14]
	cmp	r7, r1
	bhi	.L1875
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
	ldr	r3, [r8, #-4000]
	b	.L1878
.L1870:
	add	r6, r4, r5, lsl #3
	ldrh	r3, [r6, #16]
	cmp	r3, r0
	str	r3, [fp, #-48]
	bne	.L1863
	ldrh	r1, [r6, #20]
	add	r3, r4, #1024
	add	r1, r1, #63
	bic	r1, r1, #63
	cmp	r9, r1
	str	r1, [fp, #-52]
	bls	.L1864
	ldrh	r1, [r4, #14]
	cmp	r7, r1
	bhi	.L1875
	ldrh	r8, [r6, #18]
	sub	r2, r2, #1
	str	r2, [fp, #-56]
.L1865:
	ldr	r2, [fp, #-56]
	add	r6, r6, #8
	cmp	r5, r2
	bcc	.L1866
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
.L1867:
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
	ldr	r3, .L1879
	ldr	r3, [r3, #-4000]
.L1878:
	ldr	r0, [fp, #-44]
	mov	r2, r4
	mov	r1, #128
	lsl	r0, r0, #7
	blx	r3
	mov	r0, #0
.L1860:
	sub	sp, fp, #40
	ldmfd	sp, {r4, r5, r6, r7, r8, r9, r10, fp, sp, pc}
.L1866:
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
	b	.L1865
.L1864:
	add	r5, r4, r5, lsl #3
	mov	r2, r9
	mov	r1, ip
	ldrh	r0, [r5, #18]
	add	r0, r3, r0
	bl	memcpy
	strh	r9, [r6, #20]	@ movhi
	b	.L1867
.L1863:
	add	r5, r5, #1
	b	.L1862
.L1875:
	mvn	r0, #0
	b	.L1860
.L1880:
	.align	2
.L1879:
	.word	.LANCHOR3
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
	beq	.L1881
	ldr	r3, .L1907
	cmp	r6, r3
	beq	.L1884
	add	r3, r3, #1
	cmp	r6, r3
	beq	.L1885
.L1906:
	mvn	r5, #13
	b	.L1883
.L1884:
	ldr	r6, .L1907+4
	mov	r3, sp
	and	r2, r3, r6
	ldr	r3, [r2, #72]
	.syntax divided
@ 552 "./arch/arm/include/asm/uaccess.h" 1
	adds r1, r5, #8; sbcccs r1, r1, r3; movcc r3, #0
@ 0 "" 2
	.arm
	.syntax unified
	cmp	r3, #0
	bne	.L1891
	.syntax divided
@ 91 "./arch/arm/include/asm/domain.h" 1
	mrc	p15, 0, r7, c3, c0	@ get domain
@ 0 "" 2
	.arm
	.syntax unified
	bic	r3, r7, #12
	orr	r3, r3, #4
	.syntax divided
@ 101 "./arch/arm/include/asm/domain.h" 1
	mcr	p15, 0, r3, c3, c0	@ set domain
@ 0 "" 2
@ 104 "./arch/arm/include/asm/domain.h" 1
	isb 
@ 0 "" 2
	.arm
	.syntax unified
	mov	r2, #8
	mov	r1, r5
	bl	arm_copy_from_user
	.syntax divided
@ 101 "./arch/arm/include/asm/domain.h" 1
	mcr	p15, 0, r7, c3, c0	@ set domain
@ 0 "" 2
@ 104 "./arch/arm/include/asm/domain.h" 1
	isb 
@ 0 "" 2
	.arm
	.syntax unified
	cmp	r0, #0
	bne	.L1906
	ldr	r2, [r4]
	ldr	r3, .L1907+8
	cmp	r2, r3
	beq	.L1889
.L1890:
	mvn	r5, #0
.L1883:
	mov	r0, r4
	bl	kfree
.L1881:
	mov	r0, r5
	ldmfd	sp, {r4, r5, r6, r7, fp, sp, pc}
.L1889:
	ldrh	r2, [r4, #6]
	add	r1, r4, #8
	ldrh	r0, [r4, #4]
	bl	rk_sftl_vendor_read
	cmn	r0, #1
	beq	.L1890
	mov	r3, sp
	uxth	r2, r0
	and	r6, r6, r3
	strh	r0, [r4, #6]	@ movhi
	add	r2, r2, #8
	ldr	r3, [r6, #72]
	.syntax divided
@ 561 "./arch/arm/include/asm/uaccess.h" 1
	adds r1, r5, r2; sbcccs r1, r1, r3; movcc r3, #0
@ 0 "" 2
	.arm
	.syntax unified
	cmp	r3, #0
	bne	.L1906
	.syntax divided
@ 91 "./arch/arm/include/asm/domain.h" 1
	mrc	p15, 0, r6, c3, c0	@ get domain
@ 0 "" 2
	.arm
	.syntax unified
	bic	r3, r6, #12
	orr	r3, r3, #4
	.syntax divided
@ 101 "./arch/arm/include/asm/domain.h" 1
	mcr	p15, 0, r3, c3, c0	@ set domain
@ 0 "" 2
@ 104 "./arch/arm/include/asm/domain.h" 1
	isb 
@ 0 "" 2
	.arm
	.syntax unified
	mov	r1, r4
	mov	r0, r5
	bl	arm_copy_to_user
	.syntax divided
@ 101 "./arch/arm/include/asm/domain.h" 1
	mcr	p15, 0, r6, c3, c0	@ set domain
@ 0 "" 2
@ 104 "./arch/arm/include/asm/domain.h" 1
	isb 
@ 0 "" 2
	.arm
	.syntax unified
	cmp	r0, #0
	bne	.L1906
.L1897:
	mov	r5, r0
	b	.L1883
.L1885:
	ldr	r6, .L1907+4
	mov	r3, sp
	and	r2, r3, r6
	ldr	r3, [r2, #72]
	.syntax divided
@ 552 "./arch/arm/include/asm/uaccess.h" 1
	adds r1, r5, #8; sbcccs r1, r1, r3; movcc r3, #0
@ 0 "" 2
	.arm
	.syntax unified
	cmp	r3, #0
	bne	.L1891
	.syntax divided
@ 91 "./arch/arm/include/asm/domain.h" 1
	mrc	p15, 0, r7, c3, c0	@ get domain
@ 0 "" 2
	.arm
	.syntax unified
	bic	r3, r7, #12
	orr	r3, r3, #4
	.syntax divided
@ 101 "./arch/arm/include/asm/domain.h" 1
	mcr	p15, 0, r3, c3, c0	@ set domain
@ 0 "" 2
@ 104 "./arch/arm/include/asm/domain.h" 1
	isb 
@ 0 "" 2
	.arm
	.syntax unified
	mov	r2, #8
	mov	r1, r5
	bl	arm_copy_from_user
	.syntax divided
@ 101 "./arch/arm/include/asm/domain.h" 1
	mcr	p15, 0, r7, c3, c0	@ set domain
@ 0 "" 2
@ 104 "./arch/arm/include/asm/domain.h" 1
	isb 
@ 0 "" 2
	.arm
	.syntax unified
	cmp	r0, #0
	bne	.L1906
	ldr	r2, [r4]
	ldr	r3, .L1907+8
	cmp	r2, r3
	bne	.L1890
	ldrh	r2, [r4, #6]
	movw	r3, #4087
	cmp	r2, r3
	bhi	.L1890
	mov	r3, sp
	add	r2, r2, #8
	and	r6, r6, r3
	ldr	r3, [r6, #72]
	.syntax divided
@ 552 "./arch/arm/include/asm/uaccess.h" 1
	adds r1, r5, r2; sbcccs r1, r1, r3; movcc r3, #0
@ 0 "" 2
	.arm
	.syntax unified
	cmp	r3, #0
	movne	r1, r2
	movne	r0, r4
	bne	.L1905
	.syntax divided
@ 91 "./arch/arm/include/asm/domain.h" 1
	mrc	p15, 0, r6, c3, c0	@ get domain
@ 0 "" 2
	.arm
	.syntax unified
	bic	r3, r6, #12
	orr	r3, r3, #4
	.syntax divided
@ 101 "./arch/arm/include/asm/domain.h" 1
	mcr	p15, 0, r3, c3, c0	@ set domain
@ 0 "" 2
@ 104 "./arch/arm/include/asm/domain.h" 1
	isb 
@ 0 "" 2
	.arm
	.syntax unified
	mov	r1, r5
	mov	r0, r4
	bl	arm_copy_from_user
	.syntax divided
@ 101 "./arch/arm/include/asm/domain.h" 1
	mcr	p15, 0, r6, c3, c0	@ set domain
@ 0 "" 2
@ 104 "./arch/arm/include/asm/domain.h" 1
	isb 
@ 0 "" 2
	.arm
	.syntax unified
	cmp	r0, #0
	bne	.L1906
	ldrh	r2, [r4, #6]
	add	r1, r4, #8
	ldrh	r0, [r4, #4]
	bl	rk_sftl_vendor_write
	b	.L1897
.L1891:
	mov	r1, #8
.L1905:
	bl	__memzero
	b	.L1906
.L1908:
	.align	2
.L1907:
	.word	1074034177
	.word	-8192
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
	ldr	r0, .L1910
	bl	misc_register
	ldmfd	sp, {fp, sp, pc}
.L1911:
	.align	2
.L1910:
	.word	.LANCHOR2+12
	.size	rk_sftl_vendor_register, .-rk_sftl_vendor_register
	.global	ftl_temp_buf
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
	.global	g_gc_next_blk_3
	.global	g_gc_next_blk_2
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
	.type	__func__.3953, %object
	.size	__func__.3953, 17
__func__.3953:
	.ascii	"INSERT_DATA_LIST\000"
	.type	__func__.3948, %object
	.size	__func__.3948, 17
__func__.3948:
	.ascii	"INSERT_FREE_LIST\000"
	.type	__func__.3991, %object
	.size	__func__.3991, 17
__func__.3991:
	.ascii	"List_remove_node\000"
	.type	__func__.4023, %object
	.size	__func__.4023, 22
__func__.4023:
	.ascii	"List_update_data_list\000"
	.type	__func__.4134, %object
	.size	__func__.4134, 22
__func__.4134:
	.ascii	"select_l2p_ram_region\000"
	.type	__func__.4457, %object
	.size	__func__.4457, 16
__func__.4457:
	.ascii	"make_superblock\000"
	.type	__func__.4632, %object
	.size	__func__.4632, 19
__func__.4632:
	.ascii	"get_new_active_ppa\000"
	.type	__func__.14402, %object
	.size	__func__.14402, 15
__func__.14402:
	.ascii	"FlashReadPages\000"
	.type	__func__.14419, %object
	.size	__func__.14419, 15
__func__.14419:
	.ascii	"FlashProgPages\000"
	.type	__func__.4065, %object
	.size	__func__.4065, 26
__func__.4065:
	.ascii	"ftl_map_blk_alloc_new_blk\000"
	.type	__func__.3484, %object
	.size	__func__.3484, 11
__func__.3484:
	.ascii	"FtlMemInit\000"
	.type	__func__.3788, %object
	.size	__func__.3788, 14
__func__.3788:
	.ascii	"FtlBbt2Bitmap\000"
	.type	__func__.4095, %object
	.size	__func__.4095, 31
__func__.4095:
	.ascii	"Ftl_write_map_blk_to_last_page\000"
	.type	__func__.4109, %object
	.size	__func__.4109, 16
__func__.4109:
	.ascii	"FtlMapWritePage\000"
	.type	__func__.4032, %object
	.size	__func__.4032, 16
__func__.4032:
	.ascii	"load_l2p_region\000"
	.type	__func__.4080, %object
	.size	__func__.4080, 15
__func__.4080:
	.ascii	"ftl_map_blk_gc\000"
	.type	__func__.4151, %object
	.size	__func__.4151, 9
__func__.4151:
	.ascii	"log2phys\000"
	.type	__func__.4366, %object
	.size	__func__.4366, 16
__func__.4366:
	.ascii	"FtlReUsePrevPpa\000"
	.type	__func__.4519, %object
	.size	__func__.4519, 14
__func__.4519:
	.ascii	"ftl_check_vpc\000"
	.type	__func__.4246, %object
	.size	__func__.4246, 14
__func__.4246:
	.ascii	"FtlScanSysBlk\000"
	.type	__func__.4478, %object
	.size	__func__.4478, 18
__func__.4478:
	.ascii	"SupperBlkListInit\000"
	.type	__func__.4604, %object
	.size	__func__.4604, 25
__func__.4604:
	.ascii	"allocate_data_superblock\000"
	.type	__func__.4645, %object
	.size	__func__.4645, 16
__func__.4645:
	.ascii	"update_vpc_list\000"
	.type	__func__.4652, %object
	.size	__func__.4652, 20
__func__.4652:
	.ascii	"decrement_vpc_count\000"
	.type	__func__.4400, %object
	.size	__func__.4400, 22
__func__.4400:
	.ascii	"FtlRecoverySuperblock\000"
	.type	__func__.3831, %object
	.size	__func__.3831, 11
__func__.3831:
	.ascii	"FtlLoadBbt\000"
	.type	__func__.4224, %object
	.size	__func__.4224, 15
__func__.4224:
	.ascii	"FtlVpcTblFlush\000"
	.type	__func__.4503, %object
	.size	__func__.4503, 21
__func__.4503:
	.ascii	"FtlVpcCheckAndModify\000"
	.type	__func__.4625, %object
	.size	__func__.4625, 29
__func__.4625:
	.ascii	"allocate_new_data_superblock\000"
	.type	__func__.3608, %object
	.size	__func__.3608, 13
__func__.3608:
	.ascii	"FtlProgPages\000"
	.type	__func__.4723, %object
	.size	__func__.4723, 19
__func__.4723:
	.ascii	"FtlGcFreeTempBlock\000"
	.type	__func__.4829, %object
	.size	__func__.4829, 23
__func__.4829:
	.ascii	"rk_ftl_garbage_collect\000"
	.type	__func__.3636, %object
	.size	__func__.3636, 9
__func__.3636:
	.ascii	"FtlWrite\000"
	.type	__func__.4303, %object
	.size	__func__.4303, 15
__func__.4303:
	.ascii	"FtlLoadSysInfo\000"
	.space	2
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
	.word	.LC116
	.word	rk_sftl_vendor_storage_fops
	.space	28
	.bss
	.align	2
	.set	.LANCHOR0,. + 0
	.set	.LANCHOR3,. + 8184
	.type	g_nand_phy_info, %object
	.size	g_nand_phy_info, 24
g_nand_phy_info:
	.space	24
	.type	p_blk_mode_table, %object
	.size	p_blk_mode_table, 4
p_blk_mode_table:
	.space	4
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
	.type	p_valid_page_count_table, %object
	.size	p_valid_page_count_table, 4
p_valid_page_count_table:
	.space	4
	.type	p_erase_count_table, %object
	.size	p_erase_count_table, 4
p_erase_count_table:
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
	.size	gSysInfo, 12
gSysInfo:
	.space	12
	.type	g_gc_superblock, %object
	.size	g_gc_superblock, 48
g_gc_superblock:
	.space	48
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
	.type	g_gc_next_blk, %object
	.size	g_gc_next_blk, 2
g_gc_next_blk:
	.space	2
	.type	g_gc_next_blk_1, %object
	.size	g_gc_next_blk_1, 2
g_gc_next_blk_1:
	.space	2
	.type	g_gc_next_blk_2, %object
	.size	g_gc_next_blk_2, 2
g_gc_next_blk_2:
	.space	2
	.type	g_gc_next_blk_3, %object
	.size	g_gc_next_blk_3, 2
g_gc_next_blk_3:
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
	.size	g_nand_ops, 16
g_nand_ops:
	.space	16
	.type	req_sys, %object
	.size	req_sys, 20
req_sys:
	.space	20
	.type	p_sys_data_buf, %object
	.size	p_sys_data_buf, 4
p_sys_data_buf:
	.space	4
	.type	p_sys_spare_buf, %object
	.size	p_sys_spare_buf, 4
p_sys_spare_buf:
	.space	4
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
	.space	2
	.type	gVendorBlkInfo, %object
	.size	gVendorBlkInfo, 44
gVendorBlkInfo:
	.space	44
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
	.type	idb_need_write_back, %object
	.size	idb_need_write_back, 4
idb_need_write_back:
	.space	4
	.type	g_MaxLbn, %object
	.size	g_MaxLbn, 4
g_MaxLbn:
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
	.type	ftl_temp_buf, %object
	.size	ftl_temp_buf, 4096
ftl_temp_buf:
	.space	4096
	.type	check_buf, %object
	.size	check_buf, 4096
check_buf:
	.space	4096
	.type	check_vpc_table, %object
	.size	check_vpc_table, 16384
check_vpc_table:
	.space	16384
	.type	gp_flash_check_buf, %object
	.size	gp_flash_check_buf, 262144
gp_flash_check_buf:
	.space	262144
	.type	idb_buf, %object
	.size	idb_buf, 262144
idb_buf:
	.space	262144
	.type	gp_last_act_superblock, %object
	.size	gp_last_act_superblock, 4
gp_last_act_superblock:
	.space	4
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
	.ascii	"WR_CHK = %x %x %x\012\000"
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
	.ascii	"SFTL version: 5.0.45 20180810\000"
.LC71:
	.ascii	"%s\012\000"
.LC72:
	.ascii	"swblk %x ,avg = %x max= %x vpc= %x,ec=%x ,max ec=%x"
	.ascii	"\012\000"
.LC73:
	.ascii	"FtlGcRefreshBlock  0x%x\012\000"
.LC74:
	.ascii	"FtlGcMarkBadPhyBlk %d 0x%x\012\000"
.LC75:
	.ascii	"%s %p + 0x%x:\000"
.LC76:
	.ascii	"0x%08x,\000"
.LC77:
	.ascii	"0x%04x,\000"
.LC78:
	.ascii	"0x%02x,\000"
.LC79:
	.ascii	"\012\000"
.LC80:
	.ascii	"prog read error: = %x\012\000"
.LC81:
	.ascii	"prog read s error: = %x %x %x\012\000"
.LC82:
	.ascii	"prog read d error: = %x %x %x\012\000"
.LC83:
	.ascii	"FtlFreeSysBlkQueueOut free count = %d\012\000"
.LC84:
	.ascii	"FtlFreeSysBlkQueueOut = %x, free count = %d, error\012"
	.ascii	"\000"
.LC85:
	.ascii	"FtlFreeSysBlkQueueOut = %x, free count = %d\012\000"
.LC86:
	.ascii	"%s error allocating memory. return -1\012\000"
.LC87:
	.ascii	"FtlMapWritePage error = %x \012\000"
.LC88:
	.ascii	"FtlMapWritePage error = %x error count = %d\012\000"
.LC89:
	.ascii	"load_l2p_region refresh = %x phyAddr = %x\012\000"
.LC90:
	.ascii	"region_id = %x phyAddr = %x\012\000"
.LC91:
	.ascii	"spare:\000"
.LC92:
	.ascii	"map_ppn:\000"
.LC93:
	.ascii	"ftl_map_blk_gc blk info: %x %x %x\012\000"
.LC94:
	.ascii	"ftl_scan_all_data = %x\012\000"
.LC95:
	.ascii	"scan lpa = %x ppa= %x\012\000"
.LC96:
	.ascii	"lba = %x,addr= %x,spare= %x %x %x %x data=%x %x\012"
	.ascii	"\000"
.LC97:
	.ascii	"...%s enter...\012\000"
.LC98:
	.ascii	"FtlCheckVpc2 %x = %x  %x\012\000"
.LC99:
	.ascii	"free blk vpc error %x = %x  %x\012\000"
.LC100:
	.ascii	"FtlBbmTblFlush id=%x,page=%x,previd=%x cnt=%d\012\000"
.LC101:
	.ascii	"FtlBbmTblFlush error:%x\012\000"
.LC102:
	.ascii	"FtlBbmTblFlush error = %x error count = %d\012\000"
.LC103:
	.ascii	"FtlGcFreeBadSuperBlk 0x%x\012\000"
.LC104:
	.ascii	"decrement_vpc_count %x = %d\012\000"
.LC105:
	.ascii	"decrement_vpc_count %x = %d in free list\012\000"
.LC106:
	.ascii	"spuer block %x vpn is 0\012 \000"
.LC107:
	.ascii	"FtlVendorPartRead refresh = %x phyAddr = %x\012\000"
.LC108:
	.ascii	"no ect\000"
.LC109:
	.ascii	"FtlVpcTblFlush error = %x error count = %d\012\000"
.LC110:
	.ascii	"FtlCheckVpc %x = %x  %x\012\000"
.LC111:
	.ascii	"Ftlwrite decrement_vpc_count %x = %d\012\000"
.LC112:
	.ascii	"SWL %x, FSB = %x vpc= %x,ec=%x th=%x\012\000"
.LC113:
	.ascii	"g_gc_superblock_free %x %x %x %x %x\012\000"
.LC114:
	.ascii	"FtlWrite: lpa error:%x %x\012\000"
.LC115:
	.ascii	"write_idblock fail! %x %x %x %x\012\000"
.LC116:
	.ascii	"vendor_storage\000"
