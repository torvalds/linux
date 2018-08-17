/* SPDX-License-Identifier: GPL-2.0 */

/*
 * Copyright (c) 2018 Rockchip Electronics Co. Ltd.
 * date: 2018-08-17
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
	ldr	r3, .L38
	ldr	r0, [r3, #140]
	bx	lr
.L39:
	.align	2
.L38:
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
	ldr	r4, .L41
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
	ldr	r0, .L41+4
	bl	sftl_printk
	ldrh	r3, [r4, #158]
	movs	r0, #0
	adds	r3, r3, #1
	strh	r3, [r4, #158]	@ movhi
	add	sp, sp, #12
	@ sp needed
	pop	{r4, r5, r6, r7, pc}
.L42:
	.align	2
.L41:
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
	ldr	r5, .L44
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
.L45:
	.align	2
.L44:
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
	ldr	r0, .L48
	movw	r3, #65535
	movs	r2, #16
	movs	r1, #255
	strh	r3, [r0, #152]	@ movhi
	movs	r3, #0
	strh	r3, [r0, #158]	@ movhi
	adds	r0, r0, #164
	b	memset
.L49:
	.align	2
.L48:
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
	ldr	r3, .L57
	push	{r4, r5, r6, lr}
	.save {r4, r5, r6, lr}
	movs	r5, #0
	mov	r4, r5
	ldrh	r2, [r3, #98]
	ldrh	r6, [r3, #54]
	muls	r6, r2, r6
.L51:
	uxth	r0, r5
	cmp	r0, r6
	blt	.L53
	mov	r0, r4
	pop	{r4, r5, r6, pc}
.L53:
	bl	FtlBbmIsBadBlock
	cbz	r0, .L52
	adds	r4, r4, #1
	uxth	r4, r4
.L52:
	adds	r5, r5, #1
	b	.L51
.L58:
	.align	2
.L57:
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
	ldr	r4, .L60
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
.L61:
	.align	2
.L60:
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
	ldr	r3, .L63
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
.L64:
	.align	2
.L63:
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
	ldr	r4, .L66
	ldrh	r1, [r4, #98]
	bl	__aeabi_uidivmod
	uxth	r0, r1
	ldrh	r1, [r4, #56]
	bl	__aeabi_uidiv
	uxth	r0, r0
	pop	{r4, pc}
.L67:
	.align	2
.L66:
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
	bls	.L69
	subs	r0, r0, r1
	cmp	r0, #-2147483648
	ite	hi
	movhi	r0, #0
	movls	r0, #1
	bx	lr
.L69:
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
	ldr	r3, .L72
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
.L73:
	.align	2
.L72:
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
	ldr	r3, .L75
	ldrh	r0, [r3, #218]
	clz	r0, r0
	lsrs	r0, r0, #5
	bx	lr
.L76:
	.align	2
.L75:
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
	ldr	r3, .L78
	ldrh	r0, [r3, #218]
	sub	r3, r0, #1024
	rsbs	r0, r3, #0
	adcs	r0, r0, r3
	bx	lr
.L79:
	.align	2
.L78:
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
	ldr	r3, .L89
	push	{r4, r5, r6, lr}
	.save {r4, r5, r6, lr}
	ldrh	r2, [r3, #218]
	cbz	r2, .L80
	ldrh	r5, [r3, #2296]
	movs	r0, #0
	ldrh	r1, [r3, #214]
	mov	r6, r0
	ldrh	r2, [r3, #216]
	and	r5, r5, #31
.L82:
	uxth	r4, r0
	adds	r0, r0, #1
	cmp	r5, r4
	bgt	.L83
	cbz	r6, .L80
	strh	r1, [r3, #214]	@ movhi
	strh	r2, [r3, #216]	@ movhi
.L80:
	pop	{r4, r5, r6, pc}
.L83:
	add	r4, r3, r1, lsl #1
	adds	r1, r1, #1
	ubfx	r1, r1, #0, #10
	ldrh	r6, [r4, #220]
	add	r4, r3, r2, lsl #1
	strh	r6, [r4, #220]	@ movhi
	movs	r6, #1
	add	r2, r2, r6
	ubfx	r2, r2, #0, #10
	b	.L82
.L90:
	.align	2
.L89:
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
	ldr	r3, .L97
	push	{r4, r5, lr}
	.save {r4, r5, lr}
	ldrh	r4, [r3, #218]
	cmp	r4, #1024
	beq	.L95
	ldrh	r5, [r3, #214]
	movs	r1, #0
.L93:
	cmp	r1, r4
	bcc	.L94
.L95:
	movs	r0, #0
	pop	{r4, r5, pc}
.L94:
	adds	r2, r1, r5
	ubfx	r2, r2, #0, #10
	add	r2, r3, r2, lsl #1
	ldrh	r2, [r2, #220]
	cmp	r2, r0
	beq	.L96
	adds	r1, r1, #1
	b	.L93
.L96:
	movs	r0, #1
	pop	{r4, r5, pc}
.L98:
	.align	2
.L97:
	.word	.LANCHOR0
	.fnend
	.size	IsInFreeQueue, .-IsInFreeQueue
	.align	1
	.global	test_node_in_list
	.syntax unified
	.thumb
	.thumb_func
	.fpu softvfp
	.type	test_node_in_list, %function
test_node_in_list:
	.fnstart
	@ args = 0, pretend = 0, frame = 0
	@ frame_needed = 0, uses_anonymous_args = 0
	ldr	r3, .L104
	push	{r4, r5, lr}
	.save {r4, r5, lr}
	movw	r5, #65535
	ldr	r2, [r0]
	ldr	r4, [r3, #2316]
	subs	r3, r2, r4
	asrs	r0, r3, #1
	ldr	r3, .L104+4
	muls	r3, r0, r3
	movs	r0, #6
	uxth	r3, r3
.L101:
	cmp	r3, r1
	beq	.L102
	ldrh	r3, [r2]
	cmp	r3, r5
	beq	.L103
	mla	r2, r0, r3, r4
	b	.L101
.L102:
	movs	r0, #1
	pop	{r4, r5, pc}
.L103:
	movs	r0, #0
	pop	{r4, r5, pc}
.L105:
	.align	2
.L104:
	.word	.LANCHOR0
	.word	-1431655765
	.fnend
	.size	test_node_in_list, .-test_node_in_list
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
	ldr	r2, .L121
	ldrh	r3, [r2, #40]
	mov	r5, r2
	cmp	r3, r0
	bls	.L108
	movs	r4, #6
	ldr	ip, [r2, #2316]
	muls	r4, r0, r4
	movw	r3, #65535
	add	r1, ip, r4
	strh	r3, [r1, #2]	@ movhi
	strh	r3, [ip, r4]	@ movhi
	ldr	r3, [r2, #2320]
	cbnz	r3, .L109
	str	r1, [r2, #2320]
.L108:
	movs	r0, #0
	add	sp, sp, #12
	@ sp needed
	pop	{r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L109:
	ldrh	r6, [r1, #4]
	lsl	r10, r0, #1
	ldr	r8, [r2, #2324]
	ldrh	r2, [r8, r0, lsl #1]
	cmp	r6, #0
	beq	.L119
	muls	r6, r2, r6
.L110:
	ldr	lr, [r5, #2316]
	ldrh	r7, [r5, #40]
	sub	r2, r3, lr
	asr	r9, r2, #1
	ldr	r2, .L121+4
	str	r7, [sp]
	mul	r2, r2, r9
	ldr	r9, [r5, #2328]
	add	r5, r9, r10
	uxth	r2, r2
	str	r5, [sp, #4]
	movs	r5, #0
.L117:
	adds	r5, r5, #1
	ldr	r7, [sp]
	uxth	r5, r5
	cmp	r5, r7
	bhi	.L108
	cmp	r0, r2
	beq	.L108
	ldrh	r7, [r3, #4]
	lsl	r10, r2, #1
	ldrh	fp, [r8, r2, lsl #1]
	cbz	r7, .L120
	mul	r7, r7, fp
.L112:
	cmp	r6, r7
	bne	.L113
	ldr	r7, [sp, #4]
	ldrh	r10, [r9, r10]
	ldrh	r7, [r7]
	cmp	r10, r7
	bcc	.L115
.L114:
	strh	r2, [ip, r4]	@ movhi
	ldr	r4, .L121
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
	b	.L108
.L119:
	mov	r6, #-1
	b	.L110
.L120:
	mov	r7, #-1
	b	.L112
.L113:
	bcc	.L114
.L115:
	ldrh	r7, [r3]
	movw	r10, #65535
	cmp	r7, r10
	bne	.L116
	strh	r2, [r1, #2]	@ movhi
	strh	r0, [r3]	@ movhi
	ldr	r3, .L121
	str	r1, [r3, #2332]
	b	.L108
.L116:
	movs	r3, #6
	mov	r2, r7
	mla	r3, r3, r7, lr
	b	.L117
.L122:
	.align	2
.L121:
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
	ldr	r2, .L125
	ldrh	r3, [r2, #2336]
	adds	r3, r3, #1
	uxth	r3, r3
	strh	r3, [r2, #2336]	@ movhi
	ldrh	r2, [r2, #40]
	cmp	r2, r3
	bcs	.L123
	movs	r2, #221
	ldr	r1, .L125+4
	ldr	r0, .L125+8
	pop	{r3, lr}
	b	sftl_printk
.L123:
	pop	{r3, pc}
.L126:
	.align	2
.L125:
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
	beq	.L128
	ldr	r2, .L134
	movs	r1, #6
	mul	r7, r1, r0
	ldr	ip, [r2, #2316]
	mov	r5, r2
	add	r6, ip, r7
	strh	r4, [r6, #2]	@ movhi
	strh	r4, [ip, r7]	@ movhi
	ldr	r3, [r2, #2340]
	cbnz	r3, .L129
	str	r6, [r2, #2340]
.L128:
	movs	r0, #0
	pop	{r4, r5, r6, r7, r8, r9, r10, pc}
.L129:
	ldr	lr, [r2, #2316]
	ldr	r8, [r2, #2328]
	sub	r2, r3, lr
	asr	r10, r2, #1
	ldr	r2, .L134+4
	ldrh	r9, [r8, r0, lsl #1]
	mul	r2, r2, r10
	mov	r10, r4
	uxth	r2, r2
.L132:
	ldrh	r4, [r8, r2, lsl #1]
	cmp	r4, r9
	bcs	.L130
	ldrh	r4, [r3]
	cmp	r4, r10
	bne	.L131
	strh	r2, [r6, #2]	@ movhi
	strh	r0, [r3]	@ movhi
	b	.L128
.L131:
	mla	r3, r1, r4, lr
	mov	r2, r4
	b	.L132
.L130:
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
	b	.L128
.L135:
	.align	2
.L134:
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
	ldr	r2, .L138
	ldrh	r3, [r2, #2344]
	adds	r3, r3, #1
	uxth	r3, r3
	strh	r3, [r2, #2344]	@ movhi
	ldrh	r2, [r2, #40]
	cmp	r2, r3
	bcs	.L136
	movs	r2, #214
	ldr	r1, .L138+4
	ldr	r0, .L138+8
	pop	{r3, lr}
	b	sftl_printk
.L136:
	pop	{r3, pc}
.L139:
	.align	2
.L138:
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
	ldr	r4, .L146
	muls	r6, r1, r6
	movw	r3, #65535
	mov	r8, r0
	ldr	r7, [r4, #2316]
	adds	r5, r7, r6
	ldrh	r2, [r5, #2]
	cmp	r2, r3
	bne	.L141
	ldr	r3, [r0]
	cmp	r5, r3
	beq	.L141
	movw	r2, #397
	ldr	r1, .L146+4
	ldr	r0, .L146+8
	bl	sftl_printk
.L141:
	ldr	r3, [r8]
	movw	r1, #65535
	cmp	r5, r3
	ldrh	r3, [r7, r6]
	bne	.L142
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
.L144:
	movw	r3, #65535
	movs	r0, #0
	strh	r3, [r7, r6]	@ movhi
	strh	r3, [r5, #2]	@ movhi
	pop	{r4, r5, r6, r7, r8, pc}
.L142:
	cmp	r3, r1
	ldrh	r1, [r5, #2]
	bne	.L145
	cmp	r1, r3
	beq	.L144
	movs	r2, #6
	ldr	r0, [r4, #2316]
	muls	r1, r2, r1
	strh	r3, [r0, r1]	@ movhi
	b	.L144
.L145:
	ldr	r0, [r4, #2316]
	movs	r2, #6
	mla	r3, r2, r3, r0
	strh	r1, [r3, #2]	@ movhi
	ldrh	r0, [r5, #2]
	ldrh	r1, [r7, r6]
	ldr	r3, [r4, #2316]
	muls	r2, r0, r2
	strh	r1, [r3, r2]	@ movhi
	b	.L144
.L147:
	.align	2
.L146:
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
	cbz	r3, .L154
	ldr	r2, .L155
	movw	r5, #65535
	movs	r6, #6
	ldr	r2, [r2, #2316]
.L150:
	cbnz	r1, .L151
.L153:
	ldr	r4, .L155+4
	subs	r3, r3, r2
	asrs	r3, r3, #1
	muls	r4, r3, r4
	uxth	r1, r4
	bl	List_remove_node
	uxth	r0, r4
	pop	{r4, r5, r6, pc}
.L151:
	ldrh	r4, [r3]
	cmp	r4, r5
	beq	.L153
	subs	r1, r1, #1
	mla	r3, r6, r4, r2
	uxth	r1, r1
	b	.L150
.L154:
	movw	r0, #65535
	pop	{r4, r5, r6, pc}
.L156:
	.align	2
.L155:
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
	ldr	r2, .L164
	push	{r4, lr}
	.save {r4, lr}
	ldr	r3, [r2, #2320]
	cbz	r3, .L163
	ldr	r1, [r2, #2316]
	movs	r4, #6
	movw	r2, #65535
.L160:
	cbz	r0, .L161
	ldrh	r3, [r3]
	cmp	r3, r2
	bne	.L162
.L163:
	movw	r0, #65535
	pop	{r4, pc}
.L162:
	subs	r0, r0, #1
	mla	r3, r4, r3, r1
	uxth	r0, r0
	b	.L160
.L161:
	ldr	r0, .L164+4
	subs	r3, r3, r1
	asrs	r3, r3, #1
	muls	r3, r0, r3
	uxth	r0, r3
	pop	{r4, pc}
.L165:
	.align	2
.L164:
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
	ldr	r4, .L177
	ldrh	r3, [r4, #2348]
	cmp	r3, r0
	beq	.L168
	ldrh	r3, [r4, #2396]
	cmp	r3, r0
	beq	.L168
	ldrh	r3, [r4, #2444]
	cmp	r3, r0
	beq	.L168
	movs	r7, #6
	ldr	r9, [r4, #2316]
	muls	r7, r0, r7
	ldr	r3, [r4, #2320]
	add	r8, r9, r7
	cmp	r8, r3
	beq	.L168
	ldrh	r6, [r8, #4]
	ldr	r3, [r4, #2324]
	ldrh	r3, [r3, r0, lsl #1]
	cmp	r6, #0
	beq	.L175
	muls	r6, r3, r6
.L170:
	ldrh	r3, [r8, #2]
	movw	r2, #65535
	cmp	r3, r2
	bne	.L171
	ldrh	r2, [r9, r7]
	cmp	r2, r3
	bne	.L171
	mov	r2, #488
	ldr	r1, .L177+4
	ldr	r0, .L177+8
	bl	sftl_printk
.L171:
	ldrh	r3, [r8, #2]
	movw	r2, #65535
	cmp	r3, r2
	bne	.L172
	ldrh	r2, [r9, r7]
	cmp	r2, r3
	beq	.L168
.L172:
	movs	r2, #6
	muls	r2, r3, r2
	ldr	r3, .L177+12
	asrs	r1, r2, #1
	muls	r3, r1, r3
	ldr	r1, [r4, #2324]
	ldrh	r0, [r1, r3, lsl #1]
	ldr	r1, [r4, #2316]
	add	r2, r2, r1
	ldrh	r3, [r2, #4]
	cbz	r3, .L176
	muls	r3, r0, r3
.L173:
	cmp	r6, r3
	bcs	.L168
	mov	r1, r5
	ldr	r0, .L177+16
	bl	List_remove_node
	ldrh	r3, [r4, #2336]
	cbnz	r3, .L174
	movw	r2, #499
	ldr	r1, .L177+4
	ldr	r0, .L177+8
	bl	sftl_printk
.L174:
	ldrh	r3, [r4, #2336]
	mov	r0, r5
	subs	r3, r3, #1
	strh	r3, [r4, #2336]	@ movhi
	bl	INSERT_DATA_LIST
.L168:
	movs	r0, #0
	pop	{r3, r4, r5, r6, r7, r8, r9, pc}
.L175:
	mov	r6, #-1
	b	.L170
.L176:
	mov	r3, #-1
	b	.L173
.L178:
	.align	2
.L177:
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
	ldr	r5, .L189
	movs	r0, #12
	movw	r6, #65535
	ldrh	r2, [r5, #138]
	ldr	r3, [r5, #2492]
.L180:
	uxth	r4, r1
	cmp	r4, r2
	bcc	.L182
	mov	r4, r2
	movs	r1, #0
	mov	r7, #-2147483648
	mov	ip, #12
.L183:
	uxth	r6, r1
	cmp	r6, r2
	bcc	.L185
	cmp	r4, r2
	bcc	.L181
	ldrh	r7, [r5, #2496]
	mov	r4, r2
	movs	r1, #0
	mov	r0, #-1
.L186:
	uxth	r5, r1
	cmp	r5, r2
	bcc	.L188
	cmp	r4, r2
	bcc	.L181
	movw	r2, #823
	ldr	r1, .L189+4
	ldr	r0, .L189+8
	bl	sftl_printk
	b	.L181
.L182:
	adds	r1, r1, #1
	mla	r7, r0, r1, r3
	ldrh	r7, [r7, #-12]
	cmp	r7, r6
	bne	.L180
.L181:
	mov	r0, r4
	pop	{r3, r4, r5, r6, r7, pc}
.L185:
	mla	r0, ip, r1, r3
	ldr	r0, [r0, #4]
	cmp	r0, #0
	blt	.L184
	cmp	r7, r0
	itt	hi
	movhi	r7, r0
	movhi	r4, r6
.L184:
	adds	r1, r1, #1
	b	.L183
.L188:
	ldr	r6, [r3, #4]
	cmp	r0, r6
	bls	.L187
	ldrh	ip, [r3]
	cmp	ip, r7
	itt	ne
	movne	r0, r6
	movne	r4, r5
.L187:
	adds	r1, r1, #1
	adds	r3, r3, #12
	b	.L186
.L190:
	.align	2
.L189:
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
	ldr	r1, .L197
	push	{r4, r5, lr}
	.save {r4, r5, lr}
	mov	r3, r1
	ldrh	r2, [r1, #2498]
	cmp	r2, #4
	bhi	.L192
	cbnz	r0, .L192
	adds	r2, r2, #1
	strh	r2, [r1, #2498]	@ movhi
	pop	{r4, r5, pc}
.L192:
	movs	r2, #0
	ldrh	r1, [r3, #40]
	strh	r2, [r3, #2498]	@ movhi
	movw	r5, #65535
	str	r2, [r3, #2500]
	ldr	r2, [r3, #2324]
	add	r1, r2, r1, lsl #1
.L193:
	cmp	r2, r1
	bne	.L195
	pop	{r4, r5, pc}
.L195:
	ldrh	r4, [r2], #2
	cmp	r4, r5
	ittt	ne
	ldrne	r0, [r3, #2500]
	addne	r0, r0, r4
	strne	r0, [r3, #2500]
	b	.L193
.L198:
	.align	2
.L197:
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
	cbz	r1, .L200
	b	ftl_set_blk_mode.part.6
.L200:
	ldr	r2, .L201
	lsrs	r0, r0, #5
	and	r3, r3, #31
	ldr	r1, [r2, #24]
	movs	r2, #1
	lsl	r3, r2, r3
	ldr	r2, [r1, r0, lsl #2]
	bic	r2, r2, r3
	str	r2, [r1, r0, lsl #2]
	bx	lr
.L202:
	.align	2
.L201:
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
	ldr	r3, .L204
	lsrs	r2, r0, #5
	and	r0, r0, #31
	ldr	r3, [r3, #24]
	ldr	r3, [r3, r2, lsl #2]
	lsr	r0, r3, r0
	and	r0, r0, #1
	bx	lr
.L205:
	.align	2
.L204:
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
	ldr	r3, .L213
	adds	r4, r4, #14
	ldrh	r5, [r3, #32]
.L207:
	cmp	r2, r5
	bcc	.L209
	ldrh	r3, [r3, #102]
	add	r4, r0, #16
	movw	r6, #65535
	subs	r3, r3, #1
	subs	r1, r3, r1
	movs	r3, #0
	uxth	r1, r1
.L210:
	uxth	r2, r3
	cmp	r5, r2
	bhi	.L212
	pop	{r4, r5, r6, r7, pc}
.L209:
	ldrh	r6, [r4, #2]!
	adds	r2, r2, #1
	uxth	r2, r2
	cmp	r6, r7
	ittt	ne
	ldrhne	r6, [r0, #4]
	addne	r6, r6, #1
	strhne	r6, [r0, #4]	@ movhi
	b	.L207
.L212:
	ldrh	r2, [r4], #2
	adds	r3, r3, #1
	cmp	r2, r6
	ittt	ne
	ldrhne	r2, [r0, #4]
	addne	r2, r2, r1
	strhne	r2, [r0, #4]	@ movhi
	b	.L210
.L214:
	.align	2
.L213:
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
	cbz	r3, .L215
	ldrh	r2, [r0]
	movw	r3, #65535
	cmp	r2, r3
	beq	.L215
	ldrb	r2, [r0, #6]	@ zero_extendqisi2
	movs	r5, #0
	adds	r2, r2, #8
	ldrh	r1, [r0, r2, lsl #1]
	ldr	r2, .L221
	ldrh	r4, [r2, #32]
	mov	r2, r3
.L218:
	cmp	r1, r2
	beq	.L220
.L215:
	pop	{r4, r5, pc}
.L220:
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
	b	.L218
.L222:
	.align	2
.L221:
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
	ldr	r6, .L228
	ldrh	r2, [r0]
	ldrh	r3, [r6, #40]
	cmp	r2, r3
	bcc	.L224
	movw	r2, #2146
	ldr	r1, .L228+4
	ldr	r0, .L228+8
	bl	sftl_printk
.L224:
	ldrh	r9, [r6, #32]
	add	r7, r4, #16
	ldr	r10, .L228+12
	movw	r8, #65535
	movs	r5, #0
	strh	r5, [r4, #4]	@ movhi
	strb	r5, [r4, #7]
.L225:
	uxth	r3, r5
	cmp	r9, r3
	bhi	.L227
	ldrb	r3, [r4, #7]	@ zero_extendqisi2
	movs	r0, #0
	ldrh	r2, [r6, #102]
	smulbb	r3, r3, r2
	strh	r3, [r4, #4]	@ movhi
	movs	r3, #1
	strb	r3, [r4, #9]
	pop	{r3, r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L227:
	ldrh	r1, [r4]
	ldrb	r0, [r10, r5]	@ zero_extendqisi2
	bl	V2P_block
	strh	r8, [r7]	@ movhi
	mov	fp, r0
	bl	FtlBbmIsBadBlock
	cbnz	r0, .L226
	strh	fp, [r7]	@ movhi
	ldrb	r3, [r4, #7]	@ zero_extendqisi2
	adds	r3, r3, #1
	strb	r3, [r4, #7]
.L226:
	adds	r5, r5, #1
	adds	r7, r7, #2
	b	.L225
.L229:
	.align	2
.L228:
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
	ldr	r6, .L236
	mov	r7, r0
	mov	r4, r5
	ldrh	r8, [r6, #32]
	add	r10, r6, #60
	ldrh	r9, [r6, #102]
.L231:
	uxth	r3, r5
	cmp	r8, r3
	bhi	.L233
	cbz	r4, .L235
	mov	r1, r4
	mov	r0, #32768
	bl	__aeabi_idiv
.L234:
	ldr	r1, [r6, #2316]
	movs	r3, #6
	mla	r1, r3, r7, r1
	strh	r0, [r1, #4]	@ movhi
	movs	r0, #0
	pop	{r4, r5, r6, r7, r8, r9, r10, pc}
.L233:
	mov	r1, r7
	ldrb	r0, [r10, r5]	@ zero_extendqisi2
	bl	V2P_block
	bl	FtlBbmIsBadBlock
	cbnz	r0, .L232
	add	r4, r4, r9
	uxth	r4, r4
.L232:
	adds	r5, r5, #1
	b	.L231
.L235:
	mov	r0, r4
	b	.L234
.L237:
	.align	2
.L236:
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
	ldr	r2, .L241
	ldr	r0, [r2, #2340]
	cbz	r0, .L239
	ldr	r3, [r2, #2316]
	subs	r0, r0, r3
	ldr	r3, .L241+4
	asrs	r0, r0, #1
	muls	r0, r3, r0
	ldr	r3, [r2, #2328]
	uxth	r0, r0
	ldrh	r0, [r3, r0, lsl #1]
.L239:
	bx	lr
.L242:
	.align	2
.L241:
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
	ldr	r1, .L251
	push	{r4, r5, r6, r7, lr}
	.save {r4, r5, r6, r7, lr}
	ldr	r3, [r1, #2340]
	cbz	r3, .L249
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
	ldr	r2, .L251+4
	muls	r3, r2, r3
	movs	r2, #0
	uxth	r3, r3
.L246:
	uxth	r5, r2
	cmp	r0, r5
	bls	.L248
	mul	r5, r6, r3
	adds	r2, r2, #1
	ldrh	r5, [r4, r5]
	cmp	r5, r7
	bne	.L250
.L248:
	ldr	r2, [r1, #2328]
	ldrh	r0, [r2, r3, lsl #1]
	pop	{r4, r5, r6, r7, pc}
.L250:
	mov	r3, r5
	b	.L246
.L249:
	mov	r0, r3
	pop	{r4, r5, r6, r7, pc}
.L252:
	.align	2
.L251:
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
	ldr	r5, .L265
	add	r4, r6, #12
	.pad #20
	sub	sp, sp, #20
	ldr	r1, .L265+4
	bl	strcpy
	ldr	r3, [r5, #116]
	movs	r1, #64
	ldr	r2, .L265+8
	mov	r0, r4
	bl	snprintf
	add	r4, r4, r0
	ldr	r1, .L265+12
	mov	r0, r4
	adds	r4, r4, #10
	bl	strcpy
	ldr	r3, [r5, #2504]
	movs	r1, #64
	ldr	r2, .L265+16
	mov	r0, r4
	bl	snprintf
	add	r4, r4, r0
	ldr	r3, [r5, #2500]
	ldr	r2, .L265+20
	movs	r1, #64
	mov	r0, r4
	bl	snprintf
	add	r4, r4, r0
	ldr	r3, [r5, #2508]
	ldr	r2, .L265+24
	movs	r1, #64
	mov	r0, r4
	bl	snprintf
	add	r4, r4, r0
	ldr	r3, [r5, #2512]
	ldr	r2, .L265+28
	movs	r1, #64
	mov	r0, r4
	bl	snprintf
	add	r4, r4, r0
	ldr	r3, [r5, #2516]
	ldr	r2, .L265+32
	movs	r1, #64
	mov	r0, r4
	bl	snprintf
	add	r4, r4, r0
	ldr	r3, [r5, #2520]
	ldr	r2, .L265+36
	movs	r1, #64
	mov	r0, r4
	bl	snprintf
	add	r4, r4, r0
	ldr	r3, [r5, #2524]
	ldr	r2, .L265+40
	movs	r1, #64
	mov	r0, r4
	bl	snprintf
	add	r4, r4, r0
	ldr	r3, [r5, #2528]
	ldr	r2, .L265+44
	movs	r1, #64
	mov	r0, r4
	bl	snprintf
	ldr	r3, [r5, #2532]
	add	r4, r4, r0
	ldr	r2, .L265+48
	movs	r1, #64
	mov	r0, r4
	lsrs	r3, r3, #11
	bl	snprintf
	ldr	r3, [r5, #2536]
	add	r4, r4, r0
	ldr	r2, .L265+52
	movs	r1, #64
	mov	r0, r4
	lsrs	r3, r3, #11
	bl	snprintf
	add	r4, r4, r0
	ldr	r3, [r5, #2540]
	ldr	r2, .L265+56
	movs	r1, #64
	mov	r0, r4
	bl	snprintf
	add	r4, r4, r0
	ldr	r3, [r5, #2544]
	ldr	r2, .L265+60
	movs	r1, #64
	mov	r0, r4
	bl	snprintf
	add	r4, r4, r0
	ldrh	r3, [r5, #158]
	ldr	r2, .L265+64
	movs	r1, #64
	mov	r0, r4
	bl	snprintf
	add	r4, r4, r0
	ldrh	r3, [r5, #2344]
	ldr	r2, .L265+68
	movs	r1, #64
	mov	r0, r4
	bl	snprintf
	add	r4, r4, r0
	ldr	r3, [r5, #2548]
	ldr	r2, .L265+72
	movs	r1, #64
	mov	r0, r4
	bl	snprintf
	add	r4, r4, r0
	ldr	r3, [r5, #2552]
	ldr	r2, .L265+76
	movs	r1, #64
	mov	r0, r4
	bl	snprintf
	add	r4, r4, r0
	ldr	r3, [r5, #2556]
	ldr	r2, .L265+80
	movs	r1, #64
	mov	r0, r4
	bl	snprintf
	add	r4, r4, r0
	ldr	r3, [r5, #2560]
	ldr	r2, .L265+84
	movs	r1, #64
	mov	r0, r4
	bl	snprintf
	add	r4, r4, r0
	ldr	r3, [r5, #2564]
	ldr	r2, .L265+88
	movs	r1, #64
	mov	r0, r4
	bl	snprintf
	add	r4, r4, r0
	ldr	r3, [r5, #2568]
	ldr	r2, .L265+92
	movs	r1, #64
	mov	r0, r4
	bl	snprintf
	add	r4, r4, r0
	ldrh	r3, [r5, #2298]
	ldr	r2, .L265+96
	movs	r1, #64
	mov	r0, r4
	bl	snprintf
	add	r4, r4, r0
	ldrh	r3, [r5, #2296]
	ldr	r2, .L265+100
	movs	r1, #64
	mov	r0, r4
	bl	snprintf
	add	r4, r4, r0
	ldr	r3, [r5, #140]
	ldr	r2, .L265+104
	movs	r1, #64
	mov	r0, r4
	bl	snprintf
	add	r4, r4, r0
	ldr	r3, [r5, #132]
	ldr	r2, .L265+108
	movs	r1, #64
	mov	r0, r4
	bl	snprintf
	add	r4, r4, r0
	ldr	r3, [r5, #28]
	ldr	r2, .L265+112
	movs	r1, #64
	mov	r0, r4
	bl	snprintf
	add	r4, r4, r0
	ldrh	r3, [r5, #218]
	ldr	r2, .L265+116
	movs	r1, #64
	mov	r0, r4
	bl	snprintf
	add	r4, r4, r0
	ldrh	r3, [r5, #40]
	ldr	r2, .L265+120
	movs	r1, #64
	mov	r0, r4
	bl	snprintf
	add	r4, r4, r0
	ldrh	r3, [r5, #2572]
	ldr	r2, .L265+124
	movs	r1, #64
	mov	r0, r4
	bl	snprintf
	add	r4, r4, r0
	ldr	r3, [r5, #44]
	ldr	r2, .L265+128
	movs	r1, #64
	mov	r0, r4
	bl	snprintf
	add	r4, r4, r0
	ldrh	r3, [r5, #2576]
	ldr	r2, .L265+132
	movs	r1, #64
	mov	r0, r4
	bl	snprintf
	add	r4, r4, r0
	ldrh	r3, [r5, #152]
	ldr	r2, .L265+136
	movs	r1, #64
	mov	r0, r4
	bl	snprintf
	add	r4, r4, r0
	ldrh	r3, [r5, #2350]
	ldr	r2, .L265+140
	movs	r1, #64
	mov	r0, r4
	bl	snprintf
	add	r4, r4, r0
	ldrb	r3, [r5, #2354]	@ zero_extendqisi2
	ldr	r2, .L265+144
	movs	r1, #64
	mov	r0, r4
	bl	snprintf
	add	r4, r4, r0
	ldrh	r3, [r5, #2348]
	ldr	r2, .L265+148
	movs	r1, #64
	mov	r0, r4
	bl	snprintf
	add	r4, r4, r0
	ldrb	r3, [r5, #2356]	@ zero_extendqisi2
	ldr	r2, .L265+152
	movs	r1, #64
	mov	r0, r4
	bl	snprintf
	add	r4, r4, r0
	ldrh	r3, [r5, #2352]
	ldr	r2, .L265+156
	movs	r1, #64
	mov	r0, r4
	bl	snprintf
	ldrh	r2, [r5, #2348]
	add	r4, r4, r0
	ldr	r3, [r5, #2324]
	movs	r1, #64
	mov	r0, r4
	ldrh	r3, [r3, r2, lsl #1]
	ldr	r2, .L265+160
	bl	snprintf
	add	r4, r4, r0
	ldrh	r3, [r5, #2398]
	ldr	r2, .L265+164
	movs	r1, #64
	mov	r0, r4
	bl	snprintf
	add	r4, r4, r0
	ldrb	r3, [r5, #2402]	@ zero_extendqisi2
	ldr	r2, .L265+168
	movs	r1, #64
	mov	r0, r4
	bl	snprintf
	add	r4, r4, r0
	ldrh	r3, [r5, #2396]
	ldr	r2, .L265+172
	movs	r1, #64
	mov	r0, r4
	bl	snprintf
	add	r4, r4, r0
	ldrb	r3, [r5, #2404]	@ zero_extendqisi2
	ldr	r2, .L265+176
	movs	r1, #64
	mov	r0, r4
	bl	snprintf
	add	r4, r4, r0
	ldrh	r3, [r5, #2400]
	ldr	r2, .L265+180
	movs	r1, #64
	mov	r0, r4
	bl	snprintf
	ldrh	r2, [r5, #2396]
	add	r4, r4, r0
	ldr	r3, [r5, #2324]
	movs	r1, #64
	mov	r0, r4
	ldrh	r3, [r3, r2, lsl #1]
	ldr	r2, .L265+184
	bl	snprintf
	add	r4, r4, r0
	ldrh	r3, [r5, #2446]
	ldr	r2, .L265+188
	movs	r1, #64
	mov	r0, r4
	bl	snprintf
	add	r4, r4, r0
	ldrb	r3, [r5, #2450]	@ zero_extendqisi2
	ldr	r2, .L265+192
	movs	r1, #64
	mov	r0, r4
	bl	snprintf
	add	r4, r4, r0
	ldrh	r3, [r5, #2444]
	ldr	r2, .L265+196
	movs	r1, #64
	mov	r0, r4
	bl	snprintf
	add	r4, r4, r0
	ldrb	r3, [r5, #2452]	@ zero_extendqisi2
	ldr	r2, .L265+200
	movs	r1, #64
	mov	r0, r4
	bl	snprintf
	add	r4, r4, r0
	ldrh	r3, [r5, #2448]
	ldr	r2, .L265+204
	movs	r1, #64
	mov	r0, r4
	bl	snprintf
	add	r4, r4, r0
	ldrh	r3, [r5, #2590]
	ldr	r2, .L265+208
	movs	r1, #64
	mov	r0, r4
	bl	snprintf
	add	r4, r4, r0
	ldrb	r3, [r5, #2594]	@ zero_extendqisi2
	ldr	r2, .L265+212
	movs	r1, #64
	mov	r0, r4
	bl	snprintf
	add	r4, r4, r0
	ldrh	r3, [r5, #2588]
	ldr	r2, .L265+216
	movs	r1, #64
	mov	r0, r4
	bl	snprintf
	add	r4, r4, r0
	ldrb	r3, [r5, #2596]	@ zero_extendqisi2
	ldr	r2, .L265+220
	movs	r1, #64
	mov	r0, r4
	bl	snprintf
	add	r4, r4, r0
	ldrh	r3, [r5, #2592]
	b	.L266
.L267:
	.align	2
.L265:
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
.L266:
	ldr	r2, .L268
	movs	r1, #64
	mov	r0, r4
	bl	snprintf
	ldr	r3, [r5, #2712]
	add	r4, r4, r0
	ldr	r2, .L268+4
	movs	r1, #64
	mov	r0, r4
	str	r3, [sp, #4]
	ldr	r3, [r5, #2720]
	str	r3, [sp]
	ldr	r3, [r5, #2716]
	bl	snprintf
	add	r4, r4, r0
	ldr	r3, [r5, #2708]
	ldr	r2, .L268+8
	movs	r1, #64
	mov	r0, r4
	bl	snprintf
	add	r4, r4, r0
	ldr	r3, [r5, #2732]
	ldr	r2, .L268+12
	movs	r1, #64
	mov	r0, r4
	bl	snprintf
	add	r4, r4, r0
	ldrh	r3, [r5, #3148]
	ldr	r2, .L268+16
	movs	r1, #64
	mov	r0, r4
	bl	snprintf
	add	r4, r4, r0
	ldrh	r3, [r5, #3150]
	ldr	r2, .L268+20
	movs	r1, #64
	mov	r0, r4
	bl	snprintf
	add	r4, r4, r0
	ldr	r3, [r5, #3152]
	ldr	r2, .L268+24
	movs	r1, #64
	mov	r0, r4
	bl	snprintf
	add	r4, r4, r0
	ldrh	r3, [r5, #3156]
	ldr	r2, .L268+28
	movs	r1, #64
	mov	r0, r4
	bl	snprintf
	add	r4, r4, r0
	bl	GetFreeBlockMinEraseCount
	ldr	r2, .L268+32
	mov	r3, r0
	movs	r1, #64
	mov	r0, r4
	bl	snprintf
	add	r4, r4, r0
	ldrh	r0, [r5, #2344]
	bl	GetFreeBlockMaxEraseCount
	ldr	r2, .L268+36
	mov	r3, r0
	movs	r1, #64
	mov	r0, r4
	bl	snprintf
	ldr	r3, .L268+40
	add	r4, r4, r0
	ldr	r3, [r3]
	cmp	r3, #1
	beq	.L254
.L259:
	subs	r0, r4, r6
	add	sp, sp, #20
	@ sp needed
	pop	{r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L254:
	ldrh	r3, [r5, #2588]
	movw	r2, #65535
	cmp	r3, r2
	beq	.L256
	ldr	r2, [r5, #2324]
	mov	r0, r4
	movs	r1, #64
	ldrh	r3, [r2, r3, lsl #1]
	ldr	r2, .L268+44
	bl	snprintf
	add	r4, r4, r0
.L256:
	movs	r0, #0
	ldr	r9, .L268+52
	bl	List_get_gc_head_node
	uxth	r0, r0
	movs	r7, #0
	movw	fp, #65535
	mov	r10, #6
.L258:
	cmp	r0, fp
	beq	.L257
	ldr	r3, [r5, #2328]
	mul	r8, r10, r0
	mov	r2, r9
	movs	r1, #64
	ldrh	r3, [r3, r0, lsl #1]
	str	r3, [sp, #12]
	ldr	r3, [r5, #2316]
	add	r3, r3, r8
	ldrh	r3, [r3, #4]
	str	r3, [sp, #8]
	ldr	r3, [r5, #2324]
	ldrh	r3, [r3, r0, lsl #1]
	stm	sp, {r0, r3}
	mov	r3, r7
	mov	r0, r4
	adds	r7, r7, #1
	bl	snprintf
	ldr	r3, [r5, #2316]
	cmp	r7, #16
	add	r4, r4, r0
	ldrh	r0, [r3, r8]
	bne	.L258
.L257:
	ldr	r2, [r5, #2316]
	movs	r7, #0
	ldr	r3, [r5, #2340]
	movw	r9, #65535
	ldr	fp, .L268+56
	mov	r10, #6
	subs	r3, r3, r2
	asrs	r2, r3, #1
	ldr	r3, .L268+48
	muls	r3, r2, r3
	uxth	r3, r3
.L260:
	cmp	r3, r9
	beq	.L259
	ldr	r2, [r5, #2328]
	mul	r8, r10, r3
	mov	r0, r4
	movs	r1, #64
	ldrh	r2, [r2, r3, lsl #1]
	str	r2, [sp, #8]
	ldr	r2, [r5, #2316]
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
	beq	.L259
	ldr	r3, [r5, #2316]
	ldrh	r3, [r3, r8]
	b	.L260
.L269:
	.align	2
.L268:
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
	cmp	r0, #2048
	push	{r3, r4, r5, lr}
	.save {r3, r4, r5, lr}
	mov	r5, r1
	blt	.L272
	ldr	r3, .L273
	movs	r1, #64
	ldr	r2, .L273+4
	mov	r0, r5
	bl	snprintf
	adds	r4, r5, r0
	mov	r0, r4
	bl	FtlPrintInfo2buf
	add	r0, r0, r4
	subs	r0, r0, r5
	pop	{r3, r4, r5, pc}
.L272:
	movs	r0, #0
	pop	{r3, r4, r5, pc}
.L274:
	.align	2
.L273:
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
	ldr	r4, .L299
	ldr	r2, [r4, #2556]
	ldr	r3, [r4, #2568]
	cmp	r2, r3
	bcs	.L276
	ldrh	r1, [r4, #40]
	movs	r3, #0
	ldr	r0, [r4, #2328]
	str	r3, [r4, #2548]
.L277:
	cmp	r3, r1
	bcc	.L278
	ldr	r5, [r4, #2548]
	mov	r0, r5
	bl	__aeabi_uidiv
	str	r0, [r4, #2556]
	ldr	r0, [r4, #2552]
	ldrh	r1, [r4, #92]
	subs	r0, r5, r0
	bl	__aeabi_uidiv
	str	r0, [r4, #2548]
.L279:
	ldr	r6, [r4, #2568]
	ldr	r7, [r4, #2556]
	add	r3, r6, #256
	cmp	r3, r7
	bls	.L284
	ldr	r2, [r4, #2564]
	add	r3, r6, #768
	cmp	r3, r2
	bls	.L284
.L286:
	movw	r5, #65535
.L285:
	mov	r0, r5
	add	sp, sp, #28
	@ sp needed
	pop	{r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L278:
	ldrh	r2, [r0, r3, lsl #1]
	adds	r3, r3, #1
	ldr	r5, [r4, #2548]
	add	r2, r2, r5
	str	r2, [r4, #2548]
	b	.L277
.L276:
	ldr	r3, [r4, #2564]
	cmp	r2, r3
	bls	.L279
	adds	r3, r3, #1
	str	r3, [r4, #2564]
	movs	r3, #0
.L281:
	ldrh	r2, [r4, #40]
	cmp	r3, r2
	bcs	.L279
	ldr	r1, [r4, #2328]
	ldrh	r2, [r1, r3, lsl #1]
	adds	r2, r2, #1
	strh	r2, [r1, r3, lsl #1]	@ movhi
	adds	r3, r3, #1
	b	.L281
.L284:
	ldrh	r0, [r4, #2344]
	add	r0, r0, r0, lsl #1
	ubfx	r0, r0, #2, #16
	bl	GetFreeBlockMaxEraseCount
	add	r3, r6, #64
	mov	r9, r0
	cmp	r0, r3
	bcc	.L286
	ldr	r3, [r4, #2320]
	cmp	r3, #0
	beq	.L286
	ldrh	r2, [r4, #40]
	movw	r1, #65535
	ldr	lr, [r4, #2316]
	movs	r0, #0
	ldr	r8, [r4, #2328]
	mov	r10, r1
	str	r2, [sp, #20]
	mov	fp, #6
	mov	r2, r1
.L287:
	ldrh	ip, [r3]
	cmp	ip, r10
	bne	.L290
	mov	r5, r2
.L289:
	movw	r3, #65535
	cmp	r5, r3
	beq	.L286
	ldrh	r10, [r8, r5, lsl #1]
	lsl	fp, r5, #1
	cmp	r6, r10
	bcs	.L291
	bl	GetFreeBlockMinEraseCount
	cmp	r6, r0
	it	cc
	strcc	r1, [r4, #2568]
.L291:
	cmp	r7, r10
	bls	.L286
	add	r3, r10, #128
	cmp	r9, r3
	ble	.L286
	add	r3, r10, #256
	cmp	r7, r3
	bhi	.L292
	ldr	r3, [r4, #2564]
	add	r10, r10, #768
	cmp	r10, r3
	bcs	.L286
.L292:
	str	r9, [sp, #8]
	mov	r2, r7
	ldrh	r3, [r8, fp]
	mov	r1, r5
	ldr	r0, .L299+4
	str	r3, [sp, #4]
	ldr	r3, [r4, #2324]
	ldrh	r3, [r3, fp]
	str	r3, [sp]
	ldr	r3, [r4, #2564]
	bl	sftl_printk
	movs	r3, #1
	str	r3, [r4, #3160]
	b	.L285
.L290:
	adds	r0, r0, #1
	ldr	r5, [sp, #20]
	uxth	r0, r0
	cmp	r0, r5
	bhi	.L286
	ldrh	r5, [r3, #4]
	cbz	r5, .L288
	ldr	r5, .L299+8
	sub	r3, r3, lr
	asrs	r3, r3, #1
	muls	r3, r5, r3
	uxth	r5, r3
	ldrh	r3, [r8, r5, lsl #1]
	cmp	r6, r3
	bcs	.L289
	cmp	r1, r3
	itt	hi
	movhi	r1, r3
	movhi	r2, r5
.L288:
	mla	r3, fp, ip, lr
	b	.L287
.L300:
	.align	2
.L299:
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
	beq	.L302
	ldr	r2, .L303
	movs	r1, #0
	ldr	r2, [r2, #2324]
	strh	r1, [r2, r0, lsl #1]	@ movhi
	bl	INSERT_FREE_LIST
.L302:
	movs	r0, #0
	pop	{r3, pc}
.L304:
	.align	2
.L303:
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
	bne	.L306
	movw	r2, #2776
	ldr	r1, .L319
	ldr	r0, .L319+4
	bl	sftl_printk
.L306:
	ldr	r5, .L319+8
	ldrh	r2, [r4, #2]
	ldrh	r3, [r5, #102]
	cmp	r2, r3
	bne	.L307
	movw	r2, #2777
	ldr	r1, .L319
	ldr	r0, .L319+4
	bl	sftl_printk
.L307:
	ldrh	r3, [r4, #4]
	cbnz	r3, .L308
	movw	r2, #2778
	ldr	r1, .L319
	ldr	r0, .L319+4
	bl	sftl_printk
.L308:
	ldrb	r2, [r4, #6]	@ zero_extendqisi2
	movs	r3, #0
	ldrh	r1, [r5, #32]
	movw	r6, #65535
	strb	r3, [r4, #10]
	adds	r2, r2, #8
	ldrh	r0, [r4, r2, lsl #1]
	mov	r2, r3
.L309:
	cmp	r0, r6
	ldrb	r3, [r4, #6]	@ zero_extendqisi2
	beq	.L311
	ldrh	r2, [r4, #4]
	ldrh	r6, [r4, #2]
	subs	r2, r2, #1
	uxth	r2, r2
	orr	r6, r6, r0, lsl #10
	movw	r0, #65535
	strh	r2, [r4, #4]	@ movhi
.L313:
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
	beq	.L313
	strb	r3, [r4, #6]
	ldrh	r1, [r4, #2]
	ldrh	r3, [r5, #102]
	cmp	r1, r3
	bne	.L305
	cbz	r2, .L305
	movw	r2, #2799
	ldr	r1, .L319
	ldr	r0, .L319+4
	bl	sftl_printk
.L305:
	mov	r0, r6
	pop	{r3, r4, r5, r6, r7, pc}
.L311:
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
	b	.L309
.L320:
	.align	2
.L319:
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
	ldr	r3, .L330
	mov	ip, #20
	push	{r4, r5, r6, r7, r8, r9, lr}
	.save {r4, r5, r6, r7, r8, r9, lr}
	mov	r9, #12
	movs	r6, #1
	movs	r2, #0
	mov	r7, r3
	str	r2, [r3, #3164]
.L322:
	ldrh	r1, [r3, #32]
	uxth	r0, r2
	adds	r4, r2, #1
	cmp	r0, r1
	bcc	.L325
	ldr	r7, .L330
	mov	ip, #12
	movs	r6, #0
.L326:
	ldr	r2, [r3, #3184]
	cmp	r1, r2
	bcc	.L329
	pop	{r4, r5, r6, r7, r8, r9, pc}
.L325:
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
	b	.L322
.L329:
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
	b	.L326
.L331:
	.align	2
.L330:
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
	ldr	r3, .L339
	push	{r4, r5, r6, r7, r8, r9, r10, lr}
	.save {r4, r5, r6, r7, r8, r9, r10, lr}
	movs	r5, #0
	mov	r10, #20
	mov	lr, #12
	mov	r8, r5
	ldr	ip, [r3, #3184]
	ldr	r6, [r3, #3168]
.L333:
	uxth	r3, r5
	cmp	r1, r3
	bls	.L332
	mla	r4, r10, r3, r0
	movs	r2, #0
.L334:
	uxth	r3, r2
	cmp	ip, r3
	bls	.L335
	mul	r3, lr, r3
	ldr	r7, [r4, #8]
	adds	r2, r2, #1
	add	r9, r6, r3
	ldr	r3, [r6, r3]
	cmp	r3, r7
	bne	.L334
	str	r8, [r9, #8]
.L335:
	adds	r5, r5, #1
	b	.L333
.L332:
	pop	{r4, r5, r6, r7, r8, r9, r10, pc}
.L340:
	.align	2
.L339:
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
	ldr	r3, .L348
	mov	ip, #12
	push	{r4, r5, r6, r7, r8, r9, lr}
	.save {r4, r5, r6, r7, r8, r9, lr}
	movs	r4, #0
	mov	lr, #1
	mov	r8, #20
	ldr	r5, [r3, #3184]
	ldr	r6, [r3, #3168]
.L342:
	uxth	r2, r4
	cmp	r1, r2
	bhi	.L346
	pop	{r4, r5, r6, r7, r8, r9, pc}
.L346:
	mov	r9, #0
.L343:
	uxth	r3, r9
	cmp	r5, r3
	bls	.L344
	mla	r3, ip, r3, r6
	add	r9, r9, #1
	ldr	r7, [r3, #8]
	cmp	r7, #0
	bne	.L343
	mla	r2, r8, r2, r0
	ldr	r7, [r3]
	str	lr, [r3, #8]
	str	r7, [r2, #8]
	ldr	r3, [r3, #4]
	str	r3, [r2, #12]
.L344:
	adds	r4, r4, #1
	b	.L342
.L349:
	.align	2
.L348:
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
	ldr	r2, .L355
	ldr	r3, [r2, #3188]
	ldrh	r2, [r2, #3192]
	add	r2, r3, r2, lsl #1
.L351:
	cmp	r3, r2
	bne	.L353
	movs	r0, #0
	bx	lr
.L353:
	ldrh	r1, [r3], #2
	cmp	r1, r0
	bne	.L351
	movs	r0, #1
	bx	lr
.L356:
	.align	2
.L355:
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
	ldr	r3, .L361
	ldrh	r1, [r3, #3192]
	ldr	r2, [r3, #3188]
.L358:
	uxth	ip, r4
	cmp	ip, r1
	bcc	.L360
	bne	.L359
	strh	r0, [r2, ip, lsl #1]	@ movhi
	ldrh	r0, [r3, #3192]
	adds	r0, r0, #1
	strh	r0, [r3, #3192]	@ movhi
	b	.L359
.L360:
	adds	r4, r4, #1
	add	ip, r2, r4, lsl #1
	ldrh	ip, [ip, #-2]
	cmp	ip, r0
	bne	.L358
.L359:
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
.L362:
	.align	2
.L361:
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
	push	{r4, r5, r6, lr}
	.save {r4, r5, r6, lr}
	mov	r1, r0
	mov	r4, r0
	ldr	r0, .L368
	bl	sftl_printk
	ldr	r3, .L368+4
	ldrh	r6, [r3, #3202]
	cmp	r4, r6
	beq	.L364
	ldrh	r5, [r3, #3204]
	cmp	r4, r5
	beq	.L364
	ldrh	r0, [r3, #3206]
	cmp	r4, r0
	beq	.L364
	ldrh	r1, [r3, #3208]
	cmp	r4, r1
	beq	.L364
	movw	r2, #65535
	cmp	r6, r2
	bne	.L365
	strh	r4, [r3, #3202]	@ movhi
.L364:
	movs	r0, #0
	pop	{r4, r5, r6, pc}
.L365:
	cmp	r5, r2
	bne	.L366
	strh	r4, [r3, #3204]	@ movhi
	b	.L364
.L366:
	cmp	r0, r2
	bne	.L367
	strh	r4, [r3, #3206]	@ movhi
	b	.L364
.L367:
	cmp	r1, r2
	it	eq
	strheq	r4, [r3, #3208]	@ movhi
	b	.L364
.L369:
	.align	2
.L368:
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
	ldr	r4, .L374
	bl	P2V_block_in_plane
	mov	r2, r5
	mov	r6, r0
	ldrh	r1, [r4, #3210]
	ldr	r0, .L374+4
	bl	sftl_printk
	mov	r0, r6
	bl	FtlGcRefreshBlock
	ldrh	r3, [r4, #3210]
	movs	r2, #0
	addw	r0, r4, #3212
.L371:
	uxth	r1, r2
	cmp	r3, r1
	bhi	.L373
	cmp	r3, #15
	itttt	ls
	addls	r2, r3, #1
	strhls	r2, [r4, #3210]	@ movhi
	addls	r4, r4, r3, lsl #1
	strhls	r5, [r4, #3212]	@ movhi
	b	.L372
.L373:
	adds	r2, r2, #1
	add	r1, r0, r2, lsl #1
	ldrh	r1, [r1, #-2]
	cmp	r1, r5
	bne	.L371
.L372:
	movs	r0, #0
	pop	{r4, r5, r6, pc}
.L375:
	.align	2
.L374:
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
	ldr	r4, .L382
	ldrh	r3, [r4, #3210]
	cbz	r3, .L377
	ldrh	r1, [r4, #3202]
	movw	r2, #65535
	cmp	r1, r2
	bne	.L377
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
.L377:
	movs	r0, #0
	pop	{r4, pc}
.L383:
	.align	2
.L382:
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
	ldr	r1, .L385
	b	__kmalloc
.L386:
	.align	2
.L385:
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
	ldr	fp, .L397+16
	mov	r10, r0
	mov	r7, r1
	mov	r8, r2
	mov	r9, r3
	mov	r5, r6
	mov	r4, r6
.L389:
	cmp	r4, r9
	bcc	.L395
	ldr	r0, .L397
	pop	{r3, r4, r5, r6, r7, r8, r9, r10, fp, lr}
	b	sftl_printk
.L395:
	cbnz	r5, .L390
	mov	r3, r6
	mov	r2, r7
	mov	r1, r10
	ldr	r0, .L397+4
	bl	sftl_printk
.L390:
	cmp	r8, #4
	bne	.L391
	ldr	r1, [r7, r4, lsl #2]
	ldr	r0, .L397+8
.L396:
	adds	r5, r5, #1
	bl	sftl_printk
	cmp	r5, #15
	bls	.L394
	movs	r5, #0
	ldr	r0, .L397
	bl	sftl_printk
.L394:
	adds	r4, r4, #1
	add	r6, r6, r8
	b	.L389
.L391:
	cmp	r8, #2
	ittee	eq
	ldrheq	r1, [r7, r4, lsl #1]
	moveq	r0, fp
	ldrbne	r1, [r7, r4]	@ zero_extendqisi2
	ldrne	r0, .L397+12
	b	.L396
.L398:
	.align	2
.L397:
	.word	.LC79
	.word	.LC75
	.word	.LC76
	.word	.LC78
	.word	.LC77
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
	ldr	r5, .L417
	mov	r4, r0
	movs	r6, #0
	ldr	r10, .L417+4
	ldrh	r9, [r5, #12]
	ldr	fp, .L417+8
.L400:
	cmp	r6, r8
	bne	.L407
	movs	r0, #0
	add	sp, sp, #12
	@ sp needed
	pop	{r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L407:
	ldr	r3, [r4, #8]
	cbz	r3, .L401
	ldr	r3, [r4, #12]
	cbnz	r3, .L402
.L401:
	movs	r2, #101
	mov	r1, r10
	mov	r0, fp
	bl	sftl_printk
.L402:
	mov	r2, sp
	add	r1, sp, #4
	mov	r0, r4
	bl	l2p_addr_tran
	ldr	r3, [r4, #12]
	ldr	r2, [r4, #8]
	ldr	r1, [sp, #4]
	ldrb	r0, [sp]	@ zero_extendqisi2
	ldr	r7, [r5, #3260]
	blx	r7
	str	r0, [r4]
	ldrh	r3, [r5, #14]
	cmp	r3, #4
	bne	.L404
	ldr	r3, [r4, #12]
	ldr	r2, [r4, #8]
	ldr	r1, [sp, #4]
	adds	r3, r3, #8
	ldrb	r0, [sp]	@ zero_extendqisi2
	add	r2, r2, #2048
	ldr	r7, [r5, #3260]
	add	r1, r1, r9
	blx	r7
	adds	r0, r0, #1
	beq	.L405
	ldr	r3, [r4, #12]
	ldr	r2, [r3, #12]
	adds	r2, r2, #1
	bne	.L404
	ldr	r2, [r3, #8]
	adds	r2, r2, #1
	bne	.L404
	ldr	r3, [r3]
	adds	r3, r3, #1
	beq	.L404
.L405:
	mov	r3, #-1
	str	r3, [r4]
.L404:
	adds	r6, r6, #1
	adds	r4, r4, #20
	b	.L400
.L418:
	.align	2
.L417:
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
	ldr	r4, .L429
	movw	r10, #61664
	ldr	r3, [r4, #3284]
	add	r7, r4, #162
	ldr	r8, [r4, #3288]
	add	r9, r4, #3264
	str	r3, [r4, #3272]
	str	r8, [r4, #3276]
.L420:
	ldrh	r3, [r4, #54]
	cmp	r6, r3
	bcc	.L425
	movs	r0, #0
	pop	{r4, r5, r6, r7, r8, r9, r10, pc}
.L425:
	ldrh	r5, [r4, #98]
	movw	r3, #65535
	strh	r3, [r7, #2]!	@ movhi
.L422:
	ldrh	r3, [r4, #98]
	subs	r5, r5, #1
	uxth	r5, r5
	sub	r2, r3, #16
	cmp	r5, r2
	ble	.L423
	mla	r3, r6, r3, r5
	movs	r2, #1
	mov	r1, r2
	mov	r0, r9
	lsls	r3, r3, #10
	str	r3, [r4, #3268]
	bl	FlashReadPages
	ldr	r3, [r4, #3264]
	adds	r3, r3, #1
	beq	.L422
	ldrh	r3, [r8]
	cmp	r3, r10
	bne	.L422
	strh	r5, [r7]	@ movhi
.L423:
	adds	r6, r6, #1
	b	.L420
.L430:
	.align	2
.L429:
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
	ldr	r3, .L442
	cmp	r1, #1
	push	{r4, r5, r6, r7, r8, lr}
	.save {r4, r5, r6, r7, r8, lr}
	lsl	r6, r0, #10
	.pad #88
	sub	sp, sp, #88
	mov	r2, r1
	mov	r7, r1
	it	eq
	ldrheq	r5, [r3, #104]
	mov	r1, #1
	it	ne
	ldrhne	r5, [r3, #102]
	add	r0, sp, #4
	ldr	r3, .L442+4
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
	bne	.L434
	mov	r8, #0
.L435:
	cmp	r8, r5
	ble	.L438
.L434:
	mov	r0, r5
	add	sp, sp, #88
	@ sp needed
	pop	{r4, r5, r6, r7, r8, pc}
.L438:
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
	bne	.L436
	ldr	r3, [sp, #28]
	adds	r3, r3, #1
	bne	.L436
	ldr	r3, [sp, #4]
	adds	r3, r3, #1
	beq	.L436
	subs	r4, r4, #1
	sxth	r5, r4
	b	.L435
.L436:
	adds	r4, r4, #1
	sxth	r8, r4
	b	.L435
.L443:
	.align	2
.L442:
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
	ldr	r6, .L474
	mov	r9, r2
	mov	r4, r0
	mov	r5, r0
	mov	r8, #0
	.pad #44
	sub	sp, sp, #44
	ldrh	r10, [r6, #12]
	str	r3, [sp, #4]
.L445:
	cmp	r8, fp
	bne	.L453
	ldr	r3, [sp, #4]
	cmp	r3, #0
	bne	.L460
.L473:
	movs	r0, #0
	add	sp, sp, #44
	@ sp needed
	pop	{r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L453:
	ldr	r3, [r5, #8]
	cbz	r3, .L446
	ldr	r3, [r5, #12]
	cbnz	r3, .L447
.L446:
	movs	r2, #131
	ldr	r1, .L474+4
	ldr	r0, .L474+8
	bl	sftl_printk
.L447:
	add	r2, sp, #12
	add	r1, sp, #16
	mov	r0, r5
	bl	l2p_addr_tran
	ldr	r3, [r5, #12]
	ldr	r2, [r5, #8]
	ldr	r1, [sp, #16]
	ldrb	r0, [sp, #12]	@ zero_extendqisi2
	ldr	r7, [r6, #3256]
	blx	r7
	cbnz	r0, .L448
	str	r0, [r5]
.L449:
	ldrh	r3, [r6, #14]
	cmp	r3, #4
	bne	.L451
	ldr	r3, [r5, #12]
	ldr	r2, [r5, #8]
	ldr	r1, [sp, #16]
	adds	r3, r3, #8
	ldrb	r0, [sp, #12]	@ zero_extendqisi2
	add	r2, r2, #2048
	ldr	r7, [r6, #3256]
	add	r1, r1, r10
	blx	r7
	cbz	r0, .L451
	mov	r3, #-1
	str	r3, [r5]
.L451:
	add	r8, r8, #1
	adds	r5, r5, #20
	b	.L445
.L448:
	mov	r3, #-1
	str	r3, [r5]
	b	.L449
.L458:
	movs	r3, #0
	mov	r2, r9
	str	r3, [r7]
	movs	r1, #1
	str	r3, [r6, #3292]
	add	r0, sp, #20
	ldr	r3, [r4, #4]
	str	r7, [sp, #28]
	str	r10, [sp, #32]
	str	r3, [sp, #24]
	bl	FlashReadPages
	ldr	r3, [sp, #20]
	adds	r2, r3, #1
	bne	.L455
	ldr	r1, [r4, #4]
	mov	r0, fp
	str	r3, [sp, #4]
	bl	sftl_printk
	ldr	r3, [sp, #4]
	str	r3, [r4]
.L455:
	ldr	r3, [r4, #12]
	cbz	r3, .L456
	ldr	r2, [r3]
	ldr	r3, [r6, #3292]
	cmp	r2, r3
	beq	.L456
	ldr	r1, [r4, #4]
	ldr	r0, .L474+12
	bl	sftl_printk
	mov	r3, #-1
	str	r3, [r4]
.L456:
	ldr	r3, [r4, #8]
	cbz	r3, .L457
	ldr	r2, [r3]
	ldr	r3, [r7]
	cmp	r2, r3
	beq	.L457
	ldr	r1, [r4, #4]
	ldr	r0, .L474+16
	bl	sftl_printk
	mov	r3, #-1
	str	r3, [r4]
.L457:
	adds	r5, r5, #1
	adds	r4, r4, #20
.L454:
	cmp	r8, r5
	bne	.L458
	b	.L473
.L460:
	movs	r5, #0
	ldr	r7, .L474+20
	ldr	r10, .L474+24
	ldr	fp, .L474+28
	b	.L454
.L475:
	.align	2
.L474:
	.word	.LANCHOR0
	.word	.LANCHOR1+145
	.word	.LC1
	.word	.LC81
	.word	.LC82
	.word	check_buf
	.word	.LANCHOR0+3292
	.word	.LC80
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
	ldr	r4, .L487
	mov	r5, r0
	movs	r6, #0
	mov	r9, #-1
	ldrh	r8, [r4, #12]
.L477:
	cmp	r6, r7
	bne	.L483
	movs	r0, #0
	add	sp, sp, #12
	@ sp needed
	pop	{r4, r5, r6, r7, r8, r9, pc}
.L483:
	mov	r2, sp
	add	r1, sp, #4
	mov	r0, r5
	bl	l2p_addr_tran
	ldr	r3, [r4, #3252]
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
	bne	.L481
	ldr	r1, [sp, #4]
	ldr	r3, [r4, #3252]
	ldrb	r0, [sp]	@ zero_extendqisi2
	add	r1, r1, r8
	blx	r3
	cbz	r0, .L481
	str	r9, [r5]
.L481:
	adds	r6, r6, #1
	adds	r5, r5, #20
	b	.L477
.L488:
	.align	2
.L487:
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
	bhi	.L489
	ldr	r4, .L498
	ldrh	r3, [r4, #218]
	cmp	r3, #1024
	beq	.L489
	cbz	r1, .L491
	bl	P2V_block_in_plane
	mov	r6, r0
	ldr	r0, [r4, #3804]
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
.L491:
	ldrh	r3, [r4, #218]
	adds	r3, r3, #1
	strh	r3, [r4, #218]	@ movhi
	ldrh	r3, [r4, #216]
	add	r2, r4, r3, lsl #1
	adds	r3, r3, #1
	ubfx	r3, r3, #0, #10
	strh	r5, [r2, #220]	@ movhi
	strh	r3, [r4, #216]	@ movhi
.L489:
	pop	{r4, r5, r6, pc}
.L499:
	.align	2
.L498:
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
	ldr	r4, .L545
	mov	r7, r1
	mov	r6, r10
	mov	r5, r10
	mov	r9, #20
	.pad #20
	sub	sp, sp, #20
	mov	fp, r4
	str	r0, [sp]
	str	r0, [r4, #3808]
.L501:
	ldrh	r1, [r4, #32]
	uxth	r3, r10
	cmp	r1, r3
	bhi	.L506
	cmp	r6, #0
	beq	.L500
	mov	r8, #0
	mov	r9, #20
	mov	r2, r6
	movs	r1, #0
	ldr	r0, [r4, #3804]
	bl	FlashEraseBlocks
.L509:
	uxth	r3, r8
	cmp	r6, r3
	bhi	.L511
	cmp	r7, #0
	beq	.L528
	ldrh	r3, [r4, #104]
	mov	r10, #1
	str	r3, [sp, #4]
	lsrs	r3, r3, #2
	str	r3, [sp, #8]
.L512:
	mov	r8, #0
	mov	fp, #20
.L522:
	mov	r9, #0
	mov	r6, r9
.L513:
	ldrh	r1, [r4, #32]
	uxth	r3, r9
	cmp	r1, r3
	bhi	.L517
	cbz	r6, .L500
	mov	r9, #0
	movs	r3, #1
	mov	r2, r10
	mov	r1, r6
	ldr	r0, [r4, #3804]
	bl	FlashProgPages
.L519:
	uxth	r3, r9
	cmp	r6, r3
	bhi	.L521
	ldr	r3, [sp, #8]
	add	r8, r8, r3
	ldr	r3, [sp, #4]
	uxth	r8, r8
	cmp	r3, r8
	bhi	.L522
	mov	r8, #0
	mov	r9, #20
.L523:
	uxth	r3, r8
	cmp	r6, r3
	bhi	.L525
	ldr	r3, [sp]
	cmp	r3, #63
	bls	.L526
	cbz	r7, .L500
.L526:
	mov	r2, r6
	mov	r1, r10
	ldr	r0, [r4, #3804]
	bl	FlashEraseBlocks
.L500:
	mov	r0, r5
	add	sp, sp, #20
	@ sp needed
	pop	{r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L506:
	uxth	r3, r10
	ldr	r0, [r4, #3804]
	movs	r2, #0
	mul	r1, r9, r3
	add	r3, r3, r4
	str	r2, [r0, r1]
	ldr	r1, [sp]
	ldrb	r0, [r3, #60]	@ zero_extendqisi2
	bl	V2P_block
	mov	r8, r0
	cbz	r7, .L502
	bl	IsBlkInVendorPart
	cbnz	r0, .L503
.L502:
	mov	r0, r8
	bl	FtlBbmIsBadBlock
	cbnz	r0, .L504
	ldr	r1, [r4, #3804]
	lsl	r3, r8, #10
	ldr	r0, [fp, #3816]
	mla	r1, r9, r6, r1
	str	r3, [r1, #4]
	ldr	r3, [r4, #3812]
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
.L503:
	add	r10, r10, #1
	b	.L501
.L504:
	adds	r5, r5, #1
	uxth	r5, r5
	b	.L503
.L511:
	mul	r3, r9, r8
	ldr	r2, [r4, #3804]
	adds	r1, r2, r3
	ldr	r3, [r2, r3]
	adds	r3, r3, #1
	bne	.L510
	ldr	r0, [r1, #4]
	adds	r5, r5, #1
	uxth	r5, r5
	ubfx	r0, r0, #10, #16
	bl	FtlBbmMapBadBlock
.L510:
	add	r8, r8, #1
	b	.L509
.L528:
	movs	r3, #6
	mov	r10, r7
	str	r3, [sp, #8]
	movs	r3, #1
	str	r3, [sp, #4]
	b	.L512
.L517:
	uxth	r3, r9
	ldr	r0, [r4, #3804]
	movs	r2, #0
	mul	r1, fp, r3
	add	r3, r3, r4
	str	r2, [r0, r1]
	ldr	r1, [sp]
	ldrb	r0, [r3, #60]	@ zero_extendqisi2
	bl	V2P_block
	str	r0, [sp, #12]
	cbz	r7, .L514
	bl	IsBlkInVendorPart
	cbnz	r0, .L515
.L514:
	ldr	r0, [sp, #12]
	bl	FtlBbmIsBadBlock
	cbnz	r0, .L515
	ldr	r1, [r4, #3804]
	ldr	r3, [sp, #12]
	ldr	r2, .L545
	mla	r1, fp, r6, r1
	ldr	r0, [r2, #3812]
	add	r3, r8, r3, lsl #10
	str	r3, [r1, #4]
	ldr	r3, [r4, #3820]
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
.L515:
	add	r9, r9, #1
	b	.L513
.L521:
	mul	r3, fp, r9
	ldr	r2, [r4, #3804]
	adds	r1, r2, r3
	ldr	r3, [r2, r3]
	cbz	r3, .L520
	ldr	r0, [r1, #4]
	adds	r5, r5, #1
	uxth	r5, r5
	ubfx	r0, r0, #10, #16
	bl	FtlBbmMapBadBlock
.L520:
	add	r9, r9, #1
	b	.L519
.L525:
	cbz	r7, .L524
	mul	r3, r9, r8
	ldr	r2, [r4, #3804]
	adds	r1, r2, r3
	ldr	r3, [r2, r3]
	cbnz	r3, .L524
	ldr	r0, [r1, #4]
	movs	r1, #1
	ubfx	r0, r0, #10, #16
	bl	FtlFreeSysBlkQueueIn
.L524:
	add	r8, r8, #1
	b	.L523
.L546:
	.align	2
.L545:
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
	ldr	r4, .L554
	ldr	r6, .L554+4
.L548:
	ldrh	r1, [r4, #218]
	cbz	r1, .L549
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
	ldr	r0, [r4, #3804]
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
	bhi	.L550
	mov	r0, r5
	pop	{r3, r4, r5, r6, r7, pc}
.L549:
	ldr	r0, .L554+8
	bl	sftl_printk
.L552:
	b	.L552
.L550:
	ldrh	r2, [r4, #218]
	mov	r1, r5
	mov	r0, r6
	bl	sftl_printk
	b	.L548
.L555:
	.align	2
.L554:
	.word	.LANCHOR0
	.word	.LC84
	.word	.LC83
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
.L557:
	uxth	r5, r3
	cmp	r5, r1
	bcs	.L560
	mov	r7, r2
	adds	r3, r3, #1
	ldrh	r6, [r7]
	adds	r2, r2, #2
	cmp	r6, #0
	bne	.L557
	bl	FtlFreeSysBlkQueueOut
	subs	r3, r0, #1
	movw	r2, #65533
	uxth	r3, r3
	mov	r1, r0
	strh	r0, [r7]	@ movhi
	cmp	r3, r2
	bls	.L558
	ldr	r3, .L564
	ldr	r0, .L564+4
	ldrh	r2, [r3, #218]
	bl	sftl_printk
.L559:
	b	.L559
.L558:
	ldr	r3, [r4, #28]
	strh	r6, [r4, #2]	@ movhi
	strh	r5, [r4]	@ movhi
	adds	r3, r3, #1
	str	r3, [r4, #28]
	ldrh	r3, [r4, #8]
	adds	r3, r3, #1
	strh	r3, [r4, #8]	@ movhi
.L560:
	ldrh	r3, [r4, #10]
	cmp	r3, r5
	bhi	.L562
	movw	r2, #603
	ldr	r1, .L564+8
	ldr	r0, .L564+12
	bl	sftl_printk
.L562:
	movs	r0, #0
	pop	{r3, r4, r5, r6, r7, pc}
.L565:
	.align	2
.L564:
	.word	.LANCHOR0
	.word	.LC85
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
	bls	.L568
	ldr	r5, .L569
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
.L566:
	mov	r0, r4
	add	sp, sp, #92
	@ sp needed
	pop	{r4, r5, pc}
.L568:
	movs	r4, #0
	b	.L566
.L570:
	.align	2
.L569:
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
	ldr	r5, .L578
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
	bls	.L572
	mov	r2, #256
	movs	r1, #255
	mov	r0, r4
	bl	memset
	movs	r0, #0
.L572:
	ldrh	r3, [r5, #14]
	cmp	r3, #4
	bne	.L577
	add	r1, r4, r0, lsl #1
	mov	r3, r4
.L574:
	cmp	r3, r1
	bne	.L575
.L577:
	pop	{r4, r5, r6, pc}
.L575:
	ldrh	r2, [r3]
	lsrs	r2, r2, #1
	strh	r2, [r3], #2	@ movhi
	b	.L574
.L579:
	.align	2
.L578:
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
	cbz	r2, .L581
	bl	memset
.L581:
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
	ldr	r4, .L686
	movs	r5, #0
	movs	r6, #12
	movs	r7, #20
	ldrh	r0, [r4, #106]
	str	r3, [r4, #3828]
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
	str	r5, [r4, #3824]
	str	r5, [r4, #3160]
	str	r5, [r4, #3832]
	str	r5, [r4, #3152]
	str	r5, [r4, #3808]
	strh	r5, [r4, #3156]	@ movhi
	strh	r5, [r4, #3210]	@ movhi
	strh	r5, [r4, #3246]	@ movhi
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
	lsls	r3, r3, #1
	mov	r0, r7
	adds	r3, r3, #1
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
	muls	r0, r7, r0
	bl	ftl_malloc
	str	r0, [r4, #3172]
	mov	r0, r7
	bl	ftl_malloc
	str	r0, [r4, #3820]
	mov	r0, r7
	bl	ftl_malloc
	str	r0, [r4, #3812]
	ldr	r0, [r4, #3184]
	muls	r0, r6, r0
	bl	ftl_malloc
	ldrh	r3, [r4, #112]
	ldrh	r7, [r4, #32]
	str	r0, [r4, #3168]
	muls	r7, r3, r7
	mov	r0, r7
	bl	ftl_malloc
	str	r0, [r4, #3288]
	lsls	r0, r7, #2
	bl	ftl_malloc
	ldrh	r3, [r4, #112]
	str	r0, [r4, #3816]
	ldr	r0, [r4, #3184]
	muls	r0, r3, r0
	bl	ftl_malloc
	str	r0, [r4, #3176]
	ldrh	r0, [r4, #42]
	lsls	r0, r0, #1
	uxth	r0, r0
	strh	r0, [r4, #3856]	@ movhi
	bl	ftl_malloc
	ldrh	r3, [r4, #3856]
	str	r0, [r4, #3860]
	ldr	r0, .L686+4
	addw	r3, r3, #547
	lsrs	r3, r3, #9
	and	r0, r0, r3, lsl #9
	strh	r3, [r4, #3856]	@ movhi
	bl	ftl_malloc
	ldrh	r7, [r4, #42]
	str	r0, [r4, #3864]
	adds	r0, r0, #32
	str	r0, [r4, #2328]
	lsls	r7, r7, #1
	mov	r0, r7
	bl	ftl_malloc
	str	r0, [r4, #3868]
	mov	r0, r7
	bl	ftl_malloc
	ldr	r7, [r4, #128]
	str	r0, [r4, #2324]
	lsls	r7, r7, #1
	mov	r0, r7
	bl	ftl_malloc
	str	r0, [r4, #3872]
	mov	r0, r7
	bl	ftl_malloc
	str	r0, [r4, #3876]
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
	str	r0, [r4, #3880]
	ldrh	r0, [r4, #120]
	lsls	r0, r0, #2
	bl	ftl_malloc
	str	r0, [r4, #3884]
	ldrh	r0, [r4, #122]
	lsls	r0, r0, #2
	bl	ftl_malloc
	ldrh	r2, [r4, #122]
	mov	r1, r5
	str	r0, [r4, #3888]
	add	r5, r4, #180
	lsls	r2, r2, #2
	bl	ftl_memset
	ldrh	r0, [r4, #136]
	lsls	r0, r0, #2
	bl	ftl_malloc
	str	r0, [r4, #3892]
	ldr	r0, [r4, #128]
	lsls	r0, r0, #2
	bl	ftl_malloc
	str	r0, [r4, #3896]
	ldrh	r0, [r4, #138]
	muls	r0, r6, r0
	bl	ftl_malloc
	ldrh	r3, [r4, #138]
	str	r0, [r4, #2492]
	ldrh	r0, [r4, #110]
	muls	r0, r3, r0
	bl	ftl_malloc
	ldrh	r3, [r4, #42]
	str	r0, [r4, #3900]
	movs	r0, #6
	muls	r0, r3, r0
	bl	ftl_malloc
	str	r0, [r4, #2316]
	ldrh	r0, [r4, #98]
	ldrh	r3, [r4, #54]
	adds	r0, r0, #31
	asrs	r0, r0, #5
	strh	r0, [r4, #3904]	@ movhi
	muls	r0, r3, r0
	lsls	r0, r0, #2
	bl	ftl_malloc
	ldrh	r2, [r4, #3904]
	movs	r3, #1
	ldrh	r6, [r4, #54]
	str	r0, [r4, #180]
	lsls	r2, r2, #2
	mov	r1, r2
.L586:
	cmp	r3, r6
	bcc	.L587
	ldr	r2, .L686+8
	movs	r1, #0
	add	r3, r2, r3, lsl #2
	adds	r2, r2, #56
	adds	r3, r3, #24
.L588:
	cmp	r2, r3
	bne	.L589
	ldr	r3, [r4, #3872]
	cbnz	r3, .L590
.L592:
	ldr	r1, .L686+12
	ldr	r0, .L686+16
	bl	sftl_printk
	mov	r0, #-1
	pop	{r4, r5, r6, r7, r8, pc}
.L587:
	ldr	r0, [r4, #180]
	adds	r3, r3, #1
	add	r0, r0, r1
	add	r1, r1, r2
	str	r0, [r5, #4]!
	b	.L586
.L589:
	str	r1, [r3, #4]!
	b	.L588
.L687:
	.align	2
.L686:
	.word	.LANCHOR0
	.word	33553920
	.word	.LANCHOR0+152
	.word	.LANCHOR1+186
	.word	.LC86
.L590:
	ldr	r3, [r4, #3876]
	cmp	r3, #0
	beq	.L592
	ldr	r3, [r4, #3892]
	cmp	r3, #0
	beq	.L592
	ldr	r3, [r4, #3896]
	cmp	r3, #0
	beq	.L592
	ldr	r3, [r4, #2492]
	cmp	r3, #0
	beq	.L592
	ldr	r3, [r4, #3900]
	cmp	r3, #0
	beq	.L592
	ldr	r3, [r4, #2316]
	cmp	r3, #0
	beq	.L592
	ldr	r3, [r4, #180]
	cmp	r3, #0
	beq	.L592
	ldr	r3, [r4, #2324]
	cmp	r3, #0
	beq	.L592
	ldr	r3, [r4, #3188]
	cmp	r3, #0
	beq	.L592
	ldr	r3, [r4, #3196]
	cmp	r3, #0
	beq	.L592
	ldr	r3, [r4, #3836]
	cmp	r3, #0
	beq	.L592
	ldr	r3, [r4, #3844]
	cmp	r3, #0
	beq	.L592
	ldr	r3, [r4, #3804]
	cmp	r3, #0
	beq	.L592
	ldr	r3, [r4, #3180]
	cmp	r3, #0
	beq	.L592
	ldr	r3, [r4, #3840]
	cmp	r3, #0
	beq	.L592
	ldr	r3, [r4, #3284]
	cmp	r3, #0
	beq	.L592
	ldr	r3, [r4, #3848]
	cmp	r3, #0
	beq	.L592
	ldr	r3, [r4, #3852]
	cmp	r3, #0
	beq	.L592
	ldr	r3, [r4, #3172]
	cmp	r3, #0
	beq	.L592
	ldr	r3, [r4, #3820]
	cmp	r3, #0
	beq	.L592
	ldr	r3, [r4, #3812]
	cmp	r3, #0
	beq	.L592
	ldr	r3, [r4, #3168]
	cmp	r3, #0
	beq	.L592
	ldr	r3, [r4, #3288]
	cmp	r3, #0
	beq	.L592
	ldr	r3, [r4, #3816]
	cmp	r3, #0
	beq	.L592
	ldr	r3, [r4, #3176]
	cmp	r3, #0
	beq	.L592
	ldr	r3, [r4, #2328]
	cmp	r3, #0
	beq	.L592
	ldr	r3, [r4, #3860]
	cmp	r3, #0
	beq	.L592
	ldr	r3, [r4, #148]
	cmp	r3, #0
	beq	.L592
	ldr	r3, .L688
	ldr	r2, [r3, #3880]
	cmp	r2, #0
	beq	.L592
	ldr	r2, [r3, #3884]
	cmp	r2, #0
	beq	.L592
	ldr	r3, [r3, #3888]
	cmp	r3, #0
	beq	.L592
	movs	r0, #0
	pop	{r4, r5, r6, r7, r8, pc}
.L689:
	.align	2
.L688:
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
	ldr	r6, .L696
	mov	r7, r1
	subs	r4, r5, #2
	movw	r8, #65535
	ldr	r9, .L696+4
	addw	r5, r5, #1022
	ldrh	r2, [r6, #3904]
	movs	r1, #0
	ldr	r10, .L696+8
	mov	r0, r7
	lsls	r2, r2, #2
	bl	ftl_memset
.L693:
	ldrh	r3, [r4, #2]
	cmp	r3, r8
	beq	.L690
	ldrh	r2, [r6, #98]
	cmp	r2, r3
	bhi	.L692
	movs	r2, #74
	mov	r1, r9
	mov	r0, r10
	bl	sftl_printk
.L692:
	ldrh	r3, [r4, #2]!
	movs	r2, #1
	cmp	r5, r4
	lsr	r1, r3, #5
	and	r3, r3, #31
	lsl	r3, r2, r3
	ldr	r2, [r7, r1, lsl #2]
	orr	r2, r2, r3
	str	r2, [r7, r1, lsl #2]
	bne	.L693
.L690:
	pop	{r4, r5, r6, r7, r8, r9, r10, pc}
.L697:
	.align	2
.L696:
	.word	.LANCHOR0
	.word	.LANCHOR1+197
	.word	.LC1
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
.L699:
	ldrh	r1, [r4, #6]
	uxth	r2, r3
	cmp	r1, r2
	bhi	.L703
	ldrh	fp, [r5]
	movs	r6, #0
	ldr	r2, .L712
	mov	r10, r6
.L704:
	ldrh	r1, [r4, #10]
	uxth	r3, r6
	cmp	r1, r3
	bhi	.L708
	mov	r0, r10
	add	sp, sp, #12
	@ sp needed
	pop	{r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L703:
	uxth	r2, r3
	ldr	r1, [r6, r2, lsl #2]
	movs	r2, #0
	ubfx	r1, r1, #10, #16
.L700:
	ldrh	r7, [r4, #10]
	uxth	r0, r2
	cmp	r7, r0
	bhi	.L702
	adds	r3, r3, #1
	b	.L699
.L702:
	uxth	r0, r2
	adds	r2, r2, #1
	ldrh	r7, [r9, r0, lsl #1]
	cmp	r7, r1
	ittt	eq
	ldrheq	r7, [r5, r0, lsl #1]
	addeq	r7, r7, #1
	strheq	r7, [r5, r0, lsl #1]	@ movhi
	b	.L700
.L708:
	ldrh	r1, [r4]
	uxth	r7, r6
	cmp	r1, r3
	bne	.L705
	ldrh	r1, [r2, #104]
	ldrh	r0, [r4, #2]
	cmp	r0, r1
	it	cc
	strhcc	r1, [r5, r7, lsl #1]	@ movhi
.L705:
	ldrh	r8, [r5, r7, lsl #1]
	cmp	fp, r8
	itt	hi
	movhi	r10, r3
	movhi	fp, r8
	cmp	r8, #0
	bne	.L707
	ldrh	r0, [r9, r7, lsl #1]
	cbz	r0, .L707
	movs	r1, #1
	str	r2, [sp, #4]
	bl	FtlFreeSysBlkQueueIn
	strh	r8, [r9, r7, lsl #1]	@ movhi
	ldr	r2, [sp, #4]
	ldrh	r3, [r4, #8]
	subs	r3, r3, #1
	strh	r3, [r4, #8]	@ movhi
.L707:
	adds	r6, r6, #1
	b	.L704
.L713:
	.align	2
.L712:
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
	bne	.L715
	ldrh	r3, [r0, #8]
	cbz	r3, .L716
	movw	r2, #677
	ldr	r1, .L724
	ldr	r0, .L724+4
	bl	sftl_printk
.L716:
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
.L717:
	movs	r0, #0
	pop	{r3, r4, r5, r6, r7, pc}
.L715:
	ldrh	r6, [r5, r3, lsl #1]
	movs	r1, #255
	ldrh	r3, [r0, #2]
	ldr	r5, .L724+8
	ldr	r2, [r0, #28]
	ldr	r7, [r0, #24]
	orr	r3, r3, r6, lsl #10
	str	r3, [r5, #3268]
	ldr	r3, [r5, #3284]
	str	r3, [r5, #3272]
	ldr	r3, [r5, #3288]
	str	r3, [r5, #3276]
	str	r2, [r3, #4]
	movw	r2, #64245
	strh	r2, [r3, #8]	@ movhi
	ldrh	r2, [r0, #4]
	strh	r6, [r3, #2]	@ movhi
	strh	r2, [r3]	@ movhi
	ldrh	r2, [r5, #104]
	ldr	r0, [r5, #3284]
	lsls	r2, r2, #3
	bl	ftl_memset
	movs	r2, #0
	mov	r3, r2
.L718:
	ldrh	r0, [r4, #6]
	uxth	r1, r2
	cmp	r0, r1
	bhi	.L720
	movs	r2, #1
	movs	r3, #0
	mov	r1, r2
	ldr	r0, .L724+12
	bl	FlashProgPages
	ldrh	r3, [r4, #2]
	mov	r0, r4
	adds	r3, r3, #1
	strh	r3, [r4, #2]	@ movhi
	bl	ftl_map_blk_gc
	b	.L717
.L720:
	uxth	r1, r2
	ldr	r0, [r7, r1, lsl #2]
	cmp	r6, r0, lsr #10
	bne	.L719
	ldr	r0, [r5, #3284]
	adds	r3, r3, #1
	uxth	r3, r3
	str	r1, [r0, r3, lsl #3]
	ldr	r0, [r7, r1, lsl #2]
	ldr	r1, [r5, #3284]
	add	r1, r1, r3, lsl #3
	str	r0, [r1, #4]
.L719:
	adds	r2, r2, #1
	b	.L718
.L725:
	.align	2
.L724:
	.word	.LANCHOR1+211
	.word	.LC1
	.word	.LANCHOR0
	.word	.LANCHOR0+3264
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
	ldr	r7, .L755
	mov	r9, r1
	mov	fp, r2
	movs	r6, #0
	ldr	r10, .L755+20
	mov	r5, r7
.L727:
	ldr	r3, [r7, #2524]
	adds	r3, r3, #1
	str	r3, [r7, #2524]
	ldrh	r3, [r7, #104]
	ldrh	r2, [r4, #2]
	subs	r3, r3, #1
	cmp	r2, r3
	bge	.L728
	ldrh	r2, [r4]
	movw	r3, #65535
	cmp	r2, r3
	bne	.L729
.L728:
	mov	r0, r4
	bl	Ftl_write_map_blk_to_last_page
.L729:
	ldrh	r2, [r4]
	ldr	r3, [r4, #12]
	ldrh	r3, [r3, r2, lsl #1]
	cbnz	r3, .L730
	movw	r2, #735
	mov	r1, r10
	ldr	r0, .L755+4
	bl	sftl_printk
.L730:
	ldrh	r2, [r4]
	ldrh	r3, [r4, #10]
	cmp	r2, r3
	bcc	.L731
	mov	r2, #736
	mov	r1, r10
	ldr	r0, .L755+4
	bl	sftl_printk
.L731:
	ldrh	r2, [r4]
	movs	r1, #16
	ldr	r3, [r4, #12]
	ldr	r0, [r5, #3288]
	ldrh	r8, [r3, r2, lsl #1]
	ldrh	r3, [r4, #2]
	str	r0, [r5, #3276]
	str	fp, [r5, #3272]
	orr	r3, r3, r8, lsl #10
	str	r3, [r5, #3268]
	bl	__memzero
	ldr	r3, [r5, #3276]
	ldr	r2, [r4, #28]
	ldr	r0, .L755+8
	strh	r9, [r3, #8]	@ movhi
	str	r2, [r3, #4]
	ldrh	r2, [r4, #4]
	strh	r8, [r3, #2]	@ movhi
	strh	r2, [r3]	@ movhi
	movs	r3, #1
	mov	r2, r3
	mov	r1, r3
	bl	FlashProgPages
	ldrh	r3, [r4, #2]
	adds	r3, r3, #1
	uxth	r3, r3
	strh	r3, [r4, #2]	@ movhi
	ldr	r2, [r5, #3264]
	adds	r1, r2, #1
	bne	.L732
	ldr	r1, [r5, #3268]
	adds	r6, r6, #1
	ldr	r0, .L755+12
	uxth	r6, r6
	bl	sftl_printk
	ldrh	r3, [r4, #2]
	cmp	r3, #2
	ittt	ls
	ldrhls	r3, [r5, #104]
	addls	r3, r3, #-1
	strhls	r3, [r4, #2]	@ movhi
	cmp	r6, #3
	bls	.L734
	mov	r2, r6
	ldr	r1, [r5, #3268]
	ldr	r0, .L755+16
	bl	sftl_printk
.L735:
	b	.L735
.L734:
	ldr	r3, [r4, #32]
	cmp	r3, #0
	beq	.L727
.L752:
	b	.L752
.L732:
	cbz	r2, .L738
	cmp	r3, #1
	strh	r8, [r4, #40]	@ movhi
	beq	.L727
	cmp	r2, #256
.L754:
	beq	.L727
	ldr	r2, [r5, #3268]
	movs	r0, #0
	ldr	r3, [r4, #24]
	str	r2, [r3, r9, lsl #2]
	pop	{r3, r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L738:
	cmp	r3, #1
	b	.L754
.L756:
	.align	2
.L755:
	.word	.LANCHOR0
	.word	.LC1
	.word	.LANCHOR0+3264
	.word	.LC87
	.word	.LC88
	.word	.LANCHOR1+242
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
	ldr	r4, .L764
	mov	r8, r1
	ldrh	r3, [r4, #136]
	cmp	r3, r0
	bcs	.L758
	mov	r2, #510
	ldr	r1, .L764+4
	ldr	r0, .L764+8
	bl	sftl_printk
.L758:
	ldr	r3, [r4, #3892]
	movs	r5, #12
	ldr	r7, [r3, r6, lsl #2]
	cbnz	r7, .L759
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
.L760:
	movs	r0, #0
	pop	{r4, r5, r6, r7, r8, pc}
.L759:
	mul	r5, r5, r8
	ldr	r3, [r4, #2492]
	movs	r2, #1
	ldr	r0, .L764+12
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
	bne	.L761
	mov	r2, r7
	mov	r1, r6
	ldr	r0, .L764+16
	bl	sftl_printk
	lsrs	r3, r7, #10
	mov	r1, r6
	ldr	r0, .L764+20
	strh	r3, [r4, #3948]	@ movhi
	ldr	r3, [r4, #2492]
	add	r3, r3, r5
	ldr	r2, [r3, #8]
	bl	FtlMapWritePage
.L761:
	ldrh	r3, [r8, #8]
	cmp	r3, r6
	beq	.L762
	mov	r2, r7
	mov	r1, r6
	ldr	r0, .L764+24
	bl	sftl_printk
	movs	r3, #4
	ldr	r1, [r4, #3276]
	mov	r2, r3
	ldr	r0, .L764+28
	bl	rknand_print_hex
	ldrh	r3, [r4, #136]
	movs	r2, #4
	ldr	r1, [r4, #3892]
	ldr	r0, .L764+32
	bl	rknand_print_hex
.L762:
	ldrh	r3, [r8, #8]
	cmp	r3, r6
	beq	.L763
	movw	r2, #535
	ldr	r1, .L764+4
	ldr	r0, .L764+8
	bl	sftl_printk
.L763:
	ldr	r3, [r4, #2492]
	movs	r1, #0
	adds	r2, r3, r5
	str	r1, [r2, #4]
	strh	r6, [r3, r5]	@ movhi
	b	.L760
.L765:
	.align	2
.L764:
	.word	.LANCHOR0
	.word	.LANCHOR1+258
	.word	.LC1
	.word	.LANCHOR0+3264
	.word	.LC89
	.word	.LANCHOR0+3908
	.word	.LC90
	.word	.LC91
	.word	.LC92
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
	ldrh	ip, [r4, #10]
	ldrh	r2, [r4, #8]
	ldrh	r1, [r4, #40]
	sub	r3, ip, #4
	ldr	r5, .L790
	cmp	r2, r3
	bge	.L767
	movw	r3, #65535
	cmp	r1, r3
	beq	.L769
	ldrh	r2, [r4, #2]
	ldrh	r3, [r5, #104]
	cmp	r2, r3
	bcc	.L770
.L767:
	movw	r3, #65535
	uxth	r0, r0
	cmp	r1, r3
	beq	.L771
	ldrh	r3, [r4, #2]
	ldrh	r2, [r5, #104]
	cmp	r2, r3
	bls	.L772
.L771:
	ldrh	r9, [r6, r0, lsl #1]
	lsls	r2, r0, #1
	cmp	r9, #0
	beq	.L769
	ldr	r3, [r4, #32]
	cbnz	r3, .L769
	movs	r1, #1
	str	r1, [r4, #32]
	strh	r3, [r6, r2]	@ movhi
	ldrh	r3, [r4, #8]
	ldrh	r2, [r4, #2]
	subs	r3, r3, #1
	strh	r3, [r4, #8]	@ movhi
	ldrh	r3, [r5, #104]
	cmp	r2, r3
	bcc	.L777
	mov	r0, r4
	bl	ftl_map_blk_alloc_new_blk
.L777:
	movs	r6, #0
.L778:
	ldrh	r2, [r4, #6]
	uxth	fp, r6
	cmp	r2, fp
	bhi	.L785
	movs	r1, #1
	mov	r0, r9
	bl	FtlFreeSysBlkQueueIn
	movs	r3, #0
	str	r3, [r4, #32]
.L769:
	ldrh	r2, [r4, #2]
	ldrh	r3, [r5, #104]
	cmp	r2, r3
	bcc	.L770
	mov	r0, r4
	bl	ftl_map_blk_alloc_new_blk
.L770:
	movs	r0, #0
	add	sp, sp, #12
	@ sp needed
	pop	{r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L775:
	ldrh	r8, [r6, r2, lsl #1]
	add	lr, r2, #1
	cmp	r8, r1
	beq	.L773
	mov	r2, lr
.L774:
	uxth	r7, r2
	cmp	r7, ip
	bcc	.L775
	mov	r7, r0
.L773:
	uxth	r2, r2
	ldr	r0, .L790+4
	ldrh	r2, [r6, r2, lsl #1]
	bl	sftl_printk
	movw	r3, #65535
	mov	r0, r7
	strh	r3, [r4, #40]	@ movhi
	b	.L771
.L772:
	movs	r2, #0
	b	.L774
.L785:
	uxth	r7, r6
	ldr	r2, [r10, r7, lsl #2]
	add	r3, r10, r7, lsl #2
	str	r3, [sp, #4]
	cmp	r9, r2, lsr #10
	bne	.L779
	ldr	r2, [r5, #3848]
	ldr	r8, [r5, #3288]
	ldr	r0, .L790+8
	str	r2, [r5, #3272]
	str	r8, [r5, #3276]
	ldr	r2, [r10, r7, lsl #2]
	str	r2, [r5, #3268]
	movs	r2, #1
	mov	r1, r2
	bl	FlashReadPages
	ldrh	r2, [r8, #8]
	cmp	r2, fp
	beq	.L780
	movw	r2, #647
	ldr	r1, .L790+12
	ldr	r0, .L790+16
	bl	sftl_printk
.L780:
	ldr	r2, [r5, #3264]
	adds	r2, r2, #1
	bne	.L781
.L783:
	ldr	r2, [sp, #4]
	movs	r3, #0
	str	r3, [r2]
.L782:
	b	.L782
.L781:
	ldrh	r2, [r8, #8]
	cmp	r2, fp
	bne	.L783
	ldrh	r2, [r8]
	ldrh	r3, [r4, #4]
	cmp	r2, r3
	bne	.L783
	ldr	r2, [r5, #3272]
	mov	r1, r7
	mov	r0, r4
	bl	FtlMapWritePage
.L779:
	adds	r6, r6, #1
	b	.L778
.L791:
	.align	2
.L790:
	.word	.LANCHOR0
	.word	.LC93
	.word	.LANCHOR0+3264
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
	ldr	r5, .L793
	muls	r4, r0, r4
	ldr	r3, [r5, #2492]
	addw	r0, r5, #3908
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
.L794:
	.align	2
.L793:
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
	ldr	r4, .L809
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
	bcc	.L796
	movw	r2, #847
	ldr	r1, .L809+4
	ldr	r0, .L809+8
	bl	sftl_printk
.L796:
	ldrh	r2, [r4, #138]
	movs	r3, #0
	ldr	r1, [r4, #2492]
	mov	r10, #12
.L797:
	uxth	r6, r3
	cmp	r6, r2
	bcc	.L802
	bl	select_l2p_ram_region
	mul	r10, r10, r0
	ldr	r3, [r4, #2492]
	mov	r6, r0
	ldrh	r1, [r3, r10]
	add	r2, r3, r10
	movw	r3, #65535
	cmp	r1, r3
	beq	.L803
	ldr	r3, [r2, #4]
	cmp	r3, #0
	bge	.L803
	bl	flush_l2p_region
.L803:
	mov	r1, r6
	mov	r0, r8
	bl	load_l2p_region
	b	.L798
.L802:
	adds	r3, r3, #1
	mla	r0, r10, r3, r1
	ldrh	r0, [r0, #-12]
	cmp	r0, r8
	bne	.L797
.L798:
	movs	r3, #12
	cmp	r9, #0
	bne	.L799
	ldr	r2, [r4, #2492]
	mla	r3, r3, r6, r2
	ldr	r3, [r3, #8]
	ldr	r3, [r3, r5, lsl #2]
	str	r3, [r7]
.L800:
	ldr	r2, [r4, #2492]
	movs	r3, #12
	mla	r6, r3, r6, r2
	ldr	r3, [r6, #4]
	adds	r2, r3, #1
	beq	.L806
	adds	r3, r3, #1
	str	r3, [r6, #4]
.L806:
	movs	r0, #0
	pop	{r4, r5, r6, r7, r8, r9, r10, pc}
.L799:
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
	b	.L800
.L810:
	.align	2
.L809:
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
	ldr	r5, .L821
	ubfx	r0, r1, #10, #16
	str	r1, [sp, #4]
	bl	P2V_block_in_plane
	ldr	r2, [r5, #2324]
	mov	r7, r0
	ldrh	r3, [r2, r0, lsl #1]
	cbnz	r3, .L812
	ldr	r4, [r5, #2340]
	cbz	r4, .L813
	ldr	r1, [r5, #2316]
	mov	ip, #6
	ldr	r2, .L821+4
	movw	lr, #65535
	ldrh	r0, [r5, #2344]
	subs	r4, r4, r1
	asrs	r4, r4, #1
	muls	r4, r2, r4
	uxth	r4, r4
.L814:
	uxth	r2, r3
	cmp	r0, r2
	bls	.L813
	cmp	r4, r7
	bne	.L815
	mov	r1, r4
	ldr	r0, .L821+8
	bl	List_remove_node
	ldrh	r3, [r5, #2344]
	cbnz	r3, .L816
	movw	r2, #1737
	ldr	r1, .L821+12
	ldr	r0, .L821+16
	bl	sftl_printk
.L816:
	ldrh	r3, [r5, #2344]
	mov	r0, r4
	subs	r3, r3, #1
	strh	r3, [r5, #2344]	@ movhi
	bl	INSERT_DATA_LIST
	ldr	r2, [r5, #2324]
	ldrh	r3, [r2, r7, lsl #1]
.L812:
	adds	r3, r3, #1
	strh	r3, [r2, r7, lsl #1]	@ movhi
	b	.L813
.L815:
	mul	r4, ip, r4
	adds	r3, r3, #1
	ldrh	r4, [r1, r4]
	cmp	r4, lr
	bne	.L814
.L813:
	movs	r2, #1
	add	r1, sp, #4
	mov	r0, r6
	bl	log2phys
	add	sp, sp, #12
	@ sp needed
	pop	{r4, r5, r6, r7, pc}
.L822:
	.align	2
.L821:
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
	ldr	r6, .L837
	.pad #32
	sub	sp, sp, #32
	movs	r1, #0
	ldr	r8, .L837+16
	mov	r4, r6
	ldr	r0, .L837+4
	bl	sftl_printk
.L824:
	ldr	r3, [r6, #2504]
	cmp	r5, r3
	bcc	.L830
	add	sp, sp, #32
	@ sp needed
	pop	{r4, r5, r6, r7, r8, pc}
.L830:
	movs	r2, #0
	add	r1, sp, #28
	mov	r0, r5
	bl	log2phys
	ubfx	r3, r5, #0, #11
	cbnz	r3, .L825
	ldr	r2, [sp, #28]
	mov	r1, r5
	mov	r0, r8
	bl	sftl_printk
.L825:
	ldr	r3, [sp, #28]
	adds	r2, r3, #1
	beq	.L827
	str	r3, [r4, #3268]
	movs	r2, #0
	ldr	r3, [r4, #3284]
	movs	r1, #1
	ldr	r7, [r4, #3288]
	ldr	r0, .L837+8
	str	r3, [r4, #3272]
	str	r5, [r4, #3280]
	str	r7, [r4, #3276]
	str	r2, [r4, #3264]
	bl	FlashReadPages
	ldr	r3, [r4, #3264]
	cmp	r3, #256
	beq	.L828
	adds	r3, r3, #1
	beq	.L828
	ldr	r3, [r7, #8]
	cmp	r5, r3
	beq	.L827
.L828:
	ldr	r2, [r4, #3272]
	ldr	r3, [r4, #3276]
	ldr	r0, .L837+12
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
.L827:
	adds	r5, r5, #1
	b	.L824
.L838:
	.align	2
.L837:
	.word	.LANCHOR0
	.word	.LC94
	.word	.LANCHOR0+3264
	.word	.LC96
	.word	.LC95
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
	ldr	r3, .L852
	push	{r4, r5, r6, lr}
	.save {r4, r5, r6, lr}
	.pad #88
	sub	sp, sp, #88
	mov	r4, r3
	ldr	r0, [r3, #2716]
	cmp	r0, #0
	beq	.L840
	ldr	r1, [r3, #2720]
	ldr	r2, [r3, #2504]
	cmp	r1, r2
	bcs	.L841
	mov	r5, #2048
.L846:
	ldr	r0, [r4, #2720]
	ldr	r3, [r4, #2504]
	cmp	r0, r3
	bcs	.L843
	movs	r2, #0
	mov	r1, sp
	bl	log2phys
	ldr	r2, [sp]
	ldr	r3, [r4, #2720]
	adds	r1, r2, #1
	add	r3, r3, #1
	str	r3, [r4, #2720]
	beq	.L844
	str	r3, [sp, #20]
	add	r0, sp, #88
	ldr	r3, .L852+4
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
	bne	.L843
	ldr	r0, [sp]
	ubfx	r0, r0, #10, #16
	bl	P2V_block_in_plane
	bl	FtlGcRefreshBlock
.L843:
	mov	r0, #-1
.L839:
	add	sp, sp, #88
	@ sp needed
	pop	{r4, r5, r6, pc}
.L844:
	subs	r5, r5, #1
	bne	.L846
	b	.L843
.L841:
	ldr	r2, [r3, #2508]
	movs	r0, #0
	str	r0, [r3, #2716]
	str	r0, [r3, #2720]
	str	r2, [r3, #2712]
	b	.L839
.L840:
	ldr	r1, [r3, #2508]
	ldr	r6, [r3, #2712]
	add	r2, r1, #1048576
	cmp	r6, r2
	bhi	.L849
	ldr	r2, [r3, #2564]
	lsrs	r5, r2, #10
	mov	r2, #33554432
	asrs	r2, r2, r5
	add	r2, r2, r6
	cmp	r1, r2
	bhi	.L849
	ldrb	r3, [r3, #2296]	@ zero_extendqisi2
	cmp	r3, #0
	bne	.L839
.L849:
	movs	r3, #1
	movs	r0, #0
	str	r3, [r4, #2716]
	str	r0, [r4, #2720]
	str	r1, [r4, #2712]
	b	.L839
.L853:
	.align	2
.L852:
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
	ldr	r6, .L878
	ldr	r7, .L878+4
	ldr	r1, .L878+8
	mov	r5, r6
	ldr	r0, .L878+12
	bl	sftl_printk
	mov	r1, #8192
	ldr	r0, .L878+4
	bl	__memzero
.L855:
	ldr	r3, [r6, #2504]
	cmp	r4, r3
	bcc	.L857
	ldr	r8, .L878+4
	movs	r4, #0
	ldr	r10, .L878+24
	mov	r7, r4
	movw	r9, #65535
.L858:
	ldrh	r2, [r5, #40]
	uxth	r3, r4
	cmp	r2, r3
	bhi	.L860
	ldr	r4, [r5, #2340]
	cbz	r4, .L861
	ldr	r3, [r5, #2316]
	movs	r6, #0
	ldrh	r8, [r5, #2344]
	mov	fp, #6
	ldr	r9, .L878+4
	subs	r4, r4, r3
	ldr	r3, .L878+16
	asrs	r4, r4, #1
	ldr	r10, .L878+28
	muls	r4, r3, r4
	uxth	r4, r4
.L862:
	uxth	r3, r6
	cmp	r8, r3
	bls	.L861
	ldr	r3, [r5, #2324]
	ldrh	r2, [r3, r4, lsl #1]
	cbz	r2, .L863
	movs	r7, #1
	ldrh	r3, [r9, r4, lsl #1]
	mov	r1, r4
	mov	r0, r10
	bl	sftl_printk
.L863:
	mul	r4, fp, r4
	ldr	r3, [r5, #2316]
	adds	r6, r6, #1
	ldrh	r4, [r3, r4]
	movw	r3, #65535
	cmp	r4, r3
	bne	.L862
.L861:
	cbz	r7, .L854
	movw	r2, #2373
	ldr	r1, .L878+8
	ldr	r0, .L878+20
	bl	sftl_printk
.L854:
	add	sp, sp, #12
	@ sp needed
	pop	{r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L857:
	movs	r2, #0
	add	r1, sp, #4
	mov	r0, r4
	bl	log2phys
	ldr	r0, [sp, #4]
	adds	r3, r0, #1
	beq	.L856
	ubfx	r0, r0, #10, #16
	bl	P2V_block_in_plane
	ldrh	r3, [r7, r0, lsl #1]
	adds	r3, r3, #1
	strh	r3, [r7, r0, lsl #1]	@ movhi
.L856:
	adds	r4, r4, #1
	b	.L855
.L860:
	ldr	r3, [r5, #2324]
	uxth	r6, r4
	ldrh	r2, [r3, r6, lsl #1]
	ldrh	r3, [r8, r6, lsl #1]
	cmp	r2, r3
	beq	.L859
	mov	r1, r6
	mov	r0, r10
	bl	sftl_printk
	ldr	r3, [r5, #2324]
	ldrh	r3, [r3, r6, lsl #1]
	cmp	r3, r9
	beq	.L859
	ldrh	r2, [r8, r6, lsl #1]
	cmp	r2, r3
	it	hi
	movhi	r7, #1
.L859:
	adds	r4, r4, #1
	b	.L858
.L879:
	.align	2
.L878:
	.word	.LANCHOR0
	.word	check_vpc_table
	.word	.LANCHOR1+314
	.word	.LC97
	.word	-1431655765
	.word	.LC1
	.word	.LC98
	.word	.LC99
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
	cbz	r3, .L880
	ldrh	r5, [r0, #6]
	movs	r3, #0
	ldr	r4, .L884
	ldr	r2, [r0, #24]
	str	r3, [r0, #36]
	subs	r5, r5, #1
	ldr	r0, [r4, #3848]
	uxth	r5, r5
	ldr	r1, [r4, #3288]
	str	r0, [r4, #3272]
	str	r1, [r4, #3276]
	ldr	r2, [r2, r5, lsl #2]
	str	r2, [r4, #3268]
	cbz	r2, .L882
	movs	r2, #1
	add	r0, r4, #3264
	mov	r1, r2
	bl	FlashReadPages
.L883:
	ldr	r2, [r4, #3272]
	mov	r1, r5
	mov	r0, r6
	pop	{r4, r5, r6, lr}
	b	FtlMapWritePage
.L882:
	ldrh	r2, [r4, #110]
	movs	r1, #255
	bl	ftl_memset
	b	.L883
.L880:
	pop	{r4, r5, r6, pc}
.L885:
	.align	2
.L884:
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
	ldr	r4, .L967
	mov	r1, r5
	.pad #20
	sub	sp, sp, #20
	ldr	r2, [r4, #128]
	ldr	r0, [r4, #3896]
	strh	r5, [r4, #3952]	@ movhi
	strh	r5, [r4, #144]	@ movhi
	lsls	r2, r2, #2
	bl	ftl_memset
	ldr	r2, [r4, #128]
	mov	r1, r5
	ldr	r0, [r4, #3872]
	lsls	r2, r2, #1
	bl	ftl_memset
	ldrh	r2, [r4, #120]
	mov	r1, r5
	ldr	r0, [r4, #3884]
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
.L887:
	ldrh	r3, [r4, #42]
	cmp	r3, r8
	bls	.L929
	movs	r5, #0
	ldrh	fp, [r4, #32]
	ldr	r9, [r4, #3836]
	mov	r7, r5
	ldr	r3, [r4, #3172]
	mov	r10, #20
	ldr	r2, [r4, #3176]
	ldrh	r6, [r4, #112]
	b	.L930
.L890:
	str	r3, [sp, #8]
	mov	r1, r8
	ldr	r3, .L967+4
	str	r2, [sp, #12]
	ldrb	r0, [r3, r5]	@ zero_extendqisi2
	bl	V2P_block
	str	r0, [sp, #4]
	bl	FtlBbmIsBadBlock
	ldr	r3, [sp, #8]
	ldr	r2, [sp, #12]
	cbnz	r0, .L888
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
.L888:
	adds	r5, r5, #1
.L930:
	uxth	r1, r5
	cmp	fp, r1
	bhi	.L890
	cbnz	r7, .L891
.L928:
	add	r8, r8, #1
	uxth	r8, r8
	b	.L887
.L891:
	mov	r0, r9
	mov	r9, #0
	movs	r2, #1
	mov	r1, r7
	bl	FlashReadPages
.L892:
	uxth	r3, r9
	cmp	r7, r3
	bls	.L928
	movs	r3, #20
	mul	r10, r3, r9
	ldr	r3, [r4, #3836]
	add	r2, r3, r10
	ldr	r3, [r3, r10]
	ldr	r5, [r2, #4]
	ldr	r6, [r2, #12]
	adds	r3, r3, #1
	ubfx	r5, r5, #10, #16
	bne	.L895
	mov	fp, #16
	movw	r3, #65535
.L897:
	ldr	r0, [r4, #3836]
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
	bne	.L894
	ldr	r3, [r4, #3836]
	mov	r2, #-1
	str	r2, [r3, r10]
	ldr	r3, [r4, #3836]
	ldr	r3, [r3, r10]
	cmp	r3, r2
	bne	.L895
.L896:
	movs	r1, #0
	mov	r0, r5
	bl	FtlFreeSysBlkQueueIn
	b	.L900
.L894:
	ldr	r2, [r4, #3836]
	ldr	r2, [r2, r10]
	adds	r2, r2, #1
	bne	.L895
	add	fp, fp, #-1
	uxth	fp, fp
	cmp	fp, #0
	bne	.L897
	b	.L896
.L895:
	ldr	r2, [r4, #2540]
	ldr	r3, [r6, #4]
	adds	r1, r2, #1
	beq	.L898
	cmp	r2, r3
	bhi	.L899
.L898:
	adds	r2, r3, #1
	itt	ne
	addne	r2, r3, #1
	strne	r2, [r4, #2540]
.L899:
	ldrh	r2, [r6]
	movw	r1, #61604
	cmp	r2, r1
	beq	.L901
	bhi	.L902
	movw	r3, #61574
	cmp	r2, r3
	beq	.L903
.L900:
	add	r9, r9, #1
	b	.L892
.L902:
	movw	r3, #61634
	cmp	r2, r3
	beq	.L904
	movw	r3, #65535
	cmp	r2, r3
	beq	.L896
	b	.L900
.L904:
	ldrh	r2, [r4, #3952]
	ldr	r3, [r4, #128]
	cmp	r2, r3
	bls	.L906
	mov	r2, #1256
	ldr	r1, .L967+8
	ldr	r0, .L967+12
	bl	sftl_printk
.L906:
	ldr	r1, [r4, #128]
	ldrh	r0, [r4, #3952]
	ldr	r10, [r4, #3896]
	uxth	ip, r1
	add	r3, ip, #-1
	sub	ip, ip, r0
	add	ip, ip, #-1
	sxth	r3, r3
	sxth	ip, ip
.L907:
	cmp	r3, ip
	bgt	.L913
	cmp	r3, #0
	bge	.L945
	b	.L900
.L913:
	ldr	r2, [r6, #4]
	lsl	fp, r3, #2
	mov	lr, r2
	ldr	r2, [r10, r3, lsl #2]
	cmp	lr, r2
	bls	.L908
	ldr	r2, [r10]
	cbnz	r2, .L909
	cmp	r1, r0
	itt	ne
	addne	r0, r0, #1
	strhne	r0, [r4, #3952]	@ movhi
.L909:
	uxth	ip, r3
	movs	r2, #0
.L910:
	uxth	r0, r2
	sxth	r1, r2
	cmp	ip, r0
	bhi	.L911
	ldr	r2, [r4, #3896]
	cmp	r3, #0
	ldr	r1, [r6, #4]
	str	r1, [r2, fp]
	ldr	r2, [r4, #3872]
	strh	r5, [r2, r3, lsl #1]	@ movhi
	blt	.L900
	ldrh	r0, [r4, #3952]
	ldr	r2, [r4, #128]
	subs	r2, r2, r0
	subs	r2, r2, #1
	sxth	r2, r2
	cmp	r3, r2
	bgt	.L900
.L945:
	ldr	r2, [r4, #3896]
	adds	r0, r0, #1
	ldr	r1, [r6, #4]
	strh	r0, [r4, #3952]	@ movhi
	str	r1, [r2, r3, lsl #2]
	ldr	r2, [r4, #3872]
.L965:
	strh	r5, [r2, r3, lsl #1]	@ movhi
	b	.L900
.L911:
	ldr	r0, [r4, #3896]
	adds	r2, r2, #1
	add	lr, r0, r1, lsl #2
	ldr	lr, [lr, #4]
	str	lr, [r0, r1, lsl #2]
	ldr	r0, [r4, #3872]
	add	lr, r0, r1, lsl #1
	ldrh	lr, [lr, #2]
	strh	lr, [r0, r1, lsl #1]	@ movhi
	b	.L910
.L908:
	subs	r3, r3, #1
	sxth	r3, r3
	b	.L907
.L968:
	.align	2
.L967:
	.word	.LANCHOR0
	.word	.LANCHOR0+60
	.word	.LANCHOR1+328
	.word	.LC1
.L903:
	ldrh	r2, [r4, #144]
	ldrh	r3, [r4, #120]
	cmp	r2, r3
	bls	.L916
	movw	r2, #1297
	ldr	r1, .L969
	ldr	r0, .L969+4
	bl	sftl_printk
.L916:
	ldrh	lr, [r4, #120]
	ldrh	r0, [r4, #144]
	ldr	r10, [r4, #3884]
	add	ip, lr, #-1
	sxth	r3, ip
	sub	ip, ip, r0
.L917:
	cmp	r3, ip
	ble	.L922
	ldr	r2, [r6, #4]
	lsl	fp, r3, #2
	ldr	r1, [r10, r3, lsl #2]
	cmp	r2, r1
	bls	.L918
	ldr	r2, [r10]
	cbnz	r2, .L919
	cmp	lr, r0
	itt	ne
	addne	r0, r0, #1
	strhne	r0, [r4, #144]	@ movhi
.L919:
	uxth	ip, r3
	movs	r2, #0
.L920:
	uxth	r0, r2
	sxth	r1, r2
	cmp	ip, r0
	bhi	.L921
	ldr	r2, [r4, #3884]
	ldr	r1, [r6, #4]
	str	r1, [r2, fp]
	ldr	r2, [r4, #148]
	strh	r5, [r2, r3, lsl #1]	@ movhi
.L922:
	cmp	r3, #0
	blt	.L900
	ldrh	r2, [r4, #120]
	ldrh	r1, [r4, #144]
	subs	r2, r2, #1
	subs	r2, r2, r1
	sxth	r2, r2
	cmp	r3, r2
	bgt	.L900
	adds	r1, r1, #1
	ldr	r2, [r4, #3884]
	strh	r1, [r4, #144]	@ movhi
	ldr	r1, [r6, #4]
	str	r1, [r2, r3, lsl #2]
	ldr	r2, [r4, #148]
	b	.L965
.L921:
	ldr	r0, [r4, #3884]
	adds	r2, r2, #1
	add	lr, r0, r1, lsl #2
	ldr	lr, [lr, #4]
	str	lr, [r0, r1, lsl #2]
	ldr	r0, [r4, #148]
	add	lr, r0, r1, lsl #1
	ldrh	lr, [lr, #2]
	strh	lr, [r0, r1, lsl #1]	@ movhi
	b	.L920
.L918:
	subs	r3, r3, #1
	sxth	r3, r3
	b	.L917
.L901:
	ldrh	r1, [r4, #2576]
	movw	r2, #65535
	cmp	r1, r2
	bne	.L924
	strh	r5, [r4, #2576]	@ movhi
.L966:
	str	r3, [r4, #2584]
	b	.L900
.L924:
	ldrh	r0, [r4, #2580]
	cmp	r0, r2
	beq	.L925
	movs	r1, #1
	bl	FtlFreeSysBlkQueueIn
.L925:
	ldr	r2, [r4, #2584]
	ldr	r3, [r6, #4]
	cmp	r2, r3
	bcs	.L926
	ldrh	r3, [r4, #2576]
	strh	r5, [r4, #2576]	@ movhi
	strh	r3, [r4, #2580]	@ movhi
	ldr	r3, [r6, #4]
	b	.L966
.L926:
	strh	r5, [r4, #2580]	@ movhi
	b	.L900
.L929:
	ldr	r1, [r4, #3872]
	ldrh	r3, [r1]
	cbz	r3, .L931
.L934:
	ldr	r1, [r4, #148]
	ldrh	r2, [r1]
	cmp	r2, #0
	beq	.L932
.L933:
	ldrh	r2, [r4, #3952]
	ldr	r3, [r4, #128]
	cmp	r2, r3
	bls	.L964
	movw	r2, #1422
	ldr	r1, .L969
	ldr	r0, .L969+4
	bl	sftl_printk
.L964:
	movs	r0, #0
	add	sp, sp, #20
	@ sp needed
	pop	{r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L931:
	ldrh	r2, [r4, #3952]
	cmp	r2, #0
	beq	.L934
	ldr	r0, [r4, #128]
.L935:
	sxth	r2, r3
	cmp	r2, r0
	bcs	.L934
	ldrh	r5, [r1, r2, lsl #1]
	adds	r3, r3, #1
	cmp	r5, #0
	beq	.L935
	mov	r3, r2
	movs	r5, #0
.L936:
	ldr	r1, [r4, #128]
	cmp	r3, r1
	bcs	.L934
	ldr	r1, [r4, #3872]
	subs	r0, r3, r2
	ldrh	r6, [r1, r3, lsl #1]
	strh	r6, [r1, r0, lsl #1]	@ movhi
	ldr	r1, [r4, #3896]
	ldr	r6, [r1, r3, lsl #2]
	str	r6, [r1, r0, lsl #2]
	ldr	r1, [r4, #3872]
	strh	r5, [r1, r3, lsl #1]	@ movhi
	adds	r3, r3, #1
	sxth	r3, r3
	b	.L936
.L932:
	ldrh	r3, [r4, #144]
	cmp	r3, #0
	beq	.L933
	ldrh	r0, [r4, #120]
.L941:
	sxth	r3, r2
	cmp	r3, r0
	mov	r5, r3
	bge	.L933
	ldrh	r6, [r1, r3, lsl #1]
	adds	r2, r2, #1
	cmp	r6, #0
	beq	.L941
	movs	r0, #0
.L942:
	ldrh	r2, [r4, #120]
	cmp	r3, r2
	bge	.L933
	ldr	r2, [r4, #148]
	subs	r1, r3, r5
	ldrh	r6, [r2, r3, lsl #1]
	strh	r6, [r2, r1, lsl #1]	@ movhi
	ldr	r2, [r4, #3884]
	ldr	r6, [r2, r3, lsl #2]
	str	r6, [r2, r1, lsl #2]
	ldr	r2, [r4, #148]
	strh	r0, [r2, r3, lsl #1]	@ movhi
	adds	r3, r3, #1
	sxth	r3, r3
	b	.L942
.L970:
	.align	2
.L969:
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
	ldr	r5, .L996
	ldr	r9, [r0, #12]
	lsl	r2, r10, #2
	str	r3, [sp, #12]
	ldrh	r3, [r0, #8]
	ldr	r0, [sp, #4]
	str	r3, [sp, #8]
	bl	ftl_memset
	ldr	r3, [r5, #3284]
	movs	r2, #1
	ldr	r6, [r5, #3288]
	str	r2, [r4, #36]
	str	r3, [r5, #3272]
	movw	r3, #65535
	str	r6, [r5, #3276]
	strh	r3, [r4]	@ movhi
	strh	r3, [r4, #2]	@ movhi
	movs	r3, #0
	str	r3, [r4, #32]
	mov	r8, r3
	str	r3, [r4, #28]
	ldr	r3, [sp, #8]
	add	fp, r3, #-1
	mov	r3, r5
.L972:
	ldr	r1, [sp, #8]
	sxth	r2, r8
	cmp	r2, r1
	bge	.L990
	cmp	r2, fp
	lsl	r7, r2, #1
	bne	.L973
	ldrh	r0, [r9, fp, lsl #1]
	movs	r1, #1
	bl	FtlGetLastWrittenPage
	ldr	r3, [sp, #12]
	add	r7, r7, r9
	strh	r8, [r4]	@ movhi
	sxth	r9, r0
	ldr	r8, .L996+4
	adds	r0, r0, #1
	strh	r0, [r4, #2]	@ movhi
	add	r9, r9, #1
	ldr	r3, [r3, fp, lsl #2]
	mov	fp, #0
	str	r3, [r4, #28]
.L974:
	sxth	r2, fp
	cmp	r2, r9
	blt	.L977
.L990:
	mov	r0, r4
	bl	ftl_free_no_use_map_blk
	ldrh	r2, [r4, #2]
	ldrh	r3, [r5, #104]
	cmp	r2, r3
	bne	.L979
	mov	r0, r4
	bl	ftl_map_blk_alloc_new_blk
.L979:
	mov	r0, r4
	bl	ftl_map_blk_gc
	mov	r0, r4
	bl	ftl_map_blk_gc
	movs	r0, #0
	add	sp, sp, #28
	@ sp needed
	pop	{r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L977:
	ldrh	r1, [r7]
	mov	r0, r8
	orr	r2, r2, r1, lsl #10
	str	r2, [r5, #3268]
	movs	r2, #1
	mov	r1, r2
	bl	FlashReadPages
	ldr	r2, [r5, #3264]
	adds	r2, r2, #1
	beq	.L975
	ldrh	r2, [r6, #8]
	cmp	r10, r2
	bls	.L976
	ldrh	r0, [r6]
	ldrh	r1, [r4, #4]
	cmp	r0, r1
	bne	.L976
	ldr	r1, [r5, #3268]
	ldr	r3, [sp, #4]
	str	r1, [r3, r2, lsl #2]
.L976:
	add	fp, fp, #1
	b	.L974
.L975:
	ldrh	r2, [r7]
	strh	r2, [r4, #40]	@ movhi
	b	.L976
.L973:
	ldr	r1, [r5, #3284]
	str	r3, [sp, #20]
	add	r3, r9, r7
	ldr	r0, .L996+4
	str	r1, [r5, #3272]
	ldrh	r1, [r9, r2, lsl #1]
	ldrh	r2, [r5, #104]
	str	r3, [sp, #16]
	subs	r2, r2, #1
	orr	r2, r2, r1, lsl #10
	str	r2, [r5, #3268]
	movs	r2, #1
	mov	r1, r2
	bl	FlashReadPages
	ldr	r2, [r5, #3264]
	ldr	r3, [sp, #20]
	adds	r2, r2, #1
	beq	.L992
	ldrh	r1, [r6]
	ldrh	r2, [r4, #4]
	cmp	r1, r2
	bne	.L992
	ldrh	r1, [r6, #8]
	movw	r2, #64245
	cmp	r1, r2
	beq	.L981
.L992:
	movs	r7, #0
.L982:
	ldrh	r1, [r3, #104]
	sxth	r2, r7
	cmp	r2, r1
	bge	.L988
	ldr	r1, [sp, #16]
	ldr	r0, .L996+4
	str	r3, [sp, #20]
	ldrh	r1, [r1]
	orr	r2, r2, r1, lsl #10
	str	r2, [r3, #3268]
	movs	r2, #1
	mov	r1, r2
	bl	FlashReadPages
	ldr	r3, [sp, #20]
	ldr	r2, [r3, #3264]
	adds	r2, r2, #1
	beq	.L986
	ldrh	r2, [r6, #8]
	cmp	r10, r2
	bls	.L986
	ldrh	r0, [r6]
	ldrh	r1, [r4, #4]
	cmp	r0, r1
	ittt	eq
	ldreq	r1, [r3, #3268]
	ldreq	r0, [sp, #4]
	streq	r1, [r0, r2, lsl #2]
.L986:
	adds	r7, r7, #1
	b	.L982
.L981:
	movs	r0, #0
	mov	ip, #4
.L983:
	ldrh	r1, [r5, #104]
	sxth	r2, r0
	subs	r1, r1, #1
	cmp	r2, r1
	blt	.L985
.L988:
	add	r8, r8, #1
	b	.L972
.L985:
	ldr	r7, [r5, #3284]
	ldr	r1, [r7, r2, lsl #3]
	uxth	lr, r1
	cmp	r10, lr
	bls	.L984
	add	r2, ip, r2, lsl #3
	ldr	r2, [r7, r2]
	ldr	r7, [sp, #4]
	str	r2, [r7, lr, lsl #2]
.L984:
	adds	r0, r0, #1
	b	.L983
.L997:
	.align	2
.L996:
	.word	.LANCHOR0
	.word	.LANCHOR0+3264
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
	ldr	r0, .L999
	push	{r3, lr}
	.save {r3, lr}
	ldrh	r3, [r0, #120]
	strh	r3, [r0, #3966]	@ movhi
	movw	r3, #61574
	strh	r3, [r0, #3960]	@ movhi
	ldrh	r3, [r0, #144]
	strh	r3, [r0, #3964]	@ movhi
	ldrh	r3, [r0, #122]
	strh	r3, [r0, #3962]	@ movhi
	ldr	r3, [r0, #148]
	addw	r0, r0, #3956
	str	r3, [r0, #12]
	ldr	r3, [r0, #-72]
	str	r3, [r0, #16]
	ldr	r3, [r0, #-76]
	str	r3, [r0, #20]
	ldr	r3, [r0, #-68]
	str	r3, [r0, #24]
	movw	r3, #65535
	strh	r3, [r0, #40]	@ movhi
	bl	FtlMapTblRecovery
	movs	r0, #0
	pop	{r3, pc}
.L1000:
	.align	2
.L999:
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
	ldr	r4, .L1004
	movs	r6, #12
	ldr	r2, [r4, #128]
	ldr	r0, [r4, #3876]
	lsls	r2, r2, #1
	bl	ftl_memset
	ldrh	r3, [r4, #110]
	movs	r1, #255
	ldrh	r2, [r4, #138]
	ldr	r0, [r4, #3900]
	muls	r2, r3, r2
	bl	ftl_memset
	movs	r2, #0
	mov	r3, r4
	movw	r1, #65535
	mov	r7, r2
.L1002:
	ldrh	r4, [r3, #138]
	uxth	r0, r2
	adds	r5, r2, #1
	cmp	r4, r0
	bhi	.L1003
	ldr	r2, [r3, #128]
	strh	r1, [r3, #3910]	@ movhi
	strh	r1, [r3, #3908]	@ movhi
	strh	r2, [r3, #3918]	@ movhi
	movw	r2, #61634
	strh	r2, [r3, #3912]	@ movhi
	ldrh	r2, [r3, #3952]
	strh	r1, [r3, #3948]	@ movhi
	strh	r2, [r3, #3916]	@ movhi
	ldrh	r2, [r3, #136]
	strh	r2, [r3, #3914]	@ movhi
	ldr	r2, [r3, #3872]
	str	r2, [r3, #3920]
	ldr	r2, [r3, #3896]
	str	r2, [r3, #3924]
	ldr	r2, [r3, #3876]
	str	r2, [r3, #3928]
	ldr	r2, [r3, #3892]
	str	r2, [r3, #3932]
	pop	{r3, r4, r5, r6, r7, pc}
.L1003:
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
	ldr	r4, [r3, #3900]
	bic	r2, r2, #3
	add	r2, r2, r4
	str	r2, [r0, #8]
	mov	r2, r5
	b	.L1002
.L1005:
	.align	2
.L1004:
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
	ldr	r0, .L1007
	bl	FtlMapTblRecovery
	movs	r0, #0
	pop	{r3, pc}
.L1008:
	.align	2
.L1007:
	.word	.LANCHOR0+3908
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
	ldr	r4, .L1010
	movw	r3, #65535
	mov	r1, r5
	ldrh	r2, [r4, #120]
	strh	r3, [r4, #4000]	@ movhi
	mov	r3, #-1
	ldr	r0, [r4, #148]
	str	r3, [r4, #4012]
	lsls	r2, r2, #1
	str	r5, [r4, #4004]
	str	r5, [r4, #4008]
	strh	r5, [r4, #144]	@ movhi
	bl	ftl_memset
	ldrh	r2, [r4, #42]
	mov	r1, r5
	ldr	r0, [r4, #2328]
	lsls	r2, r2, #1
	bl	ftl_memset
	ldrh	r2, [r4, #42]
	mov	r1, r5
	ldr	r0, [r4, #3860]
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
.L1011:
	.align	2
.L1010:
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
	ldr	r4, .L1023
	movs	r2, #6
	mov	r9, r5
	mov	r6, r5
	movw	fp, #65535
	.pad #20
	sub	sp, sp, #20
	ldrh	r3, [r4, #42]
	add	r10, r4, #60
	movs	r1, #0
	ldr	r0, [r4, #2316]
	muls	r2, r3, r2
	bl	ftl_memset
	str	r5, [r4, #2340]
	str	r5, [r4, #2320]
	str	r5, [r4, #2332]
	strh	r5, [r4, #2336]	@ movhi
	strh	r5, [r4, #2344]	@ movhi
.L1013:
	ldrh	r3, [r4, #40]
	uxth	r8, r5
	cmp	r8, r3
	bcs	.L1020
	ldrh	r3, [r4, #102]
	ldrh	r2, [r4, #32]
	str	r3, [sp, #4]
	movs	r3, #0
	mov	r7, r3
	b	.L1021
.L1015:
	ldrb	r0, [r10, r3]	@ zero_extendqisi2
	mov	r1, r8
	str	r2, [sp, #12]
	str	r3, [sp, #8]
	bl	V2P_block
	bl	FtlBbmIsBadBlock
	ldr	r3, [sp, #8]
	ldr	r2, [sp, #12]
	cbnz	r0, .L1014
	ldr	r1, [sp, #4]
	add	r7, r7, r1
	uxth	r7, r7
.L1014:
	adds	r3, r3, #1
.L1021:
	uxth	r1, r3
	cmp	r2, r1
	bhi	.L1015
	uxth	r3, r5
	cbz	r7, .L1016
	mov	r1, r7
	str	r3, [sp, #4]
	mov	r0, #32768
	bl	__aeabi_idiv
	ldr	r3, [sp, #4]
	uxth	r7, r0
.L1017:
	ldr	r1, [r4, #2316]
	movs	r2, #6
	mla	r2, r2, r3, r1
	strh	r7, [r2, #4]	@ movhi
	ldrh	r2, [r4, #2348]
	cmp	r2, r8
	beq	.L1018
	ldrh	r2, [r4, #2396]
	cmp	r2, r8
	beq	.L1018
	ldrh	r2, [r4, #2444]
	cmp	r2, r8
	beq	.L1018
	ldr	r2, [r4, #2324]
	ldrh	r3, [r2, r3, lsl #1]
	cbnz	r3, .L1019
	add	r9, r9, #1
	mov	r0, r8
	uxth	r9, r9
	bl	INSERT_FREE_LIST
.L1018:
	adds	r5, r5, #1
	b	.L1013
.L1016:
	ldr	r2, [r4, #2324]
	strh	fp, [r2, r3, lsl #1]	@ movhi
	b	.L1017
.L1019:
	adds	r6, r6, #1
	mov	r0, r8
	uxth	r6, r6
	bl	INSERT_DATA_LIST
	b	.L1018
.L1020:
	strh	r6, [r4, #2336]	@ movhi
	add	r6, r6, r9
	cmp	r6, r3
	strh	r9, [r4, #2344]	@ movhi
	ble	.L1022
	movw	r2, #2206
	ldr	r1, .L1023+4
	ldr	r0, .L1023+8
	bl	sftl_printk
.L1022:
	movs	r0, #0
	add	sp, sp, #20
	@ sp needed
	pop	{r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L1024:
	.align	2
.L1023:
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
	ldr	r4, .L1026
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
.L1027:
	.align	2
.L1026:
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
	ldr	r3, .L1053
	push	{r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.save {r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.pad #28
	sub	sp, sp, #28
	mov	r7, r0
	str	r1, [sp, #8]
	ldrh	r4, [r3, #4]
	movw	r3, #65535
	cmp	r4, r3
	beq	.L1047
	cbnz	r4, .L1029
.L1030:
	bl	FtlGcPageVarInit
	b	.L1031
.L1047:
	movs	r4, #0
.L1029:
	ldr	r3, .L1053+4
	ldr	r2, [sp, #8]
	ldrh	r3, [r3, #102]
	cmp	r3, r2
	beq	.L1030
.L1031:
	mov	fp, #0
	movw	r8, #65535
.L1041:
	ldrh	r3, [r7]
	movs	r0, #0
	strb	r0, [r7, #8]
	cmp	r3, r8
	beq	.L1032
	ldr	r5, .L1053+4
.L1044:
	ldr	r3, [r5, #3836]
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
.L1033:
	uxth	r3, r2
	cmp	ip, r3
	bhi	.L1037
	mov	r9, #0
	movs	r2, #0
	mov	r1, r6
	ldr	r0, [sp, #4]
	bl	FlashReadPages
.L1038:
	uxth	r3, r9
	cmp	r6, r3
	bhi	.L1042
	ldr	r3, [sp, #8]
	add	fp, fp, #1
	adds	r4, r4, #1
	uxth	r4, r4
	cmp	r3, fp
	bls	.L1043
.L1045:
	ldrh	r3, [r5, #102]
	cmp	r3, r4
	bhi	.L1044
	movs	r0, #0
	b	.L1032
.L1037:
	ldrh	r3, [lr], #2
	cmp	r3, r8
	beq	.L1034
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
.L1034:
	adds	r2, r2, #1
	b	.L1033
.L1042:
	movs	r3, #20
	ldr	r2, [r5, #3836]
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
	cbnz	r2, .L1039
	ldr	r3, [sp, #12]
	add	r9, r9, #1
	ldr	r1, [sp, #4]
	ldr	r3, [r3, #12]
	ldrh	r2, [r3]
	cmp	r2, r8
	bne	.L1040
	movs	r3, #1
	str	r3, [r5, #4008]
.L1032:
	ldr	r3, .L1053
	movw	r2, #65535
	strb	r0, [r7, #6]
	mov	r1, r4
	strh	r4, [r7, #2]	@ movhi
	strh	r2, [r3, #4]	@ movhi
	mov	r2, r0
	mov	r0, r7
	bl	ftl_sb_update_avl_pages
	b	.L1046
.L1040:
	ldr	r2, [r3, #8]
	ldr	r0, [r3, #12]
	bl	FtlGcUpdatePage
	b	.L1038
.L1039:
	ldrh	r2, [r7]
	movs	r4, #0
	ldr	r3, [r5, #2324]
	strh	r4, [r3, r2, lsl #1]	@ movhi
	ldrh	r0, [r7]
	bl	INSERT_FREE_LIST
	strh	r8, [r7]	@ movhi
	bl	FtlGcPageVarInit
	b	.L1041
.L1043:
	ldr	r2, .L1053
	ldrh	r3, [r2, #4]
	cmp	r3, r8
	beq	.L1045
	add	r3, r3, fp
	strh	r3, [r2, #4]	@ movhi
	ldrh	r3, [r5, #102]
	cmp	r3, r4
	bls	.L1045
.L1046:
	mov	r0, #-1
	add	sp, sp, #28
	@ sp needed
	pop	{r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L1054:
	.align	2
.L1053:
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
	ldr	r4, .L1065
	movs	r1, #0
	ldr	r0, [r4, #3284]
	add	r6, r4, #176
	ldr	r3, [r4, #3288]
	ldrh	r2, [r4, #110]
	str	r0, [r4, #3272]
	str	r3, [r4, #3276]
	bl	ftl_memset
.L1057:
	ldrh	r3, [r4, #54]
	cmp	r5, r3
	blt	.L1058
	ldr	r6, [r4, #3276]
	movs	r2, #16
	movs	r1, #255
	ldr	r9, .L1065+12
	ldr	r8, .L1065+4
	movs	r5, #0
	mov	r0, r6
	ldr	r10, .L1065+16
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
.L1059:
	ldr	r3, [r4, #3284]
	mov	fp, #0
	ldrh	r1, [r4, #152]
	ldrh	r2, [r4, #154]
	str	r3, [r4, #3272]
	ldr	r3, [r4, #3288]
	str	fp, [r4, #3264]
	str	r3, [r4, #3276]
	orr	r3, r2, r1, lsl #10
	ldrh	r0, [r6, #10]
	str	r3, [r4, #3268]
	ldrh	r3, [r4, #156]
	str	r0, [sp]
	mov	r0, r9
	bl	sftl_printk
	movs	r3, #1
	ldr	r0, .L1065+4
	mov	r2, r3
	mov	r1, r3
	bl	FlashProgPages
	ldrh	r3, [r4, #104]
	ldrh	r2, [r4, #154]
	subs	r3, r3, #1
	cmp	r2, r3
	blt	.L1060
	ldr	r3, [r4, #160]
	ldrh	r2, [r4, #152]
	ldr	r0, [r4, #3804]
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
	str	r3, [r4, #3268]
	str	r3, [r0, #4]
	bl	FlashEraseBlocks
	movs	r3, #1
	mov	r0, r8
	mov	r2, r3
	mov	r1, r3
	bl	FlashProgPages
.L1060:
	ldrh	r3, [r4, #154]
	adds	r3, r3, #1
	strh	r3, [r4, #154]	@ movhi
	ldr	r3, [r4, #3264]
	adds	r3, r3, #1
	bne	.L1061
	adds	r5, r5, #1
	ldr	r1, [r4, #3268]
	uxth	r5, r5
	mov	r0, r10
	bl	sftl_printk
	cmp	r5, #3
	bls	.L1059
	mov	r2, r5
	ldr	r1, [r4, #3268]
	ldr	r0, .L1065+8
	bl	sftl_printk
.L1063:
	b	.L1063
.L1058:
	ldrh	r2, [r4, #3904]
	ldr	r3, [r4, #3272]
	ldr	r1, [r6, #4]!
	mul	r0, r2, r5
	lsls	r2, r2, #2
	adds	r5, r5, #1
	add	r0, r3, r0, lsl #2
	bl	ftl_memcpy
	b	.L1057
.L1064:
	movs	r7, #1
	b	.L1059
.L1061:
	cmp	r7, #0
	beq	.L1064
	movs	r0, #0
	add	sp, sp, #12
	@ sp needed
	pop	{r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L1066:
	.align	2
.L1065:
	.word	.LANCHOR0
	.word	.LANCHOR0+3264
	.word	.LC102
	.word	.LC100
	.word	.LC101
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
	ldr	r4, .L1102
	.pad #20
	sub	sp, sp, #20
	ldr	r9, .L1102+20
.L1068:
	ldr	r3, .L1102
	ldr	r2, .L1102
	ldrh	r3, [r3, #2344]
	ldrh	r2, [r2, #2336]
	add	r3, r3, r2
	ldr	r2, .L1102
	ldrh	r2, [r2, #40]
	cmp	r3, r2
	ble	.L1069
	movw	r2, #2654
	mov	r1, r9
	ldr	r0, .L1102+4
	bl	sftl_printk
.L1069:
	ldr	r3, .L1102+8
	cmp	r5, r3
	bne	.L1094
	ldrh	r3, [r4, #2344]
	ldr	r1, [r4, #3160]
	mul	r2, r1, r3
	lsrs	r1, r3, #1
	adds	r1, r1, #1
	add	r1, r1, r2, lsr #2
	uxth	r1, r1
	cbz	r1, .L1070
	subs	r1, r1, #1
	uxth	r1, r1
.L1070:
	ldr	r0, .L1102+12
	bl	List_pop_index_node
	ldrh	r3, [r4, #2344]
	mov	r6, r0
	uxth	r8, r0
	cbnz	r3, .L1071
	movw	r2, #2663
	mov	r1, r9
	ldr	r0, .L1102+4
	bl	sftl_printk
.L1071:
	ldrh	r3, [r4, #2344]
	mov	r0, r5
	subs	r3, r3, #1
	strh	r3, [r4, #2344]	@ movhi
	strh	r8, [r5]	@ movhi
	bl	make_superblock
	ldrb	r3, [r5, #7]	@ zero_extendqisi2
	cbnz	r3, .L1072
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
	ble	.L1068
	movw	r2, #2674
	mov	r1, r9
	ldr	r0, .L1102+4
	bl	sftl_printk
	b	.L1068
.L1094:
	movs	r1, #0
	b	.L1070
.L1072:
	ldrh	r2, [r4, #2336]
	ldrh	r3, [r4, #2344]
	add	r3, r3, r2
	ldrh	r2, [r4, #40]
	cmp	r3, r2
	ble	.L1074
	movw	r2, #2677
	mov	r1, r9
	ldr	r0, .L1102+4
	bl	sftl_printk
.L1074:
	ldr	r1, [r4, #3804]
	mov	r10, #20
	ldrh	r2, [r4, #32]
	add	fp, r5, #16
	movs	r7, #0
	mov	r0, fp
	mov	r3, r1
	mov	ip, r7
	mla	r2, r10, r2, r1
	str	r2, [sp, #4]
.L1075:
	ldr	r2, [sp, #4]
	cmp	r2, r3
	bne	.L1077
	cbnz	r7, .L1078
	mov	r2, #2688
	mov	r1, r9
	ldr	r0, .L1102+4
	bl	sftl_printk
.L1078:
	ldrh	r3, [r4, #2588]
	cmp	r3, r8
	bne	.L1079
	movw	r2, #2690
	mov	r1, r9
	ldr	r0, .L1102+4
	bl	sftl_printk
.L1079:
	ldrb	r3, [r5, #8]	@ zero_extendqisi2
	uxth	r6, r6
	ldr	r2, [r4, #2328]
	cmp	r3, #0
	bne	.L1080
	ldrh	r3, [r2, r6, lsl #1]
	cmp	r3, #0
	beq	.L1081
	ldrh	r1, [r4, #92]
	add	r3, r3, r1
.L1101:
	strh	r3, [r2, r6, lsl #1]	@ movhi
	movs	r1, #0
	ldr	r3, [r4, #2548]
	mov	r0, r8
	adds	r3, r3, #1
	str	r3, [r4, #2548]
	bl	ftl_set_blk_mode
.L1083:
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
	ldr	r2, [r4, #3864]
	ldr	r1, [r4, #3804]
	str	r0, [r4, #2556]
	ldr	r3, [r2, #16]
	ldr	r0, .L1102+16
	adds	r3, r3, #1
	str	r3, [r2, #16]
	movs	r2, #20
	mla	r2, r2, r7, r1
	adds	r3, r1, #4
	adds	r2, r2, #24
.L1085:
	adds	r3, r3, #20
	cmp	r2, r3
	bne	.L1086
	ldrb	r1, [r5, #8]	@ zero_extendqisi2
	mov	r2, r7
	ldr	r0, [r4, #3804]
	mov	r10, #0
	bl	FlashEraseBlocks
	mov	r3, r10
	movs	r1, #20
.L1087:
	uxth	r2, r10
	cmp	r7, r2
	bhi	.L1089
	cmp	r3, #0
	ble	.L1090
	mov	r0, r8
	bl	update_multiplier_value
	bl	FtlBbmTblFlush
.L1090:
	ldrb	r2, [r5, #7]	@ zero_extendqisi2
	cmp	r2, #0
	bne	.L1091
	ldr	r3, [r4, #2324]
	movw	r2, #65535
	strh	r2, [r3, r6, lsl #1]	@ movhi
	b	.L1068
.L1077:
	str	ip, [r3, #8]
	movw	lr, #65535
	str	ip, [r3, #12]
	ldrh	r2, [r0], #2
	cmp	r2, lr
	beq	.L1076
	mla	lr, r10, r7, r1
	adds	r7, r7, #1
	lsls	r2, r2, #10
	uxth	r7, r7
	str	r2, [lr, #4]
.L1076:
	adds	r3, r3, #20
	b	.L1075
.L1081:
	movs	r3, #2
	b	.L1101
.L1080:
	ldrh	r3, [r2, r6, lsl #1]
	mov	r0, r8
	adds	r3, r3, #1
	strh	r3, [r2, r6, lsl #1]	@ movhi
	ldr	r3, [r4, #2552]
	adds	r3, r3, #1
	str	r3, [r4, #2552]
	bl	ftl_set_blk_mode.part.6
	b	.L1083
.L1086:
	ldr	r1, [r3, #-20]
	ands	r1, r1, r0
	str	r1, [r3, #-20]
	b	.L1085
.L1089:
	mul	r2, r1, r10
	ldr	r0, [r4, #3804]
	add	ip, r0, r2
	ldr	r2, [r0, r2]
	adds	r0, r2, #1
	bne	.L1088
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
.L1088:
	add	r10, r10, #1
	add	fp, fp, #2
	b	.L1087
.L1091:
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
	cbz	r3, .L1092
	ldrb	r3, [r5, #7]	@ zero_extendqisi2
	cbnz	r3, .L1093
.L1092:
	movw	r2, #2743
	ldr	r1, .L1102+20
	ldr	r0, .L1102+4
	bl	sftl_printk
.L1093:
	movs	r0, #0
	add	sp, sp, #20
	@ sp needed
	pop	{r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L1103:
	.align	2
.L1102:
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
	ldr	r4, .L1116
	ldrh	r3, [r4, #3210]
	cbz	r3, .L1105
	movs	r7, #0
	addw	r10, r4, #3212
.L1106:
	ldrh	r2, [r4, #32]
	uxth	r3, r7
	cmp	r2, r3
	bhi	.L1112
	bl	FtlGcReFreshBadBlk
.L1105:
	movs	r0, #0
	pop	{r3, r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L1112:
	uxtah	r3, r4, r7
	mov	r1, r8
	mov	fp, #0
	ldrb	r0, [r3, #60]	@ zero_extendqisi2
	bl	V2P_block
	mov	r9, r0
.L1107:
	ldrh	r3, [r4, #3210]
	uxth	r5, fp
	cmp	r3, r5
	bhi	.L1111
	adds	r7, r7, #1
	b	.L1106
.L1111:
	uxth	r6, fp
	add	r3, r4, r6, lsl #1
	ldrh	r3, [r3, #3212]
	cmp	r3, r9
	bne	.L1108
	mov	r1, r9
	ldr	r0, .L1116+4
	bl	sftl_printk
	mov	r0, r9
	bl	FtlBbmMapBadBlock
	bl	FtlBbmTblFlush
	ldrh	r1, [r4, #3210]
	add	r3, r10, r6, lsl #1
.L1109:
	cmp	r5, r1
	bcc	.L1110
	subs	r1, r1, #1
	strh	r1, [r4, #3210]	@ movhi
.L1108:
	add	fp, fp, #1
	b	.L1107
.L1110:
	ldrh	r0, [r3, #2]!
	adds	r5, r5, #1
	uxth	r5, r5
	strh	r0, [r3, #-2]	@ movhi
	b	.L1109
.L1117:
	.align	2
.L1116:
	.word	.LANCHOR0
	.word	.LC103
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
	ldr	r2, .L1128
	push	{r3, r4, r5, lr}
	.save {r3, r4, r5, lr}
	mov	r5, r0
	mov	r4, r2
	ldr	r3, [r2, #2324]
	ldrh	r3, [r3, r0, lsl #1]
	cmp	r3, #0
	bne	.L1119
	ldrh	r1, [r2, #2588]
	cmp	r1, r0
	bne	.L1120
	movw	r3, #65535
	strh	r3, [r2, #2588]	@ movhi
.L1121:
	mov	r1, r5
	ldr	r0, .L1128+4
	bl	List_remove_node
	ldrh	r3, [r4, #2336]
	cbnz	r3, .L1123
	movw	r2, #2813
	ldr	r1, .L1128+8
	ldr	r0, .L1128+12
	bl	sftl_printk
.L1123:
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
	ble	.L1127
	mov	r2, #2816
	ldr	r1, .L1128+8
	ldr	r0, .L1128+12
	bl	sftl_printk
.L1127:
	movs	r3, #1
	b	.L1118
.L1120:
	ldrh	r1, [r2, #2348]
	cmp	r1, r0
	beq	.L1118
	ldrh	r1, [r2, #2396]
	cmp	r1, r0
	beq	.L1118
	ldrh	r2, [r2, #2444]
	cmp	r2, r0
	bne	.L1121
.L1118:
	mov	r0, r3
	pop	{r3, r4, r5, pc}
.L1119:
	bl	List_update_data_list
	movs	r3, #0
	b	.L1118
.L1129:
	.align	2
.L1128:
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
	ldr	r5, .L1142
	beq	.L1131
	ldr	r3, [r5, #2324]
	ldrh	r2, [r3, r0, lsl #1]
	cmp	r2, #0
	bne	.L1132
	mov	r1, r0
	ldr	r0, .L1142+4
	bl	sftl_printk
	ldr	r3, [r5, #2324]
	ldrh	r3, [r3, r4, lsl #1]
	cbnz	r3, .L1133
	movw	r2, #2831
	ldr	r1, .L1142+8
	ldr	r0, .L1142+12
	bl	sftl_printk
.L1133:
	ldr	r3, [r5, #2324]
	movs	r2, #16
	mov	r1, r4
	ldr	r0, .L1142+16
	strh	r2, [r3, r4, lsl #1]	@ movhi
	bl	test_node_in_list
	cbz	r0, .L1134
	mov	r1, r4
	ldr	r0, .L1142+16
	bl	List_remove_node
	ldrh	r3, [r5, #2344]
	cbnz	r3, .L1135
	movw	r2, #2835
	ldr	r1, .L1142+8
	ldr	r0, .L1142+12
	bl	sftl_printk
.L1135:
	ldrh	r3, [r5, #2344]
	mov	r0, r4
	subs	r3, r3, #1
	strh	r3, [r5, #2344]	@ movhi
	bl	INSERT_DATA_LIST
	ldr	r3, [r5, #2324]
	mov	r1, r4
	ldr	r0, .L1142+20
	ldrh	r2, [r3, r4, lsl #1]
	bl	sftl_printk
.L1134:
	mov	r0, r4
	bl	FtlGcRefreshBlock
.L1138:
	movs	r6, #0
	b	.L1130
.L1132:
	subs	r2, r2, #1
	strh	r2, [r3, r0, lsl #1]	@ movhi
.L1131:
	ldrh	r0, [r5, #4000]
	movw	r3, #65535
	cmp	r0, r3
	bne	.L1137
	strh	r4, [r5, #4000]	@ movhi
	b	.L1138
.L1137:
	cmp	r4, r0
	beq	.L1138
	bl	update_vpc_list
	ldr	r2, [r5, #2316]
	adds	r6, r0, #0
	ldr	r3, [r5, #2320]
	it	ne
	movne	r6, #1
	strh	r4, [r5, #4000]	@ movhi
	subs	r3, r3, r2
	asrs	r2, r3, #1
	ldr	r3, .L1142+24
	muls	r3, r2, r3
	ldr	r2, [r5, #2324]
	uxth	r1, r3
	ldrh	r2, [r2, r1, lsl #1]
	cbnz	r2, .L1130
	cmp	r4, r1
	beq	.L1130
	movw	r2, #2856
	ldr	r1, .L1142+8
	ldr	r0, .L1142+12
	bl	sftl_printk
.L1130:
	mov	r0, r6
	pop	{r4, r5, r6, pc}
.L1143:
	.align	2
.L1142:
	.word	.LANCHOR0
	.word	.LC104
	.word	.LANCHOR1+401
	.word	.LC1
	.word	.LANCHOR0+2340
	.word	.LC105
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
	ldr	r4, .L1162
	ldrh	r2, [r4, #2352]
	cmp	r2, #0
	beq	.L1145
	ldrb	r3, [r4, #2356]	@ zero_extendqisi2
	cmp	r3, #0
	bne	.L1145
	ldrb	r1, [r4, #2355]	@ zero_extendqisi2
	ldrh	r3, [r4, #102]
	muls	r3, r1, r3
	cmp	r2, r3
	beq	.L1145
	ldrb	r7, [r4, #2358]	@ zero_extendqisi2
	cbnz	r7, .L1144
	ldr	r6, [r4, #2504]
	mov	r2, r7
	mov	r1, sp
	ldrh	r8, [r4, #32]
	subs	r6, r6, #1
	mov	r0, r6
	bl	log2phys
	ldr	r3, [sp]
	ldr	r5, [r4, #3288]
	ldr	r0, [r4, #3284]
	str	r3, [sp, #8]
	adds	r3, r3, #1
	str	r6, [sp, #20]
	str	r0, [sp, #12]
	str	r5, [sp, #16]
	str	r7, [r5, #4]
	beq	.L1147
	mov	r2, r7
	movs	r1, #1
	add	r0, sp, #4
	bl	FlashReadPages
.L1148:
	ldr	r9, .L1162+4
	movs	r7, #0
	lsl	r8, r8, #2
	mov	r10, r7
	movw	r3, #61589
	strh	r3, [r5]	@ movhi
.L1149:
	cmp	r8, r7
	bne	.L1153
.L1150:
	movs	r3, #1
.L1161:
	strb	r3, [r4, #2358]
.L1144:
	add	sp, sp, #24
	@ sp needed
	pop	{r4, r5, r6, r7, r8, r9, r10, pc}
.L1147:
	ldrh	r2, [r4, #110]
	movs	r1, #255
	bl	ftl_memset
	b	.L1148
.L1153:
	ldrh	r3, [r4, #2352]
	cmp	r3, #0
	beq	.L1150
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
	b	.L1149
.L1145:
	movs	r3, #0
	b	.L1161
.L1163:
	.align	2
.L1162:
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
	ldr	r5, .L1168
	movs	r6, #12
	bl	FtlWriteDump_data
.L1165:
	ldrh	r3, [r5, #138]
	uxth	r0, r4
	cmp	r3, r0
	bhi	.L1167
	movs	r0, #0
	pop	{r4, r5, r6, pc}
.L1167:
	ldr	r2, [r5, #2492]
	uxth	r3, r4
	mla	r3, r6, r3, r2
	ldr	r3, [r3, #4]
	cmp	r3, #0
	bge	.L1166
	bl	flush_l2p_region
.L1166:
	adds	r4, r4, #1
	b	.L1165
.L1169:
	.align	2
.L1168:
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
	beq	.L1295
	ldrh	r3, [r0, #2]
	ldr	r4, .L1302
	str	r3, [sp, #8]
	ldrb	r3, [r0, #6]	@ zero_extendqisi2
	ldr	r1, [sp, #8]
	str	r3, [sp, #24]
	ldrh	r3, [r4, #102]
	cmp	r3, r1
	mov	r3, #0
	bne	.L1173
	strh	r3, [r0, #4]	@ movhi
.L1300:
	strb	r3, [fp, #6]
.L1295:
	movs	r0, #0
	add	sp, sp, #52
	@ sp needed
	pop	{r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L1173:
	ldrh	r0, [r0, #16]
.L1174:
	cmp	r0, r2
	uxth	r5, r3
	add	r3, r3, #1
	beq	.L1175
	movs	r1, #1
	bl	FtlGetLastWrittenPage
	adds	r7, r0, #1
	mov	r6, r0
	beq	.L1176
	ldr	r3, [r4, #3172]
	movs	r2, #0
	ldrh	lr, [r4, #32]
	movw	r9, #65535
	ldr	r0, [r4, #3836]
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
.L1177:
	uxth	r3, r2
	cmp	lr, r3
	bhi	.L1183
	ldr	r5, .L1302
	movs	r2, #0
	mov	r1, r4
	movw	r9, #65535
	bl	FlashReadPages
	ldr	r3, [r5, #2544]
	uxth	r2, r6
	ldr	r8, [r5, #3836]
	str	r2, [sp, #12]
	subs	r3, r3, #1
	str	r3, [sp, #4]
	mov	r7, r8
	movs	r3, #0
.L1184:
	uxth	r2, r3
	cmp	r4, r2
	bhi	.L1189
	bne	.L1187
	adds	r3, r6, #1
	uxth	r3, r3
	str	r3, [sp, #16]
.L1296:
	ldr	r0, [r8, #4]
	ubfx	r0, r0, #10, #16
	bl	P2V_plane
	ldrh	r3, [r5, #102]
	ldr	r2, [sp, #16]
	str	r0, [sp, #12]
	cmp	r3, r2
	bne	.L1191
	ldrh	r3, [sp, #16]
	strh	r3, [fp, #2]	@ movhi
	movs	r3, #0
	strb	r3, [fp, #6]
	strh	r3, [fp, #4]	@ movhi
.L1191:
	ldr	r3, [sp, #16]
	ldr	r2, [sp, #8]
	cmp	r3, r2
	bne	.L1192
	ldr	r3, [sp, #12]
	ldr	r2, [sp, #24]
	cmp	r3, r2
	bne	.L1192
	ldr	r1, [sp, #16]
	mov	r2, r3
.L1301:
	mov	r0, fp
	bl	ftl_sb_update_avl_pages
	b	.L1295
.L1175:
	uxth	r1, r3
	adds	r1, r1, #8
	ldrh	r0, [fp, r1, lsl #1]
	b	.L1174
.L1176:
	ldr	r3, [sp, #8]
	cbz	r3, .L1178
	movw	r2, #1804
	ldr	r1, .L1302+4
	ldr	r0, .L1302+8
	bl	sftl_printk
.L1178:
	ldr	r3, [sp, #24]
	cbz	r3, .L1179
	cmp	r5, r3
	beq	.L1179
	movw	r2, #1805
	ldr	r1, .L1302+4
	ldr	r0, .L1302+8
	bl	sftl_printk
.L1179:
	movs	r3, #0
	strh	r3, [fp, #2]	@ movhi
	b	.L1300
.L1183:
	ldrh	r3, [ip], #2
	cmp	r3, r9
	beq	.L1180
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
.L1180:
	adds	r2, r2, #1
	b	.L1177
.L1189:
	ldr	r2, [r7]
	cbnz	r2, .L1185
	ldr	r10, [r7, #12]
	ldr	r2, [r10, #4]
	adds	r0, r2, #1
	beq	.L1186
	ldr	r1, [r5, #2544]
	mov	r0, r2
	bl	ftl_cmp_data_ver
	cbz	r0, .L1186
	adds	r2, r2, #1
	str	r2, [r5, #2544]
.L1186:
	ldr	r2, [r10]
	adds	r2, r2, #1
	bne	.L1188
.L1187:
	uxth	r2, r6
	uxth	r3, r3
	str	r2, [sp, #16]
	movs	r2, #20
	mla	r8, r2, r3, r8
	b	.L1296
.L1185:
	ldr	r9, [sp, #12]
.L1188:
	adds	r3, r3, #1
	adds	r7, r7, #20
	b	.L1184
.L1192:
	movw	r3, #65535
	cmp	r9, r3
	bne	.L1193
	ldrb	r3, [fp, #8]	@ zero_extendqisi2
	cmp	r3, #0
	bne	.L1194
.L1193:
	ldr	r3, [r5, #4012]
	uxth	r8, r6
	uxth	r6, r6
	ldr	r7, .L1302
	movw	r9, #65535
	adds	r3, r3, #1
	itt	eq
	ldreq	r3, [sp, #4]
	streq	r3, [r5, #4012]
	ldr	r3, [sp, #8]
	adds	r3, r3, #7
	cmp	r6, r3
	itet	gt
	subgt	r4, r8, #7
	ldrle	r4, [sp, #8]
	uxthgt	r4, r4
.L1197:
	cmp	r4, r8
	bhi	.L1204
	movs	r3, #0
	ldrh	lr, [r7, #32]
	ldr	r0, [r7, #3836]
	mov	r6, r3
	ldr	r1, [sp, #20]
	mov	ip, #20
	b	.L1205
.L1199:
	ldrh	r2, [r1], #2
	cmp	r2, r9
	beq	.L1198
	mla	r10, ip, r6, r0
	adds	r6, r6, #1
	orr	r2, r4, r2, lsl #10
	uxth	r6, r6
	str	r2, [r10, #4]
.L1198:
	adds	r3, r3, #1
.L1205:
	uxth	r2, r3
	cmp	r2, lr
	bcc	.L1199
	movs	r2, #0
	mov	r1, r6
	bl	FlashReadPages
	ldr	r3, [r7, #3836]
	movs	r2, #20
	mla	r6, r2, r6, r3
.L1200:
	cmp	r6, r3
	bne	.L1203
	adds	r4, r4, #1
	uxth	r4, r4
	b	.L1197
.L1203:
	ldr	r2, [r3]
	cbnz	r2, .L1194
	ldr	r2, [r3, #12]
	ldrh	r1, [r2]
	cmp	r1, r9
	beq	.L1202
	ldr	r2, [r2, #4]
	adds	r1, r2, #1
	it	ne
	strne	r2, [r7, #4012]
.L1202:
	adds	r3, r3, #20
	b	.L1200
.L1204:
	mov	r3, #-1
	str	r3, [r7, #4012]
.L1194:
	movs	r3, #1
	ldr	r0, .L1302+12
	strh	r3, [r5, #4016]	@ movhi
	ldr	r4, .L1302
	bl	FtlMapBlkWriteDump_data
	ldr	r5, [sp, #8]
.L1206:
	ldrh	r6, [r4, #32]
	movs	r3, #0
	ldr	r0, [r4, #3836]
	mov	r10, r3
	ldr	r1, [sp, #20]
	movw	r7, #65535
	mov	ip, #20
.L1207:
	uxth	r2, r3
	cmp	r6, r2
	bhi	.L1209
	movs	r2, #0
	mov	r1, r10
	bl	FlashReadPages
	movs	r3, #0
.L1299:
	str	r3, [sp, #28]
	ldrh	r3, [sp, #28]
	cmp	r10, r3
	bhi	.L1239
	adds	r5, r5, #1
	ldrh	r3, [r4, #102]
	uxth	r5, r5
	cmp	r3, r5
	bne	.L1206
	ldrh	r2, [r4, #32]
	movw	r0, #65535
	movs	r3, #0
	strh	r5, [fp, #2]	@ movhi
	strh	r3, [fp, #4]	@ movhi
.L1240:
	uxth	r1, r3
	cmp	r1, r2
	bcs	.L1295
	ldr	r1, [sp, #20]
	ldrh	r4, [r1], #2
	cmp	r4, r0
	str	r1, [sp, #20]
	add	r1, r3, #1
	bne	.L1300
	mov	r3, r1
	b	.L1240
.L1209:
	ldrh	r2, [r1], #2
	cmp	r2, r7
	beq	.L1208
	mla	lr, ip, r10, r0
	add	r10, r10, #1
	orr	r2, r5, r2, lsl #10
	uxth	r10, r10
	str	r2, [lr, #4]
.L1208:
	adds	r3, r3, #1
	b	.L1207
.L1303:
	.align	2
.L1302:
	.word	.LANCHOR0
	.word	.LANCHOR1+421
	.word	.LC1
	.word	.LANCHOR0+3908
.L1239:
	ldr	r3, [sp, #28]
	movs	r7, #20
	ldr	r9, [r4, #3836]
	muls	r7, r3, r7
	add	r8, r9, r7
	ldr	r6, [r8, #4]
	ubfx	r0, r6, #10, #16
	str	r6, [sp, #44]
	bl	P2V_plane
	ldr	r3, [sp, #8]
	cmp	r5, r3
	bcc	.L1211
	bne	.L1212
	ldr	r3, [sp, #24]
	cmp	r3, r0
	bhi	.L1211
.L1212:
	ldr	r3, [sp, #16]
	cmp	r5, r3
	bne	.L1213
	ldr	r3, [sp, #12]
	cmp	r3, r0
	beq	.L1214
.L1213:
	ldr	r3, [r9, r7]
	adds	r3, r3, #1
	beq	.L1215
	ldr	r8, [r8, #12]
	movw	r3, #61589
	ldrh	r2, [r8]
	cmp	r2, r3
	beq	.L1216
.L1223:
	ldrh	r0, [fp]
.L1298:
	bl	decrement_vpc_count
.L1211:
	ldr	r3, [sp, #28]
	adds	r3, r3, #1
	b	.L1299
.L1216:
	ldr	r3, [r8, #4]
	adds	r6, r3, #1
	str	r3, [sp, #4]
	beq	.L1217
	ldr	r1, [r4, #2544]
	mov	r0, r3
	bl	ftl_cmp_data_ver
	cbz	r0, .L1217
	ldr	r3, [sp, #4]
	adds	r3, r3, #1
	str	r3, [r4, #2544]
.L1217:
	ldrh	r2, [r8]
	movw	r3, #61589
	cmp	r2, r3
	beq	.L1218
	movw	r2, #1954
	ldr	r1, .L1304
	ldr	r0, .L1304+4
	bl	sftl_printk
.L1218:
	ldr	r6, [r8, #8]
	add	r1, sp, #40
	ldr	r3, [r8, #12]
	movs	r2, #0
	mov	r0, r6
	str	r3, [sp, #36]
	bl	log2phys
	ldr	r1, [r4, #4012]
	adds	r0, r1, #1
	beq	.L1219
	ldr	r0, [sp, #4]
	bl	ftl_cmp_data_ver
	cmp	r0, #0
	beq	.L1219
	ldr	r3, [sp, #36]
	adds	r2, r3, #1
	beq	.L1220
	ldr	r0, [r4, #3836]
	movs	r2, #0
	movs	r1, #1
	add	r0, r0, r7
	str	r3, [r0, #4]
	ldr	r8, [r0, #12]
	bl	FlashReadPages
	ldr	r2, [r4, #3836]
	ldr	r1, [r2, r7]
	adds	r3, r2, r7
	adds	r1, r1, #1
	bne	.L1221
.L1222:
	mov	r3, #-1
	ldrh	r0, [fp]
	str	r3, [sp, #36]
	bl	decrement_vpc_count
.L1230:
	ldr	r0, [sp, #36]
	adds	r1, r0, #1
	beq	.L1211
.L1243:
	ubfx	r0, r0, #10, #16
	bl	P2V_block_in_plane
	ldrh	r3, [r4, #40]
	mov	r6, r0
	cmp	r3, r0
	bhi	.L1235
	movw	r2, #2055
	ldr	r1, .L1304
	ldr	r0, .L1304+4
	bl	sftl_printk
.L1235:
	ldr	r3, [r4, #2324]
	ldrh	r3, [r3, r6, lsl #1]
	cmp	r3, #0
	beq	.L1236
	mov	r0, r6
	b	.L1298
.L1220:
	ldr	r3, [sp, #44]
	ldr	r2, [sp, #40]
	cmp	r2, r3
	bne	.L1223
	movs	r2, #1
	add	r1, sp, #36
	mov	r0, r6
	bl	log2phys
	b	.L1223
.L1221:
	ldr	r1, [r8, #8]
	cmp	r6, r1
	bne	.L1222
	ldr	r9, [r8, #4]
	ldr	r0, [r4, #4012]
	mov	r1, r9
	bl	ftl_cmp_data_ver
	cmp	r0, #0
	beq	.L1222
	ldr	r1, [sp, #40]
	ldr	r0, [sp, #44]
	cmp	r1, r0
	bne	.L1225
.L1228:
	ldr	r1, [sp, #36]
	mov	r0, r6
	bl	FtlReUsePrevPpa
	b	.L1222
.L1225:
	ldr	r0, [sp, #36]
	cmp	r1, r0
	beq	.L1222
	adds	r0, r1, #1
	beq	.L1226
	str	r1, [r3, #4]
	movs	r2, #0
	movs	r1, #1
	mov	r0, r3
	ldr	r8, [r3, #12]
	bl	FlashReadPages
.L1227:
	ldr	r3, [r4, #3836]
	ldr	r3, [r3, r7]
	adds	r3, r3, #1
	beq	.L1228
	ldr	r3, [r8, #4]
	ldr	r0, [r4, #4012]
	mov	r1, r3
	bl	ftl_cmp_data_ver
	cmp	r0, #0
	beq	.L1228
	mov	r1, r3
	mov	r0, r9
	bl	ftl_cmp_data_ver
	cmp	r0, #0
	beq	.L1222
	b	.L1228
.L1226:
	str	r1, [r2, r7]
	b	.L1227
.L1219:
	ldr	r3, [sp, #44]
	ldr	r2, [sp, #40]
	cmp	r2, r3
	beq	.L1230
	movs	r2, #1
	add	r1, sp, #44
	mov	r0, r6
	bl	log2phys
	ldr	r8, [sp, #40]
	cmp	r8, #-1
	beq	.L1230
	ldr	r3, [sp, #36]
	cmp	r8, r3
	beq	.L1233
	ubfx	r0, r8, #10, #16
	bl	P2V_block_in_plane
	ldrh	r3, [r4, #2348]
	cmp	r3, r0
	beq	.L1234
	ldrh	r3, [r4, #2396]
	cmp	r3, r0
	beq	.L1234
	ldrh	r3, [r4, #2444]
	cmp	r3, r0
	bne	.L1230
.L1234:
	ldr	r0, [r4, #3836]
	movs	r2, #0
	movs	r1, #1
	str	r8, [r0, #4]
	ldr	r7, [r0, #12]
	bl	FlashReadPages
	ldr	r3, [r4, #3836]
	ldr	r3, [r3]
	adds	r3, r3, #1
	beq	.L1230
	ldr	r1, [r7, #4]
	ldr	r0, [sp, #4]
	bl	ftl_cmp_data_ver
	cmp	r0, #0
	bne	.L1230
	movs	r2, #1
	add	r1, sp, #40
	mov	r0, r6
	bl	log2phys
	b	.L1230
.L1236:
	mov	r1, r6
	ldr	r0, .L1304+8
	bl	sftl_printk
	b	.L1211
.L1215:
	ldr	r3, [r4, #4020]
	cmp	r3, #31
	itttt	ls
	addls	r2, r4, r3, lsl #2
	addls	r3, r3, #1
	strls	r3, [r4, #4020]
	strls	r6, [r2, #4024]
	ldrh	r0, [fp]
	bl	decrement_vpc_count
	ldr	r3, [r4, #4012]
	adds	r2, r3, #1
	bne	.L1238
	ldr	r3, [sp, #4]
.L1297:
	str	r3, [r4, #4012]
	b	.L1211
.L1238:
	ldr	r2, [sp, #4]
	cmp	r2, r3
	bcs	.L1211
	mov	r3, r2
	b	.L1297
.L1214:
	ldrb	r3, [sp, #12]	@ zero_extendqisi2
	mov	r1, r5
	strh	r5, [fp, #2]	@ movhi
	ldr	r2, [sp, #12]
	strb	r3, [fp, #6]
	b	.L1301
.L1233:
	mov	r0, r8
	b	.L1243
.L1305:
	.align	2
.L1304:
	.word	.LANCHOR1+421
	.word	.LC1
	.word	.LC106
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
	ldr	r4, .L1317
	mov	r3, #-1
	mov	r8, #0
	movw	r1, #61589
	str	r3, [sp, #20]
	mov	r5, r0
	ldr	r2, [r4, #3284]
	movs	r7, #7
	ldr	r6, [r4, #3288]
	str	r2, [sp, #12]
	mvn	r2, #2
	str	r6, [sp, #16]
	str	r2, [r6, #8]
	mvn	r2, #1
	str	r2, [r6, #12]
	ldrh	r2, [r0]
	strh	r8, [r6]	@ movhi
	strh	r2, [r6, #2]	@ movhi
	ldr	r2, [r4, #3284]
	str	r1, [r2]
	add	r1, r1, #304087040
	ldr	r2, [r4, #3284]
	add	r1, r1, #1269760
	addw	r1, r1, #1507
	str	r1, [r2, #4]
.L1307:
	subs	r7, r7, #1
	beq	.L1309
	ldrh	r3, [r5, #4]
	cbnz	r3, .L1308
.L1309:
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
.L1308:
	mov	r0, r5
	bl	get_new_active_ppa
	str	r0, [sp, #8]
	adds	r0, r0, #1
	beq	.L1309
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
	b	.L1307
.L1318:
	.align	2
.L1317:
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
	ldr	r4, .L1348
	ldr	r3, [r4, #3284]
	add	r7, r4, #3264
	ldr	r6, [r4, #3288]
	str	r3, [r4, #3272]
	str	r6, [r4, #3276]
	bl	FtlBbtMemInit
	ldrh	r5, [r4, #98]
	subs	r5, r5, #1
	uxth	r5, r5
.L1320:
	ldrh	r3, [r4, #98]
	subs	r3, r3, #16
	cmp	r5, r3
	ble	.L1323
	lsls	r3, r5, #10
	movs	r2, #1
	mov	r1, r2
	mov	r0, r7
	str	r3, [r4, #3268]
	bl	FlashReadPages
	ldr	r3, [r4, #3264]
	adds	r3, r3, #1
	bne	.L1321
	ldr	r3, [r4, #3268]
	movs	r2, #1
	mov	r1, r2
	mov	r0, r7
	adds	r3, r3, #1
	str	r3, [r4, #3268]
	bl	FlashReadPages
.L1321:
	ldr	r3, [r4, #3264]
	adds	r3, r3, #1
	beq	.L1322
	ldrh	r3, [r6]
	cmp	r3, r8
	bne	.L1322
	ldr	r3, [r6, #4]
	strh	r5, [r4, #152]	@ movhi
	str	r3, [r4, #160]
	ldrh	r3, [r6, #8]
	strh	r3, [r4, #156]	@ movhi
.L1323:
	ldrh	r3, [r4, #152]
	movw	r2, #65535
	cmp	r3, r2
	beq	.L1337
	ldrh	r3, [r4, #156]
	cmp	r3, r2
	beq	.L1327
	lsls	r3, r3, #10
	movs	r2, #1
	mov	r1, r2
	ldr	r0, .L1348+4
	str	r3, [r4, #3268]
	bl	FlashReadPages
	ldr	r3, [r4, #3264]
	adds	r3, r3, #1
	beq	.L1327
	ldrh	r2, [r6]
	movw	r3, #61649
	cmp	r2, r3
	bne	.L1327
	ldr	r3, [r6, #4]
	ldr	r2, [r4, #160]
	cmp	r3, r2
	bls	.L1327
	ldrh	r2, [r4, #156]
	str	r3, [r4, #160]
	ldrh	r3, [r6, #8]
	strh	r2, [r4, #152]	@ movhi
	strh	r3, [r4, #156]	@ movhi
.L1327:
	ldr	r7, .L1348+4
	movs	r1, #1
	ldrh	r0, [r4, #152]
	movw	r8, #61649
	bl	FtlGetLastWrittenPage
	sxth	r5, r0
	adds	r0, r0, #1
	strh	r0, [r4, #154]	@ movhi
.L1329:
	cmp	r5, #0
	bge	.L1332
	movs	r2, #254
	ldr	r1, .L1348+8
	ldr	r0, .L1348+12
	bl	sftl_printk
.L1331:
	ldrh	r3, [r6, #10]
	ldrh	r0, [r6, #12]
	strh	r3, [r4, #158]	@ movhi
	movw	r3, #65535
	cmp	r0, r3
	beq	.L1334
	ldr	r2, [r4, #28]
	cmp	r0, r2
	beq	.L1334
	ldrh	r3, [r4, #42]
	lsrs	r3, r3, #2
	cmp	r2, r3
	bcs	.L1334
	cmp	r0, r3
	bcs	.L1334
	bl	FtlSysBlkNumInit
.L1334:
	ldr	r6, .L1348+16
	movs	r5, #0
.L1335:
	ldrh	r3, [r4, #54]
	cmp	r5, r3
	bcc	.L1336
	movs	r0, #0
	pop	{r4, r5, r6, r7, r8, pc}
.L1322:
	subs	r5, r5, #1
	uxth	r5, r5
	b	.L1320
.L1332:
	ldrh	r3, [r4, #152]
	movs	r2, #1
	mov	r1, r2
	mov	r0, r7
	orr	r3, r5, r3, lsl #10
	str	r3, [r4, #3268]
	ldr	r3, [r4, #3284]
	str	r3, [r4, #3272]
	bl	FlashReadPages
	ldr	r3, [r4, #3264]
	adds	r3, r3, #1
	beq	.L1330
	ldrh	r3, [r6]
	cmp	r3, r8
	beq	.L1331
.L1330:
	subs	r5, r5, #1
	sxth	r5, r5
	b	.L1329
.L1336:
	ldrh	r2, [r4, #3904]
	ldr	r1, [r4, #3272]
	ldr	r0, [r6, #4]!
	lsls	r2, r2, #2
	mla	r1, r5, r2, r1
	adds	r5, r5, #1
	bl	ftl_memcpy
	b	.L1335
.L1337:
	mov	r0, #-1
	pop	{r4, r5, r6, r7, r8, pc}
.L1349:
	.align	2
.L1348:
	.word	.LANCHOR0
	.word	.LANCHOR0+3264
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
	ldr	r9, .L1369+4
	bl	FtlBbtMemInit
	sub	r5, r9, #180
	sub	r10, r9, #18
	mov	r4, r5
	bl	FtlLoadFactoryBbt
.L1351:
	ldrh	r3, [r5, #54]
	cmp	r8, r3
	bcc	.L1357
	movs	r5, #0
.L1358:
	ldrh	r3, [r4, #114]
	uxth	r0, r5
	adds	r5, r5, #1
	cmp	r3, r0
	bhi	.L1359
	ldrh	r5, [r4, #164]
	movw	r6, #65535
	subs	r5, r5, #1
	uxth	r5, r5
.L1360:
	ldrh	r3, [r4, #164]
	subs	r3, r3, #48
	cmp	r5, r3
	ble	.L1364
	mov	r0, r5
	bl	FtlBbmIsBadBlock
	cmp	r0, #1
	beq	.L1361
	mov	r0, r5
	bl	FlashTestBlk
	cmp	r0, #0
	beq	.L1362
	mov	r0, r5
	bl	FtlBbmMapBadBlock
.L1361:
	subs	r5, r5, #1
	uxth	r5, r5
	b	.L1360
.L1357:
	ldrh	r3, [r10, #2]!
	movw	r2, #65535
	ldr	r0, [r5, #3284]
	ldr	fp, [r5, #3288]
	cmp	r3, r2
	str	r0, [r5, #3272]
	str	fp, [r5, #3276]
	beq	.L1352
	ldrh	r7, [r5, #98]
	movs	r2, #1
	mov	r1, r2
	ldr	r0, .L1369
	mla	r7, r8, r7, r3
	lsls	r3, r7, #10
	str	r3, [r5, #3268]
	bl	FlashReadPages
	ldrh	r2, [r5, #98]
	ldr	r1, [r5, #3272]
	ldr	r0, [r9]
	adds	r2, r2, #7
	asrs	r2, r2, #3
	bl	ftl_memcpy
.L1353:
	uxth	r0, r7
	add	r8, r8, #1
	add	r9, r9, #4
	bl	FtlBbmMapBadBlock
	b	.L1351
.L1352:
	mov	r1, r8
	bl	FlashGetBadBlockList
	ldr	r1, [r9]
	ldr	r0, [r5, #3272]
	bl	FtlBbt2Bitmap
	ldrh	r6, [r5, #98]
.L1355:
	subs	r6, r6, #1
	uxth	r6, r6
.L1354:
	ldrh	r0, [r4, #98]
	smlabb	r0, r0, r8, r6
	uxth	r0, r0
	bl	FtlBbmIsBadBlock
	cmp	r0, #1
	beq	.L1355
	movs	r1, #16
	ldr	r0, [r4, #3288]
	strh	r6, [r10]	@ movhi
	bl	__memzero
	mov	r1, #4096
	ldr	r0, [r4, #3284]
	bl	__memzero
	movw	r3, #61664
	strh	r3, [fp]	@ movhi
	movs	r3, #0
	str	r3, [fp, #4]
	ldrh	r3, [r10]
	ldrh	r7, [r4, #98]
	ldrh	r2, [r4, #3904]
	strh	r3, [fp, #2]	@ movhi
	ldrh	r3, [r10]
	ldr	r1, [r9]
	lsls	r2, r2, #2
	ldr	r0, [r4, #3272]
	mla	r7, r8, r7, r3
	lsls	r3, r7, #10
	str	r3, [r4, #3268]
	bl	ftl_memcpy
	movs	r2, #1
	ldr	r0, .L1369
	mov	r1, r2
	bl	FlashEraseBlocks
	movs	r3, #1
	ldr	r0, .L1369
	mov	r2, r3
	mov	r1, r3
	bl	FlashProgPages
	ldr	r3, [r4, #3264]
	adds	r3, r3, #1
	bne	.L1353
	uxth	r0, r7
	bl	FtlBbmMapBadBlock
	b	.L1354
.L1359:
	bl	FtlBbmMapBadBlock
	b	.L1358
.L1362:
	ldrh	r3, [r4, #152]
	cmp	r3, r6
	bne	.L1363
	strh	r5, [r4, #152]	@ movhi
	b	.L1361
.L1363:
	strh	r5, [r4, #156]	@ movhi
.L1364:
	ldrh	r3, [r4, #152]
	movs	r5, #0
	ldr	r0, [r4, #3804]
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
.L1370:
	.align	2
.L1369:
	.word	.LANCHOR0+3264
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
	ldr	r4, .L1383
	adds	r2, r0, r1
	.pad #108
	sub	sp, sp, #108
	mov	r8, r0
	mov	r6, r1
	ldrh	r3, [r4, #96]
	cmp	r2, r3
	bhi	.L1379
	ldrh	r7, [r4, #108]
	mov	r9, #0
	lsr	r7, r0, r7
	lsl	fp, r7, #2
.L1373:
	cbnz	r6, .L1378
.L1371:
	mov	r0, r9
	add	sp, sp, #108
	@ sp needed
	pop	{r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L1378:
	ldr	r3, [r4, #3888]
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
	cbz	r2, .L1375
	cmp	r5, r3
	beq	.L1375
	ldr	r3, [r4, #3852]
	add	r0, sp, #20
	str	r2, [sp, #24]
	movs	r2, #1
	mov	r1, r2
	str	r3, [sp, #28]
	add	r3, sp, #40
	str	r3, [sp, #32]
	bl	FlashReadPages
.L1376:
	lsls	r3, r5, #9
	ldr	r0, [r4, #3852]
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
	ldr	r2, [r4, #3852]
	ldr	r0, .L1383+4
	adds	r7, r7, #1
	bl	FtlMapWritePage
	ldr	r3, [sp, #8]
	adds	r0, r0, #1
	it	eq
	moveq	r9, #-1
	add	r10, r10, r3
	b	.L1373
.L1375:
	ldrh	r2, [r4, #110]
	movs	r1, #0
	ldr	r0, [r4, #3852]
	bl	ftl_memset
	b	.L1376
.L1379:
	mov	r9, #-1
	b	.L1371
.L1384:
	.align	2
.L1383:
	.word	.LANCHOR0
	.word	.LANCHOR0+3956
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
	ldr	r2, .L1387
	ldr	r3, .L1387+4
	ldr	r1, [r2, #2636]
	cmp	r1, r3
	bne	.L1385
	ldr	r3, .L1387+8
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
.L1385:
	bx	lr
.L1388:
	.align	2
.L1387:
	.word	.LANCHOR0
	.word	1179929683
	.word	1342177349
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
	ldr	r2, .L1394
	push	{r3, lr}
	.save {r3, lr}
	ldrh	r3, [r2, #-192]
	cmp	r3, #31
	itett	ls
	addls	r3, r3, #1
	movhi	r2, #32
	strhls	r3, [r2, #-192]	@ movhi
	movls	r2, #1
	ldr	r3, .L1394+4
	cbnz	r0, .L1391
	ldr	r1, [r3, #3864]
	ldr	r0, [r1, #20]
	ldr	r1, [r1, #16]
	add	r2, r2, r0
	cmp	r1, r2
	bcc	.L1392
.L1391:
	ldr	r2, [r3, #3864]
	movs	r0, #64
	ldr	r1, [r2, #16]
	str	r1, [r2, #20]
	ldr	r1, .L1394+8
	str	r1, [r2]
	ldrh	r1, [r3, #3856]
	ldr	r2, [r3, #3864]
	lsls	r3, r1, #9
	str	r3, [r2, #12]
	ldr	r3, [r2, #8]
	adds	r3, r3, #1
	str	r3, [r2, #8]
	movs	r3, #0
	str	r3, [r2, #4]
	bl	FtlVendorPartWrite
	bl	Ftl_save_ext_data
.L1392:
	movs	r0, #0
	pop	{r3, pc}
.L1395:
	.align	2
.L1394:
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
	@ args = 0, pretend = 0, frame = 104
	@ frame_needed = 0, uses_anonymous_args = 0
	push	{r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.save {r4, r5, r6, r7, r8, r9, r10, fp, lr}
	mov	r10, r2
	ldr	r4, .L1407
	adds	r2, r0, r1
	.pad #108
	sub	sp, sp, #108
	mov	r8, r0
	mov	r7, r1
	ldrh	r3, [r4, #96]
	cmp	r2, r3
	bhi	.L1406
	ldrh	r6, [r4, #108]
	mov	r9, #0
	lsr	r6, r0, r6
	lsl	fp, r6, #2
.L1399:
	cbnz	r7, .L1405
.L1397:
	mov	r0, r9
	add	sp, sp, #108
	@ sp needed
	pop	{r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L1405:
	ldr	r3, [r4, #3888]
	mov	r0, r8
	ldrh	r5, [r4, #58]
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
	cbz	r3, .L1401
	ldr	r2, [r4, #3852]
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
	ldr	r2, [r4, #3264]
	it	eq
	moveq	r9, #-1
	cmp	r2, #256
	bne	.L1403
	mov	r2, r3
	mov	r1, r6
	ldr	r0, .L1407+4
	bl	sftl_printk
	ldr	r2, [r4, #3852]
	mov	r1, r6
	ldr	r0, .L1407+8
	bl	FtlMapWritePage
.L1403:
	ldr	r1, [r4, #3852]
	lsls	r2, r5, #9
	ldr	r3, [sp, #4]
	mov	r0, r10
	add	r1, r1, r3, lsl #9
	bl	ftl_memcpy
.L1404:
	ldr	r3, [sp, #8]
	adds	r6, r6, #1
	subs	r7, r7, r5
	add	r8, r8, r5
	add	fp, fp, #4
	add	r10, r10, r3
	b	.L1399
.L1401:
	lsls	r2, r5, #9
	mov	r1, r3
	mov	r0, r10
	bl	ftl_memset
	b	.L1404
.L1406:
	mov	r9, #-1
	b	.L1397
.L1408:
	.align	2
.L1407:
	.word	.LANCHOR0
	.word	.LC107
	.word	.LANCHOR0+3956
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
	ldr	r4, .L1411
	ldr	r2, [r4, #3864]
	ldrh	r1, [r4, #3856]
	bl	FtlVendorPartRead
	ldr	r3, [r4, #3864]
	ldr	r2, [r3]
	ldr	r3, .L1411+4
	cmp	r2, r3
	beq	.L1410
	ldr	r1, .L1411+8
	ldr	r0, .L1411+12
	bl	sftl_printk
	ldrh	r2, [r4, #3856]
	movs	r1, #0
	ldr	r0, [r4, #3864]
	lsls	r2, r2, #9
	bl	ftl_memset
.L1410:
	movs	r0, #0
	pop	{r4, pc}
.L1412:
	.align	2
.L1411:
	.word	.LANCHOR0
	.word	1112818501
	.word	.LC108
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
	ldr	r4, .L1416
	movs	r0, #0
	ldr	r5, .L1416+4
	addw	r2, r4, #2636
	bl	FtlVendorPartRead
	ldr	r3, [r4, #2636]
	cmp	r3, r5
	beq	.L1414
	mov	r1, #512
	addw	r0, r4, #2636
	bl	__memzero
	str	r5, [r4, #2636]
.L1414:
	ldr	r3, [r4, #2636]
	cmp	r3, r5
	bne	.L1415
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
.L1415:
	ldrh	r2, [r4, #92]
	ldr	r3, [r4, #2552]
	ldr	r0, [r4, #2548]
	ldrh	r1, [r4, #40]
	mla	r0, r0, r2, r3
	bl	__aeabi_uidiv
	str	r0, [r4, #2556]
	pop	{r3, r4, r5, pc}
.L1417:
	.align	2
.L1416:
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
	ldr	r4, .L1434
	movs	r1, #255
	movw	r7, #65535
	ldr	r9, .L1434+16
	ldr	r3, [r4, #3284]
	add	r8, r4, #3264
	ldr	r6, [r4, #3288]
	ldr	r10, .L1434+20
	str	r3, [r4, #3272]
	ldrh	r3, [r4, #2576]
	str	r6, [r4, #3276]
	str	r5, [r6, #12]
	strh	r3, [r6, #2]	@ movhi
	movw	r3, #61604
	strh	r3, [r6]	@ movhi
	ldr	r3, [r4, #2584]
	str	r5, [r6, #8]
	ldrh	r2, [r4, #2350]
	str	r3, [r6, #4]
	ldr	r3, .L1434+4
	str	r3, [r4, #2268]
	ldr	r3, .L1434+8
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
	ldr	r0, [r4, #3272]
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
	ldrh	r3, [r4, #3202]
	strh	r3, [r4, #2312]	@ movhi
	ldrh	r3, [r4, #3204]
	strh	r3, [r4, #2314]	@ movhi
	bl	ftl_memset
	movs	r2, #48
	addw	r1, r4, #2268
	ldr	r0, [r4, #3272]
	bl	ftl_memcpy
	ldrh	r2, [r4, #40]
	ldr	r0, [r4, #3272]
	ldr	r1, [r4, #2324]
	lsls	r2, r2, #1
	adds	r0, r0, #48
	bl	ftl_memcpy
	ldrh	r0, [r4, #40]
	ldr	r3, [r4, #3272]
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
.L1420:
	ldr	r3, [r4, #3284]
	mov	r0, r8
	ldrh	r2, [r4, #2576]
	str	r3, [r4, #3272]
	ldr	r3, [r4, #3288]
	str	r3, [r4, #3276]
	ldrh	r3, [r4, #2578]
	orr	r3, r3, r2, lsl #10
	str	r3, [r4, #3268]
	movs	r3, #1
	mov	r2, r3
	mov	r1, r3
	bl	FlashProgPages
	ldrh	r3, [r4, #104]
	ldrh	r2, [r4, #2578]
	subs	r3, r3, #1
	cmp	r2, r3
	blt	.L1421
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
	str	r2, [r4, #3268]
	str	r3, [r6, #4]
	movs	r3, #1
	strh	r0, [r6, #2]	@ movhi
	mov	r2, r3
	mov	r1, r3
	mov	r0, r8
	bl	FlashProgPages
.L1421:
	ldrh	r3, [r4, #2578]
	ldr	r2, [r4, #3264]
	adds	r3, r3, #1
	uxth	r3, r3
	adds	r1, r2, #1
	strh	r3, [r4, #2578]	@ movhi
	bne	.L1422
	cmp	r3, #1
	bne	.L1423
	movw	r2, #1169
	mov	r1, r9
	mov	r0, r10
	bl	sftl_printk
.L1423:
	ldrh	r3, [r4, #2578]
	adds	r5, r5, #1
	uxth	r5, r5
	cmp	r3, #1
	ittt	eq
	ldrheq	r3, [r4, #104]
	addeq	r3, r3, #-1
	strheq	r3, [r4, #2578]	@ movhi
	cmp	r5, #3
	bls	.L1420
	mov	r2, r5
	ldr	r1, [r4, #3268]
	ldr	r0, .L1434+12
	bl	sftl_printk
.L1426:
	b	.L1426
.L1422:
	cmp	r3, #1
	beq	.L1420
	cmp	r2, #256
	beq	.L1420
	movw	r3, #65535
	cmp	r7, r3
	beq	.L1427
	movs	r1, #1
	mov	r0, r7
	bl	FtlFreeSysBlkQueueIn
.L1427:
	movs	r0, #0
	pop	{r4, r5, r6, r7, r8, r9, r10, pc}
.L1435:
	.align	2
.L1434:
	.word	.LANCHOR0
	.word	1179929683
	.word	1342177349
	.word	.LC109
	.word	.LANCHOR1+454
	.word	.LC1
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
	ldr	r3, .L1439
	ldr	r3, [r3]
	cmp	r3, #1
	bne	.L1438
	bl	FtlSysFlush
.L1438:
	movs	r0, #0
	pop	{r3, pc}
.L1440:
	.align	2
.L1439:
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
	ldr	r6, .L1456
	mov	r8, r0
	mov	r5, r1
	ldr	r3, [r6, #140]
	cmp	r2, r3
	bhi	.L1449
	cmp	r1, #31
	bhi	.L1443
.L1448:
	movs	r0, #0
.L1441:
	add	sp, sp, #8
	@ sp needed
	pop	{r4, r5, r6, r7, r8, pc}
.L1443:
	ldrh	r4, [r6, #58]
	mov	r1, r4
	bl	__aeabi_uidiv
	smulbb	r3, r0, r4
	mov	r7, r0
	sub	r8, r8, r3
	uxth	r3, r8
	cbz	r3, .L1444
	subs	r4, r4, r3
	adds	r7, r7, #1
	cmp	r4, r5
	it	cs
	movcs	r4, r5
	uxth	r4, r4
	subs	r5, r5, r4
.L1444:
	ldr	r4, .L1456+4
	mov	r3, #-1
	str	r3, [sp, #4]
.L1445:
	ldrh	r3, [r6, #58]
	cmp	r5, r3
	bcs	.L1447
	ldr	r3, .L1456+4
	ldr	r2, [r3, #-188]
	cmp	r2, #32
	bls	.L1448
	movs	r4, #0
	str	r4, [r3, #-188]
	bl	l2p_flush
	bl	FtlVpcTblFlush
	b	.L1448
.L1447:
	movs	r2, #0
	mov	r1, sp
	mov	r0, r7
	bl	log2phys
	ldr	r3, [sp]
	adds	r3, r3, #1
	beq	.L1446
	ldr	r3, [r4, #-188]
	movs	r2, #1
	add	r1, sp, #4
	mov	r0, r7
	adds	r3, r3, #1
	str	r3, [r4, #-188]
	ldr	r3, [r6, #2512]
	adds	r3, r3, #1
	str	r3, [r6, #2512]
	bl	log2phys
	ldr	r0, [sp]
	ubfx	r0, r0, #10, #16
	bl	P2V_block_in_plane
	bl	decrement_vpc_count
.L1446:
	ldrh	r3, [r6, #58]
	adds	r7, r7, #1
	subs	r5, r5, r3
	b	.L1445
.L1449:
	mov	r0, #-1
	b	.L1441
.L1457:
	.align	2
.L1456:
	.word	.LANCHOR0
	.word	.LANCHOR3
	.fnend
	.size	FtlDiscard, .-FtlDiscard
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
	ldr	r4, .L1470
	ldr	r1, .L1470+4
	ldr	r0, .L1470+8
	bl	sftl_printk
	ldrh	r2, [r4, #42]
	movs	r1, #0
	ldr	r0, [r4, #3868]
	lsls	r2, r2, #1
	bl	ftl_memset
.L1459:
	ldr	r3, [r4, #2504]
	cmp	r5, r3
	bcc	.L1461
	ldr	r9, .L1470+12
	movs	r7, #0
	movw	r8, #65535
.L1462:
	ldrh	r3, [r4, #40]
	uxth	r5, r7
	cmp	r3, r5
	bhi	.L1466
	add	sp, sp, #12
	@ sp needed
	pop	{r4, r5, r6, r7, r8, r9, pc}
.L1461:
	movs	r2, #0
	add	r1, sp, #4
	mov	r0, r5
	bl	log2phys
	ldr	r0, [sp, #4]
	adds	r3, r0, #1
	beq	.L1460
	ubfx	r0, r0, #10, #16
	bl	P2V_block_in_plane
	ldr	r2, [r4, #3868]
	ldrh	r3, [r2, r0, lsl #1]
	adds	r3, r3, #1
	strh	r3, [r2, r0, lsl #1]	@ movhi
.L1460:
	adds	r5, r5, #1
	b	.L1459
.L1466:
	ldr	r3, [r4, #2324]
	uxth	r6, r7
	ldrh	r2, [r3, r6, lsl #1]
	ldr	r3, [r4, #3868]
	ldrh	r3, [r3, r6, lsl #1]
	cmp	r2, r3
	beq	.L1464
	cmp	r2, r8
	beq	.L1464
	mov	r1, r6
	mov	r0, r9
	bl	sftl_printk
	ldrh	r3, [r4, #2348]
	cmp	r3, r5
	beq	.L1464
	ldrh	r3, [r4, #2444]
	cmp	r3, r5
	beq	.L1464
	ldrh	r3, [r4, #2396]
	cmp	r3, r5
	beq	.L1464
	ldr	r3, [r4, #3868]
	mov	r0, r5
	ldrh	r2, [r3, r6, lsl #1]
	ldr	r3, [r4, #2324]
	strh	r2, [r3, r6, lsl #1]	@ movhi
	bl	update_vpc_list
	bl	l2p_flush
	bl	FtlVpcTblFlush
.L1464:
	adds	r7, r7, #1
	b	.L1462
.L1471:
	.align	2
.L1470:
	.word	.LANCHOR0
	.word	.LANCHOR1+469
	.word	.LC97
	.word	.LC110
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
	ldr	r4, .L1482
	ldrh	r5, [r0]
	ldrh	r3, [r4, #40]
	cmp	r3, r5
	bcs	.L1473
	movw	r2, #2750
	ldr	r1, .L1482+4
	ldr	r0, .L1482+8
	bl	sftl_printk
.L1473:
	movw	r3, #65535
	cmp	r5, r3
	beq	.L1474
	ldr	r3, [r4, #2324]
	mov	r0, r5
	ldrh	r3, [r3, r5, lsl #1]
	cbz	r3, .L1475
	bl	INSERT_DATA_LIST
.L1474:
	ldrh	r0, [r4, #4000]
	movs	r3, #1
	strb	r3, [r6, #8]
	movw	r3, #65535
	cmp	r0, r3
	beq	.L1476
	cmp	r5, r0
	bne	.L1477
	ldr	r3, [r4, #2324]
	ldrh	r3, [r3, r0, lsl #1]
	cbz	r3, .L1478
.L1477:
	bl	update_vpc_list
.L1478:
	movw	r3, #65535
	strh	r3, [r4, #4000]	@ movhi
.L1476:
	mov	r0, r6
	bl	allocate_data_superblock
	bl	l2p_flush
	movs	r0, #0
	bl	FtlEctTblFlush
	bl	FtlVpcTblFlush
	movs	r0, #0
	pop	{r4, r5, r6, pc}
.L1475:
	bl	INSERT_FREE_LIST
	b	.L1474
.L1483:
	.align	2
.L1482:
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
	ldr	r6, .L1498
	.pad #20
	sub	sp, sp, #20
	mov	r4, r3
	movs	r2, #0
	ldr	r10, .L1498+8
	mov	r5, r0
	ldr	fp, .L1498+12
	mov	r9, r1
	ldrb	r3, [r3, #9]	@ zero_extendqisi2
	bl	FlashProgPages
.L1485:
	cmp	r7, r9
	beq	.L1492
	mov	r8, #0
	b	.L1493
.L1488:
	ldr	r0, [r5, #4]
	ubfx	r0, r0, #10, #16
	bl	P2V_block_in_plane
	ldrh	r3, [r4]
	cmp	r3, r0
	bne	.L1486
	ldr	r1, [r6, #2324]
	ldrh	r0, [r4, #4]
	ldrh	r2, [r1, r3, lsl #1]
	subs	r2, r2, r0
	strh	r2, [r1, r3, lsl #1]	@ movhi
	ldrh	r3, [r6, #102]
	strb	r8, [r4, #6]
	strh	r8, [r4, #4]	@ movhi
	strh	r3, [r4, #2]	@ movhi
.L1486:
	ldrh	r3, [r4, #4]
	cbnz	r3, .L1487
	mov	r0, r4
	bl	allocate_new_data_superblock
.L1487:
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
.L1493:
	ldr	r3, [r5]
	adds	r3, r3, #1
	beq	.L1488
	ldrb	r2, [r4, #6]	@ zero_extendqisi2
	ldrh	r3, [r6, #32]
	cmp	r2, r3
	bcc	.L1489
	movw	r2, #941
	mov	r1, r10
	mov	r0, fp
	bl	sftl_printk
.L1489:
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
	beq	.L1490
	ldr	r3, [r6, #2324]
	ldrh	r2, [r3, r0, lsl #1]
	cbnz	r2, .L1491
	mov	r1, r0
	ldr	r0, .L1498+4
	bl	sftl_printk
.L1491:
	mov	r0, r8
	bl	decrement_vpc_count
.L1490:
	adds	r7, r7, #1
	adds	r5, r5, #20
	b	.L1485
.L1492:
	ldrb	r2, [r4, #6]	@ zero_extendqisi2
	ldrh	r3, [r6, #32]
	cmp	r2, r3
	bcc	.L1484
	mov	r2, #956
	ldr	r1, .L1498+8
	ldr	r0, .L1498+12
	bl	sftl_printk
.L1484:
	add	sp, sp, #20
	@ sp needed
	pop	{r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L1499:
	.align	2
.L1498:
	.word	.LANCHOR0
	.word	.LC111
	.word	.LANCHOR1+519
	.word	.LC1
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
	push	{r0, r1, r2, r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.save {r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.pad #12
	movw	r2, #65535
	ldr	r4, .L1526
	ldrh	r5, [r4, #2444]
	ldrh	r1, [r4, #102]
	cmp	r5, r2
	bne	.L1501
.L1511:
	ldrh	r2, [r4, #2444]
	movs	r3, #0
	str	r3, [r4, #4008]
	movw	r3, #65535
	cmp	r2, r3
	bne	.L1524
.L1502:
	movs	r0, #0
.L1500:
	add	sp, sp, #12
	@ sp needed
	pop	{r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L1501:
	cbz	r0, .L1504
	ldr	r3, .L1526+4
	ldrh	r0, [r3, #4]
	cmp	r0, r2
	beq	.L1505
.L1506:
	movs	r1, #2
.L1504:
	ldr	r0, .L1526+8
	bl	FtlGcScanTempBlk
	str	r0, [sp, #4]
	adds	r0, r0, #1
	beq	.L1507
	ldr	r2, [r4, #2328]
	ldrh	r3, [r2, r5, lsl #1]
	cmp	r3, #4
	bls	.L1508
	subs	r3, r3, #5
	movs	r0, #1
	strh	r3, [r2, r5, lsl #1]	@ movhi
	bl	FtlEctTblFlush
.L1508:
	ldr	r3, [r4, #4008]
	cbnz	r3, .L1509
	ldr	r3, [r4, #2732]
	ldr	r0, [sp, #4]
	adds	r3, r3, #1
	ubfx	r0, r0, #10, #16
	str	r3, [r4, #2732]
	bl	FtlBbmMapBadBlock
	bl	FtlBbmTblFlush
.L1509:
	movs	r3, #0
	str	r3, [r4, #4008]
.L1520:
	movs	r0, #1
	b	.L1500
.L1505:
	movs	r2, #0
	strh	r2, [r3, #4]	@ movhi
	ldrh	r3, [r4, #2344]
	cmp	r3, #17
	bhi	.L1506
	b	.L1504
.L1507:
	ldr	r3, .L1526+4
	ldrh	r2, [r3, #4]
	movw	r3, #65535
	cmp	r2, r3
	bne	.L1520
	b	.L1511
.L1524:
	ldrb	r1, [r4, #2451]	@ zero_extendqisi2
	ldrh	r3, [r4, #102]
	ldrh	r2, [r4, #3200]
	muls	r3, r1, r3
	cmp	r2, r3
	beq	.L1512
	movs	r2, #162
	ldr	r1, .L1526+12
	ldr	r0, .L1526+16
	bl	sftl_printk
.L1512:
	ldrb	r3, [r4, #2451]	@ zero_extendqisi2
	movs	r6, #0
	ldrh	r0, [r4, #102]
	mov	r9, #12
	ldr	r2, [r4, #2324]
	ldrh	r1, [r4, #2444]
	ldr	r10, .L1526+12
	smulbb	r3, r3, r0
	ldr	fp, .L1526+16
	strh	r3, [r2, r1, lsl #1]	@ movhi
	ldr	r2, [r4, #2528]
	ldrh	r3, [r4, #3200]
	add	r3, r3, r2
	str	r3, [r4, #2528]
.L1513:
	ldrh	r2, [r4, #3200]
	uxth	r3, r6
	cmp	r2, r3
	bhi	.L1517
	movw	r0, #65535
	bl	decrement_vpc_count
	ldrh	r0, [r4, #2444]
	ldr	r3, [r4, #2324]
	ldrh	r3, [r3, r0, lsl #1]
	cmp	r3, #0
	beq	.L1518
	bl	INSERT_DATA_LIST
.L1519:
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
	ble	.L1502
	movs	r3, #20
	strh	r6, [r4, #2588]	@ movhi
	strh	r3, [r4, #3148]	@ movhi
	b	.L1502
.L1517:
	uxth	r7, r6
	ldr	r8, [r4, #3196]
	ldr	r3, [r4, #2504]
	mul	r7, r9, r7
	add	r5, r8, r7
	ldr	r2, [r5, #8]
	cmp	r2, r3
	bcc	.L1514
	movs	r2, #168
	mov	r1, r10
	mov	r0, fp
	bl	sftl_printk
.L1514:
	movs	r2, #0
	add	r1, sp, #4
	ldr	r0, [r5, #8]
	bl	log2phys
	ldr	r0, [r8, r7]
	ldr	r3, [sp, #4]
	cmp	r0, r3
	bne	.L1515
	ubfx	r0, r0, #10, #16
	bl	P2V_block_in_plane
	movs	r2, #1
	mov	r7, r0
	adds	r1, r5, #4
	ldr	r0, [r5, #8]
	bl	log2phys
	mov	r0, r7
.L1525:
	bl	decrement_vpc_count
.L1516:
	adds	r6, r6, #1
	b	.L1513
.L1515:
	ldr	r2, [r5, #4]
	cmp	r3, r2
	beq	.L1516
	ldrh	r0, [r4, #2444]
	b	.L1525
.L1518:
	bl	INSERT_FREE_LIST
	b	.L1519
.L1527:
	.align	2
.L1526:
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
	ldr	r4, .L1530
	ldrh	r1, [r4, #102]
	addw	r0, r4, #2444
	bl	FtlGcScanTempBlk
	ldrh	r2, [r4, #2446]
	ldrh	r3, [r4, #102]
	cmp	r2, r3
	bcc	.L1528
	addw	r0, r4, #3908
	bl	FtlMapBlkWriteDump_data
	movs	r0, #0
	bl	FtlGcFreeTempBlock
	movs	r3, #0
	str	r3, [r4, #4008]
.L1528:
	pop	{r4, pc}
.L1531:
	.align	2
.L1530:
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
	ldr	r4, .L1533
	addw	r6, r4, #2348
	str	r5, [r4, #4020]
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
.L1534:
	.align	2
.L1533:
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
	ldr	r4, .L1542
	movs	r5, #0
	movs	r6, #20
	mov	r2, r3
	ldr	r1, [r4, #3164]
	ldr	r0, [r4, #3840]
	bl	FlashProgPages
.L1536:
	ldr	r1, [r4, #3164]
	uxth	r3, r5
	cmp	r1, r3
	bhi	.L1539
	ldr	r0, [r4, #3840]
	bl	FtlGcBufFree
	ldrh	r3, [r4, #2448]
	movs	r0, #0
	str	r0, [r4, #3164]
	cbnz	r3, .L1535
	movs	r0, #1
	bl	FtlGcFreeTempBlock
	b	.L1541
.L1539:
	muls	r3, r6, r3
	ldr	r2, [r4, #3840]
	adds	r5, r5, #1
	adds	r1, r2, r3
	ldr	r2, [r2, r3]
	adds	r0, r2, #1
	bne	.L1537
	ldrh	r0, [r4, #2444]
	movs	r5, #0
	ldr	r1, [r4, #2324]
	strh	r5, [r1, r0, lsl #1]	@ movhi
	strh	r2, [r4, #2444]	@ movhi
	ldr	r2, [r4, #2732]
	adds	r2, r2, #1
	str	r2, [r4, #2732]
	ldr	r2, [r4, #3840]
	add	r3, r3, r2
	ldr	r0, [r3, #4]
	ubfx	r0, r0, #10, #16
	bl	FtlBbmMapBadBlock
	bl	FtlBbmTblFlush
	bl	FtlGcPageVarInit
.L1541:
	movs	r0, #1
.L1535:
	pop	{r4, r5, r6, pc}
.L1537:
	ldr	r3, [r1, #12]
	ldr	r1, [r1, #4]
	ldr	r2, [r3, #8]
	ldr	r0, [r3, #12]
	bl	FtlGcUpdatePage
	b	.L1536
.L1543:
	.align	2
.L1542:
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
	ldr	r4, .L1547
	ldrh	r2, [r4, #2444]
	cmp	r2, r3
	beq	.L1545
	ldrh	r3, [r4, #2448]
	cbnz	r3, .L1546
.L1545:
	movs	r0, #0
	movs	r5, #0
	bl	FtlGcFreeTempBlock
	ldr	r0, .L1547+4
	strb	r5, [r4, #2452]
	bl	allocate_data_superblock
	strh	r5, [r4, #3192]	@ movhi
	strh	r5, [r4, #3200]	@ movhi
	bl	l2p_flush
	mov	r0, r5
	bl	FtlEctTblFlush
	bl	FtlVpcTblFlush
.L1546:
	ldr	r0, .L1547+4
	pop	{r3, r4, r5, lr}
	b	get_new_active_ppa
.L1548:
	.align	2
.L1547:
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
	ldr	r3, .L1667
	push	{r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.save {r4, r5, r6, r7, r8, r9, r10, fp, lr}
	mov	r7, r0
	.pad #36
	sub	sp, sp, #36
	mov	r4, r3
	ldr	r0, [r3, #3824]
	cmp	r0, #0
	bne	.L1623
	ldrh	r2, [r3, #2336]
	cmp	r2, #47
	bls	.L1549
	ldr	r2, .L1667+4
	ldrh	r1, [r2, #4]
	movw	r2, #65535
	cmp	r1, r2
	bne	.L1551
.L1554:
	ldrh	r0, [r4, #3204]
	movw	r2, #65535
	cmp	r0, r2
	bne	.L1552
.L1553:
	ldr	r3, [r4, #3152]
	movw	r5, #65535
	ldrh	r1, [r4, #2588]
	adds	r3, r3, #1
	add	r3, r3, r7, lsl #7
	cmp	r1, r5
	str	r3, [r4, #3152]
	bne	.L1555
	ldrh	r2, [r4, #2444]
	cmp	r2, r1
	bne	.L1556
	ldrh	r8, [r4, #3202]
	cmp	r8, r2
	bne	.L1557
	ldrh	r2, [r4, #2344]
	cmp	r2, #24
	ite	cc
	movcc	r2, #5120
	movcs	r2, #1024
	cmp	r3, r2
	bls	.L1557
	ldr	r3, .L1667+8
	movs	r6, #0
	str	r6, [r4, #3152]
	strh	r6, [r3, #-184]	@ movhi
	bl	GetSwlReplaceBlock
	cmp	r0, r8
	mov	r5, r0
	bne	.L1627
	ldrh	r2, [r4, #2344]
	ldrh	r3, [r4, #3150]
	cmp	r2, r3
	bcs	.L1560
	movs	r0, #64
	bl	List_get_gc_head_node
	uxth	r3, r0
	cmp	r3, r5
	beq	.L1562
	mov	r0, r3
	ldr	r3, [r4, #2324]
	ldrh	r3, [r3, r0, lsl #1]
	cmp	r3, #7
	bhi	.L1563
	mov	r0, r6
	bl	List_get_gc_head_node
	uxth	r6, r0
	movs	r3, #128
	strh	r3, [r4, #3150]	@ movhi
	cmp	r6, r5
	bne	.L1559
.L1562:
	bl	FtlGcReFreshBadBlk
	cmp	r7, #0
	bne	.L1565
	movw	r3, #65535
	cmp	r5, r3
	bne	.L1565
.L1621:
	ldrh	r3, [r4, #2344]
	cmp	r3, #24
	bhi	.L1628
	cmp	r3, #16
	ldrh	r6, [r4, #102]
	bls	.L1567
	lsrs	r6, r6, #5
.L1566:
	ldrh	r2, [r4, #3148]
	cmp	r2, r3
	bcs	.L1570
	ldrh	r3, [r4, #2444]
	movw	r2, #65535
	cmp	r3, r2
	bne	.L1571
	ldrh	r2, [r4, #3202]
	cmp	r2, r3
	bne	.L1571
	ldr	r3, .L1667+8
	ldrh	r0, [r3, #-184]
	cbnz	r0, .L1572
	ldr	r3, [r4, #2504]
	ldr	r2, [r4, #2500]
	add	r3, r3, r3, lsl #1
	cmp	r2, r3, lsr #2
	bcs	.L1573
.L1572:
	ldrh	r3, [r4, #2572]
	add	r3, r3, r3, lsl #1
	asrs	r3, r3, #2
.L1661:
	strh	r3, [r4, #3148]	@ movhi
	movs	r3, #0
	str	r3, [r4, #3160]
.L1549:
	add	sp, sp, #36
	@ sp needed
	pop	{r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L1551:
	ldrh	r3, [r3, #2444]
	cmp	r3, r2
	beq	.L1554
	movs	r0, #1
	bl	FtlGcFreeTempBlock
	cmp	r0, #0
	beq	.L1554
	movs	r0, #1
	b	.L1549
.L1552:
	ldrh	r3, [r4, #3202]
	cmp	r3, r2
	bne	.L1553
	ldrh	r1, [r4, #3206]
	cmp	r1, r3
	beq	.L1553
	ldrh	r2, [r4, #3208]
	cmp	r2, r3
	itttt	ne
	strhne	r0, [r4, #3202]	@ movhi
	strhne	r1, [r4, #3204]	@ movhi
	strhne	r2, [r4, #3206]	@ movhi
	strhne	r3, [r4, #3208]	@ movhi
	b	.L1553
.L1563:
	movs	r3, #64
.L1660:
	strh	r3, [r4, #3150]	@ movhi
	b	.L1562
.L1560:
	movs	r3, #80
	b	.L1660
.L1627:
	mov	r6, r0
.L1559:
	ldr	r3, [r4, #2324]
	mov	r5, r6
	ldrh	r1, [r4, #3148]
	ldrh	r2, [r4, #2344]
	ldrh	r3, [r3, r6, lsl #1]
	str	r1, [sp, #4]
	ldr	r1, [r4, #2328]
	ldr	r0, .L1667+12
	ldrh	r1, [r1, r6, lsl #1]
	str	r1, [sp]
	mov	r1, r6
	bl	sftl_printk
	b	.L1562
.L1567:
	cmp	r3, #12
	bls	.L1568
	lsrs	r6, r6, #4
	b	.L1566
.L1568:
	cmp	r3, #8
	bls	.L1566
	lsrs	r6, r6, #2
	b	.L1566
.L1628:
	movs	r6, #1
	b	.L1566
.L1573:
	movs	r3, #18
	b	.L1661
.L1571:
	ldrh	r3, [r4, #2572]
	add	r3, r3, r3, lsl #1
	asrs	r3, r3, #2
	strh	r3, [r4, #3148]	@ movhi
.L1570:
	ldrh	r3, [r4, #3210]
	cbz	r3, .L1629
	adds	r6, r6, #32
	uxth	r6, r6
.L1629:
	movw	r5, #65535
.L1576:
	ldrh	r3, [r4, #2588]
	movw	r2, #65535
	cmp	r3, r2
	bne	.L1585
	cmp	r5, r3
	beq	.L1586
	strh	r5, [r4, #2588]	@ movhi
.L1587:
	ldrh	r0, [r4, #2588]
	movw	r7, #65535
	movs	r3, #0
	strb	r3, [r4, #2596]
	cmp	r0, r7
	beq	.L1585
	bl	IsBlkInGcList
	cbz	r0, .L1590
	strh	r7, [r4, #2588]	@ movhi
.L1590:
	ldrh	r2, [r4, #2588]
	movw	r3, #65535
	cmp	r2, r3
	beq	.L1585
	ldr	r0, .L1667+16
	bl	make_superblock
	ldr	r2, .L1667+8
	movs	r3, #0
	strh	r3, [r4, #2590]	@ movhi
	strb	r3, [r4, #2594]
	strh	r3, [r2, #-182]	@ movhi
	ldrh	r1, [r4, #2588]
	ldr	r3, [r4, #2324]
	ldrh	r3, [r3, r1, lsl #1]
	strh	r3, [r2, #-180]	@ movhi
.L1585:
	ldrh	r3, [r4, #2588]
	ldrh	r2, [r4, #2348]
	cmp	r2, r3
	beq	.L1591
	ldrh	r2, [r4, #2396]
	cmp	r2, r3
	beq	.L1591
	ldr	r7, .L1667+8
.L1592:
	ldrh	r9, [r4, #2588]
	movw	r3, #65535
	cmp	r9, r3
	bne	.L1593
	movs	r3, #0
	mov	r10, r3
	str	r3, [r4, #3160]
.L1594:
	ldrh	r8, [r4, #3156]
	mov	r0, r8
	bl	List_get_gc_head_node
	uxth	fp, r0
	cmp	fp, r9
	strh	fp, [r4, #2588]	@ movhi
	bne	.L1595
	movs	r3, #0
	movs	r0, #8
	strh	r3, [r4, #3156]	@ movhi
	b	.L1549
.L1577:
	movs	r3, #0
	ldrh	r7, [r4, #2344]
	str	r3, [r4, #3160]
	ldrh	r3, [r4, #3148]
	ldr	r5, .L1667+8
	cmp	r3, r7
	bcs	.L1578
	ldrh	r3, [r5, #-184]
	cbnz	r3, .L1579
	ldr	r3, [r4, #2504]
	ldr	r2, [r4, #2500]
	add	r3, r3, r3, lsl #1
	cmp	r2, r3, lsr #2
	bcs	.L1580
.L1579:
	ldrh	r3, [r4, #2572]
	add	r3, r3, r3, lsl #1
	asrs	r3, r3, #2
.L1662:
	strh	r3, [r4, #3148]	@ movhi
.L1664:
	bl	FtlReadRefresh
	ldrh	r0, [r5, #-184]
	b	.L1549
.L1580:
	movs	r3, #18
	b	.L1662
.L1578:
	ldrh	r0, [r5, #-184]
	cmp	r0, #0
	bne	.L1582
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
	ble	.L1583
	subs	r6, r6, #1
	cmp	r7, r6
	bge	.L1664
.L1583:
	cmp	r2, #0
	bne	.L1582
	movw	r0, #65535
	bl	decrement_vpc_count
	ldrh	r0, [r4, #2344]
	adds	r0, r0, #1
	b	.L1549
.L1668:
	.align	2
.L1667:
	.word	.LANCHOR0
	.word	.LANCHOR2
	.word	.LANCHOR3
	.word	.LC112
	.word	.LANCHOR0+2588
.L1586:
	ldrh	r3, [r4, #3202]
	cmp	r3, r5
	beq	.L1587
	ldr	r2, [r4, #2324]
	ldrh	r3, [r2, r3, lsl #1]
	cbnz	r3, .L1588
	strh	r5, [r4, #3202]	@ movhi
.L1588:
	ldrh	r3, [r4, #3202]
	strh	r3, [r4, #2588]	@ movhi
	movw	r3, #65535
	strh	r3, [r4, #3202]	@ movhi
	b	.L1587
.L1591:
	movw	r3, #65535
	strh	r3, [r4, #2588]	@ movhi
.L1665:
	ldr	r3, .L1669
	ldrh	r0, [r3, #-184]
	b	.L1549
.L1595:
	str	r0, [sp, #8]
	mov	r0, fp
	bl	IsBlkInGcList
	add	r8, r8, #1
	ldr	r3, [sp, #8]
	cbz	r0, .L1596
	strh	r8, [r4, #3156]	@ movhi
	b	.L1594
.L1596:
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
	bgt	.L1598
	cmp	r8, #48
	bls	.L1599
	cmp	r1, #8
	bls	.L1599
	ldrh	r1, [r4, #3192]
	cmp	r1, #35
	bhi	.L1599
.L1598:
	strh	r10, [r4, #3156]	@ movhi
.L1599:
	ldrh	r2, [r2, r0, lsl #1]
	cmp	r3, r2
	bgt	.L1600
	cmp	r5, r9
	bne	.L1600
	movs	r3, #0
	strh	r5, [r4, #2588]	@ movhi
	strh	r3, [r4, #3156]	@ movhi
	b	.L1665
.L1600:
	cbnz	r2, .L1601
	movw	r0, #65535
	bl	decrement_vpc_count
	ldrh	r3, [r4, #3156]
	adds	r3, r3, #1
	strh	r3, [r4, #3156]	@ movhi
	b	.L1594
.L1601:
	movs	r3, #0
	strb	r3, [r4, #2596]
	ldrh	r3, [r4, #2348]
	cmp	r3, fp
	bne	.L1602
	movw	r2, #665
	ldr	r1, .L1669+4
	ldr	r0, .L1669+8
	bl	sftl_printk
.L1602:
	ldrh	r2, [r4, #2588]
	ldrh	r3, [r4, #2396]
	cmp	r2, r3
	bne	.L1603
	movw	r2, #666
	ldr	r1, .L1669+4
	ldr	r0, .L1669+8
	bl	sftl_printk
.L1603:
	ldrh	r2, [r4, #2588]
	ldrh	r3, [r4, #2444]
	cmp	r2, r3
	bne	.L1604
	movw	r2, #667
	ldr	r1, .L1669+4
	ldr	r0, .L1669+8
	bl	sftl_printk
.L1604:
	ldr	r0, .L1669+12
	bl	make_superblock
	ldrh	r1, [r4, #2588]
	movs	r3, #0
	ldr	r2, [r4, #2324]
	strh	r3, [r7, #-182]	@ movhi
	ldrh	r2, [r2, r1, lsl #1]
	strh	r3, [r4, #2590]	@ movhi
	strb	r3, [r4, #2594]
	strh	r2, [r7, #-180]	@ movhi
.L1593:
	bl	FtlReadRefresh
	movs	r3, #1
	str	r3, [r4, #3824]
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
.L1663:
	str	r3, [sp, #8]
	ldrh	r3, [sp, #8]
	cmp	r6, r3
	bls	.L1614
	ldr	r3, [sp, #8]
	movw	r10, #65535
	ldrh	r1, [r4, #2590]
	mov	fp, #20
	ldrh	lr, [r4, #32]
	ldr	r0, [r4, #3180]
	add	r1, r1, r3
	movs	r3, #0
	ldr	ip, .L1669+20
	mov	r8, r3
	b	.L1615
.L1608:
	ldrh	r2, [ip, #2]!
	cmp	r2, r10
	beq	.L1607
	mla	r9, fp, r8, r0
	add	r8, r8, #1
	orr	r2, r1, r2, lsl #10
	uxth	r8, r8
	str	r2, [r9, #4]
.L1607:
	adds	r3, r3, #1
.L1615:
	uxth	r2, r3
	cmp	lr, r2
	bhi	.L1608
	mov	fp, #0
	ldrb	r2, [r4, #2596]	@ zero_extendqisi2
	mov	r1, r8
	bl	FlashReadPages
.L1609:
	uxth	r3, fp
	cmp	r8, r3
	bhi	.L1613
	ldr	r3, [sp, #8]
	adds	r3, r3, #1
	b	.L1663
.L1613:
	mov	r9, #20
	ldr	r3, [r4, #3180]
	mul	r9, r9, fp
	add	r2, r3, r9
	ldr	r3, [r3, r9]
	adds	r3, r3, #1
	beq	.L1610
	ldr	r10, [r2, #12]
	movw	r3, #61589
	ldrh	r2, [r10]
	cmp	r2, r3
	bne	.L1610
	ldr	r3, [r10, #8]
	adds	r2, r3, #1
	bne	.L1611
	movw	r2, #703
	ldr	r1, .L1669+4
	ldr	r0, .L1669+8
	str	r3, [sp, #16]
	bl	sftl_printk
	ldr	r3, [sp, #16]
.L1611:
	movs	r2, #0
	mov	r0, r3
	add	r1, sp, #28
	bl	log2phys
	ldr	r0, [r4, #3180]
	ldr	r3, [sp, #28]
	add	r0, r0, r9
	ldr	r2, [r0, #4]
	cmp	r2, r3
	bne	.L1610
	ldrh	r3, [r7, #-182]
	movs	r2, #20
	ldr	r1, [r4, #3164]
	str	r2, [sp, #20]
	adds	r3, r3, #1
	strh	r3, [r7, #-182]	@ movhi
	ldr	r3, [r4, #3840]
	mla	r3, r2, r1, r3
	ldr	r1, [r0, #16]
	str	r1, [r3, #16]
	str	r3, [sp, #16]
	bl	Ftl_get_new_temp_ppa
	ldr	r3, [sp, #16]
	ldr	r1, [r4, #3840]
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
	beq	.L1612
	ldrh	r3, [r4, #2448]
	cbnz	r3, .L1610
.L1612:
	bl	Ftl_gc_temp_data_write_back
	cbz	r0, .L1610
.L1666:
	movs	r3, #0
	ldrh	r0, [r7, #-184]
	str	r3, [r4, #3824]
	b	.L1549
.L1610:
	add	fp, fp, #1
	b	.L1609
.L1614:
	ldrh	r3, [r4, #2590]
	add	r6, r6, r3
	ldr	r3, [sp, #12]
	uxth	r6, r6
	cmp	r3, r6
	strh	r6, [r4, #2590]	@ movhi
	bhi	.L1616
	ldr	r3, [r4, #3164]
	cbz	r3, .L1617
	bl	Ftl_gc_temp_data_write_back
	cmp	r0, #0
	bne	.L1666
.L1617:
	ldrh	r6, [r7, #-182]
	cbnz	r6, .L1618
	ldrh	r1, [r4, #2588]
	ldr	r3, [r4, #2324]
	ldrh	r3, [r3, r1, lsl #1]
	cbz	r3, .L1618
	ldrh	r0, [r7, #-180]
	ldrh	r2, [r4, #2590]
	str	r3, [sp]
	mov	r3, r6
	str	r0, [sp, #4]
	ldr	r0, .L1669+16
	bl	sftl_printk
	ldrh	r2, [r4, #2588]
	ldr	r3, [r4, #2324]
	strh	r6, [r3, r2, lsl #1]	@ movhi
	ldrh	r0, [r4, #2588]
	bl	update_vpc_list
	bl	l2p_flush
	bl	FtlVpcTblFlush
.L1618:
	movw	r3, #65535
	strh	r3, [r4, #2588]	@ movhi
.L1616:
	ldrh	r3, [r4, #2344]
	cmp	r3, #2
	bhi	.L1619
	ldrh	r6, [r4, #102]
	b	.L1592
.L1619:
	movs	r2, #0
	str	r2, [r4, #3824]
	ldr	r2, .L1669
	ldrh	r0, [r2, #-184]
	cmp	r0, #0
	bne	.L1549
	adds	r0, r3, #1
	b	.L1549
.L1623:
	movs	r0, #0
	b	.L1549
.L1670:
	.align	2
.L1669:
	.word	.LANCHOR3
	.word	.LANCHOR1+551
	.word	.LC1
	.word	.LANCHOR0+2588
	.word	.LC113
	.word	.LANCHOR0+2602
.L1555:
	cmp	r7, #0
	beq	.L1621
.L1565:
	ldrh	r2, [r4, #2444]
	movw	r3, #65535
	cmp	r2, r3
	bne	.L1632
.L1622:
	ldrh	r3, [r4, #3202]
	movw	r2, #65535
	cmp	r3, r2
	bne	.L1632
	cmp	r5, r3
	bne	.L1632
	ldrh	r3, [r4, #2588]
	cmp	r3, r5
	beq	.L1577
.L1582:
	movw	r5, #65535
.L1632:
	movs	r6, #1
	b	.L1576
.L1556:
	cmp	r7, #0
	bne	.L1582
	b	.L1621
.L1557:
	cmp	r7, #0
	beq	.L1621
	movw	r5, #65535
	b	.L1622
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
	.pad #84
	sub	sp, sp, #84
	mov	r7, r1
	mov	r9, r3
	str	r2, [sp, #56]
	bne	.L1673
	mov	r2, r3
	ldr	r1, [sp, #56]
	add	r0, r7, #256
	bl	FtlVendorPartRead
	str	r0, [sp, #28]
.L1672:
	ldr	r0, [sp, #28]
	add	sp, sp, #84
	@ sp needed
	pop	{r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L1673:
	ldr	r3, [sp, #56]
	ldr	r4, .L1705
	adds	r3, r1, r3
	str	r3, [sp, #36]
	ldr	r2, [sp, #36]
	ldr	r3, [r4, #140]
	cmp	r2, r3
	bhi	.L1694
	ldr	r3, .L1705+4
	ldr	r3, [r3]
	adds	r0, r3, #1
	beq	.L1695
	ldrh	r5, [r4, #58]
	mov	r0, r7
	mov	r8, #0
	mov	r6, r8
	mov	r1, r5
	bl	__aeabi_uidiv
	ldr	r3, [sp, #36]
	mov	r1, r5
	str	r0, [sp, #40]
	subs	r0, r3, #1
	bl	__aeabi_uidiv
	ldr	r3, [sp, #40]
	ldr	r5, [sp, #40]
	str	r0, [sp, #44]
	rsb	r3, r3, #1
	str	r8, [sp, #52]
	add	r3, r3, r0
	str	r8, [sp, #48]
	str	r3, [sp, #32]
	ldr	r2, [sp, #32]
	ldr	r3, [r4, #2508]
	str	r8, [sp, #28]
	add	r3, r3, r2
	str	r3, [r4, #2508]
.L1675:
	ldr	r3, [sp, #32]
	cbnz	r3, .L1692
	ldr	r3, .L1705
	ldrh	r2, [r3, #3210]
	cbnz	r2, .L1693
	ldrh	r3, [r3, #2344]
	cmp	r3, #31
	bhi	.L1672
.L1693:
	movs	r1, #1
	movs	r0, #0
	bl	rk_ftl_garbage_collect
	b	.L1672
.L1692:
	add	r1, sp, #76
	movs	r2, #0
	mov	r0, r5
	bl	log2phys
	ldr	r3, [sp, #76]
	adds	r1, r3, #1
	bne	.L1676
	mov	r10, #0
.L1677:
	ldrh	r0, [r4, #58]
	cmp	r10, r0
	bcc	.L1679
.L1680:
	ldr	r3, [sp, #32]
	adds	r5, r5, #1
	subs	r3, r3, #1
	str	r3, [sp, #32]
	beq	.L1684
	ldrh	r3, [r4, #32]
	cmp	r6, r3, lsl #2
	bne	.L1675
.L1684:
	cmp	r6, #0
	beq	.L1675
	movs	r2, #0
	mov	r1, r6
	ldr	r0, [r4, #3836]
	mov	fp, #0
	bl	FlashReadPages
	lsl	r3, r8, #9
	str	r3, [sp, #68]
	ldr	r3, [sp, #48]
	lsls	r3, r3, #9
	str	r3, [sp, #60]
	ldr	r3, [sp, #52]
	lsls	r3, r3, #9
	str	r3, [sp, #64]
.L1691:
	movs	r3, #20
	ldr	r1, [sp, #40]
	mul	r10, r3, fp
	ldr	r3, [r4, #3836]
	add	r3, r3, r10
	ldr	r2, [r3, #16]
	cmp	r1, r2
	bne	.L1686
	ldr	r1, [r3, #8]
	ldr	r3, [r4, #3820]
	cmp	r1, r3
	bne	.L1687
	ldr	r3, [sp, #60]
	mov	r0, r9
	ldr	r2, [sp, #64]
	add	r1, r1, r3
.L1704:
	bl	ftl_memcpy
.L1687:
	ldr	r1, [r4, #3836]
	add	r1, r1, r10
	ldr	r3, [r1, #12]
	ldr	r2, [r1, #16]
	ldr	r3, [r3, #8]
	cmp	r2, r3
	beq	.L1688
	ldr	r3, [r4, #2708]
	adds	r3, r3, #1
	str	r3, [r4, #2708]
	ldr	r2, [r1, #8]
	ldr	r3, [r1, #12]
	ldr	r0, [r2, #4]
	str	r0, [sp, #16]
	ldr	r2, [r2]
	ldr	r0, .L1705+8
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
.L1688:
	ldr	r3, [r4, #3836]
	ldr	r1, [r3, r10]
	add	r0, r3, r10
	adds	r2, r1, #1
	itttt	eq
	ldreq	r2, [r4, #2708]
	streq	r1, [sp, #28]
	addeq	r2, r2, #1
	streq	r2, [r4, #2708]
	ldr	r3, [r3, r10]
	cmp	r3, #256
	bne	.L1690
	ldr	r0, [r0, #4]
	ubfx	r0, r0, #10, #16
	bl	P2V_block_in_plane
	bl	FtlGcRefreshBlock
.L1690:
	add	fp, fp, #1
	cmp	r6, fp
	bne	.L1691
	movs	r6, #0
	b	.L1675
.L1679:
	mla	r0, r0, r5, r10
	cmp	r7, r0
	bhi	.L1678
	ldr	r3, [sp, #36]
	cmp	r3, r0
	bls	.L1678
	subs	r0, r0, r7
	mov	r1, #512
	add	r0, r9, r0, lsl #9
	bl	__memzero
.L1678:
	add	r10, r10, #1
	b	.L1677
.L1676:
	ldr	r2, [r4, #3836]
	mov	fp, #20
	ldrh	r10, [r4, #58]
	mla	fp, fp, r6, r2
	str	r3, [fp, #4]
	ldr	r3, [sp, #40]
	cmp	r5, r3
	bne	.L1681
	ldr	r3, [r4, #3820]
	mov	r1, r10
	mov	r0, r7
	str	r3, [fp, #8]
	bl	__aeabi_uidivmod
	ldr	r2, [sp, #56]
	sub	r3, r10, r1
	str	r1, [sp, #48]
	cmp	r3, r2
	it	cs
	movcs	r3, r2
	cmp	r3, r10
	str	r3, [sp, #52]
	bne	.L1682
	str	r9, [fp, #8]
.L1682:
	ldrh	r3, [r4, #112]
	ldr	r2, [r4, #3816]
	str	r5, [fp, #16]
	muls	r3, r6, r3
	adds	r6, r6, #1
	bic	r3, r3, #3
	add	r3, r3, r2
	str	r3, [fp, #12]
	b	.L1680
.L1681:
	ldr	r3, [sp, #44]
	cmp	r5, r3
	bne	.L1683
	ldr	r3, [r4, #3812]
	ldr	r2, [sp, #36]
	str	r3, [fp, #8]
	mul	r3, r10, r5
	sub	r8, r2, r3
	cmp	r10, r8
	bne	.L1682
.L1703:
	subs	r3, r3, r7
	add	r3, r9, r3, lsl #9
	str	r3, [fp, #8]
	b	.L1682
.L1683:
	mul	r3, r5, r10
	b	.L1703
.L1686:
	ldr	r1, [sp, #44]
	cmp	r1, r2
	bne	.L1687
	ldr	r1, [r3, #8]
	ldr	r3, [r4, #3812]
	cmp	r1, r3
	bne	.L1687
	ldrh	r0, [r4, #58]
	ldr	r3, [sp, #44]
	ldr	r2, [sp, #68]
	muls	r0, r3, r0
	subs	r0, r0, r7
	add	r0, r9, r0, lsl #9
	b	.L1704
.L1694:
	mov	r3, #-1
.L1695:
	str	r3, [sp, #28]
	b	.L1672
.L1706:
	.align	2
.L1705:
	.word	.LANCHOR0
	.word	.LANCHOR2
	.word	.LC96
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
	bne	.L1709
	mov	r2, r3
	ldr	r1, [sp, #12]
	add	r0, r10, #256
	bl	FtlVendorPartWrite
.L1708:
	add	sp, sp, #68
	@ sp needed
	pop	{r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L1709:
	ldr	r3, [sp, #12]
	ldr	r5, .L1759
	adds	r6, r1, r3
	ldr	r3, [r5, #140]
	cmp	r6, r3
	bhi	.L1745
	ldr	r4, .L1759+4
	ldr	r0, [r4]
	adds	r7, r0, #1
	beq	.L1708
	ldr	r3, .L1759+8
	mov	r2, #2048
	ldrh	r7, [r5, #58]
	mov	r0, r10
	str	r2, [r3, #-176]
	mov	r1, r7
	bl	__aeabi_uidiv
	mov	r1, r7
	str	r0, [sp, #4]
	subs	r0, r6, #1
	bl	__aeabi_uidiv
	ldr	r2, [sp, #4]
	str	r0, [sp, #20]
	subs	r3, r0, r2
	str	r3, [sp, #24]
	adds	r3, r3, #1
	str	r3, [sp]
	ldr	r2, [sp]
	ldr	r3, [r5, #2516]
	add	r3, r3, r2
	str	r3, [r5, #2516]
	ldr	r3, [r4, #8]
	cbz	r3, .L1747
	ldrh	r2, [r5, #2352]
	addw	r3, r5, #2348
	addw	r5, r5, #2396
	cmp	r2, #0
	it	ne
	movne	r5, r3
.L1711:
	ldr	r7, [sp, #4]
	ldr	r4, .L1759
.L1712:
	ldr	r3, [sp]
	cbnz	r3, .L1741
	mov	r0, r3
	ldr	r1, [sp, #24]
	bl	rk_ftl_garbage_collect
	ldrh	r3, [r4, #2344]
	cmp	r3, #15
	bls	.L1742
.L1744:
	movs	r0, #0
	b	.L1708
.L1747:
	addw	r5, r5, #2348
	b	.L1711
.L1741:
	ldrb	r2, [r5, #6]	@ zero_extendqisi2
	ldrh	r3, [r4, #32]
	cmp	r2, r3
	bcc	.L1713
	mov	r2, #996
	ldr	r1, .L1759+12
	ldr	r0, .L1759+16
	bl	sftl_printk
.L1713:
	ldrh	r2, [r5, #4]
	cbnz	r2, .L1714
	ldr	r3, .L1759+20
	ldr	r6, .L1759+4
	cmp	r5, r3
	bne	.L1715
	ldrh	r8, [r4, #2400]
	cmp	r8, #0
	bne	.L1716
	add	r0, r5, #48
	bl	allocate_new_data_superblock
	str	r8, [r6, #8]
.L1716:
	ldr	r5, .L1759+20
	ldr	r0, .L1759+20
	bl	allocate_new_data_superblock
	ldr	r2, [r6, #8]
	add	r3, r5, #48
	cmp	r2, #0
	it	ne
	movne	r5, r3
.L1717:
	ldrh	r3, [r5, #4]
	cbnz	r3, .L1714
	mov	r0, r5
	bl	allocate_new_data_superblock
.L1714:
	ldrb	r2, [r5, #7]	@ zero_extendqisi2
	ldrh	r3, [r5, #4]
	ldr	r1, [sp]
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
	bcc	.L1718
	movw	r2, #1029
	ldr	r1, .L1759+12
	ldr	r0, .L1759+16
	bl	sftl_printk
.L1718:
	mov	fp, #0
.L1719:
	ldr	r3, [sp, #36]
	cmp	fp, r3
	bne	.L1739
.L1720:
	mov	r3, r5
	movs	r2, #0
	mov	r1, fp
	ldr	r0, [r4, #3844]
	bl	FtlProgPages
	ldr	r3, [sp]
	cmp	fp, r3
	bls	.L1740
	movw	r2, #1106
	ldr	r1, .L1759+12
	ldr	r0, .L1759+16
	bl	sftl_printk
.L1740:
	ldr	r3, [sp]
	sub	r3, r3, fp
	str	r3, [sp]
	b	.L1712
.L1715:
	str	r2, [r6, #8]
	ldrh	r2, [r4, #2352]
	cbnz	r2, .L1750
	mov	r0, r5
	bl	allocate_new_data_superblock
	b	.L1717
.L1750:
	mov	r5, r3
	b	.L1714
.L1739:
	ldrh	r3, [r5, #4]
	cmp	r3, #0
	beq	.L1720
	movs	r2, #0
	add	r1, sp, #40
	mov	r0, r7
	mov	r8, #20
	bl	log2phys
	mov	r0, r5
	mul	r8, r8, fp
	bl	get_new_active_ppa
	ldrh	r2, [r4, #112]
	ldr	r3, [r4, #3844]
	mul	r1, r2, fp
	add	r3, r3, r8
	str	r0, [r3, #4]
	str	r7, [r3, #16]
	bic	r1, r1, #3
	str	r1, [sp, #28]
	ldr	r0, [sp, #28]
	ldr	r1, [r4, #3816]
	add	r9, r1, r0
	str	r1, [sp, #32]
	str	r9, [r3, #12]
	movs	r1, #0
	mov	r0, r9
	bl	ftl_memset
	ldr	r3, [sp, #4]
	cmp	r7, r3
	beq	.L1721
	ldr	r3, [sp, #20]
	cmp	r7, r3
	bne	.L1754
	ldrh	r2, [r4, #58]
	ldr	r3, [sp, #12]
	smulbb	r2, r2, r7
	add	r6, r10, r3
	movs	r3, #0
	str	r3, [sp, #16]
	subs	r6, r6, r2
	uxth	r6, r6
	b	.L1724
.L1721:
	ldrh	r6, [r4, #58]
	mov	r0, r10
	mov	r1, r6
	bl	__aeabi_uidivmod
	ldr	r3, [sp, #12]
	subs	r6, r6, r1
	str	r1, [sp, #16]
	cmp	r6, r3
	it	cs
	movcs	r6, r3
.L1724:
	ldrh	r3, [r4, #58]
	cmp	r6, r3
	ldr	r3, [sp, #4]
	bne	.L1725
	cmp	r7, r3
	ldr	r3, [r4, #3844]
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
.L1727:
	ldrb	r2, [r5, #6]	@ zero_extendqisi2
	ldrh	r3, [r4, #32]
	cmp	r2, r3
	bcc	.L1736
	movw	r2, #1097
	ldr	r1, .L1759+12
	ldr	r0, .L1759+16
	bl	sftl_printk
.L1736:
	ldr	r2, [sp, #32]
	movw	r3, #61589
	ldr	r1, [sp, #28]
	add	fp, fp, #1
	strh	r3, [r2, r1]	@ movhi
	ldr	r3, [r4, #2544]
	str	r7, [r9, #8]
	adds	r7, r7, #1
	str	r3, [r9, #4]
	adds	r3, r3, #1
	adds	r2, r3, #1
	it	eq
	moveq	r3, #0
	str	r3, [r4, #2544]
	ldr	r3, [sp, #40]
	str	r3, [r9, #12]
	ldrh	r3, [r5]
	strh	r3, [r9, #2]	@ movhi
	b	.L1719
.L1725:
	cmp	r7, r3
	ldr	r3, [r4, #3844]
	ite	eq
	ldreq	r2, [r4, #3820]
	ldrne	r2, [r4, #3812]
	add	r3, r3, r8
	str	r2, [r3, #8]
	ldr	r3, [sp, #40]
	adds	r1, r3, #1
	beq	.L1730
	str	r3, [sp, #48]
	movs	r1, #1
	ldr	r3, [r4, #3844]
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
	adds	r3, r3, #1
	bne	.L1731
	ldr	r3, [r4, #2708]
	adds	r3, r3, #1
	str	r3, [r4, #2708]
.L1734:
	ldr	r3, [sp, #4]
	lsls	r2, r6, #9
	cmp	r7, r3
	bne	.L1735
	ldr	r3, [r4, #3844]
	ldr	r1, [sp, #8]
	add	r3, r3, r8
	ldr	r0, [r3, #8]
	ldr	r3, [sp, #16]
	add	r0, r0, r3, lsl #9
.L1757:
	bl	ftl_memcpy
	b	.L1727
.L1760:
	.align	2
.L1759:
	.word	.LANCHOR0
	.word	.LANCHOR2
	.word	.LANCHOR3
	.word	.LANCHOR1+574
	.word	.LC1
	.word	.LANCHOR0+2348
.L1731:
	ldr	r3, [r9, #8]
	cmp	r7, r3
	beq	.L1733
	ldr	r3, [r4, #2708]
	mov	r2, r7
	ldr	r0, .L1761
	adds	r3, r3, #1
	str	r3, [r4, #2708]
	ldr	r1, [r9, #8]
	bl	sftl_printk
.L1733:
	ldr	r3, [r9, #8]
	cmp	r7, r3
	beq	.L1734
	movw	r2, #1082
	ldr	r1, .L1761+4
	ldr	r0, .L1761+8
	bl	sftl_printk
	b	.L1734
.L1730:
	ldr	r3, [r4, #3844]
	movs	r1, #0
	ldrh	r2, [r4, #110]
	add	r3, r3, r8
	ldr	r0, [r3, #8]
	bl	ftl_memset
	b	.L1734
.L1735:
	ldrh	r1, [r4, #58]
	ldr	r0, [sp, #8]
	ldr	r3, [r4, #3844]
	muls	r1, r7, r1
	add	r3, r3, r8
	sub	r1, r1, r10
	add	r1, r0, r1, lsl #9
	ldr	r0, [r3, #8]
	b	.L1757
.L1754:
	ldr	r3, [r4, #3844]
	ldr	r2, [sp, #8]
	add	r8, r8, r3
	ldrh	r3, [r4, #58]
	muls	r3, r7, r3
	sub	r3, r3, r10
	add	r3, r2, r3, lsl #9
	str	r3, [r8, #8]
	b	.L1727
.L1742:
	movw	r5, #65535
.L1755:
	ldrh	r3, [r4, #2588]
	cmp	r3, r5
	bne	.L1743
	ldrh	r3, [r4, #3202]
	cmp	r3, r5
	bne	.L1743
	movs	r0, #0
	bl	List_get_gc_head_node
	uxth	r0, r0
	bl	FtlGcRefreshBlock
.L1743:
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
	bls	.L1755
	b	.L1744
.L1745:
	mov	r0, #-1
	b	.L1708
.L1762:
	.align	2
.L1761:
	.word	.LC114
	.word	.LANCHOR1+574
	.word	.LC1
	.fnend
	.size	FtlWrite, .-FtlWrite
	.global	__aeabi_idivmod
	.align	1
	.global	sftl_write
	.syntax unified
	.thumb
	.thumb_func
	.fpu softvfp
	.type	sftl_write, %function
sftl_write:
	.fnstart
	@ args = 0, pretend = 0, frame = 112
	@ frame_needed = 0, uses_anonymous_args = 0
	push	{r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.save {r4, r5, r6, r7, r8, r9, r10, fp, lr}
	adds	r4, r0, r1
	subs	r5, r4, #1
	.pad #124
	sub	sp, sp, #124
	cmp	r5, #63
	mov	fp, r0
	str	r1, [sp, #16]
	str	r2, [sp, #36]
	ldr	r6, .L1795
	bls	.L1764
	cmp	r0, #576
	bls	.L1765
.L1764:
	ldr	r3, [r6, #-172]
	cmp	r3, #0
	beq	.L1766
	ldr	r2, .L1795+4
	mov	r0, #512
	ldrh	r3, [r2, #14]
	str	r3, [sp, #20]
	ldrh	r3, [r2, #10]
	ldrb	r1, [sp, #20]	@ zero_extendqisi2
	smulbb	r3, r3, r1
	uxth	r3, r3
	mov	r1, r3
	str	r3, [sp, #24]
	bl	__aeabi_uidiv
	ldr	r2, .L1795+8
	movs	r3, #0
	ldr	r1, .L1795+12
	str	r0, [sp, #12]
	sub	r4, r2, #262144
.L1770:
	ldr	r0, [r2, #-4]
	cmp	r0, #0
	beq	.L1767
.L1771:
	ldr	r3, [sp, #12]
	movs	r4, #0
	ldr	r2, [sp, #24]
	ldr	r5, .L1795+4
	str	r4, [sp, #28]
	muls	r3, r2, r3
	str	r3, [sp, #48]
	ldr	r3, [sp, #12]
	str	r3, [sp, #32]
.L1768:
	mov	r1, #512
	ldr	r0, .L1795+16
	bl	__memzero
	ldrh	r7, [r5, #14]
	mov	r0, r4
	ldrh	r6, [r5, #10]
	mov	r1, r7
	uxtb	r8, r7
	uxth	r3, r8
	str	r3, [sp, #40]
	ldrh	r3, [sp, #40]
	smulbb	r6, r6, r3
	bl	__aeabi_uidiv
	uxth	r6, r6
	mov	r1, r0
	ldr	r3, [r5, #3252]
	movs	r0, #0
	blx	r3
	cmp	r6, #512
	bcs	.L1772
	mov	r1, r7
	adds	r0, r6, r4
	bl	__aeabi_uidiv
	ldr	r3, [r5, #3252]
	mov	r1, r0
	movs	r0, #0
	blx	r3
.L1772:
	mov	r1, r6
	mov	r0, r4
	bl	__aeabi_uidivmod
	movs	r6, #0
	mov	r9, r1
	subs	r3, r4, r1
	str	r3, [sp, #44]
.L1773:
	cmp	r6, #512
	bcc	.L1774
	ldrb	r8, [r5, #14]	@ zero_extendqisi2
	mov	r0, r4
	ldrh	r1, [r5, #10]
	movs	r6, #0
	uxth	r3, r8
	str	r3, [sp, #40]
	ldrh	r3, [sp, #40]
	smulbb	r1, r1, r3
	uxth	r1, r1
	bl	__aeabi_uidivmod
	mov	r10, r1
	subs	r3, r4, r1
	uxth	r0, r10
	mov	r1, r8
	str	r3, [sp, #44]
	bl	__aeabi_idivmod
	uxth	r7, r1
.L1775:
	cmp	r6, #512
	bcs	.L1779
	ldr	r3, [sp, #40]
	mov	r1, r8
	add	r0, r10, r6
	sub	r9, r3, r7
	ldr	r3, .L1795+16
	uxth	r9, r9
	add	r2, r3, r6, lsl #9
	str	r2, [sp, #52]
	bl	__aeabi_uidiv
	ldr	r3, [sp, #44]
	uxth	r0, r0
	mov	r1, r8
	add	r7, r7, r3
	mla	r0, r8, r0, r7
	bl	__aeabi_uidiv
	ldr	r7, [r5, #3260]
	mov	r1, r0
	add	r3, sp, #56
	ldr	r2, [sp, #52]
	movs	r0, #0
	blx	r7
	adds	r0, r0, #1
	bne	.L1776
.L1779:
	ldr	r1, .L1795+20
	movs	r6, #0
.L1777:
	ldr	r3, .L1795+16
	mov	r7, r1
	adds	r1, r1, #4
	ldr	r2, [r3, r6, lsl #2]
	ldr	r3, [r7]
	cmp	r2, r3
	beq	.L1780
	mov	r1, #512
	ldr	r0, .L1795+16
	bl	__memzero
	ldr	r2, .L1795+16
	mov	r1, r4
	str	r6, [sp]
	ldr	r3, [r7]
	ldr	r2, [r2, r6, lsl #2]
	ldr	r0, .L1795+24
	bl	sftl_printk
	mov	r0, r4
	ldr	r1, [sp, #20]
	bl	__aeabi_uidiv
	ldr	r3, [r5, #3252]
	mov	r1, r0
	movs	r0, #0
	blx	r3
	ldr	r3, [sp, #12]
	cmp	r3, #1
	bls	.L1781
	ldr	r3, [sp, #24]
	ldr	r1, [sp, #20]
	adds	r0, r3, r4
	bl	__aeabi_uidiv
	ldr	r3, [r5, #3252]
	mov	r1, r0
	movs	r0, #0
	blx	r3
.L1781:
	ldr	r2, [sp, #32]
	ldr	r3, [sp, #12]
	add	r3, r3, r2
	ldr	r2, [sp, #48]
	add	r4, r4, r2
	ldr	r2, [sp, #32]
	cmp	r2, #15
	bls	.L1787
.L1786:
	ldr	r3, .L1795
	movs	r2, #0
	str	r2, [r3, #-172]
.L1766:
	ldr	r3, [sp, #36]
	mov	r1, fp
	ldr	r2, [sp, #16]
	movs	r0, #0
	bl	FtlWrite
	add	sp, sp, #124
	@ sp needed
	pop	{r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L1767:
	ldr	r0, [r4, r3, lsl #2]
	adds	r3, r3, #1
	cmp	r3, #4096
	it	hi
	movhi	r3, #0
	str	r0, [r2, #-4]!
	cmp	r1, r2
	bne	.L1770
	b	.L1771
.L1787:
	str	r3, [sp, #32]
	b	.L1768
.L1774:
	mov	r1, r8
	add	r0, r9, r6
	bl	__aeabi_uidiv
	uxth	r10, r0
	mov	r1, r8
	ldr	r0, [sp, #44]
	mul	r3, r8, r10
	str	r3, [sp, #56]
	movw	r3, #61424
	str	r3, [sp, #60]
	ldr	r3, .L1795+20
	add	r2, r3, r6, lsl #9
	str	r2, [sp, #52]
	bl	__aeabi_uidiv
	ldr	r7, [r5, #3256]
	add	r3, sp, #56
	add	r1, r0, r10
	ldr	r2, [sp, #52]
	movs	r0, #0
	blx	r7
	ldr	r3, [sp, #40]
	add	r6, r6, r3
	uxth	r6, r6
	b	.L1773
.L1776:
	add	r6, r6, r9
	movs	r7, #0
	uxth	r6, r6
	b	.L1775
.L1780:
	adds	r6, r6, #1
	cmp	r6, #65536
	bne	.L1777
	ldr	r3, [sp, #28]
	adds	r3, r3, #1
	cmp	r3, #5
	str	r3, [sp, #28]
	bls	.L1781
	b	.L1786
.L1765:
	cmp	r0, #63
	ldr	r0, .L1795+20
	bhi	.L1783
	ldr	r3, [sp, #16]
	rsb	r1, fp, #64
	subs	r2, r3, r1
	ldr	r3, [sp, #36]
	add	r1, r3, r1, lsl #9
.L1784:
	movs	r3, #1
	cmp	r5, #576
	str	r3, [r6, #-172]
	ittt	hi
	subhi	r2, r2, r4
	mvnhi	r3, #446
	addhi	r2, r2, r3
	lsls	r2, r2, #9
	bl	memcpy
	b	.L1766
.L1783:
	ldr	r3, .L1795+28
	ldr	r1, [sp, #36]
	ldr	r2, [sp, #16]
	add	r3, r3, fp
	add	r0, r0, r3, lsl #9
	b	.L1784
.L1796:
	.align	2
.L1795:
	.word	.LANCHOR3
	.word	.LANCHOR0
	.word	idb_buf+262144
	.word	idb_buf+16388
	.word	gp_flash_check_buf
	.word	idb_buf
	.word	.LC115
	.word	8388544
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
	ldr	r4, .L1818
	ldr	r3, [r4, #3284]
	ldrh	r2, [r4, #40]
	ldr	r0, [r4, #2324]
	str	r3, [r4, #3272]
	ldr	r3, [r4, #3288]
	lsls	r2, r2, #1
	str	r3, [r4, #3276]
	bl	ftl_memset
	ldrh	r0, [r4, #2576]
	movw	r3, #65535
	cmp	r0, r3
	bne	.L1798
.L1806:
	mov	r0, #-1
	pop	{r3, r4, r5, r6, r7, pc}
.L1798:
	ldr	r7, .L1818+4
	movs	r1, #1
	add	r6, r4, #3264
	bl	FtlGetLastWrittenPage
	sxth	r5, r0
	adds	r0, r0, #1
	strh	r0, [r4, #2578]	@ movhi
.L1800:
	cmp	r5, #0
	bge	.L1803
	movw	r2, #1482
	ldr	r1, .L1818+8
	ldr	r0, .L1818+12
	bl	sftl_printk
.L1802:
	ldrh	r3, [r4, #40]
	ldrh	r2, [r4, #110]
	adds	r3, r3, #24
	cmp	r2, r3, lsl #1
	bcs	.L1805
	movw	r2, #1484
	ldr	r1, .L1818+8
	ldr	r0, .L1818+12
	bl	sftl_printk
.L1805:
	movs	r2, #48
	ldr	r1, [r4, #3272]
	ldr	r0, .L1818+16
	bl	ftl_memcpy
	ldrh	r2, [r4, #40]
	ldr	r1, [r4, #3272]
	ldr	r0, [r4, #2324]
	lsls	r2, r2, #1
	adds	r1, r1, #48
	bl	ftl_memcpy
	ldrh	r1, [r4, #40]
	ldr	r3, [r4, #3272]
	ldr	r0, [r4, #24]
	lsrs	r2, r1, #3
	adds	r1, r1, #24
	lsls	r1, r1, #1
	adds	r2, r2, #4
	bic	r1, r1, #3
	add	r1, r1, r3
	bl	ftl_memcpy
	ldr	r2, [r4, #2268]
	ldr	r3, .L1818+4
	cmp	r2, r3
	bne	.L1806
	ldrb	r2, [r4, #2278]	@ zero_extendqisi2
	ldrh	r3, [r4, #54]
	ldrh	r5, [r4, #2276]
	cmp	r2, r3
	strh	r5, [r4, #2582]	@ movhi
	bne	.L1806
	ldr	r3, .L1818+20
	ldrh	r2, [r4, #58]
	ldr	r6, [r4, #44]
	str	r5, [r3, #-168]
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
	bls	.L1807
	movw	r2, #1506
	ldr	r1, .L1818+8
	ldr	r0, .L1818+12
	bl	sftl_printk
.L1807:
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
	beq	.L1810
	ldr	r0, .L1818+24
	bl	make_superblock
.L1810:
	ldrh	r2, [r4, #2396]
	movw	r3, #65535
	cmp	r2, r3
	beq	.L1811
	ldr	r0, .L1818+28
	bl	make_superblock
.L1811:
	ldrh	r2, [r4, #2444]
	movw	r3, #65535
	cmp	r2, r3
	beq	.L1812
	ldr	r0, .L1818+32
	bl	make_superblock
.L1812:
	ldrh	r2, [r4, #2588]
	movw	r3, #65535
	cmp	r2, r3
	beq	.L1813
	ldr	r0, .L1818+36
	bl	make_superblock
.L1813:
	movs	r0, #0
	pop	{r3, r4, r5, r6, r7, pc}
.L1803:
	ldrh	r3, [r4, #2576]
	movs	r2, #1
	mov	r1, r2
	mov	r0, r6
	orr	r3, r5, r3, lsl #10
	str	r3, [r4, #3268]
	ldr	r3, [r4, #3284]
	str	r3, [r4, #3272]
	bl	FlashReadPages
	ldr	r3, [r4, #3264]
	adds	r3, r3, #1
	beq	.L1801
	ldr	r3, [r4, #3284]
	ldr	r3, [r3]
	cmp	r3, r7
	bne	.L1801
	ldr	r3, [r4, #3288]
	ldrh	r2, [r3]
	movw	r3, #61604
	cmp	r2, r3
	beq	.L1802
.L1801:
	subs	r5, r5, #1
	sxth	r5, r5
	b	.L1800
.L1819:
	.align	2
.L1818:
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
	ldr	r4, .L1844
	ldrh	r0, [r4, #36]
	strh	r3, [r4, #4016]	@ movhi
	bl	FtlFreeSysBlkQueueInit
	bl	FtlScanSysBlk
	ldrh	r2, [r4, #2576]
	movw	r3, #65535
	cmp	r2, r3
	bne	.L1821
.L1823:
	mov	r6, #-1
.L1820:
	mov	r0, r6
	pop	{r4, r5, r6, pc}
.L1821:
	bl	FtlLoadSysInfo
	mov	r6, r0
	cmp	r0, #0
	bne	.L1823
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
	mov	r3, r6
	ldr	r2, [r4, #2492]
	movs	r0, #12
.L1824:
	cmp	r3, r1
	bge	.L1829
	mla	r5, r0, r3, r2
	ldr	r5, [r5, #4]
	cmp	r5, #0
	bge	.L1825
.L1829:
	ldrh	r2, [r4, #2296]
	cmp	r3, r1
	add	r2, r2, #1
	strh	r2, [r4, #2296]	@ movhi
	bge	.L1843
.L1826:
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
	ldrh	r5, [r4, #2400]
	ldrh	r2, [r1, r0, lsl #1]
	subs	r2, r2, r5
	strh	r2, [r1, r0, lsl #1]	@ movhi
	ldrh	r2, [r4, #102]
	ldr	r0, .L1844+4
	strb	r3, [r4, #2402]
	strh	r2, [r4, #2398]	@ movhi
	strh	r3, [r4, #2400]	@ movhi
	bl	FtlMapBlkWriteDump_data
	ldr	r0, .L1844+8
	bl	FtlMapBlkWriteDump_data
	ldrh	r3, [r4, #2298]
	adds	r3, r3, #1
	strh	r3, [r4, #2298]	@ movhi
	bl	l2p_flush
	bl	FtlVpcTblFlush
	bl	FtlVpcTblFlush
	b	.L1830
.L1825:
	adds	r3, r3, #1
	b	.L1824
.L1843:
	ldrh	r3, [r4, #4016]
	cmp	r3, #0
	bne	.L1826
.L1830:
	ldrh	r5, [r4, #2312]
	movw	r2, #65533
	subs	r3, r5, #1
	uxth	r3, r3
	cmp	r3, r2
	bhi	.L1832
	mov	r1, r5
	ldr	r0, .L1844+12
	strh	r5, [r4, #3202]	@ movhi
	bl	test_node_in_list
	cbnz	r0, .L1833
	ldrh	r3, [r4, #40]
	cmp	r3, r5
	bls	.L1833
	ldr	r3, [r4, #2324]
	ldrh	r3, [r3, r5, lsl #1]
	cbnz	r3, .L1832
.L1833:
	movw	r3, #65535
	strh	r3, [r4, #3202]	@ movhi
.L1832:
	ldrh	r5, [r4, #2314]
	movw	r2, #65533
	subs	r3, r5, #1
	uxth	r3, r3
	cmp	r3, r2
	bhi	.L1836
	mov	r1, r5
	ldr	r0, .L1844+12
	strh	r5, [r4, #3204]	@ movhi
	bl	test_node_in_list
	cbnz	r0, .L1837
	ldrh	r3, [r4, #40]
	cmp	r3, r5
	bls	.L1837
	ldr	r3, [r4, #2324]
	ldrh	r3, [r3, r5, lsl #1]
	cbnz	r3, .L1836
.L1837:
	movw	r3, #65535
	strh	r3, [r4, #3204]	@ movhi
.L1836:
	ldrh	r0, [r4, #2348]
	movw	r3, #65535
	cmp	r0, r3
	beq	.L1839
	ldrh	r3, [r4, #2352]
	cbnz	r3, .L1839
	ldrh	r3, [r4, #2400]
	cbnz	r3, .L1839
	bl	FtlGcRefreshBlock
	ldrh	r0, [r4, #2396]
	bl	FtlGcRefreshBlock
	bl	FtlVpcTblFlush
	ldr	r0, .L1844+16
	bl	allocate_new_data_superblock
	ldr	r0, .L1844+20
	bl	allocate_new_data_superblock
.L1839:
	ldrh	r3, [r4, #2296]
	lsls	r3, r3, #27
	bne	.L1820
	bl	FtlVpcCheckAndModify
	b	.L1820
.L1845:
	.align	2
.L1844:
	.word	.LANCHOR0
	.word	.LANCHOR0+3908
	.word	.LANCHOR0+3956
	.word	.LANCHOR0+2340
	.word	.LANCHOR0+2348
	.word	.LANCHOR0+2396
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
	ldr	r4, .L1870
	ldrh	r0, [r4, #36]
	str	r3, [r4, #2540]
	str	r3, [r4, #2544]
	bl	FtlFreeSysBlkQueueInit
	bl	FtlLoadBbt
	cbz	r0, .L1847
	bl	FtlMakeBbt
.L1847:
	ldr	r0, .L1870+4
	movs	r2, #0
.L1848:
	ldrh	r1, [r4, #58]
	uxth	r3, r2
	adds	r2, r2, #1
	cmp	r3, r1, lsl #7
	blt	.L1849
	ldrh	r6, [r4, #40]
	movs	r5, #0
.L1850:
	ldrh	r3, [r4, #42]
	cmp	r3, r6
	bhi	.L1851
	ldrh	r1, [r4, #32]
	subs	r3, r5, #3
	cmp	r3, r1, lsl #1
	bge	.L1852
.L1856:
	movs	r5, #0
	mov	r6, r5
.L1853:
	ldrh	r3, [r4, #40]
	uxth	r0, r5
	adds	r5, r5, #1
	cmp	r3, r0
	bhi	.L1857
	ldrh	r3, [r4, #42]
	ldrh	r5, [r4, #32]
	ldr	r9, [r4, #44]
	str	r3, [r4, #3808]
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
	ble	.L1858
	mov	r1, r5
	sub	r0, r9, r6
	bl	__aeabi_uidiv
	str	r0, [r4, #2504]
	lsrs	r0, r0, #5
	adds	r0, r0, #24
	strh	r0, [r4, #2572]	@ movhi
.L1858:
	ldrh	r3, [r4, #94]
	cbz	r3, .L1860
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
.L1860:
	ldrh	r2, [r4, #2572]
	movw	r6, #65535
	ldr	r3, [r4, #2504]
	ldr	r7, .L1870+8
	subs	r3, r3, r2
	muls	r5, r3, r5
	ldrh	r3, [r4, #102]
	str	r5, [r7, #-168]
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
	ldr	r7, .L1870+12
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
.L1862:
	mov	r0, r7
	bl	make_superblock
	ldrb	r3, [r4, #2355]	@ zero_extendqisi2
	ldrh	r2, [r4, #2348]
	cmp	r3, #0
	bne	.L1863
	ldr	r3, [r4, #2324]
	strh	r6, [r3, r2, lsl #1]	@ movhi
	ldrh	r3, [r4, #2348]
	adds	r3, r3, #1
	strh	r3, [r4, #2348]	@ movhi
	b	.L1862
.L1849:
	ldr	r5, [r4, #3820]
	mvns	r1, r3
	orr	r1, r3, r1, lsl #16
	str	r1, [r5, r3, lsl #2]
	ldr	r1, [r4, #3812]
	str	r0, [r1, r3, lsl #2]
	b	.L1848
.L1851:
	mov	r0, r6
	movs	r1, #1
	bl	FtlLowFormatEraseBlock
	adds	r6, r6, #1
	add	r5, r5, r0
	uxth	r5, r5
	uxth	r6, r6
	b	.L1850
.L1852:
	mov	r0, r5
	bl	__aeabi_uidiv
	ldr	r3, [r4, #132]
	add	r0, r0, r3
	uxth	r0, r0
	bl	FtlSysBlkNumInit
	ldrh	r0, [r4, #36]
	bl	FtlFreeSysBlkQueueInit
	ldrh	r5, [r4, #40]
.L1854:
	ldrh	r3, [r4, #42]
	cmp	r3, r5
	bls	.L1856
	mov	r0, r5
	movs	r1, #1
	adds	r5, r5, #1
	bl	FtlLowFormatEraseBlock
	uxth	r5, r5
	b	.L1854
.L1857:
	movs	r1, #0
	bl	FtlLowFormatEraseBlock
	add	r6, r6, r0
	uxth	r6, r6
	b	.L1853
.L1863:
	ldr	r3, [r4, #2540]
	movw	r6, #65535
	ldrh	r1, [r4, #2352]
	ldr	r7, .L1870+16
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
.L1864:
	mov	r0, r7
	bl	make_superblock
	ldrb	r3, [r4, #2403]	@ zero_extendqisi2
	ldrh	r2, [r4, #2396]
	cbnz	r3, .L1865
	ldr	r3, [r4, #2324]
	strh	r6, [r3, r2, lsl #1]	@ movhi
	ldrh	r3, [r4, #2396]
	adds	r3, r3, #1
	strh	r3, [r4, #2396]	@ movhi
	b	.L1864
.L1865:
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
	ldr	r3, [r5, #-168]
	strh	r6, [r4, #2580]	@ movhi
	strh	r3, [r4, #2582]	@ movhi
	ldr	r3, [r4, #2540]
	str	r3, [r4, #2584]
	adds	r3, r3, #1
	str	r3, [r4, #2540]
	bl	FtlVpcTblFlush
	bl	FtlSysBlkInit
	cbnz	r0, .L1866
	ldr	r3, .L1870+20
	movs	r2, #1
	str	r2, [r3]
.L1866:
	movs	r0, #0
	pop	{r3, r4, r5, r6, r7, r8, r9, pc}
.L1871:
	.align	2
.L1870:
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
	ldr	r4, .L1878
	ldr	r5, .L1878+4
	ldr	r1, .L1878+8
	ldr	r0, .L1878+12
	str	r3, [r5]
	bl	sftl_printk
	mov	r0, r4
	bl	FtlConstantsInit
	bl	FtlMemInit
	bl	FtlVariablesInit
	ldrh	r0, [r4, #36]
	bl	FtlFreeSysBlkQueueInit
	bl	FtlLoadBbt
	cbnz	r0, .L1876
	bl	FtlSysBlkInit
	cbnz	r0, .L1876
	movs	r3, #1
	str	r3, [r5]
	ldrh	r3, [r4, #2344]
	cmp	r3, #15
	bhi	.L1876
	movw	r4, #8129
.L1875:
	movs	r1, #1
	movs	r0, #0
	bl	rk_ftl_garbage_collect
	subs	r4, r4, #1
	bne	.L1875
.L1876:
	movs	r0, #0
	pop	{r3, r4, r5, pc}
.L1879:
	.align	2
.L1878:
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
	ldr	r2, .L1884
	ldr	r3, [r2, #-164]
	cbnz	r3, .L1883
	str	r0, [r2, #-164]
	mov	r0, r3
	str	r1, [r2, #-160]
	bx	lr
.L1883:
	mov	r0, #-1
	bx	lr
.L1885:
	.align	2
.L1884:
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
	push	{r3, r4, r5, r6, r7, r8, r9, lr}
	.save {r3, r4, r5, r6, r7, r8, r9, lr}
	mov	r0, #65536
	ldr	r4, .L1894
	bl	ftl_malloc
	str	r0, [r4, #-156]
	cmp	r0, #0
	beq	.L1892
	ldr	r8, .L1894+4
	mov	r9, #0
	mov	r7, r9
	mov	r6, r9
.L1890:
	ldr	r3, [r4, #-164]
	movs	r1, #128
	ldr	r2, [r4, #-156]
	lsls	r0, r6, #7
	blx	r3
	mov	r5, r0
	cbnz	r0, .L1888
	ldr	r2, [r4, #-156]
	ldr	r3, [r2]
	cmp	r3, r8
	bne	.L1889
	movw	r3, #65532
	ldr	r1, [r2, r3]
	ldr	r3, [r2, #4]
	cmp	r1, r3
	bne	.L1889
	cmp	r1, r7
	itt	hi
	movhi	r9, r6
	movhi	r7, r1
.L1889:
	adds	r6, r6, #1
	cmp	r6, #2
	bne	.L1890
	cbz	r7, .L1891
	ldr	r3, [r4, #-164]
	movs	r1, #128
	lsl	r0, r9, #7
	blx	r3
	mov	r5, r0
	cbz	r0, .L1886
.L1888:
	ldr	r0, [r4, #-156]
	mov	r5, #-1
	bl	kfree
	movs	r3, #0
	str	r3, [r4, #-156]
	b	.L1886
.L1891:
	mov	r1, #65536
	mov	r0, r2
	bl	__memzero
	ldr	r3, [r4, #-156]
	movs	r2, #1
	movw	r1, #65532
	str	r2, [r3, #4]
	str	r8, [r3]
	str	r2, [r3, r1]
	movw	r2, #64504
	strh	r7, [r3, #12]	@ movhi
	strh	r2, [r3, #14]	@ movhi
.L1886:
	mov	r0, r5
	pop	{r3, r4, r5, r6, r7, r8, r9, pc}
.L1892:
	mvn	r5, #11
	b	.L1886
.L1895:
	.align	2
.L1894:
	.word	.LANCHOR3
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
	ldr	r3, .L1902
	mov	r0, r1
	ldr	r5, [r3, #-156]
	cbz	r5, .L1901
	ldrh	r6, [r5, #10]
	movs	r3, #0
.L1898:
	cmp	r3, r6
	bcc	.L1900
.L1901:
	mov	r0, #-1
	pop	{r3, r4, r5, r6, r7, pc}
.L1900:
	add	r1, r5, r3, lsl #3
	ldrh	r4, [r1, #16]
	cmp	r4, r7
	bne	.L1899
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
.L1899:
	adds	r3, r3, #1
	b	.L1898
.L1903:
	.align	2
.L1902:
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
	ldr	r2, .L1923
	.pad #28
	sub	sp, sp, #28
	mov	r3, r1
	ldr	r4, [r2, #-156]
	mov	r9, r2
	cmp	r4, #0
	beq	.L1919
	ldrh	r2, [r4, #10]
	add	r6, r8, #63
	ldrh	r1, [r4, #8]
	bic	r6, r6, #63
	movs	r7, #0
	str	r1, [sp, #4]
.L1906:
	cmp	r7, r2
	bcc	.L1914
	ldrh	r1, [r4, #14]
	cmp	r6, r1
	bhi	.L1919
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
	ldr	r3, [r9, #-160]
	b	.L1922
.L1914:
	add	r5, r4, r7, lsl #3
	ldrh	r1, [r5, #16]
	cmp	r1, r0
	str	r1, [sp, #8]
	bne	.L1907
	ldrh	r1, [r5, #20]
	add	fp, r4, #1024
	adds	r1, r1, #63
	bic	r1, r1, #63
	cmp	r8, r1
	str	r1, [sp, #12]
	bls	.L1908
	ldrh	r1, [r4, #14]
	cmp	r6, r1
	bhi	.L1919
	ldrh	r10, [r5, #18]
	subs	r2, r2, #1
	str	r2, [sp, #16]
.L1909:
	ldr	r2, [sp, #16]
	adds	r5, r5, #8
	cmp	r7, r2
	bcc	.L1910
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
.L1911:
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
	ldr	r3, .L1923
	ldr	r3, [r3, #-160]
.L1922:
	ldr	r0, [sp, #4]
	mov	r2, r4
	movs	r1, #128
	lsls	r0, r0, #7
	blx	r3
	movs	r0, #0
.L1904:
	add	sp, sp, #28
	@ sp needed
	pop	{r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L1910:
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
	b	.L1909
.L1908:
	ldrh	r0, [r5, #18]
	mov	r2, r8
	mov	r1, r3
	add	r0, r0, fp
	bl	memcpy
	strh	r8, [r5, #20]	@ movhi
	b	.L1911
.L1907:
	adds	r7, r7, #1
	b	.L1906
.L1919:
	mov	r0, #-1
	b	.L1904
.L1924:
	.align	2
.L1923:
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
	beq	.L1939
	ldr	r3, .L1951
	cmp	r6, r3
	beq	.L1928
	adds	r3, r3, #1
	cmp	r6, r3
	beq	.L1929
.L1950:
	mvn	r5, #13
	b	.L1927
.L1928:
	ldr	r6, .L1951+4
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
	bne	.L1935
	movs	r2, #8
	mov	r1, r5
	bl	arm_copy_from_user
	cmp	r0, #0
	bne	.L1950
	ldr	r2, [r4]
	ldr	r3, .L1951+8
	cmp	r2, r3
	beq	.L1933
.L1934:
	mov	r5, #-1
.L1927:
	mov	r0, r4
	bl	kfree
.L1925:
	mov	r0, r5
	pop	{r4, r5, r6, pc}
.L1933:
	ldrh	r2, [r4, #6]
	add	r1, r4, #8
	ldrh	r0, [r4, #4]
	bl	rk_sftl_vendor_read
	adds	r3, r0, #1
	beq	.L1934
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
	bne	.L1950
	mov	r1, r4
	mov	r0, r5
	bl	arm_copy_to_user
	cmp	r0, #0
	bne	.L1950
.L1941:
	mov	r5, r0
	b	.L1927
.L1929:
	ldr	r6, .L1951+4
	mov	r3, sp
	ands	r3, r3, r6
	ldr	r3, [r3, #72]
	.syntax unified
@ 552 "./arch/arm/include/asm/uaccess.h" 1
	adds r2, r5, #8; sbcccs r2, r2, r3; movcc r3, #0
@ 0 "" 2
	.thumb
	.syntax unified
	cbnz	r3, .L1935
	movs	r2, #8
	mov	r1, r5
	bl	arm_copy_from_user
	cmp	r0, #0
	bne	.L1950
	ldr	r2, [r4]
	ldr	r3, .L1951+8
	cmp	r2, r3
	bne	.L1934
	ldrh	r2, [r4, #6]
	movw	r3, #4087
	cmp	r2, r3
	bhi	.L1934
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
	cbnz	r3, .L1937
	mov	r1, r5
	mov	r0, r4
	bl	arm_copy_from_user
	cmp	r0, #0
	bne	.L1950
	ldrh	r2, [r4, #6]
	add	r1, r4, #8
	ldrh	r0, [r4, #4]
	bl	rk_sftl_vendor_write
	b	.L1941
.L1935:
	movs	r1, #8
.L1949:
	bl	__memzero
	b	.L1950
.L1937:
	mov	r1, r2
	mov	r0, r4
	b	.L1949
.L1939:
	mov	r5, #-1
	b	.L1925
.L1952:
	.align	2
.L1951:
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
	ldr	r0, .L1954
	b	misc_register
.L1955:
	.align	2
.L1954:
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
	.type	__func__.6749, %object
	.size	__func__.6749, 17
__func__.6749:
	.ascii	"INSERT_DATA_LIST\000"
	.type	__func__.6744, %object
	.size	__func__.6744, 17
__func__.6744:
	.ascii	"INSERT_FREE_LIST\000"
	.type	__func__.6787, %object
	.size	__func__.6787, 17
__func__.6787:
	.ascii	"List_remove_node\000"
	.type	__func__.6819, %object
	.size	__func__.6819, 22
__func__.6819:
	.ascii	"List_update_data_list\000"
	.type	__func__.6930, %object
	.size	__func__.6930, 22
__func__.6930:
	.ascii	"select_l2p_ram_region\000"
	.type	__func__.7253, %object
	.size	__func__.7253, 16
__func__.7253:
	.ascii	"make_superblock\000"
	.type	__func__.7428, %object
	.size	__func__.7428, 19
__func__.7428:
	.ascii	"get_new_active_ppa\000"
	.type	__func__.17766, %object
	.size	__func__.17766, 15
__func__.17766:
	.ascii	"FlashReadPages\000"
	.type	__func__.17783, %object
	.size	__func__.17783, 15
__func__.17783:
	.ascii	"FlashProgPages\000"
	.type	__func__.6861, %object
	.size	__func__.6861, 26
__func__.6861:
	.ascii	"ftl_map_blk_alloc_new_blk\000"
	.type	__func__.6280, %object
	.size	__func__.6280, 11
__func__.6280:
	.ascii	"FtlMemInit\000"
	.type	__func__.6584, %object
	.size	__func__.6584, 14
__func__.6584:
	.ascii	"FtlBbt2Bitmap\000"
	.type	__func__.6891, %object
	.size	__func__.6891, 31
__func__.6891:
	.ascii	"Ftl_write_map_blk_to_last_page\000"
	.type	__func__.6905, %object
	.size	__func__.6905, 16
__func__.6905:
	.ascii	"FtlMapWritePage\000"
	.type	__func__.6828, %object
	.size	__func__.6828, 16
__func__.6828:
	.ascii	"load_l2p_region\000"
	.type	__func__.6876, %object
	.size	__func__.6876, 15
__func__.6876:
	.ascii	"ftl_map_blk_gc\000"
	.type	__func__.6947, %object
	.size	__func__.6947, 9
__func__.6947:
	.ascii	"log2phys\000"
	.type	__func__.7162, %object
	.size	__func__.7162, 16
__func__.7162:
	.ascii	"FtlReUsePrevPpa\000"
	.type	__func__.7315, %object
	.size	__func__.7315, 14
__func__.7315:
	.ascii	"ftl_check_vpc\000"
	.type	__func__.7042, %object
	.size	__func__.7042, 14
__func__.7042:
	.ascii	"FtlScanSysBlk\000"
	.type	__func__.7274, %object
	.size	__func__.7274, 18
__func__.7274:
	.ascii	"SupperBlkListInit\000"
	.type	__func__.7400, %object
	.size	__func__.7400, 25
__func__.7400:
	.ascii	"allocate_data_superblock\000"
	.type	__func__.7441, %object
	.size	__func__.7441, 16
__func__.7441:
	.ascii	"update_vpc_list\000"
	.type	__func__.7448, %object
	.size	__func__.7448, 20
__func__.7448:
	.ascii	"decrement_vpc_count\000"
	.type	__func__.7196, %object
	.size	__func__.7196, 22
__func__.7196:
	.ascii	"FtlRecoverySuperblock\000"
	.type	__func__.6627, %object
	.size	__func__.6627, 11
__func__.6627:
	.ascii	"FtlLoadBbt\000"
	.type	__func__.7020, %object
	.size	__func__.7020, 15
__func__.7020:
	.ascii	"FtlVpcTblFlush\000"
	.type	__func__.7299, %object
	.size	__func__.7299, 21
__func__.7299:
	.ascii	"FtlVpcCheckAndModify\000"
	.type	__func__.7421, %object
	.size	__func__.7421, 29
__func__.7421:
	.ascii	"allocate_new_data_superblock\000"
	.type	__func__.6404, %object
	.size	__func__.6404, 13
__func__.6404:
	.ascii	"FtlProgPages\000"
	.type	__func__.7519, %object
	.size	__func__.7519, 19
__func__.7519:
	.ascii	"FtlGcFreeTempBlock\000"
	.type	__func__.7625, %object
	.size	__func__.7625, 23
__func__.7625:
	.ascii	"rk_ftl_garbage_collect\000"
	.type	__func__.6432, %object
	.size	__func__.6432, 9
__func__.6432:
	.ascii	"FtlWrite\000"
	.type	__func__.7099, %object
	.size	__func__.7099, 15
__func__.7099:
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
