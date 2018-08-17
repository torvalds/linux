/* SPDX-License-Identifier: GPL-2.0 */

/*
 * Copyright (c) 2018 Rockchip Electronics Co. Ltd.
 * date: 2018-08-17
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
	sub	sp, sp, #16
	mov	r6, r0
	ldr	r5, .L269
	add	r4, r6, #12
	ldr	r1, .L269+4
	movw	r8, #2344
	bl	strcpy
	ldr	r3, [r5, #116]
	mov	r1, #64
	ldr	r2, .L269+8
	mov	r0, r4
	bl	snprintf
	add	r4, r4, r0
	ldr	r1, .L269+12
	mov	r0, r4
	add	r4, r4, #10
	bl	strcpy
	ldr	r3, [r5, #2504]
	mov	r1, #64
	ldr	r2, .L269+16
	mov	r0, r4
	bl	snprintf
	add	r4, r4, r0
	ldr	r3, [r5, #2500]
	ldr	r2, .L269+20
	mov	r1, #64
	mov	r0, r4
	ldr	r7, .L269+24
	bl	snprintf
	add	r4, r4, r0
	ldr	r3, [r5, #2508]
	ldr	r2, .L269+28
	mov	r1, #64
	mov	r0, r4
	ldr	r9, .L269+32
	bl	snprintf
	add	r4, r4, r0
	ldr	r3, [r5, #2512]
	ldr	r2, .L269+36
	mov	r1, #64
	mov	r0, r4
	bl	snprintf
	add	r4, r4, r0
	ldr	r3, [r5, #2516]
	ldr	r2, .L269+40
	mov	r1, #64
	mov	r0, r4
	bl	snprintf
	add	r4, r4, r0
	ldr	r3, [r5, #2520]
	ldr	r2, .L269+44
	mov	r1, #64
	mov	r0, r4
	bl	snprintf
	add	r4, r4, r0
	ldr	r3, [r5, #2524]
	ldr	r2, .L269+48
	mov	r1, #64
	mov	r0, r4
	bl	snprintf
	add	r4, r4, r0
	ldr	r3, [r5, #2528]
	ldr	r2, .L269+52
	mov	r1, #64
	mov	r0, r4
	bl	snprintf
	ldr	r3, [r5, #2532]
	add	r4, r4, r0
	ldr	r2, .L269+56
	mov	r1, #64
	mov	r0, r4
	lsr	r3, r3, #11
	bl	snprintf
	ldr	r3, [r5, #2536]
	add	r4, r4, r0
	ldr	r2, .L269+60
	mov	r1, #64
	mov	r0, r4
	lsr	r3, r3, #11
	bl	snprintf
	add	r4, r4, r0
	ldr	r3, [r5, #2540]
	ldr	r2, .L269+64
	mov	r1, #64
	mov	r0, r4
	bl	snprintf
	add	r4, r4, r0
	ldr	r3, [r5, #2544]
	ldr	r2, .L269+68
	mov	r1, #64
	mov	r0, r4
	bl	snprintf
	add	r4, r4, r0
	ldrh	r3, [r5, #158]
	ldr	r2, .L269+72
	mov	r1, #64
	mov	r0, r4
	bl	snprintf
	add	r4, r4, r0
	ldrh	r3, [r5, r8]
	ldr	r2, .L269+76
	mov	r1, #64
	mov	r0, r4
	bl	snprintf
	add	r4, r4, r0
	ldr	r3, [r5, #2548]
	ldr	r2, .L269+80
	mov	r1, #64
	mov	r0, r4
	bl	snprintf
	add	r4, r4, r0
	ldr	r3, [r5, #2552]
	ldr	r2, .L269+84
	mov	r1, #64
	mov	r0, r4
	bl	snprintf
	add	r4, r4, r0
	ldr	r3, [r5, #2556]
	ldr	r2, .L269+88
	mov	r1, #64
	mov	r0, r4
	bl	snprintf
	add	r4, r4, r0
	ldr	r3, [r5, #2560]
	ldr	r2, .L269+92
	mov	r1, #64
	mov	r0, r4
	bl	snprintf
	add	r4, r4, r0
	ldr	r3, [r5, #2564]
	ldr	r2, .L269+96
	mov	r1, #64
	mov	r0, r4
	bl	snprintf
	add	r4, r4, r0
	ldr	r3, [r5, #2568]
	ldr	r2, .L269+100
	mov	r1, #64
	mov	r0, r4
	bl	snprintf
	add	r4, r4, r0
	ldrh	r3, [r7, #30]
	ldr	r2, .L269+104
	mov	r1, #64
	mov	r0, r4
	bl	snprintf
	add	r4, r4, r0
	ldrh	r3, [r7, #28]
	ldr	r2, .L269+108
	mov	r1, #64
	mov	r0, r4
	movw	r7, #2348
	bl	snprintf
	add	r4, r4, r0
	ldr	r3, [r5, #140]
	ldr	r2, .L269+112
	mov	r1, #64
	mov	r0, r4
	bl	snprintf
	add	r4, r4, r0
	ldr	r3, [r5, #132]
	ldr	r2, .L269+116
	mov	r1, #64
	mov	r0, r4
	bl	snprintf
	add	r4, r4, r0
	ldr	r3, [r5, #28]
	ldr	r2, .L269+120
	mov	r1, #64
	mov	r0, r4
	bl	snprintf
	add	r4, r4, r0
	ldrh	r3, [r5, #218]
	ldr	r2, .L269+124
	mov	r1, #64
	mov	r0, r4
	bl	snprintf
	add	r4, r4, r0
	ldrh	r3, [r5, #40]
	ldr	r2, .L269+128
	mov	r1, #64
	mov	r0, r4
	bl	snprintf
	movw	r3, #2572
	add	r4, r4, r0
	ldrh	r3, [r5, r3]
	mov	r1, #64
	ldr	r2, .L269+132
	mov	r0, r4
	bl	snprintf
	add	r4, r4, r0
	ldr	r3, [r5, #44]
	ldr	r2, .L269+136
	mov	r1, #64
	mov	r0, r4
	bl	snprintf
	add	r3, r5, #2576
	add	r4, r4, r0
	ldrh	r3, [r3]
	mov	r1, #64
	ldr	r2, .L269+140
	mov	r0, r4
	bl	snprintf
	add	r4, r4, r0
	ldrh	r3, [r5, #152]
	ldr	r2, .L269+144
	mov	r1, #64
	mov	r0, r4
	bl	snprintf
	add	r4, r4, r0
	ldrh	r3, [r9, #2]
	ldr	r2, .L269+148
	mov	r1, #64
	mov	r0, r4
	bl	snprintf
	add	r4, r4, r0
	ldrb	r3, [r5, #2354]	@ zero_extendqisi2
	ldr	r2, .L269+152
	mov	r1, #64
	mov	r0, r4
	bl	snprintf
	add	r4, r4, r0
	ldrh	r3, [r5, r7]
	ldr	r2, .L269+156
	mov	r1, #64
	mov	r0, r4
	bl	snprintf
	add	r4, r4, r0
	ldrb	r3, [r5, #2356]	@ zero_extendqisi2
	ldr	r2, .L269+160
	mov	r1, #64
	mov	r0, r4
	bl	snprintf
	add	r4, r4, r0
	ldrh	r3, [r9, #4]
	ldr	r2, .L269+164
	mov	r1, #64
	mov	r0, r4
	bl	snprintf
	ldrh	r3, [r5, r7]
	add	r4, r4, r0
	ldr	r2, [r5, #2324]
	mov	r1, #64
	mov	r0, r4
	movw	r7, #2396
	lsl	r3, r3, #1
	ldrh	r3, [r2, r3]
	ldr	r2, .L269+168
	bl	snprintf
	add	r4, r4, r0
	ldrh	r3, [r9, #50]
	ldr	r2, .L269+172
	mov	r1, #64
	mov	r0, r4
	bl	snprintf
	add	r4, r4, r0
	ldrb	r3, [r5, #2402]	@ zero_extendqisi2
	ldr	r2, .L269+176
	mov	r1, #64
	mov	r0, r4
	bl	snprintf
	add	r4, r4, r0
	ldrh	r3, [r5, r7]
	ldr	r2, .L269+180
	mov	r1, #64
	mov	r0, r4
	bl	snprintf
	add	r4, r4, r0
	ldrb	r3, [r5, #2404]	@ zero_extendqisi2
	ldr	r2, .L269+184
	mov	r1, #64
	mov	r0, r4
	bl	snprintf
	add	r4, r4, r0
	ldrh	r3, [r9, #52]
	ldr	r2, .L269+188
	mov	r1, #64
	mov	r0, r4
	bl	snprintf
	ldrh	r3, [r5, r7]
	add	r4, r4, r0
	ldr	r2, [r5, #2324]
	mov	r1, #64
	mov	r0, r4
	movw	r7, #2588
	lsl	r3, r3, #1
	ldrh	r3, [r2, r3]
	ldr	r2, .L269+192
	bl	snprintf
	add	r4, r4, r0
	ldrh	r3, [r9, #98]
	ldr	r2, .L269+196
	mov	r1, #64
	mov	r0, r4
	bl	snprintf
	add	r4, r4, r0
	ldrb	r3, [r5, #2450]	@ zero_extendqisi2
	ldr	r2, .L269+200
	mov	r1, #64
	mov	r0, r4
	bl	snprintf
	movw	r3, #2444
	add	r4, r4, r0
	ldrh	r3, [r5, r3]
	mov	r1, #64
	ldr	r2, .L269+204
	mov	r0, r4
	bl	snprintf
	add	r4, r4, r0
	ldrb	r3, [r5, #2452]	@ zero_extendqisi2
	ldr	r2, .L269+208
	mov	r1, #64
	mov	r0, r4
	bl	snprintf
	add	r4, r4, r0
	ldrh	r3, [r9, #100]
	ldr	r2, .L269+212
	mov	r1, #64
	mov	r0, r4
	bl	snprintf
	add	r4, r4, r0
	ldrh	r3, [r9, #242]
	ldr	r2, .L269+216
	mov	r1, #64
	mov	r0, r4
	bl	snprintf
	add	r4, r4, r0
	ldrb	r3, [r5, #2594]	@ zero_extendqisi2
	ldr	r2, .L269+220
	mov	r1, #64
	mov	r0, r4
	bl	snprintf
	add	r4, r4, r0
	ldrh	r3, [r5, r7]
	ldr	r2, .L269+224
	mov	r1, #64
	mov	r0, r4
	bl	snprintf
	add	r4, r4, r0
	ldrb	r3, [r5, #2596]	@ zero_extendqisi2
	ldr	r2, .L269+228
	mov	r1, #64
	mov	r0, r4
	bl	snprintf
	add	r4, r4, r0
	ldrh	r3, [r9, #244]
	ldr	r2, .L269+232
	mov	r1, #64
	mov	r0, r4
	bl	snprintf
	ldr	r3, [r5, #2712]
	add	r4, r4, r0
	ldr	r2, .L269+236
	mov	r1, #64
	mov	r0, r4
	str	r3, [sp, #4]
	ldr	r3, [r5, #2720]
	str	r3, [sp]
	ldr	r3, [r5, #2716]
	bl	snprintf
	add	r4, r4, r0
	ldr	r3, [r5, #2708]
	ldr	r2, .L269+240
	mov	r1, #64
	mov	r0, r4
	bl	snprintf
	add	r4, r4, r0
	ldr	r3, [r5, #2732]
	ldr	r2, .L269+244
	mov	r1, #64
	mov	r0, r4
	bl	snprintf
	movw	r3, #3148
	add	r4, r4, r0
	ldrh	r3, [r5, r3]
	mov	r1, #64
	ldr	r2, .L269+248
	mov	r0, r4
	bl	snprintf
	movw	r3, #3150
	add	r4, r4, r0
	ldrh	r3, [r5, r3]
	mov	r1, #64
	ldr	r2, .L269+252
	mov	r0, r4
	bl	snprintf
	add	r4, r4, r0
	ldr	r3, [r5, #3152]
	ldr	r2, .L269+256
	mov	r1, #64
	mov	r0, r4
	bl	snprintf
	movw	r3, #3156
	add	r4, r4, r0
	ldrh	r3, [r5, r3]
	mov	r1, #64
	ldr	r2, .L269+260
	mov	r0, r4
	bl	snprintf
	add	r4, r4, r0
	bl	GetFreeBlockMinEraseCount
	ldr	r2, .L269+264
	mov	r3, r0
	mov	r1, #64
	mov	r0, r4
	bl	snprintf
	add	r4, r4, r0
	ldrh	r0, [r5, r8]
	bl	GetFreeBlockMaxEraseCount
	ldr	r2, .L269+268
	mov	r3, r0
	mov	r1, #64
	mov	r0, r4
	bl	snprintf
	ldr	r3, .L269+272
	add	r4, r4, r0
	ldr	r3, [r3]
	cmp	r3, #1
	beq	.L258
.L263:
	sub	r0, r4, r6
	sub	sp, fp, #40
	ldmfd	sp, {r4, r5, r6, r7, r8, r9, r10, fp, sp, pc}
.L258:
	ldrh	r3, [r5, r7]
	movw	r2, #65535
	cmp	r3, r2
	beq	.L260
	ldr	r2, [r5, #2324]
	lsl	r3, r3, #1
	mov	r0, r4
	mov	r1, #64
	ldrh	r3, [r2, r3]
	ldr	r2, .L269+276
	bl	snprintf
	add	r4, r4, r0
.L260:
	mov	r0, #0
	ldr	r9, .L269+280
	bl	List_get_gc_head_node
	uxth	r0, r0
	mov	r7, #0
	movw	r10, #65535
.L262:
	cmp	r0, r10
	beq	.L261
	ldr	r3, [r5, #2328]
	lsl	r2, r0, #1
	lsl	r8, r0, #3
	mov	r1, #64
	ldrh	r3, [r3, r2]
	str	r3, [sp, #12]
	ldr	r3, [r5, #2316]
	add	r3, r3, r8
	ldrh	r3, [r3, #4]
	str	r3, [sp, #8]
	ldr	r3, [r5, #2324]
	ldrh	r3, [r3, r2]
	mov	r2, r9
	stm	sp, {r0, r3}
	mov	r3, r7
	mov	r0, r4
	add	r7, r7, #1
	bl	snprintf
	ldr	r3, [r5, #2316]
	cmp	r7, #16
	add	r4, r4, r0
	ldrh	r0, [r3, r8]
	bne	.L262
.L261:
	ldr	r3, [r5, #2340]
	mov	r7, #0
	ldr	r2, [r5, #2316]
	movw	r9, #65535
	ldr	r10, .L269+284
	sub	r3, r3, r2
	ubfx	r3, r3, #3, #16
.L264:
	cmp	r3, r9
	beq	.L263
	ldr	r1, [r5, #2328]
	lsl	r2, r3, #1
	lsl	r8, r3, #3
	mov	r0, r4
	ldrh	r2, [r1, r2]
	mov	r1, #64
	str	r2, [sp, #8]
	ldr	r2, [r5, #2316]
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
	ldr	r3, [r5, #2316]
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
	.word	.LANCHOR0+2268
	.word	.LC7
	.word	.LANCHOR0+2348
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
	ldr	r4, .L300
	ldr	r2, [r4, #2556]
	ldr	r3, [r4, #2568]
	cmp	r2, r3
	bcs	.L277
	ldr	r2, [r4, #2328]
	mov	r3, #0
	ldrh	r1, [r4, #40]
	str	r3, [r4, #2548]
	sub	r2, r2, #2
.L278:
	cmp	r3, r1
	bcc	.L279
	ldr	r5, [r4, #2548]
	mov	r0, r5
	bl	__udivsi3
	str	r0, [r4, #2556]
	ldr	r0, [r4, #2552]
	ldrh	r1, [r4, #92]
	sub	r0, r5, r0
	bl	__udivsi3
	str	r0, [r4, #2548]
.L280:
	ldr	r6, [r4, #2568]
	ldr	r7, [r4, #2556]
	add	r3, r6, #256
	cmp	r3, r7
	bls	.L285
	ldr	r2, [r4, #2564]
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
	ldr	ip, [r4, #2548]
	add	r0, r0, ip
	str	r0, [r4, #2548]
	b	.L278
.L277:
	ldr	r3, [r4, #2564]
	cmp	r2, r3
	addhi	r3, r3, #1
	strhi	r3, [r4, #2564]
	movhi	r3, #0
	bls	.L280
.L282:
	ldrh	r2, [r4, #40]
	cmp	r3, r2
	bcs	.L280
	ldr	r0, [r4, #2328]
	lsl	r1, r3, #1
	add	r3, r3, #1
	ldrh	r2, [r0, r1]
	add	r2, r2, #1
	strh	r2, [r0, r1]	@ movhi
	b	.L282
.L285:
	movw	r3, #2344
	ldrh	r0, [r4, r3]
	add	r0, r0, r0, lsl #1
	asr	r0, r0, #2
	bl	GetFreeBlockMaxEraseCount
	add	r3, r6, #64
	mov	r9, r0
	cmp	r0, r3
	bcc	.L287
	ldr	r3, [r4, #2320]
	cmp	r3, #0
	beq	.L287
	ldrh	r10, [r4, #40]
	movw	r1, #65535
	ldr	lr, [r4, #2316]
	mov	r0, #0
	ldr	r8, [r4, #2328]
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
	strcc	r1, [r4, #2568]
.L292:
	cmp	r7, r10
	bls	.L287
	add	r2, r10, #128
	cmp	r9, r2
	ble	.L287
	add	r2, r10, #256
	cmp	r7, r2
	bhi	.L293
	ldr	r2, [r4, #2564]
	add	r10, r10, #768
	cmp	r10, r2
	bcs	.L287
.L293:
	str	r9, [sp, #8]
	mov	r1, r5
	ldrh	r2, [r8, r3]
	ldr	r0, .L300+4
	str	r2, [sp, #4]
	ldr	r2, [r4, #2324]
	ldrh	r3, [r2, r3]
	mov	r2, r7
	str	r3, [sp]
	ldr	r3, [r4, #2564]
	bl	sftl_printk
	mov	r3, #1
	str	r3, [r4, #3160]
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
	beq	.L303
	ldr	r3, .L304
	mov	r1, #0
	ldr	r2, [r3, #2324]
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
	movw	r2, #2776
	ldr	r1, .L320
	ldr	r0, .L320+4
	bl	sftl_printk
.L307:
	ldr	r5, .L320+8
	ldrh	r2, [r4, #2]
	ldrh	r3, [r5, #102]
	cmp	r2, r3
	bne	.L308
	movw	r2, #2777
	ldr	r1, .L320
	ldr	r0, .L320+4
	bl	sftl_printk
.L308:
	ldrh	r3, [r4, #4]
	cmp	r3, #0
	bne	.L309
	movw	r2, #2778
	ldr	r1, .L320
	ldr	r0, .L320+4
	bl	sftl_printk
.L309:
	ldrb	r2, [r4, #6]	@ zero_extendqisi2
	mov	r3, #0
	ldrh	r1, [r5, #32]
	movw	ip, #65535
	strb	r3, [r4, #10]
	add	r2, r4, r2, lsl #1
	ldrh	r0, [r2, #16]
	mov	r2, r3
.L310:
	cmp	r0, ip
	ldrb	r3, [r4, #6]	@ zero_extendqisi2
	beq	.L312
	ldrh	r2, [r4, #4]
	ldrh	r6, [r4, #2]
	sub	r2, r2, #1
	uxth	r2, r2
	orr	r6, r6, r0, lsl #10
	movw	r0, #65535
	strh	r2, [r4, #4]	@ movhi
.L314:
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
	beq	.L314
	strb	r3, [r4, #6]
	cmp	r2, #0
	ldrh	r3, [r4, #2]
	ldrh	r1, [r5, #102]
	sub	r3, r3, r1
	clz	r3, r3
	lsr	r3, r3, #5
	moveq	r3, #0
	cmp	r3, #0
	beq	.L306
	movw	r2, #2799
	ldr	r1, .L320
	ldr	r0, .L320+4
	bl	sftl_printk
.L306:
	mov	r0, r6
	ldmfd	sp, {r4, r5, r6, fp, sp, pc}
.L312:
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
	b	.L310
.L321:
	.align	2
.L320:
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
	ldr	r3, .L327
	mov	lr, #12
	mov	r4, #1
	mov	r5, #20
	mov	r2, #0
	str	r2, [r3, #3164]
.L323:
	ldrh	r1, [r3, #32]
	uxth	r0, r2
	add	ip, r2, #1
	cmp	r0, r1
	bcc	.L324
	mov	ip, #12
	mov	lr, #0
.L325:
	ldr	r2, [r3, #3184]
	cmp	r1, r2
	bcc	.L326
	ldmfd	sp, {r4, r5, r6, r7, r8, fp, sp, pc}
.L324:
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
	b	.L323
.L326:
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
	b	.L325
.L328:
	.align	2
.L327:
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
	ldr	r3, .L336
	mov	lr, #0
	mov	r5, #20
	mov	r7, #12
	mov	r8, lr
	ldr	r6, [r3, #3184]
	ldr	r4, [r3, #3168]
.L330:
	uxth	r3, lr
	cmp	r1, r3
	ldmfdls	sp, {r4, r5, r6, r7, r8, r9, r10, fp, sp, pc}
	mla	ip, r5, r3, r0
	mov	r2, #0
.L331:
	uxth	r3, r2
	cmp	r6, r3
	bls	.L332
	mul	r3, r7, r3
	add	r2, r2, #1
	ldr	r10, [r4, r3]
	add	r9, r4, r3
	ldr	r3, [ip, #8]
	cmp	r10, r3
	bne	.L331
	str	r8, [r9, #8]
.L332:
	add	lr, lr, #1
	b	.L330
.L337:
	.align	2
.L336:
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
	ldr	r3, .L345
	mov	ip, #0
	mov	r6, #12
	mov	r7, #1
	mov	r8, #20
	ldr	r4, [r3, #3184]
	ldr	r5, [r3, #3168]
.L339:
	uxth	r2, ip
	cmp	r1, r2
	bhi	.L343
	ldmfd	sp, {r4, r5, r6, r7, r8, r9, fp, sp, pc}
.L343:
	mov	lr, #0
.L340:
	uxth	r3, lr
	cmp	r4, r3
	bls	.L341
	mla	r3, r6, r3, r5
	add	lr, lr, #1
	ldr	r9, [r3, #8]
	cmp	r9, #0
	bne	.L340
	mla	r2, r8, r2, r0
	ldr	lr, [r3]
	str	r7, [r3, #8]
	str	lr, [r2, #8]
	ldr	r3, [r3, #4]
	str	r3, [r2, #12]
.L341:
	add	ip, ip, #1
	b	.L339
.L346:
	.align	2
.L345:
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
	ldr	r2, .L352
	movw	r1, #3192
	uxth	r0, r0
	ldr	r3, [r2, #3188]
	ldrh	r2, [r2, r1]
	add	r2, r3, r2, lsl #1
.L348:
	cmp	r3, r2
	bne	.L350
	mov	r0, #0
	ldmfd	sp, {fp, sp, pc}
.L350:
	ldrh	r1, [r3], #2
	cmp	r1, r0
	bne	.L348
	mov	r0, #1
	ldmfd	sp, {fp, sp, pc}
.L353:
	.align	2
.L352:
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
	ldr	ip, .L358
	movw	r3, #3192
	ldr	r2, [ip, #3188]
	ldrh	lr, [ip, r3]
	mov	r3, #0
	sub	r1, r2, #2
.L355:
	uxth	r7, r3
	cmp	r7, lr
	bcc	.L357
	bne	.L356
	lsl	r3, r7, #1
	strh	r0, [r2, r3]	@ movhi
	movw	r2, #3192
	ldrh	r3, [ip, r2]
	add	r3, r3, #1
	strh	r3, [ip, r2]	@ movhi
	b	.L356
.L357:
	ldrh	r7, [r1, #2]!
	add	r3, r3, #1
	cmp	r7, r0
	bne	.L355
.L356:
	ldr	r0, .L358+4
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
.L359:
	.align	2
.L358:
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
	ldr	r0, .L365
	bl	sftl_printk
	ldr	r3, .L365+4
	ldrh	r8, [r3, r7]
	cmp	r4, r8
	beq	.L361
	movw	r5, #3204
	ldrh	r6, [r3, r5]
	cmp	r4, r6
	beq	.L361
	movw	ip, #3206
	ldrh	lr, [r3, ip]
	cmp	r4, lr
	beq	.L361
	movw	r1, #3208
	ldrh	r0, [r3, r1]
	cmp	r4, r0
	beq	.L361
	movw	r2, #65535
	cmp	r8, r2
	strheq	r4, [r3, r7]	@ movhi
	beq	.L361
	cmp	r6, r2
	strheq	r4, [r3, r5]	@ movhi
	beq	.L361
	cmp	lr, r2
	strheq	r4, [r3, ip]	@ movhi
	beq	.L361
	cmp	r0, r2
	strheq	r4, [r3, r1]	@ movhi
.L361:
	mov	r0, #0
	ldmfd	sp, {r4, r5, r6, r7, r8, fp, sp, pc}
.L366:
	.align	2
.L365:
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
	ldr	r4, .L371
	uxth	r5, r0
	movw	r6, #3210
	mov	r0, r5
	bl	P2V_block_in_plane
	mov	r2, r5
	mov	r7, r0
	ldrh	r1, [r4, r6]
	ldr	r0, .L371+4
	bl	sftl_printk
	mov	r0, r7
	bl	FtlGcRefreshBlock
	ldrh	r3, [r4, r6]
	mov	r2, #0
	ldr	r0, .L371+8
.L368:
	uxth	r1, r2
	cmp	r3, r1
	bhi	.L370
	cmp	r3, #15
	movwls	r2, #3210
	addls	r1, r3, #1
	strhls	r1, [r4, r2]	@ movhi
	lslls	r3, r3, #1
	ldrls	r2, .L371+8
	strhls	r5, [r2, r3]	@ movhi
	b	.L369
.L370:
	add	r2, r2, #1
	add	r1, r0, r2, lsl #1
	ldrh	r1, [r1, #-2]
	cmp	r1, r5
	bne	.L368
.L369:
	mov	r0, #0
	ldmfd	sp, {r4, r5, r6, r7, fp, sp, pc}
.L372:
	.align	2
.L371:
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
	ldr	r4, .L379
	movw	r3, #3210
	ldrh	r2, [r4, r3]
	cmp	r2, #0
	beq	.L374
	movw	r3, #3202
	ldrh	r1, [r4, r3]
	movw	r3, #65535
	cmp	r1, r3
	bne	.L374
	movw	r3, #3246
	movw	r5, #3246
	ldrh	r1, [r4, r3]
	cmp	r1, r2
	movcs	r2, #0
	strhcs	r2, [r4, r3]	@ movhi
	ldrh	r3, [r4, r5]
	ldr	r2, .L379+4
	lsl	r3, r3, #1
	ldrh	r0, [r2, r3]
	bl	P2V_block_in_plane
	bl	FtlGcRefreshBlock
	ldrh	r3, [r4, r5]
	add	r3, r3, #1
	strh	r3, [r4, r5]	@ movhi
.L374:
	mov	r0, #0
	ldmfd	sp, {r4, r5, fp, sp, pc}
.L380:
	.align	2
.L379:
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
	ldr	r1, .L382
	bl	__kmalloc
	ldmfd	sp, {fp, sp, pc}
.L383:
	.align	2
.L382:
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
.L386:
	cmp	r4, r9
	bcc	.L392
	ldr	r0, .L394
	bl	sftl_printk
	ldmfd	sp, {r4, r5, r6, r7, r8, r9, r10, fp, sp, pc}
.L392:
	cmp	r5, #0
	bne	.L387
	mov	r3, r6
	mov	r2, r7
	mov	r1, r10
	ldr	r0, .L394+4
	bl	sftl_printk
.L387:
	cmp	r8, #4
	ldreq	r1, [r7, r4, lsl #2]
	ldreq	r0, .L394+8
	beq	.L393
	cmp	r8, #2
	lsleq	r3, r4, #1
	ldreq	r0, .L394+12
	ldrbne	r1, [r7, r4]	@ zero_extendqisi2
	ldrheq	r1, [r7, r3]
	ldrne	r0, .L394+16
.L393:
	add	r5, r5, #1
	bl	sftl_printk
	cmp	r5, #15
	bls	.L391
	mov	r5, #0
	ldr	r0, .L394
	bl	sftl_printk
.L391:
	add	r4, r4, #1
	add	r6, r6, r8
	b	.L386
.L395:
	.align	2
.L394:
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
	ldr	r5, .L414
	mov	r7, r1
	ldr	r9, .L414+4
	mov	r4, r0
	ldr	r10, .L414+8
	ldrh	r8, [r5, #12]
.L397:
	cmp	r6, r7
	bne	.L404
	mov	r0, #0
	sub	sp, fp, #40
	ldmfd	sp, {r4, r5, r6, r7, r8, r9, r10, fp, sp, pc}
.L404:
	ldr	r3, [r4, #8]
	cmp	r3, #0
	beq	.L398
	ldr	r3, [r4, #12]
	cmp	r3, #0
	bne	.L399
.L398:
	mov	r2, #101
	mov	r1, r9
	mov	r0, r10
	bl	sftl_printk
.L399:
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
	bne	.L401
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
	beq	.L402
	ldr	r3, [r4, #12]
	ldr	r2, [r3, #12]
	cmn	r2, #1
	bne	.L401
	ldr	r2, [r3, #8]
	cmn	r2, #1
	bne	.L401
	ldr	r3, [r3]
	cmn	r3, #1
	beq	.L401
.L402:
	mvn	r3, #0
	str	r3, [r4]
.L401:
	add	r6, r6, #1
	add	r4, r4, #20
	b	.L397
.L415:
	.align	2
.L414:
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
	ldr	r4, .L426
	mov	r6, #0
	mvn	r9, #0
	ldr	r3, [r4, #3284]
	add	r7, r4, #162
	ldr	r8, [r4, #3288]
	add	r10, r4, #3264
	str	r3, [r4, #3272]
	str	r8, [r4, #3276]
.L417:
	ldrh	r3, [r4, #54]
	cmp	r6, r3
	bcc	.L422
	mov	r0, #0
	ldmfd	sp, {r4, r5, r6, r7, r8, r9, r10, fp, sp, pc}
.L422:
	ldrh	r5, [r4, #98]
	strh	r9, [r7, #2]!	@ movhi
.L419:
	ldrh	r3, [r4, #98]
	sub	r5, r5, #1
	uxth	r5, r5
	sub	r2, r3, #16
	cmp	r5, r2
	ble	.L420
	mla	r3, r6, r3, r5
	mov	r2, #1
	mov	r1, r2
	mov	r0, r10
	lsl	r3, r3, #10
	str	r3, [r4, #3268]
	bl	FlashReadPages
	ldr	r3, [r4, #3264]
	cmn	r3, #1
	beq	.L419
	ldrh	r2, [r8]
	movw	r3, #61664
	cmp	r2, r3
	bne	.L419
	strh	r5, [r7]	@ movhi
.L420:
	add	r6, r6, #1
	b	.L417
.L427:
	.align	2
.L426:
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
	ldr	r3, .L439
	lsl	r6, r0, #10
	mov	r2, r1
	mov	r7, r1
	sub	r0, fp, #116
	ldrheq	r5, [r3, #104]
	mov	r1, #1
	ldrhne	r5, [r3, #102]
	ldr	r3, .L439+4
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
	beq	.L432
.L431:
	mov	r0, r5
	sub	sp, fp, #32
	ldmfd	sp, {r4, r5, r6, r7, r8, fp, sp, pc}
.L435:
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
	bne	.L433
	ldr	r3, [fp, #-92]
	cmn	r3, #1
	bne	.L433
	ldr	r3, [fp, #-116]
	cmn	r3, #1
	subne	r4, r4, #1
	sxthne	r5, r4
	bne	.L432
.L433:
	add	r4, r4, #1
	sxth	r8, r4
.L432:
	cmp	r8, r5
	ble	.L435
	b	.L431
.L440:
	.align	2
.L439:
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
	ldr	r6, .L471
	mov	r10, r1
	mov	r8, r2
	mov	r4, r0
	mov	r5, r0
	mov	r7, #0
	ldrh	r3, [r6, #12]
	str	r3, [fp, #-72]
.L442:
	cmp	r7, r10
	bne	.L450
	cmp	r9, #0
	bne	.L457
.L470:
	mov	r0, #0
	sub	sp, fp, #40
	ldmfd	sp, {r4, r5, r6, r7, r8, r9, r10, fp, sp, pc}
.L450:
	ldr	r3, [r5, #8]
	cmp	r3, #0
	beq	.L443
	ldr	r3, [r5, #12]
	cmp	r3, #0
	bne	.L444
.L443:
	mov	r2, #131
	ldr	r1, .L471+4
	ldr	r0, .L471+8
	bl	sftl_printk
.L444:
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
	bne	.L448
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
.L448:
	add	r7, r7, #1
	add	r5, r5, #20
	b	.L442
.L455:
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
	bne	.L452
	ldr	r1, [r4, #4]
	ldr	r0, .L471+12
	str	r3, [fp, #-72]
	bl	sftl_printk
	ldr	r3, [fp, #-72]
	str	r3, [r4]
.L452:
	ldr	r3, [r4, #12]
	cmp	r3, #0
	beq	.L453
	ldr	r2, [r3]
	ldr	r3, [r6, #3292]
	cmp	r2, r3
	beq	.L453
	ldr	r1, [r4, #4]
	ldr	r0, .L471+16
	bl	sftl_printk
	mvn	r3, #0
	str	r3, [r4]
.L453:
	ldr	r3, [r4, #8]
	cmp	r3, #0
	beq	.L454
	ldr	r2, [r3]
	ldr	r3, [r9]
	cmp	r2, r3
	beq	.L454
	ldr	r1, [r4, #4]
	ldr	r0, .L471+20
	bl	sftl_printk
	mvn	r3, #0
	str	r3, [r4]
.L454:
	add	r5, r5, #1
	add	r4, r4, #20
.L451:
	cmp	r7, r5
	bne	.L455
	b	.L470
.L457:
	mov	r5, #0
	ldr	r9, .L471+24
	ldr	r10, .L471+28
	b	.L451
.L472:
	.align	2
.L471:
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
	ldr	r5, .L484
	mov	r7, r2
	mov	r4, r0
	ldrh	r8, [r5, #12]
.L474:
	cmp	r6, r7
	bne	.L480
	mov	r0, #0
	sub	sp, fp, #32
	ldmfd	sp, {r4, r5, r6, r7, r8, fp, sp, pc}
.L480:
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
	bne	.L478
	ldr	r1, [fp, #-36]
	ldr	r3, [r5, #3252]
	ldrb	r0, [fp, #-40]	@ zero_extendqisi2
	add	r1, r8, r1
	blx	r3
	cmp	r0, #0
	mvnne	r3, #0
	strne	r3, [r4]
.L478:
	add	r6, r6, #1
	add	r4, r4, #20
	b	.L474
.L485:
	.align	2
.L484:
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
	ldr	r4, .L495
	ldrh	r3, [r4, #218]
	cmp	r3, #1024
	ldmfdeq	sp, {r4, r5, r6, fp, sp, pc}
	cmp	r1, #0
	beq	.L488
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
.L488:
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
.L496:
	.align	2
.L495:
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
	ldr	r3, .L539
	mov	r4, r10
	ldr	r2, [fp, #-48]
	mov	r9, #20
	mov	r8, r3
	str	r2, [r3, #3808]
.L498:
	ldrh	r1, [r8, #32]
	uxth	r2, r10
	cmp	r1, r2
	bhi	.L502
	cmp	r5, #0
	beq	.L497
	mov	r7, #0
	mov	r9, #20
	mov	r2, r5
	mov	r1, #0
	ldr	r0, [r8, #3804]
	bl	FlashEraseBlocks
.L505:
	uxth	r2, r7
	cmp	r5, r2
	bhi	.L507
.L508:
	cmp	r6, #0
	ldrhne	r3, [r8, #104]
	moveq	r3, #6
	ldr	r8, .L539
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
.L517:
	mov	r7, #0
	mov	r5, r7
.L509:
	ldrh	r1, [r8, #32]
	uxth	r3, r7
	cmp	r1, r3
	bhi	.L512
	cmp	r5, #0
	beq	.L497
	ldr	r2, [fp, #-56]
	mov	r3, #1
	mov	r1, r5
	ldr	r0, [r8, #3804]
	bl	FlashProgPages
	mov	r9, #0
	mov	r2, #20
.L514:
	uxth	r3, r9
	cmp	r5, r3
	bhi	.L516
	ldr	r3, [fp, #-44]
	ldr	r2, [fp, #-60]
	add	r7, r3, r2
	uxth	r3, r7
	str	r3, [fp, #-44]
	ldr	r2, [fp, #-44]
	ldr	r3, [fp, #-52]
	cmp	r3, r2
	bhi	.L517
	mov	r7, #0
	mov	r9, #20
.L518:
	uxth	r3, r7
	cmp	r5, r3
	bhi	.L520
	ldr	r3, [fp, #-48]
	adds	r6, r6, #0
	movne	r6, #1
	cmp	r3, #63
	movhi	r8, r6
	orrls	r8, r6, #1
	cmp	r8, #0
	beq	.L497
	mov	r2, r5
	ldr	r1, [fp, #-56]
	ldr	r0, [r10, #3804]
	bl	FlashEraseBlocks
.L497:
	mov	r0, r4
	sub	sp, fp, #40
	ldmfd	sp, {r4, r5, r6, r7, r8, r9, r10, fp, sp, pc}
.L502:
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
	beq	.L499
	bl	IsBlkInVendorPart
	cmp	r0, #0
	bne	.L500
.L499:
	mov	r0, r7
	bl	FtlBbmIsBadBlock
	cmp	r0, #0
	addne	r4, r4, #1
	uxthne	r4, r4
	bne	.L500
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
.L500:
	add	r10, r10, #1
	b	.L498
.L507:
	mul	r2, r9, r7
	ldr	r1, [r8, #3804]
	add	r0, r1, r2
	ldr	r2, [r1, r2]
	cmn	r2, #1
	bne	.L506
	ldr	r0, [r0, #4]
	add	r4, r4, #1
	uxth	r4, r4
	ubfx	r0, r0, #10, #16
	bl	FtlBbmMapBadBlock
.L506:
	add	r7, r7, #1
	b	.L505
.L512:
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
	beq	.L510
	bl	IsBlkInVendorPart
	cmp	r0, #0
	bne	.L511
.L510:
	mov	r0, r9
	bl	FtlBbmIsBadBlock
	cmp	r0, #0
	bne	.L511
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
.L511:
	add	r7, r7, #1
	b	.L509
.L516:
	mul	r3, r2, r9
	ldr	r1, [r10, #3804]
	add	r0, r1, r3
	ldr	r3, [r1, r3]
	cmp	r3, #0
	beq	.L515
	ldr	r0, [r0, #4]
	add	r4, r4, #1
	str	r2, [fp, #-64]
	uxth	r4, r4
	ubfx	r0, r0, #10, #16
	bl	FtlBbmMapBadBlock
	ldr	r2, [fp, #-64]
.L515:
	add	r9, r9, #1
	b	.L514
.L520:
	cmp	r6, #0
	beq	.L519
	mul	r3, r9, r7
	ldr	r2, [r10, #3804]
	add	r1, r2, r3
	ldr	r3, [r2, r3]
	cmp	r3, #0
	bne	.L519
	ldr	r0, [r1, #4]
	mov	r1, #1
	ubfx	r0, r0, #10, #16
	bl	FtlFreeSysBlkQueueIn
.L519:
	add	r7, r7, #1
	b	.L518
.L540:
	.align	2
.L539:
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
	ldr	r4, .L548
	ldr	r6, .L548+4
.L542:
	ldrh	r1, [r4, #218]
	cmp	r1, #0
	beq	.L543
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
	bhi	.L544
	mov	r0, r5
	ldmfd	sp, {r4, r5, r6, r7, fp, sp, pc}
.L543:
	ldr	r0, .L548+8
	bl	sftl_printk
.L546:
	b	.L546
.L544:
	ldrh	r2, [r4, #218]
	mov	r1, r5
	mov	r0, r6
	bl	sftl_printk
	b	.L542
.L549:
	.align	2
.L548:
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
.L551:
	uxth	r5, r3
	cmp	r5, r1
	bcs	.L554
	mov	r7, r2
	add	r3, r3, #1
	ldrh	r6, [r7]
	add	r2, r2, #2
	cmp	r6, #0
	bne	.L551
	bl	FtlFreeSysBlkQueueOut
	sub	r3, r0, #1
	movw	r2, #65533
	uxth	r3, r3
	mov	r1, r0
	strh	r0, [r7]	@ movhi
	cmp	r3, r2
	bls	.L552
	ldr	r3, .L558
	ldr	r0, .L558+4
	ldrh	r2, [r3, #218]
	bl	sftl_printk
.L553:
	b	.L553
.L552:
	ldr	r3, [r4, #28]
	strh	r6, [r4, #2]	@ movhi
	strh	r5, [r4]	@ movhi
	add	r3, r3, #1
	str	r3, [r4, #28]
	ldrh	r3, [r4, #8]
	add	r3, r3, #1
	strh	r3, [r4, #8]	@ movhi
.L554:
	ldrh	r3, [r4, #10]
	cmp	r3, r5
	bhi	.L556
	movw	r2, #603
	ldr	r1, .L558+8
	ldr	r0, .L558+12
	bl	sftl_printk
.L556:
	mov	r0, #0
	ldmfd	sp, {r4, r5, r6, r7, fp, sp, pc}
.L559:
	.align	2
.L558:
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
	bls	.L560
	ldr	r5, .L563
	sub	r0, fp, #84
	mov	r2, #32
	mov	r1, #165
	str	r0, [fp, #-92]
	lsl	r4, r4, #10
	str	r5, [fp, #-96]
	bl	memset
	ldr	r3, .L563+4
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
.L560:
	mov	r0, r4
	sub	sp, fp, #20
	ldmfd	sp, {r4, r5, fp, sp, pc}
.L564:
	.align	2
.L563:
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
	ldr	r5, .L572
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
	bls	.L566
	mov	r2, #256
	mov	r1, #255
	mov	r0, r4
	bl	memset
	mov	r0, #0
.L566:
	ldrh	r3, [r5, #14]
	cmp	r3, #4
	moveq	r3, r4
	addeq	r1, r3, r0, lsl #1
	beq	.L568
	ldmfd	sp, {r4, r5, r6, fp, sp, pc}
.L569:
	ldrh	r2, [r3]
	lsr	r2, r2, #1
	strh	r2, [r3], #2	@ movhi
.L568:
	cmp	r3, r1
	bne	.L569
	ldmfd	sp, {r4, r5, r6, fp, sp, pc}
.L573:
	.align	2
.L572:
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
	beq	.L575
	bl	memset
.L575:
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
	ldr	r4, .L680
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
	ldr	r3, .L680+4
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
.L580:
	cmp	r3, lr
	bcc	.L581
	ldr	r2, .L680+8
	mov	r1, #0
	add	r3, r2, r3, lsl #2
	add	r2, r2, #56
	add	r3, r3, #24
.L582:
	cmp	r2, r3
	bne	.L583
	ldr	r3, [r4, #3872]
	cmp	r3, #0
	bne	.L584
.L586:
	ldr	r1, .L680+12
	ldr	r0, .L680+16
	bl	sftl_printk
	mvn	r0, #0
	ldmfd	sp, {r4, r5, r6, r7, r8, fp, sp, pc}
.L581:
	ldr	r0, [r4, #180]
	add	r3, r3, #1
	add	r0, r0, r1
	add	r1, r1, r2
	str	r0, [ip, #4]!
	b	.L580
.L583:
	str	r1, [r3, #4]!
	b	.L582
.L584:
	ldr	r3, [r4, #3876]
	cmp	r3, #0
	beq	.L586
	ldr	r3, [r4, #3892]
	cmp	r3, #0
	beq	.L586
	ldr	r3, [r4, #3896]
	cmp	r3, #0
	beq	.L586
	ldr	r3, [r4, #2492]
	cmp	r3, #0
	beq	.L586
	ldr	r3, [r4, #3900]
	cmp	r3, #0
	beq	.L586
	ldr	r3, [r4, #2316]
	cmp	r3, #0
	beq	.L586
	ldr	r3, [r4, #180]
	cmp	r3, #0
	beq	.L586
	ldr	r3, [r4, #2324]
	cmp	r3, #0
	beq	.L586
	ldr	r3, [r4, #3188]
	cmp	r3, #0
	beq	.L586
	ldr	r3, [r4, #3196]
	cmp	r3, #0
	beq	.L586
	ldr	r3, [r4, #3836]
	cmp	r3, #0
	beq	.L586
	ldr	r3, [r4, #3844]
	cmp	r3, #0
	beq	.L586
	ldr	r3, [r4, #3804]
	cmp	r3, #0
	beq	.L586
	ldr	r3, [r4, #3180]
	cmp	r3, #0
	beq	.L586
	ldr	r3, [r4, #3840]
	cmp	r3, #0
	beq	.L586
	ldr	r3, [r4, #3284]
	cmp	r3, #0
	beq	.L586
	ldr	r3, [r4, #3848]
	cmp	r3, #0
	beq	.L586
	ldr	r3, [r4, #3852]
	cmp	r3, #0
	beq	.L586
	ldr	r3, [r4, #3172]
	cmp	r3, #0
	beq	.L586
	ldr	r3, [r4, #3820]
	cmp	r3, #0
	beq	.L586
	ldr	r3, [r4, #3812]
	cmp	r3, #0
	beq	.L586
	ldr	r3, [r4, #3168]
	cmp	r3, #0
	beq	.L586
	ldr	r3, [r4, #3288]
	cmp	r3, #0
	beq	.L586
	ldr	r3, [r4, #3816]
	cmp	r3, #0
	beq	.L586
	ldr	r3, [r4, #3176]
	cmp	r3, #0
	beq	.L586
	ldr	r3, [r4, #2328]
	cmp	r3, #0
	beq	.L586
	ldr	r3, [r4, #3860]
	cmp	r3, #0
	beq	.L586
	ldr	r3, [r4, #148]
	cmp	r3, #0
	beq	.L586
	ldr	r3, .L680
	ldr	r2, [r3, #3880]
	cmp	r2, #0
	beq	.L586
	ldr	r2, [r3, #3884]
	cmp	r2, #0
	beq	.L586
	ldr	r3, [r3, #3888]
	cmp	r3, #0
	beq	.L586
	mov	r0, #0
	ldmfd	sp, {r4, r5, r6, r7, r8, fp, sp, pc}
.L681:
	.align	2
.L680:
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
	ldr	r6, .L688
	mov	r4, r0
	mov	r7, r1
	ldr	r8, .L688+4
	sub	r5, r4, #2
	ldrh	r2, [r6]
	add	r4, r4, #1020
	add	r4, r4, #2
	sub	r6, r6, #3904
	mov	r1, #0
	mov	r0, r7
	lsl	r2, r2, #2
	bl	ftl_memset
.L685:
	ldrh	r3, [r5, #2]
	movw	r2, #65535
	cmp	r3, r2
	ldmfdeq	sp, {r4, r5, r6, r7, r8, fp, sp, pc}
	ldrh	r2, [r6, #98]
	cmp	r2, r3
	bhi	.L684
	mov	r2, #74
	mov	r1, r8
	ldr	r0, .L688+8
	bl	sftl_printk
.L684:
	ldrh	r3, [r5, #2]!
	mov	r0, #1
	cmp	r4, r5
	lsr	r1, r3, #5
	and	r3, r3, #31
	ldr	r2, [r7, r1, lsl #2]
	orr	r3, r2, r0, lsl r3
	str	r3, [r7, r1, lsl #2]
	bne	.L685
	ldmfd	sp, {r4, r5, r6, r7, r8, fp, sp, pc}
.L689:
	.align	2
.L688:
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
.L691:
	ldrh	r1, [r4, #6]
	uxth	r3, r2
	cmp	r1, r3
	bhi	.L695
	ldrh	r2, [r5]
	mov	r7, #0
	ldr	ip, .L704
	mov	r10, r7
.L696:
	ldrh	r1, [r4, #10]
	uxth	r3, r7
	cmp	r1, r3
	bhi	.L700
	mov	r0, r10
	sub	sp, fp, #40
	ldmfd	sp, {r4, r5, r6, r7, r8, r9, r10, fp, sp, pc}
.L695:
	uxth	r3, r2
	mov	r1, #0
	ldr	r0, [r6, r3, lsl #2]
	ubfx	r0, r0, #10, #16
.L692:
	ldrh	ip, [r4, #10]
	uxth	r3, r1
	cmp	ip, r3
	addls	r2, r2, #1
	bls	.L691
.L694:
	uxth	r3, r1
	add	r1, r1, #1
	lsl	r3, r3, #1
	ldrh	ip, [r9, r3]
	cmp	ip, r0
	ldrheq	ip, [r5, r3]
	addeq	ip, ip, #1
	strheq	ip, [r5, r3]	@ movhi
	b	.L692
.L700:
	ldrh	r1, [r4]
	uxth	r6, r7
	cmp	r1, r3
	bne	.L697
	ldrh	r0, [r4, #2]
	ldrh	r1, [ip, #104]
	cmp	r0, r1
	lslcc	r0, r6, #1
	strhcc	r1, [r5, r0]	@ movhi
.L697:
	lsl	r6, r6, #1
	ldrh	r8, [r5, r6]
	cmp	r2, r8
	movhi	r10, r3
	movhi	r2, r8
	cmp	r8, #0
	bne	.L699
	ldrh	r0, [r9, r6]
	cmp	r0, #0
	beq	.L699
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
.L699:
	add	r7, r7, #1
	b	.L696
.L705:
	.align	2
.L704:
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
	bne	.L707
	ldrh	r3, [r0, #8]
	cmp	r3, #0
	beq	.L708
	movw	r2, #677
	ldr	r1, .L716
	ldr	r0, .L716+4
	bl	sftl_printk
.L708:
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
.L709:
	mov	r0, #0
	ldmfd	sp, {r4, r5, r6, r7, fp, sp, pc}
.L707:
	lsl	r3, r3, #1
	ldr	r2, [r0, #28]
	ldr	r6, [r0, #24]
	mov	r1, #255
	ldrh	r7, [r5, r3]
	ldrh	r3, [r0, #2]
	ldr	r5, .L716+8
	orr	r3, r3, r7, lsl #10
	str	r3, [r5, #3268]
	ldr	r3, [r5, #3284]
	str	r3, [r5, #3272]
	ldr	r3, [r5, #3288]
	str	r3, [r5, #3276]
	str	r2, [r3, #4]
	ldr	r2, .L716+12
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
.L710:
	ldrh	r0, [r4, #6]
	uxth	r1, r2
	cmp	r0, r1
	bhi	.L712
	mov	r2, #1
	mov	r3, #0
	mov	r1, r2
	ldr	r0, .L716+16
	bl	FlashProgPages
	ldrh	r3, [r4, #2]
	mov	r0, r4
	add	r3, r3, #1
	strh	r3, [r4, #2]	@ movhi
	bl	ftl_map_blk_gc
	b	.L709
.L712:
	uxth	r1, r2
	ldr	r0, [r6, r1, lsl #2]
	cmp	r7, r0, lsr #10
	bne	.L711
	ldr	r0, [r5, #3284]
	add	r3, r3, #1
	uxth	r3, r3
	str	r1, [r0, r3, lsl #3]
	ldr	r0, [r6, r1, lsl #2]
	ldr	r1, [r5, #3284]
	add	r1, r1, r3, lsl #3
	str	r0, [r1, #4]
.L711:
	add	r2, r2, #1
	b	.L710
.L717:
	.align	2
.L716:
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
	ldr	r7, .L738
	mov	r4, r0
	mov	r8, r1
	mov	r9, r2
	mov	r6, #0
	mov	r5, r7
.L719:
	ldr	r3, [r7, #2524]
	add	r3, r3, #1
	str	r3, [r7, #2524]
	ldrh	r3, [r7, #104]
	ldrh	r2, [r4, #2]
	sub	r3, r3, #1
	cmp	r2, r3
	bge	.L720
	ldrh	r2, [r4]
	movw	r3, #65535
	cmp	r2, r3
	bne	.L721
.L720:
	mov	r0, r4
	bl	Ftl_write_map_blk_to_last_page
.L721:
	ldrh	r3, [r4]
	ldr	r2, [r4, #12]
	lsl	r3, r3, #1
	ldrh	r3, [r2, r3]
	cmp	r3, #0
	bne	.L722
	movw	r2, #735
	ldr	r1, .L738+4
	ldr	r0, .L738+8
	bl	sftl_printk
.L722:
	ldrh	r2, [r4]
	ldrh	r3, [r4, #10]
	cmp	r2, r3
	bcc	.L723
	mov	r2, #736
	ldr	r1, .L738+4
	ldr	r0, .L738+8
	bl	sftl_printk
.L723:
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
	ldr	r0, .L738+12
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
	bne	.L724
	ldr	r1, [r5, #3268]
	add	r6, r6, #1
	ldr	r0, .L738+16
	uxth	r6, r6
	bl	sftl_printk
	ldrh	r3, [r4, #2]
	cmp	r3, #2
	ldrhls	r3, [r5, #104]
	subls	r3, r3, #1
	strhls	r3, [r4, #2]	@ movhi
	cmp	r6, #3
	bls	.L726
	mov	r2, r6
	ldr	r1, [r5, #3268]
	ldr	r0, .L738+20
	bl	sftl_printk
.L727:
	b	.L727
.L726:
	ldr	r3, [r4, #32]
	cmp	r3, #0
	beq	.L719
.L737:
	b	.L737
.L724:
	cmp	r3, #0
	strhne	r10, [r4, #40]	@ movhi
	cmp	r0, #1
	cmpne	r3, #256
	moveq	r0, #1
	movne	r0, #0
	beq	.L719
	ldr	r2, [r5, #3268]
	ldr	r3, [r4, #24]
	str	r2, [r3, r8, lsl #2]
	ldmfd	sp, {r4, r5, r6, r7, r8, r9, r10, fp, sp, pc}
.L739:
	.align	2
.L738:
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
	ldr	r4, .L747
	uxth	r6, r0
	uxth	r8, r1
	ldrh	r3, [r4, #136]
	cmp	r3, r6
	bcs	.L741
	movw	r2, #510
	ldr	r1, .L747+4
	ldr	r0, .L747+8
	bl	sftl_printk
.L741:
	ldr	r3, [r4, #3892]
	mov	r5, #12
	ldr	r7, [r3, r6, lsl #2]
	cmp	r7, #0
	bne	.L742
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
.L743:
	mov	r0, #0
	ldmfd	sp, {r4, r5, r6, r7, r8, fp, sp, pc}
.L742:
	mul	r5, r5, r8
	ldr	r3, [r4, #2492]
	mov	r2, #1
	ldr	r0, .L747+12
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
	bne	.L744
	mov	r2, r7
	mov	r1, r6
	ldr	r0, .L747+16
	bl	sftl_printk
	ldr	r0, .L747+20
	lsr	r3, r7, #10
	mov	r1, r6
	strh	r3, [r0, #40]	@ movhi
	ldr	r3, [r4, #2492]
	add	r3, r3, r5
	ldr	r2, [r3, #8]
	bl	FtlMapWritePage
.L744:
	ldrh	r3, [r8, #8]
	cmp	r3, r6
	beq	.L745
	mov	r2, r7
	mov	r1, r6
	ldr	r0, .L747+24
	bl	sftl_printk
	mov	r3, #4
	ldr	r1, [r4, #3276]
	mov	r2, r3
	ldr	r0, .L747+28
	bl	rknand_print_hex
	ldrh	r3, [r4, #136]
	mov	r2, #4
	ldr	r1, [r4, #3892]
	ldr	r0, .L747+32
	bl	rknand_print_hex
.L745:
	ldrh	r3, [r8, #8]
	cmp	r3, r6
	beq	.L746
	movw	r2, #535
	ldr	r1, .L747+4
	ldr	r0, .L747+8
	bl	sftl_printk
.L746:
	ldr	r3, [r4, #2492]
	mov	r1, #0
	add	r2, r3, r5
	str	r1, [r2, #4]
	strh	r6, [r3, r5]	@ movhi
	b	.L743
.L748:
	.align	2
.L747:
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
	ldr	r5, .L772
	cmp	r2, r3
	bge	.L750
	movw	r3, #65535
	cmp	r1, r3
	beq	.L752
	ldrh	r2, [r4, #2]
	ldrh	r3, [r5, #104]
	cmp	r2, r3
	bcc	.L753
.L750:
	movw	r3, #65535
	uxth	r0, r0
	cmp	r1, r3
	beq	.L754
	ldrh	r2, [r5, #104]
	ldrh	r3, [r4, #2]
	cmp	r2, r3
	subls	lr, r6, #2
	movls	r2, #0
	bls	.L755
.L754:
	lsl	r0, r0, #1
	ldrh	r8, [r6, r0]
	cmp	r8, #0
	beq	.L752
	ldr	r3, [r4, #32]
	cmp	r3, #0
	bne	.L752
	mov	r2, #1
	str	r2, [r4, #32]
	strh	r3, [r6, r0]	@ movhi
	ldrh	r3, [r4, #8]
	ldrh	r2, [r4, #2]
	sub	r3, r3, #1
	strh	r3, [r4, #8]	@ movhi
	ldrh	r3, [r5, #104]
	cmp	r2, r3
	bcc	.L759
	mov	r0, r4
	bl	ftl_map_blk_alloc_new_blk
.L759:
	mov	r6, #0
.L760:
	ldrh	r3, [r4, #6]
	uxth	r10, r6
	cmp	r3, r10
	bhi	.L767
	mov	r1, #1
	mov	r0, r8
	bl	FtlFreeSysBlkQueueIn
	mov	r3, #0
	str	r3, [r4, #32]
.L752:
	ldrh	r2, [r4, #2]
	ldrh	r3, [r5, #104]
	cmp	r2, r3
	bcc	.L753
	mov	r0, r4
	bl	ftl_map_blk_alloc_new_blk
.L753:
	mov	r0, #0
	sub	sp, fp, #40
	ldmfd	sp, {r4, r5, r6, r7, r8, r9, r10, fp, sp, pc}
.L757:
	ldrh	r10, [lr, #2]!
	add	r8, r2, #1
	cmp	r10, r1
	beq	.L756
	mov	r2, r8
.L755:
	uxth	r7, r2
	cmp	r7, ip
	bcc	.L757
	mov	r7, r0
.L756:
	uxth	r2, r2
	ldr	r0, .L772+4
	lsl	r2, r2, #1
	ldrh	r2, [r6, r2]
	bl	sftl_printk
	mvn	r3, #0
	mov	r0, r7
	strh	r3, [r4, #40]	@ movhi
	b	.L754
.L767:
	uxth	r7, r6
	add	r3, r9, r7, lsl #2
	str	r3, [fp, #-44]
	ldr	r3, [r9, r7, lsl #2]
	cmp	r8, r3, lsr #10
	bne	.L761
	ldr	r3, [r5, #3848]
	ldr	r0, .L772+8
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
	beq	.L762
	movw	r2, #647
	ldr	r1, .L772+12
	ldr	r0, .L772+16
	bl	sftl_printk
	ldr	r3, [fp, #-48]
.L762:
	ldr	r2, [r5, #3264]
	cmn	r2, #1
	bne	.L763
.L765:
	ldr	r2, [fp, #-44]
	mov	r3, #0
	str	r3, [r2]
.L764:
	b	.L764
.L763:
	ldrh	r2, [r3, #8]
	cmp	r2, r10
	bne	.L765
	ldrh	r2, [r3]
	ldrh	r3, [r4, #4]
	cmp	r2, r3
	bne	.L765
	ldr	r2, [r5, #3272]
	mov	r1, r7
	mov	r0, r4
	bl	FtlMapWritePage
.L761:
	add	r6, r6, #1
	b	.L760
.L773:
	.align	2
.L772:
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
	ldr	r5, .L775
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
.L776:
	.align	2
.L775:
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
	ldr	r4, .L791
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
	bcc	.L778
	movw	r2, #847
	ldr	r1, .L791+4
	ldr	r0, .L791+8
	bl	sftl_printk
.L778:
	ldrh	r1, [r4, #138]
	mov	r2, #0
	ldr	r0, [r4, #2492]
	mov	r3, #12
.L779:
	uxth	r5, r2
	cmp	r5, r1
	bcc	.L784
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
	beq	.L785
	ldr	r3, [r1, #4]
	cmp	r3, #0
	bge	.L785
	bl	flush_l2p_region
.L785:
	mov	r1, r5
	uxth	r0, r9
	bl	load_l2p_region
	b	.L780
.L784:
	add	r2, r2, #1
	mla	ip, r3, r2, r0
	ldrh	ip, [ip, #-12]
	cmp	ip, r8
	bne	.L779
.L780:
	cmp	r10, #0
	mov	r3, #12
	bne	.L781
	ldr	r2, [r4, #2492]
	mla	r3, r3, r5, r2
	ldr	r3, [r3, #8]
	ldr	r3, [r3, r6, lsl #2]
	str	r3, [r7]
.L782:
	ldr	r2, [r4, #2492]
	mov	r3, #12
	mov	r0, #0
	mla	r5, r3, r5, r2
	ldr	r3, [r5, #4]
	cmn	r3, #1
	addne	r3, r3, #1
	strne	r3, [r5, #4]
	ldmib	sp, {r4, r5, r6, r7, r8, r9, r10, fp, sp, pc}
.L781:
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
	ldr	r3, .L791+12
	strh	r8, [r3]	@ movhi
	b	.L782
.L792:
	.align	2
.L791:
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
	ldr	r7, .L803
	ubfx	r0, r1, #10, #16
	str	r1, [fp, #-32]
	bl	P2V_block_in_plane
	ldr	r2, [r7, #2324]
	lsl	r5, r0, #1
	ldrh	r3, [r2, r5]
	cmp	r3, #0
	bne	.L794
	ldr	r4, [r7, #2340]
	cmp	r4, #0
	beq	.L795
	movw	r2, #2344
	movw	lr, #65535
	ldrh	ip, [r7, r2]
	ldr	r2, [r7, #2316]
	sub	r4, r4, r2
	ubfx	r4, r4, #3, #16
.L796:
	uxth	r1, r3
	cmp	ip, r1
	bls	.L795
	cmp	r4, r0
	bne	.L797
	mov	r1, r4
	ldr	r0, .L803+4
	bl	List_remove_node
	movw	r3, #2344
	ldrh	r3, [r7, r3]
	cmp	r3, #0
	bne	.L798
	movw	r2, #1737
	ldr	r1, .L803+8
	ldr	r0, .L803+12
	bl	sftl_printk
.L798:
	movw	r2, #2344
	mov	r0, r4
	ldrh	r3, [r7, r2]
	sub	r3, r3, #1
	strh	r3, [r7, r2]	@ movhi
	bl	INSERT_DATA_LIST
	ldr	r2, [r7, #2324]
	ldrh	r3, [r2, r5]
.L794:
	add	r3, r3, #1
	strh	r3, [r2, r5]	@ movhi
	b	.L795
.L797:
	lsl	r4, r4, #3
	add	r3, r3, #1
	ldrh	r4, [r2, r4]
	cmp	r4, lr
	bne	.L796
.L795:
	mov	r2, #1
	sub	r1, fp, #32
	mov	r0, r6
	bl	log2phys
	ldmib	sp, {r4, r5, r6, r7, fp, sp, pc}
.L804:
	.align	2
.L803:
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
	ldr	r6, .L813
	mov	r1, #0
	ldr	r0, .L813+4
	bl	sftl_printk
	mov	r4, r6
.L806:
	ldr	r3, [r6, #2504]
	cmp	r5, r3
	bcc	.L812
	sub	sp, fp, #28
	ldmfd	sp, {r4, r5, r6, r7, fp, sp, pc}
.L812:
	mov	r2, #0
	sub	r1, fp, #32
	mov	r0, r5
	bl	log2phys
	ubfx	r3, r5, #0, #11
	cmp	r3, #0
	bne	.L807
	ldr	r2, [fp, #-32]
	mov	r1, r5
	ldr	r0, .L813+8
	bl	sftl_printk
.L807:
	ldr	r3, [fp, #-32]
	cmn	r3, #1
	beq	.L809
	str	r3, [r4, #3268]
	mov	r2, #0
	ldr	r3, [r4, #3284]
	mov	r1, #1
	ldr	r7, [r4, #3288]
	ldr	r0, .L813+12
	str	r3, [r4, #3272]
	str	r5, [r4, #3280]
	str	r7, [r4, #3276]
	str	r2, [r4, #3264]
	bl	FlashReadPages
	ldr	r3, [r4, #3264]
	cmn	r3, #1
	cmpne	r3, #256
	beq	.L810
	ldr	r3, [r7, #8]
	cmp	r5, r3
	beq	.L809
.L810:
	ldr	r2, [r4, #3272]
	ldr	r3, [r4, #3276]
	ldr	r0, .L813+16
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
.L809:
	add	r5, r5, #1
	b	.L806
.L814:
	.align	2
.L813:
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
	ldr	r3, .L828
	ldr	r0, [r3, #2716]
	mov	r4, r3
	cmp	r0, #0
	beq	.L816
	ldr	r1, [r3, #2720]
	ldr	r2, [r3, #2504]
	cmp	r1, r2
	bcs	.L817
	mov	r5, #2048
.L822:
	ldr	r0, [r4, #2720]
	ldr	r3, [r4, #2504]
	cmp	r0, r3
	bcs	.L819
	mov	r2, #0
	sub	r1, fp, #108
	bl	log2phys
	ldr	r2, [fp, #-108]
	ldr	r3, [r4, #2720]
	cmn	r2, #1
	add	r3, r3, #1
	str	r3, [r4, #2720]
	beq	.L820
	str	r3, [fp, #-88]
	sub	r0, fp, #20
	ldr	r3, .L828+4
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
	bne	.L819
	ldr	r0, [fp, #-108]
	ubfx	r0, r0, #10, #16
	bl	P2V_block_in_plane
	bl	FtlGcRefreshBlock
.L819:
	mvn	r0, #0
.L815:
	sub	sp, fp, #20
	ldmfd	sp, {r4, r5, fp, sp, pc}
.L820:
	subs	r5, r5, #1
	bne	.L822
	b	.L819
.L817:
	ldr	r2, [r3, #2508]
	mov	r0, #0
	str	r0, [r3, #2716]
	str	r0, [r3, #2720]
	str	r2, [r3, #2712]
	b	.L815
.L816:
	ldr	r1, [r3, #2508]
	ldr	r2, [r3, #2712]
	add	ip, r1, #1048576
	cmp	r2, ip
	bhi	.L825
	ldr	r3, [r3, #2564]
	mov	ip, #33554432
	lsr	r3, r3, #10
	add	r2, r2, ip, asr r3
	cmp	r1, r2
	bhi	.L825
	ldr	r3, .L828+8
	ldrb	r3, [r3, #28]	@ zero_extendqisi2
	cmp	r3, #0
	bne	.L815
.L825:
	mov	r3, #1
	mov	r0, #0
	str	r3, [r4, #2716]
	str	r0, [r4, #2720]
	str	r1, [r4, #2712]
	b	.L815
.L829:
	.align	2
.L828:
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
	ldr	r6, .L854
	ldr	r1, .L854+4
	ldr	r0, .L854+8
	mov	r5, r6
	bl	sftl_printk
	mov	r1, #8192
	ldr	r0, .L854+12
	bl	__memzero
.L831:
	ldr	r3, [r6, #2504]
	cmp	r4, r3
	bcc	.L833
	ldr	r8, .L854+12
	mov	r4, #0
	ldr	r9, .L854+16
	mov	r6, r4
.L834:
	ldrh	r2, [r5, #40]
	uxth	r3, r4
	cmp	r2, r3
	bhi	.L836
	ldr	r4, [r5, #2340]
	cmp	r4, #0
	beq	.L837
	movw	r3, #2344
	ldr	r9, .L854+12
	ldrh	r8, [r5, r3]
	mov	r7, #0
	ldr	r3, [r5, #2316]
	ldr	r10, .L854+20
	sub	r4, r4, r3
	ubfx	r4, r4, #3, #16
.L838:
	uxth	r3, r7
	cmp	r8, r3
	bls	.L837
	ldr	r2, [r5, #2324]
	lsl	r3, r4, #1
	ldrh	r2, [r2, r3]
	cmp	r2, #0
	beq	.L839
	mov	r6, #1
	ldrh	r3, [r9, r3]
	mov	r1, r4
	mov	r0, r10
	bl	sftl_printk
.L839:
	ldr	r3, [r5, #2316]
	lsl	r4, r4, #3
	add	r7, r7, #1
	ldrh	r4, [r3, r4]
	movw	r3, #65535
	cmp	r4, r3
	bne	.L838
.L837:
	cmp	r6, #0
	ldmibeq	sp, {r4, r5, r6, r7, r8, r9, r10, fp, sp, pc}
	movw	r2, #2373
	ldr	r1, .L854+4
	ldr	r0, .L854+24
	bl	sftl_printk
	ldmib	sp, {r4, r5, r6, r7, r8, r9, r10, fp, sp, pc}
.L833:
	mov	r2, #0
	sub	r1, fp, #44
	mov	r0, r4
	bl	log2phys
	ldr	r0, [fp, #-44]
	cmn	r0, #1
	beq	.L832
	ubfx	r0, r0, #10, #16
	bl	P2V_block_in_plane
	ldr	r2, .L854+12
	lsl	r0, r0, #1
	ldrh	r3, [r2, r0]
	add	r3, r3, #1
	strh	r3, [r2, r0]	@ movhi
.L832:
	add	r4, r4, #1
	b	.L831
.L836:
	uxth	r1, r4
	ldr	r3, [r5, #2324]
	lsl	r7, r1, #1
	ldrh	r2, [r3, r7]
	ldrh	r3, [r8, r7]
	cmp	r2, r3
	beq	.L835
	mov	r0, r9
	bl	sftl_printk
	ldr	r3, [r5, #2324]
	movw	r2, #65535
	ldrh	r3, [r3, r7]
	cmp	r3, r2
	beq	.L835
	ldrh	r2, [r8, r7]
	cmp	r2, r3
	movhi	r6, #1
.L835:
	add	r4, r4, #1
	b	.L834
.L855:
	.align	2
.L854:
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
	ldr	r3, .L863
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
	beq	.L858
	mov	r2, #1
	add	r0, r3, #3264
	mov	r1, r2
	bl	FlashReadPages
.L859:
	ldr	r2, [r4, #3272]
	mov	r1, r5
	mov	r0, r6
	bl	FtlMapWritePage
	ldmfd	sp, {r4, r5, r6, fp, sp, pc}
.L858:
	ldrh	r2, [r3, #110]
	mov	r1, #255
	bl	ftl_memset
	b	.L859
.L864:
	.align	2
.L863:
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
	ldr	r4, .L946
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
.L866:
	ldrh	r3, [r6, #42]
	ldr	r2, [fp, #-44]
	cmp	r3, r2
	bls	.L907
	ldr	r1, [r6, #3176]
	mov	r7, #0
	ldrh	r2, [r6, #32]
	mov	r10, r7
	ldr	r8, [r6, #3836]
	ldr	r3, [r6, #3172]
	str	r1, [fp, #-48]
	ldrh	r9, [r6, #112]
	ldr	ip, .L946+4
	b	.L908
.L868:
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
	bne	.L867
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
.L867:
	add	r7, r7, #1
.L908:
	uxth	r1, r7
	cmp	r2, r1
	bhi	.L868
	cmp	r10, #0
	bne	.L869
.L906:
	ldr	r3, [fp, #-44]
	add	r3, r3, #1
	uxth	r3, r3
	str	r3, [fp, #-44]
	b	.L866
.L869:
	mov	r2, #1
	mov	r1, r10
	mov	r0, r8
	bl	FlashReadPages
	mov	r3, #0
.L945:
	str	r3, [fp, #-48]
	ldrh	r3, [fp, #-48]
	cmp	r10, r3
	bls	.L906
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
	bne	.L873
	mov	r3, #16
.L875:
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
	bne	.L872
	ldr	r3, [r4, #3836]
	mvn	r2, #0
	str	r2, [r3, r9]
	ldr	r3, [r4, #3836]
	ldr	r3, [r3, r9]
	cmp	r3, r2
	bne	.L873
.L874:
	mov	r1, #0
	uxth	r0, ip
	bl	FtlFreeSysBlkQueueIn
	b	.L878
.L872:
	ldr	r2, [r4, #3836]
	ldr	r2, [r2, r9]
	cmn	r2, #1
	bne	.L873
	sub	r3, r3, #1
	uxth	r3, r3
	cmp	r3, #0
	bne	.L875
	b	.L874
.L873:
	ldr	r2, [r6, #2540]
	ldr	r3, [r7, #4]
	cmn	r2, #1
	beq	.L876
	cmp	r2, r3
	bhi	.L877
.L876:
	cmn	r3, #1
	addne	r2, r3, #1
	strne	r2, [r5, #2540]
.L877:
	ldrh	r2, [r7]
	movw	r1, #61604
	cmp	r2, r1
	beq	.L879
	bhi	.L880
	movw	r3, #61574
	cmp	r2, r3
	beq	.L881
.L878:
	ldr	r3, [fp, #-48]
	add	r3, r3, #1
	b	.L945
.L880:
	movw	r3, #61634
	cmp	r2, r3
	beq	.L882
	movw	r3, #65535
	cmp	r2, r3
	beq	.L874
	b	.L878
.L882:
	ldr	r9, .L946+8
	ldr	r3, [r4, #128]
	ldrh	r2, [r9]
	cmp	r2, r3
	bls	.L884
	movw	r2, #1256
	ldr	r1, .L946+12
	ldr	r0, .L946+16
	bl	sftl_printk
.L884:
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
.L885:
	ldr	r1, [fp, #-52]
	cmp	r3, r1
	bgt	.L891
	cmp	r3, #0
	bge	.L923
	b	.L878
.L891:
	lsl	lr, r3, #2
	ldr	r1, [r7, #4]
	str	lr, [fp, #-56]
	ldr	lr, [ip, r3, lsl #2]
	cmp	r1, lr
	bls	.L886
	ldr	r1, [ip]
	cmp	r1, #0
	bne	.L887
	cmp	r0, r2
	addne	r2, r2, #1
	strhne	r2, [r9]	@ movhi
.L887:
	uxth	ip, r3
	mov	r1, #0
.L888:
	uxth	r0, r1
	sxth	r2, r1
	cmp	ip, r0
	bhi	.L889
	ldr	r1, [r7, #4]
	cmp	r3, #0
	ldr	r2, [r5, #3896]
	ldr	r0, [fp, #-56]
	str	r1, [r2, r0]
	lsl	r2, r3, #1
	ldr	r1, [r5, #3872]
	strh	r8, [r1, r2]	@ movhi
	blt	.L878
	ldr	r2, .L946+8
	ldr	r1, [r5, #128]
	ldrh	r2, [r2]
	sub	r1, r1, r2
	sub	r1, r1, #1
	sxth	r1, r1
	cmp	r3, r1
	bgt	.L878
.L923:
	add	r2, r2, #1
	ldr	r1, [r7, #4]
	strh	r2, [r9]	@ movhi
	ldr	r2, [r4, #3896]
	str	r1, [r2, r3, lsl #2]
	lsl	r3, r3, #1
	ldr	r2, [r4, #3872]
.L943:
	strh	r8, [r2, r3]	@ movhi
	b	.L878
.L889:
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
	b	.L888
.L886:
	sub	r3, r3, #1
	sxth	r3, r3
	b	.L885
.L881:
	ldrh	r2, [r4, #144]
	ldrh	r3, [r4, #120]
	cmp	r2, r3
	bls	.L894
	movw	r2, #1297
	ldr	r1, .L946+12
	ldr	r0, .L946+16
	bl	sftl_printk
.L894:
	ldrh	r2, [r4, #120]
	ldrh	r1, [r4, #144]
	ldr	ip, [r4, #3884]
	sub	r0, r2, #1
	sxth	r3, r0
	sub	r0, r0, r1
	str	r0, [fp, #-52]
.L895:
	ldr	r0, [fp, #-52]
	cmp	r3, r0
	ble	.L900
	ldr	r9, [ip, r3, lsl #2]
	lsl	lr, r3, #2
	ldr	r0, [r7, #4]
	cmp	r0, r9
	bls	.L896
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
.L898:
	uxth	r0, r1
	sxth	r2, r1
	cmp	ip, r0
	bhi	.L899
	ldr	r1, [r7, #4]
	ldr	r2, [r5, #3884]
	str	r1, [r2, lr]
	lsl	r2, r3, #1
	ldr	r1, [r5, #148]
	strh	r8, [r1, r2]	@ movhi
.L900:
	cmp	r3, #0
	blt	.L878
	ldrh	r2, [r5, #120]
	ldrh	r1, [r5, #144]
	sub	r2, r2, #1
	sub	r2, r2, r1
	sxth	r2, r2
	cmp	r3, r2
	bgt	.L878
	add	r1, r1, #1
	ldr	r2, [r5, #3884]
	strh	r1, [r5, #144]	@ movhi
	ldr	r1, [r7, #4]
	str	r1, [r2, r3, lsl #2]
	lsl	r3, r3, #1
	ldr	r2, [r5, #148]
	b	.L943
.L899:
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
	b	.L898
.L896:
	sub	r3, r3, #1
	sxth	r3, r3
	b	.L895
.L879:
	ldr	r9, .L946+20
	movw	r2, #65535
	ldrh	r1, [r9]
	cmp	r1, r2
	strheq	r8, [r9]	@ movhi
	beq	.L944
	ldrh	r0, [r9, #4]
	cmp	r0, r2
	beq	.L903
	mov	r1, #1
	bl	FtlFreeSysBlkQueueIn
.L903:
	ldr	r3, [r7, #4]
	ldr	r2, [r4, #2584]
	cmp	r2, r3
	strhcs	r8, [r9, #4]	@ movhi
	bcs	.L878
	ldrh	r3, [r9]
	strh	r8, [r9]	@ movhi
	strh	r3, [r9, #4]	@ movhi
	ldr	r3, [r7, #4]
.L944:
	str	r3, [r4, #2584]
	b	.L878
.L907:
	ldr	r2, [r6, #3872]
	ldrh	r3, [r2]
	cmp	r3, #0
	beq	.L909
.L912:
	ldr	r1, [r4, #148]
	ldrh	r2, [r1]
	cmp	r2, #0
	beq	.L910
.L911:
	ldr	r3, .L946+8
	ldrh	r2, [r3]
	ldr	r3, [r4, #128]
	cmp	r2, r3
	bls	.L942
	movw	r2, #1422
	ldr	r1, .L946+12
	ldr	r0, .L946+16
	bl	sftl_printk
.L942:
	mov	r0, #0
	sub	sp, fp, #40
	ldmfd	sp, {r4, r5, r6, r7, r8, r9, r10, fp, sp, pc}
.L909:
	ldr	r1, .L946+8
	ldrh	r1, [r1]
	cmp	r1, #0
	ldrne	r1, [r6, #128]
	beq	.L912
.L913:
	sxth	ip, r3
	cmp	ip, r1
	bcs	.L912
	lsl	r0, ip, #1
	add	r3, r3, #1
	ldrh	r0, [r2, r0]
	cmp	r0, #0
	beq	.L913
	mov	r3, ip
	mov	lr, #0
.L914:
	ldr	r2, [r5, #128]
	cmp	r3, r2
	bcs	.L912
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
	b	.L914
.L910:
	ldrh	r3, [r4, #144]
	cmp	r3, #0
	ldrhne	r0, [r4, #120]
	beq	.L911
.L919:
	sxth	r3, r2
	cmp	r3, r0
	mov	ip, r3
	bge	.L911
	lsl	lr, r3, #1
	add	r2, r2, #1
	ldrh	lr, [r1, lr]
	cmp	lr, #0
	beq	.L919
	mov	lr, #0
.L920:
	ldrh	r2, [r5, #120]
	cmp	r3, r2
	bge	.L911
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
	b	.L920
.L947:
	.align	2
.L946:
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
	ldr	r6, .L973
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
.L949:
	ldr	r3, [fp, #-52]
	sxth	r2, r7
	cmp	r2, r3
	bge	.L967
	ldr	r3, [fp, #-52]
	lsl	r0, r2, #1
	sub	r1, r3, #1
	cmp	r2, r1
	bne	.L950
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
	ldr	ip, .L973+4
	strh	r7, [r4]	@ movhi
	add	r7, r1, #1
	strh	r0, [r4, #2]	@ movhi
	ldr	r2, [r3, r2, lsl #2]
	str	r2, [r4, #28]
.L951:
	sxth	r2, r8
	cmp	r2, r7
	blt	.L954
.L967:
	mov	r0, r4
	bl	ftl_free_no_use_map_blk
	ldrh	r2, [r4, #2]
	ldrh	r3, [r5, #104]
	cmp	r2, r3
	bne	.L956
	mov	r0, r4
	bl	ftl_map_blk_alloc_new_blk
.L956:
	mov	r0, r4
	bl	ftl_map_blk_gc
	mov	r0, r4
	bl	ftl_map_blk_gc
	mov	r0, #0
	sub	sp, fp, #40
	ldmfd	sp, {r4, r5, r6, r7, r8, r9, r10, fp, sp, pc}
.L954:
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
	beq	.L953
	ldrh	r2, [r9, #8]
	ldr	r3, [fp, #-48]
	cmp	r3, r2
	bls	.L953
	ldrh	r1, [r4, #4]
	ldrh	r0, [r9]
	cmp	r0, r1
	ldreq	r1, [r6, #3268]
	ldreq	r3, [fp, #-44]
	streq	r1, [r3, r2, lsl #2]
.L953:
	add	r8, r8, #1
	b	.L951
.L950:
	ldr	r2, [r5, #3284]
	add	r3, r8, r0
	str	r3, [fp, #-60]
	str	r2, [r5, #3272]
	ldrh	r2, [r5, #104]
	ldrh	r1, [r8, r0]
	ldr	r0, .L973+4
	sub	r2, r2, #1
	orr	r2, r2, r1, lsl #10
	str	r2, [r5, #3268]
	mov	r2, #1
	mov	r1, r2
	bl	FlashReadPages
	ldr	r2, [r5, #3264]
	cmn	r2, #1
	beq	.L969
	ldrh	r1, [r9]
	ldrh	r2, [r4, #4]
	cmp	r1, r2
	bne	.L969
	ldrh	r1, [r9, #8]
	movw	r2, #64245
	cmp	r1, r2
	beq	.L958
.L969:
	mov	r10, #0
.L959:
	ldrh	r1, [r5, #104]
	sxth	r2, r10
	cmp	r2, r1
	bge	.L965
	ldr	r3, [fp, #-60]
	ldr	r0, .L973+4
	ldrh	r1, [r3]
	orr	r2, r2, r1, lsl #10
	str	r2, [r5, #3268]
	mov	r2, #1
	mov	r1, r2
	bl	FlashReadPages
	ldr	r2, [r5, #3264]
	cmn	r2, #1
	beq	.L963
	ldrh	r2, [r9, #8]
	ldr	r3, [fp, #-48]
	cmp	r3, r2
	bls	.L963
	ldrh	r1, [r4, #4]
	ldrh	r0, [r9]
	cmp	r0, r1
	ldreq	r1, [r5, #3268]
	ldreq	r3, [fp, #-44]
	streq	r1, [r3, r2, lsl #2]
.L963:
	add	r10, r10, #1
	b	.L959
.L958:
	mov	r0, #0
	mov	lr, #4
.L960:
	ldrh	r1, [r5, #104]
	sxth	r2, r0
	sub	r1, r1, #1
	cmp	r2, r1
	blt	.L962
.L965:
	add	r7, r7, #1
	b	.L949
.L962:
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
	b	.L960
.L974:
	.align	2
.L973:
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
	ldr	r3, .L976
	ldrh	r2, [r3, #120]
	add	r0, r3, #3952
	add	r0, r0, #4
	strh	r2, [r0, #10]	@ movhi
	ldr	r2, .L976+4
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
.L977:
	.align	2
.L976:
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
	ldr	r4, .L981
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
.L979:
	ldrh	r0, [r3, #138]
	uxth	r1, r2
	add	ip, r2, #1
	cmp	r0, r1
	bhi	.L980
	mvn	r1, #0
	movw	r0, #3908
	ldr	r2, .L981+4
	strh	r1, [r3, r0]	@ movhi
	ldr	r0, [r3, #128]
	strh	r1, [r2, #2]	@ movhi
	strh	r1, [r2, #40]	@ movhi
	strh	r0, [r2, #10]	@ movhi
	ldr	r0, .L981+8
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
.L980:
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
	b	.L979
.L982:
	.align	2
.L981:
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
	ldr	r0, .L984
	bl	FtlMapTblRecovery
	mov	r0, #0
	ldmfd	sp, {fp, sp, pc}
.L985:
	.align	2
.L984:
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
	ldr	r4, .L987
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
.L988:
	.align	2
.L987:
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
	ldr	r4, .L1000
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
.L990:
	ldrh	r3, [r4, #40]
	uxth	r10, r5
	cmp	r10, r3
	bcs	.L997
	ldrh	r3, [r4, #102]
	mov	r9, r10
	ldrh	ip, [r4, #32]
	str	r3, [fp, #-44]
	mov	r3, #0
	mov	r7, r3
	b	.L998
.L992:
	ldr	r2, .L1000+4
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
.L998:
	uxth	r1, r3
	cmp	ip, r1
	bhi	.L992
	cmp	r7, #0
	beq	.L993
	mov	r1, r7
	mov	r0, #32768
	bl	__divsi3
	uxth	r7, r0
.L994:
	ldr	r3, [r4, #2316]
	add	r3, r3, r9, lsl #3
	strh	r7, [r3, #4]	@ movhi
	ldr	r3, .L1000+8
	ldrh	r1, [r3]
	cmp	r1, r10
	beq	.L995
	ldrh	r1, [r3, #48]
	cmp	r1, r10
	beq	.L995
	ldrh	r3, [r3, #96]
	cmp	r3, r10
	beq	.L995
	ldr	r2, [r4, #2324]
	lsl	r3, r9, #1
	ldrh	r3, [r2, r3]
	cmp	r3, #0
	bne	.L996
	add	r8, r8, #1
	mov	r0, r9
	uxth	r8, r8
	bl	INSERT_FREE_LIST
.L995:
	add	r5, r5, #1
	b	.L990
.L993:
	ldr	r1, [r4, #2324]
	lsl	r3, r9, #1
	mvn	r0, #0
	strh	r0, [r1, r3]	@ movhi
	b	.L994
.L996:
	add	r6, r6, #1
	mov	r0, r9
	uxth	r6, r6
	bl	INSERT_DATA_LIST
	b	.L995
.L997:
	ldr	r2, [fp, #-48]
	strh	r6, [r2]	@ movhi
	add	r6, r6, r8
	cmp	r6, r3
	movw	r2, #2344
	strh	r8, [r4, r2]	@ movhi
	ble	.L999
	movw	r2, #2206
	ldr	r1, .L1000+12
	ldr	r0, .L1000+16
	bl	sftl_printk
.L999:
	mov	r0, #0
	sub	sp, fp, #40
	ldmfd	sp, {r4, r5, r6, r7, r8, r9, r10, fp, sp, pc}
.L1001:
	.align	2
.L1000:
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
	ldr	r4, .L1003
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
.L1004:
	.align	2
.L1003:
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
	ldr	r3, .L1028
	mov	r10, r1
	ldrh	r4, [r3, #4]
	movw	r3, #65535
	cmp	r4, r3
	beq	.L1022
	cmp	r4, #0
	bne	.L1006
.L1007:
	bl	FtlGcPageVarInit
	b	.L1008
.L1022:
	mov	r4, #0
.L1006:
	ldr	r3, .L1028+4
	ldrh	r3, [r3, #102]
	cmp	r3, r10
	beq	.L1007
.L1008:
	ldr	r5, .L1028+4
	mov	r8, #0
.L1016:
	ldrh	r2, [r6]
	movw	r3, #65535
	mov	r0, #0
	strb	r0, [r6, #8]
	cmp	r2, r3
	beq	.L1009
.L1019:
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
.L1010:
	ldr	r1, [fp, #-44]
	uxth	r3, r2
	cmp	r1, r3
	bhi	.L1012
	mov	r9, #0
	mov	r2, #0
	mov	r1, r7
	bl	FlashReadPages
.L1013:
	uxth	r3, r9
	cmp	r7, r3
	bhi	.L1017
	add	r8, r8, #1
	add	r4, r4, #1
	cmp	r10, r8
	uxth	r4, r4
	bls	.L1018
.L1020:
	ldrh	r3, [r5, #102]
	cmp	r3, r4
	bhi	.L1019
	mov	r0, #0
	b	.L1009
.L1012:
	ldrh	r3, [ip], #2
	cmp	r3, r9
	beq	.L1011
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
.L1011:
	add	r2, r2, #1
	b	.L1010
.L1017:
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
	bne	.L1014
	ldr	ip, [fp, #-48]
	movw	r2, #65535
	add	r9, r9, #1
	ldr	r1, [fp, #-44]
	ldr	r3, [ip, #12]
	ldrh	ip, [r3]
	cmp	ip, r2
	bne	.L1015
	mov	r3, #1
	str	r3, [r5, #4008]
.L1009:
	ldr	r3, .L1028
	mvn	r2, #0
	strb	r0, [r6, #6]
	mov	r1, r4
	strh	r4, [r6, #2]	@ movhi
	strh	r2, [r3, #4]	@ movhi
	mov	r2, r0
	mov	r0, r6
	bl	ftl_sb_update_avl_pages
	b	.L1021
.L1015:
	ldr	r2, [r3, #8]
	ldr	r0, [r3, #12]
	bl	FtlGcUpdatePage
	b	.L1013
.L1014:
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
	b	.L1016
.L1018:
	ldr	r2, .L1028
	movw	r1, #65535
	ldrh	r3, [r2, #4]
	cmp	r3, r1
	beq	.L1020
	add	r3, r3, r8
	strh	r3, [r2, #4]	@ movhi
	ldrh	r3, [r5, #102]
	cmp	r3, r4
	bls	.L1020
.L1021:
	mvn	r0, #0
	sub	sp, fp, #40
	ldmfd	sp, {r4, r5, r6, r7, r8, r9, r10, fp, sp, pc}
.L1029:
	.align	2
.L1028:
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
	ldr	r4, .L1040
	mov	r1, #0
	ldr	r0, [r4, #3284]
	add	r6, r4, #176
	ldr	r3, [r4, #3288]
	add	r7, r4, #3904
	ldrh	r2, [r4, #110]
	str	r0, [r4, #3272]
	str	r3, [r4, #3276]
	bl	ftl_memset
.L1032:
	ldrh	r3, [r4, #54]
	cmp	r5, r3
	blt	.L1033
	ldr	r6, [r4, #3276]
	mov	r2, #16
	mov	r1, #255
	ldr	r9, .L1040+4
	ldr	r8, .L1040+8
	mov	r5, #0
	mov	r0, r6
	mov	r7, r5
	bl	memset
	ldr	r3, .L1040+12
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
.L1034:
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
	ldr	r0, .L1040+8
	mov	r2, r3
	mov	r1, r3
	bl	FlashProgPages
	ldrh	r3, [r4, #104]
	ldrh	r2, [r4, #154]
	sub	r3, r3, #1
	cmp	r2, r3
	blt	.L1035
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
.L1035:
	ldrh	r3, [r4, #154]
	add	r3, r3, #1
	strh	r3, [r4, #154]	@ movhi
	ldr	r3, [r4, #3264]
	cmn	r3, #1
	bne	.L1036
	add	r5, r5, #1
	ldr	r1, [r4, #3268]
	uxth	r5, r5
	ldr	r0, .L1040+16
	bl	sftl_printk
	cmp	r5, #3
	bls	.L1034
	mov	r2, r5
	ldr	r1, [r4, #3268]
	ldr	r0, .L1040+20
	bl	sftl_printk
.L1038:
	b	.L1038
.L1033:
	ldrh	r2, [r7]
	ldr	r3, [r4, #3272]
	ldr	r1, [r6, #4]!
	mul	r0, r2, r5
	lsl	r2, r2, #2
	add	r5, r5, #1
	add	r0, r3, r0, lsl #2
	bl	ftl_memcpy
	b	.L1032
.L1039:
	mov	r7, #1
	b	.L1034
.L1036:
	cmp	r7, #0
	beq	.L1039
	mov	r0, #0
	ldmib	sp, {r4, r5, r6, r7, r8, r9, r10, fp, sp, pc}
.L1041:
	.align	2
.L1040:
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
	ldr	r4, .L1077
	add	r9, r4, #2336
.L1043:
	ldr	r7, .L1077+4
	ldrh	r2, [r9]
	ldrh	r3, [r7]
	add	r3, r3, r2
	ldrh	r2, [r4, #40]
	cmp	r3, r2
	ble	.L1044
	movw	r2, #2654
	ldr	r1, .L1077+8
	ldr	r0, .L1077+12
	bl	sftl_printk
.L1044:
	ldr	r3, .L1077+16
	cmp	r5, r3
	movne	r1, #0
	bne	.L1045
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
.L1045:
	ldr	r0, .L1077+20
	bl	List_pop_index_node
	ldrh	r3, [r7]
	mov	r6, r0
	uxth	r8, r0
	cmp	r3, #0
	bne	.L1046
	movw	r2, #2663
	ldr	r1, .L1077+8
	ldr	r0, .L1077+12
	bl	sftl_printk
.L1046:
	ldrh	r3, [r7]
	mov	r0, r5
	sub	r3, r3, #1
	strh	r3, [r7]	@ movhi
	strh	r8, [r5]	@ movhi
	bl	make_superblock
	ldrb	r3, [r5, #7]	@ zero_extendqisi2
	cmp	r3, #0
	bne	.L1047
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
	ble	.L1043
	movw	r2, #2674
	ldr	r1, .L1077+8
	ldr	r0, .L1077+12
	bl	sftl_printk
	b	.L1043
.L1047:
	ldrh	r2, [r9]
	ldrh	r3, [r7]
	add	r3, r3, r2
	ldrh	r2, [r4, #40]
	cmp	r3, r2
	ble	.L1049
	movw	r2, #2677
	ldr	r1, .L1077+8
	ldr	r0, .L1077+12
	bl	sftl_printk
.L1049:
	ldr	r0, [r4, #3804]
	mov	r2, #20
	ldrh	r1, [r4, #32]
	mov	r7, #0
	add	ip, r5, #16
	mov	lr, r7
	mov	r3, r0
	mla	r1, r2, r1, r0
	str	r1, [fp, #-44]
.L1050:
	ldr	r1, [fp, #-44]
	cmp	r1, r3
	bne	.L1052
	cmp	r7, #0
	bne	.L1053
	mov	r2, #2688
	ldr	r1, .L1077+8
	ldr	r0, .L1077+12
	bl	sftl_printk
.L1053:
	ldr	r3, .L1077+24
	ldrh	r3, [r3]
	cmp	r3, r8
	bne	.L1054
	movw	r2, #2690
	ldr	r1, .L1077+8
	ldr	r0, .L1077+12
	bl	sftl_printk
.L1054:
	ldrb	r3, [r5, #8]	@ zero_extendqisi2
	uxth	r6, r6
	cmp	r3, #0
	bne	.L1055
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
.L1058:
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
	ldr	r0, .L1077+28
	add	r3, r3, #1
	str	r3, [r2, #16]
	mov	r2, #20
	mla	r2, r2, r7, r1
	add	r3, r1, #4
	add	r2, r2, #24
.L1060:
	add	r3, r3, #20
	cmp	r2, r3
	bne	.L1061
	ldrb	r1, [r5, #8]	@ zero_extendqisi2
	mov	r2, r7
	ldr	r0, [r4, #3804]
	bl	FlashEraseBlocks
	mov	r3, #0
	mov	ip, #20
	mov	r1, r3
.L1062:
	uxth	r2, r3
	cmp	r7, r2
	bhi	.L1064
	cmp	r1, #0
	ble	.L1065
	mov	r0, r6
	bl	update_multiplier_value
	bl	FtlBbmTblFlush
.L1065:
	ldrb	r2, [r5, #7]	@ zero_extendqisi2
	cmp	r2, #0
	bne	.L1066
	ldr	r3, [r4, #2324]
	mvn	r2, #0
	strh	r2, [r3, r10]	@ movhi
	b	.L1043
.L1052:
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
	b	.L1050
.L1055:
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
	b	.L1058
.L1061:
	ldr	r1, [r3, #-20]
	and	r1, r1, r0
	str	r1, [r3, #-20]
	b	.L1060
.L1064:
	mul	r2, ip, r3
	ldr	lr, [r4, #3804]
	add	r0, lr, r2
	ldr	r2, [lr, r2]
	cmn	r2, #1
	bne	.L1063
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
.L1063:
	add	r3, r3, #1
	b	.L1062
.L1066:
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
	beq	.L1067
	ldrb	r3, [r5, #7]	@ zero_extendqisi2
	cmp	r3, #0
	bne	.L1068
.L1067:
	movw	r2, #2743
	ldr	r1, .L1077+8
	ldr	r0, .L1077+12
	bl	sftl_printk
.L1068:
	mov	r0, #0
	sub	sp, fp, #40
	ldmfd	sp, {r4, r5, r6, r7, r8, r9, r10, fp, sp, pc}
.L1078:
	.align	2
.L1077:
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
	ldr	r6, .L1091
	str	r3, [fp, #-44]
	movw	r3, #3210
	ldrh	r3, [r6, r3]
	cmp	r3, #0
	movne	r7, #0
	ldrne	r10, .L1091+4
	bne	.L1081
.L1080:
	mov	r0, #0
	sub	sp, fp, #40
	ldmfd	sp, {r4, r5, r6, r7, r8, r9, r10, fp, sp, pc}
.L1087:
	uxtah	r3, r6, r7
	ldr	r1, [fp, #-44]
	mov	r8, #0
	ldrb	r0, [r3, #60]	@ zero_extendqisi2
	bl	V2P_block
	mov	r9, r0
.L1082:
	ldrh	r3, [r10]
	uxth	r4, r8
	cmp	r3, r4
	bhi	.L1086
	add	r7, r7, #1
.L1081:
	ldrh	r2, [r6, #32]
	uxth	r3, r7
	cmp	r2, r3
	bhi	.L1087
	bl	FtlGcReFreshBadBlk
	b	.L1080
.L1086:
	uxth	r3, r8
	ldr	r5, .L1091+8
	lsl	r2, r3, #1
	ldrh	r2, [r5, r2]
	cmp	r2, r9
	bne	.L1083
	mov	r1, r9
	ldr	r0, .L1091+12
	str	r3, [fp, #-48]
	bl	sftl_printk
	mov	r0, r9
	bl	FtlBbmMapBadBlock
	bl	FtlBbmTblFlush
	ldr	r3, [fp, #-48]
	ldrh	r2, [r10]
	add	r3, r5, r3, lsl #1
.L1084:
	cmp	r4, r2
	bcc	.L1085
	sub	r2, r2, #1
	strh	r2, [r10]	@ movhi
.L1083:
	add	r8, r8, #1
	b	.L1082
.L1085:
	ldrh	r1, [r3, #2]!
	add	r4, r4, #1
	uxth	r4, r4
	strh	r1, [r3, #-2]	@ movhi
	b	.L1084
.L1092:
	.align	2
.L1091:
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
	ldr	r3, .L1103
	uxth	r4, r0
	ldr	r1, [r3, #2324]
	lsl	r2, r4, #1
	mov	r6, r3
	ldrh	r0, [r1, r2]
	cmp	r0, #0
	bne	.L1094
	movw	r2, #2588
	ldrh	r1, [r3, r2]
	cmp	r1, r4
	mvneq	r3, #0
	strheq	r3, [r6, r2]	@ movhi
	beq	.L1096
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
.L1096:
	ldr	r5, .L1103+4
	mov	r1, r4
	ldr	r0, .L1103+8
	bl	List_remove_node
	ldrh	r3, [r5]
	cmp	r3, #0
	bne	.L1098
	movw	r2, #2813
	ldr	r1, .L1103+12
	ldr	r0, .L1103+16
	bl	sftl_printk
.L1098:
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
	ble	.L1102
	mov	r2, #2816
	ldr	r1, .L1103+12
	ldr	r0, .L1103+16
	bl	sftl_printk
.L1102:
	mov	r0, #1
	ldmfd	sp, {r4, r5, r6, fp, sp, pc}
.L1094:
	mov	r0, r4
	bl	List_update_data_list
	mov	r0, #0
	ldmfd	sp, {r4, r5, r6, fp, sp, pc}
.L1104:
	.align	2
.L1103:
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
	ldr	r5, .L1120
	beq	.L1106
	ldr	r3, [r5, #2324]
	lsl	r6, r4, #1
	ldrh	r2, [r3, r6]
	cmp	r2, #0
	subne	r2, r2, #1
	strhne	r2, [r3, r6]	@ movhi
	bne	.L1106
	mov	r1, r4
	ldr	r0, .L1120+4
	bl	sftl_printk
	ldr	r3, [r5, #2324]
	ldrh	r3, [r3, r6]
	cmp	r3, #0
	bne	.L1108
	movw	r2, #2831
	ldr	r1, .L1120+8
	ldr	r0, .L1120+12
	bl	sftl_printk
.L1108:
	ldr	r3, [r5, #2324]
	mov	r2, #16
	mov	r1, r4
	ldr	r0, .L1120+16
	strh	r2, [r3, r6]	@ movhi
	bl	test_node_in_list
	cmp	r0, #0
	beq	.L1109
	mov	r1, r4
	ldr	r0, .L1120+16
	bl	List_remove_node
	movw	r3, #2344
	ldrh	r3, [r5, r3]
	cmp	r3, #0
	bne	.L1110
	movw	r2, #2835
	ldr	r1, .L1120+8
	ldr	r0, .L1120+12
	bl	sftl_printk
.L1110:
	movw	r2, #2344
	mov	r0, r4
	ldrh	r3, [r5, r2]
	sub	r3, r3, #1
	strh	r3, [r5, r2]	@ movhi
	bl	INSERT_DATA_LIST
	ldr	r3, [r5, #2324]
	mov	r1, r4
	ldr	r0, .L1120+20
	ldrh	r2, [r3, r6]
	bl	sftl_printk
.L1109:
	mov	r0, r4
	bl	FtlGcRefreshBlock
.L1113:
	mov	r6, #0
	b	.L1105
.L1106:
	ldr	r7, .L1120+24
	movw	r3, #65535
	ldrh	r0, [r7]
	cmp	r0, r3
	strheq	r4, [r7]	@ movhi
	beq	.L1113
	cmp	r4, r0
	beq	.L1113
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
	beq	.L1105
	movw	r2, #2856
	ldr	r1, .L1120+8
	ldr	r0, .L1120+12
	bl	sftl_printk
.L1105:
	mov	r0, r6
	ldmfd	sp, {r4, r5, r6, r7, fp, sp, pc}
.L1121:
	.align	2
.L1120:
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
	ldr	r6, .L1140
	ldr	r4, .L1140+4
	ldrh	r2, [r6, #4]
	cmp	r2, #0
	beq	.L1123
	ldrb	r3, [r4, #2356]	@ zero_extendqisi2
	cmp	r3, #0
	bne	.L1123
	ldrb	r1, [r4, #2355]	@ zero_extendqisi2
	ldrh	r3, [r4, #102]
	mul	r3, r3, r1
	cmp	r2, r3
	beq	.L1123
	ldrb	r8, [r4, #2358]	@ zero_extendqisi2
	cmp	r8, #0
	bne	.L1122
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
	beq	.L1125
	mov	r2, r8
	mov	r1, #1
	sub	r0, fp, #60
	bl	FlashReadPages
.L1126:
	ldr	ip, .L1140
	mov	r8, #0
	ldr	r3, .L1140+8
	lsl	r9, r9, #2
	mov	r10, r8
	strh	r3, [r5]	@ movhi
.L1127:
	cmp	r9, r8
	bne	.L1131
.L1128:
	mov	r3, #1
.L1139:
	strb	r3, [r4, #2358]
.L1122:
	sub	sp, fp, #40
	ldmfd	sp, {r4, r5, r6, r7, r8, r9, r10, fp, sp, pc}
.L1125:
	ldrh	r2, [r4, #110]
	mov	r1, #255
	bl	ftl_memset
	b	.L1126
.L1131:
	ldrh	r3, [r6, #4]
	cmp	r3, #0
	beq	.L1128
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
	b	.L1127
.L1123:
	mov	r3, #0
	b	.L1139
.L1141:
	.align	2
.L1140:
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
	ldr	r5, .L1146
	mov	r4, #0
	mov	r6, #12
	bl	FtlWriteDump_data
.L1143:
	ldrh	r2, [r5, #138]
	uxth	r3, r4
	cmp	r2, r3
	bhi	.L1145
	mov	r0, #0
	ldmfd	sp, {r4, r5, r6, fp, sp, pc}
.L1145:
	ldr	r3, [r5, #2492]
	uxth	r0, r4
	mla	r3, r6, r0, r3
	ldr	r3, [r3, #4]
	cmp	r3, #0
	bge	.L1144
	bl	flush_l2p_region
.L1144:
	add	r4, r4, #1
	b	.L1143
.L1147:
	.align	2
.L1146:
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
	beq	.L1269
	ldrh	r3, [r0, #2]
	ldr	r6, .L1276
	str	r3, [fp, #-64]
	ldrb	r3, [r0, #6]	@ zero_extendqisi2
	ldr	r1, [fp, #-64]
	str	r3, [fp, #-80]
	ldrh	r3, [r6, #102]
	cmp	r3, r1
	mov	r3, #0
	strheq	r3, [r0, #4]	@ movhi
	ldrhne	r0, [r0, #16]
	bne	.L1152
.L1274:
	strb	r3, [r4, #6]
.L1269:
	mov	r0, #0
	sub	sp, fp, #40
	ldmfd	sp, {r4, r5, r6, r7, r8, r9, r10, fp, sp, pc}
.L1153:
	uxth	r1, r3
	add	r1, r4, r1, lsl #1
	ldrh	r0, [r1, #16]
.L1152:
	cmp	r0, r2
	uxth	r7, r3
	add	r3, r3, #1
	beq	.L1153
	mov	r1, #1
	bl	FtlGetLastWrittenPage
	cmn	r0, #1
	mov	r5, r0
	beq	.L1154
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
.L1155:
	ldr	r1, [fp, #-56]
	uxth	r3, r2
	cmp	r1, r3
	bhi	.L1159
	ldr	r7, .L1276
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
.L1160:
	uxth	r2, r3
	cmp	r6, r2
	bhi	.L1165
	addeq	r3, r5, #1
	uxtheq	r3, r3
	streq	r3, [fp, #-60]
	bne	.L1163
.L1270:
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
	beq	.L1275
	ldr	r2, [fp, #-72]
	movw	r3, #65535
	cmp	r2, r3
	bne	.L1169
	ldrb	r3, [r4, #8]	@ zero_extendqisi2
	cmp	r3, #0
	bne	.L1170
.L1169:
	ldr	r3, [r7, #4012]
	uxth	r8, r5
	uxth	r5, r5
	cmn	r3, #1
	ldreq	r3, [fp, #-56]
	streq	r3, [r7, #4012]
	ldr	r3, [fp, #-64]
	ldr	r7, .L1276
	add	r3, r3, #7
	cmp	r5, r3
	subgt	r5, r8, #7
	ldrle	r5, [fp, #-64]
	uxthgt	r5, r5
.L1173:
	cmp	r5, r8
	bhi	.L1180
	mov	r3, #0
	ldrh	r9, [r7, #32]
	ldr	r0, [r7, #3836]
	mov	r6, r3
	ldr	r1, [fp, #-76]
	movw	lr, #65535
	mov	ip, #20
	b	.L1181
.L1154:
	ldr	r3, [fp, #-64]
	cmp	r3, #0
	beq	.L1156
	movw	r2, #1804
	ldr	r1, .L1276+4
	ldr	r0, .L1276+8
	bl	sftl_printk
.L1156:
	ldr	r3, [fp, #-80]
	cmp	r3, #0
	cmpne	r7, r3
	beq	.L1157
	movw	r2, #1805
	ldr	r1, .L1276+4
	ldr	r0, .L1276+8
	bl	sftl_printk
.L1157:
	mov	r3, #0
	strh	r3, [r4, #2]	@ movhi
	b	.L1274
.L1159:
	ldrh	r3, [ip], #2
	cmp	r3, r8
	beq	.L1158
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
.L1158:
	add	r2, r2, #1
	b	.L1155
.L1165:
	ldr	r2, [r8]
	cmp	r2, #0
	bne	.L1161
	ldr	ip, [r8, #12]
	ldr	r2, [ip, #4]
	cmn	r2, #1
	beq	.L1162
	ldr	r1, [r10, #2544]
	mov	r0, r2
	str	ip, [fp, #-68]
	bl	ftl_cmp_data_ver
	ldr	ip, [fp, #-68]
	cmp	r0, #0
	addne	r2, r2, #1
	strne	r2, [r10, #2544]
.L1162:
	ldr	r2, [ip]
	cmn	r2, #1
	bne	.L1164
.L1163:
	uxth	r2, r5
	uxth	r3, r3
	str	r2, [fp, #-60]
	mov	r2, #20
	mla	r9, r2, r3, r9
	b	.L1270
.L1161:
	ldr	r2, [fp, #-60]
	str	r2, [fp, #-72]
.L1164:
	add	r3, r3, #1
	add	r8, r8, #20
	b	.L1160
.L1175:
	ldrh	r2, [r1], #2
	add	r3, r3, #1
	cmp	r2, lr
	mlane	r10, ip, r6, r0
	addne	r6, r6, #1
	orrne	r2, r5, r2, lsl #10
	uxthne	r6, r6
	strne	r2, [r10, #4]
.L1181:
	uxth	r2, r3
	cmp	r9, r2
	bhi	.L1175
	mov	r1, r6
	mov	r2, #0
	bl	FlashReadPages
	ldr	r3, [r7, #3836]
	mov	r2, #20
	movw	r1, #65535
	mla	r6, r2, r6, r3
.L1176:
	cmp	r6, r3
	addeq	r5, r5, #1
	uxtheq	r5, r5
	beq	.L1173
.L1179:
	ldr	r2, [r3]
	cmp	r2, #0
	bne	.L1170
	ldr	r2, [r3, #12]
	ldrh	r0, [r2]
	cmp	r0, r1
	beq	.L1178
	ldr	r2, [r2, #4]
	cmn	r2, #1
	strne	r2, [r7, #4012]
.L1178:
	add	r3, r3, #20
	b	.L1176
.L1180:
	mvn	r3, #0
	str	r3, [r7, #4012]
.L1170:
	ldr	r5, .L1276+12
	mov	r3, #1
	mov	r0, r5
	sub	r5, r5, #4016
	strh	r3, [r0], #-108	@ movhi
	bl	FtlMapBlkWriteDump_data
	ldr	r9, [fp, #-64]
.L1182:
	ldrh	ip, [r5, #32]
	mov	r3, #0
	ldr	r0, [r5, #3836]
	mov	r10, r3
	ldr	r1, [fp, #-76]
	movw	lr, #65535
	mov	r6, #20
.L1183:
	uxth	r2, r3
	cmp	ip, r2
	bhi	.L1185
	mov	r2, #0
	mov	r1, r10
	bl	FlashReadPages
	mov	r3, #0
.L1273:
	str	r3, [fp, #-72]
	ldrh	r3, [fp, #-72]
	cmp	r10, r3
	bhi	.L1213
	add	r9, r9, #1
	ldrh	r3, [r5, #102]
	uxth	r9, r9
	cmp	r3, r9
	bne	.L1182
	ldrh	r2, [r5, #32]
	movw	r0, #65535
	mov	r3, #0
	strh	r9, [r4, #2]	@ movhi
	strh	r3, [r4, #4]	@ movhi
.L1214:
	uxth	r1, r3
	cmp	r1, r2
	bcs	.L1269
	ldr	r1, [fp, #-76]
	ldrh	ip, [r1], #2
	cmp	ip, r0
	str	r1, [fp, #-76]
	add	r1, r3, #1
	bne	.L1274
	mov	r3, r1
	b	.L1214
.L1185:
	ldrh	r2, [r1], #2
	add	r3, r3, #1
	cmp	r2, lr
	mlane	r7, r6, r10, r0
	orrne	r2, r9, r2, lsl #10
	addne	r10, r10, #1
	uxthne	r10, r10
	strne	r2, [r7, #4]
	b	.L1183
.L1213:
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
	bcc	.L1187
	ldr	r2, [fp, #-80]
	moveq	r3, #1
	movne	r3, #0
	cmp	r2, r0
	movls	r3, #0
	andhi	r3, r3, #1
	cmp	r3, #0
	bne	.L1187
	ldr	r3, [fp, #-60]
	ldr	r2, [fp, #-68]
	cmp	r9, r3
	cmpeq	r2, r0
	beq	.L1188
	ldr	r2, [fp, #-84]
	ldr	r3, [r2, r7]
	cmn	r3, #1
	beq	.L1189
	ldr	r8, [r8, #12]
	movw	r3, #61589
	ldrh	r2, [r8]
	cmp	r2, r3
	beq	.L1190
.L1197:
	ldrh	r0, [r4]
.L1272:
	bl	decrement_vpc_count
.L1187:
	ldr	r3, [fp, #-72]
	add	r3, r3, #1
	b	.L1273
.L1190:
	ldr	r3, [r8, #4]
	cmn	r3, #1
	str	r3, [fp, #-56]
	beq	.L1191
	mov	r0, r3
	ldr	r1, [r5, #2544]
	bl	ftl_cmp_data_ver
	cmp	r0, #0
	ldrne	r3, [fp, #-56]
	addne	r3, r3, #1
	strne	r3, [r5, #2544]
.L1191:
	ldrh	r2, [r8]
	movw	r3, #61589
	cmp	r2, r3
	beq	.L1192
	movw	r2, #1954
	ldr	r1, .L1276+4
	ldr	r0, .L1276+8
	bl	sftl_printk
.L1192:
	ldr	r6, [r8, #8]
	sub	r1, fp, #48
	ldr	r3, [r8, #12]
	mov	r2, #0
	mov	r0, r6
	str	r3, [fp, #-52]
	bl	log2phys
	ldr	r1, [r5, #4012]
	cmn	r1, #1
	beq	.L1193
	ldr	r0, [fp, #-56]
	bl	ftl_cmp_data_ver
	cmp	r0, #0
	beq	.L1193
	ldr	r2, [fp, #-52]
	cmn	r2, #1
	beq	.L1194
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
	bne	.L1195
.L1196:
	mvn	r3, #0
	ldrh	r0, [r4]
	str	r3, [fp, #-52]
	bl	decrement_vpc_count
.L1204:
	ldr	r7, [fp, #-52]
	cmn	r7, #1
	beq	.L1187
.L1217:
	ubfx	r0, r7, #10, #16
	bl	P2V_block_in_plane
	ldrh	r3, [r5, #40]
	mov	r6, r0
	cmp	r3, r0
	bhi	.L1209
	movw	r2, #2055
	ldr	r1, .L1276+4
	ldr	r0, .L1276+8
	bl	sftl_printk
.L1209:
	ldr	r2, [r5, #2324]
	lsl	r3, r6, #1
	ldrh	r3, [r2, r3]
	cmp	r3, #0
	beq	.L1210
	mov	r0, r6
	b	.L1272
.L1194:
	ldr	r3, [fp, #-44]
	ldr	r2, [fp, #-48]
	cmp	r2, r3
	bne	.L1197
	mov	r2, #1
	sub	r1, fp, #52
	mov	r0, r6
	bl	log2phys
	b	.L1197
.L1195:
	ldr	r1, [r8, #8]
	cmp	r6, r1
	bne	.L1196
	ldr	r1, [r8, #4]
	ldr	r0, [r5, #4012]
	str	r1, [fp, #-84]
	bl	ftl_cmp_data_ver
	cmp	r0, #0
	beq	.L1196
	ldr	r1, [fp, #-48]
	ldr	r0, [fp, #-44]
	cmp	r1, r0
	bne	.L1199
.L1202:
	ldr	r1, [fp, #-52]
	mov	r0, r6
	bl	FtlReUsePrevPpa
	b	.L1196
.L1199:
	ldr	r0, [fp, #-52]
	cmp	r1, r0
	beq	.L1196
	cmn	r1, #1
	streq	r1, [r2, r7]
	beq	.L1201
	str	r1, [r3, #4]
	mov	r2, #0
	mov	r1, #1
	mov	r0, r3
	ldr	r8, [r3, #12]
	bl	FlashReadPages
.L1201:
	ldr	r2, [r5, #3836]
	ldr	r2, [r2, r7]
	cmn	r2, #1
	beq	.L1202
	ldr	r3, [r8, #4]
	ldr	r0, [r5, #4012]
	mov	r1, r3
	bl	ftl_cmp_data_ver
	cmp	r0, #0
	beq	.L1202
	mov	r1, r3
	ldr	r0, [fp, #-84]
	bl	ftl_cmp_data_ver
	cmp	r0, #0
	beq	.L1196
	b	.L1202
.L1193:
	ldr	r3, [fp, #-44]
	ldr	r2, [fp, #-48]
	cmp	r2, r3
	beq	.L1204
	mov	r2, #1
	sub	r1, fp, #44
	mov	r0, r6
	bl	log2phys
	ldr	r7, [fp, #-48]
	cmn	r7, #1
	beq	.L1204
	ldr	r3, [fp, #-52]
	cmp	r7, r3
	beq	.L1217
	ubfx	r0, r7, #10, #16
	bl	P2V_block_in_plane
	ldr	r3, .L1276+16
	ldrh	r2, [r3]
	cmp	r2, r0
	beq	.L1208
	ldrh	r2, [r3, #48]
	cmp	r2, r0
	beq	.L1208
	ldrh	r3, [r3, #96]
	cmp	r3, r0
	bne	.L1204
.L1208:
	ldr	r0, [r5, #3836]
	mov	r2, #0
	mov	r1, #1
	str	r7, [r0, #4]
	ldr	r8, [r0, #12]
	bl	FlashReadPages
	ldr	r3, [r5, #3836]
	ldr	r3, [r3]
	cmn	r3, #1
	beq	.L1204
	ldr	r1, [r8, #4]
	ldr	r0, [fp, #-56]
	bl	ftl_cmp_data_ver
	cmp	r0, #0
	bne	.L1204
	mov	r2, #1
	sub	r1, fp, #48
	mov	r0, r6
	bl	log2phys
	b	.L1204
.L1210:
	mov	r1, r6
	ldr	r0, .L1276+20
	bl	sftl_printk
	b	.L1187
.L1189:
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
	beq	.L1271
	ldr	r2, [fp, #-56]
	cmp	r2, r3
	bcs	.L1187
	mov	r3, r2
.L1271:
	str	r3, [r5, #4012]
	b	.L1187
.L1188:
	ldrb	r3, [fp, #-68]	@ zero_extendqisi2
	ldr	r2, [fp, #-68]
	ldr	r1, [fp, #-60]
	strb	r3, [r4, #6]
	ldrh	r3, [fp, #-60]
	strh	r3, [r4, #2]	@ movhi
.L1275:
	mov	r0, r4
	bl	ftl_sb_update_avl_pages
	b	.L1269
.L1277:
	.align	2
.L1276:
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
	ldr	r3, .L1289
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
	ldr	r1, .L1289+4
	ldr	r2, [r3, #3284]
	str	r1, [r2, #4]
.L1279:
	subs	r6, r6, #1
	beq	.L1281
	ldrh	r3, [r4, #4]
	cmp	r3, #0
	bne	.L1280
.L1281:
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
.L1280:
	mov	r0, r4
	bl	get_new_active_ppa
	cmn	r0, #1
	str	r0, [fp, #-48]
	beq	.L1281
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
	b	.L1279
.L1290:
	.align	2
.L1289:
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
	ldr	r4, .L1323
	ldr	r3, [r4, #3284]
	add	r7, r4, #3264
	ldr	r6, [r4, #3288]
	str	r3, [r4, #3272]
	str	r6, [r4, #3276]
	bl	FtlBbtMemInit
	ldrh	r5, [r4, #98]
	sub	r5, r5, #1
	uxth	r5, r5
.L1292:
	ldrh	r3, [r4, #98]
	sub	r3, r3, #16
	cmp	r5, r3
	ble	.L1295
	lsl	r3, r5, #10
	mov	r2, #1
	mov	r1, r2
	mov	r0, r7
	str	r3, [r4, #3268]
	bl	FlashReadPages
	ldr	r3, [r4, #3264]
	cmn	r3, #1
	bne	.L1293
	ldr	r3, [r4, #3268]
	mov	r2, #1
	mov	r1, r2
	mov	r0, r7
	add	r3, r3, #1
	str	r3, [r4, #3268]
	bl	FlashReadPages
.L1293:
	ldr	r3, [r4, #3264]
	cmn	r3, #1
	beq	.L1294
	ldrh	r2, [r6]
	movw	r3, #61649
	cmp	r2, r3
	bne	.L1294
	ldr	r3, [r6, #4]
	strh	r5, [r4, #152]	@ movhi
	str	r3, [r4, #160]
	ldrh	r3, [r6, #8]
	strh	r3, [r4, #156]	@ movhi
.L1295:
	ldrh	r3, [r4, #152]
	movw	r2, #65535
	cmp	r3, r2
	beq	.L1309
	ldrh	r3, [r4, #156]
	cmp	r3, r2
	beq	.L1299
	lsl	r3, r3, #10
	mov	r2, #1
	mov	r1, r2
	ldr	r0, .L1323+4
	str	r3, [r4, #3268]
	bl	FlashReadPages
	ldr	r3, [r4, #3264]
	cmn	r3, #1
	beq	.L1299
	ldrh	r2, [r6]
	movw	r3, #61649
	cmp	r2, r3
	bne	.L1299
	ldr	r3, [r6, #4]
	ldr	r2, [r4, #160]
	cmp	r3, r2
	ldrhhi	r2, [r4, #156]
	strhi	r3, [r4, #160]
	ldrhhi	r3, [r6, #8]
	strhhi	r2, [r4, #152]	@ movhi
	strhhi	r3, [r4, #156]	@ movhi
.L1299:
	ldr	r7, .L1323+4
	mov	r1, #1
	ldrh	r0, [r4, #152]
	bl	FtlGetLastWrittenPage
	sxth	r5, r0
	add	r0, r0, #1
	strh	r0, [r4, #154]	@ movhi
.L1301:
	cmp	r5, #0
	bge	.L1304
	mov	r2, #254
	ldr	r1, .L1323+8
	ldr	r0, .L1323+12
	bl	sftl_printk
.L1303:
	ldrh	r3, [r6, #10]
	ldrh	r0, [r6, #12]
	strh	r3, [r4, #158]	@ movhi
	movw	r3, #65535
	cmp	r0, r3
	beq	.L1306
	ldr	r2, [r4, #28]
	cmp	r0, r2
	beq	.L1306
	ldrh	r3, [r4, #42]
	lsr	r3, r3, #2
	cmp	r0, r3
	cmpcc	r2, r3
	bcs	.L1306
	bl	FtlSysBlkNumInit
.L1306:
	ldr	r6, .L1323+16
	mov	r5, #0
	add	r7, r6, #3728
.L1307:
	ldrh	r3, [r4, #54]
	cmp	r5, r3
	bcc	.L1308
	mov	r0, #0
	ldmfd	sp, {r4, r5, r6, r7, fp, sp, pc}
.L1294:
	sub	r5, r5, #1
	uxth	r5, r5
	b	.L1292
.L1304:
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
	beq	.L1302
	ldrh	r2, [r6]
	movw	r3, #61649
	cmp	r2, r3
	beq	.L1303
.L1302:
	sub	r5, r5, #1
	sxth	r5, r5
	b	.L1301
.L1308:
	ldrh	r2, [r7]
	ldr	r1, [r4, #3272]
	ldr	r0, [r6, #4]!
	lsl	r2, r2, #2
	mla	r1, r5, r2, r1
	add	r5, r5, #1
	bl	ftl_memcpy
	b	.L1307
.L1309:
	mvn	r0, #0
	ldmfd	sp, {r4, r5, r6, r7, fp, sp, pc}
.L1324:
	.align	2
.L1323:
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
	ldr	r8, .L1344
	bl	FtlBbtMemInit
	bl	FtlLoadFactoryBbt
	sub	r5, r8, #180
	sub	r9, r8, #18
	mov	r4, r5
.L1326:
	ldrh	r3, [r5, #54]
	cmp	r7, r3
	bcc	.L1332
	mov	r0, #0
.L1333:
	ldrh	r2, [r4, #114]
	uxth	r3, r0
	add	r5, r0, #1
	cmp	r2, r3
	bhi	.L1334
	ldrh	r5, [r4, #164]
	movw	r6, #65535
	sub	r5, r5, #1
	uxth	r5, r5
.L1335:
	ldrh	r3, [r4, #164]
	sub	r3, r3, #48
	cmp	r5, r3
	ble	.L1339
	mov	r0, r5
	bl	FtlBbmIsBadBlock
	cmp	r0, #1
	beq	.L1336
	mov	r0, r5
	bl	FlashTestBlk
	cmp	r0, #0
	beq	.L1337
	mov	r0, r5
	bl	FtlBbmMapBadBlock
.L1336:
	sub	r5, r5, #1
	uxth	r5, r5
	b	.L1335
.L1332:
	ldrh	r3, [r9, #2]!
	movw	r2, #65535
	ldr	r0, [r5, #3284]
	ldr	r10, [r5, #3288]
	cmp	r3, r2
	str	r0, [r5, #3272]
	str	r10, [r5, #3276]
	beq	.L1327
	ldrh	ip, [r5, #98]
	mov	r2, #1
	mov	r1, r2
	ldr	r0, .L1344+4
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
.L1328:
	uxth	r0, ip
	add	r7, r7, #1
	add	r8, r8, #4
	bl	FtlBbmMapBadBlock
	b	.L1326
.L1327:
	mov	r1, r7
	bl	FlashGetBadBlockList
	ldr	r1, [r8]
	ldr	r0, [r5, #3272]
	bl	FtlBbt2Bitmap
	ldrh	r6, [r5, #98]
.L1330:
	sub	r6, r6, #1
	uxth	r6, r6
.L1329:
	ldrh	r0, [r4, #98]
	smlabb	r0, r0, r7, r6
	uxth	r0, r0
	bl	FtlBbmIsBadBlock
	cmp	r0, #1
	beq	.L1330
	mov	r1, #16
	ldr	r0, [r4, #3288]
	strh	r6, [r9]	@ movhi
	bl	__memzero
	mov	r1, #4096
	ldr	r0, [r4, #3284]
	bl	__memzero
	ldr	r3, .L1344+8
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
	ldr	r3, .L1344+12
	ldrh	r2, [r3]
	lsl	r2, r2, #2
	bl	ftl_memcpy
	mov	r2, #1
	ldr	r0, .L1344+4
	mov	r1, r2
	bl	FlashEraseBlocks
	mov	r3, #1
	ldr	r0, .L1344+4
	mov	r2, r3
	mov	r1, r3
	bl	FlashProgPages
	ldr	r3, [r4, #3264]
	ldr	ip, [fp, #-44]
	cmn	r3, #1
	bne	.L1328
	uxth	r0, ip
	bl	FtlBbmMapBadBlock
	b	.L1329
.L1334:
	uxth	r0, r0
	bl	FtlBbmMapBadBlock
	mov	r0, r5
	b	.L1333
.L1337:
	ldrh	r3, [r4, #152]
	cmp	r3, r6
	strheq	r5, [r4, #152]	@ movhi
	beq	.L1336
.L1338:
	strh	r5, [r4, #156]	@ movhi
.L1339:
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
.L1345:
	.align	2
.L1344:
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
	ldr	r4, .L1355
	mov	r5, r1
	str	r2, [fp, #-128]
	add	r2, r0, r1
	ldrh	r3, [r4, #96]
	cmp	r2, r3
	mvnhi	r8, #0
	bhi	.L1346
	ldrh	r6, [r4, #108]
	mov	r8, #0
	lsr	r6, r0, r6
	lsl	r10, r6, #2
.L1348:
	cmp	r5, #0
	bne	.L1353
.L1346:
	mov	r0, r8
	sub	sp, fp, #40
	ldmfd	sp, {r4, r5, r6, r7, r8, r9, r10, fp, sp, pc}
.L1353:
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
	beq	.L1350
	ldr	r2, [r4, #3852]
	sub	r0, fp, #124
	str	ip, [fp, #-120]
	str	r2, [fp, #-116]
	sub	r2, fp, #104
	str	r2, [fp, #-112]
	mov	r2, #1
	mov	r1, r2
	bl	FlashReadPages
.L1351:
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
	ldr	r0, .L1355+4
	add	r6, r6, #1
	bl	FtlMapWritePage
	ldr	r3, [fp, #-128]
	cmn	r0, #1
	ldr	ip, [fp, #-136]
	mvneq	r8, #0
	add	r3, r3, ip
	str	r3, [fp, #-128]
	b	.L1348
.L1350:
	ldrh	r2, [r4, #110]
	ldr	r0, [r4, #3852]
	bl	ftl_memset
	b	.L1351
.L1356:
	.align	2
.L1355:
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
	ldr	r3, .L1359
	ldr	r2, .L1359+4
	ldr	r1, [r3, #2636]
	cmp	r1, r2
	ldmfdne	sp, {fp, sp, pc}
	ldr	r2, .L1359+8
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
	ldr	r2, .L1359+12
	bl	FtlVendorPartWrite
	ldmfd	sp, {fp, sp, pc}
.L1360:
	.align	2
.L1359:
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
	ldr	r2, .L1366
	ldrh	r3, [r2]
	cmp	r3, #31
	addls	r3, r3, #1
	movhi	r3, #32
	strhls	r3, [r2]	@ movhi
	movls	r3, #1
	cmp	r0, #0
	ldr	r2, .L1366+4
	bne	.L1363
	ldr	r1, [r2, #3864]
	ldr	r0, [r1, #20]
	ldr	r1, [r1, #16]
	add	r3, r3, r0
	cmp	r1, r3
	bcc	.L1364
.L1363:
	ldr	r3, [r2, #3864]
	mov	r0, #64
	ldr	r1, [r3, #16]
	str	r1, [r3, #20]
	ldr	r1, .L1366+8
	str	r1, [r3]
	ldr	r3, .L1366+12
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
.L1364:
	mov	r0, #0
	ldmfd	sp, {fp, sp, pc}
.L1367:
	.align	2
.L1366:
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
	ldr	r3, .L1378
	mov	r6, r1
	add	r1, r0, r1
	mov	r7, r0
	ldrh	r2, [r3, #96]
	mov	r10, r3
	cmp	r1, r2
	mvnhi	r8, #0
	bhi	.L1368
	ldrh	r5, [r3, #108]
	mov	r8, #0
	lsr	r5, r0, r5
	lsl	r3, r5, #2
	str	r3, [fp, #-128]
.L1370:
	cmp	r6, #0
	bne	.L1376
.L1368:
	mov	r0, r8
	sub	sp, fp, #40
	ldmfd	sp, {r4, r5, r6, r7, r8, r9, r10, fp, sp, pc}
.L1376:
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
	beq	.L1372
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
	bne	.L1374
	mov	r2, ip
	mov	r1, r5
	ldr	r0, .L1378+4
	bl	sftl_printk
	ldr	r2, [r10, #3852]
	mov	r1, r5
	ldr	r0, .L1378+8
	bl	FtlMapWritePage
.L1374:
	ldr	r1, [r10, #3852]
	lsl	r2, r4, #9
	ldr	r3, [fp, #-132]
	mov	r0, r9
	add	r1, r1, r3, lsl #9
	bl	ftl_memcpy
.L1375:
	ldr	r3, [fp, #-128]
	add	r5, r5, #1
	sub	r6, r6, r4
	add	r7, r7, r4
	add	r9, r9, r4, lsl #9
	add	r3, r3, #4
	str	r3, [fp, #-128]
	b	.L1370
.L1372:
	lsl	r2, r4, #9
	mov	r1, ip
	mov	r0, r9
	bl	ftl_memset
	b	.L1375
.L1379:
	.align	2
.L1378:
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
	ldr	r4, .L1382
	mov	r0, #64
	add	r5, r4, #3856
	ldr	r2, [r4, #3864]
	ldrh	r1, [r5]
	bl	FtlVendorPartRead
	ldr	r3, [r4, #3864]
	ldr	r2, [r3]
	ldr	r3, .L1382+4
	cmp	r2, r3
	beq	.L1381
	ldr	r1, .L1382+8
	ldr	r0, .L1382+12
	bl	sftl_printk
	ldrh	r2, [r5]
	mov	r1, #0
	ldr	r0, [r4, #3864]
	lsl	r2, r2, #9
	bl	ftl_memset
.L1381:
	mov	r0, #0
	ldmfd	sp, {r4, r5, fp, sp, pc}
.L1383:
	.align	2
.L1382:
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
	ldr	r4, .L1387
	mov	r1, #1
	mov	r0, #0
	ldr	r2, .L1387+4
	bl	FtlVendorPartRead
	ldr	r5, .L1387+8
	ldr	r3, [r4, #2636]
	cmp	r3, r5
	beq	.L1385
	mov	r1, #512
	ldr	r0, .L1387+4
	bl	__memzero
	str	r5, [r4, #2636]
.L1385:
	ldr	r3, [r4, #2636]
	cmp	r3, r5
	bne	.L1386
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
.L1386:
	ldrh	r2, [r4, #92]
	ldr	r3, [r4, #2552]
	ldr	r0, [r4, #2548]
	ldrh	r1, [r4, #40]
	mla	r0, r0, r2, r3
	bl	__udivsi3
	str	r0, [r4, #2556]
	ldmfd	sp, {r4, r5, fp, sp, pc}
.L1388:
	.align	2
.L1387:
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
	ldr	r4, .L1398
	mov	r6, #0
	mov	r1, #255
	ldr	r10, .L1398+4
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
	ldr	r3, .L1398+8
	strh	r3, [r8]	@ movhi
	ldr	r3, [r4, #2584]
	str	r6, [r8, #8]
	ldrh	r2, [r5, #-226]
	str	r3, [r8, #4]
	ldr	r3, .L1398+12
	str	r3, [r4, #2268]
	ldr	r3, .L1398+16
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
.L1390:
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
	blt	.L1391
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
.L1391:
	ldrh	r3, [r5, #2]
	ldr	r2, [r4, #3264]
	add	r3, r3, #1
	uxth	r3, r3
	cmn	r2, #1
	strh	r3, [r5, #2]	@ movhi
	bne	.L1392
	cmp	r3, #1
	bne	.L1393
	movw	r2, #1169
	mov	r1, r10
	ldr	r0, .L1398+20
	bl	sftl_printk
.L1393:
	ldrh	r3, [r5, #2]
	add	r6, r6, #1
	uxth	r6, r6
	cmp	r3, #1
	ldrheq	r3, [r4, #104]
	subeq	r3, r3, #1
	strheq	r3, [r5, #2]	@ movhi
	cmp	r6, #3
	bls	.L1390
	mov	r2, r6
	ldr	r1, [r4, #3268]
	ldr	r0, .L1398+24
	bl	sftl_printk
.L1396:
	b	.L1396
.L1392:
	cmp	r2, #256
	cmpne	r3, #1
	beq	.L1390
	movw	r3, #65535
	cmp	r7, r3
	beq	.L1397
	mov	r1, #1
	mov	r0, r7
	bl	FtlFreeSysBlkQueueIn
.L1397:
	mov	r0, #0
	ldmfd	sp, {r4, r5, r6, r7, r8, r9, r10, fp, sp, pc}
.L1399:
	.align	2
.L1398:
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
	ldr	r3, .L1403
	ldr	r3, [r3]
	cmp	r3, #1
	bne	.L1402
	bl	FtlSysFlush
.L1402:
	mov	r0, #0
	ldmfd	sp, {fp, sp, pc}
.L1404:
	.align	2
.L1403:
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
	ldr	r6, .L1420
	mov	r8, r0
	mov	r5, r1
	ldr	r3, [r6, #140]
	cmp	r2, r3
	mvnhi	r0, #0
	bhi	.L1405
	cmp	r1, #31
	bhi	.L1407
.L1412:
	mov	r0, #0
.L1405:
	sub	sp, fp, #32
	ldmfd	sp, {r4, r5, r6, r7, r8, fp, sp, pc}
.L1407:
	ldrh	r4, [r6, #58]
	mov	r1, r4
	bl	__udivsi3
	smulbb	r3, r0, r4
	mov	r7, r0
	sub	r8, r8, r3
	uxth	r8, r8
	cmp	r8, #0
	beq	.L1408
	sub	r4, r4, r8
	add	r7, r0, #1
	cmp	r4, r5
	movcs	r4, r5
	uxth	r4, r4
	sub	r5, r5, r4
.L1408:
	ldr	r4, .L1420+4
	mvn	r3, #0
	str	r3, [fp, #-36]
.L1409:
	ldrh	r3, [r6, #58]
	cmp	r5, r3
	bcs	.L1411
	ldr	r3, .L1420+4
	ldr	r2, [r3, #-4028]
	cmp	r2, #32
	bls	.L1412
	mov	r4, #0
	str	r4, [r3, #-4028]
	bl	l2p_flush
	bl	FtlVpcTblFlush
	b	.L1412
.L1411:
	mov	r2, #0
	sub	r1, fp, #40
	mov	r0, r7
	bl	log2phys
	ldr	r3, [fp, #-40]
	cmn	r3, #1
	beq	.L1410
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
.L1410:
	ldrh	r3, [r6, #58]
	add	r7, r7, #1
	sub	r5, r5, r3
	b	.L1409
.L1421:
	.align	2
.L1420:
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
	ldr	r4, .L1434
	ldr	r1, .L1434+4
	ldr	r0, .L1434+8
	bl	sftl_printk
	ldrh	r2, [r4, #42]
	mov	r1, #0
	ldr	r0, [r4, #3868]
	lsl	r2, r2, #1
	bl	ftl_memset
.L1423:
	ldr	r3, [r4, #2504]
	cmp	r5, r3
	bcc	.L1425
	ldr	r9, .L1434+12
	mov	r6, #0
	ldr	r10, .L1434+16
.L1426:
	ldrh	r3, [r4, #40]
	uxth	r5, r6
	cmp	r3, r5
	bhi	.L1430
	ldmib	sp, {r4, r5, r6, r7, r8, r9, r10, fp, sp, pc}
.L1425:
	mov	r2, #0
	sub	r1, fp, #44
	mov	r0, r5
	bl	log2phys
	ldr	r0, [fp, #-44]
	cmn	r0, #1
	beq	.L1424
	ubfx	r0, r0, #10, #16
	bl	P2V_block_in_plane
	ldr	r2, [r4, #3868]
	lsl	r0, r0, #1
	ldrh	r3, [r2, r0]
	add	r3, r3, #1
	strh	r3, [r2, r0]	@ movhi
.L1424:
	add	r5, r5, #1
	b	.L1423
.L1430:
	uxth	r8, r6
	ldr	r3, [r4, #2324]
	movw	r1, #65535
	lsl	r7, r8, #1
	ldrh	r2, [r3, r7]
	ldr	r3, [r4, #3868]
	ldrh	r3, [r3, r7]
	cmp	r2, r1
	cmpne	r2, r3
	beq	.L1428
	mov	r1, r8
	mov	r0, r9
	bl	sftl_printk
	ldrh	r3, [r10]
	cmp	r3, r5
	beq	.L1428
	ldr	r3, .L1434+20
	ldrh	r2, [r3]
	cmp	r2, r5
	beq	.L1428
	ldrh	r3, [r3, #-48]
	cmp	r3, r5
	beq	.L1428
	ldr	r3, [r4, #3868]
	mov	r0, r8
	ldrh	r2, [r3, r7]
	ldr	r3, [r4, #2324]
	strh	r2, [r3, r7]	@ movhi
	bl	update_vpc_list
	bl	l2p_flush
	bl	FtlVpcTblFlush
.L1428:
	add	r6, r6, #1
	b	.L1426
.L1435:
	.align	2
.L1434:
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
	ldr	r5, .L1446
	mov	r7, r0
	ldrh	r4, [r0]
	ldrh	r3, [r5, #40]
	cmp	r3, r4
	bcs	.L1437
	movw	r2, #2750
	ldr	r1, .L1446+4
	ldr	r0, .L1446+8
	bl	sftl_printk
.L1437:
	movw	r3, #65535
	cmp	r4, r3
	beq	.L1438
	ldr	r2, [r5, #2324]
	lsl	r3, r4, #1
	mov	r0, r4
	ldrh	r3, [r2, r3]
	cmp	r3, #0
	beq	.L1439
	bl	INSERT_DATA_LIST
.L1438:
	mov	r3, #1
	movw	r2, #65535
	strb	r3, [r7, #8]
	ldr	r3, .L1446+12
	ldrh	r0, [r3]
	mov	r6, r3
	cmp	r0, r2
	beq	.L1440
	cmp	r4, r0
	bne	.L1441
	ldr	r2, [r5, #2324]
	lsl	r3, r0, #1
	ldrh	r3, [r2, r3]
	cmp	r3, #0
	beq	.L1442
.L1441:
	bl	update_vpc_list
.L1442:
	mvn	r3, #0
	strh	r3, [r6]	@ movhi
.L1440:
	mov	r0, r7
	bl	allocate_data_superblock
	bl	l2p_flush
	mov	r0, #0
	bl	FtlEctTblFlush
	bl	FtlVpcTblFlush
	mov	r0, #0
	ldmfd	sp, {r4, r5, r6, r7, fp, sp, pc}
.L1439:
	bl	INSERT_FREE_LIST
	b	.L1438
.L1447:
	.align	2
.L1446:
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
	ldr	r6, .L1462
	mov	r4, r3
	ldr	r10, .L1462+4
	mov	r2, #0
	ldrb	r3, [r3, #9]	@ zero_extendqisi2
	mov	r5, r0
	mov	r9, r1
	bl	FlashProgPages
.L1449:
	cmp	r7, r9
	beq	.L1456
	mov	r8, #0
	b	.L1457
.L1452:
	ldr	r0, [r5, #4]
	ubfx	r0, r0, #10, #16
	bl	P2V_block_in_plane
	ldrh	r3, [r4]
	cmp	r3, r0
	bne	.L1450
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
.L1450:
	ldrh	r3, [r4, #4]
	cmp	r3, #0
	bne	.L1451
	mov	r0, r4
	bl	allocate_new_data_superblock
.L1451:
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
.L1457:
	ldr	r3, [r5]
	cmn	r3, #1
	beq	.L1452
	ldrb	r2, [r4, #6]	@ zero_extendqisi2
	ldrh	r3, [r6, #32]
	cmp	r2, r3
	bcc	.L1453
	movw	r2, #941
	mov	r1, r10
	ldr	r0, .L1462+8
	bl	sftl_printk
.L1453:
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
	beq	.L1454
	ldr	r2, [r6, #2324]
	lsl	r3, r0, #1
	ldrh	r2, [r2, r3]
	cmp	r2, #0
	bne	.L1455
	mov	r1, r0
	ldr	r0, .L1462+12
	bl	sftl_printk
.L1455:
	mov	r0, r8
	bl	decrement_vpc_count
.L1454:
	add	r7, r7, #1
	add	r5, r5, #20
	b	.L1449
.L1456:
	ldrb	r2, [r4, #6]	@ zero_extendqisi2
	ldrh	r3, [r6, #32]
	cmp	r2, r3
	bcc	.L1448
	mov	r2, #956
	ldr	r1, .L1462+4
	ldr	r0, .L1462+8
	bl	sftl_printk
.L1448:
	sub	sp, fp, #40
	ldmfd	sp, {r4, r5, r6, r7, r8, r9, r10, fp, sp, pc}
.L1463:
	.align	2
.L1462:
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
	ldr	r4, .L1490
	movw	r2, #65535
	ldrh	r5, [r4, r3]
	ldrh	r1, [r4, #102]
	cmp	r5, r2
	bne	.L1465
.L1475:
	mov	r3, #0
	str	r3, [r4, #4008]
	movw	r3, #2444
	ldrh	r2, [r4, r3]
	movw	r3, #65535
	cmp	r2, r3
	bne	.L1488
.L1466:
	mov	r0, #0
	ldmib	sp, {r4, r5, r6, r7, r8, r9, r10, fp, sp, pc}
.L1465:
	cmp	r0, #0
	beq	.L1468
	ldr	r3, .L1490+4
	ldrh	r0, [r3, #4]
	cmp	r0, r2
	beq	.L1469
.L1470:
	mov	r1, #2
.L1468:
	ldr	r0, .L1490+8
	bl	FtlGcScanTempBlk
	cmn	r0, #1
	str	r0, [fp, #-44]
	beq	.L1471
	ldr	r2, [r4, #2328]
	lsl	r5, r5, #1
	ldrh	r3, [r2, r5]
	cmp	r3, #4
	bls	.L1472
	sub	r3, r3, #5
	mov	r0, #1
	strh	r3, [r2, r5]	@ movhi
	bl	FtlEctTblFlush
.L1472:
	ldr	r3, [r4, #4008]
	cmp	r3, #0
	bne	.L1473
	ldr	r3, [r4, #2732]
	ldr	r0, [fp, #-44]
	add	r3, r3, #1
	ubfx	r0, r0, #10, #16
	str	r3, [r4, #2732]
	bl	FtlBbmMapBadBlock
	bl	FtlBbmTblFlush
.L1473:
	mov	r3, #0
	str	r3, [r4, #4008]
.L1484:
	mov	r0, #1
	ldmib	sp, {r4, r5, r6, r7, r8, r9, r10, fp, sp, pc}
.L1469:
	mov	r2, #0
	strh	r2, [r3, #4]	@ movhi
	movw	r3, #2344
	ldrh	r3, [r4, r3]
	cmp	r3, #17
	bhi	.L1470
	b	.L1468
.L1471:
	ldr	r3, .L1490+4
	ldrh	r2, [r3, #4]
	movw	r3, #65535
	cmp	r2, r3
	bne	.L1484
	b	.L1475
.L1488:
	ldrb	r1, [r4, #2451]	@ zero_extendqisi2
	ldrh	r3, [r4, #102]
	ldr	r6, .L1490+12
	mul	r3, r3, r1
	ldrh	r2, [r6]
	cmp	r2, r3
	beq	.L1476
	mov	r2, #162
	ldr	r1, .L1490+16
	ldr	r0, .L1490+20
	bl	sftl_printk
.L1476:
	ldrb	r2, [r4, #2451]	@ zero_extendqisi2
	movw	r3, #2444
	ldrh	r0, [r4, #102]
	mov	r5, #0
	ldrh	r3, [r4, r3]
	ldr	r1, [r4, #2324]
	ldr	r10, .L1490+20
	smulbb	r2, r2, r0
	lsl	r3, r3, #1
	strh	r2, [r1, r3]	@ movhi
	ldr	r2, [r4, #2528]
	ldrh	r3, [r6]
	add	r3, r3, r2
	str	r3, [r4, #2528]
.L1477:
	ldrh	r2, [r6]
	uxth	r3, r5
	cmp	r2, r3
	bhi	.L1481
	movw	r0, #65535
	bl	decrement_vpc_count
	movw	r3, #2444
	ldr	r1, [r4, #2324]
	ldrh	r0, [r4, r3]
	mov	r5, r3
	lsl	r2, r0, #1
	ldrh	r2, [r1, r2]
	cmp	r2, #0
	beq	.L1482
	bl	INSERT_DATA_LIST
.L1483:
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
	b	.L1466
.L1481:
	uxth	r3, r5
	mov	r7, #12
	ldr	r8, [r4, #3196]
	mul	r7, r7, r3
	ldr	r2, [r4, #2504]
	add	r9, r8, r7
	ldr	r1, [r9, #8]
	cmp	r1, r2
	bcc	.L1478
	mov	r2, #168
	ldr	r1, .L1490+16
	mov	r0, r10
	bl	sftl_printk
.L1478:
	mov	r2, #0
	sub	r1, fp, #44
	ldr	r0, [r9, #8]
	bl	log2phys
	ldr	r2, [fp, #-44]
	ldr	r0, [r8, r7]
	cmp	r0, r2
	bne	.L1479
	ubfx	r0, r0, #10, #16
	bl	P2V_block_in_plane
	mov	r2, #1
	mov	r7, r0
	add	r1, r9, #4
	ldr	r0, [r9, #8]
	bl	log2phys
	mov	r0, r7
.L1489:
	bl	decrement_vpc_count
.L1480:
	add	r5, r5, #1
	b	.L1477
.L1479:
	ldr	r3, [r9, #4]
	cmp	r2, r3
	beq	.L1480
	ldr	r3, .L1490+8
	ldrh	r0, [r3]
	b	.L1489
.L1482:
	bl	INSERT_FREE_LIST
	b	.L1483
.L1491:
	.align	2
.L1490:
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
	ldr	r4, .L1494
	ldr	r5, .L1494+4
	ldrh	r1, [r4, #102]
	mov	r0, r5
	bl	FtlGcScanTempBlk
	ldrh	r2, [r5, #2]
	ldrh	r3, [r4, #102]
	cmp	r2, r3
	ldmfdcc	sp, {r4, r5, fp, sp, pc}
	ldr	r0, .L1494+8
	bl	FtlMapBlkWriteDump_data
	mov	r0, #0
	bl	FtlGcFreeTempBlock
	mov	r3, #0
	str	r3, [r4, #4008]
	ldmfd	sp, {r4, r5, fp, sp, pc}
.L1495:
	.align	2
.L1494:
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
	ldr	r4, .L1497
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
.L1498:
	.align	2
.L1497:
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
	ldr	r4, .L1506
	mov	r3, #0
	mov	r5, #0
	mov	r6, #20
	mov	r2, r3
	ldr	r1, [r4, #3164]
	ldr	r0, [r4, #3840]
	bl	FlashProgPages
.L1500:
	ldr	r1, [r4, #3164]
	uxth	r3, r5
	cmp	r1, r3
	bhi	.L1503
	ldr	r0, [r4, #3840]
	bl	FtlGcBufFree
	ldr	r3, .L1506+4
	mov	r0, #0
	str	r0, [r4, #3164]
	ldrh	r3, [r3, #4]
	cmp	r3, r0
	ldmfdne	sp, {r4, r5, r6, fp, sp, pc}
	mov	r0, #1
	bl	FtlGcFreeTempBlock
	b	.L1505
.L1503:
	mul	r3, r6, r3
	ldr	r2, [r4, #3840]
	add	r5, r5, #1
	ldr	r0, [r2, r3]
	add	r1, r2, r3
	cmn	r0, #1
	bne	.L1501
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
.L1505:
	mov	r0, #1
	ldmfd	sp, {r4, r5, r6, fp, sp, pc}
.L1501:
	ldr	r3, [r1, #12]
	ldr	r1, [r1, #4]
	ldr	r2, [r3, #8]
	ldr	r0, [r3, #12]
	bl	FtlGcUpdatePage
	b	.L1500
.L1507:
	.align	2
.L1506:
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
	ldr	r4, .L1511
	movw	r3, #2444
	ldrh	r2, [r4, r3]
	movw	r3, #65535
	cmp	r2, r3
	beq	.L1509
	ldr	r3, .L1511+4
	ldrh	r3, [r3, #4]
	cmp	r3, #0
	bne	.L1510
.L1509:
	mov	r0, #0
	mov	r5, #0
	bl	FtlGcFreeTempBlock
	ldr	r0, .L1511+4
	strb	r5, [r4, #2452]
	bl	allocate_data_superblock
	movw	r3, #3192
	strh	r5, [r4, r3]	@ movhi
	ldr	r3, .L1511+8
	strh	r5, [r3]	@ movhi
	bl	l2p_flush
	mov	r0, r5
	bl	FtlEctTblFlush
	bl	FtlVpcTblFlush
.L1510:
	ldr	r0, .L1511+4
	bl	get_new_active_ppa
	ldmfd	sp, {r4, r5, fp, sp, pc}
.L1512:
	.align	2
.L1511:
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
	ldr	r3, .L1625
	ldr	r0, [r3, #3824]
	mov	r5, r3
	cmp	r0, #0
	movne	r0, #0
	bne	.L1513
	add	r2, r3, #2336
	ldrh	r2, [r2]
	cmp	r2, #47
	bls	.L1513
	ldr	r2, .L1625+4
	ldrh	r1, [r2, #4]
	movw	r2, #65535
	cmp	r1, r2
	bne	.L1515
.L1518:
	movw	ip, #3204
	movw	r2, #65535
	ldrh	r7, [r5, ip]
	cmp	r7, r2
	bne	.L1516
.L1517:
	ldr	r3, [r5, #3152]
	movw	r2, #2588
	ldrh	r2, [r5, r2]
	movw	r4, #65535
	add	r3, r3, #1
	add	r3, r3, r6, lsl #7
	cmp	r2, r4
	str	r3, [r5, #3152]
	bne	.L1519
	movw	r1, #2444
	ldrh	r4, [r5, r1]
	cmp	r4, r2
	movne	r4, r2
	bne	.L1519
	movw	r2, #3202
	ldrh	r7, [r5, r2]
	cmp	r7, r4
	bne	.L1519
	movw	r10, #2344
	ldrh	r2, [r5, r10]
	cmp	r2, #24
	movcc	r2, #5120
	movcs	r2, #1024
	cmp	r3, r2
	movls	r4, r7
	bls	.L1519
	ldr	r3, .L1625+8
	mov	r9, #0
	str	r9, [r5, #3152]
	strh	r9, [r3, #-8]	@ movhi
	bl	GetSwlReplaceBlock
	cmp	r0, r7
	mov	r4, r0
	movne	r7, r0
	bne	.L1521
	movw	r8, #3150
	ldrh	r2, [r5, r10]
	ldrh	r3, [r5, r8]
	cmp	r2, r3
	bcs	.L1522
	mov	r0, #64
	bl	List_get_gc_head_node
	uxth	r3, r0
	cmp	r3, r4
	beq	.L1524
	mov	r0, r3
	ldr	r3, [r5, #2324]
	lsl	r0, r0, #1
	ldrh	r3, [r3, r0]
	cmp	r3, #7
	bhi	.L1525
	mov	r0, r9
	bl	List_get_gc_head_node
	uxth	r7, r0
	mov	r3, #128
	strh	r3, [r5, r8]	@ movhi
	cmp	r7, r4
	bne	.L1521
.L1524:
	bl	FtlGcReFreshBadBlk
.L1519:
	movw	r2, #65535
	sub	r3, r4, r2
	clz	r3, r3
	lsr	r3, r3, #5
	cmp	r6, #0
	movne	r6, #0
	andeq	r6, r3, #1
	cmp	r6, #0
	beq	.L1527
	movw	r3, #2344
	ldrh	r3, [r5, r3]
	cmp	r3, #24
	movhi	r6, #1
	bhi	.L1528
	ldrh	r6, [r5, #102]
	cmp	r3, #16
	lsrhi	r6, r6, #5
	bhi	.L1528
	cmp	r3, #12
	lsrhi	r6, r6, #4
	bhi	.L1528
	cmp	r3, #8
	lsrhi	r6, r6, #2
.L1528:
	movw	r2, #3148
	ldrh	r1, [r5, r2]
	cmp	r1, r3
	bcs	.L1532
	movw	r3, #2444
	movw	r1, #65535
	ldrh	r3, [r5, r3]
	cmp	r3, r1
	bne	.L1533
	movw	r1, #3202
	ldrh	r1, [r5, r1]
	cmp	r1, r3
	bne	.L1533
	ldr	r3, .L1625+8
	ldrh	r0, [r3, #-8]
	cmp	r0, #0
	bne	.L1534
	ldr	r3, [r5, #2504]
	ldr	r1, [r5, #2500]
	add	r3, r3, r3, lsl #1
	cmp	r1, r3, lsr #2
	movcs	r3, #18
	bcs	.L1618
.L1534:
	movw	r3, #2572
	movw	r2, #3148
	ldrh	r3, [r5, r3]
	add	r3, r3, r3, lsl #1
	asr	r3, r3, #2
.L1618:
	strh	r3, [r5, r2]	@ movhi
	mov	r3, #0
	str	r3, [r5, #3160]
.L1513:
	sub	sp, fp, #40
	ldmfd	sp, {r4, r5, r6, r7, r8, r9, r10, fp, sp, pc}
.L1515:
	movw	r1, #2444
	ldrh	r3, [r3, r1]
	cmp	r3, r2
	beq	.L1518
	mov	r0, #1
	bl	FtlGcFreeTempBlock
	cmp	r0, #0
	beq	.L1518
	mov	r0, #1
	b	.L1513
.L1516:
	movw	r4, #3202
	ldrh	r3, [r5, r4]
	cmp	r3, r2
	bne	.L1517
	movw	r1, #3206
	ldrh	lr, [r5, r1]
	cmp	lr, r3
	beq	.L1517
	movw	r2, #3208
	ldrh	r0, [r5, r2]
	cmp	r0, r3
	mvnne	r3, #0
	strhne	r7, [r5, r4]	@ movhi
	strhne	lr, [r5, ip]	@ movhi
	strhne	r0, [r5, r1]	@ movhi
	strhne	r3, [r5, r2]	@ movhi
	b	.L1517
.L1525:
	mov	r3, #64
.L1617:
	strh	r3, [r5, r8]	@ movhi
	b	.L1524
.L1522:
	mov	r3, #80
	b	.L1617
.L1521:
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
	ldr	r0, .L1625+12
	str	r1, [sp]
	mov	r1, r7
	bl	sftl_printk
	b	.L1524
.L1533:
	movw	r3, #2572
	movw	r2, #3148
	ldrh	r3, [r5, r3]
	add	r3, r3, r3, lsl #1
	asr	r3, r3, #2
	strh	r3, [r5, r2]	@ movhi
.L1532:
	movw	r3, #3210
	movw	r4, #65535
	ldrh	r3, [r5, r3]
	cmp	r3, #0
	addne	r6, r6, #32
	uxthne	r6, r6
.L1538:
	movw	r3, #2588
	ldrh	r2, [r5, r3]
	movw	r1, #65535
	cmp	r2, r1
	bne	.L1547
	cmp	r4, r2
	strhne	r4, [r5, r3]	@ movhi
	bne	.L1549
	movw	r2, #3202
	ldrh	r3, [r5, r2]
	cmp	r3, r4
	beq	.L1549
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
.L1549:
	movw	r7, #2588
	mov	r3, #0
	ldrh	r0, [r5, r7]
	strb	r3, [r5, #2596]
	movw	r3, #65535
	cmp	r0, r3
	beq	.L1547
	bl	IsBlkInGcList
	cmp	r0, #0
	ldr	r8, .L1625+16
	mvnne	r3, #0
	strhne	r3, [r5, r7]	@ movhi
	movw	r7, #2588
	ldrh	r2, [r5, r7]
	movw	r3, #65535
	cmp	r2, r3
	beq	.L1547
	mov	r0, r8
	bl	make_superblock
	ldr	r2, .L1625+8
	mov	r3, #0
	strh	r3, [r8, #2]	@ movhi
	strb	r3, [r5, #2594]
	strh	r3, [r2, #-6]	@ movhi
	ldrh	r3, [r5, r7]
	ldr	r1, [r5, #2324]
	lsl	r3, r3, #1
	ldrh	r3, [r1, r3]
	strh	r3, [r2, #-4]	@ movhi
.L1547:
	movw	r3, #2588
	movw	r2, #2348
	ldrh	r3, [r5, r3]
	ldrh	r2, [r5, r2]
	cmp	r2, r3
	beq	.L1553
	movw	r2, #2396
	ldrh	r2, [r5, r2]
	cmp	r2, r3
	ldrne	r7, .L1625
	movne	r5, r7
	bne	.L1554
.L1553:
	mvn	r2, #0
	movw	r3, #2588
.L1623:
	strh	r2, [r5, r3]	@ movhi
	ldr	r3, .L1625+8
.L1624:
	ldrh	r0, [r3, #-8]
	b	.L1513
.L1527:
	movw	r1, #2444
	ldrh	r7, [r5, r1]
	cmp	r7, r2
	bne	.L1595
	movw	r2, #3202
	ldrh	r2, [r5, r2]
	cmp	r2, r7
	movne	r3, #0
	andeq	r3, r3, #1
	cmp	r3, #0
	beq	.L1595
	movw	r3, #2588
	ldrh	r3, [r5, r3]
	cmp	r3, r7
	beq	.L1539
.L1544:
	mov	r4, r7
.L1595:
	mov	r6, #1
	b	.L1538
.L1539:
	movw	r3, #2344
	movw	r2, #3148
	ldrh	r8, [r5, r3]
	ldrh	r3, [r5, r2]
	str	r6, [r5, #3160]
	ldr	r4, .L1625+8
	cmp	r3, r8
	bcs	.L1540
	ldrh	r3, [r4, #-8]
	cmp	r3, #0
	bne	.L1541
	ldr	r3, [r5, #2504]
	ldr	r1, [r5, #2500]
	add	r3, r3, r3, lsl #1
	cmp	r1, r3, lsr #2
	movcs	r3, #18
	bcs	.L1619
.L1541:
	movw	r3, #2572
	movw	r2, #3148
	ldrh	r3, [r5, r3]
	add	r3, r3, r3, lsl #1
	asr	r3, r3, #2
.L1619:
	strh	r3, [r5, r2]	@ movhi
.L1622:
	bl	FtlReadRefresh
	ldrh	r0, [r4, #-8]
	b	.L1513
.L1540:
	ldrh	r0, [r4, #-8]
	cmp	r0, #0
	bne	.L1544
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
	ble	.L1545
	sub	r6, r6, #1
	cmp	r8, r6
	bge	.L1622
.L1545:
	cmp	r2, #0
	bne	.L1544
	movw	r0, #65535
	bl	decrement_vpc_count
	movw	r3, #2344
	ldrh	r0, [r5, r3]
	add	r0, r0, #1
	b	.L1513
.L1557:
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
	bne	.L1556
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
	bgt	.L1560
	cmp	ip, #8
	cmphi	r10, #48
	bls	.L1561
	ldr	ip, .L1625+20
	ldrh	ip, [ip]
	cmp	ip, #35
	bhi	.L1561
.L1560:
	ldr	lr, .L1625+24
	mov	ip, #0
	strh	ip, [lr]	@ movhi
.L1561:
	ldrh	ip, [r0, r2]
	movw	r0, #65535
	cmp	r3, ip
	cmple	r4, r0
	moveq	r3, #1
	movne	r3, #0
	bne	.L1562
	mvn	r2, #0
	movw	r3, #2588
	strh	r2, [r5, r3]	@ movhi
	mov	r2, #0
	movw	r3, #3156
	b	.L1623
.L1562:
	cmp	ip, #0
	bne	.L1563
	bl	decrement_vpc_count
	ldr	r3, .L1625+24
	ldr	r2, .L1625+24
	ldrh	r3, [r3]
	add	r3, r3, #1
	strh	r3, [r2]	@ movhi
	b	.L1556
.L1563:
	strb	r3, [r5, #2596]
	ldr	r3, .L1625+28
	ldrh	r3, [r3]
	cmp	r3, r1
	bne	.L1564
	movw	r2, #665
	ldr	r1, .L1625+32
	ldr	r0, .L1625+36
	bl	sftl_printk
.L1564:
	ldr	r3, .L1625+40
	ldrh	r2, [r8]
	ldrh	r3, [r3]
	cmp	r2, r3
	bne	.L1565
	movw	r2, #666
	ldr	r1, .L1625+32
	ldr	r0, .L1625+36
	bl	sftl_printk
.L1565:
	ldr	r3, .L1625+44
	ldrh	r2, [r8]
	ldrh	r3, [r3]
	cmp	r2, r3
	bne	.L1566
	movw	r2, #667
	ldr	r1, .L1625+32
	ldr	r0, .L1625+36
	bl	sftl_printk
.L1566:
	ldr	r0, .L1625+16
	bl	make_superblock
	ldrh	r2, [r8]
	mov	r3, #0
	ldr	r1, .L1625+48
	ldr	r0, [r5, #2324]
	lsl	r2, r2, #1
	strh	r3, [r1]	@ movhi
	ldrh	r2, [r0, r2]
	strh	r3, [r8, #2]	@ movhi
	strb	r3, [r5, #2594]
	strh	r2, [r1, #2]	@ movhi
.L1555:
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
.L1621:
	str	r3, [fp, #-52]
	ldrh	r3, [fp, #-52]
	ldr	r8, .L1625+16
	cmp	r6, r3
	bls	.L1576
	ldrh	r9, [r8, #2]
	add	r1, r8, #14
	ldr	r3, [fp, #-52]
	movw	r10, #65535
	ldrh	ip, [r7, #32]
	ldr	r0, [r7, #3180]
	add	r9, r9, r3
	mov	r3, #0
	mov	r8, r3
	b	.L1577
.L1570:
	ldrh	r2, [r1, #2]!
	add	r3, r3, #1
	cmp	r2, r10
	movne	lr, #20
	orrne	r2, r9, r2, lsl #10
	mlane	lr, lr, r8, r0
	addne	r8, r8, #1
	uxthne	r8, r8
	strne	r2, [lr, #4]
.L1577:
	uxth	r2, r3
	cmp	ip, r2
	bhi	.L1570
	ldrb	r2, [r7, #2596]	@ zero_extendqisi2
	mov	r1, r8
	bl	FlashReadPages
	mov	r3, #0
.L1620:
	str	r3, [fp, #-48]
	ldrh	r3, [fp, #-48]
	cmp	r3, r8
	ldrcs	r3, [fp, #-52]
	addcs	r3, r3, #1
	bcs	.L1621
.L1575:
	ldr	r3, [fp, #-48]
	mov	r9, #20
	mul	r9, r9, r3
	ldr	r3, [r7, #3180]
	add	r2, r3, r9
	ldr	r3, [r3, r9]
	cmn	r3, #1
	beq	.L1572
	ldr	r10, [r2, #12]
	movw	r3, #61589
	ldrh	r2, [r10]
	cmp	r2, r3
	bne	.L1572
	ldr	r3, [r10, #8]
	cmn	r3, #1
	bne	.L1573
	movw	r2, #703
	ldr	r1, .L1625+32
	ldr	r0, .L1625+36
	str	r3, [fp, #-60]
	bl	sftl_printk
	ldr	r3, [fp, #-60]
.L1573:
	mov	r2, #0
	mov	r0, r3
	sub	r1, fp, #44
	bl	log2phys
	ldr	r0, [r5, #3180]
	ldr	r3, [fp, #-44]
	add	r0, r0, r9
	ldr	r2, [r0, #4]
	cmp	r2, r3
	bne	.L1572
	ldr	r3, .L1625+48
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
	ldr	r2, .L1625+44
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
	beq	.L1574
	ldr	r2, [fp, #-60]
	ldrh	r2, [r2, #4]
	cmp	r2, #0
	bne	.L1572
.L1574:
	str	r3, [fp, #-60]
	bl	Ftl_gc_temp_data_write_back
	cmp	r0, #0
	ldr	r3, [fp, #-60]
	ldrne	r2, .L1625
	movne	r1, #0
	strne	r1, [r2, #3824]
	bne	.L1624
.L1572:
	ldr	r3, [fp, #-48]
	add	r3, r3, #1
	b	.L1620
.L1576:
	ldrh	r3, [r8, #2]
	add	r6, r6, r3
	ldr	r3, [fp, #-56]
	uxth	r6, r6
	cmp	r3, r6
	strh	r6, [r8, #2]	@ movhi
	bhi	.L1578
	ldr	r3, [r7, #3164]
	ldr	r9, .L1625+8
	cmp	r3, #0
	beq	.L1579
	bl	Ftl_gc_temp_data_write_back
	cmp	r0, #0
	movne	r3, #0
	ldrhne	r0, [r9, #-8]
	strne	r3, [r7, #3824]
	bne	.L1513
.L1579:
	ldrh	r6, [r9, #-6]
	cmp	r6, #0
	bne	.L1580
	ldrh	r1, [r8]
	ldr	r2, [r7, #2324]
	lsl	r3, r1, #1
	ldrh	r3, [r2, r3]
	cmp	r3, #0
	beq	.L1580
	ldrh	r0, [r9, #-4]
	ldrh	r2, [r8, #2]
	str	r3, [sp]
	mov	r3, r6
	str	r0, [sp, #4]
	ldr	r0, .L1625+52
	bl	sftl_printk
	ldrh	r3, [r8]
	ldr	r2, [r7, #2324]
	lsl	r3, r3, #1
	strh	r6, [r2, r3]	@ movhi
	ldrh	r0, [r8]
	bl	update_vpc_list
	bl	l2p_flush
	bl	FtlVpcTblFlush
.L1580:
	mvn	r3, #0
	strh	r3, [r8]	@ movhi
.L1578:
	ldr	r3, .L1625+56
	ldrh	r3, [r3]
	cmp	r3, #2
	bhi	.L1581
	ldrh	r6, [r7, #102]
.L1554:
	ldr	r8, .L1625+16
	movw	r3, #65535
	ldrh	r2, [r8]
	cmp	r2, r3
	bne	.L1555
	add	r9, r8, #568
	mov	r3, #0
	str	r3, [r7, #3160]
.L1556:
	ldrh	r10, [r9]
	mov	r0, r10
	bl	List_get_gc_head_node
	ldr	r3, .L1625+16
	uxth	r1, r0
	strh	r1, [r3]	@ movhi
	movw	r3, #65535
	cmp	r1, r3
	bne	.L1557
	mov	r3, #0
	mov	r0, #8
	strh	r3, [r9]	@ movhi
	b	.L1513
.L1581:
	mov	r2, #0
	str	r2, [r7, #3824]
	ldr	r2, .L1625+8
	ldrh	r0, [r2, #-8]
	cmp	r0, #0
	addeq	r0, r3, #1
	b	.L1513
.L1626:
	.align	2
.L1625:
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
	bne	.L1629
	mov	r2, r3
	ldr	r1, [fp, #-72]
	add	r0, r7, #256
	bl	FtlVendorPartRead
	str	r0, [fp, #-48]
.L1628:
	ldr	r0, [fp, #-48]
	sub	sp, fp, #40
	ldmfd	sp, {r4, r5, r6, r7, r8, r9, r10, fp, sp, pc}
.L1629:
	ldr	r3, [fp, #-72]
	ldr	r4, .L1663
	add	r3, r1, r3
	str	r3, [fp, #-56]
	ldr	r2, [fp, #-56]
	ldr	r3, [r4, #140]
	cmp	r2, r3
	bhi	.L1650
	ldr	r3, .L1663+4
	ldr	r3, [r3]
	cmn	r3, #1
	beq	.L1651
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
.L1631:
	ldr	r3, [fp, #-52]
	cmp	r3, #0
	bne	.L1648
	ldr	r3, .L1663
	movw	r2, #3210
	ldrh	r2, [r3, r2]
	cmp	r2, #0
	bne	.L1649
	movw	r2, #2344
	ldrh	r3, [r3, r2]
	cmp	r3, #31
	bhi	.L1628
.L1649:
	mov	r1, #1
	mov	r0, #0
	bl	rk_ftl_garbage_collect
	b	.L1628
.L1648:
	mov	r2, #0
	sub	r1, fp, #44
	mov	r0, r5
	bl	log2phys
	ldr	r2, [fp, #-44]
	cmn	r2, #1
	moveq	r10, #0
	beq	.L1633
	ldr	r3, [r4, #3836]
	mov	r1, #20
	ldrh	r10, [r4, #58]
	mla	r3, r1, r6, r3
	str	r2, [r3, #4]
	ldr	r2, [fp, #-60]
	cmp	r5, r2
	bne	.L1637
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
.L1638:
	ldrh	r2, [r4, #112]
	ldr	r1, [r4, #3816]
	str	r5, [r3, #16]
	mul	r2, r6, r2
	add	r6, r6, #1
	bic	r2, r2, #3
	add	r2, r1, r2
	str	r2, [r3, #12]
	b	.L1636
.L1635:
	mla	r0, r0, r5, r10
	ldr	r3, [fp, #-56]
	cmp	r3, r0
	movhi	r3, #1
	movls	r3, #0
	cmp	r7, r0
	movhi	r3, #0
	cmp	r3, #0
	beq	.L1634
	sub	r0, r0, r7
	mov	r1, #512
	add	r0, r9, r0, lsl #9
	bl	__memzero
.L1634:
	add	r10, r10, #1
.L1633:
	ldrh	r0, [r4, #58]
	cmp	r10, r0
	bcc	.L1635
.L1636:
	ldr	r3, [fp, #-52]
	add	r5, r5, #1
	subs	r3, r3, #1
	str	r3, [fp, #-52]
	beq	.L1640
	ldrh	r3, [r4, #32]
	cmp	r6, r3, lsl #2
	bne	.L1631
.L1640:
	cmp	r6, #0
	beq	.L1631
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
.L1647:
	ldr	r3, [fp, #-68]
	mov	ip, #20
	ldr	r1, [fp, #-60]
	mul	r10, ip, r3
	ldr	r3, [r4, #3836]
	add	r3, r3, r10
	ldr	r2, [r3, #16]
	cmp	r1, r2
	bne	.L1642
	ldr	r1, [r3, #8]
	ldr	r3, [r4, #3820]
	cmp	r1, r3
	bne	.L1643
	ldr	r3, [fp, #-84]
	mov	r0, r9
	ldr	r2, [fp, #-88]
	add	r1, r1, r3
.L1662:
	bl	ftl_memcpy
.L1643:
	ldr	r1, [r4, #3836]
	add	r1, r1, r10
	ldr	r3, [r1, #12]
	ldr	r2, [r1, #16]
	ldr	r3, [r3, #8]
	cmp	r2, r3
	beq	.L1644
	ldr	r3, [r4, #2708]
	add	r3, r3, #1
	str	r3, [r4, #2708]
	ldr	r2, [r1, #8]
	ldr	r3, [r1, #12]
	ldr	r0, [r2, #4]
	str	r0, [sp, #16]
	ldr	r2, [r2]
	ldr	r0, .L1663+8
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
.L1644:
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
	bne	.L1646
	ldr	r0, [r0, #4]
	ubfx	r0, r0, #10, #16
	bl	P2V_block_in_plane
	bl	FtlGcRefreshBlock
.L1646:
	ldr	r3, [fp, #-68]
	add	r3, r3, #1
	cmp	r6, r3
	str	r3, [fp, #-68]
	bne	.L1647
	mov	r6, #0
	b	.L1631
.L1637:
	ldr	r2, [fp, #-64]
	cmp	r5, r2
	bne	.L1639
	ldr	r2, [r4, #3812]
	ldr	r1, [fp, #-56]
	str	r2, [r3, #8]
	mul	r2, r10, r5
	sub	r8, r1, r2
	cmp	r10, r8
	subeq	r2, r2, r7
	addeq	r2, r9, r2, lsl #9
	streq	r2, [r3, #8]
	b	.L1638
.L1639:
	mul	r10, r5, r10
	sub	r10, r10, r7
	add	r10, r9, r10, lsl #9
	str	r10, [r3, #8]
	b	.L1638
.L1642:
	ldr	r1, [fp, #-64]
	cmp	r1, r2
	bne	.L1643
	ldr	r1, [r3, #8]
	ldr	r3, [r4, #3812]
	cmp	r1, r3
	bne	.L1643
	ldrh	r0, [r4, #58]
	ldr	r3, [fp, #-64]
	ldr	r2, [fp, #-92]
	mul	r0, r3, r0
	sub	r0, r0, r7
	add	r0, r9, r0, lsl #9
	b	.L1662
.L1650:
	mvn	r3, #0
.L1651:
	str	r3, [fp, #-48]
	b	.L1628
.L1664:
	.align	2
.L1663:
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
	bne	.L1668
	mov	r2, r3
	ldr	r1, [fp, #-84]
	add	r0, r9, #256
	bl	FtlVendorPartWrite
.L1667:
	sub	sp, fp, #40
	ldmfd	sp, {r4, r5, r6, r7, r8, r9, r10, fp, sp, pc}
.L1668:
	ldr	r3, [fp, #-84]
	ldr	r4, .L1718
	add	r6, r1, r3
	ldr	r3, [r4, #140]
	cmp	r6, r3
	mvnhi	r0, #0
	bhi	.L1667
	ldr	r5, .L1718+4
	ldr	r0, [r5]
	cmn	r0, #1
	beq	.L1667
	ldr	r3, .L1718+8
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
	ldr	r4, .L1718+12
	cmp	r3, #0
	beq	.L1670
	ldrh	r3, [r4, #4]
	add	r0, r4, #48
	cmp	r3, #0
	moveq	r4, r0
.L1670:
	ldr	r5, [fp, #-76]
	ldr	r10, .L1718
.L1671:
	ldr	r3, [fp, #-72]
	cmp	r3, #0
	bne	.L1699
	mov	r0, r3
	ldr	r1, [fp, #-96]
	bl	rk_ftl_garbage_collect
	movw	r3, #2344
	ldrh	r3, [r10, r3]
	cmp	r3, #15
	bls	.L1700
.L1702:
	mov	r0, #0
	b	.L1667
.L1699:
	ldrb	r2, [r4, #6]	@ zero_extendqisi2
	ldrh	r3, [r10, #32]
	cmp	r2, r3
	bcc	.L1672
	mov	r2, #996
	ldr	r1, .L1718+16
	ldr	r0, .L1718+20
	bl	sftl_printk
.L1672:
	ldrh	r2, [r4, #4]
	cmp	r2, #0
	bne	.L1673
	ldr	r3, .L1718+12
	ldr	r6, .L1718+4
	cmp	r4, r3
	bne	.L1674
	add	r0, r4, #48
	ldrh	r7, [r0, #4]
	cmp	r7, #0
	bne	.L1675
	bl	allocate_new_data_superblock
	str	r7, [r6, #8]
.L1675:
	ldr	r0, .L1718+12
	bl	allocate_new_data_superblock
	ldr	r4, .L1718+12
	ldr	r3, [r6, #8]
	add	r0, r4, #48
	cmp	r3, #0
	movne	r4, r0
.L1676:
	ldrh	r3, [r4, #4]
	cmp	r3, #0
	bne	.L1673
	mov	r0, r4
	bl	allocate_new_data_superblock
.L1673:
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
	bcc	.L1677
	movw	r2, #1029
	ldr	r1, .L1718+16
	ldr	r0, .L1718+20
	bl	sftl_printk
.L1677:
	mov	r3, #0
.L1717:
	str	r3, [fp, #-68]
	ldr	r3, [fp, #-68]
	ldr	r2, [fp, #-108]
	cmp	r3, r2
	bne	.L1697
.L1679:
	mov	r3, r4
	mov	r2, #0
	ldr	r1, [fp, #-68]
	ldr	r0, [r10, #3844]
	bl	FtlProgPages
	ldr	r3, [fp, #-68]
	ldr	r2, [fp, #-72]
	cmp	r3, r2
	bls	.L1698
	movw	r2, #1106
	ldr	r1, .L1718+16
	ldr	r0, .L1718+20
	bl	sftl_printk
.L1698:
	ldr	r3, [fp, #-72]
	ldr	r2, [fp, #-68]
	sub	r3, r3, r2
	str	r3, [fp, #-72]
	b	.L1671
.L1674:
	str	r2, [r6, #8]
	ldrh	r2, [r3, #4]
	cmp	r2, #0
	movne	r4, r3
	bne	.L1673
	mov	r0, r4
	bl	allocate_new_data_superblock
	b	.L1676
.L1697:
	ldrh	r3, [r4, #4]
	cmp	r3, #0
	beq	.L1679
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
	bne	.L1680
	cmp	r5, r3
	bne	.L1681
	mov	r1, r6
	mov	r0, r9
	bl	__umodsi3
	ldr	r3, [fp, #-84]
	sub	r6, r6, r0
	str	r0, [fp, #-88]
	cmp	r6, r3
	movcs	r6, r3
.L1682:
	ldrh	r3, [r10, #58]
	cmp	r6, r3
	ldr	r3, [fp, #-76]
	bne	.L1683
	cmp	r5, r3
	ldr	r3, [r10, #3844]
	add	r7, r3, r7
	ldreq	r3, [fp, #-80]
	streq	r3, [r7, #8]
	beq	.L1685
.L1714:
	mul	r6, r5, r6
	ldr	r3, [fp, #-80]
	sub	r6, r6, r9
	add	r6, r3, r6, lsl #9
	str	r6, [r7, #8]
	b	.L1685
.L1681:
	ldr	r3, [fp, #-84]
	add	r2, r9, r3
	smulbb	r3, r6, r5
	sub	r6, r2, r3
	mov	r3, #0
	uxth	r6, r6
	str	r3, [fp, #-88]
	b	.L1682
.L1683:
	cmp	r5, r3
	ldr	r3, [r10, #3844]
	ldreq	r2, [r10, #3820]
	ldrne	r2, [r10, #3812]
	add	r3, r3, r7
	str	r2, [r3, #8]
	ldr	r3, [fp, #-64]
	cmn	r3, #1
	beq	.L1688
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
	beq	.L1692
	ldr	r3, [r8, #8]
	cmp	r5, r3
	beq	.L1691
	ldr	r3, [r10, #2708]
	mov	r2, r5
	ldr	r0, .L1718+24
	add	r3, r3, #1
	str	r3, [r10, #2708]
	ldr	r1, [r8, #8]
	bl	sftl_printk
.L1691:
	ldr	r3, [r8, #8]
	cmp	r5, r3
	beq	.L1692
	movw	r2, #1082
	ldr	r1, .L1718+16
	ldr	r0, .L1718+20
	bl	sftl_printk
.L1692:
	ldr	r3, [fp, #-76]
	lsl	r2, r6, #9
	cmp	r5, r3
	bne	.L1693
	ldr	r3, [r10, #3844]
	ldr	r1, [fp, #-80]
	add	r7, r3, r7
	ldr	r3, [fp, #-88]
	ldr	r0, [r7, #8]
	add	r0, r0, r3, lsl #9
.L1715:
	bl	ftl_memcpy
.L1685:
	ldrb	r2, [r4, #6]	@ zero_extendqisi2
	ldrh	r3, [r10, #32]
	cmp	r2, r3
	bcc	.L1694
	movw	r2, #1097
	ldr	r1, .L1718+16
	ldr	r0, .L1718+20
	bl	sftl_printk
.L1694:
	ldr	r3, .L1718+28
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
	b	.L1717
.L1688:
	ldr	r3, [r10, #3844]
	mov	r1, #0
	ldrh	r2, [r10, #110]
	add	r3, r3, r7
	ldr	r0, [r3, #8]
	bl	ftl_memset
	b	.L1692
.L1693:
	ldrh	r1, [r10, #58]
	ldr	r3, [r10, #3844]
	mul	r1, r5, r1
	add	r7, r3, r7
	ldr	r3, [fp, #-80]
	ldr	r0, [r7, #8]
	sub	r1, r1, r9
	add	r1, r3, r1, lsl #9
	b	.L1715
.L1680:
	ldr	r3, [r10, #3844]
	add	r7, r3, r7
	b	.L1714
.L1700:
	ldr	r4, .L1718+32
	ldr	r5, .L1718+36
.L1712:
	ldrh	r3, [r4]
	movw	r2, #65535
	cmp	r3, r2
	bne	.L1701
	ldrh	r2, [r5]
	cmp	r2, r3
	bne	.L1701
	mov	r0, #0
	bl	List_get_gc_head_node
	uxth	r0, r0
	bl	FtlGcRefreshBlock
.L1701:
	ldr	r3, .L1718+40
	mov	r1, #1
	mov	r2, #128
	mov	r0, r1
	strh	r2, [r3]	@ movhi
	strh	r2, [r3, #-2]	@ movhi
	bl	rk_ftl_garbage_collect
	mov	r1, #1
	mov	r0, #0
	bl	rk_ftl_garbage_collect
	ldr	r3, .L1718+44
	ldrh	r3, [r3]
	cmp	r3, #8
	bls	.L1712
	b	.L1702
.L1719:
	.align	2
.L1718:
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
	ldr	r4, .L1751
	beq	.L1721
	ldr	r3, [r4, #-4012]
	cmp	r3, #0
	beq	.L1722
	ldr	r2, .L1751+4
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
	ldr	r2, .L1751+8
	mov	r3, #0
	ldr	r1, .L1751+12
	str	r0, [fp, #-108]
	sub	ip, r2, #262144
.L1726:
	ldr	r0, [r2, #-4]
	cmp	r0, #0
	beq	.L1723
.L1727:
	ldr	r3, [fp, #-108]
	mov	r4, #0
	ldr	r2, [fp, #-120]
	ldr	r9, .L1751+16
	ldr	r5, .L1751+4
	str	r4, [fp, #-124]
	mul	r3, r3, r2
	str	r3, [fp, #-144]
	ldr	r3, [fp, #-108]
	str	r3, [fp, #-128]
.L1724:
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
	bcs	.L1728
	mov	r1, r8
	add	r0, r6, r4
	bl	__udivsi3
	ldr	r3, [r5, #3252]
	mov	r1, r0
	mov	r0, #0
	blx	r3
.L1728:
	mov	r1, r6
	mov	r0, r4
	bl	__umodsi3
	mov	r6, #0
	mov	r8, r0
	sub	r3, r4, r0
	str	r3, [fp, #-140]
.L1729:
	cmp	r6, #512
	bcc	.L1730
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
.L1731:
	cmp	r6, #512
	bcs	.L1735
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
	bne	.L1732
.L1735:
	ldr	r3, .L1751+20
	mov	r6, #0
.L1733:
	mov	r7, r3
	ldr	r1, [r9, r6, lsl #2]
	ldr	r2, [r7]
	add	r3, r3, #4
	cmp	r1, r2
	beq	.L1736
	mov	r1, #512
	mov	r0, r9
	bl	__memzero
	ldr	r2, [r9, r6, lsl #2]
	mov	r1, r4
	str	r6, [sp]
	ldr	r0, .L1751+24
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
	bls	.L1737
	ldr	r3, [fp, #-120]
	ldr	r1, [fp, #-116]
	add	r0, r3, r4
	bl	__udivsi3
	ldr	r3, [r5, #3252]
	mov	r1, r0
	mov	r0, #0
	blx	r3
.L1737:
	ldr	r2, [fp, #-128]
	ldr	r3, [fp, #-108]
	add	r3, r3, r2
	ldr	r2, [fp, #-144]
	add	r4, r4, r2
	ldr	r2, [fp, #-128]
	cmp	r2, #15
	bls	.L1743
.L1742:
	ldr	r3, .L1751
	mov	r2, #0
	str	r2, [r3, #-4012]
.L1722:
	ldr	r3, [fp, #-132]
	mov	r1, r10
	ldr	r2, [fp, #-112]
	mov	r0, #0
	bl	FtlWrite
	sub	sp, fp, #40
	ldmfd	sp, {r4, r5, r6, r7, r8, r9, r10, fp, sp, pc}
.L1723:
	ldr	r0, [ip, r3, lsl #2]
	add	r3, r3, #1
	cmp	r3, #4096
	movhi	r3, #0
	str	r0, [r2, #-4]!
	cmp	r1, r2
	bne	.L1726
	b	.L1727
.L1743:
	str	r3, [fp, #-128]
	b	.L1724
.L1730:
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
	ldr	r3, .L1751+20
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
	b	.L1729
.L1732:
	add	r6, r8, r6
	mov	r3, #0
	uxth	r6, r6
	b	.L1731
.L1736:
	add	r6, r6, #1
	cmp	r6, #65536
	bne	.L1733
	ldr	r3, [fp, #-124]
	add	r3, r3, #1
	cmp	r3, #5
	str	r3, [fp, #-124]
	bls	.L1737
	b	.L1742
.L1721:
	cmp	r0, #63
	ldr	r0, .L1751+20
	ldrls	r3, [fp, #-112]
	rsbls	r1, r10, #64
	ldrhi	r3, .L1751+28
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
	b	.L1722
.L1752:
	.align	2
.L1751:
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
	ldr	r4, .L1775
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
	bne	.L1755
.L1763:
	mvn	r0, #0
	ldmfd	sp, {r4, r5, r6, r7, r8, fp, sp, pc}
.L1755:
	ldr	r8, .L1775+4
	mov	r1, #1
	add	r7, r4, #3264
	bl	FtlGetLastWrittenPage
	sxth	r5, r0
	add	r0, r0, #1
	strh	r0, [r6, #2]	@ movhi
.L1757:
	cmp	r5, #0
	bge	.L1760
	movw	r2, #1482
	ldr	r1, .L1775+8
	ldr	r0, .L1775+12
	bl	sftl_printk
.L1759:
	ldrh	r3, [r4, #40]
	ldrh	r2, [r4, #110]
	add	r3, r3, #24
	cmp	r2, r3, lsl #1
	bcs	.L1762
	movw	r2, #1484
	ldr	r1, .L1775+8
	ldr	r0, .L1775+12
	bl	sftl_printk
.L1762:
	ldr	r5, .L1775+16
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
	ldr	r3, .L1775+4
	cmp	r2, r3
	bne	.L1763
	ldrb	r2, [r4, #2278]	@ zero_extendqisi2
	ldrh	r3, [r4, #54]
	ldrh	r7, [r5, #8]
	cmp	r2, r3
	strh	r7, [r6, #6]	@ movhi
	bne	.L1763
	ldr	r3, .L1775+20
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
	bls	.L1764
	movw	r2, #1506
	ldr	r1, .L1775+8
	ldr	r0, .L1775+12
	bl	sftl_printk
.L1764:
	ldrh	r0, [r5, #14]
	movw	r3, #2348
	ldr	r1, .L1775+24
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
	beq	.L1767
	ldr	r0, .L1775+24
	bl	make_superblock
.L1767:
	movw	r3, #2396
	ldrh	r2, [r4, r3]
	movw	r3, #65535
	cmp	r2, r3
	beq	.L1768
	ldr	r0, .L1775+28
	bl	make_superblock
.L1768:
	movw	r3, #2444
	ldrh	r2, [r4, r3]
	movw	r3, #65535
	cmp	r2, r3
	beq	.L1769
	ldr	r0, .L1775+32
	bl	make_superblock
.L1769:
	movw	r3, #2588
	ldrh	r2, [r4, r3]
	movw	r3, #65535
	cmp	r2, r3
	beq	.L1770
	ldr	r0, .L1775+36
	bl	make_superblock
.L1770:
	mov	r0, #0
	ldmfd	sp, {r4, r5, r6, r7, r8, fp, sp, pc}
.L1760:
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
	beq	.L1758
	ldr	r3, [r4, #3284]
	ldr	r3, [r3]
	cmp	r3, r8
	bne	.L1758
	ldr	r3, [r4, #3288]
	ldrh	r2, [r3]
	movw	r3, #61604
	cmp	r2, r3
	beq	.L1759
.L1758:
	sub	r5, r5, #1
	sxth	r5, r5
	b	.L1757
.L1776:
	.align	2
.L1775:
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
	ldr	r4, .L1801
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
	bne	.L1778
.L1780:
	mvn	r8, #0
.L1777:
	mov	r0, r8
	ldmfd	sp, {r4, r5, r6, r7, r8, fp, sp, pc}
.L1778:
	bl	FtlLoadSysInfo
	subs	r8, r0, #0
	bne	.L1780
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
.L1781:
	cmp	r3, r1
	bge	.L1786
	mla	ip, r0, r3, r2
	ldr	ip, [ip, #4]
	cmp	ip, #0
	bge	.L1782
.L1786:
	ldr	r5, .L1801+4
	cmp	r3, r1
	ldrh	r2, [r5, #28]
	add	r7, r5, #80
	add	r2, r2, #1
	strh	r2, [r5, #28]	@ movhi
	bge	.L1800
.L1783:
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
	ldr	r1, .L1801+8
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
	ldr	r0, .L1801+12
	strh	r3, [r1, #4]	@ movhi
	strh	r2, [r1, #2]	@ movhi
	strb	r3, [r4, #2402]
	bl	FtlMapBlkWriteDump_data
	ldr	r0, .L1801+16
	bl	FtlMapBlkWriteDump_data
	ldrh	r3, [r5, #30]
	add	r3, r3, #1
	strh	r3, [r5, #30]	@ movhi
	bl	l2p_flush
	bl	FtlVpcTblFlush
	bl	FtlVpcTblFlush
	b	.L1787
.L1782:
	add	r3, r3, #1
	b	.L1781
.L1800:
	ldrh	r3, [r6]
	cmp	r3, #0
	bne	.L1783
.L1787:
	ldrh	r6, [r5, #44]
	movw	r2, #65533
	sub	r3, r6, #1
	uxth	r3, r3
	cmp	r3, r2
	bhi	.L1789
	movw	r3, #3202
	mov	r1, r6
	ldr	r0, .L1801+20
	strh	r6, [r4, r3]	@ movhi
	bl	test_node_in_list
	cmp	r0, #0
	bne	.L1790
	ldrh	r3, [r4, #40]
	cmp	r3, r6
	bls	.L1790
	ldr	r3, [r4, #2324]
	lsl	r6, r6, #1
	ldrh	r3, [r3, r6]
	cmp	r3, #0
	bne	.L1789
.L1790:
	mvn	r2, #0
	movw	r3, #3202
	strh	r2, [r4, r3]	@ movhi
.L1789:
	ldrh	r6, [r5, #46]
	movw	r2, #65533
	sub	r3, r6, #1
	uxth	r3, r3
	cmp	r3, r2
	bhi	.L1793
	movw	r3, #3204
	mov	r1, r6
	ldr	r0, .L1801+20
	strh	r6, [r4, r3]	@ movhi
	bl	test_node_in_list
	cmp	r0, #0
	bne	.L1794
	ldrh	r3, [r4, #40]
	cmp	r3, r6
	bls	.L1794
	ldr	r3, [r4, #2324]
	lsl	r6, r6, #1
	ldrh	r3, [r3, r6]
	cmp	r3, #0
	bne	.L1793
.L1794:
	mvn	r2, #0
	movw	r3, #3204
	strh	r2, [r4, r3]	@ movhi
.L1793:
	movw	r3, #2348
	ldrh	r0, [r4, r3]
	movw	r3, #65535
	cmp	r0, r3
	beq	.L1796
	ldrh	r3, [r7, #4]
	cmp	r3, #0
	bne	.L1796
	ldr	r6, .L1801+8
	ldrh	r3, [r6, #4]
	cmp	r3, #0
	bne	.L1796
	bl	FtlGcRefreshBlock
	movw	r3, #2396
	ldrh	r0, [r4, r3]
	bl	FtlGcRefreshBlock
	bl	FtlVpcTblFlush
	sub	r0, r6, #48
	bl	allocate_new_data_superblock
	mov	r0, r6
	bl	allocate_new_data_superblock
.L1796:
	ldrh	r3, [r5, #28]
	tst	r3, #31
	bne	.L1777
	bl	FtlVpcCheckAndModify
	b	.L1777
.L1802:
	.align	2
.L1801:
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
	ldr	r4, .L1827
	mov	r3, #0
	ldrh	r0, [r4, #36]
	str	r3, [r4, #2540]
	str	r3, [r4, #2544]
	bl	FtlFreeSysBlkQueueInit
	bl	FtlLoadBbt
	cmp	r0, #0
	beq	.L1804
	bl	FtlMakeBbt
.L1804:
	ldr	r0, .L1827+4
	mov	r2, #0
.L1805:
	ldrh	r1, [r4, #58]
	uxth	r3, r2
	add	r2, r2, #1
	cmp	r3, r1, lsl #7
	blt	.L1806
	ldrh	r6, [r4, #40]
	mov	r5, #0
.L1807:
	ldrh	r3, [r4, #42]
	cmp	r3, r6
	bhi	.L1808
	ldrh	r1, [r4, #32]
	sub	r3, r5, #3
	cmp	r3, r1, lsl #1
	bge	.L1809
.L1813:
	mov	r0, #0
	mov	r6, r0
.L1810:
	ldrh	r2, [r4, #40]
	uxth	r3, r0
	add	r5, r0, #1
	cmp	r2, r3
	bhi	.L1814
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
	ble	.L1815
	mov	r1, r5
	sub	r0, r10, r6
	bl	__udivsi3
	str	r0, [r4, #2504]
	lsr	r0, r0, #5
	add	r0, r0, #24
	strh	r0, [r4, r9]	@ movhi
.L1815:
	ldrh	r3, [r4, #94]
	cmp	r3, #0
	beq	.L1817
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
.L1817:
	movw	r3, #2572
	ldr	r6, .L1827+8
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
	ldr	r5, .L1827+12
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
.L1819:
	mov	r0, r8
	bl	make_superblock
	ldrb	r3, [r4, #2355]	@ zero_extendqisi2
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
.L1806:
	ldr	ip, [r4, #3820]
	mvn	r1, r3
	orr	r1, r3, r1, lsl #16
	str	r1, [ip, r3, lsl #2]
	ldr	r1, [r4, #3812]
	str	r0, [r1, r3, lsl #2]
	b	.L1805
.L1808:
	mov	r0, r6
	mov	r1, #1
	bl	FtlLowFormatEraseBlock
	add	r6, r6, #1
	add	r5, r5, r0
	uxth	r5, r5
	uxth	r6, r6
	b	.L1807
.L1809:
	mov	r0, r5
	bl	__udivsi3
	ldr	r3, [r4, #132]
	add	r0, r0, r3
	uxth	r0, r0
	bl	FtlSysBlkNumInit
	ldrh	r0, [r4, #36]
	bl	FtlFreeSysBlkQueueInit
	ldrh	r5, [r4, #40]
.L1811:
	ldrh	r3, [r4, #42]
	cmp	r3, r5
	bls	.L1813
	mov	r0, r5
	mov	r1, #1
	add	r5, r5, #1
	bl	FtlLowFormatEraseBlock
	uxth	r5, r5
	b	.L1811
.L1814:
	mov	r1, #0
	uxth	r0, r0
	bl	FtlLowFormatEraseBlock
	add	r6, r6, r0
	mov	r0, r5
	uxth	r6, r6
	b	.L1810
.L1820:
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
	ldr	r2, .L1827+16
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
.L1821:
	mov	r0, r8
	bl	make_superblock
	ldrb	r3, [r4, #2403]	@ zero_extendqisi2
	cmp	r3, #0
	ldrh	r3, [r5]
	bne	.L1822
	ldr	r2, [r4, #2324]
	lsl	r3, r3, #1
	strh	r7, [r2, r3]	@ movhi
	ldrh	r3, [r5]
	add	r3, r3, #1
	strh	r3, [r5]	@ movhi
	b	.L1821
.L1822:
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
	ldr	r3, .L1827+20
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
	ldreq	r3, .L1827+24
	moveq	r2, #1
	streq	r2, [r3]
	ldmfd	sp, {r4, r5, r6, r7, r8, r9, r10, fp, sp, pc}
.L1828:
	.align	2
.L1827:
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
	ldr	r4, .L1835
	mvn	r3, #0
	ldr	r5, .L1835+4
	ldr	r1, .L1835+8
	ldr	r0, .L1835+12
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
	bne	.L1833
	bl	FtlSysBlkInit
	cmp	r0, #0
	bne	.L1833
	mov	r3, #1
	str	r3, [r5]
	movw	r3, #2344
	ldrh	r3, [r4, r3]
	cmp	r3, #15
	bhi	.L1833
	movw	r4, #8129
.L1832:
	mov	r1, #1
	mov	r0, #0
	bl	rk_ftl_garbage_collect
	subs	r4, r4, #1
	bne	.L1832
.L1833:
	mov	r0, #0
	ldmfd	sp, {r4, r5, fp, sp, pc}
.L1836:
	.align	2
.L1835:
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
	ldr	r2, .L1841
	ldr	r3, [r2, #-4004]
	cmp	r3, #0
	streq	r0, [r2, #-4004]
	moveq	r0, r3
	streq	r1, [r2, #-4000]
	mvnne	r0, #0
	ldmfd	sp, {fp, sp, pc}
.L1842:
	.align	2
.L1841:
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
	ldr	r4, .L1852
	bl	ftl_malloc
	cmp	r0, #0
	str	r0, [r4, #-3996]
	mvneq	r9, #11
	beq	.L1843
	ldr	r8, .L1852+4
	mov	r6, #0
	mov	r5, r6
	mov	r7, r6
.L1847:
	ldr	r3, [r4, #-4004]
	mov	r1, #128
	ldr	r2, [r4, #-3996]
	lsl	r0, r7, #7
	blx	r3
	subs	r9, r0, #0
	bne	.L1845
	ldr	r2, [r4, #-3996]
	ldr	r3, [r2]
	cmp	r3, r8
	bne	.L1846
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
.L1846:
	add	r7, r7, #1
	cmp	r7, #2
	bne	.L1847
	cmp	r5, #0
	beq	.L1848
	ldr	r3, [r4, #-4004]
	mov	r1, #128
	lsl	r0, r6, #7
	blx	r3
	subs	r9, r0, #0
	beq	.L1843
.L1845:
	ldr	r0, [r4, #-3996]
	mvn	r9, #0
	bl	kfree
	mov	r3, #0
	str	r3, [r4, #-3996]
	b	.L1843
.L1848:
	mov	r1, #65536
	mov	r0, r2
	bl	__memzero
	ldr	r3, [r4, #-3996]
	mov	r2, #1
	add	r1, r3, #61440
	str	r2, [r3, #4]
	str	r8, [r3]
	str	r2, [r1, #4092]
	ldr	r2, .L1852+8
	strh	r5, [r3, #12]	@ movhi
	strh	r2, [r3, #14]	@ movhi
.L1843:
	mov	r0, r9
	ldmfd	sp, {r4, r5, r6, r7, r8, r9, fp, sp, pc}
.L1853:
	.align	2
.L1852:
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
	ldr	r3, .L1860
	mov	r4, r0
	mov	r0, r1
	ldr	ip, [r3, #-3996]
	cmp	ip, #0
	ldrhne	lr, [ip, #10]
	movne	r3, #0
	bne	.L1856
.L1859:
	mvn	r0, #0
	ldmfd	sp, {r4, r5, fp, sp, pc}
.L1857:
	add	r3, r3, #1
.L1856:
	cmp	r3, lr
	bcs	.L1859
	add	r1, ip, r3, lsl #3
	ldrh	r5, [r1, #16]
	cmp	r5, r4
	bne	.L1857
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
.L1861:
	.align	2
.L1860:
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
	ldr	r8, .L1881
	mov	r9, r2
	ldr	r4, [r8, #-3996]
	cmp	r4, #0
	beq	.L1877
	ldrh	r2, [r4, #10]
	add	r7, r9, #63
	ldrh	r3, [r4, #8]
	bic	r7, r7, #63
	mov	r5, #0
	str	r3, [fp, #-44]
.L1864:
	cmp	r5, r2
	bcc	.L1872
	ldrh	r1, [r4, #14]
	cmp	r7, r1
	bhi	.L1877
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
	b	.L1880
.L1872:
	add	r6, r4, r5, lsl #3
	ldrh	r3, [r6, #16]
	cmp	r3, r0
	str	r3, [fp, #-48]
	bne	.L1865
	ldrh	r1, [r6, #20]
	add	r3, r4, #1024
	add	r1, r1, #63
	bic	r1, r1, #63
	cmp	r9, r1
	str	r1, [fp, #-52]
	bls	.L1866
	ldrh	r1, [r4, #14]
	cmp	r7, r1
	bhi	.L1877
	ldrh	r8, [r6, #18]
	sub	r2, r2, #1
	str	r2, [fp, #-56]
.L1867:
	ldr	r2, [fp, #-56]
	add	r6, r6, #8
	cmp	r5, r2
	bcc	.L1868
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
.L1869:
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
	ldr	r3, .L1881
	ldr	r3, [r3, #-4000]
.L1880:
	ldr	r0, [fp, #-44]
	mov	r2, r4
	mov	r1, #128
	lsl	r0, r0, #7
	blx	r3
	mov	r0, #0
.L1862:
	sub	sp, fp, #40
	ldmfd	sp, {r4, r5, r6, r7, r8, r9, r10, fp, sp, pc}
.L1868:
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
	b	.L1867
.L1866:
	add	r5, r4, r5, lsl #3
	mov	r2, r9
	mov	r1, ip
	ldrh	r0, [r5, #18]
	add	r0, r3, r0
	bl	memcpy
	strh	r9, [r6, #20]	@ movhi
	b	.L1869
.L1865:
	add	r5, r5, #1
	b	.L1864
.L1877:
	mvn	r0, #0
	b	.L1862
.L1882:
	.align	2
.L1881:
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
	push	{r4, r5, r6, fp, ip, lr, pc}
	sub	fp, ip, #4
	mov	r0, #4096
	mov	r5, r2
	mov	r6, r1
	bl	ftl_malloc
	subs	r4, r0, #0
	mvneq	r5, #0
	beq	.L1883
	ldr	r3, .L1908
	cmp	r6, r3
	beq	.L1886
	add	r3, r3, #1
	cmp	r6, r3
	beq	.L1887
.L1903:
	mvn	r5, #13
	b	.L1885
.L1886:
	ldr	r6, .L1908+4
	mov	r3, sp
	and	r3, r3, r6
	ldr	r3, [r3, #72]
	.syntax divided
@ 552 "./arch/arm/include/asm/uaccess.h" 1
	adds r2, r5, #8; sbcccs r2, r2, r3; movcc r3, #0
@ 0 "" 2
	.arm
	.syntax unified
	cmp	r3, #0
	bne	.L1893
	mov	r2, #8
	mov	r1, r5
	bl	arm_copy_from_user
	cmp	r0, #0
	bne	.L1903
	ldr	r2, [r4]
	ldr	r3, .L1908+8
	cmp	r2, r3
	beq	.L1891
.L1892:
	mvn	r5, #0
.L1885:
	mov	r0, r4
	bl	kfree
.L1883:
	mov	r0, r5
	ldmfd	sp, {r4, r5, r6, fp, sp, pc}
.L1891:
	ldrh	r2, [r4, #6]
	add	r1, r4, #8
	ldrh	r0, [r4, #4]
	bl	rk_sftl_vendor_read
	cmn	r0, #1
	beq	.L1892
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
	bne	.L1903
	mov	r0, r5
	mov	r1, r4
	bl	arm_copy_to_user
	subs	r5, r0, #0
	beq	.L1885
	b	.L1903
.L1887:
	ldr	r6, .L1908+4
	mov	r3, sp
	and	r3, r3, r6
	ldr	r3, [r3, #72]
	.syntax divided
@ 552 "./arch/arm/include/asm/uaccess.h" 1
	adds r2, r5, #8; sbcccs r2, r2, r3; movcc r3, #0
@ 0 "" 2
	.arm
	.syntax unified
	cmp	r3, #0
	bne	.L1893
	mov	r2, #8
	mov	r1, r5
	bl	arm_copy_from_user
	cmp	r0, #0
	bne	.L1903
	ldr	r2, [r4]
	ldr	r3, .L1908+8
	cmp	r2, r3
	bne	.L1892
	ldrh	r2, [r4, #6]
	movw	r3, #4087
	cmp	r2, r3
	bhi	.L1892
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
	bne	.L1907
	mov	r1, r5
	mov	r0, r4
	bl	arm_copy_from_user
	cmp	r0, #0
	bne	.L1903
	ldrh	r2, [r4, #6]
	add	r1, r4, #8
	ldrh	r0, [r4, #4]
	bl	rk_sftl_vendor_write
	mov	r5, r0
	b	.L1885
.L1893:
	mov	r1, #8
.L1907:
	bl	__memzero
	b	.L1903
.L1909:
	.align	2
.L1908:
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
	ldr	r0, .L1911
	bl	misc_register
	ldmfd	sp, {fp, sp, pc}
.L1912:
	.align	2
.L1911:
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
	.type	__func__.4010, %object
	.size	__func__.4010, 17
__func__.4010:
	.ascii	"INSERT_DATA_LIST\000"
	.type	__func__.4005, %object
	.size	__func__.4005, 17
__func__.4005:
	.ascii	"INSERT_FREE_LIST\000"
	.type	__func__.4048, %object
	.size	__func__.4048, 17
__func__.4048:
	.ascii	"List_remove_node\000"
	.type	__func__.4080, %object
	.size	__func__.4080, 22
__func__.4080:
	.ascii	"List_update_data_list\000"
	.type	__func__.4191, %object
	.size	__func__.4191, 22
__func__.4191:
	.ascii	"select_l2p_ram_region\000"
	.type	__func__.4514, %object
	.size	__func__.4514, 16
__func__.4514:
	.ascii	"make_superblock\000"
	.type	__func__.4689, %object
	.size	__func__.4689, 19
__func__.4689:
	.ascii	"get_new_active_ppa\000"
	.type	__func__.15027, %object
	.size	__func__.15027, 15
__func__.15027:
	.ascii	"FlashReadPages\000"
	.type	__func__.15044, %object
	.size	__func__.15044, 15
__func__.15044:
	.ascii	"FlashProgPages\000"
	.type	__func__.4122, %object
	.size	__func__.4122, 26
__func__.4122:
	.ascii	"ftl_map_blk_alloc_new_blk\000"
	.type	__func__.3541, %object
	.size	__func__.3541, 11
__func__.3541:
	.ascii	"FtlMemInit\000"
	.type	__func__.3845, %object
	.size	__func__.3845, 14
__func__.3845:
	.ascii	"FtlBbt2Bitmap\000"
	.type	__func__.4152, %object
	.size	__func__.4152, 31
__func__.4152:
	.ascii	"Ftl_write_map_blk_to_last_page\000"
	.type	__func__.4166, %object
	.size	__func__.4166, 16
__func__.4166:
	.ascii	"FtlMapWritePage\000"
	.type	__func__.4089, %object
	.size	__func__.4089, 16
__func__.4089:
	.ascii	"load_l2p_region\000"
	.type	__func__.4137, %object
	.size	__func__.4137, 15
__func__.4137:
	.ascii	"ftl_map_blk_gc\000"
	.type	__func__.4208, %object
	.size	__func__.4208, 9
__func__.4208:
	.ascii	"log2phys\000"
	.type	__func__.4423, %object
	.size	__func__.4423, 16
__func__.4423:
	.ascii	"FtlReUsePrevPpa\000"
	.type	__func__.4576, %object
	.size	__func__.4576, 14
__func__.4576:
	.ascii	"ftl_check_vpc\000"
	.type	__func__.4303, %object
	.size	__func__.4303, 14
__func__.4303:
	.ascii	"FtlScanSysBlk\000"
	.type	__func__.4535, %object
	.size	__func__.4535, 18
__func__.4535:
	.ascii	"SupperBlkListInit\000"
	.type	__func__.4661, %object
	.size	__func__.4661, 25
__func__.4661:
	.ascii	"allocate_data_superblock\000"
	.type	__func__.4702, %object
	.size	__func__.4702, 16
__func__.4702:
	.ascii	"update_vpc_list\000"
	.type	__func__.4709, %object
	.size	__func__.4709, 20
__func__.4709:
	.ascii	"decrement_vpc_count\000"
	.type	__func__.4457, %object
	.size	__func__.4457, 22
__func__.4457:
	.ascii	"FtlRecoverySuperblock\000"
	.type	__func__.3888, %object
	.size	__func__.3888, 11
__func__.3888:
	.ascii	"FtlLoadBbt\000"
	.type	__func__.4281, %object
	.size	__func__.4281, 15
__func__.4281:
	.ascii	"FtlVpcTblFlush\000"
	.type	__func__.4560, %object
	.size	__func__.4560, 21
__func__.4560:
	.ascii	"FtlVpcCheckAndModify\000"
	.type	__func__.4682, %object
	.size	__func__.4682, 29
__func__.4682:
	.ascii	"allocate_new_data_superblock\000"
	.type	__func__.3665, %object
	.size	__func__.3665, 13
__func__.3665:
	.ascii	"FtlProgPages\000"
	.type	__func__.4780, %object
	.size	__func__.4780, 19
__func__.4780:
	.ascii	"FtlGcFreeTempBlock\000"
	.type	__func__.4886, %object
	.size	__func__.4886, 23
__func__.4886:
	.ascii	"rk_ftl_garbage_collect\000"
	.type	__func__.3693, %object
	.size	__func__.3693, 9
__func__.3693:
	.ascii	"FtlWrite\000"
	.type	__func__.4360, %object
	.size	__func__.4360, 15
__func__.4360:
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
