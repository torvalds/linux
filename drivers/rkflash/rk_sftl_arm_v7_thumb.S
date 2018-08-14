/* SPDX-License-Identifier: GPL-2.0 */

/*
 * Copyright (c) 2018 Rockchip Electronics Co. Ltd.
 * date: 2018-08-14
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
	mov	r7, r0
	ldr	r6, .L265
	add	r5, r7, #12
	.pad #20
	sub	sp, sp, #20
	ldr	r1, .L265+4
	bl	strcpy
	ldr	r2, [r6, #116]
	mov	r0, r5
	ldr	r1, .L265+8
	bl	sprintf
	add	r5, r5, r0
	ldr	r1, .L265+12
	mov	r0, r5
	adds	r5, r5, #10
	bl	strcpy
	ldr	r2, [r6, #2504]
	mov	r0, r5
	ldr	r1, .L265+16
	bl	sprintf
	add	r5, r5, r0
	ldr	r2, [r6, #2500]
	ldr	r1, .L265+20
	mov	r0, r5
	bl	sprintf
	add	r5, r5, r0
	ldr	r2, [r6, #2508]
	ldr	r1, .L265+24
	mov	r0, r5
	bl	sprintf
	add	r5, r5, r0
	ldr	r2, [r6, #2512]
	ldr	r1, .L265+28
	mov	r0, r5
	bl	sprintf
	add	r5, r5, r0
	ldr	r2, [r6, #2516]
	ldr	r1, .L265+32
	mov	r0, r5
	bl	sprintf
	add	r5, r5, r0
	ldr	r2, [r6, #2520]
	ldr	r1, .L265+36
	mov	r0, r5
	bl	sprintf
	add	r5, r5, r0
	ldr	r2, [r6, #2524]
	ldr	r1, .L265+40
	mov	r0, r5
	bl	sprintf
	add	r5, r5, r0
	ldr	r2, [r6, #2528]
	ldr	r1, .L265+44
	mov	r0, r5
	bl	sprintf
	ldr	r2, [r6, #2532]
	add	r5, r5, r0
	ldr	r1, .L265+48
	mov	r0, r5
	lsrs	r2, r2, #11
	bl	sprintf
	ldr	r2, [r6, #2536]
	add	r5, r5, r0
	ldr	r1, .L265+52
	mov	r0, r5
	lsrs	r2, r2, #11
	bl	sprintf
	add	r5, r5, r0
	ldr	r2, [r6, #2540]
	ldr	r1, .L265+56
	mov	r0, r5
	bl	sprintf
	add	r5, r5, r0
	ldr	r2, [r6, #2544]
	ldr	r1, .L265+60
	mov	r0, r5
	bl	sprintf
	add	r5, r5, r0
	ldrh	r2, [r6, #158]
	ldr	r1, .L265+64
	mov	r0, r5
	bl	sprintf
	add	r5, r5, r0
	ldrh	r2, [r6, #2344]
	ldr	r1, .L265+68
	mov	r0, r5
	bl	sprintf
	add	r5, r5, r0
	ldr	r2, [r6, #2548]
	ldr	r1, .L265+72
	mov	r0, r5
	bl	sprintf
	add	r5, r5, r0
	ldr	r2, [r6, #2552]
	ldr	r1, .L265+76
	mov	r0, r5
	bl	sprintf
	add	r5, r5, r0
	ldr	r2, [r6, #2556]
	ldr	r1, .L265+80
	mov	r0, r5
	bl	sprintf
	add	r5, r5, r0
	ldr	r2, [r6, #2560]
	ldr	r1, .L265+84
	mov	r0, r5
	bl	sprintf
	add	r5, r5, r0
	ldr	r2, [r6, #2564]
	ldr	r1, .L265+88
	mov	r0, r5
	bl	sprintf
	add	r5, r5, r0
	ldr	r2, [r6, #2568]
	ldr	r1, .L265+92
	mov	r0, r5
	bl	sprintf
	add	r5, r5, r0
	ldrh	r2, [r6, #2298]
	ldr	r1, .L265+96
	mov	r0, r5
	bl	sprintf
	add	r5, r5, r0
	ldrh	r2, [r6, #2296]
	ldr	r1, .L265+100
	mov	r0, r5
	bl	sprintf
	add	r5, r5, r0
	ldr	r2, [r6, #140]
	ldr	r1, .L265+104
	mov	r0, r5
	bl	sprintf
	add	r5, r5, r0
	ldr	r2, [r6, #132]
	ldr	r1, .L265+108
	mov	r0, r5
	bl	sprintf
	add	r5, r5, r0
	ldr	r2, [r6, #28]
	ldr	r1, .L265+112
	mov	r0, r5
	bl	sprintf
	add	r5, r5, r0
	ldrh	r2, [r6, #218]
	ldr	r1, .L265+116
	mov	r0, r5
	bl	sprintf
	add	r5, r5, r0
	ldrh	r2, [r6, #40]
	ldr	r1, .L265+120
	mov	r0, r5
	bl	sprintf
	add	r5, r5, r0
	ldrh	r2, [r6, #2572]
	ldr	r1, .L265+124
	mov	r0, r5
	bl	sprintf
	add	r5, r5, r0
	ldr	r2, [r6, #44]
	ldr	r1, .L265+128
	mov	r0, r5
	bl	sprintf
	add	r5, r5, r0
	ldrh	r2, [r6, #2576]
	ldr	r1, .L265+132
	mov	r0, r5
	bl	sprintf
	add	r5, r5, r0
	ldrh	r2, [r6, #152]
	ldr	r1, .L265+136
	mov	r0, r5
	bl	sprintf
	add	r5, r5, r0
	ldrh	r2, [r6, #2350]
	ldr	r1, .L265+140
	mov	r0, r5
	bl	sprintf
	add	r5, r5, r0
	ldrb	r2, [r6, #2354]	@ zero_extendqisi2
	ldr	r1, .L265+144
	mov	r0, r5
	bl	sprintf
	add	r5, r5, r0
	ldrh	r2, [r6, #2348]
	ldr	r1, .L265+148
	mov	r0, r5
	bl	sprintf
	add	r5, r5, r0
	ldrb	r2, [r6, #2356]	@ zero_extendqisi2
	ldr	r1, .L265+152
	mov	r0, r5
	bl	sprintf
	add	r5, r5, r0
	ldrh	r2, [r6, #2352]
	ldr	r1, .L265+156
	mov	r0, r5
	bl	sprintf
	ldr	r3, [r6, #2324]
	add	r5, r5, r0
	ldrh	r2, [r6, #2348]
	mov	r0, r5
	ldr	r1, .L265+160
	ldrh	r2, [r3, r2, lsl #1]
	bl	sprintf
	add	r5, r5, r0
	ldrh	r2, [r6, #2398]
	ldr	r1, .L265+164
	mov	r0, r5
	bl	sprintf
	add	r5, r5, r0
	ldrb	r2, [r6, #2402]	@ zero_extendqisi2
	ldr	r1, .L265+168
	mov	r0, r5
	bl	sprintf
	add	r5, r5, r0
	ldrh	r2, [r6, #2396]
	ldr	r1, .L265+172
	mov	r0, r5
	bl	sprintf
	add	r5, r5, r0
	ldrb	r2, [r6, #2404]	@ zero_extendqisi2
	ldr	r1, .L265+176
	mov	r0, r5
	bl	sprintf
	add	r5, r5, r0
	ldrh	r2, [r6, #2400]
	ldr	r1, .L265+180
	mov	r0, r5
	bl	sprintf
	ldr	r3, [r6, #2324]
	add	r5, r5, r0
	ldrh	r2, [r6, #2396]
	mov	r0, r5
	ldr	r1, .L265+184
	ldrh	r2, [r3, r2, lsl #1]
	bl	sprintf
	add	r5, r5, r0
	ldrh	r2, [r6, #2446]
	ldr	r1, .L265+188
	mov	r0, r5
	bl	sprintf
	add	r5, r5, r0
	ldrb	r2, [r6, #2450]	@ zero_extendqisi2
	ldr	r1, .L265+192
	mov	r0, r5
	bl	sprintf
	add	r5, r5, r0
	ldrh	r2, [r6, #2444]
	ldr	r1, .L265+196
	mov	r0, r5
	bl	sprintf
	add	r5, r5, r0
	ldrb	r2, [r6, #2452]	@ zero_extendqisi2
	ldr	r1, .L265+200
	mov	r0, r5
	bl	sprintf
	add	r5, r5, r0
	ldrh	r2, [r6, #2448]
	ldr	r1, .L265+204
	mov	r0, r5
	bl	sprintf
	add	r5, r5, r0
	ldrh	r2, [r6, #2590]
	ldr	r1, .L265+208
	mov	r0, r5
	bl	sprintf
	add	r5, r5, r0
	ldrb	r2, [r6, #2594]	@ zero_extendqisi2
	ldr	r1, .L265+212
	mov	r0, r5
	bl	sprintf
	add	r5, r5, r0
	ldrh	r2, [r6, #2588]
	ldr	r1, .L265+216
	mov	r0, r5
	bl	sprintf
	add	r5, r5, r0
	ldrb	r2, [r6, #2596]	@ zero_extendqisi2
	ldr	r1, .L265+220
	mov	r0, r5
	bl	sprintf
	add	r5, r5, r0
	ldrh	r2, [r6, #2592]
	ldr	r1, .L265+224
	mov	r0, r5
	bl	sprintf
	ldr	r3, [r6, #2712]
	add	r5, r5, r0
	ldr	r2, [r6, #2716]
	mov	r0, r5
	ldr	r1, .L265+228
	str	r3, [sp]
	ldr	r3, [r6, #2720]
	bl	sprintf
	adds	r4, r5, r0
	ldr	r2, [r6, #2708]
	ldr	r1, .L265+232
	mov	r0, r4
	bl	sprintf
	add	r4, r4, r0
	ldr	r2, [r6, #2732]
	ldr	r1, .L265+236
	mov	r0, r4
	bl	sprintf
	add	r4, r4, r0
	ldrh	r2, [r6, #3148]
	ldr	r1, .L265+240
	mov	r0, r4
	bl	sprintf
	add	r4, r4, r0
	ldrh	r2, [r6, #3150]
	ldr	r1, .L265+244
	mov	r0, r4
	bl	sprintf
	add	r4, r4, r0
	ldr	r2, [r6, #3152]
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
	.word	.LC57
	.word	.LC58
	.word	.LC59
	.word	.LC60
	.word	.LC61
	.word	.LC62
.L266:
	ldr	r1, .L268
	mov	r0, r4
	bl	sprintf
	add	r4, r4, r0
	ldrh	r2, [r6, #3156]
	ldr	r1, .L268+4
	mov	r0, r4
	bl	sprintf
	add	r4, r4, r0
	bl	GetFreeBlockMinEraseCount
	ldr	r1, .L268+8
	mov	r2, r0
	mov	r0, r4
	bl	sprintf
	add	r4, r4, r0
	ldrh	r0, [r6, #2344]
	bl	GetFreeBlockMaxEraseCount
	ldr	r1, .L268+12
	mov	r2, r0
	mov	r0, r4
	bl	sprintf
	ldr	r3, .L268+16
	add	r4, r4, r0
	ldr	r3, [r3]
	cmp	r3, #1
	beq	.L254
.L259:
	subs	r0, r4, r7
	add	sp, sp, #20
	@ sp needed
	pop	{r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L254:
	ldrh	r3, [r6, #2588]
	movw	r2, #65535
	cmp	r3, r2
	beq	.L256
	ldr	r2, [r6, #2324]
	mov	r0, r4
	ldr	r1, .L268+20
	ldrh	r2, [r2, r3, lsl #1]
	bl	sprintf
	add	r4, r4, r0
.L256:
	movs	r0, #0
	ldr	r9, .L268+28
	bl	List_get_gc_head_node
	uxth	r3, r0
	movs	r5, #0
	movw	fp, #65535
	mov	r10, #6
.L258:
	cmp	r3, fp
	beq	.L257
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
	bne	.L258
.L257:
	ldr	r2, [r6, #2316]
	movs	r5, #0
	ldr	r3, [r6, #2340]
	movw	r9, #65535
	ldr	fp, .L268+32
	mov	r10, #6
	subs	r3, r3, r2
	asrs	r2, r3, #1
	ldr	r3, .L268+24
	muls	r3, r2, r3
	uxth	r3, r3
.L260:
	cmp	r3, r9
	beq	.L259
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
	beq	.L259
	ldr	r3, [r6, #2316]
	ldrh	r3, [r3, r8]
	b	.L260
.L269:
	.align	2
.L268:
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
	ldr	r2, .L271
	ldr	r1, .L271+4
	bl	sprintf
	adds	r4, r5, r0
	mov	r0, r4
	bl	FtlPrintInfo2buf
	add	r0, r0, r4
	subs	r0, r0, r5
	pop	{r3, r4, r5, pc}
.L272:
	.align	2
.L271:
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
	ldr	r4, .L297
	ldr	r2, [r4, #2556]
	ldr	r3, [r4, #2568]
	cmp	r2, r3
	bcs	.L274
	ldrh	r1, [r4, #40]
	movs	r3, #0
	ldr	r0, [r4, #2328]
	str	r3, [r4, #2548]
.L275:
	cmp	r3, r1
	bcc	.L276
	ldr	r5, [r4, #2548]
	mov	r0, r5
	bl	__aeabi_uidiv
	str	r0, [r4, #2556]
	ldr	r0, [r4, #2552]
	ldrh	r1, [r4, #92]
	subs	r0, r5, r0
	bl	__aeabi_uidiv
	str	r0, [r4, #2548]
.L277:
	ldr	r6, [r4, #2568]
	ldr	r7, [r4, #2556]
	add	r3, r6, #256
	cmp	r3, r7
	bls	.L282
	ldr	r2, [r4, #2564]
	add	r3, r6, #768
	cmp	r3, r2
	bls	.L282
.L284:
	movw	r5, #65535
.L283:
	mov	r0, r5
	add	sp, sp, #28
	@ sp needed
	pop	{r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L276:
	ldrh	r2, [r0, r3, lsl #1]
	adds	r3, r3, #1
	ldr	r5, [r4, #2548]
	add	r2, r2, r5
	str	r2, [r4, #2548]
	b	.L275
.L274:
	ldr	r3, [r4, #2564]
	cmp	r2, r3
	bls	.L277
	adds	r3, r3, #1
	str	r3, [r4, #2564]
	movs	r3, #0
.L279:
	ldrh	r2, [r4, #40]
	cmp	r3, r2
	bcs	.L277
	ldr	r1, [r4, #2328]
	ldrh	r2, [r1, r3, lsl #1]
	adds	r2, r2, #1
	strh	r2, [r1, r3, lsl #1]	@ movhi
	adds	r3, r3, #1
	b	.L279
.L282:
	ldrh	r0, [r4, #2344]
	add	r0, r0, r0, lsl #1
	ubfx	r0, r0, #2, #16
	bl	GetFreeBlockMaxEraseCount
	add	r3, r6, #64
	mov	r9, r0
	cmp	r0, r3
	bcc	.L284
	ldr	r3, [r4, #2320]
	cmp	r3, #0
	beq	.L284
	ldrh	r2, [r4, #40]
	movw	r1, #65535
	ldr	lr, [r4, #2316]
	movs	r0, #0
	ldr	r8, [r4, #2328]
	mov	r10, r1
	str	r2, [sp, #20]
	mov	fp, #6
	mov	r2, r1
.L285:
	ldrh	ip, [r3]
	cmp	ip, r10
	bne	.L288
	mov	r5, r2
.L287:
	movw	r3, #65535
	cmp	r5, r3
	beq	.L284
	ldrh	r10, [r8, r5, lsl #1]
	lsl	fp, r5, #1
	cmp	r6, r10
	bcs	.L289
	bl	GetFreeBlockMinEraseCount
	cmp	r6, r0
	it	cc
	strcc	r1, [r4, #2568]
.L289:
	cmp	r7, r10
	bls	.L284
	add	r3, r10, #128
	cmp	r9, r3
	ble	.L284
	add	r3, r10, #256
	cmp	r7, r3
	bhi	.L290
	ldr	r3, [r4, #2564]
	add	r10, r10, #768
	cmp	r10, r3
	bcs	.L284
.L290:
	str	r9, [sp, #8]
	mov	r2, r7
	ldrh	r3, [r8, fp]
	mov	r1, r5
	ldr	r0, .L297+4
	str	r3, [sp, #4]
	ldr	r3, [r4, #2324]
	ldrh	r3, [r3, fp]
	str	r3, [sp]
	ldr	r3, [r4, #2564]
	bl	sftl_printk
	movs	r3, #1
	str	r3, [r4, #3160]
	b	.L283
.L288:
	adds	r0, r0, #1
	ldr	r5, [sp, #20]
	uxth	r0, r0
	cmp	r0, r5
	bhi	.L284
	ldrh	r5, [r3, #4]
	cbz	r5, .L286
	ldr	r5, .L297+8
	sub	r3, r3, lr
	asrs	r3, r3, #1
	muls	r3, r5, r3
	uxth	r5, r3
	ldrh	r3, [r8, r5, lsl #1]
	cmp	r6, r3
	bcs	.L287
	cmp	r1, r3
	itt	hi
	movhi	r1, r3
	movhi	r2, r5
.L286:
	mla	r3, fp, ip, lr
	b	.L285
.L298:
	.align	2
.L297:
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
	beq	.L300
	ldr	r2, .L301
	movs	r1, #0
	ldr	r2, [r2, #2324]
	strh	r1, [r2, r0, lsl #1]	@ movhi
	bl	INSERT_FREE_LIST
.L300:
	movs	r0, #0
	pop	{r3, pc}
.L302:
	.align	2
.L301:
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
	bne	.L304
	movw	r2, #2776
	ldr	r1, .L317
	ldr	r0, .L317+4
	bl	sftl_printk
.L304:
	ldr	r5, .L317+8
	ldrh	r2, [r4, #2]
	ldrh	r3, [r5, #102]
	cmp	r2, r3
	bne	.L305
	movw	r2, #2777
	ldr	r1, .L317
	ldr	r0, .L317+4
	bl	sftl_printk
.L305:
	ldrh	r3, [r4, #4]
	cbnz	r3, .L306
	movw	r2, #2778
	ldr	r1, .L317
	ldr	r0, .L317+4
	bl	sftl_printk
.L306:
	ldrb	r2, [r4, #6]	@ zero_extendqisi2
	movs	r3, #0
	ldrh	r1, [r5, #32]
	movw	r6, #65535
	strb	r3, [r4, #10]
	adds	r2, r2, #8
	ldrh	r0, [r4, r2, lsl #1]
	mov	r2, r3
.L307:
	cmp	r0, r6
	ldrb	r3, [r4, #6]	@ zero_extendqisi2
	beq	.L309
	ldrh	r2, [r4, #4]
	ldrh	r6, [r4, #2]
	subs	r2, r2, #1
	uxth	r2, r2
	orr	r6, r6, r0, lsl #10
	movw	r0, #65535
	strh	r2, [r4, #4]	@ movhi
.L311:
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
	beq	.L311
	strb	r3, [r4, #6]
	ldrh	r1, [r4, #2]
	ldrh	r3, [r5, #102]
	cmp	r1, r3
	bne	.L303
	cbz	r2, .L303
	movw	r2, #2799
	ldr	r1, .L317
	ldr	r0, .L317+4
	bl	sftl_printk
.L303:
	mov	r0, r6
	pop	{r3, r4, r5, r6, r7, pc}
.L309:
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
	b	.L307
.L318:
	.align	2
.L317:
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
	ldr	r3, .L328
	mov	ip, #20
	push	{r4, r5, r6, r7, r8, r9, lr}
	.save {r4, r5, r6, r7, r8, r9, lr}
	mov	r9, #12
	movs	r6, #1
	movs	r2, #0
	mov	r7, r3
	str	r2, [r3, #3164]
.L320:
	ldrh	r1, [r3, #32]
	uxth	r0, r2
	adds	r4, r2, #1
	cmp	r0, r1
	bcc	.L323
	ldr	r7, .L328
	mov	ip, #12
	movs	r6, #0
.L324:
	ldr	r2, [r3, #3184]
	cmp	r1, r2
	bcc	.L327
	pop	{r4, r5, r6, r7, r8, r9, pc}
.L323:
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
	b	.L320
.L327:
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
	b	.L324
.L329:
	.align	2
.L328:
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
	ldr	r3, .L337
	push	{r4, r5, r6, r7, r8, r9, r10, lr}
	.save {r4, r5, r6, r7, r8, r9, r10, lr}
	movs	r5, #0
	mov	r10, #20
	mov	lr, #12
	mov	r8, r5
	ldr	ip, [r3, #3184]
	ldr	r6, [r3, #3168]
.L331:
	uxth	r3, r5
	cmp	r1, r3
	bls	.L330
	mla	r4, r10, r3, r0
	movs	r2, #0
.L332:
	uxth	r3, r2
	cmp	ip, r3
	bls	.L333
	mul	r3, lr, r3
	ldr	r7, [r4, #8]
	adds	r2, r2, #1
	add	r9, r6, r3
	ldr	r3, [r6, r3]
	cmp	r3, r7
	bne	.L332
	str	r8, [r9, #8]
.L333:
	adds	r5, r5, #1
	b	.L331
.L330:
	pop	{r4, r5, r6, r7, r8, r9, r10, pc}
.L338:
	.align	2
.L337:
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
	ldr	r3, .L346
	mov	ip, #12
	push	{r4, r5, r6, r7, r8, r9, lr}
	.save {r4, r5, r6, r7, r8, r9, lr}
	movs	r4, #0
	mov	lr, #1
	mov	r8, #20
	ldr	r5, [r3, #3184]
	ldr	r6, [r3, #3168]
.L340:
	uxth	r2, r4
	cmp	r1, r2
	bhi	.L344
	pop	{r4, r5, r6, r7, r8, r9, pc}
.L344:
	mov	r9, #0
.L341:
	uxth	r3, r9
	cmp	r5, r3
	bls	.L342
	mla	r3, ip, r3, r6
	add	r9, r9, #1
	ldr	r7, [r3, #8]
	cmp	r7, #0
	bne	.L341
	mla	r2, r8, r2, r0
	ldr	r7, [r3]
	str	lr, [r3, #8]
	str	r7, [r2, #8]
	ldr	r3, [r3, #4]
	str	r3, [r2, #12]
.L342:
	adds	r4, r4, #1
	b	.L340
.L347:
	.align	2
.L346:
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
	ldr	r2, .L353
	ldr	r3, [r2, #3188]
	ldrh	r2, [r2, #3192]
	add	r2, r3, r2, lsl #1
.L349:
	cmp	r3, r2
	bne	.L351
	movs	r0, #0
	bx	lr
.L351:
	ldrh	r1, [r3], #2
	cmp	r1, r0
	bne	.L349
	movs	r0, #1
	bx	lr
.L354:
	.align	2
.L353:
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
	ldr	r3, .L359
	ldrh	r1, [r3, #3192]
	ldr	r2, [r3, #3188]
.L356:
	uxth	ip, r4
	cmp	ip, r1
	bcc	.L358
	bne	.L357
	strh	r0, [r2, ip, lsl #1]	@ movhi
	ldrh	r0, [r3, #3192]
	adds	r0, r0, #1
	strh	r0, [r3, #3192]	@ movhi
	b	.L357
.L358:
	adds	r4, r4, #1
	add	ip, r2, r4, lsl #1
	ldrh	ip, [ip, #-2]
	cmp	ip, r0
	bne	.L356
.L357:
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
.L360:
	.align	2
.L359:
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
	ldr	r0, .L366
	bl	sftl_printk
	ldr	r3, .L366+4
	ldrh	r6, [r3, #3202]
	cmp	r4, r6
	beq	.L362
	ldrh	r5, [r3, #3204]
	cmp	r4, r5
	beq	.L362
	ldrh	r0, [r3, #3206]
	cmp	r4, r0
	beq	.L362
	ldrh	r1, [r3, #3208]
	cmp	r4, r1
	beq	.L362
	movw	r2, #65535
	cmp	r6, r2
	bne	.L363
	strh	r4, [r3, #3202]	@ movhi
.L362:
	movs	r0, #0
	pop	{r4, r5, r6, pc}
.L363:
	cmp	r5, r2
	bne	.L364
	strh	r4, [r3, #3204]	@ movhi
	b	.L362
.L364:
	cmp	r0, r2
	bne	.L365
	strh	r4, [r3, #3206]	@ movhi
	b	.L362
.L365:
	cmp	r1, r2
	it	eq
	strheq	r4, [r3, #3208]	@ movhi
	b	.L362
.L367:
	.align	2
.L366:
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
	ldr	r4, .L372
	bl	P2V_block_in_plane
	mov	r2, r5
	mov	r6, r0
	ldrh	r1, [r4, #3210]
	ldr	r0, .L372+4
	bl	sftl_printk
	mov	r0, r6
	bl	FtlGcRefreshBlock
	ldrh	r3, [r4, #3210]
	movs	r2, #0
	addw	r0, r4, #3212
.L369:
	uxth	r1, r2
	cmp	r3, r1
	bhi	.L371
	cmp	r3, #15
	itttt	ls
	addls	r2, r3, #1
	strhls	r2, [r4, #3210]	@ movhi
	addls	r4, r4, r3, lsl #1
	strhls	r5, [r4, #3212]	@ movhi
	b	.L370
.L371:
	adds	r2, r2, #1
	add	r1, r0, r2, lsl #1
	ldrh	r1, [r1, #-2]
	cmp	r1, r5
	bne	.L369
.L370:
	movs	r0, #0
	pop	{r4, r5, r6, pc}
.L373:
	.align	2
.L372:
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
	ldr	r4, .L380
	ldrh	r3, [r4, #3210]
	cbz	r3, .L375
	ldrh	r1, [r4, #3202]
	movw	r2, #65535
	cmp	r1, r2
	bne	.L375
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
.L375:
	movs	r0, #0
	pop	{r4, pc}
.L381:
	.align	2
.L380:
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
	ldr	r1, .L383
	b	__kmalloc
.L384:
	.align	2
.L383:
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
	ldr	fp, .L395+16
	mov	r10, r0
	mov	r7, r1
	mov	r8, r2
	mov	r9, r3
	mov	r5, r6
	mov	r4, r6
.L387:
	cmp	r4, r9
	bcc	.L393
	ldr	r0, .L395
	pop	{r3, r4, r5, r6, r7, r8, r9, r10, fp, lr}
	b	sftl_printk
.L393:
	cbnz	r5, .L388
	mov	r3, r6
	mov	r2, r7
	mov	r1, r10
	ldr	r0, .L395+4
	bl	sftl_printk
.L388:
	cmp	r8, #4
	bne	.L389
	ldr	r1, [r7, r4, lsl #2]
	ldr	r0, .L395+8
.L394:
	adds	r5, r5, #1
	bl	sftl_printk
	cmp	r5, #15
	bls	.L392
	movs	r5, #0
	ldr	r0, .L395
	bl	sftl_printk
.L392:
	adds	r4, r4, #1
	add	r6, r6, r8
	b	.L387
.L389:
	cmp	r8, #2
	ittee	eq
	ldrheq	r1, [r7, r4, lsl #1]
	moveq	r0, fp
	ldrbne	r1, [r7, r4]	@ zero_extendqisi2
	ldrne	r0, .L395+12
	b	.L394
.L396:
	.align	2
.L395:
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
	ldr	r5, .L415
	mov	r4, r0
	movs	r6, #0
	ldr	r10, .L415+4
	ldrh	r9, [r5, #12]
	ldr	fp, .L415+8
.L398:
	cmp	r6, r8
	bne	.L405
	movs	r0, #0
	add	sp, sp, #12
	@ sp needed
	pop	{r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L405:
	ldr	r3, [r4, #8]
	cbz	r3, .L399
	ldr	r3, [r4, #12]
	cbnz	r3, .L400
.L399:
	movs	r2, #101
	mov	r1, r10
	mov	r0, fp
	bl	sftl_printk
.L400:
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
	bne	.L402
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
	beq	.L403
	ldr	r3, [r4, #12]
	ldr	r2, [r3, #12]
	adds	r2, r2, #1
	bne	.L402
	ldr	r2, [r3, #8]
	adds	r2, r2, #1
	bne	.L402
	ldr	r3, [r3]
	adds	r3, r3, #1
	beq	.L402
.L403:
	mov	r3, #-1
	str	r3, [r4]
.L402:
	adds	r6, r6, #1
	adds	r4, r4, #20
	b	.L398
.L416:
	.align	2
.L415:
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
	ldr	r4, .L427
	movw	r10, #61664
	ldr	r3, [r4, #3284]
	add	r7, r4, #162
	ldr	r8, [r4, #3288]
	add	r9, r4, #3264
	str	r3, [r4, #3272]
	str	r8, [r4, #3276]
.L418:
	ldrh	r3, [r4, #54]
	cmp	r6, r3
	bcc	.L423
	movs	r0, #0
	pop	{r4, r5, r6, r7, r8, r9, r10, pc}
.L423:
	ldrh	r5, [r4, #98]
	movw	r3, #65535
	strh	r3, [r7, #2]!	@ movhi
.L420:
	ldrh	r3, [r4, #98]
	subs	r5, r5, #1
	uxth	r5, r5
	sub	r2, r3, #16
	cmp	r5, r2
	ble	.L421
	mla	r3, r6, r3, r5
	movs	r2, #1
	mov	r1, r2
	mov	r0, r9
	lsls	r3, r3, #10
	str	r3, [r4, #3268]
	bl	FlashReadPages
	ldr	r3, [r4, #3264]
	adds	r3, r3, #1
	beq	.L420
	ldrh	r3, [r8]
	cmp	r3, r10
	bne	.L420
	strh	r5, [r7]	@ movhi
.L421:
	adds	r6, r6, #1
	b	.L418
.L428:
	.align	2
.L427:
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
	ldr	r3, .L440
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
	ldr	r3, .L440+4
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
	bne	.L432
	mov	r8, #0
.L433:
	cmp	r8, r5
	ble	.L436
.L432:
	mov	r0, r5
	add	sp, sp, #88
	@ sp needed
	pop	{r4, r5, r6, r7, r8, pc}
.L436:
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
	bne	.L434
	ldr	r3, [sp, #28]
	adds	r3, r3, #1
	bne	.L434
	ldr	r3, [sp, #4]
	adds	r3, r3, #1
	beq	.L434
	subs	r4, r4, #1
	sxth	r5, r4
	b	.L433
.L434:
	adds	r4, r4, #1
	sxth	r8, r4
	b	.L433
.L441:
	.align	2
.L440:
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
	ldr	r6, .L472
	mov	r9, r2
	mov	r4, r0
	mov	r5, r0
	mov	r8, #0
	.pad #44
	sub	sp, sp, #44
	ldrh	r10, [r6, #12]
	str	r3, [sp, #4]
.L443:
	cmp	r8, fp
	bne	.L451
	ldr	r3, [sp, #4]
	cmp	r3, #0
	bne	.L458
.L471:
	movs	r0, #0
	add	sp, sp, #44
	@ sp needed
	pop	{r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L451:
	ldr	r3, [r5, #8]
	cbz	r3, .L444
	ldr	r3, [r5, #12]
	cbnz	r3, .L445
.L444:
	movs	r2, #131
	ldr	r1, .L472+4
	ldr	r0, .L472+8
	bl	sftl_printk
.L445:
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
	cbnz	r0, .L446
	str	r0, [r5]
.L447:
	ldrh	r3, [r6, #14]
	cmp	r3, #4
	bne	.L449
	ldr	r3, [r5, #12]
	ldr	r2, [r5, #8]
	ldr	r1, [sp, #16]
	adds	r3, r3, #8
	ldrb	r0, [sp, #12]	@ zero_extendqisi2
	add	r2, r2, #2048
	ldr	r7, [r6, #3256]
	add	r1, r1, r10
	blx	r7
	cbz	r0, .L449
	mov	r3, #-1
	str	r3, [r5]
.L449:
	add	r8, r8, #1
	adds	r5, r5, #20
	b	.L443
.L446:
	mov	r3, #-1
	str	r3, [r5]
	b	.L447
.L456:
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
	bne	.L453
	ldr	r1, [r4, #4]
	mov	r0, fp
	str	r3, [sp, #4]
	bl	sftl_printk
	ldr	r3, [sp, #4]
	str	r3, [r4]
.L453:
	ldr	r3, [r4, #12]
	cbz	r3, .L454
	ldr	r2, [r3]
	ldr	r3, [r6, #3292]
	cmp	r2, r3
	beq	.L454
	ldr	r1, [r4, #4]
	ldr	r0, .L472+12
	bl	sftl_printk
	mov	r3, #-1
	str	r3, [r4]
.L454:
	ldr	r3, [r4, #8]
	cbz	r3, .L455
	ldr	r2, [r3]
	ldr	r3, [r7]
	cmp	r2, r3
	beq	.L455
	ldr	r1, [r4, #4]
	ldr	r0, .L472+16
	bl	sftl_printk
	mov	r3, #-1
	str	r3, [r4]
.L455:
	adds	r5, r5, #1
	adds	r4, r4, #20
.L452:
	cmp	r8, r5
	bne	.L456
	b	.L471
.L458:
	movs	r5, #0
	ldr	r7, .L472+20
	ldr	r10, .L472+24
	ldr	fp, .L472+28
	b	.L452
.L473:
	.align	2
.L472:
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
	ldr	r4, .L485
	mov	r5, r0
	movs	r6, #0
	mov	r9, #-1
	ldrh	r8, [r4, #12]
.L475:
	cmp	r6, r7
	bne	.L481
	movs	r0, #0
	add	sp, sp, #12
	@ sp needed
	pop	{r4, r5, r6, r7, r8, r9, pc}
.L481:
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
	bne	.L479
	ldr	r1, [sp, #4]
	ldr	r3, [r4, #3252]
	ldrb	r0, [sp]	@ zero_extendqisi2
	add	r1, r1, r8
	blx	r3
	cbz	r0, .L479
	str	r9, [r5]
.L479:
	adds	r6, r6, #1
	adds	r5, r5, #20
	b	.L475
.L486:
	.align	2
.L485:
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
	bhi	.L487
	ldr	r4, .L496
	ldrh	r3, [r4, #218]
	cmp	r3, #1024
	beq	.L487
	cbz	r1, .L489
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
.L489:
	ldrh	r3, [r4, #218]
	adds	r3, r3, #1
	strh	r3, [r4, #218]	@ movhi
	ldrh	r3, [r4, #216]
	add	r2, r4, r3, lsl #1
	adds	r3, r3, #1
	ubfx	r3, r3, #0, #10
	strh	r5, [r2, #220]	@ movhi
	strh	r3, [r4, #216]	@ movhi
.L487:
	pop	{r4, r5, r6, pc}
.L497:
	.align	2
.L496:
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
	ldr	r4, .L543
	mov	r7, r1
	mov	r6, r10
	mov	r5, r10
	mov	r9, #20
	.pad #20
	sub	sp, sp, #20
	mov	fp, r4
	str	r0, [sp]
	str	r0, [r4, #3808]
.L499:
	ldrh	r1, [r4, #32]
	uxth	r3, r10
	cmp	r1, r3
	bhi	.L504
	cmp	r6, #0
	beq	.L498
	mov	r8, #0
	mov	r9, #20
	mov	r2, r6
	movs	r1, #0
	ldr	r0, [r4, #3804]
	bl	FlashEraseBlocks
.L507:
	uxth	r3, r8
	cmp	r6, r3
	bhi	.L509
	cmp	r7, #0
	beq	.L526
	ldrh	r3, [r4, #104]
	mov	r10, #1
	str	r3, [sp, #4]
	lsrs	r3, r3, #2
	str	r3, [sp, #8]
.L510:
	mov	r8, #0
	mov	fp, #20
.L520:
	mov	r9, #0
	mov	r6, r9
.L511:
	ldrh	r1, [r4, #32]
	uxth	r3, r9
	cmp	r1, r3
	bhi	.L515
	cbz	r6, .L498
	mov	r9, #0
	movs	r3, #1
	mov	r2, r10
	mov	r1, r6
	ldr	r0, [r4, #3804]
	bl	FlashProgPages
.L517:
	uxth	r3, r9
	cmp	r6, r3
	bhi	.L519
	ldr	r3, [sp, #8]
	add	r8, r8, r3
	ldr	r3, [sp, #4]
	uxth	r8, r8
	cmp	r3, r8
	bhi	.L520
	mov	r8, #0
	mov	r9, #20
.L521:
	uxth	r3, r8
	cmp	r6, r3
	bhi	.L523
	ldr	r3, [sp]
	cmp	r3, #63
	bls	.L524
	cbz	r7, .L498
.L524:
	mov	r2, r6
	mov	r1, r10
	ldr	r0, [r4, #3804]
	bl	FlashEraseBlocks
.L498:
	mov	r0, r5
	add	sp, sp, #20
	@ sp needed
	pop	{r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L504:
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
	cbz	r7, .L500
	bl	IsBlkInVendorPart
	cbnz	r0, .L501
.L500:
	mov	r0, r8
	bl	FtlBbmIsBadBlock
	cbnz	r0, .L502
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
.L501:
	add	r10, r10, #1
	b	.L499
.L502:
	adds	r5, r5, #1
	uxth	r5, r5
	b	.L501
.L509:
	mul	r3, r9, r8
	ldr	r2, [r4, #3804]
	adds	r1, r2, r3
	ldr	r3, [r2, r3]
	adds	r3, r3, #1
	bne	.L508
	ldr	r0, [r1, #4]
	adds	r5, r5, #1
	uxth	r5, r5
	ubfx	r0, r0, #10, #16
	bl	FtlBbmMapBadBlock
.L508:
	add	r8, r8, #1
	b	.L507
.L526:
	movs	r3, #6
	mov	r10, r7
	str	r3, [sp, #8]
	movs	r3, #1
	str	r3, [sp, #4]
	b	.L510
.L515:
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
	cbz	r7, .L512
	bl	IsBlkInVendorPart
	cbnz	r0, .L513
.L512:
	ldr	r0, [sp, #12]
	bl	FtlBbmIsBadBlock
	cbnz	r0, .L513
	ldr	r1, [r4, #3804]
	ldr	r3, [sp, #12]
	ldr	r2, .L543
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
.L513:
	add	r9, r9, #1
	b	.L511
.L519:
	mul	r3, fp, r9
	ldr	r2, [r4, #3804]
	adds	r1, r2, r3
	ldr	r3, [r2, r3]
	cbz	r3, .L518
	ldr	r0, [r1, #4]
	adds	r5, r5, #1
	uxth	r5, r5
	ubfx	r0, r0, #10, #16
	bl	FtlBbmMapBadBlock
.L518:
	add	r9, r9, #1
	b	.L517
.L523:
	cbz	r7, .L522
	mul	r3, r9, r8
	ldr	r2, [r4, #3804]
	adds	r1, r2, r3
	ldr	r3, [r2, r3]
	cbnz	r3, .L522
	ldr	r0, [r1, #4]
	movs	r1, #1
	ubfx	r0, r0, #10, #16
	bl	FtlFreeSysBlkQueueIn
.L522:
	add	r8, r8, #1
	b	.L521
.L544:
	.align	2
.L543:
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
	ldr	r4, .L552
	ldr	r6, .L552+4
.L546:
	ldrh	r1, [r4, #218]
	cbz	r1, .L547
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
	bhi	.L548
	mov	r0, r5
	pop	{r3, r4, r5, r6, r7, pc}
.L547:
	ldr	r0, .L552+8
	bl	sftl_printk
.L550:
	b	.L550
.L548:
	ldrh	r2, [r4, #218]
	mov	r1, r5
	mov	r0, r6
	bl	sftl_printk
	b	.L546
.L553:
	.align	2
.L552:
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
.L555:
	uxth	r5, r3
	cmp	r5, r1
	bcs	.L558
	mov	r7, r2
	adds	r3, r3, #1
	ldrh	r6, [r7]
	adds	r2, r2, #2
	cmp	r6, #0
	bne	.L555
	bl	FtlFreeSysBlkQueueOut
	subs	r3, r0, #1
	movw	r2, #65533
	uxth	r3, r3
	mov	r1, r0
	strh	r0, [r7]	@ movhi
	cmp	r3, r2
	bls	.L556
	ldr	r3, .L562
	ldr	r0, .L562+4
	ldrh	r2, [r3, #218]
	bl	sftl_printk
.L557:
	b	.L557
.L556:
	ldr	r3, [r4, #28]
	strh	r6, [r4, #2]	@ movhi
	strh	r5, [r4]	@ movhi
	adds	r3, r3, #1
	str	r3, [r4, #28]
	ldrh	r3, [r4, #8]
	adds	r3, r3, #1
	strh	r3, [r4, #8]	@ movhi
.L558:
	ldrh	r3, [r4, #10]
	cmp	r3, r5
	bhi	.L560
	movw	r2, #603
	ldr	r1, .L562+8
	ldr	r0, .L562+12
	bl	sftl_printk
.L560:
	movs	r0, #0
	pop	{r3, r4, r5, r6, r7, pc}
.L563:
	.align	2
.L562:
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
	bls	.L566
	ldr	r5, .L567
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
.L564:
	mov	r0, r4
	add	sp, sp, #92
	@ sp needed
	pop	{r4, r5, pc}
.L566:
	movs	r4, #0
	b	.L564
.L568:
	.align	2
.L567:
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
	ldr	r5, .L576
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
	bls	.L570
	mov	r2, #256
	movs	r1, #255
	mov	r0, r4
	bl	memset
	movs	r0, #0
.L570:
	ldrh	r3, [r5, #14]
	cmp	r3, #4
	bne	.L575
	add	r1, r4, r0, lsl #1
	mov	r3, r4
.L572:
	cmp	r3, r1
	bne	.L573
.L575:
	pop	{r4, r5, r6, pc}
.L573:
	ldrh	r2, [r3]
	lsrs	r2, r2, #1
	strh	r2, [r3], #2	@ movhi
	b	.L572
.L577:
	.align	2
.L576:
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
	cbz	r2, .L579
	bl	memset
.L579:
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
	ldr	r4, .L684
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
	ldr	r0, .L684+4
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
.L584:
	cmp	r3, r6
	bcc	.L585
	ldr	r2, .L684+8
	movs	r1, #0
	add	r3, r2, r3, lsl #2
	adds	r2, r2, #56
	adds	r3, r3, #24
.L586:
	cmp	r2, r3
	bne	.L587
	ldr	r3, [r4, #3872]
	cbnz	r3, .L588
.L590:
	ldr	r1, .L684+12
	ldr	r0, .L684+16
	bl	sftl_printk
	mov	r0, #-1
	pop	{r4, r5, r6, r7, r8, pc}
.L585:
	ldr	r0, [r4, #180]
	adds	r3, r3, #1
	add	r0, r0, r1
	add	r1, r1, r2
	str	r0, [r5, #4]!
	b	.L584
.L587:
	str	r1, [r3, #4]!
	b	.L586
.L685:
	.align	2
.L684:
	.word	.LANCHOR0
	.word	33553920
	.word	.LANCHOR0+152
	.word	.LANCHOR1+186
	.word	.LC86
.L588:
	ldr	r3, [r4, #3876]
	cmp	r3, #0
	beq	.L590
	ldr	r3, [r4, #3892]
	cmp	r3, #0
	beq	.L590
	ldr	r3, [r4, #3896]
	cmp	r3, #0
	beq	.L590
	ldr	r3, [r4, #2492]
	cmp	r3, #0
	beq	.L590
	ldr	r3, [r4, #3900]
	cmp	r3, #0
	beq	.L590
	ldr	r3, [r4, #2316]
	cmp	r3, #0
	beq	.L590
	ldr	r3, [r4, #180]
	cmp	r3, #0
	beq	.L590
	ldr	r3, [r4, #2324]
	cmp	r3, #0
	beq	.L590
	ldr	r3, [r4, #3188]
	cmp	r3, #0
	beq	.L590
	ldr	r3, [r4, #3196]
	cmp	r3, #0
	beq	.L590
	ldr	r3, [r4, #3836]
	cmp	r3, #0
	beq	.L590
	ldr	r3, [r4, #3844]
	cmp	r3, #0
	beq	.L590
	ldr	r3, [r4, #3804]
	cmp	r3, #0
	beq	.L590
	ldr	r3, [r4, #3180]
	cmp	r3, #0
	beq	.L590
	ldr	r3, [r4, #3840]
	cmp	r3, #0
	beq	.L590
	ldr	r3, [r4, #3284]
	cmp	r3, #0
	beq	.L590
	ldr	r3, [r4, #3848]
	cmp	r3, #0
	beq	.L590
	ldr	r3, [r4, #3852]
	cmp	r3, #0
	beq	.L590
	ldr	r3, [r4, #3172]
	cmp	r3, #0
	beq	.L590
	ldr	r3, [r4, #3820]
	cmp	r3, #0
	beq	.L590
	ldr	r3, [r4, #3812]
	cmp	r3, #0
	beq	.L590
	ldr	r3, [r4, #3168]
	cmp	r3, #0
	beq	.L590
	ldr	r3, [r4, #3288]
	cmp	r3, #0
	beq	.L590
	ldr	r3, [r4, #3816]
	cmp	r3, #0
	beq	.L590
	ldr	r3, [r4, #3176]
	cmp	r3, #0
	beq	.L590
	ldr	r3, [r4, #2328]
	cmp	r3, #0
	beq	.L590
	ldr	r3, [r4, #3860]
	cmp	r3, #0
	beq	.L590
	ldr	r3, [r4, #148]
	cmp	r3, #0
	beq	.L590
	ldr	r3, .L686
	ldr	r2, [r3, #3880]
	cmp	r2, #0
	beq	.L590
	ldr	r2, [r3, #3884]
	cmp	r2, #0
	beq	.L590
	ldr	r3, [r3, #3888]
	cmp	r3, #0
	beq	.L590
	movs	r0, #0
	pop	{r4, r5, r6, r7, r8, pc}
.L687:
	.align	2
.L686:
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
	ldr	r6, .L694
	mov	r7, r1
	subs	r4, r5, #2
	movw	r8, #65535
	ldr	r9, .L694+4
	addw	r5, r5, #1022
	ldrh	r2, [r6, #3904]
	movs	r1, #0
	ldr	r10, .L694+8
	mov	r0, r7
	lsls	r2, r2, #2
	bl	ftl_memset
.L691:
	ldrh	r3, [r4, #2]
	cmp	r3, r8
	beq	.L688
	ldrh	r2, [r6, #98]
	cmp	r2, r3
	bhi	.L690
	movs	r2, #74
	mov	r1, r9
	mov	r0, r10
	bl	sftl_printk
.L690:
	ldrh	r3, [r4, #2]!
	movs	r2, #1
	cmp	r5, r4
	lsr	r1, r3, #5
	and	r3, r3, #31
	lsl	r3, r2, r3
	ldr	r2, [r7, r1, lsl #2]
	orr	r2, r2, r3
	str	r2, [r7, r1, lsl #2]
	bne	.L691
.L688:
	pop	{r4, r5, r6, r7, r8, r9, r10, pc}
.L695:
	.align	2
.L694:
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
.L697:
	ldrh	r1, [r4, #6]
	uxth	r2, r3
	cmp	r1, r2
	bhi	.L701
	ldrh	fp, [r5]
	movs	r6, #0
	ldr	r2, .L710
	mov	r10, r6
.L702:
	ldrh	r1, [r4, #10]
	uxth	r3, r6
	cmp	r1, r3
	bhi	.L706
	mov	r0, r10
	add	sp, sp, #12
	@ sp needed
	pop	{r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L701:
	uxth	r2, r3
	ldr	r1, [r6, r2, lsl #2]
	movs	r2, #0
	ubfx	r1, r1, #10, #16
.L698:
	ldrh	r7, [r4, #10]
	uxth	r0, r2
	cmp	r7, r0
	bhi	.L700
	adds	r3, r3, #1
	b	.L697
.L700:
	uxth	r0, r2
	adds	r2, r2, #1
	ldrh	r7, [r9, r0, lsl #1]
	cmp	r7, r1
	ittt	eq
	ldrheq	r7, [r5, r0, lsl #1]
	addeq	r7, r7, #1
	strheq	r7, [r5, r0, lsl #1]	@ movhi
	b	.L698
.L706:
	ldrh	r1, [r4]
	uxth	r7, r6
	cmp	r1, r3
	bne	.L703
	ldrh	r1, [r2, #104]
	ldrh	r0, [r4, #2]
	cmp	r0, r1
	it	cc
	strhcc	r1, [r5, r7, lsl #1]	@ movhi
.L703:
	ldrh	r8, [r5, r7, lsl #1]
	cmp	fp, r8
	itt	hi
	movhi	r10, r3
	movhi	fp, r8
	cmp	r8, #0
	bne	.L705
	ldrh	r0, [r9, r7, lsl #1]
	cbz	r0, .L705
	movs	r1, #1
	str	r2, [sp, #4]
	bl	FtlFreeSysBlkQueueIn
	strh	r8, [r9, r7, lsl #1]	@ movhi
	ldr	r2, [sp, #4]
	ldrh	r3, [r4, #8]
	subs	r3, r3, #1
	strh	r3, [r4, #8]	@ movhi
.L705:
	adds	r6, r6, #1
	b	.L702
.L711:
	.align	2
.L710:
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
	bne	.L713
	ldrh	r3, [r0, #8]
	cbz	r3, .L714
	movw	r2, #677
	ldr	r1, .L722
	ldr	r0, .L722+4
	bl	sftl_printk
.L714:
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
.L715:
	movs	r0, #0
	pop	{r3, r4, r5, r6, r7, pc}
.L713:
	ldrh	r6, [r5, r3, lsl #1]
	movs	r1, #255
	ldrh	r3, [r0, #2]
	ldr	r5, .L722+8
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
.L716:
	ldrh	r0, [r4, #6]
	uxth	r1, r2
	cmp	r0, r1
	bhi	.L718
	movs	r2, #1
	movs	r3, #0
	mov	r1, r2
	ldr	r0, .L722+12
	bl	FlashProgPages
	ldrh	r3, [r4, #2]
	mov	r0, r4
	adds	r3, r3, #1
	strh	r3, [r4, #2]	@ movhi
	bl	ftl_map_blk_gc
	b	.L715
.L718:
	uxth	r1, r2
	ldr	r0, [r7, r1, lsl #2]
	cmp	r6, r0, lsr #10
	bne	.L717
	ldr	r0, [r5, #3284]
	adds	r3, r3, #1
	uxth	r3, r3
	str	r1, [r0, r3, lsl #3]
	ldr	r0, [r7, r1, lsl #2]
	ldr	r1, [r5, #3284]
	add	r1, r1, r3, lsl #3
	str	r0, [r1, #4]
.L717:
	adds	r2, r2, #1
	b	.L716
.L723:
	.align	2
.L722:
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
	ldr	r7, .L753
	mov	r9, r1
	mov	fp, r2
	movs	r6, #0
	ldr	r10, .L753+20
	mov	r5, r7
.L725:
	ldr	r3, [r7, #2524]
	adds	r3, r3, #1
	str	r3, [r7, #2524]
	ldrh	r3, [r7, #104]
	ldrh	r2, [r4, #2]
	subs	r3, r3, #1
	cmp	r2, r3
	bge	.L726
	ldrh	r2, [r4]
	movw	r3, #65535
	cmp	r2, r3
	bne	.L727
.L726:
	mov	r0, r4
	bl	Ftl_write_map_blk_to_last_page
.L727:
	ldrh	r2, [r4]
	ldr	r3, [r4, #12]
	ldrh	r3, [r3, r2, lsl #1]
	cbnz	r3, .L728
	movw	r2, #735
	mov	r1, r10
	ldr	r0, .L753+4
	bl	sftl_printk
.L728:
	ldrh	r2, [r4]
	ldrh	r3, [r4, #10]
	cmp	r2, r3
	bcc	.L729
	mov	r2, #736
	mov	r1, r10
	ldr	r0, .L753+4
	bl	sftl_printk
.L729:
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
	ldr	r0, .L753+8
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
	bne	.L730
	ldr	r1, [r5, #3268]
	adds	r6, r6, #1
	ldr	r0, .L753+12
	uxth	r6, r6
	bl	sftl_printk
	ldrh	r3, [r4, #2]
	cmp	r3, #2
	ittt	ls
	ldrhls	r3, [r5, #104]
	addls	r3, r3, #-1
	strhls	r3, [r4, #2]	@ movhi
	cmp	r6, #3
	bls	.L732
	mov	r2, r6
	ldr	r1, [r5, #3268]
	ldr	r0, .L753+16
	bl	sftl_printk
.L733:
	b	.L733
.L732:
	ldr	r3, [r4, #32]
	cmp	r3, #0
	beq	.L725
.L750:
	b	.L750
.L730:
	cbz	r2, .L736
	cmp	r3, #1
	strh	r8, [r4, #40]	@ movhi
	beq	.L725
	cmp	r2, #256
.L752:
	beq	.L725
	ldr	r2, [r5, #3268]
	movs	r0, #0
	ldr	r3, [r4, #24]
	str	r2, [r3, r9, lsl #2]
	pop	{r3, r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L736:
	cmp	r3, #1
	b	.L752
.L754:
	.align	2
.L753:
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
	ldr	r4, .L762
	mov	r8, r1
	ldrh	r3, [r4, #136]
	cmp	r3, r0
	bcs	.L756
	mov	r2, #510
	ldr	r1, .L762+4
	ldr	r0, .L762+8
	bl	sftl_printk
.L756:
	ldr	r3, [r4, #3892]
	movs	r5, #12
	ldr	r7, [r3, r6, lsl #2]
	cbnz	r7, .L757
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
.L758:
	movs	r0, #0
	pop	{r4, r5, r6, r7, r8, pc}
.L757:
	mul	r5, r5, r8
	ldr	r3, [r4, #2492]
	movs	r2, #1
	ldr	r0, .L762+12
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
	bne	.L759
	mov	r2, r7
	mov	r1, r6
	ldr	r0, .L762+16
	bl	sftl_printk
	lsrs	r3, r7, #10
	mov	r1, r6
	ldr	r0, .L762+20
	strh	r3, [r4, #3948]	@ movhi
	ldr	r3, [r4, #2492]
	add	r3, r3, r5
	ldr	r2, [r3, #8]
	bl	FtlMapWritePage
.L759:
	ldrh	r3, [r8, #8]
	cmp	r3, r6
	beq	.L760
	mov	r2, r7
	mov	r1, r6
	ldr	r0, .L762+24
	bl	sftl_printk
	movs	r3, #4
	ldr	r1, [r4, #3276]
	mov	r2, r3
	ldr	r0, .L762+28
	bl	rknand_print_hex
	ldrh	r3, [r4, #136]
	movs	r2, #4
	ldr	r1, [r4, #3892]
	ldr	r0, .L762+32
	bl	rknand_print_hex
.L760:
	ldrh	r3, [r8, #8]
	cmp	r3, r6
	beq	.L761
	movw	r2, #535
	ldr	r1, .L762+4
	ldr	r0, .L762+8
	bl	sftl_printk
.L761:
	ldr	r3, [r4, #2492]
	movs	r1, #0
	adds	r2, r3, r5
	str	r1, [r2, #4]
	strh	r6, [r3, r5]	@ movhi
	b	.L758
.L763:
	.align	2
.L762:
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
	ldr	r5, .L788
	cmp	r2, r3
	bge	.L765
	movw	r3, #65535
	cmp	r1, r3
	beq	.L767
	ldrh	r2, [r4, #2]
	ldrh	r3, [r5, #104]
	cmp	r2, r3
	bcc	.L768
.L765:
	movw	r3, #65535
	uxth	r0, r0
	cmp	r1, r3
	beq	.L769
	ldrh	r3, [r4, #2]
	ldrh	r2, [r5, #104]
	cmp	r2, r3
	bls	.L770
.L769:
	ldrh	r9, [r6, r0, lsl #1]
	lsls	r2, r0, #1
	cmp	r9, #0
	beq	.L767
	ldr	r3, [r4, #32]
	cbnz	r3, .L767
	movs	r1, #1
	str	r1, [r4, #32]
	strh	r3, [r6, r2]	@ movhi
	ldrh	r3, [r4, #8]
	ldrh	r2, [r4, #2]
	subs	r3, r3, #1
	strh	r3, [r4, #8]	@ movhi
	ldrh	r3, [r5, #104]
	cmp	r2, r3
	bcc	.L775
	mov	r0, r4
	bl	ftl_map_blk_alloc_new_blk
.L775:
	movs	r6, #0
.L776:
	ldrh	r2, [r4, #6]
	uxth	fp, r6
	cmp	r2, fp
	bhi	.L783
	movs	r1, #1
	mov	r0, r9
	bl	FtlFreeSysBlkQueueIn
	movs	r3, #0
	str	r3, [r4, #32]
.L767:
	ldrh	r2, [r4, #2]
	ldrh	r3, [r5, #104]
	cmp	r2, r3
	bcc	.L768
	mov	r0, r4
	bl	ftl_map_blk_alloc_new_blk
.L768:
	movs	r0, #0
	add	sp, sp, #12
	@ sp needed
	pop	{r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L773:
	ldrh	r8, [r6, r2, lsl #1]
	add	lr, r2, #1
	cmp	r8, r1
	beq	.L771
	mov	r2, lr
.L772:
	uxth	r7, r2
	cmp	r7, ip
	bcc	.L773
	mov	r7, r0
.L771:
	uxth	r2, r2
	ldr	r0, .L788+4
	ldrh	r2, [r6, r2, lsl #1]
	bl	sftl_printk
	movw	r3, #65535
	mov	r0, r7
	strh	r3, [r4, #40]	@ movhi
	b	.L769
.L770:
	movs	r2, #0
	b	.L772
.L783:
	uxth	r7, r6
	ldr	r2, [r10, r7, lsl #2]
	add	r3, r10, r7, lsl #2
	str	r3, [sp, #4]
	cmp	r9, r2, lsr #10
	bne	.L777
	ldr	r2, [r5, #3848]
	ldr	r8, [r5, #3288]
	ldr	r0, .L788+8
	str	r2, [r5, #3272]
	str	r8, [r5, #3276]
	ldr	r2, [r10, r7, lsl #2]
	str	r2, [r5, #3268]
	movs	r2, #1
	mov	r1, r2
	bl	FlashReadPages
	ldrh	r2, [r8, #8]
	cmp	r2, fp
	beq	.L778
	movw	r2, #647
	ldr	r1, .L788+12
	ldr	r0, .L788+16
	bl	sftl_printk
.L778:
	ldr	r2, [r5, #3264]
	adds	r2, r2, #1
	bne	.L779
.L781:
	ldr	r2, [sp, #4]
	movs	r3, #0
	str	r3, [r2]
.L780:
	b	.L780
.L779:
	ldrh	r2, [r8, #8]
	cmp	r2, fp
	bne	.L781
	ldrh	r2, [r8]
	ldrh	r3, [r4, #4]
	cmp	r2, r3
	bne	.L781
	ldr	r2, [r5, #3272]
	mov	r1, r7
	mov	r0, r4
	bl	FtlMapWritePage
.L777:
	adds	r6, r6, #1
	b	.L776
.L789:
	.align	2
.L788:
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
	ldr	r5, .L791
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
.L792:
	.align	2
.L791:
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
	ldr	r4, .L807
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
	bcc	.L794
	movw	r2, #847
	ldr	r1, .L807+4
	ldr	r0, .L807+8
	bl	sftl_printk
.L794:
	ldrh	r2, [r4, #138]
	movs	r3, #0
	ldr	r1, [r4, #2492]
	mov	r10, #12
.L795:
	uxth	r6, r3
	cmp	r6, r2
	bcc	.L800
	bl	select_l2p_ram_region
	mul	r10, r10, r0
	ldr	r3, [r4, #2492]
	mov	r6, r0
	ldrh	r1, [r3, r10]
	add	r2, r3, r10
	movw	r3, #65535
	cmp	r1, r3
	beq	.L801
	ldr	r3, [r2, #4]
	cmp	r3, #0
	bge	.L801
	bl	flush_l2p_region
.L801:
	mov	r1, r6
	mov	r0, r8
	bl	load_l2p_region
	b	.L796
.L800:
	adds	r3, r3, #1
	mla	r0, r10, r3, r1
	ldrh	r0, [r0, #-12]
	cmp	r0, r8
	bne	.L795
.L796:
	movs	r3, #12
	cmp	r9, #0
	bne	.L797
	ldr	r2, [r4, #2492]
	mla	r3, r3, r6, r2
	ldr	r3, [r3, #8]
	ldr	r3, [r3, r5, lsl #2]
	str	r3, [r7]
.L798:
	ldr	r2, [r4, #2492]
	movs	r3, #12
	mla	r6, r3, r6, r2
	ldr	r3, [r6, #4]
	adds	r2, r3, #1
	beq	.L804
	adds	r3, r3, #1
	str	r3, [r6, #4]
.L804:
	movs	r0, #0
	pop	{r4, r5, r6, r7, r8, r9, r10, pc}
.L797:
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
	b	.L798
.L808:
	.align	2
.L807:
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
	ldr	r5, .L819
	ubfx	r0, r1, #10, #16
	str	r1, [sp, #4]
	bl	P2V_block_in_plane
	ldr	r2, [r5, #2324]
	mov	r7, r0
	ldrh	r3, [r2, r0, lsl #1]
	cbnz	r3, .L810
	ldr	r4, [r5, #2340]
	cbz	r4, .L811
	ldr	r1, [r5, #2316]
	mov	ip, #6
	ldr	r2, .L819+4
	movw	lr, #65535
	ldrh	r0, [r5, #2344]
	subs	r4, r4, r1
	asrs	r4, r4, #1
	muls	r4, r2, r4
	uxth	r4, r4
.L812:
	uxth	r2, r3
	cmp	r0, r2
	bls	.L811
	cmp	r4, r7
	bne	.L813
	mov	r1, r4
	ldr	r0, .L819+8
	bl	List_remove_node
	ldrh	r3, [r5, #2344]
	cbnz	r3, .L814
	movw	r2, #1737
	ldr	r1, .L819+12
	ldr	r0, .L819+16
	bl	sftl_printk
.L814:
	ldrh	r3, [r5, #2344]
	mov	r0, r4
	subs	r3, r3, #1
	strh	r3, [r5, #2344]	@ movhi
	bl	INSERT_DATA_LIST
	ldr	r2, [r5, #2324]
	ldrh	r3, [r2, r7, lsl #1]
.L810:
	adds	r3, r3, #1
	strh	r3, [r2, r7, lsl #1]	@ movhi
	b	.L811
.L813:
	mul	r4, ip, r4
	adds	r3, r3, #1
	ldrh	r4, [r1, r4]
	cmp	r4, lr
	bne	.L812
.L811:
	movs	r2, #1
	add	r1, sp, #4
	mov	r0, r6
	bl	log2phys
	add	sp, sp, #12
	@ sp needed
	pop	{r4, r5, r6, r7, pc}
.L820:
	.align	2
.L819:
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
	ldr	r6, .L835
	.pad #32
	sub	sp, sp, #32
	movs	r1, #0
	ldr	r8, .L835+16
	mov	r4, r6
	ldr	r0, .L835+4
	bl	sftl_printk
.L822:
	ldr	r3, [r6, #2504]
	cmp	r5, r3
	bcc	.L828
	add	sp, sp, #32
	@ sp needed
	pop	{r4, r5, r6, r7, r8, pc}
.L828:
	movs	r2, #0
	add	r1, sp, #28
	mov	r0, r5
	bl	log2phys
	ubfx	r3, r5, #0, #11
	cbnz	r3, .L823
	ldr	r2, [sp, #28]
	mov	r1, r5
	mov	r0, r8
	bl	sftl_printk
.L823:
	ldr	r3, [sp, #28]
	adds	r2, r3, #1
	beq	.L825
	str	r3, [r4, #3268]
	movs	r2, #0
	ldr	r3, [r4, #3284]
	movs	r1, #1
	ldr	r7, [r4, #3288]
	ldr	r0, .L835+8
	str	r3, [r4, #3272]
	str	r5, [r4, #3280]
	str	r7, [r4, #3276]
	str	r2, [r4, #3264]
	bl	FlashReadPages
	ldr	r3, [r4, #3264]
	cmp	r3, #256
	beq	.L826
	adds	r3, r3, #1
	beq	.L826
	ldr	r3, [r7, #8]
	cmp	r5, r3
	beq	.L825
.L826:
	ldr	r2, [r4, #3272]
	ldr	r3, [r4, #3276]
	ldr	r0, .L835+12
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
.L825:
	adds	r5, r5, #1
	b	.L822
.L836:
	.align	2
.L835:
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
	ldr	r3, .L850
	push	{r4, r5, r6, lr}
	.save {r4, r5, r6, lr}
	.pad #88
	sub	sp, sp, #88
	mov	r4, r3
	ldr	r0, [r3, #2716]
	cmp	r0, #0
	beq	.L838
	ldr	r1, [r3, #2720]
	ldr	r2, [r3, #2504]
	cmp	r1, r2
	bcs	.L839
	mov	r5, #2048
.L844:
	ldr	r0, [r4, #2720]
	ldr	r3, [r4, #2504]
	cmp	r0, r3
	bcs	.L841
	movs	r2, #0
	mov	r1, sp
	bl	log2phys
	ldr	r2, [sp]
	ldr	r3, [r4, #2720]
	adds	r1, r2, #1
	add	r3, r3, #1
	str	r3, [r4, #2720]
	beq	.L842
	str	r3, [sp, #20]
	add	r0, sp, #88
	ldr	r3, .L850+4
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
	bne	.L841
	ldr	r0, [sp]
	ubfx	r0, r0, #10, #16
	bl	P2V_block_in_plane
	bl	FtlGcRefreshBlock
.L841:
	mov	r0, #-1
.L837:
	add	sp, sp, #88
	@ sp needed
	pop	{r4, r5, r6, pc}
.L842:
	subs	r5, r5, #1
	bne	.L844
	b	.L841
.L839:
	ldr	r2, [r3, #2508]
	movs	r0, #0
	str	r0, [r3, #2716]
	str	r0, [r3, #2720]
	str	r2, [r3, #2712]
	b	.L837
.L838:
	ldr	r1, [r3, #2508]
	ldr	r6, [r3, #2712]
	add	r2, r1, #1048576
	cmp	r6, r2
	bhi	.L847
	ldr	r2, [r3, #2564]
	lsrs	r5, r2, #10
	mov	r2, #33554432
	asrs	r2, r2, r5
	add	r2, r2, r6
	cmp	r1, r2
	bhi	.L847
	ldrb	r3, [r3, #2296]	@ zero_extendqisi2
	cmp	r3, #0
	bne	.L837
.L847:
	movs	r3, #1
	movs	r0, #0
	str	r3, [r4, #2716]
	str	r0, [r4, #2720]
	str	r1, [r4, #2712]
	b	.L837
.L851:
	.align	2
.L850:
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
	ldr	r6, .L876
	ldr	r7, .L876+4
	ldr	r1, .L876+8
	mov	r5, r6
	ldr	r0, .L876+12
	bl	sftl_printk
	mov	r1, #8192
	ldr	r0, .L876+4
	bl	__memzero
.L853:
	ldr	r3, [r6, #2504]
	cmp	r4, r3
	bcc	.L855
	ldr	r8, .L876+4
	movs	r4, #0
	ldr	r10, .L876+24
	mov	r7, r4
	movw	r9, #65535
.L856:
	ldrh	r2, [r5, #40]
	uxth	r3, r4
	cmp	r2, r3
	bhi	.L858
	ldr	r4, [r5, #2340]
	cbz	r4, .L859
	ldr	r3, [r5, #2316]
	movs	r6, #0
	ldrh	r8, [r5, #2344]
	mov	fp, #6
	ldr	r9, .L876+4
	subs	r4, r4, r3
	ldr	r3, .L876+16
	asrs	r4, r4, #1
	ldr	r10, .L876+28
	muls	r4, r3, r4
	uxth	r4, r4
.L860:
	uxth	r3, r6
	cmp	r8, r3
	bls	.L859
	ldr	r3, [r5, #2324]
	ldrh	r2, [r3, r4, lsl #1]
	cbz	r2, .L861
	movs	r7, #1
	ldrh	r3, [r9, r4, lsl #1]
	mov	r1, r4
	mov	r0, r10
	bl	sftl_printk
.L861:
	mul	r4, fp, r4
	ldr	r3, [r5, #2316]
	adds	r6, r6, #1
	ldrh	r4, [r3, r4]
	movw	r3, #65535
	cmp	r4, r3
	bne	.L860
.L859:
	cbz	r7, .L852
	movw	r2, #2373
	ldr	r1, .L876+8
	ldr	r0, .L876+20
	bl	sftl_printk
.L852:
	add	sp, sp, #12
	@ sp needed
	pop	{r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L855:
	movs	r2, #0
	add	r1, sp, #4
	mov	r0, r4
	bl	log2phys
	ldr	r0, [sp, #4]
	adds	r3, r0, #1
	beq	.L854
	ubfx	r0, r0, #10, #16
	bl	P2V_block_in_plane
	ldrh	r3, [r7, r0, lsl #1]
	adds	r3, r3, #1
	strh	r3, [r7, r0, lsl #1]	@ movhi
.L854:
	adds	r4, r4, #1
	b	.L853
.L858:
	ldr	r3, [r5, #2324]
	uxth	r6, r4
	ldrh	r2, [r3, r6, lsl #1]
	ldrh	r3, [r8, r6, lsl #1]
	cmp	r2, r3
	beq	.L857
	mov	r1, r6
	mov	r0, r10
	bl	sftl_printk
	ldr	r3, [r5, #2324]
	ldrh	r3, [r3, r6, lsl #1]
	cmp	r3, r9
	beq	.L857
	ldrh	r2, [r8, r6, lsl #1]
	cmp	r2, r3
	it	hi
	movhi	r7, #1
.L857:
	adds	r4, r4, #1
	b	.L856
.L877:
	.align	2
.L876:
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
	cbz	r3, .L878
	ldrh	r5, [r0, #6]
	movs	r3, #0
	ldr	r4, .L882
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
	cbz	r2, .L880
	movs	r2, #1
	add	r0, r4, #3264
	mov	r1, r2
	bl	FlashReadPages
.L881:
	ldr	r2, [r4, #3272]
	mov	r1, r5
	mov	r0, r6
	pop	{r4, r5, r6, lr}
	b	FtlMapWritePage
.L880:
	ldrh	r2, [r4, #110]
	movs	r1, #255
	bl	ftl_memset
	b	.L881
.L878:
	pop	{r4, r5, r6, pc}
.L883:
	.align	2
.L882:
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
	ldr	r4, .L965
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
.L885:
	ldrh	r3, [r4, #42]
	cmp	r3, r8
	bls	.L927
	movs	r5, #0
	ldrh	fp, [r4, #32]
	ldr	r9, [r4, #3836]
	mov	r7, r5
	ldr	r3, [r4, #3172]
	mov	r10, #20
	ldr	r2, [r4, #3176]
	ldrh	r6, [r4, #112]
	b	.L928
.L888:
	str	r3, [sp, #8]
	mov	r1, r8
	ldr	r3, .L965+4
	str	r2, [sp, #12]
	ldrb	r0, [r3, r5]	@ zero_extendqisi2
	bl	V2P_block
	str	r0, [sp, #4]
	bl	FtlBbmIsBadBlock
	ldr	r3, [sp, #8]
	ldr	r2, [sp, #12]
	cbnz	r0, .L886
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
.L886:
	adds	r5, r5, #1
.L928:
	uxth	r1, r5
	cmp	fp, r1
	bhi	.L888
	cbnz	r7, .L889
.L926:
	add	r8, r8, #1
	uxth	r8, r8
	b	.L885
.L889:
	mov	r0, r9
	mov	r9, #0
	movs	r2, #1
	mov	r1, r7
	bl	FlashReadPages
.L890:
	uxth	r3, r9
	cmp	r7, r3
	bls	.L926
	movs	r3, #20
	mul	r10, r3, r9
	ldr	r3, [r4, #3836]
	add	r2, r3, r10
	ldr	r3, [r3, r10]
	ldr	r5, [r2, #4]
	ldr	r6, [r2, #12]
	adds	r3, r3, #1
	ubfx	r5, r5, #10, #16
	bne	.L893
	mov	fp, #16
	movw	r3, #65535
.L895:
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
	bne	.L892
	ldr	r3, [r4, #3836]
	mov	r2, #-1
	str	r2, [r3, r10]
	ldr	r3, [r4, #3836]
	ldr	r3, [r3, r10]
	cmp	r3, r2
	bne	.L893
.L894:
	movs	r1, #0
	mov	r0, r5
	bl	FtlFreeSysBlkQueueIn
	b	.L898
.L892:
	ldr	r2, [r4, #3836]
	ldr	r2, [r2, r10]
	adds	r2, r2, #1
	bne	.L893
	add	fp, fp, #-1
	uxth	fp, fp
	cmp	fp, #0
	bne	.L895
	b	.L894
.L893:
	ldr	r2, [r4, #2540]
	ldr	r3, [r6, #4]
	adds	r1, r2, #1
	beq	.L896
	cmp	r2, r3
	bhi	.L897
.L896:
	adds	r2, r3, #1
	itt	ne
	addne	r2, r3, #1
	strne	r2, [r4, #2540]
.L897:
	ldrh	r2, [r6]
	movw	r1, #61604
	cmp	r2, r1
	beq	.L899
	bhi	.L900
	movw	r3, #61574
	cmp	r2, r3
	beq	.L901
.L898:
	add	r9, r9, #1
	b	.L890
.L900:
	movw	r3, #61634
	cmp	r2, r3
	beq	.L902
	movw	r3, #65535
	cmp	r2, r3
	beq	.L894
	b	.L898
.L902:
	ldrh	r2, [r4, #3952]
	ldr	r3, [r4, #128]
	cmp	r2, r3
	bls	.L904
	mov	r2, #1256
	ldr	r1, .L965+8
	ldr	r0, .L965+12
	bl	sftl_printk
.L904:
	ldr	r1, [r4, #128]
	ldrh	r0, [r4, #3952]
	ldr	r10, [r4, #3896]
	uxth	ip, r1
	add	r3, ip, #-1
	sub	ip, ip, r0
	add	ip, ip, #-1
	sxth	r3, r3
	sxth	ip, ip
.L905:
	cmp	r3, ip
	bgt	.L911
	cmp	r3, #0
	bge	.L943
	b	.L898
.L911:
	ldr	r2, [r6, #4]
	lsl	fp, r3, #2
	mov	lr, r2
	ldr	r2, [r10, r3, lsl #2]
	cmp	lr, r2
	bls	.L906
	ldr	r2, [r10]
	cbnz	r2, .L907
	cmp	r1, r0
	itt	ne
	addne	r0, r0, #1
	strhne	r0, [r4, #3952]	@ movhi
.L907:
	uxth	ip, r3
	movs	r2, #0
.L908:
	uxth	r0, r2
	sxth	r1, r2
	cmp	ip, r0
	bhi	.L909
	ldr	r2, [r4, #3896]
	cmp	r3, #0
	ldr	r1, [r6, #4]
	str	r1, [r2, fp]
	ldr	r2, [r4, #3872]
	strh	r5, [r2, r3, lsl #1]	@ movhi
	blt	.L898
	ldrh	r0, [r4, #3952]
	ldr	r2, [r4, #128]
	subs	r2, r2, r0
	subs	r2, r2, #1
	sxth	r2, r2
	cmp	r3, r2
	bgt	.L898
.L943:
	ldr	r2, [r4, #3896]
	adds	r0, r0, #1
	ldr	r1, [r6, #4]
	strh	r0, [r4, #3952]	@ movhi
	str	r1, [r2, r3, lsl #2]
	ldr	r2, [r4, #3872]
.L963:
	strh	r5, [r2, r3, lsl #1]	@ movhi
	b	.L898
.L909:
	ldr	r0, [r4, #3896]
	adds	r2, r2, #1
	add	lr, r0, r1, lsl #2
	ldr	lr, [lr, #4]
	str	lr, [r0, r1, lsl #2]
	ldr	r0, [r4, #3872]
	add	lr, r0, r1, lsl #1
	ldrh	lr, [lr, #2]
	strh	lr, [r0, r1, lsl #1]	@ movhi
	b	.L908
.L906:
	subs	r3, r3, #1
	sxth	r3, r3
	b	.L905
.L966:
	.align	2
.L965:
	.word	.LANCHOR0
	.word	.LANCHOR0+60
	.word	.LANCHOR1+328
	.word	.LC1
.L901:
	ldrh	r2, [r4, #144]
	ldrh	r3, [r4, #120]
	cmp	r2, r3
	bls	.L914
	movw	r2, #1297
	ldr	r1, .L967
	ldr	r0, .L967+4
	bl	sftl_printk
.L914:
	ldrh	lr, [r4, #120]
	ldrh	r0, [r4, #144]
	ldr	r10, [r4, #3884]
	add	ip, lr, #-1
	sxth	r3, ip
	sub	ip, ip, r0
.L915:
	cmp	r3, ip
	ble	.L920
	ldr	r2, [r6, #4]
	lsl	fp, r3, #2
	ldr	r1, [r10, r3, lsl #2]
	cmp	r2, r1
	bls	.L916
	ldr	r2, [r10]
	cbnz	r2, .L917
	cmp	lr, r0
	itt	ne
	addne	r0, r0, #1
	strhne	r0, [r4, #144]	@ movhi
.L917:
	uxth	ip, r3
	movs	r2, #0
.L918:
	uxth	r0, r2
	sxth	r1, r2
	cmp	ip, r0
	bhi	.L919
	ldr	r2, [r4, #3884]
	ldr	r1, [r6, #4]
	str	r1, [r2, fp]
	ldr	r2, [r4, #148]
	strh	r5, [r2, r3, lsl #1]	@ movhi
.L920:
	cmp	r3, #0
	blt	.L898
	ldrh	r2, [r4, #120]
	ldrh	r1, [r4, #144]
	subs	r2, r2, #1
	subs	r2, r2, r1
	sxth	r2, r2
	cmp	r3, r2
	bgt	.L898
	adds	r1, r1, #1
	ldr	r2, [r4, #3884]
	strh	r1, [r4, #144]	@ movhi
	ldr	r1, [r6, #4]
	str	r1, [r2, r3, lsl #2]
	ldr	r2, [r4, #148]
	b	.L963
.L919:
	ldr	r0, [r4, #3884]
	adds	r2, r2, #1
	add	lr, r0, r1, lsl #2
	ldr	lr, [lr, #4]
	str	lr, [r0, r1, lsl #2]
	ldr	r0, [r4, #148]
	add	lr, r0, r1, lsl #1
	ldrh	lr, [lr, #2]
	strh	lr, [r0, r1, lsl #1]	@ movhi
	b	.L918
.L916:
	subs	r3, r3, #1
	sxth	r3, r3
	b	.L915
.L899:
	ldrh	r1, [r4, #2576]
	movw	r2, #65535
	cmp	r1, r2
	bne	.L922
	strh	r5, [r4, #2576]	@ movhi
.L964:
	str	r3, [r4, #2584]
	b	.L898
.L922:
	ldrh	r0, [r4, #2580]
	cmp	r0, r2
	beq	.L923
	movs	r1, #1
	bl	FtlFreeSysBlkQueueIn
.L923:
	ldr	r2, [r4, #2584]
	ldr	r3, [r6, #4]
	cmp	r2, r3
	bcs	.L924
	ldrh	r3, [r4, #2576]
	strh	r5, [r4, #2576]	@ movhi
	strh	r3, [r4, #2580]	@ movhi
	ldr	r3, [r6, #4]
	b	.L964
.L924:
	strh	r5, [r4, #2580]	@ movhi
	b	.L898
.L927:
	ldr	r1, [r4, #3872]
	ldrh	r3, [r1]
	cbz	r3, .L929
.L932:
	ldr	r1, [r4, #148]
	ldrh	r2, [r1]
	cmp	r2, #0
	beq	.L930
.L931:
	ldrh	r2, [r4, #3952]
	ldr	r3, [r4, #128]
	cmp	r2, r3
	bls	.L962
	movw	r2, #1422
	ldr	r1, .L967
	ldr	r0, .L967+4
	bl	sftl_printk
.L962:
	movs	r0, #0
	add	sp, sp, #20
	@ sp needed
	pop	{r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L929:
	ldrh	r2, [r4, #3952]
	cmp	r2, #0
	beq	.L932
	ldr	r0, [r4, #128]
.L933:
	sxth	r2, r3
	cmp	r2, r0
	bcs	.L932
	ldrh	r5, [r1, r2, lsl #1]
	adds	r3, r3, #1
	cmp	r5, #0
	beq	.L933
	mov	r3, r2
	movs	r5, #0
.L934:
	ldr	r1, [r4, #128]
	cmp	r3, r1
	bcs	.L932
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
	b	.L934
.L930:
	ldrh	r3, [r4, #144]
	cmp	r3, #0
	beq	.L931
	ldrh	r0, [r4, #120]
.L939:
	sxth	r3, r2
	cmp	r3, r0
	mov	r5, r3
	bge	.L931
	ldrh	r6, [r1, r3, lsl #1]
	adds	r2, r2, #1
	cmp	r6, #0
	beq	.L939
	movs	r0, #0
.L940:
	ldrh	r2, [r4, #120]
	cmp	r3, r2
	bge	.L931
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
	b	.L940
.L968:
	.align	2
.L967:
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
	ldr	r5, .L994
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
.L970:
	ldr	r1, [sp, #8]
	sxth	r2, r8
	cmp	r2, r1
	bge	.L988
	cmp	r2, fp
	lsl	r7, r2, #1
	bne	.L971
	ldrh	r0, [r9, fp, lsl #1]
	movs	r1, #1
	bl	FtlGetLastWrittenPage
	ldr	r3, [sp, #12]
	add	r7, r7, r9
	strh	r8, [r4]	@ movhi
	sxth	r9, r0
	ldr	r8, .L994+4
	adds	r0, r0, #1
	strh	r0, [r4, #2]	@ movhi
	add	r9, r9, #1
	ldr	r3, [r3, fp, lsl #2]
	mov	fp, #0
	str	r3, [r4, #28]
.L972:
	sxth	r2, fp
	cmp	r2, r9
	blt	.L975
.L988:
	mov	r0, r4
	bl	ftl_free_no_use_map_blk
	ldrh	r2, [r4, #2]
	ldrh	r3, [r5, #104]
	cmp	r2, r3
	bne	.L977
	mov	r0, r4
	bl	ftl_map_blk_alloc_new_blk
.L977:
	mov	r0, r4
	bl	ftl_map_blk_gc
	mov	r0, r4
	bl	ftl_map_blk_gc
	movs	r0, #0
	add	sp, sp, #28
	@ sp needed
	pop	{r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L975:
	ldrh	r1, [r7]
	mov	r0, r8
	orr	r2, r2, r1, lsl #10
	str	r2, [r5, #3268]
	movs	r2, #1
	mov	r1, r2
	bl	FlashReadPages
	ldr	r2, [r5, #3264]
	adds	r2, r2, #1
	beq	.L973
	ldrh	r2, [r6, #8]
	cmp	r10, r2
	bls	.L974
	ldrh	r0, [r6]
	ldrh	r1, [r4, #4]
	cmp	r0, r1
	bne	.L974
	ldr	r1, [r5, #3268]
	ldr	r3, [sp, #4]
	str	r1, [r3, r2, lsl #2]
.L974:
	add	fp, fp, #1
	b	.L972
.L973:
	ldrh	r2, [r7]
	strh	r2, [r4, #40]	@ movhi
	b	.L974
.L971:
	ldr	r1, [r5, #3284]
	str	r3, [sp, #20]
	add	r3, r9, r7
	ldr	r0, .L994+4
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
	beq	.L990
	ldrh	r1, [r6]
	ldrh	r2, [r4, #4]
	cmp	r1, r2
	bne	.L990
	ldrh	r1, [r6, #8]
	movw	r2, #64245
	cmp	r1, r2
	beq	.L979
.L990:
	movs	r7, #0
.L980:
	ldrh	r1, [r3, #104]
	sxth	r2, r7
	cmp	r2, r1
	bge	.L986
	ldr	r1, [sp, #16]
	ldr	r0, .L994+4
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
	beq	.L984
	ldrh	r2, [r6, #8]
	cmp	r10, r2
	bls	.L984
	ldrh	r0, [r6]
	ldrh	r1, [r4, #4]
	cmp	r0, r1
	ittt	eq
	ldreq	r1, [r3, #3268]
	ldreq	r0, [sp, #4]
	streq	r1, [r0, r2, lsl #2]
.L984:
	adds	r7, r7, #1
	b	.L980
.L979:
	movs	r0, #0
	mov	ip, #4
.L981:
	ldrh	r1, [r5, #104]
	sxth	r2, r0
	subs	r1, r1, #1
	cmp	r2, r1
	blt	.L983
.L986:
	add	r8, r8, #1
	b	.L970
.L983:
	ldr	r7, [r5, #3284]
	ldr	r1, [r7, r2, lsl #3]
	uxth	lr, r1
	cmp	r10, lr
	bls	.L982
	add	r2, ip, r2, lsl #3
	ldr	r2, [r7, r2]
	ldr	r7, [sp, #4]
	str	r2, [r7, lr, lsl #2]
.L982:
	adds	r0, r0, #1
	b	.L981
.L995:
	.align	2
.L994:
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
	ldr	r0, .L997
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
.L998:
	.align	2
.L997:
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
	ldr	r4, .L1002
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
.L1000:
	ldrh	r4, [r3, #138]
	uxth	r0, r2
	adds	r5, r2, #1
	cmp	r4, r0
	bhi	.L1001
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
.L1001:
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
	b	.L1000
.L1003:
	.align	2
.L1002:
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
	ldr	r0, .L1005
	bl	FtlMapTblRecovery
	movs	r0, #0
	pop	{r3, pc}
.L1006:
	.align	2
.L1005:
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
	ldr	r4, .L1008
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
.L1009:
	.align	2
.L1008:
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
	ldr	r4, .L1021
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
.L1011:
	ldrh	r3, [r4, #40]
	uxth	r8, r5
	cmp	r8, r3
	bcs	.L1018
	ldrh	r3, [r4, #102]
	ldrh	r2, [r4, #32]
	str	r3, [sp, #4]
	movs	r3, #0
	mov	r7, r3
	b	.L1019
.L1013:
	ldrb	r0, [r10, r3]	@ zero_extendqisi2
	mov	r1, r8
	str	r2, [sp, #12]
	str	r3, [sp, #8]
	bl	V2P_block
	bl	FtlBbmIsBadBlock
	ldr	r3, [sp, #8]
	ldr	r2, [sp, #12]
	cbnz	r0, .L1012
	ldr	r1, [sp, #4]
	add	r7, r7, r1
	uxth	r7, r7
.L1012:
	adds	r3, r3, #1
.L1019:
	uxth	r1, r3
	cmp	r2, r1
	bhi	.L1013
	uxth	r3, r5
	cbz	r7, .L1014
	mov	r1, r7
	str	r3, [sp, #4]
	mov	r0, #32768
	bl	__aeabi_idiv
	ldr	r3, [sp, #4]
	uxth	r7, r0
.L1015:
	ldr	r1, [r4, #2316]
	movs	r2, #6
	mla	r2, r2, r3, r1
	strh	r7, [r2, #4]	@ movhi
	ldrh	r2, [r4, #2348]
	cmp	r2, r8
	beq	.L1016
	ldrh	r2, [r4, #2396]
	cmp	r2, r8
	beq	.L1016
	ldrh	r2, [r4, #2444]
	cmp	r2, r8
	beq	.L1016
	ldr	r2, [r4, #2324]
	ldrh	r3, [r2, r3, lsl #1]
	cbnz	r3, .L1017
	add	r9, r9, #1
	mov	r0, r8
	uxth	r9, r9
	bl	INSERT_FREE_LIST
.L1016:
	adds	r5, r5, #1
	b	.L1011
.L1014:
	ldr	r2, [r4, #2324]
	strh	fp, [r2, r3, lsl #1]	@ movhi
	b	.L1015
.L1017:
	adds	r6, r6, #1
	mov	r0, r8
	uxth	r6, r6
	bl	INSERT_DATA_LIST
	b	.L1016
.L1018:
	strh	r6, [r4, #2336]	@ movhi
	add	r6, r6, r9
	cmp	r6, r3
	strh	r9, [r4, #2344]	@ movhi
	ble	.L1020
	movw	r2, #2206
	ldr	r1, .L1021+4
	ldr	r0, .L1021+8
	bl	sftl_printk
.L1020:
	movs	r0, #0
	add	sp, sp, #20
	@ sp needed
	pop	{r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L1022:
	.align	2
.L1021:
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
	ldr	r4, .L1024
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
.L1025:
	.align	2
.L1024:
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
	ldr	r3, .L1051
	push	{r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.save {r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.pad #28
	sub	sp, sp, #28
	mov	r7, r0
	str	r1, [sp, #8]
	ldrh	r4, [r3, #4]
	movw	r3, #65535
	cmp	r4, r3
	beq	.L1045
	cbnz	r4, .L1027
.L1028:
	bl	FtlGcPageVarInit
	b	.L1029
.L1045:
	movs	r4, #0
.L1027:
	ldr	r3, .L1051+4
	ldr	r2, [sp, #8]
	ldrh	r3, [r3, #102]
	cmp	r3, r2
	beq	.L1028
.L1029:
	mov	fp, #0
	movw	r8, #65535
.L1039:
	ldrh	r3, [r7]
	movs	r0, #0
	strb	r0, [r7, #8]
	cmp	r3, r8
	beq	.L1030
	ldr	r5, .L1051+4
.L1042:
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
.L1031:
	uxth	r3, r2
	cmp	ip, r3
	bhi	.L1035
	mov	r9, #0
	movs	r2, #0
	mov	r1, r6
	ldr	r0, [sp, #4]
	bl	FlashReadPages
.L1036:
	uxth	r3, r9
	cmp	r6, r3
	bhi	.L1040
	ldr	r3, [sp, #8]
	add	fp, fp, #1
	adds	r4, r4, #1
	uxth	r4, r4
	cmp	r3, fp
	bls	.L1041
.L1043:
	ldrh	r3, [r5, #102]
	cmp	r3, r4
	bhi	.L1042
	movs	r0, #0
	b	.L1030
.L1035:
	ldrh	r3, [lr], #2
	cmp	r3, r8
	beq	.L1032
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
.L1032:
	adds	r2, r2, #1
	b	.L1031
.L1040:
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
	cbnz	r2, .L1037
	ldr	r3, [sp, #12]
	add	r9, r9, #1
	ldr	r1, [sp, #4]
	ldr	r3, [r3, #12]
	ldrh	r2, [r3]
	cmp	r2, r8
	bne	.L1038
	movs	r3, #1
	str	r3, [r5, #4008]
.L1030:
	ldr	r3, .L1051
	movw	r2, #65535
	strb	r0, [r7, #6]
	mov	r1, r4
	strh	r4, [r7, #2]	@ movhi
	strh	r2, [r3, #4]	@ movhi
	mov	r2, r0
	mov	r0, r7
	bl	ftl_sb_update_avl_pages
	b	.L1044
.L1038:
	ldr	r2, [r3, #8]
	ldr	r0, [r3, #12]
	bl	FtlGcUpdatePage
	b	.L1036
.L1037:
	ldrh	r2, [r7]
	movs	r4, #0
	ldr	r3, [r5, #2324]
	strh	r4, [r3, r2, lsl #1]	@ movhi
	ldrh	r0, [r7]
	bl	INSERT_FREE_LIST
	strh	r8, [r7]	@ movhi
	bl	FtlGcPageVarInit
	b	.L1039
.L1041:
	ldr	r2, .L1051
	ldrh	r3, [r2, #4]
	cmp	r3, r8
	beq	.L1043
	add	r3, r3, fp
	strh	r3, [r2, #4]	@ movhi
	ldrh	r3, [r5, #102]
	cmp	r3, r4
	bls	.L1043
.L1044:
	mov	r0, #-1
	add	sp, sp, #28
	@ sp needed
	pop	{r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L1052:
	.align	2
.L1051:
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
	ldr	r4, .L1063
	movs	r1, #0
	ldr	r0, [r4, #3284]
	add	r6, r4, #176
	ldr	r3, [r4, #3288]
	ldrh	r2, [r4, #110]
	str	r0, [r4, #3272]
	str	r3, [r4, #3276]
	bl	ftl_memset
.L1055:
	ldrh	r3, [r4, #54]
	cmp	r5, r3
	blt	.L1056
	ldr	r6, [r4, #3276]
	movs	r2, #16
	movs	r1, #255
	ldr	r9, .L1063+12
	ldr	r8, .L1063+4
	movs	r5, #0
	mov	r0, r6
	ldr	r10, .L1063+16
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
.L1057:
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
	ldr	r0, .L1063+4
	mov	r2, r3
	mov	r1, r3
	bl	FlashProgPages
	ldrh	r3, [r4, #104]
	ldrh	r2, [r4, #154]
	subs	r3, r3, #1
	cmp	r2, r3
	blt	.L1058
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
.L1058:
	ldrh	r3, [r4, #154]
	adds	r3, r3, #1
	strh	r3, [r4, #154]	@ movhi
	ldr	r3, [r4, #3264]
	adds	r3, r3, #1
	bne	.L1059
	adds	r5, r5, #1
	ldr	r1, [r4, #3268]
	uxth	r5, r5
	mov	r0, r10
	bl	sftl_printk
	cmp	r5, #3
	bls	.L1057
	mov	r2, r5
	ldr	r1, [r4, #3268]
	ldr	r0, .L1063+8
	bl	sftl_printk
.L1061:
	b	.L1061
.L1056:
	ldrh	r2, [r4, #3904]
	ldr	r3, [r4, #3272]
	ldr	r1, [r6, #4]!
	mul	r0, r2, r5
	lsls	r2, r2, #2
	adds	r5, r5, #1
	add	r0, r3, r0, lsl #2
	bl	ftl_memcpy
	b	.L1055
.L1062:
	movs	r7, #1
	b	.L1057
.L1059:
	cmp	r7, #0
	beq	.L1062
	movs	r0, #0
	add	sp, sp, #12
	@ sp needed
	pop	{r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L1064:
	.align	2
.L1063:
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
	ldr	r4, .L1100
	.pad #20
	sub	sp, sp, #20
	ldr	r9, .L1100+20
.L1066:
	ldr	r3, .L1100
	ldr	r2, .L1100
	ldrh	r3, [r3, #2344]
	ldrh	r2, [r2, #2336]
	add	r3, r3, r2
	ldr	r2, .L1100
	ldrh	r2, [r2, #40]
	cmp	r3, r2
	ble	.L1067
	movw	r2, #2654
	mov	r1, r9
	ldr	r0, .L1100+4
	bl	sftl_printk
.L1067:
	ldr	r3, .L1100+8
	cmp	r5, r3
	bne	.L1092
	ldrh	r3, [r4, #2344]
	ldr	r1, [r4, #3160]
	mul	r2, r1, r3
	lsrs	r1, r3, #1
	adds	r1, r1, #1
	add	r1, r1, r2, lsr #2
	uxth	r1, r1
	cbz	r1, .L1068
	subs	r1, r1, #1
	uxth	r1, r1
.L1068:
	ldr	r0, .L1100+12
	bl	List_pop_index_node
	ldrh	r3, [r4, #2344]
	mov	r6, r0
	uxth	r8, r0
	cbnz	r3, .L1069
	movw	r2, #2663
	mov	r1, r9
	ldr	r0, .L1100+4
	bl	sftl_printk
.L1069:
	ldrh	r3, [r4, #2344]
	mov	r0, r5
	subs	r3, r3, #1
	strh	r3, [r4, #2344]	@ movhi
	strh	r8, [r5]	@ movhi
	bl	make_superblock
	ldrb	r3, [r5, #7]	@ zero_extendqisi2
	cbnz	r3, .L1070
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
	ble	.L1066
	movw	r2, #2674
	mov	r1, r9
	ldr	r0, .L1100+4
	bl	sftl_printk
	b	.L1066
.L1092:
	movs	r1, #0
	b	.L1068
.L1070:
	ldrh	r2, [r4, #2336]
	ldrh	r3, [r4, #2344]
	add	r3, r3, r2
	ldrh	r2, [r4, #40]
	cmp	r3, r2
	ble	.L1072
	movw	r2, #2677
	mov	r1, r9
	ldr	r0, .L1100+4
	bl	sftl_printk
.L1072:
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
.L1073:
	ldr	r2, [sp, #4]
	cmp	r2, r3
	bne	.L1075
	cbnz	r7, .L1076
	mov	r2, #2688
	mov	r1, r9
	ldr	r0, .L1100+4
	bl	sftl_printk
.L1076:
	ldrh	r3, [r4, #2588]
	cmp	r3, r8
	bne	.L1077
	movw	r2, #2690
	mov	r1, r9
	ldr	r0, .L1100+4
	bl	sftl_printk
.L1077:
	ldrb	r3, [r5, #8]	@ zero_extendqisi2
	uxth	r6, r6
	ldr	r2, [r4, #2328]
	cmp	r3, #0
	bne	.L1078
	ldrh	r3, [r2, r6, lsl #1]
	cmp	r3, #0
	beq	.L1079
	ldrh	r1, [r4, #92]
	add	r3, r3, r1
.L1099:
	strh	r3, [r2, r6, lsl #1]	@ movhi
	movs	r1, #0
	ldr	r3, [r4, #2548]
	mov	r0, r8
	adds	r3, r3, #1
	str	r3, [r4, #2548]
	bl	ftl_set_blk_mode
.L1081:
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
	ldr	r0, .L1100+16
	adds	r3, r3, #1
	str	r3, [r2, #16]
	movs	r2, #20
	mla	r2, r2, r7, r1
	adds	r3, r1, #4
	adds	r2, r2, #24
.L1083:
	adds	r3, r3, #20
	cmp	r2, r3
	bne	.L1084
	ldrb	r1, [r5, #8]	@ zero_extendqisi2
	mov	r2, r7
	ldr	r0, [r4, #3804]
	mov	r10, #0
	bl	FlashEraseBlocks
	mov	r3, r10
	movs	r1, #20
.L1085:
	uxth	r2, r10
	cmp	r7, r2
	bhi	.L1087
	cmp	r3, #0
	ble	.L1088
	mov	r0, r8
	bl	update_multiplier_value
	bl	FtlBbmTblFlush
.L1088:
	ldrb	r2, [r5, #7]	@ zero_extendqisi2
	cmp	r2, #0
	bne	.L1089
	ldr	r3, [r4, #2324]
	movw	r2, #65535
	strh	r2, [r3, r6, lsl #1]	@ movhi
	b	.L1066
.L1075:
	str	ip, [r3, #8]
	movw	lr, #65535
	str	ip, [r3, #12]
	ldrh	r2, [r0], #2
	cmp	r2, lr
	beq	.L1074
	mla	lr, r10, r7, r1
	adds	r7, r7, #1
	lsls	r2, r2, #10
	uxth	r7, r7
	str	r2, [lr, #4]
.L1074:
	adds	r3, r3, #20
	b	.L1073
.L1079:
	movs	r3, #2
	b	.L1099
.L1078:
	ldrh	r3, [r2, r6, lsl #1]
	mov	r0, r8
	adds	r3, r3, #1
	strh	r3, [r2, r6, lsl #1]	@ movhi
	ldr	r3, [r4, #2552]
	adds	r3, r3, #1
	str	r3, [r4, #2552]
	bl	ftl_set_blk_mode.part.6
	b	.L1081
.L1084:
	ldr	r1, [r3, #-20]
	ands	r1, r1, r0
	str	r1, [r3, #-20]
	b	.L1083
.L1087:
	mul	r2, r1, r10
	ldr	r0, [r4, #3804]
	add	ip, r0, r2
	ldr	r2, [r0, r2]
	adds	r0, r2, #1
	bne	.L1086
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
.L1086:
	add	r10, r10, #1
	add	fp, fp, #2
	b	.L1085
.L1089:
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
	cbz	r3, .L1090
	ldrb	r3, [r5, #7]	@ zero_extendqisi2
	cbnz	r3, .L1091
.L1090:
	movw	r2, #2743
	ldr	r1, .L1100+20
	ldr	r0, .L1100+4
	bl	sftl_printk
.L1091:
	movs	r0, #0
	add	sp, sp, #20
	@ sp needed
	pop	{r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L1101:
	.align	2
.L1100:
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
	ldr	r4, .L1114
	ldrh	r3, [r4, #3210]
	cbz	r3, .L1103
	movs	r7, #0
	addw	r10, r4, #3212
.L1104:
	ldrh	r2, [r4, #32]
	uxth	r3, r7
	cmp	r2, r3
	bhi	.L1110
	bl	FtlGcReFreshBadBlk
.L1103:
	movs	r0, #0
	pop	{r3, r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L1110:
	uxtah	r3, r4, r7
	mov	r1, r8
	mov	fp, #0
	ldrb	r0, [r3, #60]	@ zero_extendqisi2
	bl	V2P_block
	mov	r9, r0
.L1105:
	ldrh	r3, [r4, #3210]
	uxth	r5, fp
	cmp	r3, r5
	bhi	.L1109
	adds	r7, r7, #1
	b	.L1104
.L1109:
	uxth	r6, fp
	add	r3, r4, r6, lsl #1
	ldrh	r3, [r3, #3212]
	cmp	r3, r9
	bne	.L1106
	mov	r1, r9
	ldr	r0, .L1114+4
	bl	sftl_printk
	mov	r0, r9
	bl	FtlBbmMapBadBlock
	bl	FtlBbmTblFlush
	ldrh	r1, [r4, #3210]
	add	r3, r10, r6, lsl #1
.L1107:
	cmp	r5, r1
	bcc	.L1108
	subs	r1, r1, #1
	strh	r1, [r4, #3210]	@ movhi
.L1106:
	add	fp, fp, #1
	b	.L1105
.L1108:
	ldrh	r0, [r3, #2]!
	adds	r5, r5, #1
	uxth	r5, r5
	strh	r0, [r3, #-2]	@ movhi
	b	.L1107
.L1115:
	.align	2
.L1114:
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
	ldr	r2, .L1126
	push	{r3, r4, r5, lr}
	.save {r3, r4, r5, lr}
	mov	r5, r0
	mov	r4, r2
	ldr	r3, [r2, #2324]
	ldrh	r3, [r3, r0, lsl #1]
	cmp	r3, #0
	bne	.L1117
	ldrh	r1, [r2, #2588]
	cmp	r1, r0
	bne	.L1118
	movw	r3, #65535
	strh	r3, [r2, #2588]	@ movhi
.L1119:
	mov	r1, r5
	ldr	r0, .L1126+4
	bl	List_remove_node
	ldrh	r3, [r4, #2336]
	cbnz	r3, .L1121
	movw	r2, #2813
	ldr	r1, .L1126+8
	ldr	r0, .L1126+12
	bl	sftl_printk
.L1121:
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
	ble	.L1125
	mov	r2, #2816
	ldr	r1, .L1126+8
	ldr	r0, .L1126+12
	bl	sftl_printk
.L1125:
	movs	r3, #1
	b	.L1116
.L1118:
	ldrh	r1, [r2, #2348]
	cmp	r1, r0
	beq	.L1116
	ldrh	r1, [r2, #2396]
	cmp	r1, r0
	beq	.L1116
	ldrh	r2, [r2, #2444]
	cmp	r2, r0
	bne	.L1119
.L1116:
	mov	r0, r3
	pop	{r3, r4, r5, pc}
.L1117:
	bl	List_update_data_list
	movs	r3, #0
	b	.L1116
.L1127:
	.align	2
.L1126:
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
	ldr	r5, .L1140
	beq	.L1129
	ldr	r3, [r5, #2324]
	ldrh	r2, [r3, r0, lsl #1]
	cmp	r2, #0
	bne	.L1130
	mov	r1, r0
	ldr	r0, .L1140+4
	bl	sftl_printk
	ldr	r3, [r5, #2324]
	ldrh	r3, [r3, r4, lsl #1]
	cbnz	r3, .L1131
	movw	r2, #2831
	ldr	r1, .L1140+8
	ldr	r0, .L1140+12
	bl	sftl_printk
.L1131:
	ldr	r3, [r5, #2324]
	movs	r2, #16
	mov	r1, r4
	ldr	r0, .L1140+16
	strh	r2, [r3, r4, lsl #1]	@ movhi
	bl	test_node_in_list
	cbz	r0, .L1132
	mov	r1, r4
	ldr	r0, .L1140+16
	bl	List_remove_node
	ldrh	r3, [r5, #2344]
	cbnz	r3, .L1133
	movw	r2, #2835
	ldr	r1, .L1140+8
	ldr	r0, .L1140+12
	bl	sftl_printk
.L1133:
	ldrh	r3, [r5, #2344]
	mov	r0, r4
	subs	r3, r3, #1
	strh	r3, [r5, #2344]	@ movhi
	bl	INSERT_DATA_LIST
	ldr	r3, [r5, #2324]
	mov	r1, r4
	ldr	r0, .L1140+20
	ldrh	r2, [r3, r4, lsl #1]
	bl	sftl_printk
.L1132:
	mov	r0, r4
	bl	FtlGcRefreshBlock
.L1136:
	movs	r6, #0
	b	.L1128
.L1130:
	subs	r2, r2, #1
	strh	r2, [r3, r0, lsl #1]	@ movhi
.L1129:
	ldrh	r0, [r5, #4000]
	movw	r3, #65535
	cmp	r0, r3
	bne	.L1135
	strh	r4, [r5, #4000]	@ movhi
	b	.L1136
.L1135:
	cmp	r4, r0
	beq	.L1136
	bl	update_vpc_list
	ldr	r2, [r5, #2316]
	adds	r6, r0, #0
	ldr	r3, [r5, #2320]
	it	ne
	movne	r6, #1
	strh	r4, [r5, #4000]	@ movhi
	subs	r3, r3, r2
	asrs	r2, r3, #1
	ldr	r3, .L1140+24
	muls	r3, r2, r3
	ldr	r2, [r5, #2324]
	uxth	r1, r3
	ldrh	r2, [r2, r1, lsl #1]
	cbnz	r2, .L1128
	cmp	r4, r1
	beq	.L1128
	movw	r2, #2856
	ldr	r1, .L1140+8
	ldr	r0, .L1140+12
	bl	sftl_printk
.L1128:
	mov	r0, r6
	pop	{r4, r5, r6, pc}
.L1141:
	.align	2
.L1140:
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
	ldr	r4, .L1160
	ldrh	r2, [r4, #2352]
	cmp	r2, #0
	beq	.L1143
	ldrb	r3, [r4, #2356]	@ zero_extendqisi2
	cmp	r3, #0
	bne	.L1143
	ldrb	r1, [r4, #2355]	@ zero_extendqisi2
	ldrh	r3, [r4, #102]
	muls	r3, r1, r3
	cmp	r2, r3
	beq	.L1143
	ldrb	r7, [r4, #2358]	@ zero_extendqisi2
	cbnz	r7, .L1142
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
	beq	.L1145
	mov	r2, r7
	movs	r1, #1
	add	r0, sp, #4
	bl	FlashReadPages
.L1146:
	ldr	r9, .L1160+4
	movs	r7, #0
	lsl	r8, r8, #2
	mov	r10, r7
	movw	r3, #61589
	strh	r3, [r5]	@ movhi
.L1147:
	cmp	r8, r7
	bne	.L1151
.L1148:
	movs	r3, #1
.L1159:
	strb	r3, [r4, #2358]
.L1142:
	add	sp, sp, #24
	@ sp needed
	pop	{r4, r5, r6, r7, r8, r9, r10, pc}
.L1145:
	ldrh	r2, [r4, #110]
	movs	r1, #255
	bl	ftl_memset
	b	.L1146
.L1151:
	ldrh	r3, [r4, #2352]
	cmp	r3, #0
	beq	.L1148
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
	b	.L1147
.L1143:
	movs	r3, #0
	b	.L1159
.L1161:
	.align	2
.L1160:
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
	ldr	r5, .L1166
	movs	r6, #12
	bl	FtlWriteDump_data
.L1163:
	ldrh	r3, [r5, #138]
	uxth	r0, r4
	cmp	r3, r0
	bhi	.L1165
	movs	r0, #0
	pop	{r4, r5, r6, pc}
.L1165:
	ldr	r2, [r5, #2492]
	uxth	r3, r4
	mla	r3, r6, r3, r2
	ldr	r3, [r3, #4]
	cmp	r3, #0
	bge	.L1164
	bl	flush_l2p_region
.L1164:
	adds	r4, r4, #1
	b	.L1163
.L1167:
	.align	2
.L1166:
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
	beq	.L1293
	ldrh	r3, [r0, #2]
	ldr	r4, .L1300
	str	r3, [sp, #8]
	ldrb	r3, [r0, #6]	@ zero_extendqisi2
	ldr	r1, [sp, #8]
	str	r3, [sp, #24]
	ldrh	r3, [r4, #102]
	cmp	r3, r1
	mov	r3, #0
	bne	.L1171
	strh	r3, [r0, #4]	@ movhi
.L1298:
	strb	r3, [fp, #6]
.L1293:
	movs	r0, #0
	add	sp, sp, #52
	@ sp needed
	pop	{r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L1171:
	ldrh	r0, [r0, #16]
.L1172:
	cmp	r0, r2
	uxth	r5, r3
	add	r3, r3, #1
	beq	.L1173
	movs	r1, #1
	bl	FtlGetLastWrittenPage
	adds	r7, r0, #1
	mov	r6, r0
	beq	.L1174
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
.L1175:
	uxth	r3, r2
	cmp	lr, r3
	bhi	.L1181
	ldr	r5, .L1300
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
.L1182:
	uxth	r2, r3
	cmp	r4, r2
	bhi	.L1187
	bne	.L1185
	adds	r3, r6, #1
	uxth	r3, r3
	str	r3, [sp, #16]
.L1294:
	ldr	r0, [r8, #4]
	ubfx	r0, r0, #10, #16
	bl	P2V_plane
	ldrh	r3, [r5, #102]
	ldr	r2, [sp, #16]
	str	r0, [sp, #12]
	cmp	r3, r2
	bne	.L1189
	ldrh	r3, [sp, #16]
	strh	r3, [fp, #2]	@ movhi
	movs	r3, #0
	strb	r3, [fp, #6]
	strh	r3, [fp, #4]	@ movhi
.L1189:
	ldr	r3, [sp, #16]
	ldr	r2, [sp, #8]
	cmp	r3, r2
	bne	.L1190
	ldr	r3, [sp, #12]
	ldr	r2, [sp, #24]
	cmp	r3, r2
	bne	.L1190
	ldr	r1, [sp, #16]
	mov	r2, r3
.L1299:
	mov	r0, fp
	bl	ftl_sb_update_avl_pages
	b	.L1293
.L1173:
	uxth	r1, r3
	adds	r1, r1, #8
	ldrh	r0, [fp, r1, lsl #1]
	b	.L1172
.L1174:
	ldr	r3, [sp, #8]
	cbz	r3, .L1176
	movw	r2, #1804
	ldr	r1, .L1300+4
	ldr	r0, .L1300+8
	bl	sftl_printk
.L1176:
	ldr	r3, [sp, #24]
	cbz	r3, .L1177
	cmp	r5, r3
	beq	.L1177
	movw	r2, #1805
	ldr	r1, .L1300+4
	ldr	r0, .L1300+8
	bl	sftl_printk
.L1177:
	movs	r3, #0
	strh	r3, [fp, #2]	@ movhi
	b	.L1298
.L1181:
	ldrh	r3, [ip], #2
	cmp	r3, r9
	beq	.L1178
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
.L1178:
	adds	r2, r2, #1
	b	.L1175
.L1187:
	ldr	r2, [r7]
	cbnz	r2, .L1183
	ldr	r10, [r7, #12]
	ldr	r2, [r10, #4]
	adds	r0, r2, #1
	beq	.L1184
	ldr	r1, [r5, #2544]
	mov	r0, r2
	bl	ftl_cmp_data_ver
	cbz	r0, .L1184
	adds	r2, r2, #1
	str	r2, [r5, #2544]
.L1184:
	ldr	r2, [r10]
	adds	r2, r2, #1
	bne	.L1186
.L1185:
	uxth	r2, r6
	uxth	r3, r3
	str	r2, [sp, #16]
	movs	r2, #20
	mla	r8, r2, r3, r8
	b	.L1294
.L1183:
	ldr	r9, [sp, #12]
.L1186:
	adds	r3, r3, #1
	adds	r7, r7, #20
	b	.L1182
.L1190:
	movw	r3, #65535
	cmp	r9, r3
	bne	.L1191
	ldrb	r3, [fp, #8]	@ zero_extendqisi2
	cmp	r3, #0
	bne	.L1192
.L1191:
	ldr	r3, [r5, #4012]
	uxth	r8, r6
	uxth	r6, r6
	ldr	r7, .L1300
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
.L1195:
	cmp	r4, r8
	bhi	.L1202
	movs	r3, #0
	ldrh	lr, [r7, #32]
	ldr	r0, [r7, #3836]
	mov	r6, r3
	ldr	r1, [sp, #20]
	mov	ip, #20
	b	.L1203
.L1197:
	ldrh	r2, [r1], #2
	cmp	r2, r9
	beq	.L1196
	mla	r10, ip, r6, r0
	adds	r6, r6, #1
	orr	r2, r4, r2, lsl #10
	uxth	r6, r6
	str	r2, [r10, #4]
.L1196:
	adds	r3, r3, #1
.L1203:
	uxth	r2, r3
	cmp	r2, lr
	bcc	.L1197
	movs	r2, #0
	mov	r1, r6
	bl	FlashReadPages
	ldr	r3, [r7, #3836]
	movs	r2, #20
	mla	r6, r2, r6, r3
.L1198:
	cmp	r6, r3
	bne	.L1201
	adds	r4, r4, #1
	uxth	r4, r4
	b	.L1195
.L1201:
	ldr	r2, [r3]
	cbnz	r2, .L1192
	ldr	r2, [r3, #12]
	ldrh	r1, [r2]
	cmp	r1, r9
	beq	.L1200
	ldr	r2, [r2, #4]
	adds	r1, r2, #1
	it	ne
	strne	r2, [r7, #4012]
.L1200:
	adds	r3, r3, #20
	b	.L1198
.L1202:
	mov	r3, #-1
	str	r3, [r7, #4012]
.L1192:
	movs	r3, #1
	ldr	r0, .L1300+12
	strh	r3, [r5, #4016]	@ movhi
	ldr	r4, .L1300
	bl	FtlMapBlkWriteDump_data
	ldr	r5, [sp, #8]
.L1204:
	ldrh	r6, [r4, #32]
	movs	r3, #0
	ldr	r0, [r4, #3836]
	mov	r10, r3
	ldr	r1, [sp, #20]
	movw	r7, #65535
	mov	ip, #20
.L1205:
	uxth	r2, r3
	cmp	r6, r2
	bhi	.L1207
	movs	r2, #0
	mov	r1, r10
	bl	FlashReadPages
	movs	r3, #0
.L1297:
	str	r3, [sp, #28]
	ldrh	r3, [sp, #28]
	cmp	r10, r3
	bhi	.L1237
	adds	r5, r5, #1
	ldrh	r3, [r4, #102]
	uxth	r5, r5
	cmp	r3, r5
	bne	.L1204
	ldrh	r2, [r4, #32]
	movw	r0, #65535
	movs	r3, #0
	strh	r5, [fp, #2]	@ movhi
	strh	r3, [fp, #4]	@ movhi
.L1238:
	uxth	r1, r3
	cmp	r1, r2
	bcs	.L1293
	ldr	r1, [sp, #20]
	ldrh	r4, [r1], #2
	cmp	r4, r0
	str	r1, [sp, #20]
	add	r1, r3, #1
	bne	.L1298
	mov	r3, r1
	b	.L1238
.L1207:
	ldrh	r2, [r1], #2
	cmp	r2, r7
	beq	.L1206
	mla	lr, ip, r10, r0
	add	r10, r10, #1
	orr	r2, r5, r2, lsl #10
	uxth	r10, r10
	str	r2, [lr, #4]
.L1206:
	adds	r3, r3, #1
	b	.L1205
.L1301:
	.align	2
.L1300:
	.word	.LANCHOR0
	.word	.LANCHOR1+421
	.word	.LC1
	.word	.LANCHOR0+3908
.L1237:
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
	bcc	.L1209
	bne	.L1210
	ldr	r3, [sp, #24]
	cmp	r3, r0
	bhi	.L1209
.L1210:
	ldr	r3, [sp, #16]
	cmp	r5, r3
	bne	.L1211
	ldr	r3, [sp, #12]
	cmp	r3, r0
	beq	.L1212
.L1211:
	ldr	r3, [r9, r7]
	adds	r3, r3, #1
	beq	.L1213
	ldr	r8, [r8, #12]
	movw	r3, #61589
	ldrh	r2, [r8]
	cmp	r2, r3
	beq	.L1214
.L1221:
	ldrh	r0, [fp]
.L1296:
	bl	decrement_vpc_count
.L1209:
	ldr	r3, [sp, #28]
	adds	r3, r3, #1
	b	.L1297
.L1214:
	ldr	r3, [r8, #4]
	adds	r6, r3, #1
	str	r3, [sp, #4]
	beq	.L1215
	ldr	r1, [r4, #2544]
	mov	r0, r3
	bl	ftl_cmp_data_ver
	cbz	r0, .L1215
	ldr	r3, [sp, #4]
	adds	r3, r3, #1
	str	r3, [r4, #2544]
.L1215:
	ldrh	r2, [r8]
	movw	r3, #61589
	cmp	r2, r3
	beq	.L1216
	movw	r2, #1954
	ldr	r1, .L1302
	ldr	r0, .L1302+4
	bl	sftl_printk
.L1216:
	ldr	r6, [r8, #8]
	add	r1, sp, #40
	ldr	r3, [r8, #12]
	movs	r2, #0
	mov	r0, r6
	str	r3, [sp, #36]
	bl	log2phys
	ldr	r1, [r4, #4012]
	adds	r0, r1, #1
	beq	.L1217
	ldr	r0, [sp, #4]
	bl	ftl_cmp_data_ver
	cmp	r0, #0
	beq	.L1217
	ldr	r3, [sp, #36]
	adds	r2, r3, #1
	beq	.L1218
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
	bne	.L1219
.L1220:
	mov	r3, #-1
	ldrh	r0, [fp]
	str	r3, [sp, #36]
	bl	decrement_vpc_count
.L1228:
	ldr	r0, [sp, #36]
	adds	r1, r0, #1
	beq	.L1209
.L1241:
	ubfx	r0, r0, #10, #16
	bl	P2V_block_in_plane
	ldrh	r3, [r4, #40]
	mov	r6, r0
	cmp	r3, r0
	bhi	.L1233
	movw	r2, #2055
	ldr	r1, .L1302
	ldr	r0, .L1302+4
	bl	sftl_printk
.L1233:
	ldr	r3, [r4, #2324]
	ldrh	r3, [r3, r6, lsl #1]
	cmp	r3, #0
	beq	.L1234
	mov	r0, r6
	b	.L1296
.L1218:
	ldr	r3, [sp, #44]
	ldr	r2, [sp, #40]
	cmp	r2, r3
	bne	.L1221
	movs	r2, #1
	add	r1, sp, #36
	mov	r0, r6
	bl	log2phys
	b	.L1221
.L1219:
	ldr	r1, [r8, #8]
	cmp	r6, r1
	bne	.L1220
	ldr	r9, [r8, #4]
	ldr	r0, [r4, #4012]
	mov	r1, r9
	bl	ftl_cmp_data_ver
	cmp	r0, #0
	beq	.L1220
	ldr	r1, [sp, #40]
	ldr	r0, [sp, #44]
	cmp	r1, r0
	bne	.L1223
.L1226:
	ldr	r1, [sp, #36]
	mov	r0, r6
	bl	FtlReUsePrevPpa
	b	.L1220
.L1223:
	ldr	r0, [sp, #36]
	cmp	r1, r0
	beq	.L1220
	adds	r0, r1, #1
	beq	.L1224
	str	r1, [r3, #4]
	movs	r2, #0
	movs	r1, #1
	mov	r0, r3
	ldr	r8, [r3, #12]
	bl	FlashReadPages
.L1225:
	ldr	r3, [r4, #3836]
	ldr	r3, [r3, r7]
	adds	r3, r3, #1
	beq	.L1226
	ldr	r3, [r8, #4]
	ldr	r0, [r4, #4012]
	mov	r1, r3
	bl	ftl_cmp_data_ver
	cmp	r0, #0
	beq	.L1226
	mov	r1, r3
	mov	r0, r9
	bl	ftl_cmp_data_ver
	cmp	r0, #0
	beq	.L1220
	b	.L1226
.L1224:
	str	r1, [r2, r7]
	b	.L1225
.L1217:
	ldr	r3, [sp, #44]
	ldr	r2, [sp, #40]
	cmp	r2, r3
	beq	.L1228
	movs	r2, #1
	add	r1, sp, #44
	mov	r0, r6
	bl	log2phys
	ldr	r8, [sp, #40]
	cmp	r8, #-1
	beq	.L1228
	ldr	r3, [sp, #36]
	cmp	r8, r3
	beq	.L1231
	ubfx	r0, r8, #10, #16
	bl	P2V_block_in_plane
	ldrh	r3, [r4, #2348]
	cmp	r3, r0
	beq	.L1232
	ldrh	r3, [r4, #2396]
	cmp	r3, r0
	beq	.L1232
	ldrh	r3, [r4, #2444]
	cmp	r3, r0
	bne	.L1228
.L1232:
	ldr	r0, [r4, #3836]
	movs	r2, #0
	movs	r1, #1
	str	r8, [r0, #4]
	ldr	r7, [r0, #12]
	bl	FlashReadPages
	ldr	r3, [r4, #3836]
	ldr	r3, [r3]
	adds	r3, r3, #1
	beq	.L1228
	ldr	r1, [r7, #4]
	ldr	r0, [sp, #4]
	bl	ftl_cmp_data_ver
	cmp	r0, #0
	bne	.L1228
	movs	r2, #1
	add	r1, sp, #40
	mov	r0, r6
	bl	log2phys
	b	.L1228
.L1234:
	mov	r1, r6
	ldr	r0, .L1302+8
	bl	sftl_printk
	b	.L1209
.L1213:
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
	bne	.L1236
	ldr	r3, [sp, #4]
.L1295:
	str	r3, [r4, #4012]
	b	.L1209
.L1236:
	ldr	r2, [sp, #4]
	cmp	r2, r3
	bcs	.L1209
	mov	r3, r2
	b	.L1295
.L1212:
	ldrb	r3, [sp, #12]	@ zero_extendqisi2
	mov	r1, r5
	strh	r5, [fp, #2]	@ movhi
	ldr	r2, [sp, #12]
	strb	r3, [fp, #6]
	b	.L1299
.L1231:
	mov	r0, r8
	b	.L1241
.L1303:
	.align	2
.L1302:
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
	ldr	r4, .L1315
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
.L1305:
	subs	r7, r7, #1
	beq	.L1307
	ldrh	r3, [r5, #4]
	cbnz	r3, .L1306
.L1307:
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
.L1306:
	mov	r0, r5
	bl	get_new_active_ppa
	str	r0, [sp, #8]
	adds	r0, r0, #1
	beq	.L1307
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
	b	.L1305
.L1316:
	.align	2
.L1315:
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
	ldr	r4, .L1346
	ldr	r3, [r4, #3284]
	add	r7, r4, #3264
	ldr	r6, [r4, #3288]
	str	r3, [r4, #3272]
	str	r6, [r4, #3276]
	bl	FtlBbtMemInit
	ldrh	r5, [r4, #98]
	subs	r5, r5, #1
	uxth	r5, r5
.L1318:
	ldrh	r3, [r4, #98]
	subs	r3, r3, #16
	cmp	r5, r3
	ble	.L1321
	lsls	r3, r5, #10
	movs	r2, #1
	mov	r1, r2
	mov	r0, r7
	str	r3, [r4, #3268]
	bl	FlashReadPages
	ldr	r3, [r4, #3264]
	adds	r3, r3, #1
	bne	.L1319
	ldr	r3, [r4, #3268]
	movs	r2, #1
	mov	r1, r2
	mov	r0, r7
	adds	r3, r3, #1
	str	r3, [r4, #3268]
	bl	FlashReadPages
.L1319:
	ldr	r3, [r4, #3264]
	adds	r3, r3, #1
	beq	.L1320
	ldrh	r3, [r6]
	cmp	r3, r8
	bne	.L1320
	ldr	r3, [r6, #4]
	strh	r5, [r4, #152]	@ movhi
	str	r3, [r4, #160]
	ldrh	r3, [r6, #8]
	strh	r3, [r4, #156]	@ movhi
.L1321:
	ldrh	r3, [r4, #152]
	movw	r2, #65535
	cmp	r3, r2
	beq	.L1335
	ldrh	r3, [r4, #156]
	cmp	r3, r2
	beq	.L1325
	lsls	r3, r3, #10
	movs	r2, #1
	mov	r1, r2
	ldr	r0, .L1346+4
	str	r3, [r4, #3268]
	bl	FlashReadPages
	ldr	r3, [r4, #3264]
	adds	r3, r3, #1
	beq	.L1325
	ldrh	r2, [r6]
	movw	r3, #61649
	cmp	r2, r3
	bne	.L1325
	ldr	r3, [r6, #4]
	ldr	r2, [r4, #160]
	cmp	r3, r2
	bls	.L1325
	ldrh	r2, [r4, #156]
	str	r3, [r4, #160]
	ldrh	r3, [r6, #8]
	strh	r2, [r4, #152]	@ movhi
	strh	r3, [r4, #156]	@ movhi
.L1325:
	ldr	r7, .L1346+4
	movs	r1, #1
	ldrh	r0, [r4, #152]
	movw	r8, #61649
	bl	FtlGetLastWrittenPage
	sxth	r5, r0
	adds	r0, r0, #1
	strh	r0, [r4, #154]	@ movhi
.L1327:
	cmp	r5, #0
	bge	.L1330
	movs	r2, #254
	ldr	r1, .L1346+8
	ldr	r0, .L1346+12
	bl	sftl_printk
.L1329:
	ldrh	r3, [r6, #10]
	ldrh	r0, [r6, #12]
	strh	r3, [r4, #158]	@ movhi
	movw	r3, #65535
	cmp	r0, r3
	beq	.L1332
	ldr	r2, [r4, #28]
	cmp	r0, r2
	beq	.L1332
	ldrh	r3, [r4, #42]
	lsrs	r3, r3, #2
	cmp	r2, r3
	bcs	.L1332
	cmp	r0, r3
	bcs	.L1332
	bl	FtlSysBlkNumInit
.L1332:
	ldr	r6, .L1346+16
	movs	r5, #0
.L1333:
	ldrh	r3, [r4, #54]
	cmp	r5, r3
	bcc	.L1334
	movs	r0, #0
	pop	{r4, r5, r6, r7, r8, pc}
.L1320:
	subs	r5, r5, #1
	uxth	r5, r5
	b	.L1318
.L1330:
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
	beq	.L1328
	ldrh	r3, [r6]
	cmp	r3, r8
	beq	.L1329
.L1328:
	subs	r5, r5, #1
	sxth	r5, r5
	b	.L1327
.L1334:
	ldrh	r2, [r4, #3904]
	ldr	r1, [r4, #3272]
	ldr	r0, [r6, #4]!
	lsls	r2, r2, #2
	mla	r1, r5, r2, r1
	adds	r5, r5, #1
	bl	ftl_memcpy
	b	.L1333
.L1335:
	mov	r0, #-1
	pop	{r4, r5, r6, r7, r8, pc}
.L1347:
	.align	2
.L1346:
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
	ldr	r9, .L1367+4
	bl	FtlBbtMemInit
	sub	r5, r9, #180
	sub	r10, r9, #18
	mov	r4, r5
	bl	FtlLoadFactoryBbt
.L1349:
	ldrh	r3, [r5, #54]
	cmp	r8, r3
	bcc	.L1355
	movs	r5, #0
.L1356:
	ldrh	r3, [r4, #114]
	uxth	r0, r5
	adds	r5, r5, #1
	cmp	r3, r0
	bhi	.L1357
	ldrh	r5, [r4, #164]
	movw	r6, #65535
	subs	r5, r5, #1
	uxth	r5, r5
.L1358:
	ldrh	r3, [r4, #164]
	subs	r3, r3, #48
	cmp	r5, r3
	ble	.L1362
	mov	r0, r5
	bl	FtlBbmIsBadBlock
	cmp	r0, #1
	beq	.L1359
	mov	r0, r5
	bl	FlashTestBlk
	cmp	r0, #0
	beq	.L1360
	mov	r0, r5
	bl	FtlBbmMapBadBlock
.L1359:
	subs	r5, r5, #1
	uxth	r5, r5
	b	.L1358
.L1355:
	ldrh	r3, [r10, #2]!
	movw	r2, #65535
	ldr	r0, [r5, #3284]
	ldr	fp, [r5, #3288]
	cmp	r3, r2
	str	r0, [r5, #3272]
	str	fp, [r5, #3276]
	beq	.L1350
	ldrh	r7, [r5, #98]
	movs	r2, #1
	mov	r1, r2
	ldr	r0, .L1367
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
.L1351:
	uxth	r0, r7
	add	r8, r8, #1
	add	r9, r9, #4
	bl	FtlBbmMapBadBlock
	b	.L1349
.L1350:
	mov	r1, r8
	bl	FlashGetBadBlockList
	ldr	r1, [r9]
	ldr	r0, [r5, #3272]
	bl	FtlBbt2Bitmap
	ldrh	r6, [r5, #98]
.L1353:
	subs	r6, r6, #1
	uxth	r6, r6
.L1352:
	ldrh	r0, [r4, #98]
	smlabb	r0, r0, r8, r6
	uxth	r0, r0
	bl	FtlBbmIsBadBlock
	cmp	r0, #1
	beq	.L1353
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
	ldr	r0, .L1367
	mov	r1, r2
	bl	FlashEraseBlocks
	movs	r3, #1
	ldr	r0, .L1367
	mov	r2, r3
	mov	r1, r3
	bl	FlashProgPages
	ldr	r3, [r4, #3264]
	adds	r3, r3, #1
	bne	.L1351
	uxth	r0, r7
	bl	FtlBbmMapBadBlock
	b	.L1352
.L1357:
	bl	FtlBbmMapBadBlock
	b	.L1356
.L1360:
	ldrh	r3, [r4, #152]
	cmp	r3, r6
	bne	.L1361
	strh	r5, [r4, #152]	@ movhi
	b	.L1359
.L1361:
	strh	r5, [r4, #156]	@ movhi
.L1362:
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
.L1368:
	.align	2
.L1367:
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
	ldr	r4, .L1381
	adds	r2, r0, r1
	.pad #108
	sub	sp, sp, #108
	mov	r8, r0
	mov	r6, r1
	ldrh	r3, [r4, #96]
	cmp	r2, r3
	bhi	.L1377
	ldrh	r7, [r4, #108]
	mov	r9, #0
	lsr	r7, r0, r7
	lsl	fp, r7, #2
.L1371:
	cbnz	r6, .L1376
.L1369:
	mov	r0, r9
	add	sp, sp, #108
	@ sp needed
	pop	{r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L1376:
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
	cbz	r2, .L1373
	cmp	r5, r3
	beq	.L1373
	ldr	r3, [r4, #3852]
	add	r0, sp, #20
	str	r2, [sp, #24]
	movs	r2, #1
	mov	r1, r2
	str	r3, [sp, #28]
	add	r3, sp, #40
	str	r3, [sp, #32]
	bl	FlashReadPages
.L1374:
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
	ldr	r0, .L1381+4
	adds	r7, r7, #1
	bl	FtlMapWritePage
	ldr	r3, [sp, #8]
	adds	r0, r0, #1
	it	eq
	moveq	r9, #-1
	add	r10, r10, r3
	b	.L1371
.L1373:
	ldrh	r2, [r4, #110]
	movs	r1, #0
	ldr	r0, [r4, #3852]
	bl	ftl_memset
	b	.L1374
.L1377:
	mov	r9, #-1
	b	.L1369
.L1382:
	.align	2
.L1381:
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
	ldr	r2, .L1385
	ldr	r3, .L1385+4
	ldr	r1, [r2, #2636]
	cmp	r1, r3
	bne	.L1383
	ldr	r3, .L1385+8
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
.L1383:
	bx	lr
.L1386:
	.align	2
.L1385:
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
	ldr	r2, .L1392
	push	{r3, lr}
	.save {r3, lr}
	ldrh	r3, [r2, #-192]
	cmp	r3, #31
	itett	ls
	addls	r3, r3, #1
	movhi	r2, #32
	strhls	r3, [r2, #-192]	@ movhi
	movls	r2, #1
	ldr	r3, .L1392+4
	cbnz	r0, .L1389
	ldr	r1, [r3, #3864]
	ldr	r0, [r1, #20]
	ldr	r1, [r1, #16]
	add	r2, r2, r0
	cmp	r1, r2
	bcc	.L1390
.L1389:
	ldr	r2, [r3, #3864]
	movs	r0, #64
	ldr	r1, [r2, #16]
	str	r1, [r2, #20]
	ldr	r1, .L1392+8
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
.L1390:
	movs	r0, #0
	pop	{r3, pc}
.L1393:
	.align	2
.L1392:
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
	ldr	r4, .L1405
	adds	r2, r0, r1
	.pad #108
	sub	sp, sp, #108
	mov	r8, r0
	mov	r7, r1
	ldrh	r3, [r4, #96]
	cmp	r2, r3
	bhi	.L1404
	ldrh	r6, [r4, #108]
	mov	r9, #0
	lsr	r6, r0, r6
	lsl	fp, r6, #2
.L1397:
	cbnz	r7, .L1403
.L1395:
	mov	r0, r9
	add	sp, sp, #108
	@ sp needed
	pop	{r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L1403:
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
	cbz	r3, .L1399
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
	bne	.L1401
	mov	r2, r3
	mov	r1, r6
	ldr	r0, .L1405+4
	bl	sftl_printk
	ldr	r2, [r4, #3852]
	mov	r1, r6
	ldr	r0, .L1405+8
	bl	FtlMapWritePage
.L1401:
	ldr	r1, [r4, #3852]
	lsls	r2, r5, #9
	ldr	r3, [sp, #4]
	mov	r0, r10
	add	r1, r1, r3, lsl #9
	bl	ftl_memcpy
.L1402:
	ldr	r3, [sp, #8]
	adds	r6, r6, #1
	subs	r7, r7, r5
	add	r8, r8, r5
	add	fp, fp, #4
	add	r10, r10, r3
	b	.L1397
.L1399:
	lsls	r2, r5, #9
	mov	r1, r3
	mov	r0, r10
	bl	ftl_memset
	b	.L1402
.L1404:
	mov	r9, #-1
	b	.L1395
.L1406:
	.align	2
.L1405:
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
	ldr	r4, .L1409
	ldr	r2, [r4, #3864]
	ldrh	r1, [r4, #3856]
	bl	FtlVendorPartRead
	ldr	r3, [r4, #3864]
	ldr	r2, [r3]
	ldr	r3, .L1409+4
	cmp	r2, r3
	beq	.L1408
	ldr	r1, .L1409+8
	ldr	r0, .L1409+12
	bl	sftl_printk
	ldrh	r2, [r4, #3856]
	movs	r1, #0
	ldr	r0, [r4, #3864]
	lsls	r2, r2, #9
	bl	ftl_memset
.L1408:
	movs	r0, #0
	pop	{r4, pc}
.L1410:
	.align	2
.L1409:
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
	ldr	r4, .L1414
	movs	r0, #0
	ldr	r5, .L1414+4
	addw	r2, r4, #2636
	bl	FtlVendorPartRead
	ldr	r3, [r4, #2636]
	cmp	r3, r5
	beq	.L1412
	mov	r1, #512
	addw	r0, r4, #2636
	bl	__memzero
	str	r5, [r4, #2636]
.L1412:
	ldr	r3, [r4, #2636]
	cmp	r3, r5
	bne	.L1413
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
.L1413:
	ldrh	r2, [r4, #92]
	ldr	r3, [r4, #2552]
	ldr	r0, [r4, #2548]
	ldrh	r1, [r4, #40]
	mla	r0, r0, r2, r3
	bl	__aeabi_uidiv
	str	r0, [r4, #2556]
	pop	{r3, r4, r5, pc}
.L1415:
	.align	2
.L1414:
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
	ldr	r4, .L1432
	movs	r1, #255
	movw	r7, #65535
	ldr	r9, .L1432+16
	ldr	r3, [r4, #3284]
	add	r8, r4, #3264
	ldr	r6, [r4, #3288]
	ldr	r10, .L1432+20
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
	ldr	r3, .L1432+4
	str	r3, [r4, #2268]
	ldr	r3, .L1432+8
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
.L1418:
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
	blt	.L1419
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
.L1419:
	ldrh	r3, [r4, #2578]
	ldr	r2, [r4, #3264]
	adds	r3, r3, #1
	uxth	r3, r3
	adds	r1, r2, #1
	strh	r3, [r4, #2578]	@ movhi
	bne	.L1420
	cmp	r3, #1
	bne	.L1421
	movw	r2, #1169
	mov	r1, r9
	mov	r0, r10
	bl	sftl_printk
.L1421:
	ldrh	r3, [r4, #2578]
	adds	r5, r5, #1
	uxth	r5, r5
	cmp	r3, #1
	ittt	eq
	ldrheq	r3, [r4, #104]
	addeq	r3, r3, #-1
	strheq	r3, [r4, #2578]	@ movhi
	cmp	r5, #3
	bls	.L1418
	mov	r2, r5
	ldr	r1, [r4, #3268]
	ldr	r0, .L1432+12
	bl	sftl_printk
.L1424:
	b	.L1424
.L1420:
	cmp	r3, #1
	beq	.L1418
	cmp	r2, #256
	beq	.L1418
	movw	r3, #65535
	cmp	r7, r3
	beq	.L1425
	movs	r1, #1
	mov	r0, r7
	bl	FtlFreeSysBlkQueueIn
.L1425:
	movs	r0, #0
	pop	{r4, r5, r6, r7, r8, r9, r10, pc}
.L1433:
	.align	2
.L1432:
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
	mov	r8, r0
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
	ldrh	r4, [r6, #58]
	mov	r1, r4
	bl	__aeabi_uidiv
	smulbb	r3, r0, r4
	mov	r7, r0
	sub	r8, r8, r3
	uxth	r3, r8
	cbz	r3, .L1442
	subs	r4, r4, r3
	adds	r7, r7, #1
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
	ldr	r2, [r3, #-188]
	cmp	r2, #32
	bls	.L1446
	movs	r4, #0
	str	r4, [r3, #-188]
	bl	l2p_flush
	bl	FtlVpcTblFlush
	b	.L1446
.L1445:
	movs	r2, #0
	mov	r1, sp
	mov	r0, r7
	bl	log2phys
	ldr	r3, [sp]
	adds	r3, r3, #1
	beq	.L1444
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
.L1444:
	ldrh	r3, [r6, #58]
	adds	r7, r7, #1
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
	ldr	r4, .L1468
	ldr	r1, .L1468+4
	ldr	r0, .L1468+8
	bl	sftl_printk
	ldrh	r2, [r4, #42]
	movs	r1, #0
	ldr	r0, [r4, #3868]
	lsls	r2, r2, #1
	bl	ftl_memset
.L1457:
	ldr	r3, [r4, #2504]
	cmp	r5, r3
	bcc	.L1459
	ldr	r9, .L1468+12
	movs	r7, #0
	movw	r8, #65535
.L1460:
	ldrh	r3, [r4, #40]
	uxth	r5, r7
	cmp	r3, r5
	bhi	.L1464
	add	sp, sp, #12
	@ sp needed
	pop	{r4, r5, r6, r7, r8, r9, pc}
.L1459:
	movs	r2, #0
	add	r1, sp, #4
	mov	r0, r5
	bl	log2phys
	ldr	r0, [sp, #4]
	adds	r3, r0, #1
	beq	.L1458
	ubfx	r0, r0, #10, #16
	bl	P2V_block_in_plane
	ldr	r2, [r4, #3868]
	ldrh	r3, [r2, r0, lsl #1]
	adds	r3, r3, #1
	strh	r3, [r2, r0, lsl #1]	@ movhi
.L1458:
	adds	r5, r5, #1
	b	.L1457
.L1464:
	ldr	r3, [r4, #2324]
	uxth	r6, r7
	ldrh	r2, [r3, r6, lsl #1]
	ldr	r3, [r4, #3868]
	ldrh	r3, [r3, r6, lsl #1]
	cmp	r2, r3
	beq	.L1462
	cmp	r2, r8
	beq	.L1462
	mov	r1, r6
	mov	r0, r9
	bl	sftl_printk
	ldrh	r3, [r4, #2348]
	cmp	r3, r5
	beq	.L1462
	ldrh	r3, [r4, #2444]
	cmp	r3, r5
	beq	.L1462
	ldrh	r3, [r4, #2396]
	cmp	r3, r5
	beq	.L1462
	ldr	r3, [r4, #3868]
	mov	r0, r5
	ldrh	r2, [r3, r6, lsl #1]
	ldr	r3, [r4, #2324]
	strh	r2, [r3, r6, lsl #1]	@ movhi
	bl	update_vpc_list
	bl	l2p_flush
	bl	FtlVpcTblFlush
.L1462:
	adds	r7, r7, #1
	b	.L1460
.L1469:
	.align	2
.L1468:
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
	ldr	r4, .L1480
	ldrh	r5, [r0]
	ldrh	r3, [r4, #40]
	cmp	r3, r5
	bcs	.L1471
	movw	r2, #2750
	ldr	r1, .L1480+4
	ldr	r0, .L1480+8
	bl	sftl_printk
.L1471:
	movw	r3, #65535
	cmp	r5, r3
	beq	.L1472
	ldr	r3, [r4, #2324]
	mov	r0, r5
	ldrh	r3, [r3, r5, lsl #1]
	cbz	r3, .L1473
	bl	INSERT_DATA_LIST
.L1472:
	ldrh	r0, [r4, #4000]
	movs	r3, #1
	strb	r3, [r6, #8]
	movw	r3, #65535
	cmp	r0, r3
	beq	.L1474
	cmp	r5, r0
	bne	.L1475
	ldr	r3, [r4, #2324]
	ldrh	r3, [r3, r0, lsl #1]
	cbz	r3, .L1476
.L1475:
	bl	update_vpc_list
.L1476:
	movw	r3, #65535
	strh	r3, [r4, #4000]	@ movhi
.L1474:
	mov	r0, r6
	bl	allocate_data_superblock
	bl	l2p_flush
	movs	r0, #0
	bl	FtlEctTblFlush
	bl	FtlVpcTblFlush
	movs	r0, #0
	pop	{r4, r5, r6, pc}
.L1473:
	bl	INSERT_FREE_LIST
	b	.L1472
.L1481:
	.align	2
.L1480:
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
	ldr	r6, .L1496
	.pad #20
	sub	sp, sp, #20
	mov	r4, r3
	movs	r2, #0
	ldr	r10, .L1496+8
	mov	r5, r0
	ldr	fp, .L1496+12
	mov	r9, r1
	ldrb	r3, [r3, #9]	@ zero_extendqisi2
	bl	FlashProgPages
.L1483:
	cmp	r7, r9
	beq	.L1490
	mov	r8, #0
	b	.L1491
.L1486:
	ldr	r0, [r5, #4]
	ubfx	r0, r0, #10, #16
	bl	P2V_block_in_plane
	ldrh	r3, [r4]
	cmp	r3, r0
	bne	.L1484
	ldr	r1, [r6, #2324]
	ldrh	r0, [r4, #4]
	ldrh	r2, [r1, r3, lsl #1]
	subs	r2, r2, r0
	strh	r2, [r1, r3, lsl #1]	@ movhi
	ldrh	r3, [r6, #102]
	strb	r8, [r4, #6]
	strh	r8, [r4, #4]	@ movhi
	strh	r3, [r4, #2]	@ movhi
.L1484:
	ldrh	r3, [r4, #4]
	cbnz	r3, .L1485
	mov	r0, r4
	bl	allocate_new_data_superblock
.L1485:
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
.L1491:
	ldr	r3, [r5]
	adds	r3, r3, #1
	beq	.L1486
	ldrb	r2, [r4, #6]	@ zero_extendqisi2
	ldrh	r3, [r6, #32]
	cmp	r2, r3
	bcc	.L1487
	mov	r2, #936
	mov	r1, r10
	mov	r0, fp
	bl	sftl_printk
.L1487:
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
	beq	.L1488
	ldr	r3, [r6, #2324]
	ldrh	r2, [r3, r0, lsl #1]
	cbnz	r2, .L1489
	mov	r1, r0
	ldr	r0, .L1496+4
	bl	sftl_printk
.L1489:
	mov	r0, r8
	bl	decrement_vpc_count
.L1488:
	adds	r7, r7, #1
	adds	r5, r5, #20
	b	.L1483
.L1490:
	ldrb	r2, [r4, #6]	@ zero_extendqisi2
	ldrh	r3, [r6, #32]
	cmp	r2, r3
	bcc	.L1482
	movw	r2, #951
	ldr	r1, .L1496+8
	ldr	r0, .L1496+12
	bl	sftl_printk
.L1482:
	add	sp, sp, #20
	@ sp needed
	pop	{r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L1497:
	.align	2
.L1496:
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
	ldr	r4, .L1524
	ldrh	r5, [r4, #2444]
	ldrh	r1, [r4, #102]
	cmp	r5, r2
	bne	.L1499
.L1509:
	ldrh	r2, [r4, #2444]
	movs	r3, #0
	str	r3, [r4, #4008]
	movw	r3, #65535
	cmp	r2, r3
	bne	.L1522
.L1500:
	movs	r0, #0
.L1498:
	add	sp, sp, #12
	@ sp needed
	pop	{r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L1499:
	cbz	r0, .L1502
	ldr	r3, .L1524+4
	ldrh	r0, [r3, #4]
	cmp	r0, r2
	beq	.L1503
.L1504:
	movs	r1, #2
.L1502:
	ldr	r0, .L1524+8
	bl	FtlGcScanTempBlk
	str	r0, [sp, #4]
	adds	r0, r0, #1
	beq	.L1505
	ldr	r2, [r4, #2328]
	ldrh	r3, [r2, r5, lsl #1]
	cmp	r3, #4
	bls	.L1506
	subs	r3, r3, #5
	movs	r0, #1
	strh	r3, [r2, r5, lsl #1]	@ movhi
	bl	FtlEctTblFlush
.L1506:
	ldr	r3, [r4, #4008]
	cbnz	r3, .L1507
	ldr	r3, [r4, #2732]
	ldr	r0, [sp, #4]
	adds	r3, r3, #1
	ubfx	r0, r0, #10, #16
	str	r3, [r4, #2732]
	bl	FtlBbmMapBadBlock
	bl	FtlBbmTblFlush
.L1507:
	movs	r3, #0
	str	r3, [r4, #4008]
.L1518:
	movs	r0, #1
	b	.L1498
.L1503:
	movs	r2, #0
	strh	r2, [r3, #4]	@ movhi
	ldrh	r3, [r4, #2344]
	cmp	r3, #17
	bhi	.L1504
	b	.L1502
.L1505:
	ldr	r3, .L1524+4
	ldrh	r2, [r3, #4]
	movw	r3, #65535
	cmp	r2, r3
	bne	.L1518
	b	.L1509
.L1522:
	ldrb	r1, [r4, #2451]	@ zero_extendqisi2
	ldrh	r3, [r4, #102]
	ldrh	r2, [r4, #3200]
	muls	r3, r1, r3
	cmp	r2, r3
	beq	.L1510
	movs	r2, #162
	ldr	r1, .L1524+12
	ldr	r0, .L1524+16
	bl	sftl_printk
.L1510:
	ldrb	r3, [r4, #2451]	@ zero_extendqisi2
	movs	r6, #0
	ldrh	r0, [r4, #102]
	mov	r9, #12
	ldr	r2, [r4, #2324]
	ldrh	r1, [r4, #2444]
	ldr	r10, .L1524+12
	smulbb	r3, r3, r0
	ldr	fp, .L1524+16
	strh	r3, [r2, r1, lsl #1]	@ movhi
	ldr	r2, [r4, #2528]
	ldrh	r3, [r4, #3200]
	add	r3, r3, r2
	str	r3, [r4, #2528]
.L1511:
	ldrh	r2, [r4, #3200]
	uxth	r3, r6
	cmp	r2, r3
	bhi	.L1515
	movw	r0, #65535
	bl	decrement_vpc_count
	ldrh	r0, [r4, #2444]
	ldr	r3, [r4, #2324]
	ldrh	r3, [r3, r0, lsl #1]
	cmp	r3, #0
	beq	.L1516
	bl	INSERT_DATA_LIST
.L1517:
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
	ble	.L1500
	movs	r3, #20
	strh	r6, [r4, #2588]	@ movhi
	strh	r3, [r4, #3148]	@ movhi
	b	.L1500
.L1515:
	uxth	r7, r6
	ldr	r8, [r4, #3196]
	ldr	r3, [r4, #2504]
	mul	r7, r9, r7
	add	r5, r8, r7
	ldr	r2, [r5, #8]
	cmp	r2, r3
	bcc	.L1512
	movs	r2, #168
	mov	r1, r10
	mov	r0, fp
	bl	sftl_printk
.L1512:
	movs	r2, #0
	add	r1, sp, #4
	ldr	r0, [r5, #8]
	bl	log2phys
	ldr	r0, [r8, r7]
	ldr	r3, [sp, #4]
	cmp	r0, r3
	bne	.L1513
	ubfx	r0, r0, #10, #16
	bl	P2V_block_in_plane
	movs	r2, #1
	mov	r7, r0
	adds	r1, r5, #4
	ldr	r0, [r5, #8]
	bl	log2phys
	mov	r0, r7
.L1523:
	bl	decrement_vpc_count
.L1514:
	adds	r6, r6, #1
	b	.L1511
.L1513:
	ldr	r2, [r5, #4]
	cmp	r3, r2
	beq	.L1514
	ldrh	r0, [r4, #2444]
	b	.L1523
.L1516:
	bl	INSERT_FREE_LIST
	b	.L1517
.L1525:
	.align	2
.L1524:
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
	ldr	r4, .L1528
	ldrh	r1, [r4, #102]
	addw	r0, r4, #2444
	bl	FtlGcScanTempBlk
	ldrh	r2, [r4, #2446]
	ldrh	r3, [r4, #102]
	cmp	r2, r3
	bcc	.L1526
	addw	r0, r4, #3908
	bl	FtlMapBlkWriteDump_data
	movs	r0, #0
	bl	FtlGcFreeTempBlock
	movs	r3, #0
	str	r3, [r4, #4008]
.L1526:
	pop	{r4, pc}
.L1529:
	.align	2
.L1528:
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
	ldr	r4, .L1531
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
.L1532:
	.align	2
.L1531:
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
	ldr	r4, .L1540
	movs	r5, #0
	movs	r6, #20
	mov	r2, r3
	ldr	r1, [r4, #3164]
	ldr	r0, [r4, #3840]
	bl	FlashProgPages
.L1534:
	ldr	r1, [r4, #3164]
	uxth	r3, r5
	cmp	r1, r3
	bhi	.L1537
	ldr	r0, [r4, #3840]
	bl	FtlGcBufFree
	ldrh	r3, [r4, #2448]
	movs	r0, #0
	str	r0, [r4, #3164]
	cbnz	r3, .L1533
	movs	r0, #1
	bl	FtlGcFreeTempBlock
	b	.L1539
.L1537:
	muls	r3, r6, r3
	ldr	r2, [r4, #3840]
	adds	r5, r5, #1
	adds	r1, r2, r3
	ldr	r2, [r2, r3]
	adds	r0, r2, #1
	bne	.L1535
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
.L1539:
	movs	r0, #1
.L1533:
	pop	{r4, r5, r6, pc}
.L1535:
	ldr	r3, [r1, #12]
	ldr	r1, [r1, #4]
	ldr	r2, [r3, #8]
	ldr	r0, [r3, #12]
	bl	FtlGcUpdatePage
	b	.L1534
.L1541:
	.align	2
.L1540:
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
	ldr	r4, .L1545
	ldrh	r2, [r4, #2444]
	cmp	r2, r3
	beq	.L1543
	ldrh	r3, [r4, #2448]
	cbnz	r3, .L1544
.L1543:
	movs	r0, #0
	movs	r5, #0
	bl	FtlGcFreeTempBlock
	ldr	r0, .L1545+4
	strb	r5, [r4, #2452]
	bl	allocate_data_superblock
	strh	r5, [r4, #3192]	@ movhi
	strh	r5, [r4, #3200]	@ movhi
	bl	l2p_flush
	mov	r0, r5
	bl	FtlEctTblFlush
	bl	FtlVpcTblFlush
.L1544:
	ldr	r0, .L1545+4
	pop	{r3, r4, r5, lr}
	b	get_new_active_ppa
.L1546:
	.align	2
.L1545:
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
	ldr	r3, .L1665
	push	{r4, r5, r6, r7, r8, r9, r10, fp, lr}
	.save {r4, r5, r6, r7, r8, r9, r10, fp, lr}
	mov	r7, r0
	.pad #36
	sub	sp, sp, #36
	mov	r4, r3
	ldr	r0, [r3, #3824]
	cmp	r0, #0
	bne	.L1621
	ldrh	r2, [r3, #2336]
	cmp	r2, #47
	bls	.L1547
	ldr	r2, .L1665+4
	ldrh	r1, [r2, #4]
	movw	r2, #65535
	cmp	r1, r2
	bne	.L1549
.L1552:
	ldrh	r0, [r4, #3204]
	movw	r2, #65535
	cmp	r0, r2
	bne	.L1550
.L1551:
	ldr	r3, [r4, #3152]
	movw	r5, #65535
	ldrh	r1, [r4, #2588]
	adds	r3, r3, #1
	add	r3, r3, r7, lsl #7
	cmp	r1, r5
	str	r3, [r4, #3152]
	bne	.L1553
	ldrh	r2, [r4, #2444]
	cmp	r2, r1
	bne	.L1554
	ldrh	r8, [r4, #3202]
	cmp	r8, r2
	bne	.L1555
	ldrh	r2, [r4, #2344]
	cmp	r2, #24
	ite	cc
	movcc	r2, #5120
	movcs	r2, #1024
	cmp	r3, r2
	bls	.L1555
	ldr	r3, .L1665+8
	movs	r6, #0
	str	r6, [r4, #3152]
	strh	r6, [r3, #-184]	@ movhi
	bl	GetSwlReplaceBlock
	cmp	r0, r8
	mov	r5, r0
	bne	.L1625
	ldrh	r2, [r4, #2344]
	ldrh	r3, [r4, #3150]
	cmp	r2, r3
	bcs	.L1558
	movs	r0, #64
	bl	List_get_gc_head_node
	uxth	r3, r0
	cmp	r3, r5
	beq	.L1560
	mov	r0, r3
	ldr	r3, [r4, #2324]
	ldrh	r3, [r3, r0, lsl #1]
	cmp	r3, #7
	bhi	.L1561
	mov	r0, r6
	bl	List_get_gc_head_node
	uxth	r6, r0
	movs	r3, #128
	strh	r3, [r4, #3150]	@ movhi
	cmp	r6, r5
	bne	.L1557
.L1560:
	bl	FtlGcReFreshBadBlk
	cmp	r7, #0
	bne	.L1563
	movw	r3, #65535
	cmp	r5, r3
	bne	.L1563
.L1619:
	ldrh	r3, [r4, #2344]
	cmp	r3, #24
	bhi	.L1626
	cmp	r3, #16
	ldrh	r6, [r4, #102]
	bls	.L1565
	lsrs	r6, r6, #5
.L1564:
	ldrh	r2, [r4, #3148]
	cmp	r2, r3
	bcs	.L1568
	ldrh	r3, [r4, #2444]
	movw	r2, #65535
	cmp	r3, r2
	bne	.L1569
	ldrh	r2, [r4, #3202]
	cmp	r2, r3
	bne	.L1569
	ldr	r3, .L1665+8
	ldrh	r0, [r3, #-184]
	cbnz	r0, .L1570
	ldr	r3, [r4, #2504]
	ldr	r2, [r4, #2500]
	add	r3, r3, r3, lsl #1
	cmp	r2, r3, lsr #2
	bcs	.L1571
.L1570:
	ldrh	r3, [r4, #2572]
	add	r3, r3, r3, lsl #1
	asrs	r3, r3, #2
.L1659:
	strh	r3, [r4, #3148]	@ movhi
	movs	r3, #0
	str	r3, [r4, #3160]
.L1547:
	add	sp, sp, #36
	@ sp needed
	pop	{r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L1549:
	ldrh	r3, [r3, #2444]
	cmp	r3, r2
	beq	.L1552
	movs	r0, #1
	bl	FtlGcFreeTempBlock
	cmp	r0, #0
	beq	.L1552
	movs	r0, #1
	b	.L1547
.L1550:
	ldrh	r3, [r4, #3202]
	cmp	r3, r2
	bne	.L1551
	ldrh	r1, [r4, #3206]
	cmp	r1, r3
	beq	.L1551
	ldrh	r2, [r4, #3208]
	cmp	r2, r3
	itttt	ne
	strhne	r0, [r4, #3202]	@ movhi
	strhne	r1, [r4, #3204]	@ movhi
	strhne	r2, [r4, #3206]	@ movhi
	strhne	r3, [r4, #3208]	@ movhi
	b	.L1551
.L1561:
	movs	r3, #64
.L1658:
	strh	r3, [r4, #3150]	@ movhi
	b	.L1560
.L1558:
	movs	r3, #80
	b	.L1658
.L1625:
	mov	r6, r0
.L1557:
	ldr	r3, [r4, #2324]
	mov	r5, r6
	ldrh	r1, [r4, #3148]
	ldrh	r2, [r4, #2344]
	ldrh	r3, [r3, r6, lsl #1]
	str	r1, [sp, #4]
	ldr	r1, [r4, #2328]
	ldr	r0, .L1665+12
	ldrh	r1, [r1, r6, lsl #1]
	str	r1, [sp]
	mov	r1, r6
	bl	sftl_printk
	b	.L1560
.L1565:
	cmp	r3, #12
	bls	.L1566
	lsrs	r6, r6, #4
	b	.L1564
.L1566:
	cmp	r3, #8
	bls	.L1564
	lsrs	r6, r6, #2
	b	.L1564
.L1626:
	movs	r6, #1
	b	.L1564
.L1571:
	movs	r3, #18
	b	.L1659
.L1569:
	ldrh	r3, [r4, #2572]
	add	r3, r3, r3, lsl #1
	asrs	r3, r3, #2
	strh	r3, [r4, #3148]	@ movhi
.L1568:
	ldrh	r3, [r4, #3210]
	cbz	r3, .L1627
	adds	r6, r6, #32
	uxth	r6, r6
.L1627:
	movw	r5, #65535
.L1574:
	ldrh	r3, [r4, #2588]
	movw	r2, #65535
	cmp	r3, r2
	bne	.L1583
	cmp	r5, r3
	beq	.L1584
	strh	r5, [r4, #2588]	@ movhi
.L1585:
	ldrh	r0, [r4, #2588]
	movw	r7, #65535
	movs	r3, #0
	strb	r3, [r4, #2596]
	cmp	r0, r7
	beq	.L1583
	bl	IsBlkInGcList
	cbz	r0, .L1588
	strh	r7, [r4, #2588]	@ movhi
.L1588:
	ldrh	r2, [r4, #2588]
	movw	r3, #65535
	cmp	r2, r3
	beq	.L1583
	ldr	r0, .L1665+16
	bl	make_superblock
	ldr	r2, .L1665+8
	movs	r3, #0
	strh	r3, [r4, #2590]	@ movhi
	strb	r3, [r4, #2594]
	strh	r3, [r2, #-182]	@ movhi
	ldrh	r1, [r4, #2588]
	ldr	r3, [r4, #2324]
	ldrh	r3, [r3, r1, lsl #1]
	strh	r3, [r2, #-180]	@ movhi
.L1583:
	ldrh	r3, [r4, #2588]
	ldrh	r2, [r4, #2348]
	cmp	r2, r3
	beq	.L1589
	ldrh	r2, [r4, #2396]
	cmp	r2, r3
	beq	.L1589
	ldr	r7, .L1665+8
.L1590:
	ldrh	r9, [r4, #2588]
	movw	r3, #65535
	cmp	r9, r3
	bne	.L1591
	movs	r3, #0
	mov	r10, r3
	str	r3, [r4, #3160]
.L1592:
	ldrh	r8, [r4, #3156]
	mov	r0, r8
	bl	List_get_gc_head_node
	uxth	fp, r0
	cmp	fp, r9
	strh	fp, [r4, #2588]	@ movhi
	bne	.L1593
	movs	r3, #0
	movs	r0, #8
	strh	r3, [r4, #3156]	@ movhi
	b	.L1547
.L1575:
	movs	r3, #0
	ldrh	r7, [r4, #2344]
	str	r3, [r4, #3160]
	ldrh	r3, [r4, #3148]
	ldr	r5, .L1665+8
	cmp	r3, r7
	bcs	.L1576
	ldrh	r3, [r5, #-184]
	cbnz	r3, .L1577
	ldr	r3, [r4, #2504]
	ldr	r2, [r4, #2500]
	add	r3, r3, r3, lsl #1
	cmp	r2, r3, lsr #2
	bcs	.L1578
.L1577:
	ldrh	r3, [r4, #2572]
	add	r3, r3, r3, lsl #1
	asrs	r3, r3, #2
.L1660:
	strh	r3, [r4, #3148]	@ movhi
.L1662:
	bl	FtlReadRefresh
	ldrh	r0, [r5, #-184]
	b	.L1547
.L1578:
	movs	r3, #18
	b	.L1660
.L1576:
	ldrh	r0, [r5, #-184]
	cmp	r0, #0
	bne	.L1580
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
	ble	.L1581
	subs	r6, r6, #1
	cmp	r7, r6
	bge	.L1662
.L1581:
	cmp	r2, #0
	bne	.L1580
	movw	r0, #65535
	bl	decrement_vpc_count
	ldrh	r0, [r4, #2344]
	adds	r0, r0, #1
	b	.L1547
.L1666:
	.align	2
.L1665:
	.word	.LANCHOR0
	.word	.LANCHOR2
	.word	.LANCHOR3
	.word	.LC112
	.word	.LANCHOR0+2588
.L1584:
	ldrh	r3, [r4, #3202]
	cmp	r3, r5
	beq	.L1585
	ldr	r2, [r4, #2324]
	ldrh	r3, [r2, r3, lsl #1]
	cbnz	r3, .L1586
	strh	r5, [r4, #3202]	@ movhi
.L1586:
	ldrh	r3, [r4, #3202]
	strh	r3, [r4, #2588]	@ movhi
	movw	r3, #65535
	strh	r3, [r4, #3202]	@ movhi
	b	.L1585
.L1589:
	movw	r3, #65535
	strh	r3, [r4, #2588]	@ movhi
.L1663:
	ldr	r3, .L1667
	ldrh	r0, [r3, #-184]
	b	.L1547
.L1593:
	str	r0, [sp, #8]
	mov	r0, fp
	bl	IsBlkInGcList
	add	r8, r8, #1
	ldr	r3, [sp, #8]
	cbz	r0, .L1594
	strh	r8, [r4, #3156]	@ movhi
	b	.L1592
.L1594:
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
	bgt	.L1596
	cmp	r8, #48
	bls	.L1597
	cmp	r1, #8
	bls	.L1597
	ldrh	r1, [r4, #3192]
	cmp	r1, #35
	bhi	.L1597
.L1596:
	strh	r10, [r4, #3156]	@ movhi
.L1597:
	ldrh	r2, [r2, r0, lsl #1]
	cmp	r3, r2
	bgt	.L1598
	cmp	r5, r9
	bne	.L1598
	movs	r3, #0
	strh	r5, [r4, #2588]	@ movhi
	strh	r3, [r4, #3156]	@ movhi
	b	.L1663
.L1598:
	cbnz	r2, .L1599
	movw	r0, #65535
	bl	decrement_vpc_count
	ldrh	r3, [r4, #3156]
	adds	r3, r3, #1
	strh	r3, [r4, #3156]	@ movhi
	b	.L1592
.L1599:
	movs	r3, #0
	strb	r3, [r4, #2596]
	ldrh	r3, [r4, #2348]
	cmp	r3, fp
	bne	.L1600
	movw	r2, #665
	ldr	r1, .L1667+4
	ldr	r0, .L1667+8
	bl	sftl_printk
.L1600:
	ldrh	r2, [r4, #2588]
	ldrh	r3, [r4, #2396]
	cmp	r2, r3
	bne	.L1601
	movw	r2, #666
	ldr	r1, .L1667+4
	ldr	r0, .L1667+8
	bl	sftl_printk
.L1601:
	ldrh	r2, [r4, #2588]
	ldrh	r3, [r4, #2444]
	cmp	r2, r3
	bne	.L1602
	movw	r2, #667
	ldr	r1, .L1667+4
	ldr	r0, .L1667+8
	bl	sftl_printk
.L1602:
	ldr	r0, .L1667+12
	bl	make_superblock
	ldrh	r1, [r4, #2588]
	movs	r3, #0
	ldr	r2, [r4, #2324]
	strh	r3, [r7, #-182]	@ movhi
	ldrh	r2, [r2, r1, lsl #1]
	strh	r3, [r4, #2590]	@ movhi
	strb	r3, [r4, #2594]
	strh	r2, [r7, #-180]	@ movhi
.L1591:
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
.L1661:
	str	r3, [sp, #8]
	ldrh	r3, [sp, #8]
	cmp	r6, r3
	bls	.L1612
	ldr	r3, [sp, #8]
	movw	r10, #65535
	ldrh	r1, [r4, #2590]
	mov	fp, #20
	ldrh	lr, [r4, #32]
	ldr	r0, [r4, #3180]
	add	r1, r1, r3
	movs	r3, #0
	ldr	ip, .L1667+20
	mov	r8, r3
	b	.L1613
.L1606:
	ldrh	r2, [ip, #2]!
	cmp	r2, r10
	beq	.L1605
	mla	r9, fp, r8, r0
	add	r8, r8, #1
	orr	r2, r1, r2, lsl #10
	uxth	r8, r8
	str	r2, [r9, #4]
.L1605:
	adds	r3, r3, #1
.L1613:
	uxth	r2, r3
	cmp	lr, r2
	bhi	.L1606
	mov	fp, #0
	ldrb	r2, [r4, #2596]	@ zero_extendqisi2
	mov	r1, r8
	bl	FlashReadPages
.L1607:
	uxth	r3, fp
	cmp	r8, r3
	bhi	.L1611
	ldr	r3, [sp, #8]
	adds	r3, r3, #1
	b	.L1661
.L1611:
	mov	r9, #20
	ldr	r3, [r4, #3180]
	mul	r9, r9, fp
	add	r2, r3, r9
	ldr	r3, [r3, r9]
	adds	r3, r3, #1
	beq	.L1608
	ldr	r10, [r2, #12]
	movw	r3, #61589
	ldrh	r2, [r10]
	cmp	r2, r3
	bne	.L1608
	ldr	r3, [r10, #8]
	adds	r2, r3, #1
	bne	.L1609
	movw	r2, #703
	ldr	r1, .L1667+4
	ldr	r0, .L1667+8
	str	r3, [sp, #16]
	bl	sftl_printk
	ldr	r3, [sp, #16]
.L1609:
	movs	r2, #0
	mov	r0, r3
	add	r1, sp, #28
	bl	log2phys
	ldr	r0, [r4, #3180]
	ldr	r3, [sp, #28]
	add	r0, r0, r9
	ldr	r2, [r0, #4]
	cmp	r2, r3
	bne	.L1608
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
	beq	.L1610
	ldrh	r3, [r4, #2448]
	cbnz	r3, .L1608
.L1610:
	bl	Ftl_gc_temp_data_write_back
	cbz	r0, .L1608
.L1664:
	movs	r3, #0
	ldrh	r0, [r7, #-184]
	str	r3, [r4, #3824]
	b	.L1547
.L1608:
	add	fp, fp, #1
	b	.L1607
.L1612:
	ldrh	r3, [r4, #2590]
	add	r6, r6, r3
	ldr	r3, [sp, #12]
	uxth	r6, r6
	cmp	r3, r6
	strh	r6, [r4, #2590]	@ movhi
	bhi	.L1614
	ldr	r3, [r4, #3164]
	cbz	r3, .L1615
	bl	Ftl_gc_temp_data_write_back
	cmp	r0, #0
	bne	.L1664
.L1615:
	ldrh	r6, [r7, #-182]
	cbnz	r6, .L1616
	ldrh	r1, [r4, #2588]
	ldr	r3, [r4, #2324]
	ldrh	r3, [r3, r1, lsl #1]
	cbz	r3, .L1616
	ldrh	r0, [r7, #-180]
	ldrh	r2, [r4, #2590]
	str	r3, [sp]
	mov	r3, r6
	str	r0, [sp, #4]
	ldr	r0, .L1667+16
	bl	sftl_printk
	ldrh	r2, [r4, #2588]
	ldr	r3, [r4, #2324]
	strh	r6, [r3, r2, lsl #1]	@ movhi
	ldrh	r0, [r4, #2588]
	bl	update_vpc_list
	bl	l2p_flush
	bl	FtlVpcTblFlush
.L1616:
	movw	r3, #65535
	strh	r3, [r4, #2588]	@ movhi
.L1614:
	ldrh	r3, [r4, #2344]
	cmp	r3, #2
	bhi	.L1617
	ldrh	r6, [r4, #102]
	b	.L1590
.L1617:
	movs	r2, #0
	str	r2, [r4, #3824]
	ldr	r2, .L1667
	ldrh	r0, [r2, #-184]
	cmp	r0, #0
	bne	.L1547
	adds	r0, r3, #1
	b	.L1547
.L1621:
	movs	r0, #0
	b	.L1547
.L1668:
	.align	2
.L1667:
	.word	.LANCHOR3
	.word	.LANCHOR1+551
	.word	.LC1
	.word	.LANCHOR0+2588
	.word	.LC113
	.word	.LANCHOR0+2602
.L1553:
	cmp	r7, #0
	beq	.L1619
.L1563:
	ldrh	r2, [r4, #2444]
	movw	r3, #65535
	cmp	r2, r3
	bne	.L1630
.L1620:
	ldrh	r3, [r4, #3202]
	movw	r2, #65535
	cmp	r3, r2
	bne	.L1630
	cmp	r5, r3
	bne	.L1630
	ldrh	r3, [r4, #2588]
	cmp	r3, r5
	beq	.L1575
.L1580:
	movw	r5, #65535
.L1630:
	movs	r6, #1
	b	.L1574
.L1554:
	cmp	r7, #0
	bne	.L1580
	b	.L1619
.L1555:
	cmp	r7, #0
	beq	.L1619
	movw	r5, #65535
	b	.L1620
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
	bne	.L1671
	mov	r2, r3
	ldr	r1, [sp, #56]
	add	r0, r7, #256
	bl	FtlVendorPartRead
	str	r0, [sp, #28]
.L1670:
	ldr	r0, [sp, #28]
	add	sp, sp, #84
	@ sp needed
	pop	{r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L1671:
	ldr	r3, [sp, #56]
	ldr	r4, .L1703
	adds	r3, r1, r3
	str	r3, [sp, #36]
	ldr	r2, [sp, #36]
	ldr	r3, [r4, #140]
	cmp	r2, r3
	bhi	.L1692
	ldr	r3, .L1703+4
	ldr	r3, [r3]
	adds	r0, r3, #1
	beq	.L1693
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
.L1673:
	ldr	r3, [sp, #32]
	cbnz	r3, .L1690
	ldr	r3, .L1703
	ldrh	r2, [r3, #3210]
	cbnz	r2, .L1691
	ldrh	r3, [r3, #2344]
	cmp	r3, #31
	bhi	.L1670
.L1691:
	movs	r1, #1
	movs	r0, #0
	bl	rk_ftl_garbage_collect
	b	.L1670
.L1690:
	add	r1, sp, #76
	movs	r2, #0
	mov	r0, r5
	bl	log2phys
	ldr	r3, [sp, #76]
	adds	r1, r3, #1
	bne	.L1674
	mov	r10, #0
.L1675:
	ldrh	r0, [r4, #58]
	cmp	r10, r0
	bcc	.L1677
.L1678:
	ldr	r3, [sp, #32]
	adds	r5, r5, #1
	subs	r3, r3, #1
	str	r3, [sp, #32]
	beq	.L1682
	ldrh	r3, [r4, #32]
	cmp	r6, r3, lsl #2
	bne	.L1673
.L1682:
	cmp	r6, #0
	beq	.L1673
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
.L1689:
	movs	r3, #20
	ldr	r1, [sp, #40]
	mul	r10, r3, fp
	ldr	r3, [r4, #3836]
	add	r3, r3, r10
	ldr	r2, [r3, #16]
	cmp	r1, r2
	bne	.L1684
	ldr	r1, [r3, #8]
	ldr	r3, [r4, #3820]
	cmp	r1, r3
	bne	.L1685
	ldr	r3, [sp, #60]
	mov	r0, r9
	ldr	r2, [sp, #64]
	add	r1, r1, r3
.L1702:
	bl	ftl_memcpy
.L1685:
	ldr	r1, [r4, #3836]
	add	r1, r1, r10
	ldr	r3, [r1, #12]
	ldr	r2, [r1, #16]
	ldr	r3, [r3, #8]
	cmp	r2, r3
	beq	.L1686
	ldr	r3, [r4, #2708]
	adds	r3, r3, #1
	str	r3, [r4, #2708]
	ldr	r2, [r1, #8]
	ldr	r3, [r1, #12]
	ldr	r0, [r2, #4]
	str	r0, [sp, #16]
	ldr	r2, [r2]
	ldr	r0, .L1703+8
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
.L1686:
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
	bne	.L1688
	ldr	r0, [r0, #4]
	ubfx	r0, r0, #10, #16
	bl	P2V_block_in_plane
	bl	FtlGcRefreshBlock
.L1688:
	add	fp, fp, #1
	cmp	r6, fp
	bne	.L1689
	movs	r6, #0
	b	.L1673
.L1677:
	mla	r0, r0, r5, r10
	cmp	r7, r0
	bhi	.L1676
	ldr	r3, [sp, #36]
	cmp	r3, r0
	bls	.L1676
	subs	r0, r0, r7
	mov	r1, #512
	add	r0, r9, r0, lsl #9
	bl	__memzero
.L1676:
	add	r10, r10, #1
	b	.L1675
.L1674:
	ldr	r2, [r4, #3836]
	mov	fp, #20
	ldrh	r10, [r4, #58]
	mla	fp, fp, r6, r2
	str	r3, [fp, #4]
	ldr	r3, [sp, #40]
	cmp	r5, r3
	bne	.L1679
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
	bne	.L1680
	str	r9, [fp, #8]
.L1680:
	ldrh	r3, [r4, #112]
	ldr	r2, [r4, #3816]
	str	r5, [fp, #16]
	muls	r3, r6, r3
	adds	r6, r6, #1
	bic	r3, r3, #3
	add	r3, r3, r2
	str	r3, [fp, #12]
	b	.L1678
.L1679:
	ldr	r3, [sp, #44]
	cmp	r5, r3
	bne	.L1681
	ldr	r3, [r4, #3812]
	ldr	r2, [sp, #36]
	str	r3, [fp, #8]
	mul	r3, r10, r5
	sub	r8, r2, r3
	cmp	r10, r8
	bne	.L1680
.L1701:
	subs	r3, r3, r7
	add	r3, r9, r3, lsl #9
	str	r3, [fp, #8]
	b	.L1680
.L1681:
	mul	r3, r5, r10
	b	.L1701
.L1684:
	ldr	r1, [sp, #44]
	cmp	r1, r2
	bne	.L1685
	ldr	r1, [r3, #8]
	ldr	r3, [r4, #3812]
	cmp	r1, r3
	bne	.L1685
	ldrh	r0, [r4, #58]
	ldr	r3, [sp, #44]
	ldr	r2, [sp, #68]
	muls	r0, r3, r0
	subs	r0, r0, r7
	add	r0, r9, r0, lsl #9
	b	.L1702
.L1692:
	mov	r3, #-1
.L1693:
	str	r3, [sp, #28]
	b	.L1670
.L1704:
	.align	2
.L1703:
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
	bne	.L1707
	mov	r2, r3
	ldr	r1, [sp, #12]
	add	r0, r10, #256
	bl	FtlVendorPartWrite
.L1706:
	add	sp, sp, #68
	@ sp needed
	pop	{r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L1707:
	ldr	r3, [sp, #12]
	ldr	r5, .L1757
	adds	r6, r1, r3
	ldr	r3, [r5, #140]
	cmp	r6, r3
	bhi	.L1743
	ldr	r4, .L1757+4
	ldr	r0, [r4]
	adds	r7, r0, #1
	beq	.L1706
	ldr	r3, .L1757+8
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
	cbz	r3, .L1745
	ldrh	r2, [r5, #2352]
	addw	r3, r5, #2348
	addw	r5, r5, #2396
	cmp	r2, #0
	it	ne
	movne	r5, r3
.L1709:
	ldr	r7, [sp, #4]
	ldr	r4, .L1757
.L1710:
	ldr	r3, [sp]
	cbnz	r3, .L1739
	mov	r0, r3
	ldr	r1, [sp, #24]
	bl	rk_ftl_garbage_collect
	ldrh	r3, [r4, #2344]
	cmp	r3, #15
	bls	.L1740
.L1742:
	movs	r0, #0
	b	.L1706
.L1745:
	addw	r5, r5, #2348
	b	.L1709
.L1739:
	ldrb	r2, [r5, #6]	@ zero_extendqisi2
	ldrh	r3, [r4, #32]
	cmp	r2, r3
	bcc	.L1711
	movw	r2, #991
	ldr	r1, .L1757+12
	ldr	r0, .L1757+16
	bl	sftl_printk
.L1711:
	ldrh	r2, [r5, #4]
	cbnz	r2, .L1712
	ldr	r3, .L1757+20
	ldr	r6, .L1757+4
	cmp	r5, r3
	bne	.L1713
	ldrh	r8, [r4, #2400]
	cmp	r8, #0
	bne	.L1714
	add	r0, r5, #48
	bl	allocate_new_data_superblock
	str	r8, [r6, #8]
.L1714:
	ldr	r5, .L1757+20
	ldr	r0, .L1757+20
	bl	allocate_new_data_superblock
	ldr	r2, [r6, #8]
	add	r3, r5, #48
	cmp	r2, #0
	it	ne
	movne	r5, r3
.L1715:
	ldrh	r3, [r5, #4]
	cbnz	r3, .L1712
	mov	r0, r5
	bl	allocate_new_data_superblock
.L1712:
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
	bcc	.L1716
	mov	r2, #1024
	ldr	r1, .L1757+12
	ldr	r0, .L1757+16
	bl	sftl_printk
.L1716:
	mov	fp, #0
.L1717:
	ldr	r3, [sp, #36]
	cmp	fp, r3
	bne	.L1737
.L1718:
	mov	r3, r5
	movs	r2, #0
	mov	r1, fp
	ldr	r0, [r4, #3844]
	bl	FtlProgPages
	ldr	r3, [sp]
	cmp	fp, r3
	bls	.L1738
	movw	r2, #1101
	ldr	r1, .L1757+12
	ldr	r0, .L1757+16
	bl	sftl_printk
.L1738:
	ldr	r3, [sp]
	sub	r3, r3, fp
	str	r3, [sp]
	b	.L1710
.L1713:
	str	r2, [r6, #8]
	ldrh	r2, [r4, #2352]
	cbnz	r2, .L1748
	mov	r0, r5
	bl	allocate_new_data_superblock
	b	.L1715
.L1748:
	mov	r5, r3
	b	.L1712
.L1737:
	ldrh	r3, [r5, #4]
	cmp	r3, #0
	beq	.L1718
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
	beq	.L1719
	ldr	r3, [sp, #20]
	cmp	r7, r3
	bne	.L1752
	ldrh	r2, [r4, #58]
	ldr	r3, [sp, #12]
	smulbb	r2, r2, r7
	add	r6, r10, r3
	movs	r3, #0
	str	r3, [sp, #16]
	subs	r6, r6, r2
	uxth	r6, r6
	b	.L1722
.L1719:
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
.L1722:
	ldrh	r3, [r4, #58]
	cmp	r6, r3
	ldr	r3, [sp, #4]
	bne	.L1723
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
.L1725:
	ldrb	r2, [r5, #6]	@ zero_extendqisi2
	ldrh	r3, [r4, #32]
	cmp	r2, r3
	bcc	.L1734
	movw	r2, #1092
	ldr	r1, .L1757+12
	ldr	r0, .L1757+16
	bl	sftl_printk
.L1734:
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
	b	.L1717
.L1723:
	cmp	r7, r3
	ldr	r3, [r4, #3844]
	ite	eq
	ldreq	r2, [r4, #3820]
	ldrne	r2, [r4, #3812]
	add	r3, r3, r8
	str	r2, [r3, #8]
	ldr	r3, [sp, #40]
	adds	r1, r3, #1
	beq	.L1728
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
	bne	.L1729
	ldr	r3, [r4, #2708]
	adds	r3, r3, #1
	str	r3, [r4, #2708]
.L1732:
	ldr	r3, [sp, #4]
	lsls	r2, r6, #9
	cmp	r7, r3
	bne	.L1733
	ldr	r3, [r4, #3844]
	ldr	r1, [sp, #8]
	add	r3, r3, r8
	ldr	r0, [r3, #8]
	ldr	r3, [sp, #16]
	add	r0, r0, r3, lsl #9
.L1755:
	bl	ftl_memcpy
	b	.L1725
.L1758:
	.align	2
.L1757:
	.word	.LANCHOR0
	.word	.LANCHOR2
	.word	.LANCHOR3
	.word	.LANCHOR1+574
	.word	.LC1
	.word	.LANCHOR0+2348
.L1729:
	ldr	r3, [r9, #8]
	cmp	r7, r3
	beq	.L1731
	ldr	r3, [r4, #2708]
	mov	r2, r7
	ldr	r0, .L1759
	adds	r3, r3, #1
	str	r3, [r4, #2708]
	ldr	r1, [r9, #8]
	bl	sftl_printk
.L1731:
	ldr	r3, [r9, #8]
	cmp	r7, r3
	beq	.L1732
	movw	r2, #1077
	ldr	r1, .L1759+4
	ldr	r0, .L1759+8
	bl	sftl_printk
	b	.L1732
.L1728:
	ldr	r3, [r4, #3844]
	movs	r1, #0
	ldrh	r2, [r4, #110]
	add	r3, r3, r8
	ldr	r0, [r3, #8]
	bl	ftl_memset
	b	.L1732
.L1733:
	ldrh	r1, [r4, #58]
	ldr	r0, [sp, #8]
	ldr	r3, [r4, #3844]
	muls	r1, r7, r1
	add	r3, r3, r8
	sub	r1, r1, r10
	add	r1, r0, r1, lsl #9
	ldr	r0, [r3, #8]
	b	.L1755
.L1752:
	ldr	r3, [r4, #3844]
	ldr	r2, [sp, #8]
	add	r8, r8, r3
	ldrh	r3, [r4, #58]
	muls	r3, r7, r3
	sub	r3, r3, r10
	add	r3, r2, r3, lsl #9
	str	r3, [r8, #8]
	b	.L1725
.L1740:
	movw	r5, #65535
.L1753:
	ldrh	r3, [r4, #2588]
	cmp	r3, r5
	bne	.L1741
	ldrh	r3, [r4, #3202]
	cmp	r3, r5
	bne	.L1741
	movs	r0, #0
	bl	List_get_gc_head_node
	uxth	r0, r0
	bl	FtlGcRefreshBlock
.L1741:
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
	bls	.L1753
	b	.L1742
.L1743:
	mov	r0, #-1
	b	.L1706
.L1760:
	.align	2
.L1759:
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
	ldr	r6, .L1793
	bls	.L1762
	cmp	r0, #576
	bls	.L1763
.L1762:
	ldr	r3, [r6, #-172]
	cmp	r3, #0
	beq	.L1764
	ldr	r2, .L1793+4
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
	ldr	r2, .L1793+8
	movs	r3, #0
	ldr	r1, .L1793+12
	str	r0, [sp, #12]
	sub	r4, r2, #262144
.L1768:
	ldr	r0, [r2, #-4]
	cmp	r0, #0
	beq	.L1765
.L1769:
	ldr	r3, [sp, #12]
	movs	r4, #0
	ldr	r2, [sp, #24]
	ldr	r5, .L1793+4
	str	r4, [sp, #28]
	muls	r3, r2, r3
	str	r3, [sp, #48]
	ldr	r3, [sp, #12]
	str	r3, [sp, #32]
.L1766:
	mov	r1, #512
	ldr	r0, .L1793+16
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
	bcs	.L1770
	mov	r1, r7
	adds	r0, r6, r4
	bl	__aeabi_uidiv
	ldr	r3, [r5, #3252]
	mov	r1, r0
	movs	r0, #0
	blx	r3
.L1770:
	mov	r1, r6
	mov	r0, r4
	bl	__aeabi_uidivmod
	movs	r6, #0
	mov	r9, r1
	subs	r3, r4, r1
	str	r3, [sp, #44]
.L1771:
	cmp	r6, #512
	bcc	.L1772
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
.L1773:
	cmp	r6, #512
	bcs	.L1777
	ldr	r3, [sp, #40]
	mov	r1, r8
	add	r0, r10, r6
	sub	r9, r3, r7
	ldr	r3, .L1793+16
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
	bne	.L1774
.L1777:
	ldr	r1, .L1793+20
	movs	r6, #0
.L1775:
	ldr	r3, .L1793+16
	mov	r7, r1
	adds	r1, r1, #4
	ldr	r2, [r3, r6, lsl #2]
	ldr	r3, [r7]
	cmp	r2, r3
	beq	.L1778
	mov	r1, #512
	ldr	r0, .L1793+16
	bl	__memzero
	ldr	r2, .L1793+16
	mov	r1, r4
	str	r6, [sp]
	ldr	r3, [r7]
	ldr	r2, [r2, r6, lsl #2]
	ldr	r0, .L1793+24
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
	bls	.L1779
	ldr	r3, [sp, #24]
	ldr	r1, [sp, #20]
	adds	r0, r3, r4
	bl	__aeabi_uidiv
	ldr	r3, [r5, #3252]
	mov	r1, r0
	movs	r0, #0
	blx	r3
.L1779:
	ldr	r2, [sp, #32]
	ldr	r3, [sp, #12]
	add	r3, r3, r2
	ldr	r2, [sp, #48]
	add	r4, r4, r2
	ldr	r2, [sp, #32]
	cmp	r2, #15
	bls	.L1785
.L1784:
	ldr	r3, .L1793
	movs	r2, #0
	str	r2, [r3, #-172]
.L1764:
	ldr	r3, [sp, #36]
	mov	r1, fp
	ldr	r2, [sp, #16]
	movs	r0, #0
	bl	FtlWrite
	add	sp, sp, #124
	@ sp needed
	pop	{r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L1765:
	ldr	r0, [r4, r3, lsl #2]
	adds	r3, r3, #1
	cmp	r3, #4096
	it	hi
	movhi	r3, #0
	str	r0, [r2, #-4]!
	cmp	r1, r2
	bne	.L1768
	b	.L1769
.L1785:
	str	r3, [sp, #32]
	b	.L1766
.L1772:
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
	ldr	r3, .L1793+20
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
	b	.L1771
.L1774:
	add	r6, r6, r9
	movs	r7, #0
	uxth	r6, r6
	b	.L1773
.L1778:
	adds	r6, r6, #1
	cmp	r6, #65536
	bne	.L1775
	ldr	r3, [sp, #28]
	adds	r3, r3, #1
	cmp	r3, #5
	str	r3, [sp, #28]
	bls	.L1779
	b	.L1784
.L1763:
	cmp	r0, #63
	ldr	r0, .L1793+20
	bhi	.L1781
	ldr	r3, [sp, #16]
	rsb	r1, fp, #64
	subs	r2, r3, r1
	ldr	r3, [sp, #36]
	add	r1, r3, r1, lsl #9
.L1782:
	movs	r3, #1
	cmp	r5, #576
	str	r3, [r6, #-172]
	ittt	hi
	subhi	r2, r2, r4
	mvnhi	r3, #446
	addhi	r2, r2, r3
	lsls	r2, r2, #9
	bl	memcpy
	b	.L1764
.L1781:
	ldr	r3, .L1793+28
	ldr	r1, [sp, #36]
	ldr	r2, [sp, #16]
	add	r3, r3, fp
	add	r0, r0, r3, lsl #9
	b	.L1782
.L1794:
	.align	2
.L1793:
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
	ldr	r4, .L1816
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
	bne	.L1796
.L1804:
	mov	r0, #-1
	pop	{r3, r4, r5, r6, r7, pc}
.L1796:
	ldr	r7, .L1816+4
	movs	r1, #1
	add	r6, r4, #3264
	bl	FtlGetLastWrittenPage
	sxth	r5, r0
	adds	r0, r0, #1
	strh	r0, [r4, #2578]	@ movhi
.L1798:
	cmp	r5, #0
	bge	.L1801
	movw	r2, #1482
	ldr	r1, .L1816+8
	ldr	r0, .L1816+12
	bl	sftl_printk
.L1800:
	ldrh	r3, [r4, #40]
	ldrh	r2, [r4, #110]
	adds	r3, r3, #24
	cmp	r2, r3, lsl #1
	bcs	.L1803
	movw	r2, #1484
	ldr	r1, .L1816+8
	ldr	r0, .L1816+12
	bl	sftl_printk
.L1803:
	movs	r2, #48
	ldr	r1, [r4, #3272]
	ldr	r0, .L1816+16
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
	ldr	r3, .L1816+4
	cmp	r2, r3
	bne	.L1804
	ldrb	r2, [r4, #2278]	@ zero_extendqisi2
	ldrh	r3, [r4, #54]
	ldrh	r5, [r4, #2276]
	cmp	r2, r3
	strh	r5, [r4, #2582]	@ movhi
	bne	.L1804
	ldr	r3, .L1816+20
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
	bls	.L1805
	movw	r2, #1506
	ldr	r1, .L1816+8
	ldr	r0, .L1816+12
	bl	sftl_printk
.L1805:
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
	beq	.L1808
	ldr	r0, .L1816+24
	bl	make_superblock
.L1808:
	ldrh	r2, [r4, #2396]
	movw	r3, #65535
	cmp	r2, r3
	beq	.L1809
	ldr	r0, .L1816+28
	bl	make_superblock
.L1809:
	ldrh	r2, [r4, #2444]
	movw	r3, #65535
	cmp	r2, r3
	beq	.L1810
	ldr	r0, .L1816+32
	bl	make_superblock
.L1810:
	ldrh	r2, [r4, #2588]
	movw	r3, #65535
	cmp	r2, r3
	beq	.L1811
	ldr	r0, .L1816+36
	bl	make_superblock
.L1811:
	movs	r0, #0
	pop	{r3, r4, r5, r6, r7, pc}
.L1801:
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
	beq	.L1799
	ldr	r3, [r4, #3284]
	ldr	r3, [r3]
	cmp	r3, r7
	bne	.L1799
	ldr	r3, [r4, #3288]
	ldrh	r2, [r3]
	movw	r3, #61604
	cmp	r2, r3
	beq	.L1800
.L1799:
	subs	r5, r5, #1
	sxth	r5, r5
	b	.L1798
.L1817:
	.align	2
.L1816:
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
	ldr	r4, .L1842
	ldrh	r0, [r4, #36]
	strh	r3, [r4, #4016]	@ movhi
	bl	FtlFreeSysBlkQueueInit
	bl	FtlScanSysBlk
	ldrh	r2, [r4, #2576]
	movw	r3, #65535
	cmp	r2, r3
	bne	.L1819
.L1821:
	mov	r6, #-1
.L1818:
	mov	r0, r6
	pop	{r4, r5, r6, pc}
.L1819:
	bl	FtlLoadSysInfo
	mov	r6, r0
	cmp	r0, #0
	bne	.L1821
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
.L1822:
	cmp	r3, r1
	bge	.L1827
	mla	r5, r0, r3, r2
	ldr	r5, [r5, #4]
	cmp	r5, #0
	bge	.L1823
.L1827:
	ldrh	r2, [r4, #2296]
	cmp	r3, r1
	add	r2, r2, #1
	strh	r2, [r4, #2296]	@ movhi
	bge	.L1841
.L1824:
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
	ldr	r0, .L1842+4
	strb	r3, [r4, #2402]
	strh	r2, [r4, #2398]	@ movhi
	strh	r3, [r4, #2400]	@ movhi
	bl	FtlMapBlkWriteDump_data
	ldr	r0, .L1842+8
	bl	FtlMapBlkWriteDump_data
	ldrh	r3, [r4, #2298]
	adds	r3, r3, #1
	strh	r3, [r4, #2298]	@ movhi
	bl	l2p_flush
	bl	FtlVpcTblFlush
	bl	FtlVpcTblFlush
	b	.L1828
.L1823:
	adds	r3, r3, #1
	b	.L1822
.L1841:
	ldrh	r3, [r4, #4016]
	cmp	r3, #0
	bne	.L1824
.L1828:
	ldrh	r5, [r4, #2312]
	movw	r2, #65533
	subs	r3, r5, #1
	uxth	r3, r3
	cmp	r3, r2
	bhi	.L1830
	mov	r1, r5
	ldr	r0, .L1842+12
	strh	r5, [r4, #3202]	@ movhi
	bl	test_node_in_list
	cbnz	r0, .L1831
	ldrh	r3, [r4, #40]
	cmp	r3, r5
	bls	.L1831
	ldr	r3, [r4, #2324]
	ldrh	r3, [r3, r5, lsl #1]
	cbnz	r3, .L1830
.L1831:
	movw	r3, #65535
	strh	r3, [r4, #3202]	@ movhi
.L1830:
	ldrh	r5, [r4, #2314]
	movw	r2, #65533
	subs	r3, r5, #1
	uxth	r3, r3
	cmp	r3, r2
	bhi	.L1834
	mov	r1, r5
	ldr	r0, .L1842+12
	strh	r5, [r4, #3204]	@ movhi
	bl	test_node_in_list
	cbnz	r0, .L1835
	ldrh	r3, [r4, #40]
	cmp	r3, r5
	bls	.L1835
	ldr	r3, [r4, #2324]
	ldrh	r3, [r3, r5, lsl #1]
	cbnz	r3, .L1834
.L1835:
	movw	r3, #65535
	strh	r3, [r4, #3204]	@ movhi
.L1834:
	ldrh	r0, [r4, #2348]
	movw	r3, #65535
	cmp	r0, r3
	beq	.L1837
	ldrh	r3, [r4, #2352]
	cbnz	r3, .L1837
	ldrh	r3, [r4, #2400]
	cbnz	r3, .L1837
	bl	FtlGcRefreshBlock
	ldrh	r0, [r4, #2396]
	bl	FtlGcRefreshBlock
	bl	FtlVpcTblFlush
	ldr	r0, .L1842+16
	bl	allocate_new_data_superblock
	ldr	r0, .L1842+20
	bl	allocate_new_data_superblock
.L1837:
	ldrh	r3, [r4, #2296]
	lsls	r3, r3, #27
	bne	.L1818
	bl	FtlVpcCheckAndModify
	b	.L1818
.L1843:
	.align	2
.L1842:
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
	ldr	r4, .L1868
	ldrh	r0, [r4, #36]
	str	r3, [r4, #2540]
	str	r3, [r4, #2544]
	bl	FtlFreeSysBlkQueueInit
	bl	FtlLoadBbt
	cbz	r0, .L1845
	bl	FtlMakeBbt
.L1845:
	ldr	r0, .L1868+4
	movs	r2, #0
.L1846:
	ldrh	r1, [r4, #58]
	uxth	r3, r2
	adds	r2, r2, #1
	cmp	r3, r1, lsl #7
	blt	.L1847
	ldrh	r6, [r4, #40]
	movs	r5, #0
.L1848:
	ldrh	r3, [r4, #42]
	cmp	r3, r6
	bhi	.L1849
	ldrh	r1, [r4, #32]
	subs	r3, r5, #3
	cmp	r3, r1, lsl #1
	bge	.L1850
.L1854:
	movs	r5, #0
	mov	r6, r5
.L1851:
	ldrh	r3, [r4, #40]
	uxth	r0, r5
	adds	r5, r5, #1
	cmp	r3, r0
	bhi	.L1855
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
	ble	.L1856
	mov	r1, r5
	sub	r0, r9, r6
	bl	__aeabi_uidiv
	str	r0, [r4, #2504]
	lsrs	r0, r0, #5
	adds	r0, r0, #24
	strh	r0, [r4, #2572]	@ movhi
.L1856:
	ldrh	r3, [r4, #94]
	cbz	r3, .L1858
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
.L1858:
	ldrh	r2, [r4, #2572]
	movw	r6, #65535
	ldr	r3, [r4, #2504]
	ldr	r7, .L1868+8
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
	ldr	r7, .L1868+12
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
.L1860:
	mov	r0, r7
	bl	make_superblock
	ldrb	r3, [r4, #2355]	@ zero_extendqisi2
	ldrh	r2, [r4, #2348]
	cmp	r3, #0
	bne	.L1861
	ldr	r3, [r4, #2324]
	strh	r6, [r3, r2, lsl #1]	@ movhi
	ldrh	r3, [r4, #2348]
	adds	r3, r3, #1
	strh	r3, [r4, #2348]	@ movhi
	b	.L1860
.L1847:
	ldr	r5, [r4, #3820]
	mvns	r1, r3
	orr	r1, r3, r1, lsl #16
	str	r1, [r5, r3, lsl #2]
	ldr	r1, [r4, #3812]
	str	r0, [r1, r3, lsl #2]
	b	.L1846
.L1849:
	mov	r0, r6
	movs	r1, #1
	bl	FtlLowFormatEraseBlock
	adds	r6, r6, #1
	add	r5, r5, r0
	uxth	r5, r5
	uxth	r6, r6
	b	.L1848
.L1850:
	mov	r0, r5
	bl	__aeabi_uidiv
	ldr	r3, [r4, #132]
	add	r0, r0, r3
	uxth	r0, r0
	bl	FtlSysBlkNumInit
	ldrh	r0, [r4, #36]
	bl	FtlFreeSysBlkQueueInit
	ldrh	r5, [r4, #40]
.L1852:
	ldrh	r3, [r4, #42]
	cmp	r3, r5
	bls	.L1854
	mov	r0, r5
	movs	r1, #1
	adds	r5, r5, #1
	bl	FtlLowFormatEraseBlock
	uxth	r5, r5
	b	.L1852
.L1855:
	movs	r1, #0
	bl	FtlLowFormatEraseBlock
	add	r6, r6, r0
	uxth	r6, r6
	b	.L1851
.L1861:
	ldr	r3, [r4, #2540]
	movw	r6, #65535
	ldrh	r1, [r4, #2352]
	ldr	r7, .L1868+16
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
.L1862:
	mov	r0, r7
	bl	make_superblock
	ldrb	r3, [r4, #2403]	@ zero_extendqisi2
	ldrh	r2, [r4, #2396]
	cbnz	r3, .L1863
	ldr	r3, [r4, #2324]
	strh	r6, [r3, r2, lsl #1]	@ movhi
	ldrh	r3, [r4, #2396]
	adds	r3, r3, #1
	strh	r3, [r4, #2396]	@ movhi
	b	.L1862
.L1863:
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
	cbnz	r0, .L1864
	ldr	r3, .L1868+20
	movs	r2, #1
	str	r2, [r3]
.L1864:
	movs	r0, #0
	pop	{r3, r4, r5, r6, r7, r8, r9, pc}
.L1869:
	.align	2
.L1868:
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
	ldr	r4, .L1876
	ldr	r5, .L1876+4
	ldr	r1, .L1876+8
	ldr	r0, .L1876+12
	str	r3, [r5]
	bl	sftl_printk
	mov	r0, r4
	bl	FtlConstantsInit
	bl	FtlMemInit
	bl	FtlVariablesInit
	ldrh	r0, [r4, #36]
	bl	FtlFreeSysBlkQueueInit
	bl	FtlLoadBbt
	cbnz	r0, .L1874
	bl	FtlSysBlkInit
	cbnz	r0, .L1874
	movs	r3, #1
	str	r3, [r5]
	ldrh	r3, [r4, #2344]
	cmp	r3, #15
	bhi	.L1874
	movw	r4, #8129
.L1873:
	movs	r1, #1
	movs	r0, #0
	bl	rk_ftl_garbage_collect
	subs	r4, r4, #1
	bne	.L1873
.L1874:
	movs	r0, #0
	pop	{r3, r4, r5, pc}
.L1877:
	.align	2
.L1876:
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
	ldr	r2, .L1882
	ldr	r3, [r2, #-164]
	cbnz	r3, .L1881
	str	r0, [r2, #-164]
	mov	r0, r3
	str	r1, [r2, #-160]
	bx	lr
.L1881:
	mov	r0, #-1
	bx	lr
.L1883:
	.align	2
.L1882:
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
	ldr	r4, .L1892
	bl	ftl_malloc
	str	r0, [r4, #-156]
	cmp	r0, #0
	beq	.L1890
	ldr	r8, .L1892+4
	mov	r9, #0
	mov	r7, r9
	mov	r6, r9
.L1888:
	ldr	r3, [r4, #-164]
	movs	r1, #128
	ldr	r2, [r4, #-156]
	lsls	r0, r6, #7
	blx	r3
	mov	r5, r0
	cbnz	r0, .L1886
	ldr	r2, [r4, #-156]
	ldr	r3, [r2]
	cmp	r3, r8
	bne	.L1887
	movw	r3, #65532
	ldr	r1, [r2, r3]
	ldr	r3, [r2, #4]
	cmp	r1, r3
	bne	.L1887
	cmp	r1, r7
	itt	hi
	movhi	r9, r6
	movhi	r7, r1
.L1887:
	adds	r6, r6, #1
	cmp	r6, #2
	bne	.L1888
	cbz	r7, .L1889
	ldr	r3, [r4, #-164]
	movs	r1, #128
	lsl	r0, r9, #7
	blx	r3
	mov	r5, r0
	cbz	r0, .L1884
.L1886:
	ldr	r0, [r4, #-156]
	mov	r5, #-1
	bl	kfree
	movs	r3, #0
	str	r3, [r4, #-156]
	b	.L1884
.L1889:
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
.L1884:
	mov	r0, r5
	pop	{r3, r4, r5, r6, r7, r8, r9, pc}
.L1890:
	mvn	r5, #11
	b	.L1884
.L1893:
	.align	2
.L1892:
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
	ldr	r3, .L1900
	mov	r0, r1
	ldr	r5, [r3, #-156]
	cbz	r5, .L1899
	ldrh	r6, [r5, #10]
	movs	r3, #0
.L1896:
	cmp	r3, r6
	bcc	.L1898
.L1899:
	mov	r0, #-1
	pop	{r3, r4, r5, r6, r7, pc}
.L1898:
	add	r1, r5, r3, lsl #3
	ldrh	r4, [r1, #16]
	cmp	r4, r7
	bne	.L1897
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
.L1897:
	adds	r3, r3, #1
	b	.L1896
.L1901:
	.align	2
.L1900:
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
	ldr	r2, .L1921
	.pad #28
	sub	sp, sp, #28
	mov	r3, r1
	ldr	r4, [r2, #-156]
	mov	r9, r2
	cmp	r4, #0
	beq	.L1917
	ldrh	r2, [r4, #10]
	add	r6, r8, #63
	ldrh	r1, [r4, #8]
	bic	r6, r6, #63
	movs	r7, #0
	str	r1, [sp, #4]
.L1904:
	cmp	r7, r2
	bcc	.L1912
	ldrh	r1, [r4, #14]
	cmp	r6, r1
	bhi	.L1917
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
	b	.L1920
.L1912:
	add	r5, r4, r7, lsl #3
	ldrh	r1, [r5, #16]
	cmp	r1, r0
	str	r1, [sp, #8]
	bne	.L1905
	ldrh	r1, [r5, #20]
	add	fp, r4, #1024
	adds	r1, r1, #63
	bic	r1, r1, #63
	cmp	r8, r1
	str	r1, [sp, #12]
	bls	.L1906
	ldrh	r1, [r4, #14]
	cmp	r6, r1
	bhi	.L1917
	ldrh	r10, [r5, #18]
	subs	r2, r2, #1
	str	r2, [sp, #16]
.L1907:
	ldr	r2, [sp, #16]
	adds	r5, r5, #8
	cmp	r7, r2
	bcc	.L1908
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
.L1909:
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
	ldr	r3, .L1921
	ldr	r3, [r3, #-160]
.L1920:
	ldr	r0, [sp, #4]
	mov	r2, r4
	movs	r1, #128
	lsls	r0, r0, #7
	blx	r3
	movs	r0, #0
.L1902:
	add	sp, sp, #28
	@ sp needed
	pop	{r4, r5, r6, r7, r8, r9, r10, fp, pc}
.L1908:
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
	b	.L1907
.L1906:
	ldrh	r0, [r5, #18]
	mov	r2, r8
	mov	r1, r3
	add	r0, r0, fp
	bl	memcpy
	strh	r8, [r5, #20]	@ movhi
	b	.L1909
.L1905:
	adds	r7, r7, #1
	b	.L1904
.L1917:
	mov	r0, #-1
	b	.L1902
.L1922:
	.align	2
.L1921:
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
	beq	.L1937
	ldr	r3, .L1949
	cmp	r6, r3
	beq	.L1926
	adds	r3, r3, #1
	cmp	r6, r3
	beq	.L1927
.L1948:
	mvn	r5, #13
	b	.L1925
.L1926:
	ldr	r6, .L1949+4
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
	bne	.L1933
	movs	r2, #8
	mov	r1, r5
	bl	arm_copy_from_user
	cmp	r0, #0
	bne	.L1948
	ldr	r2, [r4]
	ldr	r3, .L1949+8
	cmp	r2, r3
	beq	.L1931
.L1932:
	mov	r5, #-1
.L1925:
	mov	r0, r4
	bl	kfree
.L1923:
	mov	r0, r5
	pop	{r4, r5, r6, pc}
.L1931:
	ldrh	r2, [r4, #6]
	add	r1, r4, #8
	ldrh	r0, [r4, #4]
	bl	rk_sftl_vendor_read
	adds	r3, r0, #1
	beq	.L1932
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
	bne	.L1948
	mov	r1, r4
	mov	r0, r5
	bl	arm_copy_to_user
	cmp	r0, #0
	bne	.L1948
.L1939:
	mov	r5, r0
	b	.L1925
.L1927:
	ldr	r6, .L1949+4
	mov	r3, sp
	ands	r3, r3, r6
	ldr	r3, [r3, #72]
	.syntax unified
@ 552 "./arch/arm/include/asm/uaccess.h" 1
	adds r2, r5, #8; sbcccs r2, r2, r3; movcc r3, #0
@ 0 "" 2
	.thumb
	.syntax unified
	cbnz	r3, .L1933
	movs	r2, #8
	mov	r1, r5
	bl	arm_copy_from_user
	cmp	r0, #0
	bne	.L1948
	ldr	r2, [r4]
	ldr	r3, .L1949+8
	cmp	r2, r3
	bne	.L1932
	ldrh	r2, [r4, #6]
	movw	r3, #4087
	cmp	r2, r3
	bhi	.L1932
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
	cbnz	r3, .L1935
	mov	r1, r5
	mov	r0, r4
	bl	arm_copy_from_user
	cmp	r0, #0
	bne	.L1948
	ldrh	r2, [r4, #6]
	add	r1, r4, #8
	ldrh	r0, [r4, #4]
	bl	rk_sftl_vendor_write
	b	.L1939
.L1933:
	movs	r1, #8
.L1947:
	bl	__memzero
	b	.L1948
.L1935:
	mov	r1, r2
	mov	r0, r4
	b	.L1947
.L1937:
	mov	r5, #-1
	b	.L1923
.L1950:
	.align	2
.L1949:
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
	ldr	r0, .L1952
	b	misc_register
.L1953:
	.align	2
.L1952:
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
	.type	__func__.6748, %object
	.size	__func__.6748, 17
__func__.6748:
	.ascii	"INSERT_DATA_LIST\000"
	.type	__func__.6743, %object
	.size	__func__.6743, 17
__func__.6743:
	.ascii	"INSERT_FREE_LIST\000"
	.type	__func__.6786, %object
	.size	__func__.6786, 17
__func__.6786:
	.ascii	"List_remove_node\000"
	.type	__func__.6818, %object
	.size	__func__.6818, 22
__func__.6818:
	.ascii	"List_update_data_list\000"
	.type	__func__.6929, %object
	.size	__func__.6929, 22
__func__.6929:
	.ascii	"select_l2p_ram_region\000"
	.type	__func__.7252, %object
	.size	__func__.7252, 16
__func__.7252:
	.ascii	"make_superblock\000"
	.type	__func__.7427, %object
	.size	__func__.7427, 19
__func__.7427:
	.ascii	"get_new_active_ppa\000"
	.type	__func__.17765, %object
	.size	__func__.17765, 15
__func__.17765:
	.ascii	"FlashReadPages\000"
	.type	__func__.17782, %object
	.size	__func__.17782, 15
__func__.17782:
	.ascii	"FlashProgPages\000"
	.type	__func__.6860, %object
	.size	__func__.6860, 26
__func__.6860:
	.ascii	"ftl_map_blk_alloc_new_blk\000"
	.type	__func__.6279, %object
	.size	__func__.6279, 11
__func__.6279:
	.ascii	"FtlMemInit\000"
	.type	__func__.6583, %object
	.size	__func__.6583, 14
__func__.6583:
	.ascii	"FtlBbt2Bitmap\000"
	.type	__func__.6890, %object
	.size	__func__.6890, 31
__func__.6890:
	.ascii	"Ftl_write_map_blk_to_last_page\000"
	.type	__func__.6904, %object
	.size	__func__.6904, 16
__func__.6904:
	.ascii	"FtlMapWritePage\000"
	.type	__func__.6827, %object
	.size	__func__.6827, 16
__func__.6827:
	.ascii	"load_l2p_region\000"
	.type	__func__.6875, %object
	.size	__func__.6875, 15
__func__.6875:
	.ascii	"ftl_map_blk_gc\000"
	.type	__func__.6946, %object
	.size	__func__.6946, 9
__func__.6946:
	.ascii	"log2phys\000"
	.type	__func__.7161, %object
	.size	__func__.7161, 16
__func__.7161:
	.ascii	"FtlReUsePrevPpa\000"
	.type	__func__.7314, %object
	.size	__func__.7314, 14
__func__.7314:
	.ascii	"ftl_check_vpc\000"
	.type	__func__.7041, %object
	.size	__func__.7041, 14
__func__.7041:
	.ascii	"FtlScanSysBlk\000"
	.type	__func__.7273, %object
	.size	__func__.7273, 18
__func__.7273:
	.ascii	"SupperBlkListInit\000"
	.type	__func__.7399, %object
	.size	__func__.7399, 25
__func__.7399:
	.ascii	"allocate_data_superblock\000"
	.type	__func__.7440, %object
	.size	__func__.7440, 16
__func__.7440:
	.ascii	"update_vpc_list\000"
	.type	__func__.7447, %object
	.size	__func__.7447, 20
__func__.7447:
	.ascii	"decrement_vpc_count\000"
	.type	__func__.7195, %object
	.size	__func__.7195, 22
__func__.7195:
	.ascii	"FtlRecoverySuperblock\000"
	.type	__func__.6626, %object
	.size	__func__.6626, 11
__func__.6626:
	.ascii	"FtlLoadBbt\000"
	.type	__func__.7019, %object
	.size	__func__.7019, 15
__func__.7019:
	.ascii	"FtlVpcTblFlush\000"
	.type	__func__.7298, %object
	.size	__func__.7298, 21
__func__.7298:
	.ascii	"FtlVpcCheckAndModify\000"
	.type	__func__.7420, %object
	.size	__func__.7420, 29
__func__.7420:
	.ascii	"allocate_new_data_superblock\000"
	.type	__func__.6403, %object
	.size	__func__.6403, 13
__func__.6403:
	.ascii	"FtlProgPages\000"
	.type	__func__.7518, %object
	.size	__func__.7518, 19
__func__.7518:
	.ascii	"FtlGcFreeTempBlock\000"
	.type	__func__.7624, %object
	.size	__func__.7624, 23
__func__.7624:
	.ascii	"rk_ftl_garbage_collect\000"
	.type	__func__.6431, %object
	.size	__func__.6431, 9
__func__.6431:
	.ascii	"FtlWrite\000"
	.type	__func__.7098, %object
	.size	__func__.7098, 15
__func__.7098:
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
