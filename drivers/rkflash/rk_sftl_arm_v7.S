/* SPDX-License-Identifier: GPL-2.0 */

/*
 * Copyright (c) 2018 Rockchip Electronics Co. Ltd.
 * date: 2018-08-07
 */
	.file	"rk_sftl.c"
	.syntax divided
	.macro	it, cond
	.endm
	.macro	itt, cond
	.endm
	.macro	ite, cond
	.endm
	.macro	ittt, cond
	.endm
	.macro	itte, cond
	.endm
	.macro	itet, cond
	.endm
	.macro	itee, cond
	.endm
	.macro	itttt, cond
	.endm
	.macro	ittte, cond
	.endm
	.macro	ittet, cond
	.endm
	.macro	ittee, cond
	.endm
	.macro	itett, cond
	.endm
	.macro	itete, cond
	.endm
	.macro	iteet, cond
	.endm
	.macro	iteee, cond
	.endm

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
	ldr	r3, .L36
	ldr	r0, [r3, #140]
	ldmfd	sp, {fp, sp, pc}
.L37:
	.align	2
.L36:
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
	ldr	r4, .L39
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
	ldr	r0, .L39+4
	bl	sftl_printk
	ldrh	r3, [r4, #158]
	mov	r0, #0
	add	r3, r3, #1
	strh	r3, [r4, #158]	@ movhi
	ldmib	sp, {r4, r5, r6, r7, fp, sp, pc}
.L40:
	.align	2
.L39:
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
	ldr	r5, .L42
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
.L43:
	.align	2
.L42:
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
	ldr	r0, .L46
	mvn	r3, #0
	mov	r2, #16
	mov	r1, #255
	strh	r3, [r0, #152]	@ movhi
	mov	r3, #0
	strh	r3, [r0, #158]	@ movhi
	add	r0, r0, #164
	bl	memset
	ldmfd	sp, {fp, sp, pc}
.L47:
	.align	2
.L46:
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
	ldr	r3, .L55
	mov	r5, #0
	mov	r4, r5
	ldrh	r2, [r3, #98]
	ldrh	r6, [r3, #54]
	mul	r6, r6, r2
.L49:
	uxth	r0, r5
	cmp	r0, r6
	blt	.L51
	mov	r0, r4
	ldmfd	sp, {r4, r5, r6, fp, sp, pc}
.L51:
	bl	FtlBbmIsBadBlock
	cmp	r0, #0
	add	r5, r5, #1
	addne	r4, r4, #1
	uxthne	r4, r4
	b	.L49
.L56:
	.align	2
.L55:
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
	ldr	r4, .L58
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
.L59:
	.align	2
.L58:
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
	ldr	r3, .L61
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
.L62:
	.align	2
.L61:
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
	ldr	r4, .L64
	uxth	r0, r0
	ldrh	r1, [r4, #98]
	bl	__umodsi3
	ldrh	r1, [r4, #56]
	uxth	r0, r0
	bl	__udivsi3
	uxth	r0, r0
	ldmfd	sp, {r4, fp, sp, pc}
.L65:
	.align	2
.L64:
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
	bls	.L67
	sub	r0, r0, r1
	cmp	r0, #-2147483648
	movhi	r0, #0
	movls	r0, #1
	ldmfd	sp, {fp, sp, pc}
.L67:
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
	ldr	r3, .L70
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
.L71:
	.align	2
.L70:
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
	ldr	r3, .L73
	ldrh	r0, [r3, #218]
	clz	r0, r0
	lsr	r0, r0, #5
	ldmfd	sp, {fp, sp, pc}
.L74:
	.align	2
.L73:
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
	ldr	r3, .L76
	ldrh	r0, [r3, #218]
	sub	r0, r0, #1024
	clz	r0, r0
	lsr	r0, r0, #5
	ldmfd	sp, {fp, sp, pc}
.L77:
	.align	2
.L76:
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
	ldr	r3, .L87
	ldrh	r2, [r3, #218]
	cmp	r2, #0
	ldmfdeq	sp, {r4, fp, sp, pc}
	ldr	r2, .L87+4
	mov	r4, #0
	ldrh	r1, [r3, #214]
	mov	lr, r4
	ldrh	ip, [r2, #28]
	ldrh	r2, [r3, #216]
	and	ip, ip, #31
.L80:
	uxth	r0, r4
	add	r4, r4, #1
	cmp	ip, r0
	bgt	.L81
	cmp	lr, #0
	strhne	r1, [r3, #214]	@ movhi
	strhne	r2, [r3, #216]	@ movhi
	ldmfd	sp, {r4, fp, sp, pc}
.L81:
	add	r0, r3, r1, lsl #1
	add	r1, r1, #1
	ubfx	r1, r1, #0, #10
	ldrh	lr, [r0, #220]
	add	r0, r3, r2, lsl #1
	strh	lr, [r0, #220]	@ movhi
	mov	lr, #1
	add	r2, r2, lr
	ubfx	r2, r2, #0, #10
	b	.L80
.L88:
	.align	2
.L87:
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
	ldr	r3, .L95
	uxth	r0, r0
	ldrh	ip, [r3, #218]
	cmp	ip, #1024
	ldrhne	lr, [r3, #214]
	movne	r1, #0
	bne	.L91
.L93:
	mov	r0, #0
	ldmfd	sp, {fp, sp, pc}
.L92:
	add	r2, r1, lr
	ubfx	r2, r2, #0, #10
	add	r2, r3, r2, lsl #1
	ldrh	r2, [r2, #220]
	cmp	r2, r0
	beq	.L94
	add	r1, r1, #1
.L91:
	cmp	r1, ip
	bcc	.L92
	b	.L93
.L94:
	mov	r0, #1
	ldmfd	sp, {fp, sp, pc}
.L96:
	.align	2
.L95:
	.word	.LANCHOR0
	.size	IsInFreeQueue, .-IsInFreeQueue
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
	ldr	r1, .L113
	ldrh	r3, [r1, #40]
	mov	lr, r1
	cmp	r3, r0
	bls	.L99
	ldr	r6, [r1, #2316]
	lsl	r5, r0, #3
	mvn	ip, #0
	add	r2, r6, r5
	strh	ip, [r2, #2]	@ movhi
	strh	ip, [r6, r5]	@ movhi
	ldr	r3, [r1, #2320]
	cmp	r3, #0
	bne	.L100
.L112:
	str	r2, [r1, #2320]
	b	.L99
.L100:
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
.L108:
	ldr	r4, [fp, #-48]
	add	lr, lr, #1
	uxth	lr, lr
	cmp	lr, r4
	movls	r4, #0
	movhi	r4, #1
	cmp	r0, r1
	orreq	r4, r4, #1
	cmp	r4, #0
	bne	.L99
	lsl	r9, r1, #1
	ldrh	r4, [r7, r9]
	mov	ip, r4
	ldrh	r4, [r3, #4]
	cmp	r4, #0
	mulne	r4, r4, ip
	ldr	ip, [fp, #-44]
	mvneq	r4, #0
	cmp	ip, r4
	bne	.L104
	ldr	ip, [fp, #-52]
	ldrh	r9, [r8, r9]
	ldrh	r4, [ip]
	cmp	r9, r4
	bcc	.L106
.L105:
	strh	r1, [r6, r5]	@ movhi
	ldrh	r1, [r3, #2]
	strh	r1, [r2, #2]	@ movhi
	ldr	r1, .L113
	ldr	ip, [r1, #2320]
	cmp	r3, ip
	bne	.L109
	strh	r0, [r3, #2]	@ movhi
	b	.L112
.L104:
	bcc	.L105
.L106:
	ldrh	r4, [r3]
	movw	ip, #65535
	cmp	r4, ip
	bne	.L107
	strh	r1, [r2, #2]	@ movhi
	strh	r0, [r3]	@ movhi
	ldr	r3, .L113
	str	r2, [r3, #2332]
.L99:
	mov	r0, #0
	sub	sp, fp, #40
	ldmfd	sp, {r4, r5, r6, r7, r8, r9, r10, fp, sp, pc}
.L107:
	add	r3, r10, r4, lsl #3
	mov	r1, r4
	b	.L108
.L109:
	ldrh	r2, [r3, #2]
	ldr	r1, [r1, #2316]
	lsl	r2, r2, #3
	strh	r0, [r1, r2]	@ movhi
	strh	r0, [r3, #2]	@ movhi
	b	.L99
.L114:
	.align	2
.L113:
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
	ldr	r2, .L117
	add	r1, r2, #2336
	ldrh	r2, [r2, #40]
	ldrh	r3, [r1]
	add	r3, r3, #1
	uxth	r3, r3
	cmp	r2, r3
	strh	r3, [r1]	@ movhi
	ldmfdcs	sp, {fp, sp, pc}
	mov	r2, #221
	ldr	r1, .L117+4
	ldr	r0, .L117+8
	bl	sftl_printk
	ldmfd	sp, {fp, sp, pc}
.L118:
	.align	2
.L117:
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
	beq	.L120
	ldr	r2, .L126
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
	beq	.L120
	ldr	r8, [r2, #2328]
	lsl	r2, r0, #1
	ldr	r7, [r1, #2316]
	mov	r4, lr
	ldrh	r9, [r8, r2]
	sub	r2, r3, r7
	ubfx	r2, r2, #3, #16
.L124:
	lsl	lr, r2, #1
	ldrh	lr, [r8, lr]
	cmp	lr, r9
	bcs	.L122
	ldrh	lr, [r3]
	cmp	lr, r4
	bne	.L123
	strh	r2, [ip, #2]	@ movhi
	strh	r0, [r3]	@ movhi
.L120:
	mov	r0, #0
	ldmfd	sp, {r4, r5, r6, r7, r8, r9, fp, sp, pc}
.L123:
	add	r3, r7, lr, lsl #3
	mov	r2, lr
	b	.L124
.L122:
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
	b	.L120
.L127:
	.align	2
.L126:
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
	ldr	r2, .L130
	movw	r1, #2344
	ldrh	r3, [r2, r1]
	add	r3, r3, #1
	uxth	r3, r3
	strh	r3, [r2, r1]	@ movhi
	ldrh	r2, [r2, #40]
	cmp	r2, r3
	ldmfdcs	sp, {fp, sp, pc}
	mov	r2, #214
	ldr	r1, .L130+4
	ldr	r0, .L130+8
	bl	sftl_printk
	ldmfd	sp, {fp, sp, pc}
.L131:
	.align	2
.L130:
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
	ldr	r4, .L139
	uxth	r1, r1
	movw	r3, #65535
	mov	r8, r0
	ldr	r7, [r4, #2316]
	lsl	r6, r1, #3
	add	r5, r7, r6
	ldrh	r2, [r5, #2]
	cmp	r2, r3
	bne	.L133
	ldr	r3, [r0]
	cmp	r5, r3
	beq	.L133
	movw	r2, #379
	ldr	r1, .L139+4
	ldr	r0, .L139+8
	bl	sftl_printk
.L133:
	ldr	r3, [r8]
	ldrh	r2, [r7, r6]
	cmp	r5, r3
	movw	r3, #65535
	bne	.L134
	cmp	r2, r3
	ldrne	r3, [r4, #2316]
	moveq	r3, #0
	streq	r3, [r8]
	addne	r2, r3, r2, lsl #3
	mvnne	r3, #0
	strne	r2, [r8]
	strhne	r3, [r2, #2]	@ movhi
.L136:
	mvn	r3, #0
	mov	r0, #0
	strh	r3, [r7, r6]	@ movhi
	strh	r3, [r5, #2]	@ movhi
	ldmfd	sp, {r4, r5, r6, r7, r8, fp, sp, pc}
.L134:
	cmp	r2, r3
	ldrh	r3, [r5, #2]
	bne	.L137
	cmp	r3, r2
	ldrne	r2, [r4, #2316]
	lslne	r3, r3, #3
	mvnne	r1, #0
	beq	.L136
.L138:
	strh	r1, [r2, r3]	@ movhi
	b	.L136
.L137:
	ldr	r1, [r4, #2316]
	add	r2, r1, r2, lsl #3
	strh	r3, [r2, #2]	@ movhi
	ldrh	r3, [r5, #2]
	ldrh	r1, [r7, r6]
	ldr	r2, [r4, #2316]
	lsl	r3, r3, #3
	b	.L138
.L140:
	.align	2
.L139:
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
	beq	.L141
	ldr	r2, .L148
	movw	ip, #65535
	ldr	r4, [r2, #2316]
.L143:
	cmp	r1, #0
	bne	.L144
.L146:
	sub	r3, r3, r4
	ubfx	r4, r3, #3, #16
	mov	r1, r4
	bl	List_remove_node
.L141:
	mov	r0, r4
	ldmfd	sp, {r4, fp, sp, pc}
.L144:
	ldrh	r2, [r3]
	cmp	r2, ip
	beq	.L146
	sub	r1, r1, #1
	add	r3, r4, r2, lsl #3
	uxth	r1, r1
	b	.L143
.L149:
	.align	2
.L148:
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
	ldr	r1, .L157
	uxth	r2, r0
	ldr	r3, [r1, #2320]
	cmp	r3, #0
	ldrne	r1, [r1, #2316]
	movwne	r0, #65535
	bne	.L153
	movw	r0, #65535
	ldmfd	sp, {fp, sp, pc}
.L155:
	sub	r2, r2, #1
	add	r3, r1, r3, lsl #3
	uxth	r2, r2
.L153:
	cmp	r2, #0
	beq	.L154
	ldrh	r3, [r3]
	cmp	r3, r0
	bne	.L155
	ldmfd	sp, {fp, sp, pc}
.L154:
	sub	r3, r3, r1
	ubfx	r0, r3, #3, #16
	ldmfd	sp, {fp, sp, pc}
.L158:
	.align	2
.L157:
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
	ldr	r4, .L172
	movw	r3, #2348
	uxth	r5, r0
	ldrh	r3, [r4, r3]
	cmp	r3, r5
	beq	.L161
	movw	r3, #2396
	ldrh	r3, [r4, r3]
	cmp	r3, r5
	beq	.L161
	movw	r3, #2444
	ldrh	r3, [r4, r3]
	cmp	r3, r5
	beq	.L161
	ldr	r8, [r4, #2316]
	ldr	r3, [r4, #2320]
	add	r7, r8, r5, lsl #3
	cmp	r7, r3
	beq	.L161
	ldr	r2, [r4, #2324]
	lsl	r3, r5, #1
	ldrh	r6, [r7, #4]
	ldrh	r3, [r2, r3]
	cmp	r6, #0
	mvneq	r6, #0
	mulne	r6, r6, r3
	ldr	r3, [r8, r5, lsl #3]
	cmn	r3, #1
	bne	.L164
	movw	r2, #470
	ldr	r1, .L172+4
	ldr	r0, .L172+8
	bl	sftl_printk
.L164:
	ldr	r3, [r8, r5, lsl #3]
	cmn	r3, #1
	beq	.L161
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
	bcs	.L161
	ldr	r4, .L172+12
	mov	r1, r5
	ldr	r0, .L172+16
	bl	List_remove_node
	ldrh	r3, [r4]
	cmp	r3, #0
	bne	.L166
	movw	r2, #481
	ldr	r1, .L172+4
	ldr	r0, .L172+8
	bl	sftl_printk
.L166:
	ldrh	r3, [r4]
	mov	r0, r5
	sub	r3, r3, #1
	strh	r3, [r4]	@ movhi
	bl	INSERT_DATA_LIST
.L161:
	mov	r0, #0
	ldmfd	sp, {r4, r5, r6, r7, r8, fp, sp, pc}
.L173:
	.align	2
.L172:
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
	ldr	r3, .L184
	mov	r1, #0
	mov	r0, #12
	movw	ip, #65535
	ldrh	r2, [r3, #138]
	ldr	r3, [r3, #2492]
.L175:
	uxth	r4, r1
	cmp	r4, r2
	bcc	.L177
	mov	r4, r2
	mov	r1, #0
	mov	r0, #-2147483648
	mov	r5, #12
.L178:
	uxth	lr, r1
	cmp	lr, r2
	bcc	.L180
	cmp	r4, r2
	bcc	.L176
	ldr	r1, .L184+4
	mov	r4, r2
	mvn	r0, #0
	ldrh	r5, [r1]
	mov	r1, #0
.L181:
	uxth	ip, r1
	cmp	ip, r2
	bcc	.L183
	cmp	r4, r2
	bcc	.L176
	mov	r2, #784
	ldr	r1, .L184+8
	ldr	r0, .L184+12
	bl	sftl_printk
	b	.L176
.L177:
	add	r1, r1, #1
	mla	lr, r0, r1, r3
	ldrh	lr, [lr, #-12]
	cmp	lr, ip
	bne	.L175
.L176:
	mov	r0, r4
	ldmfd	sp, {r4, r5, r6, fp, sp, pc}
.L180:
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
	b	.L178
.L183:
	ldr	lr, [r3, #4]
	cmp	r0, lr
	bls	.L182
	ldrh	r6, [r3]
	cmp	r6, r5
	movne	r0, lr
	movne	r4, ip
.L182:
	add	r1, r1, #1
	add	r3, r3, #12
	b	.L181
.L185:
	.align	2
.L184:
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
	ldr	ip, .L192
	movw	r1, #2498
	ldrh	r2, [ip, r1]
	mov	r3, ip
	cmp	r2, #4
	cmpls	r0, #0
	bne	.L187
	add	r2, r2, #1
	strh	r2, [ip, r1]	@ movhi
	ldmfd	sp, {fp, sp, pc}
.L187:
	mov	r2, #0
	movw	lr, #65535
	strh	r2, [ip, r1]	@ movhi
	str	r2, [ip, #2500]
	ldrh	r1, [ip, #40]
	ldr	r2, [ip, #2324]
	add	r1, r2, r1, lsl #1
.L188:
	cmp	r2, r1
	bne	.L190
	ldmfd	sp, {fp, sp, pc}
.L190:
	ldrh	ip, [r2], #2
	cmp	ip, lr
	ldrne	r0, [r3, #2500]
	addne	r0, r0, ip
	strne	r0, [r3, #2500]
	b	.L188
.L193:
	.align	2
.L192:
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
	beq	.L195
	bl	ftl_set_blk_mode.part.6
	ldmfd	sp, {fp, sp, pc}
.L195:
	ldr	r3, .L197
	lsr	r1, r0, #5
	mov	ip, #1
	and	r0, r0, #31
	ldr	r2, [r3, #24]
	ldr	r3, [r2, r1, lsl #2]
	bic	r0, r3, ip, lsl r0
	str	r0, [r2, r1, lsl #2]
	ldmfd	sp, {fp, sp, pc}
.L198:
	.align	2
.L197:
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
	ldr	r3, .L200
	uxth	r0, r0
	ldr	r3, [r3, #24]
	lsr	r2, r0, #5
	and	r0, r0, #31
	ldr	r3, [r3, r2, lsl #2]
	lsr	r0, r3, r0
	and	r0, r0, #1
	ldmfd	sp, {fp, sp, pc}
.L201:
	.align	2
.L200:
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
	ldr	r3, .L209
	add	ip, r0, r2, lsl #1
	movw	r1, #65535
	add	ip, ip, #14
	ldrh	lr, [r3, #32]
.L203:
	cmp	r2, lr
	bcc	.L205
	ldrh	r1, [r3, #102]
	add	ip, r0, #16
	mov	r3, #0
	movw	r4, #65535
	sub	r1, r1, #1
	sub	r1, r1, r5
	uxth	r1, r1
.L206:
	uxth	r2, r3
	cmp	lr, r2
	bhi	.L208
	ldmfd	sp, {r4, r5, fp, sp, pc}
.L205:
	ldrh	r4, [ip, #2]!
	add	r2, r2, #1
	uxth	r2, r2
	cmp	r4, r1
	ldrhne	r4, [r0, #4]
	addne	r4, r4, #1
	strhne	r4, [r0, #4]	@ movhi
	b	.L203
.L208:
	ldrh	r2, [ip], #2
	add	r3, r3, #1
	cmp	r2, r4
	ldrhne	r2, [r0, #4]
	addne	r2, r1, r2
	strhne	r2, [r0, #4]	@ movhi
	b	.L206
.L210:
	.align	2
.L209:
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
	ldr	r2, .L217
	ldrh	ip, [r2, #32]
	mov	r2, r3
.L214:
	cmp	r1, r2
	beq	.L216
	ldmfd	sp, {fp, sp, pc}
.L216:
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
	b	.L214
.L218:
	.align	2
.L217:
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
	ldr	r6, .L224
	ldrh	r2, [r0]
	ldrh	r3, [r6, #40]
	cmp	r2, r3
	bcc	.L220
	movw	r2, #2097
	ldr	r1, .L224+4
	ldr	r0, .L224+8
	bl	sftl_printk
.L220:
	ldrh	r9, [r6, #32]
	add	r7, r4, #16
	ldr	r10, .L224+12
	mvn	r8, #0
	mov	r5, #0
	strh	r5, [r4, #4]	@ movhi
	strb	r5, [r4, #7]
.L221:
	uxth	r3, r5
	cmp	r9, r3
	bhi	.L223
	ldrb	r3, [r4, #7]	@ zero_extendqisi2
	mov	r0, #0
	ldrh	r2, [r6, #102]
	smulbb	r3, r3, r2
	strh	r3, [r4, #4]	@ movhi
	mov	r3, #1
	strb	r3, [r4, #9]
	ldmib	sp, {r4, r5, r6, r7, r8, r9, r10, fp, sp, pc}
.L223:
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
	b	.L221
.L225:
	.align	2
.L224:
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
	ldr	r3, .L232
	mov	r5, #0
	uxth	r6, r0
	mov	r4, r5
	ldrh	r8, [r3, #32]
	mov	r7, r3
	ldrh	r9, [r3, #102]
	add	r10, r3, #60
.L227:
	uxth	r3, r5
	cmp	r8, r3
	bhi	.L229
	cmp	r4, #0
	moveq	r0, r4
	beq	.L230
	mov	r1, r4
	mov	r0, #32768
	bl	__divsi3
.L230:
	ldr	r1, [r7, #2316]
	add	r1, r1, r6, lsl #3
	strh	r0, [r1, #4]	@ movhi
	mov	r0, #0
	ldmfd	sp, {r4, r5, r6, r7, r8, r9, r10, fp, sp, pc}
.L229:
	mov	r1, r6
	ldrb	r0, [r10, r5]	@ zero_extendqisi2
	bl	V2P_block
	bl	FtlBbmIsBadBlock
	cmp	r0, #0
	add	r5, r5, #1
	addeq	r4, r4, r9
	uxtheq	r4, r4
	b	.L227
.L233:
	.align	2
.L232:
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
	ldr	r3, .L237
	ldr	r0, [r3, #2340]
	cmp	r0, #0
	ldrne	r2, [r3, #2316]
	ldrne	r3, [r3, #2328]
	subne	r0, r0, r2
	ubfxne	r0, r0, #3, #16
	lslne	r0, r0, #1
	ldrhne	r0, [r3, r0]
	ldmfd	sp, {fp, sp, pc}
.L238:
	.align	2
.L237:
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
	ldr	r2, .L247
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
.L242:
	uxth	r0, r1
	cmp	r4, r0
	bls	.L244
	lsl	r0, r3, #3
	add	r1, r1, #1
	ldrh	lr, [ip, r0]
	cmp	lr, r5
	bne	.L246
.L244:
	ldr	r2, [r2, #2328]
	lsl	r3, r3, #1
	ldrh	r0, [r2, r3]
	ldmfd	sp, {r4, r5, fp, sp, pc}
.L246:
	mov	r3, lr
	b	.L242
.L248:
	.align	2
.L247:
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
	ldr	r6, .L261
	add	r5, r7, #12
	ldr	r1, .L261+4
	movw	r9, #2344
	bl	strcpy
	ldr	r2, [r6, #116]
	mov	r0, r5
	ldr	r1, .L261+8
	bl	sprintf
	add	r5, r5, r0
	ldr	r1, .L261+12
	mov	r0, r5
	add	r5, r5, #10
	bl	strcpy
	ldr	r2, [r6, #2504]
	mov	r0, r5
	ldr	r1, .L261+16
	bl	sprintf
	add	r5, r5, r0
	ldr	r2, [r6, #2500]
	ldr	r1, .L261+20
	mov	r0, r5
	bl	sprintf
	add	r5, r5, r0
	ldr	r2, [r6, #2508]
	ldr	r1, .L261+24
	mov	r0, r5
	bl	sprintf
	add	r5, r5, r0
	ldr	r2, [r6, #2512]
	ldr	r1, .L261+28
	mov	r0, r5
	bl	sprintf
	add	r5, r5, r0
	ldr	r2, [r6, #2516]
	ldr	r1, .L261+32
	mov	r0, r5
	bl	sprintf
	add	r5, r5, r0
	ldr	r2, [r6, #2520]
	ldr	r1, .L261+36
	mov	r0, r5
	bl	sprintf
	add	r5, r5, r0
	ldr	r2, [r6, #2524]
	ldr	r1, .L261+40
	mov	r0, r5
	bl	sprintf
	add	r5, r5, r0
	ldr	r2, [r6, #2528]
	ldr	r1, .L261+44
	mov	r0, r5
	bl	sprintf
	ldr	r2, [r6, #2532]
	add	r5, r5, r0
	ldr	r1, .L261+48
	mov	r0, r5
	ldr	r4, .L261+52
	ldr	r8, .L261+56
	lsr	r2, r2, #11
	bl	sprintf
	ldr	r2, [r6, #2536]
	add	r5, r5, r0
	ldr	r1, .L261+60
	mov	r0, r5
	lsr	r2, r2, #11
	bl	sprintf
	add	r5, r5, r0
	ldr	r2, [r6, #2540]
	ldr	r1, .L261+64
	mov	r0, r5
	bl	sprintf
	add	r5, r5, r0
	ldr	r2, [r6, #2544]
	ldr	r1, .L261+68
	mov	r0, r5
	bl	sprintf
	add	r5, r5, r0
	ldrh	r2, [r6, #158]
	ldr	r1, .L261+72
	mov	r0, r5
	bl	sprintf
	add	r5, r5, r0
	ldrh	r2, [r6, r9]
	ldr	r1, .L261+76
	mov	r0, r5
	bl	sprintf
	add	r5, r5, r0
	ldr	r2, [r6, #2548]
	ldr	r1, .L261+80
	mov	r0, r5
	bl	sprintf
	add	r5, r5, r0
	ldr	r2, [r6, #2552]
	ldr	r1, .L261+84
	mov	r0, r5
	bl	sprintf
	add	r5, r5, r0
	ldr	r2, [r6, #2556]
	ldr	r1, .L261+88
	mov	r0, r5
	bl	sprintf
	add	r5, r5, r0
	ldr	r2, [r6, #2560]
	ldr	r1, .L261+92
	mov	r0, r5
	bl	sprintf
	add	r5, r5, r0
	ldr	r2, [r6, #2564]
	ldr	r1, .L261+96
	mov	r0, r5
	bl	sprintf
	add	r5, r5, r0
	ldr	r2, [r6, #2568]
	ldr	r1, .L261+100
	mov	r0, r5
	bl	sprintf
	add	r5, r5, r0
	ldrh	r2, [r4, #30]
	ldr	r1, .L261+104
	mov	r0, r5
	bl	sprintf
	add	r5, r5, r0
	ldrh	r2, [r4, #28]
	ldr	r1, .L261+108
	mov	r0, r5
	bl	sprintf
	add	r5, r5, r0
	ldr	r2, [r6, #140]
	ldr	r1, .L261+112
	mov	r0, r5
	bl	sprintf
	add	r5, r5, r0
	ldr	r2, [r6, #132]
	ldr	r1, .L261+116
	mov	r0, r5
	bl	sprintf
	add	r5, r5, r0
	ldr	r2, [r6, #28]
	ldr	r1, .L261+120
	mov	r0, r5
	bl	sprintf
	add	r5, r5, r0
	ldrh	r2, [r6, #218]
	ldr	r1, .L261+124
	mov	r0, r5
	bl	sprintf
	add	r5, r5, r0
	ldrh	r2, [r6, #40]
	ldr	r1, .L261+128
	mov	r0, r5
	bl	sprintf
	movw	r3, #2572
	add	r5, r5, r0
	ldrh	r2, [r6, r3]
	mov	r0, r5
	ldr	r1, .L261+132
	movw	r4, #2348
	bl	sprintf
	add	r5, r5, r0
	ldr	r2, [r6, #44]
	ldr	r1, .L261+136
	mov	r0, r5
	bl	sprintf
	add	r3, r6, #2576
	add	r5, r5, r0
	ldrh	r2, [r3]
	mov	r0, r5
	ldr	r1, .L261+140
	bl	sprintf
	add	r5, r5, r0
	ldrh	r2, [r6, #152]
	ldr	r1, .L261+144
	mov	r0, r5
	bl	sprintf
	add	r5, r5, r0
	ldrh	r2, [r8, #2]
	ldr	r1, .L261+148
	mov	r0, r5
	bl	sprintf
	add	r5, r5, r0
	ldrb	r2, [r6, #2354]	@ zero_extendqisi2
	ldr	r1, .L261+152
	mov	r0, r5
	bl	sprintf
	add	r5, r5, r0
	ldrh	r2, [r6, r4]
	ldr	r1, .L261+156
	mov	r0, r5
	bl	sprintf
	add	r5, r5, r0
	ldrb	r2, [r6, #2356]	@ zero_extendqisi2
	ldr	r1, .L261+160
	mov	r0, r5
	bl	sprintf
	add	r5, r5, r0
	ldrh	r2, [r8, #4]
	ldr	r1, .L261+164
	mov	r0, r5
	bl	sprintf
	ldrh	r3, [r6, r4]
	add	r5, r5, r0
	ldr	r2, [r6, #2324]
	mov	r0, r5
	ldr	r1, .L261+168
	movw	r4, #2396
	lsl	r3, r3, #1
	ldrh	r2, [r2, r3]
	bl	sprintf
	add	r5, r5, r0
	ldrh	r2, [r8, #50]
	ldr	r1, .L261+172
	mov	r0, r5
	bl	sprintf
	add	r5, r5, r0
	ldrb	r2, [r6, #2402]	@ zero_extendqisi2
	ldr	r1, .L261+176
	mov	r0, r5
	bl	sprintf
	add	r5, r5, r0
	ldrh	r2, [r6, r4]
	ldr	r1, .L261+180
	mov	r0, r5
	bl	sprintf
	add	r5, r5, r0
	ldrb	r2, [r6, #2404]	@ zero_extendqisi2
	ldr	r1, .L261+184
	mov	r0, r5
	bl	sprintf
	add	r5, r5, r0
	ldrh	r2, [r8, #52]
	ldr	r1, .L261+188
	mov	r0, r5
	bl	sprintf
	ldrh	r3, [r6, r4]
	add	r5, r5, r0
	ldr	r2, [r6, #2324]
	mov	r0, r5
	ldr	r1, .L261+192
	add	r4, r8, #96
	lsl	r3, r3, #1
	ldrh	r2, [r2, r3]
	bl	sprintf
	add	r5, r5, r0
	ldrh	r2, [r8, #98]
	ldr	r1, .L261+196
	mov	r0, r5
	bl	sprintf
	add	r5, r5, r0
	ldrb	r2, [r6, #2450]	@ zero_extendqisi2
	ldr	r1, .L261+200
	mov	r0, r5
	bl	sprintf
	movw	r3, #2444
	add	r5, r5, r0
	ldrh	r2, [r6, r3]
	mov	r0, r5
	ldr	r1, .L261+204
	bl	sprintf
	add	r5, r5, r0
	ldrb	r2, [r6, #2452]	@ zero_extendqisi2
	ldr	r1, .L261+208
	mov	r0, r5
	bl	sprintf
	add	r5, r5, r0
	ldrh	r2, [r8, #100]
	ldr	r1, .L261+212
	mov	r0, r5
	bl	sprintf
	add	r5, r5, r0
	ldrh	r2, [r4, #146]
	ldr	r1, .L261+216
	mov	r0, r5
	bl	sprintf
	add	r5, r5, r0
	ldrb	r2, [r6, #2594]	@ zero_extendqisi2
	ldr	r1, .L261+220
	mov	r0, r5
	bl	sprintf
	movw	r8, #2588
	add	r5, r5, r0
	ldrh	r2, [r6, r8]
	mov	r0, r5
	ldr	r1, .L261+224
	bl	sprintf
	add	r5, r5, r0
	ldrb	r2, [r6, #2596]	@ zero_extendqisi2
	ldr	r1, .L261+228
	mov	r0, r5
	bl	sprintf
	add	r5, r5, r0
	ldrh	r2, [r4, #148]
	ldr	r1, .L261+232
	mov	r0, r5
	bl	sprintf
	ldr	r3, [r6, #2712]
	add	r5, r5, r0
	ldr	r2, [r6, #2716]
	mov	r0, r5
	ldr	r1, .L261+236
	str	r3, [sp]
	ldr	r3, [r6, #2720]
	bl	sprintf
	add	r4, r5, r0
	ldr	r2, [r6, #2708]
	ldr	r1, .L261+240
	mov	r0, r4
	bl	sprintf
	add	r4, r4, r0
	ldr	r2, [r6, #2732]
	ldr	r1, .L261+244
	mov	r0, r4
	bl	sprintf
	movw	r3, #3148
	add	r4, r4, r0
	ldrh	r2, [r6, r3]
	mov	r0, r4
	ldr	r1, .L261+248
	bl	sprintf
	movw	r3, #3150
	add	r4, r4, r0
	ldrh	r2, [r6, r3]
	mov	r0, r4
	ldr	r1, .L261+252
	bl	sprintf
	add	r4, r4, r0
	ldr	r2, [r6, #3152]
	ldr	r1, .L261+256
	mov	r0, r4
	bl	sprintf
	movw	r3, #3156
	add	r4, r4, r0
	ldrh	r2, [r6, r3]
	mov	r0, r4
	ldr	r1, .L261+260
	bl	sprintf
	add	r4, r4, r0
	bl	GetFreeBlockMinEraseCount
	ldr	r1, .L261+264
	mov	r2, r0
	mov	r0, r4
	bl	sprintf
	add	r4, r4, r0
	ldrh	r0, [r6, r9]
	bl	GetFreeBlockMaxEraseCount
	ldr	r1, .L261+268
	mov	r2, r0
	mov	r0, r4
	bl	sprintf
	ldr	r3, .L261+272
	add	r4, r4, r0
	ldr	r3, [r3]
	cmp	r3, #1
	beq	.L250
.L255:
	sub	r0, r4, r7
	sub	sp, fp, #40
	ldmfd	sp, {r4, r5, r6, r7, r8, r9, r10, fp, sp, pc}
.L250:
	ldrh	r3, [r6, r8]
	movw	r2, #65535
	cmp	r3, r2
	beq	.L252
	ldr	r2, [r6, #2324]
	lsl	r3, r3, #1
	mov	r0, r4
	ldr	r1, .L261+276
	ldrh	r2, [r2, r3]
	bl	sprintf
	add	r4, r4, r0
.L252:
	mov	r0, #0
	ldr	r9, .L261+280
	bl	List_get_gc_head_node
	uxth	r3, r0
	mov	r5, #0
	movw	r10, #65535
.L254:
	cmp	r3, r10
	beq	.L253
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
	bne	.L254
.L253:
	ldr	r3, [r6, #2340]
	mov	r5, #0
	ldr	r2, [r6, #2316]
	movw	r9, #65535
	ldr	r10, .L261+284
	sub	r3, r3, r2
	ubfx	r3, r3, #3, #16
.L256:
	cmp	r3, r9
	beq	.L255
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
	beq	.L255
	ldr	r3, [r6, #2316]
	ldrh	r3, [r3, r8]
	b	.L256
.L262:
	.align	2
.L261:
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
	ldr	r2, .L264
	mov	r5, r0
	ldr	r1, .L264+4
	bl	sprintf
	add	r4, r5, r0
	mov	r0, r4
	bl	FtlPrintInfo2buf
	add	r0, r4, r0
	sub	r0, r0, r5
	ldmfd	sp, {r4, r5, fp, sp, pc}
.L265:
	.align	2
.L264:
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
	ldr	r4, .L290
	ldr	r2, [r4, #2556]
	ldr	r3, [r4, #2568]
	cmp	r2, r3
	bcs	.L267
	ldr	r2, [r4, #2328]
	mov	r3, #0
	ldrh	r1, [r4, #40]
	str	r3, [r4, #2548]
	sub	r2, r2, #2
.L268:
	cmp	r3, r1
	bcc	.L269
	ldr	r5, [r4, #2548]
	mov	r0, r5
	bl	__udivsi3
	str	r0, [r4, #2556]
	ldr	r0, [r4, #2552]
	ldrh	r1, [r4, #92]
	sub	r0, r5, r0
	bl	__udivsi3
	str	r0, [r4, #2548]
.L270:
	ldr	r6, [r4, #2568]
	ldr	r7, [r4, #2556]
	add	r3, r6, #256
	cmp	r3, r7
	bls	.L275
	ldr	r2, [r4, #2564]
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
	ldr	ip, [r4, #2548]
	add	r0, r0, ip
	str	r0, [r4, #2548]
	b	.L268
.L267:
	ldr	r3, [r4, #2564]
	cmp	r2, r3
	addhi	r3, r3, #1
	strhi	r3, [r4, #2564]
	movhi	r3, #0
	bls	.L270
.L272:
	ldrh	r2, [r4, #40]
	cmp	r3, r2
	bcs	.L270
	ldr	r0, [r4, #2328]
	lsl	r1, r3, #1
	add	r3, r3, #1
	ldrh	r2, [r0, r1]
	add	r2, r2, #1
	strh	r2, [r0, r1]	@ movhi
	b	.L272
.L275:
	movw	r3, #2344
	ldrh	r0, [r4, r3]
	add	r0, r0, r0, lsl #1
	asr	r0, r0, #2
	bl	GetFreeBlockMaxEraseCount
	add	r3, r6, #64
	mov	r9, r0
	cmp	r0, r3
	bcc	.L277
	ldr	r3, [r4, #2320]
	cmp	r3, #0
	beq	.L277
	ldrh	r10, [r4, #40]
	movw	r1, #65535
	ldr	lr, [r4, #2316]
	mov	r0, #0
	ldr	r8, [r4, #2328]
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
	strcc	r1, [r4, #2568]
.L282:
	cmp	r7, r10
	bls	.L277
	add	r2, r10, #128
	cmp	r9, r2
	ble	.L277
	add	r2, r10, #256
	cmp	r7, r2
	bhi	.L283
	ldr	r2, [r4, #2564]
	add	r10, r10, #768
	cmp	r10, r2
	bcs	.L277
.L283:
	str	r9, [sp, #8]
	mov	r1, r5
	ldrh	r2, [r8, r3]
	ldr	r0, .L290+4
	str	r2, [sp, #4]
	ldr	r2, [r4, #2324]
	ldrh	r3, [r2, r3]
	mov	r2, r7
	str	r3, [sp]
	ldr	r3, [r4, #2564]
	bl	sftl_printk
	mov	r3, #1
	str	r3, [r4, #3160]
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
	beq	.L293
	ldr	r3, .L294
	mov	r1, #0
	ldr	r2, [r3, #2324]
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
	movw	r2, #2707
	ldr	r1, .L310
	ldr	r0, .L310+4
	bl	sftl_printk
.L297:
	ldr	r5, .L310+8
	ldrh	r2, [r4, #2]
	ldrh	r3, [r5, #102]
	cmp	r2, r3
	bne	.L298
	movw	r2, #2708
	ldr	r1, .L310
	ldr	r0, .L310+4
	bl	sftl_printk
.L298:
	ldrh	r3, [r4, #4]
	cmp	r3, #0
	bne	.L299
	movw	r2, #2709
	ldr	r1, .L310
	ldr	r0, .L310+4
	bl	sftl_printk
.L299:
	ldrb	r2, [r4, #6]	@ zero_extendqisi2
	mov	r3, #0
	ldrh	r1, [r5, #32]
	movw	ip, #65535
	strb	r3, [r4, #10]
	add	r2, r4, r2, lsl #1
	ldrh	r0, [r2, #16]
	mov	r2, r3
.L300:
	cmp	r0, ip
	ldrb	r3, [r4, #6]	@ zero_extendqisi2
	beq	.L302
	ldrh	r2, [r4, #4]
	ldrh	r6, [r4, #2]
	sub	r2, r2, #1
	uxth	r2, r2
	orr	r6, r6, r0, lsl #10
	movw	r0, #65535
	strh	r2, [r4, #4]	@ movhi
.L304:
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
	beq	.L304
	strb	r3, [r4, #6]
	cmp	r2, #0
	ldrh	r3, [r4, #2]
	ldrh	r1, [r5, #102]
	sub	r3, r3, r1
	clz	r3, r3
	lsr	r3, r3, #5
	moveq	r3, #0
	cmp	r3, #0
	beq	.L296
	movw	r2, #2730
	ldr	r1, .L310
	ldr	r0, .L310+4
	bl	sftl_printk
.L296:
	mov	r0, r6
	ldmfd	sp, {r4, r5, r6, fp, sp, pc}
.L302:
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
	b	.L300
.L311:
	.align	2
.L310:
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
	ldr	r3, .L317
	mov	lr, #12
	mov	r4, #1
	mov	r5, #20
	mov	r2, #0
	str	r2, [r3, #3164]
.L313:
	ldrh	r1, [r3, #32]
	uxth	r0, r2
	add	ip, r2, #1
	cmp	r0, r1
	bcc	.L314
	mov	ip, #12
	mov	lr, #0
.L315:
	ldr	r2, [r3, #3184]
	cmp	r1, r2
	bcc	.L316
	ldmfd	sp, {r4, r5, r6, r7, r8, fp, sp, pc}
.L314:
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
	b	.L313
.L316:
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
	b	.L315
.L318:
	.align	2
.L317:
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
	ldr	r3, .L326
	mov	lr, #0
	mov	r5, #20
	mov	r7, #12
	mov	r8, lr
	ldr	r6, [r3, #3184]
	ldr	r4, [r3, #3168]
.L320:
	uxth	r3, lr
	cmp	r1, r3
	ldmfdls	sp, {r4, r5, r6, r7, r8, r9, r10, fp, sp, pc}
	mla	ip, r5, r3, r0
	mov	r2, #0
.L321:
	uxth	r3, r2
	cmp	r6, r3
	bls	.L322
	mul	r3, r7, r3
	add	r2, r2, #1
	ldr	r10, [r4, r3]
	add	r9, r4, r3
	ldr	r3, [ip, #8]
	cmp	r10, r3
	bne	.L321
	str	r8, [r9, #8]
.L322:
	add	lr, lr, #1
	b	.L320
.L327:
	.align	2
.L326:
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
	ldr	r3, .L335
	mov	ip, #0
	mov	r6, #12
	mov	r7, #1
	mov	r8, #20
	ldr	r4, [r3, #3184]
	ldr	r5, [r3, #3168]
.L329:
	uxth	r2, ip
	cmp	r1, r2
	bhi	.L333
	ldmfd	sp, {r4, r5, r6, r7, r8, r9, fp, sp, pc}
.L333:
	mov	lr, #0
.L330:
	uxth	r3, lr
	cmp	r4, r3
	bls	.L331
	mla	r3, r6, r3, r5
	add	lr, lr, #1
	ldr	r9, [r3, #8]
	cmp	r9, #0
	bne	.L330
	mla	r2, r8, r2, r0
	ldr	lr, [r3]
	str	r7, [r3, #8]
	str	lr, [r2, #8]
	ldr	r3, [r3, #4]
	str	r3, [r2, #12]
.L331:
	add	ip, ip, #1
	b	.L329
.L336:
	.align	2
.L335:
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
	ldr	r2, .L342
	movw	r1, #3192
	uxth	r0, r0
	ldr	r3, [r2, #3188]
	ldrh	r2, [r2, r1]
	add	r2, r3, r2, lsl #1
.L338:
	cmp	r3, r2
	bne	.L340
	mov	r0, #0
	ldmfd	sp, {fp, sp, pc}
.L340:
	ldrh	r1, [r3], #2
	cmp	r1, r0
	bne	.L338
	mov	r0, #1
	ldmfd	sp, {fp, sp, pc}
.L343:
	.align	2
.L342:
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
	ldr	ip, .L348
	movw	r3, #3192
	ldr	r2, [ip, #3188]
	ldrh	lr, [ip, r3]
	mov	r3, #0
	sub	r1, r2, #2
.L345:
	uxth	r7, r3
	cmp	r7, lr
	bcc	.L347
	bne	.L346
	lsl	r3, r7, #1
	strh	r0, [r2, r3]	@ movhi
	movw	r2, #3192
	ldrh	r3, [ip, r2]
	add	r3, r3, #1
	strh	r3, [ip, r2]	@ movhi
	b	.L346
.L347:
	ldrh	r7, [r1, #2]!
	add	r3, r3, #1
	cmp	r7, r0
	bne	.L345
.L346:
	ldr	r0, .L348+4
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
.L349:
	.align	2
.L348:
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
	push	{r4, fp, ip, lr, pc}
	sub	fp, ip, #4
	uxth	r4, r0
	ldr	r0, .L353
	mov	r1, r4
	bl	sftl_printk
	ldr	r3, .L353+4
	movw	ip, #3202
	ldrh	lr, [r3, ip]
	cmp	r4, lr
	beq	.L351
	movw	r2, #3204
	ldrh	r0, [r3, r2]
	cmp	r4, r0
	beq	.L351
	movw	r1, #65535
	cmp	lr, r1
	strheq	r4, [r3, ip]	@ movhi
	beq	.L351
	cmp	r0, r1
	strheq	r4, [r3, r2]	@ movhi
.L351:
	mov	r0, #0
	ldmfd	sp, {r4, fp, sp, pc}
.L354:
	.align	2
.L353:
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
	ldr	r4, .L359
	uxth	r5, r0
	movw	r6, #3206
	mov	r0, r5
	bl	P2V_block_in_plane
	mov	r2, r5
	mov	r7, r0
	ldrh	r1, [r4, r6]
	ldr	r0, .L359+4
	bl	sftl_printk
	mov	r0, r7
	bl	FtlGcRefreshBlock
	ldrh	r3, [r4, r6]
	mov	r2, #0
	ldr	r0, .L359+8
.L356:
	uxth	r1, r2
	cmp	r3, r1
	bhi	.L358
	cmp	r3, #15
	movwls	r2, #3206
	addls	r1, r3, #1
	strhls	r1, [r4, r2]	@ movhi
	lslls	r3, r3, #1
	ldrls	r2, .L359+8
	strhls	r5, [r2, r3]	@ movhi
	b	.L357
.L358:
	add	r2, r2, #1
	add	r1, r0, r2, lsl #1
	ldrh	r1, [r1, #-2]
	cmp	r1, r5
	bne	.L356
.L357:
	mov	r0, #0
	ldmfd	sp, {r4, r5, r6, r7, fp, sp, pc}
.L360:
	.align	2
.L359:
	.word	.LANCHOR0
	.word	.LC74
	.word	.LANCHOR0+3208
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
	ldr	r4, .L367
	movw	r3, #3206
	ldrh	r2, [r4, r3]
	cmp	r2, #0
	beq	.L362
	movw	r3, #3202
	ldrh	r1, [r4, r3]
	movw	r3, #65535
	cmp	r1, r3
	bne	.L362
	movw	r3, #3242
	movw	r5, #3242
	ldrh	r1, [r4, r3]
	cmp	r1, r2
	movcs	r2, #0
	strhcs	r2, [r4, r3]	@ movhi
	ldrh	r3, [r4, r5]
	ldr	r2, .L367+4
	lsl	r3, r3, #1
	ldrh	r0, [r2, r3]
	bl	P2V_block_in_plane
	bl	FtlGcRefreshBlock
	ldrh	r3, [r4, r5]
	add	r3, r3, #1
	strh	r3, [r4, r5]	@ movhi
.L362:
	mov	r0, #0
	ldmfd	sp, {r4, r5, fp, sp, pc}
.L368:
	.align	2
.L367:
	.word	.LANCHOR0
	.word	.LANCHOR0+3208
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
	ldr	r1, .L370
	bl	__kmalloc
	ldmfd	sp, {fp, sp, pc}
.L371:
	.align	2
.L370:
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
.L374:
	cmp	r4, r9
	bcc	.L380
	ldr	r0, .L382
	bl	sftl_printk
	ldmfd	sp, {r4, r5, r6, r7, r8, r9, r10, fp, sp, pc}
.L380:
	cmp	r5, #0
	bne	.L375
	mov	r3, r6
	mov	r2, r7
	mov	r1, r10
	ldr	r0, .L382+4
	bl	sftl_printk
.L375:
	cmp	r8, #4
	ldreq	r1, [r7, r4, lsl #2]
	ldreq	r0, .L382+8
	beq	.L381
	cmp	r8, #2
	lsleq	r3, r4, #1
	ldreq	r0, .L382+12
	ldrbne	r1, [r7, r4]	@ zero_extendqisi2
	ldrheq	r1, [r7, r3]
	ldrne	r0, .L382+16
.L381:
	add	r5, r5, #1
	bl	sftl_printk
	cmp	r5, #15
	bls	.L379
	mov	r5, #0
	ldr	r0, .L382
	bl	sftl_printk
.L379:
	add	r4, r4, #1
	add	r6, r6, r8
	b	.L374
.L383:
	.align	2
.L382:
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
	ldr	r5, .L402
	mov	r7, r1
	ldr	r9, .L402+4
	mov	r4, r0
	ldr	r10, .L402+8
	ldrh	r8, [r5, #12]
.L385:
	cmp	r6, r7
	bne	.L392
	mov	r0, #0
	sub	sp, fp, #40
	ldmfd	sp, {r4, r5, r6, r7, r8, r9, r10, fp, sp, pc}
.L392:
	ldr	r3, [r4, #8]
	cmp	r3, #0
	beq	.L386
	ldr	r3, [r4, #12]
	cmp	r3, #0
	bne	.L387
.L386:
	mov	r2, #101
	mov	r1, r9
	mov	r0, r10
	bl	sftl_printk
.L387:
	sub	r2, fp, #48
	sub	r1, fp, #44
	mov	r0, r4
	bl	l2p_addr_tran
	ldr	r3, [r4, #12]
	ldr	ip, [r5, #3256]
	ldr	r2, [r4, #8]
	ldr	r1, [fp, #-44]
	ldrb	r0, [fp, #-48]	@ zero_extendqisi2
	blx	ip
	str	r0, [r4]
	ldrh	r3, [r5, #14]
	cmp	r3, #4
	bne	.L389
	ldr	r3, [r4, #12]
	ldr	r2, [r4, #8]
	ldr	r1, [fp, #-44]
	ldr	ip, [r5, #3256]
	add	r3, r3, #8
	add	r2, r2, #2048
	ldrb	r0, [fp, #-48]	@ zero_extendqisi2
	add	r1, r8, r1
	blx	ip
	cmn	r0, #1
	beq	.L390
	ldr	r3, [r4, #12]
	ldr	r2, [r3, #12]
	cmn	r2, #1
	bne	.L389
	ldr	r2, [r3, #8]
	cmn	r2, #1
	bne	.L389
	ldr	r3, [r3]
	cmn	r3, #1
	beq	.L389
.L390:
	mvn	r3, #0
	str	r3, [r4]
.L389:
	add	r6, r6, #1
	add	r4, r4, #20
	b	.L385
.L403:
	.align	2
.L402:
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
	ldr	r4, .L414
	mov	r6, #0
	mvn	r9, #0
	ldr	r10, .L414+4
	ldr	r3, [r4, #3280]
	add	r7, r4, #162
	ldr	r8, [r4, #3284]
	str	r3, [r4, #3268]
	str	r8, [r4, #3272]
.L405:
	ldrh	r3, [r4, #54]
	cmp	r6, r3
	bcc	.L410
	mov	r0, #0
	ldmfd	sp, {r4, r5, r6, r7, r8, r9, r10, fp, sp, pc}
.L410:
	ldrh	r5, [r4, #98]
	strh	r9, [r7, #2]!	@ movhi
.L407:
	ldrh	r3, [r4, #98]
	sub	r5, r5, #1
	uxth	r5, r5
	sub	r2, r3, #16
	cmp	r5, r2
	ble	.L408
	mla	r3, r6, r3, r5
	mov	r2, #1
	mov	r1, r2
	mov	r0, r10
	lsl	r3, r3, #10
	str	r3, [r4, #3264]
	bl	FlashReadPages
	ldr	r3, [r4, #3260]
	cmn	r3, #1
	beq	.L407
	ldrh	r2, [r8]
	movw	r3, #61664
	cmp	r2, r3
	bne	.L407
	strh	r5, [r7]	@ movhi
.L408:
	add	r6, r6, #1
	b	.L405
.L415:
	.align	2
.L414:
	.word	.LANCHOR0
	.word	.LANCHOR0+3260
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
	ldr	r3, .L424
	lsl	r6, r0, #10
	mov	r2, r1
	mov	r7, r1
	sub	r0, fp, #116
	ldrheq	r4, [r3, #104]
	mov	r1, #1
	ldrhne	r4, [r3, #102]
	ldr	r3, .L424+4
	sub	r4, r4, #1
	sxth	r5, r4
	str	r3, [fp, #-108]
	sub	r3, fp, #96
	str	r3, [fp, #-104]
	orr	r3, r5, r6
	str	r3, [fp, #-112]
	bl	FlashReadPages
	ldr	r3, [fp, #-96]
	cmn	r3, #1
	moveq	r8, #0
	beq	.L420
.L419:
	mov	r0, r5
	sub	sp, fp, #32
	ldmfd	sp, {r4, r5, r6, r7, r8, fp, sp, pc}
.L423:
	add	r4, r8, r5
	mov	r2, r7
	add	r4, r4, r4, lsr #31
	mov	r1, #1
	sub	r0, fp, #116
	asr	r4, r4, #1
	sxth	r3, r4
	orr	r3, r3, r6
	str	r3, [fp, #-112]
	bl	FlashReadPages
	ldr	r3, [fp, #-96]
	cmn	r3, #1
	bne	.L421
	ldr	r3, [fp, #-92]
	cmn	r3, #1
	subeq	r4, r4, #1
	sxtheq	r5, r4
	beq	.L420
.L421:
	add	r4, r4, #1
	sxth	r8, r4
.L420:
	cmp	r8, r5
	ble	.L423
	b	.L419
.L425:
	.align	2
.L424:
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
	ldr	r6, .L456
	mov	r10, r1
	mov	r8, r2
	mov	r4, r0
	mov	r5, r0
	mov	r7, #0
	ldrh	r3, [r6, #12]
	str	r3, [fp, #-72]
.L427:
	cmp	r7, r10
	bne	.L435
	cmp	r9, #0
	bne	.L442
.L455:
	mov	r0, #0
	sub	sp, fp, #40
	ldmfd	sp, {r4, r5, r6, r7, r8, r9, r10, fp, sp, pc}
.L435:
	ldr	r3, [r5, #8]
	cmp	r3, #0
	beq	.L428
	ldr	r3, [r5, #12]
	cmp	r3, #0
	bne	.L429
.L428:
	mov	r2, #131
	ldr	r1, .L456+4
	ldr	r0, .L456+8
	bl	sftl_printk
.L429:
	sub	r2, fp, #68
	sub	r1, fp, #64
	mov	r0, r5
	bl	l2p_addr_tran
	ldr	r3, [r5, #12]
	ldr	ip, [r6, #3252]
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
	bne	.L433
	ldr	r0, [fp, #-72]
	ldr	r3, [r5, #12]
	ldr	r2, [r5, #8]
	ldr	r1, [fp, #-64]
	add	r3, r3, #8
	ldr	ip, [r6, #3252]
	add	r2, r2, #2048
	add	r1, r0, r1
	ldrb	r0, [fp, #-68]	@ zero_extendqisi2
	blx	ip
	cmp	r0, #0
	mvnne	r3, #0
	strne	r3, [r5]
.L433:
	add	r7, r7, #1
	add	r5, r5, #20
	b	.L427
.L440:
	mov	r3, #0
	mov	r2, r8
	str	r3, [r9]
	mov	r1, #1
	str	r3, [r6, #3288]
	sub	r0, fp, #60
	ldr	r3, [r4, #4]
	str	r9, [fp, #-52]
	str	r10, [fp, #-48]
	str	r3, [fp, #-56]
	bl	FlashReadPages
	ldr	r3, [fp, #-60]
	cmn	r3, #1
	bne	.L437
	ldr	r1, [r4, #4]
	ldr	r0, .L456+12
	str	r3, [fp, #-72]
	bl	sftl_printk
	ldr	r3, [fp, #-72]
	str	r3, [r4]
.L437:
	ldr	r3, [r4, #12]
	cmp	r3, #0
	beq	.L438
	ldr	r2, [r3]
	ldr	r3, [r6, #3288]
	cmp	r2, r3
	beq	.L438
	ldr	r1, [r4, #4]
	ldr	r0, .L456+16
	bl	sftl_printk
	mvn	r3, #0
	str	r3, [r4]
.L438:
	ldr	r3, [r4, #8]
	cmp	r3, #0
	beq	.L439
	ldr	r2, [r3]
	ldr	r3, [r9]
	cmp	r2, r3
	beq	.L439
	ldr	r1, [r4, #4]
	ldr	r0, .L456+20
	bl	sftl_printk
	mvn	r3, #0
	str	r3, [r4]
.L439:
	add	r5, r5, #1
	add	r4, r4, #20
.L436:
	cmp	r7, r5
	bne	.L440
	b	.L455
.L442:
	mov	r5, #0
	ldr	r9, .L456+24
	ldr	r10, .L456+28
	b	.L436
.L457:
	.align	2
.L456:
	.word	.LANCHOR0
	.word	.LANCHOR1+145
	.word	.LC1
	.word	.LC80
	.word	.LC81
	.word	.LC82
	.word	check_buf
	.word	.LANCHOR0+3288
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
	ldr	r5, .L469
	mov	r7, r2
	mov	r4, r0
	ldrh	r8, [r5, #12]
.L459:
	cmp	r6, r7
	bne	.L465
	mov	r0, #0
	sub	sp, fp, #32
	ldmfd	sp, {r4, r5, r6, r7, r8, fp, sp, pc}
.L465:
	sub	r2, fp, #40
	sub	r1, fp, #36
	mov	r0, r4
	bl	l2p_addr_tran
	ldr	r3, [r5, #3248]
	ldr	r1, [fp, #-36]
	ldrb	r0, [fp, #-40]	@ zero_extendqisi2
	blx	r3
	cmp	r0, #0
	mvnne	r3, #0
	streq	r0, [r4]
	strne	r3, [r4]
	ldrh	r3, [r5, #14]
	cmp	r3, #4
	bne	.L463
	ldr	r1, [fp, #-36]
	ldr	r3, [r5, #3248]
	ldrb	r0, [fp, #-40]	@ zero_extendqisi2
	add	r1, r8, r1
	blx	r3
	cmp	r0, #0
	mvnne	r3, #0
	strne	r3, [r4]
.L463:
	add	r6, r6, #1
	add	r4, r4, #20
	b	.L459
.L470:
	.align	2
.L469:
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
	ldr	r4, .L480
	ldrh	r3, [r4, #218]
	cmp	r3, #1024
	ldmfdeq	sp, {r4, r5, r6, fp, sp, pc}
	cmp	r1, #0
	beq	.L473
	mov	r0, r5
	bl	P2V_block_in_plane
	mov	r6, r0
	ldr	r0, [r4, #3800]
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
.L473:
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
.L481:
	.align	2
.L480:
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
	ldr	r3, .L524
	mov	r4, r10
	ldr	r2, [fp, #-48]
	mov	r9, #20
	mov	r8, r3
	str	r2, [r3, #3804]
.L483:
	ldrh	r1, [r8, #32]
	uxth	r2, r10
	cmp	r1, r2
	bhi	.L487
	cmp	r5, #0
	beq	.L482
	mov	r7, #0
	mov	r9, #20
	mov	r2, r5
	mov	r1, #0
	ldr	r0, [r8, #3800]
	bl	FlashEraseBlocks
.L490:
	uxth	r2, r7
	cmp	r5, r2
	bhi	.L492
.L493:
	cmp	r6, #0
	ldrhne	r3, [r8, #104]
	moveq	r3, #6
	ldr	r8, .L524
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
.L502:
	mov	r7, #0
	mov	r5, r7
.L494:
	ldrh	r1, [r8, #32]
	uxth	r3, r7
	cmp	r1, r3
	bhi	.L497
	cmp	r5, #0
	beq	.L482
	ldr	r2, [fp, #-56]
	mov	r3, #1
	mov	r1, r5
	ldr	r0, [r8, #3800]
	bl	FlashProgPages
	mov	r9, #0
	mov	r2, #20
.L499:
	uxth	r3, r9
	cmp	r5, r3
	bhi	.L501
	ldr	r3, [fp, #-44]
	ldr	r2, [fp, #-60]
	add	r7, r3, r2
	uxth	r3, r7
	str	r3, [fp, #-44]
	ldr	r2, [fp, #-44]
	ldr	r3, [fp, #-52]
	cmp	r3, r2
	bhi	.L502
	mov	r7, #0
	mov	r9, #20
.L503:
	uxth	r3, r7
	cmp	r5, r3
	bhi	.L505
	ldr	r3, [fp, #-48]
	adds	r6, r6, #0
	movne	r6, #1
	cmp	r3, #63
	movhi	r8, r6
	orrls	r8, r6, #1
	cmp	r8, #0
	beq	.L482
	mov	r2, r5
	ldr	r1, [fp, #-56]
	ldr	r0, [r10, #3800]
	bl	FlashEraseBlocks
.L482:
	mov	r0, r4
	sub	sp, fp, #40
	ldmfd	sp, {r4, r5, r6, r7, r8, r9, r10, fp, sp, pc}
.L487:
	uxth	r2, r10
	ldr	r0, [r8, #3800]
	mov	r3, #0
	mul	r1, r9, r2
	add	r2, r8, r2
	str	r3, [r0, r1]
	ldr	r1, [fp, #-48]
	ldrb	r0, [r2, #60]	@ zero_extendqisi2
	bl	V2P_block
	cmp	r6, #0
	mov	r7, r0
	beq	.L484
	bl	IsBlkInVendorPart
	cmp	r0, #0
	bne	.L485
.L484:
	mov	r0, r7
	bl	FtlBbmIsBadBlock
	cmp	r0, #0
	addne	r4, r4, #1
	uxthne	r4, r4
	bne	.L485
	ldr	r1, [r8, #3800]
	lsl	r7, r7, #10
	ldr	r2, [r8, #3808]
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
	ldr	r0, [r8, #3812]
	bic	r2, r2, #3
	add	r2, r0, r2
	str	r2, [r1, #12]
.L485:
	add	r10, r10, #1
	b	.L483
.L492:
	mul	r2, r9, r7
	ldr	r1, [r8, #3800]
	add	r0, r1, r2
	ldr	r2, [r1, r2]
	cmn	r2, #1
	bne	.L491
	ldr	r0, [r0, #4]
	add	r4, r4, #1
	uxth	r4, r4
	ubfx	r0, r0, #10, #16
	bl	FtlBbmMapBadBlock
.L491:
	add	r7, r7, #1
	b	.L490
.L497:
	uxth	r3, r7
	mov	r2, #20
	ldr	r0, [r8, #3800]
	mul	r1, r2, r3
	mov	r2, #0
	add	r3, r8, r3
	str	r2, [r0, r1]
	ldr	r1, [fp, #-48]
	ldrb	r0, [r3, #60]	@ zero_extendqisi2
	bl	V2P_block
	cmp	r6, #0
	mov	r9, r0
	beq	.L495
	bl	IsBlkInVendorPart
	cmp	r0, #0
	bne	.L496
.L495:
	mov	r0, r9
	bl	FtlBbmIsBadBlock
	cmp	r0, #0
	bne	.L496
	ldr	r1, [r8, #3800]
	mov	r3, #20
	mla	r1, r3, r5, r1
	ldr	r3, [fp, #-44]
	add	r9, r3, r9, lsl #10
	ldr	r3, [r8, #3816]
	str	r9, [r1, #4]
	str	r3, [r1, #8]
	ldrh	r3, [r8, #112]
	mul	r3, r5, r3
	add	r5, r5, #1
	uxth	r5, r5
	add	r0, r3, #3
	cmp	r3, #0
	movlt	r3, r0
	ldr	r0, [r8, #3808]
	bic	r3, r3, #3
	add	r3, r0, r3
	str	r3, [r1, #12]
.L496:
	add	r7, r7, #1
	b	.L494
.L501:
	mul	r3, r2, r9
	ldr	r1, [r10, #3800]
	add	r0, r1, r3
	ldr	r3, [r1, r3]
	cmp	r3, #0
	beq	.L500
	ldr	r0, [r0, #4]
	add	r4, r4, #1
	str	r2, [fp, #-64]
	uxth	r4, r4
	ubfx	r0, r0, #10, #16
	bl	FtlBbmMapBadBlock
	ldr	r2, [fp, #-64]
.L500:
	add	r9, r9, #1
	b	.L499
.L505:
	cmp	r6, #0
	beq	.L504
	mul	r3, r9, r7
	ldr	r2, [r10, #3800]
	add	r1, r2, r3
	ldr	r3, [r2, r3]
	cmp	r3, #0
	bne	.L504
	ldr	r0, [r1, #4]
	mov	r1, #1
	ubfx	r0, r0, #10, #16
	bl	FtlFreeSysBlkQueueIn
.L504:
	add	r7, r7, #1
	b	.L503
.L525:
	.align	2
.L524:
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
	ldr	r4, .L533
	ldr	r6, .L533+4
.L527:
	ldrh	r1, [r4, #218]
	cmp	r1, #0
	beq	.L528
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
	ldr	r0, [r4, #3800]
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
	bhi	.L529
	mov	r0, r5
	ldmfd	sp, {r4, r5, r6, r7, fp, sp, pc}
.L528:
	ldr	r0, .L533+8
	bl	sftl_printk
.L531:
	b	.L531
.L529:
	ldrh	r2, [r4, #218]
	mov	r1, r5
	mov	r0, r6
	bl	sftl_printk
	b	.L527
.L534:
	.align	2
.L533:
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
.L536:
	uxth	r5, r3
	cmp	r5, r1
	bcs	.L539
	mov	r7, r2
	add	r3, r3, #1
	ldrh	r6, [r7]
	add	r2, r2, #2
	cmp	r6, #0
	bne	.L536
	bl	FtlFreeSysBlkQueueOut
	sub	r3, r0, #1
	movw	r2, #65533
	uxth	r3, r3
	mov	r1, r0
	strh	r0, [r7]	@ movhi
	cmp	r3, r2
	bls	.L537
	ldr	r3, .L543
	ldr	r0, .L543+4
	ldrh	r2, [r3, #218]
	bl	sftl_printk
.L538:
	b	.L538
.L537:
	ldr	r3, [r4, #28]
	strh	r6, [r4, #2]	@ movhi
	strh	r5, [r4]	@ movhi
	add	r3, r3, #1
	str	r3, [r4, #28]
	ldrh	r3, [r4, #8]
	add	r3, r3, #1
	strh	r3, [r4, #8]	@ movhi
.L539:
	ldrh	r3, [r4, #10]
	cmp	r3, r5
	bhi	.L541
	movw	r2, #581
	ldr	r1, .L543+8
	ldr	r0, .L543+12
	bl	sftl_printk
.L541:
	mov	r0, #0
	ldmfd	sp, {r4, r5, r6, r7, fp, sp, pc}
.L544:
	.align	2
.L543:
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
	bls	.L545
	ldr	r5, .L548
	sub	r0, fp, #84
	mov	r2, #32
	mov	r1, #165
	str	r0, [fp, #-92]
	lsl	r4, r4, #10
	str	r5, [fp, #-96]
	bl	memset
	ldr	r3, .L548+4
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
.L545:
	mov	r0, r4
	sub	sp, fp, #20
	ldmfd	sp, {r4, r5, fp, sp, pc}
.L549:
	.align	2
.L548:
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
	ldr	r5, .L557
	mov	r4, r0
	mov	r6, r1
	mov	r2, #256
	mov	r1, #255
	bl	memset
	ldr	r3, [r5, #3244]
	mov	r1, r6
	mov	r0, r4
	blx	r3
	uxth	r0, r0
	cmp	r0, #50
	bls	.L551
	mov	r2, #256
	mov	r1, #255
	mov	r0, r4
	bl	memset
	mov	r0, #0
.L551:
	ldrh	r3, [r5, #14]
	cmp	r3, #4
	moveq	r3, r4
	addeq	r1, r3, r0, lsl #1
	beq	.L553
	ldmfd	sp, {r4, r5, r6, fp, sp, pc}
.L554:
	ldrh	r2, [r3]
	lsr	r2, r2, #1
	strh	r2, [r3], #2	@ movhi
.L553:
	cmp	r3, r1
	bne	.L554
	ldmfd	sp, {r4, r5, r6, fp, sp, pc}
.L558:
	.align	2
.L557:
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
	beq	.L560
	bl	memset
.L560:
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
	ldr	r4, .L665
	movw	r3, #65535
	movw	r2, #3202
	mov	r5, #0
	mov	r6, #12
	str	r3, [r4, #3824]
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
	movw	r3, #3206
	strh	r5, [r4, r3]	@ movhi
	lsl	r0, r0, #1
	movw	r3, #3242
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
	str	r5, [r4, #3820]
	str	r5, [r4, #3160]
	str	r5, [r4, #3828]
	str	r5, [r4, #3152]
	str	r5, [r4, #3804]
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
	str	r0, [r4, #3832]
	mov	r0, r7
	bl	ftl_malloc
	str	r0, [r4, #3836]
	mov	r0, r8
	bl	ftl_malloc
	str	r0, [r4, #3840]
	mov	r0, r7
	bl	ftl_malloc
	str	r0, [r4, #3800]
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
	str	r0, [r4, #3280]
	mov	r0, r7
	bl	ftl_malloc
	str	r0, [r4, #3844]
	mov	r0, r7
	bl	ftl_malloc
	str	r0, [r4, #3848]
	ldr	r0, [r4, #3184]
	mul	r0, r0, r7
	bl	ftl_malloc
	str	r0, [r4, #3172]
	mov	r0, r7
	bl	ftl_malloc
	str	r0, [r4, #3816]
	mov	r0, r7
	bl	ftl_malloc
	str	r0, [r4, #3808]
	ldr	r0, [r4, #3184]
	mul	r0, r6, r0
	bl	ftl_malloc
	ldrh	r3, [r4, #112]
	ldrh	r7, [r4, #32]
	str	r0, [r4, #3168]
	mul	r7, r7, r3
	mov	r0, r7
	bl	ftl_malloc
	str	r0, [r4, #3284]
	lsl	r0, r7, #2
	movw	r7, #3852
	bl	ftl_malloc
	ldrh	r3, [r4, #112]
	str	r0, [r4, #3812]
	ldr	r0, [r4, #3184]
	mul	r0, r0, r3
	bl	ftl_malloc
	str	r0, [r4, #3176]
	ldrh	r0, [r4, #42]
	lsl	r0, r0, #1
	uxth	r0, r0
	strh	r0, [r4, r7]	@ movhi
	bl	ftl_malloc
	str	r0, [r4, #3856]
	ldrh	r0, [r4, r7]
	ldr	r3, .L665+4
	add	r0, r0, #544
	add	r0, r0, #3
	lsr	r0, r0, #9
	strh	r0, [r4, r7]	@ movhi
	and	r0, r3, r0, lsl #9
	bl	ftl_malloc
	ldrh	r7, [r4, #42]
	str	r0, [r4, #3860]
	add	r0, r0, #32
	str	r0, [r4, #2328]
	lsl	r7, r7, #1
	mov	r0, r7
	bl	ftl_malloc
	str	r0, [r4, #3864]
	mov	r0, r7
	bl	ftl_malloc
	ldr	r7, [r4, #128]
	str	r0, [r4, #2324]
	lsl	r7, r7, #1
	mov	r0, r7
	bl	ftl_malloc
	str	r0, [r4, #3868]
	mov	r0, r7
	bl	ftl_malloc
	str	r0, [r4, #3872]
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
	str	r0, [r4, #3876]
	ldrh	r0, [r4, #120]
	lsl	r0, r0, #2
	bl	ftl_malloc
	str	r0, [r4, #3880]
	ldrh	r0, [r4, #122]
	lsl	r0, r0, #2
	bl	ftl_malloc
	ldrh	r2, [r4, #122]
	mov	r1, r5
	str	r0, [r4, #3884]
	movw	r5, #3900
	lsl	r2, r2, #2
	bl	ftl_memset
	ldrh	r0, [r4, #136]
	lsl	r0, r0, #2
	bl	ftl_malloc
	str	r0, [r4, #3888]
	ldr	r0, [r4, #128]
	lsl	r0, r0, #2
	bl	ftl_malloc
	str	r0, [r4, #3892]
	ldrh	r0, [r4, #138]
	mul	r0, r6, r0
	bl	ftl_malloc
	ldrh	r3, [r4, #138]
	str	r0, [r4, #2492]
	ldrh	r0, [r4, #110]
	mul	r0, r0, r3
	bl	ftl_malloc
	str	r0, [r4, #3896]
	ldrh	r0, [r4, #42]
	lsl	r0, r0, #3
	bl	ftl_malloc
	str	r0, [r4, #2316]
	ldrh	r0, [r4, #98]
	ldrh	r3, [r4, #54]
	add	r0, r0, #31
	asr	r0, r0, #5
	strh	r0, [r4, r5]	@ movhi
	mul	r0, r0, r3
	lsl	r0, r0, #2
	bl	ftl_malloc
	ldrh	r2, [r4, r5]
	add	ip, r4, #180
	ldrh	lr, [r4, #54]
	mov	r3, #1
	str	r0, [r4, #180]
	lsl	r2, r2, #2
	mov	r1, r2
.L565:
	cmp	r3, lr
	bcc	.L566
	ldr	r2, .L665+8
	mov	r1, #0
	add	r3, r2, r3, lsl #2
	add	r2, r2, #56
	add	r3, r3, #24
.L567:
	cmp	r2, r3
	bne	.L568
	ldr	r3, [r4, #3868]
	cmp	r3, #0
	bne	.L569
.L571:
	ldr	r1, .L665+12
	ldr	r0, .L665+16
	bl	sftl_printk
	mvn	r0, #0
	ldmfd	sp, {r4, r5, r6, r7, r8, fp, sp, pc}
.L566:
	ldr	r0, [r4, #180]
	add	r3, r3, #1
	add	r0, r0, r1
	add	r1, r1, r2
	str	r0, [ip, #4]!
	b	.L565
.L568:
	str	r1, [r3, #4]!
	b	.L567
.L569:
	ldr	r3, [r4, #3872]
	cmp	r3, #0
	beq	.L571
	ldr	r3, [r4, #3888]
	cmp	r3, #0
	beq	.L571
	ldr	r3, [r4, #3892]
	cmp	r3, #0
	beq	.L571
	ldr	r3, [r4, #2492]
	cmp	r3, #0
	beq	.L571
	ldr	r3, [r4, #3896]
	cmp	r3, #0
	beq	.L571
	ldr	r3, [r4, #2316]
	cmp	r3, #0
	beq	.L571
	ldr	r3, [r4, #180]
	cmp	r3, #0
	beq	.L571
	ldr	r3, [r4, #2324]
	cmp	r3, #0
	beq	.L571
	ldr	r3, [r4, #3188]
	cmp	r3, #0
	beq	.L571
	ldr	r3, [r4, #3196]
	cmp	r3, #0
	beq	.L571
	ldr	r3, [r4, #3832]
	cmp	r3, #0
	beq	.L571
	ldr	r3, [r4, #3840]
	cmp	r3, #0
	beq	.L571
	ldr	r3, [r4, #3800]
	cmp	r3, #0
	beq	.L571
	ldr	r3, [r4, #3180]
	cmp	r3, #0
	beq	.L571
	ldr	r3, [r4, #3836]
	cmp	r3, #0
	beq	.L571
	ldr	r3, [r4, #3280]
	cmp	r3, #0
	beq	.L571
	ldr	r3, [r4, #3844]
	cmp	r3, #0
	beq	.L571
	ldr	r3, [r4, #3848]
	cmp	r3, #0
	beq	.L571
	ldr	r3, [r4, #3172]
	cmp	r3, #0
	beq	.L571
	ldr	r3, [r4, #3816]
	cmp	r3, #0
	beq	.L571
	ldr	r3, [r4, #3808]
	cmp	r3, #0
	beq	.L571
	ldr	r3, [r4, #3168]
	cmp	r3, #0
	beq	.L571
	ldr	r3, [r4, #3284]
	cmp	r3, #0
	beq	.L571
	ldr	r3, [r4, #3812]
	cmp	r3, #0
	beq	.L571
	ldr	r3, [r4, #3176]
	cmp	r3, #0
	beq	.L571
	ldr	r3, [r4, #2328]
	cmp	r3, #0
	beq	.L571
	ldr	r3, [r4, #3856]
	cmp	r3, #0
	beq	.L571
	ldr	r3, [r4, #148]
	cmp	r3, #0
	beq	.L571
	ldr	r3, .L665
	ldr	r2, [r3, #3876]
	cmp	r2, #0
	beq	.L571
	ldr	r2, [r3, #3880]
	cmp	r2, #0
	beq	.L571
	ldr	r3, [r3, #3884]
	cmp	r3, #0
	beq	.L571
	mov	r0, #0
	ldmfd	sp, {r4, r5, r6, r7, r8, fp, sp, pc}
.L666:
	.align	2
.L665:
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
	push	{r4, r5, r6, r7, r8, r9, fp, ip, lr, pc}
	sub	fp, ip, #4
	ldr	r6, .L673
	movw	r3, #3900
	mov	r4, r0
	ldr	r8, .L673+4
	sub	r5, r4, #2
	ldrh	r2, [r6, r3]
	add	r4, r4, #1020
	ldr	r9, .L673+8
	mov	r7, r1
	add	r4, r4, #2
	mov	r1, #0
	mov	r0, r7
	lsl	r2, r2, #2
	bl	ftl_memset
.L670:
	ldrh	r3, [r5, #2]
	movw	r2, #65535
	cmp	r3, r2
	ldmfdeq	sp, {r4, r5, r6, r7, r8, r9, fp, sp, pc}
	ldrh	r2, [r6, #98]
	cmp	r2, r3
	bhi	.L669
	mov	r2, #74
	mov	r1, r8
	mov	r0, r9
	bl	sftl_printk
.L669:
	ldrh	r3, [r5, #2]!
	mov	r0, #1
	cmp	r4, r5
	lsr	r1, r3, #5
	and	r3, r3, #31
	ldr	r2, [r7, r1, lsl #2]
	orr	r3, r2, r0, lsl r3
	str	r3, [r7, r1, lsl #2]
	bne	.L670
	ldmfd	sp, {r4, r5, r6, r7, r8, r9, fp, sp, pc}
.L674:
	.align	2
.L673:
	.word	.LANCHOR0
	.word	.LANCHOR1+197
	.word	.LC1
	.size	FtlBbt2Bitmap, .-FtlBbt2Bitmap
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
	ldr	r4, .L681
	uxth	r6, r0
	uxth	r8, r1
	ldrh	r3, [r4, #136]
	cmp	r3, r6
	bcs	.L676
	mov	r2, #492
	ldr	r1, .L681+4
	ldr	r0, .L681+8
	bl	sftl_printk
.L676:
	ldr	r3, [r4, #3888]
	mov	r5, #12
	ldr	r7, [r3, r6, lsl #2]
	cmp	r7, #0
	bne	.L677
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
.L678:
	mov	r0, #0
	ldmfd	sp, {r4, r5, r6, r7, r8, fp, sp, pc}
.L677:
	mul	r5, r5, r8
	ldr	r3, [r4, #2492]
	mov	r2, #1
	ldr	r0, .L681+12
	mov	r1, r2
	str	r7, [r4, #3264]
	add	r3, r3, r5
	ldr	r3, [r3, #8]
	str	r3, [r4, #3268]
	ldr	r3, [r4, #3284]
	str	r3, [r4, #3272]
	bl	FlashReadPages
	ldr	r8, [r4, #3272]
	ldrh	r3, [r8, #8]
	cmp	r3, r6
	beq	.L679
	mov	r2, r7
	mov	r1, r6
	ldr	r0, .L681+16
	bl	sftl_printk
	mov	r3, #4
	ldr	r1, [r4, #3272]
	mov	r2, r3
	ldr	r0, .L681+20
	bl	rknand_print_hex
	ldrh	r3, [r4, #136]
	mov	r2, #4
	ldr	r1, [r4, #3888]
	ldr	r0, .L681+24
	bl	rknand_print_hex
.L679:
	ldrh	r3, [r8, #8]
	cmp	r3, r6
	beq	.L680
	movw	r2, #513
	ldr	r1, .L681+4
	ldr	r0, .L681+8
	bl	sftl_printk
.L680:
	ldr	r3, [r4, #2492]
	mov	r1, #0
	add	r2, r3, r5
	str	r1, [r2, #4]
	strh	r6, [r3, r5]	@ movhi
	b	.L678
.L682:
	.align	2
.L681:
	.word	.LANCHOR0
	.word	.LANCHOR1+211
	.word	.LC1
	.word	.LANCHOR0+3260
	.word	.LC87
	.word	.LC88
	.word	.LC89
	.size	load_l2p_region, .-load_l2p_region
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
.L684:
	ldrh	r1, [r4, #6]
	uxth	r3, r2
	cmp	r1, r3
	bhi	.L688
	ldrh	r2, [r5]
	mov	r7, #0
	ldr	ip, .L697
	mov	r10, r7
.L689:
	ldrh	r1, [r4, #10]
	uxth	r3, r7
	cmp	r1, r3
	bhi	.L693
	mov	r0, r10
	sub	sp, fp, #40
	ldmfd	sp, {r4, r5, r6, r7, r8, r9, r10, fp, sp, pc}
.L688:
	uxth	r3, r2
	mov	r1, #0
	ldr	r0, [r6, r3, lsl #2]
	ubfx	r0, r0, #10, #16
.L685:
	ldrh	ip, [r4, #10]
	uxth	r3, r1
	cmp	ip, r3
	addls	r2, r2, #1
	bls	.L684
.L687:
	uxth	r3, r1
	add	r1, r1, #1
	lsl	r3, r3, #1
	ldrh	ip, [r9, r3]
	cmp	ip, r0
	ldrheq	ip, [r5, r3]
	addeq	ip, ip, #1
	strheq	ip, [r5, r3]	@ movhi
	b	.L685
.L693:
	ldrh	r1, [r4]
	uxth	r6, r7
	cmp	r1, r3
	bne	.L690
	ldrh	r0, [r4, #2]
	ldrh	r1, [ip, #104]
	cmp	r0, r1
	lslcc	r0, r6, #1
	strhcc	r1, [r5, r0]	@ movhi
.L690:
	lsl	r6, r6, #1
	ldrh	r8, [r5, r6]
	cmp	r2, r8
	movhi	r10, r3
	movhi	r2, r8
	cmp	r8, #0
	bne	.L692
	ldrh	r0, [r9, r6]
	cmp	r0, #0
	beq	.L692
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
.L692:
	add	r7, r7, #1
	b	.L689
.L698:
	.align	2
.L697:
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
	bne	.L700
	ldrh	r3, [r0, #8]
	cmp	r3, #0
	beq	.L701
	movw	r2, #641
	ldr	r1, .L709
	ldr	r0, .L709+4
	bl	sftl_printk
.L701:
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
.L702:
	mov	r0, #0
	ldmfd	sp, {r4, r5, r6, r7, fp, sp, pc}
.L700:
	lsl	r3, r3, #1
	ldr	r2, [r0, #28]
	ldr	r6, [r0, #24]
	mov	r1, #255
	ldrh	r7, [r5, r3]
	ldrh	r3, [r0, #2]
	ldr	r5, .L709+8
	orr	r3, r3, r7, lsl #10
	str	r3, [r5, #3264]
	ldr	r3, [r5, #3280]
	str	r3, [r5, #3268]
	ldr	r3, [r5, #3284]
	str	r3, [r5, #3272]
	str	r2, [r3, #4]
	ldr	r2, .L709+12
	strh	r2, [r3, #8]	@ movhi
	ldrh	r2, [r0, #4]
	strh	r7, [r3, #2]	@ movhi
	strh	r2, [r3]	@ movhi
	ldrh	r2, [r5, #104]
	ldr	r0, [r5, #3280]
	lsl	r2, r2, #3
	bl	ftl_memset
	mov	r2, #0
	mov	r3, r2
.L703:
	ldrh	r0, [r4, #6]
	uxth	r1, r2
	cmp	r0, r1
	bhi	.L705
	mov	r2, #1
	mov	r3, #0
	mov	r1, r2
	ldr	r0, .L709+16
	bl	FlashProgPages
	ldrh	r3, [r4, #2]
	mov	r0, r4
	add	r3, r3, #1
	strh	r3, [r4, #2]	@ movhi
	bl	ftl_map_blk_gc
	b	.L702
.L705:
	uxth	r1, r2
	ldr	r0, [r6, r1, lsl #2]
	cmp	r7, r0, lsr #10
	bne	.L704
	ldr	r0, [r5, #3280]
	add	r3, r3, #1
	uxth	r3, r3
	str	r1, [r0, r3, lsl #3]
	ldr	r0, [r6, r1, lsl #2]
	ldr	r1, [r5, #3280]
	add	r1, r1, r3, lsl #3
	str	r0, [r1, #4]
.L704:
	add	r2, r2, #1
	b	.L703
.L710:
	.align	2
.L709:
	.word	.LANCHOR1+227
	.word	.LC1
	.word	.LANCHOR0
	.word	-1291
	.word	.LANCHOR0+3260
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
	ldr	r7, .L730
	mov	r4, r0
	mov	r8, r1
	mov	r9, r2
	mov	r6, #0
	mov	r5, r7
.L712:
	ldr	r3, [r7, #2524]
	add	r3, r3, #1
	str	r3, [r7, #2524]
	ldrh	r3, [r7, #104]
	ldrh	r2, [r4, #2]
	sub	r3, r3, #1
	cmp	r2, r3
	bge	.L713
	ldrh	r2, [r4]
	movw	r3, #65535
	cmp	r2, r3
	bne	.L714
.L713:
	mov	r0, r4
	bl	Ftl_write_map_blk_to_last_page
.L714:
	ldrh	r3, [r4]
	ldr	r2, [r4, #12]
	lsl	r3, r3, #1
	ldrh	r3, [r2, r3]
	cmp	r3, #0
	bne	.L715
	movw	r2, #699
	ldr	r1, .L730+4
	ldr	r0, .L730+8
	bl	sftl_printk
.L715:
	ldrh	r2, [r4]
	ldrh	r3, [r4, #10]
	cmp	r2, r3
	bcc	.L716
	mov	r2, #700
	ldr	r1, .L730+4
	ldr	r0, .L730+8
	bl	sftl_printk
.L716:
	ldrh	r3, [r4]
	mov	r1, #16
	ldr	r2, [r4, #12]
	ldr	r0, [r5, #3284]
	lsl	r3, r3, #1
	ldrh	r10, [r2, r3]
	ldrh	r3, [r4, #2]
	str	r0, [r5, #3272]
	str	r9, [r5, #3268]
	orr	r3, r3, r10, lsl #10
	str	r3, [r5, #3264]
	bl	__memzero
	ldr	r3, [r5, #3272]
	ldr	r2, [r4, #28]
	ldr	r0, .L730+12
	strh	r8, [r3, #8]	@ movhi
	str	r2, [r3, #4]
	ldrh	r2, [r4, #4]
	strh	r10, [r3, #2]	@ movhi
	strh	r2, [r3]	@ movhi
	mov	r3, #1
	mov	r2, r3
	mov	r1, r3
	bl	FlashProgPages
	ldrh	r3, [r4, #2]
	add	r3, r3, #1
	uxth	r3, r3
	strh	r3, [r4, #2]	@ movhi
	ldr	r2, [r5, #3260]
	cmn	r2, #1
	bne	.L717
	ldr	r1, [r5, #3264]
	add	r6, r6, #1
	ldr	r0, .L730+16
	uxth	r6, r6
	bl	sftl_printk
	ldrh	r3, [r4, #2]
	cmp	r3, #2
	ldrhls	r3, [r5, #104]
	subls	r3, r3, #1
	strhls	r3, [r4, #2]	@ movhi
	cmp	r6, #3
	bls	.L719
	mov	r2, r6
	ldr	r1, [r5, #3264]
	ldr	r0, .L730+20
	bl	sftl_printk
.L720:
	b	.L720
.L719:
	ldr	r3, [r4, #32]
	cmp	r3, #0
	beq	.L712
.L729:
	b	.L729
.L717:
	cmp	r3, #1
	beq	.L712
	ldr	r2, [r5, #3264]
	mov	r0, #0
	ldr	r3, [r4, #24]
	str	r2, [r3, r8, lsl #2]
	ldmfd	sp, {r4, r5, r6, r7, r8, r9, r10, fp, sp, pc}
.L731:
	.align	2
.L730:
	.word	.LANCHOR0
	.word	.LANCHOR1+258
	.word	.LC1
	.word	.LANCHOR0+3260
	.word	.LC90
	.word	.LC91
	.size	FtlMapWritePage, .-FtlMapWritePage
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
	ldrh	r3, [r4, #10]
	ldrh	r2, [r4, #8]
	ldr	r5, .L744
	sub	r3, r3, #4
	cmp	r2, r3
	blt	.L733
	uxth	r0, r0
	lsl	r0, r0, #1
	ldrh	r8, [r6, r0]
	cmp	r8, #0
	beq	.L733
	ldr	r3, [r4, #32]
	cmp	r3, #0
	bne	.L733
	mov	r2, #1
	str	r2, [r4, #32]
	strh	r3, [r6, r0]	@ movhi
	ldrh	r3, [r4, #8]
	ldrh	r2, [r4, #2]
	sub	r3, r3, #1
	strh	r3, [r4, #8]	@ movhi
	ldrh	r3, [r5, #104]
	cmp	r2, r3
	bcc	.L734
	mov	r0, r4
	bl	ftl_map_blk_alloc_new_blk
.L734:
	ldr	r10, .L744+4
	mov	r6, #0
.L735:
	ldrh	r2, [r4, #6]
	uxth	r3, r6
	cmp	r2, r3
	bhi	.L739
	mov	r1, #1
	mov	r0, r8
	bl	FtlFreeSysBlkQueueIn
	mov	r3, #0
	str	r3, [r4, #32]
.L733:
	ldrh	r2, [r4, #2]
	ldrh	r3, [r5, #104]
	cmp	r2, r3
	bcc	.L740
	mov	r0, r4
	bl	ftl_map_blk_alloc_new_blk
.L740:
	mov	r0, #0
	sub	sp, fp, #40
	ldmfd	sp, {r4, r5, r6, r7, r8, r9, r10, fp, sp, pc}
.L739:
	uxth	r7, r6
	ldr	r2, [r9, r7, lsl #2]
	cmp	r8, r2, lsr #10
	bne	.L736
	ldr	ip, [r5, #3284]
	ldr	r2, [r5, #3844]
	ldr	r0, .L744+8
	str	ip, [r5, #3272]
	str	r2, [r5, #3268]
	ldr	r2, [r9, r7, lsl #2]
	str	r3, [fp, #-48]
	str	ip, [fp, #-44]
	str	r2, [r5, #3264]
	mov	r2, #1
	mov	r1, r2
	bl	FlashReadPages
	ldr	ip, [fp, #-44]
	ldr	r3, [fp, #-48]
	ldrh	r2, [ip, #8]
	cmp	r2, r3
	beq	.L737
	movw	r2, #613
	mov	r1, r10
	ldr	r0, .L744+12
	bl	sftl_printk
.L737:
	ldr	r3, [r5, #3260]
	cmn	r3, #1
	moveq	r3, #0
	streq	r3, [r9, r7, lsl #2]
	beq	.L736
	ldr	r2, [r5, #3268]
	mov	r1, r7
	mov	r0, r4
	bl	FtlMapWritePage
.L736:
	add	r6, r6, #1
	b	.L735
.L745:
	.align	2
.L744:
	.word	.LANCHOR0
	.word	.LANCHOR1+274
	.word	.LANCHOR0+3260
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
	ldr	r5, .L747
	uxth	r0, r0
	mov	r4, #12
	mul	r4, r4, r0
	ldr	r3, [r5, #2492]
	add	r0, r5, #3904
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
.L748:
	.align	2
.L747:
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
	ldr	r4, .L763
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
	bcc	.L750
	mov	r2, #808
	ldr	r1, .L763+4
	ldr	r0, .L763+8
	bl	sftl_printk
.L750:
	ldrh	r1, [r4, #138]
	mov	r2, #0
	ldr	r0, [r4, #2492]
	mov	r3, #12
.L751:
	uxth	r5, r2
	cmp	r5, r1
	bcc	.L756
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
	beq	.L757
	ldr	r3, [r1, #4]
	cmp	r3, #0
	bge	.L757
	bl	flush_l2p_region
.L757:
	mov	r1, r5
	uxth	r0, r9
	bl	load_l2p_region
	b	.L752
.L756:
	add	r2, r2, #1
	mla	ip, r3, r2, r0
	ldrh	ip, [ip, #-12]
	cmp	ip, r8
	bne	.L751
.L752:
	cmp	r10, #0
	mov	r3, #12
	bne	.L753
	ldr	r2, [r4, #2492]
	mla	r3, r3, r5, r2
	ldr	r3, [r3, #8]
	ldr	r3, [r3, r6, lsl #2]
	str	r3, [r7]
.L754:
	ldr	r2, [r4, #2492]
	mov	r3, #12
	mov	r0, #0
	mla	r5, r3, r5, r2
	ldr	r3, [r5, #4]
	cmn	r3, #1
	addne	r3, r3, #1
	strne	r3, [r5, #4]
	ldmib	sp, {r4, r5, r6, r7, r8, r9, r10, fp, sp, pc}
.L753:
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
	ldr	r3, .L763+12
	strh	r8, [r3]	@ movhi
	b	.L754
.L764:
	.align	2
.L763:
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
	ldr	r7, .L775
	ubfx	r0, r1, #10, #16
	str	r1, [fp, #-32]
	bl	P2V_block_in_plane
	ldr	r2, [r7, #2324]
	lsl	r5, r0, #1
	ldrh	r3, [r2, r5]
	cmp	r3, #0
	bne	.L766
	ldr	r4, [r7, #2340]
	cmp	r4, #0
	beq	.L767
	movw	r2, #2344
	movw	lr, #65535
	ldrh	ip, [r7, r2]
	ldr	r2, [r7, #2316]
	sub	r4, r4, r2
	ubfx	r4, r4, #3, #16
.L768:
	uxth	r1, r3
	cmp	ip, r1
	bls	.L767
	cmp	r4, r0
	bne	.L769
	mov	r1, r4
	ldr	r0, .L775+4
	bl	List_remove_node
	movw	r3, #2344
	ldrh	r3, [r7, r3]
	cmp	r3, #0
	bne	.L770
	movw	r2, #1688
	ldr	r1, .L775+8
	ldr	r0, .L775+12
	bl	sftl_printk
.L770:
	movw	r2, #2344
	mov	r0, r4
	ldrh	r3, [r7, r2]
	sub	r3, r3, #1
	strh	r3, [r7, r2]	@ movhi
	bl	INSERT_DATA_LIST
	ldr	r2, [r7, #2324]
	ldrh	r3, [r2, r5]
.L766:
	add	r3, r3, #1
	strh	r3, [r2, r5]	@ movhi
	b	.L767
.L769:
	lsl	r4, r4, #3
	add	r3, r3, #1
	ldrh	r4, [r2, r4]
	cmp	r4, lr
	bne	.L768
.L767:
	mov	r2, #1
	sub	r1, fp, #32
	mov	r0, r6
	bl	log2phys
	ldmib	sp, {r4, r5, r6, r7, fp, sp, pc}
.L776:
	.align	2
.L775:
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
	ldr	r6, .L785
	mov	r1, #0
	ldr	r0, .L785+4
	bl	sftl_printk
	mov	r4, r6
.L778:
	ldr	r3, [r6, #2504]
	cmp	r5, r3
	bcc	.L784
	sub	sp, fp, #28
	ldmfd	sp, {r4, r5, r6, r7, fp, sp, pc}
.L784:
	mov	r2, #0
	sub	r1, fp, #32
	mov	r0, r5
	bl	log2phys
	ubfx	r3, r5, #0, #11
	cmp	r3, #0
	bne	.L779
	ldr	r2, [fp, #-32]
	mov	r1, r5
	ldr	r0, .L785+8
	bl	sftl_printk
.L779:
	ldr	r3, [fp, #-32]
	cmn	r3, #1
	beq	.L781
	str	r3, [r4, #3264]
	mov	r2, #0
	ldr	r3, [r4, #3280]
	mov	r1, #1
	ldr	r7, [r4, #3284]
	ldr	r0, .L785+12
	str	r3, [r4, #3268]
	str	r5, [r4, #3276]
	str	r7, [r4, #3272]
	str	r2, [r4, #3260]
	bl	FlashReadPages
	ldr	r3, [r4, #3260]
	cmn	r3, #1
	cmpne	r3, #256
	beq	.L782
	ldr	r3, [r7, #8]
	cmp	r5, r3
	beq	.L781
.L782:
	ldr	r2, [r4, #3268]
	ldr	r3, [r4, #3272]
	ldr	r0, .L785+16
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
	ldr	r2, [r4, #3264]
	bl	sftl_printk
.L781:
	add	r5, r5, #1
	b	.L778
.L786:
	.align	2
.L785:
	.word	.LANCHOR0
	.word	.LC92
	.word	.LC93
	.word	.LANCHOR0+3260
	.word	.LC94
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
	ldr	r3, .L800
	ldr	r0, [r3, #2716]
	mov	r4, r3
	cmp	r0, #0
	beq	.L788
	ldr	r1, [r3, #2720]
	ldr	r2, [r3, #2504]
	cmp	r1, r2
	bcs	.L789
	mov	r5, #2048
.L794:
	ldr	r0, [r4, #2720]
	ldr	r3, [r4, #2504]
	cmp	r0, r3
	bcs	.L791
	mov	r2, #0
	sub	r1, fp, #108
	bl	log2phys
	ldr	r2, [fp, #-108]
	ldr	r3, [r4, #2720]
	cmn	r2, #1
	add	r3, r3, #1
	str	r3, [r4, #2720]
	beq	.L792
	str	r3, [fp, #-88]
	sub	r0, fp, #20
	ldr	r3, .L800+4
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
	bne	.L791
	ldr	r0, [fp, #-108]
	ubfx	r0, r0, #10, #16
	bl	P2V_block_in_plane
	bl	FtlGcRefreshBlock
.L791:
	mvn	r0, #0
.L787:
	sub	sp, fp, #20
	ldmfd	sp, {r4, r5, fp, sp, pc}
.L792:
	subs	r5, r5, #1
	bne	.L794
	b	.L791
.L789:
	ldr	r2, [r3, #2508]
	mov	r0, #0
	str	r0, [r3, #2716]
	str	r0, [r3, #2720]
	str	r2, [r3, #2712]
	b	.L787
.L788:
	ldr	r1, [r3, #2508]
	ldr	r2, [r3, #2712]
	add	ip, r1, #1048576
	cmp	r2, ip
	bhi	.L797
	ldr	r3, [r3, #2564]
	mov	ip, #33554432
	lsr	r3, r3, #10
	add	r2, r2, ip, asr r3
	cmp	r1, r2
	bhi	.L797
	ldr	r3, .L800+8
	ldrb	r3, [r3, #28]	@ zero_extendqisi2
	cmp	r3, #0
	bne	.L787
.L797:
	mov	r3, #1
	mov	r0, #0
	str	r3, [r4, #2716]
	str	r0, [r4, #2720]
	str	r1, [r4, #2712]
	b	.L787
.L801:
	.align	2
.L800:
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
	ldr	r6, .L826
	ldr	r1, .L826+4
	ldr	r0, .L826+8
	mov	r5, r6
	bl	sftl_printk
	mov	r1, #8192
	ldr	r0, .L826+12
	bl	__memzero
.L803:
	ldr	r3, [r6, #2504]
	cmp	r4, r3
	bcc	.L805
	ldr	r8, .L826+12
	mov	r4, #0
	ldr	r9, .L826+16
	mov	r6, r4
.L806:
	ldrh	r2, [r5, #40]
	uxth	r3, r4
	cmp	r2, r3
	bhi	.L808
	ldr	r4, [r5, #2340]
	cmp	r4, #0
	beq	.L809
	movw	r3, #2344
	ldr	r9, .L826+12
	ldrh	r8, [r5, r3]
	mov	r7, #0
	ldr	r3, [r5, #2316]
	ldr	r10, .L826+20
	sub	r4, r4, r3
	ubfx	r4, r4, #3, #16
.L810:
	uxth	r3, r7
	cmp	r8, r3
	bls	.L809
	ldr	r2, [r5, #2324]
	lsl	r3, r4, #1
	ldrh	r2, [r2, r3]
	cmp	r2, #0
	beq	.L811
	mov	r6, #1
	ldrh	r3, [r9, r3]
	mov	r1, r4
	mov	r0, r10
	bl	sftl_printk
.L811:
	ldr	r3, [r5, #2316]
	lsl	r4, r4, #3
	add	r7, r7, #1
	ldrh	r4, [r3, r4]
	movw	r3, #65535
	cmp	r4, r3
	bne	.L810
.L809:
	cmp	r6, #0
	ldmibeq	sp, {r4, r5, r6, r7, r8, r9, r10, fp, sp, pc}
	movw	r2, #2321
	ldr	r1, .L826+4
	ldr	r0, .L826+24
	bl	sftl_printk
	ldmib	sp, {r4, r5, r6, r7, r8, r9, r10, fp, sp, pc}
.L805:
	mov	r2, #0
	sub	r1, fp, #44
	mov	r0, r4
	bl	log2phys
	ldr	r0, [fp, #-44]
	cmn	r0, #1
	beq	.L804
	ubfx	r0, r0, #10, #16
	bl	P2V_block_in_plane
	ldr	r2, .L826+12
	lsl	r0, r0, #1
	ldrh	r3, [r2, r0]
	add	r3, r3, #1
	strh	r3, [r2, r0]	@ movhi
.L804:
	add	r4, r4, #1
	b	.L803
.L808:
	uxth	r1, r4
	ldr	r3, [r5, #2324]
	lsl	r7, r1, #1
	ldrh	r2, [r3, r7]
	ldrh	r3, [r8, r7]
	cmp	r2, r3
	beq	.L807
	mov	r0, r9
	bl	sftl_printk
	ldr	r3, [r5, #2324]
	movw	r2, #65535
	ldrh	r3, [r3, r7]
	cmp	r3, r2
	beq	.L807
	ldrh	r2, [r8, r7]
	cmp	r2, r3
	movhi	r6, #1
.L807:
	add	r4, r4, #1
	b	.L806
.L827:
	.align	2
.L826:
	.word	.LANCHOR0
	.word	.LANCHOR1+314
	.word	.LC95
	.word	check_vpc_table
	.word	.LC96
	.word	.LC97
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
	ldr	r3, .L835
	ldr	r2, [r0, #24]
	sub	r5, r5, #1
	uxth	r5, r5
	ldr	r0, [r3, #3844]
	mov	r4, r3
	ldr	r1, [r3, #3284]
	str	r0, [r3, #3268]
	str	r1, [r3, #3272]
	ldr	r2, [r2, r5, lsl #2]
	cmp	r2, #0
	str	r2, [r3, #3264]
	beq	.L830
	mov	r2, #1
	ldr	r0, .L835+4
	mov	r1, r2
	bl	FlashReadPages
.L831:
	ldr	r2, [r4, #3268]
	mov	r1, r5
	mov	r0, r6
	bl	FtlMapWritePage
	ldmfd	sp, {r4, r5, r6, fp, sp, pc}
.L830:
	ldrh	r2, [r3, #110]
	mov	r1, #255
	bl	ftl_memset
	b	.L831
.L836:
	.align	2
.L835:
	.word	.LANCHOR0
	.word	.LANCHOR0+3260
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
	ldr	r4, .L918
	movw	r3, #3944
	mov	r1, r5
	ldr	r2, [r4, #128]
	mov	r6, r4
	ldr	r0, [r4, #3892]
	strh	r5, [r4, r3]	@ movhi
	strh	r5, [r4, #144]	@ movhi
	lsl	r2, r2, #2
	bl	ftl_memset
	ldr	r2, [r4, #128]
	mov	r1, r5
	ldr	r0, [r4, #3868]
	lsl	r2, r2, #1
	bl	ftl_memset
	ldrh	r2, [r4, #120]
	mov	r1, r5
	ldr	r0, [r4, #3880]
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
.L838:
	ldrh	r3, [r6, #42]
	ldr	r2, [fp, #-44]
	cmp	r3, r2
	bls	.L879
	ldr	r1, [r6, #3176]
	mov	r7, #0
	ldrh	r2, [r6, #32]
	mov	r10, r7
	ldr	r8, [r6, #3832]
	ldr	r3, [r6, #3172]
	str	r1, [fp, #-48]
	ldrh	r9, [r6, #112]
	ldr	ip, .L918+4
	b	.L880
.L840:
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
	bne	.L839
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
.L839:
	add	r7, r7, #1
.L880:
	uxth	r1, r7
	cmp	r2, r1
	bhi	.L840
	cmp	r10, #0
	bne	.L841
.L878:
	ldr	r3, [fp, #-44]
	add	r3, r3, #1
	uxth	r3, r3
	str	r3, [fp, #-44]
	b	.L838
.L841:
	mov	r2, #1
	mov	r1, r10
	mov	r0, r8
	bl	FlashReadPages
	mov	r3, #0
.L917:
	str	r3, [fp, #-48]
	ldrh	r3, [fp, #-48]
	cmp	r10, r3
	bls	.L878
	ldr	r3, [fp, #-48]
	mov	r9, #20
	mul	r9, r9, r3
	ldr	r3, [r4, #3832]
	add	r2, r3, r9
	ldr	r3, [r3, r9]
	ldr	r1, [r2, #4]
	ldr	r7, [r2, #12]
	cmn	r3, #1
	lsr	ip, r1, #10
	uxth	r8, ip
	bne	.L845
	mov	r3, #16
.L847:
	ldr	r0, [r4, #3832]
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
	bne	.L844
	ldr	r3, [r4, #3832]
	mvn	r2, #0
	str	r2, [r3, r9]
	ldr	r3, [r4, #3832]
	ldr	r3, [r3, r9]
	cmp	r3, r2
	bne	.L845
.L846:
	mov	r1, #0
	uxth	r0, ip
	bl	FtlFreeSysBlkQueueIn
	b	.L850
.L844:
	ldr	r2, [r4, #3832]
	ldr	r2, [r2, r9]
	cmn	r2, #1
	bne	.L845
	sub	r3, r3, #1
	uxth	r3, r3
	cmp	r3, #0
	bne	.L847
	b	.L846
.L845:
	ldr	r2, [r6, #2540]
	ldr	r3, [r7, #4]
	cmn	r2, #1
	beq	.L848
	cmp	r2, r3
	bhi	.L849
.L848:
	cmn	r3, #1
	addne	r2, r3, #1
	strne	r2, [r5, #2540]
.L849:
	ldrh	r2, [r7]
	movw	r1, #61604
	cmp	r2, r1
	beq	.L851
	bhi	.L852
	movw	r3, #61574
	cmp	r2, r3
	beq	.L853
.L850:
	ldr	r3, [fp, #-48]
	add	r3, r3, #1
	b	.L917
.L852:
	movw	r3, #61634
	cmp	r2, r3
	beq	.L854
	movw	r3, #65535
	cmp	r2, r3
	beq	.L846
	b	.L850
.L854:
	ldr	r9, .L918+8
	ldr	r3, [r4, #128]
	ldrh	r2, [r9]
	cmp	r2, r3
	bls	.L856
	movw	r2, #1211
	ldr	r1, .L918+12
	ldr	r0, .L918+16
	bl	sftl_printk
.L856:
	ldr	r0, [r4, #128]
	ldrh	r2, [r9]
	ldr	ip, [r4, #3892]
	uxth	r1, r0
	sub	r3, r1, #1
	sub	r1, r1, r2
	sub	r1, r1, #1
	sxth	r3, r3
	sxth	r1, r1
	str	r1, [fp, #-52]
.L857:
	ldr	r1, [fp, #-52]
	cmp	r3, r1
	bgt	.L863
	cmp	r3, #0
	bge	.L895
	b	.L850
.L863:
	lsl	lr, r3, #2
	ldr	r1, [r7, #4]
	str	lr, [fp, #-56]
	ldr	lr, [ip, r3, lsl #2]
	cmp	r1, lr
	bls	.L858
	ldr	r1, [ip]
	cmp	r1, #0
	bne	.L859
	cmp	r0, r2
	addne	r2, r2, #1
	strhne	r2, [r9]	@ movhi
.L859:
	uxth	ip, r3
	mov	r1, #0
.L860:
	uxth	r0, r1
	sxth	r2, r1
	cmp	ip, r0
	bhi	.L861
	ldr	r1, [r7, #4]
	cmp	r3, #0
	ldr	r2, [r5, #3892]
	ldr	r0, [fp, #-56]
	str	r1, [r2, r0]
	lsl	r2, r3, #1
	ldr	r1, [r5, #3868]
	strh	r8, [r1, r2]	@ movhi
	blt	.L850
	ldr	r2, .L918+8
	ldr	r1, [r5, #128]
	ldrh	r2, [r2]
	sub	r1, r1, r2
	sub	r1, r1, #1
	sxth	r1, r1
	cmp	r3, r1
	bgt	.L850
.L895:
	add	r2, r2, #1
	ldr	r1, [r7, #4]
	strh	r2, [r9]	@ movhi
	ldr	r2, [r4, #3892]
	str	r1, [r2, r3, lsl #2]
	lsl	r3, r3, #1
	ldr	r2, [r4, #3868]
.L915:
	strh	r8, [r2, r3]	@ movhi
	b	.L850
.L861:
	ldr	r0, [r6, #3892]
	add	r1, r1, #1
	add	lr, r0, r2, lsl #2
	ldr	lr, [lr, #4]
	str	lr, [r0, r2, lsl #2]
	lsl	r2, r2, #1
	ldr	r0, [r6, #3868]
	add	lr, r0, r2
	ldrh	lr, [lr, #2]
	strh	lr, [r0, r2]	@ movhi
	b	.L860
.L858:
	sub	r3, r3, #1
	sxth	r3, r3
	b	.L857
.L853:
	ldrh	r2, [r4, #144]
	ldrh	r3, [r4, #120]
	cmp	r2, r3
	bls	.L866
	movw	r2, #1252
	ldr	r1, .L918+12
	ldr	r0, .L918+16
	bl	sftl_printk
.L866:
	ldrh	r2, [r4, #120]
	ldrh	r1, [r4, #144]
	ldr	ip, [r4, #3880]
	sub	r0, r2, #1
	sxth	r3, r0
	sub	r0, r0, r1
	str	r0, [fp, #-52]
.L867:
	ldr	r0, [fp, #-52]
	cmp	r3, r0
	ble	.L872
	ldr	r9, [ip, r3, lsl #2]
	lsl	lr, r3, #2
	ldr	r0, [r7, #4]
	cmp	r0, r9
	bls	.L868
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
.L870:
	uxth	r0, r1
	sxth	r2, r1
	cmp	ip, r0
	bhi	.L871
	ldr	r1, [r7, #4]
	ldr	r2, [r5, #3880]
	str	r1, [r2, lr]
	lsl	r2, r3, #1
	ldr	r1, [r5, #148]
	strh	r8, [r1, r2]	@ movhi
.L872:
	cmp	r3, #0
	blt	.L850
	ldrh	r2, [r5, #120]
	ldrh	r1, [r5, #144]
	sub	r2, r2, #1
	sub	r2, r2, r1
	sxth	r2, r2
	cmp	r3, r2
	bgt	.L850
	add	r1, r1, #1
	ldr	r2, [r5, #3880]
	strh	r1, [r5, #144]	@ movhi
	ldr	r1, [r7, #4]
	str	r1, [r2, r3, lsl #2]
	lsl	r3, r3, #1
	ldr	r2, [r5, #148]
	b	.L915
.L871:
	ldr	r0, [r6, #3880]
	add	r1, r1, #1
	add	r9, r0, r2, lsl #2
	ldr	r9, [r9, #4]
	str	r9, [r0, r2, lsl #2]
	lsl	r2, r2, #1
	ldr	r0, [r6, #148]
	add	r9, r0, r2
	ldrh	r9, [r9, #2]
	strh	r9, [r0, r2]	@ movhi
	b	.L870
.L868:
	sub	r3, r3, #1
	sxth	r3, r3
	b	.L867
.L851:
	ldr	r9, .L918+20
	movw	r2, #65535
	ldrh	r1, [r9]
	cmp	r1, r2
	strheq	r8, [r9]	@ movhi
	beq	.L916
	ldrh	r0, [r9, #4]
	cmp	r0, r2
	beq	.L875
	mov	r1, #1
	bl	FtlFreeSysBlkQueueIn
.L875:
	ldr	r3, [r7, #4]
	ldr	r2, [r4, #2584]
	cmp	r2, r3
	strhcs	r8, [r9, #4]	@ movhi
	bcs	.L850
	ldrh	r3, [r9]
	strh	r8, [r9]	@ movhi
	strh	r3, [r9, #4]	@ movhi
	ldr	r3, [r7, #4]
.L916:
	str	r3, [r4, #2584]
	b	.L850
.L879:
	ldr	r2, [r6, #3868]
	ldrh	r3, [r2]
	cmp	r3, #0
	beq	.L881
.L884:
	ldr	r1, [r4, #148]
	ldrh	r2, [r1]
	cmp	r2, #0
	beq	.L882
.L883:
	movw	r3, #3944
	ldrh	r2, [r4, r3]
	ldr	r3, [r4, #128]
	cmp	r2, r3
	bls	.L914
	movw	r2, #1377
	ldr	r1, .L918+12
	ldr	r0, .L918+16
	bl	sftl_printk
.L914:
	mov	r0, #0
	sub	sp, fp, #40
	ldmfd	sp, {r4, r5, r6, r7, r8, r9, r10, fp, sp, pc}
.L881:
	movw	r1, #3944
	ldrh	r1, [r6, r1]
	cmp	r1, #0
	ldrne	r1, [r6, #128]
	beq	.L884
.L885:
	sxth	ip, r3
	cmp	ip, r1
	bcs	.L884
	lsl	r0, ip, #1
	add	r3, r3, #1
	ldrh	r0, [r2, r0]
	cmp	r0, #0
	beq	.L885
	mov	r3, ip
	mov	lr, #0
.L886:
	ldr	r2, [r5, #128]
	cmp	r3, r2
	bcs	.L884
	ldr	r1, [r5, #3868]
	lsl	r2, r3, #1
	sub	r0, r3, ip
	lsl	r6, r0, #1
	ldrh	r7, [r1, r2]
	strh	r7, [r1, r6]	@ movhi
	ldr	r1, [r5, #3892]
	ldr	r6, [r1, r3, lsl #2]
	add	r3, r3, #1
	sxth	r3, r3
	str	r6, [r1, r0, lsl #2]
	ldr	r1, [r5, #3868]
	strh	lr, [r1, r2]	@ movhi
	b	.L886
.L882:
	ldrh	r3, [r4, #144]
	cmp	r3, #0
	ldrhne	r0, [r4, #120]
	beq	.L883
.L891:
	sxth	r3, r2
	cmp	r3, r0
	mov	ip, r3
	bge	.L883
	lsl	lr, r3, #1
	add	r2, r2, #1
	ldrh	lr, [r1, lr]
	cmp	lr, #0
	beq	.L891
	mov	lr, #0
.L892:
	ldrh	r2, [r5, #120]
	cmp	r3, r2
	bge	.L883
	ldr	r1, [r5, #148]
	lsl	r2, r3, #1
	sub	r0, r3, ip
	lsl	r6, r0, #1
	ldrh	r7, [r1, r2]
	strh	r7, [r1, r6]	@ movhi
	ldr	r1, [r5, #3880]
	ldr	r6, [r1, r3, lsl #2]
	add	r3, r3, #1
	sxth	r3, r3
	str	r6, [r1, r0, lsl #2]
	ldr	r1, [r5, #148]
	strh	lr, [r1, r2]	@ movhi
	b	.L892
.L919:
	.align	2
.L918:
	.word	.LANCHOR0
	.word	.LANCHOR0+60
	.word	.LANCHOR0+3944
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
	ldr	r6, .L947
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
	ldr	r3, [r6, #3280]
	mvn	r2, #0
	ldr	r9, [r6, #3284]
	str	r7, [r4, #32]
	str	r3, [r6, #3268]
	str	r9, [r6, #3272]
	strh	r2, [r4]	@ movhi
	strh	r2, [r4, #2]	@ movhi
	mov	r2, #1
	str	r7, [r4, #28]
	str	r2, [r4, #36]
.L921:
	ldr	r3, [fp, #-52]
	sxth	r2, r7
	cmp	r2, r3
	bge	.L938
	ldr	r3, [fp, #-52]
	lsl	r0, r2, #1
	sub	r1, r3, #1
	cmp	r2, r1
	bne	.L922
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
	ldr	ip, .L947+4
	strh	r7, [r4]	@ movhi
	add	r7, r1, #1
	strh	r0, [r4, #2]	@ movhi
	ldr	r2, [r3, r2, lsl #2]
	str	r2, [r4, #28]
.L923:
	sxth	r2, r8
	cmp	r2, r7
	blt	.L925
.L938:
	mov	r0, r4
	bl	ftl_free_no_use_map_blk
	ldrh	r2, [r4, #2]
	ldrh	r3, [r5, #104]
	cmp	r2, r3
	bne	.L927
	mov	r0, r4
	bl	ftl_map_blk_alloc_new_blk
.L927:
	mov	r0, r4
	bl	ftl_map_blk_gc
	mov	r0, r4
	bl	ftl_map_blk_gc
	mov	r0, #0
	sub	sp, fp, #40
	ldmfd	sp, {r4, r5, r6, r7, r8, r9, r10, fp, sp, pc}
.L925:
	ldrh	r1, [r10]
	mov	r0, ip
	str	ip, [fp, #-52]
	orr	r2, r2, r1, lsl #10
	str	r2, [r6, #3264]
	mov	r2, #1
	mov	r1, r2
	bl	FlashReadPages
	ldr	r2, [r6, #3260]
	ldr	ip, [fp, #-52]
	cmn	r2, #1
	beq	.L924
	ldrh	r2, [r9, #8]
	ldr	r3, [fp, #-48]
	cmp	r3, r2
	bls	.L924
	ldrh	r1, [r4, #4]
	ldrh	r0, [r9]
	cmp	r0, r1
	ldreq	r1, [r6, #3264]
	ldreq	r3, [fp, #-44]
	streq	r1, [r3, r2, lsl #2]
.L924:
	add	r8, r8, #1
	b	.L923
.L922:
	ldr	r2, [r5, #3280]
	add	r3, r8, r0
	str	r3, [fp, #-60]
	str	r2, [r5, #3268]
	ldrh	r2, [r5, #104]
	ldrh	r1, [r8, r0]
	ldr	r0, .L947+4
	sub	r2, r2, #1
	orr	r2, r2, r1, lsl #10
	str	r2, [r5, #3264]
	mov	r2, #1
	mov	r1, r2
	bl	FlashReadPages
	ldr	r2, [r5, #3260]
	cmn	r2, #1
	beq	.L940
	ldrh	r1, [r9]
	ldrh	r2, [r4, #4]
	cmp	r1, r2
	bne	.L940
	ldrh	r1, [r9, #8]
	movw	r2, #64245
	cmp	r1, r2
	beq	.L929
.L940:
	mov	r10, #0
.L930:
	ldrh	r1, [r5, #104]
	sxth	r2, r10
	cmp	r2, r1
	bge	.L936
	ldr	r3, [fp, #-60]
	ldr	r0, .L947+4
	ldrh	r1, [r3]
	orr	r2, r2, r1, lsl #10
	str	r2, [r5, #3264]
	mov	r2, #1
	mov	r1, r2
	bl	FlashReadPages
	ldr	r2, [r5, #3260]
	cmn	r2, #1
	beq	.L934
	ldrh	r2, [r9, #8]
	ldr	r3, [fp, #-48]
	cmp	r3, r2
	bls	.L934
	ldrh	r1, [r4, #4]
	ldrh	r0, [r9]
	cmp	r0, r1
	ldreq	r1, [r5, #3264]
	ldreq	r3, [fp, #-44]
	streq	r1, [r3, r2, lsl #2]
.L934:
	add	r10, r10, #1
	b	.L930
.L929:
	mov	r0, #0
	mov	lr, #4
.L931:
	ldrh	r1, [r5, #104]
	sxth	r2, r0
	sub	r1, r1, #1
	cmp	r2, r1
	blt	.L933
.L936:
	add	r7, r7, #1
	b	.L921
.L933:
	ldr	ip, [r5, #3280]
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
	b	.L931
.L948:
	.align	2
.L947:
	.word	.LANCHOR0
	.word	.LANCHOR0+3260
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
	ldr	r3, .L950
	ldrh	r2, [r3, #120]
	add	r0, r3, #3936
	add	r0, r0, #12
	strh	r2, [r0, #10]	@ movhi
	ldr	r2, .L950+4
	strh	r2, [r0, #4]	@ movhi
	ldrh	r2, [r3, #144]
	strh	r2, [r0, #8]	@ movhi
	ldrh	r2, [r3, #122]
	strh	r2, [r0, #6]	@ movhi
	ldr	r2, [r3, #148]
	str	r2, [r3, #3960]
	ldr	r2, [r3, #3880]
	str	r2, [r3, #3964]
	ldr	r2, [r3, #3876]
	str	r2, [r3, #3968]
	ldr	r2, [r3, #3884]
	str	r2, [r3, #3972]
	bl	FtlMapTblRecovery
	mov	r0, #0
	ldmfd	sp, {fp, sp, pc}
.L951:
	.align	2
.L950:
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
	ldr	r4, .L955
	mov	r1, #0
	mvn	r5, #0
	ldr	r2, [r4, #128]
	ldr	r0, [r4, #3872]
	lsl	r2, r2, #1
	bl	ftl_memset
	ldrh	r3, [r4, #110]
	mov	r1, #255
	ldrh	r2, [r4, #138]
	ldr	r0, [r4, #3896]
	mul	r2, r2, r3
	bl	ftl_memset
	mov	r2, #0
	mov	r3, r4
	mov	lr, #12
	mov	r4, r2
.L953:
	ldrh	r0, [r3, #138]
	uxth	r1, r2
	add	ip, r2, #1
	cmp	r0, r1
	bhi	.L954
	ldr	r2, .L955+4
	mvn	r1, #0
	strh	r1, [r2, #2]	@ movhi
	strh	r1, [r2]	@ movhi
	ldr	r1, [r3, #128]
	strh	r1, [r2, #10]	@ movhi
	ldr	r1, .L955+8
	strh	r1, [r2, #4]	@ movhi
	movw	r1, #3944
	ldrh	r1, [r3, r1]
	strh	r1, [r2, #8]	@ movhi
	ldrh	r1, [r3, #136]
	strh	r1, [r2, #6]	@ movhi
	ldr	r2, [r3, #3868]
	str	r2, [r3, #3916]
	ldr	r2, [r3, #3892]
	str	r2, [r3, #3920]
	ldr	r2, [r3, #3872]
	str	r2, [r3, #3924]
	ldr	r2, [r3, #3888]
	str	r2, [r3, #3928]
	ldmfd	sp, {r4, r5, r6, fp, sp, pc}
.L954:
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
	ldr	r0, [r3, #3896]
	bic	r2, r2, #3
	add	r2, r0, r2
	str	r2, [r1, #8]
	mov	r2, ip
	b	.L953
.L956:
	.align	2
.L955:
	.word	.LANCHOR0
	.word	.LANCHOR0+3904
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
	ldr	r0, .L958
	bl	FtlMapTblRecovery
	mov	r0, #0
	ldmfd	sp, {fp, sp, pc}
.L959:
	.align	2
.L958:
	.word	.LANCHOR0+3904
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
	ldr	r4, .L961
	mvn	r3, #0
	movw	r2, #3992
	mov	r5, #0
	strh	r3, [r4, r2]	@ movhi
	mov	r1, r5
	ldrh	r2, [r4, #120]
	ldr	r0, [r4, #148]
	str	r3, [r4, #4004]
	str	r5, [r4, #3988]
	lsl	r2, r2, #1
	str	r5, [r4, #3996]
	str	r5, [r4, #4000]
	strh	r5, [r4, #144]	@ movhi
	bl	ftl_memset
	ldrh	r2, [r4, #42]
	mov	r1, r5
	ldr	r0, [r4, #2328]
	lsl	r2, r2, #1
	bl	ftl_memset
	ldrh	r2, [r4, #42]
	mov	r1, r5
	ldr	r0, [r4, #3856]
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
.L962:
	.align	2
.L961:
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
	ldr	r4, .L974
	mov	r1, #0
	mov	r8, r5
	mov	r7, r5
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
.L964:
	ldrh	r3, [r4, #40]
	uxth	r10, r5
	cmp	r10, r3
	bcs	.L970
	ldrh	r3, [r4, #102]
	mov	r9, r10
	ldrh	ip, [r4, #32]
	str	r3, [fp, #-44]
	mov	r3, #0
	mov	r6, r3
	b	.L971
.L966:
	ldr	r2, .L974+4
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
	addeq	r6, r6, r2
	uxtheq	r6, r6
.L971:
	uxth	r1, r3
	cmp	ip, r1
	bhi	.L966
	cmp	r6, #0
	moveq	r0, r6
	beq	.L967
	mov	r1, r6
	mov	r0, #32768
	bl	__divsi3
.L967:
	ldr	r3, [r4, #2316]
	add	r3, r3, r9, lsl #3
	strh	r0, [r3, #4]	@ movhi
	ldr	r3, .L974+8
	ldrh	r1, [r3]
	cmp	r1, r10
	beq	.L968
	ldrh	r1, [r3, #48]
	cmp	r1, r10
	beq	.L968
	ldrh	r3, [r3, #96]
	cmp	r3, r10
	beq	.L968
	ldr	r2, [r4, #2324]
	lsl	r3, r9, #1
	ldrh	r3, [r2, r3]
	cmp	r3, #0
	bne	.L969
	add	r8, r8, #1
	mov	r0, r9
	uxth	r8, r8
	bl	INSERT_FREE_LIST
.L968:
	add	r5, r5, #1
	b	.L964
.L969:
	add	r7, r7, #1
	mov	r0, r9
	uxth	r7, r7
	bl	INSERT_DATA_LIST
	b	.L968
.L970:
	ldr	r2, [fp, #-48]
	strh	r7, [r2]	@ movhi
	add	r7, r7, r8
	cmp	r7, r3
	movw	r2, #2344
	strh	r8, [r4, r2]	@ movhi
	ble	.L972
	movw	r2, #2154
	ldr	r1, .L974+12
	ldr	r0, .L974+16
	bl	sftl_printk
.L972:
	mov	r0, #0
	sub	sp, fp, #40
	ldmfd	sp, {r4, r5, r6, r7, r8, r9, r10, fp, sp, pc}
.L975:
	.align	2
.L974:
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
	ldr	r4, .L977
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
.L978:
	.align	2
.L977:
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
	ldr	r3, .L1002
	mov	r10, r1
	ldrh	r4, [r3, #4]
	movw	r3, #65535
	cmp	r4, r3
	beq	.L996
	cmp	r4, #0
	bne	.L980
.L981:
	bl	FtlGcPageVarInit
	b	.L982
.L996:
	mov	r4, #0
.L980:
	ldr	r3, .L1002+4
	ldrh	r3, [r3, #102]
	cmp	r3, r10
	beq	.L981
.L982:
	ldr	r5, .L1002+4
	mov	r8, #0
.L990:
	ldrh	r2, [r6]
	movw	r3, #65535
	mov	r0, #0
	strb	r0, [r6, #8]
	cmp	r2, r3
	beq	.L983
.L993:
	ldrh	r3, [r5, #32]
	mov	r2, #0
	ldr	r0, [r5, #3832]
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
.L984:
	ldr	r1, [fp, #-44]
	uxth	r3, r2
	cmp	r1, r3
	bhi	.L986
	mov	r9, #0
	mov	r2, #0
	mov	r1, r7
	bl	FlashReadPages
.L987:
	uxth	r3, r9
	cmp	r7, r3
	bhi	.L991
	add	r8, r8, #1
	add	r4, r4, #1
	cmp	r10, r8
	uxth	r4, r4
	bls	.L992
.L994:
	ldrh	r3, [r5, #102]
	cmp	r3, r4
	bhi	.L993
	mov	r0, #0
	b	.L983
.L986:
	ldrh	r3, [ip], #2
	cmp	r3, r9
	beq	.L985
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
.L985:
	add	r2, r2, #1
	b	.L984
.L991:
	mov	r3, #20
	ldr	r2, [r5, #3832]
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
	bne	.L988
	ldr	ip, [fp, #-48]
	movw	r2, #65535
	add	r9, r9, #1
	ldr	r1, [fp, #-44]
	ldr	r3, [ip, #12]
	ldrh	ip, [r3]
	cmp	ip, r2
	bne	.L989
	mov	r3, #1
	str	r3, [r5, #4000]
.L983:
	ldr	r3, .L1002
	mvn	r2, #0
	strb	r0, [r6, #6]
	mov	r1, r4
	strh	r4, [r6, #2]	@ movhi
	strh	r2, [r3, #4]	@ movhi
	mov	r2, r0
	mov	r0, r6
	bl	ftl_sb_update_avl_pages
	b	.L995
.L989:
	ldr	r2, [r3, #8]
	ldr	r0, [r3, #12]
	bl	FtlGcUpdatePage
	b	.L987
.L988:
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
	b	.L990
.L992:
	ldr	r2, .L1002
	movw	r1, #65535
	ldrh	r3, [r2, #4]
	cmp	r3, r1
	beq	.L994
	add	r3, r3, r8
	strh	r3, [r2, #4]	@ movhi
	ldrh	r3, [r5, #102]
	cmp	r3, r4
	bls	.L994
.L995:
	mvn	r0, #0
	sub	sp, fp, #40
	ldmfd	sp, {r4, r5, r6, r7, r8, r9, r10, fp, sp, pc}
.L1003:
	.align	2
.L1002:
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
	ldr	r4, .L1014
	mov	r1, #0
	ldr	r7, .L1014+4
	ldr	r0, [r4, #3280]
	add	r6, r4, #176
	ldr	r3, [r4, #3284]
	ldrh	r2, [r4, #110]
	str	r0, [r4, #3268]
	str	r3, [r4, #3272]
	bl	ftl_memset
.L1006:
	ldrh	r3, [r4, #54]
	cmp	r5, r3
	blt	.L1007
	ldr	r6, [r4, #3272]
	mov	r2, #16
	mov	r1, #255
	ldr	r9, .L1014+8
	ldr	r8, .L1014+12
	mov	r5, #0
	mov	r0, r6
	mov	r7, r5
	bl	memset
	ldr	r3, .L1014+16
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
.L1008:
	ldr	r3, [r4, #3280]
	mov	r10, #0
	ldrh	r1, [r4, #152]
	ldrh	r2, [r4, #154]
	str	r3, [r4, #3268]
	ldr	r3, [r4, #3284]
	str	r10, [r4, #3260]
	str	r3, [r4, #3272]
	orr	r3, r2, r1, lsl #10
	ldrh	r0, [r6, #10]
	str	r3, [r4, #3264]
	ldrh	r3, [r4, #156]
	str	r0, [sp]
	mov	r0, r9
	bl	sftl_printk
	mov	r3, #1
	mov	r0, r8
	mov	r2, r3
	mov	r1, r3
	bl	FlashProgPages
	ldrh	r3, [r4, #104]
	ldrh	r2, [r4, #154]
	sub	r3, r3, #1
	cmp	r2, r3
	blt	.L1009
	ldr	r3, [r4, #160]
	ldrh	r2, [r4, #152]
	ldr	r0, [r4, #3800]
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
	str	r3, [r4, #3264]
	str	r3, [r0, #4]
	bl	FlashEraseBlocks
	mov	r3, #1
	mov	r0, r8
	mov	r2, r3
	mov	r1, r3
	bl	FlashProgPages
.L1009:
	ldrh	r3, [r4, #154]
	add	r3, r3, #1
	strh	r3, [r4, #154]	@ movhi
	ldr	r3, [r4, #3260]
	cmn	r3, #1
	bne	.L1010
	add	r5, r5, #1
	ldr	r1, [r4, #3264]
	uxth	r5, r5
	ldr	r0, .L1014+20
	bl	sftl_printk
	cmp	r5, #3
	bls	.L1008
	mov	r2, r5
	ldr	r1, [r4, #3264]
	ldr	r0, .L1014+24
	bl	sftl_printk
.L1012:
	b	.L1012
.L1007:
	ldrh	r2, [r7]
	ldr	r3, [r4, #3268]
	ldr	r1, [r6, #4]!
	mul	r0, r2, r5
	lsl	r2, r2, #2
	add	r5, r5, #1
	add	r0, r3, r0, lsl #2
	bl	ftl_memcpy
	b	.L1006
.L1013:
	mov	r7, #1
	b	.L1008
.L1010:
	cmp	r7, #0
	beq	.L1013
	mov	r0, #0
	ldmib	sp, {r4, r5, r6, r7, r8, r9, r10, fp, sp, pc}
.L1015:
	.align	2
.L1014:
	.word	.LANCHOR0
	.word	.LANCHOR0+3900
	.word	.LC98
	.word	.LANCHOR0+3260
	.word	-3887
	.word	.LC99
	.word	.LC100
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
	ldr	r4, .L1051
	add	r9, r4, #2336
.L1017:
	ldr	r7, .L1051+4
	ldrh	r2, [r9]
	ldrh	r3, [r7]
	add	r3, r3, r2
	ldrh	r2, [r4, #40]
	cmp	r3, r2
	ble	.L1018
	movw	r2, #2585
	ldr	r1, .L1051+8
	ldr	r0, .L1051+12
	bl	sftl_printk
.L1018:
	ldr	r3, .L1051+16
	cmp	r5, r3
	movne	r1, #0
	bne	.L1019
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
.L1019:
	ldr	r0, .L1051+20
	bl	List_pop_index_node
	ldrh	r3, [r7]
	mov	r6, r0
	uxth	r8, r0
	cmp	r3, #0
	bne	.L1020
	movw	r2, #2594
	ldr	r1, .L1051+8
	ldr	r0, .L1051+12
	bl	sftl_printk
.L1020:
	ldrh	r3, [r7]
	mov	r0, r5
	sub	r3, r3, #1
	strh	r3, [r7]	@ movhi
	strh	r8, [r5]	@ movhi
	bl	make_superblock
	ldrb	r3, [r5, #7]	@ zero_extendqisi2
	cmp	r3, #0
	bne	.L1021
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
	ble	.L1017
	movw	r2, #2605
	ldr	r1, .L1051+8
	ldr	r0, .L1051+12
	bl	sftl_printk
	b	.L1017
.L1021:
	ldrh	r2, [r9]
	ldrh	r3, [r7]
	add	r3, r3, r2
	ldrh	r2, [r4, #40]
	cmp	r3, r2
	ble	.L1023
	mov	r2, #2608
	ldr	r1, .L1051+8
	ldr	r0, .L1051+12
	bl	sftl_printk
.L1023:
	ldr	r0, [r4, #3800]
	mov	r2, #20
	ldrh	r1, [r4, #32]
	mov	r7, #0
	add	ip, r5, #16
	mov	lr, r7
	mov	r3, r0
	mla	r1, r2, r1, r0
	str	r1, [fp, #-44]
.L1024:
	ldr	r1, [fp, #-44]
	cmp	r1, r3
	bne	.L1026
	cmp	r7, #0
	bne	.L1027
	movw	r2, #2619
	ldr	r1, .L1051+8
	ldr	r0, .L1051+12
	bl	sftl_printk
.L1027:
	ldr	r3, .L1051+24
	ldrh	r3, [r3]
	cmp	r3, r8
	bne	.L1028
	movw	r2, #2621
	ldr	r1, .L1051+8
	ldr	r0, .L1051+12
	bl	sftl_printk
.L1028:
	ldrb	r3, [r5, #8]	@ zero_extendqisi2
	uxth	r6, r6
	cmp	r3, #0
	bne	.L1029
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
.L1032:
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
	ldr	r2, [r4, #3860]
	ldr	r1, [r4, #3800]
	str	r0, [r4, #2556]
	ldr	r3, [r2, #16]
	ldr	r0, .L1051+28
	add	r3, r3, #1
	str	r3, [r2, #16]
	mov	r2, #20
	mla	r2, r2, r7, r1
	add	r3, r1, #4
	add	r2, r2, #24
.L1034:
	add	r3, r3, #20
	cmp	r2, r3
	bne	.L1035
	ldrb	r1, [r5, #8]	@ zero_extendqisi2
	mov	r2, r7
	ldr	r0, [r4, #3800]
	bl	FlashEraseBlocks
	mov	r3, #0
	mov	ip, #20
	mov	r1, r3
.L1036:
	uxth	r2, r3
	cmp	r7, r2
	bhi	.L1038
	cmp	r1, #0
	ble	.L1039
	mov	r0, r6
	bl	update_multiplier_value
	bl	FtlBbmTblFlush
.L1039:
	ldrb	r2, [r5, #7]	@ zero_extendqisi2
	cmp	r2, #0
	bne	.L1040
	ldr	r3, [r4, #2324]
	mvn	r2, #0
	mov	r0, r6
	strh	r2, [r3, r10]	@ movhi
	bl	INSERT_DATA_LIST
	b	.L1017
.L1026:
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
	b	.L1024
.L1029:
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
	b	.L1032
.L1035:
	ldr	r1, [r3, #-20]
	and	r1, r1, r0
	str	r1, [r3, #-20]
	b	.L1034
.L1038:
	mul	r2, ip, r3
	ldr	lr, [r4, #3800]
	add	r0, lr, r2
	ldr	r2, [lr, r2]
	cmn	r2, #1
	bne	.L1037
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
.L1037:
	add	r3, r3, #1
	b	.L1036
.L1040:
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
	beq	.L1041
	ldrb	r3, [r5, #7]	@ zero_extendqisi2
	cmp	r3, #0
	bne	.L1042
.L1041:
	movw	r2, #2674
	ldr	r1, .L1051+8
	ldr	r0, .L1051+12
	bl	sftl_printk
.L1042:
	mov	r0, #0
	sub	sp, fp, #40
	ldmfd	sp, {r4, r5, r6, r7, r8, r9, r10, fp, sp, pc}
.L1052:
	.align	2
.L1051:
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
	ldr	r6, .L1065
	str	r3, [fp, #-44]
	movw	r3, #3206
	ldrh	r3, [r6, r3]
	cmp	r3, #0
	movne	r7, #0
	ldrne	r10, .L1065+4
	bne	.L1055
.L1054:
	mov	r0, #0
	sub	sp, fp, #40
	ldmfd	sp, {r4, r5, r6, r7, r8, r9, r10, fp, sp, pc}
.L1061:
	uxtah	r3, r6, r7
	ldr	r1, [fp, #-44]
	mov	r8, #0
	ldrb	r0, [r3, #60]	@ zero_extendqisi2
	bl	V2P_block
	mov	r9, r0
.L1056:
	ldrh	r3, [r10]
	uxth	r4, r8
	cmp	r3, r4
	bhi	.L1060
	add	r7, r7, #1
.L1055:
	ldrh	r2, [r6, #32]
	uxth	r3, r7
	cmp	r2, r3
	bhi	.L1061
	bl	FtlGcReFreshBadBlk
	b	.L1054
.L1060:
	uxth	r3, r8
	ldr	r5, .L1065+8
	lsl	r2, r3, #1
	ldrh	r2, [r5, r2]
	cmp	r2, r9
	bne	.L1057
	mov	r1, r9
	ldr	r0, .L1065+12
	str	r3, [fp, #-48]
	bl	sftl_printk
	mov	r0, r9
	bl	FtlBbmMapBadBlock
	bl	FtlBbmTblFlush
	ldr	r3, [fp, #-48]
	ldrh	r2, [r10]
	add	r3, r5, r3, lsl #1
.L1058:
	cmp	r4, r2
	bcc	.L1059
	sub	r2, r2, #1
	strh	r2, [r10]	@ movhi
.L1057:
	add	r8, r8, #1
	b	.L1056
.L1059:
	ldrh	r1, [r3, #2]!
	add	r4, r4, #1
	uxth	r4, r4
	strh	r1, [r3, #-2]	@ movhi
	b	.L1058
.L1066:
	.align	2
.L1065:
	.word	.LANCHOR0
	.word	.LANCHOR0+3206
	.word	.LANCHOR0+3208
	.word	.LC101
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
	ldr	r3, .L1077
	uxth	r4, r0
	ldr	r1, [r3, #2324]
	lsl	r2, r4, #1
	mov	r6, r3
	ldrh	r0, [r1, r2]
	cmp	r0, #0
	bne	.L1068
	movw	r2, #2588
	ldrh	r1, [r3, r2]
	cmp	r1, r4
	mvneq	r3, #0
	strheq	r3, [r6, r2]	@ movhi
	beq	.L1070
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
.L1070:
	ldr	r5, .L1077+4
	mov	r1, r4
	ldr	r0, .L1077+8
	bl	List_remove_node
	ldrh	r3, [r5]
	cmp	r3, #0
	bne	.L1072
	movw	r2, #2744
	ldr	r1, .L1077+12
	ldr	r0, .L1077+16
	bl	sftl_printk
.L1072:
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
	ble	.L1076
	movw	r2, #2747
	ldr	r1, .L1077+12
	ldr	r0, .L1077+16
	bl	sftl_printk
.L1076:
	mov	r0, #1
	ldmfd	sp, {r4, r5, r6, fp, sp, pc}
.L1068:
	mov	r0, r4
	bl	List_update_data_list
	mov	r0, #0
	ldmfd	sp, {r4, r5, r6, fp, sp, pc}
.L1078:
	.align	2
.L1077:
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
	uxth	r6, r0
	movw	r3, #65535
	cmp	r6, r3
	ldr	r4, .L1091
	beq	.L1080
	ldr	r3, [r4, #2324]
	lsl	r5, r6, #1
	ldrh	r2, [r3, r5]
	cmp	r2, #0
	subne	r2, r2, #1
	strhne	r2, [r3, r5]	@ movhi
	bne	.L1080
	mov	r1, r6
	ldr	r0, .L1091+4
	bl	sftl_printk
	ldr	r3, [r4, #2324]
	ldrh	r5, [r3, r5]
	cmp	r5, #0
	beq	.L1082
.L1089:
	mov	r5, #0
.L1079:
	mov	r0, r5
	ldmfd	sp, {r4, r5, r6, r7, fp, sp, pc}
.L1082:
	movw	r2, #2762
.L1090:
	ldr	r1, .L1091+8
	ldr	r0, .L1091+12
	bl	sftl_printk
	b	.L1079
.L1080:
	movw	r7, #3992
	movw	r3, #65535
	ldrh	r0, [r4, r7]
	cmp	r0, r3
	strheq	r6, [r4, r7]	@ movhi
	beq	.L1089
.L1084:
	cmp	r6, r0
	beq	.L1089
	bl	update_vpc_list
	ldr	r2, [r4, #2316]
	adds	r5, r0, #0
	ldr	r3, [r4, #2320]
	movne	r5, #1
	ldr	r1, [r4, #2324]
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
	beq	.L1079
	movw	r2, #2778
	b	.L1090
.L1092:
	.align	2
.L1091:
	.word	.LANCHOR0
	.word	.LC102
	.word	.LANCHOR1+401
	.word	.LC1
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
	ldr	r6, .L1111
	ldr	r4, .L1111+4
	ldrh	r2, [r6, #4]
	cmp	r2, #0
	beq	.L1094
	ldrb	r3, [r4, #2356]	@ zero_extendqisi2
	cmp	r3, #0
	bne	.L1094
	ldrb	r1, [r4, #2355]	@ zero_extendqisi2
	ldrh	r3, [r4, #102]
	mul	r3, r3, r1
	cmp	r2, r3
	beq	.L1094
	ldrb	r8, [r4, #2358]	@ zero_extendqisi2
	cmp	r8, #0
	bne	.L1093
	ldr	r7, [r4, #2504]
	mov	r2, r8
	sub	r1, fp, #64
	ldrh	r9, [r4, #32]
	sub	r7, r7, #1
	mov	r0, r7
	bl	log2phys
	ldr	r3, [fp, #-64]
	ldr	r5, [r4, #3284]
	ldr	r0, [r4, #3280]
	cmn	r3, #1
	str	r3, [fp, #-56]
	str	r7, [fp, #-44]
	str	r0, [fp, #-52]
	str	r5, [fp, #-48]
	str	r8, [r5, #4]
	beq	.L1096
	mov	r2, r8
	mov	r1, #1
	sub	r0, fp, #60
	bl	FlashReadPages
.L1097:
	ldr	ip, .L1111
	mov	r8, #0
	ldr	r3, .L1111+8
	lsl	r9, r9, #2
	mov	r10, r8
	strh	r3, [r5]	@ movhi
.L1098:
	cmp	r9, r8
	bne	.L1102
.L1099:
	mov	r3, #1
.L1110:
	strb	r3, [r4, #2358]
.L1093:
	sub	sp, fp, #40
	ldmfd	sp, {r4, r5, r6, r7, r8, r9, r10, fp, sp, pc}
.L1096:
	ldrh	r2, [r4, #110]
	mov	r1, #255
	bl	ftl_memset
	b	.L1097
.L1102:
	ldrh	r3, [r6, #4]
	cmp	r3, #0
	beq	.L1099
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
	b	.L1098
.L1094:
	mov	r3, #0
	b	.L1110
.L1112:
	.align	2
.L1111:
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
	ldr	r5, .L1117
	mov	r4, #0
	mov	r6, #12
	bl	FtlWriteDump_data
.L1114:
	ldrh	r2, [r5, #138]
	uxth	r3, r4
	cmp	r2, r3
	bhi	.L1116
	mov	r0, #0
	ldmfd	sp, {r4, r5, r6, fp, sp, pc}
.L1116:
	ldr	r3, [r5, #2492]
	uxth	r0, r4
	mla	r3, r6, r0, r3
	ldr	r3, [r3, #4]
	cmp	r3, #0
	bge	.L1115
	bl	flush_l2p_region
.L1115:
	add	r4, r4, #1
	b	.L1114
.L1118:
	.align	2
.L1117:
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
	mov	r10, r0
	cmp	r3, r2
	beq	.L1240
	ldrh	r3, [r0, #2]
	ldr	r5, .L1247
	str	r3, [fp, #-64]
	ldrb	r3, [r0, #6]	@ zero_extendqisi2
	ldr	r1, [fp, #-64]
	str	r3, [fp, #-80]
	ldrh	r3, [r5, #102]
	cmp	r3, r1
	mov	r3, #0
	strheq	r3, [r0, #4]	@ movhi
	ldrhne	r0, [r0, #16]
	bne	.L1123
.L1245:
	strb	r3, [r10, #6]
.L1240:
	mov	r0, #0
	sub	sp, fp, #40
	ldmfd	sp, {r4, r5, r6, r7, r8, r9, r10, fp, sp, pc}
.L1124:
	uxth	r1, r3
	add	r1, r10, r1, lsl #1
	ldrh	r0, [r1, #16]
.L1123:
	cmp	r0, r2
	uxth	r6, r3
	add	r3, r3, #1
	beq	.L1124
	mov	r1, #1
	bl	FtlGetLastWrittenPage
	cmn	r0, #1
	mov	r4, r0
	beq	.L1125
	ldrh	r3, [r5, #32]
	mov	r2, #0
	ldr	r0, [r5, #3832]
	movw	r7, #65535
	ldr	lr, [r5, #3176]
	mov	r8, #20
	str	r3, [fp, #-56]
	ldr	r3, [r5, #3172]
	ldrh	r6, [r5, #112]
	str	r3, [fp, #-60]
	ldrh	r3, [r5, #110]
	mov	r5, r2
	str	r3, [fp, #-68]
	add	r3, r10, #16
	mov	ip, r3
	str	r3, [fp, #-76]
.L1126:
	ldr	r1, [fp, #-56]
	uxth	r3, r2
	cmp	r1, r3
	bhi	.L1130
	ldr	r6, .L1247
	mov	r2, #0
	mov	r1, r5
	movw	r8, #65535
	bl	FlashReadPages
	ldr	r3, [r6, #2544]
	uxth	r2, r4
	ldr	r9, [r6, #3832]
	str	r2, [fp, #-60]
	sub	r3, r3, #1
	str	r3, [fp, #-56]
	mov	r7, r9
	mov	r3, #0
.L1131:
	uxth	r2, r3
	cmp	r5, r2
	bhi	.L1136
	addeq	r3, r4, #1
	uxtheq	r3, r3
	streq	r3, [fp, #-60]
	bne	.L1134
.L1241:
	ldr	r0, [r9, #4]
	ubfx	r0, r0, #10, #16
	bl	P2V_plane
	ldrh	r3, [r6, #102]
	sub	r1, fp, #68
	ldr	r2, [fp, #-60]
	str	r0, [fp, #-68]
	ldr	r0, [fp, #-80]
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
	beq	.L1246
	movw	r3, #65535
	cmp	r8, r3
	bne	.L1140
	ldrb	r3, [r10, #8]	@ zero_extendqisi2
	cmp	r3, #0
	bne	.L1141
.L1140:
	ldr	r3, [r6, #4004]
	uxth	r7, r4
	uxth	r4, r4
	cmn	r3, #1
	ldreq	r3, [fp, #-56]
	streq	r3, [r6, #4004]
	ldr	r3, [fp, #-64]
	ldr	r6, .L1247
	add	r3, r3, #7
	cmp	r4, r3
	subgt	r4, r7, #7
	ldrle	r4, [fp, #-64]
	uxthgt	r4, r4
.L1144:
	cmp	r4, r7
	bhi	.L1151
	mov	r3, #0
	ldrh	r8, [r6, #32]
	ldr	r0, [r6, #3832]
	mov	r5, r3
	ldr	r1, [fp, #-76]
	movw	lr, #65535
	mov	ip, #20
	b	.L1152
.L1125:
	ldr	r3, [fp, #-64]
	cmp	r3, #0
	beq	.L1127
	movw	r2, #1755
	ldr	r1, .L1247+4
	ldr	r0, .L1247+8
	bl	sftl_printk
.L1127:
	ldr	r3, [fp, #-80]
	cmp	r3, #0
	cmpne	r6, r3
	beq	.L1128
	movw	r2, #1756
	ldr	r1, .L1247+4
	ldr	r0, .L1247+8
	bl	sftl_printk
.L1128:
	mov	r3, #0
	strh	r3, [r10, #2]	@ movhi
	b	.L1245
.L1130:
	ldrh	r3, [ip], #2
	cmp	r3, r7
	beq	.L1129
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
.L1129:
	add	r2, r2, #1
	b	.L1126
.L1136:
	ldr	r2, [r7]
	cmp	r2, #0
	bne	.L1132
	ldr	ip, [r7, #12]
	ldr	r2, [ip, #4]
	cmn	r2, #1
	beq	.L1133
	ldr	r1, .L1247
	mov	r0, r2
	str	ip, [fp, #-68]
	ldr	r1, [r1, #2544]
	bl	ftl_cmp_data_ver
	ldr	ip, [fp, #-68]
	cmp	r0, #0
	ldrne	r1, .L1247
	addne	r2, r2, #1
	strne	r2, [r1, #2544]
.L1133:
	ldr	r2, [ip]
	cmn	r2, #1
	bne	.L1135
.L1134:
	uxth	r2, r4
	uxth	r3, r3
	str	r2, [fp, #-60]
	mov	r2, #20
	mla	r9, r2, r3, r9
	b	.L1241
.L1132:
	ldr	r8, [fp, #-60]
.L1135:
	add	r3, r3, #1
	add	r7, r7, #20
	b	.L1131
.L1146:
	ldrh	r2, [r1], #2
	add	r3, r3, #1
	cmp	r2, lr
	mlane	r9, ip, r5, r0
	addne	r5, r5, #1
	orrne	r2, r4, r2, lsl #10
	uxthne	r5, r5
	strne	r2, [r9, #4]
.L1152:
	uxth	r2, r3
	cmp	r8, r2
	bhi	.L1146
	mov	r1, r5
	mov	r2, #0
	bl	FlashReadPages
	ldr	r3, [r6, #3832]
	mov	r2, #20
	movw	r1, #65535
	mla	r5, r2, r5, r3
.L1147:
	cmp	r5, r3
	addeq	r4, r4, #1
	uxtheq	r4, r4
	beq	.L1144
.L1150:
	ldr	r2, [r3]
	cmp	r2, #0
	bne	.L1141
	ldr	r2, [r3, #12]
	ldrh	r0, [r2]
	cmp	r0, r1
	beq	.L1149
	ldr	r2, [r2, #4]
	cmn	r2, #1
	strne	r2, [r6, #4004]
.L1149:
	add	r3, r3, #20
	b	.L1147
.L1151:
	mvn	r3, #0
	str	r3, [r6, #4004]
.L1141:
	ldr	r1, .L1247
	mov	r2, #1
	movw	r3, #4008
	ldr	r4, .L1247
	add	r0, r1, #3904
	strh	r2, [r1, r3]	@ movhi
	bl	FtlMapBlkWriteDump_data
	ldr	r8, [fp, #-64]
.L1153:
	ldrh	ip, [r4, #32]
	mov	r3, #0
	ldr	r0, [r4, #3832]
	mov	r9, r3
	ldr	r1, [fp, #-76]
	movw	lr, #65535
	mov	r5, #20
.L1154:
	uxth	r2, r3
	cmp	ip, r2
	bhi	.L1156
	mov	r2, #0
	mov	r1, r9
	bl	FlashReadPages
	mov	r3, #0
.L1244:
	str	r3, [fp, #-72]
	ldrh	r3, [fp, #-72]
	cmp	r9, r3
	bhi	.L1184
	add	r8, r8, #1
	ldrh	r3, [r4, #102]
	uxth	r8, r8
	cmp	r3, r8
	bne	.L1153
	ldrh	r2, [r4, #32]
	movw	r0, #65535
	mov	r3, #0
	strh	r8, [r10, #2]	@ movhi
	strh	r3, [r10, #4]	@ movhi
.L1185:
	uxth	r1, r3
	cmp	r1, r2
	bcs	.L1240
	ldr	r1, [fp, #-76]
	ldrh	ip, [r1], #2
	cmp	ip, r0
	str	r1, [fp, #-76]
	add	r1, r3, #1
	bne	.L1245
	mov	r3, r1
	b	.L1185
.L1156:
	ldrh	r2, [r1], #2
	add	r3, r3, #1
	cmp	r2, lr
	mlane	r6, r5, r9, r0
	orrne	r2, r8, r2, lsl #10
	addne	r9, r9, #1
	uxthne	r9, r9
	strne	r2, [r6, #4]
	b	.L1154
.L1184:
	ldr	r3, [fp, #-72]
	mov	r6, #20
	ldr	r2, [r4, #3832]
	mul	r6, r6, r3
	str	r2, [fp, #-84]
	add	r7, r2, r6
	ldr	r5, [r7, #4]
	ubfx	r0, r5, #10, #16
	str	r5, [fp, #-44]
	bl	P2V_plane
	ldr	r3, [fp, #-64]
	cmp	r8, r3
	bcc	.L1158
	ldr	r2, [fp, #-80]
	moveq	r3, #1
	movne	r3, #0
	cmp	r2, r0
	movls	r3, #0
	andhi	r3, r3, #1
	cmp	r3, #0
	bne	.L1158
	ldr	r3, [fp, #-60]
	ldr	r2, [fp, #-68]
	cmp	r8, r3
	cmpeq	r2, r0
	beq	.L1159
	ldr	r2, [fp, #-84]
	ldr	r3, [r2, r6]
	cmn	r3, #1
	beq	.L1160
	ldr	r7, [r7, #12]
	movw	r3, #61589
	ldrh	r2, [r7]
	cmp	r2, r3
	beq	.L1161
.L1168:
	ldrh	r0, [r10]
.L1243:
	bl	decrement_vpc_count
.L1158:
	ldr	r3, [fp, #-72]
	add	r3, r3, #1
	b	.L1244
.L1161:
	ldr	r3, [r7, #4]
	cmn	r3, #1
	str	r3, [fp, #-56]
	beq	.L1162
	mov	r0, r3
	ldr	r1, [r4, #2544]
	bl	ftl_cmp_data_ver
	cmp	r0, #0
	ldrne	r3, [fp, #-56]
	addne	r3, r3, #1
	strne	r3, [r4, #2544]
.L1162:
	ldrh	r2, [r7]
	movw	r3, #61589
	cmp	r2, r3
	beq	.L1163
	movw	r2, #1905
	ldr	r1, .L1247+4
	ldr	r0, .L1247+8
	bl	sftl_printk
.L1163:
	ldr	r5, [r7, #8]
	sub	r1, fp, #48
	ldr	r3, [r7, #12]
	mov	r2, #0
	mov	r0, r5
	str	r3, [fp, #-52]
	bl	log2phys
	ldr	r1, [r4, #4004]
	cmn	r1, #1
	beq	.L1164
	ldr	r0, [fp, #-56]
	bl	ftl_cmp_data_ver
	cmp	r0, #0
	beq	.L1164
	ldr	r2, [fp, #-52]
	cmn	r2, #1
	beq	.L1165
	ldr	r0, [r4, #3832]
	mov	r1, #1
	add	r0, r0, r6
	str	r2, [r0, #4]
	mov	r2, #0
	ldr	r7, [r0, #12]
	bl	FlashReadPages
	ldr	r2, [r4, #3832]
	ldr	r1, [r2, r6]
	add	r3, r2, r6
	cmn	r1, #1
	bne	.L1166
.L1167:
	mvn	r3, #0
	ldrh	r0, [r10]
	str	r3, [fp, #-52]
	bl	decrement_vpc_count
.L1175:
	ldr	r6, [fp, #-52]
	cmn	r6, #1
	beq	.L1158
.L1188:
	ubfx	r0, r6, #10, #16
	bl	P2V_block_in_plane
	ldrh	r3, [r4, #40]
	mov	r5, r0
	cmp	r3, r0
	bhi	.L1180
	movw	r2, #2006
	ldr	r1, .L1247+4
	ldr	r0, .L1247+8
	bl	sftl_printk
.L1180:
	ldr	r3, .L1247
	ldr	r2, [r3, #2324]
	lsl	r3, r5, #1
	ldrh	r3, [r2, r3]
	cmp	r3, #0
	beq	.L1181
	mov	r0, r5
	b	.L1243
.L1165:
	ldr	r3, [fp, #-44]
	ldr	r2, [fp, #-48]
	cmp	r2, r3
	bne	.L1168
	mov	r2, #1
	sub	r1, fp, #52
	mov	r0, r5
	bl	log2phys
	b	.L1168
.L1166:
	ldr	r1, [r7, #8]
	cmp	r5, r1
	bne	.L1167
	ldr	r1, [r7, #4]
	ldr	r0, [r4, #4004]
	str	r1, [fp, #-84]
	bl	ftl_cmp_data_ver
	cmp	r0, #0
	beq	.L1167
	ldr	r1, [fp, #-48]
	ldr	r0, [fp, #-44]
	cmp	r1, r0
	bne	.L1170
.L1173:
	ldr	r1, [fp, #-52]
	mov	r0, r5
	bl	FtlReUsePrevPpa
	b	.L1167
.L1170:
	ldr	r0, [fp, #-52]
	cmp	r1, r0
	beq	.L1167
	cmn	r1, #1
	streq	r1, [r2, r6]
	beq	.L1172
	str	r1, [r3, #4]
	mov	r2, #0
	mov	r1, #1
	mov	r0, r3
	ldr	r7, [r3, #12]
	bl	FlashReadPages
.L1172:
	ldr	r2, [r4, #3832]
	ldr	r2, [r2, r6]
	cmn	r2, #1
	beq	.L1173
	ldr	r3, [r7, #4]
	ldr	r0, [r4, #4004]
	mov	r1, r3
	bl	ftl_cmp_data_ver
	cmp	r0, #0
	beq	.L1173
	mov	r1, r3
	ldr	r0, [fp, #-84]
	bl	ftl_cmp_data_ver
	cmp	r0, #0
	beq	.L1167
	b	.L1173
.L1164:
	ldr	r3, [fp, #-44]
	ldr	r2, [fp, #-48]
	cmp	r2, r3
	beq	.L1175
	mov	r2, #1
	sub	r1, fp, #44
	mov	r0, r5
	bl	log2phys
	ldr	r6, [fp, #-48]
	cmn	r6, #1
	beq	.L1175
	ldr	r3, [fp, #-52]
	cmp	r6, r3
	beq	.L1188
	ubfx	r0, r6, #10, #16
	bl	P2V_block_in_plane
	ldr	r3, .L1247+12
	ldrh	r2, [r3]
	cmp	r2, r0
	beq	.L1179
	ldrh	r2, [r3, #48]
	cmp	r2, r0
	beq	.L1179
	ldrh	r3, [r3, #96]
	cmp	r3, r0
	bne	.L1175
.L1179:
	ldr	r0, [r4, #3832]
	mov	r2, #0
	mov	r1, #1
	str	r6, [r0, #4]
	ldr	r7, [r0, #12]
	bl	FlashReadPages
	ldr	r3, [r4, #3832]
	ldr	r3, [r3]
	cmn	r3, #1
	beq	.L1175
	ldr	r1, [r7, #4]
	ldr	r0, [fp, #-56]
	bl	ftl_cmp_data_ver
	cmp	r0, #0
	bne	.L1175
	mov	r2, #1
	sub	r1, fp, #48
	mov	r0, r5
	bl	log2phys
	b	.L1175
.L1181:
	mov	r1, r5
	ldr	r0, .L1247+16
	bl	sftl_printk
	b	.L1158
.L1160:
	ldr	r3, [r4, #4012]
	cmp	r3, #31
	addls	r2, r4, r3, lsl #2
	addls	r3, r3, #1
	strls	r3, [r4, #4012]
	strls	r5, [r2, #4016]
	ldrh	r0, [r10]
	bl	decrement_vpc_count
	ldr	r3, [r4, #4004]
	cmn	r3, #1
	ldreq	r3, [fp, #-56]
	beq	.L1242
	ldr	r2, [fp, #-56]
	cmp	r2, r3
	bcs	.L1158
	mov	r3, r2
.L1242:
	str	r3, [r4, #4004]
	b	.L1158
.L1159:
	ldrb	r3, [fp, #-68]	@ zero_extendqisi2
	ldr	r2, [fp, #-68]
	ldr	r1, [fp, #-60]
	strb	r3, [r10, #6]
	ldrh	r3, [fp, #-60]
	strh	r3, [r10, #2]	@ movhi
.L1246:
	mov	r0, r10
	bl	ftl_sb_update_avl_pages
	b	.L1240
.L1248:
	.align	2
.L1247:
	.word	.LANCHOR0
	.word	.LANCHOR1+421
	.word	.LC1
	.word	.LANCHOR0+2348
	.word	.LC103
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
	ldr	r3, .L1260
	movw	r1, #61589
	mov	r4, r0
	mov	r6, #7
	ldr	r2, [r3, #3280]
	mov	r8, r3
	ldr	r5, [r3, #3284]
	str	r2, [fp, #-44]
	mvn	r2, #2
	str	r5, [fp, #-40]
	str	r2, [r5, #8]
	mvn	r2, #1
	str	r2, [r5, #12]
	ldrh	r2, [r0]
	strh	r7, [r5]	@ movhi
	strh	r2, [r5, #2]	@ movhi
	ldr	r2, [r3, #3280]
	str	r1, [r2]
	ldr	r1, .L1260+4
	ldr	r2, [r3, #3280]
	str	r1, [r2, #4]
.L1250:
	subs	r6, r6, #1
	beq	.L1252
	ldrh	r3, [r4, #4]
	cmp	r3, #0
	bne	.L1251
.L1252:
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
.L1251:
	mov	r0, r4
	bl	get_new_active_ppa
	cmn	r0, #1
	str	r0, [fp, #-48]
	beq	.L1252
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
	b	.L1250
.L1261:
	.align	2
.L1260:
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
	ldr	r4, .L1294
	ldr	r7, .L1294+4
	ldr	r3, [r4, #3280]
	ldr	r6, [r4, #3284]
	str	r3, [r4, #3268]
	str	r6, [r4, #3272]
	bl	FtlBbtMemInit
	ldrh	r5, [r4, #98]
	sub	r5, r5, #1
	uxth	r5, r5
.L1263:
	ldrh	r3, [r4, #98]
	sub	r3, r3, #16
	cmp	r5, r3
	ble	.L1266
	lsl	r3, r5, #10
	mov	r2, #1
	mov	r1, r2
	mov	r0, r7
	str	r3, [r4, #3264]
	bl	FlashReadPages
	ldr	r3, [r4, #3260]
	cmn	r3, #1
	bne	.L1264
	ldr	r3, [r4, #3264]
	mov	r2, #1
	mov	r1, r2
	mov	r0, r7
	add	r3, r3, #1
	str	r3, [r4, #3264]
	bl	FlashReadPages
.L1264:
	ldr	r3, [r4, #3260]
	cmn	r3, #1
	beq	.L1265
	ldrh	r2, [r6]
	movw	r3, #61649
	cmp	r2, r3
	bne	.L1265
	ldr	r3, [r6, #4]
	strh	r5, [r4, #152]	@ movhi
	str	r3, [r4, #160]
	ldrh	r3, [r6, #8]
	strh	r3, [r4, #156]	@ movhi
.L1266:
	ldrh	r3, [r4, #152]
	movw	r2, #65535
	cmp	r3, r2
	beq	.L1280
	ldrh	r3, [r4, #156]
	cmp	r3, r2
	beq	.L1270
	lsl	r3, r3, #10
	mov	r2, #1
	mov	r1, r2
	ldr	r0, .L1294+4
	str	r3, [r4, #3264]
	bl	FlashReadPages
	ldr	r3, [r4, #3260]
	cmn	r3, #1
	beq	.L1270
	ldrh	r2, [r6]
	movw	r3, #61649
	cmp	r2, r3
	bne	.L1270
	ldr	r3, [r6, #4]
	ldr	r2, [r4, #160]
	cmp	r3, r2
	ldrhhi	r2, [r4, #156]
	strhi	r3, [r4, #160]
	ldrhhi	r3, [r6, #8]
	strhhi	r2, [r4, #152]	@ movhi
	strhhi	r3, [r4, #156]	@ movhi
.L1270:
	ldr	r7, .L1294+4
	mov	r1, #1
	ldrh	r0, [r4, #152]
	bl	FtlGetLastWrittenPage
	sxth	r5, r0
	add	r0, r0, #1
	strh	r0, [r4, #154]	@ movhi
.L1272:
	cmp	r5, #0
	bge	.L1275
	mov	r2, #254
	ldr	r1, .L1294+8
	ldr	r0, .L1294+12
	bl	sftl_printk
.L1274:
	ldrh	r3, [r6, #10]
	ldrh	r0, [r6, #12]
	strh	r3, [r4, #158]	@ movhi
	movw	r3, #65535
	cmp	r0, r3
	beq	.L1277
	ldr	r2, [r4, #28]
	cmp	r0, r2
	beq	.L1277
	ldrh	r3, [r4, #42]
	lsr	r3, r3, #2
	cmp	r0, r3
	cmpcc	r2, r3
	bcs	.L1277
	bl	FtlSysBlkNumInit
.L1277:
	ldr	r6, .L1294+16
	mov	r5, #0
	ldr	r7, .L1294+20
.L1278:
	ldrh	r3, [r4, #54]
	cmp	r5, r3
	bcc	.L1279
	mov	r0, #0
	ldmfd	sp, {r4, r5, r6, r7, fp, sp, pc}
.L1265:
	sub	r5, r5, #1
	uxth	r5, r5
	b	.L1263
.L1275:
	ldrh	r3, [r4, #152]
	mov	r2, #1
	mov	r1, r2
	mov	r0, r7
	orr	r3, r5, r3, lsl #10
	str	r3, [r4, #3264]
	ldr	r3, [r4, #3280]
	str	r3, [r4, #3268]
	bl	FlashReadPages
	ldr	r3, [r4, #3260]
	cmn	r3, #1
	beq	.L1273
	ldrh	r2, [r6]
	movw	r3, #61649
	cmp	r2, r3
	beq	.L1274
.L1273:
	sub	r5, r5, #1
	sxth	r5, r5
	b	.L1272
.L1279:
	ldrh	r2, [r7]
	ldr	r1, [r4, #3268]
	ldr	r0, [r6, #4]!
	lsl	r2, r2, #2
	mla	r1, r5, r2, r1
	add	r5, r5, #1
	bl	ftl_memcpy
	b	.L1278
.L1280:
	mvn	r0, #0
	ldmfd	sp, {r4, r5, r6, r7, fp, sp, pc}
.L1295:
	.align	2
.L1294:
	.word	.LANCHOR0
	.word	.LANCHOR0+3260
	.word	.LANCHOR1+443
	.word	.LC1
	.word	.LANCHOR0+176
	.word	.LANCHOR0+3900
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
	ldr	r8, .L1315
	bl	FtlBbtMemInit
	bl	FtlLoadFactoryBbt
	sub	r5, r8, #180
	sub	r9, r8, #18
	mov	r4, r5
.L1297:
	ldrh	r3, [r5, #54]
	cmp	r7, r3
	bcc	.L1303
	mov	r0, #0
.L1304:
	ldrh	r2, [r4, #114]
	uxth	r3, r0
	add	r5, r0, #1
	cmp	r2, r3
	bhi	.L1305
	ldrh	r5, [r4, #164]
	movw	r6, #65535
	sub	r5, r5, #1
	uxth	r5, r5
.L1306:
	ldrh	r3, [r4, #164]
	sub	r3, r3, #48
	cmp	r5, r3
	ble	.L1310
	mov	r0, r5
	bl	FtlBbmIsBadBlock
	cmp	r0, #1
	beq	.L1307
	mov	r0, r5
	bl	FlashTestBlk
	cmp	r0, #0
	beq	.L1308
	mov	r0, r5
	bl	FtlBbmMapBadBlock
.L1307:
	sub	r5, r5, #1
	uxth	r5, r5
	b	.L1306
.L1303:
	ldrh	r3, [r9, #2]!
	movw	r2, #65535
	ldr	r0, [r5, #3280]
	ldr	r10, [r5, #3284]
	cmp	r3, r2
	str	r0, [r5, #3268]
	str	r10, [r5, #3272]
	beq	.L1298
	ldrh	ip, [r5, #98]
	mov	r2, #1
	mov	r1, r2
	ldr	r0, .L1315+4
	mla	ip, r7, ip, r3
	lsl	r3, ip, #10
	str	ip, [fp, #-44]
	str	r3, [r5, #3264]
	bl	FlashReadPages
	ldrh	r2, [r5, #98]
	ldr	r1, [r5, #3268]
	ldr	r0, [r8]
	add	r2, r2, #7
	asr	r2, r2, #3
	bl	ftl_memcpy
	ldr	ip, [fp, #-44]
.L1299:
	uxth	r0, ip
	add	r7, r7, #1
	add	r8, r8, #4
	bl	FtlBbmMapBadBlock
	b	.L1297
.L1298:
	mov	r1, r7
	bl	FlashGetBadBlockList
	ldr	r1, [r8]
	ldr	r0, [r5, #3268]
	bl	FtlBbt2Bitmap
	ldrh	r6, [r5, #98]
.L1301:
	sub	r6, r6, #1
	uxth	r6, r6
.L1300:
	ldrh	r0, [r4, #98]
	smlabb	r0, r0, r7, r6
	uxth	r0, r0
	bl	FtlBbmIsBadBlock
	cmp	r0, #1
	beq	.L1301
	mov	r1, #16
	ldr	r0, [r4, #3284]
	strh	r6, [r9]	@ movhi
	bl	__memzero
	mov	r1, #4096
	ldr	r0, [r4, #3280]
	bl	__memzero
	ldr	r3, .L1315+8
	strh	r3, [r10]	@ movhi
	mov	r3, #0
	str	r3, [r10, #4]
	ldrh	r3, [r9]
	ldrh	ip, [r4, #98]
	strh	r3, [r10, #2]	@ movhi
	ldrh	r3, [r9]
	ldr	r1, [r8]
	ldr	r0, [r4, #3268]
	mla	ip, r7, ip, r3
	lsl	r3, ip, #10
	str	ip, [fp, #-44]
	str	r3, [r4, #3264]
	ldr	r3, .L1315+12
	ldrh	r2, [r3]
	lsl	r2, r2, #2
	bl	ftl_memcpy
	mov	r2, #1
	ldr	r0, .L1315+4
	mov	r1, r2
	bl	FlashEraseBlocks
	mov	r3, #1
	ldr	r0, .L1315+4
	mov	r2, r3
	mov	r1, r3
	bl	FlashProgPages
	ldr	r3, [r4, #3260]
	ldr	ip, [fp, #-44]
	cmn	r3, #1
	bne	.L1299
	uxth	r0, ip
	bl	FtlBbmMapBadBlock
	b	.L1300
.L1305:
	uxth	r0, r0
	bl	FtlBbmMapBadBlock
	mov	r0, r5
	b	.L1304
.L1308:
	ldrh	r3, [r4, #152]
	cmp	r3, r6
	strheq	r5, [r4, #152]	@ movhi
	beq	.L1307
.L1309:
	strh	r5, [r4, #156]	@ movhi
.L1310:
	ldrh	r3, [r4, #152]
	mov	r5, #0
	ldr	r0, [r4, #3800]
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
.L1316:
	.align	2
.L1315:
	.word	.LANCHOR0+180
	.word	.LANCHOR0+3260
	.word	-3872
	.word	.LANCHOR0+3900
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
	ldr	r4, .L1326
	mov	r5, r1
	str	r2, [fp, #-128]
	add	r2, r0, r1
	ldrh	r3, [r4, #96]
	cmp	r2, r3
	mvnhi	r8, #0
	bhi	.L1317
	ldrh	r6, [r4, #108]
	mov	r8, #0
	lsr	r6, r0, r6
	lsl	r10, r6, #2
.L1319:
	cmp	r5, #0
	bne	.L1324
.L1317:
	mov	r0, r8
	sub	sp, fp, #40
	ldmfd	sp, {r4, r5, r6, r7, r8, r9, r10, fp, sp, pc}
.L1324:
	ldr	r3, [r4, #3884]
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
	beq	.L1321
	ldr	r2, [r4, #3848]
	sub	r0, fp, #124
	str	ip, [fp, #-120]
	str	r2, [fp, #-116]
	sub	r2, fp, #104
	str	r2, [fp, #-112]
	mov	r2, #1
	mov	r1, r2
	bl	FlashReadPages
.L1322:
	ldr	r3, [fp, #-132]
	lsl	ip, r9, #9
	ldr	r0, [r4, #3848]
	sub	r5, r5, r9
	mov	r2, ip
	ldr	r1, [fp, #-128]
	str	ip, [fp, #-136]
	add	r7, r7, r9
	add	r10, r10, #4
	add	r0, r0, r3, lsl #9
	bl	ftl_memcpy
	mov	r1, r6
	ldr	r2, [r4, #3848]
	ldr	r0, .L1326+4
	add	r6, r6, #1
	bl	FtlMapWritePage
	ldr	r3, [fp, #-128]
	cmn	r0, #1
	ldr	ip, [fp, #-136]
	mvneq	r8, #0
	add	r3, r3, ip
	str	r3, [fp, #-128]
	b	.L1319
.L1321:
	ldrh	r2, [r4, #110]
	ldr	r0, [r4, #3848]
	bl	ftl_memset
	b	.L1322
.L1327:
	.align	2
.L1326:
	.word	.LANCHOR0
	.word	.LANCHOR0+3948
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
	ldr	r3, .L1330
	ldr	r2, .L1330+4
	ldr	r1, [r3, #2636]
	cmp	r1, r2
	ldmfdne	sp, {fp, sp, pc}
	ldr	r2, .L1330+8
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
	ldr	r2, .L1330+12
	bl	FtlVendorPartWrite
	ldmfd	sp, {fp, sp, pc}
.L1331:
	.align	2
.L1330:
	.word	.LANCHOR0
	.word	1179929683
	.word	1342177348
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
	ldr	r2, .L1337
	ldrh	r3, [r2, #-8]
	cmp	r3, #31
	addls	r3, r3, #1
	movhi	r2, #32
	strhls	r3, [r2, #-8]	@ movhi
	movls	r2, #1
	cmp	r0, #0
	ldr	r3, .L1337+4
	bne	.L1334
	ldr	r1, [r3, #3860]
	ldr	r0, [r1, #20]
	ldr	r1, [r1, #16]
	add	r2, r2, r0
	cmp	r1, r2
	bcc	.L1335
.L1334:
	ldr	r2, [r3, #3860]
	mov	r0, #64
	ldr	r1, [r2, #16]
	str	r1, [r2, #20]
	ldr	r1, .L1337+8
	str	r1, [r2]
	movw	r1, #3852
	ldrh	r1, [r3, r1]
	ldr	r2, [r3, #3860]
	lsl	r3, r1, #9
	str	r3, [r2, #12]
	ldr	r3, [r2, #8]
	add	r3, r3, #1
	str	r3, [r2, #8]
	mov	r3, #0
	str	r3, [r2, #4]
	bl	FtlVendorPartWrite
	bl	Ftl_save_ext_data
.L1335:
	mov	r0, #0
	ldmfd	sp, {fp, sp, pc}
.L1338:
	.align	2
.L1337:
	.word	.LANCHOR3-4032
	.word	.LANCHOR0
	.word	1112818501
	.size	FtlEctTblFlush, .-FtlEctTblFlush
	.align	2
	.global	FtlVendorPartRead
	.syntax unified
	.arm
	.fpu softvfp
	.type	FtlVendorPartRead, %function
FtlVendorPartRead:
	@ args = 0, pretend = 0, frame = 92
	@ frame_needed = 1, uses_anonymous_args = 0
	mov	ip, sp
	push	{r4, r5, r6, r7, r8, r9, r10, fp, ip, lr, pc}
	sub	fp, ip, #4
	sub	sp, sp, #92
	mov	r9, r2
	ldr	r5, .L1348
	mov	r7, r1
	add	r1, r0, r1
	mov	r10, r0
	ldrh	r2, [r5, #96]
	cmp	r1, r2
	mvnhi	r8, #0
	bhi	.L1339
	ldrh	r6, [r5, #108]
	mov	r8, #0
	lsr	r6, r0, r6
	lsl	r6, r6, #2
.L1341:
	cmp	r7, #0
	bne	.L1346
.L1339:
	mov	r0, r8
	sub	sp, fp, #40
	ldmfd	sp, {r4, r5, r6, r7, r8, r9, r10, fp, sp, pc}
.L1346:
	ldr	r2, [r5, #3884]
	mov	r0, r10
	ldrh	r4, [r5, #58]
	ldr	ip, [r2, r6]
	mov	r1, r4
	str	ip, [fp, #-128]
	bl	__umodsi3
	sub	r4, r4, r0
	ldr	ip, [fp, #-128]
	uxth	r4, r4
	str	r0, [fp, #-132]
	cmp	r7, r4
	uxthcc	r4, r7
	cmp	ip, #0
	lsl	r3, r4, #9
	str	r3, [fp, #-128]
	beq	.L1343
	ldr	r2, [r5, #3848]
	sub	r3, fp, #104
	sub	r0, fp, #124
	str	r3, [fp, #-112]
	str	ip, [fp, #-120]
	str	r2, [fp, #-116]
	mov	r2, #1
	mov	r1, r2
	bl	FlashReadPages
	ldr	r1, [r5, #3848]
	mov	r0, r9
	ldr	r3, [fp, #-132]
	ldr	r2, [fp, #-124]
	add	r1, r1, r3, lsl #9
	cmn	r2, #1
	lsl	r2, r4, #9
	mvneq	r8, #0
	bl	ftl_memcpy
.L1345:
	ldr	r3, [fp, #-128]
	sub	r7, r7, r4
	add	r10, r10, r4
	add	r6, r6, #4
	add	r9, r9, r3
	b	.L1341
.L1343:
	lsl	r2, r4, #9
	mov	r1, ip
	mov	r0, r9
	bl	ftl_memset
	b	.L1345
.L1349:
	.align	2
.L1348:
	.word	.LANCHOR0
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
	ldr	r4, .L1352
	movw	r5, #3852
	mov	r0, #64
	ldr	r2, [r4, #3860]
	ldrh	r1, [r4, r5]
	bl	FtlVendorPartRead
	ldr	r3, [r4, #3860]
	ldr	r2, [r3]
	ldr	r3, .L1352+4
	cmp	r2, r3
	beq	.L1351
	ldr	r1, .L1352+8
	ldr	r0, .L1352+12
	bl	sftl_printk
	ldrh	r2, [r4, r5]
	mov	r1, #0
	ldr	r0, [r4, #3860]
	lsl	r2, r2, #9
	bl	ftl_memset
.L1351:
	mov	r0, #0
	ldmfd	sp, {r4, r5, fp, sp, pc}
.L1353:
	.align	2
.L1352:
	.word	.LANCHOR0
	.word	1112818501
	.word	.LC104
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
	ldr	r4, .L1357
	mov	r1, #1
	mov	r0, #0
	ldr	r2, .L1357+4
	bl	FtlVendorPartRead
	ldr	r5, .L1357+8
	ldr	r3, [r4, #2636]
	cmp	r3, r5
	beq	.L1355
	mov	r1, #512
	ldr	r0, .L1357+4
	bl	__memzero
	str	r5, [r4, #2636]
.L1355:
	ldr	r3, [r4, #2636]
	cmp	r3, r5
	bne	.L1356
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
.L1356:
	ldrh	r2, [r4, #92]
	ldr	r3, [r4, #2552]
	ldr	r0, [r4, #2548]
	ldrh	r1, [r4, #40]
	mla	r0, r0, r2, r3
	bl	__udivsi3
	str	r0, [r4, #2556]
	ldmfd	sp, {r4, r5, fp, sp, pc}
.L1358:
	.align	2
.L1357:
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
	ldr	r4, .L1371
	mov	r6, #0
	mov	r1, #255
	ldr	r10, .L1371+4
	ldr	r3, [r4, #3280]
	add	r5, r4, #2576
	ldr	r7, [r4, #3284]
	sub	r8, r5, #308
	add	r9, r5, #684
	str	r3, [r4, #3268]
	ldrh	r3, [r5]
	str	r7, [r4, #3272]
	str	r6, [r7, #12]
	strh	r3, [r7, #2]	@ movhi
	ldr	r3, .L1371+8
	strh	r3, [r7]	@ movhi
	ldr	r3, [r4, #2584]
	str	r6, [r7, #8]
	ldrh	r2, [r5, #-226]
	str	r3, [r7, #4]
	ldr	r3, .L1371+12
	str	r3, [r4, #2268]
	ldr	r3, .L1371+16
	str	r3, [r4, #2272]
	ldrh	r3, [r5, #6]
	strh	r3, [r8, #8]	@ movhi
	ldrh	r3, [r4, #54]
	strb	r3, [r4, #2278]
	movw	r3, #2348
	ldrh	r3, [r4, r3]
	strh	r3, [r8, #14]	@ movhi
	ldrb	r3, [r4, #2354]	@ zero_extendqisi2
	orr	r3, r3, r2, lsl #6
	ldrh	r2, [r5, #-178]
	strh	r3, [r8, #16]	@ movhi
	ldrb	r3, [r4, #2356]	@ zero_extendqisi2
	strb	r3, [r4, #2279]
	movw	r3, #2396
	ldrh	r3, [r4, r3]
	strh	r3, [r8, #18]	@ movhi
	ldrb	r3, [r4, #2402]	@ zero_extendqisi2
	orr	r3, r3, r2, lsl #6
	strh	r3, [r8, #20]	@ movhi
	ldrb	r3, [r4, #2404]	@ zero_extendqisi2
	strb	r3, [r4, #2280]
	movw	r3, #2444
	ldrh	r3, [r4, r3]
	strh	r3, [r8, #22]	@ movhi
	ldrh	r2, [r5, #-130]
	ldrb	r3, [r4, #2450]	@ zero_extendqisi2
	ldr	r0, [r4, #3268]
	orr	r3, r3, r2, lsl #6
	ldrh	r2, [r4, #110]
	strh	r3, [r8, #24]	@ movhi
	ldrb	r3, [r4, #2452]	@ zero_extendqisi2
	strb	r3, [r4, #2281]
	ldr	r3, [r4, #2548]
	str	r3, [r4, #2300]
	ldr	r3, [r4, #2540]
	str	r3, [r4, #2308]
	ldr	r3, [r4, #2544]
	str	r3, [r4, #2304]
	bl	ftl_memset
	mov	r1, r8
	mov	r2, #48
	ldr	r0, [r4, #3268]
	movw	r8, #65535
	bl	ftl_memcpy
	ldrh	r2, [r4, #40]
	ldr	r0, [r4, #3268]
	ldr	r1, [r4, #2324]
	lsl	r2, r2, #1
	add	r0, r0, #48
	bl	ftl_memcpy
	ldrh	r0, [r4, #40]
	ldr	r3, [r4, #3268]
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
.L1360:
	ldr	r3, [r4, #3280]
	mov	r0, r9
	ldrh	r2, [r5]
	str	r3, [r4, #3268]
	ldr	r3, [r4, #3284]
	str	r3, [r4, #3272]
	ldrh	r3, [r5, #2]
	orr	r3, r3, r2, lsl #10
	str	r3, [r4, #3264]
	mov	r3, #1
	mov	r2, r3
	mov	r1, r3
	bl	FlashProgPages
	ldrh	r3, [r4, #104]
	ldrh	r2, [r5, #2]
	sub	r3, r3, #1
	cmp	r2, r3
	blt	.L1361
	ldrh	r3, [r5]
	ldrh	r8, [r5, #4]
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
	str	r2, [r4, #3264]
	str	r3, [r7, #4]
	mov	r3, #1
	strh	r0, [r7, #2]	@ movhi
	mov	r2, r3
	mov	r1, r3
	mov	r0, r9
	bl	FlashProgPages
.L1361:
	ldrh	r3, [r5, #2]
	ldr	r2, [r4, #3260]
	add	r3, r3, #1
	uxth	r3, r3
	cmn	r2, #1
	strh	r3, [r5, #2]	@ movhi
	bne	.L1362
	cmp	r3, #1
	bne	.L1363
	movw	r2, #1124
	mov	r1, r10
	ldr	r0, .L1371+20
	bl	sftl_printk
.L1363:
	ldrh	r3, [r5, #2]
	add	r6, r6, #1
	uxth	r6, r6
	cmp	r3, #1
	ldrheq	r3, [r4, #104]
	subeq	r3, r3, #1
	strheq	r3, [r5, #2]	@ movhi
	cmp	r6, #3
	bls	.L1360
	mov	r2, r6
	ldr	r1, [r4, #3264]
	ldr	r0, .L1371+24
	bl	sftl_printk
.L1366:
	b	.L1366
.L1362:
	cmp	r3, #1
	beq	.L1360
	movw	r3, #65535
	cmp	r8, r3
	beq	.L1367
	mov	r1, #1
	mov	r0, r8
	bl	FtlFreeSysBlkQueueIn
.L1367:
	mov	r0, #0
	ldmfd	sp, {r4, r5, r6, r7, r8, r9, r10, fp, sp, pc}
.L1372:
	.align	2
.L1371:
	.word	.LANCHOR0
	.word	.LANCHOR1+454
	.word	-3932
	.word	1179929683
	.word	1342177348
	.word	.LC1
	.word	.LC105
	.size	FtlVpcTblFlush, .-FtlVpcTblFlush
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
	ldr	r4, .L1385
	ldr	r1, .L1385+4
	ldr	r0, .L1385+8
	bl	sftl_printk
	ldrh	r2, [r4, #42]
	mov	r1, #0
	ldr	r0, [r4, #3864]
	lsl	r2, r2, #1
	bl	ftl_memset
.L1374:
	ldr	r3, [r4, #2504]
	cmp	r5, r3
	bcc	.L1376
	ldr	r9, .L1385+12
	mov	r6, #0
	ldr	r10, .L1385+16
.L1377:
	ldrh	r3, [r4, #40]
	uxth	r5, r6
	cmp	r3, r5
	bhi	.L1381
	ldmib	sp, {r4, r5, r6, r7, r8, r9, r10, fp, sp, pc}
.L1376:
	mov	r2, #0
	sub	r1, fp, #44
	mov	r0, r5
	bl	log2phys
	ldr	r0, [fp, #-44]
	cmn	r0, #1
	beq	.L1375
	ubfx	r0, r0, #10, #16
	bl	P2V_block_in_plane
	ldr	r2, [r4, #3864]
	lsl	r0, r0, #1
	ldrh	r3, [r2, r0]
	add	r3, r3, #1
	strh	r3, [r2, r0]	@ movhi
.L1375:
	add	r5, r5, #1
	b	.L1374
.L1381:
	uxth	r8, r6
	ldr	r3, [r4, #2324]
	movw	r1, #65535
	lsl	r7, r8, #1
	ldrh	r2, [r3, r7]
	ldr	r3, [r4, #3864]
	ldrh	r3, [r3, r7]
	cmp	r2, r1
	cmpne	r2, r3
	beq	.L1379
	mov	r1, r8
	mov	r0, r9
	bl	sftl_printk
	ldrh	r3, [r10]
	cmp	r3, r5
	beq	.L1379
	ldr	r3, .L1385+20
	ldrh	r2, [r3]
	cmp	r2, r5
	beq	.L1379
	ldrh	r3, [r3, #-48]
	cmp	r3, r5
	beq	.L1379
	ldr	r3, [r4, #3864]
	mov	r0, r8
	ldrh	r2, [r3, r7]
	ldr	r3, [r4, #2324]
	strh	r2, [r3, r7]	@ movhi
	bl	update_vpc_list
	bl	l2p_flush
	bl	FtlVpcTblFlush
.L1379:
	add	r6, r6, #1
	b	.L1377
.L1386:
	.align	2
.L1385:
	.word	.LANCHOR0
	.word	.LANCHOR1+469
	.word	.LC95
	.word	.LC106
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
	push	{r4, r5, r6, fp, ip, lr, pc}
	sub	fp, ip, #4
	ldr	r4, .L1397
	mov	r6, r0
	ldrh	r5, [r0]
	ldrh	r3, [r4, #40]
	cmp	r3, r5
	bcs	.L1388
	movw	r2, #2681
	ldr	r1, .L1397+4
	ldr	r0, .L1397+8
	bl	sftl_printk
.L1388:
	movw	r3, #65535
	cmp	r5, r3
	beq	.L1389
	ldr	r2, [r4, #2324]
	lsl	r3, r5, #1
	mov	r0, r5
	ldrh	r3, [r2, r3]
	cmp	r3, #0
	beq	.L1390
	bl	INSERT_DATA_LIST
.L1389:
	mov	r3, #1
	strb	r3, [r6, #8]
	movw	r3, #3992
	ldrh	r0, [r4, r3]
	movw	r3, #65535
	cmp	r0, r3
	beq	.L1391
	cmp	r5, r0
	bne	.L1392
	ldr	r2, [r4, #2324]
	lsl	r3, r0, #1
	ldrh	r3, [r2, r3]
	cmp	r3, #0
	beq	.L1393
.L1392:
	bl	update_vpc_list
.L1393:
	mvn	r2, #0
	movw	r3, #3992
	strh	r2, [r4, r3]	@ movhi
.L1391:
	mov	r0, r6
	bl	allocate_data_superblock
	bl	l2p_flush
	mov	r0, #0
	bl	FtlEctTblFlush
	bl	FtlVpcTblFlush
	mov	r0, #0
	ldmfd	sp, {r4, r5, r6, fp, sp, pc}
.L1390:
	bl	INSERT_FREE_LIST
	b	.L1389
.L1398:
	.align	2
.L1397:
	.word	.LANCHOR0
	.word	.LANCHOR1+490
	.word	.LC1
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
	ldr	r6, .L1413
	mov	r4, r3
	ldr	r10, .L1413+4
	mov	r2, #0
	ldrb	r3, [r3, #9]	@ zero_extendqisi2
	mov	r5, r0
	mov	r9, r1
	bl	FlashProgPages
.L1400:
	cmp	r7, r9
	beq	.L1407
	mov	r8, #0
	b	.L1408
.L1403:
	ldr	r0, [r5, #4]
	ubfx	r0, r0, #10, #16
	bl	P2V_block_in_plane
	ldrh	r3, [r4]
	cmp	r3, r0
	bne	.L1401
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
.L1401:
	ldrh	r3, [r4, #4]
	cmp	r3, #0
	bne	.L1402
	mov	r0, r4
	bl	allocate_new_data_superblock
.L1402:
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
.L1408:
	ldr	r3, [r5]
	cmn	r3, #1
	beq	.L1403
	ldrb	r2, [r4, #6]	@ zero_extendqisi2
	ldrh	r3, [r6, #32]
	cmp	r2, r3
	bcc	.L1404
	movw	r2, #933
	mov	r1, r10
	ldr	r0, .L1413+8
	bl	sftl_printk
.L1404:
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
	beq	.L1405
	ldr	r2, [r6, #2324]
	lsl	r3, r0, #1
	ldrh	r2, [r2, r3]
	cmp	r2, #0
	bne	.L1406
	mov	r1, r0
	ldr	r0, .L1413+12
	bl	sftl_printk
.L1406:
	mov	r0, r8
	bl	decrement_vpc_count
.L1405:
	add	r7, r7, #1
	add	r5, r5, #20
	b	.L1400
.L1407:
	ldrb	r2, [r4, #6]	@ zero_extendqisi2
	ldrh	r3, [r6, #32]
	cmp	r2, r3
	bcc	.L1399
	mov	r2, #948
	ldr	r1, .L1413+4
	ldr	r0, .L1413+8
	bl	sftl_printk
.L1399:
	sub	sp, fp, #40
	ldmfd	sp, {r4, r5, r6, r7, r8, r9, r10, fp, sp, pc}
.L1414:
	.align	2
.L1413:
	.word	.LANCHOR0
	.word	.LANCHOR1+519
	.word	.LC1
	.word	.LC107
	.size	FtlProgPages, .-FtlProgPages
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
	push	{r4, fp, ip, lr, pc}
	sub	fp, ip, #4
	ldr	r4, .L1420
	ldr	r0, [r4, #3988]
	cmp	r0, #0
	beq	.L1416
	ldr	r3, .L1420+4
	mov	r2, #0
	mov	r1, #1
	ldr	r3, [r3, #-4036]
	bl	FtlProgPages
	mov	r3, #0
	str	r3, [r4, #3988]
.L1416:
	mov	r0, #0
	ldmfd	sp, {r4, fp, sp, pc}
.L1421:
	.align	2
.L1420:
	.word	.LANCHOR0
	.word	.LANCHOR3
	.size	FtlCacheWriteBack, .-FtlCacheWriteBack
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
	bl	FtlCacheWriteBack
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
	ldr	r6, .L1442
	mov	r7, r0
	mov	r5, r1
	ldr	r3, [r6, #140]
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
	bl	FtlCacheWriteBack
	ldrh	r4, [r6, #58]
	mov	r0, r7
	mov	r1, r4
	bl	__udivsi3
	smulbb	r3, r0, r4
	mov	r8, r0
	sub	r7, r7, r3
	uxth	r7, r7
	cmp	r7, #0
	beq	.L1430
	sub	r4, r4, r7
	add	r8, r0, #1
	cmp	r4, r5
	movcs	r4, r5
	uxth	r4, r4
	sub	r5, r5, r4
.L1430:
	ldr	r4, .L1442+4
	mvn	r3, #0
	str	r3, [fp, #-36]
.L1431:
	ldrh	r3, [r6, #58]
	cmp	r5, r3
	bcs	.L1433
	ldr	r3, .L1442+4
	ldr	r2, [r3, #-4032]
	cmp	r2, #32
	bls	.L1434
	mov	r4, #0
	str	r4, [r3, #-4032]
	bl	l2p_flush
	bl	FtlVpcTblFlush
	b	.L1434
.L1433:
	mov	r2, #0
	sub	r1, fp, #40
	mov	r0, r8
	bl	log2phys
	ldr	r3, [fp, #-40]
	cmn	r3, #1
	beq	.L1432
	ldr	r3, [r4, #-4032]
	mov	r2, #1
	sub	r1, fp, #36
	mov	r0, r8
	add	r3, r3, #1
	str	r3, [r4, #-4032]
	ldr	r3, [r6, #2512]
	add	r3, r3, #1
	str	r3, [r6, #2512]
	bl	log2phys
	ldr	r0, [fp, #-40]
	ubfx	r0, r0, #10, #16
	bl	P2V_block_in_plane
	bl	decrement_vpc_count
.L1432:
	ldrh	r3, [r6, #58]
	add	r8, r8, #1
	sub	r5, r5, r3
	b	.L1431
.L1443:
	.align	2
.L1442:
	.word	.LANCHOR0
	.word	.LANCHOR3
	.size	FtlDiscard, .-FtlDiscard
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
	ldr	r4, .L1470
	movw	r2, #65535
	ldrh	r5, [r4, r3]
	ldrh	r1, [r4, #102]
	cmp	r5, r2
	bne	.L1445
.L1455:
	mov	r3, #0
	str	r3, [r4, #4000]
	movw	r3, #2444
	ldrh	r2, [r4, r3]
	movw	r3, #65535
	cmp	r2, r3
	bne	.L1468
.L1446:
	mov	r0, #0
	ldmib	sp, {r4, r5, r6, r7, r8, r9, r10, fp, sp, pc}
.L1445:
	cmp	r0, #0
	beq	.L1448
	ldr	r3, .L1470+4
	ldrh	r0, [r3, #4]
	cmp	r0, r2
	beq	.L1449
.L1450:
	mov	r1, #2
.L1448:
	ldr	r0, .L1470+8
	bl	FtlGcScanTempBlk
	cmn	r0, #1
	str	r0, [fp, #-44]
	beq	.L1451
	ldr	r2, [r4, #2328]
	lsl	r5, r5, #1
	ldrh	r3, [r2, r5]
	cmp	r3, #4
	bls	.L1452
	sub	r3, r3, #5
	mov	r0, #1
	strh	r3, [r2, r5]	@ movhi
	bl	FtlEctTblFlush
.L1452:
	ldr	r3, [r4, #4000]
	cmp	r3, #0
	bne	.L1453
	ldr	r3, [r4, #2732]
	ldr	r0, [fp, #-44]
	add	r3, r3, #1
	ubfx	r0, r0, #10, #16
	str	r3, [r4, #2732]
	bl	FtlBbmMapBadBlock
	bl	FtlBbmTblFlush
.L1453:
	mov	r3, #0
	str	r3, [r4, #4000]
.L1464:
	mov	r0, #1
	ldmib	sp, {r4, r5, r6, r7, r8, r9, r10, fp, sp, pc}
.L1449:
	mov	r2, #0
	strh	r2, [r3, #4]	@ movhi
	movw	r3, #2344
	ldrh	r3, [r4, r3]
	cmp	r3, #17
	bhi	.L1450
	b	.L1448
.L1451:
	ldr	r3, .L1470+4
	ldrh	r2, [r3, #4]
	movw	r3, #65535
	cmp	r2, r3
	bne	.L1464
	b	.L1455
.L1468:
	bl	FtlCacheWriteBack
	ldrb	r1, [r4, #2451]	@ zero_extendqisi2
	ldrh	r3, [r4, #102]
	ldr	r6, .L1470+12
	mul	r3, r3, r1
	ldrh	r2, [r6]
	cmp	r2, r3
	beq	.L1456
	mov	r2, #162
	ldr	r1, .L1470+16
	ldr	r0, .L1470+20
	bl	sftl_printk
.L1456:
	ldrb	r2, [r4, #2451]	@ zero_extendqisi2
	movw	r3, #2444
	ldrh	r0, [r4, #102]
	mov	r5, #0
	ldrh	r3, [r4, r3]
	ldr	r1, [r4, #2324]
	ldr	r10, .L1470+20
	smulbb	r2, r2, r0
	lsl	r3, r3, #1
	strh	r2, [r1, r3]	@ movhi
	ldr	r2, [r4, #2528]
	ldrh	r3, [r6]
	add	r3, r3, r2
	str	r3, [r4, #2528]
.L1457:
	ldrh	r2, [r6]
	uxth	r3, r5
	cmp	r2, r3
	bhi	.L1461
	movw	r0, #65535
	bl	decrement_vpc_count
	movw	r3, #2444
	ldr	r1, [r4, #2324]
	ldrh	r0, [r4, r3]
	mov	r5, r3
	lsl	r2, r0, #1
	ldrh	r2, [r1, r2]
	cmp	r2, #0
	beq	.L1462
	bl	INSERT_DATA_LIST
.L1463:
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
	b	.L1446
.L1461:
	uxth	r3, r5
	mov	r7, #12
	ldr	r8, [r4, #3196]
	mul	r7, r7, r3
	ldr	r2, [r4, #2504]
	add	r9, r8, r7
	ldr	r1, [r9, #8]
	cmp	r1, r2
	bcc	.L1458
	mov	r2, #168
	ldr	r1, .L1470+16
	mov	r0, r10
	bl	sftl_printk
.L1458:
	mov	r2, #0
	sub	r1, fp, #44
	ldr	r0, [r9, #8]
	bl	log2phys
	ldr	r2, [fp, #-44]
	ldr	r0, [r8, r7]
	cmp	r0, r2
	bne	.L1459
	ubfx	r0, r0, #10, #16
	bl	P2V_block_in_plane
	mov	r2, #1
	mov	r7, r0
	add	r1, r9, #4
	ldr	r0, [r9, #8]
	bl	log2phys
	mov	r0, r7
.L1469:
	bl	decrement_vpc_count
.L1460:
	add	r5, r5, #1
	b	.L1457
.L1459:
	ldr	r3, [r9, #4]
	cmp	r2, r3
	beq	.L1460
	ldr	r3, .L1470+8
	ldrh	r0, [r3]
	b	.L1469
.L1462:
	bl	INSERT_FREE_LIST
	b	.L1463
.L1471:
	.align	2
.L1470:
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
	ldr	r4, .L1474
	ldr	r5, .L1474+4
	ldrh	r1, [r4, #102]
	mov	r0, r5
	bl	FtlGcScanTempBlk
	ldrh	r2, [r5, #2]
	ldrh	r3, [r4, #102]
	cmp	r2, r3
	ldmfdcc	sp, {r4, r5, fp, sp, pc}
	add	r0, r4, #3904
	bl	FtlMapBlkWriteDump_data
	mov	r0, #0
	bl	FtlGcFreeTempBlock
	mov	r3, #0
	str	r3, [r4, #4000]
	ldmfd	sp, {r4, r5, fp, sp, pc}
.L1475:
	.align	2
.L1474:
	.word	.LANCHOR0
	.word	.LANCHOR0+2444
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
	ldr	r4, .L1477
	mov	r6, #0
	add	r5, r4, #2336
	str	r6, [r4, #4012]
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
.L1478:
	.align	2
.L1477:
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
	ldr	r4, .L1486
	mov	r3, #0
	mov	r5, #0
	mov	r6, #20
	mov	r2, r3
	ldr	r1, [r4, #3164]
	ldr	r0, [r4, #3836]
	bl	FlashProgPages
.L1480:
	ldr	r1, [r4, #3164]
	uxth	r3, r5
	cmp	r1, r3
	bhi	.L1483
	ldr	r0, [r4, #3836]
	bl	FtlGcBufFree
	ldr	r3, .L1486+4
	mov	r0, #0
	str	r0, [r4, #3164]
	ldrh	r3, [r3, #4]
	cmp	r3, r0
	ldmfdne	sp, {r4, r5, r6, fp, sp, pc}
	mov	r0, #1
	bl	FtlGcFreeTempBlock
	b	.L1485
.L1483:
	mul	r3, r6, r3
	ldr	r2, [r4, #3836]
	add	r5, r5, #1
	ldr	r0, [r2, r3]
	add	r1, r2, r3
	cmn	r0, #1
	bne	.L1481
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
	ldr	r2, [r4, #3836]
	add	r3, r2, r3
	ldr	r0, [r3, #4]
	ubfx	r0, r0, #10, #16
	bl	FtlBbmMapBadBlock
	bl	FtlBbmTblFlush
	bl	FtlGcPageVarInit
.L1485:
	mov	r0, #1
	ldmfd	sp, {r4, r5, r6, fp, sp, pc}
.L1481:
	ldr	r3, [r1, #12]
	ldr	r1, [r1, #4]
	ldr	r2, [r3, #8]
	ldr	r0, [r3, #12]
	bl	FtlGcUpdatePage
	b	.L1480
.L1487:
	.align	2
.L1486:
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
	ldr	r4, .L1491
	movw	r3, #2444
	ldrh	r2, [r4, r3]
	movw	r3, #65535
	cmp	r2, r3
	beq	.L1489
	ldr	r3, .L1491+4
	ldrh	r3, [r3, #4]
	cmp	r3, #0
	bne	.L1490
.L1489:
	bl	FtlCacheWriteBack
	mov	r0, #0
	mov	r5, #0
	bl	FtlGcFreeTempBlock
	ldr	r0, .L1491+4
	strb	r5, [r4, #2452]
	bl	allocate_data_superblock
	movw	r3, #3192
	strh	r5, [r4, r3]	@ movhi
	ldr	r3, .L1491+8
	strh	r5, [r3]	@ movhi
	bl	l2p_flush
	mov	r0, r5
	bl	FtlEctTblFlush
	bl	FtlVpcTblFlush
.L1490:
	ldr	r0, .L1491+4
	bl	get_new_active_ppa
	ldmfd	sp, {r4, r5, fp, sp, pc}
.L1492:
	.align	2
.L1491:
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
	ldr	r3, .L1606
	ldr	r0, [r3, #3820]
	mov	r5, r3
	cmp	r0, #0
	movne	r0, #0
	bne	.L1493
	add	r2, r3, #2336
	ldrh	r2, [r2]
	cmp	r2, #47
	bls	.L1493
	ldr	r2, .L1606+4
	ldrh	r1, [r2, #4]
	movw	r2, #65535
	cmp	r1, r2
	bne	.L1495
.L1498:
	movw	r3, #3204
	movw	r0, #65535
	ldrh	r1, [r5, r3]
	cmp	r1, r0
	bne	.L1496
.L1497:
	ldr	r3, [r5, #3152]
	movw	r2, #2588
	ldrh	r2, [r5, r2]
	movw	r4, #65535
	add	r3, r3, #1
	add	r3, r3, r6, lsl #7
	cmp	r2, r4
	str	r3, [r5, #3152]
	bne	.L1499
	movw	r1, #2444
	ldrh	r4, [r5, r1]
	cmp	r4, r2
	movne	r4, r2
	bne	.L1499
	movw	r2, #3202
	ldrh	r7, [r5, r2]
	cmp	r7, r4
	bne	.L1499
	movw	r10, #2344
	ldrh	r2, [r5, r10]
	cmp	r2, #24
	movcc	r2, #5120
	movcs	r2, #1024
	cmp	r3, r2
	movls	r4, r7
	bls	.L1499
	ldr	r3, .L1606+8
	mov	r9, #0
	str	r9, [r5, #3152]
	strh	r9, [r3, #-12]	@ movhi
	bl	GetSwlReplaceBlock
	cmp	r0, r7
	mov	r4, r0
	movne	r7, r0
	bne	.L1501
	movw	r8, #3150
	ldrh	r2, [r5, r10]
	ldrh	r3, [r5, r8]
	cmp	r2, r3
	bcs	.L1502
	mov	r0, #64
	bl	List_get_gc_head_node
	uxth	r3, r0
	cmp	r3, r4
	beq	.L1504
	mov	r0, r3
	ldr	r3, [r5, #2324]
	lsl	r0, r0, #1
	ldrh	r3, [r3, r0]
	cmp	r3, #7
	bhi	.L1505
	mov	r0, r9
	bl	List_get_gc_head_node
	uxth	r7, r0
	mov	r3, #128
	strh	r3, [r5, r8]	@ movhi
	cmp	r7, r4
	bne	.L1501
.L1504:
	bl	FtlGcReFreshBadBlk
.L1499:
	movw	r2, #65535
	sub	r3, r4, r2
	clz	r3, r3
	lsr	r3, r3, #5
	cmp	r6, #0
	movne	r6, #0
	andeq	r6, r3, #1
	cmp	r6, #0
	beq	.L1507
	movw	r3, #2344
	ldrh	r3, [r5, r3]
	cmp	r3, #24
	movhi	r6, #1
	bhi	.L1508
	ldrh	r6, [r5, #102]
	cmp	r3, #16
	lsrhi	r6, r6, #5
	bhi	.L1508
	cmp	r3, #12
	lsrhi	r6, r6, #4
	bhi	.L1508
	cmp	r3, #8
	lsrhi	r6, r6, #2
.L1508:
	movw	r2, #3148
	ldrh	r1, [r5, r2]
	cmp	r1, r3
	bcs	.L1512
	movw	r3, #2444
	movw	r1, #65535
	ldrh	r3, [r5, r3]
	cmp	r3, r1
	bne	.L1513
	movw	r1, #3202
	ldrh	r1, [r5, r1]
	cmp	r1, r3
	bne	.L1513
	ldr	r3, .L1606+8
	ldrh	r0, [r3, #-12]
	cmp	r0, #0
	bne	.L1514
	ldr	r3, [r5, #2504]
	ldr	r1, [r5, #2500]
	add	r3, r3, r3, lsl #1
	cmp	r1, r3, lsr #2
	movcs	r3, #18
	bcs	.L1599
.L1514:
	movw	r3, #2572
	movw	r2, #3148
	ldrh	r3, [r5, r3]
	add	r3, r3, r3, lsl #1
	asr	r3, r3, #2
.L1599:
	strh	r3, [r5, r2]	@ movhi
	mov	r3, #0
	str	r3, [r5, #3160]
.L1493:
	sub	sp, fp, #40
	ldmfd	sp, {r4, r5, r6, r7, r8, r9, r10, fp, sp, pc}
.L1495:
	movw	r1, #2444
	ldrh	r3, [r3, r1]
	cmp	r3, r2
	beq	.L1498
	mov	r0, #1
	bl	FtlGcFreeTempBlock
	cmp	r0, #0
	beq	.L1498
	mov	r0, #1
	b	.L1493
.L1496:
	movw	r2, #3202
	ldrh	ip, [r5, r2]
	cmp	ip, r0
	strheq	r1, [r5, r2]	@ movhi
	mvneq	r2, #0
	strheq	r2, [r5, r3]	@ movhi
	b	.L1497
.L1505:
	mov	r3, #64
.L1598:
	strh	r3, [r5, r8]	@ movhi
	b	.L1504
.L1502:
	mov	r3, #80
	b	.L1598
.L1501:
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
	ldr	r0, .L1606+12
	str	r1, [sp]
	mov	r1, r7
	bl	sftl_printk
	b	.L1504
.L1513:
	movw	r3, #2572
	movw	r2, #3148
	ldrh	r3, [r5, r3]
	add	r3, r3, r3, lsl #1
	asr	r3, r3, #2
	strh	r3, [r5, r2]	@ movhi
.L1512:
	movw	r3, #3206
	movw	r4, #65535
	ldrh	r3, [r5, r3]
	cmp	r3, #0
	addne	r6, r6, #32
	uxthne	r6, r6
.L1518:
	movw	r3, #2588
	ldrh	r2, [r5, r3]
	movw	r1, #65535
	cmp	r2, r1
	bne	.L1527
	cmp	r4, r2
	strhne	r4, [r5, r3]	@ movhi
	bne	.L1529
	movw	r2, #3202
	ldrh	r3, [r5, r2]
	cmp	r3, r4
	beq	.L1529
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
.L1529:
	movw	r7, #2588
	mov	r3, #0
	ldrh	r0, [r5, r7]
	strb	r3, [r5, #2596]
	movw	r3, #65535
	cmp	r0, r3
	beq	.L1527
	bl	IsBlkInGcList
	cmp	r0, #0
	ldr	r8, .L1606+16
	mvnne	r3, #0
	strhne	r3, [r5, r7]	@ movhi
	movw	r7, #2588
	ldrh	r2, [r5, r7]
	movw	r3, #65535
	cmp	r2, r3
	beq	.L1527
	mov	r0, r8
	bl	make_superblock
	ldr	r2, .L1606+8
	mov	r3, #0
	strh	r3, [r8, #2]	@ movhi
	strb	r3, [r5, #2594]
	strh	r3, [r2, #-10]	@ movhi
	ldrh	r3, [r5, r7]
	ldr	r1, [r5, #2324]
	lsl	r3, r3, #1
	ldrh	r3, [r1, r3]
	strh	r3, [r2, #-8]	@ movhi
.L1527:
	movw	r3, #2588
	movw	r2, #2348
	ldrh	r3, [r5, r3]
	ldrh	r2, [r5, r2]
	cmp	r2, r3
	beq	.L1533
	movw	r2, #2396
	ldrh	r2, [r5, r2]
	cmp	r2, r3
	ldrne	r7, .L1606
	movne	r5, r7
	bne	.L1534
.L1533:
	mvn	r2, #0
	movw	r3, #2588
.L1604:
	strh	r2, [r5, r3]	@ movhi
	ldr	r3, .L1606+8
.L1605:
	ldrh	r0, [r3, #-12]
	b	.L1493
.L1507:
	movw	r1, #2444
	ldrh	r7, [r5, r1]
	cmp	r7, r2
	bne	.L1576
	movw	r2, #3202
	ldrh	r2, [r5, r2]
	cmp	r2, r7
	movne	r3, #0
	andeq	r3, r3, #1
	cmp	r3, #0
	beq	.L1576
	movw	r3, #2588
	ldrh	r3, [r5, r3]
	cmp	r3, r7
	beq	.L1519
.L1524:
	mov	r4, r7
.L1576:
	mov	r6, #1
	b	.L1518
.L1519:
	movw	r3, #2344
	movw	r2, #3148
	ldrh	r8, [r5, r3]
	ldrh	r3, [r5, r2]
	str	r6, [r5, #3160]
	ldr	r4, .L1606+8
	cmp	r3, r8
	bcs	.L1520
	ldrh	r3, [r4, #-12]
	cmp	r3, #0
	bne	.L1521
	ldr	r3, [r5, #2504]
	ldr	r1, [r5, #2500]
	add	r3, r3, r3, lsl #1
	cmp	r1, r3, lsr #2
	movcs	r3, #18
	bcs	.L1600
.L1521:
	movw	r3, #2572
	movw	r2, #3148
	ldrh	r3, [r5, r3]
	add	r3, r3, r3, lsl #1
	asr	r3, r3, #2
.L1600:
	strh	r3, [r5, r2]	@ movhi
.L1603:
	bl	FtlReadRefresh
	ldrh	r0, [r4, #-12]
	b	.L1493
.L1520:
	ldrh	r0, [r4, #-12]
	cmp	r0, #0
	bne	.L1524
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
	ble	.L1525
	sub	r6, r6, #1
	cmp	r8, r6
	bge	.L1603
.L1525:
	cmp	r2, #0
	bne	.L1524
	movw	r0, #65535
	bl	decrement_vpc_count
	movw	r3, #2344
	ldrh	r0, [r5, r3]
	add	r0, r0, #1
	b	.L1493
.L1537:
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
	bne	.L1536
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
	bgt	.L1540
	cmp	ip, #8
	cmphi	r10, #48
	bls	.L1541
	ldr	ip, .L1606+20
	ldrh	ip, [ip]
	cmp	ip, #35
	bhi	.L1541
.L1540:
	ldr	lr, .L1606+24
	mov	ip, #0
	strh	ip, [lr]	@ movhi
.L1541:
	ldrh	ip, [r0, r2]
	movw	r0, #65535
	cmp	r3, ip
	cmple	r4, r0
	moveq	r3, #1
	movne	r3, #0
	bne	.L1542
	mvn	r2, #0
	movw	r3, #2588
	strh	r2, [r5, r3]	@ movhi
	mov	r2, #0
	movw	r3, #3156
	b	.L1604
.L1542:
	cmp	ip, #0
	bne	.L1543
	bl	decrement_vpc_count
	ldr	r3, .L1606+24
	ldr	r2, .L1606+24
	ldrh	r3, [r3]
	add	r3, r3, #1
	strh	r3, [r2]	@ movhi
	b	.L1536
.L1543:
	strb	r3, [r5, #2596]
	ldr	r3, .L1606+28
	ldrh	r3, [r3]
	cmp	r3, r1
	bne	.L1544
	movw	r2, #658
	ldr	r1, .L1606+32
	ldr	r0, .L1606+36
	bl	sftl_printk
.L1544:
	ldr	r3, .L1606+40
	ldrh	r2, [r8]
	ldrh	r3, [r3]
	cmp	r2, r3
	bne	.L1545
	movw	r2, #659
	ldr	r1, .L1606+32
	ldr	r0, .L1606+36
	bl	sftl_printk
.L1545:
	ldr	r3, .L1606+44
	ldrh	r2, [r8]
	ldrh	r3, [r3]
	cmp	r2, r3
	bne	.L1546
	mov	r2, #660
	ldr	r1, .L1606+32
	ldr	r0, .L1606+36
	bl	sftl_printk
.L1546:
	ldr	r0, .L1606+16
	bl	make_superblock
	ldrh	r2, [r8]
	mov	r3, #0
	ldr	r1, .L1606+48
	ldr	r0, [r5, #2324]
	lsl	r2, r2, #1
	strh	r3, [r1]	@ movhi
	ldrh	r2, [r0, r2]
	strh	r3, [r8, #2]	@ movhi
	strb	r3, [r5, #2594]
	strh	r2, [r1, #2]	@ movhi
.L1535:
	bl	FtlReadRefresh
	mov	r3, #1
	str	r3, [r5, #3820]
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
.L1602:
	str	r3, [fp, #-52]
	ldrh	r3, [fp, #-52]
	ldr	r8, .L1606+16
	cmp	r6, r3
	bls	.L1556
	ldrh	r9, [r8, #2]
	mov	r2, #0
	ldr	r3, [fp, #-52]
	movw	r10, #65535
	ldrh	ip, [r7, #32]
	ldr	r0, [r7, #3180]
	add	r9, r9, r3
	add	r3, r8, #14
	mov	r8, r2
	b	.L1557
.L1550:
	ldrh	r1, [r3, #2]!
	add	r2, r2, #1
	cmp	r1, r10
	movne	lr, #20
	orrne	r1, r9, r1, lsl #10
	mlane	lr, lr, r8, r0
	addne	r8, r8, #1
	uxthne	r8, r8
	strne	r1, [lr, #4]
.L1557:
	uxth	r1, r2
	cmp	ip, r1
	bhi	.L1550
	ldrb	r2, [r7, #2596]	@ zero_extendqisi2
	mov	r1, r8
	bl	FlashReadPages
	mov	r3, #0
.L1601:
	str	r3, [fp, #-48]
	ldrh	r3, [fp, #-48]
	cmp	r3, r8
	ldrcs	r3, [fp, #-52]
	addcs	r3, r3, #1
	bcs	.L1602
.L1555:
	ldr	r3, [fp, #-48]
	mov	r9, #20
	mul	r9, r9, r3
	ldr	r3, [r7, #3180]
	add	r2, r3, r9
	ldr	r3, [r3, r9]
	cmn	r3, #1
	beq	.L1552
	ldr	r10, [r2, #12]
	movw	r3, #61589
	ldrh	r2, [r10]
	cmp	r2, r3
	bne	.L1552
	ldr	r3, [r10, #8]
	cmn	r3, #1
	bne	.L1553
	mov	r2, #696
	ldr	r1, .L1606+32
	ldr	r0, .L1606+36
	str	r3, [fp, #-60]
	bl	sftl_printk
	ldr	r3, [fp, #-60]
.L1553:
	mov	r2, #0
	mov	r0, r3
	sub	r1, fp, #44
	bl	log2phys
	ldr	r0, [r5, #3180]
	ldr	r3, [fp, #-44]
	add	r0, r0, r9
	ldr	r2, [r0, #4]
	cmp	r2, r3
	bne	.L1552
	ldr	r3, .L1606+48
	mov	r1, #20
	ldr	lr, [r5, #3164]
	ldr	r0, [r0, #16]
	ldrh	r2, [r3]
	str	r1, [fp, #-64]
	add	r2, r2, #1
	strh	r2, [r3], #10	@ movhi
	ldr	r2, [r5, #3836]
	str	r3, [fp, #-68]
	mla	r2, r1, lr, r2
	str	r0, [r2, #16]
	str	r2, [fp, #-60]
	bl	Ftl_get_new_temp_ppa
	ldr	r2, [fp, #-60]
	ldr	r1, [fp, #-64]
	str	r0, [r2, #4]
	ldr	r0, [r5, #3164]
	ldr	r2, [r5, #3836]
	mla	r1, r1, r0, r2
	ldr	r2, [r5, #3180]
	add	r2, r2, r9
	ldr	r0, [r2, #8]
	str	r0, [r1, #8]
	ldr	r2, [r2, #12]
	str	r2, [r1, #12]
	ldr	r2, [fp, #-44]
	str	r2, [r10, #12]
	ldr	r2, .L1606+44
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
	beq	.L1554
	ldr	r2, [fp, #-60]
	ldrh	r2, [r2, #4]
	cmp	r2, #0
	bne	.L1552
.L1554:
	str	r3, [fp, #-60]
	bl	Ftl_gc_temp_data_write_back
	cmp	r0, #0
	ldr	r3, [fp, #-60]
	ldrne	r2, .L1606
	movne	r1, #0
	strne	r1, [r2, #3820]
	bne	.L1605
.L1552:
	ldr	r3, [fp, #-48]
	add	r3, r3, #1
	b	.L1601
.L1556:
	ldrh	r3, [r8, #2]
	add	r6, r6, r3
	ldr	r3, [fp, #-56]
	uxth	r6, r6
	cmp	r3, r6
	strh	r6, [r8, #2]	@ movhi
	ldr	r6, .L1606+8
	bls	.L1558
	ldrh	r2, [r6, #-10]
	ldrh	r3, [r6, #-8]
	cmp	r2, r3
	bne	.L1559
.L1558:
	ldr	r3, [r7, #3164]
	cmp	r3, #0
	beq	.L1560
	bl	Ftl_gc_temp_data_write_back
	cmp	r0, #0
	movne	r3, #0
	ldrhne	r0, [r6, #-12]
	strne	r3, [r7, #3820]
	bne	.L1493
.L1560:
	ldrh	r1, [r6, #-10]
	cmp	r1, #0
	bne	.L1561
	ldrh	r3, [r8]
	ldr	r2, [r7, #2324]
	lsl	r3, r3, #1
	ldrh	r0, [r2, r3]
	cmp	r0, #0
	beq	.L1561
	strh	r1, [r2, r3]	@ movhi
	ldrh	r0, [r8]
	bl	update_vpc_list
	bl	FtlCacheWriteBack
	bl	l2p_flush
	bl	FtlVpcTblFlush
.L1561:
	mvn	r3, #0
	strh	r3, [r8]	@ movhi
.L1559:
	ldr	r3, .L1606+52
	ldrh	r3, [r3]
	cmp	r3, #2
	bhi	.L1562
	ldrh	r6, [r7, #102]
.L1534:
	ldr	r8, .L1606+16
	movw	r3, #65535
	ldrh	r2, [r8]
	cmp	r2, r3
	bne	.L1535
	add	r9, r8, #568
	mov	r3, #0
	str	r3, [r7, #3160]
.L1536:
	ldrh	r10, [r9]
	mov	r0, r10
	bl	List_get_gc_head_node
	ldr	r3, .L1606+16
	uxth	r1, r0
	strh	r1, [r3]	@ movhi
	movw	r3, #65535
	cmp	r1, r3
	bne	.L1537
	mov	r3, #0
	mov	r0, #8
	strh	r3, [r9]	@ movhi
	b	.L1493
.L1562:
	ldrh	r0, [r6, #-12]
	mov	r2, #0
	str	r2, [r7, #3820]
	cmp	r0, r2
	addeq	r0, r3, #1
	b	.L1493
.L1607:
	.align	2
.L1606:
	.word	.LANCHOR0
	.word	.LANCHOR2
	.word	.LANCHOR3-4016
	.word	.LC108
	.word	.LANCHOR0+2588
	.word	.LANCHOR0+3192
	.word	.LANCHOR0+3156
	.word	.LANCHOR0+2348
	.word	.LANCHOR1+551
	.word	.LC1
	.word	.LANCHOR0+2396
	.word	.LANCHOR0+2444
	.word	.LANCHOR3-4026
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
	sub	sp, sp, #52
	uxtb	r0, r0
	mov	r6, r1
	str	r2, [fp, #-68]
	cmp	r0, #16
	mov	r9, r3
	bne	.L1610
	mov	r2, r3
	ldr	r1, [fp, #-68]
	add	r0, r6, #256
	bl	FtlVendorPartRead
	str	r0, [fp, #-48]
.L1609:
	ldr	r0, [fp, #-48]
	sub	sp, fp, #40
	ldmfd	sp, {r4, r5, r6, r7, r8, r9, r10, fp, sp, pc}
.L1610:
	ldr	r3, [fp, #-68]
	ldr	r10, .L1643
	add	r3, r1, r3
	str	r3, [fp, #-56]
	ldr	r2, [fp, #-56]
	ldr	r3, [r10, #140]
	cmp	r2, r3
	mvnhi	r3, #0
	strhi	r3, [fp, #-48]
	bhi	.L1609
	ldr	r3, .L1643+4
	ldr	r7, [r3]
	cmn	r7, #1
	streq	r7, [fp, #-48]
	beq	.L1609
	bl	FtlCacheWriteBack
	ldrh	r4, [r10, #58]
	mov	r0, r6
	mov	r8, #0
	mov	r7, r10
	mov	r5, r8
	mov	r1, r4
	bl	__udivsi3
	ldr	r3, [fp, #-56]
	mov	r1, r4
	str	r0, [fp, #-60]
	sub	r0, r3, #1
	bl	__udivsi3
	ldr	r3, [fp, #-60]
	ldr	r4, [fp, #-60]
	str	r0, [fp, #-64]
	rsb	r3, r3, #1
	str	r8, [fp, #-76]
	add	r3, r3, r0
	str	r8, [fp, #-72]
	str	r3, [fp, #-52]
	ldr	r2, [fp, #-52]
	ldr	r3, [r10, #2508]
	str	r8, [fp, #-48]
	add	r3, r3, r2
	str	r3, [r10, #2508]
.L1612:
	ldr	r3, [fp, #-52]
	cmp	r3, #0
	bne	.L1628
	ldr	r3, .L1643
	movw	r2, #3206
	ldrh	r2, [r3, r2]
	cmp	r2, #0
	bne	.L1629
	movw	r2, #2344
	ldrh	r3, [r3, r2]
	cmp	r3, #31
	bhi	.L1609
.L1629:
	mov	r1, #1
	mov	r0, #0
	bl	rk_ftl_garbage_collect
	b	.L1609
.L1628:
	sub	r1, fp, #44
	mov	r2, #0
	mov	r0, r4
	bl	log2phys
	ldr	r1, [fp, #-44]
	cmn	r1, #1
	moveq	r10, #0
	beq	.L1614
	ldr	r2, [r7, #3832]
	mov	r0, #20
	ldr	r3, [fp, #-60]
	ldrh	r10, [r7, #58]
	mla	r2, r0, r5, r2
	cmp	r4, r3
	str	r1, [r2, #4]
	bne	.L1618
	ldr	r1, [r7, #3816]
	mov	r0, r6
	str	r2, [fp, #-80]
	str	r1, [r2, #8]
	mov	r1, r10
	bl	__umodsi3
	ldr	r3, [fp, #-68]
	sub	r1, r10, r0
	ldr	r2, [fp, #-80]
	str	r0, [fp, #-72]
	cmp	r3, r1
	movcs	r3, r1
	cmp	r3, r10
	str	r3, [fp, #-76]
	streq	r9, [r2, #8]
.L1619:
	ldrh	r1, [r7, #112]
	ldr	r0, [r7, #3812]
	str	r4, [r2, #16]
	mul	r1, r5, r1
	add	r5, r5, #1
	bic	r1, r1, #3
	add	r1, r0, r1
	str	r1, [r2, #12]
	b	.L1617
.L1616:
	mla	r0, r0, r4, r10
	ldr	r2, [fp, #-56]
	cmp	r2, r0
	movhi	r2, #1
	movls	r2, #0
	cmp	r6, r0
	movhi	r2, #0
	cmp	r2, #0
	beq	.L1615
	sub	r0, r0, r6
	mov	r1, #512
	add	r0, r9, r0, lsl #9
	bl	__memzero
.L1615:
	add	r10, r10, #1
.L1614:
	ldrh	r0, [r7, #58]
	cmp	r10, r0
	bcc	.L1616
.L1617:
	ldr	r3, [fp, #-52]
	add	r4, r4, #1
	subs	r3, r3, #1
	str	r3, [fp, #-52]
	beq	.L1621
	ldrh	r2, [r7, #32]
	cmp	r5, r2, lsl #2
	bne	.L1612
.L1621:
	cmp	r5, #0
	beq	.L1612
	mov	r2, #0
	mov	r1, r5
	ldr	r0, [r7, #3832]
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
.L1627:
	mov	r3, #20
	ldr	r2, [r7, #3832]
	mul	ip, r3, r10
	ldr	r3, [fp, #-60]
	add	r2, r2, ip
	ldr	r1, [r2, #16]
	cmp	r3, r1
	bne	.L1623
	ldr	r1, [r2, #8]
	ldr	r2, [r7, #3816]
	cmp	r1, r2
	bne	.L1624
	ldr	r3, [fp, #-80]
	mov	r0, r9
	ldr	r2, [fp, #-84]
	str	ip, [fp, #-92]
	add	r1, r1, r3
.L1642:
	bl	ftl_memcpy
	ldr	ip, [fp, #-92]
.L1624:
	ldr	r2, [r7, #3832]
	ldr	r0, [r2, ip]
	add	lr, r2, ip
	cmn	r0, #1
	ldreq	r1, [r7, #2708]
	streq	r0, [fp, #-48]
	addeq	r1, r1, #1
	streq	r1, [r7, #2708]
	ldr	r2, [r2, ip]
	cmp	r2, #256
	bne	.L1626
	ldr	r0, [lr, #4]
	ubfx	r0, r0, #10, #16
	bl	P2V_block_in_plane
	bl	FtlGcRefreshBlock
.L1626:
	add	r10, r10, #1
	cmp	r5, r10
	bne	.L1627
	mov	r5, #0
	b	.L1612
.L1618:
	ldr	r3, [fp, #-64]
	cmp	r4, r3
	bne	.L1620
	ldr	r1, [r7, #3808]
	ldr	r3, [fp, #-56]
	str	r1, [r2, #8]
	mul	r1, r10, r4
	sub	r8, r3, r1
	cmp	r10, r8
	subeq	r1, r1, r6
	addeq	r1, r9, r1, lsl #9
	streq	r1, [r2, #8]
	b	.L1619
.L1620:
	mul	r10, r4, r10
	sub	r10, r10, r6
	add	r10, r9, r10, lsl #9
	str	r10, [r2, #8]
	b	.L1619
.L1623:
	ldr	r3, [fp, #-64]
	cmp	r3, r1
	bne	.L1624
	ldr	r1, [r2, #8]
	ldr	r2, [r7, #3808]
	cmp	r1, r2
	bne	.L1624
	ldrh	r0, [r7, #58]
	str	ip, [fp, #-92]
	ldr	r2, [fp, #-88]
	mul	r0, r3, r0
	sub	r0, r0, r6
	add	r0, r9, r0, lsl #9
	b	.L1642
.L1644:
	.align	2
.L1643:
	.word	.LANCHOR0
	.word	.LANCHOR2
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
	@ args = 0, pretend = 0, frame = 64
	@ frame_needed = 1, uses_anonymous_args = 0
	mov	ip, sp
	push	{r4, r5, r6, r7, r8, r9, r10, fp, ip, lr, pc}
	sub	fp, ip, #4
	sub	sp, sp, #64
	uxtb	r0, r0
	mov	r8, r1
	str	r2, [fp, #-76]
	cmp	r0, #16
	str	r3, [fp, #-80]
	bne	.L1648
	mov	r2, r3
	ldr	r1, [fp, #-76]
	add	r0, r8, #256
	bl	FtlVendorPartWrite
.L1647:
	sub	sp, fp, #40
	ldmfd	sp, {r4, r5, r6, r7, r8, r9, r10, fp, sp, pc}
.L1648:
	ldr	r3, [fp, #-76]
	ldr	r9, .L1712
	add	r6, r1, r3
	ldr	r3, [r9, #140]
	cmp	r6, r3
	mvnhi	r0, #0
	bhi	.L1647
	ldr	r5, .L1712+4
	ldr	r0, [r5]
	cmn	r0, #1
	beq	.L1647
	ldr	r3, .L1712+8
	mov	r2, #2048
	ldrh	r4, [r9, #58]
	mov	r0, r8
	str	r2, [r3, #-4020]
	mov	r1, r4
	bl	__udivsi3
	mov	r1, r4
	str	r0, [fp, #-68]
	sub	r0, r6, #1
	bl	__udivsi3
	ldr	r2, [fp, #-68]
	ldr	r10, [r9, #3988]
	str	r0, [fp, #-88]
	sub	r6, r0, r2
	add	r3, r6, #1
	add	r2, r6, #1
	str	r3, [fp, #-72]
	cmp	r10, #0
	ldr	r3, [r9, #2516]
	add	r3, r3, r2
	str	r3, [r9, #2516]
	beq	.L1650
	ldr	r3, [r10, #16]
	ldr	r2, [fp, #-68]
	cmp	r2, r3
	beq	.L1651
	bl	FtlCacheWriteBack
.L1650:
	ldr	r3, [r5, #8]
	ldr	r4, .L1712+12
	cmp	r3, #0
	beq	.L1653
	ldrh	r3, [r4, #4]
	add	r0, r4, #48
	cmp	r3, #0
	moveq	r4, r0
.L1653:
	ldr	r5, [fp, #-68]
	ldr	r7, .L1712
.L1654:
	ldr	r3, [fp, #-72]
	cmp	r3, #0
	beq	.L1688
	ldrb	r2, [r4, #6]	@ zero_extendqisi2
	ldrh	r3, [r7, #32]
	cmp	r2, r3
	bcc	.L1655
	mov	r2, #1012
	ldr	r1, .L1712+16
	ldr	r0, .L1712+20
	bl	sftl_printk
.L1655:
	ldrh	r9, [r4, #4]
	cmp	r9, #0
	bne	.L1656
	bl	FtlCacheWriteBack
	ldr	r3, .L1712+12
	ldr	r6, .L1712+4
	cmp	r4, r3
	bne	.L1657
	add	r0, r4, #48
	ldrh	r9, [r0, #4]
	cmp	r9, #0
	bne	.L1658
	bl	allocate_new_data_superblock
	str	r9, [r6, #8]
.L1658:
	ldr	r0, .L1712+12
	bl	allocate_new_data_superblock
	ldr	r4, .L1712+12
	ldr	r3, [r6, #8]
	add	r0, r4, #48
	cmp	r3, #0
	movne	r4, r0
.L1659:
	ldrh	r3, [r4, #4]
	cmp	r3, #0
	bne	.L1656
	mov	r0, r4
	bl	allocate_new_data_superblock
.L1656:
	ldrh	r3, [r4, #4]
	ldr	r2, [fp, #-72]
	cmp	r3, r2
	movcs	r3, r2
	ldrb	r2, [r4, #7]	@ zero_extendqisi2
	lsl	r2, r2, #2
	cmp	r3, r2
	movcs	r3, r2
	ldrb	r2, [r4, #6]	@ zero_extendqisi2
	str	r3, [fp, #-104]
	ldrh	r3, [r7, #32]
	cmp	r2, r3
	bcc	.L1660
	movw	r2, #1045
	ldr	r1, .L1712+16
	ldr	r0, .L1712+20
	bl	sftl_printk
.L1660:
	mov	r9, #0
.L1661:
	ldr	r3, [fp, #-104]
	cmp	r9, r3
	bne	.L1682
.L1662:
	ldr	r3, [r7, #3988]
	cmp	r3, #0
	beq	.L1683
	ldr	r3, [fp, #-72]
	subs	r9, r9, #1
	sub	r3, r3, #1
	str	r3, [fp, #-72]
	bne	.L1683
.L1688:
	ldr	r3, [fp, #-88]
	mov	r0, #0
	ldr	r2, [fp, #-68]
	sub	r1, r3, r2
	bl	rk_ftl_garbage_collect
	ldr	r2, .L1712
	movw	r3, #2344
	ldrh	r3, [r2, r3]
	cmp	r3, #15
	bhi	.L1685
	ldr	r4, .L1712+24
	ldr	r5, .L1712+28
.L1708:
	ldrh	r3, [r4]
	movw	r2, #65535
	cmp	r3, r2
	bne	.L1689
	ldrh	r2, [r5]
	cmp	r2, r3
	bne	.L1689
	mov	r0, #0
	bl	List_get_gc_head_node
	uxth	r0, r0
	bl	FtlGcRefreshBlock
.L1689:
	ldr	r3, .L1712+32
	mov	r1, #1
	mov	r2, #128
	mov	r0, r1
	strh	r2, [r3]	@ movhi
	strh	r2, [r3, #-2]	@ movhi
	bl	rk_ftl_garbage_collect
	mov	r1, #1
	mov	r0, #0
	bl	rk_ftl_garbage_collect
	ldr	r3, .L1712+36
	ldrh	r3, [r3]
	cmp	r3, #8
	bls	.L1708
	b	.L1685
.L1651:
	ldr	r3, [r9, #2520]
	mov	r1, r4
	mov	r0, r8
	add	r3, r3, #1
	str	r3, [r9, #2520]
	bl	__umodsi3
	ldr	r3, [fp, #-76]
	sub	r4, r4, r0
	ldr	r1, [fp, #-80]
	cmp	r3, r4
	movcc	r4, r3
	ldr	r3, [r10, #8]
	lsl	r9, r4, #9
	mov	r2, r9
	add	r0, r3, r0, lsl #9
	bl	ftl_memcpy
	cmp	r6, #0
	bne	.L1652
.L1685:
	mov	r0, #0
	b	.L1647
.L1652:
	ldr	r3, [fp, #-76]
	add	r8, r8, r4
	sub	r3, r3, r4
	str	r3, [fp, #-76]
	ldr	r3, [fp, #-80]
	add	r3, r3, r9
	str	r3, [fp, #-80]
	ldr	r3, [fp, #-68]
	add	r3, r3, #1
	str	r3, [fp, #-68]
	bl	FtlCacheWriteBack
	str	r6, [fp, #-72]
	b	.L1650
.L1657:
	ldrh	r2, [r3, #4]
	str	r9, [r6, #8]
	cmp	r2, #0
	movne	r4, r3
	bne	.L1656
	mov	r0, r4
	bl	allocate_new_data_superblock
	b	.L1659
.L1682:
	ldrh	r3, [r4, #4]
	cmp	r3, #0
	beq	.L1662
	mov	r2, #0
	sub	r1, fp, #64
	mov	r0, r5
	mov	r6, #20
	bl	log2phys
	mov	r0, r4
	mul	r6, r6, r9
	bl	get_new_active_ppa
	ldrh	r2, [r7, #112]
	ldr	r3, [r7, #3840]
	mul	r1, r2, r9
	add	r3, r3, r6
	str	r0, [r3, #4]
	str	r5, [r3, #16]
	bic	r1, r1, #3
	str	r1, [fp, #-96]
	ldr	r0, [fp, #-96]
	ldr	r1, [r7, #3812]
	add	r10, r1, r0
	str	r1, [fp, #-100]
	str	r10, [r3, #12]
	mov	r1, #0
	mov	r0, r10
	bl	ftl_memset
	ldr	r3, [fp, #-68]
	ldr	r2, [fp, #-88]
	cmp	r5, r2
	cmpne	r5, r3
	ldrh	r3, [r7, #58]
	bne	.L1663
	ldr	r2, [fp, #-68]
	cmp	r5, r2
	bne	.L1664
	mov	r1, r3
	mov	r0, r8
	str	r3, [fp, #-84]
	bl	__umodsi3
	ldr	r3, [fp, #-84]
	ldr	r2, [fp, #-76]
	str	r0, [fp, #-92]
	sub	r3, r3, r0
	cmp	r3, r2
	movcs	r3, r2
	str	r3, [fp, #-84]
.L1665:
	ldrh	r3, [r7, #58]
	ldr	r2, [fp, #-84]
	cmp	r2, r3
	ldr	r3, [fp, #-68]
	bne	.L1666
	cmp	r5, r3
	ldr	r3, [r7, #3840]
	add	r6, r3, r6
	ldreq	r3, [fp, #-80]
	beq	.L1710
	ldr	r3, [fp, #-84]
	mul	r2, r3, r5
	ldr	r3, [fp, #-80]
	sub	r2, r2, r8
	add	r2, r3, r2, lsl #9
	str	r2, [r6, #8]
.L1668:
	ldrb	r2, [r4, #6]	@ zero_extendqisi2
	ldrh	r3, [r7, #32]
	cmp	r2, r3
	bcc	.L1679
	movw	r2, #1119
	ldr	r1, .L1712+16
	ldr	r0, .L1712+20
	bl	sftl_printk
.L1679:
	ldr	r3, .L1712+40
	add	r9, r9, #1
	ldr	r2, [fp, #-100]
	ldr	r1, [fp, #-96]
	strh	r3, [r2, r1]	@ movhi
	ldr	r3, [r7, #2544]
	str	r5, [r10, #8]
	add	r5, r5, #1
	str	r3, [r10, #4]
	add	r3, r3, #1
	cmn	r3, #1
	moveq	r3, #0
	str	r3, [r7, #2544]
	ldr	r3, [fp, #-64]
	str	r3, [r10, #12]
	ldrh	r3, [r4]
	strh	r3, [r10, #2]	@ movhi
	b	.L1661
.L1664:
	ldr	r2, [fp, #-76]
	smulbb	r3, r3, r5
	add	r2, r8, r2
	sub	r2, r2, r3
	uxth	r3, r2
	str	r3, [fp, #-84]
	mov	r3, #0
	str	r3, [fp, #-92]
	b	.L1665
.L1666:
	cmp	r5, r3
	ldr	r3, [r7, #3840]
	ldreq	r2, [r7, #3816]
	ldrne	r2, [r7, #3808]
	add	r3, r3, r6
	str	r2, [r3, #8]
	ldr	r3, [fp, #-64]
	cmn	r3, #1
	beq	.L1671
	str	r3, [fp, #-56]
	mov	r1, #1
	ldr	r3, [r7, #3840]
	sub	r0, fp, #60
	str	r5, [fp, #-44]
	add	r3, r3, r6
	ldr	r2, [r3, #8]
	ldr	r3, [r3, #12]
	str	r2, [fp, #-52]
	mov	r2, #0
	str	r3, [fp, #-48]
	bl	FlashReadPages
	ldr	r3, [fp, #-60]
	cmn	r3, #1
	ldreq	r3, [r7, #2708]
	addeq	r3, r3, #1
	streq	r3, [r7, #2708]
	beq	.L1675
	ldr	r3, [r10, #8]
	cmp	r5, r3
	beq	.L1674
	ldr	r3, [r7, #2708]
	mov	r2, r5
	ldr	r0, .L1712+44
	add	r3, r3, #1
	str	r3, [r7, #2708]
	ldr	r1, [r10, #8]
	bl	sftl_printk
.L1674:
	ldr	r3, [r10, #8]
	cmp	r5, r3
	beq	.L1675
	movw	r2, #1098
	ldr	r1, .L1712+16
	ldr	r0, .L1712+20
	bl	sftl_printk
.L1675:
	ldr	r3, [fp, #-68]
	cmp	r5, r3
	ldr	r3, [fp, #-84]
	lsl	r2, r3, #9
	bne	.L1676
	ldr	r3, [r7, #3840]
	ldr	r1, [fp, #-80]
	add	r3, r3, r6
	ldr	r0, [r3, #8]
	ldr	r3, [fp, #-92]
	add	r0, r0, r3, lsl #9
	bl	ftl_memcpy
	ldr	r3, [fp, #-88]
	cmp	r5, r3
	bne	.L1668
.L1677:
	ldrh	r3, [r4, #4]
	cmp	r3, #0
	ldrne	r3, [r7, #3840]
	addne	r6, r3, r6
	ldrne	r3, .L1712+8
	strne	r6, [r7, #3988]
	strne	r4, [r3, #-4036]
	b	.L1668
.L1671:
	ldr	r3, [r7, #3840]
	mov	r1, #0
	ldrh	r2, [r7, #110]
	add	r3, r3, r6
	ldr	r0, [r3, #8]
	bl	ftl_memset
	b	.L1675
.L1676:
	ldrh	r1, [r7, #58]
	ldr	r0, [fp, #-80]
	ldr	r3, [r7, #3840]
	mul	r1, r5, r1
	add	r3, r3, r6
	sub	r1, r1, r8
	add	r1, r0, r1, lsl #9
	ldr	r0, [r3, #8]
	bl	ftl_memcpy
	b	.L1677
.L1663:
	ldr	r2, [r7, #3840]
	mul	r3, r5, r3
	add	r6, r2, r6
	ldr	r2, [fp, #-80]
	sub	r3, r3, r8
	add	r3, r2, r3, lsl #9
.L1710:
	str	r3, [r6, #8]
	b	.L1668
.L1683:
	mov	r3, r4
	mov	r2, #0
	mov	r1, r9
	ldr	r0, [r7, #3840]
	bl	FtlProgPages
	ldr	r3, [fp, #-72]
	cmp	r3, r9
	bcs	.L1686
	movw	r2, #1134
	ldr	r1, .L1712+16
	ldr	r0, .L1712+20
	bl	sftl_printk
.L1686:
	ldr	r3, [fp, #-72]
	sub	r3, r3, r9
	str	r3, [fp, #-72]
	b	.L1654
.L1713:
	.align	2
.L1712:
	.word	.LANCHOR0
	.word	.LANCHOR2
	.word	.LANCHOR3
	.word	.LANCHOR0+2348
	.word	.LANCHOR1+574
	.word	.LC1
	.word	.LANCHOR0+2588
	.word	.LANCHOR0+3202
	.word	.LANCHOR0+3150
	.word	.LANCHOR0+2344
	.word	-3947
	.word	.LC109
	.size	FtlWrite, .-FtlWrite
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
	push	{fp, ip, lr, pc}
	sub	fp, ip, #4
	mov	r3, r2
	mov	r2, r1
	mov	r1, r0
	mov	r0, #0
	bl	FtlWrite
	ldmfd	sp, {fp, sp, pc}
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
	ldr	r4, .L1737
	mov	r1, #0
	ldr	r3, [r4, #3280]
	add	r6, r4, #2576
	ldrh	r2, [r4, #40]
	ldr	r0, [r4, #2324]
	str	r3, [r4, #3268]
	ldr	r3, [r4, #3284]
	lsl	r2, r2, #1
	str	r3, [r4, #3272]
	bl	ftl_memset
	ldrh	r0, [r6]
	movw	r3, #65535
	cmp	r0, r3
	bne	.L1717
.L1725:
	mvn	r0, #0
	ldmfd	sp, {r4, r5, r6, r7, r8, fp, sp, pc}
.L1717:
	ldr	r7, .L1737+4
	mov	r1, #1
	ldr	r8, .L1737+8
	bl	FtlGetLastWrittenPage
	sxth	r5, r0
	add	r0, r0, #1
	strh	r0, [r6, #2]	@ movhi
.L1719:
	cmp	r5, #0
	bge	.L1722
	movw	r2, #1437
	ldr	r1, .L1737+12
	ldr	r0, .L1737+16
	bl	sftl_printk
.L1721:
	ldrh	r3, [r4, #40]
	ldrh	r2, [r4, #110]
	add	r3, r3, #24
	cmp	r2, r3, lsl #1
	bcs	.L1724
	movw	r2, #1439
	ldr	r1, .L1737+12
	ldr	r0, .L1737+16
	bl	sftl_printk
.L1724:
	ldr	r5, .L1737+20
	mov	r2, #48
	ldr	r1, [r4, #3268]
	mov	r0, r5
	bl	ftl_memcpy
	ldrh	r2, [r4, #40]
	ldr	r1, [r4, #3268]
	ldr	r0, [r4, #2324]
	lsl	r2, r2, #1
	add	r1, r1, #48
	bl	ftl_memcpy
	ldrh	r1, [r4, #40]
	ldr	r3, [r4, #3268]
	ldr	r0, [r4, #24]
	lsr	r2, r1, #3
	add	r1, r1, #24
	lsl	r1, r1, #1
	add	r2, r2, #4
	bic	r1, r1, #3
	add	r1, r3, r1
	bl	ftl_memcpy
	ldr	r2, [r4, #2268]
	ldr	r3, .L1737+8
	cmp	r2, r3
	bne	.L1725
	ldrb	r2, [r4, #2278]	@ zero_extendqisi2
	ldrh	r3, [r4, #54]
	ldrh	r7, [r5, #8]
	cmp	r2, r3
	strh	r7, [r6, #6]	@ movhi
	bne	.L1725
	ldr	r3, .L1737+24
	ldrh	r2, [r4, #58]
	ldr	r6, [r4, #44]
	str	r7, [r3, #-4016]
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
	bls	.L1726
	movw	r2, #1461
	ldr	r1, .L1737+12
	ldr	r0, .L1737+16
	bl	sftl_printk
.L1726:
	ldrh	r0, [r5, #14]
	movw	r3, #2348
	ldr	r1, .L1737+28
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
	beq	.L1729
	ldr	r0, .L1737+28
	bl	make_superblock
.L1729:
	movw	r3, #2396
	ldrh	r2, [r4, r3]
	movw	r3, #65535
	cmp	r2, r3
	beq	.L1730
	ldr	r0, .L1737+32
	bl	make_superblock
.L1730:
	movw	r3, #2444
	ldrh	r2, [r4, r3]
	movw	r3, #65535
	cmp	r2, r3
	beq	.L1731
	ldr	r0, .L1737+36
	bl	make_superblock
.L1731:
	movw	r3, #2588
	ldrh	r2, [r4, r3]
	movw	r3, #65535
	cmp	r2, r3
	beq	.L1732
	ldr	r0, .L1737+40
	bl	make_superblock
.L1732:
	mov	r0, #0
	ldmfd	sp, {r4, r5, r6, r7, r8, fp, sp, pc}
.L1722:
	ldrh	r3, [r6]
	mov	r2, #1
	mov	r1, r2
	mov	r0, r7
	orr	r3, r5, r3, lsl #10
	str	r3, [r4, #3264]
	ldr	r3, [r4, #3280]
	str	r3, [r4, #3268]
	bl	FlashReadPages
	ldr	r3, [r4, #3260]
	cmn	r3, #1
	beq	.L1720
	ldr	r3, [r4, #3280]
	ldr	r3, [r3]
	cmp	r3, r8
	bne	.L1720
	ldr	r3, [r4, #3284]
	ldrh	r2, [r3]
	movw	r3, #61604
	cmp	r2, r3
	beq	.L1721
.L1720:
	sub	r5, r5, #1
	sxth	r5, r5
	b	.L1719
.L1738:
	.align	2
.L1737:
	.word	.LANCHOR0
	.word	.LANCHOR0+3260
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
	ldr	r4, .L1755
	mov	r2, #0
	movw	r3, #4008
	ldrh	r0, [r4, #36]
	strh	r2, [r4, r3]	@ movhi
	bl	FtlFreeSysBlkQueueInit
	bl	FtlScanSysBlk
	add	r3, r4, #2576
	ldrh	r2, [r3]
	movw	r3, #65535
	cmp	r2, r3
	bne	.L1740
.L1742:
	mvn	r8, #0
.L1739:
	mov	r0, r8
	ldmfd	sp, {r4, r5, r6, r7, r8, fp, sp, pc}
.L1740:
	bl	FtlLoadSysInfo
	subs	r8, r0, #0
	bne	.L1742
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
.L1743:
	cmp	r3, r1
	bge	.L1748
	mla	ip, r0, r3, r2
	ldr	ip, [ip, #4]
	cmp	ip, #0
	bge	.L1744
.L1748:
	ldr	r5, .L1755+4
	cmp	r3, r1
	ldrh	r2, [r5, #28]
	add	r7, r5, #80
	add	r2, r2, #1
	strh	r2, [r5, #28]	@ movhi
	bge	.L1754
.L1745:
	ldr	r6, .L1755+8
	ldr	r0, .L1755+12
	bl	FtlSuperblockPowerLostFix
	mov	r0, r6
	bl	FtlSuperblockPowerLostFix
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
	ldr	r0, [r4, #2324]
	ldrh	ip, [r6, #4]
	lsl	r2, r2, #1
	strh	r3, [r7, #2]	@ movhi
	mov	r3, #0
	strb	r3, [r4, #2354]
	strh	r3, [r7, #4]	@ movhi
	ldrh	r1, [r0, r2]
	sub	r1, r1, ip
	strh	r1, [r0, r2]	@ movhi
	ldrh	r2, [r4, #102]
	ldr	r0, .L1755+16
	strb	r3, [r4, #2402]
	strh	r2, [r6, #2]	@ movhi
	strh	r3, [r6, #4]	@ movhi
	bl	FtlMapBlkWriteDump_data
	add	r0, r6, #1552
	bl	FtlMapBlkWriteDump_data
	ldrh	r3, [r5, #30]
	add	r3, r3, #1
	strh	r3, [r5, #30]	@ movhi
	bl	l2p_flush
	bl	FtlVpcTblFlush
	bl	FtlVpcTblFlush
	b	.L1749
.L1744:
	add	r3, r3, #1
	b	.L1743
.L1754:
	movw	r3, #4008
	ldrh	r3, [r4, r3]
	cmp	r3, #0
	bne	.L1745
.L1749:
	movw	r3, #2348
	ldrh	r0, [r4, r3]
	movw	r3, #65535
	cmp	r0, r3
	beq	.L1750
	ldrh	r3, [r7, #4]
	cmp	r3, #0
	bne	.L1750
	ldr	r6, .L1755+8
	ldrh	r3, [r6, #4]
	cmp	r3, #0
	bne	.L1750
	bl	FtlGcRefreshBlock
	movw	r3, #2396
	ldrh	r0, [r4, r3]
	bl	FtlGcRefreshBlock
	sub	r0, r6, #48
	bl	allocate_new_data_superblock
	mov	r0, r6
	bl	allocate_new_data_superblock
.L1750:
	ldrh	r3, [r5, #28]
	tst	r3, #31
	bne	.L1739
	bl	FtlVpcCheckAndModify
	b	.L1739
.L1756:
	.align	2
.L1755:
	.word	.LANCHOR0
	.word	.LANCHOR0+2268
	.word	.LANCHOR0+2396
	.word	.LANCHOR0+2348
	.word	.LANCHOR0+3904
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
	ldr	r4, .L1781
	mov	r3, #0
	ldrh	r0, [r4, #36]
	str	r3, [r4, #2540]
	str	r3, [r4, #2544]
	bl	FtlFreeSysBlkQueueInit
	bl	FtlLoadBbt
	cmp	r0, #0
	beq	.L1758
	bl	FtlMakeBbt
.L1758:
	ldr	r0, .L1781+4
	mov	r2, #0
.L1759:
	ldrh	r1, [r4, #58]
	uxth	r3, r2
	add	r2, r2, #1
	cmp	r3, r1, lsl #7
	blt	.L1760
	ldrh	r6, [r4, #40]
	mov	r5, #0
.L1761:
	ldrh	r3, [r4, #42]
	cmp	r3, r6
	bhi	.L1762
	ldrh	r1, [r4, #32]
	sub	r3, r5, #3
	cmp	r3, r1, lsl #1
	bge	.L1763
.L1767:
	mov	r0, #0
	mov	r6, r0
.L1764:
	ldrh	r2, [r4, #40]
	uxth	r3, r0
	add	r5, r0, #1
	cmp	r2, r3
	bhi	.L1768
	ldrh	r3, [r4, #42]
	movw	r9, #2572
	ldrh	r5, [r4, #32]
	ldr	r10, [r4, #44]
	str	r3, [r4, #3804]
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
	ble	.L1769
	mov	r1, r5
	sub	r0, r10, r6
	bl	__udivsi3
	str	r0, [r4, #2504]
	lsr	r0, r0, #5
	add	r0, r0, #24
	strh	r0, [r4, r9]	@ movhi
.L1769:
	ldrh	r3, [r4, #94]
	cmp	r3, #0
	beq	.L1771
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
.L1771:
	movw	r3, #2572
	ldr	r6, .L1781+8
	ldrh	r2, [r4, r3]
	mvn	r7, #0
	ldr	r3, [r4, #2504]
	sub	r3, r3, r2
	mul	r5, r5, r3
	ldrh	r3, [r4, #102]
	str	r5, [r6, #-4016]
	mul	r5, r5, r3
	ldrh	r3, [r4, #58]
	str	r5, [r4, #2504]
	mul	r5, r5, r3
	str	r5, [r4, #140]
	bl	FtlBbmTblFlush
	ldrh	r2, [r4, #42]
	mov	r1, #0
	ldr	r0, [r4, #2324]
	ldr	r5, .L1781+12
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
.L1773:
	mov	r0, r8
	bl	make_superblock
	ldrb	r3, [r4, #2355]	@ zero_extendqisi2
	cmp	r3, #0
	ldrh	r3, [r5]
	bne	.L1774
	ldr	r2, [r4, #2324]
	lsl	r3, r3, #1
	strh	r7, [r2, r3]	@ movhi
	ldrh	r3, [r5]
	add	r3, r3, #1
	strh	r3, [r5]	@ movhi
	b	.L1773
.L1760:
	ldr	ip, [r4, #3816]
	mvn	r1, r3
	orr	r1, r3, r1, lsl #16
	str	r1, [ip, r3, lsl #2]
	ldr	r1, [r4, #3808]
	str	r0, [r1, r3, lsl #2]
	b	.L1759
.L1762:
	mov	r0, r6
	mov	r1, #1
	bl	FtlLowFormatEraseBlock
	add	r6, r6, #1
	add	r5, r5, r0
	uxth	r5, r5
	uxth	r6, r6
	b	.L1761
.L1763:
	mov	r0, r5
	bl	__udivsi3
	ldr	r3, [r4, #132]
	add	r0, r0, r3
	uxth	r0, r0
	bl	FtlSysBlkNumInit
	ldrh	r0, [r4, #36]
	bl	FtlFreeSysBlkQueueInit
	ldrh	r5, [r4, #40]
.L1765:
	ldrh	r3, [r4, #42]
	cmp	r3, r5
	bls	.L1767
	mov	r0, r5
	mov	r1, #1
	add	r5, r5, #1
	bl	FtlLowFormatEraseBlock
	uxth	r5, r5
	b	.L1765
.L1768:
	mov	r1, #0
	uxth	r0, r0
	bl	FtlLowFormatEraseBlock
	add	r6, r6, r0
	mov	r0, r5
	uxth	r6, r6
	b	.L1764
.L1774:
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
	ldr	r2, .L1781+16
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
.L1775:
	mov	r0, r8
	bl	make_superblock
	ldrb	r3, [r4, #2403]	@ zero_extendqisi2
	cmp	r3, #0
	ldrh	r3, [r5]
	bne	.L1776
	ldr	r2, [r4, #2324]
	lsl	r3, r3, #1
	strh	r7, [r2, r3]	@ movhi
	ldrh	r3, [r5]
	add	r3, r3, #1
	strh	r3, [r5]	@ movhi
	b	.L1775
.L1776:
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
	ldr	r3, .L1781+20
	mov	r2, #0
	strh	r2, [r3, #2]	@ movhi
	ldr	r2, [r6, #-4016]
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
	ldreq	r3, .L1781+24
	moveq	r2, #1
	streq	r2, [r3]
	ldmfd	sp, {r4, r5, r6, r7, r8, r9, r10, fp, sp, pc}
.L1782:
	.align	2
.L1781:
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
	ldr	r4, .L1789
	mvn	r3, #0
	ldr	r5, .L1789+4
	ldr	r1, .L1789+8
	ldr	r0, .L1789+12
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
	bne	.L1787
	bl	FtlSysBlkInit
	cmp	r0, #0
	bne	.L1787
	mov	r3, #1
	str	r3, [r5]
	movw	r3, #2344
	ldrh	r3, [r4, r3]
	cmp	r3, #15
	bhi	.L1787
	movw	r4, #8129
.L1786:
	mov	r1, #1
	mov	r0, #0
	bl	rk_ftl_garbage_collect
	subs	r4, r4, #1
	bne	.L1786
.L1787:
	mov	r0, #0
	ldmfd	sp, {r4, r5, fp, sp, pc}
.L1790:
	.align	2
.L1789:
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
	ldr	r2, .L1795
	ldr	r3, [r2, #-4012]
	cmp	r3, #0
	streq	r0, [r2, #-4012]
	moveq	r0, r3
	streq	r1, [r2, #-4008]
	mvnne	r0, #0
	ldmfd	sp, {fp, sp, pc}
.L1796:
	.align	2
.L1795:
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
	ldr	r4, .L1806
	bl	ftl_malloc
	cmp	r0, #0
	str	r0, [r4, #-4004]
	mvneq	r9, #11
	beq	.L1797
	ldr	r8, .L1806+4
	mov	r6, #0
	mov	r5, r6
	mov	r7, r6
.L1801:
	ldr	r3, [r4, #-4012]
	mov	r1, #128
	ldr	r2, [r4, #-4004]
	lsl	r0, r7, #7
	blx	r3
	subs	r9, r0, #0
	bne	.L1799
	ldr	r2, [r4, #-4004]
	ldr	r3, [r2]
	cmp	r3, r8
	bne	.L1800
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
.L1800:
	add	r7, r7, #1
	cmp	r7, #2
	bne	.L1801
	cmp	r5, #0
	beq	.L1802
	ldr	r3, [r4, #-4012]
	mov	r1, #128
	lsl	r0, r6, #7
	blx	r3
	subs	r9, r0, #0
	beq	.L1797
.L1799:
	ldr	r0, [r4, #-4004]
	mvn	r9, #0
	bl	kfree
	mov	r3, #0
	str	r3, [r4, #-4004]
	b	.L1797
.L1802:
	mov	r1, #65536
	mov	r0, r2
	bl	__memzero
	ldr	r3, [r4, #-4004]
	mov	r2, #1
	add	r1, r3, #61440
	str	r2, [r3, #4]
	str	r8, [r3]
	str	r2, [r1, #4092]
	ldr	r2, .L1806+8
	strh	r5, [r3, #12]	@ movhi
	strh	r2, [r3, #14]	@ movhi
.L1797:
	mov	r0, r9
	ldmfd	sp, {r4, r5, r6, r7, r8, r9, fp, sp, pc}
.L1807:
	.align	2
.L1806:
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
	ldr	r3, .L1814
	mov	r4, r0
	mov	r0, r1
	ldr	ip, [r3, #-4004]
	cmp	ip, #0
	ldrhne	lr, [ip, #10]
	movne	r3, #0
	bne	.L1810
.L1813:
	mvn	r0, #0
	ldmfd	sp, {r4, r5, fp, sp, pc}
.L1811:
	add	r3, r3, #1
.L1810:
	cmp	r3, lr
	bcs	.L1813
	add	r1, ip, r3, lsl #3
	ldrh	r5, [r1, #16]
	cmp	r5, r4
	bne	.L1811
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
.L1815:
	.align	2
.L1814:
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
	ldr	r8, .L1835
	mov	r9, r2
	ldr	r4, [r8, #-4004]
	cmp	r4, #0
	beq	.L1831
	ldrh	r2, [r4, #10]
	add	r7, r9, #63
	ldrh	r3, [r4, #8]
	bic	r7, r7, #63
	mov	r5, #0
	str	r3, [fp, #-44]
.L1818:
	cmp	r5, r2
	bcc	.L1826
	ldrh	r1, [r4, #14]
	cmp	r7, r1
	bhi	.L1831
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
	ldr	r3, [r8, #-4008]
	b	.L1834
.L1826:
	add	r6, r4, r5, lsl #3
	ldrh	r3, [r6, #16]
	cmp	r3, r0
	str	r3, [fp, #-48]
	bne	.L1819
	ldrh	r1, [r6, #20]
	add	r3, r4, #1024
	add	r1, r1, #63
	bic	r1, r1, #63
	cmp	r9, r1
	str	r1, [fp, #-52]
	bls	.L1820
	ldrh	r1, [r4, #14]
	cmp	r7, r1
	bhi	.L1831
	ldrh	r8, [r6, #18]
	sub	r2, r2, #1
	str	r2, [fp, #-56]
.L1821:
	ldr	r2, [fp, #-56]
	add	r6, r6, #8
	cmp	r5, r2
	bcc	.L1822
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
.L1823:
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
	ldr	r3, .L1835
	ldr	r3, [r3, #-4008]
.L1834:
	ldr	r0, [fp, #-44]
	mov	r2, r4
	mov	r1, #128
	lsl	r0, r0, #7
	blx	r3
	mov	r0, #0
.L1816:
	sub	sp, fp, #40
	ldmfd	sp, {r4, r5, r6, r7, r8, r9, r10, fp, sp, pc}
.L1822:
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
	b	.L1821
.L1820:
	add	r5, r4, r5, lsl #3
	mov	r2, r9
	mov	r1, ip
	ldrh	r0, [r5, #18]
	add	r0, r3, r0
	bl	memcpy
	strh	r9, [r6, #20]	@ movhi
	b	.L1823
.L1819:
	add	r5, r5, #1
	b	.L1818
.L1831:
	mvn	r0, #0
	b	.L1816
.L1836:
	.align	2
.L1835:
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
	beq	.L1837
	ldr	r3, .L1862
	cmp	r6, r3
	beq	.L1840
	add	r3, r3, #1
	cmp	r6, r3
	beq	.L1841
.L1857:
	mvn	r5, #13
	b	.L1839
.L1840:
	ldr	r6, .L1862+4
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
	bne	.L1847
	mov	r2, #8
	mov	r1, r5
	bl	arm_copy_from_user
	cmp	r0, #0
	bne	.L1857
	ldr	r2, [r4]
	ldr	r3, .L1862+8
	cmp	r2, r3
	beq	.L1845
.L1846:
	mvn	r5, #0
.L1839:
	mov	r0, r4
	bl	kfree
.L1837:
	mov	r0, r5
	ldmfd	sp, {r4, r5, r6, fp, sp, pc}
.L1845:
	ldrh	r2, [r4, #6]
	add	r1, r4, #8
	ldrh	r0, [r4, #4]
	bl	rk_sftl_vendor_read
	cmn	r0, #1
	beq	.L1846
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
	bne	.L1857
	mov	r0, r5
	mov	r1, r4
	bl	arm_copy_to_user
	subs	r5, r0, #0
	beq	.L1839
	b	.L1857
.L1841:
	ldr	r6, .L1862+4
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
	bne	.L1847
	mov	r2, #8
	mov	r1, r5
	bl	arm_copy_from_user
	cmp	r0, #0
	bne	.L1857
	ldr	r2, [r4]
	ldr	r3, .L1862+8
	cmp	r2, r3
	bne	.L1846
	ldrh	r2, [r4, #6]
	movw	r3, #4087
	cmp	r2, r3
	bhi	.L1846
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
	bne	.L1861
	mov	r1, r5
	mov	r0, r4
	bl	arm_copy_from_user
	cmp	r0, #0
	bne	.L1857
	ldrh	r2, [r4, #6]
	add	r1, r4, #8
	ldrh	r0, [r4, #4]
	bl	rk_sftl_vendor_write
	mov	r5, r0
	b	.L1839
.L1847:
	mov	r1, #8
.L1861:
	bl	__memzero
	b	.L1857
.L1863:
	.align	2
.L1862:
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
	ldr	r0, .L1865
	bl	misc_register
	ldmfd	sp, {fp, sp, pc}
.L1866:
	.align	2
.L1865:
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
	.type	__func__.3929, %object
	.size	__func__.3929, 17
__func__.3929:
	.ascii	"INSERT_DATA_LIST\000"
	.type	__func__.3924, %object
	.size	__func__.3924, 17
__func__.3924:
	.ascii	"INSERT_FREE_LIST\000"
	.type	__func__.3960, %object
	.size	__func__.3960, 17
__func__.3960:
	.ascii	"List_remove_node\000"
	.type	__func__.3992, %object
	.size	__func__.3992, 22
__func__.3992:
	.ascii	"List_update_data_list\000"
	.type	__func__.4098, %object
	.size	__func__.4098, 22
__func__.4098:
	.ascii	"select_l2p_ram_region\000"
	.type	__func__.4421, %object
	.size	__func__.4421, 16
__func__.4421:
	.ascii	"make_superblock\000"
	.type	__func__.4596, %object
	.size	__func__.4596, 19
__func__.4596:
	.ascii	"get_new_active_ppa\000"
	.type	__func__.14933, %object
	.size	__func__.14933, 15
__func__.14933:
	.ascii	"FlashReadPages\000"
	.type	__func__.14950, %object
	.size	__func__.14950, 15
__func__.14950:
	.ascii	"FlashProgPages\000"
	.type	__func__.4034, %object
	.size	__func__.4034, 26
__func__.4034:
	.ascii	"ftl_map_blk_alloc_new_blk\000"
	.type	__func__.3538, %object
	.size	__func__.3538, 11
__func__.3538:
	.ascii	"FtlMemInit\000"
	.type	__func__.3764, %object
	.size	__func__.3764, 14
__func__.3764:
	.ascii	"FtlBbt2Bitmap\000"
	.type	__func__.4001, %object
	.size	__func__.4001, 16
__func__.4001:
	.ascii	"load_l2p_region\000"
	.type	__func__.4059, %object
	.size	__func__.4059, 31
__func__.4059:
	.ascii	"Ftl_write_map_blk_to_last_page\000"
	.type	__func__.4073, %object
	.size	__func__.4073, 16
__func__.4073:
	.ascii	"FtlMapWritePage\000"
	.type	__func__.4045, %object
	.size	__func__.4045, 15
__func__.4045:
	.ascii	"ftl_map_blk_gc\000"
	.type	__func__.4115, %object
	.size	__func__.4115, 9
__func__.4115:
	.ascii	"log2phys\000"
	.type	__func__.4330, %object
	.size	__func__.4330, 16
__func__.4330:
	.ascii	"FtlReUsePrevPpa\000"
	.type	__func__.4483, %object
	.size	__func__.4483, 14
__func__.4483:
	.ascii	"ftl_check_vpc\000"
	.type	__func__.4210, %object
	.size	__func__.4210, 14
__func__.4210:
	.ascii	"FtlScanSysBlk\000"
	.type	__func__.4442, %object
	.size	__func__.4442, 18
__func__.4442:
	.ascii	"SupperBlkListInit\000"
	.type	__func__.4568, %object
	.size	__func__.4568, 25
__func__.4568:
	.ascii	"allocate_data_superblock\000"
	.type	__func__.4609, %object
	.size	__func__.4609, 16
__func__.4609:
	.ascii	"update_vpc_list\000"
	.type	__func__.4616, %object
	.size	__func__.4616, 20
__func__.4616:
	.ascii	"decrement_vpc_count\000"
	.type	__func__.4364, %object
	.size	__func__.4364, 22
__func__.4364:
	.ascii	"FtlRecoverySuperblock\000"
	.type	__func__.3807, %object
	.size	__func__.3807, 11
__func__.3807:
	.ascii	"FtlLoadBbt\000"
	.type	__func__.4188, %object
	.size	__func__.4188, 15
__func__.4188:
	.ascii	"FtlVpcTblFlush\000"
	.type	__func__.4467, %object
	.size	__func__.4467, 21
__func__.4467:
	.ascii	"FtlVpcCheckAndModify\000"
	.type	__func__.4589, %object
	.size	__func__.4589, 29
__func__.4589:
	.ascii	"allocate_new_data_superblock\000"
	.type	__func__.3662, %object
	.size	__func__.3662, 13
__func__.3662:
	.ascii	"FtlProgPages\000"
	.type	__func__.4686, %object
	.size	__func__.4686, 19
__func__.4686:
	.ascii	"FtlGcFreeTempBlock\000"
	.type	__func__.4792, %object
	.size	__func__.4792, 23
__func__.4792:
	.ascii	"rk_ftl_garbage_collect\000"
	.type	__func__.3690, %object
	.size	__func__.3690, 9
__func__.3690:
	.ascii	"FtlWrite\000"
	.type	__func__.4267, %object
	.size	__func__.4267, 15
__func__.4267:
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
	.word	.LC110
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
	.size	gL2pMapInfo, 40
gL2pMapInfo:
	.space	40
	.type	g_totle_map_block, %object
	.size	g_totle_map_block, 2
g_totle_map_block:
	.space	2
	.space	2
	.type	gVendorBlkInfo, %object
	.size	gVendorBlkInfo, 40
gVendorBlkInfo:
	.space	40
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
	.type	gp_last_act_superblock, %object
	.size	gp_last_act_superblock, 4
gp_last_act_superblock:
	.space	4
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
	.ascii	"SFTL version: 5.0.44 20180713\000"
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
	.ascii	"region_id = %x phyAddr = %x\012\000"
.LC88:
	.ascii	"spare:\000"
.LC89:
	.ascii	"map_ppn:\000"
.LC90:
	.ascii	"FtlMapWritePage error = %x \012\000"
.LC91:
	.ascii	"FtlMapWritePage error = %x error count = %d\012\000"
.LC92:
	.ascii	"ftl_scan_all_data = %x\012\000"
.LC93:
	.ascii	"scan lpa = %x ppa= %x\012\000"
.LC94:
	.ascii	"lba = %x,addr= %x,spare= %x %x %x %x data=%x %x\012"
	.ascii	"\000"
.LC95:
	.ascii	"...%s enter...\012\000"
.LC96:
	.ascii	"FtlCheckVpc2 %x = %x  %x\012\000"
.LC97:
	.ascii	"free blk vpc error %x = %x  %x\012\000"
.LC98:
	.ascii	"FtlBbmTblFlush id=%x,page=%x,previd=%x cnt=%d\012\000"
.LC99:
	.ascii	"FtlBbmTblFlush error:%x\012\000"
.LC100:
	.ascii	"FtlBbmTblFlush error = %x error count = %d\012\000"
.LC101:
	.ascii	"FtlGcFreeBadSuperBlk 0x%x\012\000"
.LC102:
	.ascii	"decrement_vpc_count %x = %d\012\000"
.LC103:
	.ascii	"spuer block %x vpn is 0\012 \000"
.LC104:
	.ascii	"no ect\000"
.LC105:
	.ascii	"FtlVpcTblFlush error = %x error count = %d\012\000"
.LC106:
	.ascii	"FtlCheckVpc %x = %x  %x\012\000"
.LC107:
	.ascii	"Ftlwrite decrement_vpc_count %x = %d\012\000"
.LC108:
	.ascii	"SWL %x, FSB = %x vpc= %x,ec=%x th=%x\012\000"
.LC109:
	.ascii	"FtlWrite: lpa error:%x %x\012\000"
.LC110:
	.ascii	"vendor_storage\000"
