/* SPDX-License-Identifier: GPL-2.0 */

/* Copyright (c) 2018 Rockchip Electronics Co. Ltd.
 * date: 2018-11-29
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
	ldrh	r2, [r7, #6]
	ldrh	ip, [r7, #14]
	ldr	r4, .L25
	ldrh	r1, [r0]
	cmp	ip, #4
	ldrh	r6, [r0, #2]
	strh	r2, [r4, #42]	@ movhi
	lsreq	r2, r2, #1
	strh	ip, [r4, #58]	@ movhi
	ldr	ip, .L25+4
	strheq	r2, [r4, #42]	@ movhi
	moveq	r2, #8
	ldrh	r0, [r0, #4]
	strheq	r2, [r4, #58]	@ movhi
	mov	r2, #0
	strh	r1, [r4, #52]	@ movhi
	strh	r6, [r4, #54]	@ movhi
	strh	r0, [r4, #56]	@ movhi
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
	ldr	r1, .L114
	ldrh	r3, [r1, #40]
	mov	lr, r1
	cmp	r3, r0
	bls	.L100
	ldr	r6, [r1, #2316]
	lsl	r5, r0, #3
	mvn	ip, #0
	add	r2, r6, r5
	strh	ip, [r2, #2]	@ movhi
	strh	ip, [r6, r5]	@ movhi
	ldr	r3, [r1, #2320]
	cmp	r3, #0
	bne	.L101
.L113:
	str	r2, [r1, #2320]
	b	.L100
.L101:
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
.L109:
	ldr	r4, [fp, #-48]
	add	lr, lr, #1
	uxth	lr, lr
	cmp	lr, r4
	movls	r4, #0
	movhi	r4, #1
	cmp	r0, r1
	orreq	r4, r4, #1
	cmp	r4, #0
	bne	.L100
	lsl	r9, r1, #1
	ldrh	r4, [r7, r9]
	mov	ip, r4
	ldrh	r4, [r3, #4]
	cmp	r4, #0
	mulne	r4, r4, ip
	ldr	ip, [fp, #-44]
	mvneq	r4, #0
	cmp	ip, r4
	bne	.L105
	ldr	ip, [fp, #-52]
	ldrh	r9, [r8, r9]
	ldrh	r4, [ip]
	cmp	r9, r4
	bcc	.L107
.L106:
	strh	r1, [r6, r5]	@ movhi
	ldrh	r1, [r3, #2]
	strh	r1, [r2, #2]	@ movhi
	ldr	r1, .L114
	ldr	ip, [r1, #2320]
	cmp	r3, ip
	bne	.L110
	strh	r0, [r3, #2]	@ movhi
	b	.L113
.L105:
	bcc	.L106
.L107:
	ldrh	r4, [r3]
	movw	ip, #65535
	cmp	r4, ip
	bne	.L108
	strh	r1, [r2, #2]	@ movhi
	strh	r0, [r3]	@ movhi
	ldr	r3, .L114
	str	r2, [r3, #2332]
.L100:
	mov	r0, #0
	sub	sp, fp, #40
	ldmfd	sp, {r4, r5, r6, r7, r8, r9, r10, fp, sp, pc}
.L108:
	add	r3, r10, r4, lsl #3
	mov	r1, r4
	b	.L109
.L110:
	ldrh	r2, [r3, #2]
	ldr	r1, [r1, #2316]
	lsl	r2, r2, #3
	strh	r0, [r1, r2]	@ movhi
	strh	r0, [r3, #2]	@ movhi
	b	.L100
.L115:
	.align	2
.L114:
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
	ldr	r2, .L118
	add	r1, r2, #2336
	ldrh	r2, [r2, #40]
	ldrh	r3, [r1]
	add	r3, r3, #1
	uxth	r3, r3
	cmp	r2, r3
	strh	r3, [r1]	@ movhi
	ldmfdcs	sp, {fp, sp, pc}
	mov	r2, #214
	ldr	r1, .L118+4
	ldr	r0, .L118+8
	bl	sftl_printk
	ldmfd	sp, {fp, sp, pc}
.L119:
	.align	2
.L118:
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
	beq	.L121
	ldr	r2, .L127
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
	beq	.L121
	ldr	r8, [r2, #2328]
	lsl	r2, r0, #1
	ldr	r7, [r1, #2316]
	mov	r4, lr
	ldrh	r9, [r8, r2]
	sub	r2, r3, r7
	ubfx	r2, r2, #3, #16
.L125:
	lsl	lr, r2, #1
	ldrh	lr, [r8, lr]
	cmp	lr, r9
	bcs	.L123
	ldrh	lr, [r3]
	cmp	lr, r4
	bne	.L124
	strh	r2, [ip, #2]	@ movhi
	strh	r0, [r3]	@ movhi
.L121:
	mov	r0, #0
	ldmfd	sp, {r4, r5, r6, r7, r8, r9, fp, sp, pc}
.L124:
	add	r3, r7, lr, lsl #3
	mov	r2, lr
	b	.L125
.L123:
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
	b	.L121
.L128:
	.align	2
.L127:
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
	ldr	r2, .L131
	movw	r1, #2344
	ldrh	r3, [r2, r1]
	add	r3, r3, #1
	uxth	r3, r3
	strh	r3, [r2, r1]	@ movhi
	ldrh	r2, [r2, #40]
	cmp	r2, r3
	ldmfdcs	sp, {fp, sp, pc}
	mov	r2, #207
	ldr	r1, .L131+4
	ldr	r0, .L131+8
	bl	sftl_printk
	ldmfd	sp, {fp, sp, pc}
.L132:
	.align	2
.L131:
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
	ldr	r4, .L140
	uxth	r1, r1
	movw	r3, #65535
	mov	r8, r0
	ldr	r7, [r4, #2316]
	lsl	r6, r1, #3
	add	r5, r7, r6
	ldrh	r2, [r5, #2]
	cmp	r2, r3
	bne	.L134
	ldr	r3, [r0]
	cmp	r5, r3
	beq	.L134
	mov	r2, #372
	ldr	r1, .L140+4
	ldr	r0, .L140+8
	bl	sftl_printk
.L134:
	ldr	r3, [r8]
	ldrh	r2, [r7, r6]
	cmp	r5, r3
	movw	r3, #65535
	bne	.L135
	cmp	r2, r3
	ldrne	r3, [r4, #2316]
	moveq	r3, #0
	streq	r3, [r8]
	addne	r2, r3, r2, lsl #3
	mvnne	r3, #0
	strne	r2, [r8]
	strhne	r3, [r2, #2]	@ movhi
.L137:
	mvn	r3, #0
	mov	r0, #0
	strh	r3, [r7, r6]	@ movhi
	strh	r3, [r5, #2]	@ movhi
	ldmfd	sp, {r4, r5, r6, r7, r8, fp, sp, pc}
.L135:
	cmp	r2, r3
	ldrh	r3, [r5, #2]
	bne	.L138
	cmp	r3, r2
	ldrne	r2, [r4, #2316]
	lslne	r3, r3, #3
	mvnne	r1, #0
	beq	.L137
.L139:
	strh	r1, [r2, r3]	@ movhi
	b	.L137
.L138:
	ldr	r1, [r4, #2316]
	add	r2, r1, r2, lsl #3
	strh	r3, [r2, #2]	@ movhi
	ldrh	r3, [r5, #2]
	ldrh	r1, [r7, r6]
	ldr	r2, [r4, #2316]
	lsl	r3, r3, #3
	b	.L139
.L141:
	.align	2
.L140:
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
	beq	.L142
	ldr	r2, .L149
	movw	ip, #65535
	ldr	r4, [r2, #2316]
.L144:
	cmp	r1, #0
	bne	.L145
.L147:
	sub	r3, r3, r4
	ubfx	r4, r3, #3, #16
	mov	r1, r4
	bl	List_remove_node
.L142:
	mov	r0, r4
	ldmfd	sp, {r4, fp, sp, pc}
.L145:
	ldrh	r2, [r3]
	cmp	r2, ip
	beq	.L147
	sub	r1, r1, #1
	add	r3, r4, r2, lsl #3
	uxth	r1, r1
	b	.L144
.L150:
	.align	2
.L149:
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
	ldr	r1, .L158
	uxth	r2, r0
	ldr	r3, [r1, #2320]
	cmp	r3, #0
	ldrne	r1, [r1, #2316]
	movwne	r0, #65535
	bne	.L154
	movw	r0, #65535
	ldmfd	sp, {fp, sp, pc}
.L156:
	sub	r2, r2, #1
	add	r3, r1, r3, lsl #3
	uxth	r2, r2
.L154:
	cmp	r2, #0
	beq	.L155
	ldrh	r3, [r3]
	cmp	r3, r0
	bne	.L156
	ldmfd	sp, {fp, sp, pc}
.L155:
	sub	r3, r3, r1
	ubfx	r0, r3, #3, #16
	ldmfd	sp, {fp, sp, pc}
.L159:
	.align	2
.L158:
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
	ldr	r4, .L173
	movw	r3, #2348
	uxth	r5, r0
	ldrh	r3, [r4, r3]
	cmp	r3, r5
	beq	.L162
	movw	r3, #2396
	ldrh	r3, [r4, r3]
	cmp	r3, r5
	beq	.L162
	movw	r3, #2444
	ldrh	r3, [r4, r3]
	cmp	r3, r5
	beq	.L162
	ldr	r8, [r4, #2316]
	ldr	r3, [r4, #2320]
	add	r7, r8, r5, lsl #3
	cmp	r7, r3
	beq	.L162
	ldr	r2, [r4, #2324]
	lsl	r3, r5, #1
	ldrh	r6, [r7, #4]
	ldrh	r3, [r2, r3]
	cmp	r6, #0
	mvneq	r6, #0
	mulne	r6, r6, r3
	ldr	r3, [r8, r5, lsl #3]
	cmn	r3, #1
	bne	.L165
	movw	r2, #463
	ldr	r1, .L173+4
	ldr	r0, .L173+8
	bl	sftl_printk
.L165:
	ldr	r3, [r8, r5, lsl #3]
	cmn	r3, #1
	beq	.L162
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
	bcs	.L162
	ldr	r4, .L173+12
	mov	r1, r5
	ldr	r0, .L173+16
	bl	List_remove_node
	ldrh	r3, [r4]
	cmp	r3, #0
	bne	.L167
	movw	r2, #474
	ldr	r1, .L173+4
	ldr	r0, .L173+8
	bl	sftl_printk
.L167:
	ldrh	r3, [r4]
	mov	r0, r5
	sub	r3, r3, #1
	strh	r3, [r4]	@ movhi
	bl	INSERT_DATA_LIST
.L162:
	mov	r0, #0
	ldmfd	sp, {r4, r5, r6, r7, r8, fp, sp, pc}
.L174:
	.align	2
.L173:
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
	ldr	r3, .L185
	mov	r1, #0
	mov	r0, #12
	movw	ip, #65535
	ldrh	r2, [r3, #138]
	ldr	r3, [r3, #2492]
.L176:
	uxth	r4, r1
	cmp	r4, r2
	bcc	.L178
	mov	r4, r2
	mov	r1, #0
	mov	r0, #-2147483648
	mov	r5, #12
.L179:
	uxth	lr, r1
	cmp	lr, r2
	bcc	.L181
	cmp	r4, r2
	bcc	.L177
	ldr	r1, .L185+4
	mov	r4, r2
	mvn	r0, #0
	ldrh	r5, [r1]
	mov	r1, #0
.L182:
	uxth	ip, r1
	cmp	ip, r2
	bcc	.L184
	cmp	r4, r2
	bcc	.L177
	movw	r2, #787
	ldr	r1, .L185+8
	ldr	r0, .L185+12
	bl	sftl_printk
	b	.L177
.L178:
	add	r1, r1, #1
	mla	lr, r0, r1, r3
	ldrh	lr, [lr, #-12]
	cmp	lr, ip
	bne	.L176
.L177:
	mov	r0, r4
	ldmfd	sp, {r4, r5, r6, fp, sp, pc}
.L181:
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
	b	.L179
.L184:
	ldr	lr, [r3, #4]
	cmp	r0, lr
	bls	.L183
	ldrh	r6, [r3]
	cmp	r6, r5
	movne	r0, lr
	movne	r4, ip
.L183:
	add	r1, r1, #1
	add	r3, r3, #12
	b	.L182
.L186:
	.align	2
.L185:
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
	ldr	ip, .L193
	movw	r1, #2498
	ldrh	r2, [ip, r1]
	mov	r3, ip
	cmp	r2, #4
	cmpls	r0, #0
	bne	.L188
	add	r2, r2, #1
	strh	r2, [ip, r1]	@ movhi
	ldmfd	sp, {fp, sp, pc}
.L188:
	mov	r2, #0
	movw	lr, #65535
	strh	r2, [ip, r1]	@ movhi
	str	r2, [ip, #2500]
	ldrh	r1, [ip, #40]
	ldr	r2, [ip, #2324]
	add	r1, r2, r1, lsl #1
.L189:
	cmp	r2, r1
	bne	.L191
	ldmfd	sp, {fp, sp, pc}
.L191:
	ldrh	ip, [r2], #2
	cmp	ip, lr
	ldrne	r0, [r3, #2500]
	addne	r0, r0, ip
	strne	r0, [r3, #2500]
	b	.L189
.L194:
	.align	2
.L193:
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
	beq	.L196
	bl	ftl_set_blk_mode.part.6
	ldmfd	sp, {fp, sp, pc}
.L196:
	ldr	r3, .L198
	lsr	r1, r0, #5
	mov	ip, #1
	and	r0, r0, #31
	ldr	r2, [r3, #24]
	ldr	r3, [r2, r1, lsl #2]
	bic	r0, r3, ip, lsl r0
	str	r0, [r2, r1, lsl #2]
	ldmfd	sp, {fp, sp, pc}
.L199:
	.align	2
.L198:
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
	ldr	r3, .L201
	uxth	r0, r0
	ldr	r3, [r3, #24]
	lsr	r2, r0, #5
	and	r0, r0, #31
	ldr	r3, [r3, r2, lsl #2]
	lsr	r0, r3, r0
	and	r0, r0, #1
	ldmfd	sp, {fp, sp, pc}
.L202:
	.align	2
.L201:
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
	ldr	r3, .L210
	add	ip, r0, r2, lsl #1
	movw	r1, #65535
	add	ip, ip, #14
	ldrh	lr, [r3, #32]
.L204:
	cmp	r2, lr
	bcc	.L206
	ldrh	r1, [r3, #102]
	add	ip, r0, #16
	mov	r3, #0
	movw	r4, #65535
	sub	r1, r1, #1
	sub	r1, r1, r5
	uxth	r1, r1
.L207:
	uxth	r2, r3
	cmp	lr, r2
	bhi	.L209
	ldmfd	sp, {r4, r5, fp, sp, pc}
.L206:
	ldrh	r4, [ip, #2]!
	add	r2, r2, #1
	uxth	r2, r2
	cmp	r4, r1
	ldrhne	r4, [r0, #4]
	addne	r4, r4, #1
	strhne	r4, [r0, #4]	@ movhi
	b	.L204
.L209:
	ldrh	r2, [ip], #2
	add	r3, r3, #1
	cmp	r2, r4
	ldrhne	r2, [r0, #4]
	addne	r2, r1, r2
	strhne	r2, [r0, #4]	@ movhi
	b	.L207
.L211:
	.align	2
.L210:
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
	ldr	r2, .L218
	ldrh	ip, [r2, #32]
	mov	r2, r3
.L215:
	cmp	r1, r2
	beq	.L217
	ldmfd	sp, {fp, sp, pc}
.L217:
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
	b	.L215
.L219:
	.align	2
.L218:
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
	ldr	r6, .L225
	ldrh	r2, [r0]
	ldrh	r3, [r6, #40]
	cmp	r2, r3
	bcc	.L221
	movw	r2, #2110
	ldr	r1, .L225+4
	ldr	r0, .L225+8
	bl	sftl_printk
.L221:
	ldrh	r9, [r6, #32]
	add	r7, r4, #16
	ldr	r10, .L225+12
	mvn	r8, #0
	mov	r5, #0
	strh	r5, [r4, #4]	@ movhi
	strb	r5, [r4, #7]
.L222:
	uxth	r3, r5
	cmp	r9, r3
	bhi	.L224
	ldrb	r3, [r4, #7]	@ zero_extendqisi2
	mov	r0, #0
	ldrh	r2, [r6, #102]
	strb	r0, [r4, #9]
	smulbb	r3, r3, r2
	strh	r3, [r4, #4]	@ movhi
	ldmib	sp, {r4, r5, r6, r7, r8, r9, r10, fp, sp, pc}
.L224:
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
	b	.L222
.L226:
	.align	2
.L225:
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
	ldr	r3, .L233
	mov	r5, #0
	uxth	r6, r0
	mov	r4, r5
	ldrh	r8, [r3, #32]
	mov	r7, r3
	ldrh	r9, [r3, #102]
	add	r10, r3, #60
.L228:
	uxth	r3, r5
	cmp	r8, r3
	bhi	.L230
	cmp	r4, #0
	moveq	r0, r4
	beq	.L231
	mov	r1, r4
	mov	r0, #32768
	bl	__divsi3
.L231:
	ldr	r1, [r7, #2316]
	add	r1, r1, r6, lsl #3
	strh	r0, [r1, #4]	@ movhi
	mov	r0, #0
	ldmfd	sp, {r4, r5, r6, r7, r8, r9, r10, fp, sp, pc}
.L230:
	mov	r1, r6
	ldrb	r0, [r10, r5]	@ zero_extendqisi2
	bl	V2P_block
	bl	FtlBbmIsBadBlock
	cmp	r0, #0
	add	r5, r5, #1
	addeq	r4, r4, r9
	uxtheq	r4, r4
	b	.L228
.L234:
	.align	2
.L233:
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
	ldr	r3, .L238
	ldr	r0, [r3, #2340]
	cmp	r0, #0
	ldrne	r2, [r3, #2316]
	ldrne	r3, [r3, #2328]
	subne	r0, r0, r2
	ubfxne	r0, r0, #3, #16
	lslne	r0, r0, #1
	ldrhne	r0, [r3, r0]
	ldmfd	sp, {fp, sp, pc}
.L239:
	.align	2
.L238:
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
	ldr	r2, .L248
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
.L243:
	uxth	r0, r1
	cmp	r4, r0
	bls	.L245
	lsl	r0, r3, #3
	add	r1, r1, #1
	ldrh	lr, [ip, r0]
	cmp	lr, r5
	bne	.L247
.L245:
	ldr	r2, [r2, #2328]
	lsl	r3, r3, #1
	ldrh	r0, [r2, r3]
	ldmfd	sp, {r4, r5, fp, sp, pc}
.L247:
	mov	r3, lr
	b	.L243
.L249:
	.align	2
.L248:
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
	ldr	r5, .L262
	add	r4, r6, #12
	ldr	r1, .L262+4
	movw	r8, #2344
	bl	strcpy
	ldr	r3, [r5, #116]
	mov	r1, #64
	ldr	r2, .L262+8
	mov	r0, r4
	bl	snprintf
	add	r4, r4, r0
	ldr	r1, .L262+12
	mov	r0, r4
	add	r4, r4, #10
	bl	strcpy
	ldr	r3, [r5, #2504]
	mov	r1, #64
	ldr	r2, .L262+16
	mov	r0, r4
	bl	snprintf
	add	r4, r4, r0
	ldr	r3, [r5, #2500]
	ldr	r2, .L262+20
	mov	r1, #64
	mov	r0, r4
	ldr	r7, .L262+24
	bl	snprintf
	add	r4, r4, r0
	ldr	r3, [r5, #2508]
	ldr	r2, .L262+28
	mov	r1, #64
	mov	r0, r4
	ldr	r9, .L262+32
	bl	snprintf
	add	r4, r4, r0
	ldr	r3, [r5, #2512]
	ldr	r2, .L262+36
	mov	r1, #64
	mov	r0, r4
	bl	snprintf
	add	r4, r4, r0
	ldr	r3, [r5, #2516]
	ldr	r2, .L262+40
	mov	r1, #64
	mov	r0, r4
	bl	snprintf
	add	r4, r4, r0
	ldr	r3, [r5, #2520]
	ldr	r2, .L262+44
	mov	r1, #64
	mov	r0, r4
	bl	snprintf
	add	r4, r4, r0
	ldr	r3, [r5, #2524]
	ldr	r2, .L262+48
	mov	r1, #64
	mov	r0, r4
	bl	snprintf
	add	r4, r4, r0
	ldr	r3, [r5, #2528]
	ldr	r2, .L262+52
	mov	r1, #64
	mov	r0, r4
	bl	snprintf
	ldr	r3, [r5, #2532]
	add	r4, r4, r0
	ldr	r2, .L262+56
	mov	r1, #64
	mov	r0, r4
	lsr	r3, r3, #11
	bl	snprintf
	ldr	r3, [r5, #2536]
	add	r4, r4, r0
	ldr	r2, .L262+60
	mov	r1, #64
	mov	r0, r4
	lsr	r3, r3, #11
	bl	snprintf
	add	r4, r4, r0
	ldr	r3, [r5, #2540]
	ldr	r2, .L262+64
	mov	r1, #64
	mov	r0, r4
	bl	snprintf
	add	r4, r4, r0
	ldr	r3, [r5, #2544]
	ldr	r2, .L262+68
	mov	r1, #64
	mov	r0, r4
	bl	snprintf
	add	r4, r4, r0
	ldrh	r3, [r5, #158]
	ldr	r2, .L262+72
	mov	r1, #64
	mov	r0, r4
	bl	snprintf
	add	r4, r4, r0
	ldrh	r3, [r5, r8]
	ldr	r2, .L262+76
	mov	r1, #64
	mov	r0, r4
	bl	snprintf
	add	r4, r4, r0
	ldr	r3, [r5, #2548]
	ldr	r2, .L262+80
	mov	r1, #64
	mov	r0, r4
	bl	snprintf
	add	r4, r4, r0
	ldr	r3, [r5, #2552]
	ldr	r2, .L262+84
	mov	r1, #64
	mov	r0, r4
	bl	snprintf
	add	r4, r4, r0
	ldr	r3, [r5, #2556]
	ldr	r2, .L262+88
	mov	r1, #64
	mov	r0, r4
	bl	snprintf
	add	r4, r4, r0
	ldr	r3, [r5, #2560]
	ldr	r2, .L262+92
	mov	r1, #64
	mov	r0, r4
	bl	snprintf
	add	r4, r4, r0
	ldr	r3, [r5, #2564]
	ldr	r2, .L262+96
	mov	r1, #64
	mov	r0, r4
	bl	snprintf
	add	r4, r4, r0
	ldr	r3, [r5, #2568]
	ldr	r2, .L262+100
	mov	r1, #64
	mov	r0, r4
	bl	snprintf
	add	r4, r4, r0
	ldrh	r3, [r7, #30]
	ldr	r2, .L262+104
	mov	r1, #64
	mov	r0, r4
	bl	snprintf
	add	r4, r4, r0
	ldrh	r3, [r7, #28]
	ldr	r2, .L262+108
	mov	r1, #64
	mov	r0, r4
	movw	r7, #2348
	bl	snprintf
	add	r4, r4, r0
	ldr	r3, [r5, #140]
	ldr	r2, .L262+112
	mov	r1, #64
	mov	r0, r4
	bl	snprintf
	add	r4, r4, r0
	ldr	r3, [r5, #132]
	ldr	r2, .L262+116
	mov	r1, #64
	mov	r0, r4
	bl	snprintf
	add	r4, r4, r0
	ldr	r3, [r5, #28]
	ldr	r2, .L262+120
	mov	r1, #64
	mov	r0, r4
	bl	snprintf
	add	r4, r4, r0
	ldrh	r3, [r5, #218]
	ldr	r2, .L262+124
	mov	r1, #64
	mov	r0, r4
	bl	snprintf
	add	r4, r4, r0
	ldrh	r3, [r5, #40]
	ldr	r2, .L262+128
	mov	r1, #64
	mov	r0, r4
	bl	snprintf
	movw	r3, #2572
	add	r4, r4, r0
	ldrh	r3, [r5, r3]
	mov	r1, #64
	ldr	r2, .L262+132
	mov	r0, r4
	bl	snprintf
	add	r4, r4, r0
	ldr	r3, [r5, #44]
	ldr	r2, .L262+136
	mov	r1, #64
	mov	r0, r4
	bl	snprintf
	add	r3, r5, #2576
	add	r4, r4, r0
	ldrh	r3, [r3]
	mov	r1, #64
	ldr	r2, .L262+140
	mov	r0, r4
	bl	snprintf
	add	r4, r4, r0
	ldrh	r3, [r5, #152]
	ldr	r2, .L262+144
	mov	r1, #64
	mov	r0, r4
	bl	snprintf
	add	r4, r4, r0
	ldrh	r3, [r9, #2]
	ldr	r2, .L262+148
	mov	r1, #64
	mov	r0, r4
	bl	snprintf
	add	r4, r4, r0
	ldrb	r3, [r5, #2354]	@ zero_extendqisi2
	ldr	r2, .L262+152
	mov	r1, #64
	mov	r0, r4
	bl	snprintf
	add	r4, r4, r0
	ldrh	r3, [r5, r7]
	ldr	r2, .L262+156
	mov	r1, #64
	mov	r0, r4
	bl	snprintf
	add	r4, r4, r0
	ldrb	r3, [r5, #2356]	@ zero_extendqisi2
	ldr	r2, .L262+160
	mov	r1, #64
	mov	r0, r4
	bl	snprintf
	add	r4, r4, r0
	ldrh	r3, [r9, #4]
	ldr	r2, .L262+164
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
	ldr	r2, .L262+168
	bl	snprintf
	add	r4, r4, r0
	ldrh	r3, [r9, #50]
	ldr	r2, .L262+172
	mov	r1, #64
	mov	r0, r4
	bl	snprintf
	add	r4, r4, r0
	ldrb	r3, [r5, #2402]	@ zero_extendqisi2
	ldr	r2, .L262+176
	mov	r1, #64
	mov	r0, r4
	bl	snprintf
	add	r4, r4, r0
	ldrh	r3, [r5, r7]
	ldr	r2, .L262+180
	mov	r1, #64
	mov	r0, r4
	bl	snprintf
	add	r4, r4, r0
	ldrb	r3, [r5, #2404]	@ zero_extendqisi2
	ldr	r2, .L262+184
	mov	r1, #64
	mov	r0, r4
	bl	snprintf
	add	r4, r4, r0
	ldrh	r3, [r9, #52]
	ldr	r2, .L262+188
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
	ldr	r2, .L262+192
	bl	snprintf
	add	r4, r4, r0
	ldrh	r3, [r9, #98]
	ldr	r2, .L262+196
	mov	r1, #64
	mov	r0, r4
	bl	snprintf
	add	r4, r4, r0
	ldrb	r3, [r5, #2450]	@ zero_extendqisi2
	ldr	r2, .L262+200
	mov	r1, #64
	mov	r0, r4
	bl	snprintf
	movw	r3, #2444
	add	r4, r4, r0
	ldrh	r3, [r5, r3]
	mov	r1, #64
	ldr	r2, .L262+204
	mov	r0, r4
	bl	snprintf
	add	r4, r4, r0
	ldrb	r3, [r5, #2452]	@ zero_extendqisi2
	ldr	r2, .L262+208
	mov	r1, #64
	mov	r0, r4
	bl	snprintf
	add	r4, r4, r0
	ldrh	r3, [r9, #100]
	ldr	r2, .L262+212
	mov	r1, #64
	mov	r0, r4
	bl	snprintf
	add	r4, r4, r0
	ldrh	r3, [r9, #242]
	ldr	r2, .L262+216
	mov	r1, #64
	mov	r0, r4
	bl	snprintf
	add	r4, r4, r0
	ldrb	r3, [r5, #2594]	@ zero_extendqisi2
	ldr	r2, .L262+220
	mov	r1, #64
	mov	r0, r4
	bl	snprintf
	add	r4, r4, r0
	ldrh	r3, [r5, r7]
	ldr	r2, .L262+224
	mov	r1, #64
	mov	r0, r4
	bl	snprintf
	add	r4, r4, r0
	ldrb	r3, [r5, #2596]	@ zero_extendqisi2
	ldr	r2, .L262+228
	mov	r1, #64
	mov	r0, r4
	bl	snprintf
	add	r4, r4, r0
	ldrh	r3, [r9, #244]
	ldr	r2, .L262+232
	mov	r1, #64
	mov	r0, r4
	bl	snprintf
	ldr	r3, [r5, #2712]
	add	r4, r4, r0
	ldr	r2, .L262+236
	mov	r1, #64
	mov	r0, r4
	str	r3, [sp, #4]
	ldr	r3, [r5, #2720]
	str	r3, [sp]
	ldr	r3, [r5, #2716]
	bl	snprintf
	add	r4, r4, r0
	ldr	r3, [r5, #2708]
	ldr	r2, .L262+240
	mov	r1, #64
	mov	r0, r4
	bl	snprintf
	add	r4, r4, r0
	ldr	r3, [r5, #2732]
	ldr	r2, .L262+244
	mov	r1, #64
	mov	r0, r4
	bl	snprintf
	movw	r3, #3148
	add	r4, r4, r0
	ldrh	r3, [r5, r3]
	mov	r1, #64
	ldr	r2, .L262+248
	mov	r0, r4
	bl	snprintf
	movw	r3, #3150
	add	r4, r4, r0
	ldrh	r3, [r5, r3]
	mov	r1, #64
	ldr	r2, .L262+252
	mov	r0, r4
	bl	snprintf
	add	r4, r4, r0
	ldr	r3, [r5, #3152]
	ldr	r2, .L262+256
	mov	r1, #64
	mov	r0, r4
	bl	snprintf
	movw	r3, #3156
	add	r4, r4, r0
	ldrh	r3, [r5, r3]
	mov	r1, #64
	ldr	r2, .L262+260
	mov	r0, r4
	bl	snprintf
	add	r4, r4, r0
	bl	GetFreeBlockMinEraseCount
	ldr	r2, .L262+264
	mov	r3, r0
	mov	r1, #64
	mov	r0, r4
	bl	snprintf
	add	r4, r4, r0
	ldrh	r0, [r5, r8]
	bl	GetFreeBlockMaxEraseCount
	ldr	r2, .L262+268
	mov	r3, r0
	mov	r1, #64
	mov	r0, r4
	bl	snprintf
	ldr	r3, .L262+272
	add	r4, r4, r0
	ldr	r3, [r3]
	cmp	r3, #1
	beq	.L251
.L256:
	sub	r0, r4, r6
	sub	sp, fp, #40
	ldmfd	sp, {r4, r5, r6, r7, r8, r9, r10, fp, sp, pc}
.L251:
	ldrh	r3, [r5, r7]
	movw	r2, #65535
	cmp	r3, r2
	beq	.L253
	ldr	r2, [r5, #2324]
	lsl	r3, r3, #1
	mov	r0, r4
	mov	r1, #64
	ldrh	r3, [r2, r3]
	ldr	r2, .L262+276
	bl	snprintf
	add	r4, r4, r0
.L253:
	mov	r0, #0
	ldr	r9, .L262+280
	bl	List_get_gc_head_node
	uxth	r0, r0
	mov	r7, #0
	movw	r10, #65535
.L255:
	cmp	r0, r10
	beq	.L254
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
	bne	.L255
.L254:
	ldr	r3, [r5, #2340]
	mov	r7, #0
	ldr	r2, [r5, #2316]
	movw	r9, #65535
	ldr	r10, .L262+284
	sub	r3, r3, r2
	ubfx	r3, r3, #3, #16
.L257:
	cmp	r3, r9
	beq	.L256
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
	beq	.L256
	ldr	r3, [r5, #2316]
	ldrh	r3, [r3, r8]
	b	.L257
.L263:
	.align	2
.L262:
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
	blt	.L266
	ldr	r3, .L267
	mov	r1, #64
	ldr	r2, .L267+4
	mov	r0, r5
	bl	snprintf
	add	r4, r5, r0
	mov	r0, r4
	bl	FtlPrintInfo2buf
	add	r0, r4, r0
	sub	r0, r0, r5
	ldmfd	sp, {r4, r5, fp, sp, pc}
.L266:
	mov	r0, #0
	ldmfd	sp, {r4, r5, fp, sp, pc}
.L268:
	.align	2
.L267:
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
	ldr	r4, .L293
	ldr	r2, [r4, #2556]
	ldr	r3, [r4, #2568]
	cmp	r2, r3
	bcs	.L270
	ldr	r2, [r4, #2328]
	mov	r3, #0
	ldrh	r1, [r4, #40]
	str	r3, [r4, #2548]
	sub	r2, r2, #2
.L271:
	cmp	r3, r1
	bcc	.L272
	ldr	r5, [r4, #2548]
	mov	r0, r5
	bl	__udivsi3
	str	r0, [r4, #2556]
	ldr	r0, [r4, #2552]
	ldrh	r1, [r4, #92]
	sub	r0, r5, r0
	bl	__udivsi3
	str	r0, [r4, #2548]
.L273:
	ldr	r6, [r4, #2568]
	ldr	r7, [r4, #2556]
	add	r3, r6, #256
	cmp	r3, r7
	bls	.L278
	ldr	r2, [r4, #2564]
	add	r3, r6, #768
	cmp	r3, r2
	bls	.L278
.L280:
	movw	r5, #65535
.L279:
	mov	r0, r5
	sub	sp, fp, #40
	ldmfd	sp, {r4, r5, r6, r7, r8, r9, r10, fp, sp, pc}
.L272:
	ldrh	r0, [r2, #2]!
	add	r3, r3, #1
	ldr	ip, [r4, #2548]
	add	r0, r0, ip
	str	r0, [r4, #2548]
	b	.L271
.L270:
	ldr	r3, [r4, #2564]
	cmp	r2, r3
	addhi	r3, r3, #1
	strhi	r3, [r4, #2564]
	movhi	r3, #0
	bls	.L273
.L275:
	ldrh	r2, [r4, #40]
	cmp	r3, r2
	bcs	.L273
	ldr	r0, [r4, #2328]
	lsl	r1, r3, #1
	add	r3, r3, #1
	ldrh	r2, [r0, r1]
	add	r2, r2, #1
	strh	r2, [r0, r1]	@ movhi
	b	.L275
.L278:
	movw	r3, #2344
	ldrh	r0, [r4, r3]
	add	r0, r0, r0, lsl #1
	asr	r0, r0, #2
	bl	GetFreeBlockMaxEraseCount
	add	r3, r6, #64
	mov	r9, r0
	cmp	r0, r3
	bcc	.L280
	ldr	r3, [r4, #2320]
	cmp	r3, #0
	beq	.L280
	ldrh	r10, [r4, #40]
	movw	r1, #65535
	ldr	lr, [r4, #2316]
	mov	r0, #0
	ldr	r8, [r4, #2328]
	mov	r2, r1
.L281:
	ldrh	ip, [r3]
	movw	r5, #65535
	cmp	ip, r5
	bne	.L284
	mov	r5, r2
.L283:
	movw	r3, #65535
	cmp	r5, r3
	beq	.L280
	lsl	r3, r5, #1
	ldrh	r10, [r8, r3]
	cmp	r6, r10
	bcs	.L285
	str	r3, [fp, #-44]
	bl	GetFreeBlockMinEraseCount
	ldr	r3, [fp, #-44]
	cmp	r6, r0
	strcc	r1, [r4, #2568]
.L285:
	cmp	r7, r10
	bls	.L280
	add	r2, r10, #128
	cmp	r9, r2
	ble	.L280
	add	r2, r10, #256
	cmp	r7, r2
	bhi	.L286
	ldr	r2, [r4, #2564]
	add	r10, r10, #768
	cmp	r10, r2
	bcs	.L280
.L286:
	str	r9, [sp, #8]
	mov	r1, r5
	ldrh	r2, [r8, r3]
	ldr	r0, .L293+4
	str	r2, [sp, #4]
	ldr	r2, [r4, #2324]
	ldrh	r3, [r2, r3]
	mov	r2, r7
	str	r3, [sp]
	ldr	r3, [r4, #2564]
	bl	sftl_printk
	mov	r3, #1
	str	r3, [r4, #3160]
	b	.L279
.L284:
	add	r0, r0, #1
	uxth	r0, r0
	cmp	r0, r10
	bhi	.L280
	ldrh	r5, [r3, #4]
	cmp	r5, #0
	beq	.L282
	sub	r3, r3, lr
	asr	r3, r3, #3
	uxth	r5, r3
	lsl	r3, r5, #1
	ldrh	r3, [r8, r3]
	cmp	r6, r3
	bcs	.L283
	cmp	r1, r3
	movhi	r1, r3
	movhi	r2, r5
.L282:
	add	r3, lr, ip, lsl #3
	b	.L281
.L294:
	.align	2
.L293:
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
	beq	.L296
	ldr	r3, .L297
	mov	r1, #0
	ldr	r2, [r3, #2324]
	lsl	r3, r0, #1
	strh	r1, [r2, r3]	@ movhi
	bl	INSERT_FREE_LIST
.L296:
	mov	r0, #0
	ldmfd	sp, {fp, sp, pc}
.L298:
	.align	2
.L297:
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
	bne	.L300
	movw	r2, #2740
	ldr	r1, .L313
	ldr	r0, .L313+4
	bl	sftl_printk
.L300:
	ldr	r5, .L313+8
	ldrh	r2, [r4, #2]
	ldrh	r3, [r5, #102]
	cmp	r2, r3
	bne	.L301
	movw	r2, #2741
	ldr	r1, .L313
	ldr	r0, .L313+4
	bl	sftl_printk
.L301:
	ldrh	r3, [r4, #4]
	cmp	r3, #0
	bne	.L302
	movw	r2, #2742
	ldr	r1, .L313
	ldr	r0, .L313+4
	bl	sftl_printk
.L302:
	ldrb	r2, [r4, #6]	@ zero_extendqisi2
	mov	r3, #0
	ldrh	r1, [r5, #32]
	movw	ip, #65535
	strb	r3, [r4, #10]
	add	r2, r4, r2, lsl #1
	ldrh	r0, [r2, #16]
	mov	r2, r3
.L303:
	cmp	r0, ip
	ldrb	r3, [r4, #6]	@ zero_extendqisi2
	beq	.L305
	ldrh	r2, [r4, #4]
	ldrh	r6, [r4, #2]
	sub	r2, r2, #1
	uxth	r2, r2
	orr	r6, r6, r0, lsl #10
	movw	r0, #65535
	strh	r2, [r4, #4]	@ movhi
.L307:
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
	beq	.L307
	strb	r3, [r4, #6]
	cmp	r2, #0
	ldrh	r3, [r4, #2]
	ldrh	r1, [r5, #102]
	sub	r3, r3, r1
	clz	r3, r3
	lsr	r3, r3, #5
	moveq	r3, #0
	cmp	r3, #0
	beq	.L299
	movw	r2, #2763
	ldr	r1, .L313
	ldr	r0, .L313+4
	bl	sftl_printk
.L299:
	mov	r0, r6
	ldmfd	sp, {r4, r5, r6, fp, sp, pc}
.L305:
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
	b	.L303
.L314:
	.align	2
.L313:
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
	ldr	r3, .L320
	mov	lr, #12
	mov	r4, #1
	mov	r5, #20
	mov	r2, #0
	str	r2, [r3, #3164]
.L316:
	ldrh	r1, [r3, #32]
	uxth	r0, r2
	add	ip, r2, #1
	cmp	r0, r1
	bcc	.L317
	mov	ip, #12
	mov	lr, #0
.L318:
	ldr	r2, [r3, #3184]
	cmp	r1, r2
	bcc	.L319
	ldmfd	sp, {r4, r5, r6, r7, r8, fp, sp, pc}
.L317:
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
	b	.L316
.L319:
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
	b	.L318
.L321:
	.align	2
.L320:
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
	ldr	r3, .L329
	mov	lr, #0
	mov	r5, #20
	mov	r7, #12
	mov	r8, lr
	ldr	r6, [r3, #3184]
	ldr	r4, [r3, #3168]
.L323:
	uxth	r3, lr
	cmp	r1, r3
	ldmfdls	sp, {r4, r5, r6, r7, r8, r9, r10, fp, sp, pc}
	mla	ip, r5, r3, r0
	mov	r2, #0
.L324:
	uxth	r3, r2
	cmp	r6, r3
	bls	.L325
	mul	r3, r7, r3
	add	r2, r2, #1
	ldr	r10, [r4, r3]
	add	r9, r4, r3
	ldr	r3, [ip, #8]
	cmp	r10, r3
	bne	.L324
	str	r8, [r9, #8]
.L325:
	add	lr, lr, #1
	b	.L323
.L330:
	.align	2
.L329:
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
	ldr	r3, .L338
	mov	ip, #0
	mov	r6, #12
	mov	r7, #1
	mov	r8, #20
	ldr	r4, [r3, #3184]
	ldr	r5, [r3, #3168]
.L332:
	uxth	r2, ip
	cmp	r1, r2
	bhi	.L336
	ldmfd	sp, {r4, r5, r6, r7, r8, r9, fp, sp, pc}
.L336:
	mov	lr, #0
.L333:
	uxth	r3, lr
	cmp	r4, r3
	bls	.L334
	mla	r3, r6, r3, r5
	add	lr, lr, #1
	ldr	r9, [r3, #8]
	cmp	r9, #0
	bne	.L333
	mla	r2, r8, r2, r0
	ldr	lr, [r3]
	str	r7, [r3, #8]
	str	lr, [r2, #8]
	ldr	r3, [r3, #4]
	str	r3, [r2, #12]
.L334:
	add	ip, ip, #1
	b	.L332
.L339:
	.align	2
.L338:
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
	ldr	r2, .L345
	movw	r1, #3192
	uxth	r0, r0
	ldr	r3, [r2, #3188]
	ldrh	r2, [r2, r1]
	add	r2, r3, r2, lsl #1
.L341:
	cmp	r3, r2
	bne	.L343
	mov	r0, #0
	ldmfd	sp, {fp, sp, pc}
.L343:
	ldrh	r1, [r3], #2
	cmp	r1, r0
	bne	.L341
	mov	r0, #1
	ldmfd	sp, {fp, sp, pc}
.L346:
	.align	2
.L345:
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
	ldr	ip, .L351
	movw	r3, #3192
	ldr	r2, [ip, #3188]
	ldrh	lr, [ip, r3]
	mov	r3, #0
	sub	r1, r2, #2
.L348:
	uxth	r7, r3
	cmp	r7, lr
	bcc	.L350
	bne	.L349
	lsl	r3, r7, #1
	strh	r0, [r2, r3]	@ movhi
	movw	r2, #3192
	ldrh	r3, [ip, r2]
	add	r3, r3, #1
	strh	r3, [ip, r2]	@ movhi
	b	.L349
.L350:
	ldrh	r7, [r1, #2]!
	add	r3, r3, #1
	cmp	r7, r0
	bne	.L348
.L349:
	ldr	r0, .L351+4
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
.L352:
	.align	2
.L351:
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
	ldr	r0, .L356
	mov	r1, r4
	bl	sftl_printk
	ldr	r3, .L356+4
	movw	ip, #3202
	ldrh	lr, [r3, ip]
	cmp	r4, lr
	beq	.L354
	movw	r2, #3204
	ldrh	r0, [r3, r2]
	cmp	r4, r0
	beq	.L354
	movw	r1, #65535
	cmp	lr, r1
	strheq	r4, [r3, ip]	@ movhi
	beq	.L354
	cmp	r0, r1
	strheq	r4, [r3, r2]	@ movhi
.L354:
	mov	r0, #0
	ldmfd	sp, {r4, fp, sp, pc}
.L357:
	.align	2
.L356:
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
	ldr	r4, .L362
	uxth	r5, r0
	movw	r6, #3206
	mov	r0, r5
	bl	P2V_block_in_plane
	mov	r2, r5
	mov	r7, r0
	ldrh	r1, [r4, r6]
	ldr	r0, .L362+4
	bl	sftl_printk
	mov	r0, r7
	bl	FtlGcRefreshBlock
	ldrh	r3, [r4, r6]
	mov	r2, #0
	ldr	r0, .L362+8
.L359:
	uxth	r1, r2
	cmp	r3, r1
	bhi	.L361
	cmp	r3, #15
	movwls	r2, #3206
	addls	r1, r3, #1
	strhls	r1, [r4, r2]	@ movhi
	lslls	r3, r3, #1
	ldrls	r2, .L362+8
	strhls	r5, [r2, r3]	@ movhi
	b	.L360
.L361:
	add	r2, r2, #1
	add	r1, r0, r2, lsl #1
	ldrh	r1, [r1, #-2]
	cmp	r1, r5
	bne	.L359
.L360:
	mov	r0, #0
	ldmfd	sp, {r4, r5, r6, r7, fp, sp, pc}
.L363:
	.align	2
.L362:
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
	ldr	r4, .L370
	movw	r3, #3206
	ldrh	r2, [r4, r3]
	cmp	r2, #0
	beq	.L365
	movw	r3, #3202
	ldrh	r1, [r4, r3]
	movw	r3, #65535
	cmp	r1, r3
	bne	.L365
	movw	r3, #3242
	movw	r5, #3242
	ldrh	r1, [r4, r3]
	cmp	r1, r2
	movcs	r2, #0
	strhcs	r2, [r4, r3]	@ movhi
	ldrh	r3, [r4, r5]
	ldr	r2, .L370+4
	lsl	r3, r3, #1
	ldrh	r0, [r2, r3]
	bl	P2V_block_in_plane
	bl	FtlGcRefreshBlock
	ldrh	r3, [r4, r5]
	add	r3, r3, #1
	strh	r3, [r4, r5]	@ movhi
.L365:
	mov	r0, #0
	ldmfd	sp, {r4, r5, fp, sp, pc}
.L371:
	.align	2
.L370:
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
	ldr	r1, .L373
	bl	__kmalloc
	ldmfd	sp, {fp, sp, pc}
.L374:
	.align	2
.L373:
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
.L377:
	cmp	r4, r9
	bcc	.L383
	ldr	r0, .L385
	bl	sftl_printk
	ldmfd	sp, {r4, r5, r6, r7, r8, r9, r10, fp, sp, pc}
.L383:
	cmp	r5, #0
	bne	.L378
	mov	r3, r6
	mov	r2, r7
	mov	r1, r10
	ldr	r0, .L385+4
	bl	sftl_printk
.L378:
	cmp	r8, #4
	ldreq	r1, [r7, r4, lsl #2]
	ldreq	r0, .L385+8
	beq	.L384
	cmp	r8, #2
	lsleq	r3, r4, #1
	ldreq	r0, .L385+12
	ldrbne	r1, [r7, r4]	@ zero_extendqisi2
	ldrheq	r1, [r7, r3]
	ldrne	r0, .L385+16
.L384:
	add	r5, r5, #1
	bl	sftl_printk
	cmp	r5, #15
	bls	.L382
	mov	r5, #0
	ldr	r0, .L385
	bl	sftl_printk
.L382:
	add	r4, r4, #1
	add	r6, r6, r8
	b	.L377
.L386:
	.align	2
.L385:
	.word	.LC79
	.word	.LC75
	.word	.LC76
	.word	.LC77
	.word	.LC78
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
	ldr	r6, .L403
	mov	r8, r2
	mov	r4, r0
	mov	r5, r0
	ldrh	r9, [r6, #12]
	lsl	r10, r9, #3
.L388:
	cmp	r7, r8
	beq	.L392
	sub	r2, fp, #48
	sub	r1, fp, #44
	mov	r0, r5
	bl	l2p_addr_tran
	ldr	ip, [fp, #-48]
	cmp	ip, #0
	bne	.L389
	ldr	r3, [fp, #-44]
	cmp	r10, r3
	bls	.L389
	ldr	r5, .L403+4
	ldr	r6, .L403+8
	b	.L402
.L391:
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
	ldr	r0, .L403+12
	bl	rknand_print_hex
	mov	r3, #4
	ldr	r1, [r4, #-8]
	mov	r2, r3
	ldr	r0, .L403+16
	bl	rknand_print_hex
	ldr	ip, [fp, #-52]
	add	ip, ip, #1
.L402:
	cmp	ip, r8
	add	r4, r4, #20
	bne	.L391
	bl	dump_stack
.L392:
	mov	r0, #0
	sub	sp, fp, #40
	ldmfd	sp, {r4, r5, r6, r7, r8, r9, r10, fp, sp, pc}
.L389:
	ldr	r3, [r6, #3248]
	uxtb	r0, ip
	ldr	r1, [fp, #-44]
	blx	r3
	cmp	r0, #0
	mvnne	r3, #0
	streq	r0, [r5]
	strne	r3, [r5]
	ldrh	r3, [r6, #14]
	cmp	r3, #4
	bne	.L396
	ldr	r1, [fp, #-44]
	ldr	r3, [r6, #3248]
	ldrb	r0, [fp, #-48]	@ zero_extendqisi2
	add	r1, r9, r1
	blx	r3
	cmp	r0, #0
	mvnne	r3, #0
	strne	r3, [r5]
.L396:
	add	r7, r7, #1
	add	r5, r5, #20
	b	.L388
.L404:
	.align	2
.L403:
	.word	.LANCHOR0
	.word	.LANCHOR1+130
	.word	.LC80
	.word	.LC81
	.word	.LC82
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
	ldr	r4, .L414
	ldrh	r3, [r4, #218]
	cmp	r3, #1024
	ldmfdeq	sp, {r4, r5, r6, fp, sp, pc}
	cmp	r1, #0
	beq	.L407
	mov	r0, r5
	bl	P2V_block_in_plane
	mov	r6, r0
	ldr	r0, [r4, #3268]
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
.L407:
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
.L415:
	.align	2
.L414:
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
	push	{r4, r5, fp, ip, lr, pc}
	sub	fp, ip, #4
	ldr	r4, .L421
	ldrh	r2, [r4, #218]
	cmp	r2, #0
	movweq	r5, #65535
	beq	.L417
	ldrh	r3, [r4, #214]
	sub	r2, r2, #1
	ldr	r0, [r4, #3268]
	strh	r2, [r4, #218]	@ movhi
	mov	r2, #1
	add	r1, r4, r3, lsl #1
	add	r3, r3, #1
	ubfx	r3, r3, #0, #10
	ldrh	r5, [r1, #220]
	mov	r1, r2
	strh	r3, [r4, #214]	@ movhi
	lsl	r3, r5, #10
	str	r3, [r0, #4]
	bl	FlashEraseBlocks
	ldr	r3, [r4, #2560]
	add	r3, r3, #1
	str	r3, [r4, #2560]
.L417:
	sub	r3, r5, #1
	movw	r2, #65533
	uxth	r3, r3
	cmp	r3, r2
	bls	.L418
	ldrh	r2, [r4, #218]
	mov	r1, r5
	ldr	r0, .L421+4
	bl	sftl_printk
.L419:
	b	.L419
.L418:
	mov	r0, r5
	ldmfd	sp, {r4, r5, fp, sp, pc}
.L422:
	.align	2
.L421:
	.word	.LANCHOR0
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
.L424:
	uxth	r5, r3
	cmp	r5, r1
	bcs	.L427
	mov	r7, r2
	add	r3, r3, #1
	ldrh	r6, [r7]
	add	r2, r2, #2
	cmp	r6, #0
	bne	.L424
	bl	FtlFreeSysBlkQueueOut
	sub	r3, r0, #1
	movw	r2, #65533
	uxth	r3, r3
	mov	r1, r0
	strh	r0, [r7]	@ movhi
	cmp	r3, r2
	bls	.L425
	ldr	r3, .L431
	ldr	r0, .L431+4
	ldrh	r2, [r3, #218]
	bl	sftl_printk
.L426:
	b	.L426
.L425:
	ldr	r3, [r4, #28]
	strh	r6, [r4, #2]	@ movhi
	strh	r5, [r4]	@ movhi
	add	r3, r3, #1
	str	r3, [r4, #28]
	ldrh	r3, [r4, #8]
	add	r3, r3, #1
	strh	r3, [r4, #8]	@ movhi
.L427:
	ldrh	r3, [r4, #10]
	cmp	r3, r5
	bhi	.L429
	movw	r2, #578
	ldr	r1, .L431+8
	ldr	r0, .L431+12
	bl	sftl_printk
.L429:
	mov	r0, #0
	ldmfd	sp, {r4, r5, r6, r7, fp, sp, pc}
.L432:
	.align	2
.L431:
	.word	.LANCHOR0
	.word	.LC84
	.word	.LANCHOR1+147
	.word	.LC1
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
	ldr	r5, .L440
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
	bls	.L434
	mov	r2, #256
	mov	r1, #255
	mov	r0, r4
	bl	memset
	mov	r0, #0
.L434:
	ldrh	r3, [r5, #14]
	cmp	r3, #4
	moveq	r3, r4
	addeq	r1, r3, r0, lsl #1
	beq	.L436
	ldmfd	sp, {r4, r5, r6, fp, sp, pc}
.L437:
	ldrh	r2, [r3]
	lsr	r2, r2, #1
	strh	r2, [r3], #2	@ movhi
.L436:
	cmp	r3, r1
	bne	.L437
	ldmfd	sp, {r4, r5, r6, fp, sp, pc}
.L441:
	.align	2
.L440:
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
	beq	.L443
	bl	memset
.L443:
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
	ldr	r4, .L548
	movw	r3, #65535
	movw	r2, #3202
	mov	r5, #0
	mov	r7, #12
	str	r3, [r4, #3276]
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
	mov	r6, #20
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
	str	r5, [r4, #3272]
	str	r5, [r4, #3160]
	str	r5, [r4, #3280]
	str	r5, [r4, #3152]
	str	r5, [r4, #3284]
	bl	ftl_malloc
	str	r0, [r4, #3188]
	ldrh	r0, [r4, #106]
	mul	r0, r7, r0
	bl	ftl_malloc
	ldrh	r3, [r4, #32]
	str	r0, [r4, #3196]
	mul	r6, r6, r3
	lsl	r8, r6, #2
	mov	r0, r8
	bl	ftl_malloc
	str	r0, [r4, #3288]
	mov	r0, r6
	bl	ftl_malloc
	str	r0, [r4, #3292]
	mov	r0, r8
	bl	ftl_malloc
	str	r0, [r4, #3296]
	mov	r0, r6
	bl	ftl_malloc
	str	r0, [r4, #3268]
	mov	r0, r6
	bl	ftl_malloc
	ldrh	r3, [r4, #32]
	ldrh	r6, [r4, #110]
	str	r0, [r4, #3180]
	lsl	r3, r3, #1
	mov	r0, r6
	add	r3, r3, #1
	str	r3, [r4, #3184]
	bl	ftl_malloc
	str	r0, [r4, #3300]
	mov	r0, r6
	bl	ftl_malloc
	str	r0, [r4, #3304]
	mov	r0, r6
	bl	ftl_malloc
	str	r0, [r4, #3308]
	ldr	r0, [r4, #3184]
	mul	r0, r0, r6
	bl	ftl_malloc
	str	r0, [r4, #3172]
	mov	r0, r6
	bl	ftl_malloc
	str	r0, [r4, #3312]
	mov	r0, r6
	bl	ftl_malloc
	str	r0, [r4, #3316]
	ldr	r0, [r4, #3184]
	mul	r0, r7, r0
	bl	ftl_malloc
	str	r0, [r4, #3168]
	mov	r0, r6
	bl	ftl_malloc
	str	r0, [r4, #3320]
	mov	r0, r6
	bl	ftl_malloc
	str	r0, [r4, #3324]
	ldrh	r0, [r4, #58]
	lsl	r0, r0, #2
	bl	ftl_malloc
	ldrh	r3, [r4, #112]
	ldrh	r6, [r4, #32]
	str	r0, [r4, #3328]
	mul	r6, r6, r3
	mov	r0, r6
	bl	ftl_malloc
	str	r0, [r4, #3332]
	lsl	r0, r6, #2
	movw	r6, #3340
	bl	ftl_malloc
	ldrh	r3, [r4, #112]
	str	r0, [r4, #3336]
	ldr	r0, [r4, #3184]
	mul	r0, r0, r3
	bl	ftl_malloc
	str	r0, [r4, #3176]
	ldrh	r0, [r4, #42]
	lsl	r0, r0, #1
	uxth	r0, r0
	strh	r0, [r4, r6]	@ movhi
	bl	ftl_malloc
	str	r0, [r4, #3344]
	ldrh	r0, [r4, r6]
	ldr	r3, .L548+4
	add	r0, r0, #544
	add	r0, r0, #3
	lsr	r0, r0, #9
	strh	r0, [r4, r6]	@ movhi
	and	r0, r3, r0, lsl #9
	bl	ftl_malloc
	ldrh	r6, [r4, #42]
	str	r0, [r4, #3348]
	add	r0, r0, #32
	str	r0, [r4, #2328]
	lsl	r6, r6, #1
	mov	r0, r6
	bl	ftl_malloc
	str	r0, [r4, #3352]
	mov	r0, r6
	bl	ftl_malloc
	ldr	r6, [r4, #128]
	str	r0, [r4, #2324]
	lsl	r6, r6, #1
	mov	r0, r6
	bl	ftl_malloc
	str	r0, [r4, #3356]
	mov	r0, r6
	bl	ftl_malloc
	str	r0, [r4, #3360]
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
	str	r0, [r4, #3364]
	ldrh	r0, [r4, #120]
	lsl	r0, r0, #2
	bl	ftl_malloc
	str	r0, [r4, #3368]
	ldrh	r0, [r4, #122]
	lsl	r0, r0, #2
	bl	ftl_malloc
	ldrh	r2, [r4, #122]
	mov	r1, r5
	str	r0, [r4, #3372]
	movw	r5, #3388
	lsl	r2, r2, #2
	bl	ftl_memset
	ldrh	r0, [r4, #136]
	lsl	r0, r0, #2
	bl	ftl_malloc
	str	r0, [r4, #3376]
	ldr	r0, [r4, #128]
	lsl	r0, r0, #2
	bl	ftl_malloc
	str	r0, [r4, #3380]
	ldrh	r0, [r4, #138]
	mul	r0, r7, r0
	bl	ftl_malloc
	ldrh	r3, [r4, #138]
	str	r0, [r4, #2492]
	ldrh	r0, [r4, #110]
	mul	r0, r0, r3
	bl	ftl_malloc
	str	r0, [r4, #3384]
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
.L448:
	cmp	r3, lr
	bcc	.L449
	ldr	r2, .L548+8
	mov	r1, #0
	add	r3, r2, r3, lsl #2
	add	r2, r2, #56
	add	r3, r3, #24
.L450:
	cmp	r2, r3
	bne	.L451
	ldr	r3, [r4, #3356]
	cmp	r3, #0
	bne	.L452
.L454:
	ldr	r1, .L548+12
	ldr	r0, .L548+16
	bl	sftl_printk
	mvn	r0, #0
	ldmfd	sp, {r4, r5, r6, r7, r8, fp, sp, pc}
.L449:
	ldr	r0, [r4, #180]
	add	r3, r3, #1
	add	r0, r0, r1
	add	r1, r1, r2
	str	r0, [ip, #4]!
	b	.L448
.L451:
	str	r1, [r3, #4]!
	b	.L450
.L452:
	ldr	r3, [r4, #3360]
	cmp	r3, #0
	beq	.L454
	ldr	r3, [r4, #3376]
	cmp	r3, #0
	beq	.L454
	ldr	r3, [r4, #3380]
	cmp	r3, #0
	beq	.L454
	ldr	r3, [r4, #2492]
	cmp	r3, #0
	beq	.L454
	ldr	r3, [r4, #3384]
	cmp	r3, #0
	beq	.L454
	ldr	r3, [r4, #2316]
	cmp	r3, #0
	beq	.L454
	ldr	r3, [r4, #180]
	cmp	r3, #0
	beq	.L454
	ldr	r3, [r4, #2324]
	cmp	r3, #0
	beq	.L454
	ldr	r3, [r4, #3188]
	cmp	r3, #0
	beq	.L454
	ldr	r3, [r4, #3196]
	cmp	r3, #0
	beq	.L454
	ldr	r3, [r4, #3288]
	cmp	r3, #0
	beq	.L454
	ldr	r3, [r4, #3296]
	cmp	r3, #0
	beq	.L454
	ldr	r3, [r4, #3268]
	cmp	r3, #0
	beq	.L454
	ldr	r3, [r4, #3180]
	cmp	r3, #0
	beq	.L454
	ldr	r3, [r4, #3292]
	cmp	r3, #0
	beq	.L454
	ldr	r3, [r4, #3300]
	cmp	r3, #0
	beq	.L454
	ldr	r3, [r4, #3304]
	cmp	r3, #0
	beq	.L454
	ldr	r3, [r4, #3308]
	cmp	r3, #0
	beq	.L454
	ldr	r3, [r4, #3172]
	cmp	r3, #0
	beq	.L454
	ldr	r3, [r4, #3312]
	cmp	r3, #0
	beq	.L454
	ldr	r3, [r4, #3316]
	cmp	r3, #0
	beq	.L454
	ldr	r3, [r4, #3168]
	cmp	r3, #0
	beq	.L454
	ldr	r3, [r4, #3332]
	cmp	r3, #0
	beq	.L454
	ldr	r3, [r4, #3336]
	cmp	r3, #0
	beq	.L454
	ldr	r3, [r4, #3176]
	cmp	r3, #0
	beq	.L454
	ldr	r3, [r4, #2328]
	cmp	r3, #0
	beq	.L454
	ldr	r3, [r4, #3344]
	cmp	r3, #0
	beq	.L454
	ldr	r3, [r4, #148]
	cmp	r3, #0
	beq	.L454
	ldr	r3, .L548
	ldr	r2, [r3, #3364]
	cmp	r2, #0
	beq	.L454
	ldr	r2, [r3, #3368]
	cmp	r2, #0
	beq	.L454
	ldr	r3, [r3, #3372]
	cmp	r3, #0
	beq	.L454
	mov	r0, #0
	ldmfd	sp, {r4, r5, r6, r7, r8, fp, sp, pc}
.L549:
	.align	2
.L548:
	.word	.LANCHOR0
	.word	33553920
	.word	.LANCHOR0+152
	.word	.LANCHOR1+173
	.word	.LC85
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
	ldr	r6, .L556
	movw	r3, #3388
	mov	r4, r0
	ldr	r8, .L556+4
	sub	r5, r4, #2
	ldrh	r2, [r6, r3]
	add	r4, r4, #1020
	ldr	r9, .L556+8
	mov	r7, r1
	add	r4, r4, #2
	mov	r1, #0
	mov	r0, r7
	lsl	r2, r2, #2
	bl	ftl_memset
.L553:
	ldrh	r3, [r5, #2]
	movw	r2, #65535
	cmp	r3, r2
	ldmfdeq	sp, {r4, r5, r6, r7, r8, r9, fp, sp, pc}
	ldrh	r2, [r6, #98]
	cmp	r2, r3
	bhi	.L552
	mov	r2, #74
	mov	r1, r8
	mov	r0, r9
	bl	sftl_printk
.L552:
	ldrh	r3, [r5, #2]!
	mov	r0, #1
	cmp	r4, r5
	lsr	r1, r3, #5
	and	r3, r3, #31
	ldr	r2, [r7, r1, lsl #2]
	orr	r3, r2, r0, lsl r3
	str	r3, [r7, r1, lsl #2]
	bne	.L553
	ldmfd	sp, {r4, r5, r6, r7, r8, r9, fp, sp, pc}
.L557:
	.align	2
.L556:
	.word	.LANCHOR0
	.word	.LANCHOR1+184
	.word	.LC1
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
.L559:
	ldrh	r1, [r4, #6]
	uxth	r3, r2
	cmp	r1, r3
	bhi	.L563
	ldr	r3, .L578
	mov	r6, #0
	mov	r8, r6
	mov	r10, r6
	ldrh	r2, [r3, #104]
	ldrh	r3, [r4]
	lsl	r3, r3, #1
	strh	r2, [r5, r3]	@ movhi
	ldrh	r9, [r5]
.L564:
	ldrh	r3, [r4, #10]
	uxth	r1, r6
	cmp	r3, r1
	bhi	.L568
	mov	r0, r8
	ldmib	sp, {r4, r5, r6, r7, r8, r9, r10, fp, sp, pc}
.L563:
	uxth	r3, r2
	mov	r1, #0
	ldr	r0, [r6, r3, lsl #2]
	ubfx	r0, r0, #10, #16
.L560:
	ldrh	ip, [r4, #10]
	uxth	r3, r1
	cmp	ip, r3
	addls	r2, r2, #1
	bls	.L559
.L562:
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
	b	.L560
.L568:
	uxth	r3, r6
	lsl	r3, r3, #1
	ldrh	r2, [r5, r3]
	cmp	r9, r2
	bls	.L565
	ldrh	r0, [r7, r3]
	add	ip, r7, r3
	cmp	r0, #0
	bne	.L566
.L567:
	add	r6, r6, #1
	b	.L564
.L565:
	cmp	r2, #0
	bne	.L567
	ldrh	r0, [r7, r3]
	add	ip, r7, r3
	cmp	r0, #0
	beq	.L567
.L569:
	mov	r1, #1
	str	ip, [fp, #-44]
	bl	FtlFreeSysBlkQueueIn
	ldr	ip, [fp, #-44]
	strh	r10, [ip]	@ movhi
	ldrh	r3, [r4, #8]
	sub	r3, r3, #1
	strh	r3, [r4, #8]	@ movhi
	b	.L567
.L566:
	subs	r9, r2, #0
	mov	r8, r1
	beq	.L569
	b	.L567
.L579:
	.align	2
.L578:
	.word	.LANCHOR0
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
	push	{r4, r5, r6, fp, ip, lr, pc}
	sub	fp, ip, #4
	ldr	r4, .L583
	mov	r1, #0
	mvn	r5, #0
	ldr	r2, [r4, #128]
	ldr	r0, [r4, #3360]
	lsl	r2, r2, #1
	bl	ftl_memset
	ldrh	r3, [r4, #110]
	mov	r1, #255
	ldrh	r2, [r4, #138]
	ldr	r0, [r4, #3384]
	mul	r2, r2, r3
	bl	ftl_memset
	mov	r2, #0
	mov	r3, r4
	mov	lr, #12
	mov	r4, r2
.L581:
	ldrh	r0, [r3, #138]
	uxth	r1, r2
	add	ip, r2, #1
	cmp	r0, r1
	bhi	.L582
	ldr	r2, .L583+4
	mvn	r1, #0
	strh	r1, [r2, #2]	@ movhi
	strh	r1, [r2]	@ movhi
	ldr	r1, [r3, #128]
	strh	r1, [r2, #10]	@ movhi
	ldr	r1, .L583+8
	strh	r1, [r2, #4]	@ movhi
	movw	r1, #3436
	ldrh	r1, [r3, r1]
	strh	r1, [r2, #8]	@ movhi
	ldrh	r1, [r3, #136]
	strh	r1, [r2, #6]	@ movhi
	ldr	r2, [r3, #3356]
	str	r2, [r3, #3404]
	ldr	r2, [r3, #3380]
	str	r2, [r3, #3408]
	ldr	r2, [r3, #3360]
	str	r2, [r3, #3412]
	ldr	r2, [r3, #3376]
	str	r2, [r3, #3416]
	ldmfd	sp, {r4, r5, r6, fp, sp, pc}
.L582:
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
	ldr	r0, [r3, #3384]
	bic	r2, r2, #3
	add	r2, r0, r2
	str	r2, [r1, #8]
	mov	r2, ip
	b	.L581
.L584:
	.align	2
.L583:
	.word	.LANCHOR0
	.word	.LANCHOR0+3392
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
	ldr	r4, .L586
	mvn	r3, #0
	movw	r2, #3438
	mov	r5, #0
	strh	r3, [r4, r2]	@ movhi
	mov	r1, r5
	ldrh	r2, [r4, #120]
	ldr	r0, [r4, #148]
	str	r3, [r4, #3448]
	str	r5, [r4, #3440]
	lsl	r2, r2, #1
	str	r5, [r4, #3444]
	strh	r5, [r4, #144]	@ movhi
	bl	ftl_memset
	ldrh	r2, [r4, #42]
	mov	r1, r5
	ldr	r0, [r4, #2328]
	lsl	r2, r2, #1
	bl	ftl_memset
	ldrh	r2, [r4, #42]
	mov	r1, r5
	ldr	r0, [r4, #3344]
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
.L587:
	.align	2
.L586:
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
	ldr	r4, .L599
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
.L589:
	ldrh	r3, [r4, #40]
	uxth	r10, r5
	cmp	r10, r3
	bcs	.L596
	ldrh	r3, [r4, #102]
	mov	r9, r10
	ldrh	ip, [r4, #32]
	str	r3, [fp, #-44]
	mov	r3, #0
	mov	r7, r3
	b	.L597
.L591:
	ldr	r2, .L599+4
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
.L597:
	uxth	r1, r3
	cmp	ip, r1
	bhi	.L591
	cmp	r7, #0
	beq	.L592
	mov	r1, r7
	mov	r0, #32768
	bl	__divsi3
	uxth	r7, r0
.L593:
	ldr	r3, [r4, #2316]
	add	r3, r3, r9, lsl #3
	strh	r7, [r3, #4]	@ movhi
	ldr	r3, .L599+8
	ldrh	r1, [r3]
	cmp	r1, r10
	beq	.L594
	ldrh	r1, [r3, #48]
	cmp	r1, r10
	beq	.L594
	ldrh	r3, [r3, #96]
	cmp	r3, r10
	beq	.L594
	ldr	r2, [r4, #2324]
	lsl	r3, r9, #1
	ldrh	r3, [r2, r3]
	cmp	r3, #0
	bne	.L595
	add	r8, r8, #1
	mov	r0, r9
	uxth	r8, r8
	bl	INSERT_FREE_LIST
.L594:
	add	r5, r5, #1
	b	.L589
.L592:
	ldr	r1, [r4, #2324]
	lsl	r3, r9, #1
	mvn	r0, #0
	strh	r0, [r1, r3]	@ movhi
	b	.L593
.L595:
	add	r6, r6, #1
	mov	r0, r9
	uxth	r6, r6
	bl	INSERT_DATA_LIST
	b	.L594
.L596:
	ldr	r2, [fp, #-48]
	strh	r6, [r2]	@ movhi
	add	r6, r6, r8
	cmp	r6, r3
	movw	r2, #2344
	strh	r8, [r4, r2]	@ movhi
	ble	.L598
	movw	r2, #2170
	ldr	r1, .L599+12
	ldr	r0, .L599+16
	bl	sftl_printk
.L598:
	mov	r0, #0
	sub	sp, fp, #40
	ldmfd	sp, {r4, r5, r6, r7, r8, r9, r10, fp, sp, pc}
.L600:
	.align	2
.L599:
	.word	.LANCHOR0
	.word	.LANCHOR0+60
	.word	.LANCHOR0+2348
	.word	.LANCHOR1+198
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
	ldr	r4, .L602
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
.L603:
	.align	2
.L602:
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
	ldr	r5, .L633
	mov	r8, r1
	ldr	r9, .L633+4
	mov	r4, r0
	ldr	r10, .L633+8
	ldrh	r3, [r5, #12]
	str	r3, [fp, #-52]
.L606:
	cmp	r7, r8
	bne	.L617
	mov	r0, #0
	sub	sp, fp, #40
	ldmfd	sp, {r4, r5, r6, r7, r8, r9, r10, fp, sp, pc}
.L617:
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
	ldr	ip, [r5, #3256]
	tst	r6, #63
	ldr	r1, [fp, #-44]
	ldrne	r6, [r5, #3320]
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
	ldr	ip, [r5, #3256]
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
.L613:
	ldr	r3, [r5, #3320]
	cmp	r6, r3
	bne	.L610
	ldr	r0, [r4, #8]
	cmp	r6, r0
	beq	.L610
	ldrh	r2, [r5, #58]
	mov	r1, r6
	lsl	r2, r2, #9
	bl	ftl_memcpy
.L610:
	add	r7, r7, #1
	add	r4, r4, #20
	b	.L606
.L634:
	.align	2
.L633:
	.word	.LANCHOR0
	.word	.LANCHOR1+216
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
	ldr	r4, .L645
	mov	r6, #0
	mvn	r9, #0
	ldr	r10, .L645+4
	ldr	r3, [r4, #3300]
	add	r7, r4, #162
	ldr	r8, [r4, #3332]
	str	r3, [r4, #3460]
	str	r8, [r4, #3464]
.L636:
	ldrh	r3, [r4, #54]
	cmp	r6, r3
	bcc	.L641
	mov	r0, #0
	ldmfd	sp, {r4, r5, r6, r7, r8, r9, r10, fp, sp, pc}
.L641:
	ldrh	r5, [r4, #98]
	strh	r9, [r7, #2]!	@ movhi
.L638:
	ldrh	r3, [r4, #98]
	sub	r5, r5, #1
	uxth	r5, r5
	sub	r2, r3, #16
	cmp	r5, r2
	ble	.L639
	mla	r3, r6, r3, r5
	mov	r2, #1
	mov	r1, r2
	mov	r0, r10
	lsl	r3, r3, #10
	str	r3, [r4, #3456]
	bl	FlashReadPages
	ldr	r3, [r4, #3452]
	cmn	r3, #1
	beq	.L638
	ldrh	r2, [r8]
	movw	r3, #61664
	cmp	r2, r3
	bne	.L638
	strh	r5, [r7]	@ movhi
.L639:
	add	r6, r6, #1
	b	.L636
.L646:
	.align	2
.L645:
	.word	.LANCHOR0
	.word	.LANCHOR0+3452
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
	ldr	r3, .L658
	lsl	r6, r0, #10
	mov	r2, r1
	mov	r7, r1
	sub	r0, fp, #116
	ldrheq	r5, [r3, #104]
	mov	r1, #1
	ldrhne	r5, [r3, #102]
	ldr	r3, [r3, #3324]
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
	beq	.L651
.L650:
	mov	r0, r5
	sub	sp, fp, #32
	ldmfd	sp, {r4, r5, r6, r7, r8, fp, sp, pc}
.L654:
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
	bne	.L652
	ldr	r3, [fp, #-92]
	cmn	r3, #1
	bne	.L652
	ldr	r3, [fp, #-116]
	cmn	r3, #1
	subne	r4, r4, #1
	sxthne	r5, r4
	bne	.L651
.L652:
	add	r4, r4, #1
	sxth	r8, r4
.L651:
	cmp	r8, r5
	ble	.L654
	b	.L650
.L659:
	.align	2
.L658:
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
	ldr	r4, .L742
	movw	r3, #3436
	mov	r1, r5
	ldr	r2, [r4, #128]
	mov	r6, r4
	ldr	r0, [r4, #3380]
	strh	r5, [r4, r3]	@ movhi
	strh	r5, [r4, #144]	@ movhi
	lsl	r2, r2, #2
	bl	ftl_memset
	ldr	r2, [r4, #128]
	mov	r1, r5
	ldr	r0, [r4, #3356]
	lsl	r2, r2, #1
	bl	ftl_memset
	ldrh	r2, [r4, #120]
	mov	r1, r5
	ldr	r0, [r4, #3368]
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
.L661:
	ldrh	r3, [r6, #42]
	ldr	r2, [fp, #-44]
	cmp	r3, r2
	bls	.L702
	ldr	r1, [r6, #3176]
	mov	r7, #0
	ldrh	r2, [r6, #32]
	mov	r10, r7
	ldr	r8, [r6, #3288]
	ldr	r3, [r6, #3172]
	str	r1, [fp, #-48]
	ldrh	r9, [r6, #112]
	ldr	ip, .L742+4
	b	.L703
.L663:
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
	bne	.L662
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
.L662:
	add	r7, r7, #1
.L703:
	uxth	r1, r7
	cmp	r2, r1
	bhi	.L663
	cmp	r10, #0
	bne	.L664
.L701:
	ldr	r3, [fp, #-44]
	add	r3, r3, #1
	uxth	r3, r3
	str	r3, [fp, #-44]
	b	.L661
.L664:
	mov	r2, #1
	mov	r1, r10
	mov	r0, r8
	bl	FlashReadPages
	mov	r3, #0
.L741:
	str	r3, [fp, #-48]
	ldrh	r3, [fp, #-48]
	cmp	r10, r3
	bls	.L701
	ldr	r3, [fp, #-48]
	mov	r9, #20
	mul	r9, r9, r3
	ldr	r3, [r4, #3288]
	add	r2, r3, r9
	ldr	r3, [r3, r9]
	ldr	r1, [r2, #4]
	ldr	r7, [r2, #12]
	cmn	r3, #1
	lsr	ip, r1, #10
	uxth	r8, ip
	bne	.L668
	mov	r3, #16
.L670:
	ldr	r0, [r4, #3288]
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
	bne	.L667
	ldr	r3, [r4, #3288]
	mvn	r2, #0
	str	r2, [r3, r9]
	ldr	r3, [r4, #3288]
	ldr	r3, [r3, r9]
	cmp	r3, r2
	bne	.L668
.L669:
	mov	r1, #1
	b	.L740
.L667:
	ldr	r2, [r4, #3288]
	ldr	r2, [r2, r9]
	cmn	r2, #1
	bne	.L668
	sub	r3, r3, #1
	uxth	r3, r3
	cmp	r3, #0
	bne	.L670
	b	.L669
.L668:
	ldr	r2, [r6, #2540]
	ldr	r3, [r7, #4]
	cmn	r2, #1
	beq	.L671
	cmp	r2, r3
	bhi	.L672
.L671:
	cmn	r3, #1
	addne	r2, r3, #1
	strne	r2, [r5, #2540]
.L672:
	ldrh	r2, [r7]
	movw	r1, #61604
	cmp	r2, r1
	beq	.L674
	bhi	.L675
	movw	r3, #61574
	cmp	r2, r3
	beq	.L676
.L673:
	ldr	r3, [fp, #-48]
	add	r3, r3, #1
	b	.L741
.L675:
	movw	r3, #61634
	cmp	r2, r3
	beq	.L677
	movw	r3, #65535
	cmp	r2, r3
	moveq	r1, #0
	bne	.L673
.L740:
	uxth	r0, ip
	bl	FtlFreeSysBlkQueueIn
	b	.L673
.L677:
	ldr	r9, .L742+8
	ldr	r3, [r4, #128]
	ldrh	r2, [r9]
	cmp	r2, r3
	bls	.L679
	movw	r2, #1222
	ldr	r1, .L742+12
	ldr	r0, .L742+16
	bl	sftl_printk
.L679:
	ldr	r0, [r4, #128]
	ldrh	r2, [r9]
	ldr	ip, [r4, #3380]
	uxth	r1, r0
	sub	r3, r1, #1
	sub	r1, r1, r2
	sub	r1, r1, #1
	sxth	r3, r3
	sxth	r1, r1
	str	r1, [fp, #-52]
.L680:
	ldr	r1, [fp, #-52]
	cmp	r3, r1
	bgt	.L686
	cmp	r3, #0
	bge	.L718
	b	.L673
.L686:
	lsl	lr, r3, #2
	ldr	r1, [r7, #4]
	str	lr, [fp, #-56]
	ldr	lr, [ip, r3, lsl #2]
	cmp	r1, lr
	bls	.L681
	ldr	r1, [ip]
	cmp	r1, #0
	bne	.L682
	cmp	r0, r2
	addne	r2, r2, #1
	strhne	r2, [r9]	@ movhi
.L682:
	uxth	ip, r3
	mov	r1, #0
.L683:
	uxth	r0, r1
	sxth	r2, r1
	cmp	ip, r0
	bhi	.L684
	ldr	r1, [r7, #4]
	cmp	r3, #0
	ldr	r2, [r5, #3380]
	ldr	r0, [fp, #-56]
	str	r1, [r2, r0]
	lsl	r2, r3, #1
	ldr	r1, [r5, #3356]
	strh	r8, [r1, r2]	@ movhi
	blt	.L673
	ldr	r2, .L742+8
	ldr	r1, [r5, #128]
	ldrh	r2, [r2]
	sub	r1, r1, r2
	sub	r1, r1, #1
	sxth	r1, r1
	cmp	r3, r1
	bgt	.L673
.L718:
	add	r2, r2, #1
	ldr	r1, [r7, #4]
	strh	r2, [r9]	@ movhi
	ldr	r2, [r4, #3380]
	str	r1, [r2, r3, lsl #2]
	lsl	r3, r3, #1
	ldr	r2, [r4, #3356]
.L738:
	strh	r8, [r2, r3]	@ movhi
	b	.L673
.L684:
	ldr	r0, [r6, #3380]
	add	r1, r1, #1
	add	lr, r0, r2, lsl #2
	ldr	lr, [lr, #4]
	str	lr, [r0, r2, lsl #2]
	lsl	r2, r2, #1
	ldr	r0, [r6, #3356]
	add	lr, r0, r2
	ldrh	lr, [lr, #2]
	strh	lr, [r0, r2]	@ movhi
	b	.L683
.L681:
	sub	r3, r3, #1
	sxth	r3, r3
	b	.L680
.L676:
	ldrh	r2, [r4, #144]
	ldrh	r3, [r4, #120]
	cmp	r2, r3
	bls	.L689
	movw	r2, #1263
	ldr	r1, .L742+12
	ldr	r0, .L742+16
	bl	sftl_printk
.L689:
	ldrh	r2, [r4, #120]
	ldrh	r1, [r4, #144]
	ldr	ip, [r4, #3368]
	sub	r0, r2, #1
	sxth	r3, r0
	sub	r0, r0, r1
	str	r0, [fp, #-52]
.L690:
	ldr	r0, [fp, #-52]
	cmp	r3, r0
	ble	.L695
	ldr	r9, [ip, r3, lsl #2]
	lsl	lr, r3, #2
	ldr	r0, [r7, #4]
	cmp	r0, r9
	bls	.L691
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
.L693:
	uxth	r0, r1
	sxth	r2, r1
	cmp	ip, r0
	bhi	.L694
	ldr	r1, [r7, #4]
	ldr	r2, [r5, #3368]
	str	r1, [r2, lr]
	lsl	r2, r3, #1
	ldr	r1, [r5, #148]
	strh	r8, [r1, r2]	@ movhi
.L695:
	cmp	r3, #0
	blt	.L673
	ldrh	r2, [r5, #120]
	ldrh	r1, [r5, #144]
	sub	r2, r2, #1
	sub	r2, r2, r1
	sxth	r2, r2
	cmp	r3, r2
	bgt	.L673
	add	r1, r1, #1
	ldr	r2, [r5, #3368]
	strh	r1, [r5, #144]	@ movhi
	ldr	r1, [r7, #4]
	str	r1, [r2, r3, lsl #2]
	lsl	r3, r3, #1
	ldr	r2, [r5, #148]
	b	.L738
.L694:
	ldr	r0, [r6, #3368]
	add	r1, r1, #1
	add	r9, r0, r2, lsl #2
	ldr	r9, [r9, #4]
	str	r9, [r0, r2, lsl #2]
	lsl	r2, r2, #1
	ldr	r0, [r6, #148]
	add	r9, r0, r2
	ldrh	r9, [r9, #2]
	strh	r9, [r0, r2]	@ movhi
	b	.L693
.L691:
	sub	r3, r3, #1
	sxth	r3, r3
	b	.L690
.L674:
	ldr	r9, .L742+20
	movw	r2, #65535
	ldrh	r1, [r9]
	cmp	r1, r2
	strheq	r8, [r9]	@ movhi
	beq	.L739
	ldrh	r0, [r9, #4]
	cmp	r0, r2
	beq	.L698
	mov	r1, #1
	bl	FtlFreeSysBlkQueueIn
.L698:
	ldr	r3, [r7, #4]
	ldr	r2, [r4, #2584]
	cmp	r2, r3
	strhcs	r8, [r9, #4]	@ movhi
	bcs	.L673
	ldrh	r3, [r9]
	strh	r8, [r9]	@ movhi
	strh	r3, [r9, #4]	@ movhi
	ldr	r3, [r7, #4]
.L739:
	str	r3, [r4, #2584]
	b	.L673
.L702:
	ldr	r2, [r6, #3356]
	ldrh	r3, [r2]
	cmp	r3, #0
	beq	.L704
.L707:
	ldr	r1, [r4, #148]
	ldrh	r2, [r1]
	cmp	r2, #0
	beq	.L705
.L706:
	movw	r3, #3436
	ldrh	r2, [r4, r3]
	ldr	r3, [r4, #128]
	cmp	r2, r3
	bls	.L737
	movw	r2, #1388
	ldr	r1, .L742+12
	ldr	r0, .L742+16
	bl	sftl_printk
.L737:
	mov	r0, #0
	sub	sp, fp, #40
	ldmfd	sp, {r4, r5, r6, r7, r8, r9, r10, fp, sp, pc}
.L704:
	movw	r1, #3436
	ldrh	r1, [r6, r1]
	cmp	r1, #0
	ldrne	r1, [r6, #128]
	beq	.L707
.L708:
	sxth	ip, r3
	cmp	ip, r1
	bcs	.L707
	lsl	r0, ip, #1
	add	r3, r3, #1
	ldrh	r0, [r2, r0]
	cmp	r0, #0
	beq	.L708
	mov	r3, ip
	mov	lr, #0
.L709:
	ldr	r2, [r5, #128]
	cmp	r3, r2
	bcs	.L707
	ldr	r1, [r5, #3356]
	lsl	r2, r3, #1
	sub	r0, r3, ip
	lsl	r6, r0, #1
	ldrh	r7, [r1, r2]
	strh	r7, [r1, r6]	@ movhi
	ldr	r1, [r5, #3380]
	ldr	r6, [r1, r3, lsl #2]
	add	r3, r3, #1
	sxth	r3, r3
	str	r6, [r1, r0, lsl #2]
	ldr	r1, [r5, #3356]
	strh	lr, [r1, r2]	@ movhi
	b	.L709
.L705:
	ldrh	r3, [r4, #144]
	cmp	r3, #0
	ldrhne	r0, [r4, #120]
	beq	.L706
.L714:
	sxth	r3, r2
	cmp	r3, r0
	mov	ip, r3
	bge	.L706
	lsl	lr, r3, #1
	add	r2, r2, #1
	ldrh	lr, [r1, lr]
	cmp	lr, #0
	beq	.L714
	mov	lr, #0
.L715:
	ldrh	r2, [r5, #120]
	cmp	r3, r2
	bge	.L706
	ldr	r1, [r5, #148]
	lsl	r2, r3, #1
	sub	r0, r3, ip
	lsl	r6, r0, #1
	ldrh	r7, [r1, r2]
	strh	r7, [r1, r6]	@ movhi
	ldr	r1, [r5, #3368]
	ldr	r6, [r1, r3, lsl #2]
	add	r3, r3, #1
	sxth	r3, r3
	str	r6, [r1, r0, lsl #2]
	ldr	r1, [r5, #148]
	strh	lr, [r1, r2]	@ movhi
	b	.L715
.L743:
	.align	2
.L742:
	.word	.LANCHOR0
	.word	.LANCHOR0+60
	.word	.LANCHOR0+3436
	.word	.LANCHOR1+231
	.word	.LC1
	.word	.LANCHOR0+2576
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
	push	{r4, r5, r6, r7, fp, ip, lr, pc}
	sub	fp, ip, #4
	ldr	r4, .L776
	ldr	r7, .L776+4
	ldr	r3, [r4, #3300]
	ldr	r6, [r4, #3332]
	str	r3, [r4, #3460]
	str	r6, [r4, #3464]
	bl	FtlBbtMemInit
	ldrh	r5, [r4, #98]
	sub	r5, r5, #1
	uxth	r5, r5
.L745:
	ldrh	r3, [r4, #98]
	sub	r3, r3, #16
	cmp	r5, r3
	ble	.L748
	lsl	r3, r5, #10
	mov	r2, #1
	mov	r1, r2
	mov	r0, r7
	str	r3, [r4, #3456]
	bl	FlashReadPages
	ldr	r3, [r4, #3452]
	cmn	r3, #1
	bne	.L746
	ldr	r3, [r4, #3456]
	mov	r2, #1
	mov	r1, r2
	mov	r0, r7
	add	r3, r3, #1
	str	r3, [r4, #3456]
	bl	FlashReadPages
.L746:
	ldr	r3, [r4, #3452]
	cmn	r3, #1
	beq	.L747
	ldrh	r2, [r6]
	movw	r3, #61649
	cmp	r2, r3
	bne	.L747
	ldr	r3, [r6, #4]
	strh	r5, [r4, #152]	@ movhi
	str	r3, [r4, #160]
	ldrh	r3, [r6, #8]
	strh	r3, [r4, #156]	@ movhi
.L748:
	ldrh	r3, [r4, #152]
	movw	r2, #65535
	cmp	r3, r2
	beq	.L762
	ldrh	r3, [r4, #156]
	cmp	r3, r2
	beq	.L752
	lsl	r3, r3, #10
	mov	r2, #1
	mov	r1, r2
	ldr	r0, .L776+4
	str	r3, [r4, #3456]
	bl	FlashReadPages
	ldr	r3, [r4, #3452]
	cmn	r3, #1
	beq	.L752
	ldrh	r2, [r6]
	movw	r3, #61649
	cmp	r2, r3
	bne	.L752
	ldr	r3, [r6, #4]
	ldr	r2, [r4, #160]
	cmp	r3, r2
	ldrhhi	r2, [r4, #156]
	strhi	r3, [r4, #160]
	ldrhhi	r3, [r6, #8]
	strhhi	r2, [r4, #152]	@ movhi
	strhhi	r3, [r4, #156]	@ movhi
.L752:
	ldr	r7, .L776+4
	mov	r1, #1
	ldrh	r0, [r4, #152]
	bl	FtlGetLastWrittenPage
	sxth	r5, r0
	add	r0, r0, #1
	strh	r0, [r4, #154]	@ movhi
.L754:
	cmp	r5, #0
	bge	.L757
	mov	r2, #253
	ldr	r1, .L776+8
	ldr	r0, .L776+12
	bl	sftl_printk
.L756:
	ldrh	r3, [r6, #10]
	ldrh	r0, [r6, #12]
	strh	r3, [r4, #158]	@ movhi
	movw	r3, #65535
	cmp	r0, r3
	beq	.L759
	ldr	r2, [r4, #28]
	cmp	r0, r2
	beq	.L759
	ldrh	r3, [r4, #42]
	lsr	r3, r3, #2
	cmp	r0, r3
	cmpcc	r2, r3
	bcs	.L759
	bl	FtlSysBlkNumInit
.L759:
	ldr	r6, .L776+16
	mov	r5, #0
	ldr	r7, .L776+20
.L760:
	ldrh	r3, [r4, #54]
	cmp	r5, r3
	bcc	.L761
	mov	r0, #0
	ldmfd	sp, {r4, r5, r6, r7, fp, sp, pc}
.L747:
	sub	r5, r5, #1
	uxth	r5, r5
	b	.L745
.L757:
	ldrh	r3, [r4, #152]
	mov	r2, #1
	mov	r1, r2
	mov	r0, r7
	orr	r3, r5, r3, lsl #10
	str	r3, [r4, #3456]
	ldr	r3, [r4, #3300]
	str	r3, [r4, #3460]
	bl	FlashReadPages
	ldr	r3, [r4, #3452]
	cmn	r3, #1
	beq	.L755
	ldrh	r2, [r6]
	movw	r3, #61649
	cmp	r2, r3
	beq	.L756
.L755:
	sub	r5, r5, #1
	sxth	r5, r5
	b	.L754
.L761:
	ldrh	r2, [r7]
	ldr	r1, [r4, #3460]
	ldr	r0, [r6, #4]!
	lsl	r2, r2, #2
	mla	r1, r5, r2, r1
	add	r5, r5, #1
	bl	ftl_memcpy
	b	.L760
.L762:
	mvn	r0, #0
	ldmfd	sp, {r4, r5, r6, r7, fp, sp, pc}
.L777:
	.align	2
.L776:
	.word	.LANCHOR0
	.word	.LANCHOR0+3452
	.word	.LANCHOR1+245
	.word	.LC1
	.word	.LANCHOR0+176
	.word	.LANCHOR0+3388
	.size	FtlLoadBbt, .-FtlLoadBbt
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
	ldr	r4, .L799
	mov	r1, #0
	ldr	r3, [r4, #3300]
	add	r6, r4, #2576
	ldrh	r2, [r4, #40]
	ldr	r0, [r4, #2324]
	str	r3, [r4, #3460]
	ldr	r3, [r4, #3332]
	lsl	r2, r2, #1
	str	r3, [r4, #3464]
	bl	ftl_memset
	ldrh	r0, [r6]
	movw	r3, #65535
	cmp	r0, r3
	bne	.L779
.L787:
	mvn	r0, #0
	ldmfd	sp, {r4, r5, r6, r7, r8, fp, sp, pc}
.L779:
	ldr	r7, .L799+4
	mov	r1, #1
	ldr	r8, .L799+8
	bl	FtlGetLastWrittenPage
	sxth	r5, r0
	add	r0, r0, #1
	strh	r0, [r6, #2]	@ movhi
.L781:
	cmp	r5, #0
	bge	.L784
	movw	r2, #1448
	ldr	r1, .L799+12
	ldr	r0, .L799+16
	bl	sftl_printk
.L783:
	ldrh	r3, [r4, #40]
	ldrh	r2, [r4, #110]
	add	r3, r3, #24
	cmp	r2, r3, lsl #1
	bcs	.L786
	movw	r2, #1450
	ldr	r1, .L799+12
	ldr	r0, .L799+16
	bl	sftl_printk
.L786:
	ldr	r5, .L799+20
	mov	r2, #48
	ldr	r1, [r4, #3460]
	mov	r0, r5
	bl	ftl_memcpy
	ldrh	r2, [r4, #40]
	ldr	r1, [r4, #3460]
	ldr	r0, [r4, #2324]
	lsl	r2, r2, #1
	add	r1, r1, #48
	bl	ftl_memcpy
	ldrh	r1, [r4, #40]
	ldr	r3, [r4, #3460]
	ldr	r0, [r4, #24]
	lsr	r2, r1, #3
	add	r1, r1, #24
	lsl	r1, r1, #1
	add	r2, r2, #4
	bic	r1, r1, #3
	add	r1, r3, r1
	bl	ftl_memcpy
	ldr	r2, [r4, #2268]
	ldr	r3, .L799+8
	cmp	r2, r3
	bne	.L787
	ldrb	r2, [r4, #2278]	@ zero_extendqisi2
	ldrh	r3, [r4, #54]
	ldrh	r7, [r5, #8]
	cmp	r2, r3
	strh	r7, [r6, #6]	@ movhi
	bne	.L787
	ldrh	r3, [r4, #102]
	ldrh	r2, [r4, #58]
	ldr	r6, [r4, #44]
	ldrh	r0, [r4, #158]
	mul	r3, r7, r3
	str	r7, [r4, #3472]
	ldrh	r1, [r4, #32]
	sub	r0, r6, r0
	sub	r0, r0, r7
	str	r3, [r4, #2504]
	mul	r3, r3, r2
	str	r3, [r4, #140]
	bl	__udivsi3
	cmp	r7, r6
	movw	r3, #2572
	strh	r0, [r4, r3]	@ movhi
	bls	.L788
	mov	r2, #1472
	ldr	r1, .L799+12
	ldr	r0, .L799+16
	bl	sftl_printk
.L788:
	ldrh	r0, [r5, #14]
	movw	r3, #2348
	ldr	r1, .L799+24
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
	beq	.L791
	ldr	r0, .L799+24
	bl	make_superblock
.L791:
	movw	r3, #2396
	ldrh	r2, [r4, r3]
	movw	r3, #65535
	cmp	r2, r3
	beq	.L792
	ldr	r0, .L799+28
	bl	make_superblock
.L792:
	movw	r3, #2444
	ldrh	r2, [r4, r3]
	movw	r3, #65535
	cmp	r2, r3
	beq	.L793
	ldr	r0, .L799+32
	bl	make_superblock
.L793:
	movw	r3, #2588
	ldrh	r2, [r4, r3]
	movw	r3, #65535
	cmp	r2, r3
	beq	.L794
	ldr	r0, .L799+36
	bl	make_superblock
.L794:
	mov	r0, #0
	ldmfd	sp, {r4, r5, r6, r7, r8, fp, sp, pc}
.L784:
	ldrh	r3, [r6]
	mov	r2, #1
	mov	r1, r2
	mov	r0, r7
	orr	r3, r5, r3, lsl #10
	str	r3, [r4, #3456]
	ldr	r3, [r4, #3300]
	str	r3, [r4, #3460]
	bl	FlashReadPages
	ldr	r3, [r4, #3452]
	cmn	r3, #1
	beq	.L782
	ldr	r3, [r4, #3300]
	ldr	r3, [r3]
	cmp	r3, r8
	bne	.L782
	ldr	r3, [r4, #3332]
	ldrh	r2, [r3]
	movw	r3, #61604
	cmp	r2, r3
	beq	.L783
.L782:
	sub	r5, r5, #1
	sxth	r5, r5
	b	.L781
.L800:
	.align	2
.L799:
	.word	.LANCHOR0
	.word	.LANCHOR0+3452
	.word	1179929683
	.word	.LANCHOR1+256
	.word	.LC1
	.word	.LANCHOR0+2268
	.word	.LANCHOR0+2348
	.word	.LANCHOR0+2396
	.word	.LANCHOR0+2444
	.word	.LANCHOR0+2588
	.size	FtlLoadSysInfo, .-FtlLoadSysInfo
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
	ldr	r6, .L839
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
.L802:
	ldr	r3, [fp, #-76]
	cmp	r8, r3
	bne	.L816
	ldr	r3, [fp, #-88]
	cmp	r3, #0
	beq	.L810
	mov	r5, #0
	ldr	r7, .L839+4
	ldr	r6, .L839
	b	.L817
.L816:
	ldr	r3, [r5, #8]
	cmp	r3, #0
	beq	.L803
	ldr	r3, [r5, #12]
	cmp	r3, #0
	bne	.L804
.L803:
	mov	r2, #140
	ldr	r1, .L839+8
	ldr	r0, .L839+12
	bl	sftl_printk
.L804:
	sub	r2, fp, #68
	sub	r1, fp, #64
	mov	r0, r5
	bl	l2p_addr_tran
	ldr	r7, [fp, #-68]
	cmp	r7, #3
	bls	.L805
.L838:
	mvn	r3, #0
	str	r3, [r5]
	b	.L806
.L805:
	cmp	r7, #0
	bne	.L807
	ldr	r3, [fp, #-64]
	ldr	r2, [fp, #-80]
	cmp	r2, r3
	bls	.L807
	ldr	r5, .L839+8
	ldr	r6, .L839+16
	b	.L837
.L809:
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
	ldr	r0, .L839+20
	bl	rknand_print_hex
	mov	r3, #4
	ldr	r1, [r4, #-8]
	mov	r2, r3
	ldr	r0, .L839+24
	bl	rknand_print_hex
.L837:
	ldr	r3, [fp, #-76]
	add	r4, r4, #20
	cmp	r7, r3
	bne	.L809
	bl	dump_stack
.L810:
	mov	r0, #0
	sub	sp, fp, #40
	ldmfd	sp, {r4, r5, r6, r7, r8, r9, r10, fp, sp, pc}
.L807:
	ldr	r1, [r5, #8]
	tst	r1, #63
	moveq	r7, r1
	beq	.L811
	ldr	r7, [r10, #3320]
	cmp	r1, r7
	beq	.L811
	ldrh	r2, [r10, #58]
	mov	r0, r7
	lsl	r2, r2, #9
	bl	ftl_memcpy
.L811:
	ldr	r3, [r5, #12]
	mov	r2, r7
	ldr	ip, [r10, #3252]
	ldr	r1, [fp, #-64]
	ldrb	r0, [fp, #-68]	@ zero_extendqisi2
	blx	ip
	cmp	r0, #0
	mvnne	r3, #0
	streq	r0, [r5]
	strne	r3, [r5]
	ldrh	r3, [r6, #14]
	cmp	r3, #4
	bne	.L806
	ldr	r0, [fp, #-72]
	add	r2, r7, #2048
	ldr	r3, [r5, #12]
	ldr	r1, [fp, #-64]
	ldr	ip, [r6, #3252]
	add	r3, r3, #8
	add	r1, r0, r1
	ldrb	r0, [fp, #-68]	@ zero_extendqisi2
	blx	ip
	cmp	r0, #0
	bne	.L838
.L806:
	add	r8, r8, #1
	add	r5, r5, #20
	b	.L802
.L821:
	ldr	r2, [r9, #3320]
	mov	r3, #0
	mov	r1, #1
	sub	r0, fp, #60
	str	r3, [r2]
	ldr	r2, [r9, #3328]
	str	r3, [r2]
	ldr	r3, [r4, #4]
	ldr	r2, [fp, #-84]
	str	r3, [fp, #-56]
	ldr	r3, [r9, #3320]
	str	r3, [fp, #-52]
	ldr	r3, [r9, #3328]
	str	r3, [fp, #-48]
	bl	FlashReadPages
	ldr	r10, [fp, #-60]
	cmn	r10, #1
	bne	.L818
	ldr	r1, [r4, #4]
	mov	r0, r7
	bl	sftl_printk
	str	r10, [r4]
.L818:
	ldr	r3, [r4, #12]
	cmp	r3, #0
	beq	.L819
	ldr	r2, [r3]
	ldr	r3, [r6, #3328]
	ldr	r3, [r3]
	cmp	r2, r3
	beq	.L819
	ldr	r1, [r4, #4]
	ldr	r0, .L839+28
	bl	sftl_printk
	mvn	r3, #0
	str	r3, [r4]
.L819:
	ldr	r3, [r4, #8]
	cmp	r3, #0
	beq	.L820
	ldr	r2, [r3]
	ldr	r3, [r6, #3320]
	ldr	r3, [r3]
	cmp	r2, r3
	beq	.L820
	ldr	r1, [r4, #4]
	ldr	r0, .L839+32
	bl	sftl_printk
	mvn	r3, #0
	str	r3, [r4]
.L820:
	add	r5, r5, #1
	add	r4, r4, #20
.L817:
	cmp	r8, r5
	bne	.L821
	b	.L810
.L840:
	.align	2
.L839:
	.word	.LANCHOR0
	.word	.LC86
	.word	.LANCHOR1+271
	.word	.LC1
	.word	.LC80
	.word	.LC81
	.word	.LC82
	.word	.LC87
	.word	.LC88
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
	mov	r9, #0
	ldr	ip, .L883
	uxtb	r6, r1
	uxth	r7, r0
	mov	r5, r9
	mov	r4, r9
	mov	r10, ip
	str	r7, [ip, #3284]
.L842:
	ldrh	r1, [r10, #32]
	uxth	r3, r9
	cmp	r1, r3
	bhi	.L846
	cmp	r5, #0
	beq	.L841
	mov	r8, #0
	mov	r9, #20
	mov	r2, r5
	mov	r1, #0
	ldr	r0, [r10, #3268]
	bl	FlashEraseBlocks
.L849:
	uxth	r3, r8
	cmp	r5, r3
	bhi	.L851
.L852:
	cmp	r6, #0
	ldrhne	r3, [r10, #104]
	moveq	r3, #2
	streq	r3, [fp, #-52]
	streq	r6, [fp, #-48]
	strne	r3, [fp, #-52]
	movne	r3, #1
	strne	r3, [fp, #-48]
	mov	r3, #0
	str	r3, [fp, #-44]
.L861:
	mov	r8, #0
	mov	r5, r8
.L853:
	ldrh	r1, [r10, #32]
	uxth	r3, r8
	cmp	r1, r3
	bhi	.L856
	cmp	r5, #0
	beq	.L841
	mov	r9, #0
	mov	r3, #1
	ldr	r2, [fp, #-48]
	mov	r1, r5
	ldr	r0, [r10, #3268]
	bl	FlashProgPages
.L858:
	uxth	r3, r9
	cmp	r5, r3
	bhi	.L860
	ldr	r3, [fp, #-44]
	ldr	r2, [fp, #-52]
	add	r3, r3, #1
	str	r3, [fp, #-44]
	ldrh	r3, [fp, #-44]
	cmp	r2, r3
	bhi	.L861
	mov	r8, #0
	mov	r9, #20
.L862:
	uxth	r3, r8
	cmp	r5, r3
	bhi	.L864
	adds	r6, r6, #0
	movne	r6, #1
	cmp	r7, #63
	movhi	r7, r6
	orrls	r7, r6, #1
	cmp	r7, #0
	beq	.L841
	mov	r2, r5
	ldr	r1, [fp, #-48]
	ldr	r0, [r10, #3268]
	bl	FlashEraseBlocks
.L841:
	mov	r0, r4
	sub	sp, fp, #40
	ldmfd	sp, {r4, r5, r6, r7, r8, r9, r10, fp, sp, pc}
.L846:
	uxth	r3, r9
	mov	r2, #20
	ldr	r0, [r10, #3268]
	mul	r1, r2, r3
	mov	r2, #0
	add	r3, r10, r3
	str	r2, [r0, r1]
	mov	r1, r7
	ldrb	r0, [r3, #60]	@ zero_extendqisi2
	bl	V2P_block
	cmp	r6, #0
	mov	r8, r0
	beq	.L843
	bl	IsBlkInVendorPart
	cmp	r0, #0
	bne	.L844
.L843:
	mov	r0, r8
	bl	FtlBbmIsBadBlock
	cmp	r0, #0
	addne	r4, r4, #1
	uxthne	r4, r4
	bne	.L844
	ldr	r1, [r10, #3268]
	mov	r3, #20
	lsl	r8, r8, #10
	mla	r1, r3, r5, r1
	ldr	r3, [r10, #3316]
	str	r3, [r1, #8]
	ldrh	r3, [r10, #112]
	str	r8, [r1, #4]
	mul	r3, r5, r3
	add	r5, r5, #1
	uxth	r5, r5
	add	r0, r3, #3
	cmp	r3, #0
	movlt	r3, r0
	ldr	r0, [r10, #3336]
	bic	r3, r3, #3
	add	r3, r0, r3
	str	r3, [r1, #12]
.L844:
	add	r9, r9, #1
	b	.L842
.L851:
	mul	r3, r9, r8
	ldr	r2, [r10, #3268]
	add	r1, r2, r3
	ldr	r3, [r2, r3]
	cmn	r3, #1
	bne	.L850
	ldr	r0, [r1, #4]
	add	r4, r4, #1
	uxth	r4, r4
	ubfx	r0, r0, #10, #16
	bl	FtlBbmMapBadBlock
.L850:
	add	r8, r8, #1
	b	.L849
.L856:
	uxth	r3, r8
	mov	r2, #20
	ldr	r0, [r10, #3268]
	mul	r1, r2, r3
	mov	r2, #0
	add	r3, r10, r3
	str	r2, [r0, r1]
	mov	r1, r7
	ldrb	r0, [r3, #60]	@ zero_extendqisi2
	bl	V2P_block
	cmp	r6, #0
	mov	r9, r0
	beq	.L854
	bl	IsBlkInVendorPart
	cmp	r0, #0
	bne	.L855
.L854:
	mov	r0, r9
	bl	FtlBbmIsBadBlock
	cmp	r0, #0
	bne	.L855
	ldr	r1, [r10, #3268]
	mov	r3, #20
	mla	r1, r3, r5, r1
	ldr	r3, [fp, #-44]
	add	r9, r3, r9, lsl #10
	ldr	r3, [r10, #3312]
	str	r9, [r1, #4]
	str	r3, [r1, #8]
	ldrh	r3, [r10, #112]
	mul	r3, r5, r3
	add	r5, r5, #1
	uxth	r5, r5
	add	r0, r3, #3
	cmp	r3, #0
	movlt	r3, r0
	ldr	r0, [r10, #3316]
	bic	r3, r3, #3
	add	r3, r0, r3
	str	r3, [r1, #12]
.L855:
	add	r8, r8, #1
	b	.L853
.L860:
	mov	r3, #20
	ldr	r2, [r10, #3268]
	mul	r3, r3, r9
	add	r1, r2, r3
	ldr	r3, [r2, r3]
	cmp	r3, #0
	beq	.L859
	ldr	r0, [r1, #4]
	add	r4, r4, #1
	uxth	r4, r4
	ubfx	r0, r0, #10, #16
	bl	FtlBbmMapBadBlock
.L859:
	add	r9, r9, #1
	b	.L858
.L864:
	cmp	r6, #0
	beq	.L863
	mul	r3, r9, r8
	ldr	r2, [r10, #3268]
	add	r1, r2, r3
	ldr	r3, [r2, r3]
	cmp	r3, #0
	bne	.L863
	ldr	r0, [r1, #4]
	mov	r1, #1
	ubfx	r0, r0, #10, #16
	bl	FtlFreeSysBlkQueueIn
.L863:
	add	r8, r8, #1
	b	.L862
.L884:
	.align	2
.L883:
	.word	.LANCHOR0
	.size	FtlLowFormatEraseBlock, .-FtlLowFormatEraseBlock
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
	bne	.L886
	ldrh	r3, [r0, #8]
	cmp	r3, #0
	beq	.L887
	movw	r2, #641
	ldr	r1, .L895
	ldr	r0, .L895+4
	bl	sftl_printk
.L887:
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
.L888:
	mov	r0, #0
	ldmfd	sp, {r4, r5, r6, r7, fp, sp, pc}
.L886:
	lsl	r3, r3, #1
	ldr	r2, [r0, #28]
	ldr	r6, [r0, #24]
	mov	r1, #255
	ldrh	r7, [r5, r3]
	ldrh	r3, [r0, #2]
	ldr	r5, .L895+8
	orr	r3, r3, r7, lsl #10
	str	r3, [r5, #3456]
	ldr	r3, [r5, #3300]
	str	r3, [r5, #3460]
	ldr	r3, [r5, #3332]
	str	r3, [r5, #3464]
	str	r2, [r3, #4]
	ldr	r2, .L895+12
	strh	r2, [r3, #8]	@ movhi
	ldrh	r2, [r0, #4]
	strh	r7, [r3, #2]	@ movhi
	strh	r2, [r3]	@ movhi
	ldrh	r2, [r5, #104]
	ldr	r0, [r5, #3300]
	lsl	r2, r2, #3
	bl	ftl_memset
	mov	r2, #0
	mov	r3, r2
.L889:
	ldrh	r0, [r4, #6]
	uxth	r1, r2
	cmp	r0, r1
	bhi	.L891
	mov	r2, #1
	mov	r3, #0
	mov	r1, r2
	ldr	r0, .L895+16
	bl	FlashProgPages
	ldrh	r3, [r4, #2]
	mov	r0, r4
	add	r3, r3, #1
	strh	r3, [r4, #2]	@ movhi
	bl	ftl_map_blk_gc
	b	.L888
.L891:
	uxth	r1, r2
	ldr	r0, [r6, r1, lsl #2]
	cmp	r7, r0, lsr #10
	bne	.L890
	ldr	r0, [r5, #3300]
	add	r3, r3, #1
	uxth	r3, r3
	str	r1, [r0, r3, lsl #3]
	ldr	r0, [r6, r1, lsl #2]
	ldr	r1, [r5, #3300]
	add	r1, r1, r3, lsl #3
	str	r0, [r1, #4]
.L890:
	add	r2, r2, #1
	b	.L889
.L896:
	.align	2
.L895:
	.word	.LANCHOR1+286
	.word	.LC1
	.word	.LANCHOR0
	.word	-1291
	.word	.LANCHOR0+3452
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
	ldr	r7, .L915
	mov	r4, r0
	mov	r8, r1
	mov	r9, r2
	mov	r6, #0
	mov	r5, r7
.L898:
	ldr	r3, [r7, #2524]
	add	r3, r3, #1
	str	r3, [r7, #2524]
	ldrh	r3, [r7, #104]
	ldrh	r2, [r4, #2]
	sub	r3, r3, #1
	cmp	r2, r3
	bge	.L899
	ldrh	r2, [r4]
	movw	r3, #65535
	cmp	r2, r3
	bne	.L900
.L899:
	mov	r0, r4
	bl	Ftl_write_map_blk_to_last_page
.L900:
	ldrh	r3, [r4]
	ldr	r2, [r4, #12]
	lsl	r3, r3, #1
	ldrh	r3, [r2, r3]
	cmp	r3, #0
	bne	.L901
	movw	r2, #699
	ldr	r1, .L915+4
	ldr	r0, .L915+8
	bl	sftl_printk
.L901:
	ldrh	r2, [r4]
	ldrh	r3, [r4, #10]
	cmp	r2, r3
	bcc	.L902
	mov	r2, #700
	ldr	r1, .L915+4
	ldr	r0, .L915+8
	bl	sftl_printk
.L902:
	ldrh	r3, [r4]
	mov	r1, #16
	ldr	r2, [r4, #12]
	ldr	r0, [r5, #3332]
	lsl	r3, r3, #1
	ldrh	r10, [r2, r3]
	ldrh	r3, [r4, #2]
	str	r0, [r5, #3464]
	str	r9, [r5, #3460]
	orr	r3, r3, r10, lsl #10
	str	r3, [r5, #3456]
	bl	__memzero
	ldr	r3, [r5, #3464]
	ldr	r2, [r4, #28]
	ldr	r0, .L915+12
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
	ldr	r2, [r5, #3452]
	cmn	r2, #1
	bne	.L903
	ldr	r1, [r5, #3456]
	add	r6, r6, #1
	ldr	r0, .L915+16
	uxth	r6, r6
	bl	sftl_printk
	ldrh	r3, [r4, #2]
	cmp	r3, #2
	ldrhls	r3, [r5, #104]
	subls	r3, r3, #1
	strhls	r3, [r4, #2]	@ movhi
	cmp	r6, #3
	bls	.L905
	mov	r2, r6
	ldr	r1, [r5, #3456]
	ldr	r0, .L915+20
	bl	sftl_printk
.L906:
	b	.L906
.L905:
	ldr	r3, [r4, #32]
	cmp	r3, #0
	beq	.L898
.L914:
	b	.L914
.L903:
	cmp	r3, #1
	cmpne	r2, #256
	beq	.L909
	ldr	r0, [r4, #36]
	cmp	r0, #0
	beq	.L910
.L909:
	mov	r3, #0
	str	r3, [r4, #36]
	b	.L898
.L910:
	ldr	r2, [r5, #3456]
	ldr	r3, [r4, #24]
	str	r2, [r3, r8, lsl #2]
	ldmfd	sp, {r4, r5, r6, r7, r8, r9, r10, fp, sp, pc}
.L916:
	.align	2
.L915:
	.word	.LANCHOR0
	.word	.LANCHOR1+317
	.word	.LC1
	.word	.LANCHOR0+3452
	.word	.LC89
	.word	.LC90
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
	ldr	r4, .L924
	uxth	r6, r0
	uxth	r8, r1
	ldrh	r3, [r4, #136]
	cmp	r3, r6
	bcs	.L918
	movw	r2, #485
	ldr	r1, .L924+4
	ldr	r0, .L924+8
	bl	sftl_printk
.L918:
	ldr	r3, [r4, #3376]
	mov	r5, #12
	ldr	r7, [r3, r6, lsl #2]
	cmp	r7, #0
	bne	.L919
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
.L920:
	mov	r0, #0
	ldmfd	sp, {r4, r5, r6, r7, r8, fp, sp, pc}
.L919:
	mul	r5, r5, r8
	ldr	r3, [r4, #2492]
	mov	r2, #1
	ldr	r0, .L924+12
	mov	r1, r2
	str	r7, [r4, #3456]
	add	r3, r3, r5
	ldr	r3, [r3, #8]
	str	r3, [r4, #3460]
	ldr	r3, [r4, #3332]
	str	r3, [r4, #3464]
	bl	FlashReadPages
	ldr	r8, [r4, #3464]
	ldrh	r3, [r8, #8]
	cmp	r3, r6
	beq	.L921
	mov	r2, r7
	mov	r1, r6
	ldr	r0, .L924+16
	bl	sftl_printk
	mov	r3, #4
	ldr	r1, [r4, #3464]
	mov	r2, r3
	ldr	r0, .L924+20
	bl	rknand_print_hex
	ldrh	r3, [r4, #136]
	mov	r2, #4
	ldr	r1, [r4, #3376]
	ldr	r0, .L924+24
	bl	rknand_print_hex
.L922:
	ldrh	r3, [r8, #8]
	cmp	r3, r6
	beq	.L923
	mov	r2, #508
	ldr	r1, .L924+4
	ldr	r0, .L924+8
	bl	sftl_printk
.L923:
	ldr	r3, [r4, #2492]
	mov	r1, #0
	add	r2, r3, r5
	str	r1, [r2, #4]
	strh	r6, [r3, r5]	@ movhi
	b	.L920
.L921:
	ldr	r3, [r4, #3452]
	cmp	r3, #256
	bne	.L922
	mov	r2, r7
	mov	r1, r6
	ldr	r0, .L924+28
	bl	sftl_printk
	ldr	r3, [r4, #2492]
	mov	r1, r6
	ldr	r0, .L924+32
	add	r3, r3, r5
	ldr	r2, [r3, #8]
	bl	FtlMapWritePage
	b	.L922
.L925:
	.align	2
.L924:
	.word	.LANCHOR0
	.word	.LANCHOR1+333
	.word	.LC1
	.word	.LANCHOR0+3452
	.word	.LC91
	.word	.LC92
	.word	.LC93
	.word	.LC94
	.word	.LANCHOR0+3392
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
	ldrh	r3, [r4, #10]
	ldrh	r2, [r4, #8]
	ldr	r5, .L942
	sub	r3, r3, #5
	cmp	r2, r3
	blt	.L927
	uxth	r0, r0
	lsl	r0, r0, #1
	ldrh	r8, [r6, r0]
	cmp	r8, #0
	beq	.L927
	ldr	r3, [r4, #32]
	cmp	r3, #0
	bne	.L927
	mov	r2, #1
	str	r2, [r4, #32]
	strh	r3, [r6, r0]	@ movhi
	ldrh	r3, [r4, #8]
	ldrh	r2, [r4, #2]
	sub	r3, r3, #1
	strh	r3, [r4, #8]	@ movhi
	ldrh	r3, [r5, #104]
	cmp	r2, r3
	bcc	.L928
	mov	r0, r4
	bl	ftl_map_blk_alloc_new_blk
.L928:
	mov	r6, #0
.L929:
	ldrh	r3, [r4, #6]
	uxth	r10, r6
	cmp	r3, r10
	bhi	.L936
	mov	r1, #1
	mov	r0, r8
	bl	FtlFreeSysBlkQueueIn
	mov	r3, #0
	str	r3, [r4, #32]
.L927:
	ldrh	r2, [r4, #2]
	ldrh	r3, [r5, #104]
	cmp	r2, r3
	bcc	.L937
	mov	r0, r4
	bl	ftl_map_blk_alloc_new_blk
.L937:
	mov	r0, #0
	sub	sp, fp, #40
	ldmfd	sp, {r4, r5, r6, r7, r8, r9, r10, fp, sp, pc}
.L936:
	uxth	r7, r6
	add	r3, r9, r7, lsl #2
	str	r3, [fp, #-44]
	ldr	r3, [r9, r7, lsl #2]
	cmp	r8, r3, lsr #10
	bne	.L930
	ldr	r3, [r5, #3304]
	ldr	r0, .L942+4
	str	r3, [r5, #3460]
	ldr	r3, [r5, #3332]
	str	r3, [r5, #3464]
	ldr	r2, [r9, r7, lsl #2]
	str	r3, [fp, #-48]
	str	r2, [r5, #3456]
	mov	r2, #1
	mov	r1, r2
	bl	FlashReadPages
	ldr	r3, [fp, #-48]
	ldrh	r2, [r3, #8]
	cmp	r2, r10
	beq	.L931
	movw	r2, #611
	ldr	r1, .L942+8
	ldr	r0, .L942+12
	bl	sftl_printk
	ldr	r3, [fp, #-48]
.L931:
	ldr	r2, [r5, #3452]
	cmn	r2, #1
	bne	.L932
.L934:
	ldr	r2, [fp, #-44]
	mov	r3, #0
	str	r3, [r2]
.L933:
	b	.L933
.L932:
	ldrh	r2, [r3, #8]
	cmp	r2, r10
	bne	.L934
	ldrh	r2, [r3]
	ldrh	r3, [r4, #4]
	cmp	r2, r3
	bne	.L934
	ldr	r2, [r5, #3460]
	mov	r1, r7
	mov	r0, r4
	bl	FtlMapWritePage
.L930:
	add	r6, r6, #1
	b	.L929
.L943:
	.align	2
.L942:
	.word	.LANCHOR0
	.word	.LANCHOR0+3452
	.word	.LANCHOR1+349
	.word	.LC1
	.size	ftl_map_blk_gc, .-ftl_map_blk_gc
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
	ldr	r3, [r6, #3300]
	mvn	r2, #0
	ldr	r9, [r6, #3332]
	str	r7, [r4, #32]
	str	r3, [r6, #3460]
	str	r9, [r6, #3464]
	strh	r2, [r4]	@ movhi
	strh	r2, [r4, #2]	@ movhi
	mov	r2, #1
	str	r7, [r4, #28]
	str	r2, [r4, #36]
.L945:
	ldr	r3, [fp, #-52]
	sxth	r2, r7
	cmp	r2, r3
	bge	.L962
	ldr	r3, [fp, #-52]
	lsl	r0, r2, #1
	sub	r1, r3, #1
	cmp	r2, r1
	bne	.L946
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
.L947:
	sxth	r2, r8
	cmp	r2, r7
	blt	.L949
.L962:
	mov	r0, r4
	bl	ftl_free_no_use_map_blk
	ldrh	r2, [r4, #2]
	ldrh	r3, [r5, #104]
	cmp	r2, r3
	bne	.L951
	mov	r0, r4
	bl	ftl_map_blk_alloc_new_blk
.L951:
	mov	r0, r4
	bl	ftl_map_blk_gc
	mov	r0, r4
	bl	ftl_map_blk_gc
	mov	r0, #0
	sub	sp, fp, #40
	ldmfd	sp, {r4, r5, r6, r7, r8, r9, r10, fp, sp, pc}
.L949:
	ldrh	r1, [r10]
	mov	r0, ip
	str	ip, [fp, #-52]
	orr	r2, r2, r1, lsl #10
	str	r2, [r6, #3456]
	mov	r2, #1
	mov	r1, r2
	bl	FlashReadPages
	ldr	r2, [r6, #3452]
	ldr	ip, [fp, #-52]
	cmn	r2, #1
	beq	.L948
	ldrh	r2, [r9, #8]
	ldr	r3, [fp, #-48]
	cmp	r3, r2
	bls	.L948
	ldrh	r1, [r4, #4]
	ldrh	r0, [r9]
	cmp	r0, r1
	ldreq	r1, [r6, #3456]
	ldreq	r3, [fp, #-44]
	streq	r1, [r3, r2, lsl #2]
.L948:
	add	r8, r8, #1
	b	.L947
.L946:
	ldr	r2, [r5, #3300]
	add	r3, r8, r0
	str	r3, [fp, #-60]
	str	r2, [r5, #3460]
	ldrh	r2, [r5, #104]
	ldrh	r1, [r8, r0]
	ldr	r0, .L971+4
	sub	r2, r2, #1
	orr	r2, r2, r1, lsl #10
	str	r2, [r5, #3456]
	mov	r2, #1
	mov	r1, r2
	bl	FlashReadPages
	ldr	r2, [r5, #3452]
	cmn	r2, #1
	beq	.L964
	ldrh	r1, [r9]
	ldrh	r2, [r4, #4]
	cmp	r1, r2
	bne	.L964
	ldrh	r1, [r9, #8]
	movw	r2, #64245
	cmp	r1, r2
	beq	.L953
.L964:
	mov	r10, #0
.L954:
	ldrh	r1, [r5, #104]
	sxth	r2, r10
	cmp	r2, r1
	bge	.L960
	ldr	r3, [fp, #-60]
	ldr	r0, .L971+4
	ldrh	r1, [r3]
	orr	r2, r2, r1, lsl #10
	str	r2, [r5, #3456]
	mov	r2, #1
	mov	r1, r2
	bl	FlashReadPages
	ldr	r2, [r5, #3452]
	cmn	r2, #1
	beq	.L958
	ldrh	r2, [r9, #8]
	ldr	r3, [fp, #-48]
	cmp	r3, r2
	bls	.L958
	ldrh	r1, [r4, #4]
	ldrh	r0, [r9]
	cmp	r0, r1
	ldreq	r1, [r5, #3456]
	ldreq	r3, [fp, #-44]
	streq	r1, [r3, r2, lsl #2]
.L958:
	add	r10, r10, #1
	b	.L954
.L953:
	mov	r0, #0
	mov	lr, #4
.L955:
	ldrh	r1, [r5, #104]
	sxth	r2, r0
	sub	r1, r1, #1
	cmp	r2, r1
	blt	.L957
.L960:
	add	r7, r7, #1
	b	.L945
.L957:
	ldr	ip, [r5, #3300]
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
	b	.L955
.L972:
	.align	2
.L971:
	.word	.LANCHOR0
	.word	.LANCHOR0+3452
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
	add	r0, r3, #3472
	add	r0, r0, #4
	strh	r2, [r0, #10]	@ movhi
	ldr	r2, .L974+4
	strh	r2, [r0, #4]	@ movhi
	ldrh	r2, [r3, #144]
	strh	r2, [r0, #8]	@ movhi
	ldrh	r2, [r3, #122]
	strh	r2, [r0, #6]	@ movhi
	ldr	r2, [r3, #148]
	str	r2, [r3, #3488]
	ldr	r2, [r3, #3368]
	str	r2, [r3, #3492]
	ldr	r2, [r3, #3364]
	str	r2, [r3, #3496]
	ldr	r2, [r3, #3372]
	str	r2, [r3, #3500]
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
	ldr	r0, .L977
	bl	FtlMapTblRecovery
	mov	r0, #0
	ldmfd	sp, {fp, sp, pc}
.L978:
	.align	2
.L977:
	.word	.LANCHOR0+3392
	.size	FtlLoadMapInfo, .-FtlLoadMapInfo
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
	ldr	r5, .L980
	uxth	r0, r0
	mov	r4, #12
	mul	r4, r4, r0
	ldr	r3, [r5, #2492]
	add	r0, r5, #3392
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
.L981:
	.align	2
.L980:
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
	ldr	r4, .L995
	mov	r7, r1
	mov	r10, r2
	ldr	r3, [r4, #2504]
	ldrh	r5, [r4, #108]
	cmp	r0, r3
	bcc	.L983
	movw	r2, #811
	ldr	r1, .L995+4
	ldr	r0, .L995+8
	bl	sftl_printk
.L983:
	ldr	r3, [r4, #2504]
	cmp	r6, r3
	bcs	.L984
	add	r5, r5, #7
	ldrh	r0, [r4, #138]
	lsr	r3, r6, r5
	ldr	ip, [r4, #2492]
	mov	r1, #0
	str	r3, [fp, #-44]
	mov	r2, #12
	ldrh	r9, [fp, #-44]
.L985:
	uxth	r8, r1
	cmp	r8, r0
	bcc	.L990
	str	r2, [fp, #-48]
	bl	select_l2p_ram_region
	ldr	r2, [fp, #-48]
	mov	r8, r0
	ldr	r1, [r4, #2492]
	mul	r2, r2, r0
	add	ip, r1, r2
	ldrh	r1, [r1, r2]
	movw	r2, #65535
	cmp	r1, r2
	beq	.L991
	ldr	r2, [ip, #4]
	cmp	r2, #0
	bge	.L991
	bl	flush_l2p_region
.L991:
	mov	r1, r8
	ldrh	r0, [fp, #-44]
	bl	load_l2p_region
	b	.L987
.L984:
	cmp	r10, #0
	mvn	r0, #0
	streq	r0, [r7]
.L982:
	sub	sp, fp, #40
	ldmfd	sp, {r4, r5, r6, r7, r8, r9, r10, fp, sp, pc}
.L990:
	add	r1, r1, #1
	mla	lr, r2, r1, ip
	ldrh	lr, [lr, #-12]
	cmp	lr, r9
	bne	.L985
.L987:
	mvn	r2, #0
	cmp	r10, #0
	bic	r5, r6, r2, lsl r5
	mov	r2, #12
	uxth	r5, r5
	bne	.L988
	ldr	r1, [r4, #2492]
	mla	r2, r2, r8, r1
	ldr	r2, [r2, #8]
	ldr	r2, [r2, r5, lsl #2]
	str	r2, [r7]
.L989:
	ldr	r1, [r4, #2492]
	mov	r2, #12
	mov	r0, #0
	mla	r3, r2, r8, r1
	ldr	r2, [r3, #4]
	cmn	r2, #1
	addne	r2, r2, #1
	strne	r2, [r3, #4]
	b	.L982
.L988:
	mul	r2, r2, r8
	ldr	r1, [r4, #2492]
	ldr	r0, [r7]
	add	r1, r1, r2
	ldr	r1, [r1, #8]
	str	r0, [r1, r5, lsl #2]
	ldr	r1, [r4, #2492]
	add	r2, r1, r2
	ldr	r1, [r2, #4]
	orr	r1, r1, #-2147483648
	str	r1, [r2, #4]
	ldr	r2, .L995+12
	strh	r9, [r2]	@ movhi
	b	.L989
.L996:
	.align	2
.L995:
	.word	.LANCHOR0
	.word	.LANCHOR1+364
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
	ldr	r7, .L1007
	ubfx	r0, r1, #10, #16
	str	r1, [fp, #-32]
	bl	P2V_block_in_plane
	ldr	r2, [r7, #2324]
	lsl	r5, r0, #1
	ldrh	r3, [r2, r5]
	cmp	r3, #0
	bne	.L998
	ldr	r4, [r7, #2340]
	cmp	r4, #0
	beq	.L999
	movw	r2, #2344
	movw	lr, #65535
	ldrh	ip, [r7, r2]
	ldr	r2, [r7, #2316]
	sub	r4, r4, r2
	ubfx	r4, r4, #3, #16
.L1000:
	uxth	r1, r3
	cmp	ip, r1
	bls	.L999
	cmp	r4, r0
	bne	.L1001
	mov	r1, r4
	ldr	r0, .L1007+4
	bl	List_remove_node
	movw	r3, #2344
	ldrh	r3, [r7, r3]
	cmp	r3, #0
	bne	.L1002
	movw	r2, #1699
	ldr	r1, .L1007+8
	ldr	r0, .L1007+12
	bl	sftl_printk
.L1002:
	movw	r2, #2344
	mov	r0, r4
	ldrh	r3, [r7, r2]
	sub	r3, r3, #1
	strh	r3, [r7, r2]	@ movhi
	bl	INSERT_DATA_LIST
	ldr	r2, [r7, #2324]
	ldrh	r3, [r2, r5]
.L998:
	add	r3, r3, #1
	strh	r3, [r2, r5]	@ movhi
	b	.L999
.L1001:
	lsl	r4, r4, #3
	add	r3, r3, #1
	ldrh	r4, [r2, r4]
	cmp	r4, lr
	bne	.L1000
.L999:
	mov	r2, #1
	sub	r1, fp, #32
	mov	r0, r6
	bl	log2phys
	ldmib	sp, {r4, r5, r6, r7, fp, sp, pc}
.L1008:
	.align	2
.L1007:
	.word	.LANCHOR0
	.word	.LANCHOR0+2340
	.word	.LANCHOR1+373
	.word	.LC1
	.size	FtlReUsePrevPpa, .-FtlReUsePrevPpa
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
	ldr	r6, .L1033
	ldr	r1, .L1033+4
	ldr	r0, .L1033+8
	mov	r5, r6
	bl	sftl_printk
	mov	r1, #8192
	ldr	r0, .L1033+12
	bl	__memzero
.L1010:
	ldr	r3, [r6, #2504]
	cmp	r4, r3
	bcc	.L1012
	ldr	r8, .L1033+12
	mov	r4, #0
	ldr	r9, .L1033+16
	mov	r6, r4
.L1013:
	ldrh	r2, [r5, #40]
	uxth	r3, r4
	cmp	r2, r3
	bhi	.L1015
	ldr	r4, [r5, #2340]
	cmp	r4, #0
	beq	.L1016
	movw	r3, #2344
	ldr	r9, .L1033+12
	ldrh	r8, [r5, r3]
	mov	r7, #0
	ldr	r3, [r5, #2316]
	ldr	r10, .L1033+20
	sub	r4, r4, r3
	ubfx	r4, r4, #3, #16
.L1017:
	uxth	r3, r7
	cmp	r8, r3
	bls	.L1016
	ldr	r2, [r5, #2324]
	lsl	r3, r4, #1
	ldrh	r2, [r2, r3]
	cmp	r2, #0
	beq	.L1018
	mov	r6, #1
	ldrh	r3, [r9, r3]
	mov	r1, r4
	mov	r0, r10
	bl	sftl_printk
.L1018:
	ldr	r3, [r5, #2316]
	lsl	r4, r4, #3
	add	r7, r7, #1
	ldrh	r4, [r3, r4]
	movw	r3, #65535
	cmp	r4, r3
	bne	.L1017
.L1016:
	cmp	r6, #0
	ldmibeq	sp, {r4, r5, r6, r7, r8, r9, r10, fp, sp, pc}
	movw	r2, #2343
	ldr	r1, .L1033+4
	ldr	r0, .L1033+24
	bl	sftl_printk
	ldmib	sp, {r4, r5, r6, r7, r8, r9, r10, fp, sp, pc}
.L1012:
	mov	r2, #0
	sub	r1, fp, #44
	mov	r0, r4
	bl	log2phys
	ldr	r0, [fp, #-44]
	cmn	r0, #1
	beq	.L1011
	ubfx	r0, r0, #10, #16
	bl	P2V_block_in_plane
	ldr	r2, .L1033+12
	lsl	r0, r0, #1
	ldrh	r3, [r2, r0]
	add	r3, r3, #1
	strh	r3, [r2, r0]	@ movhi
.L1011:
	add	r4, r4, #1
	b	.L1010
.L1015:
	uxth	r1, r4
	ldr	r3, [r5, #2324]
	lsl	r7, r1, #1
	ldrh	r2, [r3, r7]
	ldrh	r3, [r8, r7]
	cmp	r2, r3
	beq	.L1014
	mov	r0, r9
	bl	sftl_printk
	ldr	r3, [r5, #2324]
	movw	r2, #65535
	ldrh	r3, [r3, r7]
	cmp	r3, r2
	beq	.L1014
	ldrh	r2, [r8, r7]
	cmp	r2, r3
	movhi	r6, #1
.L1014:
	add	r4, r4, #1
	b	.L1013
.L1034:
	.align	2
.L1033:
	.word	.LANCHOR0
	.word	.LANCHOR1+389
	.word	.LC95
	.word	check_vpc_table
	.word	.LC96
	.word	.LC97
	.word	.LC1
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
	ldr	r6, .L1043
	mov	r1, #0
	ldr	r0, .L1043+4
	bl	sftl_printk
	mov	r4, r6
.L1036:
	ldr	r3, [r6, #2504]
	cmp	r5, r3
	bcc	.L1042
	sub	sp, fp, #28
	ldmfd	sp, {r4, r5, r6, r7, fp, sp, pc}
.L1042:
	mov	r2, #0
	sub	r1, fp, #32
	mov	r0, r5
	bl	log2phys
	ubfx	r3, r5, #0, #11
	cmp	r3, #0
	bne	.L1037
	ldr	r2, [fp, #-32]
	mov	r1, r5
	ldr	r0, .L1043+8
	bl	sftl_printk
.L1037:
	ldr	r3, [fp, #-32]
	cmn	r3, #1
	beq	.L1039
	str	r3, [r4, #3456]
	mov	r2, #0
	ldr	r3, [r4, #3300]
	mov	r1, #1
	ldr	r7, [r4, #3332]
	ldr	r0, .L1043+12
	str	r3, [r4, #3460]
	str	r5, [r4, #3468]
	str	r7, [r4, #3464]
	str	r2, [r4, #3452]
	bl	FlashReadPages
	ldr	r3, [r4, #3452]
	cmn	r3, #1
	cmpne	r3, #256
	beq	.L1040
	ldr	r3, [r7, #8]
	cmp	r5, r3
	beq	.L1039
.L1040:
	ldr	r2, [r4, #3460]
	ldr	r3, [r4, #3464]
	ldr	r0, .L1043+16
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
	ldr	r2, [r4, #3456]
	bl	sftl_printk
.L1039:
	add	r5, r5, #1
	b	.L1036
.L1044:
	.align	2
.L1043:
	.word	.LANCHOR0
	.word	.LC98
	.word	.LC99
	.word	.LANCHOR0+3452
	.word	.LC100
	.size	ftl_scan_all_data, .-ftl_scan_all_data
	.align	2
	.global	FtlGcScanTempBlk
	.syntax unified
	.arm
	.fpu softvfp
	.type	FtlGcScanTempBlk, %function
FtlGcScanTempBlk:
	@ args = 0, pretend = 0, frame = 44
	@ frame_needed = 1, uses_anonymous_args = 0
	mov	ip, sp
	push	{r4, r5, r6, r7, r8, r9, r10, fp, ip, lr, pc}
	sub	fp, ip, #4
	sub	sp, sp, #44
	mov	r5, r0
	ldr	r3, .L1075
	str	r1, [fp, #-68]
	ldrh	r6, [r3, #4]
	movw	r3, #65535
	cmp	r6, r3
	beq	.L1070
	cmp	r6, #0
	bne	.L1046
.L1047:
	bl	FtlGcPageVarInit
	b	.L1048
.L1070:
	mov	r6, #0
.L1046:
	ldr	r3, .L1075+4
	ldr	r2, [fp, #-68]
	ldrh	r3, [r3, #102]
	cmp	r3, r2
	beq	.L1047
.L1048:
	ldr	r4, .L1075+4
	mov	r9, #0
.L1049:
	ldrh	r2, [r5]
	mov	r3, #0
	strb	r3, [r5, #8]
	movw	r3, #65535
	cmp	r2, r3
	beq	.L1050
.L1067:
	ldrh	r3, [r4, #32]
	mov	r2, #0
	ldr	r0, [r4, #3288]
	add	ip, r5, #16
	ldrh	lr, [r4, #112]
	mov	r10, r2
	str	r3, [fp, #-72]
	movw	r7, #65535
	ldr	r3, [r4, #3172]
	str	r3, [fp, #-76]
	ldrh	r3, [r4, #110]
	str	r3, [fp, #-80]
	ldr	r3, [r4, #3176]
	str	r3, [fp, #-84]
.L1051:
	ldr	r1, [fp, #-72]
	uxth	r3, r2
	cmp	r1, r3
	bhi	.L1053
	mov	r8, #0
	mov	r2, #0
	mov	r1, r10
	bl	FlashReadPages
.L1054:
	uxth	r3, r8
	cmp	r10, r3
	bhi	.L1065
	ldr	r3, [fp, #-68]
	add	r9, r9, #1
	add	r6, r6, #1
	uxth	r6, r6
	cmp	r3, r9
	bls	.L1066
.L1068:
	ldrh	r3, [r4, #102]
	cmp	r3, r6
	bhi	.L1067
.L1050:
	ldr	r3, .L1075
	mvn	r2, #0
	strh	r6, [r5, #2]	@ movhi
	mov	r1, r6
	mov	r0, r5
	strh	r2, [r3, #4]	@ movhi
	mov	r2, #0
	strb	r2, [r5, #6]
	bl	ftl_sb_update_avl_pages
	b	.L1069
.L1053:
	ldrh	r3, [ip], #2
	cmp	r3, r7
	beq	.L1052
	mov	r1, #20
	orr	r3, r6, r3, lsl #10
	mla	r1, r1, r10, r0
	str	r3, [r1, #4]
	ldr	r3, [fp, #-80]
	mul	r3, r3, r10
	add	r8, r3, #3
	cmp	r3, #0
	movlt	r3, r8
	ldr	r8, [fp, #-76]
	bic	r3, r3, #3
	add	r3, r8, r3
	str	r3, [r1, #8]
	mul	r3, lr, r10
	add	r10, r10, #1
	uxth	r10, r10
	add	r8, r3, #3
	cmp	r3, #0
	movlt	r3, r8
	ldr	r8, [fp, #-84]
	bic	r3, r3, #3
	add	r3, r8, r3
	str	r3, [r1, #12]
.L1052:
	add	r2, r2, #1
	b	.L1051
.L1065:
	mov	r3, #20
	ldr	r2, [r4, #3288]
	mul	ip, r3, r8
	ldr	r7, [r2, ip]
	add	r3, r2, ip
	ldr	r1, [r3, #4]
	cmp	r7, #0
	str	r1, [fp, #-72]
	bne	.L1055
	ldr	r3, [r3, #12]
	movw	r2, #65535
	ldrh	r1, [r3]
	cmp	r1, r2
	bne	.L1056
.L1055:
	ldrh	r3, [r5]
	mov	r1, #0
	ldr	r2, [r4, #2324]
	mov	r6, #0
	lsl	r3, r3, #1
	strh	r1, [r2, r3]	@ movhi
	ldrh	r0, [r5]
	bl	INSERT_FREE_LIST
	mvn	r3, #0
	strh	r3, [r5]	@ movhi
	bl	FtlGcPageVarInit
	b	.L1049
.L1056:
	ldr	r0, [r3, #8]
	ldr	r2, [r4, #2504]
	str	ip, [fp, #-80]
	str	r3, [fp, #-76]
	cmp	r0, r2
	bhi	.L1055
	mov	r2, r7
	sub	r1, fp, #64
	bl	log2phys
	ldr	r3, [fp, #-76]
	ldr	r1, [fp, #-64]
	ldr	ip, [fp, #-80]
	ldr	r2, [r3, #12]
	cmn	r1, #1
	sub	r0, r2, r1
	clz	r0, r0
	lsr	r0, r0, #5
	moveq	r0, #0
	cmp	r0, #0
	bne	.L1059
.L1063:
	ldr	r2, [r3, #8]
	add	r8, r8, #1
	ldr	r1, [fp, #-72]
	ldr	r0, [r3, #12]
	bl	FtlGcUpdatePage
	b	.L1054
.L1059:
	str	r2, [fp, #-56]
	mov	r1, #1
	ldr	r2, [r4, #3316]
	sub	r0, fp, #60
	str	ip, [fp, #-80]
	str	r3, [fp, #-76]
	str	r2, [fp, #-52]
	ldr	r2, [r4, #3336]
	str	r2, [fp, #-48]
	mov	r2, r7
	bl	FlashReadPages
	ldrh	r1, [r4, #58]
	ldr	r2, [r4, #3288]
	ldr	ip, [fp, #-80]
	ldr	r0, [fp, #-52]
	ldr	r3, [fp, #-76]
	lsl	r1, r1, #7
	add	ip, r2, ip
	mov	r2, r7
.L1060:
	cmp	r2, r1
	beq	.L1063
	ldr	lr, [ip, #8]
	ldr	r7, [lr, r2, lsl #2]
	ldr	lr, [r0, r2, lsl #2]
	cmp	r7, lr
	beq	.L1061
	ldr	r2, [fp, #-56]
	ldrh	r1, [r5]
	ldr	r0, .L1075+8
	bl	sftl_printk
	b	.L1055
.L1061:
	add	r2, r2, #1
	b	.L1060
.L1066:
	ldr	r2, .L1075
	movw	r1, #65535
	ldrh	r3, [r2, #4]
	cmp	r3, r1
	beq	.L1068
	add	r3, r3, r9
	strh	r3, [r2, #4]	@ movhi
	ldrh	r3, [r4, #102]
	cmp	r3, r6
	bls	.L1068
.L1069:
	mvn	r0, #0
	sub	sp, fp, #40
	ldmfd	sp, {r4, r5, r6, r7, r8, r9, r10, fp, sp, pc}
.L1076:
	.align	2
.L1075:
	.word	.LANCHOR2
	.word	.LANCHOR0
	.word	.LC101
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
	push	{r4, r5, fp, ip, lr, pc}
	sub	fp, ip, #4
	sub	sp, sp, #88
	ldr	r3, .L1090
	ldr	r0, [r3, #2716]
	mov	r4, r3
	cmp	r0, #0
	beq	.L1078
	ldr	r1, [r3, #2720]
	ldr	r2, [r3, #2504]
	cmp	r1, r2
	bcs	.L1079
	mov	r5, #2048
.L1084:
	ldr	r0, [r4, #2720]
	ldr	r3, [r4, #2504]
	cmp	r0, r3
	bcs	.L1081
	mov	r2, #0
	sub	r1, fp, #108
	bl	log2phys
	ldr	r2, [fp, #-108]
	ldr	r3, [r4, #2720]
	cmn	r2, #1
	add	r3, r3, #1
	str	r3, [r4, #2720]
	beq	.L1082
	str	r3, [fp, #-88]
	sub	r0, fp, #20
	ldr	r3, [r4, #3324]
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
	bne	.L1081
	ldr	r0, [fp, #-108]
	ubfx	r0, r0, #10, #16
	bl	P2V_block_in_plane
	bl	FtlGcRefreshBlock
.L1081:
	mvn	r0, #0
.L1077:
	sub	sp, fp, #20
	ldmfd	sp, {r4, r5, fp, sp, pc}
.L1082:
	subs	r5, r5, #1
	bne	.L1084
	b	.L1081
.L1079:
	ldr	r2, [r3, #2508]
	mov	r0, #0
	str	r0, [r3, #2716]
	str	r0, [r3, #2720]
	str	r2, [r3, #2712]
	b	.L1077
.L1078:
	ldr	r1, [r3, #2508]
	ldr	r2, [r3, #2712]
	add	ip, r1, #1048576
	cmp	r2, ip
	bhi	.L1087
	ldr	r3, [r3, #2564]
	mov	ip, #33554432
	lsr	r3, r3, #10
	add	r2, r2, ip, asr r3
	cmp	r1, r2
	bhi	.L1087
	ldr	r3, .L1090+4
	ldrb	r3, [r3, #28]	@ zero_extendqisi2
	cmp	r3, #0
	bne	.L1077
.L1087:
	mov	r3, #1
	mov	r0, #0
	str	r3, [r4, #2716]
	str	r0, [r4, #2720]
	str	r1, [r4, #2712]
	b	.L1077
.L1091:
	.align	2
.L1090:
	.word	.LANCHOR0
	.word	.LANCHOR0+2268
	.size	FtlReadRefresh, .-FtlReadRefresh
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
	ldr	r3, .L1099
	ldr	r2, [r0, #24]
	sub	r5, r5, #1
	uxth	r5, r5
	ldr	r0, [r3, #3304]
	mov	r4, r3
	ldr	r1, [r3, #3332]
	str	r0, [r3, #3460]
	str	r1, [r3, #3464]
	ldr	r2, [r2, r5, lsl #2]
	cmp	r2, #0
	str	r2, [r3, #3456]
	beq	.L1094
	mov	r2, #1
	ldr	r0, .L1099+4
	mov	r1, r2
	bl	FlashReadPages
.L1095:
	ldr	r2, [r4, #3460]
	mov	r1, r5
	mov	r0, r6
	bl	FtlMapWritePage
	ldmfd	sp, {r4, r5, r6, fp, sp, pc}
.L1094:
	ldrh	r2, [r3, #110]
	mov	r1, #255
	bl	ftl_memset
	b	.L1095
.L1100:
	.align	2
.L1099:
	.word	.LANCHOR0
	.word	.LANCHOR0+3452
	.size	FtlMapBlkWriteDump_data, .-FtlMapBlkWriteDump_data
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
	bls	.L1101
	ldr	r3, .L1104
	sub	r0, fp, #84
	mov	r2, #32
	mov	r1, #165
	str	r0, [fp, #-92]
	lsl	r4, r4, #10
	ldr	r5, [r3, #3324]
	str	r5, [fp, #-96]
	bl	memset
	ldr	r3, .L1104+4
	mov	r2, #1
	mov	r1, r2
	sub	r0, fp, #104
	str	r3, [r5]	@ unaligned
	str	r3, [r5, #4]	@ unaligned
	str	r4, [fp, #-100]
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
.L1101:
	mov	r0, r4
	sub	sp, fp, #20
	ldmfd	sp, {r4, r5, fp, sp, pc}
.L1105:
	.align	2
.L1104:
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
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 1, uses_anonymous_args = 0
	mov	ip, sp
	push	{r4, r5, r6, r7, r8, r9, fp, ip, lr, pc}
	sub	fp, ip, #4
	sub	sp, sp, #4
	mov	r5, #0
	ldr	r4, .L1115
	mov	r1, #0
	ldr	r7, .L1115+4
	ldr	r0, [r4, #3300]
	add	r6, r4, #176
	ldr	r3, [r4, #3332]
	ldrh	r2, [r4, #110]
	str	r0, [r4, #3460]
	str	r3, [r4, #3464]
	bl	ftl_memset
.L1107:
	ldrh	r3, [r4, #54]
	cmp	r5, r3
	blt	.L1108
	ldr	r5, [r4, #3464]
	mov	r2, #16
	mov	r1, #255
	ldr	r8, .L1115+8
	mov	r6, #0
	mov	r0, r5
	mov	r7, r6
	bl	memset
	ldr	r3, .L1115+12
	strh	r3, [r5]	@ movhi
	ldr	r3, [r4, #160]
	str	r3, [r5, #4]
	ldrh	r3, [r4, #152]
	strh	r3, [r5, #2]	@ movhi
	ldrh	r3, [r4, #156]
	strh	r3, [r5, #8]	@ movhi
	ldrh	r3, [r4, #158]
	strh	r3, [r5, #10]	@ movhi
	ldr	r3, [r4, #28]
	strh	r3, [r5, #12]	@ movhi
.L1109:
	ldr	r3, [r4, #3300]
	mov	r9, #0
	ldrh	r2, [r4, #154]
	ldrh	r1, [r4, #152]
	str	r3, [r4, #3460]
	ldr	r3, [r4, #3332]
	str	r9, [r4, #3452]
	str	r3, [r4, #3464]
	orr	r3, r2, r1, lsl #10
	ldrh	r0, [r5, #10]
	str	r3, [r4, #3456]
	ldrh	r3, [r4, #156]
	str	r0, [sp]
	mov	r0, r8
	bl	sftl_printk
	ldrh	r3, [r4, #104]
	ldrh	r2, [r4, #154]
	sub	r3, r3, #1
	cmp	r2, r3
	blt	.L1110
	ldr	r3, [r4, #160]
	ldrh	r2, [r4, #152]
	ldr	r0, [r4, #3268]
	add	r3, r3, #1
	strh	r9, [r4, #154]	@ movhi
	str	r3, [r4, #160]
	str	r3, [r5, #4]
	ldrh	r3, [r4, #156]
	strh	r2, [r5, #8]	@ movhi
	strh	r2, [r4, #156]	@ movhi
	mov	r2, #1
	strh	r3, [r4, #152]	@ movhi
	mov	r1, r2
	lsl	r3, r3, #10
	str	r3, [r4, #3456]
	str	r3, [r0, #4]
	bl	FlashEraseBlocks
.L1110:
	mov	r3, #1
	ldr	r0, .L1115+16
	mov	r2, r3
	mov	r1, r3
	bl	FlashProgPages
	ldrh	r3, [r4, #154]
	add	r3, r3, #1
	strh	r3, [r4, #154]	@ movhi
	ldr	r3, [r4, #3452]
	cmn	r3, #1
	bne	.L1111
	add	r6, r6, #1
	ldr	r1, [r4, #3456]
	uxth	r6, r6
	ldr	r0, .L1115+20
	bl	sftl_printk
	cmp	r6, #3
	bls	.L1109
	mov	r2, r6
	ldr	r1, [r4, #3456]
	ldr	r0, .L1115+24
	bl	sftl_printk
.L1113:
	b	.L1113
.L1108:
	ldrh	r2, [r7]
	ldr	r3, [r4, #3460]
	ldr	r1, [r6, #4]!
	mul	r0, r2, r5
	lsl	r2, r2, #2
	add	r5, r5, #1
	add	r0, r3, r0, lsl #2
	bl	ftl_memcpy
	b	.L1107
.L1114:
	mov	r7, #1
	b	.L1109
.L1111:
	cmp	r7, #0
	beq	.L1114
	mov	r0, #0
	ldmib	sp, {r4, r5, r6, r7, r8, r9, fp, sp, pc}
.L1116:
	.align	2
.L1115:
	.word	.LANCHOR0
	.word	.LANCHOR0+3388
	.word	.LC102
	.word	-3887
	.word	.LANCHOR0+3452
	.word	.LC103
	.word	.LC104
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
	ldr	r10, .L1153
.L1118:
	ldr	r8, .L1153+4
	ldrh	r1, [r8, #-8]
	ldrh	r2, [r8]
	add	r2, r2, r1
	ldrh	r1, [r10, #40]
	cmp	r2, r1
	ble	.L1119
	movw	r2, #2615
	ldr	r1, .L1153+8
	ldr	r0, .L1153+12
	bl	sftl_printk
.L1119:
	ldr	r2, .L1153+16
	cmp	r4, r2
	movne	r1, #0
	bne	.L1120
	ldrh	r2, [r8]
	ldr	r1, [r10, #3160]
	mul	r0, r1, r2
	lsr	r1, r2, #1
	add	r1, r1, #1
	add	r1, r1, r0, lsr #2
	uxth	r1, r1
	cmp	r1, #0
	subne	r1, r1, #1
	uxthne	r1, r1
.L1120:
	ldr	r0, .L1153+20
	bl	List_pop_index_node
	ldrh	r2, [r8]
	mov	r7, r0
	uxth	r9, r0
	cmp	r2, #0
	bne	.L1121
	mov	r2, #2624
	ldr	r1, .L1153+8
	ldr	r0, .L1153+12
	bl	sftl_printk
.L1121:
	ldrh	r2, [r8]
	sub	r2, r2, #1
	strh	r2, [r8]	@ movhi
	ldrh	r2, [r10, #40]
	cmp	r2, r9
	bls	.L1118
	uxth	r7, r7
	ldr	r2, [r10, #2324]
	lsl	r6, r7, #1
	ldrh	r5, [r2, r6]
	cmp	r5, #0
	bne	.L1118
	strh	r9, [r4]	@ movhi
	mov	r0, r4
	bl	make_superblock
	ldrb	r2, [r4, #7]	@ zero_extendqisi2
	cmp	r2, #0
	bne	.L1123
	ldr	r2, [r10, #2324]
	mvn	r1, #0
	mov	r0, r7
	strh	r1, [r2, r6]	@ movhi
	bl	INSERT_DATA_LIST
	ldr	r3, .L1153+24
	ldrh	r2, [r8]
	ldrh	r1, [r3]
	add	r2, r2, r1
	ldrh	r1, [r10, #40]
	cmp	r2, r1
	ble	.L1118
	movw	r2, #2638
	ldr	r1, .L1153+8
	ldr	r0, .L1153+12
	bl	sftl_printk
	b	.L1118
.L1123:
	ldr	r3, .L1153+24
	ldrh	r2, [r8]
	ldrh	r1, [r3]
	add	r2, r2, r1
	ldrh	r1, [r10, #40]
	cmp	r2, r1
	ble	.L1125
	movw	r2, #2641
	ldr	r1, .L1153+8
	ldr	r0, .L1153+12
	bl	sftl_printk
.L1125:
	ldr	r0, [r10, #3268]
	mov	r1, #20
	ldrh	lr, [r10, #32]
	add	ip, r4, #16
	str	r1, [fp, #-44]
	mov	r2, r0
	mla	r3, r1, lr, r0
	mov	lr, #0
.L1126:
	cmp	r3, r2
	bne	.L1128
	cmp	r5, #0
	bne	.L1129
	movw	r2, #2652
	ldr	r1, .L1153+8
	ldr	r0, .L1153+12
	bl	sftl_printk
.L1129:
	ldr	r2, .L1153+28
	ldrh	r2, [r2]
	cmp	r2, r9
	bne	.L1130
	movw	r2, #2654
	ldr	r1, .L1153+8
	ldr	r0, .L1153+12
	bl	sftl_printk
.L1130:
	ldrb	r2, [r4, #8]	@ zero_extendqisi2
	ldr	r1, [r10, #2328]
	cmp	r2, #0
	ldrh	r2, [r1, r6]
	bne	.L1131
	cmp	r2, #0
	ldrhne	r0, [r10, #92]
	moveq	r2, #2
	addne	r2, r2, r0
	mov	r0, r7
	strh	r2, [r1, r6]	@ movhi
	mov	r1, #0
	ldr	r2, [r10, #2548]
	add	r2, r2, #1
	str	r2, [r10, #2548]
	bl	ftl_set_blk_mode
.L1134:
	ldr	r2, [r10, #2328]
	ldr	r1, [r10, #2564]
	ldr	ip, [r10, #2548]
	ldrh	r2, [r2, r6]
	ldrh	r0, [r10, #92]
	cmp	r2, r1
	ldrh	r1, [r10, #40]
	strhi	r2, [r10, #2564]
	ldr	r2, [r10, #2552]
	mla	r0, ip, r0, r2
	bl	__udivsi3
	ldr	r1, [r10, #3348]
	str	r0, [r10, #2556]
	ldr	r0, [r10, #3268]
	ldr	r2, [r1, #16]
	ldr	ip, .L1153+32
	add	r2, r2, #1
	str	r2, [r1, #16]
	mov	r1, #20
	mla	r1, r1, r5, r0
	add	r2, r0, #4
	add	r1, r1, #24
.L1136:
	add	r2, r2, #20
	cmp	r1, r2
	bne	.L1137
	ldrb	r1, [r4, #8]	@ zero_extendqisi2
	mov	r2, r5
	ldr	r0, [r10, #3268]
	mov	r8, #0
	bl	FlashEraseBlocks
	mov	r1, r8
	mov	ip, #20
.L1138:
	uxth	r2, r8
	cmp	r5, r2
	bhi	.L1140
	cmp	r1, #0
	ble	.L1141
	mov	r0, r7
	bl	update_multiplier_value
	bl	FtlBbmTblFlush
.L1141:
	ldrb	r1, [r4, #7]	@ zero_extendqisi2
	cmp	r1, #0
	bne	.L1142
	ldr	r2, [r10, #2324]
	mvn	r1, #0
	strh	r1, [r2, r6]	@ movhi
	b	.L1118
.L1128:
	str	lr, [r2, #8]
	movw	r8, #65535
	str	lr, [r2, #12]
	add	r2, r2, #20
	ldrh	r1, [ip], #2
	cmp	r1, r8
	ldrne	r8, [fp, #-44]
	lslne	r1, r1, #10
	mlane	r8, r8, r5, r0
	addne	r5, r5, #1
	uxthne	r5, r5
	strne	r1, [r8, #4]
	b	.L1126
.L1131:
	add	r2, r2, #1
	mov	r0, r7
	strh	r2, [r1, r6]	@ movhi
	ldr	r2, [r10, #2552]
	add	r2, r2, #1
	str	r2, [r10, #2552]
	bl	ftl_set_blk_mode.part.6
	b	.L1134
.L1137:
	ldr	r0, [r2, #-20]
	and	r0, r0, ip
	str	r0, [r2, #-20]
	b	.L1136
.L1140:
	mul	r2, ip, r8
	ldr	lr, [r10, #3268]
	add	r0, lr, r2
	ldr	r2, [lr, r2]
	cmn	r2, #1
	bne	.L1139
	ldr	r0, [r0, #4]
	add	r1, r1, #1
	str	ip, [fp, #-52]
	str	r2, [fp, #-48]
	ubfx	r0, r0, #10, #16
	str	r1, [fp, #-44]
	bl	FtlBbmMapBadBlock
	ldr	r2, [fp, #-48]
	add	r0, r4, r8, lsl #1
	ldr	ip, [fp, #-52]
	ldr	r1, [fp, #-44]
	strh	r2, [r0, #16]	@ movhi
	ldrb	r2, [r4, #7]	@ zero_extendqisi2
	sub	r2, r2, #1
	strb	r2, [r4, #7]
.L1139:
	add	r8, r8, #1
	b	.L1138
.L1142:
	ldrh	r2, [r10, #102]
	strh	r9, [r4]	@ movhi
	smulbb	r2, r2, r1
	mov	r1, #0
	strh	r1, [r4, #2]	@ movhi
	strb	r1, [r4, #6]
	ldr	r1, [r10, #2540]
	uxth	r2, r2
	ldr	r0, [r10, #2324]
	strh	r2, [r4, #4]	@ movhi
	str	r1, [r4, #12]
	add	r1, r1, #1
	str	r1, [r10, #2540]
	ldrh	r1, [r4]
	lsl	r3, r1, #1
	strh	r2, [r0, r3]	@ movhi
	ldrh	r3, [r4, #4]
	cmp	r3, #0
	beq	.L1143
	ldrb	r3, [r4, #7]	@ zero_extendqisi2
	cmp	r3, #0
	bne	.L1144
.L1143:
	movw	r2, #2707
	ldr	r1, .L1153+8
	ldr	r0, .L1153+12
	bl	sftl_printk
.L1144:
	mov	r0, #0
	sub	sp, fp, #40
	ldmfd	sp, {r4, r5, r6, r7, r8, r9, r10, fp, sp, pc}
.L1154:
	.align	2
.L1153:
	.word	.LANCHOR0
	.word	.LANCHOR0+2344
	.word	.LANCHOR1+403
	.word	.LC1
	.word	.LANCHOR0+2444
	.word	.LANCHOR0+2340
	.word	.LANCHOR0+2336
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
	ldr	r6, .L1167
	str	r3, [fp, #-44]
	movw	r3, #3206
	ldrh	r3, [r6, r3]
	cmp	r3, #0
	movne	r7, #0
	ldrne	r10, .L1167+4
	bne	.L1157
.L1156:
	mov	r0, #0
	sub	sp, fp, #40
	ldmfd	sp, {r4, r5, r6, r7, r8, r9, r10, fp, sp, pc}
.L1163:
	uxtah	r3, r6, r7
	ldr	r1, [fp, #-44]
	mov	r8, #0
	ldrb	r0, [r3, #60]	@ zero_extendqisi2
	bl	V2P_block
	mov	r9, r0
.L1158:
	ldrh	r3, [r10]
	uxth	r4, r8
	cmp	r3, r4
	bhi	.L1162
	add	r7, r7, #1
.L1157:
	ldrh	r2, [r6, #32]
	uxth	r3, r7
	cmp	r2, r3
	bhi	.L1163
	bl	FtlGcReFreshBadBlk
	b	.L1156
.L1162:
	uxth	r3, r8
	ldr	r5, .L1167+8
	lsl	r2, r3, #1
	ldrh	r2, [r5, r2]
	cmp	r2, r9
	bne	.L1159
	mov	r1, r9
	ldr	r0, .L1167+12
	str	r3, [fp, #-48]
	bl	sftl_printk
	mov	r0, r9
	bl	FtlBbmMapBadBlock
	bl	FtlBbmTblFlush
	ldr	r3, [fp, #-48]
	ldrh	r2, [r10]
	add	r3, r5, r3, lsl #1
.L1160:
	cmp	r4, r2
	bcc	.L1161
	sub	r2, r2, #1
	strh	r2, [r10]	@ movhi
.L1159:
	add	r8, r8, #1
	b	.L1158
.L1161:
	ldrh	r1, [r3, #2]!
	add	r4, r4, #1
	uxth	r4, r4
	strh	r1, [r3, #-2]	@ movhi
	b	.L1160
.L1168:
	.align	2
.L1167:
	.word	.LANCHOR0
	.word	.LANCHOR0+3206
	.word	.LANCHOR0+3208
	.word	.LC105
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
	ldr	r3, .L1179
	uxth	r4, r0
	ldr	r1, [r3, #2324]
	lsl	r2, r4, #1
	mov	r6, r3
	ldrh	r0, [r1, r2]
	cmp	r0, #0
	bne	.L1170
	movw	r2, #2588
	ldrh	r1, [r3, r2]
	cmp	r1, r4
	mvneq	r3, #0
	strheq	r3, [r6, r2]	@ movhi
	beq	.L1172
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
.L1172:
	ldr	r5, .L1179+4
	mov	r1, r4
	ldr	r0, .L1179+8
	bl	List_remove_node
	ldrh	r3, [r5]
	cmp	r3, #0
	bne	.L1174
	movw	r2, #2777
	ldr	r1, .L1179+12
	ldr	r0, .L1179+16
	bl	sftl_printk
.L1174:
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
	ble	.L1178
	movw	r2, #2780
	ldr	r1, .L1179+12
	ldr	r0, .L1179+16
	bl	sftl_printk
.L1178:
	mov	r0, #1
	ldmfd	sp, {r4, r5, r6, fp, sp, pc}
.L1170:
	mov	r0, r4
	bl	List_update_data_list
	mov	r0, #0
	ldmfd	sp, {r4, r5, r6, fp, sp, pc}
.L1180:
	.align	2
.L1179:
	.word	.LANCHOR0
	.word	.LANCHOR0+2336
	.word	.LANCHOR0+2320
	.word	.LANCHOR1+428
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
	ldr	r4, .L1193
	beq	.L1182
	ldr	r3, [r4, #2324]
	lsl	r5, r6, #1
	ldrh	r2, [r3, r5]
	cmp	r2, #0
	subne	r2, r2, #1
	strhne	r2, [r3, r5]	@ movhi
	bne	.L1182
	mov	r1, r6
	ldr	r0, .L1193+4
	bl	sftl_printk
	ldr	r3, [r4, #2324]
	ldrh	r5, [r3, r5]
	cmp	r5, #0
	beq	.L1184
.L1191:
	mov	r5, #0
.L1181:
	mov	r0, r5
	ldmfd	sp, {r4, r5, r6, r7, fp, sp, pc}
.L1184:
	movw	r2, #2795
.L1192:
	ldr	r1, .L1193+8
	ldr	r0, .L1193+12
	bl	sftl_printk
	b	.L1181
.L1182:
	movw	r7, #3438
	movw	r3, #65535
	ldrh	r0, [r4, r7]
	cmp	r0, r3
	strheq	r6, [r4, r7]	@ movhi
	beq	.L1191
.L1186:
	cmp	r6, r0
	beq	.L1191
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
	beq	.L1181
	movw	r2, #2811
	b	.L1192
.L1194:
	.align	2
.L1193:
	.word	.LANCHOR0
	.word	.LC106
	.word	.LANCHOR1+444
	.word	.LC1
	.size	decrement_vpc_count, .-decrement_vpc_count
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
	beq	.L1315
	ldrh	r3, [r0, #2]
	ldr	r5, .L1322
	str	r3, [fp, #-64]
	ldrb	r3, [r0, #6]	@ zero_extendqisi2
	ldr	r1, [fp, #-64]
	str	r3, [fp, #-80]
	ldrh	r3, [r5, #102]
	cmp	r3, r1
	mov	r3, #0
	strheq	r3, [r0, #4]	@ movhi
	ldrhne	r0, [r0, #16]
	bne	.L1199
.L1320:
	strb	r3, [r10, #6]
.L1315:
	mov	r0, #0
	sub	sp, fp, #40
	ldmfd	sp, {r4, r5, r6, r7, r8, r9, r10, fp, sp, pc}
.L1200:
	uxth	r1, r3
	add	r1, r10, r1, lsl #1
	ldrh	r0, [r1, #16]
.L1199:
	cmp	r0, r2
	uxth	r6, r3
	add	r3, r3, #1
	beq	.L1200
	mov	r1, #1
	bl	FtlGetLastWrittenPage
	cmn	r0, #1
	mov	r4, r0
	beq	.L1201
	ldrh	r3, [r5, #32]
	mov	r2, #0
	ldr	r0, [r5, #3288]
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
.L1202:
	ldr	r1, [fp, #-56]
	uxth	r3, r2
	cmp	r1, r3
	bhi	.L1206
	ldr	r6, .L1322
	mov	r2, #0
	mov	r1, r5
	movw	r8, #65535
	bl	FlashReadPages
	ldr	r3, [r6, #2544]
	uxth	r2, r4
	ldr	r9, [r6, #3288]
	str	r2, [fp, #-60]
	sub	r3, r3, #1
	str	r3, [fp, #-56]
	mov	r7, r9
	mov	r3, #0
.L1207:
	uxth	r2, r3
	cmp	r5, r2
	bhi	.L1212
	addeq	r3, r4, #1
	uxtheq	r3, r3
	streq	r3, [fp, #-60]
	bne	.L1210
.L1316:
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
	beq	.L1321
	movw	r3, #65535
	cmp	r8, r3
	bne	.L1216
	ldrb	r3, [r10, #8]	@ zero_extendqisi2
	cmp	r3, #0
	bne	.L1217
.L1216:
	ldr	r3, [r6, #3448]
	uxth	r7, r4
	uxth	r4, r4
	cmn	r3, #1
	ldreq	r3, [fp, #-56]
	streq	r3, [r6, #3448]
	ldr	r3, [fp, #-64]
	ldr	r6, .L1322
	add	r3, r3, #7
	cmp	r4, r3
	subgt	r4, r7, #7
	ldrle	r4, [fp, #-64]
	uxthgt	r4, r4
.L1220:
	cmp	r4, r7
	bhi	.L1227
	mov	r3, #0
	ldrh	r8, [r6, #32]
	ldr	r0, [r6, #3288]
	mov	r5, r3
	ldr	r1, [fp, #-76]
	movw	lr, #65535
	mov	ip, #20
	b	.L1228
.L1201:
	ldr	r3, [fp, #-64]
	cmp	r3, #0
	beq	.L1203
	movw	r2, #1766
	ldr	r1, .L1322+4
	ldr	r0, .L1322+8
	bl	sftl_printk
.L1203:
	ldr	r3, [fp, #-80]
	cmp	r3, #0
	cmpne	r6, r3
	beq	.L1204
	movw	r2, #1767
	ldr	r1, .L1322+4
	ldr	r0, .L1322+8
	bl	sftl_printk
.L1204:
	mov	r3, #0
	strh	r3, [r10, #2]	@ movhi
	b	.L1320
.L1206:
	ldrh	r3, [ip], #2
	cmp	r3, r7
	beq	.L1205
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
.L1205:
	add	r2, r2, #1
	b	.L1202
.L1212:
	ldr	r2, [r7]
	cmp	r2, #0
	bne	.L1208
	ldr	ip, [r7, #12]
	ldr	r2, [ip, #4]
	cmn	r2, #1
	beq	.L1209
	ldr	r1, .L1322
	mov	r0, r2
	str	ip, [fp, #-68]
	ldr	r1, [r1, #2544]
	bl	ftl_cmp_data_ver
	ldr	ip, [fp, #-68]
	cmp	r0, #0
	ldrne	r1, .L1322
	addne	r2, r2, #1
	strne	r2, [r1, #2544]
.L1209:
	ldr	r2, [ip]
	cmn	r2, #1
	bne	.L1211
.L1210:
	uxth	r2, r4
	uxth	r3, r3
	str	r2, [fp, #-60]
	mov	r2, #20
	mla	r9, r2, r3, r9
	b	.L1316
.L1208:
	ldr	r8, [fp, #-60]
.L1211:
	add	r3, r3, #1
	add	r7, r7, #20
	b	.L1207
.L1222:
	ldrh	r2, [r1], #2
	add	r3, r3, #1
	cmp	r2, lr
	mlane	r9, ip, r5, r0
	addne	r5, r5, #1
	orrne	r2, r4, r2, lsl #10
	uxthne	r5, r5
	strne	r2, [r9, #4]
.L1228:
	uxth	r2, r3
	cmp	r8, r2
	bhi	.L1222
	mov	r1, r5
	mov	r2, #0
	bl	FlashReadPages
	ldr	r3, [r6, #3288]
	mov	r2, #20
	movw	r1, #65535
	mla	r5, r2, r5, r3
.L1223:
	cmp	r5, r3
	addeq	r4, r4, #1
	uxtheq	r4, r4
	beq	.L1220
.L1226:
	ldr	r2, [r3]
	cmp	r2, #0
	bne	.L1217
	ldr	r2, [r3, #12]
	ldrh	r0, [r2]
	cmp	r0, r1
	beq	.L1225
	ldr	r2, [r2, #4]
	cmn	r2, #1
	strne	r2, [r6, #3448]
.L1225:
	add	r3, r3, #20
	b	.L1223
.L1227:
	mvn	r3, #0
	str	r3, [r6, #3448]
.L1217:
	ldr	r4, .L1322+12
	mov	r3, #1
	ldr	r8, [fp, #-64]
	strh	r3, [r4]	@ movhi
	sub	r4, r4, #3520
.L1229:
	ldrh	ip, [r4, #32]
	mov	r3, #0
	ldr	r0, [r4, #3288]
	mov	r9, r3
	ldr	r1, [fp, #-76]
	movw	lr, #65535
	mov	r5, #20
.L1230:
	uxth	r2, r3
	cmp	ip, r2
	bhi	.L1232
	mov	r2, #0
	mov	r1, r9
	bl	FlashReadPages
	mov	r3, #0
.L1319:
	str	r3, [fp, #-72]
	ldrh	r3, [fp, #-72]
	cmp	r9, r3
	bhi	.L1259
	add	r8, r8, #1
	ldrh	r3, [r4, #102]
	uxth	r8, r8
	cmp	r3, r8
	bne	.L1229
	ldrh	r2, [r4, #32]
	movw	r0, #65535
	mov	r3, #0
	strh	r8, [r10, #2]	@ movhi
	strh	r3, [r10, #4]	@ movhi
.L1260:
	uxth	r1, r3
	cmp	r1, r2
	bcs	.L1315
	ldr	r1, [fp, #-76]
	ldrh	ip, [r1], #2
	cmp	ip, r0
	str	r1, [fp, #-76]
	add	r1, r3, #1
	bne	.L1320
	mov	r3, r1
	b	.L1260
.L1232:
	ldrh	r2, [r1], #2
	add	r3, r3, #1
	cmp	r2, lr
	mlane	r6, r5, r9, r0
	orrne	r2, r8, r2, lsl #10
	addne	r9, r9, #1
	uxthne	r9, r9
	strne	r2, [r6, #4]
	b	.L1230
.L1259:
	ldr	r3, [fp, #-72]
	mov	r6, #20
	ldr	r2, [r4, #3288]
	mul	r6, r6, r3
	str	r2, [fp, #-84]
	add	r7, r2, r6
	ldr	r5, [r7, #4]
	ubfx	r0, r5, #10, #16
	str	r5, [fp, #-44]
	bl	P2V_plane
	ldr	r3, [fp, #-64]
	cmp	r8, r3
	bcc	.L1234
	ldr	r2, [fp, #-80]
	moveq	r3, #1
	movne	r3, #0
	cmp	r2, r0
	movls	r3, #0
	andhi	r3, r3, #1
	cmp	r3, #0
	bne	.L1234
	ldr	r3, [fp, #-60]
	ldr	r2, [fp, #-68]
	cmp	r8, r3
	cmpeq	r2, r0
	beq	.L1235
	ldr	r2, [fp, #-84]
	ldr	r3, [r2, r6]
	cmn	r3, #1
	beq	.L1236
	ldr	r7, [r7, #12]
	movw	r3, #61589
	ldrh	r2, [r7]
	cmp	r2, r3
	ldrhne	r0, [r10]
	bne	.L1317
	ldr	r3, [r7, #4]
	cmn	r3, #1
	str	r3, [fp, #-56]
	beq	.L1238
	mov	r0, r3
	ldr	r1, [r4, #2544]
	bl	ftl_cmp_data_ver
	cmp	r0, #0
	ldrne	r3, [fp, #-56]
	addne	r3, r3, #1
	strne	r3, [r4, #2544]
.L1238:
	ldrh	r2, [r7]
	movw	r3, #61589
	cmp	r2, r3
	beq	.L1239
	movw	r2, #1918
	ldr	r1, .L1322+4
	ldr	r0, .L1322+8
	bl	sftl_printk
.L1239:
	ldr	r5, [r7, #8]
	sub	r1, fp, #48
	ldr	r3, [r7, #12]
	mov	r2, #0
	mov	r0, r5
	str	r3, [fp, #-52]
	bl	log2phys
	ldr	r1, [r4, #3448]
	cmn	r1, #1
	beq	.L1240
	ldr	r0, [fp, #-56]
	bl	ftl_cmp_data_ver
	cmp	r0, #0
	beq	.L1240
	ldr	r2, [fp, #-52]
	cmn	r2, #1
	beq	.L1241
	ldr	r0, [r4, #3288]
	mov	r1, #1
	add	r0, r0, r6
	str	r2, [r0, #4]
	mov	r2, #0
	ldr	r7, [r0, #12]
	bl	FlashReadPages
	ldr	r2, [r4, #3288]
	ldr	r1, [r2, r6]
	add	r3, r2, r6
	cmn	r1, #1
	bne	.L1242
.L1243:
	mvn	r3, #0
	str	r3, [fp, #-52]
.L1250:
	ldr	r6, [fp, #-52]
	cmn	r6, #1
	beq	.L1234
.L1263:
	ubfx	r0, r6, #10, #16
	bl	P2V_block_in_plane
	ldrh	r3, [r4, #40]
	mov	r5, r0
	cmp	r3, r0
	bhi	.L1255
	movw	r2, #2019
	ldr	r1, .L1322+4
	ldr	r0, .L1322+8
	bl	sftl_printk
.L1255:
	ldr	r2, [r4, #2324]
	lsl	r3, r5, #1
	ldrh	r3, [r2, r3]
	cmp	r3, #0
	beq	.L1256
	mov	r0, r5
.L1317:
	bl	decrement_vpc_count
	b	.L1234
.L1241:
	ldr	r3, [fp, #-44]
	ldr	r2, [fp, #-48]
	cmp	r2, r3
	bne	.L1234
	mov	r2, #1
	sub	r1, fp, #52
	mov	r0, r5
	bl	log2phys
.L1234:
	ldr	r3, [fp, #-72]
	add	r3, r3, #1
	b	.L1319
.L1242:
	ldr	r1, [r7, #8]
	cmp	r5, r1
	bne	.L1243
	ldr	r1, [r7, #4]
	ldr	r0, [r4, #3448]
	str	r1, [fp, #-84]
	bl	ftl_cmp_data_ver
	cmp	r0, #0
	beq	.L1243
	ldr	r1, [fp, #-48]
	ldr	r0, [fp, #-44]
	cmp	r1, r0
	bne	.L1245
.L1248:
	ldr	r1, [fp, #-52]
	mov	r0, r5
	bl	FtlReUsePrevPpa
	b	.L1243
.L1245:
	ldr	r0, [fp, #-52]
	cmp	r1, r0
	beq	.L1243
	cmn	r1, #1
	streq	r1, [r2, r6]
	beq	.L1247
	str	r1, [r3, #4]
	mov	r2, #0
	mov	r1, #1
	mov	r0, r3
	ldr	r7, [r3, #12]
	bl	FlashReadPages
.L1247:
	ldr	r2, [r4, #3288]
	ldr	r2, [r2, r6]
	cmn	r2, #1
	beq	.L1248
	ldr	r3, [r7, #4]
	ldr	r0, [r4, #3448]
	mov	r1, r3
	bl	ftl_cmp_data_ver
	cmp	r0, #0
	beq	.L1248
	mov	r1, r3
	ldr	r0, [fp, #-84]
	bl	ftl_cmp_data_ver
	cmp	r0, #0
	beq	.L1243
	b	.L1248
.L1240:
	ldr	r3, [fp, #-44]
	ldr	r2, [fp, #-48]
	cmp	r2, r3
	beq	.L1250
	mov	r2, #1
	sub	r1, fp, #44
	mov	r0, r5
	bl	log2phys
	ldr	r6, [fp, #-48]
	cmn	r6, #1
	beq	.L1250
	ldr	r3, [fp, #-52]
	cmp	r6, r3
	beq	.L1263
	ubfx	r0, r6, #10, #16
	bl	P2V_block_in_plane
	ldr	r3, .L1322+16
	ldrh	r2, [r3]
	cmp	r2, r0
	beq	.L1254
	ldrh	r2, [r3, #48]
	cmp	r2, r0
	beq	.L1254
	ldrh	r3, [r3, #96]
	cmp	r3, r0
	bne	.L1250
.L1254:
	ldr	r0, [r4, #3288]
	mov	r2, #0
	mov	r1, #1
	str	r6, [r0, #4]
	ldr	r7, [r0, #12]
	bl	FlashReadPages
	ldr	r3, [r4, #3288]
	ldr	r3, [r3]
	cmn	r3, #1
	beq	.L1250
	ldr	r1, [r7, #4]
	ldr	r0, [fp, #-56]
	bl	ftl_cmp_data_ver
	cmp	r0, #0
	bne	.L1250
	mov	r2, #1
	sub	r1, fp, #48
	mov	r0, r5
	bl	log2phys
	b	.L1250
.L1256:
	mov	r1, r5
	ldr	r0, .L1322+20
	bl	sftl_printk
	b	.L1234
.L1236:
	ldr	r3, [r4, #3524]
	cmp	r3, #31
	addls	r2, r4, r3, lsl #2
	addls	r3, r3, #1
	strls	r3, [r4, #3524]
	strls	r5, [r2, #3528]
	ldrh	r0, [r10]
	bl	decrement_vpc_count
	ldr	r3, [r4, #3448]
	cmn	r3, #1
	ldreq	r3, [fp, #-56]
	beq	.L1318
	ldr	r2, [fp, #-56]
	cmp	r2, r3
	bcs	.L1234
	mov	r3, r2
.L1318:
	str	r3, [r4, #3448]
	b	.L1234
.L1235:
	ldrb	r3, [fp, #-68]	@ zero_extendqisi2
	ldr	r2, [fp, #-68]
	ldr	r1, [fp, #-60]
	strb	r3, [r10, #6]
	ldrh	r3, [fp, #-60]
	strh	r3, [r10, #2]	@ movhi
.L1321:
	mov	r0, r10
	bl	ftl_sb_update_avl_pages
	b	.L1315
.L1323:
	.align	2
.L1322:
	.word	.LANCHOR0
	.word	.LANCHOR1+464
	.word	.LC1
	.word	.LANCHOR0+3520
	.word	.LANCHOR0+2348
	.word	.LC107
	.size	FtlRecoverySuperblock, .-FtlRecoverySuperblock
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
	ldr	r6, .L1342
	ldr	r4, .L1342+4
	ldrh	r2, [r6, #4]
	cmp	r2, #0
	beq	.L1325
	ldrb	r3, [r4, #2356]	@ zero_extendqisi2
	cmp	r3, #0
	bne	.L1325
	ldrb	r1, [r4, #2355]	@ zero_extendqisi2
	ldrh	r3, [r4, #102]
	mul	r3, r3, r1
	cmp	r2, r3
	beq	.L1325
	ldrb	r8, [r4, #2358]	@ zero_extendqisi2
	cmp	r8, #0
	bne	.L1324
	ldr	r7, [r4, #2504]
	mov	r2, r8
	sub	r1, fp, #64
	ldrh	r9, [r4, #32]
	sub	r7, r7, #1
	mov	r0, r7
	bl	log2phys
	ldr	r3, [fp, #-64]
	ldr	r5, [r4, #3332]
	ldr	r0, [r4, #3300]
	cmn	r3, #1
	str	r3, [fp, #-56]
	str	r7, [fp, #-44]
	str	r0, [fp, #-52]
	str	r5, [fp, #-48]
	str	r8, [r5, #4]
	beq	.L1327
	mov	r2, r8
	mov	r1, #1
	sub	r0, fp, #60
	bl	FlashReadPages
.L1328:
	ldr	ip, .L1342
	mov	r8, #0
	ldr	r3, .L1342+8
	lsl	r9, r9, #2
	mov	r10, r8
	strh	r3, [r5]	@ movhi
.L1329:
	cmp	r9, r8
	bne	.L1333
.L1330:
	mov	r3, #1
.L1341:
	strb	r3, [r4, #2358]
.L1324:
	sub	sp, fp, #40
	ldmfd	sp, {r4, r5, r6, r7, r8, r9, r10, fp, sp, pc}
.L1327:
	ldrh	r2, [r4, #110]
	mov	r1, #255
	bl	ftl_memset
	b	.L1328
.L1333:
	ldrh	r3, [r6, #4]
	cmp	r3, #0
	beq	.L1330
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
	b	.L1329
.L1325:
	mov	r3, #0
	b	.L1341
.L1343:
	.align	2
.L1342:
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
	ldr	r5, .L1348
	mov	r4, #0
	mov	r6, #12
	bl	FtlWriteDump_data
.L1345:
	ldrh	r2, [r5, #138]
	uxth	r3, r4
	cmp	r2, r3
	bhi	.L1347
	mov	r0, #0
	ldmfd	sp, {r4, r5, r6, fp, sp, pc}
.L1347:
	ldr	r3, [r5, #2492]
	uxth	r0, r4
	mla	r3, r6, r0, r3
	ldr	r3, [r3, #4]
	cmp	r3, #0
	bge	.L1346
	bl	flush_l2p_region
.L1346:
	add	r4, r4, #1
	b	.L1345
.L1349:
	.align	2
.L1348:
	.word	.LANCHOR0
	.size	l2p_flush, .-l2p_flush
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
	ldr	r3, .L1363
	movw	r1, #61589
	mov	r4, r0
	ldr	r2, [r3, #3300]
	mov	r8, r3
	ldr	r5, [r3, #3332]
	str	r2, [fp, #-44]
	mvn	r2, #2
	str	r5, [fp, #-40]
	str	r2, [r5, #8]
	mvn	r2, #1
	str	r2, [r5, #12]
	ldrh	r2, [r0]
	strh	r6, [r5]	@ movhi
	strh	r2, [r5, #2]	@ movhi
	ldr	r2, [r3, #3300]
	str	r1, [r2]
	ldr	r1, .L1363+4
	ldr	r2, [r3, #3300]
	str	r1, [r2, #4]
	ldrh	r2, [r0, #4]
	tst	r2, #1
	moveq	r7, #6
	movne	r7, #7
.L1356:
	ldrh	r3, [r4, #4]
	cmp	r3, #0
	bne	.L1352
.L1353:
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
.L1352:
	mov	r0, r4
	bl	get_new_active_ppa
	cmn	r0, #1
	str	r0, [fp, #-48]
	beq	.L1353
	ldr	r3, [r8, #2544]
	mov	r1, #1
	sub	r0, fp, #52
	str	r3, [r5, #4]
	add	r3, r3, #1
	cmn	r3, #1
	moveq	r3, r6
	str	r3, [r8, #2544]
	mov	r3, #0
	mov	r2, r3
	bl	FlashProgPages
	ldrh	r0, [r4]
	bl	decrement_vpc_count
	subs	r7, r7, #1
	bne	.L1356
	b	.L1353
.L1364:
	.align	2
.L1363:
	.word	.LANCHOR0
	.word	305419896
	.size	FtlSuperblockPowerLostFix, .-FtlSuperblockPowerLostFix
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
	ldr	r4, .L1374
	mov	r5, r1
	str	r2, [fp, #-128]
	add	r2, r0, r1
	ldrh	r3, [r4, #96]
	cmp	r2, r3
	mvnhi	r8, #0
	bhi	.L1365
	ldrh	r6, [r4, #108]
	mov	r8, #0
	lsr	r6, r0, r6
	lsl	r10, r6, #2
.L1367:
	cmp	r5, #0
	bne	.L1372
.L1365:
	mov	r0, r8
	sub	sp, fp, #40
	ldmfd	sp, {r4, r5, r6, r7, r8, r9, r10, fp, sp, pc}
.L1372:
	ldr	r3, [r4, #3372]
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
	beq	.L1369
	ldr	r2, [r4, #3308]
	sub	r0, fp, #124
	str	ip, [fp, #-120]
	str	r2, [fp, #-116]
	sub	r2, fp, #104
	str	r2, [fp, #-112]
	mov	r2, #1
	mov	r1, r2
	bl	FlashReadPages
.L1370:
	ldr	r3, [fp, #-132]
	lsl	ip, r9, #9
	ldr	r0, [r4, #3308]
	sub	r5, r5, r9
	mov	r2, ip
	ldr	r1, [fp, #-128]
	str	ip, [fp, #-136]
	add	r7, r7, r9
	add	r10, r10, #4
	add	r0, r0, r3, lsl #9
	bl	ftl_memcpy
	mov	r1, r6
	ldr	r2, [r4, #3308]
	ldr	r0, .L1374+4
	add	r6, r6, #1
	bl	FtlMapWritePage
	ldr	r3, [fp, #-128]
	cmn	r0, #1
	ldr	ip, [fp, #-136]
	mvneq	r8, #0
	add	r3, r3, ip
	str	r3, [fp, #-128]
	b	.L1367
.L1369:
	ldrh	r2, [r4, #110]
	ldr	r0, [r4, #3308]
	bl	ftl_memset
	b	.L1370
.L1375:
	.align	2
.L1374:
	.word	.LANCHOR0
	.word	.LANCHOR0+3476
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
	ldr	r3, .L1378
	ldr	r2, .L1378+4
	ldr	r1, [r3, #2636]
	cmp	r1, r2
	ldmfdne	sp, {fp, sp, pc}
	ldr	r2, .L1378+8
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
	ldr	r2, .L1378+12
	bl	FtlVendorPartWrite
	ldmfd	sp, {fp, sp, pc}
.L1379:
	.align	2
.L1378:
	.word	.LANCHOR0
	.word	1179929683
	.word	1342177352
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
	ldr	r3, .L1385
	movw	r1, #3656
	ldrh	r2, [r3, r1]
	cmp	r2, #31
	addls	r2, r2, #1
	movhi	r2, #32
	strhls	r2, [r3, r1]	@ movhi
	movls	r2, #1
	cmp	r0, #0
	bne	.L1382
	ldr	r1, [r3, #3348]
	ldr	r0, [r1, #20]
	ldr	r1, [r1, #16]
	add	r2, r2, r0
	cmp	r1, r2
	bcc	.L1383
.L1382:
	ldr	r2, [r3, #3348]
	mov	r0, #64
	ldr	r1, [r2, #16]
	str	r1, [r2, #20]
	ldr	r1, .L1385+4
	str	r1, [r2]
	movw	r1, #3340
	ldrh	r1, [r3, r1]
	ldr	r2, [r3, #3348]
	lsl	r3, r1, #9
	str	r3, [r2, #12]
	ldr	r3, [r2, #8]
	add	r3, r3, #1
	str	r3, [r2, #8]
	mov	r3, #0
	str	r3, [r2, #4]
	bl	FtlVendorPartWrite
	bl	Ftl_save_ext_data
.L1383:
	mov	r0, #0
	ldmfd	sp, {fp, sp, pc}
.L1386:
	.align	2
.L1385:
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
	@ args = 0, pretend = 0, frame = 96
	@ frame_needed = 1, uses_anonymous_args = 0
	mov	ip, sp
	push	{r4, r5, r6, r7, r8, r9, r10, fp, ip, lr, pc}
	sub	fp, ip, #4
	sub	sp, sp, #96
	mov	r9, r2
	ldr	r3, .L1397
	mov	r6, r1
	add	r1, r0, r1
	mov	r7, r0
	ldrh	r2, [r3, #96]
	mov	r10, r3
	cmp	r1, r2
	mvnhi	r8, #0
	bhi	.L1387
	ldrh	r5, [r3, #108]
	mov	r8, #0
	lsr	r5, r0, r5
	lsl	r3, r5, #2
	str	r3, [fp, #-128]
.L1389:
	cmp	r6, #0
	bne	.L1395
.L1387:
	mov	r0, r8
	sub	sp, fp, #40
	ldmfd	sp, {r4, r5, r6, r7, r8, r9, r10, fp, sp, pc}
.L1395:
	ldr	r3, [fp, #-128]
	mov	r0, r7
	ldr	r2, [r10, #3372]
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
	beq	.L1391
	ldr	r2, [r10, #3308]
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
	ldr	r2, [r10, #3452]
	mvneq	r8, #0
	cmp	r2, #256
	bne	.L1393
	mov	r2, ip
	mov	r1, r5
	ldr	r0, .L1397+4
	bl	sftl_printk
	ldr	r2, [r10, #3308]
	mov	r1, r5
	ldr	r0, .L1397+8
	bl	FtlMapWritePage
.L1393:
	ldr	r1, [r10, #3308]
	lsl	r2, r4, #9
	ldr	r3, [fp, #-132]
	mov	r0, r9
	add	r1, r1, r3, lsl #9
	bl	ftl_memcpy
.L1394:
	ldr	r3, [fp, #-128]
	add	r5, r5, #1
	sub	r6, r6, r4
	add	r7, r7, r4
	add	r9, r9, r4, lsl #9
	add	r3, r3, #4
	str	r3, [fp, #-128]
	b	.L1389
.L1391:
	lsl	r2, r4, #9
	mov	r1, ip
	mov	r0, r9
	bl	ftl_memset
	b	.L1394
.L1398:
	.align	2
.L1397:
	.word	.LANCHOR0
	.word	.LC108
	.word	.LANCHOR0+3476
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
	ldr	r4, .L1401
	movw	r5, #3340
	mov	r0, #64
	ldr	r2, [r4, #3348]
	ldrh	r1, [r4, r5]
	bl	FtlVendorPartRead
	ldr	r3, [r4, #3348]
	ldr	r2, [r3]
	ldr	r3, .L1401+4
	cmp	r2, r3
	beq	.L1400
	ldr	r1, .L1401+8
	ldr	r0, .L1401+12
	bl	sftl_printk
	ldrh	r2, [r4, r5]
	mov	r1, #0
	ldr	r0, [r4, #3348]
	lsl	r2, r2, #9
	bl	ftl_memset
.L1400:
	mov	r0, #0
	ldmfd	sp, {r4, r5, fp, sp, pc}
.L1402:
	.align	2
.L1401:
	.word	.LANCHOR0
	.word	1112818501
	.word	.LC109
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
	ldr	r4, .L1406
	mov	r1, #1
	mov	r0, #0
	ldr	r2, .L1406+4
	bl	FtlVendorPartRead
	ldr	r5, .L1406+8
	ldr	r3, [r4, #2636]
	cmp	r3, r5
	beq	.L1404
	mov	r1, #512
	ldr	r0, .L1406+4
	bl	__memzero
	str	r5, [r4, #2636]
.L1404:
	ldr	r3, [r4, #2636]
	cmp	r3, r5
	bne	.L1405
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
.L1405:
	ldrh	r2, [r4, #92]
	ldr	r3, [r4, #2552]
	ldr	r0, [r4, #2548]
	ldrh	r1, [r4, #40]
	mla	r0, r0, r2, r3
	bl	__udivsi3
	str	r0, [r4, #2556]
	ldmfd	sp, {r4, r5, fp, sp, pc}
.L1407:
	.align	2
.L1406:
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
	ldr	r4, .L1417
	mov	r5, #0
	mov	r1, #255
	ldr	r10, .L1417+4
	ldr	r3, [r4, #3300]
	add	r6, r4, #2576
	ldr	r7, [r4, #3332]
	sub	r8, r6, #308
	add	r9, r6, #876
	str	r3, [r4, #3460]
	ldrh	r3, [r6]
	str	r7, [r4, #3464]
	str	r5, [r7, #12]
	strh	r3, [r7, #2]	@ movhi
	ldr	r3, .L1417+8
	strh	r3, [r7]	@ movhi
	ldr	r3, [r4, #2584]
	str	r5, [r7, #8]
	ldrh	r2, [r6, #-226]
	str	r3, [r7, #4]
	ldr	r3, .L1417+12
	str	r3, [r4, #2268]
	ldr	r3, .L1417+16
	str	r3, [r4, #2272]
	ldrh	r3, [r6, #6]
	strh	r3, [r8, #8]	@ movhi
	ldrh	r3, [r4, #54]
	strb	r3, [r4, #2278]
	movw	r3, #2348
	ldrh	r3, [r4, r3]
	strh	r3, [r8, #14]	@ movhi
	ldrb	r3, [r4, #2354]	@ zero_extendqisi2
	orr	r3, r3, r2, lsl #6
	ldrh	r2, [r6, #-178]
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
	ldrh	r2, [r6, #-130]
	ldrb	r3, [r4, #2450]	@ zero_extendqisi2
	ldr	r0, [r4, #3460]
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
	ldr	r0, [r4, #3460]
	movw	r8, #65535
	bl	ftl_memcpy
	ldrh	r2, [r4, #40]
	ldr	r0, [r4, #3460]
	ldr	r1, [r4, #2324]
	lsl	r2, r2, #1
	add	r0, r0, #48
	bl	ftl_memcpy
	ldrh	r0, [r4, #40]
	ldr	r3, [r4, #3460]
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
.L1409:
	ldr	r3, [r4, #3300]
	ldrh	r1, [r6, #2]
	ldrh	r2, [r6]
	str	r3, [r4, #3460]
	ldr	r3, [r4, #3332]
	str	r3, [r4, #3464]
	orr	r3, r1, r2, lsl #10
	str	r3, [r4, #3456]
	ldrh	r3, [r4, #104]
	sub	r3, r3, #1
	cmp	r1, r3
	blt	.L1410
	mov	r3, #0
	ldrh	r8, [r6, #4]
	strh	r3, [r6, #2]	@ movhi
	strh	r2, [r6, #4]	@ movhi
	bl	FtlFreeSysBlkQueueOut
	ldr	r3, [r4, #2540]
	strh	r0, [r6]	@ movhi
	add	r2, r3, #1
	str	r3, [r4, #2584]
	str	r2, [r4, #2540]
	lsl	r2, r0, #10
	str	r2, [r4, #3456]
	str	r3, [r7, #4]
	strh	r0, [r7, #2]	@ movhi
.L1410:
	mov	r3, #1
	mov	r0, r9
	mov	r2, r3
	mov	r1, r3
	bl	FlashProgPages
	ldrh	r3, [r6, #2]
	ldr	r2, [r4, #3452]
	add	r3, r3, #1
	uxth	r3, r3
	cmn	r2, #1
	strh	r3, [r6, #2]	@ movhi
	bne	.L1411
	cmp	r3, #1
	bne	.L1412
	movw	r2, #1135
	mov	r1, r10
	ldr	r0, .L1417+20
	bl	sftl_printk
.L1412:
	ldrh	r3, [r6, #2]
	add	r5, r5, #1
	uxth	r5, r5
	cmp	r3, #1
	ldrheq	r3, [r4, #104]
	subeq	r3, r3, #1
	strheq	r3, [r6, #2]	@ movhi
	cmp	r5, #3
	bls	.L1409
	mov	r2, r5
	ldr	r1, [r4, #3456]
	ldr	r0, .L1417+24
	bl	sftl_printk
.L1415:
	b	.L1415
.L1411:
	cmp	r2, #256
	cmpne	r3, #1
	beq	.L1409
	movw	r3, #65535
	cmp	r8, r3
	beq	.L1416
	mov	r1, #1
	mov	r0, r8
	bl	FtlFreeSysBlkQueueIn
.L1416:
	mov	r0, #0
	ldmfd	sp, {r4, r5, r6, r7, r8, r9, r10, fp, sp, pc}
.L1418:
	.align	2
.L1417:
	.word	.LANCHOR0
	.word	.LANCHOR1+486
	.word	-3932
	.word	1179929683
	.word	1342177352
	.word	.LC1
	.word	.LC110
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
	ldr	r3, .L1422
	ldr	r3, [r3]
	cmp	r3, #1
	bne	.L1421
	bl	FtlSysFlush
.L1421:
	mov	r0, #0
	ldmfd	sp, {fp, sp, pc}
.L1423:
	.align	2
.L1422:
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
	ldr	r4, .L1439
	mov	r8, r0
	mov	r6, r1
	ldr	r3, [r4, #140]
	cmp	r2, r3
	mvnhi	r0, #0
	bhi	.L1424
	cmp	r1, #31
	bhi	.L1426
.L1431:
	mov	r0, #0
.L1424:
	sub	sp, fp, #32
	ldmfd	sp, {r4, r5, r6, r7, r8, fp, sp, pc}
.L1426:
	ldrh	r5, [r4, #58]
	mov	r1, r5
	bl	__udivsi3
	smulbb	r3, r0, r5
	mov	r7, r0
	sub	r8, r8, r3
	uxth	r8, r8
	cmp	r8, #0
	beq	.L1427
	sub	r5, r5, r8
	add	r7, r0, #1
	cmp	r5, r6
	movcs	r5, r6
	uxth	r5, r5
	sub	r6, r6, r5
.L1427:
	mvn	r3, #0
	str	r3, [fp, #-36]
.L1428:
	ldrh	r3, [r4, #58]
	cmp	r6, r3
	bcs	.L1430
	ldr	r3, [r4, #3660]
	cmp	r3, #32
	bls	.L1431
	mov	r5, #0
	str	r5, [r4, #3660]
	bl	l2p_flush
	bl	FtlVpcTblFlush
	b	.L1431
.L1430:
	mov	r2, #0
	sub	r1, fp, #40
	mov	r0, r7
	bl	log2phys
	ldr	r3, [fp, #-40]
	cmn	r3, #1
	beq	.L1429
	ldr	r3, [r4, #3660]
	mov	r2, #1
	sub	r1, fp, #36
	mov	r0, r7
	add	r3, r3, #1
	str	r3, [r4, #3660]
	ldr	r3, [r4, #2512]
	add	r3, r3, #1
	str	r3, [r4, #2512]
	bl	log2phys
	ldr	r0, [fp, #-40]
	ubfx	r0, r0, #10, #16
	bl	P2V_block_in_plane
	bl	decrement_vpc_count
.L1429:
	ldrh	r3, [r4, #58]
	add	r7, r7, #1
	sub	r6, r6, r3
	b	.L1428
.L1440:
	.align	2
.L1439:
	.word	.LANCHOR0
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
	ldr	r4, .L1454
	ldr	r1, .L1454+4
	ldr	r0, .L1454+8
	bl	sftl_printk
	ldrh	r2, [r4, #42]
	mov	r1, #0
	ldr	r0, [r4, #3352]
	lsl	r2, r2, #1
	bl	ftl_memset
.L1442:
	ldr	r3, [r4, #2504]
	cmp	r5, r3
	bcc	.L1444
	ldr	r9, .L1454+12
	mov	r7, #0
	ldr	r10, .L1454+16
.L1445:
	ldrh	r3, [r4, #40]
	uxth	r6, r7
	cmp	r3, r6
	bhi	.L1450
	bl	l2p_flush
	bl	FtlVpcTblFlush
	ldmib	sp, {r4, r5, r6, r7, r8, r9, r10, fp, sp, pc}
.L1444:
	mov	r2, #0
	sub	r1, fp, #44
	mov	r0, r5
	bl	log2phys
	ldr	r0, [fp, #-44]
	cmn	r0, #1
	beq	.L1443
	ubfx	r0, r0, #10, #16
	bl	P2V_block_in_plane
	ldr	r2, [r4, #3352]
	lsl	r0, r0, #1
	ldrh	r3, [r2, r0]
	add	r3, r3, #1
	strh	r3, [r2, r0]	@ movhi
.L1443:
	add	r5, r5, #1
	b	.L1442
.L1450:
	uxth	r8, r7
	ldr	r3, [r4, #2324]
	movw	r1, #65535
	lsl	r5, r8, #1
	ldrh	r2, [r3, r5]
	ldr	r3, [r4, #3352]
	ldrh	r3, [r3, r5]
	cmp	r2, r1
	cmpne	r2, r3
	beq	.L1447
	mov	r1, r8
	mov	r0, r9
	bl	sftl_printk
	ldrh	r3, [r10]
	cmp	r3, r6
	beq	.L1447
	ldr	r3, .L1454+20
	ldrh	r2, [r3]
	cmp	r2, r6
	beq	.L1447
	ldrh	r3, [r3, #-48]
	cmp	r3, r6
	beq	.L1447
	ldr	r3, [r4, #2324]
	ldrh	r2, [r3, r5]
	cmp	r2, #0
	ldr	r2, [r4, #3352]
	ldrh	r2, [r2, r5]
	strh	r2, [r3, r5]	@ movhi
	bne	.L1449
.L1447:
	add	r7, r7, #1
	b	.L1445
.L1449:
	mov	r0, r8
	bl	update_vpc_list
	b	.L1447
.L1455:
	.align	2
.L1454:
	.word	.LANCHOR0
	.word	.LANCHOR1+501
	.word	.LC95
	.word	.LC111
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
	ldr	r4, .L1466
	mov	r6, r0
	ldrh	r5, [r0]
	ldrh	r3, [r4, #40]
	cmp	r3, r5
	bcs	.L1457
	movw	r2, #2714
	ldr	r1, .L1466+4
	ldr	r0, .L1466+8
	bl	sftl_printk
.L1457:
	movw	r3, #65535
	cmp	r5, r3
	beq	.L1458
	ldr	r2, [r4, #2324]
	lsl	r3, r5, #1
	mov	r0, r5
	ldrh	r3, [r2, r3]
	cmp	r3, #0
	beq	.L1459
	bl	INSERT_DATA_LIST
.L1458:
	mov	r3, #1
	strb	r3, [r6, #8]
	movw	r3, #3438
	ldrh	r0, [r4, r3]
	movw	r3, #65535
	cmp	r0, r3
	beq	.L1460
	cmp	r5, r0
	bne	.L1461
	ldr	r2, [r4, #2324]
	lsl	r3, r0, #1
	ldrh	r3, [r2, r3]
	cmp	r3, #0
	beq	.L1462
.L1461:
	bl	update_vpc_list
.L1462:
	mvn	r2, #0
	movw	r3, #3438
	strh	r2, [r4, r3]	@ movhi
.L1460:
	mov	r0, r6
	bl	allocate_data_superblock
	bl	l2p_flush
	mov	r0, #0
	bl	FtlEctTblFlush
	bl	FtlVpcTblFlush
	mov	r0, #0
	ldmfd	sp, {r4, r5, r6, fp, sp, pc}
.L1459:
	bl	INSERT_FREE_LIST
	b	.L1458
.L1467:
	.align	2
.L1466:
	.word	.LANCHOR0
	.word	.LANCHOR1+522
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
	ldr	r6, .L1482
	mov	r4, r3
	ldr	r10, .L1482+4
	mov	r2, #0
	ldrb	r3, [r3, #9]	@ zero_extendqisi2
	mov	r5, r0
	mov	r9, r1
	bl	FlashProgPages
.L1469:
	cmp	r7, r9
	beq	.L1476
	mov	r8, #0
	b	.L1477
.L1472:
	ldr	r0, [r5, #4]
	ubfx	r0, r0, #10, #16
	bl	P2V_block_in_plane
	ldrh	r3, [r4]
	cmp	r3, r0
	bne	.L1470
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
.L1470:
	ldrh	r3, [r4, #4]
	cmp	r3, #0
	bne	.L1471
	mov	r0, r4
	bl	allocate_new_data_superblock
.L1471:
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
.L1477:
	ldr	r3, [r5]
	cmn	r3, #1
	beq	.L1472
	ldrb	r2, [r4, #6]	@ zero_extendqisi2
	ldrh	r3, [r6, #32]
	cmp	r2, r3
	bcc	.L1473
	mov	r2, #960
	mov	r1, r10
	ldr	r0, .L1482+8
	bl	sftl_printk
.L1473:
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
	beq	.L1474
	ldr	r2, [r6, #2324]
	lsl	r3, r0, #1
	ldrh	r2, [r2, r3]
	cmp	r2, #0
	bne	.L1475
	mov	r1, r0
	ldr	r0, .L1482+12
	bl	sftl_printk
.L1475:
	mov	r0, r8
	bl	decrement_vpc_count
.L1474:
	add	r7, r7, #1
	add	r5, r5, #20
	b	.L1469
.L1476:
	ldrb	r2, [r4, #6]	@ zero_extendqisi2
	ldrh	r3, [r6, #32]
	cmp	r2, r3
	bcc	.L1468
	movw	r2, #975
	ldr	r1, .L1482+4
	ldr	r0, .L1482+8
	bl	sftl_printk
.L1468:
	sub	sp, fp, #40
	ldmfd	sp, {r4, r5, r6, r7, r8, r9, r10, fp, sp, pc}
.L1483:
	.align	2
.L1482:
	.word	.LANCHOR0
	.word	.LANCHOR1+551
	.word	.LC1
	.word	.LC112
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
	ldr	r4, .L1510
	movw	r2, #65535
	ldrh	r5, [r4, r3]
	ldrh	r1, [r4, #102]
	cmp	r5, r2
	bne	.L1485
.L1495:
	mov	r3, #0
	str	r3, [r4, #3444]
	movw	r3, #2444
	ldrh	r2, [r4, r3]
	movw	r3, #65535
	cmp	r2, r3
	bne	.L1508
.L1486:
	mov	r0, #0
	ldmib	sp, {r4, r5, r6, r7, r8, r9, r10, fp, sp, pc}
.L1485:
	cmp	r0, #0
	beq	.L1488
	ldr	r3, .L1510+4
	ldrh	r0, [r3, #4]
	cmp	r0, r2
	beq	.L1489
.L1490:
	mov	r1, #2
.L1488:
	ldr	r0, .L1510+8
	bl	FtlGcScanTempBlk
	cmn	r0, #1
	str	r0, [fp, #-44]
	beq	.L1491
	ldr	r2, [r4, #2328]
	lsl	r5, r5, #1
	ldrh	r3, [r2, r5]
	cmp	r3, #4
	bls	.L1492
	sub	r3, r3, #5
	mov	r0, #1
	strh	r3, [r2, r5]	@ movhi
	bl	FtlEctTblFlush
.L1492:
	ldr	r3, [r4, #3444]
	cmp	r3, #0
	bne	.L1493
	ldr	r3, [r4, #2732]
	ldr	r0, [fp, #-44]
	add	r3, r3, #1
	ubfx	r0, r0, #10, #16
	str	r3, [r4, #2732]
	bl	FtlBbmMapBadBlock
	bl	FtlBbmTblFlush
.L1493:
	mov	r3, #0
	str	r3, [r4, #3444]
.L1504:
	mov	r0, #1
	ldmib	sp, {r4, r5, r6, r7, r8, r9, r10, fp, sp, pc}
.L1489:
	mov	r2, #0
	strh	r2, [r3, #4]	@ movhi
	movw	r3, #2344
	ldrh	r3, [r4, r3]
	cmp	r3, #17
	bhi	.L1490
	b	.L1488
.L1491:
	ldr	r3, .L1510+4
	ldrh	r2, [r3, #4]
	movw	r3, #65535
	cmp	r2, r3
	bne	.L1504
	b	.L1495
.L1508:
	ldrb	r1, [r4, #2451]	@ zero_extendqisi2
	ldrh	r3, [r4, #102]
	ldr	r6, .L1510+12
	mul	r3, r3, r1
	ldrh	r2, [r6]
	cmp	r2, r3
	beq	.L1496
	mov	r2, #162
	ldr	r1, .L1510+16
	ldr	r0, .L1510+20
	bl	sftl_printk
.L1496:
	ldrb	r2, [r4, #2451]	@ zero_extendqisi2
	movw	r3, #2444
	ldrh	r0, [r4, #102]
	mov	r5, #0
	ldrh	r3, [r4, r3]
	ldr	r1, [r4, #2324]
	ldr	r10, .L1510+20
	smulbb	r2, r2, r0
	lsl	r3, r3, #1
	strh	r2, [r1, r3]	@ movhi
	ldr	r2, [r4, #2528]
	ldrh	r3, [r6]
	add	r3, r3, r2
	str	r3, [r4, #2528]
.L1497:
	ldrh	r2, [r6]
	uxth	r3, r5
	cmp	r2, r3
	bhi	.L1501
	movw	r0, #65535
	bl	decrement_vpc_count
	movw	r3, #2444
	ldr	r1, [r4, #2324]
	ldrh	r0, [r4, r3]
	mov	r5, r3
	lsl	r2, r0, #1
	ldrh	r2, [r1, r2]
	cmp	r2, #0
	beq	.L1502
	bl	INSERT_DATA_LIST
.L1503:
	mvn	r7, #0
	movw	r3, #3192
	strh	r7, [r4, r5]	@ movhi
	mov	r5, #0
	strh	r5, [r4, r3]	@ movhi
	strh	r5, [r6]	@ movhi
	bl	l2p_flush
	bl	FtlVpcTblFlush
	movw	r3, #2588
	strh	r7, [r4, r3]	@ movhi
	movw	r3, #2344
	ldrh	r2, [r4, r3]
	movw	r3, #2572
	ldrh	r3, [r4, r3]
	add	r3, r3, r3, lsl #1
	cmp	r2, r3, asr #2
	movgt	r2, #20
	movwgt	r3, #3148
	strhgt	r2, [r4, r3]	@ movhi
	b	.L1486
.L1501:
	uxth	r3, r5
	mov	r7, #12
	ldr	r8, [r4, #3196]
	mul	r7, r7, r3
	ldr	r2, [r4, #2504]
	add	r9, r8, r7
	ldr	r1, [r9, #8]
	cmp	r1, r2
	bcc	.L1498
	mov	r2, #168
	ldr	r1, .L1510+16
	mov	r0, r10
	bl	sftl_printk
.L1498:
	mov	r2, #0
	sub	r1, fp, #44
	ldr	r0, [r9, #8]
	bl	log2phys
	ldr	r2, [fp, #-44]
	ldr	r0, [r8, r7]
	cmp	r0, r2
	bne	.L1499
	ubfx	r0, r0, #10, #16
	bl	P2V_block_in_plane
	mov	r2, #1
	mov	r7, r0
	add	r1, r9, #4
	ldr	r0, [r9, #8]
	bl	log2phys
	mov	r0, r7
.L1509:
	bl	decrement_vpc_count
.L1500:
	add	r5, r5, #1
	b	.L1497
.L1499:
	ldr	r3, [r9, #4]
	cmp	r2, r3
	beq	.L1500
	ldr	r3, .L1510+8
	ldrh	r0, [r3]
	b	.L1509
.L1502:
	bl	INSERT_FREE_LIST
	b	.L1503
.L1511:
	.align	2
.L1510:
	.word	.LANCHOR0
	.word	.LANCHOR2
	.word	.LANCHOR0+2444
	.word	.LANCHOR0+3200
	.word	.LANCHOR1+564
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
	ldr	r4, .L1514
	ldr	r5, .L1514+4
	ldrh	r1, [r4, #102]
	mov	r0, r5
	bl	FtlGcScanTempBlk
	ldrh	r2, [r5, #2]
	ldrh	r3, [r4, #102]
	cmp	r2, r3
	ldmfdcc	sp, {r4, r5, fp, sp, pc}
	add	r0, r4, #3392
	bl	FtlMapBlkWriteDump_data
	mov	r0, #0
	bl	FtlGcFreeTempBlock
	mov	r3, #0
	str	r3, [r4, #3444]
	ldmfd	sp, {r4, r5, fp, sp, pc}
.L1515:
	.align	2
.L1514:
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
	ldr	r4, .L1517
	mov	r6, #0
	add	r5, r4, #2336
	str	r6, [r4, #3524]
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
	ldr	r4, .L1526
	mov	r3, #0
	mov	r5, #0
	mov	r6, #20
	mov	r2, r3
	ldr	r1, [r4, #3164]
	ldr	r0, [r4, #3292]
	bl	FlashProgPages
.L1520:
	ldr	r1, [r4, #3164]
	uxth	r3, r5
	cmp	r1, r3
	bhi	.L1523
	ldr	r0, [r4, #3292]
	bl	FtlGcBufFree
	ldr	r3, .L1526+4
	mov	r0, #0
	str	r0, [r4, #3164]
	ldrh	r3, [r3, #4]
	cmp	r3, r0
	ldmfdne	sp, {r4, r5, r6, fp, sp, pc}
	mov	r0, #1
	bl	FtlGcFreeTempBlock
	b	.L1525
.L1523:
	mul	r3, r6, r3
	ldr	r2, [r4, #3292]
	add	r5, r5, #1
	ldr	r0, [r2, r3]
	add	r1, r2, r3
	cmn	r0, #1
	bne	.L1521
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
	ldr	r2, [r4, #3292]
	add	r3, r2, r3
	ldr	r0, [r3, #4]
	ubfx	r0, r0, #10, #16
	bl	FtlBbmMapBadBlock
	bl	FtlBbmTblFlush
	bl	FtlGcPageVarInit
.L1525:
	mov	r0, #1
	ldmfd	sp, {r4, r5, r6, fp, sp, pc}
.L1521:
	ldr	r3, [r1, #12]
	ldr	r1, [r1, #4]
	ldr	r2, [r3, #8]
	ldr	r0, [r3, #12]
	bl	FtlGcUpdatePage
	b	.L1520
.L1527:
	.align	2
.L1526:
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
	ldr	r4, .L1531
	movw	r3, #2444
	ldrh	r2, [r4, r3]
	movw	r3, #65535
	cmp	r2, r3
	beq	.L1529
	ldr	r3, .L1531+4
	ldrh	r3, [r3, #4]
	cmp	r3, #0
	bne	.L1530
.L1529:
	mov	r0, #0
	mov	r5, #0
	bl	FtlGcFreeTempBlock
	ldr	r0, .L1531+4
	strb	r5, [r4, #2452]
	bl	allocate_data_superblock
	movw	r3, #3192
	strh	r5, [r4, r3]	@ movhi
	ldr	r3, .L1531+8
	strh	r5, [r3]	@ movhi
	bl	l2p_flush
	mov	r0, r5
	bl	FtlEctTblFlush
	bl	FtlVpcTblFlush
.L1530:
	ldr	r0, .L1531+4
	bl	get_new_active_ppa
	ldmfd	sp, {r4, r5, fp, sp, pc}
.L1532:
	.align	2
.L1531:
	.word	.LANCHOR0
	.word	.LANCHOR0+2444
	.word	.LANCHOR0+3200
	.size	Ftl_get_new_temp_ppa, .-Ftl_get_new_temp_ppa
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
	ldr	r4, .L1548
	mov	r3, #0
	add	r7, r4, #3520
	ldrh	r0, [r4, #36]
	strh	r3, [r7]	@ movhi
	bl	FtlFreeSysBlkQueueInit
	bl	FtlScanSysBlk
	add	r3, r4, #2576
	ldrh	r2, [r3]
	movw	r3, #65535
	cmp	r2, r3
	bne	.L1534
.L1536:
	mvn	r8, #0
.L1533:
	mov	r0, r8
	ldmfd	sp, {r4, r5, r6, r7, r8, fp, sp, pc}
.L1534:
	bl	FtlLoadSysInfo
	subs	r8, r0, #0
	bne	.L1536
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
.L1537:
	cmp	r3, r1
	bge	.L1542
	mla	ip, r0, r3, r2
	ldr	ip, [ip, #4]
	cmp	ip, #0
	bge	.L1538
.L1542:
	ldr	r5, .L1548+4
	cmp	r3, r1
	ldrh	r2, [r5, #28]
	add	r6, r5, #80
	add	r2, r2, #1
	strh	r2, [r5, #28]	@ movhi
	bge	.L1547
.L1539:
	ldr	r7, .L1548+8
	ldr	r0, .L1548+12
	bl	FtlSuperblockPowerLostFix
	mov	r0, r7
	bl	FtlSuperblockPowerLostFix
	movw	r3, #2348
	ldr	r1, [r4, #2324]
	ldrh	r3, [r4, r3]
	ldrh	r0, [r6, #4]
	lsl	r3, r3, #1
	ldrh	r2, [r1, r3]
	sub	r2, r2, r0
	strh	r2, [r1, r3]	@ movhi
	movw	r2, #2396
	ldrh	r2, [r4, r2]
	ldrh	r3, [r4, #102]
	ldr	r0, [r4, #2324]
	ldrh	ip, [r7, #4]
	lsl	r2, r2, #1
	strh	r3, [r6, #2]	@ movhi
	mov	r3, #0
	strb	r3, [r4, #2354]
	strh	r3, [r6, #4]	@ movhi
	ldrh	r1, [r0, r2]
	sub	r1, r1, ip
	strh	r1, [r0, r2]	@ movhi
	strb	r3, [r4, #2402]
	strh	r3, [r7, #4]	@ movhi
	ldrh	r3, [r5, #30]
	ldrh	r2, [r4, #102]
	add	r3, r3, #1
	strh	r2, [r7, #2]	@ movhi
	strh	r3, [r5, #30]	@ movhi
	bl	l2p_flush
	bl	FtlVpcTblFlush
	bl	FtlVpcTblFlush
	b	.L1543
.L1538:
	add	r3, r3, #1
	b	.L1537
.L1547:
	ldrh	r3, [r7]
	cmp	r3, #0
	bne	.L1539
.L1543:
	bl	FtlVpcCheckAndModify
	movw	r3, #2348
	ldrh	r0, [r4, r3]
	movw	r3, #65535
	cmp	r0, r3
	beq	.L1533
	ldrh	r3, [r6, #4]
	cmp	r3, #0
	bne	.L1533
	ldr	r5, .L1548+8
	ldrh	r3, [r5, #4]
	cmp	r3, #0
	bne	.L1533
	bl	FtlGcRefreshBlock
	movw	r3, #2396
	ldrh	r0, [r4, r3]
	bl	FtlGcRefreshBlock
	bl	FtlVpcTblFlush
	sub	r0, r5, #48
	bl	allocate_new_data_superblock
	mov	r0, r5
	bl	allocate_new_data_superblock
	b	.L1533
.L1549:
	.align	2
.L1548:
	.word	.LANCHOR0
	.word	.LANCHOR0+2268
	.word	.LANCHOR0+2396
	.word	.LANCHOR0+2348
	.size	FtlSysBlkInit, .-FtlSysBlkInit
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
	ldr	r3, .L1669
	str	r0, [fp, #-60]
	ldr	r0, [r3, #3272]
	mov	r6, r3
	cmp	r0, #0
	movne	r0, #0
	bne	.L1550
	add	r2, r3, #2336
	ldrh	r2, [r2]
	cmp	r2, #47
	bls	.L1550
	ldr	r2, .L1669+4
	ldrh	r1, [r2, #4]
	movw	r2, #65535
	cmp	r1, r2
	bne	.L1552
.L1555:
	movw	r3, #3204
	movw	r0, #65535
	ldrh	r1, [r6, r3]
	cmp	r1, r0
	bne	.L1553
.L1554:
	bl	FtlReadRefresh
	ldr	r3, [r6, #3152]
	movw	r4, #65535
	ldr	r2, [fp, #-60]
	add	r3, r3, #1
	add	r3, r3, r2, lsl #7
	movw	r2, #2588
	ldrh	r2, [r6, r2]
	str	r3, [r6, #3152]
	cmp	r2, r4
	bne	.L1556
	movw	r1, #2444
	ldrh	r4, [r6, r1]
	cmp	r4, r2
	movne	r4, r2
	bne	.L1556
	movw	r2, #3202
	ldrh	r5, [r6, r2]
	cmp	r5, r4
	bne	.L1556
	movw	r9, #2344
	ldrh	r2, [r6, r9]
	cmp	r2, #24
	movcc	r2, #5120
	movcs	r2, #1024
	cmp	r3, r2
	movls	r4, r5
	bls	.L1556
	ldr	r3, .L1669+8
	mov	r8, #0
	str	r8, [r6, #3152]
	strh	r8, [r3]	@ movhi
	bl	GetSwlReplaceBlock
	cmp	r0, r5
	mov	r4, r0
	movne	r5, r0
	bne	.L1558
	movw	r7, #3150
	ldrh	r2, [r6, r9]
	ldrh	r3, [r6, r7]
	cmp	r2, r3
	bcs	.L1559
	mov	r0, #64
	bl	List_get_gc_head_node
	uxth	r3, r0
	cmp	r3, r4
	beq	.L1561
	mov	r0, r3
	ldr	r3, [r6, #2324]
	lsl	r0, r0, #1
	ldrh	r3, [r3, r0]
	cmp	r3, #7
	bhi	.L1562
	mov	r0, r8
	bl	List_get_gc_head_node
	uxth	r5, r0
	mov	r3, #128
	strh	r3, [r6, r7]	@ movhi
	cmp	r5, r4
	bne	.L1558
.L1561:
	bl	FtlGcReFreshBadBlk
.L1556:
	movw	r1, #65535
	ldr	r2, [fp, #-60]
	sub	r3, r4, r1
	clz	r3, r3
	lsr	r3, r3, #5
	cmp	r2, #0
	movne	r2, #0
	andeq	r2, r3, #1
	cmp	r2, #0
	beq	.L1564
	movw	r3, #2344
	ldrh	r3, [r6, r3]
	cmp	r3, #24
	movhi	r5, #1
	bhi	.L1565
	ldrh	r5, [r6, #102]
	cmp	r3, #16
	lsrhi	r5, r5, #5
	bhi	.L1565
	cmp	r3, #12
	lsrhi	r5, r5, #4
	bhi	.L1565
	cmp	r3, #8
	lsrhi	r5, r5, #2
.L1565:
	movw	r2, #3148
	ldrh	r1, [r6, r2]
	cmp	r1, r3
	bcs	.L1569
	movw	r3, #2444
	movw	r1, #65535
	ldrh	r3, [r6, r3]
	cmp	r3, r1
	bne	.L1570
	movw	r1, #3202
	ldrh	r1, [r6, r1]
	cmp	r1, r3
	bne	.L1570
	ldr	r3, .L1669+8
	ldrh	r0, [r3]
	cmp	r0, #0
	bne	.L1571
	ldr	r3, [r6, #2504]
	ldr	r1, [r6, #2500]
	add	r3, r3, r3, lsl #1
	cmp	r1, r3, lsr #2
	movcs	r3, #18
	bcs	.L1663
.L1571:
	movw	r3, #2572
	movw	r2, #3148
	ldrh	r3, [r6, r3]
	add	r3, r3, r3, lsl #1
	asr	r3, r3, #2
.L1663:
	strh	r3, [r6, r2]	@ movhi
	mov	r3, #0
	str	r3, [r6, #3160]
.L1550:
	sub	sp, fp, #40
	ldmfd	sp, {r4, r5, r6, r7, r8, r9, r10, fp, sp, pc}
.L1552:
	movw	r1, #2444
	ldrh	r3, [r3, r1]
	cmp	r3, r2
	beq	.L1555
	mov	r0, #1
	bl	FtlGcFreeTempBlock
	cmp	r0, #0
	beq	.L1555
	mov	r0, #1
	b	.L1550
.L1553:
	movw	r2, #3202
	ldrh	ip, [r6, r2]
	cmp	ip, r0
	strheq	r1, [r6, r2]	@ movhi
	mvneq	r2, #0
	strheq	r2, [r6, r3]	@ movhi
	b	.L1554
.L1562:
	mov	r3, #64
.L1662:
	strh	r3, [r6, r7]	@ movhi
	b	.L1561
.L1559:
	mov	r3, #80
	b	.L1662
.L1558:
	movw	r0, #3148
	ldr	r3, [r6, #2324]
	ldrh	r0, [r6, r0]
	lsl	r1, r5, #1
	movw	r2, #2344
	ldrh	r3, [r3, r1]
	mov	r4, r5
	ldrh	r2, [r6, r2]
	str	r0, [sp, #4]
	ldr	r0, [r6, #2328]
	ldrh	r1, [r0, r1]
	ldr	r0, .L1669+12
	str	r1, [sp]
	mov	r1, r5
	bl	sftl_printk
	b	.L1561
.L1570:
	movw	r3, #2572
	movw	r2, #3148
	ldrh	r3, [r6, r3]
	add	r3, r3, r3, lsl #1
	asr	r3, r3, #2
	strh	r3, [r6, r2]	@ movhi
.L1569:
	movw	r3, #3206
	movw	r4, #65535
	ldrh	r3, [r6, r3]
	cmp	r3, #0
	addne	r5, r5, #32
	uxthne	r5, r5
.L1575:
	movw	r3, #2588
	ldrh	r2, [r6, r3]
	movw	r1, #65535
	cmp	r2, r1
	bne	.L1584
	cmp	r4, r2
	strhne	r4, [r6, r3]	@ movhi
	bne	.L1586
	movw	r2, #3202
	ldrh	r3, [r6, r2]
	cmp	r3, r4
	beq	.L1586
	ldr	r1, [r6, #2324]
	lsl	r3, r3, #1
	ldrh	r3, [r1, r3]
	cmp	r3, #0
	mvneq	r3, #0
	strheq	r3, [r6, r2]	@ movhi
	movw	r3, #3202
	ldrh	r1, [r6, r3]
	movw	r2, #2588
	strh	r1, [r6, r2]	@ movhi
	mvn	r2, #0
	strh	r2, [r6, r3]	@ movhi
.L1586:
	movw	r7, #2588
	mov	r3, #0
	ldrh	r0, [r6, r7]
	strb	r3, [r6, #2596]
	movw	r3, #65535
	cmp	r0, r3
	beq	.L1584
	bl	IsBlkInGcList
	cmp	r0, #0
	ldr	r8, .L1669+16
	mvnne	r3, #0
	strhne	r3, [r6, r7]	@ movhi
	movw	r7, #2588
	ldrh	r2, [r6, r7]
	movw	r3, #65535
	cmp	r2, r3
	beq	.L1584
	mov	r0, r8
	bl	make_superblock
	mov	r3, #0
	movw	r2, #3666
	strh	r3, [r6, r2]	@ movhi
	strh	r3, [r8, #2]	@ movhi
	strb	r3, [r6, #2594]
	ldrh	r3, [r6, r7]
	ldr	r2, [r6, #2324]
	lsl	r3, r3, #1
	ldrh	r2, [r2, r3]
	movw	r3, #3668
	strh	r2, [r6, r3]	@ movhi
.L1584:
	movw	r3, #2588
	movw	r2, #2348
	ldrh	r3, [r6, r3]
	ldrh	r2, [r6, r2]
	cmp	r2, r3
	beq	.L1590
	movw	r2, #2396
	ldrh	r2, [r6, r2]
	cmp	r2, r3
	bne	.L1591
.L1590:
	mvn	r2, #0
	movw	r3, #2588
	strh	r2, [r6, r3]	@ movhi
.L1591:
	ldr	r6, .L1669
	mov	r9, r6
.L1620:
	ldr	r7, .L1669+16
	movw	r3, #65535
	ldrh	r2, [r7]
	cmp	r2, r3
	bne	.L1592
	add	r10, r7, #568
	mov	r3, #0
	str	r3, [r6, #3160]
.L1593:
	ldrh	r8, [r10]
	mov	r0, r8
	bl	List_get_gc_head_node
	ldr	r3, .L1669+16
	uxth	r2, r0
	strh	r2, [r3]	@ movhi
	movw	r3, #65535
	cmp	r2, r3
	bne	.L1594
	mov	r3, #0
	mov	r0, #8
	strh	r3, [r10]	@ movhi
	b	.L1550
.L1564:
	movw	r0, #2444
	ldrh	r5, [r6, r0]
	cmp	r5, r1
	bne	.L1634
	movw	r1, #3202
	ldrh	r1, [r6, r1]
	cmp	r1, r5
	movne	r3, #0
	andeq	r3, r3, #1
	cmp	r3, #0
	beq	.L1634
	movw	r3, #2588
	ldrh	r3, [r6, r3]
	cmp	r3, r5
	beq	.L1576
.L1581:
	mov	r4, r5
.L1634:
	mov	r5, #1
	b	.L1575
.L1576:
	movw	r3, #2344
	str	r2, [r6, #3160]
	movw	r2, #3148
	ldrh	r1, [r6, r3]
	ldrh	r3, [r6, r2]
	ldr	r4, .L1669+8
	cmp	r1, r3
	bls	.L1577
	ldrh	r3, [r4]
	cmp	r3, #0
	bne	.L1578
	ldr	r3, [r6, #2504]
	ldr	r1, [r6, #2500]
	add	r3, r3, r3, lsl #1
	cmp	r1, r3, lsr #2
	movcs	r3, #18
	bcs	.L1664
.L1578:
	movw	r3, #2572
	movw	r2, #3148
	ldrh	r3, [r6, r3]
	add	r3, r3, r3, lsl #1
	asr	r3, r3, #2
.L1664:
	strh	r3, [r6, r2]	@ movhi
	bl	FtlReadRefresh
	mov	r0, #0
	bl	List_get_gc_head_node
	uxth	r0, r0
	ldr	r3, [r6, #2324]
	lsl	r0, r0, #1
	ldrh	r3, [r3, r0]
	cmp	r3, #4
	bls	.L1577
.L1667:
	ldrh	r0, [r4]
	b	.L1550
.L1577:
	ldrh	r0, [r4]
	cmp	r0, #0
	bne	.L1581
	movw	r3, #2572
	movw	r2, #3148
	ldrh	r7, [r6, r3]
	add	r3, r7, r7, lsl #1
	asr	r3, r3, #2
	strh	r3, [r6, r2]	@ movhi
	bl	List_get_gc_head_node
	uxth	r0, r0
	ldr	r3, [r6, #2324]
	ldrh	r1, [r6, #104]
	lsl	r0, r0, #1
	ldrh	r2, [r3, r0]
	ldrh	r3, [r6, #32]
	mul	r3, r3, r1
	add	r3, r3, r3, lsr #31
	cmp	r2, r3, asr #1
	ble	.L1582
	movw	r3, #2344
	sub	r7, r7, #1
	ldrh	r3, [r6, r3]
	cmp	r3, r7
	blt	.L1582
	bl	FtlReadRefresh
	b	.L1667
.L1582:
	cmp	r2, #0
	bne	.L1581
	movw	r0, #65535
	bl	decrement_vpc_count
	movw	r3, #2344
	ldrh	r0, [r6, r3]
	add	r0, r0, #1
	b	.L1550
.L1594:
	uxth	r3, r0
	str	r2, [fp, #-52]
	add	r8, r8, #1
	mov	r0, r3
	str	r3, [fp, #-48]
	bl	IsBlkInGcList
	cmp	r0, #0
	ldr	r3, [fp, #-48]
	ldr	r2, [fp, #-52]
	strhne	r8, [r10]	@ movhi
	bne	.L1593
	lsl	r0, r3, #1
	ldrh	lr, [r9, #32]
	ldrh	r3, [r9, #102]
	uxth	r8, r8
	ldr	r1, [r9, #2324]
	strh	r8, [r10]	@ movhi
	mul	r3, lr, r3
	ldrh	ip, [r1, r0]
	add	lr, r3, r3, lsr #31
	cmp	ip, lr, asr #1
	bgt	.L1597
	cmp	ip, #8
	cmphi	r8, #48
	bls	.L1598
	ldr	ip, .L1669+20
	ldrh	ip, [ip]
	cmp	ip, #35
	bhi	.L1598
.L1597:
	ldr	lr, .L1669+24
	mov	ip, #0
	strh	ip, [lr]	@ movhi
.L1598:
	ldrh	r1, [r1, r0]
	movw	r0, #65535
	cmp	r3, r1
	cmple	r4, r0
	bne	.L1599
	ldr	r3, .L1669+24
	ldrh	r3, [r3]
	cmp	r3, #3
	bhi	.L1599
	mvn	r2, #0
	movw	r3, #2588
	strh	r2, [r9, r3]	@ movhi
	mov	r3, #0
	ldr	r2, .L1669+24
	strh	r3, [r2]	@ movhi
	add	r3, r2, #508
.L1668:
	ldrh	r0, [r3]
	b	.L1550
.L1599:
	cmp	r1, #0
	bne	.L1600
	movw	r0, #65535
	bl	decrement_vpc_count
	ldr	r3, .L1669+24
	ldr	r2, .L1669+24
	ldrh	r3, [r3]
	add	r3, r3, #1
	strh	r3, [r2]	@ movhi
	b	.L1593
.L1600:
	mov	r3, #0
	strb	r3, [r9, #2596]
	ldr	r3, .L1669+28
	ldrh	r3, [r3]
	cmp	r3, r2
	bne	.L1601
	mov	r2, #700
	ldr	r1, .L1669+32
	ldr	r0, .L1669+36
	bl	sftl_printk
.L1601:
	ldr	r3, .L1669+40
	ldrh	r2, [r7]
	ldrh	r3, [r3]
	cmp	r2, r3
	bne	.L1602
	movw	r2, #701
	ldr	r1, .L1669+32
	ldr	r0, .L1669+36
	bl	sftl_printk
.L1602:
	ldr	r3, .L1669+44
	ldrh	r2, [r7]
	ldrh	r3, [r3]
	cmp	r2, r3
	bne	.L1603
	movw	r2, #702
	ldr	r1, .L1669+32
	ldr	r0, .L1669+36
	bl	sftl_printk
.L1603:
	ldr	r0, .L1669+16
	bl	make_superblock
	ldrh	r2, [r7]
	mov	r3, #0
	ldr	r1, .L1669+48
	ldr	r0, [r9, #2324]
	lsl	r2, r2, #1
	strh	r3, [r1]	@ movhi
	ldrh	r2, [r0, r2]
	strh	r3, [r7, #2]	@ movhi
	strb	r3, [r9, #2594]
	strh	r2, [r1, #2]	@ movhi
.L1592:
	mov	r3, #1
	str	r3, [r9, #3272]
	ldrh	r3, [r9, #102]
	str	r3, [fp, #-48]
	ldr	r3, [fp, #-60]
	cmp	r3, #0
	beq	.L1604
	ldr	r2, [fp, #-48]
	ldrh	r3, [r9, #32]
	ldr	r1, [r9, #2324]
	mul	r3, r2, r3
	ldrh	r2, [r7]
	lsl	r2, r2, #1
	ldrh	r2, [r1, r2]
	sub	r3, r3, r2
	add	r2, r3, #3
	cmp	r3, #0
	movlt	r3, r2
	add	r5, r5, r3, asr #2
	uxth	r5, r5
.L1604:
	ldrh	r3, [r7, #2]
	ldr	r1, [fp, #-48]
	add	r2, r3, r5
	cmp	r2, r1
	movgt	r2, r1
	subgt	r5, r2, r3
	mov	r3, #0
	uxthgt	r5, r5
.L1666:
	str	r3, [fp, #-56]
	ldrh	r3, [fp, #-56]
	ldr	r7, .L1669+16
	cmp	r5, r3
	bls	.L1614
	ldrh	ip, [r7, #2]
	mov	r2, #0
	ldr	r3, [fp, #-56]
	mov	lr, #20
	ldrh	r8, [r6, #32]
	ldr	r0, [r6, #3180]
	add	ip, ip, r3
	add	r3, r7, #14
	mov	r7, r2
	b	.L1615
.L1608:
	ldrh	r1, [r3, #2]!
	movw	r10, #65535
	add	r2, r2, #1
	cmp	r1, r10
	mlane	r10, lr, r7, r0
	addne	r7, r7, #1
	orrne	r1, ip, r1, lsl #10
	uxthne	r7, r7
	strne	r1, [r10, #4]
.L1615:
	uxth	r1, r2
	cmp	r8, r1
	bhi	.L1608
	ldrb	r2, [r6, #2596]	@ zero_extendqisi2
	mov	r1, r7
	bl	FlashReadPages
	mov	r3, #0
.L1665:
	str	r3, [fp, #-52]
	ldrh	r3, [fp, #-52]
	cmp	r7, r3
	ldrls	r3, [fp, #-56]
	addls	r3, r3, #1
	bls	.L1666
.L1613:
	ldr	r3, [fp, #-52]
	mov	r8, #20
	mul	r8, r8, r3
	ldr	r3, [r6, #3180]
	add	r2, r3, r8
	ldr	r3, [r3, r8]
	cmn	r3, #1
	beq	.L1610
	ldr	r10, [r2, #12]
	movw	r3, #61589
	ldrh	r2, [r10]
	cmp	r2, r3
	bne	.L1610
	ldr	r3, [r10, #8]
	cmn	r3, #1
	bne	.L1611
	mov	r2, #736
	ldr	r1, .L1669+32
	ldr	r0, .L1669+36
	str	r3, [fp, #-64]
	bl	sftl_printk
	ldr	r3, [fp, #-64]
.L1611:
	mov	r2, #0
	sub	r1, fp, #44
	mov	r0, r3
	bl	log2phys
	ldr	r3, [r6, #3180]
	ldr	r2, [fp, #-44]
	add	r3, r3, r8
	ldr	r1, [r3, #4]
	cmp	r1, r2
	bne	.L1610
	ldr	r2, .L1669+48
	ldr	r1, .L1669+48
	ldr	r0, [r6, #3164]
	ldrh	r2, [r2]
	ldr	r3, [r3, #16]
	add	r2, r2, #1
	strh	r2, [r1]	@ movhi
	mov	r1, #20
	ldr	r2, [r6, #3292]
	str	r1, [fp, #-68]
	mla	r2, r1, r0, r2
	str	r3, [r2, #16]
	str	r2, [fp, #-64]
	bl	Ftl_get_new_temp_ppa
	ldr	r2, [fp, #-64]
	ldr	r3, [r6, #3292]
	ldr	r1, [fp, #-68]
	str	r0, [r2, #4]
	ldr	r2, [r6, #3164]
	mla	r3, r1, r2, r3
	ldr	r2, [r6, #3180]
	add	r2, r2, r8
	ldr	r1, [r2, #8]
	str	r1, [r3, #8]
	mov	r1, #1
	ldr	r2, [r2, #12]
	str	r2, [r3, #12]
	ldr	r3, [fp, #-44]
	str	r3, [r10, #12]
	ldr	r3, .L1669+44
	ldrh	r2, [r3]
	str	r3, [fp, #-64]
	strh	r2, [r10, #2]	@ movhi
	ldr	r2, [r6, #2544]
	ldr	r0, [r6, #3180]
	str	r2, [r10, #4]
	ldr	r2, [r6, #3164]
	add	r0, r0, r8
	add	r2, r2, #1
	str	r2, [r6, #3164]
	bl	FtlGcBufAlloc
	ldrb	r1, [r6, #2451]	@ zero_extendqisi2
	ldr	r2, [r6, #3164]
	cmp	r1, r2
	beq	.L1612
	ldr	r3, [fp, #-64]
	ldrh	r3, [r3, #4]
	cmp	r3, #0
	bne	.L1610
.L1612:
	bl	Ftl_gc_temp_data_write_back
	cmp	r0, #0
	ldrne	r3, .L1669
	movne	r2, #0
	strne	r2, [r3, #3272]
	addne	r3, r3, #3664
	bne	.L1668
.L1610:
	ldr	r3, [fp, #-52]
	add	r3, r3, #1
	b	.L1665
.L1614:
	ldrh	r3, [r7, #2]
	add	r5, r5, r3
	ldr	r3, [fp, #-48]
	uxth	r5, r5
	cmp	r3, r5
	strh	r5, [r7, #2]	@ movhi
	bhi	.L1616
	ldr	r3, [r6, #3164]
	cmp	r3, #0
	beq	.L1617
	bl	Ftl_gc_temp_data_write_back
	cmp	r0, #0
	movne	r3, #0
	strne	r3, [r6, #3272]
	ldrne	r3, .L1669+8
	bne	.L1668
.L1617:
	ldr	r3, .L1669+48
	ldrh	r1, [r3]
	cmp	r1, #0
	bne	.L1618
	ldrh	r3, [r7]
	ldr	r2, [r6, #2324]
	lsl	r3, r3, #1
	ldrh	r0, [r2, r3]
	cmp	r0, #0
	beq	.L1618
	strh	r1, [r2, r3]	@ movhi
	ldrh	r0, [r7]
	bl	update_vpc_list
	bl	l2p_flush
	bl	FtlVpcTblFlush
.L1618:
	mvn	r3, #0
	strh	r3, [r7]	@ movhi
.L1616:
	mov	r3, #0
	str	r3, [r6, #3272]
	ldr	r3, .L1669+52
	ldrh	r3, [r3]
	cmp	r3, #2
	ldrhls	r5, [r6, #102]
	bls	.L1620
.L1619:
	ldr	r2, .L1669+8
	ldrh	r0, [r2]
	cmp	r0, #0
	addeq	r0, r3, #1
	b	.L1550
.L1670:
	.align	2
.L1669:
	.word	.LANCHOR0
	.word	.LANCHOR2
	.word	.LANCHOR0+3664
	.word	.LC113
	.word	.LANCHOR0+2588
	.word	.LANCHOR0+3192
	.word	.LANCHOR0+3156
	.word	.LANCHOR0+2348
	.word	.LANCHOR1+583
	.word	.LC1
	.word	.LANCHOR0+2396
	.word	.LANCHOR0+2444
	.word	.LANCHOR0+3666
	.word	.LANCHOR0+2344
	.size	rk_ftl_garbage_collect, .-rk_ftl_garbage_collect
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
	ldr	r4, .L1677
	mvn	r3, #0
	ldr	r5, .L1677+4
	ldr	r1, .L1677+8
	ldr	r0, .L1677+12
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
	bne	.L1675
	bl	FtlSysBlkInit
	cmp	r0, #0
	bne	.L1675
	mov	r3, #1
	str	r3, [r5]
	movw	r3, #2344
	ldrh	r3, [r4, r3]
	cmp	r3, #15
	bhi	.L1675
	movw	r4, #8129
.L1674:
	mov	r1, #1
	mov	r0, #0
	bl	rk_ftl_garbage_collect
	subs	r4, r4, #1
	bne	.L1674
.L1675:
	mov	r0, #0
	ldmfd	sp, {r4, r5, fp, sp, pc}
.L1678:
	.align	2
.L1677:
	.word	.LANCHOR0
	.word	.LANCHOR2
	.word	.LC70
	.word	.LC71
	.size	sftl_init, .-sftl_init
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
	str	r2, [fp, #-52]
	cmp	r0, #16
	mov	r9, r3
	bne	.L1681
	mov	r2, r3
	ldr	r1, [fp, #-52]
	add	r0, r6, #256
	bl	FtlVendorPartRead
	str	r0, [fp, #-48]
.L1680:
	ldr	r0, [fp, #-48]
	sub	sp, fp, #40
	ldmfd	sp, {r4, r5, r6, r7, r8, r9, r10, fp, sp, pc}
.L1681:
	ldr	r3, [fp, #-52]
	ldr	r10, .L1715
	add	r3, r1, r3
	str	r3, [fp, #-60]
	ldr	r2, [fp, #-60]
	ldr	r3, [r10, #140]
	cmp	r2, r3
	mvnhi	r3, #0
	strhi	r3, [fp, #-48]
	bhi	.L1680
	ldr	r3, .L1715+4
	ldr	r7, [r3]
	cmn	r7, #1
	streq	r7, [fp, #-48]
	beq	.L1680
	ldrh	r4, [r10, #58]
	mov	r0, r6
	mov	r8, #0
	mov	r7, r10
	mov	r5, r8
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
	ldr	r3, [r10, #2536]
	str	r8, [fp, #-72]
	str	r8, [fp, #-48]
	add	r3, r3, r2
	ldr	r2, [fp, #-56]
	str	r3, [r10, #2536]
	ldr	r3, [r10, #2508]
	add	r3, r3, r2
	str	r3, [r10, #2508]
.L1683:
	ldr	r3, [fp, #-56]
	cmp	r3, #0
	bne	.L1700
	ldr	r3, .L1715
	movw	r2, #3206
	ldrh	r2, [r3, r2]
	cmp	r2, #0
	bne	.L1701
	movw	r2, #2344
	ldrh	r3, [r3, r2]
	cmp	r3, #31
	bhi	.L1680
.L1701:
	mov	r1, #1
	mov	r0, #0
	bl	rk_ftl_garbage_collect
	b	.L1680
.L1700:
	sub	r1, fp, #44
	mov	r2, #0
	mov	r0, r4
	bl	log2phys
	ldr	r1, [fp, #-44]
	cmn	r1, #1
	moveq	r10, #0
	beq	.L1685
	ldr	r2, [r7, #3288]
	mov	r0, #20
	ldr	r3, [fp, #-64]
	ldrh	r10, [r7, #58]
	mla	r2, r0, r5, r2
	cmp	r4, r3
	str	r1, [r2, #4]
	bne	.L1689
	ldr	r1, [r7, #3312]
	mov	r0, r6
	str	r2, [fp, #-80]
	str	r1, [r2, #8]
	mov	r1, r10
	bl	__umodsi3
	ldr	r3, [fp, #-52]
	sub	r1, r10, r0
	ldr	r2, [fp, #-80]
	str	r0, [fp, #-72]
	cmp	r3, r1
	movcs	r3, r1
	cmp	r3, r10
	str	r3, [fp, #-76]
	streq	r9, [r2, #8]
.L1690:
	ldrh	r1, [r7, #112]
	ldr	r0, [r7, #3336]
	str	r4, [r2, #16]
	mul	r1, r5, r1
	add	r5, r5, #1
	bic	r1, r1, #3
	add	r1, r0, r1
	str	r1, [r2, #12]
	b	.L1688
.L1687:
	mla	r0, r0, r4, r10
	ldr	r2, [fp, #-60]
	cmp	r2, r0
	movhi	r2, #1
	movls	r2, #0
	cmp	r6, r0
	movhi	r2, #0
	cmp	r2, #0
	beq	.L1686
	sub	r0, r0, r6
	mov	r1, #512
	add	r0, r9, r0, lsl #9
	bl	__memzero
.L1686:
	add	r10, r10, #1
.L1685:
	ldrh	r0, [r7, #58]
	cmp	r10, r0
	bcc	.L1687
.L1688:
	ldr	r3, [fp, #-56]
	add	r4, r4, #1
	subs	r3, r3, #1
	str	r3, [fp, #-56]
	beq	.L1692
	ldrh	r2, [r7, #32]
	cmp	r5, r2, lsl #2
	bne	.L1683
.L1692:
	cmp	r5, #0
	beq	.L1683
	mov	r2, #0
	mov	r1, r5
	ldr	r0, [r7, #3288]
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
.L1699:
	mov	ip, #20
	ldr	r2, [r7, #3288]
	mul	ip, ip, r10
	ldr	r3, [fp, #-64]
	add	r2, r2, ip
	ldr	r1, [r2, #16]
	cmp	r3, r1
	bne	.L1694
	ldr	r1, [r2, #8]
	ldr	r2, [r7, #3312]
	cmp	r1, r2
	bne	.L1695
	ldr	r3, [fp, #-80]
	mov	r0, r9
	ldr	r2, [fp, #-84]
	str	ip, [fp, #-92]
	add	r1, r1, r3
.L1714:
	bl	ftl_memcpy
	ldr	ip, [fp, #-92]
.L1695:
	ldr	r1, [r7, #3288]
	add	r2, r1, ip
	ldr	r0, [r2, #12]
	ldr	lr, [r2, #16]
	ldr	r0, [r0, #8]
	cmp	lr, r0
	ldrne	r0, [r7, #2708]
	addne	r0, r0, #1
	strne	r0, [r7, #2708]
	ldr	lr, [r1, ip]
	cmn	lr, #1
	ldreq	r0, [r7, #2708]
	streq	lr, [fp, #-48]
	addeq	r0, r0, #1
	streq	r0, [r7, #2708]
	ldr	r1, [r1, ip]
	cmp	r1, #256
	bne	.L1698
	ldr	r0, [r2, #4]
	ubfx	r0, r0, #10, #16
	bl	P2V_block_in_plane
	bl	FtlGcRefreshBlock
.L1698:
	add	r10, r10, #1
	cmp	r5, r10
	bne	.L1699
	mov	r5, #0
	b	.L1683
.L1689:
	ldr	r3, [fp, #-68]
	cmp	r4, r3
	bne	.L1691
	ldr	r1, [r7, #3316]
	ldr	r3, [fp, #-60]
	str	r1, [r2, #8]
	mul	r1, r10, r4
	sub	r8, r3, r1
	cmp	r10, r8
	subeq	r1, r1, r6
	addeq	r1, r9, r1, lsl #9
	streq	r1, [r2, #8]
	b	.L1690
.L1691:
	mul	r10, r4, r10
	sub	r10, r10, r6
	add	r10, r9, r10, lsl #9
	str	r10, [r2, #8]
	b	.L1690
.L1694:
	ldr	r3, [fp, #-68]
	cmp	r3, r1
	bne	.L1695
	ldr	r1, [r2, #8]
	ldr	r2, [r7, #3316]
	cmp	r1, r2
	bne	.L1695
	ldrh	r0, [r7, #58]
	str	ip, [fp, #-92]
	ldr	r2, [fp, #-88]
	mul	r0, r3, r0
	sub	r0, r0, r6
	add	r0, r9, r0, lsl #9
	b	.L1714
.L1716:
	.align	2
.L1715:
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
	bne	.L1720
	mov	r2, r3
	ldr	r1, [fp, #-80]
	add	r0, r9, #256
	bl	FtlVendorPartWrite
.L1719:
	sub	sp, fp, #40
	ldmfd	sp, {r4, r5, r6, r7, r8, r9, r10, fp, sp, pc}
.L1720:
	ldr	r3, [fp, #-80]
	ldr	r4, .L1771
	add	r6, r1, r3
	ldr	r3, [r4, #140]
	cmp	r6, r3
	mvnhi	r0, #0
	bhi	.L1719
	ldr	r5, .L1771+4
	ldr	r0, [r5]
	cmn	r0, #1
	beq	.L1719
	ldrh	r7, [r4, #58]
	mov	r3, #2048
	str	r3, [r4, #3672]
	mov	r0, r9
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
	ldr	r3, [r4, #2532]
	add	r3, r3, r2
	ldr	r2, [fp, #-72]
	str	r3, [r4, #2532]
	ldr	r3, [r4, #2516]
	add	r3, r3, r2
	str	r3, [r4, #2516]
	ldr	r3, [r5, #8]
	ldr	r4, .L1771+8
	cmp	r3, #0
	beq	.L1722
	ldrh	r3, [r4, #4]
	add	r0, r4, #48
	cmp	r3, #0
	moveq	r4, r0
.L1722:
	ldr	r5, [fp, #-76]
	ldr	r10, .L1771
.L1723:
	ldr	r3, [fp, #-72]
	cmp	r3, #0
	bne	.L1751
	mov	r0, r3
	ldr	r1, [fp, #-96]
	bl	rk_ftl_garbage_collect
	movw	r3, #2344
	ldrh	r3, [r10, r3]
	cmp	r3, #5
	bls	.L1752
.L1754:
	mov	r0, #0
	b	.L1719
.L1751:
	ldrb	r2, [r4, #6]	@ zero_extendqisi2
	ldrh	r3, [r10, #32]
	cmp	r2, r3
	bcc	.L1724
	mov	r2, #1016
	ldr	r1, .L1771+12
	ldr	r0, .L1771+16
	bl	sftl_printk
.L1724:
	ldrh	r2, [r4, #4]
	cmp	r2, #0
	bne	.L1725
	ldr	r3, .L1771+8
	ldr	r6, .L1771+4
	cmp	r4, r3
	bne	.L1726
	add	r0, r4, #48
	ldrh	r7, [r0, #4]
	cmp	r7, #0
	bne	.L1727
	bl	allocate_new_data_superblock
	str	r7, [r6, #8]
.L1727:
	ldr	r0, .L1771+8
	bl	allocate_new_data_superblock
	ldr	r4, .L1771+8
	ldr	r3, [r6, #8]
	add	r0, r4, #48
	cmp	r3, #0
	movne	r4, r0
.L1728:
	ldrh	r3, [r4, #4]
	cmp	r3, #0
	bne	.L1725
	mov	r0, r4
	bl	allocate_new_data_superblock
.L1725:
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
	ldrh	r3, [r10, #32]
	cmp	r2, r3
	bcc	.L1729
	movw	r2, #1049
	ldr	r1, .L1771+12
	ldr	r0, .L1771+16
	bl	sftl_printk
.L1729:
	mov	r3, #0
.L1770:
	str	r3, [fp, #-68]
	ldr	r3, [fp, #-68]
	ldr	r2, [fp, #-108]
	cmp	r3, r2
	bne	.L1749
.L1731:
	mov	r3, r4
	mov	r2, #0
	ldr	r1, [fp, #-68]
	ldr	r0, [r10, #3296]
	bl	FtlProgPages
	ldr	r3, [fp, #-68]
	ldr	r2, [fp, #-72]
	cmp	r3, r2
	bls	.L1750
	movw	r2, #1126
	ldr	r1, .L1771+12
	ldr	r0, .L1771+16
	bl	sftl_printk
.L1750:
	ldr	r3, [fp, #-72]
	ldr	r2, [fp, #-68]
	sub	r3, r3, r2
	str	r3, [fp, #-72]
	b	.L1723
.L1726:
	str	r2, [r6, #8]
	ldrh	r2, [r3, #4]
	cmp	r2, #0
	movne	r4, r3
	bne	.L1725
	mov	r0, r4
	bl	allocate_new_data_superblock
	b	.L1728
.L1749:
	ldrh	r3, [r4, #4]
	cmp	r3, #0
	beq	.L1731
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
	ldr	r3, [r10, #3296]
	bic	r1, r1, #3
	str	r1, [fp, #-100]
	add	r3, r3, r7
	ldr	r1, [r10, #3336]
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
	bne	.L1732
	cmp	r5, r3
	bne	.L1733
	mov	r1, r6
	mov	r0, r9
	bl	__umodsi3
	ldr	r3, [fp, #-80]
	sub	r6, r6, r0
	str	r0, [fp, #-88]
	cmp	r6, r3
	movcs	r6, r3
.L1734:
	ldrh	r3, [r10, #58]
	cmp	r6, r3
	ldr	r3, [fp, #-76]
	bne	.L1735
	cmp	r5, r3
	ldr	r3, [r10, #3296]
	add	r7, r3, r7
	ldreq	r3, [fp, #-84]
	streq	r3, [r7, #8]
	beq	.L1737
.L1767:
	mul	r6, r5, r6
	ldr	r3, [fp, #-84]
	sub	r6, r6, r9
	add	r6, r3, r6, lsl #9
	str	r6, [r7, #8]
	b	.L1737
.L1733:
	ldr	r3, [fp, #-80]
	add	r2, r9, r3
	smulbb	r3, r6, r5
	sub	r6, r2, r3
	mov	r3, #0
	uxth	r6, r6
	str	r3, [fp, #-88]
	b	.L1734
.L1735:
	cmp	r5, r3
	ldr	r3, [r10, #3296]
	ldreq	r2, [r10, #3312]
	ldrne	r2, [r10, #3316]
	add	r3, r3, r7
	str	r2, [r3, #8]
	ldr	r3, [fp, #-64]
	cmn	r3, #1
	beq	.L1740
	str	r3, [fp, #-56]
	mov	r1, #1
	ldr	r3, [r10, #3296]
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
	beq	.L1744
	ldr	r3, [r8, #8]
	cmp	r5, r3
	beq	.L1743
	ldr	r3, [r10, #2708]
	mov	r2, r5
	ldr	r0, .L1771+20
	add	r3, r3, #1
	str	r3, [r10, #2708]
	ldr	r1, [r8, #8]
	bl	sftl_printk
.L1743:
	ldr	r3, [r8, #8]
	cmp	r5, r3
	beq	.L1744
	movw	r2, #1102
	ldr	r1, .L1771+12
	ldr	r0, .L1771+16
	bl	sftl_printk
.L1744:
	ldr	r3, [fp, #-76]
	lsl	r2, r6, #9
	cmp	r5, r3
	bne	.L1745
	ldr	r3, [r10, #3296]
	ldr	r1, [fp, #-84]
	add	r7, r3, r7
	ldr	r3, [fp, #-88]
	ldr	r0, [r7, #8]
	add	r0, r0, r3, lsl #9
.L1768:
	bl	ftl_memcpy
.L1737:
	ldrb	r2, [r4, #6]	@ zero_extendqisi2
	ldrh	r3, [r10, #32]
	cmp	r2, r3
	bcc	.L1746
	movw	r2, #1117
	ldr	r1, .L1771+12
	ldr	r0, .L1771+16
	bl	sftl_printk
.L1746:
	ldr	r3, .L1771+24
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
	b	.L1770
.L1740:
	ldr	r3, [r10, #3296]
	mov	r1, #0
	ldrh	r2, [r10, #110]
	add	r3, r3, r7
	ldr	r0, [r3, #8]
	bl	ftl_memset
	b	.L1744
.L1745:
	ldrh	r1, [r10, #58]
	ldr	r3, [r10, #3296]
	mul	r1, r5, r1
	add	r7, r3, r7
	ldr	r3, [fp, #-84]
	ldr	r0, [r7, #8]
	sub	r1, r1, r9
	add	r1, r3, r1, lsl #9
	b	.L1768
.L1732:
	ldr	r3, [r10, #3296]
	add	r7, r3, r7
	b	.L1767
.L1752:
	ldr	r5, .L1771+28
	mov	r4, #256
	ldr	r6, .L1771+32
.L1755:
	ldrh	r3, [r5]
	movw	r2, #65535
	cmp	r3, r2
	bne	.L1753
	ldrh	r2, [r6]
	cmp	r2, r3
	bne	.L1753
	mov	r0, #0
	bl	List_get_gc_head_node
	uxth	r0, r0
	bl	FtlGcRefreshBlock
.L1753:
	ldr	r3, .L1771+36
	mov	r1, #1
	mov	r2, #128
	mov	r0, r1
	strh	r2, [r3]	@ movhi
	strh	r2, [r3, #-2]	@ movhi
	bl	rk_ftl_garbage_collect
	mov	r1, #1
	mov	r0, #0
	bl	rk_ftl_garbage_collect
	ldr	r3, .L1771+40
	ldrh	r3, [r3]
	cmp	r3, #2
	bhi	.L1754
	subs	r4, r4, #1
	bne	.L1755
	b	.L1754
.L1772:
	.align	2
.L1771:
	.word	.LANCHOR0
	.word	.LANCHOR2
	.word	.LANCHOR0+2348
	.word	.LANCHOR1+606
	.word	.LC1
	.word	.LC114
	.word	-3947
	.word	.LANCHOR0+2588
	.word	.LANCHOR0+3202
	.word	.LANCHOR0+3150
	.word	.LANCHOR0+2344
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
	ldr	r8, .L1793
	bl	FtlBbtMemInit
	bl	FtlLoadFactoryBbt
	sub	r5, r8, #180
	sub	r9, r8, #18
	mov	r4, r5
.L1775:
	ldrh	r3, [r5, #54]
	cmp	r7, r3
	bcc	.L1781
	mov	r0, #0
.L1782:
	ldrh	r2, [r4, #114]
	uxth	r3, r0
	add	r5, r0, #1
	cmp	r2, r3
	bhi	.L1783
	ldrh	r5, [r4, #164]
	movw	r6, #65535
	sub	r5, r5, #1
	uxth	r5, r5
.L1784:
	ldrh	r3, [r4, #164]
	sub	r3, r3, #48
	cmp	r5, r3
	ble	.L1788
	mov	r0, r5
	bl	FtlBbmIsBadBlock
	cmp	r0, #1
	beq	.L1785
	mov	r0, r5
	bl	FlashTestBlk
	cmp	r0, #0
	beq	.L1786
	mov	r0, r5
	bl	FtlBbmMapBadBlock
.L1785:
	sub	r5, r5, #1
	uxth	r5, r5
	b	.L1784
.L1781:
	ldrh	r3, [r9, #2]!
	movw	r2, #65535
	ldr	r0, [r5, #3300]
	ldr	r10, [r5, #3332]
	cmp	r3, r2
	str	r0, [r5, #3460]
	str	r10, [r5, #3464]
	beq	.L1776
	ldrh	ip, [r5, #98]
	mov	r2, #1
	mov	r1, r2
	ldr	r0, .L1793+4
	mla	ip, r7, ip, r3
	lsl	r3, ip, #10
	str	ip, [fp, #-44]
	str	r3, [r5, #3456]
	bl	FlashReadPages
	ldrh	r2, [r5, #98]
	ldr	r1, [r5, #3460]
	ldr	r0, [r8]
	add	r2, r2, #7
	asr	r2, r2, #3
	bl	ftl_memcpy
	ldr	ip, [fp, #-44]
.L1777:
	uxth	r0, ip
	add	r7, r7, #1
	add	r8, r8, #4
	bl	FtlBbmMapBadBlock
	b	.L1775
.L1776:
	mov	r1, r7
	bl	FlashGetBadBlockList
	ldr	r1, [r8]
	ldr	r0, [r5, #3460]
	bl	FtlBbt2Bitmap
	ldrh	r6, [r5, #98]
.L1779:
	sub	r6, r6, #1
	uxth	r6, r6
.L1778:
	ldrh	r0, [r4, #98]
	smlabb	r0, r0, r7, r6
	uxth	r0, r0
	bl	FtlBbmIsBadBlock
	cmp	r0, #1
	beq	.L1779
	mov	r1, #16
	ldr	r0, [r4, #3332]
	strh	r6, [r9]	@ movhi
	bl	__memzero
	mov	r1, #4096
	ldr	r0, [r4, #3300]
	bl	__memzero
	ldr	r3, .L1793+8
	strh	r3, [r10]	@ movhi
	mov	r3, #0
	str	r3, [r10, #4]
	ldrh	r3, [r9]
	ldrh	ip, [r4, #98]
	strh	r3, [r10, #2]	@ movhi
	ldrh	r3, [r9]
	ldr	r1, [r8]
	ldr	r0, [r4, #3460]
	mla	ip, r7, ip, r3
	lsl	r3, ip, #10
	str	ip, [fp, #-44]
	str	r3, [r4, #3456]
	ldr	r3, .L1793+12
	ldrh	r2, [r3]
	lsl	r2, r2, #2
	bl	ftl_memcpy
	mov	r2, #1
	ldr	r0, .L1793+4
	mov	r1, r2
	bl	FlashEraseBlocks
	mov	r3, #1
	ldr	r0, .L1793+4
	mov	r2, r3
	mov	r1, r3
	bl	FlashProgPages
	ldr	r3, [r4, #3452]
	ldr	ip, [fp, #-44]
	cmn	r3, #1
	bne	.L1777
	uxth	r0, ip
	bl	FtlBbmMapBadBlock
	b	.L1778
.L1783:
	uxth	r0, r0
	bl	FtlBbmMapBadBlock
	mov	r0, r5
	b	.L1782
.L1786:
	ldrh	r3, [r4, #152]
	cmp	r3, r6
	strheq	r5, [r4, #152]	@ movhi
	beq	.L1785
.L1787:
	strh	r5, [r4, #156]	@ movhi
.L1788:
	ldrh	r3, [r4, #152]
	mov	r5, #0
	ldr	r0, [r4, #3268]
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
.L1794:
	.align	2
.L1793:
	.word	.LANCHOR0+180
	.word	.LANCHOR0+3452
	.word	-3872
	.word	.LANCHOR0+3388
	.size	FtlMakeBbt, .-FtlMakeBbt
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
	ldr	r4, .L1819
	mov	r3, #0
	ldrh	r0, [r4, #36]
	str	r3, [r4, #2540]
	str	r3, [r4, #2544]
	bl	FtlFreeSysBlkQueueInit
	bl	FtlLoadBbt
	cmp	r0, #0
	beq	.L1796
	bl	FtlMakeBbt
.L1796:
	ldr	r0, .L1819+4
	mov	r2, #0
.L1797:
	ldrh	r1, [r4, #58]
	uxth	r3, r2
	add	r2, r2, #1
	cmp	r3, r1, lsl #7
	blt	.L1798
	ldrh	r6, [r4, #40]
	mov	r5, #0
.L1799:
	ldrh	r3, [r4, #42]
	cmp	r3, r6
	bhi	.L1800
	ldrh	r1, [r4, #32]
	sub	r3, r5, #3
	cmp	r3, r1, lsl #1
	bge	.L1801
.L1805:
	mov	r0, #0
	mov	r6, r0
.L1802:
	ldrh	r2, [r4, #40]
	uxth	r3, r0
	add	r5, r0, #1
	cmp	r2, r3
	bhi	.L1806
	ldrh	r3, [r4, #42]
	movw	r9, #2572
	ldrh	r5, [r4, #32]
	ldr	r10, [r4, #44]
	str	r3, [r4, #3284]
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
	ble	.L1807
	mov	r1, r5
	sub	r0, r10, r6
	bl	__udivsi3
	str	r0, [r4, #2504]
	lsr	r0, r0, #5
	add	r0, r0, #24
	strh	r0, [r4, r9]	@ movhi
.L1807:
	ldrh	r3, [r4, #94]
	cmp	r3, #0
	beq	.L1809
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
.L1809:
	movw	r3, #2572
	mvn	r6, #0
	ldrh	r2, [r4, r3]
	ldr	r3, [r4, #2504]
	sub	r3, r3, r2
	mul	r5, r5, r3
	ldrh	r3, [r4, #102]
	str	r5, [r4, #3472]
	mul	r5, r5, r3
	ldrh	r3, [r4, #58]
	str	r5, [r4, #2504]
	mul	r5, r5, r3
	str	r5, [r4, #140]
	bl	FtlBbmTblFlush
	ldrh	r2, [r4, #42]
	mov	r1, #0
	ldr	r0, [r4, #2324]
	ldr	r5, .L1819+8
	lsl	r2, r2, #1
	sub	r5, r5, #240
	bl	ftl_memset
	movw	r2, #2588
	mov	r3, #0
	strh	r6, [r4, r2]	@ movhi
	movw	r2, #2348
	strh	r3, [r4, r2]	@ movhi
	mov	r1, #255
	ldrh	r2, [r4, #40]
	strh	r3, [r5, #242]	@ movhi
	mov	r7, r5
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
.L1811:
	mov	r0, r7
	bl	make_superblock
	ldrb	r3, [r4, #2355]	@ zero_extendqisi2
	cmp	r3, #0
	ldrh	r3, [r5]
	bne	.L1812
	ldr	r2, [r4, #2324]
	lsl	r3, r3, #1
	strh	r6, [r2, r3]	@ movhi
	ldrh	r3, [r5]
	add	r3, r3, #1
	strh	r3, [r5]	@ movhi
	b	.L1811
.L1798:
	ldr	ip, [r4, #3312]
	mvn	r1, r3
	orr	r1, r3, r1, lsl #16
	str	r1, [ip, r3, lsl #2]
	ldr	r1, [r4, #3316]
	str	r0, [r1, r3, lsl #2]
	b	.L1797
.L1800:
	mov	r0, r6
	mov	r1, #1
	bl	FtlLowFormatEraseBlock
	add	r6, r6, #1
	add	r5, r5, r0
	uxth	r5, r5
	uxth	r6, r6
	b	.L1799
.L1801:
	mov	r0, r5
	bl	__udivsi3
	ldr	r3, [r4, #132]
	add	r0, r0, r3
	uxth	r0, r0
	bl	FtlSysBlkNumInit
	ldrh	r0, [r4, #36]
	bl	FtlFreeSysBlkQueueInit
	ldrh	r5, [r4, #40]
.L1803:
	ldrh	r3, [r4, #42]
	cmp	r3, r5
	bls	.L1805
	mov	r0, r5
	mov	r1, #1
	add	r5, r5, #1
	bl	FtlLowFormatEraseBlock
	uxth	r5, r5
	b	.L1803
.L1806:
	mov	r1, #0
	uxth	r0, r0
	bl	FtlLowFormatEraseBlock
	add	r6, r6, r0
	mov	r0, r5
	uxth	r6, r6
	b	.L1802
.L1812:
	ldr	r2, [r4, #2540]
	lsl	r3, r3, #1
	ldrh	r1, [r5, #4]
	mvn	r6, #0
	str	r2, [r4, #2360]
	add	r2, r2, #1
	str	r2, [r4, #2540]
	ldr	r2, [r4, #2324]
	strh	r1, [r2, r3]	@ movhi
	mov	r3, #0
	ldr	r2, .L1819+12
	movw	r1, #2396
	strb	r3, [r4, #2402]
	strh	r3, [r2, #2]	@ movhi
	mov	r7, r2
	ldrh	r3, [r5]
	mov	r5, r2
	add	r3, r3, #1
	strh	r3, [r4, r1]	@ movhi
	mov	r3, #1
	strb	r3, [r4, #2404]
.L1813:
	mov	r0, r7
	bl	make_superblock
	ldrb	r3, [r4, #2403]	@ zero_extendqisi2
	cmp	r3, #0
	ldrh	r3, [r5]
	bne	.L1814
	ldr	r2, [r4, #2324]
	lsl	r3, r3, #1
	strh	r6, [r2, r3]	@ movhi
	ldrh	r3, [r5]
	add	r3, r3, #1
	strh	r3, [r5]	@ movhi
	b	.L1813
.L1814:
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
	ldr	r3, .L1819+16
	mov	r2, #0
	strh	r2, [r3, #2]	@ movhi
	ldr	r2, [r4, #3472]
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
	ldreq	r3, .L1819+20
	moveq	r2, #1
	streq	r2, [r3]
	ldmfd	sp, {r4, r5, r6, r7, r8, r9, r10, fp, sp, pc}
.L1820:
	.align	2
.L1819:
	.word	.LANCHOR0
	.word	168778952
	.word	.LANCHOR0+2588
	.word	.LANCHOR0+2396
	.word	.LANCHOR0+2576
	.word	.LANCHOR2
	.size	ftl_low_format, .-ftl_low_format
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
	ldr	r6, .L1886
	beq	.L1823
	ldr	r3, [r6, #3676]
	cmp	r3, #0
	bne	.L1824
.L1885:
	mov	r4, #0
	b	.L1822
.L1824:
	ldr	r3, [r6, #3680]
	ldr	r2, [r3]
	str	r3, [fp, #-112]
	ldr	r3, .L1886+4
	cmp	r2, r3
	bne	.L1826
	ldr	r3, [fp, #-112]
	mov	r2, #0
	ldrh	r1, [r6, #10]
	add	r0, r3, #262144
	movw	r3, #65535
.L1830:
	ldr	ip, [r0, #-4]
	cmp	ip, #0
	bne	.L1827
	ldr	ip, [fp, #-112]
	sub	r3, r3, #1
	ldr	ip, [ip, r2, lsl #2]
	add	r2, r2, #1
	cmp	r2, #4096
	movhi	r2, #0
	cmp	r3, #4096
	str	ip, [r0, #-4]!
	bne	.L1830
	mov	r3, #512
	b	.L1883
.L1827:
	add	r3, r3, #127
	lsr	r3, r3, #7
.L1883:
	str	r3, [fp, #-116]
	lsl	r1, r1, #2
	ldr	r3, [fp, #-116]
	uxth	r1, r1
	ldr	r4, .L1886
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
.L1853:
	mov	r1, #512
	ldr	r0, [r4, #3684]
	bl	__memzero
	ldr	r3, [fp, #-108]
	ldrh	r7, [r4, #10]
	mul	r8, r3, r7
	ldr	r3, [r4, #3260]
	cmp	r3, #0
	moveq	r10, #6
	beq	.L1831
	ldr	r3, [r4, #3264]
	cmp	r3, #0
	moveq	r10, #6
	movne	r10, #9
.L1831:
	ldr	r3, [r6, #3248]
	mov	r1, r8
	mov	r0, #0
	blx	r3
	ldr	r3, [fp, #-120]
	cmp	r3, #1
	beq	.L1832
	ldr	r3, [r6, #3248]
	add	r1, r7, r8
	mov	r0, #0
	blx	r3
.L1832:
	cmp	r10, #9
	movne	r5, #0
	bne	.L1833
	ldr	r5, [r6, #3684]
	mov	r1, #1024
	mov	r0, r5
	bl	__memzero
	ldr	r2, .L1886+8
	mov	r3, #12
	add	r1, r5, #11
	add	r0, r5, #23
	stm	r5, {r2, r3}
	mov	r3, #0
	mov	r2, #4
	str	r3, [r5, #12]
	strb	r3, [r5, #16]
	strb	r2, [r5, #17]
	strb	r3, [r5, #20]
	ldrh	r2, [r6, #10]
	strh	r3, [r5, #22]	@ movhi
	ldr	r3, .L1886+12
	strh	r2, [r5, #18]	@ movhi
	mov	r2, #16
	strb	r2, [r5, #21]
.L1834:
	lsr	r2, r3, #2
	ldrb	ip, [r1, #1]!	@ zero_extendqisi2
	add	r2, r2, r3, lsl #5
	cmp	r0, r1
	add	r2, r2, ip
	eor	r3, r3, r2
	bne	.L1834
	str	r3, [r5, #8]
.L1833:
	ldr	r3, [fp, #-120]
	mov	r9, r8
	mul	r3, r7, r3
	mov	r7, #0
	str	r3, [fp, #-136]
	ldr	r3, [fp, #-112]
	str	r3, [fp, #-128]
.L1835:
	ldr	r3, [fp, #-136]
	cmp	r7, r3
	beq	.L1842
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
	bne	.L1838
	ldr	r3, [r4, #3260]
	mov	r0, #70
	blx	r3
	sub	r3, fp, #104
	ldr	ip, [r4, #3252]
	mov	r2, r5
	mov	r1, r8
	mov	r0, #0
	blx	ip
	ldr	r2, [r4, #3260]
	str	r0, [fp, #-140]
	ldrb	r0, [r4, #22]	@ zero_extendqisi2
	blx	r2
	ldr	r3, [fp, #-140]
	cmn	r3, #1
	bne	.L1839
.L1842:
	ldrb	r3, [r4, #14]	@ zero_extendqisi2
	ldrh	r10, [r4, #10]
	ldr	r7, [r4, #3684]
	str	r3, [fp, #-128]
	ldr	r3, [fp, #-108]
	mul	r9, r3, r10
	ldr	r3, [r4, #3260]
	cmp	r3, #0
	moveq	r8, #6
	beq	.L1841
	ldr	r3, [r4, #3264]
	cmp	r3, #0
	moveq	r8, #6
	movne	r8, #9
.L1841:
	ldr	r3, [fp, #-120]
	mov	r5, #0
	mul	r10, r10, r3
.L1844:
	cmp	r5, r10
	beq	.L1848
	cmp	r5, #0
	cmpeq	r8, #9
	moveq	r0, #1
	movne	r0, #0
	bne	.L1845
	ldr	r3, [r4, #3260]
	mov	r0, #70
	blx	r3
	ldr	r3, [r4, #3264]
	mov	r0, #2
	blx	r3
	mov	r2, r7
	ldr	ip, [r4, #3256]
	mov	r1, r9
	sub	r3, fp, #104
	mov	r0, #0
	blx	ip
	ldr	r3, [r4, #3264]
	ldr	r0, [fp, #-128]
	blx	r3
	ldr	r3, [r4, #3260]
	ldrb	r0, [r4, #22]	@ zero_extendqisi2
	blx	r3
	ldr	r3, [r7]
	ldr	r2, .L1886+8
	cmp	r3, r2
	beq	.L1846
.L1848:
	ldr	r0, [r4, #3684]
	mov	r5, #0
	ldr	r3, [fp, #-112]
.L1847:
	mov	r8, r3
	ldr	r1, [r0, r5, lsl #2]
	ldr	r2, [r8]
	lsl	r7, r5, #2
	add	r3, r3, #4
	cmp	r1, r2
	beq	.L1850
	mov	r1, #512
	bl	__memzero
	ldr	r2, [r6, #3684]
	ldr	r1, [fp, #-108]
	str	r5, [sp]
	ldr	r0, .L1886+16
	ldr	r3, [r8]
	ldr	r2, [r2, r7]
	bl	sftl_printk
	ldrh	r1, [r6, #10]
	mov	r0, #0
	ldr	r2, [fp, #-108]
	ldr	r3, [r6, #3248]
	mul	r1, r2, r1
	blx	r3
.L1851:
	ldr	r3, [fp, #-108]
	ldr	r2, [fp, #-120]
	add	r3, r3, r2
	cmp	r3, #15
	str	r3, [fp, #-108]
	bls	.L1853
	ldr	r3, [fp, #-124]
	cmp	r3, #0
	bne	.L1854
.L1826:
	mvn	r3, #0
	str	r3, [fp, #-116]
.L1854:
	ldr	r5, .L1886
	mov	r3, #0
	ldr	r4, [fp, #-116]
	ldr	r0, [r5, #3680]
	str	r3, [r5, #3676]
	bl	kfree
	ldr	r0, [r5, #3684]
	bl	kfree
.L1822:
	mov	r0, r4
	sub	sp, fp, #40
	ldmfd	sp, {r4, r5, r6, r7, r8, r9, r10, fp, sp, pc}
.L1838:
	ldr	ip, [r4, #3252]
	sub	r3, fp, #104
	ldr	r2, [fp, #-128]
	mov	r1, r9
	blx	ip
	cmn	r0, #1
	beq	.L1842
	ldr	r3, [fp, #-128]
	add	r3, r3, #2048
	str	r3, [fp, #-128]
.L1839:
	add	r7, r7, #1
	add	r9, r9, #1
	b	.L1835
.L1845:
	ldr	ip, [r4, #3256]
	sub	r3, fp, #104
	mov	r2, r7
	add	r1, r9, r5
	blx	ip
	cmn	r0, #1
	beq	.L1848
	ldr	r2, [fp, #-100]
	movw	r3, #61424
	cmp	r2, r3
	bne	.L1848
	add	r7, r7, #2048
.L1846:
	add	r5, r5, #1
	b	.L1844
.L1850:
	ldr	r2, [fp, #-132]
	add	r5, r5, #1
	cmp	r5, r2
	bne	.L1847
	ldr	r3, [fp, #-124]
	add	r3, r3, #1
	cmp	r3, #5
	str	r3, [fp, #-124]
	bls	.L1851
	b	.L1854
.L1823:
	cmp	r0, #64
	bne	.L1855
	mov	r0, #262144
	bl	ftl_malloc
	str	r0, [r6, #3680]
	mov	r0, #262144
	bl	ftl_malloc
	ldr	r3, [r6, #3680]
	str	r0, [r6, #3684]
	cmp	r0, #0
	cmpne	r3, #0
	beq	.L1856
	mov	r2, #1
	mov	r1, #262144
	mov	r0, r3
	str	r2, [r6, #3676]
	bl	__memzero
.L1855:
	ldr	r3, [r6, #3676]
	cmp	r3, #0
	beq	.L1885
	cmp	r5, #63
	ldr	r1, [r6, #3680]
	ldrhi	r6, .L1886+20
	rsbls	r5, r5, #64
	subls	r4, r4, r5
	addls	r7, r7, r5, lsl #9
	movls	r6, r1
	ldr	r0, .L1886+24
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
	b	.L1885
.L1856:
	ldr	r1, .L1886+28
	ldr	r0, .L1886+32
	bl	sftl_printk
	b	.L1855
.L1887:
	.align	2
.L1886:
	.word	.LANCHOR0
	.word	-52655045
	.word	1179535694
	.word	1204201446
	.word	.LC115
	.word	8388544
	.word	.LC117
	.word	.LANCHOR1+615
	.word	.LC116
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
	ldr	r2, .L1892
	ldr	r3, [r2, #3688]
	cmp	r3, #0
	streq	r0, [r2, #3688]
	moveq	r0, r3
	streq	r1, [r2, #3692]
	mvnne	r0, #0
	ldmfd	sp, {fp, sp, pc}
.L1893:
	.align	2
.L1892:
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
	ldr	r4, .L1903
	bl	ftl_malloc
	cmp	r0, #0
	str	r0, [r4, #3696]
	mvneq	r9, #11
	beq	.L1894
	ldr	r8, .L1903+4
	mov	r6, #0
	mov	r5, r6
	mov	r7, r6
.L1898:
	ldr	r3, [r4, #3688]
	mov	r1, #128
	ldr	r2, [r4, #3696]
	lsl	r0, r7, #7
	blx	r3
	subs	r9, r0, #0
	bne	.L1896
	ldr	r2, [r4, #3696]
	ldr	r3, [r2]
	cmp	r3, r8
	bne	.L1897
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
.L1897:
	add	r7, r7, #1
	cmp	r7, #2
	bne	.L1898
	cmp	r5, #0
	beq	.L1899
	ldr	r3, [r4, #3688]
	mov	r1, #128
	lsl	r0, r6, #7
	blx	r3
	subs	r9, r0, #0
	beq	.L1894
.L1896:
	ldr	r0, [r4, #3696]
	mvn	r9, #0
	bl	kfree
	mov	r3, #0
	str	r3, [r4, #3696]
	b	.L1894
.L1899:
	mov	r1, #65536
	mov	r0, r2
	bl	__memzero
	ldr	r3, [r4, #3696]
	mov	r2, #1
	add	r1, r3, #61440
	str	r2, [r3, #4]
	str	r8, [r3]
	str	r2, [r1, #4092]
	ldr	r2, .L1903+8
	strh	r5, [r3, #12]	@ movhi
	strh	r2, [r3, #14]	@ movhi
.L1894:
	mov	r0, r9
	ldmfd	sp, {r4, r5, r6, r7, r8, r9, fp, sp, pc}
.L1904:
	.align	2
.L1903:
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
	ldr	r3, .L1911
	mov	r4, r0
	mov	r0, r1
	ldr	ip, [r3, #3696]
	cmp	ip, #0
	ldrhne	lr, [ip, #10]
	movne	r3, #0
	bne	.L1907
.L1910:
	mvn	r0, #0
	ldmfd	sp, {r4, r5, fp, sp, pc}
.L1908:
	add	r3, r3, #1
.L1907:
	cmp	r3, lr
	bcs	.L1910
	add	r1, ip, r3, lsl #3
	ldrh	r5, [r1, #16]
	cmp	r5, r4
	bne	.L1908
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
.L1912:
	.align	2
.L1911:
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
	ldr	r8, .L1932
	mov	r9, r2
	ldr	r4, [r8, #3696]
	cmp	r4, #0
	beq	.L1928
	ldrh	r2, [r4, #10]
	add	r7, r9, #63
	ldrh	r3, [r4, #8]
	bic	r7, r7, #63
	mov	r5, #0
	str	r3, [fp, #-44]
.L1915:
	cmp	r5, r2
	bcc	.L1923
	ldrh	r1, [r4, #14]
	cmp	r7, r1
	bhi	.L1928
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
	ldr	r3, [r8, #3692]
	b	.L1931
.L1923:
	add	r6, r4, r5, lsl #3
	ldrh	r3, [r6, #16]
	cmp	r3, r0
	str	r3, [fp, #-48]
	bne	.L1916
	ldrh	r1, [r6, #20]
	add	r3, r4, #1024
	add	r1, r1, #63
	bic	r1, r1, #63
	cmp	r9, r1
	str	r1, [fp, #-52]
	bls	.L1917
	ldrh	r1, [r4, #14]
	cmp	r7, r1
	bhi	.L1928
	ldrh	r8, [r6, #18]
	sub	r2, r2, #1
	str	r2, [fp, #-56]
.L1918:
	ldr	r2, [fp, #-56]
	add	r6, r6, #8
	cmp	r5, r2
	bcc	.L1919
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
.L1920:
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
	ldr	r3, .L1932
	ldr	r3, [r3, #3692]
.L1931:
	ldr	r0, [fp, #-44]
	mov	r2, r4
	mov	r1, #128
	lsl	r0, r0, #7
	blx	r3
	mov	r0, #0
.L1913:
	sub	sp, fp, #40
	ldmfd	sp, {r4, r5, r6, r7, r8, r9, r10, fp, sp, pc}
.L1919:
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
	b	.L1918
.L1917:
	add	r5, r4, r5, lsl #3
	mov	r2, r9
	mov	r1, ip
	ldrh	r0, [r5, #18]
	add	r0, r3, r0
	bl	memcpy
	strh	r9, [r6, #20]	@ movhi
	b	.L1920
.L1916:
	add	r5, r5, #1
	b	.L1915
.L1928:
	mvn	r0, #0
	b	.L1913
.L1933:
	.align	2
.L1932:
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
	mov	r5, r2
	mov	r6, r1
	bl	ftl_malloc
	subs	r4, r0, #0
	mvneq	r5, #0
	beq	.L1934
	ldr	r3, .L1959
	cmp	r6, r3
	beq	.L1937
	add	r3, r3, #1
	cmp	r6, r3
	beq	.L1938
.L1954:
	mvn	r5, #13
	b	.L1936
.L1937:
	ldr	r6, .L1959+4
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
	bne	.L1944
	mov	r2, #8
	mov	r1, r5
	bl	arm_copy_from_user
	cmp	r0, #0
	bne	.L1954
	ldr	r2, [r4]
	ldr	r3, .L1959+8
	cmp	r2, r3
	beq	.L1942
.L1943:
	mvn	r5, #0
.L1936:
	mov	r0, r4
	bl	kfree
.L1934:
	mov	r0, r5
	ldmfd	sp, {r4, r5, r6, fp, sp, pc}
.L1942:
	ldrh	r2, [r4, #6]
	add	r1, r4, #8
	ldrh	r0, [r4, #4]
	bl	rk_sftl_vendor_read
	cmn	r0, #1
	beq	.L1943
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
	bne	.L1954
	mov	r0, r5
	mov	r1, r4
	bl	arm_copy_to_user
	subs	r5, r0, #0
	beq	.L1936
	b	.L1954
.L1938:
	ldr	r6, .L1959+4
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
	bne	.L1944
	mov	r2, #8
	mov	r1, r5
	bl	arm_copy_from_user
	cmp	r0, #0
	bne	.L1954
	ldr	r2, [r4]
	ldr	r3, .L1959+8
	cmp	r2, r3
	bne	.L1943
	ldrh	r2, [r4, #6]
	movw	r3, #4087
	cmp	r2, r3
	bhi	.L1943
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
	bne	.L1958
	mov	r1, r5
	mov	r0, r4
	bl	arm_copy_from_user
	cmp	r0, #0
	bne	.L1954
	ldrh	r2, [r4, #6]
	add	r1, r4, #8
	ldrh	r0, [r4, #4]
	bl	rk_sftl_vendor_write
	mov	r5, r0
	b	.L1936
.L1944:
	mov	r1, #8
.L1958:
	bl	__memzero
	b	.L1954
.L1960:
	.align	2
.L1959:
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
	ldr	r0, .L1962
	bl	misc_register
	ldmfd	sp, {fp, sp, pc}
.L1963:
	.align	2
.L1962:
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
	.type	__func__.3954, %object
	.size	__func__.3954, 17
__func__.3954:
	.ascii	"INSERT_DATA_LIST\000"
	.type	__func__.3949, %object
	.size	__func__.3949, 17
__func__.3949:
	.ascii	"INSERT_FREE_LIST\000"
	.type	__func__.3985, %object
	.size	__func__.3985, 17
__func__.3985:
	.ascii	"List_remove_node\000"
	.type	__func__.4017, %object
	.size	__func__.4017, 22
__func__.4017:
	.ascii	"List_update_data_list\000"
	.type	__func__.4124, %object
	.size	__func__.4124, 22
__func__.4124:
	.ascii	"select_l2p_ram_region\000"
	.type	__func__.4447, %object
	.size	__func__.4447, 16
__func__.4447:
	.ascii	"make_superblock\000"
	.type	__func__.4622, %object
	.size	__func__.4622, 19
__func__.4622:
	.ascii	"get_new_active_ppa\000"
	.type	__func__.15029, %object
	.size	__func__.15029, 17
__func__.15029:
	.ascii	"FlashEraseBlocks\000"
	.type	__func__.4059, %object
	.size	__func__.4059, 26
__func__.4059:
	.ascii	"ftl_map_blk_alloc_new_blk\000"
	.type	__func__.3564, %object
	.size	__func__.3564, 11
__func__.3564:
	.ascii	"FtlMemInit\000"
	.type	__func__.3791, %object
	.size	__func__.3791, 14
__func__.3791:
	.ascii	"FtlBbt2Bitmap\000"
	.type	__func__.4468, %object
	.size	__func__.4468, 18
__func__.4468:
	.ascii	"SupperBlkListInit\000"
	.type	__func__.14986, %object
	.size	__func__.14986, 15
__func__.14986:
	.ascii	"FlashReadPages\000"
	.type	__func__.4236, %object
	.size	__func__.4236, 14
__func__.4236:
	.ascii	"FtlScanSysBlk\000"
	.type	__func__.3834, %object
	.size	__func__.3834, 11
__func__.3834:
	.ascii	"FtlLoadBbt\000"
	.type	__func__.4293, %object
	.size	__func__.4293, 15
__func__.4293:
	.ascii	"FtlLoadSysInfo\000"
	.type	__func__.15005, %object
	.size	__func__.15005, 15
__func__.15005:
	.ascii	"FlashProgPages\000"
	.type	__func__.4085, %object
	.size	__func__.4085, 31
__func__.4085:
	.ascii	"Ftl_write_map_blk_to_last_page\000"
	.type	__func__.4099, %object
	.size	__func__.4099, 16
__func__.4099:
	.ascii	"FtlMapWritePage\000"
	.type	__func__.4026, %object
	.size	__func__.4026, 16
__func__.4026:
	.ascii	"load_l2p_region\000"
	.type	__func__.4070, %object
	.size	__func__.4070, 15
__func__.4070:
	.ascii	"ftl_map_blk_gc\000"
	.type	__func__.4141, %object
	.size	__func__.4141, 9
__func__.4141:
	.ascii	"log2phys\000"
	.type	__func__.4356, %object
	.size	__func__.4356, 16
__func__.4356:
	.ascii	"FtlReUsePrevPpa\000"
	.type	__func__.4509, %object
	.size	__func__.4509, 14
__func__.4509:
	.ascii	"ftl_check_vpc\000"
	.type	__func__.4594, %object
	.size	__func__.4594, 25
__func__.4594:
	.ascii	"allocate_data_superblock\000"
	.type	__func__.4635, %object
	.size	__func__.4635, 16
__func__.4635:
	.ascii	"update_vpc_list\000"
	.type	__func__.4642, %object
	.size	__func__.4642, 20
__func__.4642:
	.ascii	"decrement_vpc_count\000"
	.type	__func__.4390, %object
	.size	__func__.4390, 22
__func__.4390:
	.ascii	"FtlRecoverySuperblock\000"
	.type	__func__.4214, %object
	.size	__func__.4214, 15
__func__.4214:
	.ascii	"FtlVpcTblFlush\000"
	.type	__func__.4493, %object
	.size	__func__.4493, 21
__func__.4493:
	.ascii	"FtlVpcCheckAndModify\000"
	.type	__func__.4615, %object
	.size	__func__.4615, 29
__func__.4615:
	.ascii	"allocate_new_data_superblock\000"
	.type	__func__.3688, %object
	.size	__func__.3688, 13
__func__.3688:
	.ascii	"FtlProgPages\000"
	.type	__func__.4712, %object
	.size	__func__.4712, 19
__func__.4712:
	.ascii	"FtlGcFreeTempBlock\000"
	.type	__func__.4823, %object
	.size	__func__.4823, 23
__func__.4823:
	.ascii	"rk_ftl_garbage_collect\000"
	.type	__func__.3716, %object
	.size	__func__.3716, 9
__func__.3716:
	.ascii	"FtlWrite\000"
	.type	__func__.15153, %object
	.size	__func__.15153, 14
__func__.15153:
	.ascii	"FtlWriteToIDB\000"
	.space	3
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
	.word	.LC118
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
	.type	g_MaxLbn, %object
	.size	g_MaxLbn, 4
g_MaxLbn:
	.space	4
	.type	gVendorBlkInfo, %object
	.size	gVendorBlkInfo, 44
gVendorBlkInfo:
	.space	44
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
	.ascii	"SFTL version: 5.0.49 20181030\000"
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
	.ascii	"%s: addr: %x is in id block!!!!!!!!!!\012\000"
.LC81:
	.ascii	"not free: w: d:\000"
.LC82:
	.ascii	"not free: w: s:\000"
.LC83:
	.ascii	"FtlFreeSysBlkQueueOut = %x, free count = %d, error\012"
	.ascii	"\000"
.LC84:
	.ascii	"FtlFreeSysBlkQueueOut = %x, free count = %d\012\000"
.LC85:
	.ascii	"%s error allocating memory. return -1\012\000"
.LC86:
	.ascii	"prog read error: = %x\012\000"
.LC87:
	.ascii	"prog read s error: = %x %x %x\012\000"
.LC88:
	.ascii	"prog read d error: = %x %x %x\012\000"
.LC89:
	.ascii	"FtlMapWritePage error = %x \012\000"
.LC90:
	.ascii	"FtlMapWritePage error = %x error count = %d\012\000"
.LC91:
	.ascii	"region_id = %x phyAddr = %x\012\000"
.LC92:
	.ascii	"spare:\000"
.LC93:
	.ascii	"map_ppn:\000"
.LC94:
	.ascii	"load_l2p_region refresh = %x phyAddr = %x\012\000"
.LC95:
	.ascii	"...%s enter...\012\000"
.LC96:
	.ascii	"FtlCheckVpc2 %x = %x  %x\012\000"
.LC97:
	.ascii	"free blk vpc error %x = %x  %x\012\000"
.LC98:
	.ascii	"ftl_scan_all_data = %x\012\000"
.LC99:
	.ascii	"scan lpa = %x ppa= %x\012\000"
.LC100:
	.ascii	"lba = %x,addr= %x,spare= %x %x %x %x data=%x %x\012"
	.ascii	"\000"
.LC101:
	.ascii	"FtlGcScanTempBlk Error ID %x %x!!!!!!! \012\000"
.LC102:
	.ascii	"FtlBbmTblFlush id=%x,page=%x,previd=%x cnt=%d\012\000"
.LC103:
	.ascii	"FtlBbmTblFlush error:%x\012\000"
.LC104:
	.ascii	"FtlBbmTblFlush error = %x error count = %d\012\000"
.LC105:
	.ascii	"FtlGcFreeBadSuperBlk 0x%x\012\000"
.LC106:
	.ascii	"decrement_vpc_count %x = %d\012\000"
.LC107:
	.ascii	"spuer block %x vpn is 0\012 \000"
.LC108:
	.ascii	"FtlVendorPartRead refresh = %x phyAddr = %x\012\000"
.LC109:
	.ascii	"no ect\000"
.LC110:
	.ascii	"FtlVpcTblFlush error = %x error count = %d\012\000"
.LC111:
	.ascii	"FtlCheckVpc %x = %x  %x\012\000"
.LC112:
	.ascii	"Ftlwrite decrement_vpc_count %x = %d\012\000"
.LC113:
	.ascii	"SWL %x, FSB = %x vpc= %x,ec=%x th=%x\012\000"
.LC114:
	.ascii	"FtlWrite: lpa error:%x %x\012\000"
.LC115:
	.ascii	"write_idblock fail! %x %x %x %x\012\000"
.LC116:
	.ascii	"%s idb buffer alloc fail\012\000"
.LC117:
	.ascii	"%p %x %p %x\012\000"
.LC118:
	.ascii	"vendor_storage\000"
