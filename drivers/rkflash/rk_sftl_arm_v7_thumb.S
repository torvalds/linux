/* SPDX-License-Identifier: GPL-2.0 */

/*
 * Copyright (c) 2018 Rockchip Electronics Co. Ltd.
 * date: 2018-07-19
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
	ldrh	r2, [r3, #32]
	ldrh	r1, [r3, #42]
	str	r0, [r3, #28]
	muls	r2, r0, r2
	subs	r0, r1, r0
	ldr	r1, [r3, #48]
	strh	r0, [r3, #40]	@ movhi
	movs	r0, #0
	str	r2, [r3, #36]
	subs	r2, r1, r2
	str	r2, [r3, #44]
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
	ldrh	r3, [fp, #14]
	ldrh	r2, [r0]
	ldr	r4, .L25
	cmp	r3, #4
	ldrh	r6, [r0, #2]
	ldrh	r0, [r0, #4]
	strh	r2, [r4, #52]	@ movhi
	strh	r6, [r4, #54]	@ movhi
	strh	r0, [r4, #56]	@ movhi
	bne	.L16
	ldrh	r3, [fp, #6]
	lsrs	r3, r3, #1
	strh	r3, [r4, #42]	@ movhi
	movs	r3, #8
	strh	r3, [r4, #58]	@ movhi
.L16:
	ldr	r1, .L25+4
	movs	r3, #0
.L17:
	strb	r3, [r3, r1]
	adds	r3, r3, #1
	cmp	r3, #32
	bne	.L17
	ldrh	r5, [r4, #42]
	smulbb	r6, r6, r0
	movs	r3, #5
	cmp	r2, #1
	strh	r3, [r4, #92]	@ movhi
	uxth	r6, r6
	mov	r3, #0
	smulbb	r0, r0, r5
	mov	r7, #640
	it	eq
	strheq	r2, [r4, #92]	@ movhi
	strh	r3, [r4, #94]	@ movhi
	uxth	r0, r0
	strh	r7, [r4, #96]	@ movhi
	strh	r6, [r4, #32]	@ movhi
	strh	r0, [r4, #98]	@ movhi
	bl	Ftl_log2
	ldrh	r9, [fp, #12]
	ldrh	r10, [r4, #58]
	strh	r0, [r4, #100]	@ movhi
	strh	r9, [r4, #102]	@ movhi
	smulbb	r3, r6, r9
	mov	r0, r10
	strh	r9, [r4, #104]	@ movhi
	strh	r3, [r4, #106]	@ movhi
	bl	Ftl_log2
	lsl	r3, r10, #9
	mov	r8, r0
	strh	r0, [r4, #108]	@ movhi
	mul	r1, r9, r10
	uxth	r3, r3
	mov	r0, #5120
	asr	r7, r7, r8
	strh	r3, [r4, #110]	@ movhi
	lsrs	r3, r3, #8
	add	r8, r8, #9
	strh	r3, [r4, #112]	@ movhi
	adds	r7, r7, #2
	ldrh	r3, [fp, #20]
	strh	r3, [r4, #114]	@ movhi
	mul	r3, r5, r6
	lsls	r5, r5, #6
	asr	r5, r5, r8
	str	r3, [r4, #48]
	mul	r3, r10, r3
	mul	r3, r9, r3
	asrs	r3, r3, #11
	str	r3, [r4, #116]
	bl	__aeabi_idiv
	uxth	r0, r0
	strh	r5, [r4, #124]	@ movhi
	uxth	r5, r5
	mov	r1, r6
	cmp	r0, #4
	strh	r7, [r4, #122]	@ movhi
	itet	ls
	movls	r3, #4
	strhhi	r0, [r4, #120]	@ movhi
	strhls	r3, [r4, #120]	@ movhi
	mul	r3, r6, r5
	ldrh	r0, [r4, #120]
	adds	r5, r5, #8
	str	r3, [r4, #128]
	bl	__aeabi_uidiv
	uxtah	r0, r5, r0
	cmp	r6, #1
	it	eq
	addeq	r0, r0, #4
	str	r0, [r4, #28]
	ldrh	r0, [r4, #28]
	bl	FtlSysBlkNumInit
	ldr	r3, [r4, #28]
	movs	r0, #0
	str	r0, [r4, #140]
	str	r3, [r4, #132]
	ldr	r3, [r4, #44]
	lsls	r2, r3, #2
	ldrh	r3, [r4, #102]
	muls	r3, r2, r3
	ldrh	r2, [r4, #108]
	adds	r2, r2, #9
	lsrs	r3, r3, r2
	adds	r3, r3, #2
	strh	r3, [r4, #136]	@ movhi
	movs	r3, #32
	strh	r3, [r4, #138]	@ movhi
	ldrh	r3, [r4, #120]
	adds	r3, r3, #3
	strh	r3, [r4, #120]	@ movhi
	ldr	r3, [r4, #128]
	adds	r3, r3, #3
	str	r3, [r4, #128]
	pop	{r3, r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L26:
	.align	2
.L25:
	.word	.LANCHOR0
	.word	.LANCHOR0+60
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
	ldr	r2, .L34
	ldrh	r3, [r2, #144]
	cbz	r3, .L33
	ldr	r3, [r2, #148]
	ldrh	r2, [r2, #120]
	add	r2, r3, r2, lsl #1
.L29:
	cmp	r3, r2
	bne	.L30
.L33:
	movs	r0, #0
	bx	lr
.L30:
	ldrh	r1, [r3], #2
	cmp	r0, r1
	bne	.L29
	movs	r0, #1
	bx	lr
.L35:
	.align	2
.L34:
	.word	.LANCHOR0
	.fnend
	.size	IsBlkInVendorPart, .-IsBlkInVendorPart
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
	ldr	r3, .L37
	ldr	r0, [r3, #140]
	bx	lr
.L38:
	.align	2
.L37:
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
	ldr	r4, .L40
	ldrh	r7, [r4, #98]
	mov	r1, r7
	bl	__aeabi_uidiv
	uxth	r6, r0
	mov	r1, r7
	mov	r0, r5
	bl	__aeabi_uidivmod
	add	r2, r4, r6, lsl #2
	uxth	r3, r1
	ldr	r2, [r2, #180]
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
	ldr	r0, .L40+4
	bl	printk
	ldrh	r3, [r4, #158]
	movs	r0, #0
	adds	r3, r3, #1
	strh	r3, [r4, #158]	@ movhi
	add	sp, sp, #12
	@ sp needed
	pop	{r4, r5, r6, r7, pc}
.L41:
	.align	2
.L40:
	.word	.LANCHOR0
	.word	.LC0
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
	ldr	r5, .L43
	ldrh	r6, [r5, #98]
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
	ldr	r3, [r5, #180]
	ldr	r0, [r3, r2, lsl #2]
	lsrs	r0, r0, r4
	and	r0, r0, #1
	pop	{r3, r4, r5, r6, r7, pc}
.L44:
	.align	2
.L43:
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
	ldr	r0, .L47
	movw	r3, #65535
	movs	r2, #16
	movs	r1, #255
	strh	r3, [r0, #152]	@ movhi
	movs	r3, #0
	strh	r3, [r0, #158]	@ movhi
	adds	r0, r0, #164
	b	memset
.L48:
	.align	2
.L47:
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
	ldr	r3, .L56
	push	{r4, r5, r6, lr}
	.save {r4, r5, r6, lr}
	movs	r5, #0
	mov	r4, r5
	ldrh	r2, [r3, #98]
	ldrh	r6, [r3, #54]
	muls	r6, r2, r6
.L50:
	uxth	r0, r5
	cmp	r0, r6
	blt	.L52
	mov	r0, r4
	pop	{r4, r5, r6, pc}
.L52:
	bl	FtlBbmIsBadBlock
	cbz	r0, .L51
	adds	r4, r4, #1
	uxth	r4, r4
.L51:
	adds	r5, r5, #1
	b	.L50
.L57:
	.align	2
.L56:
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
	ldr	r4, .L59
	mov	r7, r0
	ldrh	r6, [r4, #56]
	mov	r1, r6
	bl	__aeabi_uidiv
	ldrh	r4, [r4, #98]
	smulbb	r5, r6, r5
	mov	r1, r6
	smulbb	r4, r4, r0
	mov	r0, r7
	bl	__aeabi_uidivmod
	adds	r0, r5, r1
	add	r0, r0, r4
	uxth	r0, r0
	pop	{r3, r4, r5, r6, r7, pc}
.L60:
	.align	2
.L59:
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
	ldr	r3, .L62
	push	{r4, r5, r6, lr}
	.save {r4, r5, r6, lr}
	mov	r6, r0
	ldrh	r5, [r3, #56]
	ldrh	r1, [r3, #98]
	bl	__aeabi_uidiv
	mov	r1, r5
	smulbb	r4, r0, r5
	mov	r0, r6
	bl	__aeabi_uidivmod
	add	r1, r1, r4
	uxth	r0, r1
	pop	{r4, r5, r6, pc}
.L63:
	.align	2
.L62:
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
	ldr	r4, .L65
	ldrh	r1, [r4, #98]
	bl	__aeabi_uidivmod
	uxth	r0, r1
	ldrh	r1, [r4, #56]
	bl	__aeabi_uidiv
	uxth	r0, r0
	pop	{r4, pc}
.L66:
	.align	2
.L65:
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
	bls	.L68
	subs	r0, r0, r1
	cmp	r0, #-2147483648
	ite	hi
	movhi	r0, #0
	movls	r0, #1
	bx	lr
.L68:
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
	ldr	r3, .L71
	mov	r1, #2048
	push	{r4, lr}
	.save {r4, lr}
	movs	r4, #0
	strh	r0, [r3, #212]	@ movhi
	add	r0, r3, #220
	strh	r4, [r3, #214]	@ movhi
	strh	r4, [r3, #216]	@ movhi
	strh	r4, [r3, #218]	@ movhi
	bl	__memzero
	mov	r0, r4
	pop	{r4, pc}
.L72:
	.align	2
.L71:
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
	ldr	r3, .L74
	ldrh	r0, [r3, #218]
	clz	r0, r0
	lsrs	r0, r0, #5
	bx	lr
.L75:
	.align	2
.L74:
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
	ldr	r3, .L77
	ldrh	r0, [r3, #218]
	sub	r3, r0, #1024
	rsbs	r0, r3, #0
	adcs	r0, r0, r3
	bx	lr
.L78:
	.align	2
.L77:
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
	ldr	r3, .L88
	push	{r4, r5, r6, lr}
	.save {r4, r5, r6, lr}
	ldrh	r2, [r3, #218]
	cbz	r2, .L79
	ldrh	r5, [r3, #2296]
	movs	r0, #0
	ldrh	r1, [r3, #214]
	mov	r6, r0
	ldrh	r2, [r3, #216]
	and	r5, r5, #31
.L81:
	uxth	r4, r0
	adds	r0, r0, #1
	cmp	r5, r4
	bgt	.L82
	cbz	r6, .L79
	strh	r1, [r3, #214]	@ movhi
	strh	r2, [r3, #216]	@ movhi
.L79:
	pop	{r4, r5, r6, pc}
.L82:
	add	r4, r3, r1, lsl #1
	adds	r1, r1, #1
	ubfx	r1, r1, #0, #10
	ldrh	r6, [r4, #220]
	add	r4, r3, r2, lsl #1
	strh	r6, [r4, #220]	@ movhi
	movs	r6, #1
	add	r2, r2, r6
	ubfx	r2, r2, #0, #10
	b	.L81
.L89:
	.align	2
.L88:
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
	ldr	r3, .L96
	push	{r4, r5, lr}
	.save {r4, r5, lr}
	ldrh	r4, [r3, #218]
	cmp	r4, #1024
	beq	.L94
	ldrh	r5, [r3, #214]
	movs	r1, #0
.L92:
	cmp	r1, r4
	bcc	.L93
.L94:
	movs	r0, #0
	pop	{r4, r5, pc}
.L93:
	adds	r2, r1, r5
	ubfx	r2, r2, #0, #10
	add	r2, r3, r2, lsl #1
	ldrh	r2, [r2, #220]
	cmp	r2, r0
	beq	.L95
	adds	r1, r1, #1
	b	.L92
.L95:
	movs	r0, #1
	pop	{r4, r5, pc}
.L97:
	.align	2
.L96:
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
	ldr	r2, .L113
	ldrh	r3, [r2, #40]
	mov	r5, r2
	cmp	r3, r0
	bls	.L100
	movs	r4, #6
	ldr	ip, [r2, #2316]
	muls	r4, r0, r4
	movw	r3, #65535
	add	r1, ip, r4
	strh	r3, [r1, #2]	@ movhi
	strh	r3, [ip, r4]	@ movhi
	ldr	r3, [r2, #2320]
	cbnz	r3, .L101
	str	r1, [r2, #2320]
.L100:
	movs	r0, #0
	add	sp, sp, #12
	@ sp needed
	pop	{r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L101:
	ldrh	r6, [r1, #4]
	lsl	r10, r0, #1
	ldr	r8, [r2, #2324]
	ldrh	r2, [r8, r0, lsl #1]
	cmp	r6, #0
	beq	.L111
	muls	r6, r2, r6
.L102:
	ldr	lr, [r5, #2316]
	ldrh	r7, [r5, #40]
	sub	r2, r3, lr
	asr	r9, r2, #1
	ldr	r2, .L113+4
	str	r7, [sp]
	mul	r2, r2, r9
	ldr	r9, [r5, #2328]
	add	r5, r9, r10
	uxth	r2, r2
	str	r5, [sp, #4]
	movs	r5, #0
.L109:
	adds	r5, r5, #1
	ldr	r7, [sp]
	uxth	r5, r5
	cmp	r5, r7
	bhi	.L100
	cmp	r0, r2
	beq	.L100
	ldrh	r7, [r3, #4]
	lsl	r10, r2, #1
	ldrh	fp, [r8, r2, lsl #1]
	cbz	r7, .L112
	mul	r7, r7, fp
.L104:
	cmp	r6, r7
	bne	.L105
	ldr	r7, [sp, #4]
	ldrh	r10, [r9, r10]
	ldrh	r7, [r7]
	cmp	r10, r7
	bcc	.L107
.L106:
	strh	r2, [ip, r4]	@ movhi
	ldr	r4, .L113
	ldrh	r2, [r3, #2]
	strh	r2, [r1, #2]	@ movhi
	ldr	r2, [r4, #2320]
	cmp	r3, r2
	ittte	ne
	ldrhne	r5, [r3, #2]
	movne	r2, #6
	ldrne	r1, [r4, #2316]
	strheq	r0, [r3, #2]	@ movhi
	iteee	eq
	streq	r1, [r4, #2320]
	mulne	r2, r2, r5
	strhne	r0, [r1, r2]	@ movhi
	strhne	r0, [r3, #2]	@ movhi
	b	.L100
.L111:
	mov	r6, #-1
	b	.L102
.L112:
	mov	r7, #-1
	b	.L104
.L105:
	bcc	.L106
.L107:
	ldrh	r7, [r3]
	movw	r10, #65535
	cmp	r7, r10
	bne	.L108
	strh	r2, [r1, #2]	@ movhi
	strh	r0, [r3]	@ movhi
	ldr	r3, .L113
	str	r1, [r3, #2332]
	b	.L100
.L108:
	movs	r3, #6
	mov	r2, r7
	mla	r3, r3, r7, lr
	b	.L109
.L114:
	.align	2
.L113:
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
	ldr	r2, .L117
	ldrh	r3, [r2, #2336]
	adds	r3, r3, #1
	uxth	r3, r3
	strh	r3, [r2, #2336]	@ movhi
	ldrh	r2, [r2, #40]
	cmp	r2, r3
	bcs	.L115
	movs	r2, #221
	ldr	r1, .L117+4
	ldr	r0, .L117+8
	pop	{r3, lr}
	b	printk
.L115:
	pop	{r3, pc}
.L118:
	.align	2
.L117:
	.word	.LANCHOR0
	.word	.LANCHOR1
	.word	.LC1
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
	beq	.L120
	ldr	r2, .L126
	movs	r1, #6
	mul	r7, r1, r0
	ldr	ip, [r2, #2316]
	mov	r5, r2
	add	r6, ip, r7
	strh	r4, [r6, #2]	@ movhi
	strh	r4, [ip, r7]	@ movhi
	ldr	r3, [r2, #2340]
	cbnz	r3, .L121
	str	r6, [r2, #2340]
.L120:
	movs	r0, #0
	pop	{r4, r5, r6, r7, r8, r9, r10, pc}
.L121:
	ldr	lr, [r2, #2316]
	ldr	r8, [r2, #2328]
	sub	r2, r3, lr
	asr	r10, r2, #1
	ldr	r2, .L126+4
	ldrh	r9, [r8, r0, lsl #1]
	mul	r2, r2, r10
	mov	r10, r4
	uxth	r2, r2
.L124:
	ldrh	r4, [r8, r2, lsl #1]
	cmp	r4, r9
	bcs	.L122
	ldrh	r4, [r3]
	cmp	r4, r10
	bne	.L123
	strh	r2, [r6, #2]	@ movhi
	strh	r0, [r3]	@ movhi
	b	.L120
.L123:
	mla	r3, r1, r4, lr
	mov	r2, r4
	b	.L124
.L122:
	ldrh	r1, [r3, #2]
	strh	r1, [r6, #2]	@ movhi
	strh	r2, [ip, r7]	@ movhi
	ldr	r2, [r5, #2340]
	cmp	r3, r2
	ittte	ne
	ldrhne	r4, [r3, #2]
	movne	r2, #6
	ldrne	r1, [r5, #2316]
	strheq	r0, [r3, #2]	@ movhi
	iteee	eq
	streq	r6, [r5, #2340]
	mulne	r2, r2, r4
	strhne	r0, [r1, r2]	@ movhi
	strhne	r0, [r3, #2]	@ movhi
	b	.L120
.L127:
	.align	2
.L126:
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
	ldr	r2, .L130
	ldrh	r3, [r2, #2344]
	adds	r3, r3, #1
	uxth	r3, r3
	strh	r3, [r2, #2344]	@ movhi
	ldrh	r2, [r2, #40]
	cmp	r2, r3
	bcs	.L128
	movs	r2, #214
	ldr	r1, .L130+4
	ldr	r0, .L130+8
	pop	{r3, lr}
	b	printk
.L128:
	pop	{r3, pc}
.L131:
	.align	2
.L130:
	.word	.LANCHOR0
	.word	.LANCHOR1+17
	.word	.LC1
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
	ldr	r4, .L138
	muls	r6, r1, r6
	movw	r3, #65535
	mov	r8, r0
	ldr	r7, [r4, #2316]
	adds	r5, r7, r6
	ldrh	r2, [r5, #2]
	cmp	r2, r3
	bne	.L133
	ldr	r3, [r0]
	cmp	r5, r3
	beq	.L133
	movw	r2, #379
	ldr	r1, .L138+4
	ldr	r0, .L138+8
	bl	printk
.L133:
	ldr	r3, [r8]
	movw	r1, #65535
	cmp	r5, r3
	ldrh	r3, [r7, r6]
	bne	.L134
	cmp	r3, r1
	ittee	ne
	ldrne	r0, [r4, #2316]
	movne	r2, #6
	moveq	r3, #0
	streq	r3, [r8]
	ittt	ne
	mlane	r3, r2, r3, r0
	strne	r3, [r8]
	strhne	r1, [r3, #2]	@ movhi
.L136:
	movw	r3, #65535
	movs	r0, #0
	strh	r3, [r7, r6]	@ movhi
	strh	r3, [r5, #2]	@ movhi
	pop	{r4, r5, r6, r7, r8, pc}
.L134:
	cmp	r3, r1
	ldrh	r1, [r5, #2]
	bne	.L137
	cmp	r1, r3
	beq	.L136
	movs	r2, #6
	ldr	r0, [r4, #2316]
	muls	r1, r2, r1
	strh	r3, [r0, r1]	@ movhi
	b	.L136
.L137:
	ldr	r0, [r4, #2316]
	movs	r2, #6
	mla	r3, r2, r3, r0
	strh	r1, [r3, #2]	@ movhi
	ldrh	r0, [r5, #2]
	ldrh	r1, [r7, r6]
	ldr	r3, [r4, #2316]
	muls	r2, r0, r2
	strh	r1, [r3, r2]	@ movhi
	b	.L136
.L139:
	.align	2
.L138:
	.word	.LANCHOR0
	.word	.LANCHOR1+34
	.word	.LC1
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
	cbz	r3, .L146
	ldr	r2, .L147
	movw	r5, #65535
	movs	r6, #6
	ldr	r2, [r2, #2316]
.L142:
	cbnz	r1, .L143
.L145:
	ldr	r4, .L147+4
	subs	r3, r3, r2
	asrs	r3, r3, #1
	muls	r4, r3, r4
	uxth	r1, r4
	bl	List_remove_node
	uxth	r0, r4
	pop	{r4, r5, r6, pc}
.L143:
	ldrh	r4, [r3]
	cmp	r4, r5
	beq	.L145
	subs	r1, r1, #1
	mla	r3, r6, r4, r2
	uxth	r1, r1
	b	.L142
.L146:
	movw	r0, #65535
	pop	{r4, r5, r6, pc}
.L148:
	.align	2
.L147:
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
	ldr	r2, .L156
	push	{r4, lr}
	.save {r4, lr}
	ldr	r3, [r2, #2320]
	cbz	r3, .L155
	ldr	r1, [r2, #2316]
	movs	r4, #6
	movw	r2, #65535
.L152:
	cbz	r0, .L153
	ldrh	r3, [r3]
	cmp	r3, r2
	bne	.L154
.L155:
	movw	r0, #65535
	pop	{r4, pc}
.L154:
	subs	r0, r0, #1
	mla	r3, r4, r3, r1
	uxth	r0, r0
	b	.L152
.L153:
	ldr	r0, .L156+4
	subs	r3, r3, r1
	asrs	r3, r3, #1
	muls	r3, r0, r3
	uxth	r0, r3
	pop	{r4, pc}
.L157:
	.align	2
.L156:
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
	ldr	r4, .L169
	ldrh	r3, [r4, #2348]
	cmp	r3, r0
	beq	.L160
	ldrh	r3, [r4, #2396]
	cmp	r3, r0
	beq	.L160
	ldrh	r3, [r4, #2444]
	cmp	r3, r0
	beq	.L160
	movs	r7, #6
	ldr	r9, [r4, #2316]
	muls	r7, r0, r7
	ldr	r3, [r4, #2320]
	add	r8, r9, r7
	cmp	r8, r3
	beq	.L160
	ldrh	r6, [r8, #4]
	ldr	r3, [r4, #2324]
	ldrh	r3, [r3, r0, lsl #1]
	cmp	r6, #0
	beq	.L167
	muls	r6, r3, r6
.L162:
	ldrh	r3, [r8, #2]
	movw	r2, #65535
	cmp	r3, r2
	bne	.L163
	ldrh	r2, [r9, r7]
	cmp	r2, r3
	bne	.L163
	mov	r2, #470
	ldr	r1, .L169+4
	ldr	r0, .L169+8
	bl	printk
.L163:
	ldrh	r3, [r8, #2]
	movw	r2, #65535
	cmp	r3, r2
	bne	.L164
	ldrh	r2, [r9, r7]
	cmp	r2, r3
	beq	.L160
.L164:
	movs	r2, #6
	muls	r2, r3, r2
	ldr	r3, .L169+12
	asrs	r1, r2, #1
	muls	r3, r1, r3
	ldr	r1, [r4, #2324]
	ldrh	r0, [r1, r3, lsl #1]
	ldr	r1, [r4, #2316]
	add	r2, r2, r1
	ldrh	r3, [r2, #4]
	cbz	r3, .L168
	muls	r3, r0, r3
.L165:
	cmp	r6, r3
	bcs	.L160
	mov	r1, r5
	ldr	r0, .L169+16
	bl	List_remove_node
	ldrh	r3, [r4, #2336]
	cbnz	r3, .L166
	movw	r2, #481
	ldr	r1, .L169+4
	ldr	r0, .L169+8
	bl	printk
.L166:
	ldrh	r3, [r4, #2336]
	mov	r0, r5
	subs	r3, r3, #1
	strh	r3, [r4, #2336]	@ movhi
	bl	INSERT_DATA_LIST
.L160:
	movs	r0, #0
	pop	{r3, r4, r5, r6, r7, r8, r9, pc}
.L167:
	mov	r6, #-1
	b	.L162
.L168:
	mov	r3, #-1
	b	.L165
.L170:
	.align	2
.L169:
	.word	.LANCHOR0
	.word	.LANCHOR1+51
	.word	.LC1
	.word	-1431655765
	.word	.LANCHOR0+2320
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
	ldr	r5, .L181
	movs	r0, #12
	movw	r6, #65535
	ldrh	r2, [r5, #138]
	ldr	r3, [r5, #2492]
.L172:
	uxth	r4, r1
	cmp	r4, r2
	bcc	.L174
	mov	r4, r2
	movs	r1, #0
	mov	r7, #-2147483648
	mov	ip, #12
.L175:
	uxth	r6, r1
	cmp	r6, r2
	bcc	.L177
	cmp	r4, r2
	bcc	.L173
	ldrh	r7, [r5, #2496]
	mov	r4, r2
	movs	r1, #0
	mov	r0, #-1
.L178:
	uxth	r5, r1
	cmp	r5, r2
	bcc	.L180
	cmp	r4, r2
	bcc	.L173
	mov	r2, #784
	ldr	r1, .L181+4
	ldr	r0, .L181+8
	bl	printk
	b	.L173
.L174:
	adds	r1, r1, #1
	mla	r7, r0, r1, r3
	ldrh	r7, [r7, #-12]
	cmp	r7, r6
	bne	.L172
.L173:
	mov	r0, r4
	pop	{r3, r4, r5, r6, r7, pc}
.L177:
	mla	r0, ip, r1, r3
	ldr	r0, [r0, #4]
	cmp	r0, #0
	blt	.L176
	cmp	r7, r0
	itt	hi
	movhi	r7, r0
	movhi	r4, r6
.L176:
	adds	r1, r1, #1
	b	.L175
.L180:
	ldr	r6, [r3, #4]
	cmp	r0, r6
	bls	.L179
	ldrh	ip, [r3]
	cmp	ip, r7
	itt	ne
	movne	r0, r6
	movne	r4, r5
.L179:
	adds	r1, r1, #1
	adds	r3, r3, #12
	b	.L178
.L182:
	.align	2
.L181:
	.word	.LANCHOR0
	.word	.LANCHOR1+73
	.word	.LC1
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
	ldr	r1, .L189
	push	{r4, r5, lr}
	.save {r4, r5, lr}
	mov	r3, r1
	ldrh	r2, [r1, #2498]
	cmp	r2, #4
	bhi	.L184
	cbnz	r0, .L184
	adds	r2, r2, #1
	strh	r2, [r1, #2498]	@ movhi
	pop	{r4, r5, pc}
.L184:
	movs	r2, #0
	ldrh	r1, [r3, #40]
	strh	r2, [r3, #2498]	@ movhi
	movw	r5, #65535
	str	r2, [r3, #2500]
	ldr	r2, [r3, #2324]
	add	r1, r2, r1, lsl #1
.L185:
	cmp	r2, r1
	bne	.L187
	pop	{r4, r5, pc}
.L187:
	ldrh	r4, [r2], #2
	cmp	r4, r5
	ittt	ne
	ldrne	r0, [r3, #2500]
	addne	r0, r0, r4
	strne	r0, [r3, #2500]
	b	.L185
.L190:
	.align	2
.L189:
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
	cbz	r1, .L192
	b	ftl_set_blk_mode.part.6
.L192:
	ldr	r2, .L193
	lsrs	r0, r0, #5
	and	r3, r3, #31
	ldr	r1, [r2, #24]
	movs	r2, #1
	lsl	r3, r2, r3
	ldr	r2, [r1, r0, lsl #2]
	bic	r2, r2, r3
	str	r2, [r1, r0, lsl #2]
	bx	lr
.L194:
	.align	2
.L193:
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
	ldr	r3, .L196
	lsrs	r2, r0, #5
	and	r0, r0, #31
	ldr	r3, [r3, #24]
	ldr	r3, [r3, r2, lsl #2]
	lsr	r0, r3, r0
	and	r0, r0, #1
	bx	lr
.L197:
	.align	2
.L196:
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
	ldr	r3, .L205
	adds	r4, r4, #14
	ldrh	r5, [r3, #32]
.L199:
	cmp	r2, r5
	bcc	.L201
	ldrh	r3, [r3, #102]
	add	r4, r0, #16
	movw	r6, #65535
	subs	r3, r3, #1
	subs	r1, r3, r1
	movs	r3, #0
	uxth	r1, r1
.L202:
	uxth	r2, r3
	cmp	r5, r2
	bhi	.L204
	pop	{r4, r5, r6, r7, pc}
.L201:
	ldrh	r6, [r4, #2]!
	adds	r2, r2, #1
	uxth	r2, r2
	cmp	r6, r7
	ittt	ne
	ldrhne	r6, [r0, #4]
	addne	r6, r6, #1
	strhne	r6, [r0, #4]	@ movhi
	b	.L199
.L204:
	ldrh	r2, [r4], #2
	adds	r3, r3, #1
	cmp	r2, r6
	ittt	ne
	ldrhne	r2, [r0, #4]
	addne	r2, r2, r1
	strhne	r2, [r0, #4]	@ movhi
	b	.L202
.L206:
	.align	2
.L205:
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
	cbz	r3, .L207
	ldrh	r2, [r0]
	movw	r3, #65535
	cmp	r2, r3
	beq	.L207
	ldrb	r2, [r0, #6]	@ zero_extendqisi2
	movs	r5, #0
	adds	r2, r2, #8
	ldrh	r1, [r0, r2, lsl #1]
	ldr	r2, .L213
	ldrh	r4, [r2, #32]
	mov	r2, r3
.L210:
	cmp	r1, r2
	beq	.L212
.L207:
	pop	{r4, r5, pc}
.L212:
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
	b	.L210
.L214:
	.align	2
.L213:
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
	ldrh	r3, [r6, #40]
	cmp	r2, r3
	bcc	.L216
	movw	r2, #2097
	ldr	r1, .L220+4
	ldr	r0, .L220+8
	bl	printk
.L216:
	ldrh	r9, [r6, #32]
	add	r7, r4, #16
	ldr	r10, .L220+12
	movw	r8, #65535
	movs	r5, #0
	strh	r5, [r4, #4]	@ movhi
	strb	r5, [r4, #7]
.L217:
	uxth	r3, r5
	cmp	r9, r3
	bhi	.L219
	ldrb	r3, [r4, #7]	@ zero_extendqisi2
	movs	r0, #0
	ldrh	r2, [r6, #102]
	smulbb	r3, r3, r2
	strh	r3, [r4, #4]	@ movhi
	movs	r3, #1
	strb	r3, [r4, #9]
	pop	{r3, r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L219:
	ldrh	r1, [r4]
	ldrb	r0, [r10, r5]	@ zero_extendqisi2
	bl	V2P_block
	strh	r8, [r7]	@ movhi
	mov	fp, r0
	bl	FtlBbmIsBadBlock
	cbnz	r0, .L218
	strh	fp, [r7]	@ movhi
	ldrb	r3, [r4, #7]	@ zero_extendqisi2
	adds	r3, r3, #1
	strb	r3, [r4, #7]
.L218:
	adds	r5, r5, #1
	adds	r7, r7, #2
	b	.L217
.L221:
	.align	2
.L220:
	.word	.LANCHOR0
	.word	.LANCHOR1+95
	.word	.LC1
	.word	.LANCHOR0+60
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
	ldrh	r8, [r6, #32]
	add	r10, r6, #60
	ldrh	r9, [r6, #102]
.L223:
	uxth	r3, r5
	cmp	r8, r3
	bhi	.L225
	cbz	r4, .L227
	mov	r1, r4
	mov	r0, #32768
	bl	__aeabi_idiv
.L226:
	ldr	r1, [r6, #2316]
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
	ldr	r0, [r2, #2340]
	cbz	r0, .L231
	ldr	r3, [r2, #2316]
	subs	r0, r0, r3
	ldr	r3, .L233+4
	asrs	r0, r0, #1
	muls	r0, r3, r0
	ldr	r3, [r2, #2328]
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
	ldr	r3, [r1, #2340]
	cbz	r3, .L241
	ldrh	r2, [r1, #2344]
	movs	r6, #6
	ldr	r4, [r1, #2316]
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
	ldr	r2, [r1, #2328]
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
	mov	r7, r0
	ldr	r6, .L257
	add	r5, r7, #12
	.pad #20
	sub	sp, sp, #20
	ldr	r1, .L257+4
	bl	strcpy
	ldr	r2, [r6, #116]
	mov	r0, r5
	ldr	r1, .L257+8
	bl	sprintf
	add	r5, r5, r0
	ldr	r1, .L257+12
	mov	r0, r5
	adds	r5, r5, #10
	bl	strcpy
	ldr	r2, [r6, #2504]
	mov	r0, r5
	ldr	r1, .L257+16
	bl	sprintf
	add	r5, r5, r0
	ldr	r2, [r6, #2500]
	ldr	r1, .L257+20
	mov	r0, r5
	bl	sprintf
	add	r5, r5, r0
	ldr	r2, [r6, #2508]
	ldr	r1, .L257+24
	mov	r0, r5
	bl	sprintf
	add	r5, r5, r0
	ldr	r2, [r6, #2512]
	ldr	r1, .L257+28
	mov	r0, r5
	bl	sprintf
	add	r5, r5, r0
	ldr	r2, [r6, #2516]
	ldr	r1, .L257+32
	mov	r0, r5
	bl	sprintf
	add	r5, r5, r0
	ldr	r2, [r6, #2520]
	ldr	r1, .L257+36
	mov	r0, r5
	bl	sprintf
	add	r5, r5, r0
	ldr	r2, [r6, #2524]
	ldr	r1, .L257+40
	mov	r0, r5
	bl	sprintf
	add	r5, r5, r0
	ldr	r2, [r6, #2528]
	ldr	r1, .L257+44
	mov	r0, r5
	bl	sprintf
	ldr	r2, [r6, #2532]
	add	r5, r5, r0
	ldr	r1, .L257+48
	mov	r0, r5
	lsrs	r2, r2, #11
	bl	sprintf
	ldr	r2, [r6, #2536]
	add	r5, r5, r0
	ldr	r1, .L257+52
	mov	r0, r5
	lsrs	r2, r2, #11
	bl	sprintf
	add	r5, r5, r0
	ldr	r2, [r6, #2540]
	ldr	r1, .L257+56
	mov	r0, r5
	bl	sprintf
	add	r5, r5, r0
	ldr	r2, [r6, #2544]
	ldr	r1, .L257+60
	mov	r0, r5
	bl	sprintf
	add	r5, r5, r0
	ldrh	r2, [r6, #158]
	ldr	r1, .L257+64
	mov	r0, r5
	bl	sprintf
	add	r5, r5, r0
	ldrh	r2, [r6, #2344]
	ldr	r1, .L257+68
	mov	r0, r5
	bl	sprintf
	add	r5, r5, r0
	ldr	r2, [r6, #2548]
	ldr	r1, .L257+72
	mov	r0, r5
	bl	sprintf
	add	r5, r5, r0
	ldr	r2, [r6, #2552]
	ldr	r1, .L257+76
	mov	r0, r5
	bl	sprintf
	add	r5, r5, r0
	ldr	r2, [r6, #2556]
	ldr	r1, .L257+80
	mov	r0, r5
	bl	sprintf
	add	r5, r5, r0
	ldr	r2, [r6, #2560]
	ldr	r1, .L257+84
	mov	r0, r5
	bl	sprintf
	add	r5, r5, r0
	ldr	r2, [r6, #2564]
	ldr	r1, .L257+88
	mov	r0, r5
	bl	sprintf
	add	r5, r5, r0
	ldr	r2, [r6, #2568]
	ldr	r1, .L257+92
	mov	r0, r5
	bl	sprintf
	add	r5, r5, r0
	ldrh	r2, [r6, #2298]
	ldr	r1, .L257+96
	mov	r0, r5
	bl	sprintf
	add	r5, r5, r0
	ldrh	r2, [r6, #2296]
	ldr	r1, .L257+100
	mov	r0, r5
	bl	sprintf
	add	r5, r5, r0
	ldr	r2, [r6, #140]
	ldr	r1, .L257+104
	mov	r0, r5
	bl	sprintf
	add	r5, r5, r0
	ldr	r2, [r6, #132]
	ldr	r1, .L257+108
	mov	r0, r5
	bl	sprintf
	add	r5, r5, r0
	ldr	r2, [r6, #28]
	ldr	r1, .L257+112
	mov	r0, r5
	bl	sprintf
	add	r5, r5, r0
	ldrh	r2, [r6, #218]
	ldr	r1, .L257+116
	mov	r0, r5
	bl	sprintf
	add	r5, r5, r0
	ldrh	r2, [r6, #40]
	ldr	r1, .L257+120
	mov	r0, r5
	bl	sprintf
	add	r5, r5, r0
	ldrh	r2, [r6, #2572]
	ldr	r1, .L257+124
	mov	r0, r5
	bl	sprintf
	add	r5, r5, r0
	ldr	r2, [r6, #44]
	ldr	r1, .L257+128
	mov	r0, r5
	bl	sprintf
	add	r5, r5, r0
	ldrh	r2, [r6, #2576]
	ldr	r1, .L257+132
	mov	r0, r5
	bl	sprintf
	add	r5, r5, r0
	ldrh	r2, [r6, #152]
	ldr	r1, .L257+136
	mov	r0, r5
	bl	sprintf
	add	r5, r5, r0
	ldrh	r2, [r6, #2350]
	ldr	r1, .L257+140
	mov	r0, r5
	bl	sprintf
	add	r5, r5, r0
	ldrb	r2, [r6, #2354]	@ zero_extendqisi2
	ldr	r1, .L257+144
	mov	r0, r5
	bl	sprintf
	add	r5, r5, r0
	ldrh	r2, [r6, #2348]
	ldr	r1, .L257+148
	mov	r0, r5
	bl	sprintf
	add	r5, r5, r0
	ldrb	r2, [r6, #2356]	@ zero_extendqisi2
	ldr	r1, .L257+152
	mov	r0, r5
	bl	sprintf
	add	r5, r5, r0
	ldrh	r2, [r6, #2352]
	ldr	r1, .L257+156
	mov	r0, r5
	bl	sprintf
	ldr	r3, [r6, #2324]
	add	r5, r5, r0
	ldrh	r2, [r6, #2348]
	mov	r0, r5
	ldr	r1, .L257+160
	ldrh	r2, [r3, r2, lsl #1]
	bl	sprintf
	add	r5, r5, r0
	ldrh	r2, [r6, #2398]
	ldr	r1, .L257+164
	mov	r0, r5
	bl	sprintf
	add	r5, r5, r0
	ldrb	r2, [r6, #2402]	@ zero_extendqisi2
	ldr	r1, .L257+168
	mov	r0, r5
	bl	sprintf
	add	r5, r5, r0
	ldrh	r2, [r6, #2396]
	ldr	r1, .L257+172
	mov	r0, r5
	bl	sprintf
	add	r5, r5, r0
	ldrb	r2, [r6, #2404]	@ zero_extendqisi2
	ldr	r1, .L257+176
	mov	r0, r5
	bl	sprintf
	add	r5, r5, r0
	ldrh	r2, [r6, #2400]
	ldr	r1, .L257+180
	mov	r0, r5
	bl	sprintf
	ldr	r3, [r6, #2324]
	add	r5, r5, r0
	ldrh	r2, [r6, #2396]
	mov	r0, r5
	ldr	r1, .L257+184
	ldrh	r2, [r3, r2, lsl #1]
	bl	sprintf
	add	r5, r5, r0
	ldrh	r2, [r6, #2446]
	ldr	r1, .L257+188
	mov	r0, r5
	bl	sprintf
	add	r5, r5, r0
	ldrb	r2, [r6, #2450]	@ zero_extendqisi2
	ldr	r1, .L257+192
	mov	r0, r5
	bl	sprintf
	add	r5, r5, r0
	ldrh	r2, [r6, #2444]
	ldr	r1, .L257+196
	mov	r0, r5
	bl	sprintf
	add	r5, r5, r0
	ldrb	r2, [r6, #2452]	@ zero_extendqisi2
	ldr	r1, .L257+200
	mov	r0, r5
	bl	sprintf
	add	r5, r5, r0
	ldrh	r2, [r6, #2448]
	ldr	r1, .L257+204
	mov	r0, r5
	bl	sprintf
	add	r5, r5, r0
	ldrh	r2, [r6, #2590]
	ldr	r1, .L257+208
	mov	r0, r5
	bl	sprintf
	add	r5, r5, r0
	ldrb	r2, [r6, #2594]	@ zero_extendqisi2
	ldr	r1, .L257+212
	mov	r0, r5
	bl	sprintf
	add	r5, r5, r0
	ldrh	r2, [r6, #2588]
	ldr	r1, .L257+216
	mov	r0, r5
	bl	sprintf
	add	r5, r5, r0
	ldrb	r2, [r6, #2596]	@ zero_extendqisi2
	ldr	r1, .L257+220
	mov	r0, r5
	bl	sprintf
	add	r5, r5, r0
	ldrh	r2, [r6, #2592]
	ldr	r1, .L257+224
	mov	r0, r5
	bl	sprintf
	ldr	r3, [r6, #2712]
	add	r5, r5, r0
	ldr	r2, [r6, #2716]
	mov	r0, r5
	ldr	r1, .L257+228
	str	r3, [sp]
	ldr	r3, [r6, #2720]
	bl	sprintf
	adds	r4, r5, r0
	ldr	r2, [r6, #2708]
	ldr	r1, .L257+232
	mov	r0, r4
	bl	sprintf
	add	r4, r4, r0
	ldr	r2, [r6, #2732]
	ldr	r1, .L257+236
	mov	r0, r4
	bl	sprintf
	add	r4, r4, r0
	ldrh	r2, [r6, #3148]
	ldr	r1, .L257+240
	mov	r0, r4
	bl	sprintf
	add	r4, r4, r0
	ldrh	r2, [r6, #3150]
	ldr	r1, .L257+244
	mov	r0, r4
	bl	sprintf
	add	r4, r4, r0
	ldr	r2, [r6, #3152]
	b	.L258
.L259:
	.align	2
.L257:
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
.L258:
	ldr	r1, .L260
	mov	r0, r4
	bl	sprintf
	add	r4, r4, r0
	ldrh	r2, [r6, #3156]
	ldr	r1, .L260+4
	mov	r0, r4
	bl	sprintf
	add	r4, r4, r0
	bl	GetFreeBlockMinEraseCount
	ldr	r1, .L260+8
	mov	r2, r0
	mov	r0, r4
	bl	sprintf
	add	r4, r4, r0
	ldrh	r0, [r6, #2344]
	bl	GetFreeBlockMaxEraseCount
	ldr	r1, .L260+12
	mov	r2, r0
	mov	r0, r4
	bl	sprintf
	ldr	r3, .L260+16
	add	r4, r4, r0
	ldr	r3, [r3]
	cmp	r3, #1
	beq	.L246
.L251:
	subs	r0, r4, r7
	add	sp, sp, #20
	@ sp needed
	pop	{r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L246:
	ldrh	r3, [r6, #2588]
	movw	r2, #65535
	cmp	r3, r2
	beq	.L248
	ldr	r2, [r6, #2324]
	mov	r0, r4
	ldr	r1, .L260+20
	ldrh	r2, [r2, r3, lsl #1]
	bl	sprintf
	add	r4, r4, r0
.L248:
	movs	r0, #0
	ldr	r9, .L260+28
	bl	List_get_gc_head_node
	uxth	r3, r0
	movs	r5, #0
	movw	fp, #65535
	mov	r10, #6
.L250:
	cmp	r3, fp
	beq	.L249
	ldr	r2, [r6, #2328]
	mul	r8, r10, r3
	mov	r0, r4
	mov	r1, r9
	ldrh	r2, [r2, r3, lsl #1]
	str	r2, [sp, #8]
	ldr	r2, [r6, #2316]
	add	r2, r2, r8
	ldrh	r2, [r2, #4]
	str	r2, [sp, #4]
	ldr	r2, [r6, #2324]
	ldrh	r2, [r2, r3, lsl #1]
	str	r2, [sp]
	mov	r2, r5
	bl	sprintf
	adds	r5, r5, #1
	ldr	r3, [r6, #2316]
	cmp	r5, #16
	add	r4, r4, r0
	ldrh	r3, [r3, r8]
	bne	.L250
.L249:
	ldr	r2, [r6, #2316]
	movs	r5, #0
	ldr	r3, [r6, #2340]
	movw	r9, #65535
	ldr	fp, .L260+32
	mov	r10, #6
	subs	r3, r3, r2
	asrs	r2, r3, #1
	ldr	r3, .L260+24
	muls	r3, r2, r3
	uxth	r3, r3
.L252:
	cmp	r3, r9
	beq	.L251
	ldr	r2, [r6, #2328]
	mul	r8, r10, r3
	mov	r0, r4
	mov	r1, fp
	ldrh	r2, [r2, r3, lsl #1]
	str	r2, [sp, #4]
	ldr	r2, [r6, #2316]
	add	r2, r2, r8
	ldrh	r2, [r2, #4]
	str	r2, [sp]
	mov	r2, r5
	adds	r5, r5, #1
	bl	sprintf
	cmp	r5, #4
	add	r4, r4, r0
	beq	.L251
	ldr	r3, [r6, #2316]
	ldrh	r3, [r3, r8]
	b	.L252
.L261:
	.align	2
.L260:
	.word	.LC63
	.word	.LC64
	.word	.LC65
	.word	.LC66
	.word	.LANCHOR2
	.word	.LC67
	.word	-1431655765
	.word	.LC68
	.word	.LC69
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
	push	{r3, r4, r5, lr}
	.save {r3, r4, r5, lr}
	mov	r5, r0
	ldr	r2, .L263
	ldr	r1, .L263+4
	bl	sprintf
	adds	r4, r5, r0
	mov	r0, r4
	bl	FtlPrintInfo2buf
	add	r0, r0, r4
	subs	r0, r0, r5
	pop	{r3, r4, r5, pc}
.L264:
	.align	2
.L263:
	.word	.LC70
	.word	.LC71
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
	ldr	r4, .L289
	ldr	r2, [r4, #2556]
	ldr	r3, [r4, #2568]
	cmp	r2, r3
	bcs	.L266
	ldrh	r1, [r4, #40]
	movs	r3, #0
	ldr	r0, [r4, #2328]
	str	r3, [r4, #2548]
.L267:
	cmp	r3, r1
	bcc	.L268
	ldr	r5, [r4, #2548]
	mov	r0, r5
	bl	__aeabi_uidiv
	str	r0, [r4, #2556]
	ldr	r0, [r4, #2552]
	ldrh	r1, [r4, #92]
	subs	r0, r5, r0
	bl	__aeabi_uidiv
	str	r0, [r4, #2548]
.L269:
	ldr	r6, [r4, #2568]
	ldr	r7, [r4, #2556]
	add	r3, r6, #256
	cmp	r3, r7
	bls	.L274
	ldr	r2, [r4, #2564]
	add	r3, r6, #768
	cmp	r3, r2
	bls	.L274
.L276:
	movw	r5, #65535
.L275:
	mov	r0, r5
	add	sp, sp, #28
	@ sp needed
	pop	{r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L268:
	ldrh	r2, [r0, r3, lsl #1]
	adds	r3, r3, #1
	ldr	r5, [r4, #2548]
	add	r2, r2, r5
	str	r2, [r4, #2548]
	b	.L267
.L266:
	ldr	r3, [r4, #2564]
	cmp	r2, r3
	bls	.L269
	adds	r3, r3, #1
	str	r3, [r4, #2564]
	movs	r3, #0
.L271:
	ldrh	r2, [r4, #40]
	cmp	r3, r2
	bcs	.L269
	ldr	r1, [r4, #2328]
	ldrh	r2, [r1, r3, lsl #1]
	adds	r2, r2, #1
	strh	r2, [r1, r3, lsl #1]	@ movhi
	adds	r3, r3, #1
	b	.L271
.L274:
	ldrh	r0, [r4, #2344]
	add	r0, r0, r0, lsl #1
	ubfx	r0, r0, #2, #16
	bl	GetFreeBlockMaxEraseCount
	add	r3, r6, #64
	mov	r9, r0
	cmp	r0, r3
	bcc	.L276
	ldr	r3, [r4, #2320]
	cmp	r3, #0
	beq	.L276
	ldrh	r2, [r4, #40]
	movw	r1, #65535
	ldr	lr, [r4, #2316]
	movs	r0, #0
	ldr	r8, [r4, #2328]
	mov	r10, r1
	str	r2, [sp, #20]
	mov	fp, #6
	mov	r2, r1
.L277:
	ldrh	ip, [r3]
	cmp	ip, r10
	bne	.L280
	mov	r5, r2
.L279:
	movw	r3, #65535
	cmp	r5, r3
	beq	.L276
	ldrh	r10, [r8, r5, lsl #1]
	lsl	fp, r5, #1
	cmp	r6, r10
	bcs	.L281
	bl	GetFreeBlockMinEraseCount
	cmp	r6, r0
	it	cc
	strcc	r1, [r4, #2568]
.L281:
	cmp	r7, r10
	bls	.L276
	add	r3, r10, #128
	cmp	r9, r3
	ble	.L276
	add	r3, r10, #256
	cmp	r7, r3
	bhi	.L282
	ldr	r3, [r4, #2564]
	add	r10, r10, #768
	cmp	r10, r3
	bcs	.L276
.L282:
	str	r9, [sp, #8]
	mov	r2, r7
	ldrh	r3, [r8, fp]
	mov	r1, r5
	ldr	r0, .L289+4
	str	r3, [sp, #4]
	ldr	r3, [r4, #2324]
	ldrh	r3, [r3, fp]
	str	r3, [sp]
	ldr	r3, [r4, #2564]
	bl	printk
	movs	r3, #1
	str	r3, [r4, #3160]
	b	.L275
.L280:
	adds	r0, r0, #1
	ldr	r5, [sp, #20]
	uxth	r0, r0
	cmp	r0, r5
	bhi	.L276
	ldrh	r5, [r3, #4]
	cbz	r5, .L278
	ldr	r5, .L289+8
	sub	r3, r3, lr
	asrs	r3, r3, #1
	muls	r3, r5, r3
	uxth	r5, r3
	ldrh	r3, [r8, r5, lsl #1]
	cmp	r6, r3
	bcs	.L279
	cmp	r1, r3
	itt	hi
	movhi	r1, r3
	movhi	r2, r5
.L278:
	mla	r3, fp, ip, lr
	b	.L277
.L290:
	.align	2
.L289:
	.word	.LANCHOR0
	.word	.LC72
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
	beq	.L292
	ldr	r2, .L293
	movs	r1, #0
	ldr	r2, [r2, #2324]
	strh	r1, [r2, r0, lsl #1]	@ movhi
	bl	INSERT_FREE_LIST
.L292:
	movs	r0, #0
	pop	{r3, pc}
.L294:
	.align	2
.L293:
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
	bne	.L296
	movw	r2, #2707
	ldr	r1, .L309
	ldr	r0, .L309+4
	bl	printk
.L296:
	ldr	r5, .L309+8
	ldrh	r2, [r4, #2]
	ldrh	r3, [r5, #102]
	cmp	r2, r3
	bne	.L297
	movw	r2, #2708
	ldr	r1, .L309
	ldr	r0, .L309+4
	bl	printk
.L297:
	ldrh	r3, [r4, #4]
	cbnz	r3, .L298
	movw	r2, #2709
	ldr	r1, .L309
	ldr	r0, .L309+4
	bl	printk
.L298:
	ldrb	r2, [r4, #6]	@ zero_extendqisi2
	movs	r3, #0
	ldrh	r1, [r5, #32]
	movw	r6, #65535
	strb	r3, [r4, #10]
	adds	r2, r2, #8
	ldrh	r0, [r4, r2, lsl #1]
	mov	r2, r3
.L299:
	cmp	r0, r6
	ldrb	r3, [r4, #6]	@ zero_extendqisi2
	beq	.L301
	ldrh	r2, [r4, #4]
	ldrh	r6, [r4, #2]
	subs	r2, r2, #1
	uxth	r2, r2
	orr	r6, r6, r0, lsl #10
	movw	r0, #65535
	strh	r2, [r4, #4]	@ movhi
.L303:
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
	beq	.L303
	strb	r3, [r4, #6]
	ldrh	r1, [r4, #2]
	ldrh	r3, [r5, #102]
	cmp	r1, r3
	bne	.L295
	cbz	r2, .L295
	movw	r2, #2730
	ldr	r1, .L309
	ldr	r0, .L309+4
	bl	printk
.L295:
	mov	r0, r6
	pop	{r3, r4, r5, r6, r7, pc}
.L301:
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
	b	.L299
.L310:
	.align	2
.L309:
	.word	.LANCHOR1+111
	.word	.LC1
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
	ldr	r3, .L320
	mov	ip, #20
	push	{r4, r5, r6, r7, r8, r9, lr}
	.save {r4, r5, r6, r7, r8, r9, lr}
	mov	r9, #12
	movs	r6, #1
	movs	r2, #0
	mov	r7, r3
	str	r2, [r3, #3164]
.L312:
	ldrh	r1, [r3, #32]
	uxth	r0, r2
	adds	r4, r2, #1
	cmp	r0, r1
	bcc	.L315
	ldr	r7, .L320
	mov	ip, #12
	movs	r6, #0
.L316:
	ldr	r2, [r3, #3184]
	cmp	r1, r2
	bcc	.L319
	pop	{r4, r5, r6, r7, r8, r9, pc}
.L315:
	uxth	r2, r2
	ldr	r8, [r3, #3168]
	mul	lr, r9, r2
	add	r1, r8, lr
	str	r6, [r1, #8]
	ldrh	r1, [r3, #110]
	muls	r1, r2, r1
	it	mi
	addmi	r1, r1, #3
	bic	r0, r1, #3
	ldr	r1, [r3, #3172]
	add	r1, r1, r0
	str	r1, [r8, lr]
	ldr	r5, [r7, #3176]
	ldr	r0, [r3, #3168]
	ldrh	r1, [r3, #112]
	muls	r1, r2, r1
	it	mi
	addmi	r1, r1, #3
	bic	r1, r1, #3
	add	r8, r0, lr
	add	r1, r1, r5
	str	r1, [r8, #4]
	ldr	r1, [r3, #3180]
	mla	r2, ip, r2, r1
	ldr	r1, [r0, lr]
	str	r1, [r2, #8]
	ldr	r1, [r8, #4]
	str	r1, [r2, #12]
	mov	r2, r4
	b	.L312
.L319:
	mul	r0, ip, r1
	ldr	r4, [r3, #3168]
	ldr	r5, [r3, #3172]
	adds	r2, r4, r0
	str	r6, [r2, #8]
	ldrh	r2, [r3, #110]
	muls	r2, r1, r2
	it	mi
	addmi	r2, r2, #3
	bic	r2, r2, #3
	add	r2, r2, r5
	str	r2, [r4, r0]
	ldr	r2, [r3, #3168]
	ldr	r4, [r7, #3176]
	add	r0, r0, r2
	ldrh	r2, [r3, #112]
	muls	r2, r1, r2
	it	mi
	addmi	r2, r2, #3
	bic	r2, r2, #3
	adds	r1, r1, #1
	add	r2, r2, r4
	uxth	r1, r1
	str	r2, [r0, #4]
	b	.L316
.L321:
	.align	2
.L320:
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
	ldr	r3, .L329
	push	{r4, r5, r6, r7, r8, r9, r10, lr}
	.save {r4, r5, r6, r7, r8, r9, r10, lr}
	movs	r5, #0
	mov	r10, #20
	mov	lr, #12
	mov	r8, r5
	ldr	ip, [r3, #3184]
	ldr	r6, [r3, #3168]
.L323:
	uxth	r3, r5
	cmp	r1, r3
	bls	.L322
	mla	r4, r10, r3, r0
	movs	r2, #0
.L324:
	uxth	r3, r2
	cmp	ip, r3
	bls	.L325
	mul	r3, lr, r3
	ldr	r7, [r4, #8]
	adds	r2, r2, #1
	add	r9, r6, r3
	ldr	r3, [r6, r3]
	cmp	r3, r7
	bne	.L324
	str	r8, [r9, #8]
.L325:
	adds	r5, r5, #1
	b	.L323
.L322:
	pop	{r4, r5, r6, r7, r8, r9, r10, pc}
.L330:
	.align	2
.L329:
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
	ldr	r3, .L338
	mov	ip, #12
	push	{r4, r5, r6, r7, r8, r9, lr}
	.save {r4, r5, r6, r7, r8, r9, lr}
	movs	r4, #0
	mov	lr, #1
	mov	r8, #20
	ldr	r5, [r3, #3184]
	ldr	r6, [r3, #3168]
.L332:
	uxth	r2, r4
	cmp	r1, r2
	bhi	.L336
	pop	{r4, r5, r6, r7, r8, r9, pc}
.L336:
	mov	r9, #0
.L333:
	uxth	r3, r9
	cmp	r5, r3
	bls	.L334
	mla	r3, ip, r3, r6
	add	r9, r9, #1
	ldr	r7, [r3, #8]
	cmp	r7, #0
	bne	.L333
	mla	r2, r8, r2, r0
	ldr	r7, [r3]
	str	lr, [r3, #8]
	str	r7, [r2, #8]
	ldr	r3, [r3, #4]
	str	r3, [r2, #12]
.L334:
	adds	r4, r4, #1
	b	.L332
.L339:
	.align	2
.L338:
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
	ldr	r2, .L345
	ldr	r3, [r2, #3188]
	ldrh	r2, [r2, #3192]
	add	r2, r3, r2, lsl #1
.L341:
	cmp	r3, r2
	bne	.L343
	movs	r0, #0
	bx	lr
.L343:
	ldrh	r1, [r3], #2
	cmp	r1, r0
	bne	.L341
	movs	r0, #1
	bx	lr
.L346:
	.align	2
.L345:
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
	ldr	r3, .L351
	ldrh	r1, [r3, #3192]
	ldr	r2, [r3, #3188]
.L348:
	uxth	ip, r4
	cmp	ip, r1
	bcc	.L350
	bne	.L349
	strh	r0, [r2, ip, lsl #1]	@ movhi
	ldrh	r0, [r3, #3192]
	adds	r0, r0, #1
	strh	r0, [r3, #3192]	@ movhi
	b	.L349
.L350:
	adds	r4, r4, #1
	add	ip, r2, r4, lsl #1
	ldrh	ip, [ip, #-2]
	cmp	ip, r0
	bne	.L348
.L349:
	ldrh	r2, [r3, #3200]
	movs	r0, #12
	muls	r0, r2, r0
	ldr	r2, [r3, #3196]
	adds	r1, r2, r0
	str	r6, [r1, #4]
	str	r7, [r1, #8]
	str	r5, [r2, r0]
	ldrh	r2, [r3, #3200]
	adds	r2, r2, #1
	strh	r2, [r3, #3200]	@ movhi
	pop	{r3, r4, r5, r6, r7, pc}
.L352:
	.align	2
.L351:
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
	ldr	r0, .L356
	bl	printk
	ldr	r3, .L356+4
	ldrh	r0, [r3, #3202]
	cmp	r4, r0
	beq	.L354
	ldrh	r1, [r3, #3204]
	cmp	r4, r1
	beq	.L354
	movw	r2, #65535
	cmp	r0, r2
	bne	.L355
	strh	r4, [r3, #3202]	@ movhi
.L354:
	movs	r0, #0
	pop	{r4, pc}
.L355:
	cmp	r1, r2
	it	eq
	strheq	r4, [r3, #3204]	@ movhi
	b	.L354
.L357:
	.align	2
.L356:
	.word	.LC73
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
	ldr	r4, .L362
	bl	P2V_block_in_plane
	mov	r2, r5
	mov	r6, r0
	ldrh	r1, [r4, #3206]
	ldr	r0, .L362+4
	bl	printk
	mov	r0, r6
	bl	FtlGcRefreshBlock
	ldrh	r3, [r4, #3206]
	movs	r2, #0
	addw	r0, r4, #3208
.L359:
	uxth	r1, r2
	cmp	r3, r1
	bhi	.L361
	cmp	r3, #15
	itttt	ls
	addls	r2, r3, #1
	strhls	r2, [r4, #3206]	@ movhi
	addls	r4, r4, r3, lsl #1
	strhls	r5, [r4, #3208]	@ movhi
	b	.L360
.L361:
	adds	r2, r2, #1
	add	r1, r0, r2, lsl #1
	ldrh	r1, [r1, #-2]
	cmp	r1, r5
	bne	.L359
.L360:
	movs	r0, #0
	pop	{r4, r5, r6, pc}
.L363:
	.align	2
.L362:
	.word	.LANCHOR0
	.word	.LC74
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
	ldr	r4, .L370
	ldrh	r3, [r4, #3206]
	cbz	r3, .L365
	ldrh	r1, [r4, #3202]
	movw	r2, #65535
	cmp	r1, r2
	bne	.L365
	ldrh	r2, [r4, #3242]
	cmp	r2, r3
	itt	cs
	movcs	r3, #0
	strhcs	r3, [r4, #3242]	@ movhi
	ldrh	r3, [r4, #3242]
	add	r3, r4, r3, lsl #1
	ldrh	r0, [r3, #3208]
	bl	P2V_block_in_plane
	bl	FtlGcRefreshBlock
	ldrh	r3, [r4, #3242]
	adds	r3, r3, #1
	strh	r3, [r4, #3242]	@ movhi
.L365:
	movs	r0, #0
	pop	{r4, pc}
.L371:
	.align	2
.L370:
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
	ldr	r1, .L373
	b	__kmalloc
.L374:
	.align	2
.L373:
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
	push	{r0, r1, r2, r3, r4, lr}
	.save {r4, lr}
	.pad #16
	muls	r3, r2, r3
	movs	r4, #0
	str	r1, [sp, #4]
	mov	r1, r0
	str	r4, [sp, #12]
	str	r3, [sp, #8]
	movs	r3, #16
	str	r2, [sp]
	movs	r2, #2
	ldr	r0, .L377
	bl	print_hex_dump
	add	sp, sp, #16
	@ sp needed
	pop	{r4, pc}
.L378:
	.align	2
.L377:
	.word	.LC75
	.fnend
	.size	rknand_print_hex, .-rknand_print_hex
	.align	1
	.global	FlashReadPages
	.syntax unified
	.thumb
	.thumb_func
	.fpu softvfp
	.type	FlashReadPages, %function
FlashReadPages:
	.fnstart
	@ args = 0, pretend = 0, frame = 8
	@ frame_needed = 0, uses_anonymous_args = 0
	push	{r0, r1, r2, r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.save {r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.pad #12
	mov	r8, r1
	ldr	r5, .L397
	mov	r4, r0
	movs	r6, #0
	ldr	r10, .L397+4
	ldrh	r9, [r5, #12]
	ldr	fp, .L397+8
.L380:
	cmp	r6, r8
	bne	.L387
	movs	r0, #0
	add	sp, sp, #12
	@ sp needed
	pop	{r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L387:
	ldr	r3, [r4, #8]
	cbz	r3, .L381
	ldr	r3, [r4, #12]
	cbnz	r3, .L382
.L381:
	movs	r2, #76
	mov	r1, r10
	mov	r0, fp
	bl	printk
.L382:
	mov	r2, sp
	add	r1, sp, #4
	mov	r0, r4
	bl	l2p_addr_tran
	ldr	r3, [r4, #12]
	ldr	r2, [r4, #8]
	ldr	r1, [sp, #4]
	ldrb	r0, [sp]	@ zero_extendqisi2
	ldr	r7, [r5, #3256]
	blx	r7
	str	r0, [r4]
	ldrh	r3, [r5, #14]
	cmp	r3, #4
	bne	.L384
	ldr	r3, [r4, #12]
	ldr	r2, [r4, #8]
	ldr	r1, [sp, #4]
	adds	r3, r3, #8
	ldrb	r0, [sp]	@ zero_extendqisi2
	add	r2, r2, #2048
	ldr	r7, [r5, #3256]
	add	r1, r1, r9
	blx	r7
	adds	r0, r0, #1
	beq	.L385
	ldr	r3, [r4, #12]
	ldr	r2, [r3, #12]
	adds	r2, r2, #1
	bne	.L384
	ldr	r2, [r3, #8]
	adds	r2, r2, #1
	bne	.L384
	ldr	r3, [r3]
	adds	r3, r3, #1
	beq	.L384
.L385:
	mov	r3, #-1
	str	r3, [r4]
.L384:
	adds	r6, r6, #1
	adds	r4, r4, #20
	b	.L380
.L398:
	.align	2
.L397:
	.word	.LANCHOR0
	.word	.LANCHOR1+130
	.word	.LC1
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
	ldr	r4, .L409
	movw	r10, #61664
	ldr	r3, [r4, #3280]
	add	r7, r4, #162
	ldr	r8, [r4, #3284]
	addw	r9, r4, #3260
	str	r3, [r4, #3268]
	str	r8, [r4, #3272]
.L400:
	ldrh	r3, [r4, #54]
	cmp	r6, r3
	bcc	.L405
	movs	r0, #0
	pop	{r4, r5, r6, r7, r8, r9, r10, pc}
.L405:
	ldrh	r5, [r4, #98]
	movw	r3, #65535
	strh	r3, [r7, #2]!	@ movhi
.L402:
	ldrh	r3, [r4, #98]
	subs	r5, r5, #1
	uxth	r5, r5
	sub	r2, r3, #16
	cmp	r5, r2
	ble	.L403
	mla	r3, r6, r3, r5
	movs	r2, #1
	mov	r1, r2
	mov	r0, r9
	lsls	r3, r3, #10
	str	r3, [r4, #3264]
	bl	FlashReadPages
	ldr	r3, [r4, #3260]
	adds	r3, r3, #1
	beq	.L402
	ldrh	r3, [r8]
	cmp	r3, r10
	bne	.L402
	strh	r5, [r7]	@ movhi
.L403:
	adds	r6, r6, #1
	b	.L400
.L410:
	.align	2
.L409:
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
	ldr	r3, .L419
	cmp	r1, #1
	push	{r4, r5, r6, r7, r8, lr}
	.save {r4, r5, r6, r7, r8, lr}
	lsl	r7, r0, #10
	.pad #88
	sub	sp, sp, #88
	mov	r2, r1
	mov	r6, r1
	it	eq
	ldrheq	r4, [r3, #104]
	mov	r1, #1
	it	ne
	ldrhne	r4, [r3, #102]
	add	r0, sp, #4
	ldr	r3, .L419+4
	subs	r4, r4, #1
	sxth	r5, r4
	str	r3, [sp, #12]
	add	r3, sp, #24
	str	r3, [sp, #16]
	orr	r3, r5, r7
	str	r3, [sp, #8]
	bl	FlashReadPages
	ldr	r3, [sp, #24]
	adds	r3, r3, #1
	bne	.L414
	mov	r8, #0
.L415:
	cmp	r8, r5
	ble	.L418
.L414:
	mov	r0, r5
	add	sp, sp, #88
	@ sp needed
	pop	{r4, r5, r6, r7, r8, pc}
.L418:
	add	r4, r8, r5
	mov	r2, r6
	add	r4, r4, r4, lsr #31
	movs	r1, #1
	add	r0, sp, #4
	asrs	r4, r4, #1
	sxth	r3, r4
	orrs	r3, r3, r7
	str	r3, [sp, #8]
	bl	FlashReadPages
	ldr	r3, [sp, #24]
	adds	r3, r3, #1
	bne	.L416
	ldr	r3, [sp, #28]
	adds	r3, r3, #1
	bne	.L416
	subs	r4, r4, #1
	sxth	r5, r4
	b	.L415
.L416:
	adds	r4, r4, #1
	sxth	r8, r4
	b	.L415
.L420:
	.align	2
.L419:
	.word	.LANCHOR0
	.word	ftl_temp_buf
	.fnend
	.size	FtlGetLastWrittenPage, .-FtlGetLastWrittenPage
	.align	1
	.global	FlashProgPages
	.syntax unified
	.thumb
	.thumb_func
	.fpu softvfp
	.type	FlashProgPages, %function
FlashProgPages:
	.fnstart
	@ args = 0, pretend = 0, frame = 40
	@ frame_needed = 0, uses_anonymous_args = 0
	push	{r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.save {r4, r5, r6, r7, r8, r9, r10, fp, lr}
	mov	fp, r1
	ldr	r6, .L451
	mov	r9, r2
	mov	r4, r0
	mov	r5, r0
	mov	r8, #0
	.pad #44
	sub	sp, sp, #44
	ldrh	r10, [r6, #12]
	str	r3, [sp, #4]
.L422:
	cmp	r8, fp
	bne	.L430
	ldr	r3, [sp, #4]
	cmp	r3, #0
	bne	.L437
.L450:
	movs	r0, #0
	add	sp, sp, #44
	@ sp needed
	pop	{r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L430:
	ldr	r3, [r5, #8]
	cbz	r3, .L423
	ldr	r3, [r5, #12]
	cbnz	r3, .L424
.L423:
	movs	r2, #106
	ldr	r1, .L451+4
	ldr	r0, .L451+8
	bl	printk
.L424:
	add	r2, sp, #12
	add	r1, sp, #16
	mov	r0, r5
	bl	l2p_addr_tran
	ldr	r3, [r5, #12]
	ldr	r2, [r5, #8]
	ldr	r1, [sp, #16]
	ldrb	r0, [sp, #12]	@ zero_extendqisi2
	ldr	r7, [r6, #3252]
	blx	r7
	cbnz	r0, .L425
	str	r0, [r5]
.L426:
	ldrh	r3, [r6, #14]
	cmp	r3, #4
	bne	.L428
	ldr	r3, [r5, #12]
	ldr	r2, [r5, #8]
	ldr	r1, [sp, #16]
	adds	r3, r3, #8
	ldrb	r0, [sp, #12]	@ zero_extendqisi2
	add	r2, r2, #2048
	ldr	r7, [r6, #3252]
	add	r1, r1, r10
	blx	r7
	cbz	r0, .L428
	mov	r3, #-1
	str	r3, [r5]
.L428:
	add	r8, r8, #1
	adds	r5, r5, #20
	b	.L422
.L425:
	mov	r3, #-1
	str	r3, [r5]
	b	.L426
.L435:
	movs	r3, #0
	mov	r2, r9
	str	r3, [r7]
	movs	r1, #1
	str	r3, [r6, #3288]
	add	r0, sp, #20
	ldr	r3, [r4, #4]
	str	r7, [sp, #28]
	str	r10, [sp, #32]
	str	r3, [sp, #24]
	bl	FlashReadPages
	ldr	r3, [sp, #20]
	adds	r2, r3, #1
	bne	.L432
	ldr	r1, [r4, #4]
	mov	r0, fp
	str	r3, [sp, #4]
	bl	printk
	ldr	r3, [sp, #4]
	str	r3, [r4]
.L432:
	ldr	r3, [r4, #12]
	cbz	r3, .L433
	ldr	r2, [r3]
	ldr	r3, [r6, #3288]
	cmp	r2, r3
	beq	.L433
	ldr	r1, [r4, #4]
	ldr	r0, .L451+12
	bl	printk
	mov	r3, #-1
	str	r3, [r4]
.L433:
	ldr	r3, [r4, #8]
	cbz	r3, .L434
	ldr	r2, [r3]
	ldr	r3, [r7]
	cmp	r2, r3
	beq	.L434
	ldr	r1, [r4, #4]
	ldr	r0, .L451+16
	bl	printk
	mov	r3, #-1
	str	r3, [r4]
.L434:
	adds	r5, r5, #1
	adds	r4, r4, #20
.L431:
	cmp	r8, r5
	bne	.L435
	b	.L450
.L437:
	movs	r5, #0
	ldr	r7, .L451+20
	ldr	r10, .L451+24
	ldr	fp, .L451+28
	b	.L431
.L452:
	.align	2
.L451:
	.word	.LANCHOR0
	.word	.LANCHOR1+145
	.word	.LC1
	.word	.LC77
	.word	.LC78
	.word	check_buf
	.word	.LANCHOR0+3288
	.word	.LC76
	.fnend
	.size	FlashProgPages, .-FlashProgPages
	.align	1
	.global	FlashEraseBlocks
	.syntax unified
	.thumb
	.thumb_func
	.fpu softvfp
	.type	FlashEraseBlocks, %function
FlashEraseBlocks:
	.fnstart
	@ args = 0, pretend = 0, frame = 8
	@ frame_needed = 0, uses_anonymous_args = 0
	push	{r0, r1, r2, r4, r5, r6, r7, r8, r9, lr}
	.save {r4, r5, r6, r7, r8, r9, lr}
	.pad #12
	mov	r7, r2
	ldr	r4, .L464
	mov	r5, r0
	movs	r6, #0
	mov	r9, #-1
	ldrh	r8, [r4, #12]
.L454:
	cmp	r6, r7
	bne	.L460
	movs	r0, #0
	add	sp, sp, #12
	@ sp needed
	pop	{r4, r5, r6, r7, r8, r9, pc}
.L460:
	mov	r2, sp
	add	r1, sp, #4
	mov	r0, r5
	bl	l2p_addr_tran
	ldr	r3, [r4, #3248]
	ldr	r1, [sp, #4]
	ldrb	r0, [sp]	@ zero_extendqisi2
	blx	r3
	cmp	r0, #0
	mov	r3, r0
	it	ne
	movne	r3, r9
	str	r3, [r5]
	ldrh	r3, [r4, #14]
	cmp	r3, #4
	bne	.L458
	ldr	r1, [sp, #4]
	ldr	r3, [r4, #3248]
	ldrb	r0, [sp]	@ zero_extendqisi2
	add	r1, r1, r8
	blx	r3
	cbz	r0, .L458
	str	r9, [r5]
.L458:
	adds	r6, r6, #1
	adds	r5, r5, #20
	b	.L454
.L465:
	.align	2
.L464:
	.word	.LANCHOR0
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
	bhi	.L466
	ldr	r4, .L475
	ldrh	r3, [r4, #218]
	cmp	r3, #1024
	beq	.L466
	cbz	r1, .L468
	bl	P2V_block_in_plane
	mov	r6, r0
	ldr	r0, [r4, #3800]
	lsls	r3, r5, #10
	movs	r2, #1
	mov	r1, r2
	str	r3, [r0, #4]
	bl	FlashEraseBlocks
	ldr	r2, [r4, #2328]
	ldrh	r3, [r2, r6, lsl #1]
	adds	r3, r3, #1
	strh	r3, [r2, r6, lsl #1]	@ movhi
	ldr	r3, [r4, #2560]
	adds	r3, r3, #1
	str	r3, [r4, #2560]
.L468:
	ldrh	r3, [r4, #218]
	adds	r3, r3, #1
	strh	r3, [r4, #218]	@ movhi
	ldrh	r3, [r4, #216]
	add	r2, r4, r3, lsl #1
	adds	r3, r3, #1
	ubfx	r3, r3, #0, #10
	strh	r5, [r2, #220]	@ movhi
	strh	r3, [r4, #216]	@ movhi
.L466:
	pop	{r4, r5, r6, pc}
.L476:
	.align	2
.L475:
	.word	.LANCHOR0
	.fnend
	.size	FtlFreeSysBlkQueueIn, .-FtlFreeSysBlkQueueIn
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
	ldr	r4, .L522
	mov	r7, r1
	mov	r6, r10
	mov	r5, r10
	mov	r9, #20
	.pad #20
	sub	sp, sp, #20
	mov	fp, r4
	str	r0, [sp]
	str	r0, [r4, #3804]
.L478:
	ldrh	r1, [r4, #32]
	uxth	r3, r10
	cmp	r1, r3
	bhi	.L483
	cmp	r6, #0
	beq	.L477
	mov	r8, #0
	mov	r9, #20
	mov	r2, r6
	movs	r1, #0
	ldr	r0, [r4, #3800]
	bl	FlashEraseBlocks
.L486:
	uxth	r3, r8
	cmp	r6, r3
	bhi	.L488
	cmp	r7, #0
	beq	.L505
	ldrh	r3, [r4, #104]
	mov	r10, #1
	str	r3, [sp, #4]
	lsrs	r3, r3, #2
	str	r3, [sp, #8]
.L489:
	mov	r8, #0
	mov	fp, #20
.L499:
	mov	r9, #0
	mov	r6, r9
.L490:
	ldrh	r1, [r4, #32]
	uxth	r3, r9
	cmp	r1, r3
	bhi	.L494
	cbz	r6, .L477
	mov	r9, #0
	movs	r3, #1
	mov	r2, r10
	mov	r1, r6
	ldr	r0, [r4, #3800]
	bl	FlashProgPages
.L496:
	uxth	r3, r9
	cmp	r6, r3
	bhi	.L498
	ldr	r3, [sp, #8]
	add	r8, r8, r3
	ldr	r3, [sp, #4]
	uxth	r8, r8
	cmp	r3, r8
	bhi	.L499
	mov	r8, #0
	mov	r9, #20
.L500:
	uxth	r3, r8
	cmp	r6, r3
	bhi	.L502
	ldr	r3, [sp]
	cmp	r3, #63
	bls	.L503
	cbz	r7, .L477
.L503:
	mov	r2, r6
	mov	r1, r10
	ldr	r0, [r4, #3800]
	bl	FlashEraseBlocks
.L477:
	mov	r0, r5
	add	sp, sp, #20
	@ sp needed
	pop	{r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L483:
	uxth	r3, r10
	ldr	r0, [r4, #3800]
	movs	r2, #0
	mul	r1, r9, r3
	add	r3, r3, r4
	str	r2, [r0, r1]
	ldr	r1, [sp]
	ldrb	r0, [r3, #60]	@ zero_extendqisi2
	bl	V2P_block
	mov	r8, r0
	cbz	r7, .L479
	bl	IsBlkInVendorPart
	cbnz	r0, .L480
.L479:
	mov	r0, r8
	bl	FtlBbmIsBadBlock
	cbnz	r0, .L481
	ldr	r1, [r4, #3800]
	lsl	r3, r8, #10
	ldr	r0, [fp, #3812]
	mla	r1, r9, r6, r1
	str	r3, [r1, #4]
	ldr	r3, [r4, #3808]
	str	r3, [r1, #8]
	ldrh	r3, [r4, #112]
	muls	r3, r6, r3
	add	r6, r6, #1
	it	mi
	addmi	r3, r3, #3
	uxth	r6, r6
	bic	r3, r3, #3
	add	r3, r3, r0
	str	r3, [r1, #12]
.L480:
	add	r10, r10, #1
	b	.L478
.L481:
	adds	r5, r5, #1
	uxth	r5, r5
	b	.L480
.L488:
	mul	r3, r9, r8
	ldr	r2, [r4, #3800]
	adds	r1, r2, r3
	ldr	r3, [r2, r3]
	adds	r3, r3, #1
	bne	.L487
	ldr	r0, [r1, #4]
	adds	r5, r5, #1
	uxth	r5, r5
	ubfx	r0, r0, #10, #16
	bl	FtlBbmMapBadBlock
.L487:
	add	r8, r8, #1
	b	.L486
.L505:
	movs	r3, #6
	mov	r10, r7
	str	r3, [sp, #8]
	movs	r3, #1
	str	r3, [sp, #4]
	b	.L489
.L494:
	uxth	r3, r9
	ldr	r0, [r4, #3800]
	movs	r2, #0
	mul	r1, fp, r3
	add	r3, r3, r4
	str	r2, [r0, r1]
	ldr	r1, [sp]
	ldrb	r0, [r3, #60]	@ zero_extendqisi2
	bl	V2P_block
	str	r0, [sp, #12]
	cbz	r7, .L491
	bl	IsBlkInVendorPart
	cbnz	r0, .L492
.L491:
	ldr	r0, [sp, #12]
	bl	FtlBbmIsBadBlock
	cbnz	r0, .L492
	ldr	r1, [r4, #3800]
	ldr	r3, [sp, #12]
	ldr	r2, .L522
	mla	r1, fp, r6, r1
	ldr	r0, [r2, #3808]
	add	r3, r8, r3, lsl #10
	str	r3, [r1, #4]
	ldr	r3, [r4, #3816]
	str	r3, [r1, #8]
	ldrh	r3, [r4, #112]
	muls	r3, r6, r3
	add	r6, r6, #1
	it	mi
	addmi	r3, r3, #3
	uxth	r6, r6
	bic	r3, r3, #3
	add	r3, r3, r0
	str	r3, [r1, #12]
.L492:
	add	r9, r9, #1
	b	.L490
.L498:
	mul	r3, fp, r9
	ldr	r2, [r4, #3800]
	adds	r1, r2, r3
	ldr	r3, [r2, r3]
	cbz	r3, .L497
	ldr	r0, [r1, #4]
	adds	r5, r5, #1
	uxth	r5, r5
	ubfx	r0, r0, #10, #16
	bl	FtlBbmMapBadBlock
.L497:
	add	r9, r9, #1
	b	.L496
.L502:
	cbz	r7, .L501
	mul	r3, r9, r8
	ldr	r2, [r4, #3800]
	adds	r1, r2, r3
	ldr	r3, [r2, r3]
	cbnz	r3, .L501
	ldr	r0, [r1, #4]
	movs	r1, #1
	ubfx	r0, r0, #10, #16
	bl	FtlFreeSysBlkQueueIn
.L501:
	add	r8, r8, #1
	b	.L500
.L523:
	.align	2
.L522:
	.word	.LANCHOR0
	.fnend
	.size	FtlLowFormatEraseBlock, .-FtlLowFormatEraseBlock
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
	push	{r3, r4, r5, r6, r7, lr}
	.save {r3, r4, r5, r6, r7, lr}
	ldr	r4, .L531
	ldr	r6, .L531+4
.L525:
	ldrh	r1, [r4, #218]
	cbz	r1, .L526
	ldrh	r3, [r4, #214]
	subs	r1, r1, #1
	strh	r1, [r4, #218]	@ movhi
	add	r2, r4, r3, lsl #1
	adds	r3, r3, #1
	ubfx	r3, r3, #0, #10
	ldrh	r5, [r2, #220]
	strh	r3, [r4, #214]	@ movhi
	mov	r0, r5
	bl	P2V_block_in_plane
	mov	r7, r0
	ldr	r0, [r4, #3800]
	lsls	r3, r5, #10
	movs	r2, #1
	mov	r1, r2
	str	r3, [r0, #4]
	bl	FlashEraseBlocks
	ldr	r2, [r4, #2328]
	ldrh	r3, [r2, r7, lsl #1]
	adds	r3, r3, #1
	strh	r3, [r2, r7, lsl #1]	@ movhi
	movw	r2, #65533
	ldr	r3, [r4, #2560]
	adds	r3, r3, #1
	str	r3, [r4, #2560]
	subs	r3, r5, #1
	uxth	r3, r3
	cmp	r3, r2
	bls	.L530
	ldrh	r2, [r4, #218]
	mov	r1, r5
	mov	r0, r6
	bl	printk
	b	.L525
.L526:
	ldr	r0, .L531+8
	bl	printk
.L529:
	b	.L529
.L530:
	mov	r0, r5
	pop	{r3, r4, r5, r6, r7, pc}
.L532:
	.align	2
.L531:
	.word	.LANCHOR0
	.word	.LC80
	.word	.LC79
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
.L534:
	uxth	r5, r3
	cmp	r5, r1
	bcs	.L537
	mov	r7, r2
	adds	r3, r3, #1
	ldrh	r6, [r7]
	adds	r2, r2, #2
	cmp	r6, #0
	bne	.L534
	bl	FtlFreeSysBlkQueueOut
	subs	r3, r0, #1
	movw	r2, #65533
	uxth	r3, r3
	mov	r1, r0
	strh	r0, [r7]	@ movhi
	cmp	r3, r2
	bls	.L535
	ldr	r3, .L541
	ldr	r0, .L541+4
	ldrh	r2, [r3, #218]
	bl	printk
.L536:
	b	.L536
.L535:
	ldr	r3, [r4, #28]
	strh	r6, [r4, #2]	@ movhi
	strh	r5, [r4]	@ movhi
	adds	r3, r3, #1
	str	r3, [r4, #28]
	ldrh	r3, [r4, #8]
	adds	r3, r3, #1
	strh	r3, [r4, #8]	@ movhi
.L537:
	ldrh	r3, [r4, #10]
	cmp	r3, r5
	bhi	.L539
	movw	r2, #581
	ldr	r1, .L541+8
	ldr	r0, .L541+12
	bl	printk
.L539:
	movs	r0, #0
	pop	{r3, r4, r5, r6, r7, pc}
.L542:
	.align	2
.L541:
	.word	.LANCHOR0
	.word	.LC81
	.word	.LANCHOR1+160
	.word	.LC1
	.fnend
	.size	ftl_map_blk_alloc_new_blk, .-ftl_map_blk_alloc_new_blk
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
	mov	r4, r0
	.pad #92
	sub	sp, sp, #92
	bls	.L545
	ldr	r5, .L546
	add	r0, sp, #24
	movs	r2, #32
	movs	r1, #165
	lsls	r4, r4, #10
	str	r0, [sp, #16]
	str	r5, [sp, #12]
	bl	memset
	movs	r2, #1
	mov	r3, #1515870810
	mov	r1, r2
	add	r0, sp, #4
	str	r3, [r5]
	str	r3, [r5, #4]
	str	r4, [sp, #8]
	bl	FlashEraseBlocks
	movs	r3, #1
	add	r0, sp, #4
	mov	r2, r3
	mov	r1, r3
	bl	FlashProgPages
	ldr	r4, [sp, #4]
	movs	r2, #1
	movs	r1, #0
	add	r0, sp, #4
	adds	r4, r4, #0
	it	ne
	movne	r4, #1
	negs	r4, r4
	bl	FlashEraseBlocks
.L543:
	mov	r0, r4
	add	sp, sp, #92
	@ sp needed
	pop	{r4, r5, pc}
.L545:
	movs	r4, #0
	b	.L543
.L547:
	.align	2
.L546:
	.word	ftl_temp_buf
	.fnend
	.size	FlashTestBlk, .-FlashTestBlk
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
	ldr	r5, .L555
	mov	r4, r0
	mov	r6, r1
	movs	r1, #255
	bl	memset
	ldr	r3, [r5, #3244]
	mov	r1, r6
	mov	r0, r4
	blx	r3
	uxth	r0, r0
	cmp	r0, #50
	bls	.L549
	mov	r2, #256
	movs	r1, #255
	mov	r0, r4
	bl	memset
	movs	r0, #0
.L549:
	ldrh	r3, [r5, #14]
	cmp	r3, #4
	bne	.L554
	add	r1, r4, r0, lsl #1
	mov	r3, r4
.L551:
	cmp	r3, r1
	bne	.L552
.L554:
	pop	{r4, r5, r6, pc}
.L552:
	ldrh	r2, [r3]
	lsrs	r2, r2, #1
	strh	r2, [r3], #2	@ movhi
	b	.L551
.L556:
	.align	2
.L555:
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
	cbz	r2, .L558
	bl	memset
.L558:
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
	ldr	r4, .L663
	movs	r5, #0
	movs	r6, #12
	movs	r7, #20
	ldrh	r0, [r4, #106]
	str	r3, [r4, #3824]
	strh	r3, [r4, #3202]	@ movhi
	strh	r3, [r4, #3204]	@ movhi
	movs	r3, #32
	strh	r3, [r4, #3148]	@ movhi
	lsls	r0, r0, #1
	movs	r3, #128
	str	r5, [r4, #2540]
	strh	r3, [r4, #3150]	@ movhi
	str	r5, [r4, #2544]
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
	strh	r5, [r4, #3156]	@ movhi
	strh	r5, [r4, #3206]	@ movhi
	strh	r5, [r4, #3242]	@ movhi
	bl	ftl_malloc
	str	r0, [r4, #3188]
	ldrh	r0, [r4, #106]
	muls	r0, r6, r0
	bl	ftl_malloc
	ldrh	r3, [r4, #32]
	str	r0, [r4, #3196]
	muls	r7, r3, r7
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
	lsls	r3, r3, #1
	mov	r0, r7
	adds	r3, r3, #1
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
	muls	r0, r7, r0
	bl	ftl_malloc
	str	r0, [r4, #3172]
	mov	r0, r7
	bl	ftl_malloc
	str	r0, [r4, #3816]
	mov	r0, r7
	bl	ftl_malloc
	str	r0, [r4, #3808]
	ldr	r0, [r4, #3184]
	muls	r0, r6, r0
	bl	ftl_malloc
	ldrh	r3, [r4, #112]
	ldrh	r7, [r4, #32]
	str	r0, [r4, #3168]
	muls	r7, r3, r7
	mov	r0, r7
	bl	ftl_malloc
	str	r0, [r4, #3284]
	lsls	r0, r7, #2
	bl	ftl_malloc
	ldrh	r3, [r4, #112]
	str	r0, [r4, #3812]
	ldr	r0, [r4, #3184]
	muls	r0, r3, r0
	bl	ftl_malloc
	str	r0, [r4, #3176]
	ldrh	r0, [r4, #42]
	lsls	r0, r0, #1
	uxth	r0, r0
	strh	r0, [r4, #3852]	@ movhi
	bl	ftl_malloc
	ldrh	r3, [r4, #3852]
	str	r0, [r4, #3856]
	ldr	r0, .L663+4
	addw	r3, r3, #547
	lsrs	r3, r3, #9
	and	r0, r0, r3, lsl #9
	strh	r3, [r4, #3852]	@ movhi
	bl	ftl_malloc
	ldrh	r7, [r4, #42]
	str	r0, [r4, #3860]
	adds	r0, r0, #32
	str	r0, [r4, #2328]
	lsls	r7, r7, #1
	mov	r0, r7
	bl	ftl_malloc
	str	r0, [r4, #3864]
	mov	r0, r7
	bl	ftl_malloc
	ldr	r7, [r4, #128]
	str	r0, [r4, #2324]
	lsls	r7, r7, #1
	mov	r0, r7
	bl	ftl_malloc
	str	r0, [r4, #3868]
	mov	r0, r7
	bl	ftl_malloc
	str	r0, [r4, #3872]
	ldrh	r0, [r4, #42]
	lsrs	r0, r0, #3
	adds	r0, r0, #4
	bl	ftl_malloc
	str	r0, [r4, #24]
	ldrh	r0, [r4, #120]
	lsls	r0, r0, #1
	bl	ftl_malloc
	str	r0, [r4, #148]
	ldrh	r0, [r4, #120]
	lsls	r0, r0, #1
	bl	ftl_malloc
	str	r0, [r4, #3876]
	ldrh	r0, [r4, #120]
	lsls	r0, r0, #2
	bl	ftl_malloc
	str	r0, [r4, #3880]
	ldrh	r0, [r4, #122]
	lsls	r0, r0, #2
	bl	ftl_malloc
	ldrh	r2, [r4, #122]
	mov	r1, r5
	str	r0, [r4, #3884]
	add	r5, r4, #180
	lsls	r2, r2, #2
	bl	ftl_memset
	ldrh	r0, [r4, #136]
	lsls	r0, r0, #2
	bl	ftl_malloc
	str	r0, [r4, #3888]
	ldr	r0, [r4, #128]
	lsls	r0, r0, #2
	bl	ftl_malloc
	str	r0, [r4, #3892]
	ldrh	r0, [r4, #138]
	muls	r0, r6, r0
	bl	ftl_malloc
	ldrh	r3, [r4, #138]
	str	r0, [r4, #2492]
	ldrh	r0, [r4, #110]
	muls	r0, r3, r0
	bl	ftl_malloc
	ldrh	r3, [r4, #42]
	str	r0, [r4, #3896]
	movs	r0, #6
	muls	r0, r3, r0
	bl	ftl_malloc
	str	r0, [r4, #2316]
	ldrh	r0, [r4, #98]
	ldrh	r3, [r4, #54]
	adds	r0, r0, #31
	asrs	r0, r0, #5
	strh	r0, [r4, #3900]	@ movhi
	muls	r0, r3, r0
	lsls	r0, r0, #2
	bl	ftl_malloc
	ldrh	r2, [r4, #3900]
	movs	r3, #1
	ldrh	r6, [r4, #54]
	str	r0, [r4, #180]
	lsls	r2, r2, #2
	mov	r1, r2
.L563:
	cmp	r3, r6
	bcc	.L564
	ldr	r2, .L663+8
	movs	r1, #0
	add	r3, r2, r3, lsl #2
	adds	r2, r2, #56
	adds	r3, r3, #24
.L565:
	cmp	r2, r3
	bne	.L566
	ldr	r3, [r4, #3868]
	cbnz	r3, .L567
.L569:
	ldr	r1, .L663+12
	ldr	r0, .L663+16
	bl	printk
	mov	r0, #-1
	pop	{r4, r5, r6, r7, r8, pc}
.L564:
	ldr	r0, [r4, #180]
	adds	r3, r3, #1
	add	r0, r0, r1
	add	r1, r1, r2
	str	r0, [r5, #4]!
	b	.L563
.L566:
	str	r1, [r3, #4]!
	b	.L565
.L664:
	.align	2
.L663:
	.word	.LANCHOR0
	.word	33553920
	.word	.LANCHOR0+152
	.word	.LANCHOR1+186
	.word	.LC82
.L567:
	ldr	r3, [r4, #3872]
	cmp	r3, #0
	beq	.L569
	ldr	r3, [r4, #3888]
	cmp	r3, #0
	beq	.L569
	ldr	r3, [r4, #3892]
	cmp	r3, #0
	beq	.L569
	ldr	r3, [r4, #2492]
	cmp	r3, #0
	beq	.L569
	ldr	r3, [r4, #3896]
	cmp	r3, #0
	beq	.L569
	ldr	r3, [r4, #2316]
	cmp	r3, #0
	beq	.L569
	ldr	r3, [r4, #180]
	cmp	r3, #0
	beq	.L569
	ldr	r3, [r4, #2324]
	cmp	r3, #0
	beq	.L569
	ldr	r3, [r4, #3188]
	cmp	r3, #0
	beq	.L569
	ldr	r3, [r4, #3196]
	cmp	r3, #0
	beq	.L569
	ldr	r3, [r4, #3832]
	cmp	r3, #0
	beq	.L569
	ldr	r3, [r4, #3840]
	cmp	r3, #0
	beq	.L569
	ldr	r3, [r4, #3800]
	cmp	r3, #0
	beq	.L569
	ldr	r3, [r4, #3180]
	cmp	r3, #0
	beq	.L569
	ldr	r3, [r4, #3836]
	cmp	r3, #0
	beq	.L569
	ldr	r3, [r4, #3280]
	cmp	r3, #0
	beq	.L569
	ldr	r3, [r4, #3844]
	cmp	r3, #0
	beq	.L569
	ldr	r3, [r4, #3848]
	cmp	r3, #0
	beq	.L569
	ldr	r3, [r4, #3172]
	cmp	r3, #0
	beq	.L569
	ldr	r3, [r4, #3816]
	cmp	r3, #0
	beq	.L569
	ldr	r3, [r4, #3808]
	cmp	r3, #0
	beq	.L569
	ldr	r3, [r4, #3168]
	cmp	r3, #0
	beq	.L569
	ldr	r3, [r4, #3284]
	cmp	r3, #0
	beq	.L569
	ldr	r3, [r4, #3812]
	cmp	r3, #0
	beq	.L569
	ldr	r3, [r4, #3176]
	cmp	r3, #0
	beq	.L569
	ldr	r3, [r4, #2328]
	cmp	r3, #0
	beq	.L569
	ldr	r3, [r4, #3856]
	cmp	r3, #0
	beq	.L569
	ldr	r3, [r4, #148]
	cmp	r3, #0
	beq	.L569
	ldr	r3, .L665
	ldr	r2, [r3, #3876]
	cmp	r2, #0
	beq	.L569
	ldr	r2, [r3, #3880]
	cmp	r2, #0
	beq	.L569
	ldr	r3, [r3, #3884]
	cmp	r3, #0
	beq	.L569
	movs	r0, #0
	pop	{r4, r5, r6, r7, r8, pc}
.L666:
	.align	2
.L665:
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
	ldr	r6, .L673
	mov	r7, r1
	subs	r4, r5, #2
	movw	r8, #65535
	ldr	r9, .L673+4
	addw	r5, r5, #1022
	ldrh	r2, [r6, #3900]
	movs	r1, #0
	ldr	r10, .L673+8
	mov	r0, r7
	lsls	r2, r2, #2
	bl	ftl_memset
.L670:
	ldrh	r3, [r4, #2]
	cmp	r3, r8
	beq	.L667
	ldrh	r2, [r6, #98]
	cmp	r2, r3
	bhi	.L669
	movs	r2, #74
	mov	r1, r9
	mov	r0, r10
	bl	printk
.L669:
	ldrh	r3, [r4, #2]!
	movs	r2, #1
	cmp	r5, r4
	lsr	r1, r3, #5
	and	r3, r3, #31
	lsl	r3, r2, r3
	ldr	r2, [r7, r1, lsl #2]
	orr	r2, r2, r3
	str	r2, [r7, r1, lsl #2]
	bne	.L670
.L667:
	pop	{r4, r5, r6, r7, r8, r9, r10, pc}
.L674:
	.align	2
.L673:
	.word	.LANCHOR0
	.word	.LANCHOR1+197
	.word	.LC1
	.fnend
	.size	FtlBbt2Bitmap, .-FtlBbt2Bitmap
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
	ldr	r4, .L681
	mov	r8, r1
	ldrh	r3, [r4, #136]
	cmp	r3, r0
	bcs	.L676
	mov	r2, #492
	ldr	r1, .L681+4
	ldr	r0, .L681+8
	bl	printk
.L676:
	ldr	r3, [r4, #3888]
	movs	r5, #12
	ldr	r7, [r3, r6, lsl #2]
	cbnz	r7, .L677
	mul	r5, r5, r8
	ldr	r3, [r4, #2492]
	ldrh	r2, [r4, #110]
	movs	r1, #255
	add	r3, r3, r5
	ldr	r0, [r3, #8]
	bl	ftl_memset
	ldr	r3, [r4, #2492]
	strh	r6, [r3, r5]	@ movhi
	ldr	r3, [r4, #2492]
	add	r5, r5, r3
	str	r7, [r5, #4]
.L678:
	movs	r0, #0
	pop	{r4, r5, r6, r7, r8, pc}
.L677:
	mul	r5, r5, r8
	ldr	r3, [r4, #2492]
	movs	r2, #1
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
	bl	printk
	movs	r3, #4
	ldr	r1, [r4, #3272]
	mov	r2, r3
	ldr	r0, .L681+20
	bl	rknand_print_hex
	ldrh	r3, [r4, #136]
	movs	r2, #4
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
	bl	printk
.L680:
	ldr	r3, [r4, #2492]
	movs	r1, #0
	adds	r2, r3, r5
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
	.word	.LC83
	.word	.LC84
	.word	.LC85
	.fnend
	.size	load_l2p_region, .-load_l2p_region
	.align	1
	.global	ftl_free_no_use_map_blk
	.syntax unified
	.thumb
	.thumb_func
	.fpu softvfp
	.type	ftl_free_no_use_map_blk, %function
ftl_free_no_use_map_blk:
	.fnstart
	@ args = 0, pretend = 0, frame = 8
	@ frame_needed = 0, uses_anonymous_args = 0
	push	{r0, r1, r2, r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.save {r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.pad #12
	mov	r4, r0
	ldrh	r2, [r0, #10]
	movs	r1, #0
	ldr	r5, [r0, #20]
	ldr	r9, [r0, #12]
	ldr	r6, [r0, #24]
	lsls	r2, r2, #1
	mov	r0, r5
	bl	ftl_memset
	movs	r3, #0
.L684:
	ldrh	r1, [r4, #6]
	uxth	r2, r3
	cmp	r1, r2
	bhi	.L688
	ldrh	fp, [r5]
	movs	r6, #0
	ldr	r2, .L697
	mov	r10, r6
.L689:
	ldrh	r1, [r4, #10]
	uxth	r3, r6
	cmp	r1, r3
	bhi	.L693
	mov	r0, r10
	add	sp, sp, #12
	@ sp needed
	pop	{r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L688:
	uxth	r2, r3
	ldr	r1, [r6, r2, lsl #2]
	movs	r2, #0
	ubfx	r1, r1, #10, #16
.L685:
	ldrh	r7, [r4, #10]
	uxth	r0, r2
	cmp	r7, r0
	bhi	.L687
	adds	r3, r3, #1
	b	.L684
.L687:
	uxth	r0, r2
	adds	r2, r2, #1
	ldrh	r7, [r9, r0, lsl #1]
	cmp	r7, r1
	ittt	eq
	ldrheq	r7, [r5, r0, lsl #1]
	addeq	r7, r7, #1
	strheq	r7, [r5, r0, lsl #1]	@ movhi
	b	.L685
.L693:
	ldrh	r1, [r4]
	uxth	r7, r6
	cmp	r1, r3
	bne	.L690
	ldrh	r1, [r2, #104]
	ldrh	r0, [r4, #2]
	cmp	r0, r1
	it	cc
	strhcc	r1, [r5, r7, lsl #1]	@ movhi
.L690:
	ldrh	r8, [r5, r7, lsl #1]
	cmp	fp, r8
	itt	hi
	movhi	r10, r3
	movhi	fp, r8
	cmp	r8, #0
	bne	.L692
	ldrh	r0, [r9, r7, lsl #1]
	cbz	r0, .L692
	movs	r1, #1
	str	r2, [sp, #4]
	bl	FtlFreeSysBlkQueueIn
	strh	r8, [r9, r7, lsl #1]	@ movhi
	ldr	r2, [sp, #4]
	ldrh	r3, [r4, #8]
	subs	r3, r3, #1
	strh	r3, [r4, #8]	@ movhi
.L692:
	adds	r6, r6, #1
	b	.L689
.L698:
	.align	2
.L697:
	.word	.LANCHOR0
	.fnend
	.size	ftl_free_no_use_map_blk, .-ftl_free_no_use_map_blk
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
	push	{r3, r4, r5, r6, r7, lr}
	.save {r3, r4, r5, r6, r7, lr}
	movw	r2, #65535
	ldrh	r3, [r0]
	mov	r4, r0
	ldr	r5, [r0, #12]
	cmp	r3, r2
	bne	.L700
	ldrh	r3, [r0, #8]
	cbz	r3, .L701
	movw	r2, #641
	ldr	r1, .L709
	ldr	r0, .L709+4
	bl	printk
.L701:
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
.L702:
	movs	r0, #0
	pop	{r3, r4, r5, r6, r7, pc}
.L700:
	ldrh	r6, [r5, r3, lsl #1]
	movs	r1, #255
	ldrh	r3, [r0, #2]
	ldr	r5, .L709+8
	ldr	r2, [r0, #28]
	ldr	r7, [r0, #24]
	orr	r3, r3, r6, lsl #10
	str	r3, [r5, #3264]
	ldr	r3, [r5, #3280]
	str	r3, [r5, #3268]
	ldr	r3, [r5, #3284]
	str	r3, [r5, #3272]
	str	r2, [r3, #4]
	movw	r2, #64245
	strh	r2, [r3, #8]	@ movhi
	ldrh	r2, [r0, #4]
	strh	r6, [r3, #2]	@ movhi
	strh	r2, [r3]	@ movhi
	ldrh	r2, [r5, #104]
	ldr	r0, [r5, #3280]
	lsls	r2, r2, #3
	bl	ftl_memset
	movs	r2, #0
	mov	r3, r2
.L703:
	ldrh	r0, [r4, #6]
	uxth	r1, r2
	cmp	r0, r1
	bhi	.L705
	movs	r2, #1
	movs	r3, #0
	mov	r1, r2
	ldr	r0, .L709+12
	bl	FlashProgPages
	ldrh	r3, [r4, #2]
	mov	r0, r4
	adds	r3, r3, #1
	strh	r3, [r4, #2]	@ movhi
	bl	ftl_map_blk_gc
	b	.L702
.L705:
	uxth	r1, r2
	ldr	r0, [r7, r1, lsl #2]
	cmp	r6, r0, lsr #10
	bne	.L704
	ldr	r0, [r5, #3280]
	adds	r3, r3, #1
	uxth	r3, r3
	str	r1, [r0, r3, lsl #3]
	ldr	r0, [r7, r1, lsl #2]
	ldr	r1, [r5, #3280]
	add	r1, r1, r3, lsl #3
	str	r0, [r1, #4]
.L704:
	adds	r2, r2, #1
	b	.L703
.L710:
	.align	2
.L709:
	.word	.LANCHOR1+227
	.word	.LC1
	.word	.LANCHOR0
	.word	.LANCHOR0+3260
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
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	push	{r3, r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.save {r3, r4, r5, r6, r7, r8, r9, r10, fp, lr}
	mov	r4, r0
	ldr	r7, .L730
	mov	r8, r1
	mov	r10, r2
	movs	r6, #0
	ldr	r9, .L730+20
	mov	r5, r7
.L712:
	ldr	r3, [r7, #2524]
	adds	r3, r3, #1
	str	r3, [r7, #2524]
	ldrh	r3, [r7, #104]
	ldrh	r2, [r4, #2]
	subs	r3, r3, #1
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
	ldrh	r2, [r4]
	ldr	r3, [r4, #12]
	ldrh	r3, [r3, r2, lsl #1]
	cbnz	r3, .L715
	movw	r2, #699
	mov	r1, r9
	ldr	r0, .L730+4
	bl	printk
.L715:
	ldrh	r2, [r4]
	ldrh	r3, [r4, #10]
	cmp	r2, r3
	bcc	.L716
	mov	r2, #700
	mov	r1, r9
	ldr	r0, .L730+4
	bl	printk
.L716:
	ldrh	r2, [r4]
	movs	r1, #16
	ldr	r3, [r4, #12]
	ldr	r0, [r5, #3284]
	ldrh	fp, [r3, r2, lsl #1]
	ldrh	r3, [r4, #2]
	str	r0, [r5, #3272]
	str	r10, [r5, #3268]
	orr	r3, r3, fp, lsl #10
	str	r3, [r5, #3264]
	bl	__memzero
	ldr	r3, [r5, #3272]
	ldr	r2, [r4, #28]
	ldr	r0, .L730+8
	strh	r8, [r3, #8]	@ movhi
	str	r2, [r3, #4]
	ldrh	r2, [r4, #4]
	strh	fp, [r3, #2]	@ movhi
	strh	r2, [r3]	@ movhi
	movs	r3, #1
	mov	r2, r3
	mov	r1, r3
	bl	FlashProgPages
	ldrh	r3, [r4, #2]
	adds	r3, r3, #1
	uxth	r3, r3
	strh	r3, [r4, #2]	@ movhi
	ldr	r2, [r5, #3260]
	adds	r2, r2, #1
	bne	.L717
	ldr	r1, [r5, #3264]
	adds	r6, r6, #1
	ldr	r0, .L730+12
	uxth	r6, r6
	bl	printk
	ldrh	r3, [r4, #2]
	cmp	r3, #2
	ittt	ls
	ldrhls	r3, [r5, #104]
	addls	r3, r3, #-1
	strhls	r3, [r4, #2]	@ movhi
	cmp	r6, #3
	bls	.L719
	mov	r2, r6
	ldr	r1, [r5, #3264]
	ldr	r0, .L730+16
	bl	printk
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
	movs	r0, #0
	ldr	r3, [r4, #24]
	str	r2, [r3, r8, lsl #2]
	pop	{r3, r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L731:
	.align	2
.L730:
	.word	.LANCHOR0
	.word	.LC1
	.word	.LANCHOR0+3260
	.word	.LC86
	.word	.LC87
	.word	.LANCHOR1+258
	.fnend
	.size	FtlMapWritePage, .-FtlMapWritePage
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
	ldr	r9, [r0, #24]
	bl	ftl_free_no_use_map_blk
	ldrh	r3, [r4, #10]
	ldrh	r2, [r4, #8]
	ldr	r5, .L744
	subs	r3, r3, #4
	cmp	r2, r3
	blt	.L733
	uxth	r0, r0
	ldrh	r8, [r6, r0, lsl #1]
	cmp	r8, #0
	beq	.L733
	ldr	r3, [r4, #32]
	cbnz	r3, .L733
	movs	r2, #1
	str	r2, [r4, #32]
	strh	r3, [r6, r0, lsl #1]	@ movhi
	ldrh	r3, [r4, #8]
	ldrh	r2, [r4, #2]
	subs	r3, r3, #1
	strh	r3, [r4, #8]	@ movhi
	ldrh	r3, [r5, #104]
	cmp	r2, r3
	bcc	.L734
	mov	r0, r4
	bl	ftl_map_blk_alloc_new_blk
.L734:
	ldr	fp, .L744+12
	movs	r6, #0
.L735:
	ldrh	r3, [r4, #6]
	uxth	r10, r6
	cmp	r3, r10
	bhi	.L739
	movs	r1, #1
	mov	r0, r8
	bl	FtlFreeSysBlkQueueIn
	movs	r3, #0
	str	r3, [r4, #32]
.L733:
	ldrh	r2, [r4, #2]
	ldrh	r3, [r5, #104]
	cmp	r2, r3
	bcc	.L740
	mov	r0, r4
	bl	ftl_map_blk_alloc_new_blk
.L740:
	movs	r0, #0
	add	sp, sp, #12
	@ sp needed
	pop	{r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L739:
	uxth	r7, r6
	ldr	r3, [r9, r7, lsl #2]
	cmp	r8, r3, lsr #10
	bne	.L736
	ldr	r3, [r5, #3844]
	ldr	r0, .L744+4
	str	r3, [r5, #3268]
	ldr	r3, [r5, #3284]
	str	r3, [r5, #3272]
	ldr	r2, [r9, r7, lsl #2]
	str	r3, [sp, #4]
	str	r2, [r5, #3264]
	movs	r2, #1
	mov	r1, r2
	bl	FlashReadPages
	ldr	r3, [sp, #4]
	ldrh	r3, [r3, #8]
	cmp	r3, r10
	beq	.L737
	movw	r2, #613
	ldr	r1, .L744+8
	mov	r0, fp
	bl	printk
.L737:
	ldr	r3, [r5, #3260]
	adds	r3, r3, #1
	bne	.L738
	movs	r3, #0
	str	r3, [r9, r7, lsl #2]
.L736:
	adds	r6, r6, #1
	b	.L735
.L738:
	ldr	r2, [r5, #3268]
	mov	r1, r7
	mov	r0, r4
	bl	FtlMapWritePage
	b	.L736
.L745:
	.align	2
.L744:
	.word	.LANCHOR0
	.word	.LANCHOR0+3260
	.word	.LANCHOR1+274
	.word	.LC1
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
	ldr	r5, .L747
	muls	r4, r0, r4
	ldr	r3, [r5, #2492]
	add	r0, r5, #3904
	adds	r2, r3, r4
	ldrh	r1, [r3, r4]
	ldr	r2, [r2, #8]
	bl	FtlMapWritePage
	ldr	r3, [r5, #2492]
	movs	r0, #0
	add	r4, r4, r3
	ldr	r3, [r4, #4]
	bic	r3, r3, #-2147483648
	str	r3, [r4, #4]
	pop	{r3, r4, r5, pc}
.L748:
	.align	2
.L747:
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
	push	{r4, r5, r6, r7, r8, r9, r10, lr}
	.save {r4, r5, r6, r7, r8, r9, r10, lr}
	mov	r7, r1
	ldr	r4, .L763
	mov	r9, r2
	ldrh	r5, [r4, #108]
	adds	r3, r5, #7
	movs	r5, #1
	lsr	r8, r0, r3
	lsls	r5, r5, r3
	ldr	r3, [r4, #2504]
	subs	r5, r5, #1
	uxth	r8, r8
	ands	r5, r5, r0
	cmp	r0, r3
	uxth	r5, r5
	bcc	.L750
	mov	r2, #808
	ldr	r1, .L763+4
	ldr	r0, .L763+8
	bl	printk
.L750:
	ldrh	r2, [r4, #138]
	movs	r3, #0
	ldr	r1, [r4, #2492]
	mov	r10, #12
.L751:
	uxth	r6, r3
	cmp	r6, r2
	bcc	.L756
	bl	select_l2p_ram_region
	mul	r10, r10, r0
	ldr	r3, [r4, #2492]
	mov	r6, r0
	ldrh	r1, [r3, r10]
	add	r2, r3, r10
	movw	r3, #65535
	cmp	r1, r3
	beq	.L757
	ldr	r3, [r2, #4]
	cmp	r3, #0
	bge	.L757
	bl	flush_l2p_region
.L757:
	mov	r1, r6
	mov	r0, r8
	bl	load_l2p_region
	b	.L752
.L756:
	adds	r3, r3, #1
	mla	r0, r10, r3, r1
	ldrh	r0, [r0, #-12]
	cmp	r0, r8
	bne	.L751
.L752:
	movs	r3, #12
	cmp	r9, #0
	bne	.L753
	ldr	r2, [r4, #2492]
	mla	r3, r3, r6, r2
	ldr	r3, [r3, #8]
	ldr	r3, [r3, r5, lsl #2]
	str	r3, [r7]
.L754:
	ldr	r2, [r4, #2492]
	movs	r3, #12
	mla	r6, r3, r6, r2
	ldr	r3, [r6, #4]
	adds	r2, r3, #1
	beq	.L760
	adds	r3, r3, #1
	str	r3, [r6, #4]
.L760:
	movs	r0, #0
	pop	{r4, r5, r6, r7, r8, r9, r10, pc}
.L753:
	muls	r3, r6, r3
	ldr	r2, [r4, #2492]
	ldr	r1, [r7]
	add	r2, r2, r3
	ldr	r2, [r2, #8]
	str	r1, [r2, r5, lsl #2]
	ldr	r2, [r4, #2492]
	strh	r8, [r4, #2496]	@ movhi
	add	r3, r3, r2
	ldr	r2, [r3, #4]
	orr	r2, r2, #-2147483648
	str	r2, [r3, #4]
	b	.L754
.L764:
	.align	2
.L763:
	.word	.LANCHOR0
	.word	.LANCHOR1+289
	.word	.LC1
	.fnend
	.size	log2phys, .-log2phys
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
	ldr	r5, .L775
	ubfx	r0, r1, #10, #16
	str	r1, [sp, #4]
	bl	P2V_block_in_plane
	ldr	r2, [r5, #2324]
	mov	r7, r0
	ldrh	r3, [r2, r0, lsl #1]
	cbnz	r3, .L766
	ldr	r4, [r5, #2340]
	cbz	r4, .L767
	ldr	r1, [r5, #2316]
	mov	ip, #6
	ldr	r2, .L775+4
	movw	lr, #65535
	ldrh	r0, [r5, #2344]
	subs	r4, r4, r1
	asrs	r4, r4, #1
	muls	r4, r2, r4
	uxth	r4, r4
.L768:
	uxth	r2, r3
	cmp	r0, r2
	bls	.L767
	cmp	r4, r7
	bne	.L769
	mov	r1, r4
	ldr	r0, .L775+8
	bl	List_remove_node
	ldrh	r3, [r5, #2344]
	cbnz	r3, .L770
	mov	r2, #1688
	ldr	r1, .L775+12
	ldr	r0, .L775+16
	bl	printk
.L770:
	ldrh	r3, [r5, #2344]
	mov	r0, r4
	subs	r3, r3, #1
	strh	r3, [r5, #2344]	@ movhi
	bl	INSERT_DATA_LIST
	ldr	r2, [r5, #2324]
	ldrh	r3, [r2, r7, lsl #1]
.L766:
	adds	r3, r3, #1
	strh	r3, [r2, r7, lsl #1]	@ movhi
	b	.L767
.L769:
	mul	r4, ip, r4
	adds	r3, r3, #1
	ldrh	r4, [r1, r4]
	cmp	r4, lr
	bne	.L768
.L767:
	movs	r2, #1
	add	r1, sp, #4
	mov	r0, r6
	bl	log2phys
	add	sp, sp, #12
	@ sp needed
	pop	{r4, r5, r6, r7, pc}
.L776:
	.align	2
.L775:
	.word	.LANCHOR0
	.word	-1431655765
	.word	.LANCHOR0+2340
	.word	.LANCHOR1+298
	.word	.LC1
	.fnend
	.size	FtlReUsePrevPpa, .-FtlReUsePrevPpa
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
	ldr	r6, .L791
	.pad #32
	sub	sp, sp, #32
	movs	r1, #0
	ldr	r8, .L791+16
	mov	r4, r6
	ldr	r0, .L791+4
	bl	printk
.L778:
	ldr	r3, [r6, #2504]
	cmp	r5, r3
	bcc	.L784
	add	sp, sp, #32
	@ sp needed
	pop	{r4, r5, r6, r7, r8, pc}
.L784:
	movs	r2, #0
	add	r1, sp, #28
	mov	r0, r5
	bl	log2phys
	ubfx	r3, r5, #0, #11
	cbnz	r3, .L779
	ldr	r2, [sp, #28]
	mov	r1, r5
	mov	r0, r8
	bl	printk
.L779:
	ldr	r3, [sp, #28]
	adds	r2, r3, #1
	beq	.L781
	str	r3, [r4, #3264]
	movs	r2, #0
	ldr	r3, [r4, #3280]
	movs	r1, #1
	ldr	r7, [r4, #3284]
	ldr	r0, .L791+8
	str	r3, [r4, #3268]
	str	r5, [r4, #3276]
	str	r7, [r4, #3272]
	str	r2, [r4, #3260]
	bl	FlashReadPages
	ldr	r3, [r4, #3260]
	cmp	r3, #256
	beq	.L782
	adds	r3, r3, #1
	beq	.L782
	ldr	r3, [r7, #8]
	cmp	r5, r3
	beq	.L781
.L782:
	ldr	r2, [r4, #3268]
	ldr	r3, [r4, #3272]
	ldr	r0, .L791+12
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
	bl	printk
.L781:
	adds	r5, r5, #1
	b	.L778
.L792:
	.align	2
.L791:
	.word	.LANCHOR0
	.word	.LC88
	.word	.LANCHOR0+3260
	.word	.LC90
	.word	.LC89
	.fnend
	.size	ftl_scan_all_data, .-ftl_scan_all_data
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
	ldr	r3, .L806
	push	{r4, r5, r6, lr}
	.save {r4, r5, r6, lr}
	.pad #88
	sub	sp, sp, #88
	mov	r4, r3
	ldr	r0, [r3, #2716]
	cmp	r0, #0
	beq	.L794
	ldr	r1, [r3, #2720]
	ldr	r2, [r3, #2504]
	cmp	r1, r2
	bcs	.L795
	mov	r5, #2048
.L800:
	ldr	r0, [r4, #2720]
	ldr	r3, [r4, #2504]
	cmp	r0, r3
	bcs	.L797
	movs	r2, #0
	mov	r1, sp
	bl	log2phys
	ldr	r2, [sp]
	ldr	r3, [r4, #2720]
	adds	r1, r2, #1
	add	r3, r3, #1
	str	r3, [r4, #2720]
	beq	.L798
	str	r3, [sp, #20]
	add	r0, sp, #88
	ldr	r3, .L806+4
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
	bne	.L797
	ldr	r0, [sp]
	ubfx	r0, r0, #10, #16
	bl	P2V_block_in_plane
	bl	FtlGcRefreshBlock
.L797:
	mov	r0, #-1
.L793:
	add	sp, sp, #88
	@ sp needed
	pop	{r4, r5, r6, pc}
.L798:
	subs	r5, r5, #1
	bne	.L800
	b	.L797
.L795:
	ldr	r2, [r3, #2508]
	movs	r0, #0
	str	r0, [r3, #2716]
	str	r0, [r3, #2720]
	str	r2, [r3, #2712]
	b	.L793
.L794:
	ldr	r1, [r3, #2508]
	ldr	r6, [r3, #2712]
	add	r2, r1, #1048576
	cmp	r6, r2
	bhi	.L803
	ldr	r2, [r3, #2564]
	lsrs	r5, r2, #10
	mov	r2, #33554432
	asrs	r2, r2, r5
	add	r2, r2, r6
	cmp	r1, r2
	bhi	.L803
	ldrb	r3, [r3, #2296]	@ zero_extendqisi2
	cmp	r3, #0
	bne	.L793
.L803:
	movs	r3, #1
	movs	r0, #0
	str	r3, [r4, #2716]
	str	r0, [r4, #2720]
	str	r1, [r4, #2712]
	b	.L793
.L807:
	.align	2
.L806:
	.word	.LANCHOR0
	.word	ftl_temp_buf
	.fnend
	.size	FtlReadRefresh, .-FtlReadRefresh
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
	ldr	r6, .L832
	ldr	r7, .L832+4
	ldr	r1, .L832+8
	mov	r5, r6
	ldr	r0, .L832+12
	bl	printk
	mov	r1, #8192
	ldr	r0, .L832+4
	bl	__memzero
.L809:
	ldr	r3, [r6, #2504]
	cmp	r4, r3
	bcc	.L811
	ldr	r8, .L832+4
	movs	r4, #0
	ldr	r10, .L832+24
	mov	r7, r4
	movw	r9, #65535
.L812:
	ldrh	r2, [r5, #40]
	uxth	r3, r4
	cmp	r2, r3
	bhi	.L814
	ldr	r4, [r5, #2340]
	cbz	r4, .L815
	ldr	r3, [r5, #2316]
	movs	r6, #0
	ldrh	r8, [r5, #2344]
	mov	fp, #6
	ldr	r9, .L832+4
	subs	r4, r4, r3
	ldr	r3, .L832+16
	asrs	r4, r4, #1
	ldr	r10, .L832+28
	muls	r4, r3, r4
	uxth	r4, r4
.L816:
	uxth	r3, r6
	cmp	r8, r3
	bls	.L815
	ldr	r3, [r5, #2324]
	ldrh	r2, [r3, r4, lsl #1]
	cbz	r2, .L817
	movs	r7, #1
	ldrh	r3, [r9, r4, lsl #1]
	mov	r1, r4
	mov	r0, r10
	bl	printk
.L817:
	mul	r4, fp, r4
	ldr	r3, [r5, #2316]
	adds	r6, r6, #1
	ldrh	r4, [r3, r4]
	movw	r3, #65535
	cmp	r4, r3
	bne	.L816
.L815:
	cbz	r7, .L808
	movw	r2, #2321
	ldr	r1, .L832+8
	ldr	r0, .L832+20
	bl	printk
.L808:
	add	sp, sp, #12
	@ sp needed
	pop	{r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L811:
	movs	r2, #0
	add	r1, sp, #4
	mov	r0, r4
	bl	log2phys
	ldr	r0, [sp, #4]
	adds	r3, r0, #1
	beq	.L810
	ubfx	r0, r0, #10, #16
	bl	P2V_block_in_plane
	ldrh	r3, [r7, r0, lsl #1]
	adds	r3, r3, #1
	strh	r3, [r7, r0, lsl #1]	@ movhi
.L810:
	adds	r4, r4, #1
	b	.L809
.L814:
	ldr	r3, [r5, #2324]
	uxth	r6, r4
	ldrh	r2, [r3, r6, lsl #1]
	ldrh	r3, [r8, r6, lsl #1]
	cmp	r2, r3
	beq	.L813
	mov	r1, r6
	mov	r0, r10
	bl	printk
	ldr	r3, [r5, #2324]
	ldrh	r3, [r3, r6, lsl #1]
	cmp	r3, r9
	beq	.L813
	ldrh	r2, [r8, r6, lsl #1]
	cmp	r2, r3
	it	hi
	movhi	r7, #1
.L813:
	adds	r4, r4, #1
	b	.L812
.L833:
	.align	2
.L832:
	.word	.LANCHOR0
	.word	check_vpc_table
	.word	.LANCHOR1+314
	.word	.LC91
	.word	-1431655765
	.word	.LC1
	.word	.LC92
	.word	.LC93
	.fnend
	.size	ftl_check_vpc, .-ftl_check_vpc
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
	cbz	r3, .L834
	ldrh	r5, [r0, #6]
	movs	r3, #0
	ldr	r4, .L838
	ldr	r2, [r0, #24]
	str	r3, [r0, #36]
	subs	r5, r5, #1
	ldr	r0, [r4, #3844]
	uxth	r5, r5
	ldr	r1, [r4, #3284]
	str	r0, [r4, #3268]
	str	r1, [r4, #3272]
	ldr	r2, [r2, r5, lsl #2]
	str	r2, [r4, #3264]
	cbz	r2, .L836
	movs	r2, #1
	addw	r0, r4, #3260
	mov	r1, r2
	bl	FlashReadPages
.L837:
	ldr	r2, [r4, #3268]
	mov	r1, r5
	mov	r0, r6
	pop	{r4, r5, r6, lr}
	b	FtlMapWritePage
.L836:
	ldrh	r2, [r4, #110]
	movs	r1, #255
	bl	ftl_memset
	b	.L837
.L834:
	pop	{r4, r5, r6, pc}
.L839:
	.align	2
.L838:
	.word	.LANCHOR0
	.fnend
	.size	FtlMapBlkWriteDump_data, .-FtlMapBlkWriteDump_data
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
	ldr	r4, .L921
	mov	r1, r5
	.pad #20
	sub	sp, sp, #20
	ldr	r2, [r4, #128]
	ldr	r0, [r4, #3892]
	strh	r5, [r4, #3944]	@ movhi
	strh	r5, [r4, #144]	@ movhi
	lsls	r2, r2, #2
	bl	ftl_memset
	ldr	r2, [r4, #128]
	mov	r1, r5
	ldr	r0, [r4, #3868]
	lsls	r2, r2, #1
	bl	ftl_memset
	ldrh	r2, [r4, #120]
	mov	r1, r5
	ldr	r0, [r4, #3880]
	lsls	r2, r2, #2
	bl	ftl_memset
	ldrh	r2, [r4, #120]
	mov	r1, r5
	ldr	r0, [r4, #148]
	lsls	r2, r2, #1
	bl	ftl_memset
	ldrh	r8, [r4, #40]
	mov	r3, #-1
	str	r3, [r4, #2576]
	str	r3, [r4, #2580]
	str	r3, [r4, #2584]
.L841:
	ldrh	r3, [r4, #42]
	cmp	r3, r8
	bls	.L883
	movs	r5, #0
	ldrh	fp, [r4, #32]
	ldr	r9, [r4, #3832]
	mov	r7, r5
	ldr	r3, [r4, #3172]
	mov	r10, #20
	ldr	r2, [r4, #3176]
	ldrh	r6, [r4, #112]
	b	.L884
.L844:
	str	r3, [sp, #8]
	mov	r1, r8
	ldr	r3, .L921+4
	str	r2, [sp, #12]
	ldrb	r0, [r3, r5]	@ zero_extendqisi2
	bl	V2P_block
	str	r0, [sp, #4]
	bl	FtlBbmIsBadBlock
	ldr	r3, [sp, #8]
	ldr	r2, [sp, #12]
	cbnz	r0, .L842
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
.L842:
	adds	r5, r5, #1
.L884:
	uxth	r1, r5
	cmp	fp, r1
	bhi	.L844
	cbnz	r7, .L845
.L882:
	add	r8, r8, #1
	uxth	r8, r8
	b	.L841
.L845:
	mov	r0, r9
	mov	r9, #0
	movs	r2, #1
	mov	r1, r7
	bl	FlashReadPages
.L846:
	uxth	r3, r9
	cmp	r7, r3
	bls	.L882
	movs	r3, #20
	mul	r10, r3, r9
	ldr	r3, [r4, #3832]
	add	r2, r3, r10
	ldr	r3, [r3, r10]
	ldr	r5, [r2, #4]
	ldr	r6, [r2, #12]
	adds	r3, r3, #1
	ubfx	r5, r5, #10, #16
	bne	.L849
	mov	fp, #16
	movw	r3, #65535
.L851:
	ldr	r0, [r4, #3832]
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
	bne	.L848
	ldr	r3, [r4, #3832]
	mov	r2, #-1
	str	r2, [r3, r10]
	ldr	r3, [r4, #3832]
	ldr	r3, [r3, r10]
	cmp	r3, r2
	bne	.L849
.L850:
	movs	r1, #0
	mov	r0, r5
	bl	FtlFreeSysBlkQueueIn
	b	.L854
.L848:
	ldr	r2, [r4, #3832]
	ldr	r2, [r2, r10]
	adds	r2, r2, #1
	bne	.L849
	add	fp, fp, #-1
	uxth	fp, fp
	cmp	fp, #0
	bne	.L851
	b	.L850
.L849:
	ldr	r2, [r4, #2540]
	ldr	r3, [r6, #4]
	adds	r1, r2, #1
	beq	.L852
	cmp	r2, r3
	bhi	.L853
.L852:
	adds	r2, r3, #1
	itt	ne
	addne	r2, r3, #1
	strne	r2, [r4, #2540]
.L853:
	ldrh	r2, [r6]
	movw	r1, #61604
	cmp	r2, r1
	beq	.L855
	bhi	.L856
	movw	r3, #61574
	cmp	r2, r3
	beq	.L857
.L854:
	add	r9, r9, #1
	b	.L846
.L856:
	movw	r3, #61634
	cmp	r2, r3
	beq	.L858
	movw	r3, #65535
	cmp	r2, r3
	beq	.L850
	b	.L854
.L858:
	ldrh	r2, [r4, #3944]
	ldr	r3, [r4, #128]
	cmp	r2, r3
	bls	.L860
	movw	r2, #1211
	ldr	r1, .L921+8
	ldr	r0, .L921+12
	bl	printk
.L860:
	ldr	r1, [r4, #128]
	ldrh	r0, [r4, #3944]
	ldr	r10, [r4, #3892]
	uxth	ip, r1
	add	r3, ip, #-1
	sub	ip, ip, r0
	add	ip, ip, #-1
	sxth	r3, r3
	sxth	ip, ip
.L861:
	cmp	r3, ip
	bgt	.L867
	cmp	r3, #0
	bge	.L899
	b	.L854
.L867:
	ldr	r2, [r6, #4]
	lsl	fp, r3, #2
	mov	lr, r2
	ldr	r2, [r10, r3, lsl #2]
	cmp	lr, r2
	bls	.L862
	ldr	r2, [r10]
	cbnz	r2, .L863
	cmp	r1, r0
	itt	ne
	addne	r0, r0, #1
	strhne	r0, [r4, #3944]	@ movhi
.L863:
	uxth	ip, r3
	movs	r2, #0
.L864:
	uxth	r0, r2
	sxth	r1, r2
	cmp	ip, r0
	bhi	.L865
	ldr	r2, [r4, #3892]
	cmp	r3, #0
	ldr	r1, [r6, #4]
	str	r1, [r2, fp]
	ldr	r2, [r4, #3868]
	strh	r5, [r2, r3, lsl #1]	@ movhi
	blt	.L854
	ldrh	r0, [r4, #3944]
	ldr	r2, [r4, #128]
	subs	r2, r2, r0
	subs	r2, r2, #1
	sxth	r2, r2
	cmp	r3, r2
	bgt	.L854
.L899:
	ldr	r2, [r4, #3892]
	adds	r0, r0, #1
	ldr	r1, [r6, #4]
	strh	r0, [r4, #3944]	@ movhi
	str	r1, [r2, r3, lsl #2]
	ldr	r2, [r4, #3868]
.L919:
	strh	r5, [r2, r3, lsl #1]	@ movhi
	b	.L854
.L865:
	ldr	r0, [r4, #3892]
	adds	r2, r2, #1
	add	lr, r0, r1, lsl #2
	ldr	lr, [lr, #4]
	str	lr, [r0, r1, lsl #2]
	ldr	r0, [r4, #3868]
	add	lr, r0, r1, lsl #1
	ldrh	lr, [lr, #2]
	strh	lr, [r0, r1, lsl #1]	@ movhi
	b	.L864
.L862:
	subs	r3, r3, #1
	sxth	r3, r3
	b	.L861
.L922:
	.align	2
.L921:
	.word	.LANCHOR0
	.word	.LANCHOR0+60
	.word	.LANCHOR1+328
	.word	.LC1
.L857:
	ldrh	r2, [r4, #144]
	ldrh	r3, [r4, #120]
	cmp	r2, r3
	bls	.L870
	movw	r2, #1252
	ldr	r1, .L923
	ldr	r0, .L923+4
	bl	printk
.L870:
	ldrh	lr, [r4, #120]
	ldrh	r0, [r4, #144]
	ldr	r10, [r4, #3880]
	add	ip, lr, #-1
	sxth	r3, ip
	sub	ip, ip, r0
.L871:
	cmp	r3, ip
	ble	.L876
	ldr	r2, [r6, #4]
	lsl	fp, r3, #2
	ldr	r1, [r10, r3, lsl #2]
	cmp	r2, r1
	bls	.L872
	ldr	r2, [r10]
	cbnz	r2, .L873
	cmp	lr, r0
	itt	ne
	addne	r0, r0, #1
	strhne	r0, [r4, #144]	@ movhi
.L873:
	uxth	ip, r3
	movs	r2, #0
.L874:
	uxth	r0, r2
	sxth	r1, r2
	cmp	ip, r0
	bhi	.L875
	ldr	r2, [r4, #3880]
	ldr	r1, [r6, #4]
	str	r1, [r2, fp]
	ldr	r2, [r4, #148]
	strh	r5, [r2, r3, lsl #1]	@ movhi
.L876:
	cmp	r3, #0
	blt	.L854
	ldrh	r2, [r4, #120]
	ldrh	r1, [r4, #144]
	subs	r2, r2, #1
	subs	r2, r2, r1
	sxth	r2, r2
	cmp	r3, r2
	bgt	.L854
	adds	r1, r1, #1
	ldr	r2, [r4, #3880]
	strh	r1, [r4, #144]	@ movhi
	ldr	r1, [r6, #4]
	str	r1, [r2, r3, lsl #2]
	ldr	r2, [r4, #148]
	b	.L919
.L875:
	ldr	r0, [r4, #3880]
	adds	r2, r2, #1
	add	lr, r0, r1, lsl #2
	ldr	lr, [lr, #4]
	str	lr, [r0, r1, lsl #2]
	ldr	r0, [r4, #148]
	add	lr, r0, r1, lsl #1
	ldrh	lr, [lr, #2]
	strh	lr, [r0, r1, lsl #1]	@ movhi
	b	.L874
.L872:
	subs	r3, r3, #1
	sxth	r3, r3
	b	.L871
.L855:
	ldrh	r1, [r4, #2576]
	movw	r2, #65535
	cmp	r1, r2
	bne	.L878
	strh	r5, [r4, #2576]	@ movhi
.L920:
	str	r3, [r4, #2584]
	b	.L854
.L878:
	ldrh	r0, [r4, #2580]
	cmp	r0, r2
	beq	.L879
	movs	r1, #1
	bl	FtlFreeSysBlkQueueIn
.L879:
	ldr	r2, [r4, #2584]
	ldr	r3, [r6, #4]
	cmp	r2, r3
	bcs	.L880
	ldrh	r3, [r4, #2576]
	strh	r5, [r4, #2576]	@ movhi
	strh	r3, [r4, #2580]	@ movhi
	ldr	r3, [r6, #4]
	b	.L920
.L880:
	strh	r5, [r4, #2580]	@ movhi
	b	.L854
.L883:
	ldr	r1, [r4, #3868]
	ldrh	r3, [r1]
	cbz	r3, .L885
.L888:
	ldr	r1, [r4, #148]
	ldrh	r2, [r1]
	cmp	r2, #0
	beq	.L886
.L887:
	ldrh	r2, [r4, #3944]
	ldr	r3, [r4, #128]
	cmp	r2, r3
	bls	.L918
	movw	r2, #1377
	ldr	r1, .L923
	ldr	r0, .L923+4
	bl	printk
.L918:
	movs	r0, #0
	add	sp, sp, #20
	@ sp needed
	pop	{r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L885:
	ldrh	r2, [r4, #3944]
	cmp	r2, #0
	beq	.L888
	ldr	r0, [r4, #128]
.L889:
	sxth	r2, r3
	cmp	r2, r0
	bcs	.L888
	ldrh	r5, [r1, r2, lsl #1]
	adds	r3, r3, #1
	cmp	r5, #0
	beq	.L889
	mov	r3, r2
	movs	r5, #0
.L890:
	ldr	r1, [r4, #128]
	cmp	r3, r1
	bcs	.L888
	ldr	r1, [r4, #3868]
	subs	r0, r3, r2
	ldrh	r6, [r1, r3, lsl #1]
	strh	r6, [r1, r0, lsl #1]	@ movhi
	ldr	r1, [r4, #3892]
	ldr	r6, [r1, r3, lsl #2]
	str	r6, [r1, r0, lsl #2]
	ldr	r1, [r4, #3868]
	strh	r5, [r1, r3, lsl #1]	@ movhi
	adds	r3, r3, #1
	sxth	r3, r3
	b	.L890
.L886:
	ldrh	r3, [r4, #144]
	cmp	r3, #0
	beq	.L887
	ldrh	r0, [r4, #120]
.L895:
	sxth	r3, r2
	cmp	r3, r0
	mov	r5, r3
	bge	.L887
	ldrh	r6, [r1, r3, lsl #1]
	adds	r2, r2, #1
	cmp	r6, #0
	beq	.L895
	movs	r0, #0
.L896:
	ldrh	r2, [r4, #120]
	cmp	r3, r2
	bge	.L887
	ldr	r2, [r4, #148]
	subs	r1, r3, r5
	ldrh	r6, [r2, r3, lsl #1]
	strh	r6, [r2, r1, lsl #1]	@ movhi
	ldr	r2, [r4, #3880]
	ldr	r6, [r2, r3, lsl #2]
	str	r6, [r2, r1, lsl #2]
	ldr	r2, [r4, #148]
	strh	r0, [r2, r3, lsl #1]	@ movhi
	adds	r3, r3, #1
	sxth	r3, r3
	b	.L896
.L924:
	.align	2
.L923:
	.word	.LANCHOR1+328
	.word	.LC1
	.fnend
	.size	FtlScanSysBlk, .-FtlScanSysBlk
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
	.pad #28
	sub	sp, sp, #28
	ldr	r3, [r0, #24]
	mov	r4, r0
	movs	r1, #0
	ldrh	r10, [r0, #6]
	str	r3, [sp, #4]
	ldr	r3, [r0, #16]
	ldr	r5, .L952
	ldr	r9, [r0, #12]
	lsl	r2, r10, #2
	str	r3, [sp, #12]
	ldrh	r3, [r0, #8]
	ldr	r0, [sp, #4]
	str	r3, [sp, #8]
	bl	ftl_memset
	ldr	r3, [r5, #3280]
	movs	r2, #1
	ldr	r6, [r5, #3284]
	str	r2, [r4, #36]
	str	r3, [r5, #3268]
	movw	r3, #65535
	str	r6, [r5, #3272]
	strh	r3, [r4]	@ movhi
	strh	r3, [r4, #2]	@ movhi
	movs	r3, #0
	str	r3, [r4, #32]
	mov	r8, r3
	str	r3, [r4, #28]
	ldr	r3, [sp, #8]
	add	fp, r3, #-1
	mov	r3, r5
.L926:
	ldr	r1, [sp, #8]
	sxth	r2, r8
	cmp	r2, r1
	bge	.L943
	cmp	r2, fp
	lsl	r7, r2, #1
	bne	.L927
	ldrh	r0, [r9, fp, lsl #1]
	movs	r1, #1
	bl	FtlGetLastWrittenPage
	ldr	r3, [sp, #12]
	add	r7, r7, r9
	strh	r8, [r4]	@ movhi
	sxth	r9, r0
	ldr	r8, .L952+4
	adds	r0, r0, #1
	strh	r0, [r4, #2]	@ movhi
	add	r9, r9, #1
	ldr	r3, [r3, fp, lsl #2]
	mov	fp, #0
	str	r3, [r4, #28]
.L928:
	sxth	r2, fp
	cmp	r2, r9
	blt	.L930
.L943:
	mov	r0, r4
	bl	ftl_free_no_use_map_blk
	ldrh	r2, [r4, #2]
	ldrh	r3, [r5, #104]
	cmp	r2, r3
	bne	.L932
	mov	r0, r4
	bl	ftl_map_blk_alloc_new_blk
.L932:
	mov	r0, r4
	bl	ftl_map_blk_gc
	mov	r0, r4
	bl	ftl_map_blk_gc
	movs	r0, #0
	add	sp, sp, #28
	@ sp needed
	pop	{r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L930:
	ldrh	r1, [r7]
	mov	r0, r8
	orr	r2, r2, r1, lsl #10
	str	r2, [r5, #3264]
	movs	r2, #1
	mov	r1, r2
	bl	FlashReadPages
	ldr	r2, [r5, #3260]
	adds	r2, r2, #1
	beq	.L929
	ldrh	r2, [r6, #8]
	cmp	r10, r2
	bls	.L929
	ldrh	r1, [r4, #4]
	ldrh	r0, [r6]
	cmp	r0, r1
	ittt	eq
	ldreq	r1, [r5, #3264]
	ldreq	r3, [sp, #4]
	streq	r1, [r3, r2, lsl #2]
.L929:
	add	fp, fp, #1
	b	.L928
.L927:
	ldr	r1, [r5, #3280]
	str	r3, [sp, #20]
	add	r3, r9, r7
	ldr	r0, .L952+4
	str	r1, [r5, #3268]
	ldrh	r1, [r9, r2, lsl #1]
	ldrh	r2, [r5, #104]
	str	r3, [sp, #16]
	subs	r2, r2, #1
	orr	r2, r2, r1, lsl #10
	str	r2, [r5, #3264]
	movs	r2, #1
	mov	r1, r2
	bl	FlashReadPages
	ldr	r2, [r5, #3260]
	ldr	r3, [sp, #20]
	adds	r2, r2, #1
	beq	.L945
	ldrh	r1, [r6]
	ldrh	r2, [r4, #4]
	cmp	r1, r2
	bne	.L945
	ldrh	r1, [r6, #8]
	movw	r2, #64245
	cmp	r1, r2
	beq	.L934
.L945:
	movs	r7, #0
.L935:
	ldrh	r1, [r3, #104]
	sxth	r2, r7
	cmp	r2, r1
	bge	.L941
	ldr	r1, [sp, #16]
	ldr	r0, .L952+4
	str	r3, [sp, #20]
	ldrh	r1, [r1]
	orr	r2, r2, r1, lsl #10
	str	r2, [r3, #3264]
	movs	r2, #1
	mov	r1, r2
	bl	FlashReadPages
	ldr	r3, [sp, #20]
	ldr	r2, [r3, #3260]
	adds	r2, r2, #1
	beq	.L939
	ldrh	r2, [r6, #8]
	cmp	r10, r2
	bls	.L939
	ldrh	r0, [r6]
	ldrh	r1, [r4, #4]
	cmp	r0, r1
	ittt	eq
	ldreq	r1, [r3, #3264]
	ldreq	r0, [sp, #4]
	streq	r1, [r0, r2, lsl #2]
.L939:
	adds	r7, r7, #1
	b	.L935
.L934:
	movs	r0, #0
	mov	ip, #4
.L936:
	ldrh	r1, [r5, #104]
	sxth	r2, r0
	subs	r1, r1, #1
	cmp	r2, r1
	blt	.L938
.L941:
	add	r8, r8, #1
	b	.L926
.L938:
	ldr	r7, [r5, #3280]
	ldr	r1, [r7, r2, lsl #3]
	uxth	lr, r1
	cmp	r10, lr
	bls	.L937
	add	r2, ip, r2, lsl #3
	ldr	r2, [r7, r2]
	ldr	r7, [sp, #4]
	str	r2, [r7, lr, lsl #2]
.L937:
	adds	r0, r0, #1
	b	.L936
.L953:
	.align	2
.L952:
	.word	.LANCHOR0
	.word	.LANCHOR0+3260
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
	ldr	r0, .L955
	push	{r3, lr}
	.save {r3, lr}
	ldrh	r3, [r0, #120]
	strh	r3, [r0, #3958]	@ movhi
	movw	r3, #61574
	strh	r3, [r0, #3952]	@ movhi
	ldrh	r3, [r0, #144]
	strh	r3, [r0, #3956]	@ movhi
	ldrh	r3, [r0, #122]
	strh	r3, [r0, #3954]	@ movhi
	ldr	r3, [r0, #148]
	addw	r0, r0, #3948
	str	r3, [r0, #12]
	ldr	r3, [r0, #-68]
	str	r3, [r0, #16]
	ldr	r3, [r0, #-72]
	str	r3, [r0, #20]
	ldr	r3, [r0, #-64]
	str	r3, [r0, #24]
	bl	FtlMapTblRecovery
	movs	r0, #0
	pop	{r3, pc}
.L956:
	.align	2
.L955:
	.word	.LANCHOR0
	.fnend
	.size	FtlLoadVonderInfo, .-FtlLoadVonderInfo
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
	ldr	r4, .L960
	movs	r6, #12
	ldr	r2, [r4, #128]
	ldr	r0, [r4, #3872]
	lsls	r2, r2, #1
	bl	ftl_memset
	ldrh	r3, [r4, #110]
	movs	r1, #255
	ldrh	r2, [r4, #138]
	ldr	r0, [r4, #3896]
	muls	r2, r3, r2
	bl	ftl_memset
	movs	r2, #0
	mov	r3, r4
	movw	r1, #65535
	mov	r7, r2
.L958:
	ldrh	r4, [r3, #138]
	uxth	r0, r2
	adds	r5, r2, #1
	cmp	r4, r0
	bhi	.L959
	ldr	r2, [r3, #128]
	strh	r1, [r3, #3906]	@ movhi
	strh	r1, [r3, #3904]	@ movhi
	strh	r2, [r3, #3914]	@ movhi
	movw	r2, #61634
	strh	r2, [r3, #3908]	@ movhi
	ldrh	r2, [r3, #3944]
	strh	r2, [r3, #3912]	@ movhi
	ldrh	r2, [r3, #136]
	strh	r2, [r3, #3910]	@ movhi
	ldr	r2, [r3, #3868]
	str	r2, [r3, #3916]
	ldr	r2, [r3, #3892]
	str	r2, [r3, #3920]
	ldr	r2, [r3, #3872]
	str	r2, [r3, #3924]
	ldr	r2, [r3, #3888]
	str	r2, [r3, #3928]
	pop	{r3, r4, r5, r6, r7, pc}
.L959:
	uxth	r2, r2
	ldr	r0, [r3, #2492]
	mul	r4, r6, r2
	add	ip, r0, r4
	str	r7, [ip, #4]
	strh	r1, [r0, r4]	@ movhi
	ldr	r0, [r3, #2492]
	add	r0, r0, r4
	ldrh	r4, [r3, #110]
	muls	r2, r4, r2
	ldr	r4, [r3, #3896]
	bic	r2, r2, #3
	add	r2, r2, r4
	str	r2, [r0, #8]
	mov	r2, r5
	b	.L958
.L961:
	.align	2
.L960:
	.word	.LANCHOR0
	.fnend
	.size	FtlL2PDataInit, .-FtlL2PDataInit
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
	ldr	r0, .L963
	bl	FtlMapTblRecovery
	movs	r0, #0
	pop	{r3, pc}
.L964:
	.align	2
.L963:
	.word	.LANCHOR0+3904
	.fnend
	.size	FtlLoadMapInfo, .-FtlLoadMapInfo
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
	ldr	r4, .L966
	movw	r3, #65535
	mov	r1, r5
	ldrh	r2, [r4, #120]
	strh	r3, [r4, #3992]	@ movhi
	mov	r3, #-1
	ldr	r0, [r4, #148]
	str	r3, [r4, #4004]
	lsls	r2, r2, #1
	str	r5, [r4, #3988]
	str	r5, [r4, #3996]
	str	r5, [r4, #4000]
	strh	r5, [r4, #144]	@ movhi
	bl	ftl_memset
	ldrh	r2, [r4, #42]
	mov	r1, r5
	ldr	r0, [r4, #2328]
	lsls	r2, r2, #1
	bl	ftl_memset
	ldrh	r2, [r4, #42]
	mov	r1, r5
	ldr	r0, [r4, #3856]
	lsls	r2, r2, #1
	bl	ftl_memset
	addw	r0, r4, #2268
	movs	r1, #48
	bl	__memzero
	addw	r0, r4, #2636
	mov	r1, #512
	bl	__memzero
	bl	FtlGcBufInit
	bl	FtlL2PDataInit
	mov	r0, r5
	pop	{r3, r4, r5, pc}
.L967:
	.align	2
.L966:
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
	@ args = 0, pretend = 0, frame = 8
	@ frame_needed = 0, uses_anonymous_args = 0
	push	{r0, r1, r2, r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.save {r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.pad #12
	movs	r6, #0
	ldr	r4, .L979
	movs	r2, #6
	mov	r9, r6
	mov	r7, r6
	movs	r1, #0
	ldrh	r3, [r4, #42]
	add	fp, r4, #60
	ldr	r0, [r4, #2316]
	muls	r2, r3, r2
	bl	ftl_memset
	str	r6, [r4, #2340]
	str	r6, [r4, #2320]
	str	r6, [r4, #2332]
	strh	r6, [r4, #2336]	@ movhi
	strh	r6, [r4, #2344]	@ movhi
.L969:
	ldrh	r3, [r4, #40]
	uxth	r8, r6
	cmp	r8, r3
	bcs	.L975
	mov	r10, #0
	ldrh	r2, [r4, #32]
	ldrh	r3, [r4, #102]
	mov	r5, r10
	b	.L976
.L971:
	mov	r1, r8
	ldrb	r0, [fp, r10]	@ zero_extendqisi2
	stm	sp, {r2, r3}
	bl	V2P_block
	bl	FtlBbmIsBadBlock
	ldm	sp, {r2, r3}
	cbnz	r0, .L970
	add	r5, r5, r3
	uxth	r5, r5
.L970:
	add	r10, r10, #1
.L976:
	uxth	r1, r10
	cmp	r2, r1
	bhi	.L971
	cbz	r5, .L978
	mov	r1, r5
	mov	r0, #32768
	bl	__aeabi_idiv
.L972:
	ldr	r2, [r4, #2316]
	uxth	r1, r6
	movs	r3, #6
	mla	r3, r3, r1, r2
	strh	r0, [r3, #4]	@ movhi
	ldrh	r3, [r4, #2348]
	cmp	r3, r8
	beq	.L973
	ldrh	r3, [r4, #2396]
	cmp	r3, r8
	beq	.L973
	ldrh	r3, [r4, #2444]
	cmp	r3, r8
	beq	.L973
	ldr	r3, [r4, #2324]
	ldrh	r3, [r3, r1, lsl #1]
	cbnz	r3, .L974
	add	r9, r9, #1
	mov	r0, r8
	uxth	r9, r9
	bl	INSERT_FREE_LIST
.L973:
	adds	r6, r6, #1
	b	.L969
.L978:
	mov	r0, r5
	b	.L972
.L974:
	adds	r7, r7, #1
	mov	r0, r8
	uxth	r7, r7
	bl	INSERT_DATA_LIST
	b	.L973
.L975:
	strh	r7, [r4, #2336]	@ movhi
	add	r7, r7, r9
	cmp	r7, r3
	strh	r9, [r4, #2344]	@ movhi
	ble	.L977
	movw	r2, #2154
	ldr	r1, .L979+4
	ldr	r0, .L979+8
	bl	printk
.L977:
	movs	r0, #0
	add	sp, sp, #12
	@ sp needed
	pop	{r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L980:
	.align	2
.L979:
	.word	.LANCHOR0
	.word	.LANCHOR1+342
	.word	.LC1
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
	ldr	r4, .L982
	movs	r1, #255
	ldrh	r2, [r4, #106]
	ldr	r0, [r4, #3188]
	strh	r3, [r4, #3192]	@ movhi
	strh	r3, [r4, #3200]	@ movhi
	lsls	r2, r2, #1
	bl	ftl_memset
	ldrh	r3, [r4, #106]
	movs	r2, #12
	ldr	r0, [r4, #3196]
	movs	r1, #255
	muls	r2, r3, r2
	bl	ftl_memset
	pop	{r4, lr}
	b	FtlGcBufInit
.L983:
	.align	2
.L982:
	.word	.LANCHOR0
	.fnend
	.size	FtlGcPageVarInit, .-FtlGcPageVarInit
	.align	1
	.global	FtlGcScanTempBlk
	.syntax unified
	.thumb
	.thumb_func
	.fpu softvfp
	.type	FtlGcScanTempBlk, %function
FtlGcScanTempBlk:
	.fnstart
	@ args = 0, pretend = 0, frame = 24
	@ frame_needed = 0, uses_anonymous_args = 0
	ldr	r3, .L1009
	push	{r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.save {r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.pad #28
	sub	sp, sp, #28
	mov	r7, r0
	str	r1, [sp, #8]
	ldrh	r4, [r3, #4]
	movw	r3, #65535
	cmp	r4, r3
	beq	.L1003
	cbnz	r4, .L985
.L986:
	bl	FtlGcPageVarInit
	b	.L987
.L1003:
	movs	r4, #0
.L985:
	ldr	r3, .L1009+4
	ldr	r2, [sp, #8]
	ldrh	r3, [r3, #102]
	cmp	r3, r2
	beq	.L986
.L987:
	mov	fp, #0
	movw	r8, #65535
.L997:
	ldrh	r3, [r7]
	movs	r0, #0
	strb	r0, [r7, #8]
	cmp	r3, r8
	beq	.L988
	ldr	r5, .L1009+4
.L1000:
	ldr	r3, [r5, #3832]
	movs	r2, #0
	ldrh	ip, [r5, #32]
	add	lr, r7, #16
	ldr	r10, [r5, #3176]
	mov	r6, r2
	str	r3, [sp, #4]
	mov	r9, #20
	ldr	r3, [r5, #3172]
	str	r3, [sp, #12]
	ldrh	r3, [r5, #110]
	str	r3, [sp, #16]
	ldrh	r3, [r5, #112]
	str	r3, [sp, #20]
.L989:
	uxth	r3, r2
	cmp	ip, r3
	bhi	.L993
	mov	r9, #0
	movs	r2, #0
	mov	r1, r6
	ldr	r0, [sp, #4]
	bl	FlashReadPages
.L994:
	uxth	r3, r9
	cmp	r6, r3
	bhi	.L998
	ldr	r3, [sp, #8]
	add	fp, fp, #1
	adds	r4, r4, #1
	uxth	r4, r4
	cmp	r3, fp
	bls	.L999
.L1001:
	ldrh	r3, [r5, #102]
	cmp	r3, r4
	bhi	.L1000
	movs	r0, #0
	b	.L988
.L993:
	ldrh	r3, [lr], #2
	cmp	r3, r8
	beq	.L990
	ldr	r1, [sp, #4]
	orr	r3, r4, r3, lsl #10
	mla	r1, r9, r6, r1
	str	r3, [r1, #4]
	ldr	r3, [sp, #16]
	muls	r3, r6, r3
	it	mi
	addmi	r3, r3, #3
	bic	r3, r3, #3
	mov	r0, r3
	ldr	r3, [sp, #12]
	add	r0, r0, r3
	ldr	r3, [sp, #20]
	muls	r3, r6, r3
	add	r6, r6, #1
	it	mi
	addmi	r3, r3, #3
	uxth	r6, r6
	bic	r3, r3, #3
	str	r0, [r1, #8]
	add	r3, r3, r10
	str	r3, [r1, #12]
.L990:
	adds	r2, r2, #1
	b	.L989
.L998:
	movs	r3, #20
	ldr	r2, [r5, #3832]
	mul	r10, r3, r9
	str	r2, [sp, #16]
	add	r3, r2, r10
	ldr	r1, [r3, #4]
	str	r3, [sp, #12]
	ubfx	r0, r1, #10, #16
	str	r1, [sp, #4]
	bl	P2V_plane
	ldr	r2, [sp, #16]
	ldr	r2, [r2, r10]
	cbnz	r2, .L995
	ldr	r3, [sp, #12]
	add	r9, r9, #1
	ldr	r1, [sp, #4]
	ldr	r3, [r3, #12]
	ldrh	r2, [r3]
	cmp	r2, r8
	bne	.L996
	movs	r3, #1
	str	r3, [r5, #4000]
.L988:
	ldr	r3, .L1009
	movw	r2, #65535
	strb	r0, [r7, #6]
	mov	r1, r4
	strh	r4, [r7, #2]	@ movhi
	strh	r2, [r3, #4]	@ movhi
	mov	r2, r0
	mov	r0, r7
	bl	ftl_sb_update_avl_pages
	b	.L1002
.L996:
	ldr	r2, [r3, #8]
	ldr	r0, [r3, #12]
	bl	FtlGcUpdatePage
	b	.L994
.L995:
	ldrh	r2, [r7]
	movs	r4, #0
	ldr	r3, [r5, #2324]
	strh	r4, [r3, r2, lsl #1]	@ movhi
	ldrh	r0, [r7]
	bl	INSERT_FREE_LIST
	strh	r8, [r7]	@ movhi
	bl	FtlGcPageVarInit
	b	.L997
.L999:
	ldr	r2, .L1009
	ldrh	r3, [r2, #4]
	cmp	r3, r8
	beq	.L1001
	add	r3, r3, fp
	strh	r3, [r2, #4]	@ movhi
	ldrh	r3, [r5, #102]
	cmp	r3, r4
	bls	.L1001
.L1002:
	mov	r0, #-1
	add	sp, sp, #28
	@ sp needed
	pop	{r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L1010:
	.align	2
.L1009:
	.word	.LANCHOR2
	.word	.LANCHOR0
	.fnend
	.size	FtlGcScanTempBlk, .-FtlGcScanTempBlk
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
	push	{r0, r1, r2, r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.save {r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.pad #12
	movs	r5, #0
	ldr	r4, .L1021
	movs	r1, #0
	ldr	r0, [r4, #3280]
	add	r6, r4, #176
	ldr	r3, [r4, #3284]
	ldrh	r2, [r4, #110]
	str	r0, [r4, #3268]
	str	r3, [r4, #3272]
	bl	ftl_memset
.L1013:
	ldrh	r3, [r4, #54]
	cmp	r5, r3
	blt	.L1014
	ldr	r6, [r4, #3272]
	movs	r2, #16
	movs	r1, #255
	ldr	r9, .L1021+12
	ldr	r8, .L1021+4
	movs	r5, #0
	mov	r0, r6
	ldr	r10, .L1021+16
	bl	memset
	movw	r3, #61649
	mov	r7, r5
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
.L1015:
	ldr	r3, [r4, #3280]
	mov	fp, #0
	ldrh	r1, [r4, #152]
	ldrh	r2, [r4, #154]
	str	r3, [r4, #3268]
	ldr	r3, [r4, #3284]
	str	fp, [r4, #3260]
	str	r3, [r4, #3272]
	orr	r3, r2, r1, lsl #10
	ldrh	r0, [r6, #10]
	str	r3, [r4, #3264]
	ldrh	r3, [r4, #156]
	str	r0, [sp]
	mov	r0, r9
	bl	printk
	movs	r3, #1
	ldr	r0, .L1021+4
	mov	r2, r3
	mov	r1, r3
	bl	FlashProgPages
	ldrh	r3, [r4, #104]
	ldrh	r2, [r4, #154]
	subs	r3, r3, #1
	cmp	r2, r3
	blt	.L1016
	ldr	r3, [r4, #160]
	ldrh	r2, [r4, #152]
	ldr	r0, [r4, #3800]
	adds	r3, r3, #1
	strh	fp, [r4, #154]	@ movhi
	str	r3, [r4, #160]
	str	r3, [r6, #4]
	ldrh	r3, [r4, #156]
	strh	r2, [r6, #8]	@ movhi
	strh	r2, [r4, #156]	@ movhi
	movs	r2, #1
	strh	r3, [r4, #152]	@ movhi
	mov	r1, r2
	lsls	r3, r3, #10
	str	r3, [r4, #3264]
	str	r3, [r0, #4]
	bl	FlashEraseBlocks
	movs	r3, #1
	mov	r0, r8
	mov	r2, r3
	mov	r1, r3
	bl	FlashProgPages
.L1016:
	ldrh	r3, [r4, #154]
	adds	r3, r3, #1
	strh	r3, [r4, #154]	@ movhi
	ldr	r3, [r4, #3260]
	adds	r3, r3, #1
	bne	.L1017
	adds	r5, r5, #1
	ldr	r1, [r4, #3264]
	uxth	r5, r5
	mov	r0, r10
	bl	printk
	cmp	r5, #3
	bls	.L1015
	mov	r2, r5
	ldr	r1, [r4, #3264]
	ldr	r0, .L1021+8
	bl	printk
.L1019:
	b	.L1019
.L1014:
	ldrh	r2, [r4, #3900]
	ldr	r3, [r4, #3268]
	ldr	r1, [r6, #4]!
	mul	r0, r2, r5
	lsls	r2, r2, #2
	adds	r5, r5, #1
	add	r0, r3, r0, lsl #2
	bl	ftl_memcpy
	b	.L1013
.L1020:
	movs	r7, #1
	b	.L1015
.L1017:
	cmp	r7, #0
	beq	.L1020
	movs	r0, #0
	add	sp, sp, #12
	@ sp needed
	pop	{r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L1022:
	.align	2
.L1021:
	.word	.LANCHOR0
	.word	.LANCHOR0+3260
	.word	.LC96
	.word	.LC94
	.word	.LC95
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
	ldr	r4, .L1058
	.pad #20
	sub	sp, sp, #20
	ldr	r9, .L1058+20
.L1024:
	ldr	r3, .L1058
	ldr	r2, .L1058
	ldrh	r3, [r3, #2344]
	ldrh	r2, [r2, #2336]
	add	r3, r3, r2
	ldr	r2, .L1058
	ldrh	r2, [r2, #40]
	cmp	r3, r2
	ble	.L1025
	movw	r2, #2585
	mov	r1, r9
	ldr	r0, .L1058+4
	bl	printk
.L1025:
	ldr	r3, .L1058+8
	cmp	r5, r3
	bne	.L1050
	ldrh	r3, [r4, #2344]
	ldr	r1, [r4, #3160]
	mul	r2, r1, r3
	lsrs	r1, r3, #1
	adds	r1, r1, #1
	add	r1, r1, r2, lsr #2
	uxth	r1, r1
	cbz	r1, .L1026
	subs	r1, r1, #1
	uxth	r1, r1
.L1026:
	ldr	r0, .L1058+12
	bl	List_pop_index_node
	ldrh	r3, [r4, #2344]
	mov	r6, r0
	uxth	r8, r0
	cbnz	r3, .L1027
	movw	r2, #2594
	mov	r1, r9
	ldr	r0, .L1058+4
	bl	printk
.L1027:
	ldrh	r3, [r4, #2344]
	mov	r0, r5
	subs	r3, r3, #1
	strh	r3, [r4, #2344]	@ movhi
	strh	r8, [r5]	@ movhi
	bl	make_superblock
	ldrb	r3, [r5, #7]	@ zero_extendqisi2
	cbnz	r3, .L1028
	ldr	r3, [r4, #2324]
	uxth	r6, r6
	movw	r2, #65535
	mov	r0, r8
	strh	r2, [r3, r6, lsl #1]	@ movhi
	bl	INSERT_DATA_LIST
	ldrh	r2, [r4, #2336]
	ldrh	r3, [r4, #2344]
	add	r3, r3, r2
	ldrh	r2, [r4, #40]
	cmp	r3, r2
	ble	.L1024
	movw	r2, #2605
	mov	r1, r9
	ldr	r0, .L1058+4
	bl	printk
	b	.L1024
.L1050:
	movs	r1, #0
	b	.L1026
.L1028:
	ldrh	r2, [r4, #2336]
	ldrh	r3, [r4, #2344]
	add	r3, r3, r2
	ldrh	r2, [r4, #40]
	cmp	r3, r2
	ble	.L1030
	mov	r2, #2608
	mov	r1, r9
	ldr	r0, .L1058+4
	bl	printk
.L1030:
	ldr	r1, [r4, #3800]
	mov	r10, #20
	ldrh	r2, [r4, #32]
	add	fp, r5, #16
	movs	r7, #0
	mov	r0, fp
	mov	r3, r1
	mov	ip, r7
	mla	r2, r10, r2, r1
	str	r2, [sp, #4]
.L1031:
	ldr	r2, [sp, #4]
	cmp	r2, r3
	bne	.L1033
	cbnz	r7, .L1034
	movw	r2, #2619
	mov	r1, r9
	ldr	r0, .L1058+4
	bl	printk
.L1034:
	ldrh	r3, [r4, #2588]
	cmp	r3, r8
	bne	.L1035
	movw	r2, #2621
	mov	r1, r9
	ldr	r0, .L1058+4
	bl	printk
.L1035:
	ldrb	r3, [r5, #8]	@ zero_extendqisi2
	uxth	r6, r6
	ldr	r2, [r4, #2328]
	cmp	r3, #0
	bne	.L1036
	ldrh	r3, [r2, r6, lsl #1]
	cmp	r3, #0
	beq	.L1037
	ldrh	r1, [r4, #92]
	add	r3, r3, r1
.L1057:
	strh	r3, [r2, r6, lsl #1]	@ movhi
	movs	r1, #0
	ldr	r3, [r4, #2548]
	mov	r0, r8
	adds	r3, r3, #1
	str	r3, [r4, #2548]
	bl	ftl_set_blk_mode
.L1039:
	ldr	r3, [r4, #2328]
	ldr	r2, [r4, #2564]
	ldrh	r0, [r4, #92]
	ldrh	r3, [r3, r6, lsl #1]
	ldrh	r1, [r4, #40]
	cmp	r3, r2
	ldr	r2, [r4, #2548]
	it	hi
	strhi	r3, [r4, #2564]
	ldr	r3, [r4, #2552]
	mla	r0, r2, r0, r3
	bl	__aeabi_uidiv
	ldr	r2, [r4, #3860]
	ldr	r1, [r4, #3800]
	str	r0, [r4, #2556]
	ldr	r3, [r2, #16]
	ldr	r0, .L1058+16
	adds	r3, r3, #1
	str	r3, [r2, #16]
	movs	r2, #20
	mla	r2, r2, r7, r1
	adds	r3, r1, #4
	adds	r2, r2, #24
.L1041:
	adds	r3, r3, #20
	cmp	r2, r3
	bne	.L1042
	ldrb	r1, [r5, #8]	@ zero_extendqisi2
	mov	r2, r7
	ldr	r0, [r4, #3800]
	mov	r10, #0
	bl	FlashEraseBlocks
	mov	r3, r10
	movs	r1, #20
.L1043:
	uxth	r2, r10
	cmp	r7, r2
	bhi	.L1045
	cmp	r3, #0
	ble	.L1046
	mov	r0, r8
	bl	update_multiplier_value
	bl	FtlBbmTblFlush
.L1046:
	ldrb	r2, [r5, #7]	@ zero_extendqisi2
	cmp	r2, #0
	bne	.L1047
	ldr	r3, [r4, #2324]
	movw	r2, #65535
	mov	r0, r8
	strh	r2, [r3, r6, lsl #1]	@ movhi
	bl	INSERT_DATA_LIST
	b	.L1024
.L1033:
	str	ip, [r3, #8]
	movw	lr, #65535
	str	ip, [r3, #12]
	ldrh	r2, [r0], #2
	cmp	r2, lr
	beq	.L1032
	mla	lr, r10, r7, r1
	adds	r7, r7, #1
	lsls	r2, r2, #10
	uxth	r7, r7
	str	r2, [lr, #4]
.L1032:
	adds	r3, r3, #20
	b	.L1031
.L1037:
	movs	r3, #2
	b	.L1057
.L1036:
	ldrh	r3, [r2, r6, lsl #1]
	mov	r0, r8
	adds	r3, r3, #1
	strh	r3, [r2, r6, lsl #1]	@ movhi
	ldr	r3, [r4, #2552]
	adds	r3, r3, #1
	str	r3, [r4, #2552]
	bl	ftl_set_blk_mode.part.6
	b	.L1039
.L1042:
	ldr	r1, [r3, #-20]
	ands	r1, r1, r0
	str	r1, [r3, #-20]
	b	.L1041
.L1045:
	mul	r2, r1, r10
	ldr	r0, [r4, #3800]
	add	ip, r0, r2
	ldr	r2, [r0, r2]
	adds	r0, r2, #1
	bne	.L1044
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
.L1044:
	add	r10, r10, #1
	add	fp, fp, #2
	b	.L1043
.L1047:
	ldrh	r3, [r4, #102]
	strh	r8, [r5]	@ movhi
	smulbb	r3, r3, r2
	movs	r2, #0
	strh	r2, [r5, #2]	@ movhi
	strb	r2, [r5, #6]
	ldr	r2, [r4, #2540]
	uxth	r3, r3
	strh	r3, [r5, #4]	@ movhi
	str	r2, [r5, #12]
	adds	r2, r2, #1
	str	r2, [r4, #2540]
	ldr	r2, [r4, #2324]
	ldrh	r1, [r5]
	strh	r3, [r2, r1, lsl #1]	@ movhi
	ldrh	r3, [r5, #4]
	cbz	r3, .L1048
	ldrb	r3, [r5, #7]	@ zero_extendqisi2
	cbnz	r3, .L1049
.L1048:
	movw	r2, #2674
	ldr	r1, .L1058+20
	ldr	r0, .L1058+4
	bl	printk
.L1049:
	movs	r0, #0
	add	sp, sp, #20
	@ sp needed
	pop	{r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L1059:
	.align	2
.L1058:
	.word	.LANCHOR0
	.word	.LC1
	.word	.LANCHOR0+2444
	.word	.LANCHOR0+2340
	.word	-1024
	.word	.LANCHOR1+360
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
	ldr	r4, .L1072
	ldrh	r3, [r4, #3206]
	cbz	r3, .L1061
	movs	r7, #0
	addw	r10, r4, #3208
.L1062:
	ldrh	r2, [r4, #32]
	uxth	r3, r7
	cmp	r2, r3
	bhi	.L1068
	bl	FtlGcReFreshBadBlk
.L1061:
	movs	r0, #0
	pop	{r3, r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L1068:
	uxtah	r3, r4, r7
	mov	r1, r8
	mov	fp, #0
	ldrb	r0, [r3, #60]	@ zero_extendqisi2
	bl	V2P_block
	mov	r9, r0
.L1063:
	ldrh	r3, [r4, #3206]
	uxth	r5, fp
	cmp	r3, r5
	bhi	.L1067
	adds	r7, r7, #1
	b	.L1062
.L1067:
	uxth	r6, fp
	add	r3, r4, r6, lsl #1
	ldrh	r3, [r3, #3208]
	cmp	r3, r9
	bne	.L1064
	mov	r1, r9
	ldr	r0, .L1072+4
	bl	printk
	mov	r0, r9
	bl	FtlBbmMapBadBlock
	bl	FtlBbmTblFlush
	ldrh	r1, [r4, #3206]
	add	r3, r10, r6, lsl #1
.L1065:
	cmp	r5, r1
	bcc	.L1066
	subs	r1, r1, #1
	strh	r1, [r4, #3206]	@ movhi
.L1064:
	add	fp, fp, #1
	b	.L1063
.L1066:
	ldrh	r0, [r3, #2]!
	adds	r5, r5, #1
	uxth	r5, r5
	strh	r0, [r3, #-2]	@ movhi
	b	.L1065
.L1073:
	.align	2
.L1072:
	.word	.LANCHOR0
	.word	.LC97
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
	ldr	r2, .L1084
	push	{r3, r4, r5, lr}
	.save {r3, r4, r5, lr}
	mov	r5, r0
	mov	r4, r2
	ldr	r3, [r2, #2324]
	ldrh	r3, [r3, r0, lsl #1]
	cmp	r3, #0
	bne	.L1075
	ldrh	r1, [r2, #2588]
	cmp	r1, r0
	bne	.L1076
	movw	r3, #65535
	strh	r3, [r2, #2588]	@ movhi
.L1077:
	mov	r1, r5
	ldr	r0, .L1084+4
	bl	List_remove_node
	ldrh	r3, [r4, #2336]
	cbnz	r3, .L1079
	movw	r2, #2744
	ldr	r1, .L1084+8
	ldr	r0, .L1084+12
	bl	printk
.L1079:
	ldrh	r3, [r4, #2336]
	mov	r0, r5
	subs	r3, r3, #1
	strh	r3, [r4, #2336]	@ movhi
	bl	free_data_superblock
	mov	r0, r5
	bl	FtlGcFreeBadSuperBlk
	ldrh	r2, [r4, #2336]
	ldrh	r3, [r4, #2344]
	add	r3, r3, r2
	ldrh	r2, [r4, #40]
	cmp	r3, r2
	ble	.L1083
	movw	r2, #2747
	ldr	r1, .L1084+8
	ldr	r0, .L1084+12
	bl	printk
.L1083:
	movs	r3, #1
	b	.L1074
.L1076:
	ldrh	r1, [r2, #2348]
	cmp	r1, r0
	beq	.L1074
	ldrh	r1, [r2, #2396]
	cmp	r1, r0
	beq	.L1074
	ldrh	r2, [r2, #2444]
	cmp	r2, r0
	bne	.L1077
.L1074:
	mov	r0, r3
	pop	{r3, r4, r5, pc}
.L1075:
	bl	List_update_data_list
	movs	r3, #0
	b	.L1074
.L1085:
	.align	2
.L1084:
	.word	.LANCHOR0
	.word	.LANCHOR0+2320
	.word	.LANCHOR1+385
	.word	.LC1
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
	ldr	r5, .L1095
	beq	.L1087
	ldr	r3, [r5, #2324]
	ldrh	r2, [r3, r0, lsl #1]
	cbnz	r2, .L1088
	mov	r1, r0
	ldr	r0, .L1095+4
	bl	printk
	ldr	r3, [r5, #2324]
	ldrh	r6, [r3, r4, lsl #1]
	cbz	r6, .L1089
.L1093:
	movs	r6, #0
.L1086:
	mov	r0, r6
	pop	{r4, r5, r6, pc}
.L1089:
	movw	r2, #2762
.L1094:
	ldr	r1, .L1095+8
	ldr	r0, .L1095+12
	bl	printk
	b	.L1086
.L1088:
	subs	r2, r2, #1
	strh	r2, [r3, r0, lsl #1]	@ movhi
.L1087:
	ldrh	r0, [r5, #3992]
	movw	r3, #65535
	cmp	r0, r3
	bne	.L1091
	strh	r4, [r5, #3992]	@ movhi
	b	.L1093
.L1091:
	cmp	r4, r0
	beq	.L1093
	bl	update_vpc_list
	ldr	r2, [r5, #2316]
	adds	r6, r0, #0
	ldr	r3, [r5, #2320]
	it	ne
	movne	r6, #1
	strh	r4, [r5, #3992]	@ movhi
	subs	r3, r3, r2
	asrs	r2, r3, #1
	ldr	r3, .L1095+16
	muls	r3, r2, r3
	ldr	r2, [r5, #2324]
	uxth	r1, r3
	ldrh	r2, [r2, r1, lsl #1]
	cmp	r2, #0
	bne	.L1086
	cmp	r4, r1
	beq	.L1086
	movw	r2, #2778
	b	.L1094
.L1096:
	.align	2
.L1095:
	.word	.LANCHOR0
	.word	.LC98
	.word	.LANCHOR1+401
	.word	.LC1
	.word	-1431655765
	.fnend
	.size	decrement_vpc_count, .-decrement_vpc_count
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
	ldr	r4, .L1115
	ldrh	r2, [r4, #2352]
	cmp	r2, #0
	beq	.L1098
	ldrb	r3, [r4, #2356]	@ zero_extendqisi2
	cmp	r3, #0
	bne	.L1098
	ldrb	r1, [r4, #2355]	@ zero_extendqisi2
	ldrh	r3, [r4, #102]
	muls	r3, r1, r3
	cmp	r2, r3
	beq	.L1098
	ldrb	r7, [r4, #2358]	@ zero_extendqisi2
	cbnz	r7, .L1097
	ldr	r6, [r4, #2504]
	mov	r2, r7
	mov	r1, sp
	ldrh	r8, [r4, #32]
	subs	r6, r6, #1
	mov	r0, r6
	bl	log2phys
	ldr	r3, [sp]
	ldr	r5, [r4, #3284]
	ldr	r0, [r4, #3280]
	str	r3, [sp, #8]
	adds	r3, r3, #1
	str	r6, [sp, #20]
	str	r0, [sp, #12]
	str	r5, [sp, #16]
	str	r7, [r5, #4]
	beq	.L1100
	mov	r2, r7
	movs	r1, #1
	add	r0, sp, #4
	bl	FlashReadPages
.L1101:
	ldr	r9, .L1115+4
	movs	r7, #0
	lsl	r8, r8, #2
	mov	r10, r7
	movw	r3, #61589
	strh	r3, [r5]	@ movhi
.L1102:
	cmp	r8, r7
	bne	.L1106
.L1103:
	movs	r3, #1
.L1114:
	strb	r3, [r4, #2358]
.L1097:
	add	sp, sp, #24
	@ sp needed
	pop	{r4, r5, r6, r7, r8, r9, r10, pc}
.L1100:
	ldrh	r2, [r4, #110]
	movs	r1, #255
	bl	ftl_memset
	b	.L1101
.L1106:
	ldrh	r3, [r4, #2352]
	cmp	r3, #0
	beq	.L1103
	ldr	r3, [sp, #8]
	mov	r0, r9
	str	r6, [r5, #8]
	adds	r7, r7, #1
	str	r3, [r5, #12]
	ldrh	r3, [r4, #2348]
	strh	r3, [r5, #2]	@ movhi
	bl	get_new_active_ppa
	ldr	r3, [r4, #2544]
	movs	r1, #1
	str	r0, [sp, #8]
	add	r0, sp, #4
	str	r3, [r5, #4]
	adds	r3, r3, #1
	adds	r2, r3, #1
	it	eq
	moveq	r3, r10
	str	r3, [r4, #2544]
	movs	r3, #0
	mov	r2, r3
	bl	FlashProgPages
	ldrh	r0, [r4, #2348]
	bl	decrement_vpc_count
	b	.L1102
.L1098:
	movs	r3, #0
	b	.L1114
.L1116:
	.align	2
.L1115:
	.word	.LANCHOR0
	.word	.LANCHOR0+2348
	.fnend
	.size	FtlWriteDump_data, .-FtlWriteDump_data
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
	ldr	r5, .L1121
	movs	r6, #12
	bl	FtlWriteDump_data
.L1118:
	ldrh	r3, [r5, #138]
	uxth	r0, r4
	cmp	r3, r0
	bhi	.L1120
	movs	r0, #0
	pop	{r4, r5, r6, pc}
.L1120:
	ldr	r2, [r5, #2492]
	uxth	r3, r4
	mla	r3, r6, r3, r2
	ldr	r3, [r3, #4]
	cmp	r3, #0
	bge	.L1119
	bl	flush_l2p_region
.L1119:
	adds	r4, r4, #1
	b	.L1118
.L1122:
	.align	2
.L1121:
	.word	.LANCHOR0
	.fnend
	.size	l2p_flush, .-l2p_flush
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
	mov	fp, r0
	cmp	r3, r2
	beq	.L1248
	ldrh	r3, [r0, #2]
	ldr	r4, .L1255
	str	r3, [sp, #8]
	ldrb	r3, [r0, #6]	@ zero_extendqisi2
	ldr	r1, [sp, #8]
	str	r3, [sp, #24]
	ldrh	r3, [r4, #102]
	cmp	r3, r1
	mov	r3, #0
	bne	.L1126
	strh	r3, [r0, #4]	@ movhi
.L1253:
	strb	r3, [fp, #6]
.L1248:
	movs	r0, #0
	add	sp, sp, #52
	@ sp needed
	pop	{r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L1126:
	ldrh	r0, [r0, #16]
.L1127:
	cmp	r0, r2
	uxth	r5, r3
	add	r3, r3, #1
	beq	.L1128
	movs	r1, #1
	bl	FtlGetLastWrittenPage
	adds	r7, r0, #1
	mov	r6, r0
	beq	.L1129
	ldr	r3, [r4, #3172]
	movs	r2, #0
	ldrh	lr, [r4, #32]
	movw	r9, #65535
	ldr	r0, [r4, #3832]
	mov	r10, #20
	str	r3, [sp, #4]
	ldrh	r3, [r4, #110]
	ldr	r8, [r4, #3176]
	ldrh	r7, [r4, #112]
	mov	r4, r2
	str	r3, [sp, #12]
	add	r3, fp, #16
	mov	ip, r3
	str	r3, [sp, #20]
.L1130:
	uxth	r3, r2
	cmp	lr, r3
	bhi	.L1136
	ldr	r5, .L1255
	movs	r2, #0
	mov	r1, r4
	movw	r9, #65535
	bl	FlashReadPages
	ldr	r3, [r5, #2544]
	uxth	r2, r6
	ldr	r8, [r5, #3832]
	str	r2, [sp, #12]
	subs	r3, r3, #1
	str	r3, [sp, #4]
	mov	r7, r8
	movs	r3, #0
.L1137:
	uxth	r2, r3
	cmp	r4, r2
	bhi	.L1142
	bne	.L1140
	adds	r3, r6, #1
	uxth	r3, r3
	str	r3, [sp, #16]
.L1249:
	ldr	r0, [r8, #4]
	ubfx	r0, r0, #10, #16
	bl	P2V_plane
	ldrh	r3, [r5, #102]
	ldr	r2, [sp, #16]
	str	r0, [sp, #12]
	cmp	r3, r2
	bne	.L1144
	ldrh	r3, [sp, #16]
	strh	r3, [fp, #2]	@ movhi
	movs	r3, #0
	strb	r3, [fp, #6]
	strh	r3, [fp, #4]	@ movhi
.L1144:
	ldr	r3, [sp, #16]
	ldr	r2, [sp, #8]
	cmp	r3, r2
	bne	.L1145
	ldr	r3, [sp, #12]
	ldr	r2, [sp, #24]
	cmp	r3, r2
	bne	.L1145
	ldr	r1, [sp, #16]
	mov	r2, r3
.L1254:
	mov	r0, fp
	bl	ftl_sb_update_avl_pages
	b	.L1248
.L1128:
	uxth	r1, r3
	adds	r1, r1, #8
	ldrh	r0, [fp, r1, lsl #1]
	b	.L1127
.L1129:
	ldr	r3, [sp, #8]
	cbz	r3, .L1131
	movw	r2, #1755
	ldr	r1, .L1255+4
	ldr	r0, .L1255+8
	bl	printk
.L1131:
	ldr	r3, [sp, #24]
	cbz	r3, .L1132
	cmp	r5, r3
	beq	.L1132
	movw	r2, #1756
	ldr	r1, .L1255+4
	ldr	r0, .L1255+8
	bl	printk
.L1132:
	movs	r3, #0
	strh	r3, [fp, #2]	@ movhi
	b	.L1253
.L1136:
	ldrh	r3, [ip], #2
	cmp	r3, r9
	beq	.L1133
	mla	r1, r10, r4, r0
	orr	r3, r6, r3, lsl #10
	str	r3, [r1, #4]
	ldr	r3, [sp, #12]
	muls	r3, r4, r3
	it	mi
	addmi	r3, r3, #3
	bic	r3, r3, #3
	mov	r5, r3
	ldr	r3, [sp, #4]
	add	r5, r5, r3
	mov	r3, r7
	muls	r3, r4, r3
	add	r4, r4, #1
	it	mi
	addmi	r3, r3, #3
	uxth	r4, r4
	bic	r3, r3, #3
	str	r5, [r1, #8]
	add	r3, r3, r8
	str	r3, [r1, #12]
.L1133:
	adds	r2, r2, #1
	b	.L1130
.L1142:
	ldr	r2, [r7]
	cbnz	r2, .L1138
	ldr	r10, [r7, #12]
	ldr	r2, [r10, #4]
	adds	r0, r2, #1
	beq	.L1139
	ldr	r1, [r5, #2544]
	mov	r0, r2
	bl	ftl_cmp_data_ver
	cbz	r0, .L1139
	adds	r2, r2, #1
	str	r2, [r5, #2544]
.L1139:
	ldr	r2, [r10]
	adds	r2, r2, #1
	bne	.L1141
.L1140:
	uxth	r2, r6
	uxth	r3, r3
	str	r2, [sp, #16]
	movs	r2, #20
	mla	r8, r2, r3, r8
	b	.L1249
.L1138:
	ldr	r9, [sp, #12]
.L1141:
	adds	r3, r3, #1
	adds	r7, r7, #20
	b	.L1137
.L1145:
	movw	r3, #65535
	cmp	r9, r3
	bne	.L1146
	ldrb	r3, [fp, #8]	@ zero_extendqisi2
	cmp	r3, #0
	bne	.L1147
.L1146:
	ldr	r3, [r5, #4004]
	uxth	r8, r6
	uxth	r6, r6
	ldr	r7, .L1255
	movw	r9, #65535
	adds	r3, r3, #1
	itt	eq
	ldreq	r3, [sp, #4]
	streq	r3, [r5, #4004]
	ldr	r3, [sp, #8]
	adds	r3, r3, #7
	cmp	r6, r3
	itet	gt
	subgt	r4, r8, #7
	ldrle	r4, [sp, #8]
	uxthgt	r4, r4
.L1150:
	cmp	r4, r8
	bhi	.L1157
	movs	r3, #0
	ldrh	lr, [r7, #32]
	ldr	r0, [r7, #3832]
	mov	r6, r3
	ldr	r1, [sp, #20]
	mov	ip, #20
	b	.L1158
.L1152:
	ldrh	r2, [r1], #2
	cmp	r2, r9
	beq	.L1151
	mla	r10, ip, r6, r0
	adds	r6, r6, #1
	orr	r2, r4, r2, lsl #10
	uxth	r6, r6
	str	r2, [r10, #4]
.L1151:
	adds	r3, r3, #1
.L1158:
	uxth	r2, r3
	cmp	r2, lr
	bcc	.L1152
	movs	r2, #0
	mov	r1, r6
	bl	FlashReadPages
	ldr	r3, [r7, #3832]
	movs	r2, #20
	mla	r6, r2, r6, r3
.L1153:
	cmp	r6, r3
	bne	.L1156
	adds	r4, r4, #1
	uxth	r4, r4
	b	.L1150
.L1156:
	ldr	r2, [r3]
	cbnz	r2, .L1147
	ldr	r2, [r3, #12]
	ldrh	r1, [r2]
	cmp	r1, r9
	beq	.L1155
	ldr	r2, [r2, #4]
	adds	r1, r2, #1
	it	ne
	strne	r2, [r7, #4004]
.L1155:
	adds	r3, r3, #20
	b	.L1153
.L1157:
	mov	r3, #-1
	str	r3, [r7, #4004]
.L1147:
	movs	r3, #1
	ldr	r0, .L1255+12
	strh	r3, [r5, #4008]	@ movhi
	ldr	r4, .L1255
	bl	FtlMapBlkWriteDump_data
	ldr	r5, [sp, #8]
.L1159:
	ldrh	r6, [r4, #32]
	movs	r3, #0
	ldr	r0, [r4, #3832]
	mov	r10, r3
	ldr	r1, [sp, #20]
	movw	r7, #65535
	mov	ip, #20
.L1160:
	uxth	r2, r3
	cmp	r6, r2
	bhi	.L1162
	movs	r2, #0
	mov	r1, r10
	bl	FlashReadPages
	movs	r3, #0
.L1252:
	str	r3, [sp, #28]
	ldrh	r3, [sp, #28]
	cmp	r10, r3
	bhi	.L1192
	adds	r5, r5, #1
	ldrh	r3, [r4, #102]
	uxth	r5, r5
	cmp	r3, r5
	bne	.L1159
	ldrh	r2, [r4, #32]
	movw	r0, #65535
	movs	r3, #0
	strh	r5, [fp, #2]	@ movhi
	strh	r3, [fp, #4]	@ movhi
.L1193:
	uxth	r1, r3
	cmp	r1, r2
	bcs	.L1248
	ldr	r1, [sp, #20]
	ldrh	r4, [r1], #2
	cmp	r4, r0
	str	r1, [sp, #20]
	add	r1, r3, #1
	bne	.L1253
	mov	r3, r1
	b	.L1193
.L1162:
	ldrh	r2, [r1], #2
	cmp	r2, r7
	beq	.L1161
	mla	lr, ip, r10, r0
	add	r10, r10, #1
	orr	r2, r5, r2, lsl #10
	uxth	r10, r10
	str	r2, [lr, #4]
.L1161:
	adds	r3, r3, #1
	b	.L1160
.L1256:
	.align	2
.L1255:
	.word	.LANCHOR0
	.word	.LANCHOR1+421
	.word	.LC1
	.word	.LANCHOR0+3904
.L1192:
	ldr	r3, [sp, #28]
	movs	r7, #20
	ldr	r9, [r4, #3832]
	muls	r7, r3, r7
	add	r8, r9, r7
	ldr	r6, [r8, #4]
	ubfx	r0, r6, #10, #16
	str	r6, [sp, #44]
	bl	P2V_plane
	ldr	r3, [sp, #8]
	cmp	r5, r3
	bcc	.L1164
	bne	.L1165
	ldr	r3, [sp, #24]
	cmp	r3, r0
	bhi	.L1164
.L1165:
	ldr	r3, [sp, #16]
	cmp	r5, r3
	bne	.L1166
	ldr	r3, [sp, #12]
	cmp	r3, r0
	beq	.L1167
.L1166:
	ldr	r3, [r9, r7]
	adds	r3, r3, #1
	beq	.L1168
	ldr	r8, [r8, #12]
	movw	r3, #61589
	ldrh	r2, [r8]
	cmp	r2, r3
	beq	.L1169
.L1176:
	ldrh	r0, [fp]
.L1251:
	bl	decrement_vpc_count
.L1164:
	ldr	r3, [sp, #28]
	adds	r3, r3, #1
	b	.L1252
.L1169:
	ldr	r3, [r8, #4]
	adds	r6, r3, #1
	str	r3, [sp, #4]
	beq	.L1170
	ldr	r1, [r4, #2544]
	mov	r0, r3
	bl	ftl_cmp_data_ver
	cbz	r0, .L1170
	ldr	r3, [sp, #4]
	adds	r3, r3, #1
	str	r3, [r4, #2544]
.L1170:
	ldrh	r2, [r8]
	movw	r3, #61589
	cmp	r2, r3
	beq	.L1171
	movw	r2, #1905
	ldr	r1, .L1257
	ldr	r0, .L1257+4
	bl	printk
.L1171:
	ldr	r6, [r8, #8]
	add	r1, sp, #40
	ldr	r3, [r8, #12]
	movs	r2, #0
	mov	r0, r6
	str	r3, [sp, #36]
	bl	log2phys
	ldr	r1, [r4, #4004]
	adds	r0, r1, #1
	beq	.L1172
	ldr	r0, [sp, #4]
	bl	ftl_cmp_data_ver
	cmp	r0, #0
	beq	.L1172
	ldr	r3, [sp, #36]
	adds	r2, r3, #1
	beq	.L1173
	ldr	r0, [r4, #3832]
	movs	r2, #0
	movs	r1, #1
	add	r0, r0, r7
	str	r3, [r0, #4]
	ldr	r8, [r0, #12]
	bl	FlashReadPages
	ldr	r2, [r4, #3832]
	ldr	r1, [r2, r7]
	adds	r3, r2, r7
	adds	r1, r1, #1
	bne	.L1174
.L1175:
	mov	r3, #-1
	ldrh	r0, [fp]
	str	r3, [sp, #36]
	bl	decrement_vpc_count
.L1183:
	ldr	r0, [sp, #36]
	adds	r1, r0, #1
	beq	.L1164
.L1196:
	ubfx	r0, r0, #10, #16
	bl	P2V_block_in_plane
	ldrh	r3, [r4, #40]
	mov	r6, r0
	cmp	r3, r0
	bhi	.L1188
	movw	r2, #2006
	ldr	r1, .L1257
	ldr	r0, .L1257+4
	bl	printk
.L1188:
	ldr	r3, [r4, #2324]
	ldrh	r3, [r3, r6, lsl #1]
	cmp	r3, #0
	beq	.L1189
	mov	r0, r6
	b	.L1251
.L1173:
	ldr	r3, [sp, #44]
	ldr	r2, [sp, #40]
	cmp	r2, r3
	bne	.L1176
	movs	r2, #1
	add	r1, sp, #36
	mov	r0, r6
	bl	log2phys
	b	.L1176
.L1174:
	ldr	r1, [r8, #8]
	cmp	r6, r1
	bne	.L1175
	ldr	r9, [r8, #4]
	ldr	r0, [r4, #4004]
	mov	r1, r9
	bl	ftl_cmp_data_ver
	cmp	r0, #0
	beq	.L1175
	ldr	r1, [sp, #40]
	ldr	r0, [sp, #44]
	cmp	r1, r0
	bne	.L1178
.L1181:
	ldr	r1, [sp, #36]
	mov	r0, r6
	bl	FtlReUsePrevPpa
	b	.L1175
.L1178:
	ldr	r0, [sp, #36]
	cmp	r1, r0
	beq	.L1175
	adds	r0, r1, #1
	beq	.L1179
	str	r1, [r3, #4]
	movs	r2, #0
	movs	r1, #1
	mov	r0, r3
	ldr	r8, [r3, #12]
	bl	FlashReadPages
.L1180:
	ldr	r3, [r4, #3832]
	ldr	r3, [r3, r7]
	adds	r3, r3, #1
	beq	.L1181
	ldr	r3, [r8, #4]
	ldr	r0, [r4, #4004]
	mov	r1, r3
	bl	ftl_cmp_data_ver
	cmp	r0, #0
	beq	.L1181
	mov	r1, r3
	mov	r0, r9
	bl	ftl_cmp_data_ver
	cmp	r0, #0
	beq	.L1175
	b	.L1181
.L1179:
	str	r1, [r2, r7]
	b	.L1180
.L1172:
	ldr	r3, [sp, #44]
	ldr	r2, [sp, #40]
	cmp	r2, r3
	beq	.L1183
	movs	r2, #1
	add	r1, sp, #44
	mov	r0, r6
	bl	log2phys
	ldr	r8, [sp, #40]
	cmp	r8, #-1
	beq	.L1183
	ldr	r3, [sp, #36]
	cmp	r8, r3
	beq	.L1186
	ubfx	r0, r8, #10, #16
	bl	P2V_block_in_plane
	ldrh	r3, [r4, #2348]
	cmp	r3, r0
	beq	.L1187
	ldrh	r3, [r4, #2396]
	cmp	r3, r0
	beq	.L1187
	ldrh	r3, [r4, #2444]
	cmp	r3, r0
	bne	.L1183
.L1187:
	ldr	r0, [r4, #3832]
	movs	r2, #0
	movs	r1, #1
	str	r8, [r0, #4]
	ldr	r7, [r0, #12]
	bl	FlashReadPages
	ldr	r3, [r4, #3832]
	ldr	r3, [r3]
	adds	r3, r3, #1
	beq	.L1183
	ldr	r1, [r7, #4]
	ldr	r0, [sp, #4]
	bl	ftl_cmp_data_ver
	cmp	r0, #0
	bne	.L1183
	movs	r2, #1
	add	r1, sp, #40
	mov	r0, r6
	bl	log2phys
	b	.L1183
.L1189:
	mov	r1, r6
	ldr	r0, .L1257+8
	bl	printk
	b	.L1164
.L1168:
	ldr	r3, [r4, #4012]
	cmp	r3, #31
	itttt	ls
	addls	r2, r4, r3, lsl #2
	addls	r3, r3, #1
	strls	r3, [r4, #4012]
	strls	r6, [r2, #4016]
	ldrh	r0, [fp]
	bl	decrement_vpc_count
	ldr	r3, [r4, #4004]
	adds	r2, r3, #1
	bne	.L1191
	ldr	r3, [sp, #4]
.L1250:
	str	r3, [r4, #4004]
	b	.L1164
.L1191:
	ldr	r2, [sp, #4]
	cmp	r2, r3
	bcs	.L1164
	mov	r3, r2
	b	.L1250
.L1167:
	ldrb	r3, [sp, #12]	@ zero_extendqisi2
	mov	r1, r5
	strh	r5, [fp, #2]	@ movhi
	ldr	r2, [sp, #12]
	strb	r3, [fp, #6]
	b	.L1254
.L1186:
	mov	r0, r8
	b	.L1196
.L1258:
	.align	2
.L1257:
	.word	.LANCHOR1+421
	.word	.LC1
	.word	.LC99
	.fnend
	.size	FtlRecoverySuperblock, .-FtlRecoverySuperblock
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
	ldr	r4, .L1270
	mov	r3, #-1
	mov	r8, #0
	movw	r1, #61589
	str	r3, [sp, #20]
	mov	r5, r0
	ldr	r2, [r4, #3280]
	movs	r7, #7
	ldr	r6, [r4, #3284]
	str	r2, [sp, #12]
	mvn	r2, #2
	str	r6, [sp, #16]
	str	r2, [r6, #8]
	mvn	r2, #1
	str	r2, [r6, #12]
	ldrh	r2, [r0]
	strh	r8, [r6]	@ movhi
	strh	r2, [r6, #2]	@ movhi
	ldr	r2, [r4, #3280]
	str	r1, [r2]
	add	r1, r1, #304087040
	ldr	r2, [r4, #3280]
	add	r1, r1, #1269760
	addw	r1, r1, #1507
	str	r1, [r2, #4]
.L1260:
	subs	r7, r7, #1
	beq	.L1262
	ldrh	r3, [r5, #4]
	cbnz	r3, .L1261
.L1262:
	ldr	r2, [r4, #2324]
	ldrh	r1, [r5]
	ldrh	r0, [r5, #4]
	ldrh	r3, [r2, r1, lsl #1]
	subs	r3, r3, r0
	strh	r3, [r2, r1, lsl #1]	@ movhi
	ldrh	r3, [r4, #102]
	strh	r3, [r5, #2]	@ movhi
	movs	r3, #0
	strb	r3, [r5, #6]
	strh	r3, [r5, #4]	@ movhi
	add	sp, sp, #24
	@ sp needed
	pop	{r4, r5, r6, r7, r8, pc}
.L1261:
	mov	r0, r5
	bl	get_new_active_ppa
	str	r0, [sp, #8]
	adds	r0, r0, #1
	beq	.L1262
	ldr	r3, [r4, #2544]
	movs	r1, #1
	add	r0, sp, #4
	str	r3, [r6, #4]
	adds	r3, r3, #1
	adds	r2, r3, #1
	it	eq
	moveq	r3, r8
	str	r3, [r4, #2544]
	movs	r3, #0
	mov	r2, r3
	bl	FlashProgPages
	ldrh	r0, [r5]
	bl	decrement_vpc_count
	b	.L1260
.L1271:
	.align	2
.L1270:
	.word	.LANCHOR0
	.fnend
	.size	FtlSuperblockPowerLostFix, .-FtlSuperblockPowerLostFix
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
	ldr	r4, .L1301
	ldr	r3, [r4, #3280]
	addw	r7, r4, #3260
	ldr	r6, [r4, #3284]
	str	r3, [r4, #3268]
	str	r6, [r4, #3272]
	bl	FtlBbtMemInit
	ldrh	r5, [r4, #98]
	subs	r5, r5, #1
	uxth	r5, r5
.L1273:
	ldrh	r3, [r4, #98]
	subs	r3, r3, #16
	cmp	r5, r3
	ble	.L1276
	lsls	r3, r5, #10
	movs	r2, #1
	mov	r1, r2
	mov	r0, r7
	str	r3, [r4, #3264]
	bl	FlashReadPages
	ldr	r3, [r4, #3260]
	adds	r3, r3, #1
	bne	.L1274
	ldr	r3, [r4, #3264]
	movs	r2, #1
	mov	r1, r2
	mov	r0, r7
	adds	r3, r3, #1
	str	r3, [r4, #3264]
	bl	FlashReadPages
.L1274:
	ldr	r3, [r4, #3260]
	adds	r3, r3, #1
	beq	.L1275
	ldrh	r3, [r6]
	cmp	r3, r8
	bne	.L1275
	ldr	r3, [r6, #4]
	strh	r5, [r4, #152]	@ movhi
	str	r3, [r4, #160]
	ldrh	r3, [r6, #8]
	strh	r3, [r4, #156]	@ movhi
.L1276:
	ldrh	r3, [r4, #152]
	movw	r2, #65535
	cmp	r3, r2
	beq	.L1290
	ldrh	r3, [r4, #156]
	cmp	r3, r2
	beq	.L1280
	lsls	r3, r3, #10
	movs	r2, #1
	mov	r1, r2
	ldr	r0, .L1301+4
	str	r3, [r4, #3264]
	bl	FlashReadPages
	ldr	r3, [r4, #3260]
	adds	r3, r3, #1
	beq	.L1280
	ldrh	r2, [r6]
	movw	r3, #61649
	cmp	r2, r3
	bne	.L1280
	ldr	r3, [r6, #4]
	ldr	r2, [r4, #160]
	cmp	r3, r2
	bls	.L1280
	ldrh	r2, [r4, #156]
	str	r3, [r4, #160]
	ldrh	r3, [r6, #8]
	strh	r2, [r4, #152]	@ movhi
	strh	r3, [r4, #156]	@ movhi
.L1280:
	ldr	r7, .L1301+4
	movs	r1, #1
	ldrh	r0, [r4, #152]
	movw	r8, #61649
	bl	FtlGetLastWrittenPage
	sxth	r5, r0
	adds	r0, r0, #1
	strh	r0, [r4, #154]	@ movhi
.L1282:
	cmp	r5, #0
	bge	.L1285
	movs	r2, #254
	ldr	r1, .L1301+8
	ldr	r0, .L1301+12
	bl	printk
.L1284:
	ldrh	r3, [r6, #10]
	ldrh	r0, [r6, #12]
	strh	r3, [r4, #158]	@ movhi
	movw	r3, #65535
	cmp	r0, r3
	beq	.L1287
	ldr	r2, [r4, #28]
	cmp	r0, r2
	beq	.L1287
	ldrh	r3, [r4, #42]
	lsrs	r3, r3, #2
	cmp	r2, r3
	bcs	.L1287
	cmp	r0, r3
	bcs	.L1287
	bl	FtlSysBlkNumInit
.L1287:
	ldr	r6, .L1301+16
	movs	r5, #0
.L1288:
	ldrh	r3, [r4, #54]
	cmp	r5, r3
	bcc	.L1289
	movs	r0, #0
	pop	{r4, r5, r6, r7, r8, pc}
.L1275:
	subs	r5, r5, #1
	uxth	r5, r5
	b	.L1273
.L1285:
	ldrh	r3, [r4, #152]
	movs	r2, #1
	mov	r1, r2
	mov	r0, r7
	orr	r3, r5, r3, lsl #10
	str	r3, [r4, #3264]
	ldr	r3, [r4, #3280]
	str	r3, [r4, #3268]
	bl	FlashReadPages
	ldr	r3, [r4, #3260]
	adds	r3, r3, #1
	beq	.L1283
	ldrh	r3, [r6]
	cmp	r3, r8
	beq	.L1284
.L1283:
	subs	r5, r5, #1
	sxth	r5, r5
	b	.L1282
.L1289:
	ldrh	r2, [r4, #3900]
	ldr	r1, [r4, #3268]
	ldr	r0, [r6, #4]!
	lsls	r2, r2, #2
	mla	r1, r5, r2, r1
	adds	r5, r5, #1
	bl	ftl_memcpy
	b	.L1288
.L1290:
	mov	r0, #-1
	pop	{r4, r5, r6, r7, r8, pc}
.L1302:
	.align	2
.L1301:
	.word	.LANCHOR0
	.word	.LANCHOR0+3260
	.word	.LANCHOR1+443
	.word	.LC1
	.word	.LANCHOR0+176
	.fnend
	.size	FtlLoadBbt, .-FtlLoadBbt
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
	ldr	r9, .L1322+4
	bl	FtlBbtMemInit
	sub	r5, r9, #180
	sub	r10, r9, #18
	mov	r4, r5
	bl	FtlLoadFactoryBbt
.L1304:
	ldrh	r3, [r5, #54]
	cmp	r8, r3
	bcc	.L1310
	movs	r5, #0
.L1311:
	ldrh	r3, [r4, #114]
	uxth	r0, r5
	adds	r5, r5, #1
	cmp	r3, r0
	bhi	.L1312
	ldrh	r5, [r4, #164]
	movw	r6, #65535
	subs	r5, r5, #1
	uxth	r5, r5
.L1313:
	ldrh	r3, [r4, #164]
	subs	r3, r3, #48
	cmp	r5, r3
	ble	.L1317
	mov	r0, r5
	bl	FtlBbmIsBadBlock
	cmp	r0, #1
	beq	.L1314
	mov	r0, r5
	bl	FlashTestBlk
	cmp	r0, #0
	beq	.L1315
	mov	r0, r5
	bl	FtlBbmMapBadBlock
.L1314:
	subs	r5, r5, #1
	uxth	r5, r5
	b	.L1313
.L1310:
	ldrh	r3, [r10, #2]!
	movw	r2, #65535
	ldr	r0, [r5, #3280]
	ldr	fp, [r5, #3284]
	cmp	r3, r2
	str	r0, [r5, #3268]
	str	fp, [r5, #3272]
	beq	.L1305
	ldrh	r7, [r5, #98]
	movs	r2, #1
	mov	r1, r2
	ldr	r0, .L1322
	mla	r7, r8, r7, r3
	lsls	r3, r7, #10
	str	r3, [r5, #3264]
	bl	FlashReadPages
	ldrh	r2, [r5, #98]
	ldr	r1, [r5, #3268]
	ldr	r0, [r9]
	adds	r2, r2, #7
	asrs	r2, r2, #3
	bl	ftl_memcpy
.L1306:
	uxth	r0, r7
	add	r8, r8, #1
	add	r9, r9, #4
	bl	FtlBbmMapBadBlock
	b	.L1304
.L1305:
	mov	r1, r8
	bl	FlashGetBadBlockList
	ldr	r1, [r9]
	ldr	r0, [r5, #3268]
	bl	FtlBbt2Bitmap
	ldrh	r6, [r5, #98]
.L1308:
	subs	r6, r6, #1
	uxth	r6, r6
.L1307:
	ldrh	r0, [r4, #98]
	smlabb	r0, r0, r8, r6
	uxth	r0, r0
	bl	FtlBbmIsBadBlock
	cmp	r0, #1
	beq	.L1308
	movs	r1, #16
	ldr	r0, [r4, #3284]
	strh	r6, [r10]	@ movhi
	bl	__memzero
	mov	r1, #4096
	ldr	r0, [r4, #3280]
	bl	__memzero
	movw	r3, #61664
	strh	r3, [fp]	@ movhi
	movs	r3, #0
	str	r3, [fp, #4]
	ldrh	r3, [r10]
	ldrh	r7, [r4, #98]
	ldrh	r2, [r4, #3900]
	strh	r3, [fp, #2]	@ movhi
	ldrh	r3, [r10]
	ldr	r1, [r9]
	lsls	r2, r2, #2
	ldr	r0, [r4, #3268]
	mla	r7, r8, r7, r3
	lsls	r3, r7, #10
	str	r3, [r4, #3264]
	bl	ftl_memcpy
	movs	r2, #1
	ldr	r0, .L1322
	mov	r1, r2
	bl	FlashEraseBlocks
	movs	r3, #1
	ldr	r0, .L1322
	mov	r2, r3
	mov	r1, r3
	bl	FlashProgPages
	ldr	r3, [r4, #3260]
	adds	r3, r3, #1
	bne	.L1306
	uxth	r0, r7
	bl	FtlBbmMapBadBlock
	b	.L1307
.L1312:
	bl	FtlBbmMapBadBlock
	b	.L1311
.L1315:
	ldrh	r3, [r4, #152]
	cmp	r3, r6
	bne	.L1316
	strh	r5, [r4, #152]	@ movhi
	b	.L1314
.L1316:
	strh	r5, [r4, #156]	@ movhi
.L1317:
	ldrh	r3, [r4, #152]
	movs	r5, #0
	ldr	r0, [r4, #3800]
	movs	r1, #1
	str	r5, [r4, #160]
	movs	r2, #2
	strh	r5, [r4, #154]	@ movhi
	lsls	r3, r3, #10
	str	r3, [r0, #4]
	ldrh	r3, [r4, #156]
	lsls	r3, r3, #10
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
	adds	r3, r3, #1
	str	r3, [r4, #160]
	ldrh	r3, [r4, #152]
	strh	r2, [r4, #152]	@ movhi
	strh	r3, [r4, #156]	@ movhi
	bl	FtlBbmTblFlush
	mov	r0, r5
	pop	{r3, r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L1323:
	.align	2
.L1322:
	.word	.LANCHOR0+3260
	.word	.LANCHOR0+180
	.fnend
	.size	FtlMakeBbt, .-FtlMakeBbt
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
	ldr	r4, .L1336
	adds	r2, r0, r1
	.pad #108
	sub	sp, sp, #108
	mov	r8, r0
	mov	r6, r1
	ldrh	r3, [r4, #96]
	cmp	r2, r3
	bhi	.L1332
	ldrh	r7, [r4, #108]
	mov	r9, #0
	lsr	r7, r0, r7
	lsl	fp, r7, #2
.L1326:
	cbnz	r6, .L1331
.L1324:
	mov	r0, r9
	add	sp, sp, #108
	@ sp needed
	pop	{r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L1331:
	ldr	r3, [r4, #3884]
	mov	r0, r8
	ldr	r2, [r3, fp]
	ldrh	r3, [r4, #58]
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
	cbz	r2, .L1328
	cmp	r5, r3
	beq	.L1328
	ldr	r3, [r4, #3848]
	add	r0, sp, #20
	str	r2, [sp, #24]
	movs	r2, #1
	mov	r1, r2
	str	r3, [sp, #28]
	add	r3, sp, #40
	str	r3, [sp, #32]
	bl	FlashReadPages
.L1329:
	lsls	r3, r5, #9
	ldr	r0, [r4, #3848]
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
	ldr	r2, [r4, #3848]
	ldr	r0, .L1336+4
	adds	r7, r7, #1
	bl	FtlMapWritePage
	ldr	r3, [sp, #8]
	adds	r0, r0, #1
	it	eq
	moveq	r9, #-1
	add	r10, r10, r3
	b	.L1326
.L1328:
	ldrh	r2, [r4, #110]
	movs	r1, #0
	ldr	r0, [r4, #3848]
	bl	ftl_memset
	b	.L1329
.L1332:
	mov	r9, #-1
	b	.L1324
.L1337:
	.align	2
.L1336:
	.word	.LANCHOR0
	.word	.LANCHOR0+3948
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
	ldr	r2, .L1340
	ldr	r3, .L1340+4
	ldr	r1, [r2, #2636]
	cmp	r1, r3
	bne	.L1338
	ldr	r3, .L1340+8
	addw	r2, r2, #2636
	movs	r1, #1
	movs	r0, #0
	str	r3, [r2, #4]
	ldr	r3, [r2, #-104]
	str	r3, [r2, #88]
	ldr	r3, [r2, #-100]
	str	r3, [r2, #92]
	ldr	r3, [r2, #-108]
	str	r3, [r2, #8]
	ldr	r3, [r2, #-120]
	str	r3, [r2, #12]
	ldr	r3, [r2, #-128]
	str	r3, [r2, #16]
	ldr	r3, [r2, #-112]
	str	r3, [r2, #20]
	ldr	r3, [r2, #-84]
	str	r3, [r2, #28]
	ldr	r3, [r2, #-76]
	str	r3, [r2, #32]
	ldr	r3, [r2, #-124]
	str	r3, [r2, #36]
	ldr	r3, [r2, #-116]
	str	r3, [r2, #40]
	ldr	r3, [r2, #-72]
	str	r3, [r2, #44]
	ldr	r3, [r2, #-68]
	str	r3, [r2, #48]
	b	FtlVendorPartWrite
.L1338:
	bx	lr
.L1341:
	.align	2
.L1340:
	.word	.LANCHOR0
	.word	1179929683
	.word	1342177348
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
	ldr	r2, .L1347
	push	{r3, lr}
	.save {r3, lr}
	ldrh	r3, [r2, #-200]
	cmp	r3, #31
	itett	ls
	addls	r3, r3, #1
	movhi	r2, #32
	strhls	r3, [r2, #-200]	@ movhi
	movls	r2, #1
	ldr	r3, .L1347+4
	cbnz	r0, .L1344
	ldr	r1, [r3, #3860]
	ldr	r0, [r1, #20]
	ldr	r1, [r1, #16]
	add	r2, r2, r0
	cmp	r1, r2
	bcc	.L1345
.L1344:
	ldr	r2, [r3, #3860]
	movs	r0, #64
	ldr	r1, [r2, #16]
	str	r1, [r2, #20]
	ldr	r1, .L1347+8
	str	r1, [r2]
	ldrh	r1, [r3, #3852]
	ldr	r2, [r3, #3860]
	lsls	r3, r1, #9
	str	r3, [r2, #12]
	ldr	r3, [r2, #8]
	adds	r3, r3, #1
	str	r3, [r2, #8]
	movs	r3, #0
	str	r3, [r2, #4]
	bl	FtlVendorPartWrite
	bl	Ftl_save_ext_data
.L1345:
	movs	r0, #0
	pop	{r3, pc}
.L1348:
	.align	2
.L1347:
	.word	.LANCHOR3
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
	@ args = 0, pretend = 0, frame = 96
	@ frame_needed = 0, uses_anonymous_args = 0
	push	{r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.save {r4, r5, r6, r7, r8, r9, r10, fp, lr}
	mov	r10, r2
	ldr	r5, .L1359
	adds	r2, r0, r1
	.pad #100
	sub	sp, sp, #100
	mov	r8, r0
	mov	r7, r1
	ldrh	r3, [r5, #96]
	cmp	r2, r3
	bhi	.L1358
	ldrh	r6, [r5, #108]
	mov	r9, #0
	lsr	r6, r0, r6
	lsls	r6, r6, #2
.L1352:
	cbnz	r7, .L1357
.L1350:
	mov	r0, r9
	add	sp, sp, #100
	@ sp needed
	pop	{r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L1357:
	ldr	r3, [r5, #3884]
	mov	r0, r8
	ldrh	r4, [r5, #58]
	ldr	r3, [r3, r6]
	mov	r1, r4
	str	r3, [sp, #4]
	bl	__aeabi_uidivmod
	subs	r4, r4, r1
	ldr	r3, [sp, #4]
	uxth	r4, r4
	str	r1, [sp]
	cmp	r7, r4
	it	cc
	uxthcc	r4, r7
	lsl	fp, r4, #9
	cbz	r3, .L1354
	str	r3, [sp, #16]
	movs	r2, #1
	ldr	r3, [r5, #3848]
	mov	r1, r2
	add	r0, sp, #12
	str	r3, [sp, #20]
	add	r3, sp, #32
	str	r3, [sp, #24]
	bl	FlashReadPages
	ldr	r3, [sp, #12]
	mov	r2, fp
	ldr	r1, [r5, #3848]
	mov	r0, r10
	adds	r3, r3, #1
	ldr	r3, [sp]
	it	eq
	moveq	r9, #-1
	add	r1, r1, r3, lsl #9
	bl	ftl_memcpy
.L1356:
	subs	r7, r7, r4
	add	r8, r8, r4
	add	r10, r10, fp
	adds	r6, r6, #4
	b	.L1352
.L1354:
	mov	r2, fp
	mov	r1, r3
	mov	r0, r10
	bl	ftl_memset
	b	.L1356
.L1358:
	mov	r9, #-1
	b	.L1350
.L1360:
	.align	2
.L1359:
	.word	.LANCHOR0
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
	ldr	r4, .L1363
	ldr	r2, [r4, #3860]
	ldrh	r1, [r4, #3852]
	bl	FtlVendorPartRead
	ldr	r3, [r4, #3860]
	ldr	r2, [r3]
	ldr	r3, .L1363+4
	cmp	r2, r3
	beq	.L1362
	ldr	r1, .L1363+8
	ldr	r0, .L1363+12
	bl	printk
	ldrh	r2, [r4, #3852]
	movs	r1, #0
	ldr	r0, [r4, #3860]
	lsls	r2, r2, #9
	bl	ftl_memset
.L1362:
	movs	r0, #0
	pop	{r4, pc}
.L1364:
	.align	2
.L1363:
	.word	.LANCHOR0
	.word	1112818501
	.word	.LC100
	.word	.LC71
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
	ldr	r4, .L1368
	movs	r0, #0
	ldr	r5, .L1368+4
	addw	r2, r4, #2636
	bl	FtlVendorPartRead
	ldr	r3, [r4, #2636]
	cmp	r3, r5
	beq	.L1366
	mov	r1, #512
	addw	r0, r4, #2636
	bl	__memzero
	str	r5, [r4, #2636]
.L1366:
	ldr	r3, [r4, #2636]
	cmp	r3, r5
	bne	.L1367
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
.L1367:
	ldrh	r2, [r4, #92]
	ldr	r3, [r4, #2552]
	ldr	r0, [r4, #2548]
	ldrh	r1, [r4, #40]
	mla	r0, r0, r2, r3
	bl	__aeabi_uidiv
	str	r0, [r4, #2556]
	pop	{r3, r4, r5, pc}
.L1369:
	.align	2
.L1368:
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
	ldr	r4, .L1383
	movs	r1, #255
	movw	r7, #65535
	ldr	r9, .L1383+16
	ldr	r3, [r4, #3280]
	addw	r8, r4, #3260
	ldr	r6, [r4, #3284]
	ldr	r10, .L1383+20
	str	r3, [r4, #3268]
	ldrh	r3, [r4, #2576]
	str	r6, [r4, #3272]
	str	r5, [r6, #12]
	strh	r3, [r6, #2]	@ movhi
	movw	r3, #61604
	strh	r3, [r6]	@ movhi
	ldr	r3, [r4, #2584]
	str	r5, [r6, #8]
	ldrh	r2, [r4, #2350]
	str	r3, [r6, #4]
	ldr	r3, .L1383+4
	str	r3, [r4, #2268]
	ldr	r3, .L1383+8
	str	r3, [r4, #2272]
	ldrh	r3, [r4, #2582]
	strh	r3, [r4, #2276]	@ movhi
	ldrh	r3, [r4, #54]
	strb	r3, [r4, #2278]
	ldrh	r3, [r4, #2348]
	strh	r3, [r4, #2282]	@ movhi
	ldrb	r3, [r4, #2354]	@ zero_extendqisi2
	orr	r3, r3, r2, lsl #6
	ldrh	r2, [r4, #2398]
	strh	r3, [r4, #2284]	@ movhi
	ldrb	r3, [r4, #2356]	@ zero_extendqisi2
	strb	r3, [r4, #2279]
	ldrh	r3, [r4, #2396]
	strh	r3, [r4, #2286]	@ movhi
	ldrb	r3, [r4, #2402]	@ zero_extendqisi2
	orr	r3, r3, r2, lsl #6
	strh	r3, [r4, #2288]	@ movhi
	ldrb	r3, [r4, #2404]	@ zero_extendqisi2
	strb	r3, [r4, #2280]
	ldrh	r3, [r4, #2444]
	ldrh	r2, [r4, #2446]
	ldr	r0, [r4, #3268]
	strh	r3, [r4, #2290]	@ movhi
	ldrb	r3, [r4, #2450]	@ zero_extendqisi2
	orr	r3, r3, r2, lsl #6
	ldrh	r2, [r4, #110]
	strh	r3, [r4, #2292]	@ movhi
	ldrb	r3, [r4, #2452]	@ zero_extendqisi2
	strb	r3, [r4, #2281]
	ldr	r3, [r4, #2548]
	str	r3, [r4, #2300]
	ldr	r3, [r4, #2540]
	str	r3, [r4, #2308]
	ldr	r3, [r4, #2544]
	str	r3, [r4, #2304]
	bl	ftl_memset
	movs	r2, #48
	addw	r1, r4, #2268
	ldr	r0, [r4, #3268]
	bl	ftl_memcpy
	ldrh	r2, [r4, #40]
	ldr	r0, [r4, #3268]
	ldr	r1, [r4, #2324]
	lsls	r2, r2, #1
	adds	r0, r0, #48
	bl	ftl_memcpy
	ldrh	r0, [r4, #40]
	ldr	r3, [r4, #3268]
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
.L1372:
	ldr	r3, [r4, #3280]
	mov	r0, r8
	ldrh	r2, [r4, #2576]
	str	r3, [r4, #3268]
	ldr	r3, [r4, #3284]
	str	r3, [r4, #3272]
	ldrh	r3, [r4, #2578]
	orr	r3, r3, r2, lsl #10
	str	r3, [r4, #3264]
	movs	r3, #1
	mov	r2, r3
	mov	r1, r3
	bl	FlashProgPages
	ldrh	r3, [r4, #104]
	ldrh	r2, [r4, #2578]
	subs	r3, r3, #1
	cmp	r2, r3
	blt	.L1373
	ldrh	r3, [r4, #2576]
	ldrh	r7, [r4, #2580]
	strh	r3, [r4, #2580]	@ movhi
	movs	r3, #0
	strh	r3, [r4, #2578]	@ movhi
	bl	FtlFreeSysBlkQueueOut
	ldr	r3, [r4, #2540]
	strh	r0, [r4, #2576]	@ movhi
	adds	r2, r3, #1
	str	r3, [r4, #2584]
	str	r2, [r4, #2540]
	lsls	r2, r0, #10
	str	r2, [r4, #3264]
	str	r3, [r6, #4]
	movs	r3, #1
	strh	r0, [r6, #2]	@ movhi
	mov	r2, r3
	mov	r1, r3
	mov	r0, r8
	bl	FlashProgPages
.L1373:
	ldrh	r3, [r4, #2578]
	ldr	r2, [r4, #3260]
	adds	r3, r3, #1
	uxth	r3, r3
	adds	r2, r2, #1
	strh	r3, [r4, #2578]	@ movhi
	bne	.L1374
	cmp	r3, #1
	bne	.L1375
	movw	r2, #1124
	mov	r1, r9
	mov	r0, r10
	bl	printk
.L1375:
	ldrh	r3, [r4, #2578]
	adds	r5, r5, #1
	uxth	r5, r5
	cmp	r3, #1
	ittt	eq
	ldrheq	r3, [r4, #104]
	addeq	r3, r3, #-1
	strheq	r3, [r4, #2578]	@ movhi
	cmp	r5, #3
	bls	.L1372
	mov	r2, r5
	ldr	r1, [r4, #3264]
	ldr	r0, .L1383+12
	bl	printk
.L1378:
	b	.L1378
.L1374:
	cmp	r3, #1
	beq	.L1372
	movw	r3, #65535
	cmp	r7, r3
	beq	.L1379
	movs	r1, #1
	mov	r0, r7
	bl	FtlFreeSysBlkQueueIn
.L1379:
	movs	r0, #0
	pop	{r4, r5, r6, r7, r8, r9, r10, pc}
.L1384:
	.align	2
.L1383:
	.word	.LANCHOR0
	.word	1179929683
	.word	1342177348
	.word	.LC101
	.word	.LANCHOR1+454
	.word	.LC1
	.fnend
	.size	FtlVpcTblFlush, .-FtlVpcTblFlush
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
	ldr	r4, .L1397
	ldr	r1, .L1397+4
	ldr	r0, .L1397+8
	bl	printk
	ldrh	r2, [r4, #42]
	movs	r1, #0
	ldr	r0, [r4, #3864]
	lsls	r2, r2, #1
	bl	ftl_memset
.L1386:
	ldr	r3, [r4, #2504]
	cmp	r5, r3
	bcc	.L1388
	ldr	r9, .L1397+12
	movs	r7, #0
	movw	r8, #65535
.L1389:
	ldrh	r3, [r4, #40]
	uxth	r5, r7
	cmp	r3, r5
	bhi	.L1393
	add	sp, sp, #12
	@ sp needed
	pop	{r4, r5, r6, r7, r8, r9, pc}
.L1388:
	movs	r2, #0
	add	r1, sp, #4
	mov	r0, r5
	bl	log2phys
	ldr	r0, [sp, #4]
	adds	r3, r0, #1
	beq	.L1387
	ubfx	r0, r0, #10, #16
	bl	P2V_block_in_plane
	ldr	r2, [r4, #3864]
	ldrh	r3, [r2, r0, lsl #1]
	adds	r3, r3, #1
	strh	r3, [r2, r0, lsl #1]	@ movhi
.L1387:
	adds	r5, r5, #1
	b	.L1386
.L1393:
	ldr	r3, [r4, #2324]
	uxth	r6, r7
	ldrh	r2, [r3, r6, lsl #1]
	ldr	r3, [r4, #3864]
	ldrh	r3, [r3, r6, lsl #1]
	cmp	r2, r3
	beq	.L1391
	cmp	r2, r8
	beq	.L1391
	mov	r1, r6
	mov	r0, r9
	bl	printk
	ldrh	r3, [r4, #2348]
	cmp	r3, r5
	beq	.L1391
	ldrh	r3, [r4, #2444]
	cmp	r3, r5
	beq	.L1391
	ldrh	r3, [r4, #2396]
	cmp	r3, r5
	beq	.L1391
	ldr	r3, [r4, #3864]
	mov	r0, r5
	ldrh	r2, [r3, r6, lsl #1]
	ldr	r3, [r4, #2324]
	strh	r2, [r3, r6, lsl #1]	@ movhi
	bl	update_vpc_list
	bl	l2p_flush
	bl	FtlVpcTblFlush
.L1391:
	adds	r7, r7, #1
	b	.L1389
.L1398:
	.align	2
.L1397:
	.word	.LANCHOR0
	.word	.LANCHOR1+469
	.word	.LC91
	.word	.LC102
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
	ldr	r4, .L1409
	ldrh	r5, [r0]
	ldrh	r3, [r4, #40]
	cmp	r3, r5
	bcs	.L1400
	movw	r2, #2681
	ldr	r1, .L1409+4
	ldr	r0, .L1409+8
	bl	printk
.L1400:
	movw	r3, #65535
	cmp	r5, r3
	beq	.L1401
	ldr	r3, [r4, #2324]
	mov	r0, r5
	ldrh	r3, [r3, r5, lsl #1]
	cbz	r3, .L1402
	bl	INSERT_DATA_LIST
.L1401:
	ldrh	r0, [r4, #3992]
	movs	r3, #1
	strb	r3, [r6, #8]
	movw	r3, #65535
	cmp	r0, r3
	beq	.L1403
	cmp	r5, r0
	bne	.L1404
	ldr	r3, [r4, #2324]
	ldrh	r3, [r3, r0, lsl #1]
	cbz	r3, .L1405
.L1404:
	bl	update_vpc_list
.L1405:
	movw	r3, #65535
	strh	r3, [r4, #3992]	@ movhi
.L1403:
	mov	r0, r6
	bl	allocate_data_superblock
	bl	l2p_flush
	movs	r0, #0
	bl	FtlEctTblFlush
	bl	FtlVpcTblFlush
	movs	r0, #0
	pop	{r4, r5, r6, pc}
.L1402:
	bl	INSERT_FREE_LIST
	b	.L1401
.L1410:
	.align	2
.L1409:
	.word	.LANCHOR0
	.word	.LANCHOR1+490
	.word	.LC1
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
	movs	r7, #0
	ldr	r6, .L1425
	.pad #20
	sub	sp, sp, #20
	mov	r4, r3
	movs	r2, #0
	ldr	r10, .L1425+8
	mov	r5, r0
	ldr	fp, .L1425+12
	mov	r9, r1
	ldrb	r3, [r3, #9]	@ zero_extendqisi2
	bl	FlashProgPages
.L1412:
	cmp	r7, r9
	beq	.L1419
	mov	r8, #0
	b	.L1420
.L1415:
	ldr	r0, [r5, #4]
	ubfx	r0, r0, #10, #16
	bl	P2V_block_in_plane
	ldrh	r3, [r4]
	cmp	r3, r0
	bne	.L1413
	ldr	r1, [r6, #2324]
	ldrh	r0, [r4, #4]
	ldrh	r2, [r1, r3, lsl #1]
	subs	r2, r2, r0
	strh	r2, [r1, r3, lsl #1]	@ movhi
	ldrh	r3, [r6, #102]
	strb	r8, [r4, #6]
	strh	r8, [r4, #4]	@ movhi
	strh	r3, [r4, #2]	@ movhi
.L1413:
	ldrh	r3, [r4, #4]
	cbnz	r3, .L1414
	mov	r0, r4
	bl	allocate_new_data_superblock
.L1414:
	ldr	r3, [r6, #2732]
	adds	r3, r3, #1
	str	r3, [r6, #2732]
	ldr	r0, [r5, #4]
	ubfx	r0, r0, #10, #16
	bl	FtlGcMarkBadPhyBlk
	mov	r0, r4
	bl	get_new_active_ppa
	movs	r2, #0
	str	r0, [r5, #4]
	str	r0, [sp, #12]
	movs	r1, #1
	ldrb	r3, [r4, #9]	@ zero_extendqisi2
	mov	r0, r5
	bl	FlashProgPages
.L1420:
	ldr	r3, [r5]
	adds	r3, r3, #1
	beq	.L1415
	ldrb	r2, [r4, #6]	@ zero_extendqisi2
	ldrh	r3, [r6, #32]
	cmp	r2, r3
	bcc	.L1416
	movw	r2, #933
	mov	r1, r10
	mov	r0, fp
	bl	printk
.L1416:
	ldr	r3, [r5, #4]
	add	r1, sp, #16
	movs	r2, #1
	ldr	r0, [r5, #16]
	str	r3, [r1, #-4]!
	bl	log2phys
	ldr	r3, [r5, #12]
	ldr	r3, [r3, #12]
	ubfx	r0, r3, #10, #16
	str	r3, [sp, #4]
	bl	P2V_block_in_plane
	ldr	r3, [sp, #4]
	mov	r8, r0
	adds	r3, r3, #1
	beq	.L1417
	ldr	r3, [r6, #2324]
	ldrh	r2, [r3, r0, lsl #1]
	cbnz	r2, .L1418
	mov	r1, r0
	ldr	r0, .L1425+4
	bl	printk
.L1418:
	mov	r0, r8
	bl	decrement_vpc_count
.L1417:
	adds	r7, r7, #1
	adds	r5, r5, #20
	b	.L1412
.L1419:
	ldrb	r2, [r4, #6]	@ zero_extendqisi2
	ldrh	r3, [r6, #32]
	cmp	r2, r3
	bcc	.L1411
	mov	r2, #948
	ldr	r1, .L1425+8
	ldr	r0, .L1425+12
	bl	printk
.L1411:
	add	sp, sp, #20
	@ sp needed
	pop	{r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L1426:
	.align	2
.L1425:
	.word	.LANCHOR0
	.word	.LC103
	.word	.LANCHOR1+519
	.word	.LC1
	.fnend
	.size	FtlProgPages, .-FtlProgPages
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
	push	{r4, lr}
	.save {r4, lr}
	ldr	r4, .L1432
	ldr	r0, [r4, #3988]
	cbz	r0, .L1428
	ldr	r3, .L1432+4
	movs	r2, #0
	movs	r1, #1
	ldr	r3, [r3, #-196]
	bl	FtlProgPages
	movs	r3, #0
	str	r3, [r4, #3988]
.L1428:
	movs	r0, #0
	pop	{r4, pc}
.L1433:
	.align	2
.L1432:
	.word	.LANCHOR0
	.word	.LANCHOR3
	.fnend
	.size	FtlCacheWriteBack, .-FtlCacheWriteBack
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
	bl	FtlCacheWriteBack
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
	ldr	r3, .L1437
	ldr	r3, [r3]
	cmp	r3, #1
	bne	.L1436
	bl	FtlSysFlush
.L1436:
	movs	r0, #0
	pop	{r3, pc}
.L1438:
	.align	2
.L1437:
	.word	.LANCHOR2
	.fnend
	.size	sftl_deinit, .-sftl_deinit
	.align	1
	.global	FtlDiscard
	.syntax unified
	.thumb
	.thumb_func
	.fpu softvfp
	.type	FtlDiscard, %function
FtlDiscard:
	.fnstart
	@ args = 0, pretend = 0, frame = 8
	@ frame_needed = 0, uses_anonymous_args = 0
	push	{r0, r1, r4, r5, r6, r7, r8, lr}
	.save {r4, r5, r6, r7, r8, lr}
	.pad #8
	adds	r2, r0, r1
	ldr	r6, .L1454
	mov	r7, r0
	mov	r5, r1
	ldr	r3, [r6, #140]
	cmp	r2, r3
	bhi	.L1447
	cmp	r1, #31
	bhi	.L1441
.L1446:
	movs	r0, #0
.L1439:
	add	sp, sp, #8
	@ sp needed
	pop	{r4, r5, r6, r7, r8, pc}
.L1441:
	bl	FtlCacheWriteBack
	ldrh	r4, [r6, #58]
	mov	r0, r7
	mov	r1, r4
	bl	__aeabi_uidiv
	smulbb	r3, r0, r4
	mov	r8, r0
	subs	r7, r7, r3
	uxth	r7, r7
	cbz	r7, .L1442
	subs	r4, r4, r7
	add	r8, r0, #1
	cmp	r4, r5
	it	cs
	movcs	r4, r5
	uxth	r4, r4
	subs	r5, r5, r4
.L1442:
	ldr	r4, .L1454+4
	mov	r3, #-1
	str	r3, [sp, #4]
.L1443:
	ldrh	r3, [r6, #58]
	cmp	r5, r3
	bcs	.L1445
	ldr	r3, .L1454+4
	ldr	r2, [r3, #-192]
	cmp	r2, #32
	bls	.L1446
	movs	r4, #0
	str	r4, [r3, #-192]
	bl	l2p_flush
	bl	FtlVpcTblFlush
	b	.L1446
.L1445:
	movs	r2, #0
	mov	r1, sp
	mov	r0, r8
	bl	log2phys
	ldr	r3, [sp]
	adds	r3, r3, #1
	beq	.L1444
	ldr	r3, [r4, #-192]
	movs	r2, #1
	add	r1, sp, #4
	mov	r0, r8
	adds	r3, r3, #1
	str	r3, [r4, #-192]
	ldr	r3, [r6, #2512]
	adds	r3, r3, #1
	str	r3, [r6, #2512]
	bl	log2phys
	ldr	r0, [sp]
	ubfx	r0, r0, #10, #16
	bl	P2V_block_in_plane
	bl	decrement_vpc_count
.L1444:
	ldrh	r3, [r6, #58]
	add	r8, r8, #1
	subs	r5, r5, r3
	b	.L1443
.L1447:
	mov	r0, #-1
	b	.L1439
.L1455:
	.align	2
.L1454:
	.word	.LANCHOR0
	.word	.LANCHOR3
	.fnend
	.size	FtlDiscard, .-FtlDiscard
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
	push	{r0, r1, r2, r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.save {r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.pad #12
	movw	r2, #65535
	ldr	r4, .L1482
	ldrh	r5, [r4, #2444]
	ldrh	r1, [r4, #102]
	cmp	r5, r2
	bne	.L1457
.L1467:
	ldrh	r2, [r4, #2444]
	movs	r3, #0
	str	r3, [r4, #4000]
	movw	r3, #65535
	cmp	r2, r3
	bne	.L1480
.L1458:
	movs	r0, #0
.L1456:
	add	sp, sp, #12
	@ sp needed
	pop	{r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L1457:
	cbz	r0, .L1460
	ldr	r3, .L1482+4
	ldrh	r0, [r3, #4]
	cmp	r0, r2
	beq	.L1461
.L1462:
	movs	r1, #2
.L1460:
	ldr	r0, .L1482+8
	bl	FtlGcScanTempBlk
	str	r0, [sp, #4]
	adds	r0, r0, #1
	beq	.L1463
	ldr	r2, [r4, #2328]
	ldrh	r3, [r2, r5, lsl #1]
	cmp	r3, #4
	bls	.L1464
	subs	r3, r3, #5
	movs	r0, #1
	strh	r3, [r2, r5, lsl #1]	@ movhi
	bl	FtlEctTblFlush
.L1464:
	ldr	r3, [r4, #4000]
	cbnz	r3, .L1465
	ldr	r3, [r4, #2732]
	ldr	r0, [sp, #4]
	adds	r3, r3, #1
	ubfx	r0, r0, #10, #16
	str	r3, [r4, #2732]
	bl	FtlBbmMapBadBlock
	bl	FtlBbmTblFlush
.L1465:
	movs	r3, #0
	str	r3, [r4, #4000]
.L1476:
	movs	r0, #1
	b	.L1456
.L1461:
	movs	r2, #0
	strh	r2, [r3, #4]	@ movhi
	ldrh	r3, [r4, #2344]
	cmp	r3, #17
	bhi	.L1462
	b	.L1460
.L1463:
	ldr	r3, .L1482+4
	ldrh	r2, [r3, #4]
	movw	r3, #65535
	cmp	r2, r3
	bne	.L1476
	b	.L1467
.L1480:
	bl	FtlCacheWriteBack
	ldrb	r1, [r4, #2451]	@ zero_extendqisi2
	ldrh	r3, [r4, #102]
	ldrh	r2, [r4, #3200]
	muls	r3, r1, r3
	cmp	r2, r3
	beq	.L1468
	movs	r2, #162
	ldr	r1, .L1482+12
	ldr	r0, .L1482+16
	bl	printk
.L1468:
	ldrb	r3, [r4, #2451]	@ zero_extendqisi2
	movs	r6, #0
	ldrh	r0, [r4, #102]
	mov	r9, #12
	ldr	r2, [r4, #2324]
	ldrh	r1, [r4, #2444]
	ldr	r10, .L1482+12
	smulbb	r3, r3, r0
	ldr	fp, .L1482+16
	strh	r3, [r2, r1, lsl #1]	@ movhi
	ldr	r2, [r4, #2528]
	ldrh	r3, [r4, #3200]
	add	r3, r3, r2
	str	r3, [r4, #2528]
.L1469:
	ldrh	r2, [r4, #3200]
	uxth	r3, r6
	cmp	r2, r3
	bhi	.L1473
	movw	r0, #65535
	bl	decrement_vpc_count
	ldrh	r0, [r4, #2444]
	ldr	r3, [r4, #2324]
	ldrh	r3, [r3, r0, lsl #1]
	cmp	r3, #0
	beq	.L1474
	bl	INSERT_DATA_LIST
.L1475:
	movs	r5, #0
	movw	r6, #65535
	strh	r6, [r4, #2444]	@ movhi
	strh	r5, [r4, #3200]	@ movhi
	strh	r5, [r4, #3192]	@ movhi
	bl	l2p_flush
	bl	FtlVpcTblFlush
	ldrh	r3, [r4, #2572]
	ldrh	r2, [r4, #2344]
	add	r3, r3, r3, lsl #1
	cmp	r2, r3, asr #2
	ble	.L1458
	movs	r3, #20
	strh	r6, [r4, #2588]	@ movhi
	strh	r3, [r4, #3148]	@ movhi
	b	.L1458
.L1473:
	uxth	r7, r6
	ldr	r8, [r4, #3196]
	ldr	r3, [r4, #2504]
	mul	r7, r9, r7
	add	r5, r8, r7
	ldr	r2, [r5, #8]
	cmp	r2, r3
	bcc	.L1470
	movs	r2, #168
	mov	r1, r10
	mov	r0, fp
	bl	printk
.L1470:
	movs	r2, #0
	add	r1, sp, #4
	ldr	r0, [r5, #8]
	bl	log2phys
	ldr	r0, [r8, r7]
	ldr	r3, [sp, #4]
	cmp	r0, r3
	bne	.L1471
	ubfx	r0, r0, #10, #16
	bl	P2V_block_in_plane
	movs	r2, #1
	mov	r7, r0
	adds	r1, r5, #4
	ldr	r0, [r5, #8]
	bl	log2phys
	mov	r0, r7
.L1481:
	bl	decrement_vpc_count
.L1472:
	adds	r6, r6, #1
	b	.L1469
.L1471:
	ldr	r2, [r5, #4]
	cmp	r3, r2
	beq	.L1472
	ldrh	r0, [r4, #2444]
	b	.L1481
.L1474:
	bl	INSERT_FREE_LIST
	b	.L1475
.L1483:
	.align	2
.L1482:
	.word	.LANCHOR0
	.word	.LANCHOR2
	.word	.LANCHOR0+2444
	.word	.LANCHOR1+532
	.word	.LC1
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
	ldr	r4, .L1486
	ldrh	r1, [r4, #102]
	addw	r0, r4, #2444
	bl	FtlGcScanTempBlk
	ldrh	r2, [r4, #2446]
	ldrh	r3, [r4, #102]
	cmp	r2, r3
	bcc	.L1484
	add	r0, r4, #3904
	bl	FtlMapBlkWriteDump_data
	movs	r0, #0
	bl	FtlGcFreeTempBlock
	movs	r3, #0
	str	r3, [r4, #4000]
.L1484:
	pop	{r4, pc}
.L1487:
	.align	2
.L1486:
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
	ldr	r4, .L1489
	addw	r6, r4, #2348
	str	r5, [r4, #4012]
	mov	r0, r6
	addw	r4, r4, #2396
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
.L1490:
	.align	2
.L1489:
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
	ldr	r4, .L1498
	movs	r5, #0
	movs	r6, #20
	mov	r2, r3
	ldr	r1, [r4, #3164]
	ldr	r0, [r4, #3836]
	bl	FlashProgPages
.L1492:
	ldr	r1, [r4, #3164]
	uxth	r3, r5
	cmp	r1, r3
	bhi	.L1495
	ldr	r0, [r4, #3836]
	bl	FtlGcBufFree
	ldrh	r3, [r4, #2448]
	movs	r0, #0
	str	r0, [r4, #3164]
	cbnz	r3, .L1491
	movs	r0, #1
	bl	FtlGcFreeTempBlock
	b	.L1497
.L1495:
	muls	r3, r6, r3
	ldr	r2, [r4, #3836]
	adds	r5, r5, #1
	adds	r1, r2, r3
	ldr	r2, [r2, r3]
	adds	r0, r2, #1
	bne	.L1493
	ldrh	r0, [r4, #2444]
	movs	r5, #0
	ldr	r1, [r4, #2324]
	strh	r5, [r1, r0, lsl #1]	@ movhi
	strh	r2, [r4, #2444]	@ movhi
	ldr	r2, [r4, #2732]
	adds	r2, r2, #1
	str	r2, [r4, #2732]
	ldr	r2, [r4, #3836]
	add	r3, r3, r2
	ldr	r0, [r3, #4]
	ubfx	r0, r0, #10, #16
	bl	FtlBbmMapBadBlock
	bl	FtlBbmTblFlush
	bl	FtlGcPageVarInit
.L1497:
	movs	r0, #1
.L1491:
	pop	{r4, r5, r6, pc}
.L1493:
	ldr	r3, [r1, #12]
	ldr	r1, [r1, #4]
	ldr	r2, [r3, #8]
	ldr	r0, [r3, #12]
	bl	FtlGcUpdatePage
	b	.L1492
.L1499:
	.align	2
.L1498:
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
	ldr	r4, .L1503
	ldrh	r2, [r4, #2444]
	cmp	r2, r3
	beq	.L1501
	ldrh	r3, [r4, #2448]
	cbnz	r3, .L1502
.L1501:
	bl	FtlCacheWriteBack
	movs	r0, #0
	movs	r5, #0
	bl	FtlGcFreeTempBlock
	ldr	r0, .L1503+4
	strb	r5, [r4, #2452]
	bl	allocate_data_superblock
	strh	r5, [r4, #3192]	@ movhi
	strh	r5, [r4, #3200]	@ movhi
	bl	l2p_flush
	mov	r0, r5
	bl	FtlEctTblFlush
	bl	FtlVpcTblFlush
.L1502:
	ldr	r0, .L1503+4
	pop	{r3, r4, r5, lr}
	b	get_new_active_ppa
.L1504:
	.align	2
.L1503:
	.word	.LANCHOR0
	.word	.LANCHOR0+2444
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
	@ args = 0, pretend = 0, frame = 24
	@ frame_needed = 0, uses_anonymous_args = 0
	ldr	r3, .L1624
	push	{r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.save {r4, r5, r6, r7, r8, r9, r10, fp, lr}
	mov	r7, r0
	.pad #36
	sub	sp, sp, #36
	mov	r4, r3
	ldr	r0, [r3, #3820]
	cmp	r0, #0
	bne	.L1580
	ldrh	r2, [r3, #2336]
	cmp	r2, #47
	bls	.L1505
	ldr	r2, .L1624+4
	ldrh	r1, [r2, #4]
	movw	r2, #65535
	cmp	r1, r2
	bne	.L1507
.L1510:
	ldrh	r2, [r4, #3204]
	movw	r1, #65535
	cmp	r2, r1
	bne	.L1508
.L1509:
	ldr	r3, [r4, #3152]
	movw	r5, #65535
	ldrh	r1, [r4, #2588]
	adds	r3, r3, #1
	add	r3, r3, r7, lsl #7
	cmp	r1, r5
	str	r3, [r4, #3152]
	bne	.L1511
	ldrh	r2, [r4, #2444]
	cmp	r2, r1
	bne	.L1512
	ldrh	r8, [r4, #3202]
	cmp	r8, r2
	bne	.L1513
	ldrh	r2, [r4, #2344]
	cmp	r2, #24
	ite	cc
	movcc	r2, #5120
	movcs	r2, #1024
	cmp	r3, r2
	bls	.L1513
	ldr	r3, .L1624+8
	movs	r6, #0
	str	r6, [r4, #3152]
	strh	r6, [r3, #-188]	@ movhi
	bl	GetSwlReplaceBlock
	cmp	r0, r8
	mov	r5, r0
	bne	.L1584
	ldrh	r2, [r4, #2344]
	ldrh	r3, [r4, #3150]
	cmp	r2, r3
	bcs	.L1516
	movs	r0, #64
	bl	List_get_gc_head_node
	uxth	r3, r0
	cmp	r3, r5
	beq	.L1518
	mov	r0, r3
	ldr	r3, [r4, #2324]
	ldrh	r3, [r3, r0, lsl #1]
	cmp	r3, #7
	bhi	.L1519
	mov	r0, r6
	bl	List_get_gc_head_node
	uxth	r6, r0
	movs	r3, #128
	strh	r3, [r4, #3150]	@ movhi
	cmp	r6, r5
	beq	.L1518
.L1515:
	ldr	r3, [r4, #2324]
	mov	r5, r6
	ldrh	r1, [r4, #3148]
	ldrh	r2, [r4, #2344]
	ldrh	r3, [r3, r6, lsl #1]
	str	r1, [sp, #4]
	ldr	r1, [r4, #2328]
	ldr	r0, .L1624+12
	ldrh	r1, [r1, r6, lsl #1]
	str	r1, [sp]
	mov	r1, r6
	bl	printk
	b	.L1518
.L1507:
	ldrh	r3, [r3, #2444]
	cmp	r3, r2
	beq	.L1510
	movs	r0, #1
	bl	FtlGcFreeTempBlock
	cmp	r0, #0
	beq	.L1510
	movs	r0, #1
	b	.L1505
.L1508:
	ldrh	r3, [r4, #3202]
	cmp	r3, r1
	itt	eq
	strheq	r2, [r4, #3202]	@ movhi
	strheq	r3, [r4, #3204]	@ movhi
	b	.L1509
.L1519:
	movs	r3, #64
.L1617:
	strh	r3, [r4, #3150]	@ movhi
.L1518:
	bl	FtlGcReFreshBadBlk
	cmp	r7, #0
	bne	.L1521
	movw	r3, #65535
	cmp	r5, r3
	bne	.L1521
.L1578:
	ldrh	r3, [r4, #2344]
	cmp	r3, #24
	bhi	.L1585
	cmp	r3, #16
	ldrh	r6, [r4, #102]
	bls	.L1523
	lsrs	r6, r6, #5
.L1522:
	ldrh	r2, [r4, #3148]
	cmp	r2, r3
	bcs	.L1526
	ldrh	r3, [r4, #2444]
	movw	r2, #65535
	cmp	r3, r2
	bne	.L1527
	ldrh	r2, [r4, #3202]
	cmp	r2, r3
	bne	.L1527
	ldr	r3, .L1624+8
	ldrh	r0, [r3, #-188]
	cbnz	r0, .L1528
	ldr	r3, [r4, #2504]
	ldr	r2, [r4, #2500]
	add	r3, r3, r3, lsl #1
	cmp	r2, r3, lsr #2
	bcs	.L1529
.L1528:
	ldrh	r3, [r4, #2572]
	add	r3, r3, r3, lsl #1
	asrs	r3, r3, #2
.L1618:
	strh	r3, [r4, #3148]	@ movhi
	movs	r3, #0
	str	r3, [r4, #3160]
.L1505:
	add	sp, sp, #36
	@ sp needed
	pop	{r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L1516:
	movs	r3, #80
	b	.L1617
.L1584:
	mov	r6, r0
	b	.L1515
.L1523:
	cmp	r3, #12
	bls	.L1524
	lsrs	r6, r6, #4
	b	.L1522
.L1524:
	cmp	r3, #8
	bls	.L1522
	lsrs	r6, r6, #2
	b	.L1522
.L1585:
	movs	r6, #1
	b	.L1522
.L1529:
	movs	r3, #18
	b	.L1618
.L1527:
	ldrh	r3, [r4, #2572]
	add	r3, r3, r3, lsl #1
	asrs	r3, r3, #2
	strh	r3, [r4, #3148]	@ movhi
.L1526:
	ldrh	r3, [r4, #3206]
	cbz	r3, .L1586
	adds	r6, r6, #32
	uxth	r6, r6
.L1586:
	movw	r5, #65535
.L1532:
	ldrh	r3, [r4, #2588]
	movw	r2, #65535
	cmp	r3, r2
	bne	.L1541
	cmp	r5, r3
	beq	.L1542
	strh	r5, [r4, #2588]	@ movhi
.L1543:
	ldrh	r0, [r4, #2588]
	movw	r7, #65535
	movs	r3, #0
	strb	r3, [r4, #2596]
	cmp	r0, r7
	beq	.L1541
	bl	IsBlkInGcList
	cbz	r0, .L1546
	strh	r7, [r4, #2588]	@ movhi
.L1546:
	ldrh	r2, [r4, #2588]
	movw	r3, #65535
	cmp	r2, r3
	beq	.L1541
	ldr	r0, .L1624+16
	bl	make_superblock
	ldr	r2, .L1624+8
	movs	r3, #0
	strh	r3, [r4, #2590]	@ movhi
	strb	r3, [r4, #2594]
	strh	r3, [r2, #-186]	@ movhi
	ldrh	r1, [r4, #2588]
	ldr	r3, [r4, #2324]
	ldrh	r3, [r3, r1, lsl #1]
	strh	r3, [r2, #-184]	@ movhi
.L1541:
	ldrh	r3, [r4, #2588]
	ldrh	r2, [r4, #2348]
	cmp	r2, r3
	beq	.L1547
	ldrh	r2, [r4, #2396]
	cmp	r2, r3
	beq	.L1547
	ldr	r7, .L1624+8
.L1548:
	ldrh	r9, [r4, #2588]
	movw	r3, #65535
	cmp	r9, r3
	bne	.L1549
	movs	r3, #0
	mov	r10, r3
	str	r3, [r4, #3160]
.L1550:
	ldrh	r8, [r4, #3156]
	mov	r0, r8
	bl	List_get_gc_head_node
	uxth	fp, r0
	cmp	fp, r9
	strh	fp, [r4, #2588]	@ movhi
	bne	.L1551
	movs	r3, #0
	movs	r0, #8
	strh	r3, [r4, #3156]	@ movhi
	b	.L1505
.L1533:
	movs	r3, #0
	ldrh	r7, [r4, #2344]
	str	r3, [r4, #3160]
	ldrh	r3, [r4, #3148]
	ldr	r5, .L1624+8
	cmp	r3, r7
	bcs	.L1534
	ldrh	r3, [r5, #-188]
	cbnz	r3, .L1535
	ldr	r3, [r4, #2504]
	ldr	r2, [r4, #2500]
	add	r3, r3, r3, lsl #1
	cmp	r2, r3, lsr #2
	bcs	.L1536
.L1535:
	ldrh	r3, [r4, #2572]
	add	r3, r3, r3, lsl #1
	asrs	r3, r3, #2
.L1619:
	strh	r3, [r4, #3148]	@ movhi
.L1621:
	bl	FtlReadRefresh
	ldrh	r0, [r5, #-188]
	b	.L1505
.L1536:
	movs	r3, #18
	b	.L1619
.L1534:
	ldrh	r0, [r5, #-188]
	cmp	r0, #0
	bne	.L1538
	ldrh	r6, [r4, #2572]
	add	r3, r6, r6, lsl #1
	asrs	r3, r3, #2
	strh	r3, [r4, #3148]	@ movhi
	bl	List_get_gc_head_node
	ldr	r3, [r4, #2324]
	uxth	r0, r0
	ldrh	r1, [r4, #104]
	ldrh	r2, [r3, r0, lsl #1]
	ldrh	r3, [r4, #32]
	muls	r3, r1, r3
	add	r3, r3, r3, lsr #31
	cmp	r2, r3, asr #1
	ble	.L1539
	subs	r6, r6, #1
	cmp	r7, r6
	bge	.L1621
.L1539:
	cmp	r2, #0
	bne	.L1538
	movw	r0, #65535
	bl	decrement_vpc_count
	ldrh	r0, [r4, #2344]
	adds	r0, r0, #1
	b	.L1505
.L1542:
	ldrh	r3, [r4, #3202]
	cmp	r3, r5
	beq	.L1543
	ldr	r2, [r4, #2324]
	ldrh	r3, [r2, r3, lsl #1]
	cbnz	r3, .L1544
	strh	r5, [r4, #3202]	@ movhi
.L1544:
	ldrh	r3, [r4, #3202]
	strh	r3, [r4, #2588]	@ movhi
	movw	r3, #65535
	strh	r3, [r4, #3202]	@ movhi
	b	.L1543
.L1625:
	.align	2
.L1624:
	.word	.LANCHOR0
	.word	.LANCHOR2
	.word	.LANCHOR3
	.word	.LC104
	.word	.LANCHOR0+2588
.L1547:
	movw	r3, #65535
	strh	r3, [r4, #2588]	@ movhi
.L1622:
	ldr	r3, .L1626
	ldrh	r0, [r3, #-188]
	b	.L1505
.L1551:
	str	r0, [sp, #8]
	mov	r0, fp
	bl	IsBlkInGcList
	add	r8, r8, #1
	ldr	r3, [sp, #8]
	cbz	r0, .L1552
	strh	r8, [r4, #3156]	@ movhi
	b	.L1550
.L1552:
	uxth	r0, r3
	ldrh	ip, [r4, #32]
	ldrh	r3, [r4, #102]
	uxth	r8, r8
	ldr	r2, [r4, #2324]
	strh	r8, [r4, #3156]	@ movhi
	mul	r3, ip, r3
	ldrh	r1, [r2, r0, lsl #1]
	add	ip, r3, r3, lsr #31
	cmp	r1, ip, asr #1
	bgt	.L1554
	cmp	r8, #48
	bls	.L1555
	cmp	r1, #8
	bls	.L1555
	ldrh	r1, [r4, #3192]
	cmp	r1, #35
	bhi	.L1555
.L1554:
	strh	r10, [r4, #3156]	@ movhi
.L1555:
	ldrh	r2, [r2, r0, lsl #1]
	cmp	r3, r2
	bgt	.L1556
	cmp	r5, r9
	bne	.L1556
	movs	r3, #0
	strh	r5, [r4, #2588]	@ movhi
	strh	r3, [r4, #3156]	@ movhi
	b	.L1622
.L1556:
	cbnz	r2, .L1557
	movw	r0, #65535
	bl	decrement_vpc_count
	ldrh	r3, [r4, #3156]
	adds	r3, r3, #1
	strh	r3, [r4, #3156]	@ movhi
	b	.L1550
.L1557:
	movs	r3, #0
	strb	r3, [r4, #2596]
	ldrh	r3, [r4, #2348]
	cmp	r3, fp
	bne	.L1558
	movw	r2, #658
	ldr	r1, .L1626+4
	ldr	r0, .L1626+8
	bl	printk
.L1558:
	ldrh	r2, [r4, #2588]
	ldrh	r3, [r4, #2396]
	cmp	r2, r3
	bne	.L1559
	movw	r2, #659
	ldr	r1, .L1626+4
	ldr	r0, .L1626+8
	bl	printk
.L1559:
	ldrh	r2, [r4, #2588]
	ldrh	r3, [r4, #2444]
	cmp	r2, r3
	bne	.L1560
	mov	r2, #660
	ldr	r1, .L1626+4
	ldr	r0, .L1626+8
	bl	printk
.L1560:
	ldr	r0, .L1626+12
	bl	make_superblock
	ldrh	r1, [r4, #2588]
	movs	r3, #0
	ldr	r2, [r4, #2324]
	strh	r3, [r7, #-186]	@ movhi
	ldrh	r2, [r2, r1, lsl #1]
	strh	r3, [r4, #2590]	@ movhi
	strb	r3, [r4, #2594]
	strh	r2, [r7, #-184]	@ movhi
.L1549:
	bl	FtlReadRefresh
	movs	r3, #1
	str	r3, [r4, #3820]
	ldrh	r3, [r4, #102]
	str	r3, [sp, #12]
	ldrh	r3, [r4, #2590]
	ldr	r1, [sp, #12]
	adds	r2, r3, r6
	cmp	r2, r1
	itt	gt
	movgt	r2, r1
	subgt	r6, r2, r3
	mov	r3, #0
	it	gt
	uxthgt	r6, r6
.L1620:
	str	r3, [sp, #8]
	ldrh	r3, [sp, #8]
	cmp	r6, r3
	bls	.L1570
	ldr	r3, [sp, #8]
	movw	r10, #65535
	ldrh	r1, [r4, #2590]
	mov	fp, #20
	ldrh	lr, [r4, #32]
	ldr	r0, [r4, #3180]
	add	r1, r1, r3
	movs	r3, #0
	ldr	ip, .L1626+16
	mov	r8, r3
	b	.L1571
.L1564:
	ldrh	r2, [ip, #2]!
	cmp	r2, r10
	beq	.L1563
	mla	r9, fp, r8, r0
	add	r8, r8, #1
	orr	r2, r1, r2, lsl #10
	uxth	r8, r8
	str	r2, [r9, #4]
.L1563:
	adds	r3, r3, #1
.L1571:
	uxth	r2, r3
	cmp	lr, r2
	bhi	.L1564
	mov	fp, #0
	ldrb	r2, [r4, #2596]	@ zero_extendqisi2
	mov	r1, r8
	bl	FlashReadPages
.L1565:
	uxth	r3, fp
	cmp	r8, r3
	bhi	.L1569
	ldr	r3, [sp, #8]
	adds	r3, r3, #1
	b	.L1620
.L1569:
	mov	r9, #20
	ldr	r3, [r4, #3180]
	mul	r9, r9, fp
	add	r2, r3, r9
	ldr	r3, [r3, r9]
	adds	r3, r3, #1
	beq	.L1566
	ldr	r10, [r2, #12]
	movw	r3, #61589
	ldrh	r2, [r10]
	cmp	r2, r3
	bne	.L1566
	ldr	r3, [r10, #8]
	adds	r2, r3, #1
	bne	.L1567
	mov	r2, #696
	ldr	r1, .L1626+4
	ldr	r0, .L1626+8
	str	r3, [sp, #16]
	bl	printk
	ldr	r3, [sp, #16]
.L1567:
	movs	r2, #0
	mov	r0, r3
	add	r1, sp, #28
	bl	log2phys
	ldr	r0, [r4, #3180]
	ldr	r3, [sp, #28]
	add	r0, r0, r9
	ldr	r2, [r0, #4]
	cmp	r2, r3
	bne	.L1566
	ldrh	r3, [r7, #-186]
	movs	r2, #20
	ldr	r1, [r4, #3164]
	str	r2, [sp, #20]
	adds	r3, r3, #1
	strh	r3, [r7, #-186]	@ movhi
	ldr	r3, [r4, #3836]
	mla	r3, r2, r1, r3
	ldr	r1, [r0, #16]
	str	r1, [r3, #16]
	str	r3, [sp, #16]
	bl	Ftl_get_new_temp_ppa
	ldr	r3, [sp, #16]
	ldr	r1, [r4, #3836]
	ldr	r2, [sp, #20]
	str	r0, [r3, #4]
	ldr	r3, [r4, #3164]
	mla	r3, r2, r3, r1
	ldr	r2, [r4, #3180]
	add	r2, r2, r9
	ldr	r1, [r2, #8]
	str	r1, [r3, #8]
	movs	r1, #1
	ldr	r2, [r2, #12]
	str	r2, [r3, #12]
	ldr	r3, [sp, #28]
	str	r3, [r10, #12]
	ldrh	r3, [r4, #2444]
	strh	r3, [r10, #2]	@ movhi
	ldr	r3, [r4, #2544]
	ldr	r0, [r4, #3180]
	str	r3, [r10, #4]
	ldr	r3, [r4, #3164]
	add	r0, r0, r9
	adds	r3, r3, #1
	str	r3, [r4, #3164]
	bl	FtlGcBufAlloc
	ldrb	r2, [r4, #2451]	@ zero_extendqisi2
	ldr	r3, [r4, #3164]
	cmp	r2, r3
	beq	.L1568
	ldrh	r3, [r4, #2448]
	cbnz	r3, .L1566
.L1568:
	bl	Ftl_gc_temp_data_write_back
	cbz	r0, .L1566
.L1623:
	movs	r3, #0
	ldrh	r0, [r7, #-188]
	str	r3, [r4, #3820]
	b	.L1505
.L1566:
	add	fp, fp, #1
	b	.L1565
.L1570:
	ldrh	r3, [r4, #2590]
	add	r6, r6, r3
	ldr	r3, [sp, #12]
	uxth	r6, r6
	cmp	r3, r6
	strh	r6, [r4, #2590]	@ movhi
	bls	.L1572
	ldrh	r2, [r7, #-186]
	ldrh	r3, [r7, #-184]
	cmp	r2, r3
	bne	.L1573
.L1572:
	ldr	r3, [r4, #3164]
	cbz	r3, .L1574
	bl	Ftl_gc_temp_data_write_back
	cmp	r0, #0
	bne	.L1623
.L1574:
	ldrh	r1, [r7, #-186]
	cbnz	r1, .L1575
	ldrh	r2, [r4, #2588]
	ldr	r3, [r4, #2324]
	ldrh	r0, [r3, r2, lsl #1]
	cbz	r0, .L1575
	strh	r1, [r3, r2, lsl #1]	@ movhi
	ldrh	r0, [r4, #2588]
	bl	update_vpc_list
	bl	FtlCacheWriteBack
	bl	l2p_flush
	bl	FtlVpcTblFlush
.L1575:
	movw	r3, #65535
	strh	r3, [r4, #2588]	@ movhi
.L1573:
	ldrh	r3, [r4, #2344]
	cmp	r3, #2
	bhi	.L1576
	ldrh	r6, [r4, #102]
	b	.L1548
.L1576:
	ldrh	r0, [r7, #-188]
	movs	r2, #0
	str	r2, [r4, #3820]
	cmp	r0, #0
	bne	.L1505
	adds	r0, r3, #1
	b	.L1505
.L1580:
	movs	r0, #0
	b	.L1505
.L1627:
	.align	2
.L1626:
	.word	.LANCHOR3
	.word	.LANCHOR1+551
	.word	.LC1
	.word	.LANCHOR0+2588
	.word	.LANCHOR0+2602
.L1511:
	cmp	r7, #0
	beq	.L1578
.L1521:
	ldrh	r2, [r4, #2444]
	movw	r3, #65535
	cmp	r2, r3
	bne	.L1589
.L1579:
	ldrh	r3, [r4, #3202]
	movw	r2, #65535
	cmp	r3, r2
	bne	.L1589
	cmp	r5, r3
	bne	.L1589
	ldrh	r3, [r4, #2588]
	cmp	r3, r5
	beq	.L1533
.L1538:
	movw	r5, #65535
.L1589:
	movs	r6, #1
	b	.L1532
.L1513:
	cmp	r7, #0
	beq	.L1578
	movw	r5, #65535
	b	.L1579
.L1512:
	cmp	r7, #0
	bne	.L1538
	b	.L1578
	.fnend
	.size	rk_ftl_garbage_collect, .-rk_ftl_garbage_collect
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
	str	r2, [sp, #28]
	bne	.L1630
	mov	r2, r3
	ldr	r1, [sp, #28]
	add	r0, r7, #256
	bl	FtlVendorPartRead
	mov	r8, r0
.L1629:
	mov	r0, r8
	add	sp, sp, #60
	@ sp needed
	pop	{r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L1630:
	ldr	r3, [sp, #28]
	ldr	r4, .L1661
	adds	r3, r1, r3
	str	r3, [sp, #8]
	ldr	r2, [sp, #8]
	ldr	r3, [r4, #140]
	cmp	r2, r3
	bhi	.L1650
	ldr	r3, .L1661+4
	ldr	r3, [r3]
	adds	r5, r3, #1
	beq	.L1651
	bl	FtlCacheWriteBack
	ldrh	r5, [r4, #58]
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
	ldr	r5, [sp, #12]
	str	r0, [sp, #16]
	rsb	r3, r3, #1
	str	r9, [sp, #24]
	add	r3, r3, r0
	str	r9, [sp, #20]
	str	r3, [sp, #4]
	ldr	r2, [sp, #4]
	ldr	r3, [r4, #2508]
	add	r3, r3, r2
	str	r3, [r4, #2508]
.L1632:
	ldr	r3, [sp, #4]
	cbnz	r3, .L1648
	ldr	r3, .L1661
	ldrh	r2, [r3, #3206]
	cbnz	r2, .L1649
	ldrh	r3, [r3, #2344]
	cmp	r3, #31
	bhi	.L1629
.L1649:
	movs	r1, #1
	movs	r0, #0
	bl	rk_ftl_garbage_collect
	b	.L1629
.L1648:
	movs	r2, #0
	add	r1, sp, #52
	mov	r0, r5
	bl	log2phys
	ldr	r2, [sp, #52]
	adds	r0, r2, #1
	bne	.L1633
	mov	fp, #0
.L1634:
	ldrh	r0, [r4, #58]
	cmp	fp, r0
	bcc	.L1636
.L1637:
	ldr	r3, [sp, #4]
	adds	r5, r5, #1
	subs	r3, r3, #1
	str	r3, [sp, #4]
	beq	.L1641
	ldrh	r3, [r4, #32]
	cmp	r6, r3, lsl #2
	bne	.L1632
.L1641:
	cmp	r6, #0
	beq	.L1632
	movs	r2, #0
	mov	r1, r6
	ldr	r0, [r4, #3832]
	mov	fp, #0
	bl	FlashReadPages
	lsl	r3, r9, #9
	str	r3, [sp, #40]
	ldr	r3, [sp, #20]
	lsls	r3, r3, #9
	str	r3, [sp, #32]
	ldr	r3, [sp, #24]
	lsls	r3, r3, #9
	str	r3, [sp, #36]
.L1647:
	movs	r3, #20
	ldr	r2, [r4, #3832]
	mul	r3, r3, fp
	ldr	r0, [sp, #12]
	add	r2, r2, r3
	ldr	r1, [r2, #16]
	cmp	r0, r1
	bne	.L1643
	ldr	r1, [r2, #8]
	ldr	r2, [r4, #3816]
	cmp	r1, r2
	bne	.L1644
	str	r3, [sp, #44]
	mov	r0, r10
	ldr	r3, [sp, #32]
	ldr	r2, [sp, #36]
	add	r1, r1, r3
.L1660:
	bl	ftl_memcpy
	ldr	r3, [sp, #44]
.L1644:
	ldr	r2, [r4, #3832]
	ldr	r0, [r2, r3]
	add	ip, r2, r3
	adds	r1, r0, #1
	itttt	eq
	ldreq	r1, [r4, #2708]
	moveq	r8, r0
	addeq	r1, r1, #1
	streq	r1, [r4, #2708]
	ldr	r3, [r2, r3]
	cmp	r3, #256
	bne	.L1646
	ldr	r0, [ip, #4]
	ubfx	r0, r0, #10, #16
	bl	P2V_block_in_plane
	bl	FtlGcRefreshBlock
.L1646:
	add	fp, fp, #1
	cmp	r6, fp
	bne	.L1647
	movs	r6, #0
	b	.L1632
.L1636:
	mla	r0, r0, r5, fp
	cmp	r7, r0
	bhi	.L1635
	ldr	r3, [sp, #8]
	cmp	r3, r0
	bls	.L1635
	subs	r0, r0, r7
	mov	r1, #512
	add	r0, r10, r0, lsl #9
	bl	__memzero
.L1635:
	add	fp, fp, #1
	b	.L1634
.L1633:
	ldr	r3, [r4, #3832]
	movs	r1, #20
	ldrh	fp, [r4, #58]
	mla	r3, r1, r6, r3
	str	r2, [r3, #4]
	ldr	r2, [sp, #12]
	cmp	r5, r2
	bne	.L1638
	ldr	r2, [r4, #3816]
	mov	r1, fp
	mov	r0, r7
	str	r3, [sp, #32]
	str	r2, [r3, #8]
	bl	__aeabi_uidivmod
	sub	r2, fp, r1
	str	r1, [sp, #20]
	ldr	r1, [sp, #28]
	mov	r3, r2
	cmp	r2, r1
	it	cs
	movcs	r3, r1
	cmp	r3, fp
	str	r3, [sp, #24]
	ldr	r3, [sp, #32]
	bne	.L1639
	str	r10, [r3, #8]
.L1639:
	ldrh	r2, [r4, #112]
	ldr	r1, [r4, #3812]
	str	r5, [r3, #16]
	muls	r2, r6, r2
	adds	r6, r6, #1
	bic	r2, r2, #3
	add	r2, r2, r1
	str	r2, [r3, #12]
	b	.L1637
.L1638:
	ldr	r2, [sp, #16]
	cmp	r5, r2
	bne	.L1640
	ldr	r2, [r4, #3808]
	ldr	r1, [sp, #8]
	str	r2, [r3, #8]
	mul	r2, fp, r5
	sub	r9, r1, r2
	cmp	fp, r9
	bne	.L1639
.L1659:
	subs	r2, r2, r7
	add	r2, r10, r2, lsl #9
	str	r2, [r3, #8]
	b	.L1639
.L1640:
	mul	r2, r5, fp
	b	.L1659
.L1643:
	ldr	r0, [sp, #16]
	cmp	r0, r1
	bne	.L1644
	ldr	r1, [r2, #8]
	ldr	r2, [r4, #3808]
	cmp	r1, r2
	bne	.L1644
	str	r3, [sp, #44]
	ldrh	r0, [r4, #58]
	ldr	r3, [sp, #16]
	ldr	r2, [sp, #40]
	muls	r0, r3, r0
	subs	r0, r0, r7
	add	r0, r10, r0, lsl #9
	b	.L1660
.L1650:
	mov	r8, #-1
	b	.L1629
.L1651:
	mov	r8, r3
	b	.L1629
.L1662:
	.align	2
.L1661:
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
	mov	r8, r1
	str	r2, [sp, #12]
	str	r3, [sp, #4]
	bne	.L1665
	mov	r2, r3
	ldr	r1, [sp, #12]
	add	r0, r8, #256
	bl	FtlVendorPartWrite
.L1664:
	add	sp, sp, #68
	@ sp needed
	pop	{r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L1665:
	ldr	r3, [sp, #12]
	ldr	r4, .L1731
	adds	r7, r1, r3
	ldr	r3, [r4, #140]
	cmp	r7, r3
	bhi	.L1707
	ldr	r6, .L1731+4
	ldr	r0, [r6]
	adds	r5, r0, #1
	beq	.L1664
	ldr	r3, .L1731+8
	mov	r2, #2048
	ldrh	r5, [r4, #58]
	mov	r0, r8
	str	r2, [r3, #-180]
	mov	r1, r5
	bl	__aeabi_uidiv
	mov	r1, r5
	mov	r10, r0
	subs	r0, r7, #1
	bl	__aeabi_uidiv
	sub	r7, r0, r10
	ldr	fp, [r4, #3988]
	adds	r3, r7, #1
	adds	r2, r7, #1
	str	r3, [sp, #8]
	ldr	r3, [r4, #2516]
	str	r0, [sp, #20]
	add	r3, r3, r2
	str	r3, [r4, #2516]
	cmp	fp, #0
	beq	.L1667
	ldr	r3, [fp, #16]
	cmp	r10, r3
	beq	.L1668
	bl	FtlCacheWriteBack
.L1667:
	ldr	r3, [r6, #8]
	cmp	r3, #0
	beq	.L1709
	ldr	r5, .L1731+12
	ldrh	r2, [r4, #2352]
	add	r3, r5, #48
	cmp	r2, #0
	it	eq
	moveq	r5, r3
.L1670:
	ldr	r4, .L1731
	mov	r6, r10
.L1671:
	ldr	r3, [sp, #8]
	cmp	r3, #0
	beq	.L1705
	ldrb	r2, [r5, #6]	@ zero_extendqisi2
	ldrh	r3, [r4, #32]
	cmp	r2, r3
	bcc	.L1672
	mov	r2, #1012
	ldr	r1, .L1731+16
	ldr	r0, .L1731+20
	bl	printk
.L1672:
	ldrh	r9, [r5, #4]
	cmp	r9, #0
	bne	.L1673
	bl	FtlCacheWriteBack
	ldr	r3, .L1731+12
	ldr	r7, .L1731+4
	cmp	r5, r3
	bne	.L1674
	ldrh	r9, [r4, #2400]
	cmp	r9, #0
	bne	.L1675
	add	r0, r5, #48
	bl	allocate_new_data_superblock
	str	r9, [r7, #8]
.L1675:
	ldr	r5, .L1731+12
	ldr	r0, .L1731+12
	bl	allocate_new_data_superblock
	ldr	r2, [r7, #8]
	add	r3, r5, #48
	cmp	r2, #0
	it	ne
	movne	r5, r3
.L1676:
	ldrh	r3, [r5, #4]
	cbnz	r3, .L1673
	mov	r0, r5
	bl	allocate_new_data_superblock
.L1673:
	ldrb	r2, [r5, #7]	@ zero_extendqisi2
	ldrh	r3, [r5, #4]
	ldr	r1, [sp, #8]
	lsls	r2, r2, #2
	cmp	r3, r1
	it	cs
	movcs	r3, r1
	cmp	r2, r3
	it	cs
	movcs	r2, r3
	ldrh	r3, [r4, #32]
	str	r2, [sp, #36]
	ldrb	r2, [r5, #6]	@ zero_extendqisi2
	cmp	r2, r3
	bcc	.L1677
	movw	r2, #1045
	ldr	r1, .L1731+16
	ldr	r0, .L1731+20
	bl	printk
.L1677:
	mov	r9, #0
.L1678:
	ldr	r3, [sp, #36]
	cmp	r9, r3
	bne	.L1699
.L1679:
	ldr	r3, [r4, #3988]
	cmp	r3, #0
	beq	.L1700
	ldr	r3, [sp, #8]
	subs	r9, r9, #1
	add	r3, r3, #-1
	str	r3, [sp, #8]
	bne	.L1700
.L1705:
	ldr	r3, [sp, #20]
	movs	r0, #0
	ldr	r4, .L1731
	sub	r1, r3, r10
	bl	rk_ftl_garbage_collect
	ldrh	r3, [r4, #2344]
	cmp	r3, #15
	bhi	.L1702
	movw	r5, #65535
.L1726:
	ldrh	r3, [r4, #2588]
	cmp	r3, r5
	bne	.L1706
	ldrh	r3, [r4, #3202]
	cmp	r3, r5
	bne	.L1706
	movs	r0, #0
	bl	List_get_gc_head_node
	uxth	r0, r0
	bl	FtlGcRefreshBlock
.L1706:
	movs	r1, #1
	movs	r3, #128
	mov	r0, r1
	strh	r3, [r4, #3150]	@ movhi
	strh	r3, [r4, #3148]	@ movhi
	bl	rk_ftl_garbage_collect
	movs	r1, #1
	movs	r0, #0
	bl	rk_ftl_garbage_collect
	ldrh	r3, [r4, #2344]
	cmp	r3, #8
	bls	.L1726
	b	.L1702
.L1668:
	ldr	r3, [r4, #2520]
	mov	r1, r5
	mov	r0, r8
	adds	r3, r3, #1
	str	r3, [r4, #2520]
	bl	__aeabi_uidivmod
	ldr	r2, [sp, #12]
	subs	r5, r5, r1
	ldr	r0, [fp, #8]
	mov	r3, r1
	ldr	r1, [sp, #4]
	cmp	r5, r2
	it	cs
	movcs	r5, r2
	lsl	r9, r5, #9
	add	r0, r0, r3, lsl #9
	mov	r2, r9
	bl	ftl_memcpy
	cbnz	r7, .L1669
.L1702:
	movs	r0, #0
	b	.L1664
.L1669:
	ldr	r3, [sp, #12]
	add	r8, r8, r5
	add	r10, r10, #1
	subs	r3, r3, r5
	str	r3, [sp, #12]
	ldr	r3, [sp, #4]
	add	r3, r3, r9
	str	r3, [sp, #4]
	bl	FtlCacheWriteBack
	str	r7, [sp, #8]
	b	.L1667
.L1709:
	ldr	r5, .L1731+12
	b	.L1670
.L1674:
	ldrh	r2, [r4, #2352]
	str	r9, [r7, #8]
	cbnz	r2, .L1712
	mov	r0, r5
	bl	allocate_new_data_superblock
	b	.L1676
.L1712:
	mov	r5, r3
	b	.L1673
.L1699:
	ldrh	r3, [r5, #4]
	cmp	r3, #0
	beq	.L1679
	movs	r2, #0
	add	r1, sp, #40
	mov	r0, r6
	movs	r7, #20
	bl	log2phys
	mov	r0, r5
	mul	r7, r7, r9
	bl	get_new_active_ppa
	ldrh	r2, [r4, #112]
	ldr	r3, [r4, #3840]
	mul	r1, r2, r9
	add	r3, r3, r7
	str	r0, [r3, #4]
	str	r6, [r3, #16]
	bic	r1, r1, #3
	str	r1, [sp, #28]
	ldr	r0, [sp, #28]
	ldr	r1, [r4, #3812]
	add	fp, r1, r0
	str	r1, [sp, #32]
	str	fp, [r3, #12]
	movs	r1, #0
	mov	r0, fp
	bl	ftl_memset
	cmp	r6, r10
	beq	.L1680
	ldr	r3, [sp, #20]
	cmp	r6, r3
	bne	.L1725
	ldr	r3, [sp, #12]
	add	r2, r8, r3
	ldrh	r3, [r4, #58]
	smulbb	r3, r3, r6
	subs	r2, r2, r3
	uxth	r3, r2
	str	r3, [sp, #16]
	movs	r3, #0
	str	r3, [sp, #24]
	b	.L1683
.L1680:
	ldrh	r2, [r4, #58]
	mov	r0, r8
	mov	r1, r2
	str	r2, [sp, #16]
	bl	__aeabi_uidivmod
	ldr	r2, [sp, #16]
	str	r1, [sp, #24]
	subs	r2, r2, r1
	ldr	r1, [sp, #12]
	mov	r3, r2
	cmp	r2, r1
	it	cs
	movcs	r3, r1
	str	r3, [sp, #16]
.L1683:
	ldrh	r3, [r4, #58]
	ldr	r2, [sp, #16]
	cmp	r2, r3
	ldr	r3, [r4, #3840]
	bne	.L1684
	cmp	r6, r10
	add	r7, r7, r3
	bne	.L1685
	ldr	r3, [sp, #4]
.L1729:
	str	r3, [r7, #8]
	b	.L1686
.L1685:
	ldr	r2, [sp, #16]
	ldr	r3, [sp, #4]
	muls	r2, r6, r2
	sub	r2, r2, r8
	add	r2, r3, r2, lsl #9
	str	r2, [r7, #8]
.L1686:
	ldrb	r2, [r5, #6]	@ zero_extendqisi2
	ldrh	r3, [r4, #32]
	cmp	r2, r3
	bcc	.L1696
	movw	r2, #1119
	ldr	r1, .L1731+16
	ldr	r0, .L1731+20
	bl	printk
.L1696:
	ldr	r2, [sp, #32]
	movw	r3, #61589
	ldr	r1, [sp, #28]
	add	r9, r9, #1
	strh	r3, [r2, r1]	@ movhi
	ldr	r3, [r4, #2544]
	str	r6, [fp, #8]
	adds	r6, r6, #1
	str	r3, [fp, #4]
	adds	r3, r3, #1
	adds	r2, r3, #1
	it	eq
	moveq	r3, #0
	str	r3, [r4, #2544]
	ldr	r3, [sp, #40]
	str	r3, [fp, #12]
	ldrh	r3, [r5]
	strh	r3, [fp, #2]	@ movhi
	b	.L1678
.L1732:
	.align	2
.L1731:
	.word	.LANCHOR0
	.word	.LANCHOR2
	.word	.LANCHOR3
	.word	.LANCHOR0+2348
	.word	.LANCHOR1+574
	.word	.LC1
.L1684:
	cmp	r6, r10
	add	r3, r3, r7
	ite	eq
	ldreq	r2, [r4, #3816]
	ldrne	r2, [r4, #3808]
	str	r2, [r3, #8]
	ldr	r3, [sp, #40]
	adds	r1, r3, #1
	beq	.L1689
	str	r3, [sp, #48]
	movs	r1, #1
	ldr	r3, [r4, #3840]
	add	r0, sp, #44
	str	r6, [sp, #60]
	add	r3, r3, r7
	ldr	r2, [r3, #8]
	ldr	r3, [r3, #12]
	str	r2, [sp, #52]
	movs	r2, #0
	str	r3, [sp, #56]
	bl	FlashReadPages
	ldr	r3, [sp, #44]
	adds	r3, r3, #1
	bne	.L1690
	ldr	r3, [r4, #2708]
	adds	r3, r3, #1
	str	r3, [r4, #2708]
.L1693:
	ldr	r3, [sp, #16]
	cmp	r6, r10
	lsl	r2, r3, #9
	bne	.L1694
	ldr	r3, [r4, #3840]
	ldr	r1, [sp, #4]
	add	r3, r3, r7
	ldr	r0, [r3, #8]
	ldr	r3, [sp, #24]
	add	r0, r0, r3, lsl #9
.L1728:
	bl	ftl_memcpy
	ldr	r3, [sp, #20]
	cmp	r6, r3
	bne	.L1686
	ldrh	r3, [r5, #4]
	cmp	r3, #0
	beq	.L1686
	ldr	r3, [r4, #3840]
	add	r7, r7, r3
	ldr	r3, .L1733
	str	r7, [r4, #3988]
	str	r5, [r3, #-196]
	b	.L1686
.L1690:
	ldr	r3, [fp, #8]
	cmp	r6, r3
	beq	.L1692
	ldr	r3, [r4, #2708]
	mov	r2, r6
	ldr	r0, .L1733+4
	adds	r3, r3, #1
	str	r3, [r4, #2708]
	ldr	r1, [fp, #8]
	bl	printk
.L1692:
	ldr	r3, [fp, #8]
	cmp	r6, r3
	beq	.L1693
	movw	r2, #1098
	ldr	r1, .L1733+8
	ldr	r0, .L1733+12
	bl	printk
	b	.L1693
.L1689:
	ldr	r3, [r4, #3840]
	movs	r1, #0
	ldrh	r2, [r4, #110]
	add	r3, r3, r7
	ldr	r0, [r3, #8]
	bl	ftl_memset
	b	.L1693
.L1694:
	ldrh	r1, [r4, #58]
	ldr	r0, [sp, #4]
	ldr	r3, [r4, #3840]
	muls	r1, r6, r1
	add	r3, r3, r7
	sub	r1, r1, r8
	add	r1, r0, r1, lsl #9
	ldr	r0, [r3, #8]
	b	.L1728
.L1725:
	ldr	r3, [r4, #3840]
	ldr	r2, [sp, #4]
	add	r7, r7, r3
	ldrh	r3, [r4, #58]
	muls	r3, r6, r3
	sub	r3, r3, r8
	add	r3, r2, r3, lsl #9
	b	.L1729
.L1700:
	mov	r3, r5
	movs	r2, #0
	mov	r1, r9
	ldr	r0, [r4, #3840]
	bl	FtlProgPages
	ldr	r3, [sp, #8]
	cmp	r3, r9
	bcs	.L1703
	movw	r2, #1134
	ldr	r1, .L1733+8
	ldr	r0, .L1733+12
	bl	printk
.L1703:
	ldr	r3, [sp, #8]
	sub	r3, r3, r9
	str	r3, [sp, #8]
	b	.L1671
.L1707:
	mov	r0, #-1
	b	.L1664
.L1734:
	.align	2
.L1733:
	.word	.LANCHOR3
	.word	.LC105
	.word	.LANCHOR1+574
	.word	.LC1
	.fnend
	.size	FtlWrite, .-FtlWrite
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
	@ link register save eliminated.
	mov	r3, r2
	mov	r2, r1
	mov	r1, r0
	movs	r0, #0
	b	FtlWrite
	.fnend
	.size	sftl_write, .-sftl_write
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
	push	{r3, r4, r5, r6, r7, lr}
	.save {r3, r4, r5, r6, r7, lr}
	movs	r1, #0
	ldr	r4, .L1757
	ldr	r3, [r4, #3280]
	ldrh	r2, [r4, #40]
	ldr	r0, [r4, #2324]
	str	r3, [r4, #3268]
	ldr	r3, [r4, #3284]
	lsls	r2, r2, #1
	str	r3, [r4, #3272]
	bl	ftl_memset
	ldrh	r0, [r4, #2576]
	movw	r3, #65535
	cmp	r0, r3
	bne	.L1737
.L1745:
	mov	r0, #-1
	pop	{r3, r4, r5, r6, r7, pc}
.L1737:
	ldr	r7, .L1757+4
	movs	r1, #1
	addw	r6, r4, #3260
	bl	FtlGetLastWrittenPage
	sxth	r5, r0
	adds	r0, r0, #1
	strh	r0, [r4, #2578]	@ movhi
.L1739:
	cmp	r5, #0
	bge	.L1742
	movw	r2, #1437
	ldr	r1, .L1757+8
	ldr	r0, .L1757+12
	bl	printk
.L1741:
	ldrh	r3, [r4, #40]
	ldrh	r2, [r4, #110]
	adds	r3, r3, #24
	cmp	r2, r3, lsl #1
	bcs	.L1744
	movw	r2, #1439
	ldr	r1, .L1757+8
	ldr	r0, .L1757+12
	bl	printk
.L1744:
	movs	r2, #48
	ldr	r1, [r4, #3268]
	ldr	r0, .L1757+16
	bl	ftl_memcpy
	ldrh	r2, [r4, #40]
	ldr	r1, [r4, #3268]
	ldr	r0, [r4, #2324]
	lsls	r2, r2, #1
	adds	r1, r1, #48
	bl	ftl_memcpy
	ldrh	r1, [r4, #40]
	ldr	r3, [r4, #3268]
	ldr	r0, [r4, #24]
	lsrs	r2, r1, #3
	adds	r1, r1, #24
	lsls	r1, r1, #1
	adds	r2, r2, #4
	bic	r1, r1, #3
	add	r1, r1, r3
	bl	ftl_memcpy
	ldr	r2, [r4, #2268]
	ldr	r3, .L1757+4
	cmp	r2, r3
	bne	.L1745
	ldrb	r2, [r4, #2278]	@ zero_extendqisi2
	ldrh	r3, [r4, #54]
	ldrh	r5, [r4, #2276]
	cmp	r2, r3
	strh	r5, [r4, #2582]	@ movhi
	bne	.L1745
	ldr	r3, .L1757+20
	ldrh	r2, [r4, #58]
	ldr	r6, [r4, #44]
	str	r5, [r3, #-176]
	ldrh	r3, [r4, #102]
	ldrh	r0, [r4, #158]
	ldrh	r1, [r4, #32]
	muls	r3, r5, r3
	subs	r0, r6, r0
	subs	r0, r0, r5
	str	r3, [r4, #2504]
	muls	r3, r2, r3
	str	r3, [r4, #140]
	bl	__aeabi_uidiv
	cmp	r5, r6
	strh	r0, [r4, #2572]	@ movhi
	bls	.L1746
	movw	r2, #1461
	ldr	r1, .L1757+8
	ldr	r0, .L1757+12
	bl	printk
.L1746:
	ldrh	r3, [r4, #2284]
	ldrh	r1, [r4, #2282]
	lsrs	r2, r3, #6
	and	r3, r3, #63
	strb	r3, [r4, #2354]
	strh	r2, [r4, #2350]	@ movhi
	ldrh	r2, [r4, #2286]
	ldrb	r3, [r4, #2279]	@ zero_extendqisi2
	strh	r1, [r4, #2348]	@ movhi
	strh	r2, [r4, #2396]	@ movhi
	ldrh	r2, [r4, #2288]
	strb	r3, [r4, #2356]
	movw	r3, #65535
	strh	r3, [r4, #2588]	@ movhi
	movs	r3, #0
	strh	r3, [r4, #2590]	@ movhi
	lsrs	r0, r2, #6
	and	r2, r2, #63
	strb	r2, [r4, #2402]
	ldrb	r2, [r4, #2280]	@ zero_extendqisi2
	strh	r0, [r4, #2398]	@ movhi
	strb	r3, [r4, #2594]
	strb	r2, [r4, #2404]
	ldrh	r2, [r4, #2290]
	strb	r3, [r4, #2596]
	str	r3, [r4, #2528]
	strh	r2, [r4, #2444]	@ movhi
	ldrh	r2, [r4, #2292]
	str	r3, [r4, #2516]
	str	r3, [r4, #2508]
	str	r3, [r4, #2524]
	lsrs	r0, r2, #6
	and	r2, r2, #63
	strb	r2, [r4, #2450]
	ldrb	r2, [r4, #2281]	@ zero_extendqisi2
	str	r3, [r4, #2552]
	strh	r0, [r4, #2446]	@ movhi
	strb	r2, [r4, #2452]
	ldr	r2, [r4, #2300]
	str	r2, [r4, #2548]
	str	r3, [r4, #2564]
	ldr	r2, [r4, #2540]
	str	r3, [r4, #2520]
	ldr	r3, [r4, #2308]
	cmp	r3, r2
	ldr	r2, [r4, #2544]
	it	hi
	strhi	r3, [r4, #2540]
	ldr	r3, [r4, #2304]
	cmp	r3, r2
	it	hi
	strhi	r3, [r4, #2544]
	movw	r3, #65535
	cmp	r1, r3
	beq	.L1749
	ldr	r0, .L1757+24
	bl	make_superblock
.L1749:
	ldrh	r2, [r4, #2396]
	movw	r3, #65535
	cmp	r2, r3
	beq	.L1750
	ldr	r0, .L1757+28
	bl	make_superblock
.L1750:
	ldrh	r2, [r4, #2444]
	movw	r3, #65535
	cmp	r2, r3
	beq	.L1751
	ldr	r0, .L1757+32
	bl	make_superblock
.L1751:
	ldrh	r2, [r4, #2588]
	movw	r3, #65535
	cmp	r2, r3
	beq	.L1752
	ldr	r0, .L1757+36
	bl	make_superblock
.L1752:
	movs	r0, #0
	pop	{r3, r4, r5, r6, r7, pc}
.L1742:
	ldrh	r3, [r4, #2576]
	movs	r2, #1
	mov	r1, r2
	mov	r0, r6
	orr	r3, r5, r3, lsl #10
	str	r3, [r4, #3264]
	ldr	r3, [r4, #3280]
	str	r3, [r4, #3268]
	bl	FlashReadPages
	ldr	r3, [r4, #3260]
	adds	r3, r3, #1
	beq	.L1740
	ldr	r3, [r4, #3280]
	ldr	r3, [r3]
	cmp	r3, r7
	bne	.L1740
	ldr	r3, [r4, #3284]
	ldrh	r2, [r3]
	movw	r3, #61604
	cmp	r2, r3
	beq	.L1741
.L1740:
	subs	r5, r5, #1
	sxth	r5, r5
	b	.L1739
.L1758:
	.align	2
.L1757:
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
	.fnend
	.size	FtlLoadSysInfo, .-FtlLoadSysInfo
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
	ldr	r4, .L1775
	ldrh	r0, [r4, #36]
	strh	r3, [r4, #4008]	@ movhi
	bl	FtlFreeSysBlkQueueInit
	bl	FtlScanSysBlk
	ldrh	r2, [r4, #2576]
	movw	r3, #65535
	cmp	r2, r3
	bne	.L1760
.L1762:
	mov	r5, #-1
.L1759:
	mov	r0, r5
	pop	{r4, r5, r6, pc}
.L1760:
	bl	FtlLoadSysInfo
	mov	r5, r0
	cmp	r0, #0
	bne	.L1762
	bl	FtlLoadMapInfo
	bl	FtlLoadVonderInfo
	bl	Ftl_load_ext_data
	bl	FtlLoadEctTbl
	bl	FtlFreeSysBLkSort
	bl	SupperBlkListInit
	bl	FtlPowerLostRecovery
	movs	r0, #1
	bl	FtlUpdateVaildLpn
	ldrh	r1, [r4, #138]
	mov	r3, r5
	ldr	r2, [r4, #2492]
	movs	r0, #12
.L1763:
	cmp	r3, r1
	bge	.L1768
	mla	r6, r0, r3, r2
	ldr	r6, [r6, #4]
	cmp	r6, #0
	bge	.L1764
.L1768:
	ldrh	r2, [r4, #2296]
	cmp	r3, r1
	add	r2, r2, #1
	strh	r2, [r4, #2296]	@ movhi
	bge	.L1774
.L1765:
	ldr	r0, .L1775+4
	bl	FtlSuperblockPowerLostFix
	ldr	r0, .L1775+8
	bl	FtlSuperblockPowerLostFix
	ldrh	r1, [r4, #2348]
	ldr	r2, [r4, #2324]
	ldrh	r0, [r4, #2352]
	ldrh	r3, [r2, r1, lsl #1]
	subs	r3, r3, r0
	strh	r3, [r2, r1, lsl #1]	@ movhi
	ldrh	r3, [r4, #102]
	ldr	r1, [r4, #2324]
	ldrh	r0, [r4, #2396]
	strh	r3, [r4, #2350]	@ movhi
	movs	r3, #0
	strb	r3, [r4, #2354]
	strh	r3, [r4, #2352]	@ movhi
	ldrh	r6, [r4, #2400]
	ldrh	r2, [r1, r0, lsl #1]
	subs	r2, r2, r6
	strh	r2, [r1, r0, lsl #1]	@ movhi
	ldrh	r2, [r4, #102]
	ldr	r0, .L1775+12
	strb	r3, [r4, #2402]
	strh	r2, [r4, #2398]	@ movhi
	strh	r3, [r4, #2400]	@ movhi
	bl	FtlMapBlkWriteDump_data
	ldr	r0, .L1775+16
	bl	FtlMapBlkWriteDump_data
	ldrh	r3, [r4, #2298]
	adds	r3, r3, #1
	strh	r3, [r4, #2298]	@ movhi
	bl	l2p_flush
	bl	FtlVpcTblFlush
	bl	FtlVpcTblFlush
	b	.L1769
.L1764:
	adds	r3, r3, #1
	b	.L1763
.L1774:
	ldrh	r3, [r4, #4008]
	cmp	r3, #0
	bne	.L1765
.L1769:
	ldrh	r0, [r4, #2348]
	movw	r3, #65535
	cmp	r0, r3
	beq	.L1770
	ldrh	r3, [r4, #2352]
	cbnz	r3, .L1770
	ldrh	r3, [r4, #2400]
	cbnz	r3, .L1770
	bl	FtlGcRefreshBlock
	ldrh	r0, [r4, #2396]
	bl	FtlGcRefreshBlock
	ldr	r0, .L1775+4
	bl	allocate_new_data_superblock
	ldr	r0, .L1775+8
	bl	allocate_new_data_superblock
.L1770:
	ldrh	r3, [r4, #2296]
	lsls	r3, r3, #27
	bne	.L1759
	bl	FtlVpcCheckAndModify
	b	.L1759
.L1776:
	.align	2
.L1775:
	.word	.LANCHOR0
	.word	.LANCHOR0+2348
	.word	.LANCHOR0+2396
	.word	.LANCHOR0+3904
	.word	.LANCHOR0+3948
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
	ldr	r4, .L1801
	ldrh	r0, [r4, #36]
	str	r3, [r4, #2540]
	str	r3, [r4, #2544]
	bl	FtlFreeSysBlkQueueInit
	bl	FtlLoadBbt
	cbz	r0, .L1778
	bl	FtlMakeBbt
.L1778:
	ldr	r0, .L1801+4
	movs	r2, #0
.L1779:
	ldrh	r1, [r4, #58]
	uxth	r3, r2
	adds	r2, r2, #1
	cmp	r3, r1, lsl #7
	blt	.L1780
	ldrh	r6, [r4, #40]
	movs	r5, #0
.L1781:
	ldrh	r3, [r4, #42]
	cmp	r3, r6
	bhi	.L1782
	ldrh	r1, [r4, #32]
	subs	r3, r5, #3
	cmp	r3, r1, lsl #1
	bge	.L1783
.L1787:
	movs	r5, #0
	mov	r6, r5
.L1784:
	ldrh	r3, [r4, #40]
	uxth	r0, r5
	adds	r5, r5, #1
	cmp	r3, r0
	bhi	.L1788
	ldrh	r3, [r4, #42]
	ldrh	r5, [r4, #32]
	ldr	r9, [r4, #44]
	str	r3, [r4, #3804]
	mov	r1, r5
	mov	r0, r9
	bl	__aeabi_uidiv
	ubfx	r7, r0, #5, #16
	mov	r8, r0
	add	r3, r7, #36
	str	r0, [r4, #2504]
	strh	r3, [r4, #2572]	@ movhi
	movs	r3, #24
	muls	r3, r5, r3
	cmp	r6, r3
	ble	.L1789
	mov	r1, r5
	sub	r0, r9, r6
	bl	__aeabi_uidiv
	str	r0, [r4, #2504]
	lsrs	r0, r0, #5
	adds	r0, r0, #24
	strh	r0, [r4, #2572]	@ movhi
.L1789:
	ldrh	r3, [r4, #94]
	cbz	r3, .L1791
	ldrh	r2, [r4, #2572]
	add	r2, r2, r3, lsr #1
	strh	r2, [r4, #2572]	@ movhi
	mul	r2, r5, r3
	cmp	r6, r2
	itttt	lt
	addlt	r3, r3, #32
	strlt	r8, [r4, #2504]
	addlt	r3, r3, r7
	strhlt	r3, [r4, #2572]	@ movhi
.L1791:
	ldrh	r2, [r4, #2572]
	movw	r6, #65535
	ldr	r3, [r4, #2504]
	ldr	r7, .L1801+8
	subs	r3, r3, r2
	muls	r5, r3, r5
	ldrh	r3, [r4, #102]
	str	r5, [r7, #-176]
	muls	r5, r3, r5
	ldrh	r3, [r4, #58]
	str	r5, [r4, #2504]
	muls	r5, r3, r5
	str	r5, [r4, #140]
	mov	r5, r7
	bl	FtlBbmTblFlush
	ldrh	r2, [r4, #42]
	movs	r1, #0
	ldr	r0, [r4, #2324]
	ldr	r7, .L1801+12
	lsls	r2, r2, #1
	bl	ftl_memset
	ldrh	r2, [r4, #40]
	movs	r3, #0
	str	r3, [r4, #2500]
	movs	r1, #255
	strh	r3, [r4, #2590]	@ movhi
	strb	r3, [r4, #2594]
	strb	r3, [r4, #2596]
	lsrs	r2, r2, #3
	strh	r3, [r4, #2350]	@ movhi
	strb	r3, [r4, #2354]
	strh	r3, [r4, #2348]	@ movhi
	movs	r3, #1
	ldr	r0, [r4, #24]
	strh	r6, [r4, #2588]	@ movhi
	strb	r3, [r4, #2356]
	bl	ftl_memset
.L1793:
	mov	r0, r7
	bl	make_superblock
	ldrb	r3, [r4, #2355]	@ zero_extendqisi2
	ldrh	r2, [r4, #2348]
	cmp	r3, #0
	bne	.L1794
	ldr	r3, [r4, #2324]
	strh	r6, [r3, r2, lsl #1]	@ movhi
	ldrh	r3, [r4, #2348]
	adds	r3, r3, #1
	strh	r3, [r4, #2348]	@ movhi
	b	.L1793
.L1780:
	ldr	r5, [r4, #3816]
	mvns	r1, r3
	orr	r1, r3, r1, lsl #16
	str	r1, [r5, r3, lsl #2]
	ldr	r1, [r4, #3808]
	str	r0, [r1, r3, lsl #2]
	b	.L1779
.L1782:
	mov	r0, r6
	movs	r1, #1
	bl	FtlLowFormatEraseBlock
	adds	r6, r6, #1
	add	r5, r5, r0
	uxth	r5, r5
	uxth	r6, r6
	b	.L1781
.L1783:
	mov	r0, r5
	bl	__aeabi_uidiv
	ldr	r3, [r4, #132]
	add	r0, r0, r3
	uxth	r0, r0
	bl	FtlSysBlkNumInit
	ldrh	r0, [r4, #36]
	bl	FtlFreeSysBlkQueueInit
	ldrh	r5, [r4, #40]
.L1785:
	ldrh	r3, [r4, #42]
	cmp	r3, r5
	bls	.L1787
	mov	r0, r5
	movs	r1, #1
	adds	r5, r5, #1
	bl	FtlLowFormatEraseBlock
	uxth	r5, r5
	b	.L1785
.L1788:
	movs	r1, #0
	bl	FtlLowFormatEraseBlock
	add	r6, r6, r0
	uxth	r6, r6
	b	.L1784
.L1794:
	ldr	r3, [r4, #2540]
	movw	r6, #65535
	ldrh	r1, [r4, #2352]
	ldr	r7, .L1801+16
	str	r3, [r4, #2360]
	adds	r3, r3, #1
	str	r3, [r4, #2540]
	ldr	r3, [r4, #2324]
	strh	r1, [r3, r2, lsl #1]	@ movhi
	movs	r3, #0
	strh	r3, [r4, #2398]	@ movhi
	strb	r3, [r4, #2402]
	ldrh	r3, [r4, #2348]
	adds	r3, r3, #1
	strh	r3, [r4, #2396]	@ movhi
	movs	r3, #1
	strb	r3, [r4, #2404]
.L1795:
	mov	r0, r7
	bl	make_superblock
	ldrb	r3, [r4, #2403]	@ zero_extendqisi2
	ldrh	r2, [r4, #2396]
	cbnz	r3, .L1796
	ldr	r3, [r4, #2324]
	strh	r6, [r3, r2, lsl #1]	@ movhi
	ldrh	r3, [r4, #2396]
	adds	r3, r3, #1
	strh	r3, [r4, #2396]	@ movhi
	b	.L1795
.L1796:
	ldr	r3, [r4, #2540]
	movw	r6, #65535
	ldrh	r1, [r4, #2400]
	str	r3, [r4, #2408]
	adds	r3, r3, #1
	str	r3, [r4, #2540]
	ldr	r3, [r4, #2324]
	strh	r1, [r3, r2, lsl #1]	@ movhi
	strh	r6, [r4, #2444]	@ movhi
	bl	FtlFreeSysBlkQueueOut
	movs	r3, #0
	strh	r0, [r4, #2576]	@ movhi
	strh	r3, [r4, #2578]	@ movhi
	ldr	r3, [r5, #-176]
	strh	r6, [r4, #2580]	@ movhi
	strh	r3, [r4, #2582]	@ movhi
	ldr	r3, [r4, #2540]
	str	r3, [r4, #2584]
	adds	r3, r3, #1
	str	r3, [r4, #2540]
	bl	FtlVpcTblFlush
	bl	FtlSysBlkInit
	cbnz	r0, .L1797
	ldr	r3, .L1801+20
	movs	r2, #1
	str	r2, [r3]
.L1797:
	movs	r0, #0
	pop	{r3, r4, r5, r6, r7, r8, r9, pc}
.L1802:
	.align	2
.L1801:
	.word	.LANCHOR0
	.word	168778952
	.word	.LANCHOR3
	.word	.LANCHOR0+2348
	.word	.LANCHOR0+2396
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
	ldr	r4, .L1809
	ldr	r5, .L1809+4
	ldr	r1, .L1809+8
	ldr	r0, .L1809+12
	str	r3, [r5]
	bl	printk
	mov	r0, r4
	bl	FtlConstantsInit
	bl	FtlMemInit
	bl	FtlVariablesInit
	ldrh	r0, [r4, #36]
	bl	FtlFreeSysBlkQueueInit
	bl	FtlLoadBbt
	cbnz	r0, .L1807
	bl	FtlSysBlkInit
	cbnz	r0, .L1807
	movs	r3, #1
	str	r3, [r5]
	ldrh	r3, [r4, #2344]
	cmp	r3, #15
	bhi	.L1807
	movw	r4, #8129
.L1806:
	movs	r1, #1
	movs	r0, #0
	bl	rk_ftl_garbage_collect
	subs	r4, r4, #1
	bne	.L1806
.L1807:
	movs	r0, #0
	pop	{r3, r4, r5, pc}
.L1810:
	.align	2
.L1809:
	.word	.LANCHOR0
	.word	.LANCHOR2
	.word	.LC70
	.word	.LC71
	.fnend
	.size	sftl_init, .-sftl_init
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
	ldr	r2, .L1815
	ldr	r3, [r2, #-172]
	cbnz	r3, .L1814
	str	r0, [r2, #-172]
	mov	r0, r3
	str	r1, [r2, #-168]
	bx	lr
.L1814:
	mov	r0, #-1
	bx	lr
.L1816:
	.align	2
.L1815:
	.word	.LANCHOR3
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
	push	{r3, r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.save {r3, r4, r5, r6, r7, r8, r9, r10, fp, lr}
	mov	r0, #65536
	ldr	r4, .L1825
	bl	ftl_malloc
	str	r0, [r4, #-164]
	cmp	r0, #0
	beq	.L1823
	ldr	fp, .L1825+4
	mov	r8, #0
	ldr	r9, .L1825+8
	mov	r7, r8
	mov	r6, r8
.L1821:
	ldr	r3, [r4, #-172]
	movs	r1, #128
	ldr	r2, [r4, #-164]
	lsls	r0, r6, #7
	blx	r3
	mov	r5, r0
	cbnz	r0, .L1819
	ldr	r1, [r4, #-164]
	movw	r10, #65532
	mov	r0, fp
	ldr	r3, [r1, #4]
	ldr	r2, [r1, r10]
	ldr	r1, [r1]
	bl	printk
	ldr	r2, [r4, #-164]
	ldr	r3, [r2]
	cmp	r3, r9
	bne	.L1820
	ldr	r3, [r2, r10]
	ldr	r1, [r2, #4]
	cmp	r3, r1
	bne	.L1820
	cmp	r3, r7
	itt	hi
	movhi	r8, r6
	movhi	r7, r3
.L1820:
	adds	r6, r6, #1
	cmp	r6, #2
	bne	.L1821
	cbz	r7, .L1822
	ldr	r3, [r4, #-172]
	movs	r1, #128
	lsl	r0, r8, #7
	blx	r3
	mov	r5, r0
	cbz	r0, .L1817
.L1819:
	ldr	r0, [r4, #-164]
	mov	r5, #-1
	bl	kfree
	movs	r3, #0
	str	r3, [r4, #-164]
	b	.L1817
.L1822:
	mov	r1, #65536
	mov	r0, r2
	bl	__memzero
	ldr	r3, [r4, #-164]
	movs	r2, #1
	movw	r1, #65532
	str	r2, [r3, #4]
	str	r9, [r3]
	str	r2, [r3, r1]
	movw	r2, #64504
	strh	r7, [r3, #12]	@ movhi
	strh	r2, [r3, #14]	@ movhi
.L1817:
	mov	r0, r5
	pop	{r3, r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L1823:
	mvn	r5, #11
	b	.L1817
.L1826:
	.align	2
.L1825:
	.word	.LANCHOR3
	.word	.LC106
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
	ldr	r3, .L1833
	mov	r0, r1
	ldr	r5, [r3, #-164]
	cbz	r5, .L1832
	ldrh	r6, [r5, #10]
	movs	r3, #0
.L1829:
	cmp	r3, r6
	bcc	.L1831
.L1832:
	mov	r0, #-1
	pop	{r3, r4, r5, r6, r7, pc}
.L1831:
	add	r1, r5, r3, lsl #3
	ldrh	r4, [r1, #16]
	cmp	r4, r7
	bne	.L1830
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
.L1830:
	adds	r3, r3, #1
	b	.L1829
.L1834:
	.align	2
.L1833:
	.word	.LANCHOR3
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
	ldr	r2, .L1854
	.pad #28
	sub	sp, sp, #28
	mov	r3, r1
	ldr	r4, [r2, #-164]
	mov	r9, r2
	cmp	r4, #0
	beq	.L1850
	ldrh	r2, [r4, #10]
	add	r6, r8, #63
	ldrh	r1, [r4, #8]
	bic	r6, r6, #63
	movs	r7, #0
	str	r1, [sp, #4]
.L1837:
	cmp	r7, r2
	bcc	.L1845
	ldrh	r1, [r4, #14]
	cmp	r6, r1
	bhi	.L1850
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
	ldr	r3, [r9, #-168]
	b	.L1853
.L1845:
	add	r5, r4, r7, lsl #3
	ldrh	r1, [r5, #16]
	cmp	r1, r0
	str	r1, [sp, #8]
	bne	.L1838
	ldrh	r1, [r5, #20]
	add	fp, r4, #1024
	adds	r1, r1, #63
	bic	r1, r1, #63
	cmp	r8, r1
	str	r1, [sp, #12]
	bls	.L1839
	ldrh	r1, [r4, #14]
	cmp	r6, r1
	bhi	.L1850
	ldrh	r10, [r5, #18]
	subs	r2, r2, #1
	str	r2, [sp, #16]
.L1840:
	ldr	r2, [sp, #16]
	adds	r5, r5, #8
	cmp	r7, r2
	bcc	.L1841
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
.L1842:
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
	ldr	r3, .L1854
	ldr	r3, [r3, #-168]
.L1853:
	ldr	r0, [sp, #4]
	mov	r2, r4
	movs	r1, #128
	lsls	r0, r0, #7
	blx	r3
	movs	r0, #0
.L1835:
	add	sp, sp, #28
	@ sp needed
	pop	{r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L1841:
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
	b	.L1840
.L1839:
	ldrh	r0, [r5, #18]
	mov	r2, r8
	mov	r1, r3
	add	r0, r0, fp
	bl	memcpy
	strh	r8, [r5, #20]	@ movhi
	b	.L1842
.L1838:
	adds	r7, r7, #1
	b	.L1837
.L1850:
	mov	r0, #-1
	b	.L1835
.L1855:
	.align	2
.L1854:
	.word	.LANCHOR3
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
	mov	r5, r2
	bl	ftl_malloc
	mov	r4, r0
	cmp	r0, #0
	beq	.L1870
	ldr	r3, .L1882
	cmp	r6, r3
	beq	.L1859
	adds	r3, r3, #1
	cmp	r6, r3
	beq	.L1860
.L1881:
	mvn	r5, #13
	b	.L1858
.L1859:
	ldr	r6, .L1882+4
	mov	r3, sp
	ands	r3, r3, r6
	ldr	r3, [r3, #72]
	.syntax unified
@ 552 "./arch/arm/include/asm/uaccess.h" 1
	adds r2, r5, #8; sbcccs r2, r2, r3; movcc r3, #0
@ 0 "" 2
	.thumb
	.syntax unified
	cmp	r3, #0
	bne	.L1866
	movs	r2, #8
	mov	r1, r5
	bl	arm_copy_from_user
	cmp	r0, #0
	bne	.L1881
	ldr	r2, [r4]
	ldr	r3, .L1882+8
	cmp	r2, r3
	beq	.L1864
.L1865:
	mov	r5, #-1
.L1858:
	mov	r0, r4
	bl	kfree
.L1856:
	mov	r0, r5
	pop	{r4, r5, r6, pc}
.L1864:
	ldrh	r2, [r4, #6]
	add	r1, r4, #8
	ldrh	r0, [r4, #4]
	bl	rk_sftl_vendor_read
	adds	r3, r0, #1
	beq	.L1865
	mov	r3, sp
	uxth	r2, r0
	ands	r6, r6, r3
	strh	r0, [r4, #6]	@ movhi
	adds	r2, r2, #8
	ldr	r3, [r6, #72]
	.syntax unified
@ 561 "./arch/arm/include/asm/uaccess.h" 1
	adds r1, r5, r2; sbcccs r1, r1, r3; movcc r3, #0
@ 0 "" 2
	.thumb
	.syntax unified
	cmp	r3, #0
	bne	.L1881
	mov	r1, r4
	mov	r0, r5
	bl	arm_copy_to_user
	cmp	r0, #0
	bne	.L1881
.L1872:
	mov	r5, r0
	b	.L1858
.L1860:
	ldr	r6, .L1882+4
	mov	r3, sp
	ands	r3, r3, r6
	ldr	r3, [r3, #72]
	.syntax unified
@ 552 "./arch/arm/include/asm/uaccess.h" 1
	adds r2, r5, #8; sbcccs r2, r2, r3; movcc r3, #0
@ 0 "" 2
	.thumb
	.syntax unified
	cbnz	r3, .L1866
	movs	r2, #8
	mov	r1, r5
	bl	arm_copy_from_user
	cmp	r0, #0
	bne	.L1881
	ldr	r2, [r4]
	ldr	r3, .L1882+8
	cmp	r2, r3
	bne	.L1865
	ldrh	r2, [r4, #6]
	movw	r3, #4087
	cmp	r2, r3
	bhi	.L1865
	mov	r3, sp
	adds	r2, r2, #8
	ands	r6, r6, r3
	ldr	r3, [r6, #72]
	.syntax unified
@ 552 "./arch/arm/include/asm/uaccess.h" 1
	adds r1, r5, r2; sbcccs r1, r1, r3; movcc r3, #0
@ 0 "" 2
	.thumb
	.syntax unified
	cbnz	r3, .L1868
	mov	r1, r5
	mov	r0, r4
	bl	arm_copy_from_user
	cmp	r0, #0
	bne	.L1881
	ldrh	r2, [r4, #6]
	add	r1, r4, #8
	ldrh	r0, [r4, #4]
	bl	rk_sftl_vendor_write
	b	.L1872
.L1866:
	movs	r1, #8
.L1880:
	bl	__memzero
	b	.L1881
.L1868:
	mov	r1, r2
	mov	r0, r4
	b	.L1880
.L1870:
	mov	r5, #-1
	b	.L1856
.L1883:
	.align	2
.L1882:
	.word	1074034177
	.word	-8192
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
	ldr	r0, .L1885
	b	misc_register
.L1886:
	.align	2
.L1885:
	.word	.LANCHOR2+12
	.fnend
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
	.type	__func__.6666, %object
	.size	__func__.6666, 17
__func__.6666:
	.ascii	"INSERT_DATA_LIST\000"
	.type	__func__.6661, %object
	.size	__func__.6661, 17
__func__.6661:
	.ascii	"INSERT_FREE_LIST\000"
	.type	__func__.6697, %object
	.size	__func__.6697, 17
__func__.6697:
	.ascii	"List_remove_node\000"
	.type	__func__.6729, %object
	.size	__func__.6729, 22
__func__.6729:
	.ascii	"List_update_data_list\000"
	.type	__func__.6835, %object
	.size	__func__.6835, 22
__func__.6835:
	.ascii	"select_l2p_ram_region\000"
	.type	__func__.7158, %object
	.size	__func__.7158, 16
__func__.7158:
	.ascii	"make_superblock\000"
	.type	__func__.7333, %object
	.size	__func__.7333, 19
__func__.7333:
	.ascii	"get_new_active_ppa\000"
	.type	__func__.17662, %object
	.size	__func__.17662, 15
__func__.17662:
	.ascii	"FlashReadPages\000"
	.type	__func__.17679, %object
	.size	__func__.17679, 15
__func__.17679:
	.ascii	"FlashProgPages\000"
	.type	__func__.6771, %object
	.size	__func__.6771, 26
__func__.6771:
	.ascii	"ftl_map_blk_alloc_new_blk\000"
	.type	__func__.6275, %object
	.size	__func__.6275, 11
__func__.6275:
	.ascii	"FtlMemInit\000"
	.type	__func__.6501, %object
	.size	__func__.6501, 14
__func__.6501:
	.ascii	"FtlBbt2Bitmap\000"
	.type	__func__.6738, %object
	.size	__func__.6738, 16
__func__.6738:
	.ascii	"load_l2p_region\000"
	.type	__func__.6796, %object
	.size	__func__.6796, 31
__func__.6796:
	.ascii	"Ftl_write_map_blk_to_last_page\000"
	.type	__func__.6810, %object
	.size	__func__.6810, 16
__func__.6810:
	.ascii	"FtlMapWritePage\000"
	.type	__func__.6782, %object
	.size	__func__.6782, 15
__func__.6782:
	.ascii	"ftl_map_blk_gc\000"
	.type	__func__.6852, %object
	.size	__func__.6852, 9
__func__.6852:
	.ascii	"log2phys\000"
	.type	__func__.7067, %object
	.size	__func__.7067, 16
__func__.7067:
	.ascii	"FtlReUsePrevPpa\000"
	.type	__func__.7220, %object
	.size	__func__.7220, 14
__func__.7220:
	.ascii	"ftl_check_vpc\000"
	.type	__func__.6947, %object
	.size	__func__.6947, 14
__func__.6947:
	.ascii	"FtlScanSysBlk\000"
	.type	__func__.7179, %object
	.size	__func__.7179, 18
__func__.7179:
	.ascii	"SupperBlkListInit\000"
	.type	__func__.7305, %object
	.size	__func__.7305, 25
__func__.7305:
	.ascii	"allocate_data_superblock\000"
	.type	__func__.7346, %object
	.size	__func__.7346, 16
__func__.7346:
	.ascii	"update_vpc_list\000"
	.type	__func__.7353, %object
	.size	__func__.7353, 20
__func__.7353:
	.ascii	"decrement_vpc_count\000"
	.type	__func__.7101, %object
	.size	__func__.7101, 22
__func__.7101:
	.ascii	"FtlRecoverySuperblock\000"
	.type	__func__.6544, %object
	.size	__func__.6544, 11
__func__.6544:
	.ascii	"FtlLoadBbt\000"
	.type	__func__.6925, %object
	.size	__func__.6925, 15
__func__.6925:
	.ascii	"FtlVpcTblFlush\000"
	.type	__func__.7204, %object
	.size	__func__.7204, 21
__func__.7204:
	.ascii	"FtlVpcCheckAndModify\000"
	.type	__func__.7326, %object
	.size	__func__.7326, 29
__func__.7326:
	.ascii	"allocate_new_data_superblock\000"
	.type	__func__.6399, %object
	.size	__func__.6399, 13
__func__.6399:
	.ascii	"FtlProgPages\000"
	.type	__func__.7423, %object
	.size	__func__.7423, 19
__func__.7423:
	.ascii	"FtlGcFreeTempBlock\000"
	.type	__func__.7529, %object
	.size	__func__.7529, 23
__func__.7529:
	.ascii	"rk_ftl_garbage_collect\000"
	.type	__func__.6427, %object
	.size	__func__.6427, 9
__func__.6427:
	.ascii	"FtlWrite\000"
	.type	__func__.7004, %object
	.size	__func__.7004, 15
__func__.7004:
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
	.word	.LC107
	.word	rk_sftl_vendor_storage_fops
	.space	28
	.bss
	.align	2
	.set	.LANCHOR0,. + 0
	.set	.LANCHOR3,. + 4344
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
	.ascii	"\0014\000"
.LC76:
	.ascii	"prog read error: = %x\012\000"
.LC77:
	.ascii	"prog read s error: = %x %x %x\012\000"
.LC78:
	.ascii	"prog read d error: = %x %x %x\012\000"
.LC79:
	.ascii	"FtlFreeSysBlkQueueOut free count = %d\012\000"
.LC80:
	.ascii	"FtlFreeSysBlkQueueOut = %x, free count = %d, error\012"
	.ascii	"\000"
.LC81:
	.ascii	"FtlFreeSysBlkQueueOut = %x, free count = %d\012\000"
.LC82:
	.ascii	"%s error allocating memory. return -1\012\000"
.LC83:
	.ascii	"region_id = %x phyAddr = %x\012\000"
.LC84:
	.ascii	"spare:\000"
.LC85:
	.ascii	"map_ppn:\000"
.LC86:
	.ascii	"FtlMapWritePage error = %x \012\000"
.LC87:
	.ascii	"FtlMapWritePage error = %x error count = %d\012\000"
.LC88:
	.ascii	"ftl_scan_all_data = %x\012\000"
.LC89:
	.ascii	"scan lpa = %x ppa= %x\012\000"
.LC90:
	.ascii	"lba = %x,addr= %x,spare= %x %x %x %x data=%x %x\012"
	.ascii	"\000"
.LC91:
	.ascii	"...%s enter...\012\000"
.LC92:
	.ascii	"FtlCheckVpc2 %x = %x  %x\012\000"
.LC93:
	.ascii	"free blk vpc error %x = %x  %x\012\000"
.LC94:
	.ascii	"FtlBbmTblFlush id=%x,page=%x,previd=%x cnt=%d\012\000"
.LC95:
	.ascii	"FtlBbmTblFlush error:%x\012\000"
.LC96:
	.ascii	"FtlBbmTblFlush error = %x error count = %d\012\000"
.LC97:
	.ascii	"FtlGcFreeBadSuperBlk 0x%x\012\000"
.LC98:
	.ascii	"decrement_vpc_count %x = %d\012\000"
.LC99:
	.ascii	"spuer block %x vpn is 0\012 \000"
.LC100:
	.ascii	"no ect\000"
.LC101:
	.ascii	"FtlVpcTblFlush error = %x error count = %d\012\000"
.LC102:
	.ascii	"FtlCheckVpc %x = %x  %x\012\000"
.LC103:
	.ascii	"Ftlwrite decrement_vpc_count %x = %d\012\000"
.LC104:
	.ascii	"SWL %x, FSB = %x vpc= %x,ec=%x th=%x\012\000"
.LC105:
	.ascii	"FtlWrite: lpa error:%x %x\012\000"
.LC106:
	.ascii	"\0013vendor storage %x,%x,%x\012\000"
.LC107:
	.ascii	"vendor_storage\000"